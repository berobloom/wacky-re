#!/usr/bin/env python3
"""difffn.py: does our C do the SAME THING as the game? The byte comparisons cannot say. A near miss
whose instructions are all in another order, and one whose stores go through a register, both look
fine to nmrefs.py - and they hid the two bugs found by hand (players_ahead's clipy never set,
fn_173c4's wrong record field). This runs one function twice under Unicorn (tools/le/emu.py): once in
the original WW.EXE, once in the hybrid build where our C sits in the game's slot
(tools/link/build.sh). Each side starts from its own image, so what differs is the code, not the
input. Compared: every byte of the data segment, EAX on return, and the sequence of library calls.

    tools/link/build.sh && tools/le/leparse.py .work/link/WWNEW.EXE .work/le/wwnew
    .work/venv/bin/python tools/wc/difffn.py cars_menu             # one function
    .work/venv/bin/python tools/wc/difffn.py                       # every open line of nearmiss.txt
    SEED=0x88ccd=1:b .work/venv/bin/python tools/wc/difffn.py NAME   # globals set first (parse_seed)
    TICK=8 .work/venv/bin/python tools/wc/difffn.py NAME   # ticks (0x7e720) += 8 per stubbed call
    NEW=wworig MAP=.work/link/WWORIG.MAP .work/venv/bin/python tools/wc/difffn.py NAME
        control: the game against itself, which must print SAME

Above the game's C module the code is not C: the hand-written asm module and the linked clib, which
make DOS calls we cannot run. That part is replaced by a stub that records the call (EAX/ECX/EDX plus
three stack dwords) and returns EAX=0. In the hybrid the same part sits in BLOBTEXT, below our _TEXT,
so its stub range is the mirror image; between the limits both builds run C - the game's and ours.
Everything runs with its own image's globals and zero arguments. A run that touches unmapped memory
(a null pointer out of a zeroed global) reports CRASH; the same crash point on both sides is
inconclusive, not a pass. On a difference it re-runs both sides with a watch on the differing bytes
and prints the instruction that writes each, which is what the hand check needs.

Note a helper is not held equal: a call below the limit runs the game's C on one side and ours on the
other, so a DIFF can name a function whose own bytes match. Check the callee's row too.

And a DIFF byte is only interesting if it is not an address: a global that caches a pointer into a
stack frame (palptr 0x7d760 - fade_in 0x32d4c stores its local palette buffer there) reads as a
difference because the two builds' frames are not the same size, while the memory it points at is
equal. Compare what such a pointer points at before writing BUG (iter 70, scores_screen 0x1ce14:
palptr 0xeffae8 vs 0xeffae4, 0x800 bytes below it equal but for the pointer itself).
"""
import json
import os
import re
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'wc'))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'le'))
from unicorn import UC_HOOK_CODE, UC_HOOK_INTR, UC_HOOK_MEM_WRITE, UC_HOOK_MEM_UNMAPPED  # noqa: E402
from unicorn.x86_const import UC_X86_REG_EIP, UC_X86_REG_ESP
import capstone                                                                          # noqa: E402
from emu import Emu, REGS, STACK_TOP, SENTINEL                                           # noqa: E402
import objmatch as M                                                                     # noqa: E402

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
CMOD_END = 0x36a4e        # the game's C module ends here (STATUS, iter 56c); asm module + clib above
COUNT = int(os.environ.get('COUNT', '3000000'))   # instruction cap; a run that hits it is CAPPED
CALLS = int(os.environ.get('CALLS', '300'))   # stop after this many stubbed calls: the loop
# counter a menu function never leaves, and the one thing that puts both runs at the same progress
TICK = int(os.environ.get('TICK', '0'))       # ticks (0x7e720) added per stubbed call; 0 = off
TICKADDR = int(os.environ.get('TICKADDR', '0x7e720'), 0)   # hex, so base 0


