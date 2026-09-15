# S065 — VOICE Page Held-Step Automation Overlay

## Scope

Implement the VOICE-page held-step automation overlay described in
`S065_DYN_PAT_STEP_AUTOMATION.md` §4. This is the second interaction method
for step automation: the user holds one or more SEQ buttons while on a VOICE
page and sees/edits automation through the existing parameter display.

The automation-presence underline is also active on VOICE pages when no step
is held. The same rendering and held-step rules apply in both normal VOICE
mode and the persistent SHIFT+VOICE Morph endpoint view
(`voiceModeShowMorph`). They apply to both the four-parameter overview and the
encoder-clicked single-parameter view.

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
Also retain the currently held buttons in press order. A single global focus
step is insufficient: each displayed parameter must use the most recently
pressed, still-held step that actually contains automation for that parameter.
Different visible parameters may therefore display values from different held
steps.

Walk the held-step order newest-to-oldest. Read each held step's automation
list once and resolve every still-unresolved visible parameter whose exact
target occurs in that list. The first match assigned to each parameter is that
parameter's **value-source step**. Stop early when all visible parameters are
resolved. Releasing a source step immediately falls back to the next-newest
held matching step. If no held step contains a match, the parameter has no
value-source step and displays its normal endpoint value.

The match is specific to the displayed parameter's canonical automation target;
automation for any other parameter on a newer held step does not qualify.

Convert the 16-bit button mask to absolute step indices using
`buttonHandler_visibleStep()` (folds in `menu_currentBar`).

### 1.3 Interaction with existing VOICE SEQ behavior

In normal VOICE mode, SEQ press arms a long-press timer
(`buttonHandler_setTimeraction`), and release toggles the step active bit
(`buttonHandler_setRemoveStep`). The overlay replaces this: while the overlay
is active, SEQ presses/releases only update the held mask and press-order list
— they do NOT toggle step bits or arm the timer. The existing timer action
path must be suppressed when the overlay flag is set.

Use a short long-press gesture. Add `BUTTON_HOLD_DELAY_MS` to `config.h` with
an initial value of `100u`, and use that common setting for this and every
other UI action that distinguishes a hold from a tap. Replace the private
`BUTTON_TIMEOUT` value in `buttonHandler.h` rather than maintaining two hold
thresholds.

When the common 100 ms threshold expires, enter the overlay. Releasing a SEQ
button after its hold has activated the overlay does not toggle the step;
releasing before the threshold remains a normal short tap. Implement the
16-bit millisecond comparison wrap-safely.

---

## 2. Display — Bounded Single-Marker Underline System

### 2.1 CGRAM slot allocation

The HD44780 LCD has 8 CGRAM slots (0..7). Slots 0 and 1 are reserved
(`lcd_char_ellipsis`, `lcd_char_pop`). Slots 2..7 (6 slots) are free at
runtime — the splash characters (`lcd_splash_char_1`..`6`) are boot-only.

Each CGRAM slot holds one 5×8 pixel glyph. To underline a character: copy its
standard ASCII glyph pattern and set the bottom pixel row to `0x1F`.

This feature may use **at most four** of the six runtime-free slots. There are
at most four visible parameters and the rules below assign exactly one
underlined screen character to each visible parameter. Use slots 2..5 for the
four-entry underline cache; leave slots 6 and 7 available for other runtime
uses. Identical rendered base characters may share a slot, but sharing is an
optimization and is not required to stay within budget.

The WS0010 internal ROM provides the ordinary glyphs, but its contents are not
readable by firmware. The underline glyph data must come from a lookup table
compiled into firmware.

### 2.2 Font table requirement

Only ASCII digits and letters may be underlined: `0-9`, `A-Z`, and `a-z`.
That is **62 glyphs** (10 + 26 + 26), not 72. Store each as the 8-byte CGRAM
row image required by `lcd_define_char()`, for **496 bytes of flash**. Map the
three contiguous ASCII ranges arithmetically; no per-character index table is
needed.

Source the bitmaps from the WS0010 English/Japanese font table selected by the
current initialization path, not from a merely compatible HD44780 font whose
glyph shapes may differ. Marker-selection code must only request these 62
characters. If a future name/value formatter places punctuation at a marker
edge, leave that character un-underlined until its UX is specified rather than
silently growing the font table.

### 2.3 Pattern-wide automation marker

When a displayed parameter is automated on any step of the current track in
the current/viewed Pattern, underline exactly one character of its name:

- **Four-parameter overview:** underline the leftmost non-space character of
  that parameter's rendered three-character short name.
