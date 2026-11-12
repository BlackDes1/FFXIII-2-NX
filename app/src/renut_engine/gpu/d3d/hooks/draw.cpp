/**
 * @file    gpu/d3d/hooks/draw.cpp
 * @brief   Draw, clear and resolve entry points, and the shader setters.
 *
 * The draws replace N&B's D3D entirely: the originals would flush the device's
 * dirty state into GPU packets, including binding shaders by reading their
 * microcode, which renderer-owned shader objects do not carry. Clear, Resolve
 * and tiling queue their host work and do the device work of N&B's D3D
 * natively (d3d_native.h): dirty masks, the tiling state, fences on the
 * destination.
 *
 * The host work runs on the render thread (queued.h); a device recording a
 * command buffer records its draws, clears and resolves (command_buffers.h)
 * instead. Neither takes State().mutex here.
 */
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <thread>
#include <chrono>

#include <rex/hook.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>
#include <rex/system/xthread.h>  // FFNS P6 job census
#include <map>
#include <array>
#include <chrono>

#include "renut_engine/gpu/banjo_renut_select.h"
#include <rex/types.h>

#include "renut_engine/gpu/guest/command_buffers.h"
#include "renut_engine/gpu/d3d/d3d.h"
#include "renut_engine/gpu/d3d/d3d_native.h"
#include "renut_engine/gpu/render/draw.h"
#include "renut_engine/gpu/guest/guest_memory.h"
#include "renut_engine/gpu/guest/guest_records.h"
#include "renut_engine/gpu/log.h"
#include "renut_engine/gpu/backend/queued.h"
#include "renut_engine/gpu/backend/render_thread.h"
#include "renut_engine/gpu/render/surfaces.h"
#include "renut_engine/gpu/backend/video.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <fmt/format.h>
extern std::atomic<uint32_t> g_ffxiii2_diag[8];  // ffxiii2-nx: counters, ffxiii2_renut_glue.cpp
extern std::atomic<uint32_t> g_ffxiii2_swap_calls;  // ffxiii2-nx k235: hooks/device.cpp

#include <rex/cvar.h>
REXCVAR_DEFINE_BOOL(ffxiii2_renut_diag_skip_quad_lists, false, "FFXIII2",
                    "Renut, a test: DrawVertices quad lists are not drawn");

using namespace renut::gpu;

// A float argument also takes the GPR (or stack) slot of its position, which
// the marshaling does not skip: the u32 after each double in the hooks'
// parameters stands in for it.

