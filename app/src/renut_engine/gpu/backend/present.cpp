/**
 * @file    gpu/backend/present.cpp
 * @brief   End of frame: swapchain acquire, guest image scan-out, overlay,
 *          submit and present.
 */
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>

#include <filesystem>
#include <plume_render_interface.h>

#include <rex/cvar.h>

#include "renut_engine/gpu/backend/backend.h"
#include "renut_engine/gpu/backend/gpu_profile.h"
#include "renut_engine/gpu/render/draw.h"
#include "renut_engine/gpu/render/textures.h"
#include "renut_engine/gpu/log.h"
#include "renut_engine/gpu/overlay/overlay_ui.h"
#include "renut_engine/gpu/overlay/overlay_drawer.h"  // banjo-nx (2026-10-19): HasLatest
#include "renut_engine/gpu/banjo_renut_tuning.h"
#include "renut_engine/gpu/backend/render_thread.h"
#include "renut_engine/gpu/backend/video.h"
#include "renut_engine/gpu/render/fsr.h"  // FFXIII-2-Native-Switch (2026-11-10): ffns_fsr
#include "renut_engine/gpu/backend/video_internal.h"
#include "renut_engine/gpu/banjo_renut_capture.h"
#if defined(PLUME_DEKO3D)
#include <plume_deko3d.h>  // banjo-nx: banjo_renut_capture_raw's format check
#endif

// banjo-nx (2026-10-17, deko3d renderer tests): the captures copy the game's frame texture as it is (4-byte formats only,
// its channels as BGRA), without the scan-out pass: which of the two a wrong capture comes from.
REXCVAR_DEFINE_BOOL(banjo_renut_capture_raw, false, "Banjo/Renut",
                    "Test: frame captures copy the game's frame texture itself instead of the scanned-out picture "
                    "(deko3d builds, 4-byte formats)");

// banjo-nx (2026-10-18, the deko3d depth of field): one of the game's textures on screen and in the captures in place of
// the frame, to compare a pass's inputs between the back ends (the [trace] resolve lines name the headers).
REXCVAR_DEFINE_STRING(banjo_renut_diag_show_texture, "", "Banjo/Renut",
                      "Diagnostic: show the host texture of this guest texture header (hex address, as the [trace] "
                      "resolve lines name them) instead of the frame, on screen and in captures");
REXCVAR_DEFINE_DOUBLE(banjo_renut_diag_show_gamma, 1.0, "Banjo/Renut",
                      "Diagnostic: banjo_renut_diag_show_texture's exponent (depth near 1 shows with 64 or so)");

REXCVAR_DEFINE_BOOL(zumasrevenge_renut_display_ramp, true, "ZumasRevenge",
                    "Renut's renderer: apply the display gamma ramp the game sets (as the console's scan-out and the "
                    "native renderer do), as the power curve closest to it; false = the frame as drawn");

REXCVAR_DEFINE_BOOL(zumasrevenge_renut_display_ramp_table, true, "ZumasRevenge",
                    "Renut's renderer: a table ramp (not piecewise linear) is applied as the table itself, exactly as "
                    "the console's scan-out; false = the closest power curve");

// ffxiii2-nx (2026-11-06, the smoke band D-003): a diagnostic stamp so a Frame Tap recording's frames can be matched
// to the renderer's per-frame logs ([rtlog], [drawlog]): the frame number (the one those logs print) as 20 bits along
// the bottom-left edge of the output (8x6 px squares, white = 1, black = 0, between a white and a black marker). It
// changes the picture: diagnostic sessions only, off by default.
REXCVAR_DEFINE_BOOL(ffxiii2_diag_frame_stamp, false, "FFXIII2/Diag",
                    "Diagnostic (changes the picture): stamp the renderer's frame number in the bottom-left corner of "
                    "the output, to match Frame Tap recordings to [rtlog] / [drawlog] lines");

REXCVAR_DEFINE_BOOL(renut_vsync, true, "Nuts&Bolts/Graphics",
                    "Show each frame at the display's refresh: no tearing, and the game runs at "
                    "the refresh rate (frame_cap then only limits below it). Off shows frames at "
                    "once, with tearing.");

// ffxiii2-nx (2026-11-06, D-003 on the PC): RenderDoc captures of single frames from Renut's present (src/me/banjo_renderdoc.cpp).
namespace banjo::renderdoc {
bool Start(const char *path_template);
bool End();
} // namespace banjo::renderdoc

REXCVAR_DEFINE_INT32(ffxiii2_diag_rdc_start_s, 0, "FFXIII2/Diag",
                     "Diagnostic (PC with RenderDoc's layer): seconds after the first present to start RenderDoc "
                     "captures of single frames into capturas/rdc/; 0 = off");
REXCVAR_DEFINE_INT32(ffxiii2_diag_rdc_count, 40, "FFXIII2/Diag", "Diagnostic: number of RenderDoc captures");
REXCVAR_DEFINE_INT32(ffxiii2_diag_rdc_every, 15, "FFXIII2/Diag",
                     "Diagnostic: frames between the RenderDoc captures");

// ffxiii2-nx (2026-11-06, D-003 on the PC): the band trap. Every presented frame is read back (a GPU sync a frame: PC
// diagnostic only) and tested for the band edge as tools/visual_debug/band_scan.py does (rows split-8..split+8, columns
// 40..1240 every 8, a fall of more than 10 grey levels from 2 rows above to 2 rows below in >= 37 % of them). A banded
// frame is logged ([band]) and written with the frame before it as PPM to capturas/band/. With ffxiii2_diag_band_rdc
// (and RenderDoc's layer) every frame is captured and the capture is kept only when the frame is banded (the others
// are discarded): one capture of exactly the frame that showed the band.
REXCVAR_DEFINE_BOOL(ffxiii2_diag_band_trap, false, "FFXIII2/Diag",
                    "Diagnostic (PC, slow): test every presented frame for the D-003 band and save banded frames");
REXCVAR_DEFINE_INT32(ffxiii2_diag_band_start_s, 0, "FFXIII2/Diag",
                     "Diagnostic: seconds after the first present before the band trap starts");
REXCVAR_DEFINE_BOOL(ffxiii2_diag_band_rdc, false, "FFXIII2/Diag",
                    "Diagnostic (PC with RenderDoc's layer): keep a RenderDoc capture of each banded frame");
REXCVAR_DEFINE_INT32(ffxiii2_diag_band_max, 12, "FFXIII2/Diag",
                     "Diagnostic: the most banded frames the trap saves (and captures)");
REXCVAR_DEFINE_INT32(ffxiii2_diag_band_split, 336, "FFXIII2/Diag", "Diagnostic: the band edge's row (720 rows)");
// band_trap1 (k219pc): at 0.37 within 8 rows the trap took HUD edges (the tutorial box, the HP gauge: 0.37-0.42); the
// user's banded frames measure 0.50-0.54 at rows 334-337.
REXCVAR_DEFINE_DOUBLE(ffxiii2_diag_band_share, 0.46, "FFXIII2/Diag",
                      "Diagnostic: the band trap's edge share (columns with the step) for a banded frame");
