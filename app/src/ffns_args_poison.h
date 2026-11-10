// FFXIII-2-Native-Switch (2026-11-08): validation build of the args_in_registers codegen (prototype P1).
//
// The codegen emits ME_ARGS_POISON(mask) before every fast call: mask bit i = register i of the pass's table
// (r3..r10, f1..f13, r0, r11, r12, f0, lr) that the caller no longer stores into ctx because it passes it by value.
// Correct code never reads those ctx fields before writing them. Here they are overwritten with a poison value, so a
// callee (or anything it reaches) that still reads a stale ctx copy gets an absurd value instead of a plausible one:
// guest addresses 0xDEAD5Exx fault at once, floats become a signalling-NaN payload that shows up in comparisons.
// ffns::g_args_poison_calls counts the poisoned calls (one relaxed atomic add per call).
//
// Only for PC / emulator validation builds. The performance flavour compiles the macro away (codegen default).
#pragma once
// Switch builds measure performance: the macro stays the codegen default (nothing) there.
#if !defined(__SWITCH__)
#include <atomic>
#include <cstdint>

namespace ffns {
inline std::atomic<uint64_t> g_args_poison_calls{0};
}

#define ME_ARGS_POISON(mask)                                                       \
  do {                                                                             \
    const uint32_t ffns_m_ = (mask);                                               \
    if (ffns_m_ & (1u << 0)) ctx.r3.u64 = 0xDEAD5EE0DEAD5EE0ull;                 \
    if (ffns_m_ & (1u << 1)) ctx.r4.u64 = 0xDEAD5EE4DEAD5EE4ull;                 \
    if (ffns_m_ & (1u << 2)) ctx.r5.u64 = 0xDEAD5EE8DEAD5EE8ull;                 \
    if (ffns_m_ & (1u << 3)) ctx.r6.u64 = 0xDEAD5EECDEAD5EECull;                 \
    if (ffns_m_ & (1u << 4)) ctx.r7.u64 = 0xDEAD5EF0DEAD5EF0ull;                 \
    if (ffns_m_ & (1u << 5)) ctx.r8.u64 = 0xDEAD5EF4DEAD5EF4ull;                 \
    if (ffns_m_ & (1u << 6)) ctx.r9.u64 = 0xDEAD5EF8DEAD5EF8ull;                 \
    if (ffns_m_ & (1u << 7)) ctx.r10.u64 = 0xDEAD5EFCDEAD5EFCull;                \
    if (ffns_m_ & (1u << 8)) ctx.f1.u64 = 0x7FF4DEAD5EED0001ull;                 \
    if (ffns_m_ & (1u << 9)) ctx.f2.u64 = 0x7FF4DEAD5EED0002ull;                 \
    if (ffns_m_ & (1u << 10)) ctx.f3.u64 = 0x7FF4DEAD5EED0003ull;                \
    if (ffns_m_ & (1u << 11)) ctx.f4.u64 = 0x7FF4DEAD5EED0004ull;                \
    if (ffns_m_ & (1u << 12)) ctx.f5.u64 = 0x7FF4DEAD5EED0005ull;                \
    if (ffns_m_ & (1u << 13)) ctx.f6.u64 = 0x7FF4DEAD5EED0006ull;                \
    if (ffns_m_ & (1u << 14)) ctx.f7.u64 = 0x7FF4DEAD5EED0007ull;                \
    if (ffns_m_ & (1u << 15)) ctx.f8.u64 = 0x7FF4DEAD5EED0008ull;                \
    if (ffns_m_ & (1u << 16)) ctx.f9.u64 = 0x7FF4DEAD5EED0009ull;                \
    if (ffns_m_ & (1u << 17)) ctx.f10.u64 = 0x7FF4DEAD5EED000Aull;               \
    if (ffns_m_ & (1u << 18)) ctx.f11.u64 = 0x7FF4DEAD5EED000Bull;               \
    if (ffns_m_ & (1u << 19)) ctx.f12.u64 = 0x7FF4DEAD5EED000Cull;               \
    if (ffns_m_ & (1u << 20)) ctx.f13.u64 = 0x7FF4DEAD5EED000Dull;               \
    if (ffns_m_ & (1u << 21)) ctx.r0.u64 = 0xDEAD5E00DEAD5E00ull;                \
    if (ffns_m_ & (1u << 22)) ctx.r11.u64 = 0xDEAD5E0BDEAD5E0Bull;               \
    if (ffns_m_ & (1u << 23)) ctx.r12.u64 = 0xDEAD5E0CDEAD5E0Cull;               \
    if (ffns_m_ & (1u << 24)) ctx.f0.u64 = 0x7FF4DEAD5EED0000ull;                \
    if (ffns_m_ & (1u << 25)) ctx.lr = 0xDEAD5E1Cull;                            \
    ::ffns::g_args_poison_calls.fetch_add(1, std::memory_order_relaxed);           \
  } while (0)
#endif  // !__SWITCH__
