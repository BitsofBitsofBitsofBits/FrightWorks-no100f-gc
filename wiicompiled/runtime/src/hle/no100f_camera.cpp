// NO100F true widescreen: RwCameraSetViewWindow (0x80168FC0) sizes every camera's view (gameplay cameras are 4:3,
// y = 0.75 * x. In a widened picture mode the view window is widened to the picture's aspect, so the game culls and
// projects for the wide view itself (Aurora's projection widening only stretches what the game already decided to
// draw, which left cutscene and demo sides empty).
//
// Native in full: an override's address is excluded from translation, so the original body cannot be called.
// It is: camera->viewWindow (+0x68) = *viewWindow; camera->recipViewWindow (+0x70) = 1 / viewWindow; if the camera
// has a frame (+4), RwFrameUpdateObjects (0x8016A300) on it; return camera.
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include "abi_bridge.h"
#include "memory.h"
#include "hle_stubs.h"
#include "hle/no100f_camera.h"
#include "ppc_runtime.h"

extern std::atomic<float> g_gameCameraAspect;  // display_settings.cpp
extern "C" void func_8016A300(CpuContext* ctx);  // the game's RwFrameUpdateObjects

namespace {
float ReadF32(uint32_t a) { const uint32_t w = Memory::Read32(a); float f; std::memcpy(&f, &w, 4); return f; }
void WriteF32(uint32_t a, float f) { uint32_t w; std::memcpy(&w, &f, 4); Memory::Write32(a, w); }
}

extern "C" void No100f_CameraSetViewWindow(CpuContext* ctx) {
    const uint32_t camera = ctx->gpr[3], viewWindow = ctx->gpr[4];
    float x = ReadF32(viewWindow), y = ReadF32(viewWindow + 4);
    const float aspect = g_gameCameraAspect.load(std::memory_order_relaxed);
    // Widen landscape camera frusta to at least the picture aspect. Square/taller views (the shadow pass) are left alone.
    x = No100fCamera::WidenedViewWindowWidth(x, y, aspect);
    WriteF32(camera + 0x68, x);
    WriteF32(camera + 0x6C, y);
    WriteF32(camera + 0x70, 1.0f / x);
    WriteF32(camera + 0x74, 1.0f / y);
    if (const uint32_t frame = Memory::Read32(camera + 4)) {
        ctx->gpr[3] = frame;
        func_8016A300(ctx);
    }
    ctx->gpr[3] = camera;
}
PPC_NATIVE_OVERRIDE_VOID(80168FC0, No100f_CameraSetViewWindow, (CpuContext* ctx), (ctx));
