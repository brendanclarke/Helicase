# LXR-02 Open-Source Firmware 
## A functional port of Sonic Potions LXR Software, version 0.37
### Introduction
The LXR02 is a digital drum synthesizer produced in collaboration with Sonic Potions and Erica Synths. It is based on a 32-bit Cortex-M7 processor. If you want to go straight to the firmware, it is './build/LXRV2_lxr02.img'. Put this in the root directory of the SD card and power on while pressing the encoder, as you would for any firmware update. You can switch between this and the Erica Synths firmware any time with this method. 

This repository ports the original firmware written for the Sonic Potions LXR Drumsynth to the LXR02 hardware. Because the original LXR was based on a dual-processor design using an Atmega644 8-bit processor and a Cortex-M4, much of the underlying code has changed, and most of the hardware drivers are new:
- All hardware read/writes happen on asynchronously draining queues, including LCD refreshes.
- SD card read/writes use https://github.com/thenickdude/asyncfatfs. One of my favorite things about the port. Thanks to Nick for making some of that background magic happen. 
- Memory mapping is updated for the M7, and there is 1.5MB available for sample storage in flash.
- The mixbus and output buffers use the full 24-bit width of the DACs. 

Other than that, the firware is designed to work exactly as on the Sonic Potions LXR, version 0.37, all files fully intercompatible, with the following additions/differences:
- The voice faders work. The log curve can be changed in config.h
- The "STEP" mode button is labeled "LOAD" on the LXR02. Pressing "LOAD" gets you step mode, as per 0.37
- The "< BAR >" buttons trigger the selected voice at 127/64 velocity and can be recorded. 
- The sample loading option is updated. You can add two directories to the root of the SD card: 'samples' and 'loops'. You can have up to 120 44.1kHz/16-bit files between the directories. They will both be loaded into flash when the option is selected, 'samples' are 1-shot and 'loops' play looped, always. The trucated filename will also show when you click in to the waveform parameter with the encoder on the OSC page.
- Global menu changes: the non-functional trigger jack output options are removed. Two new parameters are there: Oscillator Interpolation and a CPU monitor. Oscillator Interpolation interpolates between waveforms when they are automated with an LFO. It works for the main voice oscillators (not the FM oscillators), and there are two dynamically-assigned slots for this. This can be changed in config.h if you like.  

Enjoy! If you find any bugs or have an idea or make some cool music, feel free to join the Discord server: https://discord.gg/sWjGWuavUX

And if you want to support the absurd nonsense I get up to in general: https://patreon.com/voskomm

### Some notes for developers or prospective developers
This repository is designed to be as LLM-friendly as I could make it so that adding features would be easy for everyone. The whole sausage-making process of the hardware trace, bring-up, driver wrangling, and application port is there in the session logs. The idea is that you can start a session with something like:
 "The goal of this session is to implement < some feature >. Read @README.md and @MEMORY.md for project context and any further files as necessary, then write a plan of implementation with possible conflicts and risk factors to the root directory as < some feature >_AUDIT.md." 
And then the LLM will grab the context it needs as required. Then you read the plan, work through it, and write back these files and the logs when you're done. There's verbose logs, a template for that, and a lightweight log index to keep the context sorta-manageable.  
The build requirements are pretty lightweight, too. See 'requirements.txt'. You just need gcc, make, and python 3. If you don't want to deal with that, you can probably just drop the whole zip file into an LLM and make it build an .img for you. LLM-stuff starts below the fold. Have fun!
Brendan
brendanpaulclarke@gmail.com
https://brendanclarke.com
_______

## Repository
**Structure**
- **Working firmware:** primary branch `LXR02Open-prime`; repository root is the working tree root
- **Git history:** project became a git repository after Session 023
- **Original open-source LXR source:** available read-only at `knowledge_files/LXR-master/` — must not be modified

**Session Logs**
- Sessions 15-23 are documented in `knowledge_files/log_archive/015_SESSION_HANDOFF_LOG.md` through `knowledge_files/log_archive/023_SESSION_HANDOFF_LOG.md`.
- Session 023 refactored CPU scheduling and DSP hot paths: TIM6 front-panel work moved to foreground service at 500Hz, TIM7 LCD drain changed to 5kHz/low priority, idle filesystem polling is rate-limited, slider log taper uses a 4096-entry LUT, oscillator interpolation is budget-limited, oscillator-only ITCM is enabled, sample+loop loading is one menu operation, sample-load LCD transitions are cleaned up, and the main encoder direction-change residue bug is mitigated.
- This folder is the repository/codebase. `knowledge_files/LXR-master/` is read-only reference material and must not be modified.
- Original open-source LXR source: available locally at `knowledge_files/LXR-master/`

## Boot Process
1. LXR-02 bootloader (LXRV2) loads from flash
2. Bootloader reads SD card for `LXRV2_lxr02.img`
3. Image format: `[8B magic "LXRV2IMG"][4B payload size LE][4B checksum LE][payload]`
4. App loaded at 0x08008000, SP=0x20080000
5. Boot by holding main encoder button while powering on
6. Packager: `tools/build_lxrv2_img.py`

