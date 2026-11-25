// Helper -> service IPC framing: JSON lines plus opaque screen frame records (screen Step 59).
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

#include "xg/ipc_framing.hpp"

using namespace xg;

namespace {
std::string header(uint8_t type, uint32_t len) {
    return {char(0), char(type), char(len >> 24), char((len >> 16) & 0xff), char((len >> 8) & 0xff), char(len & 0xff)};
}
}  // namespace

TEST_CASE("IPC stream reader splits lines and screen frames, also when split across reads", "[ipc]") {
    const std::string frame = std::string("\x01\x02\n\x00\xff", 5) + std::string(1000, 'x');
    const std::string stream = "{\"t\":\"ping\"}\n" + ipc::screen_frame_record(frame) + "{\"t\":\"window\"}\n";
    for (size_t chunk : {size_t(1), size_t(3), size_t(7), stream.size()}) {
        ipc::StreamReader r;
        std::vector<ipc::Record> out;
        for (size_t i = 0; i < stream.size(); i += chunk)
            REQUIRE(r.feed(stream.data() + i, std::min(chunk, stream.size() - i), out));
        REQUIRE(out.size() == 3);
        CHECK(out[0].kind == ipc::Record::Kind::line);
        CHECK(out[0].data == "{\"t\":\"ping\"}");
        CHECK(out[1].kind == ipc::Record::Kind::screen_frame);
        CHECK(out[1].data == frame);  // bytes forwarded untouched, never decoded
        CHECK(out[2].data == "{\"t\":\"window\"}");
        CHECK(r.buffered() == 0);
    }
}

TEST_CASE("IPC stream reader rejects oversize, empty and unknown-type binary records", "[ipc]") {
    std::vector<ipc::Record> out;
    SECTION("oversize is rejected from the header, before the payload arrives") {
        ipc::StreamReader r;
        const auto h = header(ipc::kScreenFrame, uint32_t(ipc::kMaxScreenFrame + 1));
        CHECK_FALSE(r.feed(h.data(), h.size(), out));
        CHECK(r.buffered() == 0);
        CHECK_FALSE(r.feed("{}\n", 3, out));  // stays failed: the stream cannot be resynchronised
        CHECK(out.empty());
    }
    SECTION("exactly the cap is accepted") {
        ipc::StreamReader r;
        const std::string rec = ipc::screen_frame_record(std::string(ipc::kMaxScreenFrame, 'a'));
        REQUIRE(r.feed(rec.data(), rec.size(), out));
        REQUIRE(out.size() == 1);
        CHECK(out[0].data.size() == ipc::kMaxScreenFrame);
    }
    SECTION("wrong type") {
        ipc::StreamReader r;
        const auto h = header(0x02, 4) + "abcd";
        CHECK_FALSE(r.feed(h.data(), h.size(), out));
        CHECK(out.empty());
    }
    SECTION("zero length") {
        ipc::StreamReader r;
        const auto h = header(ipc::kScreenFrame, 0);
        CHECK_FALSE(r.feed(h.data(), h.size(), out));
    }
    SECTION("lines before the bad record are still delivered") {
        ipc::StreamReader r;
        const auto s = "{\"t\":\"ping\"}\n" + header(0x7f, 1);
        CHECK_FALSE(r.feed(s.data(), s.size(), out));
        REQUIRE(out.size() == 1);
        CHECK(out[0].data == "{\"t\":\"ping\"}");
    }
    CHECK(ipc::screen_frame_record("").empty());
    CHECK(ipc::screen_frame_record(std::string(ipc::kMaxScreenFrame + 1, 'a')).empty());
}

#ifndef _WIN32
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <thread>

#include "linux_common.hpp"

namespace {
struct UnixIpcRig {
    std::string path = (std::filesystem::temp_directory_path() / ("xg-ipc-test-" + std::to_string(::getpid()) + ".sock")).string();
    std::unique_ptr<IIpcServer> server;
    std::mutex mu;
    std::vector<std::string> lines, frames;
    std::atomic<int> disconnects{0};

    explicit UnixIpcRig(const std::string& allowed_exe) {
        ::unsetenv("XG_IPC_ALLOW_ANY");
        server = lnx::make_ipc_server(allowed_exe);
        server->set_frame_handler([this](const IIpcServer::Peer&, std::string&& f) {
            std::lock_guard lk(mu);
            frames.push_back(std::move(f));
        });
        REQUIRE(server->start(path, [this](const IIpcServer::Peer&, const std::string& l) {
            std::lock_guard lk(mu);
            lines.push_back(l);
        }, [this](const IIpcServer::Peer&, bool up) { if (!up) ++disconnects; }));
    }
    ~UnixIpcRig() { server->stop(); std::filesystem::remove(path); }
    template <class F> bool wait(F done) {
        for (int i = 0; i < 300; ++i) {
            { std::lock_guard lk(mu); if (done()) return true; }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return false;
    }
};
std::string self_exe() { return std::filesystem::read_symlink("/proc/self/exe").string(); }
}  // namespace

TEST_CASE("Unix IPC server delivers screen frames opaque to the frame handler, lines to the line handler", "[ipc][linux]") {
    UnixIpcRig rig(self_exe());
    auto client = lnx::make_ipc_client();
    REQUIRE(client->connect(rig.path));
    const std::string frame(300 * 1024, '\0');  // larger than one read, all zero bytes
    REQUIRE(client->send("{\"t\":\"ping\"}\n"));
    REQUIRE(ipc::send_screen_frame(*client, frame));
    REQUIRE(client->send("{\"t\":\"window\"}\n"));
    REQUIRE(rig.wait([&] { return rig.lines.size() == 2 && rig.frames.size() == 1; }));
    CHECK(rig.frames[0] == frame);
    CHECK(rig.lines[1] == "{\"t\":\"window\"}");
    CHECK_FALSE(ipc::send_screen_frame(*client, std::string(ipc::kMaxScreenFrame + 1, 'a')));
}

TEST_CASE("Unix IPC server closes a helper that sends an oversize or unknown binary record", "[ipc][linux]") {
    UnixIpcRig rig(self_exe());
    for (uint8_t type : {ipc::kScreenFrame, uint8_t(0x02)}) {
        auto client = lnx::make_ipc_client();
        REQUIRE(client->connect(rig.path));
        const uint32_t len = type == ipc::kScreenFrame ? uint32_t(ipc::kMaxScreenFrame + 1) : 4;
        const int before = rig.disconnects.load();
        REQUIRE(client->send(header(type, len)));
        REQUIRE(rig.wait([&] { return rig.disconnects.load() == before + 1; }));
        CHECK_FALSE(client->receive(500).has_value());
        CHECK_FALSE(client->connected());
    }
    std::lock_guard lk(rig.mu);
    CHECK(rig.frames.empty());
}

TEST_CASE("Unix IPC server accepts screen frames only from the helper binary", "[ipc][linux]") {
    UnixIpcRig rig("/nonexistent/xephyguard-helper");  // this test process is not the helper
    auto client = lnx::make_ipc_client();
    REQUIRE(client->connect(rig.path));  // accept() succeeds, then the server checks the peer and closes
    ipc::send_screen_frame(*client, "frame");
    CHECK_FALSE(client->receive(1000).has_value());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::lock_guard lk(rig.mu);
    CHECK(rig.frames.empty());
    CHECK(rig.lines.empty());
}
#endif
