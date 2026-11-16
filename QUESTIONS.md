# Open questions and hypotheses

Numbered; never renumber. When one is answered, mark it **RESOLVED** with a
pointer to the finding, and leave it here so the number stays valid.

## Questions

1. **RESOLVED** (findings/executables.md, Options): `-oa` was missing; `crc = (crc << 8) ^ table[idx]; return crc;`
   matches with `-s -3r -oailrt`. The question was: crcstep 0x102CC (`(crc >> 8) ^ b` stored to the `short` at 0x88CFA, then `(crc << 8) ^ table[idx]`): the
   game keeps `crc` in `eax` and copies `b` to `ebx` (with a dead `mov ebx,edx` after the xor); every C form tried
   (about 30: operand orders, casts, temporaries, parameter types, C++, 9.5/9.5a/9.5b, every `-o` letter) keeps
   `crc` in `ebx`. What would settle it: find the source form (perhaps a macro or an `unsigned char`/`int` mix
   not tried yet), or the compiler state that flips the register choice; the CRC module's other two functions
   (0x10260, 0x10298) compiled with it in one file may change the allocation.
2. com_open 0x102F4: src/ww.c has the game's 520 bytes except that the parameters `port`, `irq`, `comnum` sit in
   stack slots +4/0/+8 (game +8/+4/0), and the stores around them are scheduled to match. The slots change with
   the order the locals are declared in and with the parameter names (`p, q, baud, r` gives 0/+4/..), not with
   statement order or parameter types. What would settle it: learn Watcom 9.5's slot order (a symbol-table
   order: hash or declaration list?) from small probes, then pick names or declarations that give +8/+4/0.
   Same open question in sounds_load, wtext, ents_show, voices_load (registers). Data point (chat 0x14D00,
   session 2026-08?): arrays `mine,theirs,blank,buf,ch` all `char[80]` got slots (low..high) ch@0, buf@0x50,
   mine@0xa0, theirs@0xf0, blank@0x140; the game's same-named buffers sit mine@0, blank@0x50, theirs@0xa0,
   buf@0xf0, ch@0x140. Neither declaration order nor reverse-of-first-use explains ours. Counter-evidence on types:
   print_at 0x148F4 got its slots (x +4, mode 0) only with `short x` instead of `int x`, while names did
   nothing there. So types of parameters do move slots in some functions; try `short` for each parameter
   that is only stored to a 16-bit global.
   Data point (engines_menu 0x167D8): the game keeps a multi-assigned int local in EDX (xor edx / mov $1,edx /
   movsx edx / mov edx,[esp]) while our compile spills it to slot 8 with identical C; `register` and declaration
   order did not change it. screen_menu 0x16984 MATCHED only when the loop result got a second single-block
   variable (`m = menu(...); sel = m;` then switch on m) — switching on the loop-carried variable made Watcom
   reload from the slot instead of using EAX. Tree shape: `if (m == 3) break;` before `switch (m)` with two
   cases gives cmp3/je + cmp1/jb/jbe; a 3-case switch probes 2 first (game probes 3 first, so 3 is NOT a case).
   Also: functions the callers never read eax from are `void` (no xor eax at returns; zones/engines).
   Data point (fn_16ca0 0x16CA0): every statement matches; the game's scheduler defers `sar eax,1` and
   the v88c7a store across the independent movw $0x78, and homes the v7ea58s copy in DX (reused later as
   the loop counter) while ours uses AX. Same flavor as engines_menu's EDX. Loop bodies byte-identical.
   Data points (fn_16f90 0x16F90, near-miss 1020/1075): (a) the game CSEs the immediate 0 into %cx and
   stores it 8x as word (`mov %cx,0x6aaf2(%eax)` ...) while our -oneatx build emits `movw $0,disp` per
   store; no C form found that makes Watcom keep a zero register. (b) The game's zeroing do-while keeps a
   byte counter in EDX and indexes [edx+ebx+0x6aab6]; ours strength-reduces to [eax+disp]. (c) Loads of
   order_a[k]/order_b[k] are interleaved between stores in the game, before the f1c store. Struct player
   is now offset-complete (pack(1), size 0x68; an extra 2-byte field showed up as imul $0x6a in the
   listing - the struct size is checkable via any imul $0x68 in matched callers).   Data points (fn_173c4 0x173C4, near-miss 1080/1183): Watcom sinks `mp = myplayer; q = (int *)v7d868;`
   below two later constant stores (game emits them first, both in declaration form and assignment form);
   the game also keeps the local short `mp` homed in DI across calls while our build reloads [myplayer]
   after every call (same for `q` = EBP loaded at entry, written back at exit). fread argument registers
   EAX=ptr, EDX=size, EBX=count, ECX=stream CONFIRM the Watcom order arg1=EAX arg2=EDX arg3=EBX arg4=ECX
   (findings/executables.md corrected).


Data points (fn_193a8 0x193A8, near-miss 2072/2240 bytes): (a) the game keeps the global v7ebc0 in
   EBP across the menu's loops (10 `mov 0x7ebc0,%ebp` at loop entries, 5 `mov %ebp,0x7ebc0` at exits;
   wait_or_key 0x14c7c does the same in EDX and matches) while our 9.5b build of the same C emits
   plain memory reads there - the promotion depends on the whole function, and no local-copy C form
   reproduces it. (b) frame 0x32c vs our 0x310 with no [esp+0x30c..0x327] reference in the game's
   code; an unused array we declare is dropped from the frame, so the 28 bytes are not filler.
   (c) the game homes the loop counter in SI and the selection index in DI; ours puts them in EDI
   and a stack slot, changing every [esp+disp] encoding around the fn_182d0 flash calls.
   (d) `telldat() + datsize() - 0x300` accumulates in EDX for us, in EAX for the game (0x194c8).
   Blocks that do match byte for byte (modulo the 6-byte shift): every input block, both nav blocks,
   the net_key/error blocks, the message copies and the palette copy loops.
   (e) ctl_label 0x19d0e: the game zero-extends a `== 0` comparison defined in a jump-target block
   straight into EAX (`sete al; movzx eax,al`); our build uses EBX and adds `mov ebx,eax` (+2 bytes,
   and every jump after it then differs). Probed without change: `x == 0`, `!x`, `?:`, `short`/`char`
   /`register` on the variable, an `int radix` local, an unused third parameter, `!(X && Y)`, the
   condition re-written with `||`, and the 9.5 / 9.5a compilers (9.5 is worse: 328 vs 321 bytes;
   9.5a identical to 9.5b). What would settle it: the same register-choice rule as question 1.

## Hypotheses

- LIKELY: <claim>. For: <evidence>. Against / gap: <what is missing>.
  Test: <the experiment that decides it>.
