"""SHC compiler-probe harness for the Sonic Adventure (DC, US 1.005) decomp.

Two comparison modes against disc/1ST_READ.BIN (loaded at 0x8C010000):

1. Function mode (normalised, -code=asmcode + asmsh listing):
     /* @match <func> <start>-<end> */      (hex, end exclusive)
   The compiled function is compared instruction-by-instruction with the original.
   PC-relative loads are compared by the pool *value* (extern symbols named
   foo_8CXXXXXX resolve to 0x8CXXXXXX), BSR by callee address, other branches by
   offset from the function start.  Pool position/padding is NOT compared.

2. Byte-exact mode (-code=machinecode + lnk, like the original toolchain):
     /* @tu <start> */                       (address where this file's P section starts)
   The object is linked with lnk at <start>; every IMPORT named *_XXXXXXXX is
   DEFINEd at 0xXXXXXXXX; the resulting P section (code + pools + alignment padding
   + linker gap up to the 32-byte section boundary) is compared byte for byte.
   Optional range restriction: compare only [rs, re).

Usage:
    py -3.12 shcprobe.py FILE.c [--vers 5.1r03,...] [--flags "..."] [-v] [--exact [--range S-E]]

Environment: SHC_LIB/SHC_TMP are set automatically per version.  Work files go to
%TEMP%\\sadc_compiler_probe (override with PROBE_WORK).
"""
import sys, os, re, subprocess, struct, shutil, tempfile, difflib

HERE = os.path.dirname(os.path.abspath(__file__))
SADC = os.path.abspath(os.path.join(HERE, '..', '..', '..'))
sys.path.insert(0, os.path.join(SADC, 'tools'))
from sh4.isa import decode, format_insn  # noqa: E402

BASE = 0x8C010000
DATA = open(os.path.join(SADC, 'disc', '1ST_READ.BIN'), 'rb').read()
SHCDIR = os.path.join(SADC, 'tools', 'shc')
VERS = ['5.0r26', '5.0r28', '5.0r31', '5.0r32', '5.1r01', '5.1r03', '5.1r04', '5.1r08', '5.1r11', '5.1r13']
ASM_VER = '5.1r13'   # assembler used only for listings in function mode (encoding is version independent)
LNK_VER = '5.1r03'

# Flags determined for SA1 game code (see docs/compiler.md).  -round=zero is the SHC default.
SA1_FLAGS = ('-cpu=sh4 -division=cpu -fpu=single -endian=little -optimize=1 -speed '
             '-macsave=0 -extra=a=1800 -round=zero')

WORK = os.environ.get('PROBE_WORK', os.path.join(tempfile.gettempdir(), 'sadc_compiler_probe'))
os.makedirs(WORK, exist_ok=True)
TMP = os.path.join(WORK, 'tmp')
os.makedirs(TMP, exist_ok=True)


def short(p):
    """8.3 path: SHC 5.x rejects long SHC_TMP values ("Illegal environment variable")."""
    try:
        import ctypes
        buf = ctypes.create_unicode_buffer(1024)
        ctypes.windll.kernel32.GetShortPathNameW(p, buf, 1024)
        return buf.value or p
    except Exception:
        return p


def exe(ver, name):
    b = os.path.join(SHCDIR, ver, 'bin')
    for f in os.listdir(b):
        if f.lower() == name:
            return os.path.join(b, f)
    raise FileNotFoundError('%s not in %s' % (name, b))


def env_for(ver):
    e = dict(os.environ)
    e['SHC_LIB'] = os.path.join(SHCDIR, ver, 'bin')
    e['SHC_TMP'] = short(TMP)
    return e


def _run(cmd, ver):
    # SHC chokes on paths containing '-' or several dots: always run with cwd=WORK and plain names
    return subprocess.run(cmd, env=env_for(ver), capture_output=True, text=True, errors='replace', cwd=WORK)


