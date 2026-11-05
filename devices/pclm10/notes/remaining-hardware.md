# What is still on this phone, and what can be driven from bare metal

> **Current delivery status:** [功能与交付状态](功能与交付状态.md) separates test evidence from defaults and installed cold boot. This file is a chronological hardware investigation; older failures or successes below are not a current usability checklist. GPU composition, motor buzz and flashlight illumination have been demonstrated, but their default UI integration/delivery is incomplete. User reports touch/buttons usable only in hosted tests; cold-boot delivery is unresolved.

## GPU: the core takes commands, draws, and the system composites with it (2026-11-05)

> **Correction after interaction regression 03:** the renderer name "Adreno 7c+ Gen 3" below was incorrectly called an A640 name. The kernel still returned reference chip ID 0x06030500, selecting A635/gen4 in Mesa. PCLM10 DT specifies 0x06040001 and highest bank bit 15; A640 has 1 MiB GMEM and Mesa macrotile mode 0. Live interaction produced SMMU fault 0x3500, incomplete frames and user-visible lag. Prior execution evidence does not establish correct rendering or stability. Regression 04 corrects that hardware contract; runtime result pending (`logs/interactive-acceptance-04.md`).

- **The fix, measured**: the core's SMR must carry the device tree's own match mask.
  PCLM10's `gfx3d_user` is `iommus = <&gpu_smmu 0x0 0x401>` — stream ID 0, mask
  **0x401** — while the GMU's streams use mask 0x400. Every earlier probe wrote ID 0
  with mask 0x400 and read back zero, which is what led to the "stream 0 is reserved
  by the secure world" conclusion; that conclusion was wrong. With mask 0x401 every
  free stream table entry accepts the match (`logs/013009-run.log`: "DT SID0/mask401
  taken" for entries 0, 2, 3, 4, 5), entry 0 routes stream 0 to context 0, and the CP
  works at once: `THE 3D CORE TAKES COMMANDS` (ring read pointer 9 after CP_ME_INIT)
  and the scratch page reads `0x600dc0de` written by the GPU on command. The driver
  now keeps `SMMU_GMU_STREAM_MASK` (0x400) and `SMMU_CORE_STREAM_MASK` (0x401) apart.
- `logs/013516-run.log`: step 22, the 3D core fills a test picture through the CP —
  first pixel `00 80 ff ff`, middle `ff ff ff ff`, last row `ff 80 00 ff`, matching
  the orange/white/blue bands — and it goes to the panel; the eye confirmed it.
- `logs/014041-run.log`: with `adreno_keep=1` the GPU stays on and the `HakiosGPU`
  service starts; backboardd still drew with its software renderer because the small
  root image had no `libhakios.gl.dylib` (it is built with `GL=1`; the image grew
  from 4 MB to 6 MB and now carries it and `libOSMesa.8.dylib`).
- `logs/014918-run.log`, **the system composites with the GPU**: all 8 EAGLContext
  methods are replaced, CoreAnimation gets "an OpenGL ES 3 context ... OpenGL ES 3.2
  Mesa 25.0.7; Adreno 7c+ Gen 3" (Mesa's own name for this A640, so the rasterizer
  is freedreno on our hardware, not llvmpipe), compiles its first shaders, attaches
  IOSurfaces as textures ("copied into its memory by the GPU"), and draws: frame 1
  136 GL calls / 4 draws / 10962 KB refilled from an IOSurface / 86.9 ms, frame 2
  65 calls / 3.0 ms. Both reach the hardware — `HakiosGPU: submission 1/2`, the ring
  write pointer 159 then 203 with the read pointer following — and backboardd swaps
  both to the panel (`MiniIOMobileFramebuffer: swap 1/2`). No CP fault, no GPU bus
  error, no crash; 28/28 thermal sensors, hottest 42.0 C over the 90 s round.
- After those two frames the scene is static and no further frames are asked for
  (touch reports 0 downs — nobody touched it). That is the system's state, not the
  GPU's; sustained animation, GPU frequency scaling, idle collapse, and recovery from
  a lost device remain unverified.
- The working test recipe, all short rounds with `bootcpu=7` and `thermal_pclm10=1`:
  `adreno=22 adreno_skip=0x1cc0 adreno_no_oob=1 adreno_gpucc_restore=1
  adreno_vendor_rptr=1 adreno_vendor_nc=1 adreno_vendor_protect=1
  adreno_halt_observe=1 adreno_walk=1 adreno_keep=1`.

## Heat: who spends the CPU while the phone gets warm (2026-11-05, night)

Measured with `userland/rc.jobs.cpuprof` — the default flow plus five `ps -A`
snapshots, twenty seconds apart. TIME in `ps` is cumulative, so consecutive
snapshots say who is spending the CPU right then.

- **lsd's first-boot work, one round**: `logs/020628-run.log` (90 s, lsd's own
  os_log opened with `OSLOG='com.apple.lsd com.apple.backboardd'`). The
  LaunchServices store `com.apple.LaunchServices-4035-v2.csstore` does not
  exist, so lsd builds it from scratch, and along the way creates 662
  containers ("Made container live for ..."), each followed by
  `container_set_data_protection_for_current_user: error = 0b(72) UNSUPPORTED`
  (no file-level encryption here). lsd took 20.91 s of CPU inside its first
  30 s alive — about 70% of one core — containermanagerd +12.3 s and
  usermanagerd +4.0 s with it (`logs/020310-run.log`).
- **It ends; it is not a loop**: `logs/020952-run.log` (150 s, five snapshots).
  lsd's TIME freezes at 0:40.95 between the fourth and fifth snapshot,
  containermanagerd at 0:21.40, usermanagerd at 0:07.10. The whole first-boot
  pass costs about 100 s of one core and then stops. The kernel's
  "process lsd denied attempt to escape CPU monitor" lines keep coming after
  that at ~10/s until the round ends — noise, negligible CPU: privilege 1015
  is `PRIV_PROC_CPUMON_OVERRIDE` (`xnu bsd/sys/priv.h`), answered from the
  entitlement `com.apple.private.kernel.override-cpumon`
  (`bsd/kern/kern_minimac.c`), and lsd's own signed entitlements really do
  not carry it, so a stock phone would refuse it too — it just never reaches
  the threshold there, because its database is already built.
- **Why prebaking the store cannot work**: the container UUIDs are random
  every boot — `InternalDaemon/BEE7B133-...` in 020628, `InternalDaemon/
  32FB1AB3-...` in 020952. The csstore path is inside one of those
  containers. The fix is a persistent /var (the flash-root recipe), not a
  pre-generated image. Unverified until a persistent round is run.
- **The steady-state heater, found in the same round**: the display blanks on
  idle sleep about ninety seconds in ("Set Blanked: YES", backlight 0), and
  with no sleepd in the job set SpringBoard then asks launchd for
  `com.apple.sleepd.sleepserver`, `com.apple.commcenter.xpc` and
  `com.apple.chronod` about a thousand times a second — launchd's TIME went
  4.18 s → 8.67 s → 17.32 s over the last 40 s, 43% of a core and still
  climbing, while every other process had gone quiet; the log says so itself
  ("Last log repeated 902 times", "Could not find job with label
  com.apple.chronod").
- **Fixed by starting the two daemons**: `com.apple.sleepd` and
  `com.apple.chronod` are in the default job set now (`build.sh`, `SLEEPD=0`
  leaves them out). `logs/021536-run.log` (REST, 120 s) and
  `logs/022040-run.log` (default set, 120 s, the image as it now builds by
  default): both start on demand, run, never crash; **zero** failed lookups
  for `com.apple.sleepd.sleepserver` in 022040; launchd's last-20 s cost
  fell from 8.65 s (43%) to 3.78 s (19%). What is left at that hour is
  chronod driving the lock-screen widgets (WeatherWidget, PassbookWidget,
  ScreenTimeWidget) which look for locationd/geod/proactive and retry — a
  handful of lookups a second, not a thousand. Hottest sensor 44.8 C, below
  the 48.2 C of the same-length REST round and far from the 60 C stop line.
- **Still unverified about heat**: absolute sensor accuracy, and anything the
  battery or the shell would say — bq27541 never answers, its percentage is
  a resting-voltage estimate. Steady-state idle (no first-boot work, screen
  off, minutes rather than seconds) has not been measured, because it needs
  a persistent /var to be meaningful.

## GPU follow-up: 2026-11-04, after 082531

- The completed gfx.lvl transaction in 082531 used AP RSC at 0x18220d00,
  not GPU RSCC at 0x2c8d000. It does not establish GPU RSCC transport health.
- RSCC commands at 0x50000/0x50034 and related addresses match the HFI bus
  bandwidth table. In particular, 0x50034 is not the gfx.lvl resource (0x30050).
  Zero command enables/status in a snapshot do not alone prove a stuck active transfer.
- Source comparison with vendor `_load_gmu_rpmh_ucode` and Linux v6.7
  `rpmh-rsc.c` found two unsupported host actions: forcing all four GPU slots
  into AMC mode and reloading the PDC/RSC programs in step 16 after CM3 starts.
  Both have been removed for the next controlled hardware run. The diagnostic
  field at slot +0x10 is now correctly labelled `wait mask`, not `idle`.
- The modified kernel built and was copied successfully. Hardware verification
  followed using the already-resident loader (no `fastboot boot` or flash).
  No GPU pass or configuration promotion is claimed.
- `logs/105113-run.log`: skipping the step-16 sequencer rewrite and the forced
  AMC-mode writes preserved GMU bring-up, five HFI acknowledgements, and the
  old failure: no oob GPU acknowledgement, HFI START, or CP register readback.
  Slot controls still read 0x10000 before GMU runs, so these bits were not
  caused solely by the deleted writes. The A640 BW table in the driver already
  matched vendor CNOC off votes; zero in the RSCC snapshot is not evidence of
  a broken table.
- `logs/110136-run.log`: vendor SPTPRAC control value 0x778000 written only
  after GX domain power-up: control reads 0, power/clock status stays 0x80.
- `logs/110655-run.log`: read-only GX-on survey found 0x1054=0x80004220
  (gfx3d branch halted), 0x101c=0x80000000 (RCG root off), 0x1060=1
  (the earlier clock probe was not the gfx3d gate). Similar downstream
  GPUCCs place the gfx3d branch/RCG at 0x1054/0x101c; this SoC's
  mainline GPUCC exposes neither as a host-owned clock.
- `logs/111213-run.log`: controlled attempt at 0x101c/0x1054/0x10a4/0x10a8:
  GX branch enable bit latched but halt remained, RCG root stayed off and
  the CX mirrors' writes read back zero. GMU status still 0x80, HFI START
  still unanswered, CP_RB_BASE still zero after a write. This is a failure,
  not a working GPU. The driver's misleading unconditional success log after
  reading GX registers has been replaced by an explicitly inconclusive probe.
- The actual 37,680-byte `a640_gmu.bin` was extracted from the 4 MiB
  `userland/root.apfs` with `tools/apfs/apfs.py cat` into the session's temp
  directory; the ITCM blocks at 0/0x200/0x400 were assembled into a 16 KiB
  Thumb image for independent firmware analysis. REA on this Windows host
  cannot analyze a raw ARM target (no configured compatible provider), so
  static investigation uses the available WSL LLVM tools instead. A separate
  read-only firmware analysis initially misidentified ITCM 0x2c7e..0x2c94
  as a wait loop. Correct control flow: its zero branch joins the call at
  0x2c8a and returns after writing DTCM 0x402a0. It is **not** a wait
  loop and a specific firmware deadlock remains unproved. `logs/112106-run.log`
  reconfirms status 0x80, CP_RB_BASE zero,
  and the revised truthful register-probe log; no GPU pass is claimed.
- `logs/112853-run.log`: four read-only snapshots of the *live DTCM window*
  (before msg14, after ACK, after OOB attempts, after msg10 timeout) find
  DTCM[0x402a0] consistently zero, followed by 0x42228/0x4227c/0x422d0.
  Queue 0 advances 228/228 -> 232/232 at msg14 ACK but stays 232/236
  after msg10, with response queue unchanged at 20/20. GX clock status
  remains 0x80. The static routine mentioning 0x402a0 is not a wait loop;
  the zero value does not authorize changing this shared state. Step 20
  now refuses to start CP if the
  CP_RB_BASE register does not retain the programmed ring address;
  this run correctly stopped with a clear failure instead of probing a
  nonresponsive CP. GPU is still unverified.
- `logs/113549-run.log`: after removing host writes to both candidate gfx3d
  clocks and the incorrect 0x1030/0x1060 clock probe, the first read still
  found RCG 0x80000002 and GX branch 0x80004221. These are the values from
  the preceding direct-write experiment, retained across `xl run` iterations
  in the resident loader, not evidence the firmware reprogrammed them.
  GMU status stays 0x80 and CP_RB_BASE 0. A fresh power-cycle baseline
  would be cleaner, but `fastboot boot` is disallowed by the test constraints
  and a reboot out of the resident loader needs a compatible way back; do
  not attribute the persistent clock bits to the GMU until that is resolved.
- `logs/114432-run.log`: built and booted with the host's old, incorrect
  0x1030/0x1060 clock writes and the unsuccessful 0x101c/0x1054 clock
  overrides removed. Same outcome (GMU starts; 3D clocks still off and
  CP register readback fails). The clock-control values 0x80000002 and
  0x80004221 remain retained from prior iterations. Static Thumb study
  corrected a false wait-loop hypothesis at ITCM 0x2c70; the actual
  blocking point is still unknown. A return address on the GMU stack
  is not a direct PC observation.
- `logs/115458-run.log`: **single-variable timing control**
  `adreno_start_first=1`: HFI START msg10, sent immediately after msg14's
  acknowledgement, received its own success ACK; HFI command and answer
  indices reached 236/236 and 24/24. Without this control, the OOB GPU
  request and its two multi-second waits came first and msg10 was left at
  command indices 232/236 with no ACK. Therefore HFI START framing is
  viable and the failure correlates with OOB-first sequencing; it does
  not establish whether the request itself or elapsed waiting time causes
  the stall. Even in the START-first run, oob_gpu got no acknowledgement,
  SPTPRAC status stayed 0x80 and CP_RB_BASE read back zero. This is not
  functional GPU power-up.
- `logs/120055-run.log`: with START-first timing, the vendor-defined
  `A6XX_GMU_NMI_CONTROL_STATUS` (GMU base + 4*0x23f0) read zero before
  msg14, after its ACK, after successful msg10, and after the OOB attempts.
  Thus this register shows no persistent NMI indication at sampled times;
  a transient ICSR pending/active state inside CM3 remains unobserved.
  OOB remains unacknowledged and CP_RB_BASE zero.
- `logs/120607-run.log`: after msg14 ACK, waited four seconds with no
  OOB request and then sent msg10. The GMU still accepted msg10, taking
  command/response queues to 236/236 and 24/24, respectively. This
  rules out a simple four-second timeout as the reason the original
  OOB-first sequence stopped consuming HFI. It implicates the OOB
  request path (or associated state/interrupt handling), but does not
  identify the exact stuck instruction. Following OOB, GX clock remains
  off and CP_RB_BASE still cannot retain a write.
- `logs/121128-run.log`: only new-style bit30 OOB and no legacy bit16
  fallback. Msg10 still not consumed (queue 232/236, ACK 20/20), clock
  still off (0x80): the legacy fallback is not a necessary cause.
- `logs/121635-run.log`: after failed bit30 OOB, tried vendor's bit31
  HOST2GMU clear. RAW read 0x80000000, but msg10 still not consumed;
  releasing the request after its timeout does not revive HFI.
- `logs/122147-run.log`: bit30 request, poll only ~10 ms, no legacy
  fallback. RAW already cleared and no bit31 ACK; immediate msg10 not
  consumed, GX clock still 0x80 and CP remains inaccessible. Together
  with `120607`, this rules out a four-second idle delay as the sufficient
  cause. The bit30 request or its processing path stalls further HFI;
  the exact firmware instruction/hardware prerequisite is still unknown.
- `logs/122837-run.log`: single-variable `adreno_sptp_off=1` wrote vendor
  inter-frame base value 0x9c40400 (readback exact) instead of the
  SPTP_PC-specific 0x9c40405 (which reads 0x9c40401). The ~10 ms bit30
  OOB still vanished without an ACK; GX clock status remained 0x80 and
  CP_RB_BASE did not retain a write. This does not support bit2 readback
  as a sufficient root cause. The test was in the same resident loader
  and thus is not a cold-boot comparison.
- **2026-11-04 breakthrough (`logs/135448-run.log`)**: sending oob_gpu is
  what broke GX power-up. With `adreno_no_oob=1` (skip oob_gpu entirely,
  msg14 and msg10 still sent), the firmware completed the core power-up:
  `SPTPRAC_PWR_CLK_STATUS` went 0x80 -> 0x0 (GX clock ON), and the 3D
  registers became readable/writable: CP_RB_BASE retained 0x1000000,
  CP_SQE_INSTR_BASE 0x1100000, CP_RB_CNTL 0x800020c, RBBM_STATUS 0x1.
  Step 20 started the CP and CP_HW_FAULT stayed 0, SMMU sgfsr 0.
  Remaining failure moved into the CP: the ring never advanced
  (CP_RB_RPTR 0 of 9), RBBM_STATUS 0x800005 and CP_INTERRUPT_STATUS 0x80.
  So GPU power/clock bring-up is solved; CP ring/microcode bring-up is
  the new (and much narrower) blocker.
- `logs/135711-run.log`: adding vendor's CP_RB_RPTR_ADDR (scratch page)
  before CP start did not advance the ring either. Same CP interrupt 0x80.
  Step 18 reports the hypervisor refuses every writable SMR entry for
  stream 0 ("stream 0 refused", SMR reads 0), so the CP's SQE microcode
  fetch may not be translated by context 0; this is now the prime suspect
  for the CP failure (vendor also performs an SCM `CP_SMMU_APERTURE` call
  for a640v1 before start, which this driver does not).
- **CP bring-up detail (144845/145120/145219/145329/145508)**: with
  `adreno_no_oob=1 adreno_gpucc_restore=1`, the GMU no longer reports
  `PwrIdleStateMgrStart` (HFI error 115/125) and GX power/clock are
  healthy. CP registers are readable/writable and the SQE starts, but
  `CP_INTERRUPT_STATUS` bit 7 (`A6XX_CP_ILLEGAL_INSTR_ERROR`) reappears
  immediately after clearing it, `CP_RB_RPTR` stays 0 and `CP_RB_WPTR`
  reads back correctly. `CP_HW_FAULT`=0 and `SMMU_SGFSR` stays 0 even when
  `CP_SQE_INSTR_BASE` is pointed at unmapped 0x7f000000 (`adreno_blind`),
  so the CP's instruction fetch is not going through the GPU SMMU context
  we program (and no fault is reported). The vendor's a640v1 path calls
  `adreno_program_smmu_aperture()` (SCM `QCOM_SCM_SVC_MP` 0x0c /
  `CP_SMMU_APERTURE_ID` 0x1b, args `{0xffff0000|cb, 0xffffffff x3}`).
  That call now succeeds on this phone (`adreno_cp_aperture=1`, returns
  0) but does not by itself fix the illegal instruction. Tried and ruled
  out: skipping the SQE header (Linux does skip it: `fw->size-4`,
  `&fw->data[4]`), whole-file load, physical/contiguous CP buffers,
  privileged SQE PTE, clearing stale CP interrupt status. The remaining
  suspect is the CP's microcode fetch/aperture mapping (or secure-mode
  fetch), not power, clocks or the ring-buffer registers.
