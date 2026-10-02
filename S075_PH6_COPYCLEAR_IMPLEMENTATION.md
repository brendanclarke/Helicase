# S075 — Phase 6 Copy and Clear — Implementation Schedule

**Status:** line-level schedule, Session 075 (2026-10-01). No code has been
changed. This document implements `S075_PH6_COPY_CLEAR_FULL_SPEC.md`
revision 3 (the "spec"). Where this schedule and the spec disagree, the spec
wins and this schedule is corrected.

**Baseline:** branch `dev-ph6-copyclear`, HEAD `b33c94e`. Every line number in
this document refers to that tree. Within one file, apply changes from the
bottom of the file upward, or re-anchor each change by the quoted anchor text,
so earlier edits do not shift later line numbers.

**Who applies it:** the user or an implementing agent the user directs. Each
stage ends with a build, the link budget, and a hardware check by the user
(§14 of the spec). Commits are the user's.

---

## Contents

0. Conventions
1. Stage order and gates
2. RAM, stack, flash ledger
3. Stage 1 — LED group blink
4. Stage 2 — Retire global `srt`; Effect Morph on PERF
5. Stage 3 — CopyClear module skeleton, button handling, menus, suspension
6. Stage 4 — Pattern pool reserve, exclusive boundary, raw block API, pastes
7. Stage 5 — Pattern clears
8. Stage 6 — Pot-clear register and underline suppression
9. Stage 7 — FX sequence copy and clears; Effect fan-out
10. Stage 8 — Scene-level pastes and clears
11. Stage 9 — Identity rows (HCNAMES)
12. Stage 10 — Documentation and tools
13. Verification matrix

---

## 0. Conventions

- **Change IDs** are `S<stage>-<nn>`. Each entry gives the file, the line(s) at
  `b33c94e`, the action (ADD, REMOVE, MODIFY, REPLACE), the anchor text, what
  the code must do, and the comment block to place beside the code.
- **Comment blocks** are written to be pasted verbatim above the new or
  changed code (and, for declarations, above the prototype in the header).
  They follow the project rule: what the code does, why it exists, inputs,
  outputs, common accessors, affiliates. Where a `.c` and `.h` change pair
  exists, the header carries the contract block and the `.c` carries a shorter
  implementation block that points back to it, as existing modules do.
- **Removals** carry a rationale instead of a comment block; where a stale
  reference would otherwise remain, a short tombstone comment is specified.
- **New files** are given as complete outlines: file header block, includes,
  every static with its byte count and comment block, every function with its
  prototype, comment block and numbered implementation steps. No function body
  code is written here.
- **Terminology** follows the spec §1. Code names are given in backticks.
- **Binding rules** (spec header): RAM within the approved +100 B; Pattern
  writes keep the publication order (write new, PRIMASK swap of the address
  entry, free old); only one Scene's Pattern is modified at a time; SceneData
  is the only writer of retained Scene data and every retained change marks
  AutoSave; foreground only.

---

## 1. Stage order and gates

| Stage | Content | Depends on | Gate (user hardware check) |
|---|---|---|---|
| 1 | LED group blink | — | Existing LED behaviour unchanged in every mode; group blink demo via a temporary test call is not required (exercised in Stage 3) |
| 2 | Retire `srt`; PERF `fxm` cell | — | PERF `fxm` edits and fans out; old Scenes load; old `srt` automation inert; AutoSave power cycle |
| 3 | CopyClear skeleton, routing, menus, suspension, name-buffer borrow, old code removed | 1 | No leaked button edges in any mode; menus and indicator; AutoSave/trace/settings pause and resume |
| 4 | Pool reserve, exclusive boundary, raw block API, pastes | 3 | Every paste selection on a playing Pattern; ranges, wrap, retargeting, nearly-full pool |
| 5 | Pattern clears | 4 | Every clear selection; PERF clears on active and other Scenes |
| 6 | Pot-clear register, underlines | 5 | 8-entry limit; values never change; underlines |
| 7 | FX sequence, Effect fan-out | 3 | FX paste/clear with fan-out; type mismatch dropped |
| 8 | Scene-level pastes and clears | 4, 7 | Instrument, Kit, Effect, settings, Scene, Pattern copies; Scene clears; masks; present rule |
| 9 | Identity rows | 8 | HCNAMES rows after every kind |
| 10 | Documentation and tools | all | — |

Build gate after every stage: `make all && make img`, then
`python3 tools/link_budget.py arm-none-eabi-nm build/lxr02.elf`; record the
`.img` SHA-256. A stage that grows RAM beyond the ledger in §2 stops for user
approval.

---

## 2. RAM, stack, flash ledger

All new statics are SRAM1 `.bss`, firmware lifetime. Approved ceiling for this
feature: +100 B (user, 2026-10-01).

| Owner (file) | Item | Bytes |
|---|---|---:|
| `ledHandler.c` | `led_blinkGroupMask[3]` (SELECT, SEQ, VOICE) | 6 |
| `ledHandler.c` | `led_blinkPhase` | 1 |
| `PatternStackService.c` | `service_exclusive_scene` | 1 |
| `copyClearSession.c` | operation state (`cc_state`: phase, operation kind, menu kind, selection, flags, held row) | 6 |
| `copyClearSession.c` | `cc_source` (kind, Scene, track, start, end, reserved) | 6 |
| `copyClearSession.c` | press-order stack (16 nibbles) + count | 9 |
| `copyClearSession.c` | consumed-edge masks (SEQ 2, SELECT 1, TRACK 1) | 4 |
| `copyClearService.c` | queue: 4 × 6 B jobs + head + count | 26 |
| `copyClearService.c` | register: 8 × `u16` target + count + Scene | 18 |
| `copyClearService.c` | running-job state (job copy is in the queue head; phase, cursor, track cursor, flags, swap state, wait reason, name-write state, scratch state, compaction cursor ×2) | 11 |
| `filesystem.c` | `fs_name_cache_borrowed` | 1 |
| **Total** | | **89** |

Removed by Stage 2: `scene_settings_t.voice_decimation_all` (1 B × 16 Scenes
= 16 B) and `mixer_decimation_rate[6]` (4 B, `INCCMZ`). These releases are not
reused (approval policy).

Stack: the merge path and the pot-clear removal use one
`pat_automation_entry_t[63]` buffer (252 B), the same size as the existing
buffer in `pat_writeSpecials()`; the raw-block helpers use one 132 B block
buffer. Neither nests with another such buffer.

Pool data (not RAM): the swap block reserves the top 33 chunks (132 B) of each
Scene's 8,192 B pool.

Working storage: the 9,000 B name cache (`fs_list_cache_name`) is borrowed
while an operation runs (Stage 3, S3-61). Layout in S4-40.

Flash estimate: 10–14 KB.

---

## 3. Stage 1 — LED group blink

The priority layer stack (`led_activeLayers[]`, `led_renderFromStack()`) has
existed since S070 and already provides `base < blink/chase < flash < pulse`
with correct fallback on expiry. Copy/clear needs one addition: a persistent
blink that can cover up to 16 LEDs of one row at once (a step range), which
the 8 blink slots cannot. Nothing else in the LED stack changes.

#### S1-01 `Core/Hardware/frontPanel/ledHandler.c` — ADD group-blink state

- Action: ADD after line 149 (`#define LED_LAYER_COUNT  41u`) and before line
  151 (`static uint8_t led_activeLayers[...]`).
- Code: `static uint16_t led_blinkGroupMask[3];` indexed SELECT = 0, SEQ = 1,
  VOICE = 2 (a private enum `led_blink_group_index_t`), and
  `static uint8_t led_blinkPhase;`.

```c
/*
 * Persistent group-blink masks and the shared blink phase (S075).
 *
 * What: one 16-bit mask per LED row that can blink as a set (SELECT, SEQ,
 * VOICE), plus a one-bit phase flipped at every LED_BLINK_TIME_MS tick. A set
 * bit makes that LED blink until the mask is changed; the LED carries the
 * LED_LAYER_BLINK bit while it is a member.
 * Why: the eight blink slots cannot show a 16-step copy source. Group members
 * are rendered as an absolute value (base XOR phase) rather than toggled, so
 * an LED that is also in a blink slot cannot be double-toggled out of step.
 * Inputs: led_setBlinkGroup(). Outputs: temporary LED output each blink tick
 * and on every led_renderFromStack() for a member. Lifetime: firmware. RAM:
 * 7 B SRAM1 (S075 ledger). Accessors: led_setBlinkGroup(), led_tickHandler(),
 * led_renderFromStack(). Affiliates: copyClearSession.c source indication.
 */
```

#### S1-02 `ledHandler.c` — ADD private helpers `led_blinkGroupIndex()`, `led_blinkGroupMember()` and `led_baseValue()`

- Action: ADD after `led_flashGroupLed()` (ends line 612).
- `led_blinkGroupIndex(LedFlashGroup group)` returns 0/1/2 for SELECT/SEQ/VOICE
  and 0xFF otherwise. `led_blinkGroupMember(uint8_t ledNr)` returns nonzero
  when the logical LED is set in its row's group mask. `led_baseValue(uint8_t
  ledNr)` returns the remembered base bit from `led_originalLedState[]` (or
  `led_sw43OriginalState`).

```c
/*
 * Map a public flash group to a group-blink slot, and read one base bit.
 *
 * What: led_blinkGroupIndex() accepts only the three rows copy/clear blinks
 * (SELECT, SEQ, VOICE) and returns 0xFF for the others; led_baseValue()
 * returns the remembered base state of one logical LED without touching the
 * output. Why: group-blink rendering is absolute (base XOR phase), so it needs
 * the base bit, and the group API reuses LedFlashGroup so callers do not learn
 * a second row enum. Inputs: group or logical LED id. Outputs: index or bit.
 * Accessors: led_setBlinkGroup(), led_tickHandler(), led_renderFromStack().
 */
```

#### S1-03 `ledHandler.c` — MODIFY `led_renderFromStack()` (lines 299–356)

- Action: MODIFY. Replace line 349–350
  (`if (layers & (LED_LAYER_BLINK | LED_LAYER_CHASE)) return;`) with: if the LED
  is a member of a group-blink mask, `led_setValueTemp(base ^ led_blinkPhase,
  ledNr)` and return; otherwise keep the existing return.

```c
/*
 * Group-blink members re-render to their absolute phase value (S075).
 *
 * A slot blink owns its own toggled output, so the existing early return keeps
 * it. A group member has no per-LED toggle history: when a higher layer
 * (pulse or flash) expires, the member must show base XOR the shared phase,
 * or it would stay stuck on the expired layer's value until the next tick.
 */
```

#### S1-04 `ledHandler.c` — MODIFY `led_tickHandler()` (lines 856–905)

- Action: MODIFY the blink section (lines 893–905). After the slot loop, flip
  `led_blinkPhase` and, for every set bit of every group mask, write
  `led_setValueTemp(base ^ led_blinkPhase, led)` unless the LED carries a pulse
  or flash layer.

```c
/*
 * Advance the shared blink phase and render group-blink members (S075).
 *
 * Slots keep their relative toggle; groups are written after the slots as an
 * absolute value, so a LED in both sets ends each tick in the group's phase.
 * Pulse and flash layers keep priority exactly as for slot blinks.
 */
```

#### S1-05 `ledHandler.c` — ADD `led_setBlinkGroup()`

- Action: ADD after `led_clearAllBlinkLeds()` (ends line 841).
- Steps: (1) map the group; ignore unsupported groups. (2) Clean the mask with
  `led_cleanFlashMask()`. (3) For bits leaving the mask: clear
  `LED_LAYER_BLINK` unless the LED is also in a blink slot, then
  `led_renderFromStack()`. (4) For bits entering: set `LED_LAYER_BLINK` and
  render. (5) Store the mask.
- `led_clearAllBlinkLeds()` (lines 827–841) is NOT changed: group masks belong
  to their owner and are cleared only through `led_setBlinkGroup(group, 0)`.
  Its BLINK-layer clear must not remove the bit from a group member: MODIFY
  line 835–836 to keep `LED_LAYER_BLINK` when `led_blinkGroupMember(ledNr)`.

```c
/*
 * Set or replace one row's persistent group blink (S075).
 *
 * What: makes exactly the LEDs in `mask` blink as a set until the next call
 * for the same group; mask 0 stops the group. Why: copy/clear marks a source
 * that can span 16 SEQ LEDs, more than the eight blink slots hold, and must
 * survive mode-change slot clears (led_clearAllBlinkLeds() leaves groups
 * alone). Inputs: LED_FLASH_GROUP_SELECT, _SEQ or _VOICE and a row mask (bit
 * 0 = first LED of the row). Other groups are ignored. Outputs: BLINK layer
 * bits and rendered output for every LED whose membership changed.
 * Accessors: copyClearSession.c (source indication and teardown).
 * Affiliates: led_tickHandler(), led_renderFromStack(), led_cleanFlashMask().
 */
```

#### S1-06 `ledHandler.c` — MODIFY `led_init()` (line 358) and `led_clearAll()` (line 513)

- Action: MODIFY both to zero `led_blinkGroupMask[]` and `led_blinkPhase`.

```c
/* Group blinks start empty and a global clear cancels them with every layer. */
```

#### S1-07 `Core/Hardware/frontPanel/ledHandler.h` — ADD prototype

- Action: ADD after line 140 (`void led_clearAllBlinkLeds(void);`).

```c
/*
 * Persistent group blink for one LED row (S075).
 *
 * Inputs: LED_FLASH_GROUP_SELECT, LED_FLASH_GROUP_SEQ or LED_FLASH_GROUP_VOICE
 * and a row bit mask; other groups are ignored. Output: the masked LEDs blink
 * in the shared blink phase until the next call for that group (mask 0 stops
 * it). Unlike led_setBlinkLed() there is no slot limit, and
 * led_clearAllBlinkLeds() does not cancel a group. Clients: copy/clear source
 * indication. Affiliates: led_tickHandler(), the layer stack.
 */
void led_setBlinkGroup(LedFlashGroup group, uint16_t mask);
```

---
## 4. Stage 2 — Retire global `srt`; Effect Morph on PERF

Spec §10, §12.3 F1–F2. The PERF cell keeps its flat parameter slot, which is
renamed and repurposed, so no later `PAR_*` id, menu text id, AutoSave cell or
Scene target ID moves.

### 4.1 Parameter, page and text ids

#### S2-01 `Core/Bank/Scene/Preset/ParameterArray.h` — MODIFY lines 149–158

- Action: MODIFY. Rename `PAR_VOICE_DECIMATION_ALL` (line 158) to
  `PAR_EFFECT_MORPH` in the same position. Replace the comment sentence on
  lines 149–150 with the block below.

```c
	 * lists. PAR_EFFECT_MORPH (S075) occupies the slot of the retired global
	 * decimation id: it is the flat PERF mirror of the active Scene's Effect
	 * Morph amount (scene_settings_t::effect_morph_amount, 0..255), shown as
	 * PERF "fxm". It is refresh-only in menu_parseGlobalParam(); page edits
	 * commit through menu_commitEffectMorphParam() -> effects_setMorphAmount(),
	 * which fans out through the edit mask. Keeping the position means no
	 * later flat id is renumbered.
```

#### S2-02 `Core/Menu/menuPages.h` — MODIFY line 71 and the comment at lines 59–69

- Action: MODIFY line 71: `TEXT_SAMPLE_RATE` → `TEXT_EFFECT_MORPH`,
  `PAR_VOICE_DECIMATION_ALL` → `PAR_EFFECT_MORPH`. In the comment, replace
  "and Scene global decimation" (line 66) with the text below.

```c
 * Output: PERF exposes overall Scene Morph, six per-voice Morph amounts, and
 * the Scene's Effect Morph amount `fxm` (S075; it replaced the retired global
 * `srt` decimation cell).
```

#### S2-03 `Core/Menu/menu.h` — MODIFY lines 118, 172, 209

- Action: MODIFY. Rename in place (positions unchanged, parallel tables stay
  aligned): `TEXT_SAMPLE_RATE` → `TEXT_EFFECT_MORPH` (line 118),
  `LONG_SAMPLE_RATE` → `LONG_EFFECT_MORPH` (line 172), `SHORT_SR` →
  `SHORT_EFFECT_MORPH` (line 209). These names were used only by the PERF
  `srt` cell (verified: `menuPages.h:71`, `menu.c:1143`).
- Comment (append to the enum line):

```c
    /* TEXT/SHORT/LONG_EFFECT_MORPH: PERF `fxm` (S075; was the `srt` cell). */
```

#### S2-04 `Core/Menu/MenuText.h` — MODIFY lines 130 and 163

- Action: MODIFY `{"srt"}` → `{"fxm"}` (line 130, `shortNames[]`) and
  `{"SampleRt"}` → `{"FX Morph"}` (line 163, `longNames[]`).
- No separate comment; the S2-03 enum comment covers the pair.

#### S2-05 `Core/Menu/menu.c` — MODIFY line 1143

- Action: MODIFY `{SHORT_SR,CAT_VOICE,LONG_SAMPLE_RATE},` →
  `{SHORT_EFFECT_MORPH,CAT_SCENE,LONG_EFFECT_MORPH},`.

```c
    /* S075 PERF `fxm`: Scene-owned Effect Morph (was global `srt`). */
```

#### S2-06 `Core/Menu/menu.c` — MODIFY line 1028

- Action: MODIFY `[PAR_VOICE_DECIMATION_ALL] = DTYPE_0B127,` →
  `[PAR_EFFECT_MORPH] = DTYPE_0B255,`.

```c
    /* S075: PERF `fxm` shows the full 0..255 Effect Morph amount. */
```

### 4.2 PERF `fxm` behaviour

#### S2-07 `Core/Menu/menu.c` — MODIFY `menu_paramIsMorphAmount()` lines 11302–11305

- Action: MODIFY. Return nonzero also for `PAR_EFFECT_MORPH`, so the encoder
  and the endless pot use double angular speed (F1) through the existing
  `menu_updateEndlessPotScales()` path.

```c
    /*
     * S075: PERF `fxm` (Effect Morph, 0..255) uses the same double-speed pot
     * and encoder behaviour as Scene Morph (user, F1).
     */
```

#### S2-08 `Core/Menu/menu.c` — ADD `menu_commitEffectMorphParam()`; route it from `menu_cellCommitValue()`

- Action A: ADD a forward declaration after line 1827
  (`static uint8_t menu_commitBusCompParam(...)`).
- Action B: ADD the definition after `menu_commitBusCompParam()` (ends line
  11365). Steps: (1) `effects_setMorphAmount(scene_getActiveIndex(), value)`;
  (2) `preset_syncEffectMorphMirror()`; (3) return 1.
- Action C: ADD in `menu_cellCommitValue()` after line 3745 (the bus
  compressor branch): `if (cell->kind == MENU_CELL_STATIC &&
  cell->static_param == PAR_EFFECT_MORPH) return
  menu_commitEffectMorphParam((uint8_t)value);`

```c
/*
 * Commit one PERF `fxm` edit (S075).
 *
 * What: writes the active Scene's Effect Morph amount through the
 * EffectsManager edit API, which fans the value out to every Scene in the
 * active Scene's edit mask and marks each Scene's AutoSave cell 40, then
 * refreshes the flat PERF mirror from SceneData. Why: Effect Morph is
 * Scene-owned; the generic static-cell path would only change
 * parameter_values[] and route through menu_parseGlobalParam(), which is also
 * replayed by Settings Load and must stay refresh-only (O1). Inputs: the
 * clamped 0..255 value. Output: 1 (repaint requested). Accessors:
 * menu_cellCommitValue(). Affiliates: effects_setMorphAmount(),
 * preset_syncEffectMorphMirror(), the Effect page `mrp` cell.
 */
