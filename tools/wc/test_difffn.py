#!/usr/bin/env python3
"""test_difffn.py: the two pure pieces of difffn.py - which bytes count as changed, and what a
byte offset of the compared buffer means as an address. Run: .work/venv/bin/python tools/wc/test_difffn.py (it imports
difffn, which needs the venv's unicorn)"""
import os
import sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
try:
    from difffn import diff_bytes, lin                                    # noqa: E402
except ImportError:                        # overnight/check.sh uses system python3, no unicorn
    print('test_difffn skipped (run .work/venv/bin/python tools/wc/test_difffn.py)')
    sys.exit(0)

# a byte both runs left alone is not a difference, even if the images disagree there (our CONST group
# has dead bytes that differ - iter 68 datamatch)
assert diff_bytes(b'\x00\x01', b'\x00\x01', b'\x09\x01', b'\x09\x01') == []
# a byte one run changed to a value the other run also has is not a difference either
assert diff_bytes(b'\x00\x00', b'\x00\x14', b'\x00\x00', b'\x00\x14') == []
# the f14 bug: the game wrote 0x14 at 0x6ab0c, our build left 0 - and EAX garbage must not matter
assert diff_bytes(b'\x00\x00', b'\x14\x00', b'\x00\x00', b'\x00\x00') == [0]
# a byte both runs changed, to different values, is
assert diff_bytes(b'\x00', b'\x52', b'\x00', b'\x40') == [0]

# ranges are concatenated: the second one's offsets start after the first's length
class R:
    ranges = [(0, 0x10000), (0x60000, 0x35464)]


assert lin(R(), 0) == 0
assert lin(R(), 0xffff) == 0xffff
assert lin(R(), 0x10000) == 0x60000
assert lin(R(), 0x10000 + 0xab0c) == 0x6ab0c     # the f14 address, second range
print('test_difffn ok')
