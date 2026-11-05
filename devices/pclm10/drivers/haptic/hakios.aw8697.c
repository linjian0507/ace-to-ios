/*
 * The vibration motor of the OPPO Reno Ace (PCLM10): an Awinic AW8697.
 *
 * This phone's HAPC block on the PM8150B answers and takes the device tree's recipe
 * (drivers/haptic/hakios.hapc.c), but nothing moves: the motor is not on that block. The
 * overlay has the real driver, an AW8697 at I2C 0x5a on qupv3_se7, with reset on TLMM
 * gpio116 and its interrupt on gpio24 (vendor-src/dtb/dtbo.bin.01.dts, fragment@51, whose
 * target the fixup table names qupv3_se7_i2c; vendor-src/haptic/aw8697.c is the vendor's
 * own driver for it).
 *
 * The bus is the two pads of that engine (gpio98 data, gpio99 clock, qup7 in the merged
 * tree's pinctrl), moved by hand the way this port's camera and touch drivers move theirs:
 * the engine's wrapper never starts a command on this phone, and the pads are known good.
 * The chip needs no firmware for the continuous mode used here -- the vendor's RTP
 * waveforms are for touch feedback, and the CONT registers drive the LRA directly.
 *
 *   aw8697=1   reset, identify, play the vendor's continuous mode for a second, stop
 *   aw8697=2   the same, then leave it playing for an ear
 *   aw8697=3   stop whatever is playing and put the chip in standby
 *
 * Values are the vendor driver's own: AW8697_0815_* for the CONT drive level, delay,
 * zero-cross threshold and break count (aw8697.h), f0 170.0 Hz (aw8697_config.h's
 * coefficient 260), and the CONT/play-mode register recipe from
 * aw8697_haptic_cont() and aw8697_haptic_play_mode().
 */

#include <kern/clock.h>
#include <kern/startup.h>
#include <libkern/libkern.h>
#include <mach/mach_types.h>
#include <pexpert/pexpert.h>
#include <sys/systm.h>

extern vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);
extern void loader_log_putc(char c);

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

static void
spin_us(uint32_t us)
{
	uint64_t deadline;

	clock_interval_to_deadline(us, NSEC_PER_USEC, &deadline);
	while (mach_absolute_time() < deadline) {
		/* spin: a scheduler tick is not a microsecond */
	}
}

/* ---- the pads ------------------------------------------------------------------------------------ */

/* All of gpio98, gpio99 and gpio116 are in the SOUTH tile: its registers start at
 * 0x03000000 + 0x00d00000, and each pad owns 0x1000 bytes (vendor pinctrl-sm8150.c: SOUTH,
 * PINGROUP(98|99|116, SOUTH, ...)). */
#define AW_TLMM_PHYS    (0x03000000UL + 0x00d00000UL + 0x00062000UL) /* gpio98, the window's first */
#define AW_TLMM_SIZE    0x14000UL                                        /* gpio98..gpio117 */
#define AW_GPIO_CFG(n)  (aw_tlmm + 0x1000UL * ((n) - AW_SDA))
#define AW_GPIO_IO(n)   (AW_GPIO_CFG(n) + 4)
#define AW_CFG_PULL_UP  3
#define AW_CFG_OUTPUT   (1u << 9)

#define AW_SDA          98
#define AW_SCL          99
#define AW_RESET        116
#define I2C_HALF_US     5                       /* about 100 kHz, the touch driver's pace */

static vm_offset_t aw_tlmm;
static bool line_sda = true, line_scl = true;

static void
line(unsigned pin, bool high)
{
	wr(AW_GPIO_IO(pin), 0);
	wr(AW_GPIO_CFG(pin), AW_CFG_PULL_UP | (high ? 0 : AW_CFG_OUTPUT));
}

static bool
level(unsigned pin)
{
	return (rd(AW_GPIO_IO(pin)) & 1) != 0;
}

static void
half(void)
{
	spin_us(I2C_HALF_US);
}

static void
scl(bool high)
{
	line(AW_SCL, high);
	if (high) {
		for (unsigned i = 0; i < 1000 && !level(AW_SCL); i++) {
			spin_us(10);            /* a clock-stretching chip, up to 10 ms */
		}
	}
	half();
}

static void
sda(bool high)
{
	line(AW_SDA, high);
	line_sda = high;
}

static void
start(void)
{
	sda(true);
	scl(true);
	sda(false);
	half();
	scl(false);
}

