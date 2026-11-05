# WLED on PCLM10 — what the vendor driver actually does

Sources read (all paths relative to `devices/pclm10/`):

- `vendor-src/display/qcom-spmi-wled.c` — vendor WLED driver, 2447 lines, read in full. All register offsets and bit definitions are inline in this file (lines 34–185); no external header was needed. One header referenced by the driver, `<linux/qpnp/qpnp-revid.h>` (line 30, for `PM8150L_V1P0_REV4` / `PM8150L_V3P0_REV4`), is not in the local tree and was not fetched; it only affects two revision-conditional branches, noted where relevant.
- `vendor-src/spmi/spmi-pmic-arb.c` — vendor SPMI arbiter, 1389 lines, read in full.
- `vendor-src/spmi/spmi-core.c` — **fetched** from `https://raw.githubusercontent.com/oppo-source/android_kernel_oppo_sm8150/oppo/sm8150_s_12.1_oppo_reno10/drivers/spmi/spmi-core.c` (603 lines), because the local copy was missing and the regmap→arbiter path had to be proven, not assumed.
- `vendor-src/dtb/dtbo.bin.01.dts` — the only DT file that contains a `qcom,pm8150l-spmi-wled` node.
- `vendor-src/dtb/android-merged.dts` — base DT; grep for `wled|backlight|bl-pmic-control-type` returns **zero** hits.
- `drivers/display/hakios.wled.c`, `drivers/power/hakios.smb5.c`, `drivers/power/hakios.pon.c` — our drivers.

---

## 1. Where WLED actually lives

WLED is **SPMI slave 5**, not 4. PM8150L is split across two SPMI slaves in the OEM DTBO:

| node | reg | children (dtbo.bin.01.dts, verified by node scan) |
|---|---|---|
| `qcom,pm8150l@4` (1386) | `<0x04 0x00>` | `qcom,revid@100` (1392), `qcom,power-on@800` (1398), `qcom,temp-alarm@2400` (1403), `clock-controller@5b00` (1414), `pinctrl@c000` (1425), `vadc@3100` (1551), `bcl@3d00` (1612), `adc_tm@3500` (1621) |
| `qcom,pm8150l@5` (1652) | `<0x05 0x00>` | `qcom,lcdb@ec00` (1658), `qcom,leds@d300` (1695 — flash/torch LED driver, a **separate** device with `qcom,flash_0..2`, `qcom,torch_0..2` children), `qcom,wled@d800` (1826), `qcom,pwms@b100` (1860), `qcom,pwms@bc00` (1903), `qcom,leds@d000` (1912) |

The full WLED node, `dtbo.bin.01.dts:1826–1858`:

```dts
qcom,wled@d800 {
    compatible = "qcom,pm8150l-spmi-wled";
    reg = <0xd800 0x100 0xd900 0x100>;
    reg-names = "wled-ctrl-base\0wled-sink-base";
    label = "backlight";
    interrupts = <0x05 0xd8 0x01 0x01 0x05 0xd8 0x04 0x03 0x05 0xd8 0x05 0x03>;
    interrupt-names = "ovp-irq\0pre-flash-irq\0flash-irq";
    qcom,pmic-revid = <0x1b>;
    qcom,auto-calibration;
    status = "ok";
    qcom,string-cfg = <0x07>;
    qcom,leds-per-string = <0x06>;
    phandle = <0xe5>;

    qcom,wled-flash  { label = "flash";  qcom,default-led-trigger = "wled_flash"; };
    qcom,wled-torch  { label = "torch";  qcom,default-led-trigger = "wled_torch";
                       qcom,wled-torch-timer = <0x4b0>; };
    qcom,wled-switch { label = "switch"; qcom,default-led-trigger = "wled_switch"; };
};
```

Decoded against the driver's actual parser (`wled_configure`, `qcom-spmi-wled.c:2130–2323`):

