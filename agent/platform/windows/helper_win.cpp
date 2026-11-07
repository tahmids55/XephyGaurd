// Helper-side Windows adapters (run in the user's session):
//  - foreground window via SetWinEventHook(EVENT_SYSTEM_FOREGROUND)
//  - copy events via AddClipboardFormatListener (content reduced to numbers, then wiped)
//  - key timing via WH_KEYBOARD_LL (timestamps + backspace/paste-chord bits only)
//  - tray icon indicator + balloon notifications via Shell_NotifyIcon
#include "win_common.hpp"  // must precede other Windows SDK headers
#include <shellapi.h>

#include <atomic>
#include <cstring>
#include <mutex>
#include <thread>

#include "xg/paste.hpp"
#include "xg/util.hpp"

namespace xg::win {
namespace {

std::string window_app(HWND w) {
    DWORD pid = 0;
    GetWindowThreadProcessId(w, &pid);
    return normalize_exe(process_image(pid));
}

/// Reads CF_UNICODETEXT, converts to features, wipes the copy. Caller must not hold the clipboard.
bool measure_clipboard(PasteInfo& out, HWND owner_hint) {
    for (int attempt = 0; attempt < 5; ++attempt) {
        if (OpenClipboard(owner_hint)) break;
        if (attempt == 4) return false;
        Sleep(20);
    }
    bool ok = false;
    if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
        if (auto* p = static_cast<const wchar_t*>(GlobalLock(h))) {
            std::wstring w(p, wcsnlen(p, 4 * 1024 * 1024));
            GlobalUnlock(h);
            std::string text = to_utf8(w);
            SecureZeroMemory(w.data(), w.size() * sizeof(wchar_t));
            PasteInfo pi = analyze_clipboard_text(text);
            SecureZeroMemory(text.data(), text.size());
            out.chars = pi.chars;
            out.lines = pi.lines;
            out.code_score = pi.code_score;
            out.looks_like_code = pi.looks_like_code;
            ok = true;
        }
    }
    if (HWND o = GetClipboardOwner()) out.source_app = window_app(o);
    CloseClipboard();
    return ok;
}

/// Runs a message loop on its own thread with a message-only window; subclasses handle messages.
class MsgThread {
public:
    virtual ~MsgThread() { stop_thread(); }

protected:
    bool start_thread(const wchar_t* cls) {
        cls_ = cls;
        running_ = true;
        thread_ = std::thread([this] {
            thread_id_ = GetCurrentThreadId();
            WNDCLASSW wc{};
            wc.lpfnWndProc = &MsgThread::proc;
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.lpszClassName = cls_;
            RegisterClassW(&wc);
            hwnd_ = CreateWindowExW(0, cls_, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
            SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
            on_thread_start();
            MSG m;
            while (running_ && GetMessageW(&m, nullptr, 0, 0) > 0) {
                TranslateMessage(&m);
                DispatchMessageW(&m);
            }
            on_thread_stop();
            DestroyWindow(hwnd_);
        });
        return true;
    }
    void stop_thread() {
        if (!running_) return;
        running_ = false;
        PostThreadMessageW(thread_id_, WM_QUIT, 0, 0);
        if (thread_.joinable()) thread_.join();
    }
    virtual void on_thread_start() {}
    virtual void on_thread_stop() {}
    virtual LRESULT on_message(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcW(h, m, w, l); }

    HWND hwnd_ = nullptr;
    std::atomic<DWORD> thread_id_{0};

private:
    static LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l) {
        auto* self = reinterpret_cast<MsgThread*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        return self ? self->on_message(h, m, w, l) : DefWindowProcW(h, m, w, l);
    }
    const wchar_t* cls_ = L"";
    std::atomic<bool> running_{false};
    std::thread thread_;
};

// ------------------------------------------------------------------------------ window monitor
std::function<void(const WindowInfo&)> g_window_cb;

void CALLBACK win_event(HWINEVENTHOOK, DWORD, HWND hwnd, LONG id_object, LONG, DWORD, DWORD) {
    if (id_object != OBJID_WINDOW || !hwnd || !g_window_cb) return;
    wchar_t title[512];
    int n = GetWindowTextW(hwnd, title, 512);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    g_window_cb(WindowInfo{window_app(hwnd), to_utf8(std::wstring(title, size_t(n))), int64_t(pid)});
}

class WindowMonitor final : public IWindowMonitor, MsgThread {
public:
    ~WindowMonitor() override { stop(); }
    bool available() const override { return true; }
    bool start(std::function<void(const WindowInfo&)> cb) override {
        g_window_cb = std::move(cb);
        return start_thread(L"XephyGuardWindowMon");
    }
    void stop() override { stop_thread(); }

private:
    void on_thread_start() override {
        hook_ = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr, win_event, 0, 0,
                                WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
        if (HWND fg = GetForegroundWindow()) win_event(nullptr, 0, fg, OBJID_WINDOW, 0, 0, 0);
    }
    void on_thread_stop() override {
        if (hook_) UnhookWinEvent(hook_);
        hook_ = nullptr;
    }
    HWINEVENTHOOK hook_ = nullptr;
};

// --------------------------------------------------------------------------- clipboard monitor
class ClipboardMonitor final : public IClipboardMonitor, MsgThread {
public:
    ~ClipboardMonitor() override { stop(); }
    bool available() const override { return true; }
    bool start(std::function<void(const PasteInfo&)> cb) override {
        cb_ = std::move(cb);
        return start_thread(L"XephyGuardClipMon");
    }
    void stop() override { stop_thread(); }

private:
    void on_thread_start() override { AddClipboardFormatListener(hwnd_); }
    void on_thread_stop() override { RemoveClipboardFormatListener(hwnd_); }
    LRESULT on_message(HWND h, UINT m, WPARAM w, LPARAM l) override {
        if (m == WM_CLIPBOARDUPDATE) {
            PasteInfo pi;
            pi.is_copy = true;
            if (measure_clipboard(pi, hwnd_) && cb_) cb_(pi);
            return 0;
        }
        return DefWindowProcW(h, m, w, l);
    }
    std::function<void(const PasteInfo&)> cb_;
};

// ------------------------------------------------------------------------------- key timing
std::function<void(int64_t, bool, bool)> g_key_cb;

LRESULT CALLBACK ll_keyboard(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION && (wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN) && g_key_cb) {
        const auto* k = reinterpret_cast<KBDLLHOOKSTRUCT*>(lp);
        {  // injected keys (SendInput, auto-typers) are counted too: they are part of what we measure
            const DWORD vk = k->vkCode;
            const bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
            const bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
            const bool modifier = vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL || vk == VK_SHIFT ||
                                  vk == VK_LSHIFT || vk == VK_RSHIFT || vk == VK_MENU || vk == VK_LMENU || vk == VK_RMENU;
            if (!modifier) g_key_cb(now_ms(), vk == VK_BACK, (ctrl && vk == 'V') || (shift && vk == VK_INSERT));
        }
    }
    return CallNextHookEx(nullptr, code, wp, lp);  // never swallow keys
}

