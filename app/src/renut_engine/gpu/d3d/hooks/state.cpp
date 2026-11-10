/**
 * @file    gpu/d3d/hooks/state.cpp
 * @brief   N&B's D3D state calls, hooked with the native ports (d3d_native.h).
 *
 * The render and sampler state setters are the ones N&B's state tables name
 * (D3D::InitializeApiState puts them into every device's dispatch table,
 * which the game's inline SetRenderState / SetSamplerState call through).
 * The rest are D3DDevice_Set* and the CDevice internals that write the ring.
 */
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <vector>

#include <rex/cvar.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>
#include "renut_engine/gpu/log.h"

#include <rex/hook.h>

#include "renut_engine/gpu/banjo_renut_select.h"
#include <rex/types.h>

#include "renut_engine/gpu/d3d/d3d.h"
#include "renut_engine/gpu/d3d/d3d_native.h"
#include "renut_engine/gpu/guest/guest_memory.h"

using namespace renut::gpu;

// k241 counters (logged with the vertical-blank counters, ffxiii2_renut_glue.cpp): [0] headers checked, [1] colour
// slots read with other contents than bound, [2] depth slots, [3] copies made (every rule). Defined at the end.
extern std::atomic<uint32_t> g_ffxiii2_late_header[4];

// FFXIII-2-Native-Switch P6 (2026-11-09): hooks/draw.cpp (the draw chains diagnostic).
extern std::atomic<uint32_t> g_ffns_diag_depth;

