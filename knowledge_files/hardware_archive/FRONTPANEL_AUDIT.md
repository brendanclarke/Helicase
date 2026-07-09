# frontPanelParser Audit — AVR vs F765 Port Status

**Session 028 status**: this audit is historical. `Core/MIDI/frontPanelParser.c/h`
has now been deleted from the live firmware. Current code should not recreate a
parser or bridge; use the direct owner APIs documented in
`knowledge_files/MODULE_INTERCHANGE_SPEC.md`.

**Scope**: `frontPanelParser.c` (556 lines), `frontPanelParser.h` (167 lines),
and every call site that touches `frontPanel_sendData()` / `frontPanel_parseData()`
across the AVR front code: `menu.c`, `buttonHandler.c`, `copyClearTools.c`.

**Bottom line**: The inter-processor serial protocol is gone. Every
`frontPanel_sendData()` call becomes a direct function call into the sequencer
or DSP engine. Every `frontPanel_parseData()` call (which was fired by the STM32
pushing data over UART) becomes a direct call into the menu. The file itself
does not port — it is replaced by wiring. What matters is cataloguing every
message type and confirming what it needs to connect to.

---

## 1. The Protocol — What It Was

The AVR and STM32F4 communicated over USART at 500kbaud using pseudo-MIDI
messages. The AVR *sent* button/encoder events to the STM32 and *received*
LED state, sequencer state, and parameter updates back.

On the F765 this is replaced entirely by direct function calls. There are no
UART messages to parse, no bytes to format. `frontPanel_sendData()` and
`frontPanel_parseData()` are both eliminated as concepts.

Total `frontPanel_sendData()` call sites across all AVR front source: **178**.
These are the 178 places where direct calls into sequencer/DSP functions must
be substituted during porting.

---

## 2. Inbound Messages — What the AVR *Received* from the STM32

These arrived via `frontPanel_parseData()` and drove the menu/LEDs.
On F765 these become direct calls **into** the menu from the sequencer tick.

### 2a. MIDI_CC (0xB0) — Parameter sync
The STM32 pushed parameter values to the AVR using standard CC messages
(CC number = parameter index, value = 0-127). On receipt the AVR updated
`parameter_values[]` and called `menu_repaint()`.

**F765 status**: `parameter_values[]` exists and is the same array.
Direct writes to it from the sequencer/DSP already work.
`menu_repaint()` needs to exist and be callable — stub needed for Phase 1.

**Pitfall**: NRPN handling (CC numbers 96–99 — Data Entry Coarse, Fine,
Coarse MSB/LSB). The AVR used NRPN to receive parameters above index 127
since standard CC only carries values 0-127. On F765 there are no MIDI
CC constraints — the sequencer can write directly to `parameter_values[]`
at any index. The NRPN decode logic in `frontParser_parseNrpn()` is
**entirely eliminated** — replace with a direct array write.

### 2b. LED_CC (0xB1) — LED state from sequencer
Three sub-messages:

- `LED_CURRENT_STEP_NR`: Sequencer tells front which step is active
  (chaselight). Calls `led_setActive_step(stepNr)`.
  **F765**: `led_setActive_step()` does not exist in current `ledHandler.h`.
  Needs adding.

- `LED_PULSE_BEAT`: Beat indicator — turns `LED_START_STOP` on/off.
  Calls `led_setValue()`.
  **F765**: `led_setValue()` exists. `LED_START_STOP` enum value exists (38).
  **Ready to use.**

- `LED_SEQ_BUTTON`: Lights up a step LED when its step is active in the
  pattern. Calls `led_setValue(1, LED_STEP1 + stepNr)`.
  **F765**: `led_setValue()` exists. `LED_STEP1` through `LED_SEQ16` exist
  in `ledHandler.h` (as `LED_SEQ1` etc — naming differs slightly, needs
  cross-check).
  **Pitfall**: Original LED numbering uses `LED_STEP1` (= 18 in AVR enum).
  F765 uses `LED_SEQ1` (= 32). These are different physical LED assignments.
  The mapping table needs verifying against the actual hardware layout.

- `LED_SEQ_SUB_STEP`: Sub-step display when in step-edit mode.
  Calls `led_setValue()` with SELECT LEDs.
  **F765**: `led_setValue()` exists. SELECT LEDs exist. **Ready to use.**

### 2c. SEQ_CC (0xB2) — Sequencer state sync to menu

Most of these write to `parameter_values[]` and call `menu_repaint()`.

