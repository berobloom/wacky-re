# TRAPS: the full reference

Grep this file when a symptom appears (`grep -n -i 'screenshot' harness/TRAPS.md`);
do not read it whole every session. The must-know subset is `GOTCHAS.md`.

Every entry cost a real session in an earlier project: [cold] = Cold Dreams
(16-bit real mode, local models through pi), [redcat] = RedCat's Razende
Rekenrace (32-bit Watcom/DOS/4GW, Claude Code). New generic traps found in a
project go here too (see "Feeding lessons back" in `README.md`).

## Address arithmetic

- Real-mode linear = seg * 16 + off: **one** hex digit, not two
  (0x824 * 16 = 0x8240). A linear address above 0xFFFFF or an offset above
  0xFFFF means the arithmetic is wrong, not that the emulator is odd. Compute
  with `python3 -c`, never in your head. [cold]
- A breakpoint on a wrong but valid address never fires and never errors: it
  looks exactly like "this code never runs". Recompute the address before you
  conclude anything from a breakpoint that did not hit. [cold]
- gdb `eip` is always linear, for every kind of stop: the stub returns
  SegPhys(cs) + eip. Never add CS*16; offset = eip − CS*16. [cold] believed
  "an offset after an interrupt"; that guess is harmless only while the code
  sits above linear 0x10000. [dave] (code at 0x8240) disproved it.
- The load segment depends on the DOS environment block, so on the launch
  method, the `.conf` and `memsize`. Never carry an absolute segment across
  boots; derive it at run time (`PSP = first segment - 0x10`, entry CS from
  the MZ header). A dump "displaced by a small constant" is this. [cold]
- `E8 rel16` is a 3-byte instruction: target = (site + 3 + rel16) & 0xFFFF.
  For a scanned pattern like `b8 id 00 e8 lo hi` the call is at a+3, so the
  target is a + 6 + rel. Using +5 makes every call scan return "0 callers".
  [cold]
- capstone 16-bit masks branch targets to 0xFFFF and gdb's real-mode
  disassembly truncates: correct both (`tools/real/disasm16.py` does). [cold]
- ndisasm on some distros has no `-l count` option (dave-re, Oct 2025:
  "unrecognised option"); capstone's linear range or `dd | ndisasm -b16`
  with byte math works instead. [cold]
- MZ: entry CS:IP is at 0x14/0x16, relative to the load segment. The entry
  segment's offset = file offset − (header paras × 16 + e_cs × 16)
  (`tools/mz.py` prints it). An absurd header means wrong offsets. [cold]
- DOS/4GW (LE): code and data objects move by **different** deltas from the
  image's own addresses, and the deltas change with `memsize` and with what
  ran first. Record image addresses in notes; get the deltas per boot
  (`tools/dosrun.py locate <prog>`) before setting a breakpoint. [redcat]
