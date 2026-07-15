# LXR-02 Firmware Port — Project Context

This file is the working memory for Codex/Claude Code/LLM Agent sessions on this project.
Read it fully at the start of every session before touching any code.
Update it whenever something is confirmed, fixed, or decided.

---

## Quick Start

```
# Repository root is the working tree root (branch: dev-burst-reduction)

# Build
make && make img   →   build/LXRV2_lxr02.img

# Flash: copy LXRV2_lxr02.img to SD card root, hold main encoder, power on
```

**Current working source**: repository root, a development branch.

**Session 036 note**: read `knowledge_files/log_archive/015_SESSION_HANDOFF_LOG.md` through
`knowledge_files/log_archive/036_SESSION_HANDOFF_LOG.md` before related work. For current module boundaries after parser removal, Pattern/Preset ownership moves, directory Kit load/save, root Instrument loading/saving, Kit/Instrument Morph Load, descriptor-backed instrument storage, Choke/track-7 behavior, descriptor Morph/LFO/velocity targets, LFO `self` storage, descriptor-domain LFO modulation, staged Instrument commit, asyncfatfs LFN/case behavior, and the future filesystem shape, also read `knowledge_files/specification_reference/MODULE_INTERCHANGE_SPEC.md` and `knowledge_files/specification_reference/FILESYSTEM_SPEC.md`.
Session 019 adds TIM3 sequencer timing owner, interrupt-driven USART3, MidiRealtime timestamped ring, real CLK/RST jack backend, voice trigger pending ring, PAR_EXT_SYNC, CC1→MORPH, BAR1/BAR2 MIDI path, and corrects OUTPUT_DMA_SIZE to 32. Session 020 completes RV5-RV10 slider control as independent mixer-stage multipliers with per-block interpolation and configurable log taper.
Session 021 confirms OUT jack-detect mapping (OUT1L/OUT1R/OUT2L/OUT2R = PD6/PD7/PB4/PB6); after Session 025 all four jack-detect pins are retained state sampled by the 500Hz foreground service, while PD6/PD7 EXTI remains masked and PD6/PD7 use internal pull-ups to retain inserted=HIGH.
Session 022 introduces `sample_mx_t` (signed 24-bit in int32_t), widens mixer summing/output buffers/codec packer to carry true 24-bit audio, and documents the `dth` global menu option plan (not yet wired). Voice sync-blocks and distortion remain int16_t* (deferred).
Session 023 refactors CPU scheduling and DSP hot paths: TIM6 front-panel work is foreground-serviced at 500Hz, TIM7 LCD drain is 5kHz/priority 7, idle filesystem polling is rate-limited, slider log taper uses a 4096-entry LUT, oscillator interpolation is capped by `OSC_WAVE_INTERP_MAX_ACTIVE=2` in the current test build, oscillator-only ITCM is enabled, sample+loop loading is one menu command, and the main encoder direction-change residue bug is mitigated.
Session 024 fixes copy/clear ownership in the menu path and consolidates README/MEMORY.
Session 025 fixes SD/global load compatibility and rear-jack behavior: FAT12/exFAT are rejected with `Unsupported card` / `use MBR-FAT32`; current globals span is 23 bytes, legacy 22-byte globals load silently with compatibility defaults, other globals lengths use safe fallback plus `check&save` warning; CLK IN is PD4 rising edge, RST IN is PD5 rising edge, and PD6/PD7 jack detect is retained-state foreground polling with pull-ups.
Session 026 diagnoses the load/save button display glitch (one-frame edit-mode flash caused by `menu_resetSaveParameters()` firing before `menu_activePage` is updated in `menu_switchPage()` — fix documented in `LOAD_SAVE_GLITCH_ASSESSMENT.md`, NOT YET APPLIED to `menu.c`); fixes `filesystem_loadName_tick()` and `filesystem_loadKit_tick()` phase-2 zero-byte/short-file hang using correct `afatfs_feof()` EOF idiom; malformed files now show `-` in the slot-name display. Note: `PAR_EXT_SYNC` occupies the LXR037 `PAR_FETCH` parameter slot — potential cross-system file interchange mismatch; TODO before any LXR037 file interchange.
Session 027 chunks runtime kit/all/performance sound-apply completion: after audio starts, `menu_startSoundApply()` / `menu_tickSoundApply()` drive `preset_startDrumsetApply()` / `preset_tickDrumsetApply()` so one voice's velocity/LFO modulation routing is applied per foreground pass; boot-time pre-audio apply remains synchronous. `AUDIO_DMA_FRAMES` remains 96; 64-frame latency testing is deferred until the chunked path is hardware-tested.
Session 028 removes `Core/MIDI/frontPanelParser.c/h` from live code. Former parser opcodes are direct owner calls: Pattern edits through `pat_*` in `Core/Scene/Pattern/PatternData.c`, LED feedback through `SeqLedState` plus foreground `led_processSeqLedState()`, sound parameters through Preset, MIDI config through MidiParser, and transport/playback through Sequencer. Euklid/SOM now live in `Core/Scene/Pattern/`.
Session 029 completes the PatternData storage-ownership pass and Preset folder move. `seq_patternSet`, `seq_tmpPattern`, `seq_selectedStep`, `SEQ_DEFAULT_NOTE`, and `SEQ_NEXT_RANDOM*` are gone from live code; Sequencer reads/writes pattern storage through `pat_*` helpers while keeping timing/transport/recording gates. `Core/Preset/` is now `Core/Scene/Preset/`; public names remain `preset_*`, `parameterArray_*`, and `paramArray_*`. `parameter_values[]`/`parameters2[]` still live in Menu until the later instrument/file redesign. Staging/global audits are in `STAGING_AUDIT.md` and `GLOBALS_STAGING_AUDIT.md`.
Session 030 begins Phase 2 filesystem work. `FILESYSTEM_SPEC.md` began as the root layout spec and now forwards to `knowledge_files/specification_reference/FILESYSTEM_SPEC.md`; `Core/Hardware/SD/storageTypes.c/h` owns kit text schemas/parameter maps with `storage_` prefixes; normal root kit load scans `Kit/NNN Name/`, loads `kitset.kcg` plus six instrument files, and keeps morph load on legacy `.SND`. Numbered folder convention is preferred `NNN Name`, compatibility `NNN_Name`, with a FAT short-alias fallback for scan aliases like `001SLA~1`. `SD_CARD/Kit/` is generated from legacy kits using the space convention.
Session 031 completes the one-live-pattern/8-bar bridge pass and follow-ups. Live `NUM_PATTERN` is 1 while pattern files still stream the old 8-slot bridge layout; STEP front page now owns per-track length, scale, MIDI channel, MIDI note, and per-track shuffle; empty boot tracks default to 16 steps. Sequencer timing runs at corrected default speed with a 96-PPQ master step clock, per-track scale ratios, per-track shuffle, and pattern realign. LED flash is a group overlay and `led_setBlinkLed()` is idempotent. `SHIFT+VOICE` enters morph endpoint edit mode using `parameters2[]` and a blinking VOICE mode LED; stopped selected-voice re-press previews the voice. Pattern/container storage no longer imports or exports the old single shuffle byte; only per-track shuffle extension data is used, and final storage conversion remains a Phase 2 external-converter concern.
Session 032 follows through on the instrument parameter refactor. VOICE pages are now populated from descriptor-owned layouts in `Core/DSP/Instruments/*/*Parameters.c` and menu edits write Scene descriptor images instead of static `menuPages.h`/`parameter_values[]` cells. Directory kit loading writes descriptor-indexed Scene storage, and Preset/InstrumentManager applies descriptor values back into the DSP runtime; the 001 Slak kit boots and audio mostly works after the then-canonical `amp_envelope_decay_closed/open` parser-key fix. `instrument_decimation` and `velo_mod_amount` are `ROW_NOBIND_IMAGE` parameters: morphable/modulatable/automatable image values with special runtime handling instead of direct member-offset binds. Current filesystem/instrument-file authority is `knowledge_files/specification_reference/FILESYSTEM_SPEC.md`; the former `INSTRUMENT_FILE_SPEC.md` content was folded there and the separate file was deleted.
Session 033 lands the Phase 3 instrument runtime repair. LFO descriptor display was fixed to use exact descriptor strings, target selection now skips non-modulatable rows with one `off`, VOICE sub-pages can show 16 descriptor cells as four-cell screens, LFOs have two destination pairs plus shared polarity, and descriptor/Scene LFO targets now apply in DSP. Descriptor Morph works against Scene-owned images; PERF Morph is split into global set-all Morph plus six per-voice Morph controls and Scene Decimation `srt`. `Core/Scene/SceneModTargets.c/h` owns non-voice sound modulation targets (`1vm..6vm`, then Scene `srt`), velocity modulation can retained-set per-voice Morph/Scene Decimation, and LFO Morph modulation is a hidden overlay centered on the retained per-voice Morph base. The then-open voice-6/tracks-6+7/Choke cleanup was completed in Session 034; normal Kit Save and descriptor-domain LFO scaling were completed in Session 035; root Instrument Save was completed in Session 036. Descriptor-aware step automation, Scene/Bank/Effect structures, and root `settings.cfg` remain.
Session 034 completes Instrument Load. InstrumentManager registry entries now own Basic/Advanced/Choke flags, display labels, extensions, and the two-Advanced replacement rule; Drum/Snare are Basic, Cymbal Advanced, HiHat Advanced|Choke. HiHat canonical decay keys are `amp_envelope_decay` and `amp_envelope_decay_choke`; legacy closed/open names remain accepted by storage. VOICE7 dynamically resolves Choke siblings, while a non-Choke slot 6 receives a generated Scene-owned track-7 decay/morph endpoint and Scene target `7dc`. Root `Instrument/` browsing/loading is live for `.drm/.snr/.cym/.hat`, as is the nested Load UI, Scene LED selection behavior, and stopped voice preview. Instrument files now stage outside live SceneData and Preset commits an active replacement atomically: clear all outgoing modulation owners, replace and reset the runtime slot, rebuild all six Morph/runtime images, then normalize/rebind all sources; UI target-changing actions stay locked throughout. The former Voice 2 long-decay fault was isolated to the old direct one-slot Instrument load lifecycle and cleared by this transaction. Session 035 later fixed descriptor LFO raw-runtime writes for direct descriptor targets; Session 036 added root Instrument Save. Remaining Phase 3 risks are descriptor-aware step automation, dynamic modulation-node enumeration for non-adapter paths, and Scene/Bank work.
Session 035 completes a major Phase 3 save/load/modulation pass. Kit and Instrument Morph Load are live in code: KitMrp parses a normal Kit directory and copies source normal endpoints into resident morph endpoints only for matching slot types; InstrumentMrp is available only for the destination slot's current type and follows the same same-type/no-change rule. LFO target voice storage now supports file-only `self` on `lfo_target_voice`/`lfo_target_voice_2`; load resolves it immediately to the destination slot and Kit Save emits it only for own-slot LFO voice selectors. Descriptor LFO modulation now uses descriptor-owned parameter-domain metadata and InstrumentManager adapters, applying temporary values through `instrumentManager_writeRuntime()`; negative polarity matches original LXR value-relative math in parameter space. SceneData retains default and loaded instrument stems (`inst_vo1`..`inst_vo6`, first 16 filename stem chars) for display/save provenance.
Session 036 fixes the load/save filesystem foundation. asyncfatfs now has reusable `afatfs_mkdir_lfn()`, `afatfs_fopen_lfn()`, `afatfs_opendir_lfn()`, and object iteration with VFAT LFN entries, preserved SFN display case, case-sensitive matching, returned 8.3 aliases, and real file/directory kind metadata. Dot-prefixed files/directories are real objects and must not be hidden by asyncfatfs. Numbered library slots are direct `000..999`; slot `000` is real for all filetypes, while instrument file voice coordinates remain one-based `1..6`. Normal Kit Save creates visible `Kit/NNN Name/` folders and visible mixed-case/space-preserving instrument member files, writes `kitset.kcg` after aliases are known, and uses the Session 038 filesystem-level recursive cleanup path to replace every physical directory for the target Kit slot before writing. Top-level Load/Save cycling now includes the promoted Kit/KitMrp paths plus File/Dir/sDir diagnostics as applicable. Root Instrument Save is implemented from Save-page VOICE mode and writes one resident voice to `Instrument/<stem.ext>` without deleting/recreating the root `Instrument/` folder. Atomic rename/replace remains a future primitive for Scene/Bank/autosave promotion. Scene Load/Save must be deliberately redone before Bank.
Current post-038 load/save note: `Save:[KitMrp]` and nested InstrumentMrp Save are implemented on the new-format text writers. Morph Save is a flattened current-position snapshot: morphable values are interpolated at the current per-voice Morph amount and written into both normal `[params]` and `[morph]` endpoint storage. Morph Save does not rename resident kit or instrument names/stems. Load/Save hardware controls now route pots 1-4 through type/slot/cursor/character editing, BAR1/BAR2 blank exactly one character per press in name editing, and main non-instrument Load/Save rows clear instrument voice LED blink.
Future Bank/autosave target note: Root `Scene/` is an explicit library/pool like root `Kit/` and `Instrument/`, not an autosaved workspace. Only `Bank/` autosaves. Inside a Bank, non-dot files are the committed save/load truth; dot-file backers receive debounced autosave writes. Voice mode separates the active playback Scene from the SEQ-button Scene edit target set: one Voice/Kit/Instrument edit may batch-mutate any subset of the 16 resident Bank Scenes and dirty each affected Scene-local file, while Pattern edits stay active-Scene scoped. Bank SAVE waits for selected autosave writes, then promotes selected dot-file backers over non-dot files. Startup normally resumes valid dot-files; leftover `.tmp` files are incomplete temp writes and should be ignored/deleted while the prior valid dot-file remains usable. Scene RELOAD reads selected Scene non-dot files into resident memory and resets that Scene's dot-file backers to match. Root `settings.cfg` stores the active Bank number, has `.settings.cfg`, and both are rewritten when closing global settings or loading/saving a Bank. This target requires asyncfatfs rename/replace or equivalent safe promotion before implementation.

