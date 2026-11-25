// Characterization tests: pin CURRENT agent behavior (docs/features/agent). Not a spec.
// KNOWN BUG I-AGENT-n marks behavior that looks wrong but is asserted as-is.
#include <catch2/catch_test_macros.hpp>
#include <filesystem>

#include "fakes.hpp"
#include "xg/agent.hpp"
#include "xg/artifact_scan.hpp"
#include "xg/util.hpp"

using namespace xg;
namespace fs = std::filesystem;

namespace {
class FakeDevices : public IDeviceMonitor {
public:
    std::vector<DeviceInfo> list;
    std::vector<DeviceInfo> scan() override { return list; }
    bool start(std::function<void(const DeviceInfo&)>) override { return false; }
    void stop() override {}
};

struct H {
    fs::path dir = fs::temp_directory_path() / ("xgf-" + random_hex(4));
    ServicePlatform plat;
    test::FakeProcesses* procs;
    test::FakeIpc* ipc;
    test::FakeIsolator* iso;
    FakeDevices* devs;
    VectorSink sink;
    std::unique_ptr<Agent> agent;
    IIpcServer::Peer peer{7, 1000, 555, "alice"};

    H() {
        fs::create_directories(dir);
        auto i = std::make_unique<test::FakeIsolator>(); iso = i.get(); plat.isolator = std::move(i);
        auto p = std::make_unique<test::FakeProcesses>(); procs = p.get(); plat.processes = std::move(p);
        plat.network = std::make_unique<test::FakeNetwork>();
        plat.extensions = std::make_unique<test::FakeExtensions>();
        auto ip = std::make_unique<test::FakeIpc>(); ipc = ip.get(); plat.ipc = std::move(ip);
        auto d = std::make_unique<FakeDevices>(); devs = d.get(); plat.devices = std::move(d);
        plat.system = std::make_unique<test::FakeSystem>();
        Config cfg = Config::defaults();
        cfg.data_dir = dir.string();
        cfg.ccc_host = "10.0.0.5";
        agent = std::make_unique<Agent>(cfg, (dir / "agent.toml").string(), plat);
        agent->set_sink_for_tests(&sink);
        REQUIRE(agent->apply_signatures({{"version", "t"}, {"bundle", test::sample_signatures().dump()}}, false));
    }
    ~H() { agent.reset(); fs::remove_all(dir); }
    std::vector<Event> of(const std::string& t) {
        std::vector<Event> o;
        for (const auto& e : sink.events) if (e.type == t) o.push_back(e);
        return o;
    }
    void msg(const std::string& l) { agent->on_helper_message(peer, l); }
    // last broadcast line whose "t" == type
    json last_bcast(const std::string& type) {
        json out;
        std::lock_guard lk(ipc->mu);
        for (const auto& b : ipc->broadcasts) {
            auto j = json::parse(b, nullptr, false);
            if (j.is_object() && j.value("t", "") == type) out = j;
        }
        return out;
    }
};
Policy window_policy(TitleCapture tc) {
    auto p = Policy::default_for(Mode::practice);
    p.collect.window = true;
    p.title_capture = tc;
    return p;
}
}  // namespace

// ---------------------------------------------------------------- F038
TEST_CASE("F038 ping and flush are acked ok with a pong timestamp; unknown commands are ignored") {
    H h;
    h.agent->apply_policy(Policy::default_for(Mode::practice));
    h.agent->handle_command({"p1", "ping", json::object()});
    h.agent->handle_command({"f1", "flush", json::object()});
    h.agent->handle_command({"x1", "no_such_command", json::object()});
    auto r = h.of(ev::command_result);
    REQUIRE(r.size() == 3);
    CHECK(r[0].data["command_id"] == "p1");
    CHECK(r[0].data["status"] == "ok");
    CHECK(r[0].data["detail"]["pong"].is_number());
    CHECK(r[1].data["status"] == "ok");
    CHECK(r[2].data["status"] == "ignored");
}

