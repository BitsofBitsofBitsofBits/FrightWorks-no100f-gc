#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <limits>

namespace AudioHleInternal {

inline uint32_t ConsumePollDeltaMicros(std::chrono::steady_clock::time_point& cursor,
                                      std::chrono::steady_clock::time_point now)
{
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - cursor).count();
    const auto delivered = std::clamp<int64_t>(elapsed, 0, std::numeric_limits<uint32_t>::max());
    // Keep fractional microseconds and any debt beyond the tick argument's range.
    cursor += std::chrono::microseconds(delivered);
    return static_cast<uint32_t>(delivered);
}

} // namespace AudioHleInternal
