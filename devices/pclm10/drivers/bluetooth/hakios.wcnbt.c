/*
 * Bluetooth of the OPPO Reno Ace (PCLM10): the WCN3990's HCI, on the QUPv3 serial engine the
 * phone's device tree calls hsuart0. Only with boot-arg wcnbt=1 (wcnbt=2 adds an HCI Reset).
 *
 * This is the Bluetooth half of the same chip the WLAN half runs on through the SLPI
 * (drivers/wifi/hakios.slpi.c). It is a different path: the WLAN's firmware runs on the Q6 and
 * speaks GLINK/QMI; the Bluetooth controller has its own ROM, its own firmware and a plain
 * H4 serial link to this processor. No SLPI and no QMI are involved in what this file does.
 *
 * Where each number comes from.
 *
 *   transport   the overlay's bt_wcn3990 node (vendor-src/dtb/dtbo.bin.01.dts:6390-6402) names
 *               the chip qca,wcn3990; the base tree's qcom,qup_uart@0xc8c000
 *               (android-merged.dts:20134-20149) is its host port: compatible
 *               "qcom,msm-geni-serial-hs", reg 0xc8c000 0x4000, clocks <&gcc 0x73 0x7d 0x7e>
 *               and qcom,wrapper-core <&qupv3_2_geni_se>. The alias hsuart0 points at it
 *               (boot.bin.01.dts:23689). The clock IDs are the vendor's dt-bindings:
 *               GCC_QUPV3_WRAP2_S3_CLK 115, GCC_QUPV3_WRAP_2_M_AHB_CLK 125,
 *               GCC_QUPV3_WRAP_2_S_AHB_CLK 126 (include/dt-bindings/clock/qcom,gcc-sm8150.h).
 *               The vendor's own sm8150-mtp.dtsi enables &qupv3_se13_4uart and puts the same
 *               bt_wcn3990 node under &vendor (lines 34-46 and 67-69 of that file), which is
 *               where the phone's overlay comes from.
 *
 *   pins        the node's pinctrl states (android-merged.dts:6573-6650):
 *               gpio43 CTS, gpio44 RTS, gpio45 TX, gpio46 RX, all function "qup13" at 2 mA.
 *               pinctrl-sm8150.c puts those four pins in the EAST tile (PINGROUP(43, EAST, ...)
 *               at :1654-1657) and qup13 is function index 1 in that group. The tile is at
 *               0x03500000 (pinctrl-sm8150.c's EAST = 0x00500000 plus the tree's
 *               pinctrl@03000000 base), and a pin's register is tile + 0x1000 * pin
 *               (pinctrl-msm.c: base + g->ctl_reg, REG_SIZE 0x1000). Pulls: CTS and RX
 *               bias-disable (0), RTS bias-pull-down (1), TX bias-pull-up (3); drive-strength
 *               <2> is register code (2 / 2) - 1 = 0 (pinctrl-msm.c msm_config_group_set).
 *
 *   clocks      drivers/clk/qcom/gcc-sm8150.c (fetched to vendor-src/clk/) for QUPv3 wrapper 2,
 *               engine 3: the wrapper reset at 0x1e000 bit 0, the engine's branch halt at
 *               0x1e4d4 and its vote bit 7 in 0x52014, the RCG at cmd_rcgr 0x1e4d8 (M 0x1e4e0,
 *               N 0x1e4e4, D 0x1e4e8, CFG 0x1e4dc), the master/slave AHB branch halts at
 *               0x1e004/0x1e008 and their vote bits 2/1. The engine's rate comes from
 *               gcc_qupv3_wrap2_s3_clk_src's table: 7.3728 MHz = GPLL0_OUT_EVEN (300 MHz) with
 *               M 384, N 15625 and a divide-by-2 pre-divider, written the way
 *               clk_rcg2_configure() writes M/N/D and CFG. The QUP wrapper's own clock gates at
 *               0x8 take 0x303 (bits 0, 1, 8, 9), the same four the touch driver found necessary
 *               for its engine (hakios.s3706.c:670-700).
 *
 *   baud        QCA's WCN3990 protocol: the ROM/firmware link starts at 115200 and is raised to
 *               3000000 after the firmware is loaded (vendor-src/bluetooth/hci_qca.c:1012-1013,
 *               .init_speed 115200, .oper_speed 3000000; mainline hci_qca.c has the same two
 *               values). The GENI baud divider is ser_clk / (baud * 32): 7.3728 MHz / (115200 *
 *               32) = 2; 7.3728 MHz / (2400 * 32) = 96. These are the values the vendor's
 *               msm_geni_serial.c writes into GENI_SER_M_CLK_CFG/S_CLK_CFG (get_clk_div_rate(),
 *               set_termios()). 115200 is the only speed this first step needs; the 3 Mbaud step
 *               waits for the firmware path.
 *
 *   power       the bt_wcn3990 node asks for three supplies, and the overlay's label table
 *               (dtbo.bin.01.dts:8163-8165) resolves them:
 *                 qca,bt-vdd-core  pm8150_l7   1800000 uV  (0x1b7740)
 *                 qca,bt-vdd-pa    pm8150l_l2  1304000 uV  (0x13e5c0)
 *                 qca,bt-vdd-ldo   pm8150l_l11 3312000 uV  (0x328980)
 *               Their RPMh resource names are the tree's own: ldoa7 (android-merged.dts:12374-
 *               12377), ldoc2 (:12787-12790) and ldoc11 (:12956-12959). All three are VRM
 *               resources: voltage in millivolts, enable at +4, mode at +8, voted through an
 *               apps_rsc TCS slot. The three current-level properties are 0 (LPM/PFM); an RPMh
 *               VRM vote has no current field, so they change nothing here. The same three
 *               rails are the WLAN's ldoa7/ldoc2/ldoc11 (notes/wifi.md), so if the SLPI driver
 *               has already voted them the vote is simply repeated.
 *
 *   reset       there is no qca,bt-reset-gpio in this phone's bt node, and there is none in the
 *               vendor's reference sm8150-mtp.dtsi either. The WCN3990 does not use a reset pin
 *               for this: its BT_EN is driven by the UART's TX line through an external decode
 *               circuit, and the host toggles it with single-byte "power pulses" at fixed baud
 *               rates -- 0xC0 at 2400 to turn the BT side off, 0xFC at 115200 to turn it on
 *               (btqca.h: QCA_WCN3990_POWEROFF_PULSE / POWERON_PULSE; hci_qca.c
 *               qca_regulator_init(), which sets 2400, sends the off pulse, sets 115200, sends
 *               the on pulse). That is what this driver does. The pinctrl group named
 *               bt_en_active (android-merged.dts:11496-11509, gpio172) and the two
 *               conn_power_* groups (gpio173/gpio174, :11812-11840) are defined but no node in
 *               the merged tree consumes them; they are not driven here. If a later measurement
 *               shows BT_EN is a real pin on this board, it is one of those three and is worth
 *               revisiting, not guessing at now.
 *
 *   firmware    WCN3990's firmware is the TLV pair qca/crbtfw21.tlv (RAM patch, <= 200 KiB) and
 *               qca/crnv21.bin (NVM, <= 10 KiB), named from the ROM version:
 *               rom_ver = ((soc_ver & 0xf00) >> 4) | (soc_ver & 0xf). On SM8150 the controller
 *               sometimes reports ROM version 0x01 and Linux distributions carry the same file
 *               as crbtfw01.tlv -> crbtfw21.tlv; both names are the same content. The files are
 *               NOT in S:\opencode\fw-wifi\ (that directory holds the WLAN SLPI image and
 *               bdwlan only) and are not in this repository. They live in the phone's
 *               /vendor/firmware (or its vendor image); pulling them is a separate step. No
 *               firmware is needed for this first milestone: the ROM answers the version request
 *               and HCI Reset itself.
 *
 * What this first step does, in order:
 *   1. reads the boot loader's command database and votes ldoa7, ldoc2 and ldoc11 at the
 *      voltages above (three TCS commands each: voltage, enable, mode auto);
 *   2. releases QUPv3 wrapper 2's reset if it is held, programs engine 3's RCG to 7.3728 MHz,
 *      clears the slave-AHB hardware gate, votes the three branches on, and opens the wrapper's
 *      own clock gates;
 *   3. muxes gpio43-46 to qup13 with the tree's pulls and drive strength;
 *   4. resets the SE to defaults, opens its clock gates, sets FIFO mode, 8-bit words, no parity,
 *      one stop bit, TX packing 8 bits into one FIFO word and RX packing 8 bits into four
 *      (the vendor console path's shape, so a one-byte command leaves no stale bytes), and
 *      the 115200 divider;
 *   5. sends 0xC0 at 2400 and 0xFC at 115200 (the WCN3990 power-on pulse);
 *   6. sends the QCA version request (HCI opcode 0xFC00, parameter 0x19) and prints product,
 *      SOC, ROM and patch versions from the vendor event;
 *   7. at wcnbt=2, sends HCI Reset (opcode 0x0C03) and prints the Command Complete status.
 *
 * This is a polled driver: no GIC interrupt, no tty, no serdev, no DMA. The GENI FIFOs are
 * enough for HCI packets at 115200. The vendor's msm_geni_serial.c uses DMA for its high-speed
 * path, but its console path is FIFO-only and is the shape copied here; the only bus the port
 * needs beyond the engine is the register window itself. If the UART FIFO turns out to be fused
 * off on this phone (the I2C FIFO is -- see notes/wifi.md and hakios.s3706.c), the alternatives
 * are the engine's DMA path (BAM/GPI, a much larger job) or bit-banging the four pins; both are
 * written down in notes/bluetooth.md rather than attempted here.
 *
 * Status: COMPILED ONLY is not even claimed -- this file has not been built in this round (the
 * round was not allowed to build) and has never run on the phone. It is written to be read
 * against the sources above and run next round with wcnbt=1.
 *
 * Boot-args:
 *   wcnbt=1   the sequence above, ending with the chip's version
 *   wcnbt=2   the same, then HCI Reset
 */
