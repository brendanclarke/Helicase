# Bank and Preset Architecture

## Authority and scope

This is the authoritative reference for how parameters are stored in resident
memory across the Bank, Scene, Kit, Instrument, and Effect hierarchy, current
as of the **Session 075 close** (2026-10-03). S073 changed nothing here. S074
added the bus compressor Scene settings and the fourth fader mode. S075
added:

- copy/clear of Scenes and Scene children, with the edit-mask fan-out,
  exchange and reset rules in §2 (behaviour reference: `COPYCLEAR_UTILITIES.md`);
- the retirement of global `srt` (PERF `fxm` in its cell, §9);
- new Scene defaults: every voice routes to St1, the bus compressor is
  off/0/0/off (§3);
- the FX-send Morph endpoint (`fx_send_morph[6]`, §3, §7);
- the VOICE hold-SHIFT Morph view and the Effect-page SHIFT+TRACK voice-mix
  overlay (§5);
- the automation-priority rules (§3 "When parameters change", §8). How a stored value reaches the DSP
(descriptor writers, special-writer tags, LFO adapters) is in
`INSTRUMENTS_DSP_REFERENCE.md`. It describes what is stored, where it lives, when it changes,
when it becomes visible, and how it is persisted.

Related authority is deliberately separate:

- `PATTERN_DYNAMIC_STACK.md` owns all Pattern storage, allocator, and PAT4
  format details. This document covers Pattern only as a Scene child.
- `AUTOSAVE.md` owns hidden A/B record format, dirty masks, boot restore,
  and writer scheduling.
- `FILESYSTEM_SPEC.md` owns product SD layout, instrument file schemas,
  Scene/Bank directory structure, and HCNAMES grammar.
- `MODULE_INTERCHANGE_SPEC.md` owns the live direct-call ownership map.
- `STORAGE_SRAM_MANIFEST.md` owns the binding memory-reservation policy and linked
  allocation snapshot.

---

## 1. Hierarchy Overview

```
Bank (one resident at a time)
├── BankData: present mask, active Scene, voice-edit mask, restore slot
├── Scene[0..15] (up to 16 resident)
│   ├── SceneData: settings, kit slots, descriptor images, MIDI routing
│   ├── Effect: Scene-owned `effect_record_t` (420 B; `.fx` v2 child storage)
│   ├── Kit (embedded in SceneData)
│   │   ├── Kit-level settings (the slot-6/track-7 generated decay pair)
│   │   └── Instrument[0..5] (6 voice slots)
│   │       ├── Normal parameter image (byte array, descriptor-indexed)
│   │       ├── Morph parameter image (byte array, descriptor-indexed)
│   │       ├── Supplemental parameters (single-endpoint)
│   │       └── Runtime targets (velocity, LFO × 2 pairs)
│   ├── Pattern (owned by PatternData, not embedded in scene_t)
│   │   └── pat_scene_region_t: addresses, pool, bitmap, track settings
│   └── Scene settings: Morph amounts, Effect Morph, routes, FX-send Normal/Morph,
│       faders, MIDI channels/notes, bus compressor (no global decimation since S075)
└── Effect runtime (EffectsManager; type-tagged DTCM state)
```

---

## 2. BankData — `Core/Bank/BankData.c/h`

BankData owns the top-level Bank workspace state. It is NOT embedded in
Scene storage; it is a singleton module.

### Retained state

| Field | Type | Purpose |
|-------|------|---------|
| `bank_display_name[9]` | char[9] | 8-char display name + NUL, from HCNAMES row 0 |
| `bank_scene_present_mask` | uint16_t | Which of 16 Scenes are populated |
| `bank_scene_mask_voice_edit[16]` | uint16_t[16] | Per-Scene fan-out mask for VOICE-page and Scene-settings edits |
| `bank_active_scene_slot` | uint8_t | Currently active Scene index (0..15) |
| `bank_has_resident_bank` | uint8_t | Whether a Bank is loaded |
| `bank_restore_slot` | uint16_t | SD library slot for `settings.cfg` persistence |
| SD-clean authority | ~16 bytes | Session-scoped, never serialized |

