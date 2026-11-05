# Bluetooth: the WCN3990's HCI over QUPv3 engine 3 (hsuart0)

Status: **first driver written, compiled only, never run.** The phone was not available in this
round and the round was not allowed to build. The driver is
`devices/pclm10/drivers/bluetooth/hakios.wcnbt.c`, its symbol is `CONFIG_HAKIOS_WCNBT`; the boot
argument is `wcnbt=1` (version request) or `wcnbt=2` (version request and HCI Reset). It needs one
line added to `devices/pclm10/configs/pclm10.config` (see §8), which this round was not allowed to
edit.

This note records the hardware the driver is built on, where every number came from, and the
points that are still uncertain. It deliberately does not repeat the WLAN/SLPI notes: the WLAN
half of the same chip runs on the SLPI and speaks GLINK/QMI (`notes/wifi.md`); the Bluetooth half
has its own ROM, firmware and a plain H4 serial link to the application processor. No SLPI and no
QMI are on the path below.

## 1. What the phone's own trees say

| What | Where | What it says |
|---|---|---|
| the Bluetooth node | `vendor-src/dtb/dtbo.bin.01.dts:6390-6402` (fragment@47) | `bt_wcn3990`, `compatible = "qca,wcn3990"`, three supplies and their voltage/current levels |
| the supplies' labels | `dtbo.bin.01.dts:8163-8165` | `qca,bt-vdd-core` = `pm8150_l7`, `qca,bt-vdd-pa` = `pm8150l_l2`, `qca,bt-vdd-ldo` = `pm8150l_l11` |
| the host port | `vendor-src/dtb/android-merged.dts:20134-20149` | `qcom,qup_uart@0xc8c000`, `compatible = "qcom,msm-geni-serial-hs"`, `reg = <0xc8c000 0x4000>`, clocks `<&gcc 0x73 0x7d 0x7e>`, `qcom,wrapper-core = <&qupv3_2_geni_se>`, `qcom,wakeup-byte = <0xfd>`, `status = "disabled"` |
| its alias | `vendor-src/dtb/boot.bin.01.dts:23689` (and `android-merged.dts:22695`) | `hsuart0 = "/soc/qcom,qup_uart@0xc8c000"` |
| its pins | `android-merged.dts:6573-6650` | gpio43 CTS, gpio44 RTS, gpio45 TX, gpio46 RX, all function `qup13`, drive-strength 2 mA; CTS/RX `bias-disable`, RTS `bias-pull-down`, TX `bias-pull-up` |
| the wrapper | `android-merged.dts:20424-20436` | `qcom,qupv3_2_geni_se@cc0000`, `reg = <0xcc0000 0x6000>` |
| the reference board | OPPO kernel `arch/arm64/boot/dts/19031/sm8150-mtp.dtsi:34-46` and `:67-69` | the same `bt_wcn3990` node under `&vendor`, and `&qupv3_se13_4uart { status = "ok"; }`. This is the shape the phone's overlay was built from. The same node appears in `sm8150-qrd.dtsi:30-43` and `:231-233`. |

The phone's base tree has the UART node `status = "disabled"`; the dtbo's fragment@48 turns
`qupv3_se13_4uart` on (`dtbo.bin.01.dts:6445-6451` in the decompiled overlay; fragment targets are
unresolved `0xffffffff` in that dump but the alias table names fragment@48 as
`qupv3_se13_4uart`). Treat the port as enabled on Android and absent on a HakiOS boot: the
driver configures it from scratch.

`bt_wcn3990` has no `qca,bt-reset-gpio`, no `qca,bt-3P3-en-gpio`, no `qca,bt-1P3-en-gpio`, no
`clocks`, and no `pinctrl-*` of its own. The reference `sm8150-mtp.dtsi` node has exactly the same
three supplies and nothing else.

## 2. Transport: one QUPv3 GENI UART, H4, not BCS

