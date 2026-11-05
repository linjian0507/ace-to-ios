# SLPI experiment index — current corrections and reproducible checks

This is a short index to `wifi.md` section 5, not a replacement for its evidence.
Log paths are relative to the repository root. No experiment below flashes storage.

| Experiment | Isolated change / observation | Evidence | Valid conclusion |
| --- | --- | --- | --- |
| M15 | Read distributor through SCM IO | `logs/080949-run.log` | Calls returned -2 (invalid argument); no distributor state obtained through SCM. Pinctrl's secure `spi_cfg` is a different register block. |
| M16 | Attach absolute IDs 170/172; read GIC identity and SCM availability | `logs/083450-run.log:534,539,842` | Mapping answers and IO read is offered. Zero receive counts were on the wrong IDs, not proof of absent SLPI notifications. |
| M17 | Correct DT SPI indices to absolute INTIDs 202/204 | `logs/093136-run.log:538,848,2296` | SLPI-to-AP works: GLINK 2, SMP2P 1, dispatcher and handler agree. Host FIFO remains unread (tail 0, head 48). |
| M18 | Write host restart-ack bit 1 after remote announcement, then ring | `logs/094426-run.log:844,2299` | Ack changed 0 to 0x2 and read back. It alone did not restore host FIFO consumption; IPCRTR remains closed. |
| M19 | Require explicit PAS_SHUTDOWN success before shared-memory/image rewrite | `logs/095720-run.log:709,898,2306` | Shutdown succeeded (transport 0, firmware 0); restart ack read back 0x2. Host tail still 0/head 48, IPCRTR closed. This shutdown addition alone did not restore consumption. |
| M20 | Observe watchdog INTID 526, SFR and slave-kernel against post-shutdown baseline | `logs/101014-run.log:562,914,2077,2335` | New watchdog dispatch/handler 0 to 1; old SFR unchanged, fatal/ready 0. Host FIFO still unread. A healthy-but-deaf DSP is not established. |
| M21 | Withhold host GLINK transmissions/cursor updates, retain SMP2P ack and observe | `logs/102311-run.log:1339-1340,2387,2394` | Watchdog 1/1 by 5053 ms while sent 0, host cursors 0/0, remote 0/8. Host GLINK messages are not necessary; missing-host timeout vs other startup fault unresolved. |

## Numbering and encoding

- DT `<0 0xaa 1>` is SPI-relative index 170, absolute GIC INTID **202**.
- DT `<0 0xac 1>` is SPI-relative index 172, absolute GIC INTID **204**.
- XNU `gic_v3_spi_attach` takes absolute IDs; it does **not** translate DT cells.
- Both corrected IDs use enable/pending word **6**, mask **0x1400**.
- Their ICFGR word is **12**, offset **0xc30**, shifts 20 and 24.
- GIC edge field is **2**, yielding **0x02200000**. DT trigger value 1 is not the register encoding.
- SMP2P header is 20 bytes; flags offset **16**. Remote restart-done is bit 0; host acknowledgement is bit 1.

## Run constraints

- Installed loader only, `bootcpu=7`, no `fastboot boot`, no partition flashing or flash-root.
- Short tests: `XL_SECS=150`, `cpufreq=0`, `thermal=1 thermal_pclm10=1 thermal_log=5 thermal_stop=60`.
- Cool at least ten minutes after completion; forty after long runs.
- One experimental variable at a time. Host readback is not proof of DSP visibility.
- AOP mailbox acceptance is not itself evidence that the DSP is stopped.
- Complete logs, not a sampled tail, determine peak temperatures: M16 **48.2 C**, M17 **47.5 C**, M18 **48.2 C**, M19 **47.8 C**, M20 **47.8 C**.

## Host-only checks (Python 3, C11 compiler `cc`; no device accesses)

```sh
python3 tools/pclm10-test-slpi-ack.py
python3 tools/pclm10-test-slpi-shutdown.py
python3 tools/pclm10-test-slpi-crash.py
python3 tools/pclm10-test-slpi-passive.py
python3 tools/pclm10-test-slpi-report.py
python3 tools/pclm10-slpi-report.py logs/094426-run.log
```

The ACK test compiles the actual driver function and actual wire header against fake SMEM (12 cases).
The shutdown test compiles the actual fail-closed gate (4 status combinations at each of M19/M20/M21, 12 cases).
The crash test compiles M20's actual read-only baseline/report functions against fake SMEM,
covering unchanged/changed/short/unreadable/absent/later-appearing SFR and watchdog/fatal/ready states.
The report test checks complete-log peaks, actual stop wording, partial/missing records, deduplication,
counter disagreement and source line numbers (10 tests).
The passive-observer test checks 0/1/45-real-second deadlines with no GLINK write/transmit API.
These checks validate logic and evidence extraction, not physical notification delivery.

## Evidence boundaries

**Radio experiments paused after M21.** User priority is implemented-feature delivery and normal touch/button usability. No M22 or additional phone run is scheduled.

No validated evidence yet identifies AOP sequencing, SLPI receive-enable, DSP cache state or a power
domain as the cause of the unread host FIFO. `qcom,ipc-scratch` is absent from the available PIL
driver and merged DT; no speculative routing writes are part of these experiments.
The vendor allocator's publication barrier is a separate cold-allocation issue; M19 does not change it.
An old SFR text string cannot date a crash without a before/after change tied to this instance.

Builds produce the kernel but still print known symbolset/export-check failures. A build wrapper exit
of zero is therefore not described as a clean all-target build. Runtime experiment markers and
ack/shutdown return lines are required before attributing phone results to a build.
