# Session Handoff Template

## How to start a new session

Paste the following at the start of each conversation, filling in the bracketed fields:

---

**Project**: LXR-02 firmware port (STM32F765VIH6)  
**Session goal**: [e.g. "Port the sequencer engine from the original LXR AVR/STM32 source"]  
**Last session summary**: [paste the "End of session" block from the previous handoff, or "first session after hardware testing"]  
**Current tarball**: [confirm the tarball from the project files is the current source, or note if you have local changes]  
**Constraints today**: [e.g. "keep changes to sequencer files only", "don't touch USB", "15 minutes available"]

Key files to be aware of:
- Original LXR source is at `/tmp/LXR-master/` on the server (AVR: `front/LxrAvr/`, STM32F4: `mainboard/LxrStm32/src/`)
- Current port lives in the working tarball, extracted to `/home/claude/lxr02/`
- Knowledge files: HARDWARE_MAP.md, AVR_TO_F765_MIGRATION.md, FIRMWARE_STATE.md, ENHANCED_FEATURES.md

---

## End of session block

```
DATE: 2026-04-30
SESSION GOAL: Implement file save for kit (.SND) and globals (.GLO). Stub all other file types. Prepare repo so sequencer/DSP files can land verbatim.
COMPLETED: Kit save (.SND) and globals save (.GLO) implemented and verified on hardware. All other preset stubs added. Latent uint8_t loop counter bug fixed in preset_loadGlobals. DTCM linker section added with INDTCM/INDTCMZ/INCCM/INCCMZ macros. F765 hardware RNG ported. Compatibility shims for verbatim DSP/sequencer porting (stm32f4xx.h, full MidiMessages.h, SeqStep.h, frontPanelParser stubs, globals.h). uint32_t systick_ticks added alongside time_sysTick. Directory infrastructure confirmed. Build clean, image packages correctly.
Post-session addenda (same date): Global page sub-page reset bug fixed. Boot splash sequence added (cosmetic overlay only). SELECT_1 LED dark at boot documented as bug-compatible, not fixed. Encoder firing asymmetry root-caused and fixed (val>>2 arithmetic shift → round-toward-zero divide). Flash sector layout probed and memtest implemented. Parameter reconciliation, screensaver port, CGRAM custom character support added. Audio path reshaped to interrupt-driven double-buffer model (AudioCodecManager).
VERIFIED ON HARDWARE: GLO + SND save confirmed working. Boot splash sequence verified. Encoder feel symmetric after divide fix. Flash sector probe results confirmed — sectors 5-11 blank, app in sector 2, single-bank confirmed. Cymbal sounding clean; 78 startup underruns then stable.

CHANGES THIS SESSION:
- Core/Preset/presetManager.c: preset_saveDrumset, preset_saveGlobals implemented. preset_savePattern, preset_loadPattern, preset_saveAll, preset_loadAll, preset_morph, preset_getMorphValue stubbed. uint8_t loop counter in preset_loadGlobals widened to uint16_t (latent bug fix).
- Core/Preset/presetManager.h: full declarations for all save/load + morph functions.
- STM32F765VIHx_FLASH.ld: .dtcm and .dtcmz sections added. Unused 4KB ._stack reservation removed. ASSERT for DTCM overflow.
- Core/Src/startup_stm32f765xx.s: .dtcm copy loop and .dtcmz zero loop added. Vector entries for IRQ15 (DMA1_Stream4_IRQHandler) and IRQ47 (DMA1_Stream7_IRQHandler) added with weak aliases.
- config.h: INDTCM/INDTCMZ/INCCM/INCCMZ macros. NUM_VOICES=3, OUTPUT_DMA_SIZE=16, DMA_MODE_ACTIVE=1, USE_DAC2=1. MEMTEST_ENABLED flag.
- Core/DSPAudio/random.c + random.h: F765 hardware RNG port with RNG_SR_CECS|SECS recovery.
- Core/compat/stm32f4xx.h: vestigial-include shim via stdint.h.
- Core/MIDI/MidiMessages.h: replaced 41-line stripped version with full 510-line mainboard version.
- Core/MIDI/SeqStep.h: StepData struct ported from original AVR file.
- Core/MIDI/frontPanelParser.h + .c: full message-constant set, no-op stubs for all frontPanel_send*/frontParser_* functions and state vars.
- Core/MIDI/Uart.h: extended with frontpanel-UART stubs.
- Core/globals.h: port of original mainboard globals.h.
- Core/Hardware/timebase.c: volatile uint32_t systick_ticks added, incremented in TIM6_DAC_IRQHandler alongside time_sysTick.
- Makefile: Core/DSPAudio, Core/Sequencer, Core/SampleRom, Core/compat include paths added. Core/DSPAudio/random.c, Core/MIDI/frontPanelParser.c added to sources. Core/Menu/copyClearTools.c, Core/Menu/screensaver.c added.
- Core/Menu/menu.c: menu_switchPage(MENU_MIDI_PAGE) now resets menuIndex to 0 when entering from a different page.
- main.c: boot splash block added (between init completion and main loop — original init order unchanged). memtest_run() call added. ParameterArray.h included instead of Parameters.h. screensaver.h included.
- Core/Hardware/frontPanel/IO/encoder.c: encode_read4() val>>2 arithmetic shift replaced with round-toward-zero divide using int16_t intermediates. Round-toward-zero is symmetric — both directions require exactly 4 transitions to fire one count.
- Core/Hardware/memtest.h + memtest.c: flash sector probe implementation. Read-only and destructive (SHIFT-held) modes. Hard guard rejects any sector < 6 before touching FLASH_CR.
- Core/Menu/copyClearTools.c + .h: frontPanel copy/clear functions ported; frontPanel calls commented out pending replacement with direct calls.
- Core/Menu/screensaver.c + .h: screensaver timer, enable/disable, animations ported from original LXR AVR.
- Core/Preset/ParameterArray.c + .h: DSP-necessary parameter load functions. Supersedes Parameters.h.
- Core/Preset/Parameters_reference.h.bak: backup of Parameters.h for reference only.
- Core/Hardware/AudioCodecManager.h + .c: consolidated audio — DMA ISRs, I2S/GPIO/DMA init, SPSC ready queue (2 slots) replacing bCurrentSampleValid flag. Both DMA1 Stream4 and Stream7 ISRs. audioCodec_init() is single entry point.
- lcd.c: static definitions for custom characters, lcd_define_char() for loading bitmaps to CGRAM slots.
- lcd.h: lcd_define_char() declaration.
- timebase.h: includes screensaver.h. screensaver_timer incremented alongside systick.
- buttonHandler.c: includes screensaver.h, touch to disable screensaver on button press.
- adcPots.c: includes ParameterArray.h instead of Parameters.h.
- menu.h: includes ParameterArray.h instead of Parameters.h.
- Core/Preset/presetManager.c: includes ParameterArray.h instead of Parameters.h.
- Parameters.h: removed — superseded by ParameterArray.h. Parameters_reference.h.bak retained for reference.

KNOWN ISSUES INTRODUCED: Screensaver timing visibly off — clock recalculation needed. Screensaver custom characters not yet defined (CGRAM slot loading works, bitmaps TBD). GetRngValue() calls must mask result: uint16_t rnd = (int16_t)(GetRngValue() & 0x7FFF) — root cause not fully investigated, masking is confirmed working fix.
KNOWN ISSUES RESOLVED: Kit save and globals save (now working on hardware). Global page sub-page wrong on entry from voice mode (menuIndex reset fix). Encoder firing asymmetry — CCW fired on 1 transition vs 4 for CW (val>>2 arithmetic shift → round-toward-zero divide). SELECT_1 LED dark at boot documented as intentional bug-compatible behaviour (do not fix in clean port).

NEXT SESSION RECOMMENDED GOAL: Audio integration — audioCodec_init() is now the single entry point. Main loop pattern: if (audioCodec_queueFreeSlots() > 0) { mixer_calcNextSampleBlock(...); audioCodec_commitRenderBuffer(); }. Port pure DSP files next (BufferTools, 1PoleLp, dither, Decay, SlopeEg2, snapEg, squareRootLut, transientGenerator, transientTables).
BLOCKERS: buttonHandler/ledHandler audit still not done. Screensaver timing recalculation needed before screensaver is usable.

CRITICAL REMINDERS FOR NEXT SESSION:
- ALWAYS extract a fresh tarball and verify the working directory before writing any code
- The current known-good base is lxr02_new_refactored.zip (refactor session, 2026-05-04)
- EXTI_IMR = 0 must remain as the very first operation in main(), before sysclk_init()
- Do NOT add pull-down to PD4 or PD5
- lcd_init() before lcd_tim7_init()
- menu_init() calls memset on parameter_values — do not also memset in main()
- TIM7_SR defined locally in timebase.c at 0x40001410 — do not remove
- 24-bit audio: int16_t MSW+LSW packed, I2SCFGR DATLEN=01/CHLEN=1
- I2S3_SD = PB5 (PC12 is SD SCLK — was misdocumented in early sessions)
- SD card: bit-bang SPI on PC12/PD2/PC8/PD0 (NOT hardware SPI1). PA8 was a false positive. No SPI1 contention exists.
- PE13/PE14: AF1 (TIM1_CH3/CH4), external 10kΩ pull-ups, no internal pull-up
- TIM1 IRQ27, TIM6 IRQ54, TIM7 IRQ55
- DMA1 Stream 4 (I2S2/DAC2) = IRQ15; DMA1 Stream 7 (I2S3/DAC1) = IRQ47. Both in HISR/HIFCR.
- Encoder algorithm: Dannegger difference (NOT LUT)
- Seed: last = new & 3 in encode_init() (NOT (new + 3) & 3)
- encode_read4() uses round-toward-zero divide (NOT arithmetic shift >>= 2 — that was the asymmetry bug)
- encode_read1/read2 permanently removed — do not re-add
- ts_dirs[] direction buffer in encoder.c — rebound suppression. Do not remove.
- while → if in buttonHandler_processEvents() — intentional, do not revert
- LCD ring is head/tail-only SPSC (NO lcd_q_count). Do not reintroduce a shared count variable.
- sendDisplayBuffer emits lcd_setcursor before every data byte. Do not add position-tracking optimization.
- Saturation pattern in menu_encoderChangeParameter and menu_handleLoadSaveMenu: int16 sum + clamp, NOT uint8 wrap + boundary check.
- menu_knobs_dirty flag + menu_serviceKnobRepaint() is RV1-4-only. Do not extend dirty-flag coalescing to other input paths.
- DO NOT TOUCH endlessPots (RV1-4) — atan2f(b, a), do not change argument order
- DO NOT ATTEMPT TIM7 idle gating — wakeup race causes intermittent freezes. Confirmed broken.
- DO NOT ATTEMPT broad repaint coalescing across all input paths — caused intermittent freezes and lag.
- DO NOT use arithmetic right shift (>>= 2) on signed val in encode_read4(). Fixed with round-toward-zero divide.
- DTCM is NOT DMA-accessible — never tag a DMA buffer with INDTCM/INDTCMZ
- Two systick counters: time_sysTick (uint16_t, encoder/ledHandler/diskio), systick_ticks (uint32_t, DSP/Sequencer). Both 1kHz, same ISR. Don't merge them.
- GetRngValue() calls must mask the result: & 0x7FFF — do not remove this masking
- audioCodec_init() is the single entry point — replaces CodecInit() + audioTest_init() chain
- SPSC ready queue (2 slots) replaces bCurrentSampleValid flag
- Main loop pattern: if (audioCodec_queueFreeSlots() > 0) { mixer_calcNextSampleBlock(...); audioCodec_commitRenderBuffer(); }
- Stream 4 is the refill master; Stream 7 clears flags only
- Boot splash uses busy-wait on systick_ticks between init completion and main loop. TIM6 is running by that point — the wait has a live tick source. Init order is unchanged — splash is purely an overlay.
- MENU_MIDI_PAGE entry now resets menuIndex to 0 from a different page. Toggle (re-press SHIFT+MODE4 while in globals) is preserved.
- SELECT_1 LED dark at boot — faithful reproduction of original LXR bug. Do NOT fix in clean port repo. Fix in repo 2 only: add led_setActiveSelectButton(0) after menu_switchPage(VOICE1_PAGE) in menu_init().
- Erase floor for flash is sector 6. memtest_erase_sector rejects any sector < 6. Do not remove this guard.
- .SND and .GLO files saved by LXR-02 are byte-compatible with the original LXR.
- Core/compat/stm32f4xx.h resolves vestigial includes via stdint.h ONLY — do not use for files needing actual F4 register access.
- Stub frontPanel_send* functions in frontPanelParser.c are dead-stripped until called. Replace each call site with appropriate direct call into ledHandler/menu/dout — don't try to make the stubs do something generally.
- Parameters_reference.h.bak in Core/Preset/ — left for reference during reconciliation. Delete in a cleanup pass once full port is verified.
- Screensaver timing is visibly off — clock recalculation needed before it is usable.
```

