# Session 015 Handoff Log

DATE: 2026-05-09

SESSION GOAL: Reconnect and verify the sequencer/front-panel integration after Session 14 import work; resolve hardware-reported sequencer regressions; archive the root audit notes.

COMPLETED:
- Replaced original two-chip sequencer-to-front-panel UART feedback with local single-MCU dispatch.
- Wired Euclid/PATGEN, SOM, copy/clear, pattern/query, run/stop, record, chase, beat, automation, step, and main-step front-panel paths through local handlers.
- Fixed RV1 regression outside this session by user change: `NUM_PARAMS` moved down in `ParameterArray.h`; no further code change needed.
- Fixed sequencer step button behavior by correcting the AVR long-press timeout constant for the STM32 1ms tick.
- Fixed sequencer silent/no-trigger behavior by adding missing `seq_init()` at boot.
- Fixed internal sequencer tempo being 4x slow by converting the sequencer and sync math from original quarter-ms tick assumptions to this port's 1ms `systick_ticks`.
- Added future timing/jitter audit note: the 1kHz tick is too coarse for long-term sequencer quality; likely move to at least 10kHz high-priority timer.
- Fixed PATGEN/Euklid generated patterns not refreshing main-step LEDs.
- Fixed PATGEN/Euklid generated patterns being front-stacked by restoring real ARM/CMSIS `__CLZ(0) == 32` semantics in the port shim.
- Consolidated root audit notes into this archive log.

VERIFIED ON HARDWARE:
- Yes, by user feedback during the session:
- Bug 1 RV1 fixed by user `NUM_PARAMS` placement change.
- Bug 2 step LEDs/sub-step defaults/sequencer triggers reported clean after timeout + `seq_init()` work.
- Bug 3 tempo fix reported clean.
- Bug 4 PATGEN LED refresh reported working; follow-up front-stacked Euklid math fixed and user reported clean.
- Final archive/doc cleanup itself was not hardware-relevant.

CHANGES THIS SESSION:
- `Core/MIDI/frontPanelParser.c`: changed from mostly local forward dispatcher into active sequencer/front-panel bridge.
- `Core/MIDI/frontPanelParser.c`: added `seq_notifyFront()` for reverse `SEQ_CC` feedback that must not loop through `frontPanel_sendData()`.
- `Core/MIDI/frontPanelParser.c`: added `SeqLedState seq_ledState` consumer path via `seq_ledState_process()`.
- `Core/MIDI/frontPanelParser.c`: made `frontParser_handleLedMessage()` externally callable.
- `Core/MIDI/frontPanelParser.c`: wired `SEQ_EUKLID_LENGTH`, `SEQ_EUKLID_STEPS`, `SEQ_EUKLID_ROTATION`, `SEQ_REQUEST_EUKLID_PARAMS`.
- `Core/MIDI/frontPanelParser.c`: wired `SEQ_POSX`, `SEQ_POSY`, `SEQ_FLUX`, `SEQ_SOM_FREQ`.
- `Core/MIDI/frontPanelParser.c`: added main-step-only LED refresh after visible Euklid generation.
- `Core/MIDI/frontPanelParser.h`: added `SeqLedState`, dirty flags, `seq_notifyFront()`, `seq_ledState_process()`, and LED handler export.
- `Core/Sequencer/sequencer.c`: replaced sequencer-to-front-panel UART triplets with `seq_notifyFront()` or `SeqLedState` writes.
- `Core/Sequencer/sequencer.c`: fixed internal tempo delta to `60000 / (bpm * 96)` milliseconds for this port's 1ms `systick_ticks`.
- `Core/Sequencer/clockSync.c`: fixed MIDI clock BPM calculation to treat `systick_ticks` as milliseconds, removing the old `/4`.
- `Core/MIDI/MidiParser.c`: replaced MTC run/stop UART feedback with `seq_notifyFront()`.
- `Core/MIDI/MidiParser.c`: corrected MTC timeout from original quarter-ms `100 * 4` ticks to `100` ms.
- `Core/MIDI/MidiParser.c`: removed unsafe received-CC forwarding to the old AVR front panel path; local CC handling already updates state and forwarding would double-process.
- `Core/MIDI/MidiVoiceControl.c`: replaced `NOTE_ON` front-panel UART pulse with direct `led_pulseLed(LED_VOICE1 + voice)`.
- `Core/Menu/copyClearTools.c`: replaced five commented-out front-panel sequencer calls with direct `seq_clearAutomation`, `seq_clearPattern`, `seq_clearTrack`, `seq_copyTrack`, `seq_copyPattern`.
- `main.c`: included `frontPanelParser.h`, `EuklidGenerator.h`, `SomGenerator.h`.
- `main.c`: added `seq_init()` after `dsp_init()`; this is required to seed default pattern/sub-step state.
- `main.c`: added `euklid_init()` and `som_init()` after `seq_init()`.
- `main.c`: added `seq_ledState_process()` after `seq_tick()` in the main loop.
- `Makefile`: added `Core/Sequencer/EuklidGenerator.c`, `SomGenerator.c`, `SomData.c`.
- `Core/Hardware/frontPanel/buttonHandler.h`: changed `BUTTON_TIMEOUT` from original AVR tick count `38` to `500u` ms.
- `Core/Hardware/triggerJacks.h`: changed dormant `PULSE_LENGTH` from quarter-ms units to ms.
- `Core/compat/cmsis_intrinsics.h`: changed `__CLZ` from `__builtin_clz` to inline ARM `clz` instruction expression so `__CLZ(0)` returns 32.
- `AUDIT.md`, `HOOKUP-FPP-SEQ_AUDIT.md`, `LED-FPP_AUDIT.md`, `SEQ-FPP_AUDIT.md`, `SEQUENCER_AUDIT.md`: consolidated here; root copies should be removed after this file lands.

