#!/usr/bin/env python3
"""FFXIII-2-Native-Switch P5a (2026-11-09): promoted indirect calls go straight into the target's fast body.

tools/indirect_calls.py turns a call site that (almost) always goes to one target T into
    if (REX_PROMOTED_IS(ctr.u32, 0xTu)) [[likely]] { REX_PROMOTED_CALL(0xT, sub_T); sub_T(ctx, base); } else { ... }
and tools/direct_calls.py makes `sub_T(ctx, base)` `__imp__sub_T(ctx, base)` where T has no hook. On a P1 tree
__imp__sub_T is, for a function with a fast entry, only the thin wrapper
    DEFINE_REX_FUNC(sub_T) { REX_FUNC_PROLOGUE(); ME_ARGS_COUNT(...); ctx.r3.u64 = __fast_sub_T(ctx, base, ctx.r3.u64, ctx.lr); }
This step puts the wrapper's statement in place of the `__imp__sub_T(ctx, base);` call (exactly the wrapper's text:
the caller has exported every argument register and lr to ctx before the call, as for the dispatch), and declares
the fast prototypes the file now uses. One call and one return fewer per promoted call; the compiler sees the
stored ctx values next to the loads.

Only promoted calls already in their `__imp__` form (no hook: direct_calls.py's decision) and only targets whose
wrapper is exactly the one statement above are changed. Run right after tools/direct_calls.py (post_codegen.py, as
patched by tools/build/promote_fast_install.py); the later analyses treat the whole promoted line as an unknown call,
as before. tools/indirect_calls.py writes the line back in its own form on every run, so a re-run starts clean.

  python tools/ffns_promote_fast.py --gen app/generated [--check] [--restore]
REX: switch back = run with --restore (or leave it out of post_codegen.py)."""
import argparse
import os
import re
import sys

DEFINE = re.compile(r'^DEFINE_REX_FUNC\(sub_([0-9A-F]{8})\) \{$')
WRAP_STMT = re.compile(r'^\t((?:ctx\.\w+\.\w+ = )?__fast_sub_([0-9A-F]{8})\(ctx, base(?:, [^;]*)?\);)$')
FAST_DEF = re.compile(r'^(extern "C" \w+ __fast_sub_([0-9A-F]{8})\(PPCContext& __restrict ctx, .*\)) \{$')
PROMOTED_IMP = re.compile(r'REX_PROMOTED_CALL\(0x([0-9A-F]{8}), sub_\1\); (?:__imp__)?sub_\1\(ctx, base\);')
PROMOTED_FAST = re.compile(r'REX_PROMOTED_CALL\(0x([0-9A-F]{8}), sub_\1\); REX_PROMOTED_FAST\(0x\1, sub_\1\); '
                           r'((?:ctx\.\w+\.\w+ = )?__fast_sub_\1\(ctx, base[^;]*\);)')
