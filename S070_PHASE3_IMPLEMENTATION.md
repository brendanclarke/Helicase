# S070 Phase 3 — Implementation Schedule

Source: `S070_PHASE3_FEATURE_ADDITIONS.md`. Commit baseline: `98dee1a` on
`dev-ph5-effects`. This document remains the complete change-by-change
implementation guide and now also records the implementation/verification
notes below.

## Session implementation notes

### 2026-09-23 — implementation started

- Read `MEMORY.md`, `S070_PHASE3_FEATURE_ADDITIONS.md`, and the Phase 3
  source/review context. The worktree was clean at the Session 070 baseline.
- Confirmed the probability defect in `seq_advanceTrackStep()`: automation
  was queued after the active-step/probability branch.
- Confirmed Scene automation IDs 384..391 already have velocity/LFO table
  infrastructure, while the pending-automation drain intentionally drops
  them. IDs 392..403 and the automation-use flag are still absent.
- Resolved an implementation boundary in D17: `INSTRUMENT_PARAM_INVALID`
  (`0xffff`) cannot be packed into the Pattern pool's 9-bit target field.
  The implementation will use a documented 9-bit Pattern-only off sentinel,
  preserve it through the service/data owner, and make playback skip it.
- The LED change adds the plan-approved 41-byte SRAM1 active-layer bitmap;
  no additional LED state allocation is planned.

### 2026-09-23 — implementation completed and verified

- 3.1 is implemented in `seq_advanceTrackStep()`: one `should_play` decision
  now gates both voice triggering and step-automation queueing; erase remains
  independent. Playback explicitly skips the Pattern-only off sentinel.
- 3.2 is implemented across Scene target metadata, Sequencer drain dispatch,
  the `1vm`–`6vm` Morph conversion path, the `Nvm` VOICE/mix cell, the VOI
  `1..6`/`scn`/`fx` editor categories, and held-step overlay target mapping.
  Scene targets use the approved no-retrigger-restore model. AUDIO_OUT IDs
  392..397 apply through the Preset route setter; FX_SEND IDs 398..403 retain
  through the Phase 5 stub setter.
- D17 category transitions persist `PAT_AUTOMATION_TARGET_OFF` with value zero
  because the packed Pattern target is nine bits; selecting a PAR target reads
  its current Scene/descriptor value and stores it in the appropriate 7-bit
  domain. The sentinel is never queued to runtime playback.
- 3.3 is implemented with the approved 41-byte per-LED layer bitmap, including
  BAR1 at index 40. Pulse, flash, blink, and chase start/end paths maintain the
  bitmap; rendering priority is pulse > flash > blink/chase > base. `led_clearAll()`
  clears both the bitmap and effect ownership so later ticks cannot resurrect a
  cancelled layer. The final map confirms `led_activeLayers` is exactly `0x29`
  (41) bytes; the D17 category tracker is one additional byte of transient
  Menu state. Public LED APIs remain unchanged.
- Verification: `make all -j2` and `make img` both pass. Final ELF size is
  `text=455060`, `data=416`, `bss=291804`; the generated LXR-V2 image is
  `455492` bytes including its 16-byte wrapper (`455476`-byte payload). The
  remaining linker messages are the existing nano-libc syscall and serial-LTO
  warnings; no new compiler warning was emitted by the changed files. Hardware
  interaction verification remains Phase 4 work.

Each change is cited as `file:line` with an operation (ADD / MODIFY / REMOVE)
and a comment-block description that serves as documentation-in-place for
the finished code.

---

## Item 3.1 — Probability must gate the complete step

One file, one function, one restructuring.

### Change 3.1-A: Move automation queueing inside the step-active block

**File:** `Core/Sequencer/sequencer.c`
**Location:** `seq_advanceTrackStep()`, lines 558–586
**Operation:** MODIFY

**Current structure (lines 557–586):**
```
557:  if (!(seq_mutedTracks & (1u << track))) {
558:    if (pat_isStepActive(...)) {
559:      if (seq_eraseActive ...) {
             // erase path
569:      } else {
570:          pat_step_specials_t sp = ...
573:          uint8_t should_trigger = 1u;
575:          if (sp.probability < 127u) { ... should_trigger = 0u; }
581:          if (should_trigger)
582:              seq_triggerVoice(track, sp.velocity, sp.note);
583:      }
584:    }
585:    if (!seq_eraseActive || track != menu_getActiveVoice())
586:        seq_queueStepAutomations(track, (uint8_t)seq_stepIndex[track]);
587:  }
```

**New structure:**
```
557:  if (!(seq_mutedTracks & (1u << track))) {
558:    if (pat_isStepActive(...)) {
559:      if (seq_eraseActive ...) {
             // erase path — unchanged
569:      } else {
570:          pat_step_specials_t sp = ...
573:          uint8_t should_play = 1u;          // RENAMED from should_trigger
575:          if (sp.probability < 127u) { ... should_play = 0u; }
581:          if (should_play) {
582:              seq_triggerVoice(track, sp.velocity, sp.note);
+               seq_queueStepAutomations(track,
+                   (uint8_t)seq_stepIndex[track]);
+           }
583:      }
584:    }
-585:    if (!seq_eraseActive || track != menu_getActiveVoice())
-586:        seq_queueStepAutomations(track, (uint8_t)seq_stepIndex[track]);
587:  }
```

**Comment block (replace existing function-level comment at lines 530–533):**
```
/*
 * Advance one track's sequencer step and fire trigger+automation.
 *
 * Inputs: track index 0..6 from TIM3 ISR tick at priority 2 (4 kHz). Output:
 * probability gates both seq_triggerVoice() and seq_queueStepAutomations() via
 * a single should_play decision computed once per step visit. A step whose
 * probability roll fails produces no trigger and no automation entries — under
 * the hold model (SCOPING_TARGETS §4.3a), previously held values persist
 * through the skipped step exactly as if it were absent. Erase is independent
 * of probability: it clears the step data regardless of what the roll would
 * have decided.
 *
 * Affiliates: PatternData (region ownership, step trigger/automation data),
 * seq_drainPendingAutomation() (foreground consumer of queued automation).
 */
```

**Specific line-level changes:**

1. **Line 573:** rename `should_trigger` → `should_play`.
2. **Lines 581–582:** widen the `if (should_play)` block to include the
   automation queueing call.
3. **Lines 585–586:** REMOVE — the standalone `seq_queueStepAutomations()`
   call that runs outside the step-active block and outside probability
   gating. The erase guard (`!seq_eraseActive`) is now implicit because the
   erase path is the `if` branch and the play path is the `else` branch.
4. **Line 580:** no change to the probability draw itself.

---

## Item 3.2 — Complete automation target runtime ownership

### Sub-item 3.2f: Scene target table expansion

#### Change 3.2f-A: Add AUDIO_OUT and FX_SEND kind enums

**File:** `Core/Bank/Scene/SceneModTargets.h`
**Location:** `scene_mod_target_kind_t` enum, lines 9–23
**Operation:** MODIFY — insert two new values before `EFFECT_PARAMETER`

**Current:**
```c
typedef enum {
    SCENE_MOD_TARGET_KIND_VOICE_MORPH = 0,
    SCENE_MOD_TARGET_KIND_DECIMATION_ALL,
    SCENE_MOD_TARGET_KIND_SLOT6_TRACK7_AMP_DECAY,
    SCENE_MOD_TARGET_KIND_EFFECT_PARAMETER
} scene_mod_target_kind_t;
```

**New:**
```c
typedef enum {
    SCENE_MOD_TARGET_KIND_VOICE_MORPH = 0,
    SCENE_MOD_TARGET_KIND_DECIMATION_ALL,
    SCENE_MOD_TARGET_KIND_SLOT6_TRACK7_AMP_DECAY,
    /*
     * Per-voice audio output route.
     *
     * Inputs: voice_slot selects the instrument, value is a MIXER_ROUTING_*
     * enum (0..5). Output: the Scene retains the route and active-Scene
     * runtime routing is refreshed immediately through
     * preset_setVoiceAudioOut(). This is a discrete selector, not a
     * continuous parameter — velocity/LFO modulation is not applicable.
     * Affiliate: preset_applyKitAudioRouting().
     */
    SCENE_MOD_TARGET_KIND_AUDIO_OUT,
    /*
     * Per-voice FX send amount (retained only until Phase 5).
     *
     * Inputs: voice_slot selects the instrument, value is 0..127. Output:
     * the Scene retains the send amount through
     * preset_setVoiceFxSendAmount(); no audible runtime effect exists until
     * the Phase 5 FX bus attaches its runtime write to the Preset setter.
     * Step automation stores and drains correctly — the retained value will
     * become audible without automation code changes once the FX bus exists.
     * Affiliate: presetManager.c:1032 "eventual FX bus" comment.
     */
    SCENE_MOD_TARGET_KIND_FX_SEND,
    SCENE_MOD_TARGET_KIND_EFFECT_PARAMETER
} scene_mod_target_kind_t;
```

