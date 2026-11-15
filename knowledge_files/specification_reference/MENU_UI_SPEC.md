# Menu and UI System Specification

## Authority and status

This is the authoritative reference for the menu system, LCD display, LED
management, button handling, and endless-pot controls, **current as of
Session 094** (2026-11-13, `dev-ph7-alkaline`).

This document covers the UI framework: page structure, cell resolution, the
dtype system, display and LED rendering, input handling, parameter commit flow,
and the Morph views.  Module boundary contracts (full API tables) live in
`MODULE_INTERCHANGE_SPEC.md`.  The Effect page is in
`EFFECTS_BUS_REFERENCE.md` §8.  The PERF performance surface and live record
controls are in `PERFORMANCE_RECORD_SPEC.md`.

Source files: `Core/Menu/menu.c/.h` (page system, cell resolution, edit
dispatch, parameter commit, repaint), `Core/Menu/menuPages.h` (static page
table), `Core/Menu/menuEffects.c/.h` (Effect page), `Core/Menu/MenuText.h`
(label strings), `Core/Menu/Cc2Text.c` (`modTargets[]`),
`Core/Hardware/frontPanel/buttonHandler.c/.h` (event ring, mode switching, SHIFT
state), `Core/Hardware/frontPanel/ledHandler.c/.h` (LED layers, blink/pulse/flash,
chase), `Core/Hardware/frontPanel/lcd.c/.h` (LCD queue, TIM7 driver),
`Core/Hardware/frontPanel/IO/endlessPots.c/.h` (RV1-4 analog pots),
`Core/Hardware/frontPanel/IO/encoder.c/.h` (main encoder SW42).

---

## 1. Page system

### 1.1 Page table

The static page table is `menuPages[NUM_PAGES][NUM_SUB_PAGES]` in
`menuPages.h`.  Each cell in a sub-page is a `(TEXT_*, PAR_*)` pair.
`TEXT_EMPTY` / `PAR_NONE` terminates the sub-page.

| Page index | Name | Content |
|---|---|---|
| 0..6 | `VOICE1`..`VOICE7` | Descriptor-backed instrument cells (resolved dynamically, not from the static table) |
| 7 | `MENU_MIDI_PAGE` | Globals, MIDI, Scene change divisions, bus compressor settings |
| 8 | `LOAD_PAGE` | Special case (filesystem UI, not from static table) |
| 9 | `SAVE_PAGE` | Special case (filesystem UI, not from static table) |
| 10 | `PERFORMANCE_PAGE` | PERF Morph cells (`mrp fxm 1VM..6VM`), two sub-pages |
| 11 | `SEQ_PAGE` | STEP track settings and step specials |
| 12 | `EUKLID_PAGE` | Euclidean generator |
| 13 | `PATTERN_SETTINGS_PAGE` | Chain editor (`cn1 nx1 cn2 nx2`) |
| 14 | `RECORDING_PAGE` | SHIFT+REC settings (overlay types, rhythms, `qnt`, `rtk`) |
| 15 | `SOM_PAGE` | SOM generator |
| 16 | `EFFECT_PAGE` | Registry-driven (resolved by `menuEffects.c`, not from static table) |

### 1.2 Sub-page navigation

Each page has up to `NUM_SUB_PAGES` (8) sub-pages.  The encoder crosses
sub-pages through a static cross-sub-page step in `menu_moveToMenuItem()`
(PERFORMANCE_PAGE, RECORDING_PAGE, MENU_MIDI_PAGE; non-cyclic).  The MODE MENU
re-press cycle (`menu_switchSubPage()`) advances from a global sub-page without
a second half to the next populated one.

### 1.3 Compact and full view

VOICE and EFFECT pages use `menu_isScreenPage()`: a compact four-cell model.
Row 0 shows the three-character label; row 1 shows the three-character value.
Other pages use a generic eight-cell layout across two LCD rows.

---

## 2. Cell resolution and editing

### 2.1 VOICE pages (descriptor-backed)