namespace {

template <u32 State>
void SetRenderStateHook(u32 device, u32 value) {
  native::SetRenderState(native::DeviceAt(device), State, value);
}

template <u32 State>
void SetSamplerStateHook(u32 device, u32 sampler, u32 value) {
  native::SetSamplerState(native::DeviceAt(device), sampler, State, value);
}

template <u32 State>
u32 GetRenderStateHook(u32 device) {
  return native::GetRenderState(native::DeviceAt(device), State);
}

template <u32 State>
u32 GetSamplerStateHook(u32 device, u32 sampler) {
  return native::GetSamplerState(native::DeviceAt(device), sampler, State);
}

// The device's AddRef of a resource it hands out (D3DResource_AddRef,
// resource.cpp).
u32 AddRefResource(u32 resource) {
  return native::AddRefResource(resource);
}

// D3DSurface *GetRenderTarget(D3DDevice*, DWORD Index) [0x8264D8D0]
u32 D3DDevice_GetRenderTarget_hook(u32 device, u32 index) {
  const native::Device d = native::DeviceAt(device);
  const u32 surface = d && index < 4 ? d.W(native::off::RenderTargets + index * 4) : 0;
  if (surface)
    AddRefResource(surface);
  return surface;
}

// D3DSurface *GetDepthStencilSurface(D3DDevice*) [0x8264D910]
u32 D3DDevice_GetDepthStencilSurface_hook(u32 device) {
  const native::Device d = native::DeviceAt(device);
  const u32 surface = d ? d.W(native::off::DepthStencil) : 0;
  if (surface)
    AddRefResource(surface);
  return surface;
}

// D3DVertexBuffer *GetStreamSource(D3DDevice*, UINT StreamNumber,
//     UINT *pOffsetInBytes, UINT *pStride) [0x8264D840]: the offset is the
// stream's fetch constant address less the buffer's.
u32 D3DDevice_GetStreamSource_hook(u32 device, u32 stream, mapped_u32 offset, mapped_u32 stride) {
  const native::Device d = native::DeviceAt(device);
  if (!d || stream >= 16)
    return 0;
  const u32 buffer = d.W(native::off::StreamSources + stream * 4);
  u32 bytes = 0;
  if (buffer) {
    const u32 fetch = d.W(native::off::TextureFetch + (95 - stream) * 8);
    bytes = fetch - native::GpuAddress(GuestPtr<const be_u32>(buffer)[6]);
    AddRefResource(buffer);
  }
  if (offset)
    *offset = bytes;
  if (stride)
    *stride = u32(d.B(native::off::StreamStrides + stream)) << 2;
  return buffer;
}

// void D3DDevice_GetViewport(D3DDevice*, D3DVIEWPORT9*) [0x82276AC0]
void D3DDevice_GetViewport_hook(u32 device, mapped_u32 viewport) {
  const native::Device d = native::DeviceAt(device);
  if (!d || !viewport)
    return;
  for (u32 i = 0; i < 4; ++i)  // fctidz, the low word
    viewport[i] = u32(native::TruncateToI64(d.F(0x32E0 + i * 4)));
  viewport[4] = d.W(0x32F0);
  viewport[5] = d.W(0x32F4);
}

// void D3DDevice_SetClipPlane(D3DDevice*, DWORD Index, const float *pPlane) [0x8264DAE8]
void D3DDevice_SetClipPlane_hook(u32 device, u32 index, mapped_u32 plane) {
  native::Device d = native::DeviceAt(device);
  if (!d || !plane || index >= 6)
    return;
  for (u32 i = 0; i < 4; ++i)
    d.SetW(0x2820 + index * 16 + i * 4, plane[i]);
  d.Dirty(4, u64(1) << (54 - index));
}

// void D3DDevice_SetVertexFetchConstant(D3DDevice*, UINT VertexFetchRegister,
//     D3DVertexBuffer*, UINT Offset) [0x8264DB38]: a vertex fetch slot of the
// 3 in a fetch constant, made a vertex fetch constant first.
void D3DDevice_SetVertexFetchConstant_hook(u32 device, u32 reg, u32 vertex_buffer, u32 offset) {
  native::Device d = native::DeviceAt(device);
  const auto *vb = vertex_buffer ? GuestPtr<const be_u32>(vertex_buffer) : nullptr;
  if (!d || !vb || reg > 0x5F)
    return;
  const u32 slot = (0x5F - reg) / 3;
  const u32 part = (0x5F - reg) - slot * 3;
  const u32 constant = native::off::TextureFetch + slot * 24;
  const u32 type = d.W(constant) & 3;
  if (type == 0 || type == 2)
    std::memset(d.bytes + constant, 0, 24);
  d.SetW(constant, native::Insert(d.W(constant), 1, 0, 2));
  d.Dirty(3, u64(1) << (31 - slot));
  d.SetW(constant + part * 8, native::GpuAddress(u32(vb[6]) + offset));
  d.SetW(constant + part * 8 + 4, native::Insert(u32(vb[7]) - offset, 1, 28, 2));
}

// The boolean and integer shader constants (D3DDevice_Set{Vertex,Pixel}
// ShaderConstant{B,I}): bits from 0x2780 (vertex) / 0x2790 (pixel), a word
// of x, y, z bytes each from 0x27A0 / 0x27E0.
void SetShaderConstantsB(u32 device, u32 base, u32 start, mapped_u32 data, u32 count) {
  native::Device d = native::DeviceAt(device);
  if (!d || !data)
    return;
  for (u32 i = 0; i < count; ++i, ++start) {
    const u32 word = base + (start >> 5) * 4;
    const u32 bit = 1u << (start & 31);
    d.SetW(word, (d.W(word) & ~bit) | ((u32(data[i]) & 1) << (start & 31)));
  }
  d.Dirty(4, u64(1) << 56);
}

void SetShaderConstantsI(u32 device, u32 base, u32 start, mapped_u32 data, u32 count) {
  native::Device d = native::DeviceAt(device);
  if (!d || !data)
    return;
  for (u32 i = 0; i < count; ++i) {
    const u32 x = u32(data[i * 4]) & 0xFF, y = u32(data[i * 4 + 1]) & 0xFF;
    const u32 z = u32(data[i * 4 + 2]) & 0xFF;
    d.SetW(base + (start + i) * 4, (z << 16) | (y << 8) | x);
  }
  d.Dirty(4, u64(1) << 56);
}

// void D3DDevice_SetVertexShaderConstantB(D3DDevice*, UINT StartRegister,
//     const BOOL*, UINT BoolCount) [0x82221930], and the others alike.
void D3DDevice_SetVertexShaderConstantB_hook(u32 device, u32 start, mapped_u32 data, u32 count) {
  SetShaderConstantsB(device, 0x2780, start, data, count);
}
void D3DDevice_SetPixelShaderConstantB_hook(u32 device, u32 start, mapped_u32 data, u32 count) {
  SetShaderConstantsB(device, 0x2790, start, data, count);
}
void D3DDevice_SetVertexShaderConstantI_hook(u32 device, u32 start, mapped_u32 data, u32 count) {
  SetShaderConstantsI(device, 0x27A0, start, data, count);
}
void D3DDevice_SetPixelShaderConstantI_hook(u32 device, u32 start, mapped_u32 data, u32 count) {
  SetShaderConstantsI(device, 0x27E0, start, data, count);
}

// void D3DDevice_EndIndexedVertices(D3DDevice*) [0x826561F8]: the inline end
// of BeginIndexedVertices, back to the ring pointer it saved.
void D3DDevice_EndIndexedVertices_hook(u32 device) {
  native::Device d = native::DeviceAt(device);
  if (d)
    d.SetW(native::off::Ring, d.W(0x3614));
}

// void D3DDevice_SetTexture(D3DDevice*, DWORD Sampler, D3DBaseTexture*,
//     UINT64 PendingMask3) [0x8222F410]; the mask is the sampler's fetch
// constant bit, which the port derives from Sampler.
void D3DDevice_SetTexture_hook(u32 device, u32 sampler, u32 texture) {
  native::SetTexture(native::DeviceAt(device), sampler, texture);
}

// void D3DDevice_SetStreamSource(D3DDevice*, UINT StreamNumber, D3DVertexBuffer*,
//     UINT OffsetInBytes, UINT Stride, UINT64 PendingMask3) [0x8222A408]
void D3DDevice_SetStreamSource_hook(u32 device, u32 stream, u32 vertex_buffer, u32 offset,
                                    u32 stride) {
  native::SetStreamSource(native::DeviceAt(device), stream, vertex_buffer, offset, stride);
}

// void D3DDevice_SetIndices(D3DDevice*, D3DIndexBuffer*) [0x8220EE38]
void D3DDevice_SetIndices_hook(u32 device, u32 index_buffer) {
  native::SetIndices(native::DeviceAt(device), index_buffer);
}

// void D3DDevice_SetVertexDeclaration(D3DDevice*, D3DVertexDeclaration*) [0x82205700]
void D3DDevice_SetVertexDeclaration_hook(u32 device, u32 declaration) {
  native::SetVertexDeclaration(native::DeviceAt(device), declaration);
}

// void D3DDevice_SetViewport(D3DDevice*, const D3DVIEWPORT9*) [0x82222EA0]
void D3DDevice_SetViewport_hook(u32 device, u32 viewport) {
  native::SetViewport(native::DeviceAt(device), viewport);
}

// void D3D::SetViewport(D3DDevice*, float X, float Y, float Width, float Height,
//     float MinZ, float MaxZ) [0x82222F20]
void D3D_SetViewport_hook(u32 device, float x, float y, float width, float height, float min_z,
                          float max_z) {
  native::SetViewport(native::DeviceAt(device), x, y, width, height, min_z, max_z);
}

// void D3DDevice_SetScissorRect(D3DDevice*, const RECT*) [0x82223178]
void D3DDevice_SetScissorRect_hook(u32 device, mapped_u32 rect) {
  if (!rect)
    return;
  native::SetScissorRect(native::DeviceAt(device), i32(u32(rect[0])), i32(u32(rect[1])),
                         i32(u32(rect[2])), i32(u32(rect[3])));
}

// void D3D::SetSurfaceClip(CDevice*, int, int, int, int) [0x82223270]: the
// window scissor and per-tile clip packets, nothing else.
void D3D_SetSurfaceClip_hook(u32 /*device*/, u32, u32, u32, u32) {}

// ffxiii2-nx (2026-11-03): FFXIII-2 binds surface headers it builds on a thread's stack (0x7xxxxxxx: vis10's resolve
// trace had sources 7014F650 / 7014F7A0 / 7014F940 of 7174x8099, 1x1, 6995x963): the renderer reads a target's header
// on its own thread, after the game's function returned and the stack was reused. Such a header is copied into a guest
// heap copy, one per distinct header (the descriptor words and Common), and the copy is bound instead: same EDRAM base,
// format and size, the same host surface. Headers elsewhere are bound as they are.
REXCVAR_DEFINE_BOOL(ffxiii2_renut_stable_stack_surfaces, true, "FFXIII2",
                    "Renut only: surface headers on a guest stack are bound as heap copies (the renderer reads them later)");
// ffxiii2-nx (2026-11-06, D-003 / the shadow cascades): FF also rewrites heap headers in place between bindings: the
// k206 [bindlog] has 6CB07E60 (the scene's depth, 640 pitch 2x MSAA, info 0A010280) and 6CB07EB0 (the scene's colour)
// rewritten as 1024x1024 shadow cascade depth (info 10000410), bound, drawn into, then rewritten back to the scene's
// layout - all before the render thread reads the header for those draws. The console's D3D takes the registers from
// the header at bind time, so the cascade draws land in the cascade; the renderer read the scene's layout and drew
// them into the scene's depth target (k203 [rtlog]: 2,305 of 3,000 battle frames). With this key on, a heap header
// bound with other contents than at its first binding is bound as a copy (one per distinct contents, as the stack
// headers above): the contents at bind time, as on the console. Off: as before.
// Default on since 2026-11-06 12:00 (the user: "You have the privilage to fix every bug"; k210: both cascades right in
// every battle frame, no seam).
REXCVAR_DEFINE_BOOL(ffxiii2_renut_stable_rewritten_surfaces, true, "FFXIII2",
                    "Renut only: a surface header the game rewrote since its first binding is bound as a heap copy of "
                    "its contents at bind time (FF reuses its scene headers for the shadow cascades and the bloom "
                    "chain), as the console's D3D reads it at bind time. Off: the header as the render thread finds it "
                    "later (the shadow cascades drawn into the scene's depth)");
// ffxiii2-nx (2026-11-06, the PC flicker: the user's video 2026-11-06 16-38-50.mp4, single frames with the 3D scene
// black, only sky, or black in part while the HUD stays): FF binds 6CB07E60 as the scene's depth (info 0A010280, a
// depth format; flicker_bind1 [bindlog]: 189 of 189 scene bindings) and rewrites it as the post chain's 1280x720 colour
// view (info 14000500, a colour format) right after - before the render thread reads the header for the scene's draws.
// GatherTargets drops a depth whose format reads as colour, so most frames drew the scene with no depth at all
// (flicker_skip1 [rtlog]: Z00000000/base 2D0), and the frames where the render thread came first used the real depth
// target, which those no-depth frames had not cleared or written (its contents from frames before): the scene black or
// cut. The height is the same (720), so ffxiii2_renut_stable_rewritten_surfaces did not copy it. With this key a depth
// binding of a heap header is always bound as a copy of its contents at bind time, as the console's D3D takes the depth
// registers at bind time; colour bindings stay as before (their copies split the tiled scene, k207-k209).
// k222-k224 (fix_depth1-6): with the copies the flicker is gone (0 single-frame drop-outs in 2,400 battle frames), but
// the scene's right tile then differs from the left (a seam at x 640: the smoke columns only in the left half, also
// with the smoke's depth test off); Renut's tiled scene with that depth bound is not right yet. Off by default.
// k229: the seam was D3D's own tiling check: TilingBoundSurfaces compares the bound surfaces with the ones BeginTiling
// bound by address, so a bound copy was "not the tiling surfaces" and SetViewport clamped the viewport to the copy's
// 640-wide tile (the smoke's vp 640x720 in the [drawlog]): the right tile drew nothing of those passes. The check now
// compares copies by the header they were made from (native::SurfaceOrigin). Default on again.
REXCVAR_DEFINE_BOOL(ffxiii2_renut_stable_depth_surfaces, true, "FFXIII2",
                    "Renut only: a depth binding is always a copy of the header at bind time (FF rewrites its scene "
                    "depth header as a colour view before the renderer reads it). Gives a seam at x 640 in the "
                    "battle (the tiled scene with that depth): test only. Off: as before");
// ffxiii2-nx (2026-11-07, k240, D-010: the Switch's opening-cutscene flicker, single frames with the 3D scene black and
// only the subtitle layer left). The other half of D-008: FF binds 6CB07E60 as the scene's depth (info 0A010280, a depth
// format) and, rewritten in place, as the post chain's 1280x720 colour view (info 14000500). The depth bindings are
// copies since k229, but its colour bindings were bound live (same height, 720, so stable_rewritten does not copy them).
// When the render thread reached a post-chain draw after the game had already rewritten the header back to the next
// frame's depth view, GatherTargets found a depth surface in the colour slot and no depth: [ffxiii2] draw exits #4 (469
// in one 10 s window of k239u), the composite draws lost while their resolves ran (k239u [rtlog]: ~20 of 2,599 intro
// frames; the game-side draw chains of those swaps were complete). The Switch's render thread lags the game further than
// the PC's, so the PC showed it rarely. With this key a header the game has bound with a depth format is also bound as
// a copy of its contents at bind time in a colour slot, as the console's D3D latches the registers at bind time. Colour
// headers never bound as depth (the tiled scene's 6CB07EB0, whose copies split the scene in k207-k209) stay live.
REXCVAR_DEFINE_BOOL(ffxiii2_renut_stable_dual_role_surfaces, true, "FFXIII2",
                    "Renut only: a surface header the game also binds as depth (FF's 6CB07E60, the scene depth and the "
                    "post chain's colour view) is bound as a copy of its contents at bind time in a colour slot too, "
                    "as the console latches it (D-010: post-chain draws lost, frames with the scene black on the "
                    "Switch). Off: as before");
// ffxiii2-nx (2026-11-07, k241, the audit's defect class C2 "headers read late"): D-007, D-008 and D-010 were three
// instances of one fault - the render thread reads a surface header the game has rewritten since binding it - each fixed
// by a rule for one case (another height, a depth slot, a header also bound as depth). With this key every heap header
// binding is a copy of its contents at bind time, in every slot: the console's D3D latches the registers at bind time
// for all of them. The copies' identity is kept: a copy is made per (header, contents) and native::SurfaceOrigin gives
// the header it was made from (the tiling check, k229); host targets are keyed by the EDRAM view, so a copy and its
// header use the same target. k207-k209's seam with copies of the tiled scene's colour header came before the k229
// origin check.
// k241 matrix (PC, ffxiii2_diag_late_headers): with the k240 rules the post chain's 6CB07E10 was still read late in a
// colour slot - bound as 320x180 (its first contents, so the height rule left it live), read as the 640x360 view the
// game rewrote it to: 270 reads per 10 s at the PC's normal pace, 1,647 with the render thread lagging as on the
// Switch (ffxiii2_diag_render_lag_us=1500); those draws went into the other view's target. With this key: 0 late reads
// (intro with lag, battle), draw exits 0, battle 0 flicker frames of 1,500 (k240: 4 flagged), 0 dark drop-outs, seam
// step at x 640 1.215 vs k240's 1.229 (no seam), 58 copies in all. Default on since k241.
REXCVAR_DEFINE_BOOL(ffxiii2_renut_stable_all_surfaces, true, "FFXIII2",
                    "Renut only: every surface header binding (colour and depth) is a copy of its contents at bind "
                    "time, as the console's D3D latches them (the general form of the stable_* keys). Off: the "
                    "stable_* rules above");
// ffxiii2-nx (2026-11-07, k241, diagnostic, the audit's state validator for C2): for headers bound live (not copied),
// the contents at their last binding in a colour slot and in the depth slot are kept, and the render thread compares the
// header it reads for a draw or a resolve with them: a difference is a header the game rewrote after binding it and
// before the renderer used it ([ffxiii2] late header lines, counts with the vertical-blank counters). Off: no cost.
REXCVAR_DEFINE_BOOL(ffxiii2_diag_late_headers, false, "FFXIII2/Diag",
                    "Diagnostic: count and log surface headers the render thread reads with other contents than at "
                    "their binding (headers read late, defect class C2)");
// ffxiii2-nx (2026-11-06, the PC flicker, interim): what most frames drew, every frame. A depth binding of a heap header
// the game also binds as a colour target of the same height (FF's 6CB07E60: the scene depth, then the post chain's
// 1280x720 colour view) is bound as no depth, so the renderer no longer depends on whether it reads the header before
// or after the game rewrites it. Before, the render thread read the colour view in about 70 % of frames (no depth: the
// frames that looked right) and the depth in the rest (the black, sky-only and half-black frames of the user's video).
// The shadow cascades (the same header at 1024 rows) are not affected. Not the console's behaviour (it draws the scene
// with that depth): the right fix is ffxiii2_renut_stable_depth_surfaces once the tiled scene draws right with it.
// k229: off by default (the user, save 15 in New Bodhum: Serah not drawn - the field scene needs that depth).
REXCVAR_DEFINE_BOOL(ffxiii2_renut_scene_depth_as_drawn, false, "FFXIII2",
                    "Renut only: FF's scene depth header, which it rewrites as a colour view after binding it, is "
                    "bound as no depth in every frame (as the renderer read it in most frames) instead of depending "
                    "on the timing: no black frames in the battle. Off: as before");
namespace {
std::mutex g_colour_bound_mutex;
std::map<u32, u32> g_colour_bound_height;  // ffxiii2_renut_scene_depth_as_drawn: heap headers bound as colour, height
void NoteColourBinding(u32 surface) {
  if (!surface || (surface >= 0x70000000u && surface < 0x80000000u))
    return;
  const auto *w = GuestPtr<const be_u32>(surface);
  if (!w)
    return;
  std::lock_guard lock(g_colour_bound_mutex);
  g_colour_bound_height[surface] = (u32(w[9]) >> 3) & 0x7FFF;
}
// True for a depth binding to leave out (ffxiii2_renut_scene_depth_as_drawn).
bool SceneDepthAsDrawn(u32 surface) {
  static const bool on = REXCVAR_GET(ffxiii2_renut_scene_depth_as_drawn);
  if (!on || !surface || (surface >= 0x70000000u && surface < 0x80000000u))
    return false;
  const auto *w = GuestPtr<const be_u32>(surface);
  if (!w)
    return false;
  std::lock_guard lock(g_colour_bound_mutex);
  const auto it = g_colour_bound_height.find(surface);
  if (it == g_colour_bound_height.end() || it->second != ((u32(w[9]) >> 3) & 0x7FFF))
    return false;
  static uint32_t logged = 0;
  if (logged++ < 4)
    RGPU_INFO("[ffxiii2] depth {:08X} (also a colour target of the same height) bound as no depth "
              "(ffxiii2_renut_scene_depth_as_drawn)", surface);
  return true;
}
std::mutex g_stack_surface_mutex;
std::map<u32, u32> g_copy_origin;  // copy -> the header it was made from (native::SurfaceOrigin)
// k241: [0] and [1..6] the header's words 0 and 6-11, [7] the header a heap copy was made from (0 for stack headers,
// which are shared by contents: the stack is reused). Before k241 heap copies were keyed by contents alone, so two
// headers with the same contents shared a copy and g_copy_origin named whichever was bound last (audit R4).
std::map<std::array<u32, 8>, u32> g_stack_surfaces;
std::map<u32, std::array<u32, 8>> g_surface_home;  // ffxiii2_renut_stable_rewritten_surfaces: contents at first bind
std::set<u32> g_depth_format_headers;  // ffxiii2_renut_stable_dual_role_surfaces: headers bound with a depth format
// ffxiii2_diag_late_headers: a live header's words (as the key above, [7] unused) at its last binding, by VA | slot << 32
// (slot 1 = depth).
std::map<uint64_t, std::array<u32, 8>> g_live_bound;
bool LateHeadersOn() {
  static const bool on = REXCVAR_GET(ffxiii2_diag_late_headers);
  return on;
}
bool HeaderKey(u32 surface, std::array<u32, 8> &key) {
  const auto *w = surface ? GuestPtr<const be_u32>(surface) : nullptr;
  if (!w)
    return false;
  key[0] = w[0];
  for (u32 i = 0; i < 6; ++i)
    key[1 + i] = w[6 + i];
  key[7] = 0;
  return true;
}
// ffxiii2_diag_late_headers: a binding that was not copied (bound == surface).
void NoteLiveBinding(u32 surface, u32 bound, bool depth_slot) {
  if (!LateHeadersOn() || !surface || bound != surface)
    return;
  std::array<u32, 8> key;
  if (!HeaderKey(surface, key))
    return;
  std::lock_guard lock(g_stack_surface_mutex);
  g_live_bound[uint64_t(surface) | (uint64_t(depth_slot) << 32)] = key;
}
}  // namespace
u32 Ffxiii2StableSurface(u32 surface, bool depth_slot = false) {
  const bool stack = surface >= 0x70000000u && surface < 0x80000000u;
  static const bool stable_depth = REXCVAR_GET(ffxiii2_renut_stable_depth_surfaces);
  static const bool dual_role = REXCVAR_GET(ffxiii2_renut_stable_dual_role_surfaces);
  static const bool stable_all = REXCVAR_GET(ffxiii2_renut_stable_all_surfaces);
  // k240 (D-010): a colour binding of a header the game has bound with a depth format is a bind-time copy too.
  bool dual = false;
  if (dual_role && surface && !stack) {
    if (const auto *desc = GuestPtr<const d3d::SurfaceDesc>(surface + sizeof(d3d::Resource))) {
      const bool depth_now = d3d::IsDepthFormat(desc->Format);
      std::lock_guard lock(g_stack_surface_mutex);
      if (depth_now)
        g_depth_format_headers.insert(surface);
      else
        dual = !depth_slot && g_depth_format_headers.count(surface) != 0;
    }
    if (dual) {
      static uint32_t dual_logged = 0;
      if (dual_logged++ < 4)
        RGPU_INFO("[ffxiii2] colour binding of {:08X}, also bound as depth: bound as a copy "
                  "(ffxiii2_renut_stable_dual_role_surfaces)", surface);
    }
  }
  const bool always = (depth_slot && stable_depth && !stack && surface) || dual || (stable_all && !stack && surface);
  if (!always && (stack ? !REXCVAR_GET(ffxiii2_renut_stable_stack_surfaces)
                        : (!surface || !REXCVAR_GET(ffxiii2_renut_stable_rewritten_surfaces))))
    return surface;
  const auto *w = GuestPtr<const be_u32>(surface);
  if (!w)
    return surface;
  std::array<u32, 8> key;
  key[0] = w[0];
  for (u32 i = 0; i < 6; ++i)
    key[1 + i] = w[6 + i];
  key[7] = 0;
  std::lock_guard lock(g_stack_surface_mutex);
  if (!stack && !always) {
    // Only another height (the size word's bits 3-17): the cascades are 1024 rows, the scene 720. FF also rebinds
    // these headers for its predicated tiling (640x720 2x MSAA tiles: other pitch, MSAA, EDRAM and width words, k209)
    // and with the format word alone changed (28280106 / 18280186, k208); the renderer's tiling is built on reading
    // those as the full screen (one target grown for tiling), and copies of them split the scene: a seam at x 640 in
    // k207-k209.
    auto [home, first] = g_surface_home.try_emplace(surface, key);
    if (first || ((key[4] >> 3) & 0x7FFF) == ((home->second[4] >> 3) & 0x7FFF))
      return surface;
    static uint32_t rewritten_logged = 0;
    if (rewritten_logged++ < 64)
      RGPU_INFO("[ffxiii2] surface {:08X} rewritten since its first binding ({:08X} {:08X} {:08X} {:08X} {:08X} {:08X} "
                "-> {:08X} {:08X} {:08X} {:08X} {:08X} {:08X}): bound as a copy "
                "(ffxiii2_renut_stable_rewritten_surfaces)", surface, home->second[1], home->second[2],
                home->second[3], home->second[4], home->second[5], home->second[6], key[1], key[2], key[3], key[4],
                key[5], key[6]);
  }
  if (!stack)
    key[7] = surface;  // k241 (R4): one copy per header and contents
  u32 &copy = g_stack_surfaces[key];
  if (!copy) {
    copy = rex::system::kernel_state()->memory()->SystemHeapAlloc(48, 0x20);
    g_ffxiii2_late_header[3].fetch_add(1, std::memory_order_relaxed);
    static uint32_t logged = 0;
    if (logged++ < 8)
      RGPU_INFO("[ffxiii2] stack surface {:08X} ({}x{}, format {:08X}) bound as {:08X}", surface,
                ((key[4] >> 18) & 0x3FFF) + 1, ((key[4] >> 3) & 0x7FFF) + 1, key[5], copy);
  }
  if (auto *dst = GuestPtr<u8>(copy))
    std::memcpy(dst, w, 48);
  g_copy_origin[copy] = surface;
  return copy;
}

// The file's functions are in an anonymous namespace: native::SurfaceOrigin is defined at the end of the file.
u32 Ffxiii2SurfaceOrigin(u32 va) {
  if (!va)
    return 0;
  std::lock_guard lock(g_stack_surface_mutex);
  const auto it = g_copy_origin.find(va);
  return it == g_copy_origin.end() ? va : it->second;
}

// k241 (ffxiii2_renut_stable_all_surfaces): the game destroys header 'origin': its copies are forgotten (a header made
// later at that VA gets copies of its own) and returned, for the destroy to release their host targets after the work
// queued before it (resource.cpp). Before, copies kept their host targets for the rest of the run.
void Ffxiii2TakeSurfaceCopies(u32 origin, std::vector<u32> &out) {
  out.clear();
  static const bool stable_all = REXCVAR_GET(ffxiii2_renut_stable_all_surfaces);
  if (!stable_all || !origin || (origin >= 0x70000000u && origin < 0x80000000u))
    return;
  std::lock_guard lock(g_stack_surface_mutex);
  for (auto it = g_stack_surfaces.begin(); it != g_stack_surfaces.end();) {
    if (it->first[7] == origin) {
      out.push_back(it->second);
      g_copy_origin.erase(it->second);
      it = g_stack_surfaces.erase(it);
    } else {
      ++it;
    }
  }
  g_surface_home.erase(origin);
  g_depth_format_headers.erase(origin);
  g_live_bound.erase(uint64_t(origin));
  g_live_bound.erase(uint64_t(origin) | (1ull << 32));
}

// ffxiii2_diag_late_headers, on the render thread: the header 'va' read for a draw or a resolve, in a colour slot or the
// depth slot, against its contents at its last live binding in that kind of slot.
void Ffxiii2CheckLateHeader(u32 va, bool depth_slot) {
  if (!LateHeadersOn() || !va)
    return;
  std::array<u32, 8> now;
  if (!HeaderKey(va, now))
    return;
  g_ffxiii2_late_header[0].fetch_add(1, std::memory_order_relaxed);
  std::array<u32, 8> bound;
  {
    std::lock_guard lock(g_stack_surface_mutex);
    const auto it = g_live_bound.find(uint64_t(va) | (uint64_t(depth_slot) << 32));
    if (it == g_live_bound.end())
      return;
    bound = it->second;
  }
  if (bound == now)
    return;
  g_ffxiii2_late_header[depth_slot ? 2 : 1].fetch_add(1, std::memory_order_relaxed);
  static std::atomic<uint32_t> logged{0};
  if (logged.fetch_add(1, std::memory_order_relaxed) < 48)
    RGPU_INFO("[ffxiii2] late header {:08X} ({} slot): bound {:08X} {:08X} {:08X} {:08X} {:08X} {:08X}, read "
              "{:08X} {:08X} {:08X} {:08X} {:08X} {:08X}", va, depth_slot ? "depth" : "colour", bound[1], bound[2],
              bound[3], bound[4], bound[5], bound[6], now[1], now[2], now[3], now[4], now[5], now[6]);
}

// ffxiii2-nx (2026-11-06, D-003): the [rtlog] has the second shadow cascade drawn with the scene's depth header bound
// (device +0x3220 = 6CB07E60) while the resolve after it reads the cascade's; the delta chain is faithful (verify: 0 of
// ~2.5 M draws differ), so the game thread's device holds it. This logs the binding calls themselves.
REXCVAR_DEFINE_INT32(ffxiii2_diag_bind_log_start_s, 0, "FFXIII2/Diag",
                     "Diagnostic: this many seconds after the first target binding, log the next "
                     "ffxiii2_diag_bind_log_count binding calls ([bindlog]: the call, raw and bound headers, the device's "
                     "depth before) (0 = off)");
REXCVAR_DEFINE_INT32(ffxiii2_diag_bind_log_count, 1500, "FFXIII2/Diag",
                     "Diagnostic: ffxiii2_diag_bind_log_start_s logs this many binding calls");
namespace {
std::atomic<int32_t> g_bind_logged{0};
bool BindLogOn() {
  static const int32_t at = REXCVAR_GET(ffxiii2_diag_bind_log_start_s);
  if (at <= 0)
    return false;
  static const auto start = std::chrono::steady_clock::now();
  if (std::chrono::steady_clock::now() - start < std::chrono::seconds(at))
    return false;
  return g_bind_logged.fetch_add(1, std::memory_order_relaxed) < REXCVAR_GET(ffxiii2_diag_bind_log_count);
}
u32 HeaderWord(u32 surface, u32 i) {
  const auto *w = surface ? GuestPtr<const be_u32>(surface) : nullptr;
  return w ? u32(w[i]) : 0;
}
}  // namespace

// void D3DDevice_SetRenderTarget(D3DDevice*, DWORD Index, D3DSurface*) [0x8227AB68]
void D3DDevice_SetRenderTarget_hook(u32 device, u32 index, u32 surface) {
  NoteColourBinding(surface);
  const u32 bound = Ffxiii2StableSurface(surface);
  NoteLiveBinding(surface, bound, false);
  if (BindLogOn())
    RGPU_INFO("[bindlog] RT{} {:08X}>{:08X} info {:08X} dev {:08X} depth {:08X}", index, surface, bound,
              HeaderWord(surface, 6), device, u32(native::DeviceAt(device).W(0x3220)));
  native::SetRenderTarget(native::DeviceAt(device), index, bound);
}

// ffxiii2-nx (2026-11-03): FF's sub_8284CE30 is no SetRenderTarget thunk (the hook map's delta match): it binds
// every target at once, (device, const D3DSurface *targets[5] = {depth, color 0..3}, flags). Taken as SetRenderTarget
// (index = the array's address, >= 4) it bound nothing, so every resolve had no source (device +0x3210 was 0) and no
// frame reached the front buffer. Its stores: +12816..+12828 the colour targets, +12832 the depth (then
// D3D::SetSurfaceInfo-like sub_8284C738 with it); with flags bit 0 and device byte 11068 bit 0x20 it also saves the set
// at +13100 (colour 0, or the depth without one) and +13104..+13120.
void Ffxiii2_SetRenderTargets_hook(u32 device, u32 targets_va, u32 flags) {
  const auto *t = GuestPtr<const be_u32>(targets_va);
  native::Device d = native::DeviceAt(device);
  if (!t || !d)
    return;
  const u32 depth = t[0];
  g_ffns_diag_depth.store(depth, std::memory_order_relaxed);  // FFXIII-2-Native-Switch P6: draw chains' key
  if (BindLogOn())
    RGPU_INFO("[bindlog] SET {:08X}>{:08X} info {:08X} c {:08X} {:08X} {:08X} {:08X} flags {:X} b {:02X} dev {:08X} "
              "depth {:08X} saved {:08X}", depth, Ffxiii2StableSurface(depth, true), HeaderWord(depth, 6), u32(t[1]),
              u32(t[2]), u32(t[3]), u32(t[4]), flags, d.B(11068), device, d.W(0x3220), d.W(13120));
  if ((flags & 1) && (d.B(11068) & 0x20)) {
    for (u32 i = 0; i < 4; ++i)
      d.SetW(13104 + i * 4, t[1 + i]);
    d.SetW(13120, depth);
    d.SetW(13100, u32(t[1]) ? u32(t[1]) : depth);
  }
  for (u32 i = 0; i < 4; ++i)
    NoteColourBinding(t[1 + i]);
  const u32 bound_depth = SceneDepthAsDrawn(depth) ? 0u : Ffxiii2StableSurface(depth, true);
  NoteLiveBinding(depth, bound_depth, true);
  native::SetDepthStencilSurface(d, bound_depth);
  for (u32 i = 0; i < 4; ++i) {
    const u32 bound = Ffxiii2StableSurface(t[1 + i]);
    NoteLiveBinding(t[1 + i], bound, false);
    native::SetRenderTarget(d, i, bound);
  }
}

// void D3DDevice_SetDepthStencilSurface(D3DDevice*, D3DSurface*) [0x8227AF40]
void D3DDevice_SetDepthStencilSurface_hook(u32 device, u32 surface) {
  g_ffns_diag_depth.store(surface, std::memory_order_relaxed);  // FFXIII-2-Native-Switch P6: draw chains' key
  const u32 bound = SceneDepthAsDrawn(surface) ? 0u : Ffxiii2StableSurface(surface, true);
  NoteLiveBinding(surface, bound, true);
  if (BindLogOn())
    RGPU_INFO("[bindlog] DS {:08X}>{:08X} info {:08X} dev {:08X} depth {:08X}", surface, bound, HeaderWord(surface, 6),
              device, u32(native::DeviceAt(device).W(0x3220)));
  native::SetDepthStencilSurface(native::DeviceAt(device), bound);
}

// void D3D::SetSurfaceInfo(CDevice*, D3DSurface*) [0x8227AEB0]
void D3D_SetSurfaceInfo_hook(u32 device, u32 surface) {
  native::SetSurfaceInfo(native::DeviceAt(device), surface);
}

// void D3DDevice_SetPredication(D3DDevice*, DWORD PredicationMask) [0x8222AA18]
void D3DDevice_SetPredication_hook(u32 device, u32 mask) {
  native::SetPredication(native::DeviceAt(device), mask);
}

// void D3DDevice_SetCommandBufferPredication(D3DDevice*, DWORD TilePredication,
//     DWORD RunPredication) [0x82658678]
void D3DDevice_SetCommandBufferPredication_hook(u32 device, u32 tile_predication,
                                                u32 run_predication) {
  native::SetCommandBufferPredication(native::DeviceAt(device), tile_predication,
                                      run_predication);
}

// void D3DDevice_SetShaderGPRAllocation(D3DDevice*, DWORD Flags,
//     DWORD VertexShaderCount, DWORD PixelShaderCount) [0x82220B20]
void D3DDevice_SetShaderGPRAllocation_hook(u32 device, u32 flags, u32 vertex_count,
                                           u32 pixel_count) {
  native::SetShaderGPRAllocation(native::DeviceAt(device), flags, vertex_count, pixel_count);
}

// void D3D::CleanupGprAllocationSetup(CDevice*) [0x8264E2C0]
void D3D_CleanupGprAllocationSetup_hook(u32 device) {
  native::CleanupGprAllocationSetup(native::DeviceAt(device));
}

// void D3DDevice_SetScreenExtentQueryMode(D3DDevice*, D3DSCREENEXTENTQUERYMODE)
// [0x82658988]
void D3DDevice_SetScreenExtentQueryMode_hook(u32 device, u32 mode) {
  native::SetScreenExtentQueryMode(native::DeviceAt(device), mode);
}

// void D3DDevice_UnsetAll(D3DDevice*) [0x8264E1A8]
void D3DDevice_UnsetAll_hook(u32 device) {
  native::UnsetAll(native::DeviceAt(device));
}

// void D3D::ResetAllState(CDevice*, BOOL) [0x82287D38]
void D3D_ResetAllState_hook(u32 device, u32 /*skip_packets*/) {
  native::ResetAllState(native::DeviceAt(device));
}

// void D3D::InvalidateConstants(D3DDevice*) [0x8264E128]
void D3D_InvalidateConstants_hook(u32 device) {
  native::InvalidateConstants(native::DeviceAt(device));
}

// void D3DDevice_BlockUntilIdle(D3DDevice*) [0x82657488] (IDA: the second
// D3D::InvalidateConstants): waits for the device's current fence (N&B calls
// it before releasing a device).
void D3DDevice_BlockUntilIdle_hook(u32 device) {
  native::InvalidateConstantsAndWait(native::DeviceAt(device));
}

} // namespace

