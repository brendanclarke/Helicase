# S071 Test 12 — Morph Assignment Bug Investigation

**Branch**: `dev-ph5-effects`  
**Date**: 2026-09-26  
**Data**: `SD_CARD_S071_MORPH_BUG/`  
**Trigger**: Scene 7 (bankset `active_scene=6`), LFO inadvertently assigned
to voice 6 morph, filter frequency on voice 6 inaudible, voided on reboot

---

## Summary

An LFO was continuously driving voice 6 morph via the runtime-only
`morph_lfo_contributions` table, causing the morph engine to overwrite
all morphable descriptors (including filter frequency) every tick. The
assignment was purely transient — it did not persist to any instrument file
or survive reboot.

**Root cause**: the `INSTRUMENT_BIND_LFO_TARGET_VOICE` runtime handler
(InstrumentManager.c:3059–3068) is a store-only path. It validates and
stores the new voice cell value but does **not** call
`instrumentManager_installLfoModulationTarget()`. Changing the voice cell
from 7 ("scn") to any voice number updates the descriptor byte but leaves
the old Scene-target installation active, including any
`morph_lfo_contributions` entries written by
`presetMorph_setVoiceLfoModulation()`.

---

## Evidence from autosave trace

**Source file**: `SD_CARD_S071_MORPH_BUG/asavetrc.bin` (195 210 records)

The trace contains a boot boundary at record #106 422 (stage Q).

### Scene 6 voice morph dirty marks (post-boot only)

| Scene param | Dirty count | Record range | Note |
|-------------|-------------|--------------|------|
| 6vm | 3 944 | #106 449 – #194 103 | Sustained LFO-frequency modulation |
| 5vm | 1 290 | post-boot | Also modulated (separate LFO pair or earlier episode) |

**Pre-boot 6vm dirty count: 0.** All morph modulation occurred after the
reboot, consistent with the autosave reader loading a stale Scene-target
LFO assignment into the boot-time Kit apply.

Cluster analysis of the 6vm dirty marks shows 13 activity clusters:

| Cluster | Records | Marks | Pattern |
|---------|---------|-------|---------|
| 0 | #106 449 – #106 497 | 4 | Boot-time all-16-scene restore |
| 1 | #106 601 – #106 746 | 10 | Early post-boot |
| 2–11 | #106 867 – #188 072 | 3 576 | Sustained LFO-rate modulation |
| 12 | #193 309 – #194 103 | 114 | Final activity before clearing |

Tick deltas within clusters are 4–11 (sub-millisecond intervals),
characteristic of LFO-rate oscillation rather than manual edits.

### LFO target editing during the session

Only voice 1 (inst[0], Drum) had its LFO targets changed on Scene 6:

| Descriptor | Index | Dirty count | Record range |
|------------|-------|-------------|--------------|
| lfo\_target\_voice (pair 1) | normal[32] | 21 | #188 616 – #194 992 |
| lfo\_target\_param (pair 1) | normal[33] | 70 | #188 622 – #195 071 |
| lfo\_target\_voice\_2 (pair 2) | normal[34] | 0 | — |
| lfo\_target\_param\_2 (pair 2) | normal[35] | 0 | — |

Voices 2–6 had **zero** LFO target changes on Scene 6.

### All-16-scene simultaneous "6vm" writes

Records #106 443–106 458 (tick 23 056) contain 16 dirty marks at offsets
spaced exactly 1 920 bytes apart — one per Scene region. These are from
the boot-time drumset apply restoring stored morph amounts across all 16
scenes, not from LFO activity.

---

## On-disk instrument data

Every instrument file in `SD_CARD_S071_MORPH_BUG/Bank/000 FullBad/06 Barf/
Kit Emott/` was examined. No instrument has a Scene-namespace LFO target
on disk:

| Voice | File | lfo\_target\_voice | lfo\_target\_param | (pair 2) |
|-------|------|-------------------|-------------------|----------|
| 1 | emottd11.drm | self (→1) | 255 (OFF) | self/OFF |
| 2 | emottd22.drm | 1 | 255 (OFF) | self/OFF |
| 3 | emottd33.drm | 2 | 20 | self/OFF |
| 4 | emotts14.snr | 1 | 255 (OFF) | self/OFF |
| 5 | emottc15.cym | 4 | 20 | self/OFF |
| 6 | emotth16.hat | 1 | 255 (OFF) | self/OFF |

