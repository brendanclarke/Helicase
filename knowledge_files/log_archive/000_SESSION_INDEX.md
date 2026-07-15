# LXR-02 Firmware Port — Session Index

**Project**: STM32F765VIH6 port of LXR 0.37 drum machine firmware
**Repo**: `lxr02-037_port/` | **Log format**: `00x_SESSION_HANDOFF_LOG.md`

---

## Quick Reference — What's In Each Log

| # | Date | Source at end | Topic |
|---|------|----------------|-------|
| 001 | 2026-04-19 | `lxr02.tar.gz` | Hardware bring-up |
| 002 | 2026-04-21 | `lxr02_dannegger_enc.tar.gz` | LCD, quad encoders, menu stub, encoder direction |
| 003 | 2026-04-23 | `lxr02_enc_tim14_2khz.tar.gz` | Resource audit, 24-bit audio, encoder investigation |
| 004 | 2026-04-24 | `lxr02_enc_accel_v2.tar.gz` | TIM1 IC encoder (permanent), Dannegger algo, acceleration, rebound suppression |
| 005 | 2026-04-26 | `lxr02_reorganized.tar.gz` | Full menu port, preset load, display bugs, directory restructure |
| 006 | 2026-04-29 | `lxr02_knob_collapse.tar.gz` | Display race fix, encoder overflow bugs, endlessPots refactor, knob repaint collapse |
| 007 | 2026-04-30 | `lxr02_seq_dsp_prep.tar.gz` | Kit/globals save, DTCM linker, RNG port, compat shims, encoder asymmetry fix, memtest, screensaver, AudioCodecManager |
| 008 | 2026-05-04 | `lxr02_new_refactored.zip` | Audio buffer pipeline fix (SPSC queue), DSP voice bring-up (VLAs, RNG, LFO, FPU, -O2) |
| 009 | 2026-05-04 | `lxr02-037_port.tar.gz` | Refactor/consolidation: AudioCodecManager absorbed audioTest+sineBufferTest, lcd_diag functions, sdTest moved, stale files deleted |
| 010 | 2026-05-05 | `lxr02-037_port.tar.gz` (unchanged) | Architecture: SD blocking root cause, non-blocking SD ISR plan, ISR priority redesign |
| 011 | 2026-05-05 | `lxr02-037_port.tar.gz` (unchanged) | SD solution: asyncfatfs adoption, NB_FatFS rejected, FatFS fork rejected, full implementation plan |
| 012 | 2026-05-08 | `lxr02-037_port-02.tar.gz` | asyncfatfs implementation, preset_morph index 127 fix, HiHat VLA fix, sd_fsm EOF fix |
| 013 | 2026-05-08 | `lxr02-037_port-02.tar.gz` (unchanged) | DSP performance audit, I/D-cache+MPU enable, LFO kit load fix, kit save size fix (229B) |
| 014 | 2026-05-09 | `lxr02-037_port-02.tar.gz` (unchanged base) | Sequencer source import, button/menu + LED audit closure, frontPanelParser dispatcher wiring |
| 015 | 2026-05-09 | `lxr02-037_port-02.tar.gz` (local changes; no new tarball) | Sequencer/front-panel wiring fixes, seq_init, tempo math, PATGEN/Euklid LED+CLZ |
| 016 | 2026-05-10 | `lxr02-037_port-02.tar.gz` (local changes; no new tarball) | Parameter morph bring-up, morph final-pass scheduler, endless-pot deadzone/ghost fixes |
| 017 | 2026-05-11 | `lxr02-037_port-02.tar.gz` (local changes; no new tarball) | Full typed save/load, SD filesystem facade/reorg, canonical DSP/system timebase, CPU-use widget, UI/LED polish |
| 018 | 2026-05-12 | `lxr02-037_port-02.tar.gz` (local changes; no new tarball) | Sample flash loading, loop append loader, audio suspend/resume, filename display/sort |
| 019 | 2026-05-14 | `lxr02-037_port-02.tar.gz` (local changes; no new tarball) | Full MIDI/clock/jack implementation: USART3 interrupt-driven RX/TX, TIM2 timestamp counter, MidiRealtime ring, TIM3 sequencer timing owner, real CLK/RST jack backend, voice trigger pending ring, PAR_EXT_SYNC, CC1→MORPH, BAR1/BAR2 MIDI path, OUTPUT_DMA_SIZE corrected to 32 |
| 020 | 2026-05-14 | `lxr02-037_port-02.tar.gz` (local changes; no new tarball) | Slider audio path completion: direct mixer multiplier, always-on update, per-block interpolation, configurable log taper |
| 021 | 2026-05-16 | `lxr02-037_port-02.tar.gz` (local changes; no new tarball) | Audio jack-detect traced: OUT1L/OUT1R/OUT2L/OUT2R mapped to PD6/PD7/PB4/PB6; runtime model superseded in Session 025 |
| 022 | 2026-05-16 | `lxr02-037_port-02.tar.gz` (local changes; no new tarball) | Dither audit + 24-bit path upgrade: sample_mx_t widening at mixer/output stage, true 24-bit DMA pack, loudness regression fixed; dth global menu option planned but not yet wired |
| 023 | 2026-05-17 | local working directory `lxr02-037_port/` | CPU refactor + DSP hot-path cleanup: foreground front-panel service, 5kHz LCD, slider LUT, filesystem idle poll limit, oscillator interp budget, oscillator-only ITCM, combined sample/loop load, encoder residue fix, memory audit |
| 024 | 2026-05-21 | local working directory `lxr02-037_port/` | Copy/clear audit and fix: clear-mode encoder target select + execute, shift/copy release ownership, COPYCLEAR_AUDIT, README/MEMORY consolidation |
| 025 | 2026-05-23 | local working directory `lxr02-037_port/` | SD/FAT compatibility, stale globals/.all load policy, menu globals cleanup, CLK/RST correction, OUT jack-detect retained-state polling |
| 026 | 2026-05-25 | local working directory `lxr02-037_port/` | Load/save button glitch root-cause audit (fix pending menu.c); filesystem malformed-file name fallthrough fix (afatfs_feof pattern) |
| 027 | 2026-07-04 | local working directory, branch `dev-burst-reduction` | Main-loop burst reduction: chunked kit/all/performance sound apply; AUDIO_DMA_FRAMES left at 96 |
| 028 | 2026-07-05 | local working directory, branch `dev-burst-reduction` | frontPanelParser removed: direct Menu/Button/LED/Preset/MIDI/Pattern/Sequencer APIs; PatternData + Scene/Pattern introduced |
| 029 | 2026-07-06 | local working directory, branch `dev-burst-reduction` | Pattern storage ownership pass + Preset folder move: Sequencer raw pattern storage access removed, PatternData APIs expanded, `Core/Preset` moved to `Core/Scene/Preset`, staging/globals audits written |
| 030 | 2026-07-07 | local working directory, branch `dev-burst-reduction` | Phase 2 filesystem start: `knowledge_files/specification_reference/FILESYSTEM_SPEC.md`, generated `SD_CARD/Kit`, `storageTypes.c/h`, directory-kit load from root `Kit/`, space/underscore numbered folder scan |
| 031 | 2026-07-09 | local working directory, branch `dev-burst-reduction` | One-pattern/8-bar bridge, STEP track settings, LED flash overlay, kit converter rebuild, morph VOICE mode, voice preview, per-track shuffle |
| 032 | 2026-07-10 | local working directory, branch `dev-burst-reduction` | Instrument parameter refactor follow-up: descriptor voice pages, Scene kit readthrough, runtime shapers, Slak load fix, instrument spec consolidation; Morph/mod/automation descriptor paths still broken |
| 033 | 2026-07-11 | local working directory, branch `dev-burst-reduction` | Phase 3 instrument runtime: LFO target/display/apply repair, LFO two-target expansion, descriptor Morph fix, per-voice Morph, Scene modulation targets, per-voice Morph modulation, docs/spec closeout |
| 034 | 2026-07-12 | local working directory, a development branch | Instrument Load completion: instrument registry flags, Choke/track-7 behavior, root Instrument pool/load UI, scene-aware Kit/Instrument load refinements, and staged transactional Instrument commit to eliminate stale modulation/runtime state |
| 035 | 2026-07-13 | local working directory, branch `dev-phase2-filesys` | Phase 3 Kit/Instrument Morph Load, LFO `self` storage, descriptor-domain LFO scaling, and new-format Kit Save to `Kit/` directories |
| 036 | 2026-07-14 | local working directory, development branch | asyncfatfs LFN/case expansion, File/Dir diagnostics, Kit Save repair, `000` slot policy, restored Kit load/save, and root Instrument Save |
| 037 | 2026-07-15 | local working directory, branch `dev-phase2-filesys` | FAILED TESTING: Morph save/load expansion and asyncfatfs rename/replace attempts left Kit Save unable to create/load usable Kit directories; consider rollback to Session 036 boundary or pre-LFN expansion |

---

## Session Summaries

### 001 — Hardware Bring-up (2026-04-19)
Full peripheral confirmation on physical hardware: LCD, LEDs, buttons, encoders, sliders, both audio DACs (I2S2/I2S3, CS4344), MIDI DIN TX, USB MIDI, CLK/RST jacks. Clock brought to 216MHz. Key discoveries: EXTI_IMR must be cleared before sysclk_init (bootloader leaves EXTI4/5 armed); DCD_DevConnect() needed explicitly for USB (VBUS sensing disabled for ADUM3160). SD card SPI noted as working — **later found to be a false positive** (PA8=CS was wrong; CMD0 0x00 was 74HC165 driving MISO low).
- **Find here**: IRQ numbers, clock config, EXTI_IMR rationale, USB init order, pin confirmations

### 002 — LCD, Quad Encoders, Menu Stub (2026-04-21)
Non-blocking LCD driver (TIM7, 10kHz, 128-entry SPSC ring). Quad encoder RV1-4 rewritten as atan2-based delta tracking (analog sine/cosine, not Gray code). frontPanelParser audit. Menu stub with dirty-flag repaint. Encoder direction fixed (A/B swap). Algorithm at this point: GRAY table lookup — **later replaced in Session 4**.
- **Find here**: TIM7 LCD driver design, atan2f(b,a) argument order rationale, menu_dirty flag pattern, lcd_init ordering

### 003 — Resource Audit, 24-bit Audio, Encoder Investigation (2026-04-23)
Flash/RAM budget established. 24-bit I2S confirmed: SPI_DR is always 16-bit even in 24-bit mode; DMA uses int16_t MSW+LSW pairs. Encoder hardware traced: PE13/PE14 have external 10kΩ pull-ups (no internal pull). Eight encoder approaches tried and documented. Final for this session: bare Dannegger in TIM14 at 2kHz — **superseded in Session 4**. Hardware map corrected (R73/R74 = 1kΩ series, R75/R76 = 10kΩ pull-ups, no filter caps).
- **Find here**: Flash sector layout (initial), 24-bit I2S DMA rationale, encoder hardware trace, PE13/PE14 pull-up confirmation

