// Stream voices' DVD read callback, with the stale end-of-track check fixed: see hle/no100f_stream_read.h.
#include "hle/no100f_stream_read.h"
#include "hle_stubs.h"
#include "ppc_runtime.h"

extern "C" void ARQPostRequest_Gc(uint32_t request, uint32_t owner, uint32_t type, uint32_t priority,
                                  uint32_t source, uint32_t dest, uint32_t length, uint32_t callback);

namespace {
constexpr uint32_t kFileInfoEntry = 0x2C;       // DVDFileInfo user data: the stream table entry
constexpr uint32_t kCopyDone = 0x800DA63C;      // the game's ARQ callback
constexpr int32_t kRefillFlagFromR13 = -0x7258;
}  // namespace

// fn_800DA71C(result in r3, DVDFileInfo* in r4)
extern "C" void No100f_StreamChunkRead(CpuContext* ctx) {
    using namespace No100fStreamRead;
    const uint32_t entry = Memory::Read32(ctx->gpr[4] + kFileInfoEntry);
    const Result result = OnChunkRead(entry, ctx->gpr[3],
        [](uint32_t address) { return Memory::Read32(address); },
        [](uint32_t address, uint32_t value) { Memory::Write32(address, value); });
    if (result.wakeRefill) Memory::Write32(ctx->gpr[13] + kRefillFlagFromR13, 1);
    if (result.copyDest != 0) {
        ARQPostRequest_Gc(entry + kRequest, entry, 0, 1, Memory::Read32(entry + kMainBuffer), result.copyDest,
                          kChunkBytes, kCopyDone);
    }
}
PPC_NATIVE_OVERRIDE_VOID(800DA71C, No100f_StreamChunkRead, (CpuContext* ctx), (ctx));