def errors_of(log):
    return [l for l in log.split('\n') if '(E)' in l or '(F)' in l or '**' in l][:4]


def compile_c(ver, src, flags, out, code='asmcode'):
    po = os.path.join(WORK, out)
    if os.path.exists(po):
        os.remove(po)
    r = _run([exe(ver, 'shc.exe'), src] + flags.split() + ['-code=' + code, '-objectfile=' + out], ver)
    if not os.path.exists(po):
        return None, r.stdout + r.stderr
    if code == 'asmcode':
        return open(po, errors='replace').read(), r.stdout
    return po, r.stdout


# ---------------------------------------------------------------- function mode
def assemble_listing(src, lis):
    pl = os.path.join(WORK, lis)
    if os.path.exists(pl):
        os.remove(pl)
    _run([exe(ASM_VER, 'asmsh.exe'), src, '-cpu=sh4', '-endian=little',
          '-object=' + lis[:-4] + '.obj', '-list=' + lis], ASM_VER)
    if not os.path.exists(pl):
        return None
    return open(pl, errors='replace').read().split('\n')


SYMRE = re.compile(r'([0-9A-Fa-f]{8})$')


def symval(expr):
    """Value of a .DATA operand: sum of H'xxxx / decimal / symbol-with-address-suffix terms."""
    expr = expr.split(';')[0].strip()
    total, syms = 0, []
    for sign, t in re.findall(r"([+-]?)\s*(H'[0-9A-Fa-f]+|\w+)", expr):
        k = -1 if sign == '-' else 1
        if t.startswith("H'"):
            total += k * int(t[2:], 16)
        elif t.isdigit():
            total += k * int(t)
        else:
            m = SYMRE.search(t)
            if m and len(t) > 8:
                total += k * int(m.group(1), 16)
            else:
                syms.append(t)
    if syms:
        return '+'.join(syms) + ('%+d' % total if total else '')
    return total & 0xFFFFFFFF


def compiled_funcs(lines):
    funcs, order, pool, labels = {}, [], {}, {}
    cur, sect = None, None
    for ln in lines:
        if len(ln) < 37 or not ln[:5].strip().isdigit():
            continue
        addr = ln[6:14].strip()
        addr = int(addr, 16) if addr else None
        code = ln[15:31].strip()
        t = ln[37:].strip()
        if t.upper().startswith('.SECTION'):
            sect = t.split()[1].split(',')[0]
            continue
        if sect != 'P':
            continue
        m = re.match(r'^(\w+):\s*;\s*function:\s*(\w+)', t)
        if m:
            cur = m.group(2)
            funcs[cur] = []
            order.append(cur)
            labels[addr] = symval(cur)
            continue
        if re.match(r'^\w+:', t):
            continue
        tu = t.upper()
        if tu.startswith('.DATA.L') or tu.startswith('.DATA.W'):
            pool[addr] = (4 if tu.startswith('.DATA.L') else 2, symval(t.split(None, 1)[1]))
            cur = None
            continue
        if t.startswith('.') or not code:
            continue
        if cur is not None and len(code) == 4:
            funcs[cur].append((addr, int(code, 16)))
    return funcs, order, pool, labels


def fmtv(v):
    return ('%08X' % v) if isinstance(v, int) else str(v)


def norm_seq(words, start, poolget, labelget):
    out = []
    for a, w in words:
        ins = decode(w, a)
        if ins is None:
            out.append('.DATA.W %04X' % w)
            continue
        p = ins.pcrel
        if p:
            out.append('%s =%s,%s' % (ins.mnem, fmtv(poolget(p[2], 4 if p[1] == 0 else p[1])), ins.ops[-1][1]))
            continue
        bt = ins.branch_target
        if bt is not None:
            if ins.mnem == 'BSR':
                out.append('BSR %s' % fmtv(labelget(bt)))
            else:
                out.append('%s +%d' % (ins.mnem, bt - start))
            continue
        out.append(format_insn(ins))
    return out


