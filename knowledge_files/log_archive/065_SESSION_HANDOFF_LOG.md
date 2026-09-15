# Session 065 — Step Automation Editing + Sequencer Playback

```
DATE: 2026-09-15
SESSION GOAL: Implement step automation editing (Method 1) and sequencer
  playback from S065_DYN_PAT_STEP_AUTOMATION.md §§1-5,8-9 via the
  S065_DYN_PAT_STEP_AUTOM_EDITING.md implementation plan.
COMPLETED: Full end-to-end step automation path: pool block read/write/remove
  APIs, step-edit menu with cursor navigation and detail views, sequencer
  pending buffer with foreground drain, per-slot dirty bitmap and trigger-time
  restore, dtype-aware value display and bounds clamping. Hardware-tested.
VERIFIED ON HARDWARE: yes — automation application, restore-on-trigger, menu
  navigation, value display, dtype-aware formatting, bounds clamping

CHANGES THIS SESSION:
- Core/Sequencer/sequencer.c: +89 lines — seq_automation_dirty[6] bitmap,
  seq_clearAutomationDirty(), seq_restoreAutomatedParameters(),
  dirty-bit set in seq_drainPendingAutomation(), clear in seq_init/
  seq_setRunning/seq_setStepIndexToStart
- Core/Sequencer/sequencer.h: +10 lines — public declaration of
  seq_restoreAutomatedParameters()
- Core/MIDI/MidiVoiceControl.c: +7 lines — #include "sequencer.h",
  seq_restoreAutomatedParameters(voice) call in voiceControl_triggerNow()
  after deferred Scene apply, before instrumentManager_triggerTrack()
- Core/Menu/menu.c: +411/-62 lines — menu_stepAutoCursor,
  menu_stepAutoNumberLocked state; cursor-aware navigation in
  menu_moveToMenuItem; cursor-aware click dispatch in menu_parseEncoder;
  number-lock + field-edit in menu_encoderChangeParameter;
  menu_repaintStepAutomation restructured with detail views and compact view;
  checkScrollSign '>' for specials subpage; memset stale-character fix in
  overview path; menu_stepAutomationReset expanded; post-add cursor reset;
  menu_automationValueMax() helper; menu_formatAutomationValue3() helper;
  dtype-aware bounds clamping in field 3 editor
- main.c: +8/-4 lines — seq_drainPendingAutomation() moved into
  audio_check_and_render() immediately after voiceControl_processPending();
  standalone drain call and comment block removed from main loop
- S065_DYN_PAT_AUTOM_EDITING_ADDITIONS.md: +192 lines — post-testing
  amendments document (Sections A-D)

KNOWN ISSUES INTRODUCED: none
KNOWN ISSUES RESOLVED:
- Automation ordering race (50% success rate): drain and trigger ring
  consumer were in different main loop positions. Fixed by co-locating them
  in audio_check_and_render().
- Wrong restore value source: seq_restoreAutomatedParameters read from
  instrument_parameters[] (raw Scene A) instead of morph_interpolation[]
  (morph-crossfader result). Fixed.
- numtostrpu 3-character overflow on compact view page number: wrote 3
  chars to a 2-character field. Fixed with manual 2-digit formatting.
- Filter type adjustable past menu table limits: field 3 value editor
  clamped to 0-127 regardless of dtype. Fixed with dtype-aware upper bound
  via menu_automationValueMax().
- Zero-padded amount display: changed to space-padding for consistency.

NEXT SESSION RECOMMENDED GOAL: Session 066 — VOICE page held-step automation
  overlay (Method 2) from S065_DYN_PAT_VOICE_PARAM_UX.md: CGRAM underline
  rendering, step illumination, pot-to-automation write, async track-wide
  search agent.
BLOCKERS: none

CRITICAL REMINDERS FOR NEXT SESSION:
- seq_drainPendingAutomation() MUST remain inside audio_check_and_render()
  immediately after voiceControl_processPending(). Moving it elsewhere
  reintroduces the 50% ordering race.
- seq_restoreAutomatedParameters() reads morph_interpolation[], NOT
  instrument_parameters[]. The user explicitly specified this.
- numtostrpu(buf, num, pad) writes 3 characters. Never point it at a
  2-character field.
- getMenuItemNameForValue() lacks OOB checking for non-waveform menu
  tables. menu_formatAutomationValue3() bounds-checks before calling it.
- The S065_DYN_PAT_VOICE_PARAM_UX.md general plan is ready for Session 066.
  It has open questions on overlay entry gesture (Option A recommended),
  pot writes to Scene image, font table scope, and Tier 2 search rate.
```

---

## 1. Implementation Detail

### 1.1 Automation parameter reset on voice retrigger (Section A)

**Problem**: When the sequencer applied automation via
`seq_drainPendingAutomation` → `instrumentManager_writeRuntime`, the DSP
runtime value persisted indefinitely. No trigger path restored the Scene
image value.