- CP fetch investigation continued (`logs/145508`–`151207`): SQE microcode
  uses Linux's layout (`fw->data[4]`, `fw->size-4`; the first dword is a
  header whose low 12 bits are version 0x190). Ruled out as causes:
  whole-file load, physically contiguous CP buffers with physical
  `CP_SQE_INSTR_BASE`, privileged SQE PTE, stale CP interrupt status, and
  `CP_APRIV_CNTL` (a640 has no HW_APRIV quirk; its write reads 0xffffffff
  and is now behind `adreno_apriv`). The SCM
  `CP_SMMU_APERTURE` call returns 0 for context bank 0 and 1 but does not
  by itself fix the illegal-instruction interrupt. Attempting to share the
  GMU's page table (blocks mapped into the GMU space, `adreno_shared_cb=1`)
  exposes that the hypervisor rejects changes to the SMR match mask: entry
  1 reads back 0x84000005 even after writing mask 0x405. So the GPU's
  stream table is hypervisor-owned; stream 0 cannot be redirected by the
  host. The CP's instruction fetch therefore still goes somewhere we do
  not control (or a secure aperture), and the remaining work is the
  secure-SMMU/CP-aperture contract (kgsl's qcom_scm secure page-table
  calls), not another clock or ring register.
- CP/SMMU follow-up (`logs/152327`–`153503`): the zap image is an MDT
  whose only `PT_LOAD` segment is the relocatable `a640_zap.b02`
  (`p_flags` 0x08000007); `mdt_loader.c` therefore places it at offset 0
  of a 0x1000 region, so the original single-page b02 load was correct.
  A multi-segment attempt was rejected by TrustZone and reverted. The SQE
  image in memory was verified word for word (`0x016ee190 0x01001f0b …`)
  and both loading offsets (`+4` vs whole file) were tried.
- The hypervisor locks the GPU stream table: SMR entries other than 1 are
  unwritable, and entry 1's ID/mask cannot be changed (`0x84000005` stays
  even after writing stream 0 or mask 0x405). Sharing CB0 between GMU and
  core works (GMU still runs), so CB0 translation is functional, yet the
  CP still raises `A6XX_CP_ILLEGAL_INSTR_ERROR` immediately. The SCM
  `CP_SMMU_APERTURE` call returns 0 for CB 0/1/2, and the secure page
  table size call (`0x0c/0x03`) returns 0/0. Physical/contiguous
  `CP_SQE_INSTR_BASE` also fails, and `SMMU_SGFSR` stays 0 even with an
  unmapped base. So the CP's instruction fetch is outside the GPU SMMU
  contexts this driver can program, and the remaining work is to replicate
  the vendor's `kgsl_mmu_start()` / secure-context setup (the public
  `drivers/gpu/msm/kgsl_iommu.c` is now in the session temp directory).
- Mainline's own order (`a6xx_gpu.c`): the CP is started and runs
  `a6xx_cp_init` **before** the zap is loaded; only then does the host send
  `CP_SET_SECURE_MODE(0)` through the ring. If the zap is absent, mainline
  writes `RBBM_SECVID_TRUST_CNTL = 0` to leave secure mode manually — and
  warns that with a zap present that write is blocked and faults. Our
  write hangs the bus, which confirms the GPU is still in secure mode: the
  secure firmware (the zap) is not running, so the CP's initial microcode
  is TZ's, not ours, and `CP_SQE_INSTR_BASE` is not yet in effect.
  The next check is whether `PAS_AUTH_AND_RESET` really started the zap on
  this phone (the vendor `subsys-pil-tz.c` additionally votes the PAS bus
  bandwidth around the call; our driver does not).
- **Root cause of the CP failure, measured (`logs/155023-run.log`)**: the
  SMMU's stream-match registers are not globally locked. Entry 1 accepts a
  changed stream ID (5 -> 7 reads back 7), and entry 0 accepts **ID 7**
  (reads `0x84000007`) but **ignores ID 0** (stays 7). So **stream ID 0 is
  reserved by the secure world** and cannot be matched from the non-secure
  side at all. The 3D core's CP is stream 0, so its instruction fetch and
  ring accesses go through the hypervisor's secure routing, no matter how
  the host programs SMR/S2CR/CB. This also explains why every CB and the
  SCM aperture call made no difference, and why `SGFSR` stays 0.
  The remaining work is entirely on the secure side: make the zap shader
  actually take the GPU out of secure mode (it currently leaves
  `RBBM_SECVID_TRUST_CNTL` at 0x1) or find the SCM call that releases
  stream 0.
- The same illegal-instruction result holds with the core page table moved
  to context banks 2 and 3 and `CP_SMMU_APERTURE` naming the same bank, and
  with S2CR[0] (the unmatched-stream default) pointed at the GMU's working
  CB1. All CBs 0..3 therefore fail identically, which rules out "wrong
  context bank" as the explanation.
- The vendor `arm-smmu.c` shows `disable_bypass = true` (unmatched streams
  are terminated: `sCR0.USFCFG`) and that with `qcom,skip-init` the
  bootloader's SMR/S2CR table is kept. Since that table is not readable
  and not writable from the non-secure side for any entry except 1, the
  GPU's stream-0 route is fixed by the secure firmware.
- `RBBM_SECVID_TRUST_CNTL` (GPU word 0xF400) reads **0x1 before and after
  the zap**, i.e. the zap does not change it. Writing 0
  to it from the non-secure side hung the GPU bus (`logs/153839`), so it
  cannot be changed by the host. The GPU SMMU DT node has `qcom,dynamic`,
  `qcom,skip-init`, two TBUs (stream ranges 0..0x3ff and 0x400..0x7ff) and
  the vendor `arm-smmu.c` speaks of **secure aliases** of the stream
  registers (`nsCR0` at 0x400 etc.). Taken together: the CP's microcode
  fetch is a secure transaction served by the hypervisor's secure SMR/S2CR
  view, not by the non-secure context banks this driver can write. The
  remaining work is to make the zap shader actually clear the GPU's
  trusted state (vendor PIL secure path), after which the CP fetch should
  use our non-secure CB.
- Reference projects checked: `hakios-metal` is a userland Metal driver
  for a different Adreno (7c+ Gen3) and does not contain kernel CP/SMMU
  code; `dipper-xnu` is an SDM845/A630 port using the same hackios kernel
  driver (GPU via Mesa/freedreno). Its upstream `hakios.adreno.c` has the
  same CP start sequence (CP_SQE_INSTR_BASE, CP_RB_CNTL, CP_SQE_CNTL=1),
  confirming the driver sequence is right and the difference is this
  platform's secure SMMU.
## Bluetooth, camera and the torch/haptics action round (2026-11-04)

- **Bluetooth is verified on the phone** (`logs/163728-run.log`,
  `wcnbt=2`): the three rails took their RPMh votes, GCC's qup2
  m-ahb/s-ahb/engine-3 branches are on, gpio43-46 are qup13, the WCN3990
  reports `product 0xa, soc 0x40010224, rom 0x1001, patch 0x6699`, and
  HCI Reset answers `num_cmds 1, opcode 0, status 0`. The driver used to
  `ml_io_map` two whole 3 MB TLMM tiles; `kmem_alloc(0x300000)` then
  failed with a resource shortage (`logs/163026`). It now maps two small
  windows (EAST gpio43-46, NORTH gpio172-174). `device.conf` has the
  `bluetooth works` status; firmware loading is the next step
  (`qca/crbtfw01.tlv`, `qca/crnv01.bin`).
- **Camera `cam=1` now passes end to end** (`logs/164351-run.log`): the
  TLMM windows were also one page short (gpio51/52 need 0x2000, gpio13..34
  need 0x16000), which had put pin 52's registers outside the mapping and
  hung the PM8008 read. With that fixed the read-only look completes: the
  sensor table, GCC, titan GDSC, all four MCLK roots, every pin, the
  three PMIC GPIOs, PM8008 #2's seven LDOs (1200/2800/1800/2800 mV, mode
  0x7, enable 0), and PM8008 #1 reporting no answer because its enable is
  low.
- **Camera `cam=2` powers the rear main sensor correctly**
  (`logs/164459-run.log`): PM8008 #2's chip answers (revision ...01), LDO1
  1100 mV, LDO7 2900 mV, LDO3 2800 mV and LDO4 1800 mV come up, MCLK0 runs
  at 19.200 MHz from the crystal on gpio13, the sensor is taken out of
  reset, then the CCI bus (gpio17/18) reads both lines low even with the
  pull-ups, so the address scan is skipped and everything is powered down
  cleanly. The CCI lines being clamped points at the sensor's I/O rail or
  its pull-up supply, which is the next camera step.
- **Camera, a real address bug fixed** (`logs/164014-run.log`): the
  driver mapped TLMM at `0x0f100000`, which is not a TLMM address on this
  SoC; the first pin read hung the bus (`logs/162817`, `logs/162922`).
  The tiles are offsets from `pinctrl@03000000` (vendor
  `pinctrl-sm8150.c`: NORTH 0x00900000, SOUTH 0x00D00000), so the driver
  now maps gpio13..34 at NORTH+0xd000 (0x6000 long) and gpio51/52 at
  SOUTH+0x33000 (0x1000). `cam=1` now reads the sensor table, GCC, the
  titan GDSC, all four MCLK roots, every camera pin, and the three PMIC
  GPIOs correctly. It stops at the PM8008 #1 I2C read: the PM8008's bus
  (SE4, gpio51/52, bit-banged) is clamped, so that chip's power tree (its
  VIN) is the next camera step, not the TLMM.
