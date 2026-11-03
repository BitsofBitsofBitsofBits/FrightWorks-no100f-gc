#pragma once

#include <cstdint>

namespace AxDspHle {
constexpr uint32_t PartialAddressSyncFlags(uint32_t flags, uint32_t field) {
    // The SDK consumer prioritizes selective bits over the complete address reset.
    // Keep the full commit pending so it copies the latest shadow, including this edit.
    return (flags & 0x1000u) ? (flags & ~0x1e000u) : (flags | field);
}
}
