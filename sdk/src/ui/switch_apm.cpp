/*
 * Horizon console performance profile (apm).
 *
 * Why
 *   The GPU runs at 307.2 MHz in handheld mode and it is one of the two walls of the port (the other
 *   is the CPU, with three cores at 85-99 %). Nintendo later released the 460.8 MHz handheld profile
 *   precisely for games that need it, and this one does: 25 FPS with the GPU at its limit. It is not
 *   an overclock: no KIP or sysmodule is touched, it is requested through Horizon's official API
 *   (apm), the same one any commercial game uses. That is why not every game requests it: those that
 *   do not need it get longer battery life.
 *
 *   307.2 -> 460.8 MHz is +50 % of GPU.
 *
 * Memory is not touched
 *   The first version raised the memory from 1331.2 to 1597.5 MHz by accident: apm does not take MHz,
 *   it takes a configuration number, and the high-GPU configurations come in two flavors, with EMC
 *   1331.2 and with EMC 1600. The 1600 one got in. A clock overlay showed it, and a commercial game
 *   runs at GPU 460.8 with MEM 1331.2: so that combination exists and it is the one we want.
 *
 *   Raising the memory clock uses more battery and heat without giving us anything: our own
 *   measurements say bandwidth is at 27 % and what saturates is the fragment ALU.
 *
 * The three rounds it took, and what each one taught
 *
 *   1. It applied the configuration and read the clocks on the next line. The log said "the
 *      memory stays at 1331.2" and the session profile recorded 1600.0 in every sample. They
 *      did not contradict each other: it was checked too early. apm hands the change to pcv and
 *      the clocks are applied later, so the immediate read returned the old value. The same bug
 *      left dead a fix that only lowered the EMC by hand "if it is detected that it went up": it
 *      was never detected.
 *
 *   2. A 250 ms wait plus a once-per-second watchdog that lowered the EMC with clkrst. The log said
 *      the two things that needed knowing:
 *        - after 250 ms the memory was still at 1331.2 (so the raise takes between 0.25 and ~1 s,
 *          and 250 ms was not enough either);
 *        - the watchdog found it at 1600 and lowered it eight times with clkrst; all eight were
 *          accepted and all eight were reverted by the system. pcv reimposes the EMC of the active
 *          apm configuration, and clkrst cannot fight that.
 *
 *   3. So the problem was never how to lower the memory, but which configuration is requested.
 *      0x92220007 is "GPU 460.8 + EMC 1600", and its sibling 0x92220008 is "GPU 460.8 + EMC
 *      1331.2": that is the right one, and it was missing from the list (a nonexistent 0x92220006
 *      was there instead). Now:
 *        1. the table has the real IDs, sorted with the low EMC first;
 *        2. the check is a 1.5 s poll (StillMemoryDuring), not a single read;
 *        3. the watchdog stays as a safety net, in case the system reimposes the EMC on a mode
 *           change or when waking from sleep.
 *
 *   It only ever lowers back to what the console had at the start: nothing is ever raised above
 *   that. And if this firmware did not have the "high GPU + unchanged RAM" pair, the GPU is left
 *   as it was rather than raising the RAM; masseffect_switch_ram_1600 opts into that trade.
 */

#include "rex/ui/switch_apm.h"

#if REX_PLATFORM_SWITCH

#include <switch.h>

#include <atomic>
#include <cstdio>
#include <mutex>

/*
 * FFXIII-2-Native-Switch (2026-11-11): ONE clkrst session per clock (CPU, GPU, EMC) for the whole run, shared by
 * everything in the game that reads or sets a clock: this file's apm checks and CPU pin thread, and switch_perf's
 * "clocks:" line. Before, the game held 4 sessions for the run (3 for the profiler line, 1 for the CPU pin thread)
 * and opened and closed 2 more for every apm check. clkrst sessions are served by pcv, whose session memory is small
 * and shared with every other client (hoc-clk / sys-clk and the clock and status overlays). On 2026-11-11 pcv ran out
 * of it during play (Atmosphere fatal 2011-0102, hipc::ResultOutOfSessionMemory, in pcv; ptm and Bus followed with
 * 2011-0301): now the game takes at most 3, opened on first use and kept (the process end closes them), and a session
 * that cannot be opened is not asked for again.
 */
