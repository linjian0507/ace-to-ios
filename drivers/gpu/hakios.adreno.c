/*
 * The GPU of the POCO X5 Pro (redwood), an Adreno 642L, woken step by step up to a picture that
 * it fills and the panel shows. Every address below belongs to this phone, and nothing here is
 * touched unless a boot-arg asks for it.
 *
 * Boot-arg gpu=1, on the phone and with the GPU's firmware in the small image: the GPU is
 * started while the system starts and kept for the window server to draw with (the service
 * HakiosGPU; "the GPU kept on" below). Should a step fail, one line says which, and the system
 * draws in software as it does without the boot-arg.
 *
 * The steps are how this was found out, and they stay for looking: how much of the GPU a kernel
 * under Qualcomm's hypervisor may reach was not known, and an access the hypervisor does not
 * like ends the run, at once or a quarter of a minute later, and often without a word. adreno=N
 * performs steps 1 to N and stops; a step says what it is about to touch before it touches it
 * and what it found after, reads come before writes, and a step that does not find what it
 * needs ends the sequence instead of going on blind.
 *
 *   1  reads   what feeds the GPU from outside: its clocks in the global clock controller, and
 *              the names RPMh knows its supplies by
 *   2  reads   the fuses that say which GPU this is
 *   3  reads   the GPU's own clock controller, as the boot loader left it
 *   4  writes  the "CX" power switch on: the half of the GPU block that is not the 3D core --
 *              the power management processor ("GMU"), the bus interface, the GPU's SMMU
 *   5  writes  the clocks of that half on, and measures with the SoC's frequency counter whether
 *              they run: the control register of one of them does not say
 *   6  reads   the first words from the GPU block itself: registers of the GMU (among them a
 *              counter that should be seen counting) and of the bus interface
 *   7  reads   the GMU's own channel to RPMh
 *   8  reads   the SMMU in front of the GPU: its ID registers, and which stream goes where
 *   9  reads   that SMMU's first translation context
 *  10  writes  two words into the GMU's memory and reads them back: the way its firmware goes
 *  11  asks    TrustZone questions that pass no memory: whether it takes calls from this kernel,
 *              and which firmware images it says it authenticates
 *  12  asks    TrustZone to check the signature of the GPU's "zap shader", the signed image
 *              without which the 3D core stays locked in secure mode: its header goes from the
 *              small image (fw/a640_zap.mdt) into memory of ours that TrustZone is pointed at
 *  13  writes  page tables for the memory the GMU's firmware asks for, and the SMMU: a
 *              translation context with those tables, and the GMU's requests sent through it
 *  14  writes  the GMU's firmware (fw/a640_gmu.bin) into its own memory and into that memory
 *  15  writes  the GMU's configuration, and lets its processor out of reset: the firmware runs
 *  16  writes  the GMU's connection to RPMh; starts its message queues ("HFI")
 *  17  talks   to the GMU: the tables it needs, and the request to power the 3D core, whose
 *              registers are then read
 *  18  writes  the 3D core's translation context, with its ring and its microcode
 *              (fw/a630_sqe.fw) in it
 *  19  asks    TrustZone to load the zap shader (and first to give up the one a kernel before
 *              this one may have left; the stop at the end of a run asks for that too)
 *  20  writes  the 3D core's registers; starts its command processor and gives it its first
 *              command
 *  21  commands the 3D core out of secure mode, and to write a word into a page of ours
 *  22  commands a picture filled with three colours, and shows it on the panel
 * When the steps are over, what they started is stopped again, in reverse, and the CX half
 * switched off: nothing of the GPU block outlives the run (its SMMU contexts point into this
 * kernel's memory), and the next one starts from what the phone's own start leaves.
 *
 * The firmware is the phone's own, from /vendor/firmware of its Android. There is no register that holds the chip ID (0x06030500,
 * "qcom,chipid" in the device tree): the fuses of step 2 are all the hardware says about
 * itself, and the GMU's version is a property of its firmware.
 *
 * More boot-args:
 *   adreno_only=K      step K alone (what steps 6 to 10 need, they check for themselves: within
 *                      one session of the loader, power and clocks stay as an earlier run left them)
 *   adreno_skip=MASK   leave out the steps whose bits are set (bit 2 = step 2)
 *   adreno_pause=MS    how long the newest step waits after announcing a group of accesses
 *                      (default 2200). The loader, watching from another core (bootcpu=N), writes
 *                      the end of our log to storage every two seconds, and after a silent reset
 *                      that note is all there is.
 *   adreno_fine=1      the newest step waits like that before every single access
 *   adreno_keep=1      with adreno=21: the GPU is not stopped again but kept for the system to draw
 *                      with, through the service HakiosGPU (see "the GPU kept on" below); it is
 *                      given back when the run ends (adreno_dirty=1: left as a panic leaves it)
 *   adreno_speed=N     step 17 asks the GMU for the 3D core's Nth speed: 1 315 MHz, 2 450 MHz,
 *                      3 550 MHz (the default: the most this phone's GPU is rated for)
 *   adreno_blind=1     step 20 starts the command processor with its microcode nowhere, and
 *   adreno_stream1=N   step 18 leaves the core's second stream out (0) or gives it an empty
 *                      context of its own (2): what the SMMU then records shows what the core
 *                      asks for. With 0 and the microcode in place the phone resets.
 *   adreno_watch=S     after the last step, say once a second for S seconds (default 20) that
 *                      the machine is still alive, and what was written last
 *   adreno_return=1    when all is over, go back to the loader instead of starting the system
 *   display_probe=1    before any step: read what the firmware left the display controller
 *                      doing (display_probe() below; two clocks are turned on, nothing else written)
 * The system does not go on starting until all of this is over, so that the end of the log is
 * ours; the touch screen driver's start would talk across it (leave touchscreen=1 out).
 *
 * Addresses are from the phone's own device tree ("dt:" below; the nodes qcom,kgsl-3d0@3d00000,
 * qcom,gmu@3d69000, clock-controller@3d90000, qcom,gdsc@3d9106c, kgsl-smmu@3da0000,
 * qfprom@780000), register layouts from Linux, where SC7280 is this SoC: drivers/clk/qcom/
 * {gcc,gpucc}-sc7280.c, gdsc.c, clk-rcg2.c, drivers/gpu/drm/msm/adreno/a6xx_gmu.c with
 * registers/adreno/a6xx{,_gmu}.xml, drivers/iommu/arm/arm-smmu/arm-smmu.h, drivers/soc/qcom/
 * cmd-db.c, drivers/firmware/qcom/qcom_scm{,-smc}.c, drivers/soc/qcom/mdt_loader.c. The phone's
 * vendor kernel agrees on every offset used here, and is the only source for the frequency counter.
 */
#include <kern/clock.h>
#include <libkern/libkern.h>
#include <mach/mach_types.h>
#include <mach/vm_param.h>
#include <pexpert/pexpert.h>
#include <sys/hakios.input.h>
#include <sys/stat.h>
#include <sys/systm.h>
#include <vm/vm_kern.h>

extern vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);
extern vm_offset_t ml_io_map_wcomb(vm_offset_t phys_addr, vm_size_t size);
extern uint64_t kvtophys(vm_offset_t va);       /* pmap_paddr_t */

static uint32_t last_step, skip_steps, pause_ms = 2200, fine, watch_seconds = 20;
static uint32_t step;                   /* the one being performed */
static uint32_t speed_bin;              /* which of the GPUs of this kind the fuses say this is (step 2) */
static uint32_t newest_step;            /* the highest one asked for: the one that has not been seen to work */
static char last_write[96] = "nothing written";
static bool quiet;                      /* repeating accesses that have been announced and seen to work: say nothing */
uint32_t gpu_log;                       /* opt-in per-submission diagnostics, also used by HakiosGPU */

/*
 * The kernel's log reaches the loader when a run is over, and a run that ends in a reset
 * without a word leaves none. The loader also reads a log of the kernel's from another core
 * while it runs (loader_return.c; xl keeps it with XL_FOLLOW): every line said here goes there
 * as well, before the next register is touched.
 */
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

/* ---- register access ------------------------------------------------------------------------------- */

/*
 * One access is one plain load or store of a general register. Some of what is touched here (the
 * SMMU at least) is not the hardware but the hypervisor's imitation of it: the access traps and
 * is carried out on our behalf, which works for the simplest instruction forms only, and the
 * vendor's SMMU driver warns that this imitation "goes horribly wrong" for a store of the zero
 * register. So the compiler does not get to choose the instruction.
 */
static uint32_t __attribute__((noinline))
rd(vm_offset_t address)
{
	uint32_t value;

	__asm__ volatile ("ldr %w0, [%1]" : "=r" (value) : "r" (address) : "memory");
	return value;
}

static uint32_t __attribute__((noinline))
rd8(vm_offset_t address)
{
	uint32_t value;

	__asm__ volatile ("ldrb %w0, [%1]" : "=r" (value) : "r" (address) : "memory");
	return value;
}

static void __attribute__((noinline))
wr(vm_offset_t address, uint32_t value)
{
	__asm__ volatile ("str %w0, [%1]" : : "r" (value), "r" (address) : "memory");
}

/* A stretch of registers; mapped when a step first needs it. */
struct block {
	const char *name;
	uint32_t phys, size;
	vm_offset_t base;
};

static void
need(struct block *b)
{
	if (b->base == 0) {
		b->base = ml_io_map(b->phys, b->size);
	}
}

/* Time for the loader to note what has just been said, where it is not yet known to be harmless. */
static void
hold(bool single_access)
{
	if (step == newest_step && pause_ms && (!single_access || fine)) {
		sleep_ms(pause_ms);
	}
}

/* What a step is about to do, in a line. */
static void
announce(const char *what)
{
	printf("adreno: step %u: %s\n", step, what);
	hold(false);
}

static uint32_t
get(struct block *b, const char *name, uint32_t offset)
{
	uint32_t value;

	need(b);
	if (quiet) {
		return rd(b->base + offset);
	}
	if (step == newest_step) {
		printf("adreno: %u: about to read %s %s at %#x\n", step, b->name, name, b->phys + offset);
		hold(true);
	}
	value = rd(b->base + offset);
	printf("adreno: %u: %s %s (%#x) = %#x\n", step, b->name, name, b->phys + offset, value);
	return value;
}

static void
put(struct block *b, const char *name, uint32_t offset, uint32_t value)
{
	need(b);
	if (!quiet) {
		printf("adreno: %u: about to write %s %s at %#x: %#x\n", step, b->name, name, b->phys + offset, value);
		snprintf(last_write, sizeof(last_write), "step %u wrote %#x to %s %s at %#x", step, value, b->name, name,
		    b->phys + offset);
		hold(true);
	}
	wr(b->base + offset, value);
}

/* Set bits of a register unless they are set. Returns what the register holds afterwards. */
static uint32_t
set(struct block *b, const char *name, uint32_t offset, uint32_t bits)
{
	uint32_t was = get(b, name, offset);

	if ((was & bits) == bits) {
		return was;
	}
	put(b, name, offset, was | bits);
	return get(b, name, offset);
}

/* Look at a register that has been read before until the masked bits have the value, for so many milliseconds. */
static bool
wait_for(struct block *b, uint32_t offset, uint32_t mask, uint32_t value, uint32_t ms)
{
	for (uint32_t i = 0; i < ms * 10; i++) {
		if ((rd(b->base + offset) & mask) == value) {
			return true;
		}
		sleep_us(100);
	}
	return (rd(b->base + offset) & mask) == value;
}

/* ---- step 1: the clocks from outside, and RPMh's names ------------------------------------------------ */

/*
 * dt: clock-controller@100000 (qcom,yupik-gcc). The GPU's clocks there, by the names the GPU's
 * nodes give them: gcc_gpu_memnoc_gfx and gcc_gpu_snoc_dvm_gfx (its two bus connections),
 * "axi_clk" = gcc_ddrss_gpu_axi (memory), gcc_gpu_cfg_ahb (the registers of its clock controller);
 * and the two taps of the 600 MHz PLL that its clock controller takes as inputs. None of them is
 * among that node's protected-clocks, the ones the hypervisor keeps to itself. Offsets:
 * gcc-sc7280.c. The touch screen driver uses this controller too.
 */
static struct block gcc = { "gcc", 0x00100000, 0x80000 };
#define GCC_PLL_VOTE            0x52004         /* bit 15: gcc_gpu_gpll0_clk_src (600 MHz), bit 16: ..._div_clk_src (300 MHz); gcc-sm8150.c puts both gates here, not at 0x52000 */
#define GCC_PLL_VOTE_GPU_DIV    ((1u << 15) | (1u << 16))
#define GCC_GPU_CFG_AHB_CBCR    0x71004
#define GCC_GPU_MEMNOC_GFX_CBCR 0x7100c
#define GCC_GPU_SNOC_DVM_GFX_CBCR 0x71018
#define GCC_DDRSS_GPU_AXI_CBCR  0x71154
#define CBCR_ENABLE             (1u << 0)       /* a clock's control register: on */
#define CBCR_OFF                (1u << 31)      /* ... and whether it is in fact off */

/* dt: reserved-memory cmd_db@80860000 is the reference phone's; this phone's qcom,cmd-db@c3f000c
 * points at 0x85f20000 (the touch driver read the pointer and the table in the M1 rounds). */
#define CMD_DB_PHYS             0x85f20000UL
#define CMD_DB_SIZE             0x20000UL

struct cmd_db_slave {
	uint16_t id, header_offset, data_offset, count, version, reserved[3];
};
struct cmd_db_header {
	uint32_t version;
	uint8_t magic[4];
	struct cmd_db_slave slave[8];
	uint32_t checksum, reserved;
	uint8_t data[];
};
struct cmd_db_entry {
	char id[8];
	uint32_t priority[2];
	uint32_t address;
	uint16_t length, offset;
};

static const struct cmd_db_header *cmd_db;

/*
 * Where RPMh has a resource (0: nowhere) and, for a supply rail, the levels it knows: the GMU's
 * firmware is told the levels by their position in this list.
 */
static uint32_t
rpmh_resource(const char *name, uint16_t *levels, unsigned *level_count)
{
	const struct cmd_db_header *db = cmd_db;
	char id[8] = { 0 };

	if (level_count != NULL) {
		*level_count = 0;
	}
	if (db == NULL) {
		db = cmd_db = (const struct cmd_db_header *)ml_io_map_wcomb(CMD_DB_PHYS, CMD_DB_SIZE);  /* memory, not registers */
	}
	if (db->magic[0] != 0xdb || db->magic[1] != 0x30 || db->magic[2] != 0x03 || db->magic[3] != 0x0c) {
		return 0;
	}
	strlcpy(id, name, sizeof(id));
	for (unsigned s = 0; s < 8; s++) {
		const struct cmd_db_slave *slave = &db->slave[s];
		const struct cmd_db_entry *entry = (const struct cmd_db_entry *)(db->data + slave->header_offset);

		if (slave->id == 0 || slave->count > 512) {
			continue;
		}
		for (unsigned i = 0; i < slave->count; i++) {
			const uint8_t *data = db->data + slave->data_offset + entry[i].offset;
			unsigned count = entry[i].length / 2u;

			if (memcmp(entry[i].id, id, sizeof(id)) != 0) {
				continue;
			}
			if ((unsigned)slave->data_offset + entry[i].offset + entry[i].length > CMD_DB_SIZE - 0x1000) {
				count = 0;
			}
			for (unsigned l = 0; levels != NULL && l < count && l < 16; l++) {
				levels[l] = (uint16_t)(data[2 * l] | data[2 * l + 1] << 8);
				if (l > 0 && levels[l] == 0) {
					break;
				}
				*level_count = l + 1;
			}
			return entry[i].address;
		}
	}
	return 0;
}

static void
show_rpmh_resource(const char *name)
{
	uint16_t level[16];
	unsigned count = 0;
	uint32_t address = rpmh_resource(name, level, &count);
	char levels[160];
	int at = 0;

	if (address == 0) {
		printf("adreno: %u: RPMh has no \"%s\"\n", step, name);
		return;
	}
	levels[0] = 0;
	for (unsigned l = 0; l < count && at < (int)sizeof(levels) - 8; l++) {
		at += snprintf(levels + at, sizeof(levels) - (size_t)at, " %u", level[l]);
	}
	printf("adreno: %u: RPMh has \"%s\" at %#x; levels:%s\n", step, name, address, levels);
}

/*
 * The AP's own RPMh command slots (apps_rsc), copied from the SLPI driver where they were
 * proven on this phone (hakios.slpi.c: the M1 rounds' rail votes). The GMU's OOB switch will
 * not close until the GX rail is voted, and the GMU's own vote on the HFI is asynchronous;
 * the host's vote here is what a6xx_gmu_gfx_rail_on gets from kgsl_pwrctrl on Android.
 */
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
	printf("adreno: %u: rpmh: %#lx would not take %#x: it reads %#x\n", step, (unsigned long) where, want,
	    rd(where));
	return false;
}

static bool
tcs_send(uint32_t cell, uint32_t data)
{
	uint32_t status = 0, polls, irq;
	unsigned slot;
	bool found = false;

	if (rsc_tcs == 0) {
		rsc_tcs = ml_io_map(RSC_TCS_PHYS, RSC_TCS_SIZE);
		if (rsc_tcs == 0) {
			printf("adreno: %u: rpmh: could not map the tcs window at %#lx\n", step,
			    (unsigned long) RSC_TCS_PHYS);
			return false;
		}
	}
	for (slot = 0; slot < RSC_ACTIVE_SLOTS; slot++) {
		if (rd(DRV_TCS(slot, DRV_STATUS, 0)) != 0) {
			found = true;
			break;
		}
	}
	if (!found) {
		printf("adreno: %u: rpmh: no slot has ever completed a transaction\n", step);
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
	printf("adreno: %u: rpmh: slot %u: cell %#x data %#x: %u polls, interrupts %#x, status %#x%s\n", step, slot,
	    cell, data, polls, irq, status, status & (1u << 16) ? ", completed" : ", not completed");
	wr(rsc_tcs + RSC_IRQ_CLEAR, 1u << slot);
	wr(DRV_TCS(slot, DRV_CMD_ENABLE, 0), 0);
	wr(DRV_TCS(slot, DRV_CMD_WAIT_FOR_CMPL, 0), 0);
	return (status & (1u << 16)) != 0;
}

static bool
step_outside(void)
{
	announce("read the GPU's clocks in the global clock controller (registers the touch screen driver uses too)");
	get(&gcc, "pll votes", GCC_PLL_VOTE);
	get(&gcc, "gpu_cfg_ahb", GCC_GPU_CFG_AHB_CBCR);
	get(&gcc, "gpu_memnoc_gfx", GCC_GPU_MEMNOC_GFX_CBCR);
	get(&gcc, "gpu_snoc_dvm_gfx", GCC_GPU_SNOC_DVM_GFX_CBCR);
	get(&gcc, "ddrss_gpu_axi", GCC_DDRSS_GPU_AXI_CBCR);

	announce("look up the GPU's supply rails in the boot loader's list of RPMh resources (memory at 0x80860000)");
	show_rpmh_resource("gfx.lvl");          /* the 3D core's own rail (dt: VDD_GFX_LEVEL, PM8350C S7) */
	show_rpmh_resource("cx.lvl");           /* the rail of the CX half, shared with most of the SoC */
	show_rpmh_resource("mx.lvl");           /* memories */
	return true;
}

/* ---- step 2: the fuses ------------------------------------------------------------------------------- */

/*
 * dt: qfprom@780000 with gpu_speed_bin@1e9 (two bytes, 8 bits from bit 5) and gpu_gaming_bin@1f5
 * (bit 5). Linux has the same cell 0x4000 further on, in the error-corrected copy of the fuses
 * (sc7280.dtsi: efuse@784000), and that is the copy it reads under this hypervisor on sibling
 * boards; so that one first.
 */
static struct block qfprom = { "qfprom", 0x00780000, 0x7000 };
#define QFPROM_CORRECTED        0x4000
#define QFPROM_GPU_SPEED_BIN    0x1e9
#define QFPROM_GPU_GAMING_BIN   0x1f5

static uint32_t
get8(struct block *b, const char *name, uint32_t offset)
{
	uint32_t value;

	need(b);
	if (step == newest_step) {
		printf("adreno: %u: about to read %s %s at %#x (a byte)\n", step, b->name, name, b->phys + offset);
		hold(true);
	}
	value = rd8(b->base + offset);
	printf("adreno: %u: %s %s (%#x) = %#x\n", step, b->name, name, b->phys + offset, value);
	return value;
}

static bool
step_fuses(void)
{
	uint32_t bin, gaming;

	announce("read the GPU's fuses in the error-corrected copy (0x784000), which Linux reads");
	bin = get8(&qfprom, "speed bin, low", QFPROM_CORRECTED + QFPROM_GPU_SPEED_BIN);
	bin |= get8(&qfprom, "speed bin, high", QFPROM_CORRECTED + QFPROM_GPU_SPEED_BIN + 1) << 8;
	bin = bin >> 5 & 0xff;
	speed_bin = bin;
	gaming = get8(&qfprom, "gaming bin", QFPROM_CORRECTED + QFPROM_GPU_GAMING_BIN) >> 5 & 1;
	/* dt: qcom,gpu-models: 0xac and 0xbe are sold as Adreno 643, anything else is the 642L */
	printf("adreno: %u: speed bin %u: an Adreno %s; gaming bin %u\n", step, bin, bin == 0xac || bin == 0xbe ? "643" : "642L",
	    gaming);

	announce("read the same fuses where the phone's device tree has them (0x780000)");
	bin = get8(&qfprom, "raw speed bin, low", QFPROM_GPU_SPEED_BIN);
	bin |= get8(&qfprom, "raw speed bin, high", QFPROM_GPU_SPEED_BIN + 1) << 8;
	printf("adreno: %u: raw speed bin %u\n", step, bin >> 5 & 0xff);
	return true;
}

/* ---- step 3: the GPU's clock controller ----------------------------------------------------------------- */

/*
 * dt: clock-controller@3d90000 (qcom,yupik-gpucc), 0x9000 long; offsets: gpucc-sc7280.c. Its
 * registers answer only while gcc_gpu_cfg_ahb runs, which Linux turns on for good when it
 * starts (gcc-sc7280.c, the end of the probe function).
 *
 * On this phone the same controller is qcom,gpucc (qcom,gpucc-sm8150), reg <0x2c90000 0x9000>,
 * and the device tree's own syscon aliases land on the same offsets: gpu_gx_sw_reset at
 * 0x2c91008, gpu_gx_gdsc at 0x2c9100c, gpu_cx_gdsc at 0x2c9106c, gpu_cx_hw_ctrl at 0x2c91540 --
 * which are this block's 0x1008, 0x100c, 0x106c and 0x1540. The GPU, GMU and SMMU move with it:
 * 0x2c00000 (the kgsl-3d0 reg), 0x2c6a000 (qcom,gmu) and 0x2ca0000 (kgsl-iommu), the same
 * relative layout the SC7280 tree has.
 */
static struct block gpucc = { "gpucc", 0x02c90000, 0x9000 };
#define GPUCC_PLL0_MODE         0x0000          /* the 3D core's PLL: bit 31 locked, bit 0 output on; +4: times 19.2 MHz */
#define GPUCC_PLL1_MODE         0x0100          /* a 500 MHz PLL, an alternative source for the GMU's clock */
#define GPUCC_CXO_AON_CBCR      0x1004
#define GPUCC_GX_SW_RESET       0x1008          /* dt: gpu_gx_sw_reset */
#define GPUCC_GX_GDSCR          0x100c          /* dt: gpu_gx_gdsc, the 3D core's power switch: the GMU's to operate */
#define GPUCC_GX_CFG_GDSCR      0x1010          /* ... and its status: Linux's gpucc-sm8150.c reads this one (POLL_CFG_GDSCR) */
#define GPUCC_GX_HW_CTRL        0x1508          /* dt: gpu_gx_hw_ctrl; Linux's gpucc-sm8150.c calls this gx_gdsc.clamp_io_ctrl */
#define GPUCC_GX_GMU_CBCR       0x1064
#define GPUCC_CX_GDSCR          0x106c          /* dt: gpu_cx_gdsc */
#define GPUCC_AHB_CBCR          0x1078          /* gpu_cc_ahb_clk: the register bus inside the GPU block */
#define GPUCC_CX_GMU_CBCR       0x1098          /* gpu_cc_cx_gmu_clk: the GMU */
#define GPUCC_CXO_CBCR          0x109c          /* gpu_cc_cxo_clk: the crystal, for the GMU's timers */
#define GPUCC_GMU_RCGR          0x1120          /* gpu_cc_gmu_clk_src: +0 command, +4 source and divider */
#define GPUCC_CB_CBCR           0x1170          /* gpu_cc_cb_clk: Linux keeps it on always */
#define GPUCC_HUB_AON_CBCR      0x1178          /* gpu_cc_hub_aon_clk */
#define GPUCC_HUB_RCGR          0x117c          /* gpu_cc_hub_clk_src */
#define GPUCC_HUB_CX_INT_CBCR   0x1204          /* gpu_cc_hub_cx_int_clk */
#define GPUCC_CX_HW_CTRL        0x1540          /* dt: gpu_cx_hw_ctrl: bit 31 is where the vendor's driver sees CX come on */
#define GPUCC_GX_DOMAIN_MISC    0x158c          /* dt: gpu_gx_domain_addr */
#define GPUCC_SMMU_VOTE_CBCR    0x5000          /* gpu_cc_hlos1_vote_gpu_smmu_clk: our vote for the SMMU's clock */

#define GDSC_SW_COLLAPSE        (1u << 0)       /* a power switch's control register (gdsc.c): off, by software */
#define GDSC_HW_CONTROL         (1u << 1)       /* something else than software operates it */
#define GDSC_POWER_ON           (1u << 31)
/* cfg_gdscr, the switch's status next to its control (Linux's gdsc.h, polled when POLL_CFG_GDSCR):
 * bit 16 says the domain really has power, bit 15 that it really is off. gdscr's own bit 31 is a
 * request the hardware answers, and for this switch it has never been seen to change (040314). */
#define GDSC_POWER_UP_COMPLETE  (1u << 16)
#define GDSC_POWER_DOWN_COMPLETE (1u << 15)
/* In clamp_io_ctrl (gdsc.c: GMEM_RESET_MASK, GMEM_CLAMP_IO_MASK): the switch's AON reset, and
 * the clamp that keeps its outputs at a known level while it is off. gdsc_enable() asserts the
 * reset for a microsecond, releases it, and only then lets the outputs out of the clamp. */
#define GDSC_AON_RESET          (1u << 4)
#define GDSC_CLAMP_IO           (1u << 0)

/* Read-only clock-window dump, used to capture the bootloader's working GPUCC
 * state before this driver writes anything (compare with step 17's dump). */
static void
gpucc_window_dump(const char *when)
{
	for (uint32_t off = 0x1000; off < 0x1200; off += 0x20) {
		printf("adreno: %u: gpucc %s [%#x]:", step, when, off);
		for (uint32_t i = 0; i < 8; i++) {
			printf(" %#x", rd(gpucc.base + off + 4 * i));
		}
		printf("\n");
	}
}

static bool
step_gpucc(void)
{
	announce("make sure the clock of the GPU clock controller's registers runs (gcc_gpu_cfg_ahb)");
	if ((set(&gcc, "gpu_cfg_ahb", GCC_GPU_CFG_AHB_CBCR, CBCR_ENABLE) & CBCR_OFF) &&
	    !wait_for(&gcc, GCC_GPU_CFG_AHB_CBCR, CBCR_OFF, 0, 10)) {
		printf("adreno: %u: it stays off (%#x): the GPU's clock controller would not answer\n", step,
		    rd(gcc.base + GCC_GPU_CFG_AHB_CBCR));
		return false;
	}

	/*
	 * Diagnostic recovery: earlier "turn the 3D clock on by hand" probes left
	 * branch 0x1054 running and RCG 0x101c out of root-off across `xl run`
	 * iterations. The GMU firmware then fails PwrIdleStateMgrStart (HFI error
	 * 115/125) because it expects to bring the clock up itself. Restore the
	 * bootloader's pre-driver clock state before reading anything.
	 */
	{
		uint32_t restore = 0;

		if (PE_parse_boot_argn("adreno_gpucc_restore", &restore, sizeof(restore)) && restore) {
			put(&gpucc, "gfx3d rcg (restore)", 0x101c, 0x80000012);
			put(&gpucc, "gfx3d rcg src (restore)", 0x1020, 0x1);
			put(&gpucc, "gx_gfx3d (restore)", 0x1054, 0x80004221);
			put(&gpucc, "gx_cxo (restore)", 0x1060, 0x80000001);
			put(&gpucc, "gx_gmu (restore)", 0x1064, 0x80000000);
			put(&gpucc, "pll0 opmode (restore)", 0x38, 0);
			put(&gpucc, "pll0 mode (restore)", 0x0, 0x1000004);
			printf("adreno: %u: restored bootloader 3D clock state: rcg %#x src %#x branch %#x\n", step,
			    rd(gpucc.base + 0x101c), rd(gpucc.base + 0x1020), rd(gpucc.base + 0x1054));
		}
	}
	announce("read the GPU's clock controller (0x3d90000) as it is: the first access to the GPU's part of the SoC");
	get(&gpucc, "cx_gdscr", GPUCC_CX_GDSCR);
	get(&gpucc, "cx_hw_ctrl", GPUCC_CX_HW_CTRL);
	get(&gpucc, "gx_gdscr", GPUCC_GX_GDSCR);
	get(&gpucc, "gx_sw_reset", GPUCC_GX_SW_RESET);
	get(&gpucc, "gx_domain_misc", GPUCC_GX_DOMAIN_MISC);
	get(&gpucc, "pll0 mode", GPUCC_PLL0_MODE);
	get(&gpucc, "pll0 multiplier", GPUCC_PLL0_MODE + 4);
	get(&gpucc, "pll1 mode", GPUCC_PLL1_MODE);
	get(&gpucc, "pll1 multiplier", GPUCC_PLL1_MODE + 4);
	get(&gpucc, "gmu_clk_src command", GPUCC_GMU_RCGR);
	get(&gpucc, "gmu_clk_src source", GPUCC_GMU_RCGR + 4);
	get(&gpucc, "hub_clk_src command", GPUCC_HUB_RCGR);
	get(&gpucc, "hub_clk_src source", GPUCC_HUB_RCGR + 4);
	get(&gpucc, "cxo_aon", GPUCC_CXO_AON_CBCR);
	get(&gpucc, "gx_gmu", GPUCC_GX_GMU_CBCR);
	get(&gpucc, "ahb", GPUCC_AHB_CBCR);
	get(&gpucc, "cx_gmu", GPUCC_CX_GMU_CBCR);
	get(&gpucc, "cxo", GPUCC_CXO_CBCR);
	get(&gpucc, "cb", GPUCC_CB_CBCR);
	get(&gpucc, "hub_aon", GPUCC_HUB_AON_CBCR);
	get(&gpucc, "hub_cx_int", GPUCC_HUB_CX_INT_CBCR);
	get(&gpucc, "smmu vote", GPUCC_SMMU_VOTE_CBCR);
	{
		uint32_t baseline = 0;

		if (PE_parse_boot_argn("adreno_gpucc_baseline", &baseline, sizeof(baseline)) && baseline) {
			gpucc_window_dump("baseline");
		}
	}
	return true;
}

/* ---- step 4: CX power --------------------------------------------------------------------------------- */

/* Not to touch the GPU's clock controller without the clock of its registers: where step 3 has not been. */
static bool
gpucc_answers(void)
{
	need(&gcc);
	need(&gpucc);
	if (rd(gcc.base + GCC_GPU_CFG_AHB_CBCR) & CBCR_OFF) {
		printf("adreno: %u: the GPU clock controller's registers have no clock (gcc gpu_cfg_ahb %#x): step 3\n", step,
		    rd(gcc.base + GCC_GPU_CFG_AHB_CBCR));
		return false;
	}
	return true;
}

static bool
cx_is_on(void)
{
	return (rd(gpucc.base + GPUCC_CX_GDSCR) | rd(gpucc.base + GPUCC_CX_HW_CTRL)) & GDSC_POWER_ON;
}

static void stale_stop(void);

/*
 * The switch hangs on the rail "cx.lvl", which is up for as long as the SoC runs at all; the
 * vendor's driver (gdsc-regulator.c) clears the collapse bit and waits for bit 31 of the register
 * the device tree calls hw-ctrl-addr, Linux for bit 31 of the control register itself.
 */
static bool
step_cx_power(void)
{
	uint32_t gdscr;

	if (!gpucc_answers()) {
		return false;
	}
	announce("switch the CX half of the GPU block on (gpu_cx_gdsc): clear the collapse bit of its power switch");
	gdscr = get(&gpucc, "cx_gdscr", GPUCC_CX_GDSCR);
	get(&gpucc, "cx_hw_ctrl", GPUCC_CX_HW_CTRL);
	if (cx_is_on()) {
		/*
		 * A kernel that kept the GPU for the system to use (adreno_keep) leaves it running when
		 * its run ends. What it left -- the GMU's processor, the 3D core's power, the GMU's
		 * votes in RPMh, the SMMU's contexts pointing into memory that is somebody else's now --
		 * is taken down as gpu_block_stop() would have, and the start is from nothing.
		 */
		printf("adreno: %u: CX is on already: an earlier kernel left the GPU block running; stopping it first\n", step);
		stale_stop();
		if (cx_is_on()) {
			printf("adreno: %u: CX stays on: going on with it as it is\n", step);
			return true;
		}
		gdscr = rd(gpucc.base + GPUCC_CX_GDSCR);
	}
	if (gdscr & GDSC_HW_CONTROL) {
		printf("adreno: %u: the switch is under hardware control: not ours to operate\n", step);
		return false;
	}
	put(&gpucc, "cx_gdscr", GPUCC_CX_GDSCR, gdscr & ~GDSC_SW_COLLAPSE);
	for (int i = 0; i < 500 && !cx_is_on(); i++) {
		sleep_us(100);
	}
	get(&gpucc, "cx_gdscr", GPUCC_CX_GDSCR);
	get(&gpucc, "cx_hw_ctrl", GPUCC_CX_HW_CTRL);
	if (!cx_is_on()) {
		printf("adreno: %u: CX does not come on\n", step);
		return false;
	}
	printf("adreno: %u: CX is on\n", step);
	if (rd(gpucc.base + GPUCC_GX_GDSCR) & GDSC_POWER_ON) {
		/*
		 * The 3D core kept its power while the rest of the block lost it: an ending that went
		 * wrong (a GMU that did not answer, before gpu_block_stop() switched the core off
		 * itself). Such a core is where it was, in the middle of things, and does not take the
		 * steps' commands (seen: step 21); switching it off from here has hung the whole phone
		 * (seen once). Left alone: no GPU until the phone has been restarted.
		 */
		printf("adreno: %u: THE 3D CORE HAS POWER ALREADY, left on by an earlier kernel: not usable until the phone is "
		    "restarted\n", step);
		return false;
	}
	return true;
}

/* ---- step 5: clocks ------------------------------------------------------------------------------------ */

#define RCG_UPDATE              (1u << 0)       /* command register (clk-rcg2.c): take the new source and divider */
#define RCG_SOURCE_MASK         0x371fu         /* [4:0] 2*divider-1, [10:8] source, [13:12] mode */
#define RCG_GPLL0_DIV(twice)    (6u << 8 | ((twice) - 1))       /* source 6: the 300 MHz tap; twice the divider wanted */

/* Turn a clock on and see it run. Not every clock's status can be believed (halt_check in Linux): `must` says which. */
static bool
clock_on(struct block *b, const char *name, uint32_t cbcr, bool must)
{
	if (!(set(b, name, cbcr, CBCR_ENABLE) & CBCR_OFF) || wait_for(b, cbcr, CBCR_OFF, 0, 10)) {
		return true;
	}
	printf("adreno: %u: %s %s stays off (%#x)%s\n", step, b->name, name, rd(b->base + cbcr),
	    must ? "" : "; its status bit is not one to rely on, going on");
	return !must;
}

static bool
clock_source(const char *name, uint32_t rcgr, uint32_t source)
{
	uint32_t was = get(&gpucc, name, rcgr + 4);

	if ((was & RCG_SOURCE_MASK) == source) {
		return true;
	}
	put(&gpucc, name, rcgr + 4, (was & ~RCG_SOURCE_MASK) | source);
	put(&gpucc, name, rcgr, rd(gpucc.base + rcgr) | RCG_UPDATE);
	if (!wait_for(&gpucc, rcgr, RCG_UPDATE, 0, 10)) {
		printf("adreno: %u: %s does not take its new source (command %#x)\n", step, name, rd(gpucc.base + rcgr));
		return false;
	}
	get(&gpucc, name, rcgr + 4);
	return true;
}

static bool measure_clocks(void);

/*
 * The clocks the device tree gives the GMU and the SMMU, at the rates both the vendor's driver
 * and Linux bring the GMU up with: 200 MHz for the GMU, 150 MHz for the "hub" (a6xx_gmu_resume).
 * Both come from the 300 MHz tap of the global 600 MHz PLL, which runs anyway: no PLL of the
 * GPU's own has to be started.
 */
static bool
step_clocks(void)
{
	if (!gpucc_answers()) {
		return false;
	}
	if (!cx_is_on()) {
		printf("adreno: %u: CX is off: step 4\n", step);
		return false;
	}
	announce("in the global clock controller: ask for the 300 MHz tap for the GPU; the GPU's bus and memory clocks on");
	set(&gcc, "pll votes", GCC_PLL_VOTE, GCC_PLL_VOTE_GPU_DIV);
	if (!clock_on(&gcc, "gpu_memnoc_gfx", GCC_GPU_MEMNOC_GFX_CBCR, false) ||
	    !clock_on(&gcc, "gpu_snoc_dvm_gfx", GCC_GPU_SNOC_DVM_GFX_CBCR, true) ||
	    !clock_on(&gcc, "ddrss_gpu_axi", GCC_DDRSS_GPU_AXI_CBCR, false)) {
		return false;
	}

	announce("in the GPU's clock controller: the GMU's clock to 200 MHz, the hub's to 150 MHz");
	if (!clock_source("gmu_clk_src", GPUCC_GMU_RCGR, RCG_GPLL0_DIV(3)) ||       /* 300 MHz / 1.5 */
	    !clock_source("hub_clk_src", GPUCC_HUB_RCGR, RCG_GPLL0_DIV(4))) {       /* 300 MHz / 2 */
		return false;
	}

	announce("in the GPU's clock controller: the clocks of the GMU and of the SMMU on");
	if (!clock_on(&gpucc, "cb", GPUCC_CB_CBCR, false) ||
	    !clock_on(&gpucc, "cxo", GPUCC_CXO_CBCR, true) ||
	    !clock_on(&gpucc, "ahb", GPUCC_AHB_CBCR, false) ||
	    !clock_on(&gpucc, "hub_aon", GPUCC_HUB_AON_CBCR, true) ||
	    !clock_on(&gpucc, "hub_cx_int", GPUCC_HUB_CX_INT_CBCR, true) ||
	    !clock_on(&gpucc, "cx_gmu", GPUCC_CX_GMU_CBCR, true) ||
	    !clock_on(&gpucc, "smmu vote", GPUCC_SMMU_VOTE_CBCR, false)) {
		return false;
	}
	/*
	 * The block resets in this clock controller. The vendor's power-on deasserts the CX and GMU
	 * block resets (qcom's GPUCC reset map: GPUCC_GPU_CC_CX_BCR 0x1068, GPUCC_GPU_CC_GMU_BCR
	 * 0x111c), and this driver had only touched the GX one. A GMU block reset left asserted
	 * holds the CM3 whatever CM3_SYSRESET says; writing 0 deasserts it.
	 */
	put(&gpucc, "cx_bcr", 0x1068, 0);
	put(&gpucc, "gmu_bcr", 0x111c, 0);
	get(&gpucc, "cx_bcr", 0x1068);
	get(&gpucc, "gmu_bcr", 0x111c);
	return measure_clocks();
}

/*
 * Whether a clock runs is not always to be had from its control register: the one of the register
 * bus inside the GPU block, gpu_cc_ahb, reads 0 on this phone before and after its enable bit is
 * written (Linux, for which it has "no bit to check", writes the bit and waits 10 us). The SoC can
 * measure instead: a clock controller can put any of its clocks on a debug output, and the global
 * controller counts what it is given there against the crystal. The vendor's kernel shows the
 * result as "clk_measure"; registers, selections and procedure are its debugcc-yupik.c and
 * clk-debug.c (mainline Linux has no counterpart).
 */
#define GCC_DEBUG_MUX           0x62000         /* [9:0] which clock goes to the counter */
#define GCC_DEBUG_MUX_GPUCC     0x146           /* ... the debug output of the GPU's clock controller */
#define GCC_DEBUG_DIV           0x62004         /* [3:0] divider - 1; both controllers divide by 2 */
#define GCC_DEBUG_CBCR          0x62008
#define GCC_XO_DIV4_CBCR        0x6200c         /* the counter's time base: the crystal / 4, 4.8 MHz */
#define GCC_MEASURE_CTL         0x62038         /* [19:0] how many ticks of the time base to count for */
#define MEASURE_RUN             (1u << 20)
#define MEASURE_CLEAR           (1u << 21)
#define GCC_MEASURE_STATUS      0x6203c         /* [24:0] the count */
#define MEASURE_DONE            (1u << 25)
#define MEASURE_BRIEF           0x1000          /* ticks: a millisecond */
#define MEASURE_FULL            0x20000         /* 27 ms */
#define MEASURE_FAILED          0xffffffffu
#define GPUCC_DEBUG_DIV         0x10fc
#define GPUCC_DEBUG_CBCR        0x1100
#define GPUCC_DEBUG_MUX         0x1568          /* [7:0] which of the GPU's clocks */
#define GPUCC_DEBUG_CXO_AON     0x0b
#define GPUCC_DEBUG_GX_GFX3D    0x0d            /* the 3D core's own clock */
#define GPUCC_DEBUG_GX_GMU      0x11
#define GPUCC_DEBUG_AHB         0x12
#define GPUCC_DEBUG_CX_GMU      0x1a
#define GPUCC_DEBUG_CXO         0x1b
#define GPUCC_DEBUG_HUB_CX_INT  0x1c
#define GPUCC_DEBUG_CB          0x26
#define GPUCC_DEBUG_HUB_AON     0x27

/* Let the counter run for so many ticks of its time base; what it counted of the clock on its input. */
static uint32_t
count_clock(uint32_t ticks)
{
	uint32_t control = get(&gcc, "measure control", GCC_MEASURE_CTL) & ~(MEASURE_RUN | MEASURE_CLEAR | 0xfffffu);
	uint32_t status = 0;

	put(&gcc, "measure control", GCC_MEASURE_CTL, control | MEASURE_CLEAR);
	sleep_us(10);
	put(&gcc, "measure control", GCC_MEASURE_CTL, control | ticks);
	put(&gcc, "measure control", GCC_MEASURE_CTL, control | ticks | MEASURE_RUN);
	for (int i = 0; i < 2000 && !(status & MEASURE_DONE); i++) {
		sleep_us(100);
		status = rd(gcc.base + GCC_MEASURE_STATUS);
	}
	put(&gcc, "measure control", GCC_MEASURE_CTL, control | ticks);
	return status & MEASURE_DONE ? get(&gcc, "measure status", GCC_MEASURE_STATUS) & 0x1ffffff : MEASURE_FAILED;
}

/*
 * The rate in Hz of one of the GPU clock controller's clocks; 0 if it does not run, MEASURE_FAILED
 * if the counter does not finish. Everything that is changed for it is put back.
 */
static uint32_t
measure(uint32_t select)
{
	uint32_t gpu_mux = get(&gpucc, "debug mux", GPUCC_DEBUG_MUX), gpu_div = get(&gpucc, "debug divider", GPUCC_DEBUG_DIV);
	uint32_t gpu_cbcr = get(&gpucc, "debug output", GPUCC_DEBUG_CBCR), mux = get(&gcc, "debug mux", GCC_DEBUG_MUX);
	uint32_t div = get(&gcc, "debug divider", GCC_DEBUG_DIV), cbcr = get(&gcc, "debug input", GCC_DEBUG_CBCR);
	uint32_t base = get(&gcc, "crystal/4", GCC_XO_DIV4_CBCR), control = get(&gcc, "measure control", GCC_MEASURE_CTL);
	uint32_t brief, full;

	put(&gpucc, "debug mux", GPUCC_DEBUG_MUX, (gpu_mux & ~0xffu) | select);
	put(&gpucc, "debug divider", GPUCC_DEBUG_DIV, (gpu_div & ~0xfu) | 1);
	put(&gpucc, "debug output", GPUCC_DEBUG_CBCR, gpu_cbcr | CBCR_ENABLE);
	put(&gcc, "debug mux", GCC_DEBUG_MUX, (mux & ~0x3ffu) | GCC_DEBUG_MUX_GPUCC);
	put(&gcc, "debug divider", GCC_DEBUG_DIV, (div & ~0xfu) | 1);
	put(&gcc, "debug input", GCC_DEBUG_CBCR, cbcr | CBCR_ENABLE);
	put(&gcc, "crystal/4", GCC_XO_DIV4_CBCR, base | CBCR_ENABLE);
	/* a counter whose input stands still keeps its last count: the two counts differ only if the clock runs */
	brief = count_clock(MEASURE_BRIEF);
	full = brief == MEASURE_FAILED ? MEASURE_FAILED : count_clock(MEASURE_FULL);
	put(&gcc, "measure control", GCC_MEASURE_CTL, control);
	put(&gcc, "crystal/4", GCC_XO_DIV4_CBCR, base);
	put(&gcc, "debug input", GCC_DEBUG_CBCR, cbcr);
	put(&gcc, "debug divider", GCC_DEBUG_DIV, div);
	put(&gcc, "debug mux", GCC_DEBUG_MUX, mux);
	put(&gpucc, "debug output", GPUCC_DEBUG_CBCR, gpu_cbcr);
	put(&gpucc, "debug divider", GPUCC_DEBUG_DIV, gpu_div);
	put(&gpucc, "debug mux", GPUCC_DEBUG_MUX, gpu_mux);

	if (full == MEASURE_FAILED) {
		return MEASURE_FAILED;
	}
	if (full == brief) {
		return 0;
	}
	/* the vendor's formula; times 4 for the two dividers on the way to the counter */
	return (uint32_t)(((uint64_t)full * 10 + 15) * 4800000 / ((uint64_t)MEASURE_FULL * 10 + 35) * 4);
}

static uint32_t
measure_quietly(uint32_t select)
{
	uint32_t hz;

	quiet = true;
	hz = measure(select);
	quiet = false;
	return hz;
}

/*
 * Measure the clocks step 5 has turned on. The crystal's comes first and aloud: its rate is known,
 * so it shows whether the counter can be believed; one that is off (the 3D core's) comes last.
 */
static bool
measure_clocks(void)
{
	static const struct {
		const char *name;
		uint32_t select;
	} clocks[] = {
		{ "cx_gmu (200 MHz was asked for)", GPUCC_DEBUG_CX_GMU }, { "hub_cx_int (from 150 MHz)", GPUCC_DEBUG_HUB_CX_INT },
		{ "hub_aon", GPUCC_DEBUG_HUB_AON }, { "cb", GPUCC_DEBUG_CB }, { "cxo_aon", GPUCC_DEBUG_CXO_AON },
		{ "gx_gmu (should be off)", GPUCC_DEBUG_GX_GMU }, { "ahb, THE ONE IN QUESTION,", GPUCC_DEBUG_AHB },
	};
	uint32_t hz, ahb_hz = MEASURE_FAILED;

	announce("measure the GPU block's clocks with the SoC's frequency counter (debug registers at gcc 0x162000.., "
	    "gpucc 0x3d910fc, 0x3d91100, 0x3d91568)");
	hz = measure(GPUCC_DEBUG_CXO);
	if (hz == MEASURE_FAILED) {
		/*
		 * The selections and the procedure here are the SC7280's (debugcc-yupik.c); this
		 * phone's controller never finishes the count with them. The clocks themselves are
		 * on -- the CBCRs above say so, and step 4's GDSCR took a write and reads it back,
		 * which a stopped register bus cannot do -- so the step is done even though the
		 * counter says nothing. The measured rates are a nicety, not a bring-up step.
		 */
		printf("adreno: %u: the counter did not finish (its debug selections are the SC7280's); the "
		    "clocks above are on and the block answers, so the steps go on\n", step);
		return true;
	}
	printf("adreno: %u: measured: cxo runs at %u Hz (the crystal: 19200000)\n", step, hz);
	if (hz < 18600000 || hz > 19800000) {
		printf("adreno: %u: the counter does not show the crystal's rate: nothing it says can be used\n", step);
		return false;
	}
	for (unsigned i = 0; i < sizeof(clocks) / sizeof(clocks[0]); i++) {
		hz = measure_quietly(clocks[i].select);
		if (hz == MEASURE_FAILED) {
			printf("adreno: %u: measured: %s: the counter did not finish\n", step, clocks[i].name);
		} else {
			printf("adreno: %u: measured: %s runs at %u Hz\n", step, clocks[i].name, hz);
		}
		if (clocks[i].select == GPUCC_DEBUG_AHB) {
			ahb_hz = hz;
		}
	}
	if (ahb_hz != MEASURE_FAILED && ahb_hz >= 1000000) {
		printf("adreno: %u: THE REGISTER BUS OF THE GPU BLOCK HAS ITS CLOCK, whatever its control register says\n", step);
		return true;
	}
	/* for study: what else is there in the clock controller that could be its switch */
	printf("adreno: %u: the register bus of the GPU block has no clock; what is not 0 in gpucc 0x1000..0x15fc:\n", step);
	for (uint32_t offset = 0x1000; offset < 0x1600; offset += 16) {
		uint32_t row[4] = { rd(gpucc.base + offset), rd(gpucc.base + offset + 4), rd(gpucc.base + offset + 8),
			            rd(gpucc.base + offset + 12) };

		if (row[0] | row[1] | row[2] | row[3]) {
			printf("adreno: %u: gpucc %#x: %#x %#x %#x %#x\n", step, gpucc.phys + offset, row[0], row[1], row[2],
			    row[3]);
		}
	}
	return false;
}

/*
 * What every later step stands on: CX powered, the GMU's clock running by its control register,
 * and the register bus's clock running by measurement (see above).
 */
static bool
cx_ready(void)
{
	uint32_t gmu_clock, ahb_hz;

	if (!gpucc_answers()) {
		return false;
	}
	gmu_clock = rd(gpucc.base + GPUCC_CX_GMU_CBCR);
	if (!cx_is_on() || (gmu_clock & (CBCR_ENABLE | CBCR_OFF)) != CBCR_ENABLE) {
		printf("adreno: %u: needs CX on (it is %s) and the GMU's clock running (cx_gmu %#x): steps 4 and 5\n", step,
		    cx_is_on() ? "on" : "off", gmu_clock);
		return false;
	}
	ahb_hz = measure_quietly(GPUCC_DEBUG_AHB);
	if (ahb_hz == MEASURE_FAILED) {
		/*
		 * Same as step 5: the counter's selections are the SC7280's and it never finishes on
		 * this phone. CX is on and the GMU's clock register reads enabled, and every register
		 * of this block has answered since step 3 (a stopped bus would have ended the run on
		 * the first read), so the bus's clock is there.
		 */
		printf("adreno: %u: the counter did not finish, but CX is on and the block's registers answer: "
		    "the register bus has its clock\n", step);
		return true;
	}
	printf("adreno: %u: the GPU block's register bus clock (ahb) measures %u Hz\n", step, ahb_hz);
	if (ahb_hz < 1000000) {
		printf("adreno: %u: without that clock a register of the GPU block would not answer: not touching it\n", step);
		return false;
	}
	return true;
}

/* ---- step 6: the first words from the GPU block ------------------------------------------------------- */

/*
 * dt: qcom,gmu "kgsl_gmu_reg" at 0x3d6a000, 0x34000 long. Offsets in a6xx_gmu.xml are in words.
 */
static struct block gmu = { "gmu", 0x02c6a000, 0x34000 };
#define GMU_WORD(n)             (4u * (n))
#define GMU_DTCM                GMU_WORD(0x1c00)        /* the processor's 16K of data memory (its code: 0x0c00) */
#define GMU_DTCM_VERSIONS       GMU_WORD(0x1c00 + 0xff8) /* five words where loaded firmware carries its versions */
#define GMU_CM3_SYSRESET        GMU_WORD(0x5000)        /* 1: the processor is held in reset */
#define GMU_CM3_BOOT_CONFIG     GMU_WORD(0x5001)
#define GMU_CM3_FW_INIT_RESULT  GMU_WORD(0x501c)        /* 0xbabeface once firmware has started */
#define GMU_CM3_CFG             GMU_WORD(0x502d)
#define GMU_ALWAYS_ON_COUNTER_L GMU_WORD(0x5088)        /* counts the 19.2 MHz crystal */
#define GMU_ALWAYS_ON_COUNTER_H GMU_WORD(0x5089)
#define GMU_SPTPRAC_PWR_CLK_STATUS GMU_WORD(0x50d0)     /* bit 6: the 3D core has no power, bit 7: no clock */
#define GMU_RPMH_POWER_STATE    GMU_WORD(0x50ec)
#define GMU_HFI_CTRL_STATUS     GMU_WORD(0x5180)
#define GMU_HFI_VERSION_INFO    GMU_WORD(0x5181)
#define GMU_GENERAL_7           GMU_WORD(0x51cc)
#define GMU_AO_HOST_INTERRUPT_STATUS GMU_WORD(0x9305)
#define GMU_AO_INTERRUPT_EN       GMU_WORD(0x9303)
#define GMU_AO_HOST_INTERRUPT_CLR GMU_WORD(0x9304)
#define GMU_AO_HOST_INTERRUPT_MASK GMU_WORD(0x9306)
#define GMU_RSCC_CONTROL_ACK    GMU_WORD(0x9308)
#define GMU_AO_GPU_CX_BUSY_STATUS GMU_WORD(0x930c)
#define GMU_AHB_FENCE_STATUS    GMU_WORD(0x9313)
#define GMU_RBBM_INT_UNMASKED_STATUS GMU_WORD(0x9315)
/* The always-on interrupt lines, as the vendor's driver names and masks them (kgsl_gmu.h:25-33):
 * bit 0 is the GMU's watchdog, which fires when its processor stops feeding it -- the shape of
 * every round since 055623, where the firmware answers "start the core firmware" and then goes
 * silent. Reading the whole always-on side is how to tell a watchdog stop from a fence fault. */
#define GMU_INT_WDOG_BITE       (1u << 0)
#define GMU_INT_RSCC_COMP       (1u << 1)
#define GMU_INT_FENCE_ERR       (1u << 3)
#define GMU_INT_DBD_WAKEUP      (1u << 4)
#define GMU_INT_HOST_AHB_BUS_ERR (1u << 5)
/* What the GMU's firmware keeps of the 3D core's supply and speed, and what it would use without
 * HFI (a6xx_reg.h:961-967, words from the GPU base: subtract 0x1a800 for the GMU block). The
 * firmware writes the vote indices when its DCVS is up; the no-HFI form votes through the three
 * DCVS_* registers and answers in DCVS_RETURN (adreno_a6xx_gmu.c: a6xx_gmu_dcvs_nohfi). */
#define GMU_BOOT_SLUMBER_OPTION GMU_WORD(0x23f8)
#define GMU_GX_VOTE_IDX         GMU_WORD(0x23f9)
#define GMU_MX_VOTE_IDX         GMU_WORD(0x23fa)
#define GMU_DCVS_ACK_OPTION     GMU_WORD(0x23fc)
#define GMU_DCVS_PERF_SETTING   GMU_WORD(0x23fd)
#define GMU_DCVS_BW_SETTING     GMU_WORD(0x23fe)
#define GMU_DCVS_RETURN         GMU_WORD(0x23ff)
#define GMU_NMI_CONTROL_STATUS  GMU_WORD(0x23f0) /* vendor a6xx_reg.h: A6XX_GMU_NMI_CONTROL_STATUS 0x1cbf0 */

/*
 * dt: qcom,kgsl-3d0 "kgsl_3d0_reg_memory" at 0x3d00000 is the 3D core, and without power (the GMU
 * gives it) not to be touched -- except its bus interface ("GBIF", words 0x3c00 on in a6xx.xml),
 * which is in the CX half: the vendor's driver writes GBIF_HALT before it starts the GMU.
 * "cx_misc" at 0x3d9e000 is a page of odds and ends in the CX half.
 */
static struct block gbif = { "gbif", 0x02c00000 + 4 * 0x3c00, 0x1000 };
#define GBIF_SCACHE_CNTL0       (4 * 0x01)
#define GBIF_HALT               (4 * 0x45)
#define GBIF_HALT_ACK           (4 * 0x46)
#define RBBM_GBIF_HALT          GPU_WORD(0x0016)  /* a6xx_reg.h: A6XX_RBBM_GBIF_HALT 0x00016 */
#define RBBM_GBIF_HALT_ACK      GPU_WORD(0x0017)
static struct block cx_misc = { "cx_misc", 0x03d9e000, 0x1000 };
#define CX_MISC_SYSTEM_CACHE_CNTL_0 (4 * 1)
#define CX_MISC_SYSTEM_CACHE_CNTL_1 (4 * 2)

static bool
step_first_words(void)
{
	uint64_t start, nanos;
	uint32_t before, hz;

	if (!cx_ready()) {
		return false;
	}
	announce("read registers of the GMU (0x3d6a000): the first words from the GPU block itself");
	get(&gmu, "cm3_sysreset", GMU_CM3_SYSRESET);
	get(&gmu, "cm3_boot_config", GMU_CM3_BOOT_CONFIG);
	get(&gmu, "cm3_fw_init_result", GMU_CM3_FW_INIT_RESULT);
	get(&gmu, "cm3_cfg", GMU_CM3_CFG);
	get(&gmu, "hfi_ctrl_status", GMU_HFI_CTRL_STATUS);
	get(&gmu, "hfi_version_info", GMU_HFI_VERSION_INFO);
	get(&gmu, "general_7", GMU_GENERAL_7);
	get(&gmu, "rpmh_power_state", GMU_RPMH_POWER_STATE);
	get(&gmu, "sptprac_pwr_clk_status", GMU_SPTPRAC_PWR_CLK_STATUS);
	get(&gmu, "ao_host_interrupt_status", GMU_AO_HOST_INTERRUPT_STATUS);
	get(&gmu, "ao_gpu_cx_busy_status", GMU_AO_GPU_CX_BUSY_STATUS);
	get(&gmu, "ahb_fence_status", GMU_AHB_FENCE_STATUS);
	get(&gmu, "always_on_counter_h", GMU_ALWAYS_ON_COUNTER_H);
	get(&gmu, "always_on_counter_l", GMU_ALWAYS_ON_COUNTER_L);
	start = mach_absolute_time();
	before = rd(gmu.base + GMU_ALWAYS_ON_COUNTER_L);
	sleep_ms(10);
	hz = rd(gmu.base + GMU_ALWAYS_ON_COUNTER_L) - before;
	absolutetime_to_nanoseconds(mach_absolute_time() - start, &nanos);
	hz = nanos ? (uint32_t)((uint64_t)hz * NSEC_PER_SEC / nanos) : 0;
	printf("adreno: %u: the GMU's counter counts at %u Hz: %s\n", step, hz, hz > 18000000 && hz < 20500000 ?
	    "the 19.2 MHz crystal: THE GPU BLOCK IS ALIVE" : "not the crystal's 19.2 MHz (firmware may start it): see the rest");
	for (uint32_t i = 0; i < 5; i++) {
		get(&gmu, "dtcm, where firmware has its versions", GMU_DTCM_VERSIONS + 4 * i);
	}

	announce("read the GPU's bus interface (0x3d0f000) and the CX odds and ends (0x3d9e000)");
	get(&gbif, "scache_cntl0", GBIF_SCACHE_CNTL0);
	get(&gbif, "halt", GBIF_HALT);
	get(&gbif, "halt_ack", GBIF_HALT_ACK);
	get(&cx_misc, "system_cache_cntl_0", CX_MISC_SYSTEM_CACHE_CNTL_0);
	get(&cx_misc, "system_cache_cntl_1", CX_MISC_SYSTEM_CACHE_CNTL_1);
	return true;
}

/* ---- step 7: the GMU's channel to RPMh ----------------------------------------------------------------- */

/*
 * dt: qcom,kgsl-3d0 "rscc" at 0x3de0000: the same kind of command slots through which this
 * kernel asks RPMh for the touch screen's supply, but the GMU's own; through them its firmware
 * raises the 3D core's rail. Offsets in words: a6xx_gmu.xml, domain A6XX_RSCC.
 */
static struct block rscc = { "rscc", 0x02c8d000, 0x2000 };      /* a6xx_reg.h: A6XX_GPU_RSCC_RSC_STATUS0_DRV0 0x23404 dwords from the GPU base */
#define RSCC_RSC_STATUS0_DRV0   (4 * 0x004)
#define RSCC_PDC_SLAVE_ID_DRV0  (4 * 0x00b)
#define RSCC_SEQ_BUSY_DRV0      (4 * 0x101)
#define RSCC_SEQ_MEM_0_DRV0     (4 * 0x180)
#define RSCC_TCS0_DRV0_STATUS   (4 * 0x346)
#define RSCC_ASLEEP             (1u << 16)      /* of rsc_status0: handed over to the always-on sequencer */
#define RSCC_DRV_CONFIG         0x00c
#define RSCC_SLOTS              0xd00           /* +0 interrupts enabled, +4 pending */
#define RSCC_SLOT_SIZE          0x2a0

/*
 * The channel's state and its four command slots (Linux's tcs.c: slot n at 0xd00 + 0x2a0 n:
 * +0x10 idle, +0x14 control -- bit 16 enables the slot, +0x18 status -- bit 0 says the last
 * transaction completed, which is what the vendor's a6xx_complete_rpmh_votes() waits for,
 * +0x1c the number of commands enabled, then 12 bytes a command from +0x30: number, address,
 * data, status).
 */
static void
rscc_show(void)
{
	get(&rscc, "drv config", RSCC_DRV_CONFIG);
	get(&rscc, "rsc_status0_drv0", RSCC_RSC_STATUS0_DRV0);
	get(&rscc, "seq_busy_drv0", RSCC_SEQ_BUSY_DRV0);
	get(&rscc, "slots' interrupts enabled", RSCC_SLOTS);
	get(&rscc, "slots' interrupts pending", RSCC_SLOTS + 4);
	for (uint32_t slot = 0; slot < 4; slot++) {
		vm_offset_t at = rscc.base + RSCC_SLOTS + RSCC_SLOT_SIZE * slot;

		printf("adreno: %u: rscc slot %u: wait mask %#x, control %#x, status %#x, commands enabled %#x; commands "
		    "(number, address, data, status):\n", step, slot, rd(at + 0x10), rd(at + 0x14), rd(at + 0x18),
		    rd(at + 0x1c));
		printf("adreno: %u:   %x %x %x %x | %x %x %x %x | %x %x %x %x | %x %x %x %x\n", step,
		    rd(at + 0x30), rd(at + 0x34), rd(at + 0x38), rd(at + 0x3c), rd(at + 0x44), rd(at + 0x48), rd(at + 0x4c),
		    rd(at + 0x50), rd(at + 0x58), rd(at + 0x5c), rd(at + 0x60), rd(at + 0x64), rd(at + 0x6c), rd(at + 0x70),
		    rd(at + 0x74), rd(at + 0x78));
	}
}

static bool
step_rscc(void)
{
	if (!cx_ready()) {
		return false;
	}
	announce("read the GMU's RPMh channel (0x3de0000)");
	get(&rscc, "rsc_status0_drv0", RSCC_RSC_STATUS0_DRV0);
	get(&rscc, "pdc_slave_id_drv0", RSCC_PDC_SLAVE_ID_DRV0);
	get(&rscc, "seq_busy_drv0", RSCC_SEQ_BUSY_DRV0);
	get(&rscc, "seq_mem_0_drv0", RSCC_SEQ_MEM_0_DRV0);
	get(&rscc, "tcs0_drv0_status", RSCC_TCS0_DRV0_STATUS);
	get(&gmu, "rscc_control_ack", GMU_RSCC_CONTROL_ACK);
	return true;
}

/* ---- steps 8 and 9: the SMMU --------------------------------------------------------------------------- */

/*
 * dt: kgsl-smmu@3da0000 (qcom,qsmmu-v500: an Arm MMU-500), 0x20000 long, with "qcom,skip-init":
 * the vendor's driver leaves its stream table as it finds it. Its streams (dt: qcom,kgsl-iommu
 * and qcom,gmu, "iommus"): 0 the GPU, 1 its low-priority twin, 2 secure content, 5 the GMU.
 * The global registers here are the ones the hypervisor imitates; offsets: arm-smmu.h.
 */
static struct block smmu = { "smmu", 0x02ca0000, 0x10000 };
#define SMMU_SCR0               0x000
#define SMMU_IDR0               0x020           /* [7:0] stream match registers */
#define SMMU_IDR1               0x024           /* bit 31: 64K pages, [30:28] log2(pages)-1, [7:0] translation contexts */
#define SMMU_IDR2               0x028
#define SMMU_IDR7               0x03c
#define SMMU_SGFSR              0x048
#define SMMU_SMR(n)             (0x800 + 4 * (n))       /* bit 31 valid, [30:16] mask, [14:0] stream */
#define SMMU_S2CR(n)            (0xc00 + 4 * (n))       /* [17:16] 0 translate 1 bypass 2 fault, [7:0] context */
#define SMMU_CBAR(n)            (4 * (n))               /* in the second page; [17:16] kind of context */
#define SMMU_CB_SCTLR           0x00            /* a context's own page: bit 0 = translation on */
#define SMMU_CB_TTBR0           0x20
#define SMMU_CB_TCR             0x30
#define SMMU_CB_FSR             0x58

static uint32_t smmu_page, smmu_pages, smmu_contexts, smmu_matches;

/* The layout of the SMMU's register space from its ID registers; false if it is not what the device tree says. */
static bool
smmu_layout(void)
{
	uint32_t idr0 = get(&smmu, "idr0", SMMU_IDR0), idr1 = get(&smmu, "idr1", SMMU_IDR1);

	smmu_page = idr1 >> 31 ? 0x10000 : 0x1000;
	smmu_pages = 2u << (idr1 >> 28 & 7);
	smmu_contexts = idr1 & 0xff;
	smmu_matches = idr0 & 0xff;
	printf("adreno: %u: SMMU: %u stream match registers, %u contexts, %u pages of %#x before the contexts' own\n", step,
	    smmu_matches, smmu_contexts, smmu_pages, smmu_page);
	if (2 * smmu_pages * smmu_page != smmu.size) {
		printf("adreno: %u: that makes %#x of registers, the device tree says %#x: not reading further\n", step,
		    2 * smmu_pages * smmu_page, smmu.size);
		return false;
	}
	return true;
}

static bool
step_smmu(void)
{
	if (!cx_ready()) {
		return false;
	}
	announce("read the ID registers of the GPU's SMMU (0x3da0000): global registers, which the hypervisor answers for");
	if (!smmu_layout()) {
		return false;
	}
	get(&smmu, "idr2", SMMU_IDR2);
	get(&smmu, "idr7", SMMU_IDR7);
	get(&smmu, "scr0", SMMU_SCR0);
	get(&smmu, "sgfsr", SMMU_SGFSR);

	announce("read the SMMU's stream table and the kinds of its contexts, as the boot loader and hypervisor left them");
	for (uint32_t i = 0; i < smmu_matches && i < 16; i++) {
		get(&smmu, "smr", SMMU_SMR(i));
		get(&smmu, "s2cr", SMMU_S2CR(i));
	}
	for (uint32_t i = 0; i < smmu_contexts && i < 16; i++) {
		get(&smmu, "cbar", smmu_page + SMMU_CBAR(i));
	}
	return true;
}

/*
 * Only context 0, the one both the vendor's driver and Linux give the GPU itself: of the others
 * at least one belongs to TrustZone (secure content), and not all can be expected to answer.
 */
static bool
step_smmu_context(void)
{
	uint32_t context;

	if (!cx_ready()) {
		return false;
	}
	announce("read translation context 0 of the SMMU: registers of the hardware itself, not the hypervisor's");
	if (!smmu_layout()) {
		return false;
	}
	context = smmu_pages * smmu_page;
	get(&smmu, "cb0 sctlr", context + SMMU_CB_SCTLR);
	get(&smmu, "cb0 ttbr0, low", context + SMMU_CB_TTBR0);
	get(&smmu, "cb0 ttbr0, high", context + SMMU_CB_TTBR0 + 4);
	get(&smmu, "cb0 tcr", context + SMMU_CB_TCR);
	get(&smmu, "cb0 fsr", context + SMMU_CB_FSR);
	return true;
}

/* ---- step 10: the GMU's memory -------------------------------------------------------------------------- */

/*
 * Firmware is loaded by writing it through these registers into the processor's memory while
 * the processor is held in reset (a6xx_gmu_fw_load). Two words of data memory go there and back,
 * and what was there is put back.
 */
static bool
step_gmu_memory(void)
{
	static const uint32_t pattern[2] = { 0x600d0001, 0x0a110002 };
	uint32_t was[2], back[2];

	if (!cx_ready()) {
		return false;
	}
	announce("hold the GMU's processor in reset, write two words into its data memory (0x3d71000), read them back");
	if (get(&gmu, "cm3_fw_init_result", GMU_CM3_FW_INIT_RESULT) == 0xbabeface) {
		printf("adreno: %u: firmware is running in the GMU: leaving its memory alone\n", step);
		return false;
	}
	put(&gmu, "cm3_sysreset", GMU_CM3_SYSRESET, 1);
	get(&gmu, "cm3_sysreset", GMU_CM3_SYSRESET);
	for (int i = 0; i < 2; i++) {
		was[i] = get(&gmu, "dtcm", GMU_DTCM + 4 * i);
	}
	for (int i = 0; i < 2; i++) {
		put(&gmu, "dtcm", GMU_DTCM + 4 * i, pattern[i]);
	}
	for (int i = 0; i < 2; i++) {
		back[i] = get(&gmu, "dtcm", GMU_DTCM + 4 * i);
	}
	for (int i = 0; i < 2; i++) {
		put(&gmu, "dtcm", GMU_DTCM + 4 * i, was[i]);
	}
	printf("adreno: %u: the GMU's memory %s\n", step, back[0] == pattern[0] && back[1] == pattern[1] ?
	    "TAKES WHAT IS WRITTEN: its firmware can be loaded" : "does not hold what was written");
	return back[0] == pattern[0] && back[1] == pattern[1];
}

/* ---- memory the GPU block reaches ------------------------------------------------------------------- */

/*
 * Whatever in the GPU block reads or writes memory -- the GMU's processor, later the 3D core --
 * does it through the SMMU, which makes of the address it uses ("the GPU's address" below) a
 * physical one by walking page tables of ours, as a processor's MMU does: here always three
 * levels of 4K pages, 39 bits of address (dt: qcom,use-3-lvl-tables). The SMMU has seven such
 * translations ("contexts") and a table of which stream of requests -- 0 is the 3D core, 5 the
 * GMU (dt: iommus) -- goes through which. Formats and the order of programming: Linux
 * drivers/iommu/io-pgtable-arm.c and arm-smmu.c.
 */
#define PTE_TABLE               3ull
#define PTE_PAGE                3ull
#define PTE_CACHED              (1ull << 2)     /* memory attribute 1 of SMMU_MAIR0; without it 0, uncached */
#define PTE_UNPRIVILEGED        (1ull << 6)     /* the 3D core's accesses are; the GMU's are not */
#define PTE_SHARED              (2ull << 8)
#define PTE_ACCESSED            (1ull << 10)
#define PTE_NOT_GLOBAL          (1ull << 11)
#define PTE_ADDRESS             0x0000fffffffff000ull
#define PTE_GMU                 (PTE_SHARED | PTE_ACCESSED | PTE_NOT_GLOBAL)

#define SMMU_S2CR_FAULT         (2u << 16)
#define SMMU_SMR_VALID          (1u << 31)
#define SMMU_GMU_STREAM_MASK    0x400           /* DT gmu_user/gmu_kernel */
#define SMMU_CORE_STREAM_MASK   0x401           /* PCLM10 DT gfx3d_user: iommus = <... 0x00 0x401> */
#define SMMU_CBAR_STAGE_1       (1u << 16 | 3u << 8 | 0xfu << 12)       /* our tables alone decide; weakest defaults */
#define SMMU_CBA2R(n)           (0x800 + 4 * (n))                       /* in the second page, like CBAR */
#define SMMU_CBA2R_64_BIT       1
#define SMMU_CB_TCR2            0x10
#define SMMU_CB_TTBR1           0x28
#define SMMU_CB_MAIR0           0x38
#define SMMU_CB_MAIR1           0x3c
#define SMMU_CB_FAR             0x60
#define SMMU_CB_FSYNR0          0x68
#define SMMU_CB_TLBIASID        0x610
#define SMMU_CB_TLBSYNC         0x7f0
#define SMMU_CB_TLBSTATUS       0x7f4
#define SMMU_SCTLR_ON           (1u << 0 | 1u << 1 | 1u << 2 | 1u << 5 | 1u << 12) /* on; faults recorded, no interrupt */
#define SMMU_TCR_39_BITS        (25u | 1u << 8 | 1u << 10 | 3u << 12 | 1u << 23)   /* one table of 4K pages, cached walks */
#define SMMU_TCR2               (1u | 1u << 4 | 7u << 15)       /* 36 bits of physical address, 16 of context tag */
#define SMMU_MAIR0              0xf404ff44      /* Linux's: 0 uncached, 1 cached, 2 device */
#define SMMU_FSR_ALL            0xc00001fe      /* every kind of fault, and "several" */

extern void loader_return(void);

/* Until every write to memory so far has been carried out. */
static void
settle(void)
{
	__asm__ volatile ("dsb sy");
}

/* Push what this processor has written to a stretch of memory out of its cache, and forget it: the GPU block reads memory. */
static void
to_memory(vm_offset_t start, vm_size_t size)
{
	for (vm_offset_t line = start & ~(vm_offset_t)63; line < start + size; line += 64) {
		__asm__ volatile ("dc civac, %0" : : "r" (line));
	}
	settle();
}

#define SPACE_TABLES            1024            /* 2 GB of addresses, were they all in use */

struct gpu_space {
	const char *name;
	uint32_t context;               /* which of the SMMU's */
	uint32_t control;               /* bits of its control register besides the usual */
	uint32_t table_count;
	uint64_t *root;
	struct {
		uint64_t phys;
		uint64_t *table;
		bool changed;           /* since it was last written out of this processor's cache (space_commit()) */
	} tables[SPACE_TABLES];
	uint64_t *last_entry, *last_table;      /* the level 2 entry looked up last, and its table */
	uint32_t last_changed;                  /* the table an entry was written in last */
};

static uint64_t *
space_new_table(struct gpu_space *space)
{
	static vm_offset_t spare;
	static unsigned spare_left;
	uint64_t *table;

	if (space->table_count == SPACE_TABLES) {
		return NULL;
	}
	if (spare_left == 0) {
		if (kmem_alloc(kernel_map, &spare, PAGE_SIZE, KMA_KOBJECT | KMA_ZERO | KMA_DATA, VM_KERN_MEMORY_IOKIT) !=
		    KERN_SUCCESS) {
			return NULL;
		}
		spare_left = PAGE_SIZE / 4096;
	}
	table = (uint64_t *)(spare + PAGE_SIZE - 4096 * spare_left--);
	space->tables[space->table_count].phys = kvtophys((vm_offset_t)table);
	space->tables[space->table_count++].table = table;
	return table;
}

/* The table an entry of a higher table leads to; made if the entry is empty. */
/* An entry of one of the tables has been written: that table is to go out to memory. */
static void
space_changed(struct gpu_space *space, const uint64_t *entry)
{
	if ((uintptr_t)entry - (uintptr_t)space->tables[space->last_changed].table < 4096) {
		space->tables[space->last_changed].changed = true;      /* page after page of one buffer */
		return;
	}
	for (uint32_t i = space->table_count; i-- > 0;) {
		if ((uintptr_t)entry - (uintptr_t)space->tables[i].table < 4096) {
			space->tables[i].changed = true;
			space->last_changed = i;
			return;
		}
	}
}

/* The SMMU reads the tables from memory, not from this processor's cache. */
static void
space_commit(struct gpu_space *space)
{
	for (uint32_t i = 0; i < space->table_count; i++) {
		if (space->tables[i].changed) {
			to_memory((vm_offset_t)space->tables[i].table, 4096);
			space->tables[i].changed = false;
		}
	}
}

static uint64_t *
space_next(struct gpu_space *space, uint64_t *entry)
{
	uint64_t *table;

	if (*entry & 1) {
		for (uint32_t i = 0; i < space->table_count; i++) {
			if (space->tables[i].phys == (*entry & PTE_ADDRESS)) {
				return space->tables[i].table;
			}
		}
		return NULL;
	}
	table = space_new_table(space);
	if (table != NULL) {
		*entry = kvtophys((vm_offset_t)table) | PTE_TABLE;
		space_changed(space, entry);
	}
	return table;
}

/* One 4K page. */
static bool
space_map(struct gpu_space *space, uint64_t address, uint64_t phys, uint64_t attributes)
{
	uint64_t *level2, *level3;

	if (address >> 39 || (space->root == NULL && (space->root = space_new_table(space)) == NULL)) {
		return false;
	}
	level2 = space_next(space, &space->root[address >> 30 & 0x1ff]);
	if (level2 == NULL) {
		return false;
	}
	if (space->last_entry == &level2[address >> 21 & 0x1ff]) {
		level3 = space->last_table;             /* page after page of one buffer: the same table */
	} else {
		level3 = space_next(space, &level2[address >> 21 & 0x1ff]);
		space->last_entry = &level2[address >> 21 & 0x1ff];
		space->last_table = level3;
	}
	if (level3 == NULL) {
		space->last_entry = NULL;
		return false;
	}
	level3[address >> 12 & 0x1ff] = phys == 0 && attributes == 0 ? 0 : phys | attributes | PTE_PAGE;
	space_changed(space, level3);
	return true;
}

/* Walk only our CPU-side page-table memory. This verifies the descriptors we
 * constructed, not the route actually selected by the GPU or SMMU. */
static bool
space_inspect(struct gpu_space *space, uint64_t address, uint64_t expected_phys)
{
	uint64_t *table = space->root, entry = 0;
	unsigned shifts[] = { 30, 21, 12 };

	for (unsigned level = 0; level < 3; level++) {
		if (table == NULL) {
			return false;
		}
		entry = table[(address >> shifts[level]) & 0x1ff];
		printf("adreno: %u: table walk VA %#llx level %u descriptor %#llx\n", step,
		    (unsigned long long)address, level + 1, (unsigned long long)entry);
		if ((entry & 3u) != 3u) {
			return false;
		}
		if (level < 2) {
			table = NULL;
			for (uint32_t i = 0; i < space->table_count; i++) {
				if (space->tables[i].phys == (entry & PTE_ADDRESS)) {
					table = space->tables[i].table;
					break;
				}
			}
		}
	}
	return (entry & PTE_ADDRESS) == (expected_phys & PTE_ADDRESS);
}

/* Memory of ours that the GPU block sees at an address of its own. */
struct gpu_block {
	uint64_t address;               /* the GPU's */
	uint32_t size;
	vm_offset_t memory;             /* ours */
};

static bool
block_new(struct gpu_space *space, struct gpu_block *block, uint64_t address, uint32_t size, uint64_t attributes)
{	block->address = address;
	block->size = (size + 4095) & ~4095u;
	if (kmem_alloc(kernel_map, &block->memory, round_page(block->size), KMA_KOBJECT | KMA_ZERO | KMA_DATA,
	    VM_KERN_MEMORY_IOKIT) != KERN_SUCCESS) {
		return false;
	}
	for (uint32_t offset = 0; offset < block->size; offset += 4096) {
		if (!space_map(space, address + offset, kvtophys(block->memory + offset), attributes)) {
			return false;
		}
	}
	to_memory(block->memory, block->size);
	printf("adreno: %u: %s sees %#llx..%#llx in memory of ours (%#llx)\n", step, space->name, address,
	    address + block->size - 1, kvtophys(block->memory));
	return true;
}

/*
 * The same, but the pages are physically contiguous, so block_uncached() can put one
 * write-combined alias over the whole block. The HFI table needs this: it is 0x40e0 bytes
 * (a 0xe0-byte table and four 4K queues) and both sides write words into it at the GMU's
 * report rate. A kmem_alloc() block is only virtually contiguous, and the alias would fail its
 * physical check.
 */
static bool
block_new_contig(struct gpu_space *space, struct gpu_block *block, uint64_t address, uint32_t size, uint64_t attributes)
{
	block->address = address;
	block->size = (size + 4095) & ~4095u;
	if (kmem_alloc_contig(kernel_map, &block->memory, round_page(block->size), PAGE_MASK, 0, 0,
	    KMA_KOBJECT | KMA_ZERO | KMA_DATA, VM_KERN_MEMORY_IOKIT) != KERN_SUCCESS) {
		return false;
	}
	for (uint32_t offset = 0; offset < block->size; offset += 4096) {
		if (!space_map(space, address + offset, kvtophys(block->memory + offset), attributes)) {
			return false;
		}
	}
	to_memory(block->memory, block->size);
	printf("adreno: %u: %s sees %#llx..%#llx in contiguous memory of ours (%#llx)\n", step,
	    space->name, address, address + block->size - 1, kvtophys(block->memory));
	return true;
}

/*
 * A second view of a block without this processor's cache in between, for memory that both
 * sides write a word at a time. Only blocks of one page of ours: those are in one piece.
 */
static vm_offset_t
block_uncached(const struct gpu_block *block)
{
	uint64_t phys = kvtophys(block->memory);

	for (uint32_t offset = 0; offset < block->size; offset += 4096) {
		if (kvtophys(block->memory + offset) != phys + offset) {
			return 0;
		}
	}
	return ml_io_map_wcomb(phys, block->size);
}

static uint32_t
smmu_context(const struct gpu_space *space)
{
	return (smmu_pages + space->context) * smmu_page;
}

/* What a context has recorded of a request it could not serve. */
static bool
space_faulted(const struct gpu_space *space)
{
	uint32_t context = smmu_context(space), status = rd(smmu.base + context + SMMU_CB_FSR);

	if ((status & SMMU_FSR_ALL) == 0) {             /* bits 10:9 only tell the format of the syndrome */
		return false;
	}
	printf("adreno: %u: THE SMMU REFUSED %s: status %#x, address %#llx, syndrome %#x\n", step, space->name, status,
	    (uint64_t)rd(smmu.base + context + SMMU_CB_FAR + 4) << 32 | rd(smmu.base + context + SMMU_CB_FAR),
	    rd(smmu.base + context + SMMU_CB_FSYNR0));
	return true;
}

/*
 * One more stream through a context that is on. A match is taken only as the device tree has
 * it, a stream and the mask 0x400. What the probe of stream 0 showed holds here too: the
 * hypervisor refuses any entry written with the 3D core's own stream, 0 or 1, and takes any
 * other -- the boot loader's table (the device tree says "qcom,skip-init") already routes the
 * core's streams and the hypervisor keeps it that way. So a refusal is reported and not
 * failed: the core's requests, or the fault its context records, will say whether it is so.
 */
static bool
space_stream(struct gpu_space *space, uint32_t match, uint32_t stream, uint32_t mask)
{
	uint32_t wanted = SMMU_SMR_VALID | mask << 16 | stream;

	put(&smmu, "s2cr", SMMU_S2CR(match), space->context);
	put(&smmu, "smr", SMMU_SMR(match), wanted);
	if (get(&smmu, "smr", SMMU_SMR(match)) != wanted ||
	    (get(&smmu, "s2cr", SMMU_S2CR(match)) & 0x300ff) != space->context) {
		printf("adreno: %u: the hypervisor did not grant stream %u going through context %u: entry %u reads "
		    "%#x and %#x; the boot loader's table may route it anyway\n", step, stream, space->context, match,
		    rd(smmu.base + SMMU_SMR(match)), rd(smmu.base + SMMU_S2CR(match)));
	}
	return true;
}

/*
 * Give a context our tables and send a stream through it; `match` is the entry of the stream
 * table to use. The context's own registers belong to the hardware and must take what is
 * written. The stream table's entries are the hypervisor's to grant, and what it grants for one
 * stream it may refuse for another -- stream 0's entry reads back zero however it is written,
 * while the GMU's was taken at once. So `grant_matters` tells whether a refusal ends the
 * attempt: the device tree says "qcom,skip-init", the real table was set up by the boot loader
 * and left, and an entry the hypervisor will not let us write may already route the stream
 * where we mean; the requests themselves are what will tell.
 */
static bool
space_on(struct gpu_space *space, uint32_t match, uint32_t stream, uint32_t mask, bool grant_matters)
{
	uint32_t context = smmu_context(space);
	uint64_t root = space->tables[0].phys | (uint64_t)space->context << 48;        /* tagged with the context's number */
	uint32_t wanted_smr = SMMU_SMR_VALID | mask << 16 | stream;

	for (uint32_t i = 0; i < space->table_count; i++) {
		to_memory((vm_offset_t)space->tables[i].table, 4096);
	}
	put(&smmu, "cb sctlr", context + SMMU_CB_SCTLR, 0);
	put(&smmu, "cba2r", smmu_page + SMMU_CBA2R(space->context), SMMU_CBA2R_64_BIT);
	put(&smmu, "cbar", smmu_page + SMMU_CBAR(space->context), SMMU_CBAR_STAGE_1);
	put(&smmu, "cb tcr2", context + SMMU_CB_TCR2, SMMU_TCR2);
	put(&smmu, "cb tcr", context + SMMU_CB_TCR, SMMU_TCR_39_BITS);
	put(&smmu, "cb ttbr0, low", context + SMMU_CB_TTBR0, (uint32_t)root);
	put(&smmu, "cb ttbr0, high", context + SMMU_CB_TTBR0 + 4, (uint32_t)(root >> 32));
	put(&smmu, "cb ttbr1, low", context + SMMU_CB_TTBR1, 0);
	put(&smmu, "cb ttbr1, high", context + SMMU_CB_TTBR1 + 4, 0);
	put(&smmu, "cb mair0", context + SMMU_CB_MAIR0, SMMU_MAIR0);
	put(&smmu, "cb mair1", context + SMMU_CB_MAIR1, 0);
	put(&smmu, "cb fsr", context + SMMU_CB_FSR, SMMU_FSR_ALL);
	put(&smmu, "cb tlbiasid", context + SMMU_CB_TLBIASID, space->context);
	put(&smmu, "cb tlbsync", context + SMMU_CB_TLBSYNC, 1);
	if (!wait_for(&smmu, context + SMMU_CB_TLBSTATUS, 1, 0, 100)) {
		printf("adreno: %u: the SMMU does not finish forgetting old translations (%#x)\n", step,
		    rd(smmu.base + context + SMMU_CB_TLBSTATUS));
		return false;
	}
	put(&smmu, "cb sctlr", context + SMMU_CB_SCTLR, SMMU_SCTLR_ON | space->control);
	put(&smmu, "s2cr", SMMU_S2CR(match), space->context);
	put(&smmu, "smr", SMMU_SMR(match), wanted_smr);

	/* the context's own registers are the hardware's: they have to take what is written */
	if ((get(&smmu, "cbar", smmu_page + SMMU_CBAR(space->context)) & 0x30000) != (SMMU_CBAR_STAGE_1 & 0x30000) ||
	    !(get(&smmu, "cb sctlr", context + SMMU_CB_SCTLR) & 1) ||
	    get(&smmu, "cb ttbr0, low", context + SMMU_CB_TTBR0) != (uint32_t)root ||
	    get(&smmu, "cb ttbr0, high", context + SMMU_CB_TTBR0 + 4) != (uint32_t)(root >> 32)) {
		printf("adreno: %u: the SMMU's context %u did not take the tables it was given\n", step, space->context);
		return false;
	}
	/* the global ones are the hypervisor's to grant: see what it made of them */
	if (get(&smmu, "smr", SMMU_SMR(match)) != wanted_smr ||
	    (get(&smmu, "s2cr", SMMU_S2CR(match)) & 0x300ff) != space->context) {
		printf("adreno: %u: the hypervisor did not grant stream %u going through context %u: entry %u reads "
		    "%#x and %#x\n", step, stream, space->context, match,
		    rd(smmu.base + SMMU_SMR(match)), rd(smmu.base + SMMU_S2CR(match)));
		if (grant_matters) {
			return false;
		}
	}
	get(&smmu, "cb tcr", context + SMMU_CB_TCR);
	get(&smmu, "cba2r", smmu_page + SMMU_CBA2R(space->context));
	get(&smmu, "cb tcr2", context + SMMU_CB_TCR2);
	get(&smmu, "cb fsr", context + SMMU_CB_FSR);
	get(&smmu, "sgfsr", SMMU_SGFSR);
	return true;
}

/* A stream goes nowhere again, and its context translates nothing. */
static void
space_off(struct gpu_space *space, uint32_t match)
{
	put(&smmu, "smr", SMMU_SMR(match), 0);
	put(&smmu, "s2cr", SMMU_S2CR(match), SMMU_S2CR_FAULT);
	put(&smmu, "cb sctlr", smmu_context(space) + SMMU_CB_SCTLR, 0);
}

/*
 * Experimental context-bank probe sharing the ordinary page tables. A CBAR hardware VMID is
 * not equivalent to the vendor's DOMAIN_ATTR_SECURE_VMID: the latter also controls secure
 * page-table memory ownership. Neither routing of the CP's requests to this bank nor its access
 * to those tables is established. Do not describe this experiment as a secure domain.
 */
static bool
space_cb_on(struct gpu_space *space, uint32_t context, uint32_t vmid)
{
	uint64_t root = space->tables[0].phys | (uint64_t)space->context << 48;
	uint32_t base = (smmu_pages + context) * smmu_page;

	for (uint32_t i = 0; i < space->table_count; i++) {
		to_memory((vm_offset_t)space->tables[i].table, 4096);
	}
	put(&smmu, "cb sctlr", base + SMMU_CB_SCTLR, 0);
	put(&smmu, "cba2r", smmu_page + SMMU_CBA2R(context), SMMU_CBA2R_64_BIT);
	put(&smmu, "cbar", smmu_page + SMMU_CBAR(context), SMMU_CBAR_STAGE_1 | (vmid & 0xffu));
	put(&smmu, "cb tcr2", base + SMMU_CB_TCR2, SMMU_TCR2);
	put(&smmu, "cb tcr", base + SMMU_CB_TCR, SMMU_TCR_39_BITS);
	put(&smmu, "cb ttbr0, low", base + SMMU_CB_TTBR0, (uint32_t)root);
	put(&smmu, "cb ttbr0, high", base + SMMU_CB_TTBR0 + 4, (uint32_t)(root >> 32));
	put(&smmu, "cb ttbr1, low", base + SMMU_CB_TTBR1, 0);
	put(&smmu, "cb ttbr1, high", base + SMMU_CB_TTBR1 + 4, 0);
	put(&smmu, "cb mair0", base + SMMU_CB_MAIR0, SMMU_MAIR0);
	put(&smmu, "cb mair1", base + SMMU_CB_MAIR1, 0);
	put(&smmu, "cb fsr", base + SMMU_CB_FSR, SMMU_FSR_ALL);
	put(&smmu, "cb tlbiasid", base + SMMU_CB_TLBIASID, space->context);
	put(&smmu, "cb tlbsync", base + SMMU_CB_TLBSYNC, 1);
	if (!wait_for(&smmu, base + SMMU_CB_TLBSTATUS, 1, 0, 100)) {
		printf("adreno: %u: context %u does not finish forgetting old translations (%#x)\n", step,
		    context, rd(smmu.base + base + SMMU_CB_TLBSTATUS));
		return false;
	}
	put(&smmu, "cb sctlr", base + SMMU_CB_SCTLR, SMMU_SCTLR_ON | space->control);
	if ((rd(smmu.base + smmu_page + SMMU_CBAR(context)) & 0xffu) != (vmid & 0xffu) ||
	    !(rd(smmu.base + base + SMMU_CB_SCTLR) & 1)) {
		printf("adreno: %u: context %u did not take the secure VMID %#x (cbar %#x, sctlr %#x)\n", step,
		    context, vmid, rd(smmu.base + smmu_page + SMMU_CBAR(context)),
		    rd(smmu.base + base + SMMU_CB_SCTLR));
		return false;
	}
	printf("adreno: %u: experimental context %u on: CBAR low byte %#x, tables from context %u\n", step, context,
	    vmid, space->context);
	return true;
}

/* ---- step 11: TrustZone -------------------------------------------------------------------------------- */

/*
 * The 3D core comes out of reset in "secure" mode, and on a phone only TrustZone lets it out: it
 * does when handed the vendor's signed zap shader to authenticate (a6xx_gpu.c: a6xx_hw_init;
 * qcom_scm.c: qcom_scm_pas_*; the GPU is image 13). Whether it will do that for this kernel
 * decides whether there can be a GPU at all. Questions that pass no memory show whether it
 * listens: the one Linux asks before anything else (is the call "is this call available"
 * available), whether the calls that authenticate an image are, and which images it says it
 * "supports". The last is asked of the GPU's and, for comparison, of images this phone is known
 * to start that way (dt: qcom,pas-id of the audio, modem and video firmware): no GPU driver asks
 * it, Linux's or the vendor's, so what the answer for the GPU means is not known beforehand.
 * A call is an SMC, which the hypervisor passes on; convention: qcom_scm-smc.c.
 */
#define SCM_FAST                (1ull << 31)            /* answered at once; without it a call may be preempted */
#define SCM_64                  (1ull << 30)
#define SCM_SIP                 (2ull << 24)            /* a call of the SoC maker's, not one of the Arm standard */
#define SCM_CALL(service, command) ((uint64_t)(service) << 8 | (command))
#define SCM_IS_CALL_AVAILABLE   SCM_CALL(6, 1)
#define SCM_PAS_INIT_IMAGE      SCM_CALL(2, 1)
#define SCM_PAS_AUTH_AND_RESET  SCM_CALL(2, 5)
#define SCM_PAS_SHUTDOWN        SCM_CALL(2, 6)
#define SCM_PAS_IS_SUPPORTED    SCM_CALL(2, 7)
#define SCM_ONE_VALUE           1                       /* the word describing the arguments: one, a plain number */
#define SCM_VALUE_AND_BUFFER    (2 | 2 << 6)            /* two: a number, and the address of memory it may read and write */
#define SCM_INTERRUPTED         1                       /* a call that was preempted says so, and is made again so */
#define PAS_GPU                 13

static uint64_t scm_answer2;            /* the second return word of the last SCM call */

/* One call. What TrustZone returns: 0 or an error of its own, and a word of answer. */
static int32_t
scm(const char *what, uint64_t function, uint64_t arguments, uint64_t first, uint64_t second, uint64_t third,
    uint64_t *answer)
{
	uint64_t session = 0;

	printf("adreno: %u: about to ask TrustZone %s: smc %#llx, arguments %#llx, %#llx, %#llx\n", step, what, function, first,
	    second, third);
	snprintf(last_write, sizeof(last_write), "step %u asked TrustZone %#llx(%#llx, %#llx)", step, function, first, second);
	hold(true);
	for (int i = 0; i < 100; i++) {
		register uint64_t x0 __asm__("x0") = i == 0 ? function : SCM_INTERRUPTED;
		register uint64_t x1 __asm__("x1") = arguments;
		register uint64_t x2 __asm__("x2") = first;
		register uint64_t x3 __asm__("x3") = second;
		register uint64_t x4 __asm__("x4") = third;
		register uint64_t x5 __asm__("x5") = 0;
		register uint64_t x6 __asm__("x6") = session;
		register uint64_t x7 __asm__("x7") = 0;

		__asm__ volatile ("smc #0" : "+r" (x0), "+r" (x1), "+r" (x2), "+r" (x3), "+r" (x4), "+r" (x5), "+r" (x6),
		    "+r" (x7) : : "x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17", "memory");
		session = x6;
		if (x0 != SCM_INTERRUPTED) {
			printf("adreno: %u: TrustZone returns %d, answer %#llx\n", step, (int32_t)x0, x1);
			*answer = x1;
			return (int32_t)x0;
		}
	}
	printf("adreno: %u: TrustZone keeps saying the call was interrupted\n", step);
	return -1;
}

/* Four-argument variant (x2..x5), for CP_SMMU_APERTURE. */
static int32_t
scm4(const char *what, uint64_t function, uint64_t arguments, uint64_t first, uint64_t second, uint64_t third,
    uint64_t fourth, uint64_t *answer)
{
	uint64_t session = 0;

	printf("adreno: %u: about to ask TrustZone %s: smc %#llx, arguments %#llx, %#llx, %#llx, %#llx\n", step, what,
	    function, first, second, third, fourth);
	snprintf(last_write, sizeof(last_write), "step %u asked TrustZone %#llx(%#llx)", step, function, first);
	for (int i = 0; i < 100; i++) {
		register uint64_t x0 __asm__("x0") = i == 0 ? function : SCM_INTERRUPTED;
		register uint64_t x1 __asm__("x1") = arguments;
		register uint64_t x2 __asm__("x2") = first;
		register uint64_t x3 __asm__("x3") = second;
		register uint64_t x4 __asm__("x4") = third;
		register uint64_t x5 __asm__("x5") = fourth;
		register uint64_t x6 __asm__("x6") = session;
		register uint64_t x7 __asm__("x7") = 0;

		__asm__ volatile ("smc #0" : "+r" (x0), "+r" (x1), "+r" (x2), "+r" (x3), "+r" (x4), "+r" (x5), "+r" (x6),
		    "+r" (x7) : : "x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17", "memory");
		session = x6;
		if (x0 != SCM_INTERRUPTED) {
			printf("adreno: %u: TrustZone returns %d, answer %#llx\n", step, (int32_t)x0, x1);
			*answer = x1;
			scm_answer2 = x2;
			return (int32_t)x0;
		}
	}
	printf("adreno: %u: TrustZone keeps saying the call was interrupted\n", step);
	return -1;
}

static bool
step_trustzone(void)
{
	static const struct {
		const char *name;
		uint32_t id;
	} images[] = { { "audio", 1 }, { "modem", 4 }, { "video", 9 }, { "GPU zap shader", PAS_GPU } };
	uint64_t answer = 0, how = SCM_FAST | SCM_64 | SCM_SIP;
	uint32_t fast[4] = { 0 }, slow[4] = { 0 };

	announce("ask TrustZone what Linux asks it first: is the call \"is this call available\" available");
	if (scm("whether it can be asked", how | SCM_IS_CALL_AVAILABLE, SCM_ONE_VALUE, SCM_SIP | SCM_IS_CALL_AVAILABLE, 0,
	    0, &answer) != 0 || answer != 1) {
		how = SCM_FAST | SCM_SIP;       /* the same with 32-bit arguments, Linux's second try */
		if (scm("the same, the 32-bit way", how | SCM_IS_CALL_AVAILABLE, SCM_ONE_VALUE, SCM_SIP | SCM_IS_CALL_AVAILABLE,
		    0, 0, &answer) != 0 || answer != 1) {
			printf("adreno: %u: TrustZone does not answer the way it answers Linux\n", step);
			return false;
		}
	}

	announce("ask TrustZone whether it has the calls that authenticate a firmware image");
	scm("for the call that takes an image's signature", how | SCM_IS_CALL_AVAILABLE, SCM_ONE_VALUE,
	    SCM_SIP | SCM_PAS_INIT_IMAGE, 0, 0, &answer);
	scm("for the call that authenticates and starts an image", how | SCM_IS_CALL_AVAILABLE, SCM_ONE_VALUE,
	    SCM_SIP | SCM_PAS_AUTH_AND_RESET, 0, 0, &answer);

	announce("ask TrustZone which images it \"supports\": three this phone starts through it, and the GPU's; each asked "
	    "both ways, at once and (as Linux does) as a call that may be preempted");
	for (unsigned i = 0; i < 4; i++) {
		answer = 0;
		fast[i] = scm(images[i].name, how | SCM_PAS_IS_SUPPORTED, SCM_ONE_VALUE, images[i].id, 0, 0, &answer) == 0 && answer;
		answer = 0;
		slow[i] = scm(images[i].name, (how & ~SCM_FAST) | SCM_PAS_IS_SUPPORTED, SCM_ONE_VALUE, images[i].id, 0,
		    0, &answer) == 0 && answer;
	}
	printf("adreno: %u: \"supported\", asked at once / as Linux asks: audio %u/%u, modem %u/%u, video %u/%u, GPU %u/%u\n",
	    step, fast[0], slow[0], fast[1], slow[1], fast[2], slow[2], fast[3], slow[3]);
	printf("adreno: %u: %s\n", step, fast[3] || slow[3] ? "TrustZone lists the GPU's zap shader" :
	    fast[0] || slow[0] || fast[1] || slow[1] || fast[2] || slow[2] ?
	    "TrustZone lists others but not the GPU's: step 12 will show whether that matters" :
	    "TrustZone lists none, not even those the phone starts: this question tells nothing here");
	return true;
}

/* ---- step 12: the zap shader's signature ----------------------------------------------------------------- */

extern dev_t mdevlookup(int devid);
extern int mdevgetkernelrange(dev_t dev, vm_offset_t *base, uint64_t *size);


/*
 * A file of the small image that travels with the kernel -- userland/build.sh puts what is in
 * fw-gpu/ into its directory fw/ -- found in the memory of the RAM disk itself: at this point of
 * the start that image is the root if there is no system image, and not mounted at all if there
 * is one (it is /dev/md1 then; see userland/src/stage0.c).
 */
static bool image_asked_quietly;        /* image_file() says nothing: is the file there at all */

extern const uint8_t *hakios_image_file(const char *dir, const char *name, uint64_t *size, int *disk_out);
static const uint8_t *
image_file(const char *directory, const char *name, uint64_t *size)
{
	int disk = -1;
	const uint8_t *data = hakios_image_file(directory, name, size, &disk);  /* (hakios.image.c) */

	if (data != NULL) {
		if (!image_asked_quietly) {
			printf("adreno: %u: %s/%s: %llu bytes in the image of /dev/md%d\n", step, directory, name, *size, disk);
		}
		return data;
	}
	if (!image_asked_quietly) {
		printf("adreno: %u: no %s/%s in the small image: put the phone's /vendor/firmware/%s into fw-gpu/ and run "
		    "userland/build.sh\n", step, directory, name, name);
	}
	return NULL;
}

/*
 * The first of the three calls with which Linux has a zap shader loaded (mdt_loader.c:
 * qcom_mdt_pas_init, qcom_scm.c: qcom_scm_pas_init_image): TrustZone is given the image's header --
 * the ELF header and the block of hashes and certificates, which is all of the file a640_zap.mdt
 * -- and checks the signature. Nothing of the GPU is involved, so it can be asked now. The other
 * two (where the image will be, and "authenticate and start") need the 3D core powered.
 *
 * The header goes in memory of ours, page-aligned and in one piece. Qualcomm's note at that
 * call says that for these calls the hypervisor itself arranges TrustZone's access to the memory
 * ("creates SHM bridges on behalf of Linux"), and that TrustZone protects the memory while it
 * works: so it is not touched again here, and never freed.
 */
/* The zap shader's header in a page of ours, as TrustZone wants it; where that page is in physical memory. */
static bool
zap_header(uint64_t *phys)
{
	const uint8_t *header;
	vm_offset_t page = 0;
	uint64_t size = 0;
	uint32_t segments, hashes;

	header = image_file("fw", "a640_zap.mdt", &size);
	if (header == NULL) {
		return false;
	}
	/* a 32-bit ELF: the count of segments at 44, the table of them at 52, 32 bytes each, the file size of one at +16 */
	segments = size >= 52 + 2 * 32 ? header[44] | header[45] << 8 : 0;
	if (memcmp(header, "\177ELF\1", 5) != 0 || segments < 2 || size > PAGE_SIZE) {
		printf("adreno: %u: that is not such a header (%02x %02x %02x %02x, %u segments)\n", step, header[0], header[1],
		    header[2], header[3], segments);
		return false;
	}
	memcpy(&hashes, header + 52 + 32 + 16, sizeof(hashes));
	printf("adreno: %u: an ELF header of %u segments, %u bytes with its table, then %llu bytes; by the table, %u of hashes\n",
	    step, segments, 52 + 32 * segments, size - (52 + 32 * segments), hashes);
	if (52 + 32 * segments + (uint64_t)hashes != size) {
		printf("adreno: %u: the file is not the header followed by the hashes alone: not what TrustZone takes\n", step);
		return false;
	}
	if (kmem_alloc(kernel_map, &page, PAGE_SIZE, KMA_KOBJECT | KMA_ZERO | KMA_DATA, VM_KERN_MEMORY_IOKIT) != KERN_SUCCESS) {
		printf("adreno: %u: no memory\n", step);
		return false;
	}
	memcpy((void *)page, header, size);
	to_memory(page, PAGE_SIZE);             /* TrustZone reads memory, not this processor's cache */
	*phys = kvtophys(page);
	return true;
}

static bool
step_zap_signature(void)
{
	uint64_t header = 0, answer = ~0ull;
	int32_t result;

	announce("find the zap shader's header, fw/a640_zap.mdt, in the small image that came with the kernel");
	if (!zap_header(&header)) {
		return false;
	}
	announce("hand the header to TrustZone to check its signature (image 13); a call that may be preempted, as Linux's is");
	result = scm("to take the zap shader's signature", SCM_64 | SCM_SIP | SCM_PAS_INIT_IMAGE, SCM_VALUE_AND_BUFFER, PAS_GPU,
	    header, 0, &answer);
	if (result == 0 && answer == 0) {
		printf("adreno: %u: TRUSTZONE ACCEPTS THE ZAP SHADER'S SIGNATURE FROM THIS KERNEL\n", step);
		return true;
	}
	printf("adreno: %u: TrustZone does not take it: the call returns %d, its answer is %#llx\n", step, result, answer);
	return false;
}

/* ---- step 13: memory for the GMU ------------------------------------------------------------------------ */

/*
 * The GMU is a Cortex-M3 with 16K of code memory and 16K of data memory of its own (written
 * through registers, step 10); the rest of its firmware and everything it shares with us it
 * reaches through the SMMU, at addresses the firmware file names: a file of blocks, each "this
 * goes at address A" or, with no data, "I need N bytes at address A" (vendor's
 * adreno_a6xx_gmu.c: a6xx_gmu_parse_fw, a6xx_gmu_load_fw). Besides those, where the vendor's
 * driver puts them: 16K for the firmware's log and a page for the table and queues of the
 * message protocol ("HFI").
 */
#define GMU_ITCM_ADDRESS        0x00000000u
#define GMU_DTCM_ADDRESS        0x00040000u     /* 080810 proved it: the vector table's stack pointer 0x43f80 is the
	 * data TCM's top word through this window (tcm+0x3f80), and 080116's task frames sat right below it
	 * (tcm+0x3f08, tcm+0x3a84); the 0x4000 block, by contrast, must sit in external memory or the firmware
	 * never comes up at all (074813 vs 080810). So this image's data TCM is 0x40000..0x43fff, and its
	 * initialized data is the 0x40000 block -- loaded through the window, as the vendor's load_gmu_fw
	 * routes its DTCM memdesc (adreno_a6xx_gmu.c:544-553, A6XX_GMU_CM3_DTCM_START). */
#define GMU_TCM_SIZE            0x4000u
#define GMU_LOG_ADDRESS         0x60010000u     /* the log, kept clear of the message table */
#define GMU_LOG_SIZE            0x4000u
#define GMU_HFI_ADDRESS         0x60000000u     /* the firmware reads its table here: its first read after start was 0x60000800 */
#define GMU_STREAM              5
#define GMU_MATCH               1               /* entry of the stream table; 0 is kept for the 3D core */

struct gmu_firmware_block {
	uint32_t address, size, type, value;
};
#define GMU_FIRMWARE_NEEDS      1               /* types of blocks without data: memory wanted, value = bytes */
#define GMU_FIRMWARE_NEEDS_KEPT 7

static struct gpu_space gmu_space = { .name = "the GMU", .context = 1 };
static struct gpu_block gmu_blocks[12], gmu_log, gmu_hfi, gmu_dtcm_alias;
static unsigned gmu_block_count;
static const uint8_t *gmu_firmware;
static uint64_t gmu_firmware_size;
static bool gmu_space_is_on, gmu_runs, gmu_talks, core_is_on, rsc_awake, cx_off;
static bool gpu_probe;
static void gmu_post_mortem(void);

static bool
in_tcm(uint32_t address)
{
	/* The code TCM at 0 and the data TCM at 0x40000 (see GMU_DTCM_ADDRESS), both through the
	 * GMU's own windows. Everything else the file carries -- the 0x4000 block and the 0x57000
	 * block -- the firmware's own map reaches over the AHB bus, so step 13 maps an external
	 * block for it. */
	return address - GMU_ITCM_ADDRESS < GMU_TCM_SIZE || address - GMU_DTCM_ADDRESS < GMU_TCM_SIZE;
}

static bool
step_gmu_space(void)
{
	if (!cx_ready()) {
		return false;
	}
	announce("find the GMU's firmware, fw/a640_gmu.bin, in the small image, and make the memory it asks for");
	gmu_firmware = image_file("fw", "a640_gmu.bin", &gmu_firmware_size);
	if (gmu_firmware == NULL) {
		return false;
	}
	for (uint64_t at = 0; at + sizeof(struct gmu_firmware_block) <= gmu_firmware_size;) {
		struct gmu_firmware_block block;

		memcpy(&block, gmu_firmware + at, sizeof(block));
		at += sizeof(block) + block.size;
		if (at > gmu_firmware_size) {
			printf("adreno: %u: the file ends inside a block\n", step);
			return false;
		}
		if (block.size != 0 || (block.type != GMU_FIRMWARE_NEEDS && block.type != GMU_FIRMWARE_NEEDS_KEPT) ||
		    in_tcm(block.address) ||
		    (block.address >= 0x4000u && (uint64_t) block.address + block.value <= 0x60000u)) {
			continue;
		}
		if (gmu_block_count == sizeof(gmu_blocks) / sizeof(gmu_blocks[0]) || (block.address & 4095) ||
		    block.value > 0x100000 || !block_new(&gmu_space, &gmu_blocks[gmu_block_count++], block.address, block.value,
		    PTE_GMU)) {
			printf("adreno: %u: cannot make %#x bytes at %#x for the firmware\n", step, block.value, block.address);
			return false;
		}
	}
	/* The a640's file also carries data blocks that the a660's NEEDS declarations would have
	 * covered -- the first one is at 0x40000, the GMU's own memory -- so give each such block a
	 * block of its own when nothing covers it yet. This has to happen here, before space_on()
	 * maps the GMU's address space, or the GMU could not reach the memory. */
	for (uint64_t at = 0; at + sizeof(struct gmu_firmware_block) <= gmu_firmware_size;) {
		struct gmu_firmware_block block;
		bool covered = false;

		memcpy(&block, gmu_firmware + at, sizeof(block));
		at += sizeof(block) + block.size;
		if (block.size == 0 || in_tcm(block.address) ||
		    (block.address >= 0x4000u && (uint64_t) block.address + block.size <= 0x60000u)) {
			continue;
		}
		for (unsigned i = 0; i < gmu_block_count; i++) {
			if (block.address >= gmu_blocks[i].address &&
			    (uint64_t) block.address + block.size <= gmu_blocks[i].address + gmu_blocks[i].size) {
				covered = true;
			}
		}
		if (covered) {
			continue;
		}
		uint32_t base = block.address & ~4095u;
		uint32_t size = (block.address - base + block.size + 4095u) & ~4095u;

		if (gmu_block_count == sizeof(gmu_blocks) / sizeof(gmu_blocks[0]) || block.size > 0x100000 ||
		    !block_new(&gmu_space, &gmu_blocks[gmu_block_count++], base, size, PTE_GMU)) {
			printf("adreno: %u: cannot make %#x bytes at %#x for the firmware\n", step, block.size, block.address);
			return false;
		}
	}
	/*
	 * The a640's firmware also reads and writes memory its file does not list: the first access
	 * after start was 0x58000, one page past the last block (0x57000, 0x308 bytes). That is the
	 * GMU's own runtime memory, between the firmware's data and the message table at
	 * 0x60002000; give it the range 0x58000..0x5ffff.
	 */
	/*
	 * No dummy page any more: the firmware reads its message table at 0x60000000 itself (its first
	 * read after start was 0x60000800, which the dummy page had answered with zeroes, leaving the
	 * table empty), so gmu_hfi, 16 KiB at that address, is both the table and that page.
	 */
	/*
	 * The firmware's external data: its own map sends 0x4000.. over the AHB bus, and 080810
	 * proved the firmware's initialized data at 0x4000 must be reached that way (without it
	 * there, FW_INIT_RESULT never turns). The firmware also uses runtime buffers in this range
	 * -- its first external access after start was a write to 0x5098 -- and unless the SMMU
	 * maps the range the write faults and stops it. The 0x40000 and 0x43fe0 blocks are not
	 * here: they are the data TCM's contents, loaded in step 14 through the GMU's registers.
	 */
	/* The CM3's external memory, 0x4000..0x5ffff, one block: the probe wrote 0xdeadbeef
	 * through 0x4000 and it appeared here, not in the data-TCM window. The firmware's data
	 * blocks (0x4000, 0x57000) are copied into this same memory, and the gaps (0x55000 and
	 * 0x58000..0x5ffff) are its runtime buffers; a second mapping over any of it would
	 * replace the code or the stack, which is what the old 0x4000..0x5ffff alias did when
	 * the blocks were allocated separately. */
	if (!block_new(&gmu_space, &gmu_dtcm_alias, 0x4000u, 0x5c000u, PTE_GMU)) {   /* 0x4000..0x5ffff */
		printf("adreno: %u: cannot map the GMU's external data range\n", step);
		return false;
	}
	if (!block_new(&gmu_space, &gmu_log, GMU_LOG_ADDRESS, GMU_LOG_SIZE, PTE_GMU) ||
	    !block_new_contig(&gmu_space, &gmu_hfi, GMU_HFI_ADDRESS, 0x5000u, PTE_GMU)) {
		/* The message table is 0xe0 bytes of header plus four 4K queues
		 * (kgsl_hfi.h: HFI_QUEUE_SIZE SZ_4K, HFI_QUEUE_MAX 4): 0x40e0 bytes,
		 * rounded to 0x5000. One page was mapped before, so the firmware read
		 * its command queue at 0x60000800 and then faulted on the message
		 * queue at 0x600010e0 -- which is why FW_INIT_RESULT stayed 0. */
		return false;
	}

	{
		uint32_t cb0 = 0;

		/* Probe kgsl's real model: one GPU page table (context bank 0)
		 * for the GMU and the core alike, instead of CB1 for the GMU. */
		if (PE_parse_boot_argn("adreno_gmu_cb0", &cb0, sizeof(cb0)) && cb0) {
			gmu_space.context = 0;
			printf("adreno: %u: the GMU and the core will share context bank 0\n", step);
		}
	}
	announce("give the SMMU's context 1 these tables and send the GMU's requests (stream 5) through it: the first "
	    "writes to the SMMU, the global ones (cbar, cba2r, s2cr, smr) through the hypervisor");
	if (!smmu_layout()) {
		return false;
	}
	gmu_space_is_on = true;         /* whatever of it has been written by the time something fails is taken back */
	return space_on(&gmu_space, GMU_MATCH, GMU_STREAM, SMMU_GMU_STREAM_MASK, true);
}

static struct block pdc = { "pdc", 0x0b290000, 0x10000 };       /* dt: qcom,gmu "kgsl_gmu_pdc_cfg" */
static struct block pdc_seq = { "pdc_seq", 0x0b490000, 0x10000 }; /* dt: qcom,gmu "kgsl_gmu_pdc_seq" */
#define PDC_GPU_ENABLE          (4 * 0x1140)
#define PDC_GPU_SEQ_START_ADDR  (4 * 0x1148)
#define PDC_TCS1_CONTROL        (4 * 0x1572)
#define PDC_TCS1_ENABLE         (4 * 0x1573)
#define PDC_TCS1_WAIT           (4 * 0x1574)
#define PDC_TCS1_CMD0_MSGID     (4 * 0x1575)
#define PDC_TCS1_CMD0_ADDR      (4 * 0x1576)
#define PDC_TCS1_CMD0_DATA      (4 * 0x1577)
#define PDC_TCS3_CONTROL        (4 * 0x15d6)
#define PDC_TCS3_ENABLE         (4 * 0x15d7)
#define PDC_TCS3_WAIT           (4 * 0x15d8)
#define PDC_TCS3_CMD0_MSGID     (4 * 0x15d9)
#define PDC_TCS3_CMD0_ADDR      (4 * 0x15da)
#define PDC_TCS3_CMD0_DATA      (4 * 0x15db)
#define PDC_CMD_OFFSET          (4 * 4)                 /* PDC_CMD_OFFSET 4 dwords between commands */
#define RSCC_PDC_SEQ_START_ADDR (4 * 0x008)
#define RSCC_PDC_MATCH_VALUE_LO (4 * 0x009)
#define RSCC_PDC_MATCH_VALUE_HI (4 * 0x00a)
#define RSCC_HIDDEN_TCS_CMD0_ADDR (4 * 0x00d)
#define RSCC_HIDDEN_TCS_CMD0_DATA (4 * 0x00e)
#define RSCC_OVERRIDE_START_ADDR (4 * 0x100)

/*
 * The RPMh side of the GMU, before its firmware is loaded: the vendor's a6xx_gmu_fw_start() calls
 * _load_gmu_rpmh_ucode() first, and only then load_gmu_fw() (adreno_a6xx_gmu.c:1019-1040). The GPU's
 * part of the always-on sequencer (pdc) and the GMU channel's sleep and wake program (rscc) are what
 * its firmware casts its votes through, so they are in place before it runs, not after.
 */
static bool
rpmh_ucode(void)
{
	/* The phone's own a6xx_gmu.c (_load_gmu_rpmh_ucode, adreno_a6xx_gmu.c:160-280): the RSC
	 * sleep/wake program and the PDC sequence it runs, both for this a640. The mainline program
	 * this driver carried before was another SoC's. */
	static const uint32_t rscc_program[5] = {
		0xa7a506a0u, 0xa1e6a6e7u, 0xa2e081e1u, 0xe9a982e2u, 0x0020e8a8u,
	};
	static const uint32_t pdc_program[5] = {
		0xfebea1e1u, 0xa5a4a3a2u, 0x8382a6e0u, 0xbce3e284u, 0x002081fcu,
	};

	announce("enable the GPU's part of the always-on sequencer (pdc, 0xb290000 and 0xb490000) and give the GMU's RPMh "
	    "channel its sleep and wake program (rscc, 0x2c8d000)");
	put(&rscc, "rsc_status0_drv0", RSCC_RSC_STATUS0_DRV0, 1u << 24);
	put(&rscc, "pdc_slave_id_drv0", RSCC_PDC_SLAVE_ID_DRV0, 1);
	put(&rscc, "hidden_tcs_cmd0_data", RSCC_HIDDEN_TCS_CMD0_DATA, 0);
	put(&rscc, "hidden_tcs_cmd0_addr", RSCC_HIDDEN_TCS_CMD0_ADDR, 0);
	put(&rscc, "hidden_tcs_cmd1_data", RSCC_HIDDEN_TCS_CMD0_DATA + 4 * 2, 0);
	put(&rscc, "hidden_tcs_cmd1_addr", RSCC_HIDDEN_TCS_CMD0_ADDR + 4 * 2, 0);
	put(&rscc, "hidden_tcs_cmd2_data", RSCC_HIDDEN_TCS_CMD0_DATA + 4 * 4, 0x80000000);
	put(&rscc, "hidden_tcs_cmd2_addr", RSCC_HIDDEN_TCS_CMD0_ADDR + 4 * 4, 0);
	put(&rscc, "override_start_addr", RSCC_OVERRIDE_START_ADDR, 0);
	put(&rscc, "pdc_seq_start_addr", RSCC_PDC_SEQ_START_ADDR, 0x4520);
	put(&rscc, "pdc_match_value_lo", RSCC_PDC_MATCH_VALUE_LO, 0x4510);
	put(&rscc, "pdc_match_value_hi", RSCC_PDC_MATCH_VALUE_HI, 0x4514);
	for (unsigned i = 0; i < 5; i++) {
		put(&rscc, "seq_mem_drv0", RSCC_SEQ_MEM_0_DRV0 + 4 * i, rscc_program[i]);
	}
	get(&rscc, "seq_mem_0_drv0", RSCC_SEQ_MEM_0_DRV0);

	for (unsigned i = 0; i < 5; i++) {
		put(&pdc_seq, "seq mem", 4 * i, pdc_program[i]);
	}
	put(&pdc, "tcs1 control", PDC_TCS1_CONTROL, 0);
	put(&pdc, "tcs1 enable", PDC_TCS1_ENABLE, 7);
	put(&pdc, "tcs1 wait", PDC_TCS1_WAIT, 0);
	put(&pdc, "tcs1 cmd0 msgid", PDC_TCS1_CMD0_MSGID, 0x10108);
	put(&pdc, "tcs1 cmd0 addr", PDC_TCS1_CMD0_ADDR, 0x30010);
	put(&pdc, "tcs1 cmd0 data", PDC_TCS1_CMD0_DATA, 1);
	put(&pdc, "tcs1 cmd1 msgid", PDC_TCS1_CMD0_MSGID + PDC_CMD_OFFSET, 0x10108);
	put(&pdc, "tcs1 cmd1 addr", PDC_TCS1_CMD0_ADDR + PDC_CMD_OFFSET, 0x30000);
	put(&pdc, "tcs1 cmd1 data", PDC_TCS1_CMD0_DATA + PDC_CMD_OFFSET, 0);
	put(&pdc, "tcs1 cmd2 msgid", PDC_TCS1_CMD0_MSGID + 2 * PDC_CMD_OFFSET, 0x10108);
	put(&pdc, "tcs1 cmd2 addr", PDC_TCS1_CMD0_ADDR + 2 * PDC_CMD_OFFSET, 0x30090);
	put(&pdc, "tcs1 cmd2 data", PDC_TCS1_CMD0_DATA + 2 * PDC_CMD_OFFSET, 0);
	put(&pdc, "tcs3 control", PDC_TCS3_CONTROL, 0);
	put(&pdc, "tcs3 enable", PDC_TCS3_ENABLE, 7);
	put(&pdc, "tcs3 wait", PDC_TCS3_WAIT, 0);
	put(&pdc, "tcs3 cmd0 msgid", PDC_TCS3_CMD0_MSGID, 0x10108);
	put(&pdc, "tcs3 cmd0 addr", PDC_TCS3_CMD0_ADDR, 0x30010);
	put(&pdc, "tcs3 cmd0 data", PDC_TCS3_CMD0_DATA, 2);
	put(&pdc, "tcs3 cmd1 msgid", PDC_TCS3_CMD0_MSGID + PDC_CMD_OFFSET, 0x10108);
	put(&pdc, "tcs3 cmd1 addr", PDC_TCS3_CMD0_ADDR + PDC_CMD_OFFSET, 0x30000);
	put(&pdc, "tcs3 cmd1 data", PDC_TCS3_CMD0_DATA + PDC_CMD_OFFSET, 3);
	put(&pdc, "tcs3 cmd2 msgid", PDC_TCS3_CMD0_MSGID + 2 * PDC_CMD_OFFSET, 0x10108);
	put(&pdc, "tcs3 cmd2 addr", PDC_TCS3_CMD0_ADDR + 2 * PDC_CMD_OFFSET, 0x30090);
	put(&pdc, "tcs3 cmd2 data", PDC_TCS3_CMD0_DATA + 2 * PDC_CMD_OFFSET, 3);
	put(&pdc, "gpu_seq_start_addr", PDC_GPU_SEQ_START_ADDR, 0);
	put(&pdc, "gpu_enable", PDC_GPU_ENABLE, 0x80000001);
	get(&pdc, "gpu_enable", PDC_GPU_ENABLE);

	/* Vendor _load_gmu_rpmh_ucode() does not force these slots into AMC mode.
	 * Sleep/wake slots belong to the sequencer; only an active transfer's owner
	 * selects AMC mode (Linux rpmh-rsc.c: __tcs_set_trigger). Leave their mode
	 * to the GMU rather than assigning all four to the host. */
	return true;
}

/* ---- step 14: the GMU's firmware ----------------------------------------------------------------------- */

#define GMU_ITCM                GMU_WORD(0x0c00)

static bool
step_gmu_firmware(void)
{
	uint32_t wrong = 0;

	if (!cx_ready() || gmu_firmware == NULL) {
		return false;
	}
	/* TCM retention first, as a6xx_gmu_fw_start()'s cold boot does: the TCMs keep what the load
	 * (and the zeroing below) puts in them through the processor's reset. */
	put(&gmu, "general_7", GMU_GENERAL_7, 1);
	if (!rpmh_ucode()) {
		return false;
	}
	announce("load the GMU's firmware: its first 16K of code and of data through the GMU's registers (0x3d6d000, "
	    "0x3d71000), the rest into the memory just made");
	put(&gmu, "cm3_sysreset", GMU_CM3_SYSRESET, 1);
	/* The TCMs hold whatever the last boot left, and the file fills only part of them: a firmware
	 * whose .bss is garbage does not get far. Zero both windows before the blocks go in. */
	for (uint32_t i = 0; i < GMU_TCM_SIZE / 4; i++) {
		wr(gmu.base + GMU_ITCM + 4 * i, 0);
		wr(gmu.base + GMU_DTCM + 4 * i, 0);
	}
	settle();
	for (int pass = 0; pass < 2; pass++) {
		for (uint64_t at = 0; at + sizeof(struct gmu_firmware_block) <= gmu_firmware_size;) {
			struct gmu_firmware_block block;
			const uint8_t *data = gmu_firmware + at + sizeof(block);

			memcpy(&block, gmu_firmware + at, sizeof(block));
			at += sizeof(block) + block.size;
			if (block.size == 0) {
				continue;
			}
			/* The TCMs first, as the vendor's load_gmu_fw does through its ITCM and
			 * DTCM memdescs (adreno_a6xx_gmu.c:544-553): the 0x40000 block is the
			 * firmware's initialized data in its data TCM, and the 0x43fe0 block is
			 * the version record at that TCM's top. The proof of where the TCM is:
			 * the vector table's stack pointer 0x43f80 is tcm+0x3f80 through this
			 * window, and 080116's task frames (return addresses into the ITCM code,
			 * stack canary deadba55) sat right below it. The 0x4000 block is not
			 * TCM: 080810 wrote it through the window and the firmware never came
			 * up, while 074813 had it in external memory and FW_INIT_RESULT turned
			 * 0xbabeface. */
			/* the end is worked out in 64 bits: a block whose address and size wrap past 4 GB is not "inside the TCM" and is not written */
			if (in_tcm(block.address) && (uint64_t)block.address + block.size <= 0x100000000ull &&
			    in_tcm(block.address + block.size - 1)) {
				uint32_t registers = block.address < GMU_DTCM_ADDRESS ? GMU_ITCM + block.address - GMU_ITCM_ADDRESS :
				    GMU_DTCM + block.address - GMU_DTCM_ADDRESS;

				for (uint32_t offset = 0; offset + 4 <= block.size; offset += 4) {
					uint32_t word;

					memcpy(&word, data + offset, sizeof(word));
					if (pass == 0) {
						wr(gmu.base + registers + offset, word);
					} else if (rd(gmu.base + registers + offset) != word) {
						wrong++;
					}
				}
				if (pass == 1) {
					printf("adreno: %u: %#x bytes at the GMU's %#x, through its registers\n", step, block.size,
					    block.address);
				}
				continue;
			}
			/* The rest -- the 0x4000 and 0x57000 blocks -- is the firmware's
			 * external memory: its own map sends those addresses over the AHB bus
			 * and the SMMU into the memory below (080810: without the 0x4000 block
			 * there the firmware does not come up at all; live pointers to
			 * 0x57000.. sit in its task frames). */
			if (block.address >= gmu_dtcm_alias.address &&
			    (uint64_t) block.address + block.size <= gmu_dtcm_alias.address + gmu_dtcm_alias.size) {
				memcpy((void *)(gmu_dtcm_alias.memory + (block.address - gmu_dtcm_alias.address)), data, block.size);
				to_memory(gmu_dtcm_alias.memory, gmu_dtcm_alias.size);
				if (pass == 1) {
					printf("adreno: %u: %#x bytes at the GMU's %#x, in its external memory\n", step,
					    block.size, block.address);
				}
				continue;
			}
			for (unsigned i = 0; pass == 0 && i <= gmu_block_count; i++) {
				if (i == gmu_block_count) {
					printf("adreno: %u: the firmware has %#x bytes for %#x, where it asked for no memory\n", step,
					    block.size, block.address);
					return false;
				}
				/* (64-bit sum: with 32 bits a block that wraps would pass and the copy would run past the block's memory) */
				if (block.address >= gmu_blocks[i].address &&
				    (uint64_t)block.address + block.size <= gmu_blocks[i].address + gmu_blocks[i].size) {
					memcpy((void *)(gmu_blocks[i].memory + (block.address - gmu_blocks[i].address)), data, block.size);
					to_memory(gmu_blocks[i].memory, gmu_blocks[i].size);
					printf("adreno: %u: %#x bytes at the GMU's %#x, in memory\n", step, block.size, block.address);
					break;
				}
			}
		}
	}
	snprintf(last_write, sizeof(last_write), "step %u wrote the GMU's firmware into its code and data memory", step);
	if (gpu_probe) {
		/*
		 * A restartable probe, not the firmware: the reset vector stays 0x00043f80/0x9, but
		 * from 0x8 the processor now runs
		 *     ldr r0, [pc, #12]   ; 0xdeadbeef
		 *     ldr r1, [pc, #16]   ; 0x4000, its data TCM
		 *     str r0, [r1]
		 *     b   .
		 * If the marker is in the data TCM after the release, the processor executes; if it
		 * is not, no driver register is worth another guess.
		 */
		static const uint32_t stub[] = {
			0x49044803u, 0xe7fe6008u, 0xbf00bf00u, 0xbf00bf00u,
			0xdeadbeefu, 0x00004000u,
		};
		for (unsigned i = 0; i < sizeof(stub) / sizeof(stub[0]); i++) {
			wr(gmu.base + GMU_ITCM + 0x8 + 4 * i, stub[i]);
		}
		settle();
		printf("adreno: %u: the CM3 probe is in: its first instructions write 0xdeadbeef to its data TCM\n", step);
	}
	if (wrong) {
		printf("adreno: %u: %u words of the GMU's own memory do not read back as written\n", step, wrong);
		return false;
	}
	printf("adreno: %u: THE GMU'S FIRMWARE IS LOADED; its versions, read back from the GMU: core %#x, power %#x, HFI %#x\n",
	    step, rd(gmu.base + GMU_DTCM_VERSIONS), rd(gmu.base + GMU_DTCM_VERSIONS + 8), rd(gmu.base + GMU_DTCM_VERSIONS + 16));
	return true;
}

/*
 * The firmware's version block: a640_gmu.bin's block at 0x43fe0 (0x14 bytes) is the top of its
 * data TCM, and step 14 loads it through the GMU's register window (GMU_DTCM + 0x3fe0 -- the
 * same words GMU_DTCM_VERSIONS reads). Reading them back is the load's own check; if the
 * firmware is up it may also rewrite them itself.
 */
static void
gmu_version_show(void)
{
	printf("adreno: %u: the version block at 0x43fe0, through the data TCM's window: %#x %#x %#x %#x\n",
	    step, rd(gmu.base + GMU_DTCM_VERSIONS), rd(gmu.base + GMU_DTCM_VERSIONS + 4),
	    rd(gmu.base + GMU_DTCM_VERSIONS + 8), rd(gmu.base + GMU_DTCM_VERSIONS + 12));
}

/* ---- step 15: start the GMU ---------------------------------------------------------------------------- */

#define GMU_CX_GMU_WFI_CONFIG   GMU_WORD(0x5002)         /* a6xx XML 0x1f802 from the GPU base = 0x5002 from the GMU block */
#define GMU_ICACHE_CONFIG       GMU_WORD(0x4c00)
#define GMU_DCACHE_CONFIG       GMU_WORD(0x4c01)
#define GMU_SYS_BUS_CONFIG      GMU_WORD(0x4c0f)
#define GMU_PWR_COL_INTER_FRAME_CTRL GMU_WORD(0x50c0)
#define GMU_PWR_COL_SPTPRAC_HYST  GMU_WORD(0x50c2)   /* a6xx_reg.h: A6XX_GMU_PWR_COL_SPTPRAC_HYST 0x1f8c2 */
#define GMU_GX_SPTPRAC_POWER_CONTROL GMU_WORD(0x0081) /* a6xx_reg.h: A6XX_GMU_GX_SPTPRAC_POWER_CONTROL 0x1a881 */
#define GMU_RPMH_CTRL           GMU_WORD(0x50e8)
#define GMU_RPMH_CTRL_ALL       0xf11           /* the RPMh interface, and every vote the firmware may cast */
#define GMU_CX_FAL_INTF         GMU_WORD(0x50f0)
#define GMU_CX_FALNEXT_INTF     GMU_WORD(0x50f1)
#define GMU_PWR_COL_CP_MSG      GMU_WORD(0x5100)        /* at start: where the log is, and its pages - 1 */
#define GMU_PWR_COL_CP_RESP     GMU_WORD(0x5101)        /* ... and how far it was written before */
#define GMU_HFI_SFR_ADDR        GMU_WORD(0x5182)        /* at start: which GPU this is */
#define GMU_HFI_QTBL_INFO       GMU_WORD(0x5184)
#define GMU_HFI_QTBL_ADDR       GMU_WORD(0x5185)
#define GMU_HFI_CTRL_INIT       GMU_WORD(0x5186)
#define GMU_GMU2HOST_INTR_CLR   GMU_WORD(0x5191)
#define GMU_GMU2HOST_INTR_INFO  GMU_WORD(0x5192)        /* bit 0: a message for us; bit 31: the 3D core is ours */
#define GMU_HOST2GMU_INTR_SET   GMU_WORD(0x5194)        /* bit 0: a message for the GMU; bit 30, 31: see step 17 */
/* The rest of the same pair of mailboxes, for when a message goes unanswered (a6xx_gmu.xml.h,
 * Linux a6xx_gmu.xml.h: the mask of the GMU's side, and the raw view and enables of ours). */
#define GMU_GMU2HOST_INTR_MASK  GMU_WORD(0x5193)
#define GMU_HOST2GMU_INTR_RAW   GMU_WORD(0x5196)
#define GMU_HOST2GMU_INTR_EN_0  GMU_WORD(0x5197)
#define GMU_RSCC_CONTROL_REQ    GMU_WORD(0x9307)
#define GMU_AO_AHB_FENCE_CTRL   GMU_WORD(0x9310)
#define GMU_AHB_FENCE_RANGE_0   GMU_WORD(0x9311)
#define GMU_CHIP                0x06040100      /* dt qcom,chipid 0x6040001 the way the firmware wants it: major | minor << 12 | patch << 8 */
#define GMU_STARTED_MASK        0x1ff           /* of fw_init_result, for firmware newer than core 0x20010004 */
#define GMU_STARTED             0x100
#define GMU_FAILED              0xe00

/* The table at the head of the HFI page and the three queues after it, as the vendor's driver lays them out. */
struct hfi_queue {
	uint32_t status, start, type, words, message_words, dropped, rx_watermark, tx_watermark,
	    rx_request, tx_request, read, write;
};
struct hfi_table {
	uint32_t version, words, first_queue, queue_words, queues, active_queues;
	struct hfi_queue queue[17];
};
#define HFI_QUEUES              4               /* cmd, msg, dbg, and a dispatch queue the firmware leaves disabled */
#define HFI_QUEUE_WORDS         1024
#define HFI_QUEUE_AT(n)         (0xe0 + 4096 * (n))     /* the vendor's HFI_QUEUE_OFFSET: ALIGN(table,16) + i*4K */

static volatile struct hfi_table *hfi;  /* the uncached view */

/* Defined after the steps that use them here (the early-GX experiment). */
static bool rail_vote(uint32_t level, const uint16_t *rail, unsigned rail_levels, const uint16_t *mx,
    unsigned mx_levels, uint32_t *vote);
static bool gx_switch_close(const char *why);

/*
 * Everything the vendor's driver writes before it lets the processor go (a6xx_gmu_register_config,
 * a6xx_gmu_power_config), in its order; then out of reset (a6xx_gmu_device_start).
 */
static bool
step_gmu_start(void)
{
	static const struct { uint8_t id, priority, enabled; } queue[HFI_QUEUES] = {
		{ 0, 10, 1 },   /* cmd */
		{ 1, 10, 1 },   /* msg */
		{ 2, 40, 1 },   /* dbg */
		{ 3, 20, 0 },   /* dispatch: defined, disabled (kgsl_hfi.h:54-57, kgsl_hfi.c:204-209) */
	};
	uint32_t result = 0;

	announce("start the GMU: the HFI table, the RPMh channel, and out of reset");
	if (get(&rscc, "rsc_status0_drv0 (0x2c8d000)", RSCC_RSC_STATUS0_DRV0) != 0xffffffff) {
		printf("adreno: %u: the GMU's RPMh channel at 0x2c8d000 answers\n", step);
	}

	if (!cx_ready() || !gmu_space_is_on) {
		return false;
	}
	hfi = (volatile struct hfi_table *)block_uncached(&gmu_hfi);
	if (hfi == NULL) {
		printf("adreno: %u: the page for the messages is not in one piece\n", step);
		return false;
	}
	hfi->words = 6 + HFI_QUEUES * (sizeof(struct hfi_queue) / 4);   /* table header + its queue headers, in words */
	hfi->first_queue = 6;
	hfi->queue_words = sizeof(struct hfi_queue) / 4;
	hfi->queues = hfi->active_queues = HFI_QUEUES;
	for (unsigned i = 0; i < HFI_QUEUES; i++) {
		hfi->queue[i].start = GMU_HFI_ADDRESS + HFI_QUEUE_AT(i);
		hfi->queue[i].type = queue[i].id | (uint32_t) queue[i].priority << 8;
		hfi->queue[i].words = HFI_QUEUE_WORDS;
		hfi->queue[i].status = queue[i].enabled;
		hfi->queue[i].rx_watermark = hfi->queue[i].tx_watermark = 1;
		hfi->queue[i].rx_request = 1;
	}

	announce("tell the GMU where things are, by its registers: the message table, the log, which GPU this is; its "
	    "caches and its use of RPMh; then let its processor out of reset");
	if (get(&gmu, "cm3_sysreset", GMU_CM3_SYSRESET) != 1) {
		printf("adreno: %u: the GMU's processor is not held in reset: not configuring it while it runs\n", step);
		return false;
	}
	/*
	 * Our own leaving (gpu_block_stop) hands the GMU's RPMh channel to the always-on sequencer, and
	 * until it is taken back nothing the GMU asks of RPMh gets there: the vendor's
	 * a6xx_rscc_wakeup_sequence, before the firmware starts.
	 */
	rscc_show();
	if (rd(rscc.base + RSCC_RSC_STATUS0_DRV0) & RSCC_ASLEEP) {
		put(&gmu, "rscc_control_req", GMU_RSCC_CONTROL_REQ, 2);
		if (!wait_for(&gmu, GMU_RSCC_CONTROL_ACK, 2, 2, 100) || !wait_for(&rscc, RSCC_SEQ_BUSY_DRV0, ~0u, 0, 100)) {
			printf("adreno: %u: the GMU's RPMh channel does not wake\n", step);
		}
		get(&gmu, "rscc_control_ack", GMU_RSCC_CONTROL_ACK);
		put(&gmu, "rscc_control_req", GMU_RSCC_CONTROL_REQ, 0);
		get(&rscc, "rsc_status0_drv0", RSCC_RSC_STATUS0_DRV0);
		get(&rscc, "seq_busy_drv0", RSCC_SEQ_BUSY_DRV0);
	}
	rsc_awake = true;
	/* A640 vendor a6xx_gmu_fw_start does not program the A650 FAL10
	 * interface. These writes came from the original A660 port; keep a
	 * boot-arg-controlled A/B comparison before changing the default. */
	uint32_t no_fal = 0;
	PE_parse_boot_argn("adreno_no_fal", &no_fal, sizeof(no_fal));
	if (!no_fal) {
		put(&gmu, "cx_falnext_intf", GMU_CX_FALNEXT_INTF, 1);
		put(&gmu, "cx_fal_intf", GMU_CX_FAL_INTF, 1);
	} else {
		printf("adreno: %u: A640 comparison: leave the A650-only FAL10 interface untouched\n", step);
	}
	put(&gmu, "general_7", GMU_GENERAL_7, 1);
	put(&gmu, "cm3_fw_init_result", GMU_CM3_FW_INIT_RESULT, 0);
	put(&gmu, "cm3_boot_config", GMU_CM3_BOOT_CONFIG, 2);
	put(&gmu, "hfi_qtbl_addr", GMU_HFI_QTBL_ADDR, GMU_HFI_ADDRESS);
	put(&gmu, "hfi_qtbl_info", GMU_HFI_QTBL_INFO, 1);
	/* Vendor/mainline a6xx: BIT(31) | upper(0xa) << 18 | lower(0xa0).
	 * The old value here (0x80a008a0) had the upper field shifted by two
	 * and used the a7xx lower bound, which may have fenced the GMU's own
	 * writes to GPUCC. */
	put(&gmu, "ahb_fence_range_0", GMU_AHB_FENCE_RANGE_0, 0x802800a0u);
	/* The AO AHB fence must be in ALLOW mode for the processor to reach the memory
	 * outside its TCM (its stack is at 0x43f80). The vendor puts it there around its
	 * power transitions; the boot path here had never written it. */
	put(&gmu, "ao_ahb_fence_ctrl", GMU_AO_AHB_FENCE_CTRL, 0);
	put(&gmu, "cm3_cfg", GMU_CM3_CFG, 0x4052);
	get(&gmu, "cm3_cfg", GMU_CM3_CFG);
	get(&gmu, "cm3_boot_config", GMU_CM3_BOOT_CONFIG);
	put(&gbif, "halt", GBIF_HALT, 0);
	put(&gmu, "pwr_col_cp_resp", GMU_PWR_COL_CP_RESP, 0);
	put(&gmu, "hfi_sfr_addr", GMU_HFI_SFR_ADDR, GMU_CHIP);
	put(&gmu, "pwr_col_cp_msg", GMU_PWR_COL_CP_MSG, GMU_LOG_ADDRESS | (GMU_LOG_SIZE / 4096 - 1));
	put(&gmu, "sys_bus_config", GMU_SYS_BUS_CONFIG, 1);
	put(&gmu, "icache_config", GMU_ICACHE_CONFIG, 1);
	put(&gmu, "dcache_config", GMU_DCACHE_CONFIG, 1);
	/*
	 * The idle settings of the vendor's a6xx_gmu_power_config(), which it makes before the
	 * processor comes out of reset. The device tree has no qcom,gpu-ifpc, so the vendor's
	 * idle level here is GPU_HW_SPTP_PC: the SPTP RAM's hysteresis, and the SPTP enable
	 * bits (BIT(2) | BIT(0), kgsl_gmu.h:36) on top of the inter-frame control's value.
	 * Those two bits are what tells the firmware that powering the 3D core's retention
	 * RAM is its job: without them it has kept that RAM clamped in every run (sptprac_-
	 * pwr_clk_status 0x80, 055623 and 061311 alike), and its "start the core firmware"
	 * task never got as far as writing a single RPMh vote.
	 */
	put(&gmu, "pwr_col_sptprac_hyst", GMU_PWR_COL_SPTPRAC_HYST, 0x000a1680);  /* GMU_PWR_COL_HYST, adreno_a6xx_gmu.c:287 */
	/* Diagnostic only: compare the vendor's unconditional 0x9c40400
	 * baseline against its SPTP_PC branch's 0x9c40405. On this phone bit2
	 * and SPTPRAC_HYST never read back; a firmware-side OOB hang may depend
	 * on entering SPTP_PC without a usable retention control. Do not set
	 * this boot arg on a working GPU without a fresh hardware run. */
	uint32_t sptp_off = 0;
	PE_parse_boot_argn("adreno_sptp_off", &sptp_off, sizeof(sptp_off));
	put(&gmu, "pwr_col_inter_frame_ctrl", GMU_PWR_COL_INTER_FRAME_CTRL,
	    sptp_off ? 0x9c40400 : 0x9c40405);
	get(&gmu, "pwr_col_sptprac_hyst", GMU_PWR_COL_SPTPRAC_HYST);
	get(&gmu, "pwr_col_inter_frame_ctrl", GMU_PWR_COL_INTER_FRAME_CTRL);
	set(&gmu, "rpmh_ctrl", GMU_RPMH_CTRL, GMU_RPMH_CTRL_ALL);
	put(&gmu, "gmu2host_intr_clr", GMU_GMU2HOST_INTR_CLR, 0xffffffff);

	gmu_runs = true;
	put(&gmu, "wfi_config", GMU_CX_GMU_WFI_CONFIG, 0);
	/*
	 * Vendor order: the GX rail and its GDSC are host-owned and are on long before
	 * the GMU firmware boots (a6xx_gmu_fw_start runs with the GPU regulator already
	 * enabled). This driver instead collapsed GX in step 4 and only re-opened it in
	 * step 17, after the firmware was already running -- and its core-start path then
	 * never enabled the 3D clock. Probe the vendor order: vote gfx.lvl and close the
	 * GX switch before CM3 leaves reset.
	 */
	{
		uint32_t early = 0;

		if (PE_parse_boot_argn("adreno_gx_early", &early, sizeof(early)) && early) {
			uint16_t gfx[16], mx[16];
			unsigned gfx_levels = 0, mx_levels = 0;
			uint32_t cell = rpmh_resource("gfx.lvl", gfx, &gfx_levels);
			uint32_t vote = 0;

			if (cell != 0 && rpmh_resource("mx.lvl", mx, &mx_levels) &&
			    rail_vote(64, gfx, gfx_levels, mx, mx_levels, &vote)) {
				if (!tcs_send(cell, vote)) {
					printf("adreno: %u: early gfx.lvl vote did not complete\n", step);
				}
			}
			gx_switch_close("early: vendor keeps GX on before the GMU boots");
		}
	}
	put(&gmu, "cm3_sysreset", GMU_CM3_SYSRESET, 0);
	get(&gmu, "cm3_sysreset", GMU_CM3_SYSRESET);
	get(&gmu, "ao_ahb_fence_status", GMU_AHB_FENCE_STATUS);
	/*
	 * The a640's firmware says it started by writing 0xBABEFACE into FW_INIT_RESULT, and says it
	 * failed with 0xBADD FACE (the subagent's disassembly of a640_gmu.bin: the successful task at
	 * 0x720c writes the first, the error path at 0x1ec4 the second). The 0x100 mask this driver
	 * used is the a660's convention and would have taken a started firmware for a failed one.
	 */
	for (int i = 0; i < 5000 && result != 0xbabefaceu && result != 0xbaddfaceu; i++) {
		sleep_us(100);
		result = rd(gmu.base + GMU_CM3_FW_INIT_RESULT);
	}
	get(&gmu, "cm3_fw_init_result", GMU_CM3_FW_INIT_RESULT);
	if (gpu_probe) {
		uint32_t window = rd(gmu.base + GMU_DTCM);
		uint32_t alias = 0;

		if (gmu_dtcm_alias.memory != 0) {
			/* The CM3's 0x4000 may be the external alias, not the data-TCM window. */
			to_memory(gmu_dtcm_alias.memory, 64);
			alias = ((volatile uint32_t *) gmu_dtcm_alias.memory)[0];
		}
		printf("adreno: %u: the CM3 probe's marker: data-TCM window %#x, external alias at 0x4000 %#x -- %s\n",
		    step, window, alias, (window == 0xdeadbeefu || alias == 0xdeadbeefu) ?
		    "THE PROCESSOR IS RUNNING" : "the processor never wrote it");
	}
	/* If the processor ran and faulted on a translation, the SMMU says where; no fault
	 * means it never reached its first load. */
	(void)space_faulted(&gmu_space);
	get(&gmu, "gmu2host_intr_info", GMU_GMU2HOST_INTR_INFO);
	get(&gmu, "ao_host_interrupt_status", GMU_AO_HOST_INTERRUPT_STATUS);
	get(&gmu, "hfi_ctrl_status", GMU_HFI_CTRL_STATUS);
	gmu_version_show();
	if (result == 0xbaddfaceu) {
		printf("adreno: %u: THE GMU'S FIRMWARE SAYS IT FAILED (FW_INIT_RESULT 0xbaddface): its own start path "
		    "refused something the host set up\n", step);
		gmu_post_mortem();
		return false;
	}
	if (space_faulted(&gmu_space) || result != 0xbabefaceu) {
		printf("adreno: %u: the GMU's firmware does not come up (FW_INIT_RESULT %#x)\n", step, result);
		gmu_post_mortem();
		return false;
	}
	printf("adreno: %u: THE GMU'S FIRMWARE RUNS\n", step);
	return true;
}

/* ---- step 16: the GMU's way to RPMh, and its message queues ------------------------------------------------ */

/*
 * The GMU raises and drops the 3D core's supply by itself, through command slots of its own
	 * (step 7) and a sequencer in the always-on part of the SoC, "PDC". Step 14 loads their
	 * programs before releasing CM3 reset, following vendor a6xx_gmu_fw_start(). Do not
	 * rewrite the sequencers or their slot controls after the firmware has started.
 */
static bool
step_gmu_queues(void)
{
	if (!cx_ready() || !gmu_runs) {
		return false;
	}
	announce("ask the GMU to start its message queues");
	put(&gmu, "hfi_ctrl_init", GMU_HFI_CTRL_INIT, 1);
	if (!wait_for(&gmu, GMU_HFI_CTRL_STATUS, 1, 1, 500)) {
		get(&gmu, "hfi_ctrl_status", GMU_HFI_CTRL_STATUS);
		get(&gmu, "cm3_fw_init_result", GMU_CM3_FW_INIT_RESULT);
		get(&gmu, "gmu2host_intr_info", GMU_GMU2HOST_INTR_INFO);
		/* The firmware writes its version into the data TCM early: if these still hold the file's
		 * data, it never ran its first instructions. (a6xx: GMU_CORE_FW_VERSION at the DTCM's end.) */
		get(&gmu, "dtcm version 0", GMU_DTCM_VERSIONS);
		get(&gmu, "dtcm version 1", GMU_DTCM_VERSIONS + 4);
		get(&gmu, "dtcm version 2", GMU_DTCM_VERSIONS + 8);
		space_faulted(&gmu_space);
		printf("adreno: %u: the GMU does not start its message queues\n", step);
		return false;
	}
	get(&gmu, "hfi_ctrl_status", GMU_HFI_CTRL_STATUS);
	get(&gmu, "hfi_version_info", GMU_HFI_VERSION_INFO);
	gmu_talks = true;
	printf("adreno: %u: THE GMU'S MESSAGE QUEUES ARE UP\n", step);
	return !space_faulted(&gmu_space);
}

/* ---- step 17: talk to the GMU; the 3D core powered ---------------------------------------------------------- */

/*
 * A message is words: the first says which (bits 7:0), how many words (15:8), a request or an
 * answer (19:16), and a number (31:20) that the answer repeats in its second word; the third word
 * of an answer is the GMU's verdict, 0 for done. Vendor's adreno_a6xx_hfi.c, Linux a6xx_hfi.c.
 */
#define HFI_FW_VERSION          1               /* "which HFI do you speak": kgsl_hfi.h:215-218 */
#define HFI_BW_TABLE            3
#define HFI_PERF_TABLE          4
#define HFI_ACD_TABLE           7               /* the ACD table, and the feature that turns it on */
#define HFI_START               10
#define HFI_FEATURE_CTRL        11
#define HFI_CORE_FW_START       14
#define HFI_GX_BW_PERF_VOTE     30
#define HFI_PREPARE_SLUMBER     33
#define HFI_ERROR               100
#define HFI_ANSWER              1               /* in bits 19:16 */
#define HFI_NO_INDEX            0xff
/* The features the GMU's firmware knows (vendor kgsl_hfi.h:79-92, a6xx_hfi.c:866-889). */
#define HFI_FEATURE_ACD         12
#define HFI_FEATURE_ECP         1
/* The ACD table's fixed header values (vendor kgsl_hfi.h:278-290). */
#define HFI_ACD_VERSION         1
#define HFI_ACD_STRIDE          2
#define HFI_ACD_LEVELS          6
/* What we claim to speak, in the version message's second word (vendor's GMU_VERSION(major, minor):
 * major << 28 | minor << 16). kgsl_hfi.c:631-670 only warns when the firmware's answer differs, so
 * any value is safe to ask with; the firmware's own HFI here is 2.1 (hfi_version_info 0x20000001). */
#define GMU_VER_MAJOR           2
#define GMU_VER_MINOR           1
#define GMU_VERSION             (GMU_VER_MAJOR << 28 | GMU_VER_MINOR << 16)

struct hfi_perf_table {
	uint32_t header, gpu_levels, gmu_levels;
	struct {
		uint32_t vote, acd, khz;
	} gpu[16];
	struct {
		uint32_t vote, khz;
	} gmu[4];
};
struct hfi_bw_table {
	uint32_t header, levels, cnoc_commands, ddr_commands, cnoc_wait, ddr_wait;
	uint32_t cnoc_address[6], cnoc_data[2][6], ddr_address[8], ddr_data[16][8];
};

static uint32_t hfi_number;
static bool post_mortem_done;
static uint32_t hfi_last_answer[8];       /* the last answer the GMU gave, for its sender to read past the verdict */

/* Everything waiting in a queue of the GMU's; true if the answer to the message numbered `number` was among it. */
static bool
hfi_receive(unsigned which, uint32_t number, uint32_t *verdict)
{
	volatile struct hfi_queue *queue = &hfi->queue[which];
	volatile uint32_t *data = (volatile uint32_t *)((vm_offset_t)hfi + HFI_QUEUE_AT(which));
	bool answered = false;

	while (queue->read != queue->write) {
		uint32_t message[8] = { 0 }, at = queue->read % HFI_QUEUE_WORDS, words = data[at] >> 8 & 0xff;

		for (uint32_t i = 0; i < words; i++) {
			if (i < 8) {
				message[i] = data[at];
			}
			at = (at + 1) % HFI_QUEUE_WORDS;
		}
		queue->read = (at + 3) / 4 * 4 % HFI_QUEUE_WORDS;
		if ((message[0] >> 16 & 0xf) == HFI_ANSWER && message[1] >> 20 == number) {
			*verdict = message[2];
			memcpy(hfi_last_answer, message, sizeof(hfi_last_answer));
			answered = true;
		} else {
			message[7] = 0;
			if ((message[0] & 0xff) == HFI_ERROR) {         /* vendor's adreno_a6xx_receive_err_req */
				printf("adreno: %u: THE GMU REPORTS AN ERROR: %u, %u, \"%s\"\n", step, message[1] >> 16,
				    message[1] & 0xffff, (const char *)&message[2]);
			} else {
				printf("adreno: %u: the GMU says (queue %u, %u words): %#x %#x %#x %#x %#x %#x\n", step, which, words,
				    message[0], message[1], message[2], message[3], message[4], message[5]);
			}
		}
		if (words == 0) {
			break;
		}
	}
	return answered;
}

/* A word of the GMU's own data, by the GMU's address for it; only what lies in memory of ours. */
static uint32_t
gmu_data(uint32_t address)
{
	for (unsigned i = 0; i < gmu_block_count; i++) {
		if (address - (uint32_t)gmu_blocks[i].address < gmu_blocks[i].size - 3) {
			vm_offset_t at = gmu_blocks[i].memory + (address - (uint32_t)gmu_blocks[i].address);

			to_memory(at, 4);               /* what the GMU wrote, not what our cache remembers */
			return *(volatile uint32_t *)at;
		}
	}
	return 0xdeadbeef;
}

/*
 * The always-on side of the GMU: the interrupt lines the firmware's processor can raise at the
 * host, and the AHB fence that stops its bus requests when the CX half is away. The vendor
 * unmasks three of them -- the watchdog, the fence error and the AHB bus error (adreno.c:3456,
 * GMU_AO_INT_MASK) -- and the watchdog is what every silent round since 055623 has ended with;
 * which of the others came with it says whether the processor stopped by itself or was stopped
 * by the bus.
 */
static void
gmu_ao_state(const char *when)
{
	uint32_t status = rd(gmu.base + GMU_AO_HOST_INTERRUPT_STATUS);

	printf("adreno: %u: %s: ao_host_interrupt_status %#x", step, when, status);
	if (status & GMU_INT_WDOG_BITE) {
		printf(", the GMU's watchdog (its processor stopped feeding it)");
	}
	if (status & GMU_INT_RSCC_COMP) {
		printf(", an RPMh command completed");
	}
	if (status & GMU_INT_FENCE_ERR) {
		printf(", a fenced bus request was refused");
	}
	if (status & GMU_INT_DBD_WAKEUP) {
		printf(", a debug wakeup");
	}
	if (status & GMU_INT_HOST_AHB_BUS_ERR) {
		printf(", the host's AHB path faulted");
	}
	printf("; enable %#x, mask %#x, fence control %#x, fence status %#x, rbbm interrupts %#x, cx busy %#x\n",
	    rd(gmu.base + GMU_AO_INTERRUPT_EN), rd(gmu.base + GMU_AO_HOST_INTERRUPT_MASK),
	    rd(gmu.base + GMU_AO_AHB_FENCE_CTRL), rd(gmu.base + GMU_AHB_FENCE_STATUS),
	    rd(gmu.base + GMU_RBBM_INT_UNMASKED_STATUS), rd(gmu.base + GMU_AO_GPU_CX_BUSY_STATUS));
}

/*
 * Where the firmware's processor is standing when it has stopped answering. Its stack lives in
 * the external memory the alias maps (the firmware's own block table puts it at 0x43f80): the
 * image's blocks do not include it, so gmu_data reads nothing there (071637: every word came
 * back 0xdeadbeef), and the processor's own TCM windows are not readable while it runs (the
 * vector table through the ITCM window read 0x9). The vector table is read from the image
 * itself -- its first word is the stack the firmware began with, its second the address it
 * started executing at -- and the live stack from the alias. From the stack top down, the
 * words that look like code are the chain of calls it is sitting in; the raw words go too,
 * because a blocked task's frame holds more than return addresses. This is a scan, not a walk.
 */
#define GMU_STACK_ADDRESS       0x43f80u
#define GMU_STACK_BOTTOM        0x40000u

/* Four snapshots around core-start. The shared Thumb routine at ITCM
 * 0x2c70 reads and writes DTCM 0x402a0, and a return address 0x2c8f
 * appeared in a task frame. Its zero branch rejoins a call and returns;
 * it is NOT a waiting loop. Compare live DTCM observations without
 * mistaking ITCM image offset 0x2a0 for this address's initial value. */
static void
gmu_core_wait_probe(const char *phase)
{
	vm_offset_t at = gmu.base + GMU_DTCM + 0x2a0;

	printf("adreno: %u: core-state %s: dtcm[0x402a0..0x402af] %#x %#x %#x %#x, "
	    "NMI control %#x, gx status %#x, hfi cmd %u/%u ack %u/%u\n", step, phase,
	    rd(at), rd(at + 4), rd(at + 8), rd(at + 12),
	    rd(gmu.base + GMU_NMI_CONTROL_STATUS),
	    rd(gmu.base + GMU_SPTPRAC_PWR_CLK_STATUS),
	    hfi->queue[0].read, hfi->queue[0].write,
	    hfi->queue[1].read, hfi->queue[1].write);
	/* Vendor's HWCG path toggles GX_SPTPRAC_CLOCK_CONTROL bit 0 (GMU word
	 * 0x80); GX_SPTPRAC_POWER_CONTROL is word 0x81. Read both, plus the
	 * power-counter enable, before and after the OOB request. */
	printf("adreno: %u: core-state %s: gx_sptprac_clock %#x, gx_sptprac_power %#x, power_counter %#x, "
	    "pwr_col_keepalive %#x\n", step, phase,
	    rd(gmu.base + GMU_WORD(0x0080)), rd(gmu.base + GMU_WORD(0x0081)),
	    rd(gmu.base + GMU_WORD(0x5040)), rd(gmu.base + GMU_WORD(0x50c3)));
}

static void
gmu_stack_probe(void)
{
	uint32_t start_sp = 0, reset_vector = 0, found = 0, line[8], at;
	volatile uint32_t *stack = NULL;
	unsigned skip = 0;

	for (uint64_t off = 0; off + sizeof(struct gmu_firmware_block) <= gmu_firmware_size;) {
		struct gmu_firmware_block block;

		memcpy(&block, gmu_firmware + off, sizeof(block));
		if (block.address == 0 && block.size >= 8) {
			const uint8_t *data = gmu_firmware + off + sizeof(block);

			memcpy(&start_sp, data, 4);
			memcpy(&reset_vector, data + 4, 4);
			break;
		}
		off += sizeof(block) + block.size;
	}
	printf("adreno: %u: the firmware's vector table (from its image): stack began at %#x, its first instruction is %#x\n",
	    step, start_sp, reset_vector);
	if (gmu_dtcm_alias.memory != 0 && GMU_STACK_ADDRESS >= gmu_dtcm_alias.address &&
	    GMU_STACK_ADDRESS < gmu_dtcm_alias.address + gmu_dtcm_alias.size) {
		stack = (volatile uint32_t *)(gmu_dtcm_alias.memory +
		    (GMU_STACK_ADDRESS - gmu_dtcm_alias.address));
	}
	if (stack == NULL) {
		printf("adreno: %u:   its memory is not mapped, so its stack cannot be read\n", step);
		return;
	}
	if (start_sp >= GMU_STACK_BOTTOM && start_sp <= GMU_STACK_ADDRESS) {
		skip = (GMU_STACK_ADDRESS - start_sp) / 4;
	} else {
		start_sp = GMU_STACK_ADDRESS;   /* the block table's stack top will have to do */
	}
	/* Nothing the cache might still remember from before the firmware booted. */
	to_memory((vm_offset_t)(stack - (GMU_STACK_ADDRESS - GMU_STACK_BOTTOM) / 4),
	    GMU_STACK_ADDRESS - GMU_STACK_BOTTOM + 4);
	printf("adreno: %u: the %u words below its stack pointer, as they stand:\n", step, 32);
	for (unsigned back = 0; back < 32; back += 8) {
		printf("adreno: %u:   %#x: %x %x %x %x %x %x %x %x\n", step, start_sp - back * 4,
		    stack[-(int)(skip + back)], stack[-(int)(skip + back + 1)], stack[-(int)(skip + back + 2)],
		    stack[-(int)(skip + back + 3)], stack[-(int)(skip + back + 4)], stack[-(int)(skip + back + 5)],
		    stack[-(int)(skip + back + 6)], stack[-(int)(skip + back + 7)]);
	}
	printf("adreno: %u: words in its stack that look like return addresses (an address it would go back to):\n", step);
	for (unsigned back = 0; back <= (GMU_STACK_ADDRESS - GMU_STACK_BOTTOM) / 4 && found < 64; back++) {
		at = start_sp - back * 4;
		uint32_t word = stack[-(int)(skip + back)];

		if ((word < 0x8000u && (word & 1)) || (word >= 0x40000u && word < 0x60000u && (word & 1))) {
			if (found % 8 == 0) {
				if (found != 0) {
					printf("adreno: %u:   %#x: %x %x %x %x %x %x %x %x\n", step, at - 4 * 7,
					    line[0], line[1], line[2], line[3], line[4], line[5], line[6], line[7]);
				}
				for (unsigned i = 0; i < 8; i++) {
					line[i] = 0;
				}
			}
			line[found % 8] = word;
			found++;
		}
	}
	if (found == 0) {
		printf("adreno: %u:   none: the stack holds no addresses the processor would return to\n", step);
	}
	/*
	 * The data TCM itself, through the GMU's register window (the vendor's
	 * A6XX_GMU_CM3_DTCM_START, a6xx_reg.h:959), which is the processor's
	 * 0x40000..0x43fff: the vector table's stack pointer 0x43f80 is this window's
	 * tcm+0x3f80, and 080116's task frames sat right below it -- while the 0x4000
	 * block, which 080810 wrote here and stopped the firmware dead, belongs in
	 * external memory. The scan is kept because it is the direct check that the
	 * 0x40000 block is in the TCM now and the task stacks still run above it: with
	 * the load right, tcm+0..0x1b1f hold the firmware's initialized data (not zero)
	 * and its frames stay just below tcm+0x3f80.
	 */
	{
		uint32_t hits = 0;

		printf("adreno: %u: words in its data TCM (through the register window) that look like return addresses:\n", step);
		for (uint32_t off = 0; off < GMU_TCM_SIZE && hits < 64; off += 4) {
			uint32_t word = rd(gmu.base + GMU_DTCM + off);

			if ((word & 1) && word >= 8 && word < 0x4000u) {
				if (hits % 8 == 0) {
					if (hits != 0) {
						printf("adreno: %u:   tcm+%#x: %x %x %x %x %x %x %x %x\n", step,
						    off - 4 * 7, line[0], line[1], line[2], line[3],
						    line[4], line[5], line[6], line[7]);
					}
					for (unsigned i = 0; i < 8; i++) {
						line[i] = 0;
					}
				}
				line[hits % 8] = word;
				hits++;
			}
		}
		if (hits == 0) {
			printf("adreno: %u:   none there either\n", step);
		}
		printf("adreno: %u: its data TCM's first words and its top 0x600 bytes:\n", step);
		for (uint32_t off = 0; off < 0x40; off += 8) {
			printf("adreno: %u:   tcm+%#x: %x %x %x %x %x %x %x %x\n", step, off,
			    rd(gmu.base + GMU_DTCM + off), rd(gmu.base + GMU_DTCM + off + 4),
			    rd(gmu.base + GMU_DTCM + off + 8), rd(gmu.base + GMU_DTCM + off + 12),
			    rd(gmu.base + GMU_DTCM + off + 16), rd(gmu.base + GMU_DTCM + off + 20),
			    rd(gmu.base + GMU_DTCM + off + 24), rd(gmu.base + GMU_DTCM + off + 28));
		}
		for (uint32_t off = GMU_TCM_SIZE - 0x600; off < GMU_TCM_SIZE; off += 32) {
			printf("adreno: %u:   tcm+%#x: %x %x %x %x %x %x %x %x\n", step, off,
			    rd(gmu.base + GMU_DTCM + off), rd(gmu.base + GMU_DTCM + off + 4),
			    rd(gmu.base + GMU_DTCM + off + 8), rd(gmu.base + GMU_DTCM + off + 12),
			    rd(gmu.base + GMU_DTCM + off + 16), rd(gmu.base + GMU_DTCM + off + 20),
			    rd(gmu.base + GMU_DTCM + off + 24), rd(gmu.base + GMU_DTCM + off + 28));
		}
	}
}

/*
 * What can be seen of a GMU that has stopped answering: the tail of its log (16 bytes an event:
 * which, the crystal's counter, two values; the firmware's function at 0x1a98; where it writes
 * next is in pwr_col_cp_resp), its record of what it was asked for and what it has (firmware
 * 1.8's data at 0x108454: +0x28 is what is in flight, +0x40.. what is in force), its RPMh
 * channel, and the 3D core's switch and PLLs, which only the GMU's firmware writes.
 */
#define GMU_STATE_ADDRESS       0x108454

static void
gmu_post_mortem(void)
{
	uint32_t next = rd(gmu.base + GMU_PWR_COL_CP_RESP) & 0xffff, entries = GMU_LOG_SIZE / 16;
	const volatile uint32_t *log = (const volatile uint32_t *)gmu_log.memory;

	to_memory(gmu_log.memory, GMU_LOG_SIZE);
	printf("adreno: %u: the GMU's log (event, time, two values), %u events in, the last 40:\n", step, next);
	for (uint32_t i = next > 40 ? next - 40 : 0; i < next && i + 4 <= entries; i += 4) {
		printf("adreno: %u:   %x %x %x %x | %x %x %x %x | %x %x %x %x | %x %x %x %x\n", step,
		    log[4 * i], log[4 * i + 1], log[4 * i + 2], log[4 * i + 3],
		    log[4 * i + 4], log[4 * i + 5], log[4 * i + 6], log[4 * i + 7],
		    log[4 * i + 8], log[4 * i + 9], log[4 * i + 10], log[4 * i + 11],
		    log[4 * i + 12], log[4 * i + 13], log[4 * i + 14], log[4 * i + 15]);
	}
	/* The firmware's own state record, where firmware 1.8 keeps it (0x108454). This machine's
	 * firmware is a640's and does not put its data there -- gmu_data answers 0xdeadbeef for
	 * an address that is not in memory of ours -- so the dump is only printed if the address
	 * lies in a block we gave the firmware. */
	if (gmu_data(GMU_STATE_ADDRESS) != 0xdeadbeef) {
		for (uint32_t i = 0; i < 24; i += 8) {
			printf("adreno: %u: the GMU's state +%#x: %x %x %x %x %x %x %x %x\n", step, 4 * i,
			    gmu_data(GMU_STATE_ADDRESS + 4 * i), gmu_data(GMU_STATE_ADDRESS + 4 * i + 4),
			    gmu_data(GMU_STATE_ADDRESS + 4 * i + 8), gmu_data(GMU_STATE_ADDRESS + 4 * i + 12),
			    gmu_data(GMU_STATE_ADDRESS + 4 * i + 16), gmu_data(GMU_STATE_ADDRESS + 4 * i + 20),
			    gmu_data(GMU_STATE_ADDRESS + 4 * i + 24), gmu_data(GMU_STATE_ADDRESS + 4 * i + 28));
		}
	}
	rscc_show();
	get(&gpucc, "gx_gdscr", GPUCC_GX_GDSCR);
	get(&gpucc, "gx_cfg_gdscr", GPUCC_GX_CFG_GDSCR);
	get(&gpucc, "gx_sw_reset", GPUCC_GX_SW_RESET);
	get(&gpucc, "gx_domain_misc", GPUCC_GX_DOMAIN_MISC);
	get(&gpucc, "pll0 mode", GPUCC_PLL0_MODE);
	get(&gpucc, "pll0 multiplier", GPUCC_PLL0_MODE + 4);
	get(&gpucc, "pll1 mode", GPUCC_PLL1_MODE);
	get(&gpucc, "pll1 multiplier", GPUCC_PLL1_MODE + 4);
	get(&gmu, "rpmh_power_state", GMU_RPMH_POWER_STATE);
	get(&gmu, "sptprac_pwr_clk_status", GMU_SPTPRAC_PWR_CLK_STATUS);
	gmu_ao_state("as it stopped answering");
	/* Where its processor is standing: the chain of calls underneath the task that stopped. */
	gmu_stack_probe();
}

/* Put a message whose first word is yet to be made into the GMU's command queue, and say so. */
static uint32_t
hfi_post(const char *what, uint32_t id, uint32_t *message, uint32_t words)
{
	volatile struct hfi_queue *queue = &hfi->queue[0];
	volatile uint32_t *data = (volatile uint32_t *)((vm_offset_t)hfi + HFI_QUEUE_AT(0));
	uint32_t at = queue->write % HFI_QUEUE_WORDS, number = ++hfi_number & 0xfff;

	message[0] = id | words << 8 | number << 20;
	printf("adreno: %u: about to send the GMU %s: message %u, %u words, number %u\n", step, what, id, words, number);
	snprintf(last_write, sizeof(last_write), "step %u sent the GMU message %u (%s)", step, id, what);
	hold(true);
	for (uint32_t i = 0; i < (words + 3) / 4 * 4; i++) {
		data[at] = i < words ? message[i] : 0xfafafafa;
		at = (at + 1) % HFI_QUEUE_WORDS;
	}
	queue->write = at;
	settle();
	wr(gmu.base + GMU_HOST2GMU_INTR_SET, 1);
	return number;
}

/* What the two queues and the two mailboxes say when nothing comes back: whether the GMU ever
 * read the message, whether it raised an interrupt we failed to see, and whether its side of the
 * mailbox is even unmasked (Linux a6xx_hfi.c: a6xx_hfi_send_msg, and the mask it programs). */
static void
hfi_stalled(uint32_t number)
{
	volatile uint32_t *answers = (volatile uint32_t *)((vm_offset_t)hfi + HFI_QUEUE_AT(1));
	uint32_t at = hfi->queue[1].read % HFI_QUEUE_WORDS;

	printf("adreno: %u: queue 0 (ours): read %u write %u dropped %u; queue 1 (the GMU's answers): read %u write %u "
	    "dropped %u rx_request %u status %#x, waiting for number %u\n", step,
	    hfi->queue[0].read, hfi->queue[0].write, hfi->queue[0].dropped, hfi->queue[1].read, hfi->queue[1].write,
	    hfi->queue[1].dropped, hfi->queue[1].rx_request, hfi->queue[1].status, number);
	printf("adreno: %u:   what queue 1 holds where our reading stopped: %#x %#x %#x %#x\n", step,
	    answers[at], answers[(at + 1) % HFI_QUEUE_WORDS], answers[(at + 2) % HFI_QUEUE_WORDS],
	    answers[(at + 3) % HFI_QUEUE_WORDS]);
	printf("adreno: %u: gmu2host_intr_info %#x, its mask %#x; host2gmu_intr_raw %#x, our enables %#x %#x %#x %#x\n",
	    step, rd(gmu.base + GMU_GMU2HOST_INTR_INFO), rd(gmu.base + GMU_GMU2HOST_INTR_MASK),
	    rd(gmu.base + GMU_HOST2GMU_INTR_RAW), rd(gmu.base + GMU_HOST2GMU_INTR_EN_0),
	    rd(gmu.base + GMU_HOST2GMU_INTR_EN_0 + 4), rd(gmu.base + GMU_HOST2GMU_INTR_EN_0 + 8),
	    rd(gmu.base + GMU_HOST2GMU_INTR_EN_0 + 12));
}

/* Wait for the GMU's verdict on the message numbered `number`. */
static bool
hfi_verdict(const char *what, uint32_t number)
{
	uint32_t verdict = 0, waiting = 0;
	bool answered = false;

	for (int i = 0; i < 10000 && !answered; i++) {
		sleep_us(100);
		waiting = rd(gmu.base + GMU_GMU2HOST_INTR_INFO);
		if (waiting & 1) {
			wr(gmu.base + GMU_GMU2HOST_INTR_CLR, 1);
			answered = hfi_receive(1, number, &verdict);
		}
		if (rd(gmu.base + GMU_CM3_FW_INIT_RESULT) & GMU_FAILED) {
			break;
		}
	}
	hfi_receive(2, ~0u, &waiting);
	if (!answered || verdict != 0) {
		printf("adreno: %u: the GMU %s (%#x) to %s; fw_init_result %#x, gmu2host_intr_info %#x\n", step,
		    answered ? "says no" : "does not answer", verdict, what, rd(gmu.base + GMU_CM3_FW_INIT_RESULT),
		    rd(gmu.base + GMU_GMU2HOST_INTR_INFO));
		if (!answered) {
			hfi_stalled(number);
		}
		space_faulted(&gmu_space);
		if (!post_mortem_done) {
			post_mortem_done = true;
			gmu_post_mortem();
		}
		return false;
	}
	printf("adreno: %u: the GMU accepts %s (%#x %#x)\n", step, what, hfi_last_answer[2], hfi_last_answer[3]);
	return true;
}

/* Send a message whose first word is yet to be made and wait for the GMU's verdict on it. */
static bool
hfi_send(const char *what, uint32_t id, uint32_t *message, uint32_t words)
{
	return hfi_verdict(what, hfi_post(what, id, message, words));
}

/*
 * Send a message and do not wait. The vendor's driver does the same for "prepare to slumber"
 * (a6xx_hfi.c:1015-1023, which passes no acknowledgement), and Linux's a6xx_hfi_send_msg is
 * content with an empty one; waiting for an answer that is never meant to come only costs a
 * second of boot and a line that looks like a failure.
 */
static bool
hfi_tell(const char *what, uint32_t id, uint32_t *message, uint32_t words)
{
	uint32_t nothing = 0;

	(void)hfi_post(what, id, message, words);
	(void)hfi_receive(1, ~0u, &nothing);
	return true;
}

/*
 * What the GMU's firmware keeps about the 3D core's supply, and what its own vote has reached:
 * the DCVS registers it fills when its power management is up (a6xx_reg.h:961-967), the switch's
 * real state (cfg_gdscr, which Linux's gpucc-sm8150.c polls where gdscr's request bit has never
 * been seen to change), and the GMU's RPMh channel and sequencer.
 */
static void
gmu_vote_state(const char *when)
{
	printf("adreno: %u: %s: boot_slumber %#x, gx_vote_idx %#x, mx_vote_idx %#x, dcvs_ack_option %#x, "
	    "perf_setting %#x, bw_setting %#x, dcvs_return %#x\n", step, when,
	    rd(gmu.base + GMU_BOOT_SLUMBER_OPTION), rd(gmu.base + GMU_GX_VOTE_IDX),
	    rd(gmu.base + GMU_MX_VOTE_IDX), rd(gmu.base + GMU_DCVS_ACK_OPTION),
	    rd(gmu.base + GMU_DCVS_PERF_SETTING), rd(gmu.base + GMU_DCVS_BW_SETTING),
	    rd(gmu.base + GMU_DCVS_RETURN));
	get(&gpucc, "gx_gdscr", GPUCC_GX_GDSCR);
	get(&gpucc, "gx_cfg_gdscr", GPUCC_GX_CFG_GDSCR);
	get(&gpucc, "gx_hw_ctrl", GPUCC_GX_HW_CTRL);
	get(&gpucc, "gx_domain_misc", GPUCC_GX_DOMAIN_MISC);
	get(&gmu, "sptprac_pwr_clk_status", GMU_SPTPRAC_PWR_CLK_STATUS);
	get(&gmu, "rpmh_power_state", GMU_RPMH_POWER_STATE);
	get(&gmu, "rpmh_ctrl", GMU_RPMH_CTRL);
	get(&gmu, "rscc_control_req", GMU_RSCC_CONTROL_REQ);
	get(&gmu, "rscc_control_ack", GMU_RSCC_CONTROL_ACK);
	get(&pdc, "gpu_enable", PDC_GPU_ENABLE);
	rscc_show();
	gmu_ao_state(when);
}

/*
 * Ask the GMU's firmware for something out of band: one bit of the request register, one bit of
 * the answer register (adreno_a6xx_gmu.c: a6xx_gmu_oob_set). The vendor waits 20 ms for the
 * answer; this waits two seconds and prints along the way what the request register holds,
 * because on this machine no request has ever been answered and whether the request even gets
 * through is the open question (053156: bit 30 was asked, and the request register afterwards
 * read as if it had never been written -- either the firmware swallowed it without answering or
 * the write itself vanished, and the two are only told apart by watching it pending).
 */
static bool
oob_ask(const char *what, uint32_t request, uint32_t answer, uint32_t *info_out)
{
	uint32_t info = 0;

	put(&gmu, "host2gmu_intr_set", GMU_HOST2GMU_INTR_SET, request);
	for (int i = 0; i < 20000 && !(info & answer); i++) {
		sleep_us(100);
		info = rd(gmu.base + GMU_GMU2HOST_INTR_INFO);
		if (i % 5000 == 4999) {
			printf("adreno: %u: %u ms into asking for %s: the request register still holds %#x\n",
			    step, (i + 1) / 10, what, rd(gmu.base + GMU_HOST2GMU_INTR_RAW));
		}
	}
	printf("adreno: %u: the GMU %s %s (answer register %#x, request register %#x)\n", step,
	    info & answer ? "grants" : "does not answer", what, info,
	    rd(gmu.base + GMU_HOST2GMU_INTR_RAW));
	if (info & answer) {
		put(&gmu, "gmu2host_intr_clr", GMU_GMU2HOST_INTR_CLR, answer);
	}
	*info_out = info;
	return info & answer;
}

/*
 * A vote for a level of a supply: the level, its place in RPMh's list for the rail, and the place
 * of the level the memories' rail must have with it (vendor's setup_volt_dependency_tbl).
 */
static bool
rail_vote(uint32_t level, const uint16_t *rail, unsigned rail_levels, const uint16_t *mx, unsigned mx_levels, uint32_t *vote)
{
	unsigned at, with = 0;

	for (at = 0; at < rail_levels && rail[at] < level; at++) {
	}
	if (at == rail_levels || mx_levels == 0) {
		return false;
	}
	while (with + 1 < mx_levels && mx[with] < rail[at]) {
		with++;
	}
	*vote = (uint32_t)rail[at] << 16 | with << 8 | at;
	return true;
}

/* dt: kgsl_3d0_reg_memory: the 3D core's registers; a6xx.xml counts in words */
static struct block gpu = { "gpu", 0x02c00000, 0x40000 };
#define GPU_WORD(n)             (4u * (n))
#define RBBM_INT_0_STATUS       GPU_WORD(0x0201)
#define RBBM_STATUS             GPU_WORD(0x0210)
#define RBBM_CLOCK_CNTL         GPU_WORD(0x00ae)
#define CP_RB_BASE              GPU_WORD(0x0800)
#define GMU_CORE_ACTIVE         (1u << 30)      /* oob_gpu of host2gmu_intr_set (req 0): the 3D core is active */
#define GMU_CORE_ACTIVE_ACK     (1u << 31)      /* ... and its answer in gmu2host_intr_info */
#define GMU_OOB_GPU_LEGACY      (1u << 16)      /* oob_gpu in the encoding for firmware whose HFI version reads as 0:
                                                 * the same bit asks and answers (adreno_a6xx_gmu.c:589-593) */
#define GMU_OOB_DCVS            (1u << 24)      /* oob_dcvs (req 3): carry out the speed left in the DCVS registers */
#define GMU_OOB_DCVS_ACK        (1u << 28)      /* ... and its answer (BIT(31 - 3)) */
#define GMU_OOB_DCVS_LEGACY     (1u << 19)      /* ... the same request and answer in the old encoding (BIT(3 + 16)) */
/* What SPTPRAC_PWR_CLK_STATUS says about the 3D core (a6xx_gmu.xml.h). a6xx_gmu_core_is_on()
 * calls the core up when neither of these two is set; bit 3, which the vendor's sptprac_enable
 * waits for, says the RAM's power-up completed. The vendor never writes the power switch on
 * this machine (its idle level is SPTP_PC, where the firmware owns the switch; its host-side
 * a6xx_gmu_sptprac_enable is guarded off by adreno_has_sptprac_gdsc, which a640 lacks) --
 * step 17 asks for the power-up by hand once, to see whether that is what the firmware's
 * start task waits for. */
#define SPTPRAC_GX_GDSC_OFF     (1u << 6)
#define SPTPRAC_GX_CLK_OFF      (1u << 7)

static uint32_t core_khz;               /* the 3D core's speed, as asked of the GMU */

/* Read-only GPUCC snapshot. Comparing one taken just before the GMU's OOB
 * bit30 with one taken after the firmware consumes it shows which registers
 * the firmware itself writes when it tries to start GX. Only differences are
 * printed; the block is 0x9000 long as declared by the device tree. */
/* Only the clock-controller window that answered in the read-only survey.
 * Reading the whole 0x9000 block hangs the GPUCC bus on this phone. */
#define GPUCC_SNAP_BASE 0x1000u
#define GPUCC_WORDS     (0x400u / 4u)
#define RSCC_WORDS      (0x2000u / 4u)
static uint32_t gpucc_snap[GPUCC_WORDS];
static uint32_t rscc_snap[RSCC_WORDS];

static void
rscc_snapshot_save(void)
{
	for (uint32_t i = 0; i < RSCC_WORDS; i++) {
		rscc_snap[i] = rd(rscc.base + 4 * i);
	}
}

static void
rscc_snapshot_diff(const char *when)
{
	unsigned changed = 0;

	for (uint32_t i = 0; i < RSCC_WORDS; i++) {
		uint32_t now = rd(rscc.base + 4 * i);

		if (now != rscc_snap[i]) {
			printf("adreno: %u: rscc %s: +%#x %#x -> %#x\n", step, when, 4 * i, rscc_snap[i], now);
			changed++;
		}
	}
	printf("adreno: %u: rscc %s: %u registers changed\n", step, when, changed);
}

static void
gpucc_snapshot_save(void)
{
	for (uint32_t i = 0; i < GPUCC_WORDS; i++) {
		gpucc_snap[i] = rd(gpucc.base + GPUCC_SNAP_BASE + 4 * i);
	}
}

static void
gpucc_snapshot_diff(const char *when)
{
	unsigned changed = 0;

	for (uint32_t i = 0; i < GPUCC_WORDS; i++) {
		uint32_t now = rd(gpucc.base + GPUCC_SNAP_BASE + 4 * i);

		if (now != gpucc_snap[i]) {
			printf("adreno: %u: gpucc %s: +%#x %#x -> %#x\n", step, when,
			    GPUCC_SNAP_BASE + 4 * i, gpucc_snap[i], now);
			changed++;
		}
	}
	printf("adreno: %u: gpucc %s: %u registers changed\n", step, when, changed);
}

/*
 * Close the GX domain's power switch from the host, the way the vendor's regulator core drives
 * this headswitch (dt: qcom,gdsc@0x2c9100c, whose parent-supply is the RPMh arc regulator
 * "gfx.lvl"): release the AON reset and the clamp first, as gdsc_enable() and Linux's
 * gpucc-sm8150.c order them (CLAMP_IO, AON_RESET), then assert the switch's own power request
 * (gdscr bit 31) and release the software collapse; gdsc.c's gdsc_toggle_logic() polls bit 31 as
 * the answer. The request is what was missing: clearing the collapse alone only started the
 * sequencer (050003: cfg_gdscr moved 0x88400 -> 0x1080400 and stopped there). The domain's real
 * state is what cfg_gdscr bit 16 says (POLL_CFG_GDSCR). 070208 showed why this must happen
 * before "start the core firmware": the firmware answered that message, its start task then sat
 * waiting for a rail that was still down, and its watchdog bit fired within seconds -- the
 * vendor's GX rail is always powered before its firmware boots (its gdsc is host-owned), and
 * this firmware has no way to raise it itself.
 */
static bool
gx_switch_close(const char *why)
{
	char line[160];
	uint32_t gdscr;

	snprintf(line, sizeof(line), "%s; ask for power and close the GX domain's switch", why);
	announce(line);
	gdscr = rd(gpucc.base + GPUCC_GX_HW_CTRL);
	wr(gpucc.base + GPUCC_GX_HW_CTRL, gdscr | GDSC_AON_RESET);
	sleep_us(10);
	wr(gpucc.base + GPUCC_GX_HW_CTRL, rd(gpucc.base + GPUCC_GX_HW_CTRL) & ~GDSC_AON_RESET);
	wr(gpucc.base + GPUCC_GX_HW_CTRL, rd(gpucc.base + GPUCC_GX_HW_CTRL) & ~GDSC_CLAMP_IO);
	gdscr = get(&gpucc, "gx_gdscr", GPUCC_GX_GDSCR);
	put(&gpucc, "gx_gdscr", GPUCC_GX_GDSCR, (gdscr | GDSC_POWER_ON) & ~GDSC_SW_COLLAPSE);
	for (int i = 0; i < 5000 && !((gdscr = rd(gpucc.base + GPUCC_GX_GDSCR)) & GDSC_POWER_ON) &&
	    !(rd(gpucc.base + GPUCC_GX_CFG_GDSCR) & GDSC_POWER_UP_COMPLETE); i++) {
		sleep_us(100);
	}
	printf("adreno: %u: the switch %s: gdscr %#x, cfg_gdscr %#x\n", step,
	    (gdscr & GDSC_POWER_ON) || (rd(gpucc.base + GPUCC_GX_CFG_GDSCR) & GDSC_POWER_UP_COMPLETE) ?
	    "is closed" : "will not close",
	    gdscr, rd(gpucc.base + GPUCC_GX_CFG_GDSCR));
	return (gdscr & GDSC_POWER_ON) || (rd(gpucc.base + GPUCC_GX_CFG_GDSCR) & GDSC_POWER_UP_COMPLETE);
}

/*
 * What the vendor's driver sends here for this GPU (kgsl_hfi.c: hfi_start, which is what its
 * firmware was written against): the table of speeds of the 3D core with the supply level each
 * needs, the table of bus votes, the one feature it always asks for (early clock powering off,
 * kgsl_hfi.c:739-742; the ACD table and its switch come only when the device tree asks for
 * adaptive clocking, and this machine's does not -- the only quirks it names are secvid-set-once
 * and cx-gdsc), then -- with the GX domain powered, as the vendor's a6xx_gmu_power_on has it
 * before the firmware boots -- "start the core firmware", and within milliseconds of its answer
 * (adreno.c:1995-2006) the request that the 3D core is ours (oob_gpu) and only then "start".
 * The speeds are the phone's own (dt: qcom,gpu-pwrlevels-1:
 * 675 ... 257 MHz, each with the RPMh level Linux's sm8150.dtsi pairs with it); the bus table
 * is the fixed one the vendor hard-codes for a640, not looked up in the command database, whose
 * resources here are different ones. The INIT and FW_VERSION of the old v1 protocol are skipped,
 * as the vendor's driver does for a640 and a680 (kgsl_hfi.c:713): this GMU's HFI is v2.
 */
static bool
step_core_power(void)
{
	static const struct {
		uint32_t khz, level;
	} speeds[] = { { 0, 0 }, { 257000, 64 }, { 345000, 128 }, { 427000, 192 },
	    { 499200, 224 }, { 585000, 256 }, { 675000, 320 } };
	static struct hfi_perf_table perf;
	static struct hfi_bw_table bw;
	uint32_t feature[4] = { 0, HFI_FEATURE_ECP, 0, 0 };  /* kgsl_hfi.c:739-742: hfi_send_feature_ctrl(ECP, 0, 0) */
	uint32_t core_start[2] = { 0, 0 }, start[1] = { 0 }, vote[4] = { 0, 1, 1, 0 };
	uint32_t version[2] = { 0, GMU_VERSION };
	uint32_t active = 0, speed = 0, gfx_cell = 0, status, start_first = 0, delay_first = 0, new_oob_only = 0;
	uint32_t clear_oob_probe = 0, gx_gmu_clock_probe = 0;
	uint32_t fast_oob_probe = 0;
	uint16_t gfx[16], cx[16], mx[16];
	unsigned gfx_levels, cx_levels, mx_levels;
	bool core_granted = false, start_answered = false;

	if (!cx_ready() || !gmu_talks) {
		return false;
	}
	gfx_cell = rpmh_resource("gfx.lvl", gfx, &gfx_levels);
	if (gfx_cell == 0 || !rpmh_resource("cx.lvl", cx, &cx_levels) ||
	    !rpmh_resource("mx.lvl", mx, &mx_levels)) {
		printf("adreno: %u: RPMh's list lacks one of the rails\n", step);
		return false;
	}
	perf.gpu_levels = sizeof(speeds) / sizeof(speeds[0]);
	for (unsigned i = 0; i < perf.gpu_levels; i++) {
		perf.gpu[i].khz = speeds[i].khz;
		perf.gpu[i].acd = 0xffffffff;           /* no adaptive clocking on any level */
		if (!rail_vote(speeds[i].level, gfx, gfx_levels, mx, mx_levels, &perf.gpu[i].vote)) {
			printf("adreno: %u: RPMh has no level %u for the 3D core's rail\n", step, speeds[i].level);
			return false;
		}
		printf("adreno: %u: level %u: %u MHz at supply level %u -> the GMU's vote %#x\n", step, i,
		    speeds[i].khz / 1000, speeds[i].level, perf.gpu[i].vote);
	}
	perf.gmu_levels = 2;
	perf.gmu[1].khz = 200000;
	if (!rail_vote(0, cx, cx_levels, mx, mx_levels, &perf.gmu[0].vote) ||
	    !rail_vote(48, cx, cx_levels, mx, mx_levels, &perf.gmu[1].vote)) {  /* "minimum SVS", the vendor's choice */
		return false;
	}
	printf("adreno: %u: the GMU's own two levels: %#x and %#x at 200 MHz\n", step, perf.gmu[0].vote, perf.gmu[1].vote);

	/*
	 * The bus votes the firmware will make of its own accord. These are the values the
	 * vendor's driver hard-codes for a640 (a6xx_hfi.c: a640_build_bw_table) -- one level,
	 * "off": the addresses are the RPMh resource slots of that driver's target, which are
	 * not this machine's (ours are SH0 0x5003c, MC0 0x50000, ACV 0x5000c, CN0 0x500a8), so
	 * looking them up here would send a table that matches no level the firmware knows.
	 */
	bw.levels = 1;
	bw.ddr_commands = 3;
	bw.ddr_wait = 0x01;
	bw.ddr_address[0] = 0x50000;
	bw.ddr_address[1] = 0x5003c;
	bw.ddr_address[2] = 0x5000c;
	bw.ddr_data[0][0] = bw.ddr_data[0][1] = bw.ddr_data[0][2] = 0x40000000;
	bw.cnoc_commands = 3;
	bw.cnoc_wait = 0x01;
	bw.cnoc_address[0] = 0x50034;
	bw.cnoc_address[1] = 0x5007c;
	bw.cnoc_address[2] = 0x5004c;
	bw.cnoc_data[0][0] = bw.cnoc_data[0][2] = 0x40000000;
	bw.cnoc_data[1][0] = 0x60000001;
	bw.cnoc_data[1][1] = 0x20000001;
	bw.cnoc_data[1][2] = 0x60000001;

	if (PE_parse_boot_argn("adreno_speed", &speed, sizeof(speed)) && speed >= 1 && speed < perf.gpu_levels) {
		vote[2] = speed;
	}
	core_khz = speeds[vote[2]].khz;
	printf("adreno: %u: the speed wanted: %u MHz, level %u, vote %#x\n", step, core_khz / 1000,
	    speeds[vote[2]].level, perf.gpu[vote[2]].vote);
	announce("ask the GMU's firmware which HFI it speaks, send it its tables (speeds and supply "
	    "levels, bus votes), turn its early clock powering off, vote its supply, then \"start the "
	    "core firmware\"; the firmware is asked to power the 3D core itself, and only if its own "
	    "vote fails to lift the rail does the host power the domain by hand");
	/*
	 * The version handshake the vendor's hfi_start() opens with (kgsl_hfi.c:687-767). The
	 * firmware's answer's fourth word is its version (hfi_get_fw_version reads results[3]); a
	 * mismatch is only a warning there (hfi_verify_fw_version, 631-670), so a refusal here is
	 * printed and walked past.
	 */
	if (!hfi_send("the version of HFI we speak", HFI_FW_VERSION, version, 2)) {
		printf("adreno: %u: the firmware will not discuss the version; going on with ours\n", step);
	} else {
		printf("adreno: %u: the firmware's HFI and version: verdict %#x, version %#x\n", step,
		    hfi_last_answer[2], hfi_last_answer[3]);
	}
	if (!hfi_send("the table of speeds", HFI_PERF_TABLE, &perf.header, sizeof(perf) / 4) ||
	    !hfi_send("the table of bus votes", HFI_BW_TABLE, &bw.header, sizeof(bw) / 4) ||
	    !hfi_send("early clock powering off", HFI_FEATURE_CTRL, feature, 4)) {
		return false;
	}
	/*
	 * The host's own vote for the 3D core's rail, before the core firmware is asked to start.
	 * The vendor's kgsl_pwrctrl has voted every rail long before the GMU's firmware boots, and
	 * last round the firmware's own vote left the rail down (logs/040314-run.log: gx_gdscr
	 * 0x8222000, the firmware's read pointer stopped just past its message). A vote through
	 * RPMh is what kgsl_pwrctrl does, and cannot fault: it touches no GPU register.
	 *
	 * What to put in the TCS, though, is not the level number: gfx.lvl is an ARC resource
	 * (dt: rpmh-regulator-gfxlvl, qcom,rpmh-arc-regulator), and an ARC command carries the
	 * vote packed -- voltage level in 31:16, the two priorities in 15:8 and 7:0 (the vendor's
	 * ARC_VOTE_SET, kgsl_hfi.h:230-233). That is exactly what the firmware itself would
	 * write: its RSCC wake slots carry perf-table votes verbatim (a6xx_hfi.c:304), and the
	 * perf table we sent it holds 0x400203 for this level. Two rounds sent the bare 0x40 and
	 * the RSC completed the transaction (status 0x10108) yet the rail never lifted -- an ARC
	 * reading 0x40 sees voltage level 0 at priority 0x40, which asks for nothing.
	 */
	if (!tcs_send(gfx_cell, perf.gpu[vote[2]].vote)) {
		printf("adreno: %u: the host could not vote gfx.lvl at %#x\n", step, perf.gpu[vote[2]].vote);
	}
	/*
	 * The rail must be up before the firmware's start task runs, not after. 070208: the
	 * firmware answered "start the core firmware", its task then waited for a rail that was
	 * still down (gdscr 0x8222000, cfg_gdscr 0x1080400), and the always-on watchdog fired
	 * within seconds of the answer -- a processor parked in that wait cannot feed it. The
	 * vendor never faces this because its GX gdsc is a host regulator closed long before
	 * the GMU boots. Close it here, right after the host's own gfx.lvl vote, the same
	 * writes the fallback below has always made; the poll after the message stays to show
	 * whether the firmware's own vote holds the rail up on its own afterwards.
	 */
	gx_switch_close("the rail is voted; the firmware's start task cannot wait for it");
	/* Vendor _adreno_start() calls adreno_deassert_gbif_halt() before its
	 * first oob_gpu. Its comment says a stale GX halt makes GPU wake-up
	 * fail on halted transactions. This driver only cleared the GX-side
	 * RBBM halt in step 20, after OOB. Read both and deassert before OOB. */
	{
		uint32_t gx_halt, gx_ack, cx_halt, cx_ack;

		need(&gpu);                     /* 0x2c00000 was not mapped until step 20 */
		need(&gbif);
		gx_halt = rd(gpu.base + RBBM_GBIF_HALT);
		gx_ack = rd(gpu.base + RBBM_GBIF_HALT_ACK);
		cx_halt = rd(gbif.base + GBIF_HALT);
		cx_ack = rd(gbif.base + GBIF_HALT_ACK);

		put(&gpu, "rbbm_gbif_halt (pre-OOB)", RBBM_GBIF_HALT, 0);
		put(&gbif, "gbif halt (pre-OOB)", GBIF_HALT, 0);
		printf("adreno: %u: GBIF before OOB: gx halt %#x ack %#x, cx halt %#x ack %#x -> gx %#x, cx %#x\n",
		    step, gx_halt, gx_ack, cx_halt, cx_ack,
		    rd(gpu.base + RBBM_GBIF_HALT), rd(gbif.base + GBIF_HALT));
	}
	/* The CX_GMU clock was enabled in step 5, but GX_GMU (+0x1064)
	 * was observed off at step 3. The latter is a separate branch of
	 * the same GMU clock source in gpucc-sm8150.c. Probe this one
	 * branch only, after GX power-up and before OOB_GPU. */
	if (PE_parse_boot_argn("adreno_gx_gmu_clock_probe", &gx_gmu_clock_probe,
	    sizeof(gx_gmu_clock_probe)) && gx_gmu_clock_probe) {
		uint32_t before = rd(gpucc.base + GPUCC_GX_GMU_CBCR);
		uint32_t power = rd(gpucc.base + GPUCC_GX_CFG_GDSCR);

		if (power & GDSC_POWER_UP_COMPLETE) {
			clock_on(&gpucc, "gx_gmu (GX-on probe)", GPUCC_GX_GMU_CBCR, false);
		}
		printf("adreno: %u: GX_GMU clock probe: cfg_gdscr %#x, cbcr %#x -> %#x, GX status %#x\n",
		    step, power, before, rd(gpucc.base + GPUCC_GX_GMU_CBCR),
		    rd(gmu.base + GMU_SPTPRAC_PWR_CLK_STATUS));
	}
	/* The sm8150 mainline clocks omit gfx3d entirely: it is firmware-owned.
	 * The previous 0x1030/0x1060 probe configured the wrong RCG/XO gate.
	 * 111213 and 112853 confirmed even a direct enable of 0x101c/0x1054
	 * leaves the RCG root-off and branch halted. Keep its read-only survey
	 * but no longer override the firmware's clock management. */
	if (rd(gpucc.base + GPUCC_GX_CFG_GDSCR) & GDSC_POWER_UP_COMPLETE) {
		printf("adreno: %u: gfx3d firmware-owned clocks: gx1054 %#x cx10a4 %#x cx10a8 %#x rcg101c %#x cfg1020 %#x\n",
		    step, rd(gpucc.base + 0x1054), rd(gpucc.base + 0x10a4), rd(gpucc.base + 0x10a8),
		    rd(gpucc.base + 0x101c), rd(gpucc.base + 0x1020));
		/* Read-only: are GPUCC PLL0/PLL1 configured and locked (bit31)?
		 * mainline/vendor gpucc-sm8150 probe configures PLL1; this driver
		 * never has. The 3D clock may have no running parent. */
		for (uint32_t base = 0; base <= 0x100; base += 0x100) {
			printf("adreno: %u: gpucc pll%u regs:", step, base ? 1u : 0u);
			for (uint32_t off = 0; off <= 0x3c; off += 4) {
				printf(" %02x=%#x", off, rd(gpucc.base + base + off));
			}
			printf("\n");
		}
		/* Read-only survey of the clock-controller window: branches and RCG
		 * commands both show their off/halt state in bit31. Print only the
		 * words in 0x1000..0x11ff to keep the log bounded. */
		for (uint32_t off = 0x1000; off < 0x1200; off += 0x20) {
			printf("adreno: %u: gpucc[%#x]:", step, off);
			for (uint32_t i = 0; i < 8; i++) {
				printf(" %#x", rd(gpucc.base + off + 4 * i));
			}
			printf("\n");
		}
	}
	/*
	 * The last messages in the vendor's order and with nothing slow between them: "start
	 * the core firmware" is the final thing hfi_start() sends before it returns
	 * (kgsl_hfi.c:755), and adreno_start() asks for the 3D core (oob_gpu) and sends
	 * "start" within milliseconds of its acknowledgement (adreno.c:1995-2006). The
	 * firmware's core-start task runs right after that acknowledgement, and every round
	 * that let a second of polling pass before the grant ended with its watchdog biting
	 * (logs/070931, 071637); asking the grant before the messages (071637) did not help
	 * either. The old encoding (bit 16, for firmware whose HFI version reads as 0) is
	 * asked only if the new one (bit 30, answered in bit 31) goes unanswered. The
	 * vendor's rail-on request (a6xx_gmu_gfx_rail_on, which fills the boot-slumber and
	 * vote-index registers) runs only under the HFI_USE_REG quirk, which this machine's
	 * device tree does not name, so those registers are left alone.
	 */
	gmu_core_wait_probe("before msg14");
	if (!hfi_send("\"start the core firmware\"", HFI_CORE_FW_START, core_start, 2)) {
		return false;
	}
	gmu_core_wait_probe("after msg14 ack");
	/* Experimental timing control. Mainline a640 HFI sends START immediately
	 * after CORE_FW_START, whereas the vendor's adreno_start requests OOB GPU
	 * between them. If the firmware still processes HFI when we avoid the
	 * multi-second OOB wait, the difference is measurable without guessing
	 * any power register values. Never take an ACK as proof that CP is on. */
	if (PE_parse_boot_argn("adreno_start_first", &start_first, sizeof(start_first)) && start_first) {
		if (PE_parse_boot_argn("adreno_delay_first", &delay_first, sizeof(delay_first)) && delay_first) {
			printf("adreno: %u: timing probe: wait 4 s after msg14 without OOB, then ask HFI START\n", step);
			sleep_ms(4000);
		} else {
			printf("adreno: %u: timing probe: send HFI START before asking OOB GPU\n", step);
		}
		start_answered = hfi_send("\"start\" (before OOB)", HFI_START, start, 1);
		gmu_core_wait_probe("after early msg10");
	}
	uint32_t no_oob = 0;
	PE_parse_boot_argn("adreno_no_oob", &no_oob, sizeof(no_oob));
	if (no_oob) {
		/* Control: never send oob_gpu. msg14/msg10 have been accepted; see
		 * whether the firmware brings GX up on its own and CP becomes
		 * accessible. The CP_RB_BASE readback in step 20 is the judge. */
		printf("adreno: %u: control: skip oob_gpu entirely\n", step);
		core_granted = true;    /* only means "do not try the legacy path" */
	}
	PE_parse_boot_argn("adreno_fast_oob_probe", &fast_oob_probe, sizeof(fast_oob_probe));
	if (!no_oob && fast_oob_probe) {
		/* One new-style request, no two-second wait: distinguish the request
		 * itself from a delayed firmware transition before the next HFI. */
		uint32_t snapshot = 0;

		uint32_t sptp_clock = 0, pll0_on = 0;

		/* Vendor's a6xx_hwcg_set() enables the GX SPTPRAC clock control
		 * bit (GMU word 0x80) around its HWCG programming. This driver
		 * never has, and the register reads 0. Single-variable probe. */
		PE_parse_boot_argn("adreno_sptp_clock", &sptp_clock, sizeof(sptp_clock));
		if (sptp_clock) {
			put(&gmu, "gx_sptprac_clock_control", GMU_WORD(0x0080), 1);
			printf("adreno: %u: SP clock control now %#x, gx status %#x\n", step,
			    rd(gmu.base + GMU_WORD(0x0080)), rd(gmu.base + GMU_SPTPRAC_PWR_CLK_STATUS));
		}
		/* The gfx3d RCG (0x101c command, 0x1020 source/divider) read
		 * 0x80000000 (root off) with a source of 0 -- an invalid divider.
		 * The bootloader's original value was 0x1 (XO, divider 1); an
		 * earlier experiment overwrote it with 0. Restore it and force
		 * the root on, then watch the GX branch's halt bit. */
		uint32_t gfx3d_xo = 0;

		PE_parse_boot_argn("adreno_gfx3d_xo", &gfx3d_xo, sizeof(gfx3d_xo));
		if (gfx3d_xo) {
			put(&gpucc, "gfx3d rcg source", 0x1020, 0x1);
			put(&gpucc, "gfx3d rcg cmd", 0x101c, rd(gpucc.base + 0x101c) | 1u << 1);
			for (int i = 0; i < 100; i++) {
				if (!(rd(gpucc.base + 0x101c) & (1u << 31))) {
					break;
				}
				sleep_us(10);
			}
			printf("adreno: %u: gfx3d XO: cmd %#x src %#x, gx branch %#x, gx status %#x\n", step,
			    rd(gpucc.base + 0x101c), rd(gpucc.base + 0x1020),
			    rd(gpucc.base + 0x1054), rd(gmu.base + GMU_SPTPRAC_PWR_CLK_STATUS));
		}
		/* Bounded source scan for the gfx3d RCG: with PLL0 locked, try each
		 * source selector 0..7 at divider 1 and read back whether the root
		 * comes out of ROOT_OFF (bit31) and whether the GX branch's halt
		 * bit (0x1054 bit31) clears. Read-only result apart from one RCG's
		 * config; no other clock is touched. */
		uint32_t gfx3d_scan = 0;

		PE_parse_boot_argn("adreno_gfx3d_scan", &gfx3d_scan, sizeof(gfx3d_scan));
		if (gfx3d_scan) {
			put(&gpucc, "pll0 opmode", 0x38, 1);
			put(&gpucc, "pll0 mode", 0x0, rd(gpucc.base + 0x0) | 1u);
			for (int i = 0; i < 1000 && !(rd(gpucc.base + 0x0) & (1u << 31)); i++) {
				sleep_us(10);
			}
			clock_on(&gpucc, "gx_gfx3d", 0x1054, false);
			for (uint32_t src = 0; src < 8; src++) {
				put(&gpucc, "gfx3d rcg source", 0x1020, (src << 8) | 1u);
				put(&gpucc, "gfx3d rcg cmd", 0x101c, rd(gpucc.base + 0x101c) | 1u << 1);
				for (int i = 0; i < 20; i++) {
					if (!(rd(gpucc.base + 0x101c) & (1u << 31))) {
						break;
					}
					sleep_us(10);
				}
				printf("adreno: %u: gfx3d src %u: cmd %#x cfg %#x, gx branch %#x\n", step, src,
				    rd(gpucc.base + 0x101c), rd(gpucc.base + 0x1020), rd(gpucc.base + 0x1054));
			}
			printf("adreno: %u: after gfx3d scan: gx status %#x\n", step,
			    rd(gmu.base + GMU_SPTPRAC_PWR_CLK_STATUS));
		}
		/* Combined 3D-clock probe: run GPUCC PLL0 (TRION, L=17, the
		 * already-programmed 3D PLL), then enable the GX gfx3d branch and
		 * its CX mirrors. Success is the branch's halt bit clearing and
		 * SPTPRAC bit7 going to zero -- not just the registers accepting
		 * a write. */
		uint32_t gfx3d_on = 0;

		PE_parse_boot_argn("adreno_gfx3d_on", &gfx3d_on, sizeof(gfx3d_on));
		if (gfx3d_on) {
			put(&gpucc, "pll0 opmode", 0x38, 1);
			/* PLL_OUT_MASK (bits 0..2 of USER_CTL) must all be set for
			 * every output; the bootloader left 0x3 here. */
			put(&gpucc, "pll0 user_ctl", 0x0c, rd(gpucc.base + 0x0c) | 0x7u);
			put(&gpucc, "pll0 mode", 0x0, rd(gpucc.base + 0x0) | 1u);
			for (int i = 0; i < 1000 && !(rd(gpucc.base + 0x0) & (1u << 31)); i++) {
				sleep_us(10);
			}
			clock_on(&gpucc, "crc_ahb", 0x107c, false);
			clock_on(&gpucc, "cx_apb", 0x1088, false);
			clock_on(&gpucc, "gx_gfx3d", 0x1054, false);
			clock_on(&gpucc, "cx_gfx3d", 0x10a4, false);
			clock_on(&gpucc, "cx_gfx3d_slv", 0x10a8, false);
			printf("adreno: %u: gfx3d on: pll0 %#x opmode %#x, gx branch %#x cx %#x/%#x, "
			    "rcg %#x src %#x, gx status %#x\n", step,
			    rd(gpucc.base + 0x0), rd(gpucc.base + 0x38), rd(gpucc.base + 0x1054),
			    rd(gpucc.base + 0x10a4), rd(gpucc.base + 0x10a8), rd(gpucc.base + 0x101c),
			    rd(gpucc.base + 0x1020), rd(gmu.base + GMU_SPTPRAC_PWR_CLK_STATUS));
		}
		/* GPUCC PLL0 (TRION, offset 0) carries L=17 but OPMODE=0 and
		 * OUTCTRL=0: it is programmed but not running. The gfx3d RCG's
		 * parent comes through the already-programmed 3D CRC, so with no
		 * PLL it can never start. Run it and wait for lock (MODE bit31). */
		PE_parse_boot_argn("adreno_pll0_on", &pll0_on, sizeof(pll0_on));
		if (pll0_on) {
			put(&gpucc, "pll0 opmode", 0x38, 1);
			put(&gpucc, "pll0 mode", 0x0, rd(gpucc.base + 0x0) | 1u);
			int locked = 0;
			for (int i = 0; i < 1000; i++) {
				sleep_us(10);
				if (rd(gpucc.base + 0x0) & (1u << 31)) {
					locked = 1;
					break;
				}
			}
			printf("adreno: %u: pll0 %s: mode %#x opmode %#x, rcg101c %#x, gx1054 %#x, "
			    "gx status %#x\n", step, locked ? "locked" : "did not lock",
			    rd(gpucc.base + 0x0), rd(gpucc.base + 0x38), rd(gpucc.base + 0x101c),
			    rd(gpucc.base + 0x1054), rd(gmu.base + GMU_SPTPRAC_PWR_CLK_STATUS));
		}
		PE_parse_boot_argn("adreno_gfx3d_snapshot", &snapshot, sizeof(snapshot));
		if (snapshot) {
			gpucc_snapshot_save();
			rscc_snapshot_save();
		}
		put(&gmu, "host2gmu_intr_set (short oob_gpu probe)", GMU_HOST2GMU_INTR_SET, GMU_CORE_ACTIVE);
		for (unsigned i = 0; i < 100 && !(active & GMU_CORE_ACTIVE_ACK); i++) {
			sleep_us(100);
			active = rd(gmu.base + GMU_GMU2HOST_INTR_INFO);
		}
		core_granted = (active & GMU_CORE_ACTIVE_ACK) != 0;
		printf("adreno: %u: short oob_gpu: ack %#x, raw %#x, gx status %#x\n", step, active,
		    rd(gmu.base + GMU_HOST2GMU_INTR_RAW), rd(gmu.base + GMU_SPTPRAC_PWR_CLK_STATUS));
		if (snapshot) {
			sleep_ms(1000);
			gpucc_snapshot_diff("after oob + 1s");
			rscc_snapshot_diff("after oob + 1s");
		}
		if (core_granted) {
			put(&gmu, "short oob_gpu ACK clear", GMU_GMU2HOST_INTR_CLR, GMU_CORE_ACTIVE_ACK);
		}
	} else if (!no_oob) {
		core_granted = oob_ask("the 3D core", GMU_CORE_ACTIVE, GMU_CORE_ACTIVE_ACK, &active);
	}
	PE_parse_boot_argn("adreno_new_oob_only", &new_oob_only, sizeof(new_oob_only));
	if (!core_granted && !new_oob_only && !fast_oob_probe) {
		core_granted = oob_ask("the 3D core in the old encoding", GMU_OOB_GPU_LEGACY,
		    GMU_OOB_GPU_LEGACY, &active);
	} else if (!core_granted) {
		printf("adreno: %u: timing probe: skip legacy bit16 OOB; test whether HFI survives bit30 alone\n", step);
	}
	if (PE_parse_boot_argn("adreno_clear_oob_probe", &clear_oob_probe, sizeof(clear_oob_probe)) && clear_oob_probe) {
		/* Vendor a6xx_gmu_oob_clear(req=0), not a guessed MMIO write.
		 * The grant was absent: this is solely an experiment to see whether
		 * the pending OOB request stalls HFI. Do not call it a GPU grant. */
		put(&gmu, "clear failed oob_gpu (vendor bit31)", GMU_HOST2GMU_INTR_SET, 1u << 31);
		get(&gmu, "host2gmu_intr_raw after clear", GMU_HOST2GMU_INTR_RAW);
		gmu_core_wait_probe("after failed OOB clear");
	}
	gmu_core_wait_probe("after OOB attempts");
	core_is_on = true;                      /* asked for, at least: to be given back */
	if (!start_first) {
		start_answered = hfi_send("\"start\"", HFI_START, start, 1);
	}
	gmu_core_wait_probe("after msg10");
	if (!start_answered) {
		printf("adreno: %u: the firmware did not answer \"start\"; reading the 3D core anyway\n", step);
	}
	/*
	 * The firmware's own side of the RPMh channel first of all: whether it has filled and
	 * triggered the command slots it votes through, and whether those completed. In 055623
	 * every message after "start the core firmware" went unanswered, and the slots' control
	 * registers -- which carry the bit that makes a trigger do anything -- read 0; they are
	 * enabled in step 14 now, and this is where to see whether the firmware used them.
	 */
	rscc_show();
	gmu_ao_state("just after \"start the core firmware\"");
	/*
	 * Whether the firmware's own vote holds the rail up. The host closed the switch above,
	 * so this reads the domain now; what matters is that it stays closed and that the
	 * firmware's task -- which 070208 found parked in this wait and bitten by its watchdog --
	 * now moves on and answers what follows. The firmware writes gx_gdscr (070208: 0x8222000,
	 * its software collapse released) but its request bit 31 has never been seen to change,
	 * and the domain's real state is what cfg_gdscr bit 16 says (Linux's gpucc-sm8150.c
	 * polls that register for gpu_gx_gdsc, POLL_CFG_GDSCR).
	 */
	announce("see whether the 3D core's rail stayed up and the firmware's task moved on");
	for (int i = 0; i < 2000 && !(rd(gpucc.base + GPUCC_GX_CFG_GDSCR) & GDSC_POWER_UP_COMPLETE); i++) {
		sleep_us(100);
		if (i % 500 == 499) {
			printf("adreno: %u: after %u ms: gdscr %#x, cfg_gdscr %#x\n", step, i / 10,
			    rd(gpucc.base + GPUCC_GX_GDSCR), rd(gpucc.base + GPUCC_GX_CFG_GDSCR));
		}
	}
	if (rd(gpucc.base + GPUCC_GX_CFG_GDSCR) & GDSC_POWER_UP_COMPLETE) {
		printf("adreno: %u: the 3D core's rail is up\n", step);
	} else {
		/* The host's earlier close did not take (or the domain collapsed): try once more. */
		if (!gx_switch_close("the rail fell after the message")) {
			printf("adreno: %u: the rail is still down; the firmware's start task will wait in it again\n", step);
		}
	}
	/*
	 * The 3D core's retention RAM is the firmware's switch to close, not ours: the
	 * vendor's a6xx_gmu_sptprac_enable() runs only where the host owns the domain
	 * (idle_level < GPU_HW_SPTP_PC, adreno_a6xx_gmu.c:1104), and this machine's device
	 * tree names neither the IFPC nor any quirk that would lower that level. The host's
	 * write to that register was dropped anyway (070931: gx_sptprac_power_control read
	 * back 0 after the write); whether the firmware raised its own switch is what the
	 * status register says, and gmu_vote_state below prints it.
	 */
	gmu_vote_state("after the 3D core's supply was asked for");
	put(&gpucc, "gx_sw_reset", GPUCC_GX_SW_RESET, 0);
	get(&gpucc, "gx_sw_reset", GPUCC_GX_SW_RESET);
	sleep_us(100);
	/*
	 * The 3D core's clock. The core has power now, but none of the host's writes to
	 * its registers stuck (051745: cp_rb_base and cp_apriv_cntl read 0 after their
	 * writes, and the ring was never read): the core's register interface runs on
	 * gfx3d_clk, and that clock is not running -- and writing its RCG by hand does
	 * not work. mainline's gpucc-sm8150.c knows neither gfx3d_clk_src nor
	 * hub_clk_src: the GMU's firmware owns both, through the sideband hardware that
	 * follows its DCVS engine, and while that hardware holds a switch the host's
	 * writes to the command and configuration registers are simply dropped (053156:
	 * even the forced root-enable write to gfx3d_clk_src and every write to
	 * hub_clk_src vanished, while gmu_clk_src, which the host does own, took its
	 * configuration). The vendor's clk-rcg2.c names the hardware's hold: bit 0 of
	 * the register at cmd_rcgr + 0x14. Read both holds, take the 3D core's away if
	 * it is held, and try the software path once more; if it still drops, only the
	 * firmware's own vote can raise this clock -- and that vote travels through the
	 * very hardware that holds it, which is why the DCVS request below is worth
	 * making. Slow or not, source 0 (the 19.2 MHz crystal, the RCG's reset choice
	 * and the first parent of the vendor's clk_gfx3d_src) is enough to ask whether
	 * the core answers its registers and reads its ring.
	 */
	/* 0x1030/0x1060 were inherited from a different GPUCC and are not
	 * the 3D clock here. 110655/111213 identified the true firmware-owned
	 * source/branch at 0x101c/0x1054: host writes left both halted.
	 * Do not turn DFS off or force the wrong source while diagnosing GMU. */
	get(&gpucc, "gfx3d firmware RCG", 0x101c);
	get(&gpucc, "gfx3d firmware branch", 0x1054);
	get(&gpucc, "cx_gdscr_cfg", 0x1070);
	get(&gpucc, "gfx3d firmware RCG source", 0x1020);
	/* The grant and "start" were asked right after "start the core firmware" (above, in
	 * the vendor's order); ask once more only if that first attempt went unanswered --
	 * the task may have come alive in between. */
	if (!core_granted && !new_oob_only && !fast_oob_probe) {
		announce("tell the GMU the 3D core is ours");
		core_granted = oob_ask("the 3D core", GMU_CORE_ACTIVE, GMU_CORE_ACTIVE_ACK, &active);
		if (!core_granted) {
			core_granted = oob_ask("the 3D core in the old encoding", GMU_OOB_GPU_LEGACY,
			    GMU_OOB_GPU_LEGACY, &active);
		}
	}
	if (!start_answered) {
		start_answered = hfi_send("\"start\"", HFI_START, start, 1);
		if (!start_answered) {
			printf("adreno: %u: the firmware did not answer \"start\"; reading the 3D core anyway\n", step);
		}
	}
	if (!core_granted) {
		/*
		 * The way the vendor asks for a speed without an HFI message (adreno_a6xx_gmu.c:
		 * a6xx_gmu_dcvs_nohfi, 697-728): leave the wanted level in the GMU's own DCVS
		 * registers, ask over the DCVS out-of-band line, and read the result in
		 * DCVS_RETURN. This is worth trying here for itself and not only as a fallback:
		 * the vote travels through the same hardware that holds the 3D core's clock
		 * (above), so if anything raises gfx3d_clk on this machine, it is this.
		 */
		put(&gmu, "dcvs_ack_option", GMU_DCVS_ACK_OPTION, 0);       /* DCVS_ACK_NONBLOCK (kgsl_hfi.h:76) */
		put(&gmu, "dcvs_perf_setting", GMU_DCVS_PERF_SETTING, (vote[2] & 0xffu) | 3u << 28);
		put(&gmu, "dcvs_bw_setting", GMU_DCVS_BW_SETTING, 0);       /* FREQ_VOTE with CLKSET_OPTION_ATLEAST
		                                                            * (kgsl_hfi.h:74, adreno_a6xx_gmu.c:697-698);
		                                                            * BW_VOTE of the one level of our bus table */
		if (oob_ask("a speed through its DCVS engine", GMU_OOB_DCVS, GMU_OOB_DCVS_ACK, &active) ||
		    oob_ask("a speed in the old encoding", GMU_OOB_DCVS_LEGACY, GMU_OOB_DCVS_LEGACY, &active)) {
			get(&gmu, "dcvs_return", GMU_DCVS_RETURN);
		}
	}
	/*
	 * The speed wanted, through the GMU's own power management. Every message is answered by
	 * this firmware (both of its drivers poll for it), so no answer here means its DCVS is
	 * not ready -- but the host has voted the rail itself above, so it is not fatal.
	 */
	if (!hfi_send("the speed wanted", HFI_GX_BW_PERF_VOTE, vote, 4)) {
		printf("adreno: %u: continuing without the GMU's power management: the host's own vote stands\n", step);
	}
	/* What the GMU's firmware has done with all this: whether it still holds the 3D core's
	 * switch and clock off, the state of its RPMh votes, and the speed actually running. */
	status = rd(gmu.base + GMU_SPTPRAC_PWR_CLK_STATUS);
	gmu_vote_state("after \"start\" and the speed vote");
	if (status & (SPTPRAC_GX_GDSC_OFF | SPTPRAC_GX_CLK_OFF)) {
		printf("adreno: %u: the GMU still keeps the 3D core off (oob_gpu %#x, status %#x); the GX domain and "
		    "its clock are on, so the 3D registers are read anyway\n", step, active, status);
	}
	printf("adreno: %u: measured: the 3D core's clock runs at %u Hz (%u MHz was asked for), the GMU's in it at %u Hz\n", step,
	    measure_quietly(GPUCC_DEBUG_GX_GFX3D), core_khz / 1000, measure_quietly(GPUCC_DEBUG_GX_GMU));

	announce("read registers of the 3D core itself (0x3d00000): the first time it has power");
	get(&gpu, "rbbm_status", RBBM_STATUS);
	get(&gpu, "rbbm_int_0_status", RBBM_INT_0_STATUS);
	get(&gpu, "rbbm_clock_cntl", RBBM_CLOCK_CNTL);
	get(&gpu, "cp_rb_base", CP_RB_BASE);
	get(&gpu, "rbbm_secvid_trust_cntl (before the zap)", GPU_WORD(0xF400));
	/* A CX-side read returning zero is not proof that the GX clocked core
	 * answered. CP_RB_BASE only proves accessibility after a nonzero write
	 * reads back and the command processor advances its ring in step 20. */
	printf("adreno: %u: 3D register probe finished; CP response is not established (CP_RB_BASE %#x, GX status %#x)\n",
	    step, rd(gpu.base + CP_RB_BASE), rd(gmu.base + GMU_SPTPRAC_PWR_CLK_STATUS));
	return !space_faulted(&gmu_space);
}

/* ---- step 18: the 3D core's memory and its microcode -------------------------------------------------------- */

/*
 * The 3D core takes its orders as words in a ring of memory, which a small processor inside it
 * ("SQE", with microcode of its own: a630_sqe.fw) reads up to where a register says we have
 * written. Like the GMU it reaches memory through the SMMU: stream 0, to which this gives
 * context 0, with tables of ours that hold the ring, the microcode and later what is drawn into.
 * What is written to the core's registers before its processor is started, and in what order,
 * is Linux's a6xx_gpu.c hw_init for this GPU, less what only matters once it draws (clock gating,
 * the protection of registers against user programs' commands, counters).
 */
#define CORE_STREAM             0
#define CORE_MATCH              0
#define PTE_CORE                (PTE_GMU | PTE_UNPRIVILEGED)
#define PTE_READ_ONLY           (1ull << 7)     /* Linux gives the core its microcode so (MSM_BO_GPU_READONLY) */
#define CORE_RING_ADDRESS       0x01000000u
#define CORE_RING_SIZE          0x8000u         /* Linux's; 8192 words */
#define CORE_SQE_ADDRESS        0x01100000u
#define CORE_SCRATCH_ADDRESS    0x01200000u
#define CORE_TARGET_ADDRESS     0x10000000u
#define CORE_NOWHERE            0x7f000000u     /* never given anything */

#define GBIF_QSB_SIDE0          (4 * 0x03)      /* of the bus interface (step 6) */
#define RBBM_GBIF_CLIENT_QOS_CNTL GPU_WORD(0x0011)
#define RBBM_INTERFACE_HANG_INT_CNTL GPU_WORD(0x001f)
#define RBBM_PERFCTR_CNTL       GPU_WORD(0x0500)
#define RBBM_PERFCTR_GPU_BUSY_MASKED GPU_WORD(0x050b)
#define CP_RB_CNTL              GPU_WORD(0x0802)
#define CP_RB_RPTR_ADDR_LO      GPU_WORD(0x0804)
#define CP_RB_RPTR_ADDR_HI      GPU_WORD(0x0805)
#define CP_RB_RPTR              GPU_WORD(0x0806)
#define CP_RB_WPTR              GPU_WORD(0x0807)
#define CP_SQE_CNTL             GPU_WORD(0x0808)
#define CP_HW_FAULT             GPU_WORD(0x0821)
#define CP_INTERRUPT_STATUS     GPU_WORD(0x0823)
#define CP_PROTECT_STATUS       GPU_WORD(0x0824)
#define CP_SQE_INSTR_BASE       GPU_WORD(0x0830)
#define CP_CHICKEN_DBG          GPU_WORD(0x0841)
#define CP_ADDR_MODE_CNTL       GPU_WORD(0x0842)
#define CP_APRIV_CNTL           GPU_WORD(0x0844)
#define CP_ROQ_THRESHOLDS_1     GPU_WORD(0x08c1)
#define CP_ROQ_THRESHOLDS_2     GPU_WORD(0x08c2)
#define CP_MEM_POOL_SIZE        GPU_WORD(0x08c3)
#define CP_AHB_CNTL             GPU_WORD(0x098d)
#define CP_LPAC_PROG_FIFO_SIZE  GPU_WORD(0x0b34)
#define UCHE_WRITE_RANGE_MAX    GPU_WORD(0x0e05)
#define UCHE_WRITE_THRU_BASE    GPU_WORD(0x0e07)
#define UCHE_TRAP_BASE          GPU_WORD(0x0e09)
#define UCHE_CACHE_WAYS         GPU_WORD(0x0e17)
#define UCHE_FILTER_CNTL        GPU_WORD(0x0e18)
#define UCHE_CLIENT_PF          GPU_WORD(0x0e19)
#define RB_DBG_ECO_CNTL         GPU_WORD(0x8e04)
#define PC_DBG_ECO_CNTL         GPU_WORD(0x9e00)
#define RBBM_SECVID_TSB_TRUSTED_BASE GPU_WORD(0xf800)
#define RBBM_SECVID_TSB_TRUSTED_SIZE GPU_WORD(0xf802)
#define RBBM_SECVID_TSB_CNTL    GPU_WORD(0xf803)
#define RBBM_SECVID_TSB_ADDR_MODE_CNTL GPU_WORD(0xf810)
#define CP_RB_CNTL_VALUE        (12u | 2u << 8 | 1u << 27)      /* 2^12 pairs of words; do not copy the read position to memory */

/* commands (Mesa's adreno_pm4.xml); a packet is a header word and so many more */
#define CP_NOP                  0x10
#define CP_WAIT_FOR_IDLE        0x26
#define CP_BLIT                 0x2c
#define CP_MEM_WRITE            0x3d
#define CP_EVENT_WRITE          0x46
#define CP_ME_INIT              0x48
#define CP_SET_MARKER           0x65
#define CP_SET_SECURE_MODE      0x66

/*
 * Linux (qcom_adreno_smmu_write_sctlr): after a fault the core's other requests are served, or it
 * hangs. And the core has a second stream, 1 (dt: gfx3d_lpac; Linux's sc7280.dtsi gives the GPU
 * both): once started, the microcode starts a second command processor ("LPAC"), which fetches
 * its own part of the microcode as that stream at once -- seen on the phone as a request for
 * 0x7e60 into the microcode, refused by an empty context given to stream 1 (adreno_stream1=2).
 * With nothing at all set up for stream 1 the phone resets without a word the moment the
 * command processor is started (adreno_stream1=0); a start with the microcode nowhere
 * (adreno_blind=1) does not get that far.
 */
#define SMMU_SCTLR_SERVE_AFTER_FAULT (1u << 8)
static struct gpu_space core_space = { .name = "the 3D core", .context = 0, .control = SMMU_SCTLR_SERVE_AFTER_FAULT };
/* adreno_stream1=2: the second stream gets a context of its own with nothing in it, to see what it asks for */
static struct gpu_space second_space = {
	.name = "the 3D core's second stream", .context = 2, .control = SMMU_SCTLR_SERVE_AFTER_FAULT,
};
#define SECOND_MATCH            2
static bool second_space_is_on, second_stream_is_on;
static struct gpu_block core_ring, core_sqe, core_scratch, core_target;
static bool core_space_is_on, core_runs, zap_is_loaded;
static uint32_t core_match = CORE_MATCH;  /* the stream table entry stream 0 actually goes through */
static uint64_t cp_ring_expected;        /* CP_RB_BASE as programmed (IOVA, or physical for the probe) */
#define RING_WORDS              (CORE_RING_SIZE / 4)
static uint32_t ring_at;                /* where the next word goes: the ring goes round */
static uint32_t ring_sent;              /* up to where the core has been told to read */

static uint32_t
odd_parity(uint32_t v)
{
	return 0x9669 >> (0xf & (v ^ v >> 4 ^ v >> 8 ^ v >> 12 ^ v >> 16 ^ v >> 20 ^ v >> 24 ^ v >> 28)) & 1;
}

static void
ring(uint32_t word)
{
	((uint32_t *)core_ring.memory)[ring_at] = word;
	ring_at = (ring_at + 1) % RING_WORDS;
}

/* The header of a command with so many words after it. */
static void
ring_command(uint32_t command, uint32_t words)
{
	ring(7u << 28 | words | odd_parity(words) << 15 | command << 16 | odd_parity(command) << 23);
}

/* A register of the 3D core (by its word number) written by command, in the order of the other commands. */
static void
ring_register(uint32_t number, uint32_t value)
{
	ring(4u << 28 | 1 | odd_parity(1) << 7 | number << 8 | odd_parity(number) << 27);
	ring(value);
}

/* What has been written to the ring since the last time: out to memory, and the core told how far to read. */
static void
ring_send(void)
{
	if (ring_at >= ring_sent) {
		to_memory(core_ring.memory + 4 * ring_sent, 4 * (ring_at - ring_sent));
	} else {
		to_memory(core_ring.memory + 4 * ring_sent, 4 * (RING_WORDS - ring_sent));
		to_memory(core_ring.memory, 4 * ring_at);
	}
	ring_sent = ring_at;
	wr(gpu.base + CP_RB_WPTR, ring_at);
	/* The kept GPU submits thousands of times during interaction. Do not
	 * serialize every submit on two MMIO reads and character-by-character
	 * loader logging; bring-up and explicit gpu_log retain the diagnostic.
	 * Fault/timeout reports are separate and are never suppressed here. */
	if (!quiet || gpu_log) {
		printf("adreno: %u: ring WPTR written %u, reads %u; RPTR %u\n", step, ring_at, rd(gpu.base + CP_RB_WPTR),
		    rd(gpu.base + CP_RB_RPTR));
	}
}

/* Let the 3D core have what is in the ring, and wait until it has taken all of it and rests. */
static bool
ring_run(const char *what)
{
	uint32_t taken = ~0u, status = ~0u;

	printf("adreno: %u: about to have the 3D core %s: the ring up to word %u\n", step, what, ring_at);
	snprintf(last_write, sizeof(last_write), "step %u had the 3D core %s", step, what);
	hold(true);
	ring_send();
	for (int i = 0; i < 20000 && (taken != ring_at || (status & ~1u)); i++) {
		sleep_us(100);
		taken = rd(gpu.base + CP_RB_RPTR);
		status = rd(gpu.base + RBBM_STATUS);
	}
	if (taken == ring_at && !(status & ~1u) && rd(gpu.base + CP_HW_FAULT) == 0 && !space_faulted(&core_space)) {
		printf("adreno: %u: the 3D core has done it (ring read up to %u, status %#x)\n", step, taken, status);
		return true;
	}
	printf("adreno: %u: the 3D core does not get through: ring read up to %u of %u, status %#x, interrupts %#x, "
	    "command processor: fault %#x, interrupts %#x, protection %#x; rptr word in memory %#x\n", step, taken, ring_at,
	    status, rd(gpu.base + RBBM_INT_0_STATUS), rd(gpu.base + CP_HW_FAULT), rd(gpu.base + CP_INTERRUPT_STATUS),
	    rd(gpu.base + CP_PROTECT_STATUS), core_scratch.memory ? *(volatile uint32_t *)core_scratch.memory : 0);
	space_faulted(&core_space);
	return false;
}

static void
put64(struct block *b, const char *name, uint32_t offset, uint64_t value)
{
	put(b, name, offset, (uint32_t)value);
	put(b, name, offset + 4, (uint32_t)(value >> 32));
}

/*
 * Which entries of the stream table will the hypervisor let us write, and with which stream?
 * One entry was seen to take the GMU's stream 5 at once and to read back zero for stream 0
 * however many times it was written. This writes every free entry both ways and reads it back
 * to tell whether it is the entry's refusal or the stream's: on the phone it is the stream's,
 * every entry takes stream 5 and refuses stream 0, which fits "qcom,skip-init" -- the boot
 * loader's table already routes the 3D core's stream and the hypervisor will not have it
 * moved. What an entry holds when we come to it is left behind when we go. The GMU's own entry
 * is not touched.
 */
static uint32_t smr_takes_stream0;    /* bit m: entry m was seen to accept a match for stream 0 */

static void
smr_write_test(void)
{
	for (uint32_t m = 0; m < smmu_matches && m < 6; m++) {
		uint32_t smr_old, s2cr_old, v;
		bool zero, five, user_mask, user_sid1, routes;

		if (m == GMU_MATCH) {
			continue;
		}
		smr_old = rd(smmu.base + SMMU_SMR(m));
		s2cr_old = rd(smmu.base + SMMU_S2CR(m));
		wr(smmu.base + SMMU_SMR(m), SMMU_SMR_VALID | SMMU_CORE_STREAM_MASK << 16 | CORE_STREAM);
		zero = rd(smmu.base + SMMU_SMR(m)) == (SMMU_SMR_VALID | SMMU_CORE_STREAM_MASK << 16 | CORE_STREAM);
		wr(smmu.base + SMMU_SMR(m), SMMU_SMR_VALID | SMMU_GMU_STREAM_MASK << 16 | GMU_STREAM);
		five = rd(smmu.base + SMMU_SMR(m)) == (SMMU_SMR_VALID | SMMU_GMU_STREAM_MASK << 16 | GMU_STREAM);
		/* PCLM10 DT gfx3d_user is SID 0 with mask 0x401. Probe that exact
		 * mask and its adjacent SID without changing the selected S2CR. */
		wr(smmu.base + SMMU_SMR(m), SMMU_SMR_VALID | 0x401u << 16 | CORE_STREAM);
		user_mask = rd(smmu.base + SMMU_SMR(m)) == (SMMU_SMR_VALID | 0x401u << 16 | CORE_STREAM);
		wr(smmu.base + SMMU_SMR(m), SMMU_SMR_VALID | 0x401u << 16 | 1u);
		user_sid1 = rd(smmu.base + SMMU_SMR(m)) == (SMMU_SMR_VALID | 0x401u << 16 | 1u);
		wr(smmu.base + SMMU_SMR(m), 0);
		wr(smmu.base + SMMU_S2CR(m), 0);
		v = rd(smmu.base + SMMU_S2CR(m));
		routes = (v & 0x300ff) == 0;
		wr(smmu.base + SMMU_S2CR(m), s2cr_old);
		wr(smmu.base + SMMU_SMR(m), smr_old);
		if (zero) {
			smr_takes_stream0 |= 1u << m;
		}
		printf("adreno: %u: stream table entry %u: SID0/mask400 %s, SID5/mask400 %s, "
		    "DT SID0/mask401 %s, SID1/mask401 %s, routing to a context %s\n", step, m,
		    zero ? "taken" : "refused", five ? "taken" : "refused",
		    user_mask ? "taken" : "refused", user_sid1 ? "taken" : "refused",
		    routes ? "taken" : "refused");
	}
}

/*
 * Context 0 through whichever stream table entry the hypervisor lets us write. If through none,
 * the real table is the boot loader's and left as it set it up (the device tree says
 * "qcom,skip-init"), and stream 0 may well reach context 0 already: write the context's own
 * registers, which are the hardware's, and let the 3D core's own requests say whether it does.
 */
static bool
core_space_on(void)
{
	static const uint32_t candidates[] = { 0, 2, 3, 4, 5 };

	for (uint32_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
		uint32_t m = candidates[i];

		if (!(smr_takes_stream0 & (1u << m))) {
			continue;
		}
		if (space_on(&core_space, m, CORE_STREAM, SMMU_CORE_STREAM_MASK, true)) {
			core_match = m;
			printf("adreno: %u: stream 0 goes through context 0 by stream table entry %u\n", step, m);
			return true;
		}
	}
	printf("adreno: %u: no stream table entry is ours to write: the boot loader's own table may already route "
	    "stream 0 to context 0; the 3D core's requests will tell\n", step);
	core_match = CORE_MATCH;
	return space_on(&core_space, CORE_MATCH, CORE_STREAM, SMMU_CORE_STREAM_MASK, false);
}

static bool
step_core_space(void)
{
	const uint8_t *microcode;
	uint64_t size = 0;
	uint32_t stream1 = 1, shared_cb = 0;

	if (!cx_ready() || !core_is_on) {
		return false;
	}
	PE_parse_boot_argn("adreno_stream1", &stream1, sizeof(stream1));
	PE_parse_boot_argn("adreno_shared_cb", &shared_cb, sizeof(shared_cb));
	announce("make the 3D core's memory: its ring, its microcode (fw/a630_sqe.fw), a page to try writing to");
	microcode = image_file("fw", "a630_sqe.fw", &size);
	if (microcode == NULL || size < 8 || size > 0x40000) {
		return false;
	}
	/* adreno_cp_phys=1: the CP's instruction/ring fetch may bypass the
	 * SMMU. A "physical address" test is only meaningful if the buffer is
	 * physically contiguous, so allocate these three blocks that way for
	 * the probe. */
	{
		uint32_t cp_phys = 0, sqe_priv = 0;
		uint64_t sqe_attr = PTE_CORE | PTE_READ_ONLY;
		struct gpu_space *cp_space = shared_cb ? &gmu_space : &core_space;
		PE_parse_boot_argn("adreno_cp_phys", &cp_phys, sizeof(cp_phys));
		PE_parse_boot_argn("adreno_sqe_priv", &sqe_priv, sizeof(sqe_priv));
		if (sqe_priv) {
			/* The CP's instruction fetch may be privileged; AP[1]=1
			 * (PTE_UNPRIVILEGED) might then deny it. Probe without
			 * that bit on the SQE page only. */
			sqe_attr = PTE_GMU | PTE_READ_ONLY;
		}
		if (cp_phys) {
			if (!block_new_contig(cp_space, &core_ring, CORE_RING_ADDRESS, CORE_RING_SIZE, PTE_CORE) ||
			    !block_new_contig(cp_space, &core_sqe, CORE_SQE_ADDRESS, (uint32_t)size, sqe_attr) ||
			    !block_new_contig(cp_space, &core_scratch, CORE_SCRATCH_ADDRESS, 4096, PTE_CORE)) {
				return false;
			}
			printf("adreno: %u: contiguous CP blocks: ring phys %#llx, sqe phys %#llx\n", step,
			    (unsigned long long)kvtophys(core_ring.memory), (unsigned long long)kvtophys(core_sqe.memory));
		} else if (!block_new(cp_space, &core_ring, CORE_RING_ADDRESS, CORE_RING_SIZE, PTE_CORE) ||
		    !block_new(cp_space, &core_sqe, CORE_SQE_ADDRESS, (uint32_t)size, sqe_attr) ||
		    !block_new(cp_space, &core_scratch, CORE_SCRATCH_ADDRESS, 4096, PTE_CORE)) {
			return false;
		}
	}
	/* Linux's adreno_fw_create_bo() allocates fw->size - 4 and copies
	 * &fw->data[4]: the first dword is a header (its low 12 bits are the
	 * microcode version: 0x190 here), not an instruction. */
	memcpy((void *)core_sqe.memory, microcode + 4, size - 4);
	to_memory(core_sqe.memory, core_sqe.size);
	{
		uint32_t inspect = 0;

		if (PE_parse_boot_argn("adreno_walk", &inspect, sizeof(inspect)) && inspect) {
			struct gpu_space *s = shared_cb ? &gmu_space : &core_space;
			bool sqe_ok = space_inspect(s, CORE_SQE_ADDRESS, kvtophys(core_sqe.memory));
			bool ring_ok = space_inspect(s, CORE_RING_ADDRESS, kvtophys(core_ring.memory));

			printf("adreno: %u: CPU-side page-table inspection: SQE %s, ring %s; no GPU translation verified\n",
			    step, sqe_ok ? "matches" : "MISMATCH", ring_ok ? "matches" : "MISMATCH");
		}
	}

	if (shared_cb) {
		/* One page table for GMU and core, as kgsl has it: the blocks
		 * just went into the GMU's space. Test whether the CP's fetch
		 * really goes through SMR entry 1 / context bank 1 by pointing
		 * that entry at stream 0. (The GMU's stream 5 loses its route
		 * for this diagnostic; the CP result is what is being read.) */
		uint32_t smr0 = 0, s2cr0 = 0;

		/* If unmatched streams use S2CR[0] as their default, pointing that
		 * entry at the GMU's context bank sends stream 0 through the table
		 * that demonstrably translates (the GMU's). No SMR edit needed. */
		if (PE_parse_boot_argn("adreno_s2cr0_cb", &s2cr0, sizeof(s2cr0)) && s2cr0) {
			put(&smmu, "s2cr[0] (default stream)", SMMU_S2CR(0), s2cr0 & 0xff);
			printf("adreno: %u: S2CR[0] now reads %#x (context %u wanted)\n", step,
			    rd(smmu.base + SMMU_S2CR(0)), s2cr0 & 0xff);
		}
		if (PE_parse_boot_argn("adreno_smr0_for_core", &smr0, sizeof(smr0)) && smr0) {
			put(&smmu, "smr (stream 0 for the core)", SMMU_SMR(GMU_MATCH),
			    SMMU_SMR_VALID | SMMU_CORE_STREAM_MASK << 16 | CORE_STREAM);
			printf("adreno: %u: SMR entry %u now reads %#x (stream 0 wanted %#x)\n", step, GMU_MATCH,
			    rd(smmu.base + SMMU_SMR(GMU_MATCH)),
			    SMMU_SMR_VALID | SMMU_CORE_STREAM_MASK << 16 | CORE_STREAM);
		} else {
			put(&smmu, "smr (streams 0 and 5)", SMMU_SMR(GMU_MATCH),
			    SMMU_SMR_VALID | 0x405u << 16 | GMU_STREAM);
			printf("adreno: %u: shared page table: smr %#x (want %#x), core blocks in the GMU space\n", step,
			    rd(smmu.base + SMMU_SMR(GMU_MATCH)), SMMU_SMR_VALID | 0x405u << 16 | GMU_STREAM);
		}
		core_space_is_on = true;
		get(&gpu, "rbbm_status", RBBM_STATUS);
		return !space_faulted(&gmu_space);
	}
	{
		uint32_t smr_test = 0;

		/* Are SMR writes honoured at all? Write a different stream ID to
		 * entry 1 and read it back. If it stays 5, the bootloader's table
		 * is locked and entry 1 was pre-set for the GMU. */
		if (PE_parse_boot_argn("adreno_smr_test", &smr_test, sizeof(smr_test)) && smr_test) {
			uint32_t before = rd(smmu.base + SMMU_SMR(GMU_MATCH));
			uint32_t want = SMMU_SMR_VALID | SMMU_CORE_STREAM_MASK << 16 | 7;

			put(&smmu, "smr[1] (test)", SMMU_SMR(GMU_MATCH), want);
		printf("adreno: %u: SMR[1] DT-mask test: before %#x, wrote %#x, reads %#x\n", step, before, want,
			    rd(smmu.base + SMMU_SMR(GMU_MATCH)));
			/* Does an otherwise free entry accept an ID, and does it
			 * accept ID 0 specifically? */
			put(&smmu, "smr[0] (id 7)", SMMU_SMR(0), SMMU_SMR_VALID | SMMU_CORE_STREAM_MASK << 16 | 7);
		printf("adreno: %u: SMR[0] DT-mask with id 7 reads %#x\n", step, rd(smmu.base + SMMU_SMR(0)));
			put(&smmu, "smr[0] (id 0)", SMMU_SMR(0), SMMU_SMR_VALID | SMMU_CORE_STREAM_MASK << 16 | 0);
		printf("adreno: %u: SMR[0] DT-mask with id 0 reads %#x\n", step, rd(smmu.base + SMMU_SMR(0)));
		}
	}
	{
		uint32_t core_cb = 0;

		if (PE_parse_boot_argn("adreno_core_cb", &core_cb, sizeof(core_cb))) {
			/* kgsl's kgsl_program_smmu_aperture() passes the context
			 * bank its GPU page table was actually assigned; probe
			 * which bank this phone's hypervisor accepts. */
			core_space.context = core_cb & 0xf;
			printf("adreno: %u: core page table on context bank %u\n", step, core_space.context);
		}
	}
	announce("give the SMMU's context 0 these tables and send the 3D core's requests (stream 0) through it");
	core_space_is_on = true;
	smr_write_test();
	if (!core_space_on()) {
		return false;
	}
	{
		uint32_t secure_pt = 0;

		if (PE_parse_boot_argn("adreno_secure_pt", &secure_pt, sizeof(secure_pt)) && secure_pt) {
			if (!space_cb_on(&core_space, 2, 0x0au)) {         /* VMID_CP_PIXEL */
				printf("adreno: %u: the secure context did not come on\n", step);
			}
		}
	}
	if (stream1 == 2) {
		if ((second_space.root = space_new_table(&second_space)) == NULL) {
			return false;
		}
		second_space_is_on = true;
		if (!space_on(&second_space, SECOND_MATCH, 1, SMMU_GMU_STREAM_MASK, true)) {
			return false;
		}
	} else if (stream1 == 1) {
		second_stream_is_on = true;
		if (!space_stream(&core_space, SECOND_MATCH, 1, SMMU_GMU_STREAM_MASK)) {
			return false;
		}
	}
	get(&gpu, "rbbm_status", RBBM_STATUS);
	return !space_faulted(&core_space);
}

/* ---- step 19: the zap shader, loaded -------------------------------------------------------------------- */

/*
 * The rest of what Linux does with the zap shader (mdt_loader.c: qcom_mdt_pas_init,
 * qcom_mdt_load; adreno_gpu.c: zap_shader_load_mdt): after the header (step 12), TrustZone is
 * told where the image will be -- the two pages the device tree reserves for it
 * (reserved-memory gpu_micro_code@8b71a000, the zap-shader node's memory-region) --, the image's
 * one segment (the file a640_zap.b02, relocatable: at the start of that memory) is put there,
 * and TrustZone is asked to authenticate it and run it. The vendor's driver does this before it
 * starts the 3D core's command processor (a6xx_rb_start: a6xx_microcode_load), Linux after;
 * this phone's own order is kept. Once that is done the memory is TrustZone's, and nothing
 * here can see whether it is so; to write there again would be the end of the run. Our kernels
 * come and go without the phone restarting, so a kernel leaves no zap shader behind: when it
 * gives the GPU back it has TrustZone take the shader away again (gpu_block_stop(); the
 * vendor's driver does the same when it is unloaded: kgsl_zap_shader_unload), and the next one
 * loads it as if the phone had just started. A kernel that did not end in order cannot have
 * done that, and mainline Linux asks for that giving-up here first (qcom_mdt_pas_init begins
 * with qcom_scm_pas_reset). On this phone that first question is what killed the kernel of
 * 2026-11-04 02:00: the call that gives an image up (command 6, "shutdown" in this vendor's
 * numbering, qcom_scm_pas_shutdown) was asked of an image TrustZone had never been given, and
 * TrustZone never answered -- the SMC sat there until the watchdog ended the kernel. The
 * vendor's loader never asks it (mdt_loader.c: qcom_mdt_pas_init goes straight to the
 * signature), and neither do we: a shader left by a kernel that ended in order is gone, and
 * one left by a kernel that did not shows up as the signature or the place being refused,
 * which is taken to mean that TrustZone keeps a working shader: it is used as it is, and
 * nothing is written. (Step 21 is what tells whether the 3D core gets out of secure mode with
 * it.) There was a boot-arg adreno_zap=0 for the second and later runs of a started phone;
 * nothing reads it now.
 *
 * The memory is ours to allocate: this phone's kgsl-hyp node has no memory-region, and that is the
 * case the vendor's loader handles by allocating one (peripheral-loader.c pil_alloc_region ->
 * dma_alloc_attrs: 4 KiB aligned, contiguous, zeroed, page-rounded), then telling TrustZone where
 * it is (pil_boot: mem_setup(region_start, region_size)). A page of ours with its cache written
 * out is the same thing.
 */
#define ZAP_MEMORY_SIZE         0x1000u

static vm_offset_t zap_page;            /* the region TrustZone is given; it is TrustZone's after the auth */
static uint64_t zap_size;               /* its size: the MDT image rounded to pages (0x4000 here) */
static uint64_t zap_phys;
#define SCM_PAS_MEM_SETUP       SCM_CALL(2, 2)
#define SCM_THREE_VALUES        3

/* TrustZone is to give the zap shader up, and its memory with it. Only ever asked of an image we
 * started ourselves and are handing back (gpu_block_stop); asked before the image has been given,
 * TrustZone does not answer at all. */
static bool
zap_unload(void)
{
	uint64_t answer = ~0ull;

	return scm("to give up the zap shader", SCM_64 | SCM_SIP | SCM_PAS_SHUTDOWN, SCM_ONE_VALUE, PAS_GPU, 0, 0, &answer) == 0 &&
	       answer == 0;
}

static bool
step_zap_load(void)
{
	const uint8_t *segment;
	volatile uint8_t *memory;
	uint64_t size = 0, header = 0, answer = ~0ull;

	if (!cx_ready() || !core_is_on) {
		return false;
	}
	announce("hand the zap shader's header to TrustZone, tell it where the image goes, put the image there, and ask "
	    "TrustZone to authenticate and run it");
	/* What the boot firmware left in the trusted-window registers, before this driver zeroes
	 * them: the vendor's _set_secvid() writes mmu.secure_base (a VA window) and secure_size
	 * here, and TrustZone's zap may need exactly that to run. Zero means it was never set. */
	printf("adreno: %u: RBBM_SECVID_TSB_CNTL %#x, trusted base %#x %#x, size %#x (as found)\n", step,
	    rd(gpu.base + RBBM_SECVID_TSB_CNTL), rd(gpu.base + RBBM_SECVID_TSB_TRUSTED_BASE),
	    rd(gpu.base + RBBM_SECVID_TSB_TRUSTED_BASE + 4), rd(gpu.base + RBBM_SECVID_TSB_TRUSTED_SIZE));
	/*
	 * The vendor's _set_secvid() (firmware/adreno.c) writes three of these before the GPU
	 * starts, but only when the MMU is "secured": SECVID_TSB_CNTL 0, SECVID_TSB_TRUSTED_BASE
	 * = mmu.secure_base, SIZE = mmu.secure_size. kgsl_iommu.h gives them: secure_size is
	 * SZ_256M and secure_base is KGSL_IOMMU_GLOBAL_MEM_BASE - secure_size, i.e. 0xec000000
	 * with the 64-bit global base 0xfc000000, or 0xe8000000 with the 32-bit 0xf8000000.
	 * The boot firmware left all three at 0, so TrustZone's zap shader has had no trusted
	 * window to run in. adreno_tsb=1 (64-bit base) or 2 (32-bit base) sets it before the auth.
	 */
	{
		uint32_t tsb = 0;

		if (PE_parse_boot_argn("adreno_tsb", &tsb, sizeof(tsb)) && tsb) {
			uint32_t base = tsb == 2 ? 0xe8000000u : 0xec000000u;

			put(&gpu, "rbbm_secvid_tsb_cntl", RBBM_SECVID_TSB_CNTL, 0);
			put64(&gpu, "rbbm_secvid_tsb_trusted_base", RBBM_SECVID_TSB_TRUSTED_BASE, base);
			put(&gpu, "rbbm_secvid_tsb_trusted_size", RBBM_SECVID_TSB_TRUSTED_SIZE, 0x10000000u);
			printf("adreno: %u: trusted window set for the zap: base %#x, size %#x (adreno_tsb=%u)\n",
			    step, base, 0x10000000u, tsb);
		}
	}
	/* The zap image is an MDT whose only PT_LOAD segment is the relocatable
	 * one in a640_zap.b02 (file offset 0x3000, paddr 0x5000). mdt_loader.c
	 * puts a relocatable segment at offset 0 of a region the size of its
	 * p_memsz (0x1000), not at its p_offset. So the region is one page and
	 * b02 goes at its start. */
	segment = image_file("fw", "a640_zap.b02", &size);
	if (segment == NULL || size == 0 || size > ZAP_MEMORY_SIZE) {
		return false;
	}
	if (!zap_header(&header)) {
		return false;
	}
	if (scm("to take the zap shader's signature", SCM_64 | SCM_SIP | SCM_PAS_INIT_IMAGE, SCM_VALUE_AND_BUFFER, PAS_GPU,
	    header, 0, &answer) != 0 || answer != 0) {
		printf("adreno: %u: TrustZone does not take the signature (%#llx): TAKEN TO MEAN THAT IT KEEPS A ZAP SHADER FROM "
		    "BEFORE, which is used as it is\n", step, answer);
		zap_is_loaded = true;
		return true;
	}
	if (zap_page == 0 &&
	    kmem_alloc(kernel_map, &zap_page, PAGE_SIZE, KMA_KOBJECT | KMA_ZERO | KMA_DATA, VM_KERN_MEMORY_IOKIT) != KERN_SUCCESS) {
		printf("adreno: %u: no memory for the zap shader\n", step);
		return false;
	}
	zap_phys = kvtophys(zap_page);
	if (scm("where the image will be", SCM_64 | SCM_SIP | SCM_PAS_MEM_SETUP, SCM_THREE_VALUES, PAS_GPU, zap_phys,
	    ZAP_MEMORY_SIZE, &answer) != 0 || answer != 0) {
		printf("adreno: %u: TrustZone does not take the place (%#llx): TAKEN TO MEAN THAT IT KEEPS A ZAP SHADER FROM BEFORE, "
		    "which is used as it is\n", step, answer);
		zap_is_loaded = true;
		return true;
	}
	memory = (volatile uint8_t *)zap_page;
	printf("adreno: %u: the region begins %02x %02x %02x %02x, the zap shader %02x %02x %02x %02x\n", step,
	    memory[0], memory[1], memory[2], memory[3], segment[0], segment[1], segment[2], segment[3]);
	printf("adreno: %u: about to write %llu bytes at %#llx\n", step, size, zap_phys);
	snprintf(last_write, sizeof(last_write), "step %u wrote the zap shader into memory at %#llx", step, zap_phys);
	hold(true);
	for (uint64_t i = 0; i < ZAP_MEMORY_SIZE; i++) {
		memory[i] = i < size ? segment[i] : 0;
	}
	to_memory(zap_page, PAGE_SIZE);
	if (scm("to authenticate the zap shader and run it", SCM_64 | SCM_SIP | SCM_PAS_AUTH_AND_RESET, SCM_ONE_VALUE, PAS_GPU, 0, 0,
	    &answer) != 0 || answer != 0) {
		printf("adreno: %u: TrustZone does not authenticate the zap shader (%#llx)\n", step, answer);
		return false;
	}
	need(&gpu);
	printf("adreno: %u: TRUSTZONE HAS AUTHENTICATED AND STARTED THE ZAP SHADER; secvid trust %#x\n", step,
	    rd(gpu.base + GPU_WORD(0xF400)));
	zap_is_loaded = true;
	return true;
}

/* ---- step 20: the 3D core's registers, its command processor started, its first command ------------------- */

static bool
step_core_start(void)
{
	static const uint32_t address_modes[] = { 0x0842, 0x0c01, 0x8601, 0x8e05, 0x9e01, 0xbe05, 0xa601, 0x9601, 0x0e00, 0xae01,
		                                  0xb601, 0xf810 };
	uint32_t blind = 0;

	if (!cx_ready() || !core_is_on || !core_space_is_on) {
		return false;
	}
	/*
	 * Vendor _adreno_start() calls adreno_program_smmu_aperture() for
	 * a640v1 before the core starts: an SCM call that lets the CP's own
	 * fetches use the SMMU context bank the driver programmed. Without
	 * it the CP fetches through whatever the boot firmware left and
	 * executes garbage (CP_INTERRUPT_STATUS bit 7, no SMMU fault recorded
	 * even with CP_SQE_INSTR_BASE pointed at an unmapped address).
	 * QCOM_SCM_SVC_MP 0x0c, QCOM_SCM_MP_CP_SMMU_APERTURE_ID 0x1b, args
	 * {0xffff0000 | context bank, 0xffffffff x3}, arginfo 4.
	 */
	{
		uint32_t ptbl = 0;

		/* Is the secure-page-table SCM interface present, and how big a
		 * table does it want? QCOM_SCM_SVC_MP 0x0c, cmd 0x03,
		 * QCOM_SCM_MP_IOMMU_SECURE_PTBL_SIZE. */
		if (PE_parse_boot_argn("adreno_ptbl_probe", &ptbl, sizeof(ptbl)) && ptbl) {
			uint64_t answer = 0, how = SCM_FAST | SCM_64 | SCM_SIP;
			int32_t r = scm("for the secure page table's size", how | SCM_CALL(0x0c, 0x03), SCM_ONE_VALUE, 0, 0, 0,
			    &answer);

			printf("adreno: %u: secure page table: call %d, answer %#llx, second %#llx\n", step, r,
			    (unsigned long long)answer, (unsigned long long)scm_answer2);
		}
	}
	{
		uint32_t aperture = 0, aperture_cb = 0;

		PE_parse_boot_argn("adreno_aperture_cb", &aperture_cb, sizeof(aperture_cb));
		if (PE_parse_boot_argn("adreno_cp_aperture", &aperture, sizeof(aperture)) && aperture) {
			uint64_t answer = 0, how = SCM_FAST | SCM_64 | SCM_SIP;
			uint32_t cb = aperture_cb & 0xff;
			int32_t r = scm4("to give the CP its SMMU aperture",
			    how | SCM_CALL(0x0c, 0x1b), 4, 0xffff0000u | cb, 0xffffffffu, 0xffffffffu, 0xffffffffu, &answer);

			printf("adreno: %u: CP SMMU aperture (context bank %u): call returned %d, answer %#llx\n", step, cb, r,
			    (unsigned long long)answer);
		}
	}
	/* The zap is started in step 19, but its trust word was read immediately and still said 1.
	 * A shader takes time to run, so wait for it here -- the CP has not been given a ring yet --
	 * and the moment the word clears, program stream table entry 0 again: it was refused in
	 * step 18 because the hypervisor still owned stream 0, and that refusal is the one thing
	 * that keeps the CP from fetching its microcode. */
	{
		uint32_t trust = rd(gpu.base + GPU_WORD(0xF400));
		unsigned waited = 0;

		while (trust != 0 && waited < 200) {
			sleep_ms(1);
			waited++;
			trust = rd(gpu.base + GPU_WORD(0xF400));
		}
		printf("adreno: %u: after the zap (%u ms): RBBM_SECVID_TRUST_CNTL %#x\n", step, waited, trust);
		if (trust == 0) {
			smr_write_test();
			if (!core_space_on()) {
				return false;
			}
			printf("adreno: %u: stream table entry %u is programmed again with stream 0\n", step, core_match);
		}
	}
	announce("write the 3D core's registers as Linux does before it starts the command processor: the first writes "
	    "to the core itself");
	{
		uint32_t secvid = 0;

		if (PE_parse_boot_argn("adreno_secvid", &secvid, sizeof(secvid)) && secvid) {
			/* The device tree's own quirk for this GPU is qcom,gpu-quirk-secvid-set-once: the
			 * trust word latches once after a reset. Every earlier attempt wrote it after the
			 * CP had already been started inside the secure world, and the write hung the bus
			 * (logs/153839, logs/162350). This is the point that is both after the GMU has
			 * brought GX up and before the CP has been given a ring or a microcode address:
			 * write it once, read it back, and let the next register read say whether the
			 * bus survived. */
			uint32_t before = rd(gpu.base + GPU_WORD(0xF400)), after;

			printf("adreno: %u: RBBM_SECVID_TRUST_CNTL before the write: %#x\n", step, before);
			wr(gpu.base + GPU_WORD(0xF400), 0);
			after = rd(gpu.base + GPU_WORD(0xF400));
			printf("adreno: %u: RBBM_SECVID_TRUST_CNTL written 0, reads %#x: %s\n", step, after,
			    after == 0 ? "THE CORE IS OUT OF THE SECURE WORLD" : "still trusted");
		}
	}
	put(&gbif, "halt", GBIF_HALT, 0);
	put(&gpu, "rbbm_gbif_halt", RBBM_GBIF_HALT, 0);
	/* Snapshot only: a successful write to HALT does not establish that the
	 * two interfaces have deasserted their respective acknowledgements. */
	get(&gbif, "halt_ack before CP", GBIF_HALT_ACK);
	get(&gpu, "rbbm_gbif_halt_ack before CP", RBBM_GBIF_HALT_ACK);
	{
		uint32_t observe_ack = 0;

		if (PE_parse_boot_argn("adreno_halt_observe", &observe_ack, sizeof(observe_ack)) && observe_ack) {
			/* Read-only bounded observation; do not assert halt or reset the bus. */
			for (unsigned i = 0; i < 20; i++) {
				sleep_ms(1);
				if ((rd(gbif.base + GBIF_HALT_ACK) & 3u) == 0) {
					break;
				}
			}
			printf("adreno: %u: halt observation after up to 20 ms: cx request %#x ack %#x, gx request %#x ack %#x\n",
			    step, rd(gbif.base + GBIF_HALT), rd(gbif.base + GBIF_HALT_ACK),
			    rd(gpu.base + RBBM_GBIF_HALT), rd(gpu.base + RBBM_GBIF_HALT_ACK));
		}
	}
	put(&gpu, "rbbm_secvid_tsb_cntl", RBBM_SECVID_TSB_CNTL, 0);
	put64(&gpu, "rbbm_secvid_tsb_trusted_base", RBBM_SECVID_TSB_TRUSTED_BASE, 0);
	put(&gpu, "rbbm_secvid_tsb_trusted_size", RBBM_SECVID_TSB_TRUSTED_SIZE, 0);
	for (unsigned i = 0; i < sizeof(address_modes) / sizeof(address_modes[0]); i++) {
		put(&gpu, "address mode: 64 bits", GPU_WORD(address_modes[i]), 1);
	}
	for (unsigned i = 0; i < 4; i++) {
		put(&gbif, "qsb_side", GBIF_QSB_SIDE0 + 4 * i, 0x00071620);
	}
	put(&gpu, "rbbm_gbif_client_qos_cntl", RBBM_GBIF_CLIENT_QOS_CNTL, 3);
	put(&gpu, "rbbm_perfctr_gpu_busy_masked", RBBM_PERFCTR_GPU_BUSY_MASKED, 0xffffffff);
	put64(&gpu, "uche_write_range_max", UCHE_WRITE_RANGE_MAX, 0x0001ffffffffffc0ull);
	put64(&gpu, "uche_trap_base", UCHE_TRAP_BASE, 0x0001fffffffff000ull);
	put64(&gpu, "uche_write_thru_base", UCHE_WRITE_THRU_BASE, 0x0001fffffffff000ull);
	put(&gpu, "uche_filter_cntl", UCHE_FILTER_CNTL, 0x804);
	put(&gpu, "uche_cache_ways", UCHE_CACHE_WAYS, 0x4);
	put(&gpu, "cp_roq_thresholds_2", CP_ROQ_THRESHOLDS_2, 0x02000140);
	put(&gpu, "cp_roq_thresholds_1", CP_ROQ_THRESHOLDS_1, 0x8040362c);
	put(&gpu, "cp_lpac_prog_fifo_size", CP_LPAC_PROG_FIFO_SIZE, 0x20);
	put(&gpu, "cp_mem_pool_size", CP_MEM_POOL_SIZE, 128);
	put(&gpu, "pc_dbg_eco_cntl", PC_DBG_ECO_CNTL, 0x00200200);
	put(&gpu, "cp_ahb_cntl", CP_AHB_CNTL, 1);
	/*
	 * How textures in the GPU's own tiled and compressed layouts are laid out in memory (Linux's
	 * a6xx_calc_ubwc_config, a6xx_set_ubwc_config for this GPU: highest bank bit 14, and the
	 * rest as below); Mesa is told the same (hakios_gpu_parameter()).
	 */
	{
		uint32_t vendor_nc = 0;

		PE_parse_boot_argn("adreno_vendor_nc", &vendor_nc, sizeof(vendor_nc));
		/* PCLM10 DT: highest-bank-bit=15, min-access-length=32,
		 * ubwc-mode=3, macrotiling-channels absent. Vendor a6xx_start:
		 * bit=(15-13), mal=0, mode=0, amsbc=1; do not touch RBBM
		 * NC mode unless the DT says eight channels. */
		put(&gpu, "rb_nc_mode_cntl", GPU_WORD(0x8e08), vendor_nc ? 0x14 : 0x812);
		put(&gpu, "tpl1_nc_mode_cntl", GPU_WORD(0xb604), vendor_nc ? 0x4 : 0x2);
		put(&gpu, "sp_nc_mode_cntl", GPU_WORD(0xae02), vendor_nc ? 0x4 : 0x22);
		put(&gpu, "uche_mode_cntl", GPU_WORD(0x0e01), vendor_nc ? 0x400000 : 0x200000);
		if (!vendor_nc) {
			put(&gpu, "rbbm_nc_mode_cntl", GPU_WORD(0x0534), 1);
		}
	}
	put(&gpu, "rbbm_perfctr_cntl", RBBM_PERFCTR_CNTL, 1);
	put(&gpu, "rbbm_interface_hang_int_cntl", RBBM_INTERFACE_HANG_INT_CNTL, 1u << 30 | 0x1fffff);
	put(&gpu, "uche_client_pf", UCHE_CLIENT_PF, 1u << 7 | 1);
	put(&gpu, "cp_chicken_dbg", CP_CHICKEN_DBG, 1);
	put(&gpu, "rbbm_gbif_client_qos_cntl", RBBM_GBIF_CLIENT_QOS_CNTL, 0);
	/* The upstream driver (which runs the CP on redwood) writes this
	 * unconditionally; Mesa does the same for a6xx. */
	put(&gpu, "cp_apriv_cntl", CP_APRIV_CNTL, 1u << 6 | 1u << 5 | 1u << 3 | 1u << 2 | 1u << 1);
	put(&gpu, "rb_dbg_eco_cntl", RB_DBG_ECO_CNTL, 0x04100000);     /* Mesa's value for this GPU */
	{
		uint32_t vendor_protect = 0;
		static const struct { uint32_t base, count, read_protect; } groups[] = {
			{0x600,0x51,0}, {0xae50,0x2,1}, {0x9624,0x13,1}, {0x8630,0x8,1},
			{0x9e70,0x1,1}, {0x9e78,0x187,1}, {0xf000,0x810,1}, {0xfc00,0x3,0},
			{0x50e,0,1}, {0x50f,0,0}, {0x510,0,1}, {0,0x4f9,0}, {0x501,0xa,0},
			{0x511,0x44,0}, {0xe00,0x1,1}, {0xe03,0xb,1}, {0x8e00,0,1},
			{0x8e50,0xf,1}, {0xbe02,0,1}, {0xbe20,0x11f3,1}, {0x800,0x82,1},
			{0x8a0,0x8,1}, {0x8ab,0x19,1}, {0x900,0x4d,1}, {0x98d,0x76,1},
			{0x8d0,0x23,0}, {0x980,0x4,0}, {0xa630,0,1}, {0x1b400,0x1fff,1},
		};

		if (PE_parse_boot_argn("adreno_vendor_protect", &vendor_protect, sizeof(vendor_protect)) && vendor_protect) {
			/* GPU register ranges only, as in vendor a6xx_protect_init().
			 * The vendor's optional MMU ranges are deliberately not guessed. */
			put(&gpu, "cp_protect_cntl", GPU_WORD(0x084f), 3);
			for (unsigned i = 0; i < sizeof(groups) / sizeof(groups[0]); i++) {
				uint32_t value = groups[i].base | (groups[i].count << 18) |
				    (groups[i].read_protect << 31);
				put(&gpu, "cp_protect_reg", GPU_WORD(0x0850 + i), value);
			}
		}
	}
	/*
	 * adreno_blind=1: the command processor is started with its microcode at an address where
	 * nothing is. All it can do then is ask the SMMU for it, and the SMMU's record of the refusal
	 * shows that its requests come through context 0 as meant, and what they look like.
	 */
	printf("adreno: %u: SQE image first words: %#x %#x %#x %#x; ring first: %#x %#x %#x\n", step,
	    ((volatile uint32_t *)core_sqe.memory)[0], ((volatile uint32_t *)core_sqe.memory)[1],
	    ((volatile uint32_t *)core_sqe.memory)[2], ((volatile uint32_t *)core_sqe.memory)[3],
	    ((volatile uint32_t *)core_ring.memory)[0], ((volatile uint32_t *)core_ring.memory)[1],
	    ((volatile uint32_t *)core_ring.memory)[2]);
	/* Mainline's manual secure-mode exit (write RBBM_SECVID_TRUST_CNTL = 0)
	 * is blocked on this phone even with the zap step skipped: the write
	 * hung the GPU bus (logs/153839, logs/162350). The only exit is the zap
	 * itself, so the register is left alone. */
	PE_parse_boot_argn("adreno_blind", &blind, sizeof(blind));
	/* adreno_cp_phys=1: give the CP the physical addresses of the same pages
	 * instead of their GPU IOVAs. If the CP fetch bypasses the SMMU, this is
	 * what makes the microcode readable; if it still translates, the SMMU
	 * fault record will say so. Diagnostic only. */
	{
		uint32_t cp_phys = 0;
		uint64_t sqe = CORE_SQE_ADDRESS, ring = CORE_RING_ADDRESS, rptr = CORE_SCRATCH_ADDRESS;

		if (PE_parse_boot_argn("adreno_cp_phys", &cp_phys, sizeof(cp_phys)) && cp_phys) {
			sqe = kvtophys(core_sqe.memory);
			ring = kvtophys(core_ring.memory);
			rptr = kvtophys(core_scratch.memory);
			printf("adreno: %u: CP physical-address probe: sqe %#llx ring %#llx rptr %#llx\n", step,
			    (unsigned long long)sqe, (unsigned long long)ring, (unsigned long long)rptr);
		}
		{
			uint32_t keep_base = 0;

			/* Probe: leave whatever CP_SQE_INSTR_BASE the boot firmware
			 * left, in case the CP only accepts that aperture. */
			if (PE_parse_boot_argn("adreno_keep_sqe_base", &keep_base, sizeof(keep_base)) && keep_base) {
				printf("adreno: %u: CP_SQE_INSTR_BASE left as it was: %#x %#x\n", step,
				    rd(gpu.base + CP_SQE_INSTR_BASE), rd(gpu.base + CP_SQE_INSTR_BASE + 4));
			} else {
				put64(&gpu, "cp_sqe_instr_base", CP_SQE_INSTR_BASE, blind ? CORE_NOWHERE : sqe);
			}
		}
		/* The reference upstream driver leaves RPTR_ADDR alone. PCLM10's vendor
		 * driver writes a scratch GPU address before starting SQE. Optional probe
		 * only: this does not establish why the SQE reported illegal instruction. */
		{
			uint32_t vendor_rptr = 0;

			if (PE_parse_boot_argn("adreno_vendor_rptr", &vendor_rptr, sizeof(vendor_rptr)) && vendor_rptr) {
				put64(&gpu, "cp_rb_rptr_addr", CP_RB_RPTR_ADDR_LO, rptr);
				get(&gpu, "cp_rb_rptr_addr low", CP_RB_RPTR_ADDR_LO);
				get(&gpu, "cp_rb_rptr_addr high", CP_RB_RPTR_ADDR_HI);
			}
		}
		put64(&gpu, "cp_rb_base", CP_RB_BASE, ring);
		cp_ring_expected = ring;
	}
	put(&gpu, "cp_rb_cntl", CP_RB_CNTL, CP_RB_CNTL_VALUE);
	get(&gpu, "cp_sqe_instr_base", CP_SQE_INSTR_BASE);
	get(&gpu, "cp_rb_base", CP_RB_BASE);
	get(&gpu, "cp_rb_cntl", CP_RB_CNTL);
	get(&gpu, "cp_apriv_cntl", CP_APRIV_CNTL);
	get(&gpu, "cp_protect_cntl before SQE", GPU_WORD(0x084f));
	for (unsigned i = 0; i < 32; i++) {
		printf("adreno: %u: cp_protect_reg[%u] before SQE = %#x\n", step, i,
		    rd(gpu.base + GPU_WORD(0x0850 + i)));
	}
	get(&gpu, "rbbm_status", RBBM_STATUS);
	get(&gbif, "halt_ack immediately before SQE", GBIF_HALT_ACK);
	get(&gpu, "rbbm_gbif_halt_ack immediately before SQE", RBBM_GBIF_HALT_ACK);
	if (rd(gpu.base + CP_RB_BASE) != cp_ring_expected) {
		printf("adreno: %u: 3D command processor not accessible: CP_RB_BASE wrote %#llx, reads %#x; "
		    "GX clock status %#x; do not start the CP\n", step, (unsigned long long)cp_ring_expected,
		    rd(gpu.base + CP_RB_BASE), rd(gmu.base + GMU_SPTPRAC_PWR_CLK_STATUS));
		return false;
	}

	announce(blind ? "start the 3D core's command processor with its microcode nowhere" :
	    "start the 3D core's command processor");
	core_runs = true;
	/* Clear stale CP interrupt status before starting the SQE: the bits are
	 * sticky, and a previous run's illegal-instruction bit would otherwise
	 * be read as this run's. */
	put(&gpu, "cp_interrupt_status (clear)", GPU_WORD(0x0823), 0xffffffff);
	get(&gpu, "cp_interrupt_status (after clear)", GPU_WORD(0x0823));
	put(&gpu, "cp_sqe_cntl", CP_SQE_CNTL, 1);
	sleep_ms(20);
	printf("adreno: %u: alive after it; status %#x, interrupts %#x, command processor: fault %#x, interrupts %#x, "
	    "protection %#x, ring read up to %u; misc %#x status1 %#x cp2gmu %#x sqe_instr %#x\n", step,
	    rd(gpu.base + RBBM_STATUS), rd(gpu.base + RBBM_INT_0_STATUS), rd(gpu.base + CP_HW_FAULT),
	    rd(gpu.base + CP_INTERRUPT_STATUS), rd(gpu.base + CP_PROTECT_STATUS), rd(gpu.base + CP_RB_RPTR),
	    rd(gpu.base + GPU_WORD(0x0840)), rd(gpu.base + GPU_WORD(0x0825)), rd(gpu.base + GPU_WORD(0x0812)),
	    rd(gpu.base + CP_SQE_INSTR_BASE));
	if (rd(gpu.base + CP_INTERRUPT_STATUS) & (1u << 7)) {
		/* Vendor a6xx_cp_hw_err_callback: on CP_ILLEGAL_INSTR_ERROR it
		 * asks CP_SQE_STAT for the offending opcode. */
		put(&gpu, "cp_sqe_stat_addr", GPU_WORD(0x0908), 1);
		printf("adreno: %u: CP illegal instruction; SQE stat opcode %#x\n", step,
		    rd(gpu.base + GPU_WORD(0x0909)));
	}
	get(&smmu, "sgfsr", SMMU_SGFSR);
	get(&smmu, "sgfsynr0", SMMU_SGFSR + 8);
	get(&smmu, "sgfsynr1", SMMU_SGFSR + 12);
	if (blind) {
		return space_faulted(&core_space);
	}
	if (second_space_is_on) {
		sleep_ms(200);
		return space_faulted(&second_space) | !space_faulted(&core_space);
	}
	if (space_faulted(&core_space)) {
		return false;
	}

	announce("give the 3D core its first command, \"initialize\"");
	ring_command(CP_ME_INIT, 8);            /* a6xx_cp_init */
	ring(0x0000002f);
	ring(0x00000003);                       /* several hardware contexts */
	ring(0x20000000);                       /* error detection */
	ring(0);
	ring(0);
	ring(0);
	ring(0);
	ring(0);
	if (!ring_run("initialize its command processor")) {
		return false;
	}
	printf("adreno: %u: THE 3D CORE TAKES COMMANDS\n", step);
	return true;
}

/* ---- step 21: out of secure mode; a word written on command ------------------------------------------------ */

static bool
step_core_commands(void)
{
	volatile uint32_t *scratch = (volatile uint32_t *)core_scratch.memory;

	if (!cx_ready() || !core_runs) {
		return false;
	}
	announce("command the 3D core out of secure mode (it asks the zap shader), then to write a word into a page of ours");
	ring_command(CP_SET_SECURE_MODE, 1);
	ring(0);
	if (!ring_run("leave secure mode")) {
		return false;
	}
	ring_command(CP_MEM_WRITE, 3);
	ring(CORE_SCRATCH_ADDRESS);
	ring(0);
	ring(0x600dc0de);
	if (!ring_run("write a word to memory")) {
		return false;
	}
	to_memory(core_scratch.memory, 64);
	printf("adreno: %u: the page reads %#x: %s\n", step, scratch[0], scratch[0] == 0x600dc0de ?
	    "THE 3D CORE WRITES OUR MEMORY ON COMMAND" : "not what it was told to write");
	return scratch[0] == 0x600dc0de;
}

/* ---- step 22: a frame filled by the GPU, on the panel ------------------------------------------------------ */

/*
 * The simplest thing the 3D core draws: its 2D engine filling a rectangle with one colour (Mesa's
 * fd6_blitter.cc: fd6_clear_surface; the registers under their older names, as in Linux's
 * a6xx.xml), three times for three bands. The picture is 32-bit pixels in rows of a multiple of
 * 64 bytes, in memory of ours; the framebuffer driver copies it to the panel.
 */
#define RB_CCU_CNTL             0x8e07          /* register numbers: these are written by command */
#define GRAS_2D_BLIT_CNTL       0x8400
#define GRAS_2D_DST_TL          0x8405
#define GRAS_2D_DST_BR          0x8406
#define RB_2D_BLIT_CNTL         0x8c00
#define RB_2D_PIXEL_CNTL        0x8c01
#define RB_2D_DST_INFO          0x8c17
#define RB_2D_DST               0x8c18
#define RB_2D_DST_PITCH         0x8c1a
#define RB_2D_SRC_SOLID_C0      0x8c2c
#define SP_2D_DST_FORMAT        0xacc0
#define FORMAT_8888             0x30
#define BLIT_SOLID_8888         (1u << 7 | FORMAT_8888 << 8 | 0xfu << 20 | 0x10u << 24) /* one colour; all channels; 8-bit */
#define DST_BGRA_LINEAR         (FORMAT_8888 | 1u << 10)        /* rows of pixels, blue first in memory */
#define EVENT_FLUSH_COLOR       0x1d
#define EVENT_CACHE_FLUSH       0x04
#define TARGET_WIDTH            1080
#define TARGET_HEIGHT           2400
#define TARGET_ROW              4352            /* 1088 pixels: the next multiple of 64 bytes */

extern void MiniIOMobileFramebufferShow(const uint8_t *pixels, uint32_t width, uint32_t height, uint32_t row_bytes);

static void
ring_fill(uint32_t top, uint32_t bottom, uint32_t red, uint32_t green, uint32_t blue)
{
	ring_register(GRAS_2D_DST_TL, top << 16);
	ring_register(GRAS_2D_DST_BR, (TARGET_WIDTH - 1) | (bottom - 1) << 16);
	ring_register(RB_2D_SRC_SOLID_C0, red);         /* a register for each part of the colour, 0 to 255 */
	ring_register(RB_2D_SRC_SOLID_C0 + 1, green);
	ring_register(RB_2D_SRC_SOLID_C0 + 2, blue);
	ring_register(RB_2D_SRC_SOLID_C0 + 3, 255);
	ring_register(RB_2D_BLIT_CNTL, BLIT_SOLID_8888);
	ring_register(GRAS_2D_BLIT_CNTL, BLIT_SOLID_8888);
	ring_register(SP_2D_DST_FORMAT, 1 | FORMAT_8888 << 3 | 0xfu << 12);
	ring_register(RB_2D_PIXEL_CNTL, 0);
	ring_register(RB_2D_DST_INFO, DST_BGRA_LINEAR);
	ring_register(RB_2D_DST, CORE_TARGET_ADDRESS);
	ring_register(RB_2D_DST + 1, 0);
	ring_register(RB_2D_DST_PITCH, TARGET_ROW >> 6);
	ring_command(CP_WAIT_FOR_IDLE, 0);
	ring_command(CP_BLIT, 1);
	ring(3);                                /* "scale": the 2D engine's one operation */
}

static bool
step_frame(void)
{
	const volatile uint8_t *pixel;

	if (!cx_ready() || !core_runs) {
		return false;
	}
	announce("make a picture's worth of memory for the 3D core (10 MB) and have it fill three bands of colour");
	if (!block_new(&core_space, &core_target, CORE_TARGET_ADDRESS, TARGET_ROW * TARGET_HEIGHT, PTE_CORE)) {
		return false;
	}
	/* tables that the SMMU has in use have grown: it has not seen these addresses, so nothing stale to forget */
	for (uint32_t i = 0; i < core_space.table_count; i++) {
		to_memory((vm_offset_t)core_space.tables[i].table, 4096);
	}
	ring_command(CP_SET_MARKER, 1);
	ring(12);                               /* "2D blits follow" */
	ring_register(RB_CCU_CNTL, 0x10000004); /* Mesa's for this GPU when it draws straight into memory */
	ring_fill(0, 800, 255, 128, 0);         /* orange */
	ring_fill(800, 1600, 255, 255, 255);
	ring_fill(1600, TARGET_HEIGHT, 0, 128, 255);
	/*
	 * What it has drawn out of its caches into memory. Each of the two events is followed by an
	 * address and a word that the core writes there when it is done (Linux's a6xx_submit; Mesa's
	 * tu6_emit_event_write: the events named ..._TS): the core takes those three words whether
	 * they are there or not.
	 */
	ring_command(CP_EVENT_WRITE, 4);
	ring(EVENT_FLUSH_COLOR);
	ring(CORE_SCRATCH_ADDRESS + 32);
	ring(0);
	ring(0x0c0c0c0c);
	ring_command(CP_EVENT_WRITE, 4);
	ring(EVENT_CACHE_FLUSH);
	ring(CORE_SCRATCH_ADDRESS + 16);
	ring(0);
	ring(0x0d0e0d0e);
	ring_command(CP_WAIT_FOR_IDLE, 0);
	if (!ring_run("fill a picture")) {
		return false;
	}
	to_memory(core_scratch.memory, 64);
	printf("adreno: %u: the words it writes when its caches are in memory: %#x, %#x (told: 0xc0c0c0c, 0xd0e0d0e)\n", step,
	    ((volatile uint32_t *)core_scratch.memory)[8], ((volatile uint32_t *)core_scratch.memory)[4]);
	to_memory(core_target.memory, core_target.size);
	pixel = (const volatile uint8_t *)core_target.memory;
	printf("adreno: %u: the picture's first pixel is %02x %02x %02x %02x (blue green red alpha; told: 00 80 ff ff), one in "
	    "the middle %02x %02x %02x %02x, one in the last row %02x %02x %02x %02x\n", step, pixel[0], pixel[1], pixel[2],
	    pixel[3], pixel[1200 * TARGET_ROW + 2160], pixel[1200 * TARGET_ROW + 2161], pixel[1200 * TARGET_ROW + 2162],
	    pixel[1200 * TARGET_ROW + 2163], pixel[2399 * TARGET_ROW + 4316], pixel[2399 * TARGET_ROW + 4317],
	    pixel[2399 * TARGET_ROW + 4318], pixel[2399 * TARGET_ROW + 4319]);
	if ((pixel[0] | pixel[1] | pixel[2]) == 0 || pixel[1200 * TARGET_ROW + 2160] == 0 || (pixel[2399 * TARGET_ROW + 4316] |
	    pixel[2399 * TARGET_ROW + 4317] | pixel[2399 * TARGET_ROW + 4318]) == 0) {
		printf("adreno: %u: the picture is not what the 3D core was told to draw\n", step);
		return false;
	}
	MiniIOMobileFramebufferShow((const uint8_t *)core_target.memory, TARGET_WIDTH, TARGET_HEIGHT, TARGET_ROW);
	printf("adreno: %u: A PICTURE DRAWN BY THE GPU IS ON THE PANEL: orange, white, blue from the top\n", step);
	return true;
}

/* ---- the way back ------------------------------------------------------------------------------------- */

/*
 * Nothing of the GPU block may outlive the kernel that started it: the memory its SMMU context
 * points into is the next kernel's to use. In the order of Linux's a6xx_gmu_shutdown: the 3D
 * core given back, the GMU told to drop its votes ("slumber") and stopped, its RPMh channel put
 * to sleep; then the processor into reset and its stream sent nowhere.
 */
/*
 * The 3D core's own power switch is the GMU's to operate. Where the GMU is gone and has left it
 * on (it is in reset; it did not answer when told to slumber), the switch is turned off from
 * here (vendor's a6xx_gmu_suspend): a core that keeps its power while the rest of the block
 * loses it is found by the next start as the last one left it, in the middle of things.
 */
static void
gx_off(void)
{
	uint32_t gx = get(&gpucc, "gx_gdscr", GPUCC_GX_GDSCR);

	if (gx & GDSC_POWER_ON) {
		put(&gpucc, "gx_gdscr", GPUCC_GX_GDSCR, gx | GDSC_SW_COLLAPSE);
		wait_for(&gpucc, GPUCC_GX_GDSCR, GDSC_POWER_ON, 0, 100);
		get(&gpucc, "gx_gdscr", GPUCC_GX_GDSCR);
	}
}

/* What step 4 does when it finds the GPU block running: see there. Nothing here has been set up by this kernel. */
static void
stale_stop(void)
{
	need(&gmu);
	need(&rscc);
	put(&gmu, "cm3_sysreset", GMU_CM3_SYSRESET, 1);
	gx_off();
	if (rd(rscc.base + RSCC_SEQ_MEM_0_DRV0) == 0xeaaae5a0 && !(rd(rscc.base + RSCC_RSC_STATUS0_DRV0) & RSCC_ASLEEP)) {
		put(&gmu, "rscc_control_req", GMU_RSCC_CONTROL_REQ, 1);
		wait_for(&rscc, RSCC_RSC_STATUS0_DRV0, RSCC_ASLEEP, RSCC_ASLEEP, 100);
		get(&rscc, "rsc_status0_drv0", RSCC_RSC_STATUS0_DRV0);
		put(&gmu, "rscc_control_req", GMU_RSCC_CONTROL_REQ, 0);
	}
	if (smmu_layout()) {
		for (uint32_t match = 0; match < smmu_matches && match < 6; match++) {
			put(&smmu, "smr", SMMU_SMR(match), 0);
			put(&smmu, "s2cr", SMMU_S2CR(match), SMMU_S2CR_FAULT);
		}
		for (uint32_t context = 0; context < smmu_contexts && context < 5; context++) {
			put(&smmu, "cb sctlr", (smmu_pages + context) * smmu_page + SMMU_CB_SCTLR, 0);
		}
	}
	put(&gpucc, "cx_gdscr", GPUCC_CX_GDSCR, rd(gpucc.base + GPUCC_CX_GDSCR) | GDSC_SW_COLLAPSE);
	for (int i = 0; i < 1000 && cx_is_on(); i++) {
		sleep_us(100);
	}
	get(&gpucc, "cx_gdscr", GPUCC_CX_GDSCR);
	get(&gpucc, "cx_hw_ctrl", GPUCC_CX_HW_CTRL);
}

/*
 * The GPU block's bus interface is told to let what is under way finish and to start nothing
 * more (Linux's a6xx_bus_clear_pending_transactions). (Without it, a core whose SMMU context is
 * taken away under a draw leaves the SMMU waiting for good, and the CX half cannot be switched
 * off any more: only a restart of the phone gets the GPU back.)
 * `for_good`: the 3D core may be in the middle of a frame and nobody will tell it to stop (a
 * panic; a core that does not come to rest): its own side is halted too, and all of it stays
 * halted, so that nothing of it reaches memory after this, whatever becomes of its tables.
 * Otherwise the halt is lifted again at once, as Linux does: the GMU reaches its memory through
 * the same interface, and one that cannot read its messages does not answer "slumber".
 */
static void
core_bus_halt(bool for_good)
{
	if (for_good) {
		wr(gpu.base + RBBM_GBIF_HALT, 1);
		for (int i = 0; i < 100000 && !(rd(gpu.base + RBBM_GBIF_HALT_ACK) & 1); i++) {
		}
	}
	wr(gbif.base + GBIF_HALT, 1);
	for (int i = 0; i < 100000 && !(rd(gbif.base + GBIF_HALT_ACK) & 1); i++) {
	}
	wr(gbif.base + GBIF_HALT, 2);
	for (int i = 0; i < 100000 && !(rd(gbif.base + GBIF_HALT_ACK) & 2); i++) {
	}
	if (!for_good) {
		wr(gbif.base + GBIF_HALT, 0);
	}
}

static bool core_stuck;                 /* the 3D core has not finished what it was given, and is not waited for */

static void
gpu_block_stop(void)
{
	uint32_t slumber[3] = { 0, 0, 0 };
	bool power_off = false;

	step = 0;
	newest_step = ~0u;
	if (core_runs && core_is_on) {
		/* (what the core looks like when it is given back: a GMU that then does not answer may have its reasons here) */
		printf("adreno: %u: the 3D core before it is stopped: status %#x, interrupts %#x, ring read up to %u of %u; the GMU "
		    "sees it busy: %#x\n", step, rd(gpu.base + RBBM_STATUS), rd(gpu.base + RBBM_INT_0_STATUS),
		    rd(gpu.base + CP_RB_RPTR), ring_at, rd(gmu.base + GMU_AO_GPU_CX_BUSY_STATUS));
		core_bus_halt(core_stuck);
		printf("adreno: %u: the GPU block's bus has been halted%s (rbbm_gbif_halt_ack %#x, gbif halt_ack %#x)\n", step,
		    core_stuck ? ", the 3D core's side too, and stays so" : " and let go again", rd(gpu.base + RBBM_GBIF_HALT_ACK),
		    rd(gbif.base + GBIF_HALT_ACK));
	}
	if (core_is_on) {
		put(&gmu, "host2gmu_intr_set", GMU_HOST2GMU_INTR_SET, 1u << 31);
		core_is_on = false;
	}
	if (gmu_talks) {
		hfi_tell("\"slumber\"", HFI_PREPARE_SLUMBER, slumber, 3);
		if (!wait_for(&gmu, GMU_AO_GPU_CX_BUSY_STATUS, 1u << 23, 0, 100)) {
			get(&gmu, "ao_gpu_cx_busy_status", GMU_AO_GPU_CX_BUSY_STATUS);
		}
		get(&gmu, "sptprac_pwr_clk_status", GMU_SPTPRAC_PWR_CLK_STATUS);
		get(&gpucc, "gx_gdscr", GPUCC_GX_GDSCR);
		gmu_talks = false;
	}
	if (gmu_runs) {
		put(&gmu, "cm3_sysreset", GMU_CM3_SYSRESET, 1);
		gmu_runs = false;
		gx_off();                               /* (a GMU that has slumbered has done it itself) */
	}
	if (zap_is_loaded) {
		/* the next kernel loads its own: see step_zap_load() */
		printf("adreno: %u: the zap shader: %s\n", step, zap_unload() ? "TrustZone has given it up" :
		    "TRUSTZONE DOES NOT GIVE IT UP; the next kernel will find it there");
		zap_is_loaded = false;
	}
	/* vendor's a6xx_rscc_sleep_sequence: the GMU's votes in RPMh are dropped, by the always-on sequencer */
	if (rsc_awake && rd(rscc.base + RSCC_SEQ_MEM_0_DRV0) == 0xeaaae5a0) {
		put(&gmu, "rscc_control_req", GMU_RSCC_CONTROL_REQ, 1);
		if (!wait_for(&rscc, RSCC_RSC_STATUS0_DRV0, RSCC_ASLEEP, RSCC_ASLEEP, 100)) {
			printf("adreno: %u: the GMU's RPMh channel does not go to sleep\n", step);
		}
		get(&rscc, "rsc_status0_drv0", RSCC_RSC_STATUS0_DRV0);
		put(&gmu, "rscc_control_req", GMU_RSCC_CONTROL_REQ, 0);
	}
	rsc_awake = false;
	if (gmu_space_is_on) {
		space_faulted(&gmu_space);
		space_off(&gmu_space, GMU_MATCH);
		gmu_space_is_on = false;
		power_off = true;
	}
	if (second_space_is_on) {
		space_faulted(&second_space);
		space_off(&second_space, SECOND_MATCH);
		second_space_is_on = false;
	}
	if (second_stream_is_on) {
		put(&smmu, "smr", SMMU_SMR(SECOND_MATCH), 0);
		put(&smmu, "s2cr", SMMU_S2CR(SECOND_MATCH), SMMU_S2CR_FAULT);
		second_stream_is_on = false;
	}
	if (core_space_is_on) {
		space_faulted(&core_space);
		space_off(&core_space, core_match);
		core_space_is_on = core_runs = false;
	}
	/*
	 * And the CX half off again, as it was when the phone started: the GMU with all it remembers
	 * and the SMMU lose their power, and the next time starts from nothing (vendor's
	 * a6xx_gmu_suspend). The switch takes votes; ours is all this withdraws.
	 */
	if (power_off && gpucc_answers()) {
		put(&gpucc, "cx_gdscr", GPUCC_CX_GDSCR, rd(gpucc.base + GPUCC_CX_GDSCR) | GDSC_SW_COLLAPSE);
		for (int i = 0; i < 1000 && cx_is_on(); i++) {
			sleep_us(100);
		}
		get(&gpucc, "cx_gdscr", GPUCC_CX_GDSCR);
		get(&gpucc, "cx_hw_ctrl", GPUCC_CX_HW_CTRL);
		cx_off = !cx_is_on();
	}
}

/*
 * The same when there is no time for asking: the kernel is on its way back to the loader (the
 * end of a run, a panic). Only the two writes that keep the GMU out of memory that is no longer
 * its to use.
 */
void
hakios_adreno_quiesce(void)
{
	if (cx_off) {
		return;
	}
	if (gmu_runs) {
		wr(gmu.base + GMU_CM3_SYSRESET, 1);
		gmu_runs = false;
	}
	if (core_runs) {
		core_bus_halt(true);
		core_runs = false;
	}
	if (gmu_space_is_on) {
		wr(smmu.base + smmu_context(&gmu_space) + SMMU_CB_SCTLR, 0);
		gmu_space_is_on = false;
	}
	if (core_space_is_on) {
		wr(smmu.base + smmu_context(&core_space) + SMMU_CB_SCTLR, 0);
		core_space_is_on = false;
	}
	if (second_space_is_on) {
		wr(smmu.base + smmu_context(&second_space) + SMMU_CB_SCTLR, 0);
		second_space_is_on = false;
	}
}

/* ---- the GPU kept on, for the system to draw with --------------------------------------------------- */

/*
 * With boot-arg adreno_keep=1 the steps are not undone: the GPU block stays as step 21 leaves it,
 * the 3D core powered and taking commands, and the service HakiosGPU (drivers/gpu/hakios.gpu.cpp)
 * hands it to programs through the interface of Linux's msm driver, which is what Mesa's
 * freedreno speaks. That file keeps the buffers and the clients; what is the GPU's is here:
 * addresses in the 3D core's SMMU context, the ring, and the word by which the core says how
 * far it has got (a "fence": a submission's number, written by the core when it is through
 * with it). The caller holds one lock around all of these.
 */
/* This bring-up loads a640 firmware. Its userspace hardware contract must
 * match too: PCLM10 android-merged.dts qcom,chipid = 0x06040001, bank bit 15;
 * vendor adreno-gpulist and Linux v6.7 specify 1 MiB GMEM for A640.
 * Mesa 25.0.7 selects A640/gen2 with macrotile 0, swizzle 6, not A635/gen4.
 * The previous 0x06030500 selected "Adreno 7c+ Gen 3", not an A640 alias. */
#define GPU_CHIP_ID             0x06040001ull
#define GPU_GMEM_BYTES          (1024u * 1024u)
#define GPU_HIGHEST_BANK_BIT    15u
#define GPU_UBWC_SWIZZLE        6u
#define GPU_MACROTILE_MODE      0u
#define GPU_ADDRESSES_START     0x20000000ull   /* above everything the steps use; below 4 GB, as Linux keeps them */
#define GPU_ADDRESSES_SIZE      0xe0000000ull
#define GPU_FENCE_AT            0x40            /* in the scratch page */
#define GPU_TIMES_AT            0x400           /* ... and when the core began and ended each of its last submissions */
#define GPU_TIMES               64
#define CP_ALWAYS_ON_COUNTER    0x0980          /* ticks of the 19.2 MHz crystal, 64 bits: for the core to read itself */
#define CP_REG_TO_MEM           0x3e
#define CP_SCRATCH_REG2         0x0885
#define CP_IB1_BASE             GPU_WORD(0x0928)
#define CP_IB1_REM_SIZE         GPU_WORD(0x092a)
#define CP_IB2_BASE             GPU_WORD(0x092b)
#define CP_IB2_REM_SIZE         GPU_WORD(0x092d)
#define CP_INDIRECT_BUFFER      0x3f
#define EVENT_INVALIDATE_DEPTH  0x18
#define EVENT_INVALIDATE_COLOR  0x19

static bool gpu_kept;
static uint32_t gpu_fence;              /* the number of the last submission */
static uint32_t gpu_timed;              /* ... of the last whose times have been added up */
static uint64_t gpu_ticks;              /* how long the core has worked on the submissions added up */
static uint32_t gpu_timed_count;
static uint32_t gpu_timed_by[7];        /* ... how many of them under 0.1, 0.25, 0.5, 1, 2, 4 ms, and longer */
static uint32_t gpu_exact = 1;          /* boot-arg gpu_exact=0: the core does not wait to be idle before it notes the time */
extern void hakios_tl(const char *format, ...) __printflike(1, 2);
static struct {
	uint64_t start, size;
} gpu_free[1024];                       /* the addresses not in use, in order */
static unsigned gpu_free_count;

extern void HakiosGPUStart(void);
extern void HakiosGPUStop(void);       /* takes the clients' lock, then calls hakios_gpu_stop() */
static void gpu_block_stop(void);

/*
 * A run that ends in order -- its time is up, the host asks for the end, five fingers on the
 * screen, a halt: whoever is about to call loader_return() for one of these calls
 * hakios_adreno_stop() first, from a thread that may wait -- gives the GPU back as the steps'
 * own ending does, and the next kernel finds the GPU block as the phone's start leaves it. A
 * panic cannot: it leaves the GPU block running but quiet (hakios_adreno_quiesce()), and the
 * next kernel's step 4 takes it down (stale_stop()). Boot-arg adreno_dirty=1 makes every ending
 * a panic's in this respect, to see that work.
 */
void
hakios_gpu_stop(void)
{
	if (gpu_kept) {
		uint32_t waited = 0;

		gpu_kept = false;                       /* (nothing more is taken) */
		quiet = false;
		/*
		 * What the core has been given, it finishes first: told to slumber in the middle of a
		 * frame, the GMU may not answer. Half a second at the most; then its bus is halted
		 * for good, and its power switched off from here.
		 */
		while (!hakios_gpu_done(gpu_fence) && waited < 5000) {
			sleep_us(100);
			waited++;
		}
		core_stuck = !hakios_gpu_done(gpu_fence);
		printf("adreno: the run is about to end: the GPU is given back (%s after %u us)\n",
		    core_stuck ? "STILL AT WORK" : "it had finished what it was given", 100 * waited);
		gpu_block_stop();
	}
}

void
hakios_adreno_stop(void)
{
	uint32_t dirty = 0;

	if (gpu_kept && !(PE_parse_boot_argn("adreno_dirty", &dirty, sizeof(dirty)) && dirty)) {
		HakiosGPUStop();
	}
}

bool
hakios_gpu_ready(void)
{
	return gpu_kept;
}

/* The answers to msm's GET_PARAM (Linux's adreno_get_param for this GPU; the numbers are msm_drm.h's). */
bool
hakios_gpu_parameter(uint32_t which, uint64_t *value)
{
	switch (which) {
	case 0x01:                              /* GPU_ID: none, the chip ID says it */
	case 0x06:                              /* GMEM_BASE */
	case 0x08:                              /* PP_PGTABLE */
	case 0x09:                              /* FAULTS */
	case 0x0a:                              /* SUSPENDS */
	case 0x11:                              /* RAYTRACING */
		*value = 0;
		return true;
	case 0x02:                              /* GMEM_SIZE */
		*value = GPU_GMEM_BYTES;
		return true;
	case 0x03:                              /* CHIP_ID, with the speed bin above it */
		*value = GPU_CHIP_ID;            /* reference-board fuse layout is not an A640 speed-bin contract */
		return true;
	case 0x04:                              /* MAX_FREQ */
		*value = core_khz * 1000ull;
		return true;
	case 0x05:                              /* TIMESTAMP */
		*value = mach_absolute_time();
		return true;
	case 0x07:                              /* PRIORITIES */
		*value = 1;
		return true;
	case 0x10:                              /* HIGHEST_BANK_BIT */
		*value = GPU_HIGHEST_BANK_BIT;
		return true;
	case 0x12:                              /* UBWC_SWIZZLE */
		*value = GPU_UBWC_SWIZZLE;
		return true;
	case 0x13:                              /* MACROTILE_MODE */
		*value = GPU_MACROTILE_MODE;
		return true;
	default:                                /* among them VA_START, VA_SIZE: addresses are ours to give */
		return false;
	}
}

/* Addresses for a buffer of so many bytes; 0: none left. */
uint64_t
hakios_gpu_address(uint64_t size)
{
	size = (size + 0xffff) & ~0xffffull;
	for (unsigned i = 0; i < gpu_free_count; i++) {
		if (gpu_free[i].size >= size) {
			uint64_t address = gpu_free[i].start;

			gpu_free[i].start += size;
			gpu_free[i].size -= size;
			if (gpu_free[i].size == 0) {
				memmove(&gpu_free[i], &gpu_free[i + 1], (--gpu_free_count - i) * sizeof(gpu_free[0]));
			}
			return address;
		}
	}
	return 0;
}

void
hakios_gpu_address_free(uint64_t address, uint64_t size)
{
	unsigned at;

	size = (size + 0xffff) & ~0xffffull;
	for (at = 0; at < gpu_free_count && gpu_free[at].start < address; at++) {
	}
	/* a range that overlaps one that is free already was given back twice: leave the list as it is, instead of merging it into nonsense */
	if ((at > 0 && gpu_free[at - 1].start + gpu_free[at - 1].size > address) ||
	    (at < gpu_free_count && address + size > gpu_free[at].start)) {
		printf("adreno: the addresses %#llx (%#llx bytes) are free already: not given back again\n", address, size);
		return;
	}
	if (at > 0 && gpu_free[at - 1].start + gpu_free[at - 1].size == address) {
		gpu_free[at - 1].size += size;
		if (at < gpu_free_count && address + size == gpu_free[at].start) {
			gpu_free[at - 1].size += gpu_free[at].size;
			memmove(&gpu_free[at], &gpu_free[at + 1], (--gpu_free_count - at) * sizeof(gpu_free[0]));
		}
	} else if (at < gpu_free_count && address + size == gpu_free[at].start) {
		gpu_free[at].start = address;
		gpu_free[at].size += size;
	} else if (gpu_free_count < sizeof(gpu_free) / sizeof(gpu_free[0])) {
		memmove(&gpu_free[at + 1], &gpu_free[at], (gpu_free_count++ - at) * sizeof(gpu_free[0]));
		gpu_free[at].start = address;
		gpu_free[at].size = size;
	}                                       /* (or else these addresses are lost) */
}

/* One 4K page of ours at an address of the 3D core's; after the last of a buffer's, hakios_gpu_mapped(). */
bool
hakios_gpu_map(uint64_t address, uint64_t phys, bool read_only)
{
	return space_map(&core_space, address, phys, PTE_CORE | (read_only ? PTE_READ_ONLY : 0));
}

void
hakios_gpu_mapped(void)
{
	space_commit(&core_space);
}

void
hakios_gpu_unmap(uint64_t address, uint64_t size)
{
	uint32_t context = smmu_context(&core_space);

	for (uint64_t offset = 0; offset < size; offset += 4096) {
		space_map(&core_space, address + offset, 0, 0);
	}
	space_commit(&core_space);
	wr(smmu.base + context + SMMU_CB_TLBIASID, core_space.context);
	wr(smmu.base + context + SMMU_CB_TLBSYNC, 1);
	for (int i = 0; i < 1000 && (rd(smmu.base + context + SMMU_CB_TLBSTATUS) & 1); i++) {
	}
}

/* Memory that the 3D core and a program see without this processor's cache in between: out of that cache. */
void
hakios_gpu_to_memory(void *start, uint64_t size)
{
	to_memory((vm_offset_t)start, size);
}

/* How far the 3D core has got. */
bool
hakios_gpu_done(uint32_t fence)
{
	volatile uint32_t *word = (volatile uint32_t *)(core_scratch.memory + GPU_FENCE_AT);

	to_memory((vm_offset_t)word, 4);
	return (int32_t)(*word - fence) >= 0;
}

uint32_t
hakios_gpu_last(void)
{
	return gpu_fence;
}

/* The core is to write down what time it is: at one end of submission `fence`. */
static void
ring_time(uint32_t fence, uint32_t end)
{
	ring_command(CP_REG_TO_MEM, 3);
	ring(CP_ALWAYS_ON_COUNTER | 2u << 18 | 1u << 30);       /* two words, a 64-bit register */
	ring(CORE_SCRATCH_ADDRESS + GPU_TIMES_AT + 16 * (fence % GPU_TIMES) + 8 * end);
	ring(0);
}

/* Add up the times of the submissions the core has finished since this was last done. */
static void
gpu_times(void)
{
	volatile uint64_t *times = (volatile uint64_t *)(core_scratch.memory + GPU_TIMES_AT);
	uint32_t done;

	to_memory(core_scratch.memory, 4096);
	done = *(volatile uint32_t *)(core_scratch.memory + GPU_FENCE_AT);
	if (done - gpu_timed > GPU_TIMES) {
		gpu_timed = done - GPU_TIMES;           /* (the older ones have been written over) */
	}
	while (gpu_timed != done) {
		uint32_t slot = ++gpu_timed % GPU_TIMES;

		if (gpu_timed != 0 && times[2 * slot + 1] > times[2 * slot]) {
			static const uint32_t under[6] = { 1920, 4800, 9600, 19200, 38400, 76800 };     /* (19.2 ticks a microsecond) */
			uint64_t ticks = times[2 * slot + 1] - times[2 * slot];
			unsigned by = 0;

			if (gpu_log) {
				/* "tl: q FENCE took US": what the core spent on that submission (drivers/gpu/hakios.gpu.cpp: tl: Q) */
				hakios_tl("tl: q %u took %llu\n", gpu_timed, ticks * 10 / 192);
			}

			while (by < 6 && ticks >= under[by]) {
				by++;
			}
			gpu_timed_by[by]++;
			gpu_ticks += ticks;
			gpu_timed_count++;
		}
	}
}

/*
 * How long the 3D core has worked since this was last asked, on how many submissions, and how
 * many of those took it under 0.1, 0.25, 0.5, 1, 2, 4 milliseconds and longer.
 */
void
hakios_gpu_worked(uint64_t *microseconds, uint32_t *submissions, uint32_t by[7])
{
	gpu_times();
	*microseconds = gpu_ticks * 10 / 192;
	*submissions = gpu_timed_count;
	memcpy(by, gpu_timed_by, sizeof(gpu_timed_by));
	bzero(gpu_timed_by, sizeof(gpu_timed_by));
	gpu_ticks = 0;
	gpu_timed_count = 0;
}

/*
 * Command buffers for the 3D core to run, in order: each an address and a count of words. As
 * Linux's a6xx_submit puts them in the ring. Returns the submission's number; 0: the ring is
 * full and the core is not reading it.
 */
uint32_t
hakios_gpu_submit(const uint64_t *addresses, const uint32_t *words, uint32_t count)
{
	uint32_t wanted = 26 + 4 * count, room = 0;

	for (int i = 0; i < 20000; i++) {
		room = (rd(gpu.base + CP_RB_RPTR) + RING_WORDS - ring_at - 1) % RING_WORDS;
		if (room >= wanted) {
			break;
		}
		sleep_us(100);
	}
	if (room < wanted) {
		return 0;
	}
	gpu_times();
	if (++gpu_fence == 0) {
		gpu_fence = 1;
	}
	ring_time(gpu_fence, 0);
	ring_command(CP_EVENT_WRITE, 1);
	ring(EVENT_INVALIDATE_DEPTH);
	ring_command(CP_EVENT_WRITE, 1);
	ring(EVENT_INVALIDATE_COLOR);
	for (uint32_t i = 0; i < count; i++) {
		ring_command(CP_INDIRECT_BUFFER, 3);
		ring((uint32_t)addresses[i]);
		ring((uint32_t)(addresses[i] >> 32));
		ring(words[i]);
	}
	if (gpu_exact) {
		ring_command(CP_WAIT_FOR_IDLE, 0);      /* the time is the pipeline's, not the command processor's, which is ahead of it */
	}
	ring_time(gpu_fence, 1);
	ring_register(CP_SCRATCH_REG2, gpu_fence);
	ring_command(CP_EVENT_WRITE, 4);
	ring(EVENT_CACHE_FLUSH);
	ring(CORE_SCRATCH_ADDRESS + GPU_FENCE_AT);
	ring(0);
	ring(gpu_fence);
	ring_send();
	return gpu_fence;
}

/* A submission does not come back: what the 3D core and its SMMU context say. */
void
hakios_gpu_report(const char *why)
{
	uint32_t context = smmu_context(&core_space);

	printf("adreno: gpu: %s: submission %u is the last, the core has done %u; ring: read up to %u, written up to %u; status "
	    "%#x, interrupts %#x; command processor: fault %#x, interrupts %#x, protection %#x\n", why, gpu_fence,
	    *(volatile uint32_t *)(core_scratch.memory + GPU_FENCE_AT), rd(gpu.base + CP_RB_RPTR), ring_at,
	    rd(gpu.base + RBBM_STATUS), rd(gpu.base + RBBM_INT_0_STATUS), rd(gpu.base + CP_HW_FAULT),
	    rd(gpu.base + CP_INTERRUPT_STATUS), rd(gpu.base + CP_PROTECT_STATUS));
	printf("adreno: gpu:   in the first command buffer at %#llx, %u words to go; in the second at %#llx, %u to go\n",
	    (uint64_t)rd(gpu.base + CP_IB1_BASE + 4) << 32 | rd(gpu.base + CP_IB1_BASE), rd(gpu.base + CP_IB1_REM_SIZE),
	    (uint64_t)rd(gpu.base + CP_IB2_BASE + 4) << 32 | rd(gpu.base + CP_IB2_BASE), rd(gpu.base + CP_IB2_REM_SIZE));
	step = 0;
	if (space_faulted(&core_space)) {
		wr(smmu.base + context + SMMU_CB_FSR, SMMU_FSR_ALL);    /* so that the next one shows */
	}
}

/* ---- all of it together ---------------------------------------------------------------------------- */

static bool(*const steps[])(void) = {
	step_outside, step_fuses, step_gpucc, step_cx_power, step_clocks, step_first_words, step_rscc, step_smmu,
	step_smmu_context, step_gmu_memory, step_trustzone, step_zap_signature, step_gmu_space, step_gmu_firmware,
	step_gmu_start, step_gmu_queues, step_core_power, step_core_space, step_zap_load, step_core_start, step_core_commands,
	step_frame,
};
#define STEPS                   ((uint32_t)(sizeof(steps) / sizeof(steps[0])))

/* ---- beside the steps: how warm the GPU is ------------------------------------------------------------------ */

/*
 * The SoC's temperature sensors report by themselves, in a block that is always on (dt: tsens1,
 * whose readings are at 0xc265000; Linux's tsens-v2.c): a status word a sensor, the last reading
 * in its low 12 bits, signed, in tenths of a degree, and bit 21 when there is one. Sensors 1 and
 * 2 sit at the GPU (dt: gpuss-0, gpuss-1). Read, never written: once a minute while the GPU is
 * kept, for the line beside the kernel's line about memory (osfmk/arm64/loader_return.c). A
 * sensor caught between two readings has none for a moment and is asked again; one that still
 * has none is put down as -273.0.
 */
static struct block tsens = { "tsens1", 0x0c265000, 0x200 };
#define TSENS_STATUS(sensor)    (0xa0 + 4 * (sensor))
#define TSENS_VALID             (1u << 21)

void
hakios_adreno_minute(void)
{
	int32_t tenths[2];

	if (!gpu_kept) {
		return;
	}
	need(&tsens);
	for (unsigned i = 0; i < 2; i++) {
		tenths[i] = -2730;
		for (unsigned again = 0; again < 16 && tenths[i] == -2730; again++) {
			uint32_t status = rd(tsens.base + TSENS_STATUS(1 + i));

			if (status & TSENS_VALID) {
				tenths[i] = (int32_t)(status << 20) >> 20;
			}
		}
	}
	printf("adreno: the GPU's sensors read %d.%d and %d.%d degrees; its clock is set to %u MHz\n", tenths[0] / 10,
	    tenths[0] % 10 < 0 ? -(tenths[0] % 10) : tenths[0] % 10, tenths[1] / 10,
	    tenths[1] % 10 < 0 ? -(tenths[1] % 10) : tenths[1] % 10, core_khz / 1000);
}

/* ---- beside the steps: what the firmware left the display doing (boot-arg display_probe=1) ---------------- */

/*
 * The panel is a DSI video panel (dt: qcom,mdss_dsi_m20_36_02_0a_dsc_vid: 1080 x 2400 at 120
 * frames a second, 2432 lines with the porches; or at 60 with a longer vertical front porch):
 * the display controller sends it the framebuffer over and over at the rate of its timing
 * registers, which nobody has touched since the firmware. This reads them. Each read is behind
 * a check that what it reads has its clock: the bus clock of the display's clock controller (by
 * GCC, which answers always; the firmware leaves it off, Linux keeps it on always, and so it is
 * turned on here: one write), in that controller the display block's power switch and the bus
 * clock of its registers (the firmware set the panel going and took that clock away again; it
 * is turned on here: the other write, and the last), then the timing of the interface the panel
 * hangs on (Linux's dpu_hw_intf.c has the registers' meaning) and its two counters, read twice a
 * known time apart: how many frames a second the panel is really sent. Neither clock is turned
 * off again. Every access is announced and waited for (adreno_pause), as in the newest step.
 *
 * Seen: 476 clocks a line (360 of them picture: the panel takes it compressed three to one),
 * 2432 lines a frame of which 2400 are shown, and 119.93 frames a second: the panel runs in
 * its 120 Hz mode, whatever is put into the framebuffer 60 times a second. The two counters
 * count although the register that enables them reads 0; the line counter goes from 0 to 2431,
 * 429 lines in 1.47 ms. (The first clock is found off again by the next kernel.)
 */
static struct block dispcc = { "dispcc", 0x0af00000, 0x2000 };          /* dt: clock-controller@af00000 */
static struct block disp_intf = { "intf1", 0x0ae36000, 0x300 };         /* dt: mdss_dsi0's mdp_intf_base */
#define GCC_DISP_AHB_CBCR       0x27004
#define DISPCC_MDSS_CORE_GDSCR  0x1004          /* dt: disp_cc_mdss_core_gdsc */
#define DISPCC_MDSS_PCLK0_CBCR  0x1010
#define DISPCC_MDSS_MDP_CBCR    0x1014
#define DISPCC_MDSS_AHB_CBCR    0x1050
#define INTF_TIMING_ENGINE_EN   0x000
#define INTF_HSYNC_CTL          0x008           /* clocks a line << 16 | of which the pulse */
#define INTF_VSYNC_PERIOD       0x00c           /* clocks a frame */
#define INTF_DISPLAY_V_START    0x01c           /* clocks before the first line shown, and */
#define INTF_DISPLAY_V_END      0x024           /* ... up to the last */
#define INTF_DISPLAY_HCTL       0x03c
#define INTF_FRAME_LINE_COUNT_EN 0x0a8
#define INTF_FRAME_COUNT        0x0ac
#define INTF_LINE_COUNT         0x0b0

static void
display_probe(void)
{
	uint32_t saved_fine = fine, line, frame, shown, counting, frames[2], lines[2], bus;
	uint64_t times[2];

	step = newest_step = 0;
	fine = 1;
	announce("the display: has its clock controller its bus clock (gcc 0x127004; GCC answers always)");
	if (get(&gcc, "disp_ahb_cbcr", GCC_DISP_AHB_CBCR) & CBCR_OFF) {
		announce("turn the bus clock of the display's clock controller on (gcc 0x127004 bit 0: THE ONE WRITE)");
		put(&gcc, "disp_ahb_cbcr", GCC_DISP_AHB_CBCR, rd(gcc.base + GCC_DISP_AHB_CBCR) | CBCR_ENABLE);
		sleep_ms(10);
	}
	/*
	 * Turned on, the register still says the clock is off (seen: 0x8000000b). Its bit 1 is
	 * set as the firmware left it: the clock is gated by the hardware itself while nobody
	 * talks to the block, and that is what "off" then means. Linux turns the clock on the same
	 * way, never looks at this bit and reads the clock controller next; so does this, if the
	 * clock is at least asked for.
	 */
	if (!(get(&gcc, "disp_ahb_cbcr", GCC_DISP_AHB_CBCR) & CBCR_ENABLE)) {
		printf("adreno: 0: the display's clock controller has no bus clock, and does not get one: nothing more is read\n");
		goto out;
	}
	announce("the display block's power switch and clocks, in its clock controller (0xaf00000: NOT READ BEFORE)");
	get(&dispcc, "mdss_mdp_cbcr", DISPCC_MDSS_MDP_CBCR);
	get(&dispcc, "mdss_pclk0_cbcr", DISPCC_MDSS_PCLK0_CBCR);
	if (!(get(&dispcc, "mdss_core_gdscr", DISPCC_MDSS_CORE_GDSCR) & GDSC_POWER_ON)) {
		printf("adreno: 0: the display block is without power: nothing more is read\n");
		goto out;
	}
	bus = get(&dispcc, "mdss_ahb_cbcr", DISPCC_MDSS_AHB_CBCR);
	if (bus & CBCR_OFF) {
		/* (seen: the power switch on, the display's own clock and the pixel clock running, this one off: 0x80000000) */
		announce("turn the bus clock of the display block's registers on (dispcc 0xaf01050 bit 0: THE ONE WRITE THERE)");
		put(&dispcc, "mdss_ahb_cbcr", DISPCC_MDSS_AHB_CBCR, bus | CBCR_ENABLE);
		sleep_ms(10);
		bus = get(&dispcc, "mdss_ahb_cbcr", DISPCC_MDSS_AHB_CBCR);
	}
	if (bus & CBCR_OFF) {
		/* (Linux waits for this bit to go, and gives up if it does not: there is no clock to read the block with) */
		printf("adreno: 0: the bus clock of the display block's registers does not run (%#x): nothing more is read\n", bus);
		goto out;
	}
	announce("the timing of the interface the panel hangs on (0xae36000: NOT READ BEFORE)");
	get(&disp_intf, "timing_engine_en", INTF_TIMING_ENGINE_EN);
	line = get(&disp_intf, "hsync_ctl", INTF_HSYNC_CTL) >> 16;
	frame = get(&disp_intf, "vsync_period", INTF_VSYNC_PERIOD);
	shown = get(&disp_intf, "display_v_end", INTF_DISPLAY_V_END) - get(&disp_intf, "display_v_start", INTF_DISPLAY_V_START);
	get(&disp_intf, "display_hctl", INTF_DISPLAY_HCTL);
	counting = get(&disp_intf, "frame_line_count_en", INTF_FRAME_LINE_COUNT_EN);
	frames[0] = get(&disp_intf, "frame_count", INTF_FRAME_COUNT);
	lines[0] = get(&disp_intf, "line_count", INTF_LINE_COUNT);
	/* the two counters together, twice, a second apart by our own clock */
	quiet = true;
	for (int i = 0; i < 2; i++) {
		frames[i] = rd(disp_intf.base + INTF_FRAME_COUNT);
		lines[i] = rd(disp_intf.base + INTF_LINE_COUNT);
		absolutetime_to_nanoseconds(mach_absolute_time(), &times[i]);
		if (rd(disp_intf.base + INTF_FRAME_COUNT) != frames[i]) {
			i--;                            /* (a frame began between the two reads: once more) */
			continue;
		}
		if (i == 0) {
			sleep_ms(1000);
		}
	}
	quiet = false;
	if (line != 0) {
		printf("adreno: 0: THE DISPLAY: %u lines a frame, of which %u shown, %u clocks a line (the device tree's timing "
		    "for 120 frames a second has 2432 lines; for 60, about twice that)\n", frame / line, (shown + 1) / line, line);
	}
	printf("adreno: 0: the interface's counters (frame_line_count_en %#x): frame %u line %u, then frame %u line %u %llu us "
	    "later\n", counting, frames[0], lines[0], frames[1], lines[1], (times[1] - times[0]) / 1000);
	if (line != 0 && frame >= line && frames[1] != frames[0] && times[1] > times[0]) {
		/* in thousandths of a frame a second: the frames gone by, and the part of one that the lines say */
		uint64_t per_frame = frame / line;
		uint64_t gone = (uint64_t)(frames[1] - frames[0]) * per_frame + lines[1] - lines[0];
		uint64_t rate = gone * 1000000000ull / per_frame * 1000 / (times[1] - times[0]);

		printf("adreno: 0: THE PANEL IS SENT %llu.%03llu FRAMES A SECOND (%llu lines in that time)\n", rate / 1000, rate % 1000,
		    gone);
	}
	/* where the scan is, read eight times a millisecond and a bit apart: how fast it goes down the panel */
	for (int i = 0; i < 8; i++) {
		uint64_t nanos;

		absolutetime_to_nanoseconds(mach_absolute_time(), &nanos);
		printf("adreno: 0: the scan: at %llu us line %u of frame %u\n", nanos / 1000 % 1000000, rd(disp_intf.base + INTF_LINE_COUNT),
		    rd(disp_intf.base + INTF_FRAME_COUNT));
		sleep_us(1300);
	}
out:
	fine = saved_fine;
	newest_step = 0;
}

/*
 * Boot-arg gpu=1: is this the phone, and is its GPU's firmware in the small image? Then the
 * GPU is started and kept for the system to draw with, which is what
 * "adreno=21 adreno_skip=0x1c00 adreno_pause=0 adreno_keep=1" asks for at length: the steps
 * that only look (10 to 12) left out, no pauses, no watch. Anywhere else -- QEMU, whose machine
 * has its peripherals elsewhere -- not one register of the above is touched.
 */
#define HAKIOS_SOC_BASE        0x00400000ul    /* where the loader says the phone's peripherals begin (its "redwood") */

extern vm_offset_t pe_arm_get_soc_base_phys(void);

static bool
start_by_itself(void)
{
	static const char *const files[] = { "a640_gmu.bin", "a630_sqe.fw", "a640_zap.mdt", "a640_zap.b02" };
	const char *missing = NULL;
	uint64_t size;

	if (pe_arm_get_soc_base_phys() != HAKIOS_SOC_BASE) {
		printf("adreno: THE GPU IS NOT STARTED: gpu=1 is for the phone, and this is not it\n");
		return false;
	}
	image_asked_quietly = true;
	for (unsigned i = 0; i < sizeof(files) / sizeof(files[0]) && missing == NULL; i++) {
		if (image_file("fw", files[i], &size) == NULL) {
			missing = files[i];
		}
	}
	image_asked_quietly = false;
	if (missing != NULL) {
		printf("adreno: THE GPU IS NOT STARTED: the small image has no fw/%s (the phone's /vendor/firmware/%s goes into "
		    "fw-gpu/); the system draws with its software renderer\n", missing, missing);
		return false;
	}
	return true;
}

/*
 * Called while the system starts, before there is a root file system, and the start waits: for
 * as long as the steps and the watch after them last, nothing else writes to the log, so that
 * its end -- all that the loader's notes keep -- is about what is being done here.
 */
void
hakios_adreno_init(void)
{
	static const char *const what[] = {
		"", "the clocks outside", "the fuses", "the clock controller", "the CX half's power", "the clocks", "the first words",
		"the GMU's RPMh sequencer", "the SMMU", "an SMMU context", "the GMU's memory", "TrustZone", "the zap shader's header",
		"the GMU's address space", "the GMU's firmware", "the GMU's start", "the GMU's message queues", "the 3D core's power",
		"the 3D core's address space", "the zap shader", "the 3D core's start", "the 3D core's first commands", "a frame"
	};
	uint32_t only = 0, done = 0, failed = 0, keep = 0;
	bool by_itself = false;

	if (PE_parse_boot_argn("display_probe", &only, sizeof(only)) && only) {
		display_probe();
	}
	if (!PE_parse_boot_argn("adreno", &last_step, sizeof(last_step))) {
		if (!(PE_parse_boot_argn("gpu", &only, sizeof(only)) && only) || !start_by_itself()) {
			return;
		}
		by_itself = true;
		last_step = 21;
		skip_steps = 1u << 10 | 1u << 11 | 1u << 12;
		pause_ms = 0;
		watch_seconds = 0;
		keep = 1;
	}
	PE_parse_boot_argn("adreno_skip", &skip_steps, sizeof(skip_steps));
	{
		uint32_t probe = 0;

		if (PE_parse_boot_argn("gpu_probe", &probe, sizeof(probe)) && probe) {
			gpu_probe = true;
		}
	}
	if (PE_parse_boot_argn("adreno_only", &only, sizeof(only)) && only > 0 && only <= STEPS) {
		last_step = only;
		skip_steps = ~(1u << only);
	}
	PE_parse_boot_argn("adreno_pause", &pause_ms, sizeof(pause_ms));
	PE_parse_boot_argn("adreno_fine", &fine, sizeof(fine));
	if (last_step == 0) {
		return;                                 /* (adreno=0: the GPU is left alone) */
	}
	if (last_step > STEPS) {
		last_step = STEPS;
	}
	PE_parse_boot_argn("adreno_watch", &watch_seconds, sizeof(watch_seconds));
	for (newest_step = last_step; newest_step > 0 && (skip_steps & 1u << newest_step); newest_step--) {
	}

	printf("adreno: steps up to %u, without those in mask %#x; step %u is the new one and waits %u ms %s\n", last_step,
	    skip_steps, newest_step, pause_ms, fine ? "before every access" : "after each announcement");
	for (step = 1; step <= last_step; step++) {
		if (skip_steps & 1u << step) {
			continue;
		}
		if (!steps[step - 1]()) {
			printf("adreno: step %u: FAILED; the steps after it are not attempted\n", step);
			failed = step;
			break;
		}
		printf("adreno: step %u: done\n", step);
		done = step;
	}
	PE_parse_boot_argn("adreno_keep", &keep, sizeof(keep));
	if (keep) {
		if (done >= 21 && core_runs) {
			gpu_free[0].start = GPU_ADDRESSES_START;
			gpu_free[0].size = GPU_ADDRESSES_SIZE;
			gpu_free_count = 1;
			bzero((void *)core_scratch.memory, 4096);
			to_memory(core_scratch.memory, 4096);
			PE_parse_boot_argn("gpu_exact", &gpu_exact, sizeof(gpu_exact));
			PE_parse_boot_argn("gpu_log", &gpu_log, sizeof(gpu_log));
			gpu_kept = true;
			quiet = true;
			printf("adreno: THE GPU STAYS ON for the system to draw with: the service HakiosGPU\n");
			HakiosGPUStart();
			return;
		}
		if (!by_itself) {
			printf("adreno: not kept: the steps did not get as far as a 3D core that takes commands\n");
		}
	}
	if (by_itself) {
		/* (one line for whoever wonders why the picture is drawn slowly; the steps' own lines are above it) */
		printf("adreno: THE GPU IS NOT STARTED: step %u (%s) failed; the system draws with its software renderer\n",
		    failed != 0 ? failed : 21, what[failed != 0 && failed <= STEPS ? failed : 21]);
	}
	/*
	 * Punishment for a write may come late (a reset some 17 seconds after it has been seen):
	 * keep the end of the log saying where things stand.
	 */
	for (uint32_t second = 1; second <= watch_seconds; second++) {
		sleep_ms(1000);
		printf("adreno: alive %u s after step %u; %s\n", second, done, last_write);
	}
	gpu_block_stop();
	if (PE_parse_boot_argn("adreno_return", &only, sizeof(only)) && only) {
		printf("adreno: over; back to the loader (adreno_return)\n");
		loader_return();
	}
	printf("adreno: over; the system goes on starting\n");
}
