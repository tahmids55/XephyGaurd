#include "xg/keystats.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace xg {

json KeyStatsWindow::to_json() const {
    auto r = [](double v) { return std::round(v * 10.0) / 10.0; };
    return json{{"window_start", window_start_ms}, {"window_s", window_s}, {"keys", keys}, {"backspaces", backspaces},
                {"backspace_ratio", r(backspace_ratio() * 100.0) / 100.0}, {"iki_mean", r(iki_mean_ms)},
                {"iki_median", r(iki_median_ms)}, {"iki_p90", r(iki_p90_ms)}, {"iki_std", r(iki_std_ms)},
                {"bursts", bursts}, {"max_pause", max_pause_ms}};
}

void KeyStatsAggregator::on_key(int64_t t, bool is_backspace) {
    if (times_.empty() && start_ == 0) start_ = t - (t % window_ms_);
    times_.emplace_back(t, is_backspace);
    last_key_ = t;
}

std::optional<KeyStatsWindow> KeyStatsAggregator::flush(int64_t now, bool force) {
    if (start_ == 0) return std::nullopt;
    if (!force && now < start_ + window_ms_) return std::nullopt;
    KeyStatsWindow w;
    w.window_start_ms = start_;
    w.window_s = window_s_;
    // Keys belonging to the closed window; anything later stays for the next window.
    const int64_t end = force ? INT64_MAX : start_ + window_ms_;
    auto split = std::partition_point(times_.begin(), times_.end(), [&](const auto& k) { return k.first < end; });
    std::vector<int64_t> in;
    for (auto it = times_.begin(); it != split; ++it) {
        in.push_back(it->first);
        if (it->second) ++w.backspaces;
    }
    times_.erase(times_.begin(), split);
    w.keys = uint32_t(in.size());
    if (in.size() >= 2) {
        std::vector<double> iki;
        iki.reserve(in.size() - 1);
        for (size_t i = 1; i < in.size(); ++i) iki.push_back(double(in[i] - in[i - 1]));
        const double mean = std::accumulate(iki.begin(), iki.end(), 0.0) / double(iki.size());
        double var = 0;
        for (double v : iki) var += (v - mean) * (v - mean);
        w.iki_mean_ms = mean;
        w.iki_std_ms = std::sqrt(var / double(iki.size()));
        std::vector<double> sorted = iki;
        std::sort(sorted.begin(), sorted.end());
        w.iki_median_ms = sorted[sorted.size() / 2];
        w.iki_p90_ms = sorted[std::min(sorted.size() - 1, size_t(double(sorted.size()) * 0.9))];
        w.max_pause_ms = int64_t(sorted.back());
        int run = 1;
        for (double v : iki) {
            if (v < 120.0) { ++run; }
            else { if (run >= 8) ++w.bursts; run = 1; }
        }
        if (run >= 8) ++w.bursts;
    }
    if (times_.empty()) start_ = 0;
    else start_ = times_.front().first - (times_.front().first % window_ms_);
    if (w.keys == 0) return std::nullopt;
    return w;
}

}  // namespace xg
