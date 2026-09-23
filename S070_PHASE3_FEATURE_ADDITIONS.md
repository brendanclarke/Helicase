# S070 Phase 3 — Feature Additions Plan

Baseline: commit `98dee1a` on `dev-ph5-effects` (Session 070, 2026-09-23).
Build: text=450,140, data=416, bss=291,756; image 450,572 bytes.
Source: `S070_SYSTEMS_GENERAL_CHECK_AND_REVIEW_PLAN.md` Phase 3 items 3.1–3.3.

## Scope

Three remaining Phase 4 feature-behavior items. Per-track scale/shuffle,
copy/paste, clear, live record, roll overhaul, Patgen/Euklid revert, and
automation hold reconciliation are explicitly deferred — see
`knowledge_files/drafts/PATTERN_TRACK_PROPERTIES_AND_WIDGETS_COMPLETION.md`.

---

## Item 3.1 — Probability must gate the complete step

### Current state

`seq_advanceTrackStep()` (sequencer.c:534) processes each track step in TIM3
ISR context. The flow is:

1. Check mute (line 557).
2. If step is active (line 558), read specials (line 570), draw random value
   against `sp.probability` (lines 575–580), and conditionally call
   `seq_triggerVoice()` (lines 581–582).
3. Outside the step-active block but inside the unmuted block (lines 585–586),
   `seq_queueStepAutomations()` runs unconditionally.

**Defect:** probability gates only the voice trigger. A step whose probability
roll fails still publishes all its automation entries. The drain path
(`seq_drainPendingAutomation()`, sequencer.c:609) applies those entries to
descriptor runtime images, and `seq_automation_dirty` marks them for retrigger
restore. This violates the design from `SCOPING_TARGETS.md` §4.6: probability
should control whether the whole step (trigger + automation) is played.

### Fix

Compute one `should_play` decision per step visit. Gate both
`seq_triggerVoice()` and `seq_queueStepAutomations()` behind it.

#### Implementation detail

```
// Inside seq_advanceTrackStep(), after reading specials:
uint8_t should_play = 1u;

if (sp.probability < 127u) {
    uint8_t rnd = (uint8_t)(((uint16_t)(GetRngValue() & 0x7FFFu) *
                             127u) / 32767u);
    if (rnd >= sp.probability)
        should_play = 0u;
}
if (should_play) {
    seq_triggerVoice(track, sp.velocity, sp.note);
}
// Move automation queueing inside the step-active block, gated by should_play:
if (should_play && !seq_eraseActive)
    seq_queueStepAutomations(track, (uint8_t)seq_stepIndex[track]);
```

The existing `seq_queueStepAutomations()` call at line 585–586 must move from
its current position (outside the step-active block) into the step-active
else-branch, gated by `should_play`. The erase guard remains: live erase
clears steps in TIM3 context, and erased steps must not queue automations.

#### Interaction with the automation hold model

When a step fails probability, it should not apply its automation. Under
the hold model (`SCOPING_TARGETS.md` §4.3a), previously held values continue
to hold through the skipped step — the same behavior as if the step had no
automation at all. This is correct: a probabilistic step that doesn't fire
is treated as absent for that cycle.

**Retrigger restore is unaffected.** `seq_restoreAutomatedParameters()` fires
from the trigger funnel in MidiVoiceControl.c, and is only called when a
trigger actually happens. A probability-skipped step produces no trigger, so
no restore runs. If a prior step's automation is still dirty when the next
active step triggers, the restore runs against the prior step's dirty bits,
which is the existing correct behavior.

#### Erase semantics when probability is active

Erase mode operates independently of probability. When `seq_eraseActive` is
set and the active track matches, the step is cleared (trigger bit + pool
block via `patSvc_enqueueErase()`). Erase is an edit operation on the
Pattern data, not a playback decision — it always acts regardless of what
probability would have done. This is the current behavior and should be
preserved.

### Verification

- Step with probability < 127 and automation entries: confirm automation does
  not apply when probability suppresses the step.
- Step with probability < 127, automation, and hold: confirm held values from
  a prior step persist through the skipped step.
- Step with probability = 127 (always): confirm both trigger and automation
  fire unconditionally (no regression).
- Erase during playback with probability active: confirm erase clears the
  step regardless of the probability roll.
- Track 7 (shares slot 5): confirm probability gating works for the shared
  slot case.

### Files changed

- `Core/Sequencer/sequencer.c`: `seq_advanceTrackStep()` — restructure the
  automation queue call site.

### Risk: zero

This is a one-function restructuring of existing gating logic. No new state,
no new API, no RAM cost. The probability random draw is already computed; the
only change is widening its gate from trigger-only to trigger+automation.

---

## Item 3.2 — Complete automation target runtime ownership

### Current state

`seq_drainPendingAutomation()` (sequencer.c:609) consumes the TIM3-queued
pending automation records in the foreground. Its apply condition at
lines 630–633 requires:

1. `identity & SEQ_PENDING_TYPE_AUTOMATION_BIT` — must be automation
2. `instrumentParam_isVoiceParameter(target)` — target < 384 (voice IDs)
3. `instrumentManager_targetValid(...)` — descriptor validation passes