VOICE cells are resolved dynamically from the active Scene's instrument
descriptors, not from `menuPages.h`.  Each cell resolves to a descriptor row
with a parameter ID, display type, label, value range, and Morph/automation
attributes.  The descriptor system allows each instrument type (Drum, Snare,
Cymbal, HiHat) to present its own parameter layout.

#### 2.1.1 Oscillator type menu (`typ`, S094 P1 S3)

The `typ` row on the main oscillator selects the oscillator reader.
`DTYPE_MENU` with id `MENU_OSC_TYPE`.  Not morphable, stored single-ended at
`OscInfo.oscType` byte 34.

| `typ` value | Name | Reader |
|---|---|---|
| 0 | `wav` | `OSC_TYPE_WAV` — stock waves only |
| 1 | `smp` | `OSC_TYPE_SMP` — one-shot sample reader |
| 2 | `slp` | `OSC_TYPE_SLP` — looped sample reader |
| 3 | `wtb` | `OSC_TYPE_WTB` — wavetable with frame blending |
| 4 | `pwm` | `OSC_TYPE_PWM` — pulse-width modulation (S094) |

`OSC_TYPE_MODLIST` is 5 (the menu list length: sin, saw, rec, noi, pwm).  The
stored `typ` byte uses indices 0-4; `pwm` = 4.  PWM is present on Drum,
Snare, Cymbal, and HiHat.

When `typ` = `pwm`, the `pos` knob controls pulse width: 0 = 50 % duty cycle
(unmodified wave), 127 = narrowest pulse.  Hybrid mechanism: warp (W) for
Sin/Tri, two-read (D) for Saw/Rec.

#### 2.1.2 Alkaline mix page (S094 P1 S4)

The Alkaline MIX sub-page layout:

| Cell | Label | Descriptor row | Range | Meaning |
|---|---|---|---|---|
| 0 | `vol` | `instrument_volume` | 0..127 | output volume |
| 1 | `pan` | `instrument_pan` | 0..127 (centre 64) | stereo pan |
| 2 | `ton` | `alk_tone` (row 30) | 0..127 | low-pass tone: 127 = no filtering, 0 = maximum low-pass |
| 3 | `drv` | `instrument_drive` (row 29) | 0..127 | overdrive: 0 = unity gain, 127 = maximum overdrive |

Row 29 (`instrument_drive`) uses the shared key with the Alkaline branch in
the writer.  Row 30 (`alk_tone`) is Alkaline-specific.

#### 2.1.3 LFO sync rows (S094 P1 S1)

Five `lfo_sync` rows become `ROW_MENU_SPECIAL` (named menu dtype).  The sync
writer is `lfo_setSync(v)` — writer 23; `IM_SPECIAL_WRITER_COUNT` 24.

The LFO rate table `lfoRateHz[128]`: frq 0 = stopped (DC), frq 1 = 100 s
period (0.01 Hz), semitone grid from index 63 to 127 (musical rate
quantisation above the midpoint).

### 2.2 Effect page (registry-driven)

Effect cells are resolved through `menuEffects_resolveCell()`.  Layout screens
and cell content come from the active type's registry entry
(`effect_registry_entry_t`).  `menu.c` delegates cursor navigation, format, and
commit to `menuEffects_*`.  Full specification: `EFFECTS_BUS_REFERENCE.md` §8.

### 2.3 Static pages

Global, STEP, PERF, RECORDING, PATTERN_SETTINGS, EUKLID, SOM pages use the
static `menuPages` table.  Cell identity comes from the `PAR_*` constant.

### 2.4 Parameter commit flow

All panel and MIDI edits converge on `menu_commitTargetEdit(id, value, origin)`:

1. Clamp to the menu domain.
2. Commit the Normal image (or Morph image for Morph-view edits).
3. Fan out through `bank_editTargetMask()` to every Scene in the VOICE edit
   mask.
4. Apply R12 (Instrument) or R13 (Effect) automation priority actions.
5. Record in LATCH/OVERDUB (`liveRec_paramIn()`).
6. Request repaint.

MIDI writes (`MidiControl`) reach the same function.

---

## 3. Display type system (dtype)