// Render states, by their index in N&B's table (off_82E506F0).
BANJO_RENUT_HOOK(sub_8284A2C8, SetRenderStateHook<10>);  // D3DDevice_SetRenderState_ZEnable  // N&B sub_8264C140
BANJO_RENUT_HOOK(sub_8284A338, SetRenderStateHook<11>);  // D3DDevice_SetRenderState_ZFunc  // N&B sub_821FB7B0
BANJO_RENUT_HOOK(sub_8284A308, SetRenderStateHook<12>);  // D3DDevice_SetRenderState_ZWriteEnable  // N&B sub_8264C180
BANJO_RENUT_HOOK(sub_82849BD0, SetRenderStateHook<13>);  // D3DDevice_SetRenderState_FillMode  // N&B sub_8228F330
BANJO_RENUT_HOOK(sub_82849BA0, SetRenderStateHook<14>);  // D3DDevice_SetRenderState_CullMode  // N&B sub_8228F350
BANJO_RENUT_HOOK(sub_82849C38, SetRenderStateHook<15>);  // D3DDevice_SetRenderState_AlphaBlendEnable  // N&B sub_8222A718
BANJO_RENUT_HOOK(sub_82849FC8, SetRenderStateHook<16>);  // D3DDevice_SetRenderState_SeparateAlphaBlendEnable  // N&B sub_8222A690
BANJO_RENUT_HOOK(sub_8284A0F0, SetRenderStateHook<17>);  // D3DDevice_SetRenderState_BlendFactor  // N&B sub_8226E8E0
BANJO_RENUT_HOOK(sub_82849D58, SetRenderStateHook<18>);  // D3DDevice_SetRenderState_SrcBlend  // N&B sub_82230090
BANJO_RENUT_HOOK(sub_82849DE8, SetRenderStateHook<19>);  // D3DDevice_SetRenderState_DestBlend  // N&B sub_822307B8
BANJO_RENUT_HOOK(sub_82849CC8, SetRenderStateHook<20>);  // D3DDevice_SetRenderState_BlendOp  // N&B sub_82226380
BANJO_RENUT_HOOK(sub_82849EE8, SetRenderStateHook<21>);  // D3DDevice_SetRenderState_SrcBlendAlpha  // N&B sub_822672B8
BANJO_RENUT_HOOK(sub_82849F58, SetRenderStateHook<22>);  // D3DDevice_SetRenderState_DestBlendAlpha  // N&B sub_822208E8
BANJO_RENUT_HOOK(sub_82849E78, SetRenderStateHook<23>);  // D3DDevice_SetRenderState_BlendOpAlpha  // N&B sub_8221F5F8
BANJO_RENUT_HOOK(sub_82849C00, SetRenderStateHook<24>);  // D3DDevice_SetRenderState_AlphaTestEnable  // N&B sub_8264BE98
BANJO_RENUT_HOOK(sub_8284A060, SetRenderStateHook<25>);  // D3DDevice_SetRenderState_AlphaRef  // N&B sub_8264BF50
BANJO_RENUT_HOOK(sub_8284A0C0, SetRenderStateHook<26>);  // D3DDevice_SetRenderState_AlphaFunc  // N&B sub_8264BFB8
BANJO_RENUT_HOOK(sub_8284A370, SetRenderStateHook<27>);  // D3DDevice_SetRenderState_StencilEnable  // N&B sub_8264C1C0
BANJO_RENUT_HOOK(sub_8284A3B0, SetRenderStateHook<28>);  // D3DDevice_SetRenderState_TwoSidedStencilMode  // N&B sub_8264C200
BANJO_RENUT_HOOK(sub_8284A418, SetRenderStateHook<29>);  // D3DDevice_SetRenderState_StencilFail  // N&B sub_8264C268
BANJO_RENUT_HOOK(sub_8284A450, SetRenderStateHook<30>);  // D3DDevice_SetRenderState_StencilZFail  // N&B sub_8264C2A0
BANJO_RENUT_HOOK(sub_8284A488, SetRenderStateHook<31>);  // D3DDevice_SetRenderState_StencilPass  // N&B sub_8264C2D8
BANJO_RENUT_HOOK(sub_8284A3E8, SetRenderStateHook<32>);  // D3DDevice_SetRenderState_StencilFunc  // N&B sub_8264C238
BANJO_RENUT_HOOK(sub_8284A588, SetRenderStateHook<33>);  // D3DDevice_SetRenderState_StencilRef  // N&B sub_8264C3D8
BANJO_RENUT_HOOK(sub_8284A5A8, SetRenderStateHook<34>);  // D3DDevice_SetRenderState_StencilMask  // N&B sub_8264C3F8
BANJO_RENUT_HOOK(sub_8284A5C8, SetRenderStateHook<35>);  // D3DDevice_SetRenderState_StencilWriteMask  // N&B sub_8264C418
BANJO_RENUT_HOOK(sub_8284A4E8, SetRenderStateHook<36>);  // D3DDevice_SetRenderState_CCWStencilFail  // N&B sub_8264C338
BANJO_RENUT_HOOK(sub_8284A520, SetRenderStateHook<37>);  // D3DDevice_SetRenderState_CCWStencilZFail  // N&B sub_8264C370
BANJO_RENUT_HOOK(sub_8284A558, SetRenderStateHook<38>);  // D3DDevice_SetRenderState_CCWStencilPass  // N&B sub_8264C3A8
BANJO_RENUT_HOOK(sub_8284A4B8, SetRenderStateHook<39>);  // D3DDevice_SetRenderState_CCWStencilFunc  // N&B sub_8264C308
BANJO_RENUT_HOOK(sub_8284A5E8, SetRenderStateHook<40>);  // D3DDevice_SetRenderState_CCWStencilRef  // N&B sub_8264C438
BANJO_RENUT_HOOK(sub_8284A608, SetRenderStateHook<41>);  // D3DDevice_SetRenderState_CCWStencilMask  // N&B sub_8264C458
BANJO_RENUT_HOOK(sub_8284A628, SetRenderStateHook<42>);  // D3DDevice_SetRenderState_CCWStencilWriteMask  // N&B sub_8264C478
BANJO_RENUT_HOOK(sub_8284A648, SetRenderStateHook<43>);  // D3DDevice_SetRenderState_ClipPlaneEnable  // N&B sub_8228F370
BANJO_RENUT_HOOK(sub_8284A978, SetRenderStateHook<44>);  // D3DDevice_SetRenderState_PointSize  // N&B sub_8264C6C0
BANJO_RENUT_HOOK(sub_8284A9D0, SetRenderStateHook<45>);  // D3DDevice_SetRenderState_PointSizeMin  // N&B sub_8264C720
BANJO_RENUT_HOOK(sub_8284A968, SetRenderStateHook<46>);  // D3DDevice_SetRenderState_PointSpriteEnable  // N&B sub_8264C6B0
BANJO_RENUT_HOOK(sub_8284AA20, SetRenderStateHook<47>);  // D3DDevice_SetRenderState_PointSizeMax  // N&B sub_822687D0
BANJO_RENUT_HOOK(sub_8284A818, SetRenderStateHook<48>);  // D3DDevice_SetRenderState_MultiSampleAntiAlias  // N&B sub_8264C598
BANJO_RENUT_HOOK(sub_8284A848, SetRenderStateHook<49>);  // D3DDevice_SetRenderState_MultiSampleMask  // N&B sub_8264C5C8
BANJO_RENUT_HOOK(sub_8284C688, SetRenderStateHook<50>);  // D3DDevice_SetRenderState_ScissorTestEnable  // N&B sub_8264C4A8
BANJO_RENUT_HOOK(sub_8284A6A8, SetRenderStateHook<51>);  // D3DDevice_SetRenderState_SlopeScaleDepthBias  // N&B sub_8264C4C0
BANJO_RENUT_HOOK(sub_8284A770, SetRenderStateHook<52>);  // D3DDevice_SetRenderState_DepthBias  // N&B sub_821FB718
BANJO_RENUT_HOOK(sub_8284A868, SetRenderStateHook<53>);  // D3DDevice_SetRenderState_ColorWriteEnable  // N&B sub_821FB6E0
BANJO_RENUT_HOOK(sub_8284A8A8, SetRenderStateHook<54>);  // D3DDevice_SetRenderState_ColorWriteEnable1  // N&B sub_8264C5F0
BANJO_RENUT_HOOK(sub_8284A8E8, SetRenderStateHook<55>);  // D3DDevice_SetRenderState_ColorWriteEnable2  // N&B sub_8264C630
BANJO_RENUT_HOOK(sub_8284A928, SetRenderStateHook<56>);  // D3DDevice_SetRenderState_ColorWriteEnable3  // N&B sub_8264C670
BANJO_RENUT_HOOK(sub_8284B0F0, SetRenderStateHook<57>);  // D3DDevice_SetRenderState_TessellationMode  // N&B sub_8264CDC0
BANJO_RENUT_HOOK(sub_8284B080, SetRenderStateHook<58>);  // D3DDevice_SetRenderState_MinTessellationLevel  // N&B sub_8264CD50
BANJO_RENUT_HOOK(sub_8284B0B8, SetRenderStateHook<59>);  // D3DDevice_SetRenderState_MaxTessellationLevel  // N&B sub_8264CD88
BANJO_RENUT_HOOK(sub_8284AA70, SetRenderStateHook<60>);  // D3DDevice_SetRenderState_Wrap0  // N&B sub_8264C780
BANJO_RENUT_HOOK(sub_8284AA90, SetRenderStateHook<61>);  // D3DDevice_SetRenderState_Wrap1  // N&B sub_8264C7A0
BANJO_RENUT_HOOK(sub_8284AAB8, SetRenderStateHook<62>);  // D3DDevice_SetRenderState_Wrap2  // N&B sub_8264C7C8
BANJO_RENUT_HOOK(sub_8284AAE0, SetRenderStateHook<63>);  // D3DDevice_SetRenderState_Wrap3  // N&B sub_8264C7F0
BANJO_RENUT_HOOK(sub_8284AB08, SetRenderStateHook<64>);  // D3DDevice_SetRenderState_Wrap4  // N&B sub_8264C818
BANJO_RENUT_HOOK(sub_8284AB30, SetRenderStateHook<65>);  // D3DDevice_SetRenderState_Wrap5  // N&B sub_8264C840
BANJO_RENUT_HOOK(sub_8284AB58, SetRenderStateHook<66>);  // D3DDevice_SetRenderState_Wrap6  // N&B sub_8264C868
BANJO_RENUT_HOOK(sub_8284AB80, SetRenderStateHook<67>);  // D3DDevice_SetRenderState_Wrap7  // N&B sub_8264C890
BANJO_RENUT_HOOK(sub_8284ABA0, SetRenderStateHook<68>);  // D3DDevice_SetRenderState_Wrap8  // N&B sub_8264C8B0
BANJO_RENUT_HOOK(sub_8284ABC0, SetRenderStateHook<69>);  // D3DDevice_SetRenderState_Wrap9  // N&B sub_8264C8D0
BANJO_RENUT_HOOK(sub_8284ABE8, SetRenderStateHook<70>);  // D3DDevice_SetRenderState_Wrap10  // N&B sub_8264C8F8
BANJO_RENUT_HOOK(sub_8284AC10, SetRenderStateHook<71>);  // D3DDevice_SetRenderState_Wrap11  // N&B sub_8264C920
BANJO_RENUT_HOOK(sub_8284AC38, SetRenderStateHook<72>);  // D3DDevice_SetRenderState_Wrap12  // N&B sub_8264C948
BANJO_RENUT_HOOK(sub_8284AC60, SetRenderStateHook<73>);  // D3DDevice_SetRenderState_Wrap13  // N&B sub_8264C970
BANJO_RENUT_HOOK(sub_8284AC88, SetRenderStateHook<74>);  // D3DDevice_SetRenderState_Wrap14  // N&B sub_8264C998
BANJO_RENUT_HOOK(sub_8284ACB0, SetRenderStateHook<75>);  // D3DDevice_SetRenderState_Wrap15  // N&B sub_8264C9C0
BANJO_RENUT_HOOK(sub_8284ADD0, SetRenderStateHook<76>);  // D3DDevice_SetRenderState_ViewportEnable  // N&B sub_82205A80
BANJO_RENUT_HOOK(sub_8284AE20, SetRenderStateHook<77>);  // D3DDevice_SetRenderState_HighPrecisionBlendEnable  // N&B sub_8264CAF0
BANJO_RENUT_HOOK(sub_8284AEB8, SetRenderStateHook<78>);  // D3DDevice_SetRenderState_HighPrecisionBlendEnable1  // N&B sub_8264CB88
BANJO_RENUT_HOOK(sub_8284AF50, SetRenderStateHook<79>);  // D3DDevice_SetRenderState_HighPrecisionBlendEnable2  // N&B sub_8264CC20
BANJO_RENUT_HOOK(sub_8284AFE8, SetRenderStateHook<80>);  // D3DDevice_SetRenderState_HighPrecisionBlendEnable3  // N&B sub_8264CCB8
BANJO_RENUT_HOOK(sub_8284B128, SetRenderStateHook<81>);  // D3DDevice_SetRenderState_HalfPixelOffset  // N&B sub_8264CDF8
BANJO_RENUT_HOOK(sub_8284B160, SetRenderStateHook<82>);  // D3DDevice_SetRenderState_PrimitiveResetEnable  // N&B sub_8264CE30
BANJO_RENUT_HOOK(sub_8284B190, SetRenderStateHook<83>);  // D3DDevice_SetRenderState_PrimitiveResetIndex  // N&B sub_8264CE60
BANJO_RENUT_HOOK(sub_8284B1B8, SetRenderStateHook<84>);  // D3DDevice_SetRenderState_AlphaToMaskEnable  // N&B sub_8264CE88
BANJO_RENUT_HOOK(sub_8284B1E8, SetRenderStateHook<85>);  // D3DDevice_SetRenderState_AlphaToMaskOffsets  // N&B sub_8264CEB8
BANJO_RENUT_HOOK(sub_8284B210, SetRenderStateHook<86>);  // D3DDevice_SetRenderState_GuardBand_X  // N&B sub_8264CEF0
BANJO_RENUT_HOOK(sub_8284B240, SetRenderStateHook<87>);  // D3DDevice_SetRenderState_GuardBand_Y  // N&B sub_8264CF20
BANJO_RENUT_HOOK(sub_8284B270, SetRenderStateHook<88>);  // D3DDevice_SetRenderState_DiscardBand_X  // N&B sub_8264CF50
BANJO_RENUT_HOOK(sub_8284B298, SetRenderStateHook<89>);  // D3DDevice_SetRenderState_DiscardBand_Y  // N&B sub_8264CF78
BANJO_RENUT_HOOK(sub_8284B360, SetRenderStateHook<90>);  // D3DDevice_SetRenderState_HiStencilEnable  // N&B sub_8264CFE8
BANJO_RENUT_HOOK(sub_8284B390, SetRenderStateHook<91>);  // D3DDevice_SetRenderState_HiStencilWriteEnable  // N&B sub_8264D018
BANJO_RENUT_HOOK(sub_8284B3C0, SetRenderStateHook<92>);  // D3DDevice_SetRenderState_HiStencilFunc  // N&B sub_8264D048
BANJO_RENUT_HOOK(sub_8284B3F0, SetRenderStateHook<93>);  // D3DDevice_SetRenderState_HiStencilRef  // N&B sub_8264D078
BANJO_RENUT_HOOK(sub_8284B410, SetRenderStateHook<94>);  // D3DDevice_SetRenderState_PresentInterval  // N&B sub_8264D098
BANJO_RENUT_HOOK(sub_8284B420, SetRenderStateHook<95>);  // D3DDevice_SetRenderState_PresentImmediateThreshold  // N&B sub_8264D0A8
BANJO_RENUT_HOOK(sub_8284B2C8, SetRenderStateHook<96>);  // D3DDevice_SetRenderState_HiZEnable  // N&B sub_8264CFA8
BANJO_RENUT_HOOK(sub_8284B2F8, SetRenderStateHook<97>);  // D3DDevice_SetRenderState_HiZWriteEnable  // N&B sub_822899D0
BANJO_RENUT_HOOK(sub_8284A1F0, SetRenderStateHook<98>);  // D3DDevice_SetRenderState_LastPixel  // N&B sub_8264C060
BANJO_RENUT_HOOK(sub_8284A228, SetRenderStateHook<99>);  // D3DDevice_SetRenderState_LineWidth  // N&B sub_8264C098
BANJO_RENUT_HOOK(sub_8284A298, SetRenderStateHook<100>);  // D3DDevice_SetRenderState_Buffer2Frames  // N&B sub_8264C110

