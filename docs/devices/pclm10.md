<!-- Made by tools/docs-gen.py from devices/pclm10/device.conf. Do not edit: change that, then run tools/docs-gen.py. -->


# OPPO Reno Ace (`pclm10`)

| | |
|---|---|
| Vendor | OPPO |
| SoC | `sm8150` |
| Configuration | `make pclm10_defconfig` |
| Maintainer | renny |
| Repository | [`devices/pclm10/`](https://github.com/oosexgang/hakios-xnu-kernel/tree/main/devices/pclm10) |
| Firmware | `devices/pclm10/pull-firmware.sh` pulls it from your own phone |

## What works

| Part | State | |
|---|---|---|
| display | partly | SpringBoard home/lock screen has reached the panel in RAM-boot tests (logs/130728-run.log); panel brightness, sleep/wake and cold-boot delivery are not fully verified. This is beyond a framebuffer probe, not a finished display stack. See notes/功能与交付状态.md. |
| touch | partly | Regression 03 logs/111915-run.log validates gauge diagnostics skipping live touch pins; 48 downs/528 moves/48 ups, zero read failures, and backboardd receives contacts. User still reports slow/lagging interaction: acceptance NOT passed. GPU/SMMU fault is present concurrently; do not equate event delivery with correct latency or assert sole cause. Multi-touch and cold-boot delivery remain unverified. |
| buttons | partly | Power/volume events reach the kernel and iOS volume usages reach backboardd with pon=1 (logs/100547-run.log); pon=1 is in source defaults. Reliable interaction outside hosted tests and across display sleep/wake/cold reboot is not delivered or verified. Event delivery is not proof of working audio. |
| cpu | works | Seven kernel CPUs are verified; core 0 remains with the loader. Three small cores join after the default 28-second delay (logs/083450-run.log:9482). EPSS frequency control and memory vote have been tested (logs/013949-run.log, logs/015951-run.log); recent thermal-controlled radio tests explicitly use cpufreq=0. Does not establish finished power management. |
| gpu | partly | Visual/interaction acceptance NOT passed. Regression 04 actually selects FD640 with corrected A640 ID/GMEM/layout; 500 ms unfinished frame remains, but queue completes before shutdown. SMMU 0x1600 first read during teardown is not proof of a runtime fault cause. Regression 03 previously stalled at fence3490/3495 with wrong A635 ID; 7c+ Gen3 is NOT an A640 alias. Candidate05 removes per-submit debug logging/readback only; built, not run. See logs/interactive-acceptance-04.md and -05.md; no cold-boot delivery. |
| storage | partly | Loader-backed UFS reads and RAM-root boot work. Writable persistent /var and reboot persistence are not validated or enabled; no new partition flashing or flash-root authorization. Test-run state can be lost at reboot. |
| battery | partly | SMB5/PMIC voltage and charger-register reads work with smb5=1 (logs/092331-run.log). BQ27541 does not acknowledge; UI percent is a voltage estimate, not a calibrated gauge. Charging control, unplugged use, endurance and VOOC are not delivered as reliable daily features. |
| haptic | partly | Actual AW8697 motor was identified and physically confirmed buzzing; aw8697=3 stops it (logs/195855-run.log, logs/200040-run.log). Boot-argument test only; no verified iOS haptic service or UI feedback integration. PM8150B HAPC is not the motor used here. |
| torch | partly | Physical flashlight confirmed when all three PM8150L flash channels are driven (logs/191722-run.log, logs/205931-run.log). flash=3/4 are test on/off controls; no verified Control Center flashlight integration. Must not default to leaving the lamp on. |
| bluetooth | partly | WCN3990 identification, patch/NVM download and matched HCI Reset verified at 115200 (logs/205327-run.log). No verified scan, pairing or iOS Bluetooth service; not usable for headphones or peripherals. |
| camera | partly | All five camera sensors identify over CCI after power sequencing (logs/183829-run.log). No validated sensor-to-frame capture pipeline, preview, photo or video; not a functioning Camera app. |
| wifi | partly | SLPI image authentication/start, shared-memory items and return interrupts are verified; DT SPI indices 170/172 use absolute GIC IDs 202/204 (logs/093136-run.log). No IPCRTR/QMI/association/network access. M21 observes watchdog INTID 526 by 5053 ms with zero host GLINK messages and host head/tail 0 (logs/102311-run.log:1339-1340); host GLINK traffic is not necessary for that event. Earlier wrong-ID/bidirectional-dead diagnoses are withdrawn; notes/wifi.md and notes/slpi-experiment-index.md retain evidence. |
| network | partly | BSD TCP loopback on this phone returned its three bytes (logs/001019-run.log:1177). This proves local stack operation, not Wi-Fi, Internet, DNS or mobile data. |
| thermal | partly | All 28 TSENS sensors read in recent tests; 60 C stop logic is configured, but not claimed as a physically triggered/validated protection event. Recent M17-M21 peaks 47.5-48.2 C. sleepd/chronod integration removed a lookup storm (logs/022040-run.log); absolute accuracy, sustained idle, suspend and battery safety remain unverified. |
| usb | partly | Loader USB uploads/log retrieval work; this is not a validated iOS USB data, ADB, MTP or accessory stack. The current device can remain in the loader after a timed hosted run, where iOS touch/buttons are not running. |
| audio | no | Speaker/microphone playback/recording are not verified for this device and their drivers are not enabled. Volume key events alone do not establish audio output. |
| cellular | no | No verified SIM registration, calls, SMS or mobile data; modem/QMI drivers are not enabled for normal use. |
| sensors | no | No verified user-facing auto-rotation, proximity, ambient-light or fingerprint/NFC integration. Presence in vendor hardware descriptions is not implementation. |

The state is what has been seen on this device, written down in `devices/pclm10/device.conf`; a part the table does not name has not been looked at.
