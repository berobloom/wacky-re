# Serial link (the UART interrupt handlers)

Status: OPEN (`com_getc`/`com_putc`/`com_kick` and the link packet code are
near misses; nothing here has been run against the emulator)

## Summary

`com_open` installs one of two interrupt handlers on `comvec`: `com_isr`
0x106c8 when `hasfifo` is 0, `com_isr_fifo` 0x10810 when the port has a 16550
FIFO. Both loop on the IIR port (`comport + 2`) and service 4 = received data,
2 = transmitter empty, 6 = line status, anything else = EOI and `iretd`.
The rings are 16 KB: `rxbuf` 0x84b00 with `rxhead`/`rxtail`/`rxcount`
0x84afa/0x84afc/0x84afe, `txbuf` 0x80afa with `txhead`/`txtail`/`txcount`
0x80af4/0x80af6/0x80af8. Port and counters: `comport` 0x88cba, `lsr` 0x63d42,
`rxints` 0x7edc0, `txints` 0x7edc4, `overruns` 0x7edc8, `parityerrs` 0x7edcc,
`framingerrs` 0x7edd0, `breaks` 0x7edfc (`analysis/ww_names.txt`).

## Findings

- CONFIRMED (rebuild `src/ww.c` byte-identical at 0x106c8-0x1080d, 325 bytes,
  `src/check.sh` MATCH): `com_isr` is the handler `com_open` installs when the
  FIFO probe fails, and its C shape is a `for (;;)` + `switch` over
  `inp(comport + 2) & 7` with the arms in source order 4, 2, 6 and one
  `short` local carrying the byte in both the receive and the transmit arm.
  Its receive arm reads the port *before* the index: the value goes to `EDX`
  (`mov edx,eax` 0x10716) and `rxbuf[rxhead++]` is then addressed `[eax +
  0x84b00]` (0x10726) with `EAX` the sign-extended index.
- CONFIRMED (0x10771 `movzx dx, byte ptr [eax + 0x80afa]`, 0x1079a `movsx
  eax,dx`, 0x107a4 `out dx,al`): in the transmit arm the byte also lives in
  `DX` - one live range, not two - and the port is re-read into `EDX` only
  after the byte has been widened out of it. Ours previously homed it in `AX`
  (`movzx ax` + `cwde`), which swapped `AX`/`DX` at 0x10771/0x10779.
- CONFIRMED (0x1070d/0x1072c `inc dword [0x7edc0]`, `mov ax,word [0x84afe]` +
  `inc word [0x84afe]`): `rxcount++` and `txcount--` compile to a *dead* load
  of the global plus a memory `inc`/`dec`; the load's register is the
  tie-break that showed the byte local's home (GOTCHAS "one local, both
  arms").
- CONFIRMED (rebuild `src/ww.c` byte-identical at 0x10810-0x10987, `src/check.sh`
  MATCH): `com_isr_fifo` is the 16550 variant of the same switch, and it uses
  the same single `short` byte local in both arms - the receive store
  `mov edx,eax` (0x1087b) + `mov byte [eax + 0x84b00], dl` (0x1088b) and the
  transmit load `movzx dx, byte [eax + 0x80afa]` (0x108ec) + `movsx eax,dx`
  (0x10913) all home that one local in EDX. Its case 4 is a `do { ... } while
  (inp(comport + 5) & 1)` - drain while LSR bit 0 (data ready) is set; its case
  2 sends at most 16 bytes per interrupt: `n` lives in BX (`dec bx` 0x1091e,
  `je <loop exit>` 0x10920 = `if (--n == 0) break;`) and the back edge is
  `cmp di, word [0x80af8]` + `jne 0x108de` (0x10926, DI = 0, so
  `while (txcount != 0)`); 16 is the 16550 FIFO depth (external spec - the
  counter itself is code, the constant is not read at these addresses).
- LIKELY (code shape matches the 8250 IIR/LSR register layout, not run): the
  switch subject is the IIR and the three arms are "received data available"
  (read `comport`, store, wrap at 0x4000), "transmitter holding empty" (send
  one byte per interrupt if `txcount` != 0) and "receiver line status" (read
  `comport + 5` into `lsr`, count bits 1, 2, 3, 4). Gap: no emulator run
  counting `rxints`/`txints`.
- CONFIRMED (0x107fd-0x1080c): both `default` paths jump to ONE epilogue -
  `mov edx,0x20; mov al,0x20; out dx,al` (EOI 0x20 to the PIC at 0x20) then
  `pop gs/fs/es/ds; popal; iretd`. 9.5b duplicates that epilogue for one of
  the two defaults in some layouts (GOTCHAS "the duplicated ISR epilogue").

