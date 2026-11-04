#include "abi_bridge.h"
#include "hle/no100f_map_backdrop.h"
#include "hle_stubs.h"
#include "memory.h"
#include "runtime_log.h"

#include <atomic>

extern std::atomic<float> g_gameCameraAspect;
extern "C" void func_8003FF84(CpuContext* ctx); // xSTFindAsset
extern "C" void func_80160B10(CpuContext* ctx); // RwRenderStateSet
extern "C" void func_80160B70(CpuContext* ctx); // RwRenderStateGet

namespace {

constexpr uint32_t kUiDrawReturn = 0x800D3984u;
constexpr uint32_t kScratchSize = 0x2C0u;
constexpr uint32_t kVertexOffset = 0x40u;
constexpr uint32_t kIndexOffset = kVertexOffset + 20 * sizeof(No100fMapBackdrop::Vertex);
static_assert(kIndexOffset + No100fMapBackdrop::kIndices.size() * sizeof(uint16_t) <= kScratchSize);

void ReportFailure(uint32_t flag, const char* message) {
    static uint32_t reported = 0;
    if (!(reported & flag)) {
        reported |= flag;
        RT_LOG(RT_TAG_RUNTIME) << "[map-backdrop] " << message << '\n';
    }
}

bool ReadMapSource(CpuContext* ctx, float aspect, std::array<No100fMapBackdrop::Vertex, 4>& source) {
    using namespace No100fMapBackdrop;
    if (ctx->lr != kUiDrawReturn || ctx->gpr[3] != 3 || ctx->gpr[5] != 4 || ctx->gpr[7] != 6)
        return false;
    const uint32_t ui = ctx->gpr[30];
    if (!Memory::Contains(ui, 0xF4)) return false;
    const uint32_t asset = Memory::Read32(ui + 0xEC);
    if (!Memory::Contains(asset, 124) ||
        !MatchesAsset(Memory::Read32(asset), Memory::Read32(asset + 0x58))) return false;

    const auto incompatible = [] {
        ReportFailure(1, "incompatible Map UI/vertices or guest stack; original retained");
        return false;
    };
    if (Memory::Read32(asset + 0x54) != 0x028001E0u ||
        !Memory::Contains(ctx->gpr[4], 4 * sizeof(Vertex)) || !Memory::Contains(ctx->gpr[6], 12) ||
        ctx->gpr[1] < kScratchSize || !Memory::Contains(ctx->gpr[1] - kScratchSize, kScratchSize + 8))
        return incompatible();
    for (unsigned i = 0; i < 6; ++i)
        if (Memory::Read16(ctx->gpr[6] + i * 2) != kIndices[i]) return incompatible();
    for (unsigned i = 0; i < source.size(); ++i) {
        const uint32_t address = ctx->gpr[4] + i * sizeof(Vertex);
        source[i] = {Memory::ReadFloat32(address), Memory::ReadFloat32(address + 4),
                     Memory::ReadFloat32(address + 8), Memory::Read32(address + 12),
                     Memory::ReadFloat32(address + 16), Memory::ReadFloat32(address + 20),
                     Memory::Read32(address + 24)};
    }
    if (!MatchesVertices(source) || !Finite(aspect) || !Finite(240.0f * aspect)) return incompatible();
    return true;
}

void DispatchIndexed(CpuContext* ctx, uint32_t frameSize) {
    const uint32_t stack = ctx->gpr[1], lr = ctx->lr;
    ctx->gpr[1] = stack - frameSize;
    Memory::Write32(ctx->gpr[1], stack);
    Memory::Write32(stack + 4, lr);
    ctx->gpr[8] = Memory::Read32(ctx->gpr[13] - 0x6C04);
    ctx->gpr[12] = Memory::Read32(ctx->gpr[8] + 0x34);
    ctx->ctr = ctx->gpr[12];
    ctx->lr = 0x80160C10u;
    InvokeIndirectCpu(ctx->gpr[12], ctx);
    ctx->gpr[0] = Memory::Read32(stack + 4);
    ctx->lr = ctx->gpr[0];
    ctx->gpr[1] = stack;
}

template <size_t N>
bool Draw(CpuContext* ctx, const std::array<No100fMapBackdrop::Vertex, N>& vertices, unsigned indexCount) {
    const uint32_t scratch = ctx->gpr[1] - kScratchSize;
    for (unsigned i = 0; i < vertices.size(); ++i) {
        const uint32_t address = scratch + kVertexOffset + i * sizeof(No100fMapBackdrop::Vertex);
        const auto& v = vertices[i];
        Memory::WriteFloat32(address, v.x);
        Memory::WriteFloat32(address + 4, v.y);
        Memory::WriteFloat32(address + 8, v.z);
        Memory::Write32(address + 12, v.color);
        Memory::WriteFloat32(address + 16, v.u);
        Memory::WriteFloat32(address + 20, v.v);
        Memory::Write32(address + 24, v.reciprocalZ);
    }
    for (unsigned i = 0; i < indexCount; ++i)
        Memory::Write16(scratch + kIndexOffset + i * 2, No100fMapBackdrop::kIndices[i]);
    ctx->gpr[3] = 3;
    ctx->gpr[4] = scratch + kVertexOffset;
    ctx->gpr[5] = static_cast<uint32_t>(vertices.size());
    ctx->gpr[6] = scratch + kIndexOffset;
    ctx->gpr[7] = indexCount;
    DispatchIndexed(ctx, kScratchSize);
    return ctx->gpr[3] != 0;
}

uint32_t FindForestRaster(CpuContext* ctx) {
    CpuContext call = *ctx;
    call.gpr[1] -= 0x20;
    call.gpr[3] = No100fMapBackdrop::kForestTexture;
    call.gpr[4] = 0;
    func_8003FF84(&call);
    const uint32_t texture = call.gpr[3];
    if (!texture || !Memory::Contains(texture, 0x54) ||
        std::memcmp(Memory::GetPointer(texture + 0x10, 8), "bkg_hub", 8) != 0) return 0;
    const uint32_t raster = Memory::Read32(texture);
    if (!raster || !Memory::Contains(raster, 0x18) ||
        Memory::Read32(raster + 0xC) != 256 || Memory::Read32(raster + 0x10) != 256) return 0;
    return raster;
}

bool GetRenderState(CpuContext* ctx, uint32_t state, uint32_t& value) {
    CpuContext call = *ctx;
    call.gpr[1] -= 0x20;
    const uint32_t out = call.gpr[1] + 8;
    Memory::Write32(out, 0);
    call.gpr[3] = state;
    call.gpr[4] = out;
    func_80160B70(&call);
    value = Memory::Read32(out);
    return call.gpr[3] != 0;
}

bool SetRenderState(CpuContext* ctx, uint32_t state, uint32_t value) {
    CpuContext call = *ctx;
    call.gpr[1] -= 0x20;
    call.gpr[3] = state;
    call.gpr[4] = value;
    func_80160B10(&call);
    return call.gpr[3] != 0;
}

struct RestoreForestState {
    CpuContext* ctx;
    uint32_t raster, addressU;
    bool active = true;