namespace {
std::mutex g_clk_mutex;
bool g_clk_service_tried = false, g_clk_service = false;
ClkrstSession g_clk[3]{};
bool g_clk_tried[3]{}, g_clk_open[3]{};
constexpr PcvModuleId kClkModules[3] = {PcvModuleId_CpuBus, PcvModuleId_GPU, PcvModuleId_EMC};

// With g_clk_mutex held.
ClkrstSession* ClockSessionLocked(int which) {
  if (which < 0 || which > 2) return nullptr;
  if (!g_clk_service_tried) {
    g_clk_service_tried = true;
    g_clk_service = R_SUCCEEDED(clkrstInitialize());
  }
  if (!g_clk_service) return nullptr;
  if (!g_clk_tried[which]) {
    g_clk_tried[which] = true;
    g_clk_open[which] = R_SUCCEEDED(clkrstOpenSession(&g_clk[which], kClkModules[which], 3));
  }
  return g_clk_open[which] ? &g_clk[which] : nullptr;
}
}  // namespace

extern "C" int RexSwitchClockRead(int which, unsigned int* hz) {
  if (hz) *hz = 0;
  std::lock_guard<std::mutex> lock(g_clk_mutex);
  ClkrstSession* s = ClockSessionLocked(which);
  u32 v = 0;
  const int ok = s && hz && R_SUCCEEDED(clkrstGetClockRate(s, &v)) ? 1 : 0;
  if (hz) *hz = v;
  return ok;
}

extern "C" int RexSwitchClockSet(int which, unsigned int hz) {
  std::lock_guard<std::mutex> lock(g_clk_mutex);
  ClkrstSession* s = ClockSessionLocked(which);
  return s && R_SUCCEEDED(clkrstSetClockRate(s, hz)) ? 1 : 0;
}