namespace {

constexpr u32 kE_OUTOFMEMORY = 0x8007000Eu;
// D3DVIEWPORT9 {0, 0, 0xFFFF, 0xFFFF, 0.0, 1.0} in N&B's data (unk_820018DC):
// the whole surface.
// bejeweled3-nx: 0x82001284 (SetSurfaceInfo sub_822F3500 `lis r10,-32256; addi r4,r10,4740` where Peggle 2's
// sub_8261ADB0 has 0x8207A610); its 24 bytes are the same (0, 0, 0xFFFF, 0xFFFF, 0.0, 1.0).
// ffxiii2-nx: {0, 0, 0xFFFF, 0xFFFF, 0.0, 1.0}, the viewport D3D_SetSurfaceInfo sub_8284C738 and the clears pass.
constexpr u32 kFullViewport = 0x82146974;  // ffxiii2-nx (Zuma's Revenge 0x8205E9D0, Bejeweled 3 0x82001284, N&B 0x820018DC)
// D3DRS_HIZWRITEENABLE's index in the render state table.
constexpr u32 kRenderStateHiZWriteEnable = 97;

// Guest memory Begin*Vertices hands out; the game writes one draw's vertices
// into it before its next D3D call. One per device, as devices record on
// different threads.
constexpr u32 kVerticesScratchSize = 4u << 20;
std::mutex g_vertices_scratch_mutex;  // devices call from their own threads
std::unordered_map<u32, u32> g_vertices_scratch;

u32 VerticesScratch(u32 device) {
  std::lock_guard lock(g_vertices_scratch_mutex);
  u32 &scratch = g_vertices_scratch[device];
  if (!scratch)
    scratch = records::AllocPhysicalBlock(kVerticesScratchSize);
  return scratch;
}

bool Active() {
  auto &s = State();
  return s.host_ready && !s.shutting_down.load(std::memory_order_acquire);
}

void StoreU32(u32 va, u32 value) {
  if (auto *p = GuestPtr<be_u32>(va))
    *p = value;
}

u32 LoadU32(u32 va) {
  const auto *p = GuestPtr<const be_u32>(va);
  return p ? u32(*p) : 0;
}

bool ReadRect(u32 va, ClearRect &rect) {
  const auto *p = GuestPtr<const be_u32>(va);
  if (!p)
    return false;
  rect = {i32(u32(p[0])), i32(u32(p[1])), i32(u32(p[2])), i32(u32(p[3]))};
  return true;
}

bool ReadVector4(u32 va, float out[4]) {
  const auto *p = GuestPtr<const be_u32>(va);
  if (!p)
    return false;
  for (int i = 0; i < 4; ++i) {
    const u32 bits = p[i];
    std::memcpy(&out[i], &bits, 4);
  }
  return true;
}

// ffxiii2-nx (2026-11-04, a diagnostic, either renderer): the first quad-list draws' first vertices and bound textures
// (the title screen's glyphs), to compare the emulated run with Renut's.
void Ffxiii2QuadDiag(u32 device, u32 primitive, u32 start_vertex, u32 vertex_count) {
  if (primitive == 13) {
    static uint32_t dumped = 0;
    if (++dumped == 40) {  // the text, settled: all its vertices to logs/quad_vertices.bin
      native::Device d = native::DeviceAt(device);
      const u32 w0 = d ? d.W(native::off::VertexFetch + 17 * 8) : 0;
      const u32 stride = d ? u32(d.B(native::off::StreamStrides)) * 4 : 0;
      if (const auto *v = w0 ? GuestPhysicalPtr<const u8>((w0 & 0x1FFFFFFCu) + start_vertex * stride) : nullptr) {
        if (FILE *f = std::fopen("logs/quad_vertices.bin", "wb")) {
          std::fwrite(v, 1, size_t(vertex_count) * stride, f);
          std::fclose(f);
        }
      }
    }
    static uint32_t logged = 0;
    native::Device d = native::DeviceAt(device);
    if (d && logged < 2) {
      ++logged;
      const u32 w0 = d.W(native::off::VertexFetch + 17 * 8);
      const u32 stride = u32(d.B(native::off::StreamStrides)) * 4;
      if (const auto *v = GuestPhysicalPtr<const be_u32>((w0 & 0x1FFFFFFCu) + start_vertex * stride)) {
        std::string text;
        for (u32 i = 0; i < 4 * stride / 4; ++i)
          text += fmt::format(" {:08X}", u32(v[i]));
        {
          const u32 decl = d.W(d3d::kDeviceOffsetVertexDeclaration);
          std::string dt;
          if (const auto *dw = GuestPtr<const be_u32>(decl))
            for (u32 i = 0; i < 24; ++i) dt += fmt::format(" {:08X}", u32(dw[i]));
          RGPU_INFO("[ffxiii2] quad list draw: declaration {:08X}:{}", decl, dt);
        }
        RGPU_INFO("[ffxiii2] quad list draw: textures {:08X} {:08X} fetch {:08X} {:08X} stride {} start {} count {}:{}",
                  d.W(0x3278), d.W(0x327C), w0, d.W(native::off::VertexFetch + 17 * 8 + 4), stride, start_vertex,
                  vertex_count, text);
      }
    }
  }
}

// void D3DDevice_DrawVertices(D3DDevice*, D3DPRIMITIVETYPE, UINT StartVertex,
//                             UINT VertexCount) [0x8222C7A0]
void D3DDevice_DrawVertices_hook(u32 device, u32 primitive, u32 start_vertex, u32 vertex_count) {
  g_ffxiii2_diag[0].fetch_add(1, std::memory_order_relaxed);
  if (!Active())
    return;
  if (primitive == 13 && REXCVAR_GET(ffxiii2_renut_diag_skip_quad_lists))
    return;  // ffxiii2-nx (2026-11-04, a test): is the white the quad-list (glyph) draw?
  DrawCall call;
  call.primitive = primitive;
  call.start_vertex = start_vertex;
  call.count = vertex_count;
  if (IsRecordingCommandBuffer(device))
    RecordDraw(device, call, 0, 0);
  else
    QueueDraw(device, call, 0, 0);
}

// void D3DDevice_DrawIndexedVertices(D3DDevice*, D3DPRIMITIVETYPE, INT BaseVertexIndex,
//                                    UINT StartIndex, UINT IndexCount) [0x826567C8]
void D3DDevice_DrawIndexedVertices_hook(u32 device, u32 primitive, u32 base_vertex,
                                        u32 start_index, u32 index_count) {
  g_ffxiii2_diag[0].fetch_add(1, std::memory_order_relaxed);
  if (!Active())
    return;
  DrawCall call;
  call.primitive = primitive;
  call.indexed = true;
  call.base_vertex = i32(base_vertex);
  call.start_index = start_index;
  call.count = index_count;
  if (IsRecordingCommandBuffer(device))
    RecordDraw(device, call, 0, 0);
  else
    QueueDraw(device, call, 0, 0);
}

// void D3DDevice_DrawIndexedTessellatedVertices(D3DDevice*, D3DTESSPRIMITIVETYPE,
//     INT BaseVertexIndex, UINT StartIndex, UINT IndexCount) [0x82230838]
// Replaced like the other draws: the original's state flush binds the shader
// from its microcode, and bugchecks (0xF5) on a tessellation shader without
// the tessellation state the original SetVertexShader keeps. The draw is
// tessellated on the host (DrawCall::tessellation).
void D3DDevice_DrawIndexedTessellatedVertices_hook(u32 device, u32 primitive, u32 base_vertex,
                                                   u32 start_index, u32 index_count) {
  if (!Active())
    return;
  DrawCall call;
  call.primitive = primitive;
  call.tessellation = true;
  call.indexed = true;
  call.base_vertex = i32(base_vertex);
  call.start_index = start_index;
  call.count = index_count;
  if (IsRecordingCommandBuffer(device))
    RecordDraw(device, call, 0, 0);
  else
    QueueDraw(device, call, 0, 0);
}

// void D3DDevice_DrawVerticesUP(D3DDevice*, D3DPRIMITIVETYPE, UINT VertexCount,
//                               const void *pVertexStreamZeroData, UINT Stride) [0x8222E520]
void D3DDevice_DrawVerticesUP_hook(u32 device, u32 primitive, u32 vertex_count, u32 data,
                                   u32 stride) {
  if (!Active() || !data)
    return;
  DrawCall call;
  call.primitive = primitive;
  call.count = vertex_count;
  call.up_vertices = GuestPtr<const u8>(data);
  call.up_stride = stride;
  call.up_vertex_count = vertex_count;
  if (IsRecordingCommandBuffer(device))
    RecordDraw(device, call, vertex_count * stride, 0);
  else
    QueueDraw(device, call, vertex_count * stride, 0);
}

// void D3DDevice_DrawIndexedVerticesUP(D3DDevice*, D3DPRIMITIVETYPE, UINT MinVertexIndex,
//     UINT NumVertices, UINT IndexCount, const void *pIndexData, D3DFORMAT IndexDataFormat,
//     const void *pVertexStreamZeroData, UINT VertexStreamZeroStride) [0x82656728]
void D3DDevice_DrawIndexedVerticesUP_hook(u32 device, u32 primitive, u32 min_vertex,
                                          u32 num_vertices, u32 index_count, u32 index_data,
                                          u32 index_format, u32 vertex_data, u32 stride) {
  if (!Active() || !index_data || !vertex_data)
    return;
  DrawCall call;
  call.primitive = primitive;
  call.indexed = true;
  call.count = index_count;
  call.up_indices = GuestPtr<const u8>(index_data);
  call.up_indices_32 = (index_format & 4) != 0;  // D3DFMT_INDEX32
  call.up_vertices = GuestPtr<const u8>(vertex_data);
  call.up_stride = stride;
  call.up_vertex_count = min_vertex + num_vertices;
  const u32 vertex_bytes = call.up_vertex_count * stride;
  const u32 index_bytes = index_count * (call.up_indices_32 ? 4 : 2);
  if (IsRecordingCommandBuffer(device))
    RecordDraw(device, call, vertex_bytes, index_bytes);
  else
    QueueDraw(device, call, vertex_bytes, index_bytes);
}

// void *D3DDevice_BeginVertices(D3DDevice*, D3DPRIMITIVETYPE, UINT VertexCount,
//                               UINT VertexStreamZeroStride) [0x8222CC50]
// Callers write the vertices, then end inline by restoring m_pRing from the
// device, so the draw's state is captured now and its vertices copied at the
// device's next hook (FlushDeferredVertices).
u32 D3DDevice_BeginVertices_hook(u32 device, u32 primitive, u32 vertex_count, u32 stride) {
  const u64 bytes = u64(vertex_count) * stride;
  if (!bytes || bytes > kVerticesScratchSize)
    return 0;
  // The inline end: m_pRing = [device + kDeviceOffsetVerticesRingEnd].
  StoreU32(device + d3d::kDeviceOffsetVerticesRingEnd,
           LoadU32(device + u32(offsetof(d3d::Device, m_pRing))));

  const u32 scratch = VerticesScratch(device);
  if (!scratch)
    return 0;
  DrawCall call;
  call.primitive = primitive;
  call.count = vertex_count;
  call.up_vertices = GuestPtr<const u8>(scratch);
  call.up_stride = stride;
  call.up_vertex_count = vertex_count;
  // Either way the device's previous Begin*Vertices data leaves the scratch
  // before the game writes this draw's over it.
  if (IsRecordingCommandBuffer(device))
    RecordDeferredDraw(device, call, call.up_vertices, u32(bytes));
  else if (Active())
    QueueDeferredDraw(device, call, call.up_vertices, u32(bytes));
  else
    FlushDeferredVertices(device);
  return scratch;
}

// A clear: the renderer's, recorded or queued, then N&B's device work.
// whole_target: BeginTiling's, of one tile's worth of EDRAM, which on the
// host is the whole tiled target.
void Clear(u32 device, u32 flags, const ClearRect *rect, const float color[4], float z,
           u32 stencil, bool whole_target) {
  const ClearRect *host_rect = whole_target ? nullptr : rect;
  if (IsRecordingCommandBuffer(device))
    RecordClear(device, flags, host_rect, color, z, stencil);
  else if (Active())
    QueueClear(device, flags, host_rect, color, z, stencil);
  const native::Rect native_rect = rect ? native::Rect{rect->x1, rect->y1, rect->x2, rect->y2}
                                        : native::Rect{};
  native::Clear(native::DeviceAt(device), flags, rect ? &native_rect : nullptr);
}

// A resolve: the renderer's copy (and clear), then N&B's device work.
void Resolve(u32 device, u32 flags, const ClearRect *rect, u32 dest_texture, i32 dest_x,
             i32 dest_y, u32 dest_level, u32 dest_slice, u32 color_va, float clear_z,
             u32 clear_stencil, u32 parameters) {
  float color[4] = {0, 0, 0, 0};
  const bool has_color = color_va && ReadVector4(color_va, color);
  if (IsRecordingCommandBuffer(device)) {
    RecordResolve(device, flags, rect, dest_texture, dest_x, dest_y, dest_level, dest_slice,
                  has_color ? color : nullptr, clear_z, clear_stencil);
  } else if (Active()) {
    QueueResolve(device, flags, rect, dest_texture, dest_x, dest_y, dest_level, dest_slice,
                 has_color ? color : nullptr, clear_z, clear_stencil);
  }
  native::Resolve(native::DeviceAt(device), flags, dest_texture, clear_z, clear_stencil,
                  parameters);
}

// void D3DDevice_ClearF(D3DDevice*, DWORD Flags, const D3DRECT *pRect,
//                       const D3DVECTOR4 *pColor, float Z, DWORD Stencil) [0x822811A0]
// Z takes r7's slot, so Stencil arrives in r8.
void D3DDevice_ClearF_hook(u32 device, u32 flags, u32 rect_va, u32 color_va, double z,
                           u32 /*z_slot*/, u32 stencil) {
  ClearRect rect;
  const bool has_rect = rect_va && ReadRect(rect_va, rect);
  float color[4] = {0, 0, 0, 0};  // unk_82001160 without one
  if (color_va)
    ReadVector4(color_va, color);
  Clear(device, flags, has_rect ? &rect : nullptr, color, float(z), stencil, false);
}

// void D3DDevice_Clear(D3DDevice*, DWORD Count, const D3DRECT *pRects, DWORD Flags,
//     D3DCOLOR Color, float Z, DWORD Stencil) [0x822812C8]: ClearF with the
// color as a vector (XMLoadColor: each byte as 1 + b / 2^23, scaled by
// 2^23 / 255 and offset), per rect. Z takes r8's slot.
void D3DDevice_Clear_hook(u32 device, u32 count, u32 rects_va, u32 flags, u32 color_argb,
                          double z, u32 /*z_slot*/, u32 stencil) {
  const float scale = std::bit_cast<float>(0x47008081u);  // aG, unk_82001150
  auto channel = [scale](u32 byte) {
    return std::fmaf(std::bit_cast<float>(0x3F800000u | (byte & 0xFF)), scale, -scale);
  };
  const float color[4] = {channel(color_argb >> 16), channel(color_argb >> 8),
                          channel(color_argb), channel(color_argb >> 24)};
  if (!rects_va) {
    Clear(device, flags, nullptr, color, float(z), stencil, false);
    return;
  }
  for (u32 i = 0; i < count; ++i) {
    ClearRect rect;
    if (ReadRect(rects_va + i * 16, rect))
      Clear(device, flags, &rect, color, float(z), stencil, false);
  }
}

// void D3DDevice_Resolve(D3DDevice*, DWORD Flags, const D3DRECT *pSourceRect,
//     D3DBaseTexture *pDestTexture, const D3DPOINT *pDestPoint, UINT DestLevel,
//     UINT DestSliceOrFace, const D3DVECTOR4 *pClearColor, float ClearZ,
//     DWORD ClearStencil, const D3DRESOLVE_PARAMETERS *pParameters) [0x8222D6C8]
// ClearZ is in f1 and takes the first stack slot; ClearStencil and pParameters
// are in the two after it.
void D3DDevice_Resolve_hook(u32 device, u32 flags, u32 rect_va, u32 dest_texture, u32 point_va,
                            u32 dest_level, u32 dest_slice, u32 color_va, double clear_z,
                            u32 /*z_slot*/, u32 clear_stencil, u32 parameters) {
  g_ffxiii2_diag[1].fetch_add(1, std::memory_order_relaxed);
  {  // ffxiii2-nx (2026-11-04, a diagnostic): the first 40 resolves' destinations (header, base, format, size, flags).
    static uint32_t logged = 0;
    if (logged < 40) {
      ++logged;
      const auto *h = dest_texture ? GuestPtr<const be_u32>(dest_texture) : nullptr;
      RGPU_INFO("[ffxiii2] resolve {}: flags {:X} dest {:08X} fetch {:08X} {:08X} {:08X} level {} slice {}", logged, flags,
                dest_texture, h ? u32(h[8]) : 0, h ? u32(h[9]) : 0, h ? u32(h[10]) : 0, dest_level, dest_slice);
    }
  }
  ClearRect rect;
  const bool has_rect = rect_va && ReadRect(rect_va, rect);
  i32 dest_x = 0, dest_y = 0;
  if (const auto *p = GuestPtr<const be_u32>(point_va)) {
    dest_x = i32(u32(p[0]));
    dest_y = i32(u32(p[1]));
  }
  Resolve(device, flags, has_rect ? &rect : nullptr, dest_texture, dest_x, dest_y, dest_level,
          dest_slice, color_va, float(clear_z), clear_stencil, parameters);
}

// void D3DDevice_BeginTiling(D3DDevice*, DWORD Flags, DWORD Count,
//     const D3DRECT *pTileRects, const D3DVECTOR4 *pClearColor, float ClearZ,
//     DWORD ClearStencil) [0x822788A0]
// The surfaces bound from here to EndTiling are backed by targets covering
// every tile (SetTilingExtentLocked). N&B's BeginTiling clears with Z 0.0 and
// stencil 0 (its ClearZ and ClearStencil go unused), unless the device records
// a one-pass Z pass (Flags3 0x20); its ring segment for the tiles is left out.
void D3DDevice_BeginTiling_hook(u32 device, u32 /*flags*/, u32 count, u32 rects_va, u32 color_va,
                                double /*clear_z*/, u32 /*z_slot*/, u32 /*clear_stencil*/) {
  native::Device d = native::DeviceAt(device);
  if (!d)
    return;
  std::vector<ClearRect> tiles;
  for (u32 i = 0; i < count; ++i) {
    ClearRect rect;
    if (!ReadRect(rects_va + i * 16, rect))
      break;
    tiles.push_back(rect);
  }
  count = u32(tiles.size());
  if (!IsRecordingCommandBuffer(device)) {
    FlushDeferredVertices(device);
    u32 width = 0, height = 0;
    for (const ClearRect &rect : tiles) {
      width = std::max(width, u32(rect.x2));
      height = std::max(height, u32(rect.y2));
    }
    render_thread::Queue([tiles, width, height] {
      SetTilingExtentLocked(width, height);
      SetTilesLocked(tiles.data(), u32(tiles.size()));
    });
  }

  d.SetW(native::off::TileCount, count);
  u32 extent_x = 0, extent_y = 0;
  for (u32 i = 0; i < count; ++i) {
    const ClearRect &rect = tiles[i];
    extent_x = std::max(extent_x, u32(rect.x2));
    extent_y = std::max(extent_y, u32(rect.y2));
    d.SetW(native::off::TileRects + i * 16, u32(rect.x1));
    d.SetW(native::off::TileRects + i * 16 + 4, u32(rect.y1));
    d.SetW(native::off::TileRects + i * 16 + 8, u32(rect.x2));
    d.SetW(native::off::TileRects + i * 16 + 12, u32(rect.y2));
    d.SetW(native::off::TileOrigins + i * 8, u32(rect.x1) & ~31u);
    d.SetW(native::off::TileOrigins + i * 8 + 4, u32(rect.y1) & ~31u);
  }
  d.SetW(native::off::TilingExtent, extent_x);
  d.SetW(native::off::TilingExtent + 4, extent_y);
  d.SetW(0x34FC, 0);

  if (!(d.B(native::off::Flags3) & 0x20)) {
    // The largest tile, from its origin: its clear covers a tile of EDRAM.
    u32 width = 0, height = 0;
    for (u32 i = 0; i < count; ++i) {
      width = std::max(width, u32(tiles[i].x2) - d.W(native::off::TileOrigins + i * 8));
      height = std::max(height, u32(tiles[i].y2) - d.W(native::off::TileOrigins + i * 8 + 4));
    }
    // The viewport (x, y, width, height, min z, max z) and scissor rect, for
    // after the clear.
    float viewport[6];
    for (u32 i = 0; i < 6; ++i)
      viewport[i] = d.F(0x32E0 + i * 4);
    i32 scissor[4];
    for (u32 i = 0; i < 4; ++i)
      scissor[i] = i32(d.W(native::off::ScissorRect + i * 4));
    native::SetViewport(d, kFullViewport);
    native::SetScissorRect(d, 0, 0, 0xFFFF, 0xFFFF);  // dword_82001190
    const ClearRect rect{0, 0, i32(width), i32(height)};
    float color[4] = {0, 0, 0, 0};
    if (color_va)
      ReadVector4(color_va, color);
    Clear(device, 0x3F, &rect, color, 0.0f, 0, true);
    native::SetViewport(d, viewport[0], viewport[1], viewport[2], viewport[3], viewport[4],
                        viewport[5]);
    native::SetScissorRect(d, scissor[0], scissor[1], scissor[2], scissor[3]);
  }
  if (const auto *color = color_va ? GuestPtr<const u8>(color_va) : nullptr)
    std::memcpy(d.bytes + 0x3508, color, 16);
  else
    std::memset(d.bytes + 0x3508, 0, 16);
  d.SetF(0x3500, 0.0f);  // flt_82000D18
  d.SetW(0x3504, 0);

  const u32 target = d.W(native::off::RenderTargets);
  d.SetW(0x332C, target ? target : d.W(native::off::DepthStencil));
  for (u32 i = 0; i < 5; ++i)  // the 4 targets, then depth
    d.SetW(native::off::TilingSurfaces + i * 4, d.W(native::off::RenderTargets + i * 4));
  d.SetW(0x3328, 2);
  d.SetB(native::off::Flags, d.B(native::off::Flags) | 0x20);
  d.SetW(native::off::PredicationSet, 0);
  native::UpdateTilePredicated(d);
  d.SetW(native::off::RunPredicationState, 0);
  d.SetW(native::off::Predication, ~0u);
  d.SetW(native::off::TilePredication, 0);
  d.SetW(native::off::RunPredication, 0);
  // The tiles' commands go into a ring segment of their own from a KickOff.
  native::KickOff(device);
  native::SetViewport(d, kFullViewport);
  native::ResetAllState(d);
}

// HRESULT D3DDevice_EndTiling(D3DDevice*, DWORD ResolveFlags,
//     const D3DRECT *pResolveRects, D3DBaseTexture *pDestTexture,
//     const D3DVECTOR4 *pClearColor, float ClearZ, DWORD ClearStencil,
//     const D3DRESOLVE_PARAMETERS *pParameters) [0x82212328]
// N&B's EndTiling resolves each tile's rect (ResolveFlags 0x300: color and
// depth cleared) with BeginTiling's clear values, the last one with
// pClearColor, Z 1.0 and stencil 0; ResolveFlags, pResolveRects, ClearZ,
// ClearStencil and pParameters go unused. The host resolve reads each rect
// from the full-size target. Left out: the tiles' ring segment, D3D's
// WorkerDpc callback that replays it per tile, and its fence's packets.
u32 D3DDevice_EndTiling_hook(u32 device, u32 /*resolve_flags*/, u32 /*rects_va*/,
                             u32 dest_texture, u32 color_va, double /*clear_z*/, u32 /*z_slot*/,
                             u32 /*clear_stencil*/, u32 /*parameters*/) {
  native::Device d = native::DeviceAt(device);
  if (!d)
    return 0;
  const u32 count = d.W(native::off::TileCount);
  if (dest_texture) {
    for (u32 i = 0; i < count; ++i) {
      const u32 tile = native::off::TileRects + i * 16;
      ClearRect rect{i32(d.W(tile)), i32(d.W(tile + 4)), i32(d.W(tile + 8)), i32(d.W(tile + 12))};
      // A tile at the surface's edge resolves to a multiple of 8.
      const u32 surface = d.W(0x332C);
      const auto *s = surface ? GuestPtr<const be_u32>(surface) : nullptr;
      const u32 size = s ? u32(s[9]) : 0;
      if (u32(rect.x2) == (size >> 18) + 1)
        rect.x2 = i32((u32(rect.x2) + 7) & ~7u);
      if (u32(rect.y2) == native::Extract(size, 3, 15) + 1)
        rect.y2 = i32((u32(rect.y2) + 7) & ~7u);
      native::SetPredication(d, 3u << (i * 2));
      native::SetRenderState(d, kRenderStateHiZWriteEnable,
                             (d.B(native::off::Flags3) & 0x20) ? 0 : 2);
      const i32 dest_x = i32(d.W(tile)), dest_y = i32(d.W(tile + 4));
      if (i + 1 < count) {
        Resolve(device, 0x300, &rect, dest_texture, dest_x, dest_y, 0, 0, device + 0x3508,
                d.F(0x3500), d.W(0x3504), 0);
      } else {
        Resolve(device, 0x300, &rect, dest_texture, dest_x, dest_y, 0, 0, color_va,
                1.0f /*flt_82000D0C*/, 0, 0);
      }
    }
  }
  native::SetPredication(d, 0);
  native::SetRenderState(d, kRenderStateHiZWriteEnable, 2);
  if (count != 1) {
    // Around the WorkerDpc callback.
    native::SetPredication(d, 0x80000000u);
    native::SetPredication(d, 0);
  }
  native::KickOff(device);

  d.SetB(native::off::Flags3, d.B(native::off::Flags3) & 0xCF);
  d.SetB(native::off::Flags, d.B(native::off::Flags) & 0xDF);
  d.SetW(0x332C, 0);
  d.SetW(native::off::PredicationSet, 0);
  native::UpdateTilePredicated(d);
  d.SetW(native::off::RunPredicationState, 0);
  d.SetW(native::off::Predication, ~0u);
  d.SetW(native::off::TilePredication, 0);
  d.SetW(native::off::RunPredication, 0);
  // sub_82210138: the segment's fence, unless the device's command memory ran
  // out (EndCommandBuffer marks a device so until the next Begin).
  const bool out_of_memory = (d.B(native::off::Flags1) & 0x20) != 0;
  if (out_of_memory) {
    d.SetB(native::off::Flags1, d.B(native::off::Flags1) & 0xDF);
    native::RewindRing(d);
  } else {
    native::SetFence(device);
  }
  native::SetViewport(d, kFullViewport);

  if (!IsRecordingCommandBuffer(device)) {
    render_thread::Queue([] {
      SetTilingExtentLocked(0, 0);
      SetTilesLocked(nullptr, 0);
    });
  }
  if (out_of_memory) {
    native::ResetAllState(d);
    return kE_OUTOFMEMORY;
  }
  return 0;
}

// void D3DDevice_BeginExport(D3DDevice*, DWORD Index, D3DResource*, DWORD Flags)
// [0x82269618]. The renderer finds a draw's memexport streams in its shaders.
void D3DDevice_BeginExport_hook(u32 device, u32 index, u32 resource, u32 flags) {
  native::BeginExport(native::DeviceAt(device), index, resource, flags);
}

// void D3DDevice_EndExport(D3DDevice*, DWORD Index, D3DResource*, DWORD Flags)
// [0x822693B0]: the resource is busy until the current fence, which completes
// once the render thread has written the stream's results.
void D3DDevice_EndExport_hook(u32 device, u32 index, u32 resource, u32 flags) {
  native::EndExport(native::DeviceAt(device), index, resource, flags);
}

// void D3DDevice_Present(D3DDevice*) [0x82654778]: the automatic back buffer
// resolved into the automatic front buffer, then Swap. N&B creates its device
// without an automatic front buffer, so its callers (XUI, the threaded movie
// player) have nothing to resolve to.
void D3DDevice_Present_hook(u32 device) {
  native::Device d = native::DeviceAt(device);
  if (!d)
    return;
  FlushDeferredVertices(device);
  float viewport[6];
  for (u32 i = 0; i < 6; ++i)
    viewport[i] = d.F(0x32E0 + i * 4);
  i32 scissor[4];
  for (u32 i = 0; i < 4; ++i)
    scissor[i] = i32(d.W(native::off::ScissorRect + i * 4));
  native::SetRenderTarget(d, 0, d.W(native::off::AutoBackBuffer));
  // SynchronizeToPresentationInterval (0x8228A748): its swap callback and wait
  // go into the ring; the swap is pending.
  native::SetPredication(d, 0);
  d.SetB(native::off::Flags1, d.B(native::off::Flags1) | d3d::kSwapFlagPending);
  const u32 front_buffer = d.W(native::off::AutoFrontBuffer);
  if (front_buffer)
    Resolve(device, 0, nullptr, front_buffer, 0, 0, 0, 0, 0, 1.0f, 0, 0);
  native::KickOff(device);
  native::SetViewport(d, viewport[0], viewport[1], viewport[2], viewport[3], viewport[4],
                      viewport[5]);
  native::SetScissorRect(d, scissor[0], scissor[1], scissor[2], scissor[3]);
  native::Swap(device, front_buffer, device + 0x36DC);
}

// void D3DDevice_SetVertexShader(D3DDevice*, D3DVertexShader*) [0x8222A0A8]
void D3DDevice_SetVertexShader_hook(u32 device, u32 shader) {
  native::SetVertexShader(native::DeviceAt(device), shader);
}

// void D3DDevice_SetPixelShader(D3DDevice*, D3DPixelShader*) [0x82226730]
void D3DDevice_SetPixelShader_hook(u32 device, u32 shader) {
  native::SetPixelShader(native::DeviceAt(device), shader);
}

} // namespace

