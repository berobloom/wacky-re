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
    TICKREAD=8 ...   # ticks += 8 per READ of the clock, for a wait loop whose body has no call
    RESEED=0x88cce=1:b ...   # value written after every READ of that global (press+release waits)
    KEY=0x61,0x62,0x0d ...   # the characters the clib `getch` stub returns, in order (last repeats)
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
from unicorn import (UC_HOOK_CODE, UC_HOOK_INTR, UC_HOOK_MEM_WRITE, UC_HOOK_MEM_READ,
                     UC_HOOK_MEM_UNMAPPED, UC_HOOK_INSN)                                                                        # noqa: E402
from unicorn.x86_const import (UC_X86_REG_EIP, UC_X86_REG_ESP, UC_X86_INS_IN, UC_X86_INS_OUT)
import capstone                                                                          # noqa: E402
from emu import Emu, REGS, STACK_TOP, SENTINEL                                           # noqa: E402
import objmatch as M                                                                     # noqa: E402

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
CMOD_END = 0x36a4e        # the game's C module ends here (STATUS, iter 56c); asm module + clib above
COUNT = int(os.environ.get('COUNT', '3000000'))   # instruction cap; a run that hits it is CAPPED
CALLS = int(os.environ.get('CALLS', '300'))   # stop after this many stubbed calls: the loop
# counter a menu function never leaves, and the one thing that puts both runs at the same progress
TICK = int(os.environ.get('TICK', '0'))       # ticks (0x7e720) added per stubbed call; 0 = off
TICKREAD = int(os.environ.get('TICKREAD', '0'))   # ticks += this per READ of TICKADDR; 0 = off
TICKADDR = int(os.environ.get('TICKADDR', '0x7e720'), 0)   # hex, so base 0
# FREAD=13: the byte the clib `fread` stub leaves at its destination when it is asked for ONE element
# (`fread(ptr, 1, n, fp)` - Watcom passes ptr in EAX and size in EDX). The stub delivers nothing, so a
# byte-wise line reader never sees its terminator: `read_line` 0x26ff0 loops on `cmp byte [esi],0xd`
# until ESI walks off the stack into unmapped memory - that is why gam_load 0x2717c, and with it
# fn_17928, could not return (iter 80). Bulk reads (size != 1) are left untouched. 13 = CR is what
# read_line tests; the buffer is a local of the callee, so SEED= cannot reach it (the two builds' frames
# sit at different addresses).
FREAD = int(os.environ.get('FREAD', '0'), 0)
# RAND=8: give the clib `rand` stub a small draw instead of the constant 0. fn_1be3c's pick-7-distinct
# seats loop (draw at 0x1beb8, `test ax,ax; jl 0x1beae` / `cmp ax,7; jg 0x1beae` reject everything but
# 0-7, reject a repeat at 0x1bed5-0x1bee0 `je 0x1beae`) re-draws until it gets a value in 0..7, so with
# the stub always returning 0 it loops forever and that is where demo_mode 0x1e2e4 and fn_1be3c 0x1be3c
# sit at the call cap - not a global wait, so SEED=/TICK= cannot reach it. RAND=n makes the draw
# 0..n-1: RAND=8 is what the loop wants (one draw per seat) and the game's own acceptance rate is
# ~1/4096 with the image's un-seeded LCG state (57134 draws for 6 accepted values, hotfn iter 81) -
# srand(max(racetime,8)) at 0x1be4e is stubbed, so the real seed is never installed. The generator is
# the game's own clib LCG (0x43092 `imul edx, [eax], 0x41c64e6d`, 0x43098 `add edx, 0x3039`) from a
# fixed start, because the stub never touches the clib's seed slot. Both builds draw the same nth
# value, so the comparison still compares the C; if the two sides draw at different rates the data
# DIFF shows it. fn_1be3c's other arm skips rand when splitscreen != 0 (0x1beb6), which demo_mode
# zeroes at 0x1e51e.
RAND = int(os.environ.get('RAND', '0'), 0)
# KEY=0x61,0x62,0x08,0x43,0x0d: the characters the clib `getch` stub returns, in order; after the list
# runs out its LAST value repeats, so a run always ends rather than sitting at the cap. enter_name
# 0x1cb44's input loop 0x1ccca-0x1cdff exits only on `al == 0xd` or `al == 0x1b` (0x1cd8f/0x1cd97, the
# value of the stubbed `getch` at 0x1cd88), so the stub's constant 0 loops forever and no SEED= reaches
# it - the loop tests no global. One list index per getch call, so both builds must call getch the same
# number of times; that is what the call log and the byte DIFF check.
KEY = [int(t, 0) for t in os.environ.get('KEY', '').split(',') if t.strip()]


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
# RESEED=0x88cce=1:b - the same syntax as SEED=, but written on every READ of those bytes (the
# TICKREAD mechanism, a data hook so both builds' copies of a loop are covered). A press is seedable
# and a release never is: `wait_release` 0x278c4 with mode 0 returns only while v88cce and the four joy
# flags are 0 (0x278e7 `cmp bl,[v88cce]; je` -> it stores joy88b40=1 itself and then never matches bx),
# and `wait_input` 0x27920 with mode 0 returns only once v88cce != 0 (0x27995 `cmp bl,[v88cce];
# jne 0x279e2`) - fn_1c0f4's tail calls one after the other (0x1c7d9, 0x1c7ea), so one static SEED=
# parks one side in each. The write lands on the read AFTER the one that arms it (the READ hook fires
# before the value is fetched, and unicorn 2.1.4's python wrapper has UC_HOOK_MEM_READ_AFTER commented
# out of its handler table): the first poll of the pair returns on the image's state, the next sees the
# seeded value. A UC_HOOK_MEM_READ that wrote on the first read moved the hot loop from wait_input to
# wait_release instead - it parks the run one wait earlier (iter 81). Reads are counted, so both builds
# must poll the flag the same number of times: check the byte DIFF before believing one.
RESEED = parse_seed(os.environ.get('RESEED', ''))
# ARGS=0x3f8,4,0x3840,1 -> the Watcom argument registers (eax, edx, ebx, ecx, ...). Zero arguments are
# not always a legal call: com_open's `div ecx` at 0x103b1 is a divide error when baud is 0, and both
# runs then die at the same instruction, which is inconclusive (iter 76).
ARGREGS = os.environ.get('ARGS', '').split(',')
# IN=0x3fa=0xc0: the value IN reads from one port (default 0). com_open's `iir = inp(comport + 2) &
# 0xf8 == 0xc0` test at 0x1044e picks the 16550 FIFO branch, so a zero IN only ever runs the other one
# (iter 76).
INVALS = dict((int(k, 16), int(v, 0)) for k, v in
              (item.split('=') for item in filter(None, (t.strip() for t in
                                                        os.environ.get('IN', '').split(',')))))