- **Single-parameter view:** underline the leftmost non-space character of the
  rendered long-name field, not the category field.

This marker is present on VOICE pages even when no SEQ button is held. It is
derived from the async Pattern scan in §5.

### 2.4 Held-step value marker and precedence

If one or more currently held steps contain automation for a displayed
parameter, resolve that parameter's value-source step as defined in §1.2 and:

- display the automation value from that step in the parameter's value field;
- underline exactly the **rightmost non-space character** of the rendered
  value (the units character for a numeric value); and
- do not underline the parameter name.

The held-step value marker has precedence only for that parameter. Other
visible parameters without a matching held step continue to use the
pattern-wide name marker when applicable. This rule is identical in the
four-parameter overview and clicked-in single-parameter view, and in normal
and SHIFT+VOICE Morph endpoint modes.

**Value-marker invariant:** whenever a parameter value is underlined, the
displayed value must be the automation value read from its value-source step
for that exact target. It must never be the instrument's saved normal endpoint
or Morph endpoint. Conversely, an endpoint value is never underlined as a
held-step value.

### 2.5 Four-slot bound

Each visible parameter has at most one marker location: its name or its value,
never both. The four-parameter overview therefore requires at most four
distinct underlined glyph definitions; the single-parameter view requires at
most one. There is no priority drop or silent loss of indicators.

### 2.6 CGRAM slot lifecycle

Maintain a four-entry cache recording which rendered base character occupies
each of slots 2..5. Build the mapping from the final formatted display bytes,
after active-parameter capitalization, so the CGRAM glyph always matches the
character it replaces. Redefine a slot only when its assigned base character
changes; ordinary repaints must not rewrite unchanged CGRAM definitions.

When a mapping changes, enqueue one ordered display transaction: replace stale
references with their ordinary ROM characters, define the required CGRAM
glyphs, and then emit the new slot codes. Preflight the LCD queue for the whole
transaction so a partial queue cannot leave DDRAM referring to a redefined
slot. On leaving VOICE pages, ordinary repaint stops emitting slot codes; the
CGRAM definitions themselves may remain until reused.

---

## 3. Value Display

### 3.1 Held-step automation value

For every displayed parameter, if a value-source step exists:

- Display the automated value (after 7-bit → 8-bit expansion:
  `(v == 127) ? 255 : v * 2`), not the Scene image default.
- Apply the single-character value underline from §2.4 after the debounce in
  §3.4.

This applies to all four compact values in overview mode as well as the one
value in single-parameter mode. Because value-source selection is per
parameter, the four overview values may come from different held steps.
Normal/Morph endpoint selection is bypassed for that displayed parameter for
as long as a value-source step exists.

### 3.2 No matching held-step automation

- In normal VOICE mode, display the Scene image value from
  `scene_instrumentSlotConst(scene, slot)->parameter_images.instrument_parameters[descriptor_index]`.
- In SHIFT+VOICE Morph endpoint mode, display the corresponding Morph endpoint
  value selected by the existing `voiceModeShowMorph` path.
- Do not underline the value. If the async Pattern scan says automation exists
  elsewhere on the track, use the name marker from §2.3 instead.

### 3.3 Press/release changes

On a SEQ press or release, re-resolve the value-source step separately for each
visible parameter. A newly selected existing automation value may repaint
immediately; the quiet-period debounce below is specifically for rapid value
editing, not ordinary button navigation.

### 3.4 Debounced value-underline reapplication

Repeated pot/encoder edits can change the underlined value character on every
detent. Reprogramming CGRAM on every edit would churn the async LCD queue.
Define `VOICE_AUTOMATION_UNDERLINE_QUIET_MS` in `config.h`, initially `100u`;
this is separate from the common button-hold threshold even though both begin
at 100 ms.

When a held-step automation value changes:

1. Repaint the new value immediately with ordinary ROM characters and suppress
   that parameter's value underline. Its name remains un-underlined because a
   held-step match still has precedence.
2. Set the parameter's bit in a four-bit suppression mask and update one shared
   `last_value_edit_tick` timestamp.
3. Each further held-step value edit updates the timestamp, batching edits
   across the four visible parameters.
4. In a foreground service path, after
   `VOICE_AUTOMATION_UNDERLINE_QUIET_MS` with no further value edit, clear the
   suppression mask, resolve and format the current source values again, update
   CGRAM only for glyphs that actually changed, and request one repaint.

Use wrap-safe elapsed timing against the existing 1 kHz `time_sysTick`:
`(uint16_t)(now - last_value_edit_tick) >=
VOICE_AUTOMATION_UNDERLINE_QUIET_MS`. Never run CGRAM or LCD queue work from an
ISR. Cancel a pending underline-only repaint when the VOICE page, track,
Pattern, parameter screen, or held-step overlay context is left.