```

#### S2-09 `Core/Menu/menu.c` — REPLACE `case PAR_VOICE_DECIMATION_ALL:` lines 13308–13328

- Action: REPLACE the whole case with `case PAR_EFFECT_MORPH:
  preset_syncEffectMorphMirror(); break;`.

```c
    case PAR_EFFECT_MORPH:
        /*
         * S075 PERF `fxm` is refresh-only here, like the bus compressor
         * mirrors. Settings/legacy loads replay this id range; they must not
         * write SceneData. Page edits commit through
         * menu_commitEffectMorphParam() instead.
         */
```

#### S2-10 `Core/Menu/menu.c` — REMOVE lines 13874–13884

- Action: REMOVE the comment and `parameter_values[PAR_VOICE_DECIMATION_ALL] =
  127u;`.
- Rationale: the startup hazard it guarded (a zero mirror shaping
  `mixer_decimation_rate[6]` to 0 and silencing the unit) disappears with the
  multiplier. `parameter_values[]` is memset to 0, which is the default Effect
  Morph amount, and Scene apply re-syncs the mirror (S2-13).

#### S2-11 `Core/Menu/menu.c` — ADD mirror refresh on PERF entry, after line 12841

- Action: ADD `if (pageNr == PERFORMANCE_PAGE) preset_syncEffectMorphMirror();`
  after `lockPotentiometerFetch();` in the shared PERF/PATTERN_SETTINGS/SEQ
  case of `menu_switchPage()`.

```c
        /*
         * S075: Effect Morph can change on the Effect page (`mrp`), by a paste
         * or by a clear while PERF is not shown. Refresh the `fxm` mirror on
         * entry so the first PERF frame shows the retained value.
         */
```

#### S2-12 `Core/Menu/menu.c` — REMOVE the `SCENE_MOD_TARGET_KIND_DECIMATION_ALL` case in `menu_stepAutomationCurrentValue()` lines 9284–9289

- Action: REMOVE the case (6 lines).
- Rationale: the kind is retired (S2-17). A stored retired target falls to the
  function's existing default (value 0), which the STEP page shows only for a
  stale entry.

### 4.3 Preset

#### S2-13 `Core/Bank/Scene/Preset/presetManager.c`

- (a) REMOVE `preset_applyVoiceDecimationAllRuntime()` lines 730–745.
- (b) REPLACE lines 1296–1298 in `preset_applySceneSettings()` with
  `preset_syncEffectMorphMirror();`, and in its comment block (lines
  1264–1270) replace "and global decimation is applied" with "and the PERF
  `fxm` mirror is refreshed".
- (c) REMOVE `preset_setVoiceDecimationAll()` lines 3078–3102.
- (d) ADD `preset_syncEffectMorphMirror()` after `preset_syncBusCompMirrors()`
  (ends line 3146): `parameter_values[PAR_EFFECT_MORPH] =
  scene_getEffectMorphAmount(scene_getActiveIndex());`.

```c
/*
 * Copy the active Scene's Effect Morph amount into the PERF `fxm` mirror.
 *
 * What: a read-only refresh of parameter_values[PAR_EFFECT_MORPH] from
 * SceneData. Why: the PERF static cell displays the flat mirror, while the
 * retained value can change on the Effect page, through edit-mask fan-out, by
 * Scene activation, or by a copy/clear. Inputs: SceneData's active Scene.
 * Output: one mirror byte; no retained write, no AutoSave mark. Accessors:
 * preset_applySceneSettings(), menu_commitEffectMorphParam(),
 * menu_parseGlobalParam(PAR_EFFECT_MORPH), menu_switchPage(PERFORMANCE_PAGE),
 * copy/clear completion. Affiliates: preset_syncBusCompMirrors().
 */
```

- Rationale for (a)/(c): global decimation is eliminated (F2); every caller is
  removed in S2-18..S2-21.

#### S2-14 `Core/Bank/Scene/Preset/presetManager.h`

- MODIFY lines 515–516: drop the `preset_setVoiceDecimationAll()` sentence.
- REMOVE line 530 (`void preset_setVoiceDecimationAll(...)`).
- REMOVE lines 544–553 (comment and prototype of
  `preset_applyVoiceDecimationAllRuntime()`).
- ADD after line 543 (`void preset_syncBusCompMirrors(void);`):

```c
/*
 * Refresh the PERF `fxm` mirror from the active Scene's Effect Morph (S075).
 *
 * Read-only: copies scene_getEffectMorphAmount(active) into
 * parameter_values[PAR_EFFECT_MORPH]. Callers: Scene apply, PERF entry, the
 * PERF commit path and copy/clear completion.
 */
void    preset_syncEffectMorphMirror(void);
```

### 4.4 SceneData

#### S2-15 `Core/Bank/Scene/SceneData.h`

- REMOVE lines 172–181 (the `voice_decimation_all` field and its comment).
- MODIFY the block at lines 394–401 to describe only the Scene Morph setter,
  and REMOVE line 404 (`void scene_setVoiceDecimationAll(...)`).

```c
/*
 * Store the Scene-wide Morph amount through its retained owner.
 *
 * Inputs: resident Scene plus a 0..255 amount. Outputs: changed storage is
 * committed before its named AutoSave bit; equal values and invalid Scenes do
 * nothing. Runtime Morph apply remains Preset-owned. (S075: the former global
 * decimation setter was removed with the parameter.)
 * Affiliate: preset_morphScene().
 */
```

#### S2-16 `Core/Bank/Scene/SceneData.c`

- REMOVE lines 352–369 (`scene_setVoiceDecimationAll()`).
- REMOVE line 940 (`scenes[scene_index].settings.voice_decimation_all =
  127u;`).
- Rationale: field removed.

### 4.5 Scene target namespace

#### S2-17 `Core/Bank/Scene/SceneModTargets.h` line 11 and `SceneModTargets.c` lines 12–19, 45–48

- (a) `SceneModTargets.h` line 11: rename `SCENE_MOD_TARGET_KIND_DECIMATION_ALL`
  → `SCENE_MOD_TARGET_KIND_RETIRED` (same enum position).

```c
    /*
     * Retired Scene target placeholder (S075).
     *
     * The Scene target table is positional (ID = base + row), so the row that
     * held the removed global decimation `srt` (ID 390) stays as a placeholder
     * with no use flags. Pickers skip it, sceneModTarget_valid() rejects it,
     * and no apply path handles it, so stored Pattern entries and LFO tokens
     * that name it do nothing. The ID must never be reused.
     */
```

- (b) `SceneModTargets.c` lines 45–48: the row becomes
  `{ SCENE_MOD_TARGET_ID(6u), SCENE_MOD_TARGET_KIND_RETIRED, 0xffu, 0u, 0u,
  0u, "Scene", "--------", "---" }`.
- (c) `SceneModTargets.c` lines 12–19: replace the sentence about Scene
  Decimation with "Row 6 is the retired `srt` placeholder (S075); its ID stays
  reserved so later IDs do not move."
- (d) `SceneModTargets.h` descriptor comment (around line 90, "global
  decimation"): remove "global decimation,".

### 4.6 Runtime paths

#### S2-18 `Core/DSP/Instruments/InstrumentManager.c`

- REMOVE lines 2402–2404 (velocity apply case).
- REMOVE lines 2519–2525 (LFO apply case).
- REMOVE lines 2693–2698 (the `DECIMATION_ALL` block in the LFO teardown;
  keep the following `SLOT6_TRACK7_AMP_DECAY` block).
- Rationale: the target is retired; `sceneModTarget_valid()` already prevents
  installation of a retired row, so these branches are dead.

#### S2-19 `Core/Sequencer/sequencer.c`

- REMOVE lines 383–388 (overlay restore case).
- REMOVE lines 972–979 (step automation apply case).
- Rationale: as S2-18. A stored entry for ID 390 reaches the switch's existing
  default (no action).

#### S2-20 `Core/DSPAudio/mixer.c` lines 126, 145, 154; `Core/DSPAudio/mixer.h` line 60

- MODIFY line 126 and `mixer.h:60`: `mixer_decimation_rate[7]` →
  `mixer_decimation_rate[6]`.
- REMOVE line 145 (`mixer_decimation_rate[6] = 1;`).
- MODIFY line 154: `mixer_decimation_rate[voiceNr]*mixer_decimation_rate[6]` →
  `mixer_decimation_rate[voiceNr]`.

```c
		/*
		 * S075: the global decimation multiplier (former rate[6]) is gone.
		 * With it at its default (1.0f) the product was exact, so this change
		 * is bit-identical for Scenes that left `srt` at 127 (class S0); the
		 * per-voice rate is now the only sample-rate reduction.
		 */
```

- `tools/dsp_test/frozen/` is a frozen baseline and is NOT edited.

#### S2-21 `Core/MIDI/MidiParser.c` line 1094; `Core/MIDI/MidiMessages.h` line 257

- REMOVE `case VOICE_DECIMATION_ALL:` (line 1094). Required: with the array
  shrunk, index 6 would write out of bounds.
- `MidiMessages.h:257`: keep the enum constant (later CC numbers must not
  move); append the comment below.

```c
	VOICE_DECIMATION_ALL,	/* S075: unassigned; enum kept so later CC numbers do not move */
