# Touch — Synaptics S3706 on the PCLM10

What the 2026-11-02 rounds proved, in the order the proofs came. Logs live in `hakios/logs/`;
the driver is `devices/pclm10/drivers/touch/hakios.s3706.c`, and everything below was read
from the real phone through it.

## The bus

The QUPv3 serial engine (qup2, HW 1.3.0) will not start on this phone — its FIFO interface
reports GPI-only — so the chip is asked through hand-moved pads: **gpio56 = SCL, gpio55 = SDA**
(the roles were proven by driving each and watching the other), the chip's address is 0x20, and
reset (gpio54) sits high. The enable is **PM8150L (SPMI slave 4) GPIO 5, active high**, driven
high by default; `ldoa17` (L17) feeds the panel's `lab-supply` and the touch's `vdd_2v8` and
stays off unless `touch_2v8=1` votes it (mode 2).

The one bus rule that matters: **a read longer than four bytes has never answered honestly.** The
21:25 run read the F12 data base as eight bytes and got zeros through fourteen events that the
very same round's four-byte reads answered with values. Every read taken since is four bytes or
two. Note the rule is only proved for eight bytes — and the vendor's own `get_touch_points` asks
for eight and works on the real bus — so whether a five- or six-byte read streams honestly is an
open, and decisive, question (the 22:59 round asks it).

## The function table

The vendor's `get_chip_info` (vendor-src/touch/synaptics_drivers_s3706.c:176) reads four bytes
at 0xdd, 0xe3 and 0xe9 and uses them as query, command, control and data bases directly, with
no descriptor walk — and four-byte reads at function bases are exactly the shape this bus
answers. On this chip they say:

| descriptor | function | query | cmd | ctrl | data |
|---|---|---|---|---|---|
| 0xDD | F12 2d touch | 0x49 | 0x00 | 0x13 | 0x08 |
| 0xE3 | F01 device control | 0x2a | 0x21 | 0x0d | 0x06 |
| 0xE9 | F34 flash | 0x22 | 0x00 | 0x0c | 0x00 |

The F01 status at 0x06 reads `81 24` at rest and `81 00`/`81 04`/`81 20` as events come; its
bit 2 is the F12 function asking for its data, and reading the status is what clears the
interrupt. The F12 control base at 0x13, read as the vendor's `fw_check` reads it, gives
**max x 1079, max y 2399** — this panel's real resolution. The F12 query region at 0x49 gives
**807 x 3863** — the sensor's own maximum, confirmed by two overlapping reads that agree byte
for byte (22:18 run).

## x is settled

Objects live at the F12 data base 0x08; the vendor's layout is eight bytes per object:
status, x low, x high, y low, y high, strength, touch major, width. The 22:18 run
(logs/221833-run.log, 557 passes with a finger) proved the first part of that on this glass:
**x is the full sixteen-bit little-endian number at 0x09-0x0a, already scaled to the display's
1079** — it tracked a finger from 42 to 1065, and the byte at 0x0a carries real high bits
(`0x04` at x 1064). No scaling or flipping is needed for x.

## y was the open question: the data region is a window, not a flat map

Three facts from the same run do not fit a flat register map:

1. The byte at 0x0b — y low in the vendor's layout — held between **50 and 113 through every
   press**: left edge, right edge, and the last press at x 989. That is no vertical coordinate
   across 2399 lines.
2. The byte at 0x0c answers **0xcc when it is the first byte of a burst** (even with no finger
   on the glass) and **0x00 when it is the second byte of a two-byte read started at 0x0b**.
3. The two-byte read at 0x0b — exactly the vendor's `read_word(F12_2D_DATA15)` attention read
   — answers **0x0001 with a finger and 0x0000 without**, tracking presence exactly. That is
   the attention word, and on working hardware it must be honest.

So the same addresses answer differently depending on where the read starts: **the F12 data
region behaves as a window into the chip's report, not a flat map.** The vendor's own shapes —
word at data+3, then an eight-byte block from the data base — are what ColorOS's driver uses
and what works on the real bus; our eight-byte read answers zeros, which is the window
misbehaving for a hand-moved clock, not the chip being silent.

## The 22:45 run: window proven, presses lost

The three overlapping views answered exactly once each, and decisively:

- the view from **0x09 read `00 00 00 00` in all 139 events** while the view from 0x08 answered
  the object — the flat-map cross-check failed 139 times out of 139. The region is a window; the
  0x09 view is dead.
- the view from **0x0c read `cc 18 30 14` in every pass**, finger or not.
- the word at 0x0b tracked presence (`01 00` / `00 00`) as before; 0 failed transactions.