---

## Repository Layout

```
./
├── MEMORY.md                        ← this file
├── README.md                        ← confirmed hardware, known issues, critical reminders
├── main.c
├── config.h
├── Makefile
├── STM32F765VIHx_FLASH.ld
├── requirements.txt
├── tools/
│   └── build_lxrv2_img.py          ← packages ELF → LXRV2_lxr02.img
├── build/                           ← generated, not in VCS
├── knowledge_files/
│   ├── SESSION_HANDOFF_TEMPLATE.md ← template for writing new session handoff logs
│   ├── ENHANCED_FEATURES.md        ← future enhancement notes
│   ├── OSC_INTERP_AUDIT.md         ← oscillator interpolation audit
│   ├── specification_reference/
│   │   ├── MODULE_INTERCHANGE_SPEC.md ← current direct-call API boundary map, updated through Session 033
│   │   ├── FILESYSTEM_SPEC.md         ← authoritative filesystem, kit/instrument file, Scene storage, and save/load target spec
│   │   ├── MEMORY_AUDIT.md            ← historical Session 023 memory region audit notes
│   │   └── DSP_AUDIT.md               ← historical DSP pipeline audit and hot-path notes
│   ├── hardware_archive/
│   │   ├── HARDWARE_MAP.md         ← full confirmed pin table, IRQ numbers
│   │   ├── AVR_TO_F765_MIGRATION.md ← architectural notes, sequencer ISR design baseline
│   │   ├── FRONTPANEL_AUDIT.md     ← legacy front-panel bridge elimination audit
│   │   ├── SD_CARD_INVESTIGATION.md ← SD false-positive analysis (PA8, 74HC165)
│   │   └── XP_CONNECTOR_MAPS.md   ← ribbon cable pin mappings
│   └── log_archive/
│       ├── 000_SESSION_INDEX.md    ← index of all sessions with keyword lookup
│       ├── 001_SESSION_HANDOFF_LOG.md
│       ├── 002_SESSION_HANDOFF_LOG.md
│       ├── 003_SESSION_HANDOFF_LOG.md
│       ├── 004_SESSION_HANDOFF_LOG.md
│       ├── 005_SESSION_HANDOFF_LOG.md
│       ├── 006_SESSION_HANDOFF_LOG.md
│       ├── 007_SESSION_HANDOFF_LOG.md
│       ├── 008_SESSION_HANDOFF_LOG.md
│       ├── 009_SESSION_HANDOFF_LOG.md
│       ├── 010_SESSION_HANDOFF_LOG.md
│       ├── 011_SESSION_HANDOFF_LOG.md
│       ├── 012_SESSION_HANDOFF_LOG.md
│       ├── 013_SESSION_HANDOFF_LOG.md
│       ├── 014_SESSION_HANDOFF_LOG.md
│       ├── 015_SESSION_HANDOFF_LOG.md
│       ├── 016_SESSION_HANDOFF_LOG.md
│       ├── 017_SESSION_HANDOFF_LOG.md
│       ├── 018_SESSION_HANDOFF_LOG.md
│       ├── 019_SESSION_HANDOFF_LOG.md
│       ├── 020_SESSION_HANDOFF_LOG.md
│       ├── 021_SESSION_HANDOFF_LOG.md
│       ├── 022_SESSION_HANDOFF_LOG.md
│       ├── 023_SESSION_HANDOFF_LOG.md
│       ├── 024_SESSION_HANDOFF_LOG.md
│       ├── 025_SESSION_HANDOFF_LOG.md
│       ├── 026_SESSION_HANDOFF_LOG.md
│       ├── 027_SESSION_HANDOFF_LOG.md
│       ├── 028_SESSION_HANDOFF_LOG.md
│       ├── 029_SESSION_HANDOFF_LOG.md
│       ├── 030_SESSION_HANDOFF_LOG.md
│       ├── 031_SESSION_HANDOFF_LOG.md
│       ├── 032_SESSION_HANDOFF_LOG.md
│       ├── 033_SESSION_HANDOFF_LOG.md
│       ├── 034_SESSION_HANDOFF_LOG.md
│       ├── 035_SESSION_HANDOFF_LOG.md
│       └── 036_SESSION_HANDOFF_LOG.md
└── Core/
    ├── globals.h
    ├── datatypes.h
    ├── Src/
    │   └── startup_stm32f765xx.s
    ├── Hardware/
    │   ├── clocks.c/h               ← sysclk_init(), FPU enable via CPACR
    │   ├── timebase.c/h             ← SysTick 4kHz mainboard tick, TIM6 1kHz counters + 500Hz foreground service, TIM7 5kHz LCD drain
    │   ├── AudioCodecManager.c/h    ← consolidated audio: DMA ISRs, I2S/GPIO/DMA init, SPSC queue
    │   ├── triggerJacks.c/h         ← CLK OUT/IN, RST IN; OUT jack detect is foreground-polled
    │   ├── memtest.c/h              ← flash sector probe (boot-time, MEMTEST_ENABLED gate)
    │   ├── frontPanel/
    │   │   ├── buttonHandler.c/h    ← ISR-safe event ring, main-loop processEvents()
    │   │   ├── lcd.c/h              ← TIM7-driven async queue, 128-entry SPSC ring
    │   │   ├── ledHandler.c/h
    │   │   └── IO/
    │   │       ├── adcPots.c/h      ← sliders RV5-10, ADC1 DMA
    │   │       ├── din.c/h          ← 74HC165×5 buttons, SPI1
    │   │       ├── dout.c/h         ← 74HC595×5 LEDs, SPI1
    │   │       ├── encoder.c/h      ← SW42, TIM1 IC, Dannegger, accel + rebound suppression
    │   │       └── endlessPots.c/h  ← RV1-4, atan2 delta tracking
    │   ├── SD/
    │   │   ├── filesystem.c/h       ← public facade: typed async load/save/name/scan operations; Kit load/save uses root Kit/ directories; root Instrument load/save exists
    │   │   ├── storageTypes.c/h     ← Kit/instrument text parser+writer, numbered-folder parser, descriptor file schema helpers
    │   │   ├── kitBrowser.c/h       ← kit-only 1000-slot gap-tolerant compatibility browser; 000 is real
    │   │   ├── SPI/
    │   │   │   ├── spi_sd.c/h       ← bit-bang SPI: PC12/PD2/PC8/PD0
    │   │   │   └── sd_routines.c/h  ← SD_init() only; blocking read/write superseded
    │   │   └── asyncfatfs/
    │   │       ├── asyncfatfs.c/h   ← Betaflight asyncfatfs modified for LXR-02, with LFN/case object APIs
    │   │       ├── fat_standard.c/h
    │   │       ├── sdcard.h
    │   │       └── sdcard_lxr02.c/h ← SD block-device shim over bit-bang SPI
    │   └── USB/
    │       ├── OTG_Driver/
    │       ├── Device_Library/
    │       └── App/                 ← usb_manager, usb_midi_core, etc.
    ├── Menu/
    │   ├── menu.c/h                 ← full port, all pages, load/save UI
    │   ├── menuPages.h              ← 16-page × 8-subpage table
    │   ├── MenuText.h               ← all label strings
    │   ├── Cc2Text.c                ← modTargets[] 205 entries
    │   ├── CcNr2Text.h
    │   ├── copyClearTools.c/h       ← copy/clear UI; pattern mutation through PatternData
    │   └── screensaver.c/h          ← screensaver with explicit LCD off/on phases
    ├── MIDI/
    │   ├── Uart.c/h                 ← USART3, 31250 baud, interrupt-driven dual FIFO (realtime + normal)
    │   ├── MidiRealtime.c/h         ← 32-entry timestamped SPSC ring for MIDI_CLOCK/START/CONTINUE/STOP
    │   ├── FIFO.c/h
    │   ├── MidiMessages.h           ← full mainboard version (MIDI_NRPN_* prefix)
    │   ├── MidiNoteNumbers.h
    │   ├── MidiParser.c/h
    │   ├── MidiVoiceControl.c/h
    │   ├── SeqStep.h
    │   └── valueShaper.h
    ├── Scene/
    │   ├── SceneData.c/h            ← Scene-owned settings, kit slots, descriptor images, MIDI routing
    │   ├── SceneModTargets.c/h      ← Scene-level modulation target namespace: 1vm..6vm plus Scene srt
    │   ├── Pattern/
    │   │   ├── PatternData.c/h      ← pattern/track/step storage and edit API
    │   │   ├── EuklidGenerator.c/h  ← pattern generator
    │   │   ├── SomData.c/h          ← SOM data tables
    │   │   └── SomGenerator.c/h     ← SOM pattern/performance generator
    │   └── Preset/
    │       ├── ParameterArray.h/c   ← supersedes Parameters.h; NUM_PARAMS=275
    │       └── presetManager.c/h    ← typed load/save for kit, morph, pattern, performance, all, globals
    ├── DSP/
    │   └── Instruments/
    │       ├── InstrumentManager.c/h ← descriptor registry, VOICE menu lookup, Scene-to-DSP apply bridge
    │       ├── Drum/                 ← Drum descriptor keys, flags, menu layout, runtime metadata
    │       ├── Snare/                ← Snare descriptor keys, flags, menu layout, runtime metadata
    │       ├── Cymbal/               ← Cymbal descriptor keys, flags, menu layout, runtime metadata
    │       └── HiHat/                ← HiHat/open-hat descriptor keys, flags, menu layout, runtime metadata
    ├── SampleRom/
    │   ├── SampleMemory.c/h         ← sample flash metadata/runtime cache, 120 entries, loop flags
    │   └── sampleFlash.c/h          ← guarded F765 sector 6-11 erase/program helpers
    ├── Sequencer/
    │   ├── sequencerTimer.c/h       ← TIM3 4kHz sequencer timing owner (IRQ29, priority 2) — Session 019
    │   ├── sequencer.c/h            ← original LXR sequencer source (driven by TIM3_IRQHandler)
    │   ├── clockSync.c/h
    ├── DSPAudio/
    │   ├── random.c/h               ← F765 RNG port (PLL48CLK, bare register)
    │   └── [all DSP voice files]    ← ported; mixer_calcNextSampleBlock wired to AudioCodecManager
    └── compat/
        ├── stm32f4xx.h              ← vestigial-include shim via <stdint.h>
        └── cmsis_intrinsics.h
```

