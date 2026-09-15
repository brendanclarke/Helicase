# S065 — VOICE Page Held-Step Automation Overlay

## Scope

Implement the VOICE-page held-step automation overlay described in
`S065_DYN_PAT_STEP_AUTOMATION.md` §4. This is the second interaction method
for step automation: the user holds one or more SEQ buttons while on a VOICE
page and sees/edits automation through the existing parameter display.

### Prerequisites (landed in S065_DYN_PAT_STEP_AUTOM_EDITING.md)

The following are complete and available as building blocks:

- `pat_readStepAutomations()` — read decoded entries for one step
- `pat_writeStepAutomation()` — add or update one entry (uniqueness enforced)
- `pat_removeStepAutomation()` — remove one entry by target
- `pat_stepAutomationCount()` — count entries without reading them
- `pat_automation_entry_t` — public struct `{target, value}`
- `instrumentManager_stepTargetForSlot()` — iterate automatable targets
- `instrumentManager_targetValid()` — extended for Scene targets (384+)
- `instrumentParam_make()` / `instrumentParam_slot()` / `instrumentParam_local()`
- Sequencer pending buffer and foreground drain path

### Out of scope

- Live-record capture (`SCOPING_TARGETS.md` §4.3a)
- Hold/reset flag semantics
- Copy operations on automation blocks
- Defragmentation / slack management

---

## 1. Overlay Activation

### 1.1 Entry condition

The overlay activates when ALL of the following are true:

- `buttonHandler_getMode() == SELECT_MODE_VOICE`
- `menu_isVoicePage(menu_activePage)` is true
- At least one SEQ button (`BUT_SEQ1`..`BUT_SEQ16`) is physically held
  (`btn_held[]` array, read from foreground)

The overlay exits instantly when all SEQ buttons are released.

### 1.2 Held-step tracking

Track a 16-bit mask of currently held SEQ buttons (bit N = `BUT_SEQn` held).
The **focus step** is the most recently pressed SEQ button — track on
press-edge, not on release. The focus step determines which step's automation
values are displayed and which step the value display reads from.

Convert the 16-bit button mask to absolute step indices using
`buttonHandler_visibleStep()` (folds in `menu_currentBar`).

### 1.3 Interaction with existing VOICE SEQ behavior

In normal VOICE mode, SEQ press arms a long-press timer
(`buttonHandler_setTimeraction`), and release toggles the step active bit
(`buttonHandler_setRemoveStep`). The overlay replaces this: while the overlay
is active, SEQ presses/releases only update the held mask and focus step —
they do NOT toggle step bits or arm the timer. The existing timer action path
must be suppressed when the overlay flag is set.

**Decision needed:** The general plan says "they press and hold one or more
SEQ buttons" but does not say how to distinguish a normal tap (toggle step)
from an overlay hold. Two options:

**Option A — Long press activates overlay.** The existing BUTTON_TIMEOUT timer
fires, and instead of arming the old automation step, it enters overlay mode.
SEQ release after a long press does not toggle the step. Short taps continue
to toggle.

**Option B — Immediate overlay on any SEQ hold.** Any SEQ press immediately
enters overlay mode. Release toggles the step ONLY if no pot was turned during
the hold. This is the more fluid interaction but changes SEQ button behavior
on VOICE pages.

Recommendation: Option A. It preserves existing tap-to-toggle behavior and
the timer infrastructure is already in place. The overlay activates after
BUTTON_TIMEOUT (~300ms), matching the existing long-press gesture.

---

## 2. Display — Two-Tier Underline System

### 2.1 CGRAM slot allocation

The HD44780 LCD has 8 CGRAM slots (0..7). Slots 0 and 1 are reserved
(`lcd_char_ellipsis`, `lcd_char_pop`). Slots 2..7 (6 slots) are free at
runtime — the splash characters (`lcd_splash_char_1`..`6`) are boot-only.

Each CGRAM slot holds one 5×8 pixel glyph. To underline a character: copy its
standard ASCII glyph pattern and set the bottom pixel row to `0x1F`.

The HD44780 internal ROM provides glyphs for ASCII 0x20..0x7F, but the ROM
contents are not readable by firmware. The underline glyph data must come from
a lookup table compiled into firmware.

### 2.2 Font table requirement

A const array of 5×8 glyph bitmaps for the printable ASCII range used by
parameter names and values (at minimum `0-9`, `A-Z`, `a-z`, space). This is
~96 × 8 = 768 bytes of flash. Source: the HD44780A00 ROM pattern from the
datasheet (freely available, widely reproduced).

