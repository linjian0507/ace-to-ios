/*
 * The touch screen of the OPPO Reno Ace: a Synaptics S3706 ("synaptics19081@20" in the phone's own
 * dtbo) on serial engine 17 of the second QUP wrapper, at I2C address 0x20. Only with boot-arg
 * touch=1; every address below belongs to this phone.
 *
 * This is the bring-up, not the driver yet: it turns the bus on and reads the chip's product ids, and
 * says what every step found. Touch reports, multi-touch, and the haptic that belongs to the panel come
 * after it, once the ids have been seen.
 *
 * Where the numbers come from.
 *
 *   bus      /soc/i2c@0xc80000 of the boot's device tree: reg 0xc80000, clock-names "se-clk", "m-ahb",
 *          "s-ahb" with clocks <&gcc 0x6d 0x7d 0x7e>, which dt-bindings/clock/qcom,gcc-sm8150.h names
 *          GCC_QUPV3_WRAP2_S0_CLK (109), GCC_QUPV3_WRAP_2_M_AHB_CLK (125) and
 *          GCC_QUPV3_WRAP_2_S_AHB_CLK (126); qcom,wrapper-core is qcom,qupv3_2_geni_se@cc0000. The
 *          pins are its qupv3_se17_i2c_active state: gpio55 and gpio56 to function "qup17" -- index 1
 *          of those pins' functions in drivers/pinctrl/qcom/pinctrl-sm8150.c -- with drive-strength 4
 *          and bias-disable.
 *   clocks   drivers/clk/qcom/gcc-sm8150.c: the three branches' halt registers 0x1e144, 0x1e004 and
 *          0x1e008 (bit 31: off), all voting in 0x52014 at bits 4, 2 and 1; the wrapper's reset at
 *          0x1e000; the serial engine's source clock at 0x1e148 with its configuration at 0x1e14c,
 *          whose 19.2 MHz entry is the crystal, undivided, at source 0 (ftbl_gcc_qupv3_wrap0_s0_clk_src
 *          and gcc_parent_map_0). The slave AHB branch has an hwcg_reg of its own, 0x1e008 bit 1: while
 *          that bit is set the hardware gates the clock between bus transactions and its halt bit says
 *          nothing about the bus, so it has to be cleared before the vote is worth waiting for -- the
 *          newer clock driver's clk_branch_toggle() clears it, and v6.6's clk_branch_wait() only stops
 *          waiting in that case.
 *   engine   include/linux/soc/qcom/geni-se.h and drivers/{i2c/busses/i2c-qcom-geni.c, soc/qcom/
 *          qcom-geni-se.c} for every register and command: the protocol a serial engine answers at
 *          0x68[15:8] is 3 for I2C, a command is (opcode << 27) | parameters with the address at
 *          [15:9], writes go to 0x700 and reads come from 0x780, and the SCL counters are
 *          t_high << 20 | t_low << 10 | t_cycle with SCL = source / (t_cycle * divider), which is the
 *          400 kHz row of the vendor's table (divider 2, 5, 12, 24) against the 19.2 MHz crystal. The
 *          register at 0x64 is read-only status (GENI_IF_DISABLE_RO), and no driver of either tree
 *          writes it: the 4.19 i2c-qcom-geni.c, the shape the vendor's i2c-msm-geni.c descends from,
 *          never reads it at all and drives this bus through the FIFO, and v6.6 reads it only to
 *          choose between FIFO and GPI DMA. So this driver treats it as information, resets the engine
 *          with 0x20 as both do, and lets the FIFO depths and a real transaction decide.
 *   chip     the dtbo's synaptics19081@20: reg 0x20, vdd_2v8 from pm8150_l17, enable1v8_gpio from
 *          GPIO5 of the PM8150L's GPIO controller (slave 4, pinctrl@c000: that pin is peripheral
 *          0xc4, and qcom,spmi-gpio answers type 0x10), reset on TLMM gpio54 and the interrupt line on
 *          gpio122. The two id registers are the RMI4 page zero product id at 0x0000 and the TCM
 *          bootloader's at 0x0170, both ten bytes, read by writing the address first and then reading,
 *          as drivers/hid/hid-rmi.c and the vendor's synaptics_tcm driver do.
 *   bit-bang the engine's sequencer does not start on this phone's wrapper -- the 15:05 run: the
 *          address write never completed, and asking the wrapper for the engine's sequencer changed
 *          0 into 0 -- so the last attempt is the pads themselves. Each wire is open drain: driven
 *          low as a TLMM output with its data bit 0, released as an input with the pull-up, the
 *          registers at 0x1000 * pin in the tile tlmm_probe found. The half period is the twelve
 *          microseconds the fuel gauge driver (hakios.bq27541.c) uses on this phone, about 40 kHz.
 *          Which pin is the clock the tree does not say, and the 16:01 run settled it: the chip
 *          answered its address only when gpio56 was moved as the clock, so that is the order the
 *          code uses. The three addressings tried are the three the sources name: the one-byte
 *          register write the vendor's own synaptics_drivers_s3706.c uses for every register (its
 *          touch_i2c_read_block(client, 0xE9, 4, ...) calls, whose register argument is one byte),
 *          the two-byte page-zero address hid-rmi.c writes, and the TCM bootloader's four-byte
 *          read command for 0x0170 -- opcode 0, the address little-endian, the length byte. The
 *          clock line is waited out every time it is released, because the TCM bootloader holds it
 *          down for tens of milliseconds while it prepares a read, and the first ask is written in
 *          the vendor driver's own two-message shape -- write the register, a stop, read it back --
 *          because that is the shape its touch_i2c_read_block puts on the bus.
 *   pmic     the tree's qcom,spmi@c440000: reg-names core, chnls, obsrvr. drivers/spmi/spmi-pmic-arb.c
 *          of the vendor's 4.19, which is the arbiter's version 5, reads through "obsrvr" at
 *          obsrvr + 0x80 * channel but writes through "chnls" at chnls + 0x10000 * channel, with the
 *          command (2 << 27 | reg << 4) at +0x0, the byte at +0x10 and the done bit in +0x8: a write
 *          aimed at the observer's data register, which is 0x18 and read-only there, does not land.
 *          The high address byte of a transaction comes from the channel's own ppid, so the enable pin
 *          is reached through the channel whose ppid is (4, 0xc4), not through its GPIO block's. And
 *          the pin's subtype 0x10 is the "LV" kind, whose registers pinctrl-spmi-gpio.c of that same
 *          4.19 reads differently from the older ones: the direction is 0x40 [1:0] (1 is output), the
 *          level is 0x44 bit 7 with 0x44 [3:0] = 0 for the software source, and 0x46 bit 7 enables the
 *          pin; 0x40 read on an LV pin shows the subtype, which is what looked like "mode 0x10".
 *
 * What it does to the phone, and what it leaves alone:
 *
 *   writes   in the GCC, the three clock votes and the serial engine's clock configuration; in the TLMM,
 *          the two I2C pins and the reset pin; the wrapper's clock gates and the serial engine's own
 *          registers; and -- only if the 1.8 V enable is off and the chip did not answer -- three
 *          registers of one GPIO of the PM8150L to switch that enable on. The 2.8 V (pm8150_l17) is
 *          an RPMh regulator, and the runtime tree gives that same regulator to the display as its
 *          lab-supply: the display is running from it, so the touch's 2.8 V is already there and the
 *          vote is left alone unless touch_2v8=1 asks for it. Sending it anyway, with a mode the
 *          resource does not support, is what blanked the panel at 19:42 and 19:52.
 *   reads    everything else, including the interrupt line, which is only looked at, never claimed.
 *
 * The steps, each said before it is made. Boot-arg touch_steps=N stops after N:
 *   1  the serial engine's clocks: read what they are, ask for them, and set the source to the crystal   (writes)
 *   2  the QUP wrapper: its version, its capability register, and the clock gates nothing has opened    (writes)
 *   3  the serial engine: what protocol it is, whether the FIFO interface is there, how deep the FIFOs are
 *   4  the pins: whether the pin controller answers at all, then what the two bus pins and the reset
 *      and interrupt lines are now, and the two bus pins to qup17                                    (writes)
 *   5  the 1.8 V enable: what the PM8150L's GPIO says, and switching it on if it is off             (writes, if)
 *   6  configure the engine: I2C, FIFO mode, 400 kHz                                                  (writes)
 *   7  let the chip out of reset and read its product ids; if a command does not finish, say what
 *      the engine itself sees -- its command, its lengths, the wires as it drives them, and how
 *      fast its clock really runs; and when the engine has been asked everything the vendor's
 *      driver asks and still does not finish a command, the same ids are read through the pads,
 *      the two wires moved by hand. Once the pads make the chip answer, the vendor's own chip-info
 *      read runs -- page select, the three function bases, the F01 query region that spells the
 *      chip's name -- and then the vendor's own watch: its page select before every read, its
 *      F01 status read, its object attention word read where its get_touch_points reads it and
 *      its object data in four-byte reads, kept for two hundred passes that start at the kernel's
 *      two hundredth second, after the home screen is up; which needs a finger on the glass
 *                                                           (default; writes to the chip)
 *   8  ask for the two votes neither of this soc's clock drivers claims -- bits 0 and 3 of the
 *      wrapper's vote word, which mainline leaves to the rpmh's bus clock manager -- and read the
 *      chip once more; only reached when step 7's chip did not answer                              (writes)
 *   9  hand that vote to the rpmh itself: put the QUP0 bus clock manager's cell into one of the
 *      two active-only tcs slots of apps_rsc and trigger it, exactly as the vendor's bus driver
 *      does, and read the chip once more; only reached when step 8 changed nothing                (writes)
 *  10  the cell's real address: the vendor's bus driver never uses the device tree's cell-id, it
 *      asks the boot loader's command database (cmd_db_get_addr, drivers/soc/qcom/cmd-db.c) for
 *      the resource named QUP0 and sends the vote to the address that says. Read the pointer the
 *      tree's qcom,cmd-db@c3f000c node points at, parse the table, and send the same vote to the
 *      address it lists -- which turns out to be 0x00050080, not the cell-id 0x1b80 step 9 sent:
 *      the table's address is a slave id and a cell offset, and the cell-id is only the fabric
 *      graph's node number. If the whole word does not make the chip answer, its low half alone
 *      is tried too. Only reached when step 9's vote did not make the chip answer.             (writes)
 *  11  what "the slot never freed" meant: the vendor's mailbox driver never waits on the slot's
 *      status word the way step 9 did. Its tcs_is_free() wants that word non-zero, and its
 *      tcs_irq_handler judges a send by the controller's interrupt bit for the slot and by the
 *      command's own issued and completed bits, then acknowledges the interrupt and disarms the
 *      commands, which is what makes the slot usable again. Steps 9 and 10 are asked that way
 *      now, and the wrong address of step 9 and the right one of step 10 answer differently.   (writes)
 *
 * Boot-args:
 *   touch=1             turn the bus on and read the chip's two product id registers
 *   touch_steps=N       stop after step N
 *   touch_sweep=1       re-run the sweep that asks every PMIC GPIO which one is the 1.8 V enable.
 *                       The 14:42 run answered that question (the PM8150L's pin 5, active high),
 *                       so the normal path drives that pin directly and only asks again on request.
 */
#include <kern/clock.h>
#include <kern/cpu_data.h>
#include <kern/startup.h>
#include <kern/thread.h>
#include <libkern/libkern.h>
#include <mach/mach_types.h>
#include <pexpert/pexpert.h>
#include <sys/hakios.input.h>
#include <sys/systm.h>

extern vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);
extern vm_offset_t ml_io_map_wcomb(vm_offset_t phys_addr, vm_size_t size);
extern kern_return_t thread_terminate(thread_t thread);
extern void loader_log_putc(char c);
extern void hakios_spmi_lock(void);
extern void hakios_spmi_unlock(void);

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

static void
sleep_us(uint32_t us)
{
	delay_for_interval(us, NSEC_PER_USEC);
}

static void
sleep_ms(uint32_t ms)
{
	delay_for_interval(ms, NSEC_PER_MSEC);
}

static vm_offset_t gcc, tlmm, qup, se;

/* ---- the clocks (GCC) ---------------------------------------------------------------------------- */

#define GCC_PHYS                0x00100000UL
#define GCC_SIZE                0x080000UL
#define GCC_QUP2_BCR            (gcc + 0x1e000)         /* the wrapper's reset: bit 0 holds it */
#define BCR_RESET               (1u << 0)
#define GCC_VOTE                (gcc + 0x52014)         /* what the application processor asks the hardware for */
#define VOTE_QUP2_S_AHB         (1u << 1)               /* the bits both this soc's clock drivers use -- the
                                                          * vendor's drivers/clk/qcom/gcc-sm8150.c and
                                                          * mainline's are identical here, checked 2026-11-02 */
#define VOTE_QUP2_M_AHB         (1u << 2)
#define VOTE_QUP2_SE0           (1u << 4)               /* bits 0 and 3 of the same word neither of them
                                                          * touches: mainline leaves the wrapper's own core
                                                          * clock to the rpmh's bus clock manager instead.
                                                          * Step 8 asks for these two. */
#define VOTE_QUP2_UNCLAIMED_0   (1u << 0)
#define VOTE_QUP2_UNCLAIMED_3   (1u << 3)
#define GCC_QUP2_M_AHB_CBCR     (gcc + 0x1e004)         /* bit 31: the clock is off */
#define GCC_QUP2_S_AHB_CBCR     (gcc + 0x1e008)
#define HWCG_S_AHB              (1u << 1)               /* bit 1: the hardware gates this one between transactions */
#define GCC_QUP2_SE0_CBCR       (gcc + 0x1e144)
#define GCC_QUP2_SE0_CMD        (gcc + 0x1e148)         /* bit 0: take the new configuration */
#define GCC_QUP2_SE0_CFG        (gcc + 0x1e14c)         /* [4:0] divider-1, [10:8] source, [13:12] mode, bit 20 hardware control */
#define CLOCK_OFF               (1u << 31)
#define CMD_UPDATE              (1u << 0)               /* the command register's "take the configuration" */
#define CMD_ROOT_EN             (1u << 1)               /* ... and its "turn the root on" (clk-rcg2.c names it) */
#define CMD_ROOT_OFF            (1u << 31)              /* bit 31: the root is off, so the clock is stopped */
#define CFG_KEEP                0xffe00000u             /* the parts of a configuration register that are not ours to
                                                         * change -- and bit 20 is not among them: that bit is the
                                                         * clock's hardware control bit (CFG_HW_CLK_CTRL_MASK in
                                                         * clk-rcg2.c), this clock is not one of those declared to be
                                                         * hardware controlled, and _freq_tbl_set_rate() clears the bit
                                                         * whenever it writes a rate. The first run on this phone read
                                                         * 0x100000 here: the phone's own firmware had left the
                                                         * engine's clock under hardware control, where the divider
                                                         * written below is not what decides the rate. */
#define XO_HZ                   19200000u

static const char *
clocks_on(void)
{
	uint32_t cfg = rd(GCC_QUP2_SE0_CFG), source = (cfg >> 8) & 7, divider = (cfg & 0x1f) + 1;
	uint32_t s_ahb, cmd;

	say("touch: gcc: se17 clock now: cfg %#x (source %u, divider %u%s), cmd %#x, halt %#08x; wrapper reset %#x\n",
	    cfg, source, divider, divider == 1 ? ", the crystal undivided" : "", rd(GCC_QUP2_SE0_CMD),
	    rd(GCC_QUP2_SE0_CBCR), rd(GCC_QUP2_BCR));
	if (rd(GCC_QUP2_BCR) & BCR_RESET) {
		/* nothing on this bus can answer while the wrapper is held; the vendor's driver deasserts it
		 * the same way, and no other HakiOS driver has a serial engine on this wrapper */
		wr(GCC_QUP2_BCR, rd(GCC_QUP2_BCR) & ~BCR_RESET);
		say("touch: gcc: the QUP wrapper was held in reset: released (%#x)\n", rd(GCC_QUP2_BCR));
	}
	/* the source first, as clk_rcg2_configure() and update_config() do: write the configuration, ask
	 * for it, and wait for the request bit to clear. Source 0 is the 19.2 MHz crystal and [4:0] 0 is
	 * divider 1, which is what the 400 kHz table below is counted against, and bit 20 goes with them:
	 * a clock that is not hardware controlled has that bit cleared when its rate is written. */
	wr(GCC_QUP2_SE0_CFG, rd(GCC_QUP2_SE0_CFG) & CFG_KEEP);
	wr(GCC_QUP2_SE0_CMD, rd(GCC_QUP2_SE0_CMD) | CMD_UPDATE);
	for (uint32_t i = 0; i < 500; i++) {
		if (!(rd(GCC_QUP2_SE0_CMD) & CMD_UPDATE)) {
			break;
		}
		sleep_us(10);
	}
	if (rd(GCC_QUP2_SE0_CMD) & CMD_UPDATE) {
		return "the serial engine's clock does not take its configuration";
	}
	/* The root itself. clk-rcg2.c reads bit 31 of this same register as CMD_ROOT_OFF and calls the
	 * clock enabled only when that bit is clear. Nothing in the framework writes it for an ordinary
	 * rcg -- clk_rcg2_ops has no enable op at all -- so when the boot firmware leaves a root off, the
	 * branch votes and the configuration above are all in place and the engine still gets a stopped
	 * clock. This phone's boot firmware did leave it off: the 02:02 run read `cmd 0x80000000` here,
	 * and step 6 measured the consequences -- the engine accepted a command (GO set, status 0x41) and
	 * never moved a single byte, with sda and scl both idle high the whole time. CMD_ROOT_EN is bit 1
	 * of the same register: ask for the root and wait for the hardware to clear ROOT_OFF, exactly as
	 * clk_rcg2_set_force_enable() does (write CMD_ROOT_EN, then wait for clk_rcg2_is_enabled()). */
	cmd = rd(GCC_QUP2_SE0_CMD);
	if (cmd & CMD_ROOT_OFF) {
		wr(GCC_QUP2_SE0_CMD, cmd | CMD_ROOT_EN);
		for (uint32_t i = 0; i < 500; i++) {
			if (!(rd(GCC_QUP2_SE0_CMD) & CMD_ROOT_OFF)) {
				break;
			}
			sleep_us(10);
		}
		say("touch: gcc: the engine clock's root was off (cmd %#x): asked for it, now %#x\n",
		    cmd, rd(GCC_QUP2_SE0_CMD));
	}
	/* The wrapper's slave-side AHB clock, the one register accesses travel over, is in hardware-
	 * controlled gating: gcc-sm8150.c gives that branch an hwcg_reg of its own (`.hwcg_reg = 0x1e008`,
	 * `.hwcg_bit = 1`), which is this same register's bit 1. While that bit is set the hardware gates
	 * the clock between bus transactions, so its halt bit says nothing about whether the bus works --
	 * v6.6's clk_branch_wait() returns at once in that mode, and newer Linux clears the bit on enable,
	 * as clk_branch_toggle() does. The first run on this phone stopped here for that reason alone
	 * (logs/181216-run.log: `clocks stay off: 0 0x80000002 0x4220`: the master clock and the engine's
	 * own answered the vote, the slave clock was gated with bit 1 still the hardware's). Take it out
	 * of hardware gating first, so that the vote below is what decides. */
	s_ahb = rd(GCC_QUP2_S_AHB_CBCR);
	if (s_ahb & HWCG_S_AHB) {
		wr(GCC_QUP2_S_AHB_CBCR, s_ahb & ~HWCG_S_AHB);
		say("touch: gcc: the slave AHB clock was hardware gated between transactions: asked for it to "
		    "stay on (%#x)\n", rd(GCC_QUP2_S_AHB_CBCR));
	}
	wr(GCC_VOTE, rd(GCC_VOTE) | VOTE_QUP2_S_AHB | VOTE_QUP2_M_AHB | VOTE_QUP2_SE0);
	for (uint32_t i = 0; i < 1000; i++) {
		if (!((rd(GCC_QUP2_M_AHB_CBCR) | rd(GCC_QUP2_S_AHB_CBCR) | rd(GCC_QUP2_SE0_CBCR)) & CLOCK_OFF)) {
			say("touch: clocks on: m-ahb, s-ahb and the serial engine's, from the 19.2 MHz "
			    "crystal; its configuration now says %#x\n", rd(GCC_QUP2_SE0_CFG));
			return NULL;
		}
		sleep_us(10);
	}
	say("touch: clocks stay off: m-ahb %#x, s-ahb %#x%s, the engine's %#x; the votes say %#x\n",
	    rd(GCC_QUP2_M_AHB_CBCR), rd(GCC_QUP2_S_AHB_CBCR),
	    (rd(GCC_QUP2_S_AHB_CBCR) & HWCG_S_AHB) ? ", still hardware gated" : "", rd(GCC_QUP2_SE0_CBCR),
	    rd(GCC_VOTE));
	return "the clocks stay off";
}

/* A pause the run can watch. The harness asks the phone for this log every two seconds while the
 * run lives, and a hard reset takes the log away with it: the last answer it got is what survives.
 * So a risky access is announced first and followed by a hold longer than the asking interval --
 * by the time the access hangs the bus, the announcement is already in a copy of the log the
 * harness has put on disk, and that copy says which address stopped the phone. The 20:43 and
 * 20:52 runs reset inside the first two seconds, before the harness had asked once, and left
 * nothing behind. */
#define WATCH_MS                2600
static void
pause_ms(uint32_t ms)
{
	uint64_t deadline;

	clock_interval_to_deadline(ms, NSEC_PER_MSEC, &deadline);
	while (mach_absolute_time() < deadline) {
	}
}

/* ---- the pins (TLMM) ----------------------------------------------------------------------------- */

#define TLMM_PHYS               0x03000000UL            /* /soc/pinctrl@03000000, which the tree names tlmm */
#define TLMM_SIZE               0x100000UL
#define GPIO_CFG(n)             (tlmm + 0x1000UL * (n)) /* [1:0] pull, [5:2] function, [8:6] drive, [9] output enable,
                                                          * inside the tile that owns the pin -- which is the south
                                                          * tile for every pin this driver needs, once tlmm_probe
                                                          * has found it and pointed tlmm there */
#define GPIO_IO(n)              (tlmm + 0x1000UL * (n) + 4)     /* bit 0: the level on the pin, bit 1: the level it drives */
#define FUNCTION_GPIO           0
#define FUNCTION_QUP17          1
#define DRIVE_4MA               1                       /* the 4 mA step: (2 * code) - 2, as pinctrl-msm.c computes it */
#define OUTPUT                  (1u << 9)

#define PIN_RESET               54                      /* the dtbo's reset-gpios: low holds the chip in reset */
/* Which of the pair is the clock and which the data the tree does not say: its pinctrl state names
 * both pins with one function, qup17, and lets the engine's hardware tell them apart. The 16:01 run
 * asked through the pads and the chip answered its address only when gpio56 was moved as the clock --
 * with the roles the other way round it never saw a start. So: gpio56 is the clock, gpio55 the data.
 * The engine does not care which name we give either pin; it muxes both to qup17 and drives them
 * itself. */
#define PIN_SCL                 56
#define PIN_SDA                 55
#define PIN_IRQ                 122                     /* low: the chip has something to say. Only looked at. */

static uint32_t
pin_bits(unsigned function, unsigned pull, unsigned drive, bool output)
{
	return (uint32_t)function << 2 | (uint32_t)pull | (uint32_t)drive << 6 | (output ? OUTPUT : 0);
}

