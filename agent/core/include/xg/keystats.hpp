#pragma once
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "xg/event.hpp"

namespace xg {

/// Aggregated typing rhythm for one window. Contains no characters or key codes.
struct KeyStatsWindow {
    int64_t window_start_ms = 0;
    int window_s = 5;
    uint32_t keys = 0;
    uint32_t backspaces = 0;
    double iki_mean_ms = 0, iki_median_ms = 0, iki_p90_ms = 0, iki_std_ms = 0;
    uint32_t bursts = 0;          // runs of >= 8 keys with inter-key interval < 120 ms
    int64_t max_pause_ms = 0;     // longest gap inside the window
    double backspace_ratio() const { return keys ? double(backspaces) / keys : 0.0; }
    json to_json() const;
};

/// Feeds key-down timestamps; emits one KeyStatsWindow per elapsed window.
/// The caller passes only (timestamp, is_backspace): the key identity never enters this class.
class KeyStatsAggregator {
public:
    explicit KeyStatsAggregator(int window_s = 5) : window_ms_(int64_t(window_s) * 1000), window_s_(window_s) {}
    void on_key(int64_t t_ms, bool is_backspace);
    /// Closes the current window if `now_ms` is past its end. Returns nullopt for empty windows.
    std::optional<KeyStatsWindow> flush(int64_t now_ms, bool force = false);
    void set_window(int window_s) { window_ms_ = int64_t(window_s) * 1000; window_s_ = window_s; }

private:
    int64_t window_ms_;
    int window_s_;
    int64_t start_ = 0;
    int64_t last_key_ = 0;
    std::vector<std::pair<int64_t, bool>> times_;   // (timestamp, is_backspace)
};

}  // namespace xg
