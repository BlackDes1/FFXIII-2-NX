// FFXIII-2-Native-Switch P7 (2026-11-10, the user's option A: run FF's render side concurrently with the next update,
// "but without flicker, jitter or crashes"). PC-only diagnostic, off by default: which guest memory pages FF's render side
// touches and which the following update touches, page by page, so the overlap's data conflicts are measured before
// anything runs concurrently (prototypes/P7_render_overlap/).
//
// ffns_race_probe_at = a swap count (0 = off): from then on, ffns_race_probe_count probes, ffns_race_probe_every swaps
// apart. A probe:
//   phase A: from the main thread's first render-side job of a frame (shadow 829158B0, 82AF7498 or the render job
//            829151C0) to the end of the render job;
//   phase B: from there to the next frame's first render-side job.
// At each phase start every committed guest page (all views of the guest window) is set to no access. A fault of a
// tracked thread (phase A: the main thread; phase B: the main thread and FF's three job workers, start 82886E58) is
// recorded (page, read or write, the job running on that thread) and the page gets read-only (after a read) or its
// own protection (after a write): first read and first write of every page per phase. Faults of other threads (audio,
// renderer, streaming: they already run concurrently with the render side today) pass for one instruction (trap flag)
// and the page is protected again; a page they fault on more than 64 times in a phase is left open and listed as lost.
// Pages whose own protection is read-only (the GPU write watch): a tracked write is recorded and handed to the
// runtime's handler as it is. After phase B every page gets its own protection back and ffns_race_probe_<n>.txt is
// written next to the program. Slow (a fault per first touch): diagnostic runs only.

#include <rex/cvar.h>

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>
#include <rex/system/xthread.h>

REXCVAR_DEFINE_INT32(ffns_race_probe_at, 0, "FFNS/Diag",
                     "PC diagnostic: from this swap count, record the guest pages FF's render side and the following "
                     "update touch (ffns_race_probe_<n>.txt; 0 = off; slow)");
REXCVAR_DEFINE_INT32(ffns_race_probe_count, 3, "FFNS/Diag", "PC diagnostic: number of ffns_race_probe_at probes");
REXCVAR_DEFINE_INT32(ffns_race_probe_every, 37, "FFNS/Diag", "PC diagnostic: swaps between ffns_race_probe_at probes");

extern std::atomic<uint32_t> g_ffxiii2_swap_calls;  // hooks/device.cpp

