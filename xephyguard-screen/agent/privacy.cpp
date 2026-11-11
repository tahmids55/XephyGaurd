#include "privacy.h"
#include <algorithm>
#include <cctype>

namespace xg {

static bool iequals(const std::string& a, const std::string& b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) !=
        std::tolower(static_cast<unsigned char>(b[i]))) {
      return false;
    }
  }
  return true;
}

PrivacyFilter::PrivacyFilter() = default;

void PrivacyFilter::set_rules(std::vector<PrivacyRule> rules) {
  rules_ = std::move(rules);
}

void PrivacyFilter::set_exam_mode(bool exam_mode) {
  exam_mode_ = exam_mode;
}

bool PrivacyFilter::is_exam_mode() const {
  return exam_mode_;
}

void PrivacyFilter::set_window_provider(WindowProvider provider) {
  provider_ = std::move(provider);
}

void PrivacyFilter::set_audit_callback(AuditCallback cb) {
  audit_cb_ = std::move(cb);
}

bool PrivacyFilter::is_masked() const {
  return is_masked_;
}

bool PrivacyFilter::match_rule(const PrivacyRule& rule, const WindowInfo& win) const {
  bool has_criteria = false;

  if (rule.process_name.has_value()) {
    has_criteria = true;
    if (!iequals(*rule.process_name, win.process_name)) {
      return false;
    }
  }

  if (rule.window_class.has_value()) {
    has_criteria = true;
    if (rule.window_class.value() != win.window_class) {
      return false;
    }
  }

  if (rule.title_regex.has_value()) {
    has_criteria = true;
    try {
      std::regex re(*rule.title_regex, std::regex_constants::icase);
      if (!std::regex_search(win.title, re)) {
        return false;
      }
    } catch (...) {
      // Invalid regex pattern is treated as an error -> trigger mask
      throw;
    }
  }

  return has_criteria;
}

bool PrivacyFilter::check_masked(uint64_t now_ms) {
  if (exam_mode_) {
    if (is_masked_) {
      uint64_t duration = (now_ms >= mask_start_ms_) ? (now_ms - mask_start_ms_) : 0;
      is_masked_ = false;
      if (audit_cb_) {
        audit_cb_(false, duration);
      }
    }
    return false;
  }

  bool should_mask = false;
  try {
    if (!provider_) {
      should_mask = false;
    } else {
      WindowInfo win = provider_();
      for (const auto& rule : rules_) {
        if (match_rule(rule, win)) {
          should_mask = true;
          break;
        }
      }
    }
  } catch (...) {
    // Fail-safe: "If any rule matches the foreground window, or detection throws, set MASKED"
    should_mask = true;
  }

  if (should_mask && !is_masked_) {
    is_masked_ = true;
    mask_start_ms_ = now_ms;
    if (audit_cb_) {
      audit_cb_(true, 0);
    }
  } else if (!should_mask && is_masked_) {
    uint64_t duration = (now_ms >= mask_start_ms_) ? (now_ms - mask_start_ms_) : 0;
    is_masked_ = false;
    if (audit_cb_) {
      audit_cb_(false, duration);
    }
  }

  return is_masked_;
}

}  // namespace xg
