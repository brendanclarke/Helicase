# S070 Phase 3 — Item 3.1 Remediation

## Session implementation notes

### 2026-09-23 — remediation started

- Read `MEMORY.md`, the Phase 3 feature plan, this remediation plan, and the
  dynamic-stack specification before changing source.
- Confirmed the live defect in `Core/Sequencer/sequencer.c`: special values,
  probability, trigger dispatch, and automation queueing are currently nested
  under `pat_isStepActive()`, so non-trigger automation never reaches the
  pending queue.
- Confirmed the project RNG API is `GetRngValue()` from `Core/DSPAudio/random.h`;
  the implementation will preserve the existing 0..127 probability conversion
  while moving it into the single conditional-gate helper described below.
- The remediation is internal to `sequencer.c`; no public declaration changes
  are required in `sequencer.h`. The new static helper and the revised step
  routine will each carry their descriptive block immediately adjacent to the
  code.

### 2026-09-23 — source correction applied

- Added `seq_evaluateStepCondition()` immediately before
  `seq_advanceTrackStep()`. It preserves the existing `GetRngValue()`
  probability conversion and provides the single insertion point for future
  conditional-trigger types.
- `seq_advanceTrackStep()` now resolves specials and evaluates the gate before
  `pat_isStepActive()`. Trigger dispatch remains additionally dependent on the
  trigger bit; automation dispatch depends on the gate and the existing
  live-erase guard, not on trigger state.
- Erase remains inside the active-step edit path and outside the conditional
  playback decision. The next step is a full project build and diff review.

## The error

Change 3.1-A in `S070_PHASE3_IMPLEMENTATION.md` moved
`seq_queueStepAutomations()` inside the `if (pat_isStepActive(...))` block
and further inside the `if (should_play)` block. This kills automation on
every non-trigger step. The entire value of being able to place automation
on a non-trigger step — parameter sweeps, value changes between notes,
timbral motion independent of rhythm — is destroyed.

The specification is unambiguous. `PATTERN_DYNAMIC_STACK.md` §6.1 states:

> On each step advance, `seq_advanceTrackStep()` reads automation entries
> from every step that has a pool block (bit 14 set, valid offset),
> **regardless of trigger state**.

The implementation document violated this by conflating "probability must
gate automation" with "automation must be inside the trigger block." These
are different things. Probability is a per-step property stored in the
dynamic block. It exists independently of the trigger bit. A non-trigger
step can have probability set alongside automation entries, and when it does,
probability must gate that automation.

## What conditional gating actually means

The step's dynamic block can carry a **conditional trigger gate** — currently
probability, but planned to expand to a general-purpose conditional trigger
system (see §Future below). The gate applies to the **complete step**:
everything that step contributes to the sound — trigger, automation, all of
it. The gate applies to any step that has a condition set in its dynamic
block, whether or not the step has its trigger bit (bit 15) active.

- Non-trigger step, no condition, has automation → automation fires
  unconditionally. This is the normal case for parameter sweeps.
- Non-trigger step, condition set, has automation → condition evaluation
  determines whether automation fires. Failed condition = step is silent in
  every sense.
- Trigger step, condition set, has automation → condition evaluation
  determines whether trigger AND automation fire. This is the original
  defect site.
- Trigger step, no condition, has automation → both fire unconditionally.
- Any step, erase active → erase proceeds regardless of conditions.
  Erase is a destructive edit operation, not a playback decision.

## Why the error happened

The probability special is currently read inside the `pat_isStepActive()`
block (line 570). I treated the step-active block as the only place
probability could be evaluated and jammed the automation call in alongside
the trigger. I did not consider that probability is a dynamic-block property
independent of the trigger bit, and that non-trigger steps with dynamic
blocks must also be subject to it.

## Correct fix

Move `pat_readStepSpecials()` **before** the step-active check. Evaluate
the conditional gate **before** the step-active check. Gate the trigger by
step-active AND step_allowed. Gate automation by step_allowed alone. Leave
erase independent.

### Current structure (lines 557–587)