### 004 — TIM1 IC Encoder, Acceleration, Rebound Suppression (2026-04-24)
**Permanent encoder solution**: TIM1 Input Capture on CH3/CH4 (PE13/PE14 as AF1), ICxF=0xF hardware filter (32-sample silicon debounce). LUT state machine tried then abandoned (val>>2 asymmetry). Dannegger difference algorithm adopted with `last = new & 3` seed. Phase-offset seed `(new+3)&3` tried and discarded. Acceleration: 8-entry timestamp circular buffer, 1×–4× linear multiplier, 100ms decay. Rebound suppression: `ts_dirs[]` majority-direction check. Sessions 4 and 4b merged into this log.
- **Find here**: TIM1 IC configuration, why LUT was abandoned, why (new+3)&3 is forbidden, acceleration constants, ts_dirs[] design

### 005 — Full Menu System, Preset Load, Display Fixes (2026-04-26)
Complete menu port: all voice pages, global/MIDI page, load/save page. Preset load (kit + globals from SD). Display bugs fixed: active param indicator (upr_three), scroll sign (checkScrollSign), edit mode entry (btnClicked), MODE4 mapping (BUT_MODE1-buttonNr formula), SELECT sub-page switching. LCD cursor for save page name editing. Encoder seed corrected to `last = new & 3`. quadEnc renamed to endlessPots. Directory restructured. `while→if` in buttonHandler_processEvents (intentional — documented). Known issue at end: display desync on rapid SELECT presses — **resolved in Session 6**.
- **Find here**: Menu architecture, BUT_MODE1=31 offset formula, why while→if is intentional, load/save page memset fix, directory structure

### 006 — Display Race Fix, Encoder Overflow, EndlessPots Refactor (2026-04-29)
**Display corruption root cause**: `lcd_q_count` shared RMW race (ldrb/adds/strb non-atomic). Fixed with head/tail-only SPSC ring — no shared counter. Setcursor now emitted unconditionally before every data write. Three encoder acceleration bugs fixed: uint8 wrap on fast spin (int16 saturating add), load/save underflow (same fix), rebound amplified by acceleration (ts_dirs[] majority check). EndlessPots: volatile struct split, compound-literal macros replaced, hot-path init guard. RV1-4 knob repaint collapse (menu_knobs_dirty flag). **Two failed approaches documented**: TIM7 idle gating (wakeup race → freeze), broad repaint coalescing (lag and freeze). SD card confirmed on bit-bang GPIO — all SPI1 contention concerns from Sessions 1-4 are moot.
- **Find here**: lcd_q_count race explanation, SPSC ring design, int16 saturating add pattern, why TIM7 gating is forbidden, why broad coalescing is forbidden, SD pin confirmation (PC12/PD2/PC8/PD0)

### 007 — Save Logic, DSP Prep, Encoder Asymmetry Fix, Memtest, Screensaver (2026-04-30)
Kit save (.SND) and globals save (.GLO) implemented and verified. Latent uint8_t loop counter bug fixed in preset_loadGlobals. DTCM linker section (.dtcm/.dtcmz) with INDTCM/INCCM macros. F765 RNG ported (bare register, direct write, no StdPeriph). Compat shims (stm32f4xx.h, full MidiMessages.h, frontPanelParser stubs, globals.h, SeqStep.h). uint32_t systick_ticks added alongside time_sysTick. **Encoder firing asymmetry fixed**: val>>2 was floor division on negatives (CCW fired on 1 transition, CW needed 4) — replaced with round-toward-zero divide. Flash sector layout probed by memtest (sectors 5-11 BLANK, app in sector 2, single-bank). Screensaver ported. CGRAM custom character support. AudioCodecManager reshaped to interrupt-driven double-buffer model. Boot splash added.
- **Find here**: .SND/.GLO file format, DTCM placement, systick_ticks vs time_sysTick rationale, round-toward-zero divide explanation, flash sector layout, memtest safety analysis, bCurrentSampleValid → SPSC queue transition

### 008 — Audio Buffer Pipeline Fix, DSP Voice Bring-up (2026-05-04)
**Audio glitch root cause**: `ready_slot` scalar overwritten by second render before ISR consumed first — non-atomic `ready_count` RMW corrupting queue. Fixed with 2-entry SPSC head/tail queue (no shared counter). ISR signalling: both HT and TC fire `pack_audio_half`. Pipeline verified via systematic `sineBufferTest` isolation. `bCurrentSampleValid` superseded by `audioCodec_queueFreeSlots()`. All DSP voices brought up: **VLA stack corruption** in Snare (`transBuf[size]`) and Cymbal (`mod[size]`, `mod2[size]`) silently corrupted all voices — fixed with `static [...OUTPUT_DMA_SIZE]`. **RNG bugs**: `RCC_AHB2ENR` at wrong address (0x40023830 = AHB1ENR; correct 0x40023834), `RNG_CR |=` RMW on unpowered peripheral (→ AHB stall → underruns; fixed with direct write). `GetRngValue()` changed to `int16_t`, cast required at every call site. LFO noise normalization fixed (was dividing by 0xffffffff → near-zero). FPU explicitly enabled in sysclk_init (CPACR). Makefile changed to -O2. Result: 78 startup underruns, stable thereafter.
- **Find here**: SPSC queue design, why ready_count was removed, VLA prohibition rationale, RCC_AHB2ENR correct address, RNG_CR direct-write rationale, GetRngValue cast requirement, LFO noise divisor, FPU CPACR enable, -O2 requirement

### 009 — Refactor and Consolidation (2026-05-04)
Pure refactor — no behaviour changes. `AudioCodecManager.c` absorbed `audioTest.c` and `sineBufferTest.c` into a 10-section documented structure. `audioCodec_init()` is now the single hardware entry point. `CodecInit()` retained as no-hardware legacy wrapper. `audioCodec_renderSineBlock()` absorbed from sineBufferTest. Both DMA ISRs definitively in AudioCodecManager.c. `lcd_diagDisplayInt()` and `lcd_diagDisplayFloat()` added (no printf, bare-metal safe; float format: `LABL: +0.00000E0`). `sdTest.c/h` moved to `Core/Hardware/SD/`. `main.c` TEST FUNCTIONS block added (commented out) with sine test and SD test, each with usage notes. All stale copy/backup files deleted. `random.c` comments updated to document the AHB2ENR address fix and RNG_CR direct-write rationale. Repository renamed to `lxr02-037_port`.
- **Find here**: AudioCodecManager section map, lcd_diagDisplay format spec, sdTest_tick() 1-second blocking wait note, plli2s_init HSERDY guard explanation, Stream 4/7 sync drift limitation

### 010 — Architecture: SD Blocking, Non-blocking SD ISR Plan, ISR Redesign (2026-05-05)
Pure fact-finding and design session — no code written, tarball unchanged. **Root cause of post-kit-load underruns identified**: `preset_loadDrumset()` and `preset_saveGlobals()` call blocking FatFS operations from the main loop. `f_open` blocks 1–50ms; render budget is 2.18ms. Hardware SPI remapping investigated and confirmed impossible (all viable SPI peripherals taken, SPI5/6 on unbonded TFBGA100 pins). DSP-in-DMA-ISR approach proposed and rejected (hard 2.18ms ceiling, system locks on overrun — no graceful degradation). SD-chunking-in-main-loop proposed and rejected (f_open is one FatFS call that still blocks for its full duration). **Agreed architecture**: non-blocking SD FSM on TIM5 ISR (priority 6), with byte-pump at SPI layer and explicit FSM states replacing all SD busy-wait loops. Three-session plan established. BAR1/BAR2 race condition (voiceControl_noteOn/Off called from main loop touching DSP state) identified as latent bug. Internal DAC non-use on PA4/PA5 documented. TIM6_DAC_IRQHandler name explained. Revised NVIC priority table designed.
- **Find here**: Why hardware SPI is impossible on this board, why DSP-in-ISR is wrong, why main-loop chunking is insufficient, non-blocking SD ISR architecture, target NVIC priority table, TIM6/DAC shared IRQ explanation, internal DAC PA4/PA5 conflict warning, BAR1/BAR2 race condition, encode_read4 TIM1/TIM6 preemption issue, pre-Session-17 96-frame hardware timing budget calculation

### 011 — SD Solution: asyncfatfs Adoption, Implementation Plan (2026-05-05)
Design and planning session — no code written, tarball unchanged. **Original LXR SD architecture analyzed**: two-MCU design — AVR owns SD and trickle-feeds parameters to STM32 via UART; STM32 mainboard has SD access only for sample upload (halts audio first, never concurrent SD+audio). **FatFS blocking surface fully traced**: 28 disk_read/disk_write sites across ~20 functions, up to 6 levels of call nesting. Forking FatFS for async conversion estimated at ~20 functions × manual state machine conversion — high risk, high LOC. **NB_FatFS evaluated and rejected**: C++ with heap allocation, lambdas, no _FS_TINY, no FAT16, 9500 lines, designed for hardware DMA callbacks. **asyncfatfs (Betaflight/Cleanflight) adopted**: ground-up FAT16/FAT32 reimplementation, pure C, polling FSM with 8-sector LRU cache, 3700 lines, battle-tested on STM32F4/F7, no heap, fixed file pool. Main-loop polling chosen for initial implementation (ISR migration path preserved for future). Detailed pseudocode written for every new and modified file.
- **Find here**: Original LXR two-MCU SD architecture, FatFS blocking call graph, NB_FatFS rejection rationale, asyncfatfs adoption rationale, asyncfatfs directory/file map with line-by-line notes, sdcard_lxr02.c shim pseudocode (state machine for bit-bang sector transfers), sd_fsm.c pseudocode (high-level operation sequencing), presetManager rewrite pseudocode, kitBrowser rewrite plan, main.c boot sequence pseudocode, Makefile changes, RAM budget (~5.6KB), ISR vs main-loop polling analysis, burst size options (A/B/C/D documented)

