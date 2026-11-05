# Boot-arguments

Made by `tools/boot-args.py --markdown`; CI fails if it is out of date with the source (`tools/boot-args.py --check`).

Every argument that the kernel, the loader or user space of this port reads, and which file reads it. **419 arguments**: 348 read by the kernel, 14 by the loader, 67 by user space (one is read in more than one place).

This is a catalogue of what exists and who reads it, not a manual. What an argument means is in the head of the file that reads it. The stable switches have a symbol in the configuration (`tools/hakios-config.py help SYMBOL`, `docs/configs.md`); the rest are the knobs of bring-up (a driver's steps, pauses, traces, test modes) and can change with the driver. Any of them can be given in a configuration with `CONFIG_BOOTARGS="name=value ..."`, which checks that each name is in this list. What the arguments of the two Metal paths do: [Metal on the Adreno](reference/metal.md#boot-arguments).

Layers: **K** the kernel, **L** the loader, **U** user space (through `sysctl kern.bootargs`).

## GPU and display (116)

| Argument | Layer | Read by | Configuration | Set by |
|---|---|---|---|---|
| `adreno` | K | `hakios.adreno.c` | `CONFIG_ADRENO` |  |
| `adreno_aperture_cb` | K | `hakios.adreno.c` | `CONFIG_ADRENO_APERTURE_CB` |  |
| `adreno_blind` | K | `hakios.adreno.c` | `CONFIG_ADRENO_BLIND` |  |
| `adreno_clear_oob_probe` | K | `hakios.adreno.c` | `CONFIG_ADRENO_CLEAR_OOB_PROBE` |  |
| `adreno_core_cb` | K | `hakios.adreno.c` | `CONFIG_ADRENO_CORE_CB` |  |
| `adreno_cp_aperture` | K | `hakios.adreno.c` | `CONFIG_ADRENO_CP_APERTURE` |  |
| `adreno_cp_phys` | K | `hakios.adreno.c` | `CONFIG_ADRENO_CP_PHYS` |  |
| `adreno_delay_first` | K | `hakios.adreno.c` | `CONFIG_ADRENO_DELAY_FIRST` |  |
| `adreno_dirty` | K | `hakios.adreno.c` | `CONFIG_ADRENO_DIRTY` |  |
| `adreno_fast_oob_probe` | K | `hakios.adreno.c` | `CONFIG_ADRENO_FAST_OOB_PROBE` |  |
| `adreno_fine` | K | `hakios.adreno.c` | `CONFIG_ADRENO_FINE` |  |
| `adreno_gfx3d_on` | K | `hakios.adreno.c` | `CONFIG_ADRENO_GFX3D_ON` |  |
| `adreno_gfx3d_scan` | K | `hakios.adreno.c` | `CONFIG_ADRENO_GFX3D_SCAN` |  |
| `adreno_gfx3d_snapshot` | K | `hakios.adreno.c` | `CONFIG_ADRENO_GFX3D_SNAPSHOT` |  |
| `adreno_gfx3d_xo` | K | `hakios.adreno.c` | `CONFIG_ADRENO_GFX3D_XO` |  |
| `adreno_gmu_cb0` | K | `hakios.adreno.c` | `CONFIG_ADRENO_GMU_CB0` |  |
| `adreno_gpucc_baseline` | K | `hakios.adreno.c` | `CONFIG_ADRENO_GPUCC_BASELINE` |  |
| `adreno_gpucc_restore` | K | `hakios.adreno.c` | `CONFIG_ADRENO_GPUCC_RESTORE` |  |
| `adreno_gx_early` | K | `hakios.adreno.c` | `CONFIG_ADRENO_GX_EARLY` |  |
| `adreno_gx_gmu_clock_probe` | K | `hakios.adreno.c` | `CONFIG_ADRENO_GX_GMU_CLOCK_PROBE` |  |
| `adreno_halt_observe` | K | `hakios.adreno.c` | `CONFIG_ADRENO_HALT_OBSERVE` |  |
| `adreno_keep` | K | `hakios.adreno.c` | `CONFIG_ADRENO_KEEP` |  |
| `adreno_keep_sqe_base` | K | `hakios.adreno.c` | `CONFIG_ADRENO_KEEP_SQE_BASE` |  |
| `adreno_new_oob_only` | K | `hakios.adreno.c` | `CONFIG_ADRENO_NEW_OOB_ONLY` |  |
| `adreno_no_fal` | K | `hakios.adreno.c` | `CONFIG_ADRENO_NO_FAL` |  |
| `adreno_no_oob` | K | `hakios.adreno.c` | `CONFIG_ADRENO_NO_OOB` |  |
| `adreno_only` | K | `hakios.adreno.c` | `CONFIG_ADRENO_ONLY` |  |
| `adreno_pause` | K | `hakios.adreno.c` | `CONFIG_ADRENO_PAUSE` |  |
| `adreno_pll0_on` | K | `hakios.adreno.c` | `CONFIG_ADRENO_PLL0_ON` |  |
| `adreno_ptbl_probe` | K | `hakios.adreno.c` | `CONFIG_ADRENO_PTBL_PROBE` |  |
| `adreno_return` | K | `hakios.adreno.c` | `CONFIG_ADRENO_RETURN` |  |
| `adreno_s2cr0_cb` | K | `hakios.adreno.c` | `CONFIG_ADRENO_S2CR0_CB` |  |
| `adreno_secure_pt` | K | `hakios.adreno.c` | `CONFIG_ADRENO_SECURE_PT` |  |
| `adreno_secvid` | K | `hakios.adreno.c` | `CONFIG_ADRENO_SECVID` |  |
| `adreno_shared_cb` | K | `hakios.adreno.c` | `CONFIG_ADRENO_SHARED_CB` |  |
| `adreno_skip` | K | `hakios.adreno.c` | `CONFIG_ADRENO_SKIP` |  |
| `adreno_smr0_for_core` | K | `hakios.adreno.c` | `CONFIG_ADRENO_SMR0_FOR_CORE` |  |
| `adreno_smr_test` | K | `hakios.adreno.c` | `CONFIG_ADRENO_SMR_TEST` |  |
| `adreno_speed` | K | `hakios.adreno.c` | `CONFIG_ADRENO_SPEED` |  |
| `adreno_sptp_clock` | K | `hakios.adreno.c` | `CONFIG_ADRENO_SPTP_CLOCK` |  |
| `adreno_sptp_off` | K | `hakios.adreno.c` | `CONFIG_ADRENO_SPTP_OFF` |  |
| `adreno_sqe_priv` | K | `hakios.adreno.c` | `CONFIG_ADRENO_SQE_PRIV` |  |
| `adreno_start_first` | K | `hakios.adreno.c` | `CONFIG_ADRENO_START_FIRST` |  |
| `adreno_stream1` | K | `hakios.adreno.c` | `CONFIG_ADRENO_STREAM1` |  |
| `adreno_tsb` | K | `hakios.adreno.c` | `CONFIG_ADRENO_TSB` |  |
| `adreno_vendor_nc` | K | `hakios.adreno.c` | `CONFIG_ADRENO_VENDOR_NC` |  |
| `adreno_vendor_protect` | K | `hakios.adreno.c` | `CONFIG_ADRENO_VENDOR_PROTECT` |  |
| `adreno_vendor_rptr` | K | `hakios.adreno.c` | `CONFIG_ADRENO_VENDOR_RPTR` |  |
| `adreno_walk` | K | `hakios.adreno.c` | `CONFIG_ADRENO_WALK` |  |
| `adreno_watch` | K | `hakios.adreno.c` | `CONFIG_ADRENO_WATCH` |  |
| `display_probe` | K | `hakios.adreno.c` | `CONFIG_DISPLAY_PROBE` |  |
| `fb_beam` | K | `MiniIOMobileFramebuffer.cpp` | `CONFIG_FB_BEAM` |  |
| `fb_beat` | K | `MiniIOMobileFramebuffer.cpp` | `CONFIG_FB_BEAT` |  |
| `fb_ca_hz` | K | `MiniIOMobileFramebuffer.cpp` | `CONFIG_FB_CA_HZ` |  |
| `fb_diff` | K | `MiniIOMobileFramebuffer.cpp` | `CONFIG_FB_DIFF` |  |
| `fb_filter` | K | `MiniIOMobileFramebuffer.cpp` | `CONFIG_FB_FILTER` |  |
| `fb_hz` | K | `MiniIOMobileFramebuffer.cpp` | `CONFIG_FB_HZ` |  |
| `fb_measure` | K | `MiniIOMobileFramebuffer.cpp` | `CONFIG_FB_MEASURE` |  |
| `fb_pri` | K | `MiniIOMobileFramebuffer.cpp` | `CONFIG_FB_PRI` |  |
| `fb_queue` | K | `MiniIOMobileFramebuffer.cpp` | `CONFIG_FB_QUEUE` |  |
| `fb_threads` | K | `MiniIOMobileFramebuffer.cpp` | `CONFIG_FB_THREADS` |  |
| `fbheight` | K | `MiniIOMobileFramebuffer.cpp` | `CONFIG_FBHEIGHT` |  |
| `fbscale` | K | `MiniIOMobileFramebuffer.cpp` | `CONFIG_FBSCALE` |  |
| `fbsubtype` | K | `MiniIOMobileFramebuffer.cpp` | `CONFIG_FBSUBTYPE` |  |
| `fbtrace` | K | `MiniIOMobileFramebuffer.cpp` | `CONFIG_FBTRACE` |  |
| `fbwidth` | K | `MiniIOMobileFramebuffer.cpp` | `CONFIG_FBWIDTH` |  |
| `glblit` | U | `eagl.c` | `CONFIG_GLBLIT` |  |
| `glcensus` | U | `eagl.c` | `CONFIG_GLCENSUS` |  |
| `glcopy` | U | `eagl.c` | `CONFIG_GLCOPY` |  |
| `glenv` | U | `eagl.c` | `CONFIG_GLENV` |  |
| `glfetch` | U | `eagl.c` | `CONFIG_GLFETCH` |  |
| `glframes` | U | `eagl.c` | `CONFIG_GLFRAMES` |  |
| `glgroup` | U | `eagl.c` | `CONFIG_GLGROUP` |  |
| `glnumbers` | U | `eagl.c` | `CONFIG_GLNUMBERS` |  |
| `glplain` | U | `eagl.c` | `CONFIG_GLPLAIN` |  |
| `glprecision` | U | `eagl.c` | `CONFIG_GLPRECISION` |  |
| `glprof` | U | `eagl.c` | `CONFIG_GLPROF` |  |
| `glshaders` | U | `eagl.c` | `CONFIG_GLSHADERS` |  |
| `glshow` | U | `eagl.c` | `CONFIG_GLSHOW` |  |
| `glshow_part` | U | `eagl.c` | `CONFIG_GLSHOW_PART` |  |
| `glshow_zoom` | U | `eagl.c` | `CONFIG_GLSHOW_ZOOM` |  |
| `glskip` | U | `eagl.c` | `CONFIG_GLSKIP` |  |
| `glslow` | U | `eagl.c` | `CONFIG_GLSLOW` |  |
| `glstill` | U | `eagl.c` | `CONFIG_GLSTILL` |  |
| `glsync` | U | `eagl.c` | `CONFIG_GLSYNC` |  |
| `gltrace` | U | `eagl.c` | `CONFIG_GLTRACE` |  |
| `gltrace_at` | U | `eagl.c` | `CONFIG_GLTRACE_AT` |  |
| `gltrace_corners` | U | `eagl.c` | `CONFIG_GLTRACE_CORNERS` |  |
| `gltrace_draws` | U | `eagl.c` | `CONFIG_GLTRACE_DRAWS` |  |
| `gltrace_frames` | U | `eagl.c` | `CONFIG_GLTRACE_FRAMES` |  |
| `gltrace_function` | U | `eagl.c` | `CONFIG_GLTRACE_FUNCTION` |  |
| `gltrace_lit` | U | `eagl.c` | `CONFIG_GLTRACE_LIT` |  |
| `gltrace_look` | U | `eagl.c` | `CONFIG_GLTRACE_LOOK` |  |
| `gltrace_place` | U | `eagl.c` | `CONFIG_GLTRACE_PLACE` |  |
| `gltrace_shaders` | U | `eagl.c` | `CONFIG_GLTRACE_SHADERS` |  |
| `gltrace_uniforms` | U | `eagl.c` | `CONFIG_GLTRACE_UNIFORMS` |  |
| `glwait` | U | `eagl.c` | `CONFIG_GLWAIT` |  |
| `gpu` | K | `hakios.adreno.c` | `CONFIG_GPU` |  |
| `gpu_cached` | K | `hakios.gpu.cpp` | `CONFIG_GPU_CACHED` |  |
| `gpu_exact` | K | `hakios.adreno.c` | `CONFIG_GPU_EXACT` |  |
| `gpu_keep` | K | `hakios.gpu.cpp` | `CONFIG_GPU_KEEP` |  |
| `gpu_log` | K | `hakios.adreno.c` | `CONFIG_GPU_LOG` |  |
| `gpu_probe` | K | `hakios.adreno.c` | `CONFIG_GPU_PROBE` |  |
| `hakios_kver` | K | `MiniIOMobileFramebuffer.cpp`, `hakios.version.c` |  | the loader (from the payload's kernel.ver) |
| `metal` | KU | `HakiosMetal.m`, `MiniIOMobileFramebuffer.cpp`, `inject_sb.c` | `CONFIG_METAL` |  |
| `rec_mb` | K | `MiniIOMobileFramebuffer.cpp` | `CONFIG_REC_MB` |  |
| `rec_ring` | K | `MiniIOMobileFramebuffer.cpp` | `CONFIG_REC_RING` |  |
| `rec_secs` | K | `MiniIOMobileFramebuffer.cpp` | `CONFIG_REC_SECS` |  |
| `rec_start` | K | `MiniIOMobileFramebuffer.cpp` | `CONFIG_REC_START` |  |
| `rec_step` | K | `MiniIOMobileFramebuffer.cpp` | `CONFIG_REC_STEP` |  |
| `surf_clear` | K | `MiniIOSurface.cpp` | `CONFIG_SURF_CLEAR` |  |
| `surf_wrap` | K | `MiniIOSurface.cpp` | `CONFIG_SURF_WRAP` |  |
| `surftrace` | K | `MiniIOSurface.cpp` | `CONFIG_SURFTRACE` |  |
| `surftrace_of` | K | `MiniIOSurface.cpp` | `CONFIG_SURFTRACE_OF` |  |
| `touch_lat` | KU | `HakiosMetalCA.m`, `MiniIOMobileFramebuffer.cpp`, `eagl.c`, `hakios.goodix.c`, `hakios.gpu.cpp`, `hakios.input.c`, `inject.c`, `inject_sb.c` | `CONFIG_TOUCH_LAT` |  |
| `vblur` | U | `eagl.c` | `CONFIG_VBLUR` |  |

## Security stand-ins (1)

| Argument | Layer | Read by | Configuration | Set by |
|---|---|---|---|---|
| `privs` | K | `kern_minimac.c` | `CONFIG_PRIVS` |  |

## Wi-Fi (31)

| Argument | Layer | Read by | Configuration | Set by |
|---|---|---|---|---|
| `wifi` | K | `hakios.wifi.c` | `CONFIG_WIFI` |  |
| `wifi_again` | K | `hakios.wifi.c` | `CONFIG_WIFI_AGAIN` |  |
| `wifi_ask` | K | `hakios.wifi.c` | `CONFIG_WIFI_ASK` |  |
| `wifi_ba` | K | `lx_80211.c` | `CONFIG_WIFI_BA` | implied by wifi=1 |
| `wifi_cal` | K | `hakios.wlfw.c` | `CONFIG_WIFI_CAL` |  |
| `wifi_dirty` | K | `hakios.wifi.c` | `CONFIG_WIFI_DIRTY` |  |
| `wifi_file` | K | `hakios.wifi.c` | `CONFIG_WIFI_FILE` |  |
| `wifi_fine` | K | `hakios.wifi.c` | `CONFIG_WIFI_FINE` |  |
| `wifi_hold` | K | `lx_bus.c` | `CONFIG_WIFI_HOLD` |  |
| `wifi_irq` | K | `lx_bus.c` | `CONFIG_WIFI_IRQ` | implied by wifi=1 |
| `wifi_join` | K | `lx_bus.c` | `CONFIG_WIFI_JOIN` |  |
| `wifi_keep` | K | `hakios.wifi.c` | `CONFIG_WIFI_KEEP` | implied by wifi=1 |
| `wifi_listen` | K | `hakios.wifi.c` | `CONFIG_WIFI_LISTEN` |  |
| `wifi_loud` | K | `hakios.wifi.c` | `CONFIG_WIFI_LOUD` |  |
| `wifi_lx` | K | `hakios.wifi.c`, `lx_bus.c` | `CONFIG_WIFI_LX` | implied by wifi=1 (as 2) |
| `wifi_lx_step` | K | `lx_base.c` | `CONFIG_WIFI_LX_STEP` |  |
| `wifi_mac` | K | `hakios.wifi.c` | `CONFIG_WIFI_MAC` |  |
| `wifi_mission` | K | `hakios.wlfw.c` | `CONFIG_WIFI_MISSION` |  |
| `wifi_new` | K | `hakios.wifi.c` | `CONFIG_WIFI_NEW` |  |
| `wifi_pause` | K | `hakios.wifi.c` | `CONFIG_WIFI_PAUSE` |  |
| `wifi_quiet` | K | `lx_bus.c` | `CONFIG_WIFI_QUIET` |  |
| `wifi_radio_read` | K | `hakios.wifi.c` | `CONFIG_WIFI_RADIO_READ` |  |
| `wifi_ready` | K | `hakios.wlfw.c` | `CONFIG_WIFI_READY` |  |
| `wifi_return` | K | `hakios.wifi.c` | `CONFIG_WIFI_RETURN` |  |
| `wifi_scan` | K | `lx_bus.c` | `CONFIG_WIFI_SCAN` |  |
| `wifi_smmu` | K | `hakios.wifi.c` | `CONFIG_WIFI_SMMU` | implied by wifi=1 (as 2) |
| `wifi_station` | K | `lx_bus.c` | `CONFIG_WIFI_STATION` | implied by wifi=1 |
| `wifi_step` | K | `hakios.wifi.c` | `CONFIG_WIFI_STEP` |  |
| `wifi_wait` | K | `hakios.wifi.c` | `CONFIG_WIFI_WAIT` |  |
| `wifi_watch` | K | `hakios.wifi.c` | `CONFIG_WIFI_WATCH` |  |
| `wifi_wide` | K | `lx_80211.c` | `CONFIG_WIFI_WIDE` | implied by wifi=1 |

## Cellular (53)

| Argument | Layer | Read by | Configuration | Set by |
|---|---|---|---|---|
| `modem` | K | `hakios.mpss.c` | `CONFIG_MODEM` |  |
| `modem_again` | K | `hakios.mpss.c` | `CONFIG_MODEM_AGAIN` |  |
| `modem_apn` | K | `hakios.mpss.c` | `CONFIG_MODEM_APN` |  |
| `modem_buffer` | K | `hakios.mpss.c` | `CONFIG_MODEM_BUFFER` |  |
| `modem_data_nobind` | K | `hakios.mpss.c` | `CONFIG_MODEM_DATA_NOBIND` |  |
| `modem_data_off` | K | `hakios.mpss.c` | `CONFIG_MODEM_DATA_OFF` |  |
| `modem_data_wait` | K | `hakios.mpss.c` | `CONFIG_MODEM_DATA_WAIT` |  |
| `modem_dpm_off` | K | `hakios.mpss.c` | `CONFIG_MODEM_DPM_OFF` |  |
| `modem_ipa` | K | `hakios.mpss.c` | `CONFIG_MODEM_IPA` |  |
| `modem_ipa_bus_off` | K | `hakios.mpss.c` | `CONFIG_MODEM_IPA_BUS_OFF` |  |
| `modem_ipa_data_off` | K | `hakios.ipa.c` | `CONFIG_MODEM_IPA_DATA_OFF` |  |
| `modem_ipa_data_trace` | K | `hakios.cell.c`, `hakios.ipa.c` | `CONFIG_MODEM_IPA_DATA_TRACE` |  |
| `modem_ipa_fw` | K | `hakios.mpss.c` | `CONFIG_MODEM_IPA_FW` |  |
| `modem_ipa_fw_at` | K | `hakios.mpss.c` | `CONFIG_MODEM_IPA_FW_AT` |  |
| `modem_ipa_gsi_enable` | K | `hakios.mpss.c` | `CONFIG_MODEM_IPA_GSI_ENABLE` |  |
| `modem_ipa_item` | K | `hakios.mpss.c` | `CONFIG_MODEM_IPA_ITEM` |  |
| `modem_ipa_no_probe` | K | `hakios.mpss.c` | `CONFIG_MODEM_IPA_NO_PROBE` |  |
| `modem_ipa_quiet` | K | `hakios.mpss.c` | `CONFIG_MODEM_IPA_QUIET` |  |
| `modem_ipa_raw` | K | `hakios.mpss.c` | `CONFIG_MODEM_IPA_RAW` |  |
| `modem_ipa_routes` | K | `hakios.mpss.c` | `CONFIG_MODEM_IPA_ROUTES` |  |
| `modem_ipa_skip_uc` | K | `hakios.mpss.c` | `CONFIG_MODEM_IPA_SKIP_UC` |  |
| `modem_ipa_smmu_off` | K | `hakios.mpss.c` | `CONFIG_MODEM_IPA_SMMU_OFF` |  |
| `modem_keep` | K | `hakios.mpss.c` | `CONFIG_MODEM_KEEP` |  |
| `modem_listen` | K | `hakios.mpss.c` | `CONFIG_MODEM_LISTEN` |  |
| `modem_nas_wait` | K | `hakios.mpss.c` | `CONFIG_MODEM_NAS_WAIT` |  |
| `modem_new` | K | `hakios.mpss.c` | `CONFIG_MODEM_NEW` |  |
| `modem_no_ipa_qmi` | K | `hakios.mpss.c` | `CONFIG_MODEM_NO_IPA_QMI` |  |
| `modem_no_nav` | K | `hakios.mpss.c` | `CONFIG_MODEM_NO_NAV` |  |
| `modem_no_tftp` | K | `hakios.mpss.c` | `CONFIG_MODEM_NO_TFTP` |  |
| `modem_online` | K | `hakios.mpss.c` | `CONFIG_MODEM_ONLINE` |  |
| `modem_pause` | K | `hakios.mpss.c` | `CONFIG_MODEM_PAUSE` |  |
| `modem_return` | K | `hakios.mpss.c` | `CONFIG_MODEM_RETURN` |  |
| `modem_rmtfs_delay` | K | `hakios.mpss.c` | `CONFIG_MODEM_RMTFS_DELAY` |  |
| `modem_rmtfs_rw_fake` | K | `hakios.mpss.c` | `CONFIG_MODEM_RMTFS_RW_FAKE` |  |
| `modem_rmtfs_rw_off` | K | `hakios.mpss.c` | `CONFIG_MODEM_RMTFS_RW_OFF` |  |
| `modem_share_off` | K | `hakios.mpss.c` | `CONFIG_MODEM_SHARE_OFF` |  |
| `modem_share_test` | K | `hakios.mpss.c` | `CONFIG_MODEM_SHARE_TEST` |  |
| `modem_sim_probe_off` | K | `hakios.mpss.c` | `CONFIG_MODEM_SIM_PROBE_OFF` |  |
| `modem_sim_switch_off` | K | `hakios.mpss.c` | `CONFIG_MODEM_SIM_SWITCH_OFF` |  |
| `modem_step` | K | `hakios.mpss.c` | `CONFIG_MODEM_STEP` |  |
| `modem_wait` | K | `hakios.mpss.c` | `CONFIG_MODEM_WAIT` |  |
| `modem_wda_off` | K | `hakios.mpss.c` | `CONFIG_MODEM_WDA_OFF` |  |
| `soc_cmd_db` | K | `hakios.mpss.c` | `CONFIG_SOC_CMD_DB` |  |
| `soc_gsi_base` | K | `hakios.ipa.c` | `CONFIG_SOC_GSI_BASE` |  |
| `soc_imem` | K | `hakios.mpss.c` | `CONFIG_SOC_IMEM` |  |
| `soc_ipa_base` | K | `hakios.ipa.c` | `CONFIG_SOC_IPA_BASE` |  |
| `soc_ipa_fw` | K | `hakios.mpss.c` | `CONFIG_SOC_IPA_FW` |  |
| `soc_ipa_stream` | K | `hakios.mpss.c` | `CONFIG_SOC_IPA_STREAM` |  |
| `soc_ipa_stream_uc` | K | `hakios.mpss.c` | `CONFIG_SOC_IPA_STREAM_UC` |  |
| `soc_mpss_mem` | K | `hakios.mpss.c` | `CONFIG_SOC_MPSS_MEM` |  |
| `soc_pas_ipa` | K | `hakios.mpss.c` | `CONFIG_SOC_PAS_IPA` |  |
| `soc_pas_mpss` | K | `hakios.mpss.c` | `CONFIG_SOC_PAS_MPSS` |  |
| `soc_smem` | K | `hakios.adsp.c`, `hakios.mpss.c`, `hakios.slpi.c`, `hakios.socinfo.c` | `CONFIG_SOC_SMEM` |  |

## Audio and battery (35)

| Argument | Layer | Read by | Configuration | Set by |
|---|---|---|---|---|
| `adsp` | K | `hakios.adsp.c` | `CONFIG_ADSP` |  |
| `adsp_again` | K | `hakios.adsp.c` | `CONFIG_ADSP_AGAIN` |  |
| `adsp_apr_wait` | K | `hakios.adsp.c` | `CONFIG_ADSP_APR_WAIT` |  |
| `adsp_keep` | K | `hakios.adsp.c` | `CONFIG_ADSP_KEEP` |  |
| `adsp_listen` | K | `hakios.adsp.c` | `CONFIG_ADSP_LISTEN` |  |
| `adsp_new` | K | `hakios.adsp.c` | `CONFIG_ADSP_NEW` |  |
| `adsp_pause` | K | `hakios.adsp.c` | `CONFIG_ADSP_PAUSE` |  |
| `adsp_return` | K | `hakios.adsp.c` | `CONFIG_ADSP_RETURN` |  |
| `adsp_step` | K | `hakios.adsp.c` | `CONFIG_ADSP_STEP` |  |
| `adsp_wait` | K | `hakios.adsp.c` | `CONFIG_ADSP_WAIT` |  |
| `battery` | KL | `hakios.adsp.c`, `hakios.battery.c`, `loader.c` | `CONFIG_BATTERY` |  |
| `battery_shared_pa` | K | `hakios.battery.c` |  | the loader (the address of its charger page) |
| `battery_stop` | K | `hakios.battguard.c` | `CONFIG_BATTERY_STOP` |  |
| `battery_stop_poll` | K | `hakios.battguard.c` | `CONFIG_BATTERY_STOP_POLL` |  |
| `battmgr` | K | `hakios.adsp.c` | `CONFIG_BATTMGR` |  |
| `mic` | K | `hakios.mic.c` | `CONFIG_MIC` |  |
| `mic_adc` | K | `hakios.mic.c` | `CONFIG_MIC_ADC` |  |
| `mic_input` | K | `hakios.mic.c` | `CONFIG_MIC_INPUT` |  |
| `mic_main` | K | `hakios.mic.c` | `CONFIG_MIC_MAIN` |  |
| `mic_secs` | K | `hakios.mic.c` | `CONFIG_MIC_SECS` |  |
| `mic_tap` | K | `hakios.mic.c` | `CONFIG_MIC_TAP` |  |
| `mic_test` | K | `hakios.mic.c` | `CONFIG_MIC_TEST` |  |
| `rec_sound` | K | `hakios.audio.c` | `CONFIG_REC_SOUND` |  |
| `rec_sound_fed` | K | `hakios.audio.c` | `CONFIG_REC_SOUND_FED` |  |
| `rec_sound_mic` | K | `hakios.audio.c` | `CONFIG_REC_SOUND_MIC` |  |
| `smb5` | K | `hakios.smb5.c` | `CONFIG_SMB5` |  |
| `smb5_seconds` | K | `hakios.smb5.c` | `CONFIG_SMB5_SECONDS` |  |
| `sound` | K | `hakios.sound.c` | `CONFIG_SOUND` |  |
| `sound_below` | K | `hakios.sound.c` | `CONFIG_SOUND_BELOW` |  |
| `sound_gain` | K | `hakios.sound.c` | `CONFIG_SOUND_GAIN` |  |
| `sound_guard_as` | K | `hakios.sound.c` | `CONFIG_SOUND_GUARD_AS` |  |
| `sound_pause` | K | `hakios.sound.c` | `CONFIG_SOUND_PAUSE` |  |
| `sound_secs` | K | `hakios.sound.c` | `CONFIG_SOUND_SECS` |  |
| `speaker_probe` | K | `hakios.speaker.c` | `CONFIG_SPEAKER_PROBE` |  |
| `virtio_sound` | K | `hakios.virtio_sound.c` | `CONFIG_VIRTIO_SOUND` |  |

## Cameras (13)

| Argument | Layer | Read by | Configuration | Set by |
|---|---|---|---|---|
| `cam` | KL | `hakios.camera.c`, `hakios.flash.c`, `loader.c` | `CONFIG_CAM` |  |
| `cam_after` | K | `hakios.camera.c` | `CONFIG_CAM_AFTER` |  |
| `cam_fake` | K | `hakios.camera.c` | `CONFIG_CAM_FAKE` |  |
| `cam_fake_fps` | K | `hakios.camera.c` | `CONFIG_CAM_FAKE_FPS` |  |
| `cam_frames` | K | `hakios.camera.c` | `CONFIG_CAM_FRAMES` |  |
| `cam_gain` | K | `hakios.camera.c` | `CONFIG_CAM_GAIN` |  |
| `cam_keep_mb` | K | `hakios.camera.c` | `CONFIG_CAM_KEEP_MB` |  |
| `cam_mode` | K | `hakios.camera.c` | `CONFIG_CAM_MODE` |  |
| `cam_pattern` | K | `hakios.camera.c` | `CONFIG_CAM_PATTERN` |  |
| `cam_sensor` | K | `hakios.camera.c` | `CONFIG_CAM_SENSOR` |  |
| `cam_settle` | K | `hakios.camera.c` | `CONFIG_CAM_SETTLE` |  |
| `cam_shutter` | K | `hakios.camera.c` | `CONFIG_CAM_SHUTTER` |  |
| `cam_vblank` | K | `hakios.camera.c` | `CONFIG_CAM_VBLANK` |  |

## Touch and buttons (17)

| Argument | Layer | Read by | Configuration | Set by |
|---|---|---|---|---|
| `buttons` | K | `hakios.buttons.c` | `CONFIG_BUTTONS` |  |
| `buttons_steps` | K | `hakios.buttons.c` | `CONFIG_BUTTONS_STEPS` |  |
| `pon` | K | `hakios.pon.c` | `CONFIG_PON` |  |
| `pon_steps` | K | `hakios.pon.c` | `CONFIG_PON_STEPS` |  |
| `press_power` | K | `hakios.buttons.c` | `CONFIG_PRESS_POWER` |  |
| `press_power_ms` | K | `hakios.buttons.c` | `CONFIG_PRESS_POWER_MS` |  |
| `touch` | K | `hakios.bq27541.c`, `hakios.s3706.c` | `CONFIG_TOUCH` |  |
| `touch_2v8` | K | `hakios.s3706.c` | `CONFIG_TOUCH_2V8` |  |
| `touch_bb` | K | `hakios.s3706.c` | `CONFIG_TOUCH_BB` |  |
| `touch_flip` | K | `hakios.goodix.c` | `CONFIG_TOUCH_FLIP` |  |
| `touch_poll_us` | K | `hakios.goodix.c` | `CONFIG_TOUCH_POLL_US` |  |
| `touch_rate` | K | `hakios.goodix.c` | `CONFIG_TOUCH_RATE` |  |
| `touch_spi_hz` | K | `hakios.goodix.c` | `CONFIG_TOUCH_SPI_HZ` |  |
| `touch_steps` | K | `hakios.s3706.c` | `CONFIG_TOUCH_STEPS` |  |
| `touch_sweep` | K | `hakios.s3706.c` | `CONFIG_TOUCH_SWEEP` |  |
| `touchscreen` | K | `hakios.buttons.c`, `hakios.goodix.c` | `CONFIG_TOUCHSCREEN` |  |
| `virtio_input` | K | `hakios.virtio_input.c` | `CONFIG_VIRTIO_INPUT` |  |

## Storage and APFS (43)

| Argument | Layer | Read by | Configuration | Set by |
|---|---|---|---|---|
| `apfs_cheap` | K | `apfs_vfsops.c` | `CONFIG_APFS_CHEAP` |  |
| `apfs_check` | K | `hakios.apfs_check.c` | `CONFIG_APFS_CHECK` |  |
| `apfs_chunk` | K | `hakios.apfs_check.c` | `CONFIG_APFS_CHUNK` |  |
| `apfs_contents` | K | `hakios.apfs_check.c` | `CONFIG_APFS_CONTENTS` |  |
| `apfs_depth` | K | `hakios.apfs_check.c` | `CONFIG_APFS_DEPTH` |  |
| `apfs_dev` | K | `hakios.apfs_check.c` | `CONFIG_APFS_DEV` |  |
| `apfs_hash` | K | `hakios.apfs_check.c` | `CONFIG_APFS_HASH` |  |
| `apfs_max` | K | `hakios.apfs_check.c` | `CONFIG_APFS_MAX` |  |
| `apfs_meta_cluster` | K | `apfs_vfsops.c` | `CONFIG_APFS_META_CLUSTER` |  |
| `apfs_mnt` | K | `hakios.apfs_check.c` | `CONFIG_APFS_MNT` |  |
| `apfs_par` | K | `apfs_vfsops.c` | `CONFIG_APFS_PAR` |  |
| `apfs_root_c` | K | `apfs_vfsops.c` | `CONFIG_APFS_ROOT_C` |  |
| `apfs_root_v` | K | `apfs_vfsops.c` | `CONFIG_APFS_ROOT_V` |  |
| `apfs_round` | K | `apfs_vfsops.c` | `CONFIG_APFS_ROUND` |  |
| `apfs_skip` | K | `hakios.apfs_check.c` | `CONFIG_APFS_SKIP` |  |
| `apfs_sparse` | K | `hakios.sparse.c` | `CONFIG_APFS_SPARSE` |  |
| `apfs_store_len` | K | `hakios.apfs_check.c` | `CONFIG_APFS_STORE_LEN` |  |
| `apfs_store_note` | K | `hakios.apfs_check.c` | `CONFIG_APFS_STORE_NOTE` |  |
| `apfs_store_seed` | K | `hakios.apfs_check.c` | `CONFIG_APFS_STORE_SEED` |  |
| `apfs_subtree` | K | `hakios.apfs_check.c` | `CONFIG_APFS_SUBTREE` |  |
| `apfs_threads` | K | `hakios.apfs_check.c` | `CONFIG_APFS_THREADS` |  |
| `apfs_ubc_read` | K | `apfs_vfsops.c` | `CONFIG_APFS_UBC_READ` |  |
| `apfs_volume` | K | `hakios.apfs_check.c` | `CONFIG_APFS_VOLUME` |  |
| `apfs_volume_at` | K | `hakios.apfs_check.c` | `CONFIG_APFS_VOLUME_AT` |  |
| `apfs_walkers` | K | `hakios.apfs_check.c` | `CONFIG_APFS_WALKERS` |  |
| `apfs_write_at` | K | `hakios.apfs_check.c` | `CONFIG_APFS_WRITE_AT` |  |
| `apfs_write_byte` | K | `hakios.apfs_check.c` | `CONFIG_APFS_WRITE_BYTE` |  |
| `apfs_write_len` | K | `hakios.apfs_check.c` | `CONFIG_APFS_WRITE_LEN` |  |
| `apfs_write_path` | K | `hakios.apfs_check.c` | `CONFIG_APFS_WRITE_PATH` |  |
| `ufs_bench` | K | `hakios.ufs.c` | `CONFIG_UFS_BENCH` |  |
| `ufs_check` | K | `hakios.ufs.c` | `CONFIG_UFS_CHECK` |  |
| `ufs_native` | KL | `hakios.ufs.c`, `loader.c` | `CONFIG_UFS_NATIVE` |  |
| `ufs_native_bench` | K | `hakios.ufs.c` | `CONFIG_UFS_NATIVE_BENCH` |  |
| `ufs_native_buffer` | K | `hakios.ufshc.c` | `CONFIG_UFS_NATIVE_BUFFER` |  |
| `ufs_native_check` | K | `hakios.ufs.c` | `CONFIG_UFS_NATIVE_CHECK` |  |
| `ufs_native_hang` | K | `hakios.ufshc.c` | `CONFIG_UFS_NATIVE_HANG` |  |
| `ufs_native_leave` | K | `hakios.ufshc.c` | `CONFIG_UFS_NATIVE_LEAVE` |  |
| `ufs_native_slots` | K | `hakios.ufshc.c` | `CONFIG_UFS_NATIVE_SLOTS` |  |
| `ufs_native_spin` | K | `hakios.ufshc.c` | `CONFIG_UFS_NATIVE_SPIN` |  |
| `ufs_shared_pa` | K | `hakios.ufs.c` |  | the loader (the address of its block-service page) |
| `ufs_write` | KL | `hakios.ufs.c`, `loader.c` | `CONFIG_UFS_WRITE` |  |
| `var_save_ms` | K | `hakios.ufs.c` | `CONFIG_VAR_SAVE_MS` |  |
| `virtio_blk` | K | `hakios.virtio_blk.c` | `CONFIG_VIRTIO_BLK` |  |

## Network (3)

| Argument | Layer | Read by | Configuration | Set by |
|---|---|---|---|---|
| `net_names` | K | `hakios.net.c` | `CONFIG_NET_NAMES` |  |
| `net_trace` | K | `hakios.net.c` | `CONFIG_NET_TRACE` |  |
| `virtio_net` | K | `hakios.virtio_net.c` | `CONFIG_VIRTIO_NET` |  |

## Haptics and torch (5)

| Argument | Layer | Read by | Configuration | Set by |
|---|---|---|---|---|
| `haptic` | K | `hakios.haptic.c` | `CONFIG_HAPTIC` |  |
| `haptic_effect` | K | `hakios.haptic.c` | `CONFIG_HAPTIC_EFFECT` |  |
| `torch` | KL | `hakios.torch.c`, `loader.c` | `CONFIG_TORCH` |  |
| `torch_max_s` | K | `hakios.torch.c` | `CONFIG_TORCH_MAX_S` |  |
| `torch_secs` | K | `hakios.torch.c` | `CONFIG_TORCH_SECS` |  |

## Chip, thermal and PMIC (11)

| Argument | Layer | Read by | Configuration | Set by |
|---|---|---|---|---|
| `pmic` | K | `hakios.pmic.c` | `CONFIG_PMIC` |  |
| `pmic_list` | K | `hakios.pmic.c` | `CONFIG_PMIC_LIST` |  |
| `soc_smem_size` | K | `hakios.socinfo.c` | `CONFIG_SOC_SMEM_SIZE` |  |
| `socinfo` | K | `hakios.socinfo.c` | `CONFIG_SOCINFO` |  |
| `thermal` | K | `hakios.thermal.c` | `CONFIG_THERMAL` |  |
| `thermal_log` | K | `hakios.thermal.c` | `CONFIG_THERMAL_LOG` |  |
| `thermal_pclm10` | K | `hakios.thermal.c` | `CONFIG_THERMAL_PCLM10` |  |
| `thermal_poll` | K | `hakios.thermal.c` | `CONFIG_THERMAL_POLL` |  |
| `thermal_stop` | K | `hakios.thermal.c` | `CONFIG_THERMAL_STOP` |  |
| `thermal_tm0` | K | `hakios.thermal.c` | `CONFIG_THERMAL_TM0` |  |
| `thermal_tm1` | K | `hakios.thermal.c` | `CONFIG_THERMAL_TM1` |  |

## CPU, memory and boot (30)

| Argument | Layer | Read by | Configuration | Set by |
|---|---|---|---|---|
| `applog` | K | `(patch)` | `CONFIG_APPLOG` |  |
| `applog_name` | K | `(patch)` | `CONFIG_APPLOG_NAME` |  |
| `bootcpu` | KL | `loader.c`, `loader_return.c` | `CONFIG_BOOTCPU` |  |
| `cmdb` | K | `hakios.cpufreq.c` | `CONFIG_CMDB` |  |
| `cpufreq` | K | `hakios.cpufreq.c` | `CONFIG_CPUFREQ` |  |
| `cpufreq_fix` | K | `hakios.cpufreq.c` | `CONFIG_CPUFREQ_FIX` |  |
| `cpufreq_look` | K | `hakios.cpufreq.c` | `CONFIG_CPUFREQ_LOOK` |  |
| `cpuload` | K | `hakios.cpufreq.c` | `CONFIG_CPULOAD` |  |
| `ddr` | K | `hakios.cpufreq.c` | `CONFIG_DDR` |  |
| `ddr_rest` | K | `hakios.cpufreq.c` | `CONFIG_DDR_REST` |  |
| `ddr_vote` | K | `hakios.cpufreq.c` | `CONFIG_DDR_VOTE` |  |
| `epoch` | K | `GenericARMPE.cpp` |  | xl (the host's clock) |
| `epss0` | K | `hakios.cpufreq.c` | `CONFIG_EPSS0` |  |
| `epss1` | K | `hakios.cpufreq.c` | `CONFIG_EPSS1` |  |
| `epss2` | K | `hakios.cpufreq.c` | `CONFIG_EPSS2` |  |
| `epss_hw` | K | `hakios.cpufreq.c` | `CONFIG_EPSS_HW` |  |
| `hv_wdt_pet` | K | `loader_return.c` |  | the loader, when it has armed the watchdog (the runner also passes it) |
| `loader_return_after` | KL | `loader.c`, `loader_return.c` |  | xl (XL_SECS) |
| `machine_name` | K | `GenericARMPE.cpp` | `CONFIG_MACHINE_NAME` |  |
| `membench` | K | `hakios.cpufreq.c` | `CONFIG_MEMBENCH` |  |
| `rd` | KL | `(patch)`, `loader.c` | `CONFIG_RD` |  |
| `regtrace` | K | `(patch)` | `CONFIG_REGTRACE` |  |
| `return_cpu` | K | `loader_return.c` | `CONFIG_RETURN_CPU` |  |
| `return_panic` | K | `loader_return.c` | `CONFIG_RETURN_PANIC` |  |
| `small_cores` | K | `(patch)` | `CONFIG_SMALL_CORES` |  |
| `small_cores_after` | K | `GenericARMPE.cpp` | `CONFIG_SMALL_CORES_AFTER` |  |
| `syssleep` | K | `(patch)` | `CONFIG_SYSSLEEP` |  |
| `systrace` | K | `(patch)`, `user_exception_trace.c` | `CONFIG_SYSTRACE` |  |
| `systrace_name` | K | `(patch)` | `CONFIG_SYSTRACE_NAME` |  |
| `xpcproxy_idversion` | K | `(patch)` | `CONFIG_XPCPROXY_IDVERSION` |  |

## Debugging (6)

| Argument | Layer | Read by | Configuration | Set by |
|---|---|---|---|---|
| `ustacks` | K | `user_exception_trace.c` | `CONFIG_USTACKS` |  |
| `ustacks_at` | K | `user_exception_trace.c` | `CONFIG_USTACKS_AT` |  |
| `ustacks_count` | K | `user_exception_trace.c` | `CONFIG_USTACKS_COUNT` |  |
| `ustacks_ms` | K | `user_exception_trace.c` | `CONFIG_USTACKS_MS` |  |
| `ustacks_threads` | K | `user_exception_trace.c` | `CONFIG_USTACKS_THREADS` |  |
| `ustacks_tname` | K | `user_exception_trace.c` | `CONFIG_USTACKS_TNAME` |  |

## Loader (6)

| Argument | Layer | Read by | Configuration | Set by |
|---|---|---|---|---|
| `cpumask` | L | `loader.c` | `CONFIG_CPUMASK` |  |
| `cpus` | L | `loader.c` | `CONFIG_CPUS` |  |
| `gamut` | L | `loader.c` | `CONFIG_GAMUT` |  |
| `serial` | L | `loader.c` | `CONFIG_SERIAL` |  |
| `ufs` | L | `loader.c` | `CONFIG_UFS` |  |
| `ufs_probe` | L | `loader.c` | `CONFIG_UFS_PROBE` |  |

## User space (49)

| Argument | Layer | Read by | Configuration | Set by |
|---|---|---|---|---|
| `-noprogress` | K | `pe_init.c` | `CONFIG__NOPROGRESS` |  |
| `-progress` | K | `pe_init.c` | `CONFIG__PROGRESS` |  |
| `-restore` | K | `pe_init.c` | `CONFIG__RESTORE` |  |
| `autounlock` | U | `inject_sb.c` | `CONFIG_AUTOUNLOCK` |  |
| `aw8697` | K | `hakios.aw8697.c` | `CONFIG_AW8697` |  |
| `bq27541` | K | `hakios.bq27541.c` | `CONFIG_BQ27541` |  |
| `bt` | K | `hakios.bt.c` | `CONFIG_BT` |  |
| `bt_again` | K | `hakios.bt.c` | `CONFIG_BT_AGAIN` |  |
| `ca_metallib` | U | `inject.c` | `CONFIG_CA_METALLIB` |  |
| `ca_prof` | U | `HakiosMetalCA.m` | `CONFIG_CA_PROF` |  |
| `ca_vbl` | U | `inject.c` | `CONFIG_CA_VBL` |  |
| `caenv` | U | `inject.c`, `inject_sb.c` | `CONFIG_CAENV` |  |
| `cc_fade` | U | `inject_sb.c` | `CONFIG_CC_FADE` |  |
| `flash` | K | `hakios.flash.c` | `CONFIG_FLASH` |  |
| `gic_timer_ppi` | K | `gic_v3_irq.c` | `CONFIG_GIC_TIMER_PPI` |  |
| `gl` | U | `inject.c`, `inject_sb.c` | `CONFIG_GL` |  |
| `glcompile` | U | `inject.c` | `CONFIG_GLCOMPILE` |  |
| `glno` | U | `inject.c` | `CONFIG_GLNO` |  |
| `glsb3` | U | `inject.c` | `CONFIG_GLSB3` |  |
| `glyes` | U | `inject.c` | `CONFIG_GLYES` |  |
| `hapc` | K | `hakios.hapc.c` | `CONFIG_HAPC` |  |
| `m2v_utrace` | U | `HakiosMetalCA.m` | `CONFIG_M2V_UTRACE` |  |
| `metal_all` | U | `HakiosMetalCA.m` | `CONFIG_METAL_ALL` |  |
| `metal_defer` | U | `HakiosMetalCA.m` | `CONFIG_METAL_DEFER` |  |
| `metal_dump` | U | `HakiosMetalCA.m` | `CONFIG_METAL_DUMP` |  |
| `metal_fps` | U | `HakiosMetalCA.m` | `CONFIG_METAL_FPS` |  |
| `metal_gmem` | U | `HakiosMetalCA.m` | `CONFIG_METAL_GMEM` |  |
| `metal_gmem_draws` | U | `HakiosMetalCA.m` | `CONFIG_METAL_GMEM_DRAWS` |  |
| `metal_gmem_off` | U | `HakiosMetalCA.m` | `CONFIG_METAL_GMEM_OFF` |  |
| `metal_luma_boost` | U | `HakiosMetalCA.m` | `CONFIG_METAL_LUMA_BOOST` |  |
| `metal_m2v` | U | `HakiosMetalCA.m` | `CONFIG_METAL_M2V` |  |
| `metal_skip` | U | `HakiosMetalCA.m` | `CONFIG_METAL_SKIP` |  |
| `metal_trace` | U | `HakiosMetalCA.m` | `CONFIG_METAL_TRACE` |  |
| `metal_watch` | U | `inject.c` | `CONFIG_METAL_WATCH` |  |
| `root_apfs` | U | `init.c` | `CONFIG_ROOT_APFS` |  |
| `sb_delay` | U | `inject_sb.c` |  | xl (XL_SB_DELAY) |
| `slpi` | K | `hakios.slpi.c` | `CONFIG_SLPI` |  |
| `slpi_base` | K | `hakios.slpi.c` | `CONFIG_SLPI_BASE` |  |
| `slpi_wait` | K | `hakios.slpi.c` | `CONFIG_SLPI_WAIT` |  |
| `switcher_wallpaper` | U | `inject_sb.c` | `CONFIG_SWITCHER_WALLPAPER` |  |
| `tu_debug` | U | `HakiosMetalCA.m` | `CONFIG_TU_DEBUG` |  |
| `ui_lite` | U | `inject_sb.c` | `CONFIG_UI_LITE` |  |
| `var_hfs` | U | `init.c` | `CONFIG_VAR_HFS` |  |
| `var_store` | U | `init.c` | `CONFIG_VAR_STORE` |  |
| `wallpaper_cache` | U | `inject_sb.c` | `CONFIG_WALLPAPER_CACHE` |  |
| `wallpaper_freeze` | U | `inject_sb.c` | `CONFIG_WALLPAPER_FREEZE` |  |
| `wcnbt` | K | `hakios.wcnbt.c` | `CONFIG_WCNBT` |  |
| `wled` | K | `hakios.wled.c` | `CONFIG_WLED` |  |
| `wled_test` | K | `hakios.wled.c` | `CONFIG_WLED_TEST` |  |