- **Torch and haptics action round** (`logs/164135-run.log`): the torch
  ran the vendor's LED-0 sequence (12.5 mA step, software strobe, 300 mA,
  safety timer off, module + LED enable), read `en_led 0x1 / mod_ctrl
  0x80` while lit, then switched off and restored the registers. Haptics
  played the tree's recipe (LRA, sine, 3400 mV, 6667 us, direct), read
  `status1 0x82` while playing, then stopped and restored. Both need the
  owner's eyes and ears for the physical result.
- Further investigation noted that a cold baseline cannot be produced by
  a simple `fastboot reboot` and return under the flash/reboot-only rule:
  `xl enter` from ABL uses the prohibited `fastboot boot`, and there is no
  verified PCLM10 flash target/recovery image in this workspace for a
  persistent loader. Do not flash a guessed partition just to clear
  GPUCC registers. A reboot may also retain always-on PMIC state.
- `logs/124016-run.log`: single-variable probe enabled SM8150 GX_GMU
  clock at GPUCC +0x1064 only after GX domain CFG bit16 reported powered.
  CBCR changed from 0x80000000 (halt) to 0x1 (running), but GMU GX
  clock status stayed 0x80; bit30 OOB had no bit31 ACK, msg10 was not
  consumed and CP_RB_BASE read back zero. Thus opening this upstream
  GMU branch is possible but not sufficient to start the 3D core.
- `logs/130512-run.log`: added the vendor's pre-OOB `adreno_deassert_gbif_halt`
  equivalent. Both the GX-side `RBBM_GBIF_HALT` and CX-side `GBIF_HALT`
  already read 0 before OOB (`CX halt ack 0x3`); clearing them changed
  nothing. The short bit30 OOB still vanished without bit31 ACK, HFI START
  stalled and GX clock stayed 0x80. A stale GBIF halt is not the cause.
  (The preceding `130358` round panicked in the driver because the new
  GBIF probe read `gpu.base + 0x58` before `need(&gpu)` from early step 17;
  that was a driver bug, fixed with `need(&gpu)/need(&gbif)`, not a GPU
  hardware fault.)
- `logs/124641-run.log`: omitted two A650-derived CX FAL interface
  writes from the A640 GMU pre-reset sequence (`adreno_no_fal=1`), with
  other OOB controls matching the 122147 round. GMU and HFI still came up;
  OOB bit30 still vanished without bit31 ACK, status stayed 0x80 and
  CP_RB_BASE read zero. Skipping those writes alone did not resolve it.

Everything the port has not taken over yet, read out of the phone's own device tree and matched to
the vendor driver that owns each part. Nothing here was measured on the phone — the phone was not
touched for this pass — so every "present" below means *the tree says it is there and enabled*, and
every reachability verdict is a reading of the driver's code, not a probe result.

Sources used:

- `vendor-src/dtb/android-merged.dts` — the boot DT the kernel is started with (the base).
- `vendor-src/dtb/dtbo.bin.01.dts` — the one overlay the bootloader applies on top of it.
- `github.com/oppo-source/android_kernel_oppo_sm8150` and
  `.../android_kernel_modules_and_devicetree_oppo_sm8150`, branch `oppo/sm8150_s_12.1_oppo_reno10`.

Fetched 2026-11-02 with `curl`; the scripts are `fetch2.sh` / `fetch3.sh` in the session's temp
directory. Every file listed near the end was non-empty real C/H/DTS when it landed.

## How the two trees were put back together

The base DT alone is misleading and the overlay alone is unreadable, so they were joined:

- `dtbo.bin.01.dts` is **one** overlay: `fragment@0 … fragment@81` at lines 9–7804, then
  `__symbols__` (7805), `__fixups__` (8091), `__local_fixups__` (8246). The second run of
  `fragment@0…` at line 8248 is local-fixup metadata, **not** a second overlay.
- Each fragment's `target = <0xffffffff>` was resolved by finding the label whose `__fixups__` entry
  contains `/fragment@N:target:0`, then looking that label up in the base DT's `__symbols__`
  (1550 entries) to get a path.
- The overlay's own `__symbols__` states the interesting paths outright, which is the strongest
  evidence there is. Line 7855:
  `pm8150b_haptics = "/fragment@2/__overlay__/qcom,pm8150b@3/qcom,haptics@c000"`. Line 7889:
  `flash_led = "/fragment@4/__overlay__/qcom,pm8150l@5/qcom,leds@d300"`. Line ~7900:
  `pm8150l_wled = "/fragment@4/__overlay__/qcom,pm8150l@5/qcom,wled@d800"`.
- **The method was validated against hardware the port already drives.** fragment@63 resolves to
  `/soc/i2c@0xc80000` = SE17 = gpio55/56, which is exactly the bus where the bit-banged Synaptics
  touch works today. A wrong resolution would not have landed there.
- **The boot DT has no PMIC children at all.** `/soc/qcom,spmi@c440000` (line 3175) has no
  `qcom,spmi-pmic` child. Every PMIC device — haptics, flash, WLED, SMB5, PON, pinctrl, vadc —
  comes from overlay fragments 0, 2 and 4. Any PMIC work that cites `android-merged.dts` as its
  authority is citing a tree that does not contain the device.

## The QUP buses: what is on each one

Base status is read from the base DT's own `status` property; "overlay" is what the overlay does on
top. Each serial engine is described twice in the base tree (an `i2c@…` node and a
`qcom,qup_uart@…`/`spi@…` node over the same registers); the SE number and pins come from the
pinctrl group each node points at.

| SE | registers | pins (function) | base | overlay | devices |
|---|---|---|---|---|---|
| SE0 | `0x880000` | gpio0/1 (qup0) | disabled | — | nothing |
| SE1 | `i2c@884000` | gpio115 only (qup1) | disabled | fragment@64 `status="okay"`, `qcom,clk-freq-out = <0x61a80>` (400 kHz) | `mp2650-charger@5c` |
| SE2 | `0x888000` | gpio126/127 (qup2) | disabled | — | nothing |
| SE3 | `spi@88c000` | gpio144/145 (qup3) | disabled | fragment@49 enables it | no children |
| **SE4** | `i2c@890000` | gpio51/52 | **okay** | fragments 45/50/72/77 add children | base: `fsa4480@43`, `dhall0-m1120@0f`, `dhall1-m1120@0c`; overlay: `qcom,smb1390@10`, `smb1390_slave@18` (disabled), `smb1355@0c`, `aqt1000-i2c-codec@0d`, `max989xx@39`, `max989xx@3a` |
| SE5 | `0x894000` | gpio121/122 (qup5) | disabled | — | nothing |
| SE6 | `0x898000` | gpio6/7 (qup6) | disabled | — | nothing |
| SE7 | `i2c@89c000` | gpio98/99 | disabled | fragment@51 `status="okay"` | `aw8697_haptic@5A` |
| **SE8** | `i2c@a80000` | gpio88/89 (qup8) | **okay**, 100 kHz | — | `bq27541-battery@55`, `oppo_short-ic@58`, `stm8s_fastcg@26` (the older set, see the correction below) |
| SE9 | `i2c@a84000` | gpio39/40 | disabled | fragment@53 `status="ok"` | `nq@28` (NFC) |
| SE10 | `0xa88000` | gpio9 (qup10) | disabled | — | nothing |
| SE11 | `0xa8c000` | — | disabled | — | nothing |
| SE12 | `qcom,qup_uart@0xa90000` | gpio85/86 | disabled | fragment@46 enables it | Bluetooth WCN3990 H:UART |
| SE13 | `qcom,qup_uart@0xc8c000` | gpio43–46 | disabled | fragment@48 enables it | 4-wire UART, no children |
| SE15 | `i2c@0xc94000` | gpio27/28 (qup15) | **disabled** | fragment@65 `status="okay"` + children | `da9313-divider@68`, `bq27541-battery@55`, `oplus_short-ic@58`, `stm8s_fastcg@26` |
| SE17 | `i2c@0xc80000` | gpio55/56 (qup17) | — | fragment@63 adds child | `synaptics19081@20` (the touch this port drives) |
| SLIM | `slim@171c0000` | — | present | fragments 66/67/71 | `tavil_codec`, `pahu_codec`, 4 × `wsa881x` |
| SD | `sdhci@8804000` | — | present | fragment@57 | the microSD slot |

Two things follow from this table that are worth stating plainly:

1. **The gauge and the VOOC controller appear twice, on two different buses** — SE8 in the base tree
   and SE15 in the overlay. See the correction below.
2. **SE4 is the busy bus and it is shared by things that collide.** `smb1355@0c` and the base's
   `dhall1-m1120@0c` are both at address 0x0c on the same bus, and `smb1390@10` and `smb1355@0c`
   are both `status="ok"`. A tree that enables two devices at one address is a family tree, not a
   board tree: **probe before believing a part is fitted.**

## The PMIC tree: which slave holds what

Read from the `reg = <0xN 0x00>` of each `qcom,spmi-pmic` child in fragments 0/2/4, and confirmed
by the overlay's own `__symbols__` paths quoted above.

| slave | chip | peripherals in this tree |
|---|---|---|
| 0 | PM8150 | `qcom,power-on@800` (the power key this port reads), `qcom,rtc@5c00`, `pinctrl@c000`, `vadc@3100`, `clock-controller@5b00`, `temp-alarm@2400` |
| 1 | PM8150 | (companion; no interesting children) |
| 2 | PM8150B | `qcom,revid@100`, `qcom,power-on@800`, `qcom,pbs@7200`, `sdam-qnovo@b000`, `pinctrl@c000`, `vadc@3100`, **`qcom,qpnp-smb5`** (the charger), `usb-pdphy@1700`, `adc_tm@3500` |
| 3 | PM8150B | **`qcom,haptics@c000`** (the LRA driver), `qcom,pwms@b100`, `qcom,leds@d000` tri-led (`hr_led1`, `hr_led2` — the in-display fingerprint light source) |
| 4 | PM8150L | `qcom,revid@100`, `qcom,power-on@800`, `clock-controller@5b00`, `pinctrl@c000`, `vadc@3100` — **this is the slave whose GPIO 5 the port already drives** for the touch's 1.8 V enable |
| 5 | PM8150L | `qcom,lcdb@ec00`, **`qcom,leds@d300`** (flash LED), **`qcom,wled@d800`** (backlight), `qcom,pwms@b100` + `@bc00`, `qcom,leds@d000` tri-led (red/green/blue), `qcom,amoled` (disabled) |

## Haptics — two candidates, and both are broken in this tree

| node | compatible | bus / address | status | notes |
|---|---|---|---|---|
| `qcom,haptics@c000` | `qcom,haptics` | SPMI **slave 3**, `reg = <0xc000 0x100>` | present, no `status` = enabled | irqs `<3 0xc0 0 3>` (short-circuit) and `<3 0xc0 1 3>` (play). `qcom,actuator-type = "lra"`, `qcom,lra-vmax-mv = <0xd48>` (3400), `qcom,lra-play-rate-us = <0x1a0b>` (6667), `qcom,wave-shape = "sine"`, `qcom,auto-res-mode = "qwd"`, `qcom,lra-allow-variable-play-rate`, and six `wf_*` effect nodes (wf-vmax 0xe10, patterns of 0x7e/0x3e). No supply properties. |
| `aw8697_haptic@5A` | `awinic,aw8697_haptic,19081` | I2C SE7 (`i2c@89c000`), 0x5A | `status="okay"` via fragment@51 | reset gpio116, irq gpio24, pinctrl group `aw_haptics`. |

**Neither one would bind to its vendor driver as this tree stands.**

- The AW8697 driver in the modules repo has exactly one match string —
  `vendor/oplus/kernel/vibrator/aw8697_haptic/aw8697.c`, `of_device_id aw8697_dt_match[] = { {
  .compatible = "awinic,aw8697_haptic" } }`. The DT's only compatible is
  `"awinic,aw8697_haptic,19081"`. `of_match` compares whole strings, so it does not match. The
  kernel tree's own `drivers/misc/aw8697_haptic` is an empty tree object, and there is no other
  aw8697 driver in either repo.
- The PMIC haptics driver hard-fails without a phandle the node does not have:
  `leds-qpnp-haptics.c:2051` `qpnp_haptics_parse_dt()` → `:2071` `of_parse_phandle(node,
  "qcom,pmic-revid", 0)` → `:2073` `pr_err("Missing qcom,pmic-revid property\n")` → `-EINVAL`. The
  flash LED node right next door *does* carry `qcom,pmic-revid = <0x1b>`; the haptics node does not.

So the honest statement is: **the register recipe below is what the enabled DT node asks for, not
what a working vendor driver was observed doing.** The recipe is still trustworthy, because the node
itself fully configures the LRA (vmax, play rate, waveform source, six effects) and the driver's
register writes are unambiguous.

## Light: flash LED, backlight, notification LEDs

| node | compatible | bus / address | status | what the DT asks for |
|---|---|---|---|---|
| `qcom,leds@d300` | `qcom,qpnp-flash-led-v2` | SPMI **slave 5**, `reg = <0xd300 0x100>` | `status="okay"` | `qcom,hdrm-auto-mode`, short-circuit / open-load / VPH-droop detection, thermal derating (currents `0xc8 0x1f4 0x3e8`), `qcom,isc-delay = <0xc0>`, `qcom,pmic-revid = <0x1b>`, irqs led-fault / ramp-down-done / ramp-up-done. `flash_0`/`flash_1` 1500 mA max (1000 default), `flash_2` 750 mA, all three `qcom,duration-ms = <0x500>`; `torch_*` 500 mA max (300 default); four `led_switch_*` nodes. |
| `qcom,wled@d800` | `qcom,pm8150l-spmi-wled` | SPMI **slave 5**, `reg = <0xd800 0x100 0xd900 0x100>` | `status="ok"` | `label = "backlight"`, children `qcom,wled-flash`, `qcom,wled-torch` (`qcom,wled-torch-timer = <0x4b0>`), `qcom,wled-switch`. Every panel in the overlay selects `qcom,mdss-dsi-bl-pmic-control-type = "bl_ctrl_wled"`. |
| `qcom,leds@d000` (slave 5) | `qcom,tri-led` | SPMI slave 5, 0xd000 | present | children `red`/`green`/`blue`, each `pwms = <0x1c n 0xf4240>` (1 kHz), `default-trigger = "timer"`. A notification RGB LED **in the tree** — probably not populated on this board, since the phone has no notification light. |
| `qcom,leds@d000` (slave 3) | `qcom,tri-led` | SPMI slave 3, 0xd000 | present | `hr_led1`, `hr_led2` on `qcom,pwms@b100` — the light source behind the in-display fingerprint reader. |
| `qcom,camera-flash@0…3` | gpio-regulator based | fragment@41, on `/soc` | present | `led_flash_rear`, `led_flash_rear_aux` symbols; these are GPIO enable switches for the camera flash boost, not the LPG itself. |
| `qcom,amoled` | `qcom,qpnp-amoled-regulator` | SPMI slave 5 | **disabled** | `oledb@e000`, `ab@de00`, `ibb@dc00` — the panel rails are RPMh-managed on this product, so these stay off. |