**Solution**: Per-slot 64-bit dirty bitmap (`seq_automation_dirty[6]`, 48 B
static SRAM). On each successful `instrumentManager_writeRuntime` in the
drain path, the corresponding descriptor index bit is set. Before each voice
trigger, `seq_restoreAutomatedParameters(voice)` iterates set bits using
`__builtin_ctzll` and writes the `morph_interpolation[]` value back to the
runtime. The bitmap is then cleared.

**Restore value source**: `morph_interpolation[local]` — the
morph-crossfader's interpolated result. NOT `instrument_parameters[local]`
(raw Scene A endpoint). The user explicitly specified: "the value morph
*would* apply, the interpolation value, but I don't want this running through
the morph drain, it should reset the interpolation value directly."

**Insertion point**: `voiceControl_triggerNow()` in `MidiVoiceControl.c`,
after `preset_applyDeferredSceneSlotForTrigger(voice)` and before
`instrumentManager_triggerTrack(voice, note, vel)`. This covers all trigger
sources (sequencer, MIDI, roll, button preview) via the single funnel.

**Clear points**: `seq_init()`, `seq_setRunning()` (transport stop),
`seq_setStepIndexToStart()` — prevents stale automation from crossing
transport or Scene/Pattern context boundaries.

### 1.2 Automation ordering race (Section D1)

**Symptom**: Automation applied ~50% of the time, randomly.

**Root cause**: `voiceControl_processPending()` (which calls
`voiceControl_triggerNow` → `seq_restoreAutomatedParameters`) ran inside
`audio_check_and_render()` at `main.c:191`, called ~8 times per main-loop
iteration. `seq_drainPendingAutomation()` sat at a single fixed point at
`main.c:1250`, between two `audio_check_and_render()` calls.

When TIM3 enqueued both a trigger and automation entries, and the ISR landed
between the `audio_check_and_render()` before the drain and the drain itself:
1. Drain ran first → applied automation → set dirty bits
2. Next `audio_check_and_render()` → processed the trigger → restore cleared
   the dirty bits that were *just set*
3. Trigger fired with non-automated (restored) values

Whether the ISR landed before or after the drain was timing-dependent.

**Fix**: Moved `seq_drainPendingAutomation()` into `audio_check_and_render()`
immediately after `voiceControl_processPending()`, inside the per-chunk
render loop:

```c
for (uint32_t frame = 0; frame < AUDIO_DMA_FRAMES; frame += OUTPUT_DMA_SIZE) {
    uint32_t basepri;
    voiceControl_processPending();
    seq_drainPendingAutomation();
    basepri = dsp_maskLowPriorityIrqs();
    mixer_calcNextSampleBlock(&buf[frame * 2], &buf2[frame * 2]);
    irq_setBasepri(basepri);
}
```

Removed the standalone call from `main.c:1245-1251`. The trigger ring is now
always consumed before the automation buffer within the same render chunk.

### 1.3 Wrong restore value source (Section D2)

`seq_restoreAutomatedParameters()` at `sequencer.c:657` originally read from
`instrument->parameter_images.instrument_parameters[local]` — the raw
Scene A endpoint. Changed to `morph_interpolation[local]`. This also fixed
the "freeze" symptom when changing automation targets: the restored value
differed from the morph-interpolated value, the morph system would later
overwrite it, creating a visible race.

### 1.4 Step-edit menu (Sections B, D3, D5)

**Cursor state**: `menu_stepAutoCursor` (0=number, 1=del/clr, 2=voice,
3=parameter, 4=amount) and `menu_stepAutoNumberLocked` (0/1). Both reset
in `menu_stepAutomationReset()`.

**Navigation** (`menu_moveToMenuItem`): number-locked mode changes page
index directly; normal mode moves cursor through items 0-4 on assigned
pages (0-1 on add page), wrapping to adjacent pages at boundaries. Left
past page 0 cursor 0 exits to the probability specials page.

**Click dispatch** (`menu_parseEncoder`): cursor 0 toggles number lock;
cursor 1 executes add/del/clr; cursor 2-4 toggle editModeActive for detail
views.

**Detail views** (cursor 2/3/4 with editModeActive):
- Cursor 2 (Voice): `"Target  Voice"` / voice number 1-6
- Cursor 3 (Parameter): `"Target  Parametr"` / `descriptor->category` +
  `descriptor->long_name` (e.g., "OscilltrCoarse"); "Invalid" for stale
  targets
- Cursor 4 (Amount): `"Autom.  Amount"` / dtype-aware 3-character value

**Compact view**: 2-digit zero-padded page number (manual formatting to
avoid `numtostrpu` overflow), cursor characters (`>`, `*`, space),
uppercase labels for selected items, `>` scroll indicator at position 15.

