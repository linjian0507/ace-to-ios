/*
 * The vibration motor of the OPPO Reno Ace (PCLM10): the PM8150B's HAPC block.
 *
 * The runtime device tree (vendor-src/dtb/dtbo.bin.01.dts, notes/remaining-hardware.md) names it
 * `qcom,haptics@c000` with compatible "qcom,haptics" on SPMI slave 3, and gives every number the
 * recipe needs without interpretation: qcom,actuator-type "lra", qcom,lra-vmax-mv 0xd48 (3400 mV),
 * qcom,lra-play-rate-us 0x1a0b (6667 us), qcom,wave-shape "sine". The vendor's driver for this
 * block (drivers/leds/leds-qpnp-haptics.c, fetched to vendor-src/haptic/) is not built on this
 * phone, and the AW86927 driver in drivers/haptic/ belongs to another phone's I2C bus; so the
 * register recipe is the specification, and it is eight writes on a bus this port already drives.
 *
 * hapc=1 reads the block and reports: the module's type and subtype, its live status (busy, short
 * circuit, the auto-resonance tracker) and the configuration as it stands. hapc=2 adds the
 * open-loop buzz: the device tree's own numbers written as the vendor driver writes them (LRA,
 * sine, 3400 mV, 6667 us, direct-drive source), module enable, play; the status registers are
 * read again while the wave is running, and then play and the module enable are put back as they
 * were found. hapc=3 leaves it playing, for an ear to confirm in the morning. Nothing here needs
 * an interrupt: the status registers say whether the motor is running.
 *
 * All accesses go through the PMIC arbiter under hakios_spmi_lock(), and only through a channel
 * the arbiter says system 0 owns, the same path the keys, the charger and the WLED use.
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

/* the HAPC block of the PM8150B, which is SPMI slave 3: the runtime tree's qcom,haptics@c000.
 * The registers and their meanings are the vendor driver's (leds-qpnp-haptics.c), as collected
 * in notes/remaining-hardware.md; SEC_ACCESS (0xd0) must be written before the bank's other
 * registers are touched. */
#define HAPC_SLAVE              3
#define HAPC_PERIPHERAL         0xc0
#define HAPC_STATUS_1           0x0a            /* bit 1 busy, bit 3 short circuit, bit 4 auto-res error */
#define HAPC_AUTO_RES_LO        0x0b
#define HAPC_AUTO_RES_HI        0x0c
#define HAPC_INT_RT_STS         0x10            /* bit 0 short circuit, bit 1 play (read-only) */
#define HAPC_EN_CTL             0x46            /* bit 7 module enable */
#define HAPC_CFG1               0x4c            /* bit 0: 0 = LRA, 1 = ERM */
#define HAPC_CFG2               0x4d            /* bit 0: 0 = sine, 1 = square */
#define HAPC_SEL                0x4e            /* bits [5:4] source: 0 = DIRECT */
#define HAPC_VMAX               0x51            /* round(mV / 116) << 1; 3400 mV = 0x3a */
#define HAPC_ILIM               0x52            /* bit 0: 0 = 400 mA, 1 = 800 mA */
#define HAPC_RATE1              0x54            /* play_rate_us / 5, little-endian: 6667 -> 1333 */
#define HAPC_RATE2              0x55
#define HAPC_PLAY               0x70            /* bit 7 play, bit 0 pause */
#define HAPC_SEC_ACCESS         0xd0
#define HAPC_SEC_UNSECURE       0x78