| Sub-message | Writes to | menu call | F765 status |
|---|---|---|---|
| SEQ_SET_PAT_BEAT | PAR_PATTERN_BEAT | repaint | param exists, menu stub needed |
| SEQ_SET_PAT_NEXT | PAR_PATTERN_NEXT | repaint | param exists |
| SEQ_TRACK_LENGTH | PAR_TRACK_LENGTH | repaint | param exists |
| SEQ_TRACK_ROTATION | PAR_TRACK_ROTATION | repaint | param exists |
| SEQ_EUKLID_LENGTH | PAR_EUKLID_LENGTH | repaint | param exists |
| SEQ_EUKLID_STEPS | PAR_EUKLID_STEPS | repaint | param exists |
| SEQ_EUKLID_ROTATION | PAR_EUKLID_ROTATION | repaint | param exists |
| SEQ_VOLUME | PAR_STEP_VOLUME | repaintAll | param exists |
| SEQ_PROB | PAR_STEP_PROB | repaintAll | param exists |
| SEQ_NOTE | PAR_STEP_NOTE | repaintAll | param exists |

**SEQ_CHANGE_PAT** (pattern change acknowledgement) is more complex:
calls `menu_setShownPattern()`, `led_clearSequencerLeds()`,
`led_setBlinkLed()`, `led_clearSelectLeds()`, `led_clearAllBlinkLeds()`,
`led_initPerformanceLeds()`, and queries `menu_getActiveVoice()` /
`menu_getViewedPattern()` / `buttonHandler_getMode()`.

**F765 status**:
- `led_clearSequencerLeds()` — **does not exist** in F765 ledHandler.
- `led_initPerformanceLeds()` — **does not exist** in F765 ledHandler.
- `menu_setShownPattern()`, `menu_getActiveVoice()`, `menu_getViewedPattern()` —
  **do not exist** yet (menu not ported).
- `buttonHandler_getMode()` — **does not exist** in F765 buttonHandler.
  F765 buttonHandler has no mode concept at all yet.
- `menu_activePage`, `menu_shownPattern`, `menu_playedPattern` — **do not exist**.

**SEQ_RUN_STOP**: Calls `buttonHandler_setRunStopState(running)`.
**F765**: `buttonHandler_setRunStopState()` — **does not exist**.

### 2d. SET_P1_DEST / SET_P2_DEST / SET_P1_VAL / SET_P2_VAL — Step automation
Write to `PAR_P1_DEST`, `PAR_P2_DEST`, `PAR_P1_VAL`, `PAR_P2_VAL` and call
`menu_repaintAll()`.

**F765 status**: These parameter enum values are **not in the current F765
Parameters.h**. They exist in the original AVR Parameters.h between indices
239-241. They need adding.

Also uses `paramToModTarget[]` array — a translation table mapping parameter
indices to modulation target indices. This is built at startup by
`paramToModTargetInit()` in `Cc2Text.c`. That entire module does not exist
yet in F765 code. It is a significant dependency of the automation system.

### 2e. PRESET_NAME (0xB4) — Preset name sync
Receives preset name 2 characters at a time, assembles into
`preset_currentName[]`, calls `menu_repaintAll()`.

**F765 status**: `preset_currentName[]` — **does not exist**. Preset system
not started.

### 2f. SAMPLE_CC (0xC0) — Sample upload
`SAMPLE_COUNT` sub-message calls `menu_setNumSamples()`.

**F765 status**: Samples are not supported on LXR-02 (no sample ROM).
This message type can be **permanently omitted**.

### 2g. NOTE_ON (0x90) — Voice trigger indicator
Calls `led_pulseLed(LED_VOICE1 + voiceNr)` to flash the voice LED when
the sequencer triggers a voice.

**F765 status**: `led_pulseLed()` **exists** in F765 ledHandler.
`LED_VOICE_1` through `LED_VOICE_7` exist in F765 ledHandler.
**Pitfall**: AVR uses `LED_VOICE1` (lowest index voice). F765 uses
`LED_VOICE_1` (enum value 6) because the shift register bit order is
reversed — voice 1 is at bit position 6, voice 7 at position 0.
The mapping must be confirmed against the physical hardware before wiring
this up, or voice LEDs will flash on the wrong caps.

### 2h. SysEx — Step data transfer
Used to load step parameters (volume, prob, note, param1, param2) from
sequencer to display. Elaborate 7-bit encoding to fit 8-bit data through
7-bit MIDI pipe.

**F765 status**: The MIDI 7-bit encoding constraint is **gone** — on F765
the sequencer can pass step structs directly by pointer. The entire SysEx
encode/decode machinery is eliminated. `StepData` struct ports directly from
`SeqStep.h` with no changes needed (no `avr/io.h` dependency in the struct
itself).

---

## 3. Outbound Messages — What the AVR *Sent* to the STM32

