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

    explicit Harness(const std::string& ccc_host = {}) {
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
        cfg.ccc_host = ccc_host;
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

TEST_CASE("agent inventories installed AI extensions and alerts on test mode or active use") {
    Harness h;
    // Practice mode: installed idle AI extensions are inventoried, but do not raise alerts until used.
    h.agent->apply_policy(Policy::default_for(Mode::practice));
    h.exts->list = {{"vscode", "ms-python.python", "1", "alice", ""}};
    h.agent->scan_extensions();
    CHECK(h.of(ev::extension_scan).size() == 1);
    CHECK(h.of(ev::rule_hit).empty());
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

    // In Test/Exam mode, an installed Antigravity AI extension raises a low alert even when idle.
    h.net->flows.clear();
    h.exts->list.push_back({"antigravity", "codeium.codeium", "1.8.0", "alice", "", false, ""});
    h.agent->apply_policy(Policy::default_for(Mode::test));
    h.agent->scan_extensions();
    const auto all_hits = h.of(ev::rule_hit);
    bool saw_antigravity_low = false;
    for (const auto& rh : all_hits) {
        if (rh.data["rule_id"] == "ext.codeium" && rh.data["context"]["ide"] == "antigravity" &&
            rh.data["severity"] == "low") {
            saw_antigravity_low = true;
        }
    }
    CHECK(saw_antigravity_low);

    // Browser AI tab detection
    const size_t before_tab = h.of(ev::rule_hit).size();
    IIpcServer::Peer peer{1, 1000, 555, "alice"};
    h.agent->on_helper_message(peer, R"({"t":"window","app":"chrome.exe","title":"ChatGPT - Google Chrome"})");
    REQUIRE(h.of(ev::window_focus).size() == 1);
    CHECK(h.of(ev::window_focus)[0].data["ai_site"] == "ChatGPT");
    REQUIRE(h.of(ev::rule_hit).size() == before_tab + 1);
    CHECK(h.of(ev::rule_hit).back().data["rule_id"] == "web.ai_tab");
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

TEST_CASE("network lock follows the session in proxy_only mode only") {
    Harness h("ccc.lab");
    auto pol = [](Mode m, const char* proxy_mode, const char* lock) {
        auto p = Policy::default_for(m);
        p.proxy_mode = proxy_mode;
        p.network_lock = parse_network_lock(lock);
        return p;
    };
    h.agent->apply_policy(pol(Mode::test, "proxy_only", "observe"));
    CHECK(h.iso->lock == NetworkLock::observe);
    CHECK(h.iso->lock_exempt == std::vector<std::string>{"ccc.lab"});
    CHECK(h.agent->build_heartbeat()["network_lock"] == "observe");
    CHECK_FALSE(h.iso->on);  // the lock is not isolation

    h.agent->apply_policy(pol(Mode::test, "proxy_only", "strict"));
    CHECK(h.iso->lock == NetworkLock::strict);
    const int calls = h.iso->lock_calls;
    h.agent->apply_policy(pol(Mode::test, "proxy_only", "strict"));
    CHECK(h.iso->lock_calls == calls);  // unchanged policy: no rule churn

    // Gateway mode (the gateway enforces) and Always-on never lock the PC.
    h.agent->apply_policy(pol(Mode::test, "gateway", "strict"));
    CHECK(h.iso->lock == NetworkLock::off);
    h.agent->apply_policy(pol(Mode::always_on, "proxy_only", "strict"));
    CHECK(h.iso->lock == NetworkLock::off);

    // A failed install is reported truthfully in the heartbeat.
    h.iso->lock_fails = true;
    h.agent->apply_policy(pol(Mode::practice, "proxy_only", "observe"));
    CHECK(h.agent->network_lock() == NetworkLock::off);
    CHECK(h.agent->build_heartbeat()["network_lock"] == "off");
}

TEST_CASE("a tunnel to an outside host is flagged for review during a session, never closed") {
    Harness h;
    h.net->tunnels = {{77, "ssh", "198.51.100.7", 22}, {78, "ssh", "192.168.1.10", 22}};  // 2nd: LAN, ignored
    h.agent->apply_policy(Policy::default_for(Mode::always_on));
    h.agent->scan_tunnels();
    CHECK(h.of(ev::rule_hit).empty());  // outside a session nothing is reported

    h.agent->apply_policy(Policy::default_for(Mode::test));
    h.agent->scan_tunnels();
    auto hits = h.of(ev::rule_hit);
    REQUIRE(hits.size() == 1);
    CHECK(hits[0].data["rule_id"] == "net.tunnel_flow");
    CHECK(hits[0].data["severity"] == "medium");
    CHECK(hits[0].data["context"]["remote_ip"] == "198.51.100.7");
    CHECK(hits[0].data["context"]["remote_port"] == 22);
    CHECK(hits[0].data["context"]["process"] == "ssh");
    CHECK(h.procs->killed.empty());
    h.agent->scan_tunnels();
    CHECK(h.of(ev::rule_hit).size() == 1);  // de-duplicated
}

TEST_CASE("screen_lease command: a positive ttl is accepted, a missing or zero ttl is ignored") {
    Harness h;
    h.agent->handle_command({"l1", "screen_lease", {{"ttl_s", 30}}});
    h.agent->handle_command({"l2", "screen_lease", {{"ttl_s", 0}}});
    h.agent->handle_command({"l3", "screen_lease", json::object()});
    const auto r = h.of(ev::command_result);
    REQUIRE(r.size() == 3);
    CHECK(r[0].data.at("status") == "ok");
    CHECK(r[1].data.at("status") == "ignored");
    CHECK(r[2].data.at("status") == "ignored");
}

TEST_CASE("policy screen.privacy_rules parse, round-trip, and a malformed list rejects the policy") {
    const json ok = {{"screen", {{"privacy_rules", json::array({{{"process", "keepassxc"}}, {{"title_regex", "bank|private"}}})}}}};
    std::string err;
    auto p = Policy::from_json(ok, &err);
    REQUIRE(err.empty());
    REQUIRE(p.screen_mask_rules.size() == 2);
    CHECK(p.screen_mask_rules[0].process == "keepassxc");
    CHECK(p.screen_mask_rules[1].title_regex == "bank|private");
    CHECK(Policy::from_json(p.to_json()).screen_mask_rules.size() == 2);
    CHECK(Policy::from_json(json::object()).screen_mask_rules.empty());

    for (const json& bad : {json{{"screen", {{"privacy_rules", "x"}}}}, json{{"screen", {{"privacy_rules", json::array({json::object()})}}}},
                            json{{"screen", {{"privacy_rules", json::array({{{"process", 5}}})}}}},
                            json{{"screen", {{"privacy_rules", json::array({{{"title_regex", std::string(300, 'a')}}})}}}}}) {
        std::string e;
        auto q = Policy::from_json(bad, &e);
        CHECK_FALSE(e.empty());
        CHECK(q.screen_mask_rules.empty());
    }
}

TEST_CASE("policy screen encoding: defaults to JPEG, takes valid values, ignores invalid ones") {
    auto d = Policy::from_json(json::object());
    CHECK(d.screen_codec == "jpeg");
    CHECK(d.screen_quality == 85);
    CHECK(d.screen_webp_method == 1);
    std::string err;
    auto p = Policy::from_json(json{{"screen", {{"codec", "webp"}, {"quality", 60}, {"webp_method", 0}}}}, &err);
    CHECK(err.empty());
    CHECK(p.screen_codec == "webp");
    CHECK(p.screen_quality == 60);
    CHECK(p.screen_webp_method == 0);
    auto rt = Policy::from_json(p.to_json());
    CHECK(rt.screen_codec == "webp");
    CHECK(rt.screen_quality == 60);
    auto bad = Policy::from_json(json{{"screen", {{"codec", "h264"}, {"quality", 1000}, {"webp_method", "x"}}}}, &err);
    CHECK(bad.screen_codec == "jpeg");
    CHECK(bad.screen_quality == 85);
    CHECK(bad.screen_webp_method == 1);
    CHECK(d.screen_cpu_budget_pct == 8.0);
    CHECK(Policy::from_json(json{{"screen", {{"cpu_budget_pct", 15}}}}).screen_cpu_budget_pct == 15.0);
    CHECK(Policy::from_json(json{{"screen", {{"cpu_budget_pct", 0.5}}}}).screen_cpu_budget_pct == 8.0);
    CHECK(Policy::from_json(json{{"screen", {{"cpu_budget_pct", "9"}}}}).screen_cpu_budget_pct == 8.0);
    CHECK(Policy::from_json(Policy::from_json(json{{"screen", {{"cpu_budget_pct", 12.5}}}}).to_json()).screen_cpu_budget_pct == 12.5);
}

TEST_CASE("policy network_lock parses known values and falls back to off") {
    CHECK(Policy::from_json(json{{"network_lock", "strict"}}).network_lock == NetworkLock::strict);
    CHECK(Policy::from_json(json{{"network_lock", "OBSERVE"}}).network_lock == NetworkLock::observe);
    CHECK(Policy::from_json(json{{"network_lock", "bogus"}}).network_lock == NetworkLock::off);
    CHECK(Policy::from_json(json{{"network_lock", 3}}).network_lock == NetworkLock::off);
    CHECK(Policy::from_json(json::object()).network_lock == NetworkLock::off);
    auto p = Policy::default_for(Mode::test);
    p.network_lock = NetworkLock::observe;
    CHECK(Policy::from_json(p.to_json()).network_lock == NetworkLock::observe);
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

TEST_CASE("agent refuses a bundle without domains and keeps the last-known-good one") {
    Harness h;
    json bad = test::sample_signatures();
    bad["version"] = "test-bad";
    bad["domains"] = {{"ai", json::array()}, {"ide", json::array()}};
    json env{{"version", "test-bad"}, {"bundle", bad.dump()}};
    CHECK_FALSE(h.agent->apply_signatures(env, false));
    CHECK(h.agent->build_heartbeat()["signatures_version"] == "test-1");  // last-known-good kept

    // A refused version is not refetched in a tight loop: a good bundle still applies afterwards.
    json good = test::sample_signatures();
    good["version"] = "test-2";
    CHECK(h.agent->apply_signatures(json{{"version", "test-2"}, {"bundle", good.dump()}}, false));
    CHECK(h.agent->build_heartbeat()["signatures_version"] == "test-2");
}

TEST_CASE("agent pushes ext_ids to the helper after applying a bundle") {
    Harness h;
    CHECK(h.ipc->count_containing("\"ext_ids\"") >= 1);
    CHECK(h.ipc->count_containing("github.copilot-chat") >= 1);
}

TEST_CASE("agent emits fallback hit for unclaimed IDE extension-host AI flows and matches copilot-proxy") {
    {
        Harness h;
        h.agent->apply_policy(Policy::default_for(Mode::practice));
        // Built-in or unlisted Copilot in VS Code (no github.copilot extension on disk!) must still raise a hit.
        h.exts->list = {{"vscode", "ms-vscode.cpptools", "1.0", "alice", ""}};
        h.procs->procs = {{100, 1, "C:\\Users\\alice\\AppData\\Local\\Programs\\Microsoft VS Code\\Code.exe",
                           "Code.exe", "", "alice", 0, 1}};
        h.net->flows = {{100, "code", "140.82.114.22", 443, "api.individual.githubcopilot.com", 2048, 8192}};
        h.agent->scan_extensions();
        auto hits = h.of(ev::rule_hit);
        REQUIRE(hits.size() == 1);
        CHECK(hits[0].data["rule_id"] == "net.ide_ai");
        CHECK(hits[0].data["category"] == "ai_extension");
        CHECK(hits[0].data["context"]["process"] == "code");
        CHECK(hits[0].data["context"]["domain"] == "api.individual.githubcopilot.com");
    }

    {
        Harness h;
        h.agent->apply_policy(Policy::default_for(Mode::practice));
        // When github.copilot IS installed and contacts copilot-proxy.githubusercontent.com, ext.copilot claims it
        // instead of duplicating as a generic fallback hit.
        h.procs->procs = {{100, 1, "C:\\Users\\alice\\AppData\\Local\\Programs\\Microsoft VS Code\\Code.exe",
                           "Code.exe", "", "alice", 0, 1}};
        h.exts->list = {{"vscode", "github.copilot", "1.2", "alice", "", false, ""}};
        h.net->flows = {{100, "code", "140.82.114.25", 443, "copilot-proxy.githubusercontent.com", 1024, 4096}};
        h.agent->scan_extensions();
        auto hits = h.of(ev::rule_hit);
        REQUIRE(hits.size() == 1);
        CHECK(hits[0].data["rule_id"] == "ext.copilot");
        CHECK(hits[0].data["context"]["domain"] == "copilot-proxy.githubusercontent.com");
    }
}

TEST_CASE("agent pushes proxy_pac_url to helper in proxy_only mode") {
    Harness h("192.168.0.100");
    auto p = Policy::default_for(Mode::practice);
    p.proxy_mode = "proxy_only";
    // F97: only an HTTPS PAC on the CCC host is passed on; plain HTTP or another host is dropped.
    p.proxy_pac_url = "http://192.168.0.100/proxy.pac";
    h.agent->apply_policy(p);
    CHECK(h.ipc->count_containing("http://192.168.0.100/proxy.pac") == 0);
    p.proxy_pac_url = "https://evil.example/proxy.pac";
    h.agent->apply_policy(p);
    CHECK(h.ipc->count_containing("evil.example") == 0);
    p.proxy_pac_url = "https://192.168.0.100/proxy.pac";
    h.agent->apply_policy(p);
    CHECK(h.ipc->count_containing("https://192.168.0.100/proxy.pac") >= 1);

    p.proxy_mode = "gateway";
    h.agent->apply_policy(p);
    CHECK(h.ipc->count_containing("\"proxy_pac_url\":\"\"") >= 1);
}

TEST_CASE("agent heartbeat reports honest capabilities.gpu from IGpuMonitor::available") {
    struct StubGpu final : public IGpuMonitor {
        bool avail = false;
        bool available() const override { return avail; }
        std::optional<Sample> sample() override { return std::nullopt; }
    };

    Harness h;
    CHECK(h.agent->build_heartbeat()["capabilities"]["gpu"] == false);

    const std::filesystem::path dir2 = std::filesystem::temp_directory_path() / ("xga-gpu-" + random_hex(4));
    std::filesystem::create_directories(dir2);
    ServicePlatform plat2;
    plat2.processes = std::make_unique<test::FakeProcesses>();
    plat2.network = std::make_unique<test::FakeNetwork>();
    plat2.isolator = std::make_unique<test::FakeIsolator>();
    plat2.extensions = std::make_unique<test::FakeExtensions>();
    plat2.ipc = std::make_unique<test::FakeIpc>();
    plat2.system = std::make_unique<test::FakeSystem>();
    auto gpu = std::make_unique<StubGpu>();
    StubGpu* gpu_ptr = gpu.get();
    plat2.gpu = std::move(gpu);

    Config cfg = Config::defaults();
    cfg.data_dir = dir2.string();
    Agent a(cfg, (dir2 / "agent.toml").string(), plat2);
    CHECK(a.build_heartbeat()["capabilities"]["gpu"] == false);

    gpu_ptr->avail = true;
    CHECK(a.build_heartbeat()["capabilities"]["gpu"] == true);
    std::filesystem::remove_all(dir2);
}

TEST_CASE("web.ai_tab hits carry the filtered title, never the raw one (I-AGENT-8)") {
    Harness h;
    IIpcServer::Peer peer{7, 1000, 555, "alice"};
    for (auto tc : {TitleCapture::off, TitleCapture::app_only}) {
        auto p = Policy::default_for(Mode::practice);
        p.collect.window = true;
        p.title_capture = tc;
        h.agent->apply_policy(p);
        h.sink.events.clear();
        h.agent->on_helper_message(peer, R"({"t":"window","app":"chrome","title":"ChatGPT - secret project plan"})");
        auto hits = h.of(ev::rule_hit);
        REQUIRE(hits.size() == 1);
        CHECK(hits[0].data["context"]["ai_site"] == "ChatGPT");
        CHECK(hits[0].data.dump().find("secret project plan") == std::string::npos);
        h.agent->apply_signatures({{"version", "test-2"}, {"bundle", test::sample_signatures().dump()}}, false);  // reset dedup
    }
}

TEST_CASE("helper key stats keep only allow-listed numeric fields, clamped (I-AGENT-10)") {
    Harness h;
    auto p = Policy::default_for(Mode::practice);
    p.collect.keys = true;
    h.agent->apply_policy(p);
    IIpcServer::Peer peer{7, 1000, 555, "alice"};
    h.agent->on_helper_message(peer, json({{"t", "keys"}, {"keys", 40}, {"backspaces", -5}, {"iki_mean", 110.5},
                                           {"backspace_ratio", 7}, {"typed", "my password"}, {"bursts", "x"},
                                           {"window_s", 5}}).dump());
    auto ks = h.of(ev::key_stats);
    REQUIRE(ks.size() == 1);
    const auto& d = ks[0].data;
    CHECK(d["keys"] == 40);
    CHECK(d["backspaces"] == 0);
    CHECK(d["backspace_ratio"] == 1.0);
    CHECK(d["iki_mean"] == 110.5);
    CHECK_FALSE(d.contains("typed"));
    CHECK_FALSE(d.contains("bursts"));
    CHECK(d["user"] == "alice");
    CHECK(d.dump().find("password") == std::string::npos);
}

TEST_CASE("copy/paste targets keep file names only with title_capture=full (I-AGENT-9)") {
    Harness h;
    IIpcServer::Peer peer{7, 1000, 555, "alice"};
    const std::string msg = R"j({"t":"paste","chars":10,"src":"/usr/bin/code (secret_plan.cpp)","dst":"chrome"})j";
    for (auto [tc, want] : {std::pair{TitleCapture::app_only, std::string("code")},
                            std::pair{TitleCapture::full, std::string("code (secret_plan.cpp)")}}) {
        auto p = Policy::default_for(Mode::practice);
        p.collect.clipboard = true;
        p.title_capture = tc;
        h.agent->apply_policy(p);
        h.sink.events.clear();
        h.agent->on_helper_message(peer, msg);
        auto ev = h.of(ev::paste);
        REQUIRE(ev.size() == 1);
        CHECK(ev[0].data["src"] == want);
    }
}

TEST_CASE("a flooding helper is rate limited per connection (I-AGENT-16)") {
    Harness h;
    auto p = Policy::default_for(Mode::practice);
    p.collect.window = true;
    h.agent->apply_policy(p);
    IIpcServer::Peer flood{7, 1000, 555, "alice"}, other{8, 1001, 556, "bob"};
    for (int i = 0; i < 2000; ++i)
        h.agent->on_helper_message(flood, R"({"t":"window","app":"code","title":"x"})");
    const size_t got = h.of(ev::window_focus).size();
    CHECK(got >= 200);
    CHECK(got < 260);  // burst 200 + refill while looping
    h.agent->on_helper_message(other, R"({"t":"window","app":"code","title":"x"})");
    CHECK(h.of(ev::window_focus).size() == got + 1);  // other peers are unaffected
}

TEST_CASE("kill by pid only hits the process the CCC saw, not a reused pid (F35)") {
    Harness h;
    h.agent->apply_policy(Policy::default_for(Mode::practice));
    h.procs->procs = {{77, 1, "/usr/bin/cursor", "cursor", "", "alice", 1, 5000}};
    h.agent->handle_command({"c_snap", "rescan", {{"what", "processes"}}});
    REQUIRE(h.of(ev::process_snapshot).back().data["list"][0]["start"] == 5000);
    // pid 77 now belongs to a new process (start 6000): a kill carrying the old start time must not hit it.
    h.procs->procs = {{77, 1, "/usr/bin/bash", "bash", "", "alice", 1, 6000}};
    h.agent->handle_command({"k1", "kill", {{"pid", 77}, {"start_time", 5000}}});
    CHECK(h.procs->killed.empty());
    CHECK(h.of(ev::command_result).back().data["detail"]["processes"][0]["killed"] == false);
    h.agent->handle_command({"k2", "kill", {{"pid", 77}, {"start_time", 6000}}});
    CHECK(h.procs->killed == std::vector<int64_t>{77});
}

TEST_CASE("title-only AI tab hits use the bundle severity and are marked as a title signal (F76)") {
    Harness h;
    auto p = Policy::default_for(Mode::test);
    p.collect.window = true;
    h.agent->apply_policy(p);
    IIpcServer::Peer peer{7, 1000, 555, "alice"};
    h.agent->on_helper_message(peer, R"({"t":"window","app":"chrome","title":"ChatGPT - Google Chrome"})");
    auto hits = h.of(ev::rule_hit);
    REQUIRE(hits.size() == 1);
    CHECK(hits[0].data["severity"] == "medium");  // default when the bundle does not say otherwise
    CHECK(hits[0].data["context"]["signal"] == "window_title");
}

TEST_CASE("AI browser extensions are blocked by policy only during a Test (F80)") {
    Harness h;
    auto* sys = static_cast<test::FakeSystem*>(h.plat.system.get());
    json sig = test::sample_signatures();
    sig["extensions"].push_back({{"id", "ext.browser.ai"}, {"ide", "browser"}, {"severity", "medium"},
                                 {"ext_ids", {"jgjaeacdkonaoafenlfkkkmbaopkbilf", "not-a-store-id"}}});
    REQUIRE(h.agent->apply_signatures({{"version", "t3"}, {"bundle", sig.dump()}}, false));
    h.agent->apply_policy(Policy::default_for(Mode::test));
    CHECK(sys->blocked_extensions == std::vector<std::string>{"jgjaeacdkonaoafenlfkkkmbaopkbilf"});
    h.agent->apply_policy(Policy::default_for(Mode::practice));
    CHECK(sys->blocked_extensions.empty());
}
