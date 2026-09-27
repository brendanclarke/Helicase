# Bank and Preset Architecture

## Authority and scope

This is the authoritative reference for how parameters are stored in resident
memory across the Bank, Scene, Kit, Instrument, and Effect hierarchy as of
Session 072. It describes what is stored, where it lives, when it changes,
when it becomes visible, and how it is persisted.

Related authority is deliberately separate:

- `PATTERN_DYNAMIC_STACK.md` owns all Pattern storage, allocator, and PAT4
  format details. This document covers Pattern only as a Scene child.
- `AUTOSAVE.md` owns hidden A/B record format, dirty masks, boot restore,
  and writer scheduling.
- `FILESYSTEM_SPEC.md` owns product SD layout, instrument file schemas,
  Scene/Bank directory structure, and HCNAMES grammar.
- `MODULE_INTERCHANGE_SPEC.md` owns the live direct-call ownership map.
- `SRAM_MANIFEST.md` owns the binding memory-reservation policy and linked
  allocation snapshot.

---

## 1. Hierarchy Overview

```
Bank (one resident at a time)
├── BankData: present mask, active Scene, voice-edit mask, restore slot
├── Scene[0..15] (up to 16 resident)
│   ├── SceneData: settings, kit slots, descriptor images, MIDI routing
│   ├── Effect: Scene-owned `effect_record_t` (420 B; runtime from Step 4)
│   ├── Kit (embedded in SceneData)
│   │   ├── Kit-level settings (audio routing, morph endpoints per voice)
│   │   └── Instrument[0..5] (6 voice slots)
│   │       ├── Normal parameter image (byte array, descriptor-indexed)
│   │       ├── Morph parameter image (byte array, descriptor-indexed)
│   │       ├── Supplemental parameters (single-endpoint)
│   │       └── Runtime targets (velocity, LFO × 2 pairs)
│   ├── Pattern (owned by PatternData, not embedded in scene_t)
│   │   └── pat_scene_region_t: addresses, pool, bitmap, track settings
│   └── Scene settings: decimation, MIDI channels/notes, morph values
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

**Per-Scene accessors:** `bank_setSceneMaskVoiceEditForScene(scene, mask)` and
`bank_sceneMaskVoiceEditForScene(scene)` provide indexed access for boot
restore (Autosave) and bankset load/save.

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

`scenes[16]` is 19,200 bytes total (1,200 bytes per Scene). Scene storage
contains everything except Pattern data, which lives in
`pat_regions[16]` (168,304 bytes) in PatternData.

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
| Scene Decimation | Global `srt` value | 1 byte |
| Audio routing | Per-voice output assignment (0..5) | 6 bytes |

### Parameter value domain

Instrument parameter values use `instrument_param_value_t` (uint8_t).
Target selectors use `instrument_target_token_t` (uint8_t) with `0xff`
as off. Wide descriptor and Scene IDs exist only for validation, display,
or runtime dispatch — they are never the stored representation.

### When parameters change

Parameters change through these paths:

1. **User edit (VOICE page):** `preset_setInstrumentParameter()` writes the
   descriptor-indexed byte in the Scene image, applies to DSP runtime via
   `InstrumentManager`, and optionally records automation. Fan-out: written
   to every Scene in `bank_scene_mask_voice_edit`.

2. **User edit (Scene settings/PERF page):** Scene-level values (morph,
   decimation, audio routing) written through SceneData setters. Fan-out
   same as VOICE page.

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

6. **Kit/Instrument/Scene/Bank Load:** Commits validated data from staging
   into resident storage. Triggers full morph rebuild and modulation rebind.

7. **AutoSave boot restore:** Overwrites resident values from the validated
   HCPR winner at boot.

### When parameters become visible

- **VOICE page:** Reads from the active Scene's descriptor image for the
  active voice slot. Descriptor layouts come from the installed instrument
  type's `*Parameters.c` file.

- **PERF page:** Reads from Scene settings (morph, decimation, audio routing)
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
interpolation. Written by SHIFT+VOICE morph-edit mode, KitMrp/InstrumentMrp
loads, and AutoSave restore.

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
| 390 | srt | No (global) | 255 | `preset_applyVoiceDecimationAllRuntime()` | Live |
| 391 | (reserved) | — | — | — | — |
| 392–397 | 1ou..6ou | Yes | 5 | `preset_applyVoiceAudioOutRuntime()` | Live (Session 070) |
| 398–403 | 1fx..6fx | Yes | 127 | Effective send pulled by mixer each block | Live (Session 072 Step 5) |
| 404 | fxm | No (Scene) | 255 | Reserved, no apply path | Reserved (Session 072) |

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

**FX Send:** `preset_getEffectiveFxSend(slot)` returns step override when
active, else retained Scene value. The mixer pulls it each block, applies the
stored PRE/POST/FX fader topology, and ramps the send into the live FX bus.

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

`parameter_values[275]` remains the legacy/global/menu byte store. It is
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
- PERF display mirror of Scene settings (morph, decimation, routing)

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

---

## 11. Boot Restore Order

AutoSave boot restore applies data in a specific order to maintain invariants:

1. **Bank slot** — from `settings.cfg` `active_bank`
2. **Bank name** — from HCNAMES row 0
3. **Scene present mask** — from HCPR Bank section
4. **Active Scene** — from HCPR Bank section
5. **Voice-edit mask** — from HCPR Bank section
6. **Per-Scene scalars** — Scene/Kit/Instrument values from HCPR
7. **Per-Scene Patterns** — from hidden `.patNNa`/`.patNNb` PAT4 files

The voice-edit mask is restored after the active Scene is set, ensuring the
active-Scene-in-mask invariant holds at every step. Pattern restore happens
last because it is independent of scalar state and has its own generation-
based winner selection.
