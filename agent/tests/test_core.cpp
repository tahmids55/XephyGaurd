#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>

#include "fakes.hpp"
#include "xg/config.hpp"
#include "xg/event_queue.hpp"
#include "xg/isolation.hpp"
#include "xg/keystats.hpp"
#include "xg/paste.hpp"
#include "xg/policy.hpp"
#include "xg/process_tracker.hpp"
#include "xg/protocol.hpp"
#include "xg/rules.hpp"
#include "xg/util.hpp"

using namespace xg;
namespace fs = std::filesystem;

TEST_CASE("util helpers") {
    CHECK(normalize_exe("C:\\Program Files\\Ollama\\Ollama.EXE") == "ollama");
    CHECK(normalize_exe("/usr/bin/llama-server") == "llama-server");
    CHECK(glob_match("*.gguf", "Model-Q4.GGUF"));
    CHECK_FALSE(glob_match("*.gguf", "model.gguf.part"));
    CHECK(glob_match("qemu-system-*", "qemu-system-x86_64"));
    CHECK(icontains("node /usr/lib/node_modules/@Anthropic-AI/claude-code/cli.js", "@anthropic-ai/claude-code"));
    CHECK(expand_path("~/.ollama/models", "/home/a") == "/home/a/.ollama/models");
    CHECK(expand_path("%USERPROFILE%\\.lmstudio", "C:\\Users\\a") == "C:\\Users\\a\\.lmstudio");
    CHECK(split("a,,b", ',').size() == 2);
}

TEST_CASE("rule engine matches processes") {
    RuleEngine re(SignatureSet::from_json(test::sample_signatures()));
    ProcessInfo p;
    p.exe_path = "/usr/local/bin/ollama";
    p.name = "ollama";
    auto hits = re.match_process(p);
    REQUIRE(hits.size() == 1);
    CHECK(hits[0].rule_id == "proc.ollama");
    CHECK(hits[0].severity == Severity::high);

    ProcessInfo llama{1, 0, "/opt/llama-server", "llama-server", "", "", 0, 0};
    CHECK(re.match_process(llama).at(0).rule_id == "proc.llamacpp");

    // CLI agent launched through node: cmdline match only when the interpreter is node.
    ProcessInfo node{2, 0, "/usr/bin/node", "node", "node /usr/lib/node_modules/@anthropic-ai/claude-code/cli.js", "", 0, 0};
    CHECK(re.match_process(node).at(0).rule_id == "proc.cli.claude_code");
    ProcessInfo grep{3, 0, "/usr/bin/grep", "grep", "grep @anthropic-ai/claude-code notes.txt", "", 0, 0};
    CHECK(re.match_process(grep).empty());

    ProcessInfo benign{4, 0, "/usr/bin/code", "code", "code .", "", 0, 0};
    CHECK(re.match_process(benign).empty());

    re.set_disabled({"proc.ollama"});
    CHECK(re.match_process(p).empty());
}

TEST_CASE("rule engine: extensions, ports, artifacts, adapters, devices") {
    RuleEngine re(SignatureSet::from_json(test::sample_signatures()));
    CHECK(re.match_extension({"vscode", "GitHub.Copilot", "1.0", "alice", ""})->rule_id == "ext.copilot");
    CHECK(re.match_extension({"cursor", "github.copilot-chat", "1.0", "alice", ""})->rule_id == "ext.copilot");
    CHECK_FALSE(re.match_extension({"vscode", "ms-python.python", "1.0", "alice", ""}));
    CHECK(re.match_extension({"jetbrains", "ml-llm-plugin", "", "", ""})->rule_id == "ext.jb.ai");
    CHECK(re.match_port(11434)->rule_id == "port.ollama");
    CHECK_FALSE(re.match_port(22));
    CHECK(re.match_artifact_file("/home/a/m/llama.gguf", 10)->rule_id == "file.gguf");
    CHECK_FALSE(re.match_artifact_file("/home/a/small.safetensors", 1024));
    CHECK(re.match_artifact_file("/home/a/big.safetensors", 600ull * 1024 * 1024)->rule_id == "file.safetensors");

    AdapterInfo usb{"usb0", "", "rndis_host", "", {}, true, false, false, false};
    CHECK(re.classify_adapter(usb)->rule_id == "net.tether_usb");
    AdapterInfo wg{"wg0", "", "wireguard", "", {}, true, false, false, true};
    CHECK(re.classify_adapter(wg)->rule_id == "net.vpn_adapter");
    AdapterInfo eth{"enp3s0", "", "e1000e", "", {}, true, true, false, false};
    CHECK_FALSE(re.classify_adapter(eth));
    CHECK(re.second_route_rule()->rule_id == "net.second_route");

    DeviceInfo phone{"add", "usb", "usb_device", "18d1", "4ee7", "Pixel", ""};
    CHECK(re.match_device(phone)->rule_id == "dev.phone");
    DeviceInfo stick{"add", "usb", "usb_storage", "0781", "5567", "", ""};
    CHECK(re.match_device(stick)->rule_id == "dev.usb_storage");
}