static void
report_pin(const char *what, unsigned pin)
{
	uint32_t cfg = rd(GPIO_CFG(pin)), io = rd(GPIO_IO(pin));

	say("touch: gpio%u (%s): cfg %#x (function %u, pull %u, drive code %u%s), io %#x (in %u, out %u)\n",
	    pin, what, cfg, cfg >> 2 & 0xf, cfg & 3, cfg >> 6 & 7, cfg & OUTPUT ? ", output" : "", io, io & 1,
	    io >> 1 & 1);
}

static void
pins_bus(void)
{
	/* the tree's active state for these two pins: qup17, 4 mA, no pull. The output enable is left as
	 * it is: the engine drives the clock and the data line itself, and the data line is read too. */
	const unsigned pair[] = { 55, 56 };

	for (unsigned i = 0; i < 2; i++) {
		unsigned pin = pair[i];
		uint32_t want = pin_bits(FUNCTION_QUP17, 0, DRIVE_4MA, false), now = rd(GPIO_CFG(pin));

		if ((now & 0x3ff) != (want & ~OUTPUT)) {
			wr(GPIO_CFG(pin), (now & ~0x3ffu) | (want & ~OUTPUT));
			say("touch: gpio%u set to qup17, 4 mA, no pull: %#x -> %#x\n", pin, now, rd(GPIO_CFG(pin)));
		} else {
			say("touch: gpio%u is already qup17, 4 mA, no pull (%#x)\n", pin, now);
		}
	}
	/* The reset line is part of this step too, and it was the reason no run ever saw the chip: the
	 * tree says `reset-gpio = <&tlmm 0x36 0x01>` -- gpio54, active low, so high is out of reset --
	 * and the boot firmware left it at its pull-down (cfg 0x1, level 0). The chip has been held in
	 * reset since the phone booted. Drive it high and leave it there. */
	wr(GPIO_CFG(PIN_RESET), pin_bits(FUNCTION_GPIO, 0, DRIVE_4MA, true));
	wr(GPIO_IO(PIN_RESET), 2);                      /* the data bit: 1 drives the line high */
	say("touch: gpio54 (the reset, active low) driven high: cfg %#x io %#x\n", rd(GPIO_CFG(PIN_RESET)),
	    rd(GPIO_IO(PIN_RESET)));
}

/* Step 4's other half: finding the tile that owns the pins.
 * At 19:05 every read of the window at 0x03000000 came back zero and no write landed; at 19:49
 * gpio0's register there read 0x1, so that window is alive, and yet gpio55 -- which the boot
 * firmware has on the engine's function, since the bus carried the chip's id over it -- reads 0
 * there and ignores writes. Upstream's v6.6 driver says why: sm8150's pins are split over four
 * tiles, and a pin's register is 0x1000 * pin inside its own tile, not inside one flat window --
 * pinctrl-msm.c reads at pctrl->regs[g->tile] + g->ctl_reg -- and pinctrl-sm8150.c puts every
 * pin we need, 54, 55, 56 and 122, in the south tile. Its sm8150.dtsi maps the tiles at
 * 0x3100000 west, 0x3500000 east, 0x3900000 north and 0x3d00000 south; our tree's single window,
 * 0x3000000 + 0xdc2000, starts a megabyte before the first of them, and the 21:01 run settled
 * what that first megabyte is: reading its first word hung the bus and reset the phone. It is not
 * the pin controller. So the scan below tries the four tiles upstream names, at their offsets
 * inside the tree's window, and nothing else.
 *   found     -- tlmm points at the tile that answers at the pins' slots, which is the one that
 *                owns them, and pins_bus() can do its job: it is the first thing to put these
 *                pins on the engine's function at all
 *   not found -- the tiles are not where either tree draws them; what every candidate reads is
 *                printed, and the run stops there rather than writing at the dead base.
 * The 21:14 run settled two things at once. The south tile at +0xd00000 is real: it answered at
 * all three of the pins' slots while the other tiles, whose own pins are elsewhere, read zero.
 * And the pins are idle -- function 0, the lines biased, the interrupt line pulled down -- so the
 * boot firmware never used this bus and never muxed these pins, which an earlier version of this
 * comment assumed it had. What follows is therefore not a check of someone else's work but the
 * first configuration these pins ever get. */
static void
tlmm_probe(void)
{
	/* Only the four tiles upstream names, in the order the answer matters: our pins -- gpio54,
	 * 55, 56 and 122 -- are all in the south tile, so it is read first and a hit there ends
	 * the search. The tree's base itself is not on the list: the 21:01 run hung the bus on the
	 * first read of it, physical 0x03000000, and that address is not the pin controller -- the
	 * vendor's window starts a megabyte before the first real tile, and upstream's reg-names
	 * put west at 0x03100000, east at 0x03500000, north at 0x03900000 and south at
	 * 0x03d00000, each 0x300000 long. The offsets below are from the tree's base, which is
	 * inside the loader's window, so every one of them can be mapped. Nothing reads tile + 0:
	 * only the pins' own slots, which is what a hit is judged by. */
	static const struct { const char *what; unsigned long off; } tiles[] = {
		{ "upstream's south", 0x00d00000UL },
		{ "upstream's west", 0x00100000UL },
		{ "upstream's east", 0x00500000UL },
		{ "upstream's north", 0x00900000UL },
	};
	vm_offset_t south = 0;
	unsigned long south_off = 0;

	for (unsigned i = 0; i < sizeof(tiles) / sizeof(tiles[0]); i++) {
		say("touch: tlmm: now reading %s at +%#lx\n", tiles[i].what, tiles[i].off);
		pause_ms(WATCH_MS);
		vm_offset_t tile = ml_io_map(TLMM_PHYS + tiles[i].off, 0x80000UL);

		if (tile == 0) {
			say("touch: tlmm: %s at +%#lx: could not map it\n", tiles[i].what, tiles[i].off);
			continue;
		}
		uint32_t cfg55 = rd(tile + 0x1000UL * 55), cfg56 = rd(tile + 0x1000UL * 56),
			 cfg122 = rd(tile + 0x1000UL * PIN_IRQ);

		say("touch: tlmm: %s at +%#lx: gpio55 %#x, gpio56 %#x, gpio122 %#x\n", tiles[i].what,
		    tiles[i].off, cfg55, cfg56, cfg122);
		/* the tile that owns our pins is the only one that answers at their slots. The other
		 * tiles hold their own pins' registers at those offsets, and upstream puts 54, 55, 56
		 * and 122 all in the south one. The 21:14 run showed the test works: south answered
		 * 0x1, 0x1 and 0x3 -- function 0 with the lines biased, the interrupt line pulled
		 * down, the state of pins nothing has ever used -- while the other three read zero. */
		if ((cfg55 | cfg56 | cfg122) != 0) {
			south = tile;
			south_off = tiles[i].off;
			break;
		}
	}
	if (south == 0) {
		say("touch: tlmm: none of the four tiles answers at the pins' slots: the tiles are not "
		    "where either tree draws them, and the pins stay as they are\n");
		return;
	}
	tlmm = south;
	say("touch: tlmm: the pins' tile is at +%#lx: reading and writing the pins from there\n",
	    south_off);
	/* no write test here: the pins' real state is already visible -- the 21:14 run read 0x1,
	 * 0x1 and 0x3 at the three slots -- and what step 4 really needs is the one write that
	 * must land, the two bus pins' function bits, a few lines below in pins_bus(), where
	 * report_pin() will show the result. */
}

/* ---- the 1.8 V enable (a GPIO of the PM8150L) ----------------------------------------------------- */

#define ARB_CORE_PHYS           0x0c440000UL
#define ARB_CORE_SIZE           0x1100UL
#define ARB_OBSERVER_PHYS       0x0e600000UL
#define ARB_OBSERVER_SIZE       0x10000UL
#define ARB_CHNLS_PHYS          0x0c600000UL            /* the tree's second reg, "chnls": where writes go.
                                                          * The vendor's spmi-pmic-arb.c of this generation
                                                          * reads through "obsrvr" and writes through this,
                                                          * and at 19:05 a write aimed at the observer did
                                                          * not land. The tree gives it 0x2000000, one
                                                          * 0x10000 block per channel; this driver writes
                                                          * through one channel, so it maps that channel's
                                                          * own block, at its own offset: the 19:49 run
                                                          * mapped the base and wrote at the channel's
                                                          * offset, outside the mapping, and took a data
                                                          * abort there. */
#define ARB_CHNLS_SIZE          0x1100UL
#define ARB_SYSTEM              0
#define ARB_VERSION             0x000
#define ARB_VERSION_5           0x50000000u
#define ARB_VERSION_7           0x70000000u
#define ARB_FEATURES            0x004           /* [10:0] the number of channels */
#define ARB_CHANNEL(n)          (0x900 + 4 * (n))       /* [19:8] the slave and peripheral behind channel n */
#define ARB_CNFG_PHYS           0x0c40a000UL            /* the tree's fifth reg, "cnfg". Version 5 maps one
                                                          * peripheral's ppid to several apids, one per execution
                                                          * environment, and cnfg names the owner of each: the
                                                          * vendor's pmic_arb_read_apid_map_v5() reads it at
                                                          * cnfg + 0x700 + 4 * apid and keeps, of all the
                                                          * mappings for a ppid, the one its own EE owns -- only
                                                          * an owner may write through its apid. */
#define ARB_CNFG_SIZE           0x1800UL                /* 0x700 + 4 * 511 reaches 0x16fc */
#define ARB_OWNER(n)            (0x700UL + 4 * (n))     /* [2:0]: the execution environment owning apid n */
#define ARB_MAX_CHANNELS        512

#define OBSERVER_COMMAND        0x00
#define OBSERVER_STATUS         0x08
#define OBSERVER_DATA0          0x18
#define OBSERVER_DATA1          0x1c
#define STATUS_DONE             0x1
#define STATUS_FAILURE          0x2
#define STATUS_DENIED           0x4
#define STATUS_DROPPED          0x8
#define COMMAND_READ            (1u << 27)              /* the arbiter's EXT_READL opcode */
#define READ_TIMEOUT_US         2000

/* A write goes in the same command field, but with the EXT_WRITEL opcode -- the one every real PMIC
 * register write in the vendor's driver uses, qpnpint_spmi_write() among them -- which is opcode 0,
 * so the command is the register byte shifted and nothing else. The 21:20 run showed what the other
 * extended write, EXT_WRITE (2), costs: the arbiter answers DONE, no failure, no denial, and the
 * PMIC never takes the byte -- the register reads back exactly as before, every time, and only a
 * write that happened to match what was already there looked like it worked. The data goes in the
 * channel's own write data register, which only the chnls window has. */
#define CHANNEL_COMMAND         0x00
#define CHANNEL_STATUS          0x08
#define CHANNEL_WDATA0          0x10
#define CHANNEL_WRITE           (0u << 27)              /* EXT_WRITEL */

#define ENABLE_SLAVE            4                       /* the PM8150L */
#define ENABLE_BLOCK            0xc0                    /* its GPIO controller, pinctrl@c000 */
#define GPIO_TYPE               0x10                    /* what qcom,spmi-gpio answers at 0x04 */
#define ENABLE_PERIPHERAL       0xc4                    /* the block's fifth 0x100-wide peripheral, which
                                                          * the phone's tree calls gpio5@c400: its pins are
                                                          * numbered from 1, and this is the one its
                                                          * enable1v8_gpio names. Each pin is a peripheral
                                                          * of its own, and the arbiter reaches it through
                                                          * a channel of its own: the one whose ppid is
                                                          * (4, 0xc4). */
#define ENABLE_GPIO             5                       /* the enable pin's number in the tree, for the log */

/* pinctrl-spmi-gpio.c's registers for this subtype (0x10, the "LV" kind), offsets within the pin's own
 * peripheral. The old-style registers the driver aimed at before (direction in 0x40 [6:4], the level in
 * 0x45) are what the MV/older PMICs have; an LV pin keeps the direction in 0x40 [1:0] and the level in
 * 0x44 bit 7, and reading 0x40 there shows the subtype, which is the 0x10 that looked like a mode. */
#define PMIC_RT_STS             0x10                    /* bit 0: the level on the pin, right now */
#define PMIC_MODE_CTL           0x40                    /* [1:0]: 0 input, 1 output, 2 in and out, 3 analog */
#define PMIC_LV_MODE_OUTPUT     1
#define PMIC_LV_DIG_OUT_SRC     0x44                    /* [3:0] what drives the output: 0 is the register
                                                          * below; bit 7: the level itself, high when set */
#define PMIC_LV_SOURCE_REGISTER  0
#define PMIC_LV_OUT_HIGH        (1u << 7)
#define PMIC_DIG_VIN_CTL        0x41                    /* [2:0] what the output swings between */
#define PMIC_DIG_PULL_CTL       0x42                    /* [2:0] 4 down, 5 none */
#define PMIC_PULL_DOWN          4
#define PMIC_PULL_NONE          5
#define PMIC_LV_MODE_INPUT_OUTPUT 2                   /* the output still drives and the input path is
                                                         * open too: the only configuration in which
                                                         * 0x10 says what the pin is doing */
#define PMIC_DIG_OUT_CTL        0x45                    /* [1:0] how hard, [5:4] what kind. Upstream's
                                                         * config_set writes this for every subtype; it
                                                         * is the one register this driver had not
                                                         * written, and a pin that is not driving is the
                                                         * difference it makes. */
#define PMIC_OUT_TYPE_CMOS      (0u << 4)               /* push-pull */
#define PMIC_OUT_STRONG         3                       /* upstream's PMIC_GPIO_OUT_STRENGTH_HIGH */
#define PMIC_EN_CTL             0x46                    /* bit 7: the pin is enabled at all */
#define PMIC_MASTER_EN          (1u << 7)

static vm_offset_t arb_core, arb_observer, arb_chnls, arb_config;
static uint32_t enable_channel = ~0u;

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

/* One register to the enable pin's peripheral, and read back to see whether it stayed. `reg` is the
 * whole register byte: the peripheral's high address byte comes from the channel's own ppid, which is
 * why the enable pin's channel must be the one that reaches (slave 4, peripheral 0xc4) and not its
 * GPIO block's, and why arb_chnls was mapped at that channel's block and not at the window's base.
 * The command is the vendor's v5 form, EXT_WRITEL (opcode 0, so nothing above the address field)
 * with reg << 4, into the channel's command register, with the byte in that channel's write data
 * register. */
static const char *
spmi_write(uint8_t reg, uint8_t value)
{
	vm_offset_t ch = arb_chnls;
	uint64_t start = mach_absolute_time(), now, interval;
	uint8_t back = 0xff;
	const char *problem;
	uint32_t status;

	nanoseconds_to_absolutetime(READ_TIMEOUT_US * (uint64_t)NSEC_PER_USEC, &interval);
	hakios_spmi_lock();
	wr(ch + CHANNEL_WDATA0, value);
	wr(ch + CHANNEL_COMMAND, CHANNEL_WRITE | (uint32_t)reg << 4);
	do {
		status = rd(ch + CHANNEL_STATUS);
		now = mach_absolute_time();
	} while (!(status & STATUS_DONE) && now - start < interval);
	hakios_spmi_unlock();
	if (!(status & STATUS_DONE)) {
		return "no answer to the write";
	} else if (status & STATUS_DENIED) {
		return "the write was denied";
	} else if (status & STATUS_FAILURE) {
		return "the write failed";
	} else if (status & STATUS_DROPPED) {
		return "the write was dropped";
	}
	problem = spmi_read(enable_channel, reg, &back, 1);
	if (problem != NULL) {
		return problem;
	}
	if (back != value) {
		return "it did not stay";
	}
	return NULL;
}

/* ---- the I2C bus (the serial engine in FIFO mode, polled) ----------------------------------------- */

#define QUP_PHYS                0x00cc0000UL            /* qcom,qupv3_2_geni_se@cc0000, the wrapper this engine belongs to */
#define QUP_SIZE                0x6000UL                /* the size the phone's own tree maps it at -- which is not
                                                         * the same as every word of it being there: the 23:27 run
                                                         * died reading the whole window, so this driver reads
                                                         * only the words the vendor's own driver touches */
#define QUP_HW_VER              (qup + 0x4)             /* [31:28] major, [27:16] minor, [15:0] step */
#define QUP_COMMON_CFG          (qup + 0x0)             /* read-only capability register, bit 18 saying whether
                                                         * there is a dma interface. The 23:27 run proved it is
                                                         * read-only: it reads 0x240d0600 and a write of the
                                                         * enable bits left it untouched, which is why the
                                                         * register this driver took for the clock control
                                                         * before is not one. */
#define QUP_SE_CLK_CTL_ENABLE   (qup + 0x8)             /* the wrapper's own clock gates: bit 0 the core, bit 1
                                                         * the serial engines, bits 9:8 the two sides of the ahb.
                                                         * The vendor's bus_enable writes all four every time it
                                                         * uses a bus; mainline never writes them because its
                                                         * clock driver, which we do not have, opens them
                                                         * through the rpmh. This is the one thing above the
                                                         * engine that has never been opened here, and the
                                                         * 22:52 run's symptom -- the sequencer takes fifo data
                                                         * and starts, the pins never move -- is exactly a
                                                         * sequencer with a clock and a serial side without
                                                         * one. */
#define QUP_SE_CLK_CTL_ENABLE_RO (qup + 0xc)            /* what those gates settled at */
#define CLK_CTL_ENABLE_ON       0x303u                  /* bits 0, 1, 8 and 9 together: the vendor's CLOCK_CTRL */
#define QUP_SE_STATE(n)         (qup + 0x80c + 4u * (n))/* one engine as the wrapper sees it: [3:2] what it is,
                                                         * bit 0 whether its sequencer is let run, bit 8 running a
                                                         * command, bit 4 idle. The vendor's driver sets the type
                                                         * and the enable before it touches an engine; mainline
                                                         * relies on the boot firmware having done that, and this
                                                         * phone's boot firmware never used this engine. */
#define SE_STATE_TYPE_SEQ       (2u << 2)               /* a sequencer-mode engine, which is what fifo i2c is */
#define SE_STATE_SEQ_EN         (1u << 0)               /* let its sequencer run */
#define SE_STATE_ACTIVE         (1u << 8)               /* running a command right now */
#define SE_STATE_IDLE           (1u << 4)               /* configured and waiting */

#define SE_PHYS                 0x00c80000UL            /* /soc/i2c@0xc80000 */
#define SE_SIZE                 0x4000UL

#define SE_FORCE_DEFAULT        (se + 0x020)
#define SE_OUTPUT_CTRL          (se + 0x024)            /* writing 0x7f hands the pins back to the engine */
#define SE_CGC_CTRL             (se + 0x028)            /* [6:0]: the engine's own clock gates. This is the
                                                         * register the 21:55 and 22:15 runs explain: with
                                                         * them closed the engine has no clock to clock a
                                                         * command out with, and a command that never runs
                                                         * sets no status bit at all -- no done, no error,
                                                         * the bytes still sitting in the fifo. v6.6's
                                                         * geni_se_io_init() opens all seven of them, and
                                                         * this driver had never written the register. */
#define SE_STATUS               (se + 0x040)            /* bit 0: a command is running */
#define SE_M_CLK_CFG            (se + 0x048)            /* bit 0: the clock runs, [15:4] the divider */
#define SE_CLK_CTRL_RO          (se + 0x060)            /* the engine's side of the same clock control: the
                                                         * vendor's bus_enable writes the core and engine enables
                                                         * to it on the way up, and mainline never touches it */
#define SE_IF_DISABLE           (se + 0x064)            /* read-only status: bit 0 says the FIFO interface is not up
                                                         * right now; the force-default write in step 6 resets the
                                                         * engine, and this driver reads it again there */
#define SE_FW_REVISION          (se + 0x068)            /* [15:8] the protocol: 3 is I2C */
#define SE_CLK_SEL              (se + 0x07c)            /* 0: use the clock the GCC gives this engine */
#define SE_BYTE_GRAN            (se + 0x254)            /* 0: four bytes to a fifo word, eight bits each */
#define SE_DMA_MODE_EN          (se + 0x258)            /* bit 0: DMA instead of the FIFOs */
#define SE_TX_PACKING_CFG0      (se + 0x260)            /* how the bytes of a fifo word are ordered: the */
#define SE_TX_PACKING_CFG1      (se + 0x264)            /*   four registers geni_se_config_packing() writes */
#define SE_I2C_TX_TRANS_LEN     (se + 0x26c)
#define SE_I2C_RX_TRANS_LEN     (se + 0x270)
#define SE_I2C_SCL_COUNTERS     (se + 0x278)            /* [29:20] high, [19:10] low, [9:0] cycle, in source clock periods */
#define SE_RX_PACKING_CFG0      (se + 0x284)
#define SE_RX_PACKING_CFG1      (se + 0x288)
#define SE_M_CMD0               (se + 0x600)            /* [31:27] the operation, [26:0] its parameters */
#define SE_M_CMD_CTRL           (se + 0x604)            /* bit 2: cancel, bit 1: abort, bit 0: off */
#define SE_M_IRQ_STATUS         (se + 0x610)
#define SE_M_IRQ_EN             (se + 0x614)
#define SE_M_IRQ_CLEAR          (se + 0x618)
#define SE_S_IRQ_CLEAR          (se + 0x648)
#define SE_TX_FIFO              (se + 0x700)
#define SE_RX_FIFO              (se + 0x780)
#define SE_TX_FIFO_STATUS       (se + 0x800)            /* [27:0] words in it */
#define SE_RX_FIFO_STATUS       (se + 0x804)            /* bit 31: the last word, [30:28] its valid bytes, [24:0] words */
#define SE_TX_WATERMARK         (se + 0x80c)
#define SE_RX_WATERMARK         (se + 0x810)
#define SE_RX_RFR_WATERMARK     (se + 0x814)
#define SE_GENI_IOS             (se + 0x908)            /* the two wires as the engine itself sees them:
                                                          * bit 0 sda, bit 1 scl, both names from the
                                                          * vendor's own header. The only view that works
                                                          * while the engine, not the tlmm, owns the pads:
                                                          * the 22:52 run read the levels through the
                                                          * tlmm, which cannot see what the engine drives
                                                          * or stretches while a command runs. */
#define SE_DMA_TX_IRQ_CLR       (se + 0xc44)
#define SE_DMA_RX_IRQ_CLR       (se + 0xd44)
#define SE_GSI_EVENT_EN         (se + 0xe18)
#define SE_IRQ_EN               (se + 0xe1c)            /* the wrapper's side: which of this engine's four
                                                         * interrupt sources are let through at all. Not the
                                                         * same register as the engine's own enables below,
                                                         * and v6.6's geni_se_io_set_mode() opens all four */
#define SE_DMA_GENERAL_CFG      (se + 0xe30)            /* [3:0]: the same clock gates again, on the dma and
                                                         * ahb side of the engine */
#define SE_HW_PARAM_0           (se + 0xe24)            /* [21:16] the transmit FIFO's depth in words */
#define SE_HW_PARAM_1           (se + 0xe28)            /* [21:16] the receive FIFO's depth in words */
#define SE_DFS_ENABLE           (se + 0xe38)            /* bit 0: the engine picks its own rate from the GCC's presets */

#define PROTO_I2C               3

