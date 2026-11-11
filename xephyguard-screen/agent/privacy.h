#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <optional>
#include <functional>
#include <regex>

namespace xg {

struct PrivacyRule {
  std::optional<std::string> process_name;
  std::optional<std::string> window_class;
  std::optional<std::string> title_regex;
};

struct WindowInfo {
  std::string process_name;
  std::string window_class;
  std::string title;
};

using WindowProvider = std::function<WindowInfo()>;
using AuditCallback = std::function<void(bool masked, uint64_t duration_ms)>;

class PrivacyFilter {
public:
  PrivacyFilter();

  void set_rules(std::vector<PrivacyRule> rules);
  void set_exam_mode(bool exam_mode);
  bool is_exam_mode() const;

  void set_window_provider(WindowProvider provider);
  void set_audit_callback(AuditCallback cb);

  // Evaluates whether the current screen state should be masked.
  // If provider throws or fails, returns true (fail-safe mask).
  // If exam_mode is enabled, masking is disabled and returns false.
  // Audits mask state transitions with duration.
  bool check_masked(uint64_t now_ms);

  bool is_masked() const;

private:
  std::vector<PrivacyRule> rules_;
  bool exam_mode_ = false;
  bool is_masked_ = false;
  uint64_t mask_start_ms_ = 0;

  WindowProvider provider_;
  AuditCallback audit_cb_;

  bool match_rule(const PrivacyRule& rule, const WindowInfo& win) const;
};

}  // namespace xg
