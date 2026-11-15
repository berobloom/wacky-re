<!-- From re_cold/mcp/AGENTS.md (the dos-re-harness doc), game-agnostic. -->

> **Server version.** This guide matches dos-re-suite from 2026-10-31 on (35
> tools). Older copies lack `read_screen_text`, `type_text`, `capture_wave`
> and the resolved-PC footer of `regs`: then use `send_key` for typing and
> `read_linear` of 0xB8000 for text screens. Ports are 2159/4444 unless
> `DOS_RE_GDB_PORT` / `DOS_RE_QMP_PORT` say otherwise. The examples
> (`digger.com`, `cold`) are from the projects this came from.

# Working on a DOS program through this MCP server

You are debugging a 16-bit real-mode DOS program running inside a patched
DOSBox-X. The MCP server gives you a GDB stub (registers, stepping,
breakpoints, memory writes) and a QMP endpoint (memory reads, keys,
screenshots, save states).

Read the address rules before your first tool call. Almost every wasted session
starts with a mis-added segment address, and the mistake looks like a broken
tool rather than bad arithmetic.

---

## 1. Address arithmetic

Real mode addresses are `segment:offset`. The linear address is:

```
linear = segment * 16 + offset
```

**`* 16` is one hex digit left, not two.** This is the single most common error:

```
0x824 * 16  = 0x8240        correct
0x824 * 16 != 0x82400       wrong by 16x
```

Worked example (a segment:offset written in a listing, not a gdb `eip`):

```
CS:IP = 0824:763C
  0x824 * 16 = 0x8240
  0x8240 + 0x763C = 0xF87C      <- the linear address
```

Do not do this in your head. Compute it:

```bash
python3 -c "print(hex(0x824*16 + 0x763c))"
```

Facts that follow, and that catch most errors:

- A real-mode **offset is 16-bit**: never above `0xFFFF`.
- A real-mode **linear address is 20-bit**: never above `0xFFFFF` (1 MB).
- If your sum exceeds `0xFFFFF`, you made an error. Do not reach for
  protected mode, EMS, or emulator weirdness to explain it. Recheck the
  multiply first.
- Many segment:offset pairs alias the same linear address. `0x0824:0x763C`
  and `0x0F87:0x000C` are the same byte. Compare linear addresses, never
  segment pairs.

## 2. Reading `eip` correctly

**`eip` from `regs` or a gdb stop is always the linear address**, for every
kind of stop. The patched stub returns `SegPhys(cs) + reg_eip` for the PC
register (`DEBUG_GetRegister`), so the CS base is already added.

> Never add `CS * 16` to `eip`. The offset inside CS is `eip - CS * 16`.

Example that burned a session (Dangerous Dave, halted by an interrupt):

```
CS = 0x824, eip = 0xF87C
  eip is linear: the PC is 0xF87C.
  offset = 0xF87C - 0x8240 = 0x763C, so CS:IP = 0824:763C
  (a vsync loop: in al,dx / test al,8 / je).
  Adding CS*16 instead gives 0x17ABC: graphics data, not code.
```

An older version of this guide said `eip` was an offset after an interrupt
and to add CS*16 when `eip <= 0xFFFF`. That is wrong; for a program loaded
below linear 0x10000 it is wrong on every stop. `eip > 0xFFFF` only proves
the value cannot be an offset; a small `eip` is still linear.

```
CS = 0xF000, eip = 0xFD186
  linear PC 0xFD186 (BIOS ROM), offset 0xD186: the BIOS idle loop.
```

**`regs` does this for you.** Its output ends with a `-- program counter`
footer giving the linear PC and the offset in CS. `current_instruction`
disassembles at the same address. Use those numbers rather than recomputing
them.

Raw gdb (`gdb_command`, a hand-run gdb) shows the same linear `eip`.

## 3. Using the tools

**Always pass required arguments.** Calling a tool with `{}` fails with a
validation error naming the missing fields. That error is the server working
correctly; supply the arguments and retry rather than repeating the empty call.

```
disassemble_segment {"segment": "2E25", "offset": "4DBA", "count": 20}
send_key            {"keys": "c,o,l,d,ret"}
```

Segments and offsets are **hex strings without a `0x` prefix**. Every required
parameter carries a description with a worked example in the tool schema; read
it rather than guessing the shape.

**If the identical call fails twice with the identical message, stop
repeating it.** The message says what arrived: when it reports `input_value={}`
your arguments are not reaching the server at all, and sending the same call a
third time cannot change that. Emit the call again with an explicit JSON
arguments object, or use a different tool to make progress. Repeating an
unchanged call is never the fix.

The tools, by group:

- **Lifecycle**: `status`, `start_dosbox`, `restart_dosbox`,
  `interrupt_execution`, `continue_execution`, `wait_for_stop`