// band_trap1: 11,400 frames tested with a GPU sync each (CaptureFrameLocked waits for the GPU), no band. Every PC
// capture so far (PNG bursts, RenderDoc) syncs the frame it takes, which would hide a GPU ordering fault; the user sees
// the band without any capture. Async: each frame's scan-out is copied into one of a ring of read-back buffers in the
// frame's own command list (no wait) and tested 4 presents later, when the GPU is done with it.
REXCVAR_DEFINE_BOOL(ffxiii2_diag_band_async, true, "FFXIII2/Diag",
                    "Diagnostic: the band trap reads frames back without waiting for the GPU (false = a sync each)");
// band_trap2: the edge share alone takes scene edges at the row (a horizon at y 336 in the cutscene after the battle).
// Every tested frame as a grey PGM (capturas/band/all/), for metrics offline.
REXCVAR_DEFINE_INT32(ffxiii2_diag_band_dump_all, 0, "FFXIII2/Diag",
                     "Diagnostic: write the first N tested frames as grey PGM to capturas/band/all/ (0 = none)");

#if defined(_WIN32)
#define FFXIII2_BAND_RDC 1
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// RenderDoc 1.47's in-application API header (MIT, Baldur Karlsson): the SDK's copy stops at API 1.0.1, which has no
// DiscardFrameCapture.
#include "renut_engine/gpu/backend/renderdoc_app_147.h"
#include <cstdio>
#include <vector>
namespace {
RENDERDOC_API_1_4_1 *BandRdcApi() {
  static RENDERDOC_API_1_4_1 *api = [] {
    HMODULE module = GetModuleHandleA("renderdoc.dll");
    if (!module)
      return static_cast<RENDERDOC_API_1_4_1 *>(nullptr);
    auto get_api = reinterpret_cast<pRENDERDOC_GetAPI>(GetProcAddress(module, "RENDERDOC_GetAPI"));
    RENDERDOC_API_1_4_1 *a = nullptr;
    if (!get_api || !get_api(eRENDERDOC_API_Version_1_4_1, reinterpret_cast<void **>(&a)))
      return static_cast<RENDERDOC_API_1_4_1 *>(nullptr);
    return a;
  }();
  return api;
}
} // namespace
#else
#include <cstdio>
#include <vector>
#endif

