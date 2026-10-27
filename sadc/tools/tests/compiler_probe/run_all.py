"""Run every compiler-probe test against every SHC version and print a summary.

    py -3.12 run_all.py            (takes ~1-2 minutes)

See docs/compiler.md for the interpretation.
"""
import os, re, sys, struct, subprocess
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import shcprobe as P

V = P.VERS
F = P.SA1_FLAGS


def row(name, cells):
    print('%-34s %s' % (name, ' '.join('%-7s' % c for c in cells)))


def function_tests():
    print('\n== function mode (instruction diff count per function; 0 = match), flags: %s' % F)
    row('test / function', V)
    for cf in ['t01_gamemode_getters.c', 't02_gamestat.c', 't03_vec_copy.c', 't04_stage_tu_bytematch.c',
               't05_misc_leaf.c', 't06_calls.c', 't07_setcolor_tu.c', 't08_near_misses.c']:
        path = os.path.join(HERE, cf)
        per = {}
        for v in V:
            res, log = P.run_functions(path, v, F)
            per[v] = res
        names = []
        for v in V:
            if per[v]:
                names = list(per[v].keys())
                break
        for n in names:
            row('%s:%s' % (cf[:3], n), [('-' if not per[v] else ('OK' if per[v][n][0] == 0 else str(per[v][n][0]))) for v in V])


def exact_tests():
    print('\n== byte-exact mode (differing halfwords; 0 = byte identical incl. pool, padding, link gap)')
    row('test', V)
    cases = [('t04_stage_tu_bytematch.c', F, None, 't04 SA1 flags'),
             ('t04_stage_tu_bytematch.c', F.replace(' -speed', ''), None, 't04 without -speed'),
             ('t04_stage_tu_bytematch.c', F.replace(' -extra=a=1800', ''), None, 't04 without -extra=a=1800'),
             ('t07_setcolor_tu.c', F, (0x8C02E820, 0x8C02E886), 't07 SetColor only'),
             ('t07_setcolor_tu.c', F, (0x8C02E936, 0x8C02E970), 't07 pad+pool (Disp1 2B short!)')]
    for cf, fl, rng, label in cases:
        cells = []
        for v in V:
            res, log, lines = P.run_exact(os.path.join(HERE, cf), v, fl, rng, 0)
            cells.append('-' if res is None else ('MATCH' if res[0] == 0 else str(res[0])))
        row(label, cells)


def discriminators():
    print('\n== discriminators')
    import shutil
    # d01
    cells = []
    for v in V:
        shutil.copy(os.path.join(HERE, 'd01_ushort_shift.c'), os.path.join(P.WORK, 'd01.c'))
        asm, log = P.compile_c(v, 'd01.c', F, 'd01_%s.src' % v.replace('.', ''))
        cells.append('-' if asm is None else ('NOT' if re.search(r'^\s+NOT\s', asm, re.M) else 'plain'))
    row('d01 ushort>>8 signed-shift seq', cells)
    # d02
    for extra in ('', ' -aggressive=2'):
        cells = []
        for v in V:
            shutil.copy(os.path.join(HERE, 'd02_fdiv_const.c'), os.path.join(P.WORK, 'd02.c'))
            asm, log = P.compile_c(v, 'd02.c', F + extra, 'd02_%s.src' % v.replace('.', ''))
            cells.append('-' if asm is None else ('FDIV' if 'FDIV' in asm else 'FMUL'))
        row('d02 a/8.0f%s' % extra, cells)
    # d03: padding produced by -code=machinecode objects linked with lnk
    cells_in, cells_gap = [], []
    for v in V:
        objs = []
        ok = True
        for f in ('d03_pad_a', 'd03_pad_b'):
            shutil.copy(os.path.join(HERE, f + '.c'), os.path.join(P.WORK, f + '.c'))
            po, log = P.compile_c(v, f + '.c', F, '%s_%s.obj' % (f, v.replace('.', '')), code='machinecode')
            if po is None:
                ok = False
            objs.append(os.path.basename(str(po)))
        if not ok:
            cells_in.append('-')
            cells_gap.append('-')
            continue
        elf = 'd03_%s.elf' % v.replace('.', '')
        pe = os.path.join(P.WORK, elf)
        if os.path.exists(pe):
            os.remove(pe)
        sub = ['input %s' % objs[0], 'input %s' % objs[1], 'output %s' % elf, 'start P(8C010000)', 'elf', 'form a',
               'define _ext_8C6B7078(8C6B7078)', 'exit']
        open(os.path.join(P.WORK, 'd03.sub'), 'w').write('\n'.join(sub) + '\n')
        P._run([P.exe(P.LNK_VER, 'lnk.exe'), '-sub=d03.sub'], P.LNK_VER)
        blob = P.load_elf_segment(open(pe, 'rb').read(), 0x8C010000)
        cells_in.append(blob[0x18:0x1A].hex())     # .ALIGN 32 fill before object A's pool
        cells_gap.append(blob[0x24:0x26].hex())    # gap between object A and object B
    row('d03 .ALIGN fill (machinecode)', cells_in)
    row('d03 inter-object gap (lnk)', cells_gap)


if __name__ == '__main__':
    function_tests()
    exact_tests()
    discriminators()