static void
stop(void)
{
	sda(false);
	scl(true);
	sda(true);
	half();
}

static bool
write_byte(uint8_t byte)
{
	bool ack;

	for (int bit = 7; bit >= 0; bit--) {
		sda((byte >> bit) & 1);
		half();
		scl(true);
		scl(false);
	}
	sda(true);
	half();
	scl(true);
	ack = !level(AW_SDA);
	scl(false);
	return ack;
}

static uint8_t
read_byte(bool more)
{
	uint8_t byte = 0;

	sda(true);
	for (int bit = 0; bit < 8; bit++) {
		scl(true);
		byte = (uint8_t) (byte << 1) | (level(AW_SDA) ? 1u : 0u);
		scl(false);
	}
	sda(!more);
	half();
	scl(true);
	scl(false);
	sda(true);
	return byte;
}

/* ---- the chip's registers ------------------------------------------------------------------------ */

#define AW_REG_ID       0x00
#define AW_REG_SYSST    0x01
#define AW_REG_SYSINTM  0x03
#define AW_REG_SYSCTRL  0x04
#define AW_REG_GO       0x05
#define AW_REG_DATCTRL  0x2b
#define AW_REG_CONT_CTRL 0x48
#define AW_REG_F_PRE_H  0x49
#define AW_REG_F_PRE_L  0x4a
#define AW_REG_TD_H     0x4b
#define AW_REG_TD_L     0x4c
#define AW_REG_TSET     0x4d
#define AW_REG_BEMF_VTHH_H 0x74
#define AW_REG_BEMF_VTHH_L 0x75
#define AW_REG_BEMF_VTHL_H 0x76
#define AW_REG_BEMF_VTHL_L 0x77
#define AW_REG_BEMF_NUM 0x78
#define AW_REG_TIME_NZC 0x7a
#define AW_REG_DRV_LVL  0x7b
#define AW_REG_DRV_LVL_OV 0x7c
#define AW_REG_ZC_THRSH_H 0x72
#define AW_REG_ZC_THRSH_L 0x73

#define AW_SLAVE        0x5a

static bool
write_reg(uint8_t reg, uint8_t value)
{
	start();
	if (!write_byte((uint8_t) (AW_SLAVE << 1)) || !write_byte(reg) || !write_byte(value)) {
		stop();
		return false;
	}
	stop();
	return true;
}

static bool
read_reg(uint8_t reg, uint8_t *value)
{
	start();
	if (!write_byte((uint8_t) (AW_SLAVE << 1)) || !write_byte(reg)) {
		stop();
		return false;
	}
	start();
	if (!write_byte((uint8_t) (AW_SLAVE << 1 | 1))) {
		stop();
		return false;
	}
	*value = read_byte(false);
	stop();
	return true;
}

/* ---- the continuous play ------------------------------------------------------------------------- */

/* AW8697_0815_* from the vendor's aw8697.h, and f0 = 170.0 Hz with its coefficient 260. */
#define CONT_DRV_LVL     60
#define CONT_DRV_LVL_OV  125
#define CONT_TD          0x009a
#define CONT_ZC_THR      0x0ff1
#define CONT_NUM_BRK     3
#define F0_PRE           1700
#define F0_COEFF         260

static bool
chip_reset(void)
{
	line(AW_RESET, false);
	spin_us(2000);
	line(AW_RESET, true);
	spin_us(5000);
	return true;
}