The captured autosave (`.hcprms1`, commit byte 0xa5) also shows all LFO
targets as voice=1, param=OFF for all instruments on Scene 6. This
represents the **final** state after the user edited the targets, not the
state at boot.

---

## Root cause mechanism

### The voice cell handler defect

`instrumentManager_writeRuntimeInternal()` dispatches
`INSTRUMENT_BIND_LFO_TARGET_VOICE` at line 3059:

```c
case INSTRUMENT_BIND_LFO_TARGET_VOICE:
case INSTRUMENT_BIND_LFO_TARGET_VOICE_2:
    /* "There is no standalone DSP write for this value." */
    return instrumentManager_lfoTargetVoiceValid(value);
```

This validates the byte and returns. It does **not** call
`instrumentManager_installLfoModulationTarget()`, which is the function
that:

1. Calls `instrumentManager_restoreLfoSupplementalTarget()` → clears
   `morph_lfo_contributions` via `presetMorph_clearLfoSource()`
2. Clears the `lfo_installed_targets[][]` record
3. Installs the new target (if any)

By contrast, `INSTRUMENT_BIND_LFO_TARGET_PARAM` (line 3070) reads the
sibling voice cell and calls the full install path. The design intent is
that the voice cell is "paired with the matching lfo\_target\_param binding
when that later binding is applied." This pairing assumption breaks when
the user changes the voice cell without subsequently changing the param
cell, or when the voice cell is changed back from "scn" after a
Scene-target was already installed.

### How filter frequency becomes inaudible

1. A Scene-target LFO assignment installs
   `INSTALLED_MOD_TARGET_SCENE_TARGET` and, for voice morph targets, calls
   `presetMorph_setVoiceLfoModulation()` which writes direction and depth
   into `morph_lfo_contributions[target_slot][source_slot][pair]`.

2. Each `presetMorph_tick()` call resolves the morph amount as
   `base + Σ(LFO contributions)` (clamped 0–255), then interpolates every
   morphable descriptor for that voice between its normal and morph
   endpoint images.

3. `filter_freq` is a morphable descriptor. The morph engine writes the
   interpolated value to the active Scene's descriptor image and applies
   it to the DSP runtime on every tick.

4. Manual edits to `filter_freq` via the front-panel encoder update the
   descriptor image, but the next morph tick overwrites the image with
   the LFO-driven interpolation result. At LFO rates, this overwrite
   occurs within milliseconds — making the manual edit inaudible.

### Why the bug voids on reboot

`morph_lfo_contributions` is a BSS array
(`presetMorphEngine.c`). It is zero-initialized on boot and explicitly
cleared by `presetMorph_init()`. The `lfo_installed_targets` array is
likewise BSS. On a fresh boot:

1. BSS zero-fill clears all contributions and installed targets
2. `presetMorph_init()` sets all directions to `DIRECTION_NONE`
3. Kit apply installs LFO targets from the current descriptor values
4. The descriptor values (either from Kit files or from a fresh autosave
   load) show voice=1/param=OFF → `instrumentManager_lfoTargetIdFromToken()`
   returns `INSTRUMENT_PARAM_INVALID` → no Scene-target installed → no
   morph contribution

---

## Probable trigger scenario

The trace data and code analysis converge on this sequence:

1. **A voice's LFO was assigned to a Scene-target voice morph** — either
   through direct UI editing (scrolling the voice cell to "scn" and the
   param cell to a morph index like "6vm") or through the autosave
   restoring a prior session's Scene-target assignment at boot.

2. **The voice cell was changed back** from "scn" to a voice number. The
   `INSTRUMENT_BIND_LFO_TARGET_VOICE` handler stored the new voice value
   but did not call `installLfoModulationTarget()`.

3. **The installed Scene-target and morph contribution persisted.** The
   LFO continued driving `morph_lfo_contributions[5][source][pair]`
   (voice 6 morph) at its oscillation rate.

