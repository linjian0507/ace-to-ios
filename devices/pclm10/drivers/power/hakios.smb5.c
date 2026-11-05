/*
 * The charger and the battery of the OPPO Reno Ace: the SMB5 charger and the gen4 fuel gauge on its PM8150B.
 * Only with boot-arg smb5=1.
 *
 * This phone's charging is managed on the application processor's side, not by its audio DSP: the device tree
 * has the whole charger ("qcom,qpnp-smb5") and fuel gauge ("qcom,fg-gen4") under the PM8150B, the PMIC that
 * hangs on SPMI slave 2, and no battery manager on the DSP. (That is another phone's design, and this
 * kernel's battery=1 path is that other phone's loader sharing what its firmware read.) So this driver reads
 * the two blocks itself, through the PMIC arbiter's observer channels, exactly as drivers/power/hakios.pmic.c
 * and drivers/input/hakios.buttons.c read theirs: one bus transfer at a time under hakios_spmi_lock(), only
 * the arbiter's command word ever written, and nothing at all written to a PMIC. The charger's own state
 * machine chose how to take this charge before the kernel started -- it is what charges a phone that is
 * switched off -- and goes on choosing while the kernel runs. What the kernel can do is read and tell: which
 * stage the charge is in, whether USB power is on the port and what kind of source it found, and what the
 * gauge says the battery's voltage and current are. The numbers go to drivers/power/hakios.battery.c
 * (hakios_battery_report), which is what fills kern.hakios.battery_* and the power source Darwin reads.
 *
 * The registers. The charger's from the vendor driver for this very PMIC: drivers/power/supply/qcom/qpnp-smb5.c
 * and its smb5-reg.h, which say the PM8150B's charger is SMB5 and lays it out as seven SPMI blocks of its own,
 * each answering type 0x02 with a subtype that names the part: 0x1000 CHGR (0x80) the charge state machine,
 * 0x1100 DCDC (0x81) the power path, 0x1300 USBIN (0x83) the USB input and its source detector, 0x1500 TYPEC
 * (0x85) the port, 0x1600 MISC (0x86) the temperature limits. CHGR's 0x06 says which stage the charge is in
 * (0 inhibit, 1 trickle, 2 pre, 3 fullon, 4 taper, 5 terminate, 6 pause, 7 disable -- not SMB2's order), 0x07
 * bit 1 a battery over-voltage, 0x0d how the temperature guard reads (bit 5 hot soft, bit 4 cold soft, bit 3
 * too hot, bit 2 too cold), 0x42 bit 0 whether charging is enabled; DCDC's 0x0b whether a valid input is
 * present (bit 0), whether the battery is being fed from USB (bit 4) and whether the input is suspended
 * (bit 6); USBIN's 0x07 bit 0 whether the source detection is done and 0x08 what it found (bit 3 a charger's
 * DCP, bit 2 a computer's charging port CDP, bit 1 over-current OCP, bit 0 a computer's port SDP); TYPEC's
 * 0x0b bit 0 whether anything is attached at all; MISC's 0x06 bit 5 whether temperature is limiting the
 * charge. The gauge's from Linux
 * drivers/power/supply/qcom/qpnp-fg-gen4.c, which tells its blocks apart by the subtype at 0x05 (0x10
 * BATT_SOC, 0x11 BATT_INFO, 0x0d the memory interface, 0x13 the ADC): BATT_INFO's 0xa0..0xa3 hold the
 * battery's voltage and current, two bytes each, with a shadow copy at 0xa6..0xa9 that must agree (both are
 * read and compared, as Linux's fg-utils.c does), at 122.070 uV and signed 0.488281 mA per count; 0x07 says
 * whether the battery is missing; 0x50 is the raw thermistor reading, which needs the battery profile's
 * thermistor table to turn into degrees, so it is printed raw and the temperature is left unknown. The
 * percentage itself lives in the gauge's SRAM, which only the gauge's own memory interface reaches with a
 * handshake this driver does not do yet; its BATT_SOC block's 0x09 (the monotonic percentage) is read and
 * printed, and used only if it is a number a battery could be at. The raw values are printed next to the
 * decoded ones, so the next step starts from what the phone says.
 *
 * Status (what has and has not been seen): the first run (1 November, logs/155501-run.log) showed the bus
 * works: the arbiter is version 5, the gauge's four blocks answered at slave 2 0x4000..0x4300 and gave a
 * plausible voltage with the shadow copy agreeing, and every block named itself. What was wrong was where this
 * driver looked for the charger: it used SMB2's map (the block at 0x0100), and that block answered -- type
 * 0x51 subtype 0x20, all its status registers zero -- because SMB5 does not keep its status there. The second
 * run reads the seven blocks above, which the first run's own inventory showed present at channels 265..271.
 * The gauge's voltage and current have now been read on the phone (4.33 V, and a current of one count, which
 * is what a full battery at rest reads); its percentage and temperature have not, and stay unknown until the
 * gauge's memory interface and the thermistor's table are done.
 *
 * Boot-args:
 *   smb5=1            read and report, every 5 seconds
 *   smb5_seconds=N    every N seconds instead (2 at the least)
 */
