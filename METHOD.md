# METHOD: how to reverse engineer and port one thing

Read once per session, after QUEUE.md. Every rule here comes from a mistake
that cost a session in an earlier project.

## 1. What to trust, in this order

1. **The original's code**: the disassembly of the binary in `original/`
   (static), or the live code read from the running guest.
2. **Runtime evidence**: captures, save states, memory dumps and screenshots
   from the original under the harness, compared byte for byte.
3. **The port and its checks**: written by earlier sessions, can be wrong.
4. **Notes**: `findings/`, `QUESTIONS.md`, `STATUS.md`, `GOTCHAS.md`, commit
   messages. Notes are not proof, and have been wrong while sounding certain.

Before you build on a claim from group 4, re-check it against group 1 or 2.
When one turns out wrong, correct the note in the same commit ("WRONG,
corrected: ...") and never stack a new theory on it.

## 2. Evidence levels

Every claim in `findings/` carries one:

- **CONFIRMED**: shown by the code and/or a runtime experiment; say which
  (addresses, capture file, what was measured).
- **STRONGLY SUPPORTED**: the code says so, but it was not run or not seen.
- **LIKELY**: fits everything known, with a gap you can name.
- **SPECULATIVE**: a guess worth testing.

Never present an inference as fact. Prefer evidence from the binary and from
runtime experiments over manuals, box text, or other people's notes.

## 3. Before investigating a subsystem

1. Search `findings/` (start at `findings/INDEX.md`) and `QUESTIONS.md`.
2. Reuse what is CONFIRMED; do not re-derive it.
3. Only then start new analysis, and prefer the small experiment that tells
   competing hypotheses apart over a long trace.

## 4. The loop for one behaviour or bug

1. **Reproduce it** in the port, with numbers, not impressions. If you cannot
   reproduce it you do not understand it: do not fix anything yet.
2. **Find the port function** that produces the wrong numbers (grep, then read
   only that function).
3. **Disassemble the original routine** the port function claims to implement
   (its comment names the address). Compare instruction by instruction: every
   constant, branch direction, signed vs unsigned jump, byte vs word operand.
   The bug is almost always one mistranslated instruction (section 5).
4. **Fix only what the disassembly shows**, and quote the address and the
   instruction in the code comment. Never tune a constant until the output
   looks right: a calibrated constant hides the real bug until the next
   correct fix exposes it.
5. **Look at it.** Render the screen and open the image, before and after.
   Passing checks do not mean the screen is right.
6. **Pin it** with a check that fails on the old code. Put the bug back and
   see the check fail before you believe it.
7. **Sweep for side effects.** A physics or timing fix changes other places
   too. When a check that encoded the old buggy output fails, rewrite it to
   state the visible rule and say why; never just delete it.
8. **Record it**: the finding (with its evidence level) in
   `findings/<subsystem>.md`, the name in `analysis/<prog>_names.txt`, the
   trap (if any) in GOTCHAS.md or the finding's `## Traps`. Commit with a
   short subject; the reasoning goes in the findings file.

## 5. Translating x86 to C: mistakes that recur

- **Partial-register and byte ops.** `and byte [si+2],0xF0` on the low byte
  of a 9-bit x is `x & 0x1F0`, not `x & 0xF0`. `add/adc` and `sub/sbb` pairs
  are wider adds. A lone `mov byte` keeps the other bits.
- **Flags returned by helpers.** Read the helper's own `stc`/`clc` or the
  value it leaves for `je`, then the caller's `jb`/`jae`/`je`. Never trust a
  helper's name for which way it answers.
- **Signed vs unsigned.** `jl/jg` are signed, `jb/ja` unsigned. `movsx` vs
  `movzx`. A `cmp ecx,2 / jb / jbe / cmp ecx,4 / je` chain is a switch: map
  every value, including the ones that fall through to `ret`.
- **Left/right, up/down variants are separate code.** Disassemble both;
  never mirror one by hand.
- **`mov si,ax / shl si,1` indexes carry whatever is in AH.**
- **Every jump target**: follow it and write down what is there. `jmp` to a
  `ret` is not the same as jumping to an undo block.
- **Gates**: frame-parity or counter tests often guard whole handlers; check
  which side of the gate each instruction is on.
- **Tables behind pointers**: function-pointer and jump tables hide callers.
  An empty caller list is expected, not a tool bug.
- **Loop extremes**: tie-breaks (`>` vs `>=` when picking a maximum), the
  initial value of the accumulator, and what happens when nothing qualifies.
- **Watcom 32-bit**: args in EAX, EDX, EBX, ECX, then the stack; the callee
  keeps the other registers. Ghidra loses register arguments: read the call
  site.
- **Delays and timers**: see `harness/TRAPS.md` "Timing". Port BIOS-tick waits in whole
  ticks.

## 6. Writing a check that can fail

- Prove it fails first (mutation): put the bug back, or build the previous
  commit, and run it.
- Draw onto a blank buffer, not over a copy of the expected frame: a sprite
  drawn anywhere else leaves the compared box untouched.
- State the visible rule as well as the bytes ("no sprite inside solid cells
  over 1000 frames"). Bytes pin how; rules pin what the player sees.
- A check that runs a binary runs the binary under test (an env variable),
  so a mutation harness tests the mutant.
- Print the measured and the wanted numbers on failure.

## 7. Comparing port and original

- Pixel compare against a screen-buffer dump from the original, taken at a
  known point (a breakpoint or a stable screen), not against a screenshot of
  a moving scene.
- Use a fixed seed or the original's own RNG code under an emulator
  (`tools/le/emu.py`, Unicorn) to compare generators case by case.
- For a run under a fault (divide by zero, page fault), start a fresh
  emulator for the next case: a half-run machine poisons the rest.

## 8. Habits that wasted whole sessions

- Explaining a report with a theory and never reproducing it.
- Calibrating constants against a check instead of reading the routine.
- Writing "VERIFIED" for a value read from a moving guest, or from two dumps
  that were not taken at the same instant.
- Documenting a tool bug in GOTCHAS instead of fixing the tool.
- Re-fixing an item nobody closed: use QUEUE.md (READY FOR REVIEW, then the
  human ticks it).
- Reading whole big files: `grep -n`, then read a range.
- Repeating a failing tool call unchanged. Two identical failures: change the
  approach, or mark the item BLOCKED and do offline work.

## 9. Matching decompilation: decide this first [SAM2]

Before porting by behaviour, ask whether the EXE can be rebuilt byte for
byte: is it compiled code, and can you get the exact compiler (Turbo Pascal,
Borland/Turbo C, Watcom, MSC; the runtime's routines and strings tell which)?
If yes, a matching rebuild gives every routine a check that cannot be argued
with. The pattern, from fnv_decomp (github.com/momusdigital/fnv_decomp):
pin the binary, pin the toolchain, match the libraries first, list every
routine in a recipe (`tools/tp/inventory.py` for TP), keep a build that
matches from day one (unmatched routines as reassembled asm), convert one
routine at a time, name only after the match with a source tag on each name.
Worked through for Turbo Pascal in SAM2's `MATCHING.md` (where offsets
are plain immediates, so the build's layout has to match before any
routine can: unmatched routines are size-exact stubs). For Watcom C in an
LE program, wacky-re's `MATCHING.md` (one C module compared as an object
function by function, near misses checked by the addresses they use, a
source permuter whose winners are verified in place). Hand-written
assembly games cannot be matched from C/Pascal: port them by behaviour.
A hand-written asm module *inside* a compiled program can be reassembled
byte for byte instead: one source line per original instruction, every
relocation site an external name (`equ` its address in a `-f bin` build at
the original `org`, `extern` in the OMF build), and a check that the bin is
the original's bytes and the OMF fixup sites are the original's (wacky-re
`tools/asm/genasm.py`, `asmcheck.py`: 40 KB exact in one session). Then
read and name it with that check running.
Once it matches, port by translation (section 10), not by rewriting.

## 10. From a matching rebuild to a port: translate, don't rewrite [SAM3]

When the source matches the binary, the port is a compiler job. SAM3 went
from SAM2's Pascal to a playable Rust game in one session. The behavioural
port of the same game (SAM) never got that far in 161 iterations.

1. **A translator, not a rewrite.** Parse the matched source and emit the
   target language (SAM3: `tools/pas2rs.py`, about 1,600 lines of Python,
   emitting about 12,000 lines of Rust). Get the language's rules right once:
   integer widths and wrapping (TP: Integer + Word gives LongInt, `shr` on 16
   bits is logical), length-prefixed strings, `for` limits evaluated once,
   `Exit`, nested procedures. All routines follow, including behaviour nobody
   looked at (cheats, rare branches). Never edit the output; fix the
   translator and regenerate.
2. **Keep the memory layout.** Put the globals at their real data-segment
   offsets (from the link map) in an emulated 1 MB memory, and records at
   their field offsets. Then `absolute`, untyped `var` parameters, block
   moves, pointer arithmetic and the assembly's view of the data all work
   without being understood. Scalar locals can be native variables.
3. **Run the assembly, do not translate it.** Take the linked asm blocks'
   bytes from the matched build (with their segment fixups) and run them on a
   small 8086 interpreter (about 1,000 lines), called with the language's
   convention (Pascal: arguments left to right, callee pops, result in AX).
   SAM3 ran 4,700 lines of EGA drawing, PCX decoding and decryption this way.
   The game's quirks come along (ShowPicture starts with a stray `int3`,
   which DOS ignored).