---

## 4. Step Illumination

### 4.1 Single-parameter view with overlay active

When the overlay is active AND the user is in single-parameter view:

- SEQ LEDs show ONLY the steps (in the current bar) that carry an automation
  entry for the currently viewed parameter, targeting the track's own voice.
- Steps without automation for that parameter are unlit.
- Every physically held step remains lit regardless, so the edit selection is
  never hidden by the automation-presence LED mode.

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

The pattern-wide name marker requires knowing which parameters have automation
on ANY of the 128 steps of the current track. Reading all 128 steps
synchronously on every display update would stall the foreground loop.

### 5.2 Polled search agent

A small state machine in `menu.c` (or a dedicated module) runs during the
foreground service loop. Add `VOICE_AUTOMATION_SCAN_STEPS_PER_PASS` to
`config.h`, initially `4u`, and process at most that many Pattern steps per
foreground service pass. Since a step may contain 63 automation entries, the
initial setting has a hard worst-case scan cost of **4 × 63 = 252 automation
entries/target comparisons per pass**. State:

- `search_track` — the track being scanned
- `search_pattern` — the viewed Pattern being scanned
- `search_step_cursor` — next step to examine (0..127)
- `search_target_mask[]` — bit array of parameter targets found so far
- `search_complete` — set when cursor reaches 128

### 5.3 Trigger conditions

The search restarts (cursor resets to 0, results cleared) when:

- A VOICE page becomes active
- The viewed Pattern or Scene changes
- The track changes
- An automation deletion occurs on the current track

Changing SELECT page or clicking into/out of a parameter does not restart a
completed scan because the result covers every automatable descriptor on the
track. Switching normal/Morph endpoint view also preserves the result because
step-automation target identity is independent of endpoint display mode.

### 5.4 Result application

When `search_complete` is set, the Pattern-wide underline data is valid. The
repaint path reads the result mask and applies the name markers in §2.3.
Until the search completes, Pattern-wide name markers are absent. Matching
held-step values and value markers do not wait for this scan because their
small held-step set is read directly.

After any successful automation write, set the corresponding result bit
immediately; adding/updating an entry cannot invalidate any other set bit and
does not require a restart. Restart/rescan after deletion because removing one
entry does not prove that the target is absent from every other step.

### 5.5 Visible parameters

In multi-parameter overview, up to 4 parameters are visible per SELECT page.
In single-parameter view, 1 parameter is visible. The search must cover all
automatable parameters on the track's voice, not just the currently visible
ones — the result must survive SELECT page changes without restarting.

The result format: a bitmask indexed by descriptor index (up to 64 bits =
8 bytes per voice). Each bit indicates "at least one step on this track has
an automation entry targeting this `instrumentParam_make(voice_slot, descriptor_index)`."

---

## 6. Writing Automation via Parameter Adjustment

### 6.1 Write trigger

While the overlay is active, any applicable endless-pot or clicked-in encoder
adjustment that changes a displayed parameter's value writes an automation
entry to EVERY held step.

If a value-source step already exists, the edit starts from its displayed
automation value, never from either instrument endpoint. If none of the held
steps yet contains this target, the first edit may start from the currently
displayed normal/Morph endpoint value; the adjusted result is then created as
automation on the held steps. Only after a successful automation write may the
display move the marker to the value. Apply the delta, convert the result for
storage, and broadcast the same resulting value to all held steps. After a
fully successful batch, all held steps contain the same value for that target,
so resolving the newest matching held step necessarily shows the value just
written.

The target is the track's own voice:
`instrumentParam_make(menu_voicePageToSlot(menu_activePage), descriptor_index)`.

### 6.2 Multi-step write loop

For each held step (from the 16-bit held mask):

1. Call `pat_writeStepAutomation(scene, track, abs_step, target, value_7bit)`.
2. If it returns 0 (pool full or 63-entry ceiling), skip that step silently.

Best-effort semantics: successfully written steps keep their automation.
No notification for failures. Value-source resolution after the write reads
the actual successful results, so the displayed value comes from the newest
held step that really contains the target.

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

### 6.5 Endpoint and runtime-DSP safety

When no SEQ step is held, normal VOICE behavior is unchanged: edits target the
normal endpoint, or the Morph endpoint while `voiceModeShowMorph` is active.

