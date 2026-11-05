<!-- Made by tools/docs-gen.py from drivers/*/conf and the SoC and device repositories' drivers. Do not edit: change that, then run tools/docs-gen.py. -->


# Drivers

Every driver the kernel can be built with. `CONFIG_HAKIOS_<NAME>=y` in a defconfig builds it in; [Drivers in the configuration](../drivers-config.md) says how, and [Writing a driver](../porting/drivers.md) how to add one.

## Always built

| File | What |
|---|---|
| `hakios.audio.c` | /dev/hakios-audio, the sound Darwin plays, to whatever plays it; and where the camera, flashlight, motor and speaker drivers are started from |
| `hakios.version.c` | kern.hakios.version, and the version in the boot log |
| `hakios.platform.c` | what the drivers share (the locks of the PMIC arbiter, the always-on processor, RPMh and the SMMU), and kern.hakios.drivers, the list of the drivers built in |
| `hakios.stubs.c` | what the rest of the kernel calls in a driver that is left out: nothing, and "not there" |
| `hakios.stubs.cpp` | the same, for the calls that are C++ (the display asking whether the GPU is done with a surface) (with XNU's `iokitcpp`) |
| `hakios.input.c` | /dev/hakios-touch, the queue of touches and buttons, and where the phone's drivers are started from |
| `hakios.net.c` | en0, an interface that is Ethernet to the rest of the kernel, for whatever link carries its frames |
| `hakios.image.c` | a file of one of the RAM disks by path, for the drivers that load firmware before there is a root |
| `hakios.sparse.c` | /dev/rsp0, a block device over an image that holds only some of its blocks (apfs_sparse=) |
| `hakios.apfs_check.c` | a walk of the APFS volume on /dev/rvblk0 through VFS, to compare with tools/apfs/apfs.py (with XNU's `apfs`) |

## audio

| Driver | Files | Requires | Boot arguments | pclm10 | qemu | redwood | |
|---|---|---|---|---|---|---|
| **`HAKIOS_ADSP`**<br>the audio DSP started and linked (adsp=1, adsp_step=N); the battery's state comes through it too. Needs the DSP's firmware from the phone (fw-audio/) | `hakios.adsp.c` |  | `adsp=`, `adsp_again=`, `adsp_apr_wait=`, `adsp_keep=`, `adsp_listen=`, `adsp_new=`, `adsp_pause=`, `adsp_return=` and 4 more | built |  | built | |
| **`HAKIOS_SPEAKER`**<br>the two speaker amplifiers on I2C, and their watch of temperature and battery | `hakios.speaker.c` |  | `speaker_probe=` |  |  | built | |
| **`HAKIOS_SOUND`**<br>the speakers playing (sound=N): what the DSP's services are told, the feeder, and the speakers' guard that limits what the amplifiers get | `hakios.sound.c`<br>`hakios.guard.c` | `HAKIOS_ADSP`, `HAKIOS_SPEAKER` | `sound=`, `sound_below=`, `sound_gain=`, `sound_guard_as=`, `sound_pause=`, `sound_secs=` |  |  | built | |
| **`HAKIOS_MIC`**<br>the microphones, through the audio DSP (mic=N) | `hakios.mic.c` | `HAKIOS_ADSP`, `HAKIOS_SOUND` | `mic=`, `mic_adc=`, `mic_input=`, `mic_main=`, `mic_secs=`, `mic_tap=`, `mic_test=` |  |  | built | |
| **`HAKIOS_VIRTIO_SOUND`**<br>QEMU's virtio sound device as the player of /dev/hakios-audio (virtio_sound=1). QEMU only | `hakios.virtio_sound.c` |  | `virtio_sound=` | built | built | built | |

## bluetooth

| Driver | Files | Requires | Boot arguments | pclm10 | qemu | redwood | |
|---|---|---|---|---|---|---|
| **`HAKIOS_BT`**<br>the Bluetooth half of the WCN6750: step one, a read-only look at its three pins (bt=1) | `hakios.bt.c` |  | `bt=`, `bt_again=` |  |  | built | |

## camera

| Driver | Files | Requires | Boot arguments | pclm10 | qemu | redwood | |
|---|---|---|---|---|---|---|
| **`HAKIOS_CAMERA`**<br>the cameras (cam=N): the sensors on the CCI bus and the ISP's interface. The sensors, their tables (fw-camera/) and the pins are the POCO X5 Pro's | `hakios.camera.c` |  | `cam_after=`, `cam_fake=`, `cam_fake_fps=`, `cam_frames=`, `cam_gain=`, `cam_keep_mb=`, `cam_mode=`, `cam_pattern=` and 4 more |  |  | built | |
| **`HAKIOS_TORCH`**<br>the flashlight: the flash LED module of the PM8350C (torch=1); it stays off while the battery is too warm | `hakios.torch.c` |  | `torch_max_s=`, `torch_secs=` |  |  | built | |

## core

| Driver | Files | Requires | Boot arguments | pclm10 | qemu | redwood | |
|---|---|---|---|---|---|---|
| **`HAKIOS_SOCINFO`**<br>the chip's identity from the firmware's shared memory: kern.hakios.soc_* (socinfo=1). Any Qualcomm SoC that keeps SMEM item 137; where the shared memory is comes from soc_smem= | `hakios.socinfo.c` |  | `soc_smem=`, `soc_smem_size=`, `socinfo=` |  |  | built | |

## gpu

| Driver | Files | Requires | Boot arguments | pclm10 | qemu | redwood | |
|---|---|---|---|---|---|---|
| **`HAKIOS_ADRENO`**<br>the Adreno 642L of the SC7280 woken step by step (adreno=N), and HakiosGPU, the IOKit service Mesa, Metal and the window server draw through (gpu=1, adreno_keep=1; docs/reference/metal.md). Needs the GPU's firmware from the phone (fw-gpu/) | `hakios.adreno.c`<br>`hakios.gpu.cpp` |  | `adreno=`, `adreno_aperture_cb=`, `adreno_blind=`, `adreno_clear_oob_probe=`, `adreno_core_cb=`, `adreno_cp_aperture=`, `adreno_cp_phys=`, `adreno_delay_first=` and 49 more | built |  | built | |

## haptic

| Driver | Files | Requires | Boot arguments | pclm10 | qemu | redwood | |
|---|---|---|---|---|---|---|
| **`HAKIOS_HAPTIC`**<br>the vibration motor: an Awinic AW86927 driving a linear resonant actuator (haptic=N), its waveforms from fw-haptic/ | `hakios.haptic.c` |  | `haptic=`, `haptic_effect=` |  |  | built | |

## input

| Driver | Files | Requires | Boot arguments | pclm10 | qemu | redwood | |
|---|---|---|---|---|---|---|
| **`HAKIOS_VIRTIO_INPUT`**<br>QEMU's virtio tablet and keyboard as the touch screen and the buttons (virtio_input=1). QEMU only | `hakios.virtio_input.c` |  | `virtio_input=` | built | built | built | |
| **`HAKIOS_GOODIX`**<br>the touch screen: a Goodix GT9916 ("Berlin-D") or a FocalTech FT3680 on SPI (touchscreen=1). The bus, pins and supplies are the POCO X5 Pro's; another phone with one of these controllers changes those | `hakios.goodix.c` |  | `touch_flip=`, `touch_poll_us=`, `touch_rate=`, `touch_spi_hz=`, `touchscreen=` |  |  | built | |
| **`HAKIOS_BUTTONS`**<br>the power and volume keys, read from the PMIC through the SPMI arbiter (buttons=1) | `hakios.buttons.c` |  | `buttons=`, `buttons_steps=`, `press_power=`, `press_power_ms=`, `touchscreen=` |  |  | built | |

## modem

| Driver | Files | Requires | Boot arguments | pclm10 | qemu | redwood | |
|---|---|---|---|---|---|---|
| **`HAKIOS_QMI`**<br>QMI messages over the IPC router: how the Wi-Fi firmware's service and the modem's are spoken to | `hakios.qmi.c` |  |  |  |  | built | |
| **`HAKIOS_MODEM`**<br>the modem (modem=1): started, its file systems served from the flash, the IPA's data path as en1, the SIM and the network registration. Research; needs fw-modem/ from the phone | `hakios.mpss.c`<br>`hakios.cell.c`<br>`hakios.nas.c`<br>`hakios.ipa.c` | `HAKIOS_QMI`, `HAKIOS_UFS` | `modem=`, `modem_again=`, `modem_apn=`, `modem_buffer=`, `modem_data_nobind=`, `modem_data_off=`, `modem_data_wait=`, `modem_dpm_off=` and 45 more |  |  | built | |

## net

| Driver | Files | Requires | Boot arguments | pclm10 | qemu | redwood | |
|---|---|---|---|---|---|---|
| **`HAKIOS_VIRTIO_NET`**<br>QEMU's virtio network device as the link behind en0 (virtio_net=1). QEMU only | `hakios.virtio_net.c` |  | `virtio_net=` | built | built | built | |

## power

| Driver | Files | Requires | Boot arguments | pclm10 | qemu | redwood | |
|---|---|---|---|---|---|---|
| **`HAKIOS_BATTERY`**<br>the battery: kern.hakios.battery_*, and HakiosBattery, the IOPMPowerSource Darwin reads the charge from (the state comes from the audio DSP's battery manager, with adsp=1) | `hakios.battery.c`<br>`hakios.battery.cpp` |  | `battery_shared_pa=` | built |  | built | |
| **`HAKIOS_CPUFREQ`**<br>the clock of the cores the kernel runs on (cpufreq=1), and the memory bus's votes (ddr=) | `hakios.cpufreq.c` |  | `cmdb=`, `cpufreq=`, `cpufreq_fix=`, `cpufreq_look=`, `cpuload=`, `ddr=`, `ddr_rest=`, `ddr_vote=` and 5 more | built |  | built | |
| **`HAKIOS_THERMAL`**<br>the SoC's temperature sensors (thermal=1): kern.hakios.thermal_*, and the run handed back to the loader when the phone gets too hot (thermal_stop=) | `hakios.thermal.c` |  | `thermal=`, `thermal_log=`, `thermal_pclm10=`, `thermal_poll=`, `thermal_stop=`, `thermal_tm0=`, `thermal_tm1=` | built |  | built | |
| **`HAKIOS_PMIC`**<br>the blocks of the power-management chips the SPMI arbiter shows, and the PMIC's clock (pmic=1); read only | `hakios.pmic.c` |  | `pmic=`, `pmic_list=` | built |  | built | |
| **`HAKIOS_BATTGUARD`**<br>the run handed back to the loader before the battery is flat (battery_stop=<percent>) | `hakios.battguard.c` |  | `battery_stop=`, `battery_stop_poll=` |  |  | built | |

## storage

| Driver | Files | Requires | Boot arguments | pclm10 | qemu | redwood | |
|---|---|---|---|---|---|---|
| **`HAKIOS_UFS`**<br>the phone's UFS flash: a block device for each GPT partition (ufs=1, ufs=2), and the UFS host controller driven from the kernel (ufs_native=). THE ONLY CODE OF THE KERNEL THAT MAY WRITE TO THE PHONE (ufs_write=1, the partition `darwin` only): see tools/provenance.allow | `hakios.ufs.c`<br>`hakios.ufshc.c` |  | `ufs_bench=`, `ufs_check=`, `ufs_native_bench=`, `ufs_native_buffer=`, `ufs_native_check=`, `ufs_native_hang=`, `ufs_native_leave=`, `ufs_native_slots=` and 3 more |  |  | built | |
| **`HAKIOS_VIRTIO_BLK`**<br>a disk image of the host as /dev/rvblk0 (virtio_blk=1). QEMU only | `hakios.virtio_blk.c` |  | `virtio_blk=` | built | built | built | |

## wifi

| Driver | Files | Requires | Boot arguments | pclm10 | qemu | redwood | |
|---|---|---|---|---|---|---|
| **`HAKIOS_WIFI`**<br>Wi-Fi of the WCN6750 (wifi=1, wifi_step=N): the DSP that runs the radio's firmware, Linux's ath11k (its object, made by tools/mk-ath11k.sh, is linked in only with this), a station, and the Apple80211 surface Darwin's wifid talks to. Needs fw-wifi/ from the phone | `hakios.wifi.c`<br>`hakios.wlfw.c`<br>`hakios.lx.c`<br>`hakios.sta.c`<br>`hakios.apple80211.c`<br>`hakios.wlan.cpp` | `HAKIOS_QMI` | `wifi=`, `wifi_again=`, `wifi_ask=`, `wifi_cal=`, `wifi_dirty=`, `wifi_file=`, `wifi_fine=`, `wifi_keep=` and 14 more |  |  | built | |

## bluetooth (devices/pclm10)

| Driver | Files | Requires | Boot arguments | pclm10 | qemu | redwood | |
|---|---|---|---|---|---|---|
| **`HAKIOS_WCNBT`**<br>the Bluetooth half of this phone's WCN3990: the three RPMh rails the bt_wcn3990 node asks for (ldoa7 1800 mV, ldoc2 1304 mV, ldoc11 3312 mV), the QUPv3 wrapper 2 engine 3 UART at 0xc8c000 (gpio43-46, 115200, polled GENI FIFO), and the WCN3990 power-on pulse (0xC0 at 2400, 0xFC at 115200). wcnbt=1 votes the rails, configures the port and reads the controller's product/SOC/ROM/patch version; wcnbt=2 also sends HCI Reset. No firmware is loaded at either level. Add CONFIG_HAKIOS_WCNBT=y to devices/pclm10/configs/pclm10.config to build it. | `hakios.wcnbt.c` |  | `wcnbt=` | built |  |  | |

## camera (devices/pclm10)

| Driver | Files | Requires | Boot arguments | pclm10 | qemu | redwood | |
|---|---|---|---|---|---|---|
| **`HAKIOS_FLASH`**<br>the flashlight and the cameras of this phone. flash=1 reads the PM8150L flash module at 0xd300 on SPMI slave 5 through the PMIC arbiter; flash=2 runs the vendor driver's torch on-sequence for LED 0 (12.5 mA step, software strobe, 300 mA, safety timer off, module and channel enable), reads the fault registers while it is lit, and puts everything back; flash=3 leaves the torch on. cam=1 reads only: GCC's camera clocks, the titan top GDSC, the four MCLK roots, the CCI/MCLK pins, the PM8150 GPIO1 rear reset, the PM8150L GPIO1/4 PM8008 enables and both PM8008s' LDOs; cam=2 powers the rear main sensor (CCI 0 master 0, CSIPHY 0, MCLK 0, PM8008 cam=4 all five sensors in turn; cam=5 leaves the main sensor powered. The sensor models are the public ones (IMX586 main, IMX319 ultra-wide, IMX471 front, S5K3M5 tele, a 2 MP OmniVision mono); the chip ID read is what confirms them. | `hakios.flash.c` |  | `flash=` | built | built | built | |

## display (devices/pclm10)

| Driver | Files | Requires | Boot arguments | pclm10 | qemu | redwood | |
|---|---|---|---|---|---|---|
| **`HAKIOS_WLED`**<br>the panel's backlight (wled=1): the PM8150L's WLED module at 0xd800, whose master switch is register 0x46 bit 7. The screen of this port is driven through the boot firmware's framebuffer, so backboardd's blank request reaches no hardware; this module is that hardware. A thread follows kern.hakios.screen_backlight (1 lit, 0 dark) and writes the switch; wled_test=1 adds one blink at startup for bring-up | `hakios.wled.c` |  | `wled=`, `wled_test=` | built | built | built | |

## haptic (devices/pclm10)

| Driver | Files | Requires | Boot arguments | pclm10 | qemu | redwood | |
|---|---|---|---|---|---|---|
| **`HAKIOS_HAPC`**<br>the vibration motor of this phone: the PM8150B's HAPC block at 0xc000 on SPMI slave 3, driven open-loop through the PMIC arbiter. hapc=1 reads its status and configuration; hapc=2 writes this phone's device tree's own recipe (LRA, sine, 3400 mV, 6667 us, direct-drive), plays half a second, reads the busy bits while it runs, and puts everything back; hapc=3 leaves it playing. No firmware and no interrupt: eight writes. This block is not what moves the motor on this phone: the AW8697 below is. | `hakios.hapc.c`<br>`hakios.aw8697.c` |  | `aw8697=`, `hapc=` | built | built | built | |

## power (devices/pclm10)

| Driver | Files | Requires | Boot arguments | pclm10 | qemu | redwood | |
|---|---|---|---|---|---|---|
| **`HAKIOS_SMB5`**<br>the SMB5 charger and the gen4 fuel gauge on this phone's PM8150B, read through the PMIC arbiter (smb5=1): which stage the charge is in, what the USB port found, and the battery's voltage and current, handed to the battery monitor and the power source Darwin reads | `hakios.smb5.c` | `HAKIOS_BATTERY` | `smb5=`, `smb5_seconds=` | built | built | built | |
| **`HAKIOS_PON`**<br>the keys of this phone, read through the PMIC arbiter (pon=1): the power key on the power-on block of its PM8150, the older single kind, subtype 0x04, whose KPDPWR is bit 0 of its status -- not the split PMK8350's that drivers/input/hakios.buttons.c knows -- and both volume keys on that chip's GPIO controller, blocks 0xc500 and 0xc600, which the driver enables as inputs with a pull-up and reads; the block's RESIN input is watched too and says what it sees, but it is not this phone's volume down (logs/093440-run.log) | `hakios.pon.c` |  | `pon=`, `pon_steps=` | built | built | built | |
| **`HAKIOS_BQ27541`**<br>the pack's own gauge (bq27541=1): the BQ27541 the overlay puts at 0x55 on the i2c@0xc94000 bus, read by bit-banging that bus's two pins (gpio27/gpio28, east TLMM tile) -- the QUPv3 serial engine there reports its FIFO interface disabled and its vendor driver uses GPI DMA, so the pads are driven directly. The charge, voltage, current and temperature go to the SMB5 driver's battery report | `hakios.bq27541.c` |  | `bq27541=`, `touch=` | built | built | built | |

## touch (devices/pclm10)

| Driver | Files | Requires | Boot arguments | pclm10 | qemu | redwood | |
|---|---|---|---|---|---|---|
| **`HAKIOS_S3706`**<br>the touch screen of this phone: a Synaptics S3706 at I2C address 0x20 on serial engine 17 of the second QUP wrapper, driven here without a clock, pin, I2C or interrupt framework (touch=1). This is the bring-up: the bus, the pins, the 1.8 V enable and the chip's two product id registers; touch_steps=N stops after step N. Reports and multi-touch come after. | `hakios.s3706.c` |  | `touch=`, `touch_2v8=`, `touch_bb=`, `touch_steps=`, `touch_sweep=` | built | built | built | |

## wifi (devices/pclm10)

| Driver | Files | Requires | Boot arguments | pclm10 | qemu | redwood | |
|---|---|---|---|---|---|---|
| **`HAKIOS_SLPI`**<br>the SLPI of this phone: its rails, its image, and its Q6 block. slpi=1 lists every resource the boot loader's command database holds, by name and address; slpi=2 adds the four rails the icnss node asks for (ldoa1 752 mV, ldoa7 1800 mV, ldoc2 1304 mV, ldoc11 3104 mV, three TCS votes each, the way the touch driver votes ldoa17) and a report on the two ARC resources the ssc node's vdd_cx and vdd_mx are (named, not written); slpi=3 adds M2: on a thread that waits for the root, /fw/slpi.mdt and its segment files are read, handed to TrustZone for their signature, placed in the region the device tree reserves (0x98100000, 20 MiB, the image shifted by 0xE00000 as the vendor's relocating loader does), written and read back, and started through the always-on processor and TrustZone, then the q6 block at 0x5c00000 is read after slpi_wait seconds (3); slpi=4 reads that block even when the start failed; slpi=5 leaves the always-on processor's mailbox alone and asks TrustZone directly, a diagnostic for when the mailbox itself is what stops the run. The q6 read is tied to these levels because the phone answered on 2026-11-03 (logs/095451-run.log): while the slpi is down the first read of that window never comes back and the kernel hangs where it stands -- the block is worth reading only after something has started the subsystem. slpi_base= overrides the region's address. No vote here is ever dropped. | `hakios.slpi.c` |  | `slpi=`, `slpi_base=`, `slpi_wait=`, `soc_smem=` | built | built | built | |

The columns on the right are the devices' defconfigs: "built" means that defconfig builds the driver in.