Each parameter has a display type that controls formatting, editing range, and
special behaviour.  All 16 dtype id/nibble slots are used; new cells must use
an existing dtype plus static-param special cases.

| ID | Name | Range | Display |
|---|---|---|---|
| 0 | `DTYPE_0B255` | 0..255 | numeric (wide) |
| 1 | `DTYPE_0B127` | 0..127 | numeric |
| 2 | `DTYPE_LFO_POLARITY` | 0..2 | three-state LFO polarity text (stored as the DSP enum, not a menu-table id) |
| 3 | `DTYPE_MENU` | varies by param | names from a lookup table |
| 4 | `DTYPE_PM63` | 0..127, display − 63 | signed |
| 5 | `DTYPE_1B16` | 1..16 | numeric (1-based) |
| 6 | `DTYPE_ON_OFF` | 0..1 | on/off |
| 7 | `DTYPE_MIX_FM` | varies | FM amount display |
| 8 | `DTYPE_TARGET_SELECTION_LFO` | varies | LFO target selection |
| 9 | `DTYPE_TARGET_SELECTION_VELO` | varies | velocity target selection |
| 10 | `DTYPE_VOICE_LFO` | varies | voice LFO setting |
| 11 | `DTYPE_AUTOM_TARGET` | varies | automation target selection |
| 12 | `DTYPE_0b1` | 0..1 | numeric boolean |
| 13 | `DTYPE_NOTE_NAME` | 0..127 | MIDI note name |
| 14 | `DTYPE_0B15` | 0..15 | numeric (narrow) |
| 15 | `DTYPE_1B128` | 1..128 | numeric (1-based wide) |

Saturation in `menu_encoderChangeParameter()` / `menu_handleLoadSaveMenu()`:
int16 sum + clamp (not modular arithmetic).

---

## 4. LCD display

### 4.1 Hardware

WS0010 OLED 16×2 display, 4-bit parallel interface.  Pins: RS = PE12, E = PE11,
DB4..7 = PE7..PE10.

### 4.2 Async queue architecture

All writes go into a 128-entry SPSC ring drained by TIM7 at 5 kHz (200 µs per
state-machine step).  The public API (`lcd_command()`, `lcd_data()`,
`lcd_string()`, `lcd_setcursor()`, `lcd_clear()`) enqueues operations and
returns immediately.  A full queue drops the operation rather than blocking.

**Binding rule:** the queue is head/tail-only SPSC.  Do NOT reintroduce
`lcd_q_count` (removed).

`sendDisplayBuffer()` in `menu.c` emits `lcd_setcursor` before every data byte.
This is intentional (the WS0010 auto-increment is not reliable across all
display units).

### 4.3 CGRAM

The WS0010 has 8 CGRAM slots (addresses 0..7).  `lcd_define_char(location,
charmap)` programs one slot.  `lcd_underlineGlyph(ascii, out)` produces an
underlined version of a ROM character for the automation-presence markers.  Menu
uses slots 2..5 as an underline cache.

### 4.4 Diagnostic display

`lcd_diagDisplayInt()` / `lcd_diagDisplayFloat()`: two-row labelled display
helpers for dev-mode diagnostics.  Only used in diagnostic builds.

---

## 5. Button handling

### 5.1 Event ring

Button presses and releases from the 500 Hz foreground scan
(`din_dout_exchange()`) are recorded into a 64-entry monotonic-counter event ring
(`buttonHandler_buttonPressed()` / `buttonHandler_buttonReleased()`).

`buttonHandler_processEvents()` drains **one** event per call and executes
foreground UI actions.  It is called **twice** per main-loop pass (separated by
`audio_check_and_render()`).

**Binding rule:** `buttonHandler_processEvents()` is `if`, not `while` —
intentional.  It must never call `audio_check_and_render()` itself.

On ring overflow, `buttonHandler_processEvents()` reconciles the
VOICE-Scene/Load-Scene pairing masks and the hold timer.

### 5.2 Mode switching

