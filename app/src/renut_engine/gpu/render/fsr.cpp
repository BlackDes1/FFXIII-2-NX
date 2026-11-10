// FFXIII-2-Native-Switch (2026-11-10, the user: "Integrate FSR as an option in the menu"): AMD FidelityFX FSR 1 for the
// scan-out (backend/present.cpp, PresentLocked). With dynamic resolution FF XIII-2's whole frame (the HUD included) is
// drawn at the render height (612 lines by default) and the scan-out scales it to the display with one bilinear sample a
// pixel. With ffns_fsr on, EASU (shaders/host/fsr_easu_ps.hlsl) first scales the frame into a target at the shown size,
// then the scan-out draws RCAS (fsr_rcas_ps.hlsl: sharpening, then the display ramp as present_ps.hlsl) from it. Both
// shaders are ports of SwitchWaker's (MIT), which port AMD's ffx_fsr1.h (MIT). NOT pixel-identical (a player's choice,
// off by default); at the shown size (render resolution 720p) nothing changes.
// Cost (console, S1b, handheld 460.8 MHz): EASU + RCAS 6.1 ms of GPU a frame at 1280x720 (27.9 -> 34.0 ms; vis4/fs01,
// texel fetches or samples alike: the arithmetic, about what FSR 1 costs per pixel elsewhere scaled to this GPU). Mode 1
// (bilinear + RCAS sharpening) keeps the plain filter's cost and adds RCAS only.
// Targets are kept per size and never freed (at most handheld and docked): a texture the GPU may still read is never
// destroyed.
#include "renut_engine/gpu/render/fsr.h"

#include <cmath>
#include <cstring>
#include <memory>
#include <unordered_map>

#include <plume_render_interface.h>
#include <rex/cvar.h>

#include "renut_engine/gpu/backend/backend.h"
#include "renut_engine/gpu/backend/gpu_profile.h"
#include "renut_engine/gpu/backend/video.h"
#include "renut_engine/gpu/log.h"

#include "host_shaders/fsr_easu_ps.spirv.h"
#include "host_shaders/fsr_rcas_ps.spirv.h"
#include "host_shaders/present_ps.spirv.h"
#include "host_shaders/present_vs.spirv.h"
#include "host_shaders/smaa_vs.spirv.h"
#if defined(PLUME_DEKO3D)
#include "host_shaders/fsr_easu_ps.deko3d.h"
#include "host_shaders/fsr_rcas_ps.deko3d.h"
#include "host_shaders/present_ps.deko3d.h"
#include "host_shaders/present_vs.deko3d.h"
#include "host_shaders/smaa_vs.deko3d.h"
#endif
#if defined(_WIN32)
#include "host_shaders/fsr_easu_ps.dxil.h"
#include "host_shaders/fsr_rcas_ps.dxil.h"
#include "host_shaders/present_ps.dxil.h"
#include "host_shaders/present_vs.dxil.h"
#include "host_shaders/smaa_vs.dxil.h"
#endif

REXCVAR_DEFINE_INT32(ffns_fsr, 0, "FFNS/Graphics",
                     "The scan-out's upscale when the frame is drawn under the shown size (dynamic resolution): 0 = one "
                     "bilinear sample a pixel, as before; 1 = bilinear, then AMD FidelityFX RCAS sharpening; 2 = AMD "
                     "FidelityFX FSR 1 (EASU + RCAS). NOT pixel-identical above 0");
REXCVAR_DEFINE_INT32(ffns_fsr_sharpness, 50, "FFNS/Graphics",
                     "FSR 1's RCAS sharpening in hundredths of a stop: 0 = the sharpest, 200 = soft (AMD's default 20)");

