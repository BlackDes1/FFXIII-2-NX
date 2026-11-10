# Console run of rc9 (2026-11-01 14:20-14:58 and 21:26-21:27): performance breakdown

Handheld throughout (720p, CPU 1020 MHz, GPU 460.8 MHz, memory 1600 MHz), **three cores**, symbolised with
`builds/2026-11-01_1213_rc9_shaders_dlc/katamari-nx.elf`. The console slept twice: 14:27:45-14:47:24 and
14:58:36-21:26:47. The DLC list had the seven stage packs and hid the nine unlock packs.

The `rex_crash.log` that came with these logs is not from rc9. The faulting instruction it records (`brk #1000` at
image+0x2914, lr in sub_822AE650) is rc6's supermarket crash (sub_822AC470). In rc9's ELF that address holds a `bl`.
The file was still on the SD card from the rc6 run; rc7 fixed that crash.

## Frame rate, render thread and GPU, every 10 s

Places by the music file playing (the stage files have no names in the game's tables).

| Time | Where | Game fps | Render thread ms/frame | GPU ms/frame |
|---|---|---|---|---|
| 14:20:14-14:20:54 | logos, title, save | 47-60 | 0.6-1.1 | 2.7-3.1 |
| 14:21:04-14:22:54 | stage, music M11_TUTORIAL | 39.6-45.6 | 5.4-7.6 | 15.4-18.0 |
| 14:23:04-14:23:34 | results | 53-60 | 0.7-2.9 | 3.8-8.4 |
| 14:23:55-14:25:15 | hub, music M00_SELECT | 59.6-60.1 | 1.2-3.0 | 8.0-15.1 |
| 14:25:55-14:27:45 | stage, music M71_KDXSE / M30_KDXBGM | 24.5-28.4 | 3.7-7.0 | 14.9-16.8 |
| (asleep) | | | | |
| 14:47:34-14:51:04 | the same stage, bigger ball | 19.4-29.7 | 4.2-14.0 | 14.9-20.8 |
| 14:51:14-14:52:35 | results, hub | 44-60 | 0.7-3.2 | 3.6-14.4 |
| 14:52:45-14:53:55 | collection, music M05_COLLECTION | 58.7-60.1 | 1.5-3.2 | 5.8-14.9 |
| 14:54:25-14:57:36 | a stage (last music file opened: M05_COLLECTION) | 29.2-46.6 | 3.4-10.0 | 15.4-18.2 |
| 14:57:56-14:58:36 | results | 50-60 | 0.8-1.4 | 3.1-4.0 |
| (asleep) | | | | |
| 21:26:47-21:27:27 | hub, presents menu (your screenshots, 21:26:58 and 21:27:06) | 37.6-60.3 | 2.6-3.3 | 8.6-15.7 |

**The M71_KDXSE stage is the slowest place so far: 27 fps on average, 19-30, with the GPU at 15-21 ms. It is
CPU-bound, by the game's main thread.**

## Threads and cores

The process has three cores (mask 0x7). The game's threads and most host threads may run on any of the three; the
system places them. Pinned threads:
- Renut's render thread: core 1 only, priority 0x2C (above the game's 0x3B, so it takes core 1 when it has work).
- The audio output thread and the UI thread: core 0 only.

The profile samples threads and does not record which core a sample ran on, so the per-core figures are the pinned
threads plus the floating ones' total.

