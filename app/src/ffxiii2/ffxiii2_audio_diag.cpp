// ffxiii2-nx (2026-11-05): a diagnostic of the missing game audio on the Switch (docs/HANDOFF.md, "Audio").
//
// The XMA voice table (0x82EB2058) is filled on the console, but no voice is ever kicked: the submit function
// sub_82B92878 (which copies the contexts to the hardware, kicks register 0x650 and clears the voice's 0x20000 flag)
// is reached only through sub_82B8E950, vtable slot 69 (byte 276), and on the console sub_82B8E950 is never called.
// On the PC the kick comes 13 ms after the voice is created, on the same thread. This logs who calls the two ends:
//   sub_82B90650  the function that creates the voice (calls sub_82B92B80, which sets 0x20000)
//   sub_82B8E950  slot 69 (the submit)
// with the caller's return address (lr), the object (r3) and the thread, the first 48 calls of each, so the PC's
// path to the submit can be compared with the console's. Off by default (ffxiii2_audio_diag); nothing changes in
// what the game does: each hook calls the original.
//
// Credits: reNut's recompiled renderer and runtime (masterspike52), katamari-nx, renut-nx, sa2-nx, NetherSX2_nx,
// battd_nx, bejeweled3-nx and the zuma ports. GPL-3.0.

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>

#include <fmt/format.h>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>

REXCVAR_DEFINE_BOOL(ffxiii2_audio_diag, false, "FFXIII2",
                    "Diagnostic of the missing audio: log the callers of the XMA voice creation (sub_82B90650) and of "
                    "the voice submit (sub_82B8E950, vtable slot 69), the first 48 calls of each");

namespace {
std::atomic<uint32_t> g_create_calls{0};
std::atomic<uint32_t> g_submit_calls{0};

uint32_t GuestU32(const uint8_t *base, uint32_t address) {
  uint32_t v;
  __builtin_memcpy(&v, base + address, 4);
  return __builtin_bswap32(v);
}

size_t ThreadTag() { return std::hash<std::thread::id>{}(std::this_thread::get_id()) & 0xFFFFFF; }
}  // namespace

REX_EXTERN(__imp__sub_82B90650);
REX_HOOK_RAW(sub_82B90650) {
  if (REXCVAR_GET(ffxiii2_audio_diag)) {
    const uint32_t n = g_create_calls.fetch_add(1, std::memory_order_relaxed);
    if (n < 48) {
      REXLOG_INFO("[ffxiii2_audio] create #{}: lr {:08X} r3 {:08X} r4 {:08X} r5 {:08X} thread {:06X}", n,
                  uint32_t(ctx.lr), ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, ThreadTag());
    }
  }
  __imp__sub_82B90650(ctx, base);
  if (REXCVAR_GET(ffxiii2_audio_diag) && g_create_calls.load(std::memory_order_relaxed) <= 48)
    REXLOG_INFO("[ffxiii2_audio] create returned r3 {:08X}", ctx.r3.u32);
}

// The PC run aud3 (2026-11-05): the submit is called every 5-10 ms from sub_82BD4DA0 (lr 82BD4FA8, the audio thread),
// through the object at this+268 (vtable 821D7ED8, slot 7 = byte 28; the voice at object+336 is E4687000, the same
// address as on the console). sub_82BD4DA0 is itself virtual (no direct caller) and has a native copy
// (banjo_hot_shard_8: no second hook here), so the submit hook logs its caller's this (r31, callee-saved, not yet
// saved by sub_82B8E950) and that object's words, for a peek of the same object on the console (xma_voices.py).