- A DS-relative table read (`mov bx,[0x93A5+bp*2]`) gives a DS **offset**:
  the bytes sit at file offset (DSoffset − the segment's base in the file),
  not at the DS offset inside the data file. [cold]

## GDB stub / QMP (dos-re-suite harness)

- pi's MCP comes from pi-mcp-adapter (7.0.1 checked), and it does **not**
  read `.pi/mcp.json` ("Pi mcp.json files are not read by the adapter").
  Per project it reads `.mcp.json`, then `.pi/mcp-adapter.json`. One
  `.mcp.json` serves Claude Code and pi. [digger]
- The stub has **no watchpoints**: `Z2` (write) gets an empty reply. To find
  who writes a pixel or variable, probe candidate routines with breakpoints
  and read their arguments, or let Ghidra/REA give the callers. [dave]
- The BIOS tick (0x46C) does not advance across a chain of breakpoint stops
  (60 passes of a 70 Hz loop, 0 ticks). Measure rates with the guest running
  freely: sample a game counter through QMP at a few times its rate. [dave]
- A breakpoint every pass also freezes `time()`: a game that seeds rand()
  from the clock between rounds (attract-mode demo choice) then picks the
  same thing every round. Arm only the event you trace (the instruction
  that starts it), let the rest run free. [dave]
- A breakpoint wait that times out may only mean the program never ran:
  `dosrun.py start` leaves DOSBox-X at the DOS prompt (600 s lost waiting
  for a demo that could not start). Take a screenshot before a long wait.
  [dave]
- An event chosen by rand() (which demo plays) need not be waited for:
  break right after the pick stores its result and overwrite it, then
  trace. One run instead of a dozen boots hoping chance picks it. [dave]
- No watchpoints: to find who writes a variable, break at the candidate
  routines and log the value at each hit (`dosrun.py watch`); the write
  lies between the last hit with the old value and the first with the new.
  [dave]
- **Continuing from a breakpoint re-hits it at once**: the stub does not step
  over the breakpoint at eip. A trace loop of continue/wait then spins on one
  instruction forever, the screen frozen. Remove it, `step_nowait` +
  `wait_for_stop`, re-insert, continue (`dosrun.resume_over`). [dave]
- A breakpoint on a function's entry hits once if the function is `main()`:
  for "once per pass" break on the loop head inside it (the `call` of the
  pass's first routine). If a breakpoint never hits, read the stack for
  return addresses before assuming the address is wrong: the game may be in
  a wait loop elsewhere (a "press a key" pause). [dave]
- **RSP round trips cost ~80 ms** without TCP_NODELAY (Nagle waits for the
  peer's delayed ACK after each tiny packet). dos-re-suite sets it on both
  sides since 4511afa; a hand-rolled client must set it too. Even then a
  breakpoint-per-pass trace is ~0.25 s per pass: trace short stretches, or
  compare against the port in bulk. [dave]
- A killed tracer (timeout, TaskStop) leaves its breakpoint in the stub and
  the guest halted on it: reconnect, `remove_breakpoint`, continue. [dave]

- GDB 2159 and QMP 4444 take **one client each**. A failed tool can leak a
  gdb client and every later call times out: `pkill -x gdb` (never
  `pkill -f gdb`: it matches your own shell), then re-attach. [cold]
- Diagnose "STARTED BUT NOT ANSWERING" before guessing:
  `grep 0A /proc/net/tcp | grep -E ':086F|:115C'` (2159/4444 listening).
  If absent, kill that dosbox-x and any stale gdb and start it by hand. [cold]
- The GDB stub holds the machine at boot until a client sends continue. [redcat]
- `gdb_command` answers **one call behind** (you get the previous result) and
  runs only the first line of multi-line input. Flush with a throwaway call,
  or prefer the dedicated tools. [cold]
- QMP (screenshot, save/load state, restart) needs a **running** target: a
  GDB halt starves it. Memory reads through gdb need a halted one. Working
  loop: interrupt → read → continue → save_state. [cold]
- Keys sent while halted are **queued**, not delivered; screenshots of a
  halted machine are frozen. Two identical screenshots = check you continued.
  [cold]
- No hardware breakpoints or watchpoints in this build (`hbreak`,
  `watch_memory` fail). Use `find /b`, conditional breakpoints
  (`break *0x9114 if $bx==0xa000`) and traces. [cold]
- The stub keeps its own breakpoint list: a trap can fire with
  `info breakpoints` empty. `restart_dosbox(relaunch=false)` clears it. [cold]
- `break_capture keep=true` adds a breakpoint on every call; it cannot arm
  while running; timeouts above ~60 s exceed the MCP request timeout (use
  break + `wait_for_stop` in ≤20 s chunks; re-wait once before declaring a
  trace finished). [cold]
- Counting breakpoints (`commands` / `silent` / `continue` / `end`, then
  `info break` "already hit N times") are the cheapest rate measurement. Any
  memory read that re-attaches gdb drops them and their logging: count first,
  read memory after. [cold]
- Two QMP-touching calls in parallel (two dumps) time out: issue them serially.
  [cold]
- Right after `continue_execution` QMP can answer with a stale response
  (`{'data': 'Ew==', 'size': 1}`) or time out once: retry once or twice, do
  not restart over it. [cold]
- `dump_memory("0","0",655360)` stops at 0x9FFFF: the A000 framebuffer is not
  in it. Segment/offset arguments are hex strings without 0x; `write_linear`
  wants a 0x prefix. Read the returned `Linear =` line. [cold]
- A SIGTERM makes DOSBox-X open a "quit anyway?" dialog that inherits the
  QMP/GDB ports. Set `quit warning=false` and stop it with SIGKILL. [redcat]
- QMP `screendump` looks for `dosbox-x_NNN.png`, but captures are named after
  the running program (`game_000.png`): take the newest file in the capture
  directory. [redcat]
- `screenshot` captures graphics modes only: a DOS text prompt comes back
  black or "NOTHING TO CAPTURE". Use `read_screen_text` (B8000) there. [cold]
- `load_state` clobbers the BIOS mode byte (0x449): screenshots then claim
  text mode while the game is in 13h. `write_linear 0x449 13`. [cold]
- `load_state` works only into a session where the game has already started
  and set its video mode / allocated its memory; into a bare DOS prompt it
  gives a dead black frame. Boot the game first, then load. [cold]
- A loaded state keeps running from mid-action globals; it is a starting
  scene, not fixture data. Never mix a fresh boot's dumps with a loaded
  state's in one comparison: each boot can re-roll random tables. [cold]
- A second emulator needs its own ports: other `gdbserver port` /
  `qmpserver port` in its `.conf`, the same in `DOS_RE_GDB_PORT` /
  `DOS_RE_QMP_PORT`, and stop it by its own pid. Two agents on one harness
  fight over the ports. [redcat, cold]

- dos-re-suite's checkout may have been configured in a container (paths
  under `/build/...` in every Makefile): a host `make` then fails on
  `SDL_video.h`, and `build.sh --reconfigure` needs autotools (aclocal).
  Without them: rewrite `/build/.work/dosbox-x-remotedebug` to the real path
  in the Makefiles, config.status and config.h (`find`, not `grep -r
  --include`), then `make`. A running emulator keeps its old binary. [dave]

## Input

- QMP `send-key` ignores `hold-time`: the release follows 120 passes of
  DOSBox-X's main loop, and every PIC event (Sound Blaster DMA) ends a pass.
  A game with its own INT 9 handler that polls a held-key table never sees
  the key. Send `input-send-event` down, wait, up (`dosrun.py key`). [redcat]
- A key you send to leave one wait can be taken by the next one: the BIOS
  buffer keeps it until something reads it, and many loops only peek
  (`kbhit`). Halt right after the first wait and empty the buffer (word at
  0x41C = word at 0x41A). [dave]
- Hold keys ≥ 0.08–0.3 s: a 0.15 s tap can be shorter than a game frame. [cold]
- A game that polls a key table checks it once per frame or per wait loop: a
  key counts only while it is down at a poll. [redcat]
- QMP pointer moves do not move the game's mouse: DOSBox-X counts mickeys only
  while its window has grabbed the mouse. Working route: `int33=false`, load
  the game's own mouse driver, grab with real X input on the Xvfb display
  (`xdotool key ctrl+F10`), move with `xdotool mousemove_relative`. Save
  states made without the driver do not carry it. [redcat]
- A held key can sometimes be forced by writing the game's key-state byte
  directly: useful for long walks; still tap with real key events. [cold]

## Timing

- `cycles = fixed N` is not real time when the host is busy: the BIOS tick
  came every 76–88 ms instead of 55. Time animations in BIOS ticks (read
  0x46C next to the memory you poll), never by the wall clock. [redcat]
- Delays built on `clock()` / INT 21h AH=2Ch / the BIOS tick end on a tick
  (18.2 Hz, 54.9 ms): delay(5) and delay(40) both last to the next tick,
  delay(1000) is 19 ticks. Port them in whole ticks. [redcat]
- Many games have no frame cap: the speed was whatever the PC gave. Measure
  frames per BIOS tick at a stated `cycles`, and say in the port what rate you
  chose. [redcat]
- eip sampling cannot find a main loop that spends its time in a vsync spin:
  break on the spin call's return address and count there. [cold]
- `in al,0x40/0x41` (the PIT) used as a random source makes a routine's
  result boot-dependent: replay and search the count, never calibrate. [cold]

## Save states and dumps offline

- A dump at a packed program's unpacked entry still holds the previous
  run's variables (BSS is zeroed later, by the C startup): do not read
  start values from it. A trace that stops when a mode flag changes misses
  that last pass, whose writes then look like they come from nowhere in the
  next run. [dave]
- A restore that does not stick: the code put a value back, and the rest of
  the same pass (or frame) changed it again (Dangerous Dave: a demo's end
  restores the map, then the same pass's item pickup takes the item Dave
  stands on). A watch shows the right value after the restore and the wrong
  one at the next hit; look at what runs after the restore before blaming
  the restore. [dave]
- A tool that prints progress to a redirected stdout shows nothing until
  it exits (block buffering): flush after each line in any long-running
  logger, or run Python with `-u`. [dave]
- Linear disassembly of a 16-bit image desyncs on data and misses calls:
  to list a routine's callers, scan for E8 rel16 whose target is the
  routine and decode the bytes just before it (B8 imm16 50 = the pushed
  argument). [dave]
- Wall-clock rates measured through QMP lag while the guest draws heavily
  (emulated time falls behind): measure timer-driven things during light
  load, or compare against the guest's own tick count. [dave]
- A port's string printer that treats \n and \r as controls cannot draw
  glyphs 9-13 that the original puts on screen one character at a time
  (cursors, frames): port the single-character routine too. [dave]
- To test a game-over path, poke the score and the game-over flag in RAM
  instead of playing there; restore any file the original then writes
  (high scores) from a copy, or later screen checks drift. [dave]
- Driving a menu in the original by keys alone goes wrong quietly (a
  cursor that keeps its column landed on another option, and Enter chose
  it): take a screenshot after each key, and put back any setting changed
  by mistake before the next experiment. [dave]
- The emulator has hardware the port lacks (DOSBox's mouse, Sound Blaster):
  screens that show what was detected differ there by design; mask those
  cells in a pixel check and say so in the check's name. [dave]

- A DOSBox-X `.state` is a ZIP; its member **`Memory`** (exact name;
  `Memory_Size` also matches a substring test) is a 136-byte header plus
  linear RAM from 0. Every state is a full RAM dump readable without the
  emulator (`tools/real/statedump.py`). It carries CS too. [cold]
- The VGA framebuffer is **not** in a state (A000 reads 0xFF). Compare the
  game's back buffer, or dump A000 live. [cold]
- Cross-time dumps lie: a back-buffer-vs-screen comparison needs both dumps
  taken while halted at the same instant. Dumps minutes apart with the same
  md5 mean the guest was not advancing. Screenshot the scene first. [cold]
- A dumped data file never equals the on-disk one where the game writes into
  it after loading: compare the regions it does not write, by file offset.
  [cold]
- Stale code dumps produce garbage call graphs: re-dump (or read the state's
  own copy) and diff the bytes you care about before trusting an old blob.
  [cold]
- An offline caller scan of zero is expected for code reached through
  function-pointer tables, and games self-patch segment immediates. [cold]

## Static analysis tools

- Ghidra (also through REA) loads a DOS MZ in real mode at segment 0x1000,
  so rel SSSS:OOOO is 0x10000 + SSSS*16 + OOOO. Feed it the unpacked image
  as an MZ (`tools/real/unlzexe.py` for LZEXE), not a raw blob: a raw x86
  load is not 16-bit. Function list, callers, xrefs to DS globals
  (DGROUP*16 + 0x10000 + offset) and decompiles then work. [dave]

- Capstone's `disasm()` stops **silently** at the first invalid byte: a start
  address in the middle of an instruction printed nothing at all. The DREA
  disassemblers now set `skipdata` and print `.byte`. [dave]
- A decoder you write from the code can be cleaner than the original. A
  16-bit decoder that renormalises a far pointer may drop or repeat bytes
  (Dave's RLE lost one byte at offset 0xFF00). Compare your output with the
  game's buffer in RAM, byte for byte, and reproduce the original's bug in
  the port when the game draws from the result. [dave]
- A RAM dump taken after start-up holds the game's own changes to its data.
  For a byte-exact reference of an unpacked image (e.g. to test an LZEXE
  unpacker), break at the unpacked entry and dump there. [dave]

- Ghidra has no Watcom register convention: register arguments show as
  `in_EAX` / `extraout_ECX` and calls lose their arguments. Read call sites in
  the disassembly. Watcom: first four args in EAX, EDX, EBX, ECX, the rest on
  the stack; variadic functions take everything on the stack. [redcat]
- When reading a callee's stack arguments, count its own pushes and `sub esp`
  before the first `[esp+N]`: the first stack argument is at
  esp + 4 × pushes + locals + 4. [redcat]
- Ghidra refuses project paths that contain a dot-directory (`.work`): give it
  a TMPDIR without one. REA does not open LE directly: use the ELF wrapper
  from `tools/le/leparse.py`. [redcat]
- REA's big results are written to a file: parse the file, do not call the tool
  again to "see it again". [cold]
- Do not name a local script `dis.py` (capstone imports `inspect`, which
  imports `dis`). A local `mcp/` directory shadows the `mcp` package: run the
  server with its venv python. [cold]
- A probe script with a wrong table offset can make a 15 × 31 sprite look
  50 × 69: verify every offset a probe uses against the file, not the port.
  [cold]
- `zip()` over a range that runs past the end yields nothing and fakes
  "0 differences". Size buffers to the blob and assert you read it all. [cold]
- Every scan loop must advance its index and cap its output (`| head -80`):
  unbounded output gets an unattended run killed. [cold]

- **NASM `.OBJ` for Turbo Pascal `{$L}`**: give the segment no class
  (`segment CODE public align=1`) and upper-case public names. With
  `class=CODE`, TP 5.0 rejects the file with "Error 51: Invalid PUBLIC
  definition". [SAM2]
- **Turbo Pascal puts all `{$L}` code after the unit's Pascal routines and
  init**, whatever the declaration order, so linked asm cannot sit between
  Pascal routines. A matching rebuild needs size-exact Pascal stubs instead
  (SAM2 `MATCHING.md` section 4). [SAM2, TP 5.0]
- **Turbo Pascal smart linking drops unused routines AND data in units**
  (typed constants and variables, public or private), and runtime routines
  nothing calls. A stub or filler build shrinks unless every stub is called
  and every variable is named (`Inline(>Var)` keeps a variable; `Inline`
  cannot name a routine: Error 143). Program-level variables are kept.
  [SAM2, TP 5.0]

## Old compilers: installing and pinning

- **Watcom C/C++ 9.5 floppies (WinWorld "Watcom CPP 9.5b")**: the files are
  packed (`.WPK`, packed `.LIB`), so run the original `INSTALL.EXE` under
  DOSBox. Copy all W9532 disks into one folder (no name clashes) mounted as
  `A:`. Answers piped through stdin drift: a y/n confirmation comes before
  the script, and every "insert disk" prompt eats a line. Instead edit
  `INSTALL.SCR`: every `ask VAR ...` becomes `set VAR y|n`, and the `enter`
  lines (disk changes) go. Then only the first `y` comes from stdin:
  `INSTALL A: C:\WC95\ < ANS.TXT > C:\INST.LOG`. About 10 minutes in
  stock DOSBox. [wacky]
- **The 9.5a/9.5b patches** (`Patch32.zip`): `cd \A`, `call APPLYA C:\WC95A
  -p -b`, then the same with `\B` and `APPLYB` (`-p` no prompts, `-b` no
  .BAK). Keep a copy per patch level: pinning the version needs all of them
  side by side. About 10 minutes per pass. [wacky]
- **DOSBox output redirected to a file is written in 4 KB blocks**, so the
  log looks stalled for minutes. Watch the target folder's file count or
  mtimes instead. After a `call`ed batch, stock DOSBox 0.74 did not run the
  following `-c exit`: the process stays alive and idle. Check that the last
  file the batch writes exists, then close DOSBox. [wacky]
- **Pin a library version by its modules, not its strings**:
  `tools/wc/libmatch.py PROG LIB...` searches every OMF module's code
  (fixup bytes masked) in the LE program. Run it on the same library from
  each patch level; the level with modules nobody else has is the one that
  linked. Wacky Wheels: CLIB3R 72 / 84 / 93 modules for 9.5 / 9.5a / 9.5b.
  The copyright year in the run-time string only narrows it to a range. [wacky]

## Running the port headless

- Run SDL ports under `Xvfb` with `SDL_AUDIODRIVER=dummy`. Make the Xvfb
  screen larger than the window (a 1280 × 960 window on a 1024 × 768 screen is
  placed partly off-screen and clicks miss). [redcat]
- xdotool keys and clicks often do not reach an SDL window without a window
  manager, and `xdotool search --pid` fails when the program runs under
  `timeout`. For tests, add a CLI flag or env hook that puts the port in the
  state you need, and remove ad-hoc hooks before committing. [cold, redcat]
- `import -window root` while the program is still open; after it exits the
  root window is black. A one-shot view that waits for input never exits:
  background it, or give it an exit timer. [cold]
- A build error does not stop a `make | grep` pipeline, and the previous
  binary stays: grep the build output for `error:` before trusting a PASS.
  [cold]
- **Showing a window on the human's desktop: the agent's shell has no
  `DISPLAY`** (and no `WAYLAND_DISPLAY`), even with a desktop session
  running. Do not guess `:0`. List `/tmp/.X11-unix/` (X0, X1, ...), find the
  X server process (`ps -u $(id -u) -o pid,comm | grep -i xwayland`), and
  read the display from its command line (`tr '\0' ' ' < /proc/PID/cmdline`
  shows `Xwayland :1 ...`). On COSMIC (Wayland) it was `:1` through Xwayland;
  `DISPLAY=:1 dosbox ...` then opens on the user's screen. Tests still go
  to a private Xvfb. [SAM2, SAM3]
- **Emulated retrace polled by real time can be missed forever.** A game's
  `wait for 3DAh bit 3` loop that sleeps or presents a frame each iteration
  can alias with a 1.4 ms window in a 14.3 ms frame and never see it
  (SetViewStart hung in some runs only). Make retrace virtual: a read at or
  after the frame boundary starts the retrace, which then lasts a fixed time
  from that read. [SAM3]
- **Scripted key taps must be held.** A game that polls the last scan code
  (its own int 9 stores port 60h) sees only the release code when the press
  and release arrive in the same poll. Hold taps for about 100 ms. [SAM3]

## Process and git

- A check with few samples can pass with the bug back: Dave's frame check
  saw one animated tile (1 in 5 chance), so a wrong rand seed passed. Check
  the whole state (all 75 animation entries against RAM), not one visible
  consequence. [dave]

- The data file on the disk need not be what the screen shows: Dave ships
  EGADAVE.DAV but draws a VGA set packed inside the EXE in mode 13h. Read the
  mode byte 0x449 before comparing a decoded file with a screenshot. [dave]

- A guide's worked example can be wrong, and agents copy it. When a doc and
  the tool's code disagree, the code wins; fix the doc. The eip rule "an
  offset after an interrupt" survived in three guides while server.py's
  `_resolve_pc` said "always linear". [dave]
- Game memory is game code: RAM dumps, unpacked images and save states stay
  out of git (`.gitignore` covers `captures/*.bin`, `*.state`, `*.dmp`). [dave]

- Notes are not proof. Earlier notes have been confidently wrong; re-check a
  claim against the code or a capture before building on it, and correct the
  note in the same commit. [cold]
- A check that cannot fail proves nothing: put the bug back and see it fail
  (mutation test). Draw onto a blank buffer, not over a copy of the expected
  frame. [cold]
- Two unattended runs in one worktree destroy each other's work. One agent per
  worktree; commit each deliverable with explicit paths as soon as it exists;
  never end with a dirty tree. A lost `git add` survives as a dangling blob
  (`git fsck`). [cold]
- An interrupted run leaves untracked captures: `git status --short` before
  spending harness time. [cold]

- **A memory dump and the words read next to it are not one instant.** A
  64000-byte VRAM read over QMP takes long enough for the guest to advance 1-2
  game steps, so the state words sit 2 px (or 1-2 animation rows) ahead of
  what the screen shows; the readout can also land inside a sprite update
  (background restored, sprite not yet drawn), giving a frame with no sprite at
  all. Replay the state and tolerate the small skew; treat a dump that matches
  nothing as torn rather than as a modelling error, and keep the count of which
  dumps matched. [dave]
- **`git checkout <file>` reverts uncommitted work in it.** During mutation
  experiments on a check/test file, `git checkout` to undo the mutation also
  deleted a check added earlier the same session. Copy the file to /tmp before
  mutating and restore with `cp`, or commit before experimenting. [dave]
- **Decode a table only after checking its base and element size.** A DGROUP
  address read from the wrong linear base (off by the load segment << 4) or a
  word table read as bytes still prints as plausible numbers, and the garbage
  gets written into findings as a pattern (a row-offset table came out as a
   Fibonacci sequence; a tile-class table began 4, 20, 10, 47). Print the same
  range as bytes and as words, and confirm the base against a value whose
  address is already known. [dave]
- **Swapping an emulator config needs the old instance killed properly.** A
  stop/pkill that matches the conf filename leaves the guest started from the
  previous conf holding the control ports; the next start() then reports
  "already running" and the new config is never used, while the run looks
  normal and its screenshots come from the old guest. After a kill, check the
  emulator log actually changed.
- **`pkill -f <pattern>` kills the command that runs it.** Inside a
  `bash -c "..."` wrapper the pattern matches the wrapper's own command line,
  so the shell is SIGKILLed: exit code 137, no output, no new emulator log -
  which reads like the emulator failed to start. Use `pkill -x <name>` (exact
  process name) or a pattern that cannot appear in the caller, e.g.
  `[d]osbox-x`. A whole debug session was lost reading this as a harness
  failure. [dave]
- **A constant in Ghidra's decompiled call is not evidence of the argument.**
  `__cdecl16near` prototypes frequently mis-attribute or drop the real
  argument pair: a call printed as `alloc(0x6000)` turned out to allocate
  71238 bytes (the buffer contents proved it), and the value was an unrelated
  register pair. When a decompiled size, offset or handle matters, check the
  raw bytes at the call site (push sequence) or confirm the effect empirically
  from memory. [dave]

- Replay checks against a captured wait/blink loop can be phase-dependent
  when the guest's animation tick counter persists across screens (set only
  at boot). The captured frames then sample (blink, anim-frame) combinations
  a fresh replay from the screen start never generates. Dump the counter
  alongside the screen, or reconstruct the pass count into the loop, before
  trusting a pixel-replay check. (dave-re: DS:615A, level 10 blink wait.)

## Embedded / cross targets

- devkitARM `gba.specs` (gba_cart.ld) places plain `.bss` and `.data` in the
  32K `iwram` region; a reconstruction with ~100K of static arrays fails to
  link with the misleading "section `.data' is not within region `iwram'".
  Put big zero-initialized buffers in `__attribute__((section(".sbss")))`:
  `.sbss` is NOLOAD in the 256K `ewram` region and the malloc heap starts
  after it. libgba also defines `EWRAM` as the address macro 0x02000000, so
  do not name a helper macro that. (dave-re: GBA port.)
- GBA `REG_DISPSTAT` (0x4000004): bit 0 VBlank flag, 1 HBlank flag, 2
  VCount match flag, 3/4/5 the matching IRQ *enables*. libgba's `LCDC_IRQ`
  enum (gba_video.h) is correct and agrees with GBATEK. `while
  (!(REG_DISPSTAT & 0x8));` waits on an enable bit nothing set and hangs
  after frame 1; `& 2` (HBlank flag) never hangs but paces nothing. Wait on
  bit 0 or on REG_VCOUNT >= 160. Register bit meanings get quoted from the
  header or GBATEK, never written from memory: a local model wrote this
  entry inverted first, and it cost a session. (dave-re: GBA port,
  corrected 2026-11-08.)
- GBA `VBlankIntrWait()` (SWI 5) halts forever unless the VBlank IRQ is
  enabled: `irqInit(); irqSet(IRQ_VBLANK, h); irqEnable(IRQ_VBLANK);`.
  irqSet only stores the handler; irqEnable writes DISPSTAT bit 3, REG_IE and
  REG_IME (objdump of libgba.a). Not a trap: libgba's `SystemCall` asm has no
  outputs, so it is implicitly volatile and `-O2` keeps the SWI (checked).
  (dave-re: GBA port.)
- Driving a Qt emulator headlessly: XTEST keyboard reaches global shortcuts
  but not emulated pad input, XSendEvent is ignored by Qt, and `xdotool
  search` can hang once probe windows churned the display. Do not drive the
  GUI; use a frontend that calls the core's own key API (libmgba
  `core->setKeys`). Poking the input register from a gdb stub is unreliable
  (KEYINPUT is read-only on hardware; the mGBA stub also desynced). Wrap all
  such probes in `timeout` and redirect background GUI apps' stdio or the
  tool call blocks on their pipe. (dave-re: GBA port; its headless runner on
  the distro libmgba is dave-re tools/gba/gba_run.c: script on stdin, keys,
  memory reads, PNG, cycle-weighted PC profile. Build against the distro
  headers with `-include mgba/flags.h`: the USE_* flags change struct
  layouts and common.h does not include them.)
- GBA homebrew lag: devkitARM code runs from cart ROM on a 16-bit bus, and
  the power-on WAITCNT is 4/2 wait states without prefetch. An ARM-mode
  pixel loop there measured ~90 cycles a pixel. Set REG_WAITCNT = 0x4317
  (tonc WS_STANDARD; libgba has no name for it) and put hot loops in IWRAM
  as ARM (libgba IWRAM_CODE + ARM_CODE, plus noinline: GCC otherwise
  inlines the function back into its ROM caller). Profile cycle-weighted;
  instruction-count sampling hides wait states. With -fno-strict-aliasing
  (common in reconstructions) copy struct fields to locals before pixel
  loops, or every store forces a reload from slow RAM. (dave-re: GBA port,
  3 -> 30 fps.)
- Porting a fixed-rate game to slower hardware: do not run one logic pass
  per drawn frame. The dave-re GBA port did, so at 20 fps the game ran at a
  third of the original's 70 passes/s, and users report that as "lag". Run
  logic passes on a hardware timer at the original rate (with the
  original's extra-long passes, such as scroll retraces) and draw as often
  as the remaining time allows. (dave-re: GBA port.)
- PC speaker on GBA: the speaker is a square wave at 1193182 / divisor
  Hz, so PSG square channel 1/2 at 50% duty plays it exactly enough:
  x = 2048 - 131072 / f (GBATEK). Run the game's INT 8 (one sound word per
  PIT channel-0 period) on a timer IRQ, change only the frequency register
  per word (no restart bit), and gate rests with SOUNDCNT_L's enable bits.
  Testing it headlessly: libmgba's blip buffers give the samples, but the
  output filter leaves a decaying tail after a channel switches off, so a
  "loud span" reads 30-50% too long. Count rising edges and time first to
  last edge instead. In-game sounds get cut off by higher-priority ones;
  measure a sequence that waits for each sound to finish (a busy-wait in
  the game). (dave-re: GBA port, tools/gba_check.py.)
- DOSBox-X: the QMP socket (4489) can die or never come up while the GDB
  stub (2189) stays alive. GDB-side tools (breakpoints, registers, linear
  memory reads) keep working; screenshot/save-state/keyboard tools fail with
  "Bad file descriptor" or timeout. If captures are required, restart the
  emulator with the project .conf instead of debugging the socket.
  (digger-re: 2025-06, live INT-handler sampling done GDB-only.)
- DOSBox-X / QMP: arrow keys sent through the QMP key path arrive in the
  guest **E0-prefixed**: one keypress produces four keyboard events
  (E0, code, E0, break). Against a game with its own INT 9 handler every
  byte re-fires the handler, so a breakpoint on the scancode latch store
  sits in a re-fire loop and repeated `continue` looks like an infinite
  loop (it is compounded by the one-call lag). Escape with `delete` or
  `si`; never by repeating `continue`. Also: QMP arrow-key *direction* is
  not trustworthy: "right" produced guest scancode 0x4D (left). Verify
  the scancode via the keyboard latch before concluding.
  (digger-re: 2025-06, INT 9 / scancode-table session.)