#### Change 3.2f-B: Add SCENE_MOD_TARGET_USE_AUTOMATION flag

**File:** `Core/Bank/Scene/SceneModTargets.h`
**Location:** `scene_mod_target_use_t` enum, lines 25–28
**Operation:** MODIFY — add a third flag

**Current:**
```c
typedef enum {
    SCENE_MOD_TARGET_USE_VELOCITY = 1u << 0,
    SCENE_MOD_TARGET_USE_LFO      = 1u << 1
} scene_mod_target_use_t;
```

**New:**
```c
typedef enum {
    SCENE_MOD_TARGET_USE_VELOCITY   = 1u << 0,
    SCENE_MOD_TARGET_USE_LFO        = 1u << 1,
    /*
     * Step automation eligibility flag.
     *
     * Inputs: checked by the step-edit PAR field cycling path when VOI
     * shows 'scn'. Output: targets without this flag are skipped during
     * PAR cycling but can still be addressed by other modulation sources.
     * All current Scene targets carry this flag; future entries may not.
     * Affiliate: menu_stepAutomationEdit() field 2 Scene-target walk.
     */
    SCENE_MOD_TARGET_USE_AUTOMATION = 1u << 2
} scene_mod_target_use_t;
```

#### Change 3.2f-C: Add 12 entries to scene_mod_targets[] table

**File:** `Core/Bank/Scene/SceneModTargets.c`
**Location:** `scene_mod_targets[]` array, after line 44 (after the `7dc` entry)
**Operation:** ADD — 12 new entries; also MODIFY existing 8 entries to add
the `SCENE_MOD_TARGET_USE_AUTOMATION` flag to their `use_flags` field.

**Existing entries (modify use_flags only):** each of the 8 existing entries
gains `| SCENE_MOD_TARGET_USE_AUTOMATION` on its `use_flags` field. Example
for `1vm` (line 22):
```c
/* Before: */ SCENE_MOD_TARGET_USE_VELOCITY | SCENE_MOD_TARGET_USE_LFO,
/* After:  */ SCENE_MOD_TARGET_USE_VELOCITY | SCENE_MOD_TARGET_USE_LFO
              | SCENE_MOD_TARGET_USE_AUTOMATION,
```

**New entries after `7dc` (insert before closing `};`):**

```c
    /*
     * Per-voice audio output route targets (IDs 392..397).
     *
     * Inputs: step automation stores a 7-bit value; runtime clamps to
     * MIXER_ROUTING_DAC2_R (5). Output: preset_setVoiceAudioOut() retains
     * the route and refreshes active-Scene runtime routing immediately.
     * These targets carry no velocity/LFO flags because a discrete 6-value
     * routing selector cannot be meaningfully shaped by continuous
     * modulation sources. Affiliate: seq_applySceneAutomation().
     */
    { SCENE_MOD_TARGET_ID(8u),  SCENE_MOD_TARGET_KIND_AUDIO_OUT, 0u,
      0u, 5u, SCENE_MOD_TARGET_USE_AUTOMATION,
      "Voice", "1 AudOut", "1ou" },
    { SCENE_MOD_TARGET_ID(9u),  SCENE_MOD_TARGET_KIND_AUDIO_OUT, 1u,
      0u, 5u, SCENE_MOD_TARGET_USE_AUTOMATION,
      "Voice", "2 AudOut", "2ou" },
    { SCENE_MOD_TARGET_ID(10u), SCENE_MOD_TARGET_KIND_AUDIO_OUT, 2u,
      0u, 5u, SCENE_MOD_TARGET_USE_AUTOMATION,
      "Voice", "3 AudOut", "3ou" },
    { SCENE_MOD_TARGET_ID(11u), SCENE_MOD_TARGET_KIND_AUDIO_OUT, 3u,
      0u, 5u, SCENE_MOD_TARGET_USE_AUTOMATION,
      "Voice", "4 AudOut", "4ou" },
    { SCENE_MOD_TARGET_ID(12u), SCENE_MOD_TARGET_KIND_AUDIO_OUT, 4u,
      0u, 5u, SCENE_MOD_TARGET_USE_AUTOMATION,
      "Voice", "5 AudOut", "5ou" },
    { SCENE_MOD_TARGET_ID(13u), SCENE_MOD_TARGET_KIND_AUDIO_OUT, 5u,
      0u, 5u, SCENE_MOD_TARGET_USE_AUTOMATION,
      "Voice", "6 AudOut", "6ou" },
    /*
     * Per-voice FX send amount targets (IDs 398..403).
     *
     * Inputs: step automation stores a 7-bit value 0..127 (identity
     * mapping). Output: preset_setVoiceFxSendAmount() retains the value;
     * no audible runtime effect exists until Phase 5 FX bus. These targets
     * carry no velocity/LFO flags until the runtime path is implemented.
     * Affiliate: seq_applySceneAutomation().
     */
    { SCENE_MOD_TARGET_ID(14u), SCENE_MOD_TARGET_KIND_FX_SEND, 0u,
      0u, 127u, SCENE_MOD_TARGET_USE_AUTOMATION,
      "Voice", "1 FxSend", "1fx" },
    { SCENE_MOD_TARGET_ID(15u), SCENE_MOD_TARGET_KIND_FX_SEND, 1u,
      0u, 127u, SCENE_MOD_TARGET_USE_AUTOMATION,
      "Voice", "2 FxSend", "2fx" },
    { SCENE_MOD_TARGET_ID(16u), SCENE_MOD_TARGET_KIND_FX_SEND, 2u,
      0u, 127u, SCENE_MOD_TARGET_USE_AUTOMATION,
      "Voice", "3 FxSend", "3fx" },
    { SCENE_MOD_TARGET_ID(17u), SCENE_MOD_TARGET_KIND_FX_SEND, 3u,
      0u, 127u, SCENE_MOD_TARGET_USE_AUTOMATION,
      "Voice", "4 FxSend", "4fx" },
    { SCENE_MOD_TARGET_ID(18u), SCENE_MOD_TARGET_KIND_FX_SEND, 4u,
      0u, 127u, SCENE_MOD_TARGET_USE_AUTOMATION,
      "Voice", "5 FxSend", "5fx" },
    { SCENE_MOD_TARGET_ID(19u), SCENE_MOD_TARGET_KIND_FX_SEND, 5u,
      0u, 127u, SCENE_MOD_TARGET_USE_AUTOMATION,
      "Voice", "6 FxSend", "6fx" },
```

**ID mapping summary:**

| Index | ID  | Kind | Short |
|-------|-----|------|-------|
| 0–5   | 384–389 | VOICE_MORPH | `1vm`–`6vm` |
| 6     | 390 | DECIMATION_ALL | `srt` |
| 7     | 391 | SLOT6_TRACK7_AMP_DECAY | `7dc` |
| 8–13  | 392–397 | AUDIO_OUT | `1ou`–`6ou` |
| 14–19 | 398–403 | FX_SEND | `1fx`–`6fx` |

---

### Sub-item 3.2a: Scene automation drain loop

#### Change 3.2a-A: Add `seq_applySceneAutomation()` static function

**File:** `Core/Sequencer/sequencer.c`
**Location:** BEFORE `seq_drainPendingAutomation()` (insert before line 609)
**Operation:** ADD — new static function

