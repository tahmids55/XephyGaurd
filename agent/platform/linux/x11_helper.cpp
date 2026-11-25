// Helper-side Linux adapters: active window + clipboard (X11), transparency indicator, notifications.
// On Wayland the compositor hides other clients' windows and clipboard; these adapters then report
// available() == false and the agent tells the CCC what it cannot see.
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
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
#include <X11/extensions/shape.h>
#endif

namespace xg::lnx {

namespace {

bool x11_session() {
    const char* d = std::getenv("DISPLAY");
    const char* t = std::getenv("XDG_SESSION_TYPE");
    if (!d || !*d) return false;
    return !(t && std::string(t) == "wayland");  // XWayland exists but cannot see native Wayland clients
}

ChildReaper g_notify_children;

void spawn_notify(const std::string& title, const std::string& body, bool urgent) {
    pid_t pid = fork();
    if (pid == 0) {
        execlp("notify-send", "notify-send", "-a", "XephyGuard", "-u", urgent ? "critical" : "normal", title.c_str(),
               body.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }
    g_notify_children.add(pid);
    g_notify_children.reap();
}

#ifdef XG_HAVE_X11
std::atomic<bool> g_x11_bad_window{false};

int x11_error_handler(Display* d, XErrorEvent* ev) {
    if (ev && (ev->error_code == BadWindow || ev->error_code == BadDrawable)) {
        g_x11_bad_window.store(true, std::memory_order_relaxed);
        log::debug("x11: ignored transient resource error {} on window {:#x}", int(ev->error_code),
                   static_cast<unsigned long>(ev->resourceid));
        return 0;
    }
    if (d && ev) {
        char buf[128] = {0};
        XGetErrorText(d, ev->error_code, buf, sizeof buf - 1);
        log::debug("x11: non-fatal error {} ({}): req {}.{}", int(ev->error_code), buf, int(ev->request_code),
                   int(ev->minor_code));
    }
    return 0;
}

void ensure_x11_initialized() {
    static std::once_flag once;
    std::call_once(once, [] {
        XInitThreads();
        XSetErrorHandler(x11_error_handler);
    });
}

bool window_alive(Display* d, Window w) {
    if (!d || w == None) return false;
    g_x11_bad_window.store(false, std::memory_order_relaxed);
    XWindowAttributes attrs{};
    const Status ok = XGetWindowAttributes(d, w, &attrs);
    return ok != 0 && !g_x11_bad_window.load(std::memory_order_relaxed);
}

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
            return out.chars > 0 || out.lines > 0;
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
        ensure_x11_initialized();
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
                if (!window_alive(d, w)) {
                    last_w = 0;
                    return;
                }
                if (w != last_w) {
                    if (last_w && window_alive(d, last_w)) XSelectInput(d, last_w, NoEventMask);
                    g_x11_bad_window.store(false, std::memory_order_relaxed);
                    XSelectInput(d, w, PropertyChangeMask);  // title changes of the focused window
                    XSync(d, False);
                    if (g_x11_bad_window.load(std::memory_order_relaxed)) {
                        last_w = 0;
                        return;
                    }
                    last_w = w;
                }
                WindowInfo wi{window_app(d, w, pid), window_title(d, w, name, utf8), 0};
                if (!window_alive(d, w)) {
                    last_w = 0;
                    return;
                }
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
        ensure_x11_initialized();
        running_ = true;
        thread_ = std::thread([this, cb] {
            Display* d = XOpenDisplay(nullptr);
            if (!d) return;
            int ev_base = 0, err_base = 0;
            if (!XFixesQueryExtension(d, &ev_base, &err_base)) { XCloseDisplay(d); return; }
            const Atom clip = XInternAtom(d, "CLIPBOARD", False);
            const Atom active = XInternAtom(d, "_NET_ACTIVE_WINDOW", False);
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
                    if (pi.source_app.empty() && active != None) {
                        Atom t = None;
                        int f = 0;
                        unsigned long n = 0, a = 0;
                        unsigned char* data = nullptr;
                        if (XGetWindowProperty(d, DefaultRootWindow(d), active, 0, 1, False, XA_WINDOW, &t, &f, &n, &a,
                                               &data) == Success) {
                            if (data && n == 1) pi.source_app = window_app(d, *reinterpret_cast<Window*>(data), pid);
                            if (data) XFree(data);
                        }
                    }
                    if (measure_clipboard(d, win, pi, 500) && (pi.chars > 0 || pi.lines > 0)) cb(pi);
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

IndicatorWorkArea query_work_area(Display* d) {
    const int scr = DefaultScreen(d);
    const int scr_w = DisplayWidth(d, scr);
    const int scr_h = DisplayHeight(d, scr);
    const int top_reserve = std::min(36, std::max(0, scr_h / 4));
    IndicatorWorkArea fallback{0, top_reserve, scr_w, std::max(100, scr_h - top_reserve)};

    const Window root = RootWindow(d, scr);
    Atom desktop_atom = XInternAtom(d, "_NET_CURRENT_DESKTOP", True);
    long desktop = 0;
    if (desktop_atom != None) {
        Atom type = None;
        int fmt = 0;
        unsigned long n = 0, after = 0;
        unsigned char* data = nullptr;
        if (XGetWindowProperty(d, root, desktop_atom, 0, 1, False, XA_CARDINAL, &type, &fmt, &n, &after, &data) ==
                Success &&
            data && n == 1 && fmt == 32) {
            desktop = *reinterpret_cast<long*>(data);
        }
        if (data) XFree(data);
    }

    Atom workarea_atom = XInternAtom(d, "_NET_WORKAREA", True);
    if (workarea_atom == None) return fallback;

    Atom type = None;
    int fmt = 0;
    unsigned long n = 0, after = 0;
    unsigned char* data = nullptr;
    IndicatorWorkArea out = fallback;
    if (XGetWindowProperty(d, root, workarea_atom, 0, 64, False, XA_CARDINAL, &type, &fmt, &n, &after, &data) ==
            Success &&
        data && n >= 4 && fmt == 32) {
        const auto* vals = reinterpret_cast<const unsigned long*>(data);
        const unsigned long off = (desktop >= 0 && static_cast<unsigned long>(desktop + 1) * 4 <= n)
                                      ? static_cast<unsigned long>(desktop) * 4
                                      : 0;
        const int wx = static_cast<int>(vals[off + 0]);
        const int wy = static_cast<int>(vals[off + 1]);
        const int ww = static_cast<int>(vals[off + 2]);
        const int wh = static_cast<int>(vals[off + 3]);
        if (ww >= 140 && wh >= 40 && wx >= 0 && wy >= 0 && wx + ww <= scr_w && wy + wh <= scr_h) {
            out = IndicatorWorkArea{wx, wy, ww, wh};
        }
    }
    if (data) XFree(data);
    return out;
}

/// Small always-visible status pill positioned cleanly inside the desktop workarea (never over panels).
class X11StudentUi final : public IStudentUi {
public:
    X11StudentUi() {
        if (!x11_session()) return;
        ensure_x11_initialized();
        d_ = XOpenDisplay(nullptr);
        if (!d_) return;
        const int scr = DefaultScreen(d_);
        rect_ = compute_indicator_rect(query_work_area(d_), 160);

        XSetWindowAttributes a{};
        a.override_redirect = True;
        a.background_pixel = 0x1E293B;
        w_ = XCreateWindow(d_, RootWindow(d_, scr), rect_.x, rect_.y, static_cast<unsigned int>(rect_.width),
                           static_cast<unsigned int>(rect_.height), 0, CopyFromParent, InputOutput, CopyFromParent,
                           CWOverrideRedirect | CWBackPixel, &a);
        XSelectInput(d_, w_, ExposureMask);
        gc_ = XCreateGC(d_, w_, 0, nullptr);
        for (const char* name : {"-misc-fixed-medium-r-normal--13-120-75-75-c-70-iso8859-1", "7x13", "fixed"}) {
            font_ = XLoadQueryFont(d_, name);
            if (font_) {
                XSetFont(d_, gc_, font_->fid);
                break;
            }
        }
        XStoreName(d_, w_, "XephyGuard");

        const Atom win_type = XInternAtom(d_, "_NET_WM_WINDOW_TYPE", False);
        const Atom tooltip_type = XInternAtom(d_, "_NET_WM_WINDOW_TYPE_TOOLTIP", False);
        XChangeProperty(d_, w_, win_type, XA_ATOM, 32, PropModeReplace,
                        reinterpret_cast<const unsigned char*>(&tooltip_type), 1);

        const Atom wm_state = XInternAtom(d_, "_NET_WM_STATE", False);
        const Atom states[3] = {
            XInternAtom(d_, "_NET_WM_STATE_SKIP_TASKBAR", False),
            XInternAtom(d_, "_NET_WM_STATE_SKIP_PAGER", False),
            XInternAtom(d_, "_NET_WM_STATE_ABOVE", False),
        };
        XChangeProperty(d_, w_, wm_state, XA_ATOM, 32, PropModeReplace,
                        reinterpret_cast<const unsigned char*>(states), 3);

        // Pass pointer clicks through so the badge never intercepts underlying UI controls.
        XserverRegion empty = XFixesCreateRegion(d_, nullptr, 0);
        if (empty != None) {
            XFixesSetWindowShapeRegion(d_, w_, ShapeInput, 0, 0, empty);
            XFixesDestroyRegion(d_, empty);
        }
        XFlush(d_);
    }
    ~X11StudentUi() override {
        if (d_) {
            if (font_) XFreeFont(d_, font_);
            XFreeGC(d_, gc_);
            XDestroyWindow(d_, w_);
            XCloseDisplay(d_);
        }
    }
    void set_indicator(const std::string& text, const std::string& mode) override {
        if (text == text_ && mode == mode_) return;
        const bool first = text_.empty();
        text_ = text;
        display_text_ = sanitize_indicator_label(text);
        mode_ = mode;
        if (!d_) {
            if (first || mode != "always_on") spawn_notify("XephyGuard", text, false);
            return;
        }
        if (display_text_.empty()) {
            if (mapped_) {
                XUnmapWindow(d_, w_);
                XFlush(d_);
                mapped_ = false;
            }
            return;
        }
        update_geometry(true);
        if (!mapped_) {
            XMapRaised(d_, w_);
            mapped_ = true;
        }
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
            if (ev.type == Expose && mapped_) draw();
        }
        if (++ticks_ % 20 == 0 && mapped_) {
            if (update_geometry(false)) draw();
            XRaiseWindow(d_, w_);
            XFlush(d_);
        }
    }

private:
    bool update_geometry(bool) {
        if (!d_) return false;
        const int text_w = font_ ? XTextWidth(font_, display_text_.c_str(), int(display_text_.size()))
                                 : int(display_text_.size()) * 7;
        const IndicatorRect next = compute_indicator_rect(query_work_area(d_), text_w);
        if (next.x == rect_.x && next.y == rect_.y && next.width == rect_.width && next.height == rect_.height) {
            return false;
        }
        rect_ = next;
        XMoveResizeWindow(d_, w_, rect_.x, rect_.y, static_cast<unsigned int>(rect_.width),
                          static_cast<unsigned int>(rect_.height));
        return true;
    }

    void draw() {
        if (!d_ || !mapped_) return;
        unsigned long bg = 0x1E293B;
        unsigned long border = 0x475569;
        unsigned long dot = 0x22C55E;
        if (mode_ == "test" || display_text_.find("viewing") != std::string::npos) {
            bg = 0x7F1D1D;
            border = 0xEF4444;
            dot = 0xFCA5A5;
        } else if (display_text_.find("not reachable") != std::string::npos) {
            bg = 0x78350F;
            border = 0xF59E0B;
            dot = 0xFCD34D;
        } else if (mode_ == "practice") {
            bg = 0x1E3A8A;
            border = 0x3B82F6;
            dot = 0x93C5FD;
        }

        XSetForeground(d_, gc_, bg);
        XFillRectangle(d_, w_, gc_, 0, 0, static_cast<unsigned int>(rect_.width),
                       static_cast<unsigned int>(rect_.height));
        XSetForeground(d_, gc_, border);
        XDrawRectangle(d_, w_, gc_, 0, 0, static_cast<unsigned int>(rect_.width - 1),
                       static_cast<unsigned int>(rect_.height - 1));
        XSetForeground(d_, gc_, dot);
        XFillArc(d_, w_, gc_, 9, (rect_.height - 6) / 2, 6, 6, 0, 360 * 64);

        const int baseline = font_ ? (rect_.height + font_->ascent - font_->descent) / 2 : 16;
        XSetForeground(d_, gc_, 0xF8FAFC);
        XDrawString(d_, w_, gc_, 22, baseline, display_text_.c_str(), int(display_text_.size()));
        XFlush(d_);
    }

    Display* d_ = nullptr;
    Window w_ = 0;
    GC gc_{};
    XFontStruct* font_ = nullptr;
    IndicatorRect rect_{};
    bool mapped_ = false;
    std::string text_, display_text_, mode_;
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
    ensure_x11_initialized();
    static std::mutex mu;
    std::lock_guard lk(mu);
    Display* d = XOpenDisplay(nullptr);
    if (!d) return false;
    const Atom active = XInternAtom(d, "_NET_ACTIVE_WINDOW", False);
    const Atom pid_atom = XInternAtom(d, "_NET_WM_PID", False);
    if (active != None) {
        Atom t = None;
        int f = 0;
        unsigned long n = 0, a = 0;
        unsigned char* data = nullptr;
        if (XGetWindowProperty(d, DefaultRootWindow(d), active, 0, 1, False, XA_WINDOW, &t, &f, &n, &a, &data) ==
            Success) {
            if (data && n == 1) out.target_app = window_app(d, *reinterpret_cast<Window*>(data), pid_atom);
            if (data) XFree(data);
        }
    }
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