// Sampler states, by their index in N&B's table (off_82E50BD0).
BANJO_RENUT_HOOK(sub_8284BBB8, SetSamplerStateHook<0>);  // D3DDevice_SetSamplerState_AddressU  // N&B sub_8227D2D0
BANJO_RENUT_HOOK(sub_8284BC08, SetSamplerStateHook<1>);  // D3DDevice_SetSamplerState_AddressV  // N&B sub_8220C4E0
BANJO_RENUT_HOOK(sub_8284BC58, SetSamplerStateHook<2>);  // D3DDevice_SetSamplerState_AddressW  // N&B sub_8264D630
BANJO_RENUT_HOOK(sub_8284BB48, SetSamplerStateHook<3>);  // D3DDevice_SetSamplerState_BorderColor  // N&B sub_8264D590
BANJO_RENUT_HOOK(sub_8284B5E8, SetSamplerStateHook<4>);  // D3DDevice_SetSamplerState_MagFilter  // N&B sub_8222F1B8
BANJO_RENUT_HOOK(sub_8284B440, SetSamplerStateHook<5>);  // D3DDevice_SetSamplerState_MinFilter  // N&B sub_8222F280
BANJO_RENUT_HOOK(sub_8284B788, SetSamplerStateHook<6>);  // D3DDevice_SetSamplerState_MipFilter  // N&B sub_8228F2F0
BANJO_RENUT_HOOK(sub_8284B9A8, SetSamplerStateHook<7>);  // D3DDevice_SetSamplerState_MipMapLodBias  // N&B sub_8264D460
BANJO_RENUT_HOOK(sub_8284BA48, SetSamplerStateHook<8>);  // D3DDevice_SetSamplerState_MaxMipLevel  // N&B sub_8222F590
BANJO_RENUT_HOOK(sub_8284B888, SetSamplerStateHook<9>);  // D3DDevice_SetSamplerState_MaxAnisotropy  // N&B sub_8264D340
BANJO_RENUT_HOOK(sub_8284B6E8, SetSamplerStateHook<10>);  // D3DDevice_SetSamplerState_MagFilterZ  // N&B sub_8264D1E0
BANJO_RENUT_HOOK(sub_8284B540, SetSamplerStateHook<11>);  // D3DDevice_SetSamplerState_MinFilterZ  // N&B sub_8264D100
BANJO_RENUT_HOOK(sub_8284B7E0, SetSamplerStateHook<12>);  // D3DDevice_SetSamplerState_SeparateZFilterEnable  // N&B sub_8264D298
BANJO_RENUT_HOOK(sub_8284BAC8, SetSamplerStateHook<13>);  // D3DDevice_SetSamplerState_MinMipLevel  // N&B sub_8264D510
BANJO_RENUT_HOOK(sub_8284BCA8, SetSamplerStateHook<14>);  // D3DDevice_SetSamplerState_TrilinearThreshold  // N&B sub_8264D680
BANJO_RENUT_HOOK(sub_8284B900, SetSamplerStateHook<15>);  // D3DDevice_SetSamplerState_AnisotropyBias  // N&B sub_8264D3B8
BANJO_RENUT_HOOK(sub_8284BD00, SetSamplerStateHook<16>);  // D3DDevice_SetSamplerState_HGradientExpBias  // N&B sub_8264D6D8
BANJO_RENUT_HOOK(sub_8284BD58, SetSamplerStateHook<17>);  // D3DDevice_SetSamplerState_VGradientExpBias  // N&B sub_8264D730
BANJO_RENUT_HOOK(sub_8284BDB0, SetSamplerStateHook<18>);  // D3DDevice_SetSamplerState_WhiteBorderColorW  // N&B sub_8264D788
BANJO_RENUT_HOOK(sub_8284BE08, SetSamplerStateHook<19>);  // D3DDevice_SetSamplerState_PointBorderEnable  // N&B sub_8264D7E0

