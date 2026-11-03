#include "no100f_resources.h"

#include <cassert>
#include <cmath>
#include <limits>
#include <string_view>

namespace {

void Near(double actual, double expected) {
    assert(std::abs(actual - expected) < 0.000001);
}

} // namespace

int main() {
    using namespace no100f_resources::detail;
    CpuSampler cpu;
    assert(cpu.Update(0, 100, 8).status == CpuStatus::WarmingUp);
    auto reading = cpu.Update(kTicksPerSecond, 100 + 2 * kTicksPerSecond, 8);
    assert(reading.status == CpuStatus::Ready);
    Near(reading.corePercent, 200.0);
    assert(reading.machinePercent);
    Near(*reading.machinePercent, 25.0);

    reading = cpu.Update(2 * kTicksPerSecond, 100 + 2 * kTicksPerSecond, 8);
    assert(reading.status == CpuStatus::Ready);
    Near(reading.corePercent, 0.0);
    Near(*reading.machinePercent, 0.0);

    cpu.Reset();
    assert(cpu.Update(0, 0, 0).status == CpuStatus::WarmingUp);
    reading = cpu.Update(kTicksPerSecond, kTicksPerSecond / 2, 0);
    Near(reading.corePercent, 50.0);
    assert(!reading.machinePercent); // Unknown topology must not become a fake normalized zero.
    reading = cpu.Update(2 * kTicksPerSecond, std::nullopt, 8);
    assert(reading.status == CpuStatus::Unavailable);
    assert(!reading.machinePercent);
    assert(cpu.Update(3 * kTicksPerSecond, kTicksPerSecond, 8).status == CpuStatus::WarmingUp);
    reading = cpu.Update(5 * kTicksPerSecond, 2 * kTicksPerSecond, 8);
    Near(reading.corePercent, 50.0); // Actual elapsed time, not an assumed one-second interval.
    Near(*reading.machinePercent, 6.25);

    assert(cpu.Update(6 * kTicksPerSecond, 0, 8).status == CpuStatus::WarmingUp);
    assert(cpu.Update(6 * kTicksPerSecond, 1, 8).status == CpuStatus::WarmingUp);
    assert(cpu.Update(5 * kTicksPerSecond, 2, 8).status == CpuStatus::WarmingUp);
    reading = cpu.Update(6 * kTicksPerSecond, kTicksPerSecond + 2, 8);
    Near(reading.corePercent, 100.0);

    cpu.Reset();
    constexpr auto big = std::numeric_limits<std::uint64_t>::max() - 4 * kTicksPerSecond;
    assert(cpu.Update(big, big, 4).status == CpuStatus::WarmingUp);
    reading = cpu.Update(big + kTicksPerSecond, big + kTicksPerSecond / 2, 4);
    Near(reading.corePercent, 50.0);
    Near(*reading.machinePercent, 12.5);

    SampleSchedule schedule;
    auto decision = schedule.Visit(0, 10);
    assert(decision.sample && decision.resetBaseline);
    assert(!schedule.Visit(0, 10).sample); // Repeated drawing in one frame must not re-query.
    assert(!schedule.Visit(kTicksPerSecond - 1, 11).sample);
    decision = schedule.Visit(kTicksPerSecond, 12);
    assert(decision.sample && !decision.resetBaseline);
    assert(!schedule.Visit(kTicksPerSecond + 1, 13).sample);
    decision = schedule.Visit(10 * kTicksPerSecond, 100); // Tab was hidden.
    assert(decision.sample && decision.resetBaseline);
    cpu.Reset();
    assert(cpu.Update(10 * kTicksPerSecond, big, 8).status == CpuStatus::WarmingUp);
    assert(!schedule.Visit(10 * kTicksPerSecond + 1, 101).sample);
    decision = schedule.Visit(1, 102); // Clock discontinuity must not underflow.
    assert(decision.sample && decision.resetBaseline);
    decision = schedule.Visit(2, 1); // New ImGui context / frame counter reset.
    assert(decision.sample && decision.resetBaseline);

    Near(1'048'576.0 / kBytesPerMiB, 1.0);
    assert(std::string_view(kGpuUnavailableReason).find("per-process") != std::string_view::npos);
    assert(std::string_view(kGpuUnavailableReason).find("FPS") != std::string_view::npos);
}
