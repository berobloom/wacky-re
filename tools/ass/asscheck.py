#!/usr/bin/env python3
"""asscheck.py: check the Apogee Sound System port (src/ass) function by function.

usage: tools/ass/asscheck.py [--lib] [-v] [MODULE ...]

Default: builds src/ass at Wacky Wheels' revision (tools/ass/build.sh, LIBVER_ASSREV 19941101) and runs
tools/wc/objmatch.py on every module against WW.EXE, each public compared at its address in
analysis/ww_names.txt (the ASSMATCH block and the other sound-library names): per module the MATCHes,
the near misses (named, differing) and the functions with no address; -v prints objmatch's diffs.
Exit 0 always; the last line is the total, `ASS: N of M named functions byte-identical`.

--lib: the regression check. Builds at ASS 1.04 (19950518) and compares each module's code bytes, fixups
masked, with the same module of the original 1.04 AUDIO_WF.LIB (.work/ext/audiolib/origlibs/104): every
module must be identical, so a change meant for Wacky Wheels' revision (under `#if LIBVER_ASSREV <
19950518L`) cannot have broken the 1.04 code. Exit 1 on a difference."""
import os
import re
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'wc'))
import libmatch as L  # noqa: E402
import objmatch  # noqa: E402
sys.path.insert(0, os.path.join(ROOT, 'tools', 'le'))
from lelib import Image  # noqa: E402

IMG = Image('ww')

WW_REV, LIB_REV = '19941101', '19950518'


def build(rev):
    r = subprocess.run(['sh', 'tools/ass/build.sh', rev], cwd=ROOT, capture_output=True, text=True)
    sys.stdout.write(r.stdout)
    if r.returncode:
        sys.exit('build failed')
    return os.path.join(ROOT, '.work', 'ass', rev)


def code(path_or_recs):
    recs = path_or_recs
    if isinstance(recs, str):
        recs = [r for _, r in L.modules(open(recs, 'rb').read())][0]
    return L.code_of(recs)


def check_lib(mods):
    out = build(LIB_REV)
    lib = {n.replace('\\', '/').split('/')[-1].lower(): r for n, r in
           L.modules(open(os.path.join(ROOT, '.work', 'ext', 'audiolib', 'origlibs', '104', 'AUDIO_WF.LIB'), 'rb').read())}
    bad = 0
    for m in mods:
        ours = code(os.path.join(out, m + '.OBJ'))
        theirs = code(lib[{'BLASTOLD': 'blaster'}.get(m, m.lower()) + '.c'])  # blastold.c keeps blaster.c's module name
        for sg in sorted(set(ours) | set(theirs)):
            a, ma = ours.get(sg, (b'', b''))
            b, mb = theirs.get(sg, (b'', b''))
            diff = sum(1 for i in range(min(len(a), len(b))) if not (ma[i] or mb[i]) and a[i] != b[i])
            if len(a) != len(b) or diff:
                bad += 1
                print('%-9s %-8s ours %5d bytes, LIB %5d, %d bytes differ' % (m, sg, len(a), len(b), diff))
    print('ASS 1.04 rebuild: %d modules, %s' % (len(mods), 'all identical to the original LIB' if not bad else '%d segments differ' % bad))
    return 1 if bad else 0


def check_ww(mods, verbose):
    out = build(WW_REV)
    tot = ok = mods_ok = 0
    for m in mods:
        r = subprocess.run([sys.executable, 'tools/wc/objmatch.py', 'ww', os.path.join(out, m + '.OBJ')],
                           cwd=ROOT, capture_output=True, text=True)
        lines = r.stdout.splitlines()
        fn = [l for l in lines if re.match(r'^\S+_?\s+\d+ bytes', l)]
        match = [l.split()[0] for l in fn if ' MATCH at ' in l]
        near = [l.split()[0] for l in fn if 'differs from' in l]
        tot += len(match) + len(near)
        ok += len(match)
        whole = segment(os.path.join(out, m + '.OBJ'), lines)
        mods_ok += whole.startswith('whole')
        print('%-9s %3d MATCH, %3d near miss, %3d with no address  %s%s' % (
            m, len(match), len(near), len(fn) - len(match) - len(near), whole,
            ('  near: ' + ' '.join(n.rstrip('_') for n in near))[:200] if near else ''))
        if verbose:
            print('\n'.join('    ' + l for l in lines))
    print('ASS: %d of %d named functions byte-identical; %d modules identical as a whole' % (ok, tot, mods_ok))
    return 0


def segment(obj, lines):
    """the whole _TEXT against the game, placed by the first MATCHed public: statics and padding included"""
    recs = [r for _, r in L.modules(open(obj, 'rb').read())][0]
    segs = L.code_of(recs)
    if '_TEXT' not in segs:
        return ''
    buf, mask = segs['_TEXT']
    offs = {n: o for s, o, n in objmatch.publics(recs) if s == '_TEXT'}
    for l in lines:
        m = re.match(r'^(\S+)\s+\d+ bytes\s+MATCH at 0x([0-9a-f]+)', l)
        if m and m.group(1) in offs:
            base = int(m.group(2), 16) - offs[m.group(1)]
            g = IMG.code[base - IMG.code_base:base - IMG.code_base + len(buf)]
            d = sum(1 for i in range(len(buf)) if not mask[i] and buf[i] != g[i])
            return ('whole module identical at 0x%05X' % base) if not d else '(module: %d bytes differ)' % d
    return ''


def main(argv):
    mods = [a.upper() for a in argv if not a.startswith('-')]
    if not mods:
        mods = ['ADLIBFX', 'AL_MIDI', 'AWE32', 'BLASTOLD', 'DEBUGIO', 'DMA', 'DPMI', 'FX_MAN', 'GMTIMBRE', 'GUS', 'GUSMIDI',
             'GUSWAVE', 'IRQ', 'LL_MAN', 'MIDI', 'MPU401', 'MULTIVOC', 'MUSIC', 'PAS16', 'PCFX', 'PITCH', 'SNDSCAPE',
             'SNDSRC', 'TASK_MAN', 'USER']
    if '--lib' in argv:
        return check_lib(mods)
    return check_ww(mods, '-v' in argv)


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
