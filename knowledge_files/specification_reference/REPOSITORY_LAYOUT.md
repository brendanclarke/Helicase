# Repository Layout

```
./
├── MEMORY.md                        ← startup context for agent sessions
├── README.md                        ← confirmed hardware, known issues, critical reminders
├── main.c
├── config.h
├── Makefile
├── STM32F765VIHx_FLASH.ld
├── requirements.txt
├── tools/
│   ├── build_lxrv2_img.py          ← stamps the boot image check and packages the .bin → LXRV2_lxr02.img
│   ├── link_budget.py              ← flash/ITCM/DTCM/arena report after every build
│   ├── decode_devlogs.py           ← decodes /bootlog.bin and /asavetrc.bin
│   ├── gen_stock_tables.py         ← generates Core/DSPAudio/wavetableStock.c (Re3/Sw3) from plots/stock_wave_harmonics.csv; --check (S086)
│   ├── plots/                      ← stock-wave recipe and analysis plots (plot_stock_waves.py, plot_stock_tables.py; numpy/matplotlib) (S086)
│   └── dsp_test/                   ← host test bench (DSP_TEST.md): modules/ (one test per module), spike/ (effects retry kit)
├── build/                           ← generated, not in VCS
├── knowledge_files/
│   ├── SESSION_HANDOFF_TEMPLATE.md ← template for writing new session handoff logs
│   ├── ENHANCED_FEATURES.md        ← future enhancement notes
│   ├── OSC_INTERP_AUDIT.md         ← oscillator interpolation audit
│   ├── specification_reference/
│   │   ├── ASYNCFATFS_REFERENCE.md    ← low-level async FAT/VFAT API contracts, pumping, LFN/object identity, deletion, and caller rules
│   │   ├── AUTOSAVE.md                 ← authoritative hidden A/B format, boot readers, dirty ownership, writer lifecycle, limitations, and validation status
│   │   ├── BANK_PRESET_ARCHITECTURE.md ← parameter storage across Bank/Scene/Kit/Instrument/Effect, Morph, runtime overlays, Scene targets, dirty marking
│   │   ├── COPYCLEAR_UTILITIES.md     ← Phase 6 copy/clear: behaviour, menus, gestures, engines, fan-out, names, trace stage `c` (S075)
│   │   ├── CPU_USE_DSP_AUDIT.md       ← DSP cost audit (S073 section, sound classes) and the ordered optimization record (items 1–26)
│   │   ├── DEV_MODES.md                ← authoritative screen-diagnostic versus file-only logging contract and current log formats
│   │   ├── FILESYSTEM_SPEC.md         ← authoritative product filesystem, kit/instrument files, Scene/Bank storage, and save/load target spec
│   │   ├── EFFECTS_BUS_REFERENCE.md   ← as-built Effect system: registry, resolution, page, FX sequencer, storage, add-a-type tutorial
│   │   ├── EFFECTS_MIXER_DSP_REFERENCE.md ← mixer, FX bus and Effect DSP, output pipeline, DTCM arena contract, costs (S073)
│   │   ├── INSTRUMENTS_DSP_REFERENCE.md ← instrument DSP, parameters, modulation/LFO, costs, how to extend (S073)
│   │   ├── MODULE_INTERCHANGE_SPEC.md ← current direct-call API ownership/boundary map through Session 078
│   │   ├── OSC_INTERP_AUDIT.md        ← oscillator waveform interpolation implementation, persistence, runtime behavior, risks, and validation
│   │   ├── PATTERN_DYNAMIC_STACK.md   ← dynamic Pattern storage, PAT4, Pattern Stack Service, step automation encoding and drain
│   │   ├── REPOSITORY_LAYOUT.md       ← this file
│   │   └── STORAGE_SRAM_MANIFEST.md   ← flash, sample flash and RAM map, linker guards, image check, reservation/approval policy (S073)
│   ├── hardware_archive/
│   │   ├── HARDWARE_MAP.md         ← full confirmed pin table, IRQ numbers and priorities
│   │   ├── AVR_TO_F765_MIGRATION.md ← architectural notes, sequencer ISR design baseline
│   │   ├── FRONTPANEL_AUDIT.md     ← legacy front-panel bridge elimination audit
│   │   ├── SD_CARD_INVESTIGATION.md ← SD false-positive analysis (PA8, 74HC165)
│   │   └── XP_CONNECTOR_MAPS.md   ← ribbon cable pin mappings
│   └── log_archive/
│       ├── 000_SESSION_INDEX.md    ← index of all sessions with keyword lookup
│       ├── 001_SESSION_HANDOFF_LOG.md
│       ├── 002_SESSION_HANDOFF_LOG.md
│       ├── 003_SESSION_HANDOFF_LOG.md
│       └── ...
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
    │   ├── flashImage.c/h           ← boot-time per-sector CRC32 check of the app image (S073)
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
    │   ├── CopyClear/               ← S075 Phase 6 copy/clear: session (routing, range rule, menus, LEDs, suspension), copyOps, clearOps, copyClearService (queue, Pattern engines, register, name buffer, HCNAMES write)
    │   ├── menuEffects.c/h          ← SHIFT+PERF Effect page: SELECT layout, `typ` transaction, SHIFT Morph view, SEQ lock editing/LEDs
    │   └── screensaver.c/h          ← screensaver with explicit LCD off/on phases
    ├── MIDI/                        ← S083 rebuild, single-role modules
    │   ├── MidiDefs.h               ← MidiMsg (6 B, arrival stamp), statuses, CCs, filters, MRT
    │   ├── MidiBuffer.c/h           ← DIN TX lanes and stamped RX buffer
    │   ├── MidiDin.c/h              ← USART3 driver (PB10/PB11)
    │   ├── MidiIn.c/h               ← framing, RXF, S082 drop filter, consume-then-route, MRT
    │   ├── MidiTransport.c/h        ← realtime ring, TIM3 handler, Start/Continue/Stop, SPP, MTC
    │   ├── MidiNoteMap.c/h          ← N1-N5 note map and Q4 TX inverse
    │   ├── MidiNotes.c/h            ← triggers, overlays, CC2, LiveRecord age
    │   ├── MidiControl.c/h          ← NRPN, CC1, value rules, menu-edit writes
    │   ├── MidiLearn.c/h            ← 128-row Learn table, gesture, midilrn.cfg lines
    │   ├── MidiProgram.c/h          ← PC and CC0
    │   ├── MidiOut.c/h              ← all TX, forwarding, framing, no echo
    │   ├── MidiPitch.h              ← default note 63, frequency table, detune
    │   └── MidiVoiceControl.c/h     ← trigger funnel (unchanged)
    ├── Bank/
    │   ├── BankData.c/h             ← resident Bank display name, active Bank-local Scene slot, loaded-bank flag
    │   └── Scene/
    │       ├── SceneData.c/h        ← Scene-owned settings, kit slots, descriptor images, MIDI routing
    │       ├── SceneModTargets.c/h  ← Scene-level target namespace: 1vm..6vm, 390 retired `srt` placeholder (S075), 7dc, 1ou..6ou, 1fx..6fx, fxm (404)
    │       ├── Pattern/
    │       │   ├── PatternData.c/h      ← pattern/track/step storage and edit API
    │       │   ├── EuklidGenerator.c/h  ← pattern generator
    │       │   ├── SomData.c/h          ← SOM data tables
    │       │   └── SomGenerator.c/h     ← SOM pattern/performance generator
    │       └── Preset/
    │           ├── ParameterArray.h/c   ← supersedes Parameters.h; NUM_PARAMS=275
    │           └── presetManager.c/h    ← typed load/save for kit, morph, pattern, performance, all, globals
    ├── DSP/
    │   ├── Effects/
    │   │   ├── EffectTypes.h          ← retained 420 B Effect record contract, block-7 target IDs
    │   │   ├── EffectsManager.c/h     ← registry (off/flt), per-block resolution, edit API + fan-out, FX-seq, automation/LFO overlays
    │   │   ├── EffectParamRows.h      ← common/type descriptor row macros
    │   │   ├── FxBuffer.c/h           ← DTCM `.dtcm_fxbuf` arena, voice units, Effect share, handoff record
    │   │   └── StereoFilter/          ← `flt` descriptors (…Parameters.c/h) and runtime/ops (…Effect.c/h, -Ofast)
    │   └── Instruments/
    │       ├── InstrumentManager.c/h ← descriptor registry, VOICE menu lookup, Scene-to-DSP apply bridge
    │       ├── Drum/                 ← Drum descriptor keys, flags, menu layout, runtime metadata
    │       ├── Snare/                ← Snare descriptor keys, flags, menu layout, runtime metadata
    │       ├── Cymbal/               ← Cymbal descriptor keys, flags, menu layout, runtime metadata
    │       └── HiHat/                ← HiHat/open-hat descriptor keys, flags, menu layout, runtime metadata
    ├── SampleRom/
    │   ├── SampleMemory.c/h         ← sample flash metadata/runtime cache, 128 entries, catalogue v2 flags
    │   └── sampleFlash.c/h          ← guarded F765 sector 7-11 erase/program helpers
    ├── Sequencer/
    │   ├── sequencerTimer.c/h       ← TIM3 4kHz sequencer timing owner (IRQ29, priority 2) — Session 019
    │   ├── sequencer.c/h            ← original LXR sequencer source (driven by TIM3_IRQHandler); FX step latch, Effect automation markers
    │   ├── StepScale.c/h            ← shared 14-entry 96-PPQ step-scale table (tracks + FX sequencer)
    │   ├── clockSync.c/h
    ├── DSPAudio/
    │   ├── random.c/h               ← F765 RNG port (PLL48CLK, bare register)
    │   ├── Oscillator.c/h           ← oscillator loops, dispatchers, S085 sync/ring family, S086 pos helpers (OscInfo padding)
    │   ├── wavetable.c/h            ← sine, Tri/Saw/Rec [11][1024] tables (externs incl. the stock waves)
    │   ├── wavetableStock.c         ← GENERATED Re3/Sw3 tables (tools/gen_stock_tables.py) (S086)
    │   ├── Samples.c/h              ← crashSample[32769] (32,768 points + guard)
    │   └── [all DSP voice files]    ← ported; mixer_calcNextSampleBlock wired to AudioCodecManager
    └── compat/
        ├── stm32f4xx.h              ← vestigial-include shim via <stdint.h>
        └── cmsis_intrinsics.h
```
