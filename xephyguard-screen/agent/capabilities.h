#pragma once
#include <string>
#include <functional>

namespace xg {

struct Capabilities {
  std::string os;
  std::string session_type;
  bool snapshot = true;
  bool live = true;
  bool delta = true;
  bool multi_monitor = true;
  bool secure_desktop = false;
  bool silent_capture = true;
};

// Queries platform capabilities.
// An optional env_getter can be provided for testing environment variable lookups.
Capabilities query_capabilities(std::function<const char*(const char*)> env_getter = nullptr);

// Serializes capabilities to a compact JSON string.
std::string to_json(const Capabilities& caps);

}  // namespace xg
