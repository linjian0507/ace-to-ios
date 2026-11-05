# OPPO Reno Ace (pclm10) — porting log

> **当前可用功能与交付状态（2026-11-05）**：先看 [功能与交付状态](notes/功能与交付状态.md) 及 [功能落实计划与勾选账本](notes/功能落实计划.md)。下文是历史移植日志，不是当前日常可用性承诺。用户已要求逐项实现/测试/验收，做好一项划掉一项；**全部通过后**统一更新冷启动内核与配套内容。当前不提前刷写，测试外触摸/按键体验尚未闭环。

A HakiOS device repository: what is true of this device and of no other. The kernel, the loader, the drivers and the tools are
the common repository's; the sm8150 family's facts are in `soc/`, carried with the device. How the three are put together:
[docs/porting/repositories.md](https://github.com/oosexgang/hakios-xnu-kernel/blob/main/docs/porting/repositories.md).

| What | Where |
|---|---|
| SoC | Qualcomm Snapdragon 855 (SM8150, "msmnile" in the device tree) |
| Phone | OPPO Reno Ace (PCLM10), 1080x2400 90 Hz panel, 8/12 GB LPDDR4X, UFS 3.0, WCN3990 Wi-Fi, Adreno 640 |
| Android | ColorOS 11.1 / Android 11, `PCLM10_11_F.07`; BL unlocked (`verifiedbootstate=orange`) |
| U-Boot for the phone | `u-boot/files/`: its device tree (`sm8150-oppo-pclm10.dts`), `hakios_pclm10_defconfig` and environment, which `uefi/u-boot/apply.sh` lays over U-Boot (the common repository's `docs/u-boot.md`) |
| Entry point | the phone's ABL has no `fastboot boot` command and **never loads a recovery image** (a control test with the stock recovery proved it: its recovery UI is ABL's own), so U-Boot is **flashed to `boot`** with the stock image's AVB footer transplanted; Android lives on slot `_a` and a `fastboot flash boot bk/boot.bin` is one command from home — but only from a fastboot that can flash, which the loader's is not (the Nov 3 section says the way home from the loader) |
| The phone's firmware | **not in the repository**: `pull-firmware.sh` pulls it off your phone over `adb` into `fw-*/` at the top; `pull-firmware-root.sh` if the files are root-only |
| The SoC's addresses | `pull-soc.sh`: the phone's own device tree over adb, turned into `soc.args` by `tools/soc-args.py` |

Nothing here is upstream-ready: this is a working tree for one phone, by its owner.

## Where the port stands

Done, without the phone:

- `tools/hakios-device.py check pclm10` holds; `make pclm10_defconfig && make config` passes (kernel.ver built, all 21 phone
  drivers off, only virtio left).
- `make u-boot DEVICE=pclm10` builds: `u-boot/build-pclm10/u-boot.bin` with `CMD_HAKIOS=y`. U-Boot v2026.07 already carries
  SM8150: `CLK_QCOM_SM8150`, `PINCTRL_QCOM_SM8150`, UFS, DWC3, PMIC GPIO, RPMH — the defconfig is `qcom_defconfig` plus the
  HakiOS settings, like redwood's. (An early `u-boot/` was a half-applied tree, the patch having failed on CRLF line endings:
  moved aside and rebuilt from `uefi/u-boot/apply.sh`, which now applies cleanly.)
- **The whole start chain rehearsed end to end under QEMU** (`qemu/run-u-boot.sh`, all prebuilt: kernel, loader, hello ramdisk):
  U-Boot's bootcmd runs `hakios load 0x50000000 && hakios boot` → the payload lands at 0xf0000000, the loader starts → XNU
  boots → `Hello from user space on Darwin!` → `panic: initproc failed to start`, which is exactly where a run with no root
  image is supposed to end (docs/porting/index.md step 2). Two fixes fell out of it: `hakios_qemu_defconfig`'s BOOTCOMMAND
  referenced an undefined `hakios_start` (now `hakios boot`), and feeding commands over the QEMU serial eats the first
  character while autoboot counts down — the phone's env uses BOOTCOMMAND, not fed input.
- root is scripted: `/root/magisk-patch-boot.sh` patches the EDL-read `boot.bin` with Magisk's own `boot_patch.sh` (from the
  v31.1 APK, with its arm64 magiskinit/magisk/init-ld; the x86_64 magiskboot runs in WSL) and writes a padded
  `boot-magisk.bin` for `wr_boot.xml`; tested end to end against a synthetic boot image (`cpio test` says patched).
- the image step is rehearsed too: `tools/mkubootimg.py -d pclm10` runs end to end (`/root/mkubootimg-selftest.sh`) and
  the result unpacks clean: kernel = gzip(u-boot) matching the build, the DTB in its own header field, an HKUB ramdisk
  with `payload` + `loader`, all CRCs good; 17.5 MB against the 96 MB recovery partition.

What the phone's own tree settled (read out of its `boot` partition over EDL, 31 Oct; its `dtbo` holds two audio
overlays — `MTP` and `SDX50M MTP` — and no board overlay, so that tree is the whole story for this phone):

- `prebuilt/device-tree.dtb` **is** that tree, 506207 bytes: `qcom,msm-id = <339 0x20000>` (SM8150 v2),
  `qcom,board-id = <0 0>`, `oppo,dtsi_no = <0x4a89>` — the identity ABL matches, so it takes the image and patches
  the memory node into the tree it hands over.
- `cont_splash_region` is at **0x9d000000 for 36 MiB**, not the SM8150 reference 0xe1000000. The DTS here uses it, and
  so does the loader: `platform_sm8150` is in `xnuloader/loader.c` and `efi_main()` knows this phone by
  `SCREEN.base == 0x9D000000ULL`.
- `capacity-dmips-mhz` is 0x400 on cpu0-3 and 0x6cc on cpu4-7 and `psci { method = "smc" }`, so the loader's
  `small_cores = 0x0f` and its SMC calls are what the phone itself says; its GIC is
  `reg = <0x17a00000 0x10000 0x17a60000 0x100000>`, the addresses redwood's SM7325 has too.
