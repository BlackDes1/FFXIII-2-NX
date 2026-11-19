// ffns_native_game_locks (2026-11-19): the engine's own mutex class on a native lock (fnxf_xiii's
// fnxf_xiii_native_game_locks, recomp-nx/fnxf_xiii, ported to FF XIII-2's addresses).
//
// FF XIII-2's engine mutex is the same 12-byte class as FF XIII's: +0 a mutant handle (CreateMutex(NULL, FALSE, name)
// in its constructor sub_824D2AF8 through sub_82B62E88), +4 / +8 the wrapper's owner tag and count. The mutant is used
// by three functions only (found 2026-11-19 in app/generated: the same shape as FF XIII's sub_8243DA20 / D8C0 / DA78):
//   sub_824D2BB8(lock)  lock:     WaitForSingleObject(lock->handle, INFINITE)   (254 call sites)
//   sub_824D2A58(lock)  try-lock: WaitForSingleObject(lock->handle, 0); 0 when 258 (timeout) or 128, else 1 (2 sites)
//   sub_824D2C10(lock)  unlock:   ReleaseMutex(lock->handle)                    (628 call sites)
// ReleaseMutex (sub_82B62F10) is called only by sub_824D2C10 and sub_82C44D90 (another class with its own mutants,
// created by sub_82C44C58). Each call goes through the kernel object table (its global lock), the mutant's host mutex
// and condition variable; fnxf_xiii measured ~740 acquires a frame on FF XIII's main thread, 4.7 ms -> 1.7 ms a frame.
//
// Here each lock (by its guest address) gets a libnx mutex, recursive like a mutant: an uncontended lock or unlock is
// one atomic operation, a contended one spins up to ffns_native_game_locks_spin_us and then sleeps in the kernel's
// arbiter (which hands the lock to the sleeper at the release: no starvation of the loader, fnxf_xiii seen24/25).
// The guest mutant stays, untouched and always free; its destructor still closes it.
// Same exclusion and results: lock -> 0 (WAIT_OBJECT_0), try-lock 1/0, unlock 1, or 0 when the thread does not own it
// (ReleaseMutex's failure). Differences: a thread that exits holding one leaves it held (a mutant would be abandoned);
// a lock whose handle is 0 keeps the original path. The three functions are in args_exclude.txt (no fast body), so
// every caller reaches these hooks. Init-only key. Switch only; elsewhere the original functions run.
#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>

#include <algorithm>
#include <atomic>
#include <cstdint>

#if defined(__SWITCH__)
#include <switch.h>
#endif

