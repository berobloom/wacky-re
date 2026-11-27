"""slottrace.py: trace Watcom 9.5b WCC386's stack-slot assignment (AssignOtherLocals) under the
dos-re-suite DOSBox-X, per compiled function.

usage: DREA_CONF=$PWD/.work/wcc/wcc.conf slottrace.py FILE.C OUT.txt [secs]
FILE.C sits in .work/wcc/probe (D:; tools/wcc95/setup.sh first); compiled with -s -3r -oneatx -d1.

Breaks (image addresses; linear = image + DELTA), findings/compiler.md "Stack slots":
  0x3e674 AssignOtherLocals' SortList call: Names[N_TEMP] before the sort (eax), TempAllocBefore 0x3e716
  0x3e679 after the sort (eax = sorted head)
  0x3e712 after the AllocNewLocal loop: every temp's location
  0x3e7f3 AllocNewLocal(eax): the order temps get memory, with the caller (also the calls before the sort:
          those after `END n` belong to function n+1)
  0x457aa AssignPushLocals (0x456d6, before AssignOtherLocals): a leading `mov reg -> temp` of the first
          block makes the temp a push local, the next slot from the top (CurrProc+0xc); 0x457ce: where it stops
  0x2dfa4/0x2dfa9 RoughSortTemps' SortList (AllocBefore 0x2dec2), 0x46220/0x46225 the id sort (0x46208):
          the list before (RPRE) and after (RPOST); a name with a conflict shows its savings (s)
  0x44ac3 SortConflicts (a register-allocation pass starts): RASORT n
  0x4580a SetTempLocation(eax name, edx size): every location handed out, with the caller
Name: +0 next, +8 size, +0x10 v.offset, +0x14 FE symbol, +0x18 usage (0x80 NEEDS_MEMORY, 0x40 HAS_MEMORY,
0x08 USE_ADDRESS, 0x02 USE_IN_ANOTHER_BLOCK), +0x20 t.location, +0x24 t.v.id, +0x28 blk_id (word),
+0x2c temp_flags (2 ALIAS).  Each pass is written as  PASS n  then PRE / POST / LOC lists, one temp a line:
  name-address size usage flags id sym location
"""
import os, sys, time, struct
sys.path.insert(0, 'tools')
import dosrun
from dos_re_harness.remote_capture import RspClient
DELTA = 0x9a0000
BP = (0x3e674, 0x3e679, 0x3e712, 0x3e7f3, 0x4580a, 0x457aa, 0x457ce, 0x44ac3, 0x2dfa4, 0x2dfa9, 0x46220, 0x46225)


def main():
    f, outp = sys.argv[1], sys.argv[2]
    secs = float(sys.argv[3]) if len(sys.argv) > 3 else 600
    out = open(outp, 'w')
    r = RspClient('127.0.0.1', dosrun.GDB_PORT, secs + 20)
    rd = lambda a, n: r.read_memory(a + DELTA, n)
    d32 = lambda a: struct.unpack('<I', rd(a, 4))[0]

    def one(nm):
        b = rd(nm, 0x30)
        u = lambda o: struct.unpack_from('<I', b, o)[0]
        loc = u(0x20)
        c = u(0xc)
        sv = (' s%d' % d32(c + 0x14)) if c else ''
        return 'n%x sz%d us%02x fl%02x%02x id%d sym%x off%d blk%d loc%d%s' % (
            nm, u(8), b[0x18], b[0x2d], b[0x2c], u(0x24), u(0x14), u(0x10),
            struct.unpack_from('<h', b, 0x28)[0], loc - (1 << 32) if loc >= 1 << 31 else loc, sv)

    def walk(h, tag):
        k = 0
        while h and k < 300:
            out.write('  %s %s\n' % (tag, one(h))); h = d32(h); k += 1

    r.halt(5.0)
    for a in BP: r.insert_breakpoint(a + DELTA)
    r.continue_nowait()
    outtxt = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '.work', 'wcc', 'probe', 'OUT.TXT')
    if os.path.exists(outtxt): os.remove(outtxt)

    def done():
        try:
            t = open(outtxt, 'rb').read()
            return b'Code size' in t or b'rror' in t
        except OSError:
            return False
    with dosrun.Dos() as d:
        d.type_line('WCC386 %s -s -3r -oneatx -d1 > OUT.TXT' % f)
    t0 = time.time(); n = 0; npass = 0
    try:
        while time.time() - t0 < secs:
            try: r.wait_for_stop(5.0)
            except Exception:
                if done(): break
                continue
            g = r.registers(); eip = g['eip'] - DELTA; n += 1
            if eip == 0x3e674:
                npass += 1
                out.write('PASS %d\n' % npass); walk(g['eax'], 'PRE ')
            elif eip == 0x3e679:
                walk(g['eax'], 'POST')
            elif eip == 0x3e712:
                walk(d32(0x6ff56), 'LOC '); out.write('END %d\n' % npass)
            elif eip == 0x3e7f3:
                out.write('  ALLOC %s from %x\n' % (one(g['eax']), d32(g['esp'])))
            elif eip == 0x4580a:
                out.write('  SETLOC %s size %d from %x\n' % (one(g['eax']), g['edx'], d32(g['esp'])))
            elif eip in (0x2dfa4, 0x46220):
                out.write('  %s\n' % ('ROUGHSORT' if eip == 0x2dfa4 else 'IDSORT')); walk(g['eax'], 'RPRE')
            elif eip in (0x2dfa9, 0x46225):
                walk(g['eax'], 'RPOST')
            elif eip == 0x44ac3:
                c = g['eax']; k = 0
                while c and k < 400: c = d32(c); k += 1
                out.write('  RASORT %d conflicts\n' % k)
            elif eip == 0x457aa:
                out.write('  PUSHLOC %s at %d line %d\n' % (one(g['edx']), -(g['eax'] if g['eax'] < 1 << 31 else g['eax'] - (1 << 32)),
                                                       struct.unpack('<H', rd(g['ebx'] + 0x20, 2))[0]))
            elif eip == 0x457ce:
                b = g['ebx']
                out.write('  PUSHSTOP ins %x op %02x line %d\n' % (b, rd(b + 0x22, 1)[0] if b else 0,
                                                                  struct.unpack('<H', rd(b + 0x20, 2))[0] if b else 0))
            out.flush()
            dosrun.resume_over(r, g['eip'])
    finally:
        try: r.halt(5.0)
        except Exception: pass
        for a in BP: r.remove_breakpoint(a + DELTA)
        r.continue_nowait(); r.close()
    print('hits', n, 'passes', npass, 'seconds %.0f' % (time.time() - t0))


main()