#define M_CMD_DONE              (1u << 0)
#define M_CMD_OVERRUN           (1u << 1)
#define M_ILLEGAL_CMD           (1u << 2)
#define M_CMD_FAILURE           (1u << 3)
#define M_CMD_CANCEL            (1u << 4)
#define M_CMD_ABORT             (1u << 5)
#define M_NACK                  (1u << 10)              /* the chip did not answer its address */
#define M_BUS_PROTO             (1u << 12)
#define M_ARB_LOST              (1u << 13)
#define M_RX_WATERMARK          (1u << 26)
#define M_RX_LAST               (1u << 27)
#define M_TX_WATERMARK          (1u << 30)
#define M_ERRORS                (M_CMD_OVERRUN | M_ILLEGAL_CMD | M_CMD_FAILURE | M_NACK | M_BUS_PROTO | M_ARB_LOST)
#define M_COMMON_EN             (0x7eu | (1u << 22) | (1u << 23) | (1u << 24) | (1u << 25) | (1u << 28) | (1u << 29))
/* What geni_se_select_fifo_mode() of v6.6 adds on top of the common bits: bit 0, the done bit this
 * driver waits for, and the three fifo ones that come with it. The 21:55 run read M_IRQ_EN back as
 * 0x33c0007e, which is the common set and nothing else: bit 0 was never in it. */
#define M_FIFO_EN               (M_CMD_DONE | M_RX_WATERMARK | M_RX_LAST | M_TX_WATERMARK)

#define OP_WRITE                (1u << 27)
#define OP_READ                 (2u << 27)
#define OP_BUS_CLEAR            (6u << 27)
#define STOP_STRETCH            (1u << 2)               /* hold the bus: the next command starts again without a stop */
#define SLAVE_ADDRESS           0x20                    /* the dtbo's synaptics19081@20 */
#define CMD_ADDRESS             (uint32_t)(SLAVE_ADDRESS << 9 & 0xfe00u)

/* 400 kHz from the 19.2 MHz crystal: SCL = source / (t_cycle * divider), the vendor's second row */
#define CLK_DIVIDER             2u
#define SCL_COUNTERS            (5u << 20 | 12u << 10 | 24u)

/* The bring-up values v6.6's geni_se_io_init() and geni_se_config_packing() write. The clock gates are
 * the difference between an engine that ignores a command and one that runs it. The two packing words
 * are four bytes to a fifo word with the first byte in the low eight, which is the order the fifo reads
 * and writes below already use: writing them is what turns that order into a decision rather than an
 * assumption about what the phone's boot firmware left behind. */
#define DEFAULT_CGC_EN          0x7fu                   /* every gate in SE_CGC_CTRL */
#define DMA_CGC_ON              0xfu                    /* the four gates in the low bits of SE_DMA_GENERAL_CFG */
#define IRQ_ROUTING_ON          0xfu                    /* m, s, dma tx, dma rx: all four, in SE_IRQ_EN */
#define PACKING_CFG0            0x0007f8feu
#define PACKING_CFG1            0x00ffc2feu

#define TRANSFER_TIMEOUT_MS     200
#define TRANSFER_POLL_US        100                     /* how often bus_wait() asks; the count of polls
                                                          * is also the measure of how long a command
                                                          * took, which a plain deadline was not */
#define FIFO_MAX_WORDS          64

static uint32_t rx_depth;
static uint32_t transfer_polls = TRANSFER_TIMEOUT_MS * 1000u / TRANSFER_POLL_US;
static uint32_t last_wait_polls;                        /* polls the last command took: the 23:51 run's
                                                          * bus clear finished, but nothing said whether
                                                          * that took one poll or nineteen hundred, and
                                                          * those are different faults -- one is the
                                                          * clock the configuration says, the other is a
                                                          * clock a thousand times slower */

/* What a status means in words a person can act on. */
static const char *
bus_problem(uint32_t status)
{
	if (status & M_NACK) {
		return "nothing answered the address";
	}
	if (status & M_BUS_PROTO) {
		return "a bus protocol error";
	}
	if (status & M_ARB_LOST) {
		return "another master took the bus";
	}
	if (status & M_CMD_OVERRUN) {
		return "the command overran";
	}
	if (status & M_ILLEGAL_CMD) {
		return "the engine says the command is illegal";
	}
	if (status & M_CMD_FAILURE) {
		return "the command failed";
	}
	if (status & M_CMD_ABORT) {
		return "the command was aborted";
	}
	if (status & M_CMD_CANCEL) {
		return "the command was cancelled";
	}
	return "the command did not finish";
}

/* Stop whatever is running, as geni_se_abort_m_cmd() and the goodix driver's recovery do. */
static void
bus_abort(void)
{
	wr(SE_M_CMD_CTRL, 1u << 2);
	for (uint32_t i = 0; i < 200; i++) {
		if (rd(SE_M_IRQ_STATUS) & M_CMD_CANCEL) {
			break;
		}
		sleep_us(100);
	}
	if (!(rd(SE_M_IRQ_STATUS) & M_CMD_CANCEL)) {
		wr(SE_M_CMD_CTRL, 1u << 1);
		for (uint32_t i = 0; i < 200; i++) {
			if (rd(SE_M_IRQ_STATUS) & M_CMD_ABORT) {
				break;
			}
			sleep_us(100);
		}
	}
	wr(SE_M_CMD_CTRL, 0);
	for (uint32_t i = 0; i < FIFO_MAX_WORDS && (rd(SE_RX_FIFO_STATUS) & 0x01ffffff); i++) {
		(void)rd(SE_RX_FIFO);
	}
	wr(SE_M_IRQ_CLEAR, 0xffffffff);
}

/* Take the words the chip sent into `rx`, counting them in *received; without rx, only count them. */
static void
bus_drain(uint8_t *rx, uint32_t length, uint32_t *received)
{
	uint32_t status = rd(SE_RX_FIFO_STATUS), words = status & 0x01ffffff, bytes;

	if (words > FIFO_MAX_WORDS) {
		words = FIFO_MAX_WORDS;
	}
	bytes = words * 4;
	if (status & 0x80000000u) {
		uint32_t valid = status >> 28 & 7;

		if (valid && valid < 4) {
			bytes -= 4 - valid;
		}
	}
	for (uint32_t i = 0; i < words; i++) {
		uint32_t word = rd(SE_RX_FIFO);

		for (int j = 0; j < 4 && bytes; j++, bytes--) {
			if (*received < length) {
				if (rx != NULL) {
					rx[*received] = (uint8_t)(word >> (8 * j));
				}
				(*received)++;
			}
		}
	}
}

/* Wait for the engine to finish what it was told to do, counting the polls it took. A plain deadline
 * could only ever say "it did not finish"; the count can tell "finished at once" from "only just made
 * it", which is the difference between a bus at the 266 kHz this engine is configured for and one
 * crawling at a few hundred hertz -- and both of those look like a stuck read from the outside. */
static uint32_t
bus_wait(void)
{
	uint32_t status = 0;

	for (last_wait_polls = 0; last_wait_polls < transfer_polls; last_wait_polls++) {
		status = rd(SE_M_IRQ_STATUS);
		if (status & (M_CMD_DONE | M_ERRORS | M_CMD_ABORT | M_CMD_CANCEL)) {
			break;
		}
		sleep_us(TRANSFER_POLL_US);
	}
	return status;
}

/* Whether the wrapper did anything at all while a command was running. The engine's own registers say a
 * command started; the only way to learn whether the wrapper's side ever moved is to have looked at it
 * beforehand. The 23:27 run died reading the wrapper's whole window, so this watches only the two state
 * slots this engine could be -- 1, the first of this wrapper's eight, or 17, its number in the whole
 * soc -- both of which step 2 has already read without harm. Whichever of the two moves while a command
 * runs is the answer to which one this engine is. */
static uint32_t state_before[2];
static bool qup_watched;

static void
qup_watch_arm(void)
{
	state_before[0] = rd(QUP_SE_STATE(1));
	state_before[1] = rd(QUP_SE_STATE(17));
	qup_watched = true;
}

static void
qup_watch_report(const char *when)
{
	static const uint32_t slots[] = { 1, 17 };
	bool moved = false;

	for (unsigned i = 0; i < sizeof(slots) / sizeof(slots[0]); i++) {
		uint32_t now = rd(QUP_SE_STATE(slots[i]));

		if (now == state_before[i]) {
			continue;
		}
		say("touch:   %s: the wrapper's state for engine %u moved %#x -> %#x\n", when, slots[i],
		    state_before[i], now);
		moved = true;
	}
	if (!moved) {
		say("touch:   %s: the wrapper's state for this engine never moved: it never saw the command "
		    "start\n", when);
	}
}

/* What the engine says about itself right now, in one line. The 21:55 and 22:15 runs answered "the
 * command did not finish" and had nothing else to show, which is how a command that never started and a
 * chip that never answered looked the same from here. */
static void
bus_dump(const char *when)
{
	uint32_t ios;

	say("touch:   %s: irq status %#x, engine status %#x, clock gates %#x, irq enable %#x, routing %#x, "
	    "transmit fifo %u words, receive fifo %u words\n", when, rd(SE_M_IRQ_STATUS), rd(SE_STATUS),
	    rd(SE_CGC_CTRL), rd(SE_M_IRQ_EN), rd(SE_IRQ_EN), rd(SE_TX_FIFO_STATUS) & 0x0fffffff,
	    rd(SE_RX_FIFO_STATUS) & 0x01ffffff);
	/* The second line is the clock path and the wrapper's view of this engine. The 22:52 run proved the
	 * lines are high and the command is running, so what is left to tell apart is whether the engine's
	 * clock is really ticking and whether the wrapper knows this engine exists at all. Both state slots
	 * are read because which index this engine is -- 1, the first of this wrapper's eight, or 17, its
	 * number in the whole soc, which is what the tree's label qupv3_se17_i2c says -- is exactly what
	 * the watch underneath is there to answer. */
	say("touch:   %s: master clock %#x, scl counters %#x, clock control as settled %#x, outputs %#x, "
	    "wrapper state for engine 1 %#x, for engine 17 %#x\n", when, rd(SE_M_CLK_CFG),
	    rd(SE_I2C_SCL_COUNTERS), rd(SE_CLK_CTRL_RO), rd(SE_OUTPUT_CTRL), rd(QUP_SE_STATE(1)),
	    rd(QUP_SE_STATE(17)));
	/* The third line is the transaction as the engine itself defines it: the command it is running,
	 * the two lengths it was given, and the two wires as it sees them. A length that does not
	 * match what was written is the whole bug by itself -- the engine would be waiting for bytes
	 * nobody promised it, and would never say so. Wires high and idle while the command runs mean
	 * it is waiting on something inside itself; an sda it sees low means the chip is holding the
	 * bus, which is a chip-sized fault with a chip-sized fix. The poll count says how fast this
	 * engine really clocks: nine clocks at the configured rate are forty microseconds, less than
	 * one poll, and a clear that took hundreds is the clock, not the chip. */
	ios = rd(SE_GENI_IOS);
	say("touch:   %s: command %#x, %u bytes to send, %u to receive, the engine sees sda %u scl %u "
	    "(raw %#x), this wait used %u of its %u polls\n", when, rd(SE_M_CMD0),
	    rd(SE_I2C_TX_TRANS_LEN), rd(SE_I2C_RX_TRANS_LEN), ios & 1u, ios >> 1 & 1u, ios,
	    last_wait_polls, transfer_polls);
	if (qup_watched) {
		qup_watch_report(when);
	}
}

/*
 * Write `tx` to the chip and then read `read` bytes back, with the chip selected the whole time: the
 * write ends with the clock stretched and the read starts again without a stop, which is how a register
 * address is handed to a Synaptics. Both directions in one command (the engine's WRITE_READ operation)
 * is what a newer vendor driver does; this one does what v6.6's i2c-qcom-geni.c does, two commands.
 *
 * Which of the two failed is part of the answer now: the address write failing is the chip not taking
 * its address, the read failing is the chip taking it and then not answering, and those are different
 * faults with different fixes.
 */
static const char *
chip_read(const uint8_t *tx, uint32_t written, uint8_t *rx, uint32_t read)
{
	static char why[128];
	uint32_t sent = 0, got = 0, status;

	if (rd(SE_STATUS) & 1u) {
		bus_abort();
	}
	wr(SE_M_IRQ_CLEAR, 0xffffffff);
	wr(SE_I2C_TX_TRANS_LEN, written);
	/* remember what the wrapper says about this engine before the command goes in: if the wrapper
	 * never moves while the engine insists it is clocking, that is a different answer, with a
	 * different fix, than the wrapper moving and the chip still not answering */
	qup_watch_arm();
	wr(SE_M_CMD0, OP_WRITE | STOP_STRETCH | CMD_ADDRESS);
	if (written) {
		/* v6.6's geni_i2c_tx_one_msg() asks for one word in the fifo before it fills it, and
		 * stops asking when the bytes are all in: the engine pulls from the fifo as it clocks, and
		 * the watermark is how it says it wants more. Left at zero the engine never asks. */
		wr(SE_TX_WATERMARK, 1);
		/* the words go to the FIFO while the engine clocks them out; two bytes is one word, and the
		 * engine takes them as soon as it has the address, so this cannot block */
		while (sent < written) {
			uint32_t word = 0;

			for (int j = 0; j < 4 && sent < written; j++, sent++) {
				word |= (uint32_t)tx[sent] << (8 * j);
			}
			wr(SE_TX_FIFO, word);
		}
		for (uint32_t i = 0; i < 200; i++) {
			if (!(rd(SE_TX_FIFO_STATUS) & 0x0fffffff)) {
				break;
			}
			sleep_us(100);
		}
		wr(SE_TX_WATERMARK, 0);
	}
	status = bus_wait();
	if (!(status & M_CMD_DONE)) {
		bus_dump("writing the address");
		bus_abort();
		snprintf(why, sizeof(why), "writing the address: %s", bus_problem(status));
		return why;
	}
	wr(SE_M_IRQ_CLEAR, status);

	wr(SE_I2C_RX_TRANS_LEN, read);
	wr(SE_M_CMD0, OP_READ | CMD_ADDRESS);
	status = bus_wait();
	bus_drain(rx, read, &got);
	if (!(status & M_CMD_DONE)) {
		bus_dump("reading the bytes back");
		bus_abort();
		snprintf(why, sizeof(why), "reading %u bytes back: %s", read, bus_problem(status));
		return why;
	}
	wr(SE_M_IRQ_CLEAR, status);
	if (got != read) {
		return "the chip sent fewer bytes than asked for";
	}
	return NULL;
}

/* Step 3: what this engine is. */
static bool
engine_look(void)
{
	uint32_t proto = rd(SE_FW_REVISION) >> 8 & 0xff, hw0 = rd(SE_HW_PARAM_0), hw1 = rd(SE_HW_PARAM_1);
	uint32_t tx_depth = hw0 >> 16 & 0x3f, rx_words = hw1 >> 16 & 0x3f;

	say("touch: se17: firmware revision %#x (protocol %u%s), status %#x, fifo interface %s\n",
	    rd(SE_FW_REVISION), proto, proto == PROTO_I2C ? ", I2C" : "", rd(SE_STATUS),
	    rd(SE_IF_DISABLE) & 1 ? "DISABLED" : "there");
	if (proto != PROTO_I2C) {
		say("touch: the engine is not an I2C master: nothing to do\n");
		return false;
	}
	/* 0x64 is read-only status -- GENI_IF_DISABLE_RO upstream -- and this engine said its FIFO was
	 * disabled while the phone's own kernel drives this very bus through it: the 4.19
	 * i2c-qcom-geni.c, the shape the vendor's i2c-msm-geni.c descends from, never even reads the
	 * register, and v6.6 reads it only to choose between FIFO and GPI DMA, never to stop. So it is
	 * not a gate: step 6's write of the force-default register resets the engine and reads this
	 * again, the FIFO depths below and the transaction in step 7 are the real tests. */
	if (tx_depth == 0 || rx_words == 0) {
		/* QUP 3.2 and later report the depth in the eight bits above these */
		tx_depth = hw0 >> 16 & 0xff;
		rx_words = hw1 >> 16 & 0xff;
	}
	say("touch: se17: hardware parameters %#x %#x: transmit fifo %u words, receive fifo %u words; dma mode %#x, "
	    "dfs %#x, clk sel %#x, m clock %#x\n", hw0, hw1, tx_depth, rx_words, rd(SE_DMA_MODE_EN), rd(SE_DFS_ENABLE),
	    rd(SE_CLK_SEL), rd(SE_M_CLK_CFG));
	if (rd(SE_DFS_ENABLE) & 1) {
		say("touch: the engine is set to choose its own rate: turning that off in step 6\n");
	}
	if (tx_depth < 2 || rx_words < 2) {
		say("touch: the engine reports no usable FIFO: nothing to do\n");
		return false;
	}
	rx_depth = rx_words;
	return true;
}

/* Step 6: the engine as an I2C master at 400 kHz, in FIFO mode, polled. geni_se_init(),
 * geni_se_select_fifo_mode() and qcom_geni_i2c_conf() of v6.6, in that order -- and this time every
 * register those three write, which the previous version of this function did not.
 *
 * The two runs that reached step 7 both stopped with an empty status register: no done bit, no error
 * bit, the two bytes of the address still sitting in the transmit fifo. A chip refusing its address
 * answers M_NACK and a bus held low answers something else; an empty status register is the engine not
 * running the command at all. The reason is the one register this function never wrote: v6.6's
 * geni_se_io_init() opens all seven clock gates of SE_CGC_CTRL and the four of SE_DMA_GENERAL_CFG
 * before anything else, and an engine with those closed has no clock to clock a command out with. The
 * done bit of M_IRQ_EN is the other half of the same story -- geni_se_select_fifo_mode() turns it on,
 * and the 21:55 run read 0x33c0007e back, which is the common set with no bit 0 in it. */
static void
engine_configure(void)
{
	uint32_t en = rd(SE_M_IRQ_EN);
	uint32_t routing = rd(SE_IRQ_EN);

	/* geni_se_irq_clear(): the four clear registers, and the event enable that goes with them */
	wr(SE_GSI_EVENT_EN, 0);
	wr(SE_M_IRQ_CLEAR, 0xffffffff);
	wr(SE_S_IRQ_CLEAR, 0xffffffff);
	wr(SE_DMA_TX_IRQ_CLR, 0xffffffff);
	wr(SE_DMA_RX_IRQ_CLR, 0xffffffff);
	/* geni_se_io_init(): the clock gates first, because nothing below works without them, then the
	 * pins handed to the engine, then the reset to defaults. v6.6 writes them in that order, which is
	 * its way of saying the reset leaves the gates alone. */
	wr(SE_CGC_CTRL, rd(SE_CGC_CTRL) | DEFAULT_CGC_EN);
	wr(SE_DMA_GENERAL_CFG, rd(SE_DMA_GENERAL_CFG) | DMA_CGC_ON);
	wr(SE_OUTPUT_CTRL, 0x7f);
	wr(SE_FORCE_DEFAULT, 1);
	/* This hardware has no obligation to agree with v6.6 about what that reset reaches, so ask it, and
	 * put the gates back if it turned out they were closed by the reset after all. */
	if ((rd(SE_CGC_CTRL) & DEFAULT_CGC_EN) != DEFAULT_CGC_EN) {
		say("touch:   the reset to defaults closed the engine's own clock gates: opening them again\n");
		wr(SE_CGC_CTRL, rd(SE_CGC_CTRL) | DEFAULT_CGC_EN);
	}
	if ((rd(SE_DMA_GENERAL_CFG) & DMA_CGC_ON) != DMA_CGC_ON) {
		say("touch:   the reset to defaults closed the ahb side as well: opening those too\n");
		wr(SE_DMA_GENERAL_CFG, rd(SE_DMA_GENERAL_CFG) | DMA_CGC_ON);
	}
	/* Two questions the 22:25 run leaves open, and both are about whether the engine's outputs reach
	 * the pads at all -- which is the one thing that would explain a command that starts, takes its
	 * bytes out of the fifo, and then waits forever with nothing to report.
	 *
	 * FORCE_DEFAULT is the last write geni_se_io_init() makes, and that function works on real phones,
	 * so the bit must clear itself. If it did not, the outputs would be held at GENI_OUTPUT_CTRL and
	 * no engine could ever clock a line. Ask the hardware, and let go if it turns out the bit stays.
	 *
	 * The engine also says its own fifo interface is disabled. Upstream calls that bit FIFO_IF_DISABLE,
	 * names the register read only, and never writes it; the vendor's driver writes a zero there when
	 * it powers an engine up. One write says which of the two this hardware agrees with -- and if the
	 * vendor is right, that write is also the fix. */
	if (rd(SE_FORCE_DEFAULT) & 1u) {
		say("touch:   the reset to defaults left the outputs pinned: letting go of them\n");
		wr(SE_FORCE_DEFAULT, 0);
	}
	if (rd(SE_IF_DISABLE) & 1u) {
		uint32_t still;

		wr(SE_IF_DISABLE, 0);
		still = rd(SE_IF_DISABLE) & 1u;
		say("touch:   asked for the fifo interface: it now reads %u, which means %s\n", still,
		    still ? "it is a status readout and whatever switches the interface on is above us in "
			    "the wrapper"
			  : "the interface was switched off, and that was the fault");
	}
	/* geni_se_io_set_mode(): the four interrupt sources let through to the wrapper, and the fifos
	 * rather than dma. Both halves matter: this register is the wrapper's gate, the enables below are
	 * the engine's own events, and a done bit has to pass both to be readable from here. */
	wr(SE_IRQ_EN, routing | IRQ_ROUTING_ON);
	wr(SE_DMA_MODE_EN, rd(SE_DMA_MODE_EN) & ~1u);
	wr(SE_GSI_EVENT_EN, 0);
	/* geni_se_init()'s watermarks: stop taking bytes three words before the receive fifo is full, so
	 * that an overrun cannot be what loses the last byte of a reply, and pass them on two early. */
	wr(SE_RX_WATERMARK, rx_depth - 3);
	wr(SE_RX_RFR_WATERMARK, rx_depth - 2);
	wr(SE_TX_WATERMARK, 0);
	/* geni_se_init() and geni_se_select_fifo_mode() together: the error and fifo bits, and with them
	 * bit 0, which is the done bit bus_wait() waits for and which no run so far has had enabled. */
	wr(SE_M_IRQ_EN, en | M_COMMON_EN | M_FIFO_EN);
	/* geni_se_config_packing(8, 4, msb first) and the byte granularity that belongs with it: four
	 * bytes to a fifo word, the first byte in the low eight, which is the order the fifos below are
	 * already read and written in. */
	wr(SE_TX_PACKING_CFG0, PACKING_CFG0);
	wr(SE_TX_PACKING_CFG1, PACKING_CFG1);
	wr(SE_RX_PACKING_CFG0, PACKING_CFG0);
	wr(SE_RX_PACKING_CFG1, PACKING_CFG1);
	wr(SE_BYTE_GRAN, 0);
	wr(SE_CLK_SEL, 0);
	wr(SE_M_CLK_CFG, CLK_DIVIDER << 4 | 1u);
	wr(SE_I2C_SCL_COUNTERS, SCL_COUNTERS);
	say("touch: se17 configured: I2C, fifo mode, m clock divider %u against %u Hz, SCL counters high %u low %u "
	    "cycle %u (%u kHz), rx watermark %u, the clock gates open\n", CLK_DIVIDER, XO_HZ, 5, 12, 24,
	    XO_HZ / (24 * CLK_DIVIDER) / 1000, rx_depth - 3);
	say("touch: se17 now: clk sel %#x, m clock %#x, scl %#x, dma %#x, clock gates %#x, ahb gates %#x, irq "
	    "enable %#x, routing %#x, packing %#x %#x, status %#x, outputs pinned %u, fifo interface %s\n",
	    rd(SE_CLK_SEL), rd(SE_M_CLK_CFG), rd(SE_I2C_SCL_COUNTERS), rd(SE_DMA_MODE_EN), rd(SE_CGC_CTRL),
	    rd(SE_DMA_GENERAL_CFG), rd(SE_M_IRQ_EN), rd(SE_IRQ_EN), rd(SE_TX_PACKING_CFG0),
	    rd(SE_TX_PACKING_CFG1), rd(SE_STATUS), rd(SE_FORCE_DEFAULT) & 1u,
	    (rd(SE_IF_DISABLE) & 1) ? "still says disabled, which the transaction in step 7 will answer"
				   : "there again after the reset");
}