```c
/*
 * Apply one Scene-target automation value from the pending queue.
 *
 * Inputs: canonical Scene target ID (384+) and 7-bit stored value from the
 * automation pool. Output: the Scene target's runtime owner is called with
 * the appropriate value conversion applied. Voice Morph targets expand the
 * stored 7-bit value to the full 0..255 Morph domain (stored 0..126 → applied
 * 0..252; stored 127 → applied 255) so the Morph endpoint is reachable. All
 * other Scene targets use identity mapping (stored = applied).
 *
 * Scene targets do not participate in seq_automation_dirty[] or
 * seq_restoreAutomatedParameters() — they follow the Scene-level reset model:
 * no retrigger reset, reset on Scene change / parameter edit / re-automation.
 *
 * Common callers: seq_drainPendingAutomation() for targets that pass
 * sceneModTarget_isSceneTarget(). Affiliates: preset_morphVoice(),
 * preset_setVoiceDecimationAll(), preset_setSlot6Track7AmpEnvelopeDecay(),
 * preset_setVoiceAudioOut(), preset_setVoiceFxSendAmount().
 */
static uint8_t seq_applySceneAutomation(uint16_t target, uint8_t value)
{
    const scene_mod_target_descriptor_t *descriptor =
        sceneModTarget_descriptor(target);

    if (!descriptor)
        return 0u;

    if (value > descriptor->max_value)
        value = descriptor->max_value;

    switch (descriptor->kind) {
    case SCENE_MOD_TARGET_KIND_VOICE_MORPH:
        /* 7-bit-to-8-bit expansion: 0..126 → 0..252, 127 → 255. */
        preset_morphVoice(descriptor->voice_slot,
                          (value < 127u) ? (uint8_t)(value * 2u) : 255u);
        return 1u;
    case SCENE_MOD_TARGET_KIND_DECIMATION_ALL:
        preset_setVoiceDecimationAll(scene_getActiveIndex(), value);
        return 1u;
    case SCENE_MOD_TARGET_KIND_SLOT6_TRACK7_AMP_DECAY:
        preset_setSlot6Track7AmpEnvelopeDecay(
            scene_getActiveIndex(), 0u, value, 0u);
        return 1u;
    case SCENE_MOD_TARGET_KIND_AUDIO_OUT:
        preset_setVoiceAudioOut(scene_getActiveIndex(),
                                descriptor->voice_slot, value);
        return 1u;
    case SCENE_MOD_TARGET_KIND_FX_SEND:
        preset_setVoiceFxSendAmount(scene_getActiveIndex(),
                                    descriptor->voice_slot, value);
        return 1u;
    default:
        return 0u;
    }
}
```

#### Change 3.2a-B: Modify the drain loop condition

**File:** `Core/Sequencer/sequencer.c`
**Location:** `seq_drainPendingAutomation()`, lines 630–648
**Operation:** MODIFY — add Scene target branch after the voice descriptor
branch

**Current (lines 630–648):**
```c
            if ((identity & SEQ_PENDING_TYPE_AUTOMATION_BIT) != 0u &&
                instrumentParam_isVoiceParameter(target) &&
                instrumentManager_targetValid(seq_activePattern, target,
                                              INSTRUMENT_TARGET_AUTOMATION)) {
                // ... voice descriptor path ...
            }
            i++;
```

**New:**
```c
            if ((identity & SEQ_PENDING_TYPE_AUTOMATION_BIT) != 0u) {
                if (instrumentParam_isVoiceParameter(target) &&
                    instrumentManager_targetValid(seq_activePattern, target,
                                                  INSTRUMENT_TARGET_AUTOMATION)) {
                    // ... existing voice descriptor path, unchanged ...
                } else if (sceneModTarget_isSceneTarget(target)) {
                    seq_applySceneAutomation(target, value);
                }
            }
            i++;
```

**Comment block update for `seq_drainPendingAutomation()` (lines 598–608):**
Replace existing function-level comment:
```
/*
 * Apply queued step automation after front-panel service.
 *
 * Inputs: the volatile four-byte queue published by TIM3. Output: valid voice
 * descriptor targets update their owning runtime image through
 * InstrumentManager; Scene targets are dispatched through
 * seq_applySceneAutomation() to their runtime owners (preset_morphVoice,
 * preset_setVoiceDecimationAll, preset_setSlot6Track7AmpEnvelopeDecay,
 * preset_setVoiceAudioOut, preset_setVoiceFxSendAmount). Scene targets do not
 * participate in seq_automation_dirty[] retrigger restore. The foreground
 * follows the live producer count and atomically resets only after no append
 * raced the drain, while PatternTrace remains independent of playback.
 * Affiliate: main.c's pre-audio foreground sequence.
 */
```

#### Change 3.2a-C: Add `#include "SceneModTargets.h"` if not present

**File:** `Core/Sequencer/sequencer.c`
**Location:** include block (top of file)
**Operation:** ADD if not already included (check transitive inclusion)

Check: `sequencer.c` includes `InstrumentManager.h` which includes
`SceneModTargets.h` — already present transitively. Verify at implementation
time; add explicit include only if the transitive chain is broken.

---

### Sub-item 3.2b: Voice Morph value conversion

The 7-bit-to-8-bit expansion is already handled inside `seq_applySceneAutomation()`
(Change 3.2a-A). The inverse (parameter→stored) conversion applies in three
menu-side locations where Voice Morph values are stored to the automation pool.

#### Change 3.2b-A: Add `menu_morphAutomationStore()` helper

**File:** `Core/Menu/menu.c`
**Location:** before `menu_stepAutomationEdit()` (insert around line 8345)
**Operation:** ADD — new static function

```c
/*
 * Convert a 0..255 Voice Morph parameter value to 7-bit automation storage.
 *
 * Inputs: full-range Morph parameter value. Output: 0..126 for values 0..254
 * (integer halving), 127 for value 255 (preserves Morph endpoint). This is the
 * inverse of the expansion in seq_applySceneAutomation(). Common callers:
 * step-edit value encoding and overlay pot-to-automation writes for voice Morph
 * Scene targets. Affiliate: SCOPING_TARGETS §3.3 MIDI CC 7-bit rule.
 */
static uint8_t menu_morphAutomationStore(uint8_t morph_value)
{
    return (morph_value == 255u) ? 127u : (uint8_t)(morph_value / 2u);
}
```

#### Change 3.2b-B: Add `menu_morphAutomationExpand()` helper

**File:** `Core/Menu/menu.c`
**Location:** immediately after `menu_morphAutomationStore()` (above)
**Operation:** ADD — new static function

```c
/*
 * Expand a 7-bit stored Voice Morph automation value to the 0..255 display
 * domain.
 *
 * Inputs: 7-bit stored value from the automation pool. Output: 0..252 for
 * values 0..126 (doubling), 255 for value 127 (Morph endpoint). This is the
 * display inverse of menu_morphAutomationStore(). Common callers: step-edit
 * list value display and step-edit value encoder for Voice Morph Scene targets.
 * Affiliate: seq_applySceneAutomation() uses the same expansion at runtime.
 */
static uint8_t menu_morphAutomationExpand(uint8_t stored)
{
    return (stored >= 127u) ? 255u : (uint8_t)(stored * 2u);
}
```

---

### Sub-item 3.2d: Mix sub-page Voice Morph cell

#### Change 3.2d-A: Increase `MENU_SCENE_SETTING_COUNT`

**File:** `Core/Menu/menu.c`
**Location:** line 78
**Operation:** MODIFY

```c
/* Before: */ #define MENU_SCENE_SETTING_COUNT 3u
/* After:  */ #define MENU_SCENE_SETTING_COUNT 4u
```

#### Change 3.2d-B: Add `MENU_SCENE_SETTING_VOICE_MORPH` to enum

**File:** `Core/Menu/menu.c`
**Location:** `menu_scene_setting_kind_t` enum, lines 1610–1613
**Operation:** MODIFY — add fourth value

```c
/* Before: */
typedef enum {
    MENU_SCENE_SETTING_AUDIO_OUT = 0,
    MENU_SCENE_SETTING_FX_SEND_AMOUNT,
    MENU_SCENE_SETTING_FADER_SETTING
} menu_scene_setting_kind_t;

/* After: */
typedef enum {
    MENU_SCENE_SETTING_AUDIO_OUT = 0,
    MENU_SCENE_SETTING_FX_SEND_AMOUNT,
    MENU_SCENE_SETTING_FADER_SETTING,
    /*
     * Per-voice Morph amount on the VOICE/mix Scene-setting sub-page.
     *
     * Inputs: zero-based instrument slot from the resolved cell. Output:
     * display reads scene_getVoiceMorphAmount(); commit writes through
     * preset_morphVoice(). The automation overlay maps this cell to the
     * corresponding Scene target ID via sceneModTarget_voiceMorphId().
     * Affiliate: MENU_SCENE_SETTING_COUNT must equal the number of entries.
     */
    MENU_SCENE_SETTING_VOICE_MORPH
} menu_scene_setting_kind_t;
```