def parse_seed(text):
    """SEED=addr=value[:b|:w|:d],... -> [(addr, bytes)]: globals written into both images before the run
    (and before the init snapshot, so the watch re-run starts from the same seeded state). A wait loop
    that polls a global no stub ever sets (a key flag, a frame counter) runs to the cap; seed the
    global the loop tests and both sides leave it. Addresses are the game's: the hybrid links its
    data at the same addresses (iter 74, dosrun locate + the v7d884 pointer check). Width :d (default),
    :w or :b after the value - a suffix letter alone would be a hex digit (0x1b)."""
    out = []
    for item in filter(None, (t.strip() for t in text.split(','))):
        addr, _, val = item.partition('=')
        val, _, w = val.partition(':')
        width = {'b': 1, 'w': 2, 'd': 4, '': 4}[w.lower()]
        out.append((int(addr, 16), (int(val, 0) & ((1 << (8 * width)) - 1)).to_bytes(width, 'little')))
    return out


SEED = parse_seed(os.environ.get('SEED', ''))
ACC = {16: 'read', 17: 'write', 18: 'fetch', 19: 'write_prot', 20: 'read_prot', 21: 'fetch_prot'}


class Run:
    """one image, and what one call into it left behind"""

    def __init__(self, img, lo, hi):
        self.e, self.img = Emu(img), img
        self.lo, self.hi = lo, max(hi, lo)              # the stub range (asm module + clib)
        self.stubs, self.crash, self.watch, self.calls = [], None, set(), False
        o = self.e.img.meta['objects']
        self.cend = o[0]['base'] + o[0]['vsize']
        # the DPMI zero page: half these functions run with a pointer global still 0, so give the
        # null dereference somewhere to go. Both sides get the same scratch page and it is compared
        # with the data segment, so a difference in what they write through a null pointer shows up.
        self.e.uc.mem_map(0, 0x10000)
        self.ranges = [(0, 0x10000), (o[1]['base'], o[1]['vsize'])]
        self.dbase, self.dsize = o[1]['base'], o[1]['vsize']
        for a, v in SEED:                               # SEED=..., before the snapshot (parse_seed)
            self.e.uc.mem_write(a, v)
        self.init = self._read()
        self.went = False
        # the per-instruction hook covers only the stub range, so running C costs nothing
        self.e.uc.hook_add(UC_HOOK_CODE, self._stub, begin=self.lo, end=self.hi)
        self.e.uc.hook_add(UC_HOOK_INTR, self._intr)
        self.e.uc.hook_add(UC_HOOK_MEM_UNMAPPED, self._unmapped)

    def _stub(self, uc, addr, size, _):
        esp = uc.reg_read(UC_X86_REG_ESP)
        self.stubs.append((addr, self.e.reg('eax'), self.e.reg('ecx'), self.e.reg('edx'),
                           [self.e.u32(esp + 4 * i) for i in range(3)]))
        self.e.reg('eax', 0)
        if TICK:                                  # the timer ISR that drives ticks lives above the C
            self.e.put32(TICKADDR, self.e.u32(TICKADDR) + TICK)   # module (0x10a15 et al only zero it),
        # so `while (ticks - snapshot < 8)` at 0x1604c can never end. A stubbed call is the frame pump
        # (menu's tail call 0x37de0), so this is the one place both sides can advance it, identically.
        uc.reg_write(UC_X86_REG_EIP, self.e.u32(esp))   # pretend the stub returned
        uc.reg_write(UC_X86_REG_ESP, esp + 4)
        if len(self.stubs) >= CALLS:
            self.calls = True
            uc.emu_stop()

    def _intr(self, uc, num, _):
        self.stubs.append(('int%d' % num, self.e.reg('eax'), 0, 0, []))
        self.e.reg('eax', 0)
        if TICK:
            self.e.put32(TICKADDR, self.e.u32(TICKADDR) + TICK)
        if len(self.stubs) >= CALLS:
            self.calls = True
            uc.emu_stop()

    def _unmapped(self, uc, access, address, size, value, _):
        eip = uc.reg_read(UC_X86_REG_EIP)
        self.crash = ('%s at %#x' % (ACC.get(access, access), address), one(eip, self))
        return False

    def _watch(self, uc, access, address, size, value, _):
        if address in self.watch:
            self.events.append((uc.reg_read(UC_X86_REG_EIP), address, size, value))

    def _read(self):
        return b''.join(bytes(self.e.uc.mem_read(a, n)) for a, n in self.ranges)

    def restore(self):
        """back to the image's bytes: the watch re-run has to start where the first one did"""
        off = 0
        for a, n in self.ranges:
            self.e.uc.mem_write(a, self.init[off:off + n])
            off += n

    def go(self, addr, count=COUNT):
        if self.went:
            self.restore()
        esp = STACK_TOP - 0x100
        self.e.put32(esp, SENTINEL)
        self.e.uc.reg_write(UC_X86_REG_ESP, esp)
        for r in ('eax', 'ebx', 'ecx', 'edx', 'esi', 'edi', 'ebp'):
            self.e.reg(r, 0)
        self.events = []
        # a re-run in the same Run inherits the first run's counters, and the CALLS cap then fires on
        # the first stubbed call: the watch pass of a CALLCAP function stopped at once and printed no
        # writers at all (iter 70)
        self.stubs, self.crash, self.calls = [], None, False
        if self.watch:
            self.wh = self.e.uc.hook_add(UC_HOOK_MEM_WRITE, self._watch)
        try:
            self.e.uc.emu_start(addr, SENTINEL, count=count)
        except Exception as err:                        # a fault; the unmapped hook named it
            if not self.crash:
                self.crash = str(err)
        self.done = self.e.uc.reg_read(UC_X86_REG_EIP) == SENTINEL
        self.went = True
        self.fin = self._read()
        return self