/* ---- the chip -------------------------------------------------------------------------------------- */

#define RMI_PRODUCT_ID          0x0000                  /* page zero: product id, date, info, firmware id */
#define TCM_PRODUCT_ID          0x0170                  /* the TCM bootloader's own id, which is what the vendor's driver reads first */
#define ID_LENGTH               10

static const char *
read_id(uint16_t address, uint8_t *id)
{
	uint8_t where[2] = { (uint8_t)(address >> 8), (uint8_t)address };

	return chip_read(where, 2, id, ID_LENGTH);
}

static void
say_id(const char *what, uint16_t address, const uint8_t *id)
{
	char hex[3 * ID_LENGTH + 1];

	for (uint32_t i = 0; i < ID_LENGTH; i++) {
		snprintf(&hex[3 * i], 4, "%02x ", id[i]);
	}
	hex[3 * ID_LENGTH] = 0;
	/* the product id is two big-endian bytes, then the build date as BCD, then the info, then the
	 * firmware id: the same reading hid-rmi.c's product id string is */
	say("touch: %s at %#x: %s%s\n", what, address, hex,
	    id[0] == 0x37 && id[1] == 0x06 ? "  <-- an S3706, which is what the dtbo says this is" : "");
}

/* The chip out of reset: the vendor's driver's own sequence, low for ten milliseconds, then away, then
 * the pause its firmware needs before it will answer its address. */
static void
reset_chip(void)
{
	wr(GPIO_IO(PIN_RESET), 0);
	wr(GPIO_CFG(PIN_RESET), (rd(GPIO_CFG(PIN_RESET)) & ~0x3ffu) | pin_bits(FUNCTION_GPIO, 0, DRIVE_4MA, true));
	sleep_ms(10);
	wr(GPIO_IO(PIN_RESET), 2);
	say("touch: reset released; the chip's firmware needs a moment\n");
	sleep_ms(250);
}

/* Are the two wires of the bus alive? This is the question the 22:25 run cannot answer and everything
 * else is settled: the engine has its clocks, its pins, its fifo and its command, and it ran the command
 * -- took both bytes of the address out of the fifo -- and then waited forever with nothing to report. No
 * done, no nack, no failure. An I2C master that waits forever without saying anything is a master whose
 * clock line never comes back up, and there are two ways that happens: nothing is pulling the bus up,
 * because on this board the pull-ups sit on the chip's own supply and the chip is unpowered; or something
 * is holding a line down, which is what a chip does while it boots and what a dead chip does too.
 *
 * The engine cannot tell those apart, so the pins are borrowed from it for a few milliseconds and read as
 * plain inputs. What the pads show with nothing driving them is the bus's own state. The internal pull-up
 * on the second read tells us which of the two it is: a line that reads high only with the pull-up on is
 * a line nothing pulls up from outside, and a line that reads low even then is being held down.
 *
 * The bit positions are upstream's for this SoC, from pinctrl-sm8150.c: a pin's control register is
 * 0x1000 * pin inside the south tile, with the function at [5:2], the pull at [1:0] -- 0 for none, 1 for
 * down, 2 for keeper, 3 for up -- and the output enable at bit 9; the register at +4 reads the pad itself
 * at bit 0. Function 0 is a plain gpio, which is what a pin has to be to be read like this.
 */
static void
bus_levels(const char *when)
{
	static const struct {
		const char *what;
		unsigned pin;
	} wires[] = { { "clock", PIN_SCL }, { "data", PIN_SDA } };

	say("touch:   %s: the two wires, read as plain inputs\n", when);
	for (unsigned i = 0; i < 2; i++) {
		unsigned pin = wires[i].pin;
		uint32_t was = rd(GPIO_CFG(pin));
		uint32_t idle, pulled;

		wr(GPIO_CFG(pin), (was & ~0x3ffu) | pin_bits(FUNCTION_GPIO, 0, DRIVE_4MA, false));
		sleep_ms(2);
		idle = rd(GPIO_IO(pin)) & 1u;
		wr(GPIO_CFG(pin), (was & ~0x3ffu) | pin_bits(FUNCTION_GPIO, 3, DRIVE_4MA, false));
		sleep_ms(2);
		pulled = rd(GPIO_IO(pin)) & 1u;
		wr(GPIO_CFG(pin), was);
		say("touch:     the %s line, gpio%u: nothing driving it reads %s, with the internal pull-up %s:"
		    " %s\n", wires[i].what, pin, idle ? "HIGH" : "low", pulled ? "HIGH" : "low",
		    idle ? "a pull-up is there and powered, so the bus is idle and the fault is on our side "
			    "of the pads"
			 : pulled ? "nothing pulls it up from outside: the rail behind the board's pull-ups "
				    "is off, or the chip holds the line down"
				  : "something holds it down hard: an unpowered chip loading the line, or a "
				    "short");
	}
}

/* ---- the same bus, moved by hand (the pads as plain GPIOs) ---------------------------------------- */

/* The engine has every clock, gate and reset the vendor's driver asks for and still does not finish a
 * command on this phone -- the 15:05 run: the address write never completed, and asking the wrapper for
 * the engine's sequencer changed 0 into 0. The pads are below all of that, and the fuel gauge driver
 * (hakios.bq27541.c) reads its chip through pads driven exactly this way. So the chip's ids are asked
 * once more with the two wires moved by hand: each wire open drain -- driven low as a TLMM output with
 * its data bit 0, released as an input with the pull-up -- at the twelve-microsecond half period that
 * driver uses, about 40 kHz. The pins' registers are the tile tlmm_probe found, at 0x1000 * pin; the
 * pins are on qup17 when this runs, so their original cfg words are kept and put back at the end. */

#define BB_HALF_US              12                      /* the fuel gauge driver's figure: ~40 kHz */

static uint32_t bb_scl_saved, bb_sda_saved;

/* Which pin is the clock and which is the data is not in the tree: its pinctrl state names the pair
 * "gpio55\gpio56" with one function, qup17, and lets the engine's hardware tell them apart. Moved by
 * hand that choice is ours, so both orders are tried. The half period is a variable for the same
 * reason: if a chip only answers a slower clock, that is worth knowing too. */
static unsigned bb_clk = PIN_SCL, bb_dat = PIN_SDA;
static uint32_t bb_half_us = BB_HALF_US;

/* How long the clock was stretched since the last reset of the counters. A chip that answers the
 * address and then holds the clock down is a chip that wants time -- the TCM bootloader says so with
 * its clock, not with its data -- and reading a bit before the clock has really come up reads zero. */
static uint32_t bb_stretch_us;

static void
bb_stretch_reset(void)
{
	bb_stretch_us = 0;
}

static void
bb_release(unsigned pin)
{
	wr(GPIO_CFG(pin), pin_bits(FUNCTION_GPIO, 3, DRIVE_4MA, false));
}

static void
bb_low(unsigned pin)
{
	wr(GPIO_CFG(pin), pin_bits(FUNCTION_GPIO, 0, DRIVE_4MA, true));
	wr(GPIO_IO(pin), 0);
}

static bool
bb_level(unsigned pin)
{
	return (rd(GPIO_IO(pin)) & 1u) != 0;
}

/* The half-period has to be a time, not a scheduler tick. sleep_us() goes through
 * delay_for_interval(), which waits for the next timer interrupt -- on this kernel that is
 * about 7.5 ms -- so a five-byte report read is dragged out to tens of milliseconds. The
 * one-processor boot answered contacts that way (logs/204724-run.log); with the seven
 * processors the same read answers 00 00 00 00 00 on every bit-2 event while the status
 * read at 0x06 keeps answering (logs/210105-run.log). Spin the half-period out against the
 * timebase instead: twelve microseconds is a handful of instructions and the bus is then
 * fast enough for the chip's report window even while other processors run. */
static void
bb_spin_us(uint32_t us)
{
	uint64_t deadline;

	clock_interval_to_deadline(us, NSEC_PER_USEC, &deadline);
	while (mach_absolute_time() < deadline) {
		/* spin: a scheduler tick is not a microsecond */
	}
}

static void
bb_half(void)
{
	bb_spin_us(bb_half_us);
}

static void
bb_scl(bool high)
{
	if (high) {
		bb_release(bb_clk);
		/* wait out a chip that stretches the clock: the line must really come up before the
		 * bit means anything. Fifty milliseconds, because the TCM bootloader takes tens of
		 * them to answer a read command, and count what we waited. */
		for (unsigned i = 0; i < 5000 && !bb_level(bb_clk); i++) {
			bb_spin_us(10);
			bb_stretch_us += 10;
		}
	} else {
		bb_low(bb_clk);
	}
	bb_half();
}

static void
bb_sda(bool high)
{
	if (high) {
		bb_release(bb_dat);
	} else {
		bb_low(bb_dat);
	}
}

static void
bb_start(void)
{
	bb_sda(true);
	bb_scl(true);
	bb_sda(false);
	bb_half();
	bb_scl(false);
}

static void
bb_stop(void)
{
	bb_sda(false);
	bb_scl(true);
	bb_sda(true);
	bb_half();
}

/* Eight bits, most significant first, and whether the chip pulled the next clock's data line down --
 * an acknowledge. */
static bool
bb_write(uint8_t byte)
{
	for (int bit = 7; bit >= 0; bit--) {
		bb_sda((byte >> bit) & 1);
		bb_half();
		bb_scl(true);
		bb_scl(false);
	}
	bb_sda(true);
	bb_half();
	bb_scl(true);
	bool ack = !bb_level(bb_dat);
	bb_scl(false);
	return ack;
}

static uint8_t
bb_read(bool acknowledge)
{
	uint8_t byte = 0;

	bb_sda(true);
	for (int bit = 0; bit < 8; bit++) {
		bb_scl(true);
		byte = (uint8_t)(byte << 1) | (bb_level(bb_dat) ? 1u : 0u);
		bb_scl(false);
	}
	bb_sda(!acknowledge);                     /* the master's answer: pulled low asks for more */
	bb_half();
	bb_scl(true);
	bb_scl(false);
	bb_sda(true);
	bb_half();
	return byte;
}

/* Select the chip, hand it `written` bytes, and take `read` back, repeated start in between: the same
 * shape as chip_read(), moved by hand. Which byte lost the answer is part of what it says. */
static const char *
bb_transaction_locked(uint8_t address, const uint8_t *bytes, uint32_t written, uint8_t *rx, uint32_t read)
{
	static char why[64];

	bb_start();
	if (!bb_write((uint8_t)(address << 1))) {
		bb_stop();
		snprintf(why, sizeof(why), "nothing answered the write address %#x", address);
		return why;
	}
	for (uint32_t i = 0; i < written; i++) {
		if (!bb_write(bytes[i])) {
			bb_stop();
			snprintf(why, sizeof(why), "the chip stopped answering after byte %u of the address",
			    i);
			return why;
		}
	}
	if (read == 0) {
		bb_stop();
		return NULL;
	}
	bb_start();
	if (!bb_write((uint8_t)(address << 1 | 1))) {
		bb_stop();
		snprintf(why, sizeof(why), "nothing answered the read address %#x", address);
		return why;
	}
	for (uint32_t i = 0; i < read; i++) {
		rx[i] = bb_read(i + 1 < read);
	}
	bb_stop();
	return NULL;
}

/*
 * Every byte on these wires is moved by this thread, a half-period at a time; a thread switched out
 * between two moves is a byte the chip never hears. That is what the phone showed: with the seven
 * processors the shipping string hands over, the chip's interrupt bit kept firing while every report
 * object read back zero (logs/204137-run.log), and the same driver with one processor read 72
 * contacts (logs/204724-run.log). Preemption is off for the whole transaction, which also makes
 * sleep_us() spin instead of waiting a scheduler tick -- the half-period wants a spin anyway.
 */
static const char *
bb_transaction(uint8_t address, const uint8_t *bytes, uint32_t written, uint8_t *rx, uint32_t read)
{
	const char *problem;

	disable_preemption();
	problem = bb_transaction_locked(address, bytes, written, rx, read);
	enable_preemption();
	return problem;
}

/* Free the bus before asking anything. The engine's "clear" never clocked a thing -- its sequencer
 * never started -- and the first order of the pads, with the roles the other way round, put garbage
 * on a chip that was listening: a chip left mid-transfer holds the data line down and answers nothing
 * until it has seen nine clocks and a stop. That is the i2c core's bus recovery, moved by hand. */
static void
bb_bus_release(void)
{
	bb_release(bb_dat);
	for (unsigned clock = 0; clock < 9; clock++) {
		bb_scl(true);
		bb_scl(false);
		if (bb_level(bb_dat)) {
			break;
		}
	}
	bb_stop();
	say("touch:   nine clocks and a stop: the data line %s\n",
	    bb_level(bb_dat) ? "is free" : "is still held down");
}

/* Whether each pad's output driver really reaches the line: drive it low and read it back. The pull
 * side was proved at 14:42, when both read high with the pull-ups on; a pin that reads high while it
 * is driven low is a pin whose output is not moving, whatever its registers say, and then no chip on
 * the bus could ever see an address. */
static void
bb_selftest(void)
{
	unsigned pins[] = { PIN_SCL, PIN_SDA };

	for (unsigned i = 0; i < 2; i++) {
		unsigned pin = pins[i];
		bool high_while_low;

		bb_low(pin);
		bb_half();
		high_while_low = bb_level(pin);
		bb_release(pin);
		bb_half();
		say("touch:   gpio%u driven low reads %s, released reads %s: %s\n", pin,
		    high_while_low ? "high" : "low", bb_level(pin) ? "high" : "low",
		    high_while_low ? "the output does not reach the line"
		           : "the output and the pull-up both move the line");
	}
}

/* Print a run of bytes of any length up to 32 and say whether any of them is not zero. A chip that
 * answers every address and returns only zeros is on the bus with nothing to say, and that is not an
 * answer: the 16:48 run set answered on all-zero data, and because of that the slower rounds never
 * ran. */
static bool
say_bytes(const char *what, const uint8_t *bytes, uint32_t count)
{
	char hex[3 * 32 + 1];
	bool any = false, printable = true;

	if (count > 32) {
		count = 32;
	}
	for (uint32_t i = 0; i < count; i++) {
		snprintf(&hex[3 * i], 4, "%02x ", bytes[i]);
		if (bytes[i] != 0) {
			any = true;
		}
		if (bytes[i] < 0x20 || bytes[i] > 0x7e) {
			printable = false;
		}
	}
	hex[3 * count] = 0;
	say("touch:   %s: %s%s\n", what, hex, any ? "" : " (all zero)");
	if (printable) {
		/* an RMI4 product id is eight printable characters, "S3706Axx" for this chip */
		say("touch:   which spells: \"%.*s\"\n", (int)count, bytes);
	}
	return any;
}

/* The F12 2d touch's query and data bases and the F01 rmi data base, remembered from the 0xdd and
 * 0xe3 reads so the finger watch below can use them. The 21:42 run made the query base necessary:
 * its objects reported y 3072-3327 against a display that ends at 2399, and the only registers
 * that say whether those are sensor coordinates are the maximum x and y in the F12 query region. */
static uint8_t bb_f12_data, bb_f12_query, bb_f01_data, bb_f01_control;

/* The flash-mode question the vendor's own driver asks at probe. synaptics_get_chip_info() (synaptics_-
 * drivers_s3706.c:176) first selects page zero by writing 0x00 to 0xff, and only then reads four
 * bytes at 0xdd, 0xe3 and 0xe9, which are the query, command, control and data bases of F12, F01 and
 * F34. The 16:48 run showed the chip answering every address and every register with zeros and never
 * stretching the clock once, and this is the read the vendor makes that we have never made -- and
 * the vendor never reads anything at all without first writing the page select, so those zeros may be
 * no more than a page pointer sitting somewhere else.
 *
 * The 17:33 run proved the whole thing: the chip answered with its real bases, the F01 query region
 * spelled "S3706A" -- this chip's name -- at the query base plus 0x0b, and the F01 control base plus
 * one read 0x7f, which is exactly what the vendor's own enable_interrupts (synaptics_drivers_s3706.c:
 * 410) tests for when the touch firmware's interrupts are live. The F01 status registers and the F12
 * data registers are read here too, because those are the registers the vendor's driver runs on and
 * their values are the proof the bases are the running ones.
 *
 * Returns 2 when a byte that is not zero came back, 1 when the chip took the addresses but stayed
 * silent, 0 when it did not even take the page select. */
static int
bb_chip_info(void)
{
	static const struct {
		uint8_t reg;
		const char *what;
	} bases[] = {
		{ 0xDD, "F12 2d touch's query, command, control and data bases, read at 0xdd" },
		{ 0xE3, "F01 rmi's query, command, control and data bases, read at 0xe3" },
		{ 0xE9, "F34 flash's query, command, control and data bases, read at 0xe9" },
	};
	static const uint8_t page0[2] = { 0xFF, 0x00 };
	uint8_t bases_read[3][4] = { { 0 }, { 0 }, { 0 } }, f01[4] = { 0, 0, 0, 0 };
	bool base_ok[3] = { false, false, false }, f01_read = false, any = false;
	const char *problem;
	uint8_t buf[32], reg;

	bb_stretch_reset();
	problem = bb_transaction(SLAVE_ADDRESS, page0, sizeof(page0), NULL, 0);
	if (problem != NULL) {
		say("touch:   the page select, writing 0x00 to 0xff: %s\n", problem);
		return 0;
	}
	say("touch:   the page select: the chip took it, the page pointer is on page zero\n");

	for (unsigned i = 0; i < sizeof(bases) / sizeof(bases[0]); i++) {
		bb_stretch_reset();
		problem = bb_transaction(SLAVE_ADDRESS, &bases[i].reg, 1, buf, 4);
		if (problem != NULL) {
			say("touch:   reading %s: %s\n", bases[i].what, problem);
			continue;
		}
		if (say_bytes(bases[i].what, buf, 4)) {
			any = true;
		}
		for (unsigned j = 0; j < 4; j++) {
			bases_read[i][j] = buf[j];
		}
		base_ok[i] = true;
		if (i == 1) {
			f01[0] = buf[0];
			f01[1] = buf[1];
			f01[2] = buf[2];
			f01[3] = buf[3];
			f01_read = true;
			bb_f01_data = buf[3];
		}
	}
	if (!f01_read) {
		return any ? 2 : 1;
	}
	if (f01[0] == 0 && f01[1] != 0) {
		f01[0] = (uint8_t)(f01[1] - 0x0F);
		say("touch:   the F01 query base read zero but its command base did not: the query "
		    "base is 0x%02x, the command base less the spec's 0x0f\n", f01[0]);
	}
	if (f01[3] & 0x40) {
		say("touch:   bit 6 of the F01 data base is set: the vendor's own test says this chip "
		    "is in bootloader mode and its flash is waiting for a firmware\n");
	}
	/* The whole F01 query region in one transaction. The 17:11 run read "06A" at the query base
	 * plus 0x0e -- the tail of an "S3706A" that must start a few bytes earlier -- and its two
	 * reads disagreed about the byte at query plus 0x12, which says single reads can lose their
	 * first bytes. One long read cannot hide that: every byte lands in one order, and the name,
	 * the revision and the firmware id all show where they really are. */
	reg = f01[0];
	bb_stretch_reset();
	problem = bb_transaction(SLAVE_ADDRESS, &reg, 1, buf, 32);
	if (problem != NULL) {
		say("touch:   reading the F01 query region at 0x%02x: %s\n", reg, problem);
	} else {
		char what[96];

		snprintf(what, sizeof(what),
		    "the F01 query region, thirty-two bytes from 0x%02x: the product name is at "
		    "plus 0x0b and the revision follows it", reg);
		if (say_bytes(what, buf, 32)) {
			any = true;
		}
	}
	/* The registers the vendor's driver really runs on. It takes the control base from the 0xe3
	 * read and writes device control there (synaptics_sleep_mode), reads device status and the
	 * interrupt status one and two past the data base (F01_RMI_DATA01), and takes the touch count
	 * three past the F12 data base (F12_2D_DATA15). Reading those addresses is the proof that the
	 * bases are the running ones and not page-zero noise. */
	if (base_ok[1]) {
		const struct {
			uint8_t *base;
			const char *what;
		} runtime[] = {
			{ &f01[2], "the F01 control registers, the base the vendor's driver writes "
			    "for sleep and doze" },
			{ &f01[3], "the F01 status registers: device status, then the interrupt "
			    "status the vendor reads before every touch read" },
		};

		for (unsigned i = 0; i < sizeof(runtime) / sizeof(runtime[0]); i++) {
			reg = *runtime[i].base;
			bb_stretch_reset();
			problem = bb_transaction(SLAVE_ADDRESS, &reg, 1, buf, 4);
			if (problem != NULL) {
				say("touch:   reading %s at 0x%02x: %s\n", runtime[i].what, reg,
				    problem);
				continue;
			}
			if (say_bytes(runtime[i].what, buf, 4)) {
				any = true;
			}
		}
		if (f01[2] != 0) {
			bb_f01_control = f01[2];
		}
	}
	if (base_ok[0] && bases_read[0][3] != 0) {
		bb_f12_data = bases_read[0][3];
		bb_f12_query = bases_read[0][0];
		reg = bases_read[0][3];
		bb_stretch_reset();
		problem = bb_transaction(SLAVE_ADDRESS, &reg, 1, buf, 4);
		if (problem != NULL) {
			say("touch:   reading the F12 data registers at 0x%02x: %s\n", reg, problem);
		} else {
			/* the third and fourth bytes are the object attention word the vendor's
			 * synaptics_get_touch_points reads: one bit per object that is down */
			if (say_bytes("the F12 data registers: the third and fourth are the "
			    "object attention word", buf, 4)) {
				any = true;
			}
		}
	}
	/* The panel resolution the vendor's own synaptics_fw_check reads at the F12 control base
	 * (synaptics_drivers_s3706.c:360): four bytes, max x little-endian then max y. If those are
	 * the panel's numbers -- this phone's glass is 1080 wide by 2340 or so -- then the F12 bases
	 * are the running ones and the finger watch below is watching the right registers; if they
	 * are zero or nonsense, the 0xdd read lied and the watch would prove nothing. */
	if (base_ok[0] && bases_read[0][2] != 0) {
		reg = bases_read[0][2];
		bb_stretch_reset();
		problem = bb_transaction(SLAVE_ADDRESS, &reg, 1, buf, 4);
		if (problem != NULL) {
			say("touch:   reading the F12 resolution at 0x%02x: %s\n", reg, problem);
		} else {
			say("touch:   the F12 resolution, as the vendor's fw_check reads it at 0x%02x: "
			    "max x %u, max y %u\n", reg, (unsigned)buf[0] | ((unsigned)buf[1] << 8),
			    (unsigned)buf[2] | ((unsigned)buf[3] << 8));
			any = true;
		}
	}
	say("touch:   the whole chip-info read stretched the clock for %u ms\n",
	    bb_stretch_us / 1000);
	return any ? 2 : 1;
}

