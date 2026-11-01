// Named-pipe IPC (\\.\pipe\XephyGuardHelper). The service verifies each client's image path and
// session; the pipe DACL allows authenticated users to connect but not to create server instances.
#include "win_common.hpp"  // must precede other Windows SDK headers
#include <sddl.h>
#include <wtsapi32.h>

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

#include "xg/ipc_messages.hpp"
#include "xg/log.hpp"
#include "xg/util.hpp"

namespace xg::win {
namespace {

bool overlapped_write(HANDLE h, const std::string& s) {
    OVERLAPPED ov{};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    DWORD n = 0;
    BOOL ok = WriteFile(h, s.data(), DWORD(s.size()), nullptr, &ov);
    if (!ok && GetLastError() == ERROR_IO_PENDING) ok = TRUE;
    if (ok) ok = GetOverlappedResult(h, &ov, &n, TRUE) && n == s.size();
    CloseHandle(ov.hEvent);
    return ok != FALSE;
}

class IpcServer final : public IIpcServer {
public:
    explicit IpcServer(std::string allowed) : allowed_(to_lower(std::move(allowed))) {}
    ~IpcServer() override { stop(); }

    bool start(const std::string& endpoint, std::function<void(const Peer&, const std::string&)> on_msg,
               std::function<void(const Peer&, bool)> on_peer) override {
        name_ = to_wide(endpoint);
        running_ = true;
        accept_ = std::thread([this, on_msg, on_peer] { accept_loop(on_msg, on_peer); });
        return true;
    }

    bool send(uint64_t id, const std::string& line) override {
        std::shared_ptr<Conn> c;
        {
            std::lock_guard lk(mu_);
            auto it = conns_.find(id);
            if (it == conns_.end()) return false;
            c = it->second;
        }
        std::lock_guard wl(c->write_mu);
        return overlapped_write(c->h, line);
    }

    void broadcast(const std::string& line) override {
        std::vector<std::shared_ptr<Conn>> all;
        {
            std::lock_guard lk(mu_);
            for (auto& [id, c] : conns_) all.push_back(c);
        }
        for (auto& c : all) {
            std::lock_guard wl(c->write_mu);
            overlapped_write(c->h, line);
        }
    }

    void stop() override {
        running_ = false;
        if (accept_.joinable()) accept_.join();
        std::vector<std::thread> readers;
        {
            std::lock_guard lk(mu_);
            for (auto& [id, c] : conns_) CancelIoEx(c->h, nullptr);
            readers.swap(readers_);
        }
        for (auto& t : readers) t.join();
        std::lock_guard lk(mu_);
        for (auto& [id, c] : conns_) CloseHandle(c->h);
        conns_.clear();
    }

private:
    struct Conn {
        HANDLE h;
        Peer peer;
        std::mutex write_mu;
    };

    void accept_loop(const std::function<void(const Peer&, const std::string&)>& on_msg,
                     const std::function<void(const Peer&, bool)>& on_peer) {
        PSECURITY_DESCRIPTOR sd = nullptr;
        // SYSTEM: full; Administrators: full; Authenticated Users: read/write (connect as client only).
        ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:(A;;GA;;;SY)(A;;GA;;;BA)(A;;GRGW;;;AU)", SDDL_REVISION_1,
                                                             &sd, nullptr);
        SECURITY_ATTRIBUTES sa{sizeof sa, sd, FALSE};
        while (running_) {
            HANDLE h = CreateNamedPipeW(name_.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                                        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                                        PIPE_UNLIMITED_INSTANCES, 64 * 1024, 64 * 1024, 0, &sa);
            if (h == INVALID_HANDLE_VALUE) { Sleep(1000); continue; }
            OVERLAPPED ov{};
            ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            BOOL connected = ConnectNamedPipe(h, &ov);
            DWORD err = GetLastError();
            if (!connected && err == ERROR_IO_PENDING) {
                while (running_ && WaitForSingleObject(ov.hEvent, 500) == WAIT_TIMEOUT) {}
                DWORD dummy;
                connected = running_ && GetOverlappedResult(h, &ov, &dummy, FALSE);
            } else if (!connected && err == ERROR_PIPE_CONNECTED) {
                connected = TRUE;
            }
            CloseHandle(ov.hEvent);
            if (!connected) { CancelIoEx(h, nullptr); CloseHandle(h); continue; }

            ULONG pid = 0, sid = 0;
            GetNamedPipeClientProcessId(h, &pid);
            ProcessIdToSessionId(pid, &sid);
            const std::string exe = to_lower(process_image(pid));
            if (exe != allowed_ && !std::getenv("XG_IPC_ALLOW_ANY")) {
                log::warn("ipc: rejected pipe client pid {} ({})", pid, exe);
                DisconnectNamedPipe(h);
                CloseHandle(h);
                continue;
            }
            auto c = std::make_shared<Conn>();
            c->h = h;
            c->peer.conn_id = ++next_id_;
            c->peer.pid = pid;
            c->peer.uid = sid;
            c->peer.user = process_user(pid);
            {
                std::lock_guard lk(mu_);
                conns_[c->peer.conn_id] = c;
                readers_.emplace_back([this, c, on_msg, on_peer] { read_loop(c, on_msg, on_peer); });
            }
            on_peer(c->peer, true);
        }
        if (sd) LocalFree(sd);
    }