// ---------------------------------------------------------------- F075
TEST_CASE("F075 window titles follow title_capture: off=empty, app_only=app name, full=title cut at 256") {
    H h;
    const std::string line = R"({"t":"window","app":"/usr/bin/code","title":"main.cpp - lab3"})";
    h.agent->apply_policy(window_policy(TitleCapture::off));
    h.msg(line);
    h.agent->apply_policy(window_policy(TitleCapture::app_only));
    h.msg(line);
    h.agent->apply_policy(window_policy(TitleCapture::full));
    h.msg(line);
    h.msg(json({{"t", "window"}, {"app", "code"}, {"title", std::string(400, 'a')}}).dump());
    auto w = h.of(ev::window_focus);
    REQUIRE(w.size() == 4);
    CHECK(w[0].data["title"] == "");
    CHECK(w[1].data["title"] == "code");
    CHECK(w[2].data["title"] == "main.cpp - lab3");
    CHECK(w[2].data["app"] == "code");
    CHECK(w[2].data["user"] == "alice");
    CHECK(w[3].data["title"].get<std::string>().size() == 256);
}

TEST_CASE("F075 window events are dropped when collect.window is off, and oversize or bad lines are ignored") {
    H h;
    auto p = window_policy(TitleCapture::full);
    p.collect.window = false;
    h.agent->apply_policy(p);
    h.msg(R"({"t":"window","app":"code","title":"x"})");
    CHECK(h.of(ev::window_focus).empty());
    p.collect.window = true;
    h.agent->apply_policy(p);
    h.msg(std::string(20000, 'x'));
    h.msg("[1,2]");
    h.msg(R"({"t":"unknown"})");
    CHECK(h.of(ev::window_focus).empty());
}

// ---------------------------------------------------------------- F076
TEST_CASE("F076 AI browser tab titles map to a site name and raise web.ai_tab; plain titles do not") {
    H h;
    h.agent->apply_policy(window_policy(TitleCapture::app_only));
    const std::vector<std::pair<std::string, std::string>> cases = {
        {"ChatGPT - Google Chrome", "ChatGPT"},    {"Claude - Mozilla Firefox", "Claude AI"},
        {"Google Gemini", "Google Gemini"},        {"DeepSeek Chat", "DeepSeek"},
        {"Perplexity", "Perplexity AI"},           {"Grok", "Grok (xAI)"},
        {"NotebookLM", "NotebookLM"},              {"Poe - Fast AI", "Poe AI"},
    };
    for (const auto& [title, site] : cases) {
        h.sink.events.clear();
        h.msg(json({{"t", "window"}, {"app", "chrome.exe"}, {"title", title}}).dump());
        auto w = h.of(ev::window_focus);
        REQUIRE(w.size() == 1);
        INFO(title);
        CHECK(w[0].data["ai_site"] == site);
        CHECK(h.of(ev::rule_hit).size() == 1);
    }
    h.sink.events.clear();
    h.msg(R"({"t":"window","app":"chrome.exe","title":"Weather today - Google Chrome"})");
    REQUIRE(h.of(ev::window_focus).size() == 1);
    CHECK_FALSE(h.of(ev::window_focus)[0].data.contains("ai_site"));
    CHECK(h.of(ev::rule_hit).empty());
}

TEST_CASE("F076 titles that only mention 'claude' or 'grok' are not AI tabs, and non-browsers are ignored (I-AGENT-18 fixed)") {
    H h;
    h.agent->apply_policy(window_policy(TitleCapture::app_only));
    h.msg(R"({"t":"window","app":"chrome.exe","title":"Claude Debussy - Wikipedia"})");
    h.msg(R"({"t":"window","app":"chrome.exe","title":"Grokking algorithms - Google Chrome"})");
    h.msg(R"({"t":"window","app":"code","title":"claude_notes.py - Visual Studio Code"})");
    REQUIRE(h.of(ev::window_focus).size() == 3);
    for (const auto& e : h.of(ev::window_focus)) CHECK_FALSE(e.data.contains("ai_site"));
    CHECK(h.of(ev::rule_hit).empty());
}

TEST_CASE("F076 a repeated AI tab is reported once (dedup) while focus events keep flowing") {
    H h;
    h.agent->apply_policy(window_policy(TitleCapture::app_only));
    for (int i = 0; i < 3; ++i) h.msg(R"({"t":"window","app":"chrome.exe","title":"ChatGPT"})");
    CHECK(h.of(ev::window_focus).size() == 3);
    CHECK(h.of(ev::rule_hit).size() == 1);
}

