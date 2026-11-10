// ffxiii2-nx (2026-11-03): what zumasrevenge-nx's Direct3D layer (app/src/renut_engine/gpu, taken for FFXIII-2's XDK
// 2.0.20871 device layout) needs from its app that katamari-nx's app does not define (zumasrevenge-nx's
// src/renut_engine/zumasrevenge_renut_glue.cpp and src/me/zumasrevenge_render_mode.cpp):
//   - the game screen's width (N&B's widescreen hook, guest/widescreen.cpp, is not built): FFXIII-2 renders 1280 wide;
//   - the render scale in quarters of 1280x720 (zumasrevenge-nx's per-mode render height): 4 = the game's own 720p,
//     picture-identical. A different scale is a picture change and the user's decision (a config, later).
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <string>
#include <thread>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/system/function_dispatcher.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>
#include <rex/system/xthread.h>

#include "renut_engine/gpu/backend/video.h"
#include "renut_engine/gpu/guest/callbacks.h"

namespace renut::gpu {
uint32_t GuestScreenWidth() {
  return 1280;
}
}  // namespace renut::gpu

namespace zumasrevenge::render {
uint32_t WantedScaleQuarters() {
  return 4;
}
}  // namespace zumasrevenge::render

// ffxiii2-nx (2026-11-03): the vertical blank for Renut's renderer. The emulated GPU calls the game's graphics interrupt
// callback (D3D's handler sub_828510C8, registered through VdSetGraphicsInterruptCallback with the device) every
// vertical blank (GraphicsSystem::MarkVblank). With Renut there is no GPU plugin, and Renut's Direct3D_CreateDevice
// replaces the original that registered it, so nothing ever called it: the first Renut run (renut11, a stack dump at
// 40 s) had the main thread waiting forever in sub_8290D670 (WaitForSingleObject on the engine's frame event, the step
// before Present) after the first Swap. Here a guest host thread calls the handler with source 0 (vertical blank) and
// Renut's guest device, at 60 Hz, as the emulated GPU does.
REXCVAR_DEFINE_BOOL(ffxiii2_renut_vblank, true, "FFXIII2",
                    "Renut only: call the game's graphics interrupt handler every vertical blank (60 Hz)");

// D3D's handler takes source 0 as a vertical blank only when the display's interrupt status register (MMIO 0x7FC86544,
// bit 0) says so (sub_828510C8: `lis r11,32712; lwz r11,25924(r11); clrlwi. r11,r11,31`), then sub_828576F8 counts the
// blank, flips the pending swap and calls the game's swap callback (device +16948, FF's sub_82887E18, which sets the
// frame event). The emulated GPU's MMIO answers 1 there; with Renut nothing maps those registers (plain guest memory,
// 0), so the GPU register range is mapped here with the emulated GPU's fixed answers (GraphicsSystem::ReadRegister):
// interrupt status 1 (vertical blank), the EDRAM timing / BC control words, the line counter and the viewport size.
static std::atomic<uint32_t> g_gpu_register_reads{0};
static uint32_t ReadGpuRegister(void*, void*, uint32_t addr) {
  g_gpu_register_reads.fetch_add(1, std::memory_order_relaxed);
  switch ((addr & 0xFFFF) / 4) {
    case 0x0F00: return 0x08100748;              // RB_EDRAM_TIMING
    case 0x0F01: return 0x0000200E;              // RB_BC_CONTROL
    case 0x194C: return 720;                     // D1MODE_V_COUNTER
    case 0x1951: return 1;                       // interrupt status: vertical blank
    case 0x1961: return (1280u << 16) | 720u;    // D1MODE_VIEWPORT_SIZE
    default: return 0;
  }
}
static void WriteGpuRegister(void*, void*, uint32_t, uint32_t) {}

// Experiment (2026-11-03, off: the vertical blank was the missing part): source 1 (the command processor's interrupt, which the Swap's ring packet raises on the
// console and the emulated GPU forwards: CommandProcessor -> DispatchInterruptCallback(1, ...)) after each vertical
// blank: the engine's frame event (sub_8290D818, through the callback sub_82887E18 FF registers with sub_82858100) is
// set from the interrupt handler, and source 0 alone did not set it.
REXCVAR_DEFINE_BOOL(ffxiii2_renut_cp_interrupt, false, "FFXIII2",
                    "Renut only, experiment: also raise source 1 (command processor) after each vertical blank");