**Amount display**: Both detail and compact views use
`menu_formatAutomationValue3(descriptor, value, buf)` for dtype-aware
3-character formatting:
- `DTYPE_MENU`: short name from menu text table (bounds-checked)
- `DTYPE_ON_OFF`: `on`/`off`
- `DTYPE_MIX_FM`: `mix`/`fm`
- `DTYPE_LFO_POLARITY`: `neg`/`pos`/`bi`
- `DTYPE_PM63`: signed ±63
- `DTYPE_NOTE_NAME`: note name
- Default: space-padded numeric

**Bounds clamping**: Field 3 value editor resolves the target's descriptor
and calls `menu_automationValueMax(desc)` for the dtype-aware upper bound
instead of hardcoded 127:
- `DTYPE_MENU`: `table[0][0] - 1` (except `MENU_WAVEFORM` → 127)
- `DTYPE_ON_OFF`, `DTYPE_MIX_FM`: 1
- `DTYPE_LFO_POLARITY`: 2
- Default: 127

**Stale character fix**: `memset(editDisplayBuffer[0], ' ', 16u)` and
`memset(editDisplayBuffer[1], ' ', 16u)` added at the entry to the overview
path in `menu_repaintGeneric` to clear characters left by the automation
renderer.

**Specials scroll indicator**: `checkScrollSign` returns `>` for SEQ_PAGE
subpage 1 parameters 0-2 when `!menu_stepAutoActive`.

### 1.5 numtostrpu overflow bug

`numtostrpu(&editDisplayBuffer[0][1], page, '0')` wrote 3 characters to
positions 1-3, but the compact layout needs only 2 digits (positions 1-2)
with position 3 as a space separator before "voi" at position 4. The third
digit bled into the separator. Fixed with manual 2-digit formatting:
```c
editDisplayBuffer[0][1] = (char)('0' + (page / 10u));
editDisplayBuffer[0][2] = (char)('0' + (page % 10u));
```

### 1.6 Duplicate target prevention

Audited: `pat_writeStepAutomation()` scans existing entries and updates
in-place if target found, appends only if new. No TOCTOU window since all
writers are foreground-only. No code change needed.

---

## 2. RAM Impact

| Symbol | Size | Region | Owner |
|--------|------|--------|-------|
| `seq_automation_dirty[6]` | 48 B | `.bss` SRAM1 | sequencer.c |
| `menu_stepAutoCursor` | 1 B | `.bss` SRAM1 | menu.c |
| `menu_stepAutoNumberLocked` | 1 B | `.bss` SRAM1 | menu.c |
| **Total new static** | **50 B** | | |

The sequencer pending automation buffer (192 B) and drain flag/count
variables were allocated in the previous commit (`87e275e`), not this
session's changes.

---

## 3. Build

Post-fix build: `text=434,036 data=412 bss=290,788 total=725,236`.
Clean, no warnings.

Previous commit baseline (`87e275e`): `text=432,348`.

Session delta: +1,688 B text, +824 B BSS (50 B new static + the BSS delta
from Section A dirty bitmap and Section B menu state).

---

## 4. Files Changed

| File | Lines | Change |
|------|-------|--------|
| `Core/Sequencer/sequencer.c` | +89 | dirty bitmap, clear helper, restore function, dirty-bit set in drain, init/stop clear |
| `Core/Sequencer/sequencer.h` | +10 | `seq_restoreAutomatedParameters()` declaration |
| `Core/MIDI/MidiVoiceControl.c` | +7 | include, restore call in triggerNow |
| `Core/Menu/menu.c` | +411/-62 | cursor state, navigation, click dispatch, encoder edit, detail views, compact view, helpers, bounds clamping, overview memset, scroll indicator |
| `main.c` | +8/-4 | drain relocation into audio_check_and_render |
| `S065_DYN_PAT_AUTOM_EDITING_ADDITIONS.md` | +192 | post-testing amendments document |

---

## 5. Planning Documents Produced This Session

These documents contain detailed specifications and implementation plans.
Their durable content is preserved in this handoff log and in the updated
specification references. The documents themselves will be deleted after
archival:

| Document | Contents |
|----------|----------|
| `S065_DYN_PAT_STEP_AUTOMATION.md` | General plan: automation wire format (2-byte LE, 7-bit value + 9-bit target), block layout with automation, pool block reader/writer extension, Method 1 step-edit menu, Method 2 VOICE overlay, sequencer playback design, PAT4 compatibility, risk cases, implementation order |
| `S065_DYN_PAT_STEP_AUTOM_EDITING.md` | Session 065 implementation plan: detailed task phases A-F with code locations, PatternData API signatures, sequencer pending buffer design, step-edit menu rendering, RAM impact |
| `S065_DYN_PAT_AUTOM_EDITING_ADDITIONS.md` | Post-testing amendments: Section A (dirty bitmap + trigger-time restore), Section B (step-edit menu UX redesign with cursor/navigation/detail views), Section C (implementation audit), Section D (ordering race fix, restore source fix, display label/amount fixes) |
| `S065_DYN_PAT_VOICE_PARAM_UX.md` | Session 066 general plan: VOICE overlay activation, two-tier CGRAM underline, value display, step illumination, async track-wide search, pot-to-automation write |