#### Change 3.2d-C: Extend `menu_sceneSettingShortName()`

**File:** `Core/Menu/menu.c`
**Location:** function at line 2811, switch statement at line 2824
**Operation:** MODIFY — add `MENU_SCENE_SETTING_VOICE_MORPH` case

```c
/* Add before default: */
    case MENU_SCENE_SETTING_VOICE_MORPH:
        dst[1] = 'v';
        dst[2] = 'm';
        break;
```

#### Change 3.2d-D: Extend `menu_cellDtype()`

**File:** `Core/Menu/menu.c`
**Location:** function at line 2857, Scene setting branch at lines 2861–2866
**Operation:** MODIFY — add Voice Morph dtype

**Current:**
```c
    if (cell->kind == MENU_CELL_SCENE_SETTING) {
        if (cell->scene_setting == MENU_SCENE_SETTING_AUDIO_OUT)
            return (uint8_t)((MENU_AUDIO_OUT << 4) | DTYPE_MENU);
        if (cell->scene_setting == MENU_SCENE_SETTING_FADER_SETTING)
            return DTYPE_0B15;
        return DTYPE_0B127;
    }
```

**New:**
```c
    if (cell->kind == MENU_CELL_SCENE_SETTING) {
        if (cell->scene_setting == MENU_SCENE_SETTING_AUDIO_OUT)
            return (uint8_t)((MENU_AUDIO_OUT << 4) | DTYPE_MENU);
        if (cell->scene_setting == MENU_SCENE_SETTING_FADER_SETTING)
            return DTYPE_0B15;
        if (cell->scene_setting == MENU_SCENE_SETTING_VOICE_MORPH)
            return DTYPE_0B255;
        return DTYPE_0B127;
    }
```

#### Change 3.2d-E: Extend `menu_cellDisplayValue()`

**File:** `Core/Menu/menu.c`
**Location:** function at line 2876, Scene setting switch at lines 2917–2926
**Operation:** MODIFY — add Voice Morph case

```c
/* Add before default: */
        case MENU_SCENE_SETTING_VOICE_MORPH:
            return scene_getVoiceMorphAmount(scene_index, cell->slot);
```

#### Change 3.2d-F: Extend `menu_cellCommitValue()`

**File:** `Core/Menu/menu.c`
**Location:** function at line 2933, Scene setting switch at lines 3014–3031
**Operation:** MODIFY — add Voice Morph case

```c
/* Add before default: */
            case MENU_SCENE_SETTING_VOICE_MORPH:
                changed |= preset_morphVoiceScene(scene_index, cell->slot,
                                                   (uint8_t)value);
                break;
```

Note: `preset_morphVoiceScene()` (presetManager.c:2857) takes the Scene index
directly, unlike `preset_morphVoice()` which always uses the active Scene.
The multi-Scene edit mask loop must write to each masked Scene, not just the
active one.

#### Change 3.2d-G: Extend `menu_clampCellValue()`

**File:** `Core/Menu/menu.c`
**Location:** function at line 3668, Scene setting clamp at lines 3674–3691
**Operation:** MODIFY — add Voice Morph clamp

**Current (lines 3682–3690):**
```c
        if (cell->scene_setting == MENU_SCENE_SETTING_AUDIO_OUT) {
            if (*value > 5u)
                *value = 5u;
        } else if (cell->scene_setting == MENU_SCENE_SETTING_FADER_SETTING) {
            if (*value > 2u)
                *value = 2u;
        } else if (*value > 127u) {
            *value = 127u;
        }
```

**New:**
```c
        if (cell->scene_setting == MENU_SCENE_SETTING_AUDIO_OUT) {
            if (*value > 5u)
                *value = 5u;
        } else if (cell->scene_setting == MENU_SCENE_SETTING_FADER_SETTING) {
            if (*value > 2u)
                *value = 2u;
        } else if (cell->scene_setting == MENU_SCENE_SETTING_VOICE_MORPH) {
            if (*value > 255u)
                *value = 255u;
        } else if (*value > 127u) {
            *value = 127u;
        }
```

#### Change 3.2d-H: Extend edit-mode full display for Voice Morph

**File:** `Core/Menu/menu.c`
**Location:** `menu_repaintGeneric()`, Scene setting edit display, lines 8774–8792
**Operation:** MODIFY — add Voice Morph case to the label switch

```c
/* Add before default: at line 8790 */
                case MENU_SCENE_SETTING_VOICE_MORPH:
                    menu_copyPaddedField(&editDisplayBuffer[0][8],
                                         "VcMorph", 8u);
                    break;
```

---

### Sub-item 3.2c: Step-edit list `scn`/`fx` cycling

#### Change 3.2c-A: Rewrite `menu_stepAutomationEdit()` field 1

**File:** `Core/Menu/menu.c`
**Location:** `menu_stepAutomationEdit()`, field 1 block at lines 8367–8398
**Operation:** MODIFY — replace the simple 0–5 slot modulus with a
category-aware stepper

**Current logic (lines 8367–8398):**
- Rejects non-voice targets at line 8375
- Cycles `new_slot` through 0..5 with modular arithmetic
- Calls `instrumentParam_make(new_slot, old_local)` to build a new target
- Falls back to `menu_stepAutomationFirstTarget()` if the new target is invalid

**New logic:**

```c
    if (field == 1u) {
        /*
         * Step-edit VOI (voice/category) field cycling.
         *
         * Inputs: signed encoder movement, current target from the page.
         * Output: category change replaces the target with off/invalid
         * (D17), staying within the same category replaces with the
         * matching-local target on the adjacent slot. Categories cycle:
         * voices 1–6 → scn → fx → wrap. When entering scn or fx, the
         * target is set to off and the user selects a target via the PAR
         * field. Affiliate: sceneModTarget_isSceneTarget(),
         * instrumentParam_isVoiceParameter().
         */
        instrument_param_id_t old_target = autos[page].target;
        uint8_t old_category; /* 0..5 = voice slots, 6 = scn, 7 = fx */
        uint8_t new_category;
        instrument_param_id_t new_target;
        uint8_t movement = (uint8_t)(inc < 0 ? -inc : inc);

        /* Determine current category from old_target. */
        if (instrumentParam_isVoiceParameter(old_target))
            old_category = instrumentParam_slot(old_target);
        else if (sceneModTarget_isSceneTarget(old_target))
            old_category = 6u;
        else
            old_category = 7u; /* fx or unknown */

        new_category = old_category;
        while (movement--) {
            if (inc > 0)
                new_category = (uint8_t)((new_category + 1u) % 8u);
            else
                new_category = (new_category == 0u) ? 7u
                    : (uint8_t)(new_category - 1u);
        }

        if (new_category == old_category)
            return 0u;

        if (new_category < 6u) {
            /* Voice slot: if same local param valid, use it; else off. */
            uint8_t old_local = instrumentParam_isVoiceParameter(old_target)
                ? instrumentParam_local(old_target) : 0u;
            new_target = instrumentParam_make(new_category, old_local);
            if (!instrumentManager_targetValid(scene, new_target,
                                               INSTRUMENT_TARGET_AUTOMATION) ||
                menu_stepAutomationTargetUsed(autos, count, new_target, page))
                new_target = INSTRUMENT_PARAM_INVALID;
        } else {
            /* scn (6) or fx (7): always start at off (D17). */
            new_target = INSTRUMENT_PARAM_INVALID;
        }
        return menu_stepAutomationReplaceTarget(
            scene, track, step, old_target, new_target,
            (new_target == INSTRUMENT_PARAM_INVALID) ? 0u : autos[page].value,
            count);
    }
```

#### Change 3.2c-B: Extend `menu_stepAutomationEdit()` field 2 for Scene targets

**File:** `Core/Menu/menu.c`
**Location:** `menu_stepAutomationEdit()`, field 2 block at lines 8400–8415
**Operation:** MODIFY — handle Scene targets in the PAR cycling path

**Current (lines 8400–8415):**
```c
    if (field == 2u) {
        instrument_param_id_t old_target = autos[page].target;
        instrument_param_id_t new_target;
        uint8_t slot;

        if (!instrumentParam_isVoiceParameter(old_target))
            return 0u;
        slot = instrumentParam_slot(old_target);
        new_target = menu_stepAutomationNextTarget(
            scene, slot, old_target, inc, autos, count, page);
        ...
    }
```