namespace renut::gpu {

namespace {

// renut_vsync onto the swapchain (both backends start with it on). Vulkan
// makes the change by recreating the swapchain, which needsResize asks for.
void ApplyVsync(VideoState &s) {
  static bool applied = true;
  const bool vsync = REXCVAR_GET(renut_vsync);
  if (vsync == applied)
    return;
  s.swap_chain->setVsyncEnabled(vsync);
  applied = vsync;
  RGPU_INFO("Vsync {}", vsync ? "on" : "off");
}

void RebuildSwapChain(VideoState &s) {
  // Submit rather than abandon an open list: plume tracks layouts on record.
  if (s.list_open)
    detail::SubmitAndAdvance(s, nullptr, nullptr);
  detail::WaitAllSubmitted(s);
  s.swap_framebuffers.clear();
  if (!s.swap_chain->resize() || !detail::BuildSwapChainTargets(s)) {
    if (s.swap_chain->getWidth() && s.swap_chain->getHeight())
      RGPU_ERROR("Swapchain resize failed");  // 0x0 while minimized is expected
  }
}

// The most of the game's frame trimmed off either axis to fill the output.
constexpr float kMaxTrim = 0.15f;

struct FrameFit {
  plume::RenderViewport viewport;
  float visible[2] = {1.0f, 1.0f};  // the share of the frame shown on each axis
};

// The frame fills the output (a window that is not the frame's shape, say a
// maximized one) without bars: it covers it, keeping its proportions, and the
// part that overflows is trimmed evenly from both edges. Past kMaxTrim, the
// rest is fitted with bars.
FrameFit FitFrame(uint32_t out_w, uint32_t out_h, uint32_t src_w, uint32_t src_h) {
  FrameFit fit;
  fit.viewport = plume::RenderViewport(0.0f, 0.0f, float(out_w), float(out_h));
  if (src_w == 0 || src_h == 0 || out_w == 0 || out_h == 0)
    return fit;
  const float out_aspect = float(out_w) / float(out_h);
  float src_aspect = float(src_w) / float(src_h);
  if (src_aspect > out_aspect) {
    fit.visible[0] = std::max(out_aspect / src_aspect, 1.0f - kMaxTrim);
    src_aspect *= fit.visible[0];
  } else {
    fit.visible[1] = std::max(src_aspect / out_aspect, 1.0f - kMaxTrim);
    src_aspect /= fit.visible[1];
  }
  float w = float(out_w), h = float(out_h);
  if (src_aspect > out_aspect)
    h = w / src_aspect;
  else
    w = h * src_aspect;
  fit.viewport = plume::RenderViewport((float(out_w) - w) * 0.5f, (float(out_h) - h) * 0.5f, w, h);
  return fit;
}

// peggle2-nx (2026-10-20): the exponent of the game's display ramp (NoteDisplayRamp); 1 until the game sets one.
std::atomic<float> g_display_ramp_exponent{1.0f};

// bejeweled3-nx (2026-10-22): the table itself. The power curve left Renut's frames 2 levels of 255 from the emulated
// GPU's on average (Bejeweled 3's ramp: exponent 1.172, worst 6 of 255). NoteDisplayRamp fills the table (any thread);
// the render thread uploads it as a 256x3 RGBA8 texture (each 10-bit value as r = high 8 bits, g = low 2 bits) and
// present_ps looks the channels up in it.
struct DisplayRampTable {
  std::mutex mutex;
  uint16_t values[3][256] = {};  // 10-bit outputs per channel
  uint64_t generation = 0;       // 0 = none (the power curve applies)
};
DisplayRampTable g_display_ramp_table;
struct DisplayRampTexture {
  std::unique_ptr<plume::RenderTexture> texture;
  std::unique_ptr<plume::RenderTextureView> view;
  uint32_t slot = kInvalidDescriptor;
  uint64_t generation = 0;
};
DisplayRampTexture g_display_ramp_texture;  // render thread

void UpdateDisplayRampLocked() {
  uint8_t rgba[3 * 256 * 4];
  {
    std::lock_guard<std::mutex> lock(g_display_ramp_table.mutex);
    if (g_display_ramp_table.generation == g_display_ramp_texture.generation)
      return;
    g_display_ramp_texture.generation = g_display_ramp_table.generation;
    for (int c = 0; c < 3; ++c) {
      for (int i = 0; i < 256; ++i) {
        uint8_t *texel = rgba + (c * 256 + i) * 4;
        const uint16_t v = g_display_ramp_table.values[c][i];
        texel[0] = uint8_t(v >> 2);
        texel[1] = uint8_t(v & 3);
        texel[2] = 0;
        texel[3] = 0xFF;
      }
    }
  }
  if (!Video::EnsureCommandListLocked())
    return;
  DisplayRampTexture &t = g_display_ramp_texture;
  if (!t.texture) {
    plume::RenderDevice *device = Video::Device();
    t.texture = device->createTexture(plume::RenderTextureDesc::Texture2D(256, 3, 1, plume::RenderFormat::R8G8B8A8_UNORM));
    if (!t.texture)
      return;
    t.texture->setName("renut display ramp");
    t.view = t.texture->createTextureView(plume::RenderTextureViewDesc::Texture2D(plume::RenderFormat::R8G8B8A8_UNORM));
    t.slot = t.view ? Video::AllocateTextureSlotLocked() : kInvalidDescriptor;
    if (t.slot == kInvalidDescriptor) {
      t.texture.reset();
      t.view.reset();
      return;
    }
    Video::SetTextureSlotLocked(t.slot, t.texture.get(), t.view.get());
  }
  const bool uploaded = Video::UploadRGBA8Locked(t.texture.get(), 256, 3, rgba);
  if (!uploaded)
    t.generation = 0;  // try again next frame
  static int logged = 0;
  if (logged++ < 3)
    RGPU_INFO("Display ramp table: slot {}, upload {}, generation {}", t.slot, uploaded ? "recorded" : "failed",
              t.generation);
}

// What present_ps gets for the table: 1 + its slot (sent negated in param0), 0 = the power curve.
float DisplayRampTableParam() {
  const DisplayRampTexture &t = g_display_ramp_texture;
  return t.generation && t.slot != kInvalidDescriptor ? float(t.slot) + 1.0f : 0.0f;
}

// FFXIII-2-Native-Switch (2026-11-10): the scan-out's param0 / param1 (present_ps.hlsl), as RecordScanOut sets them,
// for FSR's RCAS pass (render/fsr.cpp), which draws the scan-out in its place.
void ScanOutParams(VideoState &s, const PresentImage &image, float &param0, float &param1) {
  param0 = image.gamma > 0.0f ? image.gamma : s.output_gamma * g_display_ramp_exponent.load(std::memory_order_relaxed);
  param1 = 0.0f;
  if (image.gamma <= 0.0f && s.output_gamma == 1.0f) {
    if (const float table = DisplayRampTableParam(); table > 0.0f) {
      const uint32_t slot = uint32_t(table) - 1;
      param0 = -1.0f;
      std::memcpy(&param1, &slot, sizeof(slot));
    }
  }
}

void RecordScanOut(VideoState &s, const PresentImage &image, uint32_t out_w, uint32_t out_h) {
  plume::RenderCommandList *cmd = s.command_list;
  if (image.layout && *image.layout != plume::RenderTextureLayout::SHADER_READ) {
    cmd->barriers(plume::RenderBarrierStage::GRAPHICS,
                  plume::RenderTextureBarrier(image.texture, plume::RenderTextureLayout::SHADER_READ));
    *image.layout = plume::RenderTextureLayout::SHADER_READ;
  }

  const uint32_t src_w = image.source_width ? image.source_width : image.width;
  const uint32_t src_h = image.source_height ? image.source_height : image.height;
  const FrameFit fit = FitFrame(out_w, out_h, src_w, src_h);
  const plume::RenderViewport &vp = fit.viewport;
  cmd->setViewports(vp);
  cmd->setScissors(plume::RenderRect(int32_t(vp.x), int32_t(vp.y), int32_t(vp.x + vp.width),
                                     int32_t(vp.y + vp.height)));

  cmd->setGraphicsPipelineLayout(s.utility_layout.get());
  cmd->setPipeline(s.present_pipeline.get());
  cmd->setGraphicsDescriptorSet(s.texture_set.get(), 0);
  cmd->setGraphicsDescriptorSet(s.sampler_set.get(), 1);

  UtilityPush push{};
  // The frame's share of the texture, less what the fit trims from its edges.
  const float frame_u = image.width ? float(src_w) / float(image.width) : 1.0f;
  const float frame_v = image.height ? float(src_h) / float(image.height) : 1.0f;
  push.scale[0] = frame_u * fit.visible[0];
  push.scale[1] = frame_v * fit.visible[1];
  push.translate[0] = frame_u * (1.0f - fit.visible[0]) * 0.5f;
  push.translate[1] = frame_v * (1.0f - fit.visible[1]) * 0.5f;
  push.texture_index = image.descriptor;
  push.sampler_index = kLinearClampSamplerSlot;
  push.param0 = image.gamma > 0.0f ? image.gamma
                                   : s.output_gamma * g_display_ramp_exponent.load(std::memory_order_relaxed);
  // bejeweled3-nx: the exact table instead, for the game's frame (not a diagnostic view with its own gamma): param0 -1
  // selects it, param1 holds its slot's bits (present_ps).
  if (image.gamma <= 0.0f && s.output_gamma == 1.0f) {
    if (const float table = DisplayRampTableParam(); table > 0.0f) {
      const uint32_t slot = uint32_t(table) - 1;
      push.param0 = -1.0f;
      std::memcpy(&push.param1, &slot, sizeof(slot));  // the slot's bits (present_ps: asuint)
    }
  }
  cmd->setGraphicsPushConstants(0, &push, 0, sizeof(push));
  cmd->drawInstanced(3, 1, 0, 0);
}

// banjo-nx: the test captures (banjo_renut_capture.h). CaptureFrame asks; the next Present, minimized or not, scans
// the guest image out once more into an offscreen target of the frame's size and reads it back.
struct FrameCapture {
  std::mutex mutex;
  std::condition_variable cv;
  std::atomic<uint64_t> requested{0};
  uint64_t served = 0;  // under mutex
  bool ok = false;
  rex::ui::RawImage image;
  // Render thread only. Kept between captures of one size (the recording takes one per frame); never in flight
  // between them, as each capture waits for the GPU.
  std::unique_ptr<plume::RenderTexture> target;
  std::unique_ptr<plume::RenderFramebuffer> framebuffer;
  std::unique_ptr<plume::RenderBuffer> readback;
  uint32_t width = 0, height = 0, row_texels = 0;
  uint64_t served_render = 0;
};
FrameCapture g_capture;

// The present pipeline's format (kSwapChainFormat in video.cpp).
constexpr plume::RenderFormat kCaptureFormat = plume::RenderFormat::B8G8R8A8_UNORM;

bool CaptureFrameLocked(VideoState &s, const PresentImage &image, rex::ui::RawImage &out) {
  const uint32_t w = image.source_width ? image.source_width : image.width;
  const uint32_t h = image.source_height ? image.source_height : image.height;
  if (!w || !h || !Video::EnsureCommandListLocked())
    return false;
  FrameCapture &c = g_capture;
  if (!c.target || c.width != w || c.height != h) {
    c.framebuffer.reset();
    c.target.reset();
    c.readback.reset();
    plume::RenderTextureDesc desc = plume::RenderTextureDesc::Texture2D(w, h, 1, kCaptureFormat);
    desc.flags |= plume::RenderTextureFlag::RENDER_TARGET;
    c.target = s.device->createTexture(desc);
    if (!c.target)
      return false;
    const plume::RenderTexture *attachment = c.target.get();
    c.framebuffer = s.device->createFramebuffer(plume::RenderFramebufferDesc(&attachment, 1));
    c.row_texels = (w + 63) & ~63u;  // D3D12 copies rows of 256-byte multiples
    c.readback = s.device->createBuffer(plume::RenderBufferDesc::ReadbackBuffer(uint64_t(c.row_texels) * h * 4));
    if (!c.framebuffer || !c.readback) {
      c.framebuffer.reset();
      c.target.reset();
      c.readback.reset();
      return false;
    }
    c.width = w;
    c.height = h;
  }
  plume::RenderCommandList *cmd = s.command_list;
  bool raw = false;
  bool raw_rgba = false;  // the frame texture's bytes are R G B A (else B G R A, as the scan-out's readback)
#if defined(PLUME_DEKO3D)
  if (REXCVAR_GET(banjo_renut_capture_raw) && IsDeko3D()) {
    const auto *t = static_cast<const plume::Deko3DTexture *>(image.texture);
    if (t && t->valid) {
      switch (t->desc.format) {
      case plume::RenderFormat::R8G8B8A8_TYPELESS:
      case plume::RenderFormat::R8G8B8A8_UNORM:
        raw = raw_rgba = true;
        break;
      case plume::RenderFormat::B8G8R8A8_UNORM:
        raw = true;
        break;
      default:
        break;
      }
    }
    static int logged = 0;
    if (t && logged++ < 4)
      RGPU_INFO("banjo_renut_capture_raw: frame texture {}x{} format {}{}", t->desc.width, t->desc.height,
                int(t->desc.format), raw ? "" : ": not 8-bit RGBA or BGRA, the scan-out is captured");
  }
#endif
  if (raw) {
    cmd->barriers(plume::RenderBarrierStage::COPY,
                  plume::RenderTextureBarrier(image.texture, plume::RenderTextureLayout::COPY_SOURCE));
    if (image.layout)
      *image.layout = plume::RenderTextureLayout::COPY_SOURCE;
    const plume::RenderBox box(0, 0, int32_t(w), int32_t(h));
    cmd->copyTextureRegion(
        plume::RenderTextureCopyLocation::PlacedFootprint(c.readback.get(), kCaptureFormat, w, h, 1, c.row_texels),
        plume::RenderTextureCopyLocation::Subresource(image.texture), 0, 0, 0, &box);
  } else {
  cmd->barriers(plume::RenderBarrierStage::GRAPHICS,
                plume::RenderTextureBarrier(c.target.get(), plume::RenderTextureLayout::COLOR_WRITE));
  cmd->setFramebuffer(c.framebuffer.get());
  RecordScanOut(s, image, w, h);
  cmd->setFramebuffer(nullptr);
  cmd->barriers(plume::RenderBarrierStage::COPY,
                plume::RenderTextureBarrier(c.target.get(), plume::RenderTextureLayout::COPY_SOURCE));
  cmd->copyTextureRegion(
      plume::RenderTextureCopyLocation::PlacedFootprint(c.readback.get(), kCaptureFormat, w, h, 1, c.row_texels),
      plume::RenderTextureCopyLocation::Subresource(c.target.get()));
  }
  // Submits the frame's work with the capture and waits; the next list starts with nothing bound.
  detail::SubmitAndWaitLocked(s);
  const auto *data = static_cast<const uint8_t *>(c.readback->map());
  if (!data)
    return false;
  out.width = w;
  out.height = h;
  out.stride = size_t(w) * 4;
  out.data.resize(size_t(w) * h * 4);
  for (uint32_t y = 0; y < h; ++y) {
    const uint8_t *row = data + size_t(y) * c.row_texels * 4;
    uint8_t *dest = out.data.data() + size_t(y) * w * 4;
    // B G R A -> R G B X; a raw RGBA frame texture is in order already (before 2026-10-17 it was swapped too: every
    // raw capture of the Ryujinx runs had red and blue exchanged, the screen itself did not).
    const uint32_t r = raw_rgba ? 0 : 2, b = raw_rgba ? 2 : 0;
    for (uint32_t x = 0; x < w; ++x) {
      dest[x * 4 + 0] = row[x * 4 + r];
      dest[x * 4 + 1] = row[x * 4 + 1];
      dest[x * 4 + 2] = row[x * 4 + b];
      dest[x * 4 + 3] = 0xFF;
    }
  }
  c.readback->unmap();
  return true;
}

// From PresentLocked: one relaxed load when nobody asked.
void ServeCaptureLocked(VideoState &s, const PresentImage *image) {
  FrameCapture &c = g_capture;
  const uint64_t requested = c.requested.load(std::memory_order_acquire);
  if (requested == c.served_render || !image || !image->texture || image->descriptor == kInvalidDescriptor)
    return;
  rex::ui::RawImage out;
  const bool ok = CaptureFrameLocked(s, *image, out);
  c.served_render = requested;
  // ffxiii2-nx (k239, the Switch opening's stale frames): the captures (banjo_video_s) scan the image out again without
  // the stamp; with ffxiii2_diag_frame_stamp on, each served capture names its renderer frame ([rtlog]'s numbering).
  static const bool stamp = REXCVAR_GET(ffxiii2_diag_frame_stamp);
  if (stamp)
    RGPU_INFO("[capframe] capture {} = renderer frame {}{}", requested, TextureFrameLocked(), ok ? "" : " (failed)");
  {
    std::lock_guard lock(c.mutex);
    c.image = std::move(out);
    c.ok = ok;
    c.served = requested;
  }
  c.cv.notify_all();
}

// banjo_renut_diag_show_texture: the texture to show instead of the frame, when it has a 2D host texture and slot.
bool DiagTextureImageLocked(PresentImage &out) {
  static const u32 va = [] {
    const std::string v = REXCVAR_GET(banjo_renut_diag_show_texture);
    return v.empty() ? 0u : u32(std::strtoul(v.c_str(), nullptr, 16));
  }();
  if (!va)
    return false;
  HostTexture *t = FindHostTextureLocked(va);
  if (!t || !t->texture || t->slot == UINT32_MAX || t->slot == kInvalidDescriptor ||
      t->dimension != TextureDimension::Tex2D || t->stacked)
    return false;
  out.texture = t->texture.get();
  out.layout = &t->layout;
  out.descriptor = t->slot;
  out.width = t->alloc_width;
  out.height = t->alloc_height;
  out.gamma = float(REXCVAR_GET(banjo_renut_diag_show_gamma));
  return true;
}

// ffxiii2-nx (2026-11-06, D-003 on the PC): RenderDoc captures of single presented frames, ffxiii2_diag_rdc_count of
// them, one every ffxiii2_diag_rdc_every frames from ffxiii2_diag_rdc_start_s seconds after the first present, into
// capturas/rdc/ (needs RenderDoc's Vulkan layer in the process: VK_LAYER_PATH / VK_INSTANCE_LAYERS). Each capture runs
// from one present to the next, so it holds one frame of Renut's GPU work and its present.
void RenderDocCaptures() {
  static const int32_t start_s = REXCVAR_GET(ffxiii2_diag_rdc_start_s);
  if (start_s <= 0)
    return;
  static const int32_t count = REXCVAR_GET(ffxiii2_diag_rdc_count);
  static const int32_t every = std::max<int32_t>(1, REXCVAR_GET(ffxiii2_diag_rdc_every));
  static const auto t0 = std::chrono::steady_clock::now();
  static int32_t done = 0, since = 0;
  static bool capturing = false;
  if (capturing) {
    capturing = false;
    ++done;
    const bool ok = banjo::renderdoc::End();
    RGPU_INFO("[rdc] capture {} of frame {} ended: {}", done, TextureFrameLocked(), ok ? "ok" : "FAILED");
    since = 0;
    return;
  }
  if (done >= count || std::chrono::steady_clock::now() - t0 < std::chrono::seconds(start_s) || ++since < every)
    return;
  std::error_code ec;
  std::filesystem::create_directories("capturas/rdc", ec);
  if (banjo::renderdoc::Start("capturas/rdc/ffx")) {
    capturing = true;
    RGPU_INFO("[rdc] capture {} started after frame {}", done + 1, TextureFrameLocked());
  } else {
    static bool warned = false;
    if (!warned) {
      warned = true;
      RGPU_WARN("[rdc] RenderDoc is not loaded (set VK_LAYER_PATH and VK_INSTANCE_LAYERS=VK_LAYER_RENDERDOC_Capture)");
    }
  }
}

// The band trap (ffxiii2_diag_band_trap): see the cvars.
constexpr uint32_t kBandSlots = 6;
struct BandSlot {
  std::unique_ptr<plume::RenderBuffer> buffer;
  uint64_t frame = 0, present = 0;
  bool pending = false;
};
struct BandTrap {
  bool on = false, rdc = false, capturing = false, async = true;
  std::unique_ptr<plume::RenderTexture> target;
  std::unique_ptr<plume::RenderFramebuffer> framebuffer;
  uint32_t width = 0, height = 0, row_texels = 0;
  BandSlot slots[kBandSlots];
  uint32_t next_slot = 0;
  uint64_t presents = 0;
  int32_t max = 0, saved = 0, split = 336;
  std::chrono::steady_clock::time_point t0, start;
  rex::ui::RawImage prev, cur;
  uint64_t frames = 0, prev_frame = 0;
  double worst = 0.0;
};
BandTrap g_band;

double BandShare(const rex::ui::RawImage &im, int32_t split) {
  if (im.width < 1280 || im.height < 720 || im.data.empty())
    return 0.0;
  auto grey = [&](uint32_t x, int32_t y) {
    const uint8_t *p = im.data.data() + size_t(y) * im.stride + size_t(x) * 4;
    return 0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2];
  };
  double best = 0.0;
  for (int32_t r = split - 4; r <= split + 4; ++r) {
    if (r - 2 < 0 || r + 2 >= int32_t(im.height))
      continue;
    int n = 0, hit = 0;
    for (uint32_t x = 40; x <= 1240; x += 8, ++n)
      if (grey(x, r - 2) - grey(x, r + 2) > 10.0)
        ++hit;
    best = std::max(best, double(hit) / n);
  }
  return best;
}

void WritePpm(const rex::ui::RawImage &im, const std::string &path) {
  FILE *f = std::fopen(path.c_str(), "wb");
  if (!f)
    return;
  std::fprintf(f, "P6\n%u %u\n255\n", unsigned(im.width), unsigned(im.height));
  std::vector<uint8_t> row(size_t(im.width) * 3);
  for (uint32_t y = 0; y < im.height; ++y) {
    const uint8_t *src = im.data.data() + size_t(y) * im.stride;
    for (uint32_t x = 0; x < im.width; ++x) {
      row[x * 3 + 0] = src[x * 4 + 0];
      row[x * 3 + 1] = src[x * 4 + 1];
      row[x * 3 + 2] = src[x * 4 + 2];
    }
    std::fwrite(row.data(), 1, row.size(), f);
  }
  std::fclose(f);
}

bool BandTestImage(rex::ui::RawImage &img, uint64_t frame);

// Async trap: tests the slots the GPU is done with (in order), then records this frame's scan-out into a free slot.
void BandTrapAsyncLocked(VideoState &s, const PresentImage &image) {
  BandTrap &b = g_band;
  ++b.presents;
  for (uint32_t k = 0; k < kBandSlots; ++k) {
    BandSlot &slot = b.slots[(b.next_slot + k) % kBandSlots];
    if (!slot.pending || b.presents - slot.present < 4)
      continue;
    slot.pending = false;
    const auto *data = static_cast<const uint8_t *>(slot.buffer->map());
    if (!data)
      continue;
    b.cur.width = b.width;
    b.cur.height = b.height;
    b.cur.stride = size_t(b.width) * 4;
    b.cur.data.resize(size_t(b.width) * b.height * 4);
    for (uint32_t y = 0; y < b.height; ++y) {
      const uint8_t *row = data + size_t(y) * b.row_texels * 4;
      uint8_t *dest = b.cur.data.data() + size_t(y) * b.width * 4;
      for (uint32_t x = 0; x < b.width; ++x) {
        dest[x * 4 + 0] = row[x * 4 + 2];
        dest[x * 4 + 1] = row[x * 4 + 1];
        dest[x * 4 + 2] = row[x * 4 + 0];
        dest[x * 4 + 3] = 0xFF;
      }
    }
    slot.buffer->unmap();
    BandTestImage(b.cur, slot.frame);
  }
  const uint32_t w = image.source_width ? image.source_width : image.width;
  const uint32_t h = image.source_height ? image.source_height : image.height;
  if (!w || !h || !Video::EnsureCommandListLocked())
    return;
  if (!b.target || b.width != w || b.height != h) {
    for (BandSlot &slot : b.slots) {
      if (slot.pending)
        return;  // a size change: wait for the ring to drain first
    }
    b.framebuffer.reset();
    b.target.reset();
    for (BandSlot &slot : b.slots)
      slot.buffer.reset();
    plume::RenderTextureDesc desc = plume::RenderTextureDesc::Texture2D(w, h, 1, kCaptureFormat);
    desc.flags |= plume::RenderTextureFlag::RENDER_TARGET;
    b.target = s.device->createTexture(desc);
    if (!b.target)
      return;
    const plume::RenderTexture *attachment = b.target.get();
    b.framebuffer = s.device->createFramebuffer(plume::RenderFramebufferDesc(&attachment, 1));
    b.row_texels = (w + 63) & ~63u;
    for (BandSlot &slot : b.slots)
      slot.buffer = s.device->createBuffer(plume::RenderBufferDesc::ReadbackBuffer(uint64_t(b.row_texels) * h * 4));
    b.width = w;
    b.height = h;
  }
  BandSlot *free_slot = nullptr;
  for (uint32_t k = 0; k < kBandSlots && !free_slot; ++k) {
    BandSlot &slot = b.slots[(b.next_slot + k) % kBandSlots];
    if (!slot.pending && slot.buffer) {
      free_slot = &slot;
      b.next_slot = (b.next_slot + k + 1) % kBandSlots;
    }
  }
  if (!free_slot || !b.framebuffer)
    return;
  plume::RenderCommandList *cmd = s.command_list;
  cmd->barriers(plume::RenderBarrierStage::GRAPHICS,
                plume::RenderTextureBarrier(b.target.get(), plume::RenderTextureLayout::COLOR_WRITE));
  cmd->setFramebuffer(b.framebuffer.get());
  RecordScanOut(s, image, w, h);
  cmd->setFramebuffer(nullptr);
  cmd->barriers(plume::RenderBarrierStage::COPY,
                plume::RenderTextureBarrier(b.target.get(), plume::RenderTextureLayout::COPY_SOURCE));
  cmd->copyTextureRegion(
      plume::RenderTextureCopyLocation::PlacedFootprint(free_slot->buffer.get(), kCaptureFormat, w, h, 1, b.row_texels),
      plume::RenderTextureCopyLocation::Subresource(b.target.get()));
  free_slot->pending = true;
  free_slot->frame = TextureFrameLocked();
  free_slot->present = b.presents;
}

// Before the present's own work (inside the capture that began after the previous present): reads the frame back and
// tests it. Returns true when it is banded.
bool BandTrapTestLocked(VideoState &s, const PresentImage *image) {
  BandTrap &b = g_band;
  static const bool init = [] {
    g_band.on = REXCVAR_GET(ffxiii2_diag_band_trap);
    g_band.rdc = REXCVAR_GET(ffxiii2_diag_band_rdc);
    g_band.max = REXCVAR_GET(ffxiii2_diag_band_max);
    g_band.split = REXCVAR_GET(ffxiii2_diag_band_split);
    g_band.async = REXCVAR_GET(ffxiii2_diag_band_async) && !g_band.rdc;
    g_band.t0 = std::chrono::steady_clock::now();
    g_band.start = g_band.t0 + std::chrono::seconds(std::max<int32_t>(0, REXCVAR_GET(ffxiii2_diag_band_start_s)));
    return true;
  }();
  (void)init;
  static const bool dumping = REXCVAR_GET(ffxiii2_diag_band_dump_all) > 0;
  if (!b.on || (b.saved >= b.max && !dumping) || !image || !image->texture ||
      image->descriptor == kInvalidDescriptor || std::chrono::steady_clock::now() < b.start)
    return false;
  if (b.async) {
    BandTrapAsyncLocked(s, *image);
    return false;
  }
  if (!CaptureFrameLocked(s, *image, b.cur))
    return false;
  return BandTestImage(b.cur, TextureFrameLocked());
}

void BandDumpGrey(const rex::ui::RawImage &im, uint64_t frame) {
  static const int32_t limit = REXCVAR_GET(ffxiii2_diag_band_dump_all);
  static int32_t dumped = 0;
  if (dumped >= limit)
    return;
  if (dumped++ == 0) {
    std::error_code ec;
    std::filesystem::create_directories("capturas/band/all", ec);
  }
  char name[96];
  std::snprintf(name, sizeof(name), "capturas/band/all/f%06llu.pgm", (unsigned long long)frame);
  FILE *f = std::fopen(name, "wb");
  if (!f)
    return;
  std::fprintf(f, "P5\n%u %u\n255\n", unsigned(im.width), unsigned(im.height));
  std::vector<uint8_t> row(im.width);
  for (uint32_t y = 0; y < im.height; ++y) {
    const uint8_t *src = im.data.data() + size_t(y) * im.stride;
    for (uint32_t x = 0; x < im.width; ++x)
      row[x] = uint8_t((src[x * 4] * 77 + src[x * 4 + 1] * 150 + src[x * 4 + 2] * 29) >> 8);
    std::fwrite(row.data(), 1, row.size(), f);
  }
  std::fclose(f);
}

bool BandTestImage(rex::ui::RawImage &img, uint64_t frame) {
  BandTrap &b = g_band;
  BandDumpGrey(img, frame);
  if (b.saved >= b.max)
    return false;
  const double share = BandShare(img, b.split);
  ++b.frames;
  b.worst = std::max(b.worst, share);
  if (b.frames % 600 == 0)
    RGPU_INFO("[band] {} frames tested, worst edge share {:.2f}, {} banded", b.frames, b.worst, b.saved);
  static const double need = REXCVAR_GET(ffxiii2_diag_band_share);
  const bool banded = share >= need;
  if (banded) {
    ++b.saved;
    std::error_code ec;
    std::filesystem::create_directories("capturas/band", ec);
    char name[96];
    std::snprintf(name, sizeof(name), "capturas/band/f%06llu_band.ppm", (unsigned long long)frame);
    WritePpm(img, name);
    if (!b.prev.data.empty()) {
      std::snprintf(name, sizeof(name), "capturas/band/f%06llu_before.ppm", (unsigned long long)b.prev_frame);
      WritePpm(b.prev, name);
    }
    RGPU_INFO("[band] frame {} banded (edge share {:.2f}), saved {} of {}{}", frame, share, b.saved, b.max,
              b.capturing ? ", RenderDoc capture kept" : "");
  } else if (share >= 0.36) {
    RGPU_INFO("[band] frame {} near (edge share {:.2f})", frame, share);
  }
  std::swap(b.prev, img);
  b.prev_frame = frame;
  return banded;
}

// After the present: ends the rolling capture (kept when the frame was banded) and starts the next one.
void BandTrapRdcAfterPresent(bool banded) {
#if defined(FFXIII2_BAND_RDC)
  BandTrap &b = g_band;
  if (!b.on || !b.rdc)
    return;
  RENDERDOC_API_1_4_1 *api = BandRdcApi();
  if (!api) {
    static bool warned = false;
    if (!warned) {
      warned = true;
      RGPU_WARN("[band] RenderDoc is not loaded: no captures (VK_LAYER_PATH / VK_INSTANCE_LAYERS)");
    }
    return;
  }
  if (b.capturing) {
    b.capturing = false;
    if (banded)
      RGPU_INFO("[band] RenderDoc capture of frame {} written: {}", TextureFrameLocked(),
                api->EndFrameCapture(nullptr, nullptr) ? "ok" : "FAILED");
    else
      api->DiscardFrameCapture(nullptr, nullptr);
  }
  if (b.saved >= b.max || std::chrono::steady_clock::now() < b.start)
    return;
  static bool dir = false;
  if (!dir) {
    dir = true;
    std::error_code ec;
    std::filesystem::create_directories("capturas/band", ec);
    api->SetCaptureFilePathTemplate("capturas/band/band");
  }
  api->StartFrameCapture(nullptr, nullptr);
  b.capturing = true;
#else
  (void)banded;
#endif
}

// Records the back-buffer pass and presents. Caller holds s.mutex.
void PresentLocked(VideoState &s, const PresentImage *image) {
  // Logs, once, why the GPU stopped if an earlier submission faulted.
  ReportDeviceLost(s.device.get());
  PresentImage diag_image;
  if (image && DiagTextureImageLocked(diag_image))
    image = &diag_image;
  ApplyVsync(s);
  if (s.swap_chain->needsResize() || s.resize_requested.exchange(false, std::memory_order_acq_rel))
    RebuildSwapChain(s);
  UpdateDisplayRampLocked();      // bejeweled3-nx: the game's display ramp table, before anything scans out
  ServeCaptureLocked(s, image);  // banjo-nx: test captures (banjo_renut_capture.h)
  const bool banded = BandTrapTestLocked(s, image);  // ffxiii2-nx: the D-003 band trap (off by default)
  if (s.swap_framebuffers.empty()) {
    // Minimized or failed resize: keep the frame ring moving without a present.
    if (s.list_open)
      detail::SubmitAndAdvance(s, nullptr, nullptr);
    return;
  }

  plume::RenderCommandSemaphore *acquire = s.acquire_semaphores[s.frame].get();
  uint32_t image_index = 0;
  auto acquire_image = [&] {
    render_thread::WaitScope wait;
    return s.swap_chain->acquireTexture(acquire, &image_index);
  };
  if (!acquire_image()) {
    RebuildSwapChain(s);
    acquire = s.acquire_semaphores[s.frame].get();
    if (s.swap_framebuffers.empty() || !acquire_image()) {
      if (s.list_open)
        detail::SubmitAndAdvance(s, nullptr, nullptr);
      return;
    }
  }

  if (!Video::EnsureCommandListLocked())
    return;
  static const uint32_t kProfileLabel = gpu_profile::Label("present and overlays");
  gpu_profile::Mark(s, kProfileLabel);
  plume::RenderCommandList *cmd = s.command_list;
  plume::RenderTexture *back = s.swap_chain->getTexture(image_index);
  plume::RenderFramebuffer *back_fb = s.swap_framebuffers[image_index].get();
  const uint32_t out_w = s.swap_chain->getWidth();
  const uint32_t out_h = s.swap_chain->getHeight();

  // FFXIII-2-Native-Switch (2026-11-10, ffns_fsr: the options menu's "Upscaling", render/fsr.cpp): with the frame drawn
  // under the shown size (dynamic resolution), FSR 1's EASU scales it into a target at the shown size first, outside the
  // swapchain's pass; the scan-out below then sharpens that (RCAS) and applies the display ramp, instead of sampling the
  // frame bilinearly. Off, or at the shown size: the scan-out as before.
  plume::RenderViewport fsr_viewport = plume::RenderViewport(0.0f, 0.0f, float(out_w), float(out_h));
  bool fsr = false;
  if (image && image->texture && image->descriptor != kInvalidDescriptor && FsrWanted()) {
    const uint32_t src_w = image->source_width ? image->source_width : image->width;
    const uint32_t src_h = image->source_height ? image->source_height : image->height;
    const FrameFit fit = FitFrame(out_w, out_h, src_w, src_h);
    const float frame_u = image->width ? float(src_w) / float(image->width) : 1.0f;
    const float frame_v = image->height ? float(src_h) / float(image->height) : 1.0f;
    const float scale[2] = {frame_u * fit.visible[0], frame_v * fit.visible[1]};
    const float translate[2] = {frame_u * (1.0f - fit.visible[0]) * 0.5f, frame_v * (1.0f - fit.visible[1]) * 0.5f};
    fsr_viewport = fit.viewport;
    // The swapchain's format (kSwapChainFormat in video.cpp; kCaptureFormat above).
    fsr = FsrEasuLocked(s, *image, fit.viewport, scale, translate, src_w, src_h, kCaptureFormat);
  }

  cmd->barriers(plume::RenderBarrierStage::GRAPHICS,
                plume::RenderTextureBarrier(back, plume::RenderTextureLayout::COLOR_WRITE));
  cmd->setFramebuffer(back_fb);
  const bool scan_out = image && image->texture && image->descriptor != kInvalidDescriptor;
  // Letterbox bars (and the whole output when there is no guest image). banjo-nx (2026-10-19, GPU): not when the
  // frame covers the whole output, where the scan-out writes every pixel the clear would.
  bool covers = false;
  if (scan_out) {
    const FrameFit fit = FitFrame(out_w, out_h, image->source_width ? image->source_width : image->width,
                                  image->source_height ? image->source_height : image->height);
    covers = fit.viewport.x <= 0.0f && fit.viewport.y <= 0.0f && fit.viewport.x + fit.viewport.width >= float(out_w) &&
             fit.viewport.y + fit.viewport.height >= float(out_h);
  }
  if (!covers)
    cmd->clearColor(0, plume::RenderColor(0.0f, 0.0f, 0.0f, 1.0f));

  if (scan_out) {
    if (fsr) {
      float param0 = 0.0f, param1 = 0.0f;
      ScanOutParams(s, *image, param0, param1);
      FsrScanOutLocked(s, fsr_viewport, kCaptureFormat, param0, param1);
    } else {
      RecordScanOut(s, *image, out_w, out_h);
    }
  }

  cmd->setViewports(plume::RenderViewport(0.0f, 0.0f, float(out_w), float(out_h)));
  cmd->setScissors(plume::RenderRect(0, 0, int32_t(out_w), int32_t(out_h)));
  // The SDK's own menus, then reNut's overlay over them.
  if (s.overlay_hook)
    s.overlay_hook(cmd, back_fb, out_w, out_h);
  overlay::DrawLocked(cmd, out_w, out_h);
  static const bool stamp = REXCVAR_GET(ffxiii2_diag_frame_stamp);
  if (stamp && out_h >= 8) {
    const uint64_t n = TextureFrameLocked();
    plume::RenderRect ones[22], zeros[22];
    uint32_t n1 = 0, n0 = 0;
    const int32_t y0 = int32_t(out_h) - 6, y1 = int32_t(out_h);
    ones[n1++] = plume::RenderRect(0, y0, 8, y1);     // start marker: white
    zeros[n0++] = plume::RenderRect(8, y0, 16, y1);   // then black
    for (int32_t bit = 0; bit < 20; ++bit) {
      const int32_t x = 16 + bit * 8;
      if ((n >> bit) & 1)
        ones[n1++] = plume::RenderRect(x, y0, x + 8, y1);
      else
        zeros[n0++] = plume::RenderRect(x, y0, x + 8, y1);
    }
    cmd->clearColor(0, plume::RenderColor(1.0f, 1.0f, 1.0f, 1.0f), ones, n1);
    cmd->clearColor(0, plume::RenderColor(0.0f, 0.0f, 0.0f, 1.0f), zeros, n0);
  }

  cmd->setFramebuffer(nullptr);
  cmd->barriers(plume::RenderBarrierStage::GRAPHICS,
                plume::RenderTextureBarrier(back, plume::RenderTextureLayout::PRESENT));

  plume::RenderCommandSemaphore *present_semaphore = s.present_semaphores[image_index].get();
  detail::SubmitAndAdvance(s, acquire, present_semaphore);
  plume::RenderCommandSemaphore *waits[] = {present_semaphore};
  bool presented;
  {
    // With vsync the present can hold until the display takes a frame.
    render_thread::WaitScope wait;
    presented = s.swap_chain->present(image_index, waits, 1);
  }
  if (!presented)
    s.resize_requested.store(true, std::memory_order_release);
  RenderDocCaptures();
  BandTrapRdcAfterPresent(banded);
}

} // namespace

