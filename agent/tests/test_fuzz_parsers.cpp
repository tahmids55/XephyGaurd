// Fuzz-style table tests (plan step 15): every parser of external input gets valid seeds, deterministic
// byte/JSON mutations and type swaps, and must neither throw nor hang. No libFuzzer; Catch2 generators only.
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>
#include <random>
#include <string>
#include <vector>

#include "xg/discovery.hpp"
#include "xg/http.hpp"
#include "xg/policy.hpp"
#include "xg/protocol.hpp"
#include "xg/signatures.hpp"

using namespace xg;

namespace {
std::string mutate(std::string s, std::mt19937& rng) {
    static const std::string junk[] = {"\r\n", "\0", "ffffffffffffffff", "-1", "99999999999999999999", "{", "}", "\"",
                                       "[", "null", "\xff\xfe", ":", ";", " ", "0x10"};
    std::uniform_int_distribution<int> op(0, 3);
    const int edits = 1 + int(rng() % 4);
    for (int e = 0; e < edits && !s.empty(); ++e) {
        const size_t pos = rng() % s.size();
        switch (op(rng)) {
            case 0: s.erase(pos, 1 + rng() % 8); break;
            case 1: s.insert(pos, junk[rng() % std::size(junk)]); break;
            case 2: s[pos] = char(rng() % 256); break;
            default: s = s.substr(0, pos); break;
        }
    }
    return s;
}

// Replaces one member of a JSON object (recursively) with a value of another type.
void swap_types(json& j, std::mt19937& rng) {
    static const json values[] = {json(nullptr), json(true), json(-1), json(1e300), json("x"), json::array(),
                                  json::object(), json(18446744073709551615ULL), json(std::string(5000, 'a'))};
    if (j.is_object() && !j.empty()) {
        auto it = j.begin();
        std::advance(it, long(rng() % j.size()));
        if ((it->is_object() || it->is_array()) && rng() % 2) swap_types(*it, rng);
        else *it = values[rng() % std::size(values)];
    } else if (j.is_array() && !j.empty()) {
        auto& el = j[rng() % j.size()];
        if ((el.is_object() || el.is_array()) && rng() % 2) swap_types(el, rng);
        else el = values[rng() % std::size(values)];
    }
}
}  // namespace

TEST_CASE("fuzz: HTTP response parser") {
    const auto seed = GENERATE(Catch::Generators::range(1u, 9u));
    std::mt19937 rng(seed);
    const std::vector<std::string> seeds = {
        "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 2\r\n\r\n{}",
        "HTTP/1.1 201 Created\r\nTransfer-Encoding: chunked\r\n\r\n4\r\nWiki\r\n5\r\npedia\r\n0\r\n\r\n",
        "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n\r\n",
        "HTTP/1.1 204 No Content\r\n\r\n"};
    for (int i = 0; i < 500; ++i) {
        const std::string in = mutate(seeds[rng() % seeds.size()], rng);
        net::HttpResponse r;
        size_t used = 0;
        bool bad = false;
        REQUIRE_NOTHROW(net::parse_http_response(in, r, &used, &bad));
        CHECK(used <= in.size());
    }
}

TEST_CASE("fuzz: batch ack, command, discovery reply") {
    const auto seed = GENERATE(Catch::Generators::range(1u, 9u));
    std::mt19937 rng(seed);
    const json ack = {{"ack_seq", 3}, {"server_time_ms", 5000}, {"policy_version", 9}, {"signatures_version", "v"},
                      {"isolated", true}};
    const json cmd = {{"id", "c1"}, {"type", "kill"}, {"args", {{"pid", 5}, {"exe", "ollama"}}}};
    const json disc = {{"service", "xephyguard-ccc"}, {"host", "10.0.0.2"}, {"api_port", 8443}, {"lab", "L3"}};
    for (int i = 0; i < 300; ++i) {
        json a = ack, c = cmd, d = disc;
        swap_types(a, rng);
        swap_types(c, rng);
        swap_types(d, rng);
        REQUIRE_NOTHROW(protocol::parse_batch_ack(a.dump()));
        REQUIRE_NOTHROW(protocol::parse_batch_ack(mutate(ack.dump(), rng)));
        REQUIRE_NOTHROW(protocol::parse_command(c));
        REQUIRE_NOTHROW(net::parse_discovery_reply(d.dump(), "10.0.0.9"));
        REQUIRE_NOTHROW(net::parse_discovery_reply(mutate(disc.dump(), rng), "10.0.0.9"));
    }
}

TEST_CASE("fuzz: policy and signature bundles from the CCC") {
    const auto seed = GENERATE(Catch::Generators::range(1u, 5u));
    std::mt19937 rng(seed);
    const json pol = Policy::default_for(Mode::test).to_json();
    const json sig = json::parse(R"({"version":"v1",
        "processes":[{"id":"p","severity":"high","description":"d","match":{"exe":["ollama"]}}],
        "extensions":[{"id":"e","ide":"vscode","ext_ids":["a.b"]}],
        "ports":{"rules":[{"id":"port","port":11434}],"probe":{"enabled":true,"paths":["/v1/models"]}},
        "artifacts":{"rules":[{"id":"f","globs":["*.gguf"]}],"scan":{"roots":["~"],"max_depth":3}},
        "adapters":[{"id":"a","kind":"vpn"}],"devices":[{"id":"d","classes":["usb_storage"]}],
        "domains":{"ai":["chatgpt.com"],"ide":["githubcopilot.com"]}})");
    for (int i = 0; i < 300; ++i) {
        json p = pol, s = sig;
        swap_types(p, rng);
        swap_types(s, rng);
        std::string err;
        REQUIRE_NOTHROW(Policy::from_json(p, &err));
        REQUIRE_NOTHROW(SignatureSet::from_json(s, &err));
    }
}
