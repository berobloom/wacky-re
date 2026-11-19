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
   that is only stored to a 16-bit global. Data point (com_open 0x102F4, iter 33): its three parameter
   slots (+4/0/+8) became the game's +8/+4/0 when an unrelated register-allocated `char mcr` temp was
   added in the middle of the body, and went back when the temp was removed - the parameter permutation
   follows the whole allocation state, not the parameter list, so com_open is no longer an example
   of this question (13 rows left, all register homes).
   Data point (engines_menu 0x167D8): the game keeps a multi-assigned int local in EDX (xor edx / mov $1,edx /
   movsx edx / mov edx,[esp]) while our compile spills it to slot 8 with identical C; `register` and declaration
   order did not change it. Data point (view_at_curpos 0x2F408, iter 20): the same shape with NO
   locals at all - the game does `movsx edx,[v7d610]` for the first operand of a `short + short` sum,
   ours `movsx eax,[v7d610]; mov [esp],eax; mov edx,[esp]` in the slot the next sum reuses; 30
   spellings (both operand orders, statement order, casts, temps for the sum and for each first
   operand) leave it, and every local grows the frame to `sub esp,8` where the game has one slot, so
   no C form is left there. screen_menu 0x16984 MATCHED only when the loop result got a second single-block
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
   Data point (ent_sprites_load 0x21310): the game recomputes `imul ebp,edi,0x28` for `sprattr[i]` inside
   the inner loop; ours strength-reduces it to an EBP pointer stepped in the outer loop. Eleven C forms
   tried, none keeps the imul. Same kind as fn_16f90 (b): is the game built without loop SR here?
   Data point (line_points 0x21554): the game keeps a short dx in ESI and the int count n on the stack;
   ours swaps them (n in EAX, dx spilled). Declaration orders, `register`, initializers do not move it.
   Data point (players_project 0x255d4, near miss ~130 instrdiffs): the game promotes the global loop
   pointer v7d800 (`for (v7d800 = players, v88b72 = 0; ...; v7d800++, v88b72++)`) into EBX, storing it
   before the in_sector/isqrt calls and at the exit; kart_draw 0x24bcc gets the same promotion of v7d868
   in our build, players_project does not, even cut down to the bounds test and the in_sector call, and
   not with a local short counter (`v88b72 = i;` in the body) like kart_draw's either. Same in
   ents_project 0x25a78 (v7d7f4/ECX) and shots_project 0x2632c (v7d874/ECX), while shots_move 0x2602c,
   anim_tick 0x261fc (v7d7f4, v7d800 and even twoplayer) do get it. Common to the three that fail (LIKELY
   the cause, untested): they are the
   ones that call in_sector and isqrt with the pointer live across the calls.
   Ruled out as levers (iter 25, .work/probe_nkp, .work/probe_fi, .work/probe_is): explicit register
   variables - WCC386 9.5b has none in any scope (function scope E1009, file scope E1059 "Invalid
   storage class for non-local variable", and without `register` E1009 + E1011); the order of the
   initializers in a comma `for` (only the two zeroing `xor`s change order, fade_in 0x32cf8 keeps
   i=EBX/k=EDX both ways); and source position relative to a branch (a statement written below a
   branch sinks past the whole branch, so the four tbl6e858 loads sitting in in_sector's branch shadow
   at 0x24a33-0x24a46 are not reachable that way). What would settle it is unchanged: the allocator's
   own order, not the C.
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
   More (iter 14): shots_clear 0x2d4fc (v7d874/EBX, one store at the end) and shot_fire 0x2d548
   (v7d874/EDX, stored before shot_step, reloaded after, stored at both exits) walk shots[] with the
   global itself; written with a local pointer they differ in registers only (near misses).

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
6. **WORKAROUND FOUND, cause still open** (iter 14): declaring the function a second time under
   another C name with `#pragma aux alias "real_"` and calling the alias reproduces the game's
   `call` (the inliner never sees the alias's body; the fixup names the real symbol). This matched
   `dial`, `link_open` (com_getc), `modem` (com_getc) and `kart_state`'s kart_anim2 calls, and with
   `#define readdat readdat_x` (plus seekdat/telldat/datsize) after the definitions it fixed
   fn_17928's readdat cause and the telldat trap at 0x194c8. Still open: why the game's build
   calls them at all (a second module stays the likeliest original). Original question:
   what makes the game's build *call* its small
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
   New evidence (iter 14), LIKELY: the pair is not inlined by the compiler at all. `wait_connect`
   0x1dca0 (byte-identical) has the pair's body five times and calls it twice, the calls only in
   the escape branch (0x1de6b/0x1dea6), and `modem` has the same mix. Both rebuild byte-identical
   with the bodies written out and `fn_1c068`/`fn_1c0ac` kept `extern`. That reads as a source-level
   copy (a macro or pasted code) next to real calls, not an inline budget. If so, the question is
   only why our `-oe` inlines a *defined* `fn_1c068`, and the two-module test settles it.
   `com_getc` 0x105b0 is the same case: called at `link_open` 0x1dc7f and `dial` 0x1dfc2 (the only difference left
   in either) and at the `net_end` family, while `com_waitfor` inlines it.

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

13. **OPEN** (src/ww.c `main_menu`): why does the game's build keep a constant in a register
    instead of folding it? 0x1f950 `mov edx,9` before `call ents_init`, then `movsx eax,dx` as
    menu's first argument at 0x1f9b2, 0x1fa89 and 0x1fadf. The same DX saves pal_a around every
    f375cd fade (0x1fb6f, 0x1fc43, ...). Our 9.5b build folds the 9 (`mov eax,9`) and saves pal_a in
    BX. PART ANSWERED (iter 36): f375cd 0x375cd is pushal..popal, so `void f375cd(void)` lets DX hold
    the save across the call (main_menu 379 -> 341 rows); the `mov edx,9` part is still open. Tried and failed: one shared short local for both uses, separate locals, `int sel`, six
    declaration orders. Possibly the same build difference as QUESTIONS 6 (calls the game keeps that
    our `-oe` inlines). Test: 8 fade blocks and the three menu(9) calls in 0x1f858 match.

14. **OPEN** (src/ww.c `fn_182d0`, findings/menus.md): what makes the game's build keep a dead copy
    of an incoming register? 0x182d4 `mov esi,ebx` is overwritten two bytes later by
    `imul esi,ebx,3` (0x182d6) and ESI is only ever read as the word `si` (0x182f4 `cmp si,bx`), so the
    copy feeds nothing. CONFIRMED: the two instructions and their addresses (WW.EXE code object,
    `.work/le/ww.obj1.bin`); SPECULATIVE: that it is the same build difference as the
    `xor edx,edx` of music_jingle 0x11884, the two hoisted loads of track_setup 0x124e8 and
    QUESTIONS 13. Tried and byte-identical: 12 spellings over two sessions (`t = a3`, comma,
    `register`, the expression in both loops, `lim = a3;` then `lim *= 3;`, `lim = lim * 3`,
    `lim = (short)(a3*3)`, a `long n = a3` read by `if (n == a3)`, a `long t = a3` as both loop
    bounds, an `int` counter, two limit locals). Test that would settle it: any build option or
    front-end (WPP386 vs WCC386, `-ox`?) that makes one of these copies appear.

15. **OPEN** (src/ww.c `pal_shift`, findings/video.md): who calls `pal_shift` 0x114a8, and what is
    in the 768 bytes it scales? CONFIRMED: the function is byte-identical to `v = *p; v <<= 2;
    *p++ = v;` over `for (i = 1; i <= 768; i++)` (0x114a8-0x114c5, `cmp bx,0x300` + `jle`); no
    `call rel32` names it and no dword 0x114a8 appears in either code object (scan of
    `.work/le/ww.obj1.bin`, `.work/le/ww.obj2.bin`). SPECULATIVE: it is dead code kept by the link.
    Test that would settle it: scan the LE fixups/relocation records (not the applied bytes) for
    0x114a8, and search for a far-pointer table whose offset field is 0x114a8 with a nonzero
    selector.

16. **ANSWERED as far as the shipped files go** (iter 56, findings/formats.md "SETUP.EXE"):
    SETUP.EXE never reads or writes the record area either (field census empty), so no shipped
    program explains the order; it stays SPECULATIVE authoring data. Original question: why do the 24 default records of `WACKY.ING` start their class
    groups at slot 0 / 4 / 7? CONFIRMED (byte dump of the DAT entry `WACKY.ING`, decoded with the
    `tbl77758` record shape): the block is a 0x42 header + 24 records of 0x2e = 0x492 exactly; every
    record is ("MAR 1994", "KNOCK ME OFF!", f04 = 0) and (f00, f02) runs 1/0-7, 2/4-7+0-3, 3/7+0-6.
    Also CONFIRMED (`tools/le/rangexref.py ww 0x7ee1c 0x7f2ae`): nothing in WW.EXE touches the
    record area, so this table is data the game carries but never reads. SPECULATIVE: the rotated
    start slot is an artifact of whatever wrote the file, not a game rule. Test that would settle
    it: `SETUP.EXE` (a bound DOS/4GW LE, not 16-bit) - if it rewrites `WACKY.CFG`'s record area, its
    writer shows the intended order.

17. **ANSWERED** (iter 56, from SETUP.EXE statically; findings/formats.md "SETUP.EXE"):
    `cfg7b6d8` = 2 players x 6 key slots (steer left, steer right, accelerate, fire, brake,
    hand-brake turn), scancode in the low byte; `cfg77128` holds the six 14-byte key names per
    player at +0x30a, the baud rates, the modem strings and the joystick flags. The live
    test below is no longer needed for the slot order. Original question: what are `cfg7b6d8` 0x18 (12 words) and
    `cfg77128` 0x630 for? CONFIRMED: the CFG's 12 words are BIOS scancodes `4b 4d 38 1d 50 39 20
    21 1e 1f 22 23`; `main` puts the block's address into `v7d8ac`/`v7d8b0`(+0xc)/`v7d8a4`/`v7d990`
    (0x35d39-0x35d57) and `cars_menu` 0x1a380 tests its bytes indexed (0x1abc2, 0x1b3db);
    `ctl_setup` 0x14b00 reads `cfg77128` +0x08..+0x12 (0x14ba6-0x14bf4) and `main` requires
    `w77430 == 0xfa` (0x36072); control names ("LEFT CURSOR", "ALT", "SPACE", ...) sit inside
    `cfg77128` at 0x77432. LIKELY: the per-player key map and its names. Test that would settle
    it: run `SETUP.EXE`, change two keys, diff `WACKY.CFG` against this copy (the diff offsets name
    the slots).
18. **OPEN** (findings/formats.md ".SIN"): why does every `.SIN` record start with four bytes the
   engine throws away, and what are they? CONFIRMED: `track_load` reads 4 bytes then 8 per record
   over 108 iterations (0x27d7b-0x27dbc), the only reader of the table is `f37b04` (0x37b80), and it
   uses only the two kept dwords; the dropped leading value is 0x1, 0x10000 or 0x10001 (two 0/1
   shorts; 1544 / 476 / 2516 records over all 42 tracks), so it is inert in the shipped engine and
   must come from whatever wrote the tracks. Also CONFIRMED inert: `v88ba0 = ncells / 4` -
   `tools/le/xref.py ww 0x88ba0` finds one store (0x27d09) and no read.
   Test that would settle the first: a track editor's file, or the same fields in another Apogee
   title's data; nothing in WW.EXE can read them.
19. **CLOSED** (iter 56c; findings/formats.md "The twelve zone-less slots"): what are the 12
   track slots 16-21 and 37-42? Six duplicate art pairs with an all-zero `.POS` map, and **never
   loaded**: `fn_17928`'s arms give tracks 1..15 (regular) and 22..36 (bonus pack, `v88b88` +
   0x15/0x1a/0x1f), exactly the 30 slots with zones. CONFIRMED live: `v7ecb4` = 3 (zones_menu's
   BONUS PACK value) and pick 1 load track 22 with BONUSB.SP. Iter 53's "only 1..30, 16..21 from
   the bonus arms" added 0x15 as 15. Only `release_check` touches 42/20 by name.
   SPECULATIVE: which release each copy was shipped for.
20. **CLOSED** (iter 56c; findings/formats.md ".PAR"): the 25600-byte `.PAR` is a 1280x20 8-bit
   horizon panorama, row-major. CONFIRMED live: the four writes per address are planes 0..3 (a
   plane read equals `22.PAR[1::4]` in all 6400 bytes); the asm copier 0x395dd sends a 20-row,
   320-pixel window from `v7d860` to screen rows 110..129 each frame, and the screenshot matches
   that window (93.9%, the rest karts and HUD). The earlier LIKELY 320x20 at 4 bytes a pixel was
   wrong.
21. **OPEN** (findings/formats.md "SPRITE.ATR"): what do `kind` 3..7 do in the race? CONFIRMED: the
   record field at +0x00 partitions the 36 sets - kind 0 the twelve one-frame OB sets, kind 1 OB1.SP
   alone, kind 2 the eighteen animated H sets, and kinds 3,4,5,6,7 exactly one set each: H1.SP (id 0,
   the only set in all 42 `.SPW` lists, 69 instances), H15.SP, H12.SP, H16.SP, H20.SP; the game reads
   the field only at 0x2139d (`v7eda4` = first kind-2 id), 0x21439/0x21454 (`ents_flags` 0x213f0:
   `kind >= 2` clears `ents[].f2` in an off-line KID MODE race, `kind > 2` is rewritten to `v7eda4`
   under `netvoices`) and in the hit tests (`src/ww.c:9038,9043,10121`, `kart_state`'s `k != 1` /
   `k == 2` arms). Also CONFIRMED unused: +0x02 (10 in all 36), +0x08/+0x0a (copies of w,h) and
   +0x18..+0x23 (zero) - no read of those offsets exists in 0x10260-0x36a60. Test: read the `kind`
   branches per function offline and state, per kind, what the player notices (shootable? score?
   blocks the kart?).
22. **RESOLVED** iter 67 (six switch tables that sit inside MATCHed functions; `tools/wc/fixsites.py
    --arms NAME`, iters 66-67): all 54 entries decided - 14 `kart_frame` by hand (iter 62), 4
    `fn_19db8` (iter 66), and the last 36 (`cars_menu` 8, `kart_fire` 9, `main_menu` 13, `trial_book` 6)
    by comparing the two blocks an entry points at: each side's 4-byte fields resolved to the address
    that side means, jump targets and fields pointing into a near-miss body masked, the numbers in the
    op_str masked (the game folds a base into a displacement), 1- and 2-byte immediates included. One
    BUG came out of it: `cars_menu`'s carstat==0 slide arm lost the store `ents[i].frame = 2`
    (game 0x1b7de, findings/car_select.md) - no row count and no address-set census saw it, the global
    `ents` is read by both sides either way. The other 35 pairs are same-shape: the game keeps values in
    registers we re-read or reload (v7eb9c in ECX, curpos hoisted 0x24613), uses another register (the
    voice number ESI vs CX), and schedules independent stores differently - in the four `main_menu`
    order-only blocks the read-before-write order of every touched address is kept.
    The 36 entries and how they were decided, per function, are in `src/nearmiss.txt`'s `BEHAV:` cells.
    Old text follows.
22b. (iter 66, open then) six switch tables that sit inside MATCHed functions:
   Watcom emits a switch's table *in front of* its function, so those dwords are inside the preceding
   function's bytes and MATCH there while their targets are arms of a near miss: `cpu_drive_` 14 ->
   `kart_frame` (the table at 0x29068), `ctl_label_` 4 -> `fn_19db8` (0x19da8), `fn_1a2f8_` 8 ->
   `cars_menu` (0x1a360), `kart_boost_` 9 -> `kart_fire` (0x24340), `main_menu_names_` 13 ->
   `main_menu` (0x1f824) and `rows_pad_` 6 -> `trial_book` (0x2e4a0) - 54 entries in all; the tables'
   each table lies in the code between the preceding named function and the switch function. The byte census cannot decide
   them: a near miss's blocks are laid out differently, so its intra-function offsets mean nothing
   (`kart_frame` arm 0: our +0x853 against the game's +0x84b). The one entry that left its function is
   resolved, not open: the game's 0x1f844 holds 0x16978 = `engines_menu`'s epilogue, which
   `main_menu`'s case 9 shares (src/ww.c's main_menu header, GOTCHAS "copies in one probe share
   their identical tail"); fixsites adds that our entry for the same case lands on `main_menu`'s own
   copy of those nine bytes, `add esp,8` + `pop ebp/edi/esi/edx/ecx/ebx` + `ret` (our +0x1a74), so it
   is same-shape.
   `ctl_label_`'s 4 entries into `fn_19db8` are resolved same-shape (iter 66, `fixsites.py --arms`:
   the game's arms 0x19dde/0x19e02/0x19e37/0x19e6f open with `mov dword [v7ece0], 0x14/8/4/1` and ours
   - our +0x26/+0x4b/+0x81/+0xb8 - set the same four constants in the same order; only the intra-block
   offsets differ, by 0-2 bytes).
   What would settle the rest: arm by arm against the disassembly, as iter 62 did for `kart_frame`'s
   14 (same animation table per case, `findings/game-loop.md`); `cars_menu` (8 entries, table 0x1a360),
   `kart_fire` (9, 0x24340), `main_menu` (the other 12, 0x1f824) and `trial_book` (6, 0x2e4a0) are
   unchecked - 35 entries.

23. **OPEN** (iter 68, `tools/wc/datamatch.py`, findings/executables.md "Data layout (obj2)"): the
    initialized data our build does not emit. CONFIRMED census: obj2 holds 7869 initialized bytes in 43
    regions no group of ours covers, and our one group (0x60004-0x6235A) equals the game's at every byte
    something reads. Most of the 43 are library data that comes in with the real libraries (the Apogee
    Sound System's error strings 0x6235C-0x631D1, DOS/4GW's math strings 0x63004-0x63147, clib's `_iob`
    0x6400C); two sets are the game's own C: the credits table (`extern char credits[][0x2c]`,
    src/ww.c:8086, 0x632F0-0x63D5D) and the race-result praise strings at 0x63D44 / 0x63E0C / 0x63ED4
    (check.sh's `unnamed arr63d44`, `arr63e0c`, `arr63ed4`). Nothing is wrong today: `tools/link/delink.py`
    writes those bytes into BLOB.OBJ, whose data segment opens DGROUP at 0x60000 (tools/link/build.sh), so
    the hybrid and racetest see the game's own values. What would settle it: for a link without BLOB.OBJ,
    define them in their own object the way tools/link/extra.c does the two page-flip functions - not in
    `ww.c`, where adding literals moves every later literal of its CONST group and breaks the
    327-references base check. Unknown neighbour: the three `01` bytes at 0x60000-0x60002 (four dwords in
    the image hold 0x60000: 0x35580, 0x47692, 0x476c6, 0x4d636; none of the other three addresses).