### Voice-Edit Fan-Out Mask (Per-Scene, Session 071)

`bank_scene_mask_voice_edit[16]` is a per-Scene array of fan-out masks. Each
Scene has its own mask controlling which resident Scenes receive edits made on
the VOICE page and Scene settings pages. When the user edits a parameter, the
edit is applied to every Scene whose bit is set in the *active Scene's* mask.

**Default:** Each Scene's mask defaults to self-only: `(1u << scene_index)`.
The user opts into multi-Scene fan-out by toggling Scene bits with VOICE + SEQ.

**Invariant:** The active Scene's bit must always be set in its own mask.
`bank_ensureActiveInVoiceEditMask()` enforces this: when the new active
Scene's bit is not already set in the mask, the entire mask is dropped to
just the active Scene's bit. This prevents edits from silently fanning out to
Scenes that were selected for a different active Scene context.

**Setting the mask:** `bank_setSceneMaskVoiceEdit(mask)` normalizes to 16
bits, ensures the active Scene bit is set, but does NOT intersect with the
present mask. A Scene can be in the voice-edit mask even if not marked
present — this supports workflows where the user wants to pre-populate a
Scene before formally activating it. All public accessors read/write
`bank_scene_mask_voice_edit[bank_active_scene_slot]` transparently.

Turning a Scene bit on through the VOICE-held SEQ view additionally requires
the target Scene to have the same Effect type and the same six Instrument slot
types as the active Scene. A mismatch is rejected without toggling or flashing;
turning a bit off remains allowed. The shared predicate is
`scene_editLayoutMatches()`.

After a retained type can change, `bank_revalidateVoiceEditMasks()` walks all
16 directional owner entries and drops members whose layout no longer matches,
while preserving each owner bit. Menu load-completion funnels, the end of boot,
and `effects_changeType()` invoke this repair. Dropped bits mark the Bank
VOICE-mask AutoSave field through the indexed setter.

**Per-Scene accessors:** `bank_setSceneMaskVoiceEditForScene(scene, mask)` and
`bank_sceneMaskVoiceEditForScene(scene)` provide indexed access for boot
restore (Autosave) and bankset load/save.

**Copy/clear (Session 075).** The edit mask locks Scenes together for
everything except the Pattern:

- Pastes and clears of Scene children fan out through the **destination
  Scene's own entry** (the active Scene's entry for VOICE/STEP/EFFECTS
  pastes; the pressed Scene's entry for PERF Scene pastes, even when it is not
  active): `copy instrument`, `copy kit`, `copy effect`, FX step pastes,
  `clear fx`, `clear fx sequence`, the EFFECTS SEQ clear, the FX-lane part of
  a pot clear, and `clear send`. `bank_sceneFanoutMask(scene)` returns the
  Scene plus the present, layout-matching members of its entry; Effect
  operations use `effects_fanoutMask()` with the same entry and the
  same-type guard. Effect fan-out from a non-active origin now uses the
  origin's own entry.
- `copy scene` and `copy scene settings` do not fan out; the destination's
  entry becomes the source's entry with the two Scenes' bits exchanged
  (`bank_exchangeVoiceEditMask()`), then masks are revalidated.
- `clear scene` and `clear scene settings` do not fan out and reset the
  Scene's entry to itself (`bank_resetVoiceEditMaskToSelf()`).
- Pattern pastes and clears never fan out.

**Active Scene change:** Both `bank_setActiveSceneSlot()` and
`bank_selectActiveSceneForEditMask()` call the invariant enforcer.

**Scene switch morph rebuild:** `preset_applySceneSettings()` calls
`presetMorph_rebuildScene(scene_index)` after mirror sync, queues all 6 slots
for the bounded morph worker so the DSP converges to the new Scene's per-voice
morph amounts within the normal foreground budget, and activates the Scene's
Effect runtime through `effects_activateScene()`.

### Persistence