    bool Restore() {
        if (!active) return true;
        active = false;
        const bool restoredRaster = SetRenderState(ctx, 1, raster);
        const bool restoredAddress = SetRenderState(ctx, 3, addressU);
        if (!restoredRaster) ReportFailure(16, "cannot restore the Map raster");
        if (!restoredAddress) ReportFailure(128, "cannot restore the Map U addressing");
        return restoredRaster && restoredAddress;
    }
    ~RestoreForestState() { if (active) Restore(); }
};

bool DrawMap(CpuContext* ctx, const std::array<No100fMapBackdrop::Vertex, 4>& source, float aspect) {
    using namespace No100fMapBackdrop;
    const uint32_t forest = FindForestRaster(ctx);
    uint32_t originalRaster = 0, originalAddressU = 0;
    if (!forest || !GetRenderState(ctx, 1, originalRaster) || !originalRaster ||
        !GetRenderState(ctx, 3, originalAddressU)) {
        ReportFailure(2, "resident forest texture/raster state unavailable; original retained");
        return false;
    }
    RestoreForestState restore{ctx, originalRaster, originalAddressU};
    if (!SetRenderState(ctx, 1, forest)) {
        ReportFailure(4, "cannot bind the forest raster");
        ctx->gpr[3] = 0;
        return true;
    }
    // UI preset 1 selects WRAP. At U=0, bilinear filtering would pull in the opposite Map edge.
    // Clamp the compound Map too, so its feather does not sample the opposite image edge.
    // Both states are restored before returning to the original UI renderer.
    if (!SetRenderState(ctx, 3, 3)) {
        ReportFailure(64, "cannot clamp the backdrop U addressing");
        ctx->gpr[3] = 0;
        return true;
    }
    const float wingWidth = 240.0f * aspect - 320.0f;
    for (bool right : {false, true}) {
        for (unsigned panel = 0; float(panel) * kForestPanelWidth < wingWidth; ++panel) {
            const float offset = float(panel) * kForestPanelWidth;
            const float end = std::min(float(panel + 1) * kForestPanelWidth, wingWidth);
            if (!Draw(ctx, BuildForestPanel(source, right, offset, end), 6)) {
                ReportFailure(8, "forest panel draw failed");
                ctx->gpr[3] = 0;
                return true;
            }
        }
        // Continue the forest underneath the translucent inner Map strip, reflecting at x=0/640.
        const float inset = std::min(kMapInset, wingWidth);
        if (!Draw(ctx, BuildForestPanel(source, right, -inset, 0), 6)) {
            ReportFailure(256, "forest edge underlay failed");
            ctx->gpr[3] = 0;
            return true;
        }
    }
    if (!SetRenderState(ctx, 1, originalRaster)) {
        ReportFailure(16, "cannot restore the Map raster");
        ctx->gpr[3] = 0;
        return true;
    }
    const bool drawn = Draw(ctx, BuildVertices(source, aspect), static_cast<unsigned>(kIndices.size()));
    if (!drawn) ReportFailure(32, "Map picture draw failed");
    if (!restore.Restore()) {
        ctx->gpr[3] = 0;
        return true;
    }
    if (!drawn) return true;
    static bool reported = false;
    if (!reported) {
        RT_LOG(RT_TAG_RUNTIME) << "[map-backdrop] original Map mapping retained; forest fill with two-sided Map-edge feather\n";
        reported = true;
    }
    return true;
}

} // namespace

// GIHE78 rev 1 RwIm2DRenderIndexedPrimitive: dispatch through RwEngineInstance +0x34.
// Retain the original linkage frame, engine in r8, indirect target in r12/CTR and callback return value.
extern "C" void No100f_DrawIndexed2D(CpuContext* ctx) {
    const float aspect = g_gameCameraAspect.load(std::memory_order_relaxed);
    std::array<No100fMapBackdrop::Vertex, 4> source{};
    if (aspect > 4.0f / 3.0f && ReadMapSource(ctx, aspect, source) && DrawMap(ctx, source, aspect)) return;
    DispatchIndexed(ctx, 0x10u);
}
PPC_NATIVE_OVERRIDE_VOID(80160BF4, No100f_DrawIndexed2D, (CpuContext* ctx), (ctx));
