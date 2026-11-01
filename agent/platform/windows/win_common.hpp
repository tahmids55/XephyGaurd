#pragma once
#include <windows.h>

#include <string>

#include "xg/interfaces.hpp"

namespace xg::win {

std::string to_utf8(const std::wstring& w);
std::wstring to_wide(const std::string& s);
std::string last_error_text(DWORD err = GetLastError());
/// Full image path of a process ("" if not accessible).
std::string process_image(DWORD pid);
std::string process_user(DWORD pid);
std::string profiles_dir();

std::unique_ptr<IProcessMonitor> make_process_monitor();
std::unique_ptr<INetworkMonitor> make_network_monitor();
std::unique_ptr<IDeviceMonitor> make_device_monitor();
std::unique_ptr<IExtensionScanner> make_extension_scanner();
std::unique_ptr<IFileWatcher> make_file_watcher();
std::unique_ptr<ISystemInfo> make_system_info();
std::unique_ptr<IGpuMonitor> make_gpu_monitor();
std::unique_ptr<IServiceHost> make_service_host();
std::unique_ptr<IHelperLauncher> make_helper_launcher(std::string helper_path);
std::unique_ptr<IIpcServer> make_ipc_server(std::string allowed_exe);
std::unique_ptr<IIpcClient> make_ipc_client();

std::unique_ptr<IWindowMonitor> make_window_monitor();
std::unique_ptr<IClipboardMonitor> make_clipboard_monitor();
std::unique_ptr<IInputTimingMonitor> make_input_monitor();
std::unique_ptr<IStudentUi> make_student_ui();
bool probe_clipboard(PasteInfo& out);

}  // namespace xg::win