- `bankset.bcg` v2 stores `active_scene` and 16 per-Scene
  `scene_mask_voice_edit_NN` lines. Legacy single-key
  `scene_mask_voice_edit=XXXX` is accepted on read and expanded to self-only
  defaults `(1u << i)`.
- `settings.cfg` stores `active_bank` (the library slot of the current Bank).
- AutoSave HCPR captures the Bank-level scalar fields (32 bytes for the per-Scene
  mask array at offsets 13..44).
- The SD-clean authority is never serialized.

---

## 3. SceneData — `Core/Bank/Scene/SceneData.c/h`

`scenes[16]` is 26,080 bytes total (1,630 bytes per Scene since S075 F2:
50 B settings, the 420 B Effect record, and the 1,160 B Kit; `scene_t` has
2-byte alignment, measured). S074 had 1,626 B (45 B settings + one alignment
byte); the S075 base pass removed `voice_decimation_all` (1,624 B, the
alignment byte went with it) and F2 added `fx_send_morph[6]` (+6 B). The figure of 1,200 B per Scene in earlier revisions predates the
Session 072 Effect record. Scene storage contains everything except Pattern
data, which lives in `pat_regions[16]` (168,304 bytes) in PatternData.

### Per-Scene contents

| Category | Fields | Notes |
|----------|--------|-------|
| Kit slots | Instrument type per slot, audio routing per slot | 6 slots |
| Normal parameter images | Byte array per slot, descriptor-indexed | One image per voice |
| Morph parameter images | Byte array per slot, descriptor-indexed | One image per voice |
| Supplemental parameters | Single-endpoint values (decimation, velo_mod_amount) | Per voice |
| Target selections | Velocity target, LFO target × 2 pairs, per voice | byte tokens, 0xff = off |
| MIDI routing | Channel and note per track | 7 tracks |
| Scene Morph | Per-voice morph amount (0..255) | 6 values |
| Audio routing | Per-voice output assignment (0..5: St1, St2, L1, R1, L2, R2) | 6 bytes; default St1 (route 0) for every voice since S075 F2 (was L1 for voice 1 and St2 for voice 6) |
| FX send | Per-voice Normal and Morph send endpoints (0..127) | 12 bytes; the mixer reads the step override, otherwise interpolates the endpoints by the voice's resolved Morph amount each block (S075 F2) |
| Fader mode | Per-voice `pre`/`pst`/`fx`/`xfd` (0..3) | 6 bytes; `xfd` (3) added in S074 (`SCENE_FADER_SETTING_MAX`) |
| Bus compressor (S074) | `bus_comp[4]`: `cmp` 0..2 (off/St1/St2), `cam` 0..127, `ctm` 0..127, `csc` 0..6 | 4 bytes; defaults off/0/0/off; AutoSave Scene parameters 41..44; `sceneset.scg` `bus_comp_*` keys; edited on the last settings page and fanned out to the VOICE edit mask; not modulatable |
| Effect | Type, 64 normal cells, 64 Morph cells, 16-step sequence | 420-byte Scene-owned record; saved as named `.fx` v2 child |
| Effect Morph | Scene `effect_morph_amount` | AutoSave Scene setting index 40; serialized in `sceneset.scg` when present; edited on PERF as `fxm` (S075) and on the Effect page as `mrp` |

Fresh, cleared and default-staged Scenes (`scene_initAll()`,
`scene_settingsDefaults()` used by `clear scene` / `clear scene settings`,
`filesystem_initSceneStage()`, the boot empty-Scene reset) all use the same
defaults: MIDI channel track + 1, note 63, St1 routes, FX send Normal and
Morph 0, fader `pre`, Morph amounts 0, Effect Morph 0, compressor
off/0/0/off.

The Effect record is initialized to the registry's `off` defaults and is
replaced only through a complete SceneData transaction. EffectsManager owns
the runtime type dispatch; it does not own retained bytes. Scene and Bank
filesystem loads parse the optional first `<name>.fx` child into the shared
2,048-byte stage, then commit the Effect together with Scene settings and
Kit. Scene and Bank saves stream the corresponding named `.fx` v2 child. A
blank HCNAMES Effect name uses `none.fx` on card while preserving a blank
resident name cell.

