/**
 * @file    gpu/render/surfaces.cpp
 */
#include "renut_engine/gpu/render/surfaces.h"

#include <cmath>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <rex/cvar.h>

#include "renut_engine/gpu/backend/backend.h"
#include "renut_engine/gpu/banjo_renut_tuning.h"
#include "renut_engine/gpu/d3d/d3d.h"
#include "renut_engine/gpu/render/format.h"
#include "renut_engine/gpu/render/fsr.h"
#include "renut_engine/gpu/render/graphics_options.h"
#include "renut_engine/gpu/render/textures.h"
#include "renut_engine/gpu/guest/guest_memory.h"
#include "renut_engine/gpu/guest/widescreen.h"
#include "renut_engine/gpu/log.h"
#include "renut_engine/gpu/backend/video.h"

// FFXIII-2-Native-Switch (2026-11-10, katamari-nx's msaa_level): the most samples a target the game makes with MSAA
// gets: 2 (FF XIII-2's own 2x) or 1 (single-sampled; NOT pixel-identical). Read when a target is made.
REXCVAR_DEFINE_INT32(msaa_level, 2, "FFNS/Graphics",
                     "The most MSAA samples a game target gets: 2 = the game's 2x, 1 = off (NOT pixel-identical; "
                     "applies at the next start)");
// FFXIII-2-Native-Switch (2026-11-12, the user: "The message in the blue boxes is hard to read" / "Yes do it"): see
// surfaces.h. Not with the options menu's sharpen / FSR 1 upscaling (those take the whole frame at the lowered height).
REXCVAR_DEFINE_BOOL(ffns_ui_native, true, "FFNS/Graphics",
                    "deko3d, with a render height below 720: text, HUD and menus drawn at the display's 720 lines "
                    "over the 3D image scaled up once (render/ui_scale.cpp). Off: the whole frame at the render "
                    "height, scaled at the scan-out. Applies at the next frame");
REXCVAR_DEFINE_STRING(renut_resolution, "720p", "Nuts&Bolts/Graphics",
                      "Internal render resolution, by its height: 480p, 720p (the game's own), "
                      "1080p, 1440p or 4K (2160 lines). Applies from the next frame.")
    .allowed({"480p", "720p", "1080p", "1440p", "4K"});

// peggle2-nx (2026-10-20): the render height of Peggle 2's own keys, per console mode (zumasrevenge_render_height_handheld
// / _docked: 720 / 1080 by default; the PC's simulated mode is F9 / zumasrevenge_render_docked), as the native renderer
// renders, instead of renut_resolution.
REXCVAR_DEFINE_BOOL(zumasrevenge_renut_resolution_by_mode, true, "ZumasRevenge",
                    "Renut's renderer: render at zumasrevenge_render_height_handheld / _docked for the console's mode (as "
                    "the native renderer); false = renut_resolution (and 1280x720 on deko3d)");

namespace zumasrevenge::render {
uint32_t WantedScaleQuarters();  // src/me/zumasrevenge_render_mode.cpp: the height / 180 for the console's mode
}

namespace renut::gpu {

namespace {

// ResolutionScale, once latched.
RenderScale g_resolution_scale;
bool g_resolution_latched = false;

// FFXIII-2-Native-Switch (2026-11-12, ffns_ui_native; renut-nx's): the composite's UI target and this frame's redirect to
// it, how many frames in a row have had no composite (kMenuFrames of them: 2D screens), and the surface the last frame
// resolved to the front buffer (the one a frame of 2D screens draws into).
constexpr uint32_t kMenuFrames = 30;
struct UiTarget {
  HostSurface *scene = nullptr;  // the target it was made for, and that target's generation then
  uint64_t scene_generation = 0;
  std::unique_ptr<HostSurface> surface;
  bool redirect = false;
};
UiTarget g_ui, g_ui_depth;
bool g_composite_seen = false;
uint32_t g_frames_without_composite = kMenuFrames;
bool g_ui_native_frame = false;  // UiNativeWanted, latched at the Swap
const HostSurface *g_frame_resolve_source = nullptr;  // this frame's last whole-screen colour resolve
uint64_t g_frame_resolve_generation = 0;
const HostSurface *g_screen_source = nullptr;  // the frame before's
uint64_t g_screen_source_generation = 0;

struct FramebufferKey {
  const HostSurface *colors[d3d::kMaxRenderTargets] = {};
  uint64_t color_generations[d3d::kMaxRenderTargets] = {};
  const HostSurface *depth = nullptr;
  uint64_t depth_generation = 0;

