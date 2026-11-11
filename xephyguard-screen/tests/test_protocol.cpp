#include "../third_party/doctest.h"
#include "../core/protocol.h"
#include <random>

TEST_CASE("Protocol: Round trip with 0, 1, and 100 tiles") {
  for (int count : {0, 1, 100}) {
    xg::Message m;
    m.type = xg::MsgType::Frame;
    m.flags = xg::Flags::KEY | xg::Flags::FULL;
    m.monitor_id = 1;
    m.stream_id = 42;
    m.seq = 100;
    m.base_seq = 0;
    m.width = 1920;
    m.height = 1080;

    for (int i = 0; i < count; ++i) {
      xg::TileRec t;
      t.x = static_cast<uint16_t>(i % 30);
      t.y = static_cast<uint16_t>(i / 30);
      t.codec = xg::Codec::Webp;
      t.payload = {static_cast<uint8_t>(i), 0xAA, 0xBB, 0xCC};
      m.tiles.push_back(t);
    }

    auto encoded = xg::encode(m);
    auto decoded = xg::decode(encoded.data(), encoded.size());

    REQUIRE(decoded.has_value());
    CHECK(decoded->type == m.type);
    CHECK(decoded->flags == m.flags);
    CHECK(decoded->monitor_id == m.monitor_id);
    CHECK(decoded->stream_id == m.stream_id);
    CHECK(decoded->seq == m.seq);
    CHECK(decoded->base_seq == m.base_seq);
    CHECK(decoded->width == m.width);
    CHECK(decoded->height == m.height);
    REQUIRE(decoded->tiles.size() == m.tiles.size());

    for (size_t i = 0; i < m.tiles.size(); ++i) {
      CHECK(decoded->tiles[i].x == m.tiles[i].x);
      CHECK(decoded->tiles[i].y == m.tiles[i].y);
      CHECK(decoded->tiles[i].codec == m.tiles[i].codec);
      CHECK(decoded->tiles[i].payload == m.tiles[i].payload);
    }
  }
}

TEST_CASE("Protocol: Decode returns nullopt on malformed inputs") {
  xg::Message m;
  m.type = xg::MsgType::Frame;
  m.flags = xg::Flags::KEY;
  m.monitor_id = 0;
  m.stream_id = 1;
  m.seq = 1;
  m.base_seq = 0;
  m.width = 64;
  m.height = 64;
  xg::TileRec tr{0, 0, xg::Codec::Raw, {1, 2, 3, 4}};
  m.tiles.push_back(tr);

  auto valid = xg::encode(m);

  // 1. Truncated header
  CHECK_FALSE(xg::decode(valid.data(), xg::kHeaderSize - 1).has_value());

  // 2. Wrong magic
  auto bad_magic = valid;
  bad_magic[0] = 0x00;
  CHECK_FALSE(xg::decode(bad_magic.data(), bad_magic.size()).has_value());

  // 3. Wrong version
  auto bad_ver = valid;
  bad_ver[2] = 2;
  CHECK_FALSE(xg::decode(bad_ver.data(), bad_ver.size()).has_value());

  // 4. Tile count larger than data
  auto big_tile_count = valid;
  big_tile_count[24] = 0xFF;
  big_tile_count[25] = 0x00;  // 255 tiles, but data has only 1
  CHECK_FALSE(xg::decode(big_tile_count.data(), big_tile_count.size()).has_value());

  // 5. Tile count > kMaxTiles (4096)
  auto exceed_tile_count = valid;
  exceed_tile_count[24] = 0x01;
  exceed_tile_count[25] = 0x11;  // > 4096
  CHECK_FALSE(xg::decode(exceed_tile_count.data(), exceed_tile_count.size()).has_value());

  // 6. Payload len larger than remaining bytes
  auto huge_len = valid;
  huge_len[xg::kHeaderSize + 6] = 0xFF;
  huge_len[xg::kHeaderSize + 7] = 0x00;
  CHECK_FALSE(xg::decode(huge_len.data(), huge_len.size()).has_value());

  // 7. Payload len > kMaxTilePayload (16384)
  auto exceed_payload = valid;
  exceed_payload[xg::kHeaderSize + 6] = 0x01;
  exceed_payload[xg::kHeaderSize + 7] = 0x41;  // > 16384
  CHECK_FALSE(xg::decode(exceed_payload.data(), exceed_payload.size()).has_value());
}

TEST_CASE("Protocol: Fuzzing 10,000 random buffers never crashes") {
  std::mt19937 rng(1337);
  std::uniform_int_distribution<size_t> len_dist(0, 1024);
  std::uniform_int_distribution<int> byte_dist(0, 255);

  std::vector<uint8_t> buf;
  for (int i = 0; i < 10000; ++i) {
    size_t len = len_dist(rng);
    buf.resize(len);
    for (size_t b = 0; b < len; ++b) {
      buf[b] = static_cast<uint8_t>(byte_dist(rng));
    }
    // Must never crash, must return either valid or nullopt
    auto res = xg::decode(buf.data(), buf.size());
    (void)res;
  }
}

TEST_CASE("Protocol: Mutation tests with truncation and bit flips") {
  xg::Message m;
  m.type = xg::MsgType::Frame;
  m.flags = xg::Flags::DELTA;
  m.monitor_id = 0;
  m.stream_id = 99;
  m.seq = 50;
  m.base_seq = 49;
  m.width = 1280;
  m.height = 720;
  for (uint16_t i = 0; i < 10; ++i) {
    m.tiles.push_back({i, i, xg::Codec::Webp, std::vector<uint8_t>(32, static_cast<uint8_t>(i))});
  }
  auto valid = xg::encode(m);

  // Truncate at every length
  for (size_t len = 0; len <= valid.size(); ++len) {
    auto res = xg::decode(valid.data(), len);
    if (len < valid.size()) {
      CHECK_FALSE(res.has_value());
    } else {
      CHECK(res.has_value());
    }
  }

  // Bit flip mutations
  std::mt19937 rng(42);
  std::uniform_int_distribution<size_t> idx_dist(0, valid.size() - 1);
  std::uniform_int_distribution<int> bit_dist(0, 7);

  for (int iter = 0; iter < 1000; ++iter) {
    auto mutated = valid;
    for (int flip = 0; flip < 3; ++flip) {
      size_t byte_idx = idx_dist(rng);
      mutated[byte_idx] ^= static_cast<uint8_t>(1 << bit_dist(rng));
    }
    auto res = xg::decode(mutated.data(), mutated.size());
    (void)res;  // never crashes
  }
}
