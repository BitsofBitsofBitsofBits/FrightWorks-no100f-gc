// NO100F pad update (0x8000F5E8, GIHE78 rev 1): one controller port's PADStatus -> the game's action bits and stick
// values. A faithful reimplementation (hle/no100f_pad_actions.h has the mapping), so that with the original Xbox
// controls Black and White reach the game as actions of their own instead of GC Z + R / Z + L, which can't be held
// together with run / gum. Without the Xbox layout this is the original code.
//
// Native in full: an override's address is excluded from translation, so the original body cannot be called.
#include <cstdint>
#include "abi_bridge.h"
#include "memory.h"
#include "hle_stubs.h"
#include "ppc_runtime.h"
#include "runtime_config.h"
#include "hle/no100f_pad_actions.h"

extern "C" void func_8004421C(CpuContext* ctx);  // nonzero in the game states that skip pad input
extern "C" void func_8010C164(CpuContext* ctx);  // PADClamp(status)
extern "C" uint32_t PAD__Read_HLE(uint32_t statusPtr);
extern "C" uint32_t PAD__Reset_HLE(uint32_t mask);

namespace {
constexpr uint32_t kStatuses = 0x801BBE00u;   // the game's PADStatus[4] (12 bytes each), read once per frame by port 0
constexpr uint32_t kPortState = 0x80239068u;  // 12 bytes per port; +8 = connection state (1 lost, 2 connected)
constexpr uint32_t kStatusSize = 12;

// fn_8004427C: only port 0's state is kept.
void SetPortState(int32_t port, uint32_t state) {
    if (port == 0) Memory::Write32(kPortState + 8, state);
}
}  // namespace

// r3 = the game's pad object (+0x54 port, +0x56 sub-index, +0x38..0x3B sticks), r4 = where the action bits go.
// Returns 1 when the pad was read.
extern "C" void No100f_PadUpdate(CpuContext* ctx) {
    namespace A = No100fPadActions;
    const uint32_t pad = ctx->gpr[3];
    const uint32_t out = ctx->gpr[4];

    func_8004421C(ctx);
    if (ctx->gpr[3] != 0) {
        ctx->gpr[3] = 0;
        return;
    }
    if (Memory::Read16(pad + 0x54) == 0 && Memory::Read16(pad + 0x56) == 0) {
        PAD__Read_HLE(kStatuses);
        ctx->gpr[3] = kStatuses;
        func_8010C164(ctx);
    }

    const int32_t port = static_cast<int16_t>(Memory::Read16(pad + 0x54));
    const uint32_t status = kStatuses + static_cast<uint32_t>(port) * kStatusSize;
    const int8_t err = static_cast<int8_t>(Memory::Read8(status + 0xA));
    if (err != 0) {
        if (err == -1) {  // PAD_ERR_NO_CONTROLLER
            SetPortState(port, 1);
            PAD__Reset_HLE(0x80000000u >> (port & 31));
        }
        ctx->gpr[3] = 0;
        return;
    }

    uint32_t actions = A::FromPad(Memory::Read16(status), Memory::Read8(status + 6), Memory::Read8(status + 7));
    if (RuntimeConfigFile::XboxOriginalLayout() && port >= 0) {
        bool black = false, white = false;
        No100fPad::XboxButtons(static_cast<uint32_t>(port), black, white);
        actions = A::AddXboxActions(actions, black, white);
    }
    Memory::Write32(out, actions);

    const auto axis = [&](uint32_t offset) { return static_cast<int8_t>(Memory::Read8(status + offset)); };
    Memory::Write8(pad + 0x38, static_cast<uint8_t>(A::Stick(axis(2))));
    Memory::Write8(pad + 0x39, static_cast<uint8_t>(-A::Stick(axis(3))));
    Memory::Write8(pad + 0x3A, static_cast<uint8_t>(A::Stick(axis(4))));
    Memory::Write8(pad + 0x3B, static_cast<uint8_t>(-A::Stick(axis(5))));

    if (Memory::Read32(kPortState + static_cast<uint32_t>(port) * 12 + 8) != 2) SetPortState(port, 2);
    ctx->gpr[3] = 1;
}
PPC_NATIVE_OVERRIDE_VOID(8000F5E8, No100f_PadUpdate, (CpuContext* ctx), (ctx));