**The WLED slave is wrong in the port.** `drivers/display/hakios.wled.c` has `WLED_SLAVE 4` and
cites `android-merged.dts`, which contains no PMIC children at all. The overlay's symbol table puts
`pm8150l_wled` and `flash_led` under `qcom,pm8150l@5`. Before writing anything to 0xd300, settle it
with reads: `revid` at 0x0100 on slaves 4 and 5, and the WLED identity at 0xd800 on both. One SPMI
read each.

## Audio: the parts are on I2C, the sound is not

| node | compatible | bus / address | status | notes |
|---|---|---|---|---|
| `aqt1000-i2c-codec@0d` | `qcom,aqt1000` | I2C SE4, 0x0d | `status="ok"` (fragment@72) | `qcom,gpio-connect = <… gpio125 …>`, `cdc-vdd-rxtx/buck/vbat` supplies, `qcom,cdc-ext-clk-rate = <0x124f800>` (19.2 MHz), `qcom,cdc-mclk-clk-rate = <0x927c00>` (9.6 MHz), micbias1 1800 mV. |
| `max989xx@39` | `maxim,max98927L` | I2C SE4, 0x39 | `status="okay"` (fragment@77) | right speaker amp, reset gpio148, `mono_stereo_mode = <0x03>`, `range_mohms = <0x1388 0x1f40>` (20/50 mΩ sense). |
| `max989xx@3a` | `maxim,max98927R` | I2C SE4, 0x3A | `status="okay"` | left speaker amp, reset gpio37. |
| `fsa4480@43` | `qcom,fsa4480-i2c` | I2C SE4, 0x43 | base DT, enabled | the 4-pole headset switch (headphone / mic / button detection). |
| `tavil_codec`, `pahu_codec`, `wsa881x@…` ×4 | `qcom,wcd93xx`-family, `qcom,wsa881x` | **SLIMbus** `slim@171c0000` | fragments 66/67/71 | the on-board codec, the receiver/earpiece, and four smart speaker amps — all on SLIM, all behind ADSP. |
| `wcd9xxx-irq`, `msm_cdc_pinctrl@143`, `qocm,wcd-dsp-glink`, `wcd-dsp-mgr@1/2` | — | fragment@70, on `/soc` | present | the codec IRQ router and the ADSP audio-DSP manager. |

**Verdict: not reachable without the ADSP.** The I2C addresses are real and probeable, but
`aqt1000-core.c` and `max98927.c` both register an ASoC component whose data path is MI2S/LPAIF
(`qcom,msm-dai-q6-mi2s` in fragment@78) and whose clocking comes from the ADSP. Writing registers
turns the chip on; nothing will come out of it until LPASS/ADSP is up. The earpiece and the
receiver path additionally live in `wcd93xx`/`pahu` over SLIM, which has no AP-side path at all.

## WiFi and Bluetooth

| node | compatible | where | status | notes |
|---|---|---|---|---|
| `qcom,icnss@18800000` | `qcom,icnss` | `/soc`, MMIO `0x18800000`, PCIe RC1 | present, no `status` = enabled | `vdd-cx-mx`, `vdd-1.8-xo`, `vdd-1.3-rfa`, `vdd-3.3-ch0` supplies; SMP2P child for fatal-error/early-crash. |
| `qcom,cnss-qca6390@a0000000` | `qcom,cnss-qca6390` | PCIe, SMMU IOVA window | present | `wlan-en-gpio = <tlmm gpio169>`, MHI channel table (`mhi_chan@…`). |
| `qcom,mhi@0` under `pcie@1c08000/pcie_rc1` | — | fragment@59 | enabled | the MHI client for the WiFi chip. |
| `wcn3990` | `qcom,btfmslim_slave` | SLIMbus `slim@171c0000`, `elemental-addr = [00 01 20 02 17 02]` | base DT, present | the BT/FM side of the same combo chip, on SLIM. |
| `bt_wcn3990` | — | fragment@47, on `/vendor` | present | the BT enable/supply node. |
| SE12 `qcom,qup_uart@0xa90000` | `qcom,msm-uart-geni` | gpio85/86 | **disabled in base**, enabled by fragment@46 | this is the H:UART the BT chip talks to. |
| `qcom,wil6210` | `qcom,wil6210` | PCIe | **`status = "disabled"`** (base line 6218) | 60 GHz WiFi — not on this phone. |

**Verdict: WiFi is out, Bluetooth is a UART project.** WiFi on this chip is PCIe + MHI + firmware
download through the ADSP-side glue (`icnss.c` is 98 KB of PIL/SMP2P/QMI plumbing) — far beyond a
bare-metal driver. Bluetooth needs the opposite: one host UART (SE12, and `msm_geni_serial.c` is
fetched as the reference for the FIFO-mode programming), then the 3-wire/4-wire H:UART setup in
`hci_qca.c` + `btqca.c` (soc_type for WCN3990, the NVM/BD-address download, the 32-kHz sleep wire).
There is no `local-bd-address` property anywhere in either tree, so the address has to come from
OTP or a fixed value.

## NFC

| node | compatible | bus / address | status | gpios |
|---|---|---|---|---|
| `nq@28` | `qcom,nq-nci` | I2C SE9 (`i2c@a84000`), 0x28 | `status="ok"` via fragment@53 | `qcom,nq-irq` gpio47, `qcom,nq-ven` gpio36, `qcom,nq-firm` gpio48, `qcom,nq-clkreq` gpio113 |
| `nfc_chipset` | `oppo-nfc-chipset` | fragment@52, on `/soc` | present | `chipset-19081 = "SN100T"` — the chip is an SEC **SN100T**. |

**Verdict: reachable, low payoff.** NCI over I2C is a plain framed byte stream and the driver
(`drivers/nfc/nq-nci.c`) does the ven/clkreq handshake in open-coded GPIO calls. It needs an SE9
host first, and it buys a feature nobody misses on a desktop-ish port.

## Fingerprint

| node | compatible | status | notes |
|---|---|---|---|
| `goodix_fp` | `goodix,goodix_fp` | `status="ok"` (fragment@52, target `/soc`) | `goodix,gpio_irq` gpio118, `goodix,gpio_reset` gpio121, `goodix,goodix_pwr` gpio101, `ldo7-supply` with `ldo-config = <0x325aa0 0x325aa0 0x249f0>`, `power-num = <0x02>` and two `power_source_*` children. **No `reg`, no parent bus.** |
| `goodix_optical` | (child of a devinfo node) | present | `chip-name = "G_OPTICAL_G3"`, `vendor-chip = <0x0b>`, `oplus,fp-id = <0x01>` — an in-display **optical** sensor. |

**Verdict: out of scope.** The node has no bus address at all: the driver is a platform device that
drives power and GPIOs and hands the image work to the secure side (the light source is the
`hr_led1/hr_led2` PWM pair on PMIC slave 3). There is nothing to poke from bare metal.

## Sensors

The only sensor-ish nodes in either tree are `oplus_sensor` (`compatible = "oplus,sensor-devinfo"`,
fragment@52) with `gsensor@0` / `msensor@0` / `pickup_detect` / `lux_aod` children that carry
**orientation and calibration metadata only** (`sensor-name`, `sensor-direction`,
`soft-mag-parameter`) — no bus, no address, no compatible per device. The actual devices hang off
SLPI: `qcom,firmware-name = "slpi"` (base lines 3336, 3508), `qcom,slpi_qrtr` (4111),
`qcom,slpi_glink_ssr` (4123), and the SLPI remoteproc with its `slpi-pil` mailbox.

**Verdict: out of scope.** Every sensor on this phone is served by the SLPI image over QMI/GLINK.
There is no AP-side I2C accelerometer to drive.

## Charging, the pack, and VOOC

| node | compatible | bus / address | status | notes |
|---|---|---|---|---|
| `mp2650-charger@5c` | `oplus,mp2650-charger` | I2C SE1, 0x5C | `status="okay"` (fragment@64, 400 kHz) | `qcom,mps_otg_en-gpio` on the GPIO controller at phandle 0x08 (the same controller the volume keys use — not TLMM, which is phandle 0x37 in this tree). |
| `da9313-divider@68` | `oplus,da9313-divider` | I2C SE15, 0x68 | `status="okay"` | the VOOC voltage-divider/charge-pump monitor. |
| `bq27541-battery@55` | `oplus,bq27541-battery` + **`qcom,batt_bq28z610`** | I2C SE15, 0x55 | `status="okay"` | the overlay's copy declares the pack a **BQ28Z610**-class gauge and adds `qcom,sha1_key_index = <0x01>`. |
| `oplus_short-ic@58` | `oplus,oplus_short-ic` | I2C SE15, 0x58 | `status="okay"` | the cable/adapter short detector. |
| `stm8s_fastcg@26` | `oplus,stm8s-fastcg` | I2C SE15, 0x26 | `status="okay"` | the VOOC controller; `qcom,oplus_batt_4400mv`, `qcom,support_vooc_by_normal_charger_path`, VOOC temp/SOC thresholds, and four VOOC path GPIOs (switch gpio59, reset gpio10, clock gpio92, data gpio93). |
| `qcom,qpnp-smb5` | `qcom,qpnp-smb5` | SPMI slave 2 | present | the on-board charger + USB-PD PHY at `usb-pdphy@1700`. |
| `qcom,smb1390@10` / `smb1390_slave@18` / `smb1355@0c` | charge pumps | I2C SE4 | `@10` and `@0c` **both `status="ok"`**, `@18` disabled | two different charge-pump families both enabled → family tree, not board tree. |
| `qcom,fg` (PM8150B fuel gauge) | — | SPMI slave 2 | **disabled** (`qcom,fg` `disable = <1>`) | the pack has its own gauge, so the PMIC gauge is off. |
| `vooc_charge_strategy_65w` | — | fragment@65 | present | the 65 W VOOC strategy table. |

## Keys, hall and the flippers

| node | compatible | bus / address | status | notes |
|---|---|---|---|---|
| `gpio_keys` `vol_up` / `vol_down` | `gpio-keys` | fragment@52, on `/soc` | present | gpios 6 and 7 on the PMIC GPIO controller (phandle 0x08), active low, `linux,code` 0x73/0x72, wakeup. |
| `dhall0-m1120@0f` | `oppo,dhall-m1120` | I2C SE4, **0x0F** | base DT, enabled | `dhall,irq-gpio` = tlmm **gpio121**, `dhall,id = <0>`, pinctrl `hall0_interrupt_input`. |
| `dhall1-m1120@0c` | `oppo,dhall-m1120` | I2C SE4, **0x0C** | base DT, enabled | `dhall,irq-gpio` = tlmm **gpio120**, `dhall,id = <1>`. |
| `hall-irq-pos`, `hall-max-thrd = <0x05 0x05>`, `hall-data-sign` | — | a devinfo-ish node, base line 23011 | present | the fold/flip orientation policy: which hall is which and the threshold. |
| `oplus_tri_key` | — | `drivers/misc/tri_state_key/` | driver present | the three-state key (the Ace has none; fetched to read the hall interface). |

The M1120 is a plain digital hall: 1-byte registers over `i2c_transfer`, device ID register 0x09
must read **0x9C** (`M1120_VAL_DID`), status 0x10 (bit 0 = DRDY), field at 0x11/0x12, interrupt
setup at 0x01 (bit 7 enable, bits 1:0 sensitivity). The driver reports `ABS_X` and a `miscdevice`
at `/dev/m1120`.

## Explicitly not on this phone, or switched off

| thing | evidence |
|---|---|
| 60 GHz WiFi (`wil6210`) | base line 6218, `status = "disabled"` |
| PMIC AMOLED rails (`oledb`/`ab`/`ibb`) | overlay `qcom,amoled` under slave 5, `status = "disabled"` |
| PM8150B SMB5 slave charge pump | `smb1390_slave@18` `status = "disabled"` |
| PM8150B's own fuel gauge | `qcom,fg` `disable = <1>` |
| SE0, SE2, SE5, SE6, SE10, SE11 | base `status = "disabled"`, no overlay fragment enables them, no children |
| SLPI-side sensors | SLPI PIL + QMI/GLINK only; no AP-side bus |
| a notification LED in practice | the RGB tri-led exists in the tree with `default-trigger = "timer"`, but the product has none — probe it, don't trust it |

## Four things the port currently has wrong

**1. The gauge and VOOC parts are on SE15, not SE8.** The port probes `i2c@a80000` (SE8, gpio88/89)
and the bus is silent. That bus is real — the base DT has it `status = "okay"` at 100 kHz with
`bq27541-battery@55`, `oppo_short-ic@58` and `stm8s_fastcg@26` on it — but it is the **older** set:
its gauge child has no `qcom,batt_bq28z610`, and its short-ic child is `oppo,oppo_short-ic`. The
overlay's set on SE15 (`i2c@0xc94000`, gpio27/28, function `qup15`) is the shipped one: it enables a
bus the base left `disabled`, renames the short-ic to `oplus,oplus_short-ic`, marks the pack
`qcom,batt_bq28z610`, and adds the 65 W VOOC strategy. A bus the base leaves disabled is not used
until the overlay turns it on — and the overlay turns on SE15. **Retry the gauge at 0x55 on
gpio27/28.** The pinctrl function for those pins is already documented in
`vendor-src/INDEX.md` ("qup15 on gpio27/28").

**2. The WLED (and therefore the flash LED) is on PMIC slave 5, not slave 4.** Covered above;
settle it with two reads before writing.

**3. The AW8697 compatible does not match its driver.** Covered above. Practical consequence: do not
spend time on the AW8697 expecting the vendor driver to be the reference behaviour — on stock,
vibration is not served by that driver.

