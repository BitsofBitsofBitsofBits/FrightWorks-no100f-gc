#include "../src/hle/audio/ax_address_sync.h"
#include <cstdint>
#include <iostream>
#include <stdexcept>

int main() {
    try {
        constexpr uint32_t fields[] = {0x2000, 0x4000, 0x8000, 0x10000};
        for (uint32_t subset = 0; subset < 16; ++subset) {
            const uint32_t selective = subset << 13;
            for (uint32_t field : fields) {
                const uint32_t ordinary = 0xe02b7u | selective;
                if (AxDspHle::PartialAddressSyncFlags(ordinary, field) != (ordinary | field))
                    throw std::runtime_error("Normal selective update changed");
                const uint32_t pending = ordinary | 0x1000u;
                const uint32_t committed = AxDspHle::PartialAddressSyncFlags(pending, field);
                if (committed != (pending & ~0x1e000u))
                    throw std::runtime_error("Partial update suppressed a pending full commit");
                if (!(committed & 0x1000u) || (committed & 0x1e000u))
                    throw std::runtime_error("SDK consumer would not choose complete address copy");
            }
        }
        if (AxDspHle::PartialAddressSyncFlags(0xe12b7u, 0x8000u) != 0xe12b7u ||
            AxDspHle::PartialAddressSyncFlags(0xe92b7u, 0x8000u) != 0xe12b7u)
            throw std::runtime_error("Captured restart/end combination did not preserve full reset");
        if (AxDspHle::PartialAddressSyncFlags(0x4000u, 0x8000u) != 0xc000u)
            throw std::runtime_error("Independent loop/end transition changed");
        std::cout << "PASS: full reset preservation, all partial masks and ordinary updates\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