#include <kern/clock.h>
#include <kern/startup.h>
#include <kern/thread.h>
#include <libkern/libkern.h>
#include <mach/mach_types.h>
#include <pexpert/pexpert.h>
#include <sys/hakios.input.h>
#include <sys/sysctl.h>
#include <sys/systm.h>

extern vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);
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

/* the arbiter, as drivers/power/hakios.pmic.c maps it: the same bus, the same version, this phone's family */
#define ARB_CORE_PHYS           0x0c440000UL
#define ARB_CORE_SIZE           0x1100UL
#define ARB_OBSERVER_PHYS       0x0e600000UL
#define ARB_OBSERVER_SIZE       0x10000UL
#define ARB_SYSTEM              0
#define ARB_VERSION             0x000
#define ARB_VERSION_5           0x50000000u
#define ARB_VERSION_7           0x70000000u
#define ARB_FEATURES            0x004
#define ARB_CHANNEL(n)          (0x900 + 4 * (n))               /* [19:8] slave and peripheral */
#define ARB_MAX_CHANNELS        512
#define OBSERVER_COMMAND        0x00
#define OBSERVER_STATUS         0x08
#define OBSERVER_DATA0          0x18
#define OBSERVER_DATA1          0x1c
#define STATUS_DONE             0x1
#define STATUS_FAILURE          0x2
#define STATUS_DENIED           0x4
#define STATUS_DROPPED          0x8
#define COMMAND_READ            (1u << 27)
#define READ_TIMEOUT_US         2000

static vm_offset_t arb_core, arb_observer;

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

/* ---- the blocks we are after, all on the PM8150B (SPMI slave 2) ----------------------------------- */

#define CHG_SLAVE               2
/* SMB5 keeps itself as seven SPMI blocks, each answering type 0x02 with a subtype that names the part.
 * The first run's inventory (logs/155501-run.log) showed all seven, slave 2 0x1000..0x1600, exactly as
 * the vendor driver's smb5-reg.h lays them out. We read five of them: CHGR the charge state machine,
 * DCDC the power path, USBIN the USB input and its source detector, TYPEC the port, MISC the temperature
 * limits. Not BATIF (0x82), which is the battery interface the gauge owns, nor DCIN (0x84), the wireless
 * input's equivalent of USBIN, which this phone does not use, nor 0x1700 and up, which are block logic. */
#define SMB5_TYPE               0x02
#define SUBTYPE_CHGR            0x80
#define SUBTYPE_DCDC            0x81
#define SUBTYPE_USBIN           0x83
#define SUBTYPE_TYPEC           0x85
#define SUBTYPE_MISC            0x86
#define FG_BLOCK_FIRST          0x40          /* the gen4 gauge: 0x4000, 0x4100, 0x4200, 0x4300 */
#define FG_BLOCK_LAST           0x43