- ctrl base = **0xd800**, sink base = **0xd900** — taken from `reg[0]`/`reg[1]` by index via `of_get_address`, lines 2224–2236 (the `reg-names` strings are not consulted).
- `qcom,string-cfg = <0x07>` → `cfg.string_cfg = 7` (bits 0–2 = strings 1, 2, 3). The option table `wled_string_cfg` (1473–1475) has no value list, and `wled_values()` falls through to `return idx` (line 1495), so the DT value is stored raw.
- `qcom,leds-per-string = <0x06>` → `wled->leds_per_string = 6`, read raw at 1932–1933.
- `qcom,auto-calibration` → `cfg.auto_calib_enabled = true` (bool table, 2221).
- `qcom,pmic-revid = <0x1b>` → phandle of the `qcom,revid@100` node under `qcom,pm8150l@4` (dtbo 1392); resolved via `get_revid_data()` at 2241–2257. Used only for the two revision branches described in §3.
- The node **omits** `qcom,boost-current-limit`, `qcom,fs-current-limit`, `qcom,ovp`, `qcom,switching-freq`, `qcom,modulator-sel`, `qcom,cabc-sel`, `qcom,en-cabc`, `default-brightness`, `max-brightness` — so `wled5_config_defaults` (1382–1393) applies: `boost_i_limit = 5`, `fs_current = 10`, `ovp = 4`, `switch_freq = -EINVAL`, `mod_sel = 0 (MOD_A)`, `cabc_sel = 0`, `en_cabc = 0`. Absent `default-brightness`/`max-brightness` leave `WLED_DEFAULT_BRIGHTNESS = 2048` and `WLED_MAX_BRIGHTNESS_12B = 4095` (macros 35–36, probe reads at 2372–2378).
- Physical values behind those indices: `fs_current = 10` → 25 mA full-scale per string (table at 1420–1423: index 10 = 25000 µA); `boost_i_limit = 5` → 525 + 5·175 = 1400 mA (fn at 1410–1413); `ovp = 4` → 38500 − 4·1500 = 32.5 V (fn at 1448–1456); `switch_freq = -EINVAL` → the switch-frequency write is **skipped** (1119–1126).
- One revision clamp in probe: if the die is `PM8150L_V1P0_REV4`, `fs_current` is clamped to 8 (20 mA) at 2388–2390. Whether this applies on our unit depends on the revid read (§3.5).

So the WLED APID's PPID is `(5 << 8) | 0xd8 = 0x5d8`.

---

## 2. How the vendor reaches those registers, and why our scan finds "no channel of ours"

### 2.1 The vendor path is the same SPMI path we already use

`wled_probe` is a `platform_driver` for `qcom,pm8150l-spmi-wled` (lines 2428–2444) whose regmap comes from `dev_get_regmap(pdev->dev.parent, NULL)` (line 2339) — the regmap of the parent PMIC device, covering the whole PMIC bus. Every register access in the WLED driver is `regmap_update_bits`/`regmap_read`/`regmap_write`/`regmap_bulk_write` on that map.

The fetched `spmi-core.c` closes the chain: `spmi_ext_register_writel` → `ctrl->write_cmd` (lines 246–263), `spmi_ext_register_readl` → `ctrl->read_cmd` (lines 265–278), and for the QCOM arbiter those pointers are `pmic_arb_write_cmd` / `pmic_arb_read_cmd` (`spmi-pmic-arb.c:1172–1173`). **No SDP (shared data port) is used anywhere** — grep for `sdp` in `spmi-pmic-arb.c` finds only the channel-offset macros (lines 71–72). The vendor "regmap" is nothing more than the same APID→chnls-window mechanism our SMB5 and PON drivers already drive.

### 2.2 The arbiter's channel-ownership rules (SM8150 = arbiter v5)