// aud9 + console peek (2026-11-05): the voice callback runs inside the engine pass sub_82BBAFE8 (manager 4000D6C0,
// vtable 821DCFDC, at the same address on both machines): it calls [+144]->slot0, then sub_82BBD1E0([+152]) and
// (when +640) sub_82BBAD80, then sub_82BB95F0([+148]); only when none of them returns < 0 does it set +616 = +592
// (the voice list) and process it (sub_82BB9C80). On the console +616 stays 0. These log each step's result.
namespace {
std::atomic<uint32_t> g_pass_calls{0}, g_step_calls[3];
void LogStep(int which, const char *name, uint32_t in, uint32_t result) {
  const uint32_t n = g_step_calls[which].fetch_add(1, std::memory_order_relaxed);
  if (n < 16 || (n % 2000) == 0 || (int32_t(result) < 0 && n < 400))
    REXLOG_INFO("[ffxiii2_audio] {} #{}: r3 {:08X} -> {:08X}{}", name, n, in, result,
                int32_t(result) < 0 ? " (FAILED: the pass stops)" : "");
}
}  // namespace

// The guest call chain from a function's entry: return addresses up the back chain (see the submit hook).
static std::string GuestChain(const uint8_t *base, uint32_t sp) {
  std::string chain;
  for (int depth = 0; depth < 12 && sp >= 0x70000000u && sp < 0x80000000u; ++depth) {
    const uint32_t up = GuestU32(base, sp);
    if (up <= sp || up >= 0x80000000u || up - sp > 0x10000u)
      break;
    chain += fmt::format(" {:08X}", GuestU32(base, up - 8));
    if ((up & 0xFFFu) == 0)
      break;
    sp = up;
  }
  return chain;
}

// Console peek (2026-11-05 22:45): the stream's source voice (vtable 821F644C) has +100 = 1 on the console: never
// started. Its Start (slot 5, sub_82BD3E88: +112 = 0, +100 = 0, then the XMA object's slot 0) clears it; on the PC it
// is in the engine's active list, so started. Who calls Start?
std::atomic<uint32_t> g_start_calls{0};
REX_EXTERN(__imp__sub_82BD3E88);
REX_HOOK_RAW(sub_82BD3E88) {
  if (REXCVAR_GET(ffxiii2_audio_diag)) {
    const uint32_t n = g_start_calls.fetch_add(1, std::memory_order_relaxed);
    if (n < 32)
      REXLOG_INFO("[ffxiii2_audio] voice Start #{}: this {:08X} vtable {:08X} +100 {:08X} +268 {:08X} thread {:06X}; lr {:08X} <-{}",
                  n, ctx.r3.u32, GuestU32(base, ctx.r3.u32), GuestU32(base, ctx.r3.u32 + 100),
                  GuestU32(base, ctx.r3.u32 + 268), ThreadTag(), uint32_t(ctx.lr), GuestChain(base, ctx.r1.u32));
  }
  __imp__sub_82BD3E88(ctx, base);
}

// Console (k181): the engine's operation queue is empty: nothing asks to start a voice. The public source voice
// (vtable 821DD080, IXAudio2SourceVoice: slot 17 Start = sub_82BBC628, 18 Stop = sub_82BBCC10, 19 SubmitSourceBuffer
// = sub_82BBC778) holds the internal voice at +8. These log the game's calls (PC: who starts the streams; console:
// whether it does). PC aud13: the slot mapping is NOT the XAudio2 order assumed here: "API Start" (sub_82BBC628) was
// called from inside the engine (lr 82BBD02C, the operation apply path) with 40013A04 / 40013A34: the labels are
// guesses until the vtable is mapped.
std::atomic<uint32_t> g_api_calls[3];
static void LogApi(int which, const char *name, PPCContext &ctx, const uint8_t *base) {
  const uint32_t n = g_api_calls[which].fetch_add(1, std::memory_order_relaxed);
  // k186: after the first 4, only calls for other objects than 40013A04 / 40013A34 (the first 48 calls were those two
  // on both machines, +8 = 4000C5DC for both: not the internal voice).
  const uint32_t self = ctx.r3.u32;
  const bool other = self != 0x40013A04u && self != 0x40013A34u;
  static std::atomic<uint32_t> other_logged{0};
  if (n < 4 || (other && other_logged.fetch_add(1, std::memory_order_relaxed) < 200))
    REXLOG_INFO("[ffxiii2_audio] {} #{}: voice {:08X} (internal {:08X}) r4 {:08X} r5 {:08X} thread {:06X}; lr {:08X} <-{}", name,
                n, ctx.r3.u32, GuestU32(base, ctx.r3.u32 + 8), ctx.r4.u32, ctx.r5.u32, ThreadTag(), uint32_t(ctx.lr),
                GuestChain(base, ctx.r1.u32));
}
REX_EXTERN(__imp__sub_82BBC628);
REX_HOOK_RAW(sub_82BBC628) {
  if (REXCVAR_GET(ffxiii2_audio_diag))
    LogApi(0, "API Start", ctx, base);
  __imp__sub_82BBC628(ctx, base);

}
REX_EXTERN(__imp__sub_82BBCC10);
REX_HOOK_RAW(sub_82BBCC10) {
  if (REXCVAR_GET(ffxiii2_audio_diag))
    LogApi(1, "API Stop", ctx, base);
  __imp__sub_82BBCC10(ctx, base);
}
REX_EXTERN(__imp__sub_82BBC778);
REX_HOOK_RAW(sub_82BBC778) {
  if (REXCVAR_GET(ffxiii2_audio_diag))
    LogApi(2, "API SubmitSourceBuffer", ctx, base);
  __imp__sub_82BBC778(ctx, base);
}

