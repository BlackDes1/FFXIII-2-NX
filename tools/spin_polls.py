#!/usr/bin/env python3
"""ffxiii2-nx (2026-11-05): a backoff in the generated code's pure polling loops (post-codegen step, the last one).

A pure polling loop re-reads one guest word and branches back until it changes, nothing else:

    loc_828B8364:
        REX_LOOP_BARRIER();
        // lwz r11,0(r9)
        l_r11.u64 = REX_LOAD_D_U32(l_r9.u32, 0);
        // cmplwi cr6,r11,0
        cr6.compare<uint32_t>(l_r11.u32, 0, xer);
        // bne cr6,0x828b8364
        if (!cr6.eq) goto loc_828B8364;

On the Xbox 360 another hardware thread changes the word; on the Switch the waiting thread holds one of the game's
three cores while it waits, often the core the thread it waits for needs (the console run of rc1: the four effect
workers at ~240 % CPU in such loops, the main thread at 14 %, 6-7 fps). This step adds REX_SPIN_POLL(); after the
loop's REX_LOOP_BARRIER(); (tools/loop_barriers.py), which calls rex_spin_poll() (src/ffxiii2/ffxiii2_spin_poll.cpp:
the CPU's yield hint, then a core yield, then short sleeps). The loop's own statements are untouched: it reads the
word again every iteration and leaves exactly when it did; same values, same order of guest accesses.

A loop qualifies only when every line between the barrier and its one backward goto is a comment, exactly one guest
load into a register (REX_LOAD_*), register arithmetic without REX_ macros or calls, or a compare, and the goto is
the loop's last line, and no register of the load's address is assigned in the loop (an advancing pointer is a
scan, a pointer loaded from the word a list walk: both make progress and stay as they are). Loops with stores, calls, reservations (lwarx/stwcx.) or more than one load are left alone.

The pch gets the macro (REX_SPIN_POLL_OFF compiles it to nothing). Idempotent.

  python tools/spin_polls.py --gen app/generated [--check]
"""
import argparse
import os
import re
import sys

LABEL = re.compile(r'^(loc_[0-9A-F]{8}):\s*$')
BARRIER = 'REX_LOOP_BARRIER();'
POLL = '\tREX_SPIN_POLL();'
LOAD = re.compile(r'^\t[\w.]+ = REX_LOAD_[A-Z0-9_]+\([^;]*\);$')
COMPARE = re.compile(r'^\tcr\d\.compare<\w+>\([^;]*\);$')
ARITH = re.compile(r'^\t[\w.]+ = [\w.\s&|^+\-~<>()?:=!]+;$')
# FFXIII-2-Native-Switch (2026-11-08): registers that are plain C++ locals (non_argument_as_local / args_in_registers:
# `r11`, `r3`) count too; without them a scanning loop's advancing pointer was not seen and strlen-like loops were
# marked as pure polling (159 instead of 25 loops in the P1a codegen).
REG = re.compile(r'\b((?:ll_|l_|ctx\.)?r\d+)\b')
GOTO = re.compile(r'^\tif \([^;]*\) goto (loc_[0-9A-F]{8});$')

PCH_MARKER = '// ffxiii2-nx: the backoff of the guest polling loops (tools/spin_polls.py)'
PCH_BLOCK = PCH_MARKER + '''
#ifndef REX_SPIN_POLL_OFF
extern "C" void rex_spin_poll(void);
#define REX_SPIN_POLL() rex_spin_poll()
#else
#define REX_SPIN_POLL() ((void)0)
#endif
'''


def target(lhs):
    """The register (or other variable) a statement assigns: l_r11.u64 -> l_r11, ea -> ea."""
    m = REG.match(lhs)
    return m.group(1) if m else lhs.split('.')[0]


def poll_loop(lines, i, label):
    """lines[i] is the label; True when lines[i+1:] is a pure polling loop back to it."""
    if i + 1 >= len(lines) or lines[i + 1].strip() != BARRIER:
        return False
    loads = 0
    assigned, address, ea_regs = set(), set(), set()
    j = i + 2
    if j < len(lines) and lines[j] == POLL:
        j += 1
    while j < len(lines):
        line = lines[j]
        if line.lstrip().startswith('//'):
            j += 1
            continue
        m = GOTO.match(line)
        if m:
            # A poll re-reads the same address: no register of the load's address is assigned in the loop (a pointer
            # that advances is a scan, a pointer loaded from the word is a list walk: both make progress).
            return m.group(1) == label and loads == 1 and not (assigned & address)
        if LOAD.match(line):
            loads += 1
            lhs, rhs = line.strip().split(' = ', 1)
            assigned.add(target(lhs))
            address |= set(REG.findall(rhs))
            if re.search(r'\bea\b', rhs):
                address |= ea_regs | {'ea'}
        elif COMPARE.match(line):
            pass
        elif ARITH.match(line) and 'REX_' not in line and '__imp' not in line and 'base' not in line:
            lhs, rhs = line.strip().split(' = ', 1)
            assigned.add(target(lhs))
            if target(lhs) == 'ea':
                ea_regs |= set(REG.findall(rhs))
        else:
            return False
        j += 1
    return False


def process(path, check):
    text = open(path, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in text else '\n'
    lines = text.split(nl)
    out, found, added = [], 0, 0
    i = 0
    while i < len(lines):
        out.append(lines[i])
        m = LABEL.match(lines[i])
        if m and poll_loop(lines, i, m.group(1)):
            found += 1
            out.append(lines[i + 1])  # the barrier
            if i + 2 < len(lines) and lines[i + 2] == POLL:
                out.append(lines[i + 2])
                i += 3
            else:
                out.append(POLL)
                added += 1
                i += 2
            continue
        i += 1
    if added and not check:
        open(path, 'w', encoding='utf-8', newline='').write(nl.join(out))
    return found, added


def patch_pch(gen, check):
    for name in os.listdir(gen):
        if not name.endswith('_pch.h'):
            continue
        path = os.path.join(gen, name)
        text = open(path, encoding='utf-8', newline='').read()
        if PCH_MARKER in text:
            continue
        anchor = text.find('// sa2-nx optimised codegen: loop barrier (tools/loop_barriers.py)')
        if anchor < 0:
            sys.exit(f'ERROR (post-codegen patch): spin_polls: no loop barrier block (tools/loop_barriers.py) in {path}')
        if check:
            print(f'spin_polls: {path} lacks the REX_SPIN_POLL block (check only)')
            continue
        nl = '\r\n' if '\r\n' in text else '\n'
        # Before the loop barrier's block, at the top level of the pch.
        text = text[:anchor] + PCH_BLOCK.replace('\n', nl) + text[anchor:]
        open(path, 'w', encoding='utf-8', newline='').write(text)
        print(f'spin_polls: REX_SPIN_POLL defined in {path}')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--gen', required=True)
    ap.add_argument('--check', action='store_true')
    a = ap.parse_args()
    patch_pch(a.gen, a.check)
    total = added = files = 0
    for folder in (a.gen, os.path.join(a.gen, 'hot')):
        if not os.path.isdir(folder):
            continue
        for name in sorted(os.listdir(folder)):
            if name.endswith(('.cpp', '.h', '.inc')) and not name.endswith('_pch.h'):
                f, n = process(os.path.join(folder, name), a.check)
                total += f
                added += n
                files += bool(f)
    print(f'spin_polls: {total} pure polling loops in {files} files ({added} marked now){" (check only)" if a.check else ""}')


if __name__ == '__main__':
    main()