## Toolchain
```
arm-none-eabi-gcc -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard
make && make img  → build/LXRV2_lxr02.img
```

## Clock Configuration (confirmed)
- HSE = 16MHz (ZQ1 crystal confirmed)
- SYSCLK = 216MHz: PLLM=16, PLLN=432, PLLP=2
- PCLK1 = 54MHz (APB1/4)
- PCLK2 = 108MHz (APB2/2)
- PLL48CLK = 48MHz (PLLQ=9) — USB
- PLLI2S: N=271, R=2 → 135.5MHz → Fs=44108Hz
- RCC_DCKCFGR2 (0x40023890): CLK48SEL=00 written explicitly

## Directory Structure
```
./
├── main.c
├── config.h
├── STM32F765VIHx_FLASH.ld
├── Makefile
├── tools/build_lxrv2_img.py
└── Core/
    ├── Src/
    │   └── startup_stm32f765xx.s
    ├── Hardware/
    │   ├── clocks.c/h
    │   ├── timebase.c/h         ← SysTick 4kHz mainboard tick, TIM6 1kHz counters + 500Hz foreground service, TIM7 5kHz LCD ISR
    │   ├── AudioCodecManager.c/h ← consolidated audio: DMA ISRs, I2S/GPIO/DMA init, SPSC queue
    │   ├── triggerJacks.c/h     ← CLK OUT/IN, RST IN, OUT1 detect EXTI9_5
    │   ├── memtest.c/h          ← flash sector probe (boot-time, gated by MEMTEST_ENABLED)
    │   ├── frontPanel/
    │   │   ├── buttonHandler.c/h ← ISR-safe event ring, main-loop processEvents()
    │   │   ├── lcd.c/h           ← TIM7-driven async queue, 128 entries, SPSC ring
    │   │   ├── ledHandler.c/h
    │   │   └── IO/
    │   │       ├── adcPots.c/h   ← sliders RV5-10, ADC1 DMA
    │   │       ├── din.c/h       ← 74HC165×5 buttons, SPI1
    │   │       ├── dout.c/h      ← 74HC595×5 LEDs, SPI1
    │   │       ├── encoder.c/h   ← SW42, TIM1 IC, Dannegger, accel + rebound suppression
    │   │       └── endlessPots.c/h ← RV1-4, atan2 delta tracking (refactored)
    │   ├── SD/
    │   │   ├── filesystem.c/h    ← public facade: typed async load/save/name/scan operations
    │   │   ├── kitBrowser.c/h    ← kit-only 128-slot gap-tolerant browser
    │   │   ├── SPI/
    │   │   │   ├── spi_sd.c/h    ← bit-bang SPI: PC12/PD2/PC8/PD0
    │   │   │   └── sd_routines.c/h ← SD_init() only; blocking read/write superseded
    │   │   └── asyncfatfs/
    │   │       ├── asyncfatfs.c/h ← Betaflight asyncfatfs (modified for LXR-02)
    │   │       ├── fat_standard.c/h
    │   │       ├── sdcard.h
    │   │       └── sdcard_lxr02.c/h ← SD block-device shim over bit-bang SPI
    │   └── USB/
    │       ├── OTG_Driver/
    │       ├── Device_Library/
    │       └── App/              ← usb_manager, usb_midi_core, etc.
    ├── Menu/
    │   ├── menu.c/h              ← full port, all pages, load/save UI
    │   ├── menuPages.h           ← 16-page × 8-subpage table
    │   ├── MenuText.h            ← all label strings
    │   ├── Cc2Text.c             ← modTargets[] 205 entries
    │   ├── CcNr2Text.h
    │   ├── copyClearTools.c/h    ← copy/clear tools; direct seq_* calls wired Session 15
    │   └── screensaver.c/h       ← screensaver with explicit LCD off/on phases
    ├── Preset/
    │   ├── ParameterArray.h/c   ← supersedes Parameters.h; NUM_PARAMS=273
    │   └── presetManager.c/h    ← typed load/save for kit, morph, pattern, performance, all, globals
    ├── MIDI/
    │   ├── Uart.c/h              ← USART3, 31250 baud
    │   ├── FIFO.c/h
    │   ├── MidiMessages.h        ← full mainboard version (MIDI_NRPN_* prefix to avoid collision)
    │   ├── MidiNoteNumbers.h
    │   ├── MidiParser.c/h
    │   ├── MidiVoiceControl.c/h
    │   ├── SeqStep.h
    │   ├── frontPanelParser.c/h  ← local dispatcher + sequencer/front-panel bridge
    │   └── valueShaper.h
    ├── SampleRom/
    │   ├── SampleMemory.c/h      ← sample metadata/runtime cache, 120 entries, loop flags
    │   └── sampleFlash.c/h       ← guarded F765 sector 6-11 erase/program helpers
    ├── Sequencer/
    │   ├── sequencerTimer.c/h    ← TIM3 4kHz sequencer timing owner (IRQ29, priority 2)
    │   ├── sequencer.c/h         ← original LXR sequencer source (driven by TIM3_IRQHandler)
    │   ├── EuklidGenerator.c/h   ← original LXR euclid generator source
    │   ├── SomData.c/h           ← original LXR SOM data tables
    │   ├── SomGenerator.c/h      ← original LXR SOM generator source
    │   ├── clockSync.c/h
    ├── DSPAudio/
    │   ├── random.c/h            ← F765 RNG port (PLL48CLK, bare register)
    │   ├── AudioCodecManager → see Core/Hardware/AudioCodecManager.c/h
    │   └── [all DSP voice files] ← ported; mixer_calcNextSampleBlock wired to AudioCodecManager
    └── compat/
        ├── stm32f4xx.h           ← vestigial-include shim via <stdint.h>
        └── cmsis_intrinsics.h
```