// ffxiii2-nx (2026-11-06, k234, a diagnostic for the intro's every-other-frame face): from the device's swap count
// ffxiii2_diag_draw_chains_at, for ffxiii2_diag_draw_chains_frames swaps, every draw's guest call chain (the caller's
// LR and 10 frames up) with its count per swap; capturas/draw_chains.txt. The chains present only in every other swap
// are the face's path; their first frame above the common one is where the game decides.
REXCVAR_DEFINE_INT32(ffxiii2_diag_draw_chains_at, 0, "FFXIII2/Diag",
                     "Diagnostic: record draw call chains from this device swap count (0 = off)");
REXCVAR_DEFINE_INT32(ffxiii2_diag_draw_chains_frames, 40, "FFXIII2/Diag",
                     "Diagnostic: ffxiii2_diag_draw_chains_at records this many swaps");
// FFXIII-2-Native-Switch P6 (2026-11-09): the depth surface last bound (hooks/state.cpp), in each chain's key, so the
// shadow passes' draws can be told from the scene's.
std::atomic<uint32_t> g_ffns_diag_depth{0};
namespace {
std::mutex g_chain_mutex;
std::unordered_map<std::string, std::vector<u16>> g_chains;
bool g_chains_written = false;
std::atomic<uint32_t> g_note_calls[2];
void NoteDrawChain(PPCContext &ctx, char kind) {
  g_note_calls[kind == 'I'].fetch_add(1, std::memory_order_relaxed);
  static const int32_t at = REXCVAR_GET(ffxiii2_diag_draw_chains_at);
  if (at <= 0)
    return;
  static const u32 frames = u32(std::max<int32_t>(1, REXCVAR_GET(ffxiii2_diag_draw_chains_frames)));
  // The title device's swap count (the indexed draws come on another device: intro_i recorded none with r3's).
  const u32 title = Video::GuestDevice();
  const auto *sw = title ? GuestPtr<const be_u32>(title + 16968) : nullptr;
  if (!sw)
    return;
  const u32 swaps = g_ffxiii2_swap_calls.load(std::memory_order_relaxed);  // k235: the Swap calls (pass_counts numbering)
  if (swaps < u32(at))
    return;
  std::lock_guard lock(g_chain_mutex);
  if (g_chains_written)
    return;
  if (swaps >= u32(at) + frames) {
    g_chains_written = true;
    if (FILE *f = std::fopen("capturas/draw_chains.txt", "w")) {
      std::fprintf(f, "swaps %d..%u, %zu chains: per-swap draw counts | kind lr chain\n", at, u32(at) + frames - 1,
                   g_chains.size());
      for (const auto &[chain, counts] : g_chains) {
        std::string row;
        for (u16 c : counts)
          row += fmt::format("{} ", c);
        std::fprintf(f, "%s| %s\n", row.c_str(), chain.c_str());
      }
      std::fclose(f);
    }
    RGPU_INFO("[ffxiii2] draw chains written: {} chains; calls so far: DrawVertices {}, DrawIndexedVertices {}, draw "
              "hooks {}",
              g_chains.size(), g_note_calls[0].load(), g_note_calls[1].load(), g_ffxiii2_diag[0].load());
    return;
  }
  std::string chain = fmt::format("{} z{:08X} {:08X} d{}:", kind, g_ffns_diag_depth.load(std::memory_order_relaxed),
                                  u32(ctx.lr), ctx.r3.u32 == title ? 'T' : 'O');
  u32 sp = ctx.r1.u32;
  for (int depth = 0; depth < 10; ++depth) {
    const auto *p = GuestPtr<const be_u32>(sp);
    if (!p)
      break;
    const u32 up = u32(*p);
    if (up <= sp || up - sp > 0x10000u)
      break;
    const auto *l = GuestPtr<const be_u32>(up - 8);
    if (!l)
      break;
    chain += fmt::format(" {:08X}", u32(*l));
    if ((up & 0xFFFu) == 0)  // the stack's top: nothing above (reading it faulted, intro_h)
      break;
    sp = up;
  }
  auto &counts = g_chains[chain];
  counts.resize(frames);
  ++counts[swaps - u32(at)];
}
// k234 h: FF's render-list calls (the face's prepass/shadow draws are missing from every other swap's lists,
// draw_chains intro_n: 36/20 and 34/26 a swap): each call's caller and first arguments, counted per swap.
}  // namespace
REXCVAR_DEFINE_BOOL(ffxiii2_diag_pass_counts, false, "FFXIII2/Diag",
                    "Diagnostic, either renderer: per Swap, FF's pass-1 list calls (sub_82972D48 from 829858EC) and those "
                    "of the intro face's first part (6B3BB7F0); capturas/pass_counts.txt every 300 swaps");
