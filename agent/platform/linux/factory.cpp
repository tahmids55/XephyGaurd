#include "linux_common.hpp"

#ifdef XG_HAVE_X11
#include <X11/Xlib.h>
#endif

namespace xg {

std::unique_ptr<ServicePlatform> make_service_platform(const Config& cfg) {
    auto p = std::make_unique<ServicePlatform>();
    p->system = lnx::make_system_info();
    p->processes = lnx::make_process_monitor();
    p->network = lnx::make_network_monitor();
    p->devices = lnx::make_device_monitor();
    p->extensions = lnx::make_extension_scanner(p->system.get());
    p->files = lnx::make_file_watcher();
    p->input = lnx::make_evdev_input();  // root can read evdev: works on X11 and Wayland
    p->gpu = lnx::make_gpu_monitor();
    p->service_host = lnx::make_service_host();
    p->helper_launcher = lnx::make_helper_launcher();
    p->ipc = lnx::make_ipc_server(cfg.helper_path);
    return p;
}

std::unique_ptr<HelperPlatform> make_helper_platform() {
#ifdef XG_HAVE_X11
    XInitThreads();
#endif
    auto p = std::make_unique<HelperPlatform>();
    p->window = lnx::make_window_monitor();
    p->clipboard = lnx::make_clipboard_monitor();
    p->input = nullptr;  // the root service reads key timing from evdev on Linux
    p->ui = lnx::make_student_ui();
    p->ipc = lnx::make_ipc_client();
    p->probe_clipboard = [](PasteInfo& out) { return lnx::probe_clipboard(out); };
    return p;
}

}  // namespace xg