| Thread | Cores | Tutorial (45 fps) | M71_KDXSE stage (27 fps) | 14:54 stage (37 fps) | Hub (61 fps) | Presents menu after sleep | Top function |
|---|---|---|---|---|---|---|---|
| Main XThread (game logic) | 0-2 | 95.7 | **101.3** | 99.3 | 36.3 | 37.7 | sub_82277E70 / sub_822683D0 (machine) |
| XThreadCB58A4C0 (draw submission) | 0-2 | 87.6 | 49.0 | 72.6 | 38.3 | 40.1 | sub_82738260 (machine) |
| Renut render thread | 1 | 28.8 | 17.9 | 25.9 | 15.0 | 15.9 | FastDrawLocked |
| XThreadCB5679C0 (the game's audio engine) | 0-2 | 12.9 | 14.2 | 16.1 | 12.0 | 11.4 | sub_82536F90 (machine) |
| XMA Decoder (host) | 0-2 | 3.5 | 6.4 | 8.6 | 2.2 | 2.5 | decode_frame (FFmpeg) |
| TimerQueue (host) | 0-2 | 1.7 | 2.7 | 1.7 | under 1 | **74.2** | TimerThreadMain |
| Audio Worker (host) | 0-2 | 1.8 | 1.7 | 1.8 | 1.6 | 2.0 | waits |
| Audio output (host) | 0 | 1.4 | 1.3 | 1.4 | under 1 | under 1 | SwitchAudioSystem |
| main (UI) | 0 | 0.9 | 0.8 | 0.9 | under 1 | under 1 | ImGui |
| XThreadEAE02EA0 | 0-2 | 0.5 | 0.7 | 0.5 | under 1 | 31.7 | sub_826399A8 (native) |
| Four XThreads (hub objects) | 0-2 | - | - | - | 2.3-2.4 each | 2.2-2.4 each | sub_82317068 (machine) |
| **All threads (of 300 %)** | | **237** | **199** | **231** | **130** | **234** | |

In the M71_KDXSE stage the three cores are two-thirds busy (199 of 300 %). The main thread fills one core by itself,
and the frame waits on it: the draw thread drops to 49 % because it has less to submit. A fourth core would not help
there; less work on the main thread would.

## CPU by thread (% of one core; the categories are shares of that thread's samples)

"hand" = hand-written natives, "machine" = machine-optimised copies, "not optimised" = plain recompiled code, the rest
host code (the port's own C++). Three cores = 300 %.

### The M71_KDXSE stage (34 reports, mean 27.4 fps)

| Thread | CPU % | hand | machine | not optimised | renderer | audio (host) | libc | runtime / kernel | app hooks |
|---|---|---|---|---|---|---|---|---|---|
| Main XThread (game logic) | **101.3** | 0.7 | 93.4 | 3.8 | 0 | 0 | 1.0 | 1.1 | 0 |
| XThreadA4C0 (draw submission) | 49.0 | 0.1 | 59.9 | 11.8 | 11.8 | 0 | 3.9 | 10.3 | 2.3 |
| XThread79C0 (the game's audio engine) | 14.2 | 0 | 93.6 | 1.7 | 0 | 0.5 | 0.6 | 3.6 | 0 |
| XMA Decoder | 6.4 | 0 | 0 | 0 | 0 | 40.5 | 5.8 | 53.7 | 0 |
| TimerQueue | 2.7 | 0 | 0 | 0 | 0 | 0 | 6.4 | 93.6 | 0 |
| renderer thread | 1.8 | 0 | 0 | 0 | 83.1 | 2.3 | 5.1 | 9.5 | 0 |
| others | under 2 each | | | | | | | | |

### The tutorial stage (M11_TUTORIAL, 12 reports, mean 45.0 fps)

| Thread | CPU % | hand | machine | not optimised | renderer | audio (host) | libc | runtime / kernel | app hooks |
|---|---|---|---|---|---|---|---|---|---|
| Main XThread | 95.7 | 0.7 | 93.9 | 3.3 | 0 | 0 | 0.7 | 1.4 | 0 |
| XThreadA4C0 (draw) | 87.6 | 0.1 | 60.6 | 11.9 | 11.8 | 0 | 4.0 | 9.3 | 2.3 |
| XThread79C0 (audio engine) | 13.0 | 0 | 95.8 | 1.2 | 0 | 0.2 | 0.2 | 2.7 | 0 |
| XMA Decoder | 3.5 | 0 | 0 | 0 | 0 | 45.1 | 10.7 | 44.3 | 0 |

### The 14:54-14:57 stage (20 reports, mean 37.5 fps)

| Thread | CPU % | hand | machine | not optimised | renderer | audio (host) | libc | runtime / kernel | app hooks |
|---|---|---|---|---|---|---|---|---|---|
| Main XThread | 99.3 | 0.7 | 92.1 | 5.3 | 0 | 0 | 0.7 | 1.2 | 0 |
| XThreadA4C0 (draw) | 72.6 | 0.1 | 60.4 | 12.3 | 11.6 | 0 | 3.7 | 9.5 | 2.4 |
| XThread79C0 (audio engine) | 16.1 | 0 | 94.6 | 1.6 | 0 | 0.4 | 0.4 | 3.0 | 0 |
| XMA Decoder | 8.6 | 0 | 0 | 0 | 0 | 40.9 | 6.3 | 52.8 | 0 |

### Hub (9 reports, 61.5 fps)

| Thread | CPU % | hand | machine | not optimised | renderer | audio (host) | libc | runtime / kernel | app hooks |
|---|---|---|---|---|---|---|---|---|---|
| XThreadA4C0 (draw) | 38.3 | 0.2 | 54.4 | 11.7 | 13.1 | 0 | 5.8 | 11.8 | 3.1 |
| Main XThread | 36.3 | 1.4 | 79.6 | 13.3 | 0.7 | 0 | 0.8 | 4.2 | 0 |
| XThread79C0 (audio engine) | 12.0 | 0 | 94.7 | 1.2 | 0 | 0 | 0.4 | 3.7 | 0 |

### Presents menu after the 6.5-hour sleep (5 reports, 59.2 fps)

| Thread | CPU % | hand | machine | not optimised | renderer | audio (host) | libc | runtime / kernel | app hooks |
|---|---|---|---|---|---|---|---|---|---|
| **TimerQueue** | **74.2** (66-88 in every report) | 0 | 0 | 0 | 0 | 0 | 7.6 | 92.4 | 0 |
| XThreadA4C0 (draw) | 40.1 | 0.4 | 53.9 | 11.1 | 11.3 | 0 | 7.3 | 12.7 | 3.2 |
| Main XThread | 37.7 | 2.2 | 77.5 | 14.7 | 0.4 | 0 | 0.6 | 4.7 | 0 |
| XThread2EA0 | 31.7 | 0 | 71.5 | 0.4 | 0 | 0 | 0.7 | 27.3 | 0 |

## Top functions, the M71_KDXSE stage (share of the thread's samples)

| Thread | Function | Share | Status |
|---|---|---|---|
| Main | sub_82277E70 | 25.1 % | machine (its matrix multiplies are hand natives, inlined) |
| Main | sub_822683D0 | 21.3 % | machine |
| Main | sub_822470D0 | 7.9 % | machine |
| Main | sub_827A5B00 | 4.3 % | machine |
| Main | sub_8227A280 | 3.4 % | machine |
| Main | sub_822772C0 | 2.5 % | machine |
| Main | sub_822889D8 | 2.4 % | machine |
| Main | sub_8278B2B8 | 1.9 % | machine |
| Draw | sub_82738260 | 9.9 % | machine |
| Draw | sub_82746B70 | 5.1 % | machine |
| Draw | sub_82656DF8 | 5.0 % | machine |
| Draw | renut CaptureDelta (draw capture) | 4.8 % | host renderer |
| Draw | sub_82672410 | 4.6 % | machine |
| Draw | sub_826A13D8 | 3.4 % | not optimised (interlocked reference count: no copy) |
| Draw | memcpy | 2.9 % | host libc |
| Draw | rex UpdateGuestClock | 2.9 % | host runtime (lock-free since rc6; still called ~300,000 times a second) |

The same two functions led rc5's first level (17.9 % and 14.3 % of the main thread). In this stage they take
**46 % of a main thread that is 101 % busy**, so they set the frame rate.

## Findings

1. **The M71_KDXSE stage runs at 19-30 fps, held back by the main thread.** sub_82277E70 and sub_822683D0 take 46 %
   of it. Both are machine copies; neither has a hand-written native of its own (sub_82277E70's matrix multiplies do).
   They are the targets for the next hand work.
2. **After a sleep, the timer thread replays every missed tick.** A repeating timer that is late adds one interval
   and fires again (`sdk/src/core/timer_queue.cpp`), so it calls the game's callback for every interval the console
   slept through. After the 20-minute sleep it used 44 % of a core for one report. After the 6.5-hour sleep it used
   66-88 % of a core for all 50 s logged, still catching up. Fix: a timer that is far behind fires once and goes on
   from the present.
3. Stages on three cores: the tutorial 40-46 fps, the 14:54 stage 29-47 fps.
4. Hub, menus and results: 60 fps.
5. The draw thread's UpdateGuestClock is 2.9 % (lock-free since rc6).