```

### 4.7 Storage and AutoSave

#### S2-22 `Core/Hardware/SD/storageTypes.c` lines 626–633 and 1883

- REPLACE the `voice_decimation_all` branch with an accept-and-ignore branch
  (the key is consumed; the value is not stored).

```c
    } else if (storage_streq(key, "voice_decimation_all")) {
        /*
         * S075: global decimation was removed. Scenes saved before S075 still
         * carry this key; accept it so the file stays valid, and ignore the
         * value. New Scenes never write it.
         */
```

- MODIFY line 1883: drop "voice_decimation_all" from the example list.

#### S2-23 `Core/Hardware/SD/filesystem.c` — sceneset writer lines 17126–17165; stage default line 16518; boot empty line 28663

- REMOVE `case 4u:` (lines 17126–17129) and renumber cases 5..14 to 4..13; in
  the bus compressor case change `op_write_line_index - 11u` to
  `op_write_line_index - 10u`.

```c
    /*
     * S075: line 4 (voice_decimation_all) is no longer written; later lines
     * moved up by one. Parsing is key-based, so line order is not a format
     * contract (FILESYSTEM_SPEC.md sceneset table updated in Stage 10).
     */
```

- REMOVE line 16518 and line 28663 (`... voice_decimation_all = 127u;`).

#### S2-24 `Core/Bank/Scene/Autosave.h` line 237

- MODIFY: `AUTOSAVE_SCENE_PARAM_DECIMATION_ALL = 7,` →
  `AUTOSAVE_SCENE_PARAM_RESERVED_7 = 7,`.

```c
    /*
     * S075: former global decimation cell. The record layout, mask and format
     * version are unchanged: the cell is written as 127 (the neutral value an
     * older firmware expects) and ignored on restore.
     */
```

#### S2-25 `Core/Bank/Scene/Autosave.c` lines 61–67, 907–911, 1347, 1379–1381

- Lines 61–67: rename the enum in both static asserts.
- Lines 907–911 (`autosave_getSceneParameter()`): the voice-Morph bound uses
  `AUTOSAVE_SCENE_PARAM_RESERVED_7`; the cell returns `127u`.

```c
    } else if (parameter_index == AUTOSAVE_SCENE_PARAM_RESERVED_7) {
        /* S075: retired global decimation; neutral constant for old readers. */
```

- Line 1347: drop `voice_decimation_all` from the list.
- Lines 1379–1381 (`autosave_applyScenePayload()`): the branch does nothing.

```c
        } else if (parameter_index == AUTOSAVE_SCENE_PARAM_RESERVED_7) {
            /* S075: retired global decimation byte; ignored on restore. */
```

---

## 5. Stage 3 — CopyClear module skeleton, button handling, menus, suspension

This stage creates `Core/Menu/CopyClear/`, removes the old copy/clear code,
routes every button, encoder and pot event through the new module while the
copy/clear button is held, draws the menus, suspends AutoSave and Pattern
maintenance, and borrows the 9 kB name buffer. Pastes and clears are queued
but their executors are stubs that drop the job until Stages 4–9 fill them.

### 5.1 New directory and build

#### S3-01 `Makefile` — MODIFY line 95 and ADD an include path after line 28

- Line 95: replace `Core/Menu/copyClearTools.c \` with:
  `Core/Menu/CopyClear/copyClearSession.c \`,
  `Core/Menu/CopyClear/copyOps.c \`, `Core/Menu/CopyClear/clearOps.c \`,
  `Core/Menu/CopyClear/copyClearService.c \`.
- After line 28 (`-ICore/Menu \`): add `-ICore/Menu/CopyClear \`.

```make
# S075: Phase 6 copy/clear module (session, copy, clear, background service).
```

#### S3-02 REMOVE `Core/Menu/copyClearTools.c` and `Core/Menu/copyClearTools.h`

- Rationale: the old LXR clear menu (`track`, `pattern`, `autom.1`,
  `autom.2`, where the last two cleared the whole track through a missing
  `case`), its direct LCD writes, the no-op copy wrappers and the empty
  `copyClear_clearTrackAutom()` stub are replaced by `Core/Menu/CopyClear/`.
  Include sites are changed in S3-20 and S3-40.

### 5.2 `Core/Menu/CopyClear/copyClearSession.h` (new)

```c
/*
 * copyClearSession.h — Phase 6 copy/clear operation state and button routing.
 *
 * What: owns the copy and clear operations while the copy/clear button is
 * held: which operation is running, the source, the menu and selection, the
 * range rule for SEQ/SELECT rows, button-edge ownership, LEDs, and the
 * predicate that suspends AutoSave and Pattern maintenance. Why: one owner
 * must see every front-panel event first while copy/clear is held, so no
 * press leaks into step toggles, holds, Scene switches or mutes (spec §7).
 * Inputs: buttonHandler events, encoder and endless-pot turns routed by Menu,
 * mode/bar/track/Scene context from Menu and buttonHandler. Outputs: queued
 * pastes and clears (copyClearService.h), LED state, the menu text drawn by
 * Menu. Affiliates: copyOps.h, clearOps.h, copyClearService.h, menu.c,
 * buttonHandler.c, ledHandler.c. Spec: S075_PH6_COPY_CLEAR_FULL_SPEC.md.
 */
```

Types (all in this header, shared by the CopyClear files):

```c
/*
 * Operation phase. CC_OP_ARMED_COPY: copy/clear held, no source yet.
 * CC_OP_COPY: a copy operation has started (source set). CC_OP_CLEAR: SHIFT
 * was held when copy/clear was pressed. CC_OP_NONE: button not held (queued
 * work may still be finishing; see copyClear_backgroundSuspended()).
 */
typedef enum { CC_OP_NONE = 0, CC_OP_ARMED_COPY, CC_OP_COPY, CC_OP_CLEAR } cc_op_t;

/* What a copy object button selected. */
typedef enum {
    CC_KIND_NONE = 0, CC_KIND_STEP, CC_KIND_BAR, CC_KIND_TRACK,
    CC_KIND_SCENE, CC_KIND_FX_STEP
} cc_kind_t;

/*
 * One source or held clear object (6 B).
 *
 * kind: cc_kind_t. scene: the Scene it belongs to. track: 0..6 for step, bar
 * and track objects. start/end: absolute step 0..127, bar 0..7, FX step
 * 0..15 or Scene 0..15, in press order (start may exceed end: reversed).
 * A single object has start == end.
 */
typedef struct {
    uint8_t kind;
    uint8_t scene;
    uint8_t track;
    uint8_t start;
    uint8_t end;
    uint8_t reserved;
} cc_source_t;

/* Which selection list the menu shows (copyOps.h / clearOps.h own labels). */
typedef enum {
    CC_MENU_NONE = 0,
    CC_MENU_COPY_STEP, CC_MENU_COPY_BAR, CC_MENU_COPY_TRACK,
    CC_MENU_COPY_SCENE, CC_MENU_COPY_FX,
    CC_MENU_CLEAR_STEP, CC_MENU_CLEAR_BAR, CC_MENU_CLEAR_TRACK,
    CC_MENU_CLEAR_TRACK_FX, CC_MENU_CLEAR_SCENE
} cc_menu_t;

/*
 * Automation targets under one endless pot (resolved by Menu, Stage 6).
 * pattern_target: Pattern automation ID or INSTRUMENT_PARAM_INVALID.
 * fx_lane: FX sequence lane 0..15 or 0xFF for none.
 */
typedef struct {
    uint16_t pattern_target;
    uint8_t fx_lane;
} cc_pot_target_t;
```

Prototypes, each with its contract block:

```c
/*
 * Boot initialization. Inputs: none. Output: every operation, source, row
 * stack and edge mask is idle/empty and ccSvc_init() has run. Caller: main.c
 * after patSvc_init(). Affiliates: copyClearService.c.
 */
void copyClear_init(void);

/*
 * Copy/clear button press (called by buttonHandler only when the press is not
 * the recording+running erase gesture).
 * Inputs: shift_held = SHIFT state at the press. Output: nonzero when an
 * operation was armed (copy) or a clear operation began; zero when refused
 * (recording/erasing, Load/Save or Instrument Load busy, LOAD/SAVE, MENU or
 * SOM mode). LEDs: copy -> copy/clear LED steady; clear -> SHIFT and
 * copy/clear blink, latched until release (spec §3, §8.2).
 * Affiliates: buttonHandler.c processPress() case BUT_COPY.
 */
uint8_t copyClear_copyPressed(uint8_t shift_held);

/*
 * Copy/clear button release: ends menu interaction (spec §3).
 * Output: no further presses act; held clear objects apply nothing; the
 * menu closes; LEDs return to the mode's state; queued work keeps running and
 * the AutoSave/maintenance suspension lasts until ccSvc reports idle.
 * Affiliates: buttonHandler.c processRelease() case BUT_COPY,
 * ccSvc_interactionEnded().
 */
void copyClear_copyReleased(void);

/*
 * Route one press/release while an operation is armed or running.
 * Inputs: BUT_* number. Output: nonzero when copy/clear consumed the edge
 * (the caller must not process it further); zero when the normal handler
 * should run (navigation). Consumed presses record their button in an edge
 * mask so the matching release is consumed too. Spec §7 routing tables.
 * Affiliates: buttonHandler.c processPress()/processRelease().
 */
uint8_t copyClear_buttonPressed(uint8_t buttonNr);
uint8_t copyClear_buttonReleased(uint8_t buttonNr);

/*
 * After every processed button event (consumed or not).
 * Output: re-asserts copy/clear and SHIFT LED blinks (a mode change clears
 * blink slots) and recomputes the source group blink for the now-visible
 * bar/track/Scene. Cheap and idempotent. Caller: buttonHandler_processEvents().
 */
void copyClear_postEvent(void);

/* Button-event ring overflow: forget every edge mask and the row stack. */
void copyClear_eventOverflow(void);

/* Nonzero while the copy/clear button is held (any operation phase). */
uint8_t copyClear_holdActive(void);

/*
 * Menu drawing. copyClear_menuVisible() is nonzero while a copy menu (source
 * set) or a clear menu (object pressed) is shown; copyClear_formatMenu()
 * fills two 16-character rows: row 0 "COPY "/"CLR  " plus the source
 * indicator, row 1 the bracketed selection label. Caller: menu_repaint().
 */
uint8_t copyClear_menuVisible(void);
void copyClear_formatMenu(char row0[16], char row1[16]);

/*
 * Encoder and pot ownership while the copy/clear button is held.
 * copyClear_ownsEncoder()/copyClear_ownsPots() are nonzero for the whole
 * hold, so no parameter value changes (spec §3.2). copyClear_encoderTurned()
 * moves the selection (clamped, no wrap) when a menu is shown and does
 * nothing otherwise; clicks are ignored. copyClear_potTurned() starts a pot
 * clear in a clear operation when no menu is shown (Stage 6) and does
 * nothing otherwise. Callers: menu_parseEncoder(), menu_parseKnobDelta().
 */
uint8_t copyClear_ownsEncoder(void);
void copyClear_encoderTurned(int8_t inc);
uint8_t copyClear_ownsPots(void);
void copyClear_potTurned(const cc_pot_target_t *target);

/*
 * AutoSave and Pattern maintenance suspension (spec §9.2).
 * Output: nonzero from the start of an operation (first copy object press or
 * pot clear) until the copy/clear button has been released and the service
 * has finished every queued paste/clear, the register, every apply worker it
 * started, and the name write. Callers: filesystem_tick() scheduler gates,
 * patSvc_tick() repair gate.
 */
uint8_t copyClear_backgroundSuspended(void);

/*
 * Called by ccSvc when all background work is done after interaction ended.
 * Output: the source is forgotten, the suspension ends, the copy/clear-owned
 * LEDs are removed. Caller: ccSvc_tick().
 */
void copyClear_serviceFinished(void);

/* Read-only source access for copyOps/clearOps/ccSvc. */
const cc_source_t *copyClear_source(void);
```

### 5.3 `Core/Menu/CopyClear/copyClearSession.c` (new)

File header block: as the `.h` block, plus "Implementation of the routing
tables in spec §7 and the range rule in spec §4.2."

Includes: `copyClearSession.h`, `copyOps.h`, `clearOps.h`,
`copyClearService.h`, `buttonHandler.h`, `ledHandler.h`, `menu.h`,
`PatternData.h`, `SceneData.h`, `BankData.h`, `sequencer.h`, `config.h`,
`<string.h>`.

Statics (RAM, ledger §2):

```c
/*
 * Operation state (6 B): phase (cc_op_t), started flag, menu kind
 * (cc_menu_t), selection index, held row (0 none, 1 SEQ, 2 SELECT,
 * 3 TRACK, 4 PERF SEQ), and flags (bit0 source set, bit1 clear object held,
 * bit2 pair recorded for the current row hold). Lifetime: firmware; reset
 * at copy/clear release and at copyClear_serviceFinished().
 */
static struct { uint8_t phase, started, menu, selection, row, flags; } cc_state;

/* Source (copy) or held object (clear), spec §4.2 range rule. 6 B. */
static cc_source_t cc_source;

/*
 * Press-order stack for the held row (9 B): 16 four-bit button indices,
 * oldest first, plus a count. Top = most recent button still held. Used only
 * for SEQ and SELECT rows. Accessors: cc_rowPush/cc_rowRemove/cc_rowTop.
 */
static uint8_t cc_rowStack[8];
static uint8_t cc_rowCount;

/*
 * Consumed-edge masks (4 B): bit N set when copy/clear consumed the press of
 * SEQ N+1, SELECT N+1 or TRACK N+1, so the matching release is consumed too,
 * even after the copy/clear button has been released.
 */
static uint16_t cc_seqMask;
static uint8_t cc_selectMask;
static uint8_t cc_trackMask;
/* Consumed MODE and BAR presses are not paired: their release handlers only
 * restore LEDs/overlays and are harmless after a consumed press. */
```

Functions (static unless declared in the header):

1. `copyClear_init()` — zero all statics; `ccSvc_init()`.
2. `copyClear_copyPressed(shift_held)` — steps: (1) refuse when
   `seq_recordActive`, `seq_eraseActive`, `menu_isStorageBusy()`,
   `menu_loadInstrumentTransactionBusy()`, or `buttonHandler_getMode()` is not
   VOICE, STEP, PERF or FX; (2) `cc_state.phase = shift_held ? CC_OP_CLEAR :
   CC_OP_ARMED_COPY`; (3) LEDs: copy → `led_setValue(1, LED_COPY)`; clear →
   `led_setBlinkLed(LED_SHIFT, 1)`, `led_setBlinkLed(LED_COPY, 1)`;
   (4) return 1.
3. `copyClear_copyReleased()` — steps: (1) if phase is NONE return;
   (2) forget the row stack, any held clear object (nothing applies), the menu;
   (3) LEDs: `led_setBlinkLed(LED_COPY,0)`, `led_setBlinkLed(LED_SHIFT,0)`,
   `led_setValue(0, LED_COPY)`, SHIFT LED base = physical SHIFT state, all
   three group blinks to 0; (4) `cc_state.phase = CC_OP_NONE`; (5) if the
   menu was visible, `menu_copyClearMenuClosed()`; (6)
   `ccSvc_interactionEnded()`; if not started, also clear `cc_source`.
4. `copyClear_buttonPressed(buttonNr)` — the router. Steps:
   1. If phase is NONE return 0.
   2. Classify with `buttonHandler_seqIndex()`, `buttonHandler_selectIndex()`,
      `buttonHandler_voiceIndex()` (S3-24) and BUT_MODEx/BAR/SHIFT.
   3. SHIFT: return 0 (it keeps its MODE-modifier role; LED layers stay).
   4. MODE: compute the target mode exactly as `handleModeButtons()` does
      (`(mode + 4) & 7` with SHIFT); if it is LOAD/SAVE, MENU or SOM, consume.
      Otherwise return 0 (navigation).
   5. BAR1/BAR2: consume while `cc_rowCount != 0` or while the source is an
      FX step; otherwise return 0.
   6. SEQ, SELECT, TRACK: dispatch on phase, mode and source kind exactly as
      the spec §7.1/§7.2 tables, via `cc_copyPress()` or `cc_clearPress()`.
      TRACK "navigate" in VOICE/STEP returns 0 (the normal handler selects the
      track); in PERF and EFFECTS a TRACK press that is not a source or paste
      is consumed (those modes use TRACK for mutes).
   7. Record consumed presses in the edge masks; return 1.
5. `cc_copyPress(kind_of_button, index)` — steps: no source yet → start the
   source (TRACK, PERF SEQ: commit at once and `cc_start()`; SEQ/SELECT rows:
   push to the row stack, set start/end per §4.2); source set → if this button
   pastes for this source kind and mode, build the destination (active Scene,
   active track, absolute step `buttonHandler_visibleStep(i)`, bar `i`, PERF
   Scene `i`, FX step `i`) and call `ccCopy_requestPaste()`; flash the
   destination LED; else ignore.
6. `cc_clearPress(kind_of_button, index)` — steps: EFFECTS SEQ →
   `ccClear_fxStepNow()` and `cc_start()`; otherwise open the clear menu for
   the object (`ccClear_menuForObject()`), selection 0 (`cancel`), capture the
   object (row stack for SEQ/SELECT), `cc_start()`, `menu_copyClearMenuChanged()`.
7. `copyClear_buttonReleased(buttonNr)` — steps: if the edge mask does not
   hold this button, return 0. Clear the bit. For SEQ/SELECT rows remove the
   button from the row stack; when the stack becomes empty: copy (no source
   yet) → commit the source, `cc_start()`, set the copy menu
   (`ccCopy_menuForSource()`), selection = default, `menu_copyClearMenuChanged()`;
   clear → if the selection is not `cancel`, `ccClear_requestClear()`.
   TRACK/PERF SEQ releases in a clear operation queue the clear the same way.
   Return 1.
8. `cc_start()` — sets `cc_state.started = 1` (suspension begins) once.
9. `copyClear_postEvent()` — steps: if phase is NONE return; re-assert LEDs
   (S3 step 2 rules; blink calls are idempotent); compute the group-blink masks
   for the visible source (step range in the shown bar of the source Scene and
   track in VOICE/STEP; bars in STEP; track LED; Scene LED in PERF; FX steps in
   EFFECTS on the source Scene) and call `led_setBlinkGroup()` for SEQ, SELECT
   and VOICE.
10. `copyClear_eventOverflow()` — zero the edge masks and the row stack.
11. `copyClear_menuVisible()`, `copyClear_formatMenu()` — row 0:
    `"COPY "` or `"CLR  "` then `cc_formatIndicator()` (spec §8.1 formats,
    one-based numbers, at most 8 characters); row 1: `[`, the 14-character
    padded label from `ccCopy_label()`/`ccClear_label()`, `]`.
12. `copyClear_ownsEncoder()`, `copyClear_encoderTurned(inc)` — clamp the
    selection to `0..count-1` (count from `ccCopy_selectionCount()` or
    `ccClear_selectionCount()`), then `menu_repaint()`.
13. `copyClear_ownsPots()`, `copyClear_potTurned(t)` — clear operation, no
    menu shown → `ccClear_potTurned(t)` and `cc_start()` if it registered
    anything; otherwise nothing.
14. `copyClear_backgroundSuspended()` — `(phase != NONE && started) ||
    ccSvc_busy()`.
15. `copyClear_serviceFinished()` — if phase is NONE: clear `cc_source`,
    `started = 0`, group blinks 0.
16. `copyClear_source()` — returns `&cc_source`.

Each static helper carries a short block naming its step in spec §4.2/§7.

### 5.4 `Core/Menu/CopyClear/copyOps.h` / `copyOps.c` (new; executors filled in Stages 4 and 8)

Header block:

```c
/*
 * copyOps.h — paste rules for Phase 6 copy (spec §4).
 *
 * What: the copy menus (labels, defaults, counts), turning a destination
 * press into a queued paste, automation retargeting and merge rules used by
 * the background service, and the Scene-level paste executors (Instrument,
 * Kit, Effect, Scene settings, Scene, FX steps) including edit-mask fan-out
 * and the edit-mask exchange. Why: copy rules are policy; the service owns
 * only sequencing, bounded pool work and the name write. Inputs: the source
 * from copyClearSession, destinations from the router, resident Scene data.
 * Outputs: queued jobs; committed Scene data through SceneData/Preset/
 * EffectsManager; name remaps. Affiliates: copyClearService.h, PatternData.h,
 * SceneData.h, presetManager.h, EffectsManager.h, BankData.h.
 */
```

Enums and prototypes:

```c
/* Step and bar copy selections (bar menus use the same values). */
typedef enum { CC_COPY_ALL = 0, CC_COPY_MERGE_ALL, CC_COPY_AUTO,
               CC_COPY_MERGE_AUTO } cc_copy_step_sel_t;
typedef enum { CC_COPY_TRACK = 0, CC_COPY_INSTRUMENT } cc_copy_track_sel_t;
typedef enum { CC_COPY_SCENE = 0, CC_COPY_SCENE_SETTINGS, CC_COPY_KIT,
               CC_COPY_EFFECT, CC_COPY_PATTERN } cc_copy_scene_sel_t;

cc_menu_t ccCopy_menuForSource(const cc_source_t *src);
uint8_t ccCopy_selectionCount(cc_menu_t menu);
const char *ccCopy_label(cc_menu_t menu, uint8_t selection);

/*
 * Queue one paste. Inputs: the operation's source, the selection shown at
 * the press, and the destination Scene/track/start. Output: nonzero when the
 * job was queued; zero when it was a no-op (identical to the source) or the
 * queue was full (dropped silently, spec §3.1). Caller: the router.
 */
uint8_t ccCopy_requestPaste(const cc_source_t *src, uint8_t selection,
                            uint8_t dst_scene, uint8_t dst_track,
                            uint8_t dst_start);
```

Stage 3 implementation: menu tables and `ccCopy_requestPaste()` (builds a
`cc_job_t`, rejects identical source/destination, `ccSvc_enqueue()`).
Retarget, merge and executors are added in S4-50..S4-53 and S8-xx.

Label tables (`static const char` 14-character rows), from spec §8.1:
step `step all`, `merge all`, `automation`, `merge auto`; bar `bar all`,
`merge all`, `automation`, `merge auto`; track `track`, `instrument`; Scene
`scene`, `settings`, `kit`, `effect`, `pattern`; FX `step`.

### 5.5 `Core/Menu/CopyClear/clearOps.h` / `clearOps.c` (new; executors filled in Stages 5–8)

Header block:

```c
/*
 * clearOps.h — clear rules for Phase 6 clear (spec §5, §6).
 *
 * What: the clear menus (labels, counts; every menu opens at `cancel`), the
 * object-to-menu mapping per mode, turning a released object into a queued
 * clear, the immediate EFFECTS SEQ clear, the pot-clear front end, and the
 * Scene-level clear executors (Scene, Scene settings, Effect, FX sequence,
 * send). Why: clear rules are policy; the service owns sequencing and pool
 * work. Inputs: held objects from copyClearSession, pot targets from Menu.
 * Outputs: queued jobs, immediate FX sequence clears, register entries.
 * Affiliates: copyClearService.h, EffectsManager.h, SceneData.h,
 * presetManager.h, BankData.h, menu.h.
 */
```

Enums and prototypes:

```c
typedef enum { CC_CLEAR_CANCEL = 0, CC_CLEAR_ALL, CC_CLEAR_AUTO,
               CC_CLEAR_NOTES, CC_CLEAR_SEND } cc_clear_obj_sel_t;
typedef enum { CC_CLEAR_SCENE_CANCEL = 0, CC_CLEAR_SCENE_ALL,
               CC_CLEAR_SCENE_SETTINGS, CC_CLEAR_SCENE_PATTERN,
               CC_CLEAR_SCENE_AUTOMATION, CC_CLEAR_SCENE_NOTES,
               CC_CLEAR_SCENE_FX, CC_CLEAR_SCENE_FX_SEQUENCE } cc_clear_scene_sel_t;

cc_menu_t ccClear_menuForObject(uint8_t mode, cc_kind_t kind);
uint8_t ccClear_selectionCount(cc_menu_t menu);
const char *ccClear_label(cc_menu_t menu, uint8_t selection);
uint8_t ccClear_requestClear(const cc_source_t *object, uint8_t selection);
void ccClear_fxStepNow(uint8_t scene, uint8_t step);          /* Stage 7 */
uint8_t ccClear_potTurned(const cc_pot_target_t *target);     /* Stage 6 */
```

Stage 3 implementation: tables, `ccClear_menuForObject()` (VOICE/STEP/PERF
TRACK → `CC_MENU_CLEAR_TRACK`, EFFECTS TRACK → `CC_MENU_CLEAR_TRACK_FX`,
SEQ in VOICE/STEP → `CC_MENU_CLEAR_STEP`, SELECT in STEP →
`CC_MENU_CLEAR_BAR`, SEQ in PERF → `CC_MENU_CLEAR_SCENE`),
`ccClear_requestClear()` (builds a `cc_job_t`, `ccSvc_enqueue()`); the other
two return without action until Stages 6 and 7.

### 5.6 `Core/Menu/CopyClear/copyClearService.h` / `copyClearService.c` (new; skeleton)

Header block:

```c
/*
 * copyClearService.h — background service for Phase 6 copy and clear.
 *
 * What: a FIFO of up to four pastes/clears, the eight-entry pot-clear
 * register, the 9 kB name buffer borrowed from the filesystem for the length
 * of an operation, bounded Pattern work under the Pattern Stack Service's
 * exclusive boundary (one Scene at a time), Scene-level executors waiting for
 * idle Preset workers, and the end-of-operation name write. Why: pastes and
 * clears must finish after the user releases the copy/clear button, and every
 * Pattern change must be bounded per tick and publication-safe (spec §9).
 * Inputs: jobs from copyOps/clearOps, register entries from clearOps.
 * Outputs: Pattern/Scene changes, LED/menu refresh requests, the busy state
 * that holds the AutoSave/maintenance suspension. Cadence: ccSvc_tick() at
 * 500 Hz from timebase_serviceFrontPanel(), after patSvc_tick().
 * Affiliates: PatternStackService.h, PatternData.h, filesystem.h, copyOps.h,
 * clearOps.h, copyClearSession.h.
 */
```

Types and prototypes:

```c
/* Job classes (high nibble of cc_job_t.op). */
#define CC_JOB_PASTE  0x10u
#define CC_JOB_CLEAR  0x20u

/*
 * One queued paste or clear (6 B). op: class | selection. kind: source kind
 * (paste) or object kind (clear). scene/track: destination (paste) or object
 * Scene/track (clear). start: destination start (paste) or object start.
 * end: object end (clear); unused for pastes (length comes from the source).
 */
typedef struct {
    uint8_t op;
    uint8_t kind;
    uint8_t scene;
    uint8_t track;
    uint8_t start;
    uint8_t end;
} cc_job_t;

void ccSvc_init(void);
void ccSvc_tick(void);
/* Queue one job; zero when the queue already holds four (dropped). */
uint8_t ccSvc_enqueue(const cc_job_t *job);
/* Nonzero while a job, the register, an apply wait or the name write remains. */
uint8_t ccSvc_busy(void);
/* Interaction ended: allow the name write and the final teardown. */
void ccSvc_interactionEnded(void);
/* Register (Stage 6). */
uint8_t ccSvc_registerAdd(uint16_t target, uint8_t scene);
uint8_t ccSvc_registerFull(void);
uint8_t ccSvc_targetPending(uint16_t target);
/* Names (Stage 9). */
void ccSvc_nameCopy(uint16_t dst_row, uint16_t src_row);
void ccSvc_nameContentChanged(uint16_t row);
```

Statics (ledger §2):

```c
/* FIFO of pending pastes/clears: 4 x 6 B plus head and count (26 B). */
static cc_job_t ccSvc_queue[4];
static uint8_t ccSvc_head;
static uint8_t ccSvc_count;
/* Pot-clear register: 8 targets, count, Scene (18 B). Stage 6. */
static uint16_t ccSvc_regTarget[8];
static uint8_t ccSvc_regCount;
static uint8_t ccSvc_regScene;
/*
 * Running-job state (11 B): phase, step cursor, track cursor, total steps,
 * flags (job active, boundary held, swap claimed, scratch borrowed, name
 * write needed, interaction ended), wait reason, name-write state,
 * compaction cursor (u16), spare.
 */
static struct { ... } ccSvc_run;
```

Stage 3 `ccSvc_tick()` steps:

1. If a job is active, run its executor; Stage 3 executors return DONE at
   once (no effect). On DONE pop the queue head.
2. Else if the queue is not empty: ensure the 9 kB name buffer is borrowed
   (`filesystem_borrowNameCacheScratch()`; wait if refused); start the head.
3. Else if the interaction has ended: return the name buffer
   (`filesystem_returnNameCacheScratch()`) if held, then
   `copyClear_serviceFinished()`.

`ccSvc_busy()` returns nonzero for a non-empty queue, an active job, a
non-empty register, a pending name write, or a borrowed buffer not yet
returned.

### 5.7 `buttonHandler.c` / `buttonHandler.h`

#### S3-20 `Core/Hardware/frontPanel/buttonHandler.c` line 25 — MODIFY include

- `#include "copyClearTools.h"` → `#include "copyClearSession.h"`.

#### S3-21 `buttonHandler.c` — REMOVE copy branches in `buttonHandler_partButtonPressed()` (lines 1019–1035), `buttonHandler_partButtonReleased()` (lines 1044–1046), `handleVoiceButton()` (lines 1072–1100)

- After removal, `buttonHandler_partButtonPressed()` contains only
  `handleSelectButton(partNr);`.
- Rationale: copy/clear presses never reach these handlers any more; the
  router consumes them first (S3-22).

#### S3-22 `buttonHandler.c` — ADD router calls at the top of `processPress()` (after line 1262) and `processRelease()` (after line 1488)

```c
    /*
     * S075: while the copy/clear button is held, copy/clear sees every press
     * and release first. A consumed edge must not reach step toggles, hold
     * timers, the VOICE edit-mask overlay, FX lane-lock holds, PERF Scene
     * switching, mutes or audition (spec §7). Unconsumed edges (navigation)
     * continue below unchanged. Affiliates: copyClear_buttonPressed()/
     * copyClear_buttonReleased(), copyClear_postEvent().
     */
    if (copyClear_buttonPressed(buttonNr))
        return;
```

(and the same with `copyClear_buttonReleased(buttonNr)` in `processRelease()`).

#### S3-23 `buttonHandler.c` — REPLACE the `BUT_COPY` cases: press lines 1352–1376, release lines 1557–1565; REMOVE the clear cancel at lines 1574–1577

- Press: keep the erase branch (SHIFT + recording + running) unchanged; every
  other press becomes `(void)copyClear_copyPressed(buttonHandler_getShift());`.
- Release: keep the erase stop; otherwise `copyClear_copyReleased();`.
- Remove lines 1574–1577 (`if (copyClear_Mode == MODE_CLEAR &&
  !btn_held[BUT_COPY]) ...`).

```c
    case BUT_COPY:
        /*
         * SHIFT + copy/clear while recording and running keeps its erase
         * meaning. Every other press arms a copy operation, or a clear
         * operation when SHIFT is held (S075, spec §3). The release ends menu
         * interaction; queued pastes/clears keep running in the background.
         * Affiliates: copyClear_copyPressed()/copyClear_copyReleased().
         */
```

- Rationale for the removed SHIFT-release lines: a clear operation no longer
  depends on SHIFT staying held (spec §3.2).

#### S3-24 `buttonHandler.c` / `.h` — ADD public index helpers

- ADD after `btn_to_voice()` (ends line 348): `int8_t
  buttonHandler_seqIndex(uint8_t buttonNr)`, `int8_t
  buttonHandler_selectIndex(uint8_t)`, `int8_t buttonHandler_voiceIndex(uint8_t)`
  returning the existing static mappings. Declare in `buttonHandler.h` after
  line 97 (`uint8_t buttonHandler_visibleStep(...)`).

```c
/*
 * Map a BUT_* number to its zero-based SEQ, SELECT or TRACK index, or -1.
 *
 * What: public wrappers over the private btn_to_seq/select/voice tables.
 * Why: copy/clear routes these rows before buttonHandler's own handlers and
 * must not duplicate the shift-register button order. Inputs: BUT_* number.
 * Outputs: index or -1. Clients: copyClearSession.c.
 */
```

#### S3-25 `buttonHandler.c` — MODIFY `buttonHandler_processEvents()` lines 1650–1655 and 1675–1678

- In the overflow block after line 1654: `copyClear_eventOverflow();`.
- After `processPress()`/`processRelease()` (lines 1675–1678):
  `copyClear_postEvent();`.

```c
        /* S075: copy/clear edge ownership is reset with the other pairings. */
```

```c
        /*
         * S075: re-assert copy/clear LEDs after any event; a mode change
         * clears blink slots, and navigation changes which source LEDs are
         * visible. Idempotent and cheap when no operation is held.
         */
```

### 5.8 Menu

#### S3-40 `Core/Menu/menu.c` line 33 — MODIFY include; lines 10 and 89 — REMOVE stale stub notes

- `#include "copyClearTools.h"` → `#include "copyClearSession.h"`.
- Remove line 10 (` *   - copyClear_isClearModeActive() → always returns 0`) and line 89
  (`// static inline uint8_t copyClear_isClearModeActive(void){return 0;}`).

#### S3-41 `menu.c` — ADD the copy/clear menu overlay to `menu_repaint()` after line 8321

```c
    /*
     * S075 copy/clear menu overlay.
     *
     * What: while copy/clear shows a menu, every repaint draws the two menu
     * rows instead of the page (the page state underneath is untouched).
     * Why: page repaints (knob service, search completion, mode changes)
     * keep happening during an operation and must not paint over the menu;
     * the old menu wrote the LCD directly and was overwritten.
     * Inputs: copyClear_menuVisible(), copyClear_formatMenu(). Output: one
     * sendDisplayBuffer() frame; no hardware cursor. Affiliates:
     * menu_copyClearMenuChanged()/Closed(), va_queueMarkerTransaction().
     */
    if (copyClear_menuVisible()) {
        memset(editDisplayBuffer, ' ', sizeof(editDisplayBuffer));
        copyClear_formatMenu(editDisplayBuffer[0], editDisplayBuffer[1]);
        cur_want_on = 0u;
        sendDisplayBuffer();
        return;
    }
```

#### S3-42 `menu.c` — ADD a guard at the top of `va_queueMarkerTransaction()` after line 2341

```c
    /*
     * S075: no CGRAM marker transaction while the copy/clear menu is shown;
     * the menu frame owns the display. menu_copyClearMenuClosed() invalidates
     * the marker cache so the next page repaint redefines what it needs.
     */
    if (copyClear_menuVisible())
        return;
```

#### S3-43 `menu.c` — ADD `menu_copyClearMenuChanged()`, `menu_copyClearMenuClosed()`, `menu_isStorageBusy()`; declare in `menu.h`

- Definitions after `menu_getActiveVoice()` (line 13564).
  `menu_copyClearMenuChanged()` calls `menu_repaint()`.
  `menu_copyClearMenuClosed()` sets `va_cgramValid = 0u`, clears
  `va_underlineSuppressed`, then `menu_repaintAll()`.
  `menu_isStorageBusy()` returns `menu_storageBusy`.
- Declarations in `menu.h` after line 541 (`uint8_t menu_getActiveVoice(void);`).

```c
/*
 * Copy/clear menu bridge (S075).
 *
 * menu_copyClearMenuChanged(): repaint after the copy/clear menu opened or
 * its selection changed. menu_copyClearMenuClosed(): the menu closed; drop
 * the CGRAM marker cache (the menu frame overwrote every marker cell) and
 * repaint the page in full. menu_isStorageBusy(): read-only view of the
 * Load/Save/Instrument storage lock, used to refuse an operation.
 * Clients: copyClearSession.c. Affiliates: menu_repaint() overlay branch,
 * va_queueMarkerTransaction().
 */
void menu_copyClearMenuChanged(void);
void menu_copyClearMenuClosed(void);
uint8_t menu_isStorageBusy(void);
```

#### S3-44 `menu.c` — REPLACE the clear-mode encoder block, lines 11124–11148

```c
    /*
     * S075: while the copy/clear button is held, copy/clear owns the encoder.
     * A turn changes the menu selection when a menu is shown; clicks are
     * ignored for now (user, B18); no parameter value or edit mode changes.
     * Affiliates: copyClear_ownsEncoder(), copyClear_encoderTurned().
     */
    if (copyClear_ownsEncoder()) {
        if (inc != 0)
            copyClear_encoderTurned(inc);
        return;
    }
```

#### S3-45 `menu.c` — ADD pot ownership to `menu_parseKnobDelta()` after line 11404

```c
    /*
     * S075: while the copy/clear button is held, no pot changes a value.
     * In a clear operation with no menu shown, the turn is a pot clear of the
     * parameter under the pot (resolved in Stage 6); otherwise it is ignored.
     */
    if (copyClear_ownsPots()) {
        /* Stage 6 resolves the target and calls copyClear_potTurned(). */
        return;
    }
```

#### S3-46 `menu.c` / `menu.h` — RENAME `menu_voiceAutoOverlayPatternDeleted()` (definition line 2267, comment 2255–2266; declaration `menu.h:483`, comment `menu.h:470–482`) to `menu_patternContentChanged()`

```c
/*
 * Restart the automation-presence search after Pattern content changed.
 *
 * What: restarts the bounded search shared by the VOICE pages (active track)
 * and the Effect page (all seven tracks), then repaints, so underlines follow
 * the new content once the rescan completes. Why: a paste or clear can add or
 * remove automation anywhere in the viewed Pattern. Inputs: none. Output: a
 * cleared search and a repaint on VOICE and Effect pages; nothing elsewhere.
 * Callers (S075): copyClearService.c after every Pattern paste/clear on the
 * viewed Scene. Affiliates: va_searchRestart(), va_scanService().
 */
void menu_patternContentChanged(void);
```

### 5.9 Suspension gates and the name buffer

#### S3-60 `Core/Hardware/SD/filesystem.c` — ADD include and gate every background scheduler in `filesystem_tick()`

- ADD `#include "copyClearSession.h"` beside the existing `menu.h` include.
- In `filesystem_tick()`, before line 25428, compute once:
  `const uint8_t cc_suspended = copyClear_backgroundSuspended();`
- MODIFY each admission so it also requires `!cc_suspended`: lines 25428
  (settings writer), 25447 (AutoSave trace flush), 25450 (Pattern trace
  flush), 25465 (deferred Load/Save HCNAMES flush), 25487 (scalar AutoSave),
  25490 (semantic Pattern AutoSave), 25495 (non-semantic Pattern AutoSave).
- While suspended set `fs_autosave_page_suppressed = 1u;` so the first
  scalar drain afterwards uses the 250 ms continuation deadline (existing
  behaviour at line 24702).

```c
    /*
     * S075 copy/clear suspension (spec §9.2).
     *
     * What: while a copy or clear operation runs (from its first copy object
     * press until every paste, clear, apply and name write has finished), no
     * background writer is admitted: settings.cfg, both trace flushes, the
     * deferred Load/Save HCNAMES flush (it would also use the borrowed 9 kB
     * name buffer), scalar AutoSave, and both Pattern AutoSave drains. A
     * writer already running finishes normally; nothing is pre-empted.
     * Why: user requirement; it also frees the name buffer for copy/clear.
     * Inputs: copyClear_backgroundSuspended(). Output: scheduler admission
     * only; dirty marks keep accumulating and are written afterwards.
     * Affiliates: copyClearService.c, patSvc_tick() repair gate.
     */
```

#### S3-61 `filesystem.c` — ADD the borrowed-buffer domain and API

- Enum line 203–215: ADD `FS_NAME_CACHE_COPYCLEAR,` before the legacy
  `FS_NAME_CACHE_HCNAMES` value.
- ADD `static uint8_t fs_name_cache_borrowed;` (1 B) beside
  `fs_list_cache_kind` (line 1664).
- ADD after `filesystem_clearNameCacheStorage()` (ends line 1750):
  `uint8_t *filesystem_borrowNameCacheScratch(void)` and
  `void filesystem_returnNameCacheScratch(void)`.
- Borrow steps: refuse (NULL) unless `status == FS_STATUS_IDLE` and not
  already borrowed; `filesystem_clearNameCacheStorage()`;
  `fs_list_cache_kind = FS_NAME_CACHE_COPYCLEAR`; set borrowed; return
  `(uint8_t *)fs_list_cache_name`. Return steps: clear storage
  (domain NONE); borrowed = 0.

```c
/*
 * Lend the 9,000 B name cache to copy/clear as working storage (S075).
 *
 * What: hands out the whole fs_list_cache_name array while a copy/clear
 * operation runs, tagged FS_NAME_CACHE_COPYCLEAR so every browser accessor
 * reports "not loaded" and Load/Save reloads its index on the next entry.
 * Why: during an operation AutoSave, the deferred HCNAMES flush and the
 * Load/Save pages cannot run (suspension, routing), so the cache is idle;
 * copy/clear needs up to 8,609 B for source snapshots and the name remap
 * (user, 2026-10-01). Inputs: none. Output: buffer pointer, or NULL while
 * the facade is busy or the buffer is already lent. Return clears it.
 * Clients: copyClearService.c only. Affiliates:
 * filesystem_requestCopyResidentNames() (Stage 9) reads the remap at offset
 * 0 and uses offsets 256.. during its own operation.
 */
```

- `filesystem.h`: ADD after line 1033 (`void filesystem_clearNameCache(void);`)
  the two prototypes with the same block in contract form, plus
  `#define FS_NAME_SCRATCH_BYTES 9000u` and a static assert in
  `filesystem.c` that `sizeof(fs_list_cache_name) == FS_NAME_SCRATCH_BYTES`.

#### S3-62 `Core/Bank/Scene/Pattern/PatternStackService.c` — MODIFY the repair gate at line 1612

- `if (menu_activePage == LOAD_PAGE || menu_activePage == SAVE_PAGE)` →
  add `|| copyClear_backgroundSuspended()`; ADD `#include
  "copyClearSession.h"` after line 21.

```c
    /*
     * S075: the repair epoch (reservations, repair relocations) is Pattern
     * maintenance and does not start while a copy/clear operation runs. The
     * cursor is kept; queue drain, barriers and handover continue.
     */
```

#### S3-63 `Core/Hardware/timebase.c` — ADD `ccSvc_tick()` after line 200 and its include after line 70

```c
    /*
     * S075: copy/clear background service at the same bounded 500 Hz
     * cadence, after the Pattern Stack Service pass so a finished queue
     * drain or handover is visible to it. Foreground only; never in TIM3.
     */
    ccSvc_tick();
```

#### S3-64 `main.c` — ADD `copyClear_init();` after line 1290 (`patSvc_init();`) and the include

```c
    /* S075: copy/clear state starts idle once the Pattern service exists. */
```

---

## 6. Stage 4 — Pattern pool reserve, exclusive boundary, raw block API, pastes

### 6.1 Swap block reserve

#### S4-01 `config.h` — ADD after line 304 (`#define PAT_DEFAULT_VELOCITY 100u`)

```c
/*
 * Permanent swap-block reserve at the top of every Scene's pool (S075).
 *
 * What: the last PAT_POOL_SWAP_CHUNKS four-byte chunks of the backed pool
 * (132 B, one largest possible block) are never handed out by normal
 * allocation, reservation repair or compaction. PAT_POOL_ALLOC_CHUNKS is the
 * allocatable chunk count; PAT_POOL_SWAP_OFFSET is the reserve's byte offset.
 * Why: a block can then always be rewritten without needing free pool space
 * while the old block is still live (write into the reserve, publish, free,
 * move back), so copy/clear removals and pastes never fail for lack of a
 * contiguous run (user, 2026-10-01). Copy/clear is the first user; the
 * reserve stays available to any later feature that needs the same
 * guarantee. The PAT4 format is unchanged: the reserve is free space.
 * Inputs: PAT_STACK_SIZE. Outputs: allocator, service and copy/clear bounds.
 * Affiliates: pat_poolAlloc(), PatternStackService.c PATSVC_ALLOC_CHUNKS,
 * copyClearService.c, PATTERN_DYNAMIC_STACK.md.
 */
#define PAT_POOL_SWAP_CHUNKS   33u
#define PAT_POOL_ALLOC_CHUNKS  ((uint16_t)(PAT_STACK_SIZE * 8u - PAT_POOL_SWAP_CHUNKS))
#define PAT_POOL_SWAP_OFFSET   ((uint16_t)(PAT_POOL_ALLOC_CHUNKS * 4u))
```

#### S4-02 `Core/Bank/Scene/Pattern/PatternData.c` — MODIFY `pat_poolAlloc()` line 235

- `uint16_t max_chunk = (uint16_t)(PAT_STACK_SIZE * 8u);` →
  `uint16_t max_chunk = PAT_POOL_ALLOC_CHUNKS;`

```c
    /* S075: never allocate inside the permanent swap-block reserve. */
```

#### S4-03 `PatternData.c` — MODIFY `pat_poolUsagePercent()` line 140

- Denominator `(PAT_STACK_SIZE * 8u)` → `PAT_POOL_ALLOC_CHUNKS`.

```c
    /* S075: report occupancy against the allocatable pool (reserve excluded). */
```

#### S4-03a `PatternData.c` — MODIFY `pat_tryAppendAutomation()`: ADD a bound before line 510

- ADD before the adjacency loop at line 510:
  `if ((uint32_t)(old_offset >> 2u) + new_chunks > PAT_POOL_ALLOC_CHUNKS) return 0u;`

```c
    /*
     * S075: in-place growth must not extend a block into the swap-block
     * reserve; the reserve chunks read as free in the bitmap. Falling back to
     * 0 lets pat_writeDynamic() allocate a new run below the reserve.
     */
```

#### S4-04 `PatternData.c` lines 1052–1095 and `PatternData.h` lines 216–227 — REMOVE the copy no-ops

- REMOVE `pat_copyTrack()`, `pat_copyPattern()`, `pat_copyBar()` and their
  prototypes. MODIFY the header comment (lines 216–220) to:

```c
/*
 * Range clear operations for UI and generators. Clear operations reset
 * address entries. (S075: copy is implemented by copyClearService.c through
 * the raw block API below; the Session-062 copy no-ops were removed.)
 */
```

### 6.2 PatternStackService: allocation bounds and the exclusive boundary

#### S4-10 `Core/Bank/Scene/Pattern/PatternStackService.c` — ADD `PATSVC_ALLOC_CHUNKS` after line 29

```c
/*
 * S075: allocatable chunk count. PATSVC_POOL_CHUNKS still sizes the bitmap
 * views; every search for free space, reservation or relocation target uses
 * PATSVC_ALLOC_CHUNKS so the swap-block reserve is never handed out.
 */
#define PATSVC_ALLOC_CHUNKS     PAT_POOL_ALLOC_CHUNKS
```

#### S4-11 `PatternStackService.c` — MODIFY search bounds

Replace `PATSVC_POOL_CHUNKS` with `PATSVC_ALLOC_CHUNKS` at: line 384
(density percent denominator), line 527 (`patSvc_largestFreeRun()` loop),
lines 548 and 550 (`patSvc_findFreeRun()`), lines 583 and 585
(`patSvc_findFreeRunReclaiming()`), line 759 (repair trailing bound), line 785
(repair relocation search), lines 1093 and 1153 (free-chunk classification:
`PATSVC_ALLOC_CHUNKS - used`). Lines 352, 368, 511, 687, 704, 811, 895 keep
`PATSVC_POOL_CHUNKS` (bitmap/reservation storage geometry).

```c
    /* S075: searches stop below the swap-block reserve (PATSVC_ALLOC_CHUNKS). */
```

#### S4-12 `PatternStackService.c` — ADD `service_exclusive_scene` after line 80

```c
/*
 * Exclusive copy/clear claim (S075; 1 B, 0xFF = none).
 *
 * What: the one Scene copy/clear is currently writing. Why: only one Scene's
 * Pattern may be modified at a time (user). For the service Scene the claim
 * uses the existing replace boundary (admission closed, queue drained); for
 * another Scene it also closes admission on the service Scene and holds any
 * playback handover into the claimed Scene until the claim ends.
 * Accessors: patSvc_beginExclusive(), patSvc_endExclusive(), patSvc_tick().
 */
#define PATSVC_NO_SCENE 0xFFu
static uint8_t service_exclusive_scene = PATSVC_NO_SCENE;
```

#### S4-13 `PatternStackService.c` — ADD `patSvc_beginExclusive()` / `patSvc_endExclusive()` after `patSvc_finishSceneReplace()` (ends line 1290)

Steps for begin(scene):
1. Reject an invalid Scene, or a claim on a different Scene, with 0.
2. Record `service_exclusive_scene = scene`.
3. If `scene == service_scene`: return `patSvc_prepareSceneReplace(scene)`
   (closes admission; ready when queue, bulk and reactive work are idle).
4. Otherwise: close admission on the service Scene the same way
   (`service_open = 0`, `service_handover = 1`); return ready when the queue,
   bulk and reactive work are idle.
5. On the first ready return, clear the reservation image
   (`patSvc_clearReservationImage()`): copy/clear may use every free chunk
   below the reserve (user, A2); the rebuild runs after the suspension ends.

Steps for end(scene):
1. Ignore a Scene that is not claimed.
2. If it is the service Scene: `patSvc_finishSceneReplace(scene)`.
3. Otherwise recount nothing (the claimed Scene is recounted at its next
   handover) and leave handover mode so `patSvc_tick()` can reopen admission.
4. `service_exclusive_scene = PATSVC_NO_SCENE`.

```c
/*
 * Exclusive Pattern access for copy/clear (S075, spec §9.4).
 *
 * What: grants one caller sole write access to one Scene's Pattern region
 * (address array, pool, bitmap, track settings) between begin and end. While
 * the claim exists, no queued edit, barrier, repair or reactive step touches
 * any pool, and playback cannot be handed over into the claimed Scene.
 * Why: copy/clear pastes and clears run over many ticks with the raw block
 * API and must be the only writer; the existing replace boundary covered only
 * the service Scene. Inputs: a resident Scene. Outputs: begin returns 1 when
 * the caller may write (repeat until it does); end releases the claim.
 * Clients: copyClearService.c. Affiliates: patSvc_prepareSceneReplace(),
 * patSvc_finishSceneReplace(), patSvc_tick() handover branch, the raw block
 * API in PatternData.h.
 */
```

#### S4-14 `PatternStackService.c` — MODIFY the handover branch of `patSvc_tick()` after line 1541

- ADD `if (service_exclusive_scene != PATSVC_NO_SCENE) return;` after
  `if (service_replace_pending) return;`.

```c
        /*
         * S075: while copy/clear holds an exclusive claim, the handover does
         * not complete (it would reopen admission or adopt the claimed Scene
         * as the service target mid-write).
         */
```

#### S4-15 `PatternStackService.c` — ADD exclusive-holder helpers at the end of the file

`uint8_t patSvc_exclusiveCompactStep(uint8_t scene)` and
`uint8_t patSvc_exclusiveEvacuateSwapStep(uint8_t scene)`.

- Compact steps: (1) require the claim on `scene`; (2) from a retained cursor,
  inspect up to `PAT_COMPACT_SCAN_PER_TICK` address entries; relocate the
  first block that can move lower with `patSvc_relocateIndex(scene, i, 0, 1,
  ...)`; (3) return 1 when a block moved, 0 when the cursor reached the end
  without a move (cursor resets).
- Evacuate steps: find the next address entry whose block overlaps chunks
  `PATSVC_ALLOC_CHUNKS..PATSVC_POOL_CHUNKS-1`; relocate it with
  `patSvc_findFreeRun()` (which stays below the reserve); return 1 when moved,
  0 when no block overlaps the reserve, 2 when an overlapping block cannot be
  moved (no room).
- The cursor reuses `reactive_scan_cursor` (reactive recovery cannot run
  during a claim: the queue is drained).

```c
/*
 * Bounded compaction and swap-reserve evacuation for the exclusive holder.
 *
 * What: one relocation per call, using the same write-new / publish / free-old
 * relocation as reactive recovery. Compact moves a published block lower to
 * coalesce free space; evacuate moves a block that overlaps the swap reserve
 * (possible only in Patterns saved before S075) below it. Why: a step-by-step
 * paste whose new block has no contiguous run waits in the swap block while
 * compaction makes one; the swap block must be empty before first use.
 * Inputs: the claimed Scene. Outputs: 1 moved, 0 nothing to move, 2 (evacuate
 * only) cannot move. Clients: copyClearService.c. Affiliates:
 * patSvc_relocateIndex(), patSvc_findFreeRun().
 */
```

#### S4-16 `PatternStackService.h` — ADD prototypes after line 76

The contract blocks from S4-13 and S4-15 go above:

```c
uint8_t patSvc_beginExclusive(uint8_t scene);
void patSvc_endExclusive(uint8_t scene);
uint8_t patSvc_exclusiveCompactStep(uint8_t scene);
uint8_t patSvc_exclusiveEvacuateSwapStep(uint8_t scene);
```

### 6.3 PatternData raw block API (exclusive holder only)

#### S4-20 `PatternData.h` — ADD before line 313 (`#endif`)

```c
/*
 * Raw block API for the exclusive copy/clear holder (S075).
 *
 * What: read, decode, encode and place whole step blocks, publish empty steps,
 * and copy or reset a whole region, always in publication order (write new,
 * PRIMASK swap of the 16-bit address entry, free old) so a reader (TIM3
 * playback now; chaining or per-track playback later) sees a complete old or
 * new step. Why: pastes and clears replace complete steps and whole regions;
 * the single-entry pat_* setters cannot express that, and the swap-block
 * reserve needs a placement path. Inputs: resident Scene/track/step and
 * caller buffers of PAT_RAW_BLOCK_MAX bytes. Outputs: committed steps, dirty
 * marks (pat_markSceneDirty) and results described per function.
 * Restriction: callable only between patSvc_beginExclusive(scene) == 1 and
 * patSvc_endExclusive(scene). Clients: copyClearService.c. Affiliates:
 * pat_blockWrite(), pat_poolAlloc(), pat_poolFree(), PAT_POOL_SWAP_OFFSET.
 */
#define PAT_RAW_BLOCK_MAX 132u
#define PAT_RAW_TRIGGER_KEEP 0u   /* keep the live trigger bit */
#define PAT_RAW_TRIGGER_OFF  1u
#define PAT_RAW_TRIGGER_ON   2u

/* Copy one live block (bytes = chunks * 4) and the entry; 0 when no block. */
uint8_t pat_rawReadBlock(uint8_t scene, uint8_t track, uint8_t step,
                         uint8_t out[PAT_RAW_BLOCK_MAX], uint16_t *entry_out);
/* Allocated byte size of an encoded block (from its flags and count). */
uint8_t pat_rawBlockBytes(const uint8_t *block);
/* Decode specials and automation; returns the automation count. */
uint8_t pat_rawDecode(const uint8_t *block, pat_step_specials_t *specials,
                      pat_automation_entry_t *autos, uint8_t capacity);
/* Encode a block (back-reference left 0; placement sets it); returns bytes,
 * 0 for an empty block (no specials and no automation). */
uint8_t pat_rawEncode(uint8_t out[PAT_RAW_BLOCK_MAX], uint8_t flags,
                      uint8_t note, uint8_t velocity, uint8_t probability,
                      const pat_automation_entry_t *autos, uint8_t count);
/* Allocate below the reserve, write, publish, free old. 0 = no run. */
uint8_t pat_rawPlace(uint8_t scene, uint8_t track, uint8_t step,
                     const uint8_t *block, uint8_t trigger_mode);
/* Same through the swap block (must be free); always succeeds. */
uint8_t pat_rawPlaceViaSwap(uint8_t scene, uint8_t track, uint8_t step,
                            const uint8_t *block, uint8_t trigger_mode);
/* Move the block now living in the swap block to a run below it; 0 = no run. */
uint8_t pat_rawSwapReturn(uint8_t scene, uint8_t track, uint8_t step);
/* Publish "no block" with the trigger policy, then free the old block. */
void pat_rawPublishEmpty(uint8_t scene, uint8_t track, uint8_t step,
                         uint8_t trigger_mode);
/* Free chunks below the reserve; nonzero when the whole reserve is free. */
uint16_t pat_rawFreeChunks(uint8_t scene);
uint8_t pat_rawSwapFree(uint8_t scene);
/* Whole-region helpers (spec §9.8), each one bounded pass. */
void pat_rawRegionSilence(uint8_t dst);                 /* all entries empty */
void pat_rawRegionCopyBody(uint8_t src, uint8_t dst);   /* pool, bitmap, params */
void pat_rawRegionPublishSteps(uint8_t src, uint8_t dst, uint16_t first,
                               uint16_t count);          /* addresses last */
void pat_rawRegionReset(uint8_t dst);                    /* silence + init */
/* Rewrite one not-yet-published block in dst in place (retarget/shrink). */
uint8_t pat_rawRegionRewriteBlock(uint8_t dst, uint16_t address_index,
                                  const pat_automation_entry_t *autos,
                                  uint8_t count);
```

#### S4-21 `PatternData.c` — ADD the raw block API at the end of the file (after line 1439)

Implementation steps per function (each with a short block pointing to the
header contract):

- `pat_rawReadBlock`: `pat_addrPtr()`; if bit 14 clear or offset invalid →
  `*entry_out = entry`, return 0; else copy `pat_blockChunks(flags,count)*4`
  bytes, return that size.
- `pat_rawBlockBytes`: `pat_blockChunks(block[2], block[1] & 0x3F) * 4`.
- `pat_rawDecode`: specials as `pat_blockRead()`, automation as
  `pat_blockReadAutomations()` but from a caller buffer (factor both existing
  helpers to take `const uint8_t *p` so pool and buffer share one decoder).
- `pat_rawEncode`: `pat_blockWrite()` logic into a caller buffer (factor
  `pat_blockWrite()` the same way; step id 0).
- `pat_rawPlace`: `pat_poolAlloc(r, chunks)`; on failure return 0; copy bytes;
  set the 10-bit back-reference to `track*128+step`; PRIMASK: read the live
  trigger, compose `trigger | SPECIALS_BIT | offset` per `trigger_mode`,
  store; free the old block if it existed; `pat_markSceneDirty()`; return 1.
- `pat_rawPlaceViaSwap`: as above at `PAT_POOL_SWAP_OFFSET`, marking the
  swap chunks occupied in the bitmap (the reserve is outside allocation, so
  the bitmap bits are free when the swap block is empty).
- `pat_rawSwapReturn`: allocate the block's chunks below the reserve; copy
  from the swap offset; publish (trigger KEEP); clear the swap chunks' bitmap
  bits and zero the bytes.
- `pat_rawPublishEmpty`: PRIMASK publish of `trigger | 0x3FFF`; free the old
  block (same detach-before-free order as `pat_releaseStepDynamic()`).
- `pat_rawFreeChunks` / `pat_rawSwapFree`: bitmap popcount over
  `0..PAT_POOL_ALLOC_CHUNKS-1` / test of the reserve bits.
- `pat_rawRegionSilence`: every entry of dst → `0x3FFF` (trigger off), one
  aligned store each (PRIMASK per entry not required: a single halfword store
  is atomic).
- `pat_rawRegionCopyBody`: memcpy pool, bitmap, track settings,
  `pattern_change_bar`, `pattern_next` from src to dst.
- `pat_rawRegionPublishSteps`: copy `count` address entries from src to dst
  starting at flat index `first`; `pat_markSceneDirty(dst)` at the end.
- `pat_rawRegionReset`: silence, then `pat_initScene(dst)`, then
  `pat_markSceneDirty(dst)`.
- `pat_rawRegionRewriteBlock`: re-encode the entries into the existing block
  (same flags and specials); if it shrinks, clear the tail chunks' bitmap
  bits; if count is 0 and there are no specials, free the block and mark the
  source entry for publication as empty (the caller publishes it).

### 6.4 Paste engine in `copyClearService.c`

#### S4-40 Scratch layout (constants in `copyClearService.c`)

```c
/*
 * Layout of the borrowed 9 kB name buffer while an operation runs (S075).
 *
 *   [0    .. 160]   HCNAMES row remap (session lifetime, Stage 9)
 *   [256  .. 511]   source step table: one u16 per source step: bit 15
 *                   trigger, bit 14 has block, bits 10..0 block offset / 4
 *   [512  .. 8703]  source blocks, already retargeted (a track's blocks can
 *                   never exceed one pool, 8,192 B)
 *   FX range snapshot (288 B) and Kit/Effect use start at 512 too.
 * Why: a snapshot taken when the paste starts makes reversal, overlap and
 * wrap safe without pairwise ordering, and keeps every write in publication
 * order. Owner: copyClearService.c. Affiliate: FS_NAME_SCRATCH_BYTES.
 */
#define CC_SCRATCH_REMAP_OFFSET   0u
#define CC_SCRATCH_TABLE_OFFSET   256u
#define CC_SCRATCH_BLOCK_OFFSET   512u
_Static_assert(CC_SCRATCH_BLOCK_OFFSET + PAT_STACK_SIZE * 32u <=
               FS_NAME_SCRATCH_BYTES, "copy/clear scratch must fit the name buffer");
```

#### S4-41 Paste job phases (`ccSvc_runPatternPaste()`, static)

Applies to step, step range, bar, bar range and `copy track`. Bounded to 8
steps per tick per phase (16 for snapshot reads).

1. **Plan** (one pass): from the source (`copyClear_source()`) and the job:
   step list order (start → end, bars fully reversed when start > end,
   `copy track` = steps 0..127), count N, destination step `(start + i) % 128`.
   Retarget context: source/destination Scene and slot (`track < 6 ? track :
   5`).
2. **Claim**: `patSvc_beginExclusive(job.scene)` until 1. If the reserve is
   occupied (`!pat_rawSwapFree()`), run `patSvc_exclusiveEvacuateSwapStep()`
   each tick; a result of 2 drops the job (end claim).
3. **Snapshot**: for each source step `pat_rawReadBlock()` from the source
   Scene; decode, `ccCopy_retargetEntries()` (when Scene or track differs),
   re-encode compact into the block area; write the table entry.
4. **Check**: for each destination step compute the new block size per
   selection (S4-52) and the live old size; keep the running sum of
   new − old chunks and its maximum. If the maximum exceeds
   `pat_rawFreeChunks(job.scene)`, drop the job (nothing changed).
5. **Place**: for each step build the final block (snapshot block, or the
   merge result in a 132 B stack buffer) and its trigger mode
   (`copy … all`: ON/OFF from the source; merge all: ON when the source is
   on, else KEEP; automation selections: KEEP). Empty → `pat_rawPublishEmpty()`.
   Otherwise `pat_rawPlace()`; when it returns 0 use `pat_rawPlaceViaSwap()`,
   then call `pat_rawSwapReturn()` each tick, running
   `patSvc_exclusiveCompactStep()` between attempts, before moving on.
6. **Finish**: step/bar pastes extend the destination track length to
   `max(length, 16 * (highest written bar + 1))` (highest bar = 7 when the
   range wrapped); `copy track` copies `track_length`, `track_scale`,
   `track_shuffle` with `pat_setTrackLength/Scale/Shuffle()`;
   `patSvc_endExclusive()`; if the destination is the viewed Scene:
   `menu_patternContentChanged()`, `led_updatePatternTrack(menu_getActiveVoice(),
   menu_getViewedPattern(), buttonHandler_selectedStep)` in VOICE/STEP,
   `pat_applyTrackSettingsToMenu()`.

```c
/*
 * Run one step/range/bar/track paste in bounded steps (spec §9.5).
 *
 * What: snapshot the source into the borrowed buffer (retargeted), check that
 * the worst running pool growth fits, then replace each destination step in
 * publication order, using the swap block and compaction when no contiguous
 * run exists, so a paste either completes or is dropped before any change.
 * Why: user rule "always step by step"; the reserve makes every step
 * placeable once the total fits. Inputs: the queue-head job, the operation's
 * source. Outputs: committed steps, track length/settings, UI refresh.
 * Returns DONE, WAIT (call again next tick) or DROP. Caller: ccSvc_tick().
 * Affiliates: patSvc_beginExclusive(), the raw block API, ccCopy_retarget*,
 * ccCopy_mergeStep().
 */
```

#### S4-50 `copyOps.c` — ADD retargeting (`ccCopy_retarget()`, `ccCopy_retargetEntries()`)

```c
/*
 * Retarget automation for a different destination track or Scene (spec §9.7).
 *
 * What: maps one source target ID to the destination: the source track's own
 * slot moves to the destination track's slot (track 7 = slot 6); other slots
 * stay; per-voice Scene targets (Nvm, Nou, Nfx) move with the slot; `7dc` and
 * `_choke` parameters map only to the destination track 7's alternate decay;
 * Scene-wide targets and the off value stay; Effect locals map by key when
 * the Effect types differ. Instrument parameters match by same type and
 * index, else same file key, else same VOICE page position; the match must be
 * automatable and have the same dtype, otherwise the entry is dropped.
 * Why: user rule (A3, F8). Inputs: context (source/destination Scene, track,
 * slot), source target. Outputs: 1 + destination target, or 0 (drop).
 * ccCopy_retargetEntries() applies it to a decoded list in place, removes
 * dropped entries, and keeps the first of any duplicates. Clients:
 * copyClearService.c snapshot and whole-Pattern rewrite. Affiliates:
 * instrumentManager_descriptor(), instrumentManager_descriptorIndexByKey(),
 * instrumentManager_voicePageDescriptorIndex(),
 * instrumentManager_chokeDescriptorIndexForBase(), effects_descriptor(),
 * effects_descriptorByKey(), sceneModTarget_descriptor().
 */
typedef struct {
    uint8_t src_scene, dst_scene, src_slot, dst_slot, src_track, dst_track;
} cc_retarget_ctx_t;
uint8_t ccCopy_retarget(const cc_retarget_ctx_t *ctx, uint16_t src_target,
                        uint16_t *dst_target);
uint8_t ccCopy_retargetEntries(const cc_retarget_ctx_t *ctx,
                               pat_automation_entry_t *autos, uint8_t count);
```

Implementation steps for `ccCopy_retarget()`:
1. Off value (`PAT_AUTOMATION_TARGET_OFF`): keep.
2. Voice target (`instrumentParam_isVoiceParameter()`): slot `s`, local `l`.
   Destination slot `d = (s == src_slot) ? dst_slot : s`. Source descriptor
   from the source Scene's slot type; destination type from the destination
   Scene's slot `d`. Same type → same `l`. Else key lookup
   (`instrumentManager_descriptorIndexByKey()`), else page position (find
   the `(page, position)` of `l` in the source type with
   `instrumentManager_voicePageDescriptorIndex()`, then the descriptor at the
   same position in the destination type). Check AUTOMATABLE flag and equal
   `dtype`. Return `instrumentParam_make(d, l')`.
3. Track-7 alternates: a source on track 7 whose target is the slot-6
   `_choke` descriptor or `7dc` maps only when the destination track is 7, to
   the destination slot-6 type's alternate (choke descriptor via
   `instrumentManager_chokeDescriptorIndexForBase()`, or `7dc` for a
   non-Choke type with a base decay); otherwise drop.
