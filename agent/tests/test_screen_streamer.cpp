// Helper-side streamer after screen Step 61: no network, frames to a callback (the IPC channel),
// capture driven only by gateway messages relayed by the service.
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <mutex>
#include <optional>
#include <random>
#include <thread>
#include <turbojpeg.h>

#include "agent_state.h"
#include "fake_capture.h"
#include "protocol.h"
#include "screen_streamer.hpp"

using namespace xg;

namespace {
std::string control(MsgType type, uint16_t flags, uint32_t stream_id) {
    Message m{};
    m.type = type;
    m.flags = flags;
    m.stream_id = stream_id;
    const auto v = encode(m);
    return {v.begin(), v.end()};
}

// A gateway Status message: `viewers` in flags, view mode code in the reserved field (1 live, 2 snapshot, 3 wall).
std::string status(uint16_t viewers, uint16_t mode, uint32_t stream_id) {
    auto m = control(MsgType::Status, viewers, stream_id);
    m[26] = static_cast<char>(mode & 0xff);
    m[27] = static_cast<char>(mode >> 8);
    return m;
}

struct Rig {
    ScreenStreamer s;
    std::mutex mu;
    std::vector<std::string> frames;
    std::atomic<int> viewing{-1};
    std::mutex wmu;
    std::optional<ScreenWindow> win = ScreenWindow{"editor", "main.cpp"};  // what the "window monitor" reports
    explicit Rig(int w = 320, int h = 240) : s([w, h] { return std::make_shared<FakeCapture>(w, h); }) {
        s.grant_lease(60);  // most tests are about what happens inside a CCC lease
        s.set_window_source([this] {
            std::lock_guard lk(wmu);
            return win;
        });
    }
    void set_window(std::optional<ScreenWindow> w) {
        std::lock_guard lk(wmu);
        win = std::move(w);
    }
    // 1 = a MASKED placeholder was sent, 2 = a real (unmasked) frame was sent, 0 = nothing yet.
    int kinds() {
        std::lock_guard lk(mu);
        int k = 0;
        for (const auto& f : frames)
            if (auto m = decode(reinterpret_cast<const uint8_t*>(f.data()), f.size()); m && m->type == MsgType::Frame)
                k |= (m->flags & Flags::MASKED) ? 1 : 2;
        return k;
    }
    bool start(uint32_t id) {
        return s.start(id, [this](std::string_view f) {
            std::lock_guard lk(mu);
            frames.emplace_back(f);
            return true;
        }, [this](bool v) { viewing = v ? 1 : 0; });
    }
    size_t count() { std::lock_guard lk(mu); return frames.size(); }
    // Width of the newest real frame (0 if none yet).
    int last_width() {
        std::lock_guard lk(mu);
        for (auto it = frames.rbegin(); it != frames.rend(); ++it)
            if (auto m = decode(reinterpret_cast<const uint8_t*>(it->data()), it->size()); m && m->type == MsgType::Frame && m->width > 0)
                return m->width;
        return 0;
    }
    template <class F> static bool eventually(F f) {
        for (int i = 0; i < 300; ++i) {
            if (f()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return false;
    }
};
}  // namespace

TEST_CASE("helper streamer stays idle until the gateway asks, then sends frames to the IPC callback", "[screen]") {
    Rig r;
    REQUIRE(r.start(42));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK_FALSE(r.s.is_capturing());
    CHECK(r.count() == 0);  // armed, but nobody is viewing: no capture, no frames

    r.s.on_gateway_message(control(MsgType::KeyframeReq, 0, 42));
    REQUIRE(Rig::eventually([&] { return r.count() > 0; }));
    CHECK(r.viewing == 1);
    {
        std::lock_guard lk(r.mu);
        auto m = decode(reinterpret_cast<const uint8_t*>(r.frames[0].data()), r.frames[0].size());
        REQUIRE(m);
        CHECK(m->stream_id == 42);
    }

    r.s.on_gateway_message(control(MsgType::Status, 0, 42));  // last viewer left
    CHECK_FALSE(r.s.is_capturing());
    CHECK(r.viewing == 0);
}

TEST_CASE("helper streamer stops capturing when the service link is lost and ignores garbage", "[screen]") {
    Rig r;
    REQUIRE(r.start(42));
    r.s.on_gateway_message(std::string("\x01\x02\x03", 3));  // malformed: ignored, no capture
    CHECK_FALSE(r.s.is_capturing());
    r.s.on_gateway_message(control(MsgType::Status, 1, 42));
    REQUIRE(Rig::eventually([&] { return r.s.is_capturing(); }));
    r.s.on_link_lost();
    CHECK_FALSE(r.s.is_capturing());
    CHECK(r.viewing == 0);
    r.s.stop();
    CHECK_FALSE(r.s.is_running());
    CHECK_FALSE(r.start(0));  // no stream id, no streamer
}

TEST_CASE("privacy mask: no window source or an unknown window masks the picture (fail-safe)", "[screen][privacy]") {
    Rig r;
    r.set_window(std::nullopt);  // the helper has no window report yet
    REQUIRE(r.start(42));
    r.s.on_gateway_message(control(MsgType::KeyframeReq, 0, 42));
    REQUIRE(Rig::eventually([&] { return r.kinds() != 0; }));
    CHECK(r.kinds() == 1);

    ScreenStreamer bare([] { return std::make_shared<FakeCapture>(320, 240); });  // never given a window source
    bare.grant_lease(60);
    std::mutex mu;
    int masked = 0, real = 0;
    REQUIRE(bare.start(7, [&](std::string_view f) {
        auto m = decode(reinterpret_cast<const uint8_t*>(f.data()), f.size());
        std::lock_guard lk(mu);
        if (m && m->type == MsgType::Frame) ((m->flags & Flags::MASKED) ? masked : real)++;
        return true;
    }));
    bare.on_gateway_message(control(MsgType::KeyframeReq, 0, 7));
    REQUIRE(Rig::eventually([&] { std::lock_guard lk(mu); return masked > 0; }));
    std::lock_guard lk(mu);
    CHECK(real == 0);
}

TEST_CASE("privacy mask: a matching window is masked, leaving it lifts the mask", "[screen][privacy]") {
    Rig r;
    r.s.set_mask_rules({{"keepassxc", ""}, {"", "private|incognito"}});
    r.set_window(ScreenWindow{"KeePassXC", "Passwords"});  // app name compares case-insensitively
    REQUIRE(r.start(42));
    r.s.on_gateway_message(control(MsgType::KeyframeReq, 0, 42));
    REQUIRE(Rig::eventually([&] { return r.kinds() != 0; }));
    CHECK(r.kinds() == 1);

    r.set_window(ScreenWindow{"firefox", "Hello - Incognito window"});  // title regex rule
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    CHECK(r.kinds() == 1);

    r.set_window(ScreenWindow{"editor", "main.cpp"});
    REQUIRE(Rig::eventually([&] { return (r.kinds() & 2) != 0; }));
}

TEST_CASE("privacy mask: an invalid rule regex masks (fail-safe); rules can change on a running stream", "[screen][privacy]") {
    Rig r;
    r.s.set_mask_rules({{"", "([unclosed"}});
    REQUIRE(r.start(42));
    r.s.on_gateway_message(control(MsgType::KeyframeReq, 0, 42));
    REQUIRE(Rig::eventually([&] { return r.kinds() != 0; }));
    CHECK(r.kinds() == 1);
    r.s.set_mask_rules({});  // the bad list is replaced
    REQUIRE(Rig::eventually([&] { return (r.kinds() & 2) != 0; }));
}

TEST_CASE("streamer follows the mode in the gateway's Status: wall sends thumbnails, live full size, and it switches", "[screen]") {
    Rig r(640, 480);
    REQUIRE(r.start(42));
    r.s.on_gateway_message(status(1, 3, 42));  // one wall viewer
    REQUIRE(Rig::eventually([&] { return r.last_width() > 0; }));
    CHECK(r.last_width() == 320);

    r.s.on_gateway_message(status(2, 1, 42));  // a live viewer joined: the most demanding mode wins
    REQUIRE(Rig::eventually([&] { return r.last_width() == 640; }));

    r.s.on_gateway_message(status(0, 0, 42));  // everyone left
    CHECK_FALSE(r.s.is_capturing());
}

TEST_CASE("streamer treats a Status without a mode as live, and a snapshot Status starts capture", "[screen]") {
    Rig r(640, 480);
    REQUIRE(r.start(42));
    r.s.on_gateway_message(status(1, 0, 42));  // an older gateway sends 0
    REQUIRE(Rig::eventually([&] { return r.last_width() == 640; }));
    r.s.on_gateway_message(status(0, 0, 42));
    REQUIRE(Rig::eventually([&] { return !r.s.is_capturing(); }));
    r.s.on_gateway_message(status(1, 2, 42));  // snapshot
    REQUIRE(Rig::eventually([&] { return r.s.is_capturing(); }));
}

TEST_CASE("streamer never starts without a CCC lease and stops by itself when the lease lapses", "[screen][lease]") {
    ScreenStreamer s([] { return std::make_shared<FakeCapture>(320, 240); });
    s.set_window_source([] { return ScreenWindow{"editor", "main.cpp"}; });
    std::atomic<int> sent{0};
    REQUIRE(s.start(42, [&](std::string_view) { ++sent; return true; }));
    s.on_gateway_message(control(MsgType::KeyframeReq, 0, 42));  // a gateway request alone
    s.on_gateway_message(status(1, 1, 42));
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    CHECK_FALSE(s.is_capturing());
    CHECK(sent == 0);

    s.grant_lease(1);
    s.on_gateway_message(status(1, 1, 42));
    REQUIRE(Rig::eventually([&] { return s.is_capturing(); }));
    s.tick();
    CHECK(s.is_capturing());  // lease still running
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    s.tick();
    CHECK_FALSE(s.is_capturing());
    s.on_gateway_message(control(MsgType::KeyframeReq, 0, 42));  // lapsed: no restart either
    CHECK_FALSE(s.is_capturing());

    s.grant_lease(30);
    s.on_gateway_message(control(MsgType::KeyframeReq, 0, 42));
    REQUIRE(Rig::eventually([&] { return s.is_capturing(); }));
    s.on_link_lost();  // the service link dropped: the lease is gone with it
    s.on_gateway_message(control(MsgType::KeyframeReq, 0, 42));
    CHECK_FALSE(s.is_capturing());
}

TEST_CASE("streamer encodes JPEG by default and WebP after set_encoding, from the next view on", "[screen]") {
    auto first_codec = [](Rig& r) -> int {
        std::lock_guard lk(r.mu);
        for (const auto& f : r.frames)
            if (auto m = decode(reinterpret_cast<const uint8_t*>(f.data()), f.size()); m && m->type == MsgType::Frame && !m->tiles.empty())
                return static_cast<int>(m->tiles[0].codec);
        return -1;
    };
    Rig r;
    REQUIRE(r.start(9));
    r.s.on_gateway_message(control(MsgType::KeyframeReq, 0, 9));
    REQUIRE(Rig::eventually([&] { return first_codec(r) >= 0; }));
    CHECK(first_codec(r) == static_cast<int>(Codec::Jpeg));

    r.s.on_gateway_message(control(MsgType::Status, 0, 9));  // view ends
    r.s.set_encoding(ScreenEncoding{true, 70, 0});
    { std::lock_guard lk(r.mu); r.frames.clear(); }
    r.s.on_gateway_message(control(MsgType::KeyframeReq, 0, 9));
    REQUIRE(Rig::eventually([&] { return first_codec(r) >= 0; }));
    CHECK(first_codec(r) == static_cast<int>(Codec::Webp));
    r.s.stop();
}

TEST_CASE("streamer passes the CPU budget to the running view's governor", "[screen]") {
    Rig r;
    REQUIRE(r.start(11));
    r.s.set_cpu_budget_pct(15);
    CHECK(r.s.stream_stats().budget_pct == 15.0);
    r.s.set_cpu_budget_pct(500);  // clamped
    CHECK(r.s.stream_stats().budget_pct == 100.0);
    CHECK(r.s.cost().level == r.s.stream_stats().level);  // heartbeat cost mirrors the governor
    r.s.stop();
}

// ---------------------------------------------------------------------------- evidence screenshot (F123)
namespace {
// Width x height of a JPEG, {0,0} if it does not decode.
std::pair<int, int> jpeg_size(const std::string& j) {
    tjhandle tj = tjInitDecompress();
    int w = 0, h = 0, sub = 0, cs = 0;
    const bool ok = tjDecompressHeader3(tj, reinterpret_cast<const unsigned char*>(j.data()),
                                        static_cast<unsigned long>(j.size()), &w, &h, &sub, &cs) == 0;
    tjDestroy(tj);
    return ok ? std::pair{w, h} : std::pair{0, 0};
}
bool soi_eoi(const std::string& j) {
    return j.size() > 4 && static_cast<uint8_t>(j[0]) == 0xFF && static_cast<uint8_t>(j[1]) == 0xD8 &&
           static_cast<uint8_t>(j[j.size() - 2]) == 0xFF && static_cast<uint8_t>(j[j.size() - 1]) == 0xD9;
}
struct SecureCapture : FakeCapture {
    CaptureStatus capture(Frame&) override { return CaptureStatus::SecureDesktop; }
};
struct NoiseCapture : FakeCapture {  // incompressible picture: a JPEG of it is large
    NoiseCapture() : FakeCapture(1920, 1080) {}
    CaptureStatus capture(Frame& f) override {
        f.width = 1920; f.height = 1080; f.stride = 1920 * 4;
        f.bgra.resize(static_cast<size_t>(f.stride) * f.height);
        std::mt19937 rng(7);
        for (auto& b : f.bgra) b = static_cast<uint8_t>(rng());
        f.full_dirty = true;
        return CaptureStatus::Ok;
    }
};
}  // namespace

TEST_CASE("evidence shot: one full-frame JPEG without a lease or a running view", "[screen][evidence]") {
    ScreenStreamer s([] { return std::make_shared<FakeCapture>(320, 240); });
    s.set_window_source([] { return std::optional<ScreenWindow>{ScreenWindow{"editor", "main.cpp"}}; });
    const EvidenceShot shot = s.evidence_shot({});
    REQUIRE(shot.kind == EvidenceShot::Kind::jpeg);
    CHECK(soi_eoi(shot.jpeg));
    CHECK(jpeg_size(shot.jpeg) == std::pair{320, 240});
    CHECK_FALSE(s.is_capturing());  // the live-view path is untouched
}

TEST_CASE("evidence shot: a wide screen is scaled down to max_width, aspect kept", "[screen][evidence]") {
    ScreenStreamer s([] { return std::make_shared<FakeCapture>(2560, 1440); });
    s.set_window_source([] { return std::optional<ScreenWindow>{ScreenWindow{"editor", "main.cpp"}}; });
    EvidenceRequest req;
    req.max_width = 1920;
    const EvidenceShot shot = s.evidence_shot(req);
    REQUIRE(shot.kind == EvidenceShot::Kind::jpeg);
    CHECK(jpeg_size(shot.jpeg) == std::pair{1920, 1080});
    req.max_width = 10;  // clamped to 320
    CHECK(jpeg_size(s.evidence_shot(req).jpeg) == std::pair{320, 180});
}

TEST_CASE("evidence shot: unknown window, matching mask rule or bad regex give no image (fail-safe)", "[screen][evidence][privacy]") {
    ScreenStreamer bare([] { return std::make_shared<FakeCapture>(320, 240); });  // no window source
    CHECK(bare.evidence_shot({}).kind == EvidenceShot::Kind::masked);

    Rig r;
    r.set_window(std::nullopt);
    CHECK(r.s.evidence_shot({}).kind == EvidenceShot::Kind::masked);
    r.s.set_mask_rules({{"keepassxc", ""}});
    r.set_window(ScreenWindow{"KeePassXC", "Passwords"});
    const EvidenceShot m = r.s.evidence_shot({});
    CHECK(m.kind == EvidenceShot::Kind::masked);
    CHECK(m.jpeg.empty());
    r.s.set_mask_rules({{"", "([unclosed"}});
    r.set_window(ScreenWindow{"editor", "main.cpp"});
    CHECK(r.s.evidence_shot({}).kind == EvidenceShot::Kind::masked);
    r.s.set_mask_rules({});
    CHECK(r.s.evidence_shot({}).kind == EvidenceShot::Kind::jpeg);
}

TEST_CASE("evidence shot: a masked window that appears during the capture drops the picture", "[screen][evidence][privacy]") {
    ScreenStreamer s([] { return std::make_shared<FakeCapture>(320, 240); });
    s.set_mask_rules({{"keepassxc", ""}});
    auto calls = std::make_shared<std::atomic<int>>(0);
    s.set_window_source([calls] {  // first check (before capture): editor; second check (after): the password manager
        return std::optional<ScreenWindow>{(*calls)++ == 0 ? ScreenWindow{"editor", "x"} : ScreenWindow{"keepassxc", "y"}};
    });
    CHECK(s.evidence_shot({}).kind == EvidenceShot::Kind::masked);
    CHECK(calls->load() >= 2);
}

TEST_CASE("evidence shot: secure desktop is masked, no capture or an oversized picture fails", "[screen][evidence]") {
    auto win = [] { return std::optional<ScreenWindow>{ScreenWindow{"editor", "main.cpp"}}; };
    ScreenStreamer sec([] { return std::make_shared<SecureCapture>(); });
    sec.set_window_source(win);
    CHECK(sec.evidence_shot({}).kind == EvidenceShot::Kind::masked);

    ScreenStreamer none([] { return std::shared_ptr<ScreenCapture>{}; });
    none.set_window_source(win);
    const EvidenceShot n = none.evidence_shot({});
    CHECK(n.kind == EvidenceShot::Kind::failed);
    CHECK(n.reason == "capture_unavailable");

    ScreenStreamer noisy([] { return std::make_shared<NoiseCapture>(); });
    noisy.set_window_source(win);
    EvidenceRequest small;
    small.max_bytes = 1;  // clamped to 64 KiB, still far below a noise picture
    const EvidenceShot big = noisy.evidence_shot(small);
    CHECK(big.kind == EvidenceShot::Kind::failed);
    CHECK(big.reason == "too_large");
    const EvidenceShot ok = noisy.evidence_shot({});  // 4 MiB fits (q 70, or the q 30 retry)
    REQUIRE(ok.kind == EvidenceShot::Kind::jpeg);
    CHECK(ok.jpeg.size() <= 4u * 1024 * 1024);
}