ACC = {16: 'read', 17: 'write', 18: 'fetch', 19: 'write_prot', 20: 'read_prot', 21: 'fetch_prot'}


class Run:
    """one image, and what one call into it left behind"""

    def __init__(self, img, lo, hi):
        self.e, self.img = Emu(img), img
        self.lo, self.hi = lo, max(hi, lo)              # the stub range (asm module + clib)
        self.stubs, self.crash, self.watch, self.calls = [], None, set(), False
        self.ncalls = 0        # stubbed calls and interrupts only - not the port traffic below
        self.fread = None      # this image's clib fread, set by main() (map offset + code base)
        self.rand = None       # ditto for clib rand (RAND=)
        self.getch = None      # ditto for clib getch (KEY=)
        self.exit = None       # ditto for clib exit_ (the run ends there; see _stub)
        self.nkey = 0          # getch calls so far: which KEY= entry to return
        self.rseed = 0x1234    # the rand stub's own seed; the clib's slot is never written
        self.reseed = dict(RESEED)   # RESEED= address -> bytes (written after every READ but the first)
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
        # `in`/`out` (Watcom's inline outp/inp, e.g. com_open's UART init at 0x10436) are not
        # emulated: without these hooks emu_start returns at the first one, no crash, no cap, and
        # main() prints the run as if it had burned the whole instruction budget (iter 76: com_open
        # 0x102f4 stopped at 0x103b1 `in al, dx` after 27k insns, 0.2 s). They become recorded
        # calls, so the port traffic is compared too; IN reads 0 (no hardware here).
        self.e.uc.hook_add(UC_HOOK_INSN, self._pio_in, None, 0, 0, UC_X86_INS_IN, 0)
        self.e.uc.hook_add(UC_HOOK_INSN, self._pio_out, None, 0, 0, UC_X86_INS_OUT, 0)
        # TICKREAD: the timer ISR advances the clock while a wait loop polls it, and the loop body has
        # no stubbed call for TICK= to fire at (chat's 0x150e5 `mov eax,[ticks]; sub eax,edi; cmp
        # eax,0x18; jl` is the whole loop). Hooking the clock's ADDRESS rather than a code range works
        # in both builds, whose copies of that loop sit at different addresses. Both runs get the same
        # rule, so the comparison still means the same thing - but it counts reads, so a build that
        # polls the clock a different number of times per pass advances it differently: check a
        # tick-dependent DIFF against the read counts before calling it a bug (iter 79).
        if TICKREAD:
            self.e.uc.hook_add(UC_HOOK_MEM_READ, self._tickread, begin=TICKADDR, end=TICKADDR + 3)
        if RESEED:      # the address test is in the callback: one hook for all entries
            self.e.uc.hook_add(UC_HOOK_MEM_READ, self._reseed)

    def _stub(self, uc, addr, size, _):
        esp = uc.reg_read(UC_X86_REG_ESP)
        self.stubs.append((addr, self.e.reg('eax'), self.e.reg('ecx'), self.e.reg('edx'),
                           [self.e.u32(esp + 4 * i) for i in range(3)]))
        self.ncalls += 1
        if addr == self.exit:      # the clib `exit_` (game 0x42c11, hybrid 0x1c1d3): the program is
            # over and its stub cannot return - popping the slot at ESP jumps into the C module's 0xcc
            # padding at 0x10000 (`int3; jmp 0x10000`, 289 int3 records in a `main` run), while our
            # build, whose copy of the same entry sits in BLOBTEXT, is itself stubbed there and returns
            # to whatever the stack holds. End the run at the call instead: `main` 0x35ca4 tail-jumps to
            # it at 0x35e10 and ours at 0x5ae18, so both sides stop at the same point (iter 82).
            uc.reg_write(UC_X86_REG_EIP, SENTINEL)   # go() reads EIP == SENTINEL as `returned`
            uc.emu_stop()
            return
        ret = 0
        if KEY and addr == self.getch:
            ret = KEY[min(self.nkey, len(KEY) - 1)]
            self.nkey += 1
        if FREAD and addr == self.fread and self.e.reg('edx') == 1:
            self.e.uc.mem_write(self.e.reg('eax'), bytes([FREAD]))   # before EAX is zeroed
        if RAND and addr == self.rand:
            self.rseed = (self.rseed * 0x41c64e6d + 0x3039) & 0xffffffff   # the game's clib LCG
            self.e.reg('eax', (self.rseed >> 16) % RAND)                   # RAND=n -> a draw of 0..n-1
        else:
            self.e.reg('eax', ret)
        if TICK:                                  # the timer ISR that drives ticks lives above the C
            self.e.put32(TICKADDR, self.e.u32(TICKADDR) + TICK)   # module (0x10a15 et al only zero it),
        # so `while (ticks - snapshot < 8)` at 0x1604c can never end. A stubbed call is the frame pump
        # (menu's tail call 0x37de0), so this is the one place both sides can advance it, identically.
        uc.reg_write(UC_X86_REG_EIP, self.e.u32(esp))   # pretend the stub returned
        uc.reg_write(UC_X86_REG_ESP, esp + 4)
        if self.ncalls >= CALLS:
            self.calls = True
            uc.emu_stop()

    def _pio_in(self, uc, port, size, _):
        val = INVALS.get(port, 0) & ((1 << (8 * size)) - 1)
        self.stubs.append(('$in %#x' % port, val, size, 0, []))
        return val                                # the value IN leaves in the register

    def _pio_out(self, uc, port, size, value, _):
        self.stubs.append(('$out %#x' % port, value, size, 0, []))

    def _tickread(self, uc, _access, _address, _size, _value, _):
        self.e.put32(TICKADDR, self.e.u32(TICKADDR) + TICKREAD)

    def _reseed(self, uc, _access, address, _size, _value, _):
        for a, v in self.reseed.items():
            if a <= address < a + len(v):
                if a in self.reseed_seen:      # the arming read itself still sees the old bytes
                    self.e.uc.mem_write(a, v)
                self.reseed_seen.add(a)

    def _intr(self, uc, num, _):
        self.stubs.append(('int%d' % num, self.e.reg('eax'), 0, 0, []))
        self.e.reg('eax', 0)
        self.ncalls += 1
        if TICK:
            self.e.put32(TICKADDR, self.e.u32(TICKADDR) + TICK)
        if self.ncalls >= CALLS:
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
        for r, v in zip(('eax', 'edx', 'ebx', 'ecx'), ARGREGS):
            if v.strip():
                self.e.reg(r, int(v, 0))
        self.events = []
        # a re-run in the same Run inherits the first run's counters, and the CALLS cap then fires on
        # the first stubbed call: the watch pass of a CALLCAP function stopped at once and printed no
        # writers at all (iter 70)
        self.stubs, self.crash, self.calls, self.ncalls = [], None, False, 0
        self.rseed = 0x1234                      # so the watch re-run draws the same sequence
        self.nkey = 0                             # ... and types the same KEY= characters
        self.reseed_seen = set()                 # ... and arms its RESEED globals the same way
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


