#include "../third_party/doctest.h"
#include "../core/flow.h"

TEST_CASE("FlowControl: no pacing floor, only the in-flight frame window limits can_send") {
  xg::FlowControl fc(3, 1 << 20);
  uint64_t t = 1000;
  CHECK(fc.can_send(t));
  fc.on_sent(1, t, 100);
  CHECK(fc.needs_keyframe());
  fc.note_keyframe_sent();
  CHECK_FALSE(fc.needs_keyframe());
  CHECK(fc.can_send(t + 1));  // 1 ms later: no 200 ms floor any more
  fc.on_sent(2, t + 1, 100);
  fc.on_sent(3, t + 2, 100);
  CHECK_FALSE(fc.can_send(t + 3));  // 3 frames in flight
  fc.on_ack(1, t + 40);
  CHECK(fc.can_send(t + 40));
  CHECK(fc.in_flight() == 2);
  CHECK(fc.in_flight_bytes() == 200);
}

TEST_CASE("FlowControl: the window is in bytes, but one frame always goes out when nothing is in flight") {
  xg::FlowControl fc(8, 1000);
  uint64_t t = 1000;
  fc.on_sent(1, t, 600);
  CHECK(fc.can_send(t));
  fc.on_sent(2, t, 600);  // 1200 >= 1000: full
  CHECK_FALSE(fc.can_send(t));
  fc.on_ack(1, t + 10);
  fc.on_ack(2, t + 10);
  CHECK(fc.in_flight_bytes() == 0);
  fc.on_sent(3, t + 20, 5000);  // larger than the whole window: still allowed alone
  CHECK_FALSE(fc.can_send(t + 20));
  fc.on_ack(3, t + 30);
  CHECK(fc.can_send(t + 30));
}

TEST_CASE("FlowControl: frames unacknowledged for 2 s expire, a lost Ack cannot stall the stream") {
  xg::FlowControl fc(1, 1 << 20);
  fc.on_sent(1, 1000, 10);
  CHECK_FALSE(fc.can_send(1000 + xg::FlowControl::kAckTimeoutMs - 1));
  CHECK(fc.can_send(1000 + xg::FlowControl::kAckTimeoutMs));
  CHECK(fc.in_flight_bytes() == 0);
  fc.on_ack(1, 4000);  // late Ack for an expired frame: ignored
  CHECK(fc.in_flight() == 0);
}

TEST_CASE("FlowControl: RTT is smoothed from Acks") {
  xg::FlowControl fc;
  fc.on_sent(1, 1000);
  fc.on_ack(1, 1050);
  CHECK(fc.smoothed_rtt_ms() == doctest::Approx(50.0));
  fc.on_sent(2, 2000);
  fc.on_ack(2, 2250);
  CHECK(fc.smoothed_rtt_ms() == doctest::Approx(0.2 * 250 + 0.8 * 50));
}

TEST_CASE("FlowControl: Quality drops on a full window and rises on 5 clean acks") {
  xg::FlowControl fc(3, 1 << 20);
  uint64_t t = 1000;
  CHECK(fc.quality_scale() == doctest::Approx(1.0));
  fc.on_sent(1, t);
  fc.on_sent(2, t);
  fc.on_sent(3, t);  // full: -0.1
  CHECK(fc.quality_scale() == doctest::Approx(0.9));
  fc.on_ack(1, t);
  fc.on_sent(4, t);  // full again: -0.1
  CHECK(fc.quality_scale() == doctest::Approx(0.8));
  fc.on_ack(2, t);
  fc.on_ack(3, t);
  fc.on_ack(4, t);
  for (uint32_t i = 1; i <= 5; ++i) {
    fc.on_sent(10 + i, t);
    fc.on_ack(10 + i, t + 50);
  }
  CHECK(fc.quality_scale() == doctest::Approx(0.85));
  for (int i = 0; i < 20; ++i) fc.on_sent(100 + i, t);
  CHECK(fc.quality_scale() == doctest::Approx(0.3));
}

TEST_CASE("FlowControl: reset sets needs_keyframe until note_keyframe_sent and empties the window") {
  xg::FlowControl fc(3);
  fc.on_sent(1, 1000, 500);
  CHECK(fc.needs_keyframe());
  fc.note_keyframe_sent();
  CHECK_FALSE(fc.needs_keyframe());
  fc.reset();
  CHECK(fc.needs_keyframe());
  CHECK(fc.in_flight_bytes() == 0);
}