// 2026-11-06: the engine's operations are queued only by sub_82B9CE30 (-> sub_82BBAD08 -> sub_82BBA750 ->
// sub_82BBA6D0, the push onto [manager+152]+8), which is reached directly from two virtual methods only:
// sub_82B9DB38 (a thunk: this-8, r4 = [r4+16], r5 = [r4+20]) and sub_82B9E600 (this-4, ...). Their calls on the PC and
// the console, with the game's call chain, show which operation the console never queues.
std::atomic<uint32_t> g_queue_calls[2];
static void LogQueue(int which, const char *name, PPCContext &ctx, const uint8_t *base) {
  const uint32_t n = g_queue_calls[which].fetch_add(1, std::memory_order_relaxed);
  if (n < 60)
    REXLOG_INFO("[ffxiii2_audio] {} #{}: this {:08X} r4 {:08X} r5 {:08X} r6 {:08X} r7 {:08X} [r4+16] {:08X} [r4+20] {:08X} "
                "thread {:06X}; lr {:08X} <-{}",
                name, n, ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32, ctx.r7.u32,
                (ctx.r4.u32 >= 0x40000000u && ctx.r4.u32 < 0x80000000u) ? GuestU32(base, ctx.r4.u32 + 16) : 0u,
                (ctx.r4.u32 >= 0x40000000u && ctx.r4.u32 < 0x80000000u) ? GuestU32(base, ctx.r4.u32 + 20) : 0u,
                ThreadTag(), uint32_t(ctx.lr), GuestChain(base, ctx.r1.u32));
}
REX_EXTERN(__imp__sub_82B9DB38);
REX_HOOK_RAW(sub_82B9DB38) {
  if (REXCVAR_GET(ffxiii2_audio_diag))
    LogQueue(0, "queue A (sub_82B9DB38)", ctx, base);
  __imp__sub_82B9DB38(ctx, base);
}
REX_EXTERN(__imp__sub_82B9E600);
REX_HOOK_RAW(sub_82B9E600) {
  if (REXCVAR_GET(ffxiii2_audio_diag))
    LogQueue(1, "queue B (sub_82B9E600)", ctx, base);
  __imp__sub_82B9E600(ctx, base);
}

