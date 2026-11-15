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
   Same open question in sounds_load, wtext, ents_show, voices_load (registers). Counter-evidence on types:
   print_at 0x148F4 got its slots (x +4, mode 0) only with `short x` instead of `int x`, while names did
   nothing there. So types of parameters do move slots in some functions; try `short` for each parameter
   that is only stored to a 16-bit global.

## Hypotheses

- LIKELY: <claim>. For: <evidence>. Against / gap: <what is missing>.
  Test: <the experiment that decides it>.