BANJO_RENUT_HOOK(sub_8284F548, D3DDevice_SetTexture_hook);  // D3DDevice_SetTexture  // N&B sub_8222F410
#if 0  // peggle2-nx / peggle1-nx / bejeweled3-nx: N&B sub_82B96538 (D3DDevice_SetTexture_Inline) is linked in none of Peggle 2, Peggle 1 and Bejeweled 3
BANJO_RENUT_UNHOOKED_HOOK(sub_82B96538, D3DDevice_SetTexture_hook);  // D3DDevice_SetTexture_Inline  // ffxiii2-nx: unhooked (out/ff_hook_map.txt)
#endif
BANJO_RENUT_HOOK(sub_8284BF60, D3DDevice_SetStreamSource_hook);  // D3DDevice_SetStreamSource  // N&B sub_8222A408
BANJO_RENUT_HOOK(sub_8284C080, D3DDevice_SetIndices_hook);  // D3DDevice_SetIndices  // ffxiii2-nx (2026-11-04): FF's SetIndices (writes device +0x320C)  // N&B sub_8220EE38
BANJO_RENUT_HOOK(sub_82849298, D3DDevice_SetVertexDeclaration_hook);  // D3DDevice_SetVertexDeclaration  // N&B sub_82205700
BANJO_RENUT_HOOK(sub_8284C698, D3DDevice_SetViewport_hook);  // D3DDevice_SetViewport  // N&B sub_82222EA0
BANJO_RENUT_HOOK(sub_8284C430, D3D_SetViewport_hook);  // D3D_SetViewport  // N&B sub_82222F20
BANJO_RENUT_HOOK(sub_8284BE68, D3DDevice_SetScissorRect_hook);  // D3DDevice_SetScissorRect  // N&B sub_82223178
BANJO_RENUT_HOOK(sub_828498B8, D3D_SetSurfaceClip_hook);  // D3D_SetSurfaceClip  // N&B sub_82223270
BANJO_RENUT_HOOK(sub_8284C7C8, D3DDevice_SetRenderTarget_hook);  // D3DDevice_SetRenderTarget  // N&B sub_8227AB68
BANJO_RENUT_HOOK(sub_8284CB58, D3DDevice_SetDepthStencilSurface_hook);  // D3DDevice_SetDepthStencilSurface  // N&B sub_8227AF40
BANJO_RENUT_HOOK(sub_8284C738, D3D_SetSurfaceInfo_hook);  // D3D_SetSurfaceInfo  // N&B sub_8227AEB0
BANJO_RENUT_UNHOOKED_HOOK(sub_8231BE38, D3DDevice_SetPredication_hook);  // D3DDevice_SetPredication  // N&B sub_8222AA18  // ffxiii2-nx: unhooked (out/ff_hook_map.txt)
#if 0  // bejeweled3-nx: N&B sub_82658678 (D3DDevice_SetCommandBufferPredication) is not linked in Bejeweled 3
BANJO_RENUT_UNHOOKED_HOOK(sub_82446398, D3DDevice_SetCommandBufferPredication_hook);  // D3DDevice_SetCommandBufferPredication  // N&B sub_82658678  // ffxiii2-nx: unhooked (out/ff_hook_map.txt)
#endif
BANJO_RENUT_HOOK(sub_82849460, D3DDevice_SetShaderGPRAllocation_hook);  // D3DDevice_SetShaderGPRAllocation  // N&B sub_82220B20
BANJO_RENUT_HOOK(sub_82848948, D3D_CleanupGprAllocationSetup_hook);  // D3D_CleanupGprAllocationSetup  // N&B sub_8264E2C0
BANJO_RENUT_UNHOOKED_HOOK(sub_82329F38, D3DDevice_SetScreenExtentQueryMode_hook);  // D3DDevice_SetScreenExtentQueryMode  // N&B sub_82658988  // ffxiii2-nx: unhooked (out/ff_hook_map.txt)
BANJO_RENUT_UNHOOKED_HOOK(sub_82321900, D3DDevice_UnsetAll_hook);  // D3DDevice_UnsetAll  // N&B sub_8264E1A8  // ffxiii2-nx: unhooked (out/ff_hook_map.txt)
BANJO_RENUT_HOOK(sub_8284C338, D3D_ResetAllState_hook);  // D3D_ResetAllState  // N&B sub_82287D38
#if 0  // bejeweled3-nx: N&B sub_8264E128 (D3D_InvalidateConstants) is not linked in Bejeweled 3
BANJO_RENUT_UNHOOKED_HOOK(sub_82443FC8, D3D_InvalidateConstants_hook);  // D3D_InvalidateConstants  // N&B sub_8264E128  // ffxiii2-nx: unhooked (out/ff_hook_map.txt)
#endif
BANJO_RENUT_HOOK(sub_82852A20, D3DDevice_BlockUntilIdle_hook);  // D3DDevice_BlockUntilIdle  // N&B sub_82657488
// Through a thunk (j_D3DDevice_SetRenderTarget 0x8264D8C8).
BANJO_RENUT_HOOK(sub_8284CE30, Ffxiii2_SetRenderTargets_hook);  // ffxiii2-nx: FF's set-all-targets (not N&B's thunk sub_8264D8C8)
// ffxiii2-nx (2026-11-04): FF's sub_8284C198 is no getter (a 1648-byte frame, a copy into device +15408 through
// sub_82853618): unhooked, like sub_8284C080 (SetIndices, which the map also had as a getter).
BANJO_RENUT_UNHOOKED_HOOK(sub_8284C198, D3DDevice_GetRenderTarget_hook);  // D3DDevice_GetRenderTarget  // N&B sub_8264D8D0
BANJO_RENUT_UNHOOKED_HOOK(sub_8230D8F0, D3DDevice_GetDepthStencilSurface_hook);  // D3DDevice_GetDepthStencilSurface  // N&B sub_8264D910  // ffxiii2-nx: unhooked (out/ff_hook_map.txt)
// ffxiii2-nx (2026-11-04): FF's sub_8284C080 is SetIndices (device, index buffer -> device +0x320C, the D3D index
// buffer word, with the old one's fence update), not GetStreamSource as the N&B map had it: the GetStreamSource hook
// wrote its outputs through r5/r6 and crashed the game after the title screen (write of 0x8215E9EC). FF's
// GetStreamSource is not found yet.
BANJO_RENUT_UNHOOKED_HOOK(sub_8284C080, D3DDevice_GetStreamSource_hook);  // D3DDevice_GetStreamSource  // N&B sub_8264D840
#if 0  // bejeweled3-nx: N&B sub_82276AC0 (D3DDevice_GetViewport) is not linked in Bejeweled 3
BANJO_RENUT_UNHOOKED_HOOK(sub_82436468, D3DDevice_GetViewport_hook);  // D3DDevice_GetViewport  // N&B sub_82276AC0  // ffxiii2-nx: unhooked (out/ff_hook_map.txt)
#endif
BANJO_RENUT_UNHOOKED_HOOK(sub_8230DAD0, D3DDevice_SetClipPlane_hook);  // D3DDevice_SetClipPlane  // N&B sub_8264DAE8  // ffxiii2-nx: unhooked (out/ff_hook_map.txt)
#if 0  // bejeweled3-nx: N&B sub_8264DB38 (D3DDevice_SetVertexFetchConstant) is not linked in Bejeweled 3
BANJO_RENUT_UNHOOKED_HOOK(sub_82436B20, D3DDevice_SetVertexFetchConstant_hook);  // D3DDevice_SetVertexFetchConstant  // N&B sub_8264DB38  // ffxiii2-nx: unhooked (out/ff_hook_map.txt)
#endif
BANJO_RENUT_HOOK(sub_82848C80, D3DDevice_SetVertexShaderConstantB_hook);  // D3DDevice_SetVertexShaderConstantB  // N&B sub_82221930
BANJO_RENUT_HOOK(sub_82848CE0, D3DDevice_SetPixelShaderConstantB_hook);  // D3DDevice_SetPixelShaderConstantB  // N&B sub_82268810
BANJO_RENUT_HOOK(sub_82848D40, D3DDevice_SetVertexShaderConstantI_hook);  // D3DDevice_SetVertexShaderConstantI  // N&B sub_8264E570
BANJO_RENUT_HOOK(sub_82848D98, D3DDevice_SetPixelShaderConstantI_hook);  // D3DDevice_SetPixelShaderConstantI  // N&B sub_8264E5C8
BANJO_RENUT_HOOK(sub_82852EB8, D3DDevice_EndIndexedVertices_hook);  // D3DDevice_EndIndexedVertices  // N&B sub_826561F8