```c
if (!(seq_mutedTracks & (1u << track))) {
    if (pat_isStepActive(...)) {
        if (seq_eraseActive && track == menu_getActiveVoice()) {
            // erase path
        } else {
            pat_step_specials_t sp = pat_readStepSpecials(...);
            uint8_t should_trigger = 1u;
            if (sp.probability < 127u) {
                // RNG roll; maybe should_trigger = 0
            }
            if (should_trigger) {
                seq_triggerVoice(track, sp.velocity, sp.note);
                seq_queueStepAutomations(track, (uint8_t)seq_stepIndex[track]);
            }
        }
    }
}
```

### Corrected structure

```c
if (!(seq_mutedTracks & (1u << track))) {
    /*
     * Conditional step gate: read the step's dynamic-block specials
     * before evaluating trigger or automation. The gate determines
     * whether this step participates in playback at all.
     *
     * Currently the only condition is probability (RNG roll against
     * stored 0..127 value, default 127 = always pass). This is the
     * entry point for future conditional trigger expansion — any
     * state-dependent gating logic (track mute combinations, loop
     * count, scene chain origin, button state, etc.) evaluates here
     * alongside probability to produce a single step_allowed decision.
     *
     * step_allowed gates the complete step: a failed condition
     * suppresses both voice trigger and automation queueing. Erase
     * is independent of the conditional gate.
     */
    pat_step_specials_t sp = pat_readStepSpecials(
        seq_activePattern, track, (uint8_t)seq_stepIndex[track]);
    uint8_t step_allowed = seq_evaluateStepCondition(&sp);

    if (pat_isStepActive(...)) {
        if (seq_eraseActive && track == menu_getActiveVoice()) {
            // erase path — independent of conditional gate
        } else if (step_allowed) {
            seq_triggerVoice(track, sp.velocity, sp.note);
        }
    }

    if (step_allowed) {
        if (!seq_eraseActive || track != menu_getActiveVoice())
            seq_queueStepAutomations(track,
                (uint8_t)seq_stepIndex[track]);
    }
}
```

### `seq_evaluateStepCondition()` — new static function

```c
/*
 * Evaluate the conditional trigger gate for one step.
 *
 * Inputs: pointer to the step's resolved specials (read from the dynamic
 * block before the trigger-active check). Output: 1 if the step is allowed
 * to participate in playback (trigger + automation), 0 if suppressed.
 *
 * Currently evaluates probability only: default 127 (or absent special)
 * always passes; values < 127 are compared against an RNG draw.
 *
 * This function is the single entry point for all step-level conditional
 * gating. When conditional triggers expand beyond probability (track mute
 * combinations, loop iteration count, scene chain origin, held button
 * state, etc.), those evaluations are added here. The probability special's
 * value space will be partitioned: 1..100 (or decade values) for
 * probability, remaining values as selectors for named conditions. The
 * function returns a single allow/suppress decision regardless of how many
 * condition types are evaluated.
 *
 * Common caller: seq_advanceTrackStep() (sole caller, ISR context).
 * Affiliates: pat_readStepSpecials(), GetRngValue().
 */
static uint8_t seq_evaluateStepCondition(const pat_step_specials_t *sp)
{
    /* Probability gate. */
    if (sp->probability < 127u) {
        uint8_t rng = (uint8_t)(((uint16_t)(GetRngValue() & 0x7FFFu) *
                                 127u) / 32767u);
        if (rng >= sp->probability)
            return 0u;
    }

    /* Future conditional trigger evaluations go here. */

    return 1u;
}
```

### What changed and why

1. `pat_readStepSpecials()` moves from inside the step-active block to
   before it. This reads the dynamic block for every visited step, not just
   trigger-active ones. Cost: one address-word read + sentinel check for
   steps without a dynamic block (cheaper than the already-unconditional
   `seq_queueStepAutomations()` call that follows).

2. The probability roll moves into `seq_evaluateStepCondition()`, which runs
   before the step-active check. `step_allowed` is computed once and used
   twice: once for the trigger, once for automation.

3. The trigger fires only when `pat_isStepActive() && step_allowed && !erase`.
   The `else if (step_allowed)` replaces the former `else { ... if
   (should_trigger) ... }` — same gate, different nesting level.

