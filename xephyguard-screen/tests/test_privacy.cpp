#include "doctest.h"
#include "privacy.h"
#include <stdexcept>

TEST_SUITE("Privacy") {

TEST_CASE("PrivacyFilter: Rule matching by process, class, and title regex") {
  xg::PrivacyFilter filter;

  xg::PrivacyRule r1;
  r1.process_name = "keepassxc.exe";

  xg::PrivacyRule r2;
  r2.title_regex = ".*Banking.*";

  filter.set_rules({r1, r2});

  xg::WindowInfo current_win{"notepad.exe", "Notepad", "My Notes"};
  filter.set_window_provider([&]() { return current_win; });

  // Unrelated window: not masked
  CHECK_FALSE(filter.check_masked(1000));
  CHECK_FALSE(filter.is_masked());

  // Matches process name (case-insensitive)
  current_win.process_name = "KeePassXC.exe";
  CHECK(filter.check_masked(1100));
  CHECK(filter.is_masked());

  // Switch to banking title
  current_win.process_name = "chrome.exe";
  current_win.title = "Personal Banking Login";
  CHECK(filter.check_masked(1200));
  CHECK(filter.is_masked());

  // Switch back to normal window
  current_win.process_name = "code.exe";
  current_win.title = "project - Visual Studio Code";
  CHECK_FALSE(filter.check_masked(1500));
  CHECK_FALSE(filter.is_masked());
}

TEST_CASE("PrivacyFilter: Provider failure or throw triggers safe MASKED") {
  xg::PrivacyFilter filter;
  filter.set_window_provider([]() -> xg::WindowInfo {
    throw std::runtime_error("OS API failure");
  });

  CHECK(filter.check_masked(1000));
  CHECK(filter.is_masked());
}

TEST_CASE("PrivacyFilter: Exam mode overrides all masking rules and errors") {
  xg::PrivacyFilter filter;
  filter.set_rules({xg::PrivacyRule{"secret.exe", std::nullopt, std::nullopt}});
  filter.set_window_provider([]() -> xg::WindowInfo {
    throw std::runtime_error("Failed to query window");
  });

  // Enable exam mode
  filter.set_exam_mode(true);
  CHECK(filter.is_exam_mode());

  // Even if provider throws, exam mode disables masking
  CHECK_FALSE(filter.check_masked(1000));
  CHECK_FALSE(filter.is_masked());
}

TEST_CASE("PrivacyFilter: Audit logging records mask transitions and duration") {
  xg::PrivacyFilter filter;
  xg::PrivacyRule rule;
  rule.process_name = "restricted.exe";
  filter.set_rules({rule});

  xg::WindowInfo current_win{"safe.exe", "", ""};
  filter.set_window_provider([&]() { return current_win; });

  bool last_masked_state = false;
  uint64_t last_duration = 0;
  int audit_events = 0;

  filter.set_audit_callback([&](bool masked, uint64_t duration) {
    last_masked_state = masked;
    last_duration = duration;
    audit_events++;
  });

  // 1. Initial check (unmasked)
  filter.check_masked(1000);
  CHECK(audit_events == 0);

  // 2. Trigger mask at t = 2000
  current_win.process_name = "restricted.exe";
  filter.check_masked(2000);
  CHECK(audit_events == 1);
  CHECK(last_masked_state == true);
  CHECK(last_duration == 0);

  // 3. Still masked at t = 3000 (no transition event)
  filter.check_masked(3000);
  CHECK(audit_events == 1);

  // 4. Return to safe window at t = 4500 (duration = 4500 - 2000 = 2500 ms)
  current_win.process_name = "safe.exe";
  filter.check_masked(4500);
  CHECK(audit_events == 2);
  CHECK(last_masked_state == false);
  CHECK(last_duration == 2500);
}

}