namespace {
constexpr uint32_t kShadowJob = 0x829158B0u, kPreJob = 0x82AF7498u, kRenderJob = 0x829151C0u;
constexpr uint32_t kWorkerStart = 0x82886E58u;
constexpr uintptr_t kPage = 4096;
enum : int { kIdle = 0, kPhaseA = 1, kPhaseB = 2, kDone = 3 };

struct Region {
  uintptr_t base;
  size_t size;
  DWORD protect;  // its own protection (PAGE_READWRITE or PAGE_READONLY)
};
struct PageInfo {
  uint8_t flags = 0;     // 1 read, 2 write, 4 lost (other threads), 8 handed to the runtime (write watch)
  uint8_t rcls = 0, wcls = 0;  // 1 main, 2 worker
  uint32_t rfn = 0, wfn = 0;   // the job running on that thread (0 = none)
  uint32_t va = 0;             // the guest virtual address seen first
  uint16_t other_faults = 0;
};

std::atomic<int> g_state{kIdle};
std::atomic<bool> g_probe_ran{false};  // set at the first probe: from then on every fault takes the slow check
std::mutex g_mutex;
std::vector<Region> g_regions;  // sorted by base
std::unordered_map<uintptr_t, DWORD> g_cur;      // host page -> protection set during the phase (default no access)
std::unordered_map<uint64_t, PageInfo> g_pages[2];  // canonical page key -> info, per phase
std::vector<std::string> g_threads;
PVOID g_veh = nullptr;
int g_probe = 0;
uint32_t g_next_swap = 0;
uint32_t g_probe_swap = 0;
uint64_t g_faults[2] = {0, 0}, g_other_faults[2] = {0, 0};
std::string g_header;
// Exact mode (every probe after the first): only the pages the first probe found touched by both sides stay protected
// and every access to them faults (trap flag after it, the page closed again): exact addresses, 4-byte granules.
std::unordered_map<uint64_t, std::vector<uintptr_t>> g_key_hosts;  // probe 0: canonical page -> host pages (views)
std::unordered_map<uintptr_t, uint64_t> g_conflict;                // host page -> canonical key (exact mode)
struct Exact {
  uint8_t flags = 0;  // 1 read, 2 write
  uint8_t rcls = 0, wcls = 0;
  uint32_t rfn = 0, wfn = 0;
};
std::unordered_map<uint64_t, Exact> g_exact[2];  // (key << 12 | offset & ~3) -> access
bool ExactMode() { return g_probe > 0 && !g_conflict.empty(); }

thread_local int t_class = -1;       // 1 main, 2 job worker, 0 other
thread_local uint32_t t_fn = 0;      // the job running on this thread
thread_local std::vector<uintptr_t> t_pending;  // pages opened for one instruction of an untracked thread

int ThreadClass() {
  if (t_class >= 0)
    return t_class;
  auto *thread = rex::system::XThread::GetCurrentThread();
  int cls = 0;
  if (thread && thread->main_thread())
    cls = 1;
  else if (thread && thread->creation_params()->start_address == kWorkerStart)
    cls = 2;
  t_class = cls;
  if (thread && cls != 0) {
    std::lock_guard lock(g_mutex);
    char line[160];
    std::snprintf(line, sizeof(line), "# thread %u class %d pcr %08X tls %08X", thread->thread_id(), cls,
                  thread->pcr_ptr(), thread->tls_ptr());
    g_threads.push_back(line);
  }
  return cls;
}

const Region *FindRegion(uintptr_t page) {
  auto it = std::upper_bound(g_regions.begin(), g_regions.end(), page,
                             [](uintptr_t p, const Region &r) { return p < r.base; });
  if (it == g_regions.begin())
    return nullptr;
  --it;
  return page < it->base + it->size ? &*it : nullptr;
}

DWORD CurrentProtect(uintptr_t page, const Region &region) {
  const int state = g_state.load(std::memory_order_relaxed);
  if (state != kPhaseA && state != kPhaseB)
    return region.protect;
  if (ExactMode())
    return g_conflict.count(page) ? PAGE_NOACCESS : region.protect;
  auto it = g_cur.find(page);
  return it == g_cur.end() ? PAGE_NOACCESS : it->second;
}

uintptr_t g_vlo = 0, g_vhi = 0, g_plo = 0, g_phi = 0;  // the virtual window and the physical view

void ScanRange(uintptr_t lo_addr, uintptr_t hi_addr) {
  uint8_t *lo = reinterpret_cast<uint8_t *>(lo_addr);
  uint8_t *hi = reinterpret_cast<uint8_t *>(hi_addr);
  MEMORY_BASIC_INFORMATION mbi;
  for (uint8_t *p = lo; p < hi && VirtualQuery(p, &mbi, sizeof(mbi)) == sizeof(mbi);
       p = static_cast<uint8_t *>(mbi.BaseAddress) + mbi.RegionSize) {
    if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_GUARD | PAGE_NOCACHE)))
      continue;
    const DWORD base_protect = mbi.Protect & 0xFF;
    if (base_protect != PAGE_READWRITE && base_protect != PAGE_READONLY)
      continue;
    uintptr_t b = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
    uintptr_t e = b + mbi.RegionSize;
    b = std::max(b, lo_addr);
    e = std::min(e, hi_addr);
    if (b < e)
      g_regions.push_back({b, e - b, base_protect});
  }
}

void Snapshot() {
  g_regions.clear();
  rex::memory::Memory *memory = REX_KERNEL_MEMORY();
  g_vlo = reinterpret_cast<uintptr_t>(memory->virtual_membase());
  g_plo = reinterpret_cast<uintptr_t>(memory->physical_membase());
  // the 4 GB guest window (plus the small host offset of the 0xE0000000 heap), ending at the physical view if that
  // follows it; the physical view: 512 MB (the renderer's GuestPhysicalPtr reads go through it)
  g_vhi = g_vlo + 0x100010000ull;
  if (g_plo > g_vlo && g_plo < g_vhi)
    g_vhi = g_plo;
  g_phi = g_plo + 0x20000000ull;
  ScanRange(g_vlo, g_vhi);
  ScanRange(g_plo, g_phi);
  std::sort(g_regions.begin(), g_regions.end(), [](const Region &a, const Region &b) { return a.base < b.base; });
}

