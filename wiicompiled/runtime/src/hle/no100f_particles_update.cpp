// NO100F 60 fps ([video] fps60): particles keep the fade, motion and animation they have at the game's 30 Hz.
//
// With fps60 the main loop runs twice per 30 Hz frame with half the frame time. Three things went wrong:
// - Fade. The per-group update (0x80032918) adds rate * dt to each colour channel, cut to a whole number each frame. A
//   rate of -51 a second loses 1 per frame at 30 fps but rounds to 0 at 60, so the particle never fades; -102 loses 3
//   at 30 fps (-90 a second) but 1 at 60 (-60 a second). Here, with fps60, colour changes once per 30 Hz step using the
//   step's summed time, which cuts to the same whole numbers as 30 fps. Size and life still change every frame.
// - Velocity. MOVE (PARS command 3, 0x800C38F0) adds the velocity to the position every frame with no dt, and emit
//   scales a velocity by the frame time, so velocity means distance per frame, half as large at 60 frames a second. A
//   command that changes velocity by an amount per second then moves particles twice as far. Here it gets half the time.
// - Per-call steps. Some commands do a fixed thing per call or cut to a whole number, so no share of dt makes them
//   right. Here they run once per 30 Hz step, as they do at 30 fps.
// The commands themselves stay the game's code: the system update (0x800C68E8) is what hands them their time, and
// CommandTimeFps60 in the header says which command gets what. Without fps60 this is the original code.
//
// Native in full: an override's address is excluded from translation, so the original body cannot be called.
#include <cstdint>
#include "abi_bridge.h"
#include "hle/no100f_particles_update.h"
#include "hle_stubs.h"
#include "memory.h"
#include "ppc_runtime.h"
#include "runtime_config.h"

extern "C" void func_80032578(CpuContext* ctx);  // returns a particle to the free pool
extern "C" void func_800C48BC(CpuContext* ctx);  // the function of a PARS command type, or 0

namespace {
constexpr uint32_t kUpdateFrameCount = 0x80270890u;  // [r13 - 0x79F0]: main loop frame counter

// Particle fields
constexpr uint32_t kParticleNext = 0x00, kParticlePrev = 0x04, kParticleLife = 0x08, kParticleColor = 0x0C,
                   kParticleSize = 0x1C, kParticleSizeRate = 0x2C, kParticleOwner = 0x3C, kParticleColorRate = 0x40;
// Group fields
constexpr uint32_t kGroupHead = 0x00, kGroupDeadList = 0x04, kGroupCount = 0x08, kGroupActive = 0x0C,
                   kGroupEnabled = 0x0E, kGroupFlags = 0x12, kGroupNext = 0x14;
constexpr uint8_t kGroupReturnToPool = 0x2, kGroupAgeless = 0x4, kGroupLoopLife = 0x8;
constexpr uint32_t kEmitterAssetLife = 0x64;  // emitter asset: particle life, the loop value
// System fields
constexpr uint32_t kSystemCommandCount = 0x14, kSystemCommands = 0x18, kSystemParent = 0x1C, kSystemGroups = 0x20;
constexpr uint32_t kCommandSize = 8, kCommandAsset = 4;  // a command in a system's list; its asset starts with the type

bool ParticlesFps60() {
    static const bool fps60 = RuntimeConfigFile::Fps60();
    return fps60;
}

// With fps60, the time of this frame's 30 Hz step (see StepClock).
float StepTimeFps60(float dt) {
    static No100fParticlesUpdate::StepClock clock;
    return clock.Step(Memory::Read32(kUpdateFrameCount), dt);
}

void UnlinkParticle(uint32_t group, uint32_t particle, uint32_t next) {
    if (particle == Memory::Read32(group + kGroupHead)) {
        Memory::Write32(group + kGroupHead, next);
        if (next != 0) Memory::Write32(next + kParticlePrev, 0);
    } else {
        const uint32_t prev = Memory::Read32(particle + kParticlePrev);
        if (next != 0) Memory::Write32(next + kParticlePrev, prev);
        if (prev != 0) Memory::Write32(prev + kParticleNext, next);
    }
    Memory::Write32(particle + kParticleNext, 0);
    Memory::Write32(particle + kParticlePrev, 0);
}
}  // namespace

