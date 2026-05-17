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
DATE: 2026-04-26
SESSION GOAL: Full menu system port, preset load/save, display stability, buttonHandler/ledHandler audit, directory restructure.
COMPLETED: Full menu system ported (all voice pages, global/MIDI page, load/save page). Preset load implemented (kit + globals). Display bugs fixed (active-param indicator, scroll sign, edit mode entry, MODE4 mapping, SELECT sub-page switching, LCD sync). LCD cursor implemented for save page name editing. Encoder init fix (last = new & 3). quadEnc renamed to endlessPots. Directory restructured. Button processing while→if fix. Load/save page display fixed.
VERIFIED ON HARDWARE: Parameter display, encoder navigation, SELECT sub-page switching, MODE page switching, kit load from SD card on boot. Single button presses stable. Multi-press serialized correctly.

CHANGES THIS SESSION:
- Core/Preset/Parameters.h: exact port of original (NUM_PARAMS=273), LXR-02 slider/encoder params appended beyond NUM_PARAMS
- Core/Menu/menu.h/.c: full port — parameter_dtypes[], valueNames[], repaintGeneric (4-column + edit mode), repaintLoadSavePage, encoder navigation, quad encoder parameter editing, load/save page UI, page switching, clamp-by-dtype value editing
- Core/Menu/MenuText.h: all label strings, PROGMEM stripped
- Core/Menu/menuPages.h: complete 16-page × 8-subpage table, PROGMEM stripped
- Core/Menu/Cc2Text.c/.h: full modTargets[] array (205 entries, all 6 voices), modTargetVoiceOffsets[], paramToModTarget[], getNumModTargets()
- Core/Preset/presetManager.c/.h: load-only — preset_loadDrumset() reads 8-byte name + END_OF_SOUND_PARAMETERS bytes, preset_loadGlobals() reads GLO.CFG
- Core/Hardware/frontPanel/buttonHandler.c/.h: rewritten — ISR-safe event ring, main-loop processing, MODE/SELECT/VOICE button handling
- encoder.c: encode_init() seed changed to last = new & 3 (was (new + 3) & 3 — that seed was a mistaken carry-forward, now corrected)
- Core/Hardware/frontPanel/IO/quadEnc.c/.h: renamed to endlessPots.c/.h — all function names, Makefile entry, references updated
- Full directory restructure: Core/Hardware/frontPanel/IO/, Core/Menu/, Core/Preset/, Core/MIDI/, Core/SampleRom/, Core/Sequencer/, Core/DSPAudio/ — all #include paths converted to bare filenames, Makefile rewritten
- buttonHandler.c: while → if in buttonHandler_processEvents() — intentional, do not revert

KNOWN ISSUES INTRODUCED: Display buffer desync on rapid SELECT button presses — top row can freeze showing content from a previous mode. Unresolved at end of session; see Part 9.
KNOWN ISSUES RESOLVED: Encoder first-click wrong direction (seed fix). Encoder mis-direction (A/B swap in menu_parseEncoder corrected). MODE4 mapping wrong (BUT_MODE1 - buttonNr formula). SELECT buttons not changing voice sub-pages. Active parameter indicator wrong (upr_three() port). Scroll indicator wrong (checkScrollSign() port). Edit mode entry wrong (btnClicked flag port). LCD flooding on fast encoder turns (menu_dirty flag pattern). LCD sync on fast repaints (position-tracking + queue size 128). Button processing flooding TIM7 queue (while→if). Load/save page indicator not clearing on cursor move (memset fix).

NEXT SESSION RECOMMENDED GOAL: 1. Display buffer fix — implement sendDisplayBuffer() queue-idle gate (see Part 9). 2. buttonHandler/ledHandler audit — compare against original open-source files, verify processPress() case-by-case.
BLOCKERS: None.

