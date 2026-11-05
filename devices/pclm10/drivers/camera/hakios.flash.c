/*
 * The camera and the flashlight of the OPPO Reno Ace (PCLM10).
 *
 * The flashlight: the PM8150L's flash LED module.
 *
 * The runtime device tree (vendor-src/dtb/dtbo.bin.01.dts, notes/remaining-hardware.md) names it
 * `qcom,leds@d300` with compatible "qcom,qpnp-flash-led-v2" on SPMI slave 5, status "okay": the
 * same chip the WLED driver already reaches (the arbiter's channel table gives peripheral 0xd3 a
 * PPID of 0x5d3, slave 5). The vendor's driver for this block (drivers/leds/leds-qpnp-flash-v2.c,
 * fetched to vendor-src/leds/) is not built here, and the PM8350C torch driver in drivers/camera/
 * belongs to another phone; so the register recipe is the specification, taken from that driver as
 * collected in notes/remaining-hardware.md.
 *
 * flash=1 reads the module and reports: its type and subtype, the PM8150L's own revid (function
 * 0x0100 of the same slave, the read that settles which chip this is), and the live state: the
 * fault and status registers and the current configuration. flash=2 adds the torch: the vendor
 * driver's own on-sequence for LED 0 (current step 12.5 mA, software strobe, 300 mA target, safety
 * timer off, module enable, channel enable), the fault registers read while it is lit, and then
 * everything put back as it was found. flash=3 leaves the torch on, for an eye to confirm in the
 * morning; flash=4 forces it off after such a round. A lit torch here is independent of the
 * panel's backlight: different module, different registers.
 *
 * The camera: the first bring-up of this phone's rear main camera (cam-sensor@0), added here
 * because this file is already built (CONFIG_HAKIOS_FLASH=y) and the task is one module that owns
 * this phone's camera block. The topology is the phone's own overlay, fragment@42 of
 * vendor-src/dtb/dtbo.bin.01.dts (cam-sensor@0..4, eeprom@0..3, actuator@0/1/3, ois@0/1): the main
 * sensor is CCI 0 master 0 (TLMM gpio17/18), CSIPHY 0, MCLK 0 on gpio13 at 19.2 MHz, reset is the
 * PM8150's GPIO1, and its rails are the second PM8008's LDO7 2.9 V (VANA), LDO1 1.1 V (VDIG),
 * LDO3 2.8 V (VAF) and LDO4 1.8 V (V_CUSTOM1); that PM8008 answers at I2C 0x04/0x05 on serial
 * engine 4 (TLMM gpio51/52) and is enabled by the PM8150L's GPIO4. The other four sensors' own
 * numbers are in the table below (sensor 2 is the front one, CCI 1 master 1, CSIPHY 2, MCLK 2 at
 * the tree's 24 MHz). The base tree supplies the controllers: CSIPHY v1.1 at 0xac65000 + 0x1000*n,
 * CCI at 0xac4a000/0xac4b000, CSID175/IFE175 and csid-lite175/vfe-lite175 (android-merged.dts
 * 1409-1490, 1493-1655, 1976-2135); CAMCC is 0xad00000, the titan top GDSC at 0xad0c1bc, GCC's
 * camera AHB and XO at 0xb008/0xb044, and the four MCLK roots at 0x5004/0x5024/0x5044/0x5064 with
 * their branches at 0x501c/0x503c/0x505c/0x507c (vendor camcc-sm8150.c, gcc-sm8150.c).
 *
 * The sensor's model is not in the device tree (Qualcomm's camera stack chooses it in userspace
 * from the module file); this port's reading is the one public specs and the expected chip IDs
 * give: sensor 0 is the 48 MP Sony IMX586 (ID 0x0586 at 0x0016), sensor 1 the 8 MP ultra-wide
 * Sony IMX319 (0x0319 at 0x0016), sensor 2 the 16 MP front Sony IMX471 (0x0471 at 0x0016),
 * sensor 3 the 13 MP telephoto Samsung S5K3M5 (0x30d5 at 0x0000) and sensor 4 the 2 MP mono,
 * which answered at 0x37 with 0x02d0 at register 0xf0 (an 8-bit register map): a GalaxyCore
 * two-megapixel part, not the OmniVision the first table guessed (logs/182953-run.log).
 * The ID read is the experiment that settles all five.
 *
 *   cam=1   a look, reading only: GCC's camera clocks, the titan top GDSC, the four MCLK roots,
 *           the CCI and MCLK pins, the three PMIC GPIOs that reset/enable the cameras, and the
 *           two PM8008s' LDOs as they are
 *   cam=2   ... and the main sensor powered up as the tree wires it: PM8008 #2 enabled, LDOs on,
 *           MCLK 0 at 19.2 MHz, reset out of the PM8150, then the CCI bus scanned, the chip ID
 *           read at 0x0016, the EEPROM at 0x50..0x57 read at 0x0000, and everything put back
 *   cam=3   the same for the front sensor (its module asks 24 MHz; this first version runs its
 *           MCLK from the crystal at 19.2 MHz for the ID read only and says so)
 *   cam=4   all five sensors, one at a time: bus scan, IDs, EEPROMs, each powered down before the
 *           next; the inventory that fills in the table above from the phone's own answers
 *   cam=5   the main sensor, left powered at the end, for a meter or a scope
 *   cam=6   the mono sensor alone, twice: gpio29 (the shared front/mono enable) high, then low
 *
 * All accesses go through the PMIC arbiter under hakios_spmi_lock(), and only through a channel
 * the arbiter says system 0 owns, the same path the keys, the charger and the WLED use. The
 * PM8008s' I2C is bit-banged on serial engine 4's pins, as the touch and gauge buses are; the
 * sensors' I2C is bit-banged on the CCI pins, which is the same trick drivers/camera/hakios.camera.c
 * uses. Every write is read back; a read that does not answer is reported and the next step is
 * not guessed.
 *
 * Status: the flash half has run on the phone (logs/092331-run.log); the camera half is written,
 * not compiled or run yet. The first run is cam=1 (read-only), then cam=2 (the main sensor).
 */
#include <kern/clock.h>
#include <kern/startup.h>
#include <libkern/libkern.h>
#include <mach/mach_types.h>
#include <pexpert/pexpert.h>
#include <sys/systm.h>

extern vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);
extern void loader_log_putc(char c);
extern void hakios_spmi_lock(void);
extern void hakios_spmi_unlock(void);

static int say(const char *format, ...) __printflike(1, 2);

static int
say(const char *format, ...)
{
	char line[300];
	va_list list;

	va_start(list, format);
	vsnprintf(line, sizeof(line), format, list);
	va_end(list);
	for (const char *c = line; *c != 0; c++) {
		loader_log_putc(*c);
	}
	return printf("%s", line);
}
#define printf say

static inline uint32_t
rd(vm_offset_t address)
{
	return *(volatile uint32_t *)address;
}

static inline void
wr(vm_offset_t address, uint32_t value)
{
	*(volatile uint32_t *)address = value;
}

/* ---- the PMIC arbiter (the same map the keys, the charger and the WLED use) ---------------------- */

#define ARB_CORE_PHYS           0x0c440000UL
#define ARB_CORE_SIZE           0x1100UL
#define ARB_CNFG_PHYS           0x0c40a000UL
#define ARB_CNFG_SIZE           0x1800UL
#define ARB_OBSERVER_PHYS       0x0e600000UL
#define ARB_OBSERVER_SIZE       0x10000UL
#define ARB_CHNLS_PHYS          0x0c600000UL    /* writes: 0x10000 per channel */
#define ARB_SYSTEM              0
#define ARB_VERSION             0x000
#define ARB_VERSION_5           0x50000000u
#define ARB_VERSION_7           0x70000000u
#define ARB_FEATURES            0x004
#define ARB_CHANNEL(n)          (0x900 + 4 * (n))       /* [19:8] slave and peripheral */
#define ARB_OWNER(n)            (0x700 + 4 * (n))       /* [2:0] the system allowed to write */
#define ARB_MAX_CHANNELS        512

#define OBSERVER_COMMAND        0x00
#define OBSERVER_STATUS         0x08
#define OBSERVER_DATA0          0x18
#define OBSERVER_DATA1          0x1c
#define CHANNEL_COMMAND         0x00
#define CHANNEL_STATUS          0x08
#define CHANNEL_WDATA0          0x10
#define STATUS_DONE             0x1
#define STATUS_FAILURE          0x2
#define STATUS_DENIED           0x4
#define STATUS_DROPPED          0x8
#define COMMAND_WRITE           (0u << 27)              /* EXT_WRITEL */
#define COMMAND_READ            (1u << 27)
#define READ_TIMEOUT_US         2000

/* the flash LED module of the PM8150L, SPMI slave 5, and the revid function of the same chip.
 * The registers and the on-sequence are the vendor driver's (leds-qpnp-flash-v2.c), as collected
 * in notes/remaining-hardware.md: per-LED registers at base + led id. */
#define FLASH_SLAVE             5
#define FLASH_PERIPHERAL        0xd3
#define FLASH_REVID_PERIPHERAL  0x01
#define FLASH_SAFETY(id)        (0x40 + (id))   /* bit 7 enable, (ms / 10) - 1; 0x13 = timer off */
#define FLASH_TGR(id)           (0x43 + (id))   /* round(mA * 1000 / ires_uA) - 1; 300 mA at 12.5 = 0x17 */
#define FLASH_MOD_CTRL          0x46            /* bit 7 module enable */
#define FLASH_IRES              0x47            /* two bits per LED: index 0 = 12.5 mA step */
#define FLASH_STROBE(id)        (0x49 + (id))   /* bit 2 = the strobe is the software's */
#define FLASH_EN_LED_CTRL       0x4c            /* bit id = that LED on */
#define FLASH_LED_STATUS1       0x08            /* fault and state, read-only, two registers */
#define FLASH_INT_RT_STS        0x10            /* fault / ramp-up / ramp-down, read-only */
#define FLASH_IRES_STEP0        0               /* 12.5 mA: the step the device tree's torch uses */
#define FLASH_TGR_300MA         0x17            /* round(300000 / 12500) - 1 */
#define FLASH_SAFETY_OFF        0x13            /* bit 7 clear: no safety timer */
#define FLASH_SEC_ACCESS        0xd0
#define FLASH_SEC_UNSECURE      0x78

static vm_offset_t arb_core, arb_cnfg, arb_observer, flash_write;
static uint32_t flash_channel = ~0u;
static uint8_t flash_mod = 0xff, flash_en = 0xff;      /* what 0x46 and 0x4c said before we touched them */

static const char *
spmi_read(uint32_t channel, uint8_t reg, uint8_t *bytes, uint32_t count)
{
	vm_offset_t ch = arb_observer + 0x10000UL * ARB_SYSTEM + 0x80UL * channel;
	uint64_t start = mach_absolute_time(), now, interval;
	uint32_t status, data[2];

	nanoseconds_to_absolutetime(READ_TIMEOUT_US * (uint64_t)NSEC_PER_USEC, &interval);
	hakios_spmi_lock();
	wr(ch + OBSERVER_COMMAND, COMMAND_READ | (uint32_t)reg << 4 | (count - 1));
	do {
		status = rd(ch + OBSERVER_STATUS);
		now = mach_absolute_time();
	} while (!(status & STATUS_DONE) && now - start < interval);
	data[0] = rd(ch + OBSERVER_DATA0);
	data[1] = count > 4 ? rd(ch + OBSERVER_DATA1) : 0;
	hakios_spmi_unlock();

	if (!(status & STATUS_DONE)) {
		return "no answer";
	} else if (status & STATUS_DENIED) {
		return "denied";
	} else if (status & STATUS_FAILURE) {
		return "failed";
	} else if (status & STATUS_DROPPED) {
		return "dropped";
	}
	for (uint32_t i = 0; i < count; i++) {
		bytes[i] = (uint8_t)(data[i / 4] >> (8 * (i % 4)));
	}
	return NULL;
}

