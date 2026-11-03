#include "no100f_model_culling.h"

#include <array>
#include <cassert>
#include <cmath>
#include <limits>

int main() {
    using namespace no100f_model_culling;
    constexpr uint32_t camera = 0, sphere = 0x120;
    std::array<float, 80> memory{};
    const auto classify = [&] {
        return ClassifySphere(camera, sphere, [&](uint32_t address) { return memory.at(address / 4); });
    };
    // A unit box with outward-facing planes.
    for (unsigned i = 0; i < 6; ++i) {
        const unsigned p = (0x98 + i * 0x14) / 4;
        memory[p + i / 2] = i % 2 ? -1.0f : 1.0f;
        memory[p + 3] = 1.0f;
    }
    memory[sphere / 4 + 3] = 0.1f;
    assert(classify() == 2);
    for (unsigned axis = 0; axis < 3; ++axis) {
        for (float sign : {-1.0f, 1.0f}) {
            memory[sphere / 4 + axis] = sign;
            assert(classify() == 1);
            memory[sphere / 4 + axis] = sign * 1.2f;
            assert(classify() == 0);
            memory[sphere / 4 + axis] = 0;
        }
    }
    memory[sphere / 4] = 1.25f;
    memory[sphere / 4 + 3] = 0.25f;
    assert(classify() == 1); // Tangency is not outside.
    memory[sphere / 4] = std::nextafter(1.25f, 2.0f);
    assert(classify() == 0);
    memory[sphere / 4] = std::numeric_limits<float>::quiet_NaN();
    assert(classify() == 0); // Match the guest's ordered comparison.

    assert(IsWideEntityGate(16.0f / 9.0f, 1, 1, 0x8000E380, 0x80022D20));
    assert(!IsWideEntityGate(0, 1, 1, 0x8000E380, 0x80022D20));
    assert(!IsWideEntityGate(4.0f / 3.0f, 1, 1, 0x8000E380, 0x80022D20));
    assert(!IsWideEntityGate(16.0f / 9.0f, 2, 1, 0x8000E380, 0x80022D20));
    assert(!IsWideEntityGate(16.0f / 9.0f, 1, 1, 0x8000E380, 0x8003075C));
    assert(!IsWideEntityGate(16.0f / 9.0f, 1, 1, 0, 0x80022D20));

    const auto next = [](uint32_t model) { return model < 3 ? model + 1 : 0; };
    assert(AnyModelVisible(1, next, [](uint32_t model) { return model == 2; }));
    assert(AnyModelVisible(1, next, [](uint32_t) { return false; }) == false);
    assert(AnyModelVisible(1, [](uint32_t) { return 0; }, [](uint32_t) { return true; }));
    assert(!AnyModelVisible(0, next, [](uint32_t) { return true; }));
}