These are the 178 `frontPanel_sendData()` calls. They become direct calls
into sequencer/DSP functions. Grouped by category:

### 3a. Parameter writes (the majority — ~60 call sites in menu.c)

`frontPanel_sendData(MIDI_CC, paramNr, value)` and
`frontPanel_sendData(CC_2, paramNr-128, value)` — the CC_2 variant handled
parameters above index 127 (another MIDI 7-bit workaround, fully eliminated).

**F765 replacement**: Direct write to the DSP/sequencer parameter. The exact
function depends on the parameter type — some are audio DSP parameters (voice
filter, envelope, LFO), some are sequencer parameters (BPM, track length).
None of these DSP functions exist yet. Until the DSP engine is ported, these
calls are stubs that only write to `parameter_values[]`.

### 3b. Sequencer control (~30 call sites in buttonHandler.c + menu.c)

Examples:
- `frontPanel_sendData(SEQ_CC, SEQ_CHANGE_PAT, patternNr)` → `seq_changePattern(patternNr)`
- `frontPanel_sendData(SEQ_CC, SEQ_RUN_STOP, 1)` → `seq_setRunning(1)`
- `frontPanel_sendData(SEQ_CC, SEQ_SET_ACTIVE_TRACK, voice)` → `seq_setActiveTrack(voice)`
- `frontPanel_sendData(SEQ_CC, SEQ_MUTE_TRACK, voice)` → `seq_muteTrack(voice)`
- `frontPanel_sendData(SEQ_CC, SEQ_REQUEST_STEP_PARAMS, stepNr)` → `seq_getStepParams(stepNr)`
- `frontPanel_sendData(SET_BPM, lo, hi)` → `seq_setBpm(value)` (BPM was split
  across 2 × 7-bit bytes to fit MIDI — on F765 pass the 16-bit value directly)

**F765 status**: None of these sequencer functions exist yet. They are
all stubs during Phase 1.

### 3c. LED queries (~10 call sites)

`frontPanel_sendData(LED_CC, LED_QUERY_SEQ_TRACK, value)` — AVR asked
the STM32 to send back the current step states for all 16 steps of a track
so the AVR could light up the correct step LEDs. This was necessary because
the AVR had no direct access to sequencer state.

**F765 replacement**: **Eliminated entirely**. On F765 the menu has direct
read access to sequencer state. The step LEDs are set by reading
`seq_patternSet` directly, with no query/response round-trip needed.

### 3d. Automation arm/disarm (~5 call sites in buttonHandler.c)

`frontPanel_sendData(ARM_AUTOMATION_STEP, stepNr, track|onOff)` →
direct call into sequencer automation system.

**F765 status**: Sequencer automation not started. Stub.

### 3e. Copy/clear operations (copyClearTools.c — 5 call sites)

Clear automation, clear pattern, clear track, copy track, copy pattern —
all become direct sequencer calls.

**F765 status**: Sequencer not started. Stubs.

### 3f. Trigger/clock prescaler (~4 call sites)

`SEQ_TRIGGER_IN_PPQ`, `SEQ_TRIGGER_OUT1_PPQ`, `SEQ_TRIGGER_GATE_MODE` →
direct calls into `triggerJacks.c` functions that don't exist yet (TIM2
for interval measurement not initialised).

**F765 status**: `triggerJacks.c` has CLK OUT/IN/RST hardware confirmed
but the BPM measurement and prescaler logic is not implemented. These are
known gaps from FIRMWARE_STATE.md.

---

## 4. Functions frontPanelParser Called That Don't Exist in F765

This is the precise gap list for the menu/button layer:

### In ledHandler (missing from F765):
- `led_setActive_step(stepNr)` — chaselight
- `led_clearActive_step()` — clear chaselight
- `led_clearSequencerLeds()` — clear all 16 step LEDs
- `led_clearSelectLeds()` — clear select button LEDs
- `led_initPerformanceLeds()` — set up performance mode LED layout
- `led_setActiveVoice(voiceNr)` — highlight active voice LED
- `led_setActivePage(pageNr)` — highlight active page mode LED

### In buttonHandler (missing from F765):
- `buttonHandler_getMode()` — returns SELECT_MODE_VOICE / PERF / STEP etc.
- `buttonHandler_getShift()` — returns shift button state
- `buttonHandler_setRunStopState(running)` — sync run/stop LED from MIDI
- `buttonHandler_muteVoice(voice, isMuted)` — mute state
- `buttonHandler_selectedStep` (extern variable) — currently selected step