extern std::atomic<uint32_t> g_ffxiii2_swap_calls;  // hooks/device.cpp
namespace {
constexpr u32 kPassCountSwaps = 40000;
std::vector<uint16_t> g_pass_total(kPassCountSwaps), g_pass_face(kPassCountSwaps);
std::atomic<u32> g_first_face_swap{0};
// k235: the face item's inserts per swap (count, its +25 flag byte at the last one, the last caller).
std::vector<uint16_t> g_ins_count(kPassCountSwaps);
std::vector<uint8_t> g_ins_flags(kPassCountSwaps);
std::vector<uint32_t> g_ins_lr(kPassCountSwaps), g_ins_lr2(kPassCountSwaps);
void NotePassCount(PPCContext &ctx) {
  static const bool on = REXCVAR_GET(ffxiii2_diag_pass_counts);
  if (!on)
    return;
  const u32 n = g_ffxiii2_swap_calls.load(std::memory_order_relaxed);
  if (n >= kPassCountSwaps)
    return;
  // k239 (the Switch gave 0 on every swap): every call counted in the 6th field, the face's caller in the 5th, so
  // a caller check that does not hold on a platform shows as such
  ++g_ins_lr2[n];
  if (ctx.r3.u32 == 0x6B3BB7F0u)
    g_ins_lr[n] = u32(ctx.lr);
  if (u32(ctx.lr) != 0x829858ECu)
    return;
  ++g_pass_total[n];
  if (ctx.r3.u32 == 0x6B3BB7F0u) {
    ++g_pass_face[n];
    u32 none = 0;
    g_first_face_swap.compare_exchange_strong(none, n);  // k235: the face_object window starts here
  }
}
}  // namespace
void Ffxiii2SwapNoted() {
  static const bool on = REXCVAR_GET(ffxiii2_diag_pass_counts);
  const u32 n = g_ffxiii2_swap_calls.load(std::memory_order_relaxed);
  if (on && n < kPassCountSwaps) {  // k237: the device's vertical blank count at each Swap (the 3rd field)
    const u32 title = Video::GuestDevice();
    const auto *blanks = title ? GuestPtr<const be_u32>(title + 16952) : nullptr;
    g_ins_count[n] = blanks ? uint16_t(u32(*blanks)) : 0;
  }
  if (!on || n % 300 != 0 || n > kPassCountSwaps)
    return;
  if (FILE *f = std::fopen("capturas/pass_counts.txt", "w")) {
    for (u32 i = 0; i < n; i += 50) {
      std::fprintf(f, "%u:", i);
      for (u32 j = i; j < std::min(n, i + 50); ++j)
        std::fprintf(f, " %u/%u/%u/%02X/%08X/%08X", g_pass_total[j], g_pass_face[j], g_ins_count[j], g_ins_flags[j],
                     g_ins_lr[j], g_ins_lr2[j]);
      std::fprintf(f, "\n");
    }
    std::fclose(f);
  }
}
namespace {
void NoteFfCall(PPCContext &ctx, const char *tag) {
  if (tag[0] == 'p' && tag[4] == '1')
    NotePassCount(ctx);
  static const int32_t at = REXCVAR_GET(ffxiii2_diag_draw_chains_at);
  if (at <= 0)
    return;
  static const u32 frames = u32(std::max<int32_t>(1, REXCVAR_GET(ffxiii2_diag_draw_chains_frames)));
  const u32 title = Video::GuestDevice();
  const auto *sw = title ? GuestPtr<const be_u32>(title + 16968) : nullptr;
  if (!sw)
    return;
  const u32 swaps = g_ffxiii2_swap_calls.load(std::memory_order_relaxed);  // k235: the Swap calls (pass_counts numbering)
  if (swaps < u32(at) || swaps >= u32(at) + frames)
    return;
  const std::string key = fmt::format("{} lr {:08X} r3 {:08X} r4 {:08X} r5 {:08X} r6 {:08X}", tag, u32(ctx.lr),
                                      ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32);
  std::lock_guard lock(g_chain_mutex);
  if (g_chains_written)
    return;
  auto &counts = g_chains[key];
  counts.resize(frames);
  ++counts[swaps - u32(at)];
}
}  // namespace


