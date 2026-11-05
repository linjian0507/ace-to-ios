/*
 * The backlight of the OPPO Reno Ace (PCLM10): the PM8150L's WLED module.
 *
 * The phone's merged device tree (vendor-src/dtb/android-merged.dts) names it `qcom,wled@d800` with
 * compatible "qcom,pm8150l-spmi-wled" and label "backlight". The PM8150L occupies two SPMI slaves,
 * 4 and 5, and the WLED module hangs off the second one: the arbiter's own channel table gives
 * peripheral 0xd8 a PPID of 0x5d8, slave 5, and the runtime tree's `__symbols__` resolves the
 * backlight phandle to the pm8150l@5 node. The module is its peripheral at 0xd800. The vendor's
 * driver (vendor-src/display/qcom-spmi-wled.c)
 * keeps the module's master switch in register 0x46, bit 7 (WLED_CTRL_MOD_ENABLE,
 * WLED_CTRL_MODULE_EN_SHIFT = 7); the per-string brightness lives in other registers that this
 * driver deliberately does not touch, so switching the module off and on again resumes the light at
 * exactly the brightness the boot firmware and Android left it at.
 *
 * Why it exists: the panel of this port is driven through the boot firmware's framebuffer, and
 * backboardd's "Set Blanked: YES" / backlight-factor 0 currently reaches no hardware — the screen
 * stays lit. This module is the hardware behind that request. A thread watches the sysctl
 * `kern.hakios.screen_backlight` (1 = lit, 0 = dark) and writes the module enable when it changes,
 * so whatever observes backboardd's blanking can drive it.
 *
 * Only with boot-arg wled=1; wled_test=1 adds one visible blink at startup (off 800 ms, on), which
 * is how this driver was first checked on the phone. All accesses go through the PMIC arbiter under
 * hakios_spmi_lock(), and only through a channel the arbiter says system 0 owns.
 */
#include <kern/clock.h>
#include <kern/startup.h>
#include <kern/thread.h>
#include <libkern/libkern.h>
#include <mach/mach_types.h>
#include <pexpert/pexpert.h>
#include <sys/systm.h>
#include <sys/sysctl.h>

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

/* ---- the PMIC arbiter (the same map the keys and the charger use) --------------------------------- */

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

/* the WLED module, which is a peripheral of the PM8150L's second SPMI slave: the arbiter's channel
 * table reads its PPID as 0x5d8, slave 5 and peripheral 0xd8, and the runtime tree's __symbols__
 * resolves the backlight phandle to pm8150l@5. Slave 4 is the GPIO controller the touch enable is on. */
#define WLED_SLAVE              5
#define WLED_PERIPHERAL         0xd8
#define WLED_MOD_ENABLE         0x46
#define WLED_MODULE_EN_SHIFT    7

