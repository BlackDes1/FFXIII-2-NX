#!/usr/bin/env python3
"""ffxiii2-nx (2026-11-05): every Xenos shader container in Final Fantasy XIII-2's own files, for the complete shader
set (as renut-nx's installer dumps all 4,731 of Nuts & Bolts from its files, instead of the ones a run happened to
meet). Final Fantasy XIII-2 keeps its shaders inside zlib streams of the alba_data archives (white_*.x360.bin): each
stream that inflates is scanned too.

A container is what XenosRecomp itself accepts (main.cpp: flags & 0xFFFFFF00 == 0x102A1100, field1C == field20 == 0,
virtual + physical size inside the data). The containers of one source file are written back to back into
--out/scan_<file>.bin (the .bin the build globs as a dependency), a file XenosRecomp scans like the shader dump (it hashes each container with XXH3, the runtime's
key, so one already in the dump adds nothing).

  python tools/ffxiii2_scan_shaders.py --out out/shader_scan [--report] <files or folders...>
"""
import argparse
import os
import re
import struct
import zlib

SIG = b'\x10\x2a\x11'
ZLIB = re.compile(rb'\x78[\x01\x5e\x9c\xda]')
MAX_STREAM_IN = 32 << 20     # compressed bytes tried per stream
MAX_STREAM_OUT = 128 << 20   # inflated bytes kept per stream


def containers(data):
    """(offset, size) of each container in data, XenosRecomp's test."""
    found = []
    at = data.find(SIG)
    while at >= 0:
        if at % 4 == 0 and at + 36 <= len(data):
            flags, vsize, psize, _fc, _ct, _dt, so, f1c, f20 = struct.unpack_from('>9I', data, at)
            size = vsize + psize
            if ((flags & 0xFFFFFF00) == 0x102A1100 and f1c == 0 and f20 == 0 and 36 < size <= len(data) - at
                    and size < (1 << 20) and so < size):
                found.append((at, size))
                at = data.find(SIG, at + size)
                continue
        at = data.find(SIG, at + 1)
    return found


def scan_file(path):
    return scan_data(open(path, 'rb').read())


def scan_data(data):
    """The containers of one file's bytes (and of its zlib streams), and the number of streams that inflated.
    ffxiii2-nx (2026-11-10, GitHub package): split from scan_file so tools/gh/iso_tool.py scans files straight from the
    disc image."""
    blobs = {}
    for at, size in containers(data):
        blobs.setdefault(data[at:at + size], None)
    streams = 0
    end = -1
    for m in ZLIB.finditer(data):
        at = m.start()
        if at < end:
            continue  # inside a stream already inflated
        d = zlib.decompressobj()
        try:
            out = d.decompress(data[at:at + MAX_STREAM_IN], MAX_STREAM_OUT)
        except zlib.error:
            continue
        if len(out) < 64:
            continue
        streams += 1
        used = MAX_STREAM_IN - len(d.unused_data) if d.eof else 0
        if used > 0:
            end = at + used
        for cat, size in containers(out):
            blobs.setdefault(out[cat:cat + size], None)
    return list(blobs), streams


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', required=True)
    ap.add_argument('--report', action='store_true', help='count only')
    ap.add_argument('--jobs', type=int, default=1, help='files scanned at once (default 1)')
    ap.add_argument('paths', nargs='+')
    a = ap.parse_args()
    files = []
    for p in a.paths:
        if os.path.isdir(p):
            for d, _, names in os.walk(p):
                files += [os.path.join(d, n) for n in names]
        else:
            files.append(p)
    if not a.report:
        os.makedirs(a.out, exist_ok=True)
    seen = set()
    total = 0
    files = [f for f in sorted(files) if os.path.getsize(f) >= 64]
    # ffxiii2-nx (2026-11-10, GitHub package): --jobs N scans N files at once (each in its own process; the archives
    # are up to 2 GB, so N is bounded by the memory). The order of the results, and so the output, is the same.
    if a.jobs > 1 and len(files) > 1:
        import concurrent.futures
        pool = concurrent.futures.ProcessPoolExecutor(a.jobs)
        results = pool.map(scan_file, files)
    else:
        pool = None
        results = map(scan_file, files)
    for f, (blobs, streams) in zip(files, results):
        new = [b for b in blobs if b not in seen]
        seen.update(new)
        total += len(new)
        print(f'{len(blobs):6d} containers ({len(new)} new) in {streams} streams: {f}', flush=True)
        if new and not a.report:
            name = 'scan_' + re.sub(r'[^0-9A-Za-z_.-]', '_', os.path.relpath(f)).strip('._') + '.bin'
            with open(os.path.join(a.out, name), 'wb') as o:
                o.write(b''.join(new))
    if pool:
        pool.shutdown()
    print(f'{total} different containers in {len(files)} files')


if __name__ == '__main__':
    main()