// FFXIII-2-Native-Switch P6 item 2 (2026-11-09, draw distance): ffns_diag_node_dump_at = a swap count: during that
// swap FF's mesh items (sub_82972D48) with their transforms and every draw with the vertex shader constants c0-c31,
// to ffns_node_dump.txt (tools/build/p6_node_dump_patch.py). 0 = off.
REXCVAR_DEFINE_INT32(ffns_diag_node_dump_at, 0, "FFNS/Diag",
                     "Diagnostic: at this swap count, one swap of FF's mesh items and draws with their vertex constants "
                     "to ffns_node_dump.txt (0 = off)");
#if REX_PLATFORM_SWITCH
extern "C" const char *RexSwitchLogDir(void);  // the SDK's log folder (app/src/profiler/cpu_profiler_switch.cpp)
#endif
namespace ffns_node {
std::atomic<uint32_t> g_rctx{0};
std::mutex g_mutex;
std::string g_text;
int g_state = 0;  // 0 waiting, 1 recording, 2 written
u32 W(u32 va) {
  const auto *p = GuestPtr<const be_u32>(va);
  return p ? u32(*p) : 0;
}
float F(u32 va) {
  const auto *p = GuestPtr<const be_u32>(va);
  return p ? std::bit_cast<float>(u32(*p)) : NAN;
}
std::string Floats(u32 va, int n) {
  std::string s;
  for (int i = 0; i < n; ++i)
    s += fmt::format(" {:.6g}", F(va + 4 * u32(i)));
  return s;
}
bool Recording() {
  static const int32_t at = REXCVAR_GET(ffns_diag_node_dump_at);
  if (at <= 0)
    return false;
  static std::atomic<bool> done{false};
  if (done.load(std::memory_order_relaxed))
    return false;
  const u32 swaps = g_ffxiii2_swap_calls.load(std::memory_order_relaxed);
  if (swaps < u32(at))
    return false;
  std::lock_guard lock(g_mutex);
  if (g_state == 2)
    return false;
  if (swaps > u32(at)) {
    g_state = 2;
    done.store(true, std::memory_order_relaxed);
#if REX_PLATFORM_SWITCH
    const std::string path = std::string(RexSwitchLogDir()) + "ffns_node_dump.txt";  // <NRO folder>/logs/rex/
#else
    const std::string path = "ffns_node_dump.txt";
#endif
    if (FILE *f = std::fopen(path.c_str(), "w")) {
      std::fputs(g_text.c_str(), f);
      std::fclose(f);
    }
    RGPU_INFO("[ffns] node dump written: {} bytes", g_text.size());
    return false;
  }
  if (g_state == 0) {
    g_state = 1;
    g_text += fmt::format("# ffns node dump, swap {}\n", at);
  }
  return true;
}
void Append(const std::string &s) {
  std::lock_guard lock(g_mutex);
  if (g_state == 1)
    g_text += s;
}
u32 Draws() { return g_note_calls[0].load(std::memory_order_relaxed) + g_note_calls[1].load(std::memory_order_relaxed); }
void NoteDraw(PPCContext &ctx, char kind) {
  if (!Recording())
    return;
  const u32 dev = ctx.r3.u32;
  const u32 rctx = g_rctx.load(std::memory_order_relaxed);
  const u32 node = rctx ? W(rctx + 804) : 0;
  const u32 vsf = dev + u32(offsetof(d3d::Device, m_Constants) + offsetof(d3d::Constants, VertexShaderF));
  // the guest call chain (saved LR of 6 frames up)
  std::string chain;
  u32 sp = ctx.r1.u32;
  for (int depth = 0; depth < 6; ++depth) {
    const u32 up = W(sp);
    if (up <= sp || up - sp > 0x10000u)
      break;
    chain += fmt::format(" {:08X}", W(up - 8));
    if ((up & 0xFFFu) == 0)
      break;
    sp = up;
  }
  Append(fmt::format("D {} dev {:08X} z {:08X} lr {:08X} node {:08X} vt {:08X} args {:X} {:X} {:X} {:X} chain{} |",
                     kind, dev, g_ffns_diag_depth.load(std::memory_order_relaxed), u32(ctx.lr), node,
                     node ? W(node) : 0, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32, ctx.r7.u32, chain) +
         Floats(vsf, 128) + "\n");
}
}  // namespace ffns_node

