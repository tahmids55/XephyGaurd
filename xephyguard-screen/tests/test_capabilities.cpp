#include "doctest.h"
#include "capabilities.h"
#include <cstring>

TEST_SUITE("Capabilities") {

TEST_CASE("Capabilities: Wayland session sets capture to false") {
  auto mock_wayland_env = [](const char* name) -> const char* {
    if (std::strcmp(name, "XDG_SESSION_TYPE") == 0) {
      return "wayland";
    }
    return nullptr;
  };

  auto caps = xg::query_capabilities(mock_wayland_env);
  CHECK(caps.session_type == "wayland");
  CHECK(caps.silent_capture == false);
  CHECK(caps.live == false);
  CHECK(caps.snapshot == false);
  CHECK(caps.delta == false);
  CHECK(caps.multi_monitor == false);

  std::string json = xg::to_json(caps);
  CHECK(json.find("\"session_type\":\"wayland\"") != std::string::npos);
  CHECK(json.find("\"live\":false") != std::string::npos);
  CHECK(json.find("\"snapshot\":false") != std::string::npos);
  CHECK(json.find("\"silent_capture\":false") != std::string::npos);
}

TEST_CASE("Capabilities: Non-Wayland environment enables capture") {
  auto mock_x11_env = [](const char* name) -> const char* {
    if (std::strcmp(name, "XDG_SESSION_TYPE") == 0) {
      return "x11";
    }
    return nullptr;
  };

  auto caps = xg::query_capabilities(mock_x11_env);
  CHECK(caps.session_type != "wayland");
  CHECK(caps.live == true);
  CHECK(caps.snapshot == true);
  CHECK(caps.silent_capture == true);

  std::string json = xg::to_json(caps);
  CHECK(json.find("\"live\":true") != std::string::npos);
  CHECK(json.find("\"snapshot\":true") != std::string::npos);
}

}
