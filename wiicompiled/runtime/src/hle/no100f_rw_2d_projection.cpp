// NO100F: RenderWare's 2D projections are one pixel short (seam around the top-left 256x256 of the picture in every
// level; the original in Dolphin has it too).
//
// RenderWare's GameCube driver draws in pixel coordinates in three places: the Im2D setup (0x801727C8: HUD, text,
// fades, anything RwIm2D), the camera clear (0x8017A3EC, used by 0x8017A71C) and 0x80179734. Each sets the viewport
// to the whole framebuffer (fbWidth x xfbHeight pixels) but builds its orthographic projection over 0..fbWidth - 1 x
// 0..xfbHeight - 1 (C_MTXOrtho at 0x80172A84; projection vectors with 2/(fbWidth - 1), -2/(xfbHeight - 1), or the
// precomputed 2/639, -2/527, for GXSetProjectionv at 0x8017A6EC and 0x80179A60). So every 2D vertex lands
// x * 640/639, y * 480/479 from the top-left corner: most 2D art is just 0.4 pixel bigger.
// The shadow pass (0x8003B548) is where it shows. In the middle of the frame it copies the top-left 256x256 of the
// scene to a texture, clears that corner (camera clear), renders the shadow camera into it, inverts it (Im2D quad,
// 0x8003B2C8), copies the shadow out, and draws the saved corner back (Im2D quad (0,0)-(256,256), UV 0-1, nearest;
// 0x8003B47C). Clear, invert and restore all come out 256.4 x 256.5 pixels: the restore covers what the other two
// spill over the box, but the restored scene is enlarged against the scene around it. At 640x480 one row repeats at
// y = 256; at higher resolutions everything inside the box sits up to a screen pixel off along its right and bottom
// edges, and textures crossing them look cut. (Correcting only the restore quad uncovers the strip the clear and
// invert spill.)
// Here those three projections span 0..fbWidth x 0..xfbHeight, so 2D coordinates are pixel coordinates. The three
// calls are told apart by their return address; every other projection is built and loaded as before.
//
// Native in full: an override's address is excluded from translation, so the original body cannot be called.
// C_MTXOrtho(m, t, b, l, r, n, f) (0x80103544), single precision: m = [2/(r-l) 0 0 -(r+l)/(r-l); 0 2/(t-b) 0
// -(t+b)/(t-b); 0 0 -1/(f-n) -f/(f-n); 0 0 0 1]. Its other caller (0x800DB268) is unchanged.
// GXSetProjectionv(v) (0x801025A8): __GXData (*(r13 - 0x7DB8)) +0x420 = (u32)v[0] (projection type), +0x424..+0x438 =
// v[1..6]; FIFO: 0x10, 0x00061020, v[1..6], type (XF projection registers); __GXData +2 (u16) = 1. Its other callers
// (camera begin update 0x80176698, the Im2D restore of the camera's projection 0x80172E48) are unchanged.
#include <cmath>
#include <cstdint>
#include "abi_bridge.h"
#include "memory.h"
#include "hle_stubs.h"
#include "ppc_runtime.h"

namespace {
constexpr uint32_t kIm2DOrthoReturn = 0x80172A88u;        // after C_MTXOrtho at 0x80172A84 (Im2D setup)
constexpr uint32_t kClearProjectionReturn = 0x8017A6F0u;  // after GXSetProjectionv at 0x8017A6EC (camera clear)
constexpr uint32_t kBlitProjectionReturn = 0x80179A64u;   // after GXSetProjectionv at 0x80179A60 (0x80179734)
constexpr uint32_t kGxFifo = 0xCC008000u;
constexpr int32_t kGxDataSda = -0x7DB8;  // [r13 - 0x7DB8]: __GXData

// A 2D scale 2/(n - 1) (or -2/(n - 1)) from those projection vectors, as 2/n: n - 1 is a whole pixel count.
float FullPixelScale(float scale) {
    const float span = std::round(2.0f / std::fabs(scale));
    return std::copysign(2.0f / (span + 1.0f), scale);
}
}  // namespace

extern "C" void No100f_MtxOrtho(CpuContext* ctx) {
    const uint32_t m = ctx->gpr[3];
    const float t = static_cast<float>(ctx->fpr[1].d), l = static_cast<float>(ctx->fpr[3].d);
    const float n = static_cast<float>(ctx->fpr[5].d), f = static_cast<float>(ctx->fpr[6].d);
    float b = static_cast<float>(ctx->fpr[2].d), r = static_cast<float>(ctx->fpr[4].d);
    if (ctx->lr == kIm2DOrthoReturn) {
        // The Im2D setup passes bottom = xfbHeight - 1 and right = fbWidth - 1 (whole numbers): span the full pixels.
        b += 1.0f;
        r += 1.0f;
    }
    const float invRl = 1.0f / (r - l), invTb = 1.0f / (t - b), invFn = 1.0f / (f - n);
    const float row[16] = {
        2.0f * invRl, 0.0f, 0.0f, invRl * -(r + l),
        0.0f, 2.0f * invTb, 0.0f, invTb * -(t + b),
        0.0f, 0.0f, -1.0f * invFn, -f * invFn,
        0.0f, 0.0f, 0.0f, 1.0f,
    };
    for (uint32_t i = 0; i < 16; ++i) Memory::WriteFloat32(m + i * 4, row[i]);
}
PPC_NATIVE_OVERRIDE_VOID(80103544, No100f_MtxOrtho, (CpuContext* ctx), (ctx));

extern "C" void No100f_SetProjectionv(CpuContext* ctx) {
    const uint32_t src = ctx->gpr[3];
    float v[7];
    for (uint32_t i = 0; i < 7; ++i) v[i] = Memory::ReadFloat32(src + i * 4);
    if (ctx->lr == kClearProjectionReturn || ctx->lr == kBlitProjectionReturn) {
        v[1] = FullPixelScale(v[1]);  // offsets v[2] = -1, v[4] = +1 keep 0 at the top-left corner
        v[3] = FullPixelScale(v[3]);
    }
    const uint32_t type = v[0] > 0.0f ? static_cast<uint32_t>(v[0]) : 0u;  // __cvt_fp2unsigned
    const uint32_t gx = Memory::Read32(ctx->gpr[13] + kGxDataSda);
    Memory::Write32(gx + 0x420, type);
    for (uint32_t i = 1; i < 7; ++i) Memory::WriteFloat32(gx + 0x420 + i * 4, v[i]);
    Memory::Write8(kGxFifo, 0x10);
    Memory::Write32(kGxFifo, 0x00061020u);
    for (uint32_t i = 1; i < 7; ++i) Memory::WriteFloat32(kGxFifo, v[i]);
    Memory::Write32(kGxFifo, type);
    Memory::Write16(gx + 2, 1);
}
PPC_NATIVE_OVERRIDE_VOID(801025A8, No100f_SetProjectionv, (CpuContext* ctx), (ctx));
