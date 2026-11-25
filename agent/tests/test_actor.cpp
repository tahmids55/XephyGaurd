// Actor-core primitives and message flow (docs/DECISIONS.md "agent actor core").
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <thread>

#include "xg/mailbox.hpp"

using namespace xg;
using namespace std::chrono_literals;

TEST_CASE("mailbox keeps FIFO order across producers and drops when full") {
    Mailbox<int> mb(1000);
    std::vector<std::thread> producers;
    for (int t = 0; t < 4; ++t)
        producers.emplace_back([&mb, t] {
            for (int i = 0; i < 200; ++i) mb.post(t * 1000 + i);
        });
    for (auto& p : producers) p.join();
    auto all = mb.take_all();
    REQUIRE(all.size() == 800);
    // Per producer, messages arrive in the order they were posted.
    for (int t = 0; t < 4; ++t) {
        int last = -1;
        for (int v : all)
            if (v / 1000 == t) {
                CHECK(v % 1000 > last);
                last = v % 1000;
            }
    }
    Mailbox<int> small(2);
    CHECK(small.post(1));
    CHECK(small.post(2));
    CHECK_FALSE(small.post(3));
    CHECK(small.dropped() == 1);
    small.close();
    CHECK_FALSE(small.post(4));
    CHECK(small.take_all().size() == 2);
}

TEST_CASE("mailbox wait returns on a message, on wake, or on timeout") {
    Mailbox<std::string> mb;
    auto t0 = std::chrono::steady_clock::now();
    CHECK(mb.wait_take_all(30ms).empty());
    CHECK(std::chrono::steady_clock::now() - t0 >= 25ms);
    std::thread th([&] {
        std::this_thread::sleep_for(20ms);
        mb.post("hi");
    });
    auto got = mb.wait_take_all(5s);
    th.join();
    REQUIRE(got.size() == 1);
    CHECK(got[0] == "hi");
    std::thread w([&] {
        std::this_thread::sleep_for(20ms);
        mb.wake();
    });
    t0 = std::chrono::steady_clock::now();
    CHECK(mb.wait_take_all(5s).empty());
    w.join();
    CHECK(std::chrono::steady_clock::now() - t0 < 4s);
}

TEST_CASE("snapshot readers keep their copy while a writer publishes") {
    Snapshot<std::vector<int>> s;
    CHECK(s.get()->empty());
    s.set({1, 2, 3});
    auto held = s.get();
    s.set({4});
    CHECK(held->size() == 3);
    CHECK(s.get()->front() == 4);
}

// ------------------------------------------------------------------ agent core fed by messages
#include <filesystem>

#include "fakes.hpp"
#include "xg/agent.hpp"
#include "xg/util.hpp"

namespace fs = std::filesystem;

namespace {
struct Core {
    fs::path dir = fs::temp_directory_path() / ("xgc-" + random_hex(4));
    ServicePlatform plat;
    test::FakeProcesses* procs;
    test::FakeIsolator* iso;
    test::FakeIpc* ipc;
    VectorSink sink;
    std::unique_ptr<Agent> agent;
    Core() {
        fs::create_directories(dir);
        auto p = std::make_unique<test::FakeProcesses>(); procs = p.get(); plat.processes = std::move(p);
        auto i = std::make_unique<test::FakeIsolator>(); iso = i.get(); plat.isolator = std::move(i);
        auto c = std::make_unique<test::FakeIpc>(); ipc = c.get(); plat.ipc = std::move(c);
        plat.network = std::make_unique<test::FakeNetwork>();
        plat.system = std::make_unique<test::FakeSystem>();
        Config cfg = Config::defaults();
        cfg.data_dir = dir.string();
        cfg.ccc_host = "10.0.0.5";
        agent = std::make_unique<Agent>(cfg, (dir / "agent.toml").string(), plat);
        agent->set_sink_for_tests(&sink);
        REQUIRE(agent->apply_signatures({{"version", "t"}, {"bundle", test::sample_signatures().dump()}}, false));
    }
    ~Core() { agent.reset(); fs::remove_all(dir); }
    size_t count(const std::string& t) {
        size_t n = 0;
        for (const auto& e : sink.events) n += e.type == t;
        return n;
    }
};
}  // namespace