**Defect:** Scene targets (IDs 384–391) satisfy condition 1 and pass condition
3's validation (`instrumentManager_targetValid` delegates to
`sceneModTarget_isSceneTarget()` for IDs >= 384), but are filtered out by
condition 2. They are silently dropped.

### The eight Scene targets

The Scene target table (`SceneModTargets.c:20–45`) defines eight targets:

| ID  | Kind | Short | Runtime owner |
|-----|------|-------|---------------|
| 384 | VOICE_MORPH | 1vm | `preset_morphVoice(0, value)` |
| 385 | VOICE_MORPH | 2vm | `preset_morphVoice(1, value)` |
| 386 | VOICE_MORPH | 3vm | `preset_morphVoice(2, value)` |
| 387 | VOICE_MORPH | 4vm | `preset_morphVoice(3, value)` |
| 388 | VOICE_MORPH | 5vm | `preset_morphVoice(4, value)` |
| 389 | VOICE_MORPH | 6vm | `preset_morphVoice(5, value)` |
| 390 | DECIMATION_ALL | srt | `preset_setVoiceDecimationAll(active, value)` |
| 391 | SLOT6_TRACK7_AMP_DECAY | 7dc | `preset_setSlot6Track7AmpEnvelopeDecay(active, 0, value, 0)` |

All eight have value range 0–127 and all have `SCENE_MOD_TARGET_USE_VELOCITY
| SCENE_MOD_TARGET_USE_LFO` use flags. The automation pool stores 7-bit
values, which maps directly to these ranges.

This session adds 12 entries for audio_out (IDs 392–397) and fx_send
(IDs 398–403), bringing the table to 20 entries. See FU-1 resolution below
for the full new-entry table.

### Existing runtime apply infrastructure

InstrumentManager.c already has typed dispatchers for Scene targets:

- **Velocity path** (`instrumentManager_applyVelocitySceneTarget()`,
  InstrumentManager.c:2238): dispatches on `descriptor->kind` to
  `preset_morphVoice()`, `preset_setVoiceDecimationAll()`, or
  `preset_setSlot6Track7AmpEnvelopeDecay()`.
- **LFO path** (`instrumentManager_updateLfoSceneDestination()`,
  InstrumentManager.c:2282): dispatches the same way, using base+shape
  modulation and runtime-only overrides where appropriate.

Step automation needs a third apply path. Unlike velocity (which shapes
against normalized 0–1 input) and LFO (which shapes against a base value with
polarity), step automation applies a direct 7-bit value from the pool — the
same identity mapping used for voice descriptor targets (Session 065: stored
value = parameter value).

### Fix

Add a Scene-target automation apply function and wire it into the drain loop.

#### New function: `seq_applySceneAutomation()`

A static function in sequencer.c, called from `seq_drainPendingAutomation()`
for targets that pass `sceneModTarget_isSceneTarget()`.