    void read_loop(std::shared_ptr<Conn> c, const std::function<void(const Peer&, const std::string&)>& on_msg,
                   const std::function<void(const Peer&, bool)>& on_peer) {
        std::string buf;
        char tmp[8192];
        OVERLAPPED ov{};
        ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        while (running_) {
            ResetEvent(ov.hEvent);
            DWORD n = 0;
            BOOL ok = ReadFile(c->h, tmp, sizeof tmp, nullptr, &ov);
            if (!ok && GetLastError() != ERROR_IO_PENDING) break;
            while (running_ && WaitForSingleObject(ov.hEvent, 500) == WAIT_TIMEOUT) {}
            if (!running_ || !GetOverlappedResult(c->h, &ov, &n, FALSE) || n == 0) break;
            buf.append(tmp, n);
            size_t pos;
            while ((pos = buf.find('\n')) != std::string::npos) {
                on_msg(c->peer, buf.substr(0, pos));
                buf.erase(0, pos + 1);
            }
            if (buf.size() > ipc::kMaxLine) buf.clear();
        }
        CloseHandle(ov.hEvent);
        {
            std::lock_guard lk(mu_);
            conns_.erase(c->peer.conn_id);
        }
        DisconnectNamedPipe(c->h);
        CloseHandle(c->h);
        on_peer(c->peer, false);
    }

    std::string allowed_;
    std::wstring name_;
    std::atomic<bool> running_{false};
    std::thread accept_;
    std::mutex mu_;
    std::map<uint64_t, std::shared_ptr<Conn>> conns_;
    std::vector<std::thread> readers_;
    std::atomic<uint64_t> next_id_{0};
};

class IpcClient final : public IIpcClient {
public:
    ~IpcClient() override { close(); }
    bool connect(const std::string& endpoint) override {
        close();
        h_ = CreateFileW(to_wide(endpoint).c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                         FILE_FLAG_OVERLAPPED, nullptr);
        if (h_ == INVALID_HANDLE_VALUE) { h_ = nullptr; return false; }
        ev_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        return true;
    }
    bool send(const std::string& line) override {
        std::lock_guard lk(mu_);
        if (!h_) return false;
        return overlapped_write(h_, line);
    }
    std::optional<std::string> receive(int timeout_ms) override {
        if (!h_) return std::nullopt;
        if (auto l = pop()) return l;
        if (!pending_) {
            ov_ = OVERLAPPED{};
            ResetEvent(ev_);
            ov_.hEvent = ev_;
            if (!ReadFile(h_, tmp_, sizeof tmp_, nullptr, &ov_) && GetLastError() != ERROR_IO_PENDING) {
                close();
                return std::nullopt;
            }
            pending_ = true;
        }
        if (WaitForSingleObject(ev_, DWORD(timeout_ms)) != WAIT_OBJECT_0) return std::nullopt;
        DWORD n = 0;
        pending_ = false;
        if (!GetOverlappedResult(h_, &ov_, &n, FALSE) || n == 0) { close(); return std::nullopt; }
        buf_.append(tmp_, n);
        return pop();
    }
    bool connected() const override { return h_ != nullptr; }
    void close() override {
        std::lock_guard lk(mu_);
        if (h_) {
            CancelIoEx(h_, nullptr);
            CloseHandle(h_);
        }
        if (ev_) CloseHandle(ev_);
        h_ = ev_ = nullptr;
        pending_ = false;
        buf_.clear();
    }

private:
    std::optional<std::string> pop() {
        auto pos = buf_.find('\n');
        if (pos == std::string::npos) return std::nullopt;
        auto l = buf_.substr(0, pos);
        buf_.erase(0, pos + 1);
        return l;
    }
    HANDLE h_ = nullptr, ev_ = nullptr;
    OVERLAPPED ov_{};
    bool pending_ = false;
    char tmp_[8192];
    std::string buf_;
    std::mutex mu_;
};

}  // namespace

std::unique_ptr<IIpcServer> make_ipc_server(std::string allowed_exe) {
    return std::make_unique<IpcServer>(std::move(allowed_exe));
}
std::unique_ptr<IIpcClient> make_ipc_client() { return std::make_unique<IpcClient>(); }

}  // namespace xg::win
