# ffxiii2 - Final Fantasy XIII-2 (Europe + TU6), ReXGlue recompiled project.
# SDK: the sa2-nx fork in ../sdk (REXSDK_DIR). generated/ comes from tools/win/codegen.ps1 (one zone module per run).
#
#   Windows PC:       tools\win\build.ps1         (clang 20, emulated Xenos GPU on D3D12)
#   Nintendo Switch:  tools\win\build_switch.ps1  (devkitA64 GCC + libnx, emulated Xenos GPU on static NVK; the
#                     toolchain file is tools/switch/cmake/switch-devkitA64.cmake, from sa2-nx)
cmake_minimum_required(VERSION 3.25)
project(ffxiii2 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Switch: -mearly-ra=none for everything compiled for the console, before the SDK is added so its libraries get it
# too. Evidence (sa2-nx app/CMakeLists.txt, 2026-10-19): devkitA64 GCC 16.1's AArch64 early register allocation
# (-mearly-ra=all at -O2+) dropped table lookups of the XDK's streaming copy in Peggle 2's console build; sa2-nx,
# peggle2-nx, sasrt-nx and banjo-nx all build with it off. The installed devkitA64 here is GCC 16.1.0 too.
option(FFXIII2_SWITCH_EARLY_RA "Leave GCC's AArch64 early register allocation on (Switch; known miscompiles)" OFF)
if(REXGLUE_PLATFORM_SWITCH AND NOT FFXIII2_SWITCH_EARLY_RA)
    add_compile_options(-mearly-ra=none)
    add_link_options(-mearly-ra=none)
endif()

if(NOT REXSDK_DIR)
    set(REXSDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../sdk" CACHE PATH "rexglue-sdk source tree" FORCE)
endif()

include(generated/rexglue.cmake)

set(FFXIII2_SOURCES
    src/main.cpp
    src/ffxiii2_capture.cpp
    src/ffxiii2_irq.cpp
    src/ffxiii2_profile.cpp
    src/ffxiii2_switch_cvars.cpp
)
if(WIN32)
    add_executable(ffxiii2 WIN32 ${FFXIII2_SOURCES})
else()
    add_executable(ffxiii2 ${FFXIII2_SOURCES})
endif()
# GPU_PLUGINS xenos: the emulated GPU is loaded at run time (gpu_plugin = "xenos" in ffxiii2.toml), so it has to be
# staged next to the exe; the SDK copies it only when it is named here. On the Switch the plugin is linked into the
# NRO instead (sdk/cmake/rexglue_switch.cmake).
rexglue_setup_target(ffxiii2 GPU_PLUGINS xenos)

if(NOT REXGLUE_PLATFORM_SWITCH)
    # Settings next to the executable (only when missing: the copy there is the one to edit).
    add_custom_command(TARGET ffxiii2 POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/ffxiii2.toml -DDST=$<TARGET_FILE_DIR:ffxiii2>/ffxiii2.toml
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/copy_if_missing.cmake)
endif()

if(REXGLUE_PLATFORM_SWITCH)
    # *_register.cpp is one generated function of tens of thousands of SetFunction calls that runs once at start.
    # sa2-nx measured GCC -O3 at 771-927 s on its one file (the critical path of the whole build); -O0 compiles it in
    # seconds and costs a few microseconds of start-up (from sasrt-nx, 2026-10-06).
    foreach(_module default z0070 z0130 z0160 z0161)
        set(_register "${CMAKE_CURRENT_SOURCE_DIR}/generated/${_module}/ffxiii2_register.cpp")
        if(EXISTS "${_register}")
            set_property(SOURCE "${_register}" APPEND PROPERTY COMPILE_OPTIONS -O0)
        endif()
    endforeach()

    # The four zone modules (z0070/z0130/z0160/z0161.xex, all based at 0x88800000) go into the NRO: an NRO cannot load
    # libraries. Each generated SHARED library is built STATIC on this platform; its archive is partially linked into
    # one object, every hidden symbol is made local (each module's generated globals - PPCFuncMappings, sub_888xxxxx
    # of the same addresses, ... - share names with the other modules and the executable), and its two entry points
    # are renamed for src/ffxiii2_static_modules.cpp, which registers them under the shared-library name of
    # generated/default/module_registry.cpp. Method: sa2-nx's Data_DLL (app/CMakeLists.txt, sa2_static_modules.cpp).
    get_filename_component(_switch_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
    foreach(_module z0070 z0130 z0160 z0161)
        if(NOT TARGET ffxiii2_${_module})
            message(FATAL_ERROR "generated/${_module} is missing: the Switch build links all four zone modules")
        endif()
        set(_object "${CMAKE_CURRENT_BINARY_DIR}/ffxiii2_${_module}_linked.o")
        add_custom_command(OUTPUT "${_object}"
            # --force-group-allocation: COMDAT groups (inline functions) become ordinary sections resolved inside the
            # module; left as groups, the final link could keep another object's copy of a group whose symbols
            # --localize-hidden made local here (sa2-nx).
            COMMAND "${_switch_bin}/aarch64-none-elf-ld" -r --force-group-allocation -o "${_object}.tmp"
                    --whole-archive $<TARGET_FILE:ffxiii2_${_module}> --no-whole-archive
            COMMAND "${CMAKE_OBJCOPY}" --localize-hidden
                    --redefine-sym ReXModule_Register=ReXModule_Register_ffxiii2_${_module}
                    --redefine-sym ReXModule_GetImageInfo=ReXModule_GetImageInfo_ffxiii2_${_module}
                    "${_object}.tmp" "${_object}"
            DEPENDS ffxiii2_${_module}
            COMMENT "Linking the ${_module} zone module into one object for the NRO"
            VERBATIM)
        set_source_files_properties("${_object}" PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)
        target_sources(ffxiii2 PRIVATE "${_object}")
    endforeach()
    target_sources(ffxiii2 PRIVATE src/ffxiii2_static_modules.cpp)

    # The Switch build's fixed settings (ffxiii2_switch_defaults.toml) are compiled in (src/ffxiii2_app.h
    # BuiltInConfig); the SD card's ffxiii2.toml (written from ffxiii2_switch.toml when missing) overrides them.
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/ffxiii2_switch_defaults.toml" FFXIII2_BUILTIN_CONFIG)
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/ffxiii2_switch.toml" FFXIII2_PLAYER_CONFIG)
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/src/ffxiii2_builtin_config.h.in"
                   "${CMAKE_CURRENT_BINARY_DIR}/builtin_config/ffxiii2_builtin_config.h" @ONLY)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
                 "${CMAKE_CURRENT_SOURCE_DIR}/ffxiii2_switch_defaults.toml"
                 "${CMAKE_CURRENT_SOURCE_DIR}/ffxiii2_switch.toml")
    target_include_directories(ffxiii2 PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/builtin_config")
    target_compile_definitions(ffxiii2 PRIVATE FFXIII2_BUILTIN_CONFIG=1)

    # An emulator starts homebrew without a loader (no argv[0]): the NRO's folder is then its usual place on the SD.
    if(TARGET rexcore)
        target_compile_definitions(rexcore PRIVATE REX_SWITCH_FALLBACK_EXE="/switch/ffxiii2-nx/ffxiii2.nro")
    endif()

    # Line tables for the app's own code (crash logs and profiler samples are translated with addr2line on the ELF).
    target_compile_options(ffxiii2 PRIVATE -g1)

    rexglue_switch_add_nro(ffxiii2
        NAME "Final Fantasy XIII-2"
        AUTHOR "FFXIII-2-NX"
        VERSION "0.1.0")
endif()
