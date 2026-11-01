// Linux device monitor: kernel uevents over NETLINK_KOBJECT_UEVENT (no libudev dependency).
#include <linux/netlink.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <map>
#include <thread>

#include "linux_common.hpp"
#include "xg/crypto.hpp"
#include "xg/util.hpp"

namespace xg::lnx {
namespace {

class DeviceMonitor final : public IDeviceMonitor {
public:
    ~DeviceMonitor() override { stop(); }

    bool start(std::function<void(const DeviceInfo&)> cb) override {
        int s = ::socket(AF_NETLINK, SOCK_DGRAM | SOCK_CLOEXEC, NETLINK_KOBJECT_UEVENT);
        if (s < 0) return false;
        sockaddr_nl sa{};
        sa.nl_family = AF_NETLINK;
        sa.nl_groups = 1;  // kernel uevents
        if (::bind(s, reinterpret_cast<sockaddr*>(&sa), sizeof sa) < 0) { ::close(s); return false; }
        fd_ = s;
        running_ = true;
        thread_ = std::thread([this, cb] {
            char buf[8192];
            while (running_) {
                pollfd pfd{fd_, POLLIN, 0};
                if (::poll(&pfd, 1, 500) <= 0) continue;
                ssize_t n = ::recv(fd_, buf, sizeof buf - 1, 0);
                if (n <= 0) continue;
                std::map<std::string, std::string> kv;
                for (ssize_t i = 0; i < n;) {
                    std::string item(buf + i);
                    i += ssize_t(item.size()) + 1;
                    auto eq = item.find('=');
                    if (eq != std::string::npos) kv[item.substr(0, eq)] = item.substr(eq + 1);
                }
                if (auto d = interpret(kv)) cb(*d);
            }
        });
        return true;
    }

    void stop() override {
        running_ = false;
        if (thread_.joinable()) thread_.join();
        if (fd_ >= 0) ::close(fd_);
        fd_ = -1;
    }

private:
    static std::string pad4(const std::string& hex) {
        std::string h = to_lower(hex);
        while (h.size() < 4) h = "0" + h;
        return h;
    }

    std::optional<DeviceInfo> interpret(const std::map<std::string, std::string>& kv) {
        auto get = [&](const char* k) { auto it = kv.find(k); return it == kv.end() ? std::string{} : it->second; };
        const auto action = get("ACTION");
        if (action != "add" && action != "remove") return std::nullopt;
        const auto subsystem = get("SUBSYSTEM");
        DeviceInfo d;
        d.action = action;
        d.subsystem = subsystem;
        if (subsystem == "usb" && get("DEVTYPE") == "usb_device") {
            auto prod = split(get("PRODUCT"), '/');
            if (prod.size() >= 2) { d.vendor_id = pad4(prod[0]); d.product_id = pad4(prod[1]); }
            const std::string sys = "/sys" + get("DEVPATH");
            d.model = trim(read_small(sys + "/manufacturer") + " " + read_small(sys + "/product"));
            const auto serial = trim(read_small(sys + "/serial"));
            if (!serial.empty()) d.serial_hash = crypto::sha256_hex(serial).substr(0, 16);
            d.devclass = "usb_device";
            if (action == "remove") {
                // sysfs is gone on remove: reuse what we saw on add.
                if (auto it = seen_.find(get("DEVPATH")); it != seen_.end()) { auto c = it->second; c.action = "remove"; seen_.erase(it); return c; }
            } else seen_[get("DEVPATH")] = d;
            return d;
        }
        if (subsystem == "usb" && get("DEVTYPE") == "usb_interface" && action == "add") {
            // INTERFACE=class/subclass/protocol ; class 8 = mass storage
            if (get("INTERFACE").rfind("8/", 0) == 0) {
                d.devclass = "usb_storage";
                auto prod = split(get("PRODUCT"), '/');
                if (prod.size() >= 2) { d.vendor_id = pad4(prod[0]); d.product_id = pad4(prod[1]); }
                return d;
            }
            return std::nullopt;
        }
        if (subsystem == "block" && get("DEVTYPE") == "disk") {
            const auto devpath = get("DEVPATH");
            if (devpath.find("/usb") == std::string::npos) return std::nullopt;  // only removable USB disks
            d.devclass = "disk";
            d.model = get("DEVNAME");
            return d;
        }
        return std::nullopt;
    }

    std::map<std::string, DeviceInfo> seen_;
    std::atomic<bool> running_{false};
    std::thread thread_;
    int fd_ = -1;
};

}  // namespace

std::unique_ptr<IDeviceMonitor> make_device_monitor() { return std::make_unique<DeviceMonitor>(); }

}  // namespace xg::lnx