4. Automation fires only when `step_allowed`. The existing erase guard
   (`!seq_eraseActive || track != menu_getActiveVoice()`) is preserved
   inside the step_allowed block.

5. Erase remains inside `pat_isStepActive()` and outside `step_allowed`.
   A step being erased is a user edit, not a playback decision.

### ISR cost analysis

`pat_readStepSpecials()` is now called for every step on every track,
not just trigger-active steps. For steps without a dynamic block (no bit 14),
the function reads one `uint16_t` address word, checks the sentinel, and
returns defaults. This is a single SRAM1 read + branch. For steps with a
block, it reads the header + flags + present specials — the same work it
does today, just potentially on non-trigger steps too.

After this fix, `seq_queueStepAutomations()` runs for every allowed step and
does the heavier work: block address read, header decode, and automation entry
iteration. Adding the specials read before it is strictly cheaper than the
work that follows for a step carrying a dynamic block, while steps without a
block still take only the bounded address/sentinel checks in each helper.

### Comment block for `seq_advanceTrackStep()`

```c
/*
 * Advance one track's sequencer step and fire trigger+automation.
 *
 * Inputs: track index 0..6 from TIM3 ISR tick at priority 2 (4 kHz).
 *
 * The step's dynamic-block specials are read before the trigger-active
 * check. seq_evaluateStepCondition() evaluates all conditional gates
 * (currently probability; future: named conditions) and produces a single
 * step_allowed decision that applies to every step carrying a condition,
 * regardless of whether the step has a voice trigger. A non-trigger step
 * with automation and a condition is subject to the same evaluation as a
 * trigger step. Default (no condition set) always passes. Erase is
 * independent of the conditional gate.
 *
 * Output: step_allowed controls both seq_triggerVoice() (gated additionally
 * by step-active and not-erasing) and seq_queueStepAutomations() (gated
 * additionally by the erase guard). A step whose condition fails produces
 * no trigger and no automation entries; previously held values persist
 * through the skipped step exactly as if it were absent.
 *
 * Affiliates: PatternData (region ownership, step trigger/automation data),
 * seq_evaluateStepCondition() (conditional gate evaluator),
 * seq_drainPendingAutomation() (foreground consumer of queued automation).
 */
```

## Future: conditional trigger expansion

The probability special byte currently stores 0..127 with 127 meaning
"always play." This will be restructured into a **conditional trigger**
system where the byte's value space is partitioned:

- **Probability values** (e.g. 1..100 or decade steps 10/20/../100): RNG
  roll as today, but with a reduced value range.
- **Named condition selectors** (remaining values): state-dependent gates
  evaluated at step time. Planned conditions include but are not limited to:
  - Other tracks muted/unmuted (e.g. "play only if track 3 is muted")
  - Track loop iteration count within the current pattern
  - Whether the current pattern was reached via scene/pattern chain
  - User holding or having pressed a button combination
  - Any other runtime state that can be cheaply evaluated in ISR context

The `seq_evaluateStepCondition()` function is the single entry point for
all of this. When the conditional trigger system is built:

1. The probability special byte is reinterpreted: low values are probability,
   high values are condition selectors.
2. `seq_evaluateStepCondition()` gains a switch or lookup for the selector
   values, each calling a small evaluator function.
3. The `pat_step_specials_t` struct may gain a decoded condition field
   alongside the raw probability byte.
4. The step-edit UI's probability page becomes a conditional trigger page
   with the probability range as one option among several.

The key invariant is unchanged: the conditional gate runs before the
trigger-active check, gates the complete step, and defaults to allow.

## Specification update

`PATTERN_DYNAMIC_STACK.md` §6.1 has been updated to state:
- Automation on non-trigger steps is valid audio automation data and a
  critical product feature.
- Conditioning automation queueing on trigger state (bit 15) is
  unconditionally wrong.
- Trigger condition including probability gates the complete step: trigger
  and automation together.
- The conditional gate is evaluated before the trigger-active check.

### 2026-09-23 — verification completed

- `make all -j2` passed. The final linked image reports `text=455196`,
  `data=416`, and `bss=291804` bytes.