**New:**
```c
    if (field == 2u) {
        /*
         * Step-edit PAR (parameter) field cycling.
         *
         * Inputs: signed encoder movement, current target, current
         * category. Output: walks the voice descriptor table for voice
         * categories, the Scene target table for scn category, or refuses
         * movement for fx (no entries yet). Scene target walking uses
         * sceneModTarget_step() with SCENE_MOD_TARGET_USE_AUTOMATION,
         * skipping targets already used on this step. When the current
         * target is INSTRUMENT_PARAM_INVALID (off), +1 enters the first
         * available target for the category. Affiliate:
         * menu_stepAutomationNextTarget(), sceneModTarget_step().
         */
        instrument_param_id_t old_target = autos[page].target;
        instrument_param_id_t new_target;

        if (old_target == INSTRUMENT_PARAM_INVALID) {
            /* Off → first available for the category stored in field 1. */
            /* Determine category from the compact display state. */
            /* For scn: walk from INVALID +1 in Scene target table. */
            /* For voice: walk from INVALID +1 in descriptor table. */
            /* For fx: refuse (no entries). */
            /* Implementation: examine the display VOI field to determine
             * category, then call the appropriate first-target walk.
             * See implementation note below. */
            return 0u; /* placeholder — see Change 3.2c-D */
        }

        if (instrumentParam_isVoiceParameter(old_target)) {
            uint8_t slot = instrumentParam_slot(old_target);
            new_target = menu_stepAutomationNextTarget(
                scene, slot, old_target, inc, autos, count, page);
        } else if (sceneModTarget_isSceneTarget(old_target)) {
            new_target = old_target;
            uint8_t tries;
            for (tries = 0u; tries < 20u; tries++) {
                instrument_param_id_t candidate = sceneModTarget_step(
                    new_target, inc, SCENE_MOD_TARGET_USE_AUTOMATION);
                if (candidate == new_target ||
                    candidate == INSTRUMENT_PARAM_INVALID)
                    break;
                if (!menu_stepAutomationTargetUsed(autos, count,
                                                   candidate, page)) {
                    new_target = candidate;
                    break;
                }
                new_target = candidate;
            }
        } else {
            return 0u; /* fx — no entries yet */
        }

        if (new_target == old_target)
            return 0u;
        return menu_stepAutomationReplaceTarget(
            scene, track, step, old_target, new_target, autos[page].value,
            count);
    }
```

#### Change 3.2c-C: Extend `menu_stepAutomationEdit()` field 3 for Scene targets

**File:** `Core/Menu/menu.c`
**Location:** `menu_stepAutomationEdit()`, field 3 block at lines 8417–8443
**Operation:** MODIFY — handle Scene target value editing with Morph conversion

**Current (lines 8417–8443):** only resolves descriptor for voice parameters.

**New — add Scene target handling after the voice descriptor resolution:**
```c
    if (field == 3u) {
        instrument_param_id_t vt = autos[page].target;
        const ParamDescriptor *desc = 0;
        uint8_t max_val = 127u;
        int16_t next;
        uint8_t is_morph = 0u;

        if (instrumentParam_isVoiceParameter(vt) &&
            instrumentManager_targetValid(scene, vt,
                                          INSTRUMENT_TARGET_AUTOMATION)) {
            uint8_t s = instrumentParam_slot(vt);
            const kit_instrument_slot_t *inst =
                scene_instrumentSlotConst(scene, s);
            if (inst)
                desc = instrumentManager_descriptor(
                    inst->type, instrumentParam_local(vt));
        } else if (sceneModTarget_isSceneTarget(vt)) {
            const scene_mod_target_descriptor_t *scene_desc =
                sceneModTarget_descriptor(vt);
            if (scene_desc) {
                max_val = (uint8_t)scene_desc->max_value;
                is_morph = (uint8_t)(scene_desc->kind ==
                    SCENE_MOD_TARGET_KIND_VOICE_MORPH);
                if (is_morph)
                    max_val = 127u; /* stored domain is 0..127 */
            }
        }
        if (desc)
            max_val = menu_automationValueMax(desc);
        next = (int16_t)autos[page].value + inc;
        if (next < 0)
            next = 0;
        if (next > (int16_t)max_val)
            next = (int16_t)max_val;
        if ((uint8_t)next == autos[page].value)
            return 0u;
        return patSvc_writeStepAutomation(scene, track, step, vt,
                                          (uint8_t)next);
    }
```

#### Change 3.2c-D: Add category state tracking for off→target resolution

**File:** `Core/Menu/menu.c`
**Location:** near `menu_stepAutoPageIndex` static variables (around line 1190)
**Operation:** ADD — new static variable

```c
/*
 * Tracks the current VOI category in the step-edit automation list.
 *
 * Inputs: set by menu_stepAutomationEdit() field 1 on category change.
 * Output: read by field 2 when the target is INSTRUMENT_PARAM_INVALID (off)
 * to determine which table to walk when the user first selects a target.
 * Values: 0..5 = voice slots, 6 = scn, 7 = fx. Affiliate:
 * menu_stepAutomationEdit().
 */
static uint8_t menu_stepAutoCategory = 0u;
```

The field 1 rewrite (Change 3.2c-A) must set `menu_stepAutoCategory =
new_category` on each category change. The field 2 off→target resolution
(Change 3.2c-B placeholder) reads it to determine the walk table.

#### Change 3.2c-E: Extend compact-view rendering for Scene/fx categories

**File:** `Core/Menu/menu.c`
**Location:** `menu_repaintStepAutomation()`, compact view lines 8682–8704
**Operation:** MODIFY — the voice field rendering at line 8682–8701 already
handles `sceneModTarget_isSceneTarget()` targets (shows `scn` at line 8683).
Additional handling needed:

1. When target is `INSTRUMENT_PARAM_INVALID` (off), display the category
   name from `menu_stepAutoCategory`:
   - 0–5: display voice number
   - 6: display `scn`
   - 7: display `fx `

2. For Scene target values, the `menu_formatAutomationValue3()` call at
   line 8703 passes `descriptor = 0` for Scene targets — this correctly
   falls through to numeric display. For Voice Morph targets, display the
   expanded 0–255 value instead of the stored 0–127 value.

**Specific change for the value display (around line 8703):**
```c
        /* For Scene targets, resolve display value. */
        if (sceneModTarget_isSceneTarget(target)) {
            const scene_mod_target_descriptor_t *scene_desc =
                sceneModTarget_descriptor(target);
            uint8_t display_val = autos[page].value;
            if (scene_desc && scene_desc->kind ==
                    SCENE_MOD_TARGET_KIND_VOICE_MORPH)
                display_val = menu_morphAutomationExpand(display_val);
            numtostrpu(&editDisplayBuffer[1][13], display_val, ' ');
        } else {
            menu_formatAutomationValue3(descriptor, autos[page].value,
                                        &editDisplayBuffer[1][13]);
        }
```

#### Change 3.2c-F: Extend edit-mode detail view for Scene targets

**File:** `Core/Menu/menu.c`
**Location:** `menu_repaintStepAutomation()`, edit-mode detail views at
lines 8536–8638
**Operation:** MODIFY

1. **Voice detail view (cursor == 2, lines 8539–8549):** when target is a
   Scene target, display `scn` instead of a slot number. When target is
   `INSTRUMENT_PARAM_INVALID`, display the category from
   `menu_stepAutoCategory`.

2. **Amount detail view (cursor == 4, lines 8618–8637):** for Scene targets,
   use expanded Morph value for VOICE_MORPH kind, and use Scene target's
   `max_value` for clamping display. The existing code at lines 8625–8636
   only resolves voice descriptors — add an `else if
   (sceneModTarget_isSceneTarget(target))` branch.

#### Change 3.2c-G: Extend `menu_stepAutomationAddDefault()` for D17

**File:** `Core/Menu/menu.c`
**Location:** `menu_stepAutomationAddDefault()`, lines 8280–8308
**Operation:** MODIFY — change default creation to use "off" (D17)

The current function calls `menu_stepAutomationFirstTarget()` to find the
first available descriptor target. Under D17, the default is always "off"
with no target.

