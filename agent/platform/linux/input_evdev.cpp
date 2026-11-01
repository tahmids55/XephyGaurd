// Key-timing monitor reading evdev keyboards as root. Works on X11, Wayland and consoles.
// PRIVACY: only press timestamps, a "was backspace" bit and a "was a paste chord" bit leave this
// file. The key code is inspected in-function and discarded immediately; nothing is buffered.
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <atomic>
#include <thread>
#include <vector>

#include "linux_common.hpp"
#include "xg/util.hpp"

namespace xg::lnx {
namespace {

#define XG_BITS_PER_LONG (sizeof(long) * 8)
#define XG_NBITS(x) ((((x)-1) / XG_BITS_PER_LONG) + 1)
#define XG_TEST_BIT(bit, array) ((array[(bit) / XG_BITS_PER_LONG] >> ((bit) % XG_BITS_PER_LONG)) & 1)

bool is_keyboard(int fd) {
    unsigned long evbits[XG_NBITS(EV_MAX)] = {0};
    unsigned long keybits[XG_NBITS(KEY_MAX)] = {0};
    if (ioctl(fd, EVIOCGBIT(0, sizeof evbits), evbits) < 0) return false;
    if (!XG_TEST_BIT(EV_KEY, evbits)) return false;
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof keybits), keybits) < 0) return false;
    return XG_TEST_BIT(KEY_A, keybits) && XG_TEST_BIT(KEY_Z, keybits) && XG_TEST_BIT(KEY_SPACE, keybits);
}

class EvdevInput final : public IInputTimingMonitor {
public:
    ~EvdevInput() override { stop(); }
    bool available() const override { return ::access("/dev/input", R_OK) == 0; }

    bool start(std::function<void(int64_t, bool, bool)> on_key) override {
        if (running_) return true;
        running_ = true;
        thread_ = std::thread([this, on_key] { loop(on_key); });
        return true;
    }

    void stop() override {
        running_ = false;
        if (thread_.joinable()) thread_.join();
    }

private:
    void loop(const std::function<void(int64_t, bool, bool)>& on_key) {
        std::vector<int> fds;
        int64_t next_rescan = 0;
        bool ctrl = false, shift = false;
        while (running_) {
            if (steady_ms() >= next_rescan) {
                for (int fd : fds) ::close(fd);
                fds.clear();
                for (const auto& n : list_dir("/dev/input")) {
                    if (!n.starts_with("event")) continue;
                    int fd = ::open(("/dev/input/" + n).c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
                    if (fd < 0) continue;
                    if (is_keyboard(fd)) fds.push_back(fd);
                    else ::close(fd);
                }
                next_rescan = steady_ms() + 30000;  // pick up hot-plugged keyboards
            }
            if (fds.empty()) { std::this_thread::sleep_for(std::chrono::seconds(1)); continue; }
            std::vector<pollfd> p;
            for (int fd : fds) p.push_back({fd, POLLIN, 0});
            if (::poll(p.data(), p.size(), 500) <= 0) continue;
            for (auto& pf : p) {
                if (!(pf.revents & POLLIN)) continue;
                input_event ev[64];
                ssize_t n = ::read(pf.fd, ev, sizeof ev);
                if (n <= 0) { next_rescan = 0; continue; }
                for (size_t i = 0; i < size_t(n) / sizeof(input_event); ++i) {
                    if (ev[i].type != EV_KEY) continue;
                    const unsigned code = ev[i].code;
                    const int value = ev[i].value;  // 1 press, 0 release, 2 autorepeat
                    if (code == KEY_LEFTCTRL || code == KEY_RIGHTCTRL) { ctrl = value != 0; continue; }
                    if (code == KEY_LEFTSHIFT || code == KEY_RIGHTSHIFT) { shift = value != 0; continue; }
                    if (value != 1) continue;
                    if (code >= BTN_MISC) continue;  // mouse buttons etc.
                    const bool paste = (ctrl && code == KEY_V) || (shift && code == KEY_INSERT);
                    const int64_t t = int64_t(ev[i].input_event_sec) * 1000 + int64_t(ev[i].input_event_usec) / 1000;
                    on_key(t > 0 ? t : now_ms(), code == KEY_BACKSPACE, paste);
                }
            }
        }
        for (int fd : fds) ::close(fd);
    }

    std::atomic<bool> running_{false};
    std::thread thread_;
};

}  // namespace

std::unique_ptr<IInputTimingMonitor> make_evdev_input() { return std::make_unique<EvdevInput>(); }

}  // namespace xg::lnx