4. Scene target: per-voice kinds with `voice_slot == src_slot` move to
   `dst_slot` (same kind); the retired row drops; others stay.
5. Effect local (`effectTarget_isEffectId()`): same type → keep; else
   destination descriptor by `base.file_key`, must be automatable with equal
   `base.dtype`; else drop.

#### S4-51 `copyOps.c` — ADD `ccCopy_mergeStep()`

```c
/*
 * Build the destination block for one step under a merge or automation
 * selection (spec §4.4).
 *
 * What: `merge … all` combines triggers (OR), lets each source special win and
 * keeps destination-only specials, and forms the automation union with the
 * source winning on equal targets, source entries first, destination entries
 * beyond 63 dropped silently; `copy … automation` keeps the destination
 * specials and takes the source automation; `merge automation` keeps the
 * destination specials and forms the union. Why: user rules B5. Inputs: the
 * decoded source (snapshot) and destination (live) steps. Outputs: encoded
 * block bytes in a caller buffer (0 = empty) and whether the source trigger
 * applies. Uses one 63-entry stack buffer. Caller: copyClearService.c check
 * and place phases. Affiliates: pat_rawDecode(), pat_rawEncode().
 */
uint8_t ccCopy_mergeStep(uint8_t selection,
                         const uint8_t *src_block, uint8_t src_trigger,
                         const uint8_t *dst_block,
                         uint8_t out[PAT_RAW_BLOCK_MAX], uint8_t *trigger_mode);
```

