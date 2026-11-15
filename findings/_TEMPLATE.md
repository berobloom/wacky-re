# <Subsystem>

Status: OPEN (<what is left>) | CLOSED (pinned by <check>)

## Summary

Three to five lines a newcomer needs: what the subsystem does and the key
addresses.

## Findings

One entry per claim: the level, the evidence in brackets, the claim, then
where the port implements it and how it was compared.

- CONFIRMED (<code 0xADDR / capture captures/x.bin / experiment>): <claim>.
- STRONGLY SUPPORTED (<code only, not run>): <claim>.
- LIKELY: <claim>. Gap: <what is missing>.
- SPECULATIVE: <claim>. Test: <the experiment that would decide it>.

## Traps

Game-specific traps of this subsystem (harness traps go in GOTCHAS.md).

---

## Worked example (redcat, delete when copying)

Two real entries, to show the level of detail that made them reusable:

- CONFIRMED (code 0x111ee, 0x114b8, 0x15a14, 0x22580): after a world's last
  race the leader is the first racer with strictly more points than the best
  so far, starting from 0 with none (esi 0, edi −1), so ties go to RedCat,
  then Brutus; no podium is shown. `0x15a14` stops track 0 and calls the
  Smacker player `0x22580([0x7f954], 0, 0, [0x7f95e], [0x7f962], name,
  flags 0x2a, track 0, sample 0x21)` (LIKELY screen, x, y, width, height):
  0x20 sets mode 13h and restores the mode afterwards, 2 keeps the film's
  palette, 8 turns off the sound sync. Port: `game.c` `start_winner_film`
  (not yet compared against DOSBox).
- CONFIRMED (code; DOSBox-X, memory polling): delays. `0x249f3(ms)` waits
  until clock() × 100 ≥ start × 100 + ms × 10; clock() counts hundredths from
  INT 21h AH=2Ch, which moves with the BIOS tick. So delay(5) and delay(40)
  last to the next tick, delay(1000) 19 ticks. Measured by polling the back
  buffer and 0x46C together.

What makes them good: every address named; the evidence says how it was
known (code read vs run); the uncertain part is split off and labelled LIKELY
instead of lowering the whole entry; edge cases (ties, nothing qualifies) are
stated; the port location and its comparison status are given.
