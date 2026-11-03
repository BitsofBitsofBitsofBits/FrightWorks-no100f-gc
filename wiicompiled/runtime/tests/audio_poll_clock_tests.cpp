#include "../src/hle/audio/audio_poll_clock.h"

#include <iostream>
#include <stdexcept>

int main()
{
    try {
        using namespace std::chrono;
        steady_clock::time_point cursor{}, now{};
        const auto poll = [&] { return AudioHleInternal::ConsumePollDeltaMicros(cursor, now); };
        if (poll() != 0)
            throw std::runtime_error("Initial poll advanced time");

        now += microseconds(242440);
        if (poll() != 242440 || cursor != now)
            throw std::runtime_error("Captured stall discarded elapsed time");

        uint64_t delivered = 0;
        for (int index = 0; index < 1000; ++index) {
            now += nanoseconds(900);
            delivered += poll();
        }
        if (delivered != 900 || cursor != now)
            throw std::runtime_error("Frequent polls discarded fractional microseconds");

        for (uint32_t interval : {99999u, 100000u, 100001u}) {
            now += microseconds(interval);
            if (poll() != interval || cursor != now)
                throw std::runtime_error("Former cap boundary discarded time");
        }

        constexpr auto maximum = std::numeric_limits<uint32_t>::max();
        now += microseconds(uint64_t(maximum) + 42);
        if (poll() != maximum || poll() != 42 || cursor != now || poll() != 0)
            throw std::runtime_error("Long-pause delivery lost debt or overflowed");

        std::cout << "PASS: captured stall, fractional time, cap boundaries and bounded delivery debt\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
