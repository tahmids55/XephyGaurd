#include "protocol.h"
#include <algorithm>
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
  const bool v2 = m.version == kVersion2;
  const size_t rec_header = v2 ? kRectHeaderSize : kTileHeaderSize;
  size_t total_size = kHeaderSize + m.monitors.size() * kMonitorRecSize;
  for (const auto& t : m.tiles) {
    total_size += rec_header + t.payload.size();
  }

  std::vector<uint8_t> buf(total_size);
  uint8_t* ptr = buf.data();

  // Header (28 bytes)
  write_u16_le(ptr + 0, kMagic);
  ptr[2] = v2 ? kVersion2 : kVersion;
  ptr[3] = static_cast<uint8_t>(m.type);
  write_u16_le(ptr + 4, m.flags);
  write_u16_le(ptr + 6, m.monitor_id);
  write_u32_le(ptr + 8, m.stream_id);
  write_u32_le(ptr + 12, m.seq);
  write_u32_le(ptr + 16, m.base_seq);
  write_u16_le(ptr + 20, m.width);
  write_u16_le(ptr + 22, m.height);
  write_u16_le(ptr + 24, static_cast<uint16_t>(m.type == MsgType::Monitors ? m.monitors.size() : m.tiles.size()));
  write_u16_le(ptr + 26, 0);  // reserved
  ptr += kHeaderSize;

  for (const auto& r : m.monitors) {
    write_u16_le(ptr + 0, r.x);
    write_u16_le(ptr + 2, r.y);
    write_u16_le(ptr + 4, r.w);
    write_u16_le(ptr + 6, r.h);
    ptr += kMonitorRecSize;
  }

  // Tiles
  for (const auto& t : m.tiles) {
    write_u16_le(ptr + 0, t.x);
    write_u16_le(ptr + 2, t.y);
    if (v2) {
      write_u16_le(ptr + 4, t.w);
      write_u16_le(ptr + 6, t.h);
      ptr[8] = static_cast<uint8_t>(t.codec);
      ptr[9] = 0;  // reserved
      write_u32_le(ptr + 10, static_cast<uint32_t>(t.payload.size()));
    } else {
      ptr[4] = static_cast<uint8_t>(t.codec);
      ptr[5] = 0;  // reserved
      write_u32_le(ptr + 6, static_cast<uint32_t>(t.payload.size()));
    }
    ptr += rec_header;

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
  if (version != kVersion && version != kVersion2) {
    return std::nullopt;
  }
  const bool v2 = version == kVersion2;

  const uint8_t raw_type = data[3];
  if (raw_type < 1 || raw_type > 6) {
    return std::nullopt;
  }

  Message m;
  m.version = version;
  m.type = static_cast<MsgType>(raw_type);
  m.flags = read_u16_le(data + 4);
  m.monitor_id = read_u16_le(data + 6);
  m.stream_id = read_u32_le(data + 8);
  m.seq = read_u32_le(data + 12);
  m.base_seq = read_u32_le(data + 16);
  m.width = read_u16_le(data + 20);
  m.height = read_u16_le(data + 22);
  const uint16_t tile_count = read_u16_le(data + 24);

  if (m.type == MsgType::Monitors) {  // a list of monitor rectangles, v1 only
    if (v2 || tile_count > kMaxMonitors || len != kHeaderSize + static_cast<size_t>(tile_count) * kMonitorRecSize) {
      return std::nullopt;
    }
    for (uint16_t i = 0; i < tile_count; ++i) {
      const uint8_t* r = data + kHeaderSize + i * kMonitorRecSize;
      MonitorRec rec{read_u16_le(r), read_u16_le(r + 2), read_u16_le(r + 4), read_u16_le(r + 6)};
      if (rec.w == 0 || rec.h == 0) return std::nullopt;
      m.monitors.push_back(rec);
    }
    return m;
  }
  if (tile_count > kMaxTiles) {
    return std::nullopt;
  }
  // v2 carries rectangles for Frame messages only; every other type is a bare header.
  if (v2 && m.type != MsgType::Frame && tile_count != 0) {
    return std::nullopt;
  }
  const size_t rec_header = v2 ? kRectHeaderSize : kTileHeaderSize;

  size_t offset = kHeaderSize;
  m.tiles.reserve(std::min<size_t>(tile_count, (len - kHeaderSize) / rec_header));  // a lying count cannot force a big allocation

  for (uint16_t i = 0; i < tile_count; ++i) {
    if (len - offset < rec_header) {
      return std::nullopt;
    }

    const uint8_t* tptr = data + offset;
    TileRec rec;
    rec.x = read_u16_le(tptr + 0);
    rec.y = read_u16_le(tptr + 2);
    uint8_t raw_codec;
    uint32_t payload_len;
    uint32_t max_payload;
    if (v2) {
      rec.w = read_u16_le(tptr + 4);
      rec.h = read_u16_le(tptr + 6);
      if (rec.w < 1 || rec.h < 1 || rec.w > kMaxRectSide || rec.h > kMaxRectSide) {
        return std::nullopt;
      }
      if (static_cast<uint32_t>(rec.x) + rec.w > m.width || static_cast<uint32_t>(rec.y) + rec.h > m.height) {
        return std::nullopt;
      }
      raw_codec = tptr[8];
      payload_len = read_u32_le(tptr + 10);
      max_payload = kMaxRectPayload;
    } else {
      raw_codec = tptr[4];
      payload_len = read_u32_le(tptr + 6);
      max_payload = kMaxTilePayload;
    }
    if (raw_codec > 2 || payload_len > max_payload) {
      return std::nullopt;
    }
    // A raw rectangle is exactly w*h BGRA pixels.
    if (v2 && raw_codec == 0 && payload_len != static_cast<uint32_t>(rec.w) * rec.h * 4) {
      return std::nullopt;
    }

    offset += rec_header;
    if (len - offset < payload_len) {
      return std::nullopt;
    }

    rec.codec = static_cast<Codec>(raw_codec);
    rec.payload.assign(data + offset, data + offset + payload_len);
    offset += payload_len;
    m.tiles.push_back(std::move(rec));
  }

  return m;
}

}  // namespace xg