TEST_CASE("policy defaults and JSON round trip") {
    auto test = Policy::default_for(Mode::test);
    auto acts = test.actions_for("proc.ollama", Severity::high);
    CHECK(acts == std::vector<Action>{Action::log, Action::warn, Action::alert, Action::kill});  // AI apps are closed in tests
    auto always = Policy::default_for(Mode::always_on);
    CHECK(always.actions_for("x", Severity::critical) == std::vector<Action>{Action::log, Action::alert});  // never closes
    auto practice = Policy::default_for(Mode::practice);
    CHECK(practice.actions_for("x", Severity::high) == std::vector<Action>{Action::log, Action::alert});  // never closes
    CHECK_FALSE(always.collect.keys);

    json j = {{"version", 7}, {"mode", "test"}, {"session_id", 3}, {"title_capture", "full"},
              {"collect", {{"keys", false}}}, {"intervals", {{"heartbeat_s", 1}}},
              {"actions", {{"rules", {{"proc.ollama", {"log", "kill", "alert"}}}}}},
              {"disabled_rules", {"vm.container"}}};
    auto p = Policy::from_json(j);
    CHECK(p.version == 7);
    CHECK(p.mode == Mode::test);
    CHECK_FALSE(p.collect.keys);
    CHECK(p.collect.clipboard);                // inherited from the Test default
    CHECK(p.intervals.heartbeat_s == 2);       // clamped
    CHECK(p.actions_for("proc.ollama", Severity::high) == std::vector<Action>{Action::log, Action::kill, Action::alert});
    CHECK(p.actions_for("vm.container", Severity::low).empty());
    CHECK(p.filter_title("code", "secret.txt - code") == "secret.txt - code");
    auto again = Policy::from_json(p.to_json());
    CHECK(again.to_json() == p.to_json());

    Policy app_only = Policy::default_for(Mode::practice);
    CHECK(app_only.filter_title("firefox", "My private mail") == "firefox");
}

TEST_CASE("event queue persists, acks and keeps sequence monotonic") {
    auto dir = fs::temp_directory_path() / ("xgq-" + random_hex(4));
    auto db = (dir / "q.db").string();
    {
        EventQueue q(db);
        REQUIRE(q.ok());
        CHECK(q.push("a", {{"x", 1}}, 10) == 1);
        CHECK(q.push("b", {{"x", 2}}, 11) == 2);
        CHECK(q.push("c", {{"x", 3}}, 12) == 3);
        auto ev = q.peek(10);
        REQUIRE(ev.size() == 3);
        CHECK(ev[1].type == "b");
        CHECK(ev[1].data["x"] == 2);
        q.ack(2);
        CHECK(q.size() == 1);
        q.kv_set("policy", "{}");
    }
    {
        EventQueue q(db);  // reopen: survives restart
        CHECK(q.size() == 1);
        CHECK(q.kv_get("policy").value() == "{}");
        q.ack(3);
        CHECK(q.size() == 0);
        CHECK(q.push("d", json::object(), 13) == 4);  // never reuses sequence numbers
    }
    fs::remove_all(dir);
}

TEST_CASE("protocol batch and command validation") {
    std::vector<Event> evs;
    for (int i = 1; i <= 3; ++i) evs.push_back({i, 1000 + i, "process_start", {{"exe", "ollama"}}});
    size_t n = 0;
    auto b = protocol::build_batch("12", evs, {{"mode", "test"}}, &n);
    CHECK(n == 3);
    CHECK(b["events"].size() == 3);
    CHECK(b["pc_id"] == "12");

    auto ack = protocol::parse_batch_ack(R"({"ack_seq":3,"server_time_ms":5000,"policy_version":9,"isolated":true})");
    REQUIRE(ack);
    CHECK(ack->ack_seq == 3);
    CHECK(ack->isolated);
    CHECK_FALSE(protocol::parse_batch_ack("not json"));
    CHECK(protocol::clock_offset(1000, 1100, 2050) == 1000);

    CHECK(protocol::parse_command({{"id", "c1"}, {"type", "message"}, {"args", {{"text", "hi"}}}}));
    CHECK_FALSE(protocol::parse_command({{"id", "c1"}, {"type", "rm -rf"}, {"args", json::object()}}));
    CHECK_FALSE(protocol::parse_command({{"id", "bad id!"}, {"type", "ping"}}));
    CHECK_FALSE(protocol::parse_command({{"id", "c2"}, {"type", "kill"}, {"args", json::object()}}));
    CHECK(protocol::parse_command({{"id", 42}, {"type", "kill"}, {"args", {{"pid", 5}}}})->id == "42");
}