// Render state getters, by their index in N&B's table (off_82E506F0, field 0).
BANJO_RENUT_HOOK(sub_8284A300, GetRenderStateHook<10>);  // D3DDevice_GetRenderState_ZEnable  // N&B sub_8264C178
BANJO_RENUT_HOOK(sub_8284A360, GetRenderStateHook<11>);  // D3DDevice_GetRenderState_ZFunc  // N&B sub_8264C1B0
BANJO_RENUT_HOOK(sub_8284A328, GetRenderStateHook<12>);  // D3DDevice_GetRenderState_ZWriteEnable  // N&B sub_8264C1A0
BANJO_RENUT_HOOK(sub_82849BF0, GetRenderStateHook<13>);  // D3DDevice_GetRenderState_FillMode  // N&B sub_8264BE88
BANJO_RENUT_HOOK(sub_82849BC0, GetRenderStateHook<14>);  // D3DDevice_GetRenderState_CullMode  // N&B sub_8264BE78
BANJO_RENUT_HOOK(sub_82849CB8, GetRenderStateHook<15>);  // D3DDevice_GetRenderState_AlphaBlendEnable  // N&B sub_8264BED0
BANJO_RENUT_HOOK(sub_8284A050, GetRenderStateHook<16>);  // D3DDevice_GetRenderState_SeparateAlphaBlendEnable  // N&B sub_8264BF40
BANJO_RENUT_HOOK(sub_8284A180, GetRenderStateHook<17>);  // D3DDevice_GetRenderState_BlendFactor  // N&B sub_8264BFE8
BANJO_RENUT_HOOK(sub_82849DD8, GetRenderStateHook<18>);  // D3DDevice_GetRenderState_SrcBlend  // N&B sub_8264BEF0
BANJO_RENUT_HOOK(sub_82849E68, GetRenderStateHook<19>);  // D3DDevice_GetRenderState_DestBlend  // N&B sub_8264BF00
BANJO_RENUT_HOOK(sub_82849D48, GetRenderStateHook<20>);  // D3DDevice_GetRenderState_BlendOp  // N&B sub_8264BEE0
BANJO_RENUT_HOOK(sub_82849F48, GetRenderStateHook<21>);  // D3DDevice_GetRenderState_SrcBlendAlpha  // N&B sub_8264BF20
BANJO_RENUT_HOOK(sub_82849FB8, GetRenderStateHook<22>);  // D3DDevice_GetRenderState_DestBlendAlpha  // N&B sub_8264BF30
BANJO_RENUT_HOOK(sub_82849ED8, GetRenderStateHook<23>);  // D3DDevice_GetRenderState_BlendOpAlpha  // N&B sub_8264BF10
BANJO_RENUT_HOOK(sub_82849C28, GetRenderStateHook<24>);  // D3DDevice_GetRenderState_AlphaTestEnable  // N&B sub_8264BEC0
BANJO_RENUT_HOOK(sub_8284A098, GetRenderStateHook<25>);  // D3DDevice_GetRenderState_AlphaRef  // N&B sub_8264BF88
BANJO_RENUT_HOOK(sub_8284A0E0, GetRenderStateHook<26>);  // D3DDevice_GetRenderState_AlphaFunc  // N&B sub_8264BFD8
BANJO_RENUT_HOOK(sub_8284A3A8, GetRenderStateHook<27>);  // D3DDevice_GetRenderState_StencilEnable  // N&B sub_8264C1F8
BANJO_RENUT_HOOK(sub_8284A3D8, GetRenderStateHook<28>);  // D3DDevice_GetRenderState_TwoSidedStencilMode  // N&B sub_8264C228
BANJO_RENUT_HOOK(sub_8284A440, GetRenderStateHook<29>);  // D3DDevice_GetRenderState_StencilFail  // N&B sub_8264C290
BANJO_RENUT_HOOK(sub_8284A478, GetRenderStateHook<30>);  // D3DDevice_GetRenderState_StencilZFail  // N&B sub_8264C2C8
BANJO_RENUT_HOOK(sub_8284A4A8, GetRenderStateHook<31>);  // D3DDevice_GetRenderState_StencilPass  // N&B sub_8264C2F8
BANJO_RENUT_HOOK(sub_8284A408, GetRenderStateHook<32>);  // D3DDevice_GetRenderState_StencilFunc  // N&B sub_8264C258
BANJO_RENUT_HOOK(sub_8284A5A0, GetRenderStateHook<33>);  // D3DDevice_GetRenderState_StencilRef  // N&B sub_8264C3F0
BANJO_RENUT_HOOK(sub_8284A5C0, GetRenderStateHook<34>);  // D3DDevice_GetRenderState_StencilMask  // N&B sub_8264C410
BANJO_RENUT_HOOK(sub_8284A5E0, GetRenderStateHook<35>);  // D3DDevice_GetRenderState_StencilWriteMask  // N&B sub_8264C430
BANJO_RENUT_HOOK(sub_8284A510, GetRenderStateHook<36>);  // D3DDevice_GetRenderState_CCWStencilFail  // N&B sub_8264C360
BANJO_RENUT_HOOK(sub_8284A548, GetRenderStateHook<37>);  // D3DDevice_GetRenderState_CCWStencilZFail  // N&B sub_8264C398
BANJO_RENUT_HOOK(sub_8284A578, GetRenderStateHook<38>);  // D3DDevice_GetRenderState_CCWStencilPass  // N&B sub_8264C3C8
BANJO_RENUT_HOOK(sub_8284A4D8, GetRenderStateHook<39>);  // D3DDevice_GetRenderState_CCWStencilFunc  // N&B sub_8264C328
BANJO_RENUT_HOOK(sub_8284A600, GetRenderStateHook<40>);  // D3DDevice_GetRenderState_CCWStencilRef  // N&B sub_8264C450
BANJO_RENUT_HOOK(sub_8284A620, GetRenderStateHook<41>);  // D3DDevice_GetRenderState_CCWStencilMask  // N&B sub_8264C470
BANJO_RENUT_HOOK(sub_8284A640, GetRenderStateHook<42>);  // D3DDevice_GetRenderState_CCWStencilWriteMask  // N&B sub_8264C490
BANJO_RENUT_HOOK(sub_8284A690, GetRenderStateHook<43>);  // D3DDevice_GetRenderState_ClipPlaneEnable  // N&B sub_8264C498
BANJO_RENUT_HOOK(sub_8284A9C0, GetRenderStateHook<44>);  // D3DDevice_GetRenderState_PointSize  // N&B sub_8264C710
BANJO_RENUT_HOOK(sub_8284AA10, GetRenderStateHook<45>);  // D3DDevice_GetRenderState_PointSizeMin  // N&B sub_8264C760
BANJO_RENUT_HOOK(sub_8284A970, GetRenderStateHook<46>);  // D3DDevice_GetRenderState_PointSpriteEnable  // N&B sub_8264C6B8
BANJO_RENUT_HOOK(sub_8284AA60, GetRenderStateHook<47>);  // D3DDevice_GetRenderState_PointSizeMax  // N&B sub_8264C770
BANJO_RENUT_HOOK(sub_8284A838, GetRenderStateHook<48>);  // D3DDevice_GetRenderState_MultiSampleAntiAlias  // N&B sub_8264C5B8
BANJO_RENUT_HOOK(sub_8284A860, GetRenderStateHook<49>);  // D3DDevice_GetRenderState_MultiSampleMask  // N&B sub_8264C5E0
BANJO_RENUT_HOOK(sub_8284A6A0, GetRenderStateHook<50>);  // D3DDevice_GetRenderState_ScissorTestEnable  // N&B sub_8264C4B8
BANJO_RENUT_HOOK(sub_8284A750, GetRenderStateHook<51>);  // D3DDevice_GetRenderState_SlopeScaleDepthBias  // N&B sub_8264C568
BANJO_RENUT_HOOK(sub_8284A808, GetRenderStateHook<52>);  // D3DDevice_GetRenderState_DepthBias  // N&B sub_8264C588
BANJO_RENUT_HOOK(sub_8284A8A0, GetRenderStateHook<53>);  // D3DDevice_GetRenderState_ColorWriteEnable  // N&B sub_8264C5E8
BANJO_RENUT_HOOK(sub_8284A8E0, GetRenderStateHook<54>);  // D3DDevice_GetRenderState_ColorWriteEnable1  // N&B sub_8264C628
BANJO_RENUT_HOOK(sub_8284A920, GetRenderStateHook<55>);  // D3DDevice_GetRenderState_ColorWriteEnable2  // N&B sub_8264C668
BANJO_RENUT_HOOK(sub_8284A960, GetRenderStateHook<56>);  // D3DDevice_GetRenderState_ColorWriteEnable3  // N&B sub_8264C6A8
BANJO_RENUT_HOOK(sub_8284B118, GetRenderStateHook<57>);  // D3DDevice_GetRenderState_TessellationMode  // N&B sub_8264CDE8
BANJO_RENUT_HOOK(sub_8284B0A8, GetRenderStateHook<58>);  // D3DDevice_GetRenderState_MinTessellationLevel  // N&B sub_8264CD78
BANJO_RENUT_HOOK(sub_8284B0E0, GetRenderStateHook<59>);  // D3DDevice_GetRenderState_MaxTessellationLevel  // N&B sub_8264CDB0
BANJO_RENUT_HOOK(sub_8284ACD0, GetRenderStateHook<60>);  // D3DDevice_GetRenderState_Wrap0  // N&B sub_8264C9E0
BANJO_RENUT_HOOK(sub_8284ACE0, GetRenderStateHook<61>);  // D3DDevice_GetRenderState_Wrap1  // N&B sub_8264C9F0
BANJO_RENUT_HOOK(sub_8284ACF0, GetRenderStateHook<62>);  // D3DDevice_GetRenderState_Wrap2  // N&B sub_8264CA00
BANJO_RENUT_HOOK(sub_8284AD00, GetRenderStateHook<63>);  // D3DDevice_GetRenderState_Wrap3  // N&B sub_8264CA10
BANJO_RENUT_HOOK(sub_8284AD10, GetRenderStateHook<64>);  // D3DDevice_GetRenderState_Wrap4  // N&B sub_8264CA20
BANJO_RENUT_HOOK(sub_8284AD20, GetRenderStateHook<65>);  // D3DDevice_GetRenderState_Wrap5  // N&B sub_8264CA30
BANJO_RENUT_HOOK(sub_8284AD30, GetRenderStateHook<66>);  // D3DDevice_GetRenderState_Wrap6  // N&B sub_8264CA40
BANJO_RENUT_HOOK(sub_8284AD40, GetRenderStateHook<67>);  // D3DDevice_GetRenderState_Wrap7  // N&B sub_8264CA50
BANJO_RENUT_HOOK(sub_8284AD50, GetRenderStateHook<68>);  // D3DDevice_GetRenderState_Wrap8  // N&B sub_8264CA60
BANJO_RENUT_HOOK(sub_8284AD60, GetRenderStateHook<69>);  // D3DDevice_GetRenderState_Wrap9  // N&B sub_8264CA70
BANJO_RENUT_HOOK(sub_8284AD70, GetRenderStateHook<70>);  // D3DDevice_GetRenderState_Wrap10  // N&B sub_8264CA80
BANJO_RENUT_HOOK(sub_8284AD80, GetRenderStateHook<71>);  // D3DDevice_GetRenderState_Wrap11  // N&B sub_8264CA90
BANJO_RENUT_HOOK(sub_8284AD90, GetRenderStateHook<72>);  // D3DDevice_GetRenderState_Wrap12  // N&B sub_8264CAA0
BANJO_RENUT_HOOK(sub_8284ADA0, GetRenderStateHook<73>);  // D3DDevice_GetRenderState_Wrap13  // N&B sub_8264CAB0
BANJO_RENUT_HOOK(sub_8284ADB0, GetRenderStateHook<74>);  // D3DDevice_GetRenderState_Wrap14  // N&B sub_8264CAC0
BANJO_RENUT_HOOK(sub_8284ADC0, GetRenderStateHook<75>);  // D3DDevice_GetRenderState_Wrap15  // N&B sub_8264CAD0
BANJO_RENUT_HOOK(sub_8284AE10, GetRenderStateHook<76>);  // D3DDevice_GetRenderState_ViewportEnable  // N&B sub_8264CAE0
BANJO_RENUT_HOOK(sub_8284AEB0, GetRenderStateHook<77>);  // D3DDevice_GetRenderState_HighPrecisionBlendEnable  // N&B sub_8264CB80
BANJO_RENUT_HOOK(sub_8284AF48, GetRenderStateHook<78>);  // D3DDevice_GetRenderState_HighPrecisionBlendEnable1  // N&B sub_8264CC18
BANJO_RENUT_HOOK(sub_8284AFE0, GetRenderStateHook<79>);  // D3DDevice_GetRenderState_HighPrecisionBlendEnable2  // N&B sub_8264CCB0
BANJO_RENUT_HOOK(sub_8284B078, GetRenderStateHook<80>);  // D3DDevice_GetRenderState_HighPrecisionBlendEnable3  // N&B sub_8264CD48
BANJO_RENUT_HOOK(sub_8284B150, GetRenderStateHook<81>);  // D3DDevice_GetRenderState_HalfPixelOffset  // N&B sub_8264CE20
BANJO_RENUT_HOOK(sub_8284B180, GetRenderStateHook<82>);  // D3DDevice_GetRenderState_PrimitiveResetEnable  // N&B sub_8264CE50
BANJO_RENUT_HOOK(sub_8284B1B0, GetRenderStateHook<83>);  // D3DDevice_GetRenderState_PrimitiveResetIndex  // N&B sub_8264CE80
BANJO_RENUT_HOOK(sub_8284B1D8, GetRenderStateHook<84>);  // D3DDevice_GetRenderState_AlphaToMaskEnable  // N&B sub_8264CEA8
BANJO_RENUT_HOOK(sub_8284B208, GetRenderStateHook<85>);  // D3DDevice_GetRenderState_AlphaToMaskOffsets  // N&B sub_8264CED8
BANJO_RENUT_HOOK(sub_8284B238, GetRenderStateHook<86>);  // D3DDevice_GetRenderState_GuardBand_X  // N&B sub_8264CF18
BANJO_RENUT_HOOK(sub_8284B268, GetRenderStateHook<87>);  // D3DDevice_GetRenderState_GuardBand_Y  // N&B sub_8264CF48
BANJO_RENUT_HOOK(sub_8284B290, GetRenderStateHook<88>);  // D3DDevice_GetRenderState_DiscardBand_X  // N&B sub_8264CF70
BANJO_RENUT_HOOK(sub_8284B2C0, GetRenderStateHook<89>);  // D3DDevice_GetRenderState_DiscardBand_Y  // N&B sub_8264CFA0
BANJO_RENUT_HOOK(sub_8284B380, GetRenderStateHook<90>);  // D3DDevice_GetRenderState_HiStencilEnable  // N&B sub_8264D008
BANJO_RENUT_HOOK(sub_8284B3B0, GetRenderStateHook<91>);  // D3DDevice_GetRenderState_HiStencilWriteEnable  // N&B sub_8264D038
BANJO_RENUT_HOOK(sub_8284B3E0, GetRenderStateHook<92>);  // D3DDevice_GetRenderState_HiStencilFunc  // N&B sub_8264D068
BANJO_RENUT_HOOK(sub_8284B408, GetRenderStateHook<93>);  // D3DDevice_GetRenderState_HiStencilRef  // N&B sub_8264D090
BANJO_RENUT_HOOK(sub_8284B418, GetRenderStateHook<94>);  // D3DDevice_GetRenderState_PresentInterval  // N&B sub_8264D0A0
BANJO_RENUT_HOOK(sub_8284B430, GetRenderStateHook<95>);  // D3DDevice_GetRenderState_PresentImmediateThreshold  // N&B sub_8264D0B8
BANJO_RENUT_HOOK(sub_8284B2E8, GetRenderStateHook<96>);  // D3DDevice_GetRenderState_HiZEnable  // N&B sub_8264CFC8
BANJO_RENUT_HOOK(sub_8284B350, GetRenderStateHook<97>);  // D3DDevice_GetRenderState_HiZWriteEnable  // N&B sub_8264CFD8
BANJO_RENUT_HOOK(sub_8284A218, GetRenderStateHook<98>);  // D3DDevice_GetRenderState_LastPixel  // N&B sub_8264C088
BANJO_RENUT_HOOK(sub_8284A268, GetRenderStateHook<99>);  // D3DDevice_GetRenderState_LineWidth  // N&B sub_8264C0D8
BANJO_RENUT_HOOK(sub_8284A2B8, GetRenderStateHook<100>);  // D3DDevice_GetRenderState_Buffer2Frames  // N&B sub_8264C130

