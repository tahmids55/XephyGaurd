#include "xg/uplink.hpp"

#include <algorithm>
#include <thread>

#include "xg/json_read.hpp"
#include "xg/log.hpp"
#include "xg/pusher.hpp"
#include "xg/util.hpp"

namespace xg {

namespace {
constexpr int64_t kMinPolicyFetchGapMs = 2000;  // never refetch in a tight loop (e.g. an invalid policy)
}

net::TlsOptions tls_options_for(const UplinkView& v, bool with_cert) {
    net::TlsOptions o;
    o.ca_file = v.ca_file;
    if (with_cert) {
        o.cert_file = v.cert_file;
        o.key_file = v.key_file;
    }
    o.expected_host = v.ccc_host;
    o.timeout_ms = 10000;
    return o;
}

void Uplink::wake() {
    {
        std::lock_guard lk(wake_mu_);
        wake_ = true;
    }
    wake_cv_.notify_all();
}

net::HttpsClient& Uplink::api(const UplinkView& v) {
    const std::string key = v.ccc_host + ":" + std::to_string(v.api_port) + ":" + v.token;
    if (!api_ || key != api_key_) {
        api_ = std::make_unique<net::HttpsClient>(v.ccc_host, v.api_port, tls_options_for(v, true));
        api_->set_bearer(v.token);
        api_key_ = key;
    }
    return *api_;
}

void Uplink::poll_enrollment(const UplinkView& v) {
    if (v.enrollment_id.empty()) {
        if (!warned_not_enrolled_) {
            log::warn("agent is not enrolled: run `xephyguard-agent enroll --key <lab key>`");
            warned_not_enrolled_ = true;
        }
        return;
    }
    if (enroll_posted_for_ == v.enrollment_id) return;  // the core is applying it
    net::HttpsClient c(v.ccc_host, v.api_port, tls_options_for(v, false));
    auto r = c.request("GET", std::string(protocol::kEnrollPath) + "/" + v.enrollment_id + "?poll_token=" + v.poll_token);
    if (!r.ok()) return;
    auto j = json::parse(r.body, nullptr, false);
    const auto st = jr::str(j, "status");
    msg::EnrollApproved a{jr::str(j, "cert_pem"), jr::str(j, "ca_pem"), jr::str(j, "token"), jr::i64(j, "pc_id", 0, 1)};
    if (st == "approved" && !a.cert_pem.empty() && !a.token.empty() && a.pc_id > 0) {
        if (core_.post(std::move(a))) enroll_posted_for_ = v.enrollment_id;
        return;
    }
    if (st == "rejected") log::error("enrollment was rejected by the CCC administrator");
}

bool Uplink::fetch_policy(const UplinkView& v) {
    auto r = api(v).request("GET", protocol::kPolicyPath);
    if (!r.ok()) {
        log::warn("policy: fetch failed ({} {})", r.status, r.error);
        return false;
    }
    auto j = json::parse(r.body, nullptr, false);
    if (j.is_discarded() || !j.is_object() || !j.contains("policy") || !j["policy"].is_object()) {
        log::warn("policy: response is not a policy object");
        return false;
    }
    return core_.post(msg::PolicyFetched{std::move(j)});
}

bool Uplink::flush_once() {
    const auto v = view_.get();
    if (!v->enrolled) return false;
    const auto events = queue_ ? queue_->peek(protocol::kMaxBatchEvents) : std::vector<Event>{};
    json hb = v->heartbeat;
    if (hb.is_object()) {
        hb["local_time_ms"] = now_ms();
        if (hb.contains("usage") && hb["usage"].is_object()) hb["usage"]["bytes_up"] = status_.bytes_up.load();
        hb["queue_depth"] = queue_ ? queue_->size() : 0;
    }
    size_t included = 0;
    const std::string s = protocol::build_batch(v->pc_id, events, hb, &included).dump();
    const int64_t t0 = now_ms();
    auto r = api(*v).request("POST", protocol::kEventsPath, s);
    const int64_t t1 = now_ms();
    if (!r.ok()) {
        if (r.status == 401 || r.status == 403) log::error("uplink: CCC rejected our credentials ({})", r.status);
        else log::debug("uplink: send failed ({} {})", r.status, r.error);
        return false;
    }
    status_.bytes_up += s.size();
    std::string err;
    auto ack = protocol::parse_batch_ack(r.body, &err);
    if (!ack) {
        log::warn("uplink: bad ack: {}", err);
        return false;
    }
    if (queue_ && ack->ack_seq > 0) queue_->ack(ack->ack_seq);
    core_.post(msg::AckReceived{*ack, t0, t1});
    return true;
}

void Uplink::run(const std::atomic<bool>& stop) {
    int fail_backoff_s = 1;
    while (!stop) {
        int wait_ms = 5000;
        try {
            const auto v = view_.get();
            if (!v->enrolled) {
                poll_enrollment(*v);
            } else {
                const int64_t now = steady_ms();
                if (policy_dirty_ && now - last_policy_fetch_ >= kMinPolicyFetchGapMs) {
                    last_policy_fetch_ = now;
                    if (fetch_policy(*v)) policy_dirty_ = false;
                }
                const bool ok = flush_once();
                if (ok != status_.online.load())
                    log::info("uplink: CCC {}", ok ? "reachable" : "unreachable (buffering locally)");
                status_.online = ok;
                if (ok) {
                    fail_backoff_s = 1;
                    // A new policy announced in the ack is fetched right away (realtime may be unavailable).
                    const int64_t gap = last_policy_fetch_ + kMinPolicyFetchGapMs - steady_ms();
                    wait_ms = policy_dirty_ ? int(std::max<int64_t>(0, gap))
                              : (queue_ && queue_->size() > 0) ? 200
                                                               : v->heartbeat_s * 1000;
                } else {
                    wait_ms = fail_backoff_s * 1000;
                    fail_backoff_s = std::min(fail_backoff_s * 2, 30);
                }
            }
        } catch (const std::exception& e) {
            // Server data must never terminate the root service (I-AGENT-13).
            log::error("uplink: {}", e.what());
            status_.online = false;
        }
        std::unique_lock lk(wake_mu_);
        wake_cv_.wait_for(lk, std::chrono::milliseconds(wait_ms), [&] { return stop.load() || wake_; });
        wake_ = false;
    }
}

void Realtime::run(const std::atomic<bool>& stop) {
    while (!stop) {
        const auto v = view_.get();
        if (!v->enrolled || !v->realtime.is_object() || jr::str(v->realtime, "app_key").empty()) {
            for (int i = 0; i < 20 && !stop; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        net::PusherConfig pc;
        pc.host = v->ccc_host;
        // Reverb is served by the same nginx endpoint as the API, so reuse the port the agent already
        // reaches the CCC on (the server cannot know about NAT / port forwarding in between).
        pc.port = v->api_port;
        pc.app_key = jr::str(v->realtime, "app_key");
        pc.channel = jr::str(v->realtime, "channel", "private-pc." + v->pc_id);
        pc.auth_path = jr::str(v->realtime, "auth_path", protocol::kBroadcastAuthPath);
        net::HttpsClient api(v->ccc_host, v->api_port, tls_options_for(*v, true));
        api.set_bearer(v->token);
        net::PusherClient client(pc, tls_options_for(*v, true), api, [this](const std::string& event, const json& data) {
            if (event != "command") return;
            try {
                std::string err;
                if (auto cmd = protocol::parse_command(data, &err)) {
                    if (!core_.post(msg::CommandMsg{std::move(*cmd)})) log::error("realtime: core busy, command dropped");
                } else {
                    log::warn("realtime: rejected command: {}", err);
                }
            } catch (const std::exception& e) {
                log::error("realtime: command failed: {}", e.what());
            }
        });
        std::atomic<bool> session_stop{false};
        std::thread watcher([&] {
            while (!stop && !session_stop) {
                status_.realtime_connected = client.connected();
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
        });
        try {
            client.run(stop);
        } catch (const std::exception& e) {
            log::error("realtime: {}", e.what());
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }
        session_stop = true;
        watcher.join();
        status_.realtime_connected = false;
    }
}

}  // namespace xg