`pmic_arb_init` selects `pmic_arb_v5` for `PMIC_ARB_HW_4_0_0` (lines 1247–1250, 1330–1333), and `pmic_arb_v5` sets `pmic_arb_fmt_cmd = pmic_arb_fmt_cmd_v2` (line 1169) — i.e. our command encoding (`COMMAND_WRITE (0<<27)`, `COMMAND_READ (1<<27)`, reg in bits [15:4], byte count in [2:0], `hakios.wled.c:142–151`) is exactly `pmic_arb_fmt_cmd_v2` (`spmi-pmic-arb.c:1027–1035`). v5 commands carry **no SID field**; the SID is implied by the APID.

The APID table is built once at arbiter probe from hardware state, `pmic_arb_read_apid_map_v5` (lines 900–966):

- APID map entries: `apid_map_offset_v5(n) = core + 0x900 + 4*n` (lines 1120–1123). Entry valid iff `regval & BIT(15)` (`PMIC_ARB_APBID_ENABLE`, line 54); PPID = `(regval >> 8) & 0xFFF` (line 924).
- **Write permission is not in the DT** — it is read from the arbiter's own ownership registers: `apidd->write_ee = readl(pmic_arb->cnfg + 0x700 + 4*apid) & 0x7` (`SPMI_OWNERSHIP_TABLE_REG`/`SPMI_OWNERSHIP_PERIPH2OWNER`, lines 59–60, 927–929). These CNFG registers are programmed before Linux boots (by xBL/UEFI).
- Multiple APIDs can map to one PPID, one per would-be writer EE (comment at 908–915). The lookup table keeps the **Apps-owned** mapping: `if (!valid || apidd->write_ee == pmic_arb->ee) ppid_to_apid[ppid] = i | VALID` (937–939). `pmic_arb->ee = 0` comes from `qcom,ee = <0x00>` in the arbiter node (`android-merged.dts:3181`).
- If no valid entry matches the PPID, `pmic_arb_ppid_to_apid_v5` returns `-ENODEV` (968–974) — the APID simply does not exist for this EE.

Channel selection, `pmic_arb_offset_v5` (lines 997–1025):

```c
ppid = (sid << 8) | (addr >> 8);                       /* 1003 */
rc = pmic_arb_ppid_to_apid_v5(pmic_arb, ppid);        /* 1005 */
switch (ch_type) {
case PMIC_ARB_CHANNEL_OBS:
        offset = 0x10000 * pmic_arb->ee + 0x80 * apid;  /* 1012 */
        break;
case PMIC_ARB_CHANNEL_RW:
        if (pmic_arb->apid_data[apid].write_ee != pmic_arb->ee) {
                dev_err(... "disallowed SPMI write to sid=%u, addr=0x%04X\n" ...);
                return -EPERM;                           /* 1015–1018 */
        }
        offset = 0x10000 * apid;                         /* 1020 */
}
```

- **Reads** go through the *observer* channel `0x10000*ee + 0x80*apid` (line 1012). There is **no ownership check on reads** — any EE may snoop any valid APID. This is why SMB5 (slave 2) and PON reads work unconditionally, and why our WLED observer read at init (`hakios.wled.c:112`) would work for any *existing* APID.
- **Writes** go through the RW channel `0x10000*apid` (line 1020), and only if that APID's hardware owner (CNFG) is EE0. Otherwise `-EPERM` with the "disallowed SPMI write" message (1015–1018) — never a silent failure.
- `pmic_arb_supports_write_bw` returns false for v5 (line 1174), so writes are always 1–8 plain bytes — same as our path.

### 2.3 The actual cause of our log line

`hakios.wled.c` scans for PPID `(WLED_SLAVE << 8) | (WLED_BASE >> 8)` with `WLED_SLAVE 4`, `WLED_BASE 0xd800` (lines 100–101) → **0x4d8**. No APID maps to 0x4d8, because the WLED node is under `qcom,pm8150l@5` (dtbo 1652/1826), so the live APID entry holds PPID **0x5d8**. The scan at lines 245–256 correctly reports "slave 4 0xd800 has no channel of ours" — the message is the scan working as designed; the constant is wrong. Our `owner != 0` check (line 248) reads the same CNFG ownership bits the vendor uses (927–929), so once the PPID is fixed the check is the right filter: it will accept the APID only when its write owner is EE0, exactly the condition `pmic_arb_offset_v5` enforces at line 1015.