extern std::atomic<uint32_t> g_ffxiii2_last_front_buffer;
// Counters (2026-11-03, a diagnostic, logged with the vertical blanks): draw hooks, Resolve hooks, DrawStateLocked,
// ResolveStateLocked, resolves with no source surface address, with no host surface, with no destination, with no
// destination host texture.
std::atomic<uint32_t> g_ffxiii2_diag[8];
// DrawStateLocked's exits (render/draw.cpp): invalid device, unknown declaration, no command list, shader not in
// the cache, targets, skipped (true), geometry, streams, constants, pipeline/targets bind, stream bind;
// [11] the draws that reached the end.
std::atomic<uint32_t> g_ffxiii2_draw_exit[12];
// k241 (d3d/hooks/state.cpp): headers checked by ffxiii2_diag_late_headers, colour / depth slots read with other
// contents than bound, bind-time copies made.
extern std::atomic<uint32_t> g_ffxiii2_late_header[4];

// The swap interrupt. On the console the Swap's ring packet raises the command processor's interrupt with a word,
// (front buffer physical base & ~0xFFF) | presentation interval << 8, that D3D's sub_82857828 takes: it counts the
// swap (+16972), works out the vertical blank the frame may show at and calls the game's other callback (device
// +16944, FF's sub_82887D98), which sets the target the swap callback waits for. Renut's Swap writes no ring packet,
// so after each Renut Swap the word is made here (the emulated run's words: 0x08BA0200 / 0x08F38200, interval 2) and
// sub_82857828 is called on the vertical blank thread.
REXCVAR_DEFINE_BOOL(ffxiii2_renut_swap_interrupt, true, "FFXIII2",
                    "Renut only: the game's swap interrupt (sub_82857828) after each Swap");

REXCVAR_DEFINE_BOOL(ffxiii2_renut_vblank_callbacks, true, "FFXIII2",
                    "Renut only: the vertical blank thread runs the InsertCallback callbacks the renderer has reached");

