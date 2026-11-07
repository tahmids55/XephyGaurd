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
    test::FakeSystem* sys;
    VectorSink sink;
    std::unique_ptr<Agent> agent;

    Harness() {
        fs::create_directories(dir);
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
        auto s = std::make_unique<test::FakeSystem>();
        sys = s.get();
        plat.system = std::move(s);
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
    CHECK(hits[0].data["actions"] == json({"log", "warn", "alert"}));
    CHECK(h.ipc->count_containing("\"notify\"") == 1);
    CHECK(h.procs->killed.empty());  // kill is opt-in
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
    CHECK(hits[0].data["actions"] == json({"log"}));
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

TEST_CASE("agent reports AI extensions once per change") {
    Harness h;
    h.agent->apply_policy(Policy::default_for(Mode::test));
    h.exts->list = {{"vscode", "ms-python.python", "1", "alice", ""}};
    h.agent->scan_extensions();
    CHECK(h.of(ev::extension_scan).size() == 1);
    CHECK(h.of(ev::rule_hit).empty());
    h.exts->list.push_back({"vscode", "github.copilot", "1.2", "alice", ""});
    h.agent->scan_extensions();
    CHECK(h.of(ev::extension_scan).size() == 2);
    REQUIRE(h.of(ev::rule_hit).size() == 1);
    h.agent->scan_extensions();
    CHECK(h.of(ev::extension_scan).size() == 2);  // unchanged list -> no new scan event
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
    h.agent->apply_policy(Policy::default_for(Mode::test));
    h.agent->handle_command({"c1", "message", {{"text", "Ten minutes left"}}});
    CHECK(h.ipc->count_containing("Ten minutes left") == 1);
    h.procs->procs = {{77, 1, "/usr/bin/cursor", "cursor", "", "alice", 0, 1}};
    h.agent->handle_command({"c2", "kill", {{"exe", "cursor"}}});
    CHECK(h.procs->killed == std::vector<int64_t>{77});
    auto res = h.of(ev::command_result);
    REQUIRE(res.size() == 2);
    CHECK(res[1].data["command_id"] == "c2");
    auto hb = h.agent->build_heartbeat();
    CHECK(hb["mode"] == "test");
    CHECK(hb["signatures_version"] == "test-1");
}

TEST_CASE("tools already running are re-evaluated when a Test session starts") {
    Harness h;
    h.agent->apply_policy(Policy::default_for(Mode::always_on));
    h.procs->procs = {{1, 0, "/sbin/init", "init", "", "root", 0, 1}, {42, 1, "/usr/bin/ollama", "ollama", "", "alice", 0, 2}};
    h.agent->scan_processes();
    REQUIRE(h.of(ev::rule_hit).size() == 1);
    CHECK(h.of(ev::rule_hit)[0].data["actions"] == json({"log"}));
    h.agent->apply_policy(Policy::default_for(Mode::test));
    h.agent->reevaluate_all();
    auto hits = h.of(ev::rule_hit);
    REQUIRE(hits.size() == 2);
    CHECK(hits[1].data["actions"] == json({"log", "warn", "alert"}));
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

TEST_CASE("copilot activity scan emits ide_ai_activity once per file change and never fabricates window_focus (R1/R2/R8)") {
    Harness h;
    h.agent->apply_policy(Policy::default_for(Mode::test));
    h.sys->sess = {{"alice", h.dir.string(), 1000, ":0", "x11", true}};
    const fs::path gs = h.dir / ".config" / "Code" / "User" / "globalStorage" / "github.copilot-chat";
    fs::create_directories(gs);
    const fs::path db = gs / "session-store.db";
    write_file_atomic(db.string(), "initial-state");

    // 1. Initial scan records baseline without emitting.
    h.agent->scan_copilot_activity(1000);
    CHECK(h.of(ev::ide_ai_activity).empty());
    CHECK(h.of(ev::window_focus).empty());
    CHECK(h.of(ev::rule_hit).empty());

    // 2. Unchanged file on subsequent scan emits nothing.
    h.agent->scan_copilot_activity(5000);
    CHECK(h.of(ev::ide_ai_activity).empty());

    // 3. File modification emits a single ide_ai_activity event with medium confidence and never window_focus.
    write_file_atomic(db.string(), "updated-state-with-new-bytes");
    h.agent->scan_copilot_activity(10000);
    auto evs = h.of(ev::ide_ai_activity);
    REQUIRE(evs.size() == 1);
    CHECK(evs[0].data["tool"] == "github-copilot");
    CHECK(evs[0].data["ide"] == "vscode");
    CHECK(evs[0].data["evidence"] == "file_activity");
    CHECK(evs[0].data["detail"] == "session-store.db");
    CHECK(evs[0].data["confidence"] == "medium");
    CHECK(evs[0].data["meaning"] == "IDE AI extension storage was written to");
    CHECK(evs[0].data.contains("not_meaning"));
    CHECK(h.of(ev::window_focus).empty());
    CHECK(h.of(ev::rule_hit).empty());
    CHECK(h.ipc->count_containing("\"notify\"") == 0);

    // 4. Subsequent scan without further file changes does not re-emit even after >12s.
    h.agent->scan_copilot_activity(30000);
    CHECK(h.of(ev::ide_ai_activity).size() == 1);
}

TEST_CASE("isolation lease renews, expires, saves/restores routes, and recovers on startup (Contract C3 / R11)") {
    Harness h;
    h.agent->apply_policy(Policy::default_for(Mode::test));

    // Isolate via isolate_notice with a 10s lease.
    h.agent->handle_command({"iso1", "isolate_notice", {{"isolated", true}, {"lease_s", 10}, {"reason", "exam check"}}});
    CHECK(h.agent->is_isolated());
    CHECK(h.agent->isolation_lease_s() == 10);
    CHECK(fs::exists(h.agent->isolation_state_path()));

    const int64_t t0 = h.agent->isolation_renewed_ms();
    // 5s later: lease still valid.
    h.agent->check_isolation_lease(t0 + 5000);
    CHECK(h.agent->is_isolated());
    CHECK(h.of(ev::isolation_lease_expired).empty());

    // Renew lease at t0 + 8s for another 10s.
    h.agent->renew_isolation(t0 + 8000, 10);
    // At t0 + 15s (7s after renewal): still valid.
    h.agent->check_isolation_lease(t0 + 15000);
    CHECK(h.agent->is_isolated());
    CHECK(h.of(ev::isolation_lease_expired).empty());

    // At t0 + 19s (11s after renewal > 10s lease): lease expires, network restored, event emitted.
    h.agent->check_isolation_lease(t0 + 19000);
    CHECK_FALSE(h.agent->is_isolated());
    CHECK_FALSE(fs::exists(h.agent->isolation_state_path()));
    auto expired = h.of(ev::isolation_lease_expired);
    REQUIRE(expired.size() == 1);
    CHECK(expired[0].data["lease_s"] == 10);

    // Startup crash recovery: if isolation_saved.json is left on disk with a non-.1 gateway,
    // constructing Agent restores from it and removes the file.
    const std::string saved_path = h.agent->isolation_state_path();
    json leftover = {
        {"saved_at_ms", 12345},
        {"platform", "linux"},
        {"default_routes", json::array({"default via 10.20.30.254 dev eth0"})},
    };
    REQUIRE(write_file_atomic(saved_path, leftover.dump()));
    REQUIRE(fs::exists(saved_path));

    Config cfg = Config::defaults();
    cfg.data_dir = h.dir.string();
    Agent recovered(cfg, (h.dir / "agent.toml").string(), h.plat);
    CHECK_FALSE(recovered.is_isolated());
    CHECK_FALSE(fs::exists(saved_path));
    REQUIRE(recovered.last_restored_isolation_state().contains("default_routes"));
    CHECK(recovered.last_restored_isolation_state()["default_routes"][0] == "default via 10.20.30.254 dev eth0");
}

TEST_CASE("agent enforces mode clamp, blocks kills outside Test, and reverts hosts blocklist on session end (C1/C4/R6/R7)") {
    Harness h;
    const fs::path fake_hosts = h.dir / "hosts";
    write_file_atomic(fake_hosts.string(), "127.0.0.1 localhost\n");
    h.agent->set_hosts_path_for_tests(fake_hosts.string());

    // In Always-on mode: kill commands are refused and hosts blocklist is not applied.
    Policy ao = Policy::default_for(Mode::always_on);
    ao.collect.window = true;
    ao.title_capture = TitleCapture::full;
    ao.blocked_domains = {"chatgpt.com"};
    h.agent->apply_policy(ao);
    CHECK_FALSE(h.agent->policy().collect.window);
    CHECK(h.agent->policy().title_capture == TitleCapture::off);
    CHECK(h.agent->active_blocked_domains().empty());
    CHECK(read_file(fake_hosts.string()).find("chatgpt.com") == std::string::npos);

    h.procs->procs = {{88, 1, "/usr/bin/chrome", "chrome", "", "alice", 0, 1}};
    h.agent->handle_command({"k1", "kill", {{"exe", "chrome"}}});
    CHECK(h.procs->killed.empty());
    REQUIRE(h.of(ev::command_result).size() == 1);
    CHECK(h.of(ev::command_result)[0].data["status"] == "ignored");

    // In Test mode: hosts blocklist is applied, and reverted when returning to Always-on.
    Policy test_pol = Policy::default_for(Mode::test);
    test_pol.blocked_domains = {"chatgpt.com"};
    h.agent->apply_policy(test_pol);
    CHECK(read_file(fake_hosts.string()).find("0.0.0.0 chatgpt.com") != std::string::npos);

    h.agent->apply_policy(Policy::default_for(Mode::always_on));
    CHECK(read_file(fake_hosts.string()) == "127.0.0.1 localhost\n");
}