namespace {

std::atomic<int> g_request{0};
std::atomic<bool> g_done{false};
/*
 * The escape hatch. By default no configuration that raises the RAM clock is accepted, even if that
 * means staying with the low GPU clock. If the firmware turned out not to have the "high GPU +
 * unchanged RAM" pair, setting this to true takes the RAM 1600 one anyway.
 */
std::atomic<bool> g_allow_ram_1600{false};

/*
 * Watchdog state. g_emc_original is the memory clock the console had before we touched anything, in
 * Hz, and it is 0 while we have not changed the configuration: with 0 the watchdog does nothing at
 * all (it does not even open clkrst).
 */
std::atomic<u32> g_emc_original{0};
std::atomic<int> g_ticks_emc{0};
std::atomic<int> g_fixes_emc{0};
std::atomic<bool> g_warning_emc{false};
std::atomic<bool> g_up_given_emc{false};

/*
 * How often the watchdog looks. For the first kConsecutiveTicks ticks (one second each) it always looks,
 * which is when the system finishes applying the change (the profile already saw 1600 in its first
 * sample, at 8 s). From then on, one of every kSpacedTicks, to catch late raises (mode change,
 * waking from sleep) without opening clkrst every second for the whole session.
 */
constexpr int kConsecutiveTicks = 20;
constexpr int kSpacedTicks = 30;
/*
 * And a cap on corrections. Measured: eight clkrst reductions were all accepted and all reverted by
 * pcv, which reimposes the EMC of the active apm configuration. With the corrected table this should
 * never fire; if it does, the chosen configuration is not the one we thought, and insisting fixes
 * nothing. It is logged and left alone.
 */
constexpr int kMaxFixesEmc = 3;

/*
 * What is watched after requesting the configuration, before accepting anything.
 *
 * Waiting 250 ms was still not enough: the log said "the memory stays at 1331.2 MHz (checked
 * 250 ms later)" and one second later the watchdog already found it at 1600. So the raise takes
 * between 0.25 and ~1 s. Now it polls every 150 ms for 1.5 s, and a single read that sees it moved
 * is enough to reject the configuration. It only costs that second and a half when the candidate is
 * good; when it is bad it stops as soon as it shows.
 */
constexpr u64 kPollStepNs = 150000000ULL;
constexpr int kPollSteps = 10;  // 10 x 150 ms = 1,5 s
constexpr u32 kMarginHz = 20000000u;  // 20 MHz: 1597.5 and 1331.2 are nowhere near each other

/*
 * Real hardware clocks, in Hz. Returns false if clkrst is not available. Through the shared sessions above
 * (2026-11-11: no session is opened and closed per call any more).
 */
bool ReadClocks(u32& gpu_hz, u32& emc_hz) {
  const bool gpu_ok = RexSwitchClockRead(1, &gpu_hz) != 0;
  const bool emc_ok = RexSwitchClockRead(2, &emc_hz) != 0;
  return gpu_ok && emc_ok;
}

/*
 * Polls the clocks for kPollSteps x kPollStepNs and returns true if the memory clock stayed where
 * it was the whole time. As soon as one read sees it moved, it stops and returns false, leaving the
 * last values read in gpu/emc. A single read is not enough: one read at 250 ms said the memory was
 * unchanged when it was about to go up a moment later.
 */
bool StillMemoryDuring(u32 emc0, u32& gpu, u32& emc) {
  for (int i = 0; i < kPollSteps; ++i) {
    svcSleepThread(kPollStepNs);
    if (!ReadClocks(gpu, emc)) {
      return false;
    }
    if ((emc > emc0 ? emc - emc0 : emc0 - emc) > kMarginHz) {
      return false;
    }
  }
  return true;
}

/*
 * Sets the memory module clock to hz. Returns whether the request was accepted (not whether the clock
 * already changed: that is checked later, which is exactly the lesson of the first attempt).
 */
bool PinMemory(u32 hz) {
  return RexSwitchClockSet(2, hz) != 0;
}

struct Candidate {
  int mhz;
  uint32_t config;
  int emc;  // log only: what Horizon's table says this configuration sets
};
/*
 * The correct table. The first two are confirmed by a console log:
 *
 *   0x00020003  GPU 307.2  EMC 1331.2   <- "starting point: config 0x00020003 ... memory 1331.2"
 *   0x92220007  GPU 460.8  EMC 1600     <- "SET 0x92220007" and the memory ended at 1600
 *
 * and they match Horizon's PerformanceConfiguration table exactly. The one we want is the sibling of
 * the last one, 0x92220008 = GPU 460.8 with EMC 1331.2, which is exactly the pair a commercial game
 * uses. An earlier list lacked it (it had 0x92220006, which does not exist), so the 1600 one always
 * got in.
 *
 * They are sorted by EMC: first the one that does not touch the memory. Requesting one that does not
 * exist returns an error and changes nothing, so it is enough to list them and let the check decide.
 */
constexpr Candidate kCandidate[] = {
    {460, 0x92220008u, 1331},  // GPU 460.8 + EMC 1331.2: the right one
    {460, 0x92220007u, 1600},  // GPU 460.8 + EMC 1600: raises the RAM clock
    {384, 0x00020004u, 1331},  // GPU 384 + EMC 1331,2
    {384, 0x00010000u, 1600},  // GPU 384 + EMC 1600
};


}  // namespace

extern "C" void RexSwitchApmRequestGpuMhz(int mhz) {
  g_request.store(mhz, std::memory_order_relaxed);
}

extern "C" void RexSwitchApmAllowRam1600(int allow) {
  g_allow_ram_1600.store(allow != 0, std::memory_order_relaxed);
}

