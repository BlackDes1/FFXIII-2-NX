# ffxiii2 - Final Fantasy XIII-2 (Europe + TU6), ReXGlue recompiled project.
# SDK: the sa2-nx fork in ../sdk (REXSDK_DIR). generated/ comes from tools/win/codegen.ps1 (one zone module per run).
cmake_minimum_required(VERSION 3.25)
project(ffxiii2 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT REXSDK_DIR)
    set(REXSDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../sdk" CACHE PATH "rexglue-sdk source tree" FORCE)
endif()

include(generated/rexglue.cmake)

set(FFXIII2_SOURCES
    src/main.cpp
    src/ffxiii2_capture.cpp
    src/ffxiii2_irq.cpp
    src/ffxiii2_profile.cpp
)
if(WIN32)
    add_executable(ffxiii2 WIN32 ${FFXIII2_SOURCES})
else()
    add_executable(ffxiii2 ${FFXIII2_SOURCES})
endif()
# GPU_PLUGINS xenos: the emulated GPU is loaded at run time (gpu_plugin = "xenos" in ffxiii2.toml), so it has to be
# staged next to the exe; the SDK copies it only when it is named here.
rexglue_setup_target(ffxiii2 GPU_PLUGINS xenos)

# Settings next to the executable (only when missing: the copy there is the one to edit).
add_custom_command(TARGET ffxiii2 POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/ffxiii2.toml -DDST=$<TARGET_FILE_DIR:ffxiii2>/ffxiii2.toml
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/copy_if_missing.cmake)
