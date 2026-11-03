#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

namespace aurora::gx {
// Packed direct XYZ/RGBA/ST triangle pairs, with one camera-plane depth per billboard.
inline std::vector<float> billboard_depths(std::span<const uint8_t> vertices,
                                         const std::array<float, 4>& viewDepthRow) {
  constexpr size_t stride = 24;
  constexpr size_t quadBytes = 6 * stride;
  if (vertices.empty() || vertices.size() % quadBytes != 0) return {};
  const auto read = [](const uint8_t* bytes) {
    const uint32_t bits = uint32_t(bytes[0]) << 24 | uint32_t(bytes[1]) << 16 |
                          uint32_t(bytes[2]) << 8 | uint32_t(bytes[3]);
    return std::bit_cast<float>(bits);
  };
  const auto finite = [](float value) {
    return (std::bit_cast<uint32_t>(value) & 0x7f800000u) != 0x7f800000u;
  };
  if (!std::all_of(viewDepthRow.begin(), viewDepthRow.end(), finite)) return {};
  std::vector<float> depths;
  depths.reserve(vertices.size() / quadBytes);
  for (size_t offset = 0; offset < vertices.size(); offset += quadBytes) {
    const auto* quad = vertices.data() + offset;
    unsigned shared = 0;
    for (unsigned first = 0; first < 3; ++first) {
      for (unsigned second = 3; second < 6; ++second) {
        if (std::memcmp(quad + first * stride, quad + second * stride, stride) == 0) {
          ++shared;
          break;
        }
      }
    }
    if (shared < 2) return {};
    std::array<float, 6> z{};
    for (unsigned vertex = 0; vertex < z.size(); ++vertex) {
      const auto* position = quad + vertex * stride;
      const std::array point{read(position), read(position + 4), read(position + 8)};
      if (!std::all_of(point.begin(), point.end(), finite)) return {};
      z[vertex] = -(viewDepthRow[0] * point[0] + viewDepthRow[1] * point[1] +
                    viewDepthRow[2] * point[2] + viewDepthRow[3]);
      if (!finite(z[vertex])) return {};
    }
    const auto [low, high] = std::minmax_element(z.begin(), z.end());
    if (*high - *low > 0.0001f) return {};
    depths.push_back(*low + (*high - *low) * 0.5f);
  }
  return depths;
}
} // namespace aurora::gx