#### S4-52 `copyClearService.c` — the check phase uses `ccCopy_mergeStep()` to size merges

- Replace-all size = snapshot block size; automation/merge sizes come from a
  dry run of `ccCopy_mergeStep()` into the stack buffer.

#### S4-53 `copyOps.c` — route step/bar/`copy track` jobs to the Pattern paste engine

- `ccCopy_requestPaste()`: for `CC_KIND_STEP`, `CC_KIND_BAR` and
  `CC_KIND_TRACK` with `CC_COPY_TRACK`, the job class is the Pattern paste;
  `CC_COPY_INSTRUMENT` and Scene kinds go to Stage 8 executors; FX steps to
  Stage 7.

---

## 7. Stage 5 — Pattern clears

#### S5-01 `copyClearService.c` — ADD `ccSvc_runPatternClear()` (static)

Applies to `clear step|bar|track` (all / automation / notes) and the PERF
Pattern clears (`clear pattern`, `clear automation`, `clear notes`) on any
Scene. Bounded to 8 steps per tick.

1. **Plan**: step list = object range (steps; bars × 16; track = 128; PERF
   automation/notes = 7 × 128); order does not matter for clears.
2. **Claim**: `patSvc_beginExclusive(job.scene)` until 1. Selections that
   rewrite blocks (automation, notes) require an empty swap reserve: evacuate
   as in S4-41 step 2; a result of 2 skips those rewrites (spec §9.6).