/* How far apart two positions are, without the subtraction wrapping around zero. */
static unsigned
apart(unsigned a, unsigned b)
{
	return a > b ? a - b : b - a;
}

/* The chip is answering and its firmware is running -- the F01 control base read device control 0x00,
 * no hold-on, and its interrupt mask 0x7f, exactly what the vendor's enable_interrupts tests for
 * (synaptics_drivers_s3706.c:410), and the resolution the vendor's own fw_check reads came back as
 * this panel's real 1080x2400. So the one question left is the one the phone cares about: does a
 * finger on the glass show up, and in which registers?
 *
 * The map is no longer unknown, which is why this watch reads only the addresses the vendor uses.
 * Its get_chip_info (synaptics_drivers_s3706.c:176) reads four bytes at 0xdd, 0xe3 and 0xe9 and
 * uses them as the query, command, control and data bases directly, without the five-byte descriptor
 * walk the RMI4 spec describes -- and four-byte reads at function bases are exactly the shape this
 * bus answers correctly. They put the 2d touch function's data at 0x08 and its control at 0x13, and
 * the resolution read at 0x13 came back as this panel's real 1080x2400, so 0x08 is where the
 * objects live. The vendor's get_touch_points (synaptics_drivers_s3706.c:65) then reads two bytes at
 * data plus three, 0x0b, the object attention word with one bit per object, and eight bytes per
 * object from the data base: status, x low, x high, y low, y high, strength, touch major, width. The
 * 20:32 run is why nothing else is read: block reads that start anywhere other than a function base
 * gave values that contradict the same chip's own direct reads at 0x06, 0x0d and 0x13, so a byte
 * seen at a made-up address means nothing here.
 *
 * The 21:25 run settled what the eight-byte read means: every one of its fourteen bit-2 events read
 * the data base as eight zero bytes while the very next reads, the same bytes taken in fours,
 * answered with values -- and the 20:16 run had already seen a sixty-four byte read miss every event
 * the short reads caught. This hand-moved bus does not honestly answer a read longer than four
 * bytes, so the watch takes none. The object's eight bytes come in two four-byte reads, the one
 * shape that has answered correctly everywhere it was tried, and the attention word comes as the
 * vendor takes it, two bytes at data plus three (its get_touch_points, synaptics_drivers_s3706.c:
 * 74) -- the 21:01 run's doubt about that read was drawn with no finger on the glass, where zero is
 * simply the right answer. The word read there is checked against the same two bytes out of the
 * four-byte reads, and a disagreement is printed rather than hidden.
 *
 * The watch also moved to when the finger actually is. The 21:13 and 21:25 runs both watched around
 * the kernel's fifteenth second while the person holding the phone was still waiting for a home
 * screen that arrives near the hundred and seventy-fifth second -- neither watch had a finger on the
 * glass, and both were read as if they had. The 22:01 run watched from the two hundredth second and
 * still caught nothing, because that round's screen came up at the two hundred sixty-third: the
 * screen's moment moves more between rounds than any fixed window can follow, so the watch now runs
 * on wall time from the forty-fifth second to the three hundred fortieth and asks for the press as
 * soon as the screen lights at all.
 *
 * The 21:42 run, the first with a finger on the glass, settled three things and asked one. Settled:
 * the x map is the vendor's and it works -- x tracked the finger across 61 to 957 of the 1079 the
 * control base gave; the four-byte reads are self-consistent (the fourth byte of the F01 status
 * read at 0x06, which is 0x09, always equalled the object's x low byte); and the direct two-byte
 * read at 0x0b answers something of its own -- it said 0x0001 with a finger and 0x0000 without,
 * where the four-byte reads say 0x3dcc-style values, so it tracks presence but is not the y byte.
 * Asked: y. It came out 3072 to 3327 in every pass, its high nibble always 0xc, because the byte
 * at 0x0c reads 0xcc and never moves -- with a finger, without one, in this run and the last. The
 * objects' y is either sensor coordinates for a sensor taller than the display, which the F12 query
 * region's maximum would prove and which needs only a scale, or the y high byte lives somewhere
 * else. The 22:01 run read that maximum and it answered x 807, y 3863 -- a y that leaves the
 * objects' 3072-3327 room below it and an x that leaves the observed 957 outside it, so one of
 * those numbers is not what it looks like and the raw bytes behind the parse are needed; that run
 * also caught zero touches, because its watch (two hundredth to two hundred thirty-second second)
 * ended before the home screen came up at the two hundred sixty-third. That round's watch ran to
 * the five-and-a-half-minute mark and got the first real corner data: five hundred fifty-seven
 * passes with a finger, x from 42 to 1065 with the byte at 0x0a carrying real high bits, so x is a
 * full sixteen-bit little-endian number at 0x09-0x0a scaled to the display's 1079 exactly as the
 * vendor parses it, and that question is closed. The y question is not: the byte at 0x0b -- y low
 * in the vendor's layout -- held between 50 and 113 through every press, left edge, right edge and
 * the last press at x 989, which is no coordinate across 2399 lines; the byte at 0x0c read 0xcc as
 * the first byte of its burst and 0x00 as the second byte of a word read at 0x0b, and the word
 * read at 0x0b itself answered 0x0001 or 0x0000, tracking the finger's presence exactly -- which
 * is the attention word, precisely what the vendor's read_word at data plus three asks for and
 * what it must be on working hardware. So the same addresses answer differently depending on where
 * the read starts: the data region behaves as a window, not a flat map, and which window is honest
 * is the whole question now. The 22:43 kernel tested the flat-map idea outright and the 22:45 run
 * answered it: the view started one byte into the object read 00 00 00 00 in every one of the 139
 * events while the view from the data base answered the object, so the region is a window and the
 * view at data plus one is dead and is not taken again. That round also lost its presses: the
 * screen lit near the three hundred twentieth second and the watch ended at the three hundred
 * fortieth, catching one brief left-edge press (x 41-69) and no corner holds and no drags -- the
 * screen-on moment has drifted to 175, 213, 263 and 320 seconds across rounds, so this watch runs
 * to the five hundred sixtieth second. What was open then was how far the honest window
 * reaches: the bus's one rule, that reads longer than four bytes lie, was proved only for an
 * eight-byte read, and the vendor's own get_touch_points asks for eight and works on the real
 * bus, so the length itself cannot be what breaks. The 22:59 kernel therefore took the proven
 * four-byte read again as five and six bytes, and the 23:01 run answered with its first real
 * presses: the five-byte read repeated the four-byte read's four bytes in 39 of the 43 distinct
 * events that round printed and differed by one count in four of the rest, the ordinary jitter
 * of a held finger between reads a sixth of a second apart, and it carried one byte past them
 * -- and that byte is the y high byte. The object is [count, x low, x high, y low, y high],
 * five bytes from the data base, and both coordinates are already the display's: the top-left
 * hold read 72, 65 and held there, the bottom-right hold read 1002, 2333 and held there, and a
 * drag down the middle of the glass swept y from 72 to 549 in step after monotonic step while
 * x stayed within a finger's width. The sixth byte is strength, 0x1f to 0x3b under a finger;
 * the view from 0x0d repeated the last three bytes of the view from 0x0c zero times in 998, so
 * every address's answer belongs to the window that was asked and nothing outside the vendor's
 * read shapes means anything. The map is closed. What is left is not a question but the
 * driver's job, and so this is no longer a watch: bb_touch_run polls in the order every working
 * round took, parses the object, scales it to the 0 to 65535 the /dev/hakios-touch record asks
 * for, and posts DOWN, MOVE and UP through hakios_touch_post the way
 * drivers/input/hakios.goodix.c posts its phone's fingers. The screen itself was the test, and
 * the 23:57 run answered it: every tap opened what it pointed at and backboardd printed
 * "contact 1 presence: touching" for the records -- the icons answer a finger. What that run
 * also showed is the pace: a whole drag answered two MOVEs, and a UI that gets six points a
 * second cannot be swiped back out of an app. The loop now follows the chip's interrupt line
 * (gpio122, active low): while the line is high it sleeps one scheduler tick, and the moment
 * the chip pulls it low it reads the status and takes the report, which is the vendor's
 * interrupt-driven rhythm with polling in place of the GIC handler. */
/* The F01 control block's first four registers are device control (0 = normal, 1 = sleep,
 * 2 = doze), the interrupt enable mask, the doze interval and the doze timer. This port has
 * never written them. The chip's own values (00 7f 08 14 in the 21:08 trace) let it doze after
 * a while, and a dozing chip stops reporting until it sees the wake gesture its firmware
 * expects -- which a finger on this port's bus is not. Normal operation and no doze timer is
 * written once at the start and again on every five-second heartbeat, so the tap after a quiet
 * minute is still taken. */
static const char *
f01_stay_awake(void)
{
	uint8_t reg = bb_f01_control, control[4], tx[5];

	if (reg == 0) {
		return "no control base";
	}
	if (bb_transaction(SLAVE_ADDRESS, &reg, 1, control, 4) != NULL) {
		return "the control registers do not read";
	}
	control[0] = (uint8_t) (control[0] & ~0x03u);   /* normal operation, not sleep or doze */
	control[2] = 0;                                /* doze interval */
	control[3] = 0;                                /* doze timer */
	tx[0] = reg;
	for (unsigned i = 0; i < 4; i++) {
		tx[1 + i] = control[i];
	}
	return bb_transaction(SLAVE_ADDRESS, tx, sizeof(tx), NULL, 0);
}

static void
bb_touch_run(void)
{
	enum {
		STATUS_LEN = 4,
		ATTN_LEN = 2,
		OBJECT_LEN = 5,
		POLL_MS = 150,
		PANEL_MAX_X = 1079,
		PANEL_MAX_Y = 2399,
		MAX_EVENT_REPORTS = 80,
		STATUS_EDGES = 40,
		QUIET_AFTER = 100,
	};
	static const uint8_t page0[2] = { 0xFF, 0x00 };
	uint8_t f12 = bb_f12_data != 0 ? bb_f12_data : 0x08;
	uint8_t f01 = bb_f01_data != 0 ? bb_f01_data : 0x06;
	uint8_t status[STATUS_LEN], word[ATTN_LEN], object[OBJECT_LEN], reg;
	uint8_t last_status[STATUS_LEN];
	unsigned downs = 0, moves = 0, ups = 0, posted = 0;
	unsigned failed = 0, silent = 0, pass = 0, edges = 0;
	uint64_t heartbeat = 0;
	uint64_t release_after, last_report = 0;
	uint64_t last_summary = 0;
	uint64_t summary_every;
	clock_interval_to_absolutetime_interval(2000, NSEC_PER_MSEC, &release_after);
	clock_interval_to_absolutetime_interval(5000, NSEC_PER_MSEC, &summary_every);
	last_summary = mach_absolute_time();
	bool down = false;
	uint32_t last_x = 0, last_y = 0;

	/* The reads are the ones the 23:01 round proved honest, in the order that round took
	 * them: the page select, the F01 status that clears the chip's interrupt, the attention
	 * word two bytes before the objects, and the five-byte object from the data base, which
	 * is [count, x low, x high, y low, y high] with both coordinates already scaled to the
	 * display. The count is how many fingers are down; only the first object is asked for,
	 * because one finger is what tapping and swiping need and the window has only ever
	 * answered from the base. The pace is two-speed and the reason is written where the
	 * sleep is; the status bytes are watched edge by edge because the 23:01 watch's
	 * status-change lines are what proved the chip alive, and a loop that only speaks on
	 * bit 2 cannot tell a silent chip from a quiet one. The record's x and y are 0 to
	 * 65535 across the panel, so the display coordinates are scaled the way
	 * drivers/input/hakios.goodix.c scales its own phone's, and posted through
	 * hakios_touch_post for backboardd's side of /dev/hakios-touch. */
	say("touch:   the events start here: the chip's interrupt line at gpio%d is watched every "
	    "millisecond (one scheduler tick), the F01 status at 0x%02x is read whenever it asks "
	    "or the %d ms heartbeat comes due, and when its bit 2 is set the object at 0x%02x is "
	    "parsed and posted to /dev/hakios-touch for backboardd\n", PIN_IRQ, f01, POLL_MS, f12);
	if (bb_transaction(SLAVE_ADDRESS, page0, sizeof(page0), NULL, 0) != NULL) {
		say("touch:   the chip would not take the page select: no events can come\n");
		return;
	}
	reg = f01;
	if (bb_transaction(SLAVE_ADDRESS, &reg, 1, status, STATUS_LEN) == NULL) {
		say("touch:   the F01 status before anyone touched: %02x %02x %02x %02x\n",
		    status[0], status[1], status[2], status[3]);
		for (int i = 0; i < STATUS_LEN; i++) {
			last_status[i] = status[i];
		}
	} else {
		say("touch:   nothing answered the F01 status read: no events can come\n");
		return;
	}
	{
		const char *problem = f01_stay_awake();

		say("touch:   F01 control 0x%02x: %s\n", bb_f01_control,
		    problem != NULL ? problem : "normal operation, the doze timer is 0");
	}

	for (;;) {
		const char *problem = NULL;
		bool reported = false, present, attention_ok = false;
		const char *kind = NULL;
		uint32_t raw_x = 0, raw_y = 0, x = 0, y = 0, post_x = 0, post_y = 0;
		uint64_t now = mach_absolute_time();

		/* A line every five seconds whatever the chip is doing: "a few touches and then
		 * nothing" cannot be told from a silent chip, a wedged bus and a quiet loop by the
		 * capped event lines alone (logs/192945-run.log has no pass-1000 line after its
		 * contacts). */
		if (now - last_summary > summary_every) {
			last_summary = now;
			say("touch: %u passes: %u downs, %u moves, %u ups, %u reads failed, irq gpio%u %s\n",
			    pass, downs, moves, ups, failed, PIN_IRQ, bb_level(PIN_IRQ) ? "high" : "low");
			{
				const char *problem = f01_stay_awake();

				if (problem != NULL) {
					say("touch: the doze registers did not take: %s\n", problem);
				}
			}
		}

		/* The chip's interrupt line is gpio122: an input with its pull-up, high
		 * while the chip has nothing to say and low while it is asking. Watching
		 * the pin costs one register read, and the status register -- the read
		 * that clears the interrupt -- is only touched when the pin is low or the
		 * idle heartbeat comes due. That is the difference from the 23:28
		 * failure: there the status was read eighty times a second with no report
		 * taken, and the chip went silent; here every low pin is answered with its
		 * report. While the pin is high the loop sleeps a millisecond (one
		 * scheduler tick, about 7.5 ms), so a tap is taken within one tick instead
		 * of waiting up to the old 150 ms, and the glass's reports are followed at
		 * the chip's own rate while a finger is down. */
		if (bb_level(PIN_IRQ) && mach_absolute_time() < heartbeat) {
			/* A contact that stops reporting must not leave the UI held
			 * down: if the chip has said nothing for two seconds while a
			 * finger is down, lift it. A real long-press acts well inside
			 * that; this is the net for the stale-window case that left a
			 * DOWN with no UP in the 21:15 round. */
			if (down && last_report != 0 &&
			    mach_absolute_time() - last_report > release_after) {
				hakios_touch_post(HAKIOS_TOUCH_UP, 0, last_x, last_y);
				ups++;
				down = false;
				if (posted < MAX_EVENT_REPORTS) {
					posted++;
					say("touch:   the chip stopped reporting under a "
					    "finger: UP at %u, %u after two seconds of "
					    "silence\n", last_x, last_y);
				}
			}
			sleep_ms(1);
			continue;
		}
		clock_interval_to_deadline(POLL_MS, NSEC_PER_MSEC, &heartbeat);
		pass++;
		if (bb_transaction(SLAVE_ADDRESS, page0, sizeof(page0), NULL, 0) != NULL) {
			problem = "the page select";
		} else {
			/* Reading the F01 status is what clears the chip's interrupt, and its
			 * bit 2 is the 2d touch function asking for its data: no other read is
			 * worth making when that bit is clear, and nothing the finger state
			 * was is contradicted by skipping them. */
			reg = f01;
			if (bb_transaction(SLAVE_ADDRESS, &reg, 1, status, STATUS_LEN) != NULL) {
				problem = "the F01 status read";
			} else {
				bool changed = false;
				for (int i = 0; i < STATUS_LEN; i++) {
					if (status[i] != last_status[i]) {
						changed = true;
					}
					last_status[i] = status[i];
				}
				/* The 23:01 watch's status-change lines are what proved the chip
				 * alive and told bit 2 from its neighbours; a loop that only
				 * speaks on bit 2 cannot tell a silent chip from a quiet one, so
				 * the edges are printed, capped, and the interrupt byte and the
				 * event counts are said every thousand passes whatever the bus
				 * is doing. */
				if (changed && edges++ < STATUS_EDGES) {
					say("touch:   pass %u: the F01 status went to %02x %02x %02x "
					    "%02x%s\n", pass, status[0], status[1], status[2],
					    status[3], (status[1] & 0x04) != 0 ?
					    ", bit 2: the 2d touch function asking for "
					    "its data" : "");
				}
				if (pass % 1000 == 0) {
					say("touch:   pass %u: %u downs, %u moves and %u ups posted, "
					    "%u reads failed, the interrupt byte last said "
					    "%02x\n", pass, downs, moves, ups, failed,
					    status[1]);
				}
				if ((status[1] & 0x04) != 0) {
					/* The vendor's get_touch_points reads the attention
					 * word first, two bytes at data plus three, and only
					 * then the objects; the word is not needed for the
					 * parse, but the order is what every working round
					 * read honestly, so the order stays. */
					uint64_t started, nanos;
					uint8_t f01_query[4];

					reg = (uint8_t)(f12 + 3);
					attention_ok = bb_transaction(SLAVE_ADDRESS, &reg, 1, word,
					    ATTN_LEN) == NULL;
					reg = f12;
					started = mach_absolute_time();
					if (bb_transaction(SLAVE_ADDRESS, &reg, 1, object,
					    OBJECT_LEN) != NULL) {
						problem = "the five-byte object read";
					} else {
						static unsigned object_prints;

						absolutetime_to_nanoseconds(
						    mach_absolute_time() - started, &nanos);
						if (object_prints < 8) {
							object_prints++;
							say("touch:   the attention word at 0x%02x says "
							    "%02x %02x, the object at 0x%02x came back "
							    "%02x %02x %02x %02x %02x in %llu us\n",
							    f12 + 3, word[0], word[1], f12, object[0],
							    object[1], object[2], object[3], object[4],
							    nanos / 1000);
							/* A zero object can mean two things:
							 * the bus is lying again, or the chip's
							 * window really has nothing. A read of a
							 * register that always has a value tells
							 * them apart: 0x2a is the F01 query base,
							 * whose first byte is 0x01 and whose
							 * product name starts at plus eleven. */
							if (object[0] == 0 && object[1] == 0 &&
							    object[2] == 0 && object[3] == 0 &&
							    object[4] == 0) {
								reg = 0x2a;
								if (bb_transaction(SLAVE_ADDRESS, &reg,
								    1, f01_query, 4) == NULL) {
									say("touch:   after that zero object "
									    "the F01 query at 0x2a answered "
									    "%02x %02x %02x %02x\n",
									    f01_query[0], f01_query[1],
									    f01_query[2], f01_query[3]);
								}
							}
						}
						last_report = mach_absolute_time();
						reported = true;
					}
				}
			}
		}
		if (problem != NULL) {
			failed++;
			if (++silent == QUIET_AFTER) {
				say("touch:   a hundred passes answered nothing: the bus is "
				    "asked again, quietly and a tenth of a second at a "
				    "time\n");
			}
			if (silent >= QUIET_AFTER) {
				sleep_ms(88);
			}
			/* A picture drawn or a page of Settings paged in can take this thread off the
			 * pads between two bits, and the chip then sits mid-transfer holding the data
			 * line down: every later read fails until it has seen nine clocks and a stop.
			 * That is what a tap into Settings -> Wi-Fi looked like from outside -- a few
			 * touches, then nothing (logs/192945-run.log has no pass-1000 line after its
			 * contacts). The recovery is the same one the bring-up uses. */
			if (silent == 16 || silent == QUIET_AFTER) {
				bb_bus_release();
				if (bb_transaction(SLAVE_ADDRESS, page0, sizeof(page0), NULL, 0) == NULL) {
					say("touch:   the bus is free and page zero is selected again\n");
					silent = 0;
				}
			}
			continue;
		}
		silent = 0;
		if (!reported) {
			continue;
		}
		/* The 23:01 round settled what these five bytes are: the corner holds read
		 * 72, 65 and 1002, 2333 and held, and a drag down the glass swept y from 31
		 * to 2399, the panel's own range, so both coordinates are already the
		 * display's and only the record's 0 to 65535 scaling is left. */
		/* Presence is the attention word, not the object's first byte: the word at
		 * 0x0b answers 01 00 with a finger and 00 00 without (the 22:18/23:01
		 * rounds), while the object window can answer a stale non-zero byte after
		 * the lift -- which is how the 21:15 round left a DOWN with no UP (26
		 * downs, 25 ups) and the UI stuck under a finger that was no longer there.
		 * The vendor's get_touch_points reads this word first for exactly this
		 * reason. If the word itself did not answer, fall back to the old byte. */
		present = attention_ok ?
		    (((uint16_t) word[0] | ((uint16_t) word[1] << 8)) != 0 &&
		    object[0] != 0) :
		    object[0] != 0;
		raw_x = (uint32_t) object[1] | ((uint32_t) (object[2] & 0x0F) << 8);
		raw_y = (uint32_t) object[3] | ((uint32_t) (object[4] & 0x0F) << 8);
		if (raw_x > PANEL_MAX_X) {
			raw_x = PANEL_MAX_X;
		}
		if (raw_y > PANEL_MAX_Y) {
			raw_y = PANEL_MAX_Y;
		}
		x = raw_x * 65535 / PANEL_MAX_X;
		y = raw_y * 65535 / PANEL_MAX_Y;
		if (present) {
			if (!down) {
				kind = "DOWN";
				downs++;
				hakios_touch_post(HAKIOS_TOUCH_DOWN, 0, x, y);
			} else if (x != last_x || y != last_y) {
				kind = "MOVE";
				moves++;
				hakios_touch_post(HAKIOS_TOUCH_MOVE, 0, x, y);
			}
			last_x = x;
			last_y = y;
			down = true;
			post_x = x;
			post_y = y;
		} else if (down) {
			kind = "UP";
			ups++;
			post_x = last_x;
			post_y = last_y;
			hakios_touch_post(HAKIOS_TOUCH_UP, 0, last_x, last_y);
			down = false;
		}
		if (kind != NULL) {
			if (posted < MAX_EVENT_REPORTS) {
				posted++;
				if (kind[0] == 'U') {
					say("touch:   pass %u: UP at %u, %u of 65535, where the "
					    "finger lifted\n", pass, post_x, post_y);
				} else {
					say("touch:   pass %u: %s at %u, %u of 65535, the glass's "
					    "own %u, %u of %u x %u\n", pass, kind, post_x, post_y,
					    raw_x, raw_y, PANEL_MAX_X, PANEL_MAX_Y);
				}
			} else if (posted++ == MAX_EVENT_REPORTS) {
				say("touch:   that is enough printed: the events keep going to "
				    "/dev/hakios-touch, and the screen is the place to watch "
				    "them now\n");
			}
		}
	}
}