// k188 (PC aud15 vs console): the game thread queues the same operations on both machines up to #26 (the stream's
// source voice, created from sub_82B90848 -> sub_82B8C768); the PC then queues #27.. (outputs, more stream voices,
// Start); the console queues nothing more. sub_82B90848 is slot 30 of the stream object (virtual only): it creates the
// voices (sub_82B8C768), then sub_82B8EF88, then the XMA voice (sub_82B90650). Its result says which step failed.
std::atomic<uint32_t> g_init_calls{0};
REX_EXTERN(__imp__sub_82B90848);
REX_HOOK_RAW(sub_82B90848) {
  const bool diag = REXCVAR_GET(ffxiii2_audio_diag);
  const uint32_t n = diag ? g_init_calls.fetch_add(1, std::memory_order_relaxed) : 0;
  const uint32_t self = ctx.r3.u32;
  if (diag && n < 16)
    REXLOG_INFO("[ffxiii2_audio] stream Init (sub_82B90848) #{} enter: this {:08X} r4 {:08X} r5 {:08X} r6 {:08X} r7 {:08X} "
                "r8 {:08X} thread {:06X}; lr {:08X} <-{}",
                n, self, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32, ctx.r7.u32, ctx.r8.u32, ThreadTag(), uint32_t(ctx.lr),
                GuestChain(base, ctx.r1.u32));
  __imp__sub_82B90848(ctx, base);
  if (diag && n < 16)
    REXLOG_INFO("[ffxiii2_audio] stream Init #{} returned {:08X}{}", n, ctx.r3.u32,
                int32_t(ctx.r3.u32) < 0 ? " (FAILED)" : "");
}

// aud17: the stream voice's Start runs in sub_82BBBB28 (slot 0 of a voice-command operation: a switch on the
// operation's kind [r4+8] - 6 over the internal voice [this+144]). Every application, with its kind, shows which
// command starts the stream on the PC and whether the console ever applies one.
std::atomic<uint32_t> g_cmd_calls{0};
REX_EXTERN(__imp__sub_82BBBB28);
REX_HOOK_RAW(sub_82BBBB28) {
  if (REXCVAR_GET(ffxiii2_audio_diag)) {
    const uint32_t n = g_cmd_calls.fetch_add(1, std::memory_order_relaxed);
    const uint32_t self = ctx.r3.u32, op = ctx.r4.u32;
    const uint32_t internal = GuestU32(base, self + 144);
    if (n < 120)
      REXLOG_INFO("[ffxiii2_audio] voice command #{}: this {:08X} internal {:08X} (vtable {:08X}) op {:08X} kind {} "
                  "[op+4] {:08X} [op+12] {:08X} [op+16] {:08X} [op+20] {:08X} thread {:06X}",
                  n, self, internal, internal ? GuestU32(base, internal) : 0u, op, int32_t(GuestU32(base, op + 8)),
                  GuestU32(base, op + 4), GuestU32(base, op + 12), GuestU32(base, op + 16), GuestU32(base, op + 20),
                  ThreadTag());
  }
  __imp__sub_82BBBB28(ctx, base);
}