### Where to look for things

| Question | File |
|----------|------|
| Which session introduced a fix? | `knowledge_files/log_archive/000_SESSION_INDEX.md` |
| Full details of a fix or decision? | `knowledge_files/log_archive/00x_SESSION_HANDOFF_LOG.md` |
| Current filesystem, instrument/kit file, Scene storage, menu, and DSP propagation spec? | `knowledge_files/specification_reference/FILESYSTEM_SPEC.md` |
| Current module/API ownership boundaries? | `knowledge_files/specification_reference/MODULE_INTERCHANGE_SPEC.md` |
| Confirmed pin assignments / IRQs? | `knowledge_files/hardware_archive/HARDWARE_MAP.md` |
| Sequencer / DSP architecture plans? | `knowledge_files/hardware_archive/AVR_TO_F765_MIGRATION.md` |
| Current known issues and reminders? | `MEMORY.md` |

---

## Project Goal

Port LXR 0.37 to the LXR-02 hardware (STM32F765VIH6). Original LXR: STM32F4 audio + ATmega644 AVR front panel. LXR-02: single STM32F765.

- This folder is the repository/codebase.
- `knowledge_files/LXR-master/` is read-only reference material only. Do not modify it.
- Knowledge docs should be updated when architecture changes; session logs live under `knowledge_files/log_archive/`, current direct-call API boundaries live in `knowledge_files/specification_reference/MODULE_INTERCHANGE_SPEC.md`, and filesystem plus descriptor instrument/kit storage rules live in `knowledge_files/specification_reference/FILESYSTEM_SPEC.md`.
- **Original source reference**: `knowledge_files/LXR-master/` — AVR in `front/LxrAvr/`, STM32F4 in `mainboard/LxrStm32/src/`

## General Process Reminders

- Always verify the local working repository directory before writing code.
- Blocking for 1ms anywhere in the main loop or any ISR at priority <= 4 is unacceptable.
- Runtime SD/file work must remain asynchronous; boot-only synchronous polling is allowed before audio starts.
- New code should be commented at detailed contract level: why the function,
  variable, or storage type exists; what it does; inputs/outputs; and
  clients/accessors/affiliates. Do this proactively, not as a cleanup after the
  user asks again.

---

## Hardware

### MCU: STM32F765VIH6, TFBGA100

- SYSCLK = 216MHz (HSE 16MHz, PLLM=16, PLLN=432, PLLP=2)
- PCLK1 = 54MHz (APB1/4) — TIM6, USART3, I2S2, I2S3
- PCLK2 = 108MHz (APB2/2) — SPI1, ADC
- PLL48CLK = 48MHz (PLLQ=9) — USB, RNG
- PLLI2S: N=271, R=2 → 135.5MHz → Fs=44108Hz
- Application origin: **0x08008000** — VTOR must be set at startup
- Stack top (SP): **0x20080000**
- DTCM 128KB (not DMA-accessible) + SRAM1 368KB + SRAM2 16KB
- I-Cache enabled (16KB, ICIALLU invalidate) — Session 13.
- D-Cache enabled (16KB) with MPU (WT for SRAM, SO for DMA buffers) — Session 13.
- DMA buffers live in the `.dma_nocache` linker section, marked Strongly-Ordered via MPU.
- `audioOutBuffer` lives in DTCM (`INDTCMZ`) for single-cycle access.

### Flash Sector Layout (single-bank, confirmed via memtest)

| Sector | Range | Size | Use |
|--------|-------|------|-----|
| 0 | 0x08000000–0x08007FFF | 32KB | LXRV2 Bootloader |
| 1–5 | 0x08008000–0x0807FFFF | — | Application |
| 6–11 | 0x08080000–0x081FFFFF | 6×256KB | Sample storage, implemented Session 18 |

**Erase floor: sector 6.** `sampleFlash.c` must hard-reject any erase below sector 6.

### Confirmed GPIO

| Peripheral | Pins | Notes |
|-----------|------|-------|
| LCD (4-bit parallel) | PE7-PE10 (DB4-7), PE11 (E), PE12 (RS) | TIM7 async driver |
| LEDs — 74HC595×5 | PA7 MOSI, PB3 SCK, PB2 LATCH | SPI1 |
| Buttons — 74HC165×5 | PA6 MISO, PB3 SCK, PB2 LATCH | SPI1, shared with LEDs |
| SW43 SHIFT/BAR1 | PB7 switch (active HIGH), PB8 LED | |
| Main encoder SW42 A/B | PE13/PE14 | AF1 TIM1_CH3/CH4, external 10kΩ pull-ups (R75/R76), NO internal pull-up |
| Main encoder SW42 SW | PE15 | Internal pull-up |
| Endless pots RV1-4 | PB0/1, PC4/5, PC2/3, PC0/1 | ADC1, atan2 delta tracking |
| Sliders RV5-10 | PA0-PA5 | ADC1 DMA (RV5↔RV6 label swap corrected in config.h) |
| DAC2 (CS4344, U24) | PB12 WS, PB13 CK, PB15 SD, PC6 MCK | I2S2, AF5 |
| DAC1 (CS4344, U23) | PA15 WS, PC10 CK, **PB5** SD, PC7 MCK | I2S3, AF6. **SD=PB5 not PC12** |
| SD card (bit-bang) | PC12 SCLK, PD2 MOSI, PC8 MISO, PD0 CS, PD1 DETECT | NOT SPI1. Hardware SPI remapping impossible on this board. |
| USB MIDI | PA11 D-, PA12 D+ | OTG_FS, ADUM3160 isolator, VBUS sensing disabled |
| MIDI DIN | PB10 TX, PB11 RX | USART3, 31250 baud |
| CLK OUT | PC13 | |
| CLK IN | PD4 | GPIO input pull-up, EXTI4 rising edge on low-to-high transition |
| RST IN | PD5 | GPIO input pull-up, EXTI5 rising edge on low-to-high transition |
| OUT1 L detect | PD6 | GPIO input pull-up; no plug=LOW, plug inserted=HIGH, sampled by 500Hz foreground service |
| OUT1 R detect | PD7 | GPIO input pull-up; no plug=LOW, plug inserted=HIGH, sampled by 500Hz foreground service |
| OUT2 L detect | PB4 | No plug=LOW, plug inserted=HIGH, sampled by 500Hz foreground service |
| OUT2 R detect | PB6 | No plug=LOW, plug inserted=HIGH, sampled by 500Hz foreground service |