- `make img` passed and regenerated `build/LXRV2_lxr02.img` at 455628 bytes
  (455612-byte firmware payload plus the 16-byte LXRV2 image header).
- `git diff --check` passed. No public sequencer header changed because the
  conditional gate is a private playback helper; the changed `.c` code has
  adjacent descriptive block comments.
- The only build diagnostics are the existing nano-libc syscall warnings and
  serial LTO warning; no changed-file compiler warning was emitted.

### 2026-09-23 — Issues 2 and 3 implementation started

- Confirmed Issue 2 in `Core/Menu/menu.c`: `va_scanService()` only records
  voice-descriptor targets in its 64-bit mask, while the Scene target held-value
  path already resolves the same 384..403 namespace correctly.
- Confirmed Issue 3 in the same file: `menu_cellDisplayValue()` already reads
  live Scene-owned values, but `menu_serviceRuntimeWidgets()` has no playback
  refresh path for the VOICE/mix or PERF surfaces.
- The planned state additions are bounded and documented: one Scene-marker
  mask byte and one 16-bit live-refresh timestamp, both foreground Menu state.
  The original 44-byte overlay state is now explicitly asserted at 45 bytes
  after the Scene-marker extension.

---

## Issue 2 — Scene setting underlines missing on mix sub-page

### Problem

The overlay's pattern-wide automation search agent (`va_scanService()`,
`menu.c:1786`) only records voice descriptor targets. It calls
`instrumentParam_isVoiceParameter(autos[i].target)` at line 1811 and
`instrumentParam_slot(autos[i].target) == slot` at line 1812 — Scene
targets (ID 384+) fail the first check and are silently dropped. The
`va_searchTargetMask[]` bitmap (64 bits, indexed by `descriptor_index`)
has no representation for Scene targets at all.

The result: when the user is on the VOICE/mix sub-page and a step somewhere
in the track has automation for Voice Morph, Audio Out, or FX Send, the
compact-view parameter name underline never appears. Held-step value
underlines DO work (the `va_resolveHeldValue()` path at line 2333 handles
Scene targets correctly) — only the pattern-wide "this parameter is
automated somewhere" marker is missing.

### Root cause

The search agent and its result bitmap were designed for voice descriptors
only (Session 066). The Phase 3 implementation added Scene target handling
to the held-value path and the knob-write path but did not extend the
search agent to also detect Scene targets in the automation pool.

### Fix

#### Change R2-A: Extend `va_scanService()` to detect Scene targets

**File:** `Core/Menu/menu.c`
**Location:** `va_scanService()`, lines 1786–1820
**Operation:** MODIFY

The `va_searchTargetMask[]` bitmap is 64 bits (8 bytes), indexed by voice
descriptor index 0..63. Scene targets cannot be indexed into this bitmap
because they use a different ID space (384+). Add a separate small bitmap
for Scene setting automation presence.

```c
/*
 * Scene-target automation search result: one bit per automatable
 * Scene setting kind on the current voice slot.
 *
 * Bit 0 = VOICE_MORPH, bit 1 = AUDIO_OUT, bit 2 = FX_SEND.
 * Set by va_scanService() when any step on the active track has
 * automation targeting the corresponding Scene target for the active
 * voice slot. Cleared by va_searchRestart(). Read by
 * va_applyVoiceMarkers() to show pattern-wide underlines on
 * MENU_CELL_SCENE_SETTING cells.
 *
 * Affiliate: va_searchTargetMask[] (voice descriptor bitmap),
 * sceneModTarget_descriptor() (Scene target table lookup).
 */
static uint8_t va_searchSceneMask = 0u;
```

RAM cost: 1 byte.

In `va_searchRestart()` (line 1752), add:
```c
    va_searchSceneMask = 0u;
```

In the `va_scanService()` inner loop (line 1810–1814), after the existing
voice target check, add a Scene target check:

