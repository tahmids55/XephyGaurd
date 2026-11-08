#include <catch2/catch_test_macros.hpp>
#include <filesystem>

#include "fakes.hpp"
#include "xg/agent.hpp"
#include "xg/util.hpp"

using namespace xg;
namespace fs = std::filesystem;

namespace {
struct Harness {
    fs::path dir = fs::temp_directory_path() / ("xga-" + random_hex(4));
    ServicePlatform plat;
    test::FakeProcesses* procs;
    test::FakeNetwork* net;
    test::FakeExtensions* exts;
    test::FakeIpc* ipc;
    test::FakeIsolator* iso;
    VectorSink sink;
    std::unique_ptr<Agent> agent;

    Harness() {
        fs::create_directories(dir);
        auto iso_p = std::make_unique<test::FakeIsolator>();
        iso = iso_p.get();
        plat.isolator = std::move(iso_p);
        auto p = std::make_unique<test::FakeProcesses>();
        procs = p.get();
        plat.processes = std::move(p);
        auto n = std::make_unique<test::FakeNetwork>();
        net = n.get();
        plat.network = std::move(n);
        auto e = std::make_unique<test::FakeExtensions>();
        exts = e.get();
        plat.extensions = std::move(e);
        auto i = std::make_unique<test::FakeIpc>();
        ipc = i.get();
        plat.ipc = std::move(i);
        plat.system = std::make_unique<test::FakeSystem>();
        Config cfg = Config::defaults();
        cfg.data_dir = dir.string();
        agent = std::make_unique<Agent>(cfg, (dir / "agent.toml").string(), plat);
        agent->set_sink_for_tests(&sink);
        json env{{"version", "test-1"}, {"bundle", test::sample_signatures().dump()}};
        REQUIRE(agent->apply_signatures(env, false));
    }
    ~Harness() {
        agent.reset();
        fs::remove_all(dir);
    }
    std::vector<Event> of(const std::string& type) {
        std::vector<Event> out;
        for (const auto& e : sink.events) if (e.type == type) out.push_back(e);
        return out;
    }
};
}  // namespace

TEST_CASE("agent reports and warns on an AI process in Test mode") {
    Harness h;
    h.agent->apply_policy(Policy::default_for(Mode::test));
    h.procs->procs = {{1, 0, "/sbin/init", "init", "", "root", 0, 1}};
    h.agent->scan_processes();
    CHECK(h.of(ev::rule_hit).empty());
    h.procs->procs.push_back({42, 1, "/usr/local/bin/ollama", "ollama", "ollama serve", "alice", 0, 2});
    h.agent->scan_processes();
    auto hits = h.of(ev::rule_hit);
    REQUIRE(hits.size() == 1);
    CHECK(hits[0].data["rule_id"] == "proc.ollama");
    CHECK(hits[0].data["actions"] == json({"log", "warn", "alert", "kill"}));
    CHECK(h.ipc->count_containing("\"notify\"") == 1);
    CHECK(h.procs->killed == std::vector<int64_t>{42});  // Test mode closes AI apps automatically
    REQUIRE(h.of(ev::process_start).size() == 1);
    CHECK(h.of(ev::process_start)[0].data.dump().find("ollama serve") == std::string::npos);  // no cmdline upload

    // Process exits -> stop event
    h.procs->procs.pop_back();
    h.agent->scan_processes();
    CHECK(h.of(ev::process_stop).size() == 1);

    // Restart within the cooldown: no duplicate alert
    h.procs->procs.push_back({43, 1, "/usr/local/bin/ollama", "ollama", "", "alice", 0, 3});
    h.agent->scan_processes();
    CHECK(h.of(ev::rule_hit).size() == 1);
}

