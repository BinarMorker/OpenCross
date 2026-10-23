#pragma once
// Scoped phase timer: logs "<what>: N.N ms" at debug level when it goes out of scope, and optionally adds to an accumulator.
#include <chrono>

#include "core/log.h"

namespace oc {

class ScopedTimer {
 public:
  explicit ScopedTimer(const char* what, double* accum_ms = nullptr, bool print = true)
      : what_(what), accum_(accum_ms), print_(print), t0_(std::chrono::steady_clock::now()) {}
  ~ScopedTimer() {
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0_).count();
    if (accum_) *accum_ += ms;
    if (print_) log::debug("timing: {}: {:.1f} ms", what_, ms);
  }
  ScopedTimer(const ScopedTimer&) = delete;
  ScopedTimer& operator=(const ScopedTimer&) = delete;

 private:
  const char* what_;
  double* accum_;
  bool print_;
  std::chrono::steady_clock::time_point t0_;
};

}  // namespace oc
