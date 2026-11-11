#include "doctest.h"
#include "downscale.h"

TEST_SUITE("Downscale") {

TEST_CASE("Downscale: Preserves aspect ratio") {
  xg::Frame frame;
  frame.width = 1920;
  frame.height = 1080;
  frame.stride = 1920 * 4;
  frame.bgra.resize(frame.stride * frame.height, 0);

  int out_h = 0;
  auto out = xg::downscale_box(frame, 320, &out_h);
  CHECK(out_h == 180);
  CHECK(out.size() == 320 * 180 * 4);

  // 1024x768 -> 320x240
  frame.width = 1024;
  frame.height = 768;
  frame.stride = 1024 * 4;
  frame.bgra.resize(frame.stride * frame.height, 0);
  out = xg::downscale_box(frame, 320, &out_h);
  CHECK(out_h == 240);
  CHECK(out.size() == 320 * 240 * 4);

  // Invalid inputs
  CHECK(xg::downscale_box(frame, 0, &out_h).empty());
  CHECK(out_h == 0);

  xg::Frame empty;
  CHECK(xg::downscale_box(empty, 320, &out_h).empty());
  CHECK(out_h == 0);
}

TEST_CASE("Downscale: Averages correctly on checkerboard") {
  xg::Frame frame;
  frame.width = 4;
  frame.height = 4;
  frame.stride = 4 * 4;
  frame.bgra.resize(frame.stride * frame.height, 0);

  // Alternating black (0,0,0,255) and white (255,255,255,255)
  for (int y = 0; y < 4; ++y) {
    for (int x = 0; x < 4; ++x) {
      uint8_t val = ((x + y) % 2 == 0) ? 0 : 255;
      size_t idx = (y * 4 + x) * 4;
      frame.bgra[idx + 0] = val; // B
      frame.bgra[idx + 1] = val; // G
      frame.bgra[idx + 2] = val; // R
      frame.bgra[idx + 3] = 255; // A
    }
  }

  // Downscale 4x4 to 2x2: Each 2x2 dst pixel averages 2 black and 2 white pixels
  int out_h = 0;
  auto out = xg::downscale_box(frame, 2, &out_h);
  REQUIRE(out_h == 2);
  REQUIRE(out.size() == 2 * 2 * 4);

  for (size_t i = 0; i < 4; ++i) {
    CHECK(out[i * 4 + 0] == 128); // B
    CHECK(out[i * 4 + 1] == 128); // G
    CHECK(out[i * 4 + 2] == 128); // R
    CHECK(out[i * 4 + 3] == 255); // A
  }

  // Downscale 4x4 to 1x1: all 16 pixels averaged (8 black, 8 white)
  out = xg::downscale_box(frame, 1, &out_h);
  REQUIRE(out_h == 1);
  REQUIRE(out.size() == 4);
  CHECK(out[0] == 128);
  CHECK(out[1] == 128);
  CHECK(out[2] == 128);
  CHECK(out[3] == 255);
}

TEST_CASE("Downscale: Uniform color preservation") {
  xg::Frame frame;
  frame.width = 100;
  frame.height = 50;
  frame.stride = 100 * 4;
  frame.bgra.resize(frame.stride * frame.height, 0);

  for (size_t i = 0; i < frame.bgra.size(); i += 4) {
    frame.bgra[i + 0] = 30;
    frame.bgra[i + 1] = 60;
    frame.bgra[i + 2] = 90;
    frame.bgra[i + 3] = 255;
  }

  int out_h = 0;
  auto out = xg::downscale_box(frame, 20, &out_h);
  REQUIRE(out_h == 10);
  REQUIRE(out.size() == 20 * 10 * 4);

  for (size_t i = 0; i < out.size(); i += 4) {
    CHECK(out[i + 0] == 30);
    CHECK(out[i + 1] == 60);
    CHECK(out[i + 2] == 90);
    CHECK(out[i + 3] == 255);
  }
}

}