void ProtectAll(bool open) {
  for (const Region &r : g_regions) {
    DWORD old;
    VirtualProtect(reinterpret_cast<void *>(r.base), r.size, open ? r.protect : PAGE_NOACCESS, &old);
  }
}

uint64_t PageKey(uintptr_t page, uint32_t *va_out) {
  if (page >= g_plo && page < g_phi) {  // the physical view
    const uint32_t phys = uint32_t(page - g_plo);
    *va_out = phys;
    return (uint64_t(1) << 32) | phys;
  }
  rex::memory::Memory *memory = REX_KERNEL_MEMORY();
  const uint32_t va = memory->HostToGuestVirtual(reinterpret_cast<void *>(page));
  *va_out = va;
  const uint32_t phys = va >= 0xA0000000u ? memory->GetPhysicalAddress(va) : UINT32_MAX;
  if (phys != UINT32_MAX)
    return (uint64_t(1) << 32) | (phys & ~uint32_t(kPage - 1));
  return va & ~uint32_t(kPage - 1);
}

LONG CALLBACK Veh(EXCEPTION_POINTERS *info) {
  EXCEPTION_RECORD *rec = info->ExceptionRecord;
  if (rec->ExceptionCode == EXCEPTION_SINGLE_STEP) {
    if (t_pending.empty())
      return EXCEPTION_CONTINUE_SEARCH;
    std::lock_guard lock(g_mutex);
    for (uintptr_t page : t_pending) {
      if (const Region *region = FindRegion(page)) {
        DWORD old;
        VirtualProtect(reinterpret_cast<void *>(page), kPage, CurrentProtect(page, *region), &old);
      }
    }
    t_pending.clear();
    return EXCEPTION_CONTINUE_EXECUTION;
  }
  if (rec->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || rec->NumberParameters < 2)
    return EXCEPTION_CONTINUE_SEARCH;
  if (g_state.load(std::memory_order_acquire) == kIdle && !g_probe_ran.load(std::memory_order_relaxed))
    return EXCEPTION_CONTINUE_SEARCH;
  const bool write = rec->ExceptionInformation[0] == 1;
  const uintptr_t page = uintptr_t(rec->ExceptionInformation[1]) & ~(kPage - 1);
  const int cls = ThreadClass();
  std::lock_guard lock(g_mutex);
  const int state = g_state.load(std::memory_order_acquire);
  if (state != kPhaseA && state != kPhaseB) {
    // a fault that raced with the end of a phase: the protection is back; retry if it allows the access now
    MEMORY_BASIC_INFORMATION mbi;
    if (FindRegion(page) && VirtualQuery(reinterpret_cast<void *>(page), &mbi, sizeof(mbi)) == sizeof(mbi)) {
      const DWORD now = mbi.Protect & 0xFF;
      if (now == PAGE_READWRITE || (now == PAGE_READONLY && !write))
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
  }
  const Region *region = FindRegion(page);
  if (!region)
    return EXCEPTION_CONTINUE_SEARCH;
  if (ExactMode()) {
    auto it = g_conflict.find(page);
    if (it == g_conflict.end())
      return EXCEPTION_CONTINUE_SEARCH;
    const int ph = state == kPhaseA ? 0 : 1;
    const bool tr = ph == 0 ? cls == 1 : (cls == 1 || cls == 2);
    if (tr) {
      ++g_faults[ph];
      const uintptr_t addr = uintptr_t(rec->ExceptionInformation[1]);
      Exact &e = g_exact[ph][(it->second << 12) | ((addr & (kPage - 1)) & ~uintptr_t(3))];
      if (write && !(e.flags & 2)) {
        e.flags |= 2;
        e.wcls = uint8_t(cls);
        e.wfn = t_fn;
      } else if (!write && !(e.flags & 1)) {
        e.flags |= 1;
        e.rcls = uint8_t(cls);
        e.rfn = t_fn;
      }
    } else {
      ++g_other_faults[ph];
    }
    if (write && region->protect == PAGE_READONLY)
      return EXCEPTION_CONTINUE_SEARCH;  // the runtime's write watch handles it (the page stays as it leaves it)
    DWORD old2;
    VirtualProtect(reinterpret_cast<void *>(page), kPage, region->protect, &old2);
    t_pending.push_back(page);
    info->ContextRecord->EFlags |= 0x100;
    return EXCEPTION_CONTINUE_EXECUTION;
  }
  const int phase = state == kPhaseA ? 0 : 1;
  const bool tracked = phase == 0 ? cls == 1 : (cls == 1 || cls == 2);
  uint32_t va = 0;
  const uint64_t key = PageKey(page, &va);
  PageInfo &p = g_pages[phase][key];
  if (!p.va)
    p.va = va;
  {
    auto &hosts = g_key_hosts[key];
    if (std::find(hosts.begin(), hosts.end(), page) == hosts.end())
      hosts.push_back(page);
  }
  DWORD old;
  if (!tracked) {
    ++g_other_faults[phase];
    if (++p.other_faults > 64) {
      p.flags |= 4;
      g_cur[page] = region->protect;
      VirtualProtect(reinterpret_cast<void *>(page), kPage, region->protect, &old);
      return region->protect == PAGE_READONLY && write ? EXCEPTION_CONTINUE_SEARCH : EXCEPTION_CONTINUE_EXECUTION;
    }
    if (write && region->protect == PAGE_READONLY) {  // the runtime's write watch: let its handler see the write
      VirtualProtect(reinterpret_cast<void *>(page), kPage, PAGE_READONLY, &old);
      g_cur[page] = PAGE_READONLY;
      return EXCEPTION_CONTINUE_SEARCH;
    }
    VirtualProtect(reinterpret_cast<void *>(page), kPage, region->protect, &old);
    t_pending.push_back(page);
    info->ContextRecord->EFlags |= 0x100;  // trap after one instruction: Veh's single-step branch protects it again
    return EXCEPTION_CONTINUE_EXECUTION;
  }
  ++g_faults[phase];
  if (write) {
    if (!(p.flags & 2)) {
      p.flags |= 2;
      p.wcls = uint8_t(cls);
      p.wfn = t_fn;
    }
    if (region->protect == PAGE_READONLY) {  // write watch: record, then let the runtime handle it
      p.flags |= 8;
      VirtualProtect(reinterpret_cast<void *>(page), kPage, PAGE_READONLY, &old);
      g_cur[page] = PAGE_READONLY;
      return EXCEPTION_CONTINUE_SEARCH;
    }
    g_cur[page] = region->protect;
    VirtualProtect(reinterpret_cast<void *>(page), kPage, region->protect, &old);
  } else {
    if (!(p.flags & 1)) {
      p.flags |= 1;
      p.rcls = uint8_t(cls);
      p.rfn = t_fn;
    }
    g_cur[page] = PAGE_READONLY;
    VirtualProtect(reinterpret_cast<void *>(page), kPage, PAGE_READONLY, &old);
  }
  return EXCEPTION_CONTINUE_EXECUTION;
}

void StartPhase(int state) {
  // with g_mutex held
  g_probe_ran.store(true, std::memory_order_relaxed);
  g_cur.clear();
  if (state == kPhaseA)
    Snapshot();
  g_state.store(state, std::memory_order_release);
  if (ExactMode()) {
    for (const auto &[page, key] : g_conflict) {
      DWORD old;
      VirtualProtect(reinterpret_cast<void *>(page), kPage, PAGE_NOACCESS, &old);
    }
  } else {
    ProtectAll(false);
  }
}

void Finish() {
  // with g_mutex held
  // the protections first, then the state: a fault in between still finds the phase's state and is handled
  if (ExactMode()) {
    for (const auto &[page, key] : g_conflict)
      if (const Region *region = FindRegion(page)) {
        DWORD old;
        VirtualProtect(reinterpret_cast<void *>(page), kPage, region->protect, &old);
      }
  } else {
    ProtectAll(true);
  }
  g_state.store(kIdle, std::memory_order_release);
  g_cur.clear();
  char name[64];
  std::snprintf(name, sizeof(name), "ffns_race_probe_%d.txt", g_probe);
  if (FILE *f = std::fopen(name, "w")) {
    std::fprintf(f, "%s", g_header.c_str());
    std::fprintf(f, "# faults: A tracked %llu other %llu; B tracked %llu other %llu; regions %zu\n",
                 (unsigned long long)g_faults[0], (unsigned long long)g_other_faults[0],
                 (unsigned long long)g_faults[1], (unsigned long long)g_other_faults[1], g_regions.size());
    for (const std::string &t : g_threads)
      std::fprintf(f, "%s\n", t.c_str());
    std::fprintf(f, "# phase key(1xxxxxxxx = physical) va flags(1 r 2 w 4 lost 8 watch) rcls rfn wcls wfn\n");
    for (int phase = 0; phase < 2; ++phase)
      for (const auto &[key, p] : g_pages[phase])
        std::fprintf(f, "%c %09llX %08X %X %u %08X %u %08X\n", phase ? 'B' : 'A', (unsigned long long)key, p.va,
                     p.flags, p.rcls, p.rfn, p.wcls, p.wfn);
    if (ExactMode()) {
      std::fprintf(f, "# exact mode on %zu host pages: phase key*4096+offset flags rcls rfn wcls wfn\n",
                   g_conflict.size());
      for (int phase = 0; phase < 2; ++phase)
        for (const auto &[k, e] : g_exact[phase])
          std::fprintf(f, "%c %012llX %X %u %08X %u %08X\n", phase ? 'b' : 'a', (unsigned long long)k, e.flags,
                       e.rcls, e.rfn, e.wcls, e.wfn);
    }
    std::fclose(f);
  }
  if (g_probe == 0) {
    // the pages both sides touched: render side read/wrote, update wrote; or render side wrote, update read/wrote
    for (const auto &[key, a] : g_pages[0]) {
      auto it = g_pages[1].find(key);
      if (it == g_pages[1].end())
        continue;
      const PageInfo &b = it->second;
      const bool c1 = (a.flags & 3) && (b.flags & 2);
      const bool c2 = (a.flags & 2) && (b.flags & 3);
      if (!c1 && !c2)
        continue;
      for (uintptr_t host : g_key_hosts[key])
        g_conflict[host] = key;
    }
  }
  g_exact[0].clear();
  g_exact[1].clear();
  g_pages[0].clear();
  g_pages[1].clear();
  g_faults[0] = g_faults[1] = g_other_faults[0] = g_other_faults[1] = 0;
  ++g_probe;
}
}  // namespace