CRITICAL REMINDERS FOR NEXT SESSION:
- ALWAYS extract a fresh tarball before making changes. Verify the working directory matches the stated base before writing any code.
- EXTI_IMR = 0 must remain as the very first operation in main(), before sysclk_init()
- Do NOT add pull-down to PD4 or PD5
- lcd_init() must be called before lcd_tim7_init()
- TIM7_SR defined locally in timebase.c at 0x40001410 — do not remove
- menu_init() calls memset(parameter_values, 0, NUM_PARAMS) — do not also memset in main()
- DO NOT TOUCH endlessPots (RV1-4) encoder code — working correctly. Uses atan2f(b, a) — do not change argument order.
- 24-bit audio: DMA buffers are int16_t packed MSW+LSW, I2SCFGR DATLEN=01/CHLEN=1 — do not revert
- PE13/PE14 are AF1 (TIM1_CH3/CH4) — do NOT reconfigure as GPIO or enable internal pull-ups
- Encoder uses Dannegger difference algorithm, seeded with last = new & 3. encode_read4() only — read1/read2 permanently removed.
- TIM1_CC_IRQHandler at IRQ27. TIM6 at IRQ54. TIM7 at IRQ55.
- encoder_tim14_tick() is an empty stub — TIM14 not used
- while → if in buttonHandler_processEvents() is intentional — do not revert to while
- Rate-limit attempt (20ms timer on sendDisplayBuffer) was tried and caused input/display desync. Do not attempt again without a clear separation between periodic and input-driven repaint paths.
```

---

## Session 5 — 2026-04-26 (Menu System, Preset Manager, Display, Button Handler)

**Goal**: Full menu system port, preset load/save, display stability, buttonHandler/ledHandler audit, directory restructure.

**Working tarball at start**: `lxr02_enc_accel_v2.tar.gz`  
**Working tarball at end**: `lxr02_reorganized.tar.gz` — this is the current known-good base.

---

### Part 1 — Full menu system port

**Completed**:
- `Core/Preset/Parameters.h` — exact port of original (NUM_PARAMS=273), LXR-02 slider/encoder params appended beyond NUM_PARAMS
- `Core/Menu/menu.h/.c` — full port: parameter_dtypes[], valueNames[], repaintGeneric (4-column + edit mode), repaintLoadSavePage, encoder navigation, quad encoder parameter editing, load/save page UI, page switching, clamp-by-dtype value editing
- `Core/Menu/MenuText.h` — all label strings, PROGMEM stripped
- `Core/Menu/menuPages.h` — complete 16-page × 8-subpage table, PROGMEM stripped
- `Core/Menu/Cc2Text.c/.h` — full modTargets[] array (205 entries, all 6 voices), modTargetVoiceOffsets[], paramToModTarget[], getNumModTargets()
- `Core/Preset/presetManager.c/.h` — load-only: preset_loadDrumset() reads 8-byte name + END_OF_SOUND_PARAMETERS bytes, preset_loadGlobals() reads GLO.CFG
- `Core/Hardware/frontPanel/buttonHandler.c/.h` — rewritten: ISR-safe event ring, main-loop processing, MODE/SELECT/VOICE button handling

**Key porting decisions**:
- PROGMEM / pgm_read_* stripped — direct array access on Cortex-M7
- frontPanel_sendData() → stubbed (no DSP connected yet)
- Encoder direction: original AVR did `inc *= -1` — NOT done on F765 (hardware already oriented correctly)
- MODE button offset: BUT_MODE1=31 > BUT_MODE4=28 (reversed from original). Formula `BUT_MODE1 - buttonNr` gives correct 0-3 mode index
- buttonHandler_buttonPressed/Released — called from TIM6 ISR, must only write to event ring. All menu/LCD work deferred to buttonHandler_processEvents() in main loop

**Verified on hardware**: Parameter display, encoder navigation, SELECT sub-page switching, MODE page switching, kit load from SD card on boot.

---

### Part 2 — Display bugs fixed

**Issue**: Active parameter indicator was `>` to right of param name. Should be UPPERCASE param name, others lowercase.
**Fix**: `upr_three()` applied to active column. Exact port of original.

**Issue**: Scroll indicator position and logic wrong.
**Fix**: `checkScrollSign()` ported exactly from original. `>` at col 15 when 2nd page exists, `<` on 2nd page, `*` for global menu middle pages.

**Issue**: Encoder click entered edit mode immediately on button press (original: edit mode on button press, but movement only on subsequent encoder turn).
**Fix**: Exact port of `menu_parseEncoder()` — `btnClicked` flag, `inc == 0` early return after button edge check.

**Issue**: MODE4 button mapping wrong (COPY was opening load page, SHIFT+MODE4 was opening globals on its own).
**Fix**: `BUT_MODE1 - buttonNr` offset formula applied. MODE4 now correctly opens LOAD/SAVE, SHIFT+MODE4 opens MENU/globals (MODE4 LED blinks).

**Issue**: SELECT buttons not changing voice sub-pages.
**Fix**: `handleSelectButton()` calls `menu_switchSubPage(selectNr)` in VOICE mode.

**Issue**: LCD getting out of sync on fast repaints (character corruption).
**Fix**: Position-tracking in `sendDisplayBuffer()` — omits lcd_setcursor when next write follows previous sequentially. Queue size increased to 128. `menu_repaintAll()` now only called on button click (layout change); `menu_repaint()` called for encoder movement (incremental diff).

---

### Part 3 — LCD cursor (save page name editing)

**Issue**: Save page name editing needed hardware underscore cursor beneath selected character, not replacing it.
**Fix**: `cur_want_on/col/row` state in `sendDisplayBuffer` — cursor turned off before character writes, restored at end. Only emitted when cursor state changes.

**Issue**: cursor causing glitch — underline appearing at character write positions during fast encoder use.
**Root cause**: `lcd_turnOn(cursor_on)` and `lcd_turnOn(cursor_off)` were being queued every repaint. Async queue meant cursor-on from previous repaint was executing during character writes of next repaint.
**Fix**: Hardware cursor enabled only when `cur_want_on || cur_hw_on` — cursor-off queued before character writes, cursor-on queued after. State tracked with `cur_hw_on` flag.

---

### Part 4 — Encoder init fix

**Issue**: First encoder click after boot went down then back up (one wrong-direction step).
**Root cause**: `last = (new + 3) & 3` phase offset in `encode_init()` generated one wrong-direction step before settling. This seed was a mistaken carry-forward from earlier experimentation in Session 4 and is not correct.
**Fix**: Changed to `last = new & 3` — seed with actual current Gray code state.

---

### Part 5 — quadEnc → endlessPots rename

- `Core/Hardware/frontPanel/IO/quadEnc.c/.h` renamed to `endlessPots.c/.h`
- All function names, Makefile entry, and references updated
- Zero remaining `quadEnc` references

---

### Part 6 — Directory restructure

**New structure**:
```
Core/
├── Hardware/
│   ├── clocks.c/h, timebase.c/h, audioTest.c/h, triggerJacks.c/h
│   ├── frontPanel/
│   │   ├── buttonHandler.c/h, lcd.c/h, ledHandler.c/h
│   │   └── IO/  (adcPots, din, dout, encoder, endlessPots)
│   ├── SD/      (spi_sd, sd_routines, diskio, ff, kitBrowser)
│   └── USB/     (OTG_Driver, Device_Library, App)
├── Menu/        (menu, Cc2Text, CcNr2Text, MenuText, menuPages)
├── Preset/      (presetManager, Parameters.h)
├── MIDI/        (Uart, FIFO, MidiMessages)
├── SampleRom/   (empty)
├── Sequencer/   (empty)
└── DSPAudio/    (empty)
```
- All `#include` paths converted to bare filenames — broad `-I` flags in Makefile
- Makefile fully rewritten with new paths
- Binary size unchanged — pure restructure

