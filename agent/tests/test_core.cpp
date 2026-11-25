#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <iterator>

#include "fakes.hpp"
#include "xg/artifact_scan.hpp"
#include "xg/config.hpp"
#include "xg/event_queue.hpp"
#include "xg/isolation.hpp"
#include "xg/json_read.hpp"
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
    CHECK(re.match_extension({"antigravity", "github.copilot", "1.0", "alice", ""})->rule_id == "ext.copilot");
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
    CHECK(test.suppress_ai_overview);
    auto always = Policy::default_for(Mode::always_on);
    CHECK(always.actions_for("x", Severity::critical) == std::vector<Action>{Action::log, Action::alert});  // never closes
    CHECK_FALSE(always.suppress_ai_overview);
    auto practice = Policy::default_for(Mode::practice);
    CHECK(practice.actions_for("x", Severity::high) == std::vector<Action>{Action::log, Action::alert});  // never closes
    CHECK_FALSE(practice.suppress_ai_overview);
    CHECK_FALSE(always.collect.keys);

    json j = {{"version", 7}, {"mode", "test"}, {"session_id", 3}, {"title_capture", "full"},
              {"collect", {{"keys", false}}}, {"intervals", {{"heartbeat_s", 1}}},
              {"actions", {{"rules", {{"proc.ollama", {"log", "kill", "alert"}}}}}},
              {"disabled_rules", {"vm.container"}},
              {"suppress_ai_overview", true}};
    auto p = Policy::from_json(j);
    CHECK(p.version == 7);
    CHECK(p.mode == Mode::test);
    CHECK(p.suppress_ai_overview);
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
    // The indicator follows the policy (mode + notice); there is no separate command for it.
    CHECK_FALSE(protocol::parse_command({{"id", "c3"}, {"type", "set_indicator"}, {"args", json::object()}}));
    CHECK(protocol::parse_command({{"id", 42}, {"type", "kill"}, {"args", {{"pid", 5}}}})->id == "42");
}

