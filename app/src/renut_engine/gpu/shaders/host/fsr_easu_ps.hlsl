// FFXIII-2-Native-Switch (2026-11-10, the options menu's "Upscaling": FSR 1, ffns_fsr; render/fsr.cpp): FSR 1's EASU
// (edge-adaptive spatial upsampling) from the frame at the render resolution into a target at the size it is shown at.
// A port of SwitchWaker's switch/deko/shaders/dk_fsr_easu.wgsl (MIT), itself a port of AMD FidelityFX FSR 1.0's FsrEasuF
// (ffx_fsr1.h, MIT license, Copyright (c) 2021 Advanced Micro Devices, Inc.): 12 point-sampled taps instead of the
// gathers, exact divisions instead of the approximate reciprocals.
// present_vs.hlsl's triangle: uv = the output pixel's place in the frame's texture (scale/translate: the frame's region
// less what the fit trims, as the scan-out samples it).
//   textureIndex the frame (read with Load: texel fetches, no sampler; samplerIndex unused)
//   param0 bits: the frame's width | height << 16 (texels: the taps stay inside it)
//   param1 bits: the texture's width | height << 16
#include "utility.hlsli"

// 2026-11-10 (vis4_p9a_fsr: EASU + RCAS cost the GPU 6.2 ms a frame at 1280x720): texel fetches instead of samples at
// texel centres (the same texels, without the sampler's coordinate work).
float3 Tap(int2 base, int2 o, int2 last)
{
    const int2 c = clamp(base + o, int2(0, 0), last);
    return g_Textures[g_Push.textureIndex].Load(int3(c, 0)).rgb;
}

float Luma(float3 c)
{
    return c.b * 0.5 + (c.r * 0.5 + c.g);
}

// The direction and length of the edge, from one of the four bilinear cells' weight w and the lumas above, left,
// centre, right and below the pixel it is about (FsrEasuSetF).
void EasuSet(inout float2 dir, inout float len, float w, float lA, float lB, float lC, float lD, float lE)
{
    const float dc = lD - lC;
    const float cb = lC - lB;
    const float lenX = 1.0 / max(max(abs(dc), abs(cb)), 1.0e-8);
    const float dirX = lD - lB;
    dir.x += dirX * w;
    const float sx = saturate(abs(dirX) * lenX);
    len += sx * sx * w;
    const float ec = lE - lC;
    const float ca = lC - lA;
    const float lenY = 1.0 / max(max(abs(ec), abs(ca)), 1.0e-8);
    const float dirY = lE - lA;
    dir.y += dirY * w;
    const float sy = saturate(abs(dirY) * lenY);
    len += sy * sy * w;
}

// One tap's Lanczos-like weight (FsrEasuTapF); off is the tap's offset from the output pixel in input texels.
void EasuTap(inout float3 acc, inout float wsum, float2 off, float2 dir, float2 len2, float lob, float clp, float3 c)
{
    float2 v = float2(off.x * dir.x + off.y * dir.y, off.x * (-dir.y) + off.y * dir.x);
    v *= len2;
    const float d2 = min(v.x * v.x + v.y * v.y, clp);
    float wB = 0.4 * d2 - 1.0;
    float wA = lob * d2 - 1.0;
    wB *= wB;
    wA *= wA;
    wB = 1.5625 * wB - 0.5625;
    const float w = wB * wA;
    acc += c * w;
    wsum += w;
}

