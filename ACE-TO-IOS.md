# ace-to-ios — HakiOS on the OPPO Reno Ace (PCLM10)

Source-only snapshot of the HakiOS port of Apple's open-source Darwin kernel (XNU) to the OPPO Reno Ace
(PCLM10, Qualcomm SM8150), 2026-11-05. This is a research port, not a ROM and not an iPhone system.

## What is here

| Path | What |
|---|---|
| `xnu/` | the port's changes to Apple's open-source XNU (APSL 2.0 patch and files) — no Apple source tree is included |
| `drivers/` | the port's drivers (display, GPU, input, power, thermal, storage, ...) |
| `configs/` | the configuration system and its generated catalogues |
| `xnuloader/` | the boot loader that starts the kernel from RAM |
| `devices/pclm10/` | the Reno Ace device repository: defconfig, kernel drivers, notes, device.conf |
| `tools/` | build and device tools |
| `docs/` | the port's documentation |
| `userland/` | the port's own user-space sources (no Apple userland) |

## What is deliberately NOT here

- **No Apple firmware or iOS/Darwin user space**: no IPSW, no `system.apfs`, no cryptex or dyld cache, no
  Apple binaries of any kind. The Darwin 16.5 user space is installed by the user from their own IPSW.
- **No vendor firmware**: no modem/ADSP/SLPI/Wi-Fi/Bluetooth/camera firmware pulled from the phone.
- **No prebuilt binaries or images**: no kernel binary, loader binary, disk images or logs.

## Build and run

The full instructions are in `docs/` (getting started, QEMU, on the phone, porting). The device-specific
kernel recipe is `make pclm10_defconfig` followed by `make kernel`; the small root image is
`cd userland && ./build.sh`. Running on the phone needs the user's own copy of the Darwin user space and
the phone's own firmware; neither is distributed here.

## Status

A source snapshot of work in progress. The port boots to the Darwin user space in RAM on the phone;
not every subsystem is finished. The device notes under `devices/pclm10/notes/` record what is verified
and what is not. Nothing here is a claim of daily usability or of any affiliation with Apple.