static vm_offset_t arb_core, arb_cnfg, arb_observer, hapc_write;
static uint32_t hapc_channel = ~0u;
static uint8_t hapc_en_ctl = 0xff;              /* what 0x46 said before we touched anything */

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
spmi_write(uint8_t reg, uint8_t value)
{
	uint64_t start = mach_absolute_time(), now, interval;
	uint32_t status;

	nanoseconds_to_absolutetime(READ_TIMEOUT_US * (uint64_t)NSEC_PER_USEC, &interval);
	hakios_spmi_lock();
	wr(hapc_write + CHANNEL_WDATA0, value);
	wr(hapc_write + CHANNEL_COMMAND, COMMAND_WRITE | (uint32_t)reg << 4);
	do {
		status = rd(hapc_write + CHANNEL_STATUS);
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

/* ---- what the block says, and what to do about it ------------------------------------------------- */

/* One line with everything that matters: whether the motor is running (status1 bit 1, rt_sts bit
 * 1), whether it faulted (short circuit), what the configuration is, and where the auto-resonance
 * tracker got to. Read-only; asked for before, during and after a play. */
static void
hapc_report(const char *when)
{
	static const struct { const char *name; uint8_t reg; } regs[] = {
		{ "rt_sts", HAPC_INT_RT_STS }, { "status1", HAPC_STATUS_1 },
		{ "auto_res", HAPC_AUTO_RES_LO }, { "auto_res_hi", HAPC_AUTO_RES_HI },
		{ "en_ctl", HAPC_EN_CTL }, { "cfg", HAPC_CFG1 }, { "cfg2", HAPC_CFG2 },
		{ "sel", HAPC_SEL }, { "vmax", HAPC_VMAX }, { "ilim", HAPC_ILIM },
		{ "rate1", HAPC_RATE1 }, { "rate2", HAPC_RATE2 }, { "play", HAPC_PLAY },
	};
	uint8_t value[sizeof(regs) / sizeof(regs[0])];
	const char *problem = NULL;
	unsigned i;

	for (i = 0; i < sizeof(regs) / sizeof(regs[0]) && problem == NULL; i++) {
		problem = spmi_read(hapc_channel, regs[i].reg, &value[i], 1);
	}
	if (problem != NULL) {
		say("hapc: %s: %s reading %s\n", when, problem, regs[i].name);
		return;
	}
	say("hapc: %s: rt_sts %#02x status1 %#02x auto_res %#02x %#02x en_ctl %#02x cfg %#02x %#02x "
	    "sel %#02x vmax %#02x ilim %#02x rate %#02x %#02x play %#02x\n", when, value[0], value[1],
	    value[2], value[3], value[4], value[5], value[6], value[7], value[8], value[9], value[10],
	    value[11], value[12]);
}

/* The device tree's numbers written the way the vendor driver writes them (enable/play 664-793,
 * mode/vmax 1058-1308), then played. The status registers are read while the wave runs: busy in
 * status1 bit 1 and play in rt_sts bit 1 are the register-level proof the block is driving the
 * motor even before an ear confirms it. */
static void
hapc_buzz(int leave_running)
{
	static const struct { uint8_t reg, value; const char *why; } recipe[] = {
		{ HAPC_SEC_ACCESS, HAPC_SEC_UNSECURE, "the bank's registers are open to us" },
		{ HAPC_CFG1, 0x00, "LRA, as the tree's actuator-type says" },
		{ HAPC_CFG2, 0x00, "sine, as the tree's wave-shape says" },
		{ HAPC_VMAX, 0x3a, "3400 mV, the tree's lra-vmax-mv" },
		{ HAPC_RATE1, 0x35, "6667 us play rate, the tree's lra-play-rate-us: 1333 low" },
		{ HAPC_RATE2, 0x05, "1333 high" },
		{ HAPC_SEL, 0x00, "direct-drive source: no waveform file needed" },
		{ HAPC_EN_CTL, 0x80, "module enable" },
	};

	for (unsigned i = 0; i < sizeof(recipe) / sizeof(recipe[0]); i++) {
		const char *problem = spmi_write(recipe[i].reg, recipe[i].value);

		if (problem != NULL) {
			say("hapc: writing %#02x = %#02x (%s): %s\n", recipe[i].reg, recipe[i].value,
			    recipe[i].why, problem);
			return;
		}
	}
	say("hapc: the recipe is in: LRA, sine, 3400 mV, 6667 us, direct; playing\n");
	const char *problem = spmi_write(HAPC_PLAY, 0x80);

	if (problem != NULL) {
		say("hapc: writing play: %s\n", problem);
		return;
	}
	delay_for_interval(100, NSEC_PER_MSEC);
	hapc_report("playing");
	delay_for_interval(400, NSEC_PER_MSEC);
	if (leave_running) {
		say("hapc: left playing (hapc=3): the motor should still be buzzing\n");
		return;
	}
	delay_for_interval(500, NSEC_PER_MSEC);
	(void)spmi_write(HAPC_PLAY, 0x00);
	delay_for_interval(20, NSEC_PER_MSEC);
	hapc_report("stopped");
	problem = spmi_write(HAPC_EN_CTL, hapc_en_ctl);
	say("hapc: put back as found: play off, module enable %#02x again: %s\n", hapc_en_ctl,
	    problem == NULL ? "written" : problem);
}

static bool
hapc_open(void)
{
	uint32_t version, channels;
	uint8_t type = 0xff, subtype = 0xff;

	arb_core = ml_io_map(ARB_CORE_PHYS, ARB_CORE_SIZE);
	arb_observer = ml_io_map(ARB_OBSERVER_PHYS, ARB_OBSERVER_SIZE);
	arb_cnfg = ml_io_map(ARB_CNFG_PHYS, ARB_CNFG_SIZE);
	if (arb_core == 0 || arb_observer == 0) {
		say("hapc: the PMIC arbiter does not map\n");
		return false;
	}
	version = rd(arb_core + ARB_VERSION);
	channels = rd(arb_core + ARB_FEATURES) & 0x7ff;
	if (version < ARB_VERSION_5 || version >= ARB_VERSION_7) {
		say("hapc: this driver knows version 5 of the arbiter only, not %#x\n", version);
		return false;
	}
	for (uint32_t n = 0; n < (channels ? channels : ARB_MAX_CHANNELS); n++) {
		uint32_t map = rd(arb_core + ARB_CHANNEL(n));

		if (map == 0 || ((map >> 8) & 0xfff) != ((HAPC_SLAVE << 8) | HAPC_PERIPHERAL)) {
			continue;
		}
		if (arb_cnfg != 0 && (rd(arb_cnfg + ARB_OWNER(n)) & 7) != ARB_SYSTEM) {
			continue;
		}
		hapc_channel = n;
		break;
	}
	if (hapc_channel == ~0u) {
		say("hapc: slave %u 0x%02x00 has no channel of ours: the motor stays still\n",
		    HAPC_SLAVE, HAPC_PERIPHERAL);
		return false;
	}
	if (spmi_read(hapc_channel, 0x04, &type, 1) != NULL) {
		say("hapc: slave %u 0x%02x00 does not answer\n", HAPC_SLAVE, HAPC_PERIPHERAL);
		return false;
	}
	(void)spmi_read(hapc_channel, 0x05, &subtype, 1);
	say("hapc: slave %u 0x%02x00 (channel %u): type %#02x subtype %#02x\n",
	    HAPC_SLAVE, HAPC_PERIPHERAL, hapc_channel, type, subtype);
	hapc_report("as found");
	if (spmi_read(hapc_channel, HAPC_EN_CTL, &hapc_en_ctl, 1) != NULL) {
		hapc_en_ctl = 0x00;
	}
	hapc_write = ml_io_map(ARB_CHNLS_PHYS + 0x10000UL * hapc_channel, 0x1000);
	if (hapc_write == 0) {
		say("hapc: the write window of channel %u does not map\n", hapc_channel);
		return false;
	}
	return true;
}

__startup_func
static void
hapc_init(void)
{
	uint32_t level = 0;

	if (!PE_parse_boot_argn("hapc", &level, sizeof(level)) || level == 0) {
		return;                                /* nothing is touched unless a boot-argument asks */
	}
	if (!hapc_open()) {
		return;
	}
	if (level >= 2) {
		hapc_buzz(level >= 3);
	}
}
STARTUP(EARLY_BOOT, STARTUP_RANK_LAST, hapc_init);