namespace {

// A guest that has not presented for this long is stalled (loading, movie
// decode); the UI pump then presents the overlay on its own.
constexpr int64_t kGuestStallMs = 100;

int64_t NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

} // namespace

// banjo-nx (2026-10-19, latency batch 3): the fences of the last guest presents' frames, as copies (the slot's fence is
// re-armed two frames later), for the display thread (renut_engine/display_sync.cpp) to test without waiting. Its own
// mutex: the render thread holds State().mutex for whole frames.
namespace {
constexpr uint32_t kPresentFences = 8;
struct PresentFence {
  uint64_t index = 0;
#if defined(PLUME_DEKO3D)
  plume::Deko3DFenceCopy fence;
#endif
};
std::mutex g_present_fence_mutex;
PresentFence g_present_fences[kPresentFences];
std::atomic<uint64_t> g_present_count{0};
} // namespace

uint64_t Video::PresentCount() {
  return g_present_count.load(std::memory_order_acquire);
}

int Video::PresentDone(uint64_t index) {
#if defined(PLUME_DEKO3D)
  if (!index || index > g_present_count.load(std::memory_order_acquire))
    return -1;
  plume::Deko3DFenceCopy fence;
  {
    // Copied out: the test (an ioctl) runs without the mutex the render thread takes at every present.
    std::lock_guard lock(g_present_fence_mutex);
    const PresentFence &p = g_present_fences[index % kPresentFences];
    if (p.index != index)
      return 1;  // older than the ring: long finished (two frames in flight)
    fence = p.fence;
  }
  return plume::Deko3DFenceCopyDone(fence) ? 1 : 0;
#else
  (void)index;
  return -1;
#endif
}