KNOWN ISSUES INTRODUCED:
- No intentional known issue introduced.
- Sequencer timing is now correct at gross BPM level, but clock quality/jitter was not audited. Current 1kHz `systick_ticks` is too coarse for final sequencer timing quality.
- Header dependency weakness remains in the Makefile. Header-only fixes, especially `cmsis_intrinsics.h`, require manually removing affected objects or doing a clean rebuild.

KNOWN ISSUES RESOLVED:
- RV1 encoder regression: resolved by user moving `NUM_PARAMS` down in `ParameterArray.h`.
- Sequencer step buttons not toggling reliably: `BUTTON_TIMEOUT` was wrong unit.
- Sequencer showed active main steps but produced no voices: `seq_init()` was missing, leaving `seq_patternSet` zeroed BSS instead of initialized defaults.
- First SELECT/sub-step LED dark after selecting an active main step: same missing `seq_init()` root cause.
- Sequencer tempo 4x slow: original mainboard 4kHz `systick_ticks` math was running against this port's 1kHz tick.
- PATGEN/Euklid steps written but LEDs not refreshed: parser did not refresh main-step LEDs after generated pattern mutation.
- PATGEN/Euklid generated steps front-stacked: ported `__CLZ` shim used undefined `__builtin_clz(0)`; original algorithm depends on ARM `CLZ(0) == 32`.
- Euclid/SOM/copy-clear backend paths identified as unconnected at audit start are now connected, except trigger backend remains stubbed.

NEXT SESSION RECOMMENDED GOAL:
- Hardware-test the full Session 15 image one more time across core sequencer workflows, then choose between:
- high-priority sequencer clock/jitter audit and timer migration design, or
- implement remaining trigger backend / clock-in/out PPQ behavior.

BLOCKERS:
- Need hardware measurement/feel test for final Session 15 image after `__CLZ` fix.
- Sequencer clock/jitter decision needs a timer/priority plan. 1kHz main-loop polling is known to be insufficient for final quality.
- Trigger backend remains mostly stubbed; cannot fully validate trigger PPQ/gate settings until implemented.

CRITICAL REMINDERS FOR NEXT SESSION:
- `Core/Sequencer/EuklidGenerator.c` is byte-for-byte identical to original LXR; do not "fix" its distribution logic. The crucial dependency is `__CLZ(0) == 32`.
- Do not route reverse-direction sequencer `SEQ_CC` feedback through `frontPanel_sendData()`; use `seq_notifyFront()` to avoid direction collisions.
- `seq_init()` must run at boot after `dsp_init()` and before sequencer-adjacent generator/menu startup.
- `BUTTON_TIMEOUT` is now milliseconds on this port, not original AVR ticks.
- Gross tempo math is now ms-based, but jitter remains unaudited. Future sequencer clock should likely run from a dedicated high-priority timer at >=10kHz.
- `seq_ledState_process()` must remain outside an ISR unless the pattern/LED access model is redesigned.
- Root audit files were consolidated into this archive log and should not remain in repo root.

---

## End Of Session Block