#define REG_TYPE                0x04
#define REG_SUBTYPE             0x05
#define FG_SUBTYPE_BATT_SOC     0x10          /* what the gauge's blocks say, as Linux's qpnp-fg-gen4.c picks them by */
#define FG_SUBTYPE_BATT_INFO    0x11
#define FG_SUBTYPE_MEM_IF       0x0d
#define FG_SUBTYPE_ADC_RR       0x13

/* CHGR, the charge state machine: smb5-reg.h's BATTERY_CHARGER_STATUS_*_REG */
#define CHG_STATUS1             0x06          /* [2:0] the stage the charge is in */
#define CHG_STATUS2             0x07          /* [1] the battery's voltage is over what the charger allows */
#define CHG_TEMP_STATUS         0x0d          /* [5] hot soft limit, [4] cold soft limit, [3] too hot, [2] too cold */
#define CHG_ENABLE              0x42          /* [0] charging is enabled */
#define CHG_STATE_MASK          0x07
#define CHG_BAT_OV              (1u << 1)
#define CHG_TEMP_MASK           0x3c          /* 0x0d's four temperature guards, bits 5..2 */
#define CHG_TEMP_SHIFT          2
#define CHG_ENABLED             (1u << 0)

/* DCDC, the power path: what is feeding the phone and the battery right now */
#define DCDC_STATUS             0x0b          /* [0] a valid input present, [4] the battery fed from USBIN,
                                                 [6] the input suspended */
#define DCDC_VALID_INPUT        (1u << 0)
#define DCDC_USE_USBIN          (1u << 4)
#define DCDC_SUSPENDED          (1u << 6)

/* USBIN, the USB input and its attached-source detector */
#define USBIN_APSD_DONE         0x07          /* [0] the detection is done: only then does 0x08 mean anything */
#define APSD_DONE               (1u << 0)
#define USBIN_APSD_RESULT       0x08          /* [3] a charger's DCP, [2] a computer's charging port CDP,
                                                 [1] over-current OCP, [0] a computer's port SDP */
#define APSD_DCP                (1u << 3)
#define APSD_CDP                (1u << 2)
#define APSD_OCP                (1u << 1)
#define APSD_SDP                (1u << 0)

/* TYPEC, the port itself, and MISC, where the temperature limits report */
#define TYPEC_STATUS            0x0b          /* [0] anything at all is attached */
#define TYPEC_ATTACHED          (1u << 0)
#define MISC_STATUS             0x06          /* [5] temperature is limiting the charge */
#define MISC_TLIM               (1u << 5)

#define SOC_MONOTONIC           0x09          /* the BATT_SOC block: the percentage, if the hardware keeps it here */
#define SOC_INT_STATUS          0x10          /* [1] the gauge's algorithm says its reading is ready */
#define SOC_READY               (1u << 1)
#define INFO_SYS_BATT           0x07          /* the BATT_INFO block: [2:0] the battery missing, three ways */
#define INFO_MISSING_MASK       0x07
#define INFO_BATT_TEMP          0x50          /* the raw thermistor reading, 2 bytes */
#define INFO_BATT_VI            0xa0          /* voltage then current, 2 bytes each; the shadow copy 6 bytes later */
#define INFO_BATT_VI_SHADOW     0xa6

/* Linux's fg-utils.c: the gauge's counts to volts and amps (both scales are per 1000 counts) */
#define UV_PER_1000_COUNTS      122070
#define UA_PER_1000_COUNTS      488281

static const char *const charge_stage[8] = {
	"inhibit", "trickle", "pre", "fullon", "taper", "terminate", "pause", "disable"
};

static uint32_t chg_channel = ~0u, dcdc_channel = ~0u, usbin_channel = ~0u, typec_channel = ~0u,
                misc_channel = ~0u, soc_channel = ~0u, info_channel = ~0u;