## Confirmed Working Hardware
- ✅ LCD 4-bit parallel (PE7-PE12), TIM7 async driver
- ✅ LEDs — 74HC595×5 via SPI1
- ✅ Buttons — 74HC165×5 via SPI1 (40 inputs, 1kHz poll, event ring)
- ✅ SW43 SHIFT/BAR1 button (PB7) and LED (PB8)
- ✅ Main encoder SW42 (TIM1 IC, PE13/PE14, Dannegger + acceleration + rebound suppression)
- ✅ Endless pots RV1-4 (ADC1 DMA, atan2 delta tracking, refactored)
- ✅ Sliders RV5-10 (ADC1 DMA, PA0-5)
- ✅ Audio DAC1 (CS4344, I2S3, PA15/PB5/PC7/PC10) — 24-bit, true signed-24 payload (Session 022)
- ✅ Audio DAC2 (CS4344, I2S2, PB12-13/PB15/PC6) — 24-bit, true signed-24 payload (Session 022)
- ✅ MIDI DIN RX/TX (USART3, PB10 TX / PB11 RX, 31250 baud, interrupt-driven dual FIFO)
- ✅ USB MIDI (OTG_FS, PA11/PA12, enumerates as "Sonic Potions USB MIDI")
- ✅ SD card SPI bit-bang (PC12/PD2/PC8/PD0, SDHC confirmed)
- ✅ CLK OUT jack (PC13)
- ✅ CLK IN jack (PD4, active LOW via VT1, EXTI4)
- ✅ RST IN jack (PD5, active LOW via VT2, EXTI5)
- ✅ OUT1 L/R jack detect (PD6/PD7, no plug=LOW, plug inserted=HIGH, EXTI9_5 both edges)
- ✅ OUT2 L/R jack detect (PB4/PB6, no plug=LOW, plug inserted=HIGH, sampled by the TIM6-scheduled foreground service)
- ✅ Full menu system: all voice pages, global/MIDI page, load/save page
- ✅ Kit load from SD on boot (P000.SND + GLO.CFG)
- ✅ Typed load/save from menu: kit, morph kit, pattern, performance, all, globals (async via filesystem facade)
- ✅ Canonical .SND save length restored to 236 bytes; short kits load with zero-filled tails
- ✅ MODE/SELECT/VOICE button navigation with LED feedback
- ✅ Display stability under rapid button mashing (SPSC ring fix)
- ✅ Multi-knob simultaneous spin (RV1-4 repaint collapse)
- ✅ Audio — zero startup underruns (boot SD ops complete before audio starts); small burst during kit load from menu
- ✅ I-Cache enabled (16KB, ICIALLU invalidate) — Session 13
- ✅ D-Cache enabled (16KB) with MPU (WT for SRAM, SO for DMA buffers) — Session 13
- ✅ DMA buffers in .dma_nocache linker section (Strongly-Ordered via MPU) — Session 13
- ✅ audioOutBuffer in DTCM (INDTCMZ, single-cycle access) — Session 13
- ✅ -Ofast for DSP files, -flto — Session 13
- ✅ Flash sector layout probed: sectors 5-11 BLANK, app in sector 2, single-bank confirmed
- ✅ Cosmetic boot splash sequence (all LEDs → title → menu)
- ✅ Sequencer step entry/playback, gross BPM timing, and PATGEN/Euklid LED feedback fixed in Session 15
- ✅ Parameter morph engine implemented in Session 16; morph-kit load/save wired in Session 17
- ✅ Global `cpu` read-only widget (DWT queue-free pressure meter)
- ✅ RV1-RV4 endless-pot page-change baselining, deadzone, and BPM drift fixes in Session 16
- ✅ User sample flash loading from `/samples` and append-loop loading from `/loops` in Session 18
- ✅ Audio suspend/resume around modal sample flash writes in Session 18
- ✅ Session 023 combined sample and loop installation under one `Load: Samples` menu item; `/samples` installs first, `/loops` appends next, and audio resumes once after both stages.
- ✅ Session 023 CPU refactor: slider LUT, foreground front-panel service, 5kHz low-priority LCD drain, idle filesystem polling limit, oscillator interpolation budget, oscillator-only ITCM, and mixer/oscillator hot-path cleanup.