### 012 — asyncfatfs Implementation + Bug Fixes (2026-05-08)
Full asyncfatfs integration implemented and verified on hardware. New files: `sdcard_lxr02.c` (SD card driver shim, state machine over bit-bang SPI, 16 bytes/burst), `sd_fsm.c` (operation state machine: LOAD_KIT, SAVE_KIT, LOAD_GLOBALS, SAVE_GLOBALS, SCAN_KITS, LOAD_NAME), `asyncfatfs.c/h` + `fat_standard.c/h` + `sdcard.h` (upstream library with LXR-02 modifications). Rewrote `presetManager.c` (async via sd_fsm, preset_status_t polling), `kitBrowser.c` (async via sd_fsm), `menu.c` (added `menu_pollPresetStatus()` for deferred post-load work). Boot sequence reordered: all SD ops complete before `audioCodec_init()`. ChaN FatFS removed from build. **Four bugs found and fixed**: (1) `preset_morph()` index 127 causes uint16 underflow in `midiParser_ccHandler` → wild write to `midiParser_originalCcValues[65536]` — memory corruption on every kit load since Session 7/8, masked by BSS layout luck. (2) HiHat VLA stack corruption — `mod1[size]`/`mod2[size]` in `HiHat_calcSyncBlock()` missed in Session 8 VLA sweep, masked because boot kit hi-hat produces no sound. (3) sd_fsm read hung at EOF for short .SND files (<END_OF_SOUND_PARAMETERS bytes). (4) FSM collision: `preset_loadName()` blocked subsequent `preset_loadDrumset()` on load page.
- **Find here**: asyncfatfs integration details, sdcard_lxr02 shim design (why SD_sendCommand can't be used — CS deassert), sd_fsm operation phases, preset_status_t state machine, menu_pollPresetStatus design, boot sequence ordering, preset_morph index 127 overflow root cause and fix, HiHat VLA root cause (why boot kit masked it), EOF handling for short .SND files, FSM single-operation constraint, BSS-layout-dependent corruption investigation

### 013 — DSP Performance Audit, Cache/MPU Enable, LFO Kit Load Fix (2026-05-08)
Full DSP pipeline audit (DSP_AUDIT.md) answering 10 questions about underrun causes, with 15-item prioritised action list. I-Cache enabled, D-Cache enabled with MPU (Region 0: 1MB WT for all SRAM, Region 1: 4KB Strongly-Ordered for DMA buffers). DMA buffers placed in .dma_nocache linker section; audioOutBuffer moved to DTCM via INDTCMZ. -Ofast for DSP files, -flto added (user). **LFO kit load bug**: PAR_VOICE_LFO1-6 values not reaching modulation targets during kit load. Root cause: `frontPanel_sendData()` for CC_VELO_TARGET/CC_LFO_TARGET was inadequate in merged single-MCU context. User fix: `preset_sendModTarget()` in presetManager.c calls `modNode_setDestination()` directly, bypassing frontPanel_sendData roundabout. Agent-written parameterArray population (PAR_VOICE_LFO/PAR_TARGET_LFO → &parameter_values[]) was kept but was not the fix. **Note**: preset_sendModTarget has a missing `break` between CC_VELO_TARGET and CC_LFO_TARGET cases (fall-through bug).
- **Find here**: DSP audit questions/answers, I-cache/D-cache/MPU configuration, .dma_nocache linker section, DTCM placement, -Ofast DSP Makefile rule, PAR_VOICE_LFO kit load root cause, preset_sendModTarget design, ResonantFilter double-literal locations, DrumVoice VLA location

### 014 — Sequencer Import + Front-Panel Audit Closure (2026-05-09)
Session focused on integration parity and documentation closure. `Core/Sequencer/` now carries original-LXR `sequencer.c/.h`, `EuklidGenerator.c/.h`, `SomData.c/.h`, and `SomGenerator.c/.h`, while `sequencer_.c/.h` is retained as a legacy reference copy. `BUTTONHANDLER_MENU_AUDIT.md` and `LED_AUDIT.md` were closed with results docs (`BUTTONHANDLER_MENU_AUDIT_RESULTS.md`, `LED_AUDIT_SUMMARY.md`). `Core/MIDI/frontPanelParser.c` was expanded from mostly no-op behavior into an explicit local dispatcher for the button/menu/LED protocol traffic, with remaining SOM/trigger/euclid backend gaps marked as intentional `_SEQUENCER_ADD_SPIKE_` no-ops pending backend linkage in this build target.
- **Find here**: sequencer source import scope, audit closure status, parser mediation behavior, remaining backend no-op branches

### 015 — Sequencer Front-Panel Fixes + Timing + PATGEN (2026-05-09)
Session 15 closed the root sequencer audit files and fixed the hardware-reported regressions after the Session 14 import. Reverse sequencer LED/SEQ feedback now uses `seq_notifyFront()` and `SeqLedState`; Euclid/SOM/copy-clear paths are wired; `BUTTON_TIMEOUT` is corrected to 500ms; `seq_init()` runs at boot so default sub-steps/probability/volume exist; sequencer tempo math was temporarily moved to milliseconds on the 1kHz port tick (restored to canonical 4kHz `systick_ticks` in Session 17); PATGEN main-step LEDs refresh after generation; and the Euklid front-stacking bug was fixed by restoring ARM `__CLZ(0) == 32` semantics in the CMSIS shim.
- **Find here**: reverse SEQ_CC direction collision, SeqLedState LED bridge, missing seq_init root cause, 4x slow tempo math, future sequencer jitter audit, PATGEN LED refresh, __CLZ zero-input/Euklid dependency, audit consolidation

### 016 — Parameter Morph + Endless-Pot Ghost Fixes (2026-05-10)
Session 16 implemented original-LXR parameter morph as a rate-limited front-panel CC dump with original interpolation, mod-target protections, index-127 skip, and a no-cache generation scheduler that guarantees a complete final pass at the latest morph value. It also fixed `preset_sendModTarget()` velocity fallthrough. RV1-RV4 analog endless pots gained raw A/B snapshot baselines, page-change rebaselining, post-delta rebaselining, `PAR_MORPH`-only double angular speed, and pre-delta false-start cancellation; `ENDLESS_POT_DEADZONE` is now 20.
- **Find here**: original morph behavior, parameters2[] placeholder, no morph skip cache, transient waveform final-pass bug, mod-target skip rules, endless-pot baseline/deadzone state, PAR_MORPH-only double speed, BPM drift diagnosis, audit consolidation

### 017 — Save/Load Completion, Timebase, CPU Widget (2026-05-11)
Session 17 completed the load/save backend for kits, morph kits, patterns, performances, all-files, and globals; reorganized `Core/Hardware/SD/` behind `filesystem.c/h`; removed obsolete FatFS/test leftovers; restored canonical 236-byte `.snd` saves with short-kit zero-fill; implemented direct streaming `.pat`, `.prf`, and `.all` serializers; fixed load-page empty-slot and async-name races; restored canonical "Loading pattern" UI behavior; split the audio hardware buffer (`AUDIO_DMA_FRAMES=96`) from the canonical DSP/control block (`OUTPUT_DMA_SIZE=16`); restored `systick_ticks` to the original 4kHz mainboard timebase while keeping TIM6 `time_sysTick` at 1kHz for UI/service work; added a DWT-based Global `cpu` widget; polished boot splash/screensaver behavior; and fixed PERF/VOICE SELECT LED repaint edge cases.
- **Find here**: filesystem facade/filetype registry, `.snd` 236-byte canonical behavior, short-kit zero-fill, morph-kit load/save semantics, `.pat`/`.prf`/`.all` container layouts, load/save UI races, 16-frame DSP sub-blocks, 4kHz SysTick split, DWT queue-free CPU-use meter, boot splash/screensaver LCD on/off, PERF queued-pattern LED behavior, VOICE subpage LED behavior

### 018 — Sample Flash Loading, Loop Append, Audio Resume (2026-05-12)
Session 18 implemented user sample loading from SD into flash. The linker now reserves sectors 6-11 (`0x08080000-0x081FFFFF`) for sample storage; `sampleFlash.c/h` provides guarded F765 erase/program helpers; `SampleMemory.c/h` now scans and caches 120 metadata/display-name entries with loop flags; `Load:[Samples ]` wipes/reinstalls accepted WAVs from `/samples`; `Load:[SampLoop]` appends looped WAVs from `/loops`; the menu shows compact `s01`.. labels and full filename-derived 8-char names; and `AudioCodecManager` gained suspend/resume that hardware testing confirmed returns audio after sample load. User verified normal sample load sounds good. Big caveat: `SampleInfo.size` is 32-bit metadata now, but oscillator indexing still uses the legacy `phase >> 17` path, so true long-sample playback remains unfinished.
- **Find here**: sample flash linker reserve, sampleFlash sector guards, SampleMemory 120-entry metadata/name tables, `/samples` full install, `/loops` append loop install, mono 16-bit 44.1kHz WAV validation, whole-filename lexicographic LFN sort, first4+CGRAM0+last3 display-name abbreviation, audioCodec_suspend/resume, long-sample playback caveat

### 019 — MIDI, Clock, and Trigger-Jack Implementation (2026-05-14)
Session 019 implemented all outstanding MIDI, clock, and jack phases from AUDIT-CLOCK-MIDI.md. USART3 RX/TX is now fully interrupt-driven with dual TX FIFOs (realtime priority + normal). TIM2 is initialised as a shared free-running 1 µs timestamp counter. A new `MidiRealtime.c/h` provides a 32-entry timestamped SPSC ring for MIDI_CLOCK/START/CONTINUE/STOP, pushed in the USART3 and USB ISRs. TIM3 (4 kHz, IRQ29, priority 2) is the new sequencer timing owner: it drains the realtime ring, drains jack events, and calls `seq_tick()` — removing all three from the main loop. Real trigger-jack backend replaces the PD3 diagnostic: PC13 CLK OUT, PD4 CLK IN (EXTI4), PD5 RST IN (EXTI9_5). Voice triggers from MIDI, BAR1/BAR2, and sequencer are deferred through a 32-entry pending ring and drained at the audio render boundary, eliminating the BAR1/BAR2 DSP race. New `PAR_EXT_SYNC` (SyncInpt) replaces the old BPM=0 external-sync toggle. AUTO mode priority: jack > DIN MIDI > USB MIDI > internal. CC1 on the global MIDI channel controls MORPH; the current descriptor/morph target conversion is 0..126 -> value * 2, 127 -> 255. BAR1/BAR2 record through the full MIDI note path using assigned/default voice note numbers (Drum1=36..Drum7=42). `OUTPUT_DMA_SIZE` corrected to 32 (was 16). Build verified; full hardware bench testing is the next step.
- **Find here**: TIM3 sequencer timing owner, TIM2 timestamp counter, MidiRealtime ring design, dual TX FIFO, voice trigger pending ring and audio-boundary drain, EXTI4/EXTI9_5 jack backend, PAR_EXT_SYNC AUTO priority logic, CC1→MORPH mapping and double-fire fix, BAR1/BAR2 MIDI path with assigned note lookup, OUTPUT_DMA_SIZE=32 correction, startup vector table updates (IRQ10/23/29/39); see Session 025 for current RST IN semantics

### 020 — Slider Mixer Multiplier + Taper (2026-05-14)
Session 020 completed the full slider audio-path audit and moved RV5–RV10 to a dedicated mixer-stage gain multiplier architecture. Sliders now update `slider_vol[]` continuously from ADC DMA with deadzone clamping and configurable log taper mapping, while base voice volume (`voice.vol`) remains fully owned by preset/morph/LFO/MIDI modulation. The mixer applies `slider_vol[i]` as a post-voice multiply for each voice block before routing, and slider zippering was reduced by per-block gain interpolation (`last_gain -> current_gain`). Build verified; hardware listening test reported substantial zipper reduction. The audit doc (`SLIDER_AUDIT.md`) was updated to reflect final implemented behavior and synced to `/Users/bc/Downloads/SLIDER_AUDIT.md`.
- **Find here**: `adcPots.c` slider path rewrite, decoupling from `voice.vol` and parameter system, mixer-stage per-voice multiplier, per-block gain interpolation, configurable `SLIDER_LOG_TAPER_DB`, updated audit checklist and architecture notes

### 021 — Audio Jack Detect Trace + ISR Integration (2026-05-16)
Session 021 completed rear audio jack-detect tracing and confirmed the physical mapping: OUT1L=PD6, OUT1R=PD7, OUT2L=PB4, OUT2R=PB6. The original Session 021 runtime integration was later superseded by Session 025: all four jack-detect pins are now retained state sampled by the 500Hz foreground service, and PD6/PD7 EXTI is masked. `mixer_checkOutJackAvailable()` still consumes cached availability variables (`l1_Available/r1_Available/l2_Available/r2_Available`).
- **Find here**: confirmed OUT jack-detect pin mapping, mixer cached jack-availability hook, hardware map and connector map updates; see Session 025 for current runtime behavior

### 022 — Dither Audit + 24-bit Path Widening (2026-05-16)
Full audit of dither in both LXR-master and our port: the only dither call (`calcDrumVoiceSyncBlock()`) is guarded by `#ifdef USE_AMP_FILTER` which is never defined in either Makefile — dither is entirely inactive in both codebases. DMA was sending `[int16 MSW, 0x0000 LSW]`, wasting the lower 8 bits of the 24-bit I2S frame. Introduced `sample_mx_t` (signed 24-bit value in int32_t) in new `Core/DSPAudio/sample_mix.h`. Widened render buffers, mixer summing/routing, BufferTools helpers, and codec packer (`pack_half()`) to carry true 24-bit data. Voice sync-block and distortion interfaces were prototyped wide then rolled back to `int16_t` (deferred; pinned in DITHER_AUDIT.md). Conversion from int16 voices to widened domain happens in mixer immediately before pan/sum (`bufferTool_convertInt16ToSampleMix`). Loudness regression found and fixed: extra `>>8` in `sampleMix_toS24()` removed. Original `dth` global menu option (short `dth`, long `16bitDth`, DTYPE_ON_OFF, default off, second-last before CPU widget) fully designed in DITHER_AUDIT.md Steps 1–8 but not yet wired — infrastructure now in place. Build verified clean.
- **Find here**: dither audit (why dither is inactive in both codebases), sample_mx_t numeric contract (int16<<8 convention), sampleMix_toS24 loudness fix (do not re-add >>8), deferred voice/distortion widening plan, dth global menu wiring plan Steps 1–8, bufferTool_convertInt16ToSampleMix as conversion boundary

### 023 — CPU Refactor + DSP Hot-Path Cleanup (2026-05-17)
Session 023 started with `AUDIT_REFACTOR.md`, then implemented the lowest-risk CPU wins and wrote `MEMORY_AUDIT.md`. Slider log taper now uses a 4096-entry LUT derived from `SLIDER_LOG_TAPER_DB`; idle filesystem polling is rate-limited; TIM6 was reduced to counters plus a 500Hz foreground service flag; TIM7 LCD drain is 5kHz/priority 7; main-loop DSP subblocks mask only low-priority service interrupts with BASEPRI; BufferTools uses packed saturating word helpers; mixer routing/slider/24-bit conversion work was fused; user-sample oscillator metadata is generation-cached; oscillator frequency setup is cached; oscillator interpolation is capped by a configurable active-target budget currently set to 2 and uses block renders; oscillator-only ITCM is enabled while filter/distortion ITCM remains disabled; global apply after load is amortized; `Load:[Samples ]` now runs sample install then loop append with one audio suspend/resume; modal load LCD transitions drain cleanly; and main encoder direction reversal clears sub-detent residue. `AUDIO_DMA_FRAMES` remains 96. A 128-frame experiment fails the current 4KB `.dma_nocache` MPU/linker window because the two audio DMA buffers alone consume 4096 bytes before the ADC DMA buffer.
- **Find here**: `AUDIT_REFACTOR.md` implementation status, `MEMORY_AUDIT.md` section sizes, TIM6 foreground service, TIM7 5kHz LCD drain, BASEPRI DSP guard, slider LUT, filesystem idle poll limit, oscillator interpolation budget, oscillator-only ITCM, combined sample/loop loader, LCD modal drain, encoder residue fix, `.dma_nocache` 4KB limit and 128-frame explanation

### 024 — Copy/Clear Fix + README/MEMORY Cleanup (2026-05-21)
Session 024 audited `Core/Menu/copyClearTools.c` against `knowledge_files/LXR-master/front/LxrAvr/Menu/copyClearTools.c`, wrote `COPYCLEAR_AUDIT.md`, and found that the direct `seq_*` copy/clear calls were mostly correct while the active regression was in the menu/control path. `menu_parseEncoder()` now lets clear mode own encoder turns (target select) and encoder clicks (execute clear) before normal edit-mode toggling. `buttonHandler.c` no longer exits `MODE_CLEAR` on SHIFT release while COPY is still held, so clear mode stays armed until both combo buttons are released. README was trimmed so content after the `# MOVE EVERYTHING AFTER THIS TO MEMORY.md` marker lives in MEMORY; historical/non-enabled items were removed from README's confirmed hardware list and folded into MEMORY without duplicating the full moved block. `make -j4` passed after the copy/clear code changes.
- **Find here**: `COPYCLEAR_AUDIT.md`, `menu_parseEncoder()` clear-mode ownership, SHIFT/COPY release ordering, README/MEMORY split, retained MEMORY details for known issues/critical reminders/toolchain

### 025 — SD/FAT, Globals Compatibility, CLK/RST + Jack Detect Cleanup (2026-05-23)
Session 025 fixed unsupported-card boot handling, audited and patched `glo.cfg`/`.all` global load semantics, removed obsolete global-menu/settings entries, corrected CLK/RST assumptions from hardware diagnostics, and stabilized OUT jack detect as retained foreground state. FAT12/exFAT now show `Unsupported card` / `use MBR-FAT32` and do not mount. Global settings remain raw/unversioned: 22-byte legacy globals load silently with compatibility defaults, 23-byte current globals load normally, and other lengths use safe fallback plus `check&save` warnings. CLK IN is PD4 rising edge, RST IN is PD5 rising edge/reset-to-pattern-start, and PD6/PD7 jack detect uses pull-ups plus 500Hz foreground polling with PB4/PB6.
- **Find here**: `FAT_AUDIT.md`, `SAVE_ALL_AUDIT.md`, `ST1_JACK_DET_AUDIT.md`, unsupported-card warning, 22/23/stale globals policy, `PAR_FETCH`/phantom-param removal, `menuPages.h` TEXT_EMPTY/PAR_NONE trap, PD4/PD5 rising-edge pull-up config, PD6/PD7 retained-state polling

### 026 — Load/Save Glitch Audit; Malformed File Name Fallthrough (2026-05-25)
Load/save button display glitch diagnosed: `menu_resetSaveParameters()` is called before `menu_activePage` is updated in `menu_switchPage()` `case LOAD_PAGE:`, causing `menu_repaintAll()` to fire on the old voice/seq page with `editModeActive=1` — a one-frame edit-mode flash indistinguishable from an encoder click. Fix is a two-line reorder documented in `LOAD_SAVE_GLITCH_ASSESSMENT.md` but NOT yet applied to `menu.c`. `filesystem_loadName_tick()` and `filesystem_loadKit_tick()` phase-2 zero-byte hang fixed: bare `n==0` does not mean EOF in asyncfatfs (buffer not ready); correct idiom is `n==0 && afatfs_feof(op_file)`, matching all 20+ other read phases in `filesystem.c`. Malformed or zero-byte files now store `"-       "` in `loaded_name`/`preset_currentName` and advance to close cleanly. Kit loads on malformed files abort with `FS_STATUS_ERROR` before touching `parameter_values[]`. Minor TODO noted: `PAR_EXT_SYNC` occupies the slot where `PAR_FETCH` lived in LXR037; cross-system file interchange may cause a parameter offset mismatch at that location.
- **Find here**: `LOAD_SAVE_GLITCH_ASSESSMENT.md`, `FILE_FALLTHROUGH_AUDIT.md`, menu_switchPage ordering fix, afatfs EOF idiom (`afatfs_feof` not bare `n==0`), filesystem_loadName_tick/-loadKit_tick phase-2 malformed-file fix, PAR_EXT_SYNC/PAR_FETCH slot conflict note

### 027 — Main-Loop Burst Reduction (2026-07-04)
Session 027 audited `BURST_REDUCTION.md` against live code, wrote `BURST_REDUCTION_AUDIT.md`, then implemented the low-risk part of the plan without changing `AUDIO_DMA_FRAMES`. The synchronous runtime sound-apply burst in `PRESET_OP_KIT_LOAD`, `PRESET_OP_ALL_LOAD`, and `PRESET_OP_PERFORMANCE_LOAD` is now chunked: `presetManager.c` factors the old six-voice modulation-destination apply into a one-voice helper plus `preset_startDrumsetApply()` / `preset_tickDrumsetApply()`, while `menu.c` owns operation-specific follow-up through `menu_startSoundApply()` / `menu_tickSoundApply()` / `menu_finishSoundApply()`. Boot-time behavior remains synchronous when `audioCodec_renderCount == 0`. `AUDIO_DMA_FRAMES` remains 96; 64-frame latency testing is explicitly deferred until this scheduling change is hardware-tested. `make clean`, `make`, and `make img` passed; user reported no obvious regression but no direct instrumentation/hardware proof of burst reduction.
- **Find here**: `BURST_REDUCTION_AUDIT.md`, chunked drumset/sound apply, `preset_tickDrumsetApply()`, `menu_tickSoundApply()`, kit/all/performance load completion scheduling, `AUDIO_DMA_FRAMES=96` retained, 64-frame test deferred

### 028 — frontPanelParser Removal and Scene/Pattern Split (2026-07-05)
Session 028 removed the obsolete single-CPU front-panel parser bridge instead of replacing it with another mediation layer. `Core/MIDI/frontPanelParser.c/h` were deleted; front-panel opcodes were replaced by direct owner APIs in Menu, buttonHandler, ledHandler, Preset, MidiParser, Sequencer, copy/clear, filesystem, and new PatternData. Pattern-owned data moved under new `Core/Scene/Pattern/` with `PatternData.c/h`; Euklid and SOM files moved there too. LED reverse feedback is now a `SeqLedState` dirty-byte payload drained by `led_processSeqLedState()` in the foreground main loop. Pattern/track/step/automation edits now enter through `pat_*` APIs; sound parameter writes enter through Preset; MIDI config writes enter through MidiParser; transport/mute/roll remain Sequencer-owned. A second pass added detailed comments explaining every new direct-call boundary and risk. `make && make img` passed after the functional removal; the comments-only pass passed `make`. Parser/protocol greps were clean in live code, with remaining hits only in documentation/comments.
- **Find here**: `REMOVE_FPP_AUDIT_2.md`, `MODULE_INTERCHANGE_SPEC.md`, `Core/Scene/Pattern/PatternData.c/h`, `led_processSeqLedState()`, direct `pat_*` APIs, deleted `Core/MIDI/frontPanelParser.c/h`

### 029 — Pattern Storage Ownership + Preset Folder Move (2026-07-06)
Session 029 completed `SCOPING_TARGETS.md` 1.3 and 1.4 as mechanical ownership/refactor work. Pattern storage fields/constants were renamed to `pat_*`/`PAT_*`; the transitional `seq_patternSet`, `seq_tmpPattern`, `seq_selectedStep`, `SEQ_DEFAULT_NOTE`, and `SEQ_NEXT_RANDOM*` names were eliminated from live code. Sequencer still owns timing, transport, quantization, runtime step indices, MIDI output, and recording gates, but storage reads/writes now go through PatternData APIs such as `pat_readStep()`, `pat_getEffectiveTrackLength()`, `pat_recordNote()`, `pat_eraseMainStepSubSteps()`, `pat_commitStagedPattern()`, and `pat_setAllShuffle()`. Filesystem shuffle imports now go through PatternData. Oscillator, MIDI, SOM, and Sequencer default-note/random-pattern references now use `PAT_DEFAULT_NOTE` / `PAT_NEXT_RANDOM`. `Core/Preset/` was moved intact to `Core/Scene/Preset/`, the Makefile include/source paths were updated, `main.c` now includes `ParameterArray.h` directly, and Preset public names intentionally stayed `preset_*`/`parameterArray_*`. New audits documented staging buffers/continuation state and global-parameter duplication, with future direction: one active-pattern temporary buffer until 17th Scene/background-bank-load design, leave new filesystem buffers alone, and eventually replace raw globals with scene/bank/system settings structs. `make`, `make img`, stale-path greps, and `git diff --check` passed.
- **Find here**: `029_SESSION_HANDOFF_LOG.md`, `Core/Scene/Pattern/PatternData.c/h`, `Core/Sequencer/sequencer.c`, `Core/Hardware/SD/filesystem.c`, `Core/Scene/Preset/`, `STAGING_AUDIT.md`, `GLOBALS_STAGING_AUDIT.md`

### 030 — Phase 2 Filesystem Start + Directory Kit Load (2026-07-07)
Session 030 began Phase 2 by writing the initial filesystem spec, updating `SCOPING_TARGETS.md` to the current SD hierarchy, generating `SD_CARD/Kit/` from legacy `Pxxx.SND`, and implementing root `Kit/` directory kit loading. New `Core/Hardware/SD/storageTypes.c/h` owned text-format parsing, numbered folder parsing, kitset/instrument validation, and the then-current instrument-key-to-`ParameterArray` mapping; Session 032 superseded that last piece with descriptor-key-to-Scene-storage writes. `filesystem.c` now scans `Kit/` folders, caches slot presence/display/open names, loads `kitset.kcg` plus six instrument files for normal kits, and keeps morph kit load on the legacy `.SND` path until morph save/instrument morph data is designed. `asyncfatfs` now sets opened handle type from the FAT directory entry so directories can be entered reliably. Menu kit load from the Load page displays one-based slot numbers and cached names. Folder naming was revised to preferred `NNN Name` with `_` accepted for compatibility; a short-alias fallback handles FAT aliases like `001SLA~1`. The generated converter and `SD_CARD/Kit` tree now use space-named folders such as `004 Moch to`. A late pass added detailed comments to all touched code paths per the new session standard. `make` and `git diff --check` passed; hardware reported menu/init load working after discovery fixes, with final short-alias fallback not yet separately re-tested in hardware.
- **Find here**: `030_SESSION_HANDOFF_LOG.md`, `knowledge_files/specification_reference/FILESYSTEM_SPEC.md`, `KIT_DIR_LOAD_AUDIT.md`, `Core/Hardware/SD/storageTypes.c/h`, `Core/Hardware/SD/filesystem.c`, `Core/Hardware/SD/asyncfatfs/asyncfatfs.c`, `tools/convert_legacy_kits.py`, `SD_CARD/Kit/`

### 031 — One-Pattern/8-Bar Bridge + Supplementary Pattern Features (2026-07-09)
Session 031 completed the bridge pass toward one live 128-step pattern: `NUM_PATTERN` is now 1, `NUM_BARS` is 8, `NUM_STEPS_PER_BAR` is 16, while pattern/container files still stream the old 8-slot bridge layout. Sequencer timing was slowed to the corrected default rate with a 96-PPQ master step clock, per-track scale ratios (`/8..x8` including `/25`), per-track shuffle, and PERF SELECT1 realign for the active pattern. STEP mode gained a front-page/track-settings page that refreshes immediately on entry and track change, shows length, scale, MIDI channel, MIDI note, and toggles to a second half for per-track shuffle; empty boot tracks default to 16 steps. LED flash was converted into a group-aware overlay with 16-bit masks, per-group cancellation/restore, preserved 400/80 timing, and SELECT sub-page retention through BAR flash; full LED layer consolidation is now part of the Phase 5 UI/performance cleanup. Directory kit loading was fixed and the legacy kit converter was rebuilt from the Slak canary: `kitset.kcg` is reduced, instrument files emit `[params]` and `[morph]`, comments are not authoritative, `audio_out` values are preserved in kitset (`0=stereo1`, `1=stereo2`, `2=L1`, etc.), and `SD_CARD/Kit` was regenerated from the current `P*.SND` set. Supplementary features added persistent `SHIFT+VOICE` morph endpoint edit mode using `parameters2[]` with a blinking VOICE mode LED, stopped selected-voice re-press preview through `seq_previewVoice()`, and PatternData-owned per-track shuffle. The old single/global shuffle byte is ignored and no longer imported/exported; future storage migration remains an external Python-converter job once Phase 2 storage stabilizes. `make`, `make img`, and `git diff --check` passed; final UI behavior still needs hardware confirmation.
- **Find here**: `031_SESSION_HANDOFF_LOG.md`, `PAT_8BAR_SINGLE_AUDIT.md`, `PAT_SUPPLEMENTARY_FEATURES_AUDIT.md`, `Core/Scene/Pattern/PatternData.c/h`, `Core/Sequencer/sequencer.c/h`, `Core/Menu/menu.c/h`, `Core/Hardware/frontPanel/buttonHandler.c`, `Core/Hardware/frontPanel/ledHandler.c`, `Core/Hardware/SD/filesystem.c`, `tools/convert_legacy_kits.py`, `SD_CARD/Kit/`

### 032 — Instrument Parameter Refactor Follow-up + Spec Consolidation (2026-07-10)
Session 032 followed the instrument-parameter refactor through the live boot path. Voice pages are now populated from descriptor-owned menu layouts in `Core/DSP/Instruments/*/*Parameters.c` instead of hard-coded legacy VOICE cells in `menuPages.h`; the layouts were checked against `menuPages.old`, changed from raw numeric descriptor indexes to instrument-local enum names, and documented next to their row/bind/flag macros. Directory kit loading now writes descriptor-indexed Scene storage, including the 001 Slak kit, and the runtime apply path uses `InstrumentManager` descriptor bindings and special shapers for parameters that cannot be written through a simple struct offset. `storageTypes.c` grew a 32-byte instrument key buffer after the Slak audit found `amp_envelope_decay_closed/open` exceeded the old key length. `instrument_decimation` and `velo_mod_amount` are now `ROW_NOBIND_IMAGE` parameters: morphable, modulatable, and automatable image values with no direct member-offset binding. The Session 032 instrument-file spec was later folded completely into `knowledge_files/specification_reference/FILESYSTEM_SPEC.md`; the separate `INSTRUMENT_FILE_SPEC.md` file was deleted. Hardware reported that boot/menu/audio mostly works after this pass, but descriptor Morph does not work and LFO/velocity modulation plus step automation assignments do not yet affect descriptor-backed parameters.
- **Find here**: `032_SESSION_HANDOFF_LOG.md`, `knowledge_files/specification_reference/FILESYSTEM_SPEC.md`, `Core/DSP/Instruments/InstrumentManager.c/h`, `Core/DSP/Instruments/Drum/DrumParameters.c`, `Core/DSP/Instruments/Snare/SnareParameters.c`, `Core/DSP/Instruments/Cymbal/CymbalParameters.c`, `Core/DSP/Instruments/HiHat/HiHatParameters.c`, `Core/Menu/menu.c`, `Core/Hardware/SD/storageTypes.c/h`, `SD_CARD/Kit/001 Slak/`

### 033 — Phase 3 Instrument Runtime, LFO, Morph, and Scene Mod Targets (2026-07-11)
Session 033 fixed the runtime pieces exposed by Session 032. Descriptor menu labels now render exact padded strings instead of reading past NUL terminators, LFO/velocity target pickers enumerate active descriptors dynamically with one `off`, and VOICE sub-pages can expose 16 cells as four-cell screens. LFOs now support two destination pairs, shared polarity (`neg/pos/bi`), Scene destination `scn`, descriptor/Scene runtime apply, and plain target labels without redundant voice prefixes. Descriptor Morph now routes through the PERF/menu/MIDI path correctly at 0..255, and Morph is split into global set-all plus six Scene-retained per-voice values. `Core/Scene/SceneModTargets.c/h` introduces Scene sound targets (`1vm..6vm`, then Scene `srt`) for velocity and LFO modulation. Velocity Morph modulation retained-sets the visible per-voice value; LFO Morph modulation is a hidden overlay centered on the retained value and serviced by the Morph worker. Root audit docs from the session were consolidated into `FILESYSTEM_SPEC.md`, `MODULE_INTERCHANGE_SPEC.md`, `SCOPING_TARGETS.md`, `MEMORY.md`, and this handoff. Remaining gaps are descriptor-aware step automation and the Phase 3 file/save hierarchy.
- **Find here**: `033_SESSION_HANDOFF_LOG.md`, `knowledge_files/specification_reference/FILESYSTEM_SPEC.md`, `knowledge_files/specification_reference/MODULE_INTERCHANGE_SPEC.md`, `Core/Scene/SceneModTargets.c/h`, `Core/Scene/SceneData.c/h`, `Core/Scene/Preset/presetMorphEngine.c/h`, `Core/Scene/Preset/presetManager.c/h`, `Core/DSP/Instruments/InstrumentManager.c/h`, `Core/DSPAudio/lfo.c/h`, `Core/DSPAudio/modulationNode.c/h`, `Core/Menu/menu.c`, `Core/Menu/MenuText.h`, instrument `*Parameters.c/h` files

### 034 — Instrument Load Completion and Transactional Runtime Commit (2026-07-12)
Session 034 completed the first usable Instrument Load workflow. Instrument registry flags now express Basic/Advanced/Choke loading policy; HiHat uses canonical closed/choke decay keys, while non-Choke slot 6 receives a generated Scene-owned track-7 alternate decay. Root `Instrument/` scanning/loading and the nested Load UI were completed and refined: Kit and Instrument load use Scene LEDs correctly, unsupported legacy load/save types were removed, Instrument entry retains the kit-member display until lower-row browsing, and stopped repeated destination presses preview the voice. A live Voice 2 long-decay investigation isolated a one-slot lifecycle problem: individual Instrument loads left stale modulation/runtime state while a full Kit load repaired it. The fix stages Instrument files outside live SceneData, commits only after validation, clears all outgoing modulation owners before type replacement, resets the incoming runtime, rebuilds all six Morph/runtime images, normalizes/rebinds all six source target relationships, and locks destination-changing UI input until the transaction completes. The separate descriptor-aware Pattern automation migration and raw runtime-float modulation semantics remain follow-up work.
- **Find here**: `034_SESSION_HANDOFF_LOG.md`, `FILESYSTEM_SPEC.md`, `MODULE_INTERCHANGE_SPEC.md`, `SCOPING_TARGETS.md`, `MEMORY.md`, `Core/Hardware/SD/filesystem.c/h`, `Core/Scene/Preset/presetManager.c/h`, `Core/DSP/Instruments/InstrumentManager.c/h`, `Core/Menu/menu.c/h`, `Core/Hardware/frontPanel/buttonHandler.c`

### 035 — Kit Save, Morph Load, LFO Self, and Descriptor-Domain LFOs (2026-07-13)
Session 035 completed the next Phase 3 filesystem/runtime pass, but Session 036 later corrected its filesystem assumptions. Kit Morph Load parses normal Kit directories and copies source normal endpoint values into resident morph endpoints only for same-type slots; Instrument Morph Load is available only for the destination slot's current type and applies the same no-change mismatch rule. Instrument files support storage-only LFO target voice `self`, resolved on load to the destination slot and emitted on Kit Save when an LFO voice target points at the saved instrument's own slot. Descriptor LFO modulation uses descriptor-owned parameter-domain metadata plus InstrumentManager adapters, so temporary values go through the normal owner runtime writer and negative polarity matches original LXR value-relative math. The Session 035 Kit Save implementation exposed that asyncfatfs could create long-name directories without reliably reopening and populating them. Session 036 replaced that foundation with LFN/case-aware asyncfatfs APIs, direct `000..999` library slots with `000` real, and restored Kit Save on those APIs.
- **Find here**: `035_SESSION_HANDOFF_LOG.md`, `FILESYSTEM_SPEC.md`, `MODULE_INTERCHANGE_SPEC.md`, `SCOPING_TARGETS.md`, `MEMORY.md`, `Core/Hardware/SD/filesystem.c/h`, `Core/Hardware/SD/storageTypes.c/h`, `Core/Scene/SceneData.c/h`, `Core/Scene/Preset/presetManager.c/h`, `Core/DSP/Instruments/InstrumentManager.c/h`, `Core/DSPAudio/modulationNode.c/h`, `Core/DSPAudio/lfo.c`, `Core/Menu/menu.c/h`

### 036 — asyncfatfs LFN/Case Expansion, Kit/Instrument Load-Save Restoration (2026-07-14)
Session 036 rebuilt the save/load filesystem foundation after Kit Save exposed incomplete long-name support. asyncfatfs now preserves short-name case bits, creates VFAT LFN file and directory entries, exposes case-sensitive LFN open/create helpers with returned 8.3 aliases, and provides an LFN-aware object iterator used by generic diagnostics and restored production Kit/Instrument scans. Temporary `Load:[File]`, `Load:[Dir]`, `Save:[File]`, and `Save:[Dir]` diagnostics proved exact-case root scans, reads, writes, directory creation, dot-prefixed object visibility, and flush persistence. Kit Save was repaired to create visible `Kit/NNN Name/` folders and mixed-case member files, write `kitset.kcg` after aliases are known, update scan caches, and avoid duplicate visible folders on occupied-slot saves. Numbered library slots are now direct `000..999`; `000` is real for all filetypes, while instrument file voice coordinates remain one-based `1..6`. The Load/Save type cycler now deliberately exposes only File, Dir, and Kit; root Instrument Save was added from the Save page VOICE gesture using the same descriptor-keyed text writer as Kit member files. Morph Save and Scene Load/Save must be redone deliberately on this filesystem foundation before Bank work begins.
- **Find here**: `036_SESSION_HANDOFF_LOG.md`, `FILESYSTEM_SPEC.md`, `MODULE_INTERCHANGE_SPEC.md`, `SCOPING_TARGETS.md`, `MEMORY.md`, `Core/Hardware/SD/asyncfatfs/`, `Core/Hardware/SD/filesystem.c/h`, `Core/Hardware/SD/storageTypes.c/h`, `Core/Hardware/SD/kitBrowser.h`, `Core/Menu/menu.c/h`, `Core/Scene/Preset/presetManager.c/h`, `Core/Hardware/frontPanel/buttonHandler.c`

---

## Key Cross-Session Facts (quick lookup)

| Topic | Canonical session |
|-------|------------------|
| EXTI_IMR must be first in main() | 001 |
| SD card pins: PC12/PD2/PC8/PD0 (NOT SPI1, PA8 was false positive) | 006 |
| Hardware SPI for SD impossible — all SPI peripherals taken or unbonded | 010 |
| TIM7 LCD SPSC ring — no shared counter | 006 |
| Encoder: TIM1 IC, ICxF=0xF, Dannegger, last=new&3, round-toward-zero divide | 004, 007 |
| ts_dirs[] rebound suppression | 004 |
| (new+3)&3 seed — forbidden, tried and discarded | 004 |
| val>>=2 — forbidden, asymmetric floor divide | 007 |
| while→if in buttonHandler_processEvents — intentional | 005 |
| int16 saturating add in menu boundary checks | 006 |
| TIM7 idle gating — forbidden, wakeup race | 006 |
| Broad repaint coalescing — forbidden | 006 |
| menu_knobs_dirty / serviceKnobRepaint — RV1-4 only | 006 |
| VLAs in DSP files — forbidden, silent stack corruption | 008 |
| RCC_AHB2ENR = 0x40023834 (NOT 0x40023830) | 008, 009 |
| RNG_CR direct write (NOT \|=) | 008 |
| GetRngValue() returns int16_t, cast at every call site | 008 |
| LFO noise: divide by 32767.0f (NOT 0xffffffff) | 008 |
| 78 startup underruns — normal, fixed count | 008 |
| audioCodec_init() — single hardware entry point | 009 |
| DTCM not DMA-accessible | 007 |
| Flash sectors 6-11 sample region, erase floor at sector 6 | 007 |
| .SND remains byte-compatible; `glo.cfg` current raw span is 23 bytes with explicit legacy-22 compatibility and stale fallback | 007, 025 |
| plli2s_init HSERDY guard never entered — intentional | 009 |
| AUDIO_DMA_FRAMES=96 gives the hardware render-slot budget 2.18ms; OUTPUT_DMA_SIZE=32 is the canonical DSP/control block (corrected in Session 019; was erroneously 16) | 010, 017, 019 |
| Runtime kit/all/performance load completion must use chunked sound apply (`menu_startSoundApply()` → `preset_tickDrumsetApply()`); keep boot-time pre-audio apply synchronous | 027 |
| DSP render must stay in main loop — ISR ceiling is fatal, no graceful degradation | 010 |
| SD blocking in main loop is root cause of post-kit-load underruns | 010 |
| SD non-blocking FSM originally planned for TIM5 ISR priority 6 — TIM2 reserved for BPM/MIDI | 010 |
| TIM6_DAC_IRQHandler name is a vector table artifact — internal DAC NOT in use | 010 |
| PA4/PA5 are slider ADC inputs — internal DAC on same pins must never be enabled | 010 |
| BAR1/BAR2 processPress() calls voiceControl_noteOn/Off() — latent DSP race | 010 |
| encode_read4 moving to TIM6: cpsid/cpsie must become shadow copy — TIM1 preempts TIM6 | 010 |
| asyncfatfs adopted — replaces ChaN FatFS entirely | 011 |
| SD cards: FAT16/FAT32 supported; MBR-FAT32 recommended; FAT12/exFAT unsupported and boot warning is `Unsupported card` / `use MBR-FAT32` | 025 |
| NB_FatFS rejected — C++, heap, no FAT16, no _FS_TINY | 011 |
| FatFS fork for async rejected — 20+ functions, 28 yield points, high risk | 011 |
| Original LXR: AVR owns SD for presets, STM32 only uses SD for sample upload (halts audio) | 011 |
| asyncfatfs main-loop polling first, TIM5 ISR migration path preserved | 011 |
| sdcard_lxr02.c shim: 16 bytes/burst, state machine over spi_sd.c bit-bang | 011 |
| Storage FSM is single-operation; Session 17 moved the public boundary to filesystem.c/h | 011, 017 |
| asyncfatfs RAM budget: ~5.6KB total (cache + files + state) | 011 |
| SD_init() from sd_routines.c still needed at boot before afatfs_init() | 011 |
| Byte-by-byte f_write loops eliminated — bulk write via staging buffer | 011 |
| afatfs_poll() must be called from ONE context only — never both main loop and ISR | 011 |
| asyncfatfs implemented — ChaN FatFS removed from build | 012 |
| sdcard_lxr02.c sends CMD17/CMD24 directly — do NOT use SD_sendCommand() (deasserts CS) | 012 |
| Storage requests are single-operation — do not post two filesystem requests without waiting for completion/ack | 012, 017 |
| preset_morph() must skip index 127 — uint16 underflow in midiParser_ccHandler → wild write | 012 |
| HiHat VLA fix: mod1/mod2 are static arrays in HiHat_calcSyncBlock — do not revert to VLAs | 012 |
| preset_status_t polling: menu_pollPresetStatus() handles all post-load/save work | 012 |
| Boot sequence: all SD ops complete BEFORE audioCodec_init() — do not reorder | 012 |
| afatfs_fread clamps to file size internally (line 3108) — filesystem streaming code still checks fread==0 for EOF | 012, 017 |
| Obsolete ChaN FatFS and sdTest files were removed from active tree; do not re-add ff.c/diskio.c/sdTest | 012, 017 |
| .SND files may be shorter than END_OF_SOUND_PARAMETERS — original LXR kits vary (229–236 bytes) | 012 |
| No hi-hat at startup — boot kit masks HiHat path, separate DSP init issue, not yet fixed | 012 |
| I-Cache + D-Cache enabled in sysclk_init() — MPU required for DMA coherency | 013 |
| MPU Region 0: 1MB WT at 0x20000000 (all SRAM); Region 1: 4KB SO at 0x20020000 (DMA buffers) | 013 |
| .dma_nocache linker section: NOLOAD at start of SRAM1, 4KB max, Strongly-Ordered via MPU | 013 |
| audioOutBuffer/audioOutBuffer2 in DTCM (INDTCMZ) — single-cycle access for render hot path | 013 |
| -Ofast for Core/DSPAudio/*.c — separate Makefile rule via CFLAGS_DSP | 013 |
| preset_sendModTarget() replaces frontPanel_sendData for CC_VELO_TARGET/CC_LFO_TARGET in kit load | 013 |
| PAR_VOICE_LFO/PAR_TARGET_LFO parameterArray entries now populated → &parameter_values[] | 013 |
| Instrument file schema, Scene descriptor storage, dynamic VOICE pages, and DSP propagation are specified in `knowledge_files/specification_reference/FILESYSTEM_SPEC.md`; the separate `INSTRUMENT_FILE_SPEC.md` was deleted after being folded in | 032, 033 |
| VOICE instrument pages are descriptor layouts from `Core/DSP/Instruments/*/*Parameters.c`, not static `menuPages.h` cells | 032 |
| `instrument_decimation` and `velo_mod_amount` are `ROW_NOBIND_IMAGE`: morphable/modulatable/automatable image values with no direct struct-offset runtime bind | 032 |
| Descriptor Morph was broken after Session 032 and fixed in Session 033; Morph now runs per voice from Scene-owned descriptor images | 032, 033 |
| LFO/velocity target assignment storage/display existed after Session 032; Session 033 added descriptor/Scene runtime modulation, while step automation still needs descriptor-aware `AutomationNode` work | 032, 033 |
| Scene modulation targets live in `Core/Scene/SceneModTargets.c/h`: initial order is `1vm..6vm`, then Scene `srt`; future FX targets join this namespace | 033 |
| Instrument parser keys allow at least 32 bytes; HiHat canonical keys are `amp_envelope_decay` and `amp_envelope_decay_choke`, while storage accepts legacy closed/open aliases | 032, 034 |
| Instrument registry metadata is firmware-only: Drum/Snare Basic, Cymbal Advanced, HiHat Advanced|Choke; a replacement may not take a Kit above two Advanced types | 034 |
| Root Instrument Load validates into private staging; active commit clears all modulation owners, replaces/resets the slot, rebuilds all six runtime/Morph images, then normalizes and rebinds all sources while UI target switches are locked | 034 |
| Track 7 resolves a Choke sibling `<base>_choke` for a Choke slot 6; non-Choke slot 6 gets generated Scene-owned `slot6_track7_amp_envelope_decay`/morph storage and Scene target `7dc` when it has a base decay | 034 |
| Kit/Instrument Morph Load copies source normal endpoints into destination morph endpoints only for matching instrument types; mismatched slots are no-change | 035 |
| LFO target voice `self` is storage-only, accepted/emitted only for `lfo_target_voice` and `_2`; it resolves immediately to the destination one-based slot and is never stored as a parameter value | 035 |
| Descriptor LFO targets use InstrumentManager parameter-domain adapters and normal descriptor runtime writers; negative polarity is original-LXR `base * (1 - amount + amount * lfo)` in parameter space | 035 |
| Numbered library slots are direct `000..999`; slot `000` is real for all filetypes. Instrument file voice numbers remain a separate one-based `1..6` schema coordinate | 036 |
| Normal `Save:[Kit     ]` writes directory Kit format: `Kit/<NNN Name>/kitset.kcg` plus six visible instrument files with `[params]` and `[morph]`; `kitset.kcg` stores asyncfatfs-returned 8.3 aliases for member opens | 035, 036 |
| asyncfatfs now supports case-preserving SFN display, VFAT LFN create/open for files and directories, case-sensitive LFN matching, object iteration, and returned short aliases; atomic rename/replace and recursive directory replace remain missing primitives | 036 |
| Dot-prefixed files/directories are real filesystem objects and must not be hidden by asyncfatfs. Product scanners may filter by product naming/type rules, but the filesystem layer and File/Dir diagnostics do not suppress ordinary `.` names | 036 |
| Top-level Load/Save cycling is currently whitelisted to File, Dir, and Kit. Scene, Settings, Samples, KitMrp, and legacy containers remain compiled but gated until each is retested/promoted deliberately | 036 |
| Root Instrument Save is implemented from nested Save-page VOICE mode and writes one resident voice to `Instrument/<stem.ext>` using the same descriptor-keyed writer as Kit member files | 036 |
| Morph Save and Scene Load/Save must be deliberately redone on the Session 036 asyncfatfs foundation before starting Bank implementation | 036 |
| Pattern step destinations use canonical 16-bit IDs, but `AutomationNode` is still legacy byte CC/CC2; dynamic modulation-node enumeration also remains follow-up runtime work | 034, 035 |
| ResonantFilter.c double literals (0.5*, 1.0-) on lines 141/167 — software emulation in hot loop | 013 |
| Kit save writes canonical 236 bytes (8-byte name + END_OF_SOUND_PARAMETERS sound bytes); short kit loads zero-fill missing sound bytes | 013, 017 |
| Sequencer sources imported from original LXR: sequencer + EuklidGenerator + SomData + SomGenerator; sequencer_.c retained as legacy reference | 014 |
| frontPanelParser.c/h deleted; protocol opcodes replaced by direct owner APIs and `Core/Scene/Pattern/PatternData.c/h` | 028 |
| `led_processSeqLedState()` drains Sequencer LED dirty flags in the foreground main loop; do not move it to TIM3 without auditing Menu/button/LED state access | 028 |
| Pattern API boundary: UI/copy/filesystem/generator code should call `pat_*`; Sequencer no longer exposes `seq_patternSet`/`seq_tmpPattern`/`seq_selectedStep` compatibility names in live code | 028, 029 |
| `Core/Preset` moved to `Core/Scene/Preset`; public names remain `preset_*`, `parameterArray_*`, `paramArray_*`, and `parameter_values[]`/`parameters2[]` still live in Menu until the later instrument/file redesign | 029 |
| Active-pattern load staging buffer (`pat_tmpPattern`) stays for now and should come out with the 17th Scene/background-bank-load design; it should be the only necessary temporary pattern storage | 029 |
| Future globals direction: replace raw duplicated globals with canonical settings structs split scene-level, bank-level, and system-level; exact membership TBD during Scene/file redesign | 029 |
| Phase 2 root SD layout and current Kit/instrument file state are documented in `knowledge_files/specification_reference/FILESYSTEM_SPEC.md`: `Bank`, `Scene`, `Kit`, `Pattern`, `Sample`, `Wavetable`, `Effect`, `Instrument`, and root `settings.cfg` | 030, 032 |
| Root `Kit/` load is now directory-based for normal kits: scan `Kit/NNN Name`, load `kitset.kcg` plus six instrument files; morph load remains legacy `.SND` for now | 030 |
| Numbered folders prefer `NNN Name`; loaders may accept `NNN_Name`; Kit scan also has a FAT short-alias fallback for names like `001SLA~1` | 030 |
| `storageTypes.c/h` owns Phase 2 kit text schemas and parameter maps; all functions in that layer must use the `storage_` prefix | 030 |
| New code must be commented at detailed contract level: why it exists, what it does, inputs/outputs, and clients/accessors/affiliates | 030 |
| Live bridge pattern count is one (`NUM_PATTERN=1`), while pattern/container files still stream the old 8-slot bridge layout | 031 |
| STEP front-page track settings: length, scale, MIDI channel, MIDI note; second half contains per-track shuffle | 031 |
| Empty boot patterns default all tracks to 16 steps, not 128 | 031 |
| Per-track shuffle is PatternData-owned; the old single/global shuffle byte is ignored and omitted in Phase 2 bridge storage | 031 |
| Track scale uses exact ratios on a 96-PPQ master step clock, including `/25`, and realigns from the master clock to prevent drift | 031 |
| `SHIFT+VOICE` began as morph endpoint edit mode using `parameters2[]`; after Session 032 descriptor-backed instrument pages need Scene image morph handling instead | 031, 032 |
| Stopped selected-voice re-press previews voice through `seq_previewVoice()` without advancing pattern state | 031 |
| LED flash is a group overlay with 16-bit masks, per-group cancel/restore, and retained 400/80 timing; final LED layer consolidation now belongs with the Phase 5 UI/performance cleanup | 031 |
| Kit converter uses direct `Pxxx.SND` payload-to-`ParameterArray` mapping; source comments are not authoritative, and `audio_out` lives in `kitset.kcg` | 031 |
| Button/menu and LED audit closures are documented in BUTTONHANDLER_MENU_AUDIT_RESULTS.md and LED_AUDIT_SUMMARY.md | 014 |
| Reverse sequencer SEQ_CC feedback must use seq_notifyFront(), not frontPanel_sendData(), due direction-colliding command values | 015 |
| SeqLedState drains sequencer LED events in main loop; do not move consumer to ISR without race audit | 015 |
| seq_init() is required at boot; otherwise sub-step defaults/probability/volume stay zeroed BSS and sequencer is silent | 015 |
| BUTTON_TIMEOUT is 500ms on this port; original AVR value 38 meant 38 × 13.107ms | 015 |
| systick_ticks is the canonical 4kHz mainboard tick again; UI/service millisecond work uses time_sysTick from TIM6 | 015, 017 |
| EuklidGenerator.c matches original; PATGEN distribution depends on __CLZ(0) returning 32 | 015 |
| Euclid/SOM/copyClear backend paths wired in Session 15; trigger backend remains stubbed | 015 |
| preset_morph() is a rate-limited front-panel CC dump; no skip cache, full final pass required | 016 |
| Morph-kit load writes parameters2[] without overwriting active kit; morph-kit save writes interpolated values except mod-target ranges | 016, 017 |
| Morph skips index 127 and mod-target ranges; mod targets are not morphed | 016 |
| preset_sendModTarget() CC_VELO_TARGET fallthrough fixed; do not remove the break | 016 |
| RV1-RV4 endless pots use raw A/B deadzone baselines; page changes call snapshotAll | 016 |
| PAR_MORPH only gets endless-pot double speed; do not apply to all DTYPE_0B255 | 016 |
| ENDLESS_POT_DEADZONE=20, ENDLESS_POT_TIMEOUT_MS=5000, ENDLESS_POT_DELTA_TIMEOUT_MS=20 | 016 |
| Non-SD client code should include Core/Hardware/SD/filesystem.h only; asyncfatfs/SPI/raw SD details are private | 017 |
| SD layout: Core/Hardware/SD/filesystem.c/h facade, kitBrowser kit-only, SPI/ for bit-bang transport, asyncfatfs/ for FAT/block shim | 017 |
| Filesystem filetype registry owns .snd/.pat/.prf/.all/glo.cfg extensions and add-a-filetype documentation | 017 |
| `glo.cfg`/ALL globals policy: no versioning; 22-byte legacy accepted with defaults, 23-byte current accepted, other lengths safe-prefix/default fallback + `check&save` warning | 025 |
| Pattern/performance/all load/save use async streaming state machines; do not stage large files in RAM | 017 |
| .prf/.all format: name[8], version=2, 64-byte meta, 512-byte kit block, pattern payload without second .pat name | 017 |
| Pattern/performance/all loads show "Loading pattern", lock page changes while busy, then repaint load screen with cursor on file number | 017 |
| Empty pattern/performance/all load slots are canonical no-ops: OK on Empty does not enter storage-busy UI | 017 |
| Typed async name completions are tagged by save type/slot; stale completions from fast encoder spins are ignored | 017 |
| Menu display repaints preflight lcd_queueFree() so cursor/data half-pairs cannot be partially dropped under LCD queue pressure | 017 |
| CPU-use widget is a DWT CYCCNT event-based audio queue-free pressure meter, not generic MCU utilization | 017 |
| Global menu page 4 now shows read-only cpu/CPU use time; Trigger Out2 PPQ and Trigger Gate Mode were removed from the displayed page | 017 |
| `menuPages.h` globals rows are fragile: early `TEXT_EMPTY`/`PAR_NONE` can block later entries, so reordered globals need manual table verification | 025 |
| PERF LEDs: solid SELECT LED is currently playing pattern; blinking SELECT LED is queued/viewed next pattern until ACK | 017 |
| VOICE SELECT LED must be restored to menu_getSubPage() after switching voices | 017 |
| Boot splash text is "Sonic Potions" / "LXR Drums V0.37"; screensaver uses explicit lcd_turnOff/lcd_turnOn and clears on exit | 017 |
| knowledge_files/LXR-master is read-only reference material; do not modify it | 018 |
| App flash ends at 0x0807FFFF; sectors 6-11, 0x08080000-0x081FFFFF, are sample storage | 018 |
| sampleFlash.c must reject erase/write below sector 6 | 018 |
| Load:[Samples ] wipes and reinstalls accepted WAVs from /samples | 018 |
| Load:[SampLoop] appends looped accepted WAVs from /loops and preserves existing samples | 018 |
| Sample WAV support is mono PCM 16-bit 44.1kHz; unsupported files are silently skipped | 018 |
| Sample order uses full long filename when present, lexicographic with ASCII case folded; not natural numeric sort | 018 |
| Sample display name preserves case and compresses long stems as first4 + CGRAM char 0x00 + last3 | 018 |
| SampleInfo.size is uint32_t metadata, high bit is loop flag, lower 31 bits are frame count | 018 |
| LONG SAMPLE PLAYBACK NOT FINISHED: Oscillator.c still indexes with legacy phase >> 17 path | 018 |
| Modal sample install must suspend audio and stop sequencer before flash writes, then resume audio | 018 |
| AUDIO_DMA_FRAMES=96 gives the hardware render-slot budget 2.18ms; OUTPUT_DMA_SIZE=32 is the canonical DSP/control block (corrected from 16 in Session 019) | 010, 017, 019 |
| Runtime kit/all/performance load completion must use chunked sound apply (`menu_startSoundApply()` → `preset_tickDrumsetApply()`); keep boot-time pre-audio apply synchronous | 027 |
| TIM3 owns sequencer at 4 kHz (IRQ29, priority 2): processRealtimeEvents → triggerJacks_tick → seq_tick; do NOT add these back to main loop | 019 |
| TIM2 is a shared free-running 1 µs timestamp counter (PSC=107); do NOT reset on pulse; use unsigned delta subtraction | 019 |
| MidiRealtime.c/h: 32-entry timestamped SPSC ring for MIDI_CLOCK/START/CONTINUE/STOP; push in USART3/USB ISR, pop in TIM3 | 019 |
| Voice trigger ring: 32-entry VoiceTriggerEvent SPSC; all noteOn/Off enqueue; voiceControl_processPending() drained at audio boundary | 019 |
| Jack event ring: 16-entry; push in EXTI4/EXTI9_5, pop in TIM3; CLK IN = PD4 EXTI4 rising edge, RST IN = PD5 EXTI5 rising edge | 019, 025 |
| PAR_EXT_SYNC (SyncInpt): values off/usb/din/pls/aut; PAR_BPM minimum is now 1 — value 0 no longer means external sync | 019 |
| AUTO sync priority: jack > DIN MIDI (500 ms hold) > USB MIDI > internal free-run | 019 |
| CC1 on global MIDI channel targets Morph; future descriptor/per-voice morph conversion is 0..126 → value * 2, 127 → 255 because storage/menu Morph is 0..255 while MIDI CC is 7-bit | 019, 032 |
| BAR1/BAR2 call midiParser_playVoiceMidiNote(voice, vel); notes record through MIDI path; do NOT revert to direct voiceControl calls | 019 |
| Default voice MIDI notes: Drum1=36 … Drum7=42; overridden by CC2_MIDI_NOTE per-voice | 019 |
| RST IN semantics: PD5 GPIO input pull-up, EXTI5 rising edge, resets to pattern start without toggling transport | 025 |
| DIN TX FIFO is dual (realtime priority + normal); TXE interrupt driven; uart_sendMidiByte() is non-blocking | 019 |
| EXTI4 = IRQ10 (CLK IN PD4); EXTI9_5 = IRQ23 (RST IN PD5); USART3 = IRQ39; TIM3 = IRQ29 — all now in startup vector table | 019 |
| Do NOT call seq_tick(), midiParser_processRealtimeEvents(), or triggerJacks_tick() from main loop — TIM3 owns them | 019 |
| DIN TX FIFO inserts and USB MIDI writes from foreground must use short critical sections to avoid TIM3 interleave | 019 |
| RV5-RV10 sliders are not base volume params; they feed `slider_vol[]` and are applied as mixer-stage multipliers after voice synthesis | 020 |
| Slider gain is always refreshed from ADC DMA in `adc_checkPots()` (no hysteresis gate in audio path) | 020 |
| Slider zipper reduction path: per-block gain interpolation in mixer + optional log taper in `slider_raw_to_float()` | 020 |
| `SLIDER_LOG_TAPER_DB` controls taper depth (0=linear; higher=more audio taper). Session 020 default: 60 dB | 020 |
| OUT1L/OUT1R/OUT2L/OUT2R jack-detect mapping is PD6/PD7/PB4/PB6; no plug=LOW (GND), plug inserted=HIGH | 021, 025 |
| All four jack-detect pins are retained state sampled by the 500Hz foreground service; PD6/PD7 use internal pull-ups and EXTI is masked | 025 |
| USE_AMP_FILTER is never defined; dither call in calcDrumVoiceSyncBlock() is compiled out in both LXR-master and our port | 022 |
| sample_mx_t = int32_t carrying a signed 24-bit value; int16 voice outputs enter mixer as int16<<8 via bufferTool_convertInt16ToSampleMix() | 022 |
| sampleMix_toS24() must NOT right-shift before clamp — the <<8 scale is already in the value; do not re-add >>8 | 022 |
| Voice sync-block APIs (DrumVoice/Snare/CymbalVoice/HiHat) remain int16_t* — widening deferred; conversion to sample_mx_t happens in mixer | 022 |
| distortion.h/.c remains int16_t* — widening deferred; see DITHER_AUDIT.md pinned-for-revisit section | 022 |
| dth global menu option (short dth, long 16bitDth, DTYPE_ON_OFF, default off) is designed but not wired; full plan in DITHER_AUDIT.md Steps 1–8 | 022 |
| pack_half() now emits true signed 24-bit payload [MSW,LSW] from sample_mx_t; DMA frame LSW is no longer forced to 0x0000 | 022 |
| Session 023 working source is the local directory `lxr02-037_port/`; future logs should refer to working repository status, not a source archive | 023 |
| TIM6 is now priority 6 and only increments counters + schedules foreground service; shift-register exchange, PB4/PB6 jack detect, encoder-button debounce, and endless-pot scan run in `timebase_serviceFrontPanel()` | 023 |
| TIM7 LCD drain is 5kHz at priority 7; keep the SPSC queue and use `lcd_waitForIdle()` only for rare modal paths that already suspend audio | 023 |
| Slider log taper uses a 4096-entry LUT built from `SLIDER_LOG_TAPER_DB`; `adc_checkPots()` should not reintroduce foreground `powf()` calls | 023 |
| `OSC_WAVE_INTERP_MAX_ACTIVE=2` is the current active cap for waveform interpolation targets per 32-frame DSP block; user-sample interpolation stays enabled | 023 |
| Current ITCM test state is oscillator-only: `ENABLE_OSC_INITCM_CODE=1`, `ENABLE_EFFECT_INITCM_CODE=0`; filter/distortion ITCM tested worse on CPU monitor | 023 |
| `Load:[Samples ]` now installs `/samples` then appends `/loops`; there is no separate visible SampLoop menu entry | 023 |
| `AUDIO_DMA_FRAMES=96` remains the stable hardware DMA half; 128 needs an 8KB `.dma_nocache` MPU/linker redesign because the two audio DMA buffers alone fill 4KB | 023 |
| Clear mode owns encoder turn and click before edit-mode toggling: turn selects clear target, click executes clear | 024 |
| Clear mode should stay armed after SHIFT+COPY until both SHIFT and COPY are released; SHIFT release alone must not cancel if COPY is still held | 024 |
| README now stops at front-matter/hardware/clock summary; MEMORY is the canonical verbose home for known issues, critical reminders, and moved historical details | 024 |
| Load/save button glitch: menu_resetSaveParameters() must be called AFTER menu_activePage is updated in menu_switchPage() case LOAD_PAGE — fix documented in LOAD_SAVE_GLITCH_ASSESSMENT.md, not yet applied to menu.c | 026 |
| afatfs_fread() returning n==0 does NOT mean EOF — it means the SD buffer is not ready yet; always use n==0 && afatfs_feof(op_file) for EOF detection | 026 |
| filesystem_loadName_tick() and filesystem_loadKit_tick() phase 2: malformed/zero-byte files now set name to "-       " and close cleanly; kit loads abort with FS_STATUS_ERROR before touching parameter_values[] | 026 |
| PAR_EXT_SYNC (midi auto-sync) occupies the parameter slot where PAR_FETCH lived in LXR037 — potential cross-system file interchange mismatch at that offset; TODO before any LXR037 file interchange | 026 |

---

## Append Template

```
| 0NN | YYYY-MM-DD | working repository status/path | One-line topic |
```

```
### 0NN — Title (YYYY-MM-DD)
One paragraph summary.
- **Find here**: comma-separated topics
```

Add any new cross-session facts to the Key Cross-Session Facts table.

### 037 — Failed Morph/Kit Save Expansion Attempt (2026-07-15)
Attempted Morph Kit/Instrument save expansion, asyncfatfs LFN rename/replace policy, menu exposure fixes, and repeated Kit Save repairs. User hardware testing failed: Kit Save still does not create/load a usable Kit directory, and optimistic cache insertion briefly made nonexistent/unloadable Kits appear in the list. Treat this session as failed and high-risk; next session should isolate or roll back rather than stack more patches.
- **Find here**: failed Kit Save, failed KitMrp/InstrumentMrp expansion, asyncfatfs LFN rename/replace attempts, fake Kit cache issue, rollback boundary notes

### 038 — Save/Load Repair, KitMrp/InstrumentMrp Save, And Asyncfatfs Documentation (2026-07-15)
Recovered from Session 037 by removing broken Scene/Kit/Morph save remnants, rebuilding Kit Save on recursive directory replacement and verified exact-card scan behavior, restoring Instrument Save without corrupting root `Instrument/`, adding diagnostic filesystem error reporting, and tightening Load/Save hardware UI behavior. `Save:[KitMrp]` and nested InstrumentMrp Save now use new-format text payloads with current interpolated values written to both endpoint sections, while resident kit/instrument names remain unchanged. Added/updated Session 038 source-of-truth logs, filesystem specs, and asyncfatfs reference documentation.
- **Find here**: Session 037 cleanup/correction, recursive Kit overwrite, filesystem error overlays, Save:[KitMrp], InstrumentMrp Save projection, Load/Save endless-pot/BAR behavior, asyncfatfs API reference