- **Breakpoints**: `break_linear`, `break_segment`, `break_capture`,
  `break_list`, `break_delete`
- **Inspection**: `regs`, `stack`, `current_instruction`, `disassemble`,
  `disassemble_segment`, `step_instruction`, `trace`, `explain_address`
- **Memory**: `read_linear`, `read_segment`, `read_segbyte`, `read_segword`,
  `write_linear`, `write_segment`, `dump_memory`, `watch_memory`
- **Machine**: `type_text`, `send_key`, `read_screen_text`, `screenshot`,
  `save_state`, `load_state`, `gdb_command`

### Typing at the DOS prompt

`type_text` takes the literal string: `{"text": "digger.com"}`. It handles
punctuation and case for you and presses Enter by default.

`send_key` takes qcode **names**, not characters, so `/` is `slash` and `:` is
typed with shift. Through it a path becomes
`slash,h,o,m,e,slash,...,minus,r,e`. Use `send_key` for single keys and control
keys (`esc`, `up`, `f1`, `ret`) and `type_text` for anything you would
actually type.

**You rarely need to type a path at all.** The project `.conf` mounts the game
directory as C: in its `[autoexec]` section, so after `restart_dosbox` the
machine is already at a `C:\>` prompt with the game files present. Type the
program name and nothing else.

### Seeing the screen

- **Text mode** (a DOS prompt, most menus): `read_screen_text` decodes the
  80x25 buffer at `0xB8000` and returns it as text you can read directly.
- **Graphics mode** (mode 13h gameplay): `screenshot` for a PNG, or
  `dump_memory` from `A000:0000` for the raw 64000-byte frame.

`screenshot` renders the *graphics* framebuffer, so **a DOS text prompt comes
back black**. That is the capture method, not a dead machine. When a screenshot
looks empty, try `read_screen_text` before concluding anything.

`explain_address` exists precisely to avoid section 1. Prefer it over manual
conversion.

### If you cannot see images

`screenshot` returns a PNG path. If you have no vision capability, that path is
useless to you. Read the framebuffer numerically instead: in mode 13h the
screen is 64000 bytes at linear `0xA0000`, one byte per pixel indexing a
256-entry DAC palette.

**Match the tool to the size.** `read_linear` returns hex text into the
conversation and is capped at 4096 bytes; a whole frame is sixteen times that.
For anything large, `dump_memory` writes to a file and takes up to 1 MB:

```
dump_memory  {"segment": "A000", "offset": "0", "count": 64000,
              "path": "captures/frame.bin"}
read_linear  {"address": "A0000", "count": 256}     # a peek, not a frame
```

Then analyse the file: counting distinct byte values, or checking whether a
region is uniform, tells you whether a screen is blank, a menu, or gameplay
without seeing it.

## 4. The machine only runs when you let it

This trips people and models alike, and it looks like a broken game rather
than a stopped one.

**A halted guest executes nothing.** It does not read the keyboard, does not
advance a frame, and does not draw. While the target is halted:

- `send_key` **queues** keystrokes. They are not delivered. Typing `cold,ret`
  at a halted DOS prompt does nothing at all until the machine runs again.
- `screenshot` returns a **frozen frame**: whatever was on screen when
  execution stopped. Capturing twice returns the identical image.
- A blank capture usually means the machine was halted before it drew
  anything. That is not a game rendering a black screen.

So the cycle for making something happen is always:

```
type_text {"text": "digger.com"}   # queued while halted
continue_execution                  # now it actually runs
interrupt_execution                 # or wait_for_stop, if a breakpoint catches it
read_screen_text                    # or screenshot, in a graphics mode
```

Anything you do while halted is a request, not an event. `continue_execution`
is what turns the request into one. If two consecutive screenshots are
identical, or typing appears to do nothing, check whether you ever continued:
`status` says plainly whether the target is running or stopped.

The reverse also holds: **several tools need a RUNNING target.** QMP requests
are serviced by the emulator's main loop, so a GDB halt starves them, and
`save_state`, `screenshot` and `restart_dosbox` can time out while stopped.

## 5. A working first few minutes

1. `status`: is a target attached, running, or stopped?
2. If nothing is running, `start_dosbox`. If the state is unclear,
   `restart_dosbox` for a clean boot.
3. `interrupt_execution` to halt a running target before inspecting it.
4. `regs`, then apply section 2 to get a trustworthy PC.
5. `save_state` once you reach anywhere interesting. Replaying minutes of
   emulated play to get back is the main avoidable cost in this work.
6. Set a breakpoint, `continue_execution`, `wait_for_stop`, inspect.

Breakpoints are set on **linear** addresses. `break_segment` converts for you;
prefer it when you have a segment:offset from a disassembly.

## 6. DOS program layout

### The MZ header