### IRQ Assignments (current — Session 025)

| IRQ | Priority | Handler | Function |
|-----|----------|---------|----------|
| IRQ10 | 3 | EXTI4_IRQHandler | CLK IN — PD4 rising edge, push to trigger event ring |
| IRQ15 | 4 | DMA1_Stream4_IRQHandler | I2S2/DAC2 — audio refill master |
| IRQ23 | 3 | EXTI9_5_IRQHandler | RST IN — PD5 rising edge; PD6/PD7 masked and polled as jack state |
| IRQ27 | 1 | TIM1_CC_IRQHandler | Main encoder A/B input capture |
| IRQ29 | 2 | TIM3_IRQHandler | 4kHz sequencer timing owner: processRealtimeEvents → triggerJacks_tick → seq_tick |
| IRQ39 | 5 | USART3_IRQHandler | MIDI DIN RX/TX; timestamps bytes with TIM2; realtime bytes → MidiRealtime ring |
| IRQ47 | 4 | DMA1_Stream7_IRQHandler | I2S3/DAC1 — audio slave (flags only) |
| IRQ54 | 6 | TIM6_DAC_IRQHandler | 1kHz — counters + foreground front-panel service flag |
| IRQ55 | 7 | TIM7_IRQHandler | 5kHz — LCD queue drain |
| IRQ67 | 5 | OTG_FS_IRQHandler | USB MIDI |

TIM6 now schedules `timebase_serviceFrontPanel()` from the foreground loop.
Shift-register exchange, PD6/PD7/PB4/PB6 jack detect, encoder-button debounce, and
endless-pot angle processing no longer run in the TIM6 ISR.

---

## Audio Pipeline (AudioCodecManager.c)

`audioCodec_init()` is the **single hardware entry point**.

**Main loop pattern:**
```c
if (audioCodec_queueFreeSlots() > 0) {
    // Fill one AUDIO_DMA_FRAMES hardware slot as three OUTPUT_DMA_SIZE blocks.
    for (frame = 0; frame < AUDIO_DMA_FRAMES; frame += OUTPUT_DMA_SIZE)
        mixer_calcNextSampleBlock(&buf[frame * 2], &buf2[frame * 2]);
    audioCodec_commitRenderBuffer();
}
```

**OUTPUT_DMA_SIZE = 32** is the effective LXR-master DSP/control block (confirmed correct in Session 019 — do NOT revert to 16).
**AUDIO_DMA_FRAMES = 96** is the hardware DMA half; render budget per queued hardware slot is **2.18ms** = 471,288 cycles at 216MHz.

**DSP render must remain in the main loop.** Moving it to the DMA ISR creates a hard 2.18ms ceiling — when DSP expands beyond the budget the system locks up with no graceful degradation. Main loop allows unlimited expansion with underruns as the graceful signal.

**SPSC queue**: 2-slot queue. `ready_head` is owned by ISR, `ready_tail` by main loop, and `ready_count` tracks free/full state for queueing and diagnostics.

**CPU-use widget**: `audioCodec_getQueueFreePercent()` uses DWT cycle-counter accounting of `ready_count < 2`. This is an audio queue-free pressure meter, not generic MCU utilization.

**24-bit output path (Session 022)**: `audioOutBuffer`/`audioOutBuffer2` are `sample_mx_t` (int32_t, signed-24 value in container). `pack_half()` emits a true 24-bit payload in both halfwords of each I2S frame. Do NOT re-add `LSW = 0` zeroing. Do NOT add a `>> 8` shift in `sampleMix_toS24()` — int16 voice values enter the mixer already scaled as `int16 << 8` via `bufferTool_convertInt16ToSampleMix()`; adding `>>8` at pack time was the loudness regression fixed in session 022.

**Zero startup underruns**: boot SD operations complete before `audioCodec_init()`.
Runtime kit/all/performance sound-apply completion is chunked after audio starts (Session 027): `menu_tickSoundApply()` drives `preset_tickDrumsetApply()` so only one voice's velocity/LFO modulation routing is applied per foreground pass. Boot-time pre-audio apply remains synchronous.

### Internal DAC — Must Never Be Enabled

The STM32F765 has an internal 12-bit DAC on PA4 (DAC1_OUT) and PA5 (DAC2_OUT). These pins are also ADC1_IN4 and ADC1_IN5, used as slider inputs for RV6 and RV5. **The internal DAC must never be enabled.** Accidental DAC peripheral clock enable or pin mode change silently corrupts slider ADC readings.

`TIM6_DAC_IRQHandler` is the vector table name for IRQ54 because TIM6 and the internal DAC share an IRQ line by ST hardware design. **This does not mean the DAC is in use.** Audio is handled entirely by external CS4344 codecs via I2S2 and I2S3.

---

## SD Card Architecture (implemented Sessions 12, 17, and Phase 2 start in 030)

**SD operations must never block the main loop or any ISR at priority ≤ 4.**

**Implemented solution**: asyncfatfs (Betaflight/Cleanflight library), fronted by `filesystem.c/h`. Ground-up FAT16/FAT32 reimplementation with polling-based non-blocking I/O. Replaces ChaN FatFS entirely. Decision made in Session 11, implemented in Session 12, and reorganized behind the filesystem facade in Session 17.

**Card format policy (Session 025)**: FAT16 and FAT32 are supported; MBR-FAT32 is recommended. FAT12 and exFAT are unsupported. Boot detection for unsupported layouts shows `Unsupported card` / `use MBR-FAT32` for 5 seconds and the filesystem is not mounted, so no boot load is attempted from that card.

**Architecture:**
```
Core/Scene/Preset/presetManager.c / kitBrowser.c
  → filesystem.c (typed operations: kit/morph/pattern/performance/all/globals)
    → asyncfatfs/asyncfatfs.c (afatfs_fopen/fread/fwrite/fclose/mkdir/chdir/poll)
      → asyncfatfs/sdcard_lxr02.c (sector transfer FSM, 16 bytes/burst)
        → SPI/spi_sd.c (bit-bang SPI)
```

**Key design points:**
- `afatfs_poll()` is the single entry point for all SD background work. Called from main loop initially; can migrate to TIM5 ISR later if throughput is insufficient.
- `afatfs_poll()` must be called from **one context only** — never both main loop and ISR simultaneously.
- Session 023 rate-limits idle foreground polling to reduce CPU use. Active/busy operations and filesystem initialization still poll every pass.
- 8-sector LRU cache (4KB) between filesystem logic and SD card. Cache hits are free (no SPI traffic).
- `sdcard_lxr02.c` implements `sdcard_readBlock`/`sdcard_writeBlock`/`sdcard_poll` on top of `SPI/spi_sd.c`. Each `sdcard_poll()` call clocks a burst of 16 SPI bytes (~9µs). A 512-byte sector completes in 32 polls.
- `filesystem.c` serializes operations — one SD operation at a time. Request functions return immediately; completion is signalled via callback/status.
- `filesystem.c` owns the filetype registry and add-a-filetype checklist. Non-SD clients include `filesystem.h` only.
- Authoritative filesystem and instrument-file spec lives in
  `knowledge_files/specification_reference/FILESYSTEM_SPEC.md`. The former
  root `FILESYSTEM_SPEC.md` compatibility pointer was deleted in Session 033.
  Target root directories are `Bank`, `Scene`, `Kit`, `Pattern`, `Sample`,
  `Wavetable`, `Effect`, and `Instrument`; future system settings live in root
  `settings.cfg`. Current implemented directory work is root `Kit/` load/save
  plus root `Instrument/` load/save and Kit/Instrument Morph Save. Scene
  Load/Save must be redone before Bank. Globals still use legacy `glo.cfg`.
- `storageTypes.c/h` owns text storage schemas, parser/writer helpers, and
  descriptor-keyed parameter maps. Keep it free of `asyncfatfs` calls and keep
  function names prefixed `storage_`.
- Normal kit load now scans root `Kit/` directories named `NNN Name` or
  compatibility `NNN_Name`, then loads `kitset.kcg` plus six instrument files.
  Normal Kit Save now creates/opens a numbered Kit directory and writes
  `kitset.kcg` plus six instrument files. Numbered library slots are direct
  `000..999`; slot `000` is real for all filetypes. KitMrp Save uses the same
  directory shape and writes the current interpolated Morph state into both
  normal and morph endpoint sections.
- Session 036 adds asyncfatfs LFN component creation/object iteration through
  `afatfs_mkdir_lfn()`, `afatfs_fopen_lfn()`, `afatfs_opendir_lfn()`, and
  `afatfs_findNextObject()`. These preserve SFN display case, create VFAT LFN
  entries, support case-sensitive matching for production LFN opens, return
  generated 8.3 aliases for identity opens, and expose file/directory object
  kind. They are now used by File/Dir diagnostics, Kit scan/load/save, root
  Instrument scan/load, and root Instrument Save. Future save code should
  reuse/extend these filesystem-owned primitives rather than creating local FAT
  writers in callers.
