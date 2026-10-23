#include "core/log.h"

#include <cstdio>
#include <mutex>
#include <string>

namespace oc::log {

namespace {
std::FILE* g_file = nullptr;
std::mutex g_mutex;

const char* prefix(Level level) {
  switch (level) {
    case Level::Debug: return "D";
    case Level::Info: return "I";
    case Level::Warn: return "W";
    case Level::Error: return "E";
  }
  return "?";
}
}  // namespace

void init(std::string_view file_path, bool append) {
  std::lock_guard lock(g_mutex);
  if (g_file) std::fclose(g_file);
  g_file = std::fopen(std::string(file_path).c_str(), append ? "a" : "w");
}

void close() {
  std::lock_guard lock(g_mutex);
  if (g_file) std::fclose(g_file);
  g_file = nullptr;
}

void write(Level level, std::string_view msg) {
  std::lock_guard lock(g_mutex);
  std::fprintf(stderr, "[%s] %.*s\n", prefix(level), static_cast<int>(msg.size()), msg.data());
  if (g_file) {
    std::fprintf(g_file, "[%s] %.*s\n", prefix(level), static_cast<int>(msg.size()), msg.data());
    std::fflush(g_file);
  }
}

}  // namespace oc::log