## Known Issues / Technical Debt

### Resolved in Session 12
- ~~SD card operations block the main loop~~ — **RESOLVED** by asyncfatfs integration. Boot loads synchronous (pre-audio). Post-boot loads async via `filesystem_tick()`.
- ~~HiHat VLA stack corruption~~ — **RESOLVED**. `mod1[size]`/`mod2[size]` replaced with `static int16_t mod1[OUTPUT_DMA_SIZE]`.
- ~~preset_morph() index 127 memory corruption~~ — **RESOLVED**. Index 127 skipped (uint16 underflow in midiParser_ccHandler → wild write).

### Resolved in Session 13
- ~~PAR_VOICE_LFO1-6 not reaching modulation targets during kit load~~ — **RESOLVED**. `preset_sendModTarget()` calls `modNode_setDestination()` directly.

### Resolved / Changed in Session 023
- CPU scheduling refactor completed: TIM6 now keeps only counters and a foreground service due flag in the ISR; shift-register exchange, PB jack detect, encoder-button debounce, and endless-pot scanning run from `timebase_serviceFrontPanel()`.
- LCD drain is now TIM7 5kHz at low priority; rare modal storage screens use `lcd_waitForIdle()` so flash writes do not interrupt half-rendered status text.
- Slider log taper uses a 4096-entry LUT derived from `SLIDER_LOG_TAPER_DB`, avoiding foreground `powf()` calls while preserving the configured curve.
- Oscillator interpolation is budget-limited by `OSC_WAVE_INTERP_MAX_ACTIVE=2` in the current test build, user-sample interpolation remains enabled, and oscillator block paths are the only ITCM-enabled DSP code.
- Sample and loop installers are combined under `Load:[Samples ]`: `/samples` installs first and `/loops` appends second; the old visible `SampLoop` menu entry was removed.
- Main encoder direction-change partial residue is cleared so fast direction reversals do not require an extra detent.

### Resolved in Session 14
- ~~buttonHandler/menu/LED audit connectivity gaps~~ — **RESOLVED** for audit-defined paths. Connections are documented in `BUTTONHANDLER_MENU_AUDIT_RESULTS.md` and `LED_AUDIT_SUMMARY.md`.

### Resolved in Session 15
- ~~Sequencer step buttons unreliable / no sequenced voices~~ — **RESOLVED**. `BUTTON_TIMEOUT` corrected to 500ms and missing `seq_init()` added at boot.
- ~~Sequencer tempo 4x slow~~ — **RESOLVED** for gross BPM in Session 15; Session 17 restored `systick_ticks` to the original 4kHz LXR mainboard tick while UI millisecond timing stays on `time_sysTick`.
- ~~PATGEN/Euklid writes steps but LEDs do not update~~ — **RESOLVED**. Visible generated main-step LEDs refresh after steps/rotation changes.
- ~~PATGEN/Euklid generated steps front-stacked~~ — **RESOLVED**. `__CLZ` shim now emits ARM `clz`; `__CLZ(0)` returns 32 as original Euklid expects.
- ~~copyClearTools.c frontPanel calls commented out~~ — **RESOLVED**. Direct `seq_clear*` / `seq_copy*` calls wired.
- Euclid and SOM parser backends are wired. Trigger backend remains stubbed.

### Resolved in Session 16
- ~~preset_morph() stub / burst risk~~ — **RESOLVED**. Morph now uses original interpolation and a one-parameter-per-main-loop worker.
- ~~preset_sendModTarget() fall-through bug~~ — **RESOLVED**. `CC_VELO_TARGET` now breaks before `CC_LFO_TARGET`.
- ~~Morph return-to-zero could leave late DSP parameters stale~~ — **RESOLVED**. No skip cache; request/pass generations guarantee a full final pass at latest morph value.
- ~~RV1-RV4 page-change and idle ghost edits~~ — **RESOLVED** by raw A/B snapshots, page-change `snapshotAll()`, post-delta rebaseline, and pre-delta false-start cancellation.
- ~~Global BPM drift from broad endless-pot double-speed rule~~ — **RESOLVED**. Double angular speed now applies only to `PAR_MORPH`, not all `DTYPE_0B255`.