- the base DTB has **no volume keys** — no `gpio-keys`, no `KEY_*`, no `linux,code` — so U-Boot only ever had what the
  PON hardware gives it: the power key and the resin. Where the volume keys actually are was settled on the phone on
  Nov 2, not from this tree: RESIN never moved once while both keys were pressed over and over (logs/093440-run.log),
  and the dtbo's own `gpio_keys` node names them — `vol_up` and `vol_down` are GPIO6 and GPIO7 of the PM8150's
  `pinctrl@c000`, active low. The kernel reads them there; see the Nov 2 section at the end.
- `soc.args` is what `tools/soc-args.py` prints over that tree, which is what `pull-soc.sh` would have written from
  `/sys/firmware/fdt`. The tree names its modem `qcom,mss@4080000` (PAS 4) and its IPA `qcom,ipa@1e40000`; it has no
  `soc_mpss_mem`-style reservations for them, so those stay at the kernel's built-in values — which is only about
  starting MPSS, and that does not happen on this path.

On the phone, 1 Nov: the whole chain runs. `avb_footer_surgery.py` (Windows Python) moves the stock boot's AVB footer and
vbmeta blob onto our image, rewriting only the hash descriptor's `image_size` and `digest` (OPPO's key and the stale
signature stay; orange state boots it without a word — ABL showed no warning). Flashed to `boot` with
`fastboot flash boot` + `fastboot reboot` (`fastboot boot` answers `unknown command`): U-Boot came up on the panel —
UFS enumerated all 73 partitions in 6 units, `hakios load` found the image, the loader built the device tree
(`device tree f8410000 len 3874`) and started the sm8150 kernel (`booted to f77424a8`), the kernel ran and returned,
and its 12200-byte log went to `logdump` and read back the same. Rescue works too: `fastboot flash boot bk/boot.bin`
brings ColorOS back, proven several times.

The first `xl run` (1 Nov, after USB came up) hung the kernel and taught the expensive lesson: **without
`bootcpu=7` the kernel runs on the core the loader was on, so a kernel that hangs takes the loader's USB down with
it** — `getvar result` answered nothing, no `.log.live` could be fetched (even `oem log` needs a live loader), and
with no watchdog armed for `platform_sm8150` the phone stayed a zombie: screen dark, the controller left unable to
even answer a device descriptor (`VID_0000&PID_0002` on Windows), only a 60 s power press brings it back. So every
run on this phone starts with `XL_CMDLINE="-v bootcpu=7"` (docs and `xl` header: the loader watches from core 0,
a hung kernel still gives up its log, and `fastboot reboot` ends a run without keys). The vwdt guard for
`platform_sm8150` (the Haven SMCs are in its tree too) went from "if it ever resets on its own" to "do it": a
hung kernel should cost a reboot, not a person at the phone.

What that run did not have is USB: nothing enumerated (`fastboot flash` needs `18d1:4ee0`). Two things came out of
reading the tree afterwards:

- redwood's USB works because ABL initialized the controller: its U-Boot is started **from ABL's fastboot mode**
  (`fastboot boot`), which is the assumption written in its DTS ("the SuperSpeed PHY is left as ABL left it").
  Started cold from `boot`, ABL never touches USB: the GDSC, clocks and PHY have to come up in U-Boot. Driver model
  does the GDSC by itself (`device_probe()` → `dev_power_domain_on()`, and `clock-sm8150` registers the GCC's
  `qcom_power`/`qcom_reset` children), and `dwc3_glue` enables the clocks and BCR — what was left broken is below.
- the DWC3 **core** node still carried the upstream `phys = <&usb_1_hsphy>, <&usb_1_qmpphy ...>`; the QMP PHY is
  `disabled`, nothing binds it, and `generic_phy_get_bulk()` fails on every entry it cannot resolve — so
  `dwc3_setup_phy()` failed and the gadget never existed. redwood never sees this: kodiak's controller is a flat
  node and its DTS override already lists only the HS PHY. Fixed here by overriding `&usb_1_dwc3` itself
  (`phys = <&usb_1_hsphy>`, `phy-names = "usb2-phy"`, high-speed, peripheral).
- the screen stopped right after the `logdump` line because `CONSOLE_RECORD_OUT_SIZE=0x6000` was full — U-Boot
  prints nothing once its record buffer wraps, and the USB error was in the part that got dropped. pclm10's
  defconfig now uses `0x40000` (the 73-partition UFS log alone is near 24 KB).
- **the phone left in U-Boot for hours is hard to bring back**: no display (the kernel's last state of the panel
  clocks, U-Boot does not re-init it after a returned kernel), keys answered only by U-Boot's polled buttons, and
  the charge path left without Android's manager. The next morning it answered nothing — no charging UI, no key,
  nothing on USB — yet after 20 min on the charger the battery area was warm, so the PMIC and the battery were
  fine: a recovery window (charge ~1 h, then power 60 s, then the EDL key trick) is the order to work through,
  and EDL itself does not depend on what is in `boot`. Do not leave a run parked on the panel overnight: end it
  with `fastboot reboot` or a long power press. (The back is glued, not removable: no battery connector to reseat.)
  The hung-kernel zombie of 1 Nov (the same state reached in minutes, not overnight) answered the **short version
  of the same order**: unplug the cable first — with VBUS present the PON swallows long presses, which is why the
  keys seemed dead — then power 60 s, then power+volume-up 15 s; the loader enumerated again within the watcher's
  window. The keys are PMIC PON hardware, so one uninterrupted press is what counts, not repeated presses.

Still ahead on the phone:

- USB is **solved**: the payload needs `mkpayload.py --usb` (without it the loader parses no `usb` env entry and
  never calls `usb_start()`). With it, `boot-avb3.img` enumerated on the first try: `getvar product` answers
  `xnuloader`, `getvar version` answers `0.5` (a live loader), and `xl state` says `loader`. `xl log` and
  `xl screenshot` work — the screenshot shows the whole chain on the panel, U-Boot through `usb: started`, and
  the gadget moves 48 MB/s. One earlier worry dissolves with it: the "silent death" of the direct boots was the
  payload's `$ARGS` (`ufs=2`) carrying **no `loader_return_after`** — the kernel was very likely alive the whole
  time, it just never returned and nothing could ask it anything. Every `xl run` now ends on purpose after
  XL_SECS seconds and brings its log back over USB, hang or no hang (`XL_FOLLOW` snapshots it every 10 s).