---

### Part 7 — Load/Save page display fixes

**Issue**: `>` indicator not clearing when cursor moved off a position.
**Root cause**: `menu_repaint()` doesn't reset `editDisplayBuffer` before refilling — old `>` left at previous position.
**Fix**: `memset` both rows at start of `menu_repaintLoadSavePage()`.

**Issue**: Fast SELECT button mashing caused top row to freeze/desync.
**Root cause**: `buttonHandler_processEvents()` used `while` loop — multiple events drained per main loop iteration, each calling `menu_repaintAll()` and flooding TIM7 queue.

---

### Part 8 — Button processing: while → if

**Change** (`Core/Hardware/frontPanel/buttonHandler.c`, `buttonHandler_processEvents()`):
```c
// BEFORE:
while (evt_tail != evt_head) {

// AFTER:
if (evt_tail != evt_head) {
```

**Rationale**: Original AVR processed one button per main loop iteration (din_readNextInput read one button at a time). This naturally serialized button→repaint→drain cycles. Our TIM6 ISR reads all 40 buttons atomically every 1ms and queues all events simultaneously. `while` drained the entire ring in one loop iteration, firing multiple `menu_repaintAll()` calls back-to-back and flooding the TIM7 queue. `if` restores the one-button-per-iteration serialization, matching original behavior. F765 main loop speed (~50-200kHz) means latency is still well under 1ms — no sluggishness.