### Parameter value domain

Instrument parameter values use `instrument_param_value_t` (uint8_t).
Target selectors use `instrument_target_token_t` (uint8_t) with `0xff`
as off. Wide descriptor and Scene IDs exist only for validation, display,
or runtime dispatch — they are never the stored representation.

### When parameters change

Parameters change through these paths:

1. **User edit (VOICE page):** `preset_setInstrumentParameter()` writes the
   descriptor-indexed endpoint in the Scene image and applies only that
   parameter's resolved Morph interpolation to DSP runtime via
   `InstrumentManager`. A step-automation-held parameter keeps its runtime
   value until the next trigger. Fan-out: written to every Scene in
   `bank_scene_mask_voice_edit`.

2. **User edit (Scene settings/PERF page):** Scene-level values (Scene and
   per-voice Morph, Effect Morph `fxm`, audio routing, FX-send endpoints,
   fader mode, compressor) written through SceneData setters. Fan-out same
   as VOICE page. (Global decimation `srt` was retired in S075.)

3. **Morph interpolation:** The morph engine interpolates between normal and
   morph images at the current per-voice morph amount. Result stored in
   `morph_interpolation[]` and applied to DSP runtime. Does not change
   stored images.

4. **LFO modulation:** Descriptor LFO overlays apply in parameter space
   through InstrumentManager adapters. Does not change stored images; the
   overlay is transient.

5. **Step automation:** Voice descriptor targets write through
   `instrumentManager_writeRuntime()` and set dirty bits. Scene targets
   use runtime-only overlays (Session 070). Neither path changes stored
   images.

   **S075 F3 priority (user): automation, then menu edits, then MIDI.**
   - A voice-parameter automation value holds until the voice's next
     trigger. `seq_automationHoldsParameter(slot, local)` reports it; every
     Morph-base runtime write goes through `presetMorph_writeRuntimeBase()`,
     which skips a held parameter (the base still lands in
     `morph_interpolation[]`, which the trigger restore applies).
   - A menu edit only sets an endpoint: `preset_setInstrumentParameter()`
     re-interpolates that one parameter at the voice's resolved Morph amount
     (`presetMorph_applyParameterNow()`); with Morph above 0 the sound
     changes by less than the edit (correct). The whole voice is not queued.
   - External MIDI CC/NRPN is lowest priority:
     `preset_setInstrumentParameterFromMidi()` stores the active Scene's
     clamped Normal endpoint (retained and AutoSaved, no fan-out) and queues
     the voice; the Morph sweep applies it when it gets there.
   - Before S075 F3 a menu edit queued the whole voice and the worker
     overwrote held automation mid-note (also on Morph changes, LFO on
     Morph, `Nvm` and Kit/Instrument applies).

6. **Kit/Instrument/Scene/Bank Load:** Commits validated data from staging
   into resident storage. Scene/Bank loads stage the Effect atomically with
   Scene settings and Kit; a missing `.fx` child uses the `off` default.
   Triggers full morph rebuild, Effect activation, and modulation rebind.

7. **AutoSave boot restore:** Overwrites resident values from the validated
   HCPR v3 winner at boot, including each Scene's 512-byte Effect region; a
   refreshed Effect row can instead narrow-load its named `.fx` child.

8. **User edit (Effect page):** `menuEffects.c` resolves the registry-driven
   `EFFECT_PAGE` and sends parameter, sequence-setting, Morph amount, and type
   changes through the EffectsManager edit API. Step 10 fans active-Scene
   Effect parameters, Morph endpoints, sequence run/length/scale, lane locks,
   Effect Morph amount, and type changes through the active VOICE edit mask;
   parameter/lock/sequence writes retain a same-Effect-type guard. The `typ`
   click-out invokes `effects_changeType()` and therefore
   preserves common rows, sequence settings, and Effect Morph while restoring
   type-specific defaults and clearing the sequence. The edit-mask gate and
   fan-out are part of that EffectsManager boundary.

