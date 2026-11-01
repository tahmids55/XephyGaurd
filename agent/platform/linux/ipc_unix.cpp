// Unix-domain socket IPC between the root service and per-user helpers.
// Authentication: SO_PEERCRED gives the peer uid/pid; the peer's /proc/<pid>/exe must be the installed
// helper binary (set XG_IPC_ALLOW_ANY=1 only for development).
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <mutex>
#include <thread>

#include "linux_common.hpp"
#include "xg/ipc_messages.hpp"
#include "xg/log.hpp"

namespace xg::lnx {
namespace {

bool send_all(int fd, const std::string& s) {
    size_t off = 0;
    while (off < s.size()) {
        ssize_t n = ::send(fd, s.data() + off, s.size() - off, MSG_NOSIGNAL);
        if (n <= 0) return false;
        off += size_t(n);
    }
    return true;
}

class IpcServer final : public IIpcServer {
public:
    explicit IpcServer(std::string allowed_exe) : allowed_exe_(std::move(allowed_exe)) {}
    ~IpcServer() override { stop(); }

    bool start(const std::string& endpoint, std::function<void(const Peer&, const std::string&)> on_msg,
               std::function<void(const Peer&, bool)> on_peer) override {
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(endpoint).parent_path(), ec);
        ::chmod(std::filesystem::path(endpoint).parent_path().c_str(), 0755);
        ::unlink(endpoint.c_str());
        lfd_ = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (lfd_ < 0) return false;
        sockaddr_un sa{};
        sa.sun_family = AF_UNIX;
        std::strncpy(sa.sun_path, endpoint.c_str(), sizeof sa.sun_path - 1);
        if (::bind(lfd_, reinterpret_cast<sockaddr*>(&sa), sizeof sa) < 0 || ::listen(lfd_, 16) < 0) {
            ::close(lfd_);
            lfd_ = -1;
            return false;
        }
        ::chmod(endpoint.c_str(), 0666);  // any local user may connect; identity is checked per connection
        running_ = true;
        thread_ = std::thread([this, on_msg, on_peer] { loop(on_msg, on_peer); });
        return true;
    }

    bool send(uint64_t id, const std::string& line) override {
        std::lock_guard lk(mu_);
        auto it = conns_.find(id);
        return it != conns_.end() && send_all(it->second.fd, line);
    }

    void broadcast(const std::string& line) override {
        std::lock_guard lk(mu_);
        for (auto& [id, c] : conns_) send_all(c.fd, line);
    }

    void stop() override {
        running_ = false;
        if (thread_.joinable()) thread_.join();
        std::lock_guard lk(mu_);
        for (auto& [id, c] : conns_) ::close(c.fd);
        conns_.clear();
        if (lfd_ >= 0) ::close(lfd_);
        lfd_ = -1;
    }

private:
    struct Conn { int fd; Peer peer; std::string buf; };

    void loop(const std::function<void(const Peer&, const std::string&)>& on_msg,
              const std::function<void(const Peer&, bool)>& on_peer) {
        while (running_) {
            std::vector<pollfd> pfds{{lfd_, POLLIN, 0}};
            std::vector<uint64_t> ids;
            {
                std::lock_guard lk(mu_);
                for (auto& [id, c] : conns_) { pfds.push_back({c.fd, POLLIN, 0}); ids.push_back(id); }
            }
            if (::poll(pfds.data(), pfds.size(), 500) <= 0) continue;
            if (pfds[0].revents & POLLIN) accept_one(on_peer);
            for (size_t i = 1; i < pfds.size(); ++i) {
                if (!(pfds[i].revents & (POLLIN | POLLHUP | POLLERR))) continue;
                char buf[8192];
                ssize_t n = ::recv(pfds[i].fd, buf, sizeof buf, 0);
                Peer peer;
                std::vector<std::string> lines;
                bool closed = false;
                {
                    std::lock_guard lk(mu_);
                    auto it = conns_.find(ids[i - 1]);
                    if (it == conns_.end()) continue;
                    peer = it->second.peer;
                    if (n <= 0) {
                        ::close(it->second.fd);
                        conns_.erase(it);
                        closed = true;
                    } else {
                        auto& b = it->second.buf;
                        b.append(buf, size_t(n));
                        size_t pos;
                        while ((pos = b.find('\n')) != std::string::npos) {
                            lines.push_back(b.substr(0, pos));
                            b.erase(0, pos + 1);
                        }
                        if (b.size() > ipc::kMaxLine) b.clear();  // oversized garbage
                    }
                }
                if (closed) { on_peer(peer, false); continue; }
                for (const auto& l : lines) on_msg(peer, l);
            }
        }
    }