static bool
play_cont(void)
{
	uint32_t f0_reg = (uint32_t) (1000000000ull / ((uint64_t) F0_PRE * F0_COEFF));
	uint8_t id = 0, sysst = 0;

	if (!read_reg(AW_REG_ID, &id)) {
		say("aw8697: no answer at 0x%02x on gpio%d/gpio%d after the reset\n", AW_SLAVE, AW_SDA, AW_SCL);
		return false;
	}
	say("aw8697: the chip answers at 0x%02x, id %#02x\n", AW_SLAVE, id);
	/* soft reset, then the clock the vendor's probe gives it */
	(void)write_reg(AW_REG_ID, 0xAA);
	spin_us(4000);
	(void)read_reg(AW_REG_ID, &id);
	say("aw8697: after the soft reset, id %#02x\n", id);
	if (!write_reg(AW_REG_SYSCTRL, 0x00)) {           /* standby, boost bypass */
		say("aw8697: SYSCTRL does not take the standby value\n");
		return false;
	}
	/* CONT mode: play mode 2, boost bypass, then active */
	if (!write_reg(AW_REG_SYSCTRL, 0x08)) {
		say("aw8697: SYSCTRL does not take the CONT play mode\n");
		return false;
	}
	(void)write_reg(AW_REG_SYSINTM, 0x00);             /* UVLO interrupt enabled */
	/* f0 preset, the vendor's formula */
	(void)write_reg(AW_REG_F_PRE_H, (uint8_t) ((f0_reg >> 8) & 0xff));
	(void)write_reg(AW_REG_F_PRE_L, (uint8_t) (f0_reg & 0xff));
	say("aw8697: f0 %u.%u Hz -> 0x%04x\n", F0_PRE / 10, F0_PRE % 10, f0_reg);
	(void)write_reg(AW_REG_DATCTRL, 0xc0 | 0x20);      /* 1000 Hz filter, low-pass on */
	/* zero cross on, wait one period, by the GO bit, playback open (the vendor's
	 * AW8697_CONT_PLAYBACK_MODE is the close bit set), f0 detect off, O2C off, auto break on */
	(void)write_reg(AW_REG_CONT_CTRL, 0x89);
	(void)write_reg(AW_REG_TD_H, (uint8_t) (CONT_TD >> 8));
	(void)write_reg(AW_REG_TD_L, (uint8_t) (CONT_TD & 0xff));
	(void)write_reg(AW_REG_TSET, 0x12);
	(void)write_reg(AW_REG_ZC_THRSH_H, (uint8_t) (CONT_ZC_THR >> 8));
	(void)write_reg(AW_REG_ZC_THRSH_L, (uint8_t) (CONT_ZC_THR & 0xff));
	(void)write_reg(AW_REG_BEMF_VTHH_H, 0x10);
	(void)write_reg(AW_REG_BEMF_VTHH_L, 0x08);
	(void)write_reg(AW_REG_BEMF_VTHL_H, 0x03);
	(void)write_reg(AW_REG_BEMF_VTHL_L, 0xf8);
	(void)write_reg(AW_REG_BEMF_NUM, CONT_NUM_BRK);
	(void)write_reg(AW_REG_TIME_NZC, 0x23);
	(void)write_reg(AW_REG_DRV_LVL, CONT_DRV_LVL);
	(void)write_reg(AW_REG_DRV_LVL_OV, CONT_DRV_LVL_OV);
	if (!write_reg(AW_REG_GO, 0x01)) {
		say("aw8697: the GO bit was not taken\n");
		return false;
	}
	spin_us(10000);
	(void)read_reg(AW_REG_SYSST, &sysst);
	say("aw8697: playing: SYSST %#02x (UVLO %u, OV %u, over-current %u, done %u)\n", sysst,
	    (sysst >> 5) & 1, (sysst >> 6) & 1, (sysst >> 2) & 1, sysst & 1);
	return true;
}

static void
stop_playing(void)
{
	(void)write_reg(AW_REG_GO, 0x00);
	(void)write_reg(AW_REG_SYSCTRL, 0x01);            /* standby */
	say("aw8697: stopped (GO 0, SYSCTRL standby)\n");
}

__startup_func
static void
aw8697_init(void)
{
	uint32_t level = 0;

	if (!PE_parse_boot_argn("aw8697", &level, sizeof(level)) || level == 0) {
		return;                                /* nothing is touched unless asked */
	}
	aw_tlmm = ml_io_map(AW_TLMM_PHYS, AW_TLMM_SIZE);
	if (aw_tlmm == 0) {
		say("aw8697: the SOUTH tile at %#lx did not map\n", (unsigned long) AW_TLMM_PHYS);
		return;
	}
	/* The pads live on an engine (qup7) the boot firmware configured; take them back as
	 * plain GPIOs, released with the pull-up the bus's external resistors assume. */
	line(AW_SDA, true);
	line(AW_SCL, true);
	line(AW_RESET, false);
	say("aw8697: %s\n", "reset low, then released");
	chip_reset();
	if (level == 3) {
		stop_playing();
		return;
	}
	if (!play_cont()) {
		say("aw8697: the play sequence stopped\n");
		return;
	}
	if (level >= 2) {
		say("aw8697: left playing (aw8697=2): the motor should be buzzing\n");
		return;
	}
	spin_us(1000000);
	stop_playing();
}
STARTUP(EARLY_BOOT, STARTUP_RANK_LAST, aw8697_init);
