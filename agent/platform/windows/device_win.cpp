// USB device arrival/removal via RegisterDeviceNotification on a message-only window.
#include "win_common.hpp"  // must precede other Windows SDK headers
#include <dbt.h>
#include <initguid.h>
#include <usbiodef.h>
#include <winioctl.h>

#include <atomic>
#include <thread>

#include "xg/crypto.hpp"
#include "xg/util.hpp"

namespace xg::win {
namespace {

class DeviceMonitor final : public IDeviceMonitor {
public:
    ~DeviceMonitor() override { stop(); }

    bool start(std::function<void(const DeviceInfo&)> cb) override {
        cb_ = std::move(cb);
        thread_ = std::thread([this] { loop(); });
        return true;
    }
    void stop() override {
        if (hwnd_) PostMessageW(hwnd_, WM_CLOSE, 0, 0);
        if (thread_.joinable()) thread_.join();
        hwnd_ = nullptr;
    }

private:
    static LRESULT CALLBACK wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
        auto* self = reinterpret_cast<DeviceMonitor*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (msg == WM_DEVICECHANGE && self && (wp == DBT_DEVICEARRIVAL || wp == DBT_DEVICEREMOVECOMPLETE)) {
            auto* hdr = reinterpret_cast<DEV_BROADCAST_HDR*>(lp);
            if (hdr && hdr->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE) {
                auto* di = reinterpret_cast<DEV_BROADCAST_DEVICEINTERFACE_W*>(hdr);
                self->handle(to_utf8(di->dbcc_name), wp == DBT_DEVICEARRIVAL, di->dbcc_classguid);
            }
            return TRUE;
        }
        if (msg == WM_CLOSE) { DestroyWindow(h); return 0; }
        if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
        return DefWindowProcW(h, msg, wp, lp);
    }

    void handle(const std::string& name, bool arrival, const GUID& cls) {
        // \\?\USB#VID_18D1&PID_4EE7#SERIAL#{guid}  or  \\?\USBSTOR#Disk&Ven_...#...
        DeviceInfo d;
        d.action = arrival ? "add" : "remove";
        d.subsystem = "usb";
        const auto up = to_lower(name);
        auto vid = up.find("vid_"), pid = up.find("pid_");
        if (vid != std::string::npos) d.vendor_id = up.substr(vid + 4, 4);
        if (pid != std::string::npos) d.product_id = up.substr(pid + 4, 4);
        auto parts = split(name, '#');
        if (parts.size() >= 3) d.serial_hash = crypto::sha256_hex(parts[2]).substr(0, 16);
        if (IsEqualGUID(cls, GUID_DEVINTERFACE_DISK)) {
            if (up.find("usbstor") == std::string::npos) return;  // internal disks
            d.devclass = "usb_storage";
            d.model = parts.size() >= 2 ? parts[1] : "";
        } else {
            d.devclass = "usb_device";
        }
        if (cb_) cb_(d);
    }

    void loop() {
        WNDCLASSW wc{};
        wc.lpfnWndProc = wndproc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"XephyGuardDeviceMonitor";
        RegisterClassW(&wc);
        hwnd_ = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
        if (!hwnd_) return;
        SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
        for (const GUID& g : {GUID_DEVINTERFACE_USB_DEVICE, GUID_DEVINTERFACE_DISK}) {
            DEV_BROADCAST_DEVICEINTERFACE_W f{};
            f.dbcc_size = sizeof f;
            f.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
            f.dbcc_classguid = g;
            RegisterDeviceNotificationW(hwnd_, &f, DEVICE_NOTIFY_WINDOW_HANDLE);
        }
        MSG m;
        while (GetMessageW(&m, nullptr, 0, 0) > 0) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
    }

    std::function<void(const DeviceInfo&)> cb_;
    std::thread thread_;
    std::atomic<HWND> hwnd_{nullptr};
};

}  // namespace

std::unique_ptr<IDeviceMonitor> make_device_monitor() { return std::make_unique<DeviceMonitor>(); }

}  // namespace xg::win
