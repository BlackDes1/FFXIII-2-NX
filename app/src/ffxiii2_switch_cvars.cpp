// Keys the SDK's Switch profiler (sdk/src/ui/switch_perf.cpp) reads by name from the title: defined here so the
// toml can set them. Harmless on the PC (nothing reads them there). Same keys and meaning as sa2-nx
// (app/src/cpu/sa2_enhancements.cpp).
#include <cstdlib>
#include <string>

#include <rex/cvar.h>
#include <rex/logging.h>

#include "ffxiii2_switch_cvars.h"

// The profiler's 10-second report (logs/rex/rex_profile.log): frame rate, each thread's CPU time, priority and core,
// clocks, and per-second counts of the libnx calls that cost IPC. On in test builds (ffxiii2_switch_defaults.toml).
REXCVAR_DEFINE_BOOL(switch_profile_report, true, "FFXIII-2 Switch",
                    "Switch: the profiler writes its 10-second report to logs/rex/rex_profile.log (false = no file)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
// Stack samples every 1 ms of one busy thread (pc, lr, frame chain) into the same report; translate with addr2line
// on the build's ELF (tools/switch_profile_report.py of sa2-nx).
REXCVAR_DEFINE_BOOL(switch_profile_stacks, false, "FFXIII-2 Switch",
                    "Switch: CPU stack sampling every 1 ms (rex_profile.log), as logs/rex/stacks_profile.flag does")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// Mesa / NVK options are environment variables the driver reads with getenv when the Vulkan instance and the device
// are created (nfsmw-nx docs/platform-notes.md, "NVK on Horizon"). A homebrew program has no environment, so they
// are set from this key before the graphics system starts: "NAME=value;NAME=value". Known names in the linked Mesa
// 26.2.4 (strings of libvulkan.a): NVK_SWITCH_CPU_WRITE_MEM_UNCACHED, NVK_SWITCH_MAPPED_COMPLETION,
// NVK_SWITCH_NO_UBO_CBUF, NVK_SWITCH_PERF_LOG, NOUVEAU_HORIZON_BO_CACHE_MB, MESA_SHADER_CACHE_DISABLE, NVK_DEBUG.
REXCVAR_DEFINE_STRING(ffxiii2_mesa_env, "", "FFXIII-2 Switch",
                      "Switch: environment variables for Mesa/NVK, set before the Vulkan instance: \"NAME=value;...\"")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

namespace ffxiii2 {
void ApplyMesaEnvironment() {
  const std::string all = REXCVAR_GET(ffxiii2_mesa_env);
  size_t pos = 0;
  while (pos < all.size()) {
    size_t end = all.find(';', pos);
    if (end == std::string::npos) end = all.size();
    const std::string item = all.substr(pos, end - pos);
    pos = end + 1;
    const size_t eq = item.find('=');
    if (eq == std::string::npos || eq == 0) continue;
    const std::string name = item.substr(0, eq), value = item.substr(eq + 1);
    const int rc = setenv(name.c_str(), value.c_str(), 1);
    REXLOG_INFO("[mesa env] {}={}{}", name, value, rc == 0 ? "" : " (setenv failed)");
  }
}
}  // namespace ffxiii2