This phone does **not** use the older `msm_serial_hs`/BCS path. `drivers/tty/serial/msm_serial_hs.c`
is the wrong driver for this tree: the node is `qcom,msm-geni-serial-hs`, the wrapper is
`qcom,qupv3-geni-se`, and the vendor's implementation is
`vendor-src/bluetooth/msm_geni_serial.c` (the file fetched from the OPPO tree). The engine is
QUPv3 wrapper 2, local engine 3, global name `qupv3_se13_4uart`.

The HCI transport is H4 with Qualcomm's extensions, protocol `HCI_UART_QCA`:

- `vendor-src/bluetooth/hci_qca.c:1012-1013`: `.init_speed = 115200`, `.oper_speed = 3000000`.
  The same two values are in mainline `drivers/bluetooth/hci_qca.c` (`qca_proto`).
- H4 packet type bytes: `0x01` command, `0x02` ACL, `0x03` SCO, `0x04` event; the vendor's
  `qca_recv_pkts` (`hci_qca.c:815-822`) adds `0xFC`/`0xFD`/`0xFE` for HCI-IBS sleep/wake.
- The firmware download uses the vendor command opcode `0xFC00` and the TLV request `0x1E`;
  the version request is `0xFC00` with parameter `0x19` (`btqca.h` in the same directory).
- **No BCS/BCSP.** BCS/BCSP is the CSR BlueCore serial protocol; nothing in this phone's tree or
  in the QCA driver uses it. What the task calls "the qca protocol version" is the firmware's
  TLV patch/NVM pair described in §6, and the H4/QCA vendor command set above.

