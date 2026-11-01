#pragma once
// JSON-lines protocol between the session helper and the agent service.
//
// helper -> service:
//   {"t":"hello","v":1,"user":"alice","session":"x11","display":":0","caps":{"window":true,"clipboard":true,"keys":false}}
//   {"t":"window","app":"code","title":"main.cpp - lab3","pid":1234}
//   {"t":"paste","copy":false,"chars":812,"lines":31,"code":true,"score":0.83,"src":"firefox","dst":"code"}
//   {"t":"keys", ...KeyStatsWindow...}           (Windows only; Linux service reads evdev itself)
//   {"t":"paste_chord","ts":...}                 (Ctrl+V / Shift+Insert observed — Windows hook)
//   {"t":"ping"}
// service -> helper:
//   {"t":"config","title_capture":"app_only","collect":{"window":true,"clipboard":true,"keys":false},"mode":"test"}
//   {"t":"indicator","text":"Monitored by XephyGuard","mode":"test"}
//   {"t":"notify","title":"...","body":"...","urgent":true}
//   {"t":"pong"}
#include <string>

#include "xg/event.hpp"

namespace xg::ipc {

inline constexpr int kVersion = 1;
inline constexpr size_t kMaxLine = 16 * 1024;

inline std::string line(const json& j) { return j.dump() + "\n"; }

}  // namespace xg::ipc
