/*
 * The keys of the OPPO Reno Ace: the power key is an input of the power-on block ("PON") of the
 * PM8150, the power management chip on SPMI slave 0, and the two volume keys are pins of that same
 * chip's GPIO controller. Only with boot-arg pon=1.
 *
 * What the phone's own device tree says (the dtbo, under qcom,pm8150@0), and this driver follows:
 *
 *      qcom,power-on@800 {
 *              reg = <0x800 0x100>;
 *              interrupt-names = "kpdpwr", "resin";
 *              qcom,pon-dbc-delay = <15625>;   (the block debounces its own inputs)
 *              qcom,pon_1 { qcom,pon-type = <0>; qcom,pull-up; linux,code = <116>; }   (KEY_POWER)
 *              qcom,pon_2 { qcom,pon-type = <1>; qcom,pull-up; linux,code = <114>; }   (KEY_VOLUMEDOWN)
 *      };
 *
 * So the block is at slave 0 0x0800 with power on its KPDPWR_n pin, and that part is true: the power
 * key works exactly like this. Three of this phone's PMICs have such a block -- the arbiter's own
 * inventory (logs/160725-run.log) answered type 0x01 subtype 0x04 at 0x0800 of slaves 0, 2 and 4 -- and
 * only slave 0's is the one the keys are wired to, which is why the tree puts the two keys under
 * qcom,pm8150@0. The other two are named in the log and left alone.
 *
 * The volume keys are not here, whatever the block's RESIN input might suggest. The phone said
 * so itself: logs/093440-run.log read the RESIN bit every 16 ms while both keys were pressed over and
 * over, and RESIN never moved once while KPDPWR moved 107 times. The dtbo (dtbo.bin.01.dts, lines
 * 6503..6521) says where they are instead: a gpio_keys node whose vol_up is "gpios = <&pinctrl 6
 * 0x01>" and vol_down <&pinctrl 7 0x01>, both active low, on the PM8150's GPIO controller at 0xc000
 * -- a different block with a different layout, which this driver reads too, from the same thread.
 *
 * Why this is a separate driver from drivers/input/hakios.buttons.c: the PMK8350 there is the newer
 * kind of power-on block, split in halves, whose half for the operating system answers subtype 0x09
 * and keeps its keys at bits 7 and 6 of its status. This phone's block is the older single kind,
 * subtype 0x04, whose keys are bits 1 and 0 of the same-named register. The registers are named from
 * the vendor driver for this very SoC, the SM8150 kernel's drivers/input/misc/qpnp-power-on.c:
 * QPNP_PON_RT_STS (base+0x10) with QPNP_PON_KPDPWR_N_SET (bit 0) and QPNP_PON_RESIN_N_SET (bit 1),
 * QPNP_PON_PULL_CTL (base+0x70), one register for the whole block that pulls KPDPWR up through bit 1
 * and RESIN up through bit 0, and QPNP_PON_DBC_CTL (base+0x71), whose four low bits select how long
 * the block debounces. PON_GEN2_PRIMARY (0x04) is the subtype the tree's qcom,pon-type values index.
 *
 * The PON half only reads: its pull-ups are already enabled -- the firmware reads the same keys
 * itself to choose fastboot and recovery, and the tree says pull-up -- and the driver checks that and
 * says what it finds instead of fixing it. The GPIO half cannot be so careful: nothing in this boot
 * path has ever configured those pins (Android's own kernel does that, and we do not boot it), and an
 * unpowered pin reads low forever. So it reads each pin's setup first and says what it finds, and
 * only if the pin is not already an input with a pull-up does it write the three registers that make
 * it one -- mode, pull, enable -- each read back after, and only through a channel the arbiter says
 * this system owns. All under hakios_spmi_lock().
 *
 * What a press is: the block's "real time status" has a bit per key, set while the key is held, after
 * the block's own debouncing. A thread reads that register 60 times a second; a key has changed when
 * two looks in a row say so, and a key that reads as held before it has ever been seen released is not
 * believed. No interrupt is used, though the block has one: taking it would mean the per-channel
 * interrupt registers of the arbiter and a handler, and polling at 16 ms is what the other drivers do.
 *
 * The bring-up is made in steps, each said before it is made. Boot-arg pon_steps=N stops after N:
 *   1  map the arbiter and read its version
 *   2  find the PON blocks: every 0x0800 the bus answers, and what each one is   (the first reads)
 *   3  read how the two inputs are set up (pull, debounce) and what they say now, and do the same
 *      for the volume keys' three GPIO blocks, enabling any that the firmware left off
 *   4  watch all of them                                                        (the default)
 *
 * Boot-args:
 *   pon=1             read the power and volume down keys through the power-on block
 *   pon_steps=N       stop after step N
 */