TEST_CASE("key stats aggregate timing without key identity") {
    KeyStatsAggregator agg(5);
    const int64_t t0 = 1'000'000;
    for (int i = 0; i < 20; ++i) agg.on_key(t0 + i * 100, i % 10 == 9);
    CHECK_FALSE(agg.flush(t0 + 1000));
    auto w = agg.flush(t0 + 6000);
    REQUIRE(w);
    CHECK(w->keys == 20);
    CHECK(w->backspaces == 2);
    CHECK(w->iki_mean_ms == Catch::Approx(100.0));
    CHECK(w->bursts == 1);
    CHECK(w->backspace_ratio() == Catch::Approx(0.1));
    CHECK_FALSE(agg.flush(t0 + 20000));
}

TEST_CASE("clipboard text is reduced to features") {
    const std::string code = "#include <stdio.h>\nint main(void) {\n    for (int i = 0; i < 3; i++) {\n        printf(\"%d\\n\", i);\n    }\n    return 0;\n}\n";
    auto p = analyze_clipboard_text(code);
    CHECK(p.lines == 8);
    CHECK(p.looks_like_code);
    auto prose = analyze_clipboard_text("The quick brown fox jumps over the lazy dog. It was a sunny day in the lab.");
    CHECK_FALSE(prose.looks_like_code);
    CHECK(analyze_clipboard_text("héllo").chars == 5);
}

TEST_CASE("process tracker diffs snapshots") {
    ProcessTracker t;
    std::vector<ProcessInfo> s1{{1, 0, "/sbin/init", "init", "", "", 0, 5}, {10, 1, "/usr/bin/bash", "bash", "", "", 0, 7}};
    auto d1 = t.update(s1);
    CHECK(d1.started.size() == 2);  // first snapshot reports everything
    auto s2 = s1;
    s2.push_back({20, 10, "/usr/bin/ollama", "ollama", "", "", 0, 9});
    auto d2 = t.update(s2);
    REQUIRE(d2.started.size() == 1);
    CHECK(d2.started[0].pid == 20);
    auto d3 = t.update(s1);
    REQUIRE(d3.stopped.size() == 1);
    CHECK(d3.stopped[0].pid == 20);
    auto n1 = t.name_snapshot();
    CHECK(n1.count == 2);
    auto n2 = t.name_snapshot();
    CHECK(n2.hash == n1.hash);
    CHECK(n2.added.empty());
}

TEST_CASE("config round trip") {
    auto path = (fs::temp_directory_path() / ("xg-" + random_hex(4) + ".toml")).string();
    Config c = Config::defaults();
    c.ccc_host = "192.168.10.2";
    c.pc_id = "17";
    c.token = "tok";
    REQUIRE(c.save(path));
    bool ok = false;
    auto d = Config::load(path, &ok);
    CHECK(ok);
    CHECK(d.ccc_host == "192.168.10.2");
    CHECK(d.enrolled());
    fs::remove(path);
}

TEST_CASE("isolation range math keeps LAN and the CCC reachable") {
    using namespace xg::isolation;
    CHECK(parse_ipv4("192.168.0.51") == 0xC0A80033u);
    CHECK_FALSE(parse_ipv4("300.1.1.1"));
    CHECK_FALSE(parse_ipv4("1.2.3"));
    auto in_ranges = [](const std::vector<std::string>& rs, const std::string& ip) {
        const auto v = *parse_ipv4(ip);
        for (const auto& r : rs) {
            const auto dash = r.find('-');
            if (v >= *parse_ipv4(r.substr(0, dash)) && v <= *parse_ipv4(r.substr(dash + 1))) return true;
        }
        return false;
    };
    auto blocked = blocked_ipv4_ranges({});
    for (const char* lan : {"10.1.2.3", "172.16.0.1", "172.31.255.255", "192.168.0.51", "169.254.1.1", "127.0.0.1", "224.0.0.251", "255.255.255.255"})
        CHECK_FALSE(in_ranges(blocked, lan));
    for (const char* pub : {"8.8.8.8", "1.1.1.1", "140.82.114.22", "172.15.255.255", "172.32.0.1", "192.167.255.255", "192.169.0.1", "100.64.0.1"})
        CHECK(in_ranges(blocked, pub));
    auto carved = blocked_ipv4_ranges({"203.0.113.9", "not-an-ip"});
    CHECK_FALSE(in_ranges(carved, "203.0.113.9"));
    CHECK(in_ranges(carved, "203.0.113.8"));
    CHECK(in_ranges(carved, "203.0.113.10"));
}
