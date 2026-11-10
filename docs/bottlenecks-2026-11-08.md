# Bottlenecks on the console (2026-11-08, k257 baseline, New Bodhum field, handheld, stock clocks)

Runs: k257_diag (overlay), k260_diag (overlay + stack sampler for this run only, 39,356 samples; ELF out\k260\renut),
k261_ab_overlay (overlay on/off every 5 s in one session). Logs under logs\switch\20261108_1105_*, _1117_*, _1135_*.

## Where the frame goes (steady part, ~14-17 fps)
- Frame 60-70 ms. Main Thread (the game's) 85-90 % of a core (overlay "Thread CPU use") = the limit.
- Render thread 13-14 ms a frame; GPU 30-34 ms a frame (busy about half the frame). Neither limits now; the GPU would
  be the next wall at the 30 fps cap.
- Overlay cost (k261, same session, 47 pairs): on 14.2 fps, off 14.6 fps (-0.4 fps, ~3 %). Fine for measuring.

## Main Thread, sampled (7,113 samples)
- 82.7 % game code (61.6 % already native copies, 21 % still generated), 6.5 % renderer front-end on the game thread,
  5.3 % kernel/sync, 1 % libc.
- Flat: 1,391 functions; the top 10 are 10.5 %, top 100 39.7 %. The 0x8296xxxx-0x829Axxxx range (the engine's draw
  submission into our D3D natives) is ~23 %.
- Host top: CaptureDelta 2.2 % (per-draw device compare), memcpy 0.8, mutex 1.3, RtlEnterCriticalSection 0.6,
  ObjectTable::LookupObject 0.5, SetStreamSource 0.45, Ffxiii2StableSurface 0.42, SetTexture 0.39.

## Other threads
- XThread (the game's second heavy thread) 32-65 %: 90 % game code, two copies sub_82A34BB0 15 % and sub_82BB4008 10 %.
- Render thread: memcpy 11 %, DrawStateLocked 10 %, PagesClean 7.4 %, UploadFloatConstants 5 %, armDCacheClean 4.7 %.
- XMA Decoder 15 % of a core: decoding plus the 320-context sweep.

## Solutions (exact unless said)
1. Game code on the main thread (the limit): broad, not one function.
   a. PGO + LTO (the user's call): the only lever that speeds all 1,391 functions at once.
   b. Next machine-copy round from this profile (21 % of the main thread still generated), after a PC check of the
      copies with banjo_cpu_verify (D-011 crash below not yet ruled out).
   c. Hand-written natives: no function reaches the 2 % gate except CaptureDelta (host).
2. CaptureDelta (2.2 %): compare only the device chunks the D3D natives touched since the last draw (dirty bits set
   in the natives), instead of 39-71 chunks per draw. Same delta, less comparing.
3. Sync on the main thread (5.3 %): lock-free ObjectTable lookups (~0.5 %); the rest is the game's own critical sections.
4. GPU (next wall): per-pass GPU timing in the field and in battle before choosing; dynamic resolution stays off.

## Fixed today
- The SDK sampler ignored switch_profile_stacks from the config (read before the config is applied): now read at
  every report until set (sdk/src/ui/switch_perf.cpp, k260). Same for switch_exclusive_core (k258b).
- nav_ffxiii2.py: a truncated screenshot no longer ends the navigation.

## Open
- D-011: one guest access violation (write of 0x18010000, thread F80002C4, in the XMA submit path sub_82B92878)
  13 s after a start, 1 of ~10 runs today. Not in a copied function; copies not yet verified on the PC.