- Dot-prefixed files/directories are real filesystem objects. asyncfatfs and
  File/Dir diagnostics must not hide them; product scanners filter only after
  object iteration.
- Filesystem-level recursive directory cleanup exists for replacement-style
  saves such as Kit Save. Atomic rename/replace remains missing and is required
  before Scene/Bank/autosave promotion can claim power-loss-safe commits.
- Root `Instrument/` is a separately scanned, type-filtered source pool.
  Instrument Load initially shows the destination slot's Kit member stem;
  changing type only changes the type selection, and lower-row browsing is the
  action that replaces the slot. Pool entries load immediately after their
  private filesystem staging has validated.
- Root Instrument Save is entered from Save-page VOICE press and writes one
  resident Scene/voice slot to `Instrument/<stem.ext>` using the same
  descriptor-keyed text writer and `self` serialization rule as Kit Save.
- Instrument Load is not a one-slot write. Preset owns its completion
  transaction: clear every runtime modulation owner referencing the outgoing
  slot before the type/image replacement, reset the incoming runtime, request
  all six Morph/runtime applies, and normalize/rebind all source targets after
  the images exist. Keep mode, Scene, destination, and preview input locked
  through both read and commit phases.
- Instrument metadata is firmware-owned registry data, not file content.
  Basic types are unrestricted; at most two Advanced types may be present;
  Choke enables generic VOICE7 `<base>_choke` lookup. Current flags are Drum
  and Snare Basic, Cymbal Advanced, HiHat Advanced|Choke.
- Canonical HiHat decay file keys are `amp_envelope_decay` and
  `amp_envelope_decay_choke`; accept legacy `amp_envelope_decay_closed/open`
  aliases in storage. Non-Choke slot 6 uses generated Scene setting/morph
  endpoint `slot6_track7_amp_envelope_decay` when its descriptor table has the
  base decay; that alternate is Scene-modulatable as `7dc`.
- Kit scan keeps both display names and FAT short open aliases. If a card only
  exposes a short alias such as `001SLA~1`, scan falls back to the leading
  three-digit slot so the kit remains loadable.
- Large pattern/performance/all files are streamed in bounded chunks and are not staged wholesale in RAM.
- `kitBrowser.c/h` intentionally remains kit-only; pattern/performance/all use typed name loading and direct slot handling.
- Boot path: synchronous polling loop before `audioCodec_init()` (audio not running, blocking OK).
- Globals compatibility (Session 025): current `glo.cfg`/ALL globals span is 23 bytes (`NUM_PARAMS=275`, `PAR_BEGINNING_OF_GLOBALS=252`). Legacy 22-byte globals load silently, then force `PAR_EXT_SYNC=auto` and `PAR_OSC_WAVE_INTERP=1`. Any other globals length uses safe-prefix/default fallback, sanitizes `PAR_MIDI_CHAN_GLOBAL` to 1 if outside 1..16, and shows `old settings` / `check&save .glo` or `.all`.

**SD_init() from `SPI/sd_routines.c` still needed** at boot to bring card to SPI mode (CMD0/CMD1/CMD8/ACMD41). Called before `afatfs_init()`. The rest of sd_routines.c (SD_readSingleBlock, busy-wait loops) is superseded.

**Hardware SPI is impossible** — SD pins have no mapping to any free SPI peripheral on this board, and SPI5/6 are on unbonded TFBGA100 ports. See Session 10 log for full analysis.

**ISR migration path** (future): move `afatfs_poll()` from main loop to TIM5 ISR at priority 6, 10kHz. Everything at priority ≤ 5 preempts it. SD cards tolerate arbitrary SPI clock stretching from preemption. To migrate: init TIM5, move call site, remove main-loop call. No asyncfatfs code changes needed.

**Approaches confirmed wrong** (do not retry):
- Blocking SD calls in main loop — f_open blocks 1–50ms, kills audio
- "Chunking at FatFS-call granularity in main loop" — f_open is still one blocking call, insufficient
- Forking ChaN FatFS for async conversion — 20+ functions, 28 yield points, 6 levels of nesting, unacceptable risk
- NB_FatFS — C++ with heap alloc, lambdas, no FAT16, no _FS_TINY support, 9500 lines

---

## Sample Flash Loading (implemented Session 18)

User samples are installed with explicit modal operations from the Load page:

- `Load:[Samples ]` reads `/samples`, erases sectors 6-11, installs the accepted files, then reads `/loops`, preserves the normal samples, appends as many looped samples as fit, and skips the rest.
- The separate visible `SampLoop` menu item was removed in Session 023; the two existing loaders still run sequentially under the single Samples operation.
- Both loaders accept only mono PCM 16-bit 44.1kHz WAV files. Unsupported files are silently skipped.
- Directory entries are sorted lexicographic by the full long filename when LFN data is present, with ASCII case folded for sort. This is not natural numeric sort; e.g. `Loop 10.wav` sorts before `Loop 2.wav`.
- Installed sample menu labels are compact waveform names: `s01` through `s99`, then `sA0` upward.
- The full encoder-click parameter display shows the filename-derived 8-character display name. Stems longer than 8 characters are compressed as first 4 chars, CGRAM char `0x00`, last 3 chars, preserving case.
- `SampleInfo.size` is now `uint32_t`; the high bit is currently used as `SAMPLE_INFO_LOOP_FLAG`, leaving 31 bits for frame count.
- Metadata and display-name tables live at the top of sample flash; audio payload grows up from `0x08080000`.
- Flash writes use `sampleFlash.c/h`, which hard-rejects sectors below 6 and invalidates D-cache after erase/program.
- Audio is suspended before flash writes and fully reinitialized after. Hardware testing confirmed audio now comes back without reboot after sample load.

Sample flash map:

| Region | Range |
|--------|-------|
| App flash | `0x08008000-0x0807FFFF` |
| Sample data | `0x08080000-0x081FF69F` |
| `SampleInfo[120]` | `0x081FF6A0-0x081FFC3F` |
| Display names[120][8] | `0x081FFC40-0x081FFFFF` |

Important caveat: long samples are not fully solved. `SampleInfo.size` is 32-bit, but oscillator playback still uses the legacy `phase >> 17` index path. Treat long-sample support as unfinished until oscillator phase/index math is widened and hardware tested.

---

## Encoder (SW42)

- **Algorithm**: Dannegger difference — NOT LUT
- **Seed**: `last = new & 3` in `encode_init()` — NOT `(new+3)&3`
- **Divide**: round-toward-zero in `encode_read4()` — NOT `>>= 2`
- **Acceleration**: 8-entry `ts_dirs[]` timestamp buffer, 1×–4× linear multiplier, 100ms decay
- **Rebound suppression**: `ts_dirs[]` majority-direction check — do not remove
- `encode_read1/read2` permanently removed — do not re-add
- When moving `encode_read4()` to any interrupt context: TIM1 (priority 1) can
  preempt lower-priority service code and corrupt `enc_delta` mid-read. The
  current `cpsid/cpsie` guards are acceptable from foreground; an ISR caller
  should use a shadow-copy handoff instead.

---

## Display / Menu

- Full menu system is wired: voice pages, global/MIDI page, load/save page.
- **LCD queue**: head/tail-only SPSC — do NOT reintroduce `lcd_q_count`
- `sendDisplayBuffer` emits `lcd_setcursor` before every data byte
- `buttonHandler_processEvents()`: `if` not `while` — intentional
- **Knob repaint**: `menu_knobs_dirty` + `menu_serviceKnobRepaint()` is RV1-4 only
- **endlessPots**: `atan2f(b, a)` — do NOT change argument order
- Saturation in `menu_encoderChangeParameter` / `menu_handleLoadSaveMenu`: int16 sum + clamp
- MODE/SELECT/VOICE button navigation and LED feedback are wired.
- The cosmetic boot splash sequence is all LEDs → title → menu.
- Canonical `.SND` save length is restored to 236 bytes; short kits load with zero-filled tails.
- Display stability under rapid button mashing depends on the SPSC LCD ring behavior above.
- Multi-knob RV1-RV4 repaint collapse is intentional and should remain scoped to that input path.
- Global `cpu` is a read-only audio queue-free pressure widget, not a general MCU utilization meter.
- Runtime kit/all/performance load completion should use `menu_startSoundApply()` / `preset_tickDrumsetApply()`; do not reintroduce direct `preset_sendDrumsetParameters()` calls in those post-audio completion paths.

---

## DSP / RNG Rules

- **Compiler**: global flags are `-O2 -flto`; DSP source files use the Makefile's more-specific `-Ofast` rule.
- **FPU**: explicitly enabled in `sysclk_init()` via CPACR
- **VLAs**: forbidden in DSP voice files. Use `static int16_t buf[OUTPUT_DMA_SIZE]`. Snare/Cymbal fixed Session 8, HiHat fixed Session 12.
- **`GetRngValue()`**: returns `int16_t`. Explicit `(int16_t)` cast + `& 0x7FFF` mask at every call site.
- **LFO noise**: `lfo->rnd = (float)(GetRngValue() & 0x7FFF) / 32767.0f`
- **`RCC_AHB2ENR`**: address is `0x40023834` — NOT `0x40023830`
- **`RNG_CR`**: direct write `RNG_CR = RNG_CR_RNGEN` — NOT `|=`
- **DTCM**: not DMA-accessible

---

## Boot / Init Order (do not reorder)

```c
EXTI_IMR = 0;          // MUST be first
sysclk_init();
lcd_init();            // MUST be before lcd_tim7_init()
lcd_tim7_init();
encode_init(); din_init(); dout_init(); adc_init(); endlessPots_init();
triggerJacks_init();
dsp_init();
seq_init();            // seeds pattern/sub-step defaults; required before sequencer use
euklid_init();
som_init();
initMidiUart(); usb_init();
filesystem_initCardAndMountBlocking(); // card SPI mode + afatfs mount, pre-audio
menu_init();           // calls memset on parameter_values — do NOT also memset in main()
// Synchronous kit scan via filesystem_requestScanKits + polling
// Synchronous boot normal kit load (root Kit/001 ... directory) via preset_loadDrumset + polling + menu_pollPresetStatus
// Synchronous globals load (GLO.CFG) via preset_loadGlobals + polling + menu_pollPresetStatus
audioCodec_init();     // single audio entry point — AFTER all SD boot ops
sequencerTimer_init(); // TIM3 4kHz sequencer owner — AFTER audioCodec_init()
// main loop: filesystem_tick() + menu_pollPresetStatus() every iteration
// main loop: midi_service() for DIN/USB drain + flush
// main loop: led_processSeqLedState() (NOT seq_tick — TIM3 owns that)
```