namespace renut::gpu {
namespace {

constexpr plume::RenderFormat kFsrFormat = plume::RenderFormat::R8G8B8A8_UNORM;

struct FsrTarget {
  std::unique_ptr<plume::RenderTexture> texture;
  std::unique_ptr<plume::RenderTextureView> view;
  std::unique_ptr<plume::RenderFramebuffer> framebuffer;
  uint32_t slot = kInvalidDescriptor;
  uint32_t width = 0, height = 0;
  plume::RenderTextureLayout layout = plume::RenderTextureLayout::UNKNOWN;
};

struct FsrState {
  bool failed = false;  // a shader or pipeline could not be made: the plain scan-out for the rest of the run
  std::unique_ptr<plume::RenderShader> present_vs, present_ps, smaa_vs, easu_ps, rcas_ps;
  std::unique_ptr<plume::RenderPipeline> easu, bilinear;  // mode 2's upscale, mode 1's
  std::unordered_map<uint32_t, std::unique_ptr<plume::RenderPipeline>> rcas;  // by the scan-out's format
  std::unordered_map<uint64_t, std::unique_ptr<FsrTarget>> targets;           // by width << 32 | height
  FsrTarget *current = nullptr;  // the target this frame's EASU wrote
  uint32_t logged = 0;
};
FsrState g_fsr;

std::unique_ptr<plume::RenderPipeline> MakePipeline(VideoState &s, plume::RenderShader *vs, plume::RenderShader *ps,
                                                    plume::RenderFormat format, const char *name) {
  plume::RenderGraphicsPipelineDesc desc;
  desc.pipelineLayout = s.utility_layout.get();
  desc.vertexShader = vs;
  desc.pixelShader = ps;
  desc.primitiveTopology = plume::RenderPrimitiveTopology::TRIANGLE_LIST;
  desc.cullMode = plume::RenderCullMode::NONE;
  desc.renderTargetCount = 1;
  desc.renderTargetFormat[0] = format;
  desc.renderTargetBlend[0] = plume::RenderBlendDesc::Copy();
  desc.depthTargetFormat = plume::RenderFormat::UNKNOWN;
  std::unique_ptr<plume::RenderPipeline> pipeline = s.device->createGraphicsPipeline(desc);
  if (pipeline)
    pipeline->setName(name);
  return pipeline;
}

bool Fail(const char *what) {
  g_fsr.failed = true;
  RGPU_WARN("[fsr] {} could not be made: FSR off for this run (the plain scan-out)", what);
  return false;
}

bool Pipelines(VideoState &s, plume::RenderFormat scan_out_format) {
  FsrState &f = g_fsr;
  if (f.failed)
    return false;
  if (!f.easu) {
    f.present_vs = CreateHostShader(s.device.get(), RENUT_HOST_SHADER(present_vs), "present_vs");
    f.present_ps = CreateHostShader(s.device.get(), RENUT_HOST_SHADER(present_ps), "present_ps");
    f.smaa_vs = CreateHostShader(s.device.get(), RENUT_HOST_SHADER(smaa_vs), "smaa_vs");
    f.easu_ps = CreateHostShader(s.device.get(), RENUT_HOST_SHADER(fsr_easu_ps), "fsr_easu_ps");
    f.rcas_ps = CreateHostShader(s.device.get(), RENUT_HOST_SHADER(fsr_rcas_ps), "fsr_rcas_ps");
    if (!f.present_vs || !f.present_ps || !f.smaa_vs || !f.easu_ps || !f.rcas_ps)
      return Fail("a shader");
    f.easu = MakePipeline(s, f.present_vs.get(), f.easu_ps.get(), kFsrFormat, "ffns FSR EASU");
    if (!f.easu)
      return Fail("the EASU pipeline");
    f.bilinear = MakePipeline(s, f.present_vs.get(), f.present_ps.get(), kFsrFormat, "ffns FSR bilinear");
    if (!f.bilinear)
      return Fail("the bilinear pipeline");
  }
  auto &rcas = f.rcas[uint32_t(scan_out_format)];
  if (!rcas) {
    rcas = MakePipeline(s, f.smaa_vs.get(), f.rcas_ps.get(), scan_out_format, "ffns FSR RCAS");
    if (!rcas)
      return Fail("the RCAS pipeline");
  }
  return true;
}

FsrTarget *Target(VideoState &s, uint32_t width, uint32_t height) {
  auto &t = g_fsr.targets[uint64_t(width) << 32 | height];
  if (t)
    return t->slot != kInvalidDescriptor && t->framebuffer ? t.get() : nullptr;
  t = std::make_unique<FsrTarget>();
  t->width = width;
  t->height = height;
  t->texture = s.device->createTexture(plume::RenderTextureDesc::ColorTarget(width, height, kFsrFormat));
  if (!t->texture)
    return nullptr;
  t->texture->setName("ffns FSR target");
  t->view = t->texture->createTextureView(plume::RenderTextureViewDesc::Texture2D(kFsrFormat));
  if (!t->view)
    return nullptr;
  const plume::RenderTexture *colors[1] = {t->texture.get()};
  t->framebuffer = s.device->createFramebuffer(plume::RenderFramebufferDesc(colors, 1, nullptr));
  if (!t->framebuffer)
    return nullptr;
  t->slot = Video::AllocateTextureSlotLocked();
  if (t->slot == kInvalidDescriptor)
    return nullptr;
  Video::SetTextureSlotLocked(t->slot, t->texture.get(), t->view.get());
  return t.get();
}

}  // namespace

bool FsrWanted() { return REXCVAR_GET(ffns_fsr) > 0; }

bool FsrEasuLocked(VideoState &s, const PresentImage &image, const plume::RenderViewport &viewport, const float scale[2],
                   const float translate[2], uint32_t src_w, uint32_t src_h, plume::RenderFormat scan_out_format) {
  FsrState &f = g_fsr;
  f.current = nullptr;
  if (!FsrWanted() || !s.command_list || f.failed)
    return false;
  const uint32_t width = uint32_t(std::lround(viewport.width));
  const uint32_t height = uint32_t(std::lround(viewport.height));
  // Only an upscale, and sizes the shader's packed parameters hold.
  const float shown_rows = float(src_h) * scale[1] * (image.height ? float(image.height) / float(src_h) : 1.0f);
  if (width == 0 || height == 0 || src_w == 0 || src_h == 0 || shown_rows >= float(height) || image.width > 0xFFFF ||
      image.height > 0xFFFF || src_w > 0xFFFF || src_h > 0xFFFF)
    return false;
  if (!Pipelines(s, scan_out_format))
    return false;
  FsrTarget *t = Target(s, width, height);
  if (!t) {
    Fail("the target");
    return false;
  }
  static const uint32_t kLabel = gpu_profile::Label("FSR EASU");
  gpu_profile::Mark(s, kLabel);
  plume::RenderCommandList *cmd = s.command_list;
  plume::RenderTextureBarrier barriers[2];
  uint32_t count = 0;
  if (image.layout && *image.layout != plume::RenderTextureLayout::SHADER_READ) {
    barriers[count++] = plume::RenderTextureBarrier(image.texture, plume::RenderTextureLayout::SHADER_READ);
    *image.layout = plume::RenderTextureLayout::SHADER_READ;
  }
  if (t->layout != plume::RenderTextureLayout::COLOR_WRITE) {
    barriers[count++] = plume::RenderTextureBarrier(t->texture.get(), plume::RenderTextureLayout::COLOR_WRITE);
    t->layout = plume::RenderTextureLayout::COLOR_WRITE;
  }
  if (count)
    cmd->barriers(plume::RenderBarrierStage::GRAPHICS, nullptr, 0, barriers, count);
  cmd->setFramebuffer(t->framebuffer.get());
  cmd->setViewports(plume::RenderViewport(0.0f, 0.0f, float(width), float(height)));
  cmd->setScissors(plume::RenderRect(0, 0, int32_t(width), int32_t(height)));
  cmd->setGraphicsPipelineLayout(s.utility_layout.get());
  const bool easu = REXCVAR_GET(ffns_fsr) >= 2;
  cmd->setPipeline(easu ? f.easu.get() : f.bilinear.get());
  cmd->setGraphicsDescriptorSet(s.texture_set.get(), 0);
  cmd->setGraphicsDescriptorSet(s.sampler_set.get(), 1);
  UtilityPush push{};
  push.scale[0] = scale[0];
  push.scale[1] = scale[1];
  push.translate[0] = translate[0];
  push.translate[1] = translate[1];
  push.texture_index = image.descriptor;
  if (easu) {
    push.sampler_index = kPointClampSamplerSlot;  // unused: texel fetches
    const uint32_t frame = src_w | src_h << 16;
    const uint32_t texture = image.width | image.height << 16;
    std::memcpy(&push.param0, &frame, sizeof(frame));
    std::memcpy(&push.param1, &texture, sizeof(texture));
  } else {
    // Mode 1: present_ps.hlsl as a plain bilinear copy (gamma exponent 1); the ramp comes in the RCAS pass.
    push.sampler_index = kLinearClampSamplerSlot;
    push.param0 = 1.0f;
  }
  cmd->setGraphicsPushConstants(0, &push, 0, sizeof(push));
  cmd->drawInstanced(3, 1, 0, 0);
  cmd->setFramebuffer(nullptr);
  cmd->barriers(plume::RenderBarrierStage::GRAPHICS,
                plume::RenderTextureBarrier(t->texture.get(), plume::RenderTextureLayout::SHADER_READ));
  t->layout = plume::RenderTextureLayout::SHADER_READ;
  f.current = t;
  if (f.logged < 2) {
    ++f.logged;
    RGPU_INFO("[fsr] {} {}x{} (frame {}x{} in a {}x{} texture) -> {}x{}, then RCAS at {} hundredths of a stop", easu ? "EASU" : "bilinear", uint32_t(
                  std::lround(src_w * scale[0] * (image.width ? float(image.width) / float(src_w) : 1.0f))),
              uint32_t(std::lround(shown_rows)), src_w, src_h, image.width, image.height, width, height,
              REXCVAR_GET(ffns_fsr_sharpness));
  }
  return true;
}

void FsrScanOutLocked(VideoState &s, const plume::RenderViewport &viewport, plume::RenderFormat scan_out_format,
                      float param0, float param1) {
  FsrState &f = g_fsr;
  FsrTarget *t = f.current;
  auto it = f.rcas.find(uint32_t(scan_out_format));
  if (!t || it == f.rcas.end() || !it->second)
    return;
  plume::RenderCommandList *cmd = s.command_list;
  cmd->setViewports(viewport);
  cmd->setScissors(plume::RenderRect(int32_t(viewport.x), int32_t(viewport.y), int32_t(viewport.x + viewport.width),
                                     int32_t(viewport.y + viewport.height)));
  cmd->setGraphicsPipelineLayout(s.utility_layout.get());
  cmd->setPipeline(it->second.get());
  cmd->setGraphicsDescriptorSet(s.texture_set.get(), 0);
  cmd->setGraphicsDescriptorSet(s.sampler_set.get(), 1);
  UtilityPush push{};
  push.scale[0] = 1.0f / float(t->width);
  push.scale[1] = 1.0f / float(t->height);
  const int32_t hundredths = REXCVAR_GET(ffns_fsr_sharpness);
  push.translate[0] = float(std::exp2(-double(hundredths < 0 ? 0 : hundredths) / 100.0));
  push.texture_index = t->slot;
  push.sampler_index = kPointClampSamplerSlot;
  push.param0 = param0;
  push.param1 = param1;
  cmd->setGraphicsPushConstants(0, &push, 0, sizeof(push));
  cmd->drawInstanced(3, 1, 0, 0);
  f.current = nullptr;
}

}  // namespace renut::gpu