def orig_norm(start, end):
    words = [(a, struct.unpack_from('<H', DATA, a - BASE)[0]) for a in range(start, end, 2)]

    def pg(addr, size):
        return struct.unpack_from('<I' if size == 4 else '<H', DATA, addr - BASE)[0]
    return norm_seq(words, start, pg, lambda a: a)


def comp_norm(funcs, pool, labels, name):
    words = funcs[name]
    if not words:
        return []

    def pg(addr, size):
        if addr in pool:
            sz, v = pool[addr]
            return (v & 0xFFFF) if (size == 2 and isinstance(v, int)) else v
        return '?%X' % addr
    return norm_seq(words, words[0][0], pg, lambda a: labels.get(a, '?L%X' % a))


def diffcount(a, b):
    sm = difflib.SequenceMatcher(None, a, b, autojunk=False)
    return sum(max(i2 - i1, j2 - j1) for op, i1, i2, j1, j2 in sm.get_opcodes() if op != 'equal')


def show_diff(o, c):
    w = max([len(x) for x in o] + [10])
    for op, i1, i2, j1, j2 in difflib.SequenceMatcher(None, o, c, autojunk=False).get_opcodes():
        for k in range(max(i2 - i1, j2 - j1)):
            x = o[i1 + k] if i1 + k < i2 else ''
            y = c[j1 + k] if j1 + k < j2 else ''
            print('%s %-*s | %s' % ('  ' if op == 'equal' else '!!', w, x, y))


def tagname(cfile):
    return re.sub(r'[^A-Za-z0-9_]', '_', os.path.basename(cfile)[:-2])


def run_functions(cfile, ver, flags):
    """-> ({func: (ndiff, orig, ours)}, log) or (None, log)"""
    src = open(cfile, errors='replace').read()
    specs = [(m.group(1), int(m.group(2), 16), int(m.group(3), 16))
             for m in re.finditer(r'@match\s+(\w+)\s+([0-9A-Fa-f]{8})-([0-9A-Fa-f]{8})', src)]
    tag = tagname(cfile)
    shutil.copy(cfile, os.path.join(WORK, tag + '.c'))
    out = '%s_%s.src' % (tag, ver.replace('.', ''))
    asm, log = compile_c(ver, tag + '.c', flags, out)
    if asm is None:
        return None, log
    lines = assemble_listing(out, out[:-4] + '.lis')
    if lines is None:
        return None, 'asmsh failed'
    funcs, order, pool, labels = compiled_funcs(lines)
    res = {}
    for name, s, e in specs:
        o = orig_norm(s, e)
        if name not in funcs:
            res[name] = (999, o, ['<missing>'])
            continue
        c = comp_norm(funcs, pool, labels, name)
        res[name] = (diffcount(o, c), o, c)
    return res, asm


# ---------------------------------------------------------------- byte-exact mode
def load_elf_segment(d, start):
    phoff = struct.unpack_from('<I', d, 0x1C)[0]
    phentsize, phnum = struct.unpack_from('<HH', d, 0x2A)
    for i in range(phnum):
        _, off, vaddr, _, filesz, _, _, _ = struct.unpack_from('<8I', d, phoff + i * phentsize)
        if vaddr == start:
            return d[off:off + filesz]
    raise ValueError('no segment at %08X' % start)