Alternatively, a smaller table covering only the ~40 distinct characters that
actually appear in parameter short names and numeric values. This reduces
flash cost but requires auditing every `ParamDescriptor::short_name` string.

### 2.3 Tier 1 — Held-step automation (immediate)

When the focus step has an automation entry whose target matches a displayed
parameter's `instrument_param_id_t`:

- **Multi-parameter overview:** ALL characters of that parameter's 3-char
  short name are underlined.
- **Single-parameter view:** ALL characters of the short name AND the value
  digits are underlined.

This is the "this step automates this parameter" indicator.

### 2.4 Tier 2 — Track-wide automation (async)

When ANY step on the current track has an automation entry targeting a
displayed parameter (regardless of whether the focus step does):

- The FIRST character only of that parameter's short name is underlined.

This is the "automation exists somewhere on this track" indicator. It is
computed asynchronously (see §5).

### 2.5 Priority and slot sharing

Tier 1 fills CGRAM slots first (active editing indicator). Remaining slots go
to Tier 2. Identical base characters across parameters share one CGRAM slot.
If the 6-slot budget is exhausted, lowest-priority Tier 2 indicators silently
don't render.

A parameter qualifying for both tiers displays Tier 1 (full underline), which
subsumes the first-char indicator.

### 2.6 CGRAM slot lifecycle

Slots 2..7 are written with underline glyphs when the overlay activates or
the display updates. On overlay exit, they can remain defined — they are only
referenced when the overlay emits `\x02`..`\x07` codes in LCD strings, which
stops on exit. Normal VOICE repaint uses standard ASCII codes and ignores
CGRAM 2..7 content.

---

## 3. Value Display

### 3.1 Focus step automation value

If the focus step has an automation entry for the currently viewed parameter
(single-parameter view):

- Display the automated value (after 7-bit → 8-bit expansion:
  `(v == 127) ? 255 : v * 2`), not the Scene image default.
- Value characters are underlined (Tier 1).

### 3.2 No automation on focus step

- Display the Scene image default value from
  `scene_instrumentSlotConst(scene, slot)->parameter_images.instrument_parameters[descriptor_index]`.
- Value is NOT underlined.

### 3.3 Multi-parameter overview values

In overview mode (4 parameters visible), values remain the Scene image
defaults. Underline on the parameter NAME characters indicates automation
presence, but the compact value display does not change to show automated
values. The user enters single-parameter view (encoder click) to see and
edit the automated value.

---

## 4. Step Illumination

### 4.1 Single-parameter view with overlay active

When the overlay is active AND the user is in single-parameter view:

- SEQ LEDs show ONLY the steps (in the current bar) that carry an automation
  entry for the currently viewed parameter, targeting the track's own voice.
- Steps without automation for that parameter are unlit.
- The focus step LED remains lit regardless.

### 4.2 Restore conditions

Normal trigger-based LED illumination is restored when:

- All SEQ buttons are released (overlay exits)
- The user exits single-parameter view (encoder click-out to overview)
- Transitions are immediate.

### 4.3 Dynamic updates

While in automation illumination mode, the step LEDs must update immediately
on:

- **Bar change:** re-read the new bar's 16 steps for automation presence.
- **Track change:** different voice's descriptors; re-evaluate.
- **SELECT page change:** different parameter; re-evaluate.

Each update reads pool blocks for 16 steps — foreground, bounded.

### 4.4 Implementation

Use `pat_readStepAutomations()` or `pat_stepAutomationCount()` +
`pat_readStepAutomations()` for each of the 16 visible steps. Search each
step's automation list for the target matching the current single-parameter
view's `instrument_param_id_t`. Set `led_setValue()` accordingly.

---

## 5. Async Track-Wide Automation Search

### 5.1 Purpose

Tier 2 underline requires knowing which parameters have automation on ANY
of the 128 steps of the current track. Reading all 128 steps synchronously
on every display update would stall the foreground loop.

### 5.2 Polled search agent

A small state machine in `menu.c` (or a dedicated module) runs during the
foreground service loop. It processes a few steps per pass (e.g. 4–8 steps
per `menu_repaint()` cycle). State:

- `search_track` — the track being scanned
- `search_step_cursor` — next step to examine (0..127)
- `search_target_mask[]` — bit array of parameter targets found so far
- `search_complete` — set when cursor reaches 128

