// Nintendo Switch: the four recompiled zone modules (z0070/z0130/z0160/z0161.xex) are linked into the NRO instead of
// being shared libraries the runtime opens (an NRO cannot load libraries). app/CMakeLists.txt partially links each
// module's archive into one object, makes everything but its two entry points local, and renames those two (all
// modules are generated with the same project name and the same base 0x88800000, so their other globals collide).
// They are registered here under the shared-library names of generated/default/module_registry.cpp, which
// KernelState::LoadUserModule looks up first (sdk/src/system/kernel_state.cpp, FindStaticModule).
// Method: sa2-nx's src/sa2_static_modules.cpp (one module there).

#include <rex/system/static_modules.h>

#define FFXIII2_ZONE_MODULE(name)                                                                  \
  extern "C" void ReXModule_Register_ffxiii2_##name(rex::runtime::IModuleRegistrar* registrar);   \
  extern "C" const rex::PPCImageInfo* ReXModule_GetImageInfo_ffxiii2_##name();
FFXIII2_ZONE_MODULE(z0070)
FFXIII2_ZONE_MODULE(z0130)
FFXIII2_ZONE_MODULE(z0160)
FFXIII2_ZONE_MODULE(z0161)
#undef FFXIII2_ZONE_MODULE

namespace {

const bool g_registered = [] {
  rex::system::RegisterStaticModule("ffxiii2_z0070", ReXModule_Register_ffxiii2_z0070,
                                    ReXModule_GetImageInfo_ffxiii2_z0070);
  rex::system::RegisterStaticModule("ffxiii2_z0130", ReXModule_Register_ffxiii2_z0130,
                                    ReXModule_GetImageInfo_ffxiii2_z0130);
  rex::system::RegisterStaticModule("ffxiii2_z0160", ReXModule_Register_ffxiii2_z0160,
                                    ReXModule_GetImageInfo_ffxiii2_z0160);
  rex::system::RegisterStaticModule("ffxiii2_z0161", ReXModule_Register_ffxiii2_z0161,
                                    ReXModule_GetImageInfo_ffxiii2_z0161);
  return true;
}();

}  // namespace