`handleModeButtons()` is the only writer of `selectButtonMode`.  With SHIFT held,
a MODE button selects `(mode + 4) & 7` (SHIFT+VOICE = VOICE Morph latch).
SHIFT-derived view state set in the old mode must not rely on the SHIFT release
(it runs in the new mode).

### 5.3 SHIFT state

`bh_shiftHeld` and `bh_clearHeld` are updated before any early exit in the event
processing.  The SHIFT LED state is managed separately from the physical hold
state because latched menus (SHIFT+REC, chain editor) keep SHIFT lit after
release.

### 5.4 Long-press promotion

`buttonHandler_tick()` at 500 Hz handles:
- REC hold promotion at 300 ms (`DOUBLE_CLICK_TIMEOUT`).
- Bank COPY hold promotion at 200 ms (`BUTTON_TIMEOUT`).
- Overlay drain (`buttonHandler_overlayTick()`).
- Deferred scene change executor (`menu_serviceDeferredSceneChange()`).

---

## 6. LED management

### 6.1 Layer model

`led_activeLayers[41]` is a per-LED bitmap tracking active layers.  Priority
order (highest first):

1. **Pulse** — one-shot 50 ms indication (`led_pulseLed()`).
2. **Fast** — `LED_LAYER_FAST`, two slots: queued-Scene flash and background-load
   LED.  Double-rate (125 ms half-period).
3. **Flash** — continuous rapid toggle.
4. **Blink/Chase** — standard-rate blink and sequencer chase.
5. **Base** — static on/off.

On layer expiry, `led_renderFromStack()` clears the expired layer's bit and
renders the highest remaining active layer.

### 6.2 LED deadlines

**Binding rule (S081 P4):** pulse and flash deadlines are compared by signed
16-bit distance (`led_timeReached()`).  Never use `time_sysTick > end` — a
deadline started just before the 16-bit wrap would lose a pulse or flicker a
flash phase.

### 6.3 Sequencer chase

`led_processSeqLedState()` is the foreground drain of `SeqLedState seq_ledState`
(written by the Sequencer).  Chase LEDs: the `SEQ_LED_DIRTY_CHASE` event writes
the step LED position; when transport is stopped, it clears the chase layer
instead of installing it.

On `EFFECT_PAGE` (and during the SHIFT+TRACK voice-mix overlay), chase is
suppressed: the Effect page owns the SEQ row through
`menuEffects_renderSeqLeds()`.

### 6.4 Fast blink slots

Two slots since S082 P3: `LED_FAST_SLOT_QUEUED_SCENE` (0) and
`LED_FAST_SLOT_BANK_LOAD` (1).  Both share one phase toggled every 125 ms.
A slot that starts while the other runs joins its phase.

`led_clearAllBlinkLeds()`, `led_setMode2()` and `led_setValue()` cannot disturb
the fast layer.  `led_clearAll()` frees both slots (the owner re-asserts them
next tick).

### 6.5 PERF SEQ LEDs

**Binding rule (S080):** queued Scene = `LED_LAYER_FAST`.  Never restart running
blinks (`led_isBlinkLed()`).  Never start/stop from a per-beat path.

`menu_refreshPerfSceneLeds()` paints the PERF Scene row.  Presence comes from
`menu_residentPresentSceneMask()` (BankData availability since S082 P3).

### 6.6 VOICE-mode bar pulse

In VOICE mode, the SELECT LED of the bar the active track is playing pulses once
(50 ms) whenever that bar changes.  Suppressed when the effective length is ≤ 16
(single bar), `rnd` mode, the bar is the one being left, or on the first step
after transport start.

---

## 7. Endless pots (RV1-4)

### 7.1 Hardware

Four analog endless pots, each with two sine wave outputs 90° apart (one
electrical cycle per full revolution).  Connected to ADC1 DMA channels.

### 7.2 Driver

`endlessPots_tick()` runs at 1 kHz from TIM6 ISR.  Direction and magnitude are
derived via `atan2f(b, a)` on the centre-subtracted ADC values.

**Binding rule:** the argument order is `atan2f(b, a)`.  Do NOT change it.