### 5.3 Trigger conditions

The search restarts (cursor resets to 0, results cleared) when:

- The overlay activates
- The parameter page changes (different parameters visible)
- The track changes
- An automation write or delete occurs on the current track

### 5.4 Result application

When `search_complete` is set, the Tier 2 underline data is valid. The
repaint path reads the result mask and underlines first characters of
parameters that have track-wide automation presence. Until the search
completes, Tier 2 underlines are absent — Tier 1 (focus step) is immediate.

### 5.5 Visible parameters

In multi-parameter overview, up to 4 parameters are visible per SELECT page.
In single-parameter view, 1 parameter is visible. The search must cover all
automatable parameters on the track's voice, not just the currently visible
ones — the result must survive SELECT page changes without restarting.

The result format: a bitmask indexed by descriptor index (up to 64 bits =
8 bytes per voice). Each bit indicates "at least one step on this track has
an automation entry targeting this `instrumentParam_make(voice_slot, descriptor_index)`."

---

## 6. Writing Automation via Pot Adjustment

### 6.1 Write trigger

While the overlay is active, any pot rotation that changes a displayed
parameter's value writes an automation entry to EVERY held step.

The target is the track's own voice:
`instrumentParam_make(menu_voicePageToSlot(menu_activePage), descriptor_index)`.

### 6.2 Multi-step write loop

For each held step (from the 16-bit held mask):

1. Call `pat_writeStepAutomation(scene, track, abs_step, target, value_7bit)`.
2. If it returns 0 (pool full or 63-entry ceiling), skip that step silently.

Best-effort semantics: successfully written steps keep their automation.
No notification for failures — the user sees partial underline.

### 6.3 Value conversion

Pots report 8-bit values. Convert to 7-bit for storage:
`(value >= 255) ? 127 : value / 2`.

The display shows the 8-bit expanded value (the round-trip is lossy by ±1
LSB, which matches the existing MIDI CC path).

### 6.4 Pot input safety

The VOICE page uses endless pots with atan2 delta tracking (`endlessPots.c`).
Pressing SEQ buttons does not generate a pot delta. Only physical pot rotation
creates a delta. There is no dead-zone/pickup problem — the overlay cannot
accidentally write automation on button press alone.

### 6.5 Normal vs. overlay pot behavior

When the overlay is NOT active (no SEQ held), pot turns apply to the Scene
image as normal. When the overlay IS active, pot turns write step automation
instead. The Scene image value is not changed by overlay pot writes — the
automation overrides it at playback time only.

**Open question:** should the pot also update the Scene image (so the live
sound changes immediately), or should the automation value only take effect
at playback? The general plan does not specify. Recommendation: update the
Scene image as well, for immediate audibility. This matches the mental model
of "I'm adjusting this parameter on these steps."

---

## 7. Integration Points

### 7.1 buttonHandler.c

- `buttonHandler_setTimeraction()`: when timer fires and mode is VOICE, enter
  overlay instead of the old automation arm. Or: add a new overlay entry path
  alongside the existing timer action.
- `buttonHandler_seqButtonPressed()` / `buttonHandler_seqButtonReleased()`:
  route to overlay held-mask update when overlay is active or activating.
- Suppress step toggle on release when overlay was active.
- New export: `buttonHandler_seqHeldMask()` returning the 16-bit mask of
  currently held SEQ buttons (derived from `btn_held[]`).

### 7.2 menu.c

- Overlay state variables: `menu_voiceAutoOverlayActive`, focus step index,
  held mask, Tier 2 search state.
- `menu_repaintGeneric()`: when overlay active and on a voice page, call the
  overlay renderer instead of the normal voice renderer.
- `menu_parseKnobDelta()` / `menu_encoderChangeParameter()`: when overlay
  active, redirect pot changes to step automation writes instead of Scene
  image writes.
- `menu_switchPage()` / `menu_setActiveVoice()`: reset/restart the Tier 2
  search when track or page changes.
- New function: `menu_repaintVoiceAutomationOverlay()` — renders the display
  with CGRAM underline characters.

### 7.3 lcd.c / lcd.h

- `lcd_define_char()` is already public. No new LCD API needed.
- A compiled font table for underline glyph generation needs to be added
  (either in lcd.c or a new `lcd_font.c`).

### 7.4 ledHandler.c

