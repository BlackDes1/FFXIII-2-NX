# ffxiii2-nx: game-specific fixes

Every fix this game needed on the katamari-nx base, one file each, with its evidence in the file and in
docs/ffxiii2-nx.md.

| File | What | Evidence |
|---|---|---|
| `ffxiii2_irq.cpp` | The graphics interrupt handler sub_828510C8 runs without the runtime's global mutex | stage 3 (earlier tree): freeze after the first button press in 2 of 6 PC runs, 0 of 6 with the fix |
| SDK `src/kernel/xam/xam_user.cpp` | XamUserWriteProfileSettings stores INT32/INT64/FLOAT/DOUBLE/DATETIME settings | stage 3: "Unimplemented data type 1" and the settings lost |
| SDK `src/core/guest_memory_switch.cpp` | `switch_guest_views_eager` (Eden delivers no data aborts) | Eden runs 6-8 of the earlier tree |
| `readback_memexport = false` (PC run options / built-in config) | no GPU readback per memexport draw | stage 3: a ~100 s frozen loading screen without it |