class InputMonitor final : public IInputTimingMonitor, MsgThread {
public:
    ~InputMonitor() override { stop(); }
    bool available() const override { return true; }
    bool start(std::function<void(int64_t, bool, bool)> cb) override {
        g_key_cb = std::move(cb);
        return start_thread(L"XephyGuardKeyTiming");
    }
    void stop() override { stop_thread(); }

private:
    void on_thread_start() override { hook_ = SetWindowsHookExW(WH_KEYBOARD_LL, ll_keyboard, GetModuleHandleW(nullptr), 0); }
    void on_thread_stop() override {
        if (hook_) UnhookWindowsHookEx(hook_);
        hook_ = nullptr;
    }
    HHOOK hook_ = nullptr;
};

// --------------------------------------------------------------------------------- tray UI
class TrayUi final : public IStudentUi {
public:
    TrayUi() {
        WNDCLASSW wc{};
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"XephyGuardTray";
        RegisterClassW(&wc);
        hwnd_ = CreateWindowExW(0, wc.lpszClassName, L"XephyGuard", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance,
                                nullptr);
        nid_.cbSize = sizeof nid_;
        nid_.hWnd = hwnd_;
        nid_.uID = 1;
        nid_.uFlags = NIF_ICON | NIF_TIP | NIF_SHOWTIP;
        nid_.hIcon = LoadIconW(nullptr, MAKEINTRESOURCEW(32518));  // IDI_SHIELD
        wcsncpy_s(nid_.szTip, L"Monitored by XephyGuard", _TRUNCATE);
        Shell_NotifyIconW(NIM_ADD, &nid_);
        nid_.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &nid_);
    }
    ~TrayUi() override { Shell_NotifyIconW(NIM_DELETE, &nid_); }
    void set_indicator(const std::string& text, const std::string&) override {
        nid_.uFlags = NIF_TIP | NIF_SHOWTIP;
        wcsncpy_s(nid_.szTip, to_wide(text).c_str(), _TRUNCATE);
        Shell_NotifyIconW(NIM_MODIFY, &nid_);
    }
    void notify(const std::string& title, const std::string& body, bool urgent) override {
        nid_.uFlags = NIF_INFO | NIF_ICON | NIF_TIP;
        wcsncpy_s(nid_.szInfoTitle, to_wide(title).c_str(), _TRUNCATE);
        wcsncpy_s(nid_.szInfo, to_wide(body).c_str(), _TRUNCATE);
        nid_.dwInfoFlags = urgent ? (NIIF_WARNING | NIIF_LARGE_ICON) : (NIIF_INFO | NIIF_LARGE_ICON);
        Shell_NotifyIconW(NIM_MODIFY, &nid_);

        // Also display an unmistakable modal message dialog directly in the student's desktop session
        // in a detached thread so it does not block the helper message pump.
        const auto wtitle = to_wide(title.empty() ? "Message from your instructor" : title);
        const auto wbody = to_wide(body);
        std::thread([wtitle, wbody, urgent]() {
            UINT flags = MB_OK | MB_TOPMOST | MB_SETFOREGROUND;
            flags |= urgent ? (MB_ICONWARNING | MB_SYSTEMMODAL) : MB_ICONINFORMATION;
            MessageBoxW(nullptr, wbody.c_str(), wtitle.c_str(), flags);
        }).detach();
    }
    void pump(int ms) override {
        MsgWaitForMultipleObjects(0, nullptr, FALSE, DWORD(ms), QS_ALLINPUT);
        MSG m;
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
    }

private:
    HWND hwnd_ = nullptr;
    NOTIFYICONDATAW nid_{};
};

}  // namespace

std::unique_ptr<IWindowMonitor> make_window_monitor() { return std::make_unique<WindowMonitor>(); }
std::unique_ptr<IClipboardMonitor> make_clipboard_monitor() { return std::make_unique<ClipboardMonitor>(); }
std::unique_ptr<IInputTimingMonitor> make_input_monitor() { return std::make_unique<InputMonitor>(); }
std::unique_ptr<IStudentUi> make_student_ui() { return std::make_unique<TrayUi>(); }
bool probe_clipboard(PasteInfo& out) {
    Sleep(30);  // let the target application finish its own clipboard read first
    return measure_clipboard(out, nullptr);
}

}  // namespace xg::win