9. **FX sequencer edit/playback (S072 Step 8):** the active Scene's FX run,
   length, scale, per-step values, and lane locks remain in its retained
   Effect record. `menuEffects` writes lock edits through EffectsManager for
   the physically held steps, while the TIM3 latch and foreground service
   apply them without changing the Pattern automation layer. Track scale uses
   the shared StepScale index table and defaults to `1/16`; track playback
   remains unchanged until the joint track-scale pass.

### When parameters become visible

- **VOICE page:** Reads from the active Scene's descriptor image for the
  active voice slot. Descriptor layouts come from the installed instrument
  type's `*Parameters.c` file.

- **PERF page:** Reads from Scene settings (Scene Morph, per-voice Morph,
  Effect Morph `fxm`)
  and `parameter_values[]` for legacy/global parameters.

- **Step-edit pages:** Reads automation values from the dynamic Pattern pool
  block.

- **Scene settings sub-page (mix):** Reads per-voice audio routing, live FX
  send, fader settings, and voice morph from SceneData/Preset getters.

### Scene activation

When the active Scene changes:
1. Clear all outgoing modulation owners.
2. Image-apply all six incoming instrument types from the new Scene.
3. Perform one all-source two-LFO-pair/velocity rebind.
4. Cold boot starts this exact ordinary Scene worker after audio startup.

Never assume a fixed Drum/Snare/Cymbal/HiHat slot arrangement — instrument
membership is fully dynamic.

---

## 4. Kit Storage (Embedded in Scene)

A Kit is not a separate resident object; it is the collection of per-voice
state within a Scene. The "Kit" concept maps to:

- 6 instrument types (one per voice slot)
- 6 normal parameter images
- 6 morph parameter images
- 6 supplemental parameter sets
- 6 audio routing values
- 6 target selection sets (velocity, LFO × 2)

### Kit Load/Save

- **Directory-based:** `Kit/NNN Name/kitset.kcg` plus six instrument files.
- **kitset.kcg:** Stores per-voice audio routing and 8.3 aliases for member
  instrument files.
- **Instrument files:** `[params]` and `[morph]` sections with descriptor
  key/value pairs. Keys are instrument-type-specific strings (e.g.,
  `osc_waveform`, `amp_envelope_decay`).
- **Kit Morph Load (KitMrp):** Copies source normal endpoint values into
  resident morph endpoints only for matching-type slots. Mismatched slots
  are no-change.

---

## 5. Instrument Storage (Per-Voice in Scene)

Each voice slot holds:

### Normal image

Byte array indexed by descriptor index. Contains the "A" endpoint for morph
interpolation. Written by user VOICE-page edits, Kit/Instrument loads, and
AutoSave restore.

### Morph image

Byte array indexed by descriptor index. Contains the "B" endpoint for morph
interpolation. Written by VOICE-page edits in the Morph view, KitMrp/
InstrumentMrp loads, and AutoSave restore.

**Morph view (S075 F2).** The VOICE page shows and edits Morph endpoints
while `voiceModeShowMorph` is set: latched by SHIFT+MODE VOICE, or
momentarily while SHIFT is held in VOICE mode (release returns to the latch
state). The FX-send cell follows the same view (Morph endpoint in the Morph
view; step override or Normal endpoint in the Normal view); the other Scene
setting cells (out, fader, voice Morph) have one value. On the Effect page,
SHIFT+TRACK shows that voice's mix Scene-setting screen while TRACK is held
(`menu_fxVoiceMixOverlayBegin()`); SHIFT there selects the Morph view too.

### Supplemental parameters

Single-endpoint values that are morphable/modulatable/automatable but have
no direct struct-offset runtime binding:
- `instrument_decimation` — per-voice sample-rate reduction
- `velo_mod_amount` — velocity modulation depth

These use `ROW_NOBIND_IMAGE` layout flags and are applied through custom
shapers in InstrumentManager.

### Target selections

Each voice has:
- 1 velocity modulation target (self-scoped + Morph token)
- 2 LFO modulation target pairs, each with:
  - Target voice (self, 1..6, scn)
  - Target parameter
  - Polarity (neg/pos/bi)

