# Console run of rc4 (2026-10-31 12:55-12:58): performance breakdown

Handheld (720p, GPU 460 MHz, CPU 1020 MHz), symbolised with `builds/2026-10-31_1225_rc4_movies_crash/katamari-nx.elf`.
**Three CPU cores**: the log says "Too few processor cores" and "running as 0100080021954000, not the forwarder
05D6EA1333487100". Your forwarder's id is not the one the package's four-core NPDM was made for, so it never applied
(rc3's run had the same). `renut.toml` now names your forwarder: the first launch writes its four-core NPDM, the
second launch has four cores.

## Frame rate, render thread and GPU, every 10 s

| Time | Where | Game fps | Render thread ms/frame | GPU ms/frame |
|---|---|---|---|---|
| 12:56:02-12:56:42 | logos, opening and prolog movies (clean), menus | 58-60 | 0.7-1.4 | 2.8-3.2 |
| 12:56:52-12:57:02 | loading | 45-50 | 0.7-0.9 | 3.0-3.5 |
| 12:57:12-12:57:52 | first level | 37.7-44.3 | 5.7-6.6 | 15.4-16.1 |
| 12:58:02-12:58:12 | first level, bigger ball | 28.1-35.2 | 7.2-9.3 | 16.8-17.1 |

**Gameplay is CPU-bound now** (handheld, 720p): the GPU needs 15-17 ms a frame (60 fps fits), but the game's main
thread is 89 % busy and its draw thread 83 %, on three cores shared with the audio threads (241 % of the 300 %).

A gameplay GPU frame (16.8 ms): 4.1 ms the 535 blended object draws (PS 0C4AF44C), 4.0 ms one full-screen pass
(PS 35DB0C4B, 2 draws), 2.5 ms colour resolves, 1.9 ms PS 6DAE04AD (15 draws), 1.6 ms depth resolves, the rest under
0.6 ms each.

## CPU by thread, gameplay (9 reports, mean 35.2 fps)

Share of each thread's samples. "hand" = hand-written natives, "machine" = machine-optimised copies, "generated" =
plain recompiled code (not optimised), the rest host code.

| Thread | CPU % of a core | hand | machine | generated | renderer | audio (host) | libc | runtime / kernel | app hooks |
|---|---|---|---|---|---|---|---|---|---|
| Main XThread (game logic) | 89.1 | 0.4 | 94.7 | 3.4 | 0 | 0 | 0.7 | 0.9 | 0 |
| XThread660 (draw submission) | 83.4 | 0.1 | 56.1 | 11.2 | 11.4 | 0 | 4.2 | 14.6 | 2.4 |
| XMA Decoder (host audio decode) | 33.6 | 0 | 0 | 0 | 0 | 35.7 | 13.2 | 51.1 | 0 |
| XThread18C0 (the game's audio engine) | 25.0 | 0 | 92.5 | 2.3 | 0 | 0.7 | 0.3 | 4.2 | 0 |
| renderer thread | 2.7 | 0 | 0 | 0 | 87.0 | 1.2 | 4.5 | 7.3 | 0 |
| Audio Worker | 2.1 | 0 | 5.7 | 0 | 0 | 11.4 | 8.6 | 74.3 | 0 |
| TimerQueue | 1.7 | 0 | 0 | 0 | 0 | 0 | 16.1 | 83.9 | 0 |
| main (UI) | 1.5 | 0 | 0 | 0 | 2.6 | 0 | 0 | 96.1 | 0 |
| 4 other threads | 1.0 and under | | | | | | | | |

All threads: 241 % of a core. Machine-optimised copies 155.5 % (65 %), host runtime/kernel 36.4 % (15 %), generated
code 12.9 % (5 %), host audio 12.4 % (5 %), renderer 11.9 % (5 %), libc 9.4 % (4 %), app hooks 2.0 %, hand-written
natives 0.4 % on their own (most of them run inlined inside the machine copies and are counted there).

## Top functions per thread (share of the thread's samples)

| Thread | Function | Share | Status |
|---|---|---|---|
| Main | sub_82277E70 | 18.2 % | machine (its matrix multiplies are hand natives inlined) |
| Main | sub_822683D0 | 15.8 % | machine |
| Main | sub_827A5B00 | 6.2 % | machine |
| Main | sub_822470D0 | 4.9 % | machine |
| Main | sub_8278B2B8 | 3.4 % | machine |
| Main | sub_8227A280 | 3.0 % | machine |
| Main | sub_8279F0B0 | 2.8 % | machine |
| Main | sub_822772C0 | 2.4 % | machine |
| XThread660 | sub_82738260 | 6.0 % | machine |
| XThread660 | renut CaptureDelta (draw capture) | 5.1 % | host renderer |
| XThread660 | sub_82746B70 | 4.6 % | machine |
| XThread660 | sub_82656DF8 | 4.4 % | machine |
| XThread660 | sub_82672410 | 4.1 % | machine |
| XThread660 | memcpy | 3.3 % | host libc |
| XThread660 | rex UpdateGuestClock | 3.2 % | host runtime |
| XThread660 | sub_826A13D8 | 3.2 % | generated (interlocked reference count: no copy) |
| XThread660 | sub_826707B0 | 2.4 % | generated (interlocked increment: no copy) |
| XThread18C0 | sub_82536F90 | 32.5 % | machine |
| XThread18C0 | sub_82539A88 | 26.7 % | machine |
| XThread18C0 | sub_82550418 | 14.0 % | machine |
| XThread18C0 | sub_8253FC20 | 5.2 % | machine |
| XMA Decoder | ffmpeg decode_frame | 15.7 % | host |
| XMA Decoder | free / malloc / mutexes | 21.2 % | host (allocation per frame) |
| XMA Decoder | memset / memcpy | 11.9 % | host |
| XMA Decoder | XmaContext::ConvertFrame | 5.8 % | host |

## Audio on this run

The XMA decoder's worker decoded 10,000-12,000 contexts per 10 s in the level (about 42 % of a core), the data ready
0.8 ms after a kick on average and up to 18 ms at worst; its frame cache re-decoded 10,000-20,000 frames per 10 s "to
catch the codec up". The game's audio engine (25 %) and the decoder share three cores with a main thread at 89 % and
a draw thread at 83 %: a likely source of the beeping and of sound effects that do not start.
