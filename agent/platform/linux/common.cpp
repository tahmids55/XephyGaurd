#include <pthread.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <cerrno>
#include <dirent.h>
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <fstream>
#include <sstream>

#include "linux_common.hpp"

namespace xg::lnx {

int run_program(const std::vector<std::string>& argv, const std::string& stdin_text) {
    if (argv.empty() || argv[0].empty() || argv[0][0] != '/') return -1;
    std::vector<char*> args;
    for (const auto& a : argv) args.push_back(const_cast<char*>(a.c_str()));
    args.push_back(nullptr);
    int in[2];
    if (::pipe2(in, O_CLOEXEC) != 0) return -1;
    const pid_t pid = ::fork();
    if (pid < 0) {
        ::close(in[0]);
        ::close(in[1]);
        return -1;
    }
    if (pid == 0) {
        ::dup2(in[0], 0);
        if (int devnull = ::open("/dev/null", O_WRONLY); devnull >= 0) {
            ::dup2(devnull, 1);
            ::dup2(devnull, 2);
        }
        ::execv(args[0], args.data());
        _exit(127);
    }
    ::close(in[0]);
    // A child that exits early closes the pipe: MSG-less write returns EPIPE (SIGPIPE is ignored by the
    // service; block it here too so a CLI caller is not killed).
    sigset_t pipe_set, old_set;
    sigemptyset(&pipe_set);
    sigaddset(&pipe_set, SIGPIPE);
    ::pthread_sigmask(SIG_BLOCK, &pipe_set, &old_set);
    for (size_t off = 0; off < stdin_text.size();) {
        const ssize_t n = ::write(in[1], stdin_text.data() + off, stdin_text.size() - off);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        off += size_t(n);
    }
    ::close(in[1]);
    // Drop a SIGPIPE raised by the write above before restoring the mask.
    timespec zero{0, 0};
    while (::sigtimedwait(&pipe_set, nullptr, &zero) == SIGPIPE) {}
    ::pthread_sigmask(SIG_SETMASK, &old_set, nullptr);
    int status = 0;
    pid_t r;
    do r = ::waitpid(pid, &status, 0);
    while (r < 0 && errno == EINTR);
    if (r != pid) return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

void ChildReaper::add(pid_t pid) {
    if (pid <= 0) return;
    std::lock_guard lk(mu_);
    pids_.push_back(pid);
}

size_t ChildReaper::reap() {
    std::lock_guard lk(mu_);
    std::erase_if(pids_, [](pid_t p) {
        const pid_t r = ::waitpid(p, nullptr, WNOHANG);
        return r == p || (r < 0 && errno == ECHILD);
    });
    return pids_.size();
}


std::string read_small(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::string read_link(const std::string& path) {
    char buf[4096];
    ssize_t n = ::readlink(path.c_str(), buf, sizeof buf - 1);
    if (n <= 0) return {};
    return std::string(buf, size_t(n));
}

std::vector<std::string> list_dir(const std::string& path) {
    std::vector<std::string> out;
    DIR* d = ::opendir(path.c_str());
    if (!d) return out;
    while (dirent* e = ::readdir(d)) {
        if (e->d_name[0] == '.' && (e->d_name[1] == 0 || (e->d_name[1] == '.' && e->d_name[2] == 0))) continue;
        out.emplace_back(e->d_name);
    }
    ::closedir(d);
    return out;
}

std::string user_name(uint32_t uid) {
    passwd pw{}, *res = nullptr;
    char buf[4096];
    if (getpwuid_r(uid, &pw, buf, sizeof buf, &res) == 0 && res) return pw.pw_name;
    return std::to_string(uid);
}

std::string user_home(const std::string& user) {
    passwd pw{}, *res = nullptr;
    char buf[4096];
    if (getpwnam_r(user.c_str(), &pw, buf, sizeof buf, &res) == 0 && res) return pw.pw_dir;
    return {};
}

uint32_t file_uid(const std::string& path) {
    struct stat st{};
    return ::stat(path.c_str(), &st) == 0 ? st.st_uid : 0;
}

std::string sanitize_indicator_label(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size();) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (i + 1 < text.size() && c == 0xC2 && static_cast<unsigned char>(text[i + 1]) == 0xB7) {
            out.push_back('-');
            i += 2;
        } else if (i + 2 < text.size() && c == 0xE2 && static_cast<unsigned char>(text[i + 1]) == 0x80 &&
                   (static_cast<unsigned char>(text[i + 2]) == 0x93 ||
                    static_cast<unsigned char>(text[i + 2]) == 0x94 ||
                    static_cast<unsigned char>(text[i + 2]) == 0xA2)) {
            out.push_back('-');
            i += 3;
        } else if (c >= 0x20 && c <= 0x7E) {
            out.push_back(static_cast<char>(c));
            ++i;
        } else if ((c & 0x80) != 0) {
            // Skip non-ASCII UTF-8 bytes so core X11 fonts never render mojibake.
            ++i;
            while (i < text.size() && (static_cast<unsigned char>(text[i]) & 0xC0) == 0x80) ++i;
        } else {
            ++i;
        }
    }
    return out;
}

IndicatorRect compute_indicator_rect(const IndicatorWorkArea& work_area, int text_width_px, int margin_px) {
    constexpr int kPadLeft = 22;  // 10px margin + 6px status dot + 6px gap
    constexpr int kPadRight = 12;
    constexpr int kMinW = 140;
    constexpr int kWinH = 24;

    const int margin = std::max(0, margin_px);
    const int wa_w = std::max(kMinW, work_area.width);
    const int wa_h = std::max(kWinH, work_area.height);
    const int max_w = std::max(kMinW, wa_w - margin * 2);
    const int win_w = std::clamp(kPadLeft + std::max(0, text_width_px) + kPadRight, kMinW, max_w);

    IndicatorRect r;
    r.width = win_w;
    r.height = kWinH;
    r.x = work_area.x + std::max(0, wa_w - win_w - margin);
    r.y = work_area.y + std::max(0, wa_h - kWinH - margin);
    return r;
}

}  // namespace xg::lnx
