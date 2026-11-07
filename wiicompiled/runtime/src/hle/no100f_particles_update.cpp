// NO100F 60 fps ([video] fps60): particles keep the fade and gravity they have at the game's 30 Hz.
//
// With fps60 the main loop runs twice per 30 Hz frame with half the frame time, which broke two things:
// - Fade. The per-group update (0x80032918) adds rate * dt to each colour channel, cut to a whole number each frame. A
//   rate of -51 a second loses 1 per frame at 30 fps but rounds to 0 at 60, so the particle never fades; -102 loses 3
//   at 30 fps (-90 a second) but 1 at 60 (-60 a second). Here, with fps60, colour changes once per 30 Hz step using the
//   step's summed time, which cuts to the same whole numbers as 30 fps. Size and life still change every frame.
// - Gravity. PARS command 2 (0x800C4338) adds acceleration * dt to the velocity, and MOVE (0x800C38F0) adds the
//   velocity to the position every frame with no dt. Emit scales a velocity by the frame time, so velocity means distance
//   per frame, half as large at 60 frames a second, while the same change per second moves twice as far: particles fell
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
constexpr uint32_t kUpdateFrameCount = 0x80270890u;  // [r13 - 0x79F0]: main loop frame counter

// Particle fields
constexpr uint32_t kParticleNext = 0x00, kParticlePrev = 0x04, kParticleLife = 0x08, kParticleColor = 0x0C,
                   kParticleSize = 0x1C, kParticleVelocity = 0x20, kParticleSizeRate = 0x2C, kParticleOwner = 0x3C,
                   kParticleColorRate = 0x40;
// Group fields
constexpr uint32_t kGroupHead = 0x00, kGroupDeadList = 0x04, kGroupCount = 0x08, kGroupActive = 0x0C,
                   kGroupFlags = 0x12;
constexpr uint8_t kGroupReturnToPool = 0x2, kGroupAgeless = 0x4, kGroupLoopLife = 0x8;
constexpr uint32_t kEmitterAssetLife = 0x64;  // emitter asset: particle life, the loop value

bool ParticlesFps60() {
    static const bool fps60 = RuntimeConfigFile::Fps60();
    return fps60;
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
    static No100fParticlesUpdate::FadeClock clock;
    const float fadeDt = ParticlesFps60() ? clock.Step(Memory::Read32(kUpdateFrameCount), dt) : dt;

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

// PARS command 2: r3 = command (+4 = its asset, acceleration at +8), r4 = group, f1 = frame time.
extern "C" void No100f_ParticleAccelerate(CpuContext* ctx) {
    const uint32_t asset = Memory::Read32(ctx->gpr[3] + 4);
    const float scale = ParticlesFps60() ? No100fParticlesUpdate::kVelocityScaleFps60 : 1.0f;
    const float dt = static_cast<float>(ctx->fpr[1].d) * scale;
    float change[3];
    for (uint32_t i = 0; i < 3; ++i) change[i] = Memory::ReadFloat32(asset + 8 + i * 4) * dt;
    uint32_t particle = Memory::Read32(ctx->gpr[4]);
    for (; particle != 0; particle = Memory::Read32(particle + kParticleNext)) {
        for (uint32_t i = 0; i < 3; ++i) {
            const uint32_t velocity = particle + kParticleVelocity + i * 4;
            Memory::WriteFloat32(velocity, Memory::ReadFloat32(velocity) + change[i]);
        }
    }
}
PPC_NATIVE_OVERRIDE_VOID(800C4338, No100f_ParticleAccelerate, (CpuContext* ctx), (ctx));
