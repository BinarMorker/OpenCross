#pragma once
// Minimal column-major 4x4 float matrix (m[col * 4 + row]); the layout GLSL's mat4 expects.
#include <array>
#include <cmath>

#include "assets/model.h"

namespace oc::render {

struct Mat4 {
  std::array<float, 16> m{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  float& at(int row, int col) { return m[static_cast<size_t>(col * 4 + row)]; }
  float at(int row, int col) const { return m[static_cast<size_t>(col * 4 + row)]; }
};

inline Mat4 operator*(const Mat4& lhs, const Mat4& rhs) {
  Mat4 result;
  for (int col = 0; col < 4; ++col)
    for (int row = 0; row < 4; ++row) {
      float sum = 0;
      for (int k = 0; k < 4; ++k) sum += lhs.at(row, k) * rhs.at(k, col);
      result.at(row, col) = sum;
    }
  return result;
}

// Rotation (row-major 3x3, as assets::rotation_zyx returns) + translation.
inline Mat4 mat4_from_rt(const assets::Mat3& rot, const assets::Vec3& trans) {
  Mat4 out;
  for (int row = 0; row < 3; ++row) {
    for (int col = 0; col < 3; ++col) out.at(row, col) = rot[static_cast<size_t>(row * 3 + col)];
    out.at(row, 3) = trans[static_cast<size_t>(row)];
  }
  return out;
}

// Row-major 3x4 transform (rotation rows 0..8, translation 9..11) as the pipelines carry their camera transforms.
inline Mat4 mat4_from_rows(const std::array<float, 12>& rows) {
  Mat4 out;
  for (int row = 0; row < 3; ++row) {
    for (int col = 0; col < 3; ++col) out.at(row, col) = rows[static_cast<size_t>(row * 3 + col)];
    out.at(row, 3) = rows[static_cast<size_t>(9 + row)];
  }
  return out;
}

inline Mat4 mat4_translation(float x, float y, float z) {
  Mat4 out;
  out.at(0, 3) = x;
  out.at(1, 3) = y;
  out.at(2, 3) = z;
  return out;
}

inline Mat4 mat4_scale(float scale) {
  Mat4 out;
  out.at(0, 0) = out.at(1, 1) = out.at(2, 2) = scale;
  return out;
}

// Rotation about +Y by `rad` (standard right-handed matrix: x' = cos x + sin z, z' = -sin x + cos z).
inline Mat4 mat4_rotation_y(float rad) {
  Mat4 out;
  const float cos_a = std::cos(rad), sin_a = std::sin(rad);
  out.at(0, 0) = cos_a;
  out.at(0, 2) = sin_a;
  out.at(2, 0) = -sin_a;
  out.at(2, 2) = cos_a;
  return out;
}

}  // namespace oc::render
