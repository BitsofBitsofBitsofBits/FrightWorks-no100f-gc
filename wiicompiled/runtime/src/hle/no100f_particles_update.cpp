// NO100F 60 fps ([video] fps60): particles keep the fade and gravity they have at the game's 30 Hz.
//
// With fps60 the main loop runs twice per 30 Hz frame with half the frame time, which broke two things:
// - Fade. The per-group update (0x80032918) adds rate * dt to each colour channel, cut to a whole number each frame. A
//   rate of -51 a second loses 1 per frame at 30 fps but rounds to 0 at 60, so the particle never fades; -102 loses 3
//   at 30 fps (-90 a second) but 1 at 60 (-60 a second). Here, with fps60, colour changes once per 30 Hz step using the
//   step's summed time, which cuts to the same whole numbers as 30 fps. Size and life still change every frame.
// - Gravity. PARS command 2 (0x800C4338) adds acceleration * dt to the velocity, and MOVE (0x800C38F0) adds the velocity
//   to the position every frame with no dt. Emit scales a velocity by the frame time, so velocity means distance per
//   frame, half as large at 60 frames a second, while the same change per second moves twice as far: particles fell
//   twice as fast. Here, with fps60, the change is halved.
// Without fps60 this is the original code.
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

namespace {
using namespace No100fParticlesUpdate;

constexpr uint32_t kFrameCount = 0x80270890u;  // [r13 - 0x79F0]: main loop frame counter

// Particle fields
constexpr uint32_t kNext = 0x00, kPrev = 0x04, kLife = 0x08, kColor = 0x0C, kSize = 0x1C, kVelocity = 0x20,
                   kSizeRate = 0x2C, kOwner = 0x3C, kColorRate = 0x40;
// Group fields
constexpr uint32_t kHead = 0x00, kDeadList = 0x04, kCount = 0x08, kActive = 0x0C, kFlags = 0x12;
constexpr uint8_t kFlagReturnToPool = 0x2, kFlagAgeless = 0x4, kFlagLoopLife = 0x8;
constexpr uint32_t kAssetLife = 0x64;  // emitter asset: particle life, the loop value

bool Fps60() {
    static const bool fps60 = RuntimeConfigFile::Fps60();
    return fps60;
}

void Unlink(uint32_t group, uint32_t particle, uint32_t next) {
    if (particle == Memory::Read32(group + kHead)) {
        Memory::Write32(group + kHead, next);
        if (next != 0) Memory::Write32(next + kPrev, 0);
    } else {
        const uint32_t prev = Memory::Read32(particle + kPrev);
        if (next != 0) Memory::Write32(next + kPrev, prev);
        if (prev != 0) Memory::Write32(prev + kNext, next);
    }
    Memory::Write32(particle + kNext, 0);
    Memory::Write32(particle + kPrev, 0);
}
}  // namespace

extern "C" void No100f_UpdateParticleGroup(CpuContext* ctx) {
    const uint32_t group = ctx->gpr[3];
    const float dt = static_cast<float>(ctx->fpr[1].d);
    const float ageStep = (Memory::Read8(group + kFlags) & kFlagAgeless) != 0 ? 0.0f : dt;
    static FadeClock clock;
    const float fadeDt = Fps60() ? clock.Step(Memory::Read32(kFrameCount), dt) : dt;

    uint32_t particle = Memory::Read32(group + kHead);
    if (particle == 0) Memory::Write8(group + kActive, 0);
    while (particle != 0) {
        const uint32_t next = Memory::Read32(particle + kNext);
        const float life = Memory::ReadFloat32(particle + kLife);
        if (life < 0.0f) {
            const uint8_t flags = Memory::Read8(group + kFlags);
            if ((flags & kFlagLoopLife) != 0) {
                const uint32_t owner = Memory::Read32(particle + kOwner);
                Memory::WriteFloat32(particle + kLife, Memory::ReadFloat32(owner + kAssetLife));
            } else {
                Unlink(group, particle, next);
                Memory::Write32(group + kCount, Memory::Read32(group + kCount) - 1);
                if ((flags & kFlagReturnToPool) != 0) {
                    ctx->gpr[3] = particle;
                    func_80032578(ctx);
                } else {
                    const uint32_t dead = Memory::Read32(group + kDeadList);
                    if (dead != 0) Memory::Write32(dead + kPrev, particle);
                    Memory::Write32(particle + kNext, dead);
                    Memory::Write32(particle + kPrev, 0);
                    Memory::Write32(group + kDeadList, particle);
                }
            }
        } else {
            for (uint32_t i = 0; i < 4; ++i) {
                const float rate = Memory::ReadFloat32(particle + kColorRate + i * 4);
                Memory::Write8(particle + kColor + i, FadeChannel(Memory::Read8(particle + kColor + i), rate, fadeDt));
            }
            const float size = Memory::ReadFloat32(particle + kSize);
            const float sizeRate = Memory::ReadFloat32(particle + kSizeRate);
            Memory::WriteFloat32(particle + kSize, PPC_Fmadds(sizeRate, dt, size));
            Memory::WriteFloat32(particle + kLife, life - ageStep);
        }
        particle = next;
    }
}
PPC_NATIVE_OVERRIDE_VOID(80032918, No100f_UpdateParticleGroup, (CpuContext* ctx), (ctx));

// PARS command 2: r3 = command (+4 = its asset, acceleration at +8), r4 = group, f1 = frame time.
extern "C" void No100f_ParticleAccelerate(CpuContext* ctx) {
    const uint32_t asset = Memory::Read32(ctx->gpr[3] + 4);
    const float dt = static_cast<float>(ctx->fpr[1].d) * (Fps60() ? kVelocityScaleFps60 : 1.0f);
    float change[3];
    for (uint32_t i = 0; i < 3; ++i) change[i] = Memory::ReadFloat32(asset + 8 + i * 4) * dt;
    for (uint32_t particle = Memory::Read32(ctx->gpr[4]); particle != 0; particle = Memory::Read32(particle + kNext)) {
        for (uint32_t i = 0; i < 3; ++i) {
            const uint32_t velocity = particle + kVelocity + i * 4;
            Memory::WriteFloat32(velocity, Memory::ReadFloat32(velocity) + change[i]);
        }
    }
}
PPC_NATIVE_OVERRIDE_VOID(800C4338, No100f_ParticleAccelerate, (CpuContext* ctx), (ctx));