```
DATE: 2026-05-09
SESSION GOAL: Reconnect sequencer/front-panel paths and fix hardware-reported sequencer regressions from Session 15.
COMPLETED: Reverse LED/SEQ feedback path, Euclid/SOM/copy-clear wiring, sequencer default init, step-button timeout fix, 4x tempo fix, PATGEN LED refresh, CLZ/Euklid math fix, audit consolidation.
VERIFIED ON HARDWARE: Yes for main user-reported regressions by user feedback; final archive/doc cleanup not hardware relevant.

CHANGES THIS SESSION:
- Core/MIDI/frontPanelParser.c/h: local dispatcher expanded, reverse seq_notifyFront, SeqLedState consumer, Euclid/SOM/PATGEN LED refresh.
- Core/Sequencer/sequencer.c + clockSync.c: reverse feedback replacements and ms-based sequencer timing.
- Core/MIDI/MidiParser.c + MidiVoiceControl.c: reverse run/stop and voice LED feedback moved off old UART front-panel path.
- Core/Menu/copyClearTools.c: direct sequencer copy/clear calls.
- main.c + Makefile: sequencer generator init/build wiring, seq_init(), seq_ledState_process().
- Core/Hardware/frontPanel/buttonHandler.h: BUTTON_TIMEOUT corrected to 500ms.
- Core/compat/cmsis_intrinsics.h: __CLZ restored to ARM instruction semantics, including zero input.
- Core/Hardware/triggerJacks.h: dormant pulse length unit corrected.

KNOWN ISSUES INTRODUCED: none intentional; sequencer jitter still unaudited.
KNOWN ISSUES RESOLVED: RV1 regression, unreliable step toggles, no sequencer voices, dark default sub-step LEDs, 4x slow tempo, PATGEN LED refresh, PATGEN front-stacked Euclid math.

NEXT SESSION RECOMMENDED GOAL: Hardware-test final Session 15 image, then audit/migrate sequencer clock jitter or implement trigger backend.
BLOCKERS: Need hardware timing/jitter measurements and trigger-backend design/implementation.

CRITICAL REMINDERS FOR NEXT SESSION:
- __CLZ(0) must return 32; Euklid depends on it.
- Do not route reverse SEQ_CC feedback through frontPanel_sendData().
- seq_init() is required before menu/sequencer use.
- 1kHz sequencer timing is only a temporary gross-tempo fix; final clock should likely be >=10kHz high-priority timer.
```

---

## Consolidated Audit Details

### Reverse Sequencer To Front Panel

Original LXR used STM32 mainboard -> AVR UART byte triplets. On LXR-02, both sides live in one MCU, so those triplets must become local calls.

Replaced sequencer `uart_sendFrontpanelByte` triplets:
- Pattern change ack: `seq_notifyFront(FRONT_SEQ_CHANGE_PAT, seq_activePattern)`.
- Beat LED on/off: `seq_ledState.beatPulse`, dirty `SEQ_LED_DIRTY_BEAT`.
- Chase light: `seq_ledState.chaseStep`, dirty `SEQ_LED_DIRTY_CHASE`.
- Rotation reset on stop: `seq_notifyFront(FRONT_SEQ_TRACK_ROTATION, seq_getTrackRotation(i))`.
- Record sub-step/main-step LEDs: `seq_ledState.recordSubStep` / `recordMainStep`.

`seq_notifyFront()` is required because original front->mainboard and mainboard->front message values collide:
- `FRONT_SEQ_CC == SEQ_CC == 0xb2`.
- `FRONT_SEQ_CHANGE_PAT == SEQ_CHANGE_PAT`.
- `FRONT_SEQ_RUN_STOP == SEQ_RUN_STOP`.

Routing reverse messages through `frontPanel_sendData()` would re-trigger forward actions such as `seq_setNextPattern()` or `seq_setRunning()`. LED messages are safe through `frontParser_handleLedMessage()`, and the `SeqLedState` consumer does that in the main loop.

### Forward Front Panel To Sequencer

Button/menu paths audited:
- 31 `buttonHandler.c` sequencer/front-panel paths.
- 43 `menu.c` sequencer/front-panel paths.

Important connected paths:
- Main step: button -> `MAIN_STEP_CC` -> `seq_toggleMainStep()` -> LED update.
- Sub-step: shift+select -> `STEP_CC` -> `seq_toggleStep()` -> select LED update when visible.
- Run/stop, record, erase, mute, unmute, pattern change, roll, rotation, active track, active step, pattern params, automation arm, MIDI CC recording, BPM, quantization, shuffle.
- Copy/clear no longer uses commented-out front-panel protocol; it calls sequencer functions directly.