extern "C" void RexSwitchApmApply(void) {
  const int mhz = g_request.load(std::memory_order_relaxed);
  if (mhz <= 0 || g_done.exchange(true)) {
    return;
  }

  ApmPerformanceMode mode = ApmPerformanceMode_Invalid;
  if (R_FAILED(apmGetPerformanceMode(&mode))) {
    std::fprintf(stderr, "[apm] could not read the performance mode; nothing is touched\n");
    return;
  }
  if (mode != ApmPerformanceMode_Normal) {
    std::fprintf(stderr, "[apm] the console is docked (mode %d): nothing is requested\n", int(mode));
    return;
  }

  uint32_t config_original = 0;
  const bool has_original = R_SUCCEEDED(apmGetPerformanceConfiguration(mode, &config_original));

  u32 gpu0 = 0, emc0 = 0;
  if (!ReadClocks(gpu0, emc0)) {
    std::fprintf(stderr,
                 "[apm] cannot read the real clocks (clkrst): NOTHING is changed, because without "
                 "checking there is no way to guarantee the memory stays where it is\n");
    return;
  }
  std::fprintf(stderr, "[apm] starting point: config 0x%08X, GPU %.1f MHz, memory %.1f MHz\n",
               has_original ? config_original : 0u, double(gpu0) / 1e6, double(emc0) / 1e6);

  const u32 goal_gpu_hz = u32(mhz) * 1000000u;

  /*
   * They are tried in table order (low EMC first) and the first one that raises the GPU without
   * moving the memory is accepted. The check is the 1.5 s poll, not a single read: a read at 250 ms
   * once said the memory was still at 1331.2, and one second later it was at 1600.
   */
  for (const Candidate& c : kCandidate) {
    if (c.mhz != mhz) {
      continue;
    }
    if (R_FAILED(apmSetPerformanceConfiguration(mode, c.config))) {
      continue;  // that configuration does not exist on this firmware
    }
    u32 gpu1 = 0, emc1 = 0;
    const bool still_of_truth = StillMemoryDuring(emc0, gpu1, emc1);
    const bool still_memory =
        still_of_truth || g_allow_ram_1600.load(std::memory_order_relaxed);
    const bool gpu_rose = gpu1 + kMarginHz >= goal_gpu_hz;
    if (gpu_rose && still_memory) {
      if (!still_of_truth) {
        std::fprintf(stderr,
                     "[apm] 0x%08X raises the memory to %.1f MHz, but masseffect_switch_ram_1600 is "
                     "true: it is accepted and the memory is NOT watched\n",
                     c.config, double(emc1) / 1e6);
        std::fprintf(stderr, "[apm] SET 0x%08X: GPU %.1f MHz (was %.1f), memory %.1f MHz\n",
                     c.config, double(gpu1) / 1e6, double(gpu0) / 1e6, double(emc1) / 1e6);
        return;  // watchdog not armed: lowering it would fight what the cvar asks for
      }
      /*
       * Accepted. The watchdog is armed anyway: the system may reimpose the EMC later,
       * on a mode change or when waking from sleep.
       */
      g_emc_original.store(emc0, std::memory_order_relaxed);
      std::fprintf(stderr,
                   "[apm] SET configuration 0x%08X (table: GPU %d, EMC %d): GPU %.1f MHz (was "
                   "%.1f) and the memory stays at %.1f MHz, checked for %.1f s in a row; "
                   "it stays watched\n",
                   c.config, c.mhz, c.emc, double(gpu1) / 1e6, double(gpu0) / 1e6,
                   double(emc1) / 1e6, double(kPollStepNs) * kPollSteps / 1e9);
      return;
    }
    std::fprintf(stderr, "[apm] discarded 0x%08X: GPU %.1f MHz, memory %.1f MHz (%s)\n", c.config,
                 double(gpu1) / 1e6, double(emc1) / 1e6,
                 !gpu_rose ? "the GPU does not reach the requested clock" : "IT MOVES THE MEMORY");
  }

  /*
   * None worked. The memory is not lowered by hand: that was tried eight times with clkrst, all
   * eight were accepted and all eight were reverted by the system in under a second. pcv reimposes
   * the EMC of the active apm configuration, and clkrst cannot fight that.
   *
   * So the console is left as it was, and the log says exactly what trade is available and how to
   * take it (masseffect_switch_ram_1600).
   */
  if (has_original && R_SUCCEEDED(apmSetPerformanceConfiguration(mode, config_original))) {
    u32 gpu2 = 0, emc2 = 0;
    StillMemoryDuring(emc0, gpu2, emc2);
    std::fprintf(stderr,
                 "[apm] this firmware has NO %d MHz configuration that leaves the memory "
                 "at %.1f MHz; going back to the original 0x%08X (GPU %.1f MHz, memory %.1f MHz).\n"
                 "[apm] IF YOU PREFER THE HIGH GPU CLOCK EVEN IF THE RAM GOES UP TO 1600: set "
                 "masseffect_switch_ram_1600 = true in masseffect.toml\n",
                 mhz, double(emc0) / 1e6, config_original, double(gpu2) / 1e6, double(emc2) / 1e6);
  } else {
    std::fprintf(stderr, "[apm] no %d MHz configuration worked and the original could NOT be restored: "
                         "check the clocks in the overlay\n",
                 mhz);
  }
}