def lib_addr(img, cands):
    """one image's address of a clib routine (the FREAD=/RAND= stub hooks need it, and the two images
    differ): the WLINK map offset + that image's code object base. The original's symbols come from
    the relinked map WWORIG.MAP - 0x10000 + 0x32c68 = 0x42c68, the `call 0x42c68` of the disassembly -
    the hybrid's from its own map (MAP=), 0x10000 + 0xc22a = 0x1c22a, whose first bytes are identical
    to the game's (iter 80)."""
    mapf = (os.path.join(ROOT, '.work', 'link', 'WWORIG.MAP') if img == 'ww'
            else os.environ.get('MAP', os.path.join(ROOT, '.work', 'link', 'WWNEW.MAP')))
    _, p, _s = map_publics(mapf, os.path.join(ROOT, '.work', 'link', 'WW.LNK'))
    for cand in cands:
        if cand in p:
            return json_base(img)[0] + p[cand][1]
    return None


def fread_addr(img):
    return lib_addr(img, ('fread', 'fread_', '_fread'))


def rand_addr(img):
    return lib_addr(img, ('rand', 'rand_', '_rand'))


def getch_addr(img):
    return lib_addr(img, ('getch_', 'getch', '_getch'))


def exit_addr(img):
    return lib_addr(img, ('exit_', 'exit', '_exit'))


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

    ofread, orand, ogetch = fread_addr(orig), rand_addr(orig), getch_addr(orig)
    nfread, nrand, ngetch = fread_addr(new), rand_addr(new), getch_addr(new)
    oexit, nexit = exit_addr(orig), exit_addr(new)
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
        a = Run(orig, CMOD_END, 1 << 32)
        a.fread, a.rand, a.getch = ofread, orand, ogetch
        a.exit = oexit
        a = a.go(addr)
        b = Run(new, CMOD_END, 1 << 32) if text is None \
            else Run(new, nb[0], text)               # BLOBTEXT below our code is asm + clib
        b.fread, b.rand, b.getch = nfread, nrand, ngetch
        b.exit = nexit
        b = b.go(naddr)
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