**4. The PMIC haptics node has no `qcom,pmic-revid`, so the vendor driver would refuse to probe.**
Covered above. Practical consequence: implement the register recipe directly; do not try to
"port" `leds-qpnp-haptics.c` as-is, since its first action is a DT lookup that fails here.

## What is reachable from bare metal

| hardware | reachable? | why / what it would take |
|---|---|---|
| **PM8150B HAPC (LRA)** | **yes** | SPMI is already working on this port (slave 0 PON, slave 4 GPIO). The HAPC is a register block: ~8 writes to vibrate, no IRQ needed for open-loop. |
| **PM8150L flash LED (torch)** | **yes** | Same SPMI path. ~6 writes for a torch. The only unknown is the slave number. |
| **Gauge / VOOC / charge pump** | **yes, once the bus is right** | SMBus byte read/write. Blocked on the SE15-vs-SE8 question, not on the device. |
| **Hall M1120 ×2** | **yes** | 1-byte registers, `i2c_transfer` write-then-read, ID 0x09 → 0x9C. SE4 is already enabled in the base tree. |
| **FSA4480 headset switch** | **yes** | 1-byte registers at 0x43 on the same bus. |
| **NFC SN100T** | yes | NCI over I2C + four GPIOs. Needs an SE9 host first. |
| **Bluetooth WCN3990** | yes, but needs a UART host | SE12 GENI UART, then `hci_qca`/`btqca` setup. No ADSP needed for classic BT. |
| AW8697 haptics | probeable, driver-less | ID reg 0x00 → 0x97 at 0x5A. No matching driver exists, so it is a from-scratch driver. |
| AQT1000 / MAX98927 | control only | I2C register access works; no audio without LPASS/ADSP and MI2S. |
| WiFi QCA6390 | no | PCIe + MHI + PIL + firmware. |
| WCD93xx / Pahu / WSA881x | no | SLIMbus over ADSP. |
| Goodix optical FP | no | No bus address; secure/TEE path. |
| SLPI sensors | no | QMI over GLINK to a remote processor image. |

## Sources fetched for this pass

All from `oppo/sm8150_s_12.1_oppo_reno10`. `kernel` = `android_kernel_oppo_sm8150`, `modules` =
`android_kernel_modules_and_devicetree_oppo_sm8150`.

### `vendor-src/haptic/`

| File | Upstream | What it answers |
|---|---|---|
| `leds-qpnp-haptics.c` | `kernel: drivers/leds/` | **The whole HAPC register map and the exact write order** for both LRA and ERM. |
| `qti-haptics.c` | `kernel: drivers/input/misc/` | The newer upstream-style driver for the same block — a second, cleaner reading of the same registers. |
| `leds-qpnp-haptics.txt`, `qti-haptics.txt` | `kernel: Documentation/devicetree/bindings/` | The property semantics: units, the vmax/rate encodings, which properties are required. |
| `aw8697.c`, `.h`, `_config.h`, `_reg.h` | `modules: vendor/oplus/kernel/vibrator/aw8697_haptic/` | The AW8697 register map (ID 0x00 → `AW8697_CHIPID 0x97`), the SMBus access style, and the compatible strings. |
| `qpnp-misc.h`, `qpnp-revid.h` | `kernel: include/linux/qpnp/` | The revid phandle contract the haptics driver insists on, and the PMIC subtype constants. |

### `vendor-src/leds/`

| File | Upstream | What it answers |
|---|---|---|
| `leds-qpnp-flash-v2.c` | `kernel: drivers/leds/` | **The flash/torch register map and the on-sequence**, plus the mA→register-code formula. |
| `leds-qpnp-flash-v2.txt` | `kernel: Documentation/devicetree/bindings/leds/` | What each DT property means, including `qcom,duration-ms` and the derating table. |
| `leds-qpnp-flash.h`, `leds-qpnp-flash-v2.h`, `leds.h` | `kernel: include/linux/` | The enums and the LED-class interface. |

### `vendor-src/input/`, `charger/`, `nfc/`, `bluetooth/`, `wifi/`, `audio/`

| File | Upstream | What it answers |
|---|---|---|
| `oppo_m1120.c`, `.h`, `hall_mxm1120.h/_up.c/_down.c` | `kernel: drivers/misc/oppo_motor/digital_hall_ic/`, `drivers/misc/tri_state_key/hall_ic/` | The hall register map, the 0x9C device ID, the interrupt setup, and the two-orientation policy. |
| `oplus_tri_key.c` | `kernel: drivers/misc/tri_state_key/` | The tri-state key driver that wraps the same hall parts. |
| `fsa4480-i2c.c` | `kernel: drivers/soc/qcom/` | The headset switch: register 0x04 switch settings, 0x05 control, 0x07 status, 0x1e reset, and the init table. |
| `oplus_mp2650.c`, `.h` | `kernel: drivers/power/oppo/charger_ic/` | The MP2650 register table. **Note: it is built on a MediaTek charging framework** (`upmu_common.h`, `mt-plat/charging.h`), so the register map is reusable and the driver logic is not. |
| `oplus_da9313.c`, `.h`, `oplus_short_ic.c`, `.h` | same directory | The divider and short-IC register maps; the short-IC's chip-id check accepts `reg00 & 0xF0 ∈ {0xD0,0xE0,0xF0}`. |
| `nq-nci.c`, `.h`, `nq-nci.txt`, `oppo_nfc.h` | `kernel: drivers/nfc/`, `drivers/nfc/oppo_nfc/` | NCI-over-I2C framing and the ven/ven/clkreq/firmware GPIO sequence for the SN100T. |
| `hci_qca.c`, `btqca.c`, `.h`, `btfm_slim_wcn3990.c`, `.h` | `kernel: drivers/bluetooth/` | The WCN3990 H:UART bring-up, the SoC-type table, the NVM/BD-address download, and the SLIM side of the chip. |
| `msm_geni_serial.c` | `kernel: drivers/tty/serial/` | The GENI UART this port needs for SE12 — the same FIFO-mode rules already worked out for I2C. |
| `icnss.c`, `icnss_private.h`, `icnss.txt`, `qcom,wcnss.txt` | `kernel: drivers/soc/qcom/`, `Documentation/devicetree/bindings/` | Why WiFi is not a bare-metal project: PIL, SMP2P, MHI, firmware download. |
| `aqt1000-core.c`, `-regmap.c`, `-registers.h`, `-api.h`, `-internal.h` | `modules: vendor/qcom/opensource/audio-kernel/asoc/codecs/aqt1000/` | The AQT1000 I2C register map and what it still needs from LPASS. |
| `max98927.c`, `.h` | `kernel: sound/soc/codecs/` | The smart-amp register map and its ASoC DAI requirements. |

## Bring-up recipes read out of the drivers

### PM8150B HAPC — open-loop LRA buzz (base 0xc000, slave 3)

All addresses are absolute SPMI function addresses (`base + offset`). `SEC_ACCESS` (0xd0) must be
written before anything under a bank. From `leds-qpnp-haptics.c` (macros 32–176, enable/play
664–793, mode/vmax 1058–1308, config 1810–1946):

| address | name | what goes in it |
|---|---|---|
| `0xc04c` | CFG1 | bit 0 = actuator type: **0 = LRA**, 1 = ERM |
| `0xc04d` | CFG2 | bit 0 = waveform: **0 = sine**, 1 = square |
| `0xc051` | VMAX | `value = round(mV / 116) << 1`, range 116–3596 mV; bit 6 = OVD. **3400 mV → 0x3A** |
| `0xc052` | ILIM | bit 0: 0 = 400 mA, 1 = 800 mA |
| `0xc053` | SC_DEB | short-circuit debounce |
| `0xc054`/`0xc055` | RATE_CFG1/2 | 16-bit little-endian `play_rate = play_rate_us / 5`. **6667 µs → 1333 = 0x535 → 0x35, 0x05** |
| `0xc05e` | WF_REPEAT | bits [6:4] = log2(wave_repeat), bits [1:0] = log2(sample_repeat) |
| `0xc060`… | WF_S1…S8 | 8-byte burst waveform; bit 7 sign, bit 6 OVD, bits [5:1] magnitude (max 0x3E) |
| `0xc04e` | SEL | bits [5:4] source: **0 = DIRECT**, 1 = BUFFER, 2 = AUDIO, 3 = PWM |
| `0xc046` | EN_CTL | **bit 7 = module enable** |
| `0xc070` | PLAY | bit 7 = play, bit 0 = pause |
| `0xc010` | INT_RT_STS | bit 0 = short circuit, bit 1 = play (read-only) |
| `0xc00a` | STATUS_1 | bit 1 busy, bit 3 short-circuit flag, bit 4 auto-res error |
| `0xc00b`/`0xc00c` | LRA_AUTO_RES_LO/HI | the tracked period, if auto-resonance is on |
| `0xc04f` | LRA_AUTO_RES | bits [6:4] mode, [3:2] high-z, [1:0] res-cal |
| `0xe3` (bank 0) | TEST2 | bit 7 = auto-resonance enable on non-PM660 parts |

**The shortest thing that vibrates:** `0xc04c=0x00`, `0xc04d=0x00`, `0xc051=0x3A`, `0xc052` and
`0xc053` left at their reset values, `0xc054=0x35`, `0xc055=0x05`, `0xc04e=0x00` (DIRECT),
`0xc046=0x80`, `0xc070=0x80`. Stop with `0xc070=0x00`. That is eight writes on a bus that already
works. The buffer-waveform variant is `0xc060..0xc067 = 0x7E` ×8, `0xc05e = 0x73`,
`0xc04e = 0x10`.

### PM8150L flash LED — torch (base 0xd300, slave 5 pending verification)

Per-LED registers are at `base + led_id`. From `leds-qpnp-flash-v2.c` (macros 36–130,
`get_current_reg_code` 357, `node_set` 1236, `switch_set` 1480–1626):

| address | name | what goes in it |
|---|---|---|
| `0x47` | IRES | 2 bits per LED = the current step index. `ires_µA = 5000 + (3 − idx) × 2500` → **idx 0 = 12.5 mA, 1 = 10 mA, 2 = 7.5 mA, 3 = 5 mA**; the max current per step is 1600 / 1280 / 960 / 640 mA |
| `0x43 + id` | TGR_CURRENT | `code = round(mA × 1000 / ires_µA) − 1`. **300 mA at the 12.5 mA step → 0x17**; the DT's 1500 mA flash maximum → 0x77 |
| `0x49 + id` | STROBE_CTRL | bit 2 = select software strobe (that is how a torch is driven) |
| `0x40 + id` | SAFETY_TMR | bit 7 = enable, `(ms / 10) − 1`; **0x13 = timer off**. The DT asks for `qcom,duration-ms = <0x500>` → 0xFF |
| `0x46` | MOD_CTRL | **bit 7 = module enable**, written once |
| `0x4c` | EN_LED_CTRL | set bit `id` → the LED comes on |
| `0x08`/`0x09` | LED_STATUS1/2 | fault / state, read-only |
| `0x10` | INT_RT_STS | fault / ramp-up / ramp-down, read-only |

**Torch-on order** (exactly the order `switch_set` performs): IRES → STROBE_CTRL → TGR_CURRENT →
SAFETY_TMR → MOD_CTRL=0x80 → EN_LED_CTRL bit. Off writes 0x13 to SAFETY_TMR and clears the enable
bit. Note the driver sets a PM8150L-specific workaround (`PM8150L_IRES_WA`, line 2311) that forces
the 5 mA step for anything at or below 20 mA — relevant only for a very dim torch.

### Cheap probes, one read each

| part | bus | read | expect |
|---|---|---|---|
| AW8697 | SE7, 0x5A | reg `0x00` | `0x97` |
| M1120 hall ×2 | SE4, 0x0F / 0x0C | reg `0x09` | `0x9C` |
| short-IC | SE15, 0x58 | reg `0x00` | `& 0xF0 ∈ {0xD0, 0xE0, 0xF0}` |
| PMIC identity | SPMI slaves 4 and 5 | function `0x0100` | the PM8150L revid — this is the read that settles the WLED/flash slave question |

## What to port next

*Status 2026-11-03: items 1 and 2 are written and answered on the phone, item 3 was re-probed
and answered in the negative, and item 5's WiFi has started anyway and passed M1 — see "On the
phone: R10" and "On the phone: R11" at the end of this file.*

**1. The PM8150B HAPC on slave 3.** It is the only missing feature a user can feel, and it is the
cheapest one in absolute terms: SPMI reads and writes already work on this port, the block needs no
IRQ for open-loop output, and the DT node hands over every number the recipe needs (3400 mV vmax,
6667 µs play rate, sine, direct-drive source) without a single interpretation. Eight writes to get
vibration, with the buffer-waveform path as a follow-up for the six `wf_*` effects the node defines.
The two caveats — the node's missing `qcom,pmic-revid` and the AW8697's unmatched compatible — are
arguments *for* doing it this way: there is no vendor driver on this phone to imitate, so the
register recipe is the specification.

**2. The PM8150L flash LED torch.** Same bus, same style, six writes, and the driver's on-sequence
is short and unambiguous. Do the two-read slave check first: the port's own WLED code says slave 4
and the overlay's symbol table says slave 5, and only one of them can be true. If the WLED works
today at slave 4, that is itself the finding worth recording, because the tree says there is no
WLED at slave 4.

**3. The cheapest win of all: re-probe the gauge on SE15.** This is not a new driver, it is a
different pair of pins. The base tree leaves `i2c@0xc94000` disabled and the overlay turns it on and
puts the gauge, the short-IC, the divider and the STM8S on it, while the base's SE8 copy is the
older, differently-named set that the bench found silent. An I2C scan at 0x55 / 0x26 / 0x58 / 0x68
on gpio27/28 either answers for the battery, the charger and VOOC at once, or it falsifies the
whole overlay-based argument in one transaction.

**4. Then the hall pair and the headset switch on SE4**, which the base tree already enables and
which need only 1-byte register reads and an ID check — a good warm-up for a real I2C host driver,
since SE4 is already live and shared with four other devices.

**5. Leave WiFi, audio, the fingerprint reader and the SLPI sensors alone.** Each of them is blocked
behind a subsystem (PCIe/MHI/PIL, LPASS+ADSP+SLIM, the secure world, QMI-over-GLINK) rather than
behind a driver, and none of them can be made to work by writing a register. Bluetooth is the one
exception worth scheduling: it is a UART and a firmware download, not a remote processor.

