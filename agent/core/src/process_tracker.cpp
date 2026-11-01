#include "xg/process_tracker.hpp"

#include <algorithm>
#include <unordered_set>

#include "xg/crypto.hpp"
#include "xg/util.hpp"

namespace xg {

ProcessTracker::Diff ProcessTracker::update(const std::vector<ProcessInfo>& snap) {
    Diff d;
    std::unordered_set<int64_t> present;
    present.reserve(snap.size());
    for (const auto& p : snap) {
        present.insert(p.pid);
        auto it = procs_.find(p.pid);
        if (it == procs_.end()) {
            procs_.emplace(p.pid, p);
            if (!first_) d.started.push_back(p);
        } else if (p.start_time != 0 && it->second.start_time != 0 && p.start_time != it->second.start_time) {
            // PID reused between scans.
            d.stopped.push_back(it->second);
            it->second = p;
            if (!first_) d.started.push_back(p);
        }
    }
    for (auto it = procs_.begin(); it != procs_.end();) {
        if (!present.contains(it->first)) {
            d.stopped.push_back(it->second);
            it = procs_.erase(it);
        } else {
            ++it;
        }
    }
    if (first_) {
        // The first snapshot after start: everything already running counts as "started" so that
        // processes launched while the agent was down are still matched.
        d.started = snap;
        first_ = false;
    }
    return d;
}

bool ProcessTracker::note_started(const ProcessInfo& p) {
    auto [it, inserted] = procs_.emplace(p.pid, p);
    if (!inserted && p.start_time != 0 && it->second.start_time != p.start_time) {
        it->second = p;
        return true;
    }
    return inserted;
}

const ProcessInfo* ProcessTracker::get(int64_t pid) const {
    auto it = procs_.find(pid);
    return it == procs_.end() ? nullptr : &it->second;
}

ProcessTracker::NameSnapshot ProcessTracker::name_snapshot() {
    std::set<std::string> names;
    for (const auto& [pid, p] : procs_) {
        auto n = normalize_exe(p.exe_path.empty() ? p.name : p.exe_path);
        if (!n.empty()) names.insert(n);
    }
    NameSnapshot s;
    std::string joined;
    for (const auto& n : names) { joined += n; joined += '\n'; }
    s.hash = crypto::sha256_hex(joined);
    s.count = names.size();
    std::set_difference(names.begin(), names.end(), last_names_.begin(), last_names_.end(), std::back_inserter(s.added));
    std::set_difference(last_names_.begin(), last_names_.end(), names.begin(), names.end(), std::back_inserter(s.removed));
    last_names_ = std::move(names);
    return s;
}

}  // namespace xg