TEST_CASE("core applies posted messages only when it drains them") {
    Core c;
    c.agent->apply_policy(Policy::default_for(Mode::test));
    // Process exec from the watcher thread.
    REQUIRE(c.agent->post(msg::ProcessExec{{42, 1, "/usr/bin/ollama", "ollama", "", "alice", 0, 2}}));
    CHECK(c.count(ev::process_start) == 0);
    c.agent->drain();
    CHECK(c.count(ev::process_start) == 1);
    CHECK(c.procs->killed == std::vector<int64_t>{42});
    // The same pid again is not "fresh".
    c.agent->post(msg::ProcessExec{{42, 1, "/usr/bin/ollama", "ollama", "", "alice", 0, 2}});
    c.agent->drain();
    CHECK(c.count(ev::process_start) == 1);

    // Command from the realtime thread runs on the core, in order.
    c.agent->post(msg::CommandMsg{{"c1", "message", {{"text", "hello class"}}}});
    c.agent->post(msg::CommandMsg{{"c2", "ping", json::object()}});
    CHECK(c.ipc->count_containing("hello class") == 0);
    c.agent->drain();
    CHECK(c.ipc->count_containing("hello class") == 1);
    REQUIRE(c.count(ev::command_result) == 2);
    CHECK(c.sink.events.back().data["command_id"] == "c2");
}

TEST_CASE("core handles uplink messages: ack isolation, policy, enrollment") {
    Core c;
    protocol::BatchAck ack;
    ack.ack_seq = 5;
    ack.isolated = true;
    c.agent->post(msg::AckReceived{ack, 1000, 1100});
    c.agent->drain();
    CHECK(c.agent->isolated());
    CHECK(c.iso->on);

    auto pol = Policy::default_for(Mode::practice);
    pol.version = 9;
    pol.intervals.heartbeat_s = 15;
    c.agent->post(msg::PolicyFetched{{{"policy", pol.to_json()}, {"realtime", {{"app_key", "k"}}}, {"screen", {{"port", 70000}}}}});
    c.agent->drain();
    CHECK(c.agent->policy().mode == Mode::practice);
    CHECK(c.agent->policy().version == 9);
    auto v = c.agent->view();
    CHECK(v->heartbeat_s == 15);
    CHECK(v->realtime["app_key"] == "k");
    CHECK(v->heartbeat["policy_version"] == 9);

    CHECK_FALSE(v->enrolled);
    c.agent->post(msg::EnrollApproved{"-----BEGIN CERTIFICATE-----\n", "", "tok", 12});
    c.agent->drain();
    v = c.agent->view();
    CHECK(v->enrolled);
    CHECK(v->pc_id == "12");
    CHECK(v->token == "tok");
}

TEST_CASE("helper connections are tracked by the core") {
    Core c;
    IIpcServer::Peer peer{7, 1000, 555, "alice"};
    c.agent->post(msg::HelperConn{peer, true});
    c.agent->post(msg::HelperLine{peer, R"({"t":"ping"})"});
    c.agent->drain();
    CHECK(c.agent->build_heartbeat()["helpers"] == 1);
    CHECK(c.ipc->count_containing("pong") == 1);
    c.agent->post(msg::HelperConn{peer, false});
    c.agent->drain();
    CHECK(c.agent->build_heartbeat()["helpers"] == 0);
}