#include <kern/clock.h>
#include <kern/startup.h>
#include <kern/thread.h>
#include <libkern/libkern.h>
#include <mach/mach_types.h>
#include <pexpert/pexpert.h>
#include <sys/errno.h>
#include <sys/systm.h>

extern vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);
extern vm_offset_t ml_io_map_wcomb(vm_offset_t phys_addr, vm_size_t size);
extern kern_return_t thread_terminate(thread_t thread);
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
sleep_us(uint32_t us)
{
	delay_for_interval(us, NSEC_PER_USEC);
}

static void
sleep_ms(uint32_t ms)
{
	delay_for_interval(ms, NSEC_PER_MSEC);
}

/* ---- the boot loader's command database --------------------------------------------------------- */

#define CMD_DB_POINTER            0x00c3f000cUL         /* qcom,cmd-db@c3f000c, reg <0xc3f000c 0x08> */
#define CMD_DB_MAGIC              0x0c0330dbu
#define CMD_DB_HEADER_SIZE        148                   /* sizeof(struct cmd_db_header) in cmd-db.c */
#define CMD_DB_MAX_RES            256

struct cmd_res {
	char name[9];
	uint32_t address;
};

static struct cmd_res cmd_res_table[CMD_DB_MAX_RES];
static unsigned cmd_res_count;
static bool resources_read;

/* The table this phone's boot firmware leaves at 0x85f20000 is 4424 bytes (notes/wifi.md). It is
 * found the way the touch and SLPI drivers find it: the pointer node at 0xc3f000c holds the
 * table's address and length, and the table's magic is at +4. Only the three names below are
 * wanted, so this walk keeps its output to the ones it finds. */
static bool
id_shaped(uint32_t first, uint32_t second)
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
cmdb_read(void)
{
	vm_offset_t pointer, db;
	uint32_t base, length;

	if (resources_read) {
		return cmd_res_count != 0;
	}
	resources_read = true;
	pointer = ml_io_map(CMD_DB_POINTER, 0x1000);
	if (pointer == 0) {
		say("bt: cmd db: could not map the pointer at %#x\n", CMD_DB_POINTER);
		return false;
	}
	base = rd(pointer);
	length = rd(pointer + 4);
	if (base < 0x1000 || base + length <= base || length == 0 || length > 0x200000) {
		say("bt: cmd db: the pointer reads %#x with %u bytes, not a table\n", base, length);
		return false;
	}
	db = ml_io_map_wcomb(base, length);
	if (db == 0) {
		say("bt: cmd db: could not map the table at %#x\n", base);
		return false;
	}
	if (rd(db + 4) != CMD_DB_MAGIC) {
		say("bt: cmd db: the table at %#x has magic %#x, not the command database's\n", base,
		    rd(db + 4));
		return false;
	}
	for (unsigned at = CMD_DB_HEADER_SIZE; at + 24 <= length; at += 4) {
		uint32_t first = rd(db + at), second = rd(db + at + 4);
		uint32_t address = rd(db + at + 16);

		if (!id_shaped(first, second) || address == 0 || address >= 0x1000000) {
			continue;
		}
		if (cmd_res_count == CMD_DB_MAX_RES) {
			break;
		}
		struct cmd_res *res = &cmd_res_table[cmd_res_count++];

		for (unsigned c = 0; c < 8; c++) {
			uint32_t word = c < 4 ? first : second;

			res->name[c] = (char) ((word >> (8 * (c & 3))) & 0xff);
		}
		res->name[8] = 0;
		res->address = address;
	}
	return cmd_res_count != 0;
}

static uint32_t
cmdb_look(const char *name)
{
	for (unsigned i = 0; i < cmd_res_count; i++) {
		if (strcmp(cmd_res_table[i].name, name) == 0) {
			return cmd_res_table[i].address;
		}
	}
	return 0;
}

/* ---- the RPMh command slots --------------------------------------------------------------------- */

/* The apps_rsc controller's active-only slots, exactly as this phone's touch driver uses them
 * (hakios.s3706.c:2341-2525): two 0x2a0-wide blocks at 0x18220d00, a command in the vendor's
 * tcs_mbox_write() order, and the slot's interrupt and completed bits as its ending. */
#define RSC_TCS_PHYS              0x018220d00UL
#define RSC_TCS_SIZE              0x3000
#define DRV_TCS_STRIDE            0x2a0UL
#define DRV_CMD_STRIDE            0x14UL
#define DRV_TCS(slot, reg, cmd)   (rsc_tcs + DRV_TCS_STRIDE * (slot) + (reg) + DRV_CMD_STRIDE * (cmd))
#define DRV_CONTROL               0x14
#define DRV_STATUS                0x18
#define DRV_CMD_WAIT_FOR_CMPL     0x10
#define DRV_CMD_ENABLE            0x1c
#define DRV_CMD_MSGID             0x30
#define DRV_CMD_ADDR              0x34
#define DRV_CMD_DATA              0x38
#define DRV_CMD_STATUS            0x3c
#define RSC_IRQ_STATUS            0x04
#define RSC_IRQ_CLEAR             0x08
#define AMC_MODE_ENABLE           (1u << 16)
#define AMC_MODE_TRIGGER          (1u << 24)
#define CMD_LEN                   8
#define CMD_RESP_REQ              (1u << 8)
#define CMD_WRITE                 (1u << 16)
#define RSC_ACTIVE_SLOTS          2
#define RSC_WAIT_POLLS            2000
#define TRANSFER_POLL_US          100

static vm_offset_t rsc_tcs;

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
	say("bt: rpmh: %#lx would not take %#x (reads %#x)\n", (unsigned long) where, want, rd(where));
	return false;
}

static bool
rpmh_map(void)
{
	if (rsc_tcs != 0) {
		return true;
	}
	rsc_tcs = ml_io_map(RSC_TCS_PHYS, RSC_TCS_SIZE);
	if (rsc_tcs == 0) {
		say("bt: rpmh: could not map the tcs window at %#lx\n", (unsigned long) RSC_TCS_PHYS);
		return false;
	}
	return true;
}

static bool
tcs_send(uint32_t cell, uint32_t data)
{
	uint32_t status = 0, polls, irq;
	unsigned slot;
	bool found = false;

	if (rsc_tcs == 0) {
		return false;
	}
	for (slot = 0; slot < RSC_ACTIVE_SLOTS; slot++) {
		if (rd(DRV_TCS(slot, DRV_STATUS, 0)) != 0) {
			found = true;
			break;
		}
	}
	if (!found) {
		say("bt: rpmh: no slot has completed a transaction (both read %#x)\n",
		    rd(DRV_TCS(0, DRV_STATUS, 0)));
		return false;
	}
	wr(rsc_tcs + RSC_IRQ_CLEAR, 1u << slot);
	wr(DRV_TCS(slot, DRV_CMD_ENABLE, 0), 0);
	wr(DRV_TCS(slot, DRV_CMD_WAIT_FOR_CMPL, 0), 0);
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
	if (!(status & (1u << 16))) {
		say("bt: rpmh: cell %#x data %#x did not complete (%u polls, irq %#x, status %#x)\n",
		    cell, data, polls, irq, status);
	}
	wr(rsc_tcs + RSC_IRQ_CLEAR, 1u << slot);
	wr(DRV_TCS(slot, DRV_CMD_ENABLE, 0), 0);
	wr(DRV_TCS(slot, DRV_CMD_WAIT_FOR_CMPL, 0), 0);
	return (status & (1u << 16)) != 0;
}

