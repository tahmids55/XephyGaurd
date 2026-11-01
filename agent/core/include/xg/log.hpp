#pragma once
#include <format>
#include <string>
#include <string_view>

namespace xg::log {

enum class Level { debug = 0, info = 1, warn = 2, error = 3 };

void init(const std::string& file_path, Level level, bool also_stderr);
void set_level(Level level);
Level parse_level(std::string_view s);
void write(Level level, std::string_view msg);

template <class... Args>
void debug(std::format_string<Args...> f, Args&&... a) { write(Level::debug, std::format(f, std::forward<Args>(a)...)); }
template <class... Args>
void info(std::format_string<Args...> f, Args&&... a) { write(Level::info, std::format(f, std::forward<Args>(a)...)); }
template <class... Args>
void warn(std::format_string<Args...> f, Args&&... a) { write(Level::warn, std::format(f, std::forward<Args>(a)...)); }
template <class... Args>
void error(std::format_string<Args...> f, Args&&... a) { write(Level::error, std::format(f, std::forward<Args>(a)...)); }

}  // namespace xg::log
