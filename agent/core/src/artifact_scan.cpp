#include "xg/artifact_scan.hpp"

#include <algorithm>
#include <filesystem>
#include <set>

#include "xg/util.hpp"

namespace xg {

namespace fs = std::filesystem;

ArtifactScanResult scan_artifacts(const RuleEngine& rules, const std::vector<std::string>& homes,
                                  const std::atomic<bool>& stop) {
    ArtifactScanResult res;
    const auto spec = rules.artifact_scan();
    const auto arules = rules.artifact_rules();
    std::set<std::string> seen;

    // 1. Known model stores (cheap, exact).
    for (const auto& home : homes)
        for (const auto& r : arules)
            for (const auto& d : r.dirs) {
                const auto p = expand_path(d, home);
                if (p.find('%') != std::string::npos) continue;  // unexpanded Windows var on Linux
                std::error_code ec;
                if (!fs::is_directory(p, ec)) continue;
                // Same budget as the root walk: a user-made huge tree in ~/.ollama cannot pin the worker (I-AGENT-29).
                uint64_t total = 0, visited = 0;
                for (auto it = fs::recursive_directory_iterator(p, fs::directory_options::skip_permission_denied, ec);
                     it != fs::recursive_directory_iterator() && !stop; it.increment(ec)) {
                    if (ec) break;
                    if (++visited > spec.max_files) {
                        res.truncated = true;
                        break;
                    }
                    ++res.files_visited;
                    if (it->is_directory(ec) && it.depth() >= spec.max_depth) it.disable_recursion_pending();
                    else if (it->is_regular_file(ec)) total += it->file_size(ec);
                }
                if (seen.insert(p).second) res.found.push_back(ArtifactInfo{p, total, r.id});
            }

    // 2. Bounded walk of the scan roots for weight files, prioritizing external/removable mounts
    // (/media, /run/media, /mnt) before home trees and giving each root its own file budget.
    auto is_priority_mount = [](const std::string& p) {
        return p == "/media" || p.rfind("/media/", 0) == 0 || p == "/run/media" || p.rfind("/run/media/", 0) == 0 ||
               p == "/mnt" || p.rfind("/mnt/", 0) == 0;
    };
    std::vector<std::string> ordered_roots;
    std::set<std::string> uniq_roots;
    auto push_root = [&](const std::string& p) {
        if (!p.empty() && uniq_roots.insert(p).second) ordered_roots.push_back(p);
    };
    for (const auto& r : spec.roots) {
        if (r.rfind("~", 0) == 0 || r.find("%USERPROFILE%") != std::string::npos) {
            for (const auto& h : homes) push_root(expand_path(r, h));
        } else if (r.find('%') == std::string::npos) {
            push_root(r);
        }
    }
    std::stable_partition(ordered_roots.begin(), ordered_roots.end(), is_priority_mount);

    for (const auto& root : ordered_roots) {
        std::error_code ec;
        if (!fs::is_directory(root, ec)) continue;
        uint64_t root_visited = 0;
        auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec);
        for (; it != fs::recursive_directory_iterator() && !stop; it.increment(ec)) {
            if (ec) { ec.clear(); continue; }
            if (++root_visited > spec.max_files) { res.truncated = true; break; }
            ++res.files_visited;
            const auto& e = *it;
            const auto name = e.path().filename().string();
            if (e.is_directory(ec)) {
                const auto lname = to_lower(name);
                bool skip = it.depth() >= spec.max_depth || e.is_symlink(ec) ||
                            lname == "appdata" || lname == "application data" || lname == "local settings" ||
                            lname == "$recycle.bin" || lname == "system volume information" || lname == "windows" ||
                            lname == "program files" || lname == "program files (x86)" || lname == "programdata" ||
                            lname == "node_modules" || lname == ".git" || lname == ".venv" || lname == "venv" ||
                            lname == "__pycache__" || lname == ".cache" || lname == ".local" || lname == ".vscode" ||
                            lname == ".antigravity" || lname == ".cargo" || lname == ".rustup" || lname == "snap";
                if (!skip) {
                    const auto full = e.path().string();
                    for (const auto& s : spec.skip_dirs) {
                        if (to_lower(s) == lname || iends_with(full, s)) {
                            skip = true;
                            break;
                        }
                    }
                }
                if (skip) it.disable_recursion_pending();
                continue;
            }
            if (!e.is_regular_file(ec)) continue;
            const auto size = e.file_size(ec);
            if (ec) continue;
            if (auto hit = rules.match_artifact_file(name, size)) {
                const auto p = e.path().string();
                if (seen.insert(p).second) res.found.push_back(ArtifactInfo{p, size, hit->rule_id});
            }
        }
    }
    return res;
}

}  // namespace xg