- firmware: `/vendor/firmware*` is closed to the shell, so `pull-firmware-root.sh` runs once the patched boot boots.
  Two things are already known: the kernel's GPU driver asks for `a660_*` while this SoC's Adreno 640 needs the
  `a630_*` files, and WiFi is a WCN3990 (`qcom,wlan-rc-num`, its own PIL region) with no `wpss`, so `drivers/wifi/`
  does not apply to it.
- the hypervisor watchdog: `wdt_arm()` runs for redwood only. If this phone resets itself a few seconds into a run,
  the Haven vwdt SMCs (0x86000005-8) are here too and that guard has to cover `platform_sm8150` as well.

The kernel hang of 1 Nov, and what it turned out to be. With `bootcpu=7` the loader watches from core 0, and every
run says `result: hung 0 0` with `heartbeat 1` and 0 logged bytes — on `bootcpu=7` and `bootcpu=1` alike, so not a
big.LITTLE thing. `xl screenshot` during a run settles it: the kernel is alive and deep — `Darwin Kernel Version
22.5.0`, IOKit, `GenericARMPE: platform "D2QAP"`, cpu 0 (phys id 0x700) registered, network stack, and
`MiniIOMobileFramebuffer: display 1125x2436 on a panel of 1080x2400` — two screenshots 15 s apart end on the same
line, timestamp frozen at `[12.220259]` (`logs/midrun7a.png`, `logs/midrun7b.png`). "0 logged bytes" is normal:
`loader_log_putc` only carries the banner, the msgbuf at return time, and a few special paths — ordinary console
output never reaches the mailbox. The heartbeat explains the rest: `hv_wdt_pet_init` is the last EARLY_BOOT startup
and bumps the heartbeat once by itself, so **heartbeat 1 means the kernel finished EARLY_BOOT**; after that the
heartbeat, the `loader_return_after=8` return and the loader-stop poll all ride on `thread_call_enter_delayed`, and
none of them ever fires — **the kernel has not taken one timer interrupt**. The reason is in the phone's own tree:
its `timer` node (`arm,armv8-timer`) declares `interrupts = <GIC_PPI 1 0xf08>, <GIC_PPI 2 0xf08>, <GIC_PPI 3 0xf08>,
<GIC_PPI 0 0xf08>` — PPI numbers 17, 18, 19, 16, not a bare board's 26/27/30/31 — because ColorOS's kernel is a
guest of Qualcomm's QNX/Haven hypervisor (the same one the loader's vwdt SMCs talk to), and that hypervisor injects
its guests' timer interrupts under its own numbers. The kernel drives CNTV and enables PPI 27, which QNX keeps for
itself: the tick never arrives, and with no tick nothing timed ever happens again. redwood never sees this because
its SM7325 boots bare. Settled by experiment, with `gic_timer_ppi=N` (1 Nov): **16, 17, 18 and 27 all give no tick,
19 does** — with 19 enabled the kernel booted all the way to user space and back (`logs/124356-run.log`:
`Hello from user space on Darwin!`, `I am process 1`, the expected `initproc failed to start` end, and the
`loader_return_after=8` return working for the first time). So QNX hands its guests' EL1 timer interrupt to INTID 19,
which is not even the number the phone's own device tree calls "virtual" (18). The driver now enables 27, 18 and 19
and takes a tick from any of them — the ones with no source stay quiet on a bare board — and `gic_timer_ppi=N`
still pins one number for experiments. Rebuilt with that default (`make kernel`, theos in `/root/theos`,
`xnu/apply.sh` + `xnu-deps/setup.sh` done; `dsymutil` linked into `xnu-deps/host/bin`, `python-is-python3`):
`logs/124611-run.log`, no boot-arg, `timer on PPIs 0x80c0000`, same clean run to user space and back.

The phone runs Darwin userland from RAM, 1 Nov. `xl ramdisk userland/system.apfs` (2361 MB in 65 s, 36 MB/s) and a
run with the small image: the loader hands over `resident image ... it is the root (md0), the payload ramdisk is
md1`, init's self-checks pass (fork/exec/pipe, dyld and the shared cache), init hands process 1 to the stock launchd,
and its jobs come up on the phone: logd, cfprefsd, containermanagerd, notifyd, **backboardd**, MobileGestaltHelper
answering over the framebuffer, and the demo jobs (`surf_test` creating real IOSurfaces); `com.apple.SpringBoard`
starts as pid 6 with `libhakios.sb.dylib` injected, backboardd logs `System shell connected`, and `XL_SECS=60` runs
end with `--- loader_return_after expired ---` and the full log back (`logs/125133-run.log`, `logs/125323-run.log`).