static int smb5_state_value = -1, smb5_enable_value = -1, smb5_usbin_value = -1, smb5_apsd_value = -1;
SYSCTL_DECL(_kern_hakios);              /* the node is drivers/core/hakios.platform.c's */
SYSCTL_INT(_kern_hakios, OID_AUTO, smb5_charge_state, CTLFLAG_RD | CTLFLAG_LOCKED, &smb5_state_value, 0,
    "what stage the SMB5 charger says the charge is in, 0 inhibit, 1 trickle, 2 pre, 3 fullon, 4 taper, "
    "5 terminate, 6 pause, 7 disable; -1 unread");
SYSCTL_INT(_kern_hakios, OID_AUTO, smb5_charging_enabled, CTLFLAG_RD | CTLFLAG_LOCKED, &smb5_enable_value, 0,
    "whether the SMB5 charger says charging is enabled; -1 unread");
SYSCTL_INT(_kern_hakios, OID_AUTO, smb5_usbin, CTLFLAG_RD | CTLFLAG_LOCKED, &smb5_usbin_value, 0,
    "whether USB power is feeding the battery as the SMB5 power path sees it; -1 unread");
SYSCTL_INT(_kern_hakios, OID_AUTO, smb5_apsd, CTLFLAG_RD | CTLFLAG_LOCKED, &smb5_apsd_value, 0,
    "what the port's source detector found, its raw bits (3 DCP, 2 CDP, 1 over-current, 0 a computer's "
    "port), or -1 unread and -2 when the detection has not finished");

/* one pass over the arbiter's table: find the charger's and the gauge's channels, and say what the phone has */
static bool
discover(void)
{
	uint32_t version, channels, found = 0;
	uint16_t ppids[ARB_MAX_CHANNELS];
	uint16_t channel_of[ARB_MAX_CHANNELS];

	arb_core = ml_io_map(ARB_CORE_PHYS, ARB_CORE_SIZE);
	arb_observer = ml_io_map(ARB_OBSERVER_PHYS, ARB_OBSERVER_SIZE);
	if (arb_core == 0 || arb_observer == 0) {
		say("smb5: could not map the PMIC arbiter\n");
		return false;
	}
	version = rd(arb_core + ARB_VERSION);
	channels = rd(arb_core + ARB_FEATURES) & 0x7ff;
	say("smb5: PMIC arbiter version %#x, %u channels\n", version, channels);
	if (version < ARB_VERSION_5 || version >= ARB_VERSION_7) {
		say("smb5: this driver knows version 5 of the arbiter only: nothing read\n");
		return false;
	}
	if (channels == 0 || channels > ARB_MAX_CHANNELS) {
		channels = ARB_MAX_CHANNELS;
	}
	/* one entry for each distinct slave/peripheral, with the first channel that reaches it (as hakios.pmic.c) */
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

		if (slave != CHG_SLAVE) {
			continue;
		}
		problem = spmi_read(channel_of[i], REG_TYPE, is, 2);
		if (problem != NULL) {
			say("smb5:   slave 2 0x%02x00: %s\n", block, problem);
			continue;
		}
		if (is[0] == SMB5_TYPE) {
			/* name the part by its subtype, which is how the vendor driver's own table does it */
			struct { uint8_t subtype; const char *name; uint32_t *channel; } parts[] = {
				{ SUBTYPE_CHGR, "CHGR, the charge state machine", &chg_channel },
				{ SUBTYPE_DCDC, "DCDC, the power path", &dcdc_channel },
				{ SUBTYPE_USBIN, "USBIN, the USB input", &usbin_channel },
				{ SUBTYPE_TYPEC, "TYPEC, the port", &typec_channel },
				{ SUBTYPE_MISC, "MISC, the temperature limits", &misc_channel },
			};

			for (uint32_t p = 0; p < sizeof(parts) / sizeof(parts[0]); p++) {
				if (is[1] == parts[p].subtype && *parts[p].channel == ~0u) {
					*parts[p].channel = channel_of[i];
					say("smb5: the charger's %s is slave 2 0x%02x00, arbiter channel %u\n",
					    parts[p].name, block, channel_of[i]);
				}
			}
		} else if (block >= FG_BLOCK_FIRST && block <= FG_BLOCK_LAST) {
			switch (is[1]) {
			case FG_SUBTYPE_BATT_SOC:
				soc_channel = channel_of[i];
				say("smb5: the gauge's BATT_SOC is slave 2 0x%02x00, channel %u\n", block, soc_channel);
				break;
			case FG_SUBTYPE_BATT_INFO:
				info_channel = channel_of[i];
				say("smb5: the gauge's BATT_INFO is slave 2 0x%02x00, channel %u\n", block, info_channel);
				break;
			case FG_SUBTYPE_MEM_IF:
				say("smb5:   the gauge's memory interface is slave 2 0x%02x00, channel %u: the SRAM with the"
				    " percentage needs it, and this driver does not use it yet\n", block, channel_of[i]);
				break;
			case FG_SUBTYPE_ADC_RR:
				say("smb5:   the gauge's ADC is slave 2 0x%02x00, channel %u\n", block, channel_of[i]);
				break;
			default:
				say("smb5:   slave 2 0x%02x00: type %#02x subtype %#02x, not one of the charger's or"
				    " gauge's blocks\n", block, is[0], is[1]);
				break;
			}
		}
	}
	if (chg_channel == ~0u) {
		say("smb5: no CHGR block (no slave 2 peripheral says type 0x02 subtype 0x80): the charge state"
		    " will stay unknown\n");
	}
	if (info_channel == ~0u) {
		say("smb5: no fuel gauge (none of slave 2's blocks 0x40..0x43 says 0x11): the battery will stay unknown\n");
	}
	return chg_channel != ~0u || info_channel != ~0u;
}

