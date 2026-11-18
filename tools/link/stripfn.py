#!/usr/bin/env python3
"""stripfn.py SRC.C OUT.C name[,name...]: copy a C file without the definitions of the named
functions, leaving a prototype-free hole (the callers' existing declarations stay valid).

A definition is a line at column 0 that contains `name(` and is not a declaration (does not end
in `;`), followed by the body up to the next line that is exactly `}`. Used by tools/link/build.sh
(ORIG=...) so that tools/link/delink.py can supply the game's own bytes for those functions."""
import re
import sys


def strip(text, names):
    lines = text.split('\n')
    # a function published under the game's name by `#pragma aux local "name_"` is stripped by
    # its local name (showlines_def -> showlines_)
    alias = {m.group(2): m.group(1) for m in re.finditer(r'#pragma aux (\w+) "(\w+)_"', text)}
    names = [alias.get(n, n) if not re.search(r'^[A-Za-z_][\w \t\*]*?\b%s\s*\([^;]*$' % n, text, re.M) else n
             for n in names]
    out, i, found = [], 0, set()
    while i < len(lines):
        ln = lines[i]
        m = None
        # a definition header: type words, then the function's own name and its parameter list,
        # with the body's `{` alone on a line within the next 4 lines and no `;` before it
        h = re.match(r'[A-Za-z_][\w \t\*]*?\b(\w+)\s*\([^;]*$', ln)
        if h and h.group(1) in names and not re.match(r'(extern|typedef|static\s+\w+\s*\()', ln):
            k = i
            while k < min(i + 5, len(lines)) and lines[k] != '{' and ';' not in lines[k]:
                k += 1
            if k < len(lines) and lines[k] == '{':
                m = h.group(1)
        if m:
            j = i
            while j < len(lines) and lines[j] != '}':
                j += 1
            if j == len(lines) or j - i > 3000:
                raise SystemExit('stripfn: no closing brace for ' + m)
            if sum(1 for x in lines[i:j] if x == '{') != 1:
                raise SystemExit('stripfn: %s: more than one body between line %d and %d' % (m, i + 1, j + 1))
            out.append('/* %s: taken from the game (tools/link/stripfn.py) */' % m)
            found.add(m)
            i = j + 1
            continue
        out.append(ln)
        i += 1
    missing = set(names) - found
    if missing:
        raise SystemExit('stripfn: not defined here: ' + ' '.join(sorted(missing)))
    return '\n'.join(out)


if __name__ == '__main__':
    src, dst, names = sys.argv[1], sys.argv[2], [n for n in sys.argv[3].split(',') if n]
    open(dst, 'w', encoding='latin1').write(strip(open(src, encoding='latin1').read(), names))