Every DOS `.EXE` starts with one. The fields, at their **correct** offsets:
getting these wrong produces plausible-looking nonsense:

| Offset | Field | Meaning |
|---|---|---|
| `0x00` | `e_magic` | `MZ` |
| `0x02` | `e_cblp` | bytes used on the last page |
| `0x04` | `e_cp` | pages of 512 bytes |
| `0x06` | `e_crlc` | relocation entries |
| `0x08` | `e_cparhdr` | header size in 16-byte paragraphs |
| `0x0A` | `e_minalloc` | min extra paragraphs |
| `0x0C` | `e_maxalloc` | max extra paragraphs |
| `0x0E` | `e_ss` | initial SS, relative to load segment |
| `0x10` | `e_sp` | initial SP |
| `0x12` | `e_csum` | checksum |
| **`0x14`** | **`e_ip`** | **initial IP: the entry point** |
| **`0x16`** | **`e_cs`** | **initial CS, relative to load segment** |
| `0x18` | `e_lfarlc` | file offset of the relocation table |
| `0x1A` | `e_ovno` | overlay number |

The entry point is at `0x14` and `0x16`. Reading entry fields from `0x1E` or
beyond means reading the relocation table as if it were the header, which
yields absurd values: page counts of zero, negative sizes, six-figure
relocation counts. **If a parsed header is absurd, your offsets are wrong;
do not invent a format to explain the numbers.**

Image size is `(e_cp - 1) * 512 + e_cblp`, and code begins at
`e_cparhdr * 16` bytes into the file.

`e_cs` and `e_ss` are **relative to the load segment**, which DOS chooses at
run time. They are not absolute.

### Packed executables

Many DOS binaries are compressed (LZEXE, PKLITE, EXEPACK). The MZ header is
still valid, but `e_cs:e_ip` points at a **decompression stub**, not at the
game. The stub unpacks the real image and jumps to it.

You do not need to parse the packer's own header. Let the emulator do the work:
run the program, let the stub finish, then interrupt and inspect the unpacked
image in memory. That is both easier and more reliable than static unpacking.

Telltale: `LZ09`/`LZ91` near the start of the file, or an entry point that
disassembles into a tight copy loop.

### Memory map

| Range | Contents |
|---|---|
| `0x00000`–`0x003FF` | interrupt vector table, 4 bytes per vector |
| `0x00400`–`0x004FF` | BIOS data area |
| `0x00500`–`0x9FFFF` | conventional memory: DOS, TSRs, your program |
| `0xA0000`–`0xAFFFF` | VGA graphics framebuffer (mode 13h lives here) |
| `0xB8000`–`0xBFFFF` | colour text mode buffer |
| `0xC0000`–`0xEFFFF` | adapter ROMs |
| `0xF0000`–`0xFFFFF` | system BIOS |

A PC halted somewhere in `0xF0000`–`0xFFFFF` is in BIOS, usually an idle or
interrupt path, not a mystery.

### At program entry

`DS` and `ES` point at the **PSP**, a 256-byte control block immediately below
the program image. The command line lives at `PSP:0x80`. The load segment,
and therefore every absolute segment you record, depends on how much memory
DOS had to allocate, which depends on the environment block, which depends on
the launch command and config.

**Derive addresses at run time; do not reuse absolute segments across
sessions.** A dump that decodes correctly one day and looks corrupt the next,
displaced by a small constant, is almost always this and not a format error.

## 7. Traps worth knowing

- **One client each.** The GDB stub and QMP each accept a single connection. A
  stray debugger holding the port blocks everything.
- **QMP needs a running target.** `save_state`, `screenshot` and
  `restart_dosbox` are serviced by the emulator's main loop, so a GDB halt
  starves them. Continue the target first.
- **Verify a negative result before building on it.** A breakpoint that never
  fires is far more often a wrong address than a wrong theory about the
  program. Check the arithmetic, then check that the code is reached at all.
- **The stack tool dumps upward from SP.** Return addresses sit *below* SP;
  read them with `read_segment(SS, SP - 0x30, ...)`.
- **Filenames are plain ASCII in memory** shortly before the read that uses
  them, which is an easy way to identify loaded files without tracing INT 21.

## 8. When something looks impossible

In order:

1. Recompute the address with `python3 -c`, not mentally.
2. Check that nobody added CS*16 to `eip`: it is already linear (section 2).
3. Check whether the target is running or halted (section 4). A screen that
   never changes, or typing that has no effect, is nearly always a machine
   that was never continued, not a black screen and not a dead game.
4. Check that your arguments arrived: an error reporting `input_value={}`
   means they did not, and resending the same call cannot help.
5. Only then consider that the tool or the program is at fault.

An emulator behaving strangely is a much rarer explanation than an arithmetic
slip, and pursuing it costs far more.
