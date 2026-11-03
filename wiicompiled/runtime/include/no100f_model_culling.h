#pragma once

#include "isa/ppc_isa_float.h"

#include <cstdint>

namespace no100f_model_culling {

// RwCameraFrustumTestSphere, GIHE78 rev 1: retain the guest's single-precision
// operation order and its outside/intersect/inside return values.
template <typename ReadFloat>
uint32_t ClassifySphere(uint32_t camera, uint32_t sphere, ReadFloat read) {
    uint32_t result = 2;
    for (uint32_t i = 0; i < 6; ++i) {
        const uint32_t plane = camera + 0x98 + i * 0x14;
        const double x = PpcFmulsInline(read(sphere), read(plane));
        const double y = PpcFmulsInline(read(sphere + 4), read(plane + 4));
        const double z = PpcFmulsInline(read(sphere + 8), read(plane + 8));
        double distance = PpcForceSingleValueInline(x + y);
        distance = PpcForceSingleValueInline(z + distance);
        distance = PpcForceSingleValueInline(distance - read(plane + 0xC));
        const float radius = read(sphere + 0xC);
        if (!(distance <= radius)) return 0;
        if (!(distance <= -radius)) result = 1;
    }
    return result;
}

constexpr bool IsWideEntityGate(float aspect, uint32_t camera, uint32_t mainCamera,
                                uint32_t returnAddress, uint32_t modelCullReturn) {
    return aspect > 4.0f / 3.0f && camera == mainCamera &&
           returnAddress == 0x8000E380u && modelCullReturn == 0x80022D20u;
}

template <typename NextModel, typename ModelVisible>
bool AnyModelVisible(uint32_t model, NextModel next, ModelVisible visible) {
    for (; model; model = next(model))
        if (visible(model)) return true;
    return false;
}

} // namespace no100f_model_culling
