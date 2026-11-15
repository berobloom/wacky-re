# Menus

Status: OPEN (the two bonus menus are read; the main menu chain and fn_1f858 are not).

## Summary

Two "order the bonus content" screens share one shape: they print three
26-byte message lines into 0x7c854/0x7c890/0x7c8cc, load a PCX palette from
the dat, copy 768 bytes into a local buffer, then flash palette entries with
fn_182d0 while the player moves a selection, and finally copy the worked
palette to p7d718 and set v88b9e. `fn_1832c` (0x1832c) picks a race pack and
a sound card; `fn_193a8` (0x193a8) picks a bonus zone (v88c96). Port: both in
`src/ww.c` (near misses, see the `differs:` notes there).

## Findings

- CONFIRMED (code 0x1941c-0x19466 and 0x18404-0x1844e):
  both menus copy the same two 26-byte strings (" PLEASE SEE MAIN MENU TO ",
  " FIND OUT HOW TO ORDER   ") into 0x7c854/0x7c890 and differ only in the
  third line: " THESE EXTRA RACES.      " (races) vs " THESE EXTRA ZONES.      "
  (zones) at 0x7c8cc. `strcpy` with a literal is compiled to
  `mov ecx,6; rep movsd; movsw`.
- CONFIRMED (code 0x19480-0x194db): the zones menu's palette comes from one dat
  record, named "BSBCS.PCX" when v7ecb4 != 0 and "SBCS.PCX" otherwise; it seeks
  to `telldat() + datsize() - 0x300` and reads 0x300 bytes - the PCX palette
  tail - into the buffer `pal7d848` (0x7d848) points at. The races menu instead
  builds "SB<n>.PCX" names with inline strcpy/strcat chains (0x18471-0x18500, then datfind 0x18501 / telldat 0x18506 / datsize 0x1850d / seekdat 0x1851e / readdat 0x18525).
- CONFIRMED (code 0x18326): fn_182d0's 5th argument (the palette base) is pushed
  by the caller and popped by the callee, `ret 4`; the call sites therefore have
  no `add esp,4`, in the game and in our build.
- CONFIRMED (code 0x1956e-0x195a7, 0x19a19-0x19b63): the flash loop resets
  palette entries 5..0xa (fn_182d0 with a4=0, self-copy), then highlights entry
  v88c96-1 with a4=1 (copy from pal7d848). Inside the menu loop the entry under
  the cursor is un-flashed/highlighted again every >12 iterations, alternating
  a4 through the local at esp+0x328; each nav key flashes once, immediately.
- CONFIRMED (code 0x19aa5, 0x19b23): v88c96 is the zone selection and wraps
  6 -> 0xb and 0xb -> 6, i.e. twelve choices numbered 6..11.
- CONFIRMED (code 0x19b68-0x19bb6, 0x19c53): pressing enter (joy88b40) always
  plays voice 0x12, and the menu returns *without* setting p7d718/v88b9e unless
  netgame == 0, v7e574 != 0 and v88c96 > 7 - only that path plays voice 0x1a,
  calls showlines(3,1,0) and falls through to the exit. So zones 6 and 7 cannot
  be entered from this menu.
- LIKELY (code 0x197eb, 0x19b91: the same `> 7` test in the netgame==1 branch):
  v88c96 6/7 are the free (already ordered) zones and 8..0xb the orderable
  bonus zones. Gap: nothing in this function maps v88c96 to a race; the
  consumer is fn_1f858 (call sites 0x20286, 0x2049e), not yet read.
- CONFIRMED (code 0x196c6-0x1972e, 0x19781-0x197e5, 0x1992a-0x199a2): the two
  menus navigate differently. The races menu uses key88cd3/key88cd6; the zones
  menu uses key88cd5/key88cdd, also set from cfg7b6d8[1]/cfg7b6d8[3]
  (v7b6d9/v7b6db, 0x196d8/0x196e8) and from the peer's key byte when
  netgame == 2 (v88cef bits 1, 2, 4, 8, 0x10, 0x20, 0x40 -> key88cd5,
  key88cdd, joy88b40, joy88b42, joy88b44, joy88b46, v88cec).
- CONFIRMED (code 0x199c7-0x199fb, with wait_or_key 0x14c7c): after any input
  the menu waits up to 20 ticks, ending early when key88cd0 == 1 (which also
  sets v7ebb0 when connected). The game has wait_or_key's body inline here;
  there is no call to 0x14c7c inside fn_193a8.
- CONFIRMED (code 0x19633-0x19672): at the top of each menu iteration the game
  waits 8 ticks (or a key88cd0 press, which sets v88cec); that is the menu's
  frame limiter.
- STRONGLY SUPPORTED (code 0x19882, 0x19895): v7ecc4 counts menu iterations;
  after the first it is clamped to 2 and switches v7ed20/v7ed1c from 0x550/0x2a8
  to 0x88/0x220 (the net-game tick rates) - the same pair the races menu sets
  at 0x18365 to 0x440/0x2a8.

## Traps

- fn_182d0 takes 4 register arguments plus one on the stack and pops it itself;
  reading a call site without checking that makes the stack slot map of the
  caller look wrong (the caller's locals are one frame offset after the push,
  e.g. `mov [esp+0x32c],0` writes the game's local at frame+0x328).
- The zones menu returns *without* p7d718/v88b9e on most enter presses; a port
  that always sets v88b9e lets the player "enter" a zone they cannot order.
- The two menus look identical in a decompiler listing but use different key
  globals (key88cd3/cd6 vs key88cd5/cdd); Ghidra names neither.