def diff_bytes(a0, a1, b0, b1):
    """the compared-byte offsets one of the two runs changed whose end values disagree"""
    ch = {i for i, (x, y) in enumerate(zip(a0, a1)) if x != y}
    ch |= {i for i, (x, y) in enumerate(zip(b0, b1)) if x != y}
    return sorted(i for i in ch if a1[i] != b1[i])


def lin(r, i):
    """a compared-buffer offset as a linear address"""
    for a, n in r.ranges:
        if i < n:
            return a + i
        i -= n
    return -1


def map_publics(path, lnk):
    """a WLINK map -> ({segment class}, {symbol: (seg, off)}, {segment name: (seg, off)});
    WW.LNK's `alias a=b` adds a's name for the game's old spelling of one of our functions"""
    cls, pubs, segs = {}, {}, {}
    for line in open(path):
        m = re.match(r'^(\S+)\s+(CODE|DATA)\s+\S*\s+(\d+):([0-9a-f]{8})\s+[0-9a-f]{8}\s*$', line)
        if m:
            cls[m.group(3)] = m.group(2)
            segs[m.group(1)] = (m.group(3), int(m.group(4), 16))
            continue
        m = re.match(r'^(\d+):([0-9a-f]{8}).? (\S+)\s*$', line)   # WLINK marks publics * + -
        if m:
            pubs[m.group(3)] = (m.group(1), int(m.group(2), 16))
    for line in open(lnk):
        m = re.match(r'alias (\S+)=(\S+)', line.strip())
        if m and m.group(2) in pubs:
            pubs[m.group(1)] = pubs[m.group(2)]
    return cls, pubs, segs


def json_base(name):
    m = json.load(open(os.path.join(ROOT, '.work', 'le', name + '.json')))
    return [o['base'] for o in m['objects']]


def one(eip, r):
    try:
        buf = bytes(r.e.uc.mem_read(eip, 12))
    except Exception:
        return '?'
    for ins in md.disasm(buf, eip):
        return '%-8s %s' % (ins.mnemonic, ins.op_str)
    return '?'