```c
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
        preset_morphVoice(descriptor->voice_slot, value);
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

#### Modified drain loop

Replace the single `instrumentParam_isVoiceParameter()` gate with a
two-branch dispatch:

```c
if ((identity & SEQ_PENDING_TYPE_AUTOMATION_BIT) != 0u) {
    if (instrumentParam_isVoiceParameter(target) &&
        instrumentManager_targetValid(seq_activePattern, target,
                                      INSTRUMENT_TARGET_AUTOMATION)) {
        // ... existing voice descriptor path (unchanged) ...
    } else if (sceneModTarget_isSceneTarget(target)) {
        seq_applySceneAutomation(target, value);
    }
}
```

### Retrigger restore for Scene targets — DECIDED

Scene targets do NOT participate in `seq_automation_dirty` /
`seq_restoreAutomatedParameters()`. The voice-descriptor retrigger restore
model (restore to `morph_interpolation[]` before the next trigger of the same
voice) does not have a Scene-level analog. All Scene targets follow a single
model:

- **No reset on voice retrigger** — these are Scene-level values, not
  per-voice runtime parameters.
- **Reset on Scene change** — Scene activation replaces the full settings
  image. Automation does not require special storage to survive Scene changes.
- **Reset on parameter edit or re-automation** — a new value from any source
  (knob, menu, or another automation step) replaces the current value.

**Exception — voice 6 Choke (`7dc`):** the generated track-7 decay resets
when voice 6 triggers with its own Choke descriptor's `_choke` decay. This is
the existing Choke trigger behavior and is independent of the step automation
path — `7dc` automation writes the Scene Kit setting, while the Choke trigger
uses its own descriptor decay.

Voice Morph (`1vm`–`6vm`) resets when the global Morph value changes — the
global Morph operation already resets per-voice Morph values. Voice Morph
values also reset on Scene change (the incoming Scene's
`voice_morph_amount[]` replaces the current values). No additional
retrigger-restore machinery is needed.

Document this decision explicitly in the code.

### Track-7 choke/base-decay ownership — DECIDED

Apply regardless of source track. `7dc` lives in the `scn` category and is
available from any track's step-edit automation list. The target-to-runtime
mapping is self-contained; the pending queue does not carry track identity.

### Files changed

- `Core/Sequencer/sequencer.c`: `seq_drainPendingAutomation()` — add Scene
  target branch; add `seq_applySceneAutomation()` static function with
  voice Morph 7-bit-to-8-bit expansion, audio_out apply, and fx_send
  stubbed apply.
- `Core/Bank/Scene/SceneModTargets.h`: add `SCENE_MOD_TARGET_KIND_AUDIO_OUT`
  and `SCENE_MOD_TARGET_KIND_FX_SEND` to `scene_mod_target_kind_t` enum
  (insert before `EFFECT_PARAMETER`).
- `Core/Bank/Scene/SceneModTargets.c`: add 12 entries to
  `scene_mod_targets[]` — 6 audio_out (IDs 392–397), 6 fx_send
  (IDs 398–403).
- `Core/Menu/menu.c`: `menu_stepAutomationEdit()` field 1 — extend voice
  cycling to include `scn` and `fx` categories. Add
  `MENU_SCENE_SETTING_VOICE_MORPH` to `menu_scene_setting_kind_t`, increase
  `MENU_SCENE_SETTING_COUNT` to 4, extend `menu_sceneSettingShortName()`,
  `menu_cellDisplayValue()`, `menu_cellCommitValue()`, `menu_cellDtype()`.
  Extend `menu_voiceAutoOverlayPotTarget()` (or equivalent) for Scene
  target mapping — must handle `MENU_CELL_SCENE_SETTING` cells for all
  four scene settings (`ou`, `fx`, `fd`, `vm`), returning the appropriate
  Scene target ID for automatable cells and `INSTRUMENT_PARAM_INVALID`
  for fader (non-automatable).

### Voice Morph value conversion for automation — DECIDED

Per-voice Morph has a full 0–255 range but the automation pool stores 7-bit
values (0–127). Special conversion applies:

- **Store:** Morph value 0–252 stores as `value / 2` (0–126). Morph values
  253 and 254 also store as 126. Morph value 255 stores as 127.
- **Apply:** Stored 0–126 applies as `value * 2` (0–252). Stored 127 applies
  as 255, so the morph endpoint is reachable.

This matches the MIDI CC 7-bit conversion rule from `SCOPING_TARGETS.md`
§3.3. The `seq_applySceneAutomation()` dispatcher must apply this expansion
for `SCENE_MOD_TARGET_KIND_VOICE_MORPH` targets specifically. Other Scene
targets (`srt`, `7dc`) have max_value 127 and use identity mapping.

The step-edit automation list value display and encoder edit for voice Morph
targets must also use this conversion: display the expanded 0–255 domain,
encode edits back through the halving rule.

### Step-edit automation list: voice cycling with Scene category — NEW WORK

The MODE STEP step-edit list (View A, Session 065) currently cycles the VOI
(voice) field through slots 0–5 only (`menu_stepAutomationEdit()` field 1,
menu.c:8367). Scene targets (IDs 384+) are rejected at line 8375 because
`instrumentParam_isVoiceParameter()` returns false.

**Required extension:** the voice selector must cycle through:
- Voices 1–6 (slots 0–5, existing behavior)
- `scn` — the 8 Scene parameters (IDs 384–391)
- `fx` — future Effect parameters (Phase 5 placeholder, no entries yet)

This gives 8 categories: 6 voice slots, `scn`, and `fx`. Each category has
up to 64 automatable parameters, preserving consistent bit-packing.

When VOI shows `scn`, the PAR (parameter) field cycles through the Scene
target table using `sceneModTarget_step()`. When VOI shows a voice number,
PAR cycles through that slot's descriptor targets using
`instrumentManager_stepTargetForSlot()` (existing behavior).

The `fx` category currently has no entries. Cycling into it should show
`fx` with `---` for the parameter field and refuse to create entries until
Phase 5 populates `SCENE_MOD_TARGET_KIND_EFFECT_PARAMETER` entries.

**Implementation detail:** the VOI field's cycling logic in
`menu_stepAutomationEdit()` must be rewritten from a simple 0–5 slot modulus
to a category stepper: voices 1–6, then `scn`, then `fx`. When stepping
into `scn`, call `menu_stepAutomationFirstTarget()` adapted for Scene targets
(walk the Scene target table instead of the voice descriptor table). The
existing duplicate-target-skipping logic (`menu_stepAutomationTargetUsed()`)
already works with Scene target IDs — it compares `uint16_t target` values
without assuming they are voice parameters.

**Display:** when VOI is `scn`, display `scn` in the voice field (3 chars).
When VOI is `fx`, display `fx `. The parameter short name comes from
`sceneModTarget_formatShort()` for Scene targets.

### Voice Morph parameter lock from VOICE mode — NEW WORK

Voice Morph for the current voice must be parameter-lockable from the
VOICE-page held-step automation overlay (View B, Session 066). This requires
voice Morph to be visible as an editable parameter on the VOICE page.

**Location:** the `mix` sub-page (last SELECT button, subpage 7). Currently
has 3 appended Scene settings per voice:

| Cell | Label | Setting |
|------|-------|---------|
| 0 | `Nou` | `audio_out` — automatable |
| 1 | `Nfx` | `fx_send_amount` — automatable |
| 2 | `Nfd` | `fader_setting` — NOT automatable |

Add a 4th Scene setting cell:

| Cell | Label | Setting |
|------|-------|---------|
| 3 | `Nvm` | `voice_morph_amount` — automatable |

Where `N` is the one-based voice number (e.g., `1vm` for voice 1).

**Changes required:**

1. `MENU_SCENE_SETTING_COUNT`: increase from 3 to 4. This may add a second
   appended screen to the mix sub-page (4 cells per screen; 4 settings fits
   in one screen, so `MENU_SCENE_SETTING_SCREENS` remains 1).
2. Add `MENU_SCENE_SETTING_VOICE_MORPH = 3` to `menu_scene_setting_kind_t`.
3. `menu_sceneSettingShortName()`: add `vm` case.
4. `menu_cellDisplayValue()`: read `scene->settings.voice_morph_amount[slot]`,
   display as 0–255.
5. `menu_cellCommitValue()`: write through `preset_morphVoice(slot, value)`.
6. `menu_cellDtype()`: return `DTYPE_0B255` for voice Morph setting.
7. The held-step overlay's pot-to-automation write path already maps VOICE-page
   cells to target IDs via descriptor lookup. Voice Morph needs to map to the
   corresponding Scene target ID (`sceneModTarget_voiceMorphId(slot)`).

**Automation target mapping for the overlay:** when the user holds a step on
the VOICE page and turns a pot on the `Nvm` cell, the overlay must create or
update an automation entry targeting the corresponding `NvM` Scene target ID
(384–389). The existing overlay voice-descriptor mapping in
`menu_voiceAutoOverlayPotTarget()` must be extended to handle
`MENU_CELL_SCENE_SETTING` cells by returning the Scene target ID from
`sceneModTarget_voiceMorphId()`.

### Verification

- Step automation targeting `1vm` (voice 1 morph): confirm morph value
  changes on step playback with the 7-bit-to-8-bit expansion (stored 63
  → applied 126; stored 127 → applied 255).
- Step automation targeting `srt` (scene decimation): confirm sample rate
  changes audibly.
- Step automation targeting `7dc` (track-7 decay): confirm decay value
  changes for slot-6 instrument; confirm no effect when slot 6 is a Choke
  type (Choke uses its own `_choke` descriptor, not the generated `7dc`).
- Confirm Scene automation does NOT trigger retrigger restore.
- Confirm voice descriptor automation (IDs 0–383) continues to work unchanged.
- Step-edit list: cycle VOI through 1–6, `scn`, `fx`. Confirm `scn` shows
  Scene target short names; confirm `fx` shows `---` and refuses entry
  creation.
- Step-edit list voice Morph: confirm displayed value is 0–255, confirm
  stored pool value is halved.
- VOICE-page mix sub-page: confirm `Nvm` cell displays and edits voice Morph.
- VOICE-page held-step overlay: hold a step, turn the pot on the `Nvm` cell,
  confirm automation entry is created targeting the correct `NvM` Scene
  target.
- Scene change with active voice Morph automation: confirm voice Morph
  resets to the new Scene's value.

### Risk: low–moderate

The drain-loop Scene target dispatch is low risk (mirrors existing velocity/
LFO paths, no new RAM). The step-edit voice cycling extension is moderate
risk — it changes the field 1 cycling logic that touches the automation
target replacement path. The mix sub-page voice Morph cell is low risk
(extends an existing pattern by one entry). The overlay target mapping
extension is moderate risk (the overlay pot-to-target path must correctly
distinguish Scene settings from descriptor cells).

### Open question: EFFECT_PARAMETER kind

`SceneModTargets.h` declares `SCENE_MOD_TARGET_KIND_EFFECT_PARAMETER` (line
22) but no entries of that kind exist in the table yet. This is a Phase 5
placeholder. The dispatcher's `default` case handles it safely (returns 0).
When Phase 5 adds Effect targets to the table, the dispatcher needs a
corresponding case. No action needed now.

---

## Item 3.3 — LED state consolidation

### Current state

`ledHandler.c` (1,303 lines) manages 40 shift-register chain LEDs plus BAR1
(SW43, GPIO PB8). Five overlapping visual layers exist:

| Layer | Mechanism | Restore behavior |
|-------|-----------|-----------------|
| **Base** | `led_setValue()` writes `led_originalLedState[]` | N/A (canonical) |
| **Blink** | Up to 8 persistent togglers, 250 ms cycle | Restores to **base** via `led_reset()` |
| **Flash** | Group-based pattern, 400 ms / 80 ms cycle | Restores to **base** via `led_reset()` |
| **Pulse** | Up to 8 one-shot 50 ms inversions | Restores to **base** via `led_reset()` |
| **Chase** | Single sequencer step indicator | Restores to **base** via `led_reset()` |

**Defect:** every layer restores to base on expiry. There is no priority
stack. When layers overlap on the same LED:

- A pulsed LED that was blinking falls back to base, not to its blink state.
  The next blink tick (up to 250 ms later) re-asserts blink, causing a
  visible gap.
- A flashed LED whose base changed during the flash shows the new base
  correctly (this works by accident because `led_reset()` reads current
  `led_originalLedState[]`).
- Chase and pulse on the same LED: whichever expires first clobbers the other.

### Target priority model

From `SCOPING_TARGETS.md` §4.11:

```
base < blink < flash < pulse
```

Chase is positioned explicitly in the stack (treated as equivalent to blink
priority — it is a persistent step indicator, not a transient event).

On expiry of any layer, re-render the next-highest active layer rather than
blindly falling to base.

### Implementation approach

#### Per-LED active-layer bitmap

Add a per-LED byte (or nibble) tracking which layers are currently active on
that LED. When any layer expires or is cancelled, the LED is re-rendered from
the highest remaining active layer.

**RAM cost:** 41 bytes (40 chain LEDs + BAR1), one byte per LED, using bit
flags for each layer. **Approved** — within the existing `ledHandler.c`
static footprint.

#### Layer state tracking

Each layer already tracks its own membership:

- **Blink**: `led_blinkLeds[]` array, 8 slots with LED number and on/off
  state. Already tracks which LEDs are blinking.
- **Flash**: `led_flashMask[]` per group. Already tracks which LEDs are in
  a flash group.
- **Pulse**: `led_pulseLeds[]` array, 8 slots. Already tracks which LEDs
  are pulsing.
- **Chase**: `led_currentStepLed`. Single LED.

The per-LED bitmap is the missing piece that lets expiry handlers query
"what else is active on this LED?" efficiently.

#### Modified `led_reset()` → `led_renderFromStack()`

Replace `led_reset(ledNr)` (which unconditionally restores base) with a
function that checks the active-layer bitmap and renders the highest active
layer:

```
led_renderFromStack(ledNr):
    if (pulse active on ledNr)  → render pulse state
    if (flash active on ledNr)  → render flash state
    if (blink active on ledNr)  → render blink state
    else                        → render base (led_originalLedState[ledNr])
