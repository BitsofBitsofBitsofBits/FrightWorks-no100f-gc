#include "ax_address_sync.h"
#include "hle_stubs.h"
#include "memory.h"

extern "C" int32_t OS__DisableInterrupts_801a65ac();
extern "C" int32_t OS__RestoreInterrupts_801a65d4(int32_t level);

namespace {
void SetAddress(uint32_t voice, uint32_t offset, uint32_t address, uint32_t field) {
    const int32_t level = OS__DisableInterrupts_801a65ac();
    Memory::Write16(voice + offset, static_cast<uint16_t>(address >> 16));
    Memory::Write16(voice + offset + 2u, static_cast<uint16_t>(address));
    const uint32_t flags = Memory::Read32(voice + 0x1cu);
    Memory::Write32(voice + 0x1cu, AxDspHle::PartialAddressSyncFlags(flags, field));
    OS__RestoreInterrupts_801a65d4(level);
}
}

extern "C" void AXSetVoiceLoop_Gc(uint32_t voice, uint32_t looping) {
    const int32_t level = OS__DisableInterrupts_801a65ac();
    Memory::Write16(voice + 0x1a6u, static_cast<uint16_t>(looping));
    const uint32_t flags = Memory::Read32(voice + 0x1cu);
    Memory::Write32(voice + 0x1cu, AxDspHle::PartialAddressSyncFlags(flags, 0x2000u));
    OS__RestoreInterrupts_801a65d4(level);
}
PPC_NATIVE_OVERRIDE_VOID(800F887C, AXSetVoiceLoop_Gc,
    (uint32_t voice, uint32_t looping), (voice, looping));

extern "C" void AXSetVoiceLoopAddr_Gc(uint32_t voice, uint32_t address) {
    SetAddress(voice, 0x1aau, address, 0x4000u);
}
PPC_NATIVE_OVERRIDE_VOID(800F88C8, AXSetVoiceLoopAddr_Gc,
    (uint32_t voice, uint32_t address), (voice, address));

extern "C" void AXSetVoiceEndAddr_Gc(uint32_t voice, uint32_t address) {
    SetAddress(voice, 0x1aeu, address, 0x8000u);
}
PPC_NATIVE_OVERRIDE_VOID(800F891C, AXSetVoiceEndAddr_Gc,
    (uint32_t voice, uint32_t address), (voice, address));

extern "C" void AXSetVoiceCurrentAddr_Gc(uint32_t voice, uint32_t address) {
    SetAddress(voice, 0x1b2u, address, 0x10000u);
}
PPC_NATIVE_OVERRIDE_VOID(800F8970, AXSetVoiceCurrentAddr_Gc,
    (uint32_t voice, uint32_t address), (voice, address));