/* ---- the three rails the bt_wcn3990 node asks for ------------------------------------------------ */

struct rail {
	const char *name;
	uint32_t millivolts;
	const char *what;
};

static const struct rail rails[] = {
	{ "ldoa7",  1800, "pm8150 l7, the core rail" },
	{ "ldoc2",  1304, "pm8150l l2, the PA rail" },
	{ "ldoc11", 3312, "pm8150l l11, the 3.3 V rail" },
};

static bool
rails_on(void)
{
	bool ok = true;

	if (!cmdb_read()) {
		say("bt: the command database is not readable: the rails stay as the boot left them\n");
		return false;
	}
	if (!rpmh_map()) {
		return false;
	}
	for (unsigned i = 0; i < sizeof(rails) / sizeof(rails[0]); i++) {
		uint32_t cell = cmdb_look(rails[i].name);
		bool one;

		if (cell == 0) {
			say("bt: the command database has no \"%s\": %s stays as the boot left it\n",
			    rails[i].name, rails[i].what);
			ok = false;
			continue;
		}
		one = tcs_send(cell, rails[i].millivolts);          /* the millivolts, as the vendor's
		                                                     * rpmh_regulator_vrm_set_voltage()
		                                                     * sends them */
		one = tcs_send(cell + 4, 1) && one;                 /* enable */
		one = tcs_send(cell + 8, 2) && one;                 /* auto, the tree's init-mode */
		say("bt: %s at %#x: %u mV, enabled, auto: %s (%s)\n", rails[i].name, cell,
		    rails[i].millivolts, one ? "all three votes completed" : "a vote did not complete",
		    rails[i].what);
		ok = ok && one;
	}
	return ok;
}

/* ---- the clocks (GCC, QUPv3 wrapper 2, engine 3) ------------------------------------------------ */

#define GCC_PHYS                 0x00100000UL
#define GCC_SIZE                 0x080000UL
#define GCC_QUP2_BCR             (gcc + 0x1e000)
#define BCR_RESET                (1u << 0)
#define GCC_VOTE                 (gcc + 0x52014)
#define VOTE_QUP2_S_AHB          (1u << 1)
#define VOTE_QUP2_M_AHB          (1u << 2)
#define VOTE_QUP2_SE3            (1u << 7)
#define GCC_QUP2_M_AHB_CBCR      (gcc + 0x1e004)
#define GCC_QUP2_S_AHB_CBCR      (gcc + 0x1e008)
#define HWCG_S_AHB               (1u << 1)
#define GCC_QUP2_SE3_CBCR        (gcc + 0x1e4d4)
#define GCC_QUP2_SE3_CMD         (gcc + 0x1e4d8)
#define GCC_QUP2_SE3_CFG         (gcc + 0x1e4dc)
#define GCC_QUP2_SE3_M           (gcc + 0x1e4e0)
#define GCC_QUP2_SE3_N           (gcc + 0x1e4e4)
#define GCC_QUP2_SE3_D           (gcc + 0x1e4e8)
#define CLOCK_OFF                (1u << 31)
#define CMD_UPDATE               (1u << 0)
#define CMD_ROOT_EN              (1u << 1)
#define CMD_ROOT_OFF             (1u << 31)
#define CFG_SRC_DIV_SHIFT        0
#define CFG_SRC_SEL_SHIFT        8
#define CFG_MODE_SHIFT           12
#define CFG_MODE_DUAL_EDGE       (2u << CFG_MODE_SHIFT)
#define CFG_HW_CLK_CTRL_MASK     (1u << 20)
#define CFG_KEEP                 0xffe00000u

#define QUP_SER_CLK_HZ           7372800u
#define RCG_M                    384u                    /* 300 MHz * 384 / 15625 = 7.3728 MHz */
#define RCG_N                    15625u
#define RCG_PARENT_GPLL0_EVEN    6u                      /* gcc_parent_map_0: P_GPLL0_OUT_EVEN */

static vm_offset_t gcc, qup, se, tlmm;

static bool
qup_clocks_on(void)
{
	uint32_t cmd, s_ahb;

	if (!(rd(gcc + 0x52000) & 1u)) {
		say("bt: gcc: GPLL0's enable bit reads 0; the engine's only parent would be stopped\n");
	}
	if (rd(GCC_QUP2_BCR) & BCR_RESET) {
		wr(GCC_QUP2_BCR, rd(GCC_QUP2_BCR) & ~BCR_RESET);
		say("bt: gcc: the QUPv3 wrapper was held in reset; released (%#x)\n", rd(GCC_QUP2_BCR));
	}
	/* the RCG first: M, N, D, then CFG (divider, source, dual-edge mode), then the update bit,
	 * exactly as clk_rcg2_configure() writes them. N and D are stored inverted. */
	wr(GCC_QUP2_SE3_M, RCG_M);
	wr(GCC_QUP2_SE3_N, ~(RCG_N - RCG_M) & 0xffffu);
	wr(GCC_QUP2_SE3_D, ~RCG_N & 0xffffu);
	wr(GCC_QUP2_SE3_CFG, (1u << CFG_SRC_DIV_SHIFT) | (RCG_PARENT_GPLL0_EVEN << CFG_SRC_SEL_SHIFT) |
	    CFG_MODE_DUAL_EDGE);
	wr(GCC_QUP2_SE3_CMD, rd(GCC_QUP2_SE3_CMD) | CMD_UPDATE);
	for (unsigned i = 0; i < 500; i++) {
		if (!(rd(GCC_QUP2_SE3_CMD) & CMD_UPDATE)) {
			break;
		}
		sleep_us(10);
	}
	if (rd(GCC_QUP2_SE3_CMD) & CMD_UPDATE) {
		say("bt: gcc: the engine clock does not take its configuration (cmd %#x)\n",
		    rd(GCC_QUP2_SE3_CMD));
		return false;
	}
	cmd = rd(GCC_QUP2_SE3_CMD);
	if (cmd & CMD_ROOT_OFF) {
		wr(GCC_QUP2_SE3_CMD, cmd | CMD_ROOT_EN);
		for (unsigned i = 0; i < 500; i++) {
			if (!(rd(GCC_QUP2_SE3_CMD) & CMD_ROOT_OFF)) {
				break;
			}
			sleep_us(10);
		}
		if (rd(GCC_QUP2_SE3_CMD) & CMD_ROOT_OFF) {
			say("bt: gcc: the engine clock's root stays off (cmd %#x)\n", rd(GCC_QUP2_SE3_CMD));
			return false;
		}
	}
	/* the slave AHB branch is hardware-gated between transactions; take that bit out first so
	 * the vote below is what decides (gcc-sm8150.c gives it .hwcg_reg = 0x1e008, bit 1). */
	s_ahb = rd(GCC_QUP2_S_AHB_CBCR);
	if (s_ahb & HWCG_S_AHB) {
		wr(GCC_QUP2_S_AHB_CBCR, s_ahb & ~HWCG_S_AHB);
	}
	wr(GCC_VOTE, rd(GCC_VOTE) | VOTE_QUP2_S_AHB | VOTE_QUP2_M_AHB | VOTE_QUP2_SE3);
	for (unsigned i = 0; i < 1000; i++) {
		if (!((rd(GCC_QUP2_M_AHB_CBCR) | rd(GCC_QUP2_S_AHB_CBCR) | rd(GCC_QUP2_SE3_CBCR)) &
		    CLOCK_OFF)) {
			say("bt: gcc: m-ahb, s-ahb and engine 3's branch are on; the engine's "
			    "configuration says %#x\n", rd(GCC_QUP2_SE3_CFG));
			return true;
		}
		sleep_us(10);
	}
	say("bt: gcc: clocks stay off: m-ahb %#x, s-ahb %#x, engine %#x, votes %#x\n",
	    rd(GCC_QUP2_M_AHB_CBCR), rd(GCC_QUP2_S_AHB_CBCR), rd(GCC_QUP2_SE3_CBCR), rd(GCC_VOTE));
	return false;
}

/* ---- the four UART pins (TLMM's EAST tile) ------------------------------------------------------- */

#define TLMM_EAST_PHYS           (0x03500000UL + 0x2b000UL) /* pinctrl-sm8150.c: EAST = 0x00500000,
                                                          * plus the tree's 0x03000000 base; gpio43
                                                          * sits at tile offset 0x2b000 */