// Sampler state getters (off_82E50BD0, field 0).
BANJO_RENUT_HOOK(sub_8284BBF0, GetSamplerStateHook<0>);  // D3DDevice_GetSamplerState_AddressU  // N&B sub_8264D600
BANJO_RENUT_HOOK(sub_8284BC40, GetSamplerStateHook<1>);  // D3DDevice_GetSamplerState_AddressV  // N&B sub_8264D618
BANJO_RENUT_HOOK(sub_8284BC90, GetSamplerStateHook<2>);  // D3DDevice_GetSamplerState_AddressW  // N&B sub_8264D668
BANJO_RENUT_HOOK(sub_8284BB90, GetSamplerStateHook<3>);  // D3DDevice_GetSamplerState_BorderColor  // N&B sub_8264D5E0
BANJO_RENUT_HOOK(sub_8284B6B0, GetSamplerStateHook<4>);  // D3DDevice_GetSamplerState_MagFilter  // N&B sub_8264D1A8
BANJO_RENUT_HOOK(sub_8284B508, GetSamplerStateHook<5>);  // D3DDevice_GetSamplerState_MinFilter  // N&B sub_8264D0C8
BANJO_RENUT_HOOK(sub_8284B7C8, GetSamplerStateHook<6>);  // D3DDevice_GetSamplerState_MipFilter  // N&B sub_8264D280
BANJO_RENUT_HOOK(sub_8284BA08, GetSamplerStateHook<7>);  // D3DDevice_GetSamplerState_MipMapLodBias  // N&B sub_8264D4C0
BANJO_RENUT_HOOK(sub_8284BAB8, GetSamplerStateHook<8>);  // D3DDevice_GetSamplerState_MaxMipLevel  // N&B sub_8264D500
BANJO_RENUT_HOOK(sub_8284B8F0, GetSamplerStateHook<9>);  // D3DDevice_GetSamplerState_MaxAnisotropy  // N&B sub_8264D3A8
BANJO_RENUT_HOOK(sub_8284B778, GetSamplerStateHook<10>);  // D3DDevice_GetSamplerState_MagFilterZ  // N&B sub_8264D270
BANJO_RENUT_HOOK(sub_8284B5D8, GetSamplerStateHook<11>);  // D3DDevice_GetSamplerState_MinFilterZ  // N&B sub_8264D198
BANJO_RENUT_HOOK(sub_8284B878, GetSamplerStateHook<12>);  // D3DDevice_GetSamplerState_SeparateZFilterEnable  // N&B sub_8264D330
BANJO_RENUT_HOOK(sub_8284BB38, GetSamplerStateHook<13>);  // D3DDevice_GetSamplerState_MinMipLevel  // N&B sub_8264D580
BANJO_RENUT_HOOK(sub_8284BCE8, GetSamplerStateHook<14>);  // D3DDevice_GetSamplerState_TrilinearThreshold  // N&B sub_8264D6C0
BANJO_RENUT_HOOK(sub_8284B960, GetSamplerStateHook<15>);  // D3DDevice_GetSamplerState_AnisotropyBias  // N&B sub_8264D418
BANJO_RENUT_HOOK(sub_8284BD40, GetSamplerStateHook<16>);  // D3DDevice_GetSamplerState_HGradientExpBias  // N&B sub_8264D718
BANJO_RENUT_HOOK(sub_8284BD98, GetSamplerStateHook<17>);  // D3DDevice_GetSamplerState_VGradientExpBias  // N&B sub_8264D770
BANJO_RENUT_HOOK(sub_8284BDF0, GetSamplerStateHook<18>);  // D3DDevice_GetSamplerState_WhiteBorderColorW  // N&B sub_8264D7C8
BANJO_RENUT_HOOK(sub_8284BE50, GetSamplerStateHook<19>);  // D3DDevice_GetSamplerState_PointBorderEnable  // N&B sub_8264D828

// ffxiii2-nx (2026-11-06): see d3d_native.h.
namespace renut::gpu::native {
u32 SurfaceOrigin(u32 va) { return Ffxiii2SurfaceOrigin(va); }
// k241 (ffxiii2_diag_late_headers): declared where it is called (render/draw.cpp).
void CheckLateHeader(u32 va, bool depth_slot) { Ffxiii2CheckLateHeader(va, depth_slot); }
// k241: declared where it is called (hooks/resource.cpp, D3DResource_Destroy).
void TakeSurfaceCopies(u32 origin, std::vector<u32> &out) { Ffxiii2TakeSurfaceCopies(origin, out); }
}  // namespace renut::gpu::native
std::atomic<uint32_t> g_ffxiii2_late_header[4];
