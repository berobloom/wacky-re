#!/usr/bin/env python3
"""test_difffn.py: the iter-76 additions to difffn.py - the IN/OUT hooks and the IN value - and the
iter-80 FREAD= stub byte.

    .work/venv/bin/python tools/wc/test_difffn.py

Not a one-function-per-process run: it checks the hook contract on a throwaway VM and the recording
callbacks on a fake self, so it fails if the bindings' callback signature or the value masking breaks.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'tools', 'le'))
try:
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_INSN                  # noqa: E402
    from unicorn.x86_const import (UC_X86_REG_EDX, UC_X86_REG_EAX, UC_X86_REG_ECX,      # noqa: E402
                                   UC_X86_REG_ESP, UC_X86_INS_IN, UC_X86_INS_OUT)
    import difffn as D                                                             # noqa: E402
except ImportError as err:                        # overnight/check.sh runs the tests with plain
    print('SKIP: %s (run: .work/venv/bin/python tools/wc/test_difffn.py)' % err)
    sys.exit(0)                                   # python3, which has no unicorn


class Fake:
    stubs = []


def main():
    assert D.parse_seed('0x76=0x8c800:d,0x7e728=1:d,0x8c800=3:b') == [
        (0x76, bytes.fromhex('00c80800')), (0x7e728, bytes.fromhex('01000000')),
        (0x8c800, b'\x03')], D.parse_seed('0x76=0x8c800:d,0x7e728=1:d,0x8c800=3:b')

    u = Uc(UC_ARCH_X86, UC_MODE_32)
    u.mem_map(0x1000, 0x1000)
    seen = []
    # the real callbacks, on a VM running `mov edx,0x3f8; out dx,al; in al,dx; hlt`
    u.hook_add(UC_HOOK_INSN, lambda *a: D.Run._pio_in(Fake, *a), None, 0, 0, UC_X86_INS_IN, 0)
    u.hook_add(UC_HOOK_INSN, lambda *a: seen.append(a) or D.Run._pio_out(Fake, *a),
               None, 0, 0, UC_X86_INS_OUT, 0)
    # INVALS is read from the module, so set it the way the env would: the port read
    # back is 0x3f8 (the same DX the `out` used)
    D.INVALS[0x3f8] = 0xc0
    u.mem_write(0x1000, bytes.fromhex('ba f8 03 00 00'   # mov edx, 0x3f8
                                      'ee'                # out dx, al
                                      'ed'                # in al, dx
                                      'f4'))              # hlt
    u.reg_write(UC_X86_REG_EAX, 0x2b)
    u.reg_write(UC_X86_REG_EDX, 0)
    u.emu_start(0x1000, 0x1008)
    assert u.reg_read(UC_X86_REG_EDX) == 0x3f8            # the IN/OUT hooks ran, the VM moved on
    assert u.reg_read(UC_X86_REG_EAX) == 0xc0, hex(u.reg_read(UC_X86_REG_EAX))
    # callback args: IN (uc, port, size, ud), OUT (uc, port, size, value, ud)
    assert seen[0][1:-1] == (0x3f8, 1, 0x2b), seen
    # what difffn records and compares: the port in the call slot, the value in the EAX slot
    Fake.stubs = []
    assert D.Run._pio_in(Fake, u, 0x3fb, 1, None) == 0     # a port with no IN= entry reads 0
    assert D.Run._pio_in(Fake, u, 0x3f8, 1, None) == 0xc0
    assert [(s[0], s[1], s[2]) for s in Fake.stubs] == [('$in 0x3fb', 0, 1), ('$in 0x3f8', 0xc0, 1)], Fake.stubs

    # ---- FREAD= (iter 80): the fread stub leaves one byte at the buffer when size == 1 (EDX), and
    # only there: read_line 0x26ff0 calls fread(ptr,1,1,fp) per byte, and nothing else may be touched.
    class FE:                                       # the three Emu methods _stub uses
        def __init__(self, uc): self.uc = uc
        def reg(self, n, v=None):
            r = {'eax': UC_X86_REG_EAX, 'ecx': UC_X86_REG_ECX, 'edx': UC_X86_REG_EDX}[n]
            if v is None:
                return self.uc.reg_read(r)
            self.uc.reg_write(r, v & 0xffffffff)
        def u32(self, a): return int.from_bytes(bytes(self.uc.mem_read(a, 4)), 'little')
        def put32(self, a, v): self.uc.mem_write(a, v.to_bytes(4, 'little'))

    class FS:                                       # a fake Run for Run._stub
        def __init__(self, uc):
            self.e, self.stubs, self.ncalls, self.calls, self.fread = FE(uc), [], 0, False, 0x2000
            self.rand, self.rseed = None, 0x1234    # RAND= needs these too (iter 81)

    u.mem_map(0x2000, 0x2000)                       # the fread stub, its frame and the buffer
    u.mem_write(0x2000, bytes(8))                   # the "fread" entry and the return address
    u.mem_write(0x3000, bytes(6))                   # the caller's buffer
    u.reg_write(UC_X86_REG_ESP, 0x2100)
    u.mem_write(0x2100, (0x1234).to_bytes(4, 'little'))
    D.FREAD = 13
    for addr, size, want in ((0x2000, 1, 13), (0x2000, 0x300, 0), (0x2010, 1, 0)):
        u.mem_write(0x3000, bytes(6))               # a clean buffer per case (case 1 wrote 13)
        u.reg_write(UC_X86_REG_ESP, 0x2100)         # and a clean frame: _stub pops the return address
        u.reg_write(UC_X86_REG_EAX, 0x3000)
        u.reg_write(UC_X86_REG_EDX, size)
        fs = FS(u)
        D.Run._stub(fs, u, addr, 5, None)
        assert bytes(u.mem_read(0x3000, 1))[0] == want, (hex(addr), size, want)
        assert fs.stubs[0][0] == addr and fs.ncalls == 1
        # the stub returns like Watcom's: EAX = 0 and the caller's frame popped past the return address
        assert u.reg_read(UC_X86_REG_EAX) == 0 and u.reg_read(UC_X86_REG_ESP) == 0x2104

    # ---- RAND=n (iter 81): the `rand` stub must draw 0..n-1 AND vary, or the seat retry loop
    # (fn_1be3c 0x1beae, which rejects everything but 0-7 and re-draws) still never ends.
    fs = FS(u)
    fs.rand = 0x2000
    D.FREAD = 0                                     # the fread case above left it on; EDX is 0 here
    D.RAND = 8
    draws = []
    for _ in range(6):
        u.reg_write(UC_X86_REG_ESP, 0x2100)         # _stub pops the return address every call
        u.reg_write(UC_X86_REG_EAX, 0)
        D.Run._stub(fs, u, 0x2000, 5, None)
        draws.append(u.reg_read(UC_X86_REG_EAX))
    assert all(0 <= d < 8 for d in draws), [hex(d) for d in draws]
    assert len(set(draws)) > 1, [hex(d) for d in draws]

    # ---- RESEED= (iter 81): the read that arms the flag still sees the old byte, the next one sees
    # the new value (unicorn 2.1.4 has no READ_AFTER and the READ hook fires before the load). A value
    # written on the first read parks the run in the wait that comes first instead.
    D.RAND = 0
    D.RESEED = D.parse_seed('0x3010=1:b')
    u.mem_write(0x3010, b'\x00')
    fs.reseed, fs.reseed_seen = dict(D.RESEED), set()
    D.Run._reseed(fs, u, 0, 0x3010, 1, 0, None)
    assert bytes(u.mem_read(0x3010, 1)) == b'\x00', 'the arming read must see the old byte'
    D.Run._reseed(fs, u, 0, 0x3010, 1, 0, None)
    assert bytes(u.mem_read(0x3010, 1)) == b'\x01'
    D.Run._reseed(fs, u, 0, 0x3000, 1, 0, None)     # an unrelated read changes nothing
    assert bytes(u.mem_read(0x3010, 1)) == b'\x01'
    print('OK')


if __name__ == '__main__':
    main()
