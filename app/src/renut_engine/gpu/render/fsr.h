// FFXIII-2-Native-Switch (2026-11-10, the user: "Integrate FSR as an option in the menu"): AMD FidelityFX FSR 1 (EASU +
// RCAS) for the scan-out, when the frame is drawn under the size it is shown at (dynamic resolution). render/fsr.cpp.
#pragma once

#include <cstdint>

#include <plume_render_interface.h>

namespace renut::gpu {

struct VideoState;
struct PresentImage;

// ffns_fsr > 0 (the options menu's "Upscaling": 1 bilinear + RCAS, 2 FSR 1), read every frame.
bool FsrWanted();

// Outside any framebuffer, before the swapchain's pass: EASU from the frame's region (scale/translate in the frame
// texture's uv, as the scan-out samples it; src_w/src_h the frame's size in texels) into a target at the viewport's
// size. False (nothing recorded) when off, not an upscale, or a shader or pipeline could not be made: the scan-out then
// draws as before. scan_out_format: the swapchain's format (the RCAS pipeline's).
bool FsrEasuLocked(VideoState &s, const PresentImage &image, const plume::RenderViewport &viewport, const float scale[2],
                   const float translate[2], uint32_t src_w, uint32_t src_h, plume::RenderFormat scan_out_format);

// Inside the swapchain's pass, after FsrEasuLocked returned true: RCAS of the EASU target into the viewport, with the
// scan-out's gamma / display ramp parameters (present_ps.hlsl's param0 / param1).
void FsrScanOutLocked(VideoState &s, const plume::RenderViewport &viewport, plume::RenderFormat scan_out_format,
                      float param0, float param1);

}  // namespace renut::gpu