### In menu (not ported yet):
- `menu_repaint()` / `menu_repaintAll()` — entire menu redraw
- `menu_getActiveVoice()`, `menu_getViewedPattern()`
- `menu_setShownPattern()`, `menu_setNumSamples()`
- `menu_activePage`, `menu_shownPattern`, `menu_playedPattern` (globals)
- All page constants: `PERFORMANCE_PAGE`, `SEQ_PAGE`, `EUKLID_PAGE`,
  `MENU_MIDI_PAGE`, `PATTERN_SETTINGS_PAGE`

### In Parameters.h (missing from F765):
- `PAR_P1_DEST`, `PAR_P2_DEST`, `PAR_P1_VAL`, `PAR_P2_VAL`
- `PAR_AUTOM_TRACK`
- `PAR_POS_X`, `PAR_POS_Y`, `PAR_FLUX`, `PAR_SOM_FREQ`
- `NRPN_DATA_ENTRY_COARSE`, `NRPN_FINE`, `NRPN_COARSE` (can be omitted —
  NRPN is a MIDI-only workaround, not needed on F765)
- `PAR_MIDI_CHAN_7`, `PAR_MIDI_CHAN_GLOBAL`
- `PAR_MIDI_ROUTING`, `PAR_MIDI_FILT_TX`, `PAR_MIDI_FILT_RX`
- `PAR_PRESCALER_CLOCK_IN/OUT1/OUT2`, `PAR_TRIG_GATE_MODE`
- `PAR_BAR_RESET_MODE`
- `END_OF_SOUND_PARAMETERS` sentinel (needed by paramToModTarget init)

### Entirely new modules needed (not in F765, not trivially stubbed):
- `paramToModTarget[]` array + `paramToModTargetInit()` — required for LFO
  and velocity modulation target UI. Lives in `Cc2Text.c` (~420 lines).
  Depends on `modTargets[]` array which is a PROGMEM table in `menu.h`.
  Can be ported as a RAM table (no PROGMEM on F765).
- `preset_currentName[]` + preset system — entire `presetManager.c` +
  FatFS + SD SPI coordination. Large — separate session.

---

## 5. Things That Are Cleaner on F765 (Eliminations)

These AVR mechanisms have no equivalent on F765 and are simply dropped:

- **UART framing**: All `frontPanel_sendData()`, `frontPanel_parseData()`,
  `frontPanel_sendByte()`, `uart_putc()` calls — gone entirely.
- **ATOMIC_BLOCK**: AVR interrupt masking around UART sends — replaced by
  the natural thread safety of direct function calls (or brief `cpsid i`
  where needed for shared state).
- **NRPN encoding**: 7-bit MIDI constraint workaround for parameters > 127
  — gone. Write `parameter_values[paramNr]` directly.
- **SysEx 7-bit packing**: Step data packed into 7-bit chunks for MIDI pipe
  — gone. Pass `StepData *` directly.
- **LED query/response round-trip**: `LED_QUERY_SEQ_TRACK` request and
  response — gone. Read sequencer state directly.
- **SAMPLE_CC / sample upload**: No sample ROM on LXR-02 — omitted.
- **`<avr/io.h>`, `<util/atomic.h>`, `<avr/pgmspace.h>`**: All AVR-specific
  headers — gone. RAM replaces PROGMEM throughout.

---

## 6. Priority Order for Porting

Based on what blocks what:

1. **Parameters.h** — complete the enum to match original (adds ~15 missing
   entries). Unblocks everything else. Low risk, no code logic.

2. **ledHandler additions** — `led_setActive_step()`, `led_clearSequencerLeds()`,
   `led_clearSelectLeds()`, `led_setActiveVoice()`. These are straightforward
   additions to the existing working ledHandler. Unblocks the menu LED layer.

3. **buttonHandler additions** — `getMode()`, `getShift()`, `setRunStopState()`.
   Mode state machine is the core of buttonHandler and drives most of the menu
   branching. This is the largest single buttonHandler task.

4. **Menu shell** (current session goal) — `menu_init()`, `menu_repaint()`,
   `sendDisplayBuffer()`, `menu_parseEncoder()`, stubbed page rendering.
   Depends on 1-3 above.

5. **Sequencer** — `seq_tick()`, `seq_triggerNextMasterStep()`, all SEQ_CC
   handlers. Largest single component. Depends on DSP audio engine.

6. **DSP audio engine** — voices, envelopes, filters, LFOs. Depends on
   sequencer for triggering.

7. **Preset / FatFS / SD** — load/save. Depends on sequencer state being
   fully defined. Needs SPI1 mutex with TIM6.

8. **paramToModTarget / automation** — LFO and velocity modulation targets.
   Depends on menu and sequencer both being present.
