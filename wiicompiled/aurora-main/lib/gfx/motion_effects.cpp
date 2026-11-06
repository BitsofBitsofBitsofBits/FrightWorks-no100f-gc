#include "motion_effects.hpp"

#include "../gx/gx.hpp"
#include "../internal.hpp"
#include "tracy/Tracy.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <initializer_list>
#include <string>
#include <vector>

namespace aurora::gfx::motion {
namespace {
using webgpu::g_device;
using webgpu::g_queue;

// ----- Tuning
// The longest blur, as a share of the picture's height (each way from the pixel: a streak is up to twice this).
constexpr float kMaxBlurRadiusOfHeight = 0.06f;
// Samples along each pixel's streak (odd: the middle one is the pixel itself).
constexpr int kBlurSamples = 21;
// The most frames of motion a pixel is smeared over.
constexpr float kMaxBlurFrames = 4.f;
// Below this many presented pixels of motion (each way) nothing is blurred.
constexpr float kMinBlurRadius = 0.5f;
// Depth classification (foreground / background of a sample): soft over this share of the nearer one's distance.
constexpr float kSoftDepthExtent = 0.05f;

std::atomic<float> g_blurShutter{0.f};
std::atomic_int g_viewMode{0};
std::atomic<float> g_viewScale{16.f};

void update_request() noexcept {
  request_motion_vectors(g_blurShutter.load(std::memory_order_acquire) > 0.f ||
                         g_viewMode.load(std::memory_order_acquire) != 0);
}

// ----- Full-screen passes

constexpr const char* kFullscreenTriangleWgsl = R"""(
@vertex
fn vs_main(@builtin(vertex_index) i: u32) -> @builtin(position) vec4f {
    let p = array(vec2f(-1.0, 1.0), vec2f(-1.0, -3.0), vec2f(3.0, 1.0));
    return vec4f(p[i], 0.0, 1.0);
}
)""";

// Every pass's parameters: 4 vec4f.
using PassParams = std::array<float, 16>;

struct Pass {
  wgpu::RenderPipeline pipeline;
  wgpu::TextureFormat format = wgpu::TextureFormat::Undefined;
  bool multisampledEfb = false;
  webgpu::TextureWithSampler target;
  wgpu::Buffer params;
};

// Creates the pass's pipeline (for `format`), parameter buffer and `width` x `height` target when missing or stale.
// The shader declares the EFB motion targets as EfbTexture: multisampled with MSAA, where textureLoad's last argument
// picks sample 0 instead of mip level 0.
void prepare(Pass& pass, const char* label, const char* fragmentWgsl, wgpu::TextureFormat format, uint32_t width,
             uint32_t height, bool multisampledEfb = false) {
  if (!pass.pipeline || pass.format != format || pass.multisampledEfb != multisampledEfb) {
    const std::string code = std::string(multisampledEfb ? "alias EfbTexture = texture_multisampled_2d<f32>;\n"
                                                         : "alias EfbTexture = texture_2d<f32>;\n") +
                             kFullscreenTriangleWgsl + fragmentWgsl;
    wgpu::ShaderSourceWGSL wgsl{};
    wgsl.code = code.c_str();
    const wgpu::ShaderModuleDescriptor moduleDescriptor{.nextInChain = &wgsl, .label = label};
    const auto module = g_device.CreateShaderModule(&moduleDescriptor);
    const wgpu::ColorTargetState target{.format = format};
    const wgpu::FragmentState fragment{.module = module, .entryPoint = "fs_main", .targetCount = 1, .targets = &target};
    const wgpu::RenderPipelineDescriptor descriptor{
        .label = label,
        .vertex = {.module = module, .entryPoint = "vs_main"},
        .primitive = {.topology = wgpu::PrimitiveTopology::TriangleList},
        .fragment = &fragment,
    };
    pass.pipeline = g_device.CreateRenderPipeline(&descriptor);
    pass.format = format;
    pass.multisampledEfb = multisampledEfb;
    pass.target = {};
    const wgpu::BufferDescriptor params{
        .label = label,
        .usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst,
        .size = sizeof(PassParams),
    };
    pass.params = g_device.CreateBuffer(&params);
  }
  auto& target = pass.target;
  if (!target.texture || target.size.width != width || target.size.height != height) {
    const wgpu::TextureDescriptor descriptor{
        .label = label,
        .usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding,
        .size = {width, height, 1},
        .format = format,
    };
    target.texture = g_device.CreateTexture(&descriptor);
    target.view = target.texture.CreateView();
    target.size = descriptor.size;
    target.format = format;
    const wgpu::SamplerDescriptor sampler{
        .addressModeU = wgpu::AddressMode::ClampToEdge,
        .addressModeV = wgpu::AddressMode::ClampToEdge,
        .magFilter = wgpu::FilterMode::Linear,
        .minFilter = wgpu::FilterMode::Linear,
    };
    target.sampler = g_device.CreateSampler(&sampler);
  }
}

// Draws the pass into its target. Bindings: the parameters at 0, then `inputs` from 1 on.
void run(const wgpu::CommandEncoder& encoder, Pass& pass, const PassParams& params,
         std::initializer_list<wgpu::TextureView> inputs) {
  g_queue.WriteBuffer(pass.params, 0, params.data(), sizeof(params));
  std::vector<wgpu::BindGroupEntry> entries{
      wgpu::BindGroupEntry{.binding = 0, .buffer = pass.params, .size = sizeof(params)},
  };
  for (const auto& view : inputs) {
    entries.push_back(wgpu::BindGroupEntry{.binding = static_cast<uint32_t>(entries.size()), .textureView = view});
  }
  const wgpu::BindGroupDescriptor bindGroupDescriptor{
      .layout = pass.pipeline.GetBindGroupLayout(0),
      .entryCount = entries.size(),
      .entries = entries.data(),
  };
  const auto bindGroup = g_device.CreateBindGroup(&bindGroupDescriptor);
  const wgpu::RenderPassColorAttachment attachment{
      .view = pass.target.view,
      .loadOp = wgpu::LoadOp::Clear,
      .storeOp = wgpu::StoreOp::Store,
      .clearValue = {0.0, 0.0, 0.0, 1.0},
  };
  const wgpu::RenderPassDescriptor passDescriptor{.colorAttachmentCount = 1, .colorAttachments = &attachment};
  const auto renderPass = encoder.BeginRenderPass(&passDescriptor);
  renderPass.SetPipeline(pass.pipeline);
  renderPass.SetBindGroup(0, bindGroup);
  renderPass.Draw(3);
  renderPass.End();
}

webgpu::PresentSource present_source_of(const Pass& pass, const webgpu::PresentSource& replaced) {
  const auto& target = pass.target;
  return {
      .bindGroup = webgpu::create_copy_bind_group(target),
      .texture = target.texture,
      .size = target.size,
      .format = target.format,
      .efbRect = replaced.efbRect, // same size as the picture it replaces
  };
}

// ----- The presented picture and the EFB
// The game presents a display copy of part of the EFB (480 of its 528 lines here), scaled: a presented pixel's
// motion, depth and HUD mark are those of the EFB texel it was copied from. Every pass that reads them gets the
// mapping as its first two parameter vectors.
struct Mapping {
  std::array<float, 4> rect; // EFB rectangle the picture was copied from (render pixels)
  float toEfbX, toEfbY;      // EFB pixels per presented pixel
};

Mapping mapping_of(const webgpu::PresentSource& source, const webgpu::TextureWithSampler& motion) {
  auto rect = source.efbRect;
  if (rect[2] <= 0.f || rect[3] <= 0.f) {
    rect = {0.f, 0.f, static_cast<float>(motion.size.width), static_cast<float>(motion.size.height)};
  }
  return {
      .rect = rect,
      .toEfbX = rect[2] / static_cast<float>(source.size.width),
      .toEfbY = rect[3] / static_cast<float>(source.size.height),
  };
}

// WGSL shared by the passes that read the EFB targets: params.v0 = the EFB rectangle, params.v1.xy = EFB pixels per
// presented pixel, .zw = the EFB size.
constexpr const char* kMappingWgsl = R"""(
struct Params {
    v0: vec4f,
    v1: vec4f,
    v2: vec4f,
    v3: vec4f,
};
@group(0) @binding(0) var<uniform> params: Params;

// The EFB texel a presented pixel (its position, pixel centers at .5) was copied from.
fn efb_texel(p: vec2f) -> vec2i {
    let efb = params.v0.xy + p * params.v1.xy;
    return vec2i(clamp(floor(efb), vec2f(0.0), params.v1.zw - 1.0));
}
)""";

// ----- The debug view

Pass g_displayMotionPass;
Pass g_viewPass;

// The motion of every presented pixel, in presented pixels (prev = pos + motion, y down).
const webgpu::TextureWithSampler& encode_display_motion(const wgpu::CommandEncoder& encoder,
                                                        const webgpu::PresentSource& source,
                                                        const MotionTargets& targets, const Mapping& map) {
  static const std::string wgsl = std::string(kMappingWgsl) + R"""(
@group(0) @binding(1) var motion: EfbTexture;

@fragment
fn fs_main(@builtin(position) pos: vec4f) -> @location(0) vec4f {
    let mv = textureLoad(motion, efb_texel(pos.xy), 0).xy;
    return vec4f(mv / params.v1.xy, 0.0, 0.0);
}
)""";
  prepare(g_displayMotionPass, "Display motion vectors", wgsl.c_str(), wgpu::TextureFormat::RG16Float,
          source.size.width, source.size.height, targets.motion.texture.GetSampleCount() > 1);
  run(encoder, g_displayMotionPass,
      {map.rect[0], map.rect[1], map.rect[2], map.rect[3], map.toEfbX, map.toEfbY,
       static_cast<float>(targets.motion.size.width), static_cast<float>(targets.motion.size.height)},
      {targets.motion.view});
  return g_displayMotionPass.target;
}

webgpu::PresentSource encode_view(const wgpu::CommandEncoder& encoder, const webgpu::PresentSource& source,
                                  const MotionTargets& targets, const Mapping& map, int mode) {
  const auto& displayMotion = encode_display_motion(encoder, source, targets, map);
  prepare(g_viewPass, "Motion vector view", R"""(
struct Params {
    v0: vec4f,  // x: mode, y: presented pixels of motion for full color
    v1: vec4f,
    v2: vec4f,
    v3: vec4f,
};
@group(0) @binding(0) var<uniform> params: Params;
@group(0) @binding(1) var motion: texture_2d<f32>;
@group(0) @binding(2) var picture: texture_2d<f32>;

fn hue(h: f32) -> vec3f {
    return clamp(abs(fract(vec3f(h) - vec3f(0.0, 1.0 / 3.0, 2.0 / 3.0)) * 2.0 - 1.0) * 3.0 - 1.0, vec3f(0.0), vec3f(1.0));
}

// Grey (or the picture, dimmed) where still; the hue is the direction the surface came from, the saturation its speed.
@fragment
fn fs_main(@builtin(position) pos: vec4f) -> @location(0) vec4f {
    let xy = vec2i(pos.xy);
    let mv = textureLoad(motion, xy, 0).xy;
    let base = select(vec3f(0.5), textureLoad(picture, xy, 0).rgb * 0.5 + 0.25, params.v0.x > 1.5);
    let speed = length(mv);
    if (!(speed > 0.05)) {
        return vec4f(base, 1.0);
    }
    let t = min(speed / params.v0.y, 1.0);
    return vec4f(mix(base, hue(atan2(mv.y, mv.x) / 6.2831853), t), 1.0);
}
)""",
          source.format, source.size.width, source.size.height);
  run(encoder, g_viewPass, {static_cast<float>(mode), std::max(g_viewScale.load(), 0.25f)},
      {displayMotion.view, source.texture.CreateView()});
  return present_source_of(g_viewPass, source);
}

// ----- Motion blur
// A reconstruction filter (McGuire, Hennessy, Bukowski, Osman, "A Reconstruction Filter for Plausible Motion Blur",
// I3D 2012): the largest motion in each tile of the picture, then in each tile's neighbourhood, bounds how far a
// pixel looks for samples; each sample along that motion counts by whether it is in front of the pixel or behind it
// and how far its own and the pixel's motion reach, so moving things blur over a still background at their edges and
// a still thing in front stays sharp. The HUD is never blurred and never blurs into anything.

Pass g_tileMaxXPass;
Pass g_tileMaxYPass;
Pass g_neighborMaxPass;
Pass g_blurPass;

// WGSL: each pixel's half streak (presented pixels; the streak runs from -v to +v around it), capped at the longest.
// params.v2.x = the exposure (shutter * frame fraction), .y = the longest half streak.
constexpr const char* kVelocityWgsl = R"""(
@group(0) @binding(1) var motion: EfbTexture;

fn half_streak(texel: vec2i) -> vec2f {
    let v = textureLoad(motion, texel, 0).xy / params.v1.xy * params.v2.x * 0.5;
    let len = length(v);
    return select(v, v * (params.v2.y / len), len > params.v2.y);
}

// Keeps the longer of two half streaks.
fn longer(a: vec2f, b: vec2f) -> vec2f {
    return select(a, b, dot(b, b) > dot(a, a));
}
)""";

webgpu::PresentSource encode_blur(const wgpu::CommandEncoder& encoder, const webgpu::PresentSource& source,
                                  const MotionTargets& targets, const Mapping& map, float exposure) {
  const uint32_t width = source.size.width;
  const uint32_t height = source.size.height;
  const bool multisampled = targets.motion.texture.GetSampleCount() > 1;
  const float maxRadius = std::max(1.f, std::round(static_cast<float>(height) * kMaxBlurRadiusOfHeight));
  const uint32_t tile = static_cast<uint32_t>(maxRadius);
  const uint32_t tilesX = (width + tile - 1) / tile;
  const uint32_t tilesY = (height + tile - 1) / tile;
  const PassParams params{
      map.rect[0],
      map.rect[1],
      map.rect[2],
      map.rect[3],
      map.toEfbX,
      map.toEfbY,
      static_cast<float>(targets.motion.size.width),
      static_cast<float>(targets.motion.size.height),
      exposure,
      maxRadius,
      static_cast<float>(tile),
      gx::UseReversedZ ? 1.f : 0.f,
      static_cast<float>(width),
      static_cast<float>(height),
      0.f,
      0.f,
  };

  // The longest half streak in each tile: across a row of the tile, then down the column of those.
  static const std::string tileMaxX = std::string(kMappingWgsl) + kVelocityWgsl + R"""(
@fragment
fn fs_main(@builtin(position) pos: vec4f) -> @location(0) vec4f {
    let tile = i32(params.v2.z);
    let x0 = i32(pos.x) * tile;
    let x1 = min(x0 + tile, i32(params.v3.x));
    var v = vec2f(0.0);
    for (var x = x0; x < x1; x++) {
        v = longer(v, half_streak(efb_texel(vec2f(f32(x), pos.y) + vec2f(0.5, 0.0))));
    }
    return vec4f(v, 0.0, 0.0);
}
)""";
  prepare(g_tileMaxXPass, "Motion blur tile max (rows)", tileMaxX.c_str(), wgpu::TextureFormat::RG16Float, tilesX,
          height, multisampled);
  run(encoder, g_tileMaxXPass, params, {targets.motion.view});

  prepare(g_tileMaxYPass, "Motion blur tile max", R"""(
struct Params {
    v0: vec4f,
    v1: vec4f,
    v2: vec4f,  // z: tile size
    v3: vec4f,  // y: picture height
};
@group(0) @binding(0) var<uniform> params: Params;
@group(0) @binding(1) var rows: texture_2d<f32>;

@fragment
fn fs_main(@builtin(position) pos: vec4f) -> @location(0) vec4f {
    let tile = i32(params.v2.z);
    let y0 = i32(pos.y) * tile;
    let y1 = min(y0 + tile, i32(params.v3.y));
    var v = vec2f(0.0);
    for (var y = y0; y < y1; y++) {
        let s = textureLoad(rows, vec2i(i32(pos.x), y), 0).xy;
        v = select(v, s, dot(s, s) > dot(v, v));
    }
    return vec4f(v, 0.0, 0.0);
}
)""",
          wgpu::TextureFormat::RG16Float, tilesX, tilesY);
  run(encoder, g_tileMaxYPass, params, {g_tileMaxXPass.target.view});

  // ... and the longest among each tile and its eight neighbours: how far any streak can reach into the tile.
  prepare(g_neighborMaxPass, "Motion blur neighbour max", R"""(
struct Params {
    v0: vec4f,
    v1: vec4f,
    v2: vec4f,
    v3: vec4f,
};
@group(0) @binding(0) var<uniform> params: Params;
@group(0) @binding(1) var tiles: texture_2d<f32>;

@fragment
fn fs_main(@builtin(position) pos: vec4f) -> @location(0) vec4f {
    _ = params.v0;  // every pass binds its parameters at 0, and an unused binding would leave the layout
    let size = vec2i(textureDimensions(tiles));
    var v = vec2f(0.0);
    for (var dy = -1; dy <= 1; dy++) {
        for (var dx = -1; dx <= 1; dx++) {
            let at = clamp(vec2i(pos.xy) + vec2i(dx, dy), vec2i(0), size - 1);
            let s = textureLoad(tiles, at, 0).xy;
            v = select(v, s, dot(s, s) > dot(v, v));
        }
    }
    return vec4f(v, 0.0, 0.0);
}
)""",
          wgpu::TextureFormat::RG16Float, tilesX, tilesY);
  run(encoder, g_neighborMaxPass, params, {g_tileMaxYPass.target.view});

  static const std::string gather = std::string(kMappingWgsl) + kVelocityWgsl +
                                    fmt::format(
                                        "const kSamples = {};\nconst kMinRadius = {:.3f};\n"
                                        "const kSoftDepth = {:.3f};\n",
                                        kBlurSamples, kMinBlurRadius, kSoftDepthExtent) +
                                    R"""(
@group(0) @binding(2) var depth: EfbTexture;  // the owning surface's depth, raw
@group(0) @binding(3) var mark: EfbTexture;   // the HUD
@group(0) @binding(4) var neighborMax: texture_2d<f32>;
@group(0) @binding(5) var picture: texture_2d<f32>;

// Distance from the camera, up to a constant factor (enough to compare): from the raw depth (params.v2.w: reversed,
// 1 near and 0 far, else 0 near).
fn distance_of(texel: vec2i) -> f32 {
    let raw = textureLoad(depth, texel, 0).x;
    return 1.0 / max(select(1.0 - raw, raw, params.v2.w > 0.5), 1e-6);
}

// 1 where a is in front of b (or level with it), fading to 0 as it lies further behind.
fn in_front(a: f32, b: f32) -> f32 {
    return clamp(1.0 - (a - b) / (kSoftDepth * min(a, b)), 0.0, 1.0);
}

// Whether a streak of half length `reach` covers a point `d` away: fading over its length (cone) or a hard edge
// (cylinder).
fn cone(d: f32, reach: f32) -> f32 {
    return clamp(1.0 - d / reach, 0.0, 1.0);
}
fn cylinder(d: f32, reach: f32) -> f32 {
    return 1.0 - smoothstep(0.95 * reach, 1.05 * reach, d);
}

fn is_hud(texel: vec2i) -> bool {
    return textureLoad(mark, texel, 0).x > 0.5;
}

// Interleaved gradient noise (0..1): a different value per pixel, without visible patterns.
fn noise(p: vec2f) -> f32 {
    return fract(52.9829189 * fract(dot(p, vec2f(0.06711056, 0.00583715))));
}

@fragment
fn fs_main(@builtin(position) pos: vec4f) -> @location(0) vec4f {
    let xy = vec2i(pos.xy);
    let here = textureLoad(picture, xy, 0);
    let texel = efb_texel(pos.xy);
    // The neighbourhood's longest streak, read a little off this pixel's tile (up to a quarter tile each way): the tile
    // edges become noise instead of straight lines where the longest streak changes from one tile to the next.
    let tile = params.v2.z;
    let tileJitter = (vec2f(noise(pos.xy + vec2f(37.0, 17.0)), noise(pos.xy + vec2f(11.0, 53.0))) - 0.5) * 0.5;
    let tiles = vec2i(textureDimensions(neighborMax));
    let reach = textureLoad(neighborMax, clamp(vec2i(floor(pos.xy / tile + tileJitter)), vec2i(0), tiles - 1), 0).xy;
    if (length(reach) < kMinRadius || is_hud(texel)) {
        return here;
    }
    let ownStreak = half_streak(texel);
    let ownReach = max(length(ownStreak), kMinRadius);
    // Half the samples follow the neighbourhood's longest streak (whatever may blur over this pixel), the other half
    // this pixel's own (Jimenez, "Next Generation Post Processing in Call of Duty: Advanced Warfare", 2014): otherwise
    // a fast leg nearby sets the direction for the background of the whole tile, and that blurs the wrong way.
    let ownDirection = select(reach, ownStreak, length(ownStreak) >= kMinRadius);
    let own = distance_of(texel);
    var weight = 1.0 / ownReach;
    var sum = here.rgb * weight;
    let jitter = noise(pos.xy) - 0.5;  // a different offset along the streak per pixel instead of visible steps
    let size = vec2i(params.v3.xy);
    for (var i = 0; i < kSamples; i++) {
        if (i == kSamples / 2) {
            continue;  // the pixel itself, counted above
        }
        let t = mix(-1.0, 1.0, (f32(i) + jitter + 1.0) / f32(kSamples + 1));
        let at = vec2i(floor(pos.xy + select(reach, ownDirection, (i & 1) == 1) * t));
        if (any(at < vec2i(0)) || any(at >= size)) {
            continue;
        }
        let atTexel = efb_texel(vec2f(at) + 0.5);
        if (is_hud(atTexel)) {
            continue;
        }
        let d = length(vec2f(at) + 0.5 - pos.xy);
        let other = distance_of(atTexel);
        let otherReach = length(half_streak(atTexel));
        // A sample in front whose own streak reaches here (a moving thing blurring over this pixel); a sample behind
        // while this pixel's streak reaches it (the background behind a moving thing); both moving across each other.
        let w = in_front(other, own) * cone(d, otherReach) + in_front(own, other) * cone(d, ownReach) +
                cylinder(d, otherReach) * cylinder(d, ownReach) * 2.0;
        weight += w;
        sum += textureLoad(picture, at, 0).rgb * w;
    }
    return vec4f(sum / weight, here.a);
}
)""";
  prepare(g_blurPass, "Motion blur", gather.c_str(), source.format, width, height, multisampled);
  run(encoder, g_blurPass, params,
      {targets.motion.view, targets.depth.view, targets.mark.view, g_neighborMaxPass.target.view,
       source.texture.CreateView()});
  return present_source_of(g_blurPass, source);
}
} // namespace

void set_blur(float shutter) noexcept {
  g_blurShutter.store(std::isfinite(shutter) ? std::clamp(shutter, 0.f, kMaxBlurFrames) : 0.f,
                      std::memory_order_release);
  update_request();
}

float blur() noexcept { return g_blurShutter.load(std::memory_order_acquire); }

void set_view(int mode, float scale) noexcept {
  g_viewMode.store(std::clamp(mode, 0, 2), std::memory_order_release);
  if (scale > 0.f) {
    g_viewScale.store(scale);
  }
  update_request();
}

int view() noexcept { return g_viewMode.load(std::memory_order_acquire); }

float view_scale() noexcept { return g_viewScale.load(); }

webgpu::PresentSource encode(const wgpu::CommandEncoder& encoder, const webgpu::PresentSource& source,
                             const MotionTargets& targets, float frameFraction) {
  ZoneScoped;
  if (!source.texture || source.size.width == 0 || source.size.height == 0 || !targets.motion.texture) {
    return source;
  }
  const auto map = mapping_of(source, targets.motion);
  auto result = source;
  if (const float exposure = blur() * frameFraction; exposure > 0.f) {
    result = encode_blur(encoder, result, targets, map, exposure);
  }
  if (const int mode = view(); mode != 0) {
    result = encode_view(encoder, result, targets, map, mode);
  }
  return result;
}

void shutdown() noexcept {
  g_displayMotionPass = {};
  g_viewPass = {};
  g_tileMaxXPass = {};
  g_tileMaxYPass = {};
  g_neighborMaxPass = {};
  g_blurPass = {};
}
} // namespace aurora::gfx::motion
