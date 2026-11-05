/*
 * The battery gauge of the OPPO Reno Ace (PCLM10).
 *
 * The phone's runtime device tree -- boot.bin.01 and dtbo.bin.01, the pair the phone actually runs
 * (both carry oppo,dtsi_no 0x4a89; notes/battery-bus.md worked this out from the fixups) -- puts the
 * pack's gauge at I2C address 0x55 on the bus of **i2c@0xc94000, serial engine 15**: bq27541@55,
 * compatible "oplus,bq27541-battery" (dtbo.bin.01.dts:6957), next to the VOOC controller at 0x26 and
 * the short protection at 0x58. That bus's active pinctrl state names the two pins: gpio27 and
 * gpio28, function "qup15", in the east TLMM tile (boot.bin.01.dts:8220, pinctrl-sm8150.c:1627).
 * The earlier reading of this -- SE8 at i2c@a80000 with gpio88 and gpio89 -- came from
 * android-merged.dts, which turns out to be a different variant of this board: in the runtime tree
 * SE8 is enabled but has no children at all, which is exactly why the north pair never answered.
 * Both pairs are probed, the runtime tree's first.
 *
 * Bit-banged rather than driven by the serial engine. This SoC's QUPv3 serial engines report
 * FIFO_IF_DISABLE in GENI_IF_DISABLE_RO (0x64, bit 0), and mainline's i2c-qcom-geni.c reads exactly
 * that as "FIFO is disabled, so we can only use GPI DMA" -- it does not even try the FIFO path when
 * that bit is set -- and the vendor's own 4.19 driver uses GPI DMA on these buses as well: every i2c
 * node in the phone's tree carries dmas. GPI is a DMA engine with transfer rings and an event
 * interface; two open-drain wires are not, and the pads are already ours.
 *
 * Open drain is emulated the way the pads support it: released, the pin is an input and the board's
 * pull-up raises the line; driven, the pin is an output with its data bit at 0 and the line is
 * pulled down. No line is ever driven high.
 *
 * Only with boot-arg bq27541=1. A half period of five microseconds is about 100 kHz -- the bus ran
 * at 400 kHz under the phone's own kernel (qcom,clk-freq-out = 0x61a80 in the same overlay), and
 * nothing here needs that speed. The first transaction also settles which of the two pins is the
 * clock: the tree lists them without labels, so both orders are tried once and the one the gauge
 * answers is kept.
 *
 * What is read: the TI "standard command" set, with the register numbers from OPPO's own gauge driver
 * (drivers/power/oppo/gauge_ic/oplus_bq27541.h) -- 0x2C StateOfCharge in percent, 0x08 voltage in mV,
 * 0x14 average current in mA (signed), 0x06 temperature in 0.1 K -- or the SBS set the BQ28Z610
 * answers (0x08 temperature, 0x09 voltage, 0x0A current, 0x0D RelativeStateOfCharge). Which one is
 * decided by what reads back plausibly, not by the node's name.
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

static void
sleep_us(uint32_t us)
{
	/* A strict spin on the timebase, not delay_for_interval(): that one blocks on a timer when
	 * ml_delay_should_spin() says the interval is longer than the CPU's idle latency, and at early
	 * boot that latency is still 0, so a 12 us half period became a scheduler wait -- far longer
	 * than the SMBus timeout the gauge keeps. The timebase itself is up from the first instruction. */
	uint64_t deadline = 0;

	clock_interval_to_deadline(us, NSEC_PER_USEC, &deadline);
	while (mach_absolute_time() < deadline) {
	}
}

/* ---- the two wires ------------------------------------------------------------------------------- */

#define TLMM_PHYS               0x03000000UL
#define TLMM_EAST               0x00500000UL            /* the tile gpio27 and gpio28 live in, per
                                                         * pinctrl-sm8150.c:1627: the runtime tree's
                                                         * gauge bus */
#define TLMM_NORTH              0x00900000UL            /* the tile gpio88 and gpio89 live in: the
                                                         * merged-tree variant, kept as a fallback */