static const char *
spmi_write_at(vm_offset_t window, uint8_t reg, uint8_t value)
{
	uint64_t start = mach_absolute_time(), now, interval;
	uint32_t status;

	nanoseconds_to_absolutetime(READ_TIMEOUT_US * (uint64_t)NSEC_PER_USEC, &interval);
	hakios_spmi_lock();
	wr(window + CHANNEL_WDATA0, value);
	wr(window + CHANNEL_COMMAND, COMMAND_WRITE | (uint32_t)reg << 4);
	do {
		status = rd(window + CHANNEL_STATUS);
		now = mach_absolute_time();
	} while (!(status & STATUS_DONE) && now - start < interval);
	hakios_spmi_unlock();

	if (!(status & STATUS_DONE)) {
		return "no answer";
	} else if (status & STATUS_DENIED) {
		return "denied";
	} else if (status & STATUS_FAILURE) {
		return "failed";
	} else if (status & STATUS_DROPPED) {
		return "dropped";
	}
	return NULL;
}

static const char *
spmi_write(uint8_t reg, uint8_t value)
{
	return spmi_write_at(flash_write, reg, value);
}

/* ---- what the module says, and what to do about it ------------------------------------------------- */

/* One line with everything that matters: the fault and ramp registers (rt_sts bit 0 fault, bit 1
 * ramp-up done), the per-LED state, and the current configuration. Read-only; asked for before,
 * while lit, and after a torch. */
static void
flash_report(const char *when)
{
	static const struct { const char *name; uint8_t reg, count, at; } regs[] = {
		{ "led_status", FLASH_LED_STATUS1, 2, 0 }, { "rt_sts", FLASH_INT_RT_STS, 1, 2 },
		{ "safety0", FLASH_SAFETY(0), 1, 3 }, { "tgr0", FLASH_TGR(0), 1, 4 },
		{ "mod_ctrl", FLASH_MOD_CTRL, 1, 5 }, { "ires", FLASH_IRES, 1, 6 },
		{ "strobe0", FLASH_STROBE(0), 1, 7 }, { "en_led", FLASH_EN_LED_CTRL, 1, 8 },
	};
	uint8_t value[9];
	const char *problem = NULL;
	unsigned i;

	for (i = 0; i < sizeof(regs) / sizeof(regs[0]) && problem == NULL; i++) {
		problem = spmi_read(flash_channel, regs[i].reg, &value[regs[i].at], regs[i].count);
	}
	if (problem != NULL) {
		say("flash: %s: %s reading %s\n", when, problem, regs[i].name);
		return;
	}
	say("flash: %s: led_status %#02x %#02x rt_sts %#02x safety0 %#02x tgr0 %#02x mod_ctrl %#02x "
	    "ires %#02x strobe0 %#02x en_led %#02x\n", when, value[0], value[1], value[2], value[3],
	    value[4], value[5], value[6], value[7], value[8]);
}

/* The vendor driver's torch on-sequence for LED 0 (switch_set 1480-1626): current step, strobe
 * source, target current, safety timer, module enable, channel enable. 300 mA at the 12.5 mA step
 * is the tree's own torch default. The fault registers are read while it is lit; then everything
 * goes back as it was found. */
static void
flash_torch(int leave_on, unsigned led)
{
	static struct { uint8_t reg; const char *why; } order[] = {
		{ FLASH_SEC_ACCESS, "the bank's registers are open to us" },
		{ FLASH_IRES, "that LED's step is 12.5 mA, the step the tree's torch uses" },
		{ FLASH_STROBE(0), "the strobe is the software's: this is a torch, not a camera flash" },
		{ FLASH_TGR(0), "300 mA target, the tree's torch default" },
		{ FLASH_SAFETY(0), "safety timer off: we will switch it off ourselves" },
		{ FLASH_MOD_CTRL, "module enable" },
		{ FLASH_EN_LED_CTRL, "that LED on: the light should be lit now" },
	};
	uint8_t values[] = { FLASH_SEC_UNSECURE, 0x00, 0x04, FLASH_TGR_300MA, FLASH_SAFETY_OFF, 0x00, 0x01 };
	uint8_t read_back;
	uint8_t strobe_reg = FLASH_STROBE(led), tgr_reg = FLASH_TGR(led), safety_reg = FLASH_SAFETY(led);
	uint8_t regs[7] = { order[0].reg, order[1].reg, strobe_reg, tgr_reg, safety_reg, order[5].reg, order[6].reg };

	/* the per-LED registers are indexed by the LED number */
	for (unsigned i = 0; i < 7; i++) {
		order[i].reg = regs[i];
	}
	/* IRES and STROBE are read-modify-write: the other LEDs and the other bits stay as they are */
	if (spmi_read(flash_channel, FLASH_IRES, &read_back, 1) == NULL) {
		values[1] = (uint8_t)((read_back & ~(3u << (2 * led))) | (FLASH_IRES_STEP0 << (2 * led)));
	}
	if (spmi_read(flash_channel, strobe_reg, &read_back, 1) == NULL) {
		values[2] = (uint8_t)(read_back | 0x04);
	}
	values[5] = (uint8_t)(flash_mod | 0x80);
	values[6] = (uint8_t)(flash_en | (1u << led));
	for (unsigned i = 0; i < sizeof(order) / sizeof(order[0]); i++) {
		const char *problem = spmi_write(order[i].reg, values[i]);

		if (problem != NULL) {
			say("flash: writing %#02x (%s): %s\n", order[i].reg, order[i].why, problem);
			return;
		}
		say("flash: %s: %#02x written, LED %u (%s)\n", order[i].reg == FLASH_SEC_ACCESS ? "opening" : "on",
		    values[i], led, order[i].why);
	}
	delay_for_interval(200, NSEC_PER_MSEC);
	flash_report("torch lit");
	if (leave_on) {
		say("flash: LED %u left lit: the torch should still be shining\n", led);
		return;
	}
	(void)spmi_write(safety_reg, FLASH_SAFETY_OFF);
	(void)spmi_write(FLASH_EN_LED_CTRL, flash_en);
	(void)spmi_write(FLASH_MOD_CTRL, flash_mod);
	delay_for_interval(20, NSEC_PER_MSEC);
	flash_report("torch off");
	say("flash: put back as found: module enable %#02x, channel enable %#02x\n", flash_mod, flash_en);
}

/* flash=3 leaves the torch lit on purpose, and the next boot then finds it lit: "put back as
 * found" would light it again. flash=4 is the explicit off -- channel enable, module enable and
 * the strobe all zero -- the only way back from a left-lit round. */
static void
flash_off_all(void)
{
	(void)spmi_write(FLASH_SEC_ACCESS, FLASH_SEC_UNSECURE);
	(void)spmi_write(FLASH_EN_LED_CTRL, 0x00);
	(void)spmi_write(FLASH_MOD_CTRL, 0x00);
	for (unsigned led = 0; led < 3; led++) {
		(void)spmi_write(FLASH_STROBE(led), 0x00);
		(void)spmi_write(FLASH_TGR(led), 0x00);
	}
	(void)spmi_write(FLASH_IRES, 0x00);
	delay_for_interval(20, NSEC_PER_MSEC);
	flash_report("forced off");
	say("flash: forced off (flash=4): channel enable, module enable and the strobe are 0\n");
}

/* The module has three LED channels, and the module enable and the enable bits are shared: only
 * the per-LED current, strobe and enable bit say which one is wired. LED 0 stayed dark for a whole
 * round with every register read back as written (logs/191722-run.log, logs/192127-run.log), and a
 * round with all three channels on is the one that lit the flashlight (the eye said so). So the
 * torch is the three channels together: none of them is known to be the wired one, and driving the
 * unwired ones costs nothing. flash=4 turns them all off again. */
static void
flash_all_leds(bool leave_on)
{
	uint8_t led_status = 0, strobe;

	(void)spmi_write(FLASH_SEC_ACCESS, FLASH_SEC_UNSECURE);
	for (unsigned led = 0; led < 3; led++) {
		if (spmi_read(flash_channel, FLASH_IRES, &led_status, 1) == NULL) {
			uint8_t value = (uint8_t) ((led_status & ~(3u << (2 * led))) |
			    (FLASH_IRES_STEP0 << (2 * led)));

			(void)spmi_write(FLASH_IRES, value);
		}
		strobe = 0;
		(void)spmi_read(flash_channel, FLASH_STROBE(led), &strobe, 1);
		(void)spmi_write(FLASH_STROBE(led), (uint8_t) (strobe | 0x04));
		(void)spmi_write(FLASH_TGR(led), FLASH_TGR_300MA);
		(void)spmi_write(FLASH_SAFETY(led), FLASH_SAFETY_OFF);
	}
	(void)spmi_write(FLASH_MOD_CTRL, (uint8_t) (flash_mod | 0x80));
	(void)spmi_write(FLASH_EN_LED_CTRL, (uint8_t) (flash_en | 0x07));
	delay_for_interval(200, NSEC_PER_MSEC);
	flash_report("all three LED channels on");
	if (leave_on) {
		say("flash: LED 0, 1 and 2 are on and left on (flash=3/5): the flashlight, whichever "
		    "channel it is wired to, should be lit; flash=4 turns it off\n");
		return;
	}
	flash_off_all();
}

static bool
flash_open(void)
{
	uint32_t version, channels;
	uint8_t type = 0xff, subtype = 0xff, revid[8] = { 0 };
	uint32_t revid_channel = ~0u;

	arb_core = ml_io_map(ARB_CORE_PHYS, ARB_CORE_SIZE);
	arb_observer = ml_io_map(ARB_OBSERVER_PHYS, ARB_OBSERVER_SIZE);
	arb_cnfg = ml_io_map(ARB_CNFG_PHYS, ARB_CNFG_SIZE);
	if (arb_core == 0 || arb_observer == 0) {
		say("flash: the PMIC arbiter does not map\n");
		return false;
	}
	version = rd(arb_core + ARB_VERSION);
	channels = rd(arb_core + ARB_FEATURES) & 0x7ff;
	if (version < ARB_VERSION_5 || version >= ARB_VERSION_7) {
		say("flash: this driver knows version 5 of the arbiter only, not %#x\n", version);
		return false;
	}
	for (uint32_t n = 0; n < (channels ? channels : ARB_MAX_CHANNELS); n++) {
		uint32_t ppid = (rd(arb_core + ARB_CHANNEL(n)) >> 8) & 0xfff;

		if (ppid == ((FLASH_SLAVE << 8) | FLASH_PERIPHERAL) && flash_channel == ~0u) {
			if (arb_cnfg != 0 && (rd(arb_cnfg + ARB_OWNER(n)) & 7) != ARB_SYSTEM) {
				continue;
			}
			flash_channel = n;
		} else if (ppid == ((FLASH_SLAVE << 8) | FLASH_REVID_PERIPHERAL)) {
			revid_channel = n;
		}
	}
	if (flash_channel == ~0u) {
		say("flash: slave %u 0x%02x00 has no channel of ours: the torch stays dark\n",
		    FLASH_SLAVE, FLASH_PERIPHERAL);
		return false;
	}
	if (spmi_read(flash_channel, 0x04, &type, 1) != NULL) {
		say("flash: slave %u 0x%02x00 does not answer\n", FLASH_SLAVE, FLASH_PERIPHERAL);
		return false;
	}
	(void)spmi_read(flash_channel, 0x05, &subtype, 1);
	/* the chip's own identity: function 0x0100 of the same slave is its revid block, and this is
	 * the read that says in the phone's own words that slave 5 is a PM8150L (the WLED module is on
	 * the same chip and works). revid is 4 bytes at 0x0104, device type 2 at 0x0108, subtype at
	 * 0x010a (the vendor's qcom-revid driver's offsets). */
	if (revid_channel != ~0u && spmi_read(revid_channel, 0x04, revid, 8) == NULL) {
		say("flash: slave %u revid: %02x %02x %02x %02x, device type %#02x, subtype %#02x\n",
		    FLASH_SLAVE, revid[0], revid[1], revid[2], revid[3], revid[4] | revid[5] << 8, revid[6]);
	} else {
		say("flash: slave %u has no revid channel of ours: the chip stays unnamed\n", FLASH_SLAVE);
	}
	say("flash: slave %u 0x%02x00 (channel %u): type %#02x subtype %#02x\n",
	    FLASH_SLAVE, FLASH_PERIPHERAL, flash_channel, type, subtype);
	if (spmi_read(flash_channel, FLASH_MOD_CTRL, &flash_mod, 1) != NULL) {
		flash_mod = 0x00;
	}
	if (spmi_read(flash_channel, FLASH_EN_LED_CTRL, &flash_en, 1) != NULL) {
		flash_en = 0x00;
	}
	flash_report("as found");
	flash_write = ml_io_map(ARB_CHNLS_PHYS + 0x10000UL * flash_channel, 0x1000);
	if (flash_write == 0) {
		say("flash: the write window of channel %u does not map\n", flash_channel);
		return false;
	}
	return true;
}