#if KATAMARI_RENUT_HOOKS
#define FFXIII2_CALL_NOTE(sub, tag)            \
  REX_EXTERN(__imp__##sub);                    \
  extern "C" REX_FUNC(sub) {                   \
    NoteFfCall(ctx, tag);                      \
    __imp__##sub(ctx, base);                   \
  }
// FFXIII-2-Native-Switch (2026-11-10, the options menu's "Draw distance"): FF's placed mesh items (people, props: the
// item's model -> transform at +136, its matrix at +48 with the position in the last row) farther than this many metres
// from the camera (the render context's camera matrix at +480, its position in the last column) are not drawn in the
// scene passes (the list walk's call sites 829858E8 / 82985A94). Items without a placed transform (the town, the
// landscape) are always drawn, as are the shadow passes' items. 0 = the original. NOT pixel-identical.
REXCVAR_DEFINE_INT32(ffns_draw_distance, 0, "FFNS/Graphics",
                     "Draw distance for FF's placed objects in metres (0 = the original; NOT pixel-identical)");
namespace {
bool FfnsBeyondDrawDistance(u32 node, u32 rctx, u32 lr) {
  const int32_t limit = REXCVAR_GET(ffns_draw_distance);
  if (limit <= 0 || (lr != 0x829858ECu && lr != 0x82985A98u))
    return false;
  const auto word = [](u32 va) -> u32 {
    const auto *p = GuestPtr<const be_u32>(va);
    return p ? u32(*p) : 0u;
  };
  const auto real = [&](u32 va) { return std::bit_cast<float>(word(va)); };
  const u32 model = word(node + 344);
  const u32 xf = model ? word(model + 136) : 0;
  if (!xf || !rctx)
    return false;
  const float x = real(xf + 96), y = real(xf + 100), z = real(xf + 104);
  if (x == 0.0f && y == 0.0f && z == 0.0f)
    return false;  // no placed position: the scene's own geometry
  const float cx = real(rctx + 492), cy = real(rctx + 508), cz = real(rctx + 524);
  const float dx = x - cx, dy = y - cy, dz = z - cz;
  const float d2 = dx * dx + dy * dy + dz * dz;
  return std::isfinite(d2) && d2 > float(limit) * float(limit);
}
}  // namespace
// FFXIII-2-Native-Switch P6 item 2: FFXIII2_CALL_NOTE plus the node dump (ffns_diag_node_dump_at).
REX_EXTERN(__imp__sub_82972D48);
extern "C" REX_FUNC(sub_82972D48) {
  NoteFfCall(ctx, "pass1");
  const u32 node = ctx.r3.u32, rctx = ctx.r4.u32;
  if (FfnsBeyondDrawDistance(node, rctx, u32(ctx.lr)))
    return;
  ffns_node::g_rctx.store(rctx, std::memory_order_relaxed);
  const bool rec = ffns_node::Recording();
  std::string pre;
  u32 draws0 = 0;
  if (rec) {
    using ffns_node::W;
    static bool ctx_dumped = false;
    if (!ctx_dumped) {
      ctx_dumped = true;
      ffns_node::Append(fmt::format("C {:08X} |", rctx) + ffns_node::Floats(rctx, 0xC00) + "\n");
    }
    const u32 model = W(node + 344), xf = model ? W(model + 136) : 0;
    pre = fmt::format("N {:08X} lr {:08X} vt {:08X} model {:08X} xf {:08X} m152 {:08X} m172 {:08X} n424 {:.6g} | XF",
                      node, u32(ctx.lr), W(node), model, xf, model ? W(model + 152) : 0, model ? W(model + 172) : 0,
                      ffns_node::F(node + 424)) +
          (xf ? ffns_node::Floats(xf + 48, 16) : std::string(" -"));
    draws0 = ffns_node::Draws();
  }
  __imp__sub_82972D48(ctx, base);
  if (rec)
    ffns_node::Append(pre + fmt::format(" | draws {} | W", ffns_node::Draws() - draws0) +
                      ffns_node::Floats(rctx + 416, 16) + " | V" + ffns_node::Floats(rctx + 480, 16) + "\n");
}
#undef FFXIII2_CALL_NOTE
#endif

#if KATAMARI_RENUT_HOOKS
REX_EXTERN(__imp__sub_82860518);
extern "C" REX_FUNC(sub_82860518) {  // ffxiii2-nx: BANJO_RENUT_HOOK plus NoteDrawChain
  NoteDrawChain(ctx, 'I');
  ffns_node::NoteDraw(ctx, 'I');  // P6 item 2 diagnostic
  if (::banjo::renut_gpu::Active()) {
    ::rex::ppc::HostToGuestFunction<D3DDevice_DrawIndexedVertices_hook>(ctx, base);
    return;
  }
  __imp__sub_82860518(ctx, base);
}
REX_EXTERN(__imp__sub_82860100);
extern "C" REX_FUNC(sub_82860100) {  // ffxiii2-nx: BANJO_RENUT_HOOK plus Ffxiii2QuadDiag in either renderer
  NoteDrawChain(ctx, 'V');
  ffns_node::NoteDraw(ctx, 'V');  // P6 item 2 diagnostic
  Ffxiii2QuadDiag(ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32);
  if (::banjo::renut_gpu::Active()) {
    ::rex::ppc::HostToGuestFunction<D3DDevice_DrawVertices_hook>(ctx, base);
    return;
  }
  __imp__sub_82860100(ctx, base);
}
#endif
BANJO_RENUT_UNHOOKED_HOOK(sub_82860100_registered_above, D3DDevice_DrawVertices_hook);  // D3DDevice_DrawVertices  // N&B sub_8222C7A0  // ffxiii2-nx: the D3D trace (out/ff_hook_map.txt)
BANJO_RENUT_UNHOOKED_HOOK(sub_82860518_registered_above, D3DDevice_DrawIndexedVertices_hook);  // D3DDevice_DrawIndexedVertices  // N&B sub_826567C8  // ffxiii2-nx: the D3D trace (out/ff_hook_map.txt); k234: hooked above
#if 0  // bejeweled3-nx: N&B sub_82230838 (D3DDevice_DrawIndexedTessellatedVertices) is not linked in Bejeweled 3
BANJO_RENUT_UNHOOKED_HOOK(sub_8243C578, D3DDevice_DrawIndexedTessellatedVertices_hook);  // D3DDevice_DrawIndexedTessellatedVertices  // N&B sub_82230838  // ffxiii2-nx: unhooked (out/ff_hook_map.txt)
#endif
#if 0  // bejeweled3-nx: N&B sub_8222E520 (D3DDevice_DrawVerticesUP) is not linked in Bejeweled 3
BANJO_RENUT_HOOK(sub_8285FAD8, D3DDevice_DrawVerticesUP_hook);  // D3DDevice_DrawVerticesUP  // N&B sub_8222E520  // ffxiii2-nx: the D3D trace (out/ff_hook_map.txt)
#endif
// ffxiii2-nx: FFXIII-2 links it: sub_8285FAD8 (device, prim, count, data, stride) = BeginVertices + memcpy (the D3D trace).
BANJO_RENUT_HOOK(sub_8285FAD8, D3DDevice_DrawVerticesUP_hook);  // D3DDevice_DrawVerticesUP
#if 0  // bejeweled3-nx: N&B sub_82656728 (D3DDevice_DrawIndexedVerticesUP) is not linked in Bejeweled 3
BANJO_RENUT_UNHOOKED_HOOK(sub_8243B818, D3DDevice_DrawIndexedVerticesUP_hook);  // D3DDevice_DrawIndexedVerticesUP  // N&B sub_82656728  // ffxiii2-nx: unhooked (out/ff_hook_map.txt)
#endif
BANJO_RENUT_HOOK(sub_8285F618, D3DDevice_BeginVertices_hook);  // D3DDevice_BeginVertices  // N&B sub_8222CC50  // ffxiii2-nx: the D3D trace (out/ff_hook_map.txt)
BANJO_RENUT_HOOK(sub_8286BB18, D3DDevice_ClearF_hook);  // D3DDevice_ClearF  // N&B sub_822811A0  // ffxiii2-nx: the D3D trace (out/ff_hook_map.txt)
BANJO_RENUT_HOOK(sub_8286BC50, D3DDevice_Clear_hook);  // D3DDevice_Clear  // N&B sub_822812C8  // ffxiii2-nx: the D3D trace (out/ff_hook_map.txt)
// peggle1-nx: Peggle 1's D3DDevice_Resolve has N&B's signature and the game calls it directly (Peggle 2's was a
// wrapper around an internal resolve with two more arguments).
// bejeweled3-nx: Bejeweled 3's is Peggle 2's again: the public D3DDevice_Resolve, N&B's signature, is the 19-insn
// wrapper sub_82301020 (the game, Present and sub_8230C2D0 call it), which calls the internal resolve sub_822FFEC8
// with two more arguments (r6 = r7 = 0, the destination texture moved to r8). The hook sits on the wrapper.
BANJO_RENUT_HOOK(sub_8286A988, D3DDevice_Resolve_hook);  // D3DDevice_Resolve  // N&B sub_8222D6C8  // ffxiii2-nx: the D3D trace (out/ff_hook_map.txt)
// ffxiii2-nx (2026-11-04, the battle image split in two halves): FF draws its 3D scene in two predicated tiles (2x
// MSAA 1280x720 does not fit the EDRAM). Zuma's Revenge, the hook map's source, has no tiling, so these stayed unhooked
// and Renut drew every tile's pass into one target with no tile rects. Found in FF's generated code: sub_8285CC20 stores
// the tile count at device +0x3344 and copies the 16-byte rects to +0x3348 (native::off::TileCount / TileRects) in a
// loop over r5/r6 (BeginTiling); sub_8285CF90 reads those rects back, takes the destination texture in r6 and calls
// D3DDevice_Resolve (sub_8286A988) per tile (EndTiling). Both reset the bin mask/select through sub_8284C338.
// tile1 then showed the main pass without depth (every tiled draw: no depth target, depth control 0): N&B's native
// replacements of BeginTiling/EndTiling rewrite the device (ResetAllState, the ring segment, fences) with N&B's
// layout and steps, and the sky, drawn last, covered Chaos Bahamut. So FF's own functions run, and the renderer is only
// told the tiles: their rects after BeginTiling (the targets cover every tile, draws predicated to one tile are cut to
// it, ComputeViewport) and the end after EndTiling. N&B's replacements stay compiled, unused.
BANJO_RENUT_UNHOOKED_HOOK(ffxiii2_n_and_b_begin_tiling, D3DDevice_BeginTiling_hook);
BANJO_RENUT_UNHOOKED_HOOK(ffxiii2_n_and_b_end_tiling, D3DDevice_EndTiling_hook);
BANJO_RENUT_HOOK_RAW(sub_8285CC20) {  // D3DDevice_BeginTiling(device, flags, count, rects, ...)
  const u32 device = ctx.r3.u32, count = ctx.r5.u32, rects_va = ctx.r6.u32;
  if (!IsRecordingCommandBuffer(device))
    FlushDeferredVertices(device);
  __imp__sub_8285CC20(ctx, base);
  if (IsRecordingCommandBuffer(device))
    return;
  std::vector<ClearRect> tiles;
  u32 width = 0, height = 0;
  for (u32 i = 0; i < count && i < 16; ++i) {
    ClearRect rect;
    if (!ReadRect(rects_va + i * 16, rect))
      break;
    tiles.push_back(rect);
    width = std::max(width, u32(rect.x2));
    height = std::max(height, u32(rect.y2));
  }
  static uint32_t logged = 0;
  if (logged < 4) {
    ++logged;
    RGPU_INFO("[ffxiii2] BeginTiling: {} tiles, extent {}x{}, first ({},{})-({},{})", tiles.size(), width, height,
              tiles.empty() ? 0 : tiles[0].x1, tiles.empty() ? 0 : tiles[0].y1, tiles.empty() ? 0 : tiles[0].x2,
              tiles.empty() ? 0 : tiles[0].y2);
  }
  render_thread::Queue([tiles, width, height] {
    SetTilingExtentLocked(width, height);
    SetTilesLocked(tiles.data(), u32(tiles.size()));
  });
}
// ffxiii2-nx (2026-11-06, the opening cutscene: Lightning's face drawn every other frame). FF issues occlusion queries
// (D3DQuery_Issue sub_828546A0, D3DQuery_GetData sub_82854950) and draws the character's depth pre-pass and its shadow
// casters only when the query says it is visible ([drawlog] intro_c: those draws in every other frame only; the colour
// draws then fail the depth test). On the console the GPU answers an Issue(END) with an EVENT_WRITE_ZPD that writes the
// sample counts into the query's struct; Renut does not run the game's ring, so nothing ever answered and the struct
// held whatever the last CPU-side write left (D3D's own -275 "pending" marker or old counts). As the SDK's command
// processor does (query_occlusion_fake_sample_count), an Issue(END) of an occlusion query now answers at once: the
// counts zeroed, ZPass A and Total A = ffxiii2_renut_occlusion_samples (little-endian, as the GPU writes them).
REXCVAR_DEFINE_BOOL(ffxiii2_renut_occlusion_results, true, "FFXIII2",
                    "Renut only: occlusion queries are answered at Issue(END) with a fixed sample count (as the SDK's "
                    "command processor does); off: never answered (the intro's face drawn every other frame)");
REXCVAR_DEFINE_INT32(ffxiii2_renut_occlusion_samples, 1000, "FFXIII2",
                     "Renut only: the sample count ffxiii2_renut_occlusion_results reports");
BANJO_RENUT_HOOK_RAW(sub_828546A0) {  // D3DQuery_Issue(query, flags)
  const u32 query = ctx.r3.u32, flags = ctx.r4.u32;
  __imp__sub_828546A0(ctx, base);
  {
    static uint32_t calls_logged = 0;
    const auto *qq = GuestPtr<const be_u32>(query);
    if (calls_logged++ < 12)
      RGPU_INFO("[ffxiii2] D3DQuery_Issue {:08X} flags {:X} type {} n {} counts {:08X} {:08X}", query, flags,
                qq ? u32(qq[1]) : 0, qq ? u32(qq[37]) : 0, qq ? u32(qq[6]) : 0, qq ? u32(qq[7]) : 0);
  }
  static const bool on = REXCVAR_GET(ffxiii2_renut_occlusion_results);
  if (!on || !(flags & 1))
    return;
  const auto *q = GuestPtr<const be_u32>(query);
  if (!q || u32(q[1]) != 9)  // +4: the query type, 9 = D3DQUERYTYPE_OCCLUSION
    return;
  // +148: Issue(BEGIN) stores the number of sample-count structs there (1, or the tile count while tiling); the
  // structs' addresses follow at +24; Issue(END) reads the one at +((n + 6) * 4). All of them are answered.
  const u32 index = u32(q[37]);
  if (index > 32)
    return;
  const u32 samples = u32(std::max<int32_t>(0, REXCVAR_GET(ffxiii2_renut_occlusion_samples)));
  const u32 le = ((samples & 0xFF) << 24) | ((samples & 0xFF00) << 8) | ((samples >> 8) & 0xFF00) | (samples >> 24);
  u32 counts_va = 0;
  for (u32 k = 0; k <= index; ++k) {
    counts_va = u32(q[k + 6]);
    auto *c = counts_va ? GuestPtr<be_u32>(counts_va) : nullptr;
    if (!c)
      continue;
    for (u32 i = 0; i < 8; ++i)
      c[i] = 0;
    c[0] = le;  // Total A
    c[4] = le;  // ZPass A
  }
  static uint32_t logged = 0;
  if (logged++ < 4)
    RGPU_INFO("[ffxiii2] occlusion query {:08X} answered ({} samples, counts at {:08X}, index {})", query, samples,
              counts_va, index);
}

BANJO_RENUT_HOOK_RAW(sub_8285CF90) {  // D3DDevice_EndTiling
  const u32 device = ctx.r3.u32;
  if (!IsRecordingCommandBuffer(device))
    FlushDeferredVertices(device);
  __imp__sub_8285CF90(ctx, base);
  if (IsRecordingCommandBuffer(device))
    return;
  render_thread::Queue([] {
    SetTilingExtentLocked(0, 0);
    SetTilesLocked(nullptr, 0);
  });
}
BANJO_RENUT_UNHOOKED_HOOK(sub_82318320, D3DDevice_BeginExport_hook);  // D3DDevice_BeginExport  // N&B sub_82269618  // ffxiii2-nx: unhooked (out/ff_hook_map.txt)
BANJO_RENUT_UNHOOKED_HOOK(sub_82318418, D3DDevice_EndExport_hook);  // D3DDevice_EndExport  // N&B sub_822693B0  // ffxiii2-nx: unhooked (out/ff_hook_map.txt)
BANJO_RENUT_UNHOOKED_HOOK(sub_82309B78, D3DDevice_Present_hook);  // D3DDevice_Present  // N&B sub_82654778  // ffxiii2-nx: unhooked (out/ff_hook_map.txt)
BANJO_RENUT_HOOK(sub_82849080, D3DDevice_SetVertexShader_hook);  // D3DDevice_SetVertexShader  // N&B sub_8222A0A8
BANJO_RENUT_HOOK(sub_82848E78, D3DDevice_SetPixelShader_hook);  // D3DDevice_SetPixelShader  // N&B sub_82226730

// FFXIII-2-Native-Switch P6 (2026-11-09): the hook-call trace (banjo_renut_select.h, FFNS_HOOK_TRACE). From the device
// swap count ffns_diag_hook_trace_at, for ffns_diag_hook_trace_frames swaps, every call of a BANJO_RENUT_HOOK /
// BANJO_RENUT_HOOK_RAW hook in order: swap, hook, r3-r6, the caller's LR and 8 frames of the guest call chain;
// ffns_hook_trace.txt next to the program (its working folder). 0 = off.
REXCVAR_DEFINE_INT32(ffns_diag_hook_trace_at, 0, "FFNS/Diag",
                     "Diagnostic: trace Renut's hook calls with their guest call chains from this swap count (0 = off)");
REXCVAR_DEFINE_INT32(ffns_diag_hook_trace_frames, 3, "FFNS/Diag", "Diagnostic: ffns_diag_hook_trace_at, swaps traced");
namespace ffns_diag {
bool g_hook_trace_on = true;
namespace {
std::mutex g_trace_mutex;
std::vector<std::string> g_trace;
bool g_trace_done = false;
}  // namespace
void HookCall(const char *name, PPCContext &ctx) {
  static const int32_t at = REXCVAR_GET(ffns_diag_hook_trace_at);
  static const int32_t frames = std::max<int32_t>(1, REXCVAR_GET(ffns_diag_hook_trace_frames));
  if (at <= 0) {
    g_hook_trace_on = false;
    return;
  }
  const u32 swaps = g_ffxiii2_swap_calls.load(std::memory_order_relaxed);
  if (swaps < u32(at))
    return;
  std::lock_guard lock(g_trace_mutex);
  if (g_trace_done)
    return;
  if (swaps >= u32(at) + u32(frames)) {
    g_trace_done = true;
    g_hook_trace_on = false;
    if (FILE *f = std::fopen("ffns_hook_trace.txt", "w")) {
      std::fprintf(f, "# swap hook r3 r4 r5 r6 | lr chain (ffns_diag_hook_trace_at %d, %d swaps, %zu calls)\n", at,
                   frames, g_trace.size());
      for (const auto &l : g_trace)
        std::fprintf(f, "%s\n", l.c_str());
      std::fclose(f);
    }
    RGPU_INFO("[ffns] hook trace written: {} calls in {} swaps", g_trace.size(), frames);
    return;
  }
  static const auto t0 = std::chrono::steady_clock::now();
  const auto us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count();
  thread_local const size_t tid = std::hash<std::thread::id>{}(std::this_thread::get_id()) % 100000;
  std::string line = fmt::format("{} {} {:08X} {:08X} {:08X} {:08X} t{} @{} | {:08X}", swaps, name, ctx.r3.u32,
                                 ctx.r4.u32, ctx.r5.u32, ctx.r6.u32, tid, us, u32(ctx.lr));
  u32 sp = ctx.r1.u32;
  for (int depth = 0; depth < 8; ++depth) {
    const auto *p = GuestPtr<const be_u32>(sp);
    if (!p)
      break;
    const u32 up = u32(*p);
    if (up <= sp || up - sp > 0x10000u)
      break;
    const auto *l = GuestPtr<const be_u32>(up - 8);
    if (!l)
      break;
    line += fmt::format(" {:08X}", u32(*l));
    if ((up & 0xFFFu) == 0)
      break;
    sp = up;
  }
  if (g_trace.size() < 200000)
    g_trace.push_back(std::move(line));
}
}  // namespace ffns_diag


// FFXIII-2-Native-Switch P6 (2026-11-09): job census (tools/build/p6_job_census_patch.py). ffns_job_census = true:
// every job of FF's job system (sub_8290FC60: job +8 = callback) timed by thread and callback, inclusive and without
// nested jobs; every 10 s "[jobs]" lines: per thread, then the top 30 (thread, callback) pairs, in ms per frame.
REXCVAR_DEFINE_BOOL(ffns_job_census, false, "FFNS/Diag",
                    "Diagnostic: time FF's jobs by thread and callback, log the totals every 10 s (renut log)");
REXCVAR_DEFINE_INT32(ffns_job_timeline_at, 0, "FFNS/Diag",
                     "Diagnostic: at this swap count, every job of one swap with its thread, start and end time to "
                     "ffns_job_timeline.txt (logs/rex on the Switch; 0 = off)");
#if REX_PLATFORM_SWITCH
extern "C" const char *RexSwitchLogDir(void);
#endif
namespace ffns_jobs {
struct Span {
  uint32_t key, fn, links;
  uint64_t t0, t1;
};
std::mutex g_tl_mutex;
std::vector<Span> g_tl;
std::atomic<int> g_tl_state{0};  // 0 waiting, 1 recording, 2 written
uint64_t g_tl_start = 0;
void Timeline(const Span &span) {
  static const int32_t at = REXCVAR_GET(ffns_job_timeline_at);
  if (at <= 0 || g_tl_state.load(std::memory_order_relaxed) == 2)
    return;
  const u32 swaps = g_ffxiii2_swap_calls.load(std::memory_order_relaxed);
  if (swaps < u32(at))
    return;
  std::lock_guard lock(g_tl_mutex);
  if (g_tl_state.load(std::memory_order_relaxed) == 2)
    return;
  if (swaps > u32(at) + 1) {
    g_tl_state.store(2, std::memory_order_relaxed);
#if REX_PLATFORM_SWITCH
    const std::string path = std::string(RexSwitchLogDir()) + "ffns_job_timeline.txt";
#else
    const std::string path = "ffns_job_timeline.txt";
#endif
    if (FILE *f = std::fopen(path.c_str(), "w")) {
      std::fprintf(f, "# swaps %d-%d: thread fn links start_us end_us (from the first job)\n", at, at + 1);
      for (const Span &e : g_tl)
        std::fprintf(f, "%08X %08X %04X %.1f %.1f\n", e.key, e.fn, e.links, (e.t0 - g_tl_start) / 1e3,
                     (e.t1 - g_tl_start) / 1e3);
      std::fclose(f);
    }
    RGPU_INFO("[jobs] timeline written: {} jobs", g_tl.size());
    return;
  }
  if (g_tl_state.load(std::memory_order_relaxed) == 0) {
    g_tl_state.store(1, std::memory_order_relaxed);
    g_tl_start = span.t0;
  }
  g_tl.push_back(span);
}
struct Sum {
  uint64_t incl = 0, excl = 0;
  uint32_t n = 0;
};
std::mutex g_mutex;
// (thread key, the job's function (job +16), the job runner's callback (job +8), the job's list links mod 40 (which of
// the phase's five lists it came from: main, workers 1-3, any))
std::map<std::array<uint32_t, 4>, Sum> g_sums;
std::chrono::steady_clock::time_point g_window{};
uint32_t g_window_swaps = 0;
thread_local uint32_t t_key = 0;  // guest thread start address | 2, 1 for the main thread
thread_local uint32_t t_tid = 0;  // the guest thread id (the timeline tells the workers apart)
thread_local uint64_t t_child = 0;
inline uint64_t NowNs() {
  return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                      std::chrono::steady_clock::now().time_since_epoch())
                      .count());
}
void Record(const std::array<uint32_t, 4> &key, uint64_t incl, uint64_t excl) {
  std::lock_guard lock(g_mutex);
  const auto now = std::chrono::steady_clock::now();
  const u32 swaps = g_ffxiii2_swap_calls.load(std::memory_order_relaxed);
  if (g_window.time_since_epoch().count() == 0) {
    g_window = now;
    g_window_swaps = swaps;
  }
  auto &s = g_sums[key];
  s.incl += incl;
  s.excl += excl;
  s.n += 1;
  if (now - g_window < std::chrono::seconds(10))
    return;
  const double frames = std::max<double>(1.0, double(swaps - g_window_swaps));
  std::map<uint32_t, Sum> threads;
  std::vector<std::pair<std::array<uint32_t, 4>, Sum>> all(g_sums.begin(), g_sums.end());
  for (auto &kv : all) {
    auto &t = threads[kv.first[0]];
    t.excl += kv.second.excl;
    t.n += kv.second.n;
  }
  RGPU_INFO("[jobs] {:.0f} frames: per thread (start address, 1 = main), ms of job work per frame", frames);
  for (auto &kv : threads)
    RGPU_INFO("[jobs]   thread {:08X}: {:7.2f} ms/frame in {:.1f} jobs/frame", kv.first, kv.second.excl / 1e6 / frames,
              kv.second.n / frames);
  std::sort(all.begin(), all.end(), [](auto &a, auto &b) { return a.second.excl > b.second.excl; });
  for (size_t i = 0; i < all.size() && i < 40; ++i)
    RGPU_INFO("[jobs]   {:08X} fn {:08X} cb {:08X} links {:04X}: excl {:6.2f} incl {:6.2f} ms/frame, {:5.1f} jobs/frame",
              all[i].first[0], all[i].first[1], all[i].first[2], all[i].first[3], all[i].second.excl / 1e6 / frames,
              all[i].second.incl / 1e6 / frames, all[i].second.n / frames);
  g_sums.clear();
  g_window = now;
  g_window_swaps = swaps;
}
}  // namespace ffns_jobs
#if KATAMARI_RENUT_HOOKS
REX_EXTERN(__imp__sub_8290FC60);
#if !REX_PLATFORM_SWITCH
extern "C" bool FfnsRaceProbeOn();                               // app/src/native/ffns_race_probe_win.cpp (P7)
extern "C" void FfnsRaceProbeJob(uint32_t fn, int begin, uint32_t r1);
#endif
extern "C" REX_FUNC(sub_8290FC60) {
#if !REX_PLATFORM_SWITCH
  static const bool probe = FfnsRaceProbeOn();
#else
  static const bool probe = false;
#endif
  static const bool on = REXCVAR_GET(ffns_job_census) || REXCVAR_GET(ffns_job_timeline_at) > 0 || probe;
  if (!on) {
    __imp__sub_8290FC60(ctx, base);
    return;
  }
  using namespace ffns_jobs;
  if (t_key == 0) {
    auto *thread = rex::system::XThread::GetCurrentThread();
    t_key = thread ? (thread->main_thread() ? 1u : (thread->creation_params()->start_address | 2u)) : 3u;
    t_tid = thread ? thread->thread_id() : 0u;
  }
  const auto *job = GuestPtr<const be_u32>(ctx.r4.u32);
  const std::array<uint32_t, 4> key = {t_key, job ? u32(job[4]) : 0u, job ? u32(job[2]) : 0u,
                                       job ? ((u32(job[0]) % 40u) << 8 | (u32(job[1]) % 40u)) : 0u};
  const uint64_t saved_child = t_child;
  t_child = 0;
  const uint64_t t0 = NowNs();
#if !REX_PLATFORM_SWITCH
  const uint32_t probe_r1 = ctx.r1.u32;
  if (probe)
    FfnsRaceProbeJob(key[1], 1, probe_r1);
#endif
  __imp__sub_8290FC60(ctx, base);
#if !REX_PLATFORM_SWITCH
  if (probe)
    FfnsRaceProbeJob(key[1], 0, probe_r1);
#endif
  const uint64_t dt = NowNs() - t0;
  const uint64_t excl = dt > t_child ? dt - t_child : 0;
  t_child = saved_child + dt;
  static const bool census = REXCVAR_GET(ffns_job_census);
  if (census)
    Record(key, dt, excl);
  Timeline(Span{t_key == 1 ? 1u : t_tid, key[1], key[3], t0, t0 + dt});
}
#endif

