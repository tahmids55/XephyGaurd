// xephyguard-agent: the privileged service (root / LocalSystem) plus admin subcommands.
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "xg/agent.hpp"
#include "xg/log.hpp"
#include "xg/util.hpp"

using namespace xg;

namespace {

void usage() {
    std::cout << "XephyGuard agent " << XG_VERSION << "\n\n"
              << "Usage:\n"
              << "  xephyguard-agent run      [--config PATH] [--foreground]   run the service (default)\n"
              << "  xephyguard-agent enroll   --key KEY [--ccc HOST] [--ca-fingerprint SHA256]\n"
              << "                            [--no-wait] [--timeout SECONDS] [--trust-on-first-use]\n"
              << "  xephyguard-agent status   [--config PATH]                 show enrollment and queue state\n"
              << "  xephyguard-agent scan     [--config PATH]                 one-shot local scan, print findings\n"
              << "  xephyguard-agent version\n";
}

std::string arg_value(std::vector<std::string>& args, const std::string& name, const std::string& def = {}) {
    for (size_t i = 0; i + 1 < args.size(); ++i)
        if (args[i] == name) {
            auto v = args[i + 1];
            args.erase(args.begin() + long(i), args.begin() + long(i) + 2);
            return v;
        }
    return def;
}

bool arg_flag(std::vector<std::string>& args, const std::string& name) {
    for (size_t i = 0; i < args.size(); ++i)
        if (args[i] == name) {
            args.erase(args.begin() + long(i));
            return true;
        }
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    std::string cmd = "run";
    if (!args.empty() && args[0].rfind("--", 0) != 0) {
        cmd = args[0];
        args.erase(args.begin());
    }
    if (cmd == "version" || cmd == "--version") {
        std::cout << XG_VERSION << "\n";
        return 0;
    }
    if (cmd == "help" || cmd == "--help" || cmd == "-h") {
        usage();
        return 0;
    }
    const std::string config_path = arg_value(args, "--config", default_config_path());
    const bool foreground = arg_flag(args, "--foreground");
    bool loaded = false;
    std::string err;
    Config cfg = std::filesystem::exists(config_path) ? Config::load(config_path, &loaded, &err) : Config::defaults();
    if (!err.empty()) std::cerr << "warning: " << err << "\n";

    if (cmd == "enroll") {
        log::init("", log::Level::warn, true);
        EnrollOptions o;
        o.key = arg_value(args, "--key");
        o.ccc_host = arg_value(args, "--ccc");
        o.ca_fingerprint = arg_value(args, "--ca-fingerprint");
        o.wait = !arg_flag(args, "--no-wait");
        o.wait_timeout_s = std::stoi(arg_value(args, "--timeout", "0"));
        o.trust_on_first_use = arg_flag(args, "--trust-on-first-use");
        auto plat = make_service_platform(cfg);
        return enroll(cfg, config_path, o, *plat->system);
    }

    if (cmd == "status") {
        std::cout << "config:      " << config_path << (loaded ? "" : " (not found, defaults)") << "\n"
                  << "ccc:         " << (cfg.ccc_host.empty() ? "(auto-discovery)" : cfg.ccc_host) << ":" << cfg.api_port << "\n"
                  << "enrolled:    " << (cfg.enrolled() ? "yes, pc #" + cfg.pc_id : (cfg.enrollment_id.empty() ? "no" : "pending (request " + cfg.enrollment_id + ")")) << "\n";
        EventQueue q((std::filesystem::path(cfg.data_dir) / "queue.db").string());
        std::cout << "queue:       " << q.size() << " events buffered, last seq " << q.last_seq() << "\n";
        if (auto p = q.kv_get("policy")) {
            auto j = json::parse(*p, nullptr, false);
            if (!j.is_discarded())
                std::cout << "policy:      v" << j.value("version", 0) << " mode=" << j.value("mode", "?") << " profile='"
                          << j.value("profile", "") << "'\n";
        }
        return 0;
    }

    if (cmd == "scan") {
        log::init("", log::Level::warn, true);
        cfg.data_dir = (std::filesystem::temp_directory_path() / "xephyguard-scan").string();
        auto plat = make_service_platform(cfg);
        plat->ipc.reset();
        Agent agent(cfg, config_path, *plat);
        VectorSink sink;
        agent.set_sink_for_tests(&sink);
        agent.apply_policy(Policy::default_for(Mode::test));
        agent.scan_processes();
        agent.scan_ports();
        agent.scan_network();
        agent.scan_extensions();
        for (const auto& e : sink.events)
            if (e.type == ev::rule_hit) std::cout << e.data.dump() << "\n";
        std::cout << sink.events.size() << " events, signatures " << agent.rules().version() << "\n";
        return 0;
    }

    if (cmd != "run") {
        usage();
        return 2;
    }
    log::init(foreground ? "" : (std::filesystem::path(cfg.log_dir) / "agent.log").string(), log::parse_level(cfg.log_level),
              foreground);
    if (cfg.ccc_host.empty()) {
        log::info("no CCC configured, trying auto-discovery");
        // Resolved lazily by `enroll`; a running agent without a CCC only logs locally.
    }
    auto plat = make_service_platform(cfg);
    return plat->service_host->run([&](std::atomic<bool>& stop) {
        Agent agent(cfg, config_path, *plat);
        return agent.run(stop);
    });
}
