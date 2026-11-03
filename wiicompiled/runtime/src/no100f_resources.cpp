#include "no100f_resources.h"

#include <imgui.h>

#include <chrono>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#endif

namespace no100f_resources {
namespace {

struct ProcessSnapshot {
    std::optional<std::uint64_t> cpuTicks;
    std::optional<std::uint64_t> workingSetBytes;
    std::optional<std::uint64_t> privateCommitBytes;
    std::uint32_t logicalProcessors = 0;
    const char* cpuReason = "Current-process CPU measurement is implemented only on Windows.";
    const char* memoryReason = "Current-process RAM measurement is implemented only on Windows.";
    unsigned long cpuError = 0;
    unsigned long memoryError = 0;
};

ProcessSnapshot ReadProcess() {
    ProcessSnapshot snapshot;
#ifdef _WIN32
    const HANDLE process = GetCurrentProcess();
    FILETIME creation{}, exit{}, kernel{}, user{};
    if (GetProcessTimes(process, &creation, &exit, &kernel, &user)) {
        const auto ticks = [](const FILETIME& time) -> std::uint64_t {
            return (static_cast<std::uint64_t>(time.dwHighDateTime) << 32) | time.dwLowDateTime;
        };
        snapshot.cpuTicks = ticks(kernel) + ticks(user);
    } else {
        snapshot.cpuError = GetLastError();
        snapshot.cpuReason = "GetProcessTimes failed.";
    }
    snapshot.logicalProcessors = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);

    PROCESS_MEMORY_COUNTERS_EX memory{};
    memory.cb = static_cast<DWORD>(sizeof(memory));
    // K32 is supplied by kernel32 on Windows 7+, so no PSAPI DLL/library is needed.
    if (K32GetProcessMemoryInfo(process, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), memory.cb)) {
        snapshot.workingSetBytes = static_cast<std::uint64_t>(memory.WorkingSetSize);
        snapshot.privateCommitBytes = static_cast<std::uint64_t>(memory.PrivateUsage);
    } else {
        snapshot.memoryError = GetLastError();
        snapshot.memoryReason = "K32GetProcessMemoryInfo failed.";
    }
#endif
    return snapshot;
}

std::uint64_t WallTicks() {
    using Clock = std::chrono::steady_clock;
    using Ticks = std::chrono::duration<std::uint64_t, std::ratio<1, detail::kTicksPerSecond>>;
    static const auto origin = Clock::now();
    return std::chrono::duration_cast<Ticks>(Clock::now() - origin).count();
}

} // namespace

void Draw() {
    static detail::SampleSchedule schedule;
    static detail::CpuSampler cpu;
    static detail::CpuReading reading;
    static ProcessSnapshot snapshot;
    const std::uint64_t now = WallTicks();
    const auto decision = schedule.Visit(now, ImGui::GetFrameCount());
    if (decision.sample) {
        if (decision.resetBaseline)
            cpu.Reset();
        snapshot = ReadProcess();
        reading = cpu.Update(now, snapshot.cpuTicks, snapshot.logicalProcessors);
    }

    ImGui::TextUnformatted("Current app process");
    ImGui::TextWrapped("Sampled at most once per second while this tab is visible. "
                       "CPU needs two samples; reopening the tab starts a fresh interval.");
    ImGui::Separator();
    if (reading.status == detail::CpuStatus::Ready) {
        if (reading.machinePercent) {
            ImGui::Text("CPU (total-machine normalized): %.1f%%", *reading.machinePercent);
            ImGui::Text("Active logical processors: %u", static_cast<unsigned>(snapshot.logicalProcessors));
        } else {
            ImGui::TextUnformatted("CPU (total-machine normalized): unavailable; processor count unknown.");
        }
        ImGui::Text("CPU (one-core equivalent): %.1f%%", reading.corePercent);
    } else if (reading.status == detail::CpuStatus::WarmingUp) {
        ImGui::TextUnformatted("CPU: warming up; waiting for the next sample.");
    } else {
        ImGui::TextWrapped("CPU: unavailable. %s", snapshot.cpuReason);
        if (snapshot.cpuError != 0)
            ImGui::Text("Windows error: %lu", snapshot.cpuError);
    }
    ImGui::TextWrapped("CPU includes all app threads (kernel + user time). Total-machine 100%% means "
                       "all active logical processors; one-core 100%% means one logical processor "
                       "fully busy, and can exceed 100%%.");
    ImGui::Separator();
    if (snapshot.workingSetBytes && snapshot.privateCommitBytes) {
        ImGui::Text("RAM working set: %.1f MiB", static_cast<double>(*snapshot.workingSetBytes) / detail::kBytesPerMiB);
        ImGui::Text("RAM private committed: %.1f MiB",
                    static_cast<double>(*snapshot.privateCommitBytes) / detail::kBytesPerMiB);
    } else {
        ImGui::TextWrapped("RAM: unavailable. %s", snapshot.memoryReason);
        if (snapshot.memoryError != 0)
            ImGui::Text("Windows error: %lu", snapshot.memoryError);
    }
    ImGui::TextWrapped("Working set is resident physical memory, including shared pages. "
                       "Private committed is memory committed exclusively to this process, "
                       "which need not all be resident. These are different measurements, not additive.");
    ImGui::Separator();
    ImGui::TextWrapped("GPU utilization: unavailable. %s", detail::kGpuUnavailableReason);
}

} // namespace no100f_resources
