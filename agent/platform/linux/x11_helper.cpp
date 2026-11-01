// Helper-side Linux adapters: active window + clipboard (X11), transparency indicator, notifications.
// On Wayland the compositor hides other clients' windows and clipboard; these adapters then report
// available() == false and the agent tells the CCC what it cannot see.
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>

#include "linux_common.hpp"
#include "xg/log.hpp"
#include "xg/paste.hpp"
#include "xg/util.hpp"

#ifdef XG_HAVE_X11
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xfixes.h>
#endif

namespace xg::lnx {

namespace {

bool x11_session() {
    const char* d = std::getenv("DISPLAY");
    const char* t = std::getenv("XDG_SESSION_TYPE");
    if (!d || !*d) return false;
    return !(t && std::string(t) == "wayland");  // XWayland exists but cannot see native Wayland clients
}

void spawn_notify(const std::string& title, const std::string& body, bool urgent) {
    pid_t pid = fork();
    if (pid == 0) {
        execlp("notify-send", "notify-send", "-a", "XephyGuard", "-u", urgent ? "critical" : "normal", title.c_str(),
               body.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }
    while (waitpid(-1, nullptr, WNOHANG) > 0) {}
}

#ifdef XG_HAVE_X11
std::string window_app(Display* d, Window w, Atom pid_atom) {
    Atom type;
    int fmt;
    unsigned long n, after;
    unsigned char* data = nullptr;
    std::string app;
    if (XGetWindowProperty(d, w, pid_atom, 0, 1, False, XA_CARDINAL, &type, &fmt, &n, &after, &data) == Success && data &&
        n == 1) {
        const long pid = *reinterpret_cast<long*>(data);
        app = path_basename(read_link("/proc/" + std::to_string(pid) + "/exe"));
        if (app.empty()) app = trim(read_small("/proc/" + std::to_string(pid) + "/comm"));
    }
    if (data) XFree(data);
    if (app.empty()) {
        XClassHint ch{};
        if (XGetClassHint(d, w, &ch)) {
            if (ch.res_class) app = to_lower(ch.res_class);
            if (ch.res_name) XFree(ch.res_name);
            if (ch.res_class) XFree(ch.res_class);
        }
    }
    return app;
}

std::string window_title(Display* d, Window w, Atom name_atom, Atom utf8) {
    Atom type;
    int fmt;
    unsigned long n, after;
    unsigned char* data = nullptr;
    std::string t;
    if (XGetWindowProperty(d, w, name_atom, 0, 1024, False, utf8, &type, &fmt, &n, &after, &data) == Success && data)
        t.assign(reinterpret_cast<char*>(data), n);
    if (data) XFree(data);
    return t;
}

Window active_window(Display* d, Atom active_atom) {
    Atom type;
    int fmt;
    unsigned long n, after;
    unsigned char* data = nullptr;
    Window w = 0;
    if (XGetWindowProperty(d, DefaultRootWindow(d), active_atom, 0, 1, False, XA_WINDOW, &type, &fmt, &n, &after,
                           &data) == Success && data && n == 1)
        w = *reinterpret_cast<Window*>(data);
    if (data) XFree(data);
    return w;
}

/// Reads the CLIPBOARD selection into a transient buffer, reduces it to numbers and wipes it.
bool measure_clipboard(Display* d, Window requestor, PasteInfo& out, int timeout_ms) {
    const Atom clip = XInternAtom(d, "CLIPBOARD", False);
    const Atom utf8 = XInternAtom(d, "UTF8_STRING", False);
    const Atom prop = XInternAtom(d, "XG_SELECTION", False);
    const Atom incr = XInternAtom(d, "INCR", False);
    if (XGetSelectionOwner(d, clip) == None) return false;
    XConvertSelection(d, clip, utf8, prop, requestor, CurrentTime);
    XFlush(d);
    const int64_t deadline = steady_ms() + timeout_ms;
    while (steady_ms() < deadline) {
        XEvent ev;
        if (XCheckTypedWindowEvent(d, requestor, SelectionNotify, &ev)) {
            if (ev.xselection.property == None) return false;
            Atom type;
            int fmt;
            unsigned long n, after;
            unsigned char* data = nullptr;
            if (XGetWindowProperty(d, requestor, prop, 0, 4 * 1024 * 1024 / 4, True, AnyPropertyType, &type, &fmt, &n,
                                   &after, &data) != Success)
                return false;
            if (type == incr) {
                // Large transfer: we only learn a lower-bound size; do not fetch the content.
                if (data && n >= 1) out.chars = uint64_t(*reinterpret_cast<long*>(data));
                out.lines = 0;
                out.looks_like_code = false;
            } else if (data) {
                std::string_view text(reinterpret_cast<char*>(data), n);
                PasteInfo p = analyze_clipboard_text(text);
                out.chars = p.chars;
                out.lines = p.lines;
                out.code_score = p.code_score;
                out.looks_like_code = p.looks_like_code;
                std::memset(data, 0, n);  // discard content
            }
            if (data) XFree(data);
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

class X11WindowMonitor final : public IWindowMonitor {
public:
    ~X11WindowMonitor() override { stop(); }
    bool available() const override { return x11_session(); }
    bool start(std::function<void(const WindowInfo&)> cb) override {
        if (!available()) return false;
        running_ = true;
        thread_ = std::thread([this, cb] {
            Display* d = XOpenDisplay(nullptr);
            if (!d) return;
            const Atom active = XInternAtom(d, "_NET_ACTIVE_WINDOW", False);
            const Atom pid = XInternAtom(d, "_NET_WM_PID", False);
            const Atom name = XInternAtom(d, "_NET_WM_NAME", False);
            const Atom utf8 = XInternAtom(d, "UTF8_STRING", False);
            XSelectInput(d, DefaultRootWindow(d), PropertyChangeMask);
            std::string last_app, last_title;
            Window last_w = 0;
            auto report = [&] {
                Window w = active_window(d, active);
                if (!w) return;
                if (w != last_w) {
                    if (last_w) XSelectInput(d, last_w, NoEventMask);
                    XSelectInput(d, w, PropertyChangeMask);  // title changes of the focused window
                    last_w = w;
                }
                WindowInfo wi{window_app(d, w, pid), window_title(d, w, name, utf8), 0};
                if (wi.app != last_app || wi.title != last_title) {
                    last_app = wi.app;
                    last_title = wi.title;
                    cb(wi);
                }
            };
            report();
            while (running_) {
                pollfd p{ConnectionNumber(d), POLLIN, 0};
                ::poll(&p, 1, 500);
                bool changed = false;
                while (XPending(d)) {
                    XEvent ev;
                    XNextEvent(d, &ev);
                    if (ev.type == PropertyNotify && (ev.xproperty.atom == active || ev.xproperty.atom == name))
                        changed = true;
                }
                if (changed) report();
            }
            XCloseDisplay(d);
        });
        return true;
    }
    void stop() override {
        running_ = false;
        if (thread_.joinable()) thread_.join();
    }

private:
    std::atomic<bool> running_{false};
    std::thread thread_;
};

class X11ClipboardMonitor final : public IClipboardMonitor {
public:
    ~X11ClipboardMonitor() override { stop(); }
    bool available() const override { return x11_session(); }
    bool start(std::function<void(const PasteInfo&)> cb) override {
        if (!available()) return false;
        running_ = true;
        thread_ = std::thread([this, cb] {
            Display* d = XOpenDisplay(nullptr);
            if (!d) return;
            int ev_base = 0, err_base = 0;
            if (!XFixesQueryExtension(d, &ev_base, &err_base)) { XCloseDisplay(d); return; }
            const Atom clip = XInternAtom(d, "CLIPBOARD", False);
            const Atom pid = XInternAtom(d, "_NET_WM_PID", False);
            Window win = XCreateSimpleWindow(d, DefaultRootWindow(d), 0, 0, 1, 1, 0, 0, 0);
            XFixesSelectSelectionInput(d, DefaultRootWindow(d), clip, XFixesSetSelectionOwnerNotifyMask);
            while (running_) {
                pollfd p{ConnectionNumber(d), POLLIN, 0};
                ::poll(&p, 1, 500);
                while (XPending(d)) {
                    XEvent ev;
                    XNextEvent(d, &ev);
                    if (ev.type != ev_base + XFixesSelectionNotify) continue;
                    auto* se = reinterpret_cast<XFixesSelectionNotifyEvent*>(&ev);
                    if (se->owner == None) continue;
                    PasteInfo pi;
                    pi.is_copy = true;
                    pi.source_app = window_app(d, se->owner, pid);
                    if (measure_clipboard(d, win, pi, 500)) cb(pi);
                }
            }
            XDestroyWindow(d, win);
            XCloseDisplay(d);
        });
        return true;
    }
    void stop() override {
        running_ = false;
        if (thread_.joinable()) thread_.join();
    }

private:
    std::atomic<bool> running_{false};
    std::thread thread_;
};

/// Small always-on-top badge ("Monitored by XephyGuard · mode: Test") plus desktop notifications.
class X11StudentUi final : public IStudentUi {
public:
    X11StudentUi() {
        if (!x11_session()) return;
        d_ = XOpenDisplay(nullptr);
        if (!d_) return;
        const int scr = DefaultScreen(d_);
        XSetWindowAttributes a{};
        a.override_redirect = True;
        a.background_pixel = 0x303030;
        w_ = XCreateWindow(d_, RootWindow(d_, scr), DisplayWidth(d_, scr) - 330, 0, 330, 20, 0, CopyFromParent,
                           InputOutput, CopyFromParent, CWOverrideRedirect | CWBackPixel, &a);
        XSelectInput(d_, w_, ExposureMask);
        gc_ = XCreateGC(d_, w_, 0, nullptr);
        XStoreName(d_, w_, "XephyGuard");
        XMapRaised(d_, w_);
        XFlush(d_);
    }
    ~X11StudentUi() override {
        if (d_) {
            XFreeGC(d_, gc_);
            XDestroyWindow(d_, w_);
            XCloseDisplay(d_);
        }
    }
    void set_indicator(const std::string& text, const std::string& mode) override {
        if (text == text_ && mode == mode_) return;
        const bool first = text_.empty();
        text_ = text;
        mode_ = mode;
        if (!d_) {
            if (first || mode != "always_on") spawn_notify("XephyGuard", text, false);
            return;
        }
        unsigned long bg = mode == "test" ? 0xB71C1C : mode == "practice" ? 0x1565C0 : 0x37474F;
        XSetWindowBackground(d_, w_, bg);
        XClearWindow(d_, w_);
        draw();
    }
    void notify(const std::string& title, const std::string& body, bool urgent) override {
        spawn_notify(title, body, urgent);
    }
    void pump(int ms) override {
        if (!d_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(ms));
            return;
        }
        pollfd p{ConnectionNumber(d_), POLLIN, 0};
        ::poll(&p, 1, ms);
        while (XPending(d_)) {
            XEvent ev;
            XNextEvent(d_, &ev);
            if (ev.type == Expose) draw();
        }
        if (++ticks_ % 20 == 0) {  // keep the badge on top of fullscreen windows
            XRaiseWindow(d_, w_);
            XFlush(d_);
        }
    }

private:
    void draw() {
        if (!d_) return;
        XSetForeground(d_, gc_, 0xFFFFFF);
        XDrawString(d_, w_, gc_, 8, 14, text_.c_str(), int(text_.size()));
        XFlush(d_);
    }
    Display* d_ = nullptr;
    Window w_ = 0;
    GC gc_{};
    std::string text_, mode_;
    uint64_t ticks_ = 0;
};
#endif  // XG_HAVE_X11

class NullWindowMonitor final : public IWindowMonitor {
public:
    bool available() const override { return false; }
    bool start(std::function<void(const WindowInfo&)>) override { return false; }
    void stop() override {}
};
class NullClipboardMonitor final : public IClipboardMonitor {
public:
    bool available() const override { return false; }
    bool start(std::function<void(const PasteInfo&)>) override { return false; }
    void stop() override {}
};
class NotifyOnlyUi final : public IStudentUi {
public:
    void set_indicator(const std::string& text, const std::string& mode) override {
        if (text == text_) return;
        text_ = text;
        if (mode != "always_on") spawn_notify("XephyGuard", text, false);
    }
    void notify(const std::string& t, const std::string& b, bool u) override { spawn_notify(t, b, u); }
    void pump(int ms) override { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

private:
    std::string text_;
};

}  // namespace

std::unique_ptr<IWindowMonitor> make_window_monitor() {
#ifdef XG_HAVE_X11
    if (x11_session()) return std::make_unique<X11WindowMonitor>();
#endif
    return std::make_unique<NullWindowMonitor>();
}

std::unique_ptr<IClipboardMonitor> make_clipboard_monitor() {
#ifdef XG_HAVE_X11
    if (x11_session()) return std::make_unique<X11ClipboardMonitor>();
#endif
    return std::make_unique<NullClipboardMonitor>();
}

std::unique_ptr<IStudentUi> make_student_ui() {
#ifdef XG_HAVE_X11
    if (x11_session()) return std::make_unique<X11StudentUi>();
#endif
    return std::make_unique<NotifyOnlyUi>();
}

bool probe_clipboard(PasteInfo& out) {
#ifdef XG_HAVE_X11
    if (!x11_session()) return false;
    static std::mutex mu;
    std::lock_guard lk(mu);
    Display* d = XOpenDisplay(nullptr);
    if (!d) return false;
    Window win = XCreateSimpleWindow(d, DefaultRootWindow(d), 0, 0, 1, 1, 0, 0, 0);
    const bool ok = measure_clipboard(d, win, out, 500);
    XDestroyWindow(d, win);
    XCloseDisplay(d);
    return ok;
#else
    (void)out;
    return false;
#endif
}

}  // namespace xg::lnx
