#!/usr/bin/env python3
"""shapescan.py: classify every fndiff row of the open near misses by the source shape it points at.

usage: shapescan.py DIR    DIR holds one `tools/wc/fndiff.py ww .work/src/WW.OBJ NAME` output per
                           near miss, named NAME.txt (examples of each class go to DIR/shape_ex.json)

Classes (iter 73): foldptr = the game adds a symbol to a register (`add r,@`): an INDEX loop over the
array where we walk a pointer (GOTCHAS); width = the same instruction at another operand size or
extension: a declared type differs (short/int, char/short); slot = only [esp+N]; reg = only register
names; moved = an unpaired row whose twin is unpaired on the other side (order/scheduling, where
evaluation-order differences land); other = the rest. Found showlines 18 -> 13, line_draw 4 -> 2,
cars_menu 431 -> 416.
"""
import re, os, sys, glob, collections
D=sys.argv[1]
REG=r'\b(e?[abcd]x|e?[sd]i|e?bp|[abcd][lh])\b'
def norm_regs(s): return re.sub(REG,'R',s)
def norm_slot(s): return re.sub(r'\[esp( \+ (0x[0-9a-f]+|\d+))?\]','[esp+N]',s)
def split(l):
    if '|' not in l: return None
    a,b=l.split('|',1)
    f=lambda c: re.sub(r'^\s*0x[0-9a-f]+\s+','',c).strip()
    return f(a), f(b)
def sizes(x):
    r=re.findall(r'(dword|word|byte) ptr',x)
    r+=['32' if re.fullmatch(r'e[a-z]{2}',t) else '16' if re.fullmatch(r'[a-d]x|[sd]i|bp',t) else '8' for t in re.findall(REG,x)]
    r+=re.findall(r'movsx|movzx',x)
    return r
def strip(x):
    x=re.sub(r'(dword|word|byte) ptr ','',x); x=re.sub(r'movsx|movzx','mov',x)
    return norm_slot(re.sub(REG,'R',x))
out=[]
for fp in sorted(glob.glob(D+'/*.txt')):
    name=os.path.basename(fp)[:-4]
    L=[split(l) for l in open(fp)]
    L=[x for x in L if x]
    tail=open(fp).read().strip().split('\n')[-1]
    m=re.search(r'(\d+) instruction diffs',tail); rows=int(m.group(1)) if m else -1
    c=collections.Counter(); ex=collections.defaultdict(list)
    ours_only=[o for o,g in L if o and not g]; game_only=[g for o,g in L if g and not o]
    no=collections.Counter(norm_regs(norm_slot(x)) for x in ours_only)
    ng=collections.Counter(norm_regs(norm_slot(x)) for x in game_only)
    for o,g in L:
        if g and re.match(r'add '+REG+r', @$', g) and o!=g:
            c['foldptr']+=1; ex['foldptr'].append(g); continue
        if o and g:
            if sizes(o)!=sizes(g) and strip(o)==strip(g):
                c['width']+=1; ex['width'].append('%s / %s'%(o,g)); continue
            if norm_slot(o)==norm_slot(g): c['slot']+=1; continue
            if norm_regs(o)==norm_regs(g): c['reg']+=1; continue
            if norm_regs(norm_slot(o))==norm_regs(norm_slot(g)): c['slot']+=1; continue
            c['other']+=1; continue
        x=o or g; k=norm_regs(norm_slot(x))
        other = ng if o else no
        if other[k]>0: c['moved']+=1
        else: c['other']+=1
    out.append((name,rows,c,ex))
cols=['foldptr','width','slot','reg','moved','other']
print('%-18s %5s '%('function','rows')+' '.join('%7s'%k for k in cols))
for name,rows,c,ex in sorted(out,key=lambda x:x[1]):
    print('%-18s %5d '%(name,rows)+' '.join('%7d'%c[k] for k in cols))
import json
json.dump({n:{k:v[:6] for k,v in ex.items()} for n,r,c,ex in out}, open(D+'/shape_ex.json','w'), indent=0)