#define GPIO_CFG(n)              (tlmm + 0x1000UL * ((n) - 43)) /* only gpio43..46 are touched */
/* Map four pages around the pins wanted, not a whole 3 MB tile: ml_io_map
 * allocates kernel memory for the mapping, and two tiles (6 MB) exhausted the
 * kernel map after the other drivers (logs/163026-run.log: kmem_alloc of
 * 0x300000 failed with resource shortage). */
#define TLMM_WINDOW              0x4000UL
#define FUNCTION_QUP13           1                       /* pinctrl-sm8150.c qup13_groups, index 1 */
#define PULL_NONE                0
#define PULL_DOWN                1
#define PULL_UP                  3
#define DRIVE_2MA                0                       /* (2 mA / 2) - 1 */

static uint32_t
pin_bits(unsigned function, unsigned pull, unsigned drive)
{
	return (uint32_t)function << 2 | (uint32_t)pull | (uint32_t)drive << 6;
}

/* The three pins the tree defines but no node consumes: bt_en_active (gpio172),
 * conn_power_1p8_active (gpio173) and conn_power_pa_active (gpio174), all in the NORTH tile
 * (pinctrl-sm8150.c:1856-1858; NORTH = 0x03900000). Read their state once; if the version
 * request below gets no answer, this line says whether the board left them as pull-downs or
 * whether something (the boot loader, Android's earlier boot) drove them high. Nothing is
 * written: which of them, if any, is BT_EN is not known, and driving a supply enable blind is
 * not a bring-up step. */
static void
power_pins_look(void)
{
	vm_offset_t north = ml_io_map(0x03900000UL + 0xac000UL, TLMM_WINDOW); /* gpio172 at tile +0xac000 */

	if (north == 0) {
		say("bt: the NORTH tile would not map; gpio172-174 stay unread\n");
		return;
	}
	for (unsigned pin = 172; pin <= 174; pin++) {
		vm_offset_t at = north + 0x1000UL * (pin - 172);
		uint32_t cfg = rd(at), io = rd(at + 4);

		say("bt: gpio%u: cfg %#x (function %u, pull %u, %u mA, %s), io %#x (reads %u)\n", pin,
		    cfg, cfg >> 2 & 0xf, cfg & 3, (((cfg >> 6) & 7) + 1) * 2,
		    cfg & (1u << 9) ? (io & 2 ? "drives high" : "drives low") : "input", io, io & 1);
	}
}

static bool
uart_pins(void)
{
	static const struct {
		unsigned pin;
		unsigned pull;
		const char *what;
	} pins[] = {
		{ 43, PULL_NONE, "CTS" },
		{ 44, PULL_DOWN, "RTS" },
		{ 45, PULL_UP,   "TX"  },
		{ 46, PULL_NONE, "RX"  },
	};

	for (unsigned i = 0; i < sizeof(pins) / sizeof(pins[0]); i++) {
		uint32_t want = pin_bits(FUNCTION_QUP13, pins[i].pull, DRIVE_2MA);
		uint32_t now = rd(GPIO_CFG(pins[i].pin));

		if ((now & 0x3ffu) != (want & 0x3ffu)) {
			wr(GPIO_CFG(pins[i].pin), (now & ~0x3ffu) | (want & 0x3ffu));
		}
		if ((rd(GPIO_CFG(pins[i].pin)) & 0x3ffu) != (want & 0x3ffu)) {
			say("bt: gpio%u (%s) would not take qup13 (cfg %#x)\n", pins[i].pin,
			    pins[i].what, rd(GPIO_CFG(pins[i].pin)));
			return false;
		}
	}
	say("bt: gpio43-46 are qup13: CTS, RTS, TX, RX, the tree's pulls and 2 mA\n");
	return true;
}

/* ---- the serial engine's registers -------------------------------------------------------------- */

#define QUP_PHYS                 0x00cc0000UL            /* qcom,qupv3_2_geni_se@cc0000 */
#define QUP_SIZE                 0x6000UL
#define SE_PHYS                  0x00c8c000UL            /* qcom,qup_uart@0xc8c000, engine 3 */
#define SE_SIZE                  0x4000UL

#define QUP_SE_CLK_CTL_ENABLE    (qup + 0x8)
#define CLK_CTL_ENABLE_ON        0x303u                  /* bits 0, 1, 8, 9 */

#define GENI_SER_M_CLK_CFG       (se + 0x048)
#define GENI_SER_S_CLK_CFG       (se + 0x04c)
#define SER_CLK_EN               (1u << 0)
#define CLK_DIV_SHFT             4
#define SE_GENI_STATUS           (se + 0x040)
#define M_GENI_CMD_ACTIVE        (1u << 0)
#define S_GENI_CMD_ACTIVE        (1u << 12)
#define SE_FW_REVISION           (se + 0x068)
#define SE_CLK_SEL               (se + 0x07c)
#define SE_IF_DISABLE            (se + 0x064)            /* read-only: bit 0 says the FIFO
                                                          * interface is disabled */
#define SE_FORCE_DEFAULT         (se + 0x020)
#define SE_OUTPUT_CTRL           (se + 0x024)
#define SE_CGC_CTRL              (se + 0x028)
#define SE_BYTE_GRAN             (se + 0x254)
#define SE_DMA_MODE_EN           (se + 0x258)
#define SE_TX_PACKING_CFG0       (se + 0x260)
#define SE_TX_PACKING_CFG1       (se + 0x264)
#define SE_RX_PACKING_CFG0       (se + 0x284)
#define SE_RX_PACKING_CFG1       (se + 0x288)
#define SE_M_CMD0                (se + 0x600)
#define SE_M_CMD_CTRL            (se + 0x604)
#define SE_M_IRQ_STATUS          (se + 0x610)
#define SE_M_IRQ_EN              (se + 0x614)
#define SE_M_IRQ_CLEAR           (se + 0x618)
#define SE_S_CMD0                (se + 0x630)
#define SE_S_CMD_CTRL            (se + 0x634)
#define SE_S_IRQ_STATUS          (se + 0x640)
#define SE_S_IRQ_EN              (se + 0x644)
#define SE_S_IRQ_CLEAR           (se + 0x648)
#define SE_TX_FIFO               (se + 0x700)
#define SE_RX_FIFO               (se + 0x780)
#define SE_TX_FIFO_STATUS        (se + 0x800)
#define SE_RX_FIFO_STATUS        (se + 0x804)
#define SE_TX_WATERMARK          (se + 0x80c)
#define SE_RX_WATERMARK          (se + 0x810)
#define SE_RX_RFR_WATERMARK      (se + 0x814)
#define SE_IRQ_EN                (se + 0xe1c)
#define SE_GSI_EVENT_EN          (se + 0xe18)
#define SE_HW_PARAM_0            (se + 0xe24)
#define SE_HW_PARAM_1            (se + 0xe28)
#define SE_DMA_GENERAL_CFG       (se + 0xe30)

#define SE_UART_LOOPBACK_CFG     (se + 0x22c)
#define SE_UART_TX_TRANS_CFG     (se + 0x25c)
#define SE_UART_TX_WORD_LEN      (se + 0x268)
#define SE_UART_TX_STOP_BIT_LEN  (se + 0x26c)
#define SE_UART_TX_TRANS_LEN     (se + 0x270)
#define SE_UART_RX_TRANS_CFG     (se + 0x280)
#define SE_UART_RX_WORD_LEN      (se + 0x28c)
#define SE_UART_RX_STALE_CNT     (se + 0x294)
#define SE_UART_TX_PARITY_CFG    (se + 0x2a4)
#define SE_UART_RX_PARITY_CFG    (se + 0x2a8)
#define SE_UART_MANUAL_RFR       (se + 0x2ac)

/* Mainline's newer qcom-geni-se.c has a writable FIFO-interface control at 0x2008, separate
 * from the read-only status at 0x64. The vendor's older file does not name it, but the touch
 * driver's inability to use its I2C FIFO is consistent with the boot firmware leaving this bit
 * set (the status at 0x64 is what that driver read). Clear it before trusting the status. */
#define SE_FIFO_IF_DISABLE       (se + 0x2008)
#define FIFO_IF_DISABLE          (1u << 0)

#define UART_CTS_MASK            (1u << 1)               /* ignore CTS: the first packets need no
                                                          * flow control, and the power pulses must
                                                          * not wait for it */
#define UART_START_TX            (0x1u)
#define UART_START_READ          (0x1u)
#define UART_PARAM_RFR_OPEN      (1u << 7)
#define M_OPCODE_SHFT            27
#define M_CMD_DONE_EN            (1u << 0)
#define M_TX_FIFO_WATERMARK_EN   (1u << 30)
#define S_CMD_DONE_EN            (1u << 0)
#define S_RX_FIFO_WATERMARK_EN   (1u << 26)
#define S_RX_FIFO_LAST_EN        (1u << 27)
#define M_COMMON_EN              (0x7eu | (1u << 22) | (1u << 23) | (1u << 24) | (1u << 25) | \
                                  (1u << 28) | (1u << 29))