/* ---- what they say now --------------------------------------------------------------------------------- */

struct charger_reading {
	bool read;
	uint8_t status1, status2, temp_status, enable;   /* CHGR: the stage, the errors, the guards, the switch */
	uint8_t power_path;                              /* DCDC 0x0b: what is feeding the phone */
	uint8_t apsd_done, apsd_result;                  /* USBIN 0x07 and 0x08: the port's source, once found */
	uint8_t typec;                                   /* TYPEC 0x0b: whether anything is attached */
	uint8_t misc;                                    /* MISC 0x06: whether temperature is limiting the charge */
};

struct gauge_reading {
	bool read;
	int voltage_uv, current_ua;         /* -1 while unknown */
	int percent;                        /* from the monotonic register, -1 while unknown */
	uint8_t percent_raw;                /* what that register actually said */
	bool soc_ready, missing;
	uint8_t temp[2];                    /* the raw thermistor reading */
};

/* One block's register, or 0xff for "this phone's arbiter did not offer that block". A missing block is
 * not fatal: the others still say most of what we want to know. */
static uint8_t
read_one(uint32_t channel, uint16_t reg)
{
	uint8_t one = 0xff;

	if (channel == ~0u || spmi_read(channel, reg, &one, 1) != NULL) {
		return 0xff;
	}
	return one;
}

static const char *
charger_read(struct charger_reading *c)
{
	uint8_t b[8];
	const char *problem;

	if (chg_channel == ~0u) {
		return "no charger block";
	}
	problem = spmi_read(chg_channel, CHG_STATUS1, b, 8);        /* 0x06..0x0d in one transfer */
	if (problem != NULL) {
		return problem;
	}
	c->status1 = b[0];
	c->status2 = b[1];
	c->temp_status = b[7];
	c->enable = read_one(chg_channel, CHG_ENABLE);
	c->power_path = read_one(dcdc_channel, DCDC_STATUS);
	/* 0x07 and 0x08 together: the done flag and the result belong together, and reading them apart
	 * could pair a done flag with the previous session's result */
	c->apsd_done = c->apsd_result = 0xff;
	if (usbin_channel != ~0u) {
		if (spmi_read(usbin_channel, USBIN_APSD_DONE, b, 2) == NULL) {
			c->apsd_done = b[0];
			c->apsd_result = b[1];
		}
	}
	c->typec = read_one(typec_channel, TYPEC_STATUS);
	c->misc = read_one(misc_channel, MISC_STATUS);
	c->read = true;
	return NULL;
}

