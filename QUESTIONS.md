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
   Data points (fn_19db8 0x19DB8, near-miss 45/153 bytes, 241 bytes = the game's length): the function
   matches instruction for instruction except that the game homes the local that ends in v88b84 in EDX
   (`mov dx,[0x88b84]` 0x19dbb, `mov edx,0x54` 0x19ded, `mov [0x88b84],dx`) and uses EAX as a temp for
   the switch value (`mov ax,[0x88bb2]` 0x19dc2, `dec eax`, `cmp ax,3`, `movzx eax,ax`, table jump),
   while our 9.5b homes that local in EAX and puts the switch value in EDX (`movzwl [0x88bb2],edx`,
   `cmp dx,3`); the entry loads are also in the opposite order. Probed without change (probe files
   under .work/probe19db8/, ~30 shapes): init before/after the switch, `switch (v88bb2)` on an
   `unsigned short` global vs `switch ((unsigned short)(v88bb2 - 1))` (only the cast and the unsigned
   global give the game's 16-bit `cmp ax,3`; a `short` global gives `movswl` + `cmp edx,3`), short /
   unsigned short / int / long / char local, `register`, a comma expression inside the switch, an extra
   unused local, an explicit empty default, per-case stores to the global instead of the local (then no
   entry load at all and `movw $imm,disp`), both statement orders in the cases (only `v7ece0; value;
   strcpy` gives the game's instruction order), ternary vs if/else (the game uses the arithmetic form
   for 0x59 + (v7ee5a != 1) and the branch form for the other two), and every -o<letter> on top of
   -oneatx. Same rule as question 1 and ctl_label: which pseudo gets the accumulator.
   New data point (classes_menu 0x19eac, this session): the home follows the *position of the first
   definition*. `short nrows` assigned before the fourth row's strcpy gets EDX like the game
   (0x19f22 `mov edx,4`, 0x19f43 `mov edx,5`, 0x19f75 `movsx eax,dx`); the same variable assigned at
   its use gets ECX, and `menu(nrows,0,0,0)`'s `xor ebx/xor ecx/xor edx` order changes with it.
   Declaration order, `unsigned short` and `register` do nothing there.
3. classes_menu 0x19EAC, the last remaining byte difference (one instruction of 149): the game emits
   `mov dword [0x7edb0],1` at 0x1a05d - between the eighth KID MODE help line's `mov esi` (0x1a04f)
   and its `rep movsd` (0x1a067) - while 9.5b puts it at 0x1a080, right before the `call showlines`;
   the other store of the pair, `v88bb2 = 1`, does land at 0x1a054 like the game's. What was
   probed without change (72 placements, four probe compiles, `.work/probe5`, `probe6`, `probe8`,
   `probe9`; score the variants with `tools/wc/variants.py`): the pair before each of the last six help strcpy's
   and after all nine, both statement orders; the second store wrapped in `if (v88bb2)`, `if (1)`, a
   folded `if/else` and plain braces (a branch the optimizer deletes, in case a block boundary pins
   it); and `v7edb0 = v88bb2` to make the store depend on the first store. In every case exactly one
   of the two stores slides into the copy's unit and the other stays where the C puts it, and which
   one follows the source order. What would settle it: what the scheduler stops a sink at (a use?
   a block boundary? a second string-op unit in the same window?) - or accept the 1-instruction near
   miss (the game's `mov dword` at 0x1a05d needs no register, so no register-pressure story explains
   it).
   Data point (laps_menu 0x1A148, matched this session): the pair `v88bb0 = 0xd; v88c80 = 1;` lands
   inside the third strcpy's string-op unit (0x1a1bb, 0x1a1c4, between its `mov esi` and its
   `rep movsd`) from ALL THREE source placements - before the first copy, between the second and the
   third, and after the third (`.work/probe11`, 30 shapes) - so the pair slides as far up as it can go
   and its source placement is not a lever; the classes_menu pair stops one unit earlier.
4. **PARTIALLY ANSWERED** (findings/car_select.md): the eight ints at `v7d338` are a
   **per-car lock** (car id 0..7). Evidence: `cars_menu` blocks the confirm control when
   `v7d338[car under the cursor] != 0` (0x1ac0e-0x1ac3f, 0x1b461-0x1b491) and plays voice
   `0x1a` plus the three `" PLEASE READ ORDERING INFO "` lines (0x1b493-0x1b4dc);
   `ents_show` draws the 9x0x36 `lp7d804` sprite over an ent whose flag is set
   (0x1462d-0x14659). The only writer is `fn_1a2f8` (clears 0..7, sets [0],[1],[5],[6]) when
   `netgame && v7ec9c == 1` or, off-net, when `v7e574` - and `v7e574` is CONFIRMED the
   shareware flag (1 when neither `42.GAM` nor `20.GAM` is in WACKY.DAT, 0x34463-0x344d4),
   so the shareware release locks four cars and the registered one locks none off-net.
   Open: why `v7ec9c == 1` locks cars 0/1/5/6 in net play (what `v7ec9c` counts), and why
   `sprptr += 0x10a0` for the icons whose per-car state is not 1 (0x1a6ad-0x1a6b8).

## Hypotheses

- LIKELY: <claim>. For: <evidence>. Against / gap: <what is missing>.
  Test: <the experiment that decides it>.
5. **OPEN** (findings/car_select.md "Rebuild status"): which rule gives `cars_menu`'s 27 scalar
   slots their order? The rebuild has the right frame size, the right statements in the right
   order (71 % of the disassembly rows agree once `disp(%esp)` and the relocation bytes are
   masked) and the same 26 spilled locals, but in a different permutation: ours 0x16c = the game's
   0x114, ours 0x160 = 0x140, ours 0x140 = 0x110, ours 0x174 = 0x150. Declaration order is not the
   lever (reversing it permutes the arrays only). Candidate levers not yet tried: the *order of
   first assignment in the source* (the game's first store is its 0x114 local, but its 0x110 local
   is stored first in phase 4), and locals that must stay live across calls (the game spills what
   the rebuild keeps in ECX/EDI/ESI).
   Test: a probe function with the game's 27 stores in the game's store order, then flip one pair
   at a time and diff the frame.
6. **OPEN, narrowed** (findings/video.md, GOTCHAS): what makes the game's build *call* its small
   user functions - `readdat` (0x1424c, called at 24+ sites, 11 bytes of body) and
   `fn_1c068`/`fn_1c0ac` (68/69 bytes, 11 sites each) - when Watcom 9.5b `-oe` inlines every
   user function of that size in our build? Data this session: adding `-oe=<N>` to the pinned
   option set gives 73 / 76 / 84 / 84 / 84 / 78 byte-identical functions at N = 4 / 8 / 16 / 24 /
   32 / 48 (bare `-oe`: 84), and **no level keeps `fn_1c068` a call** - so the threshold is not
   the lever. A bare `#pragma aux name modify [...]` on the definition and a `volatile` function
   pointer to it also do not stop it. Not yet tried: the module boundary (the game's program
   clearly holds several OMF modules - objmatch's `_TEXT` base groups - and the helpers may live
   in another one, where a call is forced), and `#pragma on(off)/inlining`.
   Test: compile `src/ww.c` as two modules (the palette pair in its own) and see whether the 11
   call sites come back without losing the 84.
   `com_getc` 0x105b0 is the same case: called at `link_open` 0x1dc7f (the only difference left
   in that function) and at the `net_end` family, while `com_waitfor` inlines it.

7. **OPEN** (findings/scores.md): what are the 46-byte records of `tbl77758`
   (0x77758, row 1 at 0x77ba8), who fills them, and are the two strings at +6 and
   +0x1a a name and the stamp `fn_1c994` builds? Evidence so far: `fn_1cad0` picks
   `row = v88bb6 + v7ecb4` and groups of 8 records (0, +0x170, +0x2e0); 0x1cfb4-0x1d01d
   copies words at +0/+2/+4 and strings from +6/+0x1a; nothing in the image takes the
   address of 0x77758 except four reads. Test: boot the original, dump 0x77758..0x77ff8
   before and after a race (STATUS Next 7), and compare the +0x1a field with the string
   `fn_1c994` builds - which also decides whether the month index (seconds - 1, 0x1ca08)
   is a real bug in the shipping game.

8. **OPEN** (findings/menus.md): who writes the control names at `ctl_names77432` (0x77432,
   `cfg77128` + 0x30a: eight 84-byte records of six 14-byte strings), and where do the strings
   come from? Evidence: all zero in the image; only two reads reference the address directly
   (`tools/le/xref.py ww 0x77432` -> `add edx,0x77432` at 0x1c43d and 0x1c52c, both in
   `fn_1c0f4`), and `fn_1c0f4` replaces them with "BUTTON3"/"BUTTON4" for a device-2 side
   (0x1c592) - so keyboard/joystick players must see per-action names. The neighbouring config
   fields are named (`musicvol` 0x7742c, `ctl774e4` 0x774e4), so the writer addresses the region
   through a computed base. Test: diff every write into `cfg77128` in the matched
   `save_config`/`save_ctl`/`ctl_setup` 0x14b00 for a +0x30a form, and boot the original and
   dump 0x77432..0x77584 at the car-select screen (STATUS Next item).

9. **OPEN** (src/ww.c `enter_name`, findings/scores.md): which C shape puts the seven locals of
   `enter_name` in the game's slots - xc 0x58, cy 0x5c, x 0x60, nx 0x64, lim 0x68, y 0x6c,
   cx 0x70 (ours: y 0x58, xc 0x5c, x 0x60, cx/lim 0x64, cy 0x6c, nx 0x70)? Evidence: the frame
   (0x74), the parameter spills (`n` 0x50, `heading` 0x54) and `x` 0x60 already match; 43
   declaration orders probed with `tools/wc/variants.py` permute but never match, so 9.5b
   assigns local slots from something other than declaration or first-touch order (GOTCHAS).
   Test: keep the statements fixed and vary one thing at a time (a variable that only ever
   appears as an argument, a `short` vs `int`, an extra unused local); a match is 21 diffs gone
   and `enter_name` byte-identical at 0x1cb44.

10. **OPEN** (src/ww.c `scores_screen`, findings/scores.md): which C shape puts the two spilled
    scalars in the game's slots - the game has `j` at esp+0x50 and `which` at esp+0x54, ours the
    other way round (6 sites of the 97 diffs)? Evidence: the frame (0x58), all three array slots,
    `i` in BX, `k` in ECX and each variable's own access shape (`mov dword [slot],0` / `movsx` /
    `inc dword` / `cmp word`) already match; three declaration positions for `which` (before the
    arrays, between arrays, after the scalars) all give the same permutation. The rest of the 97
    are EAX/EDX role swaps in the shift and insert blocks (0x1cfb4, 0x1d03b) and the rotated
    `k` loop (0x1cf8c-0x1cf91 `mov ecx,3; jmp` to the tail test). Test: 97 diffs gone and the
    function byte-identical at 0x1ce14 (793 -> 807 bytes).