---

## Session 7 — 2026-04-30 (Save logic + Sequencer/DSP prep)

**Goal**: Implement file save for kit (.SND) and globals (.GLO). Stub all other file types. Prepare repo so sequencer/DSP files can land verbatim.

**Working tarball at start**: `lxr02_knob_collapse.tar.gz`  
**Working tarball at end**: `lxr02_seq_dsp_prep.tar.gz`

---

### Part 1 — Save logic for .SND and .GLO

`Core/Preset/presetManager.c`:
- `preset_saveDrumset` writes `[8B name][END_OF_SOUND_PARAMETERS bytes params]` — 236 bytes total per kit
- `preset_saveGlobals` writes `glo.cfg` with `parameter_values[PAR_BEGINNING_OF_GLOBALS..NUM_PARAMS]`
- `preset_writeDrumsetData` / `preset_writeGlobalData` static helpers
- File format identical to original LXR — files are cross-compatible
- `isMorph=1` writes live `parameter_values[]` same as `isMorph=0`. Real morph save needs `parameters2[]` + DSP. File format identical either way; saved morph kits today will load correctly when morph wires up.

Stubs added: `preset_savePattern`, `preset_loadPattern`, `preset_saveAll`, `preset_loadAll`, `preset_morph`, `preset_getMorphValue`. File-format reference preserved as comments. Menu save dispatch in `menu.c` already routes correctly through these.