// ---------------------------------------------------------------- F078
TEST_CASE("F078 helper key stats are forwarded only when collect.keys is on, without the message tag") {
    H h;
    const std::string k = R"({"t":"keys","keys":40,"backspaces":4,"iki_mean_ms":110.5})";
    auto p = Policy::default_for(Mode::practice);
    p.collect.keys = false;
    h.agent->apply_policy(p);
    h.msg(k);
    p.collect.keys = true;
    h.agent->apply_policy(p);
    h.msg(k);
    auto ks = h.of(ev::key_stats);
    REQUIRE(ks.size() == 1);
    CHECK(ks[0].data["keys"] == 40);
    CHECK(ks[0].data["user"] == "alice");
    CHECK(ks[0].data["source"] == "helper");
    CHECK_FALSE(ks[0].data.contains("t"));
}

// ---------------------------------------------------------------- F081
TEST_CASE("F081 local model files: .gguf always flagged, .safetensors only above min_size_mb") {
    const fs::path base = fs::temp_directory_path() / ("xg-f81-" + random_hex(4));
    fs::create_directories(base / "models");
    write_file_atomic((base / "models/tiny.gguf").string(), "w");
    write_file_atomic((base / "models/small.safetensors").string(), "w");
    write_file_atomic((base / "models/big.safetensors").string(), "w");
    fs::resize_file(base / "models/big.safetensors", 600ull * 1024 * 1024);  // sparse
    json sig = test::sample_signatures();
    sig["artifacts"]["scan"] = {{"roots", {base.string()}}, {"max_depth", 3}, {"max_files", 100}, {"skip_dirs", json::array()}};
    RuleEngine re(SignatureSet::from_json(sig));
    std::atomic<bool> stop{false};
    auto res = scan_artifacts(re, {base.string()}, stop);
    std::set<std::string> found;
    for (auto& a : res.found) found.insert(fs::path(a.path).filename().string() + "=" + a.rule_id);
    CHECK(found.contains("tiny.gguf=file.gguf"));
    CHECK(found.contains("big.safetensors=file.safetensors"));
    CHECK_FALSE(found.contains("small.safetensors=file.safetensors"));
    CHECK_FALSE(res.truncated);
    fs::remove_all(base);
}

// ---------------------------------------------------------------- F083
TEST_CASE("F083 USB devices: storage always logged, phones raise a hit, others skipped, serial only hashed") {
    H h;
    h.agent->apply_policy(Policy::default_for(Mode::practice));
    h.devs->list = {
        {"add", "block", "usb_storage", "0781", "5581", "Cruzer", "abcd1234"},
        {"add", "usb", "phone", "18d1", "4ee1", "Pixel", "ffff0000"},
        {"add", "usb", "keyboard", "046d", "c31c", "Keyboard", "1111"},
    };
    h.agent->scan_devices();
    auto d = h.of(ev::device);
    REQUIRE(d.size() == 2);
    CHECK(d[0].data["class"] == "usb_storage");
    CHECK(d[0].data["rule_id"] == "dev.usb_storage");
    CHECK(d[0].data["serial_hash"] == "abcd1234");
    CHECK(d[0].data.contains("vendor"));
    CHECK_FALSE(d[0].data.contains("serial"));
    CHECK(d[1].data["rule_id"] == "dev.phone");
    CHECK(h.of(ev::rule_hit).size() >= 1);

    h.sink.events.clear();
    auto p = Policy::default_for(Mode::practice);
    p.collect.devices = false;
    h.agent->apply_policy(p);
    h.agent->scan_devices();
    CHECK(h.of(ev::device).empty());
}

// ---------------------------------------------------------------- F089
TEST_CASE("F089 isolate_notice blocks Internet via the isolator, keeps the CCC reachable and reports both ways") {
    H h;
    h.agent->apply_policy(Policy::default_for(Mode::practice));
    h.agent->handle_command({"i1", "isolate_notice", {{"isolated", true}}});
    CHECK(h.iso->exempt == std::vector<std::string>{"10.0.0.5"});
    auto na = h.of(ev::net_adapter);
    REQUIRE(na.size() == 1);
    CHECK(na[0].data["change"] == "isolated");
    CHECK(na[0].data["kind"] == "net.isolated");
    CHECK(h.last_bcast("indicator")["text"].get<std::string>().find("network isolated") != std::string::npos);
    CHECK(h.last_bcast("notify")["urgent"] == true);

    h.agent->handle_command({"i2", "isolate_notice", {{"isolated", false}}});
    na = h.of(ev::net_adapter);
    REQUIRE(na.size() == 2);
    CHECK(na[1].data["change"] == "restored");
    CHECK(h.last_bcast("indicator")["text"].get<std::string>().find("network isolated") == std::string::npos);
    auto r = h.of(ev::command_result);
    CHECK(r.back().data["detail"]["isolated"] == false);
}