**New behavior:**
```c
static uint8_t menu_stepAutomationAddDefault(void)
{
    /*
     * Add the default automation entry for the selected step.
     *
     * Inputs: current viewed Scene, active track. Output: creates an entry
     * with target INSTRUMENT_PARAM_INVALID (off) and value 0. The user then
     * selects a target via the PAR field (D17). This avoids inadvertently
     * changing a parameter if playback is active.
     *
     * Common callers: menu_stepAutomationExecuteItem0() Add action.
     * Affiliate: patSvc_writeStepAutomation().
     */
    uint8_t scene = menu_getViewedPattern();
    uint8_t track = menu_getActiveVoice();

    return patSvc_writeStepAutomation(scene, track,
                                      parameter_values[PAR_ACTIVE_STEP],
                                      INSTRUMENT_PARAM_INVALID, 0u);
}
```

Verify: `patSvc_writeStepAutomation()` must accept `INSTRUMENT_PARAM_INVALID`
as a target. If not, the "off" entry must use a sentinel target value that
the pool storage and drain loop both handle as no-op.

---

### Sub-item 3.2e: Overlay Voice Morph target mapping

#### Change 3.2e-A: Extend `va_writeAutomationFromKnob()` for Scene setting cells

**File:** `Core/Menu/menu.c`
**Location:** `va_writeAutomationFromKnob()`, lines 2375–2430
**Operation:** MODIFY — handle `MENU_CELL_SCENE_SETTING` cells

**Current (lines 2389–2394):**
```c
    cell = menu_resolveCell(activePage, knobNr);
    if (cell.kind != MENU_CELL_INSTRUMENT)
        return;

    target = instrumentParam_make(menu_voicePageToSlot(menu_activePage),
                                  cell.descriptor_index);
```

**New:**
```c
    cell = menu_resolveCell(activePage, knobNr);
    if (cell.kind == MENU_CELL_SCENE_SETTING) {
        /*
         * Resolve Scene-setting cells to their Scene target IDs.
         *
         * Inputs: the resolved cell's scene_setting kind and voice slot.
         * Output: the canonical Scene target ID for automatable settings,
         * or early return for non-automatable settings (fader). Voice Morph
         * uses sceneModTarget_voiceMorphId(); audio_out and fx_send use
         * arithmetic ID lookup from the known table layout (IDs 392+slot,
         * 398+slot). Affiliate: SceneModTargets.c table order.
         */
        uint8_t slot = cell.slot;
        switch (cell.scene_setting) {
        case MENU_SCENE_SETTING_VOICE_MORPH:
            target = sceneModTarget_voiceMorphId(slot);
            break;
        case MENU_SCENE_SETTING_AUDIO_OUT:
            target = (instrument_param_id_t)(INSTRUMENT_VOICE_ID_COUNT + 8u
                                              + slot);
            break;
        case MENU_SCENE_SETTING_FX_SEND_AMOUNT:
            target = (instrument_param_id_t)(INSTRUMENT_VOICE_ID_COUNT + 14u
                                              + slot);
            break;
        default:
            return; /* fader: not automatable */
        }
        if (target == INSTRUMENT_PARAM_INVALID)
            return;
    } else if (cell.kind == MENU_CELL_INSTRUMENT) {
        target = instrumentParam_make(menu_voicePageToSlot(menu_activePage),
                                      cell.descriptor_index);
    } else {
        return;
    }
```

Note on ID computation: `INSTRUMENT_VOICE_ID_COUNT` = 384. Audio_out starts
at index 8 in the table → ID = 384 + 8 = 392. FX send starts at index 14
→ ID = 384 + 14 = 398. These constants could also be resolved via a lookup
function (e.g., `sceneModTarget_audioOutId(slot)`) but the table layout is
stable and documented, so arithmetic is simpler. At implementation time,
consider adding helper functions if the table reorders.

#### Change 3.2e-B: Handle Morph value conversion in the overlay write path

**File:** `Core/Menu/menu.c`
**Location:** `va_writeAutomationFromKnob()`, lines 2415–2417
**Operation:** MODIFY — apply Morph halving before storage

**Current (lines 2415–2417):**
```c
    /* Saturate the parameter-domain value to the 7-bit Pattern storage range;
     * no MIDI-CC-style division is valid for instrument descriptor values. */
    stored7 = (value > 127u) ? 127u : (uint8_t)value;
```

**New:**
```c
    if (cell.kind == MENU_CELL_SCENE_SETTING &&
        cell.scene_setting == MENU_SCENE_SETTING_VOICE_MORPH) {
        stored7 = menu_morphAutomationStore((uint8_t)value);
    } else {
        stored7 = (value > 127u) ? 127u : (uint8_t)value;
    }
```

#### Change 3.2e-C: Handle Scene target value seeding in the overlay

**File:** `Core/Menu/menu.c`
**Location:** `va_writeAutomationFromKnob()`, lines 2396–2403
**Operation:** MODIFY — seed from Scene setting value for Scene cells

**Current seed logic (lines 2396–2403):**
```c
    if (va_underlineSuppressed & (uint8_t)(0x10u << knobNr))
        value = (uint16_t)va_workingValue[knobNr];
    else if (va_resolveHeldValue(target, &stored7))
        value = (uint16_t)va_storedToParam(stored7);
    else
        value = menu_cellDisplayValue(&cell);
```

