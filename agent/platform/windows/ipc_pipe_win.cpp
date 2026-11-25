// Named-pipe IPC (\\.\pipe\XephyGuardHelper). The service verifies each client's image path and
// session; the pipe DACL lets authenticated users read/write data but not create server instances, the first
// instance is created with FILE_FLAG_FIRST_PIPE_INSTANCE, and the helper checks that the server is the agent
// running in session 0 (I-AGENT-5).
#include "win_common.hpp"  // must precede other Windows SDK headers
#include <sddl.h>
#include <wtsapi32.h>

#include <atomic>
#include <cstdlib>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "xg/ipc_framing.hpp"
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

    void set_frame_handler(FrameHandler h) override { on_frame_ = std::move(h); }

    void stop() override {
        running_ = false;
        if (accept_.joinable()) accept_.join();
        std::vector<Reader> readers;
        {
            std::lock_guard lk(mu_);
            for (auto& [id, c] : conns_) CancelIoEx(c->h, nullptr);
            readers.swap(readers_);
        }
        for (auto& r : readers) r.th.join();
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
    struct Reader {
        std::thread th;
        std::shared_ptr<std::atomic<bool>> done;
    };
    static constexpr size_t kMaxConnections = 64;  // same cap as the Linux socket server (I-AGENT-28)

    // Joins reader threads whose connection ended (called from the accept loop).
    void reap_readers() {
        std::vector<Reader> finished;
        {
            std::lock_guard lk(mu_);
            for (auto it = readers_.begin(); it != readers_.end();) {
                if (it->done->load()) {
                    finished.push_back(std::move(*it));
                    it = readers_.erase(it);
                } else {
                    ++it;
                }
            }
        }
        for (auto& r : finished) r.th.join();
    }

    void accept_loop(const std::function<void(const Peer&, const std::string&)>& on_msg,
                     const std::function<void(const Peer&, bool)>& on_peer) {
        PSECURITY_DESCRIPTOR sd = nullptr;
        // SYSTEM, Administrators: full. Authenticated Users: FILE_GENERIC_READ | FILE_WRITE_DATA (0x0012008B), so
        // they can talk as clients but lack FILE_CREATE_PIPE_INSTANCE and cannot add a squatting server instance.
        ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;0x0012008b;;;AU)",
                                                             SDDL_REVISION_1, &sd, nullptr);
        SECURITY_ATTRIBUTES sa{sizeof sa, sd, FALSE};
        while (running_) {
            reap_readers();
            // With no open connection our previous instances are gone and so is the pipe name: the next instance must
            // create it (FILE_FLAG_FIRST_PIPE_INSTANCE). If a squatter already holds the name, refuse and retry.
            bool first;
            {
                std::lock_guard lk(mu_);
                first = conns_.empty();
            }
            const DWORD open_mode = PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | (first ? FILE_FLAG_FIRST_PIPE_INSTANCE : 0);
            HANDLE h = CreateNamedPipeW(name_.c_str(), open_mode,
                                        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                                        PIPE_UNLIMITED_INSTANCES, 64 * 1024, 64 * 1024, 0, &sa);
            if (h == INVALID_HANDLE_VALUE) {
                if (first) log::error("ipc: cannot create {} as first instance (error {}); is another process holding it?",
                                      to_utf8(name_), GetLastError());
                Sleep(1000);
                continue;
            }
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
            {
                std::lock_guard lk(mu_);
                if (conns_.size() >= kMaxConnections) {
                    log::warn("ipc: {} helper connections open, rejecting pid {}", conns_.size(), pid);
                    DisconnectNamedPipe(h);
                    CloseHandle(h);
                    continue;
                }
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
                auto done = std::make_shared<std::atomic<bool>>(false);
                readers_.push_back(Reader{std::thread([this, c, on_msg, on_peer, done] {
                                              read_loop(c, on_msg, on_peer);
                                              done->store(true);
                                          }),
                                          done});
            }
            on_peer(c->peer, true);
        }
        if (sd) LocalFree(sd);
    }

    void read_loop(std::shared_ptr<Conn> c, const std::function<void(const Peer&, const std::string&)>& on_msg,
                   const std::function<void(const Peer&, bool)>& on_peer) {
        ipc::StreamReader reader;
        std::vector<ipc::Record> records;
        std::vector<char> tmp(64 * 1024);
        OVERLAPPED ov{};
        ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        while (running_) {
            ResetEvent(ov.hEvent);
            DWORD n = 0;
            BOOL ok = ReadFile(c->h, tmp.data(), DWORD(tmp.size()), nullptr, &ov);
            if (!ok && GetLastError() != ERROR_IO_PENDING) break;
            while (running_ && WaitForSingleObject(ov.hEvent, 500) == WAIT_TIMEOUT) {}
            if (!running_ || !GetOverlappedResult(c->h, &ov, &n, FALSE) || n == 0) break;
            records.clear();
            const bool valid = reader.feed(tmp.data(), n, records);
            for (auto& r : records) {
                if (r.kind == ipc::Record::Kind::line) on_msg(c->peer, r.data);
                else if (on_frame_) on_frame_(c->peer, std::move(r.data));
            }
            if (!valid) {
                log::warn("ipc: protocol error from helper pid {}, closing", c->peer.pid);
                break;
            }
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
    FrameHandler on_frame_;
    std::wstring name_;
    std::atomic<bool> running_{false};
    std::thread accept_;
    std::mutex mu_;
    std::map<uint64_t, std::shared_ptr<Conn>> conns_;
    std::vector<Reader> readers_;
    std::atomic<uint64_t> next_id_{0};
};

// The real server is the agent service: session 0 (user processes cannot run there) and xephyguard-agent.exe.
// A standard user cannot read a SYSTEM token, so the session id is the check that matters.
bool server_is_agent(HANDLE h) {
    if (std::getenv("XG_IPC_ALLOW_ANY")) return true;  // development only, same switch as the server side
    ULONG session = ULONG(-1), pid = 0;
    if (!GetNamedPipeServerSessionId(h, &session) || session != 0) {
        log::warn("ipc: pipe server is not in session 0 (session {}); refusing it", session);
        return false;
    }
    if (!GetNamedPipeServerProcessId(h, &pid)) return false;
    const std::string exe = to_lower(process_image(pid));
    if (!exe.empty() && !exe.ends_with("\\xephyguard-agent.exe")) {
        log::warn("ipc: pipe server is {} (pid {}), not the agent; refusing it", exe, pid);
        return false;
    }
    return true;
}

class IpcClient final : public IIpcClient {
public:
    ~IpcClient() override { close(); }
    bool connect(const std::string& endpoint) override {
        close();
        // SECURITY_IDENTIFICATION: a squatting server may not impersonate the student.
        h_ = CreateFileW(to_wide(endpoint).c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                         FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
        if (h_ == INVALID_HANDLE_VALUE) { h_ = nullptr; return false; }
        if (!server_is_agent(h_)) {
            CloseHandle(h_);
            h_ = nullptr;
            return false;
        }
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
