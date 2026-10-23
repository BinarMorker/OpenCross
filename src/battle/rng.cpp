#include "battle/rng.h"

#include <algorithm>
#include <cstring>

namespace oc::battle {

// A missing table is replaced by a Fisher-Yates shuffle driven by an LCG; the start index is the battle init's `rand() % 256`.
GameRng::GameRng(u32 seed, const std::vector<u8>& table) : state_(seed ? seed : 1) {
  if (table.size() == 256) {
    std::memcpy(table_, table.data(), 256);
  } else {
    for (int i = 0; i < 256; ++i) table_[i] = static_cast<u8>(i);
    u32 lcg = seed * 2654435761u + 12345u;
    for (int i = 255; i > 0; --i) {
      lcg = lcg * 1664525u + 1013904223u;
      std::swap(table_[i], table_[(lcg >> 8) % static_cast<u32>(i + 1)]);
    }
  }
  idx_ = static_cast<u8>(libc_rand() % 256);
}

int GameRng::libc_rand() {
  state_ = state_ * 1103515245u + 12345u;
  return static_cast<int>((state_ >> 16) & 0x7FFF);
}

int GameRng::rnd(int lo, int hi) {
  ++calls_;
  lo &= 0xFFFF;
  hi &= 0xFFFF;
  if (lo == 0xFFFF) return 0xFFFF;
  if (hi == 0) return 0;
  if (lo == hi) return lo;
  int value;
  if (hi < 0x100) {
    idx_ = static_cast<u8>(idx_ + 1);
    value = table_[idx_];
  } else {
    value = libc_rand() & 0xFFFF;
  }
  return lo + value % (hi - lo + 1);
}

int ScriptedRng::rnd(int lo, int hi) {
  ++calls;
  if (!queue_.empty()) {
    const int value = queue_.front();
    queue_.pop_front();
    return std::clamp(value, lo, std::max(lo, hi));
  }
  switch (fallback_) {
    case Fallback::Hi: return hi;
    case Fallback::Mid: return (lo + hi) / 2;
    default: return lo;
  }
}

}  // namespace oc::battle