// aud18 vs k191: the PC applies four commands for the stream voice in its first pass (kinds 5, 2, 13, then 0: the
// source buffer [op+16], after which the voice starts); the console applies 5, 2, 13 and never 0: the game never
// submits the stream's first buffer. The game's XAudio wrappers that pass the operation set 0x1337F001 and are
// reached only virtually: sub_82B91CE0, sub_82B93060, sub_82B8CB48, sub_82B8CCA0. Their calls and results.
std::atomic<uint32_t> g_wrap_calls[4];
static void LogWrapEnter(int which, const char *name, PPCContext &ctx, const uint8_t *base, uint32_t n) {
  if (n < 20)
    REXLOG_INFO("[ffxiii2_audio] {} #{} enter: this {:08X} r4 {:08X} r5 {:08X} r6 {:08X} thread {:06X}; lr {:08X} <-{}", name,
                n, ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32, ThreadTag(), uint32_t(ctx.lr),
                GuestChain(base, ctx.r1.u32));
  (void)which;
}
#define FFXIII2_AUDIO_WRAP(idx, fn)                                                            \
  REX_EXTERN(__imp__##fn);                                                                     \
  REX_HOOK_RAW(fn) {                                                                           \
    const bool diag = REXCVAR_GET(ffxiii2_audio_diag);                                         \
    const uint32_t n = diag ? g_wrap_calls[idx].fetch_add(1, std::memory_order_relaxed) : 0;   \
    if (diag)                                                                                  \
      LogWrapEnter(idx, #fn, ctx, base, n);                                                    \
    __imp__##fn(ctx, base);                                                                    \
    if (diag && n < 20)                                                                        \
      REXLOG_INFO("[ffxiii2_audio] " #fn " #{} returned {:08X}", n, ctx.r3.u32);              \
  }
FFXIII2_AUDIO_WRAP(0, sub_82B91CE0)
FFXIII2_AUDIO_WRAP(1, sub_82B93060)
FFXIII2_AUDIO_WRAP(2, sub_82B8CB48)
FFXIII2_AUDIO_WRAP(3, sub_82B8CCA0)

// k192: on the console sub_82B8CB48 (the stream's play: the PC calls it 20 times, the first from thread chain
// 82A39F40 <- 82A1B80C <- 82A1CF20 <- 82A1D9AC <- 82A0F3BC <- 82C09EC4 <- 82A5FF40 <- 82BDC468 <- 82886E6C) and
// sub_82B8CCA0 are never called. The virtual-only frames of that chain: sub_82A39EF0 (calls the play) and
// sub_82C09E88. Which one the console still reaches.
std::atomic<uint32_t> g_chain_calls[2];
#define FFXIII2_AUDIO_CHAIN(idx, fn)                                                           \
  REX_EXTERN(__imp__##fn);                                                                     \
  REX_HOOK_RAW(fn) {                                                                           \
    const bool diag = REXCVAR_GET(ffxiii2_audio_diag);                                         \
    const uint32_t n = diag ? g_chain_calls[idx].fetch_add(1, std::memory_order_relaxed) : 0;  \
    if (diag && (n < 12 || (n % 1000) == 0))                                                   \
      REXLOG_INFO("[ffxiii2_audio] " #fn " #{}: this {:08X} r4 {:08X} r5 {:08X} [this] {:08X} thread {:06X}; lr {:08X} <-{}", \
                  n, ctx.r3.u32, ctx.r4.u32, ctx.r5.u32,                                       \
                  (ctx.r3.u32 >= 0x40000000u && ctx.r3.u32 < 0x50000000u) ? GuestU32(base, ctx.r3.u32) : 0u, \
                  ThreadTag(), uint32_t(ctx.lr), GuestChain(base, ctx.r1.u32));                \
    __imp__##fn(ctx, base);                                                                    \
  }
FFXIII2_AUDIO_CHAIN(0, sub_82A39EF0)
FFXIII2_AUDIO_CHAIN(1, sub_82C09E88)

// k193: the PC calls sub_82C09E88 from a timer thread: sub_82BDC3F8 (thread loop: an event (sub_82C18788), a periodic
// waitable timer with the period [this+8] (sub_82C18718), then forever: wait (sub_82B620B0), call [this+12]([this+16])
// = sub_82A5FF00, which locks and calls slot 10 of the object at [0x82EB0F3C] = sub_82C09E88). The console never calls
// sub_82C09E88: does its timer thread start, does the tick run, is the object there?
std::atomic<uint32_t> g_tick_calls{0};
REX_EXTERN(__imp__sub_82BDC3F8);
REX_HOOK_RAW(sub_82BDC3F8) {
  if (REXCVAR_GET(ffxiii2_audio_diag))
    REXLOG_INFO("[ffxiii2_audio] timer thread loop start: this {:08X} period [+8] {} fn [+12] {:08X} arg [+16] {:08X} "
                "stop [+56] {:02X} thread {:06X}",
                ctx.r3.u32, int32_t(GuestU32(base, ctx.r3.u32 + 8)), GuestU32(base, ctx.r3.u32 + 12),
                GuestU32(base, ctx.r3.u32 + 16), GuestU32(base, ctx.r3.u32 + 56) >> 24, ThreadTag());
  __imp__sub_82BDC3F8(ctx, base);
}
// k194: the loop's function is sub_82A60E80 (period 16 ms on both machines; the thread starts on the console too).
std::atomic<uint32_t> g_tick2_calls{0};
REX_EXTERN(__imp__sub_82A60E80);
REX_HOOK_RAW(sub_82A60E80) {
  if (REXCVAR_GET(ffxiii2_audio_diag)) {
    const uint32_t n = g_tick2_calls.fetch_add(1, std::memory_order_relaxed);
    if (n < 6 || (n % 500) == 0)
      REXLOG_INFO("[ffxiii2_audio] timer tick sub_82A60E80 #{}: r3 {:08X} object [0x82EB0F3C] {:08X} thread {:06X}", n,
                  ctx.r3.u32, GuestU32(base, 0x82EB0F3Cu), ThreadTag());
  }
  __imp__sub_82A60E80(ctx, base);
}
REX_EXTERN(__imp__sub_82A5FF00);
REX_HOOK_RAW(sub_82A5FF00) {
  if (REXCVAR_GET(ffxiii2_audio_diag)) {
    const uint32_t n = g_tick_calls.fetch_add(1, std::memory_order_relaxed);
    if (n < 6 || (n % 500) == 0)
      REXLOG_INFO("[ffxiii2_audio] timer tick sub_82A5FF00 #{}: object [0x82EB0F3C] {:08X} thread {:06X}", n,
                  GuestU32(base, 0x82EB0F3Cu), ThreadTag());
  }
  __imp__sub_82A5FF00(ctx, base);
}

REX_EXTERN(__imp__sub_82BBAFE8);
REX_HOOK_RAW(sub_82BBAFE8) {
  if (REXCVAR_GET(ffxiii2_audio_diag)) {
    const uint32_t n = g_pass_calls.fetch_add(1, std::memory_order_relaxed);
    if (n < 8 || (n % 2000) == 0) {
      const uint32_t m = ctx.r3.u32;
      REXLOG_INFO("[ffxiii2_audio] engine pass #{}: this {:08X} r4 {:08X} lr {:08X}; +144 {:08X} +148 {:08X} +152 {:08X} "
                  "+592 {:08X} +616 {:08X} +640 {:08X}",
                  n, m, ctx.r4.u32, uint32_t(ctx.lr), GuestU32(base, m + 144), GuestU32(base, m + 148),
                  GuestU32(base, m + 152), GuestU32(base, m + 592), GuestU32(base, m + 616), GuestU32(base, m + 640));
    }
  }
  __imp__sub_82BBAFE8(ctx, base);
}

REX_EXTERN(__imp__sub_82BBD1E0);
REX_HOOK_RAW(sub_82BBD1E0) {
  const uint32_t in = ctx.r3.u32;
  __imp__sub_82BBD1E0(ctx, base);
  if (REXCVAR_GET(ffxiii2_audio_diag))
    LogStep(0, "step sub_82BBD1E0", in, ctx.r3.u32);
}

REX_EXTERN(__imp__sub_82BB95F0);
REX_HOOK_RAW(sub_82BB95F0) {
  const uint32_t in = ctx.r3.u32;
  __imp__sub_82BB95F0(ctx, base);
  if (REXCVAR_GET(ffxiii2_audio_diag))
    LogStep(1, "step sub_82BB95F0", in, ctx.r3.u32);
}

REX_EXTERN(__imp__sub_82BBAD80);
REX_HOOK_RAW(sub_82BBAD80) {
  const uint32_t in = ctx.r3.u32;
  __imp__sub_82BBAD80(ctx, base);
  if (REXCVAR_GET(ffxiii2_audio_diag))
    LogStep(2, "step sub_82BBAD80", in, ctx.r3.u32);
}

REX_EXTERN(__imp__sub_82B8E950);
REX_HOOK_RAW(sub_82B8E950) {
  if (REXCVAR_GET(ffxiii2_audio_diag)) {
    const uint32_t n = g_submit_calls.fetch_add(1, std::memory_order_relaxed);
    if (n < 48) {
      const uint32_t object = ctx.r3.u32;
      const uint32_t voice = object ? GuestU32(base, object + 336) : 0;
      const uint32_t flags = voice ? GuestU32(base, voice + 4) : 0;
      const uint32_t caller = ctx.r31.u32;
      REXLOG_INFO("[ffxiii2_audio] submit (slot 69) #{}: lr {:08X} object {:08X} vtable {:08X} voice {:08X} flags {:08X} "
                  "thread {:06X}; caller this (r31) {:08X} vtable {:08X} [+8] {:08X} [+80] {:08X} [+112] {:08X} "
                  "[+116] {:08X} [+120] {:08X} [+256] {:08X} [+260] {:08X} [+268] {:08X}",
                  n, uint32_t(ctx.lr), object, object ? GuestU32(base, object) : 0, voice, flags, ThreadTag(), caller,
                  GuestU32(base, caller), GuestU32(base, caller + 8), GuestU32(base, caller + 80),
                  GuestU32(base, caller + 112), GuestU32(base, caller + 116), GuestU32(base, caller + 120),
                  GuestU32(base, caller + 256), GuestU32(base, caller + 260), GuestU32(base, caller + 268));
      // The guest call chain (first 3 calls): each frame's back chain at [sp], the return address its function saved
      // at [caller's sp - 8] (the prologues' "stw r12,-8(r1)" before "stwu r1"), for the XAudio engine path that
      // reaches the voice callback on the PC.
      if (n < 3) {
        std::string chain;
        uint32_t sp = ctx.r1.u32;
        for (int depth = 0; depth < 12 && sp >= 0x70000000u && sp < 0x80000000u; ++depth) {
          // Only inside the guest stacks' range (0x7xxxxxxx): a wider range read unmapped guest memory on the PC
          // (aud6: an access violation in this walk).
          const uint32_t up = GuestU32(base, sp);
          if (up <= sp || up >= 0x80000000u || up - sp > 0x10000u)
            break;
          chain += fmt::format(" {:08X}", GuestU32(base, up - 8));
          // aud7: the outermost frame's back chain is the stack's end (0x703B0000, not mapped): no read there.
          if ((up & 0xFFFu) == 0)
            break;
          sp = up;
        }
        REXLOG_INFO("[ffxiii2_audio] submit #{} chain (sp {:08X}): lr {:08X} <-{}", n, ctx.r1.u32, uint32_t(ctx.lr),
                    chain);
        // aud8: sub_82BD4DA0 is called by sub_82BB9C80, which walks the list at its manager's +616 (r31 there). Its r31
        // was saved by sub_82BD4DA0's prologue (__savegprlr_25: std r31 at [caller's sp - 16]).
        const uint32_t sp1 = ctx.r1.u32;
        if (sp1 >= 0x70000000u && sp1 < 0x80000000u) {
          const uint32_t up1 = GuestU32(base, sp1);
          if (up1 > sp1 && up1 < 0x80000000u && up1 - sp1 < 0x10000u) {
            const uint32_t mgr = GuestU32(base, up1 - 12);
            if (mgr >= 0x40000000u && mgr < 0x50000000u) {
              std::string nodes;
              uint32_t node = GuestU32(base, mgr + 616);
              for (int k = 0; k < 6 && node >= 0x40000000u && node < 0x50000000u; ++k) {
                nodes += fmt::format(" [{:08X}: obj {:08X}]", node, GuestU32(base, node));
                node = GuestU32(base, node + 4);
              }
              REXLOG_INFO("[ffxiii2_audio] manager {:08X}: vtable {:08X} +60 {:08X} +100 {:08X} +616 {:08X} +620 {:08X} "
                          "+624 {:08X} +632 {:08X}; list{}",
                          mgr, GuestU32(base, mgr), GuestU32(base, mgr + 60), GuestU32(base, mgr + 100),
                          GuestU32(base, mgr + 616), GuestU32(base, mgr + 620), GuestU32(base, mgr + 624),
                          GuestU32(base, mgr + 632), nodes);
              if (n == 0) {  // the whole voice list (+592) once, to compare with the console's (guest_list.py)
                std::string all;
                uint32_t v = GuestU32(base, mgr + 592);
                for (int k = 0; k < 64 && v >= 0x40000000u && v < 0x50000000u; ++k) {
                  const uint32_t obj = GuestU32(base, v);
                  all += fmt::format(" {:08X}/{:08X}", obj,
                                     (obj >= 0x40000000u && obj < 0x50000000u) ? GuestU32(base, obj) : 0u);
                  v = GuestU32(base, v + 4);
                }
                REXLOG_INFO("[ffxiii2_audio] manager +592 list (object/vtable):{}", all);
              }
            }
          }
        }
      }
    }
  }
  __imp__sub_82B8E950(ctx, base);
}
