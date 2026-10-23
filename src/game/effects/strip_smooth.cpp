#include "game/effects/strip_smooth.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <tuple>

namespace oc::effects {
namespace {

// Vertices are matched by their position quantised to 1/64 units.
using Key = std::tuple<long, long, long>;
Key key_of(const float pos[3]) { return {std::lround(pos[0] * 64.0f), std::lround(pos[1] * 64.0f), std::lround(pos[2] * 64.0f)}; }

SmoothVert lerp(const SmoothVert& from, const SmoothVert& to, float frac) {
  SmoothVert result;
  for (int i = 0; i < 3; ++i) {
    result.p[i] = from.p[i] + (to.p[i] - from.p[i]) * frac;
    result.c[i] = from.c[i] + (to.c[i] - from.c[i]) * frac;
  }
  return result;
}

// Uniform Catmull-Rom through p1 (frac = 0) and p2 (frac = 1).
void catmull(const float* p0, const float* p1, const float* p2, const float* p3, float frac, float* out) {
  const float frac2 = frac * frac, frac3 = frac2 * frac;
  for (int i = 0; i < 3; ++i)
    out[i] = 0.5f * (2.0f * p1[i] + (-p0[i] + p2[i]) * frac + (2.0f * p0[i] - 5.0f * p1[i] + 4.0f * p2[i] - p3[i]) * frac2 +
                     (-p0[i] + 3.0f * p1[i] - 3.0f * p2[i] + p3[i]) * frac3);
}

struct Edge {
  Key lo, hi;
  bool operator<(const Edge& other) const { return std::tie(lo, hi) < std::tie(other.lo, other.hi); }
};
Edge edge_of(const SmoothVert& first, const SmoothVert& second) {
  Key lo = key_of(first.p), hi = key_of(second.p);
  if (hi < lo) std::swap(lo, hi);
  return {lo, hi};
}

// One quad of a strip as it was entered: `corner` = (A, B, B', A') in rung order, so A -> A' and B -> B' are the two rails and
// (A, B) / (A', B') the entering / exit rungs.
struct Step {
  int quad;
  int enter;
  SmoothVert corner[4];
  int tag;
};

}  // namespace

// 1. Every edge (quantised vertex pair) maps to the (quad, edge slot) pairs that use it; the foreign triangles register as owner -1.
// 2. A quad belongs to a strip when exactly two opposite edges are shared with one other quad each (a third shared edge, the rail a
//    multi-band ring shares with its neighbouring band, is allowed). From each unused strip quad the chain is walked through the exit
//    rung until it returns to the start through the entering slot with the same orientation (closed) or breaks.
// 3. Every closed ring projects its rails to pixels; the longest rail segment decides the subdivision count, shared by all rings.
// 4. A ring whose rail is shared with anything not itself a refined ring is dropped, iterating until stable (its neighbours too).
// 5. The surviving rings are rebuilt as `segs` sub-quads per step on a Catmull-Rom curve through the rail vertices (colours lerped).
bool smooth_quad_rings(std::vector<SmoothQuad>& quads, float max_px, const SmoothProject& project, const std::vector<SmoothVert>* foreign_tris) {
  const int quad_count = static_cast<int>(quads.size());
  if (quad_count < 3) return false;
  std::map<Edge, std::vector<std::pair<int, int>>> edges;
  for (int quad = 0; quad < quad_count; ++quad)
    for (int edge = 0; edge < 4; ++edge) edges[edge_of(quads[quad].v[edge], quads[quad].v[(edge + 1) & 3])].push_back({quad, edge});
  if (foreign_tris)
    for (size_t tri = 0; tri + 2 < foreign_tris->size(); tri += 3)
      for (int edge = 0; edge < 3; ++edge) edges[edge_of((*foreign_tris)[tri + edge], (*foreign_tris)[tri + (edge + 1) % 3])].push_back({-1, 0});
  // The single other quad sharing edge slot `edge` of `quad` (-1: unshared or shared by more than two).
  auto neighbour = [&](int quad, int edge, int* other_slot) -> int {
    const auto& owners = edges[edge_of(quads[quad].v[edge], quads[quad].v[(edge + 1) & 3])];
    if (owners.size() != 2) return -1;
    const auto& other = (owners[0].first == quad && owners[0].second == edge) ? owners[1] : owners[0];
    if (other.first == quad) return -1;
    *other_slot = other.second;
    return other.first;
  };
  // Returns the lower rung slot of a strip quad (see step 2 above).
  auto strip_slot = [&](int quad, int* slot) -> bool {
    int mask = 0, shared = 0, other_slot;
    for (int edge = 0; edge < 4; ++edge)
      if (neighbour(quad, edge, &other_slot) >= 0) {
        mask |= 1 << edge;
        ++shared;
      }
    if (shared == 2 && (mask == 0x5 || mask == 0xA)) {
      *slot = mask == 0x5 ? 0 : 1;
      return true;
    }
    if (shared == 3) {
      int unshared = 0;
      while (mask & (1 << unshared)) ++unshared;
      *slot = (unshared + 1) & 1;
      return true;
    }
    return false;
  };

  std::vector<char> used(quad_count, 0), drop(quad_count, 0);
  std::vector<SmoothQuad> added;
  std::vector<std::vector<Step>> rings;
  int segs = 1;
  for (int start = 0; start < quad_count; ++start) {
    int start_slot;
    if (used[start] || !strip_slot(start, &start_slot)) continue;
    std::vector<Step> ring;
    int quad = start, enter = start_slot;
    bool fwd = true, closed = false;
    for (int guard = 0; guard <= quad_count; ++guard) {
      Step step;
      step.quad = quad;
      step.enter = enter;
      step.tag = quads[quad].tag;
      for (int i = 0; i < 4; ++i) step.corner[i] = fwd ? quads[quad].v[(enter + i) & 3] : quads[quad].v[(enter + 1 - i + 4) & 3];
      ring.push_back(step);
      int other_enter = 0;
      const int next = neighbour(quad, (enter + 2) & 3, &other_enter);
      if (next < 0 || used[next]) break;
      if (next == start) {
        closed = other_enter == start_slot && key_of(quads[start].v[start_slot].p) == key_of(step.corner[3].p) && ring.size() >= 3;
        break;
      }
      int dummy;
      if (!strip_slot(next, &dummy)) break;
      const Key exit_key = key_of(step.corner[3].p);
      const bool next_fwd = key_of(quads[next].v[other_enter].p) == exit_key;
      if (!next_fwd && key_of(quads[next].v[(other_enter + 1) & 3].p) != exit_key) break;
      quad = next;
      enter = other_enter;
      fwd = next_fwd;
    }
    if (!closed) continue;
    for (const Step& step : ring) used[step.quad] = 1;

    const int count = static_cast<int>(ring.size());
    float longest = 0;
    bool ok = true;
    for (int k = 0; k < count && ok; ++k)
      for (int rail = 0; rail < 2; ++rail) {
        float px_a[2], px_b[2];
        if (!project(ring[k].corner[rail == 0 ? 0 : 1].p, px_a) || !project(ring[k].corner[rail == 0 ? 3 : 2].p, px_b)) {
          ok = false;
          break;
        }
        longest = std::max(longest, std::hypot(px_a[0] - px_b[0], px_a[1] - px_b[1]));
      }
    if (!ok || max_px <= 0) continue;
    segs = std::max(segs, std::clamp(static_cast<int>(std::ceil(longest / max_px)), 1, kMaxRingSegments));
    rings.push_back(std::move(ring));
  }
  std::vector<char> alive(rings.size(), 1);
  std::vector<int> ring_of(quad_count, -1);
  for (size_t ring_index = 0; ring_index < rings.size(); ++ring_index)
    for (const Step& step : rings[ring_index]) ring_of[step.quad] = static_cast<int>(ring_index);
  for (bool changed = true; changed;) {
    changed = false;
    for (size_t ring_index = 0; ring_index < rings.size(); ++ring_index) {
      if (!alive[ring_index]) continue;
      bool bad = false;
      for (const Step& step : rings[ring_index])
        for (int rail : {1, 3}) {
          const int edge = (step.enter + rail) & 3;
          for (const auto& owner : edges[edge_of(quads[step.quad].v[edge], quads[step.quad].v[(edge + 1) & 3])]) {
            if (owner.first == step.quad) continue;
            if (owner.first < 0 || ring_of[owner.first] < 0 || !alive[static_cast<size_t>(ring_of[owner.first])]) bad = true;
          }
        }
      if (bad) {
        alive[ring_index] = 0;
        changed = true;
      }
    }
  }
  bool any_alive = false;
  for (char flag : alive) any_alive |= flag != 0;
  if (!any_alive) return false;
  if (segs <= 1) return false;
  for (size_t ring_index = 0; ring_index < rings.size(); ++ring_index) {
    if (!alive[ring_index]) continue;
    const std::vector<Step>& ring = rings[ring_index];
    const int count = static_cast<int>(ring.size());
    for (const Step& step : ring) drop[step.quad] = 1;
    for (int k = 0; k < count; ++k) {
      const Step& cur = ring[k];
      const Step& prv = ring[(k + count - 1) % count];
      const Step& nxt = ring[(k + 1) % count];
      // Rail A runs prv.corner[0], cur.corner[0], cur.corner[3], nxt.corner[3]; rail B the same with corners 1 / 2.
      auto rail = [&](int which, float frac) {
        const int slot_start = which == 0 ? 0 : 1, slot_end = which == 0 ? 3 : 2;
        SmoothVert vert = lerp(cur.corner[slot_start], cur.corner[slot_end], frac);
        catmull(prv.corner[slot_start].p, cur.corner[slot_start].p, cur.corner[slot_end].p, nxt.corner[slot_end].p, frac, vert.p);
        return vert;
      };
      SmoothVert prev_a = rail(0, 0.0f), prev_b = rail(1, 0.0f);
      for (int i = 1; i <= segs; ++i) {
        const float frac = static_cast<float>(i) / static_cast<float>(segs);
        const SmoothVert next_a = rail(0, frac), next_b = rail(1, frac);
        SmoothQuad sub;
        sub.tag = cur.tag;
        sub.v[0] = prev_a;
        sub.v[1] = prev_b;
        sub.v[2] = next_b;
        sub.v[3] = next_a;
        added.push_back(sub);
        prev_a = next_a;
        prev_b = next_b;
      }
    }
  }
  std::vector<SmoothQuad> out;
  out.reserve(quad_count + added.size());
  for (int quad = 0; quad < quad_count; ++quad)
    if (!drop[quad]) out.push_back(quads[quad]);
  out.insert(out.end(), added.begin(), added.end());
  quads.swap(out);
  return true;
}

// Consecutive triangle pairs sharing exactly two vertices merge into one quad in loop order (the unshared vertex of the first, its
// next vertex, the unshared vertex of the second, the remaining vertex); the rest stays a triangle list that the quads must not crack
// against.
bool smooth_triangle_rings(std::vector<SmoothVert>& tri_verts, float max_px, const SmoothProject& project) {
  const size_t tri_count = tri_verts.size() / 3;
  std::vector<SmoothQuad> quads;
  std::vector<SmoothVert> rest;
  for (size_t tri = 0; tri < tri_count;) {
    bool merged = false;
    if (tri + 1 < tri_count) {
      const SmoothVert* first = &tri_verts[tri * 3];
      const SmoothVert* second = &tri_verts[(tri + 1) * 3];
      bool first_shared[3] = {}, second_shared[3] = {};
      int shared = 0;
      for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
          if (key_of(first[i].p) == key_of(second[j].p)) {
            first_shared[i] = second_shared[j] = true;
            ++shared;
          }
      if (shared == 2) {
        int unshared_first = 0, unshared_second = 0;
        while (first_shared[unshared_first]) ++unshared_first;
        while (second_shared[unshared_second]) ++unshared_second;
        SmoothQuad quad;
        quad.tag = static_cast<int>(tri);
        quad.v[0] = first[unshared_first];
        quad.v[1] = first[(unshared_first + 1) % 3];
        quad.v[2] = second[unshared_second];
        quad.v[3] = first[(unshared_first + 2) % 3];
        quads.push_back(quad);
        tri += 2;
        merged = true;
      }
    }
    if (!merged) {
      for (int i = 0; i < 3; ++i) rest.push_back(tri_verts[tri * 3 + i]);
      ++tri;
    }
  }
  if (!smooth_quad_rings(quads, max_px, project, &rest)) return false;
  static constexpr int kIdx[6] = {0, 1, 2, 0, 2, 3};
  for (const SmoothQuad& quad : quads)
    for (int i : kIdx) rest.push_back(quad.v[i]);
  tri_verts.swap(rest);
  return true;
}

}  // namespace oc::effects
