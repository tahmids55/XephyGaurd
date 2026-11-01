#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>

namespace xg {

/// Suppresses repeated rule hits for the same (rule, subject) within a cooldown window, so a
/// process that restarts in a loop produces one alert per minute instead of hundreds.
class HitDeduper {
public:
    explicit HitDeduper(int64_t cooldown_ms = 60000) : cooldown_ms_(cooldown_ms) {}
    /// Returns true if the hit should be reported now.
    bool should_report(const std::string& rule_id, const std::string& subject, int64_t now_ms) {
        auto key = rule_id + "\x1f" + subject;
        auto it = last_.find(key);
        if (it != last_.end() && now_ms - it->second < cooldown_ms_) return false;
        last_[key] = now_ms;
        if (last_.size() > 4096) prune(now_ms);
        return true;
    }
    void reset() { last_.clear(); }

private:
    void prune(int64_t now_ms) {
        for (auto it = last_.begin(); it != last_.end();)
            it = (now_ms - it->second >= cooldown_ms_) ? last_.erase(it) : std::next(it);
    }
    int64_t cooldown_ms_;
    std::unordered_map<std::string, int64_t> last_;
};

}  // namespace xg
