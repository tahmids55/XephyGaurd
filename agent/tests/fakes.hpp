#pragma once
// Fake platform adapters: let the agent core be tested without touching the OS.
#include <map>
#include <mutex>

#include "xg/event.hpp"
#include "xg/interfaces.hpp"

namespace xg::test {

class FakeProcesses : public IProcessMonitor {
public:
    std::vector<ProcessInfo> procs;
    std::vector<int64_t> killed;
    std::vector<ProcessInfo> snapshot() override { return procs; }
    bool watch(std::function<void(const ProcessInfo&)>) override { return false; }
    bool kill(int64_t pid, int64_t expected_start = 0) override {
        for (const auto& p : procs)
            if (p.pid == pid && expected_start != 0 && p.start_time != expected_start) return false;  // pid reused
        killed.push_back(pid);
        return true;
    }
    void stop() override {}
};

class FakeNetwork : public INetworkMonitor {
public:
    std::vector<AdapterInfo> list;
    std::vector<ListeningSocket> socks;
    std::vector<OutboundAiFlow> flows;
    std::vector<AdapterInfo> adapters() override { return list; }
    std::vector<ListeningSocket> listening_sockets() override { return socks; }
    std::vector<OutboundAiFlow> active_ai_flows() override { return flows; }
    std::vector<OutboundTunnel> tunnels;
    std::vector<OutboundTunnel> active_tunnels() override { return tunnels; }
    bool watch(std::function<void()>) override { return false; }
    void stop() override {}
};

class FakeIsolator : public INetworkIsolator {
public:
    bool on = false;
    int isolate_calls = 0, restore_calls = 0;
    std::vector<std::string> exempt;
    bool isolate(const std::vector<std::string>& e) override { on = true; ++isolate_calls; exempt = e; return true; }
    bool restore() override { on = false; ++restore_calls; return true; }
    bool active() override { return on; }
    NetworkLock lock = NetworkLock::off;
    int lock_calls = 0;
    bool lock_fails = false;
    std::vector<std::string> lock_exempt;
    bool set_network_lock(NetworkLock l, const std::vector<std::string>& e) override {
        ++lock_calls;
        if (lock_fails) return false;
        lock = l;
        lock_exempt = e;
        return true;
    }
    NetworkLock network_lock() override { return lock; }
};

class FakeExtensions : public IExtensionScanner {
public:
    std::vector<ExtensionInfo> list;
    std::vector<ExtensionInfo> scan() override { return list; }
};

class FakeSystem : public ISystemInfo {
public:
    std::vector<UserSession> sess{{"alice", "/home/alice", 1000, ":0", "x11", true}};
    std::string hostname() override { return "lab-pc-01"; }
    std::string os_name() override { return "TestOS"; }
    std::string os_version() override { return "1"; }
    std::vector<std::string> mac_addresses() override { return {"aa:bb:cc:dd:ee:ff"}; }
    std::vector<UserSession> sessions() override { return sess; }
    int64_t uptime_s() override { return 100; }
    std::string self_exe_path() override { return ""; }
    SelfUsage self_usage() override { return {20 * 1024 * 1024, 10}; }
    std::vector<std::string> blocked_extensions;
    void set_browser_extension_blocklist(const std::vector<std::string>& ids) override { blocked_extensions = ids; }
};

class FakeIpc : public IIpcServer {
public:
    std::vector<std::string> broadcasts;
    std::mutex mu;
    bool start(const std::string&, std::function<void(const Peer&, const std::string&)>,
               std::function<void(const Peer&, bool)>) override { return true; }
    bool send(uint64_t, const std::string& l) override { std::lock_guard lk(mu); broadcasts.push_back(l); return true; }
    void broadcast(const std::string& l) override { std::lock_guard lk(mu); broadcasts.push_back(l); }
    void stop() override {}
    size_t count_containing(const std::string& s) {
        std::lock_guard lk(mu);
        size_t n = 0;
        for (const auto& b : broadcasts) if (b.find(s) != std::string::npos) ++n;
        return n;
    }
};

inline json sample_signatures() {
    return json::parse(R"sig({
      "version": "test-1",
      "processes": [
        {"id":"proc.ollama","category":"local_llm","severity":"high","description":"Ollama","match":{"exe":["ollama","ollama_llama_server"]}},
        {"id":"proc.llamacpp","category":"local_llm","severity":"high","description":"llama.cpp","match":{"exe_prefix":["llama-"]}},
        {"id":"proc.cli.claude_code","category":"cli_agent","severity":"high","description":"Claude Code",
         "match":{"exe":["claude"],"interpreters":["node"],"cmdline_contains":["@anthropic-ai/claude-code"]}},
        {"id":"proc.editor.cursor","category":"ai_editor","severity":"high","description":"Cursor","match":{"exe":["cursor"]}},
        {"id":"vm.container","category":"virtualization","severity":"low","description":"Docker","match":{"exe":["dockerd"]}}
      ],
      "extensions": [
        {"id":"ext.copilot","severity":"high","ide":"vscode","ext_ids":["github.copilot","github.copilot-chat"],"description":"Copilot",
         "domains":["githubcopilot.com","copilot-proxy.githubusercontent.com","copilot-telemetry.githubusercontent.com","copilot.microsoft.com"]},
        {"id":"ext.codeium","severity":"high","ide":"vscode","ext_ids":["codeium.codeium"],"description":"Codeium","domains":["codeium.com","windsurf.com"]},
        {"id":"ext.jb.ai","severity":"high","ide":"jetbrains","ext_ids":["ml-llm"],"description":"JB AI","domains":["jetbrains.ai","grazie.ai"]}
      ],
      "ports": {"rules":[{"id":"port.ollama","severity":"high","port":11434,"description":"Ollama API"},
                         {"id":"port.vllm","severity":"medium","port":8000,"requires_probe":true,"description":"vLLM"}],
                "probe":{"enabled":false,"paths":["/v1/models"],"severity":"high","min_rss_mb":300}},
      "artifacts": {"rules":[{"id":"file.gguf","severity":"high","globs":["*.gguf"],"description":"GGUF"},
                             {"id":"file.safetensors","severity":"medium","globs":["*.safetensors"],"min_size_mb":500}],
                    "scan":{"roots":["~"],"max_depth":3,"max_files":1000,"skip_dirs":["node_modules"]}},
      "adapters": [
        {"id":"net.tether_usb","severity":"high","kind":"tethering","drivers":["rndis_host","cdc_ether","ipheth"]},
        {"id":"net.vpn_adapter","severity":"high","kind":"vpn","name_prefixes":["tun","wg"],"drivers":["wireguard"]},
        {"id":"net.second_route","severity":"high","kind":"route"}
      ],
      "devices": [
        {"id":"dev.usb_storage","severity":"low","classes":["usb_storage"]},
        {"id":"dev.phone","severity":"medium","vendors":["18d1","05ac"]}
      ],
      "web": {
        "browsers": ["chrome", "google-chrome", "chromium", "msedge", "brave", "firefox", "opera", "vivaldi", "arc"],
        "browser_title_suffixes": [" - google chrome", " - mozilla firefox", " - microsoft edge", " - brave"],
        "ai_sites": [
          {"site": "ChatGPT", "contains": ["chatgpt"]},
          {"site": "Claude AI", "equals": ["claude"], "suffix": [" - claude", " | claude"], "contains": ["claude.ai"]},
          {"site": "Google Gemini", "equals": ["gemini", "google gemini"], "suffix": [" - gemini"], "contains": ["gemini.google"]},
          {"site": "DeepSeek", "contains": ["deepseek"]},
          {"site": "Perplexity AI", "equals": ["perplexity"], "suffix": [" - perplexity"], "contains": ["perplexity.ai"]},
          {"site": "Grok (xAI)", "equals": ["grok"], "suffix": [" - grok"], "contains": ["grok.com"]},
          {"site": "Poe AI", "equals": ["poe"], "prefix": ["poe - "], "contains": ["poe.com"]},
          {"site": "NotebookLM", "contains": ["notebooklm"]}
        ],
        "telemetry_domains": ["telemetry", "exp.individual.", "exp-tas.com"],
        "process_domains": {"copilot-runtime": "api.githubcopilot.com", "copilot-language-server": "api.githubcopilot.com",
                            "copilot": "api.githubcopilot.com", "claude": "api.anthropic.com", "ollama": "ollama.com"},
        "tunnel": {"id": "net.tunnel_flow", "severity": "medium", "description": "Tunnel to an outside host",
                   "tcp_ports": [22, 1080, 9001]}
      },
      "domains": {
        "ai": ["chatgpt.com", "claude.ai", "gemini.google.com", "perplexity.ai", "deepseek.com"],
        "ide": ["api.individual.githubcopilot.com", "githubcopilot.com", "copilot-proxy.githubusercontent.com", "api2.cursor.sh"]
      }
    })sig");
}

}  // namespace xg::test
