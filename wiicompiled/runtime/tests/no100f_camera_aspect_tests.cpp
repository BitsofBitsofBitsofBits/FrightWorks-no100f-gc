#include "hle/no100f_camera.h"

#include <cassert>
#include <cmath>

int main() {
    constexpr float x = 0.5f;
    constexpr float y = 0.375f;
    constexpr float aspect16x9 = 16.0f / 9.0f;
    constexpr float aspect21x9 = 21.0f / 9.0f;
    constexpr float aspectCinematic = 2.39f;
    constexpr float aspect32x9 = 32.0f / 9.0f;

    for (const float aspect : {aspect16x9, aspect21x9, aspectCinematic, aspect32x9}) {
        const float widened = No100fCamera::WidenedViewWindowWidth(x, y, aspect);
        assert(std::abs(widened / y - aspect) < 0.00001f);
    }

    assert(No100fCamera::WidenedViewWindowWidth(0.5f, 0.4f, aspect16x9) > 0.5f);
    assert(No100fCamera::WidenedViewWindowWidth(y * aspect16x9, y, aspect16x9) == y * aspect16x9);
    assert(No100fCamera::WidenedViewWindowWidth(0.75f, y, aspect16x9) == 0.75f);
    assert(No100fCamera::WidenedViewWindowWidth(0.5f, 0.5f, aspect32x9) == 0.5f);
    assert(No100fCamera::WidenedViewWindowWidth(0.375f, 0.5f, aspect32x9) == 0.375f);
    assert(No100fCamera::WidenedViewWindowWidth(x, y, 0.0f) == x);
}