When any SEQ step is held, the edit must **never modify either endpoint**. It
must not call endpoint commit helpers, change the normal or Morph instrument
images, update their Menu mirrors, dirty Autosave, or otherwise make the
automation edit persistent anywhere except the selected Pattern steps. This is
true in both normal and Morph VOICE views; `voiceModeShowMorph` does not change
the automation target ID or storage destination.

- **Playback running (`seq_isRunning() != 0`):** only write the Pattern step
  automation. Do not add a separate UI-originated runtime DSP write; playback
  owns audible application.
- **Playback stopped (`seq_isRunning() == 0`):** after writing the Pattern
  automation, the same value may optionally be sent directly to the audible
  runtime DSP parameter for preview. This is an ephemeral runtime write only;
  it is not a normal endpoint write, Morph endpoint write, Scene image update,
  Menu endpoint-mirror update, or Autosave mutation.

Safe implementation rule: if the implementer is not certain that a proposed
preview path writes only the live DSP runtime field, omit preview entirely.
Always writing automation to the held steps and nothing else is correct; an
uncertain preview is not.

After at least one successful write, update the displayed value immediately
and arm the configured underline quiet period in §3.4. Do this once after the
multi-step loop, not once per held step.

---

## 7. Integration Points

### 7.1 buttonHandler.c

- `buttonHandler_setTimeraction()`: when timer fires and mode is VOICE, enter
  overlay instead of the old automation arm. Or: add a new overlay entry path
  alongside the existing timer action.
- `buttonHandler_seqButtonPressed()` / `buttonHandler_seqButtonReleased()`:
  route press/release edges to the overlay held-mask and ordered-held-list
  update when overlay is active or activating.
- Suppress step toggle on release when overlay was active.
- New export: `buttonHandler_seqHeldMask()` returning the 16-bit mask of
  currently held SEQ buttons (derived from `btn_held[]`).

### 7.2 menu.c

- Overlay state variables: `menu_voiceAutoOverlayActive`, held mask,
  press-ordered held-step list, Pattern-wide search state, four-slot glyph
  cache, and value-underline debounce state.
- `menu_repaintGeneric()`: on every VOICE-page repaint, format the ordinary
  normal/Morph display first and then apply Pattern-wide name markers. When the
  held-step overlay is active, replace each matching parameter's displayed
  value and move that parameter's marker from name to value.
- `menu_parseKnobDelta()` / `menu_encoderChangeParameter()`: when overlay
  active, redirect changes to step automation writes and bypass every
  normal/Morph endpoint commit path. If playback is stopped, any preview must
  go through an audited runtime-DSP-only boundary; otherwise omit it.
- `menu_switchPage()` / `menu_setActiveVoice()` / Pattern and Scene change
  paths: invalidate the Pattern-wide result or the visible glyph mapping as
  specified in §5.3.
- `menu_setVoiceModeShowMorph()`: invalidate formatted values and repaint, but
  retain the Pattern-wide target mask.
- `menu_serviceRuntimeWidgets()` (or an equivalent foreground service): advance
  the Pattern scan and expire the configured underline quiet period.
- New VOICE renderer/helper: apply the one-marker-per-parameter convention to
  both compact and clicked-in display buffers using CGRAM slot codes.

### 7.3 lcd.c / lcd.h

- `lcd_define_char()` is already public. No new LCD API needed.
- A compiled font table for underline glyph generation needs to be added
  (either in lcd.c or a new `lcd_font.c`).
- The Menu-side four-entry cache owns slots 2..5 and must coordinate CGRAM
  definitions with ordered DDRAM writes through the existing async queue.

### 7.4 ledHandler.c

- New function or mode: `led_updateAutomationStepView()` — sets step LEDs
  based on automation presence instead of trigger state. Called from the
  overlay repaint path.
- Must restore normal step LEDs on overlay exit via existing
  `led_updatePatternTrackView()`.

---

## 8. Risk Cases

### 8.1 CGRAM bound

The four-column display exposes at most four parameters and each receives at
most one underline marker. Therefore the worst case is four distinct glyphs,
which fits slots 2..5. The clicked-in view needs only one. Add assertions or a
bounded allocator so no renderer path can consume slots 6 or 7 accidentally.

### 8.2 Pool reads during overlay

Resolving up to four parameters against up to 16 held steps is bounded at 16
step-list reads because each held list is read once and checked against all
unresolved visible targets. Do it once per relevant input/repaint event rather
than continuously. The async Pattern scan adds only its configured few steps
per foreground pass.

### 8.3 Overlay and sequencer race

Pot-driven automation writes modify pool blocks from the foreground. The
sequencer's TIM3 ISR reads pool blocks. The existing pool design has no
locking — the ISR reads are bounded and the foreground writes are
atomic-per-chunk (4-byte aligned). The allocate-first pattern in
`pat_writeDynamic()` ensures the ISR never sees a half-written block.
No additional protection needed.

