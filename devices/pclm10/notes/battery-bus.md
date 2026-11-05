# The gauge bus — why nothing ACKs (PCLM10)

Read-only study of the phone's own device tree and the vendor sources. Every number is cited
`file:line`, relative to `devices/pclm10/` (vendor material under `vendor-src/`, port code under
`drivers/`). No change was made to the phone, the drivers, or `vendor-src/INDEX.md`.

## 0. Which tree the phone actually runs

Three decompiled trees exist under `vendor-src/dtb/`. Only two of them describe this phone:

| File | What it is |
|---|---|
| `dtb/boot.bin.01.dts` | the base DT from the phone's boot image — the tree the kernel is started with |
| `dtb/dtbo.bin.01.dts` | the overlay from the phone's dtbo partition — adds touch, gauge, charger |
| `dtb/android-merged.dts` | a **different variant** (from the TWRP prebuilt `Image-dtb`); its bus assignments are stale here |

The base and the overlay are provably a pair: the base header carries `oppo,dtsi_no = <0x4a89>`
(`boot.bin.01.dts:13`) and the overlay header carries the same `oppo,dtsi_no = <0x4a89>`
(`dtbo.bin.01.dts:7`). The overlay's fragment targets are label-based, not number-based:
`__fixups__` resolves `qupv3_se15_i2c → /fragment@65:target:0` (`dtbo.bin.01.dts:8207`) and
`qupv3_se1_i2c → /fragment@64:target:0` (`dtbo.bin.01.dts:8206`) against the base's `__symbols__`
(`boot.bin.01.dts:25398` → `/soc/i2c@0xc94000`, `:25364` → `/soc/i2c@884000`). The overlay is
really applied: the touch node exists **only** in the overlay (fragment@63, target `qupv3_se17_i2c`),
and the port's scan found the S3706 answering at 0x20 on that bus's pins (gpio92/93).

`android-merged.dts` is a different board: it has no SMB5/da9313 nodes at all, puts the MP2650 on
`i2c@a84000` (`android-merged.dts:20214`), and gives the gauge `compatible = "oppo,bq27541-battery"`
(`:20171`), which does **not** match the driver's `"oplus,bq27541-battery"`
(`vendor-src/power/oplus_bq27541.c:3923-3924`) — in that tree the gauge driver never even probes.
So its claim that the gauge sits on `i2c@a80000` (SE8, lines 20151-20212) does not apply to this phone.

## 1. The gauge, its bus, and the power path

**Runtime placement (base + overlay):**

- Gauge node: `dtbo.bin.01.dts:6957-6964` — `bq27541-battery@55`, `compatible = "oplus,bq27541-battery"`,
  `reg = <0x55>`, `qcom,batt_bq28z610`. **No `*-supply`, no pinctrl, no enable-gpio** — the node is
  pure I2C.
- Parent: SE15, `i2c@0xc94000` (fixup `dtbo.bin.01.dts:8207` → `boot.bin.01.dts:25398`). The
  controller node `boot.bin.01.dts:21336-21355` is `status = "ok"` at `qcom,clk-freq-out = <0x186a0>`
  (100 kHz, :21352); the overlay only adds `status = "okay"` and children (`dtbo.bin.01.dts:6948-6949`).
- Other devices on the same bus: `da9313-divider@68` (`dtbo.bin.01.dts:6951-6955`),
  `oplus_short-ic@58` (`:6966`), `stm8s_fastcg@26` — the VOOC MCU (`:6973`).
- Pins: `qupv3_se15_i2c_active` = **gpio27/gpio28, function `qup15`**, drive 2 mA, `bias-disable`
  (`boot.bin.01.dts:8220-8236`). These pins live in the **EAST TLMM tile**, `EAST 0x00500000`
  (`vendor-src/pinctrl/pinctrl-sm8150.c:28-37`, `PINGROUP(27/28, EAST, qup15, …)` `:1627-1629`).
- Idle states: `qupv3_se15_i2c_sleep` parks the pins as plain GPIO with **`bias-pull-up`**
  (`boot.bin.01.dts:8238-8250`); a dedicated `pullup` state (`:8204-8217`) and `pulldown` state
  (`:8188-8201`) exist for bus recovery.

**Power path — resolved as far as the sources allow:**

1. **Gauge power: the pack itself.** The BQ27541 is the pack-side gauge of a 2-series pack
   (`qcom,vbatt_num = <0x02>`, `dtbo.bin.01.dts:845`); the SMB5 is told the gauge is external
   (`qcom,external_gauge`, `:879`). That is why the node needs no `*-supply`: the gauge is powered
   from the pack as long as the pack is connected and not in ship mode. Ship mode is held off /
   entered through the SMB5's `qcom,ship-gpio = <&tlmm 21 0x0>` (`:888`, cell 0 resolved to the
   SoC TLMM by the `tlmm` fixup list, `:8095`).
