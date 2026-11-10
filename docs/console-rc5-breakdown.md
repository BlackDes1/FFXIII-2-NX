# Console run of rc5 (2026-11-01 07:09-07:12): performance breakdown

Handheld (720p, GPU 460 MHz, CPU 1020 MHz), symbolised with `builds/2026-10-31_1525_rc5_xma_loops/katamari-nx.elf`.
Still **three cores** on this launch: it wrote the four-core NPDM for your forwarder ("forwarder NPDM written"); the
next launch has four.

## Frame rate, render thread and GPU, every 10 s

| Time | Where | Game fps | Render thread ms/frame | GPU ms/frame |
|---|---|---|---|---|
| 07:10:02-07:10:32 | logos, movies, menus | 58.5-60.0 | 0.7-1.6 | 2.8-3.1 |
| 07:10:42-07:10:52 | loading | 45-48 | 0.8-0.9 | 3.1-3.5 |
| 07:11:02-07:11:42 | first level | 38.9-43.3 | 5.6-6.4 | 15.3-16.3 |
| 07:11:52-07:12:32 | first level, bigger ball | 33.9-36.2 | 7.0-7.7 | 16.4-18.1 |

Gameplay is CPU-bound (the GPU needs 15-18 ms a frame; the main thread is 80 % busy, the draw thread 72 %).

## CPU by thread, gameplay (14 reports, mean 39.0 fps)

"hand" = hand-written natives, "machine" = machine-optimised copies, "not optimised" = plain recompiled code, the rest
host code (the port's own C++).

| Thread | CPU % of a core | hand | machine | not optimised | renderer | audio (host) | libc | runtime / kernel | app hooks |
|---|---|---|---|---|---|---|---|---|---|
| Main XThread (game logic) | 79.8 | 0.3 | 91.6 | 4.4 | 0.1 | 0 | 1.1 | 2.6 | 0 |
| XThread51C0 (draw submission) | 71.7 | 0.1 | 57.4 | 10.4 | 11.4 | 0 | 4.0 | 14.0 | 2.7 |
| XThread0E20 (the game's audio engine) | 27.3 | 0 | 90.3 | 2.4 | 0 | 0.9 | 0.4 | 6.0 | 0 |
| XMA Decoder (host audio decode) | 18.4 | 0 | 0 | 0 | 0 | 37.4 | 9.8 | 52.8 | 0 |
| XThread0280 (streaming / movies) | 2.7 | 0 | 58.2 | 17.3 | 0 | 0 | 3.6 | 20.9 | 0 |
| renderer thread | 2.4 | 0 | 0 | 0 | 86.9 | 1.4 | 3.6 | 8.1 | 0 |
| Audio Worker | 1.7 | 0 | 9.9 | 2.5 | 0 | 8.6 | 6.2 | 72.8 | 0 |
| TimerQueue | 1.6 | 0 | 0 | 0 | 0 | 0 | 10.9 | 89.1 | 0 |
| main (UI) | 1.2 | 0 | 0 | 0 | 0 | 0 | 0 | 100 | 0 |
| 5 other threads | under 1 each | | | | | | | | |

All threads: 209 % of a core (rc4: 241 %; the XMA decoder 18 % instead of 34 % with its frame cache off).
Machine-optimised copies 142.3 % (68 %), host runtime/kernel 28.4 % (14 %), not optimised 12.2 % (6 %), renderer
10.3 % (5 %), host audio 7.3 % (3 %), libc 6.4 % (3 %), app hooks 1.9 %, hand-written natives 0.3 % on their own
(most of them run inlined inside the machine copies and are counted there).

## Top functions per thread (share of the thread's samples)

| Thread | Function | Share | Status |
|---|---|---|---|
| Main | sub_82277E70 | 17.9 % | machine (its matrix multiplies are hand natives, inlined) |
| Main | sub_822683D0 | 14.3 % | machine |
| Main | sub_827A5B00 | 6.3 % | machine |
| Main | sub_822470D0 | 5.0 % | machine |
| Main | sub_8278B2B8 | 2.9 % | machine |
| Main | sub_8227A280 | 2.9 % | machine |
| Main | sub_8279F0B0 | 2.7 % | machine |
| Main | sub_822772C0 | 2.0 % | machine |
| Draw | sub_82746B70 | 5.3 % | machine |
| Draw | sub_82738260 | 4.9 % | machine |
| Draw | sub_82656DF8 | 4.5 % | machine |
| Draw | renut CaptureDelta (draw capture) | 4.5 % | host renderer |
| Draw | sub_82672410 | 4.2 % | machine |
| Draw | sub_826A13D8 | 3.1 % | not optimised (interlocked reference count: no copy) |
| Draw | memcpy | 3.0 % | host libc |
| Draw | rex UpdateGuestClock | 2.9 % | host runtime |
| Audio engine | sub_82536F90 | 29.6 % | machine |
| Audio engine | sub_82539A88 | 23.6 % | machine |
| Audio engine | sub_82550418 | 17.5 % | machine |
| Audio engine | sub_825504F0 | 4.5 % | machine |
| Audio engine | sub_8253FC20 | 4.5 % | machine |