11. **OPEN** (src/ww.c `race_records_menu`, findings/scores.md): what makes Watcom keep the pooled
    "" byte (0x600ac) of the 0x1d16f if/else in EDX in the game and in EAX in ours? That single
    register decides everything left in this function: with AL the four `mov [mnames slot],al` stores
    are 5 bytes and -oe tail-merges the arm's fourth `strcpy(&mnames[0xb4], "")` into the else arm's
    identical store (ours 504 bytes vs the game's 516, one block at 0x1d178-0x1d196); with DL the
    stores are 6 bytes and both arms keep a copy. Evidence: the two sibling functions differ here -
    the game's 0x1d340 shows the merged form (its if arm stores three, 0x1d386-0x1d390, the fourth at
    the join 0x1d3c6 in AL), its 0x1d13c does not. Tried and failed: six spellings in `.work/rrmv.c`
    (pointer temp for the fourth row, a 4-iteration loop, two separate ifs, an unused second
    parameter, plain assignments after the declarations, flipped condition - the last changes the
    branch to `je` as well). Test: the function byte-identical at 0x1d13c (504 -> 516 bytes).
12. **OPEN** (src/ww.c `fn_1d704`, findings/scores.md): what makes 9.5b spill the `short which`
    parameter of 0x1d704 to a stack slot after `f278c4` (0x1d7b5) when ours keeps it in BX? EBX
    still holds it at 0x1d7df/0x1d7fe, yet msgbox's row (0x1d843) and the switch (0x1d875-0x1d88f)
    read the slot. `msgbox` saves EBX (0x2a414), so no call clobbers it. Tried and failed (17 diffs
    is the best): K&R definitions with `short which` / `int which` in either order, a short local
    copy before the strcat and before the switch, `(short)which` casts on an int, `int sx`, local
    declaration orders. Same family as 9 and 10. Test: 0x1d704 byte-identical (930 -> 955 bytes).