**Result**: Major improvement. Single button presses completely stable. Simultaneously-pressed buttons serialized correctly.

---

### Part 9 — Display buffer dirty flag (UNRESOLVED at end of session 5; resolved later)

**Remaining issue (at end of session 5)**: Even with `while → if` fix, rapidly pressing SELECT buttons repeatedly could still cause the top menu row to freeze showing content from a previous mode.

**Status update (resolved in subsequent work, see session 6)**: This was eventually resolved by replacing the `lcd_q_count` shared RMW variable in lcd.c with a head/tail-only SPSC ring (no shared count) plus the unconditional setcursor pattern in sendDisplayBuffer. The proposed `repaint_dirty + lcd_queue_count() == 0` gate suggested at end of session 5 was NOT the right fix — see session 6 for what actually worked.

---

## Known Issues / Technical Debt (end of Session 5)

- **Display buffer desync on rapid button presses** — see Part 9 above. Top row can freeze. Unresolved.
- buttonHandler/ledHandler audit not yet done (deferred from task list)
- TIM2 not initialised for CLK IN BPM interval measurement
- MidiParser RX not connected to sequencer/parameter system
- `paramToModTarget[]` / `Cc2Text.c` mod target gap-map not ported (stubs only)
- Preset save not implemented (load only)
- Pattern load/save not implemented
- Morph buffer not connected (parameters2[] declared, unused)
- Slider-to-parameter mapping not designed
- frontPanelParser.h — stub or eliminate decision pending (see directory restructure notes)
- Sequencer, DSPAudio, SampleRom directories empty

## Critical Reminders for Next Session

- **ALWAYS extract a fresh tarball before making changes. Verify the working directory matches the stated base before writing any code.**
- EXTI_IMR = 0 must remain as the very first operation in main(), before sysclk_init()
- Do NOT add pull-down to PD4 or PD5
- `lcd_init()` must be called before `lcd_tim7_init()`
- TIM7_SR defined locally in timebase.c at 0x40001410 — do not remove
- `menu_init()` calls `memset(parameter_values, 0, NUM_PARAMS)` — do not also memset in main()
- DO NOT TOUCH endlessPots (RV1-4) encoder code — working correctly. Uses `atan2f(b, a)` — do not change argument order.
- 24-bit audio: DMA buffers are `int16_t` packed MSW+LSW, I2SCFGR DATLEN=01/CHLEN=1 — do not revert
- PE13/PE14 are AF1 (TIM1_CH3/CH4) — do NOT reconfigure as GPIO or enable internal pull-ups
- Encoder uses Dannegger difference algorithm, seeded with `last = new & 3` (NOT `(new + 3) & 3`). encode_read4() only — read1/read2 permanently removed.
- TIM1_CC_IRQHandler at IRQ27. TIM6 at IRQ54. TIM7 at IRQ55.
- `encoder_tim14_tick()` is an empty stub — TIM14 not used
- `while → if` in buttonHandler_processEvents() is intentional — do not revert to while
- Rate-limit attempt (20ms timer on sendDisplayBuffer) was tried and caused input/display desync. Do not attempt again without a clear separation between periodic and input-driven repaint paths.

## Next Session Recommended Goal

1. **buttonHandler/ledHandler audit** — compare against original open-source files. Our button numbering is reversed from original (BUT_MODE1=31 vs original BUT_MODE1=35). Audit action logic in processPress() case-by-case against original buttonHandler_buttonPressed(). LED index mapping needs verifying against shift-register chain order.
2. **Display buffer fix** — implement `sendDisplayBuffer()` queue-idle gate as described in Part 9. Extract clean `lxr02_reorganized.tar.gz` first, verify lcd.c is clean, then apply.