## On the phone: R10, 2026-11-03 — the motor and the torch answered at the register level

Items 1 and 2 above were written as drivers (`devices/pclm10/drivers/haptic/hakios.hapc.c`,
`devices/pclm10/drivers/camera/hakios.flash.c`, both through the PMIC arbiter exactly as the WLED
driver does) and run on the phone with `hapc=2 flash=2` (logs/092331-run.log). Both answered first
try; what the phone said, verbatim:

- **HAPC, slave 3, works.** `hapc: slave 3 0xc000 (channel 307): type 0x15 subtype 0x9` — the
  block is there and names itself. As found, Android's own configuration is still in the registers
  (`vmax 0x22 rate 0xe8 0x3`), which is itself proof the block is the live one. After the device
  tree's recipe (LRA, sine, 3400 mV = `vmax 0x3a`, 6667 µs = `rate 0x35 0x05`, direct source) and
  a play: `status1 0x82` — bit 1 busy set — and `auto_res 0x35 0x50`: the auto-resonance tracker
  moved from 0 to a real resonant reading, which only happens if the motor is actually oscillating.
  After stopping, busy clears and every register is back as found. The ear's confirmation is all
  that is missing.
- **Flash LED, slave 5, works.** `flash: slave 5 revid: 51 27 00 00` and
  `flash: slave 5 0xd300 (channel 333): type 0x18 subtype 0x4` — and this settles the slave-4-vs-5
  question raised in item 2: the arbiter's channel table and the overlay's symbol table agree, the
  flash module is on slave 5, and so is the WLED (channel 334, the WLED driver's own line in the
  same log). The torch on-sequence (12.5 mA step, software strobe, `tgr0 0x17` = 300 mA, safety
  timer off, module and channel enable) read back exactly as written, and
  `rt_sts 00 led_status 00 00` while lit: no short-circuit and no open-circuit fault bit, which is
  the register-level statement that an LED is wired to the pin and conducting. The eye's
  confirmation is all that is missing.
- **The WLED's answer to its own decisive test is no.** The same round read
  `wled: ... module enable 00` while the screen was lit: WLED's master switch is off on a lit
  retail panel, so this panel's backlight is not WLED (it is `bl_ctrl_dcs`, driven by the boot
  firmware). WLED stays useful as a diagnostic only.
- **Item 3, the gauge, is answered too, and the answer is no.** Both pin pairs, both orders, with
  the wake bursts before every probe: `nothing at 0x55 on gpio27/gpio28` and the same on
  gpio88/89, and full-bus scans find zero devices. The pack is verifiably on (SMB5 reads 4.2805 V,
  even taking charge), so ship-mode is excluded. notes/battery-bus.md §4 keeps exactly one
  hypothesis open.
- **A trap for the next bit-banged bus:** with the touch controller unpowered, its bus scanned
  "ACK" at every address 0x08–0x77 with `scl 0 sda 0` — an unpowered device clamps SDA low and
  every address looks acknowledged. Power the device before believing any scan.

## On the phone: R11, 2026-11-03 — the WLAN's rails are up, and its Q6 says it is down

Item 5 said to leave WiFi alone until a subsystem could be started. The subsystem's firmware
turned up in the EDL backup the same morning (notes/wifi.md §8 item 4), which changed the
calculus, so M1 of notes/wifi.md §5 was written as `devices/pclm10/drivers/wifi/hakios.slpi.c`
and run with `slpi=2`. What the phone said:

- **The command database names everything the notes asked it to name.** All six resources the
  device tree's own `qcom,resource-name` nodes promise are there at the addresses the family
  pattern predicts: `ldoa1` 0x40800, `ldoa7` 0x41f00, `ldoc2` 0x41d00, `ldoc11` 0x41a00,
  `lcx.lvl` 0x30030, `lmx.lvl` 0x30040 (logs/095951-run.log, and the same list in the hung
  round's dump, logs/095451-run.log). The table holds 122 id-shaped names; a walk by fours
  finds each name longer than four bytes a second time as its own tail, which is a fact about
  the walk, not about the table.
- **The four rails took their votes.** Twelve TCS commands — 752, 1800, 1304 and 3104 mV,
  enable, auto mode — every one completed in 1–5 polls. The boot after them is undisturbed:
  SpringBoard up, touch alive, the gauge thread reading 4.2736 V, the 10 fps idle heartbeat to
  the 200 s return, no panic (logs/095951-run.log). Whether the rails *moved* is still only
  inferable — a completed TCS proves RPMh answered for an address — and the first chance to
  know for sure is a running SLPI.
- **The Q6 block answered, and the answer is that it is down.** The first read of 0x5c00000
  did not fault and did not return: the load waits forever, the kernel's log stopped at the
  announced line, and the loader's watchdog said THE KERNEL HUNG, no sign of life for 50
  seconds (logs/095451-run.log). The SSC's interconnect does not answer while the subsystem is
  unpowered or unclocked. So the q6 dump moved behind `slpi=3`, and the first reading of those
  registers belongs to M2 — after the image is loaded through TrustZone and the AOP — and to
  nothing before it.

## On the phone: R12, 2026-11-03 — M2 through the image, and the GPU's firmware in hand

- **The SLPI image path is through.** With `slpi=3` the driver opens the QMP mailbox to the
  always-on processor (the doorbell is `0x17c0000c`; the first attempt used `0x017c000c`, one
  zero short, and every access to that unmapped address stalled the processor — that, not the
  APCS block, was three "hung" rounds), sends `off` then `on`, and the AOP takes both
  (logs/123127-run.log, logs/124242-run.log). TrustZone accepts `slpi.mdt`'s signature, takes
  `0x1400000` bytes at `0x98100000`, all twenty segments read back exactly, and
  `PAS_AUTH_AND_RESET` returns success. `lcx.lvl` and `lmx.lvl` are voted at the ssc node's own
  level `0x181` (ARC resources, one command each; rpmh-regulator.c's `hlvl`).
- **The subsystem itself is not running yet.** The q6 block at `0x5c00000` still does not answer
  after the authentication, so the SSC's power domain is not on. This SoC's GCC has no SSC clock
  or GDSC entry; the AOP is the only manager of that domain in the tree. The next step is on the
  AOP side: whether `on` must come after the image is in place, and what its state registers say.
- **The GPU's firmware is in hand**, read out of the vendor partition over EDL (a physical
  partition, LUN 4 sectors 119866–709689, 2304 MiB; debugfs read `/firmware` without mounting it):
  `a640_gmu.bin`, `a640_zap.mdt/.b00/.b01/.b02`, and `a630_sqe.fw` (this phone ships no
  `a640_sqe.fw`). They are in `S:\opencode\fw-gpu` and go into the payload image's `fw/`.
  `CONFIG_HAKIOS_ADRENO=y` now builds `drivers/gpu/hakios.adreno.c` (zero warnings); its firmware
  names are `a640_*`/`a630_sqe.fw`, its zap memory is a page it allocates itself (this phone's
  kgsl-hyp node has no memory-region, which the vendor handles with `dma_alloc_attrs`), and its
  GPUCC/GMU/SMMU bases are moved from the SC7280's `0x3d*` to this phone's
  `0x2c90000`/`0x2c6a000`/`0x2ca0000` (the offsets are the same; the GCC GPU clock offsets already
  matched). What still needs adapting before a GPU round: the SC7280-only `cx_misc@0x3d9e000` and
  GPU `rscc@0x3de0000` blocks, which this phone's device tree does not have (its GPU RPMh goes
  through the main RSC at `0x18220000`).
- **The GPU driver is walking on the phone now** (`logs/132125-run.log`, `logs/133304-run.log`):
  step 1 (GCC clocks), 2 (fuses), 3 (GPUCC registers — the block answers), 4 (**`CX is on`**: the
  GDSCR takes the write and reads back 0xf8222000), 5 (clocks: GMU 200 MHz, hub 150 MHz, SMMU
  vote), 8 (SMMU ID: 6 stream match registers, 5 contexts), 9 and 13 (SMMU context 1 programmed
  and its stream 5 matched) all pass. Step 14 (the GMU's firmware) then stopped on the **TCM
  layout**, now fixed: the a640's data TCM is at address **0x4000** (right after its 16 KiB code
  TCM at 0), not the a660's 0x10004000, and the a640's file is a **block list** whose first entry
  (address 0, size 0x30) is the CM3's reset code — the vendor's `load_gmu_fw()` sends only
  `adreno_is_a630()` and the a615 family to the raw-image path. The register windows themselves
  were already right: the a6xx offsets in `a6xx_reg.h` and in mainline's
  `registers/adreno/a6xx_gmu.xml` are **dword indices from the GPU's base 0x2c00000**, so
  `CM3_ITCM_START` 0x1b400 is the byte address 0x2c6d000 = the GMU block (0x2c6a000) + 0x3000,
  the driver's own constant; the same holds for `CM3_DTCM_START` 0x1c400 (+0x71000),
  `CM3_SYSRESET` 0x1f800 (+0x7e000), `FW_INIT_RESULT` 0x1f81c (+0x7e070), `HFI_CTRL_STATUS` 0x1f980
  (+0x7e600) and the rest. `logs/140457-run.log` then loaded the whole firmware — the reset code
  and the 0x38bc block through the ITCM window, the 0x3a58 block at 0x4000 through the DTCM — and
  step 15 (the GMU's start) got as far as **the GMU executing and asking for memory**: its first
  refused translation was 0x58000, one page past the file's last block (0x57000, 0x308 bytes).
  A runtime block at 0x58000..0x5ffff moved the fault to **0x5098**, a write inside the range the
  firmware calls its data TCM — so the a640's CM3 routes that range to the SMMU, not to the TCM,
  and the TCM/external split in the a640's memory map is the next thing to establish
  (`logs/141115-run.log`). That is where the GPU stands: the block lists, the clocks, the CX
  domain, the SMMU and the firmware load are all through, and the GMU is running far enough to
  fault on its own memory map. Then the subagent's disassembly of the firmware closed the loop on
  two more: the GMU's **RPMh channel is at 0x2c8d000** (`a6xx_reg.h`: `A6XX_GPU_RSCC_RSC_STATUS0_DRV0`
  0x23404 dwords from the GPU base), not the SC7280's 0x3de0000 the driver used, so its sleep/wake
  program had never been loaded where the firmware looks for it; and the a640's firmware signals its
  start with **`0xBABEFACE`** in FW_INIT_RESULT and its failure with `0xBADD FACE`, where the driver
  polled the a660's `0x100` mask (which would have taken a successful start for a failure). Both are
  fixed and the RSCC now answers at 0x2c8d000; the firmware still leaves FW_INIT_RESULT 0
  (`logs/173743-run.log`), so what remains is the reason its first instructions do not commit -- the
  next step there needs a trace of the vendor kernel starting the same firmware. Since then the driver carries the vendor's own interfaces: the a640
  GMU's block list and TCM windows, mainline's dummy-page and HFI layouts, the vendor's four-queue
  HFI table (`kgsl_hfi.h`: size 54 dwords, queue headers at +0xE0 and one 4 KiB queue each, types
  `10<<8|id` with the debug queue at priority 40, the fourth disabled, `rx_req` 1), the RPMh ucode
  (pdc + the rscc sleep/wake program) moved **before** the firmware load as
  `a6xx_gmu_fw_start()` does, and the `INIT`/`FW_VERSION` HFI messages mainline sends before its
  tables (`a6xx_hfi_start_v1`). With all of that the GMU still does not set `HFI_CTRL_STATUS` bit 0
  (`logs/154804-run.log`): the firmware is loaded and released, but it does not bring its message
  queues up. The next thing to establish is therefore not in the driver's structures but in the
  firmware's own start: whether its data block at 0x4000 belongs to the TCM load port (the vendor's
  `gmu_get_memdesc` says DTCM) or to external memory (the GMU's own write to 0x5098 went to the
  SMMU), and what its first HFI message (the one bit in `gmu2host_intr_info`) actually carries.
  The next round found a plain allocation bug in that table: the driver mapped the HFI block as
  **one 4 KiB page** (`block_new(..., PAGE_SIZE)`) although the table is 0xE0 bytes of header plus
  four 4 KiB queues -- 0x40E0 bytes. The firmware read its command queue at 0x60000800 (inside the
  one page) and then its message queue at 0x600010E0 was outside the mapping; the SMMU refused it
  and the firmware stopped before it could write FW_INIT_RESULT. The block is now allocated with
  `kmem_alloc_contig()` over 0x5000 bytes (`block_new_contig()`), so the host's write-combined
  alias and the GMU's SMMU window both cover the table and all four queues. The next `gpu=1`
  round is what confirms it.
  The CM3 probe then settled the boot question: the first instructions of the code TCM were
  replaced with a stub writing `0xdeadbeef` through `0x4000`, and the marker appeared -- in the
  **external memory at 0x4000**, not in the data-TCM window the file had been loaded into. So the
  a640's CM3 routes `0x4000` to external memory, and the old alias `0x4000..0x5ffff`, mapped after
  the firmware's own `0x40000`/`0x43000`/`0x57000` blocks, replaced the code, stack and data the
  CM3 fetched with zeroes. The alias is now the one block for `0x4000..0x5ffff`, the firmware's
  external blocks are copied into it, and the runtime gaps (`0x55000`, `0x58000..0x5ffff`) are in
  the same memory. With that the firmware executes and reaches its own memory map: the next
  `gpu=1` log's `FW_INIT_RESULT` is the verdict.
  The verdict came in `logs/222800-run.log`: **`cm3_fw_init_result = 0xbabeface`**, `hfi_ctrl_status`
  0x1, `THE GMU'S FIRMWARE RUNS`, and step 16 brought the message queues up (`hfi_version_info
  0x20000001`). The GPU had been held by a memory-map bug, not by a missing power rail.
  Two more of the reference phone's settings then had to go. The RPMh command database is at
  **0x85f20000** on this phone (the DT's `qcom,cmd-db@c3f000c` points to it), not the
  reference's 0x80860000; with that base the three rails resolve (`gfx.lvl` 0x30050, `cx.lvl`
  0x30000, `mx.lvl` 0x30010, with the level tables). And the GMU's HFI is **v2**
  (`hfi_version_info 0x20000001`): the legacy v1 opening (INIT message 0, FW_VERSION 1) is
  answered with error 0x6f, and the v2 sequence starts with the performance table. The v2 order
  is perf (4), bandwidth (3), core firmware start (14), **start (10)**, and only then the
  frequency/bandwidth vote (30); sending the vote before the start left the GMU silent
  (`logs/224155-run.log`). The core power request is the vendor's `a6xx_gmu_gfx_rail_on`, and it
  needs three things the driver had: `A6XX_GMU_BOOT_SLUMBER_OPTION` (0x1cbf8) set to
  `OOB_BOOT_OPTION` (0), `A6XX_GMU_GX_VOTE_IDX` (0x1cbf9) and `A6XX_GMU_MX_VOTE_IDX` (0x1cbfa)
  set to the primary and secondary indices of the performance table's vote, and the OOB request
  **oob_boot_slumber** (set `1<<26`, answer `1<<29`, from `a6xx_gmu_oob_set`: `30 - req*2`,
  `31 - req`) -- before the HFI start, as the vendor's `a6xx_gmu_fw_start` does.
