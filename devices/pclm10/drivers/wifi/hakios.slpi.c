/*
 * The SLPI of the OPPO Reno Ace (PCLM10): its power rails, and the first look at its Q6's
 * control block. The SLPI is the subsystem the WLAN runs on (notes/wifi.md §2).
 *
 * The runtime device tree (vendor-src/dtb/boot.bin.01.dts:3322) names it `qcom,ssc@5c00000`:
 * compatible "qcom,pil-tz-generic", qcom,pas-id = <0x0c>, qcom,firmware-name = "slpi", its
 * image region at 0x98100000 for 20 MiB, its control block at 0x5c00000 + 0x4000, clocked by
 * the xo, held on by two proxy regulators (vdd_cx, vdd_mx) with qcom,keep-proxy-regs-on, and
 * started through TrustZone and the AOP (qcom,signal-aop, mboxes "slpi-pil"). The vendor's
 * driver for that shape (drivers/soc/qcom/subsys-pil-tz.c, fetched to vendor-src/remoteproc/)
 * starts and stops the subsystem with SCM calls into the secure world and mailbox messages to
 * the AOP: it never touches one MMIO register of the control block. So what this driver does is
 * not what the vendor's driver does. M1 was the rails and one read of the block. M2, since
 * 2026-11-03, starts the subsystem the way the vendor's driver starts it: the phone's own
 * signed image is read off the root, put in the region the device tree reserves for it, handed
 * to TrustZone to authenticate, and let out of reset after the AOP is told its image is loaded.
 *
 * M2, and where every number of it comes from. The image is slpi.mdt and slpi.b02 to slpi.b20
 * from /vendor/firmware_mnt/image of the phone's own Android (S:\opencode\fw-wifi\, put into the
 * system image under /fw by userland/build.sh, 6.1 MB with its header). The region is the one
 * the ssc node's memory-region phandle 0x7a names: reserved-memory pil_slpi_region at 0x98100000
 * for 20 MiB (android-merged.dts:22858). The mdt's own header is 8380 bytes and its twenty
 * loadable segments span 0x97300000 to 0x98700000, also exactly 20 MiB, and its entry,
 * 0x97300000, is the start of the lowest one. Every segment carries bit 27 of its p_flags, the
 * relocatable mark (peripheral-loader.c:810), so the vendor's loader moves the image to wherever
 * its region begins and shifts every segment, and the entry with it, by region_start minus the
 * lowest segment's address (peripheral-loader.c:815, mdt_loader.c:150-189): here that offset is
 * 0x98100000 - 0x97300000 = 0xE00000, and the span fills the region to its last byte. The
 * segment files are named by their program-header index (mdt_loader.c:180), and a segment whose
 * filesz is 0 is never asked for (mdt_loader.c:177), which is why there is no slpi.b21.
 *
 * The order of starting is the vendor's pil_boot() (peripheral-loader.c:1346) as this port's ADSP
 * driver has proven it end to end (drivers/audio/hakios.adsp.c): open the mailbox, hand TrustZone
 * a page with the mdt's header for its signature, tell TrustZone where the image will be, write
 * the segments, tell the always-on processor the image is loaded, and ask TrustZone to
 * authenticate the image and let the subsystem out of reset; an answer that is not zero gets
 * "off" back. The files are on the root, and the root is not mounted when EARLY_BOOT startup
 * runs, so M2 happens on a kernel thread of its own that waits for the root first, the way the
 * ADSP's whole_thread() does.
 *
 * The rails. The WLAN's platform node (icnss) asks for four, and the merged device tree says
 * exactly which PMIC LDO each one is and under what name the RPMh firmware knows it: its own
 * qcom,resource-name nodes name pm8150_l1 "ldoa1" and pm8150l_l2 "ldoc2" (android-merged.dts
 * 12241 and 12787), so the family letter of each PMIC is the tree's own and not a guess. The
 * voltages are the icnss node's own config pairs, and icnss_vreg_on() (vendor-src/wifi/icnss.c
 * 388-431) asks each regulator for exactly the low end of its pair:
 *
 *   ldoa1     752 mV  qcom,vdd-cx-mx-config  = <0xb7980 0xb7980>   pm8150 l1, fixed
 *   ldoa7    1800 mV  qcom,vdd-1.8-xo-config = <0x1b7740 0x1b7740> pm8150 l7
 *   ldoc2    1304 mV  qcom,vdd-1.3-rfa-config = <0x13e5c0 0x13e5c0> pm8150l l2
 *   ldoc11   3104 mV  qcom,vdd-3.3-ch0-config = <0x2f5d00 0x328980>, the low end, pm8150l l11
 *
 * Each is an RPMh VRM resource, written the way this phone's touch driver writes ldoa17: three
 * TCS commands, the voltage in millivolts at the cell, enable at +4, mode at +8. The mode sent
 * is auto, the value 2: every one of these four resources carries qcom,init-mode = <0x02>, and
 * auto is the gentler of the two modes each supports (qcom,supported-modes = <0x02 0x04>).
 *
 * The SLPI's own two supplies are ARC resources, not VRM. The boot tree's vdd_cx and vdd_mx
 * phandles resolve to the rpmh-arc-regulator nodes "lcx.lvl" and "lmx.lvl" (boot.bin.01.dts:13238
 * and :13143, the pm8150 l8 and l4 *levels*, init level 0x11). The ssc node asks for level 0x181
 * on both (qcom,vdd_cx-uV-uA = <0x181 0>, the same for vdd_mx), and that is what this driver
 * votes now: one TCS command per resource, the level as its data, no enable or mode register
 * (rpmh-regulator.c: the ARC has one, hlvl). It is part of the vendor's proxy vote, and the
 * earlier "reported, not written" left the level at the boot's 0x11.
 *
 * The Q6 block is read last, after every other line this driver can write, because reading a
 * subsystem's registers while it is unpowered or unclocked can end the kernel -- the precedent
 * in this tree is drivers/wifi/hakios.wifi.c:56-58, "has ended the kernel". The SLPI has
 * certainly not been started on this boot: nothing before this driver loaded slpi.mdt. The only
 * register names written here are the four both of the fetched q6v5 drivers agree on: the AXI
 * halt request at +0x0, its acknowledge at +0x4, idle at +0x8 and the reset control at +0x14
 * (vendor-src/remoteproc/pil-q6v5.c:40 and qcom_q6v5_pil.c:64). The rest of the block is
 * dumped without names, because the driver that owns this subsystem gives the block no names.
 *
 * What the phone answered on 2026-11-03 (logs/095451-run.log), and why the block is read only
 * after M2. The first read of 0x5c00000 did not fault and did not read zeroes or ones: the load
 * never came back. The kernel's log stopped at the announcement line and the loader's watchdog
 * said THE KERNEL HUNG, no sign of life for 50 seconds: the SSC's interconnect does not answer
 * at all while the subsystem is down, so the bus waits forever. No instruction after that
 * address can ever run before the subsystem is up, whatever the read would have returned. So the
 * block is read only after M2's attempt, from its thread: if the start came up the read is the
 * observation M1 wanted; if it did not, the read is a known way to end a run and only slpi=4
 * asks for that anyway.
 *
 * Two later rounds took the weight out of that read, and both are why it is now gated. Neither
 * the vendor's driver nor mainline's reads this block for this subsystem -- the vendor's
 * mask_scsr_irqs is SPSS-only and mainline's node for the slpi is the PIL block, with its ready,
 * fatal, handover and stop events delivered as SMP2P bits -- so the block can stay dark while the
 * Q6 runs, and a read that returns nothing proves nothing either way. And the read does stall
 * with the subsystem up: logs/030641-run.log, a run in which the slpi did come out of reset, ends
 * at the block's announcement line with the reading thread never coming back, and a stalled thread
 * there is what kept the boot processor from answering at the shutdown. So the block is read only
 * when the start failed, where the answer is wanted and the cost is known.
 *
 * slpi=1 reads the boot loader's command database and lists every resource it holds: the names
 * and addresses the votes are looked up in, printed as found. slpi=2 adds the four rail votes
 * and the two ARC reports, and nothing that can end the run. slpi=3 starts M2 on its thread and
 * reads the q6 block after it: the root's being mounted is waited for, then slpi_wait seconds
 * (3 by default) after TrustZone has let the subsystem out of reset before the block is read.
 * slpi=4 is the same and reads the block even when the start failed, knowing what that costs.
 * slpi=5 leaves the mailbox to itself and asks TrustZone directly, and does M3 instead of reading
 * the block: the outbound item 430 is made before the start and the doorbell rung, and the
 * slpi's item 481 is watched for slpi_wait seconds. slpi=6 is slpi=5 with the mailbox back, which
 * is what the phone's own kernel does -- qcom,signal-aop on the ssc node is that load_state
 * message over that mailbox (see slpi_start()). Whether the mailbox was open turned out not to be
 * the question anyone was asking: the slpi's smp2p probe runs minutes after TrustZone releases
 * its reset, and every watch that "saw nothing" had only given up too early. logs/031835-run.log
 * started the slpi with the mailbox left alone and never saw item 481; logs/033426-run.log, the
 * next round, found it already allocated and watched its entries register. logs/034623-run.log
 * started it with the mailbox open and saw nothing; logs/035630-run.log, the next round, found
 * 481 with its three entries registered and five more items allocated beside it -- 478, 480, 608,
 * 611, 613, which is where a glink edge keeps its version exchange and its ring. So the q6 runs,
 * its kernel runs, its smp2p driver is alive, and its glink edge exists on its side of the
 * boundary. What no round has ever had is the other side: the ap has no glink edge, so the
 * "slave-kernel" ready bit has nothing to be set for, and the slpi's own sfr item 424 says what
 * its boot does while it waits -- logs/035630-run.log read it as text, "SFR Init: wdog or kernel
 * error suspected.", the reason a hexagon subsystem restart records for a watchdog bite or a
 * kernel fault. The next step is therefore not another observation but a driver: the host-side
 * glink edge, M4, which drivers/wifi/hakios.wifi.c already opens for redwood's wpss. The watch
 * looks for the item for its whole duration -- logs/034623-run.log caught the old one returning
 * at once when the item was absent, which proved absence at its first second and nothing after
 * it -- and at the end the driver reads what the slpi has left that cannot end a run: its sfr
 * item, and the shared partition's items with their printable text and their first bytes in hex,
 * because a glink version structure is binary and a text pass alone would call it empty. slpi=7
 * adds M3b, reading the image's own memory afterwards -- a read known to end the run, because the
 * memory belongs to the subsystem once TrustZone has authenticated the image to it (see M3b).
 * slpi=8 starts nothing: the instance an earlier round left running is still running, its glink
 * items are in shared memory, and what has never existed is our end of that edge. slpi=8 is M4:
 * item 479 allocated, the version exchange, the channel, the intents, and slpi_wait seconds of
 * polling its queue for an answer (see M4). slpi=9 starts a fresh instance and waits for its
 * glink init with a sentinel: the mark the slpi's driver writes is set to 0xffffffff and the
 * wait polls until it reads back in range. logs/053241-run.log ran it and the mark never
 * changed -- but the wait began a minute after the reset, and the instance's init was over
 * long before that: what logs/035630-run.log (items present at a 60 s scan) and
 * logs/050214-run.log (the fifo reset by a 35 s scan) seemed to show was when our own scans
 * first looked, and logs/060328-run.log ended the question: the announcement was already
 * there on the watch's first poll, under a second after the reset. The sentinel lost
 * that race, and reading the logs side by side says what the instances actually do: the one
 * logs/043646-run.log started, which found the descriptor all zeroes and its queue empty, is
 * the only one that ever announced its version; the ones that found an old version message at
 * offset 0 of that queue reset the fifo and stayed quiet -- the message logs/051227-run.log
 * read as written-but-never-announced was the earlier instance's, still lying there -- and
 * none of them, ever, read our queue: our_tail has read 0 in every log. slpi=10 is the
 * experiment that follows: slpi_glink_clean() writes zeroes over the descriptor and both
 * queues while the old instance is powered off and the new one is still held in reset, and
 * the wait for an announcement starts at the reset itself, where a zeroed mark needs no
 * sentinel: any value above 0 is the new instance's own. logs/060328-run.log ran it the same
 * day and the model held in full: the watch read the mark on its first poll -- "after 0 s
 * the slpi's write mark reads 8" -- so an instance's init is not a minute or 60 s old at its
 * reset, it is under a second, and those 60 s had only ever measured when our own scan first
 * looked; with a clean queue in front of it the instance wrote its version at offset 0,
 * announced it, and we read it and answered it. And the rx question answered itself in the
 * negative for the fourth time: bit 0x18 was rung at the instance's freshest moment and five
 * times after it, and our_tail never moved. The doorbell is exonerated under every condition
 * -- right register, clean edge, fresh instance, its announcement already out: the instance's
 * glink driver has no working rx. Reading that rx out of the firmware closed itself off:
 * the .mdt has no section headers and is pre-relocated, exact-match scans of every segment
 * for the edge-config cluster's link-time addresses found only data-to-data links, and the
 * single hit inside a code segment, slpi.b14+0xf2650, turned out to be a record of an
 * import table -- 16-byte records whose fourth field steps by 6, a packed-reloc stream --
 * not a code reference at all. No code holds those addresses as literals; the application
 * module reaches those tables through imports resolved at load time, which static scanning
 * cannot follow. slpi=11 is the question that does not need the firmware: the smp2p rx,
 * asked with the protocol's own ping. Every 0x1a ring so far went out before a reset or as
 * a bare pulse with no bit changed, which a live handler answers with silence anyway
 * (smp2p.c:317-349 dispatches changes, and there were none); the vendor's protocol has a
 * question that cannot be answered with silence -- restart-done in our item's flags, which
 * check_ssr watches, answered by do_ssr_ack toggling restart-ack in the slpi's own item's
 * flags (smp2p.c:197-224), the ack both sides advertise with features 1 and the slpi's own
 * driver wrote features 1 into item 481 (logs/024654-run.log). The ack is a word only the
 * slpi can write; if it comes, the doorbell reaches the slpi and only glink's rx is broken,
 * and if it does not, no apcs bit reaches any handler here and the difference from Android
 * is in how the subsystem was brought up (see slpi_smp2p_handshake). logs/065342-run.log
 * asked it: nothing answered, no word moved in item 481 in 45 s and two rings. But the run
 * read one thing no earlier run had: the flags of item 481 themselves, which read 0x1 before
 * anything was rung -- the slpi's own restart-done, the announcement its smp2p driver writes
 * when its side comes out of a reset, still sitting there unanswered because our kernel never
 * implements the ack. So the driver ran its init and registered its entries; what never
 * happened is its interrupt handler. Two causes are left and only the block itself can tell
 * them apart: a bit that was rung but never taken stays visible in the apcs trigger register
 * and its queue register, a bit that was taken does not. slpi=12 is that question: the same
 * instance, the same ping, and the block read before, at once after the ring, and every
 * second after -- and the baseline is itself a witness, because logs/060328-run.log's five
 * rings of bit 0x18 at the dead glink rx and logs/065342-run.log's two rings of bit 0x1a
 * can only still be there if undelivered bits latch (see slpi_apcs_pending_watch).
 * logs/071531-run.log read the block: its trigger and queue registers were zero before, at
 * once after the ring, and every second after -- the bits are self-clearing pulses with no
 * latched witness, and the block cannot say which side is silent. What has never been asked
 * is the other direction of the same wire: when the slpi's driver rings at us, does its ring
 * reach our own interrupt controller. The vendor's devicetree names those two lines: the
 * glink edge dsps has interrupts = <0x00 0xaa 0x01>, spi 170 (android-merged.dts:4106), and
 * qcom,smp2p-dsps@1799000c has interrupts = <0x00 0xac 0x01>, spi 172 (:16769). Two rings
 * are known to have happened: the smp2p probe writes its item and its restart-done and kicks
 * (smp2p.c:181-191, 488-530), and logs/065342-run.log read that restart-done in item 481;
 * the glink init rings after it writes its version, and logs/060328-run.log read that
 * version. Our kernel masks both lines -- it has only ever polled -- and a masked edge
 * interrupt still latches in GICD_ICPENDR and stays there until someone comes for it, so one
 * read answers for every ring since this boot. slpi=13 reads that word before anything,
 * starts a fresh instance the way slpi=10 does -- its init is under a second old at its
 * reset and both rings are certain by the moment the announcement is seen -- and reads it
 * again at the announcement and at the end; the type bits are read with it, because a line
 * configured level samples a pulse and keeps nothing and its silence would prove nothing (see
 * slpi_gic_listen). logs/074524-run.log read level, the gic's reset state, on every read:
 * our kernel never programs ICFGR for a line it does not attach, so the precondition
 * failed and the run asked its own follow-up -- slpi=14 sets the type to edge, what the
 * devicetree says these lines are, allowed while the lines are disabled, and reads back
 * (see slpi_gic_edge). logs/075618-run.log wrote edge and read zeros back. That reads as
 * secure interrupts, but the vendor tree shows the plainer way to get exactly that
 * behaviour: pinctrl-msm.c programs these same GIC SPI type bits by calling the firmware,
 * scm_io_read and scm_io_write over the GICD's own ICFGR addresses -- "the shared SPI
 * config for Edge is 0 and for Level interrupt is 1" -- because on these SoCs the
 * distributor is among the registers the non-secure world may not touch, and if that is
 * true here then every direct read of ours, m13's included, returned a zero and not a
 * state. slpi=15 asks the whole question through the owner: the firmware's read of the
 * three words, the type set through it, and the pending register read through it around
 * the same fresh instance (see slpi_gic_firmware_listen). logs/080949-run.log answered
 * that in the negative too, and its own premise fell with it: the calls returned -2,
 * which the firmware's header names QCOM_SCM_EINVAL_ARG, not EOPNOTSUPP, which is -4
 * (drivers_firmware_qcom_scm.h:60-69) -- the call was dispatched and the argument
 * refused -- and the spi_cfg block pinctrl-msm.c writes through that service is not
 * the distributor at all: the devicetree names it as the pinctrl node's second reg,
 * 0x17c000f0, an always-on block beside the doorbell (android-merged.dts:6272-6275).
 * So m13-m15 read real hardware: both lines sit masked, level, not pending. What is
 * left is the one thing a hand-written register read cannot do: our kernel's own
 * gic_v3_spi_attach programs group, priority, type and route and unmasks for every
 * SPI a driver takes -- the radio's linux layer claims its lines through it,
 * hakios.lx.c:464 -- and m13 and m14 only ever wrote ICFGR by hand and never the
 * group, which is exactly how a line the secure world holds behaves. slpi=16 claims
 * the two lines through that path and asks gic_v3_spi_count(), a number that cannot
 * be refused to be read, around the same fresh instance; two read-only controls come
 * with it: the distributor's own typer and iidr through the same mapping, which
 * decide whether m13-m15 read hardware at all, and the firmware's own availability
 * probe asked about the io service with a call we make work every boot as the
 * positive control (see slpi_rings_claim).
 * M17 corrects a numbering error in M13-M16: DT SPI cells are relative
 * to 32, whereas the kernel API takes absolute INTIDs. Thus 170/172
 * become 202/204. M16's zero counts were on the wrong lines and do not
 * establish a dead return path. ICFGR edge is field 2 (not field 1).
 * logs/093136-run.log counted GLINK 2 / SMP2P 1 on those corrected IDs.
 * M18 keeps them and changes only one protocol action: acknowledge the
 * remote restart-done in our flags bit 1, then ring, before GLINK exchange.
 * M18's ack read back 0x2 but the host tail stayed zero (logs/094426-run.log).
 * M19 adds only PAS_SHUTDOWN before rewriting shared memory and reloading,
 * so AOP load_state "off" is no longer mistaken for a verified DSP stop.
 * M20 preserves M19 and instruments crash evidence: absolute watchdog INTID
 * 526 (DT SPI 0x1ee + 32), plus a read-only SFR baseline after shutdown.
 * M20 observed a new watchdog event (logs/101014-run.log). M21 changes only
 * host GLINK activity: no version, ack, pump or FIFO cursor updates during
 * a 45-real-second passive observation; SMP2P restart-ack stays enabled.
 * No vote here is ever dropped. Nothing at all happens
 * unless a boot argument asks for it.
 *
 * The command database is found the way this phone's touch driver found it (logs/012447-run.log,
 * repeated in every round since): the tree's qcom,cmd-db@c3f000c node is a pointer, eight bytes
 * at 0xc3f000c, the table's address and its length, and the table's magic is 0x0c0330db at its
 * offset 4. The names are walked, not the records: eight bytes of id-shaped text are at the
 * front of every entry, the way cmd_db_get_u64_id packs a resource id, and the address that
 * belongs with a name is the word two further on, at +16 (hakios.s3706.c:2537-2646 worked the
 * layout out entry by entry, logs/012836-run.log; this phone's table is 4424 bytes at
 * 0x85f20000, and the pointer node is what names that address, so no boot argument is needed).
 */
#include <kern/clock.h>
#include <kern/kalloc.h>
#include <kern/locks.h>
#include <kern/startup.h>
#include <kern/thread.h>
#include <libkern/libkern.h>
#include <mach/mach_types.h>
#include <mach/vm_param.h>
#include <pexpert/pexpert.h>
#include <sys/errno.h>
#include <sys/kauth.h>
#include <sys/systm.h>
#include <sys/vnode.h>
#include <sys/vnode_internal.h>
#include <vm/memory_types.h>
#include <vm/vm_kern.h>

extern vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);
extern vm_offset_t ml_io_map_wcomb(vm_offset_t phys_addr, vm_size_t size);
extern vm_offset_t ml_io_map_unmappable(vm_offset_t phys_addr, vm_size_t size, unsigned int flags);
extern void ml_io_unmap(vm_offset_t addr, vm_size_t sz);
extern uint64_t kvtophys(vm_offset_t va);
extern void gic_v3_spi_mask(uint32_t intid);   /* osfmk/arm64/gic_v3_irq.c */
extern kern_return_t thread_terminate(thread_t thread);
extern lck_mtx_t hakios_aop_mailbox_lock;
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
	delay_for_interval(us, NSEC_PER_USEC);
}

static void
sleep_ms(uint32_t ms)
{
	delay_for_interval(ms, NSEC_PER_MSEC);
}

/* Until every write to memory so far has been carried out. */
static void
settle(void)
{
	__asm__ volatile ("dsb sy");
}

/* ---- the boot loader's command database --------------------------------------------------------- */

#define CMD_DB_POINTER            0x00c3f000cUL         /* the tree's qcom,cmd-db@c3f000c: reg <0xc3f000c 0x08> */
#define CMD_DB_MAGIC              0x0c0330dbu
#define CMD_DB_HEADER_SIZE        148                   /* the vendor's sizeof(struct cmd_db_header): 8 + 8*16 + 8 */
#define CMD_DB_MAX_RES            256                   /* the phone's table held 139 entries (20+10+41+68, logs/012836) */

struct cmd_res {
	char name[9];
	uint32_t address;
};

static struct cmd_res cmd_res_table[CMD_DB_MAX_RES];
static unsigned cmd_res_count;
static bool resources_read;

/* Is the eight bytes at a table offset an id-shaped name: a lowercase letter first, then
 * letters, digits, dots and underscores -- "ldoa1", "lcx.lvl" -- NUL-padded to eight. The
 * bytes come out of the two words in little-endian order, the way the touch driver packed
 * its search key into a uint8_t array (hakios.s3706.c:2566-2573). */