MARKER = '// FFXIII-2-Native-Switch: promoted calls into fast bodies (tools/ffns_promote_fast.py)'
MACROS = MARKER + '''
// The fast body is the same call only when nothing overrides sub_T (a hook, a machine copy): sub_T is then the weak
// alias of __imp__sub_T. That is checked on the linked Switch ELF (tools/build/check_promote_fast.py), not at run
// time: on the PC (COFF, incremental-link thunks) &sub_T and &__imp__sub_T differ even for an alias (first PC run,
// 2026-11-09: all 26 fast targets "wrong" by an address compare, though none is hooked). A marker only.
#ifndef REX_PROMOTED_FAST
#define REX_PROMOTED_FAST(target, function) ((void)0)
#endif
'''
DECL_BEGIN = '// FFXIII-2-Native-Switch: fast bodies of promoted targets (tools/ffns_promote_fast.py)'
DECL_END = '// end of fast bodies of promoted targets'


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--gen', required=True)
    ap.add_argument('--check', action='store_true')
    ap.add_argument('--restore', action='store_true')
    ap.add_argument('--elf', help='a linked build of this tree: a promoted `sub_T(ctx, base)` call (one direct_calls.py '
                                  'leaves on its hookable name) is taken too when sub_T is the weak alias of '
                                  '__imp__sub_T there (no hook, no machine copy)')
    a = ap.parse_args()
    alias = None
    if a.elf and os.path.exists(a.elf):
        sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'tools', 'console'))
        import elf_sym  # noqa: E402  (tools/console/elf_sym.py of the project)
        addr = {}
        for value, _size, name in elf_sym.symbols(a.elf):
            addr.setdefault(name, value)
        alias = {n[4:] for n in addr if n.startswith('sub_') and addr.get('__imp__' + n) == addr[n]}
    files = sorted(f for f in os.listdir(a.gen) if re.match(r'\w+_recomp\.\d+\.cpp$', f))
    texts = {f: open(os.path.join(a.gen, f), encoding='utf-8').read().split('\n') for f in files}
    wrapper = {}  # T -> statement
    proto = {}  # T -> prototype
    for f in files:
        lines = texts[f]
        for i, line in enumerate(lines):
            m = DEFINE.match(line)
            if m:
                t = m.group(1)
                body = []
                k = i + 1
                while k < len(lines) and lines[k] != '}' and k < i + 8:
                    body.append(lines[k])
                    k += 1
                rest = [b for b in body if b.strip() not in ('REX_FUNC_PROLOGUE();',) and
                        not b.strip().startswith('ME_ARGS_COUNT(')]
                if len(rest) == 1:
                    w = WRAP_STMT.match(rest[0])
                    if w and w.group(2) == t:
                        wrapper[t] = w.group(1)
                continue
            m = FAST_DEF.match(line)
            if m:
                proto[m.group(2)] = m.group(1) + ';'
    changed_files = sites = 0
    for f in files:
        lines = texts[f]
        out = list(lines)
        if DECL_BEGIN in out:
            b, e = out.index(DECL_BEGIN), out.index(DECL_END)
            del out[b:e + 1]
        used = set()
        for i, line in enumerate(out):
            if 'REX_PROMOTED_CALL(' not in line:
                continue
            if a.restore:
                new = PROMOTED_FAST.sub(lambda m: 'REX_PROMOTED_CALL(0x%s, sub_%s); sub_%s(ctx, base);' %
                                        (m.group(1), m.group(1), m.group(1)), line)
            else:
                def fast(m):
                    t = m.group(1)
                    via_imp = '__imp__sub_%s(ctx, base);' % t in m.group(0)
                    if t in wrapper and t in proto and (via_imp or (alias is not None and t in alias)):
                        used.add(t)
                        return 'REX_PROMOTED_CALL(0x%s, sub_%s); REX_PROMOTED_FAST(0x%s, sub_%s); %s' % (
                            t, t, t, t, wrapper[t])
                    return m.group(0)
                new = PROMOTED_IMP.sub(fast, line)
                for m in PROMOTED_FAST.finditer(new):
                    used.add(m.group(1))
            if new != line:
                sites += 1
                out[i] = new
        if used and not a.restore:
            first = next(k for k, l in enumerate(out) if l.startswith('#include'))
            out[first + 1:first + 1] = [DECL_BEGIN] + [proto[t] for t in sorted(used)] + [DECL_END]
        if out != lines:
            changed_files += 1
            if not a.check:
                open(os.path.join(a.gen, f), 'w', encoding='utf-8', newline='\n').write('\n'.join(out))
    pchs = [f for f in os.listdir(a.gen) if f.endswith('_pch.h')]
    if len(pchs) == 1 and not a.restore:
        pch = os.path.join(a.gen, pchs[0])
        text = open(pch, encoding='utf-8').read()
        anchor = '//=============================================================================\n// Flush Mode'
        if anchor not in text:
            sys.exit('ffns_promote_fast: no "Flush Mode" section in %s' % pch)
        if MARKER in text:  # an earlier version of the block (it sits right before the anchor): replaced
            b = text.index(MARKER)
            e = text.index(anchor, b)
            new_text = text[:b] + MACROS + '\n' + text[e:]
        else:
            new_text = text.replace(anchor, MACROS + '\n' + anchor, 1)
        if new_text != text and not a.check:
            open(pch, 'w', encoding='utf-8', newline='\n').write(new_text)
    print('ffns_promote_fast%s%s: %d thin wrappers found, %d promoted calls %s, %d files changed' % (
        ' (check only)' if a.check else '', ' (restore)' if a.restore else '', len(wrapper), sites,
        'put back to __imp__' if a.restore else 'now straight into the fast body', changed_files))


if __name__ == '__main__':
    main()
