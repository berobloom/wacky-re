# Serial link (the UART interrupt handlers)

Status: OPEN (`com_isr_fifo` 0x10810 and the `com_getc`/`com_putc`/`com_kick`
side are near misses; nothing here has been run against the emulator)

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

## Traps

- Watcom's placement of the shared `done:` epilogue is not a function-local
  property: the same source came out with one epilogue in the full module and
  with two (338 vs 328 bytes) in a two-function probe. Probe the shape,
  decide in the whole file.
- The ring indices are `short` globals: the wrap is `cmp word [g], 0x4000 /
  jl <skip> / mov word [g], 0` (0x10739, 0x10786), i.e. a signed compare on a
  signed index, and the ISR resets the index only after using it.