### Resolved in Session 17
- ~~Preset save/load pattern/all/performance stubs~~ — **RESOLVED**. Kit, MorphKit, Pattern, Performance, All, and Globals now route through typed async filesystem operations.
- ~~Morph-kit load/save not connected~~ — **RESOLVED**. Morph load writes `parameters2[]`; morph save writes interpolated values with mod-target exceptions.
- ~~Slow envelopes from widened DSP block~~ — **RESOLVED**. `OUTPUT_DMA_SIZE` matches the effective LXR-master block size of 32 frames; `AUDIO_DMA_FRAMES` remains 96 for hardware DMA.
- ~~Sequencer/mainboard tick drift from reference~~ — **RESOLVED**. `systick_ticks` is 4kHz again; TIM6 `time_sysTick` remains 1kHz for UI/service timing.
- ~~Screensaver display glitches~~ — **RESOLVED** with explicit LCD off/on phases and clear-on-exit.
- ~~Load-page empty slots and fast-spin name/display races~~ — **RESOLVED** by typed async request tagging and whole-frame LCD queue preflight.

### Resolved in Session 019
- ~~USART3 RX not configured~~ — **RESOLVED**. PB11 configured as USART3_RX; interrupt-driven RXNE; DIN MIDI in now feeds midiParser.
- ~~DIN TX polled/blocking~~ — **RESOLVED**. Dual TX FIFO (realtime priority + normal); TXE interrupt drains; uart_sendMidiByte() non-blocking.
- ~~USB MIDI RX not consumed from main loop~~ — **RESOLVED**. midi_service() calls usb_getMidi() up to 8 messages per pass.
- ~~USB MIDI TX not flushed~~ — **RESOLVED**. midi_service() calls usb_tick() each pass.
- ~~BAR1/BAR2 race condition~~ — **RESOLVED**. All triggers deferred through voiceControl pending ring; drained at audio boundary only.
- ~~BAR1/BAR2 not recording through MIDI path~~ — **RESOLVED**. midiParser_playVoiceMidiNote() used; records with assigned/default note numbers.
- ~~PD3 EXTI3 diagnostic in production~~ — **RESOLVED**. Replaced with real PC13/PD4/PD5 backend.
- ~~EXTI4/EXTI9_5 vectors pointing at Default_Handler~~ — **RESOLVED**. IRQ10 → EXTI4_IRQHandler, IRQ23 → EXTI9_5_IRQHandler.
- ~~seq_tick() in main loop~~ — **RESOLVED**. TIM3 4kHz ISR owns seq_tick(), processRealtimeEvents(), and triggerJacks_tick().
- ~~MIDI realtime bytes processed without timestamp~~ — **RESOLVED**. USART3 ISR timestamps every byte; realtime bytes routed to MidiRealtime ring.
- ~~PAR_BPM=0 as external sync toggle~~ — **RESOLVED**. PAR_EXT_SYNC global (SyncInpt: off/usb/din/pls/aut) owns sync source.
- ~~CC1 mod wheel MORPH stuck at ~127~~ — **RESOLVED**. CC1→MORPH in incoming channel path only; midiParser_ccHandler ignores CC1.
- ~~OUTPUT_DMA_SIZE=16 (2× reference EG/LFO rate)~~ — **RESOLVED**. Corrected to 32 matching LXR-master block size.

### Resolved in Session 020
- ~~Slider-to-parameter mapping not designed~~ — **RESOLVED**. RV5-RV10 now feed dedicated `slider_vol[]` gains and are applied as independent post-voice multipliers in `mixer.c` (not as base `voice.vol` writes).
- ~~Slider zipper from block-edge gain jumps~~ — **MITIGATED**. Per-block interpolation (`last_gain -> current_gain`) added in mixer; log taper mapping added in adc path (`SLIDER_LOG_TAPER_DB`).

### Resolved in Session 18
- ~~SampleRom no-op stub~~ — **RESOLVED**. SampleMemory now scans/validates flash metadata, caches 120 entries and 8-char display names, and feeds installed samples to oscillator waveform values.
- ~~Sample flash region only proposed~~ — **RESOLVED**. Linker now caps app flash at `0x0807FFFF` and reserves sectors 6-11 (`0x08080000-0x081FFFFF`) for user samples.
- ~~Load:[Samples] no-op~~ — **RESOLVED**. `Load:[Samples ]` now wipes/reinstalls `/samples` and then appends looped `/loops` entries sequentially.
- ~~Audio failed to return after sample load~~ — **RESOLVED** by full I2S/DMA/PLLI2S suspend/resume framework.

### Resolved in Refactor Session
- ~~audioTest.c, sineBufferTest.c, duplicate copy files~~ — **RESOLVED** by consolidation into AudioCodecManager.c
- ~~Scattered DMA ISRs~~ — **RESOLVED**, both Stream 4 and Stream 7 ISRs now in AudioCodecManager.c
- ~~audioCodec_packHalf() public wrapper~~ — **RESOLVED**, removed; ISRs call pack_audio_half() directly

