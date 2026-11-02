# Stage 2: code generator and translation (2026-11-02)

Code generator: `rexglue` from the sa2-nx SDK fork (ReXGlue 0.10.0 + sa2-nx changes), built in a Linux cloud workspace
(clang 18, `-march=x86-64-v2`) because the PC's Linux shell cannot run Windows programs. Cloud-only build shims, not
part of the port: SDL X11/Wayland backends off, two declaration-only headers (`X11/Xlib-xcb.h`, `wayland-client.h`),
`-D__cpp_concepts=202002L` (clang 18 + libstdc++ 13 hide `std::expected`). The Windows build (clang 20) needs none.

Inputs: game_root = disc `default.xex`, `z0070/z0130/z0160/z0161.xex` with the TU6 `.xexp` beside each (the SDK
applies `<file>p` on load: "XEX patch applied successfully: base version: 0.0.0.11, new version: 0.0.6.11" for all
but z0160, which TU6 does not patch). Patched default.xex: entry 0x82B65478 (`xstart`, mainCRTStartup), code
`.text` 0x82250000-0x82C312D0 + BINK 0x82C31800-0x82C44050 + RADCODE 0x82C44800-0x82C452F4.

## Results

| check | default.xex | z0070 | z0130 | z0160 | z0161 |
|---|---|---|---|---|---|
| codegen | OK, 50,196 functions | OK | OK | OK | OK |
| "target not in any function" fixed | 20 | 1 (memcpy) | 0 | 1 (memcpy) | 0 |
| pointer-only functions added | 304 + 24 thunks | 0 | 1 | 0 | 0 |
| jump tables truncated / wrong / missed | 0 / 0 / 5* | 0/0/0 | 0/0/0 | 0/0/0 | 0/0/0 |
| setjmp / longjmp | 0x82C15C20 / 0x82C15900 | - | - | - | - |

\* the 5 "missed" bctr (sub_8263D798, sub_8263D7C0, sub_8279DA98, sub_8279DB20, sub_8279DB58) are tail-call dispatch
tables whose 104 targets are all registered functions (one-instruction bitfield getters): emitted correctly as
indirect calls. Adding them as [[switch_tables]] would be wrong. The 10 unbounded tables and 3 odd shapes are
function-pointer / handler tables (with null slots) whose non-null targets are all registered functions.

Every entry is in `app/analysis/*.toml` with its evidence. Fixpoint: after the additions, a second pointer pass
finds 0 candidates; the remaining non-padding gaps (1,976 bytes) are SEH data words before functions and one
unreferenced function (0x8286E2C0, no caller, no pointer).

## Things found on the way

- Zone DLLs share base 0x88800000: run codegen once per module (`--target`), then
  `tools/local/post_codegen_modules.py` lists all four in module_registry.cpp / dll_targets.cmake and turns the
  build's own all-modules codegen step into a stamp touch. (Same finding as leftisthominid/ffxiii-2-port.)
- Function extents cannot be taken from the generated instruction comments alone: codegen omits blocks it finds
  unreachable (sub_82B688E0: 513 comments, 530 instructions). ptrscan uses .pdata lengths + labels.
- leftisthominid/ffxiii-2-port (USA, TU 0.0.1.13) is a useful cross-check: 13/20 pass-1 addresses, 257/268 of its
  data-pointer functions and 35/58 of its virtual thunks are at the same addresses here, so the EU TU6 code layout
  is largely the USA one. Its sizes were not reused (five of its pass-1 sizes stop at the first blr).

## Not verified yet

- Nothing has been compiled or run: the generated C++ is compiled on Windows by `tools\win\build.ps1` (stage 3).
- SEH funclets (e.g. 0x82B690F4 / 0x82B69110 in sub_82B688E0) are only reached by exception dispatch; whether the
  game raises structured exceptions at run time is unknown.