def build_exact(cfile, ver, flags, start):
    tag = tagname(cfile) + 'x'
    shutil.copy(cfile, os.path.join(WORK, tag + '.c'))
    v = ver.replace('.', '')
    asm, log = compile_c(ver, tag + '.c', flags, '%s_%s.src' % (tag, v))   # for the IMPORT list
    if asm is None:
        return None, log
    imports = re.findall(r'^\s+\.IMPORT\s+(\S+)', asm, re.M)
    obj = '%s_%s.obj' % (tag, v)
    po, log = compile_c(ver, tag + '.c', flags, obj, code='machinecode')
    if po is None:
        return None, log
    defs = []
    for s in imports:
        m = re.search(r'([0-9A-Fa-f]{8})$', s)
        defs.append('define %s(%s)' % (s, m.group(1).upper() if m else '0'))
    elf = '%s_%s.elf' % (tag, v)
    pe = os.path.join(WORK, elf)
    if os.path.exists(pe):
        os.remove(pe)
    sub = ['input %s' % obj, 'output %s' % elf,
           'start P(%08X),C(8E000000),D(8E100000),B(8E200000)' % start, 'elf', 'form a'] + defs + ['exit']
    open(os.path.join(WORK, '%s_%s.sub' % (tag, v)), 'w').write('\n'.join(sub) + '\n')
    r = _run([exe(LNK_VER, 'lnk.exe'), '-sub=%s_%s.sub' % (tag, v)], LNK_VER)
    if not os.path.exists(pe):
        return None, r.stdout
    return load_elf_segment(open(pe, 'rb').read(), start), asm


def compare_bytes(blob, start, show=8):
    orig = DATA[start - BASE:start - BASE + len(blob)]
    bad = [i for i in range(0, len(blob), 2) if blob[i:i + 2] != orig[i:i + 2]]
    lines = []
    for i in bad[:show]:
        a = start + i
        wc, wo = struct.unpack_from('<H', blob, i)[0], struct.unpack_from('<H', orig, i)[0]
        ic, io = decode(wc, a), decode(wo, a)
        lines.append('  %08X  orig %04X %-28s ours %04X %s' % (a, wo, format_insn(io) if io else '-',
                                                              wc, format_insn(ic) if ic else '-'))
    return len(bad), lines


def run_exact(cfile, ver, flags, rng=None, show=8):
    src = open(cfile, errors='replace').read()
    start = int(re.search(r'@tu\s+([0-9A-Fa-f]{8})', src).group(1), 16)
    blob, log = build_exact(cfile, ver, flags, start)
    if blob is None:
        return None, log, []
    if rng:
        rs, re_ = rng
        blob = blob[rs - start:re_ - start]
        start = rs
    nbad, lines = compare_bytes(blob, start, show)
    return (nbad, len(blob)), log, lines


if __name__ == '__main__':
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument('cfile')
    ap.add_argument('--vers', default=','.join(VERS))
    ap.add_argument('--flags', default=SA1_FLAGS)
    ap.add_argument('-v', action='store_true', help='show diffs of non-matching functions')
    ap.add_argument('--exact', action='store_true', help='byte-exact TU mode (needs @tu)')
    ap.add_argument('--range', help='byte-exact: compare only START-END')
    ap.add_argument('-n', type=int, default=8)
    a = ap.parse_args()
    for ver in a.vers.split(','):
        if a.exact:
            rng = [int(x, 16) for x in a.range.split('-')] if a.range else None
            res, log, lines = run_exact(a.cfile, ver, a.flags, rng, a.n)
            if res is None:
                print('%-7s BUILD FAIL %s' % (ver, errors_of(log)))
                continue
            print('%-7s %s %d bytes, %d differing halfwords' % (ver, 'MATCH' if res[0] == 0 else 'DIFF ', res[1], res[0]))
            for l in lines:
                print(l)
            continue
        res, log = run_functions(a.cfile, ver, a.flags)
        if res is None:
            print('%-7s FAIL %s' % (ver, errors_of(log)))
            continue
        tot = sum(v[0] for v in res.values())
        print('%-7s %-5s ' % (ver, 'OK' if tot == 0 else tot) + ' '.join('%s=%d' % (k, v[0]) for k, v in res.items()))
        if a.v:
            for k, (d, o, c) in res.items():
                if d:
                    print('--- %s (orig | ours)' % k)
                    show_diff(o, c)
