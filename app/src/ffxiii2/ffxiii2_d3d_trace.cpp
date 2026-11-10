// ffxiii2-nx (2026-11-03): PC trace of FFXIII-2's Direct3D area, to pair Renut's hooks (written at Zuma's Revenge's
// addresses, XDK 2.0.20871) with this game's functions where the address alignment does not settle it
// (docs/ffxiii2-nx.md, "Renut's hooks at FF's addresses"). feedingfrenzy-nx's method (zumasrevenge-nx's
// zumasrevenge_d3d_trace.cpp): every function of 0x82840000-0x82960000 gets a pass-through hook
// (ffxiii2_d3d_trace_hooks.inc, tools/ffxiii2_d3d_trace_gen.py).
//
// ffxiii2_d3d_trace_s = N (0 = off): from the first Swap (sub_82858630) N seconds after start, the calls of the next
// three frames are recorded in order (function, r3-r6, the caller's LR); then ffxiii2_d3d_trace.txt (next to the
// executable) gets, per function: all calls so far, the calls per frame of the window, the first calls' arguments and
// whether r3 was the device (Swap's r3), and the window's call sequence. PC test builds only (FFXIII2_D3D_TRACE).
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <vector>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>

REXCVAR_DEFINE_INT32(ffxiii2_d3d_trace_s, 0, "FFXIII2",
                     "PC test aid: record FFXIII-2's Direct3D calls for three frames from this many seconds after "
                     "start (0 = off); writes ffxiii2_d3d_trace.txt");

namespace ffxiii2::d3dtrace {
void Note(int index, PPCContext& ctx);
}

#include "ffxiii2_d3d_trace_hooks.inc"

namespace ffxiii2::d3dtrace {
namespace {

constexpr uint32_t kSwap = 0x82858630u;
constexpr int kFirst = 4;
constexpr size_t kMaxSequence = 60000;
struct Args {
  uint32_t r[6];
  uint32_t lr;
};
struct Stat {
  std::atomic<uint64_t> calls{0};
  uint64_t window = 0;
  int first_n = 0;
  Args first[kFirst];
};
struct Event {
  uint16_t index;
  uint32_t r3, r4, r5, r6, lr;
};
Stat g_stats[FFXIII2_D3D_TRACE_COUNT];
std::mutex g_mutex;
std::vector<Event> g_seq;
std::atomic<int> g_state{0};  // 0 waiting, 1 recording, 2 written
int g_frames = 0;
uint32_t g_device = 0;
int g_swap_index = -1;
std::chrono::steady_clock::time_point g_start;
std::once_flag g_once;

void Write() {
  FILE* f = std::fopen("ffxiii2_d3d_trace.txt", "w");
  if (!f) {
    REXLOG_WARN("[d3dtrace] cannot write ffxiii2_d3d_trace.txt");
    return;
  }
  std::fprintf(f, "# ffxiii2_d3d_trace: device %08X, %d frames recorded, %zu calls in them\n", g_device, g_frames,
               g_seq.size());
  std::fprintf(f, "# addr calls window_per_frame | first calls: r3 r4 r5 r6 r7 r8 lr (r3=D: the device)\n");
  for (int i = 0; i < FFXIII2_D3D_TRACE_COUNT; ++i) {
    const Stat& s = g_stats[i];
    const uint64_t calls = s.calls.load();
    if (!calls) continue;
    std::fprintf(f, "%08X %llu %.1f |", kTraceAddr[i], (unsigned long long)calls,
                 g_frames ? double(s.window) / g_frames : 0.0);
    for (int k = 0; k < s.first_n; ++k) {
      const Args& a = s.first[k];
      std::fprintf(f, " [%s %08X %08X %08X %08X %08X lr %08X]", a.r[0] == g_device && g_device ? "D" : "",
                   a.r[0], a.r[1], a.r[2], a.r[3], a.r[4], a.lr);
    }
    std::fprintf(f, "\n");
  }
  std::fprintf(f, "# sequence: addr r3 r4 r5 r6 lr\n");
  for (const Event& e : g_seq)
    std::fprintf(f, "S %08X %s%08X %08X %08X %08X %08X\n", kTraceAddr[e.index],
                 e.r3 == g_device && g_device ? "D:" : "", e.r3, e.r4, e.r5, e.r6, e.lr);
  std::fclose(f);
  REXLOG_INFO("[d3dtrace] ffxiii2_d3d_trace.txt written: {} frames, {} calls", g_frames, g_seq.size());
}

}  // namespace

void Note(int index, PPCContext& ctx) {
  const int32_t after = REXCVAR_GET(ffxiii2_d3d_trace_s);
  if (after <= 0) return;
  std::call_once(g_once, [] {
    g_start = std::chrono::steady_clock::now();
    for (int i = 0; i < FFXIII2_D3D_TRACE_COUNT; ++i)
      if (kTraceAddr[i] == kSwap) g_swap_index = i;
  });
  const int state = g_state.load(std::memory_order_acquire);
  if (state == 2) return;
  Stat& s = g_stats[index];
  s.calls.fetch_add(1, std::memory_order_relaxed);
  // The lock only for the first calls, the Swaps and the recorded frames (7000 functions are hooked).
  if (state == 0 && index != g_swap_index && s.first_n >= kFirst) return;
  std::lock_guard<std::mutex> lock(g_mutex);
  if (s.first_n < kFirst) {
    Args& a = s.first[s.first_n++];
    a.r[0] = ctx.r3.u32; a.r[1] = ctx.r4.u32; a.r[2] = ctx.r5.u32;
    a.r[3] = ctx.r6.u32; a.r[4] = ctx.r7.u32; a.r[5] = ctx.r8.u32;
    a.lr = uint32_t(ctx.lr);
  }
  if (index == g_swap_index) {
    if (g_state.load() == 0) {
      if (std::chrono::steady_clock::now() - g_start >= std::chrono::seconds(after)) {
        g_device = ctx.r3.u32;
        g_state.store(1, std::memory_order_release);
        REXLOG_INFO("[d3dtrace] recording from this Swap (device {:08X})", g_device);
        return;
      }
    } else if (++g_frames >= 3) {
      g_state.store(2, std::memory_order_release);
      Write();
      return;
    }
  }
  if (g_state.load() == 1) {
    ++s.window;
    if (g_seq.size() < kMaxSequence)
      g_seq.push_back({uint16_t(index), ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32, uint32_t(ctx.lr)});
  }
}

}  // namespace ffxiii2::d3dtrace
