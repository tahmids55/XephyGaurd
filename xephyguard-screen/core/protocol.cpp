#include "protocol.h"
#include <cstring>

namespace xg {

namespace {

inline void write_u16_le(uint8_t* dst, uint16_t val) {
  dst[0] = static_cast<uint8_t>(val & 0xFF);
  dst[1] = static_cast<uint8_t>((val >> 8) & 0xFF);
}

inline void write_u32_le(uint8_t* dst, uint32_t val) {
  dst[0] = static_cast<uint8_t>(val & 0xFF);
  dst[1] = static_cast<uint8_t>((val >> 8) & 0xFF);
  dst[2] = static_cast<uint8_t>((val >> 16) & 0xFF);
  dst[3] = static_cast<uint8_t>((val >> 24) & 0xFF);
}

inline uint16_t read_u16_le(const uint8_t* src) {
  return static_cast<uint16_t>(src[0]) |
         (static_cast<uint16_t>(src[1]) << 8);
}

inline uint32_t read_u32_le(const uint8_t* src) {
  return static_cast<uint32_t>(src[0]) |
         (static_cast<uint32_t>(src[1]) << 8) |
         (static_cast<uint32_t>(src[2]) << 16) |
         (static_cast<uint32_t>(src[3]) << 24);
}

}  // namespace

std::vector<uint8_t> encode(const Message& m) {
  size_t total_size = kHeaderSize;
  for (const auto& t : m.tiles) {
    total_size += kTileHeaderSize + t.payload.size();
  }

  std::vector<uint8_t> buf(total_size);
  uint8_t* ptr = buf.data();

  // Header (28 bytes)
  write_u16_le(ptr + 0, kMagic);
  ptr[2] = kVersion;
  ptr[3] = static_cast<uint8_t>(m.type);
  write_u16_le(ptr + 4, m.flags);
  write_u16_le(ptr + 6, m.monitor_id);
  write_u32_le(ptr + 8, m.stream_id);
  write_u32_le(ptr + 12, m.seq);
  write_u32_le(ptr + 16, m.base_seq);
  write_u16_le(ptr + 20, m.width);
  write_u16_le(ptr + 22, m.height);
  write_u16_le(ptr + 24, static_cast<uint16_t>(m.tiles.size()));
  write_u16_le(ptr + 26, 0);  // reserved
  ptr += kHeaderSize;

  // Tiles
  for (const auto& t : m.tiles) {
    write_u16_le(ptr + 0, t.x);
    write_u16_le(ptr + 2, t.y);
    ptr[4] = static_cast<uint8_t>(t.codec);
    ptr[5] = 0;  // reserved
    write_u32_le(ptr + 6, static_cast<uint32_t>(t.payload.size()));
    ptr += kTileHeaderSize;

    if (!t.payload.empty()) {
      std::memcpy(ptr, t.payload.data(), t.payload.size());
      ptr += t.payload.size();
    }
  }

  return buf;
}

std::optional<Message> decode(const uint8_t* data, size_t len) {
  if (data == nullptr || len < kHeaderSize) {
    return std::nullopt;
  }

  const uint16_t magic = read_u16_le(data + 0);
  if (magic != kMagic) {
    return std::nullopt;
  }

  const uint8_t version = data[2];
  if (version != kVersion) {
    return std::nullopt;
  }

  const uint8_t raw_type = data[3];
  if (raw_type < 1 || raw_type > 5) {
    return std::nullopt;
  }

  Message m;
  m.type = static_cast<MsgType>(raw_type);
  m.flags = read_u16_le(data + 4);
  m.monitor_id = read_u16_le(data + 6);
  m.stream_id = read_u32_le(data + 8);
  m.seq = read_u32_le(data + 12);
  m.base_seq = read_u32_le(data + 16);
  m.width = read_u16_le(data + 20);
  m.height = read_u16_le(data + 22);
  const uint16_t tile_count = read_u16_le(data + 24);

  if (tile_count > kMaxTiles) {
    return std::nullopt;
  }

  size_t offset = kHeaderSize;
  m.tiles.reserve(tile_count);

  for (uint16_t i = 0; i < tile_count; ++i) {
    if (offset + kTileHeaderSize > len) {
      return std::nullopt;
    }

    const uint8_t* tptr = data + offset;
    const uint16_t tx = read_u16_le(tptr + 0);
    const uint16_t ty = read_u16_le(tptr + 2);
    const uint8_t raw_codec = tptr[4];
    if (raw_codec > 2) {
      return std::nullopt;
    }
    const uint32_t payload_len = read_u32_le(tptr + 6);
    if (payload_len > kMaxTilePayload) {
      return std::nullopt;
    }

    offset += kTileHeaderSize;
    if (offset + payload_len > len) {
      return std::nullopt;
    }

    TileRec rec;
    rec.x = tx;
    rec.y = ty;
    rec.codec = static_cast<Codec>(raw_codec);
    rec.payload.resize(payload_len);
    if (payload_len > 0) {
      std::memcpy(rec.payload.data(), data + offset, payload_len);
      offset += payload_len;
    }
    m.tiles.push_back(std::move(rec));
  }

  return m;
}

}  // namespace xg