namespace ffxiii2 {
namespace {
constexpr uint32_t kInterruptHandler = 0x828510C8u;
constexpr uint32_t kSwapInterrupt = 0x82857828u;
rex::system::object_ref<rex::system::XHostThread> g_vblank_thread;
std::atomic<bool> g_vblank_running{false};
}  // namespace

void StartRenutVblank() {
  if (!REXCVAR_GET(ffxiii2_renut_vblank) || g_vblank_running.exchange(true)) return;
  auto* kernel = REX_KERNEL_STATE();
  if (!kernel) return;
  const bool mapped = kernel->memory()->AddVirtualMappedRange(0x7FC80000, 0xFFFF0000, 0x0000FFFF, nullptr,
                                                              ReadGpuRegister, WriteGpuRegister);
  REXLOG_INFO("[ffxiii2] GPU registers 0x7FC80000 mapped for Renut: {}", mapped);
  g_vblank_thread = rex::system::object_ref<rex::system::XHostThread>(
      new rex::system::XHostThread(kernel, 128 * 1024, 0, [kernel]() {
        using clock = std::chrono::steady_clock;
        const auto interval = std::chrono::nanoseconds(1000000000LL / 60);
        auto next = clock::now() + interval;
        bool logged = false;
        while (g_vblank_running.load(std::memory_order_acquire)) {
          std::this_thread::sleep_until(next);
          next += interval;
          const uint32_t device = renut::gpu::Video::GuestDevice();
          if (!device) continue;
          static uint32_t ticks = 0;
          if (++ticks % 600 == 0)
          {
            uint32_t swaps_be = 0, blanks_be = 0, flips_be = 0;
            if (auto* d = kernel->memory()->TranslateVirtual<uint8_t*>(device)) {
              std::memcpy(&swaps_be, d + 16968, 4);
              std::memcpy(&blanks_be, d + 16952, 4);
              std::memcpy(&flips_be, d + 16976, 4);
            }
            REXLOG_INFO("[ffxiii2] vertical blanks {}, GPU register reads {}; device: swaps {}, blanks {}, flips {}",
                        ticks, g_gpu_register_reads.load(std::memory_order_relaxed), __builtin_bswap32(swaps_be),
                        __builtin_bswap32(blanks_be), __builtin_bswap32(flips_be));
            uint32_t c[8];
            for (int i = 0; i < 8; ++i) c[i] = g_ffxiii2_diag[i].load(std::memory_order_relaxed);
            REXLOG_INFO("[ffxiii2] draw hooks {}, resolve hooks {}, draws {}, resolves {} (no source address {}, no "
                        "host surface {}, no destination {}, no destination texture {})",
                        c[0], c[1], c[2], c[3], c[4], c[5], c[6], c[7]);
            std::string exits;
            for (int i = 0; i < 12; ++i) exits += std::to_string(g_ffxiii2_draw_exit[i].load(std::memory_order_relaxed)) + ' ';
            REXLOG_INFO("[ffxiii2] draw exits: {}", exits);
            REXLOG_INFO("[ffxiii2] headers: checked {}, read late in a colour slot {}, in the depth slot {}; copies {}",
                        g_ffxiii2_late_header[0].load(std::memory_order_relaxed),
                        g_ffxiii2_late_header[1].load(std::memory_order_relaxed),
                        g_ffxiii2_late_header[2].load(std::memory_order_relaxed),
                        g_ffxiii2_late_header[3].load(std::memory_order_relaxed));
          }
          if (!logged) {
            REXLOG_INFO("[ffxiii2] vertical blank for Renut: sub_{:08X}(0, device {:08X}) at 60 Hz",
                        kInterruptHandler, device);
            logged = true;
          }
          auto* thread = rex::system::XThread::GetCurrentThread();
          thread->SetActiveCpu(2);
          static uint32_t last_swaps = 0;
          if (auto* d = kernel->memory()->TranslateVirtual<uint8_t*>(device);
              d && REXCVAR_GET(ffxiii2_renut_swap_interrupt)) {
            uint32_t swaps_be;
            std::memcpy(&swaps_be, d + 16968, 4);
            const uint32_t swaps = __builtin_bswap32(swaps_be);
            const uint32_t front = g_ffxiii2_last_front_buffer.load(std::memory_order_acquire);
            if (swaps != last_swaps && front) {
              last_swaps = swaps;
              uint32_t fetch1_be = 0;
              if (auto* t = kernel->memory()->TranslateVirtual<uint8_t*>(front)) std::memcpy(&fetch1_be, t + 32, 4);
              const uint32_t word = (__builtin_bswap32(fetch1_be) & 0x1FFFF000u) | 0x200u;
              uint64_t swap_args[] = {word};
              kernel->function_dispatcher()->ExecuteInterrupt(thread->thread_state(), kSwapInterrupt, swap_args, 1);
            }
          }
          uint64_t args[] = {0, device};
          kernel->function_dispatcher()->ExecuteInterrupt(thread->thread_state(), kInterruptHandler, args, 2);
          // The InsertCallback callbacks the render thread has reached (callbacks.h runs them at a guest thread's next
          // D3D call). FF waits for one without another D3D call: sub_828878D8 inserts sub_828878C8 (SetEvent on the
          // engine's event, engine +2972), kicks off and the main thread then waits on it (sub_82887948): vis5/vis6
          // stopped there after 2 and 29 frames. On the console the GPU's interrupt runs it; here the vertical blank does.
          if (REXCVAR_GET(ffxiii2_renut_vblank_callbacks)) renut::gpu::RunReadyCallbacks();
          if (REXCVAR_GET(ffxiii2_renut_cp_interrupt)) {
            uint64_t cp_args[] = {1, device};
            kernel->function_dispatcher()->ExecuteInterrupt(thread->thread_state(), kInterruptHandler, cp_args, 2);
          }
        }
        return 0;
      }));
  g_vblank_thread->set_name("FFXIII2 Renut VBlank");
  g_vblank_thread->Create();
}
}  // namespace ffxiii2