float4 main(float4 position : SV_Position, float2 uv : TEXCOORD0) : SV_Target0
{
    const uint frame = asuint(g_Push.param0);
    const uint tex = asuint(g_Push.param1);
    const float2 in_size = float2(float(tex & 0xFFFFu), float(tex >> 16));
    const int2 last = int2(int(frame & 0xFFFFu), int(frame >> 16)) - 1;

    // The output pixel's place in the input, in texels, relative to the centre of cell f.
    const float2 pp0 = uv * in_size - 0.5;
    const float2 fp = floor(pp0);
    const float2 pp = pp0 - fp;
    const int2 base = int2(fp);

    //    b c
    //  e f g h
    //  i j k l
    //    n o
    const float3 cb = Tap(base, int2(0, -1), last);
    const float3 cc = Tap(base, int2(1, -1), last);
    const float3 ce = Tap(base, int2(-1, 0), last);
    const float3 cf = Tap(base, int2(0, 0), last);
    const float3 cg = Tap(base, int2(1, 0), last);
    const float3 ch = Tap(base, int2(2, 0), last);
    const float3 ci = Tap(base, int2(-1, 1), last);
    const float3 cj = Tap(base, int2(0, 1), last);
    const float3 ck = Tap(base, int2(1, 1), last);
    const float3 cl = Tap(base, int2(2, 1), last);
    const float3 cn = Tap(base, int2(0, 2), last);
    const float3 co = Tap(base, int2(1, 2), last);
    const float lb = Luma(cb);
    const float lc = Luma(cc);
    const float le = Luma(ce);
    const float lf = Luma(cf);
    const float lg = Luma(cg);
    const float lh = Luma(ch);
    const float li = Luma(ci);
    const float lj = Luma(cj);
    const float lk = Luma(ck);
    const float ll = Luma(cl);
    const float ln = Luma(cn);
    const float lo = Luma(co);

    // The edge's direction and length: the four bilinear cells around the pixel, weighted.
    float2 dir = float2(0.0, 0.0);
    float len = 0.0;
    EasuSet(dir, len, (1.0 - pp.x) * (1.0 - pp.y), lb, le, lf, lg, lj);
    EasuSet(dir, len, pp.x * (1.0 - pp.y), lc, lf, lg, lh, lk);
    EasuSet(dir, len, (1.0 - pp.x) * pp.y, lf, li, lj, lk, ln);
    EasuSet(dir, len, pp.x * pp.y, lg, lj, lk, ll, lo);

    // Normalise, with the flat case pointing along x.
    const float dirR2 = dir.x * dir.x + dir.y * dir.y;
    const bool zro = dirR2 < (1.0 / 32768.0);
    const float dirR = zro ? 1.0 : rsqrt(dirR2);
    dir = float2(zro ? 1.0 : dir.x, dir.y) * dirR;
    // Length {0 to 2} -> {0 to 1}, shaped with a square.
    len *= 0.5;
    len *= len;
    // Stretch the kernel: 1 along an axis, sqrt(2) on a diagonal.
    const float stretch = (dir.x * dir.x + dir.y * dir.y) / max(abs(dir.x), abs(dir.y));
    // Anisotropic lengths after the rotation: x along the edge, y across it.
    const float2 len2 = float2(1.0 + (stretch - 1.0) * len, 1.0 - 0.5 * len);
    // The window shifts from +-sqrt(2) to just beyond 2 with the amount of edge.
    const float lob = 0.5 + ((1.0 / 4.0 - 0.04) - 0.5) * len;
    const float clp = 1.0 / lob;

    // The four nearest texels bound the result (no ringing beyond them).
    const float3 min4 = min(min(cf, cg), min(cj, ck));
    const float3 max4 = max(max(cf, cg), max(cj, ck));
    float3 acc = float3(0.0, 0.0, 0.0);
    float wsum = 0.0;
    EasuTap(acc, wsum, float2(0.0, -1.0) - pp, dir, len2, lob, clp, cb);
    EasuTap(acc, wsum, float2(1.0, -1.0) - pp, dir, len2, lob, clp, cc);
    EasuTap(acc, wsum, float2(-1.0, 0.0) - pp, dir, len2, lob, clp, ce);
    EasuTap(acc, wsum, float2(0.0, 0.0) - pp, dir, len2, lob, clp, cf);
    EasuTap(acc, wsum, float2(1.0, 0.0) - pp, dir, len2, lob, clp, cg);
    EasuTap(acc, wsum, float2(2.0, 0.0) - pp, dir, len2, lob, clp, ch);
    EasuTap(acc, wsum, float2(-1.0, 1.0) - pp, dir, len2, lob, clp, ci);
    EasuTap(acc, wsum, float2(0.0, 1.0) - pp, dir, len2, lob, clp, cj);
    EasuTap(acc, wsum, float2(1.0, 1.0) - pp, dir, len2, lob, clp, ck);
    EasuTap(acc, wsum, float2(2.0, 1.0) - pp, dir, len2, lob, clp, cl);
    EasuTap(acc, wsum, float2(0.0, 2.0) - pp, dir, len2, lob, clp, cn);
    EasuTap(acc, wsum, float2(1.0, 2.0) - pp, dir, len2, lob, clp, co);
    return float4(min(max4, max(min4, acc / wsum)), 1.0);
}
