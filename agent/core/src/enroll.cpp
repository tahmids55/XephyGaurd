#include <filesystem>
#include <iostream>
#include <thread>

#include "xg/agent.hpp"
#include "xg/crypto.hpp"
#include "xg/discovery.hpp"
#include "xg/http.hpp"
#include "xg/log.hpp"
#include "xg/util.hpp"

namespace xg {

namespace fs = std::filesystem;

/// Stable per-installation identity, generated on first enrollment and persisted in data_dir.
/// The server uses it (not hostname/MAC) to tell machines apart, so cloned lab images with
/// identical hostnames or virtual-adapter MACs enroll as separate PCs.
static std::string load_or_create_machine_uid(const std::string& data_dir) {
    const std::string path = (fs::path(data_dir) / "machine_id").string();
    auto valid = [](const std::string& v) {
        if (v.size() != 32) return false;
        for (char c : v)
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
        return true;
    };
    bool ok = false;
    std::string cur = read_file(path, &ok);
    while (!cur.empty() && (cur.back() == '\n' || cur.back() == '\r' || cur.back() == ' ')) cur.pop_back();
    cur = to_lower(cur);
    if (ok && valid(cur)) return cur;
    std::string fresh = random_hex(16);
    write_file_atomic(path, fresh + "\n");
    return fresh;
}

int enroll(Config& cfg, const std::string& config_path, const EnrollOptions& o, ISystemInfo& sys) {
    if (o.key.empty()) {
        std::cerr << "enroll: --key is required (shown on the CCC 'Add a PC' page)\n";
        return 2;
    }
    // 1. Locate the CCC.
    std::string fp = to_lower(o.ca_fingerprint);
    if (!o.ccc_host.empty()) cfg.ccc_host = o.ccc_host;
    if (cfg.ccc_host.empty()) {
        std::cout << "Looking for the CCC on the lab network (UDP " << cfg.discovery_port << ")...\n";
        auto d = net::discover_ccc(cfg.discovery_port, 6000);
        if (!d) {
            std::cerr << "enroll: no CCC answered. Pass --ccc <address>.\n";
            return 3;
        }
        std::cout << "Found CCC '" << d->lab_name << "' at " << d->host << "\n";
        cfg.ccc_host = d->host;
        cfg.api_port = d->api_port;
        if (fp.empty()) fp = d->ca_fingerprint;
    }
    std::error_code ec;
    fs::create_directories(fs::path(cfg.ca_file).parent_path(), ec);
    fs::create_directories(cfg.data_dir, ec);

    // 2. Obtain and pin the CA certificate.
    bool have_ca = false;
    std::string ca = read_file(cfg.ca_file, &have_ca);
    if (!have_ca) {
        const uint16_t http_port = cfg.http_port > 0 ? cfg.http_port : 80;
        auto r = net::http_get_plain(cfg.ccc_host, http_port, "/ca.crt");
        if (!r.ok() || r.body.find("BEGIN CERTIFICATE") == std::string::npos) {
            std::cerr << "enroll: cannot download the CA certificate from http://" << cfg.ccc_host << "/ca.crt ("
                      << r.status << " " << r.error << ")\n";
            return 4;
        }
        ca = r.body;
    }
    auto actual = crypto::cert_fingerprint(ca);
    if (!actual) {
        std::cerr << "enroll: CA certificate is not valid PEM\n";
        return 4;
    }
    if (!fp.empty()) {
        std::string clean;
        for (char c : fp) if (c != ':') clean += c;
        if (clean != *actual) {
            std::cerr << "enroll: CA fingerprint mismatch!\n  expected " << clean << "\n  got      " << *actual
                      << "\nRefusing to continue (possible man-in-the-middle).\n";
            return 5;
        }
    } else if (!have_ca && !o.trust_on_first_use) {
        std::cerr << "enroll: no CA fingerprint to verify against. Pass --ca-fingerprint (shown on the Add-a-PC page)"
                     " or --trust-on-first-use.\n";
        return 5;
    }
    if (!have_ca) write_file_atomic(cfg.ca_file, ca);
    cfg.ca_fingerprint = *actual;

    // 3. Key + CSR, then register as Pending.
    std::string err;
    auto csr = crypto::generate_key_and_csr(cfg.key_file, sys.hostname(), &err);
    if (!csr) {
        std::cerr << "enroll: " << err << "\n";
        return 6;
    }
    net::TlsOptions tls;
    tls.ca_file = cfg.ca_file;
    tls.expected_host = cfg.ccc_host;
    net::HttpsClient api(cfg.ccc_host, cfg.api_port, tls);
    json body{{"enroll_key", o.key},
              {"hostname", sys.hostname()},
              {"os", sys.os_name()},
              {"os_version", sys.os_version()},
              {"macs", sys.mac_addresses()},
              {"machine_uid", load_or_create_machine_uid(cfg.data_dir)},
              {"agent_version", XG_VERSION},
              {"csr_pem", *csr}};
    auto r = api.request("POST", protocol::kEnrollPath, body.dump());
    if (!r.ok()) {
        std::cerr << "enroll: CCC refused the request (" << r.status << " " << r.error << "): " << r.body << "\n";
        return 7;
    }
    auto j = json::parse(r.body, nullptr, false);
    if (j.is_discarded() || !j.contains("enrollment_id")) {
        std::cerr << "enroll: unexpected response: " << r.body << "\n";
        return 7;
    }
    cfg.enrollment_id = std::to_string(j.value("enrollment_id", 0));
    cfg.poll_token = j.value("poll_token", "");
    cfg.pc_id.clear();
    cfg.token.clear();
    if (!cfg.save(config_path)) {
        std::cerr << "enroll: cannot write " << config_path << " (run as root/administrator)\n";
        return 8;
    }
    std::cout << "Registered with the CCC. Status: " << j.value("status", "pending") << "\n";
    if (j.value("status", "") != "approved")
        std::cout << "Ask the lab assistant to approve '" << sys.hostname() << "' on the Enrollment page.\n";
    if (!o.wait) return 0;

    // 4. Wait for approval (the service also keeps polling, so Ctrl+C here is harmless).
    const int64_t deadline = o.wait_timeout_s > 0 ? steady_ms() + int64_t(o.wait_timeout_s) * 1000 : INT64_MAX;
    while (steady_ms() < deadline) {
        auto pr = api.request("GET", std::string(protocol::kEnrollPath) + "/" + cfg.enrollment_id +
                                         "?poll_token=" + cfg.poll_token);
        if (pr.ok()) {
            auto pj = json::parse(pr.body, nullptr, false);
            const auto st = pj.is_discarded() ? "" : pj.value("status", "");
            if (st == "approved") {
                write_file_atomic(cfg.cert_file, pj.value("cert_pem", ""));
                if (pj.contains("ca_pem")) write_file_atomic(cfg.ca_file, pj["ca_pem"].get<std::string>());
                cfg.pc_id = std::to_string(pj.value("pc_id", 0));
                cfg.token = pj.value("token", "");
                cfg.poll_token.clear();
                cfg.save(config_path);
                std::cout << "Approved. This PC is #" << cfg.pc_id << " (" << pj.value("name", "") << ").\n";
                return 0;
            }
            if (st == "rejected") {
                std::cerr << "Enrollment was rejected.\n";
                return 9;
            }
        }
        std::this_thread::sleep_for(std::chrono::seconds(3));
    }
    std::cout << "Still pending. The agent service will finish enrollment once approved.\n";
    return 0;
}

}  // namespace xg