3. **Per step**:
   - all → `pat_rawPublishEmpty(scene, t, s, PAT_RAW_TRIGGER_OFF)`;
   - automation → decode the live block; if it has no automation, skip; encode
     with its specials and no automation; empty → `pat_rawPublishEmpty(KEEP)`;
     else `pat_rawPlace(KEEP)`, falling back to `pat_rawPlaceViaSwap(KEEP)` +
     `pat_rawSwapReturn()` (always succeeds: the freed old run is at least as
     large as the new block);
   - notes → as automation but keep the automation, drop the specials, trigger
     OFF; a step with no block gets `pat_rawPublishEmpty(OFF)`.
4. **PERF `clear pattern`**: one pass `pat_rawRegionReset(job.scene)`.
5. **Finish**: `clear track` (all) resets the track settings with
   `pat_setTrackLength(16)`, `pat_setTrackScale(TRACK_SCALE_DEFAULT)`,
   `pat_setTrackShuffle(0)`; `patSvc_endExclusive()`; the viewed-Scene UI
   refresh as S4-41 step 6. Names are not changed (spec §9.10); the Pattern
   row loses its refreshed flag through `autosave_markPatternDirty()` already.

```c
/*
 * Run one Pattern clear in bounded steps (spec §5, §9.6).
 *
 * What: empties steps (all), strips automation (automation) or triggers and
 * specials (notes) over a step range, a track, or the whole Pattern; resets a
 * whole Pattern; and resets track settings for `clear track`. Every rewrite is
 * publication-safe and uses the swap block when no other run is free.
 * Why: user clear rules; Pattern clears never fan out. Inputs: the queue-head
 * job (object Scene, track, range, selection). Outputs: committed steps,
 * track settings, UI refresh. Returns DONE or WAIT. Caller: ccSvc_tick().
 * Affiliates: patSvc_beginExclusive(), the raw block API.
 */
```

#### S5-02 `clearOps.c` — MODIFY `ccClear_requestClear()`

- Map objects and selections to jobs: TRACK/STEP/BAR objects with ALL, AUTO,
  NOTES → Pattern clear job on the object Scene (the active Scene at press
  time) and track; SCENE object with PATTERN, AUTOMATION, NOTES → Pattern
  clear job on that Scene, all tracks; `CANCEL` → nothing.

---

## 8. Stage 6 — Pot-clear register and underline suppression

#### S6-01 `Core/Bank/Scene/SceneModTargets.c` / `.h` — ADD `sceneModTarget_effectMorphId()`

- ADD after `sceneModTarget_voiceMorphId()` (ends line 215); declare after its
  prototype in the header.

```c
/*
 * Resolve the Scene target ID of the Effect Morph amount `fxm` (S075).
 *
 * Output: the canonical ID (404 today) found by kind, so callers do not depend
 * on table order. Clients: Menu's pot-clear target resolver.
 */
uint16_t sceneModTarget_effectMorphId(void);
```

#### S6-02 `Core/Menu/menu.c` — ADD `menu_knobClearTarget()` (static) and use it in S3-45

- ADD before `menu_parseKnobDelta()` (line 11396). Steps: resolve the cell
  under the knob exactly as `menu_parseKnobDelta()` does (`activePage`,
  `is2ndPage`, `menu_resolveCell()`); then:
  - `MENU_CELL_INSTRUMENT`: descriptor of the page's slot type; if
    AUTOMATABLE → `instrumentParam_make(slot, descriptor_index)`;
  - `MENU_CELL_SCENE_SETTING` → `menu_sceneSettingAutomationTarget(&cell)`;
  - `MENU_CELL_KIT_SETTING` with `MENU_KIT_SETTING_SLOT6_TRACK7_AMP_DECAY` →
    `7dc` (Scene target row 7);
  - `MENU_CELL_STATIC`: `PAR_VOICE1..6_MORPH` →
    `sceneModTarget_voiceMorphId(n)`; `PAR_EFFECT_MORPH` →
    `sceneModTarget_effectMorphId()` and FX lane 0 (F4); anything else none;
  - `MENU_CELL_EFFECT`: `MENU_FX_CELL_PARAM` index `L` → if
    `effects_paramAutomatable(type, L)` and `L < 63`, target
    `EFFECT_TARGET_ID_BASE + L`; lane from `effects_laneOfParam()`;
    `MENU_FX_CELL_MORPH_AMOUNT` → `fxm` and lane 0; other kinds none.
- MODIFY the S3-45 block: `cc_pot_target_t t; if (delta != 0 &&
  menu_knobClearTarget(knobNr, &t)) copyClear_potTurned(&t); return;`

```c
/*
 * Resolve the automation targets under one endless pot for a pot clear.
 *
 * What: maps the visible cell to the Pattern automation target it would
 * record and, on the Effect page or the PERF `fxm` cell, the FX sequence lane.
 * Non-automatable cells (PERF `mrp`, selectors, `typ`, `run`, `len`, `scl`,
 * fader mode) resolve to nothing, so the turn does nothing (user, B14).
 * Why: a pot clear must name exactly the parameter whose underline the user
 * sees, without changing its value. Inputs: knob number and the current
 * page/sub-page. Output: nonzero with *out filled, or zero. Caller:
 * menu_parseKnobDelta() copy/clear branch. Affiliates:
 * menu_sceneSettingAutomationTarget(), sceneModTarget_*Id(),
 * effects_paramAutomatable(), effects_laneOfParam().
 */
```

#### S6-03 `menu.c` — ADD `menu_automationTargetCleared()`; declare in `menu.h`

- ADD after `menu_patternContentChanged()` (renamed in S3-46). Steps: on a
  VOICE page, clear the search bit of a voice target whose slot is the page's
  slot, or the Scene-mask bit for the page slot's Nvm/Nou/Nfx; on the Effect
  page, clear the local bit of `448 + L` or the `fxm` Scene-mask bit; then
  `menu_repaint()`.

```c
/*
 * Drop one target's underline now, without restarting the search (S075).
 *
 * What: clears the presence bit for one target in the current search result
 * and repaints. Why: a pot clear removes the underline at the turn (spec §6)
 * while the background removal runs; restarting the whole search would make
 * every other underline vanish until the rescan completes. The search loop
 * filters pending targets (S6-04), so the bit cannot come back before the
 * removal finishes. Inputs: Pattern target ID. Output: repaint.
 * Caller: ccClear_potTurned(). Affiliates: va_searchTargetMask[],
 * va_searchSceneMask, ccSvc_targetPending().
 */
void menu_automationTargetCleared(uint16_t target);
```

#### S6-04 `menu.c` — MODIFY `va_scanService()`: ADD a filter after line 2039

```c
            /*
             * S075: a target waiting in the pot-clear register is being
             * removed; never record it, so its underline stays off until the
             * removal is done.
             */
            if (ccSvc_targetPending(autos[i].target))
                continue;
```

- ADD `#include "copyClearService.h"` beside the S3-40 include.

#### S6-05 `clearOps.c` — IMPLEMENT `ccClear_potTurned()`

Steps: (1) no Pattern target and no lane → return 0; (2) Pattern target
already pending → return 1; (3) `ccSvc_registerFull()`, or the register holds
entries for a different Scene than the viewed Scene → return 0 (nothing
happens and the underline stays; user rule); (4) lane valid →
`effects_clearSeqLanes(scene_getActiveIndex(), 0xFFFF, 1u << lane)` (fans out,
Stage 7); (5) Pattern target valid → `ccSvc_registerAdd(target,
menu_getViewedPattern())` and `menu_automationTargetCleared(target)`;
(6) return 1.

```c
/*
 * Start one pot clear (spec §6).
 *
 * What: clears the parameter's FX sequence lane at once (fanning out through
 * the edit mask) and registers its Pattern target for background removal
 * from the viewed Scene's Pattern (never fanned out). Why: user rules B14, F4.
 * Inputs: resolved targets. Output: nonzero when something was cleared or
 * registered; zero when the register is full (nothing happens).
 * Caller: copyClear_potTurned(). Affiliates: ccSvc_registerAdd(),
 * effects_clearSeqLanes(), menu_automationTargetCleared().
 */
```

#### S6-06 `copyClearService.c` — IMPLEMENT the register and its drain

- `ccSvc_registerAdd()`: refuse when 8 entries exist; set `ccSvc_regScene`
  on the first entry; append. `ccSvc_registerFull()`, `ccSvc_targetPending()`
  scan the 8 entries.
- Drain (`ccSvc_runRegister()`, static), run when no queued job is active:
  claim `ccSvc_regScene`; walk 7 tracks × 128 steps, 8 per tick; for a step
  whose block holds the head entry's target, re-encode without it and place
  it as in S5-01 (automation rewrite); at the end remove the head entry (shift
  the rest), end the claim, repaint if the Scene is viewed.

```c
/*
 * Pot-clear register (spec §6; 18 B, approved).
 *
 * What: up to eight Pattern targets waiting for removal from one Scene,
 * drained one at a time (about 224 ms each at 8 steps per tick).
 * Why: a Pattern-wide removal is bounded background work; the register keeps
 * the underline off until the target is gone and limits outstanding work.
 * Inputs: ccSvc_registerAdd(). Outputs: removed automation entries.
 * Accessors: ccClear_potTurned(), va_scanService() (ccSvc_targetPending()),
 * ccSvc_busy().
 */
```

---

## 9. Stage 7 — FX sequence copy and clears; Effect fan-out

#### S7-01 `Core/DSP/Effects/EffectsManager.c` — MODIFY `effects_fanoutMask()` lines 514–536

- Replace lines 523–524 (`if (scene_index == scene_getActiveIndex()) mask |=
  bank_sceneMaskVoiceEdit();`) with: active Scene → `bank_sceneMaskVoiceEdit()`;
  any other Scene → `bank_sceneMaskVoiceEditForScene(scene_index)`.

```c
    /*
     * S075 (user, F5): an edit, paste or clear on any Scene fans out through
     * that Scene's own edit-mask entry. The edit mask locks Scenes together
     * for everything except the Pattern, so a PERF paste onto a Scene that is
     * not active must reach that Scene's members too. The active Scene keeps
     * the self-repairing getter.
     */
```

#### S7-02 `EffectsManager.c` — ADD fan-out record and sequence operations after `effects_setSeqLaneLock()` (ends line 904)

Four functions, each computing the mask first with `effects_fanoutMask()`:

1. `uint16_t effects_pasteRecord(uint8_t dst_scene, const effect_record_t *src)`
   — mask with `match_type = 1` against the destination's current type
   (members share it by the layout gate); for each member except one whose
   record is `src` itself: `scene_commitEffectRecord(member, src)`; if the
   active Scene was written, `effects_activateScene(active)`;
   `bank_revalidateVoiceEditMasks()`; return the mask written.
2. `uint16_t effects_resetRecord(uint8_t dst_scene)` — same mask rule; for
   each member: `scene_effectRecordForWholeCommit()`,
   `scene_effectRecordDefaults()`, `scene_finishEffectWholeCommit()`;
   activate if active; revalidate; return the mask.
3. `uint8_t effects_pasteSeqStep(uint8_t dst_scene, uint8_t step,
   const effect_seq_step_t *src)` — same-type mask; per member, per lane:
   `scene_setEffectSeqLaneValue()`, `scene_setEffectSeqLaneLocked()`;
   `effects_state.seq_serial++` and `held_morph_valid = 0` when the active
   Scene's lane 0 changed.
4. `uint8_t effects_clearSeqLanes(uint8_t scene, uint16_t step_mask,
   uint16_t lane_mask)` — same-type mask; per member/step/lane: value 0,
   unlocked; serial/held-Morph as above.

```c
/*
 * Fan-out Effect record and FX sequence operations for copy/clear (S075).
 *
 * What: paste or reset a whole Effect record, paste one FX step (lock mask
 * and 16 lane values), or clear lanes on steps, applying the same change to
 * every Scene in the destination's edit mask (same Effect type). Why: user
 * rule F3/confirm 1 — Effect pastes and FX sequence pastes and clears fan
 * out; the existing setters only set locks (A15: locks could not be removed).
 * Inputs: destination Scene and data. Outputs: SceneData commits (each marks
 * its AutoSave cells and card-clean bit), runtime activation or serial bump
 * for the active Scene, mask revalidation after type changes; record
 * functions return the written Scene mask (names follow it).
 * Clients: copyOps.c, clearOps.c. Affiliates: effects_fanoutMask(),
 * scene_commitEffectRecord(), scene_setEffectSeqLane*(),
 * effects_activateScene(), bank_revalidateVoiceEditMasks().
 */
```

#### S7-03 `EffectsManager.h` — ADD the four prototypes after line 334 (`effects_setSeqLaneLock()` prototype) with the block above

#### S7-04 `copyOps.c` — ADD `ccCopy_runFxSteps()` (FX step paste executor)

Steps: (1) source and destination Effect types must match, else DROP;
(2) snapshot the source range (with wrap, range order) from
`scene_effectConst(src)->steps` into the buffer at offset 512 (≤ 288 B);
(3) for each step `effects_pasteSeqStep(job.scene, (job.start + i) % 16,
&snap[i])`; (4) on the Effect page: `menuEffects_renderSeqLeds()`,
`menu_repaint()`; DONE.

```c
/*
 * Paste FX sequence steps (spec §4.4 FX step).
 *
 * What: copies each source step's lock mask and lane values onto the
 * destination steps in range order with wrap, fanning out to same-type Scenes
 * in the destination's edit mask; a destination whose Effect type differs
 * from the source's is dropped silently. Why: user rules B9, F3. Inputs: the
 * operation's FX source and the job. Output: DONE or DROP. Caller:
 * copyClearService.c. Affiliates: effects_pasteSeqStep().
 */
```

#### S7-05 `clearOps.c` — IMPLEMENT `ccClear_fxStepNow()` and the FX clear executors

- `ccClear_fxStepNow(scene, step)`: `effects_clearSeqLanes(scene, 1u << step,
  0xFFFF)`; Effect-page LED/repaint refresh.
- `clear fx` executor: `effects_resetRecord(job.scene)`; for each written
  Scene `ccSvc_nameContentChanged(Effect row)`.
- `clear fx sequence` executor: `effects_clearSeqLanes(job.scene, 0xFFFF,
  0xFFFF)`.

```c
/*
 * Clear FX sequence steps or the whole Effect (spec §5).
 *
 * EFFECTS SEQ: that step's locks and values return to empty at once. `clear
 * fx sequence`: all 16 steps; parameters, run mode, length and scale stay.
 * `clear fx`: the record returns to defaults (type `off`). All fan out
 * through the edit mask (user, confirm 1). Callers: the router (SEQ) and
 * copyClearService.c (queued PERF clears).
 */
```

---

## 10. Stage 8 — Scene-level pastes and clears

### 10.1 BankData

#### S8-01 `Core/Bank/BankData.c` — ADD after `bank_revalidateVoiceEditMasks()` (ends line 458); `BankData.h` — declare after line 82

Three functions:

1. `uint16_t bank_sceneFanoutMask(uint8_t scene)` — `bit(scene)` plus the
   Scene's own entry (active Scene: `bank_sceneMaskVoiceEdit()`; other:
   `bank_sceneMaskVoiceEditForScene(scene)`), keeping only members that are
   present and pass `scene_editLayoutMatches(scene, member)`.
2. `void bank_exchangeVoiceEditMask(uint8_t src, uint8_t dst)` — the spec §4.4
   formula, stored through `bank_setSceneMaskVoiceEditForScene(dst, m')`.
3. `void bank_resetVoiceEditMaskToSelf(uint8_t scene)` —
   `bank_setSceneMaskVoiceEditForScene(scene, bit(scene))`.

```c
/*
 * Edit-mask helpers for copy/clear (S075, spec §4.4, §4.5, §5).
 *
 * bank_sceneFanoutMask(): the Scenes a paste or clear of a Scene child
 * (Instrument, Kit, Effect, FX sequence, mix settings) must reach: the Scene
 * itself plus its own directional entry, limited to present Scenes with the
 * same layout (types), so fan-out never writes a mismatched Scene. The
 * pressed Scene's own entry is used even when it is not active (user, F5).
 * bank_exchangeVoiceEditMask(): `copy scene` / `copy scene settings` give the
 * destination the source's entry with the two Scenes' bits exchanged.
 * bank_resetVoiceEditMaskToSelf(): `clear scene` / `clear scene settings`.
 * Outputs go through bank_setSceneMaskVoiceEditForScene(), which normalizes,
 * keeps the active-bit invariant and marks the Bank AutoSave field.
 * Clients: copyOps.c, clearOps.c. Affiliates: scene_editLayoutMatches(),
 * bank_revalidateVoiceEditMasks().
 */
uint16_t bank_sceneFanoutMask(uint8_t scene);
void bank_exchangeVoiceEditMask(uint8_t src, uint8_t dst);
void bank_resetVoiceEditMaskToSelf(uint8_t scene);
```

### 10.2 SceneData

#### S8-10 `Core/Bank/Scene/SceneData.c` — MODIFY `scene_initAll()` lines 922–925: promote `initial_types[]` to file scope

- Move the table to file scope as `static const instrument_type_t
  scene_initialInstrumentTypes[INSTRUMENT_SLOT_COUNT]` above
  `scene_initAll()`; use it there and in S8-12.

```c
/*
 * Default Instrument types of a fresh or emptied Scene (DRM, DRM, DRM, SNR,
 * CYM, HAT). Shared by scene_initAll() and scene_resetKitToDefaults() (S075)
 * so both produce the same Kit.
 */
```

#### S8-11 `SceneData.c` — ADD `scene_settingsDefaults()` and `scene_commitSettings()` after `scene_busCompDefaults()` (line 871 onward); declare in `SceneData.h` after line 517

- `scene_settingsDefaults(scene_settings_t *out)`: zero; MIDI channel
  `track + 1`, MIDI note `MIDI_DEFAULT_TRIGGER_NOTE`, audio out
  `scene_defaultVoiceAudioOut(slot)`, FX send 0, fader 0, Effect Morph 0,
  `scene_busCompDefaults(out)` (the same values as
  `filesystem_initSceneStage()`).
- `scene_commitSettings(uint8_t scene, const scene_settings_t *src)`: write
  every field through its change-aware setter (`scene_setMorphAmount`,
  `scene_setVoiceMorphAmount` ×6, `scene_setTrackMidiChannel/Note` ×7,
  `scene_setVoiceAudioOut/FxSendAmount/FaderSetting` ×6,
  `scene_setEffectMorphAmount`, `scene_setBusCompSetting` ×4). Return nonzero
  when any byte changed.

```c
/*
 * Whole-settings defaults and commit for copy/clear (S075).
 *
 * scene_settingsDefaults(): the settings of a fresh Scene (as Scene Load's
 * stage defaults). scene_commitSettings(): copies a complete settings image
 * into one Scene field by field through the change-aware setters, so every
 * changed byte marks its own AutoSave cell and the card-clean bit; no runtime
 * apply (Preset owns that). Why: `copy scene settings`, `copy scene`,
 * `clear scene` and `clear scene settings` replace all `sceneset.scg` fields
 * (user, C11) without a second writer of retained Scene data. Inputs: Scene
 * index and a source image (which may be another Scene's settings). Output:
 * changed flag. Clients: copyOps.c, clearOps.c. Affiliates: Autosave Scene
 * cells, preset_applySceneSettings().
 */
void scene_settingsDefaults(scene_settings_t *out);
uint8_t scene_commitSettings(uint8_t scene_index, const scene_settings_t *src);
```

