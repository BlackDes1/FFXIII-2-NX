import bisect, glob, json, os, re, struct
class Mod:
    def __init__(self, img_path, base, gen):
        self.img = open(img_path, 'rb').read(); self.base = base; self.gen = gen
        img = self.img
        pe = struct.unpack_from("<I", img, 0x3C)[0]; nsec = struct.unpack_from("<H", img, pe + 6)[0]; opt = struct.unpack_from("<H", img, pe + 20)[0]
        self.pe = pe; self.optoff = pe + 24
        self.secs = []; s = pe + 24 + opt
        for i in range(nsec):
            name = img[s:s + 8].split(b"\0")[0].decode(); vs, va = struct.unpack_from("<II", img, s + 8); ch = struct.unpack_from("<I", img, s + 36)[0]
            self.secs.append((name, base + va, base + va + vs, ch)); s += 40
        self.code = [(n, lo, hi) for n, lo, hi, ch in self.secs if ch & 0x20000000]
        reg = open(glob.glob(os.path.join(gen, '*_register.cpp'))[0]).read()
        self.reg = {int(m.group(1), 16): m.group(2) for m in re.finditer(r'SetFunction\(0x([0-9A-F]{8}), (\w+)\)', reg)}
        byname = {v: k for k, v in self.reg.items()}
        # emitted instruction addresses per function
        self.emit = {}; self.labels = {}; self.swt = {}
        for f in sorted(glob.glob(os.path.join(gen, "*_recomp.*.cpp"))):
            cur = None; pc = None; insw = False
            for line in open(f):
                if line.startswith("DEFINE_REX_FUNC("):
                    nm = line[16:line.index(")")]
                    cur = byname.get(nm)
                    if cur is None and nm.startswith('sub_'): cur = int(nm[4:12], 16)
                    pc = cur; self.emit[cur] = []; self.labels[cur] = set(); self.swt[cur] = set(); insw = False
                elif cur is None: continue
                elif line.startswith("loc_"):
                    pc = int(line[4:12], 16); self.labels[cur].add(pc)
                elif line.startswith("\t// "):
                    self.emit[cur].append(pc); pc += 4
                elif line.startswith("\tswitch ("): insw = True
                elif insw and line.startswith("\t\tgoto loc_"):
                    self.swt[cur].add(int(line[11:19], 16))
                elif insw and line.startswith("\t}"): insw = False
        self.pdata = {}
        for n, lo, hi, ch in self.secs:
            if n == '.pdata':
                for o in range(lo, hi - 7, 8):
                    b, w = struct.unpack_from(">II", img, o - base)
                    if b: self.pdata[b] = (w >> 8) & 0x3FFFFF
        self.end = {}
        for a in self.reg:
            e = max(self.emit.get(a, [a - 4]) or [a - 4]) + 4
            if a in self.pdata: e = max(e, a + self.pdata[a] * 4)
            if a not in self.emit: e = a + 16  # import thunk
            self.end[a] = e
        self.starts = sorted(self.reg)
        self.emitset = set(x for v in self.emit.values() for x in v)
    def w(self, a): return struct.unpack_from(">I", self.img, a - self.base)[0]
    def in_code(self, x): return any(lo <= x < hi for _, lo, hi in self.code)
    def sec_of(self, x):
        for n, lo, hi, ch in self.secs:
            if lo <= x < hi: return n
    def owners(self, x):
        """functions whose [start,end) contains x"""
        i = bisect.bisect_right(self.starts, x) - 1; out = []
        j = i
        while j >= 0 and x - self.starts[j] < 0x40000:
            s = self.starts[j]
            if s <= x < self.end[s]: out.append(s)
            j -= 1
            if len(out) and j < i - 50: break
        return out
    def relocs(self):
        out = []
        for n, lo, hi, ch in self.secs:
            if n != '.reloc': continue
            p = lo
            hi = min(hi, self.base + len(self.img))
            while p < hi - 8:
                rva, sz = struct.unpack_from("<II", self.img, p - self.base)
                if sz < 8: break
                ents = struct.unpack_from("<%dH" % ((sz - 8) // 2), self.img, p + 8 - self.base)
                k = 0
                while k < len(ents):
                    t, off = ents[k] >> 12, ents[k] & 0xFFF
                    out.append((t, self.base + rva + off))
                    if t == 4: k += 1
                    k += 1
                p += sz
        return out