---

## 6. Key Technical Details for Future Reference

### 6.1 Automation entry wire format

Each automation entry is 2 bytes, stored little-endian in the pool:
```
bits 15..9   7-bit value (0..127)
bits 8..0    9-bit parameter target (instrument_param_id_t)
```

The 9-bit target IS the existing `instrument_param_id_t` canonical namespace:
`id = slot * 64 + descriptor_index`. Voice IDs 0..383, Scene targets 384+.

### 6.2 Block layout with automation

```
bytes 0..1    LE header (bits 15..6: track*128+step, bits 5..0: auto_count)
byte 2        special flags
bytes 3..     present special values
next bytes    automation entries, 2 bytes each, auto_count entries
padding       zero to 4-byte boundary
```

### 6.3 7-bit ↔ 8-bit conversion

Same as MIDI CC path: `(v == 127) ? 255 : v * 2` (expand),
`(v >= 255) ? 127 : v / 2` (compress).

### 6.4 Trigger ordering guarantee

Inside `audio_check_and_render()`, per render chunk:
1. `voiceControl_processPending()` — drains trigger ring, each trigger calls
   `seq_restoreAutomatedParameters()` then `instrumentManager_triggerTrack()`
2. `seq_drainPendingAutomation()` — applies this step's automation entries
3. `mixer_calcNextSampleBlock()` — reads the now-correct runtime values

The ISR enqueues trigger then automation; the foreground processes them in
the same order within a single render chunk.

### 6.5 Morph interpolation as restore source

Three parameter image arrays per slot in `SceneData.h`:
- `instrument_parameters[]` — Scene A (morph endpoint)
- `morph_instrument_parameters[]` — Scene B (morph endpoint)
- `morph_interpolation[]` — runtime interpolated result

Restore uses `morph_interpolation[]` so the parameter returns to wherever
the morph crossfader currently has it, not to the raw Scene A endpoint.

### 6.6 DTYPE system for value display

`ParamDescriptor.dtype` low nibble = type enum, high nibble = menu table ID
for DTYPE_MENU. Menu text tables store count at `table[0][0]`, entries at
`table[1..n]`. `getMenuItemNameForValue()` lacks OOB checking except for
MENU_WAVEFORM (sample name fallback), so `menu_formatAutomationValue3()`
bounds-checks before calling it.

### 6.7 Uniqueness invariant

A step must never contain two automation entries with the same 9-bit target.
`pat_writeStepAutomation()` enforces this by scanning existing entries and
updating in-place if the target is found. All writers are foreground-only,
so no TOCTOU window exists.

---

## 7. Session 066 Preparation

`S065_DYN_PAT_VOICE_PARAM_UX.md` is ready as the general plan. It covers:

1. **Overlay activation**: long-press on SEQ button in VOICE mode (Option A
   recommended), held-step mask tracking, focus step on most recent press
2. **Two-tier CGRAM underline**: Tier 1 (held-step full underline) fills
   CGRAM slots 2-7 first; Tier 2 (track-wide first-char underline) uses
   remaining slots; priority allocation with silent degradation
3. **Font table**: HD44780A00 ROM glyphs compiled into ~768 B flash for
   underline glyph generation
4. **Value display**: focus step's automated value (7→8 bit expanded) with
   underlined characters
5. **Step illumination**: in single-parameter view with overlay active, SEQ
   LEDs show only steps carrying automation for the viewed parameter
6. **Pot-to-automation write**: pot delta writes to all held steps via
   `pat_writeStepAutomation()`, best-effort for pool exhaustion
7. **Async track-wide search**: polled foreground agent scanning 128 steps,
   4-8 steps per pass, updating Tier 2 underline indicators

Open questions for Session 066:
- Overlay entry gesture: long-press (Option A) vs. immediate hold (Option B)
- Pot writes to Scene image: update both, or automation-only?
- Font table scope: full ASCII 0x20-0x7F (768 B) or audited subset (~320 B)?
- Tier 2 search rate: 4 or 8 steps per foreground pass?

Prerequisites from this session (all landed):
- `pat_readStepAutomations()`, `pat_writeStepAutomation()`,
  `pat_removeStepAutomation()`, `pat_stepAutomationCount()`
- `instrumentManager_targetValid()` extended for Scene targets
- `instrumentParam_make()` / `instrumentParam_slot()` / `instrumentParam_local()`
- Sequencer pending buffer and foreground drain path