__startup_func
static void
flash_init(void)
{
	uint32_t level = 0;

	if (!PE_parse_boot_argn("flash", &level, sizeof(level)) || level == 0) {
		return;                                /* nothing is touched unless a boot-argument asks */
	}
	if (!flash_open()) {
		return;
	}
	if (level == 6 || level == 7) {
		/* flash=6: only LED 1, left lit; flash=7: only LED 2 -- for naming the wired channel */
		flash_torch(1, level == 6 ? 1 : 2);
	} else if (level == 5) {
		flash_all_leds(true);
	} else if (level >= 4) {
		flash_off_all();
	} else if (level >= 2) {
		/* The torch is all three channels: LED 0 alone was dark, all three lit the flashlight. */
		flash_all_leds(level >= 3);
	}
}
STARTUP(EARLY_BOOT, STARTUP_RANK_LAST, flash_init);

/* ===================================================================================================
 * The cameras
 * =================================================================================================== */

/*
 * TLMM tiles are offsets from the tree's pinctrl@03000000 (vendor
 * pinctrl-sm8150.c: NORTH 0x00900000, SOUTH 0x00D00000). The camera pins
 * live in NORTH (gpio13..34) and SOUTH (gpio51/52, the PM8008 I2C bus).
 * The old 0x0f100000 was not a TLMM address at all and the first read hung
 * the bus (logs/162817-run.log, logs/162922-run.log).
 */
#define CAM_TLMM_NORTH_PHYS     (0x03900000UL + 0x0000c000UL) /* gpio12..34 */
#define CAM_TLMM_NORTH_SIZE     0x17000UL
#define CAM_TLMM_EAST_PHYS      (0x03500000UL + 0x00017000UL) /* gpio23..30 */
#define CAM_TLMM_EAST_SIZE      0x8000UL
#define CAM_TLMM_SOUTH_PHYS     (0x03000000UL + 0x00d00000UL + 0x00033000UL) /* gpio51 */
#define CAM_TLMM_SOUTH_SIZE     0x2000UL                                        /* gpio51 and gpio52 */
#define CAM_GCC_PHYS            0x00100000UL
#define CAM_GCC_SIZE            0x100000UL
#define CAM_CAMCC_PHYS          0x0ad00000UL
#define CAM_CAMCC_SIZE          0x20000UL

/* [1:0] pull, [5:2] function, [8:6] drive, [9] output; bit 0/1 in io */
#define CAM_GPIO_CFG(n)         (((n) == 23 || (n) == 29 || (n) == 30) ? \
                                   cam_tlmm_east + 0x1000UL * ((n) - 23) : \
                                (n) <= 34 ? cam_tlmm_north + 0x1000UL * ((n) - 12) : \
                                   cam_tlmm_south + 0x1000UL * ((n) - 51))
#define CAM_GPIO_IO(n)          (CAM_GPIO_CFG(n) + 4)
#define CAM_CFG_PULL_UP          3
#define CAM_CFG_OUTPUT          (1u << 9)
#define CAM_CFG_FUNCTION(f)     ((uint32_t)(f) << 2)
#define CAM_CFG_DRIVE_MA(ma)    ((uint32_t)((ma) / 2 - 1) << 6)

/* GCC: the two always-on camera branches (gcc-sm8150.c: gcc_camera_ahb_clk 0xb008, xo 0xb044) */
#define CAM_GCC_CAMERA_AHB      0x0000b008
#define CAM_GCC_CAMERA_XO       0x0000b044
#define CAM_GCC_BRANCH_ON       1u
#define CAM_GCC_BRANCH_OFF      (1u << 31)

/* CAMCC: titan top GDSC and the four MCLK roots (camcc-sm8150.c) */
#define CAM_TITAN_TOP_GDSCR     0x0000c1bc      /* the tree's 0xad0c1bc: [0] collapse, [31] on */
#define CAM_GDSC_COLLAPSE       1u
#define CAM_GDSC_ON             (1u << 31)
#define CAM_MCLK0_CMD           0x00005004
#define CAM_MCLK0_CBCR          0x0000501c
#define CAM_MCLK1_CMD           0x00005024
#define CAM_MCLK1_CBCR          0x0000503c
#define CAM_MCLK2_CMD           0x00005044
#define CAM_MCLK2_CBCR          0x0000505c
#define CAM_MCLK3_CMD           0x00005064
#define CAM_MCLK3_CBCR          0x0000507c
#define CAM_MCLK_CFG_RCGR(cmd)  ((cmd) + 4)     /* [4:0] divider (2n-1), [10:8] source (0: crystal) */
#define CAM_MCLK_M(cmd)         ((cmd) + 8)
#define CAM_MCLK_N(cmd)         ((cmd) + 12)
#define CAM_MCLK_D(cmd)         ((cmd) + 16)
#define CAM_RCG_UPDATE          1u

/* The PMIC GPIOs this camera block needs (the SPMI peripheral numbers: base 0xc000 + 0x100*(pin-1)) */
#define CAM_PM8150_SLAVE        0       /* PM8150, the master PMIC: GPIO1 is the rear main reset */
#define CAM_PM8150L_SLAVE       4       /* PM8150L's primary block: its GPIOs live here, not on slave 5 */
#define CAM_PMIC_GPIO_PERIPH(n) (0xc0u + (n) - 1)
#define CAM_GPIO_REG_VIN        0x41    /* [2:0] supply: 0 = VIN0 */
#define CAM_GPIO_REG_PULL       0x42    /* [2:0] 5 = no pull */
#define CAM_GPIO_REG_MODE       0x40    /* [1:0] 1 = output */
#define CAM_GPIO_REG_OUT        0x44    /* [7] the level, [3:0] function 0 = normal */
#define CAM_GPIO_REG_EN         0x46    /* [7] enabled */

static vm_offset_t cam_tlmm_north, cam_tlmm_east, cam_tlmm_south, cam_gcc, cam_camcc;
static bool cam_mapped;

static void
cam_wait_ms(uint32_t ms)
{
	if (ms != 0) {
		delay_for_interval(ms, NSEC_PER_MSEC);
	}
}

static void
cam_spin_us(uint32_t us)
{
	uint64_t until;

	nanoseconds_to_absolutetime((uint64_t)us * NSEC_PER_USEC, &until);
	until += mach_absolute_time();
	while (mach_absolute_time() < until) {
	}
}

static void
cam_pin_show(unsigned pin, const char *what)
{
	uint32_t cfg = rd(CAM_GPIO_CFG(pin)), io = rd(CAM_GPIO_IO(pin));

	say("camera: gpio%u (%s): function %u, pull %u, %u mA, %s, reads %u\n", pin, what, (cfg >> 2) & 15, cfg & 3,
	    (((cfg >> 6) & 7) + 1) * 2, (cfg & CAM_CFG_OUTPUT) ? (io & 2 ? "drives high" : "drives low") : "input", io & 1);
}

static bool
cam_map(void)
{
	if (cam_mapped) {
		return true;
	}
	cam_tlmm_north = ml_io_map(CAM_TLMM_NORTH_PHYS, CAM_TLMM_NORTH_SIZE);
	cam_tlmm_east = ml_io_map(CAM_TLMM_EAST_PHYS, CAM_TLMM_EAST_SIZE);
	cam_tlmm_south = ml_io_map(CAM_TLMM_SOUTH_PHYS, CAM_TLMM_SOUTH_SIZE);
	cam_gcc = ml_io_map(CAM_GCC_PHYS, CAM_GCC_SIZE);
	cam_camcc = ml_io_map(CAM_CAMCC_PHYS, CAM_CAMCC_SIZE);
	if (cam_tlmm_north == 0 || cam_tlmm_east == 0 || cam_tlmm_south == 0 || cam_gcc == 0 || cam_camcc == 0) {
		say("camera: the TLMM, GCC or CAMCC window does not map\n");
		return false;
	}
	if (arb_core == 0) {
		arb_core = ml_io_map(ARB_CORE_PHYS, ARB_CORE_SIZE);
	}
	if (arb_cnfg == 0) {
		arb_cnfg = ml_io_map(ARB_CNFG_PHYS, ARB_CNFG_SIZE);
	}
	if (arb_observer == 0) {
		arb_observer = ml_io_map(ARB_OBSERVER_PHYS, ARB_OBSERVER_SIZE);
	}
	if (arb_core == 0 || arb_cnfg == 0 || arb_observer == 0) {
		say("camera: the PMIC arbiter does not map\n");
		return false;
	}
	{
		uint32_t version = rd(arb_core + ARB_VERSION);

		if (version < ARB_VERSION_5 || version >= ARB_VERSION_7) {
			say("camera: this driver knows arbiter version 5, not %#x\n", version);
			return false;
		}
	}
	cam_mapped = true;
	return true;
}

/* ---- the sensors' bus: CCI by hand, on the pins the device tree wires to each master ----------------- */

struct cam_bus {
	const char *    name;
	unsigned        sda, scl;
	uint32_t        cfg[2], io[2];  /* the pins as they were found, while taken */
};

static void
cam_bus_half_bit(void)
{
	cam_spin_us(5);                 /* 100 kHz at most */
}

static void
cam_bus_line(struct cam_bus *b, unsigned pin, bool high)
{
	wr(CAM_GPIO_IO(pin), 0);
	wr(CAM_GPIO_CFG(pin), CAM_CFG_PULL_UP | (high ? 0 : CAM_CFG_OUTPUT));
}

static bool
cam_bus_level(unsigned pin)
{
	return rd(CAM_GPIO_IO(pin)) & 1;
}

static void
cam_i2c_start(struct cam_bus *b)
{
	cam_bus_line(b, b->sda, true);
	cam_bus_line(b, b->scl, true);
	cam_bus_half_bit();
	cam_bus_line(b, b->sda, false);
	cam_bus_half_bit();
	cam_bus_line(b, b->scl, false);
	cam_bus_half_bit();
}

static void
cam_i2c_stop(struct cam_bus *b)
{
	cam_bus_line(b, b->sda, false);
	cam_bus_half_bit();
	cam_bus_line(b, b->scl, true);
	cam_bus_half_bit();
	cam_bus_line(b, b->sda, true);
	cam_bus_half_bit();
}

static bool
cam_i2c_bit(struct cam_bus *b, bool out)
{
	bool in;

	cam_bus_line(b, b->sda, out);
	cam_bus_half_bit();
	cam_bus_line(b, b->scl, true);
	for (unsigned stretched = 0; !cam_bus_level(b->scl) && stretched < 100; stretched++) {
		cam_bus_half_bit();
	}
	cam_bus_half_bit();
	in = cam_bus_level(b->sda);
	cam_bus_line(b, b->scl, false);
	return in;
}

static bool
cam_i2c_write(struct cam_bus *b, uint8_t byte)
{
	for (int bit = 7; bit >= 0; bit--) {
		cam_i2c_bit(b, (byte >> bit) & 1);
	}
	return !cam_i2c_bit(b, true);
}

static uint8_t
cam_i2c_read(struct cam_bus *b, bool more)
{
	uint8_t byte = 0;

	for (int bit = 7; bit >= 0; bit--) {
		byte = (uint8_t)(byte << 1 | cam_i2c_bit(b, true));
	}
	cam_i2c_bit(b, !more);
	return byte;
}

static void
cam_bus_give_back(struct cam_bus *b)
{
	wr(CAM_GPIO_IO(b->sda), b->io[0] & 2);
	wr(CAM_GPIO_IO(b->scl), b->io[1] & 2);
	wr(CAM_GPIO_CFG(b->sda), b->cfg[0]);
	wr(CAM_GPIO_CFG(b->scl), b->cfg[1]);
}

static bool
cam_bus_unwedge(struct cam_bus *b)
{
	for (unsigned clocks = 1; clocks <= 9; clocks++) {
		cam_bus_line(b, b->scl, false);
		cam_bus_half_bit();
		cam_bus_line(b, b->scl, true);
		cam_bus_half_bit();
		if (cam_bus_level(b->sda)) {
			cam_i2c_stop(b);
			say("camera: %s: the data line was held down and came back after %u clock(s)\n", b->name, clocks);
			return true;
		}
	}
	return false;
}