LFO voice `self` is storage-only: resolved to the destination one-based
slot on load, emitted as `self` on save. Never stored as a parameter value.

---

## 6. Scene Mod Targets — Non-Voice Parameters

Scene-level sound parameters that are modulation/automation targets but
are NOT parameters of a swappable instrument in a voice slot. These live in
`Core/Bank/Scene/SceneModTargets.c/h`.

### Current target table (Session 072)

| ID Range | Short Label | Per-Voice | Max | Apply Path | Status |
|----------|-------------|-----------|-----|------------|--------|
| 384–389 | 1vm..6vm | Yes | 127 (stored) / 255 (expanded) | Runtime morph overlay | Live |
| 390 | (retired `srt`) | — | — | none; `SCENE_MOD_TARGET_KIND_RETIRED` placeholder keeps later IDs fixed; pickers skip it, validation rejects it, old Pattern entries and LFO tokens do nothing | Retired (Session 075) |
| 391 | (reserved) | — | — | — | — |
| 392–397 | 1ou..6ou | Yes | 5 | `preset_applyVoiceAudioOutRuntime()` | Live (Session 070) |
| 398–403 | 1fx..6fx | Yes | 127 | Effective send pulled by mixer each block | Live (Session 072 Step 5) |
| 404 | fxm | No (Scene) | 127 stored / 255 expanded | `effects_setMorphAutomation()` overlay; LFO via EffectsManager | Live (Session 072 Step 9) |

### Voice Morph 7↔8 bit conversion

Step automation stores morph values in 7 bits (0..127). Runtime morph
operates in 8 bits (0..255). Conversion helpers:
- `menu_morphAutomationStore(v)`: 0..255 → 0..127 ((v+1)/2)
- `menu_morphAutomationExpand(v)`: 0..126 → 0..252 (v×2), 127 → 255

### Automation target flags

`SCENE_MOD_TARGET_USE_AUTOMATION` — set on targets reachable from step
automation. Voice Morph, Audio Out, and FX Send have this flag. Scene
Decimation does not (it is a velocity/LFO target only).

---

## 7. Runtime Overlay Architecture (Session 070)

Step automation for Scene targets uses runtime-only overlays that never
touch retained Scene/Kit setters. This prevents AutoSave thrashing and
preserves user-set values during playback.

### Overlay state

| Overlay | Location | Bytes | Scope |
|---------|----------|-------|-------|
| `morph_step_override[6]` | presetMorphEngine.c | 12 | Per-voice morph: active flag + amount |
| `preset_audioout_step_override[6]` | presetManager.c | 12 | Per-voice audio-out route: active + route (Session 071) |
| `preset_fxsend_step_override[6]` | presetManager.c | 12 | Per-voice FX-send amount: active + amount (Session 071) |
| `slot6_track7_decay_step_active/value` | InstrumentManager.c | 2 | Slot-6 generated decay |
| Audio routing | Direct mixer register write | 0 | No retained state |
| `seq_scene_automation_dirty` | sequencer.c | 4 | Bitmap of active overlays |

### Interaction with LFO (Updated Session 071)

**Voice Morph:** `presetMorph_getEffectiveVoiceAmount(slot)` returns step
override when active, else retained per-voice amount. LFO stores
base-independent direction + normalized depth (not an absolute amount).
The resolver computes signed deltas from the current effective base at
resolution time: MORPH direction scales toward 255, MAIN direction scales
toward 0. Multiple source/pair contributions sum as signed deltas and clamp
to [0, 255].

**Audio Out:** `preset_getEffectiveAudioOut(slot)` returns step override when
active, else retained Scene value. Session 071 added the step-override table.
Direct mixer register write for DSP apply.

**FX Send:** `preset_getEffectiveFxSendAmount(scene, slot)` returns the step
override when active, otherwise the Normal/Morph endpoints interpolated by the
resolved voice Morph amount (`presetMorph_getResolvedVoiceAmount()`: step
override or retained base, plus active-Scene LFO contributions):
`round(normal + (morph − normal) · amount / 255)`; equal endpoints return at
once. The VOICE Normal view
shows the override or Normal endpoint; Morph view shows the Morph endpoint.
The mixer pulls the audible value each block, applies the stored PRE/POST/FX
fader topology, and ramps the send into the live FX bus.