**Pre-existing latent bug fixed**: `preset_loadGlobals` had `uint8_t i` loop counter bound by `i < NUM_PARAMS` (=273). Always-true comparison on uint8_t; loop only terminated by `bytesRead==0` at file EOF. Widened to `uint16_t`. Original AVR used `int`.

`Core/Preset/presetManager.h` — full declarations for all save/load + morph functions.

---

### Part 2 — DTCM linker section + INDTCM/INDTCMZ macros

`STM32F765VIHx_FLASH.ld`:
- New `.dtcm` (initialised data, loaded from FLASH via `LOADADDR`)
- New `.dtcmz` (zero-init, NOLOAD)
- Removed unused 4KB `._stack` reservation in DTCM (real stack is at top of SRAM1 via `_estack=0x20080000`)
- `ASSERT` for DTCM overflow if `.dtcm + .dtcmz` exceed 128KB
- DTCM is 128KB on F765 vs 64KB on the original F4 CCM — 2× more headroom for DSP placement

`Core/Src/startup_stm32f765xx.s`:
- Added `.dtcm` copy loop (mirrors `.data` copy pattern)
- Added `.dtcmz` zero loop (mirrors `.bss` zero pattern)

`config.h`:
- `INDTCM` / `INDTCMZ` macros (`__attribute__((section(".dtcm")))` / `".dtcmz"`)
- `INCCM` / `INCCMZ` aliases — DSP source files using original LXR macros port verbatim
- DSP-related defines added: `NUM_VOICES=3`, `OUTPUT_DMA_SIZE=16`, `DMA_MODE_ACTIVE=1`, `USE_DAC2=1`

⚠ DTCM is NOT DMA-accessible (same constraint as F4 CCM). DMA buffers must stay in SRAM1.

---

### Part 3 — Hardware support files for DSP

`Core/DSPAudio/random.c` + `random.h` — F765 hardware RNG port. Bare register access (no StdPeriph). Clock source: PLL48CLK (already configured for USB; `RCC_DCKCFGR2.CK48MSEL=00` set in `clocks.c`). Adds `RNG_SR_CECS|SECS` recovery (per RM0410 §27.3.6) which the original AVR-only port lacked. API matches original (`initRng`, `GetRngValue`).

---

### Part 4 — Compatibility shims for verbatim file porting

`Core/compat/stm32f4xx.h` — resolves vestigial `#include "stm32f4xx.h"` in DSP/Sequencer files via `<stdint.h>`. Does NOT provide F4 register tokens — files needing register access fail to compile against the shim, surfacing them for review (intentional).

`Core/MIDI/MidiMessages.h` — replaced our 41-line stripped version with the full mainboard 510-line version (only change: `stm32f4xx.h` → `stdint.h`). Strict superset; no existing code affected. Adds the full CC enum (~200 entries) needed by mixer/automation, plus `NO_AUTOMATION=0xff`, `MIDI_AT`, `CHANNEL_PRESSURE`, `MIDI_CC2=0xF4`, `CC_BANK_CHANGE`, `CC_MOD_WHEEL`, etc.

`Core/MIDI/SeqStep.h` — `StepData` struct, port of original AVR file. Used by file-format buffer code in presetManager.

`Core/MIDI/frontPanelParser.h` + `.c` — full message-constant set (LED_CC, SEQ_CC, CODEC_CC, VOICE_CC, SET_BPM, CC_2, CC_LFO_TARGET, CC_VELO_TARGET, STEP_CC, SET_P*_DEST/VAL, MAIN_STEP_CC, ARM_AUTOMATION_STEP, SAMPLE_CC, all LED_*/VOICE_*/SEQ_* sub-codes, all SYSEX_* sub-codes). Stubs for `frontPanel_sendData`, `frontPanel_sendByte`, `frontPanel_sendMidiMsg`, `frontPanel_parseData`, `frontParser_parseUartData`, `frontParser_updateTrackLeds`. Shim state vars: `frontParser_activeTrack`, `frontParser_shownPattern`, `frontParser_activeFrontTrack`, `frontParser_sysexActive`, `frontParser_newSeqDataAvailable`, `frontParser_stepData`, `frontParser_midiMsg`, `frontPanel_sysexMode`. All no-op stubs are dead-stripped by `--gc-sections` until something calls them; replace each call site with the appropriate direct call (ledHandler, menu, mixer) at integration time.

