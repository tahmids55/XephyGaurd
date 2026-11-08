#pragma once
#include <string>
#include <vector>

#include "xg/interfaces.hpp"

namespace xg::lnx {

std::string read_small(const std::string& path);           // whole small file, "" if missing
std::string read_link(const std::string& path);
std::vector<std::string> list_dir(const std::string& path);
std::string user_name(uint32_t uid);
std::string user_home(const std::string& user);
uint32_t file_uid(const std::string& path);

std::unique_ptr<IProcessMonitor> make_process_monitor();
std::unique_ptr<INetworkMonitor> make_network_monitor();
std::unique_ptr<INetworkIsolator> make_network_isolator();
std::unique_ptr<IDeviceMonitor> make_device_monitor();
std::unique_ptr<IExtensionScanner> make_extension_scanner(ISystemInfo* sys);
std::unique_ptr<IFileWatcher> make_file_watcher();
std::unique_ptr<IInputTimingMonitor> make_evdev_input();
std::unique_ptr<ISystemInfo> make_system_info();
std::unique_ptr<IGpuMonitor> make_gpu_monitor();
std::unique_ptr<IServiceHost> make_service_host();
std::unique_ptr<IHelperLauncher> make_helper_launcher();
std::unique_ptr<IIpcServer> make_ipc_server(std::string allowed_exe);
std::unique_ptr<IIpcClient> make_ipc_client();

std::unique_ptr<IWindowMonitor> make_window_monitor();
std::unique_ptr<IClipboardMonitor> make_clipboard_monitor();
std::unique_ptr<IStudentUi> make_student_ui();
/// Helper-only: measure the current CLIPBOARD content (features only). Used for paste probes.
bool probe_clipboard(PasteInfo& out);

}  // namespace xg::lnx
