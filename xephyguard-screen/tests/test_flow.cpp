#include "../third_party/doctest.h"
#include "../core/flow.h"

TEST_CASE("FlowControl: Max 3 in flight limits can_send") {
  xg::FlowControl fc(3);
  uint64_t t = 1000;

  CHECK(fc.can_send(t));
  fc.on_sent(1, t);
  CHECK(fc.needs_keyframe());
  fc.note_keyframe_sent();
  CHECK_FALSE(fc.needs_keyframe());

  t += 300;
  CHECK(fc.can_send(t));
  fc.on_sent(2, t);

  t += 300;
  CHECK(fc.can_send(t));
  fc.on_sent(3, t);

  // 3 in flight -> cannot send
  t += 300;
  CHECK_FALSE(fc.can_send(t));

  // Ack one -> advance past target interval -> can send again
  fc.on_ack(1, t);
  t += fc.target_interval_ms();
  CHECK(fc.can_send(t));
}

TEST_CASE("FlowControl: Minimum target interval clamps between 200 and 1000 ms") {
  xg::FlowControl fc(3);
  uint64_t t = 1000;

  // Very fast RTT (50 ms) -> 2 * 50 = 100 ms, clamped up to 200 ms
  fc.on_sent(1, t);
  fc.on_ack(1, t + 50);
  CHECK(fc.target_interval_ms() == 200);

  // Moderate RTT (200 ms) -> 2 * 200 = 400 ms
  // With EWMA 0.2: new_rtt = 0.2 * 200 + 0.8 * 50 = 80 ms -> clamped 200
  // Push several samples to move EWMA up
  for (uint32_t s = 2; s <= 20; ++s) {
    t += 300;
    fc.on_sent(s, t);
    fc.on_ack(s, t + 250);
  }
  // 250 ms RTT -> 2 * 250 = 500 ms
  CHECK(fc.target_interval_ms() >= 450);
  CHECK(fc.target_interval_ms() <= 550);

  // Huge RTT (1000 ms) -> 2 * 1000 = 2000 ms, clamped down to 1000 ms
  for (uint32_t s = 21; s <= 40; ++s) {
    t += 1200;
    fc.on_sent(s, t);
    fc.on_ack(s, t + 1000);
  }
  CHECK(fc.target_interval_ms() == 1000);
}

TEST_CASE("FlowControl: Quality drops on max in-flight and rises on 5 clean acks") {
  xg::FlowControl fc(3);
  uint64_t t = 1000;

  CHECK(fc.quality_scale() == doctest::Approx(1.0));

  // Fill in-flight to 3
  fc.on_sent(1, t);
  t += 200;
  fc.on_sent(2, t);
  t += 200;
  fc.on_sent(3, t);  // Hits max! Quality drops by 0.1
  CHECK(fc.quality_scale() == doctest::Approx(0.9));

  // Ack 1, send 4 -> hits max again
  fc.on_ack(1, t);
  t += 200;
  fc.on_sent(4, t);  // Hits max! Quality drops to 0.8
  CHECK(fc.quality_scale() == doctest::Approx(0.8));

  // Ack outstanding packets so we are below max
  fc.on_ack(2, t);
  fc.on_ack(3, t);
  fc.on_ack(4, t);

  // 5 clean acks without hitting max -> rises by 0.05 to 0.85
  for (uint32_t i = 1; i <= 5; ++i) {
    t += 250;
    fc.on_sent(10 + i, t);
    fc.on_ack(10 + i, t + 50);
  }
  CHECK(fc.quality_scale() == doctest::Approx(0.85));

  // Clamp bottom to 0.3
  for (int i = 0; i < 20; ++i) {
    fc.on_sent(100 + i, t);
  }
  CHECK(fc.quality_scale() >= 0.3);
  CHECK(fc.quality_scale() == doctest::Approx(0.3));
}

TEST_CASE("FlowControl: reset sets needs_keyframe until note_keyframe_sent") {
  xg::FlowControl fc(3);
  CHECK(fc.needs_keyframe());
  fc.note_keyframe_sent();
  CHECK_FALSE(fc.needs_keyframe());

  fc.reset();
  CHECK(fc.needs_keyframe());
  fc.note_keyframe_sent();
  CHECK_FALSE(fc.needs_keyframe());
}