REXCVAR_DEFINE_BOOL(ffns_native_game_locks, true, "FFXIII2",
                    "CPU: the engine's own mutex class (sub_824D2BB8 lock, sub_824D2A58 try-lock, sub_824D2C10 "
                    "unlock) on a native recursive lock per mutex instead of its kernel mutant (object table, host "
                    "mutex and condition variable each call). Same exclusion (fnxf_xiii's native game locks)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

REXCVAR_DEFINE_INT32(ffns_native_game_locks_spin_us, 10, "FFXIII2",
                     "CPU (ffns_native_game_locks): a contended engine mutex is retried for up to this many "
                     "microseconds before the thread sleeps in the kernel (0: sleep at once)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

REXCVAR_DEFINE_BOOL(ffns_native_game_locks_stats, true, "FFXIII2",
                    "Diagnostic (ffns_native_game_locks): every 10 s one log line with the acquires, contended "
                    "acquires and their sleeps (relaxed counters; logged only when logging is on)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

REX_EXTERN(__imp__sub_824D2BB8);
REX_EXTERN(__imp__sub_824D2A58);
REX_EXTERN(__imp__sub_824D2C10);

namespace {

#if defined(__SWITCH__)

uint32_t MutantHandle(const uint8_t* base, uint32_t lock) {
  return __builtin_bswap32(*reinterpret_cast<const uint32_t*>(base + lock));
}

bool Native() {
  static const bool on = [] {
    const bool v = REXCVAR_GET(ffns_native_game_locks);
    REXLOG_INFO("[locks] the engine's mutexes: {}", v ? "native (ffns_native_game_locks)" : "kernel mutants");
    return v;
  }();
  return on;
}

// One per lock, by its guest address; never freed (a lock built again at the same address reuses its entry, free by
// then like the old one). When the table is crowded, new locks keep the original path.
struct Entry {
  std::atomic<uint32_t> key{0};  // the lock's guest address; 0 = free
  Mutex mutex = 0;               // libnx: 0 free, else the owner's handle (| the waiters bit)
  uint32_t depth = 0;            // the owner's recursion depth (only the owner reads or writes it)
};
constexpr uint32_t kEntries = 16384;
Entry g_entries[kEntries];
std::atomic<bool> g_full_logged{false};

Entry* EntryFor(uint32_t lock) {
  const uint32_t start = (lock * 2654435761u) >> 18;  // 14 bits
  for (uint32_t k = 0; k < 64; ++k) {
    Entry& e = g_entries[(start + k) % kEntries];
    uint32_t have = e.key.load(std::memory_order_acquire);
    if (have == 0 && e.key.compare_exchange_strong(have, lock, std::memory_order_acq_rel))
      return &e;
    if (have == lock)
      return &e;
  }
  if (!g_full_logged.exchange(true))
    REXLOG_WARN("[locks] lock table crowded at {:08X}: that lock keeps its kernel mutant", lock);
  return nullptr;
}

uint64_t SpinTicks() {
  static const uint64_t ticks = [] {
    const int32_t us = std::clamp(int32_t(REXCVAR_GET(ffns_native_game_locks_spin_us)), 0, 1000);
    return armNsToTicks(uint64_t(us) * 1000);
  }();
  return ticks;
}

// ---- counters (ffns_native_game_locks_stats) ----
std::atomic<uint64_t> g_acquires{0}, g_contended{0}, g_slept{0}, g_slept_ticks{0};
std::atomic<uint64_t> g_window_tick{0};
bool StatsOn() {
  static const bool on = REXCVAR_GET(ffns_native_game_locks_stats);
  return on;
}
void MaybeReport() {
  const uint64_t now = armGetSystemTick();
  uint64_t start = g_window_tick.load(std::memory_order_relaxed);
  if (start == 0) {
    g_window_tick.compare_exchange_strong(start, now, std::memory_order_relaxed);
    return;
  }
  const uint64_t window = armNsToTicks(10'000'000'000ull);
  if (now - start < window || !g_window_tick.compare_exchange_strong(start, now, std::memory_order_relaxed))
    return;
  const double s = double(armTicksToNs(now - start)) / 1e9;
  const uint64_t a = g_acquires.exchange(0), c = g_contended.exchange(0), sl = g_slept.exchange(0),
                 st = g_slept_ticks.exchange(0);
  REXLOG_INFO("[locks] engine mutexes, 10 s: {:.0f} acquires/s, {:.0f} contended/s, {:.0f} slept/s ({:.2f} ms/s "
              "asleep, all threads)",
              a / s, c / s, sl / s, double(armTicksToNs(st)) / 1e6 / s);
}

void LockContended(Entry& e) {
  if (StatsOn())
    g_contended.fetch_add(1, std::memory_order_relaxed);
  const uint64_t budget = SpinTicks();
  const uint64_t start = armGetSystemTick();
  while (budget && armGetSystemTick() - start < budget) {
    for (int i = 0; i < 16; ++i)
      asm volatile("yield");
    if (__atomic_load_n(&e.mutex, __ATOMIC_RELAXED) == 0 && mutexTryLock(&e.mutex))
      return;
  }
  const uint64_t t0 = armGetSystemTick();
  mutexLock(&e.mutex);
  if (StatsOn()) {
    g_slept.fetch_add(1, std::memory_order_relaxed);
    g_slept_ticks.fetch_add(armGetSystemTick() - t0, std::memory_order_relaxed);
  }
}

void Lock(Entry& e) {
  if (StatsOn()) {
    g_acquires.fetch_add(1, std::memory_order_relaxed);
    MaybeReport();
  }
  if (mutexIsLockedByCurrentThread(&e.mutex)) {
    ++e.depth;
    return;
  }
  if (!mutexTryLock(&e.mutex))
    LockContended(e);
  e.depth = 1;
}

bool TryLock(Entry& e) {
  if (mutexIsLockedByCurrentThread(&e.mutex)) {
    ++e.depth;
    return true;
  }
  if (!mutexTryLock(&e.mutex))
    return false;
  e.depth = 1;
  return true;
}

bool Unlock(Entry& e) {
  if (!mutexIsLockedByCurrentThread(&e.mutex))
    return false;  // ReleaseMutex by a thread that does not own it fails
  if (--e.depth == 0)
    mutexUnlock(&e.mutex);
  return true;
}

// The entry for this call, or null for the original path. An entry is used for every later call once made.
Entry* Pick(const uint8_t* base, uint32_t lock) {
  if (!Native() || lock < 0x10000 || MutantHandle(base, lock) == 0)
    return nullptr;
  return EntryFor(lock);
}

#endif  // __SWITCH__

}  // namespace

REX_HOOK_RAW(sub_824D2BB8) {
#if defined(__SWITCH__)
  if (Entry* e = Pick(base, ctx.r3.u32)) {
    Lock(*e);
    ctx.r3.u64 = 0;  // WAIT_OBJECT_0
    return;
  }
#endif
  __imp__sub_824D2BB8(ctx, base);
}

REX_HOOK_RAW(sub_824D2A58) {
#if defined(__SWITCH__)
  if (Entry* e = Pick(base, ctx.r3.u32)) {
    ctx.r3.u64 = TryLock(*e) ? 1 : 0;
    return;
  }
#endif
  __imp__sub_824D2A58(ctx, base);
}

REX_HOOK_RAW(sub_824D2C10) {
#if defined(__SWITCH__)
  if (Entry* e = Pick(base, ctx.r3.u32)) {
    ctx.r3.u64 = Unlock(*e) ? 1 : 0;
    return;
  }
#endif
  __imp__sub_824D2C10(ctx, base);
}