### Sequencer / PATGEN Reminders
- Sequencer-to-LED feedback uses `seq_ledState` in `ledHandler.c`; do not reintroduce the removed front-panel parser bridge.
- `led_processSeqLedState()` drains sequencer LED events in the main loop. Do not move it to an ISR without auditing PatternData and LED RMW races.
- `Core/MIDI/frontPanelParser.c/h` is deleted. Do not replace it with a generic bridge; call owner APIs directly.
- New pattern/track/step/automation work should enter through `pat_*` APIs in `Core/Scene/Pattern/PatternData.c/h`.
- Sequencer no longer exposes `seq_patternSet`, `seq_tmpPattern`, or `seq_selectedStep` compatibility names in live code. Playback and recording must use PatternData helpers such as `pat_readStep()`, `pat_getEffectiveTrackLength()`, `pat_recordNote()`, and `pat_eraseMainStepSubSteps()`.
- `pat_tmpPattern` is the remaining active-pattern load buffer. Leave it until the 17th Scene/background-bank-load design replaces it; it should be the only temporary pattern storage needed.
- `seq_offsetTrackStepIndexForRotation()` is a narrow runtime hook used by PatternData; UI code should call `pat_setTrackRotation()`.
- `Core/Scene/Preset/` owns Preset code location. Public names intentionally remain `preset_*`, `parameterArray_*`, and `paramArray_*`; do not rename only part of this API.
- `parameter_values[]` and `parameters2[]` still live in Menu and are known future migration targets for the instrument/file redesign, not a cleanup to do casually.
- Normal kit load is directory-based through root `Kit/`; morph-kit load is
  still legacy `.SND`. Do not collapse those paths until instrument morph
  save/load is designed.
- MIDI notes/channels do not belong in `kitset.kcg` or instrument files. They
  belong in future scene settings.
- `BUTTON_TIMEOUT` is milliseconds on this port (`500u`), not original AVR ticks (`38 * 13.107ms`).
- `EuklidGenerator.c` matches original LXR. PATGEN distribution depends on `__CLZ(0) == 32`; do not replace the shim with `__builtin_clz()`.
- `seq_tick()` is owned by `TIM3_IRQHandler`. **Do NOT add seq_tick() back to the main loop.**
- `midiParser_processRealtimeEvents()` and `triggerJacks_tick()` are also owned by TIM3. Do NOT call them from the main loop.
- `voiceControl_processPending()` is called only inside `audio_check_and_render()` before `mixer_calcNextSampleBlock()`. Do NOT call from ISR context.
- DIN TX FIFO inserts and USB MIDI writes from foreground code must use short critical-section wrappers to avoid corruption from TIM3 clock/note output.

### MIDI / Clock Reminders (Session 019)
- TIM2 is a shared free-running 1 µs counter. Do NOT reset it on each pulse. Use unsigned subtraction for deltas.
- TIM5 is free and reserved for future `afatfs_poll()` ISR migration. Do NOT use it for anything else.
- PAR_EXT_SYNC values in order: `off` / `usb` / `din` / `pls` / `aut`. PAR_BPM minimum is 1; value 0 no longer means external sync.
- CC1 on the global MIDI channel controls MORPH as a 7-bit input to a 0..255 parameter: values 0..126 map to `value * 2`, and 127 maps to 255 so the endpoint is reachable. This is handled in the incoming channel-MIDI path, not in `midiParser_ccHandler()`. Do not add CC1 to the CC handler.
- BAR1/BAR2 use `midiParser_playVoiceMidiNote(voice, vel)`. Do NOT revert to direct `voiceControl_noteOn/Off()`.
- Default voice MIDI notes: Drum1=36 … Drum7=42. Overridden by CC2_MIDI_NOTE per-voice setting.
- RST IN semantics: GPIO input with pull-up; low-to-high rising edge resets pattern position without toggling transport or sending MIDI stop/start.
- MIDI realtime bytes (0xF8/FA/FB/FC) are routed to the MidiRealtime ring in the USART3 ISR and must NOT disturb the channel-message running-status parser state.

---

### Morph / Endless-Pot Reminders
- `preset_morph()` is rate-limited by `preset_morphTick()`. Descriptor Morph now runs per voice from Scene-owned instrument images through `presetMorphEngine`; global `mrp` bulk-sets all six per-voice values, and LFO Morph modulation is a hidden overlay centered on the retained per-voice base.
- Do not add a morph skip cache. The request/pass generation scheduler must send a full final pass at the latest morph value.
- Legacy MorphKit load/save still uses `parameters2[]` and flat `.SND` behavior. Do not treat it as the final descriptor instrument morph persistence path.
- RV1-RV4 are analog endless pots, not the digital Gray-code encoder. The driver uses raw A/B snapshot baselines, `ENDLESS_POT_DEADZONE = 20`, `ENDLESS_POT_TIMEOUT_MS = 5000`, and `ENDLESS_POT_DELTA_TIMEOUT_MS = 20`.
- Only `PAR_MORPH` gets endless-pot double angular speed. Do not apply this to all `DTYPE_0B255`; BPM drift exposed that as too broad.

---

## Known Issues / Technical Debt

### Resolved / Changed in Session 034
- Instrument Load is complete for current instrument types. The root pool holds
  converted `.drm`, `.snr`, `.cym`, and `.hat` files; browser order is
  per-type alphanumeric with a one-based display number saturated at 999.
  New-format root Instrument Save was added in Session 036.
- The visible Load/Save menu no longer exposes Pattern, MorphKit, Perform, and
  All. After Session 036, the promoted top-level type cycler is File/Dir/Kit
  only; Instrument Load is entered by VOICE press on Load, and Instrument Save
  is entered by VOICE press on Save. Kit/Instrument Load scenes use
  `pat_sceneHasActiveSteps()` for LED base state. Kit Load permits a
  zero-or-more Scene toggle mask; Instrument Load/Save selects exactly one
  Scene. A selected Scene blinks.
- `kit_t.instrument_display_name[6][9]` preserves an eight-character Kit
  member stem only for Instrument Load provenance display; it is not runtime
  identity or a second file-authority layer.
- The direct Instrument loader caused the observed Voice 2 long-decay/locked
  parameter condition: loading any Drum file into that slot stayed bad, while
  loading a Kit repaired it. The new staged transaction prevents live Scene
  mutation during I/O and clears/rebuilds all modulation/runtime ownership on
  commit. It also prevents rapid encoder, destination, Scene, mode, or preview
  events from interleaving with a load.
- Still unresolved: Pattern `Step` stores canonical 16-bit destinations but
  `AutomationNode` is still legacy byte CC/CC2 based; migrate before enabling
  descriptor step automation. Direct descriptor LFO writes were repaired in
  Session 035 through InstrumentManager descriptor-domain adapters, but normal
  modulation-node reset/original-value paths still enumerate fixed global
  pools rather than InstrumentManager's dynamic pools.
- Converter follow-up: regenerate instrument values by live descriptor key/ID,
  not a hardcoded descriptor order. The Session 034 investigation found 42
  mismatched finite LFO target pairs among 55 active first pairs in the
  generated pool; this did not cause the long-decay fault, but the files need
  conversion-path repair before trusting their assignment data.

### Resolved / Changed in Session 030
- Phase 2 root filesystem spec began in `FILESYSTEM_SPEC.md`; after the
  Session 032 documentation consolidation and Session 033 closeout, the
  authoritative filesystem and instrument-file spec is
  `knowledge_files/specification_reference/FILESYSTEM_SPEC.md`.
  The old root pointer file was deleted in Session 033.
  Target root layout is `Bank`, `Scene`, `Kit`,
  `Pattern`, `Sample`, `Wavetable`, `Effect`, `Instrument`, plus future root
  `settings.cfg`. At Session 030 this covered root `Kit/` and root
  `Instrument/` load work; later Sessions 035-038 added new-format Kit Save,
  root Instrument Save, KitMrp Save, and InstrumentMrp Save.
- `SD_CARD/Kit/` generated from legacy `Pxxx.SND` files. Folders use preferred
  `NNN Name` convention, for example `004 Moch to`; `_` remains accepted for
  compatibility.
- New `Core/Hardware/SD/storageTypes.c/h` owns Phase 2 kit text schemas,
  numbered-folder parsing, kitset/instrument validation, and instrument
  descriptor-key parsing. After Session 032, instrument values write Scene
  descriptor storage rather than a `ParameterArray` key map. All functions in
  this layer use the `storage_` prefix and must remain independent of
  `asyncfatfs`.
- Normal kit load now reads root `Kit/` directories: scan cache ->
  `kitset.kcg` -> six instrument files. Normal Kit Save writes the same
  directory shape; KitMrp Save writes the same directory shape with current
  interpolated Morph values duplicated into both endpoint sections. Legacy
  `FS_FILE_MORPH` / MorphKit compatibility still uses legacy `.SND`.
- `asyncfatfs` now sets opened handle type from the FAT directory entry so
  opened directories can be entered/scanned reliably.
- Kit scan has a FAT short-alias fallback for space-named folders when LFN
  reconstruction is unavailable.
- MIDI note/channel settings were intentionally removed from `kitset.kcg`; they
  are future scene settings. Directory kit loads leave `PAR_MIDI_NOTE1..7`
  unchanged for now.
- Final hardware status: menu/init directory kit load worked after discovery
  fixes; final short-alias fallback still needs hardware smoke-test.