#define S_COMMON_EN              (0x3eu | (1u << 9) | (1u << 10) | (1u << 11) | (1u << 12) | \
                                  (1u << 13) | (1u << 24) | (1u << 25))
#define DEFAULT_CGC_EN           0x7fu
#define IRQ_ROUTING_ON           0xfu
/* The console path's packing: TX is 8 bits into one FIFO word, RX is 8 bits into four. The
 * vendor's msm_geni_serial_port_setup() does exactly this for the console branch (se_get_packing
 * config(8, 1) for TX, (8, 4) for RX) and it matters for a polled sender: with four bytes packed
 * into a word, a one-byte command leaves three stale bytes in the FIFO for the next command.
 * se_get_packing_config(8, 1, false) = 0x0000000f/0; (8, 4, false) = 0x0004380e/0x000c3e0e. */
#define PACKING_TX_CFG0          0x0000000fu
#define PACKING_TX_CFG1          0x00000000u
#define PACKING_RX_CFG0          0x0004380eu
#define PACKING_RX_CFG1          0x000c3e0eu
#define RX_STALE_BITS            160u                   /* 10 bits/char * 16, the vendor's default */

#define UART_TX_POLLS            2000u                  /* 200 ms at 100 us */
#define UART_RX_POLLS            10000u                 /* 1 s at 100 us */
#define HCI_RX_MAX               260u
#define HCI_TX_MAX               260u

static bool
se_reset_and_config(uint32_t divider)
{
	uint32_t routing, rx_depth;

	/* No command from an earlier user of this engine may still be armed. */
	wr(SE_M_CMD_CTRL, 0);
	wr(SE_S_CMD_CTRL, 0);

	/* geni_se_io_init(): every clock gate, the AHB/DMA gates, the outputs handed to the
	 * engine, and a reset to defaults. Then a second pass at the gates, because the reset
	 * closes them again (the touch driver's step 6 saw exactly that). */
	wr(SE_CGC_CTRL, rd(SE_CGC_CTRL) | DEFAULT_CGC_EN);
	wr(SE_DMA_GENERAL_CFG, rd(SE_DMA_GENERAL_CFG) | 0xfu);
	wr(SE_OUTPUT_CTRL, 0x7fu);
	wr(SE_FORCE_DEFAULT, 1);
	for (unsigned i = 0; i < 100; i++) {
		if (!(rd(SE_FORCE_DEFAULT) & 1u)) {
			break;
		}
		sleep_us(10);
	}
	wr(SE_FORCE_DEFAULT, 0);
	wr(SE_CGC_CTRL, rd(SE_CGC_CTRL) | DEFAULT_CGC_EN);
	wr(SE_DMA_GENERAL_CFG, rd(SE_DMA_GENERAL_CFG) | 0xfu);
	wr(SE_OUTPUT_CTRL, 0x7fu);
	if (rd(SE_FIFO_IF_DISABLE) & FIFO_IF_DISABLE) {
		wr(SE_FIFO_IF_DISABLE, rd(SE_FIFO_IF_DISABLE) & ~FIFO_IF_DISABLE);
		sleep_us(10);
		say("bt: the FIFO interface's disable bit was set; cleared (control %#x, status %#x)\n",
		    rd(SE_FIFO_IF_DISABLE), rd(SE_IF_DISABLE));
	}

	/* The four interrupt sources routed to the wrapper; the engine's own status bits are
	 * polled, no GIC handler is installed. GSI is not this path. */
	wr(SE_GSI_EVENT_EN, 0);
	routing = rd(SE_IRQ_EN);
	wr(SE_IRQ_EN, routing | IRQ_ROUTING_ON);

	/* geni_se_init()'s watermarks: stop asking for receive bytes two words before the FIFO
	 * is full, and keep RFR open. The depth is the engine's own hardware parameter (the
	 * vendor's get_tx_fifo_size() reads the same field); 16 is the GENI UART default. */
	rx_depth = rd(SE_HW_PARAM_1) >> 16 & 0x3f;
	if (rx_depth < 4 || rx_depth > 64) {
		rx_depth = 16;
	}
	wr(SE_RX_WATERMARK, rx_depth / 2);
	wr(SE_RX_RFR_WATERMARK, rx_depth - 2);
	wr(SE_TX_WATERMARK, 0);

	/* geni_se_select_fifo_mode(): DMA off, the done and FIFO bits in M_IRQ_EN, and the
	 * secondary side's done/FIFO bits. */
	wr(SE_DMA_MODE_EN, rd(SE_DMA_MODE_EN) & ~1u);
	wr(SE_M_IRQ_EN, M_COMMON_EN | M_CMD_DONE_EN | M_TX_FIFO_WATERMARK_EN);
	wr(SE_S_IRQ_EN, S_COMMON_EN | S_CMD_DONE_EN | S_RX_FIFO_WATERMARK_EN | S_RX_FIFO_LAST_EN);
	wr(SE_CLK_SEL, 0);                       /* use the GCC clock this driver configured */
	wr(SE_BYTE_GRAN, 0);
	wr(SE_TX_PACKING_CFG0, PACKING_TX_CFG0);
	wr(SE_TX_PACKING_CFG1, PACKING_TX_CFG1);
	wr(SE_RX_PACKING_CFG0, PACKING_RX_CFG0);
	wr(SE_RX_PACKING_CFG1, PACKING_RX_CFG1);

	/* 8 data bits, no parity, one stop bit, a 16-character stale timeout, no loopback, no
	 * manual RFR, CTS ignored. */
	wr(SE_UART_LOOPBACK_CFG, 0);
	wr(SE_UART_TX_TRANS_CFG, UART_CTS_MASK);
	wr(SE_UART_TX_PARITY_CFG, 0);
	wr(SE_UART_RX_TRANS_CFG, 0);
	wr(SE_UART_RX_PARITY_CFG, 0);
	wr(SE_UART_TX_WORD_LEN, 8);
	wr(SE_UART_RX_WORD_LEN, 8);
	wr(SE_UART_TX_STOP_BIT_LEN, 0);
	wr(SE_UART_RX_STALE_CNT, RX_STALE_BITS);
	wr(SE_UART_MANUAL_RFR, 0);

	/* the divider the vendor's get_clk_div_rate() computes: ser_clk / (baud * 32). */
	wr(GENI_SER_M_CLK_CFG, SER_CLK_EN | (divider << CLK_DIV_SHFT));
	wr(GENI_SER_S_CLK_CFG, SER_CLK_EN | (divider << CLK_DIV_SHFT));

	if (((rd(SE_FW_REVISION) >> 8) & 0xff) != 2) {
		say("bt: the engine's protocol is %u, not UART (2): stopped\n",
		    rd(SE_FW_REVISION) >> 8 & 0xff);
		return false;
	}
	/* The I2C engine on this SoC reports this bit set and is bit-banged instead (notes/wifi.md).
	 * If this UART engine reports it too, the FIFO path is not available and the next step is
	 * the engine's DMA path, not a longer timeout. */
	say("bt: the engine's FIFO-interface status reads %#x (%s)\n", rd(SE_IF_DISABLE),
	    rd(SE_IF_DISABLE) & 1u ? "FIFO disabled, DMA would be needed" : "FIFO available");
	if (rd(SE_IF_DISABLE) & 1u) {
		return false;
	}
	wr(SE_M_IRQ_CLEAR, 0xffffffffu);
	wr(SE_S_IRQ_CLEAR, 0xffffffffu);
	say("bt: se: clock select %#x, m clock %#x, s clock %#x, dma mode %#x, cgc %#x\n",
	    rd(SE_CLK_SEL), rd(GENI_SER_M_CLK_CFG), rd(GENI_SER_S_CLK_CFG), rd(SE_DMA_MODE_EN),
	    rd(SE_CGC_CTRL));
	say("bt: se: hardware parameters %#x %#x: tx fifo %u words, rx fifo %u words\n",
	    rd(SE_HW_PARAM_0), rd(SE_HW_PARAM_1), rd(SE_HW_PARAM_0) >> 16 & 0x3f,
	    rd(SE_HW_PARAM_1) >> 16 & 0x3f);
	return true;
}

/* One SE transmit command, no longer than the FIFO. A packet longer than the FIFO is sent as
 * several commands: the UART sees one continuous byte stream, so the controller's H4 parser does
 * not care where the command boundaries fall. */