**Can plain writes work, like SMB5/PON?** Yes, mechanically:

- The mechanism is proven end-to-end on this device. `hakios.pon.c` does not only read — it *writes* PM8150 GPIO pull registers through the same chnls window (`spmi_write` at lines 181–204, used at line 526, readback at 528) and that works on-device. The RW-channel formula `0x10000*apid` is slave-independent; what matters is that the APID exists and its CNFG owner is EE0.
- The vendor WLED driver writes these registers from Apps-EE0 through this very mechanism in the shipped firmware, so APID 0x5d8 is Apps-writable on this unit by construction.
- If a write ever hit a non-owned APID, the vendor behavior is `-EPERM` (1015–1018). Our driver should treat a readback mismatch the same way and say so, not retry blindly.

---

## 3. The register set and the vendor's exact sequences

All line references are `vendor-src/display/qcom-spmi-wled.c`. "ctrl" = offset from 0xd800, "sink" = offset from 0xd900.

### 3.1 Register map (macros at lines 34–185)

Control block (base 0xd800):

| ctrl | name | bits | macro line |
|---|---|---|---|
| 0x07 | WLED5 STATUS | — | 106 |
| 0x08 | FAULT_STATUS | bit0 ILIM, bit1 OVP, bit2 short-circuit, bit4 WLED5 OVP pre-alarm | 42–46 |
| 0x10 | INT_RT_STS | bit1 OVP fault real-time status | 48–49 |
| 0x46 | **MOD_ENABLE** | **bit7 = module enable** | 51–53 |
| 0x48 | FDBK_OP | feedback select (used only by OVP recovery / external-FET paths, 783, 896) | 55 |
| 0x4c | SWITCH_FREQ | [3:0] | 57–58 |
| 0x4d | OVP | WLED4 [1:0], **WLED5 [3:0]** | 60–62 |
| 0x4e | ILIM | [2:0] boost current limit | 64–65 |
| 0x58 | SH_FOR_SOFTSTART | bit0 sample-and-hold for soft start (PM8150L v3.0 die only) | 108–109 |
| 0x5e | SHORT_PROTECT | bit7 (external PFET short protect) | 67–68 |
| 0x5f | OVP_INT_CTL | [6:4] count, [2:0] timer | 111–114 |
| 0x61–0x65 | PRE_FLASH_BRT / PRE_FLASH_SYNC / FLASH_BRT / FLASH_SYNC / FLASH_STEP([2:0]) | flash engine | 116–122 |
| 0x69 | FLASH_HDRM | | 124 |
| 0xd0 | SEC_ACCESS | unlock value 0xa5 (written only in the external-FET setup path, 1334) | 70–71 |
| 0xe2 | TEST1 | 0x09 = external FET DTEST2 (same setup path, 1340) | 73–74 |
| 0xe5 | TEST4 | bit5 = sample-and-hold (non-v3.0 dies) | 126–127 |
| 0xef | **PBUS_WRITE_SYNC_CTL** | HFRC/PBUS sync control; the driver writes 0 or 3 | 129 |

Sink block (base 0xd900):