#### S8-12 `SceneData.c` — ADD `scene_resetKitToDefaults()`; declare in `SceneData.h`

- Steps: for each slot `instrumentManager_resetSlot(&kit.instruments[slot],
  scene_initialInstrumentTypes[slot])`; slot-6/track-7 pair through
  `scene_setSlot6Track7AmpEnvelopeDecay(scene, 0)` and its Morph setter;
  `bank_invalidateSdCleanScene(scene)`; `autosave_markKitDirty(scene)`.

```c
/*
 * Reset one Scene's Kit to the fresh-Scene Kit (S075, `clear scene` on a
 * Scene that is not active). Never called for the active Scene (its Kit is
 * kept, user B11). Marks the whole Kit for AutoSave. Client: clearOps.c.
 */
void scene_resetKitToDefaults(uint8_t scene_index);
```

### 10.3 Preset

#### S8-20 `Core/Bank/Scene/Preset/presetManager.c` — MODIFY `preset_startInstrumentApplyImage()` (line 1984): add a `source_slot` parameter

- Signature: add `uint8_t source_slot` after `slot`. After the assignment at
  line 2046 (`scene->kit.instruments[slot] = *staged;`), when `source_slot !=
  slot`, rewrite the destination's LFO voice selectors: for the descriptors
  found with `instrumentManager_descriptorIndexForBinding(type,
  INSTRUMENT_BIND_LFO_TARGET_VOICE / _VOICE_2)`, in all three images, a value
  equal to `source_slot + 1` becomes `slot + 1`. Then mark as before.
- MODIFY the call at line 2131 to pass `slot` as `source_slot` (Instrument
  Load: unchanged behaviour).

```c
        /*
         * S075: an Instrument copied from another slot keeps "self" LFO
         * targets pointing at itself. Kit Save writes `self` for a selector
         * equal to the own slot; a resident copy must apply the same rule by
         * moving source_slot+1 selectors to slot+1 in the Normal, Morph and
         * interpolation images before the runtime binds them. Loads pass
         * source_slot == slot and are unchanged.
         */
```

#### S8-21 `presetManager.c` — ADD `preset_startInstrumentCopy()` after `preset_startInstrumentApply()` (ends line 2134); `presetManager.h` — declare after line 483

- Steps: `preset_startInstrumentApplyImage(&src_scene.kit.instruments[src_slot],
  dst_mask, dst_slot, src_type, src_slot, 1u)`.

```c
/*
 * Commit one resident Instrument slot onto a slot in a set of Scenes (S075).
 *
 * What: copies type, Normal and Morph images from a resident source slot to
 * dst_slot of every Scene in dst_mask through the same commit path as
 * Instrument Load (Bank-present publication, whole-Instrument AutoSave
 * marker, card-clean invalidation, runtime modulation clear and bounded
 * apply when the active Scene is touched), with `self` LFO selectors
 * retargeted. Why: `copy instrument` (with edit-mask fan-out) must behave
 * exactly like a load of that Instrument. Inputs: source Scene/slot,
 * destination mask (the caller has checked the Advanced limit) and slot.
 * Output: none; drive preset_tickInstrumentApply() until it returns 0.
 * Client: copyOps.c. Affiliates: preset_startInstrumentApplyImage().
 */
void    preset_startInstrumentCopy(uint8_t src_scene, uint8_t src_slot,
                                   uint16_t dst_mask, uint8_t dst_slot);
```

#### S8-22 `presetManager.c` — ADD `preset_applyWorkersIdle()` after `preset_tickDrumsetApply()` (line 1576 onward); declare after `presetManager.h:462`

```c
/*
 * Report whether the Scene and Instrument apply workers are idle (S075).
 *
 * Output: nonzero when neither drumset_apply_active nor
 * instrument_apply_active is set. Why: a Scene-level paste or clear touching
 * the active Scene waits at the head of the copy/clear queue until the
 * previous apply (for example after a PERF Scene switch) has finished.
 * Client: copyClearService.c.
 */
uint8_t preset_applyWorkersIdle(void);
```

### 10.4 Scene-level executors

All executors run from `ccSvc_tick()` when their job reaches the queue head,
return DONE, WAIT or DROP, and wait (WAIT) while `!preset_applyWorkersIdle()`
before writing anything that touches the active Scene. After an apply starts,
the executor keeps returning WAIT, calling `preset_tickInstrumentApply()`
for Instrument applies (Menu does not tick copy-started applies) and
observing `preset_applyWorkersIdle()` for the Scene worker (Menu ticks it).

#### S8-30 `copyOps.c` — `ccCopy_runInstrument()`

1. Source slot = source track (track 7 → slot 6); destination slot = job
   track (track 7 → slot 6). Same Scene and slot → DONE (no-op).
2. `mask = bank_sceneFanoutMask(job.scene)`.
3. For every Scene in the mask: `instrumentManager_typeSelectableForSceneSlot(
   member, dst_slot, src_type)`; any refusal → DROP (silent, user rule).
4. Wait for idle workers; `preset_startInstrumentCopy(src_scene, src_slot,
   mask, dst_slot)`.
5. Slot-6 pair: when both slots are slot 6, for every member copy
   `slot6_track7_amp_envelope_decay` and its Morph value with
   `scene_setSlot6Track7AmpEnvelopeDecay()` / `...MorphAmpEnvelopeDecay()`.
6. `bank_revalidateVoiceEditMasks()`.
7. Names: for every member `ccSvc_nameCopy(instrument row (member, dst_slot),
   instrument row (src_scene, src_slot))`.
8. WAIT until `preset_tickInstrumentApply()` returns 0; DONE.

```c
/*
 * Paste one Instrument with edit-mask fan-out (spec §4.4, user F3).
 *
 * Copies the source track's slot onto the destination track's slot in the
 * destination Scene and every Scene in its edit mask; fails silently when any
 * of them would exceed two Advanced Instruments; track 7 acts as slot 6 and
 * the Kit-owned slot-6/track-7 decay pair travels only slot 6 -> slot 6.
 */
```

#### S8-31 `copyOps.c` — `ccCopy_runKit()`

1. `mask = bank_sceneFanoutMask(job.scene)`; wait for idle workers.
2. For every member except the source Scene: `scenes[member].kit =
   scenes[src].kit` (via `scene_get()`), `bank_invalidateSdCleanScene()`,
   `bank_setScenePresentMask(bank_scenePresentMask() | bit(member))`; when the Scene was not present
   before, `autosave_markSceneWithPatternDirty(member)`; else
   `autosave_markKitDirty(member)`.
3. If the active Scene is in the mask: `preset_startDrumsetApply()`.
4. `bank_revalidateVoiceEditMasks()`; names: Kit row and six Instrument rows
   of every member from the source Scene's rows.
5. WAIT until `preset_applyWorkersIdle()`; DONE.

```c
/*
 * Paste a whole Kit with edit-mask fan-out (spec §4.4). Same retained and
 * runtime path as Kit Load: whole-Kit AutoSave marker, Bank-present, Scene
 * worker for the active Scene, mask revalidation; a Scene that becomes
 * present gets a whole-Scene marker so settings/Effect pasted while it had no
 * Kit are captured (spec §9.9).
 */
```

#### S8-32 `copyOps.c` — `ccCopy_runEffect()`

1. Wait for idle workers if the active Scene is in
   `bank_sceneFanoutMask(job.scene)`.
2. `written = effects_pasteRecord(job.scene, &scene_getConst(src)->effect)`.
3. Names: Effect row of every Scene in `written` from the source's row.
4. DONE.

#### S8-33 `copyOps.c` — `ccCopy_runSceneSettings()`

1. `scene_commitSettings(job.scene, &scene_getConst(src)->settings)` (no
   fan-out).
2. `bank_exchangeVoiceEditMask(src, job.scene)`;
   `bank_revalidateVoiceEditMasks()`.
3. If `job.scene` is active: `preset_applySceneSettings(active)`;
   `preset_applyKitAudioRouting(active, slot)` for slots 0..5;
   `preset_syncBusCompMirrors()`; `preset_syncEffectMorphMirror()`.
4. DONE.

#### S8-34 `copyOps.c` — `ccCopy_runScene()` (multi-phase)

1. Wait for idle workers.
2. Settings: S8-33 steps 1–2 (no runtime yet).
3. Effect: `scene_commitEffectRecord(job.scene, &src.effect)` (no fan-out).
4. Kit: struct copy as S8-31 step 2 for the destination only.
5. Pattern: whole-region copy (S8-36) without retargeting (the Kit came
   along), under the exclusive boundary.
6. Present on; `autosave_markSceneWithPatternDirty(job.scene)`,
   `autosave_markEffectDirty(job.scene)`.
7. Names: Scene, Kit, six Instruments, Pattern, Effect rows.
8. If active: `preset_startDrumsetApply()`; WAIT for idle; DONE.

#### S8-35 `copyOps.c` — `ccCopy_runPatternOnly()` (`copy pattern`)

- Whole-region copy (S8-36) with retargeting when the destination's slot
  types or Effect type differ; names: Pattern row; present state unchanged
  (spec §4.4 present rule). No fan-out.

#### S8-36 `copyClearService.c` — whole-region copy phases (`ccSvc_runRegionCopy()`, static)

1. Claim the destination.
2. `pat_rawRegionSilence(dst)`; `pat_rawRegionCopyBody(src, dst)`.
3. No retarget needed: `pat_rawRegionPublishSteps(src, dst, 0, 896)` in the
   same pass; end the claim.
4. Retarget needed: 32 address entries per tick: decode the copied block in
   the destination pool, `ccCopy_retargetEntries()`,
   `pat_rawRegionRewriteBlock()`, then `pat_rawRegionPublishSteps(src, dst,
   i, 1)`.
5. End the claim; viewed-Scene refresh.

```c
/*
 * Copy one whole Pattern region onto another Scene (spec §9.8).
 *
 * Publication order for the whole region: every destination entry is made
 * empty first, the body is copied, blocks are rewritten while still
 * unreferenced, and the address entries are published last, so a reader of
 * the destination never sees an entry pointing at bytes that are not yet the
 * new block. The literal case finishes in one pass; the retarget case
 * publishes progressively (32 steps per tick, about 56 ms for a full
 * Pattern). Caller: ccSvc_tick() for `copy pattern` and `copy scene`.
 */
```

#### S8-40 `clearOps.c` — Scene-level clear executors

- `clear scene`, active Scene: wait for idle; `scene_settingsDefaults()` →
  `scene_commitSettings()`; Effect defaults in place
  (`scene_effectRecordForWholeCommit()` / `scene_effectRecordDefaults()` /
  `scene_finishEffectWholeCommit()`) and `effects_activateScene()`; Pattern
  reset (Stage 5 region reset); `bank_resetVoiceEditMaskToSelf()`; runtime as
  S8-33 step 3; `ccSvc_nameContentChanged()` for the Scene, Pattern and Effect
  rows. Kit kept (user B11). No fan-out.
- `clear scene`, other Scene: settings and Effect defaults as above;
  `scene_resetKitToDefaults()`; Pattern reset; mask to self;
  `bank_setScenePresentMask(present & ~bit)`; `ccSvc_nameContentChanged()`
  for all ten rows (names stay, refreshed flags clear). No fan-out.
- `clear scene settings`: defaults commit; mask to self; runtime if active.
- `clear send` (EFFECTS TRACK): slot = track (7 → 6); for every Scene in
  `bank_sceneFanoutMask(job.scene)`: `preset_setVoiceFxSendAmount(member,
  slot, 0)`, `preset_setVoiceFaderSetting(member, slot, 0)`.
- `clear fx`, `clear fx sequence`: S7-05.

```c
/*
 * Scene-level clears (spec §5). `clear scene` and `clear scene settings`
 * reset the Scene's own edit mask and do not fan out; `clear send`, `clear
 * fx` and `clear fx sequence` fan out through the edit mask (user, confirm
 * 1). Names stay on every clear; changed rows lose their refreshed flag.
 */
```

---

## 11. Stage 9 — Identity rows (HCNAMES)

#### S9-01 `Core/Hardware/SD/filesystem.c` — ADD the operation id after line 345

- `FS_INTERNAL_OP_UPDATE_HCNAMES_COPY,` after
  `FS_INTERNAL_OP_UPDATE_HCNAMES_PATTERN`; op-name table after line 3520:
  `case FS_INTERNAL_OP_UPDATE_HCNAMES_COPY: return "HNcU";`.

```c
    /* S075: copy/clear end-of-operation name/source overlay (remap table). */
```

#### S9-02 `filesystem.c` — MODIFY `filesystem_residentNames_tick()`

- Line 6476–6480: add the new op to the `update` predicate.
- Phase 3, before the final `} else` at line 6613: `else if (current_op ==
  FS_INTERNAL_OP_UPDATE_HCNAMES_COPY) filesystem_cacheCopyClearRemap();`.
- Phase 7, before the final `} else` at line 6834: the same.
- Dispatch at line 25545: add `case FS_INTERNAL_OP_UPDATE_HCNAMES_COPY:` to
  the case list that calls `filesystem_residentNames_tick()`.

#### S9-03 `filesystem.c` — ADD `filesystem_cacheCopyClearRemap()` after `filesystem_cacheCurrentResidentPatternName()` (ends line 6401)

Steps: (1) copy all 161 names from `hcnames_name_mirror` and all 161 sources
from `fs_resident_source` into the borrowed buffer at offsets 256 and 1705
(1,771 B); (2) for each row `r` with `remap[r] != 0xFF`: name ← original
`remap[r]` name (`filesystem_cacheResidentName()`), source ← original
source value without flags (`filesystem_setResidentSource()`), then
`filesystem_clearResidentRefreshed(r)`. Rows changed only by clears already
had their refreshed flag cleared in RAM and are written as they are.

```c
/*
 * Overlay copy/clear identity changes onto the freshly read register (S075).
 *
 * What: applies the session's row remap (destination row <- source row) from
 * the borrowed name buffer to the HCNAMES mirror and source register, using
 * a copy of the original rows so chained or swapped pastes resolve to the
 * pre-operation names. Copied rows take the source's name and source token;
 * their refreshed flag is cleared so the boot reader never reloads a library
 * object over pasted content (spec §9.10). Instrument type tokens are
 * formatted from the resident slot and follow automatically.
 * Inputs: remap[161] at offset 0 of the borrowed buffer. Output: mirror and
 * register rows; the shared writer then rewrites `.hcnamtmp` and swaps it in.
 * Caller: filesystem_residentNames_tick() phases 3 and 7.
 */
```

#### S9-04 `filesystem.c` / `filesystem.h` — ADD `filesystem_requestCopyResidentNames()` after `filesystem_requestUpdateResidentSceneNames()` (line 30345 onward); `filesystem_identityRow()` after line 5678; `FS_HCNAMES_ROW_COUNT`

- Request steps: refuse while busy or the buffer is not borrowed;
  `filesystem_prepareResidentNamesCache()`; `current_op =
  FS_INTERNAL_OP_UPDATE_HCNAMES_COPY`; `op_phase = 0`; store the callback;
  BUSY.
- `filesystem_identityRow(cls, scene, slot)`: public wrapper over the five
  static row helpers; `cls` is a new `fs_identity_row_class_t`
  (`FS_ROW_SCENE`, `FS_ROW_KIT`, `FS_ROW_INSTRUMENT`, `FS_ROW_PATTERN`,
  `FS_ROW_EFFECT`).
- `filesystem.h`: `#define FS_HCNAMES_ROW_COUNT 161u` with a static assert in
  `filesystem.c` against `FS_RESIDENT_NAMES_ROW_COUNT`.

```c
/*
 * Copy/clear identity publication (S075).
 *
 * filesystem_identityRow(): fixed HCNAMES row of one Scene/Kit/Instrument/
 * Pattern/Effect identity, FS_HCNAMES_ROW_COUNT when invalid.
 * filesystem_requestCopyResidentNames(): one asynchronous HCNAMES rewrite
 * that reads `/.hcnames`, applies the copy/clear remap held at offset 0 of
 * the borrowed name buffer (filesystem_cacheCopyClearRemap()), writes
 * `.hcnamtmp` and swaps it in, then calls cb. Refused while busy or when the
 * buffer is not borrowed. On failure the previous `.hcnames` stays intact.
 * Client: copyClearService.c. Affiliates: filesystem_residentNames_tick(),
 * filesystem_borrowNameCacheScratch().
 */
uint16_t filesystem_identityRow(fs_identity_row_class_t cls, uint8_t scene,
                                uint8_t slot);
bool filesystem_requestCopyResidentNames(fs_completion_cb_t cb);
```

#### S9-05 `copyClearService.c` — names

- On borrow: fill `remap[0..160]` with 0xFF.
- `ccSvc_nameCopy(dst, src)`: `remap[dst] = (remap[src] != 0xFF) ?
  remap[src] : src`; set "name write needed".
- `ccSvc_nameContentChanged(row)`: `filesystem_clearResidentRefreshed(row)`;
  set "name write needed".
- End of operation (interaction ended, queue and register empty, workers
  idle): if a write is needed and the card is mounted, request
  `filesystem_requestCopyResidentNames()` (retry while refused, give up after
  2 s); on completion `filesystem_ack()`, `autosave_markSourceDirty(row)` for
  every remapped row; then return the buffer and
  `copyClear_serviceFinished()`. A failure is traced and ends the operation
  the same way.

```c
/*
 * Name remap for copy/clear (spec §9.10).
 *
 * remap[dst] = the original row whose name/source the destination takes;
 * chained pastes resolve to the first source (a paste of B after A->B uses
 * A's row). Written once at the end of the operation through
 * filesystem_requestCopyResidentNames().
 */
```

---

## 12. Stage 10 — Documentation and tools

Each document is updated in the same change as its stage; this list is the
checklist.

| File | Change |
|---|---|
| `knowledge_files/specification_reference/PATTERN_DYNAMIC_STACK.md` | swap-block reserve (§ pool geometry: 8,060 B allocatable, reserve kept for other future uses); exclusive boundary (`patSvc_beginExclusive/endExclusive`); raw block API and its restriction; the in-place append bound; the region-copy publication order; the S075 finding on Pattern Load fan-out |
| `knowledge_files/specification_reference/MODULE_INTERCHANGE_SPEC.md` | new `Core/Menu/CopyClear/` module and its call map (buttonHandler → session; Menu ↔ session; service → PatternStackService, PatternData, filesystem, Preset, EffectsManager, BankData, SceneData); removed `copyClearTools`; new public functions per stage |
| `knowledge_files/specification_reference/STORAGE_SRAM_MANIFEST.md` | ledger §2 (+89 B, owners), released bytes (16 B Scene settings, 4 B `INCCMZ`), the 9 kB name buffer as borrowed working storage, pool reserve |
| `knowledge_files/specification_reference/FILESYSTEM_SPEC.md` | `sceneset.scg` table: `voice_decimation_all` no longer written, accepted and ignored on load, lines renumbered; HCNAMES copy op; name-buffer borrow |
| `knowledge_files/specification_reference/AUTOSAVE.md` | Scene cell 7 reserved (written 127, ignored on restore); suspension gates (copy/clear) beside the LOAD/SAVE gates |
| `knowledge_files/specification_reference/BANK_PRESET_ARCHITECTURE.md` | global `srt` removed; PERF `fxm`; edit-mask fan-out rules for copy/clear; edit-mask exchange and reset; O1 note (srt part gone) |
| `knowledge_files/specification_reference/dsp_instruments_effects/EFFECTS_BUS_REFERENCE.md` | fan-out from a non-active origin; `effects_pasteRecord/resetRecord/pasteSeqStep/clearSeqLanes`; A15 closed |
| `.../EFFECTS_MIXER_DSP_REFERENCE.md`, `.../INSTRUMENTS_DSP_REFERENCE.md` | global decimation multiplier removed; `srt` Scene target retired |
| `knowledge_files/specification_reference/DEV_MODES.md` | op name `HNcU` |
| `MEMORY.md` | Volatile Notes: S075 state; remove the "copy operations are no-ops" notes |
| `SCOPING_TARGETS.md` | §4.5/§6 copy/clear done; A15 closed; Session 075 findings kept |
| `tools/verify_bank_autosave.py` line 123 | Scene cell 7 is the constant 127, not the sceneset key |
| `tools/populate_scene_directory.py` line 84 | stop writing `voice_decimation_all` |
| `tools/convert_legacy_kits.py` line 270 | no change (legacy position list; the slot now holds `PAR_EFFECT_MORPH`, unused by Kit conversion); add a one-line comment |