```c
        for (i = 0u; i < count; i++) {
            if (instrumentParam_isVoiceParameter(autos[i].target) &&
                instrumentParam_slot(autos[i].target) == slot)
                va_searchSetBit(instrumentParam_local(autos[i].target));
            else if (sceneModTarget_isSceneTarget(autos[i].target)) {
                const scene_mod_target_descriptor_t *desc =
                    sceneModTarget_descriptor(autos[i].target);
                if (desc && desc->voice_slot == slot) {
                    switch (desc->kind) {
                    case SCENE_MOD_TARGET_KIND_VOICE_MORPH:
                        va_searchSceneMask |= 0x01u;
                        break;
                    case SCENE_MOD_TARGET_KIND_AUDIO_OUT:
                        va_searchSceneMask |= 0x02u;
                        break;
                    case SCENE_MOD_TARGET_KIND_FX_SEND:
                        va_searchSceneMask |= 0x04u;
                        break;
                    default:
                        break;
                    }
                }
            }
        }
```

Note: Scene targets without a `voice_slot` (DECIMATION_ALL,
SLOT6_TRACK7_AMP_DECAY) are global, not per-voice. They do not appear on
any per-voice VOICE sub-page, so filtering by `desc->voice_slot == slot`
is correct for the underline use case.

#### Change R2-B: Extend compact-view marker path for Scene settings

**File:** `Core/Menu/menu.c`
**Location:** `va_applyVoiceMarkers()`, compact view loop at lines 2358–2374
**Operation:** MODIFY

The existing compact-view marker path (line 2358) only checks
`cell.kind == MENU_CELL_INSTRUMENT`. Add a parallel check for Scene
settings using `va_searchSceneMask`:

```c
        } else if (cell.kind == MENU_CELL_SCENE_SETTING &&
                   va_searchComplete) {
            uint8_t scene_bit = 0u;
            switch (cell.scene_setting) {
            case MENU_SCENE_SETTING_VOICE_MORPH:
                scene_bit = 0x01u;
                break;
            case MENU_SCENE_SETTING_AUDIO_OUT:
                scene_bit = 0x02u;
                break;
            case MENU_SCENE_SETTING_FX_SEND_AMOUNT:
                scene_bit = 0x04u;
                break;
            default:
                break;
            }
            if (scene_bit != 0u &&
                (va_searchSceneMask & scene_bit) != 0u) {
                int8_t left;
                uint8_t start = (uint8_t)(4u * i);
                for (left = 0; left < 3 &&
                     editDisplayBuffer[0][start + left] == ' '; left++)
                    ;
                if (left < 3 && lcd_underlineGlyph(
                        (uint8_t)editDisplayBuffer[0][start + left],
                        glyph_probe)) {
                    desired_base[i] =
                        (uint8_t)editDisplayBuffer[0][start + left];
                    marker_row[i] = 0u;
                    marker_col[i] = (uint8_t)(start + left);
                    desired_valid |= (uint8_t)(1u << i);
                }
            }
        }
```

#### Change R2-C: Extend edit-mode marker path for Scene settings

**File:** `Core/Menu/menu.c`
**Location:** `va_applyVoiceMarkers()`, edit-mode block at lines 2299–2313
**Operation:** MODIFY

The edit-mode pattern-wide marker (line 2299) only fires for
`cell.kind == MENU_CELL_INSTRUMENT`. Add a Scene setting path:

```c
            } else if (cell.kind == MENU_CELL_SCENE_SETTING &&
                       va_searchComplete) {
                uint8_t scene_bit = 0u;
                switch (cell.scene_setting) {
                case MENU_SCENE_SETTING_VOICE_MORPH:
                    scene_bit = 0x01u;
                    break;
                case MENU_SCENE_SETTING_AUDIO_OUT:
                    scene_bit = 0x02u;
                    break;
                case MENU_SCENE_SETTING_FX_SEND_AMOUNT:
                    scene_bit = 0x04u;
                    break;
                default:
                    break;
                }
                if (scene_bit != 0u &&
                    (va_searchSceneMask & scene_bit) != 0u) {
                    int8_t left;
                    for (left = 8; left < 16 &&
                         editDisplayBuffer[0][left] == ' '; left++)
                        ;
                    if (left < 16 && lcd_underlineGlyph(
                            (uint8_t)editDisplayBuffer[0][left],
                            glyph_probe)) {
                        desired_base[0] =
                            (uint8_t)editDisplayBuffer[0][left];
                        marker_row[0] = 0u;
                        marker_col[0] = (uint8_t)left;
                        desired_valid = 0x01u;
                    }
                }
            }
```

