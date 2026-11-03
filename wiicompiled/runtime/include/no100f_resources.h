#pragma once

#include <cstdint>
#include <optional>

namespace no100f_resources {

// Call only while the Resources tab is visible, on the ImGui thread.
void Draw();

namespace detail {

inline constexpr std::uint64_t kTicksPerSecond = 10'000'000;
inline constexpr double kBytesPerMiB = 1024.0 * 1024.0;
inline constexpr const char* kGpuUnavailableReason =
    "The Aurora/WebGPU backend does not expose a reliable per-process GPU utilization counter. "
    "FPS, frame time and system-wide GPU load are not process GPU utilization.";

enum class CpuStatus { WarmingUp, Ready, Unavailable };

struct CpuReading {
    CpuStatus status = CpuStatus::WarmingUp;
    double corePercent = 0.0;
    std::optional<double> machinePercent;
};

class CpuSampler {
public:
    void Reset() { previousCpu_.reset(); }

    CpuReading Update(std::uint64_t wallTicks, std::optional<std::uint64_t> cpuTicks,
                      std::uint32_t logicalProcessors) {
        if (!cpuTicks) {
            Reset();
            return {CpuStatus::Unavailable, 0.0, std::nullopt};
        }
        CpuReading reading;
        if (previousCpu_ && wallTicks > previousWall_ && *cpuTicks >= *previousCpu_) {
            // Subtract integers before converting: cumulative counters can exceed double's exact range.
            reading.corePercent = 100.0 * static_cast<double>(*cpuTicks - *previousCpu_) /
                                  static_cast<double>(wallTicks - previousWall_);
            if (logicalProcessors != 0)
                reading.machinePercent = reading.corePercent / static_cast<double>(logicalProcessors);
            reading.status = CpuStatus::Ready;
        }
        previousCpu_ = cpuTicks;
        previousWall_ = wallTicks;
        return reading;
    }

private:
    std::optional<std::uint64_t> previousCpu_;
    std::uint64_t previousWall_ = 0;
};

struct SampleDecision {
    bool sample;
    bool resetBaseline;
};

class SampleSchedule {
public:
    SampleDecision Visit(std::uint64_t wallTicks, int imguiFrame) {
        const auto frame = static_cast<std::int64_t>(imguiFrame);
        const bool resumed = !lastFrame_ || frame < *lastFrame_ || frame > *lastFrame_ + 1;
        lastFrame_ = frame;
        const bool clockReset = lastSample_ && wallTicks < *lastSample_;
        if (resumed || clockReset || !lastSample_ || wallTicks - *lastSample_ >= kTicksPerSecond) {
            lastSample_ = wallTicks;
            return {true, resumed || clockReset};
        }
        return {false, false};
    }

private:
    std::optional<std::int64_t> lastFrame_;
    std::optional<std::uint64_t> lastSample_;
};

} // namespace detail
} // namespace no100f_resources