`Core/MIDI/Uart.h` — extended with frontpanel-UART stubs (`initFrontpanelUart`, `uart_processFront`, `uart_sendFrontpanelByte`, `uart_sendFrontpanelSysExByte`, `uart_clearFrontFifo`, `uart_checkAndParse`). All no-ops, defined in `frontPanelParser.c`. Sequencer's pattern-save sysex code lands verbatim.

`Core/globals.h` — port of original mainboard `globals.h`. Declares `extern volatile uint32_t systick_ticks`, `extern int16_t audioOutBuffer[2]`, `extern uint8_t bCurrentSampleValid`. `SAMPLE_VALID 0xff`, `FILTER_SHAPER -0.9f`. Last two extern symbols left undefined — they'll be defined when `AudioCodecManager.c` lands.

---

### Part 5 — uint32_t systick_ticks alongside time_sysTick

`Core/Hardware/timebase.c`:
- Added `volatile uint32_t systick_ticks = 0;`
- TIM6_DAC_IRQHandler now ticks both counters in step
- 4-byte BSS cost
- 16-bit `time_sysTick` retained — encoder/ledHandler/diskio depend on its `uint16_t` semantics. Two parallel counters is safer than widening one and risking a subtle wrap-handling regression.

Original LXR ticked at 4kHz (`SysTick_Config(HCLK/4000)`) — 0.25ms per tick. Ours ticks at 1kHz — 1ms per tick. `seq_calcDeltaT` in original `sequencer.c` computes `(1000*60)/bpm` then divides by 96 and multiplies by 4, producing milliseconds. Comparing against a 1kHz `systick_ticks` matches what the math actually computes.

---

### Part 6 — Directory infrastructure

Empty placeholders confirmed at `Core/Sequencer/`, `Core/SampleRom/`, `Core/DSPAudio/` (now contains `random.c`/`.h`).

`Makefile`:
- Added include paths: `Core/DSPAudio`, `Core/Sequencer`, `Core/SampleRom`, `Core/compat`
- Added sources: `Core/DSPAudio/random.c`, `Core/MIDI/frontPanelParser.c`

---

### Verification

Build clean. Same warning set as baseline (vendor code only — FatFS, USB driver). Final size:
- baseline (start of session): 37,312 bytes text / 12,280 bytes BSS
- end of session: 37,840 bytes text / 8,188 bytes BSS

Text +528: save logic (~120 bytes), random.c (linked but stripped — calls dead, will return when DSP uses it), `systick_ticks++`, frontPanelParser stubs.  
BSS −4,092: removed 4KB `._stack` reservation in DTCM (was wrongly counted as BSS by gnu size; recovered for INDTCM/INDTCMZ).

Image packages cleanly: `make img` → `build/LXRV2_lxr02.img` 38,224 bytes.

Smoke test (`/tmp/smoke_test.c`): compiled a fragment that includes `stm32f4xx.h`, `globals.h`, `MidiMessages.h`, `frontPanelParser.h`, `Uart.h`, `random.h` and calls `frontPanel_sendData(LED_CC, ...)`, `uart_sendFrontpanelByte()`, etc. against the cross-compiler. Clean compile — sequencer/DSP files using these will land verbatim.

Host-side file-format test (`/tmp/save_format_test.c`): produced a 236-byte `.SND` file matching original LXR byte layout. `END_OF_SOUND_PARAMETERS=228` matches original.

### Hardware mismatch flagged for future session

`mixer.c` (original DSPAudio/mixer.c) reads `GPIOC->IDR` and `GPIOA->IDR` for jack-detect on PA0/PA5/PC4/PC5. On LXR-02 those pins are slider/encoder ADC inputs. **Cannot land verbatim — jack-detect logic must be stubbed (e.g. always assume all jacks present) when `mixer.c` is added.** Documented in the compat shim header.

---

## Session 7 addendum — 2026-04-30 (post-flash bugfixes)

GLO + SND save confirmed working on hardware. Three small bugs reported by user; this session applied two fixes and documented one as bug-compatible behaviour.

**Note on history**: An earlier version of this addendum applied the boot splash by interleaving the splash phases with init steps (running USB/SD init under cover of phase 2, deferring menu_init/preset_load to end of phase 2). That introduced unnecessary risks: busy-wait loops in init that depend on TIM6 ISR liveness, reordering of menu_init relative to USB/SD init, timing coupling between SD enumeration duration and splash visibility. That version was reverted before flashing. The current implementation applies the splash purely as a cosmetic overlay AFTER all init completes — original init order preserved, splash block can be commented out wholesale to disable.

### Fix 1: Global page lands on wrong sub-page when entered from voice mode

`menu_switchPage(MENU_MIDI_PAGE)` left `menuIndex`'s sub-page bits at whatever the user was on in the previous mode. From OSC (sub-page 0) → global sub-page 0 (which exists, masked the bug). From AEG (sub-page 2) → global sub-page 2 (also exists). From MOD or beyond → non-existent global sub-page; screen showed "                <" only.

Root cause: `menuIndex = (sub_page << 3) | activeParameter` is shared state across modes. The original AVR firmware has the same bug — not visible there because users rarely jump to globals while on a voice sub-page > 1.

Fix (`Core/Menu/menu.c`, `menu_switchPage`): when entering MENU_MIDI_PAGE from a different page, set `menuIndex = 0`. The toggle path (already-on-global → press SHIFT+MODE4 again to cycle sub-pages) is unchanged.

### Fix 2: Boot splash sequence — applied as cosmetic overlay only

Old behaviour: brief "LXR-02 / Booting..." flash before menu appeared.

New sequence (purely cosmetic — applied AFTER all init completes):
- Phase 1 (0..1000ms): blank LCD, all LEDs on (incl. SW43 SHIFT/BAR1)
- Phase 2 (1000..3000ms): "Sonic Potions" / "LXR Drums V0.37", LEDs on
- Phase 3 (3000..3200ms): menu_repaintAll, LEDs stay all on
- t=3200ms: real LED state restored (MODE1 + voice 1 only, SW43 off)

