// Work-folder activity via ReadDirectoryChangesW (overlapped, one thread per root). No content is read.
#include <atomic>
#include <filesystem>
#include <map>
#include <thread>

#include "win_common.hpp"
#include "xg/util.hpp"

namespace xg::win {
namespace {
namespace fs = std::filesystem;

class FileWatcher final : public IFileWatcher {
public:
    ~FileWatcher() override { stop(); }

    bool watch(const std::vector<std::string>& dirs, std::function<void(const FileActivity&)> cb) override {
        stop();
        running_ = true;
        for (const auto& d : dirs) {
            std::error_code ec;
            if (!fs::is_directory(d, ec)) continue;
            threads_.emplace_back([this, d, cb] { loop(d, cb); });
        }
        return !threads_.empty();
    }

    void stop() override {
        running_ = false;
        for (auto& t : threads_) t.join();
        threads_.clear();
    }

private:
    void loop(const std::string& root, const std::function<void(const FileActivity&)>& cb) {
        HANDLE dir = CreateFileW(to_wide(root).c_str(), FILE_LIST_DIRECTORY,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                 FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
        if (dir == INVALID_HANDLE_VALUE) return;
        std::map<std::string, int64_t> sizes;
        std::error_code ec;
        for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec);
             it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (ec) break;
            if (it->is_regular_file(ec)) sizes[it->path().string()] = int64_t(it->file_size(ec));
        }
        alignas(DWORD) BYTE buf[32 * 1024];
        OVERLAPPED ov{};
        ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        while (running_) {
            ResetEvent(ov.hEvent);
            if (!ReadDirectoryChangesW(dir, buf, sizeof buf, TRUE,
                                       FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE,
                                       nullptr, &ov, nullptr))
                break;
            while (running_ && WaitForSingleObject(ov.hEvent, 500) == WAIT_TIMEOUT) {}
            if (!running_) { CancelIoEx(dir, &ov); break; }
            DWORD n = 0;
            if (!GetOverlappedResult(dir, &ov, &n, FALSE) || n == 0) continue;
            for (auto* fni = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(buf);;) {
                const std::string rel = to_utf8(std::wstring(fni->FileName, fni->FileNameLength / sizeof(WCHAR)));
                const std::string path = root + "\\" + rel;
                const auto name = path_basename(rel);
                if (!name.starts_with(".") && !name.ends_with("~") && !name.ends_with(".tmp")) {
                    FileActivity a;
                    a.dir = root;
                    auto dot = name.rfind('.');
                    a.file_ext = dot == std::string::npos ? "" : to_lower(name.substr(dot + 1, 8));
                    const int64_t old = sizes.contains(path) ? sizes[path] : 0;
                    std::error_code e2;
                    const bool exists = fs::is_regular_file(path, e2);
                    if (fni->Action == FILE_ACTION_REMOVED || fni->Action == FILE_ACTION_RENAMED_OLD_NAME) {
                        a.action = "deleted";
                        a.size_delta = -old;
                        sizes.erase(path);
                    } else if (exists) {
                        a.action = fni->Action == FILE_ACTION_ADDED ? "created" : "modified";
                        const int64_t now = int64_t(fs::file_size(path, e2));
                        a.size_delta = now - old;
                        sizes[path] = now;
                    }
                    if (!a.action.empty()) {
                        int64_t total = 0;
                        for (const auto& [p, s] : sizes) total += s;
                        a.total_bytes = uint64_t(total);
                        cb(a);
                    }
                }
                if (!fni->NextEntryOffset) break;
                fni = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(reinterpret_cast<BYTE*>(fni) + fni->NextEntryOffset);
            }
        }
        CloseHandle(ov.hEvent);
        CloseHandle(dir);
    }

    std::atomic<bool> running_{false};
    std::vector<std::thread> threads_;
};

}  // namespace

std::unique_ptr<IFileWatcher> make_file_watcher() { return std::make_unique<FileWatcher>(); }

}  // namespace xg::win
