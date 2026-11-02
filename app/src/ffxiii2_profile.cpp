// Sampling CPU profiler for test runs (Windows only; written for this port).
//
// With ffxiii2_profile_from_s > 0: from that second after start, for ffxiii2_profile_duration_s seconds, every thread
// of the process (except this one) is suspended every ~2 ms, its instruction pointer and the next return addresses
// (x64 unwind data, RtlVirtualUnwind) are recorded, and the thread resumed. At the end logs/profile_<from>s.txt gets:
//   - the CPU time each thread used in the window (GetThreadTimes), busiest first;
//   - per busy thread, the hottest functions (top frame) and the hottest 4-frame stacks,
// named with DbgHelp from the PDB. Recompiled guest functions are named sub_XXXXXXXX after their guest address.
#include "ffxiii2_profile.h"

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>

REXCVAR_DEFINE_INT32(ffxiii2_profile_from_s, 0, "FFXIII2", "CPU profile: start this many seconds after launch (0 = off)");
REXCVAR_DEFINE_INT32(ffxiii2_profile_duration_s, 20, "FFXIII2", "CPU profile: length of the sampled window in seconds");

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dbghelp.h>
#include <tlhelp32.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#pragma comment(lib, "dbghelp.lib")

namespace ffxiii2::profile {
namespace {
constexpr int kDepth = 4;
struct Stack { uint64_t pc[kDepth] = {}; bool operator<(const Stack& o) const { return std::lexicographical_compare(pc, pc + kDepth, o.pc, o.pc + kDepth); } };
struct ThreadData {
  HANDLE h = nullptr;
  std::wstring name;
  uint64_t cpu_start = 0, cpu_end = 0;
  uint64_t samples = 0;
  std::unordered_map<uint64_t, uint64_t> top;
  std::map<Stack, uint64_t> stacks;
};

uint64_t CpuTime(HANDLE h) {
  FILETIME c, e, k, u;
  if (!GetThreadTimes(h, &c, &e, &k, &u)) return 0;
  return ((uint64_t(k.dwHighDateTime) << 32 | k.dwLowDateTime) + (uint64_t(u.dwHighDateTime) << 32 | u.dwLowDateTime)) / 10000;  // ms
}

void Walk(HANDLE h, Stack& s) {
  CONTEXT ctx{};
  ctx.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
  if (!GetThreadContext(h, &ctx)) return;
  for (int i = 0; i < kDepth; ++i) {
    s.pc[i] = ctx.Rip;
    DWORD64 image_base = 0;
    PRUNTIME_FUNCTION fn = RtlLookupFunctionEntry(ctx.Rip, &image_base, nullptr);
    if (!fn) {  // leaf function: the return address is at [rsp]
      if (!ctx.Rsp) break;
      ctx.Rip = *reinterpret_cast<DWORD64*>(ctx.Rsp);
      ctx.Rsp += 8;
    } else {
      void* handler_data = nullptr; DWORD64 frame = 0;
      RtlVirtualUnwind(UNW_FLAG_NHANDLER, image_base, ctx.Rip, fn, &ctx, &handler_data, &frame, nullptr);
    }
    if (!ctx.Rip) break;
  }
}

std::string Name(HANDLE proc, uint64_t pc) {
  if (!pc) return "-";
  alignas(SYMBOL_INFO) char buf[sizeof(SYMBOL_INFO) + 256];
  auto* sym = reinterpret_cast<SYMBOL_INFO*>(buf);
  sym->SizeOfStruct = sizeof(SYMBOL_INFO);
  sym->MaxNameLen = 255;
  DWORD64 disp = 0;
  char mod[MAX_PATH] = "?";
  HMODULE hm = nullptr;
  if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         reinterpret_cast<LPCSTR>(pc), &hm) && hm) {
    char path[MAX_PATH];
    if (GetModuleFileNameA(hm, path, MAX_PATH)) { const char* b = strrchr(path, '\\'); snprintf(mod, sizeof(mod), "%s", b ? b + 1 : path); }
  }
  if (SymFromAddr(proc, pc, &disp, sym)) return std::string(mod) + "!" + sym->Name + "+0x" + std::to_string(disp);
  char t[64];
  snprintf(t, sizeof(t), "+0x%llx", (unsigned long long)(pc - reinterpret_cast<uint64_t>(hm)));
  return std::string(mod) + t;
}

