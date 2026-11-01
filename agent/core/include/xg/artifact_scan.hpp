#pragma once
#include <atomic>
#include <string>
#include <vector>

#include "xg/rules.hpp"
#include "xg/types.hpp"

namespace xg {

struct ArtifactScanResult {
    std::vector<ArtifactInfo> found;
    uint64_t files_visited = 0;
    bool truncated = false;
};

/// Portable (std::filesystem) scan for local model weights and model stores.
/// `homes` are the user home directories that "~" / %USERPROFILE% expand to.
ArtifactScanResult scan_artifacts(const RuleEngine& rules, const std::vector<std::string>& homes,
                                  const std::atomic<bool>& stop);

}  // namespace xg
