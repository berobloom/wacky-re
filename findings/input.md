# Input

Status: OPEN (keyboard handler, its installer and table, and the joystick poll with its calibration
are read; SETUP's limit arithmetic and what each C reader of these bytes does are not)

## Summary

Keyboard input is one interrupt handler in the assembler module, `kbd_isr` 0x40907 (src/wwasm.asm).
It keeps a few named key bytes (`key_select`, `key_up`, `key_down`, `shotkey`), a 12-entry
(scancode, state) table at `[keytab]`, and the last raw scancode in 0x88cd0 (`last_scan`; the C
calls it `aborted` / `key88cd0`). The joystick is polled, not interrupt-driven: `joy_poll` 0x3784b
(axes to direction keys) and `buttons_poll` 0x377ae (findings/video.md table, analysis/ww_names.txt).

## Findings

- CONFIRMED (code 0x40907-0x409ea, read in full): `kbd_isr`:
  - loads DS from the sel16 fixup 0x4090e (`mov bx, seg`), then `kbd_busy` 0x88b30 is a reentrancy
    guard (== 1 -> leave at once; set to 1; cleared at the end);
  - reads the scancode (`in al,0x60`), acknowledges the keyboard by pulsing bit 7 of port 0x61, and
    sends EOI (`out 0x20,0x20`) *before* decoding;
  - an `E0` prefix byte is replaced by the previous scancode (`mov al,[last_scan]`), so extended
    keys repeat the key before them instead of being told apart;
  - **press/release split at 0x52**: scancode <= 0x52 is a press (state 1), anything above is a
    release (state 0, scancode - 0x80). Keys whose press code is above 0x52 (F11 0x57, F12 0x58) are
    therefore read as the release of key code - 0x80;
  - Enter 0x1c or Space 0x39 -> `key_select` 0x88cce; Up 0x48 -> `key_up` 0x88cdb; Down 0x50 ->
    `key_down` 0x88cd2; each then also goes through the table. F10 0x44 -> `shotkey` 0x88ccd and
    skips the table;
  - the table: 12 byte pairs at `[keytab]` (0x7d990 holds the pointer); for every pair whose first
    byte is the scancode, the second byte = the state. `main` sets `keytab = cfg7b6d8` (0x35d4d):
    the WACKY.CFG key map, six slots per player (steer left, steer right, accelerate, fire, brake,
    hand-brake), so the high byte of each slot is that control's live state (findings/formats.md);
  - installed by `kbd_on` 0x32b14 (src/ww.c, int 21h 0x3509 saves the old vector, 0x2509 sets
    `kbd_isr`); `kbd_off` clears every key byte first;
  - at the end the raw byte (with its release bit) goes to `last_scan` 0x88cd0, then `iretd`.
- CONFIRMED (code 0x377ae, `buttons_poll`): `joy88b40`/`42`/`44`/`46` = the live state bytes of
  player one's accelerate/fire/brake/hand-brake slots (`[v7d8a4]` = `cfg7b6d8` too, +5/+7/+9/+11);
  when a joystick is present (`joyfound` != 1) a pressed game-port button 1-4 (0x201 bits 4-7 low)
  also sets them.
- CONFIRMED (code 0x3784b `joy_poll` + src/ww.c `ctl_setup` 0x14b00): the joystick is timed, not
  read: after `out 0x201` the loop counts how long each axis bit stays high (`joy_xbit`/`joy_ybit`,
  summed into `joy_ax`/`joy_ay` until `joy_bits` are all low; the y count is halved), with a
  16-bit timeout that sets `joyfound` = 1 (no stick). Each of the four direction keys is set when
  the doubled count is past the centre `joy_cx`/`joy_cy` by at least its limit `joy_lim_l/r/u/d`.
  `ctl_setup(which)` loads the centre and limits from the CFG calibration - player one (which 0):
  `cfg7ee1c` shorts +0x1c..+0x26, port bits 1/2/3; player two: `cfg77128` +8..+0x12, bits 4/8/12 -
  and sets `joy_skip` when that player has no stick. SETUP's calibration screen (0x129a4: centre,
  top-left, bottom-right) writes those fields (findings/formats.md). LIKELY that each limit is a
  fraction of the calibrated range: SETUP's arithmetic is not read.
- CONFIRMED (the C side, src/ww.c): `aborted == 1` / `key88cd0 == 1` tests are "Esc (scancode 1)
  was the last key event and it was a press": the handler never writes a separate abort flag.

## Traps

- 0x88cd0 has three names: `last_scan` (asm, names file), `aborted` (`char`) and `key88cd0`
  (`volatile char`) in src/ww.c. The C keeps both of its names on purpose: the qualifier changes
  the code Watcom emits, so merging them can break MATCHes (iter 91d left them; `key_up`/`key_down`,
  both plain `char` aliases, were merged and check.sh stayed at 207).
- A difffn seed of 0x88cd0 = 1 is "Esc pressed", not "abort requested" (GOTCHAS RESEED note: the
  abort arm 0x31eae re-reads it every frame).