static const char *
gauge_read(struct gauge_reading *g)
{
	uint8_t vi[4], shadow[4], one;
	const char *problem;
	unsigned tries = 0;
	uint16_t vraw;
	int iraw;

	g->read = false;
	g->voltage_uv = g->current_ua = g->percent = -1;
	g->percent_raw = 0;
	g->soc_ready = g->missing = false;
	g->temp[0] = g->temp[1] = 0;
	if (info_channel == ~0u) {
		return "no fuel gauge";
	}
	/* the voltage and current must agree with their shadow copy, as Linux's fg-utils.c requires */
	do {
		problem = spmi_read(info_channel, INFO_BATT_VI, vi, 4);
		if (problem != NULL) {
			return problem;
		}
		problem = spmi_read(info_channel, INFO_BATT_VI_SHADOW, shadow, 4);
		if (problem != NULL) {
			return problem;
		}
	} while (bcmp(vi, shadow, 4) != 0 && ++tries < 3);
	if (bcmp(vi, shadow, 4) != 0) {
		return "the gauge's shadow copy does not agree";
	}
	vraw = (uint16_t)(vi[1] << 8 | vi[0]);
	iraw = (int)(uint16_t)(vi[3] << 8 | vi[2]);
	if (iraw & 0x8000) {
		iraw -= 0x10000;                                      /* bit 15 is the sign: negative gives charge back out */
	}
	g->voltage_uv = (int)((uint64_t)vraw * UV_PER_1000_COUNTS / 1000);
	g->current_ua = (int)((int64_t)iraw * UA_PER_1000_COUNTS / 1000);
	if (spmi_read(info_channel, INFO_SYS_BATT, &one, 1) == NULL) {
		g->missing = (one & INFO_MISSING_MASK) != 0;
		if (g->missing) {
			say("smb5: the gauge says the battery is missing (0x07 = %#02x)\n", one);
		}
	}
	(void)spmi_read(info_channel, INFO_BATT_TEMP, g->temp, 2);
	if (soc_channel != ~0u) {
		if (spmi_read(soc_channel, SOC_MONOTONIC, &one, 1) == NULL) {
			g->percent_raw = one;
			g->percent = one <= 100 ? (int)one : -1;          /* a percent or nothing to us; the raw line tells the rest */
		}
		if (spmi_read(soc_channel, SOC_INT_STATUS, &one, 1) == NULL) {
			g->soc_ready = (one & SOC_READY) != 0;
		}
	}
	g->read = true;
	return NULL;
}

static const char *
apsd_name(uint8_t apsd)
{
	if (apsd & APSD_DCP) {
		return "a charger (DCP)";
	} else if (apsd & APSD_CDP) {
		return "a computer's charging port (CDP)";
	} else if (apsd & APSD_OCP) {
		return "an over-current port (OCP)";
	} else if (apsd & APSD_SDP) {
		return "a computer's port (SDP)";
	}
	return "nothing";
}

static uint32_t report_seconds = 5;

/* The pack's own gauge (hakios.bq27541.c). The phone's Android takes the charge from this part on
 * the i2c@0xc94000 bus -- its overlay puts "oplus,bq27541-battery" there and leaves the PM8150B's
 * gen4 gauge disabled -- so it is the only source of a real percentage. */
extern int hakios_bq27541_read(int *percent, int *voltage_uv, int *current_ua,
    int *temperature_hundredths);