#include <kern/clock.h>
#include <kern/startup.h>
#include <kern/thread.h>
#include <libkern/libkern.h>
#include <mach/mach_types.h>
#include <pexpert/pexpert.h>
#include <sys/hakios.input.h>
#include <sys/systm.h>

extern vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);
extern kern_return_t thread_terminate(thread_t thread);
extern void loader_log_putc(char c);

static int say(const char *format, ...) __printflike(1, 2);

static int
say(const char *format, ...)
{
	char line[400];
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

/* ---- the PMIC arbiter, as devices/pclm10/drivers/power/hakios.smb5.c maps it: same bus, same reads */

#define ARB_CORE_PHYS           0x0c440000UL
#define ARB_CORE_SIZE           0x1100UL
#define ARB_CNFG_PHYS           0x0c40a000UL    /* who owns each channel: the write permission, as in drivers/camera/hakios.camera.c */
#define ARB_CNFG_SIZE           0x1000UL
#define ARB_OBSERVER_PHYS       0x0e600000UL
#define ARB_OBSERVER_SIZE       0x10000UL
#define ARB_CHNLS_PHYS          0x0c600000UL    /* writes: 0x10000 per channel */
#define ARB_SYSTEM              0
#define ARB_VERSION             0x000
#define ARB_VERSION_5           0x50000000u
#define ARB_VERSION_7           0x70000000u
#define ARB_FEATURES            0x004           /* [10:0] the number of channels */
#define ARB_CHANNEL(n)          (0x900 + 4 * (n))       /* [19:8] the slave and peripheral behind channel n */
#define ARB_OWNER(n)            (0x700 + 4 * (n))       /* [2:0] the system allowed to write channel n */
#define ARB_MAX_CHANNELS        512

#define OBSERVER_COMMAND        0x00
#define OBSERVER_STATUS         0x08
#define OBSERVER_DATA0          0x18
#define OBSERVER_DATA1          0x1c
#define CHANNEL_COMMAND         0x00            /* the same command word, in the channel's own block */
#define CHANNEL_STATUS          0x08
#define CHANNEL_WDATA0          0x10            /* the byte a write sends */
#define STATUS_DONE             0x1
#define STATUS_FAILURE          0x2
#define STATUS_DENIED           0x4
#define STATUS_DROPPED          0x8
#define COMMAND_WRITE           (0u << 27)      /* EXT_WRITEL: what a write command word carries */
#define COMMAND_READ            (1u << 27)
#define READ_TIMEOUT_US         2000

static vm_offset_t arb_core, arb_cnfg, arb_observer;

/* Read `count` (1 to 8) registers of the peripheral at channel `channel`, from register `reg`; NULL, or what went wrong. */
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

/* Write one register of the peripheral whose write channel `ch` maps; NULL, or what went wrong. Only
 * a channel the arbiter says system 0 owns can be written at all, which is what the caller checked
 * before mapping it (as drivers/camera/hakios.camera.c's pgpio_put does). */
static const char *
spmi_write(vm_offset_t ch, uint8_t reg, uint8_t value)
{
	uint64_t start = mach_absolute_time(), now, interval;
	uint32_t status;

	nanoseconds_to_absolutetime(READ_TIMEOUT_US * (uint64_t)NSEC_PER_USEC, &interval);
	hakios_spmi_lock();
	wr(ch + CHANNEL_WDATA0, value);
	wr(ch + CHANNEL_COMMAND, COMMAND_WRITE | (uint32_t)reg << 4);
	do {
		status = rd(ch + CHANNEL_STATUS);
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

/* ---- the power-on block --------------------------------------------------------------------------- */

#define KEY_SLAVE               0               /* the PM8150: the tree's qcom,pm8150@0, where the keys are */
#define PON_BLOCK               0x08            /* qcom,power-on@800: peripheral 0x08 of its slave */
#define PON_WHO                 0x04            /* what the block is: the type byte here, the subtype at 0x05, read as
                                                 * two bytes exactly as drivers/power/hakios.pmic.c does. Not 0x01: that
                                                 * is the block's revision, and its 0x01 looks exactly like the type
                                                 * below without being it -- which is how the first run on the phone
                                                 * found three blocks and believed none of them (logs/181216-run.log:
                                                 * `subtype 0x11`, which is the revision's second byte). */
#define PON_TYPE                0x01            /* what a power-on block answers at PON_WHO */
#define PON_GEN2_PRIMARY        0x04            /* and at PON_WHO+1: qpnp-power-on.c's subtype for this kind */

/* qpnp-power-on.c's registers, offsets from the block's base, which is what a read's register byte is */
#define PON_RT_STS              0x10            /* [1] RESIN_N is held, [0] KPDPWR_N is held, right now */
#define PON_KPDPWR_N_SET        (1u << 0)
#define PON_RESIN_N_SET         (1u << 1)
#define PON_PULL_CTL            0x70            /* one register for the whole block: [1] pull KPDPWR up, [0] pull RESIN up */
#define PON_KPDPWR_PULL_UP      (1u << 1)
#define PON_RESIN_PULL_UP       (1u << 0)
#define PON_DBC_CTL             0x71            /* [3:0] the debounce time select, this subtype's mask being 0xf */
#define PON_DBC_DELAY_MASK      0xf
#define PON_AFTER_RT_STS        0x11            /* the eight registers after the status, which the vendor driver does not
                                                 * name: read only to see whether a press is latched anywhere in them */

/* ---- the PM8150's GPIO controller: where the volume keys really are -------------------------------- */

/* The controller is qcom,spmi-gpio at 0xc000 of the same PM8150, ten pins in one 0x100 block each.
 * Which numbered pin is which block the controller's own interrupt-names say: 0xc0 is "pm8150_gpio1",
 * 0xc5 is "pm8150_gpio6" and 0xc6 is "pm8150_gpio7" -- the tree's numbers are the datasheet's, one
 * based -- and qcom,gpios-disallowed (0x02 0x05 0x08) names exactly the three blocks with no
 * interrupt line, 0xc100, 0xc400 and 0xc700, which is the proof the numbering runs that way. So the
 * dtbo's vol_up <&pinctrl 6 0x01> is 0xc500 and vol_down <&pinctrl 7 0x01> is 0xc600. 0xc700, the
 * disallowed block next to them, is watched as a control: if a press ever lands there instead of at
 * 0xc600, the numbering is off by one, and that is what its line will say. */
#define GPIO_BLOCK_UP           0xc5            /* peripheral 0xc5 of slave 0: 0xc500, the tree's vol_up */
#define GPIO_BLOCK_DOWN         0xc6            /* peripheral 0xc6: 0xc600, the tree's vol_down */
#define GPIO_BLOCK_CONTROL      0xc7            /* peripheral 0xc7: 0xc700, the control */
#define GPIO_NBLOCKS            3

/* drivers/pinctrl/qcom/pinctrl-spmi-gpio.c, the low-voltage kind: the same offsets in every pin's
 * block. drivers/input/hakios.buttons.c reads one of these on another phone and agrees: the level is
 * in the real-time status register at base+0x10, bit 0, not at 0x01 -- the 095211 run read 0x01 and
 * saw both pins HELD forever, which is what register 0x01 of a GPIO block reads as, not the pin. */
#define GPIO_WHO                0x04            /* what the block is: type here, subtype at 0x05 */
#define GPIO_TYPE               0x10            /* what a GPIO block answers there */
#define GPIO_STATUS             0x10            /* the real-time status: [0] the level of the pin, right now */
#define GPIO_LEVEL              (1u << 0)
#define GPIO_REG_MODE           0x40            /* [1:0]: 0 input, 1 output, 2 both, 3 analog */
#define GPIO_REG_PULL           0x42            /* [2:0]: 0 to 3 are pull-ups (30, 1.5, 31.5 uA, 1.5 with a
                                                * boost), 4 is a pull-down, 5 none: as in hakios.buttons.c */
#define GPIO_REG_ENABLE         0x46            /* [7] */
#define GPIO_PULL_UP            0               /* the 30 uA one, which is what the tree asks for */

#define POLL_MS                 16

static uint32_t pon_channel = ~0u;              /* the channel that reaches slave 0 0x0800 */
static uint32_t steps_wanted = 4;

static struct button {
	const char *    name;
	uint32_t        record;                   /* HAKIOS_BUTTON_*, or 0 to only say the press */
	bool            usable;                   /* the block was found and is what it claims to be */
	bool            live;                     /* it has been seen released at least once */
	bool            last;                     /* the look before this one */
	bool            down;                     /* as reported */
	uint64_t        since;                    /* when it went down */
	uint32_t        presses;
} power = { .name = "power", .record = HAKIOS_BUTTON_LOCK },
    resin = { .name = "resin", .record = 0 };  /* the block's other input: watched, and known not to
                                                  * be this phone's volume down (the header says how we
                                                  * know), so it says what it sees and posts nothing */

static struct gpio_key {
	const char *    name;                     /* what the log calls the pin */
	uint8_t         block;                    /* the PM8150 peripheral it is at */
	uint32_t        channel;                  /* the arbiter channel that reaches it, ~0u for none */
	vm_offset_t     write_base;               /* its write channel, when this system owns it */
	struct button   key;                      /* .usable is what button_look checks, and what the
	                                           * poll loop follows: one flag, not two */
} gpio_keys[GPIO_NBLOCKS] = {
	{ .name = "volume up's pin (0xc500)",   .block = GPIO_BLOCK_UP,   .channel = ~0u,
	  .key = { .name = "volume up",   .record = HAKIOS_BUTTON_VOLUME_UP } },
	{ .name = "volume down's pin (0xc600)", .block = GPIO_BLOCK_DOWN, .channel = ~0u,
	  .key = { .name = "volume down", .record = HAKIOS_BUTTON_VOLUME_DOWN } },
	{ .name = "the control pin (0xc700)",   .block = GPIO_BLOCK_CONTROL, .channel = ~0u,
	  .key = { .name = "pin 0xc700",  .record = 0 } },
};

/* "12.345 s (08:13:05.918 UTC)": since boot as in the log's own stamps, and as the daemons' lines have it */
static void
now_text(char *text, size_t size, uint64_t now)
{
	clock_sec_t seconds;
	clock_usec_t micro;
	uint64_t up;

	absolutetime_to_nanoseconds(now, &up);
	clock_get_calendar_microtime(&seconds, &micro);
	snprintf(text, size, "%llu.%03llu s (%02u:%02u:%02u.%03u UTC)", up / NSEC_PER_SEC, up / NSEC_PER_MSEC % 1000,
	    (unsigned)(seconds / 3600 % 24), (unsigned)(seconds / 60 % 60), (unsigned)(seconds % 60), (unsigned)(micro / 1000));
}

/* Step 2: find every 0x0800 the bus answers, name each one, and keep the one on slave 0. */
static bool
discover(void)
{
	uint32_t version, channels, found = 0;
	uint16_t ppids[ARB_MAX_CHANNELS];
	uint16_t channel_of[ARB_MAX_CHANNELS];

	arb_core = ml_io_map(ARB_CORE_PHYS, ARB_CORE_SIZE);
	arb_observer = ml_io_map(ARB_OBSERVER_PHYS, ARB_OBSERVER_SIZE);
	arb_cnfg = ml_io_map(ARB_CNFG_PHYS, ARB_CNFG_SIZE);
	if (arb_core == 0 || arb_observer == 0) {
		say("pon: could not map the PMIC arbiter\n");
		return false;
	}
	version = rd(arb_core + ARB_VERSION);
	channels = rd(arb_core + ARB_FEATURES) & 0x7ff;
	say("pon: PMIC arbiter version %#x, %u channels\n", version, channels);
	if (version < ARB_VERSION_5 || version >= ARB_VERSION_7) {
		say("pon: this driver knows version 5 of the arbiter only: nothing read\n");
		return false;
	}
	if (channels == 0 || channels > ARB_MAX_CHANNELS) {
		channels = ARB_MAX_CHANNELS;
	}
	/* one entry for each distinct slave/peripheral, with the first channel that reaches it (as hakios.smb5.c) */
	for (uint32_t n = 0; n < channels; n++) {
		uint32_t map = rd(arb_core + ARB_CHANNEL(n));
		uint16_t ppid = (uint16_t)((map >> 8) & 0xfff);
		bool again = false;

		if (map == 0) {
			continue;
		}
		for (uint32_t i = 0; i < found; i++) {
			if (ppids[i] == ppid) {
				again = true;
				break;
			}
		}
		if (!again) {
			ppids[found] = ppid;
			channel_of[found] = (uint16_t)n;
			found++;
		}
	}
	for (uint32_t i = 0; i < found; i++) {
		uint8_t is[2] = { 0xff, 0xff };
		uint32_t slave = ppids[i] >> 8, block = ppids[i] & 0xff;
		const char *problem;

		if (slave == KEY_SLAVE) {
			for (uint32_t g = 0; g < GPIO_NBLOCKS; g++) {
				if (block != gpio_keys[g].block) {
					continue;
				}
				/* the volume keys' pins: remember how to reach them, and whether we may
				 * write them at all (only the owner of a channel can write through it) */
				uint32_t owner = arb_cnfg != 0 ? rd(arb_cnfg + ARB_OWNER(channel_of[i])) & 7 : 7;

				gpio_keys[g].channel = channel_of[i];
				say("pon: slave 0 %#x00 is on channel %u, writable by system %u%s\n", block, channel_of[i],
				    owner, owner == ARB_SYSTEM ? " (us)" : "");
				if (owner == ARB_SYSTEM) {
					gpio_keys[g].write_base = ml_io_map(ARB_CHNLS_PHYS + 0x10000UL * channel_of[i], 0x1000);
				}
			}
		}
		if (block != PON_BLOCK) {
			continue;
		}
		problem = spmi_read(channel_of[i], PON_WHO, is, 2);
		if (problem != NULL) {
			say("pon:   slave %u 0x0800: %s\n", slave, problem);
			continue;
		}
		if (is[0] != PON_TYPE) {
			continue;
		}
		/* all three of this phone's PON blocks answered subtype 0x04 in the 160725 log; say what each
		 * one is, and use only the PM8150's, because that is where the tree wires the two keys */
		say("pon: a power-on block is slave %u 0x0800, channel %u: type %#02x subtype %#02x%s\n",
		    slave, channel_of[i], is[0], is[1],
		    is[1] == PON_GEN2_PRIMARY ? " (the kind with KPDPWR at bit 0 and RESIN at bit 1)" : "");
		if (slave == KEY_SLAVE && is[1] == PON_GEN2_PRIMARY && pon_channel == ~0u) {
			pon_channel = channel_of[i];
		}
	}
	if (pon_channel == ~0u) {
		say("pon: no power-on block of the expected kind on slave 0: nothing to watch\n");
		return false;
	}
	power.usable = resin.usable = true;
	return true;
}

/* Step 3: what the block's own settings say, and what the two pins say right now. */
static void
report_setup(void)
{
	uint8_t pull = 0xff, dbc = 0xff, later[8], status[1];
	const char *problem;

	problem = spmi_read(pon_channel, PON_PULL_CTL, &pull, 1);
	if (problem != NULL) {
		say("pon: reading the pull control: %s\n", problem);
	} else {
		/* the tree asks for pull-up on both; if the firmware left them off, say so and go on anyway --
		 * an unpressed key reads high through the key's own path either way, but a pressed one
		 * needs the pull to be believed, and this is where that would first show */
		say("pon: pull control %#02x: kpdpwr %s, resin %s (%s)\n", pull,
		    pull & PON_KPDPWR_PULL_UP ? "pulled up" : "not pulled up",
		    pull & PON_RESIN_PULL_UP ? "pulled up" : "not pulled up",
		    (pull & (PON_KPDPWR_PULL_UP | PON_RESIN_PULL_UP)) ==
		    (PON_KPDPWR_PULL_UP | PON_RESIN_PULL_UP) ? "as the tree asks" :
		    "the tree asks for pull-up on both");
	}
	problem = spmi_read(pon_channel, PON_DBC_CTL, &dbc, 1);
	if (problem != NULL) {
		say("pon: reading the debounce control: %s\n", problem);
	} else {
		/* qpnp_pon_get_dbc() of the vendor driver: for this subtype the select is the four low
		 * bits and the time is one second divided by two to the (14 - select), which is how the
		 * tree's qcom,pon-dbc-delay becomes a register value */
		uint32_t select = dbc & PON_DBC_DELAY_MASK;

		if (select <= 14) {
			say("pon:   debounce control %#02x: select %u, which is %u microseconds\n", dbc, select,
			    1000000u / (1u << (14 - select)));
		} else {
			say("pon:   debounce control %#02x: select %u, past what the vendor driver decodes\n",
			    dbc, select);
		}
	}
	problem = spmi_read(pon_channel, PON_AFTER_RT_STS, later, 8);
	if (problem != NULL) {
		say("pon: reading the registers after the status: %s\n", problem);
	} else {
		say("pon:   the block's registers 0x11 to 0x18, which the vendor driver does not name:");
		for (uint32_t i = 0; i < 8; i++) {
			printf(" %02x", later[i]);
		}
		printf("\n");
	}
	problem = spmi_read(pon_channel, PON_RT_STS, status, 1);
	if (problem != NULL) {
		say("pon: reading the keys: %s\n", problem);
	} else {
		say("pon: right now: power %s, resin %s (raw %#02x)\n",
		    (status[0] & PON_KPDPWR_N_SET) ? "HELD" : "released",
		    (status[0] & PON_RESIN_N_SET) ? "HELD" : "released", status[0]);
	}
}

/* Step 3b: are the volume keys' pins what the tree says they are? Read what each block is and how
 * it is set up, say that, and only if the pin is not already an input with a pull-up and switched
 * on, write the three registers that make it one -- each read back after, and only through a channel
 * the arbiter said system 0 owns. Nothing else in this boot path configures these pins: Android's
 * own kernel does, and we do not boot it, and an unpowered pin reads low forever. */
static void
gpio_setup(void)
{
	static const struct { uint8_t reg, mask, value; } want[] = {
		{ GPIO_REG_MODE, 3, 0 },                    /* an input */
		{ GPIO_REG_PULL, 7, GPIO_PULL_UP },         /* pulled up: the key only ever pulls it low */
		{ GPIO_REG_ENABLE, 0x80, 0x80 },            /* and switched on */
	};

	for (uint32_t g = 0; g < GPIO_NBLOCKS; g++) {
		struct gpio_key *key = &gpio_keys[g];
		uint8_t is[2] = { 0xff, 0xff }, c[7], status = 0xff;
		const char *problem;

		if (key->channel == ~0u) {
			say("pon: %s: the arbiter has no channel for slave 0 %#x00\n", key->name, key->block);
			continue;
		}
		problem = spmi_read(key->channel, GPIO_WHO, is, 2);
		if (problem != NULL) {
			say("pon: %s: reading what it is: %s\n", key->name, problem);
			continue;
		}
		if (is[0] != GPIO_TYPE || is[1] < 0x10 || is[1] > 0x13) {
			say("pon: %s: type %#02x subtype %#02x: not a GPIO of the kind this driver knows\n",
			    key->name, is[0], is[1]);
			continue;
		}
		problem = spmi_read(key->channel, GPIO_REG_MODE, c, sizeof(c));
		if (problem == NULL) {
			problem = spmi_read(key->channel, GPIO_STATUS, &status, 1);
		}
		if (problem != NULL) {
			say("pon: %s: reading its setup: %s\n", key->name, problem);
			continue;
		}
		say("pon: %s: type %#02x subtype %#02x; mode %u, pull %u, %s; registers 0x40..0x46 = %02x %02x %02x %02x %02x %02x %02x; "
		    "level %s\n", key->name, is[0], is[1], c[0] & 3, c[2] & 7, c[6] & 0x80 ? "on" : "OFF",
		    c[0], c[1], c[2], c[3], c[4], c[5], c[6],
		    (status & GPIO_LEVEL) ? "high (released)" : "LOW");
		if ((c[0] & 3) == 0 && (c[2] & 7) < 4 && (c[6] & 0x80)) {
			key->key.usable = true;             /* already what the tree asks: nothing to write */
		} else if (key->write_base == 0) {
			say("pon: %s: it is not an input with a pull-up, and this system does not own the channel to write it: "
			    "left alone\n", key->name);
			continue;
		} else {
			bool fixed = true;

			for (uint32_t w = 0; w < sizeof(want) / sizeof(want[0]); w++) {
				uint8_t have = c[want[w].reg - GPIO_REG_MODE], back = 0xff;
				uint8_t to = (uint8_t)((have & ~want[w].mask) | want[w].value);

				if ((have & want[w].mask) == want[w].value) {
					continue;
				}
				problem = spmi_write(key->write_base, want[w].reg, to);
				if (problem == NULL) {
					problem = spmi_read(key->channel, want[w].reg, &back, 1);
				}
				if (problem != NULL || back != to) {
					say("pon: %s: %#02x <- %#02x: %s\n", key->name, want[w].reg, to,
					    problem != NULL ? problem : "does not read back");
					fixed = false;
					break;
				}
				say("pon: %s: %#02x <- %#02x: written\n", key->name, want[w].reg, to);
			}
			if (!fixed) {
				continue;
			}
			problem = spmi_read(key->channel, GPIO_STATUS, &status, 1);
			if (problem != NULL) {
				say("pon: %s: reading its level after the writes: %s\n", key->name, problem);
				continue;
			}
			say("pon: %s: level after the writes: %s\n", key->name,
			    (status & GPIO_LEVEL) ? "high (released)" : "still LOW");
			key->key.usable = true;
		}
		key->key.last = !(status & GPIO_LEVEL);
	}
}

static void
button_look(struct button *b, bool pressed)
{
	uint64_t now = mach_absolute_time(), held;
	bool steady = pressed == b->last;
	char when[64];

	b->last = pressed;
	if (!b->usable || !steady) {
		return;
	}
	if (!b->live) {
		if (!pressed) {
			b->live = true;
			printf("pon: %s is released now: believed from here on\n", b->name);
		}
		return;
	}
	if (pressed == b->down) {
		return;
	}
	b->down = pressed;
	now_text(when, sizeof(when), now);
	if (pressed) {
		b->since = now;
		b->presses++;
		printf("pon: %s PRESSED at %s\n", b->name, when);
	} else {
		absolutetime_to_nanoseconds(now - b->since, &held);
		printf("pon: %s released at %s, after %llu ms\n", b->name, when, held / NSEC_PER_MSEC);
	}
	if (b->record != 0) {
		hakios_touch_post(pressed ? HAKIOS_BUTTON_DOWN : HAKIOS_BUTTON_UP, b->record, 0, 0);
	}
}

static void
pon_poll(void)
{
	static unsigned said, polls;
	const char *problem;
	uint8_t status, levels[GPIO_NBLOCKS];

	problem = spmi_read(pon_channel, PON_RT_STS, &status, 1);
	if (problem == NULL) {
		button_look(&power, (status & PON_KPDPWR_N_SET) != 0);
		button_look(&resin, (status & PON_RESIN_N_SET) != 0);
	} else if (said < 8) {
		said++;
		printf("pon: reading the keys: %s\n", problem);
	}
	for (uint32_t g = 0; g < GPIO_NBLOCKS; g++) {
		levels[g] = 0xff;
		if (!gpio_keys[g].key.usable) {
			continue;
		}
		if (spmi_read(gpio_keys[g].channel, GPIO_STATUS, &levels[g], 1) == NULL) {
			button_look(&gpio_keys[g].key, (levels[g] & GPIO_LEVEL) == 0);
		}
	}
	/* the heartbeat the 09:25 run asked for, now with the three volume pins in it: every ten
	 * seconds, the raw status word and every pin's level with the kernel's own clock. a press
	 * the driver missed is then a second that read differently, and a press that never reached
	 * a pin is a driver that never saw anything but released. */
	if (++polls % 600 == 0) {
		printf("pon: heartbeat: raw %#02x (%s%s), keys: up %s, down %s, control %s\n", status,
		    status & PON_KPDPWR_N_SET ? "power held" : "power released",
		    status & PON_RESIN_N_SET ? ", resin held" : "",
		    levels[0] == 0xff ? "?" : (levels[0] & GPIO_LEVEL) ? "released" : "HELD",
		    levels[1] == 0xff ? "?" : (levels[1] & GPIO_LEVEL) ? "released" : "HELD",
		    levels[2] == 0xff ? "?" : (levels[2] & GPIO_LEVEL) ? "released" : "HELD");
	}
}

static void
pon_thread(__unused void *parameter, __unused wait_result_t result)
{
	if (!discover()) {
		goto done;
	}
	if (steps_wanted < 3) {
		goto done;
	}
	report_setup();
	gpio_setup();
	if (steps_wanted < 4) {
		goto done;
	}
	/* neither key is believed until it has been seen released: one of them may well be held right
	 * now, because holding volume down is how this phone is booted into fastboot in the first place */
	for (;;) {
		pon_poll();
		delay_for_interval(POLL_MS, NSEC_PER_MSEC);
	}
done:
	thread_terminate(current_thread());
}

__startup_func
static void
pon_init(void)
{
	uint32_t enabled = 0, steps = 0;
	thread_t thread;

	if (!PE_parse_boot_argn("pon", &enabled, sizeof(enabled)) || !enabled) {
		return;
	}
	if (PE_parse_boot_argn("pon_steps", &steps, sizeof(steps)) && steps >= 1 && steps <= 4) {
		steps_wanted = steps;
	}
	if (kernel_thread_start(pon_thread, NULL, &thread) == KERN_SUCCESS) {
		thread_deallocate(thread);
	}
}
STARTUP(EARLY_BOOT, STARTUP_RANK_LAST, pon_init);