### Audio / RNG
- `GetRngValue()` calls must be masked: `uint16_t rnd = (int16_t)(GetRngValue() & 0x7FFF)`. Root cause not fully investigated; masking is the confirmed working fix. Note for future investigation.
- Sequencer gross BPM is fixed and `systick_ticks` is back to the original 4kHz timebase; final clock/jitter still needs hardware validation.
- `__CLZ(0)` must return 32. PATGEN/Euklid distribution depends on original ARM CLZ semantics; do not replace the shim with `__builtin_clz()`.
- Reverse sequencer `SEQ_CC` feedback must use `seq_notifyFront()`, not `frontPanel_sendData()`, because original two-chip command values collide by direction.

### High Priority
1. **No hi-hat at startup**: boot kit hi-hat is silent; loading any other kit activates it. DSP init ordering issue, not yet investigated.
2. ~~Trigger backend still stubbed~~ — **RESOLVED in Session 019 Phase 5**. PC13 CLK OUT, PD4 CLK IN, PD5 RST IN, and trigger PPQ menu wiring are implemented; hardware timing still needs bench validation.
3. ~~BAR1/BAR2 race condition~~ — **RESOLVED in Session 019 Phase 7**. All voice triggers now deferred through voiceControl pending ring; drained at audio boundary.
4. **Long sample playback is not actually 32-bit clean yet**: `SampleInfo.size` is `uint32_t`, but `Oscillator.c` still derives the sample index from the legacy 32-bit phase path (`phase >> 17`). Practical direct addressing is still roughly 32768 samples until the oscillator phase/index math is spiked and widened.
5. **MIDI/clock/jack bring-up only**: All Session 019 MIDI, realtime clock, CLK IN/OUT, RST IN, and sync features are build-verified but have NOT been bench-tested on hardware. Full functional verification is the top priority for the next session.

### Medium Priority
5. **ResonantFilter.c double literals** — lines 141, 167: `0.5*in` and `1.0 - f_lp2` cause software double emulation in the hottest per-sample SVF loop. Change to `0.5f` / `1.0f`. Identified Session 13.
6. **DrumVoice.c VLA** — line 228: `int16_t modBuf[size]` still present, should be static. Identified Session 13.
7. **BufferTools.c float division** — line 120: `i/(size-1.f)` per sample in hot loop. Identified Session 13.
8. ~~TIM2 not initialised~~ — **RESOLVED in Session 019**. TIM2 is the shared 1 MHz free-running timestamp source for MIDI realtime and trigger-jack capture. Do NOT reset on pulse; use unsigned delta subtraction. Do NOT use TIM2 for SD ISR.
9. ~~MidiParser RX not connected~~ — **RESOLVED in Session 019 Phase 1**. Full MIDI in/out including clock, sync, BAR1/BAR2 MIDI path, and CC1→MORPH implemented; hardware bench validation still required.
14. Final RV1-RV4 endless-pot noise fix needs long idle hardware soak, especially on global BPM page.
15. Synced LFO tempo still needs audit/fix: current code path has used a hardcoded 130 BPM instead of `seq_getBpm()`.

### Lower Priority / Future
16. Sample display/name polish after hardware soak: Session 18 uses whole-filename lexicographic ordering with ASCII case folded for sort, stores case-sensitive 8-character display names, and abbreviates long stems as first 4 + CGRAM `0x00` + last 3.
17. SELECT_1 LED dark at boot — faithful reproduction of original LXR bug. Enhanced-firmware-only fix: add `led_setActiveSelectButton(0)` after `menu_switchPage(VOICE1_PAGE)` in menu_init().
18. Non-blocking LCD: lcd_wait_ms() in lcd_init() is still blocking but only runs pre-TIM7. Safe as-is.

## SD Card Architecture (implemented Sessions 12 and 17)

**asyncfatfs** (Betaflight library) replaces ChaN FatFS. Non-blocking polling-based I/O is fronted by `filesystem.c/h`, which is the only SD API non-SD clients should include.

```
presetManager.c / kitBrowser.c
  → filesystem.c (typed operations: kit/morph/pattern/performance/all/globals)
    → asyncfatfs/asyncfatfs.c (afatfs_fopen/fread/fwrite/fclose/poll)
      → asyncfatfs/sdcard_lxr02.c (sector transfer FSM, 16 bytes/burst)
        → SPI/spi_sd.c (bit-bang SPI)
```

Boot path: synchronous polling before `audioCodec_init()`. Post-boot: `filesystem_tick()` in main loop, non-blocking. Session 023 keeps active operations polling every pass but rate-limits idle polling to reduce background foreground load.

`filesystem.c` contains the filetype registry and the add-a-filetype checklist. `kitBrowser.c` intentionally remains kit-only; pattern/performance/all use typed name loading and direct slot handling.

**Future**: Move `afatfs_poll()` from main loop to TIM5 ISR at priority 6, 10kHz. No asyncfatfs code changes needed — just move the call site.

Current Session 023 service priority table:

