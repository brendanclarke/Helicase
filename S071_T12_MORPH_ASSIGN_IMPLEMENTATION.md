# S071 Test 12 — Morph Assignment Bug Fix Implementation Schedule

**Source**: `S071_T12_MORPH_ASSIGN_INVESTIGATION.md`  
**Branch**: `dev-ph5-effects`  
**Date**: 2026-09-26

---

## Fix — Voice cell handler must trigger LFO target reinstall

### Change 1: Replace store-only voice cell handler with full install path

**File**: `Core/DSP/Instruments/InstrumentManager.c`  
**Lines**: 3059–3068  
**Action**: MODIFY

The `INSTRUMENT_BIND_LFO_TARGET_VOICE` and `INSTRUMENT_BIND_LFO_TARGET_VOICE_2`
handler currently validates and returns without installing. The fix replaces
this with a full target reinstall that mirrors the sibling param handler
(lines 3070–3104) with the roles swapped: the voice value comes from
`value` (the argument being written), and the param token is read from
the sibling param descriptor cell.

The `preset_setSupplementalParameter()` caller (presetManager.c:1008)
stores the new voice value into `instrument_parameters[voice_index]`
before calling `instrumentManager_writeRuntime()`, so by the time this
handler fires, the Scene data already holds the new voice byte. The
sibling param cell retains whatever value it had from the last param
write or Kit load — this is correct because the voice change should
re-evaluate the existing param token under the new voice namespace.

During Kit apply, descriptors are applied in index order by
`preset_applyKitVoiceSupplemental()` (presetManager.c:1429). The voice
cell (descriptor index 32 for Drum/Cymbal/HiHat, 31 for Snare) is always
before its paired param cell (index 33 or 32 respectively), so the voice
handler's install runs first, then the param handler re-installs with the
same voice+param pair. The second install is redundant but harmless —
`instrumentManager_restoreLfoSupplementalTarget()` clears the first
install before the second takes effect.

During UI editing, only the changed cell fires. If the user changes the
voice cell alone, the voice handler now reinstalls with the existing param
token, clearing any stale Scene-target (including morph contributions).
If the user changes only the param cell, the existing param handler
continues to work as before. If both change (e.g., encoder on voice then
encoder on param), each triggers a correct independent install.

```c
/* current (lines 3059–3068): */
    case INSTRUMENT_BIND_LFO_TARGET_VOICE:
    case INSTRUMENT_BIND_LFO_TARGET_VOICE_2:
        /*
         * The selected target voice is stored in its descriptor cell and paired
         * with the matching lfo_target_param binding when that later binding is
         * applied. There is no standalone DSP write for this value. Pair 1 and
         * pair 2 share this validation but keep separate binding identities so
         * Menu/storage can find the correct sibling descriptor cells.
         */
        return instrumentManager_lfoTargetVoiceValid(value);

/* proposed: */
    case INSTRUMENT_BIND_LFO_TARGET_VOICE:
    case INSTRUMENT_BIND_LFO_TARGET_VOICE_2:
        /*
         * Reinstall the LFO destination when the target voice changes.
         *
         * The voice cell selects the namespace (voices 1–6 or Scene) under
         * which the sibling param cell's token is interpreted. Changing the
         * namespace can move the target between disjoint ID spaces — a token
         * that was a Scene-target voice morph index under namespace 7 becomes
         * a local descriptor index under namespace 1–6, or vice versa.
         *
         * The previous handler only stored the new voice value and deferred
         * target installation to the next param-cell write. That left the
         * old installed target (and any morph_lfo_contributions it wrote)
         * active until the param cell was independently rewritten — a window
         * that could persist indefinitely if the user changed only the voice
         * cell.
         *
         * The fix mirrors the param handler's install sequence: read the
         * sibling param cell, compute the target ID with the new voice
         * value, and call installLfoModulationTarget(). The install path's
         * restoreLfoSupplementalTarget() clears any stale morph or
         * decimation contributions from the old target before the new one
         * takes effect.
         */
        {
            uint8_t target_pair =
                (descriptor->runtime.kind == INSTRUMENT_BIND_LFO_TARGET_VOICE_2)
                    ? 1u : 0u;
            instrument_binding_kind_t param_kind = target_pair
                ? INSTRUMENT_BIND_LFO_TARGET_PARAM_2
                : INSTRUMENT_BIND_LFO_TARGET_PARAM;
            const kit_instrument_slot_t *source =
                scene_instrumentSlotConst(scene_getActiveIndex(), slot);
            uint8_t param_index;
            uint8_t param_token = INSTRUMENT_TARGET_TOKEN_OFF;
            if (!instrumentManager_lfoTargetVoiceValid(value))
                return 0u;
            if (source &&
                instrumentManager_descriptorIndexForBinding(
                    source->type, param_kind, &param_index)) {
                param_token =
                    source->parameter_images.instrument_parameters[param_index];
            }
            return instrumentManager_installLfoModulationTarget(
                slot, target_pair,
                instrumentManager_lfoTargetIdFromToken(
                    scene_getActiveIndex(), slot, value, param_token,
                    INSTRUMENT_TARGET_MODULATION));
        }
```

---

## Implementation order