static bool
uart_tx_chunk(const uint8_t *data, uint32_t length, const char **why)
{
	uint32_t status;

	if (length == 0 || length > 64) {
		*why = "the chunk has an impossible length";
		return false;
	}
	for (unsigned i = 0; i < UART_TX_POLLS; i++) {
		if (!(rd(SE_GENI_STATUS) & M_GENI_CMD_ACTIVE)) {
			break;
		}
		sleep_us(100);
	}
	if (rd(SE_GENI_STATUS) & M_GENI_CMD_ACTIVE) {
		*why = "the previous transmit command never finished";
		return false;
	}
	wr(SE_M_IRQ_CLEAR, 0xffffffffu);
	wr(SE_TX_WATERMARK, 2);                  /* the vendor's DEF_TX_WM */
	wr(SE_UART_TX_TRANS_LEN, length);
	wr(SE_M_CMD0, UART_START_TX << M_OPCODE_SHFT);
	for (unsigned i = 0; i < UART_TX_POLLS; i++) {
		if (rd(SE_M_IRQ_STATUS) & M_TX_FIFO_WATERMARK_EN) {
			break;
		}
		sleep_us(100);
	}
	if (!(rd(SE_M_IRQ_STATUS) & M_TX_FIFO_WATERMARK_EN)) {
		*why = "the transmitter never asked for the first word";
		return false;
	}
	/* TX packing is 8 bits into one FIFO word, so every byte is one write and no stale
	 * bytes can be left behind for the next command. */
	for (uint32_t sent = 0; sent < length; sent++) {
		wr(SE_TX_FIFO, data[sent]);
	}
	wr(SE_M_IRQ_CLEAR, M_TX_FIFO_WATERMARK_EN);
	for (unsigned i = 0; i < UART_TX_POLLS; i++) {
		status = rd(SE_M_IRQ_STATUS);
		if (status & (M_CMD_DONE_EN | 0x3eu)) {
			break;
		}
		sleep_us(100);
	}
	status = rd(SE_M_IRQ_STATUS);
	wr(SE_M_IRQ_CLEAR, status);
	wr(SE_TX_WATERMARK, 0);
	if ((status & 0x3eu) || !(status & M_CMD_DONE_EN)) {
		say("bt: transmit status %#x (engine %#x, tx fifo %#x, rx fifo %#x, protocol %u)\n",
		    status, rd(SE_GENI_STATUS), rd(SE_TX_FIFO_STATUS), rd(SE_RX_FIFO_STATUS),
		    rd(SE_FW_REVISION) >> 8 & 0xff);
		*why = "the transmit command did not complete";
		return false;
	}
	return true;
}

static bool
uart_send(const uint8_t *data, uint32_t length, const char **why)
{
	uint32_t tx_depth = rd(SE_HW_PARAM_0) >> 16 & 0x3f;

	if (length == 0 || length > HCI_TX_MAX) {
		*why = "the packet has an impossible length";
		return false;
	}
	if (tx_depth < 2 || tx_depth > 64) {
		tx_depth = 16;
	}
	for (uint32_t sent = 0; sent < length; ) {
		uint32_t chunk = length - sent;

		if (chunk > tx_depth) {
			chunk = tx_depth;
		}
		if (!uart_tx_chunk(data + sent, chunk, why)) {
			return false;
		}
		sent += chunk;
	}
	return true;
}

/* ---- H4 receive --------------------------------------------------------------------------------- */

static struct {
	uint8_t bytes[HCI_RX_MAX];
	unsigned head, tail;
} rx_ring;

static void
rx_ring_put(uint8_t byte)
{
	if (rx_ring.tail - rx_ring.head < sizeof(rx_ring.bytes)) {
		rx_ring.bytes[rx_ring.tail++ % sizeof(rx_ring.bytes)] = byte;
	}
}

static bool
rx_ring_get(uint8_t *byte)
{
	if (rx_ring.head == rx_ring.tail) {
		return false;
	}
	*byte = rx_ring.bytes[rx_ring.head++ % sizeof(rx_ring.bytes)];
	return true;
}

/* Take every whole word the RX FIFO holds into the ring. The last word's valid-byte count is
 * in [30:28] of the status when bit 31 is set; 0 there means all four bytes are valid, which is
 * the vendor's bus_drain() rule. */
static void
rx_pump(void)
{
	uint32_t status = rd(SE_RX_FIFO_STATUS);
	uint32_t words = status & 0x01ffffffu;
	uint32_t last_valid = (status >> 28) & 7u;

	if (words > 64) {
		words = 64;
	}
	for (uint32_t i = 0; i < words; i++) {
		uint32_t word = rd(SE_RX_FIFO);
		unsigned count = 4;

		if ((status & 0x80000000u) && i + 1 == words && last_valid != 0) {
			count = last_valid;
		}
		for (unsigned j = 0; j < count; j++) {
			rx_ring_put((uint8_t) (word >> (8 * j)));
		}
	}
	{
		uint32_t s_irq = rd(SE_S_IRQ_STATUS);

		if (s_irq != 0) {
			wr(SE_S_IRQ_CLEAR, s_irq);      /* polled: acknowledge whatever the engine set */
		}
	}
}

static bool
wcnbt_getc(uint8_t *byte, uint32_t polls)
{
	for (uint32_t i = 0; i < polls; i++) {
		rx_pump();
		if (rx_ring_get(byte)) {
			return true;
		}
		sleep_us(100);
	}
	return false;
}

static void
uart_rx_start(void)
{
	rx_ring.head = rx_ring.tail = 0;
	wr(SE_S_IRQ_CLEAR, 0xffffffffu);
	wr(SE_S_CMD0, (UART_START_READ << M_OPCODE_SHFT) | UART_PARAM_RFR_OPEN);
}

static void
uart_rx_flush(void)
{
	rx_ring.head = rx_ring.tail = 0;
	rx_pump();
	rx_ring.head = rx_ring.tail = 0;
}

/* The read command is re-armed at each session boundary (the ROM answers, then the patch runs):
 * a frame that was cut short leaves the SE's read command pointing into the middle of one. */
static void
rx_resync(void)
{
	rx_pump();
	rx_ring.head = rx_ring.tail = 0;
	uart_rx_start();
	rx_pump();
	rx_ring.head = rx_ring.tail = 0;
}

/* Throw away every complete H4 frame the ring holds. The ROM answers each vendor command with a
 * Command Complete, and the patch header's skip mode only stops the *patched* firmware from doing
 * it: a 729-segment download would otherwise bury the one acknowledgement that matters. */
static void
rx_discard(void)
{
	uint8_t type, code, plen;
	unsigned start;

	rx_pump();
	for (;;) {
		start = rx_ring.head;
		if (!rx_ring_get(&type)) {
			break;
		}
		if (type != 0x04) {        /* HCI_EVENT_PKT, defined below with the rest of HCI */
			continue;
		}
		if (!rx_ring_get(&code) || !rx_ring_get(&plen)) {
			rx_ring.head = start;  /* an unfinished frame stays for the next read */
			break;
		}
		bool whole = true;

		for (unsigned i = 0; i < plen; i++) {
			if (!rx_ring_get(&type)) {
				whole = false;
				break;
			}
		}
		if (!whole) {
			rx_ring.head = start;
			break;
		}
	}
}

/* ---- HCI ---------------------------------------------------------------------------------------- */

#define HCI_COMMAND_PKT          0x01
#define HCI_EVENT_PKT            0x04
#define HCI_EV_CMD_COMPLETE      0x0e
#define HCI_EV_VENDOR           0xff
#define HCI_OP_RESET             0x0c03
#define QCA_OP_PATCH             0xfc00
#define QCA_PATCH_VER_REQ        0x19
#define QCA_APP_VER_RES_EVT      0x02

static bool
hci_send(uint16_t opcode, const uint8_t *params, uint8_t plen, const char **why)
{
	uint8_t packet[HCI_TX_MAX];
	unsigned length = 0;

	packet[length++] = HCI_COMMAND_PKT;
	packet[length++] = opcode & 0xff;
	packet[length++] = opcode >> 8;
	packet[length++] = plen;
	for (unsigned i = 0; i < plen; i++) {
		packet[length++] = params[i];
	}
	return uart_send(packet, length, why);
}

/* One H4 packet, which must be an HCI event of the wanted code. Command Status (0x0f) packets
 * are reported and skipped; a vendor event carries its own two-byte EDL header in the payload. */