| IRQ | Priority | Handler |
|-----|----------|---------|
| TIM1_CC (IRQ27) | 1 | Encoder IC |
| TIM3 (IRQ29) | 2 | Sequencer timing owner |
| EXTI4 / EXTI9_5 (IRQ10/23) | 3 | CLK/RST/OUT1 edge timestamping |
| DMA1_S4 (IRQ15) | 4 | Audio pack master |
| DMA1_S7 (IRQ47) | 4 | Audio pack slave |
| USART3 (IRQ39) | 5 | DIN MIDI RX/TX |
| OTG_FS (IRQ67) | 5 | USB MIDI |
| TIM6_DAC (IRQ54) | 6 | 1kHz counters + foreground front-panel service flag |
| TIM7 (IRQ55) | 7 | 5kHz LCD drain |

## Critical Reminders

### General Process
- **ALWAYS verify the local working repository directory before writing any code**
- GetRngValue() calls must mask the result: `& 0x7FFF` — do not remove this masking
- **1ms blocking anywhere in the main loop or any ISR at priority ≤ 4 is completely unacceptable**

### Internal DAC — Must Never Be Enabled
- PA4 (ADC1_IN4) and PA5 (ADC1_IN5) are slider inputs for RV6 and RV5.
- These are the same pins as the STM32F765 internal DAC outputs (DAC1_OUT/DAC2_OUT).
- The internal DAC must never be enabled. Accidental DAC peripheral clock enable
  or DAC pin mode configuration will silently corrupt slider ADC readings.
- `TIM6_DAC_IRQHandler` is the vector table name for IRQ54 because TIM6 and the
  internal DAC share an IRQ line by ST hardware design. This does NOT mean the
  DAC is in use. We use external CS4344 codecs via I2S2/I2S3.

### Boot / Init
- EXTI_IMR = 0 must remain as the very first operation in main(), before sysclk_init()
- Do NOT add pull-down to PD4 or PD5
- `lcd_init()` before `lcd_tim7_init()`
- `menu_init()` calls memset on parameter_values — do not also memset in main()

### Sample Flash
- `knowledge_files/LXR-master/` is read-only reference material. Do not modify it.
- Application flash must end at `0x0807FFFF`; sectors 6-11 (`0x08080000-0x081FFFFF`) belong to user samples.
- `Load:[Samples ]` erases/reinstalls from `/samples`, then appends looped samples from `/loops`. There is no separate visible `SampLoop` entry after Session 023.
- `SampleInfo.size` is 32-bit metadata now, but long playback is not solved until oscillator indexing is widened beyond the legacy `phase >> 17` path.

### Hardware
- TIM7_SR defined locally in timebase.c at 0x40001410 — do not remove
- 24-bit audio: `sample_mx_t` (int32_t, signed-24 value) mixer/output path; `pack_half()` emits true 24-bit `[MSW,LSW]` payload; I2SCFGR DATLEN=01/CHLEN=1. Do NOT re-add zeroed LSW. Do NOT add `>>8` shift in `sampleMix_toS24()` — the int16<<8 scale is already in the value.
- I2S3_SD = PB5 (PC12 is SD SCLK — was misdocumented in early sessions)
- SD card: bit-bang SPI on PC12/PD2/PC8/PD0 (NOT hardware SPI1). Hardware SPI
  remapping is impossible on this board — all viable SPI peripherals are taken
  or on unbonded TFBGA100 pins.
- SD bit-bang SPI is NOT re-entrant. Must only ever be called from one context
  at a time. Only sdcard_lxr02.c calls SPI_transmit,
  driven by afatfs_poll() from one context (main loop or TIM5 ISR, never both).
- ChaN FatFS (ff.c/diskio.c) and sdTest leftovers have been removed from the
  active tree. Do not re-add them or add code that depends on
  f_open/f_read/f_write/f_close.
- PE13/PE14: AF1 (TIM1_CH3/CH4), external 10kΩ pull-ups, no internal pull-up
- TIM1 IRQ27, TIM6 IRQ54, TIM7 IRQ55
- DMA1 Stream 4 (I2S2/DAC2) = IRQ15; DMA1 Stream 7 (I2S3/DAC1) = IRQ47. Both in HISR/HIFCR.
- TIM2: reserved for CLK IN BPM measurement and MIDI RX timestamping. Do NOT use for SD ISR.
- TIM5: free, confirmed available for SD ISR.
- OUT jack detect runtime split: PB4/PB6 are sampled by the TIM6-scheduled
  foreground service; PD6/PD7 are edge-driven in EXTI9_5. This avoids EXTI
  line-sharing conflicts and keeps the TIM6 ISR short.

### Encoder (SW42)
- Encoder algorithm: Dannegger difference (NOT LUT)
- Seed: `last = new & 3` in encode_init() (NOT `(new + 3) & 3`)
- encode_read4() uses round-toward-zero divide (NOT arithmetic shift `>>= 2`)
- encode_read1/read2 permanently removed — do not re-add
- `ts_dirs[]` direction buffer in encoder.c — rebound suppression. Do not remove.
- encode_read4() does cpsid/cpsie internally. That is acceptable from
  foreground. If this is ever moved into an ISR, replace it with a shadow-copy
  handoff because TIM1 (priority 1) can preempt lower-priority service code and
  corrupt `enc_delta` mid-read.