2. **Bus pull-ups: external parts on an undocumented rail; the vendor itself falls back to the
   TLMM internal pull-up.** The active state is `bias-disable` (`boot.bin.01.dts:8232`), so the
   board has external pull-ups, but **no DT node anywhere names their rail** — the whole supply
   survey of the power area yields only `dpdm/vbus/vconn-supply → usb2_phy0`
   (`dtbo.bin.01.dts:909, 979-980`) and `vdd-pdphy-supply → pm8150_l2` (`:978`, fixup `:8107`;
   that regulator is the RPMh VRM `ldoa2`, `boot.bin.01.dts:13105-13114`). Nothing for the gauge,
   the SE, or pull-ups. The vendor's sleep/pullup pinctrl states (`:8238-8250`, `:8204-8217`) hold
   the bus with the **internal** pull-up, i.e. the vendor treats it as sufficient when idle — and
   the port's `pin_release()` does the same (`drivers/power/hakios.bq27541.c:135-143`), a setup
   proven to work on this board by the touch bus (S3706 ACKs at 0x20 on gpio92/93).
3. **The SE itself: no supply to switch.** The SE15 node and its wrapper (`qcom,qupv3_2_geni_se@cc0000`,
   `boot.bin.01.dts:21322-21330`, referenced at `:21351`) carry no regulators at all — QUP logic
   runs off the always-on SoC rails, which is why the port's bit-bang on other SEs works with no vote.

**Vendor enable/wake step for the gauge: none.** The driver's probe is pure I2C — no regulator, no
GPIO, no wake command (`vendor-src/power/oplus_bq27541.c:3804-3915`); its only tolerance is 4 retries
at 5 ms spacing in the I2C helpers (`:113-140`). The only "sleep mode" command sequence in the tree
(`0x3e`/`0x60` direct commands, `:2363-2394`) belongs to the ZY0603 gauge, not this pack. Grepping
`oplus_battery_msm8150*.c`, `qpnp-smb5.c`, `smb-lib.c` finds no gauge-bus supply and no gauge enable
line — the SMB_EN/ship-mode machinery (`oplus_battery_msm8150.c:475-532`, `:4377-4387`) drives the
**charger**, not the gauge bus. So INDEX.md's "look for what turns that bus's supply on" has no
target: the DT says there is nothing to turn on.

## 2. bq27541 status in the two trees

| Tree | Bus | Status | Compatible | Matches driver? |
|---|---|---|---|---|
| runtime (`boot.bin` + `dtbo.bin.01`) | SE15 `i2c@0xc94000`, gpio27/28 | `okay` (`dtbo.bin.01.dts:6948-6958`) | `oplus,bq27541-battery` (`:6959`) | **yes** (`oplus_bq27541.c:3923-3924`) |
| `android-merged.dts` (other variant) | SE8 `i2c@a80000`, gpio88/89 | inherited okay (`:20151-20212`) | `oppo,bq27541-battery` (`:20171`) | no |

SE8 in the **runtime** tree is enabled (`boot.bin.01.dts:21094-21111`, pins gpio88/89 function
`qup8` at `:7653-7669`) but **no dtbo fragment targets `qupv3_se8_i2c`** (zero matches in
`dtbo.bin.01.dts`) — it is an empty, unconnected bus. That fully explains the port's silence on
gpio88/89: the pins behave, the protocol works (same code talks to the touch controller on
gpio92/93), but there is no slave there on this phone.

## 3. The MP2650 charger

- Bus: SE1, `i2c@884000` (fragment@64, fixup `dtbo.bin.01.dts:8206` → `boot.bin.01.dts:25364`).
  Base node `boot.bin.01.dts:20744-20760` is `status = "disabled"`; the overlay sets it `okay` and
  raises the bus to 400 kHz (`qcom,clk-freq-out = <0x61a80>`, `dtbo.bin.01.dts:6929-6930`).
- Pins: **gpio114/gpio115, function `qup1`**, NORTH tile, `bias-disable`
  (`boot.bin.01.dts:7080-7093`).
- Address: `reg = <0x5c 0x00>` (`dtbo.bin.01.dts:6935`).
- Enable/OTG line: `qcom,mps_otg_en-gpio = <0x08 0x04 0x00>` (`:6936`). Phandle `0x08` is the
  **PM8150's SPMI GPIO controller** (`compatible = "qcom,spmi-gpio"`, `pinctrl@c000` under
  `qcom,pm8150@0`, `:88-96`) — i.e. **PMIC GPIO_4, not a TLMM pin**. Its states
  `mps_otg_en_active/sleep/default` (`:197-225`) use PMIC-GPIO mode (`function = "normal"`),
  output-high / output-low, 6 mA, `power-source = <0x01>`.
- Supply: none in the DT (none needed in DT terms): the PMIC GPIO block is powered from the PMIC's
  own always-on domain while the PMIC lives, and the MP2650 itself is powered from its VBUS/BAT
  hardware pins. The port's `control_scan()` already covers SE1 (gpio114/115)
  (`drivers/power/hakios.bq27541.c:395-410`) — an ACK at 0x5c there would confirm this map.
