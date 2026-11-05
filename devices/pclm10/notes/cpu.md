# The processor: cores and clock

Why this file exists: with the touch driver verified end to end (33 points a second reaching
backboardd, `logs/001019-run.log`) the UI is still "巨卡无比、划不动". The driver is not the
bottleneck. Two platform facts are, and both are how the reference device learned it: one core
draws 671 frames where four draw 929, with peaks of 19 vs 35 fps (`docs/ru/notes.md`, the redwood
notes), and the reference phone's standard run is `cpus=8 bootcpu=7 cpufreq=1` — cores and clock
(`devices/redwood/device.conf`, the cpu line). Software rendering through the UEFI framebuffer is
the third fact and the biggest; R6 below measured that it is what is left.

## How the cores come up here

Nothing needs rebuilding to try this: the loader already lists every core that is free of it
(every round's log has `cores for the kernel, in the order it takes them: 700 600 500 400 300 200
100`), the device tree it builds lists all eight cpu nodes, and `cpus=N` in the kernel's
arguments says how many it takes — the boot core (`bootcpu=7`, the biggest) and the N-1 nearest
its number. The kernel starts them itself with PSCI CPU_ON (`xnu/files/osfmk/arm64/psci_smp.c`);
a core that fails prints `psci: cpu N ...` with the firmware's answer, and success is silent.
The phone's own tree says PSCI is called with SMC, which is what the loader and kernel do.

## R1, 00:32: four cores answer (`logs/003246-run.log`)

`cpus=4` with everything else as in the verified touch round. The kernel's command line shows the
argument twice (the loader's `cmdline:` line and launchd's `boot-args =` dump), so the silence of
the failure lines means something: **no `psci:` line at all** — cores 6, 5 and 4 took CPU_ON and
joined within the five seconds the driver waits. No panic; the touch driver's alive lines continue
to `pass 13000` at t=717 s with zero failed reads; SpringBoard and backboardd come up as before;
and the loopback job answers in 0.00 s where the one-core rounds took 0.02 and 0.06 s. So SMP on
SM8150 works as-is: no XNU change, no loader change, only the boot-arg.

## R4, 01:09: seven cores, and what seven cores are worth (`logs/010909-run.log`)

`cpus=8`: the kernel starts seven, not eight, and that is by design — the loader itself runs on
phys 0x000 and hands over only the cores it is done with (`xnuloader/loader.c:3390` skips its own
core), so seven is the ceiling. `GenericARMPE: ... 7 processors running`, no `psci:` line, no
panic, the demo job's ten gestures run, the loopback answers in 0.00 s.

What they bought is measured, not argued: the display driver's five-second line says a flat
**10 frames a second** (12-13 during the gestures), and inside that, the driver spends ~10.5 ms
per frame and sits **89% idle**. Four times the cores did not move the frame rate, because the
frames are not waiting on other cores: backboardd draws 1080x2400 in one software thread, and
that thread was running at whatever clock the firmware left — which R5 found out exactly.

## The clock: what the driver needs for this SoC

`drivers/power/hakios.cpufreq.c` was written for the reference phone: three EPSS groups at
0x18591000, 0x1000 apart, each with a table of frequencies at +0x100 (a word per row: bits
[31:30] pick the clock source, bits [7:0] are a multiple of the 19.2 MHz XO) and one register at
+0x320 that names the wanted row; writing the top row is the whole driver. Mainline's
`sm8150.dtsi` (`cpufreq@18323000`, `qcom,sm8150-cpufreq-hw`) says where this SoC's three windows
really are: **0x18323000, 0x18325800 and 0x18327800**, for the silver cores (0-3), the gold ones
(4-6) and the prime (7) — the same 4+3+1 grouping the driver's `domain_of()` already assumes —
and the driver reads its three windows from boot-args `epss0=`, `epss1=`, `epss2=`.

What the first version got wrong is the layout. The reference phone's EPSS is the newer kind
(Linux's `epss_soc_data`); the Kryo-3xx/4xx generations are the older kind, which Linux's
`qcom-cpufreq-hw.c` describes through `qcom_soc_data`: **the rows live at +0x110 and are 32
bytes apart, the supply voltage is the word next to the frequency (+0x114), and the row wanted is
asked for far away at +0x920** (the dcvs register at +0xbc, not +0xb0). Boot-arg `epss_hw=1`
selects that layout; everything else is the same registers under other offsets.

## R2, 00:49: the wrong window, and a lesson kept (`logs/004950-run.log`)

The first cpufreq round, before anyone had compared the two layouts: it read +0x100, found all
zeros, concluded the table was empty, and wrote 0 to +0x320 — which in this layout is not the
row-wanted register at all but a word inside the table region itself. The prime's test thread
stopped making progress after the driver's last line. The round's conclusion ("nobody programs
this table; Android's CPUCC driver does, nothing does under our loader") was wrong, and R5
proved it wrong; what R2 really found is that writing a register on an assumption is not free.
The empty-table path stayed in the driver as `cpufreq_fix=1` — for a future SoC whose table
really is empty — and `cpufreq_look=1` exists so the next unknown SoC gets read before it gets
written.

## R5, 01:35: read-only, and the answer (`logs/013517-run.log`)

`cpus=4 cpufreq=1 ddr=0 epss0/1/2 epss_hw=1 cpufreq_look=1`: dump everything, write nothing.

- **The tables are full.** All forty rows of all three groups hold the vendor's own numbers:
  silver 300 → 1785 MHz, golds 710 → 2419 MHz, prime 825 → 2956 MHz, with real per-row voltages
  from 612 mV to 1016 mV. The firmware fills them; nobody has to.
- **The row encoding is confirmed**: `entry 0x40041394` decodes as source 1 (the 19.2 MHz XO),
  lval 0x94 = 148 → 2841 MHz, core-count 4 — matching the neighbours' arithmetic row for row.
- **The hardware is enabled**: `enable 0x3` on every group (bit 0 set), dcvs off (0).
- **And here is the 10 fps**: the row wanted is **15 for group 0 and 0 for groups 1 and 2** —
  the firmware leaves the golds at 710 MHz and the prime, which is where `bootcpu=7` puts
  backboardd, at **825 MHz**. Seven fast cores drawing at 825 MHz is R4's flat 10 fps.
- Two things to not believe: the "current frequency" registers (+0x20 and +0x704) read the same
  values for all three groups, so they are not this layout's real state; the row wanted plus the
  table is the truth.

So no `cpufreq_fix` is needed here at all: `top_row()` walks the real table to where the
frequency stops rising (rows 17, 16, 20) and asks for those.

## R6, 01:39: the clock raised, and what it bought (`logs/013949-run.log`)

`cpus=8 ... epss_hw=1 touch_lat=1` — R4 exactly, plus the working layout. The driver:
`group 0: row 15 -> 17 ... 1785 MHz`, `group 1: row 0 -> 16 ... 2419 MHz`, `group 2: row 0 -> 20
... 2956 MHz`, and its own verification, `on core 7 a loop ran at 149, now at 536` — **3.6x
single-core**, which is the 825 → 2956 MHz ratio. No panic; touch alive to the end; the ten
gestures run; the loopback answers in 0.00 s.

The frames, counted the same way as R4: R4 was 21 samples at 10 fps with a best of 13; R6 has
**segments at 14, 15, 16 and 20 fps** — the ceiling did move — but still 22 samples at 10 fps,
with the driver at ~5-7 ms per frame and the rest idle. So the honest reading is: the clock fix
is real and belongs in the configuration, and the flat 10 fps is now the software drawing path
itself, not the clock. The next order of magnitude is the GPU (`fw-gpu`, `adreno=`), which takes
the drawing off the cores entirely.

## R7, 01:49: the memory vote, and where this phone hides its command database (`logs/014945-run.log`)

R6 with `ddr=0` dropped: the driver's default is to take the memory-bandwidth vote before raising
the clock, and the pair had never been on the phone at once. The clock raise repeated exactly; the
vote said `ddr: no vote (MC0 0, SH0 0, ACV 0, vote 16383)` — the driver looks for the command
database at the reference phone's fixed address, 0x80860000, and this phone's is not there. Its
tree keeps a pointer instead (`qcom,cmd-db@c3f000c`), and this phone's touch driver had already
followed it to the table: 0x85f20000, version 0x1, magic 0xc0330db, 4424 bytes, stable across
every round (logs/012447-run.log). Two more things about this table: its entries do not group the
way the header's slave records suggest — MC0 and ACV sit under different slave ids, and the
records' offsets are table-absolute, not after the data — so the driver now finds an entry by its
name the way the touch driver does: eight bytes of id at the front of every entry, and the address
two words further on (+16; the word right after the name is zero for every bus clock manager,
which is what an earlier reading of this mistook for a missing address). Boot-arg `cmdb=PHYS`
names the table; the reference phone needs nothing.

## R8, 01:59: the vote goes through, and the configuration is written (`logs/015951-run.log`)

R7 plus `cmdb=0x85f20000`: `ddr: memory bandwidth vote 16383 on MC0 (0x50000) and SH0 (0x5003c)`,
no FAILED, and on the way down `ddr: memory bandwidth vote 1024 for the loader: done`. The clock
raise repeats exactly (rows 17, 16, 20; the loop on core 7, 149 → 536); no panic; touch alive to
the end; the ten gestures run; the loopback answers in 0.00 s.

The frames: the best segment of the whole series — **31 fps**, and a 19 — but still 25 samples at
10 fps. So the vote is confirmed free of side effects and the peak is a little higher, and R6's
conclusion stands untouched: the flat 10 fps is the software drawing of 1080x2400, and neither the
clock nor the memory bandwidth moves it. The order of magnitude belongs to the GPU.

What R8 verified is what the device now ships: the SoC's facts — seven cores, the older layout,
the three windows — in `devices/pclm10/soc/configs/sm8150.config` (`cpus=8 bootcpu=7 cpufreq=1
epss_hw=1 epss0/1/2`), and this phone's cmd-db address in `devices/pclm10/configs/pclm10.config`
(`CONFIG_BOOTARGS`, `cmdb=0x85f20000`). `xl run` passes only `XL_CMDLINE`; it is `make phone`
that hands a `.config`'s arguments to the kernel (`tools/hakios-config.py args`), so a round that
means to check the shipping string passes that string as `XL_CMDLINE`.

## R9, 02:12: the configuration's own string, verbatim (`logs/021259-run.log`)

`cpus=8 bootcpu=7 cpufreq=1 ufs=2 pon=1 bq27541=1 wled=1 cmdb=0x85f20000 epss0/1/2 epss_hw=1`
— the output of `tools/hakios-config.py args` for the landed configuration, nothing added. The
vote went through (MC0, SH0, and the quiesce done), all three rows were raised, the loop line
said 149 → 536, SpringBoard and backboardd came up, the loopback answered in 0.00 s, no panic.
Two things that round taught:

- `ufs=2` — the common default, never sent by any earlier xl round, and this device does not
  compile the storage driver — is inert, as expected: the `ufs:` lines in the log are the loader
  enumerating its LUNs (one of the phone's own partitions is even named "ddr", which is a red
  herring next to the vote line).
- The demo job ran its gestures even with no physical touch driver: `/dev/hakios-touch` is served
  by the input core to anything that posts, and rwtouch posts contacts itself — backboardd
  received them ("contact 1 presence: touching") and SpringBoard reacted. The physical panel
  stayed silent because `touch=1` was not in the string, and that is the configuration working as
  written, not a failure.

The frames were the idle-and-gestures shape of the earlier rounds — 31 samples at 10 fps, one at
28 — which is the same conclusion a third time: everything the clock and the memory can do, the
configuration now does on every boot, and what is left belongs to the GPU.

## Measuring the smoothness, not arguing about it

"巨卡无比" is a verdict about frames, so the answer should be frames too. The instrument R4 and
R6 used:

- `userland/rc.jobs.pclsmooth` (built into `root.apfs` with `RC_JOBS=rc.jobs.pclsmooth`): the
  same gestures in the same order every round — four home-screen page swipes and two Search
  pulls, rwtouch driving them through `/dev/hakios-touch`, which the input core serves for any
  driver that posts, ours included. The waits come from this phone's own boot timeline (the job
  starts at t+9 s, the window server answers at t+15 s, so `wait 26` puts the first gesture at
  t+35 s); nothing depends on an icon's place. The loopback runs first: it costs 0.00 s when the
  stack is well and says the network side is alive in the same log.
- boot-args `touch_lat=1`: the input core, the window server's injected library and the frame-
  buffer driver write their `tl:` lines; `tools/frames.py` turns them into frames a second and
  the share of frame-to-frame times over 16.7 ms — the objective version of the complaint.
  `rec_secs=120` adds a screen recording (`xl recording`) so the same run can be watched.
- The comparison that was worth having — one core versus everything on — is done (R4 vs R6).
  What is left on this list is the user's eyes on a real drag, and the GPU round.

## What is left

- The user's eyes on the home screen: the whole point is whether a drag drags. R6's log says the
  machine is 3.6x faster on one core and the frame curve has 14-20 fps segments in it (R8's has a
  31); whether that feels different is a question for a person, not a log.
- The GPU: the frame-drawing itself. Everything here makes the drawing core faster; the GPU would
  make the drawing not happen on any core. Needs `fw-gpu` (the a6xx zap shader and microcode) and
  `adreno=`.
