// Service-side screen relay: helper frames up to the gateway, gateway control messages down (screen Step 60).
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <thread>

#include "fakes.hpp"
#include "xg/crypto.hpp"
#include "xg/screen_relay.hpp"

using namespace xg;

namespace {
struct FakeLink : IScreenLink {
    std::mutex mu;
    std::vector<std::string> sent;
    std::deque<std::string> inbox;
    std::atomic<bool> is_open{true};
    std::atomic<bool> block{false};  // true: receive() waits its whole timeout like the real link, until interrupt()
    std::condition_variable cv;
    bool interrupted = false;
    bool send(const std::string& m) override { std::lock_guard lk(mu); sent.push_back(m); return is_open.load(); }
    std::optional<std::string> receive(int ms) override {
        {
            std::lock_guard lk(mu);
            if (!inbox.empty()) { auto m = inbox.front(); inbox.pop_front(); return m; }
        }
        if (block) {
            std::unique_lock lk(mu);
            cv.wait_for(lk, std::chrono::milliseconds(ms), [&] { return interrupted || !inbox.empty(); });
            if (interrupted) is_open = false;
            return std::nullopt;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(std::min(ms, 20)));
        return std::nullopt;
    }
    void interrupt() override { { std::lock_guard lk(mu); interrupted = true; } cv.notify_all(); }
    bool open() const override { return is_open.load(); }
    void close() override { is_open = false; }
};
// The relay owns and frees its link; the rig keeps the fake alive so the test can still inspect it.
struct LinkHandle : IScreenLink {
    std::shared_ptr<FakeLink> l;
    bool send(const std::string& m) override { return l->send(m); }
    std::optional<std::string> receive(int ms) override { return l->receive(ms); }
    bool open() const override { return l->open(); }
    void close() override { l->close(); }
    void interrupt() override { l->interrupt(); }
};

struct Rig {
    Snapshot<UplinkView> view;
    test::FakeIpc ipc;
    std::atomic<int> connects{0};
    std::atomic<FakeLink*> link{nullptr};
    std::vector<std::shared_ptr<FakeLink>> links;
    std::unique_ptr<ScreenRelay> relay;
    std::atomic<bool> stop{false};
    std::atomic<bool> block_links{false};
    std::thread th;
    UplinkView seen;
    std::mutex mu;