/* The chip, asked through the pads. The 16:48 run proved the chip takes its address, takes the
 * register byte, and answers zeros with no clock stretching at all -- so the first thing asked is the
 * vendor's own get_chip_info sequence, page select and all, and the TCM bootloader's four-byte read
 * command is kept for a chip whose flash is empty. An answer of all zeros does not end the search:
 * the round is accepted only when a byte is not zero, and the swapped pins and the ten-times-slower
 * clock really run now. The 16:01 run proved the pin order, so it stays first. */
static bool
bitbang_probe(void)
{
	static const struct {
		const char *how;
		unsigned clk, dat;
		uint32_t half_us;
	} tries[] = {
		{ "clock on gpio56, data on gpio55, the order the phone proved", PIN_SCL, PIN_SDA,
		    BB_HALF_US },
		{ "the two pins swapped", PIN_SDA, PIN_SCL, BB_HALF_US },
		{ "the proven order again, ten times slower", PIN_SCL, PIN_SDA, 100 },
	};
	static const uint8_t tcm[4] = { 0x00, 0x70, 0x01, 0x00 };
	uint8_t id[ID_LENGTH];
	const char *problem;
	bool answered = false, acked = false, rmi4 = false;

	say("touch:   the engine never finishes a command: the two wires are moved by hand, open drain, "
	    "about 40 kHz\n");
	bb_scl_saved = rd(GPIO_CFG(PIN_SCL));
	bb_sda_saved = rd(GPIO_CFG(PIN_SDA));
	bb_release(PIN_SCL);
	bb_release(PIN_SDA);
	sleep_ms(1);
	say("touch:   the two wires before anything is driven: clock %s, data %s\n",
	    bb_level(PIN_SCL) ? "high" : "low", bb_level(PIN_SDA) ? "high" : "low");
	bb_selftest();
	reset_chip();
	for (unsigned t = 0; t < sizeof(tries) / sizeof(tries[0]) && !answered; t++) {
		int info;

		bb_clk = tries[t].clk;
		bb_dat = tries[t].dat;
		bb_half_us = tries[t].half_us;
		bb_release(bb_clk);
		bb_release(bb_dat);
		sleep_ms(1);
		bb_bus_release();
		say("touch:   asking by hand, %s\n", tries[t].how);
		info = bb_chip_info();
		if (info == 2) {
			answered = true;
			rmi4 = true;
			break;
		}
		if (info == 1) {
			acked = true;
		}
		/* A chip whose flash is empty sits in the TCM bootloader and obeys nothing but its
		 * read command: opcode 0, the address 0x170 little-endian, no length byte. */
		bb_stretch_reset();
		problem = bb_transaction(SLAVE_ADDRESS, tcm, sizeof(tcm), id, ID_LENGTH);
		if (problem != NULL) {
			say("touch:   by hand, the TCM bootloader's read command for 0x170: %s\n",
			    problem);
			continue;
		}
		acked = true;
		say("touch:   the TCM command was taken after %u ms of clock stretching\n",
		    bb_stretch_us / 1000);
		if (say_bytes("the TCM bootloader's id, its four-byte read command at 0x170", id,
		    ID_LENGTH)) {
			answered = true;
		}
	}
	bb_half_us = BB_HALF_US;
	if (answered) {
		say("touch:   the chip answered by hand: the pads work, and it is the engine that "
		    "does not\n");
		if (rmi4) {
			/* The event loop drives the two wires for as long as the phone runs, so
			 * the pins keep the configuration the bit-bang gave them and are never
			 * put back. Every other way out puts them back; the loop never returns,
			 * and each of its edges writes the pin configuration again anyway. */
			bb_touch_run();
		}
	} else if (acked) {
		say("touch:   the chip takes its address and answers every read with zeros: it is on "
		    "the bus and silent, which is a chip whose flash is waiting for a firmware or one "
		    "that will not give up its page\n");
	} else {
		say("touch:   the wires moved and nothing answered: the chip does not see its address, "
		    "which leaves its supply and its pins, not the engine\n");
	}
	wr(GPIO_CFG(PIN_SCL), bb_scl_saved);
	wr(GPIO_CFG(PIN_SDA), bb_sda_saved);
	return answered;
}

/* Ask the wrapper to let this engine's sequencer run. Mainline never does this: its clock driver votes
 * the wrapper's core clocks on through the rpmh, and the boot firmware is expected to have set the engine
 * states up. Neither of those exists here -- the 22:52 run read 0 from the wrapper's state for this
 * engine -- so this is the vendor driver's step done by hand: qcom_geni_se_state_enable() writes
 * SE_TYPE_SEQ | SE_SEQ_EN to QUP_SE_STATE(se_idx) before anything touches an engine. Which index this
 * engine is, 1 or 17, is not settled from here: the tree labels 0xc80000 qupv3_se17_i2c, its number in
 * the whole soc, while it is the first of this wrapper's eight. Both are asked, and the watch says
 * which one the engine answers through. */
static void
wrapper_enable(void)
{
	static const uint32_t slots[] = { 1, 17 };

	for (unsigned i = 0; i < sizeof(slots) / sizeof(slots[0]); i++) {
		vm_offset_t where = QUP_SE_STATE(slots[i]);
		uint32_t state = rd(where);

		if ((state & (SE_STATE_SEQ_EN | SE_STATE_TYPE_SEQ)) == (SE_STATE_SEQ_EN | SE_STATE_TYPE_SEQ)) {
			say("touch:   the wrapper already says engine %u's sequencer is on: %#x\n", slots[i],
			    state);
			continue;
		}
		wr(where, state | SE_STATE_SEQ_EN | SE_STATE_TYPE_SEQ);
		say("touch:   asked the wrapper for engine %u's sequencer: %#x -> %#x\n", slots[i], state,
		    rd(where));
	}
}

/* Step 7: the chip out of reset, and what it says it is. Whether the chip answered decides whether
 * step 8 is worth making at all, so it says so in its return value. */
static bool
chip_probe(void)
{
	uint8_t id[ID_LENGTH];
	const char *problem;
	uint32_t status;

	reset_chip();
	say("touch: irq gpio%u is %s\n", PIN_IRQ, rd(GPIO_IO(PIN_IRQ)) & 1 ? "high (nothing waiting)" :
	    "LOW (the chip has something to say)");
	bus_levels("the chip is out of reset and has had a quarter of a second");

	problem = read_id(TCM_PRODUCT_ID, id);
	if (problem == NULL) {
		say_id("the TCM bootloader's id", TCM_PRODUCT_ID, id);
	} else {
		say("touch: reading the TCM id: %s\n", problem);
		/* The 22:52 run says the engine runs the command, the lines are high, and the chip still
		 * never takes its address, and the one layer above the engine that has never been asked
		 * for anything is the wrapper, whose state for this engine reads 0. So the second attempt
		 * is the same read after the wrapper has been told this engine exists. */
		wrapper_enable();
		problem = read_id(TCM_PRODUCT_ID, id);
		if (problem == NULL) {
			say_id("the TCM id, once the wrapper was asked for its sequencer", TCM_PRODUCT_ID, id);
		} else {
			say("touch: reading the TCM id again: %s\n", problem);
			/* The same read at another address fails the same way, so the third attempt is not
			 * another address but the bus and the chip given another chance: nine clocks and a
			 * stop, which is i2c-qcom-geni.c's own recovery for a line held low, and then the
			 * reset again in case the chip was only booting slowly. Whether the bus clear
			 * itself finishes is worth knowing now: a bus clear that never finishes is the
			 * wrapper still, one that finishes with the lines still low is the chip. */
			wr(SE_I2C_TX_TRANS_LEN, 0);
			wr(SE_M_CMD0, OP_BUS_CLEAR | CMD_ADDRESS);
			status = bus_wait();
			/* the poll count is the point of this line: nine clocks at the configured rate
			 * are forty microseconds, less than one poll, so a clear that finishes at once
			 * is the clock the configuration says and one that took hundreds is a clock a
			 * thousand times slower -- and in the 23:51 run both of those looked like
			 * "bus clear finished". */
			say("touch:   the bus clear %s, after %u of %u polls\n",
			    (status & M_CMD_DONE) ? "finished" :
			    (status & M_CMD_ABORT) ? "was aborted" : "did not finish either",
			    last_wait_polls, transfer_polls);
			/* if that clear was slow, the clock is not the one the configuration says, and
			 * the reads after it need the same longer wait or they will time out for the
			 * reason the clear just measured, not the reason we are asking about. */
			if (last_wait_polls > 100) {
				transfer_polls = 30000;
				say("touch:   that was slow: the waits after it will use up to %u "
				    "polls\n", transfer_polls);
			}
			wr(SE_M_IRQ_CLEAR, 0xffffffff);
			bus_levels("after the engine cleared the bus");
			reset_chip();
			problem = read_id(TCM_PRODUCT_ID, id);
			if (problem == NULL) {
				say_id("the TCM id, after a bus clear and a second reset", TCM_PRODUCT_ID, id);
			} else {
				say("touch: reading the TCM id a third time: %s\n", problem);
				/* The engine has been asked everything the vendor's driver asks and still
				 * does not finish a command. The pads are below the engine, and the fuel
				 * gauge driver reads its chip through pads moved exactly this way:
				 * the same ids, asked by hand. */
				if (bitbang_probe()) {
					return true;
				}
			}
		}
	}
	if (problem != NULL) {
		return false;
	}

	problem = read_id(RMI_PRODUCT_ID, id);
	if (problem == NULL) {
		say_id("the RMI4 page zero id", RMI_PRODUCT_ID, id);
	} else {
		say("touch: reading the RMI4 id: %s\n", problem);
	}
	return true;
}

/*
 * Step 8: the two votes neither clock driver claims.
 *
 * Both this soc's clock drivers -- the vendor's drivers/clk/qcom/gcc-sm8150.c and mainline v6.6's,
 * compared byte for byte on 2026-11-02 -- vote through 0x52014 with exactly eight bits: 1 and 2 for
 * the two ahb branches and 4 through 9 for the six serial engines. Bits 0 and 3 of the same word
 * belong to nothing either of them names, and mainline's wrapper core driver takes no clock at all:
 * its clock comes from the rpmh's bus clock manager, through the interconnect driver, which is the
 * one layer between this engine and a working bus that has never been reproduced here. The vendor's
 * wrapper node says the same thing from the other side: it names no clocks, only a bus master and
 * slave id.
 *
 * The 23:51 run weakened this: its bus clear finished, so the engine does drive the bus, and the
 * wrapper's io path is alive. But a clear is nine clocks with no slave, and a read is a command
 * that must tick, sample the wires and take an answer -- and the two state slots and the clock
 * control at +0x8 have read 0 through every run, which is what a block with no core clock looks
 * like from the outside. This is the last register-level thing that can be tried without an rpmh
 * driver, so: ask for the two bits, look at what was dead, and read the chip once more. If the
 * clock control at +0x8 comes alive, the wrapper core was the whole gap and a bus vote driver is
 * the next big item; if nothing moves, record that and stop guessing at registers.
 *
 * Whether the chip answered decides whether step 9 is worth making, so it says so in its return
 * value.
 */
static bool
unclaimed_vote(void)
{
	uint32_t vote = rd(GCC_VOTE);
	uint8_t id[ID_LENGTH];
	const char *problem;

	say("touch:   votes %#x -> %#x\n", vote,
	    vote | VOTE_QUP2_UNCLAIMED_0 | VOTE_QUP2_UNCLAIMED_3);
	wr(GCC_VOTE, vote | VOTE_QUP2_UNCLAIMED_0 | VOTE_QUP2_UNCLAIMED_3);
	pause_ms(10);
	/* the three things that have read dead in every run: the wrapper's clock control, which took
	 * no write in step 2, and the two state slots, which never moved during a command. */
	say("touch:   after the vote: votes read %#x, wrapper clock control %#x, wrapper state for "
	    "engine 1 %#x, for engine 17 %#x\n", rd(GCC_VOTE), rd(QUP_SE_CLK_CTL_ENABLE),
	    rd(QUP_SE_STATE(1)), rd(QUP_SE_STATE(17)));
	/* if a core clock only started now, the engine's configuration was written to a block that
	 * had no clock to keep it with; engine_configure() is what step 6 wrote and reads back, so
	 * write it again rather than trust the readback of a block that may have just come alive. */
	engine_configure();
	problem = read_id(TCM_PRODUCT_ID, id);
	if (problem == NULL) {
		say_id("the TCM id, once the wrapper core was voted for", TCM_PRODUCT_ID, id);
		return true;
	}
	say("touch: reading the TCM id after the vote: %s\n", problem);
	return false;
}

/* ---- the rpmh's bus clock manager: step 9's vote, sent by hand ------------------------------------- */

/*
 * The tree's mailbox@18220000 node is the apps_rsc controller: the resource controller the ap
 * hands its bus votes to. The vendor's driver for it (drivers/mailbox/qcom-rpmh-mailbox.c, the
 * "qcom,tcs-drv" compatible) maps two windows: the controller's own registers, of which its probe
 * reads one, and a window of one 0x2a0-wide block per tcs slot, which is where every command
 * goes. The tree's qcom,tcs-config lists the two active-only slots first, so those are blocks 0
 * and 1, and get_tcs_for_msg() sends active-only votes to exactly those.
 *
 * The vote itself: the tree's bcm-qup0 node, cell-id 0x1b80, is the bus clock manager cell that
 * the wrapper's master id (0x99) reaches, and the one the QUP core clock is derived from; there is
 * no second QUP cell in the whole tree. The vendor's fabric driver (msm_bus_fabric_rpmh.c) puts
 * one command per cell into a slot: address the cell, data commit bit 30, valid bit 29, and two
 * 14-bit votes, and sets cmd->complete with the commit. The i2c client's vote is I2C_CORE2X_VOTE,
 * 960, from the vendor's i2c driver; the fabric scales it on the way, and this cannot know the
 * scaled value, so the probe asks twice: once with 960 and once with the largest the cell takes.
 *
 * One thing step 9 got wrong, and step 10 checks: the address. The fabric driver never sends the
 * tree's cell-id: line 1205 is bcmdev->addr = cmd_db_get_addr(bcmdev->name), and the tcs
 * command's address is that value as-is (line 275). cmd_db_get_addr looks the resource id up in
 * the boot loader's command database, and the entry's address field is what it returns; the tree's
 * cell-id is only the fabric graph's node number, and the two do not agree -- the 012836 run read
 * the whole table and QUP0's entry says 0x00050080, not 0x1b80. The database itself is where the
 * tree's qcom,cmd-db@c3f000c node points: two words, the address of the table and its length
 * (cmd_db_dev_probe, lines 352-369), and the table's magic is 0x0c0330db at its offset 4.
 */

#define RSC_CFG_PHYS              0x018220000UL         /* the controller's own registers: the vendor's driver
                                                           * reads one of them at probe, so they answer */
#define RSC_CFG_SIZE              0x100
#define RSC_TCS_PHYS              0x018220d00UL         /* the window of tcs slot blocks */
#define RSC_TCS_SIZE              0x3000
#define RSC_CFG_SLOTS             0x0c                  /* DRV_PRNT_CHLD_CONFIG: [31:27] commands per slot,
                                                           * [19:6] slots per driver in six-bit fields, and
                                                           * this controller's driver id is 2 */
#define DRV_TCS_STRIDE            0x2a0UL               /* RSC_DRV_TCS_OFFSET in the vendor's driver: 672 */
#define DRV_CMD_STRIDE            0x14UL                /* RSC_DRV_CMD_OFFSET: 20 */
#define DRV_TCS(slot, reg, cmd)   (rsc_tcs + DRV_TCS_STRIDE * (slot) + (reg) + DRV_CMD_STRIDE * (cmd))
#define DRV_CONTROL               0x14                  /* bit 16 arms the slot, bit 24 triggers it */
#define DRV_STATUS                0x18                  /* non-zero: the controller has finished with the
                                                           * slot, which is the vendor's tcs_is_free();
                                                           * bit 0 on its own says nothing either way */
#define DRV_CMD_WAIT_FOR_CMPL     0x10                  /* which commands wait for the one before */
#define DRV_CMD_ENABLE            0x1c                  /* which of the slot's commands are live */
#define DRV_CMD_MSGID             0x30                  /* [7:0] length, bit 8 a response is wanted, bit 16 write */
#define DRV_CMD_ADDR              0x34
#define DRV_CMD_DATA              0x38
#define DRV_CMD_STATUS            0x3c                  /* bit 8 issued, bit 16 completed */
#define RSC_IRQ_ENABLE            0x00                  /* the controller's own registers share slot 0's
                                                           * first sixteen bytes: one bit per slot in each */
#define RSC_IRQ_STATUS            0x04
#define RSC_IRQ_CLEAR             0x08
#define AMC_MODE_ENABLE           (1u << 16)
#define AMC_MODE_TRIGGER          (1u << 24)
#define CMD_LEN                   8                     /* CMD_MSGID_LEN: the slot holds eight commands */
#define CMD_RESP_REQ              (1u << 8)
#define CMD_WRITE                 (1u << 16)
#define BCM_QUP0_CELL             0x1b80                /* the tree's bcm-qup0 cell-id: the fabric graph's node number, not what the vendor's driver sends (step 10) */
#define BCM_CMD_COMMIT            (1u << 30)
#define BCM_CMD_VALID             (1u << 29)
#define BCM_CMD_VOTE(v)           (((v) & 0x3fffu) << 14 | ((v) & 0x3fffu))
#define RSC_ACTIVE_SLOTS          2                     /* the two active-only slots, blocks 0 and 1 */
#define RSC_WAIT_POLLS            2000                  /* 200 ms at the transfer poll interval */

static vm_offset_t rsc_cfg, rsc_tcs;

/* The vendor's write_tcs_reg_sync(): the hardware wants the value to read back before the next
 * write, and its driver retries until it does. Bounded here: a word that will not settle must
 * say so, not hang the run. */
static bool
rsc_write_sync(vm_offset_t where, uint32_t want)
{
	for (unsigned tries = 0; tries < 1000; tries++) {
		wr(where, want);
		if (rd(where) == want) {
			return true;
		}
		sleep_us(1);
	}
	say("touch:   rpmh: %#lx would not take %#x: it reads %#x\n", (unsigned long)where, want,
	    rd(where));
	return false;
}

/* Hand one vote to a cell through a slot, in the order the vendor's tcs_mbox_write() and
 * __tcs_buffer_write() use it: the command words, then the slot's wait and enable bits, then the
 * trigger in the sequence its comment says the hardware requires -- trigger off, mode off, mode on,
 * trigger on. What the slot is asked afterwards is what the vendor's tcs_irq_handler asks: its
 * interrupt bit, and the command's own issued and completed bits. Not the slot's status word: the
 * first version of this waited for that word's bit 0 to clear, and the vendor's tcs_is_free()
 * wants the word non-zero, which is the other way round -- the slots step 9 had used were free,
 * and step 10 found them busy because of a test that meant the opposite. The commands are
 * disarmed and the interrupt acknowledged at the end, which is what the vendor's handler does to an
 * active slot and the only thing that makes a slot usable again. */
/* Map the controller once, for every vote that needs it: the bus clock votes (step 9) and the
 * touch's 2.8 V vote (step 5) both go through the same TCS slots. Mapping late was a real crash:
 * the 2.8 V vote ran at step 5 with the windows still unmapped, wrote through a zero base and took
 * a data abort at 0x18. */
static bool
rpmh_map(void)
{
	if (rsc_cfg != 0 && rsc_tcs != 0) {
		return true;
	}
	rsc_cfg = ml_io_map(RSC_CFG_PHYS, RSC_CFG_SIZE);
	rsc_tcs = ml_io_map(RSC_TCS_PHYS, RSC_TCS_SIZE);
	if (rsc_cfg == 0 || rsc_tcs == 0) {
		say("touch: could not map the rpmh controller: config %#lx, tcs %#lx\n",
		    (unsigned long)rsc_cfg, (unsigned long)rsc_tcs);
		return false;
	}
	return true;
}

static bool
tcs_send(uint32_t cell, uint32_t data)
{
	uint32_t status = 0, polls, irq = 0, state;
	unsigned slot;
	bool found = false;

	if (rsc_tcs == 0) {
		say("touch:   rpmh: the controller is not mapped: the command is not sent\n");
		return false;
	}

	for (slot = 0; slot < RSC_ACTIVE_SLOTS; slot++) {
		if (rd(DRV_TCS(slot, DRV_STATUS, 0)) != 0) {
			found = true;
			break;
		}
	}
	if (!found) {
		say("touch:   rpmh: no slot has ever completed a transaction: both read %#x\n",
		    rd(DRV_TCS(0, DRV_STATUS, 0)));
		return false;
	}
	state = rd(DRV_TCS(slot, DRV_STATUS, 0));
	/* the vendor's handler, arriving for this slot: acknowledge it, and disarm the commands
	 * it was still holding. done before the new command is written, because nothing else
	 * here reclaims a slot from a transaction that ended without an interrupt. */
	wr(rsc_tcs + RSC_IRQ_CLEAR, 1u << slot);
	wr(DRV_TCS(slot, DRV_CMD_ENABLE, 0), 0);
	wr(DRV_TCS(slot, DRV_CMD_WAIT_FOR_CMPL, 0), 0);
	/* write, eight long, response wanted: the fabric driver sets cmd->complete with the
	 * commit bit and the mailbox driver turns that into a response request and a wait. */
	wr(DRV_TCS(slot, DRV_CMD_MSGID, 0), CMD_LEN | CMD_RESP_REQ | CMD_WRITE);
	wr(DRV_TCS(slot, DRV_CMD_ADDR, 0), cell);
	wr(DRV_TCS(slot, DRV_CMD_DATA, 0), data);
	wr(DRV_TCS(slot, DRV_CMD_WAIT_FOR_CMPL, 0), 1);
	wr(DRV_TCS(slot, DRV_CMD_ENABLE, 0), 1);
	{
		uint32_t ctrl = rd(DRV_TCS(slot, DRV_CONTROL, 0));

		ctrl &= ~AMC_MODE_TRIGGER;
		rsc_write_sync(DRV_TCS(slot, DRV_CONTROL, 0), ctrl);
		ctrl &= ~AMC_MODE_ENABLE;
		rsc_write_sync(DRV_TCS(slot, DRV_CONTROL, 0), ctrl);
		rsc_write_sync(DRV_TCS(slot, DRV_CONTROL, 0), AMC_MODE_ENABLE);
		wr(DRV_TCS(slot, DRV_CONTROL, 0), AMC_MODE_ENABLE | AMC_MODE_TRIGGER);
	}
	for (polls = 0; polls < RSC_WAIT_POLLS; polls++) {
		status = rd(DRV_TCS(slot, DRV_CMD_STATUS, 0));
		if (status & (1u << 16)) {
			break;
		}
		if (rd(rsc_tcs + RSC_IRQ_STATUS) & (1u << slot)) {
			break;
		}
		sleep_us(TRANSFER_POLL_US);
	}
	irq = rd(rsc_tcs + RSC_IRQ_STATUS);
	status = rd(DRV_TCS(slot, DRV_CMD_STATUS, 0));
	say("touch:   rpmh: slot %u: cell %#x data %#x: %u of %u polls, slot status %#x, "
	    "interrupts %#x, command status %#x%s%s\n", slot, cell, data, polls, RSC_WAIT_POLLS,
	    rd(DRV_TCS(slot, DRV_STATUS, 0)), irq, status, status & (1u << 8) ? ", issued" : "",
	    status & (1u << 16) ? ", completed" : ", not completed");
	if (state != rd(DRV_TCS(slot, DRV_STATUS, 0))) {
		say("touch:   rpmh:   the slot's status changed while the command was out: it read "
		    "%#x before, %#x now\n", state, rd(DRV_TCS(slot, DRV_STATUS, 0)));
	}
	wr(rsc_tcs + RSC_IRQ_CLEAR, 1u << slot);
	wr(DRV_TCS(slot, DRV_CMD_ENABLE, 0), 0);
	wr(DRV_TCS(slot, DRV_CMD_WAIT_FOR_CMPL, 0), 0);
	return (status & (1u << 16)) != 0;
}

