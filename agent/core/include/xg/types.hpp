#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace xg {

enum class Severity { info = 0, low = 1, medium = 2, high = 3, critical = 4 };
Severity parse_severity(const std::string& s);
const char* to_string(Severity s);

struct ProcessInfo {
    int64_t pid = 0;
    int64_t ppid = 0;
    std::string exe_path;   // full path when known
    std::string name;       // short name (comm / image name)
    std::string cmdline;    // used only for matching on the PC, never uploaded in full
    std::string user;
    uint64_t rss_bytes = 0;
    int64_t start_time = 0;     // OS-specific start tick; (pid,start_time) identifies a process
    double cpu_pct = 0.0;       // 0..100 total system CPU percentage
    double net_kbps = 0.0;      // per-process network throughput (KB/s)
    uint32_t net_conns = 0;     // active network connections owned by this PID
};

struct ListeningSocket {
    uint16_t port = 0;
    std::string address;    // "127.0.0.1", "0.0.0.0", "::"
    bool tcp = true;
    int64_t pid = 0;        // 0 if unknown
    std::string process;    // exe name if known
    uint64_t rss_bytes = 0;
};

struct AdapterInfo {
    std::string name;         // eth0, "Ethernet 2"
    std::string description;  // Windows friendly description
    std::string driver;       // kernel driver / service name
    std::string mac;
    std::vector<std::string> addresses;
    bool up = false;
    bool is_default_route = false;
    bool is_wireless = false;
    bool is_virtual = false;
};

struct DeviceInfo {
    std::string action;       // add / remove
    std::string subsystem;    // usb, block
    std::string devclass;     // usb_storage, disk, phone, ...
    std::string vendor_id;    // hex, lowercase
    std::string product_id;
    std::string model;
    std::string serial_hash;  // SHA-256 prefix of the serial, never the serial itself
};

struct ExtensionInfo {
    std::string ide;          // vscode, vscodium, cursor, jetbrains, chrome, edge, firefox
    std::string id;           // publisher.name (lowercase) or plugin dir name / browser ext id
    std::string version;
    std::string user;
    std::string path;
    bool active = false;      // true only when live runtime activity is detected
    std::string activity;     // how live usage was detected (e.g. "active process", "storage write")
};

struct ArtifactInfo {
    std::string path;
    uint64_t size_bytes = 0;
    std::string rule_id;
};

struct WindowInfo {
    std::string app;          // executable basename
    std::string title;        // filtered according to policy before leaving the PC
    int64_t pid = 0;
};

struct PasteInfo {
    bool is_copy = false;
    uint64_t chars = 0;
    uint64_t lines = 0;
    bool looks_like_code = false;
    double code_score = 0.0;
    std::string source_app;   // app owning the selection, if known
    std::string target_app;   // focused app at paste time
};

struct FileActivity {
    std::string dir;
    std::string file_ext;
    std::string action;       // modified / created / deleted
    int64_t size_delta = 0;
    uint64_t total_bytes = 0;
};

struct OutboundAiFlow {
    int64_t pid = 0;
    std::string process;      // normalized exe name, e.g. "copilot-runtime", "code", "idea64", "chrome"
    std::string remote_ip;
    uint16_t remote_port = 443;
    std::string domain;       // resolved via OS DNS cache / TLS SNI, e.g. "api.individual.githubcopilot.com", "chatgpt.com"
    uint64_t bytes_sent = 0;  // delta bytes sent
    uint64_t bytes_recv = 0;  // delta bytes received
};

}  // namespace xg