**Effect parameters and `fxm`:** EffectsManager owns these overlays
(`effects_automation`). Parameter overlays end when the writing track's
automation ends (Effect step markers), and all of them clear on the common
reset path and at Scene activation. `fxm` is the first Effect Morph base
source. See `EFFECTS_BUS_REFERENCE.md` §6 and §11.

**Slot-6 Track-7 Decay:** Trigger cascade priority:
1. Step override (if `step_active`)
2. LFO contribution (if LFO active)
3. Retained Scene value

### Transport restore

On stop or pattern restart:
1. `seq_restoreAllSceneAutomation()` walks `seq_scene_automation_dirty` and
   restores each target from retained SceneData.
2. `seq_restoreAllAutomation()` walks all 6 voice dirty bitmaps and restores
   from `morph_interpolation[]`.
3. Both run BEFORE `seq_clearAutomationDirty()`.

---

## 8. Morph Engine

### Per-voice morph

Each voice slot has a retained per-voice morph amount (0..255) in SceneData.
Global Morph (CC1 or morph pot) bulk-sets all six per-voice values. The
morph engine interpolates between normal and morph images:

```
result[i] = normal[i] + ((morph[i] - normal[i]) * amount) / 255
```

Results are stored in `morph_interpolation[slot][descriptor_index]` and
applied to DSP runtime through the normal descriptor writer.

S075 F3 priority is explicit at the runtime boundary: step automation holds
the current runtime value until the next trigger; a menu endpoint edit
re-interpolates only the edited descriptor at the voice's resolved Morph
amount; an external MIDI CC stores the active Scene's Normal endpoint and is
applied by the bounded worker, which skips automation-held descriptors.

### Morph decimation

The morph engine applies one per-voice value per service tick (rate-limited
CC dump). A generation scheduler guarantees a complete final pass at the
latest morph value. The engine walks Scene-owned descriptor images and the
active slot's descriptor table — not hardcoded parameter lists.

### Morph modulation sources

- **Direct edit:** PERF page morph knobs, MIDI CC1 on global channel.
- **Velocity modulation:** Velocity morph retained-sets the per-voice value.
- **LFO overlay (updated Session 071):** Base-independent direction + depth
  stored in `morph_lfo_contributions[6][6][2]` (144 bytes). The resolver
  computes signed deltas from the current effective base (step override if
  active, else retained) at resolution time. Direction is NONE (inactive),
  MAIN (toward 0), or MORPH (toward 255). InstrumentManager encodes polarity
  to direction+depth without reading the morph base. Serviced by the bounded
  morph worker. Does not change retained value.
- **Step automation overlay (Session 070):** Runtime-only override via
  `morph_step_override[]`. LFO modulates around the override. Does not
  change retained value.
- **Effective base authority (Session 071):**
  `presetMorph_effectiveVoiceBase()` is the single helper for choosing between
  step-override and retained base. Used by the resolver, pass snapshot,
  priority path, synchronous apply, and the public effective-amount getter.

---

## 9. `parameter_values[]` and the Legacy Bridge

`parameter_values[NUM_PARAMS]` (384 bytes) remains the legacy/global/menu
byte store. It is
the bridge for non-instrument sound parameters (globals, MIDI config,
sequencer settings) and the flat mirror for PERF display.

For descriptor-backed instrument parameters, `parameter_values[]` is NOT
the canonical store. The canonical store is the Scene descriptor image in
SceneData. `parameter_values[]` may be updated by `seq_applySceneAutomation()`
for PERF display purposes, but it is not the source of truth.

### What lives in `parameter_values[]`

- Global settings (BPM, ext sync, etc.)
- MIDI channel/note assignments
- Sequencer runtime values
- PERF display mirror of Scene settings (morph, routing) and, since S075,
  `PAR_EFFECT_MORPH` (`fxm`, the active Scene's Effect Morph, in the slot of
  the retired `PAR_VOICE_DECIMATION_ALL`; refresh-only in Global apply,
  synced by `preset_syncEffectMorphMirror()`)
