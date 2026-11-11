// core/protocol.h (FIXED)
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include <optional>

namespace xg {
constexpr uint16_t kMagic = 0x5847;
constexpr uint8_t  kVersion = 1;
constexpr int      kTile = 64;
constexpr size_t   kHeaderSize = 28;
constexpr size_t   kTileHeaderSize = 10;
constexpr uint32_t kMaxTilePayload = 64 * 64 * 4;
constexpr uint16_t kMaxTiles = 4096;

enum class MsgType : uint8_t { Frame=1, Cursor=2, Ack=3, KeyframeReq=4, Status=5 };
enum Flags : uint16_t { KEY=1, FULL=2, DELTA=4, MASKED=8, SECURE=16, CURSOR_F=32 };
enum class Codec : uint8_t { Raw=0, Webp=1, Jpeg=2 };

struct TileRec { uint16_t x, y; Codec codec; std::vector<uint8_t> payload; };

struct Message {
  MsgType type; uint16_t flags; uint16_t monitor_id; uint32_t stream_id;
  uint32_t seq; uint32_t base_seq; uint16_t width, height;
  std::vector<TileRec> tiles;
};

std::vector<uint8_t> encode(const Message& m);
// Returns nullopt on ANY malformed input. Never reads out of bounds.
std::optional<Message> decode(const uint8_t* data, size_t len);
}