- **Two build traps the first GPU rounds found.** The kernel build synced `devices/pclm10/**` and
  `userland/build.sh` only; `drivers/gpu/hakios.adreno.c` lives in the top-level `drivers/` tree and
  was never copied into the build tree, so the first `gpu=1` round ran the old driver and said
  `a660_gmu.bin` (the build script now syncs that file too, and greps its warnings). And the
  payload ramdisk `userland/root.apfs` — the image whose `/redwood/fw` the drivers' `image_file()`
  reads — was rebuilt in WSL but only `system.apfs` was copied back, so the first round with the
  new kernel said `a640_gmu.bin is not there`; both images must be copied back, not just the
  system one.

## On the phone: R13, 2026-11-04 — the camera's missing rail, and four sensors that name themselves

The camera's CCI bus had read both lines low through two rounds of bring-up
(logs/164351-run.log, logs/164459-run.log). The cause was not the PM8008 rails: the
overlay gives all five sensors cam_vio-supply from pm8150l_l1
(dtbo.bin.01.dts:8133), and that node is an **XOB** regulator
(ndroid-merged.dts:12773-12785, qcom,rpmh-xob-regulator). An XOB has no voltage
register at all -- vendor pmh-regulator.c's pmh_regulator_xob_ops is enable/disable
only, and RPMH_REGULATOR_REG_XOB_ENABLE is the VRM enable cell, one 4-byte word past the
resource address. The node also carries no qcom,init-enable, so its boot state is the
firmware's choice. Voting a millivolt value (the VRM shape the Bluetooth driver uses) did
nothing: the run said the vote completed, and the CCI lines still read 0/0
(logs/172100-run.log). With the enable vote instead (ldoc1 at 0x43400, word +1 = 1)
the bus came alive (logs/172606-run.log):