static bool
cam_bus_take(struct cam_bus *b)
{
	b->cfg[0] = rd(CAM_GPIO_CFG(b->sda));
	b->cfg[1] = rd(CAM_GPIO_CFG(b->scl));
	b->io[0] = rd(CAM_GPIO_IO(b->sda));
	b->io[1] = rd(CAM_GPIO_IO(b->scl));
	cam_bus_line(b, b->sda, true);
	cam_bus_line(b, b->scl, true);
	cam_bus_half_bit();
	if (cam_bus_level(b->sda) && cam_bus_level(b->scl)) {
		return true;
	}
	if (cam_bus_level(b->scl) && !cam_bus_level(b->sda) && cam_bus_unwedge(b) && cam_bus_level(b->scl)) {
		return true;
	}
	{
		bool sda = cam_bus_level(b->sda), scl = cam_bus_level(b->scl);

		cam_bus_give_back(b);
		say("camera: %s is not at rest (gpio%u reads %u, gpio%u reads %u): not touched\n", b->name, b->sda, sda, b->scl, scl);
	}
	return false;
}

static bool
cam_i2c_transfer(struct cam_bus *b, uint8_t address, const uint8_t *out, unsigned out_count, uint8_t *in, unsigned in_count)
{
	bool answered;

	if (!cam_bus_take(b)) {
		return false;
	}
	cam_i2c_start(b);
	answered = cam_i2c_write(b, (uint8_t)(address << 1));
	for (unsigned i = 0; answered && i < out_count; i++) {
		answered = cam_i2c_write(b, out[i]);
	}
	if (answered && in_count != 0) {
		cam_i2c_start(b);
		answered = cam_i2c_write(b, (uint8_t)(address << 1 | 1));
		for (unsigned i = 0; answered && i < in_count; i++) {
			in[i] = cam_i2c_read(b, i + 1 < in_count);
		}
	}
	cam_i2c_stop(b);
	cam_bus_give_back(b);
	return answered;
}

static bool
cam_i2c_probe(struct cam_bus *b, uint8_t address)
{
	bool answered;

	if (!cam_bus_take(b)) {
		return false;
	}
	cam_i2c_start(b);
	answered = cam_i2c_write(b, (uint8_t)(address << 1));
	cam_i2c_stop(b);
	cam_bus_give_back(b);
	return answered;
}

static bool
cam_i2c_read_regs(struct cam_bus *b, uint8_t address, uint16_t reg, uint8_t *data, unsigned count, unsigned address_bytes)
{
	uint8_t where[2];

	if (address_bytes == 2) {
		where[0] = (uint8_t)(reg >> 8);
		where[1] = (uint8_t)reg;
	} else {
		where[0] = (uint8_t)reg;
	}
	return cam_i2c_transfer(b, address, where, address_bytes, data, count);
}

static bool
cam_i2c_write_regs(struct cam_bus *b, uint8_t address, uint16_t reg, uint8_t value)
{
	uint8_t out[3];

	out[0] = (uint8_t)(reg >> 8);
	out[1] = (uint8_t)reg;
	out[2] = value;
	return cam_i2c_transfer(b, address, out, 3, NULL, 0);
}

/* ---- the PMIC GPIOs that reset the main sensor and enable the two PM8008s ----------------------------- */

struct cam_pgpio {
	const char *    name;
	uint8_t         slave, periph;
	uint8_t         type, subtype;
	bool            lv_mv;          /* 0x10/0x11: the level is 0x44 bit 7; otherwise 0x40 bit 0 */
	uint32_t        channel;
	vm_offset_t     window;
	uint8_t         found[5];       /* VIN, PULL, MODE, OUT, EN as they were found */
	bool            open, changed;
};

static bool
cam_pgpio_open(struct cam_pgpio *g)
{
	uint32_t ppid = (uint32_t)(g->slave << 8) | g->periph;
	uint8_t type = 0, subtype = 0, rt = 0;

	if (g->open) {
		return true;
	}
	if (!cam_map()) {
		return false;
	}
	g->channel = ~0u;
	for (uint32_t n = 0; n < ARB_MAX_CHANNELS; n++) {
		uint32_t map = rd(arb_core + ARB_CHANNEL(n));
		uint32_t owner;

		if (map == 0 || ((map >> 8) & 0xfff) != ppid) {
			continue;
		}
		owner = rd(arb_cnfg + ARB_OWNER(n)) & 7;
		if (owner != ARB_SYSTEM) {
			say("camera: %s is on channel %u, owned by system %u (not us)\n", g->name, n, owner);
			continue;
		}
		g->channel = n;
		break;
	}
	if (g->channel == ~0u) {
		say("camera: %s (slave %u peripheral %#04x) has no channel of ours\n", g->name, g->slave, g->periph);
		return false;
	}
	g->window = ml_io_map(ARB_CHNLS_PHYS + 0x10000UL * g->channel, 0x1000);
	if (g->window == 0) {
		say("camera: %s: the write window of channel %u does not map\n", g->name, g->channel);
		return false;
	}
	if (spmi_read(g->channel, 0x04, &type, 1) != NULL || spmi_read(g->channel, 0x05, &subtype, 1) != NULL) {
		say("camera: %s does not answer\n", g->name);
		return false;
	}
	if (type != 0x10) {
		say("camera: %s: type %#02x is not a PMIC GPIO: not written\n", g->name, type);
		return false;
	}
	g->type = type;
	g->subtype = subtype;
	g->lv_mv = subtype == 0x10 || subtype == 0x11;
	(void)spmi_read(g->channel, 0x10, &rt, 1);
	{
		static const uint8_t lv_which[5] = { CAM_GPIO_REG_VIN, CAM_GPIO_REG_PULL, CAM_GPIO_REG_MODE, CAM_GPIO_REG_OUT,
			CAM_GPIO_REG_EN };
		static const uint8_t which[4] = { CAM_GPIO_REG_VIN, CAM_GPIO_REG_PULL, CAM_GPIO_REG_MODE, CAM_GPIO_REG_EN };
		static const uint8_t at[4] = { 0, 1, 2, 4 };
		unsigned count = g->lv_mv ? 5 : 4;

		g->found[3] = 0;                /* no OUT_SOURCE register on the 8-channel type */
		for (unsigned i = 0; i < count; i++) {
			uint8_t reg = g->lv_mv ? lv_which[i] : which[i];
			unsigned slot = g->lv_mv ? i : at[i];
			if (spmi_read(g->channel, reg, &g->found[slot], 1) != NULL) {
				say("camera: %s: %#02x does not answer\n", g->name, reg);
				return false;
			}
		}
	}
	g->open = true;
	say("camera: %s: slave %u peripheral %#04x, channel %u, type %#02x subtype %#02x, reads %u; "
	    "vin %#02x pull %#02x mode %#02x out %#02x en %#02x\n", g->name, g->slave, g->periph, g->channel, type,
	    subtype, rt & 1, g->found[0], g->found[1], g->found[2], g->found[3], g->found[4]);
	return true;
}

static bool
cam_pgpio_write(struct cam_pgpio *g, uint8_t reg, uint8_t value)
{
	const char *problem = spmi_write_at(g->window, reg, value);
	uint8_t back = 0xff;

	if (problem != NULL) {
		say("camera: %s: %#02x <- %#02x: %s\n", g->name, reg, value, problem);
		return false;
	}
	if (spmi_read(g->channel, reg, &back, 1) != NULL || back != value) {
		say("camera: %s: %#02x <- %#02x but reads %#02x\n", g->name, reg, value, back);
		return false;
	}
	return true;
}

static bool
cam_pgpio_drive(struct cam_pgpio *g, bool high)
{
	static const uint8_t lv_regs[5] = { CAM_GPIO_REG_VIN, CAM_GPIO_REG_PULL, CAM_GPIO_REG_MODE, CAM_GPIO_REG_OUT, CAM_GPIO_REG_EN };
	static const uint8_t regs[4] = { CAM_GPIO_REG_VIN, CAM_GPIO_REG_PULL, CAM_GPIO_REG_MODE, CAM_GPIO_REG_EN };
	uint8_t lv_want[5] = { 0, 5, 1, (uint8_t)(high ? 0x80 : 0x00), 0x80 };
	uint8_t want[4] = { 0, 5, (uint8_t)(0x10 | (high ? 1 : 0)), 0x80 };

	if (!cam_pgpio_open(g)) {
		return false;
	}
	g->changed = true;
	if (g->lv_mv) {
		for (unsigned i = 0; i < 5; i++) {
			if (!cam_pgpio_write(g, lv_regs[i], lv_want[i])) {
				return false;
			}
		}
	} else {
		for (unsigned i = 0; i < 4; i++) {
			if (!cam_pgpio_write(g, regs[i], want[i])) {
				return false;
			}
		}
	}
	say("camera: %s (%s) driven %s\n", g->name, g->lv_mv ? "LV/MV" : "8-channel", high ? "high" : "low");
	return true;
}

static void
cam_pgpio_restore(struct cam_pgpio *g)
{
	static const uint8_t lv_regs[5] = { CAM_GPIO_REG_VIN, CAM_GPIO_REG_PULL, CAM_GPIO_REG_MODE, CAM_GPIO_REG_OUT, CAM_GPIO_REG_EN };
	static const uint8_t regs[4] = { CAM_GPIO_REG_VIN, CAM_GPIO_REG_PULL, CAM_GPIO_REG_MODE, CAM_GPIO_REG_EN };
	bool ok = true;

	if (!g->open || !g->changed) {
		return;
	}
	if (g->lv_mv) {
		for (unsigned i = 0; i < 5; i++) {
			ok = cam_pgpio_write(g, lv_regs[i], g->found[i]) && ok;
		}
	} else {
		for (unsigned i = 0; i < 4; i++) {
			ok = cam_pgpio_write(g, regs[i], g->found[i]) && ok;
		}
	}
	g->changed = !ok;               /* a failed restore is tried again if anyone asks */
	say("camera: %s put back as found%s (vin %#02x pull %#02x mode %#02x out %#02x en %#02x)\n", g->name,
	    ok ? "" : " EXCEPT WHAT DID NOT READ BACK", g->found[0], g->found[1], g->found[2], g->found[3], g->found[4]);
}

static struct cam_pgpio cam_main_reset = {
	.name = "PM8150 GPIO1 (rear main reset)", .slave = CAM_PM8150_SLAVE, .periph = CAM_PMIC_GPIO_PERIPH(1),
};
static struct cam_pgpio cam_pm8008_a_enable = {
	.name = "PM8150L GPIO1 (PM8008 #1 enable)", .slave = CAM_PM8150L_SLAVE, .periph = CAM_PMIC_GPIO_PERIPH(1),
};
static struct cam_pgpio cam_pm8008_b_enable = {
	.name = "PM8150L GPIO4 (PM8008 #2 enable)", .slave = CAM_PM8150L_SLAVE, .periph = CAM_PMIC_GPIO_PERIPH(4),
};

/* ---- the two PM8008s: LDOs for the camera rails, on serial engine 4 (gpio51/52) ----------------------- */

#define CAM_PM8008_CHIP_REG     0x0950  /* [0] chip enable */
#define CAM_PM8008_LDO_BASE(n)  (0x4000 + 0x100 * ((n) - 1))
#define CAM_PM8008_STATUS1      0x08    /* [7] ready, [5] over-current, [1:0] mode */
#define CAM_PM8008_VSET_LB      0x40
#define CAM_PM8008_VSET_UB      0x41
#define CAM_PM8008_MODE_CTL1    0x45    /* [2:0] 7 = normal */
#define CAM_PM8008_ENABLE       0x46    /* [7] */

struct cam_pm8008 {
	const char *            name;
	uint8_t                 chip, regs;     /* the I2C addresses: chip controls, regulators */
	struct cam_pgpio *      enable;
	bool                    chip_was_off, chip_switched;
	bool                    ldo_switched[8];        /* only the rails this driver itself switched on */
};

static struct cam_bus cam_pm8008_bus = { .name = "the PM8008s' I2C (SE4, gpio51/52)", .sda = 51, .scl = 52 };

static struct cam_pm8008 cam_pm8008_a = {
	.name = "PM8008 #1", .chip = 0x08, .regs = 0x09, .enable = &cam_pm8008_a_enable,
};
static struct cam_pm8008 cam_pm8008_b = {
	.name = "PM8008 #2", .chip = 0x04, .regs = 0x05, .enable = &cam_pm8008_b_enable,
};