```

All existing `led_reset()` callers in expiry paths become
`led_renderFromStack()` calls. Layer-start functions set the appropriate bit;
layer-end/cancel functions clear the bit and call `led_renderFromStack()`.

#### Chase positioning — DECIDED

Chase is positioned at blink priority (same level, not above). This ensures
transient states (flash, pulse) are never missed because the chaselight
happens at the same instant. When a blink LED is also the chase LED, blink
and chase alternate based on whichever was set most recently at their shared
priority level. In practice, chase and blink rarely overlap on the same LED
(chase follows the playing step; blink marks editing state).

#### BAR1 handling

BAR1 (`LED_BAR1 = 40`) uses its own shadow variables
(`led_sw43State`/`led_sw43OriginalState`) and routes through
`dout_setSw43Led()`. The same per-LED bitmap approach applies — index 40 in
the active-layer array covers BAR1. BAR1 already has its own flash group
(`LED_FLASH_GROUP_BAR`).

#### Session 066 held-step automation overlay interaction

The overlay writes base state via `led_setValue()` and restores via
`led_updatePatternTrackView()` on exit. This is correct under the priority
model: the overlay changes what "base" means for STEP LEDs while active.
Temporary layers (blink/flash/pulse) on STEP LEDs during the overlay render
on top of the overlay's base state and fall back to it on expiry.

**No special handling needed.** The overlay is not a separate priority layer —
it is a context that changes the base state. This matches its current
implementation and preserves the existing six post-hardware-test fixes from
Session 066.

#### Session 068 double-drain timing

Session 068 added a second `buttonHandler_processEvents()` drain per
main-loop pass. This increased the effective rate of LED state changes. The
consolidation must not introduce visible flicker from the faster drain rate.

**Mitigation:** `led_renderFromStack()` is a pure state function (reads
bitmap + layer state → writes physical output). It produces the same output
regardless of how often it is called. Double-drain timing is irrelevant to
correctness; only the cost of redundant GPIO writes matters, and those are
already single SPI1 shift-register operations.

### Files changed

- `Core/Hardware/frontPanel/ledHandler.c`: add per-LED active-layer bitmap;
  replace `led_reset()` restore logic with `led_renderFromStack()`;
  update layer start/end functions to maintain the bitmap.
- `Core/Hardware/frontPanel/ledHandler.h`: no public API change needed. The
  consolidation is internal to ledHandler.

### Risk: moderate

This is the highest-risk item in Phase 3. Every LED caller in the codebase
depends on the current restore-to-base behavior. The consolidation changes
observable behavior for edge cases:

- A pulsed LED that was blinking now falls back to blink instead of base.
  This is the intended improvement, but callers that relied on the
  pulse-clears-blink side effect (if any exist) would break.
- Flash group expiry now falls back to blink/chase if those are active,
  instead of base. Again intended, but must be audited.

**Required audit:** enumerate all `led_setBlinkLed()`, `led_pulseLed()`,
`led_flashGroup()`, and `led_setActive_step()` callers. Confirm none depend
on a temporary layer clearing a lower layer as a side effect.

### Verification

- Blink + pulse on same LED: confirm pulse shows, then blink resumes (not
  base gap).
- Blink + flash on same LED: confirm flash shows, then blink resumes.
- Chase + flash on same LED: confirm flash shows, then chase resumes.
- Blink + chase on same LED: confirm last-set-wins at shared priority.
- Flash with base change during flash: confirm new base after flash ends.
- BAR1 blink + pulse: confirm same priority behavior as chain LEDs.
- VOICE-page held-step overlay active: confirm temporary layers render on
  top of overlay base state and restore correctly.
- Screensaver entry/exit: confirm `led_clearAll()` clears the layer bitmap
  along with all layer state.

---

## Implementation order

1. **3.1 Probability gating** — smallest change, zero risk, no dependencies.
2. **3.2 Scene automation targets** — low risk, depends on understanding the
   probability fix (both touch the step-play path).
3. **3.3 LED consolidation** — moderate risk, independent of 3.1/3.2 but
   benefits from having the sequencer changes landed and stable first.

After each item: `make clean && make && make img`, verify build sizes, and
confirm no new warnings. Hardware validation of all three in Phase 4.4.

---

## Phase resolution tracking

| Item | Status | Notes |
|------|--------|-------|
| 3.1 Probability gating | REMEDIATED | Originally implemented wrong (automation inside step-active block). Corrected via `seq_evaluateStepCondition()` — specials read before trigger-active check. See `S070_PHASE3_FUCKUP_REMEDIATION.md`. |
| 3.2a Scene automation drain | DONE | `seq_applySceneAutomation()` dispatches Scene targets from drain loop |
| 3.2b Voice Morph value conversion | DONE | `menu_morphAutomationStore()` / `menu_morphAutomationExpand()` |
| 3.2c Step-edit list `scn`/`fx` cycling | DONE | 8-category VOI stepper, D17 off-default, `PAT_AUTOMATION_TARGET_OFF` sentinel |
| 3.2d Mix sub-page voice Morph cell | DONE | 4th cell `Nvm`, `MENU_SCENE_SETTING_COUNT` = 4 |
| 3.2e Overlay voice Morph target mapping | DONE | `menu_sceneSettingAutomationTarget()` handles all 4 Scene setting kinds |
| 3.2f Audio_out + fx_send Scene targets | DONE | 12 entries added, IDs 392–403, `USE_AUTOMATION` flag |
| 3.3 LED consolidation | DONE | 41-byte `led_activeLayers[]`, `led_renderFromStack()`, all layer start/end paths |
| R1 Scene setting underlines | PLANNED | `va_scanService()` ignores Scene targets; needs `va_searchSceneMask`. See remediation. |
| R2 Live value display refresh | PLANNED | Scene targets not reset on retrigger → display goes stale during playback. Needs 8 Hz periodic repaint. See remediation. |

---

## Decisions log

| # | Decision | Source |
|---|----------|--------|
| D1 | Probability gates trigger + automation together | §3.1, `SCOPING_TARGETS.md` §4.6 |
| D2 | Erase always acts regardless of probability | §3.1, existing behavior preserved |
| D3 | Scene targets do not participate in retrigger restore | §3.2, user decision |
| D4 | Exception: voice 6 Choke `7dc` resets via its own descriptor trigger | §3.2, existing Choke behavior |
| D5 | Voice Morph resets on global Morph set and on Scene change | §3.2, user decision |
| D6 | `7dc` applies regardless of source track | §3.2, user decision |
| D7 | Voice Morph automation: stored = value / 2 (0–126), 255 → 127; applied = stored * 2 (0–252), 127 → 255 | §3.2, user decision |
| D8 | Step-edit VOI cycles: 1–6, `scn`, `fx` (8 categories, 64 params each) | §3.2, user decision |
| D9 | Voice Morph is 4th cell on VOICE/mix sub-page: `Nvm` | §3.2, user decision |
| D10 | `ou` automatable, `fx` automatable, `fd` NOT automatable, `vm` automatable | §3.2, user decision |
| D11 | LED per-LED layer bitmap: 41 bytes SRAM1 approved | §3.3, user approval |
| D12 | Chase at blink priority (not above) | §3.3, user decision |
| D13 | LED caller audit: audit-then-implement approach approved | §3.3, user approval |
| D14 | `EFFECT_PARAMETER` kind: Phase 5 placeholder, no action now | §3.2, deferred |
| D15 | audio_out automation: implement now via `preset_setVoiceAudioOut()` (full runtime apply) | FU-1, user decision |
| D16 | fx_send automation: ID + stubbed apply via `preset_setVoiceFxSendAmount()` (retain-only until Phase 5) | FU-1, user decision |
| D17 | Step-edit category change default: always "off" with no target, not first-available | FU-2, user decision |
| D18 | audio_out/fx_send entries: no velocity/LFO use flags (step automation only for now) | FU-1, implementation detail |

---

## Follow-up items — resolved

### FU-1: `ou` / `fx` Scene target table entries — RESOLVED (D15, D16, D18)

audio_out is fully implementable now. FX send gets an ID plus a stubbed-out
apply path for Phase 5.

`preset_setVoiceAudioOut()` (presetManager.c:1013) retains the route AND
calls `preset_applyKitAudioRouting()` for immediate runtime effect — the
full apply path for audio_out step automation exists today.

`preset_setVoiceFxSendAmount()` (presetManager.c:1032) retains the value
only; the comment marks where the Phase 5 FX bus attaches. FX send step
automation will store and drain correctly but produce no audible change
until Phase 5 provides the runtime effect.

**New entries in `scene_mod_targets[]`:**

| ID  | Kind | Slot | Max | Short | Apply path |
|-----|------|------|-----|-------|------------|
| 392 | AUDIO_OUT | 0 | 5 | `1ou` | `preset_setVoiceAudioOut(active, 0, value)` |
| 393 | AUDIO_OUT | 1 | 5 | `2ou` | `preset_setVoiceAudioOut(active, 1, value)` |
| 394 | AUDIO_OUT | 2 | 5 | `3ou` | `preset_setVoiceAudioOut(active, 2, value)` |
| 395 | AUDIO_OUT | 3 | 5 | `4ou` | `preset_setVoiceAudioOut(active, 3, value)` |
| 396 | AUDIO_OUT | 4 | 5 | `5ou` | `preset_setVoiceAudioOut(active, 4, value)` |
| 397 | AUDIO_OUT | 5 | 5 | `6ou` | `preset_setVoiceAudioOut(active, 5, value)` |
| 398 | FX_SEND | 0 | 127 | `1fx` | stubbed: `preset_setVoiceFxSendAmount(active, 0, value)` |
| 399 | FX_SEND | 1 | 127 | `2fx` | stubbed: `preset_setVoiceFxSendAmount(active, 1, value)` |
| 400 | FX_SEND | 2 | 127 | `3fx` | stubbed: `preset_setVoiceFxSendAmount(active, 2, value)` |
| 401 | FX_SEND | 3 | 127 | `4fx` | stubbed: `preset_setVoiceFxSendAmount(active, 3, value)` |
| 402 | FX_SEND | 4 | 127 | `5fx` | stubbed: `preset_setVoiceFxSendAmount(active, 4, value)` |
| 403 | FX_SEND | 5 | 127 | `6fx` | stubbed: `preset_setVoiceFxSendAmount(active, 5, value)` |

Table grows from 8 to 20 entries (IDs 384–403), within the 64-ID `scn`
namespace. No 7-bit conversion needed for either kind: audio_out max is 5
(fits trivially in 7 bits), fx_send max is 127 (identity mapping).

**New enum values** in `scene_mod_target_kind_t` (SceneModTargets.h), insert
before `SCENE_MOD_TARGET_KIND_EFFECT_PARAMETER`:
- `SCENE_MOD_TARGET_KIND_AUDIO_OUT`
- `SCENE_MOD_TARGET_KIND_FX_SEND`

**Use flags (D18):** new entries carry no `SCENE_MOD_TARGET_USE_VELOCITY` or
`SCENE_MOD_TARGET_USE_LFO` flags. Audio_out is a discrete 6-value routing
enum — velocity scaling and LFO modulation of a routing selector would
produce nonsensical results. FX send is continuous and could plausibly use
velocity/LFO in the future, but those modulation paths are not needed until
Phase 5 makes fx_send audible. Add the flags when the runtime path exists.

**Reset model:** same as all Scene targets (D3) — no retrigger reset, reset
on Scene change / parameter edit / re-automation.

### FU-2: Step-edit default target on category change — RESOLVED (D17)

The default when changing voice category — whether to a different voice
slot, or to `scn`, or to `fx` — is always "off" with no target. This avoids
inadvertently changing a parameter if playback is active.

`menu_stepAutomationAddDefault()` currently picks the first available
descriptor target and reads its current image value. The new behavior:

- When VOI changes to any new slot/category, set the target to
  `INSTRUMENT_PARAM_INVALID` (off) with value 0.
- The user then manually selects a target via the PAR field encoder.
- This applies to all category transitions: voice→voice, voice→`scn`,
  `scn`→voice, voice→`fx`, etc.

This simplifies `menu_stepAutomationAddDefault()` — no Scene-target variant
of `menu_stepAutomationFirstTarget()` is needed. The function sets "off" for
any category change, and the user-driven PAR cycling reads the current
parameter value when a target is selected.

### FU-3: Held-step overlay target resolution — RESOLVED

Approved as planned. The overlay's target resolution path handles
`MENU_CELL_SCENE_SETTING` cells by returning the corresponding Scene target
ID from `sceneModTarget_voiceMorphId(slot)` (for voice Morph) or the
equivalent lookup for audio_out/fx_send targets, instead of looking up a
descriptor. The value conversion (halving for storage) applies in the
overlay's pot-to-automation write path for voice Morph targets specifically.
No ambiguity — implementation follows the existing overlay model.

### FU-4: Scene setting cells from the held-step overlay — RESOLVED

Approved as planned, updated per FU-1 resolution. With audio_out and
fx_send Scene target IDs now present (IDs 392–403), the overlay behavior
on the mix sub-page is:

| Cell | Overlay behavior |
|------|-----------------|
| `Nou` (audio_out) | Automatable — overlay creates/updates Scene target entry (ID 392+slot) |
| `Nfx` (fx_send) | Automatable — overlay creates/updates Scene target entry (ID 398+slot), stubbed apply until Phase 5 |
| `Nfd` (fader) | NOT automatable — overlay ignores pot movement on this cell |
| `Nvm` (voice morph) | Automatable — overlay creates/updates Scene target entry (ID 384+slot) with halving conversion |

The overlay's `menu_voiceAutoOverlayPotTarget()` must handle
`MENU_CELL_SCENE_SETTING` cells by mapping the scene setting kind to the
appropriate Scene target ID. For non-automatable settings (fader), it
returns `INSTRUMENT_PARAM_INVALID` so the overlay skips the cell.

---

## Hardware validation results

Post-test output validated from `SD_CARD_PHASE3_OUTPUT/`.

### PatternTrace (`pattrace.bin`)

- 1,528 bytes, 191 records
- Zero error-class records (no H/C/F)
- All records are maintenance operations: 152× V (repair reserve), 39× L (repair reloc)
- Dynamic block allocation healthy under Scene automation load

### AutoSaveTrace (`asavetrc.bin`)

- 556,824 bytes, 69,603 records
- Zero error-class records (no E/X/Z/U)
- Pipeline flow: 209 admitted → 206 captured → 206 published
- 3 admit-only records are normal (dirty marks that resolved before capture window)

### Pattern AutoSave — Scene 5 (`pat05a`, `pat05b`)

- Generation ping-pong: `.pat05a` gen 68, `.pat05b` gen 69 — gen 69 wins
- CRC32C (Castagnoli) validated on both files
- 26 active steps, 282 automation entries total
- 38 Scene target entries across target IDs {384, 389, 392, 397, 403}:
  - 384 = voice 1 morph (`1vm`)
  - 389 = voice 6 morph (`6vm`)
  - 392 = voice 1 audio_out (`1ou`)
  - 397 = voice 6 audio_out (`6ou`)
  - 403 = voice 6 fx_send (`6fx`)
- Scene automation targets survive the full AutoSave pipeline and persist correctly in PAT4 format
- HCNAMES row 135 confirms source: `Barf	@` (Pattern AutoSave source for Scene 5)

### Assessment

All Phase 3 features are implemented and build-verified. Hardware test confirms:

1. **Dynamic block engine** operates normally under the additional Scene automation load — no fragmentation errors, no capacity drops, repair operations are routine maintenance only.
2. **AutoSave pipeline** is fully functional — the admit/capture/publish flow completes without error across 206 save cycles.
3. **Scene automation storage** round-trips correctly through the PAT4 format — all 5 Scene target ID classes (vm, ou, fx) are present in the saved pattern with correct target IDs from the 9-bit address space.

## Open remediation items

Three items identified during post-implementation review are documented in
`S070_PHASE3_FUCKUP_REMEDIATION.md` and tracked in the phase resolution table above:

1. **Change 3.1-A conditional gate restructuring** — The original probability gating implementation incorrectly placed automation queueing inside the step-active block, breaking non-trigger step automation (the primary mechanism for parameter sweeps). Corrected via `seq_evaluateStepCondition()` with specials read moved before the trigger-active check. Build-verified. The conditional gate function is structured for future expansion beyond probability (track mute combos, loop iteration count, scene chain origin, button state).

2. **R1: Scene setting underlines** — `va_scanService()` only checks `instrumentParam_isVoiceParameter()`, so Scene targets are silently dropped from the search agent. Fix requires `va_searchSceneMask` (1 byte, 3 bits for vm/ou/fx) and extending both the compact-view and edit-mode marker paths in `va_applyVoiceMarkers()`.

3. **R2: Live value display during playback** — Scene targets persist through retrigger (per D3), so displayed values go stale during playback. Fix adds `menu_sceneLiveRefreshService()` at 8 Hz, gated by `seq_isRunning()` and current page identity, with `editModeActive` guard to prevent mid-edit overwrites. 2 bytes SRAM for timer.

## Task handoff closure

Phase 3 (items 3.1, 3.2a–f, 3.3) is complete. The 42 named changes from
`S070_PHASE3_IMPLEMENTATION.md` are implemented and build-verified. Hardware
validation confirms correct operation of the dynamic block engine, AutoSave
pipeline, and Scene automation storage under real sequencer load.

The critical 3.1-A defect has been corrected and the fix build-verified.
Remediation items R1 and R2 are documented with full implementation plans and
ready for a future session.

**Session 070 Phase 3 — CLOSED.**