- rear main: answers at **0x1a** (the tree's 0x10 hint is wrong), chip ID **0x0586** at
  0x0016, EEPROM at 0x50 reading 

## On the phone: R13, 2026-11-04 — the camera's missing rail, and four sensors that name themselves

The camera's CCI bus had read both lines low through two rounds of bring-up
(`logs/164351-run.log`, `logs/164459-run.log`). The cause was not the PM8008 rails: the
overlay gives all five sensors `cam_vio-supply` from `pm8150l_l1`
(`dtbo.bin.01.dts:8133`), and that node is an **XOB** regulator
(`android-merged.dts:12773-12785`, `qcom,rpmh-xob-regulator`). An XOB has no voltage
register at all -- vendor `rpmh-regulator.c`'s `rpmh_regulator_xob_ops` is enable/disable
only, and `RPMH_REGULATOR_REG_XOB_ENABLE` is the VRM enable cell, one 4-byte word past the
resource address. The node also carries no `qcom,init-enable`, so its boot state is the
firmware's choice. Voting a millivolt value (the VRM shape the Bluetooth driver uses) did
nothing: the run said the vote completed, and the CCI lines still read 0/0
(`logs/172100-run.log`). With the enable vote instead (`ldoc1` at 0x43400, word +1 = 1)
the bus came alive (`logs/172606-run.log`):

- rear main: answers at **0x1a** (the tree's 0x10 hint is wrong), chip ID **0x0586** at
  0x0016, EEPROM at 0x50 reading `05 00 19 09 14 13 56 00`, actuator/OIS at 0x72/0x73.
- cam=4 then powered each of the five in turn (`logs/173026-run.log`): ultra-wide IMX319
  **0x0319** at 0x10 on CCI0 master1 (EEPROM 0x51), front IMX471 **0x0471** at 0x10 on
  CCI1 master1 (EEPROM 0x54), telephoto S5K3M5 **0x30d5** at 0x2d on CCI1 master0
  (EEPROM 0x50, another device at 0x58). Every one powered down cleanly.
- The mono sensor stayed silent on the shared CCI1 master0 bus, which answered only the
  tele's EEPROM: an EEPROM is powered from VIO alone, so it answers with the sensor's
  VANA/VDIG off.
- The bus rule from above still holds in reverse: with VIO off, both lines read 0 and
  nothing is on the bus; a scan before the rail is up is worthless.

Touch: `CONFIG_HAKIOS_S3706=y` had been set but `CONFIG_BOOTARGS` never carried `touch=1`,
so the driver's `__startup_func` returned before starting its thread on every daily boot.
The default now includes it (`devices/pclm10/configs/pclm10.config`), and every test
command line is generated from that defconfig by `tools/pclm10-run.sh` so the two cannot
drift again. `logs/171108-run.log` is the first enabled round: the chip, both function
bases and the event loop all came up, with no contact (0 downs) because nobody touched it.

## On the phone: R13 continued — the WCN3990 runs its patch

The TLV download needed two things the driver did not have. The first was chunking: the
SE's TX FIFO is 16 words, and `uart_send()` refused any packet longer than that, so the
first 243-byte segment failed outright (`logs/173846-run.log`). It now sends a packet as
several SE transmit commands, which the controller's H4 parser cannot tell apart from one
command.

The second was the ROM's own traffic. `crbtfw21.tlv`'s patch header says download mode 3
(skip vendor event and command complete), but that is an instruction to the *patched*
firmware; the ROM still answers every vendor command with a Command Complete. Over 729
segments those replies overflow the 128-byte RX FIFO and desynchronise the stream -- the
run after that ended with a stray `0xfd` where an H4 type should be
(`logs/175105-run.log`, `logs/180247-run.log`). Each unanswered segment now drains the ring,
keeping an unfinished frame instead of cutting it, and the UART read command is re-armed at
each session boundary.

With that, `logs/181027-run.log` is the verdict:

- `crbtfw21.tlv`: product 0x0a, rom build 0x0201 -- the controller's own product -- 177060
  bytes in 729 segments, the long final segment acknowledged.
- The version request after the download reports **soc 0x40010214, rom 0x0201, patch
  0x0002**. rom 0x0201 is rom_ver 0x21 by mainline's formula
  `((rom & 0xf00) >> 4) | (rom & 0xf)`, i.e. the file's own number; before the patch the
  ROM answered 0x1001 and the same formula would have asked for a `01` file that does not
  exist. The decoded name is only right *after* the patch is running.
- HCI Reset then matched again (`opcode 0xc03, status 0`), so the patched firmware is
  answering HCI, not just the vendor channel.

The NVM is the remaining piece: `crnv21.bin` (type 4 wrapper, type 2 inner TLV) goes in 19
segments, every one acknowledged, and then the controller goes silent -- one stray byte and
nothing after it, at 115200 (`logs/180247-run.log`). The most likely cause is the NVM's own
HCI tag setting the controller's UART divisor: mainline overwrites that tag's baud byte with
the host's chosen rate before sending, and this file's walk does not land on tag 17 at the
same offsets. Until that is resolved `wcnbt=4` sends the patch alone, which leaves a working
controller, and `wcnbt=3` keeps the full path for the next experiment.

## On the phone: R13 closed — the mono is a GalaxyCore, and the shared enable pin was the key

`cam=4` had left the fifth sensor silent on the same CCI1 master 0 bus where the telephoto
answers. The overlay gives the front (sensor 2) and the mono (sensor 4) the same enable pin,
gpio29 (`CAM_FRONT_EN` / `CAM_MONO_EN`), which is one level per module. `cam=6` powers the
mono twice, once with gpio29 high and once with it low (`logs/182119-run.log`,
`logs/182953-run.log`):

- High: only the telephoto's EEPROM answers, as before.
- Low: a new address **0x37** answers. Its registers at 0x0000, 0x0016 and the OV ID pair
  0x300a/0x300b read zero, but with an **8-bit register address** register 0xf0 answers
  **0x02d0** -- a GalaxyCore two-megapixel part, not the OmniVision the first table guessed.

So every one of the five is now named from its own answer: IMX586 0x0586 at 0x1a, IMX319
0x0319 at 0x10, IMX471 0x0471 at 0x10, S5K3M5 0x30d5 at 0x2d and GC02Mx 0x02d0 at 0x37.
The driver's table carries the mono's own address, register and ID, and gpio29's level is
recorded as the thing that selects the module.

The driver now drives gpio29 low for the mono by itself (the role decides the level), so a
single `cam=4` round names all five in turn and powers each down: IMX586, IMX319, IMX471,
S5K3M5 and the GalaxyCore GC02Mx (`logs/183829-run.log`).

### R13, the physical round (19:17-19:21)

One round with `flash=3 hapc=3 touch=1` left the torch lit and the motor running for its
whole 120 s, and the screen took real contacts: the log has DOWN/MOVE/UP with the glass's
own coordinates (1079x2399), e.g. `pass 731: DOWN at 31947, 44473 ... 526, 1628` and the
matching UP (`logs/191722-run.log`). Touch is therefore end-to-end: driver, report path and
backboardd.

The 45 s `flash=2 hapc=2` round that followed stopped the motor but *not* the LED: the boot
found `mod_ctrl 0x80 en_led 0x1` (the state `flash=3` left), and "put back as found" wrote
those back (`logs/191941-run.log`). That is correct for a diagnostic and wrong as a cleanup,
so the driver grew `flash=4`: channel enable, module enable, strobe, target and IRES all
zero, which the next round did (`logs/192127-run.log`, `as found mod_ctrl 0x80 ... forced
off mod_ctrl 00 en_led 00`).

### R13, the touch delay and the motor that is not on the PMIC

The 19:17 round took its first contact at **19:19:08**, in a boot that began at 19:17:35:
the touch thread spent that time proving the QUP engine does not start a command (three
attempts, one of them followed by a 30 s wait) before it fell back to the two pads. Every tap
in that window did nothing, which is what "touch does not work" looked like from outside.
The pads are now the default (`touch_bb`; `touch_bb=0` restores the engine-first order), and
the 19:29 round had the event loop up within seconds of the boot and took 14 contacts
(`logs/192945-run.log`). The events do reach iOS: backboardd logged
`[TouchEvents] contact 1 presence: touching` / `presence: none` for each contact
(`logs/191722-run.log`).

The vibration motor is not on the PM8150B's HAPC block, which is why `hapc=2/3` moved
nothing even though every register read back as written. This phone's overlay has an
**Awinic AW8697** on `qupv3_se7` at I2C 0x5a, reset on gpio116 and interrupt on gpio24
(`dtbo.bin.01.dts:6475-6483`, `fragment@51` target `qupv3_se7_i2c`), and the vendor driver
for it is `vendor-src/haptic/aw8697.c` with its RAM/RTP firmware (`aw8697_haptic_*.bin`) and
a CONT mode that needs no firmware. The next haptic step is that chip, on gpio98/99.

### R14, 2026-11-04 — the GPU's secure world, tested from both sides

Two rounds settled what the trust word is and is not:

- Writing `RBBM_SECVID_TRUST_CNTL` (word 0xF400, byte 0x2c3d000) to 0 **at the one point that
  is both after GX is up and before the CP has a ring** hangs the bus and kills the kernel
  (`logs/194110-run.log`: the "before the write: 0x1" line is the last thing printed). The
  device tree's `qcom,gpu-quirk-secvid-set-once` therefore does not mean "the host may write
  it once": the non-secure side cannot write it at all.
- After `PAS_AUTH_AND_RESET` returns success and TrustZone says it started the zap, the word
  still reads 0x1 after a 200 ms wait (`logs/194817-run.log`), and every stream table entry
  still refuses stream 0 while taking stream 5. So the zap is authenticated but never runs,
  or runs and fails, and the hypervisor keeps stream 0.

The vendor never writes TRUST_CNTL either (`vendor-src/firmware/adreno.c: _set_secvid()`):
when `device->mmu.secured` it writes `RBBM_SECVID_TSB_CNTL = 0`, then
`RBBM_SECVID_TSB_TRUSTED_BASE = mmu.secure_base` and `SIZE = mmu.secure_size` — a window in
the IOMMU's own VA space (`kgsl_iommu.c: secure_base = KGSL_IOMMU_SECURE_END - secure_size`),
not a physical address. Our driver writes 0 to both, and it does so after the zap.

What is left to try, in order: (1) program the trusted base and size the way the vendor does
before the zap runs; (2) run the zap before the GMU and the CP take the GPU (mainline loads it
from `a6xx_gpu_init`, with GX still the GMU's); (3) the hyp-side secure allocation behind
`qcom,hyp_secure_alloc`, which this port has no protocol for yet.

### R14, 2026-11-04 — touch that stops, and the motor that is an AW8697

"The touch screen works at first and then stops" was two separate faults, and neither was the
touch controller:

- **SpringBoard's idle timer.** About 30 s after the boot the display went `backlight-off`
  (`logs/191722-run.log: 19:18:01`), and backboardd then turned every touch into
  wake-on-touch: the first tap pressed the lock button and the gesture was swallowed. The
  panel's backlight is the boot firmware's, so the glass stayed lit while iOS believed it was
  asleep — a lit screen that ignores the finger. `libhakios.sb.dylib` now keeps
  `UIApplication.idleTimerDisabled` set (reasserted every 30 s), and `libhakios.dylib` no
  longer swallows the waking tap.
- **The chip's doze registers.** F01 control read `00 7f 08 14` and this port had never written
  it: the chip dozes after a while and then reports nothing, while the driver's loop still
  polls and counts zero failed reads. The driver now writes device control 0 (normal), doze
  interval 0 and doze timer 0 at the start and on every five-second heartbeat.

`logs/200228-run.log` is seven minutes of continuous use after both fixes: 319 downs, 2983
moves, 319 ups, **0 reads failed**, every five-second heartbeat counting to the end.

The motor: `hapc=2/3` moved nothing because the motor is not on the PM8150B's HAPC block. The
overlay has an **Awinic AW8697** on `qupv3_se7` (gpio98 data, gpio99 clock) at I2C 0x5a, reset
on gpio116, interrupt on gpio24 (`dtbo.bin.01.dts:6475-6483`). `drivers/haptic/hakios.aw8697.c`
moves the pads by hand, resets the chip, reads id **0x97**, and plays the vendor's CONT mode:
play mode 2, f0 170.0 Hz (f0 register 0x08d6), the 0815 drive level 60, delay 0x009a,
zero-cross threshold 0x0ff1, BEMF thresholds, GO. `SYSST 0x10` with no UVLO/OV/over-current,
and the motor buzzes (`logs/195855-run.log`); `aw8697=3` stops it (`logs/200040-run.log`).

### R14, 2026-11-04 — the zap's trusted window, and what is still missing

The boot firmware leaves `RBBM_SECVID_TSB_CNTL`, `TSB_TRUSTED_BASE` and `TSB_TRUSTED_SIZE` at
**0** (`logs/201957-run.log`), so TrustZone's zap shader has never had a trusted window to run
in. The vendor's `_set_secvid()` writes those three before the GPU starts, with kgsl_iommu.h's
values: `secure_size = SZ_256M`, `secure_base = KGSL_IOMMU_GLOBAL_MEM_BASE - secure_size`, i.e.
0xec000000 for the 64-bit base 0xfc000000 (0xe8000000 for the 32-bit 0xf8000000).

Setting them before `PAS_AUTH_AND_RESET` changes nothing: the auth still returns success, the
trust word still reads 0x1 after 200 ms, and no stream table entry takes stream 0
(`logs/202302-run.log`). So the zap is authenticated, given a trusted window, and still does
not run — what is missing is not a register a kernel can write. The remaining candidates are
inside the hypervisor: the secure context bank the zap would fetch through (kgsl's
`gfx3d_secure`, stream 2 in the device tree), and the `qcom,hyp_secure_alloc` allocation that
the hyp owns. This port has no protocol for either, and every non-secure write to
`RBBM_SECVID_TRUST_CNTL` itself hangs the GPU bus (logs/194110-run.log).

### R14, 2026-11-04 — the vendor's own answer, from oppo-source

OPPO's kernel for this phone is public: `oppo-source/Reno10X-RenoAce-10.0-kernel-source`,
`msm-4.14/` (the PCLM10 tree). Three facts from it settle the GPU's secure-mode question:

1. `drivers/gpu/msm/adreno.c: adreno_set_unsecured_mode()` — "GPU comes up in secured mode,
   make it unsecured by default": if a zap handle exists it sends the ring command, and
   **otherwise it simply writes `RBBM_SECVID_TRUST_CNTL = 0`**. So the write our port hangs on
   is legal on the vendor kernel; what makes it fatal here is the GPU's state, not the
   register.
2. `a6xx_rb_start()` calls that only **after the ring buffer and the microcode are started** —
   the CP runs first, in secured mode, and the trust bit is cleared afterwards. Our CP never
   starts, so this point is never reached.
3. `drivers/gpu/msm/kgsl_iommu.c: _init_secure_pt()` is what lets the CP run while secured: it
   creates a full **secure page table**, sets `DOMAIN_ATTR_SECURE_VMID = VMID_CP_PIXEL` on its
   domain, attaches it to the secure context (`KGSL_IOMMU_CONTEXT_SECURE`), and records the
   context bank. Without that secure VMID'd page table, the GPU's initial (secured) fetches
   have nothing to translate through — which is exactly what this port sees: every SMR entry
   refuses stream 0, and the CP's first fetch is an illegal instruction.

So the next real step is not another register: it is a second page table, mapped the way the
vendor maps it, installed in the GPU SMMU's secure context with VMID_CP_PIXEL, with the ring
and SQE mapped in it, before the CP is started. That is a driver-sized job, and it is the last
piece between this port and a running 3D core.

### R14, 2026-11-04 — the secure context installed, and what it did not change

Acting on the vendor source, `adreno_secure_pt=1` now gives context bank 2 the user context's
own page tables with `CBAR.VMID = 0x0a` (VMID_CP_PIXEL), the way `_init_secure_pt()` does, and
the bank takes it: `secure context 2 on: VMID 0xa, the same tables as context 0`
(`logs/202918-run.log`). The CP still dies on its first fetch: `CP illegal instruction`,
`ring read up to 0 of 9`, `SQE stat opcode 0`.

The same log says why the guess cannot be enough: the stream table entries for streams 0 and 1
**read back 0** whatever is written (`entry 0 reads 0 and 0`), so this processor is not looking
at the table the hardware uses. The device tree's `qcom,skip-init` says as much: the entries are
the boot loader's and the hypervisor's, and the register window this kernel sees does not let it
write them. Programming a context bank is allowed; choosing which stream reaches it is not.

That closes the reachable part of the GPU's secure world: the pieces a non-secure kernel can
write are written (GMU, GX, clocks, CP registers, context banks, trusted window), and the piece
it cannot write -- the stream table that decides where the CP's fetch is translated -- is the one
that fails. Without the hypervisor's protocol (the `qcom,skip-init`/`qcom,dynamic` contract the
vendor's arm-smmu and TZ share), the 3D core cannot be started from this kernel.

### R15, 2026-11-04 — the NVM's baud byte, and the controller that stays

The NVM never killed the controller: its own HCI tag asks for a different UART speed. With
OPPO's `msm-4.14` tree in hand, `drivers/bluetooth/btqca.c` shows two things this port had
wrong. The NVM tag header is **12 bytes**, not 4: the vendor walks it with
`sizeof(u16) + sizeof(u16) + 8 + tag_len`, and on that walk `crnv21.bin` has a clean chain
(tag 2 BD_ADDR, tag 17 HCI, tag 27 deep sleep, ...). And tag 17's third data byte is the UART
baud code, which the vendor overwrites with the host's rate before sending; this file carries
**0x11**, QCA_BAUDRATE_3200000. That is the whole mystery: the NVM applied, the controller
switched to 3.2 Mbaud, and the host stayed at 115200.

The driver now copies the NVM, writes 0 (QCA_BAUDRATE_115200) into that byte, and sends it with
every segment acknowledged. `logs/205327-run.log`: 46 tags walked, 1 changed, 4587 bytes in 19
segments, 19 acknowledged; the version request still answers (soc 0x40010214, rom 0x201,
patch 0x2) and HCI Reset matches (`opcode 0xc03, status 0`). The controller now runs both the
patch and this phone's NVM, which is the state a host stack needs before scanning.
> **Safety and evidence correction (latest):** The user reported severe heating and intermittent
> unresponsiveness. No calibrated device temperature or thermal shutdown behavior has been
> established; `CONFIG_HAKIOS_THERMAL` is built but not enabled by default, and the
> battery thermistor was logged as `00 00`. Stop hardware load tests until the device has cooled
> and thermal monitoring/protection can be assessed. The historical assertions below attributing
> the heat or intermittent touch failure definitively to software rendering, F01 doze or the
> SpringBoard idle timer are hypotheses, not proven diagnoses. Counters in one test do not establish
> behavior during a later failure. The user confirmed light when all LED channels were enabled,
> not which channels are connected: do not assume unused channels are harmless or leave all three
> enabled without a safety timer.

**GPU correction:** The earlier statements that CP stream 0 necessarily uses the secure page
table, that CBAR VMID 0x0a implements `DOMAIN_ATTR_SECURE_VMID`, and that the GPU is definitively
inaccessible are not established. The Qualcomm SMMU domain attribute includes secure page-table
memory ownership and authorization, distinct from a CBAR hardware VMID. The existing CBAR
probe is not an implementation of that contract. Stream routing and the CP fault remain open.
The PCLM10 merged tree does set both `qcom,skip-init` and `qcom,dynamic` on the GPU SMMU
(`android-merged.dts:22607-22615`). In the examined vendor SMMU code, skip-init suppresses
selected stream/context reset operations, not all SMMU initialization; dynamic domain support
is a separate feature. These flags do not reveal the effective CP stream routing by themselves.
The local `fw-gpu/a630_sqe.fw` is 32304 bytes, SHA-256
`a0e1b583f620fabe32729ce367959d1960638663244d7d0cfc21b9a5215a018b`.
Applying the vendor `adreno_get_ucode_version()` to its first four little-endian words gives
SQE version `0x016ee190`. Vendor `_load_firmware()` copies from `fw->data[4]`, consistent
with the port. This establishes file identity and loader offset, not firmware compatibility
or a working GPU fetch.
OPPO's public Reno Ace kernel `msm-4.14/drivers/gpu/msm/adreno-gpulist.h` lists
`a630_sqe.fw` for both A640 entries. This independently confirms the expected filename,
but not that the local blob matches the firmware originally shipped on this device.
The vendor SMMU driver uses a different valid-bit location when extended stream IDs are
advertised (`ID0_EXIDS`, bit 8). In `logs/232320-run.log` IDR0 is `0x4c017e06` (bit 8 clear),
and sCR0 is `0x270406` (EXIDENABLE bit 3 clear). Thus the vendor's non-EXIDS path, SMR bit 31
valid, is consistent with the observed configuration; the EXID interpretation is not a
supported explanation for stream 0 being refused in this run.
One concrete initialization difference remains for controlled investigation: the PCLM10
vendor `a6xx_rb_start()` programs `CP_RB_RPTR_ADDR` before RB control/base and SQE load,
whereas the current port explicitly leaves that register alone to follow a different upstream
driver. This is a source-order difference, not evidence that the missing write caused the
illegal-instruction interrupt; test it only with a mapped scratch address and appropriate
cleanup/temperature supervision.
In `logs/230014-run.log` the option `adreno_vendor_rptr=1` was present, but step 17 timed out
waiting for GMU HFI core-start message 14, so execution never reached the RPTR write in step
20. No conclusion about RPTR can be drawn. In this round the restored RCG read back `0x12`,
whereas a previous round reaching CP read `0x80000012`; causality is not established.
The 28-channel monitor's highest logged value in the short run was 52.5 C. The configured
60 C software return did not trigger and is not proven battery/case overheat protection.
After a loader reboot and re-upload, `logs/232320-run.log` reached step 20: with
`adreno_vendor_rptr=1` the low/high RPTR address registers read back `0x01200000/0`, but
the CP still signaled illegal instruction and the ring remained 0/9. This rules out the
missing RPTR address as a sufficient standalone fix, not all possible ring setup issues.
All 28 TSENS channels reported valid readings in that short run; the highest logged SoC
reading was 38.6 C. The 60 C stop did not fire and no battery/case temperature was measured.
In `logs/235113-run.log`, after clearing both halt requests, CX GBIF acknowledgement remained
`0x3` after up to 20 ms while the GX acknowledgement was zero; the CP still signaled illegal
instruction. This alone does not establish whether traffic was blocked. The same run reached
CP with RCG `0x12`, so the RCG state observed in `logs/230014-run.log` is not sufficient by
itself to explain that run's HFI timeout.
`logs/235616-run.log` tested `adreno_vendor_nc=1`: PCLM10 DT HBB=15, MAL=32,
UBWC=3 and no macrotiling-channel property yield vendor RB=0x14, TPL1=0x4,
SP=0x4 and UCHE=0x400000. The values were written but CP still reported illegal
instruction and RPTR remained 0/9. These layout values alone are not a sufficient fix.
Vendor `clk/clk-rcg2.c:33-40,76-86` identifies command bit 31 as `CMD_ROOT_OFF` and reads
it in `clk_rcg2_is_enabled`; bit 1 is `CMD_ROOT_EN`. Consequently writing bit 31 in the
existing restore probe does not itself establish a request to turn the root off. The
different readback is an observation of state, not an identified cause of the HFI timeout.

**120-second UI boot, `logs/222744-run.log`:** SpringBoard started and the kernel returned after
`loader_return_after=120` expired, not because the configured temperature threshold was reached.
The highest reported TSENS value was 54.8 C. The reference-phone sensor labels and incomplete
coverage described below still apply; neither chassis nor battery temperature was established.

The PCLM10 prebuilt DTB contains 28 TSENS sensor indices: block 0 indices 0–15 and block 1
indices 0–11. The common driver reads only block 0 indices 0–14, omitting block 0 index 15
(`gpuss-0`), and uses incorrect reference-phone names for block 1. Thus 54.8 C is only the
maximum among the 27 sampled values, not necessarily the SoC maximum. `tools/dt-thermal.py`
now recognizes this device's `qcom,tsens24xx` nodes. Device-specific mappings, calibration,
invalid-sensor handling, and the stop path remain unverified. Do not rely on the experimental
temperature threshold as validated protection.

**PCLM10 TSENS mapping test, `logs/224831-run.log`:** With `thermal_pclm10=1`, the shared
driver selected the 28 indices and names from the PCLM10 device tree; all 28 reported valid
status words in this 35-second test. The highest logged value was 47.5 C. This confirms
selection and plausible readings, not independent temperature calibration or battery/case
temperature, and the configured 60 C return path was not exercised.

**Synthetic stop-path test, `logs/224954-run.log`:** With `thermal_stop=1` solely to test
control flow without heating, three readings exceeded the deliberately artificial threshold
and the kernel returned to the loader before the 35-second timer. This demonstrates that
particular software path on this boot, not thermal calibration, hardware shutdown, loader
cooling, invalid-sensor handling, or protection of the battery and enclosure.
The subsequent rebuild reports loss of all valid readings rather than presenting a stale
maximum. The normal sensor path was re-run in `logs/225248-run.log` and all 28 channels
reported valid values; the all-invalid branch has not been induced on hardware.

**TSENS diagnostic, `logs/221325-run.log`:** A 35-second boot used `bootcpu=7`,
`cpufreq=0`, `thermal=1`, `thermal_log=5`, and an experimental `thermal_stop=70`.
The driver reported 26–27 valid status words and a maximum of 42.8 C. These are SoC
readings in that run, not measured battery or case temperatures. The threshold did not
trigger, so neither its return-to-loader path nor shutdown protection was tested. The
sensor-name table is from a different phone: for example PCLM10's merged device tree has
`gpuss-0` at tsens0 sensor 15 and `cwlan` at tsens1 sensor 1, not the names assigned by
the shared driver. Do not use its labels as PCLM10 hardware identification. Setting
`cpufreq=0` avoids requesting the top frequency in this run; it does not prove firmware
or a prior kernel has reset the current clock. The previous severe-heating cause remains
unknown and further load testing should be deferred pending usable thermal protection.