struct cam_rail {
	uint8_t         ldo;
	uint16_t        mv;
	const char *    what;
};

static bool
cam_rail_allowed(const struct cam_pm8008 *p, uint8_t ldo, uint16_t mv)
{
	/* The set points the device tree gives these two chips, and no others (dtbo.bin.01.dts 6139-6368). */
	static const struct { uint8_t chip_is_b; uint8_t ldo; uint16_t mv; } table[] = {
		{ 1, 7, 2900 }, { 1, 1, 1100 }, { 1, 3, 2800 }, { 1, 4, 1800 }, { 1, 5, 2800 }, { 1, 2, 1050 },
		{ 1, 6, 2800 },                 /* actuator@1's VAF, for the AF bring-up */
		{ 0, 4, 2800 }, { 0, 1, 1100 }, { 0, 6, 2800 }, { 0, 2, 1050 }, { 0, 5, 2800 },
		{ 0, 3, 2800 },                 /* actuator@3's VAF, for the AF bring-up */
	};

	for (unsigned i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
		if (table[i].chip_is_b == (p == &cam_pm8008_b) && table[i].ldo == ldo && table[i].mv == mv) {
			return true;
		}
	}
	return false;
}

static bool
cam_pm8008_get(const struct cam_pm8008 *p, uint16_t reg, uint8_t *data, unsigned count)
{
	return cam_i2c_read_regs(&cam_pm8008_bus, p->regs, reg, data, count, 2);
}

static bool
cam_pm8008_put(const struct cam_pm8008 *p, uint16_t reg, uint8_t value)
{
	uint8_t back = 0xff;
	uint16_t base = reg & 0xff00, offset = reg & 0xff;
	uint8_t address = reg == CAM_PM8008_CHIP_REG ? p->chip : p->regs;
	bool allowed = (reg == CAM_PM8008_CHIP_REG) ||
	    (base >= CAM_PM8008_LDO_BASE(1) && base <= CAM_PM8008_LDO_BASE(7) &&
	    (offset == CAM_PM8008_VSET_LB || offset == CAM_PM8008_VSET_UB || offset == CAM_PM8008_MODE_CTL1 || offset == CAM_PM8008_ENABLE));

	if (!allowed) {
		say("camera: %s: %#06x is not a register this driver writes\n", p->name, reg);
		return false;
	}
	if (!cam_i2c_write_regs(&cam_pm8008_bus, address, reg, value) || !cam_i2c_read_regs(&cam_pm8008_bus, address, reg, &back, 1, 2) || back != value) {
		say("camera: %s %#06x <- %#02x: %s (reads %#02x)\n", p->name, reg, value,
		    back == 0xff ? "no answer" : "does not read back", back);
		return false;
	}
	return true;
}

static bool
cam_pm8008_chip_on(struct cam_pm8008 *p)
{
	uint8_t revid[4] = { 0 }, enable = 0;

	if (!cam_pgpio_drive(p->enable, true)) {
		return false;
	}
	cam_wait_ms(5);
	if (!cam_i2c_read_regs(&cam_pm8008_bus, p->chip, 0x0100, revid, 4, 2)) {
		say("camera: %s does not answer at %#04x after its enable went high\n", p->name, p->chip);
		return false;
	}
	say("camera: %s answers: revision %02x %02x %02x %02x\n", p->name, revid[0], revid[1], revid[2], revid[3]);
	if (!cam_i2c_read_regs(&cam_pm8008_bus, p->chip, CAM_PM8008_CHIP_REG, &enable, 1, 2) || enable == 0xff) {
		say("camera: %s: the chip enable register does not answer (reads %#02x)\n", p->name, enable);
		return false;
	}
	if (!(enable & 1)) {
		p->chip_was_off = true;
		if (!cam_pm8008_put(p, CAM_PM8008_CHIP_REG, (uint8_t)(enable | 1))) {
			return false;
		}
		p->chip_switched = true;
		say("camera: %s's chip enabled\n", p->name);
	}
	return true;
}

static void
cam_pm8008_chip_off(struct cam_pm8008 *p)
{
	uint8_t enable = 0;

	if (!p->chip_switched) {
		return;
	}
	if (cam_i2c_read_regs(&cam_pm8008_bus, p->chip, CAM_PM8008_CHIP_REG, &enable, 1, 2) && enable != 0xff &&
	    cam_pm8008_put(p, CAM_PM8008_CHIP_REG, (uint8_t)(enable & ~1))) {
		p->chip_switched = false;
		say("camera: %s's chip disabled again\n", p->name);
	}
}

static void
cam_pm8008_report(struct cam_pm8008 *p)
{
	static const char *names[8] = { "", "L1", "L2", "L3", "L4", "L5", "L6", "L7" };

	say("camera: %s at I2C %#04x/%#04x as found:\n", p->name, p->chip, p->regs);
	for (unsigned n = 1; n <= 7; n++) {
		uint16_t base = (uint16_t)CAM_PM8008_LDO_BASE(n);
		uint8_t status = 0, vset[2] = { 0, 0 }, mode = 0, enable = 0;

		if (cam_pm8008_get(p, (uint16_t)(base + CAM_PM8008_STATUS1), &status, 1) &&
		    cam_pm8008_get(p, (uint16_t)(base + CAM_PM8008_VSET_LB), vset, 2) &&
		    cam_pm8008_get(p, (uint16_t)(base + CAM_PM8008_MODE_CTL1), &mode, 1) &&
		    cam_pm8008_get(p, (uint16_t)(base + CAM_PM8008_ENABLE), &enable, 1)) {
			say("camera: %s %s: status %#04x (%sready%s, mode %u), %u mV, mode control %#02x, enable %#02x\n",
			    p->name, names[n], status, status & 0x80 ? "" : "not ", status & 0x20 ? ", OVER-CURRENT" : "",
			    status & 3, vset[1] << 8 | vset[0], mode, enable);
		} else {
			say("camera: %s %s: no answer\n", p->name, names[n]);
		}
	}
}

static bool
cam_ldo_on(struct cam_pm8008 *p, uint8_t ldo, uint16_t mv)
{
	uint16_t base = (uint16_t)CAM_PM8008_LDO_BASE(ldo);
	uint8_t enable = 0, mode = 0, status = 0;

	if (!cam_rail_allowed(p, ldo, mv)) {
		say("camera: %s LDO%u at %u mV is not one of the tree's rails: not written\n", p->name, ldo, mv);
		return false;
	}
	if (!cam_pm8008_get(p, (uint16_t)(base + CAM_PM8008_ENABLE), &enable, 1) ||
	    !cam_pm8008_get(p, (uint16_t)(base + CAM_PM8008_MODE_CTL1), &mode, 1) || enable == 0xff || mode == 0xff) {
		say("camera: %s LDO%u: the enable or mode register does not answer (enable %#02x, mode %#02x)\n", p->name, ldo,
		    enable, mode);
		return false;
	}
	if (enable & 0x80) {
		say("camera: %s LDO%u was on already (enable %#02x), left as it is\n", p->name, ldo, enable);
		return true;
	}
	if (!cam_pm8008_put(p, (uint16_t)(base + CAM_PM8008_VSET_LB), (uint8_t)mv) ||
	    !cam_pm8008_put(p, (uint16_t)(base + CAM_PM8008_VSET_UB), (uint8_t)(mv >> 8)) ||
	    !cam_pm8008_put(p, (uint16_t)(base + CAM_PM8008_MODE_CTL1), (uint8_t)((mode & ~7) | 7)) ||
	    !cam_pm8008_put(p, (uint16_t)(base + CAM_PM8008_ENABLE), (uint8_t)(enable | 0x80))) {
		return false;
	}
	p->ldo_switched[ldo] = true;
	for (unsigned tries = 0; tries < 10; tries++) {
		cam_spin_us(20 + mv * 1000 / 38400 + 1);
		if (!cam_pm8008_get(p, (uint16_t)(base + CAM_PM8008_STATUS1), &status, 1)) {
			say("camera: %s LDO%u: the status does not answer\n", p->name, ldo);
			return false;
		}
		if (status == 0xff) {
			continue;
		}
		if (status & 0x20) {
			uint8_t again = 0;

			cam_wait_ms(2);
			if (cam_pm8008_get(p, (uint16_t)(base + CAM_PM8008_STATUS1), &again, 1) && !(again & 0x20)) {
				say("camera: %s LDO%u: status %#04x, and %#04x when asked again\n", p->name, ldo, status, again);
				status = again;
			} else {
				say("camera: %s LDO%u reports over-current (status %#04x)\n", p->name, ldo, status);
				return false;
			}
		}
		if (status & 0x80) {
			say("camera: %s LDO%u on at %u mV (status %#04x)\n", p->name, ldo, mv, status);
			return true;
		}
	}
	say("camera: %s LDO%u not ready (status %#04x)\n", p->name, ldo, status);
	return false;
}

static void
cam_ldo_off(struct cam_pm8008 *p, uint8_t ldo)
{
	uint16_t base = (uint16_t)CAM_PM8008_LDO_BASE(ldo);
	uint8_t enable = 0;

	if (!p->ldo_switched[ldo]) {
		return;                         /* it was on before we came: it stays on */
	}
	if (cam_pm8008_get(p, (uint16_t)(base + CAM_PM8008_ENABLE), &enable, 1) && enable != 0xff && (enable & 0x80) &&
	    cam_pm8008_put(p, (uint16_t)(base + CAM_PM8008_ENABLE), (uint8_t)(enable & ~0x80))) {
		p->ldo_switched[ldo] = false;
		say("camera: %s LDO%u off\n", p->name, ldo);
	} else {
		say("camera: %s LDO%u: the off write did not read back\n", p->name, ldo);
	}
}

/* ---- the camera's clocks: GCC's camera branches, titan top GDSC and the MCLK roots ------------------- */

struct cam_mclk {
	const char *    name;
	uint32_t        cmd, cbcr;
	unsigned        pin;
	bool            was_on, switched;
	uint32_t        found_cfg, found_cbcr, found_pin_cfg;
};

static struct cam_mclk cam_mclk[4] = {
	{ .name = "MCLK0", .cmd = CAM_MCLK0_CMD, .cbcr = CAM_MCLK0_CBCR, .pin = 13 },
	{ .name = "MCLK1", .cmd = CAM_MCLK1_CMD, .cbcr = CAM_MCLK1_CBCR, .pin = 14 },
	{ .name = "MCLK2", .cmd = CAM_MCLK2_CMD, .cbcr = CAM_MCLK2_CBCR, .pin = 15 },
	{ .name = "MCLK3", .cmd = CAM_MCLK3_CMD, .cbcr = CAM_MCLK3_CBCR, .pin = 16 },
};

static bool
cam_gcc_camera_on(void)
{
	uint32_t ahb = rd(cam_gcc + CAM_GCC_CAMERA_AHB), xo = rd(cam_gcc + CAM_GCC_CAMERA_XO);

	if (!(ahb & 1)) {
		wr(cam_gcc + CAM_GCC_CAMERA_AHB, ahb | 1);
	}
	if (!(xo & 1)) {
		wr(cam_gcc + CAM_GCC_CAMERA_XO, xo | 1);
	}
	for (unsigned waited = 0; waited < 100; waited += 10) {
		if (!(rd(cam_gcc + CAM_GCC_CAMERA_AHB) & CAM_GCC_BRANCH_OFF) &&
		    !(rd(cam_gcc + CAM_GCC_CAMERA_XO) & CAM_GCC_BRANCH_OFF)) {
			break;
		}
		cam_spin_us(10);
	}
	/* These two are always-on branches; their halt bits are a report, not a condition. */
	say("camera: GCC's camera AHB %#x and XO %#x enabled\n", rd(cam_gcc + CAM_GCC_CAMERA_AHB),
	    rd(cam_gcc + CAM_GCC_CAMERA_XO));
	return true;
}

static bool cam_gdsc_switched;