static bool
id_shaped(uint32_t first, uint32_t second)
{
	bool padded = false;

	for (unsigned c = 0; c < 8; c++) {
		uint32_t word = c < 4 ? first : second;
		uint8_t ch = (uint8_t) ((word >> (8 * (c & 3))) & 0xff);

		if (ch == 0) {
			padded = true;                  /* NUL padding is allowed, but only at the end:
			                                     * a packed id is the text and then zeroes */
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

/* Read the table once and keep what it names. The walk is by fours, the way the touch driver's
 * cmdb_look() and the clock driver's cmd_db_address() walk it: an entry's name is at the front
 * of the entry and entries are twenty-four bytes apart, so a walk by fours finds every name at
 * its start and nothing else id-shaped inside an entry -- the two words after a name are zero
 * for every resource this phone's table holds. What that walk cannot rule out is id-shaped text
 * in the data section the records point at; the address filter does, because every real cell
 * address is a small one (ldoa17 is 0x41300, MC0 0x50000, and every real address fits under
 * 0x1000000: the high half is the slave id and the low half the cell offset). */
static bool
cmdb_read(void)
{
	if (resources_read) {
		return cmd_res_count != 0;
	}
	resources_read = true;
	vm_offset_t pointer = ml_io_map(CMD_DB_POINTER, 0x1000);

	if (pointer == 0) {
		say("slpi: cmd db: could not map the pointer at %#x\n", CMD_DB_POINTER);
		return false;
	}
	uint32_t base = rd(pointer), length = rd(pointer + 4);

	say("slpi: cmd db: the pointer at %#x reads %#x with %u bytes after it\n", CMD_DB_POINTER,
	    base, length);
	if (base < 0x1000 || base + length <= base || length == 0 || length > 0x200000) {
		say("slpi: cmd db: that is not a table I can read, stopped\n");
		return false;
	}
	vm_offset_t db = ml_io_map_wcomb((vm_offset_t) base, (vm_size_t) length);

	if (db == 0) {
		say("slpi: cmd db: could not map the table at %#x\n", base);
		return false;
	}
	if (rd(db + 4) != CMD_DB_MAGIC) {
		say("slpi: cmd db: the table at %#x has magic %#x, not the cmd db magic, stopped\n",
		    base, rd(db + 4));
		return false;
	}
	for (unsigned at = CMD_DB_HEADER_SIZE; at + 24 <= length; at += 4) {
		uint32_t first = rd(db + at), second = rd(db + at + 4);
		uint32_t address = rd(db + at + 16);

		if (!id_shaped(first, second) || address == 0 || address >= 0x1000000) {
			continue;
		}
		if (cmd_res_count == CMD_DB_MAX_RES) {
			say("slpi: cmd db: stopped at %u entries, the table holds more\n", cmd_res_count);
			break;
		}
		struct cmd_res *res = &cmd_res_table[cmd_res_count++];

		for (unsigned c = 0; c < 8; c++) {
			uint32_t word = c < 4 ? first : second;

			res->name[c] = (char) ((word >> (8 * (c & 3))) & 0xff);
		}
		res->name[8] = 0;
		res->address = address;
		say("slpi: cmd db: +0x%04x: \"%s\" at %#x\n", at, res->name, address);
	}
	say("slpi: cmd db: the table at %#x holds %u named resources\n", base, cmd_res_count);
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

/*
 * The apps_rsc controller and its two active-only slots, exactly as this phone's touch driver
 * uses them (hakios.s3706.c:2341-2525, proven on the phone in logs/012836 and every round
 * since): the controller at 0x18220000 maps a window of one 0x2a0-wide block per slot from
 * 0x18220d00, the tree's qcom,tcs-config lists the active-only slots first, so those are
 * blocks 0 and 1, and a vote goes in the vendor's tcs_mbox_write() order: the command words,
 * the wait and enable bits, then trigger off, mode off, mode on, trigger on, then the wait for
 * the slot's interrupt bit and the command's completed bit, then the disarm and acknowledge
 * that make the slot usable again.
 */
#define RSC_TCS_PHYS              0x018220d00UL         /* the window of tcs slot blocks */
#define RSC_TCS_SIZE              0x3000
#define DRV_TCS_STRIDE            0x2a0UL               /* RSC_DRV_TCS_OFFSET in the vendor's driver: 672 */
#define DRV_CMD_STRIDE            0x14UL                /* RSC_DRV_CMD_OFFSET: 20 */
#define DRV_TCS(slot, reg, cmd)   (rsc_tcs + DRV_TCS_STRIDE * (slot) + (reg) + DRV_CMD_STRIDE * (cmd))
#define DRV_CONTROL               0x14                  /* bit 16 arms the slot, bit 24 triggers it */
#define DRV_STATUS                0x18                  /* non-zero: the controller has finished with the
                                                           * slot, which is the vendor's tcs_is_free() */
#define DRV_CMD_WAIT_FOR_CMPL     0x10
#define DRV_CMD_ENABLE            0x1c
#define DRV_CMD_MSGID             0x30                  /* [7:0] length, bit 8 a response is wanted, bit 16 write */
#define DRV_CMD_ADDR              0x34
#define DRV_CMD_DATA              0x38
#define DRV_CMD_STATUS            0x3c                  /* bit 8 issued, bit 16 completed */
#define RSC_IRQ_STATUS            0x04                  /* the controller's own registers share slot 0's
                                                           * first sixteen bytes: one bit per slot in each */
#define RSC_IRQ_CLEAR             0x08
#define AMC_MODE_ENABLE           (1u << 16)
#define AMC_MODE_TRIGGER          (1u << 24)
#define CMD_LEN                   8                     /* CMD_MSGID_LEN: the slot holds eight commands */
#define CMD_RESP_REQ              (1u << 8)
#define CMD_WRITE                 (1u << 16)
#define RSC_ACTIVE_SLOTS          2
#define RSC_WAIT_POLLS            2000                  /* 200 ms at the transfer poll interval */
#define TRANSFER_POLL_US          100

static vm_offset_t rsc_tcs;

/* The vendor's write_tcs_reg_sync(): the hardware wants the value to read back before the next
 * write. Bounded: a word that will not settle must say so, not hang the run. */
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
	say("slpi:   rpmh: %#lx would not take %#x: it reads %#x\n", (unsigned long) where, want,
	    rd(where));
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
		say("slpi: could not map the rpmh tcs window at %#lx\n", (unsigned long) RSC_TCS_PHYS);
		return false;
	}
	return true;
}

/* Hand one write to one address through a free slot, and wait for RPMh to say it is done. */
static bool
tcs_send(uint32_t cell, uint32_t data)
{
	uint32_t status = 0, polls, irq;
	unsigned slot;
	bool found = false;

	if (rsc_tcs == 0) {
		say("slpi:   rpmh: the controller is not mapped: the command is not sent\n");
		return false;
	}
	for (slot = 0; slot < RSC_ACTIVE_SLOTS; slot++) {
		if (rd(DRV_TCS(slot, DRV_STATUS, 0)) != 0) {
			found = true;
			break;
		}
	}
	if (!found) {
		say("slpi:   rpmh: no slot has ever completed a transaction: both read %#x\n",
		    rd(DRV_TCS(0, DRV_STATUS, 0)));
		return false;
	}
	/* reclaim the slot first, as the vendor's tcs_irq_handler does for one that ended */
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
	say("slpi:   rpmh: slot %u: cell %#x data %#x: %u of %u polls, interrupts %#x, "
	    "command status %#x%s\n", slot, cell, data, polls, RSC_WAIT_POLLS, irq, status,
	    status & (1u << 16) ? ", completed" : ", not completed");
	wr(rsc_tcs + RSC_IRQ_CLEAR, 1u << slot);
	wr(DRV_TCS(slot, DRV_CMD_ENABLE, 0), 0);
	wr(DRV_TCS(slot, DRV_CMD_WAIT_FOR_CMPL, 0), 0);
	return (status & (1u << 16)) != 0;
}

/* ---- the four rails ----------------------------------------------------------------------------- */

struct rail {
	const char *name;
	uint32_t millivolts;
	const char *what;
};

/* The names and the voltages are the device tree's own, and were read off it above; the header
 * says where each number comes from. None of these four is shared with the display: the one
 * rail this port has lost a screen over is ldoa17, and it is not in this table. */
static const struct rail rails[] = {
	{ "ldoa1",   752, "the vdd-cx-mx rail, pm8150 l1, fixed" },
	{ "ldoa7",  1800, "the 1.8 V xo rail, pm8150 l7" },
	{ "ldoc2",  1304, "the 1.3 V rfa rail, pm8150l l2" },
	{ "ldoc11", 3104, "the 3.3 V ch0 rail, pm8150l l11, the low end of its range" },
};

static void
rail_vote(const struct rail *rail)
{
	uint32_t cell = cmdb_look(rail->name);
	bool ok;

	if (cell == 0) {
		say("slpi: %s: the command database has no such resource: %s stays as the boot left it\n",
		    rail->name, rail->what);
		return;
	}
	if (!rpmh_map()) {
		return;
	}
	ok = tcs_send(cell, rail->millivolts);          /* the millivolts as they are, which is what
	                                                   * rpmh_regulator_vrm_set_voltage() sends */
	ok = tcs_send(cell + 4, 1) && ok;               /* enable */
	ok = tcs_send(cell + 8, 2) && ok;               /* auto: the qcom,init-mode of all four */
	say("slpi: %s at %#x: %u mV, enabled, auto: %s (%s)\n", rail->name, cell, rail->millivolts,
	    ok ? "all three votes completed" : "a vote did not complete", rail->what);
}

/*
 * The SLPI's own two supplies, voted the way the vendor's proxy vote does it. The ssc node's
 * qcom,vdd_cx-uV-uA = <0x181 0> and qcom,vdd_mx-uV-uA = <0x181 0> are *levels*, not microvolts:
 * the supplies resolve to rpmh-arc-regulator nodes ("pm8150 l8/l4 level", min 0x11, max 0x10000),
 * and the ARC resource has a single register, hlvl, at the entry's own address
 * (rpmh-regulator.c: RPMH_REGULATOR_REG_ARC_LEVEL = 0, name "hlvl", size 2; "ARC does not have a
 * specific register for enable voting", :70-74). One command with the level as its data is the
 * whole vote -- no enable, no mode, unlike the four VRM rails above.
 */
static void
arc_vote(void)
{
	static const struct { const char *name; const char *what; } arc[] = {
		{ "lcx.lvl", "the ssc node's vdd_cx, the pm8150 l8 level" },
		{ "lmx.lvl", "the ssc node's vdd_mx, the pm8150 l4 level" },
	};
	const uint32_t level = 0x181;   /* qcom,vdd_cx-uV-uA and qcom,vdd_mx-uV-uA, both */

	for (unsigned i = 0; i < sizeof(arc) / sizeof(arc[0]); i++) {
		uint32_t cell = cmdb_look(arc[i].name);
		bool ok;

		if (cell == 0) {
			say("slpi: %s: the command database does not name it (%s)\n", arc[i].name,
			    arc[i].what);
			continue;
		}
		if (!rpmh_map()) {
			return;
		}
		ok = tcs_send(cell, level);
		say("slpi: %s at %#x: level %#x: %s (%s)\n", arc[i].name, cell, level,
		    ok ? "the vote completed" : "the vote did not complete", arc[i].what);
	}
}

/* The subsystem's XO, without which the Q6 has no clock at all. The ssc node's
 * clocks = <&rpmhclk 0>, and the RPMh clock controller's first clock is the CXO (clk-rpmh.c:
 * rpmh_clocks[RPMH_CXO_CLK] = sm8150_bi_tcxo, resource "xo.lvl", 0x30090 in this phone's
 * command database). A BCM vote is one TCS command whose data is BCM_TCS_CMD(valid, vote) =
 * 0x40000000 | valid << 29 | vote, so an "on" vote is 0x60000001 (clk-rpmh.c: clk_rpmh_bcm_send_cmd,
 * BCM_TCS_CMD). It is part of the vendor's proxy vote for this node. */
static void
xo_vote(void)
{
	uint32_t cell = cmdb_look("xo.lvl");
	bool ok;

	if (cell == 0) {
		say("slpi: xo.lvl: the command database does not name it\n");
		return;
	}
	if (!rpmh_map()) {
		return;
	}
	ok = tcs_send(cell, 0x60000001);
	say("slpi: xo.lvl at %#x: %s (the slpi's XO clock)\n", cell,
	    ok ? "the vote completed" : "the vote did not complete");
}

/* ---- M2: the image, TrustZone, and the always-on processor ----------------------------------------- */

/*
 * The region, from the device tree: the ssc node's memory-region phandle 0x7a is reserved-memory
 * pil_slpi_region (android-merged.dts:22858), "removed-dma-pool", no-map, 20 MiB at 0x98100000:
 * memory of nobody's, exactly like the ADSP's 0x86700000. The image's own span is 20 MiB too, so
 * with the segments shifted to this base the last byte of the last one is the last byte of the
 * region; that is checked, not assumed.
 */
#define SLPI_MEMORY             0x098100000u    /* pil_slpi_region's reg */
#define SLPI_IMAGE_SIZE         0x01400000u     /* its size, and the mdt's span: 0x97300000 to 0x98700000 */
#define SLPI_LOWEST_SEGMENT     0x97300000u     /* the lowest loadable segment's p_paddr, read off slpi.mdt itself */
#define PAS_SLPI                0x0c            /* the ssc node's qcom,pas-id, boot.bin.01.dts:3331 */
#define SLPI_SEGMENT_FILES      22              /* ph2 to ph20: the phdr count the mdt's own header says it has */

struct slpi_segment {
	uint32_t index;                         /* which phdr, and therefore which .b file */
	uint32_t at;                            /* where in the region its first byte goes, after the shift */
	uint32_t file_size, memory_size;
};

static struct {
	const uint8_t *header;
	uint64_t header_size;
	struct slpi_segment segment[SLPI_SEGMENT_FILES];
	unsigned segments;
	uint32_t lowest, highest;               /* the image's own span, as its phdrs say */
	uint32_t entry, relocated_entry;
	uint32_t base, shift, size;             /* where the image will be, and by how much its addresses move */
	bool written;
} image;

/*
 * A file of /fw on the root, read whole into memory of ours: the system image, where
 * userland/build.sh puts fw-wifi/ when it makes that image. It is there only once the root is
 * mounted, which is why all of M2 waits for the root on its thread. (The size limit is the
 * region's: no file of this image comes near it, and a file that big could not belong here.)
 */
static const uint8_t *
root_file(const char *name, uint64_t *size)
{
	char path[64];
	vfs_context_t context = vfs_context_kernel();
	vnode_t vnode = NULLVP;
	struct vnode_attr attributes;
	uint8_t *data;
	int left = 0, error;

	if (rootvnode == NULLVP) {
		return NULL;
	}
	snprintf(path, sizeof(path), "/fw/%s", name);
	if (vnode_lookup(path, 0, &vnode, context) != 0) {
		return NULL;
	}
	VATTR_INIT(&attributes);
	VATTR_WANTED(&attributes, va_data_size);
	if (vnode_getattr(vnode, &attributes, context) != 0 || attributes.va_data_size == 0 ||
	    attributes.va_data_size > SLPI_IMAGE_SIZE) {
		vnode_put(vnode);
		return NULL;
	}
	data = kalloc_data(attributes.va_data_size, Z_WAITOK);
	if (data == NULL) {
		vnode_put(vnode);
		return NULL;
	}
	error = vn_rdwr(UIO_READ, vnode, (caddr_t)data, (int)attributes.va_data_size, 0, UIO_SYSSPACE, 0,
	    vfs_context_ucred(context), &left, vfs_context_proc(context));
	vnode_put(vnode);
	if (error != 0 || left != 0) {
		printf("slpi: m2: %s: read error %d, %d bytes left\n", path, error, left);
		kfree_data(data, attributes.va_data_size);
		return NULL;
	}
	*size = attributes.va_data_size;
	return data;
}

static void
image_forget(void)
{
	if (image.header != NULL) {
		uint8_t *gone = __DECONST(uint8_t *, image.header);

		kfree_data(gone, image.header_size);
	}
	bzero(&image, sizeof(image));
}

/*
 * slpi.mdt, checked against its own header the way the vendor's loader checks it
 * (peripheral-loader.c:1372-1400, mdt_loader.c:120-160): the ELF mark, the program headers
 * inside the file, every loadable segment (PT_LOAD, not the HASH one, with a memsz) inside the
 * region once shifted, and the entry inside one of them. The shift is the vendor's: every
 * segment here carries bit 27 of its p_flags, so the image moves to the region's base and every
 * address, the entry's with them, goes up by base minus the lowest segment's p_paddr. A segment
 * whose filesz is 0 has no file and is still written, all zeroes (mdt_loader.c:177-189).
 */
static bool
mdt_parse(void)
{
	const uint8_t *header;
	uint64_t size = 0;
	uint32_t phoff, phnum;

	header = root_file("slpi.mdt", &size);
	if (header == NULL) {
		printf("slpi: m2: /fw/slpi.mdt is not there: the system image has no SLPI firmware, "
		    "or the root never came up\n");
		return false;
	}
	if (size < 52 || memcmp(header, "\177ELF\1", 5) != 0 || header[4] != 1 || header[5] != 1) {
		printf("slpi: m2: slpi.mdt is not a 32-bit little-endian ELF header (%llu bytes)\n",
		    (unsigned long long) size);
		uint8_t *gone = __DECONST(uint8_t *, header);

		kfree_data(gone, size);
		return false;
	}
	phoff = header[28] | header[29] << 8 | header[30] << 16 | (uint32_t) header[31] << 24;
	phnum = header[44] | header[45] << 8;
	if (phnum == 0 || phnum > SLPI_SEGMENT_FILES || phoff < 52 || phoff - 52 + 32 * (uint64_t) phnum > size) {
		printf("slpi: m2: slpi.mdt: %u program headers at %#x are not inside %llu bytes\n",
		    phnum, phoff, (unsigned long long) size);
		uint8_t *gone = __DECONST(uint8_t *, header);

		kfree_data(gone, size);
		return false;
	}
	image.header = header;
	image.header_size = size;
	image.base = SLPI_MEMORY;
	image.size = SLPI_IMAGE_SIZE;
	image.lowest = 0xffffffff;
	PE_parse_boot_argn("slpi_base", &image.base, sizeof(image.base));
	image.shift = image.base - SLPI_LOWEST_SEGMENT;

	for (unsigned ph = 0; ph < phnum; ph++) {
		const uint8_t *entry = header + phoff + 32 * ph;
		uint32_t type = entry[0] | entry[1] << 8 | entry[2] << 16 | (uint32_t) entry[3] << 24;
		uint32_t offset = entry[4] | entry[5] << 8 | entry[6] << 16 | (uint32_t) entry[7] << 24;
		uint32_t paddr = entry[12] | entry[13] << 8 | entry[14] << 16 | (uint32_t) entry[15] << 24;
		uint32_t filesz = entry[16] | entry[17] << 8 | entry[18] << 16 | (uint32_t) entry[19] << 24;
		uint32_t memsz = entry[20] | entry[21] << 8 | entry[22] << 16 | (uint32_t) entry[23] << 24;
		uint32_t flags = entry[24] | entry[25] << 8 | entry[26] << 16 | (uint32_t) entry[27] << 24;

		if (type == 0 && (flags >> 24 & 7) == 2) {
			uint64_t hashes_end = (uint64_t) offset + filesz;

			/*
			 * The file ends before this segment does, and that is how these mdt files are
			 * shipped: the file is the header, the program headers and the hash table, and
			 * the table's declared length reaches further than the file. TrustZone reads it
			 * out of the buffer qcom_scm_pas_init_image() builds -- a dma_alloc_coherent()
			 * of fw->size, which the allocator rounds up and zeroes, so past the file are
			 * zeroes. Our page is that buffer: one 16 KiB page, cleared, the file copied in.
			 * The one thing to keep is that everything TrustZone can read is in the page.
			 */
			printf("slpi: m2: slpi.mdt: ph%u is the hash segment: %llu bytes at %#llx, ending "
			    "at %#llx; the file is %llu bytes and the page TrustZone reads is %#lx\n", ph,
			    (unsigned long long) filesz, (unsigned long long) offset,
			    (unsigned long long) hashes_end, (unsigned long long) size,
			    (unsigned long) PAGE_SIZE);
			if (hashes_end > PAGE_SIZE) {
				printf("slpi: m2: the hashes end past the page TrustZone would read: "
				    "stopped\n");
				image_forget();
				return false;
			}
		}
		if (type != 1 || (flags >> 24 & 7) == 2 || memsz == 0) {
			continue;                                /* not loadable: segment_is_loadable() */
		}
		if (!(flags & (1u << 27))) {
			printf("slpi: m2: ph%u at %#x is not marked relocatable, and this driver only "
			    "knows how to move an image that is: stopped\n", ph, paddr);
			image_forget();
			return false;
		}
		if (paddr < image.lowest) {
			image.lowest = paddr;
		}
		if (paddr + memsz > image.highest) {
			image.highest = paddr + memsz;
		}
		if (image.segments == SLPI_SEGMENT_FILES) {
			printf("slpi: m2: more loadable segments than %u: stopped\n", SLPI_SEGMENT_FILES);
			image_forget();
			return false;
		}
		struct slpi_segment *seg = &image.segment[image.segments++];

		seg->index = ph;
		seg->at = paddr - SLPI_LOWEST_SEGMENT;   /* the vendor's offset from the region's base */
		seg->file_size = filesz;
		seg->memory_size = memsz;
	}
	if (image.segments == 0 || image.lowest > image.highest || image.highest <= image.lowest) {
		printf("slpi: m2: slpi.mdt has no loadable segments\n");
		image_forget();
		return false;
	}
	if (image.lowest != SLPI_LOWEST_SEGMENT) {
		printf("slpi: m2: the lowest segment is at %#x, not the %#x the shift was computed "
		    "from: stopped\n", image.lowest, SLPI_LOWEST_SEGMENT);
		image_forget();
		return false;
	}
	if ((uint64_t) image.highest - image.lowest > image.size) {
		printf("slpi: m2: the image spans %#llx bytes and the region holds %#x: stopped\n",
		    (unsigned long long) image.highest - image.lowest, image.size);
		image_forget();
		return false;
	}
	for (unsigned s = 0; s < image.segments; s++) {
		struct slpi_segment *seg = &image.segment[s];

		if (seg->at + seg->memory_size > image.size) {
			printf("slpi: m2: ph%u at %#x for %u bytes is outside the region: stopped\n",
			    seg->index, image.base + seg->at, seg->memory_size);
			image_forget();
			return false;
		}
	}
	image.entry = header[24] | header[25] << 8 | header[26] << 16 | (uint32_t) header[27] << 24;
	image.relocated_entry = image.entry - image.lowest + image.base;   /* absolute, like the segments' */
	{
		bool in_one = false;

		for (unsigned s = 0; s < image.segments; s++) {
			if (image.relocated_entry >= image.base + image.segment[s].at &&
			    image.relocated_entry < image.base + image.segment[s].at + image.segment[s].memory_size) {
				in_one = true;
			}
		}
		if (!in_one) {
			printf("slpi: m2: the entry %#x, moved to %#x, is in no segment: stopped\n",
			    image.entry, image.relocated_entry);
			image_forget();
			return false;
		}
	}
	printf("slpi: m2: slpi.mdt: %llu bytes, %u program headers, %u loadable segments spanning %#x to "
	    "%#x; the entry %#x moves to %#x, the first segment's first byte; the image moves by %#x into "
	    "%#x for %#x, its span exactly\n", (unsigned long long) size, phnum, image.segments,
	    image.lowest, image.highest, image.entry, image.relocated_entry, image.shift, image.base,
	    image.size);
	return true;
}

/* ---- the doorbell, and the mailbox to the always-on processor -------------------------------------- */

/* Linux's qmp_open() and qmp_send(), the same words and order the ADSP's driver uses
 * (hakios.adsp.c:598-778); the box and its size come from the always-on processor's own
 * registers, and the lock is the one that driver and the Wi-Fi's start share. The doorbell is
 * not IPCC: the ssc node's mailbox is qcom,qmp-aop@c300000, whose second reg is an irq register
 * at 0x17c000c with qcom,irq-mask = <1>, and the vendor's send_irq() is one write of the mask
 * there (msm_qmp.c:219-227). */
#define QMP_MAGIC               0x00
#define QMP_MAGIC_VALUE         0x4d41494c
#define QMP_VERSION             0x04
#define QMP_THEIR_LINK          0x0c
#define QMP_THEIR_LINK_ACK      0x10
#define QMP_THEIR_CHANNEL       0x14
#define QMP_THEIR_CHANNEL_ACK   0x18
#define QMP_OUR_LINK            0x24
#define QMP_OUR_LINK_ACK        0x28
#define QMP_OUR_CHANNEL         0x2c
#define QMP_OUR_CHANNEL_ACK     0x30
#define QMP_OUR_BOX_SIZE        0x34
#define QMP_OUR_BOX             0x38
#define QMP_UP                  0x0000ffffu
#define QMP_DOWN                0xffff0000u
#define QMP_MESSAGE             64

#define QMP_IRQ_REG             0x17c0000c              /* qcom,qmp-aop's irq-reg-base: 0x17C0000C, not 0x017C000C */
#define QMP_IRQ_MASK            0x01                    /* qcom,irq-mask */

static struct block {
	const char *name;
	uint32_t phys, size;
	vm_offset_t base;
} aoss = { "mailbox", 0x0c300000, 0x400 }, irq = { "aop irq", QMP_IRQ_REG, 4 };

static uint32_t our_box;                /* where in the message memory our mailbox is; 0: not open */

static void
need(struct block *b)
{
	if (b->base == 0) {
		b->base = ml_io_map(b->phys, b->size);
	}
}

static uint32_t
get(struct block *b, const char *what, uint32_t offset)
{
	need(b);
	if (b->base == 0) {
		printf("slpi: m2: %s: could not map %s at %#x\n", b->name, what, b->phys);
		return 0;
	}
	return rd(b->base + offset);
}

static void
put(struct block *b, const char *what, uint32_t offset, uint32_t value)
{
	need(b);
	if (b->base == 0) {
		printf("slpi: m2: %s: %s at %#x is not mapped: %#x not written\n", b->name, what, b->phys,
		    value);
		return;
	}
	wr(b->base + offset, value);
}

static bool
mailbox_wait(uint32_t offset, uint32_t value, uint32_t ms)
{
	uint64_t interval = 0;
	uint64_t deadline;

	/*
	 * The deadline is time, not iterations: delay_for_interval() blocks on this port, and the
	 * scheduler's tick is about 7.5 ms, so a 100 us sleep costs a whole tick and ten thousand of
	 * them cost 75 seconds. logs/121318-run.log shows what that did: the wait the driver calls
	 * "one second" finished at 77 s of kernel time, and the loader's watchdog had already called
	 * the run hung in the earlier rounds. One tick per poll, against mach_absolute_time(), is
	 * what a second means here.
	 */
	clock_interval_to_absolutetime_interval(ms, NSEC_PER_MSEC, &interval);
	deadline = mach_absolute_time() + interval;
	for (;;) {
		if (rd(aoss.base + offset) == value) {
			return true;
		}
		if (mach_absolute_time() >= deadline) {
			return rd(aoss.base + offset) == value;
		}
		delay_for_interval(1, NSEC_PER_MSEC);
	}
}

static void
ring_aop(void)
{
	wr(irq.base, QMP_IRQ_MASK);
}

static bool
mailbox_open(void)
{
	uint32_t box, size;

	/*
	 * The always-on processor answers a link change with its own interrupt: the qmp-aop node's
	 * interrupts = <0 0x185 1>, SPI 389. Nothing in this kernel has a handler for it, and the
	 * boot chain may have left the line enabled in the GIC -- then the reply is a storm on this
	 * processor, which is exactly the shape the runs show: the log stops at the write that raises
	 * the link, while other processors go on for a few seconds (logs/120347-run.log). Mask it;
	 * the link's state is read out of the msgram, so the interrupt is not needed.
	 */
	gic_v3_spi_mask(389);

	/*
	 * The doorbell's block, one register at a time, before anything is written to it: the runs of
	 * 2026-11-03 show the write to 0x17c000c never completing, and a stall in one of these reads
	 * says whether the whole block is unclocked or only that register is not the one to write.
	 */
	{
		vm_offset_t apcs = ml_io_map(0x17c00000, 0x1000);

		if (apcs == 0) {
			printf("slpi: m2: apcs: the block at 0x17c00000 does not map\n");
		} else {
			printf("slpi: m2: apcs: the block at 0x17c00000 maps; reading it one word at a time\n");
			printf("slpi: m2: apcs: +0x0 reads %#x\n", rd(apcs + 0x00));
			printf("slpi: m2: apcs: +0x4 reads %#x\n", rd(apcs + 0x04));
			printf("slpi: m2: apcs: +0x8 reads %#x\n", rd(apcs + 0x08));
			printf("slpi: m2: apcs: +0xc reads %#x\n", rd(apcs + 0x0c));
		}
	}

	need(&aoss);
	need(&irq);
	if (get(&aoss, "the mailbox memory", QMP_MAGIC) != QMP_MAGIC_VALUE ||
	    get(&aoss, "the mailbox memory", QMP_VERSION) != 1) {
		printf("slpi: m2: not a mailbox this knows\n");
		return false;
	}
	printf("slpi: m2: mailbox: magic %#x, version %u, features %#x; theirs link %#x ack %#x "
	    "channel %#x ack %#x; ours link %#x ack %#x channel %#x ack %#x; our box %u bytes at %#x\n",
	    rd(aoss.base + QMP_MAGIC), rd(aoss.base + QMP_VERSION), rd(aoss.base + 0x08),
	    rd(aoss.base + QMP_THEIR_LINK), rd(aoss.base + QMP_THEIR_LINK_ACK),
	    rd(aoss.base + QMP_THEIR_CHANNEL), rd(aoss.base + QMP_THEIR_CHANNEL_ACK),
	    rd(aoss.base + QMP_OUR_LINK), rd(aoss.base + QMP_OUR_LINK_ACK),
	    rd(aoss.base + QMP_OUR_CHANNEL), rd(aoss.base + QMP_OUR_CHANNEL_ACK),
	    rd(aoss.base + QMP_OUR_BOX_SIZE), rd(aoss.base + QMP_OUR_BOX));
	box = rd(aoss.base + QMP_OUR_BOX);
	size = rd(aoss.base + QMP_OUR_BOX_SIZE);
	/* (box and size come from the always-on processor: "box + size" could wrap in 32 bits and pass, so it is compared as a difference) */
	if (size < QMP_MESSAGE + 4 || box < 0x3c || box >= aoss.size || size > aoss.size - box || (box & 3)) {
		printf("slpi: m2: our mailbox is not where a message fits\n");
		return false;
	}
	if (rd(aoss.base + QMP_OUR_LINK) == QMP_UP && rd(aoss.base + QMP_OUR_LINK_ACK) == QMP_UP &&
	    rd(aoss.base + QMP_OUR_CHANNEL) == QMP_UP && rd(aoss.base + QMP_THEIR_CHANNEL) == QMP_UP &&
	    rd(aoss.base + QMP_OUR_CHANNEL_ACK) == QMP_UP) {
		printf("slpi: m2: the mailbox is open already (the ADSP's start, or this same boot)\n");
		our_box = box;
		return true;
	}
	/* Each step is said and read back before the next: if the run ends inside this handshake, the
	 * last line names the register that did not answer (logs/112421-run.log stopped right after
	 * the dump, inside one of these accesses, and the whole phone went with it). */
	printf("slpi: m2: mailbox: their link %#x; acknowledging it in %#x\n",
	    rd(aoss.base + QMP_THEIR_LINK), QMP_THEIR_LINK_ACK);
	put(&aoss, "their link, seen by us", QMP_THEIR_LINK_ACK, rd(aoss.base + QMP_THEIR_LINK));
	/* Our whole half, the way init_mcore_state() writes it (msm_qmp.c:498-512): the link up, its
	 * ack down, both channel words disconnected, then the box the processor published. */
	printf("slpi: m2: mailbox: writing our half: link up, ack down, channels disconnected, box %u at %#x\n",
	    size, box);
	put(&aoss, "our link", QMP_OUR_LINK, QMP_UP);
	put(&aoss, "our link ack", QMP_OUR_LINK_ACK, QMP_DOWN);
	put(&aoss, "our channel", QMP_OUR_CHANNEL, QMP_DOWN);
	put(&aoss, "our channel ack", QMP_OUR_CHANNEL_ACK, QMP_DOWN);
	put(&aoss, "our box size", QMP_OUR_BOX_SIZE, size);
	put(&aoss, "our box", QMP_OUR_BOX, box);
	printf("slpi: m2: mailbox: our half reads link %#x ack %#x channel %#x ack %#x, box %u at %#x\n",
	    rd(aoss.base + QMP_OUR_LINK), rd(aoss.base + QMP_OUR_LINK_ACK),
	    rd(aoss.base + QMP_OUR_CHANNEL), rd(aoss.base + QMP_OUR_CHANNEL_ACK),
	    rd(aoss.base + QMP_OUR_BOX_SIZE), rd(aoss.base + QMP_OUR_BOX));
	/* No doorbell first: if the always-on processor polls the msgram, its ack comes without one,
	 * and the APCS register is never touched. */
	if (mailbox_wait(QMP_OUR_LINK_ACK, QMP_UP, 1000)) {
		printf("slpi: m2: mailbox: it acknowledged our link without a doorbell\n");
	} else {
		printf("slpi: m2: mailbox: no ack in a second; ringing the doorbell at %#x (write-only: it "
		    "is never read)\n", irq.phys);
		ring_aop();
		printf("slpi: m2: mailbox: rung; waiting again\n");
		if (!mailbox_wait(QMP_OUR_LINK_ACK, QMP_UP, 1000)) {
			printf("slpi: m2: THE ALWAYS-ON PROCESSOR DOES NOT ACKNOWLEDGE OUR LINK within a "
			    "second; taken down again\n");
			put(&aoss, "our link", QMP_OUR_LINK, QMP_DOWN);
			ring_aop();
			return false;
		}
	}
	put(&aoss, "our channel", QMP_OUR_CHANNEL, QMP_UP);
	ring_aop();
	if (!mailbox_wait(QMP_THEIR_CHANNEL, QMP_UP, 1000)) {
		printf("slpi: m2: THE ALWAYS-ON PROCESSOR DOES NOT OPEN ITS CHANNEL within a second; ours "
		    "is taken down again\n");
		put(&aoss, "our channel", QMP_OUR_CHANNEL, QMP_DOWN);
		put(&aoss, "our link", QMP_OUR_LINK, QMP_DOWN);
		ring_aop();
		return false;
	}
	put(&aoss, "their channel, seen by us", QMP_THEIR_CHANNEL_ACK, QMP_UP);
	ring_aop();
	if (!mailbox_wait(QMP_OUR_CHANNEL_ACK, QMP_UP, 1000)) {
		printf("slpi: m2: THE ALWAYS-ON PROCESSOR DOES NOT ACKNOWLEDGE OUR CHANNEL within a second; "
		    "taken down again\n");
		put(&aoss, "our channel", QMP_OUR_CHANNEL, QMP_DOWN);
		put(&aoss, "our link", QMP_OUR_LINK, QMP_DOWN);
		ring_aop();
		return false;
	}
	printf("slpi: m2: THE MAILBOX TO THE ALWAYS-ON PROCESSOR IS OPEN\n");
	our_box = box;
	return true;
}

static bool
mailbox_send(const char *text)
{
	uint32_t words[QMP_MESSAGE / 4] = { 0 };
	bool sent = false;

	lck_mtx_lock(&hakios_aop_mailbox_lock);
	if (our_box == 0 && !mailbox_open()) {
		goto out;
	}
	strlcpy((char *) words, text, sizeof(words));
	printf("slpi: m2: about to tell the always-on processor \"%s\"\n", text);
	if (rd(aoss.base + our_box) != 0) {
		printf("slpi: m2: our mailbox still holds a message of %u bytes: not sent\n",
		    rd(aoss.base + our_box));
		goto out;
	}
	for (unsigned i = 0; i < QMP_MESSAGE / 4; i++) {
		wr(aoss.base + our_box + 4 + 4 * i, words[i]);
	}
	wr(aoss.base + our_box, QMP_MESSAGE);
	(void) rd(aoss.base + our_box);
	ring_aop();
	if (!mailbox_wait(our_box, 0, 1000)) {
		printf("slpi: m2: THE ALWAYS-ON PROCESSOR DOES NOT TAKE THE MESSAGE within a second; it is "
		    "taken back\n");
		wr(aoss.base + our_box, 0);
		goto out;
	}
	printf("slpi: m2: the always-on processor has taken it\n");
	sent = true;
out:
	lck_mtx_unlock(&hakios_aop_mailbox_lock);
	return sent;
}

/* ---- TrustZone -------------------------------------------------------------------------------------- */

#define SCM_64                  (1ull << 30)
#define SCM_SIP                 (2ull << 24)
#define SCM_CALL(service, command) ((uint64_t)(service) << 8 | (command))
#define SCM_PAS_INIT_IMAGE      SCM_CALL(2, 1)
#define SCM_PAS_MEM_SETUP       SCM_CALL(2, 2)
#define SCM_PAS_AUTH_AND_RESET  SCM_CALL(2, 5)
#define SCM_PAS_SHUTDOWN        SCM_CALL(2, 6)
#define SCM_ONE_VALUE           1
#define SCM_TWO_VALUES          2
#define SCM_THREE_VALUES        3
#define SCM_VALUE_AND_BUFFER    (2 | 2 << 6)
#define SCM_INTERRUPTED         1

static int32_t
scm(const char *what, uint64_t function, uint64_t arguments, uint64_t first, uint64_t second, uint64_t third,
    uint64_t *answer)
{
	uint64_t session = 0;

	function |= SCM_64 | SCM_SIP;
	printf("slpi: m2: about to ask TrustZone %s: smc %#llx, arguments %#llx, %#llx, %#llx\n", what,
	    function, first, second, third);
	for (int i = 0; i < 100; i++) {
		register uint64_t x0 __asm__("x0") = i == 0 ? function : SCM_INTERRUPTED;
		register uint64_t x1 __asm__("x1") = arguments;
		register uint64_t x2 __asm__("x2") = first;
		register uint64_t x3 __asm__("x3") = second;
		register uint64_t x4 __asm__("x4") = third;
		register uint64_t x5 __asm__("x5") = 0;
		register uint64_t x6 __asm__("x6") = session;
		register uint64_t x7 __asm__("x7") = 0;

		__asm__ volatile ("smc #0" : "+r" (x0), "+r" (x1), "+r" (x2), "+r" (x3), "+r" (x4), "+r" (x5),
		    "+r" (x6), "+r" (x7) : : "x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17",
		    "memory");
		session = x6;
		if (x0 != SCM_INTERRUPTED) {
			printf("slpi: m2: TrustZone returns %d, answer %#llx\n", (int32_t) x0, x1);
			*answer = x1;
			return (int32_t) x0;
		}
	}
	printf("slpi: m2: TrustZone keeps saying the call was interrupted\n");
	return -1;
}

/* Where the image's zeroes are, and what is read of them after the start: M3b, below. */
static void region_note_zeroes(void);
static void slpi_region_watch(void);

/* ---- M2: putting the image in the region, and starting ---------------------------------------------- */

/*
 * The region, written and read back a word at a time: the same method the ADSP's driver proved
 * on this kind of memory (hakios.adsp.c step_image()). The whole region is zeroed first, so a
 * segment longer than its file, and the 3.5 MiB of memory the last segment's file says nothing
 * about, are zeroes without any further thought; each segment's file is read only when its turn
 * comes and given back as soon as it is written and read back, which is why this needs no more
 * memory than one segment at a time. The region's base, the shift and every segment's paddr are
 * multiples of eight, checked above, so every write lands on a whole word.
 */
static bool
segments_write(void)
{
	volatile uint64_t *memory = (volatile uint64_t *) ml_io_map_unmappable((vm_offset_t) image.base,
	    (vm_size_t) image.size, VM_WIMG_WCOMB);
	uint64_t wrote = 0, wrong = 0;

	if (memory == NULL) {
		printf("slpi: m2: could not map the region at %#x\n", image.base);
		return false;
	}
	printf("slpi: m2: clearing %#x bytes at %#x\n", image.size, image.base);
	for (uint32_t at = 0; at < image.size; at += 8) {
		memory[at / 8] = 0;
	}
	settle();
	for (unsigned s = 0; s < image.segments; s++) {
		struct slpi_segment *seg = &image.segment[s];
		volatile uint64_t *to = memory + seg->at / 8;
		const uint8_t *file = NULL;
		uint64_t size = 0;
		char name[24];

		if (seg->file_size != 0) {
			snprintf(name, sizeof(name), "slpi.b%02u", seg->index);
			file = root_file(name, &size);
			if (file == NULL || size != seg->file_size) {
				printf("slpi: m2: /fw/%s: %s (%llu bytes, the header promised %u)\n", name,
				    file == NULL ? "is not there" : "is not the size the header promised",
				    (unsigned long long) size, seg->file_size);
				if (file != NULL) {
					uint8_t *gone = __DECONST(uint8_t *, file);

					kfree_data(gone, size);
				}
				ml_io_unmap((vm_offset_t) memory, (vm_size_t) image.size);
				return false;
			}
		}
		for (uint32_t i = 0; i < seg->memory_size; i += 8) {
			uint64_t word = 0;

			if (file != NULL) {
				for (unsigned b = 0; b < 8 && i + b < seg->file_size; b++) {
					word |= (uint64_t) file[i + b] << (8 * b);
				}
			}
			to[i / 8] = word;
			wrote += 8;
		}
		settle();
		for (uint32_t i = 0; i < seg->memory_size; i += 8) {
			uint64_t word = 0;

			if (file != NULL) {
				for (unsigned b = 0; b < 8 && i + b < seg->file_size; b++) {
					word |= (uint64_t) file[i + b] << (8 * b);
				}
			}
			wrong += to[i / 8] != word;
		}
		if (file != NULL) {
			uint8_t *gone = __DECONST(uint8_t *, file);

			kfree_data(gone, size);
		}
		printf("slpi: m2: ph%02u: %u bytes of memory at %#x%s, read back %s\n", seg->index,
		    seg->memory_size, image.base + seg->at, seg->file_size == 0 ? ", no file, all zeroes" :
		    "", wrong == 0 ? "exactly as written" : "with differences");
		if (wrong != 0) {
			break;
		}
	}
	ml_io_unmap((vm_offset_t) memory, (vm_size_t) image.size);
	if (wrong != 0) {
		printf("slpi: m2: %llu words of the image did not read back as they were written: stopped\n",
		    (unsigned long long) wrong);
		return false;
	}
	printf("slpi: m2: the image is in place: %llu bytes written at %#x, every word read back\n",
	    (unsigned long long) wrote, image.base);
	region_note_zeroes();           /* before the image is forgotten: it is the map of what is zeroes */
	image.written = true;
	return true;
}

static uint32_t slpi_level;             /* what the boot argument asked for, kept for the thread */
static void slpi_glink_clean(void);     /* defined with the glink code below; slpi=10 calls it here */

/*
 * The start itself, in the vendor's pil_boot() order: "on" to the always-on processor first, because
 * that is pil_boot()'s first act and it returns if the message does not go (peripheral-loader.c:1356);
 * then the mdt's header in a page of ours for TrustZone to check the signature, the region to
 * TrustZone, the segments, and the call that authenticates the image and lets the subsystem out of
 * reset. Any failure after "on" gets "off" back, which is what pil_boot()'s single exit does
 * (peripheral-loader.c:1526) -- not only a failed auth_and_reset.
 */
static bool
slpi_start(void)
{
	vm_offset_t page = 0;
	uint64_t answer = ~0ull;
	int32_t result;
	bool ok = false;
	bool told = false;          /* the AOP has heard "on": every failure from now on owes it "off" */

	if (slpi_level != 5) {
		lck_mtx_lock(&hakios_aop_mailbox_lock);  /* the protocol every user of this mailbox keeps */
		if (!mailbox_open()) {
			lck_mtx_unlock(&hakios_aop_mailbox_lock);
			return false;
		}
		lck_mtx_unlock(&hakios_aop_mailbox_lock);

		/* pil_boot()'s first act, and it returns if the message does not go: the AOP hears "on"
		 * before TrustZone is asked anything (peripheral-loader.c:1356).
		 *
		 * "off" first, because the AOP's state survives an AP reboot: a run before this one
		 * never sent it (our runs never reach a shutdown), so the processor still believes the
		 * SLPI is loaded, and a second "on" changes nothing -- including, if a vote for the
		 * subsystem's domain is what matters, the vote it already made. The vendor's
		 * pil_shutdown() sends this "off"; a clean off-then-on is the state the processor
		 * expects. What the processor does with either message is not visible from here:
		 * logs/123127-run.log took both, and the q6 block still did not answer, which was the
		 * test that seemed to settle it at the time and does not (see the header). */
		printf("slpi: m2: telling the always-on processor \"off\" first, so its state is a clean one\n");
		(void) mailbox_send("{class: image, res: load_state, name: slpi, val: off}");
		delay_for_interval(100, NSEC_PER_MSEC);
		if (!mailbox_send("{class: image, res: load_state, name: slpi, val: on}")) {
			return false;
		}
		told = true;
		printf("slpi: m2: the always-on processor is told the image is on its way\n");
	} else {
		/* slpi=5: the mailbox is left alone and TrustZone is asked directly. A diagnostic for
		 * when the mailbox itself is the thing that stops the run (logs/120347-run.log). It is
		 * not what the phone's own kernel does: the ssc node's qcom,signal-aop is this message
		 * over this mailbox -- its mboxes = <0x1f 0x00>, mbox-names "slpi-pil", and phandle 0x1f
		 * is the qcom,qmp-aop@c300000 node (android-merged.dts:3343-3344, :4285-4296) -- and
		 * pil_notify_aop() sends exactly this text, "on" as pil_boot()'s first act and "off" on
		 * every failure after it and at a shutdown (peripheral-loader.c:1238-1253, :1358, :1537,
		 * :1567). So slpi=6 asks for it back. */
		printf("slpi: m2: slpi=5: the always-on processor is not told; TrustZone is asked directly\n");
	}

	printf("slpi: m2: handing TrustZone the mdt's header, %llu bytes in a page of ours, to check "
	    "the signature\n", (unsigned long long) image.header_size);
	if (kmem_alloc(kernel_map, &page, PAGE_SIZE, KMA_KOBJECT | KMA_ZERO | KMA_DATA,
	    VM_KERN_MEMORY_IOKIT) != KERN_SUCCESS) {
		printf("slpi: m2: no page\n");
		goto out;
	}
	memcpy((void *) page, image.header, image.header_size);
	for (vm_offset_t line = page; line < page + PAGE_SIZE; line += 64) {
		__asm__ volatile ("dc civac, %0" : : "r" (line));
	}
	settle();
	result = scm("to take the slpi's header", SCM_PAS_INIT_IMAGE, SCM_VALUE_AND_BUFFER, PAS_SLPI,
	    kvtophys(page), 0, &answer);
	if (result != 0 || answer != 0) {
		printf("slpi: m2: TRUSTZONE DOES NOT TAKE THE HEADER: the call returns %d, its answer is "
		    "%#llx\n", result, answer);
		goto out;
	}
	printf("slpi: m2: TRUSTZONE ACCEPTS THE SLPI'S SIGNATURE FROM THIS KERNEL\n");

	result = scm("where the image will be", SCM_PAS_MEM_SETUP, SCM_THREE_VALUES, PAS_SLPI, image.base,
	    image.size, &answer);
	if (result != 0 || answer != 0) {
		printf("slpi: m2: TRUSTZONE DOES NOT TAKE THE PLACE: the call returns %d, its answer is "
		    "%#llx\n", result, answer);
		goto out;
	}
	printf("slpi: m2: TrustZone takes the place: %#x bytes at %#x\n", image.size, image.base);

	if (!segments_write()) {
		goto out;
	}
	if (slpi_level == 10 || slpi_level == 13 || slpi_level == 14 || slpi_level == 15 ||
	    slpi_level == 16 || slpi_level == 17 || slpi_level == 18 || slpi_level == 19 ||
	    slpi_level == 20 || slpi_level == 21) {
		/* the old instance is off and the new one is still in reset: this is the only
		 * window in which the edge can be wiped so that a fresh instance sees it
		 * empty; m13, m14, m15 and m16 start a fresh instance exactly the way m10 does */
		slpi_glink_clean();
	}
	result = scm("to authenticate the slpi's image and start it", SCM_PAS_AUTH_AND_RESET, SCM_ONE_VALUE,
	    PAS_SLPI, 0, 0, &answer);
	if (result != 0 || answer != 0) {
		printf("slpi: m2: TRUSTZONE DOES NOT START THE SLPI: the call returns %d, its answer is "
		    "%#llx\n", result, answer);
		goto out;
	}
	printf("slpi: m2: TRUSTZONE HAS AUTHENTICATED THE IMAGE AND LET THE SLPI OUT OF RESET\n");
	ok = true;
out:
	if (!ok && told) {
		/* pil_boot()'s single exit sends this on any failure after "on" (peripheral-loader.c:1526) */
		mailbox_send("{class: image, res: load_state, name: slpi, val: off}");
	}
	if (page != 0) {
		kmem_free(kernel_map, page, PAGE_SIZE);
	}
	return ok;
}

/* ---- M3: the subsystem's own word, its SMP2P item ------------------------------------------------ */

/*
 * The SLPI's SMP2P. The vendor tree's node is qcom,smp2p-dsps@1799000c: qcom,smem = <0x1e1 0x1ae>,
 * qcom,local-pid = 0, qcom,remote-pid = 3 (android-merged.dts:16767-16786). The in item, 481
 * (0x1e1), is the one the SLPI writes; its "slave-kernel" entry (phandle 0x7b) carries the four
 * bits the ssc node names in its interrupts-extended: 0 err-fatal, 1 err-ready, 2 proxy-unvote,
 * 3 stop-ack (android-merged.dts:3341-3342). The shared memory window on this phone is 0x86000000,
 * 2 MiB (android-merged.dts:22804-22806; devices/pclm10/soc.args), not the reference device's
 * 0x80900000 that hakios.adsp.c still carries as its default.
 *
 * The item is a 20-byte SMP2P header and then 20-byte entries, a 16-byte name and a 32-bit value
 * (vendor-src/soc/smp2p.c:75-89): the value of entry i sits at the item's offset 0x24 + 20 i. The
 * item exists only after the SLPI's own SMP2P driver has run, so a failure is looked at afresh every
 * poll rather than cached.
 *
 * The out item, 430 (0x1ae), is the one the SLPI reads and we would write. In the vendor tree the
 * AP's own driver makes it before the remote's probe can finish: qcom_smp2p_alloc_outbound_item()
 * calls qcom_smem_alloc(pid, 430, sizeof(*out)) and then rewrites the header — "$SMP", version 1,
 * local 0, remote 3, total 16, valid 0 — and kicks the doorbell (vendor-src/soc/smp2p.c:485-513).
 * Our kernel has never allocated anything, so whether 430 exists at all is the first thing M3 has
 * to look at, and slpi_smem_walk() below is the read that answers it.
 */
#define SMEM_PHYS_DEFAULT       0x86000000UL
#define SMEM_SIZE               0x00200000u
#define SMEM_US                 0
#define SMEM_SLPI               3
#define SMEM_GLOBAL             0xfffe
#define SMEM_CANARY             0xa5a5
#define ITEM_SLPI_SMP2P         481
#define ITEM_SLPI_SMP2P_OUT     430                             /* 0x1ae: the item the SLPI reads */
#define ITEM_SLPI_SFR           0x1a8                           /* 424: the ssc node's qcom,smem-id, android-merged.dts:3337 */
#define SMP2P_MAGIC             0x504d5324u            /* "$SMP", little-endian */
#define SMP2P_FATAL             0
#define SMP2P_READY             1
#define SMP2P_HANDOVER          2
#define SMP2P_STOP_ACK          3
/* The item flags' two protocol bits, from the vendor's driver: check_ssr() watches the inbound
 * item's restart-done and do_ssr_ack() answers by toggling the outbound item's restart-ack
 * (vendor-src/soc/smp2p.c:54-55, 197-224). Both sides advertise the ack with features 1, and
 * the slpi's own header says features 1 (logs/024654-run.log read it in item 481). */
#define SMP2P_FLAGS_RESTART_DONE    0
#define SMP2P_FLAGS_RESTART_ACK     1

struct smem_table_entry {
	uint32_t offset, size, flags;
	uint16_t host0, host1;
	uint32_t cacheline, reserved[7];
};
struct smem_table {
	uint8_t magic[4];
	uint32_t version, count, reserved[5];
};
struct smem_partition {
	uint8_t magic[4];
	uint16_t host0, host1;
	uint32_t size, free_uncached, free_cached, reserved[3];
};
struct smem_entry {
	uint16_t canary, item;
	uint32_t size;
	uint16_t padding_data, padding_header;
	uint32_t reserved;
};
struct smem_item {
	uint32_t item, offset, size;
};
struct smp2p_header {
	uint32_t magic;
	uint8_t version, features[3];
	uint16_t local_pid, remote_pid;
	uint16_t total_entries, valid_entries;
	uint32_t flags;
};

/* Not const from here on: M3 writes one item into it. The mapping is write-combining, so a
 * caller that writes and then rings a doorbell has to settle() between the two: the dsb is what
 * carries the combined writes out, and the vendor's driver does the same with its wmb(). */
static volatile uint8_t *smem;
static uint64_t smem_phys = SMEM_PHYS_DEFAULT;

static bool
smem_peek(uint64_t offset, void *to, uint32_t size)
{
	if (smem == NULL) {
		PE_parse_boot_argn("soc_smem", &smem_phys, sizeof(smem_phys));
		smem = (volatile uint8_t *) ml_io_map_unmappable((vm_offset_t) smem_phys, SMEM_SIZE, VM_WIMG_WCOMB);
	}
	if (smem == NULL || offset + size > SMEM_SIZE) {
		return false;
	}
	for (uint32_t i = 0; i < size; i++) {
		((uint8_t *) to)[i] = smem[offset + i];
	}
	return true;
}

static uint32_t
smem_partition(uint16_t one, uint16_t other, uint32_t *cacheline, uint32_t *size)
{
	struct smem_table table;
	struct smem_table_entry entry;
	struct smem_partition header;

	if (!smem_peek(SMEM_SIZE - 0x1000, &table, sizeof(table)) || memcmp(table.magic, "$TOC", 4) != 0 ||
	    table.version != 1 || table.count > 64) {
		return 0;
	}
	for (uint32_t i = 0; i < table.count; i++) {
		if (!smem_peek(SMEM_SIZE - 0x1000 + sizeof(table) + i * sizeof(entry), &entry, sizeof(entry)) ||
		    entry.offset == 0 || entry.size == 0) {
			continue;
		}
		if (!((entry.host0 == one && entry.host1 == other) ||
		    (entry.host0 == other && entry.host1 == one))) {
			continue;
		}
		if ((uint64_t) entry.offset + entry.size > SMEM_SIZE - 0x1000 ||
		    !smem_peek(entry.offset, &header, sizeof(header)) || memcmp(header.magic, "$PRT", 4) != 0 ||
		    header.size != entry.size) {
			return 0;
		}
		*cacheline = entry.cacheline;
		*size = entry.size;
		return entry.offset;
	}
	return 0;
}

static unsigned
smem_items(uint32_t partition, uint32_t cacheline, struct smem_item *found, unsigned max)
{
	struct smem_partition header;
	struct smem_entry entry;
	uint32_t at, step_back;
	unsigned count = 0;

	if (partition == 0 || !smem_peek(partition, &header, sizeof(header)) || header.free_uncached > header.size ||
	    header.free_cached > header.size) {
		return 0;
	}
	for (at = sizeof(header); at + sizeof(entry) <= header.free_uncached;) {
		if (!smem_peek(partition + at, &entry, sizeof(entry)) || entry.canary != SMEM_CANARY ||
		    entry.size > header.size) {
			break;
		}
		if (entry.padding_data > entry.size ||
		    (uint64_t) at + sizeof(entry) + entry.padding_header + entry.size > header.size) {
			break;
		}
		if (count < max) {
			found[count].item = entry.item;
			found[count].offset = partition + at + sizeof(entry) + entry.padding_header;
			found[count].size = entry.size - entry.padding_data;
		}
		count++;
		at += sizeof(entry) + entry.padding_header + entry.size;
	}
	if (cacheline == 0 || (cacheline & (cacheline - 1))) {
		return count;
	}
	step_back = ((uint32_t) sizeof(entry) + cacheline - 1) & ~(cacheline - 1);
	for (at = header.size - step_back; at > header.free_cached && at < header.size;) {
		if (!smem_peek(partition + at, &entry, sizeof(entry)) || entry.canary != SMEM_CANARY ||
		    entry.size > at || entry.padding_data > entry.size) {
			break;
		}
		if (count < max) {
			found[count].item = entry.item;
			found[count].offset = partition + at - entry.size;
			found[count].size = entry.size - entry.padding_data;
		}
		count++;
		if (entry.size + step_back > at) {
			break;
		}
		at -= entry.size + step_back;
	}
	return count;
}

/* An item the SLPI and we share: in the partition of the two, or else in the global one. Where it is; 0: nowhere. */
static uint32_t
smem_item(uint32_t item, uint32_t *size)
{
	static struct smem_item found[256];
	static const uint16_t hosts[2][2] = { { SMEM_US, SMEM_SLPI }, { SMEM_GLOBAL, SMEM_GLOBAL } };

	for (unsigned h = 0; h < 2; h++) {
		uint32_t cacheline = 0, partition_size = 0;
		uint32_t partition = smem_partition(hosts[h][0], hosts[h][1], &cacheline, &partition_size);
		unsigned count = smem_items(partition, cacheline, found, 256);

		for (unsigned i = 0; i < count && i < 256; i++) {
			if (found[i].item == item) {
				*size = found[i].size;
				return found[i].offset;
			}
		}
	}
	return 0;
}

/* The "slave-kernel" entry's value, and what it was worth: false if the item or the entry is not there. */
static bool
slpi_smp2p_state(uint32_t *value, const char **why)
{
	struct smp2p_header header;
	uint32_t size = 0;
	uint32_t at = smem_item(ITEM_SLPI_SMP2P, &size);

	*why = "no item";
	if (at == 0) {
		return false;
	}
	*why = "no header";
	if (!smem_peek(at, &header, sizeof(header))) {
		return false;
	}
	*why = "not an SMP2P item yet";
	if (header.magic != SMP2P_MAGIC || header.version != 1) {
		return false;
	}
	*why = "not the SLPI's item";
	if (header.local_pid != SMEM_SLPI || header.remote_pid != SMEM_US) {
		return false;
	}
	*why = "the entries do not fit";
	if (header.valid_entries > 16 || size < sizeof(header) + 20u * header.valid_entries) {
		return false;
	}
	*why = "no slave-kernel entry";
	for (unsigned i = 0; i < header.valid_entries; i++) {
		uint32_t entry = at + sizeof(header) + 20u * i;
		char name[16] = { 0 };
		uint32_t v = 0;

		if (!smem_peek(entry, name, sizeof(name)) || !smem_peek(entry + 16, &v, sizeof(v))) {
			return false;
		}
		if (memcmp(name, "slave-kernel", 12) == 0) {
			*value = v;
			*why = "the entry is there";
			return true;
		}
	}
	return false;
}

/*
 * Wait for the subsystem to say it is up: its "slave-kernel" entry's ready bit, or a no if it says
 * err-fatal first. The deadline is time, not iterations, for the reason mailbox_wait() has: a sleep
 * here is a scheduler tick, about 7.5 ms.
 */
static bool
slpi_wait_ready(uint32_t timeout_ms, uint32_t *last, const char **why)
{
	uint64_t interval = 0, deadline;

	clock_interval_to_absolutetime_interval(timeout_ms, NSEC_PER_MSEC, &interval);
	deadline = mach_absolute_time() + interval;
	for (;;) {
		uint32_t value = 0;

		if (slpi_smp2p_state(&value, why)) {
			*last = value;
			if (value & (1u << SMP2P_READY)) {
				return true;
			}
			if (value & (1u << SMP2P_FATAL)) {
				return false;
			}
		}
		if (mach_absolute_time() >= deadline) {
			return false;
		}
		delay_for_interval(1, NSEC_PER_MSEC);
	}
}

/* What the item holds, once, for the log: the header's own fields and the entries by name. */
static void
slpi_smp2p_show(void)
{
	struct smp2p_header header;
	uint32_t size = 0;
	uint32_t at = smem_item(ITEM_SLPI_SMP2P, &size);

	if (at == 0) {
		printf("slpi: smp2p: item %u is not in shared memory\n", ITEM_SLPI_SMP2P);
		return;
	}
	if (!smem_peek(at, &header, sizeof(header))) {
		printf("slpi: smp2p: item %u at smem+%#x, %u bytes: no header can be read\n", ITEM_SLPI_SMP2P, at, size);
		return;
	}
	printf("slpi: smp2p: item %u at smem+%#x, %u bytes: magic %#x, version %u, local %u, remote %u, %u of %u entries\n",
	    ITEM_SLPI_SMP2P, at, size, header.magic, header.version, header.local_pid, header.remote_pid,
	    header.valid_entries, header.total_entries);
	for (unsigned i = 0; i < header.valid_entries && i < 8; i++) {
		char name[16] = { 0 };
		uint32_t v = 0;

		if (!smem_peek(at + sizeof(header) + 20u * i, name, sizeof(name)) ||
		    !smem_peek(at + sizeof(header) + 20u * i + 16, &v, sizeof(v))) {
			printf("slpi: smp2p: entry %u cannot be read\n", i);
			return;
		}
		printf("slpi: smp2p: entry %u: \"%s\" = %#x\n", i, name, v);
	}
}

/* The first bytes of an item, for the log. What an SMP2P header is supposed to say is "$SMP",
 * version 1, the two pids, sixteen entries, and how many entries are registered — the hexdump
 * lets that be read by eye even when the fields themselves refuse the checks slpi_smp2p_state
 * makes. */
static void
smem_hexdump(uint32_t at, uint32_t size, const char *what)
{
	uint8_t raw[32];
	uint32_t bytes = size < 0x40 ? size : 0x40;

	if (at == 0) {
		printf("slpi: smem: %s is not in shared memory\n", what);
		return;
	}
	for (uint32_t off = 0; off < bytes; off += sizeof(raw)) {
		uint32_t chunk = bytes - off < (uint32_t) sizeof(raw) ? bytes - off : (uint32_t) sizeof(raw);

		if (!smem_peek(at + off, raw, chunk)) {
			printf("slpi: smem: %s at smem+%#x cannot be read\n", what, at + off);
			return;
		}
		printf("slpi: smem: %s at smem+%#x:", what, at + off);
		for (uint32_t i = 0; i < chunk; i++) {
			printf(" %02x", raw[i]);
		}
		printf("\n");
	}
}

/*
 * The whole shared memory, looked at once: which partitions the table of contents names, which
 * items the SLPI's and the global one hold, the first bytes of the two SMP2P items, and after
 * five seconds which words moved. Read-only, every line an observation, asked only when M3's
 * wait has already failed. The question it answers: the vendor's node asks for qcom,smem =
 * <0x1e1 0x1ae>, in 481 and out 430 (android-merged.dts:16769), and in the vendor tree the
 * SLPI's smp2p probe cannot finish until the outbound item exists — its qcom_smem_get is
 * answered only once the AP has allocated 430 (vendor-src/soc/smp2p.c:329, 485-513) — while
 * our kernel has never allocated anything. So: is 430 there or not; and does any word in the
 * SLPI's partition change over five seconds — a glink channel's write index, say — which would
 * mean the subsystem is alive and sharing even though its ready bit never came.
 */
static void
slpi_smem_walk(void)
{
	struct smem_table table;
	struct smem_table_entry ten;
	static struct smem_item found[256];
	static uint8_t snap[SMEM_SIZE];
	uint32_t size = 0, at;

	printf("slpi: smem: walking the shared memory table, read-only (notes/wifi.md \xc2\xa7 5 M3)\n");
	if (!smem_peek(SMEM_SIZE - 0x1000, &table, sizeof(table)) || memcmp(table.magic, "$TOC", 4) != 0 ||
	    table.version != 1 || table.count > 64) {
		printf("slpi: smem: no usable table of contents at smem+%#x\n", SMEM_SIZE - 0x1000);
		return;
	}
	printf("slpi: smem: the table of contents names %u partitions\n", table.count);
	for (uint32_t i = 0; i < table.count; i++) {
		bool ours;
		unsigned count;

		if (!smem_peek(SMEM_SIZE - 0x1000 + sizeof(table) + i * sizeof(ten), &ten, sizeof(ten)) ||
		    ten.offset == 0 || ten.size == 0 || (uint64_t) ten.offset + ten.size > SMEM_SIZE - 0x1000) {
			continue;
		}
		ours = (ten.host0 == SMEM_US && ten.host1 == SMEM_SLPI) ||
		    (ten.host0 == SMEM_GLOBAL && ten.host1 == SMEM_GLOBAL);
		printf("slpi: smem: partition %u: hosts %u-%u at smem+%#x, %u bytes%s\n", i, ten.host0,
		    ten.host1, ten.offset, ten.size, ours ? ", listing its items" : "");
		if (!ours) {
			continue;
		}
		count = smem_items(ten.offset, ten.cacheline, found, 256);
		for (unsigned j = 0; j < count; j++) {
			printf("slpi: smem:   item %u at smem+%#x, %u bytes\n", found[j].item,
			    found[j].offset, found[j].size);
		}
	}
	at = smem_item(ITEM_SLPI_SMP2P, &size);
	smem_hexdump(at, size == 0 ? 0x40u : size, "the inbound item 481, the one the SLPI writes");
	at = smem_item(ITEM_SLPI_SMP2P_OUT, &size);
	smem_hexdump(at, size == 0 ? 0x40u : size, "the outbound item 430, the one we would have to make");

	/* Five seconds and a diff of the SLPI's partition: its allocated words are the glink rings
	 * and the two SMP2P items, and a word that moves is a processor writing to memory. */
	uint32_t cacheline = 0, psize = 0;
	uint32_t shared = smem_partition(SMEM_US, SMEM_SLPI, &cacheline, &psize);
	unsigned changes = 0, shown = 0;

	if (shared == 0 || psize > SMEM_SIZE || !smem_peek(shared, snap, psize)) {
		printf("slpi: smem: the slpi's partition cannot be snapshotted, so nothing can be said "
		    "about whether it is alive\n");
		return;
	}
	printf("slpi: smem: %u bytes of the slpi's partition snapshotted; five seconds, then the diff\n",
	    psize);
	sleep_ms(5000);
	for (uint32_t off = 0; off + 4 <= psize; off += 4) {
		uint32_t now = 0, was;

		if (!smem_peek(shared + off, &now, sizeof(now))) {
			break;
		}
		memcpy(&was, snap + off, sizeof(was));
		if (was == now) {
			continue;
		}
		changes++;
		if (shown++ < 24) {
			printf("slpi: smem: smem+%#x changed: %#x -> %#x\n", shared + off, was, now);
		}
	}
	printf("slpi: smem: %u words changed in five seconds: %s\n", changes,
	    changes != 0 ? "something writes this memory, so the slpi is alive and sharing" :
	    "nothing moved: the slpi does not touch this memory");
}

/* ---- M3: the outbound item 430, made the way the vendor's smp2p driver would make it ------------ */

/*
 * The DT's qcom,smp2p-dsps node says qcom,smem = <481 430>: 481 is the item the SLPI writes and
 * we read, and 430 is the item we would write and the SLPI reads. logs/024654-run.log showed 430
 * was never allocated -- the AP side of Linux never probed this smp2p on this phone, and neither
 * had we -- and the SLPI's driver fetches its inbound item lazily, in its interrupt handler
 * (vendor-src/soc/smp2p.c:329), which nothing has had cause to run. So the SLPI has never been
 * told we exist, and its own item sits at zero valid entries.
 *
 * The item itself is made by slpi_smem_alloc() below, the way qcom_smem_alloc_private() makes
 * one. The contents are what qcom_smp2p_alloc_item() and its walk of the DT's children leave
 * them (vendor-src/soc/smp2p.c:563): the "$SMP" header, then the out entries in DT order,
 * "master-kernel" first and "sleepstate" second, because the two in entries, "slave-kernel" and
 * "sleepstate_see", belong to the SLPI's item and get no room here. The values stay zero:
 * force-stop de-asserted and the AP not asleep, which is what a running system keeps them at.
 */
struct smp2p_item {
	struct smp2p_header header;
	struct {
		char name[16];
		uint32_t value;
	} entry[16];
};

#define APCS_PHYS                   0x17c00000UL        /* the qcom,ipc phandle 0x1ad: syscon@17c0000c */
#define APCS_DSPS_KICK_OFFSET       0x0c                /* the DT's qcom,ipc = <0x1ad 0x00 0x1a> */
#define APCS_DSPS_KICK_BIT          (1u << 0x1a)
/* The queued-data register of this generation of the block: a bit that was set but never
 * taken stays visible here (and in the trigger register) until the target's side clears it
 * in its interrupt handler. Whether this particular block implements it is one of the
 * things M12 reads; an unimplemented offset answers zero, which is harmless. */
#define APCS_QUEUE_OFFSET           0x14

/* One write into shared memory: the mapping, and the range, are smem_peek's business. */
static bool
smem_poke(uint64_t offset, const void *from, uint32_t size)
{
	uint8_t first;

	if (!smem_peek(offset, &first, 1) || offset + size > SMEM_SIZE) {
		return false;
	}
	for (uint32_t i = 0; i < size; i++) {
		smem[offset + i] = ((const uint8_t *) from)[i];
	}
	return true;
}

/*
 * Make an item in the shared partition between us and the slpi, or find the one an earlier
 * allocation made: the vendor's qcom_smem_alloc() answering -EEXIST is normal and its callers go
 * on either way (vendor-src/soc/smp2p.c:485). Shared memory outlives our runs, so a second run
 * always takes the EEXIST path. Where the item's data is; zero, with the reason printed, if it
 * could not be made.
 *
 * The way qcom_smem_alloc_private() makes one -- the same allocation as the redwood driver's,
 * which was checked against this phone's own items (drivers/wifi/hakios.wifi.c:1961): the entry
 * header at partition + free_uncached, the data right after it, free_uncached moved along by the
 * header and the size rounded up to eight. The two free pointers are the ends of the free space:
 * ordinary allocations, smp2p's and glink's among them, grow up from the header side and move
 * free_uncached, while cached ones grow down from the end and move free_cached. The phone's own
 * (0,3) partition says exactly that -- its six items sit from 0x20 up to 0x5238 and free_cached
 * is still the full 0x30000 (logs/030641-run.log) -- and the room check is against free_cached.
 *
 * The vendor's allocator takes a hardware spinlock, because the other processor can be allocating
 * in the same partition at the same moment. We do not: the first run allocates before the SLPI is
 * started, when nothing else can allocate, and the later runs do not touch the partition header
 * at all. M4 allocates while an slpi instance runs, and asks why that is survivable: that
 * instance's own allocations were minutes after its reset and are long over, and the canary
 * check below is the guard we have -- if the slot free_uncached points at holds an item, then
 * the layout is not what this driver thinks and nothing is written anywhere.
 */
static uint32_t
slpi_smem_alloc(uint32_t item, uint32_t bytes, const char *tag)
{
	struct smem_partition header;
	struct smem_entry found;
	uint32_t cacheline = 0, partition_size = 0, size = 0;
	uint32_t partition = smem_partition(SMEM_US, SMEM_SLPI, &cacheline, &partition_size);
	uint32_t at = smem_item(item, &size);

	if (partition == 0) {
		printf("slpi: %s: there is no shared partition between us and the slpi: no item can be made\n", tag);
		return 0;
	}
	if (!smem_peek(partition, &header, sizeof(header)) || memcmp(header.magic, "$PRT", 4) != 0) {
		printf("slpi: %s: the partition at smem+%#x does not read as a partition: no item\n", tag, partition);
		return 0;
	}
	if (at != 0) {
		if (size < bytes) {
			printf("slpi: %s: item %u is there at smem+%#x but holds only %u bytes, fewer than the "
			    "%u asked for: not touching it\n", tag, item, at, size, bytes);
			return 0;
		}
		printf("slpi: %s: item %u is already there at smem+%#x: the vendor's qcom_smem_alloc() answers "
		    "EEXIST here and its callers go on either way\n", tag, item, at);
		return at;
	}
	uint32_t rounded = (bytes + 7) & ~7u;   /* qcom_smem_alloc_private() rounds the size up to eight */
	uint32_t room = (uint32_t) sizeof(found) + rounded;
	uint32_t newer;

	if (header.free_uncached < sizeof(header) || header.free_uncached > header.free_cached ||
	    header.free_uncached + room > header.free_cached) {
		printf("slpi: %s: the partition is %u bytes with free_uncached %#x and free_cached %#x: "
		    "no room for %u more\n", tag, header.size, header.free_uncached, header.free_cached, room);
		return 0;
	}
	/* The slot free_uncached points at has to be free: if it holds a canary, then our reading of
	 * the layout is wrong and writing there would clobber somebody else's item. */
	if (!smem_peek(partition + header.free_uncached, &found, sizeof(found))) {
		printf("slpi: %s: the slot at smem+%#x cannot be read: no item\n",
		    tag, partition + header.free_uncached);
		return 0;
	}
	if (found.canary == SMEM_CANARY) {
		printf("slpi: %s: the slot at smem+%#x already holds item %u, so free_uncached %#x does not "
		    "point at free space and the layout is not what this driver thinks: not writing\n",
		    tag, partition + header.free_uncached, found.item, header.free_uncached);
		return 0;
	}
	found = (struct smem_entry) {
		.canary = SMEM_CANARY, .item = item, .size = rounded,
		.padding_data = (uint16_t) (rounded - bytes), .padding_header = 0, .reserved = 0,
	};
	if (!smem_poke(partition + header.free_uncached, &found, sizeof(found))) {
		printf("slpi: %s: the entry header at smem+%#x could not be written\n",
		    tag, partition + header.free_cached);
		return 0;
	}
	newer = header.free_uncached + room;
	if (!smem_poke(partition + __builtin_offsetof(struct smem_partition, free_uncached), &newer,
	    sizeof(newer))) {
		printf("slpi: %s: free_uncached could not be moved from %#x to %#x\n",
		    tag, header.free_uncached, newer);
		return 0;
	}
	at = partition + header.free_uncached + (uint32_t) sizeof(found);
	printf("slpi: %s: item %u is allocated at smem+%#x: the entry header sits at smem+%#x with size %u "
	    "and %u of padding, and free_uncached moves %#x -> %#x (free_cached stays %#x)\n",
	    tag, item, at, partition + header.free_uncached, rounded, rounded - bytes,
	    header.free_uncached, newer, header.free_cached);
	return at;
}

/*
 * Make the outbound item, or find the one an earlier run of this kernel made and rewrite it: the
 * contents are the smp2p item's own, whatever path the allocation took (see the section comment
 * above). Where the item's data is; zero, with the reason printed by slpi_smem_alloc(), if it
 * could not be made.
 */
static uint32_t
slpi_smp2p_make_out(void)
{
	struct smp2p_item item;
	uint32_t at = slpi_smem_alloc(ITEM_SLPI_SMP2P_OUT, (uint32_t) sizeof(item), "m3");

	if (at == 0) {
		return 0;
	}
	memset(&item, 0, sizeof(item));
	item.header.magic = SMP2P_MAGIC;
	item.header.version = 1;
	item.header.features[0] = 1;                    /* SMP2P_FEATURE_SSR_ACK, the low byte of the 24-bit field */
	item.header.local_pid = SMEM_US;
	item.header.remote_pid = SMEM_SLPI;
	item.header.total_entries = 16;
	item.header.valid_entries = 2;
	memcpy(item.entry[0].name, "master-kernel", 13);
	memcpy(item.entry[1].name, "sleepstate", 10);
	if (!smem_poke(at, &item, sizeof(item))) {
		printf("slpi: m3: the item's contents could not be written at smem+%#x\n", at);
		return 0;
	}
	settle();
	printf("slpi: m3: item %u reads: version %u, features %u, local %u, remote %u, %u of %u entries, \"%s\" "
	    "and \"%s\", both values zero: force-stop is off and we are not asleep, which is what the vendor's "
	    "driver leaves them at\n", ITEM_SLPI_SMP2P_OUT, item.header.version, item.header.features[0],
	    item.header.local_pid, item.header.remote_pid, item.header.valid_entries, item.header.total_entries,
	    item.entry[0].name, item.entry[1].name);
	return at;
}

/*
 * Ring the SLPI's doorbell: the DT's qcom,ipc for this smp2p node is <0x1ad 0x00 0x1a>, the
 * register at 0x17c0000c, bit 0x1a, write-to-trigger. qcom_smp2p_kick() does the same write after
 * its wmb; the settle() here is the wmb, and it matters, because the item was written through a
 * write-combining mapping and the doorbell through a device one.
 */
static void
slpi_smp2p_kick(const char *why)
{
	static vm_offset_t apcs;

	if (apcs == 0) {
		apcs = ml_io_map(APCS_PHYS, 0x1000);
	}
	if (apcs == 0) {
		printf("slpi: m3: the apcs block at %#x does not map: the slpi cannot be told\n", APCS_PHYS);
		return;
	}
	settle();
	wr(apcs + APCS_DSPS_KICK_OFFSET, APCS_DSPS_KICK_BIT);
	settle();
	printf("slpi: m3: bit 0x1a of %#x is written: the slpi is told, because %s\n",
	    APCS_PHYS + APCS_DSPS_KICK_OFFSET, why);
}

/* M18: the host half of check_ssr/do_ssr_ack, in thread context after the
 * fresh instance announces. Only our flags word changes; never write its item.
 * The local ack state begins false because make_out cleared our item this boot.
 * A duplicate observation must not toggle it a second time. */
static bool
slpi_smp2p_ack_restart(void)
{
	struct smp2p_header in, out;
	uint32_t in_size = 0, out_size = 0, before, after, readback = 0;
	uint32_t in_at = smem_item(ITEM_SLPI_SMP2P, &in_size);
	uint32_t out_at = smem_item(ITEM_SLPI_SMP2P_OUT, &out_size);
	uint32_t ack_mask = 1u << SMP2P_FLAGS_RESTART_ACK;
	bool done, ack;

	if (in_at == 0 || out_at == 0 || in_size < sizeof(in) || out_size < sizeof(out) ||
	    !smem_peek(in_at, &in, sizeof(in)) || !smem_peek(out_at, &out, sizeof(out))) {
		printf("slpi: m18: restart ack skipped: both complete SMP2P headers are required\n");
		return false;
	}
	if (in.magic != SMP2P_MAGIC || out.magic != SMP2P_MAGIC || in.version != 1 || out.version != 1 ||
	    in.local_pid != SMEM_SLPI || in.remote_pid != SMEM_US ||
	    out.local_pid != SMEM_US || out.remote_pid != SMEM_SLPI ||
	    (in.features[0] & out.features[0] & 1u) == 0) {
		printf("slpi: m18: restart ack skipped: invalid peer identity, version or SSR_ACK feature\n");
		return false;
	}
	before = out.flags;
	done = (in.flags & (1u << SMP2P_FLAGS_RESTART_DONE)) != 0;
	ack = (before & ack_mask) != 0;
	printf("slpi: m18: before ack: item 481 flags %#x, item 430 flags %#x; "
	    "remote restart-done %u, local restart-ack %u\n", in.flags, before, done, ack);
	if (done == ack) {
		printf("slpi: m18: no new restart: flags are left unchanged and no ack kick is sent\n");
		return false;
	}
	after = (before & ~ack_mask) | (done ? ack_mask : 0u);
	if (!smem_poke(out_at + __builtin_offsetof(struct smp2p_header, flags), &after, sizeof(after))) {
		printf("slpi: m18: restart ack could not be written\n");
		return false;
	}
	settle();
	if (!smem_peek(out_at + __builtin_offsetof(struct smp2p_header, flags), &readback, sizeof(readback)) ||
	    readback != after) {
		printf("slpi: m18: restart ack readback failed: wanted %#x, got %#x; not ringing\n", after, readback);
		return false;
	}
	printf("slpi: m18: item 430 flags %#x -> %#x, readback %#x: only restart-ack bit 1 "
	    "changed; acknowledging the SLPI restart before GLINK exchange\n", before, after, readback);
	slpi_smp2p_kick("m18: the host restart-ack matches its restart-done, as do_ssr_ack requires");
	return true;
}

/*
 * After the start: watch the SLPI's item 481 for the signs its smp2p driver got as far as
 * registering its entries. logs/024654-run.log caught it with the "$SMP" header written -- version
 * last, features set, so qcom_smp2p_alloc_outbound_item() ran to the end -- and valid_entries
 * zero and no entry names at all, which is where qcom_smp2p_alloc_item() leaves the item when it
 * fails at or before the first entry, and from which nothing after it ran: no interrupt handler,
 * no GLINK edge. If valid_entries goes non-zero, the driver is alive; it has never seen our item,
 * so we ring the doorbell then, and watch for the "slave-kernel" ready bit the GLINK edge sets
 * when its channel opens.
 *
 * The item itself is looked for for the whole wait, not just at the start. logs/034623-run.log
 * is why: the item was absent when the watch began and the old watch returned at once, which
 * proved absence at that moment and said nothing about the half-minute after it. The reading
 * that followed -- that the item 033426 watched, with its entries appearing after that run's own
 * start, had been allocated by the earlier start's instance sometime after that earlier watch had
 * given up -- was right, and logs/035630-run.log proved it outright: the item that round found
 * already allocated at its first look, with its entries registered and its glink items beside it,
 * had been written by the previous round's slpi instance minutes after that round stopped
 * watching. A subsystem's smp2p probe is one driver among its own, and its time is not ours to
 * set. Returns: 2 the ready bit came, 1 the entries appeared without it, 0 the item never
 * appeared at all.
 */
static int
slpi_smp2p_watch(uint32_t seconds)
{
	struct smp2p_header header;
	uint32_t size = 0, at = 0, was_valid = 0, was_value = 0;
	bool kicked = false, ready = false;
	const char *why = "";

	printf("slpi: m3: looking for the slpi's item %u, and then for its valid_entries, entry names "
	    "and the \"slave-kernel\" ready bit, every two seconds for %u s\n", ITEM_SLPI_SMP2P, seconds);
	at = smem_item(ITEM_SLPI_SMP2P, &size);
	if (at == 0) {
		printf("slpi: m3: the item is not there yet; the watch continues for the whole wait anyway\n");
	} else {
		printf("slpi: m3: the slpi's item is at smem+%#x\n", at);
	}
	for (uint32_t waited = 0; waited < seconds * 1000; waited += 2000) {
		sleep_ms(2000);
		if (at == 0) {
			at = smem_item(ITEM_SLPI_SMP2P, &size);
			if (at == 0) {
				continue;
			}
			printf("slpi: m3: the slpi's item has appeared at smem+%#x, %u s after TrustZone "
			    "released its reset: its smp2p driver has allocated it, so the q6 is executing\n",
			    at, waited / 1000 + 2);
		}
		if (smem_peek(at, &header, sizeof(header)) && header.magic == SMP2P_MAGIC &&
		    header.valid_entries != was_valid) {
			printf("slpi: m3: the slpi's item now has %u of %u entries valid, where it had %u: %s\n",
			    header.valid_entries, header.total_entries, was_valid,
			    header.valid_entries != 0 ? "its smp2p driver registered entries, so it is alive" :
			    "back to nothing");
			for (unsigned i = 0; i < header.valid_entries && i < 16; i++) {
				char name[16] = { 0 };
				uint32_t value = 0;

				if (smem_peek(at + sizeof(header) + 20u * i, name, sizeof(name)) &&
				    smem_peek(at + sizeof(header) + 20u * i + 16, &value, sizeof(value))) {
					printf("slpi: m3:   entry %u is \"%.*s\" with value %#x\n", i, 16, name, value);
				}
			}
			was_valid = header.valid_entries;
			if (was_valid != 0 && !kicked) {
				kicked = true;
				slpi_smp2p_kick("its driver is up and it has never been told our item exists");
			}
		}
		if (slpi_smp2p_state(&was_value, &why) && was_value & SMP2P_READY) {
			printf("slpi: m3: the \"slave-kernel\" entry reads %#x: the slpi says it is up\n", was_value);
			ready = true;
			break;
		}
	}
	/* The whole picture of the item, whether or not the ready bit came: the entry names and
	 * values are what the slpi's smp2p driver published, and how many of them are valid says
	 * how far its boot got. logs/033426-run.log is why this is printed at the end and not
	 * only on a change: the change line was the decisive one there, and a run must not depend
	 * on catching it. */
	if (at != 0 && smem_peek(at, &header, sizeof(header)) && header.magic == SMP2P_MAGIC) {
		printf("slpi: m3: the slpi's item at smem+%#x now has %u of %u entries valid:\n",
		    at, header.valid_entries, header.total_entries);
		for (unsigned i = 0; i < header.total_entries && i < 16; i++) {
			char name[16] = { 0 };
			uint32_t value = 0;

			if (smem_peek(at + sizeof(header) + 20u * i, name, sizeof(name)) &&
			    smem_peek(at + sizeof(header) + 20u * i + 16, &value, sizeof(value))) {
				printf("slpi: m3:   entry %u is \"%.*s\" with value %#x%s\n", i, 16, name, value,
				    i < header.valid_entries ? "" : ", never registered");
			}
		}
	}
	if (at == 0) {
		printf("slpi: m3: after %u s the slpi's item has never appeared: either the q6 is "
		    "not executing or it is executing and has not reached its smp2p probe yet, which "
		    "takes minutes and not seconds -- logs/035630-run.log found the previous round's "
		    "instance had reached it long after that round gave up (notes/wifi.md §5 M3)\n",
		    seconds);
		return 0;
	}
	printf("slpi: m3: after %u s: the slpi's item had %u entries valid, %s been kicked, the ready bit %s: %s\n",
	    seconds, was_valid, kicked ? "has" : "has never", ready ? "is set" : "never appeared",
	    was_valid == 0 ? "the item was allocated but its entries never registered: the failure is inside "
	    "the slpi, at or before its first smp2p entry, and what is left is to see the slpi's own reason "
	    "(notes/wifi.md §5 M3)" :
	    ready ? "the SLPI side is up and open: GLINK is next" :
	    "the slpi's smp2p driver is alive but its GLINK edge never opened: the next step is inside "
	    "GLINK, not SMP2P");
	return ready ? 2 : 1;
}

/*
 * M11: the restart-ack handshake, asked of the instance that is running now.
 *
 * M10 left one question open and closed the other: the glink rx of every instance is dead,
 * and the doorbell itself is exonerated for it. What has never been tested is the other
 * doorbell this edge has: bit 0x1a, the smp2p kick. Everything said about it so far proves
 * nothing. Every 0x1a ring in every log went out before a reset -- before the instance's
 * smp2p driver had asked for its interrupt, which is the one thing that makes a ring lost,
 * the instance's driver not having asked for it yet -- and the one ring that went out
 * while an instance ran, 45 s after its reset, was a bare pulse with no bit of our item
 * changed: the vendor's handler reads the inbound item and dispatches changes
 * (smp2p.c:317-349), and with nothing changed a live handler answers a bare pulse with
 * silence. So the pulse was the wrong question, not only mistimed.
 *
 * The protocol has a question that cannot be answered with silence. check_ssr() watches
 * the inbound item's flags bit 0, restart-done, and when it flips, do_ssr_ack() toggles
 * the outbound item's flags bit 1, restart-ack, and rings back (smp2p.c:197-224). Both
 * sides advertise it with features 1 -- SMP2P_FEATURE_SSR_ACK -- and the slpi's own driver
 * wrote features 1 into item 481 itself (logs/024654-run.log read the header). So: set
 * restart-done in our item 430's flags, ring bit 0x1a, and watch item 481's flags. The
 * answer is a word only the slpi can write, and its smp2p probe ran minutes after its
 * reset -- logs/035630-run.log learned that -- so the instance that logs/060328-run.log
 * started has long since asked for this interrupt. Nothing is restarted and nothing is
 * written to its side: our flags word and its own item are the only memory in play.
 *
 * What each answer means: the ack comes, and the apcs doorbell does reach the slpi -- its
 * smp2p rx is alive and only glink's rx is broken, which is a glink-driver question. It
 * does not come, and no apcs bit reaches any handler on this instance: the difference from
 * Android is in how the subsystem was brought up -- the vendor's pil and AOP path, not
 * ours -- and that is where the next step is (notes/wifi.md §5).
 */
static void
slpi_smp2p_handshake(uint32_t seconds)
{
	struct smp2p_header out_head, in_head;
	uint32_t size = 0, out_at = smem_item(ITEM_SLPI_SMP2P_OUT, &size);
	uint32_t in_at = smem_item(ITEM_SLPI_SMP2P, &size);
	uint32_t was_flags = 0, was_valid = 0, was_values[16] = { 0 };
	bool acked = false, ready = false, moved = false;

	printf("slpi: m11: the restart-done ping: bit 0 of our item's flags, then bit 0x1a, and the "
	    "watch is the slpi's item flags, where its driver would answer with restart-ack, bit 1 "
	    "(vendor-src/soc/smp2p.c:197-224, 317-349)\n");
	if (out_at == 0 || in_at == 0) {
		printf("slpi: m11: %s: the handshake cannot be asked\n",
		    out_at == 0 ? "our item 430 is not in shared memory" :
		    "the slpi's item 481 is not in shared memory");
		return;
	}
	if (!smem_peek(out_at, &out_head, sizeof(out_head)) || !smem_peek(in_at, &in_head, sizeof(in_head))) {
		printf("slpi: m11: one of the two headers cannot be read: the handshake cannot be asked\n");
		return;
	}
	printf("slpi: m11: before: our item's flags read %#x and the slpi's %#x; its header says version %u, "
	    "features %u, %u of %u entries: features 1 is the ack protocol, written by its own driver\n",
	    out_head.flags, in_head.flags, in_head.version, in_head.features[0], in_head.valid_entries,
	    in_head.total_entries);
	was_flags = in_head.flags;
	was_valid = in_head.valid_entries;
	for (unsigned i = 0; i < was_valid && i < 16; i++) {
		(void) smem_peek(in_at + sizeof(in_head) + 20u * i + 16, &was_values[i], sizeof(was_values[i]));
	}

	/* The ping. The whole header goes back as it came except the flags word, which is ours
	 * to set: item 430 is the outbound item, and the slpi's driver only ever reads it. */
	out_head.flags |= 1u << SMP2P_FLAGS_RESTART_DONE;
	if (!smem_poke(out_at, &out_head, sizeof(out_head))) {
		printf("slpi: m11: our flags word could not be written at smem+%#x\n", out_at);
		return;
	}
	printf("slpi: m11: our item's flags now read %#x: restart-done, which is the q6's own signal "
	    "that its side just came out of a reset\n", out_head.flags);
	slpi_smp2p_kick("the ping is in its inbound item already, and this is the first ring this "
	    "instance's smp2p driver has ever been given after its probe");

	for (uint32_t waited = 0; waited < seconds * 1000; waited += 2000) {
		sleep_ms(2000);
		if (!smem_peek(in_at, &in_head, sizeof(in_head))) {
			continue;
		}
		if (in_head.flags != was_flags) {
			printf("slpi: m11: %u s after the ring its flags moved %#x -> %#x\n",
			    waited / 1000 + 2, was_flags, in_head.flags);
			was_flags = in_head.flags;
			moved = true;
			if (was_flags & (1u << SMP2P_FLAGS_RESTART_ACK)) {
				acked = true;
			}
		}
		if (in_head.valid_entries != was_valid) {
			printf("slpi: m11: %u s after the ring its entries went %u -> %u valid\n",
			    waited / 1000 + 2, was_valid, in_head.valid_entries);
			was_valid = in_head.valid_entries;
			moved = true;
		}
		for (unsigned i = 0; i < in_head.valid_entries && i < 16; i++) {
			uint32_t value = 0;
			char name[16] = { 0 };

			if (!smem_peek(in_at + sizeof(in_head) + 20u * i + 16, &value, sizeof(value)) ||
			    value == was_values[i]) {
				continue;
			}
			(void) smem_peek(in_at + sizeof(in_head) + 20u * i, name, sizeof(name));
			printf("slpi: m11: %u s after the ring entry %u \"%.*s\" moved %#x -> %#x\n",
			    waited / 1000 + 2, i, 16, name, was_values[i], value);
			was_values[i] = value;
			moved = true;
			if (memcmp(name, "slave-kernel", 12) == 0 && (value & (1u << SMP2P_READY))) {
				ready = true;
			}
		}
		if (acked || ready) {
			break;
		}
		if (waited + 2000 == (seconds * 1000) / 2) {
			slpi_smp2p_kick("a second ring, in case the first edge was lost the way "
			    "logs/060328-run.log's rings before an init were");
		}
	}

	/* The item as it stands now, whatever the watch saw: the conclusion has to be readable
	 * from the words, and the flags word is the whole of the answer. */
	if (smem_peek(in_at, &in_head, sizeof(in_head))) {
		printf("slpi: m11: after %u s its flags read %#x, entries %u valid, and the entry values "
		    "are as the watch left them\n", seconds, in_head.flags, in_head.valid_entries);
	}
	printf("slpi: m11: after %u s and two rings: %s\n", seconds,
	    acked ? "the restart-ack came, a word only the slpi can write: its smp2p rx is alive and "
	    "the apcs doorbell does reach the slpi -- what is broken is glink's rx alone, and that is "
	    "a glink-driver question (notes/wifi.md §5 M11)" :
	    ready ? "the ready bit came though the ack did not: its rx is alive and answered in its "
	    "own way; glink's rx is the only dead one (notes/wifi.md §5 M11)" :
	    moved ? "something moved in its item, printed above, but neither the ack nor the ready bit: "
	    "its side is writing, and the answer to the ping is not the protocol's (notes/wifi.md §5 M11)" :
	    "nothing it could have written appeared: its smp2p rx never fired, so no apcs bit reaches "
	    "any handler on this instance -- the doorbell path is dead whole, and the difference from "
	    "Android is in how the subsystem was brought up, not in glink (notes/wifi.md §5 M11)");
}

/*
 * M12: the doorbell block's own words, asked of the same instance M11 asked.
 *
 * M11's answer was silence, and silence has two causes this kernel cannot
 * yet tell apart. The run did add one fact worth its while: the flags of
 * item 481 read 0x1 before anything was rung -- the slpi's own restart-done,
 * the announcement its smp2p driver writes when its side comes out of a
 * reset, still sitting there unanswered because our kernel never implements
 * the ack. So the driver ran its init and registered its entries; what
 * never happened is its interrupt handler: our ping was exactly what
 * check_ssr answers with an ack, and no ack came. Either the interrupt
 * never reached the running core, or it reached and the handler said
 * nothing.
 *
 * The apcs block itself can tell these apart, and it is alive: the m2 read
 * of logs/024654-run.log answered its first words with zeros, not garbage
 * and not a hang. Its trigger register at +0xc is write-to-trigger, and in
 * this generation of the block a bit that was set but never taken stays
 * visible there and in the queue register at +0x14 until the target's side
 * clears it in its interrupt handler. So: read the block before, ring bit
 * 0x1a once, and read again at once and every second after. The baseline
 * is itself a clue: logs/060328-run.log rang bit 0x18 five times at an rx
 * that is proven dead and nothing could have cleared those bits, and
 * logs/065342-run.log rang bit 0x1a twice at an rx that answered nothing;
 * if the baseline shows either bit still set, undelivered bits latch and
 * every readback below means what it says. If it shows zero everywhere,
 * the bits are pulses that vanish whether taken or not, and the readback
 * proves nothing -- but the unanswered restart-done stands on its own.
 *
 * What each answer means: our bit still pending at the end -- the
 * doorbell reached the hardware and the slpi never came for it: the
 * interrupt line is masked or unwired on its side, and the vendor's aop
 * path, the qmp-aop mbox and the ssctl instance 0x16 the ssc node asks for
 * (android-merged.dts:3335, 3345), is what brings up the slpi's interrupt
 * path and is the next step. Our bit taken within a second -- the
 * interrupt reached the core and its handler answered nothing: the question
 * is inside the slpi's driver state, not the wiring. Never latched -- the
 * block gives no readback, and the aop path is next all the same.
 */
static void
slpi_apcs_pending_watch(uint32_t seconds)
{
	struct smp2p_header out_head, in_head;
	uint32_t size = 0, out_at = smem_item(ITEM_SLPI_SMP2P_OUT, &size);
	uint32_t in_at = smem_item(ITEM_SLPI_SMP2P, &size);
	uint32_t was_flags = 0;
	static vm_offset_t apcs;
	bool latched = false, taken = false, acked = false;

	printf("slpi: m12: the same ping as m11 and one new witness: the doorbell block's own registers. "
	    "A bit that was rung but never taken stays visible in them; one that was taken does not. "
	    "That is the difference between a masked interrupt line and a silent handler (notes/wifi.md §5 M12)\n");
	if (out_at == 0 || in_at == 0) {
		printf("slpi: m12: %s: the ping cannot be asked\n",
		    out_at == 0 ? "our item 430 is not in shared memory" :
		    "the slpi's item 481 is not in shared memory");
		return;
	}
	if (apcs == 0) {
		apcs = ml_io_map(APCS_PHYS, 0x1000);
	}
	if (apcs == 0) {
		printf("slpi: m12: the apcs block at %#x does not map\n", APCS_PHYS);
		return;
	}

	printf("slpi: m12: before:");
	for (unsigned off = 0; off <= 0x1c; off += 4) {
		printf(" +%#x %#010x", off, rd(apcs + off));
	}
	printf("\n");
	if (rd(apcs + APCS_DSPS_KICK_OFFSET) & (1u << 0x18)) {
		printf("slpi: m12: bit 0x18 is still set in the trigger register: logs/060328-run.log's five rings at the "
		    "dead glink rx are still pending there: undelivered bits latch, and every readback below means what it says\n");
	}
	if (rd(apcs + APCS_DSPS_KICK_OFFSET) & APCS_DSPS_KICK_BIT) {
		printf("slpi: m12: bit 0x1a is still set too: logs/065342-run.log's two rings are still pending: the slpi "
		    "has not come for them either, which is the answer already\n");
		latched = true;
	}

	if (!smem_peek(out_at, &out_head, sizeof(out_head)) || !smem_peek(in_at, &in_head, sizeof(in_head))) {
		printf("slpi: m12: one of the two headers cannot be read: the ping cannot be asked\n");
		return;
	}
	was_flags = in_head.flags;
	out_head.flags |= 1u << SMP2P_FLAGS_RESTART_DONE;
	if (!smem_poke(out_at, &out_head, sizeof(out_head))) {
		printf("slpi: m12: our flags word could not be written at smem+%#x\n", out_at);
		return;
	}
	settle();
	wr(apcs + APCS_DSPS_KICK_OFFSET, APCS_DSPS_KICK_BIT);
	settle();
	uint32_t trig = rd(apcs + APCS_DSPS_KICK_OFFSET);
	uint32_t queue = rd(apcs + APCS_QUEUE_OFFSET);
	printf("slpi: m12: at once after the ring: +0xc reads %#010x and +0x14 reads %#010x: our bit%s there\n",
	    trig, queue, ((trig | queue) & APCS_DSPS_KICK_BIT) != 0 ? " is" : " is not");
	if ((trig | queue) & APCS_DSPS_KICK_BIT) {
		latched = true;
	}

	for (uint32_t waited = 0; waited < seconds; waited++) {
		sleep_ms(1000);
		trig = rd(apcs + APCS_DSPS_KICK_OFFSET);
		queue = rd(apcs + APCS_QUEUE_OFFSET);
		if (latched && !taken && ((trig | queue) & APCS_DSPS_KICK_BIT) == 0) {
			printf("slpi: m12: %u s after the ring our bit is gone from both registers: the interrupt was taken\n",
			    waited + 1);
			taken = true;
		}
		if (!smem_peek(in_at, &in_head, sizeof(in_head))) {
			continue;
		}
		if (in_head.flags != was_flags) {
			printf("slpi: m12: %u s after the ring its flags moved %#x -> %#x\n",
			    waited + 1, was_flags, in_head.flags);
			was_flags = in_head.flags;
			if (was_flags & (1u << SMP2P_FLAGS_RESTART_ACK)) {
				acked = true;
			}
		}
		if (acked) {
			break;
		}
	}

	printf("slpi: m12: at the end: +0xc reads %#010x, +0x14 reads %#010x, and its item's flags read %#x\n",
	    rd(apcs + APCS_DSPS_KICK_OFFSET), rd(apcs + APCS_QUEUE_OFFSET), was_flags);
	printf("slpi: m12: %s\n",
	    acked ? "the ack came this time: the doorbell does reach the slpi and only glink's rx is dead (notes/wifi.md §5 M12)" :
	    (latched && !taken) ? "our bit is still pending in the block: the doorbell reached the hardware and the slpi "
	    "never came for it: its interrupt line is masked or unwired, and the vendor's aop path -- qmp-aop and the "
	    "ssctl instance 0x16 the ssc node asks for -- is what brings up the slpi's interrupt path and is the next "
	    "step (notes/wifi.md §5 M12)" :
	    taken ? "the bit was taken: the interrupt reached the running core and its handler answered nothing: the "
	    "question is inside the slpi's driver state, not the wiring (notes/wifi.md §5 M12)" :
	    "the bit never latched and nothing answered: the block gives no readback of a pulse, so it cannot say which "
	    "side is silent; the unanswered restart-done of logs/065342-run.log stands on its own, and the aop path is "
	    "the next step (notes/wifi.md §5 M12)");
}

/*
 * M13: our own interrupt controller, asked whether the slpi's rings ever arrive.
 *
 * Every experiment so far has watched the slpi's writes in shared memory -- which
 * prove its cpu runs and its drivers initialise -- and rung our doorbell at it. The
 * other direction of the same wire has never been asked: when the slpi's driver
 * kicks, does its ring reach our interrupt controller? The vendor's devicetree
 * names both of our rx lines: the glink edge dsps carries interrupts = <0x00 0xaa
 * 0x01>, spi 170 (android-merged.dts:4106), and qcom,smp2p-dsps@1799000c carries
 * interrupts = <0x00 0xac 0x01>, spi 172 (:16769). Two rings are known to have
 * happened: the smp2p driver's kick is a write to its apcs bit right after it
 * writes the item and its restart-done flag (smp2p.c:181-191, 488-530), and
 * logs/065342-run.log read that flag, 0x1, in item 481; the glink edge's init
 * rings after it writes its version, and logs/060328-run.log read that version.
 *
 * Our kernel masks both lines -- it has only ever polled -- and a masked edge
 * interrupt still latches in GICD_ICPENDR and stays there until someone comes for
 * it, so the register is a witness that costs one read. The type bits have to be
 * read with it: a line configured level samples the pulse and keeps nothing, and
 * then the silence would prove nothing. The run starts a fresh instance the way
 * slpi=10 does, because its init is under a second old at its reset and both rings
 * are certain by the moment the announcement is seen; the read before anything is
 * the control, our gic having been up since this boot with nothing else driving
 * these two lines.
 *
 * What each answer means: a pending bit with the lines edge-configured -- the
 * slpi's ring reached our hardware: the apcs path works in its slpi-to-ap half and
 * only the ap-to-slpi half is dead, which is a question about the slpi's rx enable
 * and the aop path that arms it. No pending bit with the lines edge-configured --
 * the rings never reached us either: the doorbell path is dead in both directions,
 * and the difference from Android is in how the subsystem was brought up, not in
 * which bit we ring. Lines configured level -- the read cannot see a pulse; the
 * type has to be set to edge, which is allowed while the line is masked, and the
 * question asked again.
 */
#define GICD_PHYS           0x17a00000UL        /* the intc node's first reg, android-merged.dts:2436 */
/* Preserve old experiments for comparison. M17 alone applies the DT SPI +32
 * translation; gic_v3_spi_attach indexes its registers by absolute INTID. */
#define SPI_GLINK_RX        (slpi_level >= 17 ? 202u : 170u)
#define SPI_SMP2P_RX        (slpi_level >= 17 ? 204u : 172u)
#define GICD_ISENABLER(n)   (0x100u + 4u * ((n) / 32u))
#define GICD_ICPENDR(n)     (0x280u + 4u * ((n) / 32u))
#define GICD_ICFGR(n)       (0xc00u + 4u * ((n) / 16u))
#define GICD_ICFGR_SHIFT(n) (2u * ((n) % 16u))

static int gic_lines_edge = -1;     /* -1 unknown, 0 level, 1 edge: what the type bits say */
static int gic_firmware_config = 0; /* M15: nonzero once the firmware has read the type word as
	                                 * something other than zeros -- the direct mapping said
	                                 * zeros, so that difference is the proof that the
	                                 * distributor belongs to the firmware */
static uint32_t gic_last_enabled;   /* M16: the enable bits of the last slpi_gic_listen, so a run
	                                 * that arms the lines can tell whether its write took */
static int gic_mapping_alive = -1;  /* M16: whether the distributor answers the registers that
	                                 * cannot be dropped (see gic_instrument_check) */

/* The distributor, mapped once for every run that reads it by hand. */
static vm_offset_t
gicd_map(void)
{
	static vm_offset_t gicd;

	if (gicd == 0) {
		gicd = ml_io_map(GICD_PHYS, 0x10000);
	}
	return gicd;
}

/* Reads the one word that holds both lines' pending, enable and type bits, prints it
 * decoded, and returns the two bits that matter: bit 0 for the glink line, bit 1 for
 * the smp2p line, or -1 when the controller cannot be read. */
static int
slpi_gic_listen(const char *when)
{
	vm_offset_t gicd = gicd_map();
	uint32_t pending, enabled, config, type_glink, type_smp2p;
	unsigned bit_glink = SPI_GLINK_RX % 32, bit_smp2p = SPI_SMP2P_RX % 32;
	int bits = 0;

	if (gicd == 0) {
		printf("slpi: m13: the interrupt controller at %#x does not map: the question cannot be asked\n",
		    GICD_PHYS);
		return -1;
	}
	pending = rd(gicd + GICD_ICPENDR(SPI_GLINK_RX));
	enabled = rd(gicd + GICD_ISENABLER(SPI_GLINK_RX));
	config = rd(gicd + GICD_ICFGR(SPI_GLINK_RX));
	type_glink = (config >> GICD_ICFGR_SHIFT(SPI_GLINK_RX)) & 3u;
	type_smp2p = (config >> GICD_ICFGR_SHIFT(SPI_SMP2P_RX)) & 3u;
	/* GIC ICFGR bit 1 selects edge; bit 0 is reserved. DT IRQ_TYPE_EDGE_RISING
	 * is 1, but is an OS binding value, not the register's two-bit encoding. */
	if ((type_glink & 2u) != 0 && (type_smp2p & 2u) != 0) {
		gic_lines_edge = 1;
	} else if (type_glink == 0 && type_smp2p == 0) {
		gic_lines_edge = 0;
	}
	if (pending & (1u << bit_glink)) {
		bits |= 1;
	}
	if (pending & (1u << bit_smp2p)) {
		bits |= 2;
	}
	gic_last_enabled = enabled;
	printf("slpi: m13: %s: the word for interrupts %u-%u reads pending %#010x, enabled %#010x, "
	    "type %#010x: spi %u, the glink edge's line, is%s pending, %s, configured %s; spi %u, the "
	    "smp2p line, is%s pending, %s, configured %s\n",
	    when, (SPI_GLINK_RX / 32u) * 32u, (SPI_GLINK_RX / 32u) * 32u + 31u, pending, enabled, config,
	    SPI_GLINK_RX, (pending & (1u << bit_glink)) ? "" : " not",
	    (enabled & (1u << bit_glink)) ? "enabled" : "masked", (type_glink & 2u) != 0 ? "edge" : "level",
	    SPI_SMP2P_RX, (pending & (1u << bit_smp2p)) ? "" : " not",
	    (enabled & (1u << bit_smp2p)) ? "enabled" : "masked", (type_smp2p & 2u) != 0 ? "edge" : "level");
	return bits;
}

/*
 * M14: the same question with the type set the way the vendor's devicetree says it is.
 *
 * logs/074524-run.log answered m13's own precondition: both lines read type 0, level,
 * which is the gic's reset state -- our kernel never programs ICFGR for a line it does
 * not attach, and these two belong to no driver here. A level-configured line samples
 * the pulse and keeps nothing once the line falls back, so that run's silence proves
 * nothing, exactly as the design said it would. The devicetree says what they are:
 * interrupts = <0x00 0xaa 0x01> and <0x00 0xac 0x01>, the third cell 1 = rising edge
 * (android-merged.dts:4106, :16769). A line's type may be written while it is disabled,
 * and both are disabled -- the same run read enabled 0 -- so the write is allowed, and
 * it is read back here because a non-secure write to a secure interrupt's config is
 * ignored, and the readback is the only way to know which case this is.
 */
static void
slpi_gic_edge(void)
{
	static vm_offset_t gicd;
	uint32_t config;

	if (gicd == 0) {
		gicd = ml_io_map(GICD_PHYS, 0x10000);
	}
	if (gicd == 0) {
		printf("slpi: m14: the interrupt controller at %#x does not map: the type cannot be set\n",
		    GICD_PHYS);
		return;
	}
	config = rd(gicd + GICD_ICFGR(SPI_GLINK_RX));
	config &= ~((3u << GICD_ICFGR_SHIFT(SPI_GLINK_RX)) | (3u << GICD_ICFGR_SHIFT(SPI_SMP2P_RX)));
	config |= (1u << GICD_ICFGR_SHIFT(SPI_GLINK_RX)) | (1u << GICD_ICFGR_SHIFT(SPI_SMP2P_RX));
	wr(gicd + GICD_ICFGR(SPI_GLINK_RX), config);
	settle();
	printf("slpi: m14: the type word written for edge, and it reads back %#010x: spi %u %s, spi %u %s%s\n",
	    rd(gicd + GICD_ICFGR(SPI_GLINK_RX)),
	    SPI_GLINK_RX,
	    (rd(gicd + GICD_ICFGR(SPI_GLINK_RX)) >> GICD_ICFGR_SHIFT(SPI_GLINK_RX)) & 1u ? "is edge now" :
	    "still level",
	    SPI_SMP2P_RX,
	    (rd(gicd + GICD_ICFGR(SPI_GLINK_RX)) >> GICD_ICFGR_SHIFT(SPI_SMP2P_RX)) & 1u ? "is edge now" :
	    "still level",
	    gic_lines_edge == 0 &&
	    ((rd(gicd + GICD_ICFGR(SPI_GLINK_RX)) >> GICD_ICFGR_SHIFT(SPI_GLINK_RX)) & 3u) == 0 ?
	    ": a non-secure write to a secure interrupt is ignored, and the question cannot be asked from this side" :
	    "");
}

/*
 * M15: the same registers, read and written through the firmware that owns them.
 *
 * logs/075618-run.log wrote the type word for edge and read back all zeros. The
 * obvious reading was secure interrupts, whose config a non-secure write is ignored;
 * the vendor tree shows a second, plainer way to get exactly that behaviour, and its
 * own driver uses it: pinctrl-msm.c programs GIC SPI type bits by calling the
 * firmware, not by writing the distributor -- its dir-conn irq domain reads and
 * writes spi_cfg_regs, the GICD's own ICFGR addresses, through scm_io_read and
 * scm_io_write, "the shared SPI config for Edge is 0 and for Level interrupt is 1".
 * Those are the SCM IO service, 0x05, read 0x01 and write 0x02 (qcom_scm.h), built
 * for exactly this: registers the non-secure world is not allowed to touch. If the
 * distributor is one of them on this SoC, then every direct read logs/074524-run.log
 * and logs/075618-run.log made returned zero -- not the state of the hardware --
 * their writes were dropped, and neither run could have seen a ring: the question
 * was never askable from a mapping. The firmware's read is the first honest one.
 *
 * What each answer means: the firmware's ICFGR reads edge (0b01) while our direct
 * read said level -- the distributor is protected, our reads were zeros, and the
 * pending register has to be read through the firmware too, where a ring may have
 * sat unseen since this boot. The firmware reads level too -- the distributor is
 * open, the earlier readings were real, and the lines genuinely sit at their reset
 * type. A firmware write of edge that reads back edge -- the type can be set after
 * all, through the owner, and the pending question is asked again with it. A
 * firmware call that is refused -- the firmware will not act as our distributor.
 */
#define SCM_SVC_IO              5                 /* QCOM_SCM_SVC_IO, qcom_scm.h */
#define SCM_IO_READ             1                 /* QCOM_SCM_IO_READ */
#define SCM_IO_WRITE            2                 /* QCOM_SCM_IO_WRITE */

/* Reads one protected register through the firmware. The calling shape is the one
 * every PAS call in this file already speaks: the service and command in the first
 * word, the argument count next, the values after, the answer back in the second
 * register. */
static int
scm_io_read(uint64_t phys, uint32_t *value)
{
	uint64_t answer = 0;
	int result = scm("to read a register the firmware owns", SCM_CALL(SCM_SVC_IO, SCM_IO_READ),
	    SCM_ONE_VALUE, phys, 0, 0, &answer);
	if (result == 0) {
		*value = (uint32_t) answer;
	}
	return result;
}

static int
scm_io_write(uint64_t phys, uint32_t value)
{
	uint64_t answer = 0;

	return scm("to write a register the firmware owns", SCM_CALL(SCM_SVC_IO, SCM_IO_WRITE),
	    SCM_TWO_VALUES, phys, value, 0, &answer);
}

/* The three words again, this time through the owner of the registers, decoded the
 * same way slpi_gic_listen decodes them. Returns the same two bits -- bit 0 for the
 * glink line, bit 1 for the smp2p line -- or -1 when the firmware will not read. */
static int
slpi_gic_firmware_listen(const char *when)
{
	uint32_t pending = 0, enabled = 0, config = 0, type_glink, type_smp2p;
	unsigned bit_glink = SPI_GLINK_RX % 32, bit_smp2p = SPI_SMP2P_RX % 32;
	int bits = 0, bad = 0;

	if (scm_io_read(GICD_PHYS + GICD_ICPENDR(SPI_GLINK_RX), &pending) != 0) {
		bad = 1;
	}
	if (scm_io_read(GICD_PHYS + GICD_ISENABLER(SPI_GLINK_RX), &enabled) != 0) {
		bad = 1;
	}
	if (scm_io_read(GICD_PHYS + GICD_ICFGR(SPI_GLINK_RX), &config) != 0) {
		bad = 1;
	}
	if (bad) {
		printf("slpi: m15: %s: the firmware would not read the distributor back: the service "
		    "is refused and the registers have no readable owner (notes/wifi.md §5 M15)\n", when);
		return -1;
	}
	type_glink = (config >> GICD_ICFGR_SHIFT(SPI_GLINK_RX)) & 3u;
	type_smp2p = (config >> GICD_ICFGR_SHIFT(SPI_SMP2P_RX)) & 3u;
	if (config != 0) {
		gic_firmware_config = 1;
	}
	if (type_glink == 1 && type_smp2p == 1) {
		gic_lines_edge = 1;
	} else if (type_glink == 0 && type_smp2p == 0) {
		gic_lines_edge = 0;
	}
	if (pending & (1u << bit_glink)) {
		bits |= 1;
	}
	if (pending & (1u << bit_smp2p)) {
		bits |= 2;
	}
	printf("slpi: m15: %s: the firmware reads the word for interrupts 160-191 as pending %#010x, "
	    "enabled %#010x, type %#010x: spi %u, the glink edge's line, is%s pending, %s, configured %s; "
	    "spi %u, the smp2p line, is%s pending, %s, configured %s%s\n",
	    when, pending, enabled, config,
	    SPI_GLINK_RX, (pending & (1u << bit_glink)) ? "" : " not",
	    (enabled & (1u << bit_glink)) ? "enabled" : "masked", type_glink == 1 ? "edge" : "level",
	    SPI_SMP2P_RX, (pending & (1u << bit_smp2p)) ? "" : " not",
	    (enabled & (1u << bit_smp2p)) ? "enabled" : "masked", type_smp2p == 1 ? "edge" : "level",
	    config != 0 ? " -- the direct read said zero and the firmware says otherwise: these "
	    "registers belong to the firmware and our mapping only ever returned zeros" : "");
	return bits;
}

/* The type word, set to edge through the firmware, and read back through it. */
static void
slpi_gic_firmware_edge(void)
{
	uint32_t config = 0, readback = 0;

	if (scm_io_read(GICD_PHYS + GICD_ICFGR(SPI_GLINK_RX), &config) != 0) {
		printf("slpi: m15: the firmware would not read the type word: it cannot be set either "
		    "(notes/wifi.md §5 M15)\n");
		return;
	}
	config &= ~((3u << GICD_ICFGR_SHIFT(SPI_GLINK_RX)) | (3u << GICD_ICFGR_SHIFT(SPI_SMP2P_RX)));
	config |= (1u << GICD_ICFGR_SHIFT(SPI_GLINK_RX)) | (1u << GICD_ICFGR_SHIFT(SPI_SMP2P_RX));
	if (scm_io_write(GICD_PHYS + GICD_ICFGR(SPI_GLINK_RX), config) != 0) {
		printf("slpi: m15: the firmware refused the write of the type word: the registers have "
		    "no writer that answers to us (notes/wifi.md §5 M15)\n");
		return;
	}
	if (scm_io_read(GICD_PHYS + GICD_ICFGR(SPI_GLINK_RX), &readback) != 0) {
		printf("slpi: m15: the write went through but the firmware would not read the word back "
		    "(notes/wifi.md §5 M15)\n");
		return;
	}
	if (readback != 0) {
		gic_firmware_config = 1;
	}
	printf("slpi: m15: the firmware written with edge for both lines, and it reads %#010x: spi %u %s, "
	    "spi %u %s%s\n", readback,
	    SPI_GLINK_RX,
	    (readback >> GICD_ICFGR_SHIFT(SPI_GLINK_RX)) & 1u ? "is edge now" : "still level",
	    SPI_SMP2P_RX,
	    (readback >> GICD_ICFGR_SHIFT(SPI_SMP2P_RX)) & 1u ? "is edge now" : "still level",
	    readback == config ? ": the owner takes the type, and the pending register has to be read "
	    "through it from here on" : "");
}

/*
 * M16: asking the controller to receive the rings, rather than reading its registers.
 *
 * M15's premise is refuted by the vendor tree itself. The spi_cfg block its
 * scm_io_read/scm_io_write calls act on is named by the devicetree: it is the
 * pinctrl node's second reg, reg-names = "pinctrl\0spi_cfg", reg = <...
 * 0x17c000f0 0x60> (android-merged.dts:6272-6275), an always-on block beside the
 * doorbell, not the distributor at 0x17a00000. And logs/080949-run.log's -2 is
 * named in the firmware's own header: QCOM_SCM_EINVAL_ARG, while "not supported"
 * would have been -4 (drivers_firmware_qcom_scm.h:60-69) -- the call was
 * dispatched and the argument refused, which is not "no such service". So m13's
 * and m14's reads stand as reads of real hardware: both lines sit masked, level,
 * and not pending.
 *
 * Which leaves the one thing those runs could not do. Our kernel's own
 * gic_v3_spi_attach (osfmk/arm64/gic_v3_irq.c:240) writes IGROUPR, IGRPMODR,
 * IPRIORITYR, ICFGR and IROUTER and unmasks for every SPI a driver takes -- the
 * radio's linux-compat layer already claims its lines through it, hakios.lx.c:464
 * -- and gic_v3_spi_count() then counts what arrives through the real dispatch: a
 * number that cannot be "refused", only read or not read. m13 and m14 only ever
 * wrote ICFGR by hand and never the group, and a line the secure world holds
 * reads zero and drops writes in exactly the way both runs saw. Claiming the
 * lines is also the first real rx path: if a ring arrives, a handler of ours is
 * entered.
 *
 * Two controls come with it, both read-only and neither changing any state.
 * gic_instrument_check() reads GICD_TYPER and GICD_IIDR through the very mapping
 * slpi_gic_listen reads through: neither can be zero on a live distributor, so if
 * they read zero the mapping is dead and every gic reading m13-m15 made was void.
 * scm_call_available() asks the firmware's own availability probe (service 6,
 * command 1, remoteproc/scm.c:1145) about the io service, with the auth-and-reset
 * call this file makes work every boot (2,5) as the positive control.
 *
 * What each answer means: typer zero -- the mapping is dead and the question was
 * never askable by hand, from any level. A count above zero, or our handler
 * entered -- the slpi's ring reached a handler of ours: the slpi-to-ap half of the
 * doorbell works and only the ap-to-slpi half is left, which is the slpi's rx
 * enable and the aop path that arms it. Nothing counted with the lines now edge
 * and enabled, which is what the attach's own writes leave behind if they take --
 * the doorbell is dead in both directions and the difference from Android is in
 * the bring-up, not in which bit we ring. Nothing counted and the lines still
 * read masked and level after the kernel's own attach asked for otherwise -- the
 * writes were dropped, which is how lines held by the secure world behave, and
 * the question goes to the firmware's interrupt groups, not to our registers.
 */
#define GICD_TYPER              0x0004u     /* its geometry: no live distributor reads zero */
#define GICD_IIDR               0x0008u     /* who implemented it */
#define SCM_SVC_INFO            6           /* QCOM_SCM_SVC_INFO, drivers_firmware_qcom_scm.h:33 */
#define SCM_INFO_CALL_AVAIL     1           /* IS_CALL_AVAIL_CMD, remoteproc/scm.c:57 */

/* The availability probe's argument is the plain service-and-command word of the
 * call asked about, with the sip marker but without the 64-bit flag: the 64-ness
 * belongs to the call carrying the question, not to the answer -- the vendor's
 * scm_is_call_available passes exactly this (scm.c:1163). */
#define SCM_FNID(service, command) (SCM_SIP | SCM_CALL(service, command))

/* Control (a): is the mapping the hand-reads went through alive at all? */
static void
gic_instrument_check(void)
{
	vm_offset_t gicd = gicd_map();
	uint32_t typer, iidr;

	if (gicd == 0) {
		printf("slpi: m16: the distributor does not map: the control cannot be read\n");
		return;
	}
	typer = rd(gicd + GICD_TYPER);
	iidr = rd(gicd + GICD_IIDR);
	gic_mapping_alive = (typer != 0);
	printf("slpi: m16: the distributor's own identification, read through the very mapping every "
	    "hand-read was made through, reads typer %#010x, iidr %#010x: %s\n", typer, iidr,
	    typer != 0 ? "a distributor that answers: typer encodes how many lines it has and "
	    "cannot read zero, so m13-m15 read real hardware"
	    : "all zeros, which no live distributor can be: the mapping returns nothing, and every "
	    "gic reading m13-m15 made was void, not a state");
}

/* Control (b): the firmware's own availability probe: 1 if it offers the call. */
static int
scm_call_available(unsigned service, unsigned command, uint32_t *available)
{
	uint64_t answer = 0;
	int result = scm("whether a call is offered at all", SCM_CALL(SCM_SVC_INFO, SCM_INFO_CALL_AVAIL),
	    SCM_ONE_VALUE, SCM_FNID(service, command), 0, 0, &answer);

	if (result == 0) {
		*available = (uint32_t) answer;
	}
	return result;
}

/* The two lines, claimed the way this kernel claims every driver's interrupt. */
extern kern_return_t gic_v3_spi_attach(uint32_t intid, bool edge,
    void (*handler)(void *context, uint32_t intid), void *context);
extern uint64_t gic_v3_spi_count(uint32_t intid);

static unsigned slpi_rings_claimed;             /* bits 0 and 1: which line the gic took */
static volatile uint32_t slpi_ring_entered[2];  /* times our own handler was entered: [0] glink, [1] smp2p */

/* Interrupt context: record and get out. */
static void
slpi_ring_arrived(void *context, __unused uint32_t intid)
{
	unsigned which = (unsigned) (uintptr_t) context;

	if (which < 2) {
		slpi_ring_entered[which]++;
	}
}

static void
slpi_rings_claim(void)
{
	const uint32_t lines[2] = { SPI_GLINK_RX, SPI_SMP2P_RX };

	for (unsigned i = 0; i < 2; i++) {
		kern_return_t kr = gic_v3_spi_attach(lines[i], true, slpi_ring_arrived,
		    (void *) (uintptr_t) i);

		if (kr == KERN_SUCCESS) {
			slpi_rings_claimed |= 1u << i;
			printf("slpi: m16: spi %u is ours: the kernel wrote its group, priority, "
			    "edge type and route and unmasked it, the way it does for every "
			    "interrupt a driver takes\n", lines[i]);
		} else {
			printf("slpi: m16: the kernel's own attach refused spi %u: kr %d\n", lines[i],
			    kr);
		}
	}
}

/* What arrived since the claim: the gic's own count through its real dispatch,
 * and our handler's own count. */
static void
slpi_rings_report(const char *when)
{
	uint64_t fired_glink = gic_v3_spi_count(SPI_GLINK_RX), fired_smp2p = gic_v3_spi_count(SPI_SMP2P_RX);

	printf("slpi: m16: %s: the gic's dispatch has taken spi %u %llu times and spi %u %llu times; "
	    "our own handler was entered %u and %u times%s\n", when,
	    SPI_GLINK_RX, fired_glink, SPI_SMP2P_RX, fired_smp2p,
	    slpi_ring_entered[0], slpi_ring_entered[1],
	    (fired_glink + fired_smp2p) != 0 ? ": a ring of the slpi's reached a handler of ours -- "
	    "the registers said nothing to m13-m15 because nothing was ever listening to them" : "");
}

/* M20: time-scope crash observations. The SSC node's watchdog is DT SPI
 * 0x1ee (android-merged.dts), hence absolute INTID 526, not 494. The SFR
 * is sampled read-only: an unchanged old reason cannot date a new crash. */
#define SLPI_WDOG_INTID 526u
static volatile uint32_t slpi_wdog_entered;
static uint64_t slpi_wdog_baseline;
static uint8_t slpi_sfr_baseline[96];
static uint32_t slpi_sfr_baseline_at, slpi_sfr_baseline_size;
static bool slpi_sfr_baseline_valid, slpi_wdog_claimed;

static void
slpi_wdog_arrived(__unused void *context, __unused uint32_t intid)
{
	slpi_wdog_entered++;
}

static void
slpi_crash_baseline(void)
{
	uint32_t size = 0, at = smem_item(ITEM_SLPI_SFR, &size);
	kern_return_t kr = gic_v3_spi_attach(SLPI_WDOG_INTID, true, slpi_wdog_arrived, NULL);

	slpi_wdog_claimed = kr == KERN_SUCCESS;
	slpi_wdog_baseline = gic_v3_spi_count(SLPI_WDOG_INTID);
	printf("slpi: m20: after secure shutdown, watchdog DT SPI 494 -> INTID 526 "
	    "attach kr %d, count baseline %llu; no firmware or shared-memory data changed\n",
	    kr, slpi_wdog_baseline);
	slpi_sfr_baseline_at = at;
	slpi_sfr_baseline_size = size < sizeof(slpi_sfr_baseline) ? size : (uint32_t) sizeof(slpi_sfr_baseline);
	slpi_sfr_baseline_valid = at != 0 && slpi_sfr_baseline_size != 0 &&
	    smem_peek(at, slpi_sfr_baseline, slpi_sfr_baseline_size);
	printf("slpi: m20: after shutdown SFR baseline: item 424 %s, offset %#x, sampled %u bytes; "
	    "the old reason is not cleared or overwritten\n",
	    slpi_sfr_baseline_valid ? "read" : "absent/unreadable", at, slpi_sfr_baseline_size);
}

static void
slpi_crash_report(const char *when)
{
	uint8_t now[sizeof(slpi_sfr_baseline)];
	uint32_t size = 0, at = smem_item(ITEM_SLPI_SFR, &size);
	uint32_t bytes = size < sizeof(now) ? size : (uint32_t) sizeof(now);
	uint64_t count = gic_v3_spi_count(SLPI_WDOG_INTID);
	uint32_t slave_state = 0;
	const char *state_why;
	bool slave_valid = slpi_smp2p_state(&slave_state, &state_why);
	bool readable = at != 0 && bytes != 0 && smem_peek(at, now, bytes);
	bool changed = readable && (!slpi_sfr_baseline_valid || at != slpi_sfr_baseline_at ||
	    bytes != slpi_sfr_baseline_size || memcmp(now, slpi_sfr_baseline, bytes) != 0);

	printf("slpi: m20: %s: watchdog claimed %u, dispatch delta %llu, handler %u; "
	    "SFR readable %u, changed since shutdown baseline %u\n", when, slpi_wdog_claimed,
	    count - slpi_wdog_baseline, slpi_wdog_entered, readable, changed);
	printf("slpi: m20: %s: slave-kernel valid %u, value %#x, fatal %u, ready %u; %s\n",
	    when, slave_valid, slave_state, (slave_state & (1u << SMP2P_FATAL)) != 0,
	    (slave_state & (1u << SMP2P_READY)) != 0, state_why);
	if (changed) {
		uint32_t offset = 0;

		while (offset < bytes && slpi_sfr_baseline_valid && offset < slpi_sfr_baseline_size &&
		    now[offset] == slpi_sfr_baseline[offset]) {
			offset++;
		}
		printf("slpi: m20: SFR item location %#x -> %#x, first differing byte %u; "
		    "a difference is new shared-memory activity, not by itself a decoded fatal cause\n",
		    slpi_sfr_baseline_at, at, offset);
	}
}

/* ---- What the SLPI itself says: its SFR item, and any text it left in shared memory ---------------- */

/*
 * The ssc node's qcom,smem-id = <0x1a8> names the item a subsystem writes when it stops on a
 * fatal error of its own: struct smem_subsystem_item in the vendor's smem.h is a reason, two
 * time stamps, a valid flag, and an 80-byte string, and subsys-restart.c's get_failure_reason()
 * reads exactly those five fields and prints the string. Shared memory always answers and costs
 * nothing to read, so this can only add information: if the slpi's firmware reached its own
 * fatal-error handling, its reason is here.
 *
 * What the hexagon firmware actually writes is not that struct: logs/035630-run.log read item
 * 424 and every field was ascii -- "SFR " where the reason was, "Init: wd" and "og o" where the
 * time stamps and the valid flag were, and "r kernel error suspected." where the string was --
 * which run together from offset 0 as "SFR Init: wdog or kernel error suspected.", the reason a
 * hexagon subsystem restart records when its watchdog bites or its kernel faults. So the item is
 * read as what it is, a tagged string, and the struct fields are printed only when the first
 * word is not text at all.
 */
static void
slpi_sfr_read(void)
{
	uint32_t size = 0, reason = 0, stamp_lo = 0, stamp_hi = 0, valid = 0;
	char raw[96] = { 0 };
	uint32_t at = smem_item(ITEM_SLPI_SFR, &size);

	if (at == 0) {
		printf("slpi: sfr: item %u is not in shared memory: the slpi has left no reason\n",
		    ITEM_SLPI_SFR);
		return;
	}
	if (!smem_peek(at, raw, sizeof(raw) - 1)) {
		printf("slpi: sfr: item %u at smem+%#x cannot be read\n", ITEM_SLPI_SFR, at);
		return;
	}
	for (unsigned i = 0; i < sizeof(raw) - 1; i++) {
		if (raw[i] < ' ' || raw[i] > '~') {
			raw[i] = '\0';
			break;
		}
	}
	if (raw[0] != '\0') {
		printf("slpi: sfr: item %u at smem+%#x reads as text: \"%s\": the reason a hexagon "
		    "subsystem restart records for a watchdog bite or a kernel fault: the slpi got "
		    "far enough to run a kernel with a watchdog, and something it waited for did "
		    "not answer -- for us, most likely its glink edge waiting for a host side that "
		    "does not exist yet (notes/wifi.md §5 M3)\n", ITEM_SLPI_SFR, at, raw);
		return;
	}
	if (!smem_peek(at, &reason, sizeof(reason)) || !smem_peek(at + 4, &stamp_lo, 4) ||
	    !smem_peek(at + 8, &stamp_hi, 4) || !smem_peek(at + 12, &valid, sizeof(valid))) {
		printf("slpi: sfr: item %u at smem+%#x cannot be read\n", ITEM_SLPI_SFR, at);
		return;
	}
	printf("slpi: sfr: item %u at smem+%#x reads reason %u, valid %u, time %#llx: %s\n",
	    ITEM_SLPI_SFR, at, reason, valid,
	    (unsigned long long) (((uint64_t) stamp_hi << 32) | stamp_lo),
	    valid == 0 ? "nothing recorded: the slpi has not said why it stopped, which fits a "
	    "firmware that never reached its fatal-error handler rather than one that stopped and "
	    "said why" :
	    reason == 1 ? "this is the slpi's own reason for stopping, in its own words" :
	    reason == 2 ? "a watchdog bite inside the slpi" :
	    reason == 3 ? "a panic inside the slpi" : "an unrecognised reason code");
}

/*
 * The first bytes of an item, in hex and in ascii. The glink version and ring structures are
 * binary, and the text pass alone calls them empty: logs/035630-run.log listed items 478, 480,
 * 608, 611 and 613 with no printable text in any of them, which is exactly where a glink edge
 * keeps its version exchange and its fifo, and what they hold is the next thing worth reading.
 */
static void
slpi_hexdump(uint32_t at, uint32_t size, unsigned count)
{
	if (count > size) {
		count = (unsigned) size;
	}
	for (uint32_t off = 0; off < count; off += 16) {
		uint8_t bytes[16] = { 0 };
		char ascii[17] = { 0 };
		char hex[3 * 16 + 1] = { 0 };
		size_t used = 0;
		unsigned n = count - off < 16u ? count - off : 16u;

		if (!smem_peek(at + off, bytes, n)) {
			return;
		}
		for (unsigned i = 0; i < n; i++) {
			used += (size_t) snprintf(hex + used, sizeof(hex) - used, "%02x ", bytes[i]);
			ascii[i] = (bytes[i] >= ' ' && bytes[i] <= '~') ? (char) bytes[i] : '.';
		}
		printf("slpi: scan:   +%#04x: %s |%s|\n", off, hex, ascii);
	}
}

/*
 * What else the slpi wrote into the shared partition: every item in it, any run of printable
 * text inside them, and the first bytes of each in hex. The items are what its drivers allocate
 * -- glink channels, log buffers, whatever its firmware publishes -- and a firmware that prints
 * leaves printable text in memory the ap can read, so this is the closest thing to the slpi's
 * console that exists without a ramdump channel. Every item is named, text or not: an item that
 * was not there in an earlier round is itself the news -- logs/034623-run.log listed one item,
 * ours, and logs/035630-run.log listed seven, the slpi's smp2p item and five glink ones beside
 * it -- and the hex is what tells an allocated ring from an empty one.
 */
static void
slpi_partition_text(void)
{
	struct smem_item found[256];
	uint32_t cacheline = 0, partition_size = 0;
	uint32_t partition = smem_partition(SMEM_US, SMEM_SLPI, &cacheline, &partition_size);
	unsigned count = smem_items(partition, cacheline, found, 256);

	printf("slpi: scan: the shared partition at smem+%#x holds %u items: listing them, any "
	    "text inside them, and their first bytes in hex\n", partition, count);
	for (unsigned i = 0; i < count; i++) {
		char run[96] = { 0 };
		unsigned length = 0, shown = 0;

		printf("slpi: scan: item %u at smem+%#x, %u bytes%s\n", found[i].item, found[i].offset,
		    found[i].size, found[i].item == ITEM_SLPI_SMP2P ? ", the slpi's smp2p item" :
		    found[i].item == ITEM_SLPI_SMP2P_OUT ? ", ours" : "");
		if (found[i].size == 0 || found[i].size > 0x40000) {
			continue;
		}
		slpi_hexdump(found[i].offset, found[i].size, 64);
		for (uint32_t at = 0; at <= found[i].size; at++) {
			uint8_t byte = 0;

			if (at < found[i].size && !smem_peek(found[i].offset + at, &byte, 1)) {
				break;
			}
			if (at < found[i].size && byte >= ' ' && byte <= '~') {
				if (length < sizeof(run) - 1) {
					run[length++] = (char) byte;
				}
				continue;
			}
			if (length >= 8 && shown++ < 4) {
				run[length] = '\0';
				printf("slpi: scan:   text: \"%s\"\n", run);
			}
			length = 0;
		}
	}
	printf("slpi: scan: done\n");
}

/* ---- M4: the host side of the slpi's glink edge ---------------------------------------------------- */

/*
 * The "slave-kernel" ready bit has never come in any round because the channel it waits for has
 * no other end: the ap has never had a glink edge. logs/035630-run.log and logs/040912-run.log
 * read the slpi's side and found it built and idle -- item 478, glink's descriptor, 32 bytes of
 * zeroes, and item 480, its queue, 16 KiB of zeroes whose head is still zero, so it has sent
 * nothing -- while item 479, our queue, is missing because our kernel has never allocated it.
 * The device tree says what the edge is: the glink node's dsps child, transport "smem",
 * mboxes = <0xb1 0x18> where 0xb1 is mailbox@17c00000, the apcs global block
 * (android-merged.dts:4107-4135, :3988-3992) -- and its inbound is GIC interrupt 0xaa, which
 * this first round polls for instead of taking. Its channels are "IPCRTR"
 * with intents <0x800 0x05 0x2000 0x03 0x4400 0x02> (qcom,slpi_qrtr), fastrpcglink-apps-dsp
 * and glink_ssr.
 *
 * The protocol is the one drivers/wifi/hakios.wifi.c already speaks with redwood's wpss, which is
 * the vendor's glink native protocol: the queues' four marks live in the descriptor -- our
 * queue's tail (theirs to move) at +0 and head (ours) at +4, their queue's tail (ours) at +8 and
 * head (theirs) at +12 -- and through the queues go eight-byte messages, a command and two
 * parameters, some with more behind them, each padded to eight: first the two sides' versions,
 * then channels by name, each side giving a channel a number of its own, then intents, each
 * side's promise of a buffer a message may arrive in, and then data. The host starts: it
 * allocates its queue, writes its version, and rings. The slpi's glink_ssmem driver has waited
 * through every round so far for exactly that, which is why this round does not start the slpi
 * again -- the instance logs/040912-run.log started is still running -- but goes looking for its
 * edge where it left it.
 */
#define ITEM_GLINK_DESCRIPTOR   478
#define ITEM_GLINK_OUR_QUEUE    479
#define ITEM_GLINK_THEIR_QUEUE  480
#define GLINK_OUR_TAIL          0               /* how far the slpi has read our queue (its to move) */
#define GLINK_OUR_HEAD          4               /* how far we have written it */
#define GLINK_THEIR_TAIL        8               /* how far we have read the slpi's queue */
#define GLINK_THEIR_HEAD        12              /* how far it has written it (its to move) */
#define GLINK_RESERVE           16              /* of our queue, never filled (Linux's reserve, and room for one short message) */
#define GLINK_QUEUE_SIZE        0x4000          /* what the slpi's item 480 is: Linux's GLINK_SMEM_DEFAULT_FIFO_LEN */
/*
 * The doorbell. logs/043238-run.log and logs/044710-run.log both wrote zero to 0x17c00060,
 * reading the DT's cell 0x18 as a register offset, and the slpi's driver never once read our
 * queue -- it answered what it had already been sent and sat. The vendor's apcs mailbox driver
 * says what the cell means: qcom-apcs-ipc-mailbox.c gives qcom,sm8150-apcs-hmss-global the
 * offset 12, so its probe sets reg = base + 12, and its send_data is writel(BIT(idx),
 * apcs->reg) with idx the cell. So 0x18 is bit 0x18 of the apcs smp2p status register at
 * 0x17c0000c -- the same register the smp2p kick's bit 0x1a lives in (slpi_smp2p_kick), and
 * the write sets only the bits it writes, so bit 0x1a is untouched.
 */
#define APCS_GLINK_KICK_OFFSET  0x0c
#define APCS_GLINK_KICK_BIT     (1u << 0x18)

enum {
	GLINK_VERSION = 0, GLINK_VERSION_ACK = 1, GLINK_OPEN = 2, GLINK_CLOSE = 3, GLINK_OPEN_ACK = 4, GLINK_INTENT = 5,
	GLINK_DONE = 6, GLINK_INTENT_REQUEST = 7, GLINK_INTENT_REQUEST_ACK = 8, GLINK_DATA = 9, GLINK_CLOSE_ACK = 11,
	GLINK_DATA_MORE = 12, GLINK_READ_NOTIFY = 13, GLINK_DONE_REUSE = 14, GLINK_SIGNALS = 15,
};
#define GLINK_VERSION_1         1
#define GLINK_INTENT_REUSE      1u              /* the feature Linux offers over this transport */
#define GLINK_CHANNEL           "IPCRTR"
#define GLINK_US                1               /* our number for that channel */

struct glink_message {
	uint16_t command, param1;
	uint32_t param2;
};

/*
 * What the edge has reached. One thread touches all of this, so there is nothing to lock; the
 * four marks in the descriptor are the only state the other side shares.
 */
static struct {
	uint32_t descriptor, ours, ours_size, theirs, theirs_size;      /* where in the shared memory */
	bool up;                                /* the queues are found and ours to use */
	bool broken;                            /* something in the slpi's queue was not understood: nothing more is read */
	uint32_t features;
	bool agreed;                            /* the versions are settled */
	uint32_t their_channel;                 /* the slpi's number for IPCRTR; 0: it has not opened it */
	bool asked, acknowledged, open;         /* our open sent; acknowledged; both ways done and our intents told */
	struct {
		uint32_t size, fill;
		uint8_t *data;
	} our_intents[24];                      /* what the slpi may send into; an intent's number is its place here + 1 */
	uint32_t our_intent_count, their_intent_count;
	uint32_t said, heard;                   /* messages each way */
	uint32_t data_count, data_bytes;        /* whole data messages on the channel, and their bytes */
	uint8_t data_first[64];                 /* the first of their bytes, for the log: the qrtr hello should be in them */
	uint32_t data_first_len;
} glink;

static uint32_t
glink_word(uint32_t offset)
{
	uint32_t value = 0;

	(void) smem_peek(offset, &value, sizeof(value));
	return value;
}

static void
glink_mark(uint32_t offset, uint32_t value)
{
	(void) smem_poke(offset, &value, sizeof(value));
	settle();
}

/*
 * Ring the slpi's glink doorbell: the edge's mboxes = <0xb1 0x18> is bit 0x18 of the apcs smp2p
 * status register at 0x17c0000c, the way the vendor's apcs mailbox driver reads that cell (see
 * APCS_GLINK_KICK_OFFSET); the glink smem driver rings after every mark it moves. The settle()
 * before the write is its wmb: the queue was written through a write-combining mapping and the
 * doorbell goes through a device one.
 */
static void
glink_kick(const char *why)
{
	static vm_offset_t apcs;

	if (apcs == 0) {
		apcs = ml_io_map(APCS_PHYS, 0x1000);
	}
	if (apcs == 0) {
		printf("slpi: m4: the apcs block at %#x does not map: the slpi cannot be told\n", APCS_PHYS);
		return;
	}
	settle();
	wr(apcs + APCS_GLINK_KICK_OFFSET, APCS_GLINK_KICK_BIT);
	settle();
	printf("slpi: m4: bit 0x18 of %#x is written: the slpi's glink driver is told, because %s\n",
	    APCS_PHYS + APCS_GLINK_KICK_OFFSET, why);
}

static const char *
glink_name(unsigned command)
{
	static const char *const names[] = {
		"version", "version-ack", "open", "close", "open-ack", "intent", "done", "intent-request", "intent-request-ack",
		"data", "10", "close-ack", "data-more", "read-notify", "done-reuse", "signals"
	};

	return command < sizeof(names) / sizeof(names[0]) ? names[command] : "?";
}

/*
 * The two items the slpi made; false if either is missing or not of a shape this knows. Ours is
 * not looked for here: slpi_glink_make_queue() below allocates it, which is the whole point of
 * M4, and must be able to tell "the slpi's edge was never built" from "it was, and now we have
 * our end too".
 */
static bool
slpi_glink_find(void)
{
	uint32_t size = 0;

	glink.descriptor = smem_item(ITEM_GLINK_DESCRIPTOR, &size);
	if (glink.descriptor == 0 || size != 32) {
		printf("slpi: m4: glink's descriptor (item %u) is %s: the slpi's edge is not there\n",
		    ITEM_GLINK_DESCRIPTOR, size == 0 ? "not in shared memory" : "not 32 bytes");
		return false;
	}
	glink.theirs = smem_item(ITEM_GLINK_THEIR_QUEUE, &glink.theirs_size);
	if (glink.theirs == 0 || glink.theirs_size < 1024 || (glink.theirs_size & 7)) {
		printf("slpi: m4: the slpi's queue (item %u) is %s: its edge is not usable\n",
		    ITEM_GLINK_THEIR_QUEUE, glink.theirs == 0 ? "not in shared memory" : "not of a usable size");
		return false;
	}
	/* Every index below is a remote-chosen head or tail modulo a queue size: each item has to
	 * lie wholly inside the 2 MB mapping, or an index would read outside it. */
	return (uint64_t) glink.descriptor + 32 <= SMEM_SIZE && (uint64_t) glink.theirs + glink.theirs_size <= SMEM_SIZE;
}

/* Our queue, allocated the way the slpi's driver would allocate it, or found as old as its own. */
static bool
slpi_glink_make_queue(void)
{
	uint32_t size = 0;

	if (slpi_smem_alloc(ITEM_GLINK_OUR_QUEUE, GLINK_QUEUE_SIZE, "m4") == 0) {
		return false;
	}
	glink.ours = smem_item(ITEM_GLINK_OUR_QUEUE, &size);
	if (glink.ours == 0 || size < 1024 || (size & 7) || (uint64_t) glink.ours + size > SMEM_SIZE) {
		printf("slpi: m4: item %u is there but not of a usable size: nothing to send on\n",
		    ITEM_GLINK_OUR_QUEUE);
		return false;
	}
	glink.ours_size = size;
	return true;
}

/*
 * slpi=10: the edge wiped clean while the instance is still in reset.
 *
 * Four instances have come out of a reset here, and how much of its glink init each one
 * completed lines up with one thing only: what its own queue held when it came out. The
 * instance logs/043646-run.log started found item 478 all zeroes and item 480 empty, and it
 * is the only one that ever announced its version: it wrote it at offset 0 and moved its own
 * head to 8, and logs/044710-run.log read that version and answered it. The instance
 * logs/050214-run.log started found that version still at offset 0 with the head at 8: its
 * init moved the head back to 0 -- a fifo reset, and nothing of its own was ever written:
 * the message logs/051227-run.log read at offset 0 was the old one, still lying there -- and
 * it stayed quiet. The instance logs/053241-run.log started found that same old message with
 * the head already at 0, and its init, which by then had already run and passed -- an
 * instance's init is under a second old at its reset, as logs/060328-run.log later showed
 * with its first poll; the 60 s and 35 s logs/035630-run.log and logs/050214-run.log seemed
 * to show were only the moments their own scans looked -- had nothing left to make visible,
 * and the sentinel set a minute later watched a
 * mark no one was going to touch again. An instance that finds an old message where a fresh
 * edge has zeroes resets the fifo and stays quiet: the vendor's own subsystem-restart
 * recovery expects the host to speak first after a restart, and the one instance that found
 * an empty queue is the one instance that spoke. So this writes zeroes over the descriptor
 * and both queues -- the state the instance that spoke started from -- while the old instance
 * is powered off and the new one is still held in reset: between the AOP's "off" and the
 * auth-and-reset call both are true, and an idle driver that has written nothing for eight
 * minutes writes nothing here. The watch that follows reads their_head from the moment of the
 * reset: it reads 0 by construction, so any value above 0 is the new instance's own
 * announcement and no sentinel can overwrite one.
 */
static void
slpi_glink_clean(void)
{
	uint8_t *zeroes;
	uint32_t size = 0, ours;

	if (!slpi_glink_find()) {
		printf("slpi: m10: there is no glink edge to clean: its items are not in shared memory, "
		    "and a fresh instance will build one from nothing\n");
		return;
	}
	zeroes = kalloc_data(GLINK_QUEUE_SIZE, Z_WAITOK | Z_ZERO);
	if (zeroes == NULL) {
		printf("slpi: m10: no memory to clean the edge with\n");
		return;
	}
	(void) smem_poke(glink.descriptor, zeroes, 32);
	(void) smem_poke(glink.theirs, zeroes,
	    glink.theirs_size < GLINK_QUEUE_SIZE ? glink.theirs_size : GLINK_QUEUE_SIZE);
	settle();
	printf("slpi: m10: the descriptor at smem+%#x and the slpi's queue at smem+%#x are zeroes now: "
	    "the state the one instance that announced its version started from\n",
	    glink.descriptor, glink.theirs);
	ours = smem_item(ITEM_GLINK_OUR_QUEUE, &size);
	if (ours != 0 && size != 0) {
		(void) smem_poke(ours, zeroes, size < GLINK_QUEUE_SIZE ? size : GLINK_QUEUE_SIZE);
		settle();
		printf("slpi: m10: our queue at smem+%#x is zeroes too: an old round's unread bytes are "
		    "not what a fresh instance should find waiting\n", ours);
	}
	kfree_data(zeroes, GLINK_QUEUE_SIZE);
}

/* How much of the slpi's queue is unread. */
static uint32_t
glink_waiting(void)
{
	uint32_t head = glink_word(glink.descriptor + GLINK_THEIR_HEAD), tail = glink_word(glink.descriptor + GLINK_THEIR_TAIL);

	if (head >= glink.theirs_size || tail >= glink.theirs_size) {
		return 0;
	}
	return head >= tail ? head - tail : glink.theirs_size - tail + head;
}

static void
glink_peek(void *to, uint32_t offset, uint32_t count)
{
	uint32_t at = (glink_word(glink.descriptor + GLINK_THEIR_TAIL) + offset) % glink.theirs_size;

	for (uint32_t i = 0; i < count; i++) {
		((uint8_t *) to)[i] = smem[glink.theirs + (at + i) % glink.theirs_size];
	}
}

static void
glink_advance(uint32_t count)
{
	glink_mark(glink.descriptor + GLINK_THEIR_TAIL,
	    (glink_word(glink.descriptor + GLINK_THEIR_TAIL) + count) % glink.theirs_size);
}

/*
 * The other side's marks, reset: an instance's glink init rewrites the mark its driver owns,
 * and logs/051227-run.log watched one do exactly that -- their_head, 8 when that run started,
 * read 0 at its scan, written by the instance from logs/050214-run.log. What sat at offset 0
 * of its queue was the earlier instance's version, still lying there: an instance that finds
 * an old message there resets the fifo and writes nothing of its own (see slpi_glink_clean).
 * A mark outside the queue can only be that init or our own sentinel;
 * and a head behind our read cursor with zeroes sitting at the cursor is the same thing: a
 * writer that had wrapped would have left real messages there, and no glink message is all
 * zeroes. Our side of the pair is only meaningful with theirs, so when theirs resets ours goes
 * with it: our read cursor joins the new head, and our write mark, if the init moved it out of
 * range or behind where they have read to, rejoins their tail. True when something was
 * realigned: the caller starts over.
 */
static bool
glink_theirs_realigned(void)
{
	uint32_t head = glink_word(glink.descriptor + GLINK_THEIR_HEAD), tail = glink_word(glink.descriptor + GLINK_THEIR_TAIL);
	uint32_t our_head = glink_word(glink.descriptor + GLINK_OUR_HEAD), our_tail = glink_word(glink.descriptor + GLINK_OUR_TAIL);
	static const uint8_t zeroes[sizeof(struct glink_message)];
	struct glink_message at;
	bool moved = false;

	glink_peek(&at, 0, sizeof(at));
	if (tail < glink.theirs_size && (head >= glink.theirs_size || head < tail) &&
	    memcmp(&at, zeroes, sizeof(at)) == 0) {
		glink_mark(glink.descriptor + GLINK_THEIR_TAIL, head < glink.theirs_size ? head : 0);
		moved = true;
	}
	if ((our_head >= glink.ours_size || our_head < our_tail) && our_tail < glink.ours_size) {
		glink_mark(glink.descriptor + GLINK_OUR_HEAD, our_tail);
		moved = true;
	}
	if (moved) {
		printf("slpi: m4: the slpi's marks at smem+%#x were %u and %u, ours %u and %u: its glink init "
		    "reset them, so our side is realigned to match -- an instance's init zeroes the marks its "
		    "driver owns, and its reader's marks and ours only mean anything with them\n",
		    glink.descriptor, head, tail, our_head, our_tail);
	}
	return moved;
}

/* How much we may write into our queue now. */
static uint32_t
glink_room(void)
{
	uint32_t head = glink_word(glink.descriptor + GLINK_OUR_HEAD), tail = glink_word(glink.descriptor + GLINK_OUR_TAIL), room;

	if (head >= glink.ours_size || tail >= glink.ours_size) {
		return 0;
	}
	room = tail <= head ? glink.ours_size - head + tail : tail - head;
	return room < GLINK_RESERVE ? 0 : room - GLINK_RESERVE;
}

/* A message into our queue, in two pieces, padded to eight bytes; then our mark, and the ring. */
static bool
glink_put(const void *first, uint32_t first_length, const void *second, uint32_t second_length)
{
	uint32_t head = glink_word(glink.descriptor + GLINK_OUR_HEAD), length = first_length + second_length;
	uint32_t padded = (length + 7) & ~7u;

	if (!glink.up || glink_room() < padded) {
		return false;
	}
	for (uint32_t i = 0; i < padded; i++) {
		smem[glink.ours + (head + i) % glink.ours_size] = i < first_length ? ((const uint8_t *) first)[i] :
		    i < length ? ((const uint8_t *) second)[i - first_length] : 0;
	}
	settle();
	glink_mark(glink.descriptor + GLINK_OUR_HEAD, (head + padded) % glink.ours_size);
	glink_kick("we have written its queue");
	glink.said++;
	return true;
}

/* A message of ours. */
static void
glink_say(unsigned command, uint32_t param1, uint32_t param2, const void *more, uint32_t more_length)
{
	struct glink_message message = { .command = (uint16_t) command, .param1 = (uint16_t) param1, .param2 = param2 };
	bool sent = glink_put(&message, sizeof(message), more, more_length);

	printf("slpi: m4: > %s %u %#x%s\n", glink_name(command), param1, param2,
	    sent ? "" : ": NO ROOM IN OUR QUEUE, not sent");
}

/* An intent of ours of that size: a buffer, and the slpi told of it. False: there are no more. */
static bool
glink_offer(uint32_t size)
{
	uint32_t words[2] = { size, glink.our_intent_count + 1 }, i = glink.our_intent_count;

	if (i >= sizeof(glink.our_intents) / sizeof(glink.our_intents[0]) || size == 0 || size > 0x10000) {
		return false;
	}
	glink.our_intents[i].data = kalloc_data(size, Z_WAITOK | Z_ZERO);
	if (glink.our_intents[i].data == NULL) {
		printf("slpi: m4: no memory for an intent of %u bytes: the offer is refused\n", size);
		return false;
	}
	glink.our_intents[i].size = size;
	glink.our_intents[i].fill = 0;
	glink.our_intent_count++;
	glink_say(GLINK_INTENT, GLINK_US, 1, words, sizeof(words));
	return true;
}

/*
 * Everything the slpi has put into its queue, taken out and acted on: Linux's
 * qcom_glink_native_rx(). It must not wait itself: whoever asks for what the slpi sent calls it.
 */
static void
glink_pump(void)
{
	static const struct {
		uint32_t size, count;
	} intents[] = { { 0x800, 5 }, { 0x2000, 3 }, { 0x4400, 2 } };       /* dt: qcom,intents of qcom,slpi_qrtr */
	struct glink_message message;
	uint32_t words[4];
	char name[33];

	while (glink.up && !glink.broken) {
		uint32_t waiting = glink_waiting(), length;
		/* 64 bits: the lengths below come from the slpi, and in 32 bits their sums wrap to 0 and the message is never consumed */
		uint64_t wide = sizeof(message);

		if (waiting < sizeof(message)) {
			return;
		}
		glink_peek(&message, 0, sizeof(message));
		if (memcmp(&message, "\0\0\0\0\0\0\0\0", sizeof(message)) == 0) {
			/* Zeroes are not a message: no command is zero with both parameters zero, and a
			 * version of 0 is not a version. logs/051227-run.log read 2,047 of these as
			 * "version 0 0" and answered every one, because their_head 0 under their_tail 8
			 * looked like a wrap and was really the instance's init resetting its marks: the
			 * queue was read empty and 16,368 bytes of ours piled up unanswered. So a zero
			 * at the cursor ends the reading -- and if the marks show that reset, our side
			 * realigns to it and the read starts over from where the instance restarted. */
			if (glink_theirs_realigned()) {
				continue;
			}
			return;
		}
		if (message.command == GLINK_OPEN) {
			wide += message.param2;
		} else if (message.command == GLINK_INTENT) {
			wide += 8ull * message.param2;
		} else if (message.command == GLINK_DATA || message.command == GLINK_DATA_MORE) {
			if (waiting < 16) {
				return;
			}
			glink_peek(words, 8, 8);                /* the size of this piece, and what is still to come */
			wide = 16ull + words[0];
		}
		wide = (wide + 7) & ~7ull;
		if (wide > glink.theirs_size - 8) {
			printf("slpi: m4: A MESSAGE OF %llu BYTES (%s %u %#x): not believed, and nothing more is read\n",
			    wide, glink_name(message.command), message.param1, message.param2);
			glink.broken = true;
			return;
		}
		length = (uint32_t) wide;
		if (waiting < length) {
			return;                                 /* (the rest of it is on its way) */
		}
		glink.heard++;
		if (message.command != GLINK_DATA && message.command != GLINK_DATA_MORE) {
			printf("slpi: m4: < %s %u %#x\n", glink_name(message.command), message.param1, message.param2);
		}
		switch (message.command) {
		case GLINK_VERSION:
			if (message.param1 == GLINK_VERSION_1) {
				glink.features &= message.param2;
			}
			glink_advance(length);
			glink_say(GLINK_VERSION_ACK, GLINK_VERSION_1, glink.features, NULL, 0);
			break;
		case GLINK_VERSION_ACK:
			glink_advance(length);
			if (message.param1 == GLINK_VERSION_1 && message.param2 == glink.features) {
				glink.agreed = true;
				printf("slpi: m4: the versions are agreed: version %u, features %#x\n",
				    GLINK_VERSION_1, glink.features);
			} else if (message.param1 != 0) {
				glink.features &= message.param2;
				glink_say(GLINK_VERSION, GLINK_VERSION_1, glink.features, NULL, 0);
			}
			break;
		case GLINK_OPEN:
			bzero(name, sizeof(name));
			glink_peek(name, sizeof(message), message.param2 < sizeof(name) - 1 ? message.param2 : sizeof(name) - 1);
			glink_advance(length);
			if (strcmp(name, GLINK_CHANNEL) != 0) {
				printf("slpi: m4: the slpi opens \"%s\" as its channel %u: left unanswered, nobody here speaks it\n",
				    name, message.param1);
				break;
			}
			printf("slpi: m4: the slpi opens \"%s\" as its channel %u: acknowledged, and opened from our side as "
			    "channel %u\n", name, message.param1, GLINK_US);
			glink.their_channel = message.param1;
			glink_say(GLINK_OPEN_ACK, glink.their_channel, 0, NULL, 0);
			if (!glink.asked) {
				bzero(name, sizeof(name));
				strlcpy(name, GLINK_CHANNEL, sizeof(name));
				glink_say(GLINK_OPEN, GLINK_US, sizeof(GLINK_CHANNEL), name, (sizeof(GLINK_CHANNEL) + 7) & ~7u);
				glink.asked = true;
			}
			break;
		case GLINK_OPEN_ACK:
			glink_advance(length);
			if (message.param1 == GLINK_US) {
				glink.acknowledged = true;
			}
			break;
		case GLINK_INTENT:
			for (uint32_t i = 0; i < message.param2 && message.param1 == glink.their_channel; i++) {
				glink_peek(words, 8 + 8 * i, 8);        /* a size and a number */
				glink.their_intent_count++;
				printf("slpi: m4:   the slpi will take %u bytes into its intent %u\n", words[0], words[1]);
			}
			glink_advance(length);
			break;
		case GLINK_DONE:
		case GLINK_DONE_REUSE:
		case GLINK_INTENT_REQUEST_ACK:
		case GLINK_CLOSE_ACK:
		case GLINK_SIGNALS:
			glink_advance(length);
			break;
		case GLINK_INTENT_REQUEST:
			glink_advance(length);
			if (message.param1 == glink.their_channel) {
				bool granted = glink_offer(message.param2);

				glink_say(GLINK_INTENT_REQUEST_ACK, GLINK_US, granted, NULL, 0);
			}
			break;
		case GLINK_CLOSE:
			glink_advance(length);
			glink_say(GLINK_CLOSE_ACK, message.param1, 0, NULL, 0);
			if (message.param1 == glink.their_channel) {
				printf("slpi: m4: THE SLPI CLOSES \"%s\"\n", GLINK_CHANNEL);
				glink.open = false;
				glink.their_channel = 0;
			}
			break;
		case GLINK_DATA:
		case GLINK_DATA_MORE: {
			uint32_t intent = message.param2 - 1;

			if (message.param1 != glink.their_channel || intent >= glink.our_intent_count ||
			    words[0] > glink.our_intents[intent].size - glink.our_intents[intent].fill) {
				printf("slpi: m4: < data of %u bytes on channel %u for intent %u: NOT OURS OR TOO MUCH, dropped\n",
				    words[0], message.param1, message.param2);
				glink_advance(length);
				break;
			}
			for (uint32_t at = 0; at < words[0]; at++) {
				glink_peek(glink.our_intents[intent].data + glink.our_intents[intent].fill + at, 16 + at, 1);
			}
			glink.our_intents[intent].fill += words[0];
			glink_advance(length);
			if (words[1] == 0) {
				/* whole: counted, its first bytes kept for the log, and the intent is the slpi's to use again */
				glink.data_count++;
				glink.data_bytes += glink.our_intents[intent].fill;
				if (glink.data_first_len == 0) {
					glink.data_first_len = glink.our_intents[intent].fill < sizeof(glink.data_first) ?
					    glink.our_intents[intent].fill : (uint32_t) sizeof(glink.data_first);
					memcpy(glink.data_first, glink.our_intents[intent].data, glink.data_first_len);
				}
				glink.our_intents[intent].fill = 0;
			}
			break;
		}
		default:
			printf("slpi: m4: A MESSAGE THIS DOES NOT KNOW (%u, %u, %#x): nothing more is read\n",
			    message.command, message.param1, message.param2);
			glink.broken = true;
			return;
		}
		if (glink.their_channel != 0 && glink.acknowledged && !glink.open) {
			glink.open = true;
			printf("slpi: m4: THE CHANNEL \"%s\" IS OPEN both ways (ours %u, the slpi's %u); telling the slpi "
			    "our intents\n", GLINK_CHANNEL, GLINK_US, glink.their_channel);
			for (unsigned i = 0; i < sizeof(intents) / sizeof(intents[0]); i++) {
				for (unsigned n = 0; n < intents[i].count; n++) {
					glink_offer(intents[i].size);
				}
			}
		}
	}
}

/*
 * Open the edge and talk to the slpi for wait_seconds: the version exchange, the channel, the
 * intents, and whatever data its qrtr side sends once the channel is open both ways. Polling,
 * every 200 ms -- the edge's interrupt, GIC SPI 0xaa, is not wired up here -- and the version is
 * re-sent every ten seconds up to five times: if the slpi's driver missed the first ring, or the
 * instance that built the edge has died, the second and the fifth find the same silence, and
 * saying so is an answer this round can use. False: there is no edge to talk over.
 */
static void
slpi_glink_passive(uint32_t wait_seconds)
{
	uint64_t interval = 0, began = mach_absolute_time(), deadline, next = began;
	uint64_t second = 0;
	unsigned sample = 0;

	clock_interval_to_absolutetime_interval(wait_seconds, NSEC_PER_SEC, &interval);
	clock_interval_to_absolutetime_interval(1, NSEC_PER_SEC, &second);
	deadline = began + interval;
	printf("slpi: m21: no host GLINK messages or receive-pump cursor updates; "
	    "watching for %u real seconds after announcement and SMP2P ack\n", wait_seconds);
	for (;;) {
		uint64_t now = mach_absolute_time();

		if (now >= next || now >= deadline) {
			uint32_t marks[4] = { 0 };

			if (smem_peek(glink.descriptor, marks, sizeof(marks))) {
				printf("slpi: m21: sample %u at %llu ms: host tail %u head %u, "
				    "remote tail %u head %u; host messages sent %u\n", sample,
				    (now - began) * 1000 / second, marks[0], marks[1], marks[2], marks[3], glink.said);
			}
			slpi_crash_report("M21 passive observation");
			sample++;
			next = now + 5 * second;
		}
		if (now >= deadline) {
			break;
		}
		sleep_ms(100);
	}
	printf("slpi: m21: passive observation complete; no host GLINK exchange was started\n");
}

static bool
slpi_glink_try(uint32_t wait_seconds)
{
	uint32_t last_resend = 0, resends = 0;

	printf("slpi: m4: the host side of the slpi's glink edge: item %u is ours to allocate, and after that "
	    "the protocol is the one drivers/wifi/hakios.wifi.c speaks with redwood's wpss\n",
	    ITEM_GLINK_OUR_QUEUE);
	if (!slpi_glink_find()) {
		return false;
	}
	printf("slpi: m4: the slpi's descriptor is at smem+%#x and its queue at smem+%#x, %u bytes\n",
	    glink.descriptor, glink.theirs, glink.theirs_size);
	if (!slpi_glink_make_queue()) {
		return false;
	}
	printf("slpi: m4: our queue is at smem+%#x, %u bytes\n", glink.ours, glink.ours_size);
	/* Before anything is read or written, the marks have to agree: if the instance's init
	 * reset them (or slpi=9's sentinel is still in one), our side realigns to that first --
	 * glink_put below would otherwise find no room, and glink_pump would read the old
	 * offsets of a queue that restarted at zero. */
	(void) glink_theirs_realigned();
	/* The queue may hold complete messages from an earlier round -- logs/043238-run.log left
	 * five versions -- and zeroing one of our two marks would strand the other: the pair is
	 * only meaningful together, and the slpi's driver reads from its mark to ours. If it has
	 * read them all, the queue is empty and we continue from that offset; if it has not, they
	 * are valid messages and its answer to them is as good an answer as any. */
	if (glink_word(glink.descriptor + GLINK_OUR_TAIL) != glink_word(glink.descriptor + GLINK_OUR_HEAD)) {
		printf("slpi: m4: %u bytes of ours are unread from offset %u: they are complete messages from an "
		    "earlier round, left for the slpi's driver to answer\n",
		    (glink_word(glink.descriptor + GLINK_OUR_HEAD) + glink.ours_size -
		     glink_word(glink.descriptor + GLINK_OUR_TAIL)) % glink.ours_size,
		    glink_word(glink.descriptor + GLINK_OUR_TAIL));
	} else if (glink_word(glink.descriptor + GLINK_OUR_HEAD) != 0) {
		printf("slpi: m4: the queue is empty at offset %u: continuing from there\n",
		    glink_word(glink.descriptor + GLINK_OUR_HEAD));
	}
	glink.features = GLINK_INTENT_REUSE;
	glink.up = true;
	glink_say(GLINK_VERSION, GLINK_VERSION_1, glink.features, NULL, 0);
	printf("slpi: m4: the version is out and its doorbell rung; polling its queue for %u s\n", wait_seconds);
	for (uint32_t waited = 0; waited < wait_seconds * 1000; waited += 200) {
		sleep_ms(200);
		glink_pump();
		if (slpi_level == 20 && waited % 10000 == 0) {
			slpi_crash_report("during GLINK exchange");
		}
		if (glink.agreed && !glink.asked) {
			char name[33];

			/* the pump opens our side when the slpi's open comes; if it answered our version
			 * without ever opening the channel itself, we ask first */
			bzero(name, sizeof(name));
			strlcpy(name, GLINK_CHANNEL, sizeof(name));
			glink_say(GLINK_OPEN, GLINK_US, sizeof(GLINK_CHANNEL), name, (sizeof(GLINK_CHANNEL) + 7) & ~7u);
			glink.asked = true;
		}
		if (!glink.agreed && !glink.broken && waited > 0 && waited - last_resend >= 10000 && resends < 5) {
			last_resend = waited;
			resends++;
			printf("slpi: m4: no answer in ten seconds: the version goes out again (%u of 5)\n", resends);
			glink_say(GLINK_VERSION, GLINK_VERSION_1, glink.features, NULL, 0);
		}
		if (glink.data_count >= 1 || glink.broken) {
			break;
		}
	}
	printf("slpi: m4: after %u s: %u messages sent, %u heard; versions %s; channel \"%s\" %s; "
	    "intents ours %u, theirs %u; data %u messages %u bytes%s\n",
	    wait_seconds, glink.said, glink.heard,
	    glink.agreed ? "agreed" : "not answered",
	    GLINK_CHANNEL, glink.open ? "is open both ways" : (glink.their_channel != 0 ? "half open" : "never opened"),
	    glink.our_intent_count, glink.their_intent_count, glink.data_count, glink.data_bytes,
	    glink.broken ? ": BROKEN, an unknown message stopped the queue" : "");
	if (glink.data_first_len != 0) {
		printf("slpi: m4: the first data message, %u of its bytes:\n", glink.data_first_len);
		for (uint32_t at = 0; at < glink.data_first_len; at += 16) {
			char text[17];

			printf("slpi: m4:   +%04x:", at);
			for (uint32_t i = 0; i < 16; i++) {
				printf(" %02x", at + i < glink.data_first_len ? glink.data_first[at + i] : 0);
				text[i] = at + i < glink.data_first_len && glink.data_first[at + i] >= ' ' &&
				    glink.data_first[at + i] <= '~' ? (char) glink.data_first[at + i] : '.';
			}
			text[16] = '\0';
			printf("  |%s|\n", text);
		}
	}
	/* The marks as they stand when we are done: the evidence, and the state the next round
	 * would find if this one ends before the slpi's driver is done with the queues. */
	printf("slpi: m4: the descriptor at smem+%#x reads: our queue read to %u and written to %u, the slpi's "
	    "read to %u and written to %u\n", glink.descriptor,
	    glink_word(glink.descriptor + GLINK_OUR_TAIL), glink_word(glink.descriptor + GLINK_OUR_HEAD),
	    glink_word(glink.descriptor + GLINK_THEIR_TAIL), glink_word(glink.descriptor + GLINK_THEIR_HEAD));
	if (!glink.agreed) {
		printf("slpi: m4: the version went unanswered: no instance has ever read our queue -- "
		    "logs/060328-run.log rang the right doorbell at a fresh instance's freshest moment "
		    "with a clean edge and our_tail still never moved -- so its rx is the question, and "
		    "its interrupt configuration is only readable in the slpi's own firmware "
		    "(notes/wifi.md §5)\n");
	}
	return glink.agreed;
}

/*
 * slpi=9: start a fresh instance and wait, in this one run, for the moment its glink init
 * rewrites the descriptor. logs/051227-run.log watched one instance do it: their_head, 8
 * when that run started, read 0 at its scan -- the instance from logs/050214-run.log reset
 * the fifo at its init. What sat at offset 0 of its queue was the version of the instance
 * logs/043646-run.log started, still lying there: an instance that finds an old message
 * there resets the fifo and writes nothing of its own (see slpi_glink_clean). Every ring
 * before an init is lost: the instance's driver has not asked
 * for its interrupt yet, which is why no ring of
 * logs/045335-run.log's or logs/051227-run.log's was ever heard. So their_head, the mark that
 * instance's driver owns and every init rewrites, is set to 0xffffffff first -- an index no
 * queue is that long -- and the wait polls until it reads back in range. The instant it does,
 * the instance is alive and has just built its end: the caller rings at it then, at its
 * freshest. Our own queue is emptied first, because the instance's init does not write our
 * mark (logs/051227-run.log: its instance moved their_head and left our_head at 128) and a
 * queue full of an old round's messages is not where a fresh edge should start. logs/053241-run
 * .log ran this and the mark never changed -- but the sentinel was set a minute after the
 * reset, and an instance's init is under a second old at its reset (see slpi_glink_clean):
 * the init had run and passed before the sentinel was written, and this watch lost that race.
 * slpi=10's slpi_glink_watch_announce() is this one without the race. False: there
 * is no descriptor to watch, so no instance could build an edge on it.
 */
static bool
slpi_glink_watch_edge(uint32_t wait_seconds)
{
	static const uint32_t sentinel = 0xffffffff;
	uint32_t our_head, our_tail;

	if (!slpi_glink_find() || !slpi_glink_make_queue()) {
		return false;
	}
	our_head = glink_word(glink.descriptor + GLINK_OUR_HEAD);
	our_tail = glink_word(glink.descriptor + GLINK_OUR_TAIL);
	if (our_head != our_tail && our_tail < glink.ours_size) {
		glink_mark(glink.descriptor + GLINK_OUR_HEAD, our_tail);
		printf("slpi: m4: our queue held %u unread bytes from an earlier round: the fresh instance "
		    "starts on a clean edge, so our write mark rejoins where it has read to, offset %u\n",
		    (our_head + glink.ours_size - our_tail) % glink.ours_size, our_tail);
	}
	(void) smem_poke(glink.descriptor + GLINK_THEIR_HEAD, &sentinel, sizeof(sentinel));
	settle();
	printf("slpi: m4: the slpi's write mark at smem+%#x is set to 0xffffffff: its glink init will "
	    "overwrite it, and the moment it reads back in range the instance is alive and has just "
	    "built its end; watching for %u s\n", glink.descriptor + GLINK_THEIR_HEAD, wait_seconds);
	for (uint32_t waited = 0; waited < wait_seconds * 1000; waited += 200) {
		uint32_t head = glink_word(glink.descriptor + GLINK_THEIR_HEAD);

		if (head < glink.theirs_size) {
			/* The init restarts the instance's queue at 0 -- logs/050214-run.log's instance
			 * moved its head 8 to 0 -- so whatever the head now reads, everything from 0 to
			 * it is this instance's own writes. An instance that announces writes its
			 * version at offset 0 (logs/043646-run.log's did, and that is the version
			 * logs/044710-run.log read). Our read cursor belongs to the old instance and
			 * has to follow the init to 0: stopping at the head instead would read past
			 * the version sitting there. */
			glink_mark(glink.descriptor + GLINK_THEIR_TAIL, 0);
			printf("slpi: m4: the slpi's write mark reads %u: its glink init is happening now -- "
			    "this is the freshest it will ever be, its queue restarts at 0 and our read "
			    "cursor follows it there; the version goes out at it\n", head);
			return true;
		}
		if (waited > 0 && waited % 30000 == 0) {
			printf("slpi: m4: %u s and the mark is still 0xffffffff: the instance's glink init "
			    "has not come\n", waited / 1000);
		}
		sleep_ms(200);
	}
	printf("slpi: m4: %u s and the mark is still 0xffffffff: this instance's glink init never came, "
	    "and there is nothing to ring for\n", wait_seconds);
	return false;
}

/*
 * slpi=10: wait for a fresh instance to announce, with the edge wiped before its reset.
 *
 * slpi_glink_clean() has already written zeroes over the descriptor and both queues while the
 * old instance was powered off and this one was still held in reset, so their_head reads 0 by
 * construction and needs no sentinel: any value above 0 is this instance's own announcement,
 * and nothing we wrote can be mistaken for one. That removes the race that ended M9 -- in
 * logs/053241-run.log the sentinel went in a minute after the reset and the instance's init
 * was long over by then, so the watch saw a mark no one was going to touch again. This watch
 * starts at the reset itself, and logs/060328-run.log showed how right that was: the
 * announcement was already there on the first poll, under a second after the reset -- the
 * "60 s" and "35 s" the earlier logs seemed to show had only ever been the moments our own
 * scans happened to look. The window stays 300 s: it says "not coming" with enormous room to
 * spare.
 *
 * What the instance does with a queue that starts empty was the question, and that run
 * answered it in full: the head read 8, the version sat at offset 0 as this instance's own
 * writing, the model held, the version went out at the instance's freshest moment -- and the
 * rx question answered itself in the negative for the fourth time: bit 0x18 was rung at the
 * announcement and five times after it, and our_tail never moved. The instance's glink driver
 * has no working rx, and what remains is its interrupt configuration, readable only in the
 * SLPI's own firmware (notes/wifi.md §5). The timeout path still tells the two failures the
 * design separated apart, for any round that needs them: non-zero bytes at offset 0 with the
 * head still at 0 mean it wrote and never announced; all zeroes mean it never touched the
 * edge at all. False: no descriptor to watch, or no announcement.
 */
static bool
slpi_glink_watch_announce(uint32_t wait_seconds)
{
	uint8_t first[8];

	if (!slpi_glink_find() || !slpi_glink_make_queue()) {
		return false;
	}
	printf("slpi: m10: the slpi's write mark at smem+%#x was zeroed before its reset: any value "
	    "above 0 is this instance's own announcement, and no sentinel is between us and it; "
	    "watching for %u s\n", glink.descriptor + GLINK_THEIR_HEAD, wait_seconds);
	for (uint32_t waited = 0; waited < wait_seconds * 1000; waited += 200) {
		uint32_t head = glink_word(glink.descriptor + GLINK_THEIR_HEAD);

		if (head > 0 && head < glink.theirs_size) {
			/* The announcement. The queue was zeroed at every offset, so everything
			 * from 0 to this head is this instance's own writing, and its read cursor
			 * starts at the beginning of it. */
			glink_mark(glink.descriptor + GLINK_THEIR_TAIL, 0);
			printf("slpi: m10: after %u s the slpi's write mark reads %u: this instance "
			    "wrote %u bytes and announced them itself -- the version goes out at its "
			    "freshest moment\n", waited / 1000, head, head);
			return true;
		}
		if (head >= glink.theirs_size) {
			/* The descriptor was zeroed, so this can only be the instance's own
			 * writing -- an index outside the queue is a mark we cannot use, but it
			 * says the init came and did not finish the way the protocol asks. */
			printf("slpi: m10: after %u s the slpi's write mark reads %u, outside its "
			    "%u-byte queue: the instance's driver wrote its mark wrong\n",
			    waited / 1000, head, glink.theirs_size);
			return false;
		}
		if (waited > 0 && waited % 30000 == 0) {
			printf("slpi: m10: %u s and the mark is still 0: the instance has not "
			    "announced\n", waited / 1000);
		}
		sleep_ms(200);
	}
	if (smem_peek(glink.theirs, first, sizeof(first)) &&
	    (first[0] | first[1] | first[2] | first[3] | first[4] | first[5] | first[6] | first[7]) != 0) {
		smem_hexdump(glink.theirs, sizeof(first), "the slpi's queue, offset 0");
		printf("slpi: m10: %u s, the mark is still 0, but its queue holds bytes at offset 0: "
		    "this instance wrote and never announced -- the head is the part of its init "
		    "that does not run\n", wait_seconds);
	} else {
		smem_hexdump(glink.theirs, 32, "the slpi's queue, offset 0");
		printf("slpi: m10: %u s, the mark is still 0 and its queue is still all zeroes: this "
		    "instance never touched the edge at all -- with a clean queue in front of it, "
		    "that is the answer, and the init is not what the four logs said it was\n",
		    wait_seconds);
	}
	return false;
}

/* ---- M3b: is the Q6 executing at all? --------------------------------------------------------------- */

/*
 * The watch above answers whether the slpi's smp2p driver published its entries. It cannot answer
 * whether the Q6 runs at all, and the two failures want different answers: for the first the next
 * step is inside the slpi's drivers, for the second it is whatever keeps the Q6 from executing.
 *
 * The image's own memory answers. The last segment has no file and 0x376000 bytes of memory, and
 * other segments' files stop short of their memory in places (logs/031835: ph21 is written "no
 * file, all zeroes"), and all of that is zeroes we wrote. It is also where a Hexagon firmware puts
 * its heap, its stacks and its log buffer: the image's bss is exactly the memory its files say
 * nothing about. Nothing else has a reason to write there -- the ap has no mapping of it and no
 * reason to want one. So a non-zero word after the start is the q6's own writing, and none after
 * the wait means it wrote nothing to the memory it was given.
 *
 * What the phone answered when this was first tried, and why the read is now behind slpi=7: the
 * region cannot be read while the slpi runs. logs/033426-run.log ends at the announcement line
 * below -- the first word's load never came back, the thread never came back, and the boot
 * processor did not answer at the shutdown that followed. The ssc's register block stalls the
 * same way for the same kind of reason, and after PAS_MEM_SETUP and PAS_AUTH_AND_RESET the
 * region has the same standing: it belongs to the subsystem's domain, and the vendor's driver
 * only ever reads it when the subsystem is not running -- pil_dump_segs() at a ramdump, after
 * the subsystem has stopped, and pil_clear_segment() at a shutdown, after the stop-ack and after
 * the region has been handed back to the host (peripheral-loader.c:1528, :1552, :1521-1531). We
 * never hand it back. So the read is asked for knowingly now, and a run that ends at its first
 * word is itself the answer: the q6's memory is not visible from the ap while the q6 owns it,
 * and the question "did it run" has to be answered from shared memory, which is what the two
 * reads above are for.
 */
struct zero_range {
	uint32_t at, size;                      /* where in the region, and how many bytes of it */
};

static struct zero_range zero_ranges[SLPI_SEGMENT_FILES];
static unsigned zero_count;
static uint32_t zero_total;
static uint32_t region_phys, region_size;     /* the region, kept past image_forget() */

/*
 * The parts of the region that are zeroes of ours: each segment's memory past its file's end. The
 * first whole word that is nothing but zeroes starts at the file's size rounded up to eight, and
 * the range is rounded back down at the end, because segments_write() writes whole words and the
 * last partial word of a file's segment holds file bytes. Taken at the end of segments_write(),
 * the only moment the segment table and the region are both known and the image has not yet been
 * forgotten.
 */
static void
region_note_zeroes(void)
{
	zero_count = 0;
	zero_total = 0;
	for (unsigned s = 0; s < image.segments; s++) {
		struct slpi_segment *seg = &image.segment[s];
		uint32_t past = (seg->file_size + 7) & ~7u, left = seg->memory_size & ~7u;

		if (left > past && zero_count < sizeof(zero_ranges) / sizeof(zero_ranges[0])) {
			zero_ranges[zero_count++] = (struct zero_range) { .at = seg->at + past,
			    .size = left - past };
			zero_total += left - past;
		}
	}
	region_phys = image.base;
	region_size = image.size;
	printf("slpi: m3b: %u ranges, %u bytes in all, of the region are zeroes we wrote and the image "
	    "says nothing about: a hexagon firmware puts its heap, stacks and log buffer in memory like "
	    "that, so it is the place to look for it having run\n", zero_count, zero_total);
}

/*
 * Asked for by slpi=7 only. The first word is read alone and announced first: logs/033426-run.log
 * ended at exactly that line, and the run is what this read gives, not what it reads.
 */
static void
slpi_region_watch(void)
{
	volatile uint64_t *memory;
	uint64_t found = 0;
	unsigned shown = 0;

	if (region_phys == 0 || zero_count == 0) {
		printf("slpi: m3b: there is no image region to read: the q6 was given no memory\n");
		return;
	}
	printf("slpi: m3b: reading the first word of the region at %#x now: if the run ends here, the "
	    "region cannot be read while the slpi runs\n", region_phys);
	memory = (volatile uint64_t *) ml_io_map_unmappable((vm_offset_t) region_phys,
	    (vm_size_t) region_size, VM_WIMG_WCOMB);
	if (memory == NULL) {
		printf("slpi: m3b: the region at %#x does not map a second time: nothing can be read\n",
		    region_phys);
		return;
	}
	for (unsigned r = 0; r < zero_count; r++) {
		uint64_t here = 0;
		uint32_t first = 0;

		for (uint32_t at = 0; at < zero_ranges[r].size; at += 8) {
			uint64_t word = memory[(zero_ranges[r].at + at) / 8];

			if (word == 0) {
				continue;
			}
			if (here++ == 0) {
				first = at;
			}
			if (shown++ < 12) {
				printf("slpi: m3b: %#x reads %#llx where we wrote zero\n",
				    region_phys + zero_ranges[r].at + at, (unsigned long long) word);
			}
		}
		found += here;
		if (here != 0) {
			printf("slpi: m3b:   the range at %#x, %u bytes, has %llu non-zero words, the "
			    "first at %#x\n", region_phys + zero_ranges[r].at, zero_ranges[r].size,
			    (unsigned long long) here, region_phys + zero_ranges[r].at + first);
		}
	}
	ml_io_unmap((vm_offset_t) memory, (vm_size_t) region_size);
	printf("slpi: m3b: %llu of the %u words we wrote as zeroes are not zero: %s\n",
	    (unsigned long long) found, zero_total / 8, found != 0 ?
	    "something has written to the memory the q6 was given -- its heap, its stack or its log -- "
	    "so it is executing, and what is wrong is inside its own drivers: the next thing to read is "
	    "what it wrote, which is where to look for the slpi's own console" :
	    "nothing has written to the memory the q6 was given: its reset was released and nothing came "
	    "of it. That is not proof it executes nowhere -- a q6 that halts in its first instructions "
	    "may never reach memory at all, and on-chip ram would be invisible here -- but it is proof "
	    "it does not get as far as using its own image, and that is far enough to stop looking at "
	    "its drivers");
}

/* ---- the Q6's control block ---------------------------------------------------------------------- */

#define SSC_PHYS                  0x05c00000UL          /* the ssc node's reg, boot.bin.01.dts:3324 */
#define SSC_SIZE                  0x4000
#define SSC_AXI_HALT_REQUEST      0x000
#define SSC_AXI_HALT_ACKNOWLEDGE  0x004
#define SSC_AXI_IDLE              0x008
#define SSC_RESET_CONTROL         0x014

/* The four offsets both fetched q6v5 drivers agree on; the rest of the block is unnamed, because
 * the driver that owns this subsystem, subsys-pil-tz.c, gives it no names at all. */
static const char *
q6_name(unsigned offset)
{
	switch (offset) {
	case SSC_AXI_HALT_REQUEST:
		return "AXI halt request";
	case SSC_AXI_HALT_ACKNOWLEDGE:
		return "AXI halt acknowledge";
	case SSC_AXI_IDLE:
		return "AXI idle";
	case SSC_RESET_CONTROL:
		return "reset control";
	default:
		return NULL;
	}
}

/* Read once after M2, from its thread: started says whether TrustZone let the subsystem out of
 * reset a moment ago, which is what decides whether this read is an observation or a known way
 * to end the run. */
static void
q6_dump(bool started)
{
	vm_offset_t ssc = ml_io_map(SSC_PHYS, SSC_SIZE);
	uint32_t words[0x100 / 4];

	if (ssc == 0) {
		say("slpi: q6: could not map the ssc control block at %#x\n", SSC_PHYS);
		return;
	}
	/* The first read is the one that ends the run while the SLPI is down -- the load does not
	 * come back at all (logs/095451-run.log) -- so it is announced before it happens, and all
	 * sixty-four words are taken out in one go right after: once the subsystem is up, the
	 * window is safe in memory and the printing can happen at leisure. */
	say("slpi: q6: reading the block at %#x now%s: if the run ends here, the subsystem is not up, "
	    "and this line is the answer\n", SSC_PHYS, started ? "" : " (the start failed, and slpi=4 asked for this anyway)");
	for (unsigned off = 0; off < 0x100; off += 4) {
		words[off / 4] = rd(ssc + off);
	}
	for (unsigned off = 0; off < 0x100; off += 4) {
		const char *name = q6_name(off);

		say("slpi: q6: +%#05x = %#010x%s%s\n", off, words[off / 4],
		    name != NULL ? "   <- " : "", name != NULL ? name : "");
	}
	/* What the named registers would mean, in the sense the q6v5 drivers give them: halt
	 * acknowledge bit 0 says the core is halted, reset control bit 0 (Q6SS_STOP_CORE) says it
	 * is stopped, idle bit 0 says the bus is idle. All zeroes is what a held-in-reset or
	 * unpowered-but-decoding block reads; all ones is what a window nothing answers reads. */
	say("slpi: q6: halt request %#x, acknowledge %#x%s, idle %#x, reset control %#x%s: the slpi %s "
	    "on this boot, so %s\n",
	    words[SSC_AXI_HALT_REQUEST / 4], words[SSC_AXI_HALT_ACKNOWLEDGE / 4],
	    words[SSC_AXI_HALT_ACKNOWLEDGE / 4] & 1 ? " (halted)" : " (not halted)",
	    words[SSC_AXI_IDLE / 4], words[SSC_RESET_CONTROL / 4],
	    words[SSC_RESET_CONTROL / 4] & 1 ? " (stopped)" : "",
	    started ? "was started" : "was not started",
	    started ? "these words are the observation M2 wanted" :
	    words[0] == 0 && words[1] == 0 ? "a block of zeroes is expected" :
	    words[0] == 0xffffffff ? "a block of ones is expected" : "this is the observation");
}

/* ---- M2's thread, and the entry point -------------------------------------------------------------- */

/*
 * M2 from here on, on a thread of its own: the files are on the root, and the root is not
 * mounted when EARLY_BOOT startup runs, so the root's coming up is waited for first, the way the
 * ADSP's whole_thread() waits (hakios.adsp.c:3240-3242). The rails are voted again before the
 * start, whatever slpi's level did earlier: the votes are active-only and were never dropped, so
 * this changes nothing by itself, and it means the start is never attempted without them.
 */
static void
start_thread(__unused void *parameter, __unused wait_result_t result)
{
	uint32_t wait_seconds = 3;
	bool started = false;

	for (unsigned waited = 0; waited < 300 && rootvnode == NULLVP; waited++) {
		sleep_ms(200);
	}
	if (rootvnode == NULLVP) {
		printf("slpi: m2: a minute gone and there is still no root: the image cannot be read\n");
	}
	PE_parse_boot_argn("slpi_wait", &wait_seconds, sizeof(wait_seconds));
	for (unsigned i = 0; i < sizeof(rails) / sizeof(rails[0]); i++) {
		rail_vote(&rails[i]);
	}
	arc_vote();
	xo_vote();
	if (slpi_level == 9) {
		/* M9: start a fresh instance, wait in this run for its glink init, and ring at it
		 * the moment the init rewrites the descriptor's marks. logs/051227-run.log's
		 * instance zeroed their_head at its init; what sat at offset 0 of its queue was
		 * the earlier instance's version, still lying there, not its own (see
		 * slpi_glink_clean). Every ring before an init is
		 * lost -- the instance's driver has not asked for its
		 * interrupt yet. This asks whether an instance that has just built its end, with
		 * item 479 already in place from the rounds before, reads our queue when it is
		 * rung at once: if it answers, the instance was alive and waiting for us, and
		 * the silence of the later rings was only timing; if it does not, bit 0x18 of
		 * 0x17c0000c does not reach its rx and its side has to be read out of its own
		 * firmware (notes/wifi.md §5). */
		printf("slpi: m9: a fresh instance, and the wait for its glink init in this run: the "
		    "version will go out the moment the instance rewrites the descriptor's marks\n");
		if (rootvnode != NULLVP && mdt_parse()) {
			if (slpi_smp2p_make_out() != 0) {
				slpi_smp2p_kick("the item is there before the slpi comes up, in case its driver is already listening");
			}
			started = slpi_start();
			image_forget();
		}
		if (started) {
			printf("slpi: m2: the slpi is out of reset; watching its SMP2P item for %u s\n", wait_seconds);
			(void) slpi_smp2p_watch(wait_seconds);
			if (slpi_glink_watch_edge(480)) {
				(void) slpi_glink_try(wait_seconds);
			}
			slpi_sfr_read();
			slpi_partition_text();
		}
		thread_terminate(current_thread());
		return;
	}
	if (slpi_level == 10) {
		/* M10: one variable -- the state of the edge at the moment of the reset. Four
		 * instances have come out of a reset here, and the only thing that separates the
		 * one that announced its version from the three that stayed quiet is what its
		 * own queue held when it came out: the instance logs/043646-run.log started,
		 * which found the descriptor all zeroes and the queue empty, is the only
		 * announcement any log has ever seen; the ones that found an old version
		 * message at offset 0 reset the fifo and wrote nothing of their own, and none
		 * of them ever read our queue. So this run wipes the descriptor and both queues
		 * inside slpi_start(), between the AOP's "off" and the auth-and-reset -- old
		 * instance powered down, new one still held in reset -- and
		 * slpi_glink_watch_announce() reads the zeroed mark from the reset itself: any
		 * value above 0 is the new instance's own announcement, with no sentinel to
		 * lose the race logs/053241-run.log lost. logs/060328-run.log ran it and the
		 * model held in full: the mark read 8 on the watch's first poll -- the
		 * instance's init is under a second old at its reset -- the version went out at
		 * its freshest moment and was answered, and our_tail still never moved: the
		 * instance's glink driver has no working rx, and what remains is its interrupt
		 * configuration in the SLPI's own firmware (notes/wifi.md §5). */
		printf("slpi: m10: the edge is wiped while the old instance is off and the new one is "
		    "in reset, and the wait for an announcement starts at the reset itself\n");
		if (rootvnode != NULLVP && mdt_parse()) {
			if (slpi_smp2p_make_out() != 0) {
				slpi_smp2p_kick("the item is there before the slpi comes up, in case its driver is already listening");
			}
			started = slpi_start();
			image_forget();
		}
		if (started) {
			/* The announcement watch comes first: an instance's init is under
			 * a second old at its reset (logs/060328-run.log), and waiting for the
			 * smp2p entries first is the race that ended M9. */
			if (slpi_glink_watch_announce(300)) {
				(void) slpi_glink_try(wait_seconds);
			} else {
				printf("slpi: m2: the slpi is out of reset; watching its SMP2P item "
				    "for %u s: with no announcement, whether its smp2p driver came up "
				    "is the liveness answer that is left\n", wait_seconds);
				(void) slpi_smp2p_watch(wait_seconds);
			}
			slpi_sfr_read();
			slpi_partition_text();
		}
		thread_terminate(current_thread());
		return;
	}
	if (slpi_level == 12) {
		/* M12: the same instance, the same protocol ping, and one new
		 * witness: the doorbell block's own registers. logs/065342-run.log proved
		 * the ping gets no answer and proved the slpi's driver is alive (its own
		 * restart-done sits in item 481's flags, unanswered); what is left to
		 * tell apart is a masked interrupt line from a silent handler, and only
		 * the block itself can say which. Nothing is started and no new memory
		 * is written: the ping is m11's, the readback is the one new thing
		 * (notes/wifi.md §5 M12). */
		printf("slpi: m12: the ping m11 asked, and the doorbell block read back after each ring: "
		    "a bit that was taken does not stay set, a bit that was ignored does\n");
		if (slpi_glink_find()) {
			uint32_t marks[8] = { 0 };

			if (smem_peek(glink.descriptor, marks, sizeof(marks))) {
				printf("slpi: m12: the edge reads our_tail %u, our_head %u, their_tail %u, "
				    "their_head %u%s\n", marks[0], marks[1], marks[2], marks[3],
				    marks[3] == 8 ? ": still as logs/060328-run.log left it, the "
				    "announcement in place" : "");
			}
		}
		if (slpi_smp2p_make_out() != 0) {
			slpi_apcs_pending_watch(wait_seconds);
		}
		slpi_sfr_read();
		thread_terminate(current_thread());
		return;
	}
	if (slpi_level == 16 || slpi_level == 17 || slpi_level == 18 || slpi_level == 19 ||
	    slpi_level == 20 || slpi_level == 21) {
		/* M16: one variable -- the two lines are claimed through the kernel's
		 * own attach path instead of read by hand. m13-m15 only ever wrote
		 * ICFGR and never the group, and a line the secure world holds reads
		 * zero and drops writes exactly as those runs saw; gic_v3_spi_attach
		 * writes group, priority, type and route and unmasks, and
		 * gic_v3_spi_count counts what arrives through the real dispatch. See
		 * the block above slpi_rings_claim for the refutation of m15's premise
		 * and what each answer means (notes/wifi.md §5 M16). */
		char conclusion[1000];
		uint32_t avail_io = 0, avail_pas = 0;
		uint64_t fired;
		int r_io, r_pas;

		if (slpi_level == 21) {
			printf("slpi: m21: one variable from M20: withhold host GLINK transmissions "
			    "and pump, keep secure shutdown, firmware load, crash observation and "
			    "SMP2P restart-ack unchanged; observe whether watchdog still arrives\n");
		}
		if (slpi_level == 20) {
			printf("slpi: m20: M19 startup unchanged; one observation added: count "
			    "SLPI watchdog INTID 526 and compare SFR with a post-shutdown baseline. "
			    "No crash record is cleared, no protocol or power setting is changed\n");
		}
		if (slpi_level == 19) {
			printf("slpi: m19: one variable from m18: PAS_SHUTDOWN for SLPI client 12 "
			    "must succeed before our SMP2P item, GLINK queues or firmware image "
			    "are rewritten. Corrected INTIDs and restart-ack remain unchanged\n");
		}
		if (slpi_level == 18) {
			printf("slpi: m18: one variable from m17: acknowledge remote restart-done "
			    "with host restart-ack bit 1 and kick before GLINK. Corrected INTIDs "
			    "202/204 and all bring-up steps are unchanged\n");
		}
		if (slpi_level == 17) {
			printf("slpi: m17: one variable from m16: DT SPI indices 170/172 plus 32 "
			    "give absolute GIC INTIDs %u/%u; the kernel API and register offsets "
			    "use absolute IDs. The old zero counts were on the wrong lines. "
			    "The remaining m16-labelled instrumentation is reused unchanged\n",
			    SPI_GLINK_RX, SPI_SMP2P_RX);
		}
		printf("slpi: m16: the two lines the slpi rings at us with, claimed through the "
		    "kernel's own attach path around a fresh instance: its count of what arrives "
		    "cannot be refused to be read, and its writes are the first anyone has made "
		    "that ask for the group as well as the type (notes/wifi.md §5 M16)\n");
		gic_instrument_check();
		r_io = scm_call_available(SCM_SVC_IO, SCM_IO_READ, &avail_io);
		r_pas = scm_call_available(2, 5, &avail_pas);   /* auth-and-reset: the call this file makes work every boot */
		if (r_io != 0 || r_pas != 0) {
			printf("slpi: m16: the availability probe was itself refused: io read returns "
			    "%d, auth-and-reset returns %d: the firmware does not answer this question "
			    "either\n", r_io, r_pas);
		} else {
			printf("slpi: m16: the availability probe says: io read (5,1) is %s, "
			    "auth-and-reset (2,5) is %s: %s\n", avail_io != 0 ? "offered" : "not offered",
			    avail_pas != 0 ? "offered" : "not offered",
			    avail_pas == 0 ? "it denies a call this very boot made work, so the probe "
			    "is not to be trusted either way" : avail_io != 0 ? "the probe works and "
			    "the io service is offered: m15's -2 refused the addresses themselves"
			    : "the probe works and the io service is not offered at all: m15's -2 was "
			    "the argument refused, and the registers were never reachable through it");
		}
		(void) slpi_gic_listen("before anything");
		slpi_rings_claim();
		(void) slpi_gic_listen("right after the claim, which is where the readback answers "
		    "who owns these two lines: the attach just asked for edge and enabled through "
		    "the kernel's own writes");
		if (slpi_level == 19 || slpi_level == 20 || slpi_level == 21) {
			uint64_t answer = ~0ull;
			int stopped = scm("m19: to stop the previous SLPI instance before any shared memory rewrite",
			    SCM_PAS_SHUTDOWN, SCM_ONE_VALUE, PAS_SLPI, 0, 0, &answer);

			printf("slpi: m19: PAS_SHUTDOWN transport %d, firmware answer %#llx\n", stopped, answer);
			if (stopped != 0 || answer != 0) {
				printf("slpi: m19: shutdown did not succeed: no SMP2P rewrite, queue cleaning "
				    "or image reload is attempted; this does not test a clean restart\n");
				thread_terminate(current_thread());
				return;
			}
			printf("slpi: m19: previous SLPI shutdown succeeded; proceeding with the unchanged "
			    "M18 startup and acknowledgement sequence\n");
			if (slpi_level == 20 || slpi_level == 21) {
				slpi_crash_baseline();
			}
		}
		if (rootvnode != NULLVP && mdt_parse()) {
			if (slpi_smp2p_make_out() != 0) {
				slpi_smp2p_kick("the item is there before the slpi comes up, in case its driver is already listening");
			}
			started = slpi_start();
			image_forget();
		}
		if (started) {
			if (slpi_glink_watch_announce(300)) {
				slpi_rings_report("the moment its announcement is in the queue, which "
				    "is the moment it rang");
				if (slpi_level >= 18) {
					(void) slpi_smp2p_ack_restart();
				}
				if (slpi_level == 20 || slpi_level == 21) {
					slpi_crash_report("at new GLINK announcement");
				}
				if (slpi_level == 21) {
					slpi_glink_passive(wait_seconds);
				} else {
					(void) slpi_glink_try(wait_seconds);
				}
			} else {
				printf("slpi: m16: no announcement came: the glink ring cannot be "
				    "expected, but its smp2p probe writes and kicks on its own path, so "
				    "the smp2p line is still the answer\n");
				(void) slpi_smp2p_watch(wait_seconds);
			}
			slpi_rings_report("at the end");
			if (slpi_level == 20 || slpi_level == 21) {
				slpi_crash_report("at end of exchange");
			}
			if (slpi_level >= 18) {
				struct smp2p_header in, out;
				uint32_t in_size = 0, out_size = 0;
				uint32_t in_at = smem_item(ITEM_SLPI_SMP2P, &in_size);
				uint32_t out_at = smem_item(ITEM_SLPI_SMP2P_OUT, &out_size);

				if (in_at != 0 && out_at != 0 && in_size >= sizeof(in) && out_size >= sizeof(out) &&
				    smem_peek(in_at, &in, sizeof(in)) && smem_peek(out_at, &out, sizeof(out))) {
					printf("slpi: m18: end flags: item 481 %#x, item 430 %#x; GLINK "
					    "versions agreed %u, IPCRTR open %u, host queue tail %u head %u\n",
					    in.flags, out.flags, glink.agreed, glink.open,
					    glink_word(glink.descriptor + GLINK_OUR_TAIL),
					    glink_word(glink.descriptor + GLINK_OUR_HEAD));
				}
			}
			(void) slpi_gic_listen("at the end");
			slpi_sfr_read();
			slpi_partition_text();
		}
		fired = gic_v3_spi_count(SPI_GLINK_RX) + gic_v3_spi_count(SPI_SMP2P_RX);
		if (gic_mapping_alive == 0) {
			snprintf(conclusion, sizeof(conclusion), "the mapping every hand-read went "
			    "through is dead -- typer read all zeros, which no live distributor can "
			    "be -- so m13, m14 and m15 read nothing, not a state, and the glink "
			    "question is open again (notes/wifi.md §5 M16)");
		} else if (slpi_rings_claimed == 0) {
			snprintf(conclusion, sizeof(conclusion), "the kernel's own attach refused both "
			    "lines: neither can be claimed from here at all, and the rx path cannot "
			    "exist (notes/wifi.md §5 M16)");
		} else if (fired != 0 || slpi_ring_entered[0] != 0 || slpi_ring_entered[1] != 0) {
			snprintf(conclusion, sizeof(conclusion), "the gic's own dispatch counted rings "
			    "and a handler of ours was entered: the slpi-to-ap half of the doorbell "
			    "works. Host-to-SLPI notification/handling and protocol state remain "
			    "to be distinguished; these counts do not identify an AOP or rx-enable "
			    "fault (notes/wifi.md section 5 M17-M18)");
		} else if (!started) {
			snprintf(conclusion, sizeof(conclusion), "the slpi never started, so no ring "
			    "was expected; what stands is the claim and its readback alone, which the "
			    "lines' state in the last read tells (notes/wifi.md §5 M16)");
		} else if (slpi_level >= 17) {
			uint32_t mask = (1u << (SPI_GLINK_RX % 32)) | (1u << (SPI_SMP2P_RX % 32));

			snprintf(conclusion, sizeof(conclusion), "corrected INTIDs 202/204: "
			    "dispatch and handler counts stayed zero; both claims %s, both edge %s, "
			    "both enabled %s. No SLPI-to-AP interrupt was observed on these IDs; "
			    "this alone does not identify a bring-up fault (notes/wifi.md section 5 M17)",
			    slpi_rings_claimed == 3 ? "yes" : "no", gic_lines_edge == 1 ? "yes" : "no",
			    (gic_last_enabled & mask) == mask ? "yes" : "no");
		} else if (gic_lines_edge == 1 &&
		    (gic_last_enabled & ((1u << (SPI_GLINK_RX % 32)) | (1u << (SPI_SMP2P_RX % 32)))) != 0) {
			snprintf(conclusion, sizeof(conclusion), "the lines read edge and enabled now, "
			    "which is what the kernel's own writes leave when they take: these lines "
			    "are ours, and nothing arrived -- the doorbell is dead in both directions "
			    "and the difference from Android is in how the subsystem was brought up, "
			    "not in which bit we ring (notes/wifi.md §5 M16)");
		} else {
			snprintf(conclusion, sizeof(conclusion), "the lines still read masked and "
			    "level after the kernel's own attach wrote group, edge type and enable for "
			    "them: the writes were dropped, which is how lines held by the secure world "
			    "behave; the two lines belong to the firmware's interrupt groups and no "
			    "reader or writer in our world can take them (notes/wifi.md §5 M16)");
		}
		printf("slpi: %s: after a fresh instance: %s\n",
		    slpi_level == 21 ? "m21" : slpi_level == 20 ? "m20" : slpi_level == 19 ? "m19" : slpi_level == 18 ? "m18" :
		    slpi_level == 17 ? "m17" : "m16", conclusion);
		thread_terminate(current_thread());
		return;
	}
	if (slpi_level == 13 || slpi_level == 14 || slpi_level == 15) {
		/* M13/M14/M15: one variable -- the other direction of the doorbell. Every
		 * run so far has rung our bit and watched its memory; this one reads
		 * our own interrupt controller, where its two rings would sit and
		 * stay, and asks them around a fresh instance started exactly the way
		 * m10 started one -- the clean edge is what makes its announcement,
		 * and with it its two rings, certain. See slpi_gic_listen for the
		 * lines, the latching, and what each answer means (notes/wifi.md §5 M13).
		 * logs/074524-run.log answered m13's precondition in the negative -- both
		 * lines sit at the gic's reset type, level, where a pulse keeps nothing --
		 * so m14 set the type to edge first, the way the vendor's devicetree
		 * says these two lines are, and asked again (see slpi_gic_edge): the
		 * write read back as zeros. logs/075618-run.log's own reading was secure
		 * interrupts, but the vendor's pinctrl-msm.c shows the plainer way to
		 * get that exact behaviour -- a distributor the firmware owns and the
		 * non-secure world may not touch, which its own driver reads and writes
		 * through scm_io_read/scm_io_write -- and then every direct read of
		 * ours, m13's included, was a zero, not a state. m15 asks the same
		 * question through the owner: the firmware's SCM IO service. See
		 * slpi_gic_firmware_listen. */
		const char *tag = slpi_level == 15 ? "m15" : slpi_level == 14 ? "m14" : "m13";
		const char *note = slpi_level == 15 ? "M15" : slpi_level == 14 ? "M14" : "M13";
		char conclusion[1000];
		int at_end;

		printf("slpi: %s: the two lines the slpi rings at us with, read around a fresh "
		    "instance: a masked edge interrupt latches in the pending register and stays "
		    "there, so one read answers for every ring since this boot%s (notes/wifi.md §5 %s)\n",
		    tag, slpi_level == 15 ? ", this time through the firmware that may own the registers" :
		    "", note);
		(void) slpi_gic_listen("before anything");
		if (slpi_level == 14) {
			/* the one variable this run adds, set while the lines are still
			 * disabled and read back to prove the write took */
			slpi_gic_edge();
			(void) slpi_gic_listen("after the type was set to edge");
		}
		if (slpi_level == 15) {
			/* the first read anyone has made that the hardware can answer:
			 * the registers through the entity allowed to touch them */
			(void) slpi_gic_firmware_listen("before anything");
			if (gic_firmware_config != 0) {
				/* the firmware sees a word our own mapping said was
				 * zeros: the registers are its, and the type goes in
				 * through it, the way the vendor's pinctrl driver
				 * writes these same addresses */
				slpi_gic_firmware_edge();
			}
		}
		if (rootvnode != NULLVP && mdt_parse()) {
			if (slpi_smp2p_make_out() != 0) {
				slpi_smp2p_kick("the item is there before the slpi comes up, in case its driver is already listening");
			}
			started = slpi_start();
			image_forget();
		}
		if (started) {
			if (slpi_glink_watch_announce(300)) {
				struct smp2p_header in_head;
				uint32_t size = 0, in_at = smem_item(ITEM_SLPI_SMP2P, &size);

				/* The other ring's witness: its smp2p probe writes the
				 * restart-done and kicks in the same breath, so this word says
				 * whether the second ring went out too. */
				if (in_at != 0 && smem_peek(in_at, &in_head, sizeof(in_head))) {
					printf("slpi: %s: its item's flags read %#x: %s\n", tag, in_head.flags,
					    (in_head.flags & (1u << SMP2P_FLAGS_RESTART_DONE)) != 0 ?
					    "its restart-done is there, which its driver writes and then "
					    "kicks (smp2p.c:181-191): the ring that goes with it is what "
					    "spi 172 would hold" :
					    "no restart-done yet: its smp2p driver has not announced itself");
				}
				if (slpi_level == 15) {
					(void) slpi_gic_firmware_listen(
					    "the moment its announcement is in the queue, which is the moment it rang");
				} else {
					(void) slpi_gic_listen(
					    "the moment its announcement is in the queue, which is the moment it rang");
				}
				(void) slpi_glink_try(wait_seconds);
			} else {
				printf("slpi: %s: no announcement came: the glink ring cannot be expected, but its "
				    "smp2p probe writes and kicks on its own path, so the smp2p line is "
				    "still the answer\n", tag);
				(void) slpi_smp2p_watch(wait_seconds);
			}
			at_end = slpi_level == 15 ? slpi_gic_firmware_listen("at the end") :
			    slpi_gic_listen("at the end");
			slpi_sfr_read();
			slpi_partition_text();
		} else {
			at_end = -1;
		}
		if (at_end < 0) {
			snprintf(conclusion, sizeof(conclusion), "the registers could not be read at "
			    "all, from the mapping or from the firmware, and nothing was answered "
			    "(notes/wifi.md §5 %s)", note);
		} else if (slpi_level == 15 && gic_firmware_config == 0) {
			snprintf(conclusion, sizeof(conclusion), "the firmware reads the same words we "
			    "did and they are all zeros: the distributor is open to us, m13 and m14 "
			    "saw the real state, the lines sit at their reset type level with nothing "
			    "pending -- and the firmware that owns this machine never set the edge "
			    "type its own devicetree asks for, which is a difference in the bring-up "
			    "(notes/wifi.md §5 M15)");
		} else if (gic_lines_edge == 0 && slpi_level == 15) {
			snprintf(conclusion, sizeof(conclusion), "the registers belong to the firmware "
			    "-- it reads a word our own mapping only ever returned zeros for -- but "
			    "after the write of edge through the owner its readback still says level: "
			    "the type is not ours to set and the lines stay unlisten-able from either "
			    "side; the question goes back to the bring-up that should have set them "
			    "(notes/wifi.md §5 M15)");
		} else if (gic_lines_edge == 0 && slpi_level == 14) {
			snprintf(conclusion, sizeof(conclusion), "the type was written for edge and the "
			    "readback still says level: a non-secure write is ignored, which reads as "
			    "secure interrupts -- but the vendor's pinctrl-msm.c shows the same "
			    "behaviour belongs to a distributor the firmware owns, and then every "
			    "direct read of ours was a zero, not a state; the registers have to be "
			    "read through their owner, which is m15 (notes/wifi.md §5 M14)");
		} else if (gic_lines_edge == 0) {
			snprintf(conclusion, sizeof(conclusion), "both lines are configured level, not "
			    "edge: a pulse would not stay pending and the silence proves nothing; the "
			    "type has to be set to edge, which is allowed while the line is masked, and "
			    "the question asked again (notes/wifi.md §5 M13)");
		} else if ((at_end & 1) != 0) {
			snprintf(conclusion, sizeof(conclusion), "spi 170 is pending: the slpi's glink "
			    "ring reached our interrupt controller -- the apcs path works in its "
			    "slpi-to-ap half and only the ap-to-slpi half is dead, which is the slpi's "
			    "rx enable and the aop path that arms it%s (notes/wifi.md §5 %s)",
			    slpi_level == 15 ? " -- and it was readable only through the firmware, "
			    "because the distributor is its and our own mapping returned zeros" : "",
			    note);
		} else if ((at_end & 2) != 0) {
			snprintf(conclusion, sizeof(conclusion), "spi 172 is pending: the slpi's smp2p "
			    "kick reached our interrupt controller -- the same half works, and glink's "
			    "silence is its own question%s (notes/wifi.md §5 %s)",
			    slpi_level == 15 ? " -- readable only through the firmware" : "", note);
		} else {
			snprintf(conclusion, sizeof(conclusion), "neither line is pending, and they are "
			    "edge-configured and masked, which is the state that latches a ring and "
			    "keeps it: the slpi's rings never reached us either -- the doorbell path is "
			    "dead in both directions and the difference from Android is in how the "
			    "subsystem was brought up, not in which bit we ring%s (notes/wifi.md §5 %s)",
			    slpi_level == 15 ? " -- and this is the firmware's own answer, read "
			    "through the service that owns the registers, not a zero from a mapping" : "",
			    note);
		}
		printf("slpi: %s: after a fresh instance: %s\n", tag, conclusion);
		thread_terminate(current_thread());
		return;
	}
	if (slpi_level == 11) {
		/* M11: the smp2p rx, asked of the instance that is running. Nothing is
		 * started: the instance logs/060328-run.log started owns the edge now, its glink
		 * rx is proven dead, and its smp2p probe ran minutes after that reset, so its
		 * driver has asked for its interrupt long since. The one variable is the
		 * protocol's own ping -- restart-done in our item's flags, bit 0x1a, and the
		 * watch is its item's flags, where only it can write the ack. See
		 * slpi_smp2p_handshake for why every ring so far proved nothing and this one
		 * cannot be answered with silence (notes/wifi.md §5 M11). */
		printf("slpi: m11: the restart-done ping to the instance that is running now: its answer "
		    "is a word only it can write, and nothing of it is restarted or written by us\n");
		if (slpi_glink_find()) {
			uint32_t marks[8] = { 0 };

			/* Read-only, and it says whether the edge is still the one that
			 * announced: our_tail is the word its rx would move, and it has never moved. */
			if (smem_peek(glink.descriptor, marks, sizeof(marks))) {
				printf("slpi: m11: the edge reads our_tail %u, our_head %u, their_tail %u, "
				    "their_head %u%s\n", marks[0], marks[1], marks[2], marks[3],
				    marks[3] == 8 ? ": still as logs/060328-run.log left it, the "
				    "announcement in place" : "");
			}
		}
		if (slpi_smp2p_make_out() != 0) {
			slpi_smp2p_handshake(wait_seconds);
		}
		slpi_sfr_read();
		thread_terminate(current_thread());
		return;
	}
	if (slpi_level >= 8) {
		/* M4 does not start the slpi again: the instance logs/040912-run.log left
		 * running has outlived every shutdown since, and its glink items are already
		 * in shared memory. A new instance would spend minutes rebuilding what is
		 * already there before it could answer anything. The rails, ARC and XO are
		 * voted above as always: no vote here is ever dropped. */
		printf("slpi: m4: the slpi is not started again: the instance that has been running since "
		    "logs/040912-run.log built its glink edge minutes after that reset, and its items are "
		    "in shared memory now; what has never existed is our end of it, and that is what this "
		    "is for\n");
		slpi_sfr_read();
		slpi_partition_text();
		(void) slpi_glink_try(wait_seconds);
		thread_terminate(current_thread());
		return;
	}
	if (rootvnode != NULLVP && mdt_parse()) {
		/* slpi=5: make our outbound item first, the order a vendor kernel with both drivers
		 * probed would have anyway, and ring the doorbell once before the start: the pulse is
		 * lost if the SLPI's driver has not asked for its interrupt yet, which is why the
		 * watch below rings it again when it sees the SLPI's entries appear. */
		if (slpi_level >= 5 && slpi_smp2p_make_out() != 0) {
			slpi_smp2p_kick("the item is there before the slpi comes up, in case its driver is already listening");
		}
		started = slpi_start();
		image_forget();
	}
	if (started && slpi_level >= 5) {
		printf("slpi: m2: the slpi is out of reset; watching its SMP2P item for %u s\n", wait_seconds);
		(void) slpi_smp2p_watch(wait_seconds);
		/* The re-vote this used to make -- the load_state off/on sent a second time, to a q6
		 * that has been out of reset for the whole wait -- tested the one difference
		 * logs/033426-run.log had from the rounds that saw nothing, and logs/035630-run.log
		 * answered it: the entries appear given enough time whether the vote is re-sent or
		 * not, because the slpi's smp2p probe runs minutes after the reset is released.
		 * What that second wait did show, before the re-vote was dropped, is the ready bit
		 * still not coming with the entries already up and the vote taken twice: the bit
		 * waits for a host-side glink edge, and that is M4, not another observation. */
		/* What is left of the slpi's voice costs nothing to ask for: its SFR item, the
		 * reason the vendor's own restart code reads, and any text its firmware has left
		 * in shared memory. Both read shared memory, which always answers. */
		slpi_sfr_read();
		slpi_partition_text();
		/* M3b, the read of the image's own memory, is behind slpi=7 now: logs/033426-run.log
		 * showed that the region does not answer while the slpi owns it, and the run that
		 * read it lost its shutdown over it. See M3b. */
		if (slpi_level >= 7) {
			slpi_region_watch();
		}
	} else if (started) {
		uint32_t last = 0;
		const char *why = "";

		printf("slpi: m2: the slpi is out of reset; waiting up to %u s for its SMP2P ready bit\n", wait_seconds);
		if (slpi_wait_ready(wait_seconds * 1000, &last, &why)) {
			printf("slpi: THE SLPI IS UP: TrustZone authenticated and started it, and its SMP2P \"slave-kernel\" "
			    "entry says ready (value %#x; notes/wifi.md §5 M2)\n", last);
		} else {
			printf("slpi: the slpi did not say ready within %u s: its SMP2P entry reads %#x (%s). The image path "
			    "was through, so what is left is the subsystem's own start (notes/wifi.md §5 M3)\n", wait_seconds,
			    last, why);
			slpi_smp2p_show();
			slpi_smem_walk();
		}
	} else if (slpi_level >= 4) {
		printf("slpi: m2: the slpi did not start; slpi=4 asks for the q6 block anyway, and while "
		    "the subsystem is down that read ends the run: the watchdog will say so\n");
	}
	/* The q6 block is a register observation, behind slpi=4, and only when the subsystem did
	 * not start: logs/030641-run.log caught the first read of it stalling the reading thread
	 * for the rest of a run in which the SLPI *was* up, and a stalled thread there is what
	 * kept the boot processor from answering at shutdown. While the subsystem is down the read
	 * ends the run and the watchdog says so, which is the answer that read was for. */
	if (slpi_level >= 4 && !started) {
		sleep_ms(wait_seconds * 1000);
		q6_dump(started);
	}
	thread_terminate(current_thread());
}

__startup_func
static void
slpi_init(void)
{
	uint32_t level = 0;
	thread_t thread;

	if (!PE_parse_boot_argn("slpi", &level, sizeof(level)) || level == 0) {
		return;                                /* nothing is touched unless a boot-argument asks */
	}
	slpi_level = level;
	if (!cmdb_read() && level < 2) {
		return;
	}
	if (level < 2) {
		return;                                /* slpi=1: the list is the whole of it */
	}
	for (unsigned i = 0; i < sizeof(rails) / sizeof(rails[0]); i++) {
		rail_vote(&rails[i]);
	}
	arc_vote();
	xo_vote();
	if (level >= 3) {
		if (kernel_thread_start(start_thread, NULL, &thread) != KERN_SUCCESS) {
			printf("slpi: m2: no thread: the image will not be loaded\n");
			return;
		}
		thread_deallocate(thread);
	}
}
STARTUP(EARLY_BOOT, STARTUP_RANK_LAST, slpi_init);
