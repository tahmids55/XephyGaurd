#include "capabilities.h"
#include <cstdlib>
#include <sstream>

namespace xg {

Capabilities query_capabilities(std::function<const char*(const char*)> env_getter) {
  Capabilities caps;

  auto get_env = [&](const char* var) -> const char* {
    if (env_getter) {
      return env_getter(var);
    }
    return std::getenv(var);
  };

#if defined(_WIN32)
  caps.os = "windows";
  caps.session_type = "desktop";
  caps.snapshot = true;
  caps.live = true;
  caps.delta = true;
  caps.multi_monitor = true;
  caps.secure_desktop = false;
  caps.silent_capture = true;
#else
  caps.os = "linux";
  const char* session = get_env("XDG_SESSION_TYPE");
  if (session && std::string(session) == "wayland") {
    caps.session_type = "wayland";
    caps.snapshot = false;
    caps.live = false;
    caps.delta = false;
    caps.multi_monitor = false;
    caps.secure_desktop = false;
    caps.silent_capture = false;
  } else {
    caps.session_type = "x11";
    caps.snapshot = true;
    caps.live = true;
    caps.delta = true;
    caps.multi_monitor = true;
    caps.secure_desktop = false;
    caps.silent_capture = true;
  }
#endif

  // If env_getter explicitly specified wayland regardless of host OS (for testing)
  const char* session_override = get_env("XDG_SESSION_TYPE");
  if (session_override && std::string(session_override) == "wayland") {
    caps.session_type = "wayland";
    caps.snapshot = false;
    caps.live = false;
    caps.delta = false;
    caps.multi_monitor = false;
    caps.secure_desktop = false;
    caps.silent_capture = false;
  }

  return caps;
}

std::string to_json(const Capabilities& caps) {
  std::ostringstream ss;
  ss << "{"
     << "\"os\":\"" << caps.os << "\","
     << "\"session_type\":\"" << caps.session_type << "\","
     << "\"snapshot\":" << (caps.snapshot ? "true" : "false") << ","
     << "\"live\":" << (caps.live ? "true" : "false") << ","
     << "\"delta\":" << (caps.delta ? "true" : "false") << ","
     << "\"multi_monitor\":" << (caps.multi_monitor ? "true" : "false") << ","
     << "\"secure_desktop\":" << (caps.secure_desktop ? "true" : "false") << ","
     << "\"silent_capture\":" << (caps.silent_capture ? "true" : "false")
     << "}";
  return ss.str();
}

}  // namespace xg