extern "C" void No100f_UpdateParticleGroup(CpuContext* ctx) {
    const uint32_t group = ctx->gpr[3];
    const float dt = static_cast<float>(ctx->fpr[1].d);
    const float ageStep = (Memory::Read8(group + kGroupFlags) & kGroupAgeless) != 0 ? 0.0f : dt;
    const float fadeDt = ParticlesFps60() ? StepTimeFps60(dt) : dt;

    uint32_t particle = Memory::Read32(group + kGroupHead);
    if (particle == 0) Memory::Write8(group + kGroupActive, 0);
    while (particle != 0) {
        const uint32_t next = Memory::Read32(particle + kParticleNext);
        const float life = Memory::ReadFloat32(particle + kParticleLife);
        if (life < 0.0f) {
            const uint8_t flags = Memory::Read8(group + kGroupFlags);
            if ((flags & kGroupLoopLife) != 0) {
                const uint32_t owner = Memory::Read32(particle + kParticleOwner);
                Memory::WriteFloat32(particle + kParticleLife, Memory::ReadFloat32(owner + kEmitterAssetLife));
            } else {
                UnlinkParticle(group, particle, next);
                Memory::Write32(group + kGroupCount, Memory::Read32(group + kGroupCount) - 1);
                if ((flags & kGroupReturnToPool) != 0) {
                    ctx->gpr[3] = particle;
                    func_80032578(ctx);
                } else {
                    const uint32_t dead = Memory::Read32(group + kGroupDeadList);
                    if (dead != 0) Memory::Write32(dead + kParticlePrev, particle);
                    Memory::Write32(particle + kParticleNext, dead);
                    Memory::Write32(particle + kParticlePrev, 0);
                    Memory::Write32(group + kGroupDeadList, particle);
                }
            }
        } else {
            for (uint32_t i = 0; i < 4; ++i) {
                const uint32_t channel = particle + kParticleColor + i;
                const float rate = Memory::ReadFloat32(particle + kParticleColorRate + i * 4);
                Memory::Write8(channel, No100fParticlesUpdate::FadeChannel(Memory::Read8(channel), rate, fadeDt));
            }
            const float size = Memory::ReadFloat32(particle + kParticleSize);
            const float sizeRate = Memory::ReadFloat32(particle + kParticleSizeRate);
            Memory::WriteFloat32(particle + kParticleSize, PPC_Fmadds(sizeRate, dt, size));
            Memory::WriteFloat32(particle + kParticleLife, life - ageStep);
        }
        particle = next;
    }
}
PPC_NATIVE_OVERRIDE_VOID(80032918, No100f_UpdateParticleGroup, (CpuContext* ctx), (ctx));

namespace {
// Runs a system's commands on a group: r3 = command, r4 = group, f1 = time, as the game calls them.
void RunCommands(CpuContext* ctx, uint32_t system, uint32_t group, double dt) {
    for (int32_t i = 0; i < static_cast<int32_t>(Memory::Read32(system + kSystemCommandCount)); ++i) {
        const uint32_t command = Memory::Read32(system + kSystemCommands) + static_cast<uint32_t>(i) * kCommandSize;
        if (command == 0) continue;
        const uint32_t asset = Memory::Read32(command + kCommandAsset);
        if (asset == 0) continue;
        const uint32_t type = Memory::Read32(asset);
        ctx->gpr[3] = type;
        func_800C48BC(ctx);
        const uint32_t function = ctx->gpr[3];
        if (function == 0) continue;
        double time = dt;
        if (ParticlesFps60()) {
            const float frameTime = static_cast<float>(dt);
            const auto when = No100fParticlesUpdate::CommandTimeFps60(type, frameTime, StepTimeFps60(frameTime));
            if (!when.run) continue;
            time = when.time;
        }
        ctx->gpr[3] = command;
        ctx->gpr[4] = group;
        ctx->fpr[1].d = time;
        InvokeIndirectCpu(function, ctx);
    }
}

// 0x800C65C4: a system's parents' commands run first, oldest first. A parent counts if its own first group is enabled.
void RunParentCommands(CpuContext* ctx, uint32_t system, uint32_t group, double dt) {
    const uint32_t parent = Memory::Read32(system + kSystemParent);
    if (parent != 0) RunParentCommands(ctx, parent, group, dt);
    if (Memory::Read8(Memory::Read32(system + kSystemGroups) + kGroupEnabled) != 0) RunCommands(ctx, system, group, dt);
}
}  // namespace

// The system update: r3 = system, f1 = frame time. For each group: its parents' commands, its own, then the group update.
extern "C" void No100f_UpdateParticleSystem(CpuContext* ctx) {
    const uint32_t system = ctx->gpr[3];
    const double dt = ctx->fpr[1].d;
    const uint32_t parent = Memory::Read32(system + kSystemParent);
    for (uint32_t group = Memory::Read32(system + kSystemGroups); group != 0;
         group = Memory::Read32(group + kGroupNext)) {
        if (Memory::Read8(group + kGroupEnabled) != 0) {
            if (parent != 0) RunParentCommands(ctx, parent, group, dt);
            if (Memory::Read8(group + kGroupActive) != 0) RunCommands(ctx, system, group, dt);
        }
        ctx->gpr[3] = group;
        ctx->fpr[1].d = dt;
        No100f_UpdateParticleGroup(ctx);
    }
}
PPC_NATIVE_OVERRIDE_VOID(800C68E8, No100f_UpdateParticleSystem, (CpuContext* ctx), (ctx));
