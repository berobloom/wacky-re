"""ratrace.py: trace Watcom 9.5b WCC386's register allocator under the dos-re-suite DOSBox-X
(needs the suite's linear-breakpoint fix: Z0 above 1 MB / in protected mode).

usage: DREA_CONF=$PWD/.work/wcc/wcc.conf ratrace.py FILE.C OUT.txt [LO-HI] [secs]
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
import sys, time, struct
sys.path.insert(0, 'tools')
import dosrun
from dos_re_harness.remote_capture import RspClient
DELTA = 0x9a0000   # linear = image address + DELTA (one flat base for code and data)
BP = {0x44ac3: 'presort', 0x44a5b: 'assign', 0x444dd: 'given'}
REG = {0x01000003: 'EAX', 0x0200000c: 'EBX', 0x04000030: 'ECX', 0x080000c0: 'EDX'}
def main():
    f, outp = sys.argv[1], sys.argv[2]
    lo, hi = (int(x) for x in sys.argv[3].split('-')) if len(sys.argv) > 3 else (0, 1 << 30)
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
    with dosrun.Dos() as d:
        d.type_line('WCC386 %s -s -3r -oneatx -d1 > OUT.TXT' % f)
    t0 = time.time(); n = 0; cur = -1
    try:
        while time.time() - t0 < secs:
            try: r.wait_for_stop(max(1.0, secs - (time.time() - t0)))
            except Exception: break
            g = r.registers(); eip = g['eip'] - DELTA; n += 1
            if eip == 0x44ac3:
                c = g['eax']; cur = line(c) if c else -1
                if lo <= cur <= hi:
                    lst = []
                    while c and len(lst) < 400:
                        lst.append('c%x s%d %s' % (c, d32(c + 0x14), name(d32(c + 8)))); c = d32(c)
                    out.write('PRESORT line %d n %d\n' % (cur, len(lst)) + ''.join('    %s\n' % x for x in lst))
            elif eip == 0x44a5b and lo <= cur <= hi:
                c = g['ebx']; out.write('ASSIGN c%x s%d st%04x %s\n' % (c, d32(c + 0x14), struct.unpack('<H', rd(c + 0x54, 2))[0], name(d32(c + 8))))
            elif eip == 0x444dd and lo <= cur <= hi:
                out.write('GIVEN  c%x %s\n' % (g['esi'], REG.get(g['eax'], '%08x' % g['eax'])))
            out.flush()
            dosrun.resume_over(r, g['eip']) if eip in BP else r.continue_nowait()
    finally:
        try: r.halt(5.0)
        except Exception: pass
        for a in BP: r.remove_breakpoint(a + DELTA)
        r.continue_nowait(); r.close()
    print('hits', n, 'seconds %.0f' % (time.time() - t0))
main()
