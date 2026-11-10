#!/usr/bin/env python3
"""ffxiii2-nx (2026-11-10, GitHub package): the disc image steps of tools/gh/build_switch.ps1, straight from the .iso.

  python tools/gh/iso_tool.py extract <iso> <out folder>                 every file of the disc (the SD card's game_root)
  python tools/gh/iso_tool.py extract <iso> <out folder> --only a b ...  only these files of the disc's root (by name)
  python tools/gh/iso_tool.py scan <iso> --out <folder> [--prefix alba_data]

`scan` is tools/ffxiii2_scan_shaders.py on the files of the image (the same test and the same zlib streams), read from
the .iso one file at a time: the shaders of the game files are found without extracting the 8 GB disc first. Its
output is scan_disc_<path>.bin per file with containers not seen before, as ffxiii2_scan_shaders.py writes them.

Only reads the image. Your own disc image is the only input; nothing from it is part of this repository.
"""
import argparse
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))  # tools/
import extract_iso  # noqa: E402
import ffxiii2_scan_shaders as scan  # noqa: E402


def open_image(path):
    fh = open(path, 'rb')
    base, description = extract_iso.find_partition(fh)
    root_sector, root_size = extract_iso.read_volume_descriptor(fh, base)
    entries = list(extract_iso.walk(fh, base, root_sector, root_size))
    if not entries:
        sys.exit('iso_tool: the file system of %s is empty (not a plain XDVDFS .iso?)' % path)
    print('%s: game partition at 0x%08X (%s), %d entries' % (os.path.basename(path), base, description, len(entries)),
          flush=True)
    return fh, base, entries


def read_entry(fh, base, entry):
    fh.seek(base + entry['sector'] * extract_iso.SECTOR)
    data = fh.read(entry['size'])
    if len(data) != entry['size']:
        sys.exit('iso_tool: unexpected end of the image in %s (incomplete .iso?)' % entry['name'])
    return data


def cmd_extract(a):
    fh, base, entries = open_image(a.iso)
    only = {n.lower() for n in a.only} if a.only else None
    done = 0
    total = 0
    for path, entry in entries:
        if only is not None:
            if entry['is_dir'] or path.lower() not in only:
                continue
        dest = extract_iso._safe_destination(a.out, path)
        if entry['is_dir']:
            os.makedirs(dest, exist_ok=True)
            continue
        if os.path.isfile(dest) and os.path.getsize(dest) == entry['size']:
            done += 1  # already there from an earlier run (same size): kept
            continue
        extract_iso.extract_file(fh, base, entry, dest)
        done += 1
        total += entry['size']
        if done % 50 == 0:
            print('  ... %d files' % done, flush=True)
    if only is not None:
        found = {p.lower() for p, e in entries if not e['is_dir'] and p.lower() in only}
        missing = sorted(only - found)
        if missing:
            sys.exit('iso_tool: not on the disc: ' + ', '.join(missing))
    print('%d files (%.1f GB written) in %s' % (done, total / 1e9, a.out))


def _scan_entry(job):
    """One file of the image, scanned in a worker process (--jobs)."""
    iso, base, entry = job
    with open(iso, 'rb') as fh:
        return scan.scan_data(read_entry(fh, base, entry))


def cmd_scan(a):
    fh, base, entries = open_image(a.iso)
    prefix = a.prefix.strip('/').lower()
    files = [(p, e) for p, e in entries if not e['is_dir'] and e['size'] >= 64
             and (not prefix or p.lower() == prefix or p.lower().startswith(prefix + '/'))]
    os.makedirs(a.out, exist_ok=True)
    seen = set()
    total = 0
    files = sorted(files, key=lambda pe: pe[0])
    if a.jobs > 1 and len(files) > 1:
        import concurrent.futures
        pool = concurrent.futures.ProcessPoolExecutor(a.jobs)
        results = pool.map(_scan_entry, [(a.iso, base, e) for _, e in files])
    else:
        pool = None
        results = (scan.scan_data(read_entry(fh, base, e)) for _, e in files)
    for (path, entry), (blobs, streams) in zip(files, results):
        new = [b for b in blobs if b not in seen]
        seen.update(new)
        total += len(new)
        print(f'{len(blobs):6d} containers ({len(new)} new) in {streams} streams: {path}', flush=True)
        if new:
            name = 'scan_' + re.sub(r'[^0-9A-Za-z_.-]', '_', 'disc/' + path).strip('._') + '.bin'
            with open(os.path.join(a.out, name), 'wb') as o:
                o.write(b''.join(new))
    if pool:
        pool.shutdown()
    print(f'{total} different containers in {len(files)} files')


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    sub = ap.add_subparsers(dest='cmd', required=True)
    e = sub.add_parser('extract')
    e.add_argument('iso')
    e.add_argument('out')
    e.add_argument('--only', nargs='*')
    s = sub.add_parser('scan')
    s.add_argument('iso')
    s.add_argument('--out', required=True)
    s.add_argument('--prefix', default='alba_data')
    s.add_argument('--jobs', type=int, default=1, help='files scanned at once (default 1)')
    a = ap.parse_args()
    try:
        cmd_extract(a) if a.cmd == 'extract' else cmd_scan(a)
    except extract_iso.ExtractError as err:
        sys.exit('iso_tool: %s' % err)


if __name__ == '__main__':
    main()