| Step | File | Lines | Action | Depends on |
|------|------|-------|--------|------------|
| 1 | `InstrumentManager.c` / `.h` | 3059–3068 / runtime API comment | MODIFY voice cell handler and synchronize contract comment | — |

Single runtime change. No new includes, public API changes, or new functions;
`InstrumentManager.h` carries the adjacent contract description required for
the changed runtime behavior.

---

## Paths resolved

| Defect path | Mechanism |
|-------------|-----------|
| Voice cell changed from "scn" to voice 1–6 without param cell change | Voice handler now reinstalls → `restoreLfoSupplementalTarget()` clears stale morph contributions |
| Voice cell scrolled through "scn" during browsing | Each voice-cell write reinstalls with the current param token; transient Scene-target assignments are cleared as soon as the voice cell moves away from "scn" |
| Kit apply applies voice cell before param cell | Voice handler installs first, param handler re-installs second (redundant but correct — restore+install is idempotent for the same target) |
| Autosave loads stale Scene-target assignment at boot | Kit apply processes voice then param in descriptor order; both trigger install; the param handler's install is the final state (same as before) |

---

## Invariants preserved

1. **Param handler unchanged.** The `INSTRUMENT_BIND_LFO_TARGET_PARAM`
   handler (lines 3070–3104) is not modified. It continues to read the
   sibling voice cell and install. During Kit apply, it runs after the
   voice handler and produces the definitive installation.

2. **Kit apply descriptor order.** `preset_applyKitVoiceSupplemental()`
   iterates descriptors in index order. The voice cell always precedes its
   paired param cell (index 32/33 for Drum/Cymbal/HiHat, 31/32 for Snare).
   The voice handler's install is superseded by the param handler's install
   on the same pair. The restore path in each install clears the previous
   install before writing the new one, so no stale state accumulates.

3. **`instrumentManager_installLfoModulationTarget()` idempotency.**
   Calling install twice with the same source/pair is safe. The first call
   restores the old target and installs the new one. The second call
   restores the first install (which is now the "old" target) and
   reinstalls the same target. The morph contribution table ends in the
   correct state.

4. **Validation.** The voice handler validates `value` via
   `instrumentManager_lfoTargetVoiceValid()` and returns 0 (rejected)
   for invalid voice values, same as before. The param token defaults to
   `INSTRUMENT_TARGET_TOKEN_OFF` if the sibling descriptor cannot be found.

5. **Scene gate.** `preset_setSupplementalParameter()` only calls
   `instrumentManager_writeRuntime()` when `scene_index ==
   scene_getActiveIndex()` (presetManager.c:1010). Non-active scenes
   store the value without runtime install, same as before.

---

## SRAM impact

No additional SRAM. The change adds one `installLfoModulationTarget()`
call per voice-cell encoder step (at most 7 values: 1–6 + scn). The
install path uses stack-local variables and the existing BSS arrays
(`lfo_installed_targets`, `morph_lfo_contributions`).

---

## Verification criteria

1. **Morph leak — primary test**: On any voice, set LFO pair 1 target to
   `scn` / any voice morph (e.g., "6vm"). Confirm morph modulation is
   active on the target voice. Then change the voice cell back to a voice
   number (e.g., "1") WITHOUT changing the param cell. The morph
   modulation on the target voice must stop immediately.

2. **Morph leak — pair 2**: Repeat test 1 using LFO pair 2 targets
   (`vo2`/`ds2`). Same expected result.

3. **Normal LFO targeting**: Set LFO pair 1 to target a voice descriptor
   (e.g., voice 2 filter frequency). Confirm modulation works. Change
   the voice cell to a different voice. Confirm modulation moves to the
   new voice's descriptor at the same local index. Change back. Confirm
   modulation returns.

4. **Scene-target LFO**: Set LFO pair 1 to `scn` / a Scene target (e.g.,
   decimation or voice morph). Confirm modulation works. Change the param
   cell to a different Scene target. Confirm the old target is cleared and
   the new one is active.

5. **Kit apply**: Load a Kit that has LFO targets assigned. Confirm all
   LFO targets install correctly after Kit apply. The voice handler's
   initial install is superseded by the param handler — the final state
   must match the Kit's stored target pair.

6. **Filter frequency audibility**: Reproduce the original defect
   conditions (LFO targeting voice morph). After clearing via the voice
   cell change, confirm that manual filter frequency edits produce audible
   changes on the affected voice.

---

## Work log

- 2026-09-26: Implementation schedule written from investigation findings.
- 2026-09-26: Confirmed the retained voice-cell value is written to SceneData
  before `instrumentManager_writeRuntime()` runs, and confirmed the existing
  install helper restores descriptor, decimation, and Morph supplemental state.
  The sibling parameter descriptor lookup is therefore safe and preserves the
  existing pair-2 binding model.
- 2026-09-26: Replaced the store-only voice-cell handler for both LFO pairs with
  validation plus immediate sibling-token target reinstall. Added the matching
  runtime contract block in `InstrumentManager.h`; no new API or SRAM was
  introduced.
- 2026-09-26: Verification passed with `make all -j2`, producing
  `text=456748`, `data=416`, `bss=291900`; `make img` packaged the updated
  `build/LXRV2_lxr02.img`. Hardware criteria 1–6 remain pending.