- New function or mode: `led_updateAutomationStepView()` — sets step LEDs
  based on automation presence instead of trigger state. Called from the
  overlay repaint path.
- Must restore normal step LEDs on overlay exit via existing
  `led_updatePatternTrackView()`.

---

## 8. Risk Cases

### 8.1 CGRAM budget overflow

Worst case in multi-parameter overview: 8 distinct characters need
underlining, but only 6 CGRAM slots are free. Priority allocation (Tier 1
first, then Tier 2) with silent degradation handles this. In practice, glyph
sharing (e.g. two parameters both containing 'a') keeps most layouts within
budget.

### 8.2 Pool reads during overlay

Reading automation entries for 16 held steps on every repaint cycle is
bounded (16 × `pat_readStepAutomations` calls, each reading a few entries).
The Tier 2 async search adds a few more reads per cycle. Total pool read
volume is well within foreground timing.

### 8.3 Overlay and sequencer race

Pot-driven automation writes modify pool blocks from the foreground. The
sequencer's TIM3 ISR reads pool blocks. The existing pool design has no
locking — the ISR reads are bounded and the foreground writes are
atomic-per-chunk (4-byte aligned). The allocate-first pattern in
`pat_writeDynamic()` ensures the ISR never sees a half-written block.
No additional protection needed.

### 8.4 Stale display after automation write

After writing automation to held steps, the overlay display must update to
show the new underline state and value. The write loop should trigger a
repaint at the end, not after each individual step write.

### 8.5 Focus step changes mid-hold

If the user presses additional SEQ buttons while already holding some, the
focus step shifts to the newest press. The display updates to show the new
focus step's automation state. Previously held steps remain in the held mask
for writes but their values are not displayed.

---

## 9. Implementation Order

1. **Font table**: compile HD44780A00 ROM glyphs into a const array in flash.
   Add a helper `lcd_underlineGlyph(uint8_t ascii_char, uint8_t out[8])` that
   copies the base glyph and sets row 7 to `0x1F`.

2. **Overlay state and activation**: add held-mask tracking and overlay
   flag to `buttonHandler.c` / `menu.c`. Wire the long-press timer to enter
   overlay mode on VOICE pages.

3. **Tier 1 underline rendering**: implement `menu_repaintVoiceAutomationOverlay()`
   with CGRAM slot allocation for the focus step's automation matches. Single-
   parameter view first, then multi-parameter overview.

4. **Value display**: show automated value (expanded 7→8 bit) when focus step
   has automation for the viewed parameter. Underline value characters.

5. **Step illumination**: implement automation-presence LED mode for single-
   parameter view. Add restore path on overlay exit.

6. **Pot-to-automation write**: redirect pot delta to
   `pat_writeStepAutomation()` for all held steps when overlay is active.

7. **Tier 2 async search**: implement the polled search agent. Wire result
   into the underline renderer with first-char-only CGRAM allocation.

8. **Integration and edge cases**: bar/track/page change handlers, focus
   step shift on additional press, overlay exit cleanup.

9. **Hardware test**: verify underline rendering on physical LCD, test
   pot-to-step write, verify step illumination, test with playback running.

---

## 10. RAM and Flash Budget

| Resource | Estimate | Notes |
|----------|----------|-------|
| Font table | ~768 B flash | 96 × 8 bytes, ASCII 0x20..0x7F |
| Overlay state | ~12 B SRAM | held mask (2), focus step (1), active flag (1), Tier 2 search cursor + state (~8) |
| Tier 2 result mask | 8 B SRAM | 64-bit descriptor presence bitmask per voice |
| CGRAM work buffer | 48 B stack | 6 × 8 bytes for building underline glyphs |
| Step automation read | 252 B stack | `pat_automation_entry_t[63]` for one step read |
| Total SRAM (static) | ~22 B | Negligible |

---

## 11. Open Questions

1. **Overlay entry gesture**: long-press (Option A) vs. immediate hold
   (Option B). Recommendation is Option A — needs confirmation.

2. **Pot writes to Scene image**: should overlay pot turns also update the
   Scene image for immediate audibility, or only write step automation?
   Recommendation: yes, update both.

3. **Font table scope**: full ASCII 0x20..0x7F (768 B) or audited subset
   (~40 chars, ~320 B)?

4. **Tier 2 search rate**: how many steps per foreground pass? 4 steps per
   pass = ~32 passes to complete = ~160ms at 200Hz service rate. 8 steps
   per pass halves that.
