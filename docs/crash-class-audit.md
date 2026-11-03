# Crash-class audit of the generated code (FFXIII-2 EU + TU6, 2026-11-03)

Inputs: a copy of this repository with its generated code (the cloud work tree), rexglue from sa2-nx, images default_image*.bin
(z0160 re-dumped with its .xexp). Scripts: tools/local/audit/*.py (raw outputs and codegen logs were kept outside the repository).
**Result: no toml entry to add or remove in any module.** One doc correction (finding 2d).

## 1. z0160 with TU6 (z0160.xexp)
- The patch changes only 4 header/debug ranges: PE TimeDateStamp 0x888000F0, debug-dir stamp 0x88800894, build string
  0x888008B3 ("white=12814" -> "white=14954"), RSDS GUID 0x888012A8. .text/.rdata/.data are unchanged. The regenerated C++ is
  byte-identical to the old output (only codegen.stamp changed). Patched z0160 differs from patched z0070 in 14 header/debug words only.
- Codegen: no warnings. ptrscan: 0 candidates, 0 thunks, 0 prologue gaps. switch_check: 0 tables. The 34 indirect bctr are 33 import
  thunks plus 1 vtable call. No setjmp shapes. Exports 0x88810000/0x88810040/0x88810050 and xstart 0x88810368 are registered.
- `0x88814418 = { size = 168 }` is still correct: it ends at 0x888144C0 (__imp____C_specific_handler thunk). With it commented out,
  codegen fails with "b 0x8881446C from 0x888144BC - target not in any function" (codegen_z0160_nomemcpy.log). Nothing else is missing.

## 2. Pointer / gap class (including mid-function targets)
Method: every 4-aligned word of every non-code section except .pdata (.reloc/.edata/.idata lie outside the dumped images), every
code word not emitted as an instruction, and every lis+addi/ori pair. Each value that lands in code is checked against the registered
starts and the generated extents (labels + instruction comments, .pdata length).
Distinct targets: default 24,541 (17,684 starts / 6,857 inside a function / **0 in gaps**); z0130 1,481 (575/906/0); z0161 202 (132/70/0);
z0160 and z0070 45 (23/22/0). Every target inside a function is explained:
- a) Jump-table case labels (switch cases in the generated code): default 6,082, z0130 771, z0161 15. Inline table base addresses
  (lis/addi feeding lwzx; table data placed right after its bctr): default 401, z0130 79, z0161 1 (0x888164A4 from 0x8881648C).
- b) SEH scope tables (handler __C_specific_handler; records begin/end/handler/target): default 53 tables / 59 records, z0130 22/25,
  z0161 21/24, z0160 and z0070 9/9. Every handler/filter is a registered function. The __except targets (default 22, z0130 6, z0161 6,
  z0160 4: 0x88810320, 0x88810D0C, 0x888110B0, 0x88812C40) are reached only through RtlUnwind, which is a stub in the SDK, so they are
  data, not entries. Most of them are not emitted (codegen treats them as unreachable).
- c) C++ EH: the only real FuncInfo is 0x821CED18 (magic 0x19930522, function 0x82B6BEE0, handler sub_82C15368). Its IP-to-state
  entries 0x82B6BF20/0x82B6BF34 are not entries; its unwind action 0x82B6BF50 is registered. The other 0x1993052x words are coincidences.
- d) RtlUnwind TargetIp: 0x82B50030 (lis/addi at 0x82B4FFE0 in CRT sub_82B4FF38, passed to _local_unwind sub_82B58800 -> RtlUnwind)
  and 0x82C18444 (0x82C1841C/0x82C18430 in sub_82C183E0, passed to __imp__RtlUnwind). The SDK's RtlUnwind returns without jumping,
  and both callers then continue at that same address. **Not entries.** Side effect, outside this audit's crash classes: the stub skips
  the __finally (_unlock_str sub_82B5005C) on the fopen error path when the mode string is empty.
- e) Coincidences, default: 233 data targets are Shift-JIS text or hash data in .rdata/.data (for example 0x82A282DC x55 = text bytes 82A2 82DC), including
  70 round 0x82xx0000 values (a char-code table at 0x820040AC-0x82004230). The 4 code words are instructions inside unemitted SEH blocks.
  In zone .rdata, values that look like default.xex code addresses are SJIS text too (z0130 48, z0161 5).
  There is no real cross-module pointer, and zone code has no lis of a default.xex code address.
- f) Gaps: default has 208 SEH handler pairs, 360 dead `blr` after b/bctr, and two unreferenced gaps: 0x8286E2C0 (68 B) and **0x8286E518 (36 B)**.
  Neither has a data word, lis/addi or branch pointing at it. Their tails 0x8286E304/0x8286E53C are registered and reached only by bc from
  these heads: dead CRT code, no entry needed. **Doc fix:** stage2-codegen.md names only 0x8286E2C0. Zones: SEH pairs, dead blr, and the
  XEX export table at the end of .text (0x888146D0, 0x8887BEF8, 0x88825370).
- g) sub_82C18484 is a one-instruction function that codegen made from the handler word (0x82C184B0) of 0x82C1848C. It is unreferenced, so
  harmless.

## 3. Jump tables (switch_check on every code range)
- default: truncated 0, wrong 0, extra 0, "no generated code" 0. Missed 5 (sub_8263D798, sub_8263D7C0, sub_8279DA98, sub_8279DB20,
  sub_8279DB58): tail-call dispatch tables, all 104 labels registered, correct as indirect calls.
- default, 10 no-bound tables and 3 odd shapes (bctr 0x82782F70, 0x82782FB0, 0x8282A0E0): handler tables 0x821170CC (11), 0x821170F8 (39),
  0x82117198/0x82117298 (64), 0x82CB3058 (256). They have null slots; every non-null slot is registered.
- z0130: 82 tables found, 82 generated. z0161: 3/3. z0160 and z0070: none. Clean.

## 4. setjmp / longjmp
- sjlj.py, plus a scan of all code for any base register and for r1 saved/restored through a base other than r1.
- r1 is stored only at 0x82C15ED8 (setjmp 0x82C15C20) and loaded only at 0x82C15BDC (longjmp 0x82C15900) and 0x82C183B4.
  0x82C183B4 is in sub_82C182FC, a CRT EH continuation: it restores a CONTEXT from r4 and does mtlr r3; blr. It is called only from
  0x82C17818 on the C++ throw path. It is not setjmp-like, and it is not reached, because the SDK does not dispatch C++ exceptions.
- No other setjmp-like code in default or the zones. Other multi-register clusters have no r1/LR save, so they are struct copies.
- setjmp/longjmp have no caller: no b/bl, no data word except .pdata, no lis/addi. Hook word 0x82EBF700 = 0, and the generated code
  contains no ppc_setjmp. The declaration is right and is read from the include (config.cpp LoadFromTable).

## 5. Extents cut short
- 58 default functions, 7 z0130, 7 z0161 and 2 z0160 end without a terminal instruction. Each ends in a bl to a noreturn routine:
  KeBugCheck, ExTerminateThread, XamLoaderTerminateTitle, _CxxThrowException sub_82C150C8 and its throw wrappers sub_82B6BB98/sub_82B6BB60,
  or sub_82B55240. After the bl comes zero padding, the next function, or an SEH pair.
- Unemitted code inside function extents appears only after an unconditional exit: the SEH __except blocks (2b), plus the dead epilogue
  0x824FF4C0 of the infinite loop sub_824FF480.
- No b/bc leaves a function into a gap or into another function's middle: the generated code has 0 REX_FATAL / "unresolved" markers, and
  every codegen log (all 5 modules regenerated) has no warnings. Clean.

## 6. Other
- All exports registered: z0070/z0160 3, z0130 6, z0161 10. TU entry points (xstart 0x88810368, 0x88868EB0, 0x88817FD0) registered.
- No vtable or .data/.tls function-pointer entry points into a gap (2 covers every data word).
- Not a missing-entry problem, but relevant: the SDK does not dispatch exceptions. generate_exception_handlers is off (no SEH_TRY), and RtlUnwind,
  RtlCaptureContext and __C_specific_handler are stubs. A C++ throw goes to RtlRaiseException(0xE06D7363), which calls debug::Break and returns.
  So the noreturn throw sites in 5 would return into their callers.

Not verifiable statically: code addresses computed at run time other than through lis/addi, and whether the game raises exceptions.
