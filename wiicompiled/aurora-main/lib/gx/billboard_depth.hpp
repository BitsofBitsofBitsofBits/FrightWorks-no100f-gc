#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

namespace aurora::gx {
// Packed direct XYZ/RGBA/ST billboards and uniformly colored planar sprite quads.
inline std::vector<float> billboard_depths(std::span<const uint8_t> vertices,
                                         const std::array<float, 4>& viewDepthRow) {
  constexpr size_t stride = 24;
  constexpr size_t quadBytes = 6 * stride;
  if (vertices.empty() || vertices.size() % quadBytes != 0) return {};
  const auto readBits = [](const uint8_t* bytes) {
    return uint32_t(bytes[0]) << 24 | uint32_t(bytes[1]) << 16 |
           uint32_t(bytes[2]) << 8 | uint32_t(bytes[3]);
  };
  const auto read = [&](const uint8_t* bytes) { return std::bit_cast<float>(readBits(bytes)); };
  const auto finiteBits = [](uint32_t bits) { return (bits & 0x7f800000u) != 0x7f800000u; };
  const auto finite = [](float value) {
    return (std::bit_cast<uint32_t>(value) & 0x7f800000u) != 0x7f800000u;
  };
  if (!std::all_of(viewDepthRow.begin(), viewDepthRow.end(), finite)) return {};
  std::vector<float> depths;
  depths.reserve(vertices.size() / quadBytes);
  for (size_t offset = 0; offset < vertices.size(); offset += quadBytes) {
    const auto* quad = vertices.data() + offset;
    unsigned shared = 0;
    std::array<unsigned, 3> sharedVertices{};
    for (unsigned first = 0; first < 3; ++first) {
      for (unsigned second = 3; second < 6; ++second) {
        if (std::memcmp(quad + first * stride, quad + second * stride, stride) == 0) {
          sharedVertices[shared++] = first;
          break;
        }
      }
    }
    if (shared < 2) return {};
    std::array<float, 6> z{};
    std::array<std::array<float, 3>, 6> points{};
    for (unsigned vertex = 0; vertex < z.size(); ++vertex) {
      const auto* position = quad + vertex * stride;
      if (!finiteBits(readBits(position)) || !finiteBits(readBits(position + 4)) ||
          !finiteBits(readBits(position + 8))) return {};
      const std::array point{read(position), read(position + 4), read(position + 8)};
      points[vertex] = point;
      z[vertex] = -(viewDepthRow[0] * point[0] + viewDepthRow[1] * point[1] +
                    viewDepthRow[2] * point[2] + viewDepthRow[3]);
      if (!finite(z[vertex])) return {};
    }
    const auto [low, high] = std::minmax_element(z.begin(), z.end());
    const float spread = *high - *low;
    if (!finite(spread)) return {};
    if (spread > 0.0001f) {
      if (shared != 2 || std::memcmp(quad + sharedVertices[0] * stride,
                                    quad + sharedVertices[1] * stride, stride) == 0) return {};
      std::array<unsigned, 4> corners{};
      unsigned count = 0;
      for (unsigned vertex = 0; vertex < z.size(); ++vertex) {
        const auto* record = quad + vertex * stride;
        if (std::any_of(corners.begin(), corners.begin() + count, [&](unsigned corner) {
              return std::memcmp(record, quad + corner * stride, stride) == 0;
            })) continue;
        if (count == corners.size()) return {};
        corners[count++] = vertex;
      }
      if (count != corners.size()) return {};
      std::array<unsigned, 2> others{};
      unsigned otherCount = 0;
      std::array<float, 4> u{}, v{};
      for (unsigned corner = 0; corner < corners.size(); ++corner) {
        const unsigned vertex = corners[corner];
        const auto* record = quad + vertex * stride;
        if (std::memcmp(record + 12, quad + 12, 4) != 0) return {};
        if (!finiteBits(readBits(record + 16)) || !finiteBits(readBits(record + 20))) return {};
        u[corner] = read(record + 16);
        v[corner] = read(record + 20);
        if (vertex != sharedVertices[0] && vertex != sharedVertices[1]) {
          if (otherCount == others.size()) return {};
          others[otherCount++] = vertex;
        }
      }
      if (otherCount != others.size()) return {};
      // The shared edge must be the diagonal of a parallelogram, not an arbitrary mesh.
      for (unsigned axis = 0; axis < 3; ++axis) {
        const float error = (points[sharedVertices[0]][axis] + points[sharedVertices[1]][axis]) -
                            (points[others[0]][axis] + points[others[1]][axis]);
        if (!finite(error) || std::abs(error) > 0.0001f) return {};
      }
      const auto [left, right] = std::minmax_element(u.begin(), u.end());
      const auto [top, bottom] = std::minmax_element(v.begin(), v.end());
      if (*left == *right || *top == *bottom) return {};
      unsigned mask = 0, diagonalMask = 0;
      for (unsigned corner = 0; corner < corners.size(); ++corner) {
        if ((u[corner] != *left && u[corner] != *right) ||
            (v[corner] != *top && v[corner] != *bottom)) return {};
        const unsigned bit = 1u << (unsigned(u[corner] == *right) | (unsigned(v[corner] == *bottom) << 1));
        mask |= bit;
        if (corners[corner] == sharedVertices[0] || corners[corner] == sharedVertices[1]) diagonalMask |= bit;
      }
      if (mask != 15 || (diagonalMask != 9 && diagonalMask != 6)) return {};
    }
    const float depth = *low + spread * 0.5f;
    if (!finite(depth)) return {};
    depths.push_back(depth);
  }
  return depths;
}
} // namespace aurora::gx