### Resolved / Changed in Session 032
- Descriptor-backed VOICE pages are populated from
  `Core/DSP/Instruments/*/*Parameters.c` layouts. `menuPages.h` remains the
  static-page table, but instrument cells resolve through InstrumentManager and
  the active Scene slot.
- Instrument files load into Scene descriptor images. `kitset.kcg` owns slot
  type/file/audio route; `[params]` and `[morph]` instrument sections own
  descriptor values.
- `InstrumentManager` owns descriptor registry lookups, dynamic menu layouts,
  and descriptor-to-DSP runtime apply. Direct runtime writes use descriptor
  binds; special non-offset parameters use explicit shaper/setter handling.
- `instrument_decimation` and `velo_mod_amount` are `ROW_NOBIND_IMAGE`
  parameters: morphable, modulatable, and automatable image values without a
  direct struct-offset bind.
- Parser keys allow at least 32 bytes. Session 034 canonicalized HiHat decay
  to `amp_envelope_decay` / `amp_envelope_decay_choke` and retains legacy
  closed/open aliases for compatibility.
- Session 033 resolved the known descriptor Morph and LFO/velocity runtime
  modulation gaps. The remaining descriptor target runtime gap is step
  automation.

### Resolved / Changed in Session 029
- Pattern storage ownership moved further into `Core/Scene/Pattern/PatternData.c/h`. Live code no longer uses `seq_patternSet`, `seq_tmpPattern`, `seq_selectedStep`, `SEQ_DEFAULT_NOTE`, or `SEQ_NEXT_RANDOM*`.
- PatternData now owns staged pattern commit, playback-safe step reads, effective-length reads, live note record mutation, live erase mutation, and legacy file shuffle import through `pat_commitStagedPattern()`, `pat_readStep()`, `pat_getStepProbability()`, `pat_getStepNote()`, `pat_getStepVolume()`, `pat_getEffectiveTrackLength()`, `pat_recordNote()`, `pat_eraseMainStepSubSteps()`, and `pat_setAllShuffle()`.
- Sequencer still owns timing, transport, quantization, runtime step indices, MIDI output, random next-pattern resolution, and recording/erase gates, but storage mutation/readbacks now go through `pat_*` helpers.
- `Core/Preset/` moved to `Core/Scene/Preset/`. `Makefile` uses `-ICore/Scene/Preset` and sources `Core/Scene/Preset/presetManager.c` / `Core/Scene/Preset/ParameterArray.c`. `main.c` includes `ParameterArray.h` directly for `parameterArray_init()`.
- Staging/global audits written: active-pattern load staging stays until the 17th Scene/background-bank-load design; new `filesystem.c` scratch/snapshot buffers and load/save menu polling are intentionally left alone; globals should eventually become canonical scene-level, bank-level, and system-level settings structs.

### Resolved / Changed in Session 028
- `Core/MIDI/frontPanelParser.c/h` removed from live code. Former protocol opcodes are now direct owner calls documented in `knowledge_files/specification_reference/MODULE_INTERCHANGE_SPEC.md`.
- New `Core/Scene/Pattern/PatternData.c/h` owns Pattern storage/edit APIs; Euklid and SOM moved from `Core/Sequencer/` to `Core/Scene/Pattern/`.
- Sequencer LED feedback no longer uses parser callbacks. Sequencer writes `seq_ledState`; foreground `led_processSeqLedState()` performs LED/Menu/Button-aware rendering in ledHandler.
- Sound parameter application is direct through Preset (`preset_applySoundParameter`, `preset_applyVelocityModTarget`, `preset_applyLfoModTarget`); MIDI channel/routing/filter config is direct through MidiParser setters.
- Pattern file serialization reaches PatternData through pointer helpers and uses `PATTERNDATA_STAGING_PATTERN` for active-pattern load staging.

### Resolved / Changed in Session 027
- Runtime kit/all/performance load completion no longer applies all six sound modulation-routing voices in one foreground burst. After audio starts, `menu_startSoundApply()` arms the chunked apply and `menu_tickSoundApply()` lets `preset_tickDrumsetApply()` apply one voice per foreground pass before operation-specific UI/global/pattern follow-up runs. `AUDIO_DMA_FRAMES` remains 96; direct 64-frame latency testing is deferred until this path is hardware-tested.

### Resolved / Changed in Session 023
- CPU scheduling refactor completed: TIM6 keeps only 1ms counters and a foreground service due flag; shift-register exchange, PB jack detect, encoder-button debounce, and endless-pot scanning run from `timebase_serviceFrontPanel()` at about 500Hz.
- LCD servicing reduced to 5kHz at priority 7. `lcd_waitForIdle()` is used only in modal sample/loop load screens after audio has been suspended, so status text fully renders before flash work blocks.
- Slider taper mapping now uses a 4096-entry boot LUT derived from `SLIDER_LOG_TAPER_DB`, removing repeated foreground `powf()` calls.
- Oscillator interpolation is bounded by `OSC_WAVE_INTERP_MAX_ACTIVE=2` in the current test build and limited to audible oscillator waveform targets; user-sample interpolation remains enabled.
- Oscillator-only ITCM is enabled. Filter/distortion ITCM annotations remain present but disabled through `ENABLE_EFFECT_INITCM_CODE=0` after hardware CPU monitor testing looked worse.
- `Load:[Samples ]` now uses the sample/loop flow documented in Sample Flash Loading.
- Main encoder direction reversals clear only sub-detent residue, avoiding the persistent two-click feel after fast direction changes.

### Resolved in Session 022
- ~~24-bit DMA frame LSW forced to 0x0000~~ — **RESOLVED**. `pack_half()` now emits true signed-24 payload in both halfwords. `sample_mx_t` (int32_t) carries signed-24 audio; int16 voice values enter mixer as `int16<<8` via `bufferTool_convertInt16ToSampleMix()`.

### Pending from Session 022 (not resolved)
- **dth global menu option not yet wired** — complete plan in DITHER_AUDIT.md Steps 1–8. Prerequisite infrastructure (`sample_mx_t` path) is now in place. Next step: PAR_16BIT_DITHER enum + menu text + menuPages.h slot + dtype/default + mixer gate.
- **Voice sync-block and distortion widening deferred** — DrumVoice/Snare/CymbalVoice/HiHat/distortion remain int16_t* for now. Pinned plan in DITHER_AUDIT.md. Revisit when voice/distortion gain staging can be validated after widening.
- **Hardware listening test for 24-bit path needed** — loudness regression was fixed analytically (removed extra >>8); hardware confirmation still required.

### Resolved in Session 16
- ~~preset_morph() stub / burst risk~~ — **RESOLVED**. Morph now uses original interpolation and a one-parameter-per-main-loop worker.
- ~~preset_sendModTarget() fall-through bug~~ — **RESOLVED**. `CC_VELO_TARGET` now breaks before `CC_LFO_TARGET`.
- ~~Morph return-to-zero could leave late DSP parameters stale~~ — **RESOLVED**. No skip cache; request/pass generations guarantee a full final pass at latest morph value.
- ~~RV1-RV4 page-change and idle ghost edits~~ — **RESOLVED** by raw A/B snapshots, page-change `snapshotAll()`, post-delta rebaseline, and pre-delta false-start cancellation.
- ~~Global BPM drift from broad endless-pot double-speed rule~~ — **RESOLVED**. Double angular speed now applies only to `PAR_MORPH`.

### Resolved in Session 17
- ~~Preset save/load pattern/all/performance stubs~~ — **RESOLVED**. Kit, MorphKit, Pattern, Performance, All, and Globals now route through typed async filesystem operations.
- ~~Morph-kit load/save not connected~~ — **RESOLVED**. Morph load writes `parameters2[]`; morph save writes interpolated values with mod-target exceptions.
- ~~Slow envelopes from widened DSP block~~ — **RESOLVED**. `OUTPUT_DMA_SIZE` matches the effective LXR-master block size of 32 frames; `AUDIO_DMA_FRAMES` remains 96 for hardware DMA.
- ~~Sequencer/mainboard tick drift from reference~~ — **RESOLVED**. `systick_ticks` is 4kHz again; TIM6 `time_sysTick` remains 1kHz for UI/service timing.
- ~~Screensaver display glitches~~ — **RESOLVED** with explicit LCD off/on phases and clear-on-exit.
- ~~Load-page empty slots and fast-spin name/display races~~ — **RESOLVED** by typed async request tagging and whole-frame LCD queue preflight.

### Resolved in Session 019
- ~~USART3 RX not configured~~ — **RESOLVED**. Interrupt-driven RX/TX, dual TX FIFO, non-blocking.
- ~~DIN TX polled/blocking~~ — **RESOLVED**. TXE interrupt drains realtime FIFO before normal FIFO.
- ~~USB MIDI RX/TX not serviced~~ — **RESOLVED**. midi_service() in main loop.
- ~~BAR1/BAR2 DSP race~~ — **RESOLVED**. Voice trigger pending ring + audio-boundary drain.
- ~~BAR1/BAR2 not through MIDI recording path~~ — **RESOLVED**. midiParser_playVoiceMidiNote() with assigned/default note numbers.
- ~~PD3 EXTI3 diagnostic in production~~ — **RESOLVED**. Real PC13/PD4/PD5 backend.
- ~~EXTI4/EXTI9_5 pointing at Default_Handler~~ — **RESOLVED**. IRQ10/IRQ23 wired.
- ~~seq_tick() in main loop~~ — **RESOLVED**. TIM3 4kHz ISR owns it.
- ~~MIDI realtime bytes processed without timestamp~~ — **RESOLVED**. USART3 ISR timestamps every byte; realtime bytes route to the MidiRealtime ring.
- ~~PAR_BPM=0 external sync toggle~~ — **RESOLVED**. PAR_EXT_SYNC global (SyncInpt).
- ~~CC1 MORPH double-fire~~ — **RESOLVED**. CC1→MORPH in incoming channel path only.
- ~~OUTPUT_DMA_SIZE=16 (2× EG/LFO rate)~~ — **RESOLVED**. Corrected to 32.