// ffxiii2-nx (2026-11-04, a diagnostic for either renderer): after ffxiii2_dump_physical_s seconds, 1 MB of guest
// physical memory from ffxiii2_dump_physical (e.g. 0x0D268000, the UI atlas of the title screen) as
// logs/phys_<address>.bin: what the emulated run has there against the Renut run.
REXCVAR_DEFINE_INT32(ffxiii2_dump_physical, 0, "FFXIII2", "A diagnostic: the guest physical address to dump (0: off)");
REXCVAR_DEFINE_INT32(ffxiii2_zero_physical_size, 4096, "FFXIII2", "A test: bytes ffxiii2_zero_physical fills");
REXCVAR_DEFINE_INT32(ffxiii2_zero_physical, 0, "FFXIII2", "A test: fill 4 KB of guest physical memory at this address with 0x55 at the dump");
REXCVAR_DEFINE_BOOL(ffxiii2_touch_physical, false, "FFXIII2", "A diagnostic: rewrite the dumped range page by page after the dump");
REXCVAR_DEFINE_INT32(ffxiii2_dump_physical_s, 30, "FFXIII2", "A diagnostic: seconds before the physical dump");
namespace ffxiii2 {
void StartPhysicalDump() {
  const uint32_t address = uint32_t(REXCVAR_GET(ffxiii2_dump_physical));
  if (!address) return;
  const int32_t seconds = REXCVAR_GET(ffxiii2_dump_physical_s);
  if (seconds < 0) {
    // Watch mode: every 20 ms for 20 s, a new file each time the 1 MB changes (up to 12).
    std::thread([address]() {
      auto* kernel = REX_KERNEL_STATE();
      if (!kernel) return;
      const auto* data = kernel->memory()->TranslatePhysical<const uint8_t*>(address);
      uint64_t last = 0;
      int saved = 0;
      const auto start = std::chrono::steady_clock::now();
      while (saved < 12 && std::chrono::steady_clock::now() - start < std::chrono::seconds(20)) {
        uint64_t h = 1469598103934665603ull;
        for (uint32_t i = 0; i < 1024 * 1024; i += 8) {
          uint64_t v;
          std::memcpy(&v, data + i, 8);
          h = (h ^ v) * 1099511628211ull;
        }
        if (h != last) {
          last = h;
          const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
          char name[64];
          std::snprintf(name, sizeof(name), "logs/phys_%08X_%02d.bin", address, saved);
          if (FILE* f = std::fopen(name, "wb")) {
            std::fwrite(data, 1, 1024 * 1024, f);
            std::fclose(f);
          }
          REXLOG_INFO("[ffxiii2] physical {:08X} changed at {} ms: {} (hash {:016X})", address, ms, name, h);
          ++saved;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
      }
    }).detach();
    return;
  }
  std::thread([address, seconds]() {
    std::this_thread::sleep_for(std::chrono::seconds(seconds));
    auto* kernel = REX_KERNEL_STATE();
    if (!kernel) return;
    const auto* data = kernel->memory()->TranslatePhysical<const uint8_t*>(address);
    if (!data) return;
    char name[64];
    std::snprintf(name, sizeof(name), "logs/phys_%08X.bin", address);
    if (FILE* f = std::fopen(name, "wb")) {
      std::fwrite(data, 1, 1024 * 1024, f);
      std::fclose(f);
    }
    REXLOG_INFO("[ffxiii2] dumped 1 MB of physical memory at {:08X} to {}", address, name);
    // The same range through the guest's virtual views (0xA0000000 64 KB pages, 0xC0000000 16 MB, 0xE0000000 4 KB):
    // are they the physical view's bytes in this build?
    for (uint32_t view : {0xA0000000u, 0xC0000000u, 0xE0000000u}) {
      const uint32_t va = view | (address & 0x1FFFFFFFu);
      const auto* v = kernel->memory()->TranslateVirtual<const uint8_t*>(va);
      const bool same = v && std::memcmp(v, data, 1024 * 1024) == 0;
      REXLOG_INFO("[ffxiii2] view {:08X} (host {}): {} the physical view", va, static_cast<const void*>(v), same ? "same as" : "DIFFERENT from");
      if (v && !same) {
        char vname[64];
        std::snprintf(vname, sizeof(vname), "logs/virt_%08X.bin", va);
        if (FILE* f = std::fopen(vname, "wb")) {
          std::fwrite(v, 1, 1024 * 1024, f);
          std::fclose(f);
        }
      }
    }
    if (const uint32_t zero = uint32_t(REXCVAR_GET(ffxiii2_zero_physical))) {
      // A test: 4 KB of guest physical memory set to 0x55 (does the emulated GPU's texture read that page?).
      std::memset(kernel->memory()->TranslatePhysical<uint8_t*>(zero), 0x55, uint32_t(REXCVAR_GET(ffxiii2_zero_physical_size)));
      REXLOG_INFO("[ffxiii2] filled {:X} bytes of physical memory at {:08X} with 0x55", uint32_t(REXCVAR_GET(ffxiii2_zero_physical_size)), zero);
    }
    // ffxiii2_touch_physical: after the dump, every 4 KB page of the range is rewritten with its own bytes (a write
    // through the host, which trips the emulated GPU's page watches: its cached copies reload from memory).
    if (REXCVAR_GET(ffxiii2_touch_physical)) {
      auto* bytes = kernel->memory()->TranslatePhysical<volatile uint8_t*>(address);
      for (uint32_t offset = 0; offset < 1024 * 1024; offset += 4096) bytes[offset] = bytes[offset];
      REXLOG_INFO("[ffxiii2] touched 1 MB of physical memory at {:08X}", address);
    }
  }).detach();
}
}  // namespace ffxiii2