static bool
hci_wait_event(uint8_t want_code, uint8_t *payload, uint32_t capacity, uint32_t *length,
    uint32_t polls, const char **why)
{
	uint8_t type, code, plen;

	for (unsigned frames = 0; frames < 64; frames++) {
		if (!wcnbt_getc(&type, polls)) {
			*why = "no H4 byte arrived";
			return false;
		}
		if (type != HCI_EVENT_PKT) {
			say("bt: H4 type %#x while waiting for event %#x, skipped\n", type, want_code);
			continue;
		}
		if (!wcnbt_getc(&code, polls) || !wcnbt_getc(&plen, polls)) {
			*why = "an event header did not finish";
			return false;
		}
		if (plen > capacity) {
			say("bt: event %#x is %u bytes, larger than the %u-byte buffer\n", code, plen,
			    capacity);
			*why = "an event was larger than the buffer";
			return false;
		}
		for (unsigned i = 0; i < plen; i++) {
			if (!wcnbt_getc(&payload[i], polls)) {
				*why = "an event payload did not finish";
				return false;
			}
		}
		if (code == want_code) {
			*length = plen;
			return true;
		}
		say("bt: event %#x (%u bytes) while waiting for %#x, skipped\n", code, plen, want_code);
	}
	*why = "too many unrelated events";
	return false;
}

static void
read_version(void)
{
	uint8_t params[1] = { QCA_PATCH_VER_REQ };
	uint8_t payload[64];
	uint32_t length = 0;
	const char *why = "";

	if (!hci_send(QCA_OP_PATCH, params, (uint8_t) sizeof(params), &why)) {
		say("bt: could not send the QCA version request: %s\n", why);
		return;
	}
	if (!hci_wait_event(HCI_EV_VENDOR, payload, (uint32_t) sizeof(payload), &length, UART_RX_POLLS,
	    &why)) {
		say("bt: no QCA version event: %s\n", why);
		return;
	}
	if (length < 14 || payload[0] != 0x00 || payload[1] != QCA_APP_VER_RES_EVT) {
		say("bt: the version event is %u bytes, [%#x %#x], not the 14-byte EDL answer\n", length,
		    length > 0 ? payload[0] : 0, length > 1 ? payload[1] : 0);
		return;
	}
	uint32_t product = payload[2] | payload[3] << 8 | payload[4] << 16 | payload[5] << 24;
	uint16_t patch = payload[6] | payload[7] << 8;
	uint16_t rom = payload[8] | payload[9] << 8;
	uint32_t soc = payload[10] | payload[11] << 8 | payload[12] << 16 | payload[13] << 24;
	/* The driver's own soc_ver is (soc_id << 16) | rom_ver, so the four bits that name the
	 * firmware are in the ROM field, not in the SOC id (mainline btqca.c: qca_uart_setup()). */
	uint32_t rom_ver = ((rom & 0xf00) >> 4) | (rom & 0xf);

	say("bt: THE WCN3990 ANSWERED: product %#x, soc %#x, rom %#x, patch %#x\n", product, soc,
	    rom, patch);
	say("bt: its firmware, when it is pulled, is qca/crbtfw%02x.tlv and qca/crnv%02x.bin "
	    "(rom version %#x; SM8150 parts that report 0x01 use the same crbtfw21/crnv21 files)\n",
	    rom_ver, rom_ver, rom_ver);
}

/* ---- the firmware: the TLV patch and the NVM (mainline btqca.c) ------------------------------------ */

/* A segment is [0x1e, length, bytes...] inside an HCI command of opcode 0xfc00. The patch header's
 * download_mode (3, QCA_SKIP_EVT_VSE_CC) means the controller answers nothing until the last
 * segment; the NVM has no mode byte and every segment is answered. Answers are vendor events with
 * the two-byte EDL header (cresp 0, rtype 4) and a one-byte result. */
#define QCA_PATCH_TLV_REQ        0x1e
#define QCA_TLV_DNLD_RES_EVT     0x04
#define QCA_TLV_SEGMENT          243

static const uint8_t *
bt_fw_file(const char *name, uint64_t *size)
{
	extern const uint8_t *hakios_image_file(const char *dir, const char *name, uint64_t *size,
	    int *disk_out);

	return hakios_image_file("fw", name, size, NULL);
}

static bool
qca_tlv_segment(const uint8_t *data, uint32_t length, bool answered)
{
	uint8_t params[QCA_TLV_SEGMENT + 2];
	uint8_t payload[16];
	uint32_t got = 0;
	const char *why = "";

	if (length > QCA_TLV_SEGMENT) {
		return false;
	}
	params[0] = QCA_PATCH_TLV_REQ;
	params[1] = (uint8_t) length;
	for (uint32_t i = 0; i < length; i++) {
		params[2 + i] = data[i];
	}
	if (!hci_send(QCA_OP_PATCH, params, (uint8_t) (length + 2), &why)) {
		say("bt: a %u-byte TLV segment did not go: %s\n", length, why);
		return false;
	}
	if (!answered) {
		rx_discard();
		return true;
	}
	if (!hci_wait_event(HCI_EV_VENDOR, payload, (uint32_t) sizeof(payload), &got, UART_RX_POLLS,
	    &why)) {
		say("bt: no TLV acknowledgement: %s\n", why);
		return false;
	}
	if (got < 3 || payload[0] != 0x00 || payload[1] != QCA_TLV_DNLD_RES_EVT) {
		say("bt: the TLV answer is %u bytes [%#x %#x], not the download result\n", got,
		    got > 0 ? payload[0] : 0, got > 1 ? payload[1] : 0);
		return false;
	}
	if (payload[2] != 0) {
		say("bt: the controller refused that segment: result %#x\n", payload[2]);
		return false;
	}
	return true;
}

static bool
qca_download(const char *name, uint64_t expected_type, bool answered_each, uint32_t *sent_out)
{
	uint64_t size = 0, remain, done = 0;
	const uint8_t *file = bt_fw_file(name, &size), *cursor;
	unsigned segments = 0, answered = 0;

	if (file == NULL || size < 8) {
		say("bt: %s is not in the image's /fw (the build stages ../fw-bt there)\n", name);
		return false;
	}
	{
		uint32_t header = (uint32_t) file[0] | (uint32_t) file[1] << 8 |
		    (uint32_t) file[2] << 16 | (uint32_t) file[3] << 24;
		uint32_t type = header & 0xff, length = header >> 8;

		if (type != expected_type || length != size - 4) {
			say("bt: %s has TLV type %u and %u bytes, not type %llu and %llu\n", name, type,
			    length, (unsigned long long) expected_type,
			    (unsigned long long) (size - 4));
			return false;
		}
	}
	cursor = file;
	remain = size;
	while (remain > 0) {
		uint32_t length = remain > QCA_TLV_SEGMENT ? QCA_TLV_SEGMENT : (uint32_t) remain;
		bool answered_segment = answered_each || remain <= QCA_TLV_SEGMENT;

		remain -= length;
		segments++;
		if (!qca_tlv_segment(cursor, length, answered_segment)) {
			return false;
		}
		if (answered_segment) {
			answered++;
		}
		cursor += length;
		done += length;
		if ((segments % 128) == 0) {
			say("bt: %s: %llu of %llu bytes sent\n", name, (unsigned long long) done,
			    (unsigned long long) size);
		}
	}
	say("bt: %s: %llu bytes in %u segments, %u acknowledged\n", name, (unsigned long long) size,
	    segments, answered);
	*sent_out = (uint32_t) size;
	return true;
}

/* The NVM's own tags say what the controller does once it is applied. Tag 17 (EDL_TAG_ID_HCI)
 * carries the UART baud code in its third data byte, and this phone's crnv21.bin has 0x11 there,
 * which is QCA_BAUDRATE_3200000: the controller switches to 3.2 Mbaud as soon as the NVM takes,
 * and this port's UART stays at 115200 -- that silence is exactly what the earlier rounds saw.
 * The vendor overwrites that byte with the host's rate before sending (btqca.c, case
 * EDL_TAG_ID_HCI); 0 is QCA_BAUDRATE_115200. Tag headers here are 12 bytes, not the 4 the newer
 * upstream uses: the vendor's own walk advances by 2 + 2 + 8 + tag_len. */
#define QCA_TAG_HEADER      12
#define QCA_TAG_HCI         17

static uint8_t nvm_buffer[0x2000];

