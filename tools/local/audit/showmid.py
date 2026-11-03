import sys, pickle, collections
mids, gaps = pickle.load(open(sys.argv[1], 'rb'))
mode = sys.argv[2] if len(sys.argv) > 2 else 'all'
for x, f, em, lab, sw, ev in mids:
    kinds = collections.Counter(e[0] for e in ev)
    if mode == 'nosw' and sw: continue
    print(f"0x{x:08X} in 0x{f:08X} emitted={int(em)} label={int(lab)} switch={int(sw)} refs={dict(kinds)} first={[(e[0], hex(e[1])) for e in ev[:3]]}")
