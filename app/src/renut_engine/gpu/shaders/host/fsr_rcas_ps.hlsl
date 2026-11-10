// FFXIII-2-Native-Switch (2026-11-10, ffns_fsr; render/fsr.cpp): FSR 1's RCAS (robust contrast-adaptive sharpening) of
// the EASU picture, drawn as the scan-out to the swapchain, then the scan-out's display ramp or gamma (present_ps.hlsl).
// A port of SwitchWaker's switch/deko/shaders/dk_fsr_rcas.wgsl (MIT), itself a port of AMD FidelityFX FSR 1.0's FsrRcasF
// (ffx_fsr1.h, MIT license, Copyright (c) 2021 Advanced Micro Devices, Inc.), without the optional noise removal and with
// exact divisions.
// smaa_vs.hlsl's triangle: uv 0..1 over the shown picture's viewport = the EASU target, 1:1.
//   textureIndex the EASU target (read with Load: texel fetches; samplerIndex unused)
//   scale        1 / the EASU target's width and height
//   translate.x  the lobe's scale, 2^-stops (1 sharpest)
//   param0/param1 as present_ps.hlsl: the output gamma exponent, or -1 and the display ramp table's slot
#include "utility.hlsli"

float3 Tap(int2 p, int2 last)
{
    return g_Textures[g_Push.textureIndex].Load(int3(clamp(p, int2(0, 0), last), 0)).rgb;
}

float4 main(float4 position : SV_Position, float2 uv : TEXCOORD0) : SV_Target0
{
    const int2 size = int2(round(1.0 / g_Push.scale));
    const int2 last = size - 1;
    const int2 p = min(int2(uv * float2(size)), last);
    //   b
    // d e f
    //   h
    const float3 b = Tap(p + int2(0, -1), last);
    const float3 d = Tap(p + int2(-1, 0), last);
    const float3 e = Tap(p, last);
    const float3 f = Tap(p + int2(1, 0), last);
    const float3 h = Tap(p + int2(0, 1), last);
    // The ring's extremes.
    const float3 mn4 = min(min(min(b, d), f), h);
    const float3 mx4 = max(max(max(b, d), f), h);
    // The largest lobe that cannot clip: towards black, towards white.
    const float3 hitMin = min(mn4, e) / max(4.0 * mx4, float3(1.0e-5, 1.0e-5, 1.0e-5));
    const float3 hitMax = (1.0 - max(mx4, e)) / min(4.0 * mn4 - 4.0, float3(-1.0e-5, -1.0e-5, -1.0e-5));
    const float3 lobeC = max(-hitMin, hitMax);
    const float lobe = max(-0.1875, min(max(max(lobeC.r, lobeC.g), lobeC.b), 0.0)) * g_Push.translate.x;
    const float rcpL = 1.0 / (4.0 * lobe + 1.0);
    float3 color = saturate((lobe * (b + d + h + f) + e) * rcpL);

    if (g_Push.param0 < -0.5)
    {
        // The display ramp table (present_ps.hlsl): row c of the 256x3 table, high 8 bits in r, low 2 in g.
        const uint ramp = asuint(g_Push.param1);
        const float3 index = floor(color * 255.0 + 0.5);
        float3 result;
        [unroll] for (uint c = 0; c < 3; ++c)
        {
            const float2 tuv = float2((index[c] + 0.5) / 256.0, (float(c) + 0.5) / 3.0);
            const float2 v = SampleBindless(ramp, 1u /* kPointClampSamplerSlot */, tuv).rg;
            result[c] = (round(v.x * 255.0) * 4.0 + round(v.y * 255.0)) / 1023.0;
        }
        color = result;
    }
    else
    {
        color = pow(color, g_Push.param0);
    }
    return float4(color, 1.0);
}