/* When the pack gauge has not answered yet: a plain Li-ion resting curve, so the display shows
 * something a battery could be at instead of zero. Only an estimate, and it says so. */
static int
soc_from_voltage(int uv)
{
	static const struct { int mv, percent; } curve[] = {
		{ 4310, 100 }, { 4200, 92 }, { 4100, 82 }, { 4000, 70 }, { 3900, 58 },
		{ 3800, 45 }, { 3750, 38 }, { 3700, 30 }, { 3650, 22 }, { 3600, 15 },
		{ 3500, 8 }, { 3400, 3 }, { 3300, 0 },
	};
	int mv = uv / 1000;

	if (uv <= 0) {
		return -1;
	}
	for (unsigned i = 1; i < sizeof(curve) / sizeof(curve[0]); i++) {
		if (mv >= curve[i].mv) {
			int span_mv = curve[i - 1].mv - curve[i].mv;
			int span_pc = curve[i - 1].percent - curve[i].percent;

			if (span_mv <= 0) {
				return curve[i - 1].percent;
			}
			return curve[i].percent + (mv - curve[i].mv) * span_pc / span_mv;
		}
	}
	return 0;
}

static void
report_once(void)
{
	struct charger_reading c = { 0 };
	struct gauge_reading g = { 0 };
	struct hakios_battery_state b = { 0 };
	const char *problem;
	int stage, status, usb_type = 0;
	bool input_present = false, usbin = false, attached = false, apsd_done = false;
	int pack_percent = -1, pack_uv = -1, pack_ua = 0, pack_temp = -1;
	bool pack_ok;

	problem = charger_read(&c);
	if (problem != NULL) {
		say("smb5: the charger: %s\n", problem);
	}
	problem = gauge_read(&g);
	if (problem != NULL) {
		say("smb5: the gauge: %s\n", problem);
	}
	pack_ok = hakios_bq27541_read(&pack_percent, &pack_uv, &pack_ua, &pack_temp) == 0;
	if (!c.read && !g.read && !pack_ok) {
		return;
	}
	if (c.read) {
		stage = c.status1 & CHG_STATE_MASK;
		/* the power path is the honest answer to "is USB feeding the battery": an input has to be
		 * present and the DCDC has to be using it, and either can be refused on its own */
		input_present = c.power_path != 0xff && (c.power_path & DCDC_VALID_INPUT) != 0;
		usbin = input_present && (c.power_path & DCDC_USE_USBIN) != 0;
		attached = c.typec != 0xff && (c.typec & TYPEC_ATTACHED) != 0;
		apsd_done = c.apsd_done != 0xff && (c.apsd_done & APSD_DONE) != 0;
		/* Linux's power_supply states, as bsd/sys/hakios.input.h numbers them: 1 charging, 2 discharging,
		 * 3 not charging, 4 full. SMB5's stages: trickle, pre, fullon and taper are charging; terminate
		 * is full; inhibit, pause and disable are not charging. */
		status = (stage >= 1 && stage <= 4) ? 1 : stage == 5 ? 4 : 3;
		if (apsd_done) {
			usb_type = (c.apsd_result & APSD_DCP) ? 2 : (c.apsd_result & APSD_CDP) ? 3 :
			    (c.apsd_result & APSD_OCP) ? 2 : (c.apsd_result & APSD_SDP) ? 1 : 0;
		}
		smb5_state_value = stage;
		smb5_enable_value = (c.enable & CHG_ENABLED) ? 1 : 0;
		smb5_usbin_value = usbin ? 1 : 0;
		smb5_apsd_value = apsd_done ? c.apsd_result : -2;
		say("smb5: charger: stage=%s, charging %s, input %s%s, battery %s USBIN, the port %s, its source %s\n",
		    charge_stage[stage], (c.enable & CHG_ENABLED) ? "enabled" : "DISABLED",
		    input_present ? "present" : "absent",
		    (c.power_path != 0xff && (c.power_path & DCDC_SUSPENDED)) ? ", suspended" : "",
		    usbin ? "fed from" : "not fed from",
		    attached ? "has something attached" : "nothing attached",
		    apsd_done ? apsd_name(c.apsd_result) : "not detected yet");
		say("smb5:   temperature guards %#x, charge %s limited by temperature, battery over-voltage %s\n",
		    (c.temp_status >> CHG_TEMP_SHIFT) & 0x0f, (c.misc & MISC_TLIM) ? "IS" : "is not",
		    (c.status2 & CHG_BAT_OV) ? "yes" : "no");
		say("smb5:   raw: chgr 06=%#02x 07=%#02x 0d=%#02x 42=%#02x, dcdc 0b=%#02x, usbin 07=%#02x 08=%#02x,"
		    " typec 0b=%#02x, misc 06=%#02x\n", c.status1, c.status2, c.temp_status, c.enable, c.power_path,
		    c.apsd_done, c.apsd_result, c.typec, c.misc);
	} else {
		status = 2;                                           /* discharging: we cannot see the charger */
		stage = -1;
	}

	if (g.read) {
		say("smb5: gauge: V=%d uV, I=%+d uA (%s), the battery %s, its thermistor reads %#02x %#02x,"
		    " the percentage register says %#02x (%d)%s\n", g.voltage_uv, g.current_ua,
		    g.current_ua > 0 ? "taking charge" : g.current_ua < 0 ? "giving charge" : "still",
		    g.missing ? "MISSING" : "there", g.temp[1], g.temp[0], g.percent_raw, g.percent,
		    g.soc_ready ? ", ready" : "");
	}

	/* hand it on: the sysctls and the power source (drivers/power/hakios.battery.c). The pack gauge's
	 * numbers win when it answers -- that is the charge Android shows. Without it the PM8150B
	 * gauge's voltage becomes a resting-curve estimate, which is only a stand-in until the pack
	 * gauge answers. */
	b.size = sizeof(b);
	if (pack_ok) {
		b.percent_hundredths = pack_percent * 100;
		b.voltage_uv = pack_uv;
		b.current_ua = pack_ua;
		b.temperature_hundredths = pack_temp;
	} else {
		int estimate = soc_from_voltage(g.voltage_uv);

		b.percent_hundredths = (estimate > 0 ? estimate : 0) * 100;
		b.voltage_uv = g.voltage_uv;
		b.current_ua = g.current_ua;
		b.temperature_hundredths = -27315;
		if (estimate >= 0) {
			say("smb5: the pack gauge has not answered: %d%% is a resting-voltage estimate\n",
			    estimate);
		}
	}
	b.present = (g.read && !g.missing) || pack_ok;
	b.external = input_present || attached;
	b.status = status;
	b.usb_type = usb_type;
	hakios_battery_report(&b);
	(void)stage;
}

static void
smb5_thread(__unused void *parameter, __unused wait_result_t result)
{
	if (!discover()) {
		return;
	}
	for (;;) {
		report_once();
		delay_for_interval(report_seconds * 1000, NSEC_PER_MSEC);
	}
}

__startup_func
static void
smb5_init(void)
{
	uint32_t enabled = 0, seconds = 0;
	thread_t thread;

	if (!PE_parse_boot_argn("smb5", &enabled, sizeof(enabled)) || !enabled) {
		return;
	}
	if (PE_parse_boot_argn("smb5_seconds", &seconds, sizeof(seconds)) && seconds >= 2) {
		report_seconds = seconds;
	}
	/* from a thread of its own: the reads wait on the bus, and the first report has to wait for IOKit's
	 * platform to be up anyway -- a report before that is dropped, and the next one lands. */
	if (kernel_thread_start(smb5_thread, NULL, &thread) == KERN_SUCCESS) {
		thread_deallocate(thread);
	}
}
STARTUP(EARLY_BOOT, STARTUP_RANK_LAST, smb5_init);
