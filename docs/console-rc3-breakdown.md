# Console run of rc3 (2026-10-31 09:03-09:11): performance breakdown

The logs in `builds/2026-10-30_2105_rc2_diet_dof/COPY_TO_SD_switch_katamari-nx/logs` are from **rc3** (they show the
16 DLC installs at 09:03:10; rc2 has no DLC installer), so they are symbolised against
`builds/2026-10-31_0030_rc3_dlc_full/katamari-nx.elf`. Set-up: ReverseNX docked mode with handheld clocks (CPU
1020 MHz, GPU 460 MHz, memory 1600 MHz), so the port renders at 1920x1080 (the docked size) on the handheld GPU clock.

## Frame rate, render thread and GPU, every 10 s

| Time | Where | Game fps | Render thread ms/frame | GPU ms/frame |
|---|---|---|---|---|
| 09:03:28-09:05:18 | logos, opening movie, title, menus | 50-60 | 0.7-1.5 | 6-8 |
| 09:05:28 | loading into the level | 38 | 3.5 | 19 |
| 09:05:38-09:06:58 | first level | 28.5-30.9 | 6.2-8.7 | 32-34.5 |
| 09:07:08-09:08:48 | first level, larger ball | 23.6-29.4 | 7.3-12.8 | 33.5-40.6 |
| 09:09:08-09:09:39 | results, menus | 58-60 | 0.7-1.0 | 7.8-8.7 |
| 09:10:09-09:11:40 | second level | 33-34.7 | 2.7-3.8 | 24-28.4 |
| 09:11:42 | crash leaving the collection menu (fixed for rc4) | | | |

**Gameplay is GPU-bound.** The GPU needs 24-40 ms a frame at 460 MHz and 1080p; the busiest CPU thread is 62 % busy.
At the docked GPU clock (768 MHz) the same frames would take roughly 14-24 ms. CPU work does not raise this frame
rate; GPU work, a lower resolution or the docked clock does.

A gameplay GPU frame (09:06:28, 34.2 ms): 9.0 ms one full-screen pass (PS 35DB0C4B, 2 draws), 6.3 ms the 410
blended draws a frame of PS 0C4AF44C (the level's objects), 5.1 ms colour resolves, 3.7 ms PS 6DAE04AD (14 draws),
3.4 ms depth resolves, 1.3 ms PS 464A90A8, the rest under 1 ms each.

## CPU by thread, gameplay (33 reports under 45 fps, mean 29.9 fps)

Share of each thread's samples. "hand" = hand-written natives, "machine" = machine-optimised copies (localized + bulk),
"generated" = plain recompiled code (not optimised), the rest host code.

| Thread | CPU % of a core | hand | machine | generated | renderer | audio (host) | libc | runtime / kernel |
|---|---|---|---|---|---|---|---|---|
| Main XThread (game logic) | 61.6 | 0.9 | 78.8 | 16.6 | 0 | 0 | 0.7 | 3.0 |
| XThread2D40 (draw submission) | 50.6 | 1.4 | 49.6 | 15.9 | 12.6 | 0 | 4.1 | 14.0 (+2.5 app hooks) |
| XMA Decoder (host audio decode) | 47.5 | 0 | 0 | 0 | 0 | 35.1 | 12.2 | 52.7 |
| XThread0080 | 31.9 | 0 | 85.1 | 6.3 | 0 | 1.1 | 0.7 | 6.7 |
| XThreadF580 (job worker) | 18.3 | 0.4 | 63.0 | 34.0 | 0 | 0 | 0.6 | 1.9 |
| XThread3840 (job worker) | 18.1 | 0.5 | 59.5 | 36.1 | 0 | 0 | 0.4 | 3.5 |
| XThread2040 (job worker) | 17.8 | 0 | 59.7 | 36.8 | 0 | 0 | 0.5 | 3.1 |
| XThread4F80 (job worker) | 17.8 | 0.2 | 61.9 | 34.8 | 0 | 0 | 0.2 | 3.0 |
| Audio Worker | 2.0 | 1.5 | 0 | 10.6 | 0 | 6.1 | 9.8 | 72.0 |
| renderer thread | 1.7 | 0 | 0 | 0 | 82.3 | 2.3 | 6.5 | 9.0 |
| TimerQueue | 1.7 | 0 | 0 | 0 | 0 | 0 | 7.2 | 92.8 |
| 11 other threads | under 1.5 each | | | | | | | |

All threads together: 275 % of the four cores' 400 %. Of that, machine-optimised copies 146 % of a core (53 %),
generated code 48 % (18 %), host runtime/kernel 44 % (16 %), host audio 17 % (6 %), libc 10 %, renderer 8 %,
hand-written natives 1.5 %.

## Top functions per thread (share of the thread's samples)

| Thread | Function | Share | Status |
|---|---|---|---|
| Main | sub_82277E70 | 14.3 % | machine |
| Main | sub_822683D0 | 10.8 % | machine |
| Main | sub_827A5B00 | 6.9 % | machine |
| Main | sub_822470D0 | 4.0 % | machine |
| Main | sub_827987A8 | 3.3 % | machine |
| Main | sub_8278B2B8 | 3.2 % | machine |
| Main | sub_8227A280 | 3.0 % | machine |
| Main | sub_8279F0B0 | 2.3 % | machine |
| XThread2D40 | sub_82738260 | 5.8 % | machine |
| XThread2D40 | sub_82656DF8 | 5.4 % | machine |
| XThread2D40 | sub_82746B70 | 5.3 % | machine |
| XThread2D40 | renut CaptureDelta (draw capture) | 5.2 % | host renderer |
| XThread2D40 | sub_82672410 | 3.2 % | machine |
| XThread2D40 | sub_826A13D8 | 3.0 % | generated (lock-free: no copy) |
| XThread2D40 | sub_826707B0 | 2.5 % | generated |
| XThread0080 | sub_82539A88 | 26.2 % | machine |
| XThread0080 | sub_82536F90 | 24.9 % | machine |
| XThread0080 | sub_82541A50 | 11.5 % | machine |
| XThread0080 | sub_8253FC20 | 7.0 % | machine |
| workers (x4) | sub_8221BF20 | ~31 % | machine |
| workers (x4) | sub_8221BC58 | ~20 % | machine |
| workers (x4) | sub_82317F48 | ~12 % | generated |
| workers (x4) | sub_82316A78 | ~10 % | generated |
| workers (x4) | sub_8275AA88 | ~7.5 % | machine |
| XMA Decoder | ffmpeg decode_frame | 15.2 % | host |
| XMA Decoder | malloc / free / mutex | 24.5 % | host (allocation per frame) |
| XMA Decoder | XmaContext::ConvertFrame | 7.5 % | host |

The hand-written natives (matrix multiply, copies, hash) are 0.5 % of busy time in this run: the paths they won on
the PC and in Ryujinx are not where this console session spent its time.