The port is 4-wire: gpio43 CTS, gpio44 RTS, gpio45 TX, gpio46 RX. The tree's `qcom,wakeup-byte =
<0xfd>` is the HCI-IBS wake byte, not a GPIO. The first driver sends without hardware flow
control (it sets `UART_CTS_MASK` and does not wait for CTS); the pins are still muxed as the
4-wire group, and the engine's own RFR is left open. 3 Mbaud and real RTS/CTS are the later step,
after the firmware path works.

The vendor's `msm_geni_serial.c` uses DMA for its high-speed path but a FIFO-only path for the
console (`msm_geni_serial_port_setup()` at :2267-2345: console sets `FIFO_MODE`, non-console sets
`SE_DMA`; the "For now only assume FIFO mode" comment is at :2283-2285 despite the DMA assignment
that follows). The HakiOS driver copies the console/FIFO shape: polled FIFO, no DMA, no GIC.

## 3. Power: three RPMh VRM rails

The `bt_wcn3990` node asks for three supplies. Their phandles resolve through the overlay's label
table, and the base tree names each PMIC LDO and its RPMh resource:

| Property | PMIC rail | Voltage | RPMh resource | Tree line for the resource name |
|---|---|---|---|---|
| `qca,bt-vdd-core-supply` | `pm8150_l7` | 1 800 000 uV (0x1b7740) | `ldoa7` | `android-merged.dts:12374-12377` |
| `qca,bt-vdd-pa-supply` | `pm8150l_l2` | 1 304 000 uV (0x13e5c0) | `ldoc2` | `:12787-12790` |
| `qca,bt-vdd-ldo-supply` | `pm8150l_l11` | 3 312 000 uV (0x328980) | `ldoc11` | `:12956-12959` |

All three `current-level` properties are 0 (LPM/PFM). An RPMh VRM vote has no current/load field
(the vendor's `rpmh-regulator.c` writes voltage at the cell, enable at +4 and mode at +8), so
those three properties change nothing in this path. The same three resources are the WLAN's
`ldoa7`/`ldoc2`/`ldoc11` (`notes/wifi.md`); if the SLPI driver has already voted them, repeating
the vote is harmless.

The command-database addresses are the ones already measured on this phone
(`notes/wifi.md` and `notes/battery-bus.md`): `ldoa7` 0x41f00, `ldoc2` 0x41d00, `ldoc11`
0x41a00, in the 4424-byte table at 0x85f20000 found through the pointer at 0xc3f000c. The driver
does not hard-code those addresses: it walks the table by name, as the touch and SLPI drivers do.

The three rails are not the whole WCN3990 supply set. Mainline's `qca_soc_data_wcn3990`
(`drivers/bluetooth/hci_qca.c`) lists four supplies (`vddio`, `vddxo`, `vddrf`, `vddch0`). This
phone's tree describes three, so either `vddio` is an always-on rail on this board or the vendor
driver's naming folds it into `vdd-core`. There is no fourth property to vote. If the first run
shows the chip unresponsive with all three rails up, the missing `vddio` is one hypothesis; the
other is the enable pin in §4.

## 4. Enable/reset: there is no reset GPIO; the WCN3990 uses UART power pulses

This is the least obvious part and the one most likely to be wrong on a first run, so both the
evidence and the uncertainty are written down.

**What the tree says.** `bt_wcn3990` has no `qca,bt-reset-gpio`. Neither does the reference
`sm8150-mtp.dtsi` node or the QRD node. Three pinctrl groups exist in the base tree but no node
consumes them:

- `bt_en_active`, gpio172, function gpio, 2 mA, `bias-pull-down`
  (`android-merged.dts:11496-11509`);
- `conn_power_1p8_active`, gpio173, function gpio, 2 mA, `bias-pull-up` (`:11812-11825`);
- `conn_power_pa_active`, gpio174, function gpio, 2 mA, `bias-pull-up` (`:11827-11840`).

The three are in the NORTH tile (`pinctrl-sm8150.c:1856-1858`; NORTH = 0x03900000). No
`gpio172`/`173`/`174` consumer appears anywhere in the merged tree or in the reference board
files. They are therefore **candidates, not facts**: the driver reads their configuration and
level once (`power_pins_look()`) and writes nothing. If a later run needs an enable pin, driving
gpio172 high is the first experiment, not a fix to ship blind.

**What the WCN3990 does instead.** The chip's BT_EN is driven from the UART TX line through an
external decode circuit; the host toggles it with single-byte "power pulses" at fixed baud rates.
Mainline `drivers/bluetooth/btqca.h` names them:

```
#define QCA_WCN3990_POWERON_PULSE   0xFC
#define QCA_WCN3990_POWEROFF_PULSE  0xC0
```

and `hci_qca.c`'s `qca_regulator_init()` (mainline, around :1770-1840) does, for WCN3990:

1. set the host port to 2400 baud, send `0xC0` (off);
2. set the host port to the init speed 115200, send `0xFC` (on);
3. reopen the port at 115200 and go on to the version request / firmware download.

Mainline's comment is explicit that the pulse is decoded by an external circuit at the TX pin and
that hardware flow control must be off while sending it. The vendor's older `hci_qca.c` in
`vendor-src/bluetooth/` does not contain this sequence (it is the ROME path, see §6), but the
mainline WCN3990 path is the one that matches this tree's `qca,wcn3990` compatible.

The first driver sends exactly `0xC0` at 2400 and `0xFC` at 115200. The 2400-baud divider is
7.3728 MHz / (2400 × 32) = 96; the 115200 divider is 2 (§5). If the phone's bootloader or a
previous Android boot has already left the chip enabled, the off/on pair is still the documented
way to put it in a known state.

## 5. Clocks, pins and the serial engine

### 5.1 GCC (QUPv3 wrapper 2, engine 3)

From `vendor-src/clk/gcc-sm8150.c` (fetched from the same OPPO branch):

| Register | Offset | Meaning |
|---|---|---|
| wrapper reset | 0x1e000 | bit 0 holds the wrapper in reset |
| M AHB branch | halt 0x1e004, vote bit 2 in 0x52014 | wrapper master AHB clock |
| S AHB branch | halt 0x1e008, vote bit 1 in 0x52014 | wrapper slave AHB clock; `.hwcg_reg = 0x1e008`, `.hwcg_bit = 1` |
| engine 3 branch | halt 0x1e4d4, vote bit 7 in 0x52014 | `GCC_QUPV3_WRAP2_S3_CLK` = 115 |
| engine 3 RCG | cmd 0x1e4d8, CFG 0x1e4dc, M 0x1e4e0, N 0x1e4e4, D 0x1e4e8 | `gcc_qupv3_wrap2_s3_clk_src` = 116 |

The RCG's table (`ftbl_gcc_qupv3_wrap0_s0_clk_src`, reused by every QUP engine) gives
7.3728 MHz from `P_GPLL0_OUT_EVEN` (300 MHz) with M = 384, N = 15625, pre-divider 1
(`F(7372800, P_GPLL0_OUT_EVEN, 1, 384, 15625)`). `clk_rcg2_configure()` writes
M = 384, N = ~(15625 − 384), D = ~15625, then CFG = pre-div | source 6 << 8 | dual-edge, then
the update bit. The driver does the same and, if the root's bit 31 is set, asks for `CMD_ROOT_EN`
bit 1 — the touch driver's `clocks_on()` (`hakios.s3706.c:232-312`) is the precedent on this
phone. The wrapper's own gates at 0x8 take 0x303 (bits 0, 1, 8, 9), again the touch driver's
value.

### 5.2 TLMM (EAST tile)

The four pins are in the EAST tile (`PINGROUP(43, EAST, qup13, ...)` at
`pinctrl-sm8150.c:1654-1657`). The tree's `pinctrl@03000000` base is not a flat window: the real
tiles are at 0x03100000 west, 0x03500000 east, 0x03900000 north and 0x03d00000 south
(`pinctrl-sm8150.c:28-33`; the touch note explains the failed flat-window read). The driver maps
**0x03500000**, 0x80000 bytes, and writes `tile + 0x1000 * pin`.

| Pin | Signal | Function | Pull | Drive |
|---|---|---|---|---|
| 43 | CTS | qup13 (index 1) | none (0) | 2 mA (code 0) |
| 44 | RTS | qup13 | down (1) | 2 mA |
| 45 | TX | qup13 | up (3) | 2 mA |
| 46 | RX | qup13 | none (0) | 2 mA |

Drive-strength 2 in the tree becomes register code `(2 / 2) − 1 = 0`
(`pinctrl-msm.c:msm_config_group_set`, `PIN_CONFIG_DRIVE_STRENGTH`). Pull values are
`MSM_NO_PULL = 0`, `MSM_PULL_DOWN = 1`, `MSM_PULL_UP = 3` (`pinctrl-msm.c`). The OE bit (bit 9)
is left alone; the engine's `GENI_OUTPUT_CTRL = 0x7f` hands the pads to the engine.

### 5.3 The serial engine (0xc8c000)

The register numbers are the vendor's `msm_geni_serial.c:37-47` and the common
`include/linux/qcom-geni-se.h` (fetched from the same tree):

| Register | Offset | Use here |
|---|---|---|
| `SE_GENI_STATUS` | 0x040 | bit 0 M command active, bit 12 S command active |
| `GENI_SER_M_CLK_CFG` / `S` | 0x048 / 0x04c | bit 0 clock enable, [15:4] divider |
| `SE_FW_REVISION` | 0x068 | [15:8] protocol; UART = 2 |
| `SE_FORCE_DEFAULT` | 0x020 | write 1 to reset the engine, then 0 |
| `SE_OUTPUT_CTRL` | 0x024 | 0x7f hands the pads to the engine |
| `SE_CGC_CTRL` | 0x028 | 0x7f opens the engine's seven clock gates |
| `SE_BYTE_GRAN` | 0x254 | 0: the 4x8 FIFO-entry granularity used by both packings |
| `SE_FIFO_IF_DISABLE` | 0x2008 | writable FIFO-interface disable; clear bit 0 for FIFO mode (mainline's newer `qcom-geni-se.c`; the vendor's older file does not name it) |
| `GENI_IF_DISABLE_RO` | 0x064 | read-only status; bit 0 = FIFO interface disabled |
| `SE_DMA_MODE_EN` | 0x258 | bit 0 = 0: FIFO mode |
| `SE_TX/RX_PACKING_CFG0/1` | 0x260/0x264, 0x284/0x288 | TX 8-bit into one FIFO word (0x0000000f/0), RX 8-bit into four (0x0004380e/0x000c3e0e), the vendor console path's shape |
| `SE_M_CMD0` | 0x600 | opcode in [31:27]; TX = 1 |
| `SE_S_CMD0` | 0x630 | opcode in [31:27]; read = 1, parameter bit 7 = RFR open |
| `SE_M_IRQ_STATUS/EN/CLEAR` | 0x610/0x614/0x618 | polled: bit 0 done, bit 30 TX watermark |
| `SE_TX/RX_FIFO` | 0x700/0x780 | one 32-bit word per access |
| `SE_TX/RX_FIFO_STATUS` | 0x800/0x804 | TX words, RX words [24:0] and last-valid [30:28] |
| `SE_IRQ_EN` | 0xe1c | four interrupt sources routed to the wrapper (no GIC handler) |
| `SE_DMA_GENERAL_CFG` | 0xe30 | low four clock gates |
| `SE_UART_TX_TRANS_CFG` | 0x25c | bit 1 `UART_CTS_MASK` (ignore CTS) |
| `SE_UART_TX_WORD_LEN` / `RX` | 0x268 / 0x28c | 8 |
| `SE_UART_TX_STOP_BIT_LEN` | 0x26c | 0 = 1 stop bit |
| `SE_UART_TX_TRANS_LEN` | 0x270 | bytes to send |
| `SE_UART_RX_STALE_CNT` | 0x294 | 160 (10 bits/char × 16) |
| `SE_UART_TX/RX_PARITY_CFG` | 0x2a4/0x2a8 | 0 = no parity |
| `SE_UART_MANUAL_RFR` | 0x2ac | 0 = hardware RFR |

The UART baud divider is `ser_clk / (baud × 32)` (`msm_geni_serial.c:get_clk_div_rate()` and
`set_termios()`), which is why the engine clock must be 7.3728 MHz for 115200 and 2400. At
7.3728 MHz: 2400 → 96, 115200 → 2, 3 000 000 would need the 96 MHz RCG entry instead (later).

Polled transmit, in the vendor console's order (`msm_geni_serial.c:935-970` and
`__msm_geni_serial_console_write`): clear status, set the TX watermark to 2, write the length,
start the M command, wait for the watermark bit, write one byte per FIFO word (TX packing 8x1,
which is why no stale byte survives a one-byte command), clear the watermark, wait for the done
bit, clear the status. Polled receive: start the S command
with RFR open, then drain `SE_RX_FIFO` whenever `SE_RX_FIFO_STATUS` reports words, expanding the
last word by its valid-byte count. No interrupt is used.

## 6. Firmware: the TLV pair, and what is missing

WCN3990's Bluetooth firmware is a RAM patch plus an NVM file in Qualcomm's TLV format. Mainline
`drivers/bluetooth/btqca.c` derives the file names from the ROM version:

```
soc_ver = (soc_id << 16) | rom_ver
rom_ver = ((soc_ver & 0x00000f00) >> 4) | (soc_ver & 0x0000000f);
patch: qca/crbtfw%02x.tlv
NVM:   qca/crnv%02x.bin
```

The four bits that name the file are in the **ROM version field**, not in the SOC id; the driver
therefore applies the formula to the payload's `rom_ver` (`rom` in the log), not to `soc_id`.

with `case QCA_WCN3990:` and a size limit of 200 KiB for the patch and 10 KiB for the NVM
(`MAX_PATCH_FILE_SIZE`, `MAX_NVM_FILE_SIZE`). On SM8150 the controller sometimes reports ROM
version 0x01; the same file is distributed as `crbtfw01.tlv` → `crbtfw21.tlv` and
`crnv01.bin` → `crnv21.bin` (a 2026 linux-firmware symlink request documents exactly that for
SM8150). The expected pair for this phone is therefore **`crbtfw21.tlv` + `crnv21.bin`**, with
`crbtfw01.tlv`/`crnv01.bin` as the alternate names, whichever ROM version the chip in §7 reports.

**They are not in this repository and not in `S:\opencode\fw-wifi\`.** That directory holds the
WLAN's SLPI image (`slpi.mdt`, `slpi.b00`–`b20`, `bdwlan.bin`, `wlanmdsp.mbn`), not the Bluetooth
firmware. They live in the phone's `/vendor/firmware` (or the vendor image inside super); pulling
them is a separate step, the same way `notes/wifi.md` §8 pulled the SLPI image from the EDL
backup's modem FAT volume. The first milestone does not need them: the ROM answers the QCA
version request and HCI Reset by itself.

A caution about the fetched vendor sources: `vendor-src/bluetooth/btqca.c` on this branch is the
older ROME-only loader. Its TLV names are `image/btfw32.tlv` / `image/btnv32.bin`, and its
version enum has `ROME_SOC_ID_44`, `HST_SOC_ID_0200`, `GNA_SOC_ID_0200` only. It has no WCN3990
or Cherokee case. So the WCN3990 file names above come from mainline `btqca.c`, not from that
local file; the local file is useful for the H4/IBS state machine and the `0xFC00` command
encoding, not for the firmware names. This is consistent with Android possibly doing the
WCN3990 download through a userspace `hciattach`/`btpower` path rather than this kernel's
`btqca`; the names and TLV format are the same either way.

## 7. The first driver and what success looks like

`devices/pclm10/drivers/bluetooth/hakios.wcnbt.c`, symbol `CONFIG_HAKIOS_WCNBT`, boot arguments:

- `wcnbt=1`: vote the three rails, configure GCC/QUP/TLMM/SE, send the 0xC0/0xFC power pulses,
  send the QCA version request (`01 00 fc 01 19`), print product/SOC/ROM/patch.
- `wcnbt=2`: the same, then HCI Reset (`01 03 0c 00`) and its Command Complete.

The sequence is on a kernel thread that waits 3 s after `STARTUP(EARLY_BOOT, STARTUP_RANK_LAST)`,
so it does not slow the rest of the boot. `wcnbt=0`/absent touches nothing.

Expected log, if the hardware path is right:

```
bt: ldoa7 at 0x41f00: 1800 mV, enabled, auto: all three votes completed (pm8150 l7, the core rail)
bt: ldoc2 at 0x41d00: 1304 mV, enabled, auto: all three votes completed (pm8150l l2, the PA rail)
bt: ldoc11 at 0x41a00: 3312 mV, enabled, auto: all three votes completed (pm8150l l11, the 3.3 V rail)
bt: gcc: m-ahb, s-ahb and engine 3's branch are on; the engine's configuration says 0x...
bt: gpio43-46 are qup13: CTS, RTS, TX, RX, the tree's pulls and 2 mA
bt: gpio172: cfg 0x... (function 0, pull 1, 2 mA, input), io 0x... (reads 0)
bt: gpio173: ...
bt: gpio174: ...
bt: the wrapper's clock gates read 0x... (asked for 0x303)
bt: the WCN3990 is powered and the UART is open at 115200 (polled GENI FIFO)
bt: THE WCN3990 ANSWERED: product 0x..., soc 0x40010201, rom 0x..., patch 0x...
bt: HCI Reset: the controller is ready (num_cmds 1, opcode 0xc03, status 0x0)
```

The exact product/SOC/ROM/patch values are what the next run is for. `soc 0x40010201` with
`rom 0x21` would name `crbtfw21.tlv`; `soc 0x40010214` or a `rom 0x01` report would name the
`01` alias. The driver prints the name it derives, not a hard-coded one.

If the version line is absent, the log's last lines narrow it down:

- `the engine's protocol is N, not UART (2)` — QUP configured the SE for another protocol or the
  clock/engine is not the one assumed; check `SE_FW_REVISION` and the wrapper/engine index.