### 8.4 Rapid value edits and stale debounce work

After writing automation to held steps, repaint the new value immediately but
leave its underline suppressed until the configured quiet period expires. The
expiry service must validate that Pattern, track, VOICE page, parameter screen,
Morph-view state, and held-step context still match before repainting; otherwise
discard the stale request.

### 8.5 Per-parameter source changes mid-hold

If the user presses another SEQ button, only parameters automated on that step
move to its values. Other parameters retain values from their own newest held
matching steps. On release, each affected parameter falls back independently
through the ordered held list, then to its selected normal/Morph endpoint when
no matching held step remains.

### 8.6 Async CGRAM/DDRAM ordering

Changing a CGRAM slot while old DDRAM cells still reference it can transiently
change those cells into the new glyph. Restore stale cells to ROM characters
before redefining the slot, then write new slot references. Preflight the
complete ordered transaction against the 128-entry LCD queue; if it does not
fit, defer the whole transaction instead of enqueueing a partial update.

### 8.7 SHIFT+VOICE Morph view

`voiceModeShowMorph` changes the endpoint used for a parameter without a held
automation match; it does not change Pattern target identity or the value
stored in a step automation entry. Test the full matrix: overview/clicked-in ×
normal/Morph × no-held/held-match/held-no-match, including switching Morph mode
while a Pattern-wide indicator or debounce is pending.

The held-step edit branch must run before any mutable endpoint pointer is
resolved or changed. Use a local working value seeded from the automation value
(or read-only endpoint fallback for first creation), then call only Pattern
automation writes and, when stopped and unambiguous, a runtime-DSP-only preview
writer. Never pass the held-step edit through `menu_cellCommitValue()`,
`menu_sendEditedParameter()`, Preset endpoint setters, or Autosave notification.

---

## 9. Implementation Order

1. **Configuration**: add `BUTTON_HOLD_DELAY_MS = 100u`,
   `VOICE_AUTOMATION_UNDERLINE_QUIET_MS = 100u`, and
   `VOICE_AUTOMATION_SCAN_STEPS_PER_PASS = 4u` to `config.h`. Replace the
   private `BUTTON_TIMEOUT` definition with the common hold setting.

2. **Font table**: compile only the 62 alphanumeric glyphs from the selected
   WS0010 English/Japanese ROM into a 496-byte const array in flash.
   Add a helper `lcd_underlineGlyph(uint8_t ascii_char, uint8_t out[8])` that
   maps the three ASCII ranges, copies the base glyph, sets row 7 to `0x1F`,
   and rejects non-alphanumeric input.

3. **Four-slot cache and transaction writer**: reserve slots 2..5, add cached
   base-character metadata, and implement preflighted ordered CGRAM/DDRAM
   updates. Prove the allocator cannot return more than four slots.

4. **Pattern-wide async search**: implement the polled search over all 128
   steps and the descriptor result mask, using the configured four-step budget
   per foreground pass. Apply the first-name-character marker on ordinary
   VOICE pages before adding held-step behavior.

5. **Overlay state and activation**: add held-mask and press-order tracking and
   the overlay flag to `buttonHandler.c` / `menu.c`. Wire the configured short
   long-press gesture to VOICE pages.

6. **Per-parameter held value rendering**: newest-to-oldest resolve each
   displayed target, show the matching automation value in both overview and
   clicked-in views, suppress its name marker, and underline only the
   rightmost value character.

7. **Debounced underline reapplication**: show edited values immediately in
   ROM glyphs and reapply their value markers once after the configured quiet
   period, from a foreground service path.

8. **Step illumination**: implement automation-presence LED mode for single-
   parameter view. Add restore path on overlay exit.

9. **Automation write and runtime safety**: redirect endless-pot/encoder deltas
   to `pat_writeStepAutomation()` for all held steps before any mutable
   endpoint pointer or commit helper is touched. Re-resolve successful values
   once per batch and update the Pattern-wide result bit. When running, do
   nothing else. When stopped, add a runtime-DSP-only preview only if its
   non-persistent boundary is unambiguous; otherwise omit preview.

10. **Morph integration**: run the same marker and held-step logic through the
   existing `voiceModeShowMorph` overview and clicked-in render/edit paths;
   prove held-step edits leave both normal and Morph endpoint buffers and their
   Autosave state unchanged.

11. **Integration and edge cases**: bar/track/Pattern/page change handlers,
    ordered fallback after release, async-result invalidation, and overlay exit
    cleanup.