Output is a signed delta (200 integer increments per full clockwise revolution).
The caller accumulates into whatever parameter range it needs.

### 7.3 Baselines and deadzone

The driver snapshots raw A/B baselines after page changes, after a long timeout
while moving (`ENDLESS_POT_TIMEOUT_MS` = 5000), and shortly after the last
emitted delta (`ENDLESS_POT_DELTA_TIMEOUT_MS` = 20).  Movement inside
`ENDLESS_POT_DEADZONE` (20) is ignored.

### 7.4 Double angular speed

`endlessPots_setDouble(i, enabled)`: double mode emits one delta after half the
normal angular travel.

**Binding rule:** double angular speed applies only to Morph amounts
(`menu_paramIsMorphAmount()`: `PAR_MORPH`, `PAR_VOICE1..6_MORPH`,
`PAR_EFFECT_MORPH` / PERF `fxm`) and to Effect-page 0..255 cells.  Do not apply
it to all `DTYPE_0B255` — BPM drift exposed that as too broad.

### 7.5 Knob repaint

**Binding rule:** `menu_knobs_dirty` + `menu_serviceKnobRepaint()` is RV1-4
only.  This is the coalesced repaint for endless-pot edits, running in the main
loop.

---

## 8. Main encoder (SW42)

Dannegger difference algorithm with TIM1 IC.  Acceleration and rebound
suppression.  Full operational rules in `HARDWARE_MAP.md` "Main Encoder"
section.

`menu_parseEncoder(inc, button)` handles navigation (page/sub-page/cell
traversal) and value editing.

---

## 9. SHIFT Morph views

### 9.1 VOICE Morph view

SHIFT held in VOICE mode: `menu_setVoiceModeShowMorph(1)`.  The VOICE page
shows and edits the Morph endpoint for descriptor-backed instrument cells and
the FX-send cell.  Driven by SHIFT+MODE VOICE latch and by SHIFT held in VOICE
mode.

**Binding rule (S078):** non-morphable params show/edit the Normal value, never
blanked or locked.

### 9.2 STEP Morph view

SHIFT held in STEP mode: `menu_setPatternTrackMorphEndpoint(1)`.  The view is
active only while the flag is set, `SEQ_PAGE` is shown, **and SHIFT is
physically held** (`menu_patternTrackMorphViewActive()`).  Then `len`/`scl`/`shf`
display and commit the Scene endpoints; `run`/`mch`/`not` keep their Normal
values (Morph-view rule).

**Binding rule (S078):** momentary (flag && SEQ_PAGE && shift).  No latch.
SHIFT+MODE STEP is only SOM entry.

### 9.3 Effect Morph view

SHIFT in FX mode: `menu_setEffectShowMorph(1)`.  Momentary; refreshes Effect pot
snapshots and repaint without touching VOICE morph state.

---

## 10. Automation-presence underline search

### 10.1 Architecture

`va_scanService()` runs 4 step reads per foreground pass, searching for automation
entries that match the currently visible parameters:

- **VOICE page:** searches the active track.
- **Effect page:** searches all 7 tracks of the viewed Pattern.

The result sets underline markers on cells that have automation on any step.

### 10.2 State

13 shared bytes: `va_searchSceneMask` bits, search cursor, result bitmap.

**Binding rule (S078):** `va_searchSceneMask` bits control which targets are
searched.  Every context or content change needs `va_searchRestart()`.

### 10.3 STEP track automation search

On `SEQ_PAGE`, `va_seqTrackSearchBit()` maps targets 405..425 to
`VA_SEARCH_SCENE_TRACK_BIT(0..2)` = 0x10/0x20/0x40 in `va_searchSceneMask`.
`sa_applyTrackMarkers()` underlines held values (overlay) or names (search).

### 10.4 Restarts

Required at: SEQ entry, track/Scene change, `menu_patternContentChanged()`,
step-automation editor target changes/deletes, held-step clear.

### 10.5 Live-record integration

`menu_automationTargetRecorded(scene, track, target)`: sets the
automation-presence bit a live-record write just made true without restarting the
search.

---

## 11. Runtime widgets and service