TEST_CASE("agent kills when the policy opts in, but never exempt processes") {
    Harness h;
    auto p = Policy::default_for(Mode::test);
    p.rule_actions["proc.ollama"] = {Action::log, Action::kill, Action::alert};
    h.agent->apply_policy(p);
    h.procs->procs = {{1, 0, "/sbin/init", "init", "", "root", 0, 1}};
    h.agent->scan_processes();
    h.procs->procs.push_back({42, 1, "/usr/local/bin/ollama", "ollama", "", "alice", 0, 2});
    h.agent->scan_processes();
    REQUIRE(h.procs->killed == std::vector<int64_t>{42});
    // Repeated launch within the cooldown is still killed, just not re-reported.
    h.procs->procs.back() = {44, 1, "/usr/local/bin/ollama", "ollama", "", "alice", 0, 4};
    h.agent->scan_processes();
    CHECK(h.procs->killed.size() == 2);
    CHECK(h.of(ev::rule_hit).size() == 1);
}

TEST_CASE("always-on mode only logs") {
    Harness h;
    h.agent->apply_policy(Policy::default_for(Mode::always_on));
    h.procs->procs = {};
    h.agent->scan_processes();
    h.procs->procs = {{7, 1, "/usr/bin/llama-server", "llama-server", "", "bob", 0, 1}};
    h.agent->scan_processes();
    auto hits = h.of(ev::rule_hit);
    REQUIRE(hits.size() == 1);
    CHECK(hits[0].data["actions"] == json({"log", "alert"}));  // detect + alert, never close
    CHECK(h.procs->killed.empty());
    CHECK(h.ipc->count_containing("\"notify\"") == 0);
}

TEST_CASE("agent detects tethering, VPN and a second default route") {
    Harness h;
    h.agent->apply_policy(Policy::default_for(Mode::test));
    h.net->list = {{"enp3s0", "", "e1000e", "aa", {"192.168.10.50"}, true, true, false, false}};
    h.agent->scan_network();
    CHECK(h.of(ev::rule_hit).empty());
    h.net->list.push_back({"usb0", "", "rndis_host", "bb", {"192.168.42.10"}, true, true, false, false});
    h.agent->scan_network();
    auto hits = h.of(ev::rule_hit);
    std::set<std::string> ids;
    for (auto& e : hits) ids.insert(e.data["rule_id"]);
    CHECK(ids.contains("net.tether_usb"));
    CHECK(ids.contains("net.second_route"));
    CHECK(h.of(ev::net_adapter).back().data["change"] == "added");
}

TEST_CASE("agent inventories installed AI extensions and only alerts when actively used") {
    Harness h;
    h.agent->apply_policy(Policy::default_for(Mode::test));
    h.exts->list = {{"vscode", "ms-python.python", "1", "alice", ""}};
    h.agent->scan_extensions();
    CHECK(h.of(ev::extension_scan).size() == 1);
    CHECK(h.of(ev::rule_hit).empty());
    // Installed on disk in both VS Code and JetBrains, but idle -> listed in extension_scan.ai, NO rule_hit alert!
    h.exts->list.push_back({"vscode", "github.copilot", "1.2", "alice", "", false, ""});
    h.exts->list.push_back({"jetbrains", "ml-llm", "1.5", "alice", "", false, ""});
    h.procs->procs = {{100, 1, "C:\\Users\\alice\\AppData\\Local\\Programs\\Microsoft VS Code\\Code.exe", "Code.exe", "", "alice", 0, 1},
                      {8356, 100, "C:\\Users\\alice\\.vscode\\extensions\\github.copilot-chat\\copilot-runtime.exe", "copilot-runtime.exe", "", "alice", 0, 2}};
    h.agent->scan_extensions();
    CHECK(h.of(ev::extension_scan).size() == 2);
    CHECK(h.of(ev::extension_scan).back().data["ai"].size() == 2);
    CHECK(h.of(ev::extension_scan).back().data["ai"][0]["active"] == false);
    CHECK(h.of(ev::extension_scan).back().data["ai"][1]["active"] == false);
    CHECK(h.of(ev::rule_hit).empty());
    // Now the student actively uses Copilot in VS Code -> ONLY vscode is marked active, NOT jetbrains!
    h.net->flows = {{8356, "copilot-runtime", "140.82.114.22", 443, "api.individual.githubcopilot.com", 4096, 16384}};
    h.agent->scan_extensions();
    CHECK(h.of(ev::extension_scan).size() == 3);
    CHECK(h.of(ev::extension_scan).back().data["ai"][0]["active"] == true);
    CHECK(h.of(ev::extension_scan).back().data["ai"][1]["active"] == false);
    REQUIRE(h.of(ev::rule_hit).size() == 1);
    CHECK(h.of(ev::rule_hit)[0].data["rule_id"] == "ext.copilot");
    CHECK(h.of(ev::rule_hit)[0].data["context"]["ide"] == "vscode");
    CHECK(h.of(ev::rule_hit)[0].data["context"]["domain"] == "api.individual.githubcopilot.com");

    // Browser AI tab & outbound browser stream detection
    IIpcServer::Peer peer{1, 1000, 555, "alice"};
    h.agent->on_helper_message(peer, R"({"t":"window","app":"chrome.exe","title":"ChatGPT - Google Chrome"})");
    REQUIRE(h.of(ev::window_focus).size() == 1);
    CHECK(h.of(ev::window_focus)[0].data["ai_site"] == "ChatGPT");
    CHECK(h.of(ev::rule_hit).size() == 2);
    CHECK(h.of(ev::rule_hit)[1].data["rule_id"] == "web.ai_tab");
}