    void accept_one(const std::function<void(const Peer&, bool)>& on_peer) {
        int fd = ::accept4(lfd_, nullptr, nullptr, SOCK_CLOEXEC);
        if (fd < 0) return;
        ucred cred{};
        socklen_t len = sizeof cred;
        if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cred, &len) < 0) { ::close(fd); return; }
        const std::string exe = read_link("/proc/" + std::to_string(cred.pid) + "/exe");
        const bool dev = std::getenv("XG_IPC_ALLOW_ANY") != nullptr;
        if (!dev && exe != allowed_exe_) {
            log::warn("ipc: rejected connection from pid {} ({}) uid {}", cred.pid, exe, cred.uid);
            ::close(fd);
            return;
        }
        Peer p;
        p.conn_id = ++next_id_;
        p.uid = cred.uid;
        p.pid = cred.pid;
        p.user = user_name(cred.uid);
        {
            std::lock_guard lk(mu_);
            if (conns_.size() >= 64) { ::close(fd); return; }
            conns_[p.conn_id] = Conn{fd, p, {}};
        }
        log::info("ipc: helper connected (user {}, pid {})", p.user, p.pid);
        on_peer(p, true);
    }

    std::string allowed_exe_;
    int lfd_ = -1;
    std::atomic<bool> running_{false};
    std::thread thread_;
    std::mutex mu_;
    std::map<uint64_t, Conn> conns_;
    uint64_t next_id_ = 0;
};

class IpcClient final : public IIpcClient {
public:
    ~IpcClient() override { close(); }
    bool connect(const std::string& endpoint) override {
        close();
        fd_ = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (fd_ < 0) return false;
        sockaddr_un sa{};
        sa.sun_family = AF_UNIX;
        std::strncpy(sa.sun_path, endpoint.c_str(), sizeof sa.sun_path - 1);
        if (::connect(fd_, reinterpret_cast<sockaddr*>(&sa), sizeof sa) < 0) { close(); return false; }
        return true;
    }
    bool send(const std::string& line) override {
        std::lock_guard lk(mu_);
        if (fd_ < 0) return false;
        if (!send_all(fd_, line)) { ::close(fd_); fd_ = -1; return false; }
        return true;
    }
    std::optional<std::string> receive(int timeout_ms) override {
        if (fd_ < 0) return std::nullopt;
        if (auto pos = buf_.find('\n'); pos != std::string::npos) {
            auto l = buf_.substr(0, pos);
            buf_.erase(0, pos + 1);
            return l;
        }
        pollfd p{fd_, POLLIN, 0};
        if (::poll(&p, 1, timeout_ms) <= 0) return std::nullopt;
        char b[8192];
        ssize_t n = ::recv(fd_, b, sizeof b, 0);
        if (n <= 0) { close(); return std::nullopt; }
        buf_.append(b, size_t(n));
        if (auto pos = buf_.find('\n'); pos != std::string::npos) {
            auto l = buf_.substr(0, pos);
            buf_.erase(0, pos + 1);
            return l;
        }
        return std::nullopt;
    }
    bool connected() const override { return fd_ >= 0; }
    void close() override {
        std::lock_guard lk(mu_);
        if (fd_ >= 0) ::close(fd_);
        fd_ = -1;
        buf_.clear();
    }

private:
    int fd_ = -1;
    std::string buf_;
    std::mutex mu_;
};

}  // namespace

std::unique_ptr<IIpcServer> make_ipc_server(std::string allowed_exe) {
    return std::make_unique<IpcServer>(std::move(allowed_exe));
}
std::unique_ptr<IIpcClient> make_ipc_client() { return std::make_unique<IpcClient>(); }

}  // namespace xg::lnx