/* The bus clock manager's vote word: the vendor's bcm-voter packs the value with a commit and a
 * valid bit, and that word goes through the same slot machinery as any other command. */
static bool
bcm_send_vote(uint32_t cell, uint32_t vote)
{
	return tcs_send(cell, BCM_CMD_COMMIT | BCM_CMD_VALID | BCM_CMD_VOTE(vote));
}

/* ---- the boot loader's command database (step 10) -------------------------------------------------- */

/*
 * The tree's qcom,cmd-db@c3f000c node is eight bytes: the address of the command database and its
 * length (cmd_db_dev_probe, lines 352-369), and the table's magic is 0x0c0330db at its offset 4.
 * The 012836 run read the whole table and its layout is now known, and it is the one the phone's
 * own 4.19 kernel was written with -- not the newer struct in the vendor-cmd-db.c I have, whose
 * address field sits eight bytes earlier and which is why the first reading of it found 0. The
 * header's records are the vendor's struct rsc_hdr, sixteen bytes each: the first word's low half
 * is the slave id and the second word's high half is the count (the first reading had the count in
 * the wrong half and read 480), and from those the four resource types parse exactly: 20 + 10 + 41
 * + 68 entries, 24 bytes each, filling the table up to the data section the records point at. The
 * entry headers are 24 bytes: the name, then a word that is 0 for every bus clock manager and a
 * master id for the memory controllers, then a priority word, then the address, then the data
 * length and offset packed together -- the same struct the gpu driver reads this table with. The
 * address is what cmd_db_get_addr returns and what the fabric driver sends as-is, and its high
 * half is the resource type's slave id and its low half the cell's offset: QUP0's entry says
 * 0x00050080, slave 5 offset 0x80, where the tree's cell-id said 0x1b80. The cell-id is the
 * fabric graph's node number and never what the driver sent.
 */
#define CMD_DB_POINTER            0x00c3f000cUL         /* the tree's qcom,cmd-db@c3f000c: reg <0xc3f000c 0x08> */
#define CMD_DB_MAGIC              0x0c0330dbu
#define CMD_DB_HEADER_SIZE        148                   /* the vendor's sizeof(struct cmd_db_header): 8 + 8*16 + 8 */

static uint32_t
cmdb_look(const char *name)
{
	vm_offset_t pointer = ml_io_map(CMD_DB_POINTER, 0x1000);
	uint32_t want[2] = { 0, 0 };
	uint32_t found = 0, found_at = 0;

	/* the name is the resource id packed into eight bytes, the way cmd_db_get_u64_id packs it,
	 * and the compare below is word by word. it is not memcmp of the two reads: the four
	 * bytes that follow a uint32_t in memory are not the other uint32_t, whatever the
	 * optimiser decided to do with them, and that is why the first version of this printed
	 * the entry it was looking for and then said it could not find it. */
	for (unsigned c = 0; c < sizeof(want) && name[c] != 0; c++) {
		((uint8_t *) want)[c] = (uint8_t) name[c];
	}
	if (pointer == 0) {
		say("touch:   cmd db: could not map the pointer at %#x\n", CMD_DB_POINTER);
		return 0;
	}
	uint32_t base = rd(pointer), length = rd(pointer + 4);
	say("touch:   cmd db: the pointer at %#x reads %#x with %u bytes after it\n", CMD_DB_POINTER,
	    base, length);
	if (base < 0x1000 || base + length <= base || length == 0 || length > 0x200000) {
		say("touch:   cmd db: that is not a table I can read, stopped\n");
		return 0;
	}
	vm_offset_t db = ml_io_map_wcomb((vm_offset_t)base, (vm_size_t)length);
	if (db == 0) {
		say("touch:   cmd db: could not map the table at %#x\n", base);
		return 0;
	}
	uint32_t magic = rd(db + 4);
	say("touch:   cmd db: the table at %#x reads version %#x, magic %#x%s\n", base, rd(db), magic,
	    magic == CMD_DB_MAGIC ? " (the right one)" : " (not the cmd db magic, stopped)");
	if (magic != CMD_DB_MAGIC) {
		return 0;
	}
	/* the header's own words, four to a line: the record layout above is the vendor's struct
	 * and this is the first chance to see a real one. */
	for (unsigned line = 8; line < CMD_DB_HEADER_SIZE; line += 16) {
		say("touch:   cmd db:   %#04x: %08x %08x %08x %08x\n", line, rd(db + line),
		    rd(db + line + 4), rd(db + line + 8), rd(db + line + 12));
	}
	/* The names are the one thing every layout shares: cmd_db_get_u64_id packs the resource id
	 * into eight bytes and cmd_db_get_header compares that u64, so eight bytes of id-shaped
	 * text are at the front of every entry. Walking the table by names rather than by the
	 * records is what worked out the layout above, and it is the forgiving way to find one:
	 * the records have to be read exactly right, the name only has to be there. Every name
	 * was printed once and the layout confirmed with it; now only the entry that matches is,
	 * with the two beside it, because the table is long and the run is short. The address is
	 * the third word after the name, not the first -- the first is 0 for every bus clock
	 * manager, which is what the first reading of this mistook for a missing address. */
	for (unsigned at = CMD_DB_HEADER_SIZE; at + 8 <= length; at += 4) {
		uint32_t id0 = rd(db + at), id1 = rd(db + at + 4);

		if (id0 != want[0] || id1 != want[1]) {
			continue;
		}
		if (found == 0) {
			found = rd(db + at + 16);
			found_at = at;
		}
	}
	if (found == 0) {
		return 0;
	}
	/* the entry, and one before and one after: they are a stride of 24 apart, which is what
	 * says this is a real entry header and not text from the data section that happens to
	 * spell the name. the entry's own words are the address and the packed data length and
	 * offset, and the address is what the fabric driver sends. */
	for (int off = -24; off <= 24; off += 24) {
		uint32_t at = (uint32_t) ((int) found_at + off);
		uint32_t w0 = rd(db + at), w1 = rd(db + at + 4);
		char nm[9];

		if (off != 0 && (at < CMD_DB_HEADER_SIZE || at + 28 > length)) {
			continue;
		}
		for (unsigned c = 0; c < 4; c++) {
			nm[c] = (char) (((uint8_t *) &w0)[c]);
			nm[4 + c] = (char) (((uint8_t *) &w1)[c]);
		}
		nm[8] = 0;
		say("touch:   cmd db: %#06x: \"%s\" %08x %08x %08x %08x %08x %08x%s\n", at, nm,
		    rd(db + at + 8), rd(db + at + 12), rd(db + at + 16), rd(db + at + 20),
		    rd(db + at + 24), rd(db + at + 28), off == 0 ? "  <-- this is the entry" : "");
	}
	return found;
}

/*
 * The touch's 2.8 V. The tree gives the touch `vdd_2v8-supply = <&pm8150_l17>`, and the vendor's
 * touch driver enables that regulator at probe (regulator_set_voltage 3.0 V, then regulator_enable).
 * L17 is not an AP-writable SPMI regulator: it is an RPMh VRM resource, and the vendor's
 * rpmh-regulator.c writes three of its registers -- base+0 voltage in mV, base+4 enable, base+8 mode
 * -- through a TCS command. This asks the boot loader's command database for "ldoa17" and writes
 * those three registers through the same slot machinery the bus votes use. Nothing is written if the
 * database has no such resource.
 *
 * Two things the phone taught on 2026-11-02, which is why this is behind touch_2v8=1 and not done
 * every boot. The first is the sharing: the runtime tree's fragment@37 gives the very same
 * pm8150_l17 to `qcom,dsi-display-primary:lab-supply`, so this rail is the panel's analog supply
 * too, and it is on right now because the display is running from it -- the touch's 2.8 V is
 * already there without asking. The second is what asking does to the panel: the runs at 19:42 and
 * 19:52 kept the logo the boot had drawn and lost the UI after it, while the same boot without
 * this driver reaches the home screen (the 20:00 run, `logs/200024-run.log`). The request itself is
 * the vendor's own -- 3000 mV in millivolts is what rpmh_regulator_vrm_set_voltage() sends
 * (rpmh-regulator.c:967), and 7 is that driver's hardware value for a PMIC5 LDO's high-power mode
 * (rpmh-regulator.c:295), the mode the tree's index 0x04 maps to -- so what costs the panel its
 * light is not a wrong number in the command. It is that this rail has two owners and the display
 * is one of them: the vote is left to the display's side, and the touch takes the rail as it is.
 * When someone asks for it with touch_2v8=1 the mode sent is the tree's own init-mode, auto, the
 * gentlest of the two the resource supports.
 */
static void
touch_2v8_vote(void)
{
	uint32_t cell;
	bool ok;

	if (!rpmh_map()) {
		return;
	}
	cell = cmdb_look("ldoa17");
	if (cell == 0) {
		say("touch: the command database has no ldoa17: the 2.8 V stays as the boot left it\n");
		return;
	}
	ok = tcs_send(cell, 3000);                  /* the vendor's 3.0 V, in mV, as its own
	                                               * rpmh_regulator_vrm_set_voltage() sends */
	ok = tcs_send(cell + 4, 1) && ok;           /* enable */
	ok = tcs_send(cell + 8, 2) && ok;           /* auto, the tree's init-mode. The other mode
	                                               * this resource supports is high power, whose
	                                               * hardware value in the vendor's driver is 7;
	                                               * the gentler one is chosen because the panel
	                                               * shares the rail */
	say("touch: rpmh: ldoa17 (the touch's 2.8 V, and the display's analog) at %#x: %s\n", cell,
	    ok ? "now 3.0 V, enabled, auto" : "did not complete");
}

/* Ask the chip what it is, the same read every step makes, and say what came back. */
static bool
chip_answered(const char *when)
{
	uint8_t id[ID_LENGTH];
	const char *problem = read_id(TCM_PRODUCT_ID, id);

	if (problem == NULL) {
		say_id(when, TCM_PRODUCT_ID, id);
		return true;
	}
	say("touch: reading the TCM id %s: %s\n", when, problem);
	return false;
}

/* The same two votes, to one address: the first value is the one the vendor's i2c driver votes
 * with; if that is under the cell's lowest step, the largest it can take cannot be. The wrapper's
 * clock control is read-only and always reads 0, and the command's completed bit says only that
 * the controller got an answer for the address, so the chip itself is asked after every send:
 * whether it answers is the only thing that decides anything here. After the first vote the engine
 * is configured once more, in case its clock only now exists. */
static bool
bcm_try(uint32_t cell)
{
	static const uint32_t votes[] = { 960, 0x3fff };

	for (unsigned i = 0; i < sizeof(votes) / sizeof(votes[0]); i++) {
		bcm_send_vote(cell, votes[i]);
		pause_ms(10);
		if (i == 0) {
			wrapper_enable();
			engine_configure();
		}
		say("touch:   rpmh: after the %#x vote to %#x: wrapper clock control %#x, wrapper "
		    "state for engine 1 %#x, for engine 17 %#x\n", votes[i], cell,
		    rd(QUP_SE_CLK_CTL_ENABLE), rd(QUP_SE_STATE(1)), rd(QUP_SE_STATE(17)));
		if (chip_answered("after a vote")) {
			return true;
		}
	}
	return false;
}

static bool
bcm_vote(uint32_t cell)
{
	uint32_t cfg;

	if (!rpmh_map()) {
		return false;
	}
	/* the canary: the vendor's driver reads this same register at probe, so it answers, and
	 * it says the controller is really here: commands per slot, and slots per driver. */
	cfg = rd(rsc_cfg + RSC_CFG_SLOTS);
	say("touch:   rpmh: the controller's configuration reads %#x: %u commands per slot, %u slots "
	    "for driver 2\n", cfg, cfg >> 27 & 0x1f, cfg >> (6 * 2) & 0x3f);
	say("touch:   rpmh: before the vote: wrapper clock control %#x, wrapper state for engine 1 "
	    "%#x, for engine 17 %#x\n", rd(QUP_SE_CLK_CTL_ENABLE), rd(QUP_SE_STATE(1)),
	    rd(QUP_SE_STATE(17)));
	if (chip_answered("before the vote")) {
		return true;
	}
	return bcm_try(cell);
}

/* ---- finding the PMIC's side of this: the arbiter, and the enable pin ----------------------------- */

/* Step 5's first half: the arbiter's inventory, and the channel that reaches the PM8150L's GPIO block. */
static bool
arbiter_look(void)
{
	uint32_t version, channels, found = 0;
	uint16_t ppids[ARB_MAX_CHANNELS];
	uint16_t channel_of[ARB_MAX_CHANNELS];

	arb_core = ml_io_map(ARB_CORE_PHYS, ARB_CORE_SIZE);
	arb_observer = ml_io_map(ARB_OBSERVER_PHYS, ARB_OBSERVER_SIZE);
	if (arb_core == 0 || arb_observer == 0) {
		say("touch: could not map the PMIC arbiter\n");
		return false;
	}
	version = rd(arb_core + ARB_VERSION);
	channels = rd(arb_core + ARB_FEATURES) & 0x7ff;
	say("touch: PMIC arbiter version %#x, %u channels\n", version, channels);
	if (version < ARB_VERSION_5 || version >= ARB_VERSION_7) {
		say("touch: this driver knows version 5 of the arbiter only: the enable pin stays unread\n");
		return false;
	}
	if (channels == 0 || channels > ARB_MAX_CHANNELS) {
		channels = ARB_MAX_CHANNELS;
	}
	/* one entry for each distinct slave and peripheral, with the first channel that reaches it, as
	 * hakios.pon.c and hakios.smb5.c do */
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
		uint8_t type = 0xff, subtype = 0xff;
		uint32_t slave = ppids[i] >> 8, block = ppids[i] & 0xff;

		if (block != ENABLE_BLOCK || slave != ENABLE_SLAVE) {
			continue;       /* the whole inventory was said by the pmic and pon drivers' logs */
		}
		if (spmi_read(channel_of[i], 0x04, &type, 1) != NULL) {
			say("touch: slave %u 0x%02x00: no answer, the enable pin stays unread\n", slave, block);
			continue;
		}
		(void)spmi_read(channel_of[i], 0x05, &subtype, 1);
		say("touch: slave %u 0x%02x00: type %#02x subtype %#02x%s\n", slave, block, type, subtype,
		    type == GPIO_TYPE ? " (the PM8150L's GPIO controller: where the 1.8 V enable is)" : "");
	}
	/* the enable pin itself. Every pin is a peripheral of its own (this one is 0xc4), and the arbiter's
	 * table gives every peripheral the channels that reach it: a transaction's peripheral high byte
	 * comes from the channel's ppid, so the block's channel cannot reach the pin's registers, and the
	 * 19:05 run showed it: reads of 0xc4+0x40 came back with the block's own type and subtype. */
	/* v5 answers a read through every apid's observer slot in every EE, so the reads above said
	 * nothing about who owns the pin's write channel. A write belongs to the apid whose owner
	 * entry in cnfg names this EE, and pmic_arb_read_apid_map_v5() of the vendor's 4.19 keeps
	 * exactly that one of a ppid's several mappings; the first mapping is kept only until one
	 * for this EE turns up. Do the same, and if none is ours make the write anyway: its
	 * status word will say denied, or the bus will say what a non-owner's write costs. */
	say("touch: the arbiter's owner table at %#x, the first read of that window\n",
	    (unsigned)ARB_CNFG_PHYS);
	pause_ms(WATCH_MS);
	arb_config = ml_io_map(ARB_CNFG_PHYS, ARB_CNFG_SIZE);
	if (arb_config == 0) {
		say("touch: the arbiter's configuration window at %#x could not be mapped: the first "
		    "channel reaching the enable pin will be used for the write anyway\n",
		    (unsigned)ARB_CNFG_PHYS);
	}
	for (uint32_t n = 0; n < channels; n++) {
		uint32_t map = rd(arb_core + ARB_CHANNEL(n));

		if (((map >> 8) & 0xfff) != ((ENABLE_SLAVE << 8) | ENABLE_PERIPHERAL)) {
			continue;
		}
		if (arb_config == 0) {
			if (enable_channel == ~0u) {
				enable_channel = n;
			}
			continue;
		}
		uint32_t owner = rd(arb_config + ARB_OWNER(n)) & 7;

		say("touch: the enable pin is reachable through %u, owned by EE %u%s\n", n, owner,
		    owner == ARB_SYSTEM ? " (ours: the write is ours to make)" : "");
		if (owner == ARB_SYSTEM) {
			enable_channel = n;
			break;
		}
		if (enable_channel == ~0u) {
			enable_channel = n; /* none of ours yet, and may never be: the vendor's
			                     * driver keeps the first mapping in just this case */
		}
	}
	if (enable_channel == ~0u) {
		say("touch: the enable pin's own peripheral is not reachable: the enable pin stays unread\n");
		return false;
	}
	/* the write window, mapped only now and only around the one channel this driver writes
	 * through. The tree gives chnls 0x2000000, one 0x10000 block per channel, and the kernel's
	 * whole I/O mapping arena is 8 MB; the 19:49 run mapped one block at the window's base and
	 * then wrote at the enable pin's channel offset, which is outside it, and took a data abort
	 * there. The channel number is worth a line: it says which block was mapped. */
	arb_chnls = ml_io_map(ARB_CHNLS_PHYS + 0x10000UL * enable_channel, ARB_CHNLS_SIZE);
	if (arb_chnls == 0) {
		say("touch: could not map the write window of channel %u: the enable pin stays unread\n",
		    enable_channel);
		return false;
	}
	say("touch: the enable pin is reached through arbiter channel %u\n", enable_channel);
	return true;
}

/* Which PMIC really carries the touch's 1.8 V enable: the overlay gives the pin's controller as a
 * bare phandle number of the base device tree, which the boot tree's numbering does not resolve, so
 * all three PMIC GPIO controllers this phone has are asked in turn. The pin's peripheral answers
 * type 0x10 when it is a GPIO block; the one that is the enable is driven as an output, level low
 * (the overlay's flag 0x01 is active low), and the bus scan that follows says which one the chip
 * answered after. */
static void
enable_try_on(uint32_t slave, uint8_t peripheral)
{
	uint32_t channel = ~0u, any_channel = ~0u, ppid = (slave << 8) | peripheral;
	uint8_t type = 0xff, subtype = 0xff, regs[4] = { 0xff, 0xff, 0xff, 0xff };
	vm_offset_t saved = arb_chnls;

	if (arb_core == 0) {
		return;
	}
	for (uint32_t n = 0; n < ARB_MAX_CHANNELS; n++) {
		uint32_t map = rd(arb_core + ARB_CHANNEL(n));

		if (map == 0 || ((map >> 8) & 0xfff) != ppid) {
			continue;
		}
		if (any_channel == ~0u) {
			any_channel = n;
		}
		if (arb_config == 0 || (rd(arb_config + ARB_OWNER(n)) & 7) == ARB_SYSTEM) {
			channel = n;
			break;
		}
	}
	if (any_channel == ~0u) {
		say("touch: slave %u 0x%02x: no channel reaches it\n", slave, peripheral);
		return;
	}
	if (spmi_read(any_channel, 0x04, &type, 1) != NULL) {
		say("touch: slave %u 0x%02x: no answer\n", slave, peripheral);
		return;
	}
	(void)spmi_read(any_channel, 0x05, &subtype, 1);
	/* the pin's own registers, readable through any channel's observer slot: mode, output
	 * source and level, master enable, and the real-time status */
	(void)spmi_read(any_channel, PMIC_MODE_CTL, &regs[0], 1);
	(void)spmi_read(any_channel, PMIC_LV_DIG_OUT_SRC, &regs[1], 1);
	(void)spmi_read(any_channel, PMIC_EN_CTL, &regs[2], 1);
	(void)spmi_read(any_channel, PMIC_RT_STS, &regs[3], 1);
	say("touch: slave %u 0x%02x: type %#02x subtype %#02x (read via %u, write channel %d): "
	    "mode %#02x, source %#02x, enable %#02x, level %#02x\n", slave, peripheral, type, subtype,
	    any_channel, channel == ~0u ? -1 : (int)channel, regs[0], regs[1], regs[2], regs[3]);
	if (type != GPIO_TYPE || channel == ~0u) {
		if (type == GPIO_TYPE) {
			say("touch:   its write channel belongs to another execution environment: "
			    "left alone, and its state above is what the owner has it at\n");
		}
		return;
	}
	arb_chnls = ml_io_map(ARB_CHNLS_PHYS + 0x10000UL * channel, ARB_CHNLS_SIZE);
	if (arb_chnls != 0) {
		say("touch:   driving it as the 1.8 V enable: output, level low, master enable\n");
		(void)spmi_write(PMIC_MODE_CTL, PMIC_LV_MODE_OUTPUT);
		(void)spmi_write(PMIC_LV_DIG_OUT_SRC, PMIC_LV_SOURCE_REGISTER);
		(void)spmi_write(PMIC_EN_CTL, PMIC_MASTER_EN);
	} else {
		say("touch:   its write window does not map: not driven\n");
	}
	arb_chnls = saved;
}

/* Is the touch bus out of its clamp? Both wires high with a pull-up and nothing driving them. */
static bool
bus_alive(void)
{
	uint32_t scl = rd(GPIO_CFG(PIN_SCL)), sda = rd(GPIO_CFG(PIN_SDA));
	bool alive;

	wr(GPIO_CFG(PIN_SCL), pin_bits(FUNCTION_GPIO, 3, DRIVE_4MA, false));
	wr(GPIO_CFG(PIN_SDA), pin_bits(FUNCTION_GPIO, 3, DRIVE_4MA, false));
	sleep_us(300);
	alive = (rd(GPIO_IO(PIN_SCL)) & 1) && (rd(GPIO_IO(PIN_SDA)) & 1);
	wr(GPIO_CFG(PIN_SCL), scl);
	wr(GPIO_CFG(PIN_SDA), sda);
	return alive;
}

/* The sweep's answer is known since the 14:42 run: slave 4 (the PM8150L), pin 5, active high. It is
 * only re-run when a boot argument asks, to re-confirm on a different board revision. */
