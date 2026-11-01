#include "xg/pusher.hpp"

#include <algorithm>
#include <thread>

#include "xg/crypto.hpp"
#include "xg/log.hpp"
#include "xg/util.hpp"

namespace xg::net {

std::string pusher_auth_signature(const std::string& key, const std::string& secret, const std::string& socket_id,
                                  const std::string& channel) {
    return key + ":" + crypto::hmac_sha256_hex(secret, socket_id + ":" + channel);
}

PusherClient::PusherClient(PusherConfig cfg, TlsOptions tls, HttpsClient& api, Handler h)
    : cfg_(std::move(cfg)), tls_(std::move(tls)), api_(api), handler_(std::move(h)) {}

void PusherClient::run(const std::atomic<bool>& stop) {
    int backoff_s = 1;
    while (!stop) {
        const int64_t started = steady_ms();
        session(stop);
        connected_ = false;
        if (stop) break;
        if (steady_ms() - started > 60000) backoff_s = 1;  // healthy session -> reset backoff
        for (int i = 0; i < backoff_s * 10 && !stop; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
        backoff_s = std::min(backoff_s * 2, 60);
    }
}

bool PusherClient::session(const std::atomic<bool>& stop) {
    WebSocketClient ws;
    std::string err;
    const std::string path = "/app/" + cfg_.app_key + "?protocol=7&client=xephyguard-agent&version=" XG_VERSION;
    if (!ws.connect(cfg_.host, cfg_.port, path, tls_, &err)) {
        log::debug("realtime: connect failed: {}", err);
        return false;
    }
    // 1. connection_established -> socket_id
    std::string socket_id;
    if (auto m = ws.receive(10000)) {
        auto j = json::parse(*m, nullptr, false);
        if (!j.is_discarded() && j.value("event", "") == "pusher:connection_established") {
            auto d = json::parse(j.value("data", "{}"), nullptr, false);
            if (!d.is_discarded()) socket_id = d.value("socket_id", "");
        }
    }
    if (socket_id.empty()) { log::warn("realtime: no socket id from server"); return false; }
    // 2. sign the private channel subscription via the CCC API (authenticated by mTLS + token)
    json body{{"socket_id", socket_id}, {"channel_name", cfg_.channel}};
    auto r = api_.request("POST", cfg_.auth_path, body.dump());
    if (!r.ok()) { log::warn("realtime: channel auth failed ({} {})", r.status, r.error); return false; }
    auto aj = json::parse(r.body, nullptr, false);
    if (aj.is_discarded() || !aj.contains("auth")) { log::warn("realtime: bad auth response"); return false; }
    json sub{{"event", "pusher:subscribe"}, {"data", {{"auth", aj["auth"]}, {"channel", cfg_.channel}}}};
    ws.send_text(sub.dump());
    log::info("realtime: subscribed to {}", cfg_.channel);
    connected_ = true;
    int64_t last_rx = steady_ms(), last_ping = steady_ms();
    while (!stop && ws.open()) {
        auto m = ws.receive(1000);
        const int64_t now = steady_ms();
        if (m) {
            last_rx = now;
            auto j = json::parse(*m, nullptr, false);
            if (j.is_discarded()) continue;
            const std::string ev = j.value("event", "");
            if (ev == "pusher:ping") { ws.send_text(R"({"event":"pusher:pong","data":{}})"); continue; }
            if (ev == "pusher:pong" || ev == "pusher_internal:subscription_succeeded") continue;
            if (ev == "pusher:error") { log::warn("realtime: server error {}", j.dump()); continue; }
            json data = j.contains("data") ? j["data"] : json::object();
            if (data.is_string()) {
                auto parsed = json::parse(data.get<std::string>(), nullptr, false);
                data = parsed.is_discarded() ? json::object() : parsed;
            }
            if (handler_) handler_(ev, data);
        }
        if (now - last_ping > 30000) { ws.send_text(R"({"event":"pusher:ping","data":{}})"); last_ping = now; }
        if (now - last_rx > 120000) { log::warn("realtime: connection idle, reconnecting"); break; }
    }
    ws.close();
    return true;
}

}  // namespace xg::net