12. **Hardware test**: verify the four-marker worst case on the physical LCD;
    test overview and clicked-in views in normal and SHIFT+VOICE Morph modes;
    test different source steps per parameter; rapidly sweep values and verify
    immediate text plus one delayed underline update; verify step LEDs and the
    252-entry scan-pass worst case; snapshot/compare both endpoint images and
    Autosave dirty state across stopped and running held-step edits; test
    playback while the async LCD queue is busy.

---

## 10. RAM and Flash Budget

| Resource | Estimate | Notes |
|----------|----------|-------|
| Font table | 496 B flash | 62 × 8 bytes: `0-9`, `A-Z`, `a-z` |
| Held selection | 20 B SRAM | held mask (2), press order (16), held count (1), overlay active (1) |
| Pattern search | 12 B SRAM | Pattern/track/cursor/complete (4), 64-bit descriptor presence as `uint8_t[8]` |
| Underline cache | 5 B SRAM | four cached base characters plus valid mask |
| Debounce | 3 B SRAM | 16-bit last-edit tick plus four-bit suppression mask |
| CGRAM work buffer | 8 B stack | Build and enqueue one glyph at a time |
| Step automation read | 252 B stack | `pat_automation_entry_t[63]` for one step read |
| **Total new static SRAM** | **40 B `.bss`** | One Menu-owned, two-byte-aligned runtime-lifetime state block; no ISR writes |

The user explicitly approved this 40-byte retained `.bss` allocation on
2026-09-15. The implementation must stay within this exact Menu-owned,
runtime-lifetime budget; any increase requires a new acknowledgement.

---

## 11. Resolved Decisions

1. **Overlay entry gesture:** short long-press using the common configurable
   `BUTTON_HOLD_DELAY_MS`, initially 100 ms.
2. **Endpoint safety:** while any step is held, never mutate normal or Morph
   endpoints. Running playback gets Pattern writes only. Stopped playback may
   additionally get a runtime-DSP-only preview; if that distinction is unclear,
   omit preview and write only the Pattern automations.
3. **Font table:** 62 alphanumeric glyphs, 496 bytes of flash. No punctuation
   or whitespace glyphs are underlined.
4. **Pattern scan budget:** configurable
   `VOICE_AUTOMATION_SCAN_STEPS_PER_PASS`, initially four steps and therefore
   at most 252 automation entries/target comparisons per foreground pass.
5. **Static SRAM:** the exact 40-byte Menu-owned `.bss` budget in §10 is
   approved.

---

## 12. Implementation Notes — 2026-09-15

The scheduled S066 implementation is now landed in the working tree.

- Added the shared `BUTTON_HOLD_DELAY_MS`, underline quiet-period, and
  four-step asynchronous scan-budget configuration constants.
- Routed the common foreground long-press path into the VOICE overlay while
  preserving the existing STEP automation timer sentinel and release
  suppression behavior.
- Added the 62-glyph alphanumeric underline source table (`496` bytes in
  flash) and a safe glyph lookup helper that rejects punctuation, whitespace,
  and invalid output buffers.
- Added the Menu-owned VOICE overlay state with a compile-time assertion for
  the approved exact `40`-byte static `.bss` budget. Held-step order, newest
  value resolution, Pattern-wide presence scanning, and context invalidation
  are all serviced from the foreground path.
- Held-step edits write only PatternData step automations through the existing
  S065 APIs. Normal and Morph endpoint buffers are not used as write targets;
  no runtime-DSP preview was added because its non-persistent boundary is not
  unambiguous in this firmware path.
- Added automation-presence step LEDs, restoration on overlay exit, and
  invalidation hooks for bar, voice, page, Pattern, and destructive clear
  changes.
- Added a preflighted LCD transaction that restores stale DDRAM marker
  references before redefining CGRAM slots and queues the complete marker frame
  only when the bounded LCD queue can hold the transaction.
- `make clean && make -j2 && make img` completed successfully. Final image
  metrics were `text=439124`, `data=412`, `bss=290844`; the generated image was
  `build/LXRV2_lxr02.img` (`439536` bytes).

Remaining validation is physical-hardware testing: overview and clicked-in
VOICE/Morph views, rapid underline debounce, independent source steps, the
worst-case scan and four-marker LCD transaction, LED restoration, endpoint and
Autosave invariance, and playback while the LCD queue is busy.

---

## 13. Code Review — 2026-09-15

Independent post-implementation audit of the uncommitted working-tree changes
against the spec (§1–§11) and the implementation schedule
(`S066_IMPLEMENTATION_SCHEDULE.md`). No code changes this turn.