TEST_CASE("ports and helper messages respect policy and privacy") {
    Harness h;
    auto p = Policy::default_for(Mode::practice);
    h.agent->apply_policy(p);
    h.net->socks = {{11434, "127.0.0.1", true, 99, "ollama", 0}, {22, "0.0.0.0", true, 1, "sshd", 0}};
    h.agent->scan_ports();
    REQUIRE(h.of(ev::listening_port).size() == 1);
    CHECK(h.of(ev::listening_port)[0].data["port"] == 11434);

    IIpcServer::Peer peer{1, 1000, 555, "alice"};
    h.agent->on_helper_message(peer, R"({"t":"window","app":"/usr/bin/firefox","title":"Bank account - Firefox"})");
    REQUIRE(h.of(ev::window_focus).size() == 1);
    CHECK(h.of(ev::window_focus)[0].data["title"] == "firefox");  // app_only in Practice
    h.agent->on_helper_message(peer, R"({"t":"paste","copy":false,"chars":900,"lines":30,"code":true,"score":0.8,"src":"firefox","dst":"code"})");
    REQUIRE(h.of(ev::paste).size() == 1);
    CHECK(h.of(ev::paste)[0].data["chars"] == 900);
    h.agent->on_helper_message(peer, "garbage{");

    h.agent->apply_policy(Policy::default_for(Mode::always_on));
    h.agent->on_helper_message(peer, R"({"t":"window","app":"code","title":"x"})");
    CHECK(h.of(ev::window_focus).size() == 1);  // not collected in Always-on
}

TEST_CASE("commands are executed and acknowledged") {
    Harness h;
    h.agent->apply_policy(Policy::default_for(Mode::practice));
    h.agent->handle_command({"c1", "message", {{"text", "Ten minutes left"}}});
    CHECK(h.ipc->count_containing("Ten minutes left") == 1);
    h.procs->procs = {{77, 1, "/usr/bin/cursor", "cursor", "", "alice", 256ull * 1024 * 1024, 1},
                      {88, 1, "/usr/bin/firefox", "firefox", "", "alice", 512ull * 1024 * 1024, 2}};
    h.agent->handle_command({"c_scan", "rescan", {{"what", "processes"}}});
    auto snaps = h.of(ev::process_snapshot);
    REQUIRE(snaps.size() == 1);
    REQUIRE(snaps[0].data["list"].size() == 2);
    // Flagged AI process (cursor) is sorted first ahead of higher-RAM non-flagged process (firefox)
    CHECK(snaps[0].data["list"][0]["exe"] == "cursor");
    CHECK(snaps[0].data["list"][0]["rule_id"] == "proc.editor.cursor");
    CHECK(snaps[0].data["list"][1]["exe"] == "firefox");
    CHECK(snaps[0].data["list"][1]["rss_mb"] == 512);

    h.agent->handle_command({"c2", "kill", {{"exe", "cursor"}}});
    CHECK(h.procs->killed == std::vector<int64_t>{77});
    auto res = h.of(ev::command_result);
    REQUIRE(res.size() == 3);
    CHECK(res[2].data["command_id"] == "c2");
    auto hb = h.agent->build_heartbeat();
    CHECK(hb["mode"] == "practice");
    CHECK(hb["signatures_version"] == "test-1");
}

