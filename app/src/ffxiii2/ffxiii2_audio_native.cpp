// ffxiii2-nx (2026-11-12, ffns_native_biquad): a native body for the XAudio engine's per-voice biquad filter,
// sub_82A34BB0 (the console profile of 2026-11-11: 25 % of the mixer thread "XThreadB637C4C0" at 30 fps and 40 % of it
// in the scenes that drop, where that thread takes 56 % of a core beside the main thread).
//
// What the original does (app/generated, read instruction by instruction):
//   r3 = the filter (u16 channel count at +194, four state words per channel from +64: x1, x2, y1, y2; the
//        coefficient slots at +36..+60, read through sub_82B89340 / released through sub_82B893B8),
//   r5 = the input block (+0 samples, +4 a flag that must be 1, +8 the frame count), r7 = the output block, r8 = on.
//   For every frame and every channel: y = b0 x + b1 x1 + b2 x2 + a1 y1 + a2 y2 (coefficients c0..c4), then
//   y2 = y1, y1 = y, x2 = x1, x1 = x. Each product is added with a single-rounded fmadds, and the ORDER of the five
//   terms depends on the channel's place: the compiler unrolled four channels and wrote the first of each four
//   differently from the other three, and the channels left over (all of them below four channels) differently again.
//   The state lives in guest memory and is read and written for every sample, so every sample waits for two store
//   -> load round trips and ten float <-> double conversions on top of the five operations.
//
// Here: the same operations on the same values in the same order per channel (the three orders below), with the
// state in registers for the whole block. The generated code makes each fmadds as float(fma(double, double, double))
// (two roundings); a plain single fma (one rounding) gives another last bit about once in 2^29 operations. So the
// chain the next sample waits for is computed with the single fma, the two-rounding value is computed beside it (off
// the chain), and a sample whose two values differ in any bit is computed again the generated code's way: the
// results are the generated code's, bit for bit, at the single fma's latency.
// Declined (the generated body runs): the filter off (r8 = 0, a copy), more than 8 channels, or blocks / state that
// overlap (the original re-reads the input after writing the output).
// Key ffns_native_biquad (default on).
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/ppc/context.h>
#include <rex/ppc/diet_ops.h>  // rex::ppc::QuietSingleBits
#include <rex/system/xmemory.h>

#include "cpu/banjo_hot_common.h"  // ::banjo::hot::Raw, Card

REXCVAR_DEFINE_BOOL(ffns_native_biquad, true, "FFXIII2",
                    "CPU (the audio mixer thread): XAudio's per-voice biquad filter sub_82A34BB0 as a native body "
                    "with the state in registers: the same results bit for bit (src/ffxiii2/ffxiii2_audio_native.cpp). "
                    "Off: the generated body");

REX_EXTERN(__imp__sub_82A34BB0);
REX_EXTERN(sub_82B89340);  // the coefficients in use (takes the pending set)
REX_EXTERN(sub_82B893B8);  // gives the set back

