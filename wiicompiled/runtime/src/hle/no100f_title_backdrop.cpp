#include "abi_bridge.h"
#include "hle/no100f_title_backdrop.h"
#include "hle_stubs.h"
#include "memory.h"
#include "runtime_log.h"

extern "C" void func_8000E978(CpuContext* ctx); // iModelFileNew: consumes and closes the memory stream synchronously
extern "C" void func_8002FDB0(CpuContext* ctx); // xMemAlloc(heap, size), released with the scene heap

namespace {

bool SetTitleTextureEdges(uint32_t atomic) {
    if (!atomic || !Memory::Contains(atomic, 0x1C) || Memory::Read8(atomic) != 1) return false;
    const uint32_t geometry = Memory::Read32(atomic + 0x18);
    if (!geometry || !Memory::Contains(geometry, 0x28) ||
        Memory::Read32(geometry + 0x10) != No100fTitleBackdrop::kTriangles ||
        Memory::Read32(geometry + 0x14) != No100fTitleBackdrop::kVertices ||
        Memory::Read32(geometry + 0x24) != 1) return false;
    const uint32_t materials = Memory::Read32(geometry + 0x20);
    if (!materials || !Memory::Contains(materials, 4)) return false;
    const uint32_t material = Memory::Read32(materials);
    if (!material || !Memory::Contains(material, 4)) return false;
    const uint32_t texture = Memory::Read32(material);
    if (!texture || !Memory::Contains(texture, 0x54) ||
        std::memcmp(Memory::GetPointer(texture + 0x10, 11), "text_title", 11) != 0) return false;
    const uint32_t flags = Memory::Read32(texture + 0x50);
    const uint32_t addressU = (flags >> 8) & 0xF;
    if (addressU != 1 && addressU != 3) return false;
    // The pinned loader retains dictionary sampler state rather than the material-stream flags.
    // Only U changes (WRAP -> CLAMP); preserve the game's chosen V addressing and filter mode.
    Memory::Write32(texture + 0x50, (flags & ~0xF00u) | 0x300u);
    return true;
}

} // namespace

// GIHE78 rev 1 MODEL_Read: iModelFileNew(r5, r6), *r7 = 0x70, return model.
extern "C" void No100f_ReadTitleModel(CpuContext* ctx) {
    uint32_t data = ctx->gpr[5], size = ctx->gpr[6];
    const uint32_t outSize = ctx->gpr[7];
    bool expandedTitle = false;
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
                    expandedTitle = true;
                }
            }
        }
    }
    ctx->gpr[3] = data;
    ctx->gpr[4] = size;
    func_8000E978(ctx);
    if (expandedTitle) {
        if (SetTitleTextureEdges(ctx->gpr[3])) {
            RT_LOG(RT_TAG_RUNTIME) << "[title-backdrop] injected forest side panels with edge-addressed texture; original title box retained\n";
        } else {
            RT_LOG(RT_TAG_RUNTIME) << "[title-backdrop] cannot configure loaded title texture; edge seams may remain\n";
        }
    }
    Memory::Write32(outSize, 0x70);
}
PPC_NATIVE_OVERRIDE_VOID(800458D4, No100f_ReadTitleModel, (CpuContext* ctx), (ctx));
