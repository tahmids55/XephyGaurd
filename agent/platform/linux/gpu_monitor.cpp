// GPU usage: NVIDIA via NVML (dlopen, optional) or AMD/Intel via sysfs.
#include <dlfcn.h>

#include "linux_common.hpp"
#include "xg/util.hpp"

namespace xg::lnx {
namespace {

struct NvmlUtil { unsigned int gpu, memory; };
struct NvmlMem { unsigned long long total, free, used; };
using nvmlInit_t = int (*)();
using nvmlGetHandle_t = int (*)(unsigned int, void**);
using nvmlGetUtil_t = int (*)(void*, NvmlUtil*);
using nvmlGetMem_t = int (*)(void*, NvmlMem*);
using nvmlGetName_t = int (*)(void*, char*, unsigned int);

class GpuMonitor final : public IGpuMonitor {
public:
    GpuMonitor() {
        lib_ = dlopen("libnvidia-ml.so.1", RTLD_LAZY | RTLD_LOCAL);
        if (lib_) {
            auto init = reinterpret_cast<nvmlInit_t>(dlsym(lib_, "nvmlInit_v2"));
            get_handle_ = reinterpret_cast<nvmlGetHandle_t>(dlsym(lib_, "nvmlDeviceGetHandleByIndex_v2"));
            get_util_ = reinterpret_cast<nvmlGetUtil_t>(dlsym(lib_, "nvmlDeviceGetUtilizationRates"));
            get_mem_ = reinterpret_cast<nvmlGetMem_t>(dlsym(lib_, "nvmlDeviceGetMemoryInfo"));
            get_name_ = reinterpret_cast<nvmlGetName_t>(dlsym(lib_, "nvmlDeviceGetName"));
            if (!init || init() != 0 || !get_handle_ || get_handle_(0, &dev_) != 0) dev_ = nullptr;
        }
    }
    ~GpuMonitor() override { if (lib_) dlclose(lib_); }

    std::optional<Sample> sample() override {
        if (dev_ && get_util_ && get_mem_) {
            NvmlUtil u{};
            NvmlMem m{};
            if (get_util_(dev_, &u) == 0 && get_mem_(dev_, &m) == 0) {
                Sample s;
                s.util_pct = u.gpu;
                s.mem_used_mb = double(m.used) / 1048576.0;
                s.mem_total_mb = double(m.total) / 1048576.0;
                char name[96] = {0};
                if (get_name_) get_name_(dev_, name, sizeof name);
                s.name = name;
                return s;
            }
        }
        for (const auto& card : list_dir("/sys/class/drm")) {
            if (!card.starts_with("card") || card.find('-') != std::string::npos) continue;
            const std::string d = "/sys/class/drm/" + card + "/device/";
            auto busy = trim(read_small(d + "gpu_busy_percent"));
            if (busy.empty()) continue;
            Sample s;
            s.util_pct = std::stod(busy);
            auto used = trim(read_small(d + "mem_info_vram_used"));
            auto total = trim(read_small(d + "mem_info_vram_total"));
            if (!used.empty()) s.mem_used_mb = std::stod(used) / 1048576.0;
            if (!total.empty()) s.mem_total_mb = std::stod(total) / 1048576.0;
            s.name = card;
            return s;
        }
        return std::nullopt;
    }

private:
    void* lib_ = nullptr;
    void* dev_ = nullptr;
    nvmlGetHandle_t get_handle_ = nullptr;
    nvmlGetUtil_t get_util_ = nullptr;
    nvmlGetMem_t get_mem_ = nullptr;
    nvmlGetName_t get_name_ = nullptr;
};

}  // namespace

std::unique_ptr<IGpuMonitor> make_gpu_monitor() { return std::make_unique<GpuMonitor>(); }

}  // namespace xg::lnx