TEST_CASE("tools already running are re-evaluated when a Test session starts") {
    Harness h;
    h.agent->apply_policy(Policy::default_for(Mode::always_on));
    h.procs->procs = {{1, 0, "/sbin/init", "init", "", "root", 0, 1}, {42, 1, "/usr/bin/ollama", "ollama", "", "alice", 0, 2}};
    h.agent->scan_processes();
    REQUIRE(h.of(ev::rule_hit).size() == 1);
    CHECK(h.of(ev::rule_hit)[0].data["actions"] == json({"log", "alert"}));
    h.agent->apply_policy(Policy::default_for(Mode::test));
    h.agent->reevaluate_all();
    auto hits = h.of(ev::rule_hit);
    REQUIRE(hits.size() == 2);
    CHECK(hits[1].data["actions"] == json({"log", "warn", "alert", "kill"}));
    CHECK(hits[1].data["mode"] == "test");
}

TEST_CASE("common dev ports need a positive LLM-API probe") {
    Harness h;
    h.agent->apply_policy(Policy::default_for(Mode::test));
    // Nothing listens for real on 8000 here, so the probe fails and no hit is reported.
    h.net->socks = {{8000, "127.0.0.1", true, 77, "php", 0}};
    h.agent->scan_ports();
    CHECK(h.of(ev::listening_port).empty());
    CHECK(h.agent->rules().match_port(8000) == std::nullopt);
    CHECK(h.agent->rules().match_port(8000, true)->rule_id == "port.vllm");
}

TEST_CASE("isolate cuts the Internet (LAN + CCC kept) and closes AI apps even outside tests") {
    Harness h;
    h.agent->apply_policy(Policy::default_for(Mode::practice));  // practice: detect + alert only
    h.procs->procs = {{1, 0, "/sbin/init", "init", "", "root", 0, 1}};
    h.agent->scan_processes();
    h.procs->procs.push_back({42, 1, "/usr/local/bin/ollama", "ollama", "", "alice", 0, 2});
    h.agent->scan_processes();
    CHECK(h.procs->killed.empty());

    h.agent->handle_command({"i1", "isolate_notice", {{"isolated", true}}});
    CHECK(h.agent->isolated());
    CHECK(h.iso->on);
    CHECK(h.iso->isolate_calls == 1);
    REQUIRE(h.iso->exempt.size() == 1);  // the CCC stays reachable
    h.agent->reevaluate_all();           // already-running AI app is closed right away
    CHECK(h.procs->killed == std::vector<int64_t>{42});
    h.procs->procs.push_back({43, 1, "/usr/local/bin/ollama", "ollama", "", "alice", 0, 3});
    h.agent->scan_processes();            // and a new launch is closed too
    CHECK(h.procs->killed.size() == 2);
    CHECK(h.ipc->count_containing("local network still works") == 1);

    h.agent->handle_command({"i2", "isolate_notice", {{"isolated", false}}});
    CHECK_FALSE(h.agent->isolated());
    CHECK_FALSE(h.iso->on);
    CHECK(h.iso->restore_calls == 1);
    h.procs->procs.push_back({44, 1, "/usr/local/bin/ollama", "ollama", "", "alice", 0, 4});
    h.agent->scan_processes();
    CHECK(h.procs->killed.size() == 2);  // practice mode again: alert only
}
