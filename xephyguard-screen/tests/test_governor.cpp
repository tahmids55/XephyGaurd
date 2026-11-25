#include "doctest.h"
#include "governor.h"

TEST_CASE("CpuGovernor: over budget lowers fps to the floor, then quality; never below either") {
  xg::CpuGovernor g(8.0, 15.0, 2.0);
  CHECK(g.fps() == 15.0);
  CHECK(g.level() == 0);
  g.update(0.30, 1.0);  // 30 % of a core
  CHECK(g.fps() < 15.0);
  CHECK(g.fps() >= 2.0);
  CHECK(g.quality_scale() == 1.0);  // fps goes first
  for (int i = 0; i < 20; ++i) g.update(0.30, 1.0);
  CHECK(g.fps() == 2.0);
  CHECK(g.quality_scale() == doctest::Approx(0.5));
  CHECK(g.cpu_pct() == doctest::Approx(30.0));
  CHECK(g.level() > 0);
  CHECK(g.min_interval_ms() == 500);
}

TEST_CASE("CpuGovernor: recovers slowly with headroom, quality first, and holds inside the band") {
  xg::CpuGovernor g(8.0, 15.0, 2.0);
  for (int i = 0; i < 20; ++i) g.update(0.30, 1.0);
  g.update(0.02, 1.0);  // 2 %: headroom
  CHECK(g.fps() == 2.0);
  CHECK(g.quality_scale() == doctest::Approx(0.55));
  for (int i = 0; i < 9; ++i) g.update(0.02, 1.0);
  CHECK(g.quality_scale() == doctest::Approx(1.0));
  g.update(0.02, 1.0);
  CHECK(g.fps() == 3.0);  // one frame per second per step
  g.update(0.06, 1.0);    // 6 %: under budget but no headroom -> hold
  CHECK(g.fps() == 3.0);
  g.update(0.0, 0.0);     // ignored
  CHECK(g.fps() == 3.0);
  g.set_budget_pct(0.0);  // clamped to 1 %
  CHECK(g.budget_pct() == 1.0);
}

TEST_CASE("CpuGovernor: the cut starts from the frames really sent, not from the fps cap") {
  xg::CpuGovernor g(8.0, 15.0, 2.0);
  g.update(0.16, 1.0, 5);  // 16 % at 5 frames/s (flow control capped it): aim at 2.5 fps at once
  CHECK(g.fps() == doctest::Approx(2.5));
}

TEST_CASE("CpuGovernor: a second encoder worker only with headroom, back to one when throttled or near the budget") {
  xg::CpuGovernor g(8.0, 15.0, 2.0);
  CHECK(g.workers() == 1);
  g.update(0.03, 1.0);  // 3 % < 50 % of the budget, not throttled
  CHECK(g.workers() == 2);
  g.update(0.05, 1.0);  // 5 %: inside the band, keep 2
  CHECK(g.workers() == 2);
  g.update(0.07, 1.0);  // 7 % > 80 % of the budget: drop to 1 (fps still 15)
  CHECK(g.workers() == 1);
  g.update(0.05, 1.0);  // 5 %: hold at 1, no flapping
  CHECK(g.workers() == 1);
  g.update(0.03, 1.0);
  CHECK(g.workers() == 2);
  g.update(0.30, 1.0);  // over budget: throttled, 1 worker
  CHECK(g.workers() == 1);
  g.update(0.01, 1.0);  // headroom again but still throttled (level > 0): stays 1 until fully recovered
  CHECK(g.level() > 0);
  CHECK(g.workers() == 1);
  for (int i = 0; i < 40 && g.level() > 0; ++i) g.update(0.01, 1.0);
  CHECK(g.level() == 0);
  CHECK(g.workers() == 2);
  g.update(0.0, 0.0);  // ignored
  CHECK(g.workers() == 2);
}