| sink | name | bits | macro line |
|---|---|---|---|
| 0x46 | CURR_SINK_EN | [7:4] = enable current sinks 1–4 (string n ↔ bit n+3) | 77–79 |
| 0x47 | SYNC | [3:0] FSC sync pulse, strings 1–4 | 81–87 |
| 0x50 / 0x60 | MOD_A_EN / MOD_B_EN | bit7 | 132–134 |
| 0x51 / 0x61 | MOD_A/B SRC_SEL | [1:0]: 0=high, 1=CABC1, 2=CABC2, 3=external | 136–142 |
| 0x52 / 0x62 | MOD_A/B BR_WID_SEL | 0=12-bit, 1=15-bit brightness | 144–147 |
| 0x53 / 0x54 | **MOD_A BRT LSB / MSB** | LSB byte, MSB nibble ([3:0] 12-bit, [6:0] 15-bit) | 149–150 |
| 0x63 / 0x64 | MOD_B BRT LSB / MSB | | 151–152 |
| 0x65 | **MOD_SYNC_BIT** | bit0 = latch MOD_A, bit1 = latch MOD_B | 154–157 |
| 0x72+0x10n | string n FS_CURR | [3:0] (n = 0..3 → strings 1..4) | 159, 96 |
| 0x73+0x10n | string n SRC_SEL | [1:0]: 0=MOD_A, 1=MOD_B | 161–164 |
| 0xb0–0xb5 | FLASH_CTL(bit7 flash, bit6 pre-flash) / FLASH_SINK_EN / FLASH_FSC([3:0]) / FLASH_SYNC(bit0) / (0xb4 gap) / FLASH_TIMER_CTL | flash/torch engine | 166–183 |
| 0xb6 | **FLASH_SHDN_CLR** | clear flash shutdown state | 185 |

### 3.2 Setup at probe — what the vendor actually writes on this device

`wled5_setup` (1094–1233), with this node's values substituted:

1. `wled_flash_setup` first (1102) — flash/torch registers 0xb0–0xb6 etc.; irrelevant to display brightness.
2. ctrl 0x4d [3:0] ← **4** (OVP 32.5 V) — 1107–1109.
3. ctrl 0x4e [2:0] ← **5** (ILIM 1400 mA) — 1113–1115.
4. switch-frequency write **skipped** (`switch_freq == -EINVAL`) — 1119–1126.
5. For each set bit of `string_cfg = 7` (i = 0, 1, 2 → strings 1–3): sink 0x72+0x10·i [3:0] ← **10** (25 mA FS), sink 0x73+0x10·i [1:0] ← **0** (MOD_A); accumulate `sink_en |= 1 << (i+4)` — 1129–1149.
6. CABC configured **off** (`cabc_sel = 0`) — 1152.
7. sink 0x50 bit7 ← **1** (MOD_A enabled); sink 0x60 bit7 ← 0 — 1157–1169.
8. sink 0x52 ← **0** (12-bit brightness width; `max_brightness` is 4095, not 32767) — 1171–1174.
9. sink 0x46 [7:4] ← **0x70** (current sinks 1–3 enabled) — 1178–1180.
10. `wled_sync_toggle`: sink 0x47 [3:0] ← 0x0F, then ← 0x00 (latch the FSC changes) — 1185, function 364–379.
11. `wled_auto_calibrate_at_init` (node has `qcom,auto-calibration`) — 1191.
12. Request OVP IRQ; then read ctrl 0x46 and **disable the OVP IRQ while bit7 is clear** — 1195–1212.

### 3.3 Module enable / disable — the blank/restore primitive

`wled_module_enable(wled, val)` (303–355), PM8150L branch:

1. ctrl 0xef ← `val ? 0 : 3` ("force HFRC off"; plain `regmap_write`) — 312–318.
2. ctrl 0x46 bit7 ← `val` (`regmap_update_bits`, mask BIT(7)) — 320–322.
3. Only when enabling: sink 0xb6 ← **0** (clear flash shutdown latch) — 327–332.
4. Only when enabling: `usleep_range(10000, 11000)` — soft-start wait, `WLED_SOFT_START_DLY_US = 10000` (39, 340–341) — then re-enable the OVP IRQ; when disabling, disable the OVP IRQ — 343–352.

### 3.4 Brightness

`wled5_set_brightness` (429–466), with `max_brightness = 4095`, `mod_sel = MOD_A`:

