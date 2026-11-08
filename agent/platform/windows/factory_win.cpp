#include "win_common.hpp"

namespace xg {

std::unique_ptr<ServicePlatform> make_service_platform(const Config& cfg) {
    auto p = std::make_unique<ServicePlatform>();
    p->system = win::make_system_info();
    p->processes = win::make_process_monitor();
    p->network = win::make_network_monitor();
    p->isolator = win::make_network_isolator();
    p->devices = win::make_device_monitor();
    p->extensions = win::make_extension_scanner();
    p->files = win::make_file_watcher();
    p->input = nullptr;  // session 0 cannot see user input: the helper hooks key timing
    p->gpu = win::make_gpu_monitor();
    p->service_host = win::make_service_host();
    p->helper_launcher = win::make_helper_launcher(cfg.helper_path);
    p->ipc = win::make_ipc_server(cfg.helper_path);
    return p;
}

std::unique_ptr<HelperPlatform> make_helper_platform() {
    auto p = std::make_unique<HelperPlatform>();
    p->window = win::make_window_monitor();
    p->clipboard = win::make_clipboard_monitor();
    p->input = win::make_input_monitor();
    p->ui = win::make_student_ui();
    p->ipc = win::make_ipc_client();
    p->probe_clipboard = [](PasteInfo& out) { return win::probe_clipboard(out); };
    return p;
}

}  // namespace xg