extern "C" void RexSwitchApmWatch(void) {
  const u32 emc0 = g_emc_original.load(std::memory_order_relaxed);
  if (emc0 == 0 || g_up_given_emc.load(std::memory_order_relaxed)) {
    return;  // nothing changed, or we already know this firmware does not allow it
  }

  const int tic = g_ticks_emc.fetch_add(1, std::memory_order_relaxed);
  if (tic >= kConsecutiveTicks && (tic % kSpacedTicks) != 0) {
    return;
  }

  u32 gpu = 0, emc = 0;
  if (!ReadClocks(gpu, emc) || emc <= emc0 + kMarginHz) {
    return;  // where it should be
  }

  /* It moved: the configuration raised it after we checked. */
  const bool drop = PinMemory(emc0);
  const int n = g_fixes_emc.fetch_add(1, std::memory_order_relaxed) + 1;
  if (!g_warning_emc.exchange(true)) {
    std::fprintf(stderr,
                 "[apm] the memory had gone up by itself to %.1f MHz (t=%d s): lowering it to %.1f with clkrst "
                 "(%s)\n",
                 double(emc) / 1e6, tic, double(emc0) / 1e6, drop ? "accepted" : "REJECTED");
  }
  if (!drop || n >= kMaxFixesEmc) {
    g_up_given_emc.store(true, std::memory_order_relaxed);
    std::fprintf(stderr,
                 "[apm] giving up on the memory after %d attempt(s): this firmware "
                 "reimposes it. The GPU stays at the requested clock; the RAM at whatever the system dictates\n",
                 n);
  }
}


/*
 * FFXIII-2-Native-Switch (2026-11-10, ffns_cpu_mhz, the user's toggle "without sys-clock"): the CPU clock set by the game
 * itself through clkrst. Horizon puts its own clock (1020) back again and again: p10b set it once a second and read 1020
 * at every next second, and 1428 then gave +12 % instead of the ~40 % the clock is worth. So a small thread of its own
 * (the console's core 3, priority 0x2C, asleep in between) reads the clock every 20 ms and sets it again when it moved,
 * and logs every 10 s how often it had to. 0 = the console's own clock: the one found before the first change is put
 * back (also by RexSwitchApmCpuRestore at exit). Clamped to 1020..1581 MHz (the user's limit, raised from 1428 on 2026-11-12).
 */
