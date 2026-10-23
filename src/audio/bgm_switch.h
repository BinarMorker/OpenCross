#pragma once
// Live BGM source switch (remastered HCA <-> original AKAO): pure position mapping. A song is described by its intro length and its
// loop length in seconds on each source; the position is carried as "inside the intro" or "phase inside the loop" and rescaled by the
// two sources' lengths (the arrangements differ slightly in tempo).
#include <algorithm>
#include <cmath>

#include "core/types.h"

namespace oc::audio {

struct SongTiming {
  double intro = 0;   // seconds before the loop body starts
  double loop = 0;    // loop body length in seconds, 0 = the song plays once
  double length = 0;  // AKAO one-shots: seconds until the last channel ends (informational)
  bool looping() const { return loop > 0; }
};

// SEAD material loop points (loop_end inclusive; 0/0 = plays once).
constexpr SongTiming hca_timing(u32 loop_start, u32 loop_end, u32 rate) {
  if (!(loop_end > loop_start) || rate == 0) return {};
  return {double(loop_start) / rate, double(loop_end + 1 - loop_start) / rate, 0};
}

// The two sources play the same composition from the same start, so the unrolled timeline (seconds since the song began, across loops)
// carries over; it is only scaled by the arrangements' loop lengths (the tempo ratio). The AKAO "intro" of measure_timing is the first
// recurring sequencer state, not necessarily the musical loop start (many songs play a different first pass), so only the loop LENGTH is
// used from it. `seconds` = unrolled seconds on `from`, result = unrolled seconds on `to`. Jingles / unmeasured songs keep the
// elapsed time, as does a loop-length ratio outside (0.5, 2), which means a failed measurement.
inline double map_song_position(double seconds, const SongTiming& from, const SongTiming& to) {
  seconds = std::max(0.0, seconds);
  if (!from.looping() || !to.looping()) return seconds;
  const double ratio = to.loop / from.loop;
  return ratio > 0.5 && ratio < 2.0 ? seconds * ratio : seconds;
}

// Unrolled time -> position inside a looping stream (HCA): past the loop end it wraps to the loop body.
inline double stream_position(double seconds, const SongTiming& timing) {
  seconds = std::max(0.0, seconds);
  if (!timing.looping() || seconds < timing.intro + timing.loop) return seconds;
  return timing.intro + std::fmod(seconds - timing.intro, timing.loop);
}

}  // namespace oc::audio