`menu_serviceRuntimeWidgets()` runs in the main loop.  It maintains:
- CPU-use widget (`PAR_RUNTIME_CPU_USE`).
- Pattern pool-use reading (`PAR_PAT_STORE_USE`).
- Effect page live refresh (when a type's `format_value3` hook is present).
- `menuEffects_service()` (even under the SHIFT+TRACK voice-mix overlay).

---

## 12. Runtime sound loading

### 12.1 Sound apply

`menu_startSoundApply()` / `preset_tickDrumsetApply()`: post-load apply funnels.
Both call `bank_revalidateVoiceEditMasks()` after the retained commit.

**Binding rule:** do not reintroduce direct `preset_sendDrumsetParameters()`
after audio.  The worker stays active until the all-source LFO/velocity rebind
cursor drains.

### 12.2 Instrument apply

`menu_startInstrumentApply()`: post-load apply for a single Instrument.  Also
calls `bank_revalidateVoiceEditMasks()`.

---

## 13. Active Scene

**Binding rule (S075):** Active Scene = viewed and written to, not playback.

The active Scene is the one whose data the menu displays and edits.  Under
per-track Scene playback (S077), a track may play a different Scene's Pattern.
The active Scene determines: which Scene's image the VOICE/Effect pages show,
which Scene edits are committed to, which Scene the edit mask fans out from.

---

## 14. Copy/clear interaction

Copy/clear owns: exclusive holder for the raw block API; top 132 B = swap block;
publish before free; `copyClear_backgroundSuspended()` blocks
AutoSave/trace/settings/repair.

`menu_copyClearMenuChanged()` / `menu_copyClearMenuClosed()` handle repaint and
CGRAM cache invalidation.  `menu_patternContentChanged()` restarts the
automation-presence search after any Pattern paste or clear.

Full specification: `COPYCLEAR_UTILITIES.md`.

---

## 15. Invariants

- `buttonHandler_processEvents()` is `if`, not `while`.
- `lcd_setcursor` before every data byte in `sendDisplayBuffer()`.
- LCD queue is head/tail-only SPSC; no `lcd_q_count`.
- Knob repaint is RV1-4 only.
- `endlessPots`: `atan2f(b, a)` argument order must not change.
- Encoder Dannegger algorithm: `encode_read1/read2` permanently removed.
- LED deadlines: signed 16-bit distance `led_timeReached()`.
- PERF SEQ LEDs: queued Scene = `LED_LAYER_FAST`; never restart running blinks;
  never start/stop from per-beat path.
- All 16 menu dtype ids/nibbles are used.  New cells: existing dtype +
  static-param special cases.
- `va_searchRestart()` on every context/content change.
- Active Scene = viewed and written to, not playback.
- Non-morphable params in SHIFT Morph-view: show/edit Normal, never blanked.
- STEP Morph view is momentary (flag && SEQ_PAGE && shift); SHIFT+MODE STEP is
  only SOM entry.

---

## 16. Cross-references

| Topic | Document |
|---|---|
| Module boundary contracts (full API tables) | `MODULE_INTERCHANGE_SPEC.md` |
| Effect page, R13, FX-sequence locks | `EFFECTS_BUS_REFERENCE.md` §6, §8 |
| PERF surface, live record, live erase | `PERFORMANCE_RECORD_SPEC.md` |
| Descriptor-backed instruments | `INSTRUMENTS_DSP_REFERENCE.md` |
| Pattern data, step automation, PAT4 | `PATTERN_DYNAMIC_STACK.md` |
| Automation priority R12 | `PATTERN_DYNAMIC_STACK.md` §6.2a |
| Copy/clear | `COPYCLEAR_UTILITIES.md` |
| Load/Save filesystem UI | `FILESYSTEM_SPEC.md` |
| Hardware pin table, encoder, IRQ | `HARDWARE_MAP.md` |
| Bus compressor settings page | `MIXER_SYSTEM_DSP_REFERENCE.md` §4 |
| Bank/Scene/Kit parameter storage | `BANK_PRESET_ARCHITECTURE.md` |