static bool
cam_gdsc_on(void)
{
	uint32_t found = rd(cam_camcc + CAM_TITAN_TOP_GDSCR);

	if (!(found & CAM_GDSC_COLLAPSE) && (found & CAM_GDSC_ON)) {
		say("camera: the titan top power domain was on already (%#x)\n", found);
		return true;
	}
	wr(cam_camcc + CAM_TITAN_TOP_GDSCR, found & ~CAM_GDSC_COLLAPSE);
	cam_gdsc_switched = true;
	cam_spin_us(1);
	for (unsigned waited = 0; waited < 100; waited += 10) {
		if (rd(cam_camcc + CAM_TITAN_TOP_GDSCR) & CAM_GDSC_ON) {
			say("camera: the titan top power domain on (%#x)\n", rd(cam_camcc + CAM_TITAN_TOP_GDSCR));
			return true;
		}
		cam_spin_us(10);
	}
	say("camera: the titan top power domain does not come on (%#x)\n", rd(cam_camcc + CAM_TITAN_TOP_GDSCR));
	return false;
}

static void
cam_gdsc_off(void)
{
	if (!cam_gdsc_switched) {
		return;
	}
	wr(cam_camcc + CAM_TITAN_TOP_GDSCR, rd(cam_camcc + CAM_TITAN_TOP_GDSCR) | CAM_GDSC_COLLAPSE);
	for (unsigned waited = 0; waited < 100; waited += 10) {
		if (!(rd(cam_camcc + CAM_TITAN_TOP_GDSCR) & CAM_GDSC_ON)) {
			break;
		}
		cam_spin_us(10);
	}
	cam_gdsc_switched = false;
	say("camera: the titan top power domain off (%#x)\n", rd(cam_camcc + CAM_TITAN_TOP_GDSCR));
}

/* MCLK at 19.2 MHz from the crystal (the tree's rate for sensors 0, 1 and 3). */
static bool
cam_mclk_on(struct cam_mclk *m, uint16_t khz)
{
	uint32_t cfg, branch;

	if (m->switched || m->was_on) {
		return true;
	}
	if (khz != 19200) {
		say("camera: %s: the module asks %u kHz; this first version runs the RCG from the crystal at "
		    "19200 kHz for the ID read only\n", m->name, khz);
	}
	m->found_cfg = rd(cam_camcc + CAM_MCLK_CFG_RCGR(m->cmd));
	m->found_cbcr = rd(cam_camcc + m->cbcr);
	m->found_pin_cfg = rd(CAM_GPIO_CFG(m->pin));
	if (m->found_cbcr & 1) {
		m->was_on = true;
		say("camera: %s was on already (RCG cfg %#x, branch %#x): left as it is\n", m->name, m->found_cfg, m->found_cbcr);
		return true;
	}
	m->switched = true;             /* from here, a failure is taken back by cam_mclk_off() */
	cfg = (m->found_cfg & ~0x371fu) | (0u << 8) | 1u;
	wr(cam_camcc + CAM_MCLK_CFG_RCGR(m->cmd), cfg);
	wr(cam_camcc + m->cmd, rd(cam_camcc + m->cmd) | CAM_RCG_UPDATE);
	for (unsigned waited = 0; (rd(cam_camcc + m->cmd) & CAM_RCG_UPDATE) && waited < 500; waited += 10) {
		cam_spin_us(10);
	}
	if (rd(cam_camcc + m->cmd) & CAM_RCG_UPDATE) {
		say("camera: %s: the RCG does not take its configuration (cmd %#x)\n", m->name,
		    rd(cam_camcc + m->cmd));
		return false;
	}
	branch = m->found_cbcr | 1;
	wr(cam_camcc + m->cbcr, branch);
	for (unsigned waited = 0; (rd(cam_camcc + m->cbcr) & CAM_GCC_BRANCH_OFF) && waited < 500; waited += 10) {
		cam_spin_us(10);
	}
	if (rd(cam_camcc + m->cbcr) & CAM_GCC_BRANCH_OFF) {
		say("camera: %s: the branch does not come on (%#x)\n", m->name, rd(cam_camcc + m->cbcr));
		return false;
	}
	wr(CAM_GPIO_CFG(m->pin), CAM_CFG_FUNCTION(1) | CAM_CFG_DRIVE_MA(4));     /* cam_mclk, no pull, 4 mA */
	say("camera: %s on at 19.200 MHz from the crystal, on gpio%u (RCG cfg %#x, branch %#x)\n", m->name, m->pin,
	    rd(cam_camcc + CAM_MCLK_CFG_RCGR(m->cmd)), rd(cam_camcc + m->cbcr));
	return true;
}

static void
cam_mclk_off(struct cam_mclk *m)
{
	if (!m->switched) {
		return;
	}
	wr(cam_camcc + m->cbcr, rd(cam_camcc + m->cbcr) & ~1u);
	for (unsigned waited = 0; !(rd(cam_camcc + m->cbcr) & CAM_GCC_BRANCH_OFF) && waited < 500; waited += 10) {
		cam_spin_us(10);
	}
	wr(cam_camcc + CAM_MCLK_CFG_RCGR(m->cmd), m->found_cfg);
	wr(cam_camcc + m->cmd, rd(cam_camcc + m->cmd) | CAM_RCG_UPDATE);
	wr(CAM_GPIO_CFG(m->pin), m->found_pin_cfg);
	m->switched = false;
	say("camera: %s off (branch %#x, RCG cfg %#x as found)\n", m->name, rd(cam_camcc + m->cbcr),
	    rd(cam_camcc + CAM_MCLK_CFG_RCGR(m->cmd)));
}

/* ---- the five sensors: the tree's own wiring, and what this port expects to find ---------------------- */

struct cam_sensor {
	const char *            name;
	const char *            role;
	uint8_t                 cci, master, csiphy;
	uint8_t                 sda, scl;
	struct cam_mclk *       mclk;
	uint16_t                mclk_khz;
	bool                    reset_pmic;
	uint8_t                 reset_slave, reset_periph;
	uint8_t                 reset_gpio;     /* TLMM, when reset_pmic is false */
	int8_t                  enable_gpio;    /* CAM_FRONT_EN / CAM_MONO_EN, -1 when none */
	struct cam_pm8008 *     pm;
	struct cam_rail         rails[4];
	uint8_t                 address;        /* the likely 7-bit address; the scan is what decides */
	uint16_t                id_reg;
	uint8_t                 id_address_bytes;
	uint16_t                id;
};

static const struct cam_sensor cam_sensors[] = {
	{       /* fragment@42 qcom,cam-sensor@0: csiphy 0, cci 0 master 0, gpio13 MCLK0, PM8150 GPIO1 reset */
		.name = "rear main (IMX586, 48 MP)", .role = "main", .cci = 0, .master = 0, .csiphy = 0, .sda = 17, .scl = 18,
		.mclk = &cam_mclk[0], .mclk_khz = 19200, .reset_pmic = true, .reset_slave = CAM_PM8150_SLAVE,
		.reset_periph = CAM_PMIC_GPIO_PERIPH(1), .reset_gpio = 0, .enable_gpio = -1, .pm = &cam_pm8008_b,
		.rails = { { 1, 1100, "VDIG" }, { 7, 2900, "VANA" }, { 3, 2800, "VAF" }, { 4, 1800, "V_CUSTOM1" } },
		.address = 0x1a, .id_reg = 0x0016, .id_address_bytes = 2, .id = 0x0586,
	},
	{       /* cam-sensor@1: csiphy 1, cci 0 master 1, gpio14 MCLK1, TLMM gpio30 reset */
		.name = "rear ultra-wide (IMX319, 8 MP)", .role = "ultra-wide", .cci = 0, .master = 1, .csiphy = 1, .sda = 19, .scl = 20,
		.mclk = &cam_mclk[1], .mclk_khz = 19200, .reset_pmic = false, .reset_gpio = 30, .enable_gpio = -1,
		.pm = &cam_pm8008_a, .rails = { { 1, 1100, "VDIG" }, { 4, 2800, "VANA" } },
		.address = 0x10, .id_reg = 0x0016, .id_address_bytes = 2, .id = 0x0319,
	},
	{       /* cam-sensor@2: csiphy 2, cci 1 master 1, gpio15 MCLK2 24 MHz, TLMM gpio12 reset + gpio29 front enable */
		.name = "front (IMX471, 16 MP)", .role = "front", .cci = 1, .master = 1, .csiphy = 2, .sda = 33, .scl = 34,
		.mclk = &cam_mclk[2], .mclk_khz = 24000, .reset_pmic = false, .reset_gpio = 12, .enable_gpio = 29,
		.pm = &cam_pm8008_b, .rails = { { 2, 1050, "VDIG" }, { 5, 2800, "VANA" } },
		.address = 0x10, .id_reg = 0x0016, .id_address_bytes = 2, .id = 0x0471,
	},
	{       /* cam-sensor@3: csiphy 3, cci 1 master 0, gpio16 MCLK3, TLMM gpio23 reset; the symbol table calls it tele */
		.name = "rear telephoto (S5K3M5, 13 MP)", .role = "tele", .cci = 1, .master = 0, .csiphy = 3, .sda = 31, .scl = 32,
		.mclk = &cam_mclk[3], .mclk_khz = 19200, .reset_pmic = false, .reset_gpio = 23, .enable_gpio = -1,
		.pm = &cam_pm8008_a, .rails = { { 2, 1050, "VDIG" }, { 6, 2800, "VANA" } },
		.address = 0x2d, .id_reg = 0x0000, .id_address_bytes = 2, .id = 0x30d5,
	},
	{       /* cam-sensor@4: csiphy 2, cci 1 master 0, gpio15 MCLK2 24 MHz, gpio12 reset + gpio29 mono enable */
		.name = "rear mono (GalaxyCore GC02Mx, 2 MP)", .role = "mono", .cci = 1, .master = 0, .csiphy = 2, .sda = 31, .scl = 32,
		.mclk = &cam_mclk[2], .mclk_khz = 24000, .reset_pmic = false, .reset_gpio = 12, .enable_gpio = 29,
		.pm = &cam_pm8008_a, .rails = { { 5, 2800, "VANA" } },
		.address = 0x37, .id_reg = 0x00f0, .id_address_bytes = 1, .id = 0x02d0,
	},
};

#define CAM_SENSOR_COUNT (sizeof(cam_sensors) / sizeof(cam_sensors[0]))

/* ---- the bus scan: what answers, what its chip ID is, and what the EEPROM says ------------------------ */

static uint16_t
cam_read16(struct cam_bus *bus, uint8_t address, uint16_t reg, unsigned address_bytes, bool *answered)
{
	uint8_t data[2] = { 0, 0 };

	*answered = cam_i2c_read_regs(bus, address, reg, data, 2, address_bytes);
	return (uint16_t)(data[0] << 8 | data[1]);
}