void Video::Present(const PresentImage *image) {
  auto &s = State();
  if (s.shutting_down.load(std::memory_order_acquire))
    return;
  std::lock_guard lock(s.mutex);
  if (!s.host_ready || s.shutting_down.load(std::memory_order_acquire))
    return;
  PresentLocked(s, image);
  s.last_guest_present_ms.store(NowMs(), std::memory_order_release);
  detail::LatencyTrimLocked(s);  // banjo-nx (2026-10-20, input latency, banjo_deko3d_latency_trim)
  {
    // The frame just submitted: the slot before the one SubmitAndAdvance moved to.
    std::lock_guard fence_lock(g_present_fence_mutex);
    const uint64_t index = g_present_count.load(std::memory_order_relaxed) + 1;
    PresentFence &p = g_present_fences[index % kPresentFences];
    p.index = index;
#if defined(PLUME_DEKO3D)
    p.fence = plume::Deko3DCopyFence(s.fences[(s.frame + kFramesInFlight - 1) % kFramesInFlight].get());
#endif
    g_present_count.store(index, std::memory_order_release);
  }
}

void Video::PresentOverlayOnly() {
  auto &s = State();
  if (s.shutting_down.load(std::memory_order_acquire))
    return;
  const int64_t now = NowMs();
  if (s.guest_ready &&
      now - s.last_guest_present_ms.load(std::memory_order_acquire) < kGuestStallMs)
    return;
  // The render thread holds the mutex while it waits on the UI thread for the
  // overlay, so never block on it here.
  std::unique_lock lock(s.mutex, std::try_to_lock);
  if (!lock.owns_lock() || !s.host_ready || s.shutting_down.load(std::memory_order_acquire))
    return;
  // banjo-nx (2026-10-19, the colour flash): while the guest stalls (loading) with nothing of the overlay to show,
  // nothing is presented: the display keeps the last frame, as the console's does. The re-present of the last front
  // buffer showed whatever that texture held by then, and the game may free or remake it while it loads: on deko3d a
  // new image's memory is not cleared (Vulkan's came zeroed), a full-screen frame of other data. With the overlay up
  // the last frame is presented under it, but only from a texture a resolve wrote (else the overlay on black).
  // banjo_renut_stall_present = true: as before.
  if (s.guest_ready && !banjo::renut_gpu::StallPresentOn() && !OverlayDrawer::HasLatest())
    return;
  PresentImage image =
      s.guest_ready && s.last_front_buffer ? FrontBufferImageLocked(s.last_front_buffer) : PresentImage{};
  if (image.texture && !banjo::renut_gpu::StallPresentOn() && !FrontBufferWrittenLocked(s.last_front_buffer))
    image = PresentImage{};
  PresentLocked(s, image.texture ? &image : nullptr);
}

