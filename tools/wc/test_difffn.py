#!/usr/bin/env python3
"""test_difffn.py: the iter-76 additions to difffn.py - the IN/OUT hooks and the IN value.

    .work/venv/bin/python tools/wc/test_difffn.py

Not a one-function-per-process run: it checks the hook contract on a throwaway VM and the recording
callbacks on a fake self, so it fails if the bindings' callback signature or the value masking breaks.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'tools', 'le'))
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_INSN                      # noqa: E402
from unicorn.x86_const import UC_X86_REG_EDX, UC_X86_REG_EAX, UC_X86_INS_IN, UC_X86_INS_OUT  # noqa: E402
import difffn as D                                                                 # noqa: E402


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
    print('OK')


if __name__ == '__main__':
    main()
