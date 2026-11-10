// ffxiii2-nx (2026-11-05): a backoff for the game's guest polling loops (tools/spin_polls.py marks them with
// REX_SPIN_POLL(), which calls rex_spin_poll() once per iteration).
//
// Why: the console run of rc1 (switchlog/Normal Run, rex_profile.log) showed the four "vfx-qix-effect" worker
// threads at ~60 % CPU each (~240 % of the three cores the game has) and the main thread at 14 %, the game at 6-7 fps
// in the field. The sampled profile (switchlog/Profile with A and B) puts 99 % of the workers' samples in three
// polling loops of the effect job system: sub_828B8310 (a counting barrier, 64 %), sub_828BADA0 (an inlined wait for the
// leader's work pointer, 28 %) and sub_828B8298 (the leader election's wait, 8 %). On the Xbox 360 the workers had six
// hardware threads; here four workers busy-wait on three cores at the main thread's priority (0x3B) and take the cores
// from the threads they wait for.
//
// What: a waiting iteration first issues the CPU's yield hint (the first 64 polls: short waits stay as fast as
// before), then yields the core to another ready thread (svcSleepThread(0), the next 32), then sleeps 25 us per poll
// until the word changes. The loop still re-reads the guest word every iteration and leaves exactly when it did
// before: the same values, the same order of guest memory accesses; only how long a waiting thread holds the core
// changes. A wait is new when more than ~5 us passed since this thread's last poll.
// The key ffxiii2_spin_backoff (default on) and the in-run A/B target 19 of banjo_renut_ab_target switch it off.
// Performance is the console's call: renut_ab.toml with target 19 measures it there.
#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>

#include <rex/cvar.h>

#if REX_PLATFORM_SWITCH
#include <switch.h>
#elif defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#endif

REXCVAR_DEFINE_BOOL(ffxiii2_spin_backoff, true, "FFXIII2",
                    "CPU enhancement (the console): the game's guest polling loops (tools/spin_polls.py) give the core "
                    "up while they wait: the CPU's yield hint for the first 64 polls, then a yield to another ready "
                    "thread, then 25 us sleeps until the word they poll changes. Same values and order of memory "
                    "accesses; in-run A/B target 19 of banjo_renut_ab_target");

namespace {
std::atomic<int> g_spin_state{-1};  // -1 not read yet, 0 off, 1 on
std::atomic<bool> g_spin_ab_off{false};

constexpr uint32_t kHintPolls = 64;
constexpr uint32_t kYieldPolls = 96;

thread_local uint64_t t_last = 0;
thread_local uint32_t t_polls = 0;

inline uint64_t Now() {
#if REX_PLATFORM_SWITCH
  return armGetSystemTick();  // 19.2 MHz
#else
  return uint64_t(std::chrono::steady_clock::now().time_since_epoch().count());
#endif
}

#if REX_PLATFORM_SWITCH
constexpr uint64_t kNewWaitTicks = 96;  // 5 us
#else
constexpr uint64_t kNewWaitTicks =
    uint64_t(std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::microseconds(5)).count());
#endif

inline void CpuHint() {
#if REX_PLATFORM_SWITCH || defined(__aarch64__)
  __asm__ __volatile__("yield");
#elif defined(__x86_64__) || defined(_M_X64)
  _mm_pause();
#endif
}
}  // namespace

// The in-run A/B (banjo_renut_tuning.cpp, AbApply target 19): true switches the backoff off for the off half.
void Ffxiii2SpinPollAbOff(bool off) { g_spin_ab_off.store(off, std::memory_order_relaxed); }

extern "C" void rex_spin_poll(void) {
  int state = g_spin_state.load(std::memory_order_relaxed);
  if (state < 0) {
    state = REXCVAR_GET(ffxiii2_spin_backoff) ? 1 : 0;
    g_spin_state.store(state, std::memory_order_relaxed);
  }
  if (!state || g_spin_ab_off.load(std::memory_order_relaxed))
    return;
  const uint64_t now = Now();
  if (now - t_last > kNewWaitTicks)
    t_polls = 0;
  ++t_polls;
  if (t_polls <= kHintPolls) {
    CpuHint();
  } else if (t_polls <= kYieldPolls) {
#if REX_PLATFORM_SWITCH
    svcSleepThread(0);
#else
    std::this_thread::yield();
#endif
  } else {
#if REX_PLATFORM_SWITCH
    svcSleepThread(25000);
#else
    std::this_thread::yield();
#endif
  }
  t_last = Now();
}