- the bus compressor page mirrors `PAR_BUS_COMP_MODE..SIDECHAIN` (ids
  58..61, S074). They are refreshed from the active Scene by
  `preset_syncBusCompMirrors()` and never serialized as Globals.

**Bulk Global apply hazard (S074 observation O1, not fixed).**
`menu_tickGlobalApply()` and `menu_sendAllGlobals()` replay
`menu_parseGlobalParam()` for every id from `PAR_BEGINNING_OF_GLOBALS` up,
after a Settings Load or a legacy `.all` load. That range includes the PERF
mirrors `PAR_VOICE1_MORPH..PAR_VOICE6_MORPH`, whose handlers write the
mirrored (active-Scene) value into **every Scene of the VOICE edit mask** and
mark AutoSave. So a Settings Load equalises per-voice Morph across the mask.
(The `srt` part of O1 is gone since S075: `PAR_EFFECT_MORPH` in that slot is
refresh-only.) The bus compressor ids avoid this by
design: in `menu_parseGlobalParam()` they only refresh their mirrors. A fix
for the older ids could use the same refresh-only pattern.

### What does NOT live in `parameter_values[]`

- Descriptor-backed instrument parameters (live in SceneData images)
- Per-voice morph endpoints (live in SceneData)
- Pattern data (lives in PatternData)
- Bank metadata (lives in BankData)

---

## 10. Autosave Dirty Marking Summary

| Edit type | What is marked dirty | AutoSave owner |
|-----------|---------------------|----------------|
| VOICE page parameter edit | Scene scalar (image byte) | Scalar HCPR mask |
| Scene morph edit | Scene scalar (morph value) | Scalar HCPR mask |
| Scene settings edit | Scene scalar (setting byte) | Scalar HCPR mask |
| Audio routing edit | Scene scalar (routing byte) | Scalar HCPR mask |
| MIDI channel/note edit | Scene scalar | Scalar HCPR mask |
| Pattern step toggle/edit | Pattern semantic mutation | Pattern dirty mask |
| Pattern track settings | Pattern semantic mutation | Pattern dirty mask |
| Step automation edit | Pattern semantic mutation | Pattern dirty mask |
| Pool relocation (non-semantic) | Physical layout change only | Non-semantic Pattern mask |
| Step automation playback (voice) | Transient runtime only | NOT marked dirty |
| Step automation playback (Scene) | Transient runtime overlay | NOT marked dirty |
| LFO modulation | Transient runtime only | NOT marked dirty |
| Morph interpolation | Transient runtime only | NOT marked dirty |
| Effect type change | Retained type token, type-specific defaults, and cleared Effect sequence | Effect-region mask via `scene_finishEffectWholeCommit()` |
| Bus compressor edit (S074) | Scene parameter 41 + field, for every Scene in the VOICE edit mask | Scalar HCPR mask (`scene_setBusCompSetting()`) |
| Fader mode edit (incl. `xfd`) | Scene parameter 20 + slot | Scalar HCPR mask |

---

## 11. Boot Restore Order

AutoSave boot restore applies data in a specific order to maintain invariants:

1. **Bank slot** — from `settings.cfg` `active_bank`
2. **Bank name** — from HCNAMES row 0
3. **Scene present mask** — from HCPR Bank section
4. **Active Scene** — from HCPR Bank section
5. **Voice-edit mask** — from HCPR Bank section
6. **Per-Scene scalars and Effects** — Scene/Kit/Instrument values plus the
   512-byte Effect regions from HCPR v3, subject to refreshed-row narrow loads
7. **Per-Scene Patterns** — from hidden `.patNNa`/`.patNNb` PAT4 files

The voice-edit mask is restored after the active Scene is set, ensuring the
active-Scene-in-mask invariant holds at every step. Pattern restore happens
last because it is independent of scalar state and has its own generation-
based winner selection.