extern "C" bool FfnsRaceProbeOn() { return REXCVAR_GET(ffns_race_probe_at) > 0; }

// From the job runner hook (hooks/draw.cpp, sub_8290FC60): begin = 1 before a job, 0 after it.
extern "C" void FfnsRaceProbeJob(uint32_t fn, int begin, uint32_t r1) {
  static const int32_t at = REXCVAR_GET(ffns_race_probe_at);
  static const int32_t count = REXCVAR_GET(ffns_race_probe_count);
  static const int32_t every = std::max<int32_t>(1, REXCVAR_GET(ffns_race_probe_every));
  if (at <= 0)
    return;
  const int cls = ThreadClass();
  static thread_local std::vector<uint32_t> stack;
  if (begin) {
    stack.push_back(t_fn);
    t_fn = fn;
  } else {
    t_fn = stack.empty() ? 0 : stack.back();
    if (!stack.empty())
      stack.pop_back();
  }
  if (cls != 1)
    return;
  const bool render_side = fn == kShadowJob || fn == kPreJob || fn == kRenderJob;
  const int state = g_state.load(std::memory_order_acquire);
  const uint32_t swaps = g_ffxiii2_swap_calls.load(std::memory_order_relaxed);
  if (state == kIdle && begin && render_side && g_probe < count) {
    if (g_next_swap == 0)
      g_next_swap = uint32_t(at);
    if (swaps < g_next_swap)
      return;
    std::lock_guard lock(g_mutex);
    if (!g_veh)
      g_veh = AddVectoredExceptionHandler(1, Veh);
    auto *thread = rex::system::XThread::GetCurrentThread();
    char line[256];
    std::snprintf(line, sizeof(line), "# ffns race probe %d at swap %u, first job %08X; main r1 %08X pcr %08X tls %08X\n",
                  g_probe, swaps, fn, r1, thread ? thread->pcr_ptr() : 0, thread ? thread->tls_ptr() : 0);
    g_header = line;
    g_probe_swap = swaps;
    StartPhase(kPhaseA);
    return;
  }
  if (state == kPhaseA && !begin && fn == kRenderJob) {
    std::lock_guard lock(g_mutex);
    StartPhase(kPhaseB);
    return;
  }
  if (state == kPhaseB && begin && render_side) {
    std::lock_guard lock(g_mutex);
    Finish();
    g_next_swap = swaps + uint32_t(every);
  }
}

#else
extern "C" bool FfnsRaceProbeOn() { return false; }
extern "C" void FfnsRaceProbeJob(uint32_t, int, uint32_t) {}
#endif