---

## Issue 3 — Scene settings must show live values during playback

### Problem

Scene target values (Voice Morph, Audio Out, FX Send) change during
playback via step automation and are never reset on retrigger (D3 — Scene
targets opt out of retrigger restore). The displayed value on the VOICE/mix
sub-page and the PERF page only updates on user interaction (encoder turn,
page switch). During playback, automation can change these values every
step, but the LCD shows stale data until the user next touches a control.

This is specifically a Scene-target problem. Voice descriptor parameters
reset on retrigger and their automation values are transient — the display
showing the "resting" state is correct for those. Scene targets persist
indefinitely, and their display must track the live state.

### Two display surfaces affected

**Surface A — VOICE/mix sub-page (Scene setting cells):**
`menu_cellDisplayValue()` reads directly from `scene_getVoiceMorphAmount()`
etc. (line 3050–3062). The live Scene data IS the source of truth and IS
updated by `seq_applySceneAutomation()` → `preset_morphVoice()` →
`scene_setVoiceMorphAmount()`. The display just needs periodic repainting to
show the current value.

**Surface B — PERF page (flat parameter mirrors):**
`parameter_values[PAR_VOICE1_MORPH + slot]` IS updated by
`preset_morphVoiceScene()` (line 2872 of presetManager.c) for the active
Scene. Audio Out and FX Send do NOT have PERF page mirrors — they are
VOICE/mix sub-page only. So the PERF morph display also just needs periodic
repainting to show the current value.

### Fix

The fix is a lightweight periodic repaint trigger that fires during
playback when the user is viewing a page with Scene-target cells. This
must NOT repaint on every tick (that would thrash the LCD and CGRAM
pipeline). Instead, it should repaint at a rate sufficient for the user
to track automation movement — 4–8 Hz is appropriate (every 125–250 ms).

#### Change R3-A: Add Scene-target live display service

**File:** `Core/Menu/menu.c`
**Location:** near `va_underlineService()` (after line 2407)
**Operation:** ADD — new static function and timer variable

```c
/*
 * Periodic display refresh for Scene-target automation during playback.
 *
 * Inputs: time_sysTick, playback state from seq_isRunning(), and
 * current page identity. Output: triggers menu_repaint() at ~8 Hz
 * while playback is active and the user is viewing a page that
 * displays Scene-target values (VOICE/mix sub-page or PERF page).
 *
 * Why: Scene targets are not reset on retrigger (D3), so their live
 * values change during playback via step automation and persist
 * indefinitely. Unlike voice descriptor parameters (which reset to
 * resting state on retrigger), Scene target display must track the
 * live state or the user has no feedback on what automation is doing.
 *
 * The refresh is gated by playback state to avoid unnecessary LCD
 * traffic when the sequencer is stopped (values cannot be changing).
 * The 125 ms interval (8 Hz) balances visual responsiveness against
 * LCD/CGRAM transaction cost. The repaint reads live Scene data
 * through menu_cellDisplayValue() (mix sub-page) or the
 * parameter_values[] mirrors (PERF), both of which are already
 * updated by the automation drain path.
 *
 * Common callers: menu_voiceAutoService() (called from the per-tick
 * menu service path). Affiliate: seq_applySceneAutomation(),
 * va_underlineService().
 */
#define SCENE_LIVE_REFRESH_INTERVAL_MS  125u

static uint16_t menu_sceneLiveRefreshTick = 0u;

static void menu_sceneLiveRefreshService(void)
{
    if (!seq_isRunning())
        return;

    if ((uint16_t)(time_sysTick - menu_sceneLiveRefreshTick) <
        SCENE_LIVE_REFRESH_INTERVAL_MS)
        return;

    menu_sceneLiveRefreshTick = time_sysTick;

    if (menu_activePage == PERFORMANCE_PAGE) {
        menu_repaint();
        return;
    }

    if (menu_isVoicePage(menu_activePage)) {
        uint8_t activePage =
            (uint8_t)((menuIndex & MASK_PAGE) >> PAGE_SHIFT);
        uint8_t k;
        for (k = 0u; k < 4u; k++) {
            menu_cell_t cell = menu_resolveCell(activePage, k);
            if (cell.kind == MENU_CELL_SCENE_SETTING) {
                menu_repaint();
                return;
            }
        }
    }
}
```

