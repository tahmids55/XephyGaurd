// core/protocol.h (FIXED)
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include <optional>

namespace xg {
constexpr uint16_t kMagic = 0x5847;
constexpr uint8_t  kVersion = 1;
constexpr uint8_t  kVersion2 = 2;  // Frame records are pixel rectangles (x,y,w,h)
constexpr int      kTile = 64;
constexpr size_t   kHeaderSize = 28;
constexpr size_t   kTileHeaderSize = 10;
constexpr uint32_t kMaxTilePayload = 64 * 64 * 4;
constexpr uint16_t kMaxTiles = 16384;  // 64 px tiles: 8K is 8160, three 4K monitors side by side 6120
constexpr size_t   kMonitorRecSize = 8;   // Monitors message record: x, y, w, h as u16 (pixels in the whole-desktop picture)
constexpr size_t   kMaxMonitors = 16;
constexpr size_t   kRectHeaderSize = 14;
constexpr int      kMaxRectSide = 256;
constexpr uint32_t kMaxRectPayload = 256 * 256 * 4;

enum class MsgType : uint8_t { Frame=1, Cursor=2, Ack=3, KeyframeReq=4, Status=5, Monitors=6 };
// CACHED is set only by the gateway on a replayed cached keyframe (never by an agent).
enum Flags : uint16_t { KEY=1, FULL=2, DELTA=4, MASKED=8, SECURE=16, CURSOR_F=32, CACHED=64, ALL_SCREENS=128 };
// ALL_SCREENS is set only on a Monitors message: the agent can also show the whole desktop (KeyframeReq monitor_id 0xFFFF).
enum class Codec : uint8_t { Raw=0, Webp=1, Jpeg=2 };

// v1: x,y are tile indices (64 px grid), w=h=0. v2: x,y,w,h are pixels (w,h 1..256, inside the frame).
struct TileRec { uint16_t x, y; Codec codec; std::vector<uint8_t> payload; uint16_t w = 0, h = 0; };

struct MonitorRec { uint16_t x, y, w, h; };

struct Message {
  uint8_t version = kVersion;  // kVersion (tiles) or kVersion2 (rectangles; Frame messages only)
  MsgType type; uint16_t flags; uint16_t monitor_id; uint32_t stream_id;
  uint32_t seq; uint32_t base_seq; uint16_t width, height;
  std::vector<TileRec> tiles;
  // Monitors message only (type 6): one record per monitor, id = position + 1. The header's monitor_id is the one
  // being shown (0 = whole desktop); the header's tile_count field carries the record count.
  std::vector<MonitorRec> monitors;
};

std::vector<uint8_t> encode(const Message& m);
// Returns nullopt on ANY malformed input. Never reads out of bounds.
std::optional<Message> decode(const uint8_t* data, size_t len);
}