The panel still shows the kernel's **text console** during a run, not the SpringBoard picture (`logs/springboard60.png`,
48 s into a 60 s run): launchd's os_log lines over the shared cont_splash framebuffer, with the loader's old upload
progress lines still showing through at the bottom. That is the expected state one step short: `MiniIOMobileFramebuffer`
switches the console off by itself the moment a real surface is presented (`fGraphics = true`, `PE_initialize_console
(kPEGraphicsMode)`), and no surface ever is, because backboardd's CoreAnimation says `Failed to initialize Metal or
OpenGLES contexts!` (`logs/125452-run.log`) — the image was built without `GL=1`, so there is no `libhakios.gl.dylib`
and `inject.c opengl_es()` returns at once. The way to a picture on this panel is known and does not need the GPU:
`tools/mk-mesa.sh` (Mesa 25.0.7, softpipe; needs meson/ninja/llvm-ar/python3-venv in WSL) + `GL=1 userland/build.sh`
+ boot-arg `gl=1`, which is exactly "also without a GPU, by Mesa's software driver (slow)" (`inject.c`, and
`userland/src/eagl.c` for what CoreAnimation asks of OpenGL ES). The GPU path — `gpu=1`, freedreno through
`drivers/gpu/hakios.adreno.c` + `../fw-gpu` — is the step after that.

**The iOS home screen is on the panel, 1 Nov, with no GPU at all.** `tools/mk-mesa.sh` built Mesa 25.0.7 in about a
minute once meson/ninja/llvm-ar/python3-venv were installed (`libOSMesa.8.dylib`, 14.8 MB, 1296 `gl*` functions,
softpipe + freedreno); `GL=1 userland/build.sh` put it and `libhakios.gl.dylib` into `userland/root.apfs` (5.9 MB);
and `XL_SECS=180 XL_CMDLINE='-v bootcpu=7 gl=1' XL_RAMDISK=userland/root.apfs` drew the SpringBoard home screen:
`logs/uigl150.png`, taken ~150 s into the run, shows the real icons — FaceTime, Calendar (SUN 1), Photos, Mail, an
analog Clock, Podcasts, App Store, Maps, Health, Wallet, Settings — with their labels, the four-item Dock and the
Search pill. The run ended normally (`logs/130728-run.log`, `--- loader_return_after expired ---`, `returned 392962 0`).
The log carries the whole chain: `hakios-gl: ... OpenGL ES 3.1 Mesa 25.0.7; softpipe`, all seven EAGLContext methods
replaced, the 3317-character extension string read back, and an IOSurface of 1125x2436 BGRA attached through
`-texImageIOSurface:` — CoreAnimation drawing through Mesa into the cont_splash framebuffer, software only.
Two things to know about the picture: console text from before `fGraphics` takes over still shows at the top (the
first frame does not clear the panel, and there is no blur to hide it — softpipe has no blur), and the screenshot
itself is sheared sideways because the loader reads the panel as 1080x2400 while the kernel draws 1125x2436; both
are cosmetic. One unreproduced oddity: an earlier 60 s run (`logs/130500-run.log`) came back at ~25 s with status 0
and no panic and no `loader_return_after` banner — the only paths into `loader_return()` besides the timer are
`halt_all_cpus()` (a reboot syscall) and the panic paths, and the log shows none of their prints; the 180 s run,
also a second kernel in the same loader session, did not repeat it.

The EDL read flow (proven 31 Oct, scripted in `edl_now.ps1`/`edl_read3.ps1` on the Windows side): `adb reboot edl` →
poll `VID_05C6&PID_9008` for the COM port → `QSaharaServer -p \\.\COMx -s 13:...qtool\bin\tool\qctool\oppo\devprg_855`
→ **one small `<read>` first, without `--skip_configure`** (`prime.xml`, one sector of lun0) → then the real read XML.

The `<configure>` matters more than it looks. This programmer (OPPO `devprg_855`, built 4 Mar 2020) answers every `<read>`
with garbage unless it has been configured: with `--skip_configure` it returns the *same internal buffer* whatever is
asked — any LUN, any `start_sector`, 1 sector or 96 MiB — and the tell-tale is that every output file has identical
bytes, a 93-byte `<?xml ...><response value="ACK" rawmode="true" /></data>` prefix, and no partition data at all. Once a
`<configure>` has been sent the target logs `Storage type set to value UFS` and the same XML reads `ANDROID!`, `AVB0`
and `EFI PART` where they belong. The configure stays in effect for the running firehose, so one prime read is enough
for every read after it; a new EDL session needs it again.

This fh_loader build (23.06.20) has **no `--prog` option** — passing it dies with `Invalid input to GetFullDirPath`; the
programmer must be loaded by QSaharaServer first. There is no `--reset` either; `--power=reset,1` is the valid way to
leave (`--power=restart-edl` is rejected as an invalid option), and without it the phone leaves EDL on its own when the
session times out. Note that after an EDL session ColorOS sometimes comes up with the USB function set to MIDI, which
leaves no adb interface: replugging the cable with the screen unlocked brings adb back.

One PowerShell trap cost a false start on the write: `@("--port=\\.\" + $com, '--memoryname=ufs', ...)` quietly becomes
**one** string — the trailing `\"` is parsed as an escaped quote, so the rest of the list is swallowed — and fh_loader
answers `Failed to open com port '\\.\COM6 --memoryname=ufs ...'` while QSaharaServer on the same port is fine. Write
the port the way `edl_now.ps1` does, as one interpolated string: `"--port=\\.\$com"`.

The order from here (the EDL read flow above is done and proven; writes now go through ABL fastboot, which this
phone does have — only `fastboot boot` is missing): `xl enter` is not needed either — the flashed payload *is*
the resident loader, so `xl` talks to it directly (`xl run` uploads a kernel and starts it; `enter` returns at
once when `state` is already `loader`). First `xl run` with `XL_RAMDISK=prebuilt/userland/hello` (no root.apfs
is built yet). Then the dual-boot step: the stock Android boot image goes to the empty `recovery` partition as
a copy, and U-Boot learns to chainload it (parse the `ANDROID!` header, add the `androidboot.*` command line).
Root comes after: the same footer surgery on `boot-magisk.bin`, and `sysimage/unpack.sh` + `userland/build.sh`
in WSL for a real root.apfs.

A survey of **UniXNU** (github.com/UniXNU, unixnu.com — an OpenCore-like UEFI environment for running iOS/macOS on
non-Apple ARM64) is worth keeping: its device catalog carries three other SM8150 ports, and **guacamole (OnePlus 7
Pro, SM8150, Adreno 640 — the closest machine to this phone) reports iOS 16.7.16 userland reached, GPU pending**;
nabu (Xiaomi Pad 5) runs launchd, cepheus (Mi 9) has a prepared boot chain. It is a different stack — per-device
UEFI firmware repositories plus shared SoC components (`ShengUfs`, `QcomXnuUsbDebug`, `GopFramebuffer`, Adreno
drivers) — and there is no pclm10 port, so switching now would redo the AVB/ABL handoff inside their framework.
Keep it as the plan B and as reading: how guacamole brings up dwc3 on this exact SoC is directly relevant if the
U-Boot gadget refuses to come up after the `--usb` payload.

## Differences from the reference phone to keep in mind

- Wi-Fi is a WCN3990: its firmware runs on the ADSP, there is no `wpss` — `drivers/wifi/` (built for WCN6750/wpss) does not
  apply; the WCN3990 path is its own work.
- The GPU (Adreno 640) names its firmware `a630_*` on CAF targets of this SoC; `drivers/gpu/` asks for `a660_*` by name.
  `pull-firmware.sh` takes whatever names the phone really has.
- No Mu-Silicium UEFI exists for this phone: the U-Boot path (docs/u-boot.md) is the way in, and the ABL stays.
- The Reno Ace has no `boot` fastboot command in its ABL; EDL (9008) with `devprg_855` is how partitions are read and
  written. The EDL window after `reboot edl` is short (~20 s): poll for the QDLoader COM port, do not sleep.

## Running

Nothing is upstream tooling here yet: the Windows side keeps `adb`/`fastboot`/EDL (OET `fh_loader` + `devprg_855`), WSL
builds. Partition layout as read from the phone's GPT (4096-byte sectors): boot lun4/94982/24576, recovery lun4/715850/24576,
dtbo lun4/709706/6144, vbmeta lun4/709690/16.

`xl` runs from the Windows checkout in Git Bash (`docs/windows.md`), with two local additions: fastboot/adb live at
`C:\Users\renny\ADBTools\platform-tools` (not `C:\platform-tools`), and **Git Bash's `python3` is the empty WindowsApps
stub — it answers rc=49 with no output**, which fails `xl screenshot` and `mkpayload.py` silently. The fix is a shim
named exactly `python3` (no extension: MSYS prefers an exact-name match in any PATH directory over a `.cmd` earlier in
PATH) that execs `/c/Program Files/Python310/python.exe`; `tools/win-bin/flock` covers the rest. The wrapper script
lives outside the repository (`%TEMP%\opencode\xlr.sh`): it sets that PATH and defaults `XL_RAMDISK` to
`prebuilt/userland/hello` until a real root.apfs is built.

## The panel during a run: why the home screen vanished from the 13:23 screenshots (Nov 1)

The 13:19 kernel (first-frame `bzero` clear, see above) ran at 13:23:53 (`logs/132353-run.log`) and drew only
three frames — 13:24:34, 13:24:37, 13:26:42 — and the 95 s and 150 s screenshots showed the boot's console
text instead of the home screen. Neither is a kernel fault:

- Three frames is what an **idle SpringBoard** draws: one wallpaper, one icon set, then nothing until something
  wakes it. The 13:07 run logged frame 1 and 2 the same way (13:07:51, 13:07:53) and only reached frame 600 in
  the six seconds after the screen blanked and woke again (13:08:55 blank, `pocketTouches` wake 13:09:04) — the
  wake animation is the frame storm, and `uigl150.png` caught its tail.
- The big text over the picture is **U-Boot's USB gadget driver**, not the loader: `lib/efi_loader/efi_qcom_usb.c`
  prints what the host does over fastboot — every command (`host: upload`, `host: oem screenshot`), every
  download and upload with its size and progress (`sending 10125 KiB`, `30% 50% 80%`) — marked `[oosexgang]`,
  the organization's name. (It was read as `osxexgang` for two days, which is why every string search in the
  repository came up empty; `grep -c oosexgang u-boot.bin` says 25.) The loader's own text is quiet:
  `put_char()` gates the panel on `screen_quiet`, and the session log of the 13:56 run — the running loader's
  own `log_buf`, which holds every line regardless of that gate — has no echo lines at all. But the driver
  prints through U-Boot's text console, whose vidconsole redraws the whole text screen per line, and that
  covers what the kernel has on the panel. The 10 MB root upload and the screenshots during a run are the
  lines; a quiet SpringBoard never redraws behind them.