    Rig() {
        UplinkView v;
        v.enrolled = true;
        v.ccc_host = "ccc.lab";
        v.pc_id = "42";
        view.set(v);
        relay = std::make_unique<ScreenRelay>(view, &ipc, [this](const UplinkView& uv, std::string*) {
            ++connects;
            auto h = std::make_unique<LinkHandle>();
            h->l = std::make_shared<FakeLink>();
            h->l->block = block_links.load();
            {
                std::lock_guard lk(mu);
                seen = uv;
                links.push_back(h->l);
            }
            link = h->l.get();
            return std::unique_ptr<IScreenLink>(std::move(h));
        });
        th = std::thread([this] { relay->run(stop); });
    }
    ~Rig() { stop = true; relay->request_stop(); th.join(); }
    template <class F> static bool eventually(F f) {
        for (int i = 0; i < 200; ++i) {
            if (f()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return false;
    }
    size_t sent_count() {
        auto* l = link.load();
        if (!l) return 0;
        std::lock_guard lk(l->mu);
        return l->sent.size();
    }
};
}  // namespace

TEST_CASE("screen relay stays disconnected until the core names a source helper", "[screen]") {
    Rig r;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK(r.connects == 0);
    r.relay->push(7, "frame");  // no source: dropped, never queued
    CHECK(r.relay->dropped() == 1);
    r.relay->set_source(7);
    REQUIRE(Rig::eventually([&] { return r.link.load() != nullptr; }));
    CHECK(r.connects == 1);
    std::lock_guard lk(r.mu);
    CHECK(r.seen.pc_id == "42");
}

TEST_CASE("screen relay forwards only the source helper's frames, unchanged", "[screen]") {
    Rig r;
    r.relay->grant_lease(60);
    r.relay->set_source(7);
    REQUIRE(Rig::eventually([&] { return r.link.load() != nullptr; }));
    REQUIRE(Rig::eventually([&] { r.relay->push(7, std::string("\x00\x01\xff", 3)); return r.sent_count() >= 1; }));
    const auto before = r.relay->dropped();
    r.relay->push(8, "other user's helper");  // wrong peer
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    CHECK(r.relay->dropped() == before + 1);
    // Streaming cost counters (heartbeat usage.screen) count only what the link accepted.
    CHECK(Rig::eventually([&] { return r.relay->sent_frames() == r.sent_count(); }));
    CHECK(r.relay->sent_bytes() == 3 * r.relay->sent_frames());
    std::lock_guard lk(r.link.load()->mu);
    for (const auto& m : r.link.load()->sent) CHECK(m == std::string("\x00\x01\xff", 3));
}

TEST_CASE("screen relay sends gateway messages to the source helper as screen_rx, capped", "[screen]") {
    Rig r;
    r.relay->grant_lease(60);
    r.relay->set_source(7);
    REQUIRE(Rig::eventually([&] { return r.link.load() != nullptr; }));
    {
        std::lock_guard lk(r.link.load()->mu);
        r.link.load()->inbox.push_back(std::string(28, '\x01'));
        r.link.load()->inbox.push_back(std::string(ScreenRelay::kMaxDownlink + 1, 'x'));  // oversize: dropped
    }
    const std::string b64 = crypto::base64_encode(std::string(28, '\x01'));
    REQUIRE(Rig::eventually([&] { return r.ipc.count_containing(b64) == 1; }));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK(r.ipc.count_containing("\"screen_rx\"") == 1);
}

TEST_CASE("screen relay reconnects after the link drops and closes it when the source goes away", "[screen]") {
    Rig r;
    r.relay->set_source(7);
    REQUIRE(Rig::eventually([&] { return r.connects == 1 && r.link.load(); }));
    r.link.load()->close();
    REQUIRE(Rig::eventually([&] { return r.connects == 2; }));  // after the 1 s backoff
    auto* l = r.link.load();
    r.relay->set_source(0);
    REQUIRE(Rig::eventually([&] { return !l->open(); }));
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    CHECK(r.connects == 2);
}

TEST_CASE("screen relay backoff grows from 1 s and is capped at 30 s", "[screen]") {
    CHECK(ScreenRelay::next_backoff_ms(0) == 1000);
    CHECK(ScreenRelay::next_backoff_ms(1000) == 2000);
    CHECK(ScreenRelay::next_backoff_ms(16000) == 30000);
    CHECK(ScreenRelay::next_backoff_ms(30000) == 30000);
}

TEST_CASE("mTLS screen link refuses to connect without a numeric pc id and a client cert", "[screen]") {
    auto f = mtls_screen_link_factory();
    UplinkView v;
    v.enrolled = true;
    v.ccc_host = "127.0.0.1";
    v.pc_id = "42/../../api";
    v.cert_file = "c.pem";
    v.key_file = "k.pem";
    std::string err;
    CHECK(f(v, &err) == nullptr);
    v.pc_id = "42";
    v.cert_file.clear();
    CHECK(f(v, &err) == nullptr);
    CHECK_FALSE(err.empty());
}

TEST_CASE("screen relay passes nothing without a CCC lease, tells the helper the lease, and replays an early request", "[screen][lease]") {
    Rig r;
    r.relay->set_source(7);
    REQUIRE(Rig::eventually([&] { return r.link.load() != nullptr; }));
    CHECK_FALSE(r.relay->lease_active());
    const auto before = r.relay->dropped();
    r.relay->push(7, "frame");  // helper ignoring the lease rule: dropped
    CHECK(r.relay->dropped() == before + 1);

    std::string key(28, '\0');  // KeyframeReq (type 4) from the gateway, before any lease command
    key[3] = 4;
    {
        std::lock_guard lk(r.link.load()->mu);
        r.link.load()->inbox.push_back(key);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    CHECK(r.ipc.count_containing("\"screen_rx\"") == 0);  // nothing reached the helper

    r.relay->grant_lease(500);  // clamped to 120 s
    CHECK(r.relay->lease_active());
    REQUIRE(Rig::eventually([&] { return r.ipc.count_containing("\"screen_lease\"") == 1; }));
    CHECK(r.ipc.count_containing("\"ttl_s\":120") == 1);
    REQUIRE(Rig::eventually([&] { return r.ipc.count_containing(crypto::base64_encode(key)) == 1; }));  // replayed once
    r.relay->grant_lease(10);  // a renewal: told again, nothing replayed twice
    REQUIRE(Rig::eventually([&] { return r.ipc.count_containing("\"screen_lease\"") == 2; }));
    CHECK(r.ipc.count_containing("\"screen_rx\"") == 1);
}

TEST_CASE("an idle connected relay blocks in receive, and a source change or shutdown ends the link at once", "[screen][idle]") {
    using namespace std::chrono;
    auto t_end = [](auto t0) { return duration_cast<milliseconds>(steady_clock::now() - t0).count(); };
    {
        Rig r;
        r.block_links = true;  // the relay's receive(10000) would otherwise sit for 10 s
        r.relay->set_source(7);
        REQUIRE(Rig::eventually([&] { return r.link.load() != nullptr; }));
        std::this_thread::sleep_for(milliseconds(100));  // now blocked inside receive
        const auto t0 = steady_clock::now();
        r.relay->set_source(0);
        REQUIRE(Rig::eventually([&] { return !r.link.load()->open(); }));
        CHECK(t_end(t0) < 1000);
    }
    Rig r;  // shutdown while blocked: ~Rig joins the relay thread
    r.block_links = true;
    r.relay->set_source(7);
    REQUIRE(Rig::eventually([&] { return r.link.load() != nullptr; }));
    std::this_thread::sleep_for(milliseconds(100));
    const auto t0 = steady_clock::now();
    r.stop = true;
    r.relay->request_stop();
    r.th.join();
    CHECK(t_end(t0) < 1000);
    r.th = std::thread([] {});  // ~Rig joins again
}