### Audio / AudioCodecManager
- audioCodec_init() is the single entry point
- SPSC ready queue (2 slots) replaces bCurrentSampleValid flag
- Main loop pattern: if an audio queue slot is free, fill one `AUDIO_DMA_FRAMES` hardware slot as three `OUTPUT_DMA_SIZE` mixer blocks, then `audioCodec_commitRenderBuffer()`.
- Stream 4 is the refill master; Stream 7 clears flags only
- `OUTPUT_DMA_SIZE = 32` is the effective LXR-master DSP/control block (corrected from 16 in Session 019).
- `AUDIO_DMA_FRAMES = 96` is the hardware DMA half; render budget per queued hardware slot is 2.18ms = 471,288 cycles at 216MHz.
- DSP render must stay in the main loop. Moving it to the DMA ISR creates a
  hard 2.18ms ceiling with no graceful degradation — when DSP expands beyond
  the budget the system locks up. Main loop allows unlimited DSP expansion with
  underruns as the graceful signal.
- `audioCodec_getQueueFreePercent()` is a DWT-based audio queue-free pressure
  meter used by the Global `cpu` widget; it is not generic MCU utilization.

### Menu / Display
- DO NOT TOUCH endlessPots (RV1-4) — `atan2f(b, a)`, do not change argument order
- RV1-RV4 endless pots use raw A/B snapshot baselines and deadzone false-start cancellation. `PAR_MORPH` is the only target with double angular speed.
- LCD ring is head/tail-only SPSC (NO `lcd_q_count`). Do not reintroduce a shared count variable.
- `sendDisplayBuffer` emits `lcd_setcursor` before every data byte. Do not add position-tracking optimization.
- `while → if` in buttonHandler_processEvents() — intentional, do not revert
- Saturation pattern in `menu_encoderChangeParameter` and `menu_handleLoadSaveMenu`: int16 sum + clamp, NOT uint8 wrap + boundary check.
- `menu_knobs_dirty` flag + `menu_serviceKnobRepaint()` is RV1-4-only. **Do not extend dirty-flag coalescing to other input paths.**

### Failed Approaches — Do Not Retry
- **DO NOT attempt TIM7 idle gating.** Confirmed broken Session 6.
- **DO NOT attempt broad repaint coalescing across all input paths.** Confirmed broken Session 6.
- **DO NOT use arithmetic right shift (`>>= 2`) on signed val in encode_read4().** Asymmetric — fixed Session 7.
- **DO NOT move DSP render into the DMA ISR.** Creates hard 2.18ms ceiling with no graceful degradation. Confirmed wrong architectural direction Session 10.
- **DO NOT implement SD operations as blocking calls in the main loop or in any ISR at priority ≤ 4.** f_open blocks 1–50ms. Confirmed root cause of audio corruption after kit load Session 10.
- **DO NOT "chunk" SD at FatFS-call granularity in the main loop.** f_open is one FatFS call and still blocks for its full duration. Confirmed insufficient Session 10.
- **DO NOT fork ChaN FatFS for async conversion.** 20+ functions need state machine conversion, 28 yield points, 6 levels of call nesting. Unacceptable risk. asyncfatfs adopted instead. Session 11.
- **DO NOT use NB_FatFS.** C++ with heap allocation, lambdas, no FAT16, no _FS_TINY. Session 11.
- **DO NOT remove the `i == 127` skip in `preset_morph()`.** Index 127 causes uint16 underflow in midiParser_ccHandler → wild write to midiParser_originalCcValues[65536]. Session 12.
- **DO NOT use VLAs in HiHat_calcSyncBlock() (or any DSP file).** `mod1[size]`/`mod2[size]` cause silent stack corruption with -O2. Use static arrays. Session 12.
- **DO NOT call SD_sendCommand() from sdcard_lxr02.c.** It deasserts CS after the R1 response — incompatible with data transfer. Use send_cmd_keep_cs() which leaves CS asserted. Session 12.
- **DO NOT post two filesystem requests back-to-back.** The storage FSM is single-operation. Wait for completion or ack first. Session 12/17.
- **DO NOT use frontPanel_sendData() for CC_VELO_TARGET/CC_LFO_TARGET during kit load.** The MIDI encode/decode roundabout doesn't properly establish modulation targets in the merged single-MCU context. Use `preset_sendModTarget()` instead. Session 13.
- **DO NOT add a morph skip cache.** Morph must send a complete final pass at the latest requested value or DSP/menu state can diverge. Session 16.
- **DO NOT apply endless-pot double speed to all `DTYPE_0B255` parameters.** Hardware testing showed BPM drift; only `PAR_MORPH` gets double angular speed. Session 16.