But the screen lit near 320 s and the watch ended at 340 s, so only one brief left-edge press was
caught (x 41–69). No corner holds, no drags. The screen-on moment has drifted to 175, 213, 263 and
now ~320 s across rounds — any watch must run on wall time and extend past 350 s.

## The 23:01 run: y settled

2478 passes, 998 bit-2 events, 871 with a finger, 0 failed transactions — and the first real
presses. The stream test answered outright:

- Of the 43 distinct events printed, the **5-byte read from 0x08 repeated the 4-byte read's four
  bytes 39 times**, differed by ±1 in 4 (a held finger's jitter between reads a sixth of a second
  apart) and was never worse. The 6-byte read repeated the 5-byte read's five bytes 35 times, ±1
  in 5, worse in 3 (real movement between reads). **The window streams; the fifth byte is the y
  high byte.**
- The object is `[count, x_lo, x_hi, y_lo, y_hi]`: `x = b1 | (b2 & 0x0F) << 8`,
  `y = b3 | (b4 & 0x0F) << 8`, and **both are already display-scaled** — the top-left hold read
  (72, 65) and held, the bottom-right hold (1002, 2333) and held, and a drag down the middle of
  the glass swept y 72→93→123→203→284→330→360→397→413→464→496→516→549 monotonically while x
  stayed within a finger's width. Whole-run ranges: x 58–1002, y 31–2399 — the panel's own
  1079×2399 (the ctrl 0x13 confirmation), not the query's 807×3863.
- The sixth byte is strength: 0x1f–0x3b under a finger, 0x00 without one.
- The flat test: the 0x0d view repeated the 0x0c view's last three bytes **0 times in 998**
  (`00 7f 08 14` against `cc 18 30 14`). Every address's answer belongs to the window that was
  asked; both constants are window artifacts.

The map is closed. The one lie this bus tells is an eight-byte read; **a five-byte read from the
data base is the smallest honest read that reaches y_hi.**

## The events round (built 23:27)

