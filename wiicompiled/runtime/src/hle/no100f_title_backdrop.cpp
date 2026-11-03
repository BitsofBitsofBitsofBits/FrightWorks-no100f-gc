#include "abi_bridge.h"
#include "hle/no100f_title_backdrop.h"
#include "hle_stubs.h"
#include "memory.h"
#include "runtime_log.h"

extern "C" void func_8000E978(CpuContext* ctx); // iModelFileNew: consumes and closes the memory stream synchronously
extern "C" void func_8002FDB0(CpuContext* ctx); // xMemAlloc(heap, size), released with the scene heap

// GIHE78 rev 1 MODEL_Read: iModelFileNew(r5, r6), *r7 = 0x70, return model.
extern "C" void No100f_ReadTitleModel(CpuContext* ctx) {
    uint32_t data = ctx->gpr[5], size = ctx->gpr[6];
    const uint32_t outSize = ctx->gpr[7];
    if (ctx->gpr[4] == No100fTitleBackdrop::kModelId) {
        if (!Memory::Contains(data, size)) {
            RT_LOG(RT_TAG_RUNTIME) << "[title-backdrop] model input outside guest memory\n";
        } else {
            const auto expanded = No100fTitleBackdrop::BuildModel(Memory::GetPointer(data, size), size);
            if (expanded.empty()) {
                RT_LOG(RT_TAG_RUNTIME) << "[title-backdrop] incompatible title model; original retained\n";
            } else {
                CpuContext allocation = *ctx;
                allocation.gpr[3] = Memory::Read32(ctx->gpr[13] - 31024);
                allocation.gpr[4] = static_cast<uint32_t>(expanded.size());
                func_8002FDB0(&allocation);
                const uint32_t buffer = allocation.gpr[3];
                if (!buffer || !Memory::Contains(buffer, expanded.size())) {
                    RT_LOG(RT_TAG_RUNTIME) << "[title-backdrop] cannot allocate side panels; original retained\n";
                } else {
                    std::memcpy(Memory::GetPointer(buffer, expanded.size()), expanded.data(), expanded.size());
                    data = buffer;
                    size = static_cast<uint32_t>(expanded.size());
                    RT_LOG(RT_TAG_RUNTIME) << "[title-backdrop] injected forest side panels; original title box retained\n";
                }
            }
        }
    }
    ctx->gpr[3] = data;
    ctx->gpr[4] = size;
    func_8000E978(ctx);
    Memory::Write32(outSize, 0x70);
}
PPC_NATIVE_OVERRIDE_VOID(800458D4, No100f_ReadTitleModel, (CpuContext* ctx), (ctx));
