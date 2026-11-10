// The game's graphics interrupt handler, sub_828510C8 (registered with VdSetGraphicsInterruptCallback; the boot log
// shows "SetInterruptCallback(828510C8, 40002180)"). Same address in the USA TU 0.0.1.13 build.
//
// Problem (stage 3b boot test, docs/stage3-boot.md): after the first button press on "Press any button to continue"
// the screen stays black and one CPU core is busy for minutes. ReXGlue's FunctionDispatcher::ExecuteInterrupt holds
// the runtime's global mutex for the whole handler. The handler takes kernel spinlocks; a game thread that holds one
// of those spinlocks and then needs the global mutex (any kernel call, any guest "interrupts off" section) waits
// forever, while the handler spins on the spinlock holding the mutex: no frame is presented again. The same freeze
// and cause are documented by leftisthominid/ffxiii-2-port for the USA build (written independently here).
//
// On the console an interrupt is masked only on the hardware thread that disabled it, so the handler never excluded
// the other threads' "interrupts off" sections. Default here: the handler runs WITHOUT the global mutex (released for
// the call, taken back after). FFXIII2_IRQ_LOCK=global keeps ReXGlue's behaviour, for comparison.
//
// Diagnostics, every 5 s while interrupts arrive: "[irq] N calls, longest X ms; inside the handler for Y ms" - a handler
// that is still inside after seconds is the freeze.
// ffxiii2-nx on the katamari-nx base (2026-11-03): from the earlier tree's app/src/ffxiii2_irq.cpp (stage 3, where it was
// measured: 2 of 6 PC runs froze without it, 0 of 6 with it), without the frame counter (VdSwapCount is not in this SDK).
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <thread>

#include <rex/hook.h>
#include <rex/logging.h>
#include <fmt/format.h>
#include <rex/thread/mutex.h>

namespace {
using Clock = std::chrono::steady_clock;
std::atomic<uint64_t> g_calls{0}, g_longest_us{0};
std::atomic<int64_t> g_inside_since_us{0};  // 0 = not inside

int64_t NowUs() { return std::chrono::duration_cast<std::chrono::microseconds>(Clock::now().time_since_epoch()).count(); }

bool GlobalMode() {
  static const bool global = [] {
    const char* v = std::getenv("FFXIII2_IRQ_LOCK");
    const bool g = v && (v[0] == 'g' || v[0] == 'G');
    REXLOG_INFO("[irq] graphics interrupt handler sub_828510C8 runs {}", g ? "under the global mutex (ReXGlue default)"
                                                                          : "without the global mutex");
    return g;
  }();
  return global;
}

void StartReporter() {
  static std::atomic<bool> started{false};
  if (started.exchange(true)) return;
  // Persistent thread, never detached: on Horizon std::thread::detach() threw std::system_error and std::terminate
  // ended the game at start (console run 2026-11-03, rex_crash.log: __cxa_throw <- std::thread::detach <-
  // ReXApp::ConstructRuntime -> OnPostSetup; nfsmw-nx docs/platform-notes.md "Threads"). The object is leaked on purpose.
  new std::thread([] {
    uint64_t last = 0;
    for (;;) {
      std::this_thread::sleep_for(std::chrono::seconds(5));
      const uint64_t calls = g_calls.load(), longest = g_longest_us.exchange(0);
      const int64_t since = g_inside_since_us.load();
      const int64_t inside_ms = since ? (NowUs() - since) / 1000 : 0;
      REXLOG_INFO("[irq] {} calls in 5 s, longest {:.2f} ms{}", calls - last, longest / 1000.0,
                  inside_ms > 100 ? fmt::format("; INSIDE the handler for {} ms", inside_ms) : std::string());
      last = calls;
    }
  });
}
}  // namespace

REX_EXTERN(__imp__sub_828510C8);
REX_HOOK_RAW(sub_828510C8) {
  StartReporter();
  const int64_t t0 = NowUs();
  g_inside_since_us.store(t0);
  if (GlobalMode()) {
    __imp__sub_828510C8(ctx, base);
  } else {
    auto& global = rex::thread::global_critical_region::mutex();
    global.unlock();  // the level FunctionDispatcher::ExecuteInterrupt took for this call
    __imp__sub_828510C8(ctx, base);
    global.lock();
  }
  const uint64_t dt = uint64_t(NowUs() - t0);
  g_inside_since_us.store(0);
  g_calls.fetch_add(1, std::memory_order_relaxed);
  for (uint64_t cur = g_longest_us.load(); dt > cur && !g_longest_us.compare_exchange_weak(cur, dt);) {}
}
