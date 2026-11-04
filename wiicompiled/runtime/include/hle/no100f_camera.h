#pragma once

#include <algorithm>
#include <cmath>

namespace No100fCamera {

inline float WidenedViewWindowWidth(float x, float y, float aspect) noexcept {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(aspect) ||
        x <= 0.0f || y <= 0.0f || aspect <= 0.0f || y >= x) {
        return x;
    }
    return std::max(x, y * aspect);
}

} // namespace No100fCamera
