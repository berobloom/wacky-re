"""ratrace.py: trace Watcom 9.5b WCC386's register allocator under the dos-re-suite DOSBox-X
(needs the suite's linear-breakpoint fix: Z0 above 1 MB / in protected mode).

usage: DREA_CONF=$PWD/.work/wcc/wcc.conf ratrace.py FILE.C OUT.txt [NAME:LO-HI,...] [secs]
FILE.C sits in .work/wcc/probe (D:; tools/wcc95/setup.sh first); compiled with -s -3r -oneatx -d1 (-d1: line numbers, same code).
Only events whose conflict list starts in source lines LO..HI are written.
Breaks at (image addresses; linear = image + DELTA):
  0x44ac3 SortConflicts' SortList call: ConfList before the sort (eax)
  0x44a5b AssignConflicts: each conflict in allocation order (ebx)
  0x444dd GiveRegister: GiveBestReg's result (eax) for conflict esi
Conflict: +0 next, +8 name, +0xc start block, +0x14 savings (unsigned), +0x54 state.
Name: +4 class (2 = temp), +0x14 FE symbol?, +0x24 temp id, +0x2d temp flags.
Block +4 -> instruction, +0x20 its source line (-d1).
Registers: 01000003 EAX, 0200000c EBX, 04000030 ECX, 080000c0 EDX.
"""
import os, sys, time, struct
sys.path.insert(0, 'tools')
import dosrun
from dos_re_harness.remote_capture import RspClient
DELTA = 0x9a0000   # linear = image address + DELTA (one flat base for code and data)
BP = {0x44ac3: 'presort', 0x44a5b: 'assign', 0x444dd: 'given',
      0x44176: 'cand', 0x441a3: 'greed', 0x441bb: 'saves'}   # GiveBestReg 0x440b4's candidate loop
REG = {0x01000003: 'EAX', 0x0200000c: 'EBX', 0x04000030: 'ECX', 0x080000c0: 'EDX', 0x00000003: 'AX',
       0x0000000c: 'BX', 0x00000030: 'CX', 0x000000c0: 'DX', 0x10000100: 'ESI', 0x20000200: 'EDI',
       0x00000100: 'SI', 0x00000200: 'DI', 0: '-'}
rn = lambda v: REG.get(v, '%08x' % v)
def main():
    f, outp = sys.argv[1], sys.argv[2]
    # ranges: NAME:LO-HI[,NAME:LO-HI...] or LO-HI
    rng = []
    for item in (sys.argv[3].split(',') if len(sys.argv) > 3 else ['all:0-1073741824']):
        nm, _, lh = item.rpartition(':'); a, b = lh.split('-'); rng.append((nm or 'fn', int(a), int(b)))
    def which(ln):
        for nm, a, b in rng:
            if a <= ln <= b: return nm
        return None
    secs = float(sys.argv[4]) if len(sys.argv) > 4 else 600
    out = open(outp, 'w')
    r = RspClient('127.0.0.1', dosrun.GDB_PORT, secs + 20)
    rd = lambda a, n: r.read_memory(a + DELTA, n)
    d32 = lambda a: struct.unpack('<I', rd(a, 4))[0]
    def line(c):
        try: return struct.unpack('<H', rd(d32(d32(c + 0xc) + 4) + 0x20, 2))[0]
        except Exception: return -1
    def name(nm):
        b = rd(nm, 0x30)
        return 'id%d sym%x cls%d fl%02x' % (struct.unpack_from('<I', b, 0x24)[0], struct.unpack_from('<I', b, 0x14)[0], b[4], b[0x2d])
    r.halt(5.0)
    for a in BP: r.insert_breakpoint(a + DELTA)
    r.continue_nowait()
    outtxt = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '.work', 'wcc', 'probe', 'OUT.TXT')
    if os.path.exists(outtxt): os.remove(outtxt)
    def done():
        try: return b'Code size' in open(outtxt, 'rb').read() or b'rror' in open(outtxt, 'rb').read()
        except OSError: return False
    with dosrun.Dos() as d:
        d.type_line('WCC386 %s -s -3r -oneatx -d1 > OUT.TXT' % f)
    t0 = time.time(); n = 0; cur = -1; on = False
    try:
        while time.time() - t0 < secs:
            try: r.wait_for_stop(5.0)
            except Exception:
                if done(): break                     # the compiler has written its summary
                continue
            g = r.registers(); eip = g['eip'] - DELTA; n += 1
            if eip == 0x44ac3:
                c = g['eax']; cur = line(c) if c else -1
                on = which(cur) is not None
                if on:
                    lst = []
                    while c and len(lst) < 400:
                        lst.append('c%x s%d %s' % (c, d32(c + 0x14), name(d32(c + 8)))); c = d32(c)
                    out.write('PRESORT %s line %d n %d\n' % (which(cur), cur, len(lst)) + ''.join('    %s\n' % x for x in lst))
            elif eip == 0x44a5b and on:
                c = g['ebx']; out.write('ASSIGN c%x s%d st%04x %s\n' % (c, d32(c + 0x14), struct.unpack('<H', rd(c + 0x54, 2))[0], name(d32(c + 8))))
            elif eip == 0x444dd and on:
                out.write('GIVEN  c%x %s\n' % (g['esi'], rn(g['eax'])))
            elif eip == 0x44176 and on:
                c = g['edi']; out.write('  cand c%x %-4s with %s except %s given %s\n' % (c, rn(g['esi']),
                    rn(d32(c + 0x20)), rn(d32(g['ebp'] - 0x24)), rn(d32(0x6ff14))))
            elif eip == 0x441a3 and on:
                out.write('    greedy %d\n' % (g['eax'] & 0xff))
            elif eip == 0x441bb and on:
                out.write('    saves %d\n' % (g['eax'] if g['eax'] < 1 << 31 else g['eax'] - (1 << 32)))
            out.flush()
            dosrun.resume_over(r, g['eip']) if eip in BP else r.continue_nowait()
    finally:
        try: r.halt(5.0)
        except Exception: pass
        for a in BP: r.remove_breakpoint(a + DELTA)
        r.continue_nowait(); r.close()
    print('hits', n, 'seconds %.0f' % (time.time() - t0))
main()
