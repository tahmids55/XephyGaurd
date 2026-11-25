#pragma once
// Platform adapter contracts. Each OS implements these; the core only talks to the interfaces,
// so every piece of agent logic can be unit-tested with fakes (see tests/fakes.hpp).
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "xg/config.hpp"
#include "xg/event.hpp"
#include "xg/signatures.hpp"
#include "xg/types.hpp"

namespace xg {

class IProcessMonitor {
public:
    virtual ~IProcessMonitor() = default;
    /// Full process list (cheap: implementations cache per pid).
    virtual std::vector<ProcessInfo> snapshot() = 0;
    /// Optional push notifications of exec events (netlink proc connector / ETW). Returns false if
    /// unsupported; the agent then relies on periodic snapshots only.
    virtual bool watch(std::function<void(const ProcessInfo&)> on_exec) = 0;
    /// Terminates `pid`. With `expected_start` != 0 it acts only if that pid still has this start time
    /// (ProcessInfo::start_time), checked race-free (pidfd / process handle), so a reused pid is never hit.
    virtual bool kill(int64_t pid, int64_t expected_start = 0) = 0;
    virtual void stop() = 0;
};

class INetworkMonitor {
public:
    virtual ~INetworkMonitor() = default;
    virtual std::vector<AdapterInfo> adapters() = 0;
    virtual std::vector<ListeningSocket> listening_sockets() = 0;
    virtual std::vector<OutboundAiFlow> active_ai_flows() { return {}; }
    /// Established TCP connections for which WebHints::is_tunnel() is true (hints from set_web_hints).
    virtual std::vector<OutboundTunnel> active_tunnels() { return {}; }
    /// Updates the set of monitored AI / IDE host suffixes from synced signatures/blocklists.
    virtual void set_ai_domains(std::vector<std::string> /*domains*/) {}
    /// Telemetry hosts and per-process fallback domains from the bundle's web section.
    virtual void set_web_hints(const WebHints& /*hints*/) {}
    /// Calls `on_change` (debounced by the caller) when links, addresses or routes change.
    virtual bool watch(std::function<void()> on_change) = 0;
    virtual void stop() = 0;
};

/// Enforces "Isolate": outbound Internet is cut, LAN (and the exempt hosts, i.e. the CCC) keeps working.
class INetworkIsolator {
public:
    virtual ~INetworkIsolator() = default;
    /// Installs the block rules. Idempotent. `exempt_hosts` are host names / IPs that must stay reachable.
    virtual bool isolate(const std::vector<std::string>& exempt_hosts) = 0;
    /// Removes every rule installed by `isolate`.
    virtual bool restore() = 0;
    /// True while the block rules are present and enabled (the agent re-checks this periodically).
    virtual bool active() = 0;
    /// Session egress lock, independent of isolate()/restore() (own table / firewall rule). `off` removes it.
    /// Idempotent. Returns false if the rules could not be installed (or the adapter has no lock).
    virtual bool set_network_lock(NetworkLock level, const std::vector<std::string>& exempt_hosts) {
        (void)exempt_hosts;
        return level == NetworkLock::off;
    }
    /// The lock that is installed right now (the agent re-checks this for tamper).
    virtual NetworkLock network_lock() { return NetworkLock::off; }
};

class IWindowMonitor {
public:
    virtual ~IWindowMonitor() = default;
    virtual bool available() const = 0;
    virtual bool start(std::function<void(const WindowInfo&)> on_focus) = 0;
    virtual void stop() = 0;
};

class IClipboardMonitor {
public:
    virtual ~IClipboardMonitor() = default;
    virtual bool available() const = 0;
    /// Reports copy events (selection owner changed). Content is analyzed into numbers and dropped.
    virtual bool start(std::function<void(const PasteInfo&)> on_event) = 0;
    virtual void stop() = 0;
};

class IInputTimingMonitor {
public:
    virtual ~IInputTimingMonitor() = default;
    virtual bool available() const = 0;
    /// `on_key(t_ms, is_backspace, is_paste_chord)` for key-down events. No key identity is exposed.
    virtual bool start(std::function<void(int64_t, bool, bool)> on_key) = 0;
    virtual void stop() = 0;
};

class IFileWatcher {
public:
    virtual ~IFileWatcher() = default;
    virtual bool watch(const std::vector<std::string>& dirs, std::function<void(const FileActivity&)> cb) = 0;
    virtual void stop() = 0;
};

class IExtensionScanner {
public:
    virtual ~IExtensionScanner() = default;
    virtual std::vector<ExtensionInfo> scan() = 0;
};

class IDeviceMonitor {
public:
    virtual ~IDeviceMonitor() = default;
    virtual std::vector<DeviceInfo> scan() { return {}; }
    virtual bool start(std::function<void(const DeviceInfo&)> cb) = 0;
    virtual void stop() = 0;
};

struct UserSession {
    std::string user;
    std::string home;
    uint32_t uid = 0;          // Linux uid / Windows session id
    std::string display;       // ":0", "wayland-0", "console"
    std::string session_type;  // x11, wayland, tty, rdp, console
    bool active = false;
};

class ISystemInfo {
public:
    virtual ~ISystemInfo() = default;
    virtual std::string hostname() = 0;
    virtual std::string os_name() = 0;
    virtual std::string os_version() = 0;
    virtual std::vector<std::string> mac_addresses() = 0;
    virtual std::vector<UserSession> sessions() = 0;
    virtual int64_t uptime_s() = 0;
    virtual std::string self_exe_path() = 0;
    struct SelfUsage { uint64_t rss_bytes = 0; uint64_t cpu_time_ms = 0; };
    virtual SelfUsage self_usage() = 0;
    /// Enables/disables exam-mode lockdown policies (e.g. DisableTaskMgr on Windows) cleanly via official OS APIs.
    virtual void set_exam_mode(bool /*active*/) {}
    /// Suppresses or restores browser AI Overview search features (Chrome, Edge, Brave, Firefox) via official enterprise policies.
    virtual void set_browser_ai_overview_suppressed(bool /*suppressed*/, const std::string& /*search_url*/ = {}) {}
    /// Configures or clears browser PAC proxy policies (Chrome, Edge, Brave, Opera, Vivaldi, Firefox) for proxy_only mode.
    virtual void set_browser_proxy_pac(const std::string& /*pac_url*/) {}
    /// Blocks (non-empty) or unblocks (empty) Chromium-family browser extensions by Web Store id through managed
    /// browser policy (ExtensionInstallBlocklist), keeping entries an administrator added (plan alternative F80).
    virtual void set_browser_extension_blocklist(const std::vector<std::string>& /*chromium_ids*/) {}
    /// Pins system DNS to the lab resolver when gateway mode is active on Linux; empty clears the override.
    virtual void set_lab_dns(const std::string& /*dns_ip*/) {}
    /// Screen capabilities to report before any helper has sent its own `screen_caps`.
    virtual json fallback_screen_caps(const std::vector<UserSession>& /*sessions*/, bool /*screen_enabled*/) {
        return {{"session_type", "unknown"}, {"snapshot", false}, {"live", false}, {"delta_tiles", false},
                {"monitors", 1}, {"secure_desktop", false}};
    }
};

class IGpuMonitor {
public:
    virtual ~IGpuMonitor() = default;
    struct Sample { double util_pct = 0; double mem_used_mb = 0; double mem_total_mb = 0; std::string name; };
    virtual bool available() const { return true; }
    virtual std::optional<Sample> sample() = 0;
};

/// How the agent process is hosted: Windows SCM service or systemd unit (with watchdog).
class IServiceHost {
public:
    virtual ~IServiceHost() = default;
    /// Runs `body(stop_flag)` under the service manager; returns process exit code.
    virtual int run(const std::function<int(std::atomic<bool>&)>& body) = 0;
    virtual void notify_ready() = 0;
    virtual void watchdog_ping() = 0;
    virtual void notify_stopping() = 0;
};

/// Starts/monitors the session helper in each interactive user session.
class IHelperLauncher {
public:
    virtual ~IHelperLauncher() = default;
    /// Called periodically: (re)launch helpers for sessions that have none. Returns number launched.
    virtual int ensure_helpers(const std::vector<UserSession>& sessions, const std::vector<uint32_t>& connected) = 0;
};

/// Local authenticated IPC between service (server) and helpers (clients). Messages are JSON lines.
class IIpcServer {
public:
    virtual ~IIpcServer() = default;
    struct Peer { uint64_t conn_id = 0; uint32_t uid = 0; int64_t pid = 0; std::string user; };
    virtual bool start(const std::string& endpoint,
                       std::function<void(const Peer&, const std::string& line)> on_message,
                       std::function<void(const Peer&, bool connected)> on_peer) = 0;
    virtual bool send(uint64_t conn_id, const std::string& line) = 0;
    virtual void broadcast(const std::string& line) = 0;
    virtual void stop() = 0;
    /// Receives screen frame records (opaque bytes, type and size already checked, ipc_framing.hpp). Set before
    /// start(); without a handler frames are dropped. Called on the IPC thread: must not block for long.
    using FrameHandler = std::function<void(const Peer&, std::string&& frame)>;
    virtual void set_frame_handler(FrameHandler h) { (void)h; }
};

class IIpcClient {
public:
    virtual ~IIpcClient() = default;
    virtual bool connect(const std::string& endpoint) = 0;
    virtual bool send(const std::string& line) = 0;
    /// Blocks up to timeout for one line; nullopt on timeout or disconnect (check connected()).
    virtual std::optional<std::string> receive(int timeout_ms) = 0;
    virtual bool connected() const = 0;
    virtual void close() = 0;
};

/// Helper-side UI: transparency indicator and on-screen notices to the student.
class IStudentUi {
public:
    virtual ~IStudentUi() = default;
    virtual void set_indicator(const std::string& text, const std::string& mode) = 0;
    virtual void notify(const std::string& title, const std::string& body, bool urgent) = 0;
    /// Pumps the UI event loop for up to `ms` (needed by X11/Win32 message loops).
    virtual void pump(int ms) = 0;
};

/// Everything the service process needs from the OS.
struct ServicePlatform {
    std::unique_ptr<IProcessMonitor> processes;
    std::unique_ptr<INetworkMonitor> network;
    std::unique_ptr<INetworkIsolator> isolator;
    std::unique_ptr<IDeviceMonitor> devices;
    std::unique_ptr<IExtensionScanner> extensions;
    std::unique_ptr<IFileWatcher> files;
    std::unique_ptr<IInputTimingMonitor> input;     // Linux: evdev from root. Windows: nullptr (helper does it)
    std::unique_ptr<ISystemInfo> system;
    std::unique_ptr<IGpuMonitor> gpu;
    std::unique_ptr<IServiceHost> service_host;
    std::unique_ptr<IHelperLauncher> helper_launcher;
    std::unique_ptr<IIpcServer> ipc;
};

/// Per-user OS settings the helper applies inside the session (adapters only, never in apps/).
class IHelperSystem {
public:
    virtual ~IHelperSystem() = default;
    /// Windows: HKCU DisableTaskMgr during Test mode. Other platforms: no-op.
    virtual void set_taskmgr_disabled(bool /*disabled*/) {}
    /// Sets (non-empty) or clears (empty) the user's system proxy PAC URL. Idempotent.
    virtual void set_user_proxy(const std::string& pac_url) = 0;
};

/// Everything the per-session helper needs.
struct HelperPlatform {
    std::unique_ptr<IWindowMonitor> window;
    std::unique_ptr<IClipboardMonitor> clipboard;
    std::unique_ptr<IInputTimingMonitor> input;     // Windows low-level hook (timing only); Linux: nullptr
    std::unique_ptr<IStudentUi> ui;
    std::unique_ptr<IIpcClient> ipc;
    std::unique_ptr<IHelperSystem> system;
    /// Measures the clipboard right now (features only). Used when a paste is observed.
    std::function<bool(PasteInfo&)> probe_clipboard;
};

std::unique_ptr<ServicePlatform> make_service_platform(const Config& cfg);
std::unique_ptr<HelperPlatform> make_helper_platform();
/// Default IPC endpoint for this OS.
std::string default_ipc_endpoint();
std::string default_data_dir();
std::string default_log_dir();
std::string default_config_dir();
/// Installed path of the session helper binary.
std::string default_helper_path();

}  // namespace xg