static uint32_t sweep_wanted;

/* The 2.8 V vote, off unless asked for. See touch_2v8_vote: the rail it would raise is the same rail
 * the display's analog (lab-supply) is fed from, and asking for it wrongly is what blanked the panel
 * at 19:42 and 19:52. */
static uint32_t vote_2v8_wanted;

/* Which PMIC GPIO, if any, is the touch's 1.8 V enable. The overlay's phandle for it does not
 * resolve against any device tree this port can read (the base tree's 0x63 is a camera node), so the
 * only honest way left is to ask the hardware: every GPIO of the three PMIC GPIO controllers is
 * driven as an output, first low then high (the enable may be active low or active high), and after
 * each the two bus wires are read back. The pin that makes them both rise is the enable; each pin's
 * original registers are put back before the next one is tried. */
static void
enable_sweep(void)
{
	static const uint32_t slaves[] = { 0, 2, 4 };
	unsigned tried = 0;

	for (unsigned s = 0; s < sizeof(slaves) / sizeof(slaves[0]); s++) {
		for (unsigned pin = 1; pin <= 12; pin++) {
			uint32_t ppid = (slaves[s] << 8) | (0xc0 + pin - 1);
			uint32_t channel = ~0u;
			uint8_t type = 0xff, mode, src, en;
			vm_offset_t saved;

			if (arb_core == 0) {
				return;
			}
			for (uint32_t n = 0; n < ARB_MAX_CHANNELS; n++) {
				uint32_t map = rd(arb_core + ARB_CHANNEL(n));

				if (map == 0 || ((map >> 8) & 0xfff) != ppid) {
					continue;
				}
				if (arb_config == 0 || (rd(arb_config + ARB_OWNER(n)) & 7) == ARB_SYSTEM) {
					channel = n;
					break;
				}
			}
			if (channel == ~0u || spmi_read(channel, 0x04, &type, 1) != NULL || type != GPIO_TYPE) {
				continue;
			}
			if (spmi_read(channel, PMIC_MODE_CTL, &mode, 1) != NULL ||
			    spmi_read(channel, PMIC_LV_DIG_OUT_SRC, &src, 1) != NULL ||
			    spmi_read(channel, PMIC_EN_CTL, &en, 1) != NULL) {
				continue;
			}
			saved = arb_chnls;
			arb_chnls = ml_io_map(ARB_CHNLS_PHYS + 0x10000UL * channel, ARB_CHNLS_SIZE);
			if (arb_chnls == 0) {
				continue;
			}
			tried++;
			(void)spmi_write(PMIC_EN_CTL, en | PMIC_MASTER_EN);
			(void)spmi_write(PMIC_MODE_CTL, PMIC_LV_MODE_OUTPUT);
			(void)spmi_write(PMIC_LV_DIG_OUT_SRC, PMIC_LV_SOURCE_REGISTER);
			if (bus_alive()) {
				say("touch: ** slave %u pin %u driven LOW is the enable: the bus reads high **\n",
				    slaves[s], pin);
				return;                 /* leave this one driven */
			}
			(void)spmi_write(PMIC_LV_DIG_OUT_SRC, PMIC_LV_SOURCE_REGISTER | PMIC_LV_OUT_HIGH);
			if (bus_alive()) {
				say("touch: ** slave %u pin %u driven HIGH is the enable: the bus reads high **\n",
				    slaves[s], pin);
				return;                 /* leave this one driven */
			}
			(void)spmi_write(PMIC_MODE_CTL, mode);
			(void)spmi_write(PMIC_LV_DIG_OUT_SRC, src);
			(void)spmi_write(PMIC_EN_CTL, en);
			arb_chnls = saved;
		}
	}
	say("touch: the sweep drove %u PMIC GPIOs and the touch bus stayed low through every one\n", tried);
}

/* One register of the enable pin's peripheral, then the level on the pin immediately after. This is the
 * pair the experiment in enable_look() asks about, so it is asked the same way every time: write, read
 * the level, one line about both. Whether the pin read high is the answer the caller wants. */
static bool
try_level(const char *what, uint8_t reg, uint8_t value)
{
	const char *problem = spmi_write(reg, value);
	uint8_t level = 0xff;
	const char *unread = spmi_read(enable_channel, PMIC_RT_STS, &level, 1);

	if (unread != NULL) {
		say("touch:   %s (%#02x = %#02x): %s, the level could not be read: %s\n", what, reg, value,
		    problem == NULL ? "written" : problem, unread);
		return false;
	}
	say("touch:   %s (%#02x = %#02x): %s, the pin reads %s\n", what, reg, value,
	    problem == NULL ? "written" : problem, (level & 1) ? "HIGH" : "low");
	return (level & 1) != 0;
}

/* Every register of the enable pin's peripheral this driver has a name for, in one line at the end of
 * the experiment: the 21:34 run showed six of them and reasoned about the rest, and the reasoning about
 * the drive register (0x45) is the one that is still standing. */
static void
snapshot_pin(void)
{
	static const struct { uint8_t reg; const char *what; } all[] = {
		{ 0x04, "type" }, { 0x05, "subtype" }, { PMIC_RT_STS, "level" },
		{ PMIC_MODE_CTL, "mode" }, { PMIC_DIG_VIN_CTL, "voltage in" }, { PMIC_DIG_PULL_CTL, "pull" },
		{ 0x43, "input" }, { PMIC_LV_DIG_OUT_SRC, "output source" }, { PMIC_DIG_OUT_CTL, "drive" },
		{ PMIC_EN_CTL, "enable" }, { 0x47, "int type" }, { 0x48, "int pulse" } };

	say("touch:   the enable pin's registers, all of them, at the end of step 5");
	pause_ms(WATCH_MS);
	printf("touch:   ");
	for (uint32_t i = 0; i < sizeof(all) / sizeof(all[0]); i++) {
		uint8_t v = 0xff;

		if (spmi_read(enable_channel, all[i].reg, &v, 1) != NULL) {
			printf(" %s?", all[i].what);
			continue;
		}
		printf(" %s %#02x", all[i].what, v);
	}
	printf("\n");
}

/* Step 5's second half: the enable pin. Reading it is safe; switching it on is the one PMIC write here,
 * and only when it is off and the chip has not answered -- an enable pin of its own, which is what it
 * exists for. */
static void
enable_look(void)
{
	static const char *names[] = { "real time status", "mode", "output source", "pull", "voltage in",
	    "enable" };
	static const uint8_t regs[] = { PMIC_RT_STS, PMIC_MODE_CTL, PMIC_LV_DIG_OUT_SRC, PMIC_DIG_PULL_CTL,
	    PMIC_DIG_VIN_CTL, PMIC_EN_CTL };
	uint8_t here[6];
	bool high = false, driven = false, powered = false;

	/* one register at a time, which is what the vendor's driver does for a PMIC, and the register byte
	 * on its own: the peripheral's high byte is the channel's, as the vendor's spmi layer passes it */
	for (uint32_t i = 0; i < 6; i++) {
		if (spmi_read(enable_channel, regs[i], &here[i], 1) != NULL) {
			here[i] = 0xff;
		}
	}
	say("touch: pmic gpio%u (the 1.8 V enable) at slave %u 0x%02x00:", ENABLE_GPIO, ENABLE_SLAVE,
	    ENABLE_PERIPHERAL);
	for (uint32_t i = 0; i < 6; i++) {
		printf(" %s %#02x", names[i], here[i]);
	}
	printf("\n");
	high = here[0] & 1;
	driven = (here[1] & 3) == PMIC_LV_MODE_OUTPUT;
	powered = here[5] & PMIC_MASTER_EN;
	say("touch: the 1.8 V enable: the pin is %s, %s, and reads %s\n",
	    powered ? "enabled" : "NOT enabled",
	    driven ? ((here[2] & PMIC_LV_OUT_HIGH) ? "driven by this PMIC, output high" :
	        "set as an output but not driving") : "not set as an output",
	    high ? "high" : "low");
	if (high && powered) {
		return;
	}
	/* the way upstream's pinctrl-spmi-gpio.c brings up an LV output: the direction, then the level
	 * from the software source, then enable, in that order and with those values (its config_set
	 * writes 0x40 = 1, then 0x44 = the value in bit 7 with source 0, then 0x46 = bit 7). The pull
	 * and the drive range are left as the boot firmware left them for now: the experiment below
	 * asks about those separately. */
	say("touch:   the first writes to the arbiter's channel window at %#x\n",
	    (unsigned)ARB_CHNLS_PHYS);
	pause_ms(WATCH_MS);
	for (uint32_t i = 0; i < 3; i++) {
		static const struct { uint8_t reg; uint8_t value; const char *what; } want[] = {
			{ PMIC_MODE_CTL, PMIC_LV_MODE_OUTPUT, "direction output" },
			{ PMIC_LV_DIG_OUT_SRC, PMIC_LV_SOURCE_REGISTER | PMIC_LV_OUT_HIGH, "the level high" },
			{ PMIC_EN_CTL, PMIC_MASTER_EN, "the master enable" },
		};
		const char *problem = spmi_write(want[i].reg, want[i].value);

		say("touch:   writing %s %#02x: %s\n", want[i].what, want[i].value,
		    problem == NULL ? "ok" : problem);
	}
	/* The 21:34 run stopped here with every write accepted and the pin reading low, and concluded
	 * the 1.8 V was off. That conclusion was premature, and upstream says why: its pmic_gpio_get()
	 * reads the real-time status register only when the pad's input is enabled, and for a pad it has
	 * set to an output it returns the value it wrote without reading the hardware at all. A pin
	 * configured as an output alone has its input path closed, so 0x10 reads 0 whatever the pin is
	 * doing, and the 00 above says nothing about the level. What follows opens the input path along
	 * with the output (0x40 = 2, which upstream uses for a pad that is both), which is the first
	 * configuration in which 0x10 means what it looks like, and then asks the one question that
	 * settles the pin: does the level follow bit 7 of 0x44? The pull stays a pull-down on purpose:
	 * with the output off, that pull is what makes a low reading honest rather than floating. */
	say("touch:   the input path is closed on an output-only pin: opening it to read the level for real\n");
	pause_ms(WATCH_MS);
	(void)try_level("the input path open as well as the output", PMIC_MODE_CTL,
	    PMIC_LV_MODE_INPUT_OUTPUT);
	(void)try_level("the drive at its strongest", PMIC_DIG_OUT_CTL, PMIC_OUT_TYPE_CMOS | PMIC_OUT_STRONG);
	(void)try_level("the level bit cleared", PMIC_LV_DIG_OUT_SRC, PMIC_LV_SOURCE_REGISTER);
	(void)try_level("the level bit set again", PMIC_LV_DIG_OUT_SRC,
	    PMIC_LV_SOURCE_REGISTER | PMIC_LV_OUT_HIGH);
	(void)try_level("the pull released", PMIC_DIG_PULL_CTL, PMIC_PULL_NONE);
	(void)try_level("the pull back to a pull-down", PMIC_DIG_PULL_CTL, PMIC_PULL_DOWN);
	(void)try_level("the voltage input named 0", PMIC_DIG_VIN_CTL, 0);
	/* The tree says `enable1v8_gpio = <&pm8150l 5 0x01>`, flag 0x01 = GPIO_ACTIVE_LOW, and an
	 * earlier run trusted that and drove the pin low: the bus stayed clamped down. The hardware
	 * was asked directly instead (the sweep below, the 14:42 run) and it answered: this very pin
	 * driven HIGH is what makes the two bus wires rise; driven low they stay down. The flag is
	 * wrong or describes something else, and the level the hardware agrees with is high, so high
	 * is what is left asserted: output, level from the software register with bit 7 set, master
	 * enable, which is upstream's config_set for an LV output with the value 1. */
	(void)try_level("the enable as an output", PMIC_MODE_CTL, PMIC_LV_MODE_OUTPUT);
	(void)try_level("the enable asserted, the 1.8 V on (active high, proved on the phone)",
	    PMIC_LV_DIG_OUT_SRC, PMIC_LV_SOURCE_REGISTER | PMIC_LV_OUT_HIGH);
	/* and the 2.8 V, which the vendor's touch driver turns on at probe -- but the same rail is the
	 * display's lab-supply, the display is already running from it, and asking for it with a mode the
	 * resource does not support blanks the panel, so this is left to touch_2v8=1. */
	if (vote_2v8_wanted) {
		touch_2v8_vote();
	} else {
		say("touch: the 2.8 V is left as the boot left it: pm8150_l17 is also the display's "
		    "lab-supply and the display is running from it now\n");
	}
	/* The sweep that asked every PMIC GPIO which one is the enable ran on the phone at 14:42 and
	 * answered: slave 4 (the PM8150L), pin 5, active high: the pin just driven. It stays behind
	 * touch_sweep=1 in case a later board revision moves the enable somewhere else. */
	if (sweep_wanted) {
		enable_sweep();
	}
	/* And look at the two wires right here, in order: the pins are on qup17 at this point, so
	 * switch them to GPIO with a pull-up for the read and put them back afterwards. A bus still
	 * clamped low after every PMIC's enable has been driven is not a rail problem any more. */
	{
		uint32_t saved_scl = rd(GPIO_CFG(PIN_SCL)), saved_sda = rd(GPIO_CFG(PIN_SDA));

		wr(GPIO_CFG(PIN_SCL), pin_bits(FUNCTION_GPIO, 3, DRIVE_4MA, false));
		wr(GPIO_CFG(PIN_SDA), pin_bits(FUNCTION_GPIO, 3, DRIVE_4MA, false));
		sleep_us(200);
		say("touch: with the enables driven, gpio55/gpio56 read %u/%u (cfg %#x %#x)\n",
		    rd(GPIO_IO(PIN_SCL)) & 1, rd(GPIO_IO(PIN_SDA)) & 1, rd(GPIO_CFG(PIN_SCL)),
		    rd(GPIO_CFG(PIN_SDA)));
		wr(GPIO_CFG(PIN_SCL), saved_scl);
		wr(GPIO_CFG(PIN_SDA), saved_sda);
	}
	snapshot_pin();
}

/* ---- the steps ------------------------------------------------------------------------------------ */

static uint32_t steps_wanted = 7;

/* The pads are the default: the QUP engine this phone's touch chip hangs on never starts a
 * command (step 6's own words), so going through it first costs the boot the engine's three
 * attempts and two long waits before the bit-bang path begins. touch_bb=0 asks for that order
 * back for a driver experiment. */
static bool touch_bb_first = true;

static bool
touch_bring_up(void)
{
	uint32_t version, ctrl, cfg;
	bool answered;

	gcc = ml_io_map(GCC_PHYS, GCC_SIZE);
	tlmm = ml_io_map(TLMM_PHYS, TLMM_SIZE);
	qup = ml_io_map(QUP_PHYS, QUP_SIZE);
	se = ml_io_map(SE_PHYS, SE_SIZE);
	if (gcc == 0 || tlmm == 0 || qup == 0 || se == 0) {
		say("touch: could not map the bus: gcc %#lx, tlmm %#lx, qup %#lx, se %#lx\n", (unsigned long)gcc,
		    (unsigned long)tlmm, (unsigned long)qup, (unsigned long)se);
		return false;
	}
	say("touch: step 1: the serial engine's clocks\n");
	if (clocks_on() != NULL) {
		return false;
	}
	if (steps_wanted < 2) {
		return false;
	}
	say("touch: step 2: the QUP wrapper\n");
	/* QUP_HW_VER_REG is the one register of the wrapper's core that mainline's qcom-geni-se.c reads
	 * (0x4); what follows is the vendor driver's own opening sequence -- the capability register, the
	 * clock gates, the engine states -- which mainline leaves to the rpmh and to the boot firmware,
	 * and neither of those has prepared this engine. The wrapper's reset was read and, if it was
	 * held, released in step 1. */
	version = rd(QUP_HW_VER);
	say("touch: qup2 at %#x: hardware version %u.%u.%u\n", (unsigned)QUP_PHYS, version >> 28 & 0xf,
	    version >> 16 & 0xfff, version & 0xffff);
	cfg = rd(QUP_COMMON_CFG);
	say("touch: qup2: common configuration %#x%s\n", cfg,
	    cfg & (1u << 18) ? ", with a dma interface" : ", no dma interface");
	/* The wrapper's clock gates, which nothing has ever opened here: the vendor's bus_enable writes
	 * all four before any use of a bus, and mainline leaves that to the rpmh. +0x8 is a word this
	 * phone has never been asked for, and the 23:27 run showed the window is not backed everywhere,
	 * so say which word comes next before reading it: if this is the one that makes the bus hang,
	 * the log says so. The 23:51 run answered the rest: the word reads 0 and stays 0 no matter what
	 * is written to it, and so do the engine's own gates at +0x60 and the state slots at +0x80c --
	 * the wrapper takes its orders from the rpmh, not from these addresses, so read them and report
	 * and write nothing, because a write to a word the bus is not expecting is what hung the 23:27
	 * run. */
	say("touch: qup2: probing the clock control at +0x8\n");
	say("touch: qup2: clock control reads %#x, the read-only view says %#x\n",
	    rd(QUP_SE_CLK_CTL_ENABLE), rd(QUP_SE_CLK_CTL_ENABLE_RO));
	/* The same gates on this engine's side, which is where the vendor's bus_enable writes them too.
	 * +0x60 is inside the engine's own block, which every earlier run read from end to end without
	 * harm, so this needs no marker. */
	ctrl = rd(SE_CLK_CTRL_RO);
	if ((ctrl & 3u) != 3u) {
		wr(SE_CLK_CTRL_RO, 3u);
		say("touch: se17: engine clock control %#x -> %#x\n", ctrl, rd(SE_CLK_CTRL_RO));
	} else {
		say("touch: se17: engine clock control %#x, already open\n", ctrl);
	}
	for (unsigned i = 0; i < 2; i++) {
		uint32_t slot = i == 0 ? 1u : 17u;
		uint32_t state = rd(QUP_SE_STATE(slot));

		say("touch: qup2: engine %u state %#x: %s\n", slot, state,
		    state & SE_STATE_ACTIVE ? "running a command" :
		    state & SE_STATE_IDLE ? "configured and idle" :
		    state & SE_STATE_SEQ_EN ? "enabled, waiting" : "nothing at all");
	}
	if (steps_wanted < 3) {
		return false;
	}
	say("touch: step 3: the serial engine\n");
	if (!engine_look()) {
		return false;
	}
	if (steps_wanted < 4) {
		return false;
	}
	say("touch: step 4: the pins\n");
	tlmm_probe();
	report_pin("the chip's reset", PIN_RESET);
	report_pin("the bus clock", PIN_SCL);
	report_pin("the bus data", PIN_SDA);
	report_pin("the chip's interrupt", PIN_IRQ);
	say("touch: tlmm: now putting gpio55 and gpio56 on the engine's function\n");
	pause_ms(WATCH_MS);
	pins_bus();
	report_pin("the bus clock", PIN_SCL);
	if (steps_wanted < 5) {
		return false;
	}
	say("touch: step 5: the 1.8 V enable\n");
	if (!arbiter_look()) {
		return false;
	}
	enable_look();
	if (steps_wanted < 6) {
		return false;
	}
	/* The engine on this phone never finishes a command, and proving that costs the bring-up
	 * two long waits (one of them 30 s) before the pads are used instead: the screen is dead
	 * for a minute and a half after every boot (logs/191722-run.log's taps start at 19:19:08,
	 * the boot at 19:17:35). So the pads are the default; touch_bb=0 asks for the long way. */
	if (touch_bb_first) {
		say("touch: the pads first (touch_bb): the engine is left out of the bring-up\n");
		if (bitbang_probe()) {
			return true;
		}
		say("touch: the pads answered nothing; going back to the engine\n");
	}
	say("touch: step 6: configuring the engine\n");
	engine_configure();
	if (steps_wanted < 7) {
		return false;
	}
	say("touch: step 7: the chip\n");
	answered = chip_probe();
	if (steps_wanted < 8) {
		return true;
	}
	if (answered) {
		say("touch: step 8: nothing to ask: the chip answered already\n");
		return true;
	}
	say("touch: step 8: the two votes no clock driver claims\n");
	answered = unclaimed_vote();
	if (steps_wanted < 9) {
		return true;
	}
	if (answered) {
		say("touch: step 9: nothing to ask: the chip answered already\n");
		return true;
	}
	say("touch: step 9: the bus clock manager's vote, sent by hand\n");
	answered = bcm_vote(BCM_QUP0_CELL);
	if (steps_wanted < 10) {
		return true;
	}
	if (answered) {
		say("touch: step 10: nothing to ask: the chip answered already\n");
		return true;
	}
	/* the vendor's fabric driver takes the address from the command database, never from the
	 * tree's cell-id: find what it says for QUP0 and send the same vote there. the entry's
	 * address word is a slave id and a cell offset; the fabric driver sends the whole word
	 * and the hardware is believed to mask what it does not need, but the low half alone is
	 * the plain cell address, so if the whole word does not reach the chip, try that. */
	say("touch: step 10: the cell's real address, from the boot loader's table\n");
	uint32_t real = cmdb_look("QUP0");
	if (real == 0) {
		say("touch:   cmd db: no address for \"QUP0\": the tree's cell-id is all there is\n");
		return true;
	}
	if (real == BCM_QUP0_CELL) {
		say("touch:   cmd db: the table says %#x, which is the cell-id step 9 sent: the "
		    "address was not the problem\n", real);
		return true;
	}
	say("touch:   cmd db: the table says %#x, not the cell-id %#x: sending the vote there\n",
	    real, BCM_QUP0_CELL);
	answered = bcm_vote(real);
	if (!answered && (real & ~0xffffu) != 0) {
		say("touch:   cmd db: %#x did not reach the chip: trying its cell offset %#x "
		    "alone\n", real, real & 0xffffu);
		answered = bcm_vote(real & 0xffffu);
	}
	if (!answered) {
		say("touch: neither the table's address nor its cell offset alone made the chip "
		    "answer\n");
	}
	return true;
}
static void
touch_thread(__unused void *parameter, __unused wait_result_t result)
{
	if (!touch_bring_up()) {
		say("touch: stopped: the next step needs the one before it to work\n");
	}
	thread_terminate(current_thread());
}

__startup_func
static void
s3706_init(void)
{
	uint32_t enabled = 0, steps = 0;
	thread_t thread;

	if (!PE_parse_boot_argn("touch", &enabled, sizeof(enabled)) || !enabled) {
		return;
	}
	{
		uint32_t bb = 1;

		if (PE_parse_boot_argn("touch_bb", &bb, sizeof(bb))) {
			touch_bb_first = bb != 0;
		}
	}
	if (PE_parse_boot_argn("touch_steps", &steps, sizeof(steps)) && steps >= 1 && steps <= 10) {
		steps_wanted = steps;
	}
	(void)PE_parse_boot_argn("touch_sweep", &sweep_wanted, sizeof(sweep_wanted));
	(void)PE_parse_boot_argn("touch_2v8", &vote_2v8_wanted, sizeof(vote_2v8_wanted));
	/* a thread of its own: the clock waits and the bus waits, and the reset alone is a quarter of a
	 * second, which does not belong in the boot path */
	if (kernel_thread_start(touch_thread, NULL, &thread) == KERN_SUCCESS) {
		thread_deallocate(thread);
	}
}
STARTUP(EARLY_BOOT, STARTUP_RANK_LAST, s3706_init);
