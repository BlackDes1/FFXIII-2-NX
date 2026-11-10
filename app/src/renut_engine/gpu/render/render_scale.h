/**
 * @file    gpu/render/render_scale.h
 * @brief   Render resolution: host pixels per guest pixel.
 */
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace renut::gpu {

// Host pixels per guest pixel of a render target, and of what resolves from
// it, as one of a fixed set of ratios: the render resolutions
// (renut_resolution: 1x, 480p, 1080p, 1440p, 4K) and the targets that render
// finer than the rest (graphics_options.h: shadow maps at 4x, ambient
// occlusion at twice the resolution: 4/3, 6x). Code 0 is 1x, the scale of all
// content that is not rendered (guest textures). Shaders get the code in the
// descriptor index (kResolutionScaleShift) and have the same factors
// (renutTextureScale in shader_common.h).
// ffxiii2-nx (2026-11-07, dynamic resolution; the design of renut-nx's banjo_renut_dynamic_resolution,
// docs/enhancements-reference.md "Dynamic resolution"): codes 4 and 6 can stand for a render height of the deko3d
// renderer's choosing instead of 3x and 6x (PC-only scales). The GLSL package's scale chain reads codes 4 and 6 as
// SharedConstants v[84].w's heights / 720 (tools/deko3d_glsl_opt.py, pass scalecodes; render/draw.cpp writes them), so
// a height here and there is the same factor. Set once, before any target or texture of that code exists
// (StartDynamicResolutionLocked, surfaces.cpp), and never changed afterwards: a host surface's size is its code's factor times its
// guest size, so a code's meaning must not change while anything holds it. 0 = the static factor.
inline uint32_t g_render_scale_dynamic_height[2] = {0, 0};  // code 4, code 6

struct RenderScale {
  static constexpr uint32_t kCount = 8;
  static constexpr uint32_t kNumerators[kCount] = {1, 2, 3, 2, 3, 4, 6, 4};
  static constexpr uint32_t kDenominators[kCount] = {1, 3, 2, 1, 1, 1, 1, 3};
  static constexpr uint32_t kGuestHeight = 720;

  uint32_t code = 0;

  // The height a dynamic code stands for (0: the code is static).
  static uint32_t DynamicHeight(uint32_t c) {
    return c == 4 ? g_render_scale_dynamic_height[0] : c == 6 ? g_render_scale_dynamic_height[1] : 0;
  }
  static uint32_t Numerator(uint32_t c) {
    const uint32_t h = DynamicHeight(c);
    return h ? h : kNumerators[c];
  }
  static uint32_t Denominator(uint32_t c) { return DynamicHeight(c) ? kGuestHeight : kDenominators[c]; }

  float Factor() const { return float(Numerator(code)) / float(Denominator(code)); }
  bool operator==(const RenderScale &) const = default;

  // A guest pixel edge in host pixels, to the nearest: rects that share an
  // edge in guest pixels share it in host pixels.
  int32_t Coord(int32_t guest) const {
    return int32_t(std::lround(double(guest) * Numerator(code) / Denominator(code)));
  }

  // A guest size in host pixels, 1 at least.
  uint32_t Size(uint32_t guest) const {
    return std::max<uint32_t>(uint32_t(Coord(int32_t(guest))), 1u);
  }

  // The scale of ratio numerator / denominator, if it is one of them.
  static bool FromRatio(uint32_t numerator, uint32_t denominator, RenderScale &out) {
    for (uint32_t c = 0; c < kCount; ++c) {
      if (uint64_t(numerator) * Denominator(c) == uint64_t(Numerator(c)) * denominator) {
        out.code = c;
        return true;
      }
    }
    return false;
  }

  // This scale times 'multiplier', or this one when that is not among them.
  RenderScale Times(uint32_t multiplier) const {
    RenderScale out = *this;
    FromRatio(Numerator(code) * multiplier, Denominator(code), out);
    return out;
  }
};

} // namespace renut::gpu