Remaining no-op/stub areas at end of Session 15:
- Trigger backend: `SEQ_TRIGGER_IN_PPQ`, `SEQ_TRIGGER_OUT1_PPQ`, `SEQ_TRIGGER_OUT2_PPQ`, `SEQ_TRIGGER_GATE_MODE`.
- `triggerJacks.c` still has stubs for sequencer-facing trigger voice/clock behavior.
- Dead SysEx step dump functions remain in `sequencer.c` as reference only.

### LED Architecture

`ledHandler.c` did not need structural changes. It remains the low-level LED driver.

`SeqLedState`:
- Written by sequencer code.
- Drained by `seq_ledState_process()` in the main loop after `seq_tick()`.
- Dispatches through `frontParser_handleLedMessage()`.

If `seq_tick()` moves to an ISR later:
- byte fields are atomic, but `dirty |=` vs read/clear can lose updates unless redesigned.
- `seq_ledState_process()` should stay out of high-priority ISR context because it queries `seq_patternSet` and calls LED/page logic.
- Pattern writers such as Euclid/SOM/copy-clear need race audit if sequencer reads move to ISR.

### Sequencer Init And Pattern Defaults

`seq_init()` was missing from startup. This was the root cause of:
- dark first SELECT/sub-step LEDs,
- active main steps producing no sound,
- manually toggled substeps still silent due to zero probability/volume.

`seq_clearTrack()` seeds the defaults:
- every first sub-step active (`k % 8 == 0`),
- note = `SEQ_DEFAULT_NOTE`,
- probability = `127`,
- volume = `100`,
- main steps clear,
- length/rotation reset.

Without `seq_init()`, `seq_patternSet` stays zeroed BSS. Main-step bit toggles can work visually while playback has no valid active/audible sub-step data.

### Timing Math

Original mainboard:
- `systick_ticks` configured at 4kHz.
- one tick = 0.25ms.
- sequencer code used `quarter_ms / 96 * 4`.

This port:
- `systick_ticks` increments from TIM6 at 1kHz.
- one tick = 1ms.
- old formula produced exactly 4x slow tempo.

Fixed internal math:
- `seq_deltaT = (1000.f * 60.f) / ((float)bpm * 96.f)`.

At 120 BPM:
- quarter note = 500ms.
- 96PPQ internal tick = 5.208ms.
- sequencer 32PPQ sub-step clock every third internal tick = 15.625ms.
- one main step = 8 substeps = 125ms.
- 16 main steps = 2 seconds.

Future timing audit:
- This only fixes gross BPM units.
- 1kHz timing is too coarse for final sequencer quality.
- Need high-priority fast timer, likely >=10kHz, and jitter analysis against audio render, TIM7 LCD, SD/USB, and main-loop polling.

### PATGEN / Euklid

Two separate bugs were resolved:

1. LED refresh:
- Euklid generation writes `seq_patternSet.seq_mainSteps[pattern][track]` directly through `euklid_transferPattern()`.
- Manual toggles update LEDs because they go through `MAIN_STEP_CC`.
- Parser now refreshes visible main-step LEDs after `SEQ_EUKLID_STEPS` and `SEQ_EUKLID_ROTATION`.
- Refresh is main-step-only so PATGEN SELECT LEDs remain pattern selectors.

2. Pattern distribution:
- `Core/Sequencer/EuklidGenerator.c` matches original LXR exactly.
- The bug was the ported CMSIS `__CLZ` shim.
- Original ARM `CLZ(0)` returns 32.
- `getObjSize(0)` depends on that to return 1.
- `__builtin_clz(0)` is undefined and LTO can exploit that, collapsing patterns into front-stacked pulses.
- Fixed by emitting inline `clz`.

Known expected values:
- 4/16 -> `0x1111` -> `1000100010001000`.
- 5/16 -> `0x1249` -> `1001001001001000`.
- 8/16 -> `0x5555` -> `1010101010101010`.

### Dead / Cleanup Candidates

- `seq_sendMainStepInfoToFront()` and `seq_sendStepInfoToFront()` are dead original SysEx protocol helpers. Keep for reference until cleanup; they document original packing.
- `sequencer_.c/.h` are legacy reference copies, not compiled.
- Root audit files were temporary and should be deleted after this archive log is committed/written.

### Build Status

Final clean image after Session 15 fixes:
- `build/LXRV2_lxr02.img`
- image size: 242932 bytes
- ELF: `text=242584, data=332, bss=72396`

Only usual nano syscall warnings were emitted (`_close`, `_lseek`, `_read`, `_write`) plus LTO serial compilation note.