1. Clamp: `0 < brightness < 4095/1000 = 4` → brightness = 4 (0.1 % floor) — 432–437.
2. 2-byte bulk write at sink 0x53: `v[0] = brt & 0xFF`, `v[1] = (brt >> 8) & 0x0F` (12-bit MSB mask; 0x7F only for 15-bit) — 439–448, register table `wled5_brt_reg[MOD_A] = 0x53` (270–273).
3. sink 0x65 [1:0] ← **0x01** (pulse MOD_A sync), then sink 0x65 [1:0] ← **0x00** — 452–465.

### 3.5 Framebuffer blank / unblank

`wled_update_status` (502–569): any of `power`/`fb_blank` non-unblank or `BL_CORE_FBBLANK` forces brightness 0 (508–511).

- **Blank (brightness 0):** *only* `wled_module_enable(0)` — no brightness-register write at all (546–552).
- **Unblank (brightness > 0):** set brightness (515) → S_H off (521–528) → `wled_module_enable(1)` **only if `prev_state` was 0** (530–536) → S_H workaround (538–545).
- **S_H workaround** `wled5_sample_hold_control` (381–427): acts only when coming from off (`prev_state == 0`), CABC disabled, and brightness ≤ `max/100` (= 41); sleeps 5 ms (407–408); then on `PM8150L_V3P0_REV4` dies toggles ctrl 0x58 bit0, otherwise ctrl 0xe5 bit5 (411–419). The die revision comes from the revid node phandle 0x1b → `qcom,revid@100` under `qcom,pm8150l@4` (dtbo 1392; PPID 0x410 — readable through the observer channel).
- WLED5 does **not** do the final sink-0x47 sync toggle after brightness changes (that is WLED4-only, 556–562).
- Fault inspection: ctrl 0x08 bits (42–46), ctrl 0x10 bit1 (48–49), ctrl 0x07 (106).

---

## 4. Is the retail panel's backlight actually driven by WLED? — the evidence says no

- `android-merged.dts`: zero occurrences of `wled`, `backlight`, or `bl-pmic-control-type` — the base DT has no display backlight configuration at all.
- In the applied DTBO, every panel carrying an OPPO customization property (`oppo,mdss-dsi-vendor-name`) uses **DCS** backlight control:
  - `qcom,mdss_dsi_samsung_s6e3fc2x01_fhd_plus_cmd` (4003; vendor name "S6E3FC2X01" at 4028; 1080×2340 = 0x438×0x924 at 4067–4068): `bl_ctrl_dcs` at **4056**.
  - `qcom,mdss_dsi_samsung_s6e3fc2x01_fhd_plus_61fps_cmd` (4125; vendor name 4150): `bl_ctrl_dcs` at **4178**.
  - `qcom,mdss_dsi_samsung_sofef03f_m_fhd_plus_90fps_cmd` (4247; vendor name "SOFEF03F_M" 4274): `bl_ctrl_dcs` at **4277**.
  - Even the reference `rm69299` node (4587) uses `bl_ctrl_dcs` (4609).
- On these panels brightness itself is a DCS write (WLED is not involved): the on-command sets panel brightness registers via DCS `53 20`/`55 00` (4084), HBM is DCS `51 03 ff` + `53 e0` (4091–4093), display-off is DCS `28` + `10` (4086), AOD levels are DCS `53 22`/`53 23` (4094–4095).
- The `bl_ctrl_wled` nodes (2819, 2901, 2990, 3091, 3164, 3250, 3347, 3428, 3506, 4563) are **all Qualcomm MTP/reference panels** (`sim_sec_hd`, `sharp_4k_dsc`, `nt35597_truly`, `nt35695b_truly`, `nt36850_truly`) — none carries an `oppo,` property.
- The WLED node's `label = "backlight"` (1830) is only the backlight-class name used on MTP builds. Its phandle `0xe5` (1838) is **referenced by no other node** anywhere in the DTBO (grep for `= <0xe5>` matches only the definition).
- No display node has a backlight phandle: `qcom,dsi-display-primary` (5072–5088) lists only `vddio-supply`/`lab-supply` (resolved by local_fixups 8121–8122).