TEST_CASE("F089 a missing isolate flag defaults to restore") {
    H h;
    h.agent->apply_policy(Policy::default_for(Mode::practice));
    h.agent->handle_command({"i1", "isolate_notice", {{"isolated", true}}});
    h.agent->handle_command({"i2", "isolate_notice", json::object()});
    CHECK_FALSE(h.agent->isolated());
    CHECK(h.iso->restore_calls == 1);
}

// ---------------------------------------------------------------- F093
TEST_CASE("F093 message command notifies helpers with default title and urgent flag") {
    H h;
    h.agent->apply_policy(Policy::default_for(Mode::practice));
    h.agent->handle_command({"m1", "message", {{"text", "Time is up"}}});
    auto n = h.last_bcast("notify");
    CHECK(n["title"] == "Message from your instructor");
    CHECK(n["body"] == "Time is up");
    CHECK(n["urgent"] == false);
    h.agent->handle_command({"m2", "message", {{"text", "Stop"}, {"title", "Exam"}, {"urgent", true}}});
    n = h.last_bcast("notify");
    CHECK(n["title"] == "Exam");
    CHECK(n["urgent"] == true);
    CHECK(h.of(ev::command_result).back().data["detail"]["helpers"] == 0);
}

// ---------------------------------------------------------------- F094 / F096 / F097
TEST_CASE("F094 helper hello triggers an indicator broadcast whose text follows the mode and carries the notice") {
    H h;
    for (auto [mode, label] : std::vector<std::pair<Mode, std::string>>{
             {Mode::always_on, "Always-on"}, {Mode::practice, "Practice"}, {Mode::test, "Test"}}) {
        auto p = Policy::default_for(mode);
        p.notice_text = "Lab rules apply";
        h.agent->apply_policy(p);
        h.msg(R"({"t":"hello","v":1,"session":"x11","display":":0"})");
        auto ind = h.last_bcast("indicator");
        INFO(label);
        CHECK(ind["mode"] == to_string(mode));
        CHECK(ind["text"].get<std::string>().starts_with("Monitored by XephyGuard"));
        CHECK(ind["notice"] == "Lab rules apply");
    }
}

TEST_CASE("F096 hello is recorded as helper_status and ping gets a pong to the same connection") {
    H h;
    h.agent->apply_policy(Policy::default_for(Mode::practice));
    h.msg(R"({"t":"hello","v":1,"session":"x11","display":":0","caps":{"window":true}})");
    auto hs = h.of(ev::helper_status);
    REQUIRE(hs.size() == 1);
    CHECK(hs[0].data["connected"] == true);
    CHECK(hs[0].data["user"] == "alice");
    CHECK(hs[0].data["caps"]["window"] == true);
    CHECK(hs[0].data["helper_version"] == 1);
    const auto before = h.ipc->count_containing("pong");
    h.msg(R"({"t":"ping"})");
    CHECK(h.ipc->count_containing("pong") == before + 1);
}

TEST_CASE("F097 helper config carries the PAC URL only in proxy_only mode, plus mode and collect flags") {
    H h;
    auto p = Policy::default_for(Mode::test);
    p.proxy_mode = "proxy_only";
    p.proxy_pac_url = "https://10.0.0.5/proxy.pac";  // F97: https on the CCC host (10.0.0.5) only
    h.agent->apply_policy(p);
    h.msg(R"({"t":"hello","v":1})");
    auto c = h.last_bcast("config");
    CHECK(c["proxy_pac_url"] == "https://10.0.0.5/proxy.pac");
    CHECK(c["mode"] == "test");
    CHECK(c["collect"].contains("window"));
    CHECK(c["t"] == "config");
    p.proxy_mode = "gateway";
    h.agent->apply_policy(p);
    h.msg(R"({"t":"hello","v":1})");
    CHECK(h.last_bcast("config")["proxy_pac_url"] == "");
}
