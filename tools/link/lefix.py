"""lefix.py: every fixup of an LE image as (source VA, kind, target VA).
kind: 'off32' (LE source type 7, a 32-bit offset) or 'sel16' (type 2, a 16-bit selector of the
target object; target VA = that object's base). Usage as a module: fixups(path)."""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'le'))
from leparse import LE


def fixups(path):
    le = LE(open(path, 'rb').read())
    bases = {o['num']: o['base'] for o in le.objects}
    out = []
    for o in le.objects:
        for i in range(o['page_count']):
            page = o['page_index'] + i
            for stype, so, tobj, toff in le.fixups(page):
                src = o['base'] + i * le.page_size + so
                if stype == 0x07:
                    out.append((src, 'off32', bases[tobj] + toff))
                elif stype == 0x02:
                    out.append((src, 'sel16', bases[tobj]))
                else:
                    raise ValueError('fixup source type 0x%02x at 0x%x' % (stype, src))
    return le, out


if __name__ == '__main__':
    le, fx = fixups(sys.argv[1] if len(sys.argv) > 1 else 'original/WW.EXE')
    print(len(fx), 'fixups')
