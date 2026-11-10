#!/usr/bin/env python3
"""ffxiii2-nx (2026-11-04): the PC profile's part of the machine track (docs/optimising-functions.md, "Every opportunity":
"Any PC profile: add what it names to localized_bulk.txt"). The same filters as tools/switch_hot_bulk.py --write --merge
(tiny leaves, refused forms, addresses an app or SDK source mentions, threaded dispatch, already a native), applied to
the guest functions of a PC profile (logs/pc_profile.log of banjo_pc_profile_s, read as tools/pc_hot_functions.py reads
it), merged into the bulk list. A PC profile qualifies machine copies only, never hand-written natives.

  python tools/pc_hot_bulk.py PC_PROFILE [--min-samples 20] [--write app/src/cpu/hot/localized_bulk.txt]
"""
import argparse
import collections
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

LINE = re.compile(r'^\s+[\d.]+%\s+\S*?(sub_[0-9A-F]{8}) x(\d+)$')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('profile')
    ap.add_argument('--min-samples', type=int, default=20, help='PC samples (1 kHz, all windows, all threads)')
    ap.add_argument('--tiny', type=int, default=12)
    ap.add_argument('--write', default=os.path.join(ROOT, 'app', 'src', 'cpu', 'hot', 'localized_bulk.txt'))
    a = ap.parse_args()

    # Per-thread blocks only (the "all threads" block at the top of each window repeats them).
    per_func = collections.Counter()
    in_thread = False
    for line in open(a.profile, encoding='utf-8', errors='replace'):
        if line.startswith('thread '):
            in_thread = True
            continue
        if line.startswith('=='):
            in_thread = False
            continue
        m = LINE.match(line.rstrip('\n'))
        if m and in_thread:
            per_func[m.group(1)] += int(m.group(2))

    import localize_native as ln
    import store_cards
    index = ln.Index(ln.GEN)
    listed = [e[0] for e in ln.list_entries(ln.LIST)]
    hand = ln.hand_entries()
    hooks = ln.Hooks(listed)
    sizes = ln.addresses(ln.GEN)

    def raw_ok(sub):
        for _, l in index.body(sub):
            l = store_cards.restore_text(l)
            if 'REX_RAW_ADDR(' in l and not (ln.RAW_VECTOR_STORE.match(l) or ln.RAW_MEMSET.match(l) or
                                             ln.RAW_LOAD.match(l)):
                return False
        return True

    post = open(os.path.join(ROOT, 'tools', 'post_codegen.py'), encoding='utf-8').read()
    threaded = set(re.findall(r'sub_[0-9A-F]{8}', post[post.find("tool('switch_goto.py'), '--gen', gen, '--thread'"):][:300]))
    kept = open(a.write, encoding='utf-8').read().split('\n') if os.path.isfile(a.write) else []
    have = {m.group(1).upper() for l in kept for m in [re.match(r'\s*([0-9A-Fa-f]{8})\b', l)] if m}
    rows, left = [], collections.Counter()
    for sub, n in per_func.most_common():
        if n < a.min_samples:
            break
        f = index.funcs.get(sub)
        if sub[4:].upper() in have:
            continue
        if f is None:
            why = 'not a generated function'
        elif sub in listed or sub in hand:
            why = 'already a native'
        elif not f['callees'] and not f['unknown'] and sizes.get(sub, (0, 4 * (a.tiny + 1)))[1] // 4 <= a.tiny:
            why = 'tiny leaf'
        elif hooks.replaced(sub):
            why = 'an app or SDK source mentions it'
        elif sub in threaded:
            why = 'threaded dispatch (switch_goto.py)'
        else:
            why = ln.fits(index, hooks, sub)
            if why == 'raw guest access' and raw_ok(sub):
                why = None
        if why:
            left[why.split(':')[0][:40]] += 1
            continue
        rows.append((sub, n, sizes.get(sub, (0, 0))[1] // 4, len(f['callees'])))
    lines = [l for l in kept if l.strip()]
    rel = os.path.relpath(a.profile, ROOT).replace('\\', '/')
    for sub, n, ins, calls in rows:
        lines.append(f'{sub[4:]} {n} PC samples ({rel}), {ins} instructions, {calls} direct callees')
    open(a.write, 'w', encoding='utf-8', newline='\n').write('\n'.join(lines) + '\n')
    print(f'{len(rows)} functions added to {a.write}; left out: ' +
          ', '.join(f'{k} {v}' for k, v in left.most_common()))


if __name__ == '__main__':
    main()