For Scene target cells, `va_storedToParam()` returns the stored 7-bit value
unchanged, but Voice Morph display is 0–255 (the cell's display value).
The seed from `va_resolveHeldValue` needs Morph expansion when the target
is a Morph Scene target:

```c
    if (va_underlineSuppressed & (uint8_t)(0x10u << knobNr))
        value = (uint16_t)va_workingValue[knobNr];
    else if (va_resolveHeldValue(target, &stored7)) {
        if (cell.kind == MENU_CELL_SCENE_SETTING &&
            cell.scene_setting == MENU_SCENE_SETTING_VOICE_MORPH)
            value = (uint16_t)menu_morphAutomationExpand(stored7);
        else
            value = (uint16_t)va_storedToParam(stored7);
    } else
        value = menu_cellDisplayValue(&cell);
```

#### Change 3.2e-D: Extend overlay marker path for Scene setting cells

**File:** `Core/Menu/menu.c`
**Location:** `va_applyVoiceMarkers()`, lines 2208–2276
**Operation:** MODIFY — the edit-mode marker block at lines 2226–2271
currently only handles `MENU_CELL_INSTRUMENT`. Add a parallel block for
`MENU_CELL_SCENE_SETTING`:

After line 2271 (`}` closing the `MENU_CELL_INSTRUMENT` block), add:
```c
        } else if (cell.kind == MENU_CELL_SCENE_SETTING) {
            instrument_param_id_t target;
            uint8_t value7;

            /* Resolve Scene target ID for this cell. */
            switch (cell.scene_setting) {
            case MENU_SCENE_SETTING_VOICE_MORPH:
                target = sceneModTarget_voiceMorphId(cell.slot);
                break;
            case MENU_SCENE_SETTING_AUDIO_OUT:
                target = (instrument_param_id_t)(
                    INSTRUMENT_VOICE_ID_COUNT + 8u + cell.slot);
                break;
            case MENU_SCENE_SETTING_FX_SEND_AMOUNT:
                target = (instrument_param_id_t)(
                    INSTRUMENT_VOICE_ID_COUNT + 14u + cell.slot);
                break;
            default:
                target = INSTRUMENT_PARAM_INVALID;
                break;
            }
            if (target != INSTRUMENT_PARAM_INVALID &&
                va_overlayActive && va_resolveHeldValue(target, &value7)) {
                /* Show held-step value marker for Scene setting. */
                /* ... same marker rendering pattern as MENU_CELL_INSTRUMENT ... */
            }
        }
```

Similarly, the compact-view marker loop at lines 2278–2320 checks
`cell.kind != MENU_CELL_INSTRUMENT` and skips — extend with Scene setting
handling using the same target resolution.

---

## Item 3.3 — LED state consolidation

### Change 3.3-A: Add per-LED active-layer bitmap

**File:** `Core/Hardware/frontPanel/ledHandler.c`
**Location:** after `led_sw43OriginalState` (line 121)
**Operation:** ADD — new static array

```c
/*
 * Per-LED active-layer bitmap for priority-based fallback rendering.
 *
 * Inputs: layer start/end functions set and clear bits. Output:
 * led_renderFromStack() reads this bitmap to determine the highest active
 * layer when any layer expires, ensuring expiry falls back to the next active
 * layer rather than unconditionally restoring base state. Index 0..39 maps to
 * chain LEDs; index 40 maps to LED_BAR1. One byte per LED; bits are layer
 * flags from the LED_LAYER_* constants below.
 *
 * RAM cost: 41 bytes SRAM1 (approved in S070 plan, D11).
 * Common accessors: led_pulseLed(), led_setBlinkLed(), led_flashGroup(),
 * led_setActive_step(), led_clearActive_step(), led_renderFromStack().
 */
#define LED_LAYER_BLINK  (1u << 0)
#define LED_LAYER_CHASE  (1u << 1)
#define LED_LAYER_FLASH  (1u << 2)
#define LED_LAYER_PULSE  (1u << 3)
#define LED_LAYER_COUNT  41u

static uint8_t led_activeLayers[LED_LAYER_COUNT];
```

### Change 3.3-B: Add `led_renderFromStack()` function

**File:** `Core/Hardware/frontPanel/ledHandler.c`
**Location:** after `led_resetToOriginal()` (insert after line 243)
**Operation:** ADD — new static function

```c
/*
 * Re-render one LED from the highest active layer in its priority stack.
 *
 * Inputs: logical LED ID. Output: the physical LED output is set to the state
 * dictated by the highest active layer (pulse > flash > blink/chase > base).
 * When no temporary layer is active, the LED falls back to its base state from
 * led_originalLedState[] or led_sw43OriginalState. This replaces the old
 * led_reset() restore-to-base semantics with a priority-aware fallback.
 *
 * The function reads existing layer membership arrays (led_pulseLedNumber[],
 * led_flashMask[], led_blinkLedNumber[], led_currentStepLed) to determine what
 * state the highest layer wants. Each layer's "desired state" is the inverse of
 * the current base state (toggle semantic) for blink/pulse/chase, or the flash
 * phase's current on/off for flash.
 *
 * Common callers: all expiry and cancellation paths that formerly called
 * led_reset(). Affiliates: led_tickHandler(), led_setBlinkLed(),
 * led_clearAllBlinkLeds(), led_clearActive_step(), led_setActive_step(),
 * led_restoreFlashMask().
 */
static void led_renderFromStack(uint8_t ledNr)
{
    uint8_t physLed = led_toPhysicalNumber(ledNr);
    uint8_t index = (physLed == LED_BAR1) ? 40u : physLed;
    uint8_t layers;

    if (index >= LED_LAYER_COUNT)
        return;
    layers = led_activeLayers[index];

    if (layers & LED_LAYER_PULSE) {
        /* Pulse is active: toggle from base (pulse effect is an inversion). */
        /* Pulse layer is the highest priority — its physical state stands.
         * Do nothing here because the pulse already wrote its temp state
         * and hasn't expired yet. */
        return;
    }
    if (layers & LED_LAYER_FLASH) {
        /* Flash is active: the flash tick handler owns the physical state.
         * Restore to the flash phase's current value. */
        /* Find which flash group owns this LED and apply its current phase. */
        uint8_t g;
        for (g = 0u; g < NUM_OF_FLASHABLE_LEDS; g++) {
            if (led_flashingLeds & (1u << g)) {
                uint8_t bit;
                uint8_t count = led_flashGroupLedCount((LedFlashGroup)g);
                for (bit = 0u; bit < count; bit++) {
                    if (led_flashGroupLed((LedFlashGroup)g, bit) == ledNr) {
                        if (led_flashMask[g] & (uint16_t)(1u << bit)) {
                            led_setValueTemp(
                                (uint8_t)((led_flashPhase[g] & 1u) == 0u),
                                ledNr);
                            return;
                        }
                    }
                }
            }
        }
        /* Flash bit set but LED not found in any group — clear and fall through. */
        led_activeLayers[index] &= (uint8_t)~LED_LAYER_FLASH;
        layers = led_activeLayers[index];
    }
    if (layers & (LED_LAYER_BLINK | LED_LAYER_CHASE)) {
        /* Blink or chase is active: the blink tick / chase setter owns
         * the physical state. The LED should show the blink/chase
         * temporary state. Since blink toggles periodically, the current
         * temp state from the last blink tick is correct — do not
         * overwrite it. Just leave the current physical output as-is;
         * the next blink tick or chase update will refresh it. */
        return;
    }
    /* No temporary layer: restore to base. */
    led_reset(ledNr);
}
```

**Implementation note:** the exact `led_renderFromStack()` body above is a
first-pass design. The key behavioral contract is:
- Pulse active → leave physical state (pulse owns it)
- Flash active → show flash phase state
- Blink/chase active → leave physical state (blink/chase owns it)
- None → restore to base

The challenge is that "restore to flash phase state" requires scanning flash
groups. At implementation time, consider whether a simpler approach works:
just calling `led_reset()` (restore to base) and letting the next tick
handler re-assert the active layer. This creates a single-frame gap (up to
~1ms at 1kHz service rate) but may be acceptable for visual quality. If so,
`led_renderFromStack()` simplifies to:
```c
    if (!(layers & (LED_LAYER_PULSE | LED_LAYER_FLASH |
                    LED_LAYER_BLINK | LED_LAYER_CHASE)))
        led_reset(ledNr);
    /* else: a higher layer is active; it will reassert on its next tick. */
```

This simpler version avoids the flash-group scan entirely. **Decide at
implementation time** based on visual testing.

### Change 3.3-C: Maintain bitmap in `led_pulseLed()`

**File:** `Core/Hardware/frontPanel/ledHandler.c`
**Location:** `led_pulseLed()`, lines 426–441
**Operation:** MODIFY — set `LED_LAYER_PULSE` bit on start

After line 436 (`led_toggleTemp(ledNr);`), add:
```c
            {
                uint8_t index = (physLed == LED_BAR1) ? 40u : physLed;
                if (index < LED_LAYER_COUNT)
                    led_activeLayers[index] |= LED_LAYER_PULSE;
            }
```

### Change 3.3-D: Maintain bitmap in pulse expiry (tick handler)

**File:** `Core/Hardware/frontPanel/ledHandler.c`
**Location:** `led_tickHandler()`, pulse expiry at lines 684–688
**Operation:** MODIFY — clear `LED_LAYER_PULSE` bit and use `led_renderFromStack()`

**Current (lines 684–688):**
```c
    for (i=0;i<NUM_OF_PULSABLE_LEDS;i++) {
        if ((led_pulsingLeds & (1<<i)) && (time_sysTick > led_pulseEndTime[i])) {
            led_pulsingLeds &= (uint8_t)~(1<<i);
            led_reset(led_pulseLedNumber[i]);
        }
    }
```

**New:**
```c
    for (i=0;i<NUM_OF_PULSABLE_LEDS;i++) {
        if ((led_pulsingLeds & (1<<i)) && (time_sysTick > led_pulseEndTime[i])) {
            uint8_t ledNr = led_pulseLedNumber[i];
            uint8_t phys = led_toPhysicalNumber(ledNr);
            uint8_t idx = (phys == LED_BAR1) ? 40u : phys;
            led_pulsingLeds &= (uint8_t)~(1<<i);
            if (idx < LED_LAYER_COUNT)
                led_activeLayers[idx] &= (uint8_t)~LED_LAYER_PULSE;
            led_renderFromStack(ledNr);
        }
    }
```

### Change 3.3-E: Maintain bitmap in `led_setBlinkLed()`

**File:** `Core/Hardware/frontPanel/ledHandler.c`
**Location:** `led_setBlinkLed()`, lines 609–645
**Operation:** MODIFY

On blink start (line 633–634), after `led_toggleTemp(ledNr)`:
```c
                {
                    uint8_t idx = (physLed == LED_BAR1) ? 40u : physLed;
                    if (idx < LED_LAYER_COUNT)
                        led_activeLayers[idx] |= LED_LAYER_BLINK;
                }
```

On blink cancel (line 641–642), replace `led_reset(ledNr)`:
```c
                led_blinkingLeds &= (uint8_t)~(1<<i);
                {
                    uint8_t idx = (physLed == LED_BAR1) ? 40u : physLed;
                    if (idx < LED_LAYER_COUNT)
                        led_activeLayers[idx] &= (uint8_t)~LED_LAYER_BLINK;
                }
                led_renderFromStack(ledNr);
```

### Change 3.3-F: Maintain bitmap in `led_clearAllBlinkLeds()`

**File:** `Core/Hardware/frontPanel/ledHandler.c`
**Location:** lines 658–666
**Operation:** MODIFY — replace `led_reset()` with bitmap clear +
`led_renderFromStack()`

```c
void led_clearAllBlinkLeds(void)
{
    int i;
    for (i=0;i<NUM_OF_BLINKABLE_LEDS;i++) {
        if (led_blinkingLeds & (1<<i)) {
            uint8_t ledNr = led_blinkLedNumber[i];
            uint8_t phys = led_toPhysicalNumber(ledNr);
            uint8_t idx = (phys == LED_BAR1) ? 40u : phys;
            if (idx < LED_LAYER_COUNT)
                led_activeLayers[idx] &= (uint8_t)~LED_LAYER_BLINK;
            led_renderFromStack(ledNr);
            led_blinkingLeds &= (uint8_t)~(1<<i);
        }
    }
}
```

### Change 3.3-G: Maintain bitmap in flash start/expiry

**File:** `Core/Hardware/frontPanel/ledHandler.c`
**Location:** `led_flashGroup()`, lines 528–559
**Operation:** MODIFY

On flash start (after line 559 `led_applyFlashMask(..., 1u)`), set
`LED_LAYER_FLASH` for each LED in the mask:
```c
    {
        uint8_t bit;
        uint8_t fcount = led_flashGroupLedCount(group);
        for (bit = 0u; bit < fcount; bit++) {
            if (mask & (uint16_t)(1u << bit)) {
                uint8_t fledNr = led_flashGroupLed(group, bit);
                if (fledNr != 0xFFu) {
                    uint8_t fphys = led_toPhysicalNumber(fledNr);
                    uint8_t fidx = (fphys == LED_BAR1) ? 40u : fphys;
                    if (fidx < LED_LAYER_COUNT)
                        led_activeLayers[fidx] |= LED_LAYER_FLASH;
                }
            }
        }
    }
```

**Location:** `led_restoreFlashMask()`, lines 515–526
**Operation:** MODIFY — clear `LED_LAYER_FLASH` and use `led_renderFromStack()`

**Current (lines 515–526):**
```c
static void led_restoreFlashMask(LedFlashGroup group, uint16_t mask)
{
    uint8_t bit;
    uint8_t count = led_flashGroupLedCount(group);
    for (bit = 0; bit < count; bit++) {
        if (mask & (uint16_t)(1u << bit)) {
            uint8_t ledNr = led_flashGroupLed(group, bit);
            if (ledNr != 0xFFu)
                led_reset(ledNr);
        }
    }
}
```

**New:**
```c
static void led_restoreFlashMask(LedFlashGroup group, uint16_t mask)
{
    uint8_t bit;
    uint8_t count = led_flashGroupLedCount(group);
    for (bit = 0; bit < count; bit++) {
        if (mask & (uint16_t)(1u << bit)) {
            uint8_t ledNr = led_flashGroupLed(group, bit);
            if (ledNr != 0xFFu) {
                uint8_t phys = led_toPhysicalNumber(ledNr);
                uint8_t idx = (phys == LED_BAR1) ? 40u : phys;
                if (idx < LED_LAYER_COUNT)
                    led_activeLayers[idx] &= (uint8_t)~LED_LAYER_FLASH;
                led_renderFromStack(ledNr);
            }
        }
    }
}
```

Also in `led_flashGroup()` at lines 544–548, when cancelling the previous
flash mask, the call to `led_restoreFlashMask()` already clears the flash
bitmap bits via the modified `led_restoreFlashMask()` above — no additional
change needed there.

### Change 3.3-H: Maintain bitmap in chase LED functions

**File:** `Core/Hardware/frontPanel/ledHandler.c`
**Location:** `led_setActive_step()`, lines 916–926
**Operation:** MODIFY — maintain `LED_LAYER_CHASE` bitmap

**Current (lines 916–926):**
```c
void led_setActive_step(uint8_t stepNr)
{
    uint8_t ledNr = (uint8_t)(LED_STEP1 + (stepNr % NUM_STEPS_PER_BAR));
    if (led_currentStepLed != ledNr) {
        if (led_currentStepLed != 0xFFu) {
            led_reset(led_currentStepLed);
        }
        led_currentStepLed = ledNr;
        led_toggleTemp(ledNr);
    }
}
```

**New:**
```c
void led_setActive_step(uint8_t stepNr)
{
    uint8_t ledNr = (uint8_t)(LED_STEP1 + (stepNr % NUM_STEPS_PER_BAR));
    if (led_currentStepLed != ledNr) {
        if (led_currentStepLed != 0xFFu) {
            uint8_t old_phys = led_toPhysicalNumber(led_currentStepLed);
            if (old_phys < LED_LAYER_COUNT)
                led_activeLayers[old_phys] &= (uint8_t)~LED_LAYER_CHASE;
            led_renderFromStack(led_currentStepLed);
        }
        led_currentStepLed = ledNr;
        {
            uint8_t new_phys = led_toPhysicalNumber(ledNr);
            if (new_phys < LED_LAYER_COUNT)
                led_activeLayers[new_phys] |= LED_LAYER_CHASE;
        }
        led_toggleTemp(ledNr);
    }
}
```

**Location:** `led_clearActive_step()`, lines 935–941
**Operation:** MODIFY

```c
void led_clearActive_step(void)
{
    if (led_currentStepLed != 0xFFu) {
        uint8_t phys = led_toPhysicalNumber(led_currentStepLed);
        if (phys < LED_LAYER_COUNT)
            led_activeLayers[phys] &= (uint8_t)~LED_LAYER_CHASE;
        led_renderFromStack(led_currentStepLed);
        led_currentStepLed = 0xFFu;
    }
}
```

### Change 3.3-I: Clear bitmap in `led_clearAll()` and `led_init()`

**File:** `Core/Hardware/frontPanel/ledHandler.c`
**Location:** `led_clearAll()`, line 402; `led_init()`, line 256
**Operation:** MODIFY — add `memset(led_activeLayers, 0, sizeof(led_activeLayers))`

In `led_clearAll()`, add after the existing clears (after line 408):
```c
    memset(led_activeLayers, 0, sizeof(led_activeLayers));
```

In `led_init()`, add after the existing clears (after line 262):
```c
    memset(led_activeLayers, 0, sizeof(led_activeLayers));
```

### Change 3.3-J: No public API change

**File:** `Core/Hardware/frontPanel/ledHandler.h`
**Operation:** NO CHANGE — `led_renderFromStack()` is static internal to
ledHandler.c. `led_reset()` remains in the public API as the base-state
restore primitive (used by the stack function internally). The public
function signatures are unchanged.

---

## Implementation order

1. **3.1** — one function change in sequencer.c. Build + verify.
2. **3.2f** — SceneModTargets.h/c table expansion. Build + verify (no
   consumers yet; table entries are inert until the drain loop dispatches
   them).
3. **3.2a** — drain loop Scene target branch + `seq_applySceneAutomation()`.
   Build + verify; existing Scene targets now apply at runtime.
4. **3.2b** — Morph conversion helpers in menu.c (inert until consumed by
   3.2c/3.2e).
5. **3.2d** — mix sub-page Voice Morph cell. Build + verify; cell displays
   and edits voice Morph from the VOICE page.
6. **3.2c** — step-edit list `scn`/`fx` cycling + category state. Build +
   verify; Scene targets can now be assigned from the step-edit UI.
7. **3.2e** — overlay pot-to-automation mapping for Scene setting cells.
   Build + verify; held-step overlay writes Scene target automation entries.
8. **3.3** — LED consolidation. Build + verify after all sequencer changes
   are stable.

After each item: `make clean && make && make img`, verify build sizes, and
confirm no new warnings.

---

## Summary of files changed

| File | Changes |
|------|---------|
| `Core/Sequencer/sequencer.c` | 3.1-A, 3.2a-A, 3.2a-B, 3.2a-C |
| `Core/Bank/Scene/SceneModTargets.h` | 3.2f-A, 3.2f-B |
| `Core/Bank/Scene/SceneModTargets.c` | 3.2f-C |
| `Core/Menu/menu.c` | 3.2b-A, 3.2b-B, 3.2c-A–G, 3.2d-A–H, 3.2e-A–D |
| `Core/Hardware/frontPanel/ledHandler.c` | 3.3-A–I |
| `Core/Hardware/frontPanel/ledHandler.h` | 3.3-J (no change) |

Total: 4 files modified, 0 new files created.