def main():
    new = os.environ.get('NEW', 'wwnew')
    orig = os.environ.get('ORIG_IMG', 'ww')
    mapf = os.environ.get('MAP', os.path.join(ROOT, '.work', 'link', 'WWNEW.MAP'))
    nb = json_base(new)
    cls, pubs, segs = map_publics(mapf, os.path.join(ROOT, '.work', 'link', 'WW.LNK'))
    # our C module starts where _TEXT starts; a build with no _TEXT (NEW=wworig, the game relinked -
    # the control) is stubbed by the same rule as the original, above the game's C module
    text = nb[0] + segs['_TEXT'][1] if '_TEXT' in segs else None

    def new_addr(name):
        for cand in (name + '_', name, '_' + name):
            if cand in pubs:
                s, off = pubs[cand]
                return (nb[0] if cls[s] == 'CODE' else nb[1]) + off
        return None

    names = M.names(orig)
    todo = []
    for a in sys.argv[1:]:
        nm, _, ad = a.partition('=')
        todo.append((nm, int(ad, 16) if ad else names.get(nm)))
    if not todo:
        for line in open(os.path.join(ROOT, 'src', 'nearmiss.txt')):
            f = line.split()
            if len(f) >= 4 and f[1].startswith('0x') and not f[3].startswith('MATCH'):
                todo.append((f[2], int(f[1], 16)))
    for name, addr in todo:
        naddr = new_addr(name)
        if not addr or naddr is None:
            print('%-20s not in %s names or not a hybrid public' % (name, orig))
            continue
        a = Run(orig, CMOD_END, 1 << 32).go(addr)
        b = Run(new, CMOD_END, 1 << 32).go(naddr) if text is None \
            else Run(new, nb[0], text).go(naddr)     # BLOBTEXT below our code is asm + clib
        dif = diff_bytes(a.init, a.fin, b.init, b.fin)
        notes = []
        if a.crash or b.crash:
            notes.append('CRASH %s %s | %s %s'
                         % (a.crash[1] if a.crash else '-', a.crash[0] if a.crash else '-',
                            b.crash[1] if b.crash else '-', b.crash[0] if b.crash else '-'))
            if a.crash and a.crash[1] == b.crash[1]:
                notes[-1] += ' (same instruction: inconclusive)'
        if a.calls or b.calls:
            notes.append('CALLCAP %d/%d' % (len(a.stubs), len(b.stubs)))
        if not a.done and not b.done and not (a.calls or b.calls):
            notes.append('both stopped at the %d-instruction cap: the two runs are at whatever '
                         'point their own code reached, a DIFF here needs checking by hand' % COUNT)
        elif not a.done or not b.done:
            notes.append('one side never returned (game %s ours %s)' % (a.done, b.done))
        if dif:
            notes.append('DIFF %d byte(s)' % len(dif))
        else:
            notes.insert(0, 'SAME')
        if a.e.reg('eax') != b.e.reg('eax'):
            notes.append('EAX %#x/%#x (a void function leaves garbage in EAX)' % (a.e.reg('eax'),
                                                                                 b.e.reg('eax')))
        if len(a.stubs) != len(b.stubs):
            notes.append('calls %d/%d' % (len(a.stubs), len(b.stubs)))
        tag = '  '.join(notes)
        if dif:
            dif = dif[:40]
        print('%-20s %s  (game 0x%x ours 0x%x)' % (name, tag, addr, naddr))
        if dif:
            for i in dif[:12]:
                print('    %#08x  game %02x -> %02x   ours %02x -> %02x'
                      % (lin(a, i), a.init[i], a.fin[i], b.init[i], b.fin[i]))
            a.watch = b.watch = {lin(a, i) for i in dif}
            a.go(addr), b.go(naddr)
            for side, r in (('game ', a), ('ours ', b)):
                for eip, ad, sz, val in r.events[-6:]:   # the last writes to those bytes decide them
                    print('    %s %#x <- %#x (%d) at %#x  %s'
                          % (side, ad, val, sz, eip, one(eip, r)))
            for k, (sa, sb) in enumerate(zip(a.stubs, b.stubs)):
                if sa[1:] != sb[1:]:
                    print('    call %d: game %s%s  ours %s%s'
                          % (k, hex(sa[0]) if isinstance(sa[0], int) else sa[0], sa[1:],
                             hex(sb[0]) if isinstance(sb[0], int) else sb[0], sb[1:]))
                    break


if __name__ == '__main__':
    main()