// peggle2-nx (2026-10-20): see video.h. Peggle 2's ramp (the XDK's linear ramp through the display's gamma table:
// [64] = 193, [128] = 462, [255] = 1023 of 1023) is close to a power of ~1.18; the native renderer applies the table
// itself (the darks of the two differed by up to 16 of 255 before this). The exponent is the least-squares fit of
// x^g to the green ramp over its whole range (golden-section search on g in [0.25, 4]).
void NoteDisplayRamp(const uint16_t *ramp, bool pwl) {
  float xs[256], ys[256];
  int n = 0;
  if (pwl) {
    for (int k = 1; k < 128; ++k) {
      xs[n] = float(k) / 128.0f;
      ys[n++] = float(ramp[256 + k * 2] >> 6) / 1023.0f;
    }
  } else {
    for (int i = 1; i < 256; ++i) {
      xs[n] = float(i) / 255.0f;
      ys[n++] = float(ramp[256 + i] >> 6) / 1023.0f;
    }
  }
  auto error = [&](float g) {
    double sum = 0.0;
    for (int i = 0; i < n; ++i) {
      const double d = std::pow(double(xs[i]), double(g)) - ys[i];
      sum += d * d;
    }
    return sum;
  };
  float lo = 0.25f, hi = 4.0f;
  const float phi = 0.618034f;
  float a = hi - phi * (hi - lo), b = lo + phi * (hi - lo);
  double ea = error(a), eb = error(b);
  for (int i = 0; i < 60; ++i) {
    if (ea < eb) {
      hi = b; b = a; eb = ea; a = hi - phi * (hi - lo); ea = error(a);
    } else {
      lo = a; a = b; ea = eb; b = lo + phi * (hi - lo); eb = error(b);
    }
  }
  const float g = 0.5f * (lo + hi);
  float worst = 0.0f;
  for (int i = 0; i < n; ++i)
    worst = std::max(worst, std::abs(float(std::pow(double(xs[i]), double(g))) - ys[i]));
  const bool on = REXCVAR_GET(zumasrevenge_renut_display_ramp);
  // bejeweled3-nx: a table ramp goes to the present as the table (UpdateDisplayRampLocked).
  if (on && !pwl && REXCVAR_GET(zumasrevenge_renut_display_ramp_table)) {
    std::lock_guard<std::mutex> lock(g_display_ramp_table.mutex);
    bool same = g_display_ramp_table.generation != 0;
    for (int c = 0; c < 3; ++c) {
      for (int i = 0; i < 256; ++i) {
        const uint16_t v = uint16_t(ramp[c * 256 + i] >> 6);
        same = same && g_display_ramp_table.values[c][i] == v;
        g_display_ramp_table.values[c][i] = v;
      }
    }
    if (!same) {
      ++g_display_ramp_table.generation;
      RGPU_INFO("Display ramp: the table itself (256 entries a channel; [128] = {}/{}/{} of 1023)",
                g_display_ramp_table.values[0][128], g_display_ramp_table.values[1][128],
                g_display_ramp_table.values[2][128]);
    }
  }
  const float previous = g_display_ramp_exponent.exchange(on ? g : 1.0f, std::memory_order_relaxed);
  if (std::abs(previous - (on ? g : 1.0f)) > 1e-4f)
    RGPU_INFO("Display ramp{}: exponent {:.3f} (worst difference {:.1f} of 255){}", pwl ? " (piecewise linear)" : "", g,
              worst * 255.0f, on ? "" : "; not applied (zumasrevenge_renut_display_ramp = false)");
}

bool CaptureFrame(rex::ui::RawImage &out, int timeout_ms) {
  FrameCapture &c = g_capture;
  std::unique_lock lock(c.mutex);
  const uint64_t ticket = c.requested.fetch_add(1, std::memory_order_acq_rel) + 1;
  if (!c.cv.wait_for(lock, std::chrono::milliseconds(timeout_ms), [&] { return c.served >= ticket; }) || !c.ok)
    return false;
  out = c.image;
  return true;
}

} // namespace renut::gpu