namespace ffxiii2::audio_native {

[[gnu::always_inline]] inline uint32_t LoadW(const uint8_t* p) {
  uint32_t v;
  std::memcpy(&v, p, 4);
  return __builtin_bswap32(v);
}
[[gnu::always_inline]] inline void StoreW(uint8_t* p, uint32_t v) {
  v = __builtin_bswap32(v);
  std::memcpy(p, &v, 4);
}
[[gnu::always_inline]] inline float LoadF(const uint8_t* p) { return std::bit_cast<float>(LoadW(p)); }
[[gnu::always_inline]] inline void StoreF(uint8_t* p, float f) { StoreW(p, std::bit_cast<uint32_t>(f)); }
[[gnu::always_inline]] inline bool Overlap(uint32_t a, uint64_t n, uint32_t b, uint64_t m) {
  return uint64_t(a) < uint64_t(b) + m && uint64_t(b) < uint64_t(a) + n;
}

// The generated code's fmadds: float(fma) of the doubles.
[[gnu::always_inline]] inline float Fma2(float a, float b, float c) {
  return float(__builtin_fma(double(a), double(b), double(c)));
}

// The three orders. kind 0: the first channel of an unrolled four; 1: the other three; 2: the channels left over.
// x1, x2, y1, y2 = the state words 0..3; c[0..4] the coefficients as loaded (c0 with x, c1 with x1, c2 with x2,
// c3 with y1, c4 with y2).
template <int kKind>
[[gnu::always_inline]] inline float Exact(float x, float x1, float x2, float y1, float y2, const float* c) {
  if (kKind == 2) {
    float t = x1 * c[1];  // fmuls
    t = Fma2(y2, c[4], t);
    t = Fma2(y1, c[3], t);
    t = Fma2(x2, c[2], t);
    return Fma2(x, c[0], t);
  }
  float t = y1 * c[3];  // fmuls
  t = Fma2(y2, c[4], t);
  t = Fma2(x2, c[2], t);
  if (kKind == 0) {
    t = Fma2(x, c[0], t);
    return Fma2(x1, c[1], t);
  }
  t = Fma2(x1, c[1], t);
  return Fma2(x, c[0], t);
}

// One step: the single fma on the chain, the generated code's value beside it; `bad` collects any differing bit.
#define FFNS_STEP(a, b)                                                             \
  do {                                                                              \
    const float two = Fma2((a), (b), t);                                            \
    t = __builtin_fmaf((a), (b), t);                                                \
    bad |= std::bit_cast<uint32_t>(two) ^ std::bit_cast<uint32_t>(t);               \
  } while (0)

template <int kKind>
[[gnu::always_inline]] inline float Sample(float x, float x1, float x2, float y1, float y2, const float* c) {
  uint32_t bad = 0;
  float t;
  if (kKind == 2) {
    t = x1 * c[1];
    FFNS_STEP(y2, c[4]);
    FFNS_STEP(y1, c[3]);
    FFNS_STEP(x2, c[2]);
    FFNS_STEP(x, c[0]);
  } else {
    t = y1 * c[3];
    FFNS_STEP(y2, c[4]);
    FFNS_STEP(x2, c[2]);
    if (kKind == 0) {
      FFNS_STEP(x, c[0]);
      FFNS_STEP(x1, c[1]);
    } else {
      FFNS_STEP(x1, c[1]);
      FFNS_STEP(x, c[0]);
    }
  }
  if (bad) [[unlikely]] return Exact<kKind>(x, x1, x2, y1, y2, c);
  return t;
}
#undef FFNS_STEP

template <uint32_t N>
[[gnu::noinline]] void Block(uint8_t* state, const uint8_t* in, uint8_t* out, uint32_t frames, const float* c) {
  float x1[N], x2[N], y1[N], y2[N];
  for (uint32_t k = 0; k < N; ++k) {
    x1[k] = LoadF(state + 16 * k);
    x2[k] = LoadF(state + 16 * k + 4);
    y1[k] = LoadF(state + 16 * k + 8);
    y2[k] = LoadF(state + 16 * k + 12);
  }
  constexpr uint32_t kUnrolled = N & ~3u;
  for (uint32_t i = 0; i < frames; ++i) {
    for (uint32_t k = 0; k < N; ++k) {
      const float x = LoadF(in + 4 * k);
      const float y = k >= kUnrolled  ? Sample<2>(x, x1[k], x2[k], y1[k], y2[k], c)
                      : (k & 3u) == 0 ? Sample<0>(x, x1[k], x2[k], y1[k], y2[k], c)
                                      : Sample<1>(x, x1[k], x2[k], y1[k], y2[k], c);
      StoreF(out + 4 * k, y);
      y2[k] = y1[k];
      y1[k] = y;
      x2[k] = x1[k];
      x1[k] = x;
    }
    in += 4 * N;
    out += 4 * N;
  }
  // The original's state stores quiet a signalling NaN (stfs); every word was stored at least once when frames > 0
  // ... except x2 / y2 after a single frame, which hold what x1 / y1 held: stored through the same conversion.
  for (uint32_t k = 0; k < N; ++k) {
    StoreW(state + 16 * k, rex::ppc::QuietSingleBits(std::bit_cast<uint32_t>(x1[k])));
    StoreW(state + 16 * k + 4, rex::ppc::QuietSingleBits(std::bit_cast<uint32_t>(x2[k])));
    StoreW(state + 16 * k + 8, rex::ppc::QuietSingleBits(std::bit_cast<uint32_t>(y1[k])));
    StoreW(state + 16 * k + 12, rex::ppc::QuietSingleBits(std::bit_cast<uint32_t>(y2[k])));
  }
}

// false: not covered, nothing written (the generated body must run).
bool Run(PPCContext& __restrict ctx, uint8_t* base) {
  const uint32_t self = ctx.r3.u32, in_block = ctx.r5.u32, out_block = ctx.r7.u32;
  if (ctx.r8.s32 == 0) return false;
  if (self > 0xFFFF0000u || in_block > 0xFFFF0000u || out_block > 0xFFFF0000u) [[unlikely]] return false;
  uint8_t* const ib = ::banjo::hot::Raw(base, in_block);
  uint8_t* const ob = ::banjo::hot::Raw(base, out_block);
  if (int32_t(LoadW(ib + 4)) != 1) return false;  // the original only clears the output's flag: left to it
  uint8_t* const f = ::banjo::hot::Raw(base, self);
  const uint32_t out = LoadW(ob), in = LoadW(ib);
  const uint32_t nch = uint32_t(f[194]) << 8 | f[195];
  const int32_t frames = int32_t(LoadW(ib + 8));
  if (nch > 8) [[unlikely]] return false;
  const uint64_t bytes = frames > 0 ? uint64_t(frames) * nch * 4u : 0u;
  if (bytes) {
    if (uint64_t(in) + bytes > 0xFFFF0000ull || uint64_t(out) + bytes > 0xFFFF0000ull) [[unlikely]] return false;
    if (Overlap(in, bytes, out, bytes) || Overlap(out, bytes, self, 256) || Overlap(in, bytes, self, 256) ||
        Overlap(out, bytes, in_block, 12) || Overlap(out, bytes, out_block, 12))
      [[unlikely]] return false;
  }
  // From here on as the original: the coefficient set, the loop, the set given back, the output block's words.
  ctx.r3.u64 = self;
  sub_82B89340(ctx, base);
  const uint32_t coefficients = ctx.r3.u32;
  ctx.fpscr.disableFlushMode();
  if (bytes) {
    const uint8_t* cp = ::banjo::hot::Raw(base, coefficients);
    const float c[5] = {LoadF(cp), LoadF(cp + 4), LoadF(cp + 8), LoadF(cp + 12), LoadF(cp + 16)};
    uint8_t* const state = f + 64;
    const uint8_t* const ip = ::banjo::hot::Raw(base, in);
    uint8_t* const op = ::banjo::hot::Raw(base, out);
    const uint32_t n = uint32_t(frames);
    switch (nch) {
    case 1: Block<1>(state, ip, op, n, c); break;
    case 2: Block<2>(state, ip, op, n, c); break;
    case 3: Block<3>(state, ip, op, n, c); break;
    case 4: Block<4>(state, ip, op, n, c); break;
    case 5: Block<5>(state, ip, op, n, c); break;
    case 6: Block<6>(state, ip, op, n, c); break;
    case 7: Block<7>(state, ip, op, n, c); break;
    case 8: Block<8>(state, ip, op, n, c); break;
    default: break;  // no channel: the original's loops do nothing
    }
    if (nch) {
      ::banjo::hot::Card(base, out, uint32_t(bytes));
      ::banjo::hot::Card(base, self + 64, 16 * nch);
    }
  }
  ctx.r3.u64 = self;
  sub_82B893B8(ctx, base);
  StoreW(ob + 8, LoadW(ib + 8));
  StoreW(ob + 4, LoadW(ib + 4));
  ::banjo::hot::Card(base, out_block + 4, 8);
  ctx.r3.u64 = self;
  return true;
}

}  // namespace ffxiii2::audio_native

REX_HOOK_RAW(sub_82A34BB0) {
  static const bool on = REXCVAR_GET(ffns_native_biquad);
  if (on && ffxiii2::audio_native::Run(ctx, base)) return;
  __imp__sub_82A34BB0(ctx, base);
}