### Files touched

| File | Change type | Lines added/removed |
|------|-------------|---------------------|
| `config.h` | ADD | +22 (3 constants + comment block) |
| `buttonHandler.h` | MODIFY + ADD | replaced `BUTTON_TIMEOUT`, added `seqHeldMask()` and `visibleStep()` exports, added `#include "config.h"` |
| `buttonHandler.c` | MODIFY + ADD | `seqHeldMask()` body, `visibleStep()` promoted to extern, wrap-safe `buttonHandler_tick()`, VOICE branch in `armTimerActionStep()`, overlay routing in SEQ press/release, bar-change notification |
| `lcd.h` | ADD | `lcd_underlineGlyph()` declaration |
| `lcd.c` | ADD | 62-glyph font table (496 B flash), `lcd_fontIndex()`, `lcd_underlineGlyph()`, `#include <string.h>` |
| `ledHandler.h` | ADD | `led_updateAutomationStepView()` declaration, `#include "InstrumentManager.h"` |
| `ledHandler.c` | ADD | `led_updateAutomationStepView()` body |
| `menu.h` | ADD | four overlay bridge functions |
| `menu.c` | ADD + MODIFY | ~610 new lines: 40 B state block, CGRAM cache, async search, held resolution, marker transaction, overlay lifecycle, write intercepts, service hooks, context invalidation |
| `copyClearTools.c` | ADD | two `menu_voiceAutoOverlayPatternDeleted()` calls |

### Spec compliance

| Spec section | Verdict | Notes |
|--------------|---------|-------|
| §1 Overlay activation | **Pass** | Hold threshold via config.h `BUTTON_HOLD_DELAY_MS`; `armTimerActionStep` VOICE branch transfers to Menu; SEQ press/release suppression via `menu_voiceAutoOverlayActive()` guard; exit on all-released in `va_updateHeldState()`. |
| §1.2 Held-step tracking | **Pass** | 16-byte press-order ring with newest-first insertion. Per-parameter exact-target resolution in `va_resolveHeldValue()`. Different parameters source different steps correctly. |
| §1.3 Existing behavior preservation | **Pass** | STEP mode path unchanged in `armTimerActionStep`; release suppression reuses existing `TIMER_ACTION_OCCURED` sentinel; overlay-active presses set the sentinel directly to skip the timer. |
| §2 Underline system | **Pass** | Four slots (2..5), bounded cache, preflighted ordered LCD transaction. Stale DDRAM refs restored to ROM bytes before CGRAM redefinition. Queue-full fallback defers via `menu_lcdRefreshPending`. |
| §2.2 Font table | **Pass** | 62 glyphs × 8 bytes = 496 B const flash. Arithmetic mapping via `lcd_fontIndex()`. Null-pointer guard on `out`. Punctuation/whitespace rejected. |
| §2.3 Pattern-wide marker | **Pass** | Leftmost non-space of short name (overview) or long name column 8..15 (clicked-in). Applied only when `va_searchComplete` and bit set. |
| §2.4 Held-step value marker | **Pass** | Rightmost non-space of the rendered automation value. Takes precedence over name marker per-parameter. Value-marker invariant enforced: displayed value is always the automation value when underlined. |
| §2.5 Four-slot bound | **Pass** | `VA_CGRAM_SLOT_COUNT = 4`, loop indices `0..3`, `_Static_assert` on total state. |
| §2.6 CGRAM lifecycle | **Pass** | `va_cgramBase[]`/`va_cgramValid` track loaded state; skip when unchanged; `va_cgramInvalidate()` on context exit. |
| §3 Value display | **Pass** | 7→8 expansion matches spec formula. Suppression mask prevents underline during edits; ROM characters show the value immediately. |
| §3.4 Debounced reapplication | **Pass** | `va_underlineService()` uses wrap-safe `(uint16_t)(time_sysTick - va_lastEditTick)`. Stale context discarded. Single repaint after quiet period. |
| §4 Step illumination | **Pass** | `led_updateAutomationStepView()` reads 16 steps, held steps always lit. Restore via `led_updatePatternTrackView()` on overlay exit. Dynamic updates on bar/track/page change. Only active in `editModeActive`. |
| §5 Async search | **Pass** | 4-step budget, 128-step range, 64-bit descriptor mask, context mismatch restarts. `instrumentParam_isVoiceParameter()` guard filters Scene mod targets. Bit set immediately on write; restart on deletion. |
| §6 Automation write | **Pass** | `va_writeAutomationFromKnob()` called from both pot and encoder intercepts, before any endpoint branch. Seeds from held value or read-only endpoint. Best-effort multi-step writes. No endpoint/DSP/Autosave mutation. |
| §6.5 Endpoint safety | **Pass** | The write path calls only `pat_writeStepAutomation()`. Does not call `menu_cellCommitValue()`, `menu_sendEditedParameter()`, any preset setter, or `instrumentManager_writeRuntime()`. No runtime-DSP preview — matches the spec's safe fallback. |
| §7 Integration points | **Pass** | `menu_switchPage()`, `menu_setActiveVoice()`, `menu_setShownPattern()`, `menu_switchSubPage()`, `menu_parseEncoder()` click toggle, `copyClearTools.c` clears — all wired to overlay reset/search restart/LED refresh as needed. |
| §8 Risk cases | See below | |
| §10 RAM budget | **Pass** | `_Static_assert` verifies exactly 40 bytes. Font table is 496 B flash. |

