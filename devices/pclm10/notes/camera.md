# The cameras of the OPPO Reno Ace (PCLM10)

What the phone's own device tree and the vendor clock/pinctrl drivers say about the five sensors,
and what the first driver (`devices/pclm10/drivers/camera/hakios.flash.c`) does with it. The
camera part below has not been run on the phone yet: it was written from the tree, not observed.
The register-level `cam=1` and `cam=2` runs are the next step. (The flash half of the same file
has run; see `remaining-hardware.md`.)

## The tree's camera nodes

All references are to `devices/pclm10/vendor-src/dtb/dtbo.bin.01.dts` (the phone's own overlay,
`oppo,prjversion = <0x01 0x4a89>`, the Reno Ace project) and `android-merged.dts` (the base tree
the overlay lands on). The overlay's camera fragment is fragment@42, opened at `dtbo.bin.01.dts:5378`.

| node | lines | what it gives |
|---|---|---|
| `qcom,cam-res-mgr` | 5385-5387 | the camera resource manager, `status = "ok"` |
| `qcom,actuator@0` | 5390-5404 | rear main AF, CCI 0 master 0, `cam_vaf` 3.25 V (the `af_ldo` fixed regulator at `dtbo:5261-5274`, enabled by TLMM gpio11) |
| `qcom,actuator@1` | 5405-5420 | rear ultra-wide AF, CCI 0 master 1, `cam_vaf` 2.8 V (phandle 0x64 = PM8008 #2 L6) |
| `qcom,actuator@3` | 5421-5436 | tele AF, CCI 1 master 0, `cam_vaf` 2.8 V (phandle 0x65 = PM8008 #1 L3) |
| `qcom,ois@1`, `qcom,ois@0` | 5437-5468 | the two OIS coils; `ois@0` is the rear main one (`ois-src` of sensor 0) |
| `qcom,eeprom@0..3` | 5469-5608 | four separate EEPROMs, for sensors 0/1/2/3 (main, ultra-wide, front, tele); the mono (4) has none |
| `qcom,cam-sensor@0..4` | 5609-5811 | the five sensors, below |

The overlay's `__symbols__` names the same nodes (`dtbo.bin.01.dts:8002-8010`): `actuator_rear`,
`actuator_rear_aux`, `actuator_tele`, `eeprom_rear`, `eeprom_rear_aux`, `eeprom_front`,
`eeprom_tele`. This is useful because it fixes the roles: cell-index 1 is the rear **aux**
(ultra-wide), cell-index 3 is the **tele**, cell-index 2 is the **front**.

### The five sensors

The tree does **not** name the sensor model. Qualcomm's camera stack chooses the sensor in
userspace from a module file; the DT gives only the bus, the PHY, the MCLK, the reset and the
rails. The model column below is this port's reading of the public specifications plus the chip
IDs the drivers in `drivers/media/i2c/` use; the `cam=4` run is what confirms or falsifies it.

| cell-index | node lines | role / expected model | CCI | CSIPHY | MCLK | reset | enable |
|---|---|---|---|---|---|---|---|
| 0 | 5609-5650 | rear main, Sony **IMX586** 48 MP | 0 master 0, gpio17/18 | 0 | MCLK0, gpio13, 19.2 MHz (`clock-rates 0x124f800`) | **PM8150 GPIO1** | — |
| 1 | 5651-5691 | rear ultra-wide, Sony **IMX319** 8 MP | 0 master 1, gpio19/20 | 1 | MCLK1, gpio14, 19.2 MHz | TLMM gpio30 | — |
| 2 | 5692-5731 | front, Sony **IMX471** 16 MP | 1 master 1, gpio33/34 | 2 | MCLK2, gpio15, 24 MHz (`0x16e3600`) | TLMM gpio12 | `CAM_FRONT_EN`, gpio29 |
| 3 | 5732-5771 | rear telephoto, Samsung **S5K3M5** 13 MP | 1 master 0, gpio31/32 | 3 | MCLK3, gpio16, 19.2 MHz | TLMM gpio23 | — |
| 4 | 5772-5811 | rear mono, 2 MP OmniVision (part not settled) | 1 master 0, gpio31/32 | 2 (shared with front) | MCLK2, gpio15, 24 MHz | TLMM gpio12 | `CAM_MONO_EN`, gpio29 |

Notes on the table:

- The `gpio-req-tbl-label` strings in the tree are copied inconsistently (sensor 0 says
  `CAMIF_MCLK3` while its `clocks` index is MCLK0, sensor 3 says `CAMIF_MCLK3` while it uses
  gpio16 = MCLK3). The **pin numbers**, `clocks = <&clock_camcc ...>` and `clock-rates` are the
  facts to follow. The base tree's `cam_sensor_mclk0..3_active` groups confirm gpio13/14/15/16
  (`android-merged.dts:10640-10755`).
- Sensor 1 and sensor 3 are the two that are easy to swap. The symbols above (`actuator_rear_aux`
  vs `actuator_tele`) and the tele's own actuator@3 fix sensor 1 as the ultra-wide and sensor 3 as
  the tele.
- Sensor 3 and sensor 4 say `cci-device = <1>; cci-master = <0>`, i.e. CCI 1's *other* master.
  The base tree only assigns CCI 1's `cci3_active` pins (gpio33/34) to that controller
  (`android-merged.dts:1600-1610`); the `cci2_active` group for gpio31/32 exists but is not
  referenced by the controller node. The camera overlay is the only place that uses that second
  master. The first driver bit-bangs gpio31/32 itself, so it does not depend on the CCI controller
  being muxed correctly.
- Sensor 2 and sensor 4 share CSIPHY 2; only one of them can be streaming at a time.

### The rails

The two PM8008s are both on serial engine 4: `i2c@890000`, pins gpio51 (data) / gpio52 (clock),
`qupv3_se4_i2c_active` at `android-merged.dts:7259-7277`. Their nodes are
`dtbo.bin.01.dts:6139-6368`:

| chip | I2C addresses | enable | LDOs and the tree's set points |
|---|---|---|---|
| PM8008 #1 | chip `0x08`, regulators `0x09` | **PM8150L GPIO1** (`dtbo:1522-1534`, slave 4 peripheral 0xc0) | L1 1.1 V, L2 1.05 V, L3 2.8 V, L4 2.8 V, L5 2.8 V, L6 2.8 V, L7 3.25 V |
| PM8008 #2 | chip `0x04`, regulators `0x05` | **PM8150L GPIO4** (`dtbo:1536-1546`, slave 4 peripheral 0xc3) | L1 1.1 V, L2 1.05 V, L3 2.8 V, L4 1.8 V, L5 2.8 V, L6 2.8 V, L7 2.9 V |

The sensor nodes' phandles map to these rails (`dtbo.bin.01.dts:5619-5627`, `5683-5689`,
`5723-5727`, `5762-5768`, `5796-5800`):

| sensor | VANA | VDIG | VAF | V_CUSTOM1 / other | VIO |
|---|---|---|---|---|---|
| 0 main (IMX586) | PM8008 #2 L7 2.9 V | PM8008 #2 L1 1.1 V | PM8008 #2 L3 2.8 V | PM8008 #2 L4 1.8 V | PM8150L L1 (external; the tree's `pm8150l_l1`) |
| 1 ultra-wide (IMX319) | PM8008 #1 L4 2.8 V | PM8008 #1 L1 1.1 V | — | — | PM8150L L1 |
| 2 front (IMX471) | PM8008 #2 L5 2.8 V | PM8008 #2 L2 1.05 V | — | — | PM8150L L1 |
| 3 tele (S5K3M5) | PM8008 #1 L6 2.8 V | PM8008 #1 L2 1.05 V | — (actuator@3 has its own `cam_vaf` on PM8008 #1 L3 2.8 V) | — | PM8150L L1 |
| 4 mono (OV) | PM8008 #1 L5 2.8 V | — | — | — | PM8150L L1 |

**The PM8150L's GPIOs are on SPMI slave 4, not slave 5.** The PM8150L appears twice in the
overlay: `qcom,pm8150l@4` (`dtbo:1386`) carries the revid, power-on and the `pinctrl@c000` with
the PM8008 enables; `qcom,pm8150l@5` (`dtbo:1652`) carries the LCDB, the flash module at 0xd300
and the WLED. The flash driver's slave 5 is right for 0xd300; the camera enables are slave 4
peripheral 0xc0 (GPIO1) and 0xc3 (GPIO4). This distinction is easy to miss because both nodes are
called `pm8150l`.

The main sensor's reset is **not** a TLMM pin: sensor 0's second `gpios` entry is
`<0x08 0x01 0x00>`, phandle 0x08 = `pm8150_gpios` (the PM8150 at slave 0, `dtbo:86-94`), pin 1.
The matching pinctrl group is `cam_rear_reset_act/cam_rear_reset_active` (`dtbo:162-174`), also
under `qcom,pm8150@0`. The PM8150's `qcom,gpios-disallowed = <0x02 0x05 0x08>` leaves GPIO1
usable. The other four resets are ordinary TLMM pins.

### Clocks and the camera power domain

- `CAMCC` is at `0xad00000`; the titan top GDSC is `qcom,gdsc@0xad0c1bc`, register `0xad0c1bc`
  (`android-merged.dts:663-674`). Vendor source: `drivers/clk/qcom/camcc-sm8150.c`.
- The four MCLK roots and their branches (vendor `camcc-sm8150.c`): MCLK0 RCG `0x5004`, branch
  `0x501c`; MCLK1 `0x5024`/`0x503c`; MCLK2 `0x5044`/`0x505c`; MCLK3 `0x5064`/`0x507c`. The RCG's
  source 0 is the crystal (19.2 MHz); the divider field is `2n - 1`, so 19.2 MHz is source 0,
  divider 1. The 24 MHz the front/mono modules ask for needs CAMCC PLL2 (`0x2000`, `L = 0x32`,
  Regera) and the RCG's source 5 with M/N 1/4, divider 19; **that is not brought up in this first
  version** (the driver runs those two at 19.2 MHz for the ID read and says so).
- GCC's camera AHB and XO branches are at `0xb008` and `0xb044` (vendor `gcc-sm8150.c:1668-1724`),
  not the yupik offsets the common driver uses.

Vendor file:line for the clock numbers (branch `oppo/sm8150_s_12.1_oppo_reno10`):
`drivers/clk/qcom/camcc-sm8150.c` — MCLK0 RCG `1180`, MCLK1 `1200`, MCLK2 `1220`, MCLK3 `1240`;
their branches `2190`, `2208`, `2226`, `2244`; PLL2 config `322-378`. `drivers/clk/qcom/gcc-sm8150.c`
— camera AHB `1668-1683`, camera XO `1710-1724`. `drivers/pinctrl/qcom/pinctrl-sm8150.c:1599-1640`
— gpio13-16 `cam_mclk`, gpio17-20 and gpio31-34 `cci_i2c`.
`drivers/pinctrl/qcom/pinctrl-spmi-gpio.c:43-44` — the LV/MV subtypes; `60-67` — the non-LV
`MODE_CTL` bit positions; `365-375` and `622-628` — the two writes the driver makes. The sensor
IDs are from `drivers/media/i2c/imx471.c:25-26` (0x0471 at 0x0016), `imx319.c:21-22` (0x0319 at
0x0016), `s5k3m5.c:22-23` (0x30d5 at 0x0000); the IMX586's 0x0586 at 0x0016 is the same Sony
family and is confirmed by public module documentation; `ov02a10.c:19-22` is the OmniVision
comparison (0x2509 at register 0x02).

### Controllers (base tree, for the next step)

`android-merged.dts`: CSIPHY v1.1 at `0xac65000 + 0x1000*n` (1409-1490); CCI0 `0xac4a000`
(1493-1583), CCI1 `0xac4b000` (1585-1655); CSID0/1 `0xacb3000`/`0xacba000` and full IFE0/1
`0xacaf000`/`0xacb6000`, CSID-lite0/1 `0xacc8000`/`0xaccf000` and IFE-lite0/1
`0xacc4000`/`0xaccb000`, all `175`-generation (1976-2135). The main sensor's CSIPHY 0 is expected
to feed CSID0/IFE0 (full), not the lite pair; the vendor camera driver's port table is what will
settle it.

## What the first driver does

`devices/pclm10/drivers/camera/hakios.flash.c`, the existing `CONFIG_HAKIOS_FLASH=y` module, now
contains two halves. The flash half is unchanged. The camera half is a bring-up, not a camera:

- `cam=1`: read-only report. Maps TLMM/GCC/CAMCC/arbiter, prints the sensor table, GCC's camera
  AHB/XO, the titan GDSC, all four MCLK roots, pins 13-20/31-34/51-52, and opens (reads only) the
  three PMIC GPIOs: PM8150 GPIO1, PM8150L GPIO1, PM8150L GPIO4. If a PM8008 enable is high it reads
  all seven LDOs of that chip.
- `cam=2`: the rear main sensor. GCC camera branches, titan GDSC, PM8150 GPIO1 reset low, PM8008 #2
  enable high and chip enable, LDOs VDIG 1.1 V -> VANA 2.9 V -> VAF 2.8 V -> V_CUSTOM1 1.8 V,
  MCLK0 at 19.2 MHz, reset high, 20 ms, then a full CCI0-master0 scan (0x08..0x77), the chip ID
  read at 0x0016 and 0x0000 for every address that answers, and the EEPROMs at 0x50..0x57 read at
  0x0000. Then everything is put back in reverse.
- `cam=3`: the same for the front sensor (CCI1 master1, PM8008 #2 L5/L2, MCLK2 at 19.2 MHz for this
  first read, TLMM gpio12 reset, gpio29 front enable).
- `cam=4`: all five in turn, each powered down before the next. This is the inventory run.
- `cam=5`: the main sensor, left powered for a meter/scope.

Every write goes through the PMIC arbiter (`hakios_spmi_lock`) or the bit-banged PM8008/CCI buses,
is read back, and a disagreement stops the run and powers down. The PM8008 LDO set points are a
whitelist of the tree's own values; the driver never writes a voltage the tree does not name. It
only writes the PMIC GPIO registers named above (the five/six pinctrl registers), the PM8008
`0x0950` chip enable and the named LDO registers, the four CAMCC MCLK/GDSC registers, GCC's two
camera branches, and the sensor's own reset/enable pins.

## What to verify on the phone (next round)

No build or flash was done in this round; the phone was not touched. The next run should be:

1. **`cam=1` first, read-only.** It must find the three PMIC GPIO channels (PM8150 GPIO1, PM8150L
   GPIO1/4) and print their type/subtype and five registers. If a channel is missing, the arbiter's
   PPID assumption (slave 4 for the PM8150L GPIOs) is the first thing to question: read the
   arbiter's channel table and find where 0x04c0/0x04c3 are.
2. **`cam=2`, the main sensor.** Expected log: PM8008 #2 revision bytes; LDO1/7/3/4 ready with
   status `0x87`; MCLK0 on at 19.2 MHz; reset high; at least one CCI address answering. The main
   sensor is expected at 7-bit `0x10` (Sony modules often strap `0x1a` as well; the scan covers
   both) with ID `0x0586` at `0x0016`. An EEPROM at `0x50..0x57` should return non-`ff` bytes at
   `0x0000`.
3. If nothing answers, the order to check is: (a) PM8008 #2's enable GPIO actually high and its
   LDOs ready; (b) the reset polarity — the driver drives low then high, and the tree's pinctrl
   "active" state is output-low; (c) MCLK0's pin function and branch; (d) the CCI pins are not
   held by the CCI controller (the driver sets function 0 and pulls up; a stuck line is reported);
   (e) the PM8150L's L1 VIO, which the tree gives the sensors but this driver does not vote for
   (it is an RPMh rail, like the touch's L17; if `cam=2` has rails ready and MCLK but no ACK, that
   is the rail to read).
4. **`cam=4`** then identifies the rest. Expected: `0x0319` (IMX319) at sensor 1, `0x0471`
   (IMX471) at sensor 2, `0x30d5` (S5K3M5) at sensor 3, and a raw 2-byte ID for sensor 4. Sensor 2
   and 4 run at 19.2 MHz in this version; if their IDs do not answer, the next change is CAMCC PLL2
   and the 24 MHz MCLK path, not the power sequence.
5. Only after the IDs are confirmed: CSIPHY v1.1 power-up tables (`cam_csiphy_1_1_hwreg.h` from
   the vendor camera driver), CSID0/IFE0 for the main sensor, then a test pattern through CSID and
   a first frame. The common `drivers/camera/hakios.camera.c` is useful for the shape of that work
   (CCI bit-bang, PM8008 recipe, MCLK/GDSC ordering) but **none of its addresses or tables can be
   used as they stand**: see below.

## The common driver's applicability

Reusable, with the numbers replaced:

- the CCI bit-bang and bus-recovery pattern (`bus_take`, nine-clock unwedge, repeated start);
- the PM8008 register recipe (VSET low/high, mode 0x45 = 7, enable 0x46 bit 7, status 0x08 ready
  bit 7 / over-current bit 5, chip enable 0x0950 bit 0);
- the shape of the MCLK/GDSC sequence and the "every write read back" discipline;
- the SPMI arbiter channel/owner lookup and the PMIC GPIO five-register recipe.

Must be replaced for this phone:

- **every sensor entry**: the common file's four are the POCO X5 Pro's, with different buses,
  addresses, IDs, pins and rails; there is no PCLM10 module table in `fw-camera/`;
- **PM8008 bus and instances**: gpio51/52 and addresses 0x04/0x05 + 0x08/0x09 here, gpio4/5 and
  0x08/0x09 there; the enable GPIOs are PM8150L GPIO4/1, not PM8350C GPIO3;
- **CAMCC**: MCLK roots 0x5004/0x5024/0x5044/0x5064 and branches 0x501c/0x503c/0x505c/0x507c,
  not 0xe000-series; titan GDSC 0xad0c1bc, not 0xc194; GCC camera AHB/XO 0xb008/0xb044, not
  0x26004/0x26028;
- **CSID/IFE**: sm8150's `175` blocks at 0xacb3000/0xacaf000 (full) and
  0xacc8000/0xacc4000 (lite). The lite pair's addresses happen to be the same as the common
  driver's, but its headers are `cam_ife_csid_lite17x.h`/`cam_vfe_lite16x.h` (the `165` layout),
  not sm8150's `175`; the register fields do not carry over. CSIPHY v1.1 is at
  `0xac65000 + 0x1000*n`, not the common driver's v1.2 at `0xace0000 + 0x2000*n`; its tables do
  not carry over either;
- **the sensor model/tables**: the common driver loads them from `fw-camera/*.tab`, which does not
  exist for this phone; this driver reads the ID registers directly instead.

The flash half of the file is untouched: `flash=` still behaves exactly as before.

## Open questions

- The exact mono part (sensor 4). Its ID read at 0x0000 will settle it; OmniVision's OV02A10 uses
  register 0x02 and ID 0x2509, which is a useful comparison but not proof.
- The main sensor's 7-bit address (0x10 vs 0x1a). The scan answers it; no code change needed.
- The front/mono 24 MHz path (CAMCC PLL2). Deliberately not written in this first version.
- The PM8008s' own VIN (`vdd_l1_l2-supply`, an unresolved phandle in the overlay) is not controlled
  here. If an enable GPIO is high but the chip does not answer at 0x04/0x08, that supply and the
  SE4 pin mux are the two things to read.
- Whether the PM8008 #2 LDO7 2.9 V is the module's VANA (the tree says 2.9 V, the sensor's public
  spec says 2.8 V analog); the tree wins here, but the LDO status is printed so a mismatch is
  visible.
- Whether the PM8150 GPIO1 reset is active-low or active-high at the module. The driver drives low
  then high, following Qualcomm's `cam_sensor` power sequence; if the ID does not read, flipping
  the two levels is the first experiment.
- The titan top GDSC and CAMCC may also want CX/MX RPMh votes at a camera level. The first driver
  only writes the GDSC's collapse bit; `cam=1` reads its status, and if it will not come on, the
  next step is the common driver's RPMh rail code, not a different register.
- The actuators' VAF rails are **not** switched by this first version: main `actuator@0` uses the
  fixed `af_ldo` 3.25 V (`gpio-regulator@7`, `dtbo:5261-5274`, TLMM gpio11), ultra-wide
  `actuator@1` PM8008 #2 L6 2.8 V, tele `actuator@3` PM8008 #1 L3 2.8 V. The ID read does not need
  them; AF bring-up will. The PM8150L BOB (`cam_bob-supply` of sensors 1 and 3) is also not voted
  for; it is an actuator/OIS boost, not a sensor core rail.