// FFXIII-2-Native-Switch P6 (2026-11-09, the user's option 3: non-exact changes allowed; item 1, shadows). FF draws its
// two shadow maps (1024x1024, depth only) in sub_82962070, called twice a frame from sub_82962BA8 (its only call site:
// `sub_82962070(ctx, base)`, the hookable name): the first call clears, draws (~20 casters in New Bodhum) and resolves
// to 6D0592FC, the second (~110 casters) to 6D0594FC (the PC hook trace ffns_hook_trace.txt, swaps 7000-7002: chains
// 8296211C <- 829634FC <- 829159AC <- the job system). ffns_shadow_far_every = N > 1 draws the second map only every
// Nth frame: in the others the call returns at once, so its texture keeps the last frame's map. NOT exact: distant
// shadows lag up to N-1 frames. 1 = the original.
REXCVAR_DEFINE_INT32(ffns_shadow_far_every, 1, "FFNS/Performance",
                     "Shadows: draw FF's second (larger) shadow map only every this many frames (1 = every frame, "
                     "the original; NOT pixel-identical above 1)");
namespace {
std::atomic<uint32_t> g_ffns_shadow_frame{~0u};
std::atomic<uint32_t> g_ffns_shadow_calls{0};
std::atomic<uint64_t> g_ffns_shadow_skipped{0};
}  // namespace
#if KATAMARI_RENUT_HOOKS
REX_EXTERN(__imp__sub_82962070);
extern "C" REX_FUNC(sub_82962070) {
  const int32_t every = std::max<int32_t>(1, REXCVAR_GET(ffns_shadow_far_every));  // menu: changes while playing
  if (every > 1) {
    const u32 swaps = g_ffxiii2_swap_calls.load(std::memory_order_relaxed);
    if (g_ffns_shadow_frame.exchange(swaps, std::memory_order_relaxed) != swaps)
      g_ffns_shadow_calls.store(0, std::memory_order_relaxed);
    const u32 index = g_ffns_shadow_calls.fetch_add(1, std::memory_order_relaxed);
    // 2026-11-12 (the user: the characters' shadow flickers, is misplaced or disappears on the field): the map was told
    // by the call's place in the frame (the second call since the last Swap). The calls come from the job system, not in
    // step with the Swap, so a Swap between a frame's two calls made the NEXT frame's first call "the second": the near
    // map (the characters' shadows) was left out for a frame. Now the far map is told by its object (r3 of the call: one
    // object a map, sub_82962BA8): the object that is mostly the second call, and it is skipped by its own count.
    struct Map { u32 object; u32 second, first, draws; };
    static Map maps[4] = {};
    static std::mutex maps_mutex;
    static std::atomic<uint64_t> near_saved{0};
    bool skip = false;
    {
      std::lock_guard<std::mutex> lock(maps_mutex);
      Map *m = nullptr;
      for (Map &e : maps)
        if (e.object == ctx.r3.u32 || !e.object) { m = &e; break; }
      if (m) {
        m->object = ctx.r3.u32;
        if (index == 1) ++m->second; else ++m->first;
        if (m->second + m->first > 100000) { m->second /= 2; m->first /= 2; }
        const bool far_map = m->second + m->first >= 16 && m->second > 3 * m->first;
        skip = far_map && (m->draws++ % u32(every)) != 0;
        if (!far_map && index == 1 && swaps % u32(every) != 0)
          near_saved.fetch_add(1, std::memory_order_relaxed);  // the old rule would have left this one out
      }
    }
    if (skip) {
      const uint64_t n = g_ffns_shadow_skipped.fetch_add(1, std::memory_order_relaxed) + 1;
      if (n == 1 || n % 1000 == 0)
        RGPU_INFO("[ffns] ffns_shadow_far_every {}: far shadow map skipped {} times; other maps the old rule (second "
                  "call of the frame) would have skipped: {}", every, n, near_saved.load(std::memory_order_relaxed));
      return;
    }
  }
  __imp__sub_82962070(ctx, base);
}
#endif