4. **The user's voice 1 LFO target editing** (21 voice + 70 param changes)
   cleared pair 0 morph contributions for source slot 0 each time the
   param cell was written. But if the actual morph source was a different
   voice's LFO or pair 2, those edits would not have cleared the relevant
   contribution — explaining why the morph modulation continued after
   voice 1's targets were changed to OFF.

5. **The morph eventually cleared** — the last 6vm cluster ends at record
   #194 103. This could have been triggered by a scene switch (Kit apply
   normalizes all targets), by editing the correct voice/pair, or by the
   mechanism that produced the SD card capture.

6. **On reboot**, BSS cleared all contributions. Kit apply installed from
   the saved descriptor values (all OFF). Bug gone.

### `VO=1, DS=1vm` display anomaly

The user briefly observed `VO=1, DS=1vm` on one voice. This combination
is architecturally inconsistent for a stable display: with `VO=1` (voice
namespace), the display resolves target IDs in the range 0–383, which map
to descriptor short names ("wav", "frq", etc.), never to Scene-target
labels like "1vm" (which require ID ≥ 384, voice namespace "scn").

Most likely explanations:

- **Transient display during rapid scrolling**: the voice cell was updated
  to 1 while the display still rendered the param cell's previous
  Scene-target resolution before the next encoder-event display refresh.
- **"VO=1" referred to the voice page**, not the lfo\_target\_voice cell.
  The user was viewing voice 1's LFO page where VO displayed as "scn" and
  DS as "1vm", but reported "VO=1" as the voice being edited rather than
  the target voice value.

---

## Fix

### Required: voice cell handler must trigger target reinstall

**File**: `Core/DSP/Instruments/InstrumentManager.c`  
**Lines**: 3059–3068

The `INSTRUMENT_BIND_LFO_TARGET_VOICE` (and `_VOICE_2`) handler must
perform a full target reinstall when the voice cell changes. This ensures
that any old Scene-target installation — including morph contributions —
is properly restored/cleared before the new voice context takes effect.

The reinstall should read the sibling param cell (same pattern as the
`INSTRUMENT_BIND_LFO_TARGET_PARAM` handler at line 3070), compute the
new target ID with the updated voice value, and call
`instrumentManager_installLfoModulationTarget()`.

**Why not just call `presetMorph_clearLfoSource()`?** A standalone clear
would fix the morph leak but would not restore other supplemental targets
(slot decimation, Scene decimation). The full install path handles all
target kinds through `instrumentManager_restoreLfoSupplementalTarget()`.

### Impact

- No new SRAM. The install path already exists.
- Voice cell changes become slightly more expensive (one
  `installLfoModulationTarget()` call per encoder step), but the voice
  cell spans only 7 values (1–6 + scn) so the overhead is bounded.
- Eliminates the entire class of stale Scene-target leaks through the
  voice cell path.

---

## Files referenced

| File | Relevance |
|------|-----------|
| `Core/DSP/Instruments/InstrumentManager.c` | Voice cell handler (defect), param cell handler, install/restore path |
| `Core/Bank/Scene/Preset/presetMorphEngine.c` | `morph_lfo_contributions`, set/clear/resolve, tick |
| `Core/Bank/Scene/Preset/presetManager.c` | Supplemental parameter path, Kit apply, normalization |
| `Core/DSP/Instruments/Drum/DrumParameters.c` | Descriptor index 32 = lfo\_target\_voice |
| `Core/DSP/Instruments/HiHat/HiHatParameters.c` | Descriptor index 32 = lfo\_target\_voice |
| `Core/DSP/Instruments/Snare/SnareParameters.c` | Descriptor index 31 = lfo\_target\_voice |
| `Core/DSP/Instruments/Cymbal/CymbalParameters.c` | Descriptor index 32 = lfo\_target\_voice |
| `SD_CARD_S071_MORPH_BUG/asavetrc.bin` | Autosave trace (195 210 records) |
| `SD_CARD_S071_MORPH_BUG/.hcprms1` | Captured autosave payload (post-edit state) |
| `SD_CARD_S071_MORPH_BUG/Bank/000 FullBad/06 Barf/Kit Emott/` | On-disk instrument files |