Implementation (in `main.c`):
- Boot string ("LXR-02 / Booting...") deleted entirely. LCD stays blank during init.
- Splash block placed BETWEEN init completion and main loop. Original init order is unchanged from the v3 baseline.
- LED override goes only into `dout_outputData[]`, never into `led_originalLedState[]`. Post-splash, `led_clearAll` zeros both arrays then the same setters menu_init uses (`led_setMode2`, `led_setActiveVoice`) rebuild a clean baseline that keeps led_reset/blink/pulse semantics correct.
- Includes `globals.h` for `systick_ticks` (1kHz counter from TIM6, advances during the busy-wait loops).
- Block delimited by `===== BOOT SPLASH START =====` / `===== BOOT SPLASH END =====` markers. To disable, comment out everything between these markers — no other code needs to change.

Brief (~10ms) menu flicker may be visible at t=0 before phase 1's `lcd_clear` enqueue takes effect. menu_init + preset_load enqueue ~96 character writes that drain at 10kHz before lcd_clear executes. Imperceptible in practice.

### Fix 3: SELECT_1 LED dark at boot — DOCUMENTED, NOT FIXED

The SELECT_1 LED (OSC sub-page indicator) does not light at power-on. It lights correctly after navigating away to another sub-page and back, and updates correctly throughout normal operation.

This is faithful reproduction of original LXR firmware behaviour — original AVR `menu_init` calls `menu_switchPage(VOICE1_PAGE)` which paints the LCD and sets MODE/voice LEDs but does not call any "highlight current SELECT button" function for sub-page 0. Subsequent SELECT-button presses trigger `handleSelectButton` which lights the active SELECT LED via `led_setActiveSelectButton`.

Per user direction: **do NOT fix this in the clean 0.37 port repo** — bug-compatible behaviour. Address in repo 2 (enhanced firmware) only. Suggested fix at that time: in `menu_init` after `menu_switchPage(VOICE1_PAGE)`, call `led_setActiveSelectButton(0)`. Verify this doesn't conflict with the existing `led_clearSequencerLeds` in `menu_switchPage` — sequencer-LED clear comes first, the SELECT LED set should win.

---

## Session 7 follow-up — encoder firing asymmetry

User reported (after flashing v4):
- CCW: tiny jiggle without crossing detent bump fires a count, then bounces back
- CW: must fully cross detent bump for a count to fire
- At param=0, the CCW jiggle lands param at 1
- "Always returns to correct value" elsewhere — symptoms always paired

### Root cause

NOT the seed. The `last = new & 3` seed is correct.

Real bug is in `encode_read4` divide step: `val >>= 2` on int8_t is arithmetic right shift = FLOOR division for negative values. `-1 >> 2 = -1`, not 0. So `val=-1` (one CCW transition) fires `-1` immediately, while `val=+1` (one CW transition) correctly returns 0. Same threshold should be 4 transitions in either direction.

Floor-divide also leaves wrong-sign residue: val=-1 → residue=+3 (because -1 = 4·(-1) + 3 under floor decomposition). When the user's jiggle then returns +1 to the encoder, residue becomes +4 → fires +1. Net behaviour: down-then-up jiggle.

