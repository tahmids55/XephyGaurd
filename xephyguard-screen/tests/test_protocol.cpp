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

namespace {
xg::Message make_v2() {
  xg::Message m{};
  m.version = xg::kVersion2;
  m.type = xg::MsgType::Frame;
  m.flags = xg::Flags::KEY;
  m.stream_id = 7;
  m.seq = 3;
  m.width = 300;
  m.height = 200;
  xg::TileRec r;
  r.x = 256; r.y = 100; r.w = 44; r.h = 100;
  r.codec = xg::Codec::Jpeg;
  r.payload = {0xAA, 0xBB};
  m.tiles.push_back(r);
  return m;
}
}  // namespace

TEST_CASE("Protocol v2: golden bytes and round trip") {
  const std::vector<uint8_t> golden = {
      0x47, 0x58, 0x02, 0x01, 0x01, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x03, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2C, 0x01, 0xC8, 0x00, 0x01, 0x00, 0x00, 0x00,
      0x00, 0x01, 0x64, 0x00, 0x2C, 0x00, 0x64, 0x00, 0x02, 0x00, 0x02, 0x00, 0x00, 0x00, 0xAA, 0xBB};
  CHECK(xg::encode(make_v2()) == golden);
  auto d = xg::decode(golden.data(), golden.size());
  REQUIRE(d.has_value());
  CHECK(d->version == xg::kVersion2);
  REQUIRE(d->tiles.size() == 1);
  CHECK(d->tiles[0].x == 256);
  CHECK(d->tiles[0].y == 100);
  CHECK(d->tiles[0].w == 44);
  CHECK(d->tiles[0].h == 100);
  CHECK(d->tiles[0].codec == xg::Codec::Jpeg);
  CHECK(d->tiles[0].payload == std::vector<uint8_t>{0xAA, 0xBB});
}

TEST_CASE("Protocol v2: v1 still decodes and keeps version 1") {
  xg::Message m{};
  m.type = xg::MsgType::Frame;
  m.width = 128; m.height = 64;
  xg::TileRec t{1, 0, xg::Codec::Webp, {1, 2, 3}};
  m.tiles.push_back(t);
  auto e = xg::encode(m);
  CHECK(e[2] == 1);
  auto d = xg::decode(e.data(), e.size());
  REQUIRE(d.has_value());
  CHECK(d->version == xg::kVersion);
  CHECK(d->tiles[0].w == 0);
}

TEST_CASE("Protocol v2: malformed rectangles are rejected") {
  auto reject = [](const std::vector<uint8_t>& b) { return !xg::decode(b.data(), b.size()).has_value(); };
  const auto ok = xg::encode(make_v2());
  REQUIRE_FALSE(reject(ok));
  const size_t r = xg::kHeaderSize;
  auto with = [&](size_t off, uint8_t val) { auto b = ok; b[off] = val; return b; };

  CHECK(reject(with(r + 4, 0)));                            // w = 0
  CHECK(reject(with(r + 6, 0)));                            // h = 0
  { auto b = with(r + 5, 1); CHECK(reject(b)); }            // w = 300 > 256
  CHECK(reject(with(r + 4, 45)));                           // x + w = 301 > width
  CHECK(reject(with(r + 6, 101)));                          // y + h = 201 > height
  CHECK(reject(with(r + 8, 3)));                            // unknown codec
  CHECK(reject(with(r + 10, 3)));                           // payload longer than the message
  CHECK(reject(std::vector<uint8_t>(ok.begin(), ok.end() - 1)));          // truncated payload
  CHECK(reject(std::vector<uint8_t>(ok.begin(), ok.begin() + r + 10)));   // truncated record header
  CHECK(reject(with(3, 2)));                                // Cursor with rectangles in v2

  // Raw payload must be exactly w*h*4
  auto raw = make_v2();
  raw.tiles[0].codec = xg::Codec::Raw;
  raw.tiles[0].payload.assign(44 * 100 * 4, 7);
  auto rb = xg::encode(raw);
  CHECK_FALSE(reject(rb));
  raw.tiles[0].payload.pop_back();
  CHECK(reject(xg::encode(raw)));
}

TEST_CASE("Protocol v2: mutated and random buffers never crash") {
  std::mt19937 rng(4242);
  const auto base = xg::encode(make_v2());
  for (int i = 0; i < 20000; ++i) {
    auto b = base;
    const int flips = 1 + static_cast<int>(rng() % 4);
    for (int f = 0; f < flips; ++f) b[rng() % b.size()] = static_cast<uint8_t>(rng());
    if (rng() % 4 == 0) b.resize(rng() % (b.size() + 1));
    auto d = xg::decode(b.data(), b.size());
    if (d && d->version == xg::kVersion2) {
      for (const auto& t : d->tiles) {
        CHECK(t.w >= 1);
        CHECK(t.w <= xg::kMaxRectSide);
        CHECK(t.x + t.w <= d->width);
        CHECK(t.y + t.h <= d->height);
      }
    }
  }
}