static void
cam_scan_bus(const struct cam_sensor *s)
{
	struct cam_bus bus = { .name = "the sensor's CCI bus", .sda = s->sda, .scl = s->scl };
	unsigned found = 0;

	say("camera: %s: scanning CCI%d master%d, gpio%u (data) and gpio%u (clock), addresses 0x08..0x77\n", s->name, s->cci,
	    s->master, s->sda, s->scl);
	if (!cam_bus_take(&bus)) {
		say("camera: %s: the bus is not at rest; the scan is skipped\n", s->name);
		return;
	}
	cam_bus_give_back(&bus);
	for (unsigned address = 0x08; address <= 0x77; address++) {
		if (!cam_i2c_probe(&bus, (uint8_t)address)) {
			continue;
		}
		found++;
		say("camera: %s: [%#04x] answers\n", s->name, address);
		if (address < 0x50) {
			bool aid = false, a16 = false, a00 = false;
			uint16_t idid = cam_read16(&bus, (uint8_t)address, s->id_reg, s->id_address_bytes, &aid);
			uint16_t id16 = cam_read16(&bus, (uint8_t)address, 0x0016, 2, &a16);
			uint16_t id00 = cam_read16(&bus, (uint8_t)address, 0x0000, 2, &a00);

			say("camera: %s: [%#04x] id@%#04x = %#06x%s, 0x0016 = %#06x%s, 0x0000 = %#06x%s\n", s->name, address,
			    s->id_reg, aid ? idid : 0xffff, aid ? "" : " (no answer)", a16 ? id16 : 0xffff, a16 ? "" : " (no answer)",
			    a00 ? id00 : 0xffff, a00 ? "" : " (no answer)");
			if (strcmp(s->role, "mono") == 0) {
				/* An OmniVision part answers on its 16-bit register map; its ID pair is at
				 * 0x300a/0x300b, not at 0x0016 (OV02A10 datasheet). */
				bool ahi = false, alo = false;
				uint16_t hi = cam_read16(&bus, (uint8_t)address, 0x300a, 2, &ahi);
				uint16_t lo = cam_read16(&bus, (uint8_t)address, 0x300b, 2, &alo);

				say("camera: %s: [%#04x] 0x300a = %#06x%s, 0x300b = %#06x%s (the OV 16-bit ID pair)\n",
				    s->name, address, ahi ? hi : 0xffff, ahi ? "" : " (no answer)",
				    alo ? lo : 0xffff, alo ? "" : " (no answer)");
				/* GalaxyCore parts answer at 0x37 and put their ID at 0x03f0, read both
				 * with a 16-bit register address and with an 8-bit one. */
				bool agc = false, agc8 = false;
				uint16_t gcid = cam_read16(&bus, (uint8_t)address, 0x03f0, 2, &agc);
				uint16_t gcid8 = cam_read16(&bus, (uint8_t)address, 0x00f0, 1, &agc8);

				say("camera: %s: [%#04x] 0x03f0 = %#06x%s, 0xf0 = %#06x%s (the GalaxyCore ID)\n",
				    s->name, address, agc ? gcid : 0xffff, agc ? "" : " (no answer)",
				    agc8 ? gcid8 : 0xffff, agc8 ? "" : " (no answer)");
			}
			if (s->id != 0 && aid && idid == s->id) {
				say("camera: %s: the expected chip ID %#06x is there: this is the %s\n", s->name, s->id, s->role);
			}
		}
		if (address >= 0x50 && address <= 0x57) {
			uint8_t data16[8] = { 0 }, data8[8] = { 0 };
			bool a16 = cam_i2c_read_regs(&bus, (uint8_t)address, 0x0000, data16, sizeof(data16), 2);
			bool a8 = cam_i2c_read_regs(&bus, (uint8_t)address, 0x0000, data8, sizeof(data8), 1);

			if (a16) {
				say("camera: %s: EEPROM [%#04x] 0x0000 (16-bit address): %02x %02x %02x %02x %02x %02x %02x %02x\n",
				    s->name, address, data16[0], data16[1], data16[2], data16[3], data16[4], data16[5], data16[6],
				    data16[7]);
			}
			if (a8) {
				say("camera: %s: EEPROM [%#04x] 0x00 (8-bit address): %02x %02x %02x %02x %02x %02x %02x %02x\n",
				    s->name, address, data8[0], data8[1], data8[2], data8[3], data8[4], data8[5], data8[6], data8[7]);
			}
			if (!a16 && !a8) {
				say("camera: %s: EEPROM [%#04x] answers but 0x0000 cannot be read\n", s->name, address);
			}
		}
	}
	if (found == 0) {
		say("camera: %s: nothing answers on CCI%d master%d; the rails, the clock or the reset line is the "
		    "first thing to check\n", s->name, s->cci, s->master);
	} else {
		say("camera: %s: %u address(es) answered\n", s->name, found);
	}
}

/* ---- the camera I/O rail: PM8150L L1, voted through RPMh --------------------------------------------- */

/*
 * The overlay's five sensors all take their cam_vio-supply from pm8150l_l1
 * (dtbo.bin.01.dts:8133, the sensor phandles' __fixups__); that node is
 * rpmh-regulator-ldoc1, a qcom,rpmh-xob-regulator fixed at 1800 mV
 * (android-merged.dts:12773-12785). The PM8008 rails alone do not power the
 * sensor's I/O ring, and with only those on the CCI bus (gpio17/18) reads both
 * lines low even though the TLMM's pull-ups are set (logs/164459-run.log).
 * The vote is the same RPMh command the Bluetooth driver sends its rails with
 * (hakios.wcnbt.c): the command database's "ldoc1" cell, the millivolts, as
 * rpmh_regulator_vrm_set_voltage() sends them.
 */

#define CAM_CMD_DB_POINTER      0x00c3f000cUL
#define CAM_CMD_DB_MAGIC        0x0c0330dbu
#define CAM_CMD_DB_HEADER       148
#define CAM_CMD_DB_MAX_RES      256

#define CAM_RSC_TCS_PHYS        0x018220d00UL
#define CAM_RSC_TCS_SIZE        0x3000
#define CAM_TCS_STRIDE          0x2a0UL
#define CAM_TCS(slot, reg, cmd) (cam_rsc_tcs + CAM_TCS_STRIDE * (slot) + (reg) + 0x14UL * (cmd))
#define CAM_TCS_CONTROL         0x14
#define CAM_TCS_STATUS          0x18
#define CAM_TCS_WAIT_FOR_CMPL   0x10
#define CAM_TCS_ENABLE          0x1c
#define CAM_TCS_MSGID           0x30
#define CAM_TCS_ADDR            0x34
#define CAM_TCS_DATA            0x38
#define CAM_TCS_CMD_STATUS      0x3c
#define CAM_RSC_IRQ_STATUS      0x04
#define CAM_RSC_IRQ_CLEAR       0x08
#define CAM_AMC_MODE_ENABLE     (1u << 16)
#define CAM_AMC_MODE_TRIGGER    (1u << 24)
#define CAM_CMD_LEN             8
#define CAM_CMD_RESP_REQ        (1u << 8)
#define CAM_CMD_WRITE           (1u << 16)
#define CAM_RSC_SLOTS           2
#define CAM_RSC_POLLS           2000

struct cam_cmd_res {
	char name[9];
	uint32_t address;
};

static struct cam_cmd_res cam_cmd_res[CAM_CMD_DB_MAX_RES];
static unsigned cam_cmd_res_count;
static bool cam_cmd_res_read;
static vm_offset_t cam_rsc_tcs;

static bool
cam_cmd_id_shaped(uint32_t first, uint32_t second)
{
	bool padded = false;

	for (unsigned c = 0; c < 8; c++) {
		uint32_t word = c < 4 ? first : second;
		uint8_t ch = (uint8_t) ((word >> (8 * (c & 3))) & 0xff);

		if (ch == 0) {
			padded = true;
			continue;
		}
		if (padded) {
			return false;
		}
		if (c == 0) {
			if (ch < 'a' || ch > 'z') {
				return false;
			}
			continue;
		}
		if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '.' || ch == '_')) {
			return false;
		}
	}
	return (uint8_t) first != 0;
}

static bool
cam_cmdb_read(void)
{
	vm_offset_t pointer, db;
	uint32_t base, length;

	if (cam_cmd_res_read) {
		return cam_cmd_res_count != 0;
	}
	cam_cmd_res_read = true;
	pointer = ml_io_map(CAM_CMD_DB_POINTER, 0x1000);
	if (pointer == 0) {
		say("camera: the command database pointer at %#x did not map\n", CAM_CMD_DB_POINTER);
		return false;
	}
	base = rd(pointer);
	length = rd(pointer + 4);
	if (base < 0x1000 || base + length <= base || length == 0 || length > 0x200000) {
		say("camera: the command database pointer reads %#x with %u bytes, not a table\n", base, length);
		return false;
	}
	db = ml_io_map(base, length);
	if (db == 0) {
		say("camera: the command database at %#x did not map\n", base);
		return false;
	}
	if (rd(db + 4) != CAM_CMD_DB_MAGIC) {
		say("camera: the table at %#x has magic %#x, not the command database's\n", base, rd(db + 4));
		return false;
	}
	for (unsigned at = CAM_CMD_DB_HEADER; at + 24 <= length; at += 4) {
		uint32_t first = rd(db + at), second = rd(db + at + 4);
		uint32_t address = rd(db + at + 16);

		if (!cam_cmd_id_shaped(first, second) || address == 0 || address >= 0x1000000) {
			continue;
		}
		if (cam_cmd_res_count == CAM_CMD_DB_MAX_RES) {
			break;
		}
		struct cam_cmd_res *res = &cam_cmd_res[cam_cmd_res_count++];

		for (unsigned c = 0; c < 8; c++) {
			uint32_t word = c < 4 ? first : second;

			res->name[c] = (char) ((word >> (8 * (c & 3))) & 0xff);
		}
		res->name[8] = 0;
		res->address = address;
	}
	return cam_cmd_res_count != 0;
}

static uint32_t
cam_cmdb_look(const char *name)
{
	for (unsigned i = 0; i < cam_cmd_res_count; i++) {
		if (strcmp(cam_cmd_res[i].name, name) == 0) {
			return cam_cmd_res[i].address;
		}
	}
	return 0;
}

static bool
cam_rpmh_map(void)
{
	if (cam_rsc_tcs != 0) {
		return true;
	}
	cam_rsc_tcs = ml_io_map(CAM_RSC_TCS_PHYS, CAM_RSC_TCS_SIZE);
	if (cam_rsc_tcs == 0) {
		say("camera: the RPMh command slots at %#lx did not map\n", (unsigned long) CAM_RSC_TCS_PHYS);
		return false;
	}
	return true;
}

static bool
cam_tcs_write_sync(vm_offset_t where, uint32_t want)
{
	for (unsigned tries = 0; tries < 1000; tries++) {
		wr(where, want);
		if (rd(where) == want) {
			return true;
		}
		cam_spin_us(1);
	}
	say("camera: RPMh: %#lx would not take %#x (reads %#x)\n", (unsigned long) where, want, rd(where));
	return false;
}

static bool
cam_tcs_send(uint32_t cell, uint32_t data)
{
	unsigned slot;
	bool found = false;

	if (cam_rsc_tcs == 0) {
		return false;
	}
	for (slot = 0; slot < CAM_RSC_SLOTS; slot++) {
		if (rd(CAM_TCS(slot, CAM_TCS_STATUS, 0)) != 0) {
			found = true;
			break;
		}
	}
	if (!found) {
		say("camera: RPMh: no slot has completed a transaction (both read %#x)\n",
		    rd(CAM_TCS(0, CAM_TCS_STATUS, 0)));
		return false;
	}
	wr(cam_rsc_tcs + CAM_RSC_IRQ_CLEAR, 1u << slot);
	wr(CAM_TCS(slot, CAM_TCS_ENABLE, 0), 0);
	wr(CAM_TCS(slot, CAM_TCS_WAIT_FOR_CMPL, 0), 0);
	wr(CAM_TCS(slot, CAM_TCS_MSGID, 0), CAM_CMD_LEN | CAM_CMD_RESP_REQ | CAM_CMD_WRITE);
	wr(CAM_TCS(slot, CAM_TCS_ADDR, 0), cell);
	wr(CAM_TCS(slot, CAM_TCS_DATA, 0), data);
	wr(CAM_TCS(slot, CAM_TCS_WAIT_FOR_CMPL, 0), 1);
	wr(CAM_TCS(slot, CAM_TCS_ENABLE, 0), 1);
	{
		uint32_t ctrl = rd(CAM_TCS(slot, CAM_TCS_CONTROL, 0));

		ctrl &= ~CAM_AMC_MODE_TRIGGER;
		cam_tcs_write_sync(CAM_TCS(slot, CAM_TCS_CONTROL, 0), ctrl);
		ctrl &= ~CAM_AMC_MODE_ENABLE;
		cam_tcs_write_sync(CAM_TCS(slot, CAM_TCS_CONTROL, 0), ctrl);
		cam_tcs_write_sync(CAM_TCS(slot, CAM_TCS_CONTROL, 0), CAM_AMC_MODE_ENABLE);
		wr(CAM_TCS(slot, CAM_TCS_CONTROL, 0), CAM_AMC_MODE_ENABLE | CAM_AMC_MODE_TRIGGER);
	}
	for (unsigned polls = 0; polls < CAM_RSC_POLLS; polls++) {
		uint32_t status = rd(CAM_TCS(slot, CAM_TCS_CMD_STATUS, 0));

		if (status & (1u << 16)) {
			wr(cam_rsc_tcs + CAM_RSC_IRQ_CLEAR, 1u << slot);
			return true;
		}
		if (rd(cam_rsc_tcs + CAM_RSC_IRQ_STATUS) & (1u << slot)) {
			status = rd(CAM_TCS(slot, CAM_TCS_CMD_STATUS, 0));
			wr(cam_rsc_tcs + CAM_RSC_IRQ_CLEAR, 1u << slot);
			say("camera: RPMh: the slot answered by interrupt, command status %#x\n", status);
			return true;
		}
		cam_spin_us(100);
	}
	say("camera: RPMh: the vote never completed (command status %#x)\n",
	    rd(CAM_TCS(slot, CAM_TCS_CMD_STATUS, 0)));
	return false;
}