#define GPIO_CFG(tile, n)       ((tile) + 0x1000UL * (n))
#define GPIO_IO(tile, n)        ((tile) + 0x1000UL * (n) + 4)
#define OUTPUT                  (1u << 9)
#define PULL_UP                 3u                     /* the TLMM's pull field is 0 none, 1 down,
                                                        * 2 keeper, 3 up (pinctrl-msm.c's MSM_*). Two
                                                        * is a keeper, and a keeper holds whatever the
                                                        * line was last left at -- which is why the
                                                        * first version read a bus the serial engine
                                                        * had left low as still low. */
#define DRIVE_4MA               1u

/* A tile window is 512 KB of kernel map that is never given back, and the 17:23 run panicked in
 * kmem_alloc(524288) exactly because the probing here asked for a dozen of them. Each tile is
 * therefore mapped once and remembered. */
static vm_offset_t
tile_map(unsigned long tile_off)
{
	static struct { unsigned long off; vm_offset_t map; } mapped[4];

	for (unsigned i = 0; i < sizeof(mapped) / sizeof(mapped[0]); i++) {
		if (mapped[i].map != 0) {
			if (mapped[i].off == tile_off) {
				return mapped[i].map;
			}
			continue;
		}
		vm_offset_t tile = ml_io_map(TLMM_PHYS + tile_off, 0x80000UL);

		if (tile != 0) {
			mapped[i].off = tile_off;
			mapped[i].map = tile;
		}
		return tile;
	}
	say("bq27541: four tiles are mapped and this is a fifth; no map\n");
	return 0;
}

#define PIN_GPIO_A              27                     /* the runtime tree's gauge pins, in its order */
#define PIN_GPIO_B              28
#define PIN_ALT_A               88                     /* the merged-tree variant's pins, tried second */
#define PIN_ALT_B               89

#define I2C_HALF_US             12                     /* about 40 kHz: slower than the bus ever ran, and
                                                        * the bring-up wants edges the gauge cannot miss */

static vm_offset_t tlmm;
static unsigned scl_pin = PIN_GPIO_A, sda_pin = PIN_GPIO_B;

static void
pin_high(unsigned pin, vm_offset_t tile)
{
	/* output, data 1: a reset line that has no pull-up of its own must be driven high, not
	 * released -- released it floats and the chip stays in reset */
	wr(GPIO_CFG(tile, pin), PULL_UP | DRIVE_4MA << 6 | OUTPUT);
	wr(GPIO_IO(tile, pin), 2);
}

static void
pin_release(unsigned pin)
{
	/* input, pull-up: the line belongs to the board, nothing here drives it */
	wr(GPIO_CFG(tlmm, pin), PULL_UP | DRIVE_4MA << 6);
}

static void
pin_low(unsigned pin)
{
	/* output, data 0: the pad pulls the line down */
	wr(GPIO_CFG(tlmm, pin), PULL_UP | DRIVE_4MA << 6 | OUTPUT);
	wr(GPIO_IO(tlmm, pin), 0);
}

static unsigned
pin_level(unsigned pin)
{
	return rd(GPIO_IO(tlmm, pin)) & 1u;
}

static void
i2c_delay(void)
{
	sleep_us(I2C_HALF_US);
}

static void
i2c_start(void)
{
	pin_release(sda_pin);
	pin_release(scl_pin);
	i2c_delay();
	pin_low(sda_pin);
	i2c_delay();
	pin_low(scl_pin);
	i2c_delay();
}

static void
i2c_stop(void)
{
	pin_low(sda_pin);
	i2c_delay();
	pin_release(scl_pin);
	i2c_delay();
	pin_release(sda_pin);
	i2c_delay();
}

/* One byte out; true when the slave answered its ninth clock. */
static bool
i2c_write(uint8_t byte)
{
	for (unsigned bit = 0; bit < 8; bit++) {
		if (byte & 0x80) {
			pin_release(sda_pin);
		} else {
			pin_low(sda_pin);
		}
		byte <<= 1;
		i2c_delay();
		pin_release(scl_pin);
		i2c_delay();
		pin_low(scl_pin);
	}
	/* the ninth clock: let the slave pull the data line down */
	pin_release(sda_pin);
	i2c_delay();
	pin_release(scl_pin);
	i2c_delay();
	bool ack = pin_level(sda_pin) == 0;
	pin_low(scl_pin);
	return ack;
}

static uint8_t
i2c_read(bool ack)
{
	uint8_t byte = 0;

	pin_release(sda_pin);
	for (unsigned bit = 0; bit < 8; bit++) {
		i2c_delay();
		pin_release(scl_pin);
		i2c_delay();
		byte = (uint8_t)(byte << 1 | pin_level(sda_pin));
		pin_low(scl_pin);
	}
	if (ack) {
		pin_low(sda_pin);
	} else {
		pin_release(sda_pin);
	}
	i2c_delay();
	pin_release(scl_pin);
	i2c_delay();
	pin_low(scl_pin);
	pin_release(sda_pin);
	return byte;
}

/* The usual register read: command out, repeated start, bytes in. */
static bool
i2c_read_reg(uint8_t address, uint8_t command, uint8_t *data, unsigned count)
{
	if (count == 0 || count > 8) {
		return false;
	}
	i2c_start();
	if (!i2c_write((uint8_t)(address << 1))) {
		i2c_stop();
		return false;
	}
	if (!i2c_write(command)) {
		i2c_stop();
		return false;
	}
	i2c_start();
	if (!i2c_write((uint8_t)(address << 1 | 1))) {
		i2c_stop();
		return false;
	}
	for (unsigned i = 0; i < count; i++) {
		data[i] = i2c_read(i + 1 < count);
	}
	i2c_stop();
	return true;
}

/* Just the address phase: does anything on the bus acknowledge this address? */
static bool
i2c_ping(uint8_t address)
{
	i2c_start();
	bool ack = i2c_write((uint8_t)(address << 1));

	i2c_stop();
	return ack;
}

/* ---- the gauge ------------------------------------------------------------------------------------ */

#define GAUGE_ADDRESS           0x55

/* The other bus the overlay names (fragment@64 targets qupv3_se1_i2c, i2c@884000, with the MP2650
 * charger at 0x5c): gpio114 and gpio115, function "qup1", in the south TLMM tile. It is scanned as
 * a control for the bit-banging itself: the south tile is the one the touch driver already proved
 * answers, so a device found here means the wires and the protocol are right and the gauge's own
 * bus is what is not. */
#define TLMM_SOUTH              0x00d00000UL
#define PIN_CONTROL_A           114
#define PIN_CONTROL_B           115

static bool gauge_up;                                  /* the pins answered at least once */
static int gauge_set = -1;                             /* 0: TI standard commands, 1: SBS */
static uint16_t gauge_type;
static int gauge_failures;
static unsigned start_attempts;

static bool
gauge_word(uint8_t low_command, uint16_t *value)
{
	uint8_t bytes[2];

	if (!i2c_read_reg(GAUGE_ADDRESS, low_command, bytes, 2)) {
		return false;
	}
	*value = (uint16_t)(bytes[1] << 8 | bytes[0]);
	return true;
}

static bool
gauge_byte(uint8_t command, uint16_t *value)
{
	uint8_t one;

	if (!i2c_read_reg(GAUGE_ADDRESS, command, &one, 1)) {
		return false;
	}
	*value = one;
	return true;
}

static bool
gauge_probe_order(unsigned scl, unsigned sda)
{
	uint16_t type;

	scl_pin = scl;
	sda_pin = sda;
	if (!gauge_word(0x00, &type)) {
		return false;
	}
	gauge_type = type;
	gauge_up = true;
	return true;
}

/* A gauge in nap mode answers nothing until it has seen activity on its clock: three starts and
 * stops with nothing between them, then a tenth of a second. It costs nothing to a gauge that is
 * already awake and it is the only way a sleeping one ever answers its first probe. */
static void
gauge_wake(void)
{
	for (unsigned round = 0; round < 3; round++) {
		i2c_start();
		i2c_stop();
		sleep_us(20000);
	}
	sleep_us(100000);
}

/* Which TLMM tile really holds the two pins, and whether the board's own pull-ups hold the two
 * lines up without ours. Read-only except for the bias (restored at the end). */
static void
tile_check(unsigned long tile_off, unsigned a, unsigned b)
{
	static const struct { const char *what; unsigned long off; } tiles[] = {
		{ "west", 0x00100000UL },
		{ "east", 0x00500000UL },
		{ "north", 0x00900000UL },
		{ "south", 0x00d00000UL },
	};
	vm_offset_t tile = tile_map(tile_off);

	for (unsigned i = 0; i < sizeof(tiles) / sizeof(tiles[0]); i++) {
		vm_offset_t other = tile_map(tiles[i].off);

		if (other == 0) {
			continue;
		}
		say("bq27541: tile %s +%#lx: gpio%u cfg %#x io %#x, gpio%u cfg %#x io %#x\n",
		    tiles[i].what, tiles[i].off, a, rd(GPIO_CFG(other, a)), rd(GPIO_IO(other, a)),
		    b, rd(GPIO_CFG(other, b)), rd(GPIO_IO(other, b)));
	}
	if (tile == 0) {
		return;
	}
	wr(GPIO_CFG(tile, a), DRIVE_4MA << 6);
	wr(GPIO_CFG(tile, b), DRIVE_4MA << 6);
	sleep_us(200);
	say("bq27541: without our pull-up the wires read scl %u sda %u: the board's own pull-ups are %s\n",
	    pin_level(a), pin_level(b),
	    (pin_level(a) && pin_level(b)) ? "there" : "ABSENT (the high levels above "
	    "were ours)" );
	pin_release(a);
	pin_release(b);
}

/* The control buses, bit-banged with the same code: qupv3_se1_i2c's two pins (gpio114/115, south
 * tile) with its MP2650 at 0x5c, and the touch's own bus qupv3_se17_i2c (gpio55/56, south tile) with
 * the S3706 at 0x20. What answers here says whether the wires and the protocol are right,
 * independently of the gauge's bus. */
static unsigned
bus_scan(const char *what, unsigned long tile_off, unsigned pin_a, unsigned pin_b)
{
	vm_offset_t saved_tlmm = tlmm;
	unsigned saved_scl = scl_pin, saved_sda = sda_pin;
	vm_offset_t tile = tile_map(tile_off);
	unsigned found = 0;

	if (tile == 0) {
		say("bq27541: %s: its tile does not map, no scan\n", what);
		return 0;
	}
	tlmm = tile;
	scl_pin = pin_a;
	sda_pin = pin_b;
	pin_release(pin_a);
	pin_release(pin_b);
	sleep_us(200);
	say("bq27541: %s: before the scan the lines read scl %u sda %u (gpio%u/gpio%u, cfg %#x %#x)\n",
	    what, pin_level(pin_a), pin_level(pin_b), pin_a, pin_b, rd(GPIO_CFG(tile, pin_a)),
	    rd(GPIO_CFG(tile, pin_b)));
	for (uint8_t address = 0x08; address <= 0x77; address++) {
		if (i2c_ping(address)) {
			say("bq27541: %s: address 0x%02x acknowledges (gpio%u clock, gpio%u data)\n", what,
			    address, pin_a, pin_b);
			found++;
		}
	}
	say("bq27541: %s (gpio%u/gpio%u) found %u device%s\n", what, pin_a, pin_b, found,
	    found == 1 ? "" : "s");
	tlmm = saved_tlmm;
	scl_pin = saved_scl;
	sda_pin = saved_sda;
	return found;
}

static void
control_scan(void)
{
	uint32_t touch_enabled = 0;

	/* This legacy diagnostic takes over gpio55/56 and pulses gpio54. It is
	 * not a battery operation and must never race the live S3706 owner.
	 * Check before any scan: even the first unrelated scan extends that race.
	 * Standalone gauge diagnostics with touch off retain the old path. */
	PE_parse_boot_argn("touch", &touch_enabled, sizeof(touch_enabled));
	if (touch_enabled != 0) {
		say("bq27541: control-bus diagnostics skipped: touch owns gpio54/55/56; "
		    "no touch scan, reset pulse or pad override is permitted\n");
		return;
	}
	(void)bus_scan("the SE1 bus (MP2650 at 0x5c)", TLMM_SOUTH, PIN_CONTROL_A, PIN_CONTROL_B);
	/* The touch bus first as it is, then after the chip's own reset line (gpio54, low holds it in
	 * reset) has been pulsed: an S3706 left mid-transaction by the serial engine's attempts may be
	 * holding SDA down, and its reset is the one thing that clears that from our side. */
	(void)bus_scan("the touch bus (S3706 at 0x20)", TLMM_SOUTH, 55, 56);
	vm_offset_t tile = tile_map(TLMM_SOUTH);

	if (tile != 0) {
		say("bq27541: gpio54 (the touch reset) reads cfg %#x io %#x before the pulse\n",
		    rd(GPIO_CFG(tile, 54)), rd(GPIO_IO(tile, 54)));
		/* reset low, then driven high and left high: the line has no pull-up of its own (the
		 * boot state is a pull-down), so releasing it would keep the chip in reset */
		wr(GPIO_CFG(tile, 54), PULL_UP | DRIVE_4MA << 6 | OUTPUT);
		wr(GPIO_IO(tile, 54), 0);
		sleep_us(20000);
		pin_high(54, tile);
		sleep_us(60000);
		say("bq27541: gpio54 driven high: cfg %#x io %#x; the bus now reads scl %u sda %u\n",
		    rd(GPIO_CFG(tile, 54)), rd(GPIO_IO(tile, 54)), rd(GPIO_IO(tile, 55)) & 1,
		    rd(GPIO_IO(tile, 56)) & 1);
	/* Can the pad itself drive the line high? A pad that cannot is looking at a hard short; a pad
	 * that can, while the released line stays low, is looking at a weak clamp (a diode to an
	 * unpowered rail, which is what an unpowered chip's I/O looks like). */
		wr(GPIO_CFG(tile, 55), PULL_UP | DRIVE_4MA << 6 | OUTPUT);
		wr(GPIO_IO(tile, 55), 2);
		wr(GPIO_CFG(tile, 56), PULL_UP | DRIVE_4MA << 6 | OUTPUT);
		wr(GPIO_IO(tile, 56), 2);
		sleep_us(100);
		say("bq27541: driven high, the touch bus reads scl %u sda %u (a low here is a hard short)\n",
		    rd(GPIO_IO(tile, 55)) & 1, rd(GPIO_IO(tile, 56)) & 1);
		/* release them on this tile, not through pin_release(): that one writes the tile the
		 * global tlmm points at, which is the gauge.s north tile here, and pin 55 or 56 has no
		 * register there */
		wr(GPIO_CFG(tile, 55), PULL_UP | DRIVE_4MA << 6);
		wr(GPIO_CFG(tile, 56), PULL_UP | DRIVE_4MA << 6);
	}
	(void)bus_scan("the touch bus after its reset", TLMM_SOUTH, 55, 56);
}

static bool
gauge_start(void)
{
	if (gauge_up) {
		return true;
	}
	if (tlmm == 0) {
		return false;
	}
	/* a gauge that never answers is asked again once a minute (the charger reports every five
	 * seconds), not on every report: a probe is two transactions and a log line is not free */
	start_attempts++;
	if (start_attempts > 1 && (start_attempts % 12) != 0) {
		return false;
	}
	/* Two candidate buses, the runtime tree's first. Its gauge sits on SE15, i2c@0xc94000, whose
	 * active pinctrl state names gpio27 and gpio28 in the east tile; the merged tree's SE8 claim,
	 * gpio88 and gpio89 in the north tile, is a different variant of this board and the north
	 * pair has never answered. Each pair is woken and asked in both orders. */
	static const struct {
		const char *what;
		unsigned long tile_off;
		unsigned a, b;
	} buses[] = {
		{ "SE15, the runtime tree's gauge bus", TLMM_EAST, PIN_GPIO_A, PIN_GPIO_B },
		{ "SE8, the merged tree's claim", TLMM_NORTH, PIN_ALT_A, PIN_ALT_B },
	};

	for (unsigned i = 0; i < sizeof(buses) / sizeof(buses[0]); i++) {
		vm_offset_t tile = tile_map(buses[i].tile_off);

		if (tile == 0) {
			continue;
		}
		tlmm = tile;
		pin_release(buses[i].a);
		pin_release(buses[i].b);
		scl_pin = buses[i].a;
		sda_pin = buses[i].b;
		gauge_wake();
		if (gauge_probe_order(buses[i].a, buses[i].b) ||
		    gauge_probe_order(buses[i].b, buses[i].a)) {
			say("bq27541: the gauge answered on %s: gpio%u (clock) and gpio%u (data): "
			    "device type %#06x\n", buses[i].what, scl_pin, sda_pin, gauge_type);
			return true;
		}
		say("bq27541: %s: nothing at 0x%02x on gpio%u/gpio%u\n", buses[i].what,
		    GAUGE_ADDRESS, buses[i].a, buses[i].b);
	}
	/* the diagnostics stay on the runtime tree's pair: it is the one the phone's own kernel
	 * used, and the north pair was already scanned and found silent in the 16:01 run */
	tlmm = tile_map(TLMM_EAST);
	scl_pin = PIN_GPIO_A;
	sda_pin = PIN_GPIO_B;
	pin_release(scl_pin);
	pin_release(sda_pin);
	sleep_us(50);
	/* One line, once: a bus with no pull-up, a gauge with no power and a wrong pin pair all look
	 * the same from an address that never answers. The pin check comes first: a pad that does not
	 * read back what it drives would make every transaction here a lie. */
	if (start_attempts == 1) {
		pin_low(scl_pin);
		sleep_us(50);
		unsigned scl_driven = pin_level(scl_pin);
		pin_release(scl_pin);
		pin_low(sda_pin);
		sleep_us(50);
		unsigned sda_driven = pin_level(sda_pin);
		pin_release(sda_pin);
		sleep_us(50);
		say("bq27541: pins: driven low scl reads %u sda reads %u; released they read %u and %u "
		    "(cfg %#x %#x)\n", scl_driven, sda_driven, pin_level(scl_pin), pin_level(sda_pin),
		    rd(GPIO_CFG(tlmm, scl_pin)), rd(GPIO_CFG(tlmm, sda_pin)));
		say("bq27541: no answer at 0x%02x: %s\n", GAUGE_ADDRESS,
		    (scl_driven || sda_driven) ? "a pad does not drive low, the wiring is not ours to drive"
					       : "the wires are pulled up but nothing acknowledged");
		/* what else is on this bus: the overlay names four parts (0x26 the VOOC controller,
		 * 0x55 the gauge, 0x58 the short protection, 0x68 the divider); a scan says whether the
		 * pads and the bus itself are right, whatever the gauge is doing */
		unsigned found = 0;
		for (uint8_t address = 0x08; address <= 0x77; address++) {
			if (i2c_ping(address)) {
				say("bq27541:   address 0x%02x acknowledges\n", address);
				found++;
			}
		}
		say("bq27541: the scan found %u device%s\n", found, found == 1 ? "" : "s");
		tile_check(TLMM_EAST, PIN_GPIO_A, PIN_GPIO_B);
		control_scan();
	}
	return false;
}

/* The four readings in one set; false when any of them is not a number a battery could be at. The
 * TI set's numbers are OPPO's own (drivers/power/oppo/gauge_ic/oplus_bq27541.h): 0x2C SOC, 0x08
 * voltage, 0x14 average current, 0x06 temperature. 0x0A is the flags register and 0x0C is the
 * nominal available capacity -- neither is current or temperature. The SBS set gives the charge as
 * one byte at 0x0D (0x0E is a different quantity there, so the word read must not be used). */
static bool
gauge_set_read(int set, uint16_t *soc, uint16_t *volt, uint16_t *current, uint16_t *temp)
{
	if (set == 0) {
		return gauge_word(0x2C, soc) && gauge_word(0x08, volt) && gauge_word(0x14, current) &&
		    gauge_word(0x06, temp);
	}
	return gauge_byte(0x0D, soc) && gauge_word(0x09, volt) && gauge_word(0x0A, current) &&
	    gauge_word(0x08, temp);
}

static bool
gauge_plausible(uint16_t soc, uint16_t volt)
{
	return soc <= 100 && volt >= 2500 && volt <= 5000;
}

/*
 * What the charger driver asks for: the charge in percent, the pack voltage in microvolts, the
 * current in microamps (positive charging) and the temperature in hundredths of a degree Celsius.
 * Zero on success, -1 when the gauge has nothing to say. The gauge's own units: percent, mV, mA
 * (signed) and 0.1 K.
 */
int
hakios_bq27541_read(int *percent, int *voltage_uv, int *current_ua, int *temperature_hundredths)
{
	uint16_t soc, volt, current, temp;

	if (!gauge_up && !gauge_start()) {
		return -1;
	}
	if (gauge_set < 0) {
		/* the OPPO part is named after the BQ27541, so its set is tried first; the other set is
		 * the fallback for the BQ28Z610 the node's qcom,batt_bq28z610 flag also names */
		static const int order[2] = { 0, 1 };
		for (unsigned i = 0; i < 2; i++) {
			if (!gauge_set_read(order[i], &soc, &volt, &current, &temp)) {
				continue;
			}
			if (gauge_plausible(soc, volt)) {
				gauge_set = order[i];
				break;
			}
		}
		if (gauge_set < 0) {
			goto failed;
		}
		say("bq27541: the %s register set reads back plausibly (soc %u%%, %u mV)\n",
		    gauge_set == 0 ? "TI standard" : "SBS", soc, volt);
	}
	if (!gauge_set_read(gauge_set, &soc, &volt, &current, &temp) || !gauge_plausible(soc, volt)) {
		goto failed;
	}
	*percent = (int)soc;
	*voltage_uv = (int)volt * 1000;
	*current_ua = (int)(int16_t)current * 1000;
	*temperature_hundredths = (int)temp * 10 - 27315;      /* 0.1 K to hundredths of a degree C */
	return 0;
failed:
	if (++gauge_failures <= 3 || gauge_failures % 30 == 0) {
		say("bq27541: the gauge stopped answering (failure %d)\n", gauge_failures);
	}
	return -1;
}

__startup_func
static void
bq27541_init(void)
{
	uint32_t enabled = 0;

	if (!PE_parse_boot_argn("bq27541", &enabled, sizeof(enabled)) || !enabled) {
		return;                                /* nothing is read unless a boot-argument asks */
	}
	tlmm = tile_map(TLMM_EAST);
	if (tlmm == 0) {
		say("bq27541: the east TLMM tile does not map; the gauge stays unread\n");
		return;
	}
	pin_release(PIN_GPIO_A);
	pin_release(PIN_GPIO_B);
	/* the probe runs from the charger's thread, not here: the first transaction needs microsecond
	 * delays, and early boot is where the timer is least able to give them (see sleep_us()) */
}
STARTUP(EARLY_BOOT, STARTUP_RANK_LAST, bq27541_init);