- Adjacent charger lines, for completeness: SMB5 `qcom,dischg-gpio = <&pm8150_gpio 9 0>`
  (`dtbo.bin.01.dts:889`); the VOOC 2-wire UART to the STM8S is TLMM **gpio85/86** in GPIO mode
  (`chg_qupv3_se12_2uart_default`, `boot.bin.01.dts:12608-12616`, wired to the SMB5 at
  `dtbo.bin.01.dts:899-900`).

## 4. Why "lines idle high, nobody ACKs" — ranked hypotheses

As of R10 (logs/092331-run.log) every cheap on-phone check in this table has been run. #1, #2, #3
and #5 are closed as recorded in their rows; #4 is the only hypothesis left standing, and its check
(rise time on the released lines, and the pulldown+release recovery sequence) is the next thing this
driver can do from software. Until then the charge shown comes from SMB5's own gauge, which reads
the pack correctly (4.28 V, 97%).

| # | Hypothesis | Evidence | Cheapest on-phone check |
|---|---|---|---|
| 1 | **Scanning the wrong pins.** The port bit-bangs gpio88/89 (SE8) — an empty bus in the runtime tree; the gauge is on **gpio27/28 (SE15, EAST tile)**. | **closed, R10 (logs/092331-run.log).** The check was run: `gauge_start()` probes both pairs, the runtime tree's first, in both orders, and `bus_scan()` is tile-parameterized. Still "nothing at 0x55 on gpio27/gpio28", and the full 0x08–0x77 scan of that bus finds zero devices. The right pins were scanned. | done — no further on-phone check |
| 2 | **Gauge asleep.** The BQ27541-G8 enters sleep after ~8 s of bus silence and can NACK the first access after wake. | **closed, R10 (logs/092331-run.log).** The check was run and it is already in the driver: `gauge_wake()` (three start/stop bursts, then 100 ms) runs before every probe of both pairs, in both orders. Still nothing at 0x55, and a full 0x08–0x77 scan of both buses finds zero devices. A sleeping gauge answers its clock bursts; a gauge with no supply answers nothing. | done — no further on-phone check; what remains is #3/#4 |
| 3 | **Bus unpowered because the pack is off/ship-mode.** Nothing in the DT powers this bus; it lives only as long as the pack does. | **closed for ship-mode, R10 (logs/092331-run.log).** The check was run: SMB5 reads VBAT = 4.2805 V with the pack present and even taking charge (+67 mA). The pack is on and nowhere near ship mode. A gauge rail derived from VBAT would be live, so if the gauge is silent its supply is a different one — which is #4's territory. | done — no further on-phone check |
| 4 | **Pull-ups weak/absent on the new pins.** The "idle high" seen today is partly the port's own internal pull-up (`hakios.bq27541.c:135-143`), so it proves nothing about the board. | touch bus works with the same internal-pull-up setup, so the method is sound | only relevant if #1 still NACKs everything: then check rise time / try the `pulldown`+release recovery sequence from `boot.bin.01.dts:8188-8217` |
| 5 | QUP function-selector confusion (`qup15` = selector 1 vs `qup8` = 2). | `pinctrl-sm8150.c:1627-1629` vs `:1716-1718` | irrelevant — the port uses GPIO mode, not the serial engine |

The port's header comment (`drivers/power/hakios.bq27541.c:8`, "qup8, north TLMM tile") is stale
against the runtime tree and should not be trusted.

## 5. Corrections to `vendor-src/INDEX.md` (not applied there)

- Lines 73–74: the dtbo's `qupv3_se15_i2c` claim for the gauge is **correct** for this phone; it is
  `android-merged.dts` that is the wrong variant (different PMIC set, non-matching compatible).
- Lines 84–85: there is no "bus supply" in the vendor charging stack to turn on — the DT carries no
  gauge-bus supply at all, and no vendor driver has a gauge enable/wake step. The bus is pack-powered
  by hardware; the only software-visible lever is the SMB5 ship-mode GPIO (TLMM 21).

## Sources

| File | Lines used |
|---|---|
| `vendor-src/dtb/dtbo.bin.01.dts` | 7, 88-96, 197-225, 845, 879, 887-889, 909, 978-980, 6925-6943, 6945-7010, 8095-8107, 8122, 8206-8207, 9256-9266 |
| `vendor-src/dtb/boot.bin.01.dts` | 13, 7080-7095, 7653-7681, 8188-8251, 12608-12616, 13105-13114, 20744-20760, 21094-21111, 21322-21355, 25364, 25398 |
| `vendor-src/dtb/android-merged.dts` | 20151-20214, 20457-20473 |
| `vendor-src/power/oplus_bq27541.c` | 113-140, 2363-2394, 3804-3924 |
| `vendor-src/power/oplus_battery_msm8150.c` | 475-532, 4377-4387 |
| `vendor-src/pinctrl/pinctrl-sm8150.c` | 28-37, 1627-1629, 1716-1718 |
| `drivers/power/hakios.bq27541.c` | 8, 88-110, 135-143, 359-410 |