### Resolved in Session 020
- ~~Slider-to-parameter mapping not designed~~ — **RESOLVED**. RV5-RV10 now feed dedicated `slider_vol[]` gains and are applied as independent post-voice multipliers in `mixer.c` (not as base `voice.vol` writes).
- ~~Slider zipper from block-edge gain jumps~~ — **MITIGATED**. Per-block interpolation (`last_gain -> current_gain`) added in mixer; log taper mapping added in adc path (`SLIDER_LOG_TAPER_DB`).

### Resolved in Session 18
- ~~SampleMemory.c no-op stub~~ — **RESOLVED**. SampleMemory now validates/caches flash metadata and display names for up to 120 installed samples.
- ~~Sample flash region only proposed~~ — **RESOLVED**. Linker caps app flash at `0x0807FFFF`; sectors 6-11 are reserved for sample storage.
- ~~Load:[Samples] no-op~~ — **RESOLVED**. `/samples` full reinstall and `/loops` append-loop installer are wired through the Load page.
- ~~Audio resume after modal sample writes failed~~ — **RESOLVED**. `audioCodec_suspend()`/`audioCodec_resume()` now fully stop/reset/restart DMA, I2S, and PLLI2S.

### Resolved in Refactor Session
- ~~audioTest.c, sineBufferTest.c, duplicate copy files~~ — **RESOLVED** by consolidation into `AudioCodecManager.c`.
- ~~Scattered DMA ISRs~~ — **RESOLVED**. DMA1 Stream 4 and Stream 7 ISRs now live in `AudioCodecManager.c`.
- ~~audioCodec_packHalf() public wrapper~~ — **RESOLVED**. Removed; ISRs call `pack_audio_half()` directly.

### Resolved in Session 15
- ~~Sequencer step buttons unreliable / no sequenced voices~~ — **RESOLVED**. `BUTTON_TIMEOUT` corrected to 500ms and missing `seq_init()` added at boot.
- ~~Sequencer tempo 4x slow~~ — **RESOLVED** for gross BPM in Session 15; Session 17 restored `systick_ticks` to the original 4kHz LXR mainboard tick while UI millisecond timing stays on `time_sysTick`.
- ~~PATGEN/Euklid writes steps but LEDs do not update~~ — **RESOLVED**. Visible generated main-step LEDs refresh after steps/rotation changes.
- ~~PATGEN/Euklid generated steps front-stacked~~ — **RESOLVED**. `__CLZ` shim now emits ARM `clz`; `__CLZ(0)` returns 32 as original Euklid expects.
- ~~copyClearTools.c frontPanel calls commented out~~ — **RESOLVED**. Direct `seq_clear*` / `seq_copy*` calls wired.
- Euclid and SOM parser backends are wired. Trigger backend was still stubbed at Session 15 and was later resolved in Session 019.

### Resolved in Session 14
- ~~buttonHandler/menu/LED audit connectivity gaps~~ — **RESOLVED** for audit-defined paths. Connections documented in `BUTTONHANDLER_MENU_AUDIT_RESULTS.md` and `LED_AUDIT_SUMMARY.md`.

### Resolved in Session 13
- ~~PAR_VOICE_LFO1-6 not reaching modulation targets during kit load~~ — **RESOLVED**. Preset modulation-target helpers call `modNode_setDestination()` directly.

### Resolved in Session 12
- ~~SD card operations block the main loop~~ — **RESOLVED** by asyncfatfs.
- ~~HiHat VLA stack corruption~~ — **RESOLVED**. Static arrays.
- ~~preset_morph() index 127 memory corruption~~ — **RESOLVED**. Index 127 skipped.

### High Priority
1. **No hi-hat at startup** — boot kit hi-hat is silent; loading any other kit activates it. DSP init ordering issue. VLA bug that masked this is now fixed.
2. ~~Trigger backend still stubbed~~ — **RESOLVED in Session 019 Phase 5**. PC13 CLK OUT, PD4 CLK IN, PD5 RST IN, and trigger PPQ menu wiring are implemented; hardware bench validation still needed.
3. ~~BAR1/BAR2 race condition~~ — **RESOLVED in Session 019 Phase 7**. All voice triggers deferred through voiceControl pending ring; drained at audio boundary.
4. **Long sample playback is not 32-bit clean yet** — `SampleInfo.size` is `uint32_t`, but `Oscillator.c` still derives the address index with the legacy `phase >> 17` path.
5. **MIDI/clock/jack bring-up only** — All Session 019 features are build-verified only. Hardware bench testing required before claiming correctness.
6. **Load/save button display glitch fix NOT YET APPLIED** — Fix is a two-line reorder in `menu_switchPage()` `case LOAD_PAGE:`: update `menu_activePage` BEFORE calling `menu_resetSaveParameters()`. Full details in `LOAD_SAVE_GLITCH_ASSESSMENT.md`. Do not restructure further.

### Medium Priority
5. **ResonantFilter.c double literals** — lines 141, 167: `0.5*in` and `1.0 - f_lp2` cause software double emulation in SVF_calcBlockZDF hot loop. Change to `0.5f` and `1.0f`.
6. **DrumVoice.c VLA** — line 228: `int16_t modBuf[size]` still present, should be static.
7. **BufferTools.c float division** — line 120: `i/(size-1.f)` per sample in hot loop.
8. ~~TIM2 not initialised~~ — **RESOLVED in Session 019**. TIM2 is the shared 1 MHz free-running timestamp source. Do NOT reset on pulse. Do NOT use for SD ISR (TIM5 reserved for that).
9. ~~MidiParser RX not connected~~ — **RESOLVED in Session 019**. Full MIDI in/out including clock, sync, CC1→MORPH, and BAR1/BAR2 MIDI path implemented. Hardware validation pending.
14. Final RV1-RV4 endless-pot noise fix needs long idle hardware soak, especially on global BPM page.
15. Synced LFO tempo still needs audit/fix: current code path has used a hardcoded 130 BPM instead of `seq_getBpm()`.
16. **PAR_EXT_SYNC / PAR_FETCH slot conflict with LXR037** — `PAR_EXT_SYNC` (MIDI auto-sync) occupies the parameter array slot where `PAR_FETCH` lived in LXR037. `.SND`, `.ALL`, `.PRF` files saved on LXR-02 and loaded on an LXR037 (or vice versa) may have that byte misinterpreted. No fix needed now; resolve before any cross-system file interchange.

### Lower Priority
16. Sample loader naming/display/order needs hardware soak after Session 18 LFN changes. Sort is whole-filename lexicographic with ASCII case folded; display names preserve case.
17. SELECT_1 LED dark at boot — enhanced-firmware-only fix
18. ~~Sequencer needs dedicated TIM ISR~~ — **RESOLVED in Session 019**. TIM3 owns sequencer timing.

---

## Failed Approaches — Do Not Retry

- **TIM7 idle gating**: wakeup race → freeze. Session 6.
- **Broad repaint coalescing**: freezes and lag. Session 6.
- **`val >>= 2` in `encode_read4()`**: asymmetric floor divide. Session 7.
- **`last = (new+3)&3` encoder seed**: wrong direction click. Session 4/5.
- **`RNG_CR |= RNG_CR_RNGEN`**: RMW on unpowered peripheral → stall. Session 8.
- **`ready_count` shared RMW in audio queue**: non-atomic. Session 8.
- **VLAs in DSP voice files**: silent stack overflow. Session 8.
- **DSP render in DMA ISR**: hard 2.18ms ceiling, no graceful degradation, system locks on overrun. Session 10.
- **Blocking SD calls in main loop**: f_open blocks 1–50ms, kills audio render. Session 10.
- **SD chunking at FatFS-call granularity in main loop**: f_open is still one blocking call, insufficient. Session 10.
- **Forking ChaN FatFS for async conversion**: 20+ functions need state machine conversion, 28 yield points, 6 levels of call nesting — unacceptable risk and complexity. Session 11.
- **NB_FatFS library**: C++ with heap allocation (new/delete), lambdas, global voidPtr for context, no _FS_TINY support, no FAT16, 9500 lines, designed for hardware DMA callbacks. Session 11.
- **preset_morph() sending index 127 through the old MIDI_CC packing**: `(127+1) & 0x7f = 0` → `paramNr = 0 - 1 = 65535` → wild write to `midiParser_originalCcValues[65536]`. Memory corruption. Must skip index 127. Session 12.
- **VLAs in HiHat_calcSyncBlock**: `int16_t mod1[size], mod2[size]` — silent stack corruption with -O2. Must use static arrays. Session 12.
- **SD_sendCommand() from sdcard_lxr02.c**: Deasserts CS after R1 response — breaks data transfer. Must use send_cmd_keep_cs() instead. Session 12.
- **Two back-to-back filesystem requests**: storage FSM is single-operation. Must wait for completion or ack. Session 12/17.
- **Old CC_VELO_TARGET/CC_LFO_TARGET bridge during kit load**: routing modulation-target changes through the removed parser path did not properly establish modulation targets in the merged single-MCU context. Use Preset modulation-target helpers instead. Session 13.
- **Morph skip cache**: can skip necessary DSP restores after returning to morph 0. Morph must send a complete final pass at the latest value. Session 16.
- **Endless-pot double speed for all `DTYPE_0B255`**: made global BPM drift more visible. Only `PAR_MORPH` gets double angular speed. Session 16.
- **Bare `n == 0` as asyncfatfs EOF test**: `afatfs_fread()` returns 0 while the SD buffer is still being populated — it is not an EOF signal. All EOF checks must use `n == 0 && afatfs_feof(op_file)`. Session 026.

---

## Toolchain

```
arm-none-eabi-gcc -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -O2 -flto
```

DSP source files are compiled by the Makefile's more-specific `-Ofast` rule.

Image format: `[8B "LXRV2IMG"][4B payload size LE][4B checksum LE][payload]`
Boot: hold main encoder button while powering on.