static vm_offset_t arb_core, arb_cnfg, arb_observer, wled_write;
static uint32_t wled_channel = ~0u;
static uint8_t wled_mod = 0xff;                         /* what 0x46 said last */

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
	wr(wled_write + CHANNEL_WDATA0, value);
	wr(wled_write + CHANNEL_COMMAND, COMMAND_WRITE | (uint32_t)reg << 4);
	do {
		status = rd(wled_write + CHANNEL_STATUS);
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

/* ---- the switch ----------------------------------------------------------------------------------- */

static int screen_backlight = 1;

SYSCTL_DECL(_kern_hakios);
SYSCTL_INT(_kern_hakios, OID_AUTO, screen_backlight, CTLFLAG_RW | CTLFLAG_LOCKED, &screen_backlight, 0,
    "the panel's backlight: 1 lit, 0 dark (the PM8150L's WLED module)");

static const char *
wled_apply(int lit)
{
	uint8_t value;

	if (wled_channel == ~0u) {
		return "no channel";
	}
	if (lit) {
		value = (uint8_t)(wled_mod | (1u << WLED_MODULE_EN_SHIFT));
	} else {
		value = (uint8_t)(wled_mod & ~(1u << WLED_MODULE_EN_SHIFT));
	}
	const char *problem = spmi_write(WLED_MOD_ENABLE, value);

	if (problem != NULL) {
		return problem;
	}
	wled_mod = value;
	return NULL;
}

static void
wled_thread(__unused void *parameter, __unused wait_result_t result)
{
	int last = screen_backlight;

	for (;;) {
		int now = screen_backlight;

		if (now != last) {
			const char *problem = wled_apply(now);

			say("wled: the backlight %s: %s (module enable register %#02x)\n",
			    now ? "on" : "off", problem == NULL ? "written" : problem, wled_mod);
			if (problem == NULL) {
				last = now;
			}
		}
		delay_for_interval(100, NSEC_PER_MSEC);
	}
}

static bool
wled_open(void)
{
	uint32_t version, channels;
	uint8_t type = 0xff, subtype = 0xff;
	uint8_t regs[4] = { 0xff, 0xff, 0xff, 0xff };

	arb_core = ml_io_map(ARB_CORE_PHYS, ARB_CORE_SIZE);
	arb_observer = ml_io_map(ARB_OBSERVER_PHYS, ARB_OBSERVER_SIZE);
	arb_cnfg = ml_io_map(ARB_CNFG_PHYS, ARB_CNFG_SIZE);
	if (arb_core == 0 || arb_observer == 0) {
		say("wled: the PMIC arbiter does not map\n");
		return false;
	}
	version = rd(arb_core + ARB_VERSION);
	channels = rd(arb_core + ARB_FEATURES) & 0x7ff;
	say("wled: PMIC arbiter version %#x, %u channels\n", version, channels);
	if (version < ARB_VERSION_5 || version >= ARB_VERSION_7) {
		say("wled: this driver knows version 5 of the arbiter only\n");
		return false;
	}
	for (uint32_t n = 0; n < (channels ? channels : ARB_MAX_CHANNELS); n++) {
		uint32_t map = rd(arb_core + ARB_CHANNEL(n));

		if (map == 0 || ((map >> 8) & 0xfff) != ((WLED_SLAVE << 8) | WLED_PERIPHERAL)) {
			continue;
		}
		if (arb_cnfg != 0 && (rd(arb_cnfg + ARB_OWNER(n)) & 7) != ARB_SYSTEM) {
			continue;
		}
		wled_channel = n;
		break;
	}
	if (wled_channel == ~0u) {
		say("wled: slave %u 0x%02x00 has no channel of ours: the backlight stays as it is\n",
		    WLED_SLAVE, WLED_PERIPHERAL);
		return false;
	}
	if (spmi_read(wled_channel, 0x04, &type, 1) != NULL) {
		say("wled: slave %u 0x%02x00 does not answer\n", WLED_SLAVE, WLED_PERIPHERAL);
		return false;
	}
	(void)spmi_read(wled_channel, 0x05, &subtype, 1);
	(void)spmi_read(wled_channel, WLED_MOD_ENABLE, &regs[0], 1);
	say("wled: slave %u 0x%02x00 (channel %u): type %#02x subtype %#02x, module enable %#02x\n",
	    WLED_SLAVE, WLED_PERIPHERAL, wled_channel, type, subtype, regs[0]);
	wled_mod = regs[0];
	wled_write = ml_io_map(ARB_CHNLS_PHYS + 0x10000UL * wled_channel, 0x1000);
	if (wled_write == 0) {
		say("wled: the write window of channel %u does not map\n", wled_channel);
		return false;
	}
	screen_backlight = (wled_mod & (1u << WLED_MODULE_EN_SHIFT)) != 0;
	return true;
}

__startup_func
static void
wled_init(void)
{
	uint32_t enabled = 0, blink = 0;
	thread_t thread;

	if (!PE_parse_boot_argn("wled", &enabled, sizeof(enabled)) || !enabled) {
		return;                                /* nothing is switched unless a boot-argument asks */
	}
	if (!wled_open()) {
		return;
	}
	if (PE_parse_boot_argn("wled_test", &blink, sizeof(blink)) && blink) {
		const char *problem;

		say("wled: the bring-up blink: off for 800 ms, then back on\n");
		problem = wled_apply(0);
		delay_for_interval(800, NSEC_PER_MSEC);
		if (problem == NULL) {
			problem = wled_apply(1);
		}
		say("wled: the blink %s (module enable %#02x)\n",
		    problem == NULL ? "finished" : problem, wled_mod);
	}
	if (kernel_thread_start(wled_thread, NULL, &thread) == KERN_SUCCESS) {
		thread_deallocate(thread);
	}
}
STARTUP(EARLY_BOOT, STARTUP_RANK_LAST, wled_init);