4. **Emulate the machine, not the game.** The runtime is the compiler's
   library (System/Crt/Dos) plus the hardware the code touches: VGA/EGA
   registers and write modes, the PIT, port 60h with the BIOS int 9 buffer,
   the speaker. Interrupt handlers run at poll points: the translator puts a
   `poll()` in every loop body, and the interpreter polls every few thousand
   instructions. Everything in this layer is documented hardware, so it can be
   checked against references, not guessed from the game.
5. **Bugs now have two places to live:** the translator or the runtime. "Is
   the game supposed to do this?" stops being a question. Test with scripted
   input and screenshots of a hidden display (SAM3's `SAM3_SCRIPT`,
   `SAM3_TRACE`, `SAM3_WATCH`), then compare with the original in DOSBox.

Precondition: the matching rebuild (section 9). Without one, a translator
reproduces your decompilation's mistakes faithfully.

## Finding assets from the screen

Faster than reading loader code, and it finds packed or embedded data. [dave]

1. Halt in a scene, dump A000 (mode 13h: 64000 bytes) and RAM 0..0xA0000.
2. Take a 16-byte row of a tile from A000 and search RAM for it. A hit with
   rows 16 (or the sprite width) apart is the unpacked graphics buffer; read
   back from it for a count + offset table.
3. Search the program image for a few rows; a partial hit is the packed
   copy. Find its start by trial-decoding from every earlier byte until the
   output equals the buffer's first bytes; a size word often sits just in
   front. The immediate segment of that address in the code leads to the
   call and the decoder.
4. Palette: pair each A000 index with the screenshot's RGB at that pixel,
   convert to 6 bits (`round(c * 63 / 255)`, exact for DOSBox-X shots), and
   search RAM for the triples at 3 × index. Indices with two colours between
   shot and dump are palette cycling.
5. Confirm by decoding the packed copy yourself and comparing with the RAM
   buffer byte for byte.
6. Maps: once the tiles are known, match every 16×16 block of A000 against
   them (try all 16 × 16 grid offsets) to get a grid of tile numbers. Search
   RAM for one row as bytes; skip rows with animated tiles or sprites over
   them (the map stores the base tile). The row stride gives the map width;
   an immediate in the code near it gives the record layout. [dave: ten
   0x500-byte level records in the data segment]