  bool operator==(const FramebufferKey &o) const {
    for (uint32_t i = 0; i < d3d::kMaxRenderTargets; ++i) {
      if (colors[i] != o.colors[i] || color_generations[i] != o.color_generations[i])
        return false;
    }
    return depth == o.depth && depth_generation == o.depth_generation;
  }
};

struct CachedFramebuffer {
  FramebufferKey key;
  std::unique_ptr<plume::RenderFramebuffer> framebuffer;
};

// A D3DSurface is a view of EDRAM: its base tile (RB_COLOR_INFO /
// RB_DEPTH_INFO bits 0-11), pitch (RB_SURFACE_INFO bits 0-13) and sample
// count. Surface objects over the same EDRAM with the same layout see the same
// pixels on the GPU, and N&B relies on it: the depth surface its after-effects
// pass binds is a second object over the scene's depth buffer (the character
// holograms test against it), and each hologram renders through surfaces of
// its own at the same EDRAM bases. So host targets belong to the EDRAM view,
// shared by every surface object over it, as long as they also agree on the
// host format and render scale: graphics_options renders some surfaces at
// another scale (the post-processing buffers at 1x), and a view used by
// surfaces of both kinds would otherwise be rebuilt at every switch.
uint64_t TargetKey(const d3d::SurfaceDesc &desc, bool depth, plume::RenderFormat format,
                   RenderScale scale) {
  const u32 info = desc.SurfaceInfo;
  return uint64_t(u32(desc.ColorInfo) & 0xFFF) | uint64_t(info & 0x3FFF) << 12 |
         uint64_t((info >> 16) & 3) << 26 | uint64_t(depth) << 28 |
         uint64_t(scale.code & 7) << 29 | uint64_t(u32(format)) << 32;
}

std::unordered_map<uint64_t, std::unique_ptr<HostSurface>> g_targets;
// Surface VA -> its target's key, and how many surfaces use each target.
std::unordered_map<u32, uint64_t> g_surface_keys;
std::unordered_map<uint64_t, uint32_t> g_target_users;
std::vector<CachedFramebuffer> g_framebuffers;
// banjo-nx (2026-10-19, render-thread proposal 8): the last framebuffers looked up, checked before the search of every
// framebuffer made (a linear search with an 80-byte compare at every draw's BindTargets); emptied whenever a cached
// framebuffer is retired (g_framebuffer_epoch).
struct RecentFramebuffer {
  FramebufferKey key;
  plume::RenderFramebuffer *framebuffer = nullptr;
  uint64_t epoch = 0;
};
RecentFramebuffer g_recent_framebuffers[4];
uint64_t g_framebuffer_epoch = 1;
uint64_t g_next_generation = 1;
// banjo-nx (2026-10-19, banjo_deko3d_depth24): the D3D depth formats each EDRAM depth view has been bound as (1 D24S8,
// 2 D24FS8). The host format is part of a target's key, so a view used as both would get two targets where the console
// has one memory: once a view has been bound as D24FS8, every surface over it takes the 32-bit float format.
std::unordered_map<uint64_t, uint8_t> g_depth_view_formats;
// SetTilingExtentLocked; 0 x 0 when not tiling.
uint32_t g_tiling_width = 0;
uint32_t g_tiling_height = 0;

// Drops cached framebuffers that reference 'surface'.
void ForgetFramebuffers(const HostSurface *surface) {
  for (size_t i = 0; i < g_framebuffers.size();) {
    const FramebufferKey &key = g_framebuffers[i].key;
    bool uses = key.depth == surface;
    for (const HostSurface *color : key.colors)
      uses = uses || color == surface;
    if (uses) {
      Video::RetireLocked(std::move(g_framebuffers[i].framebuffer));
      g_framebuffers[i] = std::move(g_framebuffers.back());
      g_framebuffers.pop_back();
      ++g_framebuffer_epoch;
    } else {
      ++i;
    }
  }
}

// Retires the target and everything made over it (deferred past in-flight
// frames).
// banjo-nx (2026-10-19, CPU proposal 8, banjo_renut_cpu_memos): GetHostSurfaceLocked's result per surface VA and the
// three descriptor words it reads, valid while nothing it reads has changed (g_surface_epoch: bumped by every target
// built, retired, unused or rebound, the resolution scale latched and the tiling extent set).
struct SurfaceMemo {
  u32 va = 0, size = 0, info = 0, format = 0;
  uint64_t epoch = 0;
  HostSurface *surface = nullptr;
};
SurfaceMemo g_surface_memo[64];  // banjo-nx (2026-10-19, render-thread proposal 8): 64, indexed by a hash of the VA
uint64_t g_surface_epoch = 1;

void FreeDepthViews(std::vector<DepthImage::View> &views) {
  for (DepthImage::View &v : views) {
    if (v.slot != UINT32_MAX && v.slot != kInvalidDescriptor)
      Video::FreeTextureSlotLocked(v.slot);
    Video::RetireLocked(std::move(v.view));
  }
  views.clear();
}

void RetireTarget(HostSurface &surface) {
  // FFXIII-2-Native-Switch (2026-11-12, ffns_ui_native): the composite's target goes, and its UI target with it.
  for (UiTarget *u : {&g_ui, &g_ui_depth}) {
    if (&surface == u->scene) {
      u->scene = nullptr;
      u->redirect = false;
      if (u->surface && u->surface->texture)
        RetireTarget(*u->surface);
    }
  }
  if (&surface == g_screen_source)
    g_screen_source = nullptr;
  if (&surface == g_frame_resolve_source)
    g_frame_resolve_source = nullptr;
  if (surface.depth && surface.handed)
    DepthHandOverBeforeRetireLocked(&surface);  // banjo-nx (2026-10-19, banjo_deko3d_depth_handover)
  FreeDepthViews(surface.handover_views);
  surface.stencil_used = false;
  ++g_surface_epoch;
  ForgetFramebuffers(&surface);
  if (surface.sample_slot != UINT32_MAX)
    Video::FreeTextureSlotLocked(surface.sample_slot);
  surface.sample_slot = UINT32_MAX;
  Video::RetireLocked(std::move(surface.sample_view));
  if (surface.stencil_slot != UINT32_MAX && surface.stencil_slot != kInvalidDescriptor)
    Video::FreeTextureSlotLocked(surface.stencil_slot);
  surface.stencil_slot = UINT32_MAX;
  Video::RetireLocked(std::move(surface.stencil_view));
  Video::RetireLocked(std::move(surface.texture));
}

plume::RenderSampleCounts SampleCount(u32 info_word) {
  switch ((info_word >> 16) & 3) {
  case 1:
    return plume::RenderSampleCount::COUNT_2;
  case 2:
    return plume::RenderSampleCount::COUNT_4;
  default:
    return plume::RenderSampleCount::COUNT_1;
  }
}

// At least 'min_width' x 'min_height' (see SetTilingExtentLocked), at 'scale'.
bool BuildTarget(HostSurface &surface, const d3d::SurfaceDesc &desc, uint32_t min_width,
                 uint32_t min_height, RenderScale scale) {
  surface.size_word = desc.Size;
  surface.info_word = desc.SurfaceInfo;
  surface.format_word = desc.Format;
  surface.width = std::max(d3d::SurfaceWidth(surface.size_word), min_width);
  surface.height = std::max(d3d::SurfaceHeight(surface.size_word), min_height);
  surface.scale = scale;
  std::fill(std::begin(surface.profile_labels), std::end(surface.profile_labels), UINT32_MAX);
  const uint32_t host_width = surface.scale.Size(surface.width);
  const uint32_t host_height = surface.scale.Size(surface.height);
  surface.depth = d3d::IsDepthFormat(surface.format_word);
  surface.samples = SampleCount(surface.info_word);
  // FFXIII-2-Native-Switch (2026-11-10, msaa_level): only lowers a count.
  if (REXCVAR_GET(msaa_level) <= 1 && uint32_t(surface.samples) > 1) {
    static uint32_t logged = 0;
    if (logged++ < 8)
      RGPU_INFO("Surface {:08X}: {}x{} x{} MSAA made single-sampled (msaa_level 1)", surface.guest_va, surface.width,
                surface.height, uint32_t(surface.samples));
    surface.samples = plume::RenderSampleCount::COUNT_1;
  }
  if (surface.depth) {
    surface.format = DepthTargetFormat(surface.format_word);  // banjo-nx (2026-10-19, banjo_deko3d_depth24)
    if (banjo::renut_gpu::Depth24On()) {
      // As GetHostSurfaceSlow keys it: a view ever bound as D24FS8 is 32-bit float for every surface over it.
      const auto it = g_depth_view_formats.find(TargetKey(desc, true, plume::RenderFormat::UNKNOWN, scale));
      if (it != g_depth_view_formats.end() && (it->second & 2))
        surface.format = DepthTargetFormat();
    }
    if (surface.format != DepthTargetFormat()) {
      static uint32_t logged = 0;
      if (logged++ < 16)
        RGPU_INFO("Depth target {:08X}: {}x{} x{} samples, 24-bit unorm depth with stencil (banjo_deko3d_depth24)",
                  surface.guest_va, surface.width, surface.height, u32(surface.samples));
    }
  } else {
    surface.format = SurfaceColorFormat(desc);
  }

  plume::RenderDevice *device = Video::Device();
  const plume::RenderMultisampling multisampling(surface.samples);
  // Sampled too: resolves read them, and so does scan-out of a bound target.
  if (surface.depth) {
    surface.texture = device->createTexture(plume::RenderTextureDesc::DepthTarget(
        host_width, host_height, surface.format, multisampling));
  } else {
    surface.texture = device->createTexture(plume::RenderTextureDesc::ColorTarget(
        host_width, host_height, surface.format, multisampling));
  }
  surface.layout = plume::RenderTextureLayout::UNKNOWN;
  surface.generation = g_next_generation++;
  if (!TextureCreated(surface.texture.get())) {
    surface.texture.reset();
    if (!ReportDeviceLost(device)) {
      RGPU_ERROR("Surface {:08X}: {}x{} {} target creation failed", surface.guest_va,
                 host_width, host_height, surface.depth ? "depth" : "color");
    }
    return false;
  }
  surface.texture->setName(surface.depth ? "renut depth surface" : "renut color surface");
  RGPU_DEBUG("Surface {:08X}: {}x{} (x{:.3g} scale) {} format {} x{} samples", surface.guest_va,
             surface.width, surface.height, surface.scale.Factor(), surface.depth ? "depth" : "color",
             u32(surface.format), u32(surface.samples));
  return true;
}

} // namespace

uint64_t SurfaceEpochLocked() { return g_surface_epoch; }  // sa2-nx (2026-10-20, sa2_renut_chain_reuse)
uint64_t FramebufferEpochLocked() { return g_framebuffer_epoch; }  // sa2-nx (2026-10-20, sa2_renut_chain_reuse)

RenderScale ResolutionScale() {
  return g_resolution_scale;
}

// ---------------------------------------------------------------------------------------------------------------------
// ffxiii2-nx (2026-11-07, the user: "Dynamik resolution is allowd"; the 30 fps work order at the console's default
// handheld clocks): dynamic resolution, after renut-nx's banjo_renut_dynamic_resolution (ChanseyIsTheBest/renut-nx,
// docs/enhancements-reference.md "Dynamic resolution": GPU average over about 16 frames against a budget, down to the
// level predicted to fit, up when the level above is predicted under 85 % of the budget for a while; its shaders read
// the heights from SharedConstants). NOT pixel-identical. Three fixed levels instead of renut-nx's six steps: 720 (code
// 0), the middle height (code 4) and the low one (code 6), each code's height set once for the session
// (render_scale.h: a code never changes meaning while targets or resolved textures hold it, so nothing made at one
// height is ever read as another). The 2026-11-07 console baseline (k245a, renut_gpu_profile): battle GPU 36.4 ms a
// frame, 31.5 ms of it in the 1280x720 targets' draws and resolves (the part that scales with the height).
REXCVAR_DEFINE_BOOL(banjo_renut_dynamic_resolution, false, "Banjo/Renut",
                    "deko3d: lower the 3D image's render height while the GPU is over its budget (720, the middle "
                    "height, the low one); NOT pixel-identical (the user's call, 2026-11-07)");
REXCVAR_DEFINE_INT32(banjo_renut_dynamic_resolution_handheld_mid, 612, "Banjo/Renut",
                     "Dynamic resolution: the middle render height (a multiple of 36, 360 to 684)");
REXCVAR_DEFINE_INT32(banjo_renut_dynamic_resolution_handheld_low, 540, "Banjo/Renut",
                     "Dynamic resolution: the lowest render height (a multiple of 36, 360 to 684, below the middle one)");
REXCVAR_DEFINE_DOUBLE(banjo_renut_dynamic_resolution_gpu_ms, 30.0, "Banjo/Renut",
                      "Dynamic resolution: the GPU budget a frame, in ms (30 fps with a two-blank swap is 33.3)");
REXCVAR_DEFINE_DOUBLE(banjo_renut_dynamic_resolution_up_s, 2.0, "Banjo/Renut",
                      "Dynamic resolution: how long the level above must be predicted under 85 % of the budget");
REXCVAR_DEFINE_DOUBLE(banjo_renut_dynamic_resolution_scaled_share, 0.85, "Banjo/Renut",
                      "Dynamic resolution: the share of the GPU time taken to scale with the pixels (the prediction)");
// FFXIII-2-Native-Switch P6 (2026-11-09): console battle run bt03_dyn (levels 720/612/540, budget 30 ms): after level
// changes in the middle of the opening battle the picture broke (rainbow-coloured characters, blocky backgrounds), the
// "broken battle picture" of 2026-11-08; forced to one level from the first frames (bt02_low, dy02_low) it stayed
// right. Until the change itself is fixed: one height for the whole session, taken at the first Swap, never changed.
REXCVAR_DEFINE_INT32(ffns_dynres_fixed_height, 0, "Banjo/Renut",
                     "Dynamic resolution: > 0 = this render height for the whole session (a multiple of 36, 360 to "
                     "684), set at the first frame and never changed (level changes during play broke the picture)");
REXCVAR_DEFINE_DOUBLE(banjo_renut_dynamic_resolution_test_s, 0.0, "Banjo/Renut",
                      "Dynamic resolution, a test: > 0 cycles 720, middle, low every this many seconds (A/B timing)");

namespace {

using DynClock = std::chrono::steady_clock;

constexpr uint32_t kDynLevels = 3;
constexpr uint32_t kDynCodes[kDynLevels] = {0, 4, 6};
constexpr uint32_t kDynWindow = 16;     // frames averaged
constexpr uint32_t kDynSettle = 4;      // GPU frames dropped after a change (frames in flight at the old height)
constexpr double kDynDownAfterS = 1.0;  // the least time between a change and a step down
constexpr double kUpShare = 0.85;

struct DynamicResolution {
  std::atomic<bool> active{false};      // enabled and the heights set (the first latch)
  bool fixed = false;                   // ffns_dynres_fixed_height: one level for the session, no controller
  std::atomic<uint32_t> level{0};       // what the next latch takes
  std::atomic<uint32_t> latched_level{0};
  uint32_t heights[kDynLevels] = {720, 0, 0};
  // The controller (OnGpuFrame, the render thread).
  std::mutex mutex;
  double samples[kDynWindow] = {};
  uint32_t count = 0, next = 0, settle = 0;
  DynClock::time_point changed{}, up_since{}, test_next{};
  bool up_ok = false;
  // Diagnostics.
  std::atomic<uint64_t> changes{0};
  std::atomic<uint64_t> frames_at[kDynLevels]{};
  double last_avg = 0.0;
  std::atomic<uint32_t> last_avg_us{0};
};
DynamicResolution g_dyn;

double PixelShare(uint32_t level) {
  const double r = double(g_dyn.heights[level]) / 720.0;
  return r * r;
}

// The GPU time predicted at 'to' from 'avg' measured at 'from'.
double Predict(double avg, uint32_t from, uint32_t to) {
  const double f = std::clamp(REXCVAR_GET(banjo_renut_dynamic_resolution_scaled_share), 0.0, 1.0);
  return avg * ((1.0 - f) + f * PixelShare(to) / PixelShare(from));
}

void SetLevel(uint32_t to, double avg, const char *why) {
  const uint32_t from = g_dyn.level.load(std::memory_order_relaxed);
  if (to == from)
    return;
  g_dyn.level.store(to, std::memory_order_relaxed);
  g_dyn.changed = DynClock::now();
  g_dyn.count = g_dyn.next = 0;
  g_dyn.settle = kDynSettle;
  g_dyn.up_ok = false;
  const uint64_t n = g_dyn.changes.fetch_add(1, std::memory_order_relaxed) + 1;
  if (n <= 200 || n % 100 == 0)
    RGPU_INFO("Dynamic resolution: {} -> {} lines ({}; GPU {:.1f} ms a frame, budget {:.1f}; change {})",
              g_dyn.heights[from], g_dyn.heights[to], why, avg,
              REXCVAR_GET(banjo_renut_dynamic_resolution_gpu_ms), n);
}

bool ValidHeight(int32_t h) {
  return h >= 360 && h <= 684 && h % 36 == 0;
}

// The first latch: the heights of codes 4 and 6, once for the session.
void StartDynamicResolutionLocked() {
  static bool tried = false;
  if (tried)
    return;
  tried = true;
  if (!REXCVAR_GET(banjo_renut_dynamic_resolution))
    return;
  if (!IsDeko3D()) {
    RGPU_WARN("banjo_renut_dynamic_resolution: deko3d only (the PC's SPIR-V shaders have codes 4 and 6 at 3x and 6x)");
    return;
  }
  const int32_t mid = REXCVAR_GET(banjo_renut_dynamic_resolution_handheld_mid);
  const int32_t low = REXCVAR_GET(banjo_renut_dynamic_resolution_handheld_low);
  if (!ValidHeight(mid) || !ValidHeight(low) || low >= mid) {
    RGPU_WARN("banjo_renut_dynamic_resolution: heights {} and {} refused (multiples of 36 from 360 to 684, the low "
              "one below the middle one); off", mid, low);
    return;
  }
  g_dyn.heights[1] = uint32_t(mid);
  g_dyn.heights[2] = uint32_t(low);
  const int32_t fixed = REXCVAR_GET(ffns_dynres_fixed_height);
  if (fixed > 0 && fixed != 720) {
    if (!ValidHeight(fixed)) {
      RGPU_WARN("ffns_dynres_fixed_height {} refused (a multiple of 36 from 360 to 684); the dynamic levels", fixed);
    } else {
      // The middle code stands for the fixed height; the controller never runs (DynamicResolutionGpuFrame).
      g_dyn.heights[1] = uint32_t(fixed);
      g_dyn.heights[2] = uint32_t(fixed > 360 ? fixed - 36 : fixed);  // unused, only kept below the middle one
      g_dyn.fixed = true;
      g_dyn.level.store(1, std::memory_order_relaxed);
    }
  }
  g_render_scale_dynamic_height[0] = g_dyn.heights[1];
  g_render_scale_dynamic_height[1] = g_dyn.heights[2];
  g_dyn.changed = g_dyn.test_next = DynClock::now();
  g_dyn.active.store(true, std::memory_order_release);
  RGPU_INFO("Dynamic resolution on: 720, {} ({}x{}) and {} ({}x{}) lines; GPU budget {:.1f} ms a frame, up after {:.1f} s",
            mid, RenderScale{4}.Size(GuestScreenWidth()), mid, low, RenderScale{6}.Size(GuestScreenWidth()), low,
            REXCVAR_GET(banjo_renut_dynamic_resolution_gpu_ms), REXCVAR_GET(banjo_renut_dynamic_resolution_up_s));
  if (g_dyn.fixed)
    RGPU_INFO("Dynamic resolution fixed at {} lines ({}x{}) for the session (ffns_dynres_fixed_height): no level changes",
              g_dyn.heights[1], RenderScale{4}.Size(GuestScreenWidth()), g_dyn.heights[1]);
}

} // namespace

void DynamicResolutionGpuFrame(double ms) {
  if (!g_dyn.active.load(std::memory_order_acquire))
    return;
  std::lock_guard lock(g_dyn.mutex);
  const auto now = DynClock::now();
  const uint32_t level = g_dyn.level.load(std::memory_order_relaxed);
  g_dyn.frames_at[g_dyn.latched_level.load(std::memory_order_relaxed)].fetch_add(1, std::memory_order_relaxed);
  if (g_dyn.fixed)
    return;  // ffns_dynres_fixed_height: no level changes
  const double test_s = REXCVAR_GET(banjo_renut_dynamic_resolution_test_s);
  if (test_s > 0.0) {
    if (now >= g_dyn.test_next) {
      g_dyn.test_next = now + std::chrono::duration_cast<DynClock::duration>(std::chrono::duration<double>(test_s));
      SetLevel((level + 1) % kDynLevels, ms, "test cycle");
    }
    return;
  }
  if (g_dyn.settle) {
    --g_dyn.settle;
    return;
  }
  g_dyn.samples[g_dyn.next] = ms;
  g_dyn.next = (g_dyn.next + 1) % kDynWindow;
  if (g_dyn.count < kDynWindow)
    ++g_dyn.count;
  if (g_dyn.count < kDynWindow)
    return;
  double sum = 0.0;
  for (double v : g_dyn.samples)
    sum += v;
  const double avg = sum / double(kDynWindow);
  g_dyn.last_avg_us.store(uint32_t(avg * 1000.0), std::memory_order_relaxed);
  const double budget = REXCVAR_GET(banjo_renut_dynamic_resolution_gpu_ms);
  const double since = std::chrono::duration<double>(now - g_dyn.changed).count();
  if (avg > budget) {
    g_dyn.up_ok = false;
    if (level + 1 < kDynLevels && since >= kDynDownAfterS) {
      uint32_t to = kDynLevels - 1;  // the lowest when none is predicted to fit
      for (uint32_t l = level + 1; l < kDynLevels; ++l) {
        if (Predict(avg, level, l) <= budget) {
          to = l;
          break;
        }
      }
      SetLevel(to, avg, "over the budget");
    }
    return;
  }
  if (level == 0)
    return;
  if (Predict(avg, level, level - 1) < kUpShare * budget) {
    if (!g_dyn.up_ok) {
      g_dyn.up_ok = true;
      g_dyn.up_since = now;
    } else if (std::chrono::duration<double>(now - g_dyn.up_since).count() >=
               REXCVAR_GET(banjo_renut_dynamic_resolution_up_s)) {
      SetLevel(level - 1, avg, "the level above fits");
    }
  } else {
    g_dyn.up_ok = false;
  }
}

DynamicResolutionInfo GetDynamicResolutionInfo() {
  DynamicResolutionInfo info;
  info.active = g_dyn.active.load(std::memory_order_acquire);
  if (!info.active)
    return info;
  info.height = g_dyn.heights[g_dyn.latched_level.load(std::memory_order_relaxed)];
  info.budget_ms = REXCVAR_GET(banjo_renut_dynamic_resolution_gpu_ms);
  info.gpu_avg_ms = double(g_dyn.last_avg_us.load(std::memory_order_relaxed)) / 1000.0;
  info.changes = g_dyn.changes.load(std::memory_order_relaxed);
  for (uint32_t l = 0; l < kDynLevels; ++l) {
    info.heights[l] = g_dyn.heights[l];
    info.frames_at[l] = g_dyn.frames_at[l].load(std::memory_order_relaxed);
  }
  return info;
}

void LatchResolutionScaleLocked() {
  LatchGraphicsOptionsLocked();
  const std::string resolution = REXCVAR_GET(renut_resolution);
  // Codes of RenderScale.
  RenderScale scale{resolution == "480p"    ? 1u
                    : resolution == "1080p" ? 2u
                    : resolution == "1440p" ? 3u
                    : resolution == "4K"    ? 4u
                                            : 0u};
  // peggle2-nx: Peggle 2's height for the console's mode, as the nearest of Renut's scales (720 -> 1x, 1080 -> 1.5x).
  // On deko3d too: Peggle 2's six shaders have neither the per-texture scale divisor nor the pixel-position scaling
  // that banjo-nx's scale1 / fragscale passes take out (their GLSL reads no scale code and no resolution_scale), and
  // plume binds a texture word's low 20 bits only (plume_deko3d_commands.cpp), so a resolve target's scale code
  // never reaches a handle.
  const bool by_mode = REXCVAR_GET(zumasrevenge_renut_resolution_by_mode);
  if (by_mode) {
    const float want = float(zumasrevenge::render::WantedScaleQuarters()) / 4.0f;
    uint32_t best = 0;
    for (uint32_t c = 0; c < RenderScale::kCount; ++c) {
      const float d = std::abs(RenderScale{c}.Factor() - want), db = std::abs(RenderScale{best}.Factor() - want);
      if (d < db - 1e-4f || (std::abs(d - db) <= 1e-4f && RenderScale{c}.Factor() < RenderScale{best}.Factor()))
        best = c;
    }
    scale = RenderScale{best};
  }
  // banjo-nx (2026-10-18): the deko3d renderer draws at the console's resolution only; its shaders are made for it
  // (tools/deko3d_glsl_opt.py, pass scale1: no per-texture scale divisor, resolution_scale 1).
  if (IsDeko3D() && scale.code != 0 && !by_mode) {
    static bool logged = false;
    if (!logged) {
      logged = true;
      RGPU_WARN("renut_resolution = \"{}\" is not taken on deko3d: it draws at the console's 1280x720", resolution);
    }
    scale = {};
  }
  // ffxiii2-nx (2026-11-07): dynamic resolution's level, taken here only (at Swap), so a frame never mixes heights.
  StartDynamicResolutionLocked();
  if (g_dyn.active.load(std::memory_order_acquire) && scale.code == 0) {
    const uint32_t level = g_dyn.level.load(std::memory_order_relaxed);
    scale = RenderScale{kDynCodes[level]};
    g_dyn.latched_level.store(level, std::memory_order_relaxed);
  }
  // FFXIII-2-Native-Switch (2026-11-12, ffns_ui_native): the frame's redirect to the UI target ends with it; the
  // frames without a composite are counted; the surface the frame resolved to the front buffer is the next one's.
  for (UiTarget *u : {&g_ui, &g_ui_depth}) {
    if (u->redirect) {
      u->redirect = false;
      ++g_surface_epoch;
    }
  }
  g_frames_without_composite = g_composite_seen ? 0 : std::min(g_frames_without_composite + 1, kMenuFrames);
  g_composite_seen = false;
  g_screen_source = g_frame_resolve_source;
  g_screen_source_generation = g_frame_resolve_generation;
  g_frame_resolve_source = nullptr;
  {
    const bool ui_native = IsDeko3D() && REXCVAR_GET(ffns_ui_native) && !FsrWanted() && scale != RenderScale{};
    if (ui_native != g_ui_native_frame) {
      g_ui_native_frame = ui_native;
      ++g_surface_epoch;
      RGPU_INFO("Text, HUD and menus at {} (ffns_ui_native)", ui_native ? "the display's 1280x720" : "the render height");
    }
  }
  if (!g_resolution_latched || scale != g_resolution_scale) {
    RGPU_INFO("Render resolution {}x{} ({:.3g}x)", scale.Size(GuestScreenWidth()), scale.Size(720),
              scale.Factor());
    g_resolution_scale = scale;
    ++g_surface_epoch;
    g_resolution_latched = true;
  }
}

void SetTilingExtentLocked(uint32_t width, uint32_t height) {
  if (g_tiling_width != width || g_tiling_height != height)
    ++g_surface_epoch;
  g_tiling_width = width;
  g_tiling_height = height;
}

// A surface stops using its target (destroyed, or its header now describes
// another view); the last user retires it.
void UnuseTarget(uint64_t key) {
  auto users = g_target_users.find(key);
  if (users == g_target_users.end() || --users->second)
    return;
  g_target_users.erase(users);
  ++g_surface_epoch;
  if (auto it = g_targets.find(key); it != g_targets.end()) {
    RetireTarget(*it->second);
    g_targets.erase(it);
  }
}

HostSurface *FindHostSurfaceLocked(u32 surface_va) {
  auto key = g_surface_keys.find(surface_va);
  if (key == g_surface_keys.end())
    return nullptr;
  auto it = g_targets.find(key->second);
  return it != g_targets.end() ? it->second.get() : nullptr;
}

void ColorAliasesLocked(const HostSurface *surface, std::vector<HostSurface *> &out) {
  out.clear();
  if (!surface || surface->depth)
    return;
  // TargetKey: the view in the low 32 bits, the host format above.
  constexpr uint64_t kViewMask = 0xFFFFFFFFull;
  uint64_t own = 0;
  bool found = false;
  for (const auto &[key, target] : g_targets) {
    if (target.get() == surface) {
      own = key;
      found = true;
      break;
    }
  }
  if (!found)
    return;
  for (const auto &[key, target] : g_targets) {
    if (target && target.get() != surface && target->texture && !target->depth &&
        (key & kViewMask) == (own & kViewMask))
      out.push_back(target.get());
  }
}

HostSurface *GetHostSurfaceSlow(u32 surface_va, const d3d::SurfaceDesc *desc);

namespace {
// The UI target in place of its scene, with the words of the surface bound now (as the lookup gives the scene).
HostSurface *UiAnswer(UiTarget &u, const HostSurface *scene) {
  HostSurface *ui = u.surface.get();
  ui->size_word = scene->size_word;
  ui->info_word = scene->info_word;
  ui->format_word = scene->format_word;
  return ui;
}
}  // namespace

HostSurface *GetHostSurfaceLocked(u32 surface_va) {
  if (!surface_va)
    return nullptr;
  // Targets made before the first Swap are of the configured scale too.
  if (!g_resolution_latched)
    LatchResolutionScaleLocked();
  const auto *desc = GuestPtr<const d3d::SurfaceDesc>(surface_va + sizeof(d3d::Resource));
  if (!desc)
    return nullptr;

  const bool memo = banjo::renut_gpu::CpuMemosOn();
  SurfaceMemo &m = g_surface_memo[(surface_va * 0x9E3779B1u) >> 26];
  if (memo && m.epoch == g_surface_epoch && m.va == surface_va && m.size == desc->Size &&
      m.info == desc->SurfaceInfo && m.format == desc->Format) {
    // the same as the lookup's: the surface bound now
    m.surface->size_word = desc->Size;
    m.surface->info_word = desc->SurfaceInfo;
    m.surface->format_word = desc->Format;
    // FFXIII-2-Native-Switch (2026-11-12, ffns_ui_native): the composite's target after the composite: its UI target.
    if (g_ui.redirect && m.surface == g_ui.scene)
      return UiAnswer(g_ui, m.surface);
    if (g_ui_depth.redirect && m.surface == g_ui_depth.scene)
      return UiAnswer(g_ui_depth, m.surface);
    return m.surface;
  }
  HostSurface *result = GetHostSurfaceSlow(surface_va, desc);
  if (memo && result) {
    m.va = surface_va;
    m.size = desc->Size;
    m.info = desc->SurfaceInfo;
    m.format = desc->Format;
    m.epoch = g_surface_epoch;
    m.surface = result;
  }
  if (g_ui.redirect && result && result == g_ui.scene)
    return UiAnswer(g_ui, result);  // FFXIII-2-Native-Switch (2026-11-12, ffns_ui_native)
  if (g_ui_depth.redirect && result && result == g_ui_depth.scene)
    return UiAnswer(g_ui_depth, result);
  return result;
}

// FFXIII-2-Native-Switch (2026-11-12, ffns_ui_native; renut-nx's): see surfaces.h.
RenderScale UiScale() { return RenderScale{}; }

bool UiNativeWanted() { return g_ui_native_frame; }

bool UiRedirectedLocked() { return g_ui.redirect; }

void NoteCompositeLocked() { g_composite_seen = true; }

void RedirectToUiSurfaceLocked() {
  if (g_ui.scene && g_ui.surface && g_ui.surface->texture && !g_ui.redirect) {
    g_ui.redirect = true;
    ++g_surface_epoch;
  }
}

void RedirectDepthToUiLocked() {
  if (g_ui_depth.scene && g_ui_depth.surface && g_ui_depth.surface->texture && !g_ui_depth.redirect) {
    g_ui_depth.redirect = true;
    ++g_surface_epoch;
  }
}

void NoteScreenResolveLocked(HostSurface *source) {
  if (!source || source->depth)
    return;
  if (g_ui.surface && source == g_ui.surface.get())
    source = g_ui.scene;  // the UI target stands for its scene
  if (!source || source->width < GuestScreenWidth() || source->height < 720 ||
      source->samples != plume::RenderSampleCount::COUNT_1)
    return;
  g_frame_resolve_source = source;
  g_frame_resolve_generation = source->generation;
}

bool UiMenuFramePendingLocked() {
  return g_ui_native_frame && !g_ui.redirect && g_frames_without_composite >= kMenuFrames && g_screen_source;
}

bool UiMenuFrameWantedLocked(const HostSurface *color) {
  return g_ui_native_frame && !g_ui.redirect && g_frames_without_composite >= kMenuFrames && color &&
         color == g_screen_source && color->generation == g_screen_source_generation && !color->depth &&
         color->texture && color->samples == plume::RenderSampleCount::COUNT_1 && color->scale != UiScale();
}

namespace {

// The UI target 'u' for 'scene' (colour or depth): kept while it fits, made again otherwise.
HostSurface *MakeUiTarget(UiTarget &u, HostSurface *scene) {
  if (!scene || !scene->texture)
    return nullptr;
  HostSurface *ui = u.surface.get();
  if (ui && ui->texture && u.scene == scene && u.scene_generation == scene->generation &&
      ui->scale == UiScale() && ui->width == scene->width && ui->height == scene->height &&
      ui->format == scene->format && ui->samples == scene->samples) {
    ui->size_word = scene->size_word;
    ui->info_word = scene->info_word;
    ui->format_word = scene->format_word;
    return ui;
  }
  if (!ui) {
    u.surface = std::make_unique<HostSurface>();
    ui = u.surface.get();
  } else if (ui->texture) {
    RetireTarget(*ui);
  }
  ui->guest_va = scene->guest_va;
  ui->size_word = scene->size_word;
  ui->info_word = scene->info_word;
  ui->format_word = scene->format_word;
  ui->width = scene->width;
  ui->height = scene->height;
  ui->scale = UiScale();
  ui->depth = scene->depth;
  ui->format = scene->format;
  ui->samples = scene->samples;
  ui->writes = 0;
  std::fill(std::begin(ui->profile_labels), std::end(ui->profile_labels), UINT32_MAX);
  const uint32_t host_width = ui->scale.Size(ui->width), host_height = ui->scale.Size(ui->height);
  plume::RenderDevice *device = Video::Device();
  const plume::RenderMultisampling multisampling(ui->samples);
  ui->texture = device->createTexture(
      ui->depth ? plume::RenderTextureDesc::DepthTarget(host_width, host_height, ui->format, multisampling)
                : plume::RenderTextureDesc::ColorTarget(host_width, host_height, ui->format, multisampling));
  ui->layout = plume::RenderTextureLayout::UNKNOWN;
  ui->generation = g_next_generation++;
  ++g_surface_epoch;
  if (!TextureCreated(ui->texture.get())) {
    ui->texture.reset();
    u.scene = nullptr;
    if (!ReportDeviceLost(device))
      RGPU_ERROR("UI {} target for {:08X}: {}x{} creation failed", ui->depth ? "depth" : "colour", scene->guest_va,
                 host_width, host_height);
    return nullptr;
  }
  ui->texture->setName(ui->depth ? "renut UI depth surface" : "renut UI surface");
  u.scene = scene;
  u.scene_generation = scene->generation;
  RGPU_INFO("UI {} target for {:08X}: {}x{} (the 3D image {}x{})", ui->depth ? "depth" : "colour", scene->guest_va,
            host_width, host_height, scene->scale.Size(scene->width), scene->scale.Size(scene->height));
  return ui;
}

}  // namespace

HostSurface *UiSurfaceLocked(HostSurface *scene) {
  return scene && !scene->depth ? MakeUiTarget(g_ui, scene) : nullptr;
}

HostSurface *UiDepthSurfaceLocked(HostSurface *scene_depth) {
  return scene_depth && scene_depth->depth ? MakeUiTarget(g_ui_depth, scene_depth) : nullptr;
}

HostSurface *GetHostSurfaceSlow(u32 surface_va, const d3d::SurfaceDesc *desc) {
  const bool depth = d3d::IsDepthFormat(desc->Format);
  plume::RenderFormat format = depth ? DepthTargetFormat(desc->Format) : SurfaceColorFormat(*desc);
  // The render resolution, or the finer one graphics_options gives it.
  const RenderScale scale = SurfaceRenderScale(*desc, g_resolution_scale);
  if (depth && banjo::renut_gpu::Depth24On()) {
    // banjo-nx (2026-10-19, banjo_deko3d_depth24): one view, one format (g_depth_view_formats).
    uint8_t &seen = g_depth_view_formats[TargetKey(*desc, true, plume::RenderFormat::UNKNOWN, scale)];
    const uint8_t bit = (desc->Format & 0x3F) == 23 ? 2 : 1;
    if ((seen | bit) == 3 && seen != 3) {
      static uint32_t logged = 0;
      if (logged++ < 8)
        RGPU_WARN("Depth view of surface {:08X} bound as both D24S8 and D24FS8: 32-bit float from now on "
                  "(banjo_deko3d_depth24)", surface_va);
    }
    seen |= bit;
    if (seen & 2)
      format = DepthTargetFormat();
  }
  const uint64_t key = TargetKey(*desc, depth, format, scale);
  auto [bound, first_use] = g_surface_keys.try_emplace(surface_va, key);
  if (first_use) {
    ++g_target_users[key];
    ++g_surface_epoch;
  } else if (bound->second != key) {
    ++g_surface_epoch;
    // The header was rewritten (or the VA reused without Destroy reaching
    // us): the surface is a view of other EDRAM now.
    UnuseTarget(bound->second);
    bound->second = key;
    ++g_target_users[key];
  }

  auto &slot = g_targets[key];
  // The view's target covers every surface over it, and every tile while
  // tiling (SetTilingExtentLocked).
  uint32_t min_width = std::max(g_tiling_width, d3d::SurfaceWidth(desc->Size));
  uint32_t min_height = std::max(g_tiling_height, d3d::SurfaceHeight(desc->Size));
  if (slot && slot->texture) {
    // The surface bound now: its guest size (Targets::guest_width) and D3D
    // format (D24S8 or D24FS8 for a depth resolve).
    slot->size_word = desc->Size;
    slot->info_word = desc->SurfaceInfo;
    slot->format_word = desc->Format;
    if (slot->width >= min_width && slot->height >= min_height && slot->scale == scale)
      return slot.get();
    // A larger surface over the view, or a larger tiling: grow it, keeping
    // the size the others gave it (what it held is lost). (Another scale is
    // another target: TargetKey.)
    min_width = std::max(min_width, slot->width);
    min_height = std::max(min_height, slot->height);
    RetireTarget(*slot);
  } else if (!slot) {
    slot = std::make_unique<HostSurface>();
    slot->guest_va = surface_va;
  }
  ++g_surface_epoch;
  if (!BuildTarget(*slot, *desc, min_width, min_height, scale))
    return nullptr;
  // No defined content yet; D3D12 wants a new target discarded (or cleared)
  // before anything else uses it, and a resolve may read it before any draw.
  if (plume::RenderCommandList *cmd = Video::CommandListLocked()) {
    TransitionSurfaceLocked(slot.get(), slot->depth ? plume::RenderTextureLayout::DEPTH_WRITE
                                                    : plume::RenderTextureLayout::COLOR_WRITE);
    cmd->discardTexture(slot->texture.get());
  }
  return slot.get();
}

bool GrowHostSurfaceLocked(HostSurface *surface, uint32_t width, uint32_t height) {
  if (!surface || !surface->texture || (surface->width >= width && surface->height >= height))
    return false;
  d3d::SurfaceDesc desc{};
  desc.SurfaceInfo = surface->info_word;
  desc.Size = surface->size_word;
  desc.Format = surface->format_word;
  const uint32_t w = std::max(width, surface->width), h = std::max(height, surface->height);
  const RenderScale scale = surface->scale;
  RetireTarget(*surface);
  ++g_surface_epoch;
  if (!BuildTarget(*surface, desc, w, h, scale))
    return false;
  if (plume::RenderCommandList *cmd = Video::CommandListLocked()) {
    TransitionSurfaceLocked(surface, surface->depth ? plume::RenderTextureLayout::DEPTH_WRITE
                                                    : plume::RenderTextureLayout::COLOR_WRITE);
    cmd->discardTexture(surface->texture.get());
  }
  return true;
}

HostSurface *SurfaceByGenerationLocked(uint64_t generation) {
  for (const auto &[key, surface] : g_targets)
    if (surface && surface->texture && surface->generation == generation)
      return surface.get();
  for (const UiTarget *u : {&g_ui, &g_ui_depth})  // ffns_ui_native: the UI targets are no view's
    if (u->surface && u->surface->texture && u->surface->generation == generation)
      return u->surface.get();
  return nullptr;
}

void SurfaceMemoryLocked(size_t &targets, uint64_t &bytes) {
  targets = 0;
  bytes = 0;
  for (const auto &[key, surface] : g_targets) {
    if (!surface || !surface->texture)
      continue;
    ++targets;
    const uint64_t texel = plume::RenderFormatSize(surface->format);
    bytes += uint64_t(surface->scale.Size(surface->width)) * surface->scale.Size(surface->height) *
             texel * surface->samples;
  }
  for (const UiTarget *u : {&g_ui, &g_ui_depth}) {  // ffns_ui_native
    if (const HostSurface *ui = u->surface.get(); ui && ui->texture) {
      ++targets;
      bytes += uint64_t(ui->scale.Size(ui->width)) * ui->scale.Size(ui->height) *
               plume::RenderFormatSize(ui->format) * ui->samples;
    }
  }
}

void ReleaseHostSurfaceLocked(u32 surface_va) {
  auto it = g_surface_keys.find(surface_va);
  if (it == g_surface_keys.end())
    return;
  const uint64_t key = it->second;
  g_surface_keys.erase(it);
  ++g_surface_epoch;
  UnuseTarget(key);
}

uint32_t SurfaceSampleSlotLocked(HostSurface *surface) {
  if (!surface || !surface->texture)
    return kInvalidDescriptor;
  if (surface->sample_slot != UINT32_MAX)
    return surface->sample_slot;
  // The depth format views the depth plane.
  surface->sample_view = surface->texture->createTextureView(
      plume::RenderTextureViewDesc::Texture2D(surface->format));
  if (!surface->sample_view)
    return kInvalidDescriptor;
  const uint32_t slot = Video::AllocateTextureSlotLocked();
  if (slot == kInvalidDescriptor)
    return kInvalidDescriptor;
  Video::SetTextureSlotLocked(slot, surface->texture.get(), surface->sample_view.get());
  surface->sample_slot = slot;
  return slot;
}

// banjo-nx (2026-10-18): the bindless slot of a depth/stencil target's stencil view (read as uint by the depth
// resolve), or kInvalidDescriptor (no stencil, D3D12, or no view). Tried once a surface.
uint32_t SurfaceStencilSlotLocked(HostSurface *surface) {
  if (!surface || !surface->texture || !UsesVulkanBindings() || !plume::RenderFormatIsStencil(surface->format))
    return kInvalidDescriptor;
  if (surface->stencil_slot != UINT32_MAX)
    return surface->stencil_slot;
  surface->stencil_slot = kInvalidDescriptor;
  surface->stencil_view = surface->texture->createTextureView(
      plume::RenderTextureViewDesc::Texture2D(plume::RenderFormat::S8_UINT));
  if (!surface->stencil_view)
    return kInvalidDescriptor;
  const uint32_t slot = Video::AllocateTextureSlotLocked();
  if (slot == kInvalidDescriptor)
    return kInvalidDescriptor;
  Video::SetTextureSlotLocked(slot, surface->texture.get(), surface->stencil_view.get());
  surface->stencil_slot = slot;
  return slot;
}

void TransitionSurfaceLocked(HostSurface *surface, plume::RenderTextureLayout layout) {
  if (!surface || !surface->texture || surface->layout == layout)
    return;
  // banjo-nx (2026-10-19, banjo_deko3d_barrier_lean): from one read layout to another: no barrier (textures.cpp).
  if (IsDeko3D() && banjo::renut_gpu::Deko3DBarrierLeanOn() && ReadOnlyLayout(surface->layout) &&
      ReadOnlyLayout(layout)) {
    surface->layout = layout;
    return;
  }
  CountTransitionLocked(1, surface->layout, layout);  // peggle2-nx: zumasrevenge_renut_diag_transitions
  plume::RenderCommandList *cmd = Video::CommandListLocked();
  if (!cmd)
    return;
  cmd->barriers(layout == plume::RenderTextureLayout::COPY_SOURCE ||
                        layout == plume::RenderTextureLayout::COPY_DEST ||
                        layout == plume::RenderTextureLayout::RESOLVE_SOURCE ||
                        layout == plume::RenderTextureLayout::RESOLVE_DEST
                    ? plume::RenderBarrierStage::COPY
                    : plume::RenderBarrierStage::GRAPHICS,
                plume::RenderTextureBarrier(surface->texture.get(), layout));
  surface->layout = layout;
}

void SwapDepthImageLocked(HostSurface &surface, DepthImage &image) {
  ++g_surface_epoch;
  std::swap(surface.texture, image.texture);
  std::swap(surface.layout, image.layout);
  std::swap(surface.generation, image.generation);
  std::swap(surface.sample_view, image.sample_view);
  std::swap(surface.sample_slot, image.sample_slot);
  std::swap(surface.stencil_view, image.stencil_view);
  std::swap(surface.stencil_slot, image.stencil_slot);
  std::swap(surface.handover_views, image.views);
  image.host_width = surface.scale.Size(surface.width);
  image.host_height = surface.scale.Size(surface.height);
  image.format = surface.format;
  image.samples = surface.samples;
  image.format_word = surface.format_word;
}

bool MakeDepthImageLocked(const HostSurface &surface, DepthImage &image) {
  plume::RenderDevice *device = Video::Device();
  const uint32_t w = surface.scale.Size(surface.width), h = surface.scale.Size(surface.height);
  image.texture = device->createTexture(
      plume::RenderTextureDesc::DepthTarget(w, h, surface.format, plume::RenderMultisampling(surface.samples)));
  if (!TextureCreated(image.texture.get())) {
    image.texture.reset();
    ReportDeviceLost(device);
    return false;
  }
  image.texture->setName("renut depth surface (hand-over)");
  image.layout = plume::RenderTextureLayout::UNKNOWN;
  image.generation = g_next_generation++;
  image.host_width = w;
  image.host_height = h;
  image.format = surface.format;
  image.samples = surface.samples;
  image.format_word = surface.format_word;
  return true;
}

void RetireDepthImageLocked(DepthImage &image) {
  // The framebuffers a surface made over this image (cached by its generation).
  for (size_t i = 0; i < g_framebuffers.size();) {
    if (g_framebuffers[i].key.depth && g_framebuffers[i].key.depth_generation == image.generation) {
      Video::RetireLocked(std::move(g_framebuffers[i].framebuffer));
      g_framebuffers[i] = std::move(g_framebuffers.back());
      g_framebuffers.pop_back();
      ++g_framebuffer_epoch;
    } else {
      ++i;
    }
  }
  FreeDepthViews(image.views);
  if (image.sample_slot != UINT32_MAX && image.sample_slot != kInvalidDescriptor)
    Video::FreeTextureSlotLocked(image.sample_slot);
  image.sample_slot = UINT32_MAX;
  Video::RetireLocked(std::move(image.sample_view));
  if (image.stencil_slot != UINT32_MAX && image.stencil_slot != kInvalidDescriptor)
    Video::FreeTextureSlotLocked(image.stencil_slot);
  image.stencil_slot = UINT32_MAX;
  Video::RetireLocked(std::move(image.stencil_view));
  Video::RetireLocked(std::move(image.texture));
}

void TransitionDepthImageLocked(DepthImage &image, plume::RenderTextureLayout layout) {
  if (!image.texture || image.layout == layout)
    return;
  if (IsDeko3D() && banjo::renut_gpu::Deko3DBarrierLeanOn() && ReadOnlyLayout(image.layout) && ReadOnlyLayout(layout)) {
    image.layout = layout;
    return;
  }
  CountTransitionLocked(2, image.layout, layout);  // peggle2-nx: zumasrevenge_renut_diag_transitions
  plume::RenderCommandList *cmd = Video::CommandListLocked();
  if (!cmd)
    return;
  cmd->barriers(plume::RenderBarrierStage::GRAPHICS, plume::RenderTextureBarrier(image.texture.get(), layout));
  image.layout = layout;
}

uint32_t DepthImageViewSlotLocked(plume::RenderTexture *texture, plume::RenderFormat format,
                                  std::vector<DepthImage::View> &views, const plume::RenderComponentMapping &mapping) {
  if (!texture)
    return kInvalidDescriptor;
  for (const DepthImage::View &v : views) {
    if (v.mapping.r == mapping.r && v.mapping.g == mapping.g && v.mapping.b == mapping.b && v.mapping.a == mapping.a)
      return v.slot;
  }
  // The depth format views the depth plane (deko3d samples it in the red channel), with the destination's mapping.
  plume::RenderTextureViewDesc desc = plume::RenderTextureViewDesc::Texture2D(format);
  desc.componentMapping = mapping;
  DepthImage::View v;
  v.mapping = mapping;
  v.view = texture->createTextureView(desc);
  if (!v.view)
    return kInvalidDescriptor;
  v.slot = Video::AllocateTextureSlotLocked();
  if (v.slot == kInvalidDescriptor) {
    Video::RetireLocked(std::move(v.view));
    return kInvalidDescriptor;
  }
  Video::SetTextureSlotLocked(v.slot, texture, v.view.get());
  views.push_back(std::move(v));
  return views.back().slot;
}

plume::RenderFramebuffer *GetFramebufferLocked(HostSurface *const *colors, uint32_t color_count,
                                               HostSurface *depth) {
  FramebufferKey key;
  for (uint32_t i = 0; i < color_count && i < d3d::kMaxRenderTargets; ++i) {
    key.colors[i] = colors[i];
    key.color_generations[i] = colors[i] ? colors[i]->generation : 0;
  }
  key.depth = depth;
  key.depth_generation = depth ? depth->generation : 0;
  // banjo-nx (2026-10-19, render-thread proposal 8): the recent ones first, the found one moved to the front.
  for (uint32_t i = 0; i < 4; ++i) {
    RecentFramebuffer &r = g_recent_framebuffers[i];
    if (r.epoch == g_framebuffer_epoch && r.framebuffer && r.key == key) {
      plume::RenderFramebuffer *found = r.framebuffer;
      if (i) {
        const RecentFramebuffer hit = r;
        for (uint32_t j = i; j > 0; --j)
          g_recent_framebuffers[j] = g_recent_framebuffers[j - 1];
        g_recent_framebuffers[0] = hit;
      }
      return found;
    }
  }
  auto remember = [&key](plume::RenderFramebuffer *framebuffer) {
    for (uint32_t j = 3; j > 0; --j)
      g_recent_framebuffers[j] = g_recent_framebuffers[j - 1];
    g_recent_framebuffers[0] = {key, framebuffer, g_framebuffer_epoch};
    return framebuffer;
  };
  for (auto &cached : g_framebuffers) {
    if (cached.key == key)
      return remember(cached.framebuffer.get());
  }

  const plume::RenderTexture *attachments[d3d::kMaxRenderTargets] = {};
  uint32_t count = 0;
  for (uint32_t i = 0; i < color_count && i < d3d::kMaxRenderTargets; ++i) {
    if (!colors[i] || !colors[i]->texture)
      break;  // host framebuffers need contiguous color attachments
    attachments[count++] = colors[i]->texture.get();
  }
  // ffxiii2-nx (2026-11-06, the right tile of the scene with its depth bound): framebuffers whose attachments differ.
  if (depth && depth->texture && count &&
      (depth->width != colors[0]->width || depth->height != colors[0]->height ||
       depth->samples != colors[0]->samples)) {
    static uint32_t logged = 0;
    if (logged++ < 24)
      RGPU_INFO("[fbsize] colour {:08X} {}x{} x{} with depth {:08X} {}x{} x{} (info {:08X} size {:08X})",
                colors[0]->guest_va, colors[0]->width, colors[0]->height, u32(colors[0]->samples), depth->guest_va,
                depth->width, depth->height, u32(depth->samples), depth->info_word, depth->size_word);
  }
  plume::RenderFramebufferDesc desc(attachments, count,
                                    depth && depth->texture ? depth->texture.get() : nullptr);
  auto framebuffer = Video::Device()->createFramebuffer(desc);
  if (!framebuffer) {
    RGPU_ERROR("Framebuffer creation failed ({} color targets, depth {})", count, depth != nullptr);
    return nullptr;
  }
  g_framebuffers.push_back({key, std::move(framebuffer)});
  return remember(g_framebuffers.back().framebuffer.get());
}

} // namespace renut::gpu
