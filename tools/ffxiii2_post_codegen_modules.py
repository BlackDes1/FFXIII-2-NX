#!/usr/bin/env python3
"""ffxiii2-nx: after the per-module codegen runs (codegen --target zXXXX, one zone DLL per run: all four load at
0x88800000, and analysing them together maps them over each other -> bogus "target not in any function"), write the
two files a run writes for its one module for all four, and make the build's own codegen step a no-op (it would run
all modules at once). From the earlier ffxiii2-nx tree's tools/local/post_codegen_modules.py, for katamari-nx's layout
(the entry point in app/generated, the modules in app/generated_zones/<module>).
usage: ffxiii2_post_codegen_modules.py APP_DIR"""
import os
import re
import sys

MODULES = ["z0070", "z0130", "z0160", "z0161"]
app = sys.argv[1]
gen = os.path.join(app, "generated")


def find(name):
    for d, _, files in os.walk(gen):
        if name in files:
            return os.path.join(d, name)
    sys.exit(f"{name} not found under {gen}")


p = find("module_registry.cpp")
s = open(p).read()
calls = list(re.finditer(r'  kernel_state->RegisterRecompiledModule\(\s*"(\w+)\.xex",.*?\);\n', s, re.S))
if not calls:
    sys.exit(f"no module registration in {p}")
one = calls[0].group(1)
body = "\n".join(calls[0].group(0).replace(one, m) for m in MODULES)
s = s[:calls[0].start()] + body + s[calls[-1].end():]
open(p, "w").write(s)

p = find("dll_targets.cmake")
s = open(p).read()
blocks = list(re.finditer(r"\n# DLL module: (\w+)\n.*?\nendif\(\)\n", s, re.S))
if not blocks:
    sys.exit(f"no module block in {p}")
one = blocks[0].group(1)
body = "".join(blocks[0].group(0).replace(one, m) for m in MODULES)
s = s[:blocks[0].start()] + body + s[blocks[-1].end():]
open(p, "w").write(s)

p = find("rexglue.cmake")
s = open(p).read()
if "FFXIII2: codegen runs outside the build" not in s:
    stamp = re.search(r'OUTPUT "([^"]*codegen\.build\.stamp)"', s)
    stamp = stamp.group(1) if stamp else "${CMAKE_CURRENT_SOURCE_DIR}/generated/codegen.build.stamp"
    s, n = re.subn(r"( *)COMMAND \$<TARGET_FILE:rex::rexglue> codegen [^\n]*\n",
                   lambda m: f'{m.group(1)}# FFXIII2: codegen runs outside the build (tools/ffxiii2_codegen.bat, one zone module per run)\n'
                             f'{m.group(1)}COMMAND ${{CMAKE_COMMAND}} -E touch "{stamp}"\n', s)
    if n != 1:
        sys.exit(f"codegen build step not found in {p}")
    open(p, "w").write(s)
for m in MODULES:
    if not os.path.exists(os.path.join(app, "generated_zones", m, "sources.cmake")):
        sys.exit(f"generated_zones/{m} missing: run codegen --target {m}")
print(f"module registry + dll targets: {', '.join(MODULES)}; build-time codegen off")