TEST_CASE("server JSON with wrong types never throws (I-AGENT-13)") {
    // Ack fields of the wrong type fall back to defaults instead of throwing json::type_error.
    std::optional<protocol::BatchAck> ack;
    REQUIRE_NOTHROW(ack = protocol::parse_batch_ack(
                        R"({"ack_seq":3,"server_time_ms":"x","policy_version":[1],"signatures_version":7,"isolated":"yes"})"));
    REQUIRE(ack);
    CHECK(ack->server_time_ms == 0);
    CHECK(ack->policy_version == 0);
    CHECK(ack->signatures_version.empty());
    CHECK_FALSE(ack->isolated);
    // Command type/args of the wrong type are rejected, not thrown.
    CHECK_NOTHROW(protocol::parse_command({{"id", "c1"}, {"type", 5}}));
    CHECK_FALSE(protocol::parse_command({{"id", "c1"}, {"type", 5}}));
    CHECK_FALSE(protocol::parse_command({{"id", "c1"}, {"type", "ping"}, {"args", "x"}}));
    // jr helpers: type and range checked reads.
    const json j = {{"s", "v"}, {"n", 5}, {"neg", -2}, {"big", 18446744073709551615ULL}, {"f", 0.5}, {"b", true}};
    CHECK(jr::str(j, "n", "d") == "d");
    CHECK(jr::i64(j, "s", 7) == 7);
    CHECK(jr::i64(j, "big", 7) == 7);
    CHECK(jr::i64(j, "neg", 7, 0) == 7);
    CHECK(jr::i64(j, "n") == 5);
    CHECK(jr::num(j, "f", 0, 0, 1) == 0.5);
    CHECK(jr::boolean(j, "n", true));
    CHECK(jr::obj(j, "s").empty());
    CHECK(jr::str(json::array(), "s", "d") == "d");
    // Policy with a non-string mode does not throw.
    CHECK_NOTHROW(Policy::from_json({{"mode", 3}}));
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

TEST_CASE("config drops the old screen gateway token and address on save") {
    auto path = (fs::temp_directory_path() / ("xg-" + random_hex(4) + ".toml")).string();
    {
        std::ofstream f(path);
        f << "[ccc]\nhost = \"10.0.0.2\"\n[screen]\nenabled = false\nport = 8082\n"
             "path = \"/agent\"\ntls = false\ntoken = \"old-shared-secret\"\n";
    }
    bool ok = false;
    auto c = Config::load(path, &ok);
    REQUIRE(ok);
    CHECK_FALSE(c.screen_enabled);
    REQUIRE(c.save(path));
    std::ifstream in(path);
    const std::string saved((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK(saved.find("old-shared-secret") == std::string::npos);
    CHECK(saved.find("port = 8082") == std::string::npos);
    CHECK(Config::load(path, &ok).screen_enabled == false);
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

TEST_CASE("is_public_ip and tunnel hints: only public destinations on listed ports") {
    using xg::isolation::is_public_ip;
    for (const char* pub : {"8.8.8.8", "198.51.100.7", "::ffff:140.82.114.22", "2001:4860:4860::8888", "2a00:1450::1"})
        CHECK(is_public_ip(pub));
    for (const char* lan : {"10.0.0.5", "192.168.1.10", "172.20.0.1", "127.0.0.1", "169.254.3.4", "::ffff:10.1.1.1",
                            "::1", "fe80::1", "fd00::5", "ff02::1", "200::1", "", "not-an-ip", "2001"})
        CHECK_FALSE(is_public_ip(lan));

    auto set = SignatureSet::from_json(test::sample_signatures());
    CHECK(set.web.tunnel.id == "net.tunnel_flow");
    CHECK(set.web.is_tunnel("198.51.100.7", 22));
    CHECK_FALSE(set.web.is_tunnel("198.51.100.7", 2222));
    CHECK_FALSE(set.web.is_tunnel("192.168.1.10", 22));

    // 80/443 and junk ports are dropped; an id outside net.* disables the rule.
    auto j = test::sample_signatures();
    j["web"]["tunnel"] = {{"id", "net.t"}, {"tcp_ports", json::array({443, 80, 0, 70000, "22", 1194})}};
    CHECK(SignatureSet::from_json(j).web.tunnel.tcp_ports == std::vector<uint16_t>{1194});
    j["web"]["tunnel"]["id"] = "proc.t";
    CHECK_FALSE(SignatureSet::from_json(j).web.is_tunnel("198.51.100.7", 1194));
}

TEST_CASE("artifact scan gives each root its own file budget and skips noisy cache dirs") {
    const fs::path base = fs::temp_directory_path() / ("xg-art-" + random_hex(4));
    const fs::path home = base / "home";
    const fs::path usb = base / "usb";
    fs::create_directories(home / ".cache/huge");
    fs::create_directories(home / ".antigravity/extensions");
    fs::create_directories(home / "docs");
    fs::create_directories(usb);

    // Exhaust > max_files inside home docs AND noise inside skipped dirs, plus a real model on usb root
    for (int i = 0; i < 6; ++i) write_file_atomic((home / "docs" / ("f" + std::to_string(i) + ".txt")).string(), "x");
    write_file_atomic((home / ".cache/huge/junk.txt").string(), "j");
    write_file_atomic((home / ".antigravity/extensions/cached.gguf").string(), "skipme");
    write_file_atomic((usb / "phi3.gguf").string(), "weights");

    json sig = test::sample_signatures();
    sig["artifacts"]["scan"] = {{"roots", {home.string(), usb.string()}}, {"max_depth", 4}, {"max_files", 4}, {"skip_dirs", json::array()}};
    RuleEngine re(SignatureSet::from_json(sig));
    std::atomic<bool> stop{false};
    const auto res = scan_artifacts(re, {home.string()}, stop);

    CHECK(res.truncated);  // home root hit max_files=4
    bool saw_usb = false, saw_skipped = false;
    for (const auto& a : res.found) {
        if (a.path == (usb / "phi3.gguf").string() && a.rule_id == "file.gguf") saw_usb = true;
        if (a.path.find(".antigravity") != std::string::npos) saw_skipped = true;
    }
    CHECK(saw_usb);
    CHECK_FALSE(saw_skipped);
    fs::remove_all(base);
}

TEST_CASE("Policy reload replaces work_dirs instead of accumulating stale directories") {
    json p1 = {
        {"version", 1},
        {"profile", "exam"},
        {"mode", "test"},
        {"work_dirs", {"/tmp/lab-a", "/tmp/lab-b"}},
    };
    const Policy first = Policy::from_json(p1);
    REQUIRE(first.work_dirs.size() == 2);

    json p2 = {
        {"version", 2},
        {"profile", "exam"},
        {"mode", "test"},
        {"work_dirs", {"/tmp/lab-c"}},
    };
    const Policy second = Policy::from_json(p2);
    REQUIRE(second.work_dirs.size() == 1);
    CHECK(second.work_dirs[0] == "/tmp/lab-c");
}

#ifndef _WIN32
#include <sys/stat.h>
TEST_CASE("write_file_atomic creates secrets 0600 from the start and leaves no temp file (I-AGENT-14)") {
    const fs::path dir = fs::temp_directory_path() / ("xgw-" + random_hex(4));
    fs::create_directories(dir);
    const auto old_mask = ::umask(0);  // a permissive umask must not widen anything
    const std::string secret = (dir / "agent.toml").string(), pub = (dir / "ca.crt").string();
    REQUIRE(write_file_atomic(secret, "token = \"x\"\n", FileMode::secret));
    REQUIRE(write_file_atomic(pub, "cert\n"));
    ::umask(old_mask);
    struct stat st{};
    REQUIRE(::stat(secret.c_str(), &st) == 0);
    CHECK((st.st_mode & 0777) == 0600);
    REQUIRE(::stat(pub.c_str(), &st) == 0);
    CHECK((st.st_mode & 0777) == 0644);
    // Overwrite keeps content atomic; a planted symlink at the old fixed temp name is never followed.
    fs::create_symlink("/etc/passwd", secret + ".tmp");
    REQUIRE(write_file_atomic(secret, "v2", FileMode::secret));
    CHECK(read_file(secret) == "v2");
    size_t files = 0;
    for (const auto& e : fs::directory_iterator(dir)) files += e.path().filename().string().find(".tmp.") != std::string::npos;
    CHECK(files == 0);
    fs::remove_all(dir);
}
#endif

#ifndef _WIN32
TEST_CASE("protect_secret_file narrows an existing file to owner-only (I-AGENT-2)") {
    const fs::path f = fs::temp_directory_path() / ("xg-sec-" + random_hex(4));
    REQUIRE(write_file_atomic(f.string(), "token"));  // shared: 0644
    REQUIRE(protect_secret_file(f.string()));
    CHECK((fs::status(f).permissions() & fs::perms::all) == (fs::perms::owner_read | fs::perms::owner_write));
    fs::remove(f);
}
#endif

TEST_CASE("make_private_temp_dir never reuses a directory (I-AGENT-24)") {
    const auto a = make_private_temp_dir("xg-test-scan");
    const auto b = make_private_temp_dir("xg-test-scan");
    REQUIRE_FALSE(a.empty());
    REQUIRE_FALSE(b.empty());
    CHECK(a != b);
    CHECK(fs::is_empty(a));
#ifndef _WIN32
    CHECK((fs::status(a).permissions() & fs::perms::all) == fs::perms::owner_all);
#endif
    fs::remove_all(a);
    fs::remove_all(b);
}

TEST_CASE("event queue hard cap evicts oldest non-tamper events when low-value ones are gone (I-AGENT-16)") {
    const fs::path dir = fs::temp_directory_path() / ("xgq-" + random_hex(4));
    fs::create_directories(dir);
    {
        EventQueue q((dir / "q.db").string(), 50, 1u << 30);
        q.emit(ev::tamper, {{"kind", "first"}});
        for (int i = 0; i < 500; ++i) q.emit(ev::paste, {{"chars", i}});
        CHECK(q.size() <= 60);
        auto evs = q.peek(100);
        REQUIRE_FALSE(evs.empty());
        CHECK(evs.front().type == ev::tamper);  // tamper evidence survives
        CHECK(evs.back().data["chars"] == 499);  // newest kept
    }
    {
        EventQueue q((dir / "b.db").string(), 100000, 20000);  // byte cap
        for (int i = 0; i < 300; ++i) q.emit(ev::paste, {{"pad", std::string(200, 'x')}});
        size_t bytes = 0;
        for (const auto& e : q.peek(1000)) bytes += e.data.dump().size();
        CHECK(bytes < 30000);
    }
    fs::remove_all(dir);
}

TEST_CASE("known model-store walk honours max_files and max_depth (I-AGENT-29)") {
    const fs::path home = fs::temp_directory_path() / ("xg-store-" + random_hex(4));
    fs::create_directories(home / ".ollama/models/a/b/c/d/e");
    for (int i = 0; i < 50; ++i) write_file_atomic((home / ".ollama/models" / ("blob" + std::to_string(i))).string(), "x");
    json sig = test::sample_signatures();
    sig["artifacts"]["rules"].push_back({{"id", "store.ollama"}, {"severity", "high"}, {"dirs", {"~/.ollama/models"}}});
    sig["artifacts"]["scan"] = {{"roots", json::array()}, {"max_depth", 2}, {"max_files", 10}, {"skip_dirs", json::array()}};
    RuleEngine re(SignatureSet::from_json(sig));
    std::atomic<bool> stop{false};
    const auto res = scan_artifacts(re, {home.string()}, stop);
    CHECK(res.truncated);
    CHECK(res.files_visited <= 11);
    bool found = false;
    for (const auto& a : res.found) found = found || a.rule_id == "store.ollama";
    CHECK(found);  // the store is still reported
    fs::remove_all(home);
}

TEST_CASE("default student notice states live screen viewing truthfully (I-AGENT-21)") {
    const auto n = Policy::default_for(Mode::test).notice_text;
    CHECK(n.find("view this screen live") != std::string::npos);
    CHECK(n.find("screen content is recorded") == std::string::npos);
}

TEST_CASE("signature bundle web hints: AI sites only in browsers, extension domains, net hints") {
    json sig = test::sample_signatures();
    sig["extensions"].push_back({{"id", "ext.jb.misc"}, {"ide", "jetbrains"}, {"ext_ids", {"tabnine", "codeium"}},
                                 {"id_domains", {{"tabnine", {"tabnine.com"}}}}});
    sig["extensions"][0]["domains"] = {"githubcopilot.com"};
    sig["web"] = json::parse(R"({
      "browsers": ["chrome", "firefox"],
      "browser_title_suffixes": [" - google chrome", " - mozilla firefox"],
      "ai_sites": [{"site": "Claude AI", "equals": ["claude"], "suffix": [" - claude"], "contains": ["claude.ai"]},
                   {"site": "ChatGPT", "contains": ["chatgpt"]},
                   {"site": "Broken"}],
      "telemetry_domains": ["telemetry"],
      "process_domains": {"Claude.exe": "API.anthropic.com"}})");
    std::string err;
    auto set = SignatureSet::from_json(sig, &err);
    REQUIRE(err.empty());
    CHECK(set.web.ai_sites.size() == 2);  // a site without patterns is dropped
    CHECK(set.web.process_domains.at("claude") == "api.anthropic.com");
    RuleEngine re(std::move(set));
    CHECK(re.match_ai_site("chrome.exe", "Claude - Google Chrome") == "Claude AI");
    CHECK(re.match_ai_site("firefox", "Refactor plan - Claude - Mozilla Firefox") == "Claude AI");
    CHECK(re.match_ai_site("chrome", "ChatGPT") == "ChatGPT");
    // I-AGENT-18: not every title with the word is an AI site, and non-browsers are never checked.
    CHECK_FALSE(re.match_ai_site("chrome", "Claude Debussy - Wikipedia - Google Chrome"));
    CHECK_FALSE(re.match_ai_site("code", "claude_notes.py - Visual Studio Code"));
    CHECK_FALSE(re.match_ai_site("code", "ChatGPT"));
    ExtensionInfo copilot{"vscode", "github.copilot", "1", "alice", "", false, ""};
    CHECK(re.extension_domains(copilot, "ext.copilot") == std::vector<std::string>{"githubcopilot.com"});
    ExtensionInfo tab{"jetbrains", "tabnine-intellij", "1", "alice", "", false, ""};
    CHECK(re.extension_domains(tab, "ext.jb.misc") == std::vector<std::string>{"tabnine.com"});
    ExtensionInfo cod{"jetbrains", "codeium", "1", "alice", "", false, ""};
    CHECK(re.extension_domains(cod, "ext.jb.misc").empty());
}

TEST_CASE("PAC URLs are only trusted as https on the CCC host (F97)") {
    CHECK(trusted_pac_url("https://192.168.10.2/proxy.pac", "192.168.10.2"));
    CHECK(trusted_pac_url("https://CCC.lab:443/proxy.pac", "ccc.lab"));
    CHECK(trusted_pac_url("https://[fd00::2]/proxy.pac", "fd00::2"));
    CHECK_FALSE(trusted_pac_url("http://192.168.10.2/proxy.pac", "192.168.10.2"));     // plain HTTP
    CHECK_FALSE(trusted_pac_url("https://evil.example/proxy.pac", "192.168.10.2"));    // other host
    CHECK_FALSE(trusted_pac_url("https://192.168.10.2@evil/p.pac", "192.168.10.2"));   // userinfo trick
    CHECK_FALSE(trusted_pac_url("https://192.168.10.2.evil/p.pac", "192.168.10.2"));
    CHECK_FALSE(trusted_pac_url("https://192.168.10.2:80x/p.pac", "192.168.10.2"));
    CHECK_FALSE(trusted_pac_url("https://192.168.10.2/p\".pac", "192.168.10.2"));
    CHECK_FALSE(trusted_pac_url("https://192.168.10.2/proxy.pac", ""));
}