- The switch is an env: `hakios_status=0` in the board's default environment
  (`devices/pclm10/u-boot/files/board/qualcomm/hakios-pclm10.env`) turns the driver's status off — the host
  sees the same in its fastboot output, and the panel stays the system's. Note that `uefi/u-boot/build.sh`
  does **not** copy the device files into the `u-boot/` tree (`apply.sh` does): the first rebuild of
  boot-avb5 built the old env until `cp -r devices/pclm10/u-boot/files/. u-boot/` was run by hand.
  `boot-avb5.img` carries that U-Boot; until it is flashed, screenshots during a run stay unreliable.

Two things to watch once `boot-avb5.img` is in and the panel is quiet:

- SpringBoard blanks the idle screen about two minutes in (`[Display] Set Blanked: YES` at 13:26:43 in the
  13:23 run; the driver's blank path then clears the panel to black). Capture screenshots before ~120 s —
  `shot_retry.sh 60/95/110` — or the run ends on a black panel.
- Whether the first-frame `bzero` reaches the panel has never been seen on screen: the 13:07 home screen came
  from the 12:45 kernel without it (its translucent status bar let old console text show through). Verify with
  an early screenshot after the flash.

## The panel keeps the home screen: kPEDisableScreen, and the logo that came back (Nov 1, afternoon)

The gl5 run (14:33, `logs/last.log`, `returned 290721 0`) settled the open questions above and raised one more:

- The `hakios_status=0` U-Boot was in and worked: nothing new appeared on the panel during the 2.3 GB upload
  (`logs/av5-afterupload.png`), and the `[oosexgang]`大字 are gone from every screenshot since.
- The first frame does reach the panel: `swap 1 (#0): surfaces 3 ... 1125x2436 'BGRA'` at kernel time 169.158 s
  ↔ 14:34:38.6 wall clock (the correlation is `swap 4` at 296.03 s ↔ the `Set Blanked: YES` of 14:36:45.47),
  and the `hakios-gl: frame 1/2/3` lines match. The bzero ran with it.
- And still the 60/95/110 s screenshots (`logs/gl5-60.png` etc.) showed black-backed text, not the home screen:
  **XNU's own video console keeps drawing after the first frame.** `PE_initialize_console(kPEGraphicsMode)`,
  which stood in the first-frame path, only records `gc_graphics_boot = FALSE` — it stops nothing. What draws
  is `vcputc_options()`, gated on `gc_enabled` (`osfmk/console/video_console.c`), and what clears `gc_enabled`
  and `disableConsoleOutput` is `gc_pause(TRUE, …)`, reached through **kPEDisableScreen** — the call IOGraphics
  itself makes when it takes the display (the commented-out line at `IOPlatformExpert.cpp:388`).
- The fix is one word in `MiniIOMobileFramebuffer.cpp`'s first-frame path: `PE_initialize_console(NULL,
  kPEDisableScreen)`. The log survives the silence: what user space writes to `/dev/console` reaches the
  loader's log through `kmoutput()` before the gate (`bsd/dev/arm/km.c`), and the kernel msg buffer is handed
  back at return time (`loader_return.c`); kprintf goes through `PE_kputc` → `console_write_unbuffered` →
  `_cnputc`, which is gated by the same `disableConsoleOutput` (`osfmk/kern/printf.c:843`), and with no UART
  `switch_to_serial_console()` leaves `cons_ops_index` alone (`serial_console_enabled == false`).

The gl6 run (14:55, kernel built 14:52:43, `logs/145504-run.log` → `logs/last.log`, `returned 538785 0`) shows
the result: `kPEDisableScreen 1` in the log right after `swap 1`, and `logs/gl6-105.png` / `logs/gl6-home.png`
are the clean iOS home screen — icons, labels, Dock, Search pill, status bar — with no console text anywhere.
This was reached with no GPU at all: Mesa softpipe through `gl=1`, second kernel in the same loader session.

One thing still draws over the picture between the first frame and the clean shot: the **Apple logo** (gl6-55
and gl6-80, ~31 s past the first frame). It is not text and not the loader — it is `initialize_screen`'s
**kPEReleaseScreen** case, `vc_progress_set(FALSE, 0)`, which draws the boot logo when a user-space process
opens its display session and releases the screen through `IOPlatformExpert::consoleInitialize`. The guard is
in `pexpert/arm/pe_init.c` (now part of the `xnu/files` overlay): a `static int screen_disabled` set by the
kPEDisableScreen case, and the kPEReleaseScreen case reduced to `initialize_screen(info, op)` when it is set.
Verify with the next GL run: after the first frame the panel should stay the home screen at every capture.

Two session notes from the same afternoon. The **Haven virtual watchdog is not available** on this phone: every
SMC `watchdog:` call returns `ffffffffffffffff`, so `wdt_armed` is 0 and the rescue story stays what it was —
`bootcpu=7` keeps the loader on core 0 with USB alive, and a hung kernel still gives up its log. And the
**battery drains during a run**: there is no charging driver in the kernel, so plugged into the host the phone
merely discharges faster than it charges; touch and buttons do nothing by design (no drivers). Let it power off
and charge with the stock PMIC path before the next session. (Nov 2: the buttons sentence is out of date — see
the last section.)

## The keys work: power on PON, both volume keys on the PM8150's GPIOs (Nov 2)

All three keys are driven by `devices/pclm10/drivers/power/hakios.pon.c` (`pon=1`) and were verified on the
phone, each step said in the log before it is made.

**Power — the PON block, first press verified at 09:16** (`logs/091641-run.log`): the block is slave 0
`0x800`, the older single kind answering type `0x01` subtype `0x04`, whose KPDPWR is bit 0 of the real-time
status at `+0x10` — not the split PMK8350's that `drivers/input/hakios.buttons.c` knows. The pull-ups and the
1953 µs debounce are already enabled (the firmware reads the same keys to choose fastboot), so the driver only
reads. `pon: power PRESSED … released … after 3085 ms`, and on the panel the screen went blank and back
through backboardd (`[Button] Lock page`, `[Display] Set Blanked`). A later heartbeat run counted 107 presses.

**Volume — not RESIN, and the phone proved it** (`logs/093440-run.log`): that heartbeat read the PON status
every 16 ms while both keys were pressed over and over — KPDPWR moved 107 times, RESIN not once. The dtbo
(`dtbo.bin.01.dts`, lines 6503–6521) says where they are instead: a `gpio_keys` node whose `vol_up` is
`gpios = <&pinctrl 6 0x01>` and `vol_down` `<&pinctrl 7 0x01>`, both active low, on the PM8150's GPIO
controller at `0xc000`. That the tree's numbers are the datasheet's (one based) is proven by the controller's
own `interrupt-names` (`0xc5` is "pm8150_gpio6") together with `qcom,gpios-disallowed = <0x02 0x05 0x08>`
naming exactly the three blocks with no interrupt line. So `0xc500` is volume up and `0xc600` is volume down;
`0xc700`, the disallowed block next to them, is watched as a control against an off-by-one — and a press did
land where the driver said it would, so the numbering stands.

**Reaching them** (`logs/095211-run.log`): the arbiter's own inventory answers `0xc500` on channel 260 and
`0xc600` on channel 261, both owned by system 0 (writable for us); `0xc700` is channel 52, owned by system 4,
so it is read but never written. Both key blocks answer type `0x10` subtype `0x11` and are already inputs with
the 30 µA pull-up and switched on — nothing to configure, only to read.

**Two bugs, both found by the phone and both said in its logs.** The level of a PMIC GPIO is in the
real-time status register at `base+0x10`, bit 0 — `pinctrl-spmi-gpio.c` and mainline and `hakios.buttons.c`
all agree — and the driver had read register `0x01`, which reads as zero forever: both pins said `level LOW`,
which under an active-low key means HELD forever (`logs/095211-run.log`). Fixed, the pins said `level high
(released)` and the heartbeats caught the presses at exactly the seconds they were made (`down HELD` at 86.7 s,
`up HELD` at 175.9 s, `logs/095929-run.log`) — and still no event came, because the driver carried two
`usable` flags: the setup set the outer one, `button_look` checked the inner one. One flag now.

**End to end** (`logs/100547-run.log`): five volume-down presses and six volume-up presses, every one logged
as `PRESSED`/`released … after N ms` with the kernel's clock, and backboardd received the iOS codes —
`[Button] VolumeDecrement page:0xC usage:0xEA` and `VolumeIncrement … usage:0xE9`, matching to the
millisecond. What the panel does not show is the volume HUD: SpringBoard draws it together with the sound
server, which is not ported yet; the event path is complete as far as it can be. Closed out: `pon=1` in
`CONFIG_BOOTARGS`, `status buttons works` in `device.conf`, the device page regenerated.

## November 2 — the battery gauge, and the two gates on the serial engine

**The pack's gauge is not the PMIC's.** The phone's overlay keeps the PM8150B's gen4 fuel gauge
(qpnp,fg) at status = 

## November 2 — the battery gauge, and the two gates on the serial engine

**The pack's gauge is not the PMIC's.** The phone's overlay keeps the PM8150B's gen4 fuel gauge
(`qpnp,fg`) at `status = "disable"` and puts OPPO's own part on a bus of its own. The overlay says
`qupv3_se15_i2c`, but the phone's own **merged** device tree — the Android base DT appended to
`prebuilt/Image-dtb` (fetched from the TWRP device tree repository, `prebuilt/Image-dtb`) — is what
the phone's kernel actually used, and it puts `bq27541-battery@55` (`compatible =
"oppo,bq27541-battery"`) under **`i2c@a80000`, serial engine 8**, next to the short protection at
0x58 and the VOOC controller at 0x26. That bus's active state names **gpio88 and gpio89**, function
"qup8", in the **north** TLMM tile. Scanning gpio27/28 (the overlay's SE15) was therefore scanning a
bus with nothing on it — the first corrected run is `logs/1429xx` territory.

**The vendor's own hardware sources are in this repository now** (`vendor-src/`, with `INDEX.md`):
the OPPO/Qualcomm kernel for this SoC (`github.com/oppo-source/android_kernel_oppo_sm8150`) and the
OPlus touch drivers, fetched once so that every register and power sequence can be checked against
the phone's own code. The gauge's command map above, the touch's power-up order, the RPMh regulator
vote format and the TLMM pull encoding all come from there.

**What the battery shows now** (`logs/111821-run.log`, `logs/112238-run.log`): the SMB5 driver
reports the pack voltage through a resting Li-ion curve when the pack gauge has not answered
(`smb5: the pack gauge has not answered: 99% is a resting-voltage estimate`), instead of zero — the
indicator is no longer the low-battery state. The real charge still has to come from the gauge.

**The gauge does not answer yet.** `devices/pclm10/drivers/power/hakios.bq27541.c` (`bq27541=1`)
bit-bangs that bus on gpio27/28 at about 40 kHz. The pads are right — driven low they read 0,
released they read 1 — but a scan of every address 0x08–0x77 finds nothing, neither the gauge nor
the VOOC controller, the short protection or the divider. The next run adds the three checks that
separate "wrong pins" from "unpowered bus": gpio27/28 read in all four TLMM tiles, a read of the
lines with our own pull-up disabled (do the board's pull-ups hold them?), and a control scan of the
MP2650's bus on gpio114/115, in the south tile this kernel has already used.

**Touch: the engine's clock root, and the FIFO interface, are two different gates**
(`logs/110134-run.log`). That run had already shown the command being accepted (GO set, status
0x41) with the bus never moving; the two pieces that followed:

*The root.* `clk-rcg2.c` reads bit 31 of the rcg's command register (0x1e148 here) as `CMD_ROOT_OFF`
and calls the clock enabled only when that bit is clear. Mainline's `clk_rcg2_ops` has no enable
callback — an rcg is "always enabled" as far as the framework is concerned — while the vendor's 4.19
ops carry `.enable = clk_rcg2_enable`, which writes `CMD_ROOT_EN` (bit 1) and waits for ROOT_OFF.
This phone's boot firmware left the root off: `touch: gcc: the engine clock's root was off (cmd
0x80000000): asked for it, now 0x2`. `clocks_on()` now makes that write, and the configuration then
takes (`cfg` reads 0: source 0, divider 1, hardware control cleared) along with the branch votes.
The engine accepted the command and took the bytes out of the transmit FIFO — and still did not move
the bus, which is the second gate.

*The FIFO interface.* `GENI_IF_DISABLE_RO` (0x64), bit 0, is set on this engine and does not clear.
Mainline's `i2c-qcom-geni.c` reads exactly that as "FIFO is disabled, so we can only use GPI DMA"
and never tries the FIFO path when it is set; the vendor's 4.19 i2c driver uses GPI DMA on these
buses as well (every i2c node in the phone's tree carries `dmas`). On this SoC the QUPv3 I2C serial
engines are GPI-type and the FIFO path is not available — so the SE register work above (protocol
I2C, FIFO mode bit, packing, 400 kHz SCL counters, watermarks, interrupt routing) is all verified
correct, and the next move for both the touch and the gauge is bit-banged I2C on the pins the tree
gives each bus, which is what the gauge driver already does.

**The wrapper's per-engine state registers** (`QUP_SE_STATE(n)`, wrapper + 0x80c + 4n) read 0 and do
not accept writes on this phone's wrapper (hardware version 1.3.0), and the 4.19 vendor driver never
touches them; they belong to newer QUPv3 revisions. The wrapper's clock-control register at +0x8
likewise reads 0 and ignores writes; the engine's clock is the GCC rcg/branch that `clocks_on()`
now drives correctly.

**The touch chip's own two lines, from the overlay** (`dtbo.bin.01.dts`, fragment@63): `reset-gpio =
<&tlmm 0x36 0x01>` — gpio54, flag 0x01 = active low, so **high is out of reset** — and
`enable1v8_gpio = <&pm8150l_gpio 0x05 0x01>`, the PM8150L's GPIO5. Until this round the reset line
had the boot state's pull-down (`cfg 0x1`) and was never driven, i.e. the chip was held in reset the
whole time; it is now driven high (cfg 0x242, io 0x3). The enable's flag is where the overlay lies:
trusting "active low" and driving the pin low kept the bus clamped. The hardware was asked directly
instead — the sweep below — and the PM8150L's GPIO5 driven **HIGH** is what makes the bus come up
(`logs/144224-run.log`: `** slave 4 pin 5 driven HIGH is the enable: the bus reads high **`). The
driver now drives that pin high and leaves it there; the sweep itself stays available behind
`touch_sweep=1`.

**The touch chip's 2.8 V is an RPMh regulator.** `pm8150_l17` is not an AP-writable SPMI LDO: it is
the RPMh VRM resource `ldoa17` (the boot loader's command database has the entry at `0x0ca8`, its
address `0x00041300`), and the vendor's touch driver sets it to 3.0 V and enables it at probe. The
port's step 5 now sends the same three registers the vendor's `rpmh-regulator.c` sends through a TCS
command (base+0 voltage in mV, base+4 enable, base+8 mode = PMIC5 LDO HPM). The first attempt ran
before the controller's windows were mapped and took a data abort at 0x18 (`logs/134555-run.log`,
`x0 = 0x41300`); the mapping is now shared by every vote.

**The bus clamp is resolved.** With the chip out of reset and the 1.8 V asserted, the bit-banged
scan (`logs/132404-run.log`) still read `scl 0 sda 0` on gpio55/56 with a real pull-up (cfg 0x43,
the TLMM's pull-up is 3 — the first version used 2, which is the keeper, and a keeper holds the line
at whatever the serial engine last left it at). Driving the pads high by hand read `scl 1 sda 1`, so
the bus was not shorted: the lines were clamped weakly, which is what an unpowered chip's I/O diodes
look like. That was the right diagnosis, and the sweep answered the question it pointed at: every
GPIO of the three PMIC GPIO controllers was driven, low first then high, and the two bus wires read
back after each (`logs/144224-run.log`). The PM8150L's GPIO5 driven **high** made both wires read
high — `** slave 4 pin 5 driven HIGH is the enable: the bus reads high **`, and with the enables
driven `gpio55/gpio56 read 1/1`. The overlay's active-low flag for that pin is simply wrong, and the
driver now asserts it high by default. What is still unproven is whether the chip itself answers at
0x20: the product-id read is the next run.

**The gauge's register map came from OPPO's own source.** The vendor kernel is public
(`github.com/oppo-source/android_kernel_oppo_sm8150`, branch `oppo/sm8150_s_12.1_oppo_reno10`);
`drivers/power/oppo/gauge_ic/oplus_bq27541.h` gives the part's standard commands: 0x2C
StateOfCharge, 0x08 voltage, 0x14 average current, 0x06 temperature (0x0A is the flags register and
0x0C the nominal capacity — the driver's first version had those two wrong). The same tree has the
touch drivers (`drivers/input/touchscreen/synaptics_*`) and the RPMh regulator driver that carries
`pm8150_l17` (`qcom,resource-name = "ldoa17"`, init 2.856 V, `qcom,set = <3>` — an RPMh VRM vote,
applied at boot, so the 2.8 V may well be on already; the 1.8 V load switch behind the PM8150L's
GPIO5 is the other half).

**The gauge's bus has pull-ups but no acknowledged slave.** On the corrected bus — gpio88/89, the
north tile, `i2c@a80000` (SE8), see above — `logs/144224-run.log` shows the board's own pull-ups
holding both lines high without ours, our pads driving and reading correctly, and a full address scan
at 40 kHz (slower than the bus ever ran in Android) finding **no device at any address**, 0x55
included. The bit-banging and the pins are right; nothing on that bus is answering. The vendor's
gauge driver (`oplus_bq27541.c`) offers no explanation for a silent bus: it has no wake sequence, no
bus recovery, and its very first traffic is a write it expects to be acknowledged —
`bq27541_hw_config()` writes control subcommand 0x0000 to 0x00 and reads it back, then 0x0001
(device type: `0xFFA5` is a BQ28Z610, `.h:175`), and for a plain BQ27541 writes **0x0021, Enable
Gas Gauging** (`.c:2071`). That write matters for us even once the chip answers: without it the SOC
and current registers can hold stale values. The one variant difference in the whole read table is
the BQ28Z610's average current at **0x0C** instead of 0x14 (`.c:2079`); SOC, voltage and temperature
are the same commands we already use. The MP2650 on the charge bus (gpio114/115) is silent too, so
the most likely explanation is a charging-subsystem supply that is off until something enables it —
the same shape as the touch's 1.8 V, which turned out to hide behind an active-high PMIC GPIO the
overlay had labelled active-low.

## November 3 — a cold reset from the loader comes back to the loader (and the way home)

After the R9 run, `fastboot reboot` was meant to bring ColorOS up so `pull-firmware.sh` could take
the GPU's files over adb. Seven minutes of `adb wait-for-device` later, `fastboot devices` answered
`xnuloader`: `getvar product` is `xnuloader` and `oem log` answers OKAY — the phone was back in the
loader, not in its stock fastboot and not in Android.

The reason is the one thing this device does differently from the reference phone. There the loader
runs on top of ABL's fastboot (started with `fastboot boot`), so a cold reset falls through ABL and
on to Android. Here the loader runs on top of the U-Boot **flashed to `boot_a`**, and a cold reset
is just a restart: ABL → boot_a → U-Boot → `hakios load` (the image's own HKUB ramdisk) → `hakios
boot` → the loader waits in fastboot again. `xl`'s "the phone is in this loader: sending it on to
Android" (its reboot and stop paths) assumes the reference phone's shape; on this one it re-enters
the loader. And the loader's fastboot is not a flashable one: its whole command set is getvar,
download, upload, the oem verbs and reboot (`xnuloader/loader.c`, `fb_command`), and `flash:` is
answered `unknown command`. So "one command from home" needs a hand at the phone for its first step.

Three ways home, each one key press deep: ABL's fastboot (the key trick), U-Boot's bootmenu (hold
Volume Down while U-Boot starts — entry 3, "Enable fastboot mode"), or EDL. The images are ready:
`bk/boot.bin` (the stock boot, read over EDL on 31 Oct) and `boot-avb5.img` (the current U-Boot
image), both in the working directory outside the repository. The short order: reboot from the
loader, hold Volume Down into the bootmenu, fastboot mode, `fastboot flash boot bk/boot.bin`,
`fastboot reboot` — ColorOS. adb has never been confirmed with this PC: if it says `unauthorized`,
tap Allow; if there is no adb interface at all, the USB function came up as MIDI — replug with the
screen unlocked (the note above the EDL order).

What the pull is for, in the order of what it buys: `fw-gpu` (the Adreno 640's gmu/sqe/zap — the
one order of magnitude left for the UI, notes/cpu.md R6/R8/R9), the ADSP's files (the WCN3990's
firmware runs on it: WLAN M1, and audio), and the modem's. One thing to settle once the files are
in hand: the GPU driver asks its small image for `a660_gmu.bin`, `a660_sqe.fw`, `a660_zap.mdt` and
`a660_zap.b02` by name (`drivers/gpu/hakios.adreno.c`, `image_file`), while CAF targets of this
SoC name the same GPU's files `a630_*`. The pull takes whatever names the phone really has; the
driver then needs one small change to ask for those — which names, decided by the pull, not by a
guess. Until the files are in, the GPU round cannot be prepared further from this side.