/* PM8150L L1 is an XOB regulator: it has an enable vote and no voltage
 * register at all (vendor rpmh-regulator.c: rpmh_regulator_xob_ops has only
 * enable/disable/is_enabled, and RPMH_REGULATOR_REG_XOB_ENABLE is
 * RPMH_REGULATOR_REG_VRM_ENABLE, one 4-byte cell past the resource address).
 * The node carries no qcom,init-enable (android-merged.dts:12773-12785), so
 * the boot default is the firmware's choice, not ours. */
#define CAM_VIO_ENABLE_OFFSET 4

static uint32_t cam_vio_cell;
static bool cam_vio_voted;

static bool
cam_vio_on(void)
{
	uint32_t cell;

	if (!cam_cmdb_read()) {
		say("camera: VIO: the command database is not readable; PM8150L L1 stays as the boot left it\n");
		return false;
	}
	if (!cam_rpmh_map()) {
		return false;
	}
	cell = cam_cmdb_look("ldoc1");
	if (cell == 0) {
		say("camera: VIO: the command database has no \"ldoc1\"; PM8150L L1 stays as the boot left it\n");
		return false;
	}
	if (!cam_tcs_send(cell + CAM_VIO_ENABLE_OFFSET, 1)) {
		say("camera: VIO: the enable vote for PM8150L L1 (cell %#x) did not complete\n", cell);
		return false;
	}
	cam_vio_cell = cell;
	cam_vio_voted = true;
	say("camera: VIO: PM8150L L1 (ldoc1 at %#x, the sensors' cam_vio) enabled through its XOB enable vote\n",
	    cell);
	return true;
}

static void
cam_vio_off(void)
{
	if (!cam_vio_voted) {
		return;
	}
	cam_vio_voted = false;
	if (cam_tcs_send(cam_vio_cell + CAM_VIO_ENABLE_OFFSET, 0)) {
		say("camera: VIO: PM8150L L1 disabled again\n");
	} else {
		say("camera: VIO: PM8150L L1 is left enabled (the disable vote did not complete)\n");
	}
}

/* ---- power-up and power-down, in the tree's order, every step said out loud --------------------------- */

struct cam_power_state {
	struct cam_pgpio        reset;
	bool                    reset_is_pmic, reset_low, reset_high;
	uint32_t                reset_cfg, reset_io;
	bool                    reset_tlmm_saved;
	uint32_t                enable_cfg, enable_io;
	bool                    enable_saved;
	bool                    rails_on[4];
};

/* The front and the mono sensor share gpio29 (CAM_FRONT_EN / CAM_MONO_EN in the overlay): one
 * level powers one module. The front answers with it high and the mono (0x37, GC02Mx) with it
 * low -- logs/182953-run.log is the round that showed both. cam_mono_high forces the other
 * level for an A/B. */
static bool cam_mono_high;

static void
cam_power_down(const struct cam_sensor *s, struct cam_power_state *st)
{
	if (st->reset_high) {
		if (st->reset_is_pmic) {
			(void)cam_pgpio_drive(&st->reset, false);
		} else if (st->reset_tlmm_saved) {
			wr(CAM_GPIO_IO(s->reset_gpio), 0);
		}
		st->reset_high = false;
	}
	cam_mclk_off(s->mclk);
	for (int i = 3; i >= 0; i--) {
		if (st->rails_on[i]) {
			cam_ldo_off(s->pm, s->rails[i].ldo);
			st->rails_on[i] = false;
		}
	}
	cam_pm8008_chip_off(s->pm);
	cam_vio_off();
	if (st->enable_saved) {
		wr(CAM_GPIO_IO(s->enable_gpio), st->enable_io & 2);
		wr(CAM_GPIO_CFG(s->enable_gpio), st->enable_cfg);
		st->enable_saved = false;
	}
	if (st->reset_is_pmic) {
		cam_pgpio_restore(&st->reset);
	} else if (st->reset_tlmm_saved) {
		wr(CAM_GPIO_IO(s->reset_gpio), st->reset_io & 2);
		wr(CAM_GPIO_CFG(s->reset_gpio), st->reset_cfg);
		st->reset_tlmm_saved = false;
	}
	cam_gdsc_off();
	say("camera: %s: powered down and every pin put back\n", s->name);
}

static bool
cam_power_up(const struct cam_sensor *s, bool leave_on)
{
	struct cam_power_state st = { 0 };
	unsigned rail_count = 0;

	say("camera: ==== %s: CCI%d master%d, CSIPHY%d, MCLK %u kHz, %s\n", s->name, s->cci, s->master, s->csiphy, s->mclk_khz,
	    s->reset_pmic ? "reset from the PMIC" : "reset from the TLMM");
	if (!cam_map()) {
		return false;
	}
	if (!cam_gcc_camera_on()) {
		goto fail;
	}
	if (!cam_gdsc_on()) {
		goto fail;
	}
	/* The sensors' cam_vio comes from PM8150L L1, not from either PM8008. It is
	 * voted, not gated: a failure here does not stop the scan, which is what says
	 * whether the I/O ring is powered. */
	(void)cam_vio_on();
	if (s->reset_pmic) {
		st.reset.name = "the sensor's PMIC reset";
		st.reset.slave = s->reset_slave;
		st.reset.periph = s->reset_periph;
		st.reset_is_pmic = true;
		if (!cam_pgpio_drive(&st.reset, false)) {
			goto fail;
		}
		st.reset_low = true;
	} else {
		st.reset_cfg = rd(CAM_GPIO_CFG(s->reset_gpio));
		st.reset_io = rd(CAM_GPIO_IO(s->reset_gpio));
		st.reset_tlmm_saved = true;
		wr(CAM_GPIO_IO(s->reset_gpio), 0);
		wr(CAM_GPIO_CFG(s->reset_gpio), CAM_CFG_OUTPUT | CAM_CFG_DRIVE_MA(2));
		st.reset_low = true;
		say("camera: %s: gpio%u (reset) driven low\n", s->name, s->reset_gpio);
	}
	if (s->enable_gpio >= 0) {
		bool mono = strcmp(s->role, "mono") == 0;
		bool high = !mono || cam_mono_high;

		st.enable_cfg = rd(CAM_GPIO_CFG(s->enable_gpio));
		st.enable_io = rd(CAM_GPIO_IO(s->enable_gpio));
		st.enable_saved = true;
		wr(CAM_GPIO_IO(s->enable_gpio), high ? 2 : 0);
		wr(CAM_GPIO_CFG(s->enable_gpio), CAM_CFG_OUTPUT | CAM_CFG_DRIVE_MA(2));
		say("camera: %s: gpio%u (%s) driven %s%s\n", s->name, s->enable_gpio, s->role,
		    high ? "high" : "low", mono ? " for the mono module" : "");
	}
	if (!cam_pm8008_chip_on(s->pm)) {
		goto fail;
	}
	for (unsigned i = 0; i < sizeof(s->rails) / sizeof(s->rails[0]) && s->rails[i].what != NULL; i++) {
		rail_count++;
		st.rails_on[i] = true;          /* cam_ldo_off() is a no-op if this rail never switched */
		if (!cam_ldo_on(s->pm, s->rails[i].ldo, s->rails[i].mv)) {
			goto fail;
		}
		cam_wait_ms(1);
	}
	say("camera: %s: %u rails on, waiting 1 ms, then the MCLK\n", s->name, rail_count);
	cam_wait_ms(1);
	if (!cam_mclk_on(s->mclk, s->mclk_khz)) {
		goto fail;
	}
	cam_wait_ms(4);
	say("camera: %s: MCLK running for 4 ms, then out of reset\n", s->name);
	if (st.reset_is_pmic) {
		if (!cam_pgpio_drive(&st.reset, true)) {
			goto fail;
		}
	} else {
		wr(CAM_GPIO_IO(s->reset_gpio), 2);
	}
	st.reset_high = true;
	cam_wait_ms(20);
	say("camera: %s: out of reset for 20 ms; the tree's own address hint is %#04x\n", s->name, s->address);
	cam_scan_bus(s);
	if (leave_on) {
		say("camera: %s: left powered (cam=5): reset high, rails on, MCLK running\n", s->name);
		return true;
	}
	cam_power_down(s, &st);
	return true;

fail:
	say("camera: %s: stopping and putting back what was switched on\n", s->name);
	cam_power_down(s, &st);
	return false;
}

/* ---- cam=1: everything read, nothing written --------------------------------------------------------- */

static void
cam_report(void)
{
	say("camera: sensor table (fragment@42, dtbo.bin.01.dts 5609-5811):\n");
	for (unsigned i = 0; i < CAM_SENSOR_COUNT; i++) {
		say("camera:   %u %-36s CCI%u master%u, CSIPHY%u, gpio%u/%u, MCLK %u kHz on gpio%u, reset %s, PM8008 %s\n", i,
		    cam_sensors[i].name, cam_sensors[i].cci, cam_sensors[i].master, cam_sensors[i].csiphy, cam_sensors[i].sda,
		    cam_sensors[i].scl, cam_sensors[i].mclk_khz, cam_sensors[i].mclk->pin,
		    cam_sensors[i].reset_pmic ? "PMIC" : "TLMM", cam_sensors[i].pm->name);
	}
	say("camera: GCC: camera AHB %#x, camera XO %#x\n", rd(cam_gcc + CAM_GCC_CAMERA_AHB), rd(cam_gcc + CAM_GCC_CAMERA_XO));
	say("camera: camcc: titan top GDSC %#x\n", rd(cam_camcc + CAM_TITAN_TOP_GDSCR));
	for (unsigned i = 0; i < 4; i++) {
		say("camera: camcc: %s RCG cmd %#x cfg %#x, branch %#x\n", cam_mclk[i].name,
		    rd(cam_camcc + cam_mclk[i].cmd), rd(cam_camcc + CAM_MCLK_CFG_RCGR(cam_mclk[i].cmd)),
		    rd(cam_camcc + cam_mclk[i].cbcr));
	}
	for (unsigned pin = 13; pin <= 20; pin++) {
		cam_pin_show(pin, pin <= 16 ? "cam_mclk" : "cci_i2c");
	}
	for (unsigned pin = 31; pin <= 34; pin++) {
		cam_pin_show(pin, "cci_i2c");
	}
	cam_pin_show(51, "SE4 I2C data (PM8008)");
	cam_pin_show(52, "SE4 I2C clock (PM8008)");
	(void)cam_pgpio_open(&cam_main_reset);
	(void)cam_pgpio_open(&cam_pm8008_a_enable);
	(void)cam_pgpio_open(&cam_pm8008_b_enable);
	if (cam_pm8008_a_enable.open && (cam_pm8008_a_enable.found[4] & 0x80)) {
		cam_pm8008_report(&cam_pm8008_a);
	} else {
		say("camera: %s is not enabled: its LDOs are not read\n", cam_pm8008_a.name);
	}
	if (cam_pm8008_b_enable.open && (cam_pm8008_b_enable.found[4] & 0x80)) {
		cam_pm8008_report(&cam_pm8008_b);
	} else {
		say("camera: %s is not enabled: its LDOs are not read\n", cam_pm8008_b.name);
	}
	say("camera: cam=1 is a look only: no register above was written\n");
}

__startup_func
static void
camera_init(void)
{
	uint32_t level = 0;

	if (!PE_parse_boot_argn("cam", &level, sizeof(level)) || level == 0) {
		return;                                /* nothing is touched unless a boot-argument asks */
	}
	if (!cam_map()) {
		return;
	}
	if (level == 1) {
		cam_report();
		return;
	}
	if (level == 2) {
		(void)cam_power_up(&cam_sensors[0], false);
		return;
	}
	if (level == 3) {
		(void)cam_power_up(&cam_sensors[2], false);
		return;
	}
	if (level == 4) {
		for (unsigned i = 0; i < CAM_SENSOR_COUNT; i++) {
			(void)cam_power_up(&cam_sensors[i], false);
		}
		return;
	}
	if (level == 6) {
		/* The mono at its own level, then the other one: the pin is what selects the module. */
		(void)cam_power_up(&cam_sensors[4], false);
		say("camera: mono: the same sensor with gpio29 high\n");
		cam_mono_high = true;
		(void)cam_power_up(&cam_sensors[4], false);
		cam_mono_high = false;
		return;
	}
	if (level >= 5) {
		(void)cam_power_up(&cam_sensors[0], true);
		return;
	}
}
STARTUP(EARLY_BOOT, STARTUP_RANK_LAST, camera_init);