TEST_CASE("agent run loop serves messages from many threads and stops cleanly") {
    Core c;
    c.agent->apply_policy(Policy::default_for(Mode::test));
    std::atomic<bool> stop{false};
    std::thread core([&] { c.agent->run(stop); });
    std::vector<std::thread> producers;
    for (int t = 0; t < 4; ++t)
        producers.emplace_back([&, t] {
            for (int i = 0; i < 200; ++i) {
                c.agent->post(msg::ProcessExec{{1000 + t * 1000 + i, 1, "/usr/bin/true", "true", "", "alice", 0, 1}});
                c.agent->post(msg::KeyEvent{now_ms(), i % 7 == 0, false});
                c.agent->post(msg::NetworkChanged{});
            }
            c.agent->post(msg::CommandMsg{{"p" + std::to_string(t), "ping", json::object()}});
        });
    for (auto& p : producers) p.join();
    // Wait until the four pings are acknowledged.
    for (int i = 0; i < 200; ++i) {
        size_t n = 0;
        {
            std::lock_guard lk(c.sink.mu);
            for (const auto& e : c.sink.events) n += e.type == ev::command_result;
        }
        if (n >= 4) break;
        std::this_thread::sleep_for(10ms);
    }
    stop = true;
    core.join();
    CHECK(c.count(ev::command_result) == 4);
    CHECK(c.count(ev::agent_stopping) == 1);
}

TEST_CASE("helper disconnect is tamper only if the session stays without a helper (I-AGENT-17)") {
    Core c;
    c.agent->set_helper_grace_for_tests(30);
    auto tampers = [&] {
        size_t n = 0;
        for (const auto& e : c.sink.events) n += e.type == ev::tamper && e.data["kind"] == "helper_terminated";
        return n;
    };
    IIpcServer::Peer peer{7, 1000, 555, "alice"};  // FakeSystem: alice uid 1000, x11, active
    c.agent->post(msg::HelperConn{peer, true});
    c.agent->post(msg::HelperConn{peer, false});
    c.agent->drain();
    CHECK(tampers() == 0);  // not immediately
    // Helper comes back within the grace period: nothing reported.
    c.agent->post(msg::HelperConn{IIpcServer::Peer{8, 1000, 556, "alice"}, true});
    c.agent->drain();
    std::this_thread::sleep_for(50ms);
    c.agent->check_helpers();
    CHECK(tampers() == 0);
    // Killed and not back while the session is still active: reported after the grace period.
    c.agent->post(msg::HelperConn{IIpcServer::Peer{8, 1000, 556, "alice"}, false});
    c.agent->drain();
    c.agent->check_helpers();
    CHECK(tampers() == 0);
    std::this_thread::sleep_for(50ms);
    c.agent->check_helpers();
    CHECK(tampers() == 1);
}

#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

TEST_CASE("LLM port probes run on the probe worker and report back to the core (F74)") {
    // Local server that answers /v1/models like an OpenAI-compatible API.
    int lfd = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    REQUIRE(::bind(lfd, reinterpret_cast<sockaddr*>(&a), sizeof a) == 0);
    ::listen(lfd, 4);
    socklen_t al = sizeof a;
    ::getsockname(lfd, reinterpret_cast<sockaddr*>(&a), &al);
    const uint16_t port = ntohs(a.sin_port);
    std::thread srv([lfd] {
        int c = ::accept(lfd, nullptr, nullptr);
        if (c < 0) return;
        char b[1024];
        (void)::recv(c, b, sizeof b, 0);
        const char r[] = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 11\r\n\r\n{\"data\":[]}";
        ::send(c, r, sizeof r - 1, MSG_NOSIGNAL);
        ::close(c);
    });

    Core c;
    json sig = test::sample_signatures();
    sig["ports"]["probe"] = {{"enabled", true}, {"paths", {"/v1/models"}}, {"severity", "high"}, {"min_rss_mb", 1}};
    REQUIRE(c.agent->apply_signatures({{"version", "t2"}, {"bundle", sig.dump()}}, false));
    c.agent->apply_policy(Policy::default_for(Mode::test));
    auto* net = static_cast<test::FakeNetwork*>(c.plat.network.get());
    net->socks = {{port, "127.0.0.1", true, 4242, "llama-server-renamed", 512ull * 1024 * 1024}};

    c.agent->scan_ports();  // returns without waiting for the probe
    bool seen = false;
    for (int i = 0; i < 300 && !seen; ++i) {
        std::this_thread::sleep_for(10ms);
        c.agent->drain();
        for (const auto& e : c.sink.events)
            seen = seen || (e.type == ev::listening_port && e.data["probed"] == true &&
                            e.data["rule_id"] == "port.generic_llm_api");
    }
    srv.join();
    ::close(lfd);
    CHECK(seen);
}
#endif
