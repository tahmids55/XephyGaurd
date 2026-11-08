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
    bool kill(int64_t pid) override { killed.push_back(pid); return true; }
    void stop() override {}
};

class FakeNetwork : public INetworkMonitor {
public:
    std::vector<AdapterInfo> list;
    std::vector<ListeningSocket> socks;
    std::vector<AdapterInfo> adapters() override { return list; }
    std::vector<ListeningSocket> listening_sockets() override { return socks; }
    bool watch(std::function<void()>) override { return false; }
    void stop() override {}
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
    return json::parse(R"({
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
        {"id":"ext.copilot","severity":"high","ide":"vscode","ext_ids":["github.copilot","github.copilot-chat"],"description":"Copilot"},
        {"id":"ext.jb.ai","severity":"high","ide":"jetbrains","ext_ids":["ml-llm"],"description":"JB AI"}
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
      ]
    })");
}

}  // namespace xg::test