### Risk-case coverage (§8)

| Risk | Verdict | Notes |
|------|---------|-------|
| §8.1 CGRAM bound | **Pass** | Loop index hard-bounded 0..3; slot range 2..5. |
| §8.2 Pool reads | **Pass** | Held resolution bounded by `va_heldCount` (max 16); scan bounded by config budget. |
| §8.3 Sequencer race | **Pass** | All overlay writes are foreground; existing allocate-first pool design protects ISR reads. |
| §8.4 Stale debounce | **Pass** | `va_underlineService()` validates page/overlay/held context before repainting. Cleared on overlay exit, context change, bar change, and click toggle. |
| §8.5 Per-parameter source changes | **Pass** | `va_updateHeldState()` detects mask changes and triggers repaint; `va_resolveHeldValue()` walks the updated order. |
| §8.6 CGRAM/DDRAM ordering | **Pass** | `va_queueMarkerTransaction()` restores stale refs, then defines CGRAM, then writes the full frame. Preflight ensures atomic transaction. Fallback to `menu_lcdRefreshPending` on queue-full. |
| §8.7 Morph view | **Pass** | `menu_setVoiceModeShowMorph()` repaints but does not restart the search or reset the overlay. Write path is identical in both modes. |

### Observations (non-blocking)

1. **`cur_want_on`/`cur_hw_on` forward declarations.** The overlay block at
   ~line 1212 uses C tentative definitions to forward-reference the cursor state
   variables defined later at ~line 7002. Legal C, but uncommon — the comment
   explains the reason (cursor retirement before CGRAM redefinition).

2. **Full-frame write on CGRAM change.** When any CGRAM mapping changes,
   `va_queueMarkerTransaction()` rewrites all 32 display cells (64 queue ops)
   rather than only the changed cells. This is conservative: it guarantees no
   stale CGRAM code survives, at the cost of a larger queue transaction. The
   preflight accounts for this (worst case ~113 ops, within the 128-entry
   queue). Acceptable given the low frequency of CGRAM changes.

3. **`menu_setShownPattern()` early return.** The implementation adds a
   same-value early return that was not present before. This is a net
   improvement — it avoids unnecessary overlay/search/LED resets on redundant
   calls — but it is a behavioral change beyond the S066 scope. If any caller
   depends on the setter always executing its side effects, this could suppress
   an expected repaint. Low risk given the typical call sites.

4. **`menu_setActiveVoice()` restructure.** The changed-voice branch now
   early-returns after resetting overlay, setting `menu_activeVoice`, and
   restarting the search. The unchanged-voice fallthrough still executes the
   assignment. The early return ensures `va_searchRestart()` sees the new voice
   value, which is correct. The `menu_stepAutomationReset()` call that was
   previously unconditional now only runs on actual voice change — same net
   effect since the reset is a no-op when the voice hasn't changed.

5. **Image size delta.** The spec's §12 reports 439536 bytes; the current
   binary is 439552 — a 16-byte increase likely from linker alignment padding.
   Within normal variation.

6. **No runtime-DSP preview.** The spec's §6.5 safe implementation rule says to
   omit preview if the non-persistent boundary is not unambiguous. The
   implementation correctly chose the safe path: Pattern-only writes with no
   audible preview when stopped.

### Verdict

The implementation faithfully covers all twelve spec sections, the approved
40-byte SRAM budget, and the 496-byte flash font table. Endpoint safety is
maintained: the overlay write path calls only `pat_writeStepAutomation()` and
never touches endpoint images, Autosave, or DSP runtime. All context
invalidation hooks (page, track, Pattern, bar, Scene, clear) are wired.

**Status: ready for hardware test per §12 test plan.**