At param=0 with clamping: the spurious -1 fire is lost (param can't go below 0), but the +4-fires-+1 path completes normally → param ends at 1. Verified by host-side simulation reproducing the exact symptoms.

Sustained CCW also produced ~25% more counts than equivalent CW — consistent feel difference, not just an edge case.

### Fix

`Core/Hardware/frontPanel/IO/encoder.c`, `encode_read4`: replaced floor-divide with round-toward-zero divide and signed modulo. New table:

| val   | return | residue |
|-------|--------|---------|
| -8    |   -2   |    0    |
| -5    |   -1   |   -1    |
| -4    |   -1   |    0    |
| -3..-1|    0   |  -3..-1 |
|  0    |    0   |    0    |
|  1..3 |    0   |   1..3  |
|  4    |    1   |    0    |
|  5    |    1   |    1    |
|  8    |    2   |    0    |

Symmetric in sign. Both directions need exactly 4 transitions to fire one count.

Implementation: int16_t intermediates to dodge int8_t -val overflow risk on val=-128 (theoretical only; never reached in practice). All inside the cpsid/cpsie critical section so atomic with ISR.

No interaction with acceleration: divide runs first, acceleration multiplier applies to the now-symmetric `val`. Rebound suppression compares against `val_sign` which now correctly reflects net direction.

### Bug-history reminder

Three encoder fixes have stacked over time. Do NOT regress these:
1. **Seed**: `last = new & 3` (Session 5). Clean boot, no spurious first-click.
2. **TIM1 IC hardware filter**: `ICxF=0xF` (Session 4). 32-sample silicon debounce.
3. **Symmetric divide**: round-toward-zero in `encode_read4` (this session). Symmetric CW/CCW threshold.

Specifically forbidden:
- `last = (new + 3) & 3` — tried in Session 4b, discarded. Loses the first edge and produces a wrong-direction click on motion onset.
- LUT-based state machine (CHECKPOINT in Session 4 history). Direction-asymmetric due to different bug.
- `val >>= 2` directly on signed val — the bug fixed this session.

### What this fix does NOT address

Only the firing-threshold asymmetry (which exactly matches user's described symptoms). If the user reports residual "feel" asymmetry after flashing — i.e. one direction still requires more mechanical travel than the other to fire — that would be a *physical phase alignment* issue (firing-point relative to detent bump), separate from the divide. Different fix entirely; would need to investigate at that point.

---

## Session 7 follow-up — flash sector layout & sample-region probe

User flagged the SampleMemory.c / flash_if.c risk and asked to design memory tests now so we know the safe layout when we wire sample storage later.

### Authoritative facts established this session

- **F765VI sector layout (single-bank, 2MB) per AN4826 §2.2:**
  - Sectors 0-3: 32KB each (0x08000000-0x0801FFFF)
  - Sector 4: 128KB (0x08020000-0x0803FFFF)
  - Sectors 5-11: 256KB each (0x08040000-0x081FFFFF)
- F765 supports both single-bank and dual-bank via FLASH_OPTCR.nDBANK (bit 29). Default factory is single-bank.
- F765 sectors are LARGER than F407 (128KB became 256KB for sectors 5+). Original LXR sector address constants would target wrong sectors on F765.
- F765 has D-cache (F407 doesn't). Reads after flash writes need explicit invalidation via SCB_DCIMVAC.
- During flash erase/program, reads from any sector in the same bank stall the CPU until the operation completes. In single-bank mode all sectors are in one bank, so all code fetches stall during erase. The polling loop "works" because it stalls on its own next-instruction fetch and only iterates once after BSY clears.

### Proposed sample-storage layout (validated by memtest)

```
Sector 0:    bootloader              32KB   0x08000000-0x08007FFF
Sector 1:    app start               32KB   0x08008000-0x0800FFFF
Sector 2:    app                     32KB   0x08010000-0x08017FFF
Sector 3:    app                     32KB   0x08018000-0x0801FFFF
Sector 4:    app                     128KB  0x08020000-0x0803FFFF
Sector 5:    app reserve             256KB  0x08040000-0x0807FFFF
─────────────────── erase floor ────────────────────────────────
Sector 6:    sample storage          256KB  0x08080000-0x080BFFFF
Sector 7:    sample storage          256KB  0x080C0000-0x080FFFFF
Sector 8:    sample storage          256KB  0x08100000-0x0813FFFF
Sector 9:    sample storage          256KB  0x08140000-0x0817FFFF
Sector 10:   sample storage          256KB  0x08180000-0x081BFFFF
Sector 11:   sample storage          256KB  0x081C0000-0x081FFFFF
```

Application currently reaches `0x081221B0` (`_etext + .data init` size = `0x08012030 + 0x180`) — well within sector 2. Up to ~480KB of app growth available before any change to sample-region floor.

### Files added

- `Core/Hardware/memtest.h` — public memtest_run() entry, gated on MEMTEST_ENABLED in config.h
- `Core/Hardware/memtest.c` — full implementation:
  - F765 single-bank sector table (verified against AN4826)
  - Read-only probe: flash size register, dual-bank status, app boundary (etext+sector), sectors 5-11 first/middle/last word + blank check
  - Destructive probe (SHIFT held at boot only, single-bank only): erase sector 11, verify blank, verify sector 5 unchanged (proves erase scope), program 3 test words, verify, re-erase to clean up
  - Hard guard: `memtest_erase_sector` rejects any sector < 6 before touching FLASH_CR
  - All erase/program with IRQs disabled (TIM6 stalls ~2s during sector erase — acceptable for one-shot boot test)
  - D-cache invalidate after writes (no-op when D-cache disabled, ready for when we enable it)
  - Re-erase cleanup so sector 11 is left blank at end
- `config.h`: `MEMTEST_ENABLED` flag (default 1; set 0 for production)

### Files modified

- `main.c`: includes `memtest.h`, calls `memtest_run()` between hardware init and SD card init. memtest_run is empty stub when MEMTEST_ENABLED=0.
- `Makefile`: added `Core/Hardware/memtest.c` to source list.

### Safety analysis

- **Bootloader safety**: erase floor is sector 6. Bootloader in sector 0 cannot be touched.
- **App safety**: erase floor is sector 6. App in sectors 1-5 cannot be touched.
- **Worst-case bug**: a hypothetical caller bug passing sector_nr=0 to memtest_erase_sector returns -1 without touching FLASH_CR. The guard is the load-bearing safety check.
- **Worst-case scenario the test is designed to detect**: user holds SHIFT, dual-bank mode is active (nDBANK=0), our SNB encoding is wrong → would erase wrong sector → recoverable via SD-card-bootloader reflash but loses installed samples. **Mitigation**: destructive test refuses to run if dual-bank detected, displays "DUAL BANK abort" and aborts.
- **Test leaves no flash state**: re-erases sector 11 after verify so flash is unmodified at end.

### What this test does NOT establish

- The LXRV2 bootloader's erase boundary during firmware install. The memtest probes sectors 6-11 read-only — if they all show BLANK on first boot, that's strong (but not definitive) evidence the bootloader doesn't write past sector 5. **The bootloader source still needs to be inspected before sample-install code goes into production.**
- Whether erase/program work reliably across multiple cycles (this is a one-shot test). Production sample-install will need wear-aware logic (tracking erase-cycle counts per sector, handling errors gracefully).
- Whether D-cache invalidation is correct (D-cache is currently disabled — code is correct but not exercised). When we enable D-cache later, run the destructive probe again to verify.

### How to use the test

1. Power off, hold SHIFT button (PB7), power on. → Destructive probe runs on sector 11 only.
2. Power off, do NOT hold SHIFT, power on. → Read-only probe runs.
3. Set `MEMTEST_ENABLED 0` in config.h before production builds.

---

## Session 7 follow-up — Parameter reconciliation, screensaver and CGRAM addition

Reconciliation of parameter enum mismatch as legacy of AVR-STM32 split. All parameters are listed in ParameterArray.h, which should correspond with Parameters.h for all sound parameters in the enum. Tested as working in menu. Parameters.h was renamed to "Parameters_reference.h.bak" as a reference in case any further reconciliation is needed.

Screen saver implementation was added, ported from original LXR AVR code, which required some clock recalculations that should be checked, the screen saver functions all work but the timing is visibly off.

The characters used by the original LXR implementation for the screensaver don't exist in the character table for this display, so an implementation for adding custom characters in the CGRAM was implemented. The specific characters will have to be implemented at some point, but loading the bitmaps into CGRAM slots works as expected.

### Files added

- `Core/Menu/copyClearTools.c` — functions that governed how the AVR front panel transmitted commands to copy and clear sequencer data. TODO: the frontPanel calls are commented out. These need to be replaced by looking at the original LXR files `/front/LxrAvr/frontPanelParser.c` and the data transmitted to `/mainboard/LxrStm32/src/MIDI/frontPanelParser.c` and this communication structure simplified. Future session.
- `Core/Menu/copyClearTools.h` — declarations for copyClearTools.c
- `Core/Menu/screensaver.c` — functions governing timer, enable/disable, and animations of the screensaver.
- `Core/Menu/screensaver.h` — declarations for screensaver.c
- `Core/Preset/ParameterArray.c` — DSP-necessary functions to load parameters.
- `Core/Preset/ParameterArray.h` — supersedes `Parameters.h`.
- `Core/Preset/Parameters_reference.h.bak` — backup of `Parameters.h` for reference only.

### Files modified

- `main.c`: includes `ParameterArray.h` instead of `Parameters.h`, includes `screensaver.h`. Function calls for screensaver touch and check.
- `Makefile`: added `Core/Menu/copyClearTools.c`, `Core/Menu/screensaver.c`
- `buttonHandler.c`: includes `screensaver.h`, touch to disable screensaver when pressing button.
- `adcPots.c`: includes `ParameterArray.h` instead of `Parameters.h`.
- `lcd.c`: static definitions for some custom characters, loading of the characters in `init`. adds function `lcd_define_char` to add one of the static const bitmaps to the CGRAM.
- `lcd.h`: definition for `lcd_define_char`. may be unnecessary, never called outside lcd.c
- `timebase.c`: increments `screensaver_timer` alongside systick. may be unnecessary, better to re-architect using only systick.
- `timebase.h`: includes `screensaver.h`
- `menu.c`: includes `screensaver.h`, `copyClearTools.h`, `ParameterArray.h` instead of `Parameters.h`, stub functions for screensaver and copyClear commented out, more timely `lcd_clear()` for menu init.
- `menu.h`: includes `ParameterArray.h` instead of `Parameters.h`.
- `Core/Preset/presetManager.c` — includes `ParameterArray.h` instead of `Parameters.h`.

### Files removed
- `Parameters.h`: superseded by ParameterArray.h. `Parameters_reference.h.bak` is included as a temporary reference only until all functions of the full port are done and tested.

---

## Session 7 follow-up — audio path reshape (option a)

User decision: adopt original LXR mainboard's interrupt-driven double-buffer model now (clean port). Future enhancement post-port may switch to model (b) where mixer fills DMA buffer halves directly via HT/TC interrupts, saving the pack step but requiring mixer modification.

### What changed

**New files:**
- `Core/Hardware/AudioCodecManager.h` — declares `dma_buffer*` extern; references `audioOutBuffer*` and `bCurrentSampleValid` in globals.h. `audioCodec_init()` entry point. SPSC ready queue (2 slots) API: `audioCodec_queueFreeSlots()`, `audioCodec_commitRenderBuffer()`.
- `Core/Hardware/AudioCodecManager.c` — defines `dma_buffer[OUTPUT_DMA_SIZE*8]`, `dma_buffer2[OUTPUT_DMA_SIZE*8]`, `audioOutBuffer[OUTPUT_DMA_SIZE*2]`, `audioOutBuffer2[OUTPUT_DMA_SIZE*2]`. DMA1 Stream4 and Stream7 ISRs that pack mixer 16-bit output to 24-bit MSW+LSW pairs. SPSC ready queue replaces `bCurrentSampleValid` flag.

**Modified:**
- `Core/globals.h`: `audioOutBuffer[2]` → `audioOutBuffer[OUTPUT_DMA_SIZE*2]`; added `audioOutBuffer2[OUTPUT_DMA_SIZE*2]`.
- `Core/Hardware/audioTest.c`: removed test-tone buffers and sin() generation; DMA now points at `dma_buffer*` from AudioCodecManager. Circular mode + HT+TC interrupts. NDTR = `OUTPUT_DMA_SIZE * 8` (full ping-pong). NVIC priority 4 for IRQ15 (DAC2 stream 4) and IRQ47 (DAC1 stream 7).
- `Core/Src/startup_stm32f765xx.s`: vector entries for IRQ15 (DMA1_Stream4_IRQHandler) and IRQ47 (DMA1_Stream7_IRQHandler) added; weak aliases added so binary links cleanly if AudioCodecManager.c absent.
- `main.c`: `audioCodec_init()` called as single entry point (replaces CodecInit() + audioTest_init() chain). Main loop polls SPSC queue: `if (audioCodec_queueFreeSlots() > 0) { mixer_calcNextSampleBlock(audioOutBuffer, audioOutBuffer2); audioCodec_commitRenderBuffer(); }`
- `Makefile`: added AudioCodecManager.c to sources.

### Buffer model

Mixer-side (16-bit, what mixer writes):
```
audioOutBuffer  [OUTPUT_DMA_SIZE * 2]  =  [L0, R0, L1, R1, L2, R2, ...]   (32 halfwords)
audioOutBuffer2 [OUTPUT_DMA_SIZE * 2]  =  same shape
```

DMA-side (24-bit packed, ping-pong):
```
dma_buffer  [OUTPUT_DMA_SIZE * 8]  =  [half 0 | half 1]
  half 0:  [MSW0_L, LSW0_L, MSW0_R, LSW0_R, ...]   (32 halfwords = 16 frames × 4 hw)
  half 1:  same shape, 32 hw
                                                      Total: 64 hw = 128 bytes
dma_buffer2  [OUTPUT_DMA_SIZE * 8]  = same shape
```

### Pack step

Original LXR: mixer wrote 16-bit samples directly into 16-bit DMA buffer. No pack.

Ours: mixer writes 16-bit into `audioOutBuffer*`; ISR packs into 24-bit `dma_buffer*` as MSW+LSW pairs (LSW = 0). Audible result identical to original at 16-bit precision. Future: replace LSW=0 with dither for 16-bit-noise-floor → 24-bit improvement (already planned per Session 3).

Pack cost: ~5µs per HT or TC ISR (16 frames × 2 channels × 4 stores). Block budget = 363µs at 44108Hz × 16 frames. Margin ~70×.

### F7 DMA flag positions — sharp edge

LISR/LIFCR control streams 0-3. HISR/HIFCR control streams 4-7. **Stream 4 (DAC2) and Stream 7 (DAC1) BOTH live in HISR/HIFCR.** Within HISR:
- Stream 4 flags: bits 0-5 (TCIF4=5, HTIF4=4)
- Stream 7 flags: bits 22-27 (TCIF7=27, HTIF7=26)

The ISR reads `DMA1_HISR` for both streams (NOT `DMA1_LISR` for stream 4 — that was a bug caught and fixed mid-implementation).

### Verification needed at next user test

1. Boot proceeds normally — no changes to user-visible behaviour.
2. **No audible test tone any more** (removed). DACs will play silence until mixer is ported. This is expected — verify on hardware that there's no popping, clicking, or noise during boot/operation.
3. Encoder/buttons/LCD/SD all still work — verifying that the new IRQ priority (4) and ISR work doesn't break any other path.
4. Power consumption may increase slightly because DMA + ISR are now firing every 180µs continuously — measure if relevant.

---

## Known Issues / Technical Debt (end of Session 7)

### Audio / RNG
- `GetRngValue()` calls must be masked: `uint16_t rnd = (int16_t)(GetRngValue() & 0x7FFF)`. Root cause not fully investigated; masking is the confirmed working fix. Note for future investigation.
- Sequencer wiring to ISR: sequencer.c is present but runs from main loop; dedicated TIM ISR needed for sub-millisecond step jitter.

### High Priority
1. **buttonHandler/ledHandler audit**: Not yet done. Button numbering is reversed from original (BUT_MODE1=31 vs original BUT_MODE1=35). Action logic in processPress() needs case-by-case audit against original buttonHandler_buttonPressed(). LED index mapping verified.

### Medium Priority
2. TIM2 not initialised — needed for CLK IN BPM interval measurement and MIDI RX timestamping
3. MidiParser RX not connected to sequencer/parameter system
4. Preset save: pattern/all/performance stubs in place; real implementations need sequencer data structures
5. Morph buffer not connected (parameters2[] declared, unused)
6. Slider-to-parameter mapping not designed
7. Screensaver timing is visibly off (clock recalculation needed)
8. copyClearTools.c: frontPanel calls commented out, need direct replacement

### Lower Priority / Future
9. SampleRom: SampleMemory.c is a safe no-op stub. Real implementation requires F765 flash_if.c (sector 6-11 layout confirmed, IRQ-disable + D-cache invalidate required).
10. SELECT_1 LED dark at boot — faithful reproduction of original LXR bug. Fix in repo 2 only: add `led_setActiveSelectButton(0)` after `menu_switchPage(VOICE1_PAGE)` in menu_init().
11. Non-blocking LCD: lcd_wait_ms() in lcd_init() is still blocking but only runs pre-TIM7. Safe as-is.
12. Parameters_reference.h.bak in Core/Preset/ — left for reference during reconciliation. Delete in a cleanup pass once full port is verified.

## Critical Reminders for Next Session

### General Process
- **ALWAYS extract a fresh tarball and verify the working directory before writing any code**
- The current known-good base is `lxr02_new_refactored.zip` (refactor session, 2026-05-04)
- `GetRngValue()` calls must mask the result: `& 0x7FFF` — do not remove this masking

### Boot / Init
- EXTI_IMR = 0 must remain as the very first operation in main(), before sysclk_init()
- Do NOT add pull-down to PD4 or PD5
- `lcd_init()` before `lcd_tim7_init()`
- `menu_init()` calls memset on parameter_values — do not also memset in main()

### Hardware
- TIM7_SR defined locally in timebase.c at 0x40001410 — do not remove
- 24-bit audio: `int16_t` MSW+LSW packed, I2SCFGR DATLEN=01/CHLEN=1
- I2S3_SD = PB5 (PC12 is SD SCLK — was misdocumented in early sessions)
- SD card: bit-bang SPI on PC12/PD2/PC8/PD0 (NOT hardware SPI1). PA8 was a false positive. **No SPI1 contention exists.**
- PE13/PE14: AF1 (TIM1_CH3/CH4), external 10kΩ pull-ups, no internal pull-up
- TIM1 IRQ27, TIM6 IRQ54, TIM7 IRQ55
- DMA1 Stream 4 (I2S2/DAC2) = IRQ15; DMA1 Stream 7 (I2S3/DAC1) = IRQ47. Both in HISR/HIFCR.

### Encoder (SW42)
- Encoder algorithm: Dannegger difference (NOT LUT)
- Seed: `last = new & 3` in encode_init() (NOT `(new + 3) & 3`)
- encode_read4() uses round-toward-zero divide (NOT arithmetic shift `>>= 2` — that was the asymmetry bug)
- encode_read1/read2 permanently removed — do not re-add
- `ts_dirs[]` direction buffer in encoder.c — rebound suppression. Do not remove.

### Audio / AudioCodecManager
- `audioCodec_init()` is the single entry point — replaces CodecInit() + audioTest_init() chain
- SPSC ready queue (2 slots) replaces bCurrentSampleValid flag
- Main loop pattern: `if (audioCodec_queueFreeSlots() > 0) { mixer_calcNextSampleBlock(...); audioCodec_commitRenderBuffer(); }`
- Stream 4 is the refill master; Stream 7 clears flags only
- `plli2s_init()` guard checks HSERDY (bit 17 of RCC_CR) — guard body never entered post-sysclk_init(), proceeds unconditionally. Intentional. See comments in AudioCodecManager.c.

### Menu / Display
- DO NOT TOUCH endlessPots (RV1-4) — `atan2f(b, a)`, do not change argument order
- LCD ring is head/tail-only SPSC (NO `lcd_q_count`). Do not reintroduce a shared count variable.
- `sendDisplayBuffer` emits `lcd_setcursor` before every data byte. Do not add position-tracking optimization.
- `while → if` in buttonHandler_processEvents() — intentional, do not revert
- Saturation pattern in `menu_encoderChangeParameter` and `menu_handleLoadSaveMenu`: int16 sum + clamp, NOT uint8 wrap + boundary check.
- `menu_knobs_dirty` flag + `menu_serviceKnobRepaint()` is RV1-4-only. **Do not extend dirty-flag coalescing to other input paths.**

### Failed Approaches — Do Not Retry
- **DO NOT attempt TIM7 idle gating.** Wakeup race confirmed broken. See Session 6 Part 5a.
- **DO NOT attempt broad repaint coalescing across all input paths.** See Session 6 Part 5b.
- **DO NOT use arithmetic right shift (`>>= 2`) on signed val in encode_read4().** Fixed with round-toward-zero divide.
