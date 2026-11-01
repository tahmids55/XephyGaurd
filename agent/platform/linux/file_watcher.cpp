// inotify-based work-folder watcher. Reports save/create/delete events with size deltas; never content.
#include <poll.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <filesystem>
#include <map>
#include <thread>

#include "linux_common.hpp"
#include "xg/util.hpp"

namespace xg::lnx {
namespace {
namespace fs = std::filesystem;

class FileWatcher final : public IFileWatcher {
public:
    ~FileWatcher() override { stop(); }

    bool watch(const std::vector<std::string>& dirs, std::function<void(const FileActivity&)> cb) override {
        stop();
        fd_ = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
        if (fd_ < 0) return false;
        const uint32_t mask = IN_CLOSE_WRITE | IN_CREATE | IN_DELETE | IN_MOVED_TO | IN_MOVED_FROM;
        for (const auto& root : dirs) {
            std::error_code ec;
            if (!fs::is_directory(root, ec)) continue;
            add(root, root, mask);
            for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec);
                 it != fs::recursive_directory_iterator() && wd_.size() < 512; it.increment(ec)) {
                if (ec) break;
                const auto name = it->path().filename().string();
                if (it->is_directory(ec)) {
                    if (it.depth() >= 4 || name.starts_with(".") || name == "node_modules" || name == "__pycache__" ||
                        name == "build" || name == "target") {
                        it.disable_recursion_pending();
                        continue;
                    }
                    add(it->path().string(), root, mask);
                } else if (it->is_regular_file(ec)) {
                    sizes_[it->path().string()] = int64_t(it->file_size(ec));
                }
            }
        }
        if (wd_.empty()) { ::close(fd_); fd_ = -1; return false; }
        running_ = true;
        thread_ = std::thread([this, cb] { loop(cb); });
        return true;
    }

    void stop() override {
        running_ = false;
        if (thread_.joinable()) thread_.join();
        if (fd_ >= 0) ::close(fd_);
        fd_ = -1;
        wd_.clear();
        sizes_.clear();
    }

private:
    void add(const std::string& dir, const std::string& root, uint32_t mask) {
        int wd = inotify_add_watch(fd_, dir.c_str(), mask);
        if (wd >= 0) wd_[wd] = {dir, root};
    }

    void loop(const std::function<void(const FileActivity&)>& cb) {
        alignas(inotify_event) char buf[16384];
        while (running_) {
            pollfd pfd{fd_, POLLIN, 0};
            if (::poll(&pfd, 1, 500) <= 0) continue;
            ssize_t n = ::read(fd_, buf, sizeof buf);
            for (ssize_t i = 0; i < n;) {
                auto* e = reinterpret_cast<inotify_event*>(buf + i);
                i += ssize_t(sizeof(inotify_event) + e->len);
                auto it = wd_.find(e->wd);
                if (it == wd_.end() || e->len == 0) continue;
                const std::string name = e->name;
                if (name.starts_with(".") || name.ends_with("~") || name.ends_with(".swp")) continue;
                const std::string path = it->second.first + "/" + name;
                FileActivity a;
                a.dir = it->second.second;
                auto dot = name.rfind('.');
                a.file_ext = dot == std::string::npos ? "" : to_lower(name.substr(dot + 1, 8));
                struct stat st{};
                const bool exists = ::stat(path.c_str(), &st) == 0;
                if (exists && S_ISDIR(st.st_mode)) continue;
                const int64_t old = sizes_.contains(path) ? sizes_[path] : 0;
                if (e->mask & (IN_DELETE | IN_MOVED_FROM)) {
                    a.action = "deleted";
                    a.size_delta = -old;
                    sizes_.erase(path);
                } else {
                    a.action = (e->mask & IN_CREATE) ? "created" : "modified";
                    const int64_t now = exists ? int64_t(st.st_size) : 0;
                    a.size_delta = now - old;
                    sizes_[path] = now;
                    if ((e->mask & IN_CREATE) && now == 0) continue;  // wait for the close_write
                }
                int64_t total = 0;
                for (const auto& [p, s] : sizes_) if (p.rfind(a.dir, 0) == 0) total += s;
                a.total_bytes = uint64_t(total);
                cb(a);
            }
        }
    }

    int fd_ = -1;
    std::map<int, std::pair<std::string, std::string>> wd_;   // wd -> (dir, root)
    std::map<std::string, int64_t> sizes_;
    std::atomic<bool> running_{false};
    std::thread thread_;
};

}  // namespace

std::unique_ptr<IFileWatcher> make_file_watcher() { return std::make_unique<FileWatcher>(); }

}  // namespace xg::lnx
