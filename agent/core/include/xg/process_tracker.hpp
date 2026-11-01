#pragma once
#include <cstdint>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "xg/types.hpp"

namespace xg {

/// Diffs successive process snapshots into start/stop events and maintains the
/// "snapshot hash + diff" of process names reported periodically to the CCC.
class ProcessTracker {
public:
    struct Diff {
        std::vector<ProcessInfo> started;
        std::vector<ProcessInfo> stopped;
    };
    Diff update(const std::vector<ProcessInfo>& snapshot);
    /// Registers a process seen via an exec notification; returns false if it was already known.
    bool note_started(const ProcessInfo& p);
    bool known(int64_t pid) const { return procs_.contains(pid); }
    const ProcessInfo* get(int64_t pid) const;

    struct NameSnapshot {
        std::string hash;                 // SHA-256 of the sorted unique exe-name list
        size_t count = 0;
        std::vector<std::string> added;   // names not present in the previous name snapshot
        std::vector<std::string> removed;
    };
    NameSnapshot name_snapshot();

private:
    std::unordered_map<int64_t, ProcessInfo> procs_;
    std::set<std::string> last_names_;
    bool first_ = true;
};

}  // namespace xg
