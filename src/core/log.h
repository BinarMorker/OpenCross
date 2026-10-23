#pragma once
// Minimal logging: every message goes to stderr; init() additionally writes them to a file until close().
#include <format>
#include <string_view>

namespace oc::log {

enum class Level { Debug, Info, Warn, Error };

void init(std::string_view file_path, bool append = false);
void close();
void write(Level level, std::string_view msg);

template <typename... Args>
void debug(std::format_string<Args...> fmt, Args&&... args) {
  write(Level::Debug, std::format(fmt, std::forward<Args>(args)...));
}
template <typename... Args>
void info(std::format_string<Args...> fmt, Args&&... args) {
  write(Level::Info, std::format(fmt, std::forward<Args>(args)...));
}
template <typename... Args>
void warn(std::format_string<Args...> fmt, Args&&... args) {
  write(Level::Warn, std::format(fmt, std::forward<Args>(args)...));
}
template <typename... Args>
void error(std::format_string<Args...> fmt, Args&&... args) {
  write(Level::Error, std::format(fmt, std::forward<Args>(args)...));
}

}  // namespace oc::log