void Run(int from_s, int dur_s) {
  std::this_thread::sleep_for(std::chrono::seconds(from_s));
  REXLOG_INFO("[profile] sampling all threads for {} s", dur_s);
  const DWORD pid = GetCurrentProcessId(), self = GetCurrentThreadId();
  std::map<DWORD, ThreadData> threads;
  auto refresh = [&] {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 te{sizeof(te)};
    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
      if (te.th32OwnerProcessID != pid || te.th32ThreadID == self || threads.count(te.th32ThreadID)) continue;
      HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID);
      if (!h) continue;
      ThreadData& d = threads[te.th32ThreadID];
      d.h = h;
      d.cpu_start = CpuTime(h);
      // GetThreadDescription (Windows 10 1607+) looked up at run time: not declared for every SDK target version.
      using GetDescFn = HRESULT(WINAPI*)(HANDLE, PWSTR*);
      static const auto get_desc = reinterpret_cast<GetDescFn>(
          reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetThreadDescription")));
      PWSTR desc = nullptr;
      if (get_desc && SUCCEEDED(get_desc(h, &desc)) && desc) { d.name = desc; LocalFree(desc); }
    }
    CloseHandle(snap);
  };
  refresh();
  const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(dur_s);
  int tick = 0;
  while (std::chrono::steady_clock::now() < end) {
    if (++tick % 500 == 0) refresh();  // threads created during the window
    for (auto& [tid, d] : threads) {
      if (SuspendThread(d.h) == DWORD(-1)) continue;
      Stack s;
      Walk(d.h, s);
      ResumeThread(d.h);
      if (!s.pc[0]) continue;
      ++d.samples; ++d.top[s.pc[0]]; ++d.stacks[s];
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  for (auto& [tid, d] : threads) d.cpu_end = CpuTime(d.h);

  HANDLE proc = GetCurrentProcess();
  SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
  SymInitialize(proc, nullptr, TRUE);
  const auto path = rex::filesystem::GetExecutableFolder() / "logs" / ("profile_" + std::to_string(from_s) + "s.txt");
  FILE* f = _wfopen(path.c_str(), L"w");
  if (!f) return;
  std::vector<std::pair<uint64_t, DWORD>> busy;
  for (auto& [tid, d] : threads) busy.push_back({d.cpu_end - d.cpu_start, tid});
  std::sort(busy.rbegin(), busy.rend());
  fprintf(f, "CPU profile %d..%d s after start, ~2 ms sampling\n\nCPU ms in the window per thread:\n", from_s, from_s + dur_s);
  for (auto& [ms, tid] : busy) {
    std::string n(threads[tid].name.begin(), threads[tid].name.end());
    fprintf(f, "  %6llu ms  tid %5lu  %s\n", (unsigned long long)ms, tid, n.c_str());
  }
  for (auto& [ms, tid] : busy) {
    if (ms < uint64_t(dur_s) * 100) break;  // only threads busier than 10 %
    ThreadData& d = threads[tid];
    std::string n(d.name.begin(), d.name.end());
    fprintf(f, "\n== tid %lu %s: %llu ms CPU, %llu samples\n  hottest functions:\n", tid, n.c_str(), (unsigned long long)ms, (unsigned long long)d.samples);
    std::map<std::string, uint64_t> by_fn;  // fold offsets into functions
    for (auto& [pc, c] : d.top) { std::string s = Name(proc, pc); by_fn[s.substr(0, s.rfind("+0x"))] += c; }
    std::vector<std::pair<uint64_t, std::string>> fv;
    for (auto& [s, c] : by_fn) fv.push_back({c, s});
    std::sort(fv.rbegin(), fv.rend());
    for (size_t i = 0; i < fv.size() && i < 15; ++i) fprintf(f, "    %5.1f %%  %s\n", 100.0 * fv[i].first / d.samples, fv[i].second.c_str());
    std::vector<std::pair<uint64_t, Stack>> sv;
    for (auto& [s, c] : d.stacks) sv.push_back({c, s});
    std::sort(sv.begin(), sv.end(), [](auto& a, auto& b) { return a.first > b.first; });
    fprintf(f, "  hottest stacks:\n");
    for (size_t i = 0; i < sv.size() && i < 8; ++i) {
      fprintf(f, "    %5.1f %%\n", 100.0 * sv[i].first / d.samples);
      for (int k = 0; k < kDepth; ++k) fprintf(f, "        %s\n", Name(proc, sv[i].second.pc[k]).c_str());
    }
  }
  fclose(f);
  for (auto& [tid, d] : threads) CloseHandle(d.h);
  REXLOG_INFO("[profile] written to {}", path.string());
}
}  // namespace

void Start() {
  const int from = REXCVAR_GET(ffxiii2_profile_from_s), dur = REXCVAR_GET(ffxiii2_profile_duration_s);
  if (from <= 0 || dur <= 0) return;
  std::thread(Run, from, dur).detach();
}
}  // namespace ffxiii2::profile

#else
namespace ffxiii2::profile {
void Start() {}
}
#endif