#### Change R3-B: Call the service from the menu tick path

**File:** `Core/Menu/menu.c`
**Location:** `menu_voiceAutoService()` or equivalent per-tick service
function — identify the function that calls `va_scanService()` and
`va_underlineService()`, and add `menu_sceneLiveRefreshService()` after
them.

The service function is called from the foreground tick and is already
gated by basic page checks. Adding the Scene refresh call there ensures
it runs at the same cadence as the other overlay services.

```c
    va_scanService();
    va_underlineService();
    menu_sceneLiveRefreshService();
```

#### Change R3-C: PERF page — no additional data path change needed

`preset_morphVoiceScene()` already writes
`parameter_values[PAR_VOICE1_MORPH + slot]` at line 2872 of
presetManager.c for the active Scene. The PERF display reads from
`parameter_values[]` at repaint time. The periodic repaint from R3-A
is sufficient to make PERF morph values track the live state.

Audio Out and FX Send do not have PERF page cells — they are mix sub-page
only. No PERF mirror update is needed for them.

#### Change R3-D: Rate-limit during edit mode

The 8 Hz repaint must not interfere with active edit-mode interaction
(encoder detents mid-turn). Add a guard:

```c
static void menu_sceneLiveRefreshService(void)
{
    if (!seq_isRunning())
        return;
    /* Don't repaint while the user is actively editing — their detents
     * already trigger repaints, and a background repaint mid-edit could
     * overwrite working-value display with the live Scene value before
     * the commit lands. */
    if (editModeActive && menu_isVoicePage(menu_activePage))
        return;
    ...
```

For the PERF page, `editModeActive` is also set during knob interaction,
so the same guard applies.

### Cost analysis

- One `uint16_t` timer variable (2 bytes SRAM).
- One timestamp comparison per foreground tick (~4 cycles).
- One `menu_repaint()` call at 8 Hz during playback on relevant pages.
  `menu_repaint()` is the existing full-frame LCD renderer — it already
  runs on every encoder detent, page switch, and overlay event. 8 Hz is
  well within the LCD's refresh budget (the existing `lcd_writeData()`
  SPI path runs at ~200 kHz, and a full 2×16 frame is ~40 µs).
- The per-tick scan of 4 cells to detect MENU_CELL_SCENE_SETTING presence
  is 4 function calls with early-out — negligible.

### 2026-09-23 — Issues 2 and 3 implemented and verified

- Issue 2 is implemented in `Core/Menu/menu.c`: `va_scanService()` now
  recognizes per-voice Scene target descriptors for Voice Morph, Audio Out,
  and FX Send; `va_applyVoiceMarkers()` consumes the result in both compact and
  edit-mode name-marker paths. Global Scene targets and non-automatable Fader
  Setting are intentionally excluded from per-voice underlines.
- Issue 3 is implemented in `menu_sceneLiveRefreshService()`, called from
  `menu_serviceRuntimeWidgets()`. During playback it repaints visible PERF
  Morph cells and visible VOICE/mix Scene-setting cells at 8 Hz, while
  respecting screensaver and edit-mode ownership of the LCD.
- Adjacent source documentation records the +1-byte Scene search mask, +2-byte
  refresh timestamp, and the updated 45-byte overlay static assertion. The
  public runtime-service declaration in `menu.h` has a matching contract block;
  no new public API was introduced.
- `make all -j2` passed with final ELF sizes `text=455548`, `data=416`, and
  `bss=291812`. `make img` passed with a 455964-byte firmware payload and a
  455980-byte LXRV2 image. `git diff --check` passed.
- Remaining diagnostics are existing project warnings: packed-member access in
  PatternData, unused legacy filesystem helpers, nano-libc syscall stubs, and
  serial LTO. No changed Menu source warning was emitted.
