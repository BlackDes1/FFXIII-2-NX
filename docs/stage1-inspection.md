# Stage 1: extraction and inspection (Final Fantasy XIII-2, Europe, title 53510806)

Inputs (read only, never copied into the repo):
- `Final Fantasy XIII-2 (Europe).iso`, 8,738,846,720 bytes, XGD3 (partition at 0x02080000), 83 files, 7.63 GiB.
- `tu0000000b_00000000`, LIVE package, content type 0x000B0000, display name "Final Fantasy 13-2 TU 6".

Tools (written for this port, read-only on the inputs): `tools/local/xdvdfs.py`, `tools/local/stfs.py`
(every STFS block checked against its SHA-1 hash entry: 0 mismatches), `tools/local/xexinfo.py`.
Output: `..\game_data\disc`, `..\game_data\tu` (outside the repository).

## Executables

| file | SHA-256 | base | entry | version |
|---|---|---|---|---|
| default.xex (white.exe) | 645745df61ea4466157dda317e0191424ccd47db46ee10f54c79db340dcdfe3b | 0x82000000, image 0xF10000 | 0x82B31D08 | 0.0.0.11 |
| default.xexp (TU6) | 8d48ccf8aabff2da2a1da3e4e4315fcb765007ac0c4d293880874f7464224029 | image 0xF50000 | | 0.0.0.11 -> 0.0.6.11 |
| z0070.xex (z0070.dll) | 294dcedf... | 0x88800000, 0x30000 | 0x88810368 | 0.0.0.11 |
| z0130.xex (z0130.dll) | 272889d8... | 0x88800000, 0x70000 (TU: 0x90000) | 0x88842A70 | 0.0.0.11 |
| z0160.xex (z0160.dll) | 906d9de8... | 0x88800000, 0x30000 | 0x88810368 | 0.0.0.11, no TU patch |
| z0161.xex (z0161.dll) | c483efd1... | 0x88800000, 0x30000 (TU: 0x40000) | 0x88810368 | 0.0.0.11 |

TU patches: default, z0070, z0130, z0161 (.xexp, delta 0.0.0.11 -> 0.0.6.11). No z0160.xexp in TU6.
The TU also carries data patches: white_patch_e / white_patch_scr_e + their filelists.

XDK: 2.0.20871 (D3D9, XAUDIO2, XMP, XONLINE ...), compiler 16.0.11886 (Xenon MSVC). Imports: xboxkrnl.exe, xam.xex.

## Code modules

The four zone DLLs are guest modules loaded at run time (all at the same base 0x88800000, so only one is
resident at a time). They go into the manifest as `[[modules]]`. No other code containers were found on the disc
(the `$SystemUpdate` xex files are the console's system update, not game code).

## Difference from leftisthominid/ffxiii-2-port

That project supports only the USA disc (default.xex SHA-256 73D79C6D...) with TU 0.0.1.13 (tu0000000d).
This copy is the European disc with TU6 (0.0.6.11, tu0000000b). Its function addresses
(`project/analysis/*.toml`) were found for the USA executable and are NOT valid here without re-matching each
entry against this executable (evidence per entry). Its hook code is usable as a reference for what to find.