namespace {
std::atomic<int> g_cpu_want_mhz{0};
std::atomic<bool> g_cpu_given_up{false};  // another clock tool owns the CPU clock for this run
std::atomic<u32> g_cpu_stock_hz{0};  // the clock before our first change; 0 = never changed
std::atomic<bool> g_cpu_thread_started{false};
std::atomic<bool> g_cpu_stop{false};
constexpr u32 kCpuMarginHz = 10000000u;
constexpr u32 kCpuStockHz = 1020000000u;
constexpr u64 kCpuPollNs = 20000000ULL;  // 20 ms

// The shared CPU session (RexSwitchClockRead / Set, kept for the run: a rate set through a session might end with it).
Thread g_cpu_thread;

bool CpuClock(u32* read_hz, const u32* set_hz) {
  return set_hz ? RexSwitchClockSet(0, *set_hz) != 0 : RexSwitchClockRead(0, read_hz) != 0;
}

/*
 * 2026-11-11: another clock manager (hoc-clk / sys-clk, an overlay) that keeps putting its own CPU clock back is not
 * fought. Horizon itself resets the clock now and then (a few times in a 10 s window: 3 on 2026-11-11, about 10 in
 * p10b); a manager polling every few hundred ms does it far more often. Two 10 s windows in a row with more than
 * kCpuFightResets resets each: the game leaves the CPU clock to that tool for the rest of the run and says so.
 */
constexpr int kCpuFightResets = 25;

// FFXIII-2-Native-Switch (2026-11-12, ffns_gpu_mhz, the user: "GPU can use 537"): the GPU clock the same way, from
// the same thread (the shared GPU session of RexSwitchClockRead / Set). 0 = as the console sets it (handheld 460.8 with
// banjo_gpu_boost); 537 = 537.6 MHz. The clock found before the first set is put back at 0 and at exit.
std::atomic<int> g_gpu_want_mhz{0};
std::atomic<u32> g_gpu_stock_hz{0};
constexpr u32 kGpuOcHz = 537600000u;
void GpuPinStep() {
  static bool restored = true;
  static int sets = 0, polls = 0;
  const int want = g_gpu_want_mhz.load(std::memory_order_relaxed);
  if (want <= 0 && restored) return;
  u32 now_hz = 0;
  if (!RexSwitchClockRead(1, &now_hz) || !now_hz) return;
  u32 goal;
  if (want > 0) {
    if (g_gpu_stock_hz.load(std::memory_order_relaxed) == 0)
      g_gpu_stock_hz.store(now_hz < kGpuOcHz - 5000000u ? now_hz : 460800000u, std::memory_order_relaxed);
    goal = kGpuOcHz;
    restored = false;
  } else {
    goal = g_gpu_stock_hz.load(std::memory_order_relaxed);
    restored = true;
  }
  if (goal && (now_hz > goal ? now_hz - goal : goal - now_hz) > 5000000u) {
    const bool ok = RexSwitchClockSet(1, goal) != 0;
    if (sets++ < 8 || !ok)
      std::fprintf(stderr, "[gpu clock] found at %.1f MHz, set to %.1f MHz%s (ffns_gpu_mhz)\n", double(now_hz) / 1e6,
                   double(goal) / 1e6, ok ? "" : ": REFUSED");
  }
  if (++polls % 3000 == 0)
    std::fprintf(stderr, "[gpu clock] %d reads, set %d times, now %.1f MHz, wanted %.1f MHz\n", polls, sets,
                 double(now_hz) / 1e6, double(goal) / 1e6);
}

void CpuPinThread(void*) {
  u64 last_report = armGetSystemTick();
  int polls = 0, resets = 0, refused = 0, failures = 0, fight_windows = 0;
  u32 seen_hz = 0;
  bool restored = true;
  while (!g_cpu_stop.load(std::memory_order_relaxed)) {
    svcSleepThread(kCpuPollNs);
    GpuPinStep();
    const int want = g_cpu_want_mhz.load(std::memory_order_relaxed);
    const u32 stock = g_cpu_stock_hz.load(std::memory_order_relaxed);
    if (want <= 0 && restored) continue;
    u32 now_hz = 0;
    if (!CpuClock(&now_hz, nullptr)) {
      if (++failures == 50) std::fprintf(stderr, "[cpu clock] clkrst does not answer: giving up\n");
      if (failures >= 50) return;
      continue;
    }
    ++polls;
    u32 goal;
    if (want > 0) {
      if (stock == 0)
        g_cpu_stock_hz.store(now_hz && now_hz <= kCpuStockHz + kCpuMarginHz ? now_hz : kCpuStockHz,
                             std::memory_order_relaxed);
      goal = u32(want) * 1000000u;
      restored = false;
    } else {
      goal = stock;
    }
    if ((now_hz > goal ? now_hz - goal : goal - now_hz) > kCpuMarginHz) {
      seen_hz = now_hz;
      if (CpuClock(nullptr, &goal)) ++resets; else ++refused;
    }
    if (want <= 0 && !restored) {
      restored = true;
      std::fprintf(stderr, "[cpu clock] ffns_cpu_mhz = 0: the console's own %.1f MHz back\n", double(goal) / 1e6);
    }
    const u64 tick = armGetSystemTick();
    if (armTicksToNs(tick - last_report) >= 10000000000ULL) {
      std::fprintf(stderr, "[cpu clock] last 10 s: %d reads, set again %d times (found at %.1f MHz)%s, wanted %.1f MHz\n",
                   polls, resets, double(seen_hz) / 1e6, refused ? " SOME REFUSED" : "", double(goal) / 1e6);
      fight_windows = (want > 0 && resets > kCpuFightResets) ? fight_windows + 1 : 0;
      polls = resets = refused = 0;
      last_report = tick;
      if (fight_windows >= 2) {
        std::fprintf(stderr, "[cpu clock] another clock tool (hoc-clk / sys-clk?) keeps setting the CPU to %.1f MHz: "
                             "the game stops setting it and leaves the CPU clock to that tool for this run\n",
                     double(seen_hz) / 1e6);
        g_cpu_want_mhz.store(0, std::memory_order_relaxed);  // (the thread goes on for the GPU clock)
        g_cpu_given_up.store(true, std::memory_order_relaxed);
        fight_windows = 0;
      }
    }
  }
}
}  // namespace