- CONFIRMED (rebuild `src/ww.c` byte-identical at 0x2dd30-0x2df70, `src/check.sh` MATCH, iter 21;
  bytes 0x2dd42-0x2dd4e): `net_key` keeps `v88cae` as the **low byte** of the sequence counter
  `v7ed8c` - `mov ax,word [0x7ed8c]; xor ah,ah; mov word [0x88cae],ax` - so the stale-packet test
  `v88cb0 < v88cae` compares truncated sequence bytes, and `v88cb0` is the byte the reply carried
  (`net_recv` stores `buf[3]`). The packet itself is `type, a, b, v7ed8c(byte), crc16 low first`
  (`net_send` 0x2dc38; the `'Z'`/`'W'` types zero `a` and `b`). A half-done exchange is parked in
  `v7ed94` (resume flag) / `v7edac` (wait) / `v7edb8` (resend) / `v88cf1`, `v88cf4` (our `a`, `b`),
  and resumed at 0x2dd5d-0x2dd8f by byte-copying `v88cf1`->`v88cef` and `v88cf4`->`v88cf2`
  (each value loaded back out of the global it was just stored to - five load/store pairs).

- CONFIRMED (com_open 0x102f4-0x104fb, iter 33): the divisor is `1843200UL / (rate * 16)` with
  `rate = baud` first and `rate = 19200` only when `baud == 14400` (0x10392 `movsx ecx,di`,
  0x1039a `cmp ecx,0x3840`, 0x103a2 `mov ecx,0x4b00`, 0x103ac `shl ecx,4`, 0x103b1 `div ecx`),
  so a 14400 link is clocked as 19200. `com_open(short port, short irq, short baud, short comnum)`:
  WCC delivers the 3rd integral argument in EBX and the 4th in ECX, so the caller's 0x1dc2e
  `movsx ecx,word [0x7ec50]` is `comnum` (INT 14h DX, stored at 0x7edec as `comnum - 1`) and 0x1dc35
  `movsx ebx,word [0x7ebd4]` is `baud` (compared above); the callee's prologue 0x102f9-0x10303 and
  `mov ebx,edx` for int386's 3rd argument (0x10385) confirm it. Reading ECX/EBX in C order gives the
  swapped signature `(port, irq, comnum, baud)`, which changes no call-site byte but opens the wrong
  port and divides by the wrong rate - `objmatch.py`'s `WRONG link_open_@0x1dc31` line is the check.

- CONFIRMED (difffn seeds, iter 75): `com_wait_byte` 0x2df70 and `com_expect` 0x2e008 pop the ring
  the same way in our C. `SEED=0x84afe=1:w,0x84afc=3:w` puts one byte in the ring with a non-zero read
  index, so the pop path runs (0x2df8c-0x2dfc4 / 0x2e02d-0x2e065: `inc word [0x84afc]`, the `cwde` +
  `movzx ax,byte [eax+0x84b00]` read, `dec word [0x84afe]`, the `cmp word [0x84afc],0x4000` wrap) and
  both runs return. The byte the pop reads is `rxbuf[rxtail]`, so a non-zero value goes in at
  `0x84b03` (= 0x84b00 + 3) when rxtail is seeded to 3: with `+0x84b03=0x41:b` both runs still return
  and EAX agrees (difffn prints EAX only when the sides disagree; a direct probe of the game's
  0x2df70 shows that value is 0x41), which answers the open EAX question for this path (a pop returns
  the byte, the timeout arms return 0). For `com_expect` the popped byte must equal its `want`
  argument, which difffn passes as 0: seeding `0x84b03=0x41:b` puts both runs back at the instruction
  cap. `com_expect`
  also matches its control (the game against the relinked game); a control for `com_wait_byte` cannot
  be run - it is not a public in `.work/link/WWORIG.MAP` (the nearest are `com_getc_` 1:0x5b0,
  `com_waitfor_` 1:0xa94), the game's spelling of it is static.
  Their timeout arms stay uncovered: com_wait_byte's 0x2dfd6 `cmp ecx,0x110` compares `ticks` with a
  snapshot of `ticks` taken at entry (0x2df75 `mov ebx,[0x7e720]`), and com_expect's timeout arm is
  gated on its own argument (0x2e08a `test edx,edx; je 0x2e01c`, `cmp ecx,edx` 0x2e096) which difffn
  passes as 0. Neither wait loop makes a stubbed call, so difffn's `TICK` (findings/menus.md) cannot
  advance it.

## Traps

- Watcom's placement of the shared `done:` epilogue is not a function-local
  property: the same source came out with one epilogue in the full module and
  with two (338 vs 328 bytes) in a two-function probe. Probe the shape,
  decide in the whole file.
- The ring indices are `short` globals: the wrap is `cmp word [g], 0x4000 /
  jl <skip> / mov word [g], 0` (0x10739, 0x10786), i.e. a signed compare on a
  signed index, and the ISR resets the index only after using it.