static uint32_t
qca_nvm_prepare(void)
{
	uint64_t size = 0;
	const uint8_t *file = bt_fw_file("crnv21.bin", &size);
	uint64_t at, end;
	unsigned tags = 0, patched = 0;

	if (file == NULL || size < 32 || size > sizeof(nvm_buffer)) {
		say("bt: crnv21.bin is not in the image's /fw, or is larger than the buffer\n");
		return 0;
	}
	for (uint64_t i = 0; i < size; i++) {
		nvm_buffer[i] = file[i];
	}
	/* the outer type 4 and the inner type 2 headers, then the tags */
	at = 8;
	end = size;
	while (at + QCA_TAG_HEADER <= end && tags++ < 256) {
		uint16_t tag = (uint16_t) (nvm_buffer[at] | nvm_buffer[at + 1] << 8);
		uint16_t length = (uint16_t) (nvm_buffer[at + 2] | nvm_buffer[at + 3] << 8);

		if (at + QCA_TAG_HEADER + length > end) {
			break;
		}
		if (tag == QCA_TAG_HCI && length >= 3) {
			uint8_t *baud = &nvm_buffer[at + QCA_TAG_HEADER + 2];

			say("bt: NVM: the HCI tag asks for baud code %#02x; writing 0 (115200) so the UART "
			    "stays where it is\n", *baud);
			*baud = 0;
			patched++;
		}
		at += QCA_TAG_HEADER + length;
	}
	say("bt: NVM prepared: %llu bytes, %u tag(s) walked, %u changed\n", (unsigned long long) size,
	    tags, patched);
	return patched != 0 ? (uint32_t) size : 0;
}

static bool
qca_send_nvm(uint32_t size)
{
	uint32_t remain = size, done = 0;
	const uint8_t *cursor = nvm_buffer;
	unsigned segments = 0, answered = 0;

	while (remain > 0) {
		uint32_t length = remain > QCA_TLV_SEGMENT ? QCA_TLV_SEGMENT : remain;
		/* the NVM has no download-mode byte: every segment is acknowledged (mainline's
		 * qca_tlv_check_data leaves dnld_mode at QCA_SKIP_EVT_NONE for TLV_TYPE_NVM) */
		bool answered_segment = true;

		remain -= length;
		segments++;
		if (!qca_tlv_segment(cursor, length, answered_segment)) {
			return false;
		}
		if (answered_segment) {
			answered++;
		}
		cursor += length;
		done += length;
	}
	say("bt: crnv21.bin: %u bytes in %u segments, %u acknowledged\n", done, segments, answered);
	return true;
}

/* The patch file names the part it belongs to at offset 4+12 (tlv_type_patch.product_id) and its
 * ROM build at the next two bytes; the product must be the one the version request reported. */
static bool
qca_patch_product_matches(uint16_t product)
{
	uint64_t size = 0;
	const uint8_t *file = bt_fw_file("crbtfw21.tlv", &size);
	uint16_t file_product, rom_build;

	if (file == NULL || size < 20) {
		return false;
	}
	file_product = (uint16_t) (file[16] | file[17] << 8);
	rom_build = (uint16_t) (file[18] | file[19] << 8);
	say("bt: crbtfw21.tlv: product %#04x rom build %#04x (this controller: product %#04x)\n",
	    file_product, rom_build, product);
	return file_product == product;
}

static void
download_firmware(bool with_nvm)
{
	uint32_t sent = 0;

	if (!qca_patch_product_matches(0x0a)) {
		say("bt: the patch is not for this controller; nothing is downloaded\n");
		return;
	}
	if (!qca_download("crbtfw21.tlv", 1, false, &sent)) {
		say("bt: the patch download stopped after %u bytes\n", sent);
		return;
	}
	say("bt: the patch is in; the next version answer should carry its build\n");
	sleep_ms(20);
	rx_discard();
	if (!with_nvm) {
		say("bt: the NVM is not sent in this round (wcnbt=4): the patch alone is what is tested\n");
		return;
	}
	{
		uint32_t nvm = qca_nvm_prepare();

		if (nvm == 0 || !qca_send_nvm(nvm)) {
			say("bt: the NVM download stopped (the patch is already running)\n");
		}
	}
	sleep_ms(100);
	rx_discard();
}

static void
send_reset(void)
{
	uint8_t payload[16];
	uint32_t length = 0;
	const char *why = "";

	if (!hci_send(HCI_OP_RESET, NULL, 0, &why)) {
		say("bt: could not send HCI Reset: %s\n", why);
		return;
	}
	/* A previous vendor request can leave a Command Complete queued.
	 * Only the opcode matching Reset proves that Reset was executed. */
	for (unsigned events = 0; events < 8; events++) {
		if (!hci_wait_event(HCI_EV_CMD_COMPLETE, payload, (uint32_t) sizeof(payload), &length,
		    30000u, &why)) {
			say("bt: no matching HCI Reset Command Complete: %s\n", why);
			return;
		}
		if (length < 4) {
			say("bt: Command Complete has %u bytes, too short for Reset\n", length);
			return;
		}
		uint16_t opcode = (uint16_t)(payload[1] | payload[2] << 8);

		if (opcode != HCI_OP_RESET) {
			say("bt: pending Command Complete: opcode %#x status %#x, still waiting for Reset %#x\n",
			    opcode, payload[3], HCI_OP_RESET);
			continue;
		}
		say("bt: HCI Reset: %s (num_cmds %u, opcode %#x, status %#x)\n",
		    payload[3] == 0 ? "matched success" : "matched rejection", payload[0], opcode, payload[3]);
		return;
	}
	say("bt: HCI Reset not verified: too many unrelated Command Complete events\n");
}

/* ---- the thread and the entry point -------------------------------------------------------------- */

static uint32_t bt_level;

static void
bt_thread(__unused void *parameter, __unused wait_result_t result)
{
	const char *why = "";

	/* Let the rest of the boot settle; nothing here is needed by the system. */
	sleep_ms(3000);
	if (!rails_on()) {
		say("bt: the rails are not all voted; the sequence continues, because the boot "
		    "firmware may already have left them on\n");
	}

	gcc = ml_io_map(GCC_PHYS, GCC_SIZE);
	qup = ml_io_map(QUP_PHYS, QUP_SIZE);
	se = ml_io_map(SE_PHYS, SE_SIZE);
	tlmm = ml_io_map(TLMM_EAST_PHYS, TLMM_WINDOW);
	if (gcc == 0 || qup == 0 || se == 0 || tlmm == 0) {
		say("bt: could not map one of gcc %#lx qup %#lx se %#lx tlmm %#lx\n",
		    (unsigned long) gcc, (unsigned long) qup, (unsigned long) se, (unsigned long) tlmm);
		thread_terminate(current_thread());
		return;
	}
	if (!qup_clocks_on() || !uart_pins()) {
		thread_terminate(current_thread());
		return;
	}
	power_pins_look();
	wr(QUP_SE_CLK_CTL_ENABLE, rd(QUP_SE_CLK_CTL_ENABLE) | CLK_CTL_ENABLE_ON);
	say("bt: the wrapper's clock gates read %#x (asked for %#x)\n",
	    rd(QUP_SE_CLK_CTL_ENABLE), CLK_CTL_ENABLE_ON);

	/* 2400 baud: 7.3728 MHz / (2400 * 32) = 96. */
	if (!se_reset_and_config(96)) {
		thread_terminate(current_thread());
		return;
	}
	if (!uart_send((const uint8_t[]){ 0xc0 }, 1, &why)) {
		say("bt: the 2400-baud power-off pulse did not go: %s\n", why);
		thread_terminate(current_thread());
		return;
	}
	sleep_ms(20);

	/* 115200 baud: 7.3728 MHz / (115200 * 32) = 2. This is also the speed at which the
	 * controller answers the version request. */
	wr(GENI_SER_M_CLK_CFG, SER_CLK_EN | (2u << CLK_DIV_SHFT));
	wr(GENI_SER_S_CLK_CFG, SER_CLK_EN | (2u << CLK_DIV_SHFT));
	if (!uart_send((const uint8_t[]){ 0xfc }, 1, &why)) {
		say("bt: the 115200-baud power-on pulse did not go: %s\n", why);
		thread_terminate(current_thread());
		return;
	}
	sleep_ms(100);
	uart_rx_start();
	uart_rx_flush();
	say("bt: the WCN3990 is powered and the UART is open at 115200 (polled GENI FIFO)\n");

	read_version();
	if (bt_level >= 3) {
		download_firmware(bt_level < 4);
		sleep_ms(200);
		rx_resync();
		say("bt: after the download, the version request comes first: it is answered even when Reset is not\n");
		read_version();
	}
	if (bt_level >= 2) {
		rx_resync();
		sleep_ms(100);
		send_reset();
	}
	say("bt: done; the next step is scanning and pairing (notes/bluetooth.md)\n");
	thread_terminate(current_thread());
}

__startup_func
static void
wcnbt_init(void)
{
	thread_t thread;

	if (!PE_parse_boot_argn("wcnbt", &bt_level, sizeof(bt_level)) || bt_level == 0) {
		return;                                /* nothing is touched unless asked */
	}
	if (kernel_thread_start(bt_thread, NULL, &thread) != KERN_SUCCESS) {
		say("bt: no thread: nothing will be done\n");
		return;
	}
	thread_deallocate(thread);
}
STARTUP(EARLY_BOOT, STARTUP_RANK_LAST, wcnbt_init);