extern "C" void RexSwitchApmSetGpuMhz(int mhz) {
  g_gpu_want_mhz.store(mhz >= 500 ? 537 : 0, std::memory_order_relaxed);  // 537.6 MHz or the console's own
}

extern "C" void RexSwitchApmSetCpuMhz(int mhz) {
  if (g_cpu_given_up.load(std::memory_order_relaxed)) return;
  if (mhz > 0) mhz = mhz < 1020 ? 1020 : (mhz > 1581 ? 1581 : mhz);
  g_cpu_want_mhz.store(mhz < 0 ? 0 : mhz, std::memory_order_relaxed);
}

// Called once a second (switch_perf.cpp): starts the pin thread the first time a clock is wanted.
extern "C" void RexSwitchApmCpuWatch(void) {
  if ((g_cpu_want_mhz.load(std::memory_order_relaxed) <= 0 && g_gpu_want_mhz.load(std::memory_order_relaxed) <= 0) ||
      g_cpu_thread_started.exchange(true))
    return;
  u64 mask = 0;
  svcGetInfo(&mask, InfoType_CoreMask, CUR_PROCESS_HANDLE, 0);
  const int core = (mask & (1u << 3)) ? 3 : -2;
  if (R_FAILED(threadCreate(&g_cpu_thread, CpuPinThread, nullptr, nullptr, 0x4000, 0x2C, core)) ||
      R_FAILED(threadStart(&g_cpu_thread))) {
    std::fprintf(stderr, "[cpu clock] the pin thread could not start: ffns_cpu_mhz has no effect\n");
    return;
  }
  std::fprintf(stderr, "[cpu clock] pin thread started on core %d (every %llu ms)\n", core,
               static_cast<unsigned long long>(kCpuPollNs / 1000000ULL));
}

extern "C" void RexSwitchApmCpuRestore(void) {
  g_cpu_stop.store(true, std::memory_order_relaxed);
  svcSleepThread(50000000ULL);  // the pin thread's last pass (20 ms) is over before the clock goes back
  u32 stock = g_cpu_stock_hz.load(std::memory_order_relaxed);
  if (stock != 0) CpuClock(nullptr, &stock);
  const u32 gpu_stock = g_gpu_stock_hz.load(std::memory_order_relaxed);
  if (gpu_stock != 0) RexSwitchClockSet(1, gpu_stock);  // ffns_gpu_mhz
}

#else   // !REX_PLATFORM_SWITCH
extern "C" void RexSwitchApmSetCpuMhz(int) {}
extern "C" void RexSwitchApmSetGpuMhz(int) {}
extern "C" void RexSwitchApmCpuWatch(void) {}
extern "C" void RexSwitchApmCpuRestore(void) {}
extern "C" void RexSwitchApmRequestGpuMhz(int) {}
extern "C" void RexSwitchApmAllowRam1600(int) {}
extern "C" void RexSwitchApmApply(void) {}
extern "C" void RexSwitchApmWatch(void) {}
extern "C" int RexSwitchClockRead(int, unsigned int* hz) { if (hz) *hz = 0; return 0; }
extern "C" int RexSwitchClockSet(int, unsigned int) { return 0; }
#endif  // REX_PLATFORM_SWITCH


extern "C" void RexSwitchApmCpuBoost(int mode) {
  if (mode <= 0) return;
  const Result rc = appletSetCpuBoostMode(ApmCpuBoostMode(mode));
  std::fprintf(stderr, "[apm] appletSetCpuBoostMode(%d) -> 0x%X\n", mode, unsigned(rc));
}