- `clocks stay off` — GCC vote/RCG/GPLL0 problem; the line prints all three branch halt words
  and the vote word.
- `gpio.. would not take qup13` — wrong TLMM tile or pin; the EAST tile at 0x03500000 is the
  tree's own.
- no `THE WCN3990 ANSWERED` and no `no QCA version event` line — the transmit path is stuck or
  the chip is not enabled. The `gpio172/173/174` lines say whether the board left the enable
  candidates low; the power-pulse/GPIO question in §4 is the first thing to revisit.

## 8. What is blocked, and the alternatives

1. **No tty/serdev/clock framework on HakiOS.** This driver does not use them: it maps the GCC
   and GENI windows and programs the RCG and SE directly. That is the proven pattern on this
   phone (the touch driver does the same for QUP2 engine 0), but it is the first UART on this
   port and the first use of engine 3. The GENI serial engine's own firmware/RAM configuration
   is another layer the vendor's driver assumes the boot chain left in place: the driver reads
   `SE_FW_REVISION` and stops with "the engine's protocol is N, not UART (2)" if it is not 2.
   If that happens on the phone, the missing piece is the GENI firmware load (the bootloader's
   job, or mainline's `geni_se_load_fw()` path for newer GENI), not another UART register; the
   touch engine proves the boot chain does load some SEs, but not necessarily this one.
2. **The GENI FIFO may be fused off.** The I2C engine on this SoC reports its FIFO interface
   disabled (`notes/wifi.md`, `hakios.s3706.c:1093-1118`), which is why the touch bus is
   bit-banged. UART is a different protocol block and the vendor's console path is FIFO-only, so
   FIFO is the reasonable first attempt. The driver clears the writable disable bit at 0x2008
   (mainline's newer `geni_se_select_mode()` path), then reads the read-only
   `GENI_IF_DISABLE_RO` (0x64), says "FIFO disabled, DMA would be needed" and stops rather than
   timing out if bit 0 is still set. If `SE_FW_REVISION` says UART but no byte moves, the
   alternatives are (a) the engine's DMA path
   (`SE_DMA_MODE_EN`, the wrapper's `qcom,iommu-s1-bypass`, the vendor driver's non-console
   `SE_DMA` path, a much larger job), or (b) bit-banging gpio45/46 at 115200 with the TLMM
   (possible for short HCI packets but fragile; not attempted here).
3. **The power-pulse vs enable-pin question.** §4 is the largest uncertainty. The driver follows
   mainline's WCN3990 bring-up (pulses) and only observes gpio172-174. The first run's version
   line settles it. If it fails with the rails up, the next experiment is to drive gpio172 high
   before the pulses (a `wcnbt=3` level), then gpio173/174 if the schematic/board file ever
   confirms them.
4. **No firmware.** The version request and HCI Reset are ROM services. The TLV download
   (`0xFC00`/`0x1E`), NVM, the 3 Mbaud change and normal HCI operation wait for
   `crbtfw21.tlv`/`crnv21.bin` to be pulled from the phone. The driver does not read files yet;
   that is the next code step, not this one.
5. **No GIC interrupt.** The engine's status is polled and `SE_IRQ_EN` is set only because the
   vendor's init sequence sets it; no handler is installed. If the UART's SPI (the tree's
   `interrupts-extended` second cell 0x249) turns out to be enabled by the boot chain, the polled
   driver still works but an interrupt storm is possible; masking that SPI before enabling the
   engine is the first thing to add if the log shows repeated spurious interrupts.
6. **Build configuration.** `devices/pclm10/configs/pclm10.config` currently has
   `# CONFIG_HAKIOS_BT is not set` (the common WCN6750 driver's symbol, not this one). To build
   this driver, add:

   ```
   CONFIG_HAKIOS_WCNBT=y
   CONFIG_BOOTARGS="... wcnbt=1"
   ```

   This round was not allowed to edit that file, so the driver is present but not selected.
   Adding `wcnbt` also makes the generated argument catalogs stale (`tools/boot-args.py --check`
   names `docs/boot-args.md`, `configs/boot-args.lst` and `configs/symbols.tsv`); they are outside
   this round's allowed paths. Regenerate them in the same step:

   ```
   python3 tools/boot-args.py --markdown > docs/boot-args.md
   python3 tools/boot-args.py --lst      > configs/boot-args.lst
   python3 tools/boot-args.py --symbols  > configs/symbols.tsv
   ```

## 9. Next round on the phone

1. Add `CONFIG_HAKIOS_WCNBT=y` and `wcnbt=1` to the pclm10 configuration, build, boot. Read the
   log from `bt: ldoa7` through `bt: done`. The first success is the `THE WCN3990 ANSWERED` line;
   `wcnbt=2` adds the HCI Reset status.
2. If the version arrives, the hardware path (rails, clock, pins, pulses, H4) is proven. Then
   pull `crbtfw21.tlv`/`crnv21.bin` (or `crbtfw01.tlv`/`crnv01.bin`) from the phone's
   `/vendor/firmware` or vendor image into `S:\opencode\fw-wifi\` (or a new `fw-bt\`), and extend
   the driver with the TLV download: `0xFC00` + `0x1E`, 243-byte segments, the 0x00/0x04 response,
   then NVM, then the 3 Mbaud change (0xFC00 baud-rate command) and HCI Reset. This is the same
   order as mainline `btqca.c`/`hci_qca.c`; the vendor's `btqca.c` gives the exact segment layout
   (`MAX_SIZE_PER_TLV_SEGMENT = 243`) and the 200 ms/20 ms waits.
3. If the version does not arrive, in this order: (a) look at the gpio172/173/174 line; (b) try
   driving gpio172 high (new `wcnbt=3`) and repeat; (c) read `SE_FW_REVISION`, `SE_GENI_STATUS`,
   `SE_M_IRQ_STATUS`, `SE_RX_FIFO_STATUS` in the failure path (they are already in the log on
   the transmit path, but add a dump after the timeout); (d) verify the RCG's CFG reads back
   0x2601 and the branch CBCRs' bit 31 is clear; (e) only then consider the DMA path.
4. Do not try 3 Mbaud before the firmware path works. The ROM's 115200 link is the known-good
   one, and a failed baud change leaves the host and the chip at different speeds with no way to
   resynchronize except a power cycle.

## 10. Files this round

- `devices/pclm10/drivers/bluetooth/hakios.wcnbt.c` — the driver described above.
- `devices/pclm10/drivers/bluetooth/conf` — the `WCNBT` symbol and its help text.
- `devices/pclm10/notes/bluetooth.md` — this note.

Read but not modified: `drivers/bluetooth/hakios.bt.c` (the common WCN6750 pin-look; its
interface is a boot-arg-gated `STARTUP` and `ml_io_map`, which this driver follows),
`devices/pclm10/drivers/wifi/hakios.slpi.c` (the RPMh/TCS and thread patterns copied),
`devices/pclm10/drivers/touch/hakios.s3706.c` (the GCC/QUP/TLMM patterns copied), and the vendor
sources under `devices/pclm10/vendor-src/`.
