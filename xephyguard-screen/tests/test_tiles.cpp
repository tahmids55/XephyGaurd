#include "../third_party/doctest.h"
#include "../core/tiles.h"

static xg::Frame make_solid_frame(int w, int h, uint8_t r, uint8_t g, uint8_t b) {
  xg::Frame f;
  f.width = w;
  f.height = h;
  f.stride = w * 4;
  f.bgra.resize(static_cast<size_t>(f.stride * h));
  for (int y = 0; y < h; ++y) {
    uint8_t* row = f.bgra.data() + y * f.stride;
    for (int x = 0; x < w; ++x) {
      row[x * 4 + 0] = b;
      row[x * 4 + 1] = g;
      row[x * 4 + 2] = r;
      row[x * 4 + 3] = 0xFF;
    }
  }
  return f;
}

TEST_CASE("TileTracker: Identical frame twice gives no changes") {
  xg::TileTracker tracker(1920, 1080);
  auto f1 = make_solid_frame(1920, 1080, 10, 20, 30);
  f1.full_dirty = true;
  auto ch1 = tracker.update(f1);
  CHECK(ch1.size() == static_cast<size_t>(tracker.cols() * tracker.rows()));
  tracker.mark_sent();

  // Second identical frame (not full dirty, but with dirty rect touching whole screen)
  auto f2 = f1;
  f2.full_dirty = false;
  f2.dirty = {{0, 0, 1920, 1080}};
  auto ch2 = tracker.update(f2);
  CHECK(ch2.empty());
  CHECK(tracker.pending().empty());
}

TEST_CASE("TileTracker: Changing one pixel changes exactly one tile") {
  xg::TileTracker tracker(640, 480);
  auto f1 = make_solid_frame(640, 480, 100, 100, 100);
  f1.full_dirty = true;
  tracker.update(f1);
  tracker.mark_sent();

  // Change single pixel at (70, 70), which belongs to tile (tx=1, ty=1) -> index: 1 * 10 + 1 = 11
  auto f2 = f1;
  f2.full_dirty = false;
  f2.dirty = {{70, 70, 1, 1}};
  f2.bgra[static_cast<size_t>(70 * f2.stride + 70 * 4)] = 0xAA;

  auto ch = tracker.update(f2);
  REQUIRE(ch.size() == 1);
  CHECK(ch[0] == 11);
  CHECK(tracker.pending() == std::vector<uint32_t>{11});
}

TEST_CASE("TileTracker: Dirty rect spanning 4 tiles with no pixel change reports none") {
  xg::TileTracker tracker(640, 480);
  auto f1 = make_solid_frame(640, 480, 50, 50, 50);
  f1.full_dirty = true;
  tracker.update(f1);
  tracker.mark_sent();

  // Dirty rect across tiles (0,0), (1,0), (0,1), (1,1): x=60..70, y=60..70
  auto f2 = f1;
  f2.full_dirty = false;
  f2.dirty = {{60, 60, 10, 10}};
  // but pixels remain identical
  auto ch = tracker.update(f2);
  CHECK(ch.empty());
  CHECK(tracker.pending().empty());
}

TEST_CASE("TileTracker: full_dirty rehashes all and marks changes") {
  xg::TileTracker tracker(128, 128);  // 2x2 = 4 tiles
  auto f1 = make_solid_frame(128, 128, 0, 0, 0);
  f1.full_dirty = true;
  tracker.update(f1);
  tracker.mark_sent();

  // Modify 2 tiles, set full_dirty = true
  auto f2 = make_solid_frame(128, 128, 0, 0, 0);
  // Modify tile (0, 0)
  f2.bgra[0] = 0xFF;
  // Modify tile (1, 1) -> (64, 64)
  f2.bgra[static_cast<size_t>(64 * f2.stride + 64 * 4)] = 0xFF;
  f2.full_dirty = true;

  auto ch = tracker.update(f2);
  REQUIRE(ch.size() == 2);
  CHECK(ch[0] == 0);
  CHECK(ch[1] == 3);
}

TEST_CASE("TileTracker: pending() accumulates across two update() calls until mark_sent()") {
  xg::TileTracker tracker(640, 480);
  auto f1 = make_solid_frame(640, 480, 1, 1, 1);
  f1.full_dirty = true;
  tracker.update(f1);
  tracker.mark_sent();

  // First change at (10, 10) -> tile 0
  auto f2 = f1;
  f2.full_dirty = false;
  f2.dirty = {{10, 10, 1, 1}};
  f2.bgra[10 * f2.stride + 10 * 4] = 99;
  tracker.update(f2);

  CHECK(tracker.pending() == std::vector<uint32_t>{0});

  // Second change at (70, 10) -> tile 1
  auto f3 = f2;
  f3.full_dirty = false;
  f3.dirty = {{70, 10, 1, 1}};
  f3.bgra[10 * f3.stride + 70 * 4] = 99;
  tracker.update(f3);

  CHECK(tracker.pending() == std::vector<uint32_t>{0, 1});

  tracker.mark_sent();
  CHECK(tracker.pending().empty());
}

TEST_CASE("TileTracker: Odd dimensions work (1366x768)") {
  xg::TileTracker tracker(1366, 768);
  // cols = (1366 + 63)/64 = 22
  // rows = (768 + 63)/64 = 12
  CHECK(tracker.cols() == 22);
  CHECK(tracker.rows() == 12);

  auto f1 = make_solid_frame(1366, 768, 12, 34, 56);
  f1.full_dirty = true;
  auto ch = tracker.update(f1);
  CHECK(ch.size() == static_cast<size_t>(22 * 12));

  // Modify pixel on the partial right edge: x=1365, y=10 -> tile index: (0 * 22) + 21 = 21
  auto f2 = f1;
  f2.full_dirty = false;
  f2.dirty = {{1365, 10, 1, 1}};
  f2.bgra[static_cast<size_t>(10 * f2.stride + 1365 * 4)] ^= 0xFF;

  auto ch2 = tracker.update(f2);
  REQUIRE(ch2.size() == 1);
  CHECK(ch2[0] == 21);
}