`bb_touch_demo` became `bb_touch_run`, the driver's production loop: every 12 ms, page select →
F01 status at 0x06 (this clears the chip's interrupt) → if the interrupt byte's bit 2 is set, the
attention word at 0x0b (the vendor's order; the word itself is not needed for the parse) →
**five-byte object at 0x08**. `count != 0` is a finger; parse x and y, clamp to 1079×2399, scale
to the record's **0–65535** (`x * 65535 / 1079`, `y * 65535 / 2399`, the goodix idiom), and post
`hakios_touch_post(DOWN/MOVE/UP)`: DOWN on first sight, MOVE on any change, UP at the last
position on a count-zero report. One finger (object 0 only). The first 80 events print with both
the scaled and the glass's own coordinates, then the console goes quiet; a count line every ~60 s
says the loop is alive; after 100 silent passes in a row the bus is asked a tenth of a second
apart, forever. The bit-bang path no longer restores the two pins when the chip answered as RMI4
— the loop owns them — and the F12 query-region read left with the watch.

The test is the screen itself: once it lights (~300–320 s — real time, not the kernel clock),
swipe, tap icons, drag things. If the UI answers the finger, backboardd is reading the records.
If the log shows events and the UI does not answer, the records are right and the backboardd-side
synthesizer is the suspect; if the log shows nothing while the chip is reporting, the read order
regressed and the raw lines will say so.

## The 23:28 run: the chip went silent, and the reason is the poll rate

The loop ran (pre-touch status read `81 00 00 00`) and then said nothing for the rest of the
round: no DOWN, no MOVE, no UP, no count line, not one status change — through the whole press
window after the screen lit at 23:35:42. The final fetch came back truncated (5.7 KB against the
1.3 MB the live snapshots had), so the real log is `logs/232836-run.log.live.100`.

The one behavioral difference from the healthy 23:01 watch was the pace. At 12 ms the loop read
the F01 status eighty times a second from ~30 s of boot; every read clears the chip's interrupt,
and none took the report with it. By the time a finger went down the interrupt byte had been 0x00
through some two million reads. The watch at 150 ms saw that byte change 428 times and answer 998
bit-2 events. **A controller whose interrupt is cleared eighty times a second without its report
being taken is a controller that stops telling.** Two more flaws came with that round: the alive
line sat after the no-event `continue` and could never print, and status edges were never shown —
the watch's status-change lines are what proved the chip alive.

## The events round, take two (built 23:55)

`bb_touch_run` now polls at the proven 150 ms pace, prints every F01 status edge (capped at 40,
with a bit-2 marker), and prints an alive line every 1000 passes (~150 s) whatever the bus is
doing, so a silent chip and a quiet one are told apart. The parse and the posting are unchanged.
The round's log must be read from the live snapshots while it runs: the final fetch can return a
reset buffer.

## The 23:57 run: the UI answers a finger — and the pace is now the problem

15 DOWNs, 51 MOVEs, 14 UPs printed (the 80-line print cap, then the console goes quiet while the
posting continues), and `backboardd [TouchEvents] contact 1 presence: touching` in the syslog —
the iOS HID path read the records and the icons answered. The user's taps opened what they
pointed at; corner holds and drags came through with sane coordinates.

But the user's verdict was "非常卡顿，点了一个图标就退不出去了": a whole drag produced **two
MOVEs** — six points a second is below what a back-swipe or a fling needs, so the app opened and
the way back did not answer. The idle 150 ms pace stays (it is the one the chip tolerates); the
loop now goes **fast (30 ms) while the chip is reporting**: any bit 2 opens a 40-read window,
refreshed by every bit 2, so a held finger is followed near its report rate and an idle stretch
costs at most forty fast reads. That is the vendor's interrupt-driven rhythm — interrupt, take
the report, interrupt — and the silence mechanism of 23:28 (a cleared interrupt with no report,
tens of thousands of times) cannot apply to a window whose every bit 2 is answered. Event lines
now carry the pass number so the MOVE rate is measurable from the log directly.

## The seven processors: the half-period was a scheduler tick (2026-11-03)

The shipping string runs seven processors, and on it the touch loop saw the chip's interrupt
(bit 2 of the F01 status at 0x06) but every five-byte report read came back `00 00 00 00 00`
(`logs/204137-run.log`, `logs/205314-run.log`, `logs/210105-run.log`), while the same driver
with one processor read 72 contacts (`logs/204724-run.log`). The first suspect was preemption;
`disable_preemption()` around each transaction did not change the zeros (`logs/205314-run.log`).

The real cause was the half-period. `sleep_us()` reaches `delay_for_interval()`, which parks the
thread until the next timer interrupt -- about 7.5 ms on this kernel -- so every byte of the
hand-moved bus was several milliseconds wide and the report window was read slowly enough, under
SMP, to answer zeros. The fix is to spin the half-period against the timebase
(`bb_spin_us()`: `clock_interval_to_deadline()` plus `mach_absolute_time()`), which makes the
bus the ~40 kHz the pins were chosen for. `logs/210849-run.log` is the proof on seven
processors: the object read now takes **2388 us**, and the round posted **37 downs, 118 moves
and 36 ups** with zero failed reads. The zero-object events that remain are the chip's empty
wake interrupts -- the attention word at 0x0b says `00 00`, and a read of the F01 query at 0x2a
right afterwards answers `01 e0 00 00`, so the bus is honest and the window really has no
finger.

## The interrupt line is the pace (2026-11-03)

The two-speed poll still left a tap waiting up to the 150 ms idle read. The chip's interrupt
line is **gpio122**, an input with its pull-up, **low while it is asking** (the pin the driver
had only looked at). The loop now watches that pin: while it is high it sleeps one millisecond
(one scheduler tick, ~7.5 ms), and the moment the chip pulls it low it reads the status and
takes the report. The status register -- the read that clears the interrupt -- is only touched
when the pin asks or the 150 ms heartbeat comes due, which is the vendor's interrupt-driven
rhythm with polling in place of the GIC handler and avoids the 23:28 failure (status read eighty
times a second with no report taken, after which the chip went silent). `logs/211524-run.log`
is that round: **26 downs, 682 moves and 25 ups, zero failed reads**, the object read steady at
2385 us, and the MOVE count jumped from 118 in the 210849 round to 682 -- the loop is following
the chip's own reports instead of a 30 ms timer.

## What is left

- Verify the two-speed round: taps fast, scrolling smooth, a back-swipe out of an app.
- The single core (`cpus=1`) also costs smoothness; SMP is its own track, not touch's — it is
  proven now, see `notes/cpu.md` (four cores answer, `logs/003246-run.log`).
- Then `CONFIG_HAKIOS_TOUCH=y` with its boot-arg, and the `status touch works` line in
  `device.conf`.
- A faster idle pace is tempting but must come after the UI is smooth, and carefully: the 12 ms
  experiment says the chip does not tolerate a status read that clears its interrupt without a
  report. Multi-touch (a second five-byte read at the next object's base — whether the window
  answers it is untested) and the interrupt line (the QUPv3 engine still will not start; polling
  is the design for now).