**Conclusion:** on the retail PCLM10 the panel backlight is driven by DCS commands inside the panel/MDSS driver, not by the PM8150L WLED module. The camera flash is the separate `qcom,leds@d300` flash driver (1695). WLED5 is still configured and may still be *enabled* (the vendor driver runs `wled5_setup` + backlight class regardless), but toggling ctrl 0x46 bit7 is not guaranteed to dim the retail panel.

**Decisive non-invasive test (read-only, works today):** observer-read ctrl 0xd846 (PPID 0x5d8, observer channel `0x80*apid`) while the screen is lit at normal brightness. bit7 = 1 ⇒ the vendor keeps WLED enabled while lit (either it drives the backlight, or it idles unused); bit7 = 0 while lit ⇒ WLED definitively does **not** drive the panel light, and our blank/restore hook should not be expected to dim anything.

---

## What our driver should do next

1. **Fix the slave constant.** `drivers/display/hakios.wled.c:100` `WLED_SLAVE 4` → **5**. The scan (245) then computes PPID **0x5d8**, finds the live APID, and the existing owner check (248, CNFG bits [2:0]) enforces exactly the vendor's write rule (`spmi-pmic-arb.c:1015`). Nothing else about the scan changes; the "no channel of ours" message disappears once the PPID is right (or legitimately reports a non-EE0 owner).
2. **Keep the observer read at init** (`hakios.wled.c:112`) — it works for any valid APID without ownership (arbiter line 1012) and gives the cached value of ctrl 0x46 for read-modify-write.
3. **Blank sequence** (mirror `wled_module_enable(0)`, §3.3), via the existing chnls write path (`0x0c600000 + 0x10000*channel`, command `COMMAND_WRITE | reg<<4`, WDATA0 = value — `hakios.wled.c:142–151`, proven working by `hakios.pon.c:526`):
   - write slave 5, **ctrl 0xd8ef ← 0x03**
   - write slave 5, **ctrl 0xd846 ← (cached & ~0x80)**
4. **Restore sequence** (mirror `wled_module_enable(1)`):
   - write slave 5, **ctrl 0xd8ef ← 0x00**
   - write slave 5, **ctrl 0xd846 ← (cached | 0x80)**
   - write slave 5, **sink 0xd9b6 ← 0x00**
   - **wait ≥ 10 ms** (soft start, `WLED_SOFT_START_DLY_US`, line 39)
5. **Brightness (only if we choose to drive it)** — mirror §3.4: sink 0xd953 ← `brt & 0xFF`, sink 0xd954 ← `(brt >> 8) & 0x0F`, then sink 0xd965 ← 0x01, then sink 0xd965 ← 0x00. First observer-read sink 0xd952 to confirm 12-bit width (vendor writes 0 at setup). The vendor never writes brightness during blank (§3.5).
6. **Do not touch** sink 0x46/0x50/0x52/0x72/0x73 at runtime — those are probe-time setup (§3.2) and the vendor leaves them alone while running.
7. **Gate the effort on the §4 test.** Until the observer read of ctrl 0xd846-while-lit shows bit7 set, treat WLED blank/restore as a diagnostic, not as the panel dimmer: the retail panels are `bl_ctrl_dcs` and their brightness lives in DCS `0x51`/`0x53` commands.
8. Optional, for the S_H branch only: observer-read the PM8150L revid (PPID 0x410 = sid 4, peripheral 0x10, registers 0x04/0x05/0x06) to know whether ctrl 0x58-bit0 or ctrl 0xe5-bit5 is the sample-and-hold control (§3.5). Only needed if we ever bring the module up at ≤ 1 % brightness.