---

## 13. Verification matrix

### 13.1 Build checks (every stage)

- `make all && make img` with no new warnings (`-Wswitch` will flag any
  missed `SCENE_MOD_TARGET_KIND_DECIMATION_ALL` case).
- `link_budget.py`: SRAM1 growth equals the ledger; no DTCM or ITCM growth.
- `grep -rn "copyClearTools\|copyClear_Mode\|PAR_VOICE_DECIMATION_ALL\|voice_decimation_all\|DECIMATION_ALL\b" Core`
  returns only the accepted tombstones (MidiMessages.h enum, storageTypes.c
  ignore branch).

### 13.2 Hardware checks (user)

| Area | Check |
|---|---|
| Buttons | Every mode × SEQ/SELECT/TRACK/BAR/MODE/SHIFT × (no operation, copy armed, copy with each source kind, clear): no step toggles, holds, Scene switches, mutes or audition leak; navigation works where the spec allows |
| LEDs | Copy: steady then flashing; clear: SHIFT and copy/clear flash until release; source flashes when visible; mode LEDs restored after release |
| Menus | Indicator formats; selection clamps; every clear menu opens at `cancel`; menu survives page changes; page and underlines return after release |
| Suspension | AutoSave/trace/settings writers do not start while an operation runs; a writer already running finishes; the scalar drain runs about 250 ms after the end; repair resumes |
| Pastes | Every step/bar selection; ranges both directions; wrap at 128; length extension; overlap with the source; cross-track and cross-Scene retargeting (same type, different type by key, by page position, dropped dtype mismatch); track copy with settings; queue of 4 and the fifth dropped; nearly full pool: paste completes or is dropped whole; playing Pattern without glitches |
| Clears | Every selection; `clear track` resets settings; PERF clears on the active and another Scene; FX step clear |
| Pot clears | VOICE, PERF (`1vm..6vm`, `fxm`), Effect page; register of 8 and the ninth ignored; underline off at the turn and stays off; values never change; FX lane part fans out |
| Scene level | Instrument (fan-out, Advanced limit, track 7, decay pair, LFO `self`), Kit, Effect (type change, fan-out), settings (mask exchange), Scene, Pattern (retarget), clears (active keeps Kit; other emptied and dark), `clear send` |
| Names | HCNAMES rows after each kind, fanned-out and chained pastes; refreshed flags; card removed during the name write ends the operation |
| `srt` retirement | PERF `fxm` edits and fans out; Scenes saved with `srt` load; old `srt` automation and LFO targets do nothing; AutoSave round trip |
| Regression | Load/Save; Pattern service under playback and Scene switching; VOICE and Effect underlines; AutoSave power cycle |

---

## 14. Implementation log

Notes taken while implementing. The working tree baseline for this pass is
commit `00bd078` ("s075 copyclear pre-implement") plus an uncommitted partial
implementation. Line numbers in §3–§12 still refer to `b33c94e`; anchors are
used where they have moved.

### 14.1 Review of the partial implementation (2026-10-01)

| Stage | State found | Action |
|---|---|---|
| 1 LED | Group blink implemented. Bug: `led_setBlinkGroup()` cleared `LED_LAYER_BLINK` on a removed member even when a blink slot still owned that LED. One comment line lost its indent. | Fix (§14.2). |
| 2 `srt` | Done as scheduled. Bug: `MidiParser.c` removed the per-voice `VOICE_DECIMATION1..6` CC assignment together with the global case, so per-voice decimation CCs did nothing. `copyClearTools.c/h` still on disk (not built). | Fix MIDI; delete the old files. |
| 3 Session | `copyClearSession.c` departed from the spec: OK/CANCEL menus instead of the selection lists, sources set on press (no range rule), MODE/BAR/SHIFT all consumed (no navigation possible), copy LED blinking at press, clears queued on copy/clear release, encoder click posting a clear, suspension active from the press, register wiped on release. Filesystem suspension gates, the name-buffer borrow API and the repair-epoch gate were missing. | Rewrite the four CopyClear pairs to the schedule; add the gates and borrow. |
| 4 Pattern | Reserve constants, allocator bounds, in-place append bound, service bounds and exclusive begin/end done. `pat_copyStep/Track/Pattern/Bar` were made into real copies (erase-then-write, unbounded) instead of removed; `pat_rawReadBlock()` returned 1 for an empty step; `pat_rawPublishEmpty()` published twice; `pat_rawRegionRewriteBlock()` read the silenced destination and could never work; compaction and swap evacuation were stubs. No paste engine. | Remove the copies; correct the raw API; implement compaction/evacuation and the engine. |
| 5–6 | Pot-target resolver, underline hooks and register shell present; register drain used queued single-step removals and was cleared on release. | Re-implement in the service. |
| 7 FX | Fan-out mask change and the four functions present. Bug: `effects_pasteRecord()` refused a paste across Effect types, so `copy effect` could never change type. No header prototypes. | Fix; add prototypes. |
| 8–9 | Not started (BankData/SceneData/Preset helpers, executors, HCNAMES op). | Implement. |
| 10 Docs | Not started. | Update. |

### 14.2 Changes made in this pass (2026-10-01)

Every changed function carries its contract block beside the code in the
`.c` file and beside the prototype in the `.h` file.

| Stage | File(s) | Change |
|---|---|---|
| 1 | `ledHandler.c` | `led_setBlinkGroup()` keeps `LED_LAYER_BLINK` on a removed group member while a blink slot still owns that LED (new `led_blinkSlotMember()`); comment indent fixed; S075 block before the shared blink-phase toggle. |
| 2 | `MidiParser.c` | Per-voice `VOICE_DECIMATION1..6` CC assignment restored; only the global case stays removed. |
| 3 | `copyClearTools.c/h` | Deleted (`git rm`). |
| 3 | `CopyClear/copyClearSession.c/h` | Rewritten to spec §3, §4.1–§4.3, §7, §8: range rule with a 16-entry nibble press stack; sources on release (SEQ/SELECT rows) or press (TRACK, PERF SEQ); copy menus at the default, clear menus at `cancel`; per-mode routing tables; MODE to LOAD/SAVE, MENU or SOM consumed; BAR consumed while a row is held or for FX sources; SHIFT passes through; edge masks pair consumed presses with releases; source group blink recomputed after every event; encoder turns only (clicks ignored); pots only in a clear operation with no menu shown. |
| 3 | `menu.c/h` | Encoder block ignores clicks; overlay, pot-ownership and bridge comment blocks updated in both files. |
| 3 | `buttonHandler.c` | `case BUT_COPY` block per S3-23. |
| 3 | `filesystem.c/h` | Suspension gates on all seven background admissions (`cc_suspended`, page-suppressed flag held while suspended); `FS_NAME_CACHE_COPYCLEAR`, `fs_name_cache_borrowed`, `filesystem_borrowNameCacheScratch()` / `filesystem_returnNameCacheScratch()`, `FS_NAME_SCRATCH_BYTES` with static assert. |
| 3 | `PatternStackService.c` | Repair-epoch gate `copyClear_backgroundSuspended()`. |
| 4 | `config.h` | `PAT_COPY_SWAP_*` renamed to `PAT_POOL_SWAP_CHUNKS` / `PAT_POOL_SWAP_BYTES`; contract block. |
| 4 | `PatternData.c/h` | `pat_copyStep/Track/Pattern/Bar` and `pat_rawWriteBlock` removed. Raw API rewritten at the end of the file: `pat_rawReadBlock` (0 for no block), `pat_rawPlace`, `pat_rawPlaceViaSwap`, `pat_rawSwapReturn`, `pat_rawPublishEmpty` (one PRIMASK publish, then free), `pat_rawFreeChunks`, `pat_rawSwapFree`, region silence/copy-body/publish/reset, and `pat_rawRegionCopiedBlock` + `pat_rawRegionPublishRewritten` replacing `pat_rawRegionRewriteBlock`. `pat_poolAlloc()` bound is `PAT_POOL_ALLOC_CHUNKS`. Stale `copyClearTools` comment fixed. |
| 4 | `PatternStackService.c/h` | `patSvc_beginExclusive()` also refuses while a filesystem replacement is pending; `patSvc_endExclusive()` restarts the repair epoch. `patSvc_exclusiveCompactStep()` (sliding compaction, see §14.3) and `patSvc_exclusiveEvacuateSwapStep()` (moves a pre-S075 block out of the swap block, clears orphan swap bits) implemented. |
| 4–6, 9 | `CopyClear/copyClearService.c/h` | Queue (4), run state, claim helper, name-buffer borrow; Pattern paste engine (claim + evacuate, snapshot with retargeting, check, place, finish); Pattern clear engine; whole-region copy (literal one pass, retargeting 32 entries per tick) and reset; register drain (one target per pass); name remap, HCNAMES write with a 2 s retry window, buffer return, `copyClear_serviceFinished()`. |
| 4, 8 | `CopyClear/copyOps.c/h` | Menus, `ccCopy_requestPaste()`, retargeting (§9.7), `ccCopy_buildStep()` (merge rules), executors for Instrument, Kit, Effect, Scene settings, Scene, Pattern and FX steps. |
| 5–8 | `CopyClear/clearOps.c/h` | Menus, `ccClear_requestClear()`, EFFECTS SEQ clear, pot clear front end, executors for `clear send`, `clear scene`, `clear scene settings`, `clear pattern`, `clear fx`, `clear fx sequence`. |
| 6 | `SceneModTargets.h` | Full contract blocks for `sceneModTarget_effectMorphId()` / `sceneModTarget_slot6DecayId()`. |
| 7 | `EffectsManager.c/h` | `effects_pasteRecord()` no longer requires equal types (mask on the destination's current type; a member equal to the source is skipped); the four prototypes added with their block. |
| 8 | `BankData.c/h` | `bank_sceneFanoutMask()`, `bank_exchangeVoiceEditMask()`, `bank_resetVoiceEditMaskToSelf()`. |
| 8 | `SceneData.c/h` | `scene_initialInstrumentTypes[]` at file scope; `scene_settingsDefaults()`, `scene_commitSettings()`, `scene_resetKitToDefaults()`, and `scene_commitKit()` (added, see §14.3). |
| 8 | `presetManager.c/h` | `source_slot` parameter and `preset_retargetSelfLfoVoice()`; `preset_startInstrumentCopy()`; `preset_applyWorkersIdle()`. |
| 9 | `filesystem.c/h` | `FS_INTERNAL_OP_UPDATE_HCNAMES_COPY` (`HNcU`), update predicate, phase 3/7 overlay hook, dispatch; `filesystem_cacheCopyClearRemap()`; `filesystem_identityRow()` with `fs_identity_row_class_t`; `filesystem_requestCopyResidentNames()`; `FS_HCNAMES_ROW_COUNT` with static assert. |

### 14.3 Deviations from the schedule (decided while implementing)

1. **Growing pastes and compaction.** The schedule placed a growing step in
   the swap block and then compacted. A block in the swap block cannot also
   serve as the scratch area for compaction, and compaction that only moves
   blocks into lower disjoint runs is not guaranteed to produce a run of a
   given size. Implemented instead:
   - `patSvc_exclusiveCompactStep()` is a sliding compaction: the block just
     above the lowest free chunk moves down into it, through the empty swap
     block when the two runs overlap. Repeated calls always end with all free
     space as one run below the swap block.
   - The paste check requires, for each step whose block grows, that the free
     chunks before that step are at least the new size (old and new blocks
     coexist until publication). Steps that do not grow always fit through
     the swap block (place, publish, free the old run, return).
   - Effect: a paste into a nearly full pool can be dropped where the
     schedule's net-growth check would have accepted it, by at most one block
     (≤ 132 B of 8,060 B). The paste still completes whole or is dropped whole.
2. **New operation while work is queued.** A copy/clear press is refused
   silently while a previous operation's pastes or clears are still queued
   (they read that operation's source). A pending register drain or name
   write does not block a new operation.
3. **Name write and jobs.** Jobs do not start while the HCNAMES write is in
   flight (it uses the same scratch offsets as a paste snapshot).
4. **Name-buffer protection.** While lent, `filesystem_clearNameCacheStorage()`
   and `filesystem_prepareLibraryNameCache()` do nothing and
   `filesystem_start()` refuses every operation except the copy/clear name
   write (callers see a busy facade). This covers the case of entering
   LOAD/SAVE right after releasing copy/clear while background work runs.
5. **`scene_commitKit()`.** Added to SceneData so Kit pastes do not assign
   `scene_t` fields outside SceneData (project rule).
6. **Remap value 0xFE.** Marks a row whose content changed but whose name is
   kept, so the end-of-operation write also marks its source bytes for
   AutoSave. `filesystem_cacheCopyClearRemap()` ignores it.
7. **SHIFT.** SHIFT edges are not consumed (they keep their MODE-modifier and
   LED roles and their mode-specific press/release pairing); only its LED is
   latched to blink during a clear operation.
8. **Menu overlay.** The existing overlay draws after the page renderer in
   `menu_repaint()` (instead of returning before it); both rows are fully
   replaced, so the result is the same.

### 14.4 Build results (2026-10-01)

- `make all && make img`: no new warnings (the remaining ones are newlib stubs,
  `EuklidGenerator.c`, `PatternData.c:161` and unused filesystem helpers, all
  present before this pass). Image 527,804 B.
- Against a build of `HEAD` (`00bd078`): `.bss` +96 B, `.data` −4 B,
  `.dtcmz` −8 B (SRAM1 net +92 B, inside the approved +100 B). Owners:
  session 25 B, service 59 B (queue 26, register 18, run 6, flags/claim/retry
  4, buffer pointer 4, alignment), `service_exclusive_scene` 1 B,
  `fs_name_cache_borrowed` 1 B; offset by the retired `srt` bytes.
- `link_budget.py`: Flash 527,804 / 753,664 B; ITCM unchanged.
- §13.1 grep: only the accepted tombstones (`storageTypes.c` ignore branch,
  `MidiMessages.h` enum) and comments remain.

### 14.5 Open items for hardware verification

All of §13.2. Points that need particular attention:
- full-pool pastes (growing steps) and the time sliding compaction takes on a
  fragmented pool (one slide per tick);
- whole-Pattern copy with retargeting into the playing Scene (destination
  entries are silent for up to 28 ticks);
- entering LOAD/SAVE immediately after a long pot-clear register drain (the
  browser waits until the name buffer is returned).

### 14.6 Stage 10 (documentation and tools), 2026-10-01

| File | Change |
|---|---|
| `PATTERN_DYNAMIC_STACK.md` | §3 swap block; §5 copy no-ops removed note; §12.9 usage denominator 2,015; §12.12 new service calls; §12.13 `ccSvc_tick()`; new §12.17 (exclusive boundary, raw API, region-copy order, sliding compaction, evacuation, suspension, append bound) and §12.18 (Pattern Load fan-out finding). |
| `MODULE_INTERCHANGE_SPEC.md` | `copyClearTools` section replaced by `Core/Menu/CopyClear`; PatternData raw API row; PatternStackService exclusive rows; BankData, SceneData, EffectsManager, Preset, filesystem and Menu rows; affiliate lists; `srt` placeholder. |
| `STORAGE_SRAM_MANIFEST.md` | Header S075 line; §5 Session 075 ledger; §8.2 `scenes`, `pat_regions` (swap block), `fs_list_cache_name` (loan and layout), copy/clear owners; §11 history. |
| `FILESYSTEM_SPEC.md` | `sceneset.scg` table renumbered and `voice_decimation_all` retirement; settings field list; Scene target list; PERF `fxm`; LFO note; new "Copy/clear: name-buffer loan and the `HNcU` update". |
| `AUTOSAVE.md` | Scene cell 7 reserved; copy/clear suspension gates. |
| `BANK_PRESET_ARCHITECTURE.md` | Copy/clear edit-mask rules (fan-out, exchange, reset); `srt` row removed from Scene contents; target 390 retired; PERF `fxm` mirror; O1 note. |
| `EFFECTS_BUS_REFERENCE.md` | A15 closed; §9 non-active origin and copy/clear functions; Session 075 history entry. |
| `EFFECTS_MIXER_DSP_REFERENCE.md`, `INSTRUMENTS_DSP_REFERENCE.md` | Global decimation multiplier removed; `srt` target retired. |
| `DEV_MODES.md` | `HNcU` op code note. |
| `MEMORY.md` | Current state; S075 volatile note; no-op copy notes removed; module tree; FAQ row. |
| `SCOPING_TARGETS.md` | Phase 6 status; A15 closed. Historical S074 "next session" notes left as written. |
| `tools/verify_bank_autosave.py` | Scene cell 7 is the constant 127. |
| `tools/populate_scene_directory.py` | `voice_decimation_all` no longer written. |
| `tools/convert_legacy_kits.py` | Comment on the legacy `PAR_VOICE_DECIMATION_ALL` position. |

### 14.7 F1 follow-up and trace debug, 2026-10-02

Implemented from `S075_PH6_COPYCLEAR_F1_AND_TRACE_IMPLEMENTATION.md` in the
combined Stage A–K order. The pass reverted the source group-blink layer,
added provisional row menus/raw-index range handling, destination-object
flashes, retained-selection clear menus, the revised step labels and
probability/notes rules, live non-overlap pastes, overlap-only scratch loans,
early trigger capture/restore, the whole-call trickle governor, lazy final
HCNAMES phases, and the approved `c`-stage copy/clear trace witnesses. The
Pattern exclusive move no longer emits a per-move trace record; summary
records remain at the copy/clear service boundaries.

The production SRAM ledger for this pass is +124 B: early source masks +64 B,
restore masks +64 B, early flags +1 B, governor credit +2 B, offset by the
removed group-blink state −7 B. DEV logging adds the approved 22 B packed
copy/clear trace state and 2 B filesystem edge/refusal latches.

Verification on 2026-10-02:

- `make all` with `DEV_MODE_LOGGING=0`: passed; `text=516,688`, `data=408`,
  `bss=409,840`, flash used 517,096 B, no `ccSvc_traceState` or `fs_cc_*`
  symbols.
- `make all` with `DEV_MODE_LOGGING=1`: passed; `text=530,592`, `data=416`,
  `bss=426,616`, flash used 531,008 B, headroom 222,656 B; ITCM 4,168 B and
  FXBUF margin 3,712 B.
- `python3 -m py_compile tools/decode_devlogs.py`, a synthetic stage-`c`
  decode, `git diff --check`, and the forbidden-symbol grep passed.
- Hardware F1/trace verification remains pending; the exact witness matrix is
  in the combined schedule's build-and-verification section.
