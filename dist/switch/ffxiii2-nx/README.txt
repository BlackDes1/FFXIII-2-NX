Final Fantasy XIII-2 for the Nintendo Switch (ffxiii2-nx): a static recompilation of the Xbox 360 game (EU, title
update 6) with ReXGlue, rendered by reNut's renderer on deko3d. A homebrew NRO for the Homebrew Menu (Atmosphere).
No game data is included: you need your own copy of the game.

CREDITS
  reNut's renderer (masterspike52; its author permits reuse), ReXGlue, banjo-nx, katamari-nx (this port's base),
  renut-nx, sa2-nx, NetherSX2_nx, battd_nx, bejeweled3-nx and the zuma ports. GPL-3.0 where those projects require it;
  the source of this build is the source_<commit>.tgz next to this file.

INSTALL
  1. Copy the folder COPY_TO_SD_switch_ffxiii2-nx to the SD card as sdmc:/switch/ffxiii2-nx
     (so the NRO is sdmc:/switch/ffxiii2-nx/ffxiii2.nro).
  2. Extract your own disc files into sdmc:/switch/ffxiii2-nx/game_root (default.xex and the data next to it).
  3. Put title update 6 (default.xexp and its data) into sdmc:/switch/ffxiii2-nx/update.
  4. Start it from the Homebrew Menu in title-takeover mode (hold R while starting a game), not from the album applet:
     the game needs the full memory.

THE TOML
  The NRO reads one file: sdmc:/switch/ffxiii2-nx/ffxiii2.toml. Everything else is built in. The configs folder holds
  the variants; copy one over ffxiii2.toml:
    renut.toml          the test settings, logs on (the default ffxiii2.toml of this package)
    renut_play.toml     for playing: logs off
    renut_ab.toml       the in-run performance A/B of the newest change (below)
    renut_ab_control.toml  the A/B control run (switches nothing)
    renut_profile.toml  the profiling run (below)
  Nothing in these changes the picture: the default picture matches the PC build. Options that change the picture
  will come as separate files in configs, each for your decision.

WHAT TO SEND BACK AFTER A TEST
  sdmc:/switch/ffxiii2-nx/logs/renut_*.log (the newest), and sdmc:/switch/rex_crash.log if it exists.
  Every 10 s the log has a "[renut] ... fps, render thread busy ... ms, GPU ... ms" line.

PERFORMANCE A/B (renut_ab.toml)
  The console is the judge of performance, not the PC or an emulator. With renut_ab.toml the part named by
  banjo_renut_ab_target is switched off and on every 30 s (banjo_renut_ab_s) during one run; the log's "[renut A/B]"
  line gives both halves under the same scenes.
  - The CONTROL run (renut_ab_control.toml): banjo_renut_ab_target = 10 switches nothing. Play 4-5 minutes (title, a field, a battle).
    Its two halves should be equal; their gap is the noise any real A/B has to beat.
  - Then one run per part (renut_ab.toml, set to the newest change): banjo_renut_ab_target = 19 is the polling
    loops' backoff (rc2); others: 1 precise barriers, 2 the render thread's pin, 3 shared-constants reuse, 5 capture
    pool, 6 exact streams, ... (the key's help text). Play the same stretch for the same time, send the log.
  A part that comes out slower on the console is switched off in the next package.

PROFILING (renut_profile.toml, for the hot functions and the PGO build)
  It turns on the stack sampler (1 kHz, every thread). Play 5-10 minutes of the usual stretch (title, a field with
  effects, a battle), then send sdmc:/switch/rex_profile.log, the log, and say which package it was (the ELF in this
  package maps the samples to functions). The sampler costs frame rate while it runs; use it for profiling only.
  This profile decides which functions get hand-written native versions (the first candidate: the effect thread's
  spin barrier, sub_828B8310) and is the input of the PGO/LTO build (docs/pgo-lto.md), which comes after it.
