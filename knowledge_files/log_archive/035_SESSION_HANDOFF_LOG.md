# Session 035 Handoff Log

DATE: 2026-07-13
SESSION GOAL: Finish the next Phase 3 filesystem/runtime pass: add Kit and
Instrument Morph Load, make LFO target voice portable with storage-only `self`,
repair LFO scaling through descriptor-owned parameter domains, implement normal
new-format Kit Save, then fold the scratch audits into durable docs.
SOURCE AT END: local working directory, branch `dev-phase2-filesys`, with the
session's source/doc changes still uncommitted.
VERIFIED ON HARDWARE: partial/user-observed. The user reported the Morph Load
work "seems ok." Local verification was `make -B -j4`, `make -j4`, and
`git diff --check`; all passed. The build still prints the established nano
libc nosys `_close`/`_lseek`/`_read`/`_write` warnings and LTO serial note.

## Completed

- Added Kit Morph Load: `Load:[KitMrp  ]` parses the same Kit directory as
  normal Kit Load, then copies source normal endpoint values into resident
  morph endpoints only for slots whose instrument types match.
- Added Instrument Morph Load: the nested Instrument Load type row exposes only
  the destination slot's current type plus `<Type>Mrp`; morph import rejects
  type mismatches and copies source normal endpoint values into the destination
  morph image.
- Added file-only LFO target voice `self` for `lfo_target_voice` and
  `lfo_target_voice_2`, including loader support, converter/generated data
  migration, root `Instrument/` data update, and Kit Save emission.
- Repaired descriptor-backed LFO scaling by adding descriptor-owned modulation
  domains and InstrumentManager LFO adapters. Descriptor LFO targets now shape
  in parameter space and apply through `instrumentManager_writeRuntime()`.
- Matched original LXR negative LFO polarity in parameter space:
  `base * (1 - amount + amount * lfo)`.
- Implemented normal `Save:[Kit     ]` to write the new directory Kit shape:
  `Kit/<NNNxxxxx>/kitset.kcg` plus six instrument files, each with one
  `[params]` and one `[morph]` section.
- Expanded Kit slot addressing from 128 to 999 for root Kit folders.
- Added Scene-retained 16-character instrument source stems and default
  `inst_vo1`..`inst_vo6` names for later save provenance.
- Updated durable docs/specs/index so the scratch audits
  `INST_LFO_SELF_AUDIT.md`, `LFO_RANGE_MODULAR_AUDIT.md`, and
  `KIT_SAVE_AUDIT.md` may be deleted later.

## Kit And Instrument Morph Load

Kit Morph Load is a Load menu entry after normal Kit:

```text
Load:[Kit     ]
Load:[KitMrp  ]
```

It uses the same Kit browser and the same `Kit/NNN Name/kitset.kcg` plus six
instrument files as normal Kit Load. Filesystem parses the Kit into private
staging only. Preset then compares staged source slot type against resident
destination slot type for each selected Scene. Matching types copy source normal
endpoint descriptor values into the resident morph endpoint; mismatched slots
are no-change. Kit membership, audio routing, source names, and normal endpoint
values are not replaced by KitMrp.

Instrument Morph Load is nested inside Instrument Load. The type row shows the
destination slot's current type and a single `<Type>Mrp` sibling. Example: if
slot 1 currently hosts Drum, the row can show `Load:[Drum    ]` and
`Load:[DrumMrp ]`; it does not show `SnareMrp`. The lower-row browser remains
the same root `Instrument/` list for that type. On selection, Preset stages the
file through the normal Instrument loader, verifies the destination slot still
matches the requested type, then copies staged normal endpoint values into the
resident morph image. Mismatch is rejected/no-change.

Important implementation boundary: morph loads are endpoint imports, not type
or membership replacements. They do not reset runtime slot type, replace source
names, clear modulation owners, or apply audio routing. Active-scene runtime
refresh uses the bounded Morph worker.

## LFO `self` Storage

The storage model now has a portable alias for "this instrument's own voice":

```text
lfo_target_voice=self
lfo_target_voice_2=self
```

`self` is accepted only for those two voice selector keys. It is not accepted
for `lfo_target_param`, `lfo_target_param_2`, velocity targets, Menu values,
SceneData sentinels, packed `instrument_param_id_t`, or DSP runtime state.

On load, `storage_instrumentParseLine()` resolves `self` immediately using
`storage_instrument_state_t.expected_slot`, which filesystem already sets from
the final destination:

- Kit loads pass `op_instrument_slot + 1`.
- Root Instrument loads pass `op_instrument_load_destination_slot + 1`.

After parsing, every later layer sees an ordinary numeric `1..6` voice
selector. Preset's existing LFO pair normalization still rebuilds the packed
canonical target from the numeric destination voice plus the local descriptor
ID in `lfo_target_param`.

On save, the rule is relationship-based:

```text
stored numeric LFO voice == source one-based slot ? "self" : decimal voice
```

Cross-slot modulation remains a decimal voice number so the file preserves its
explicit external target when loaded elsewhere.

Converter/data work:

- `tools/convert_legacy_kits.py` emits `self` during fresh legacy `.SND`
  conversion when the legacy LFO voice selector equals the source Kit slot.
- Its current-tree upgrade path can still migrate generated Kit folders after
  legacy `PAR_*` symbols disappear from `ParameterArray.h`: it reads
  `kitset.kcg`, associates each member file with its one-based slot, and
  rewrites numeric own-slot LFO voice selectors to `self`.
- It does not rewrite legacy `0` placeholders to `self`.
- Root `SD_CARD/Instrument/` files were refreshed from the migrated Kit tree so
  individual Instrument Load exercises the portability case.

Legacy `.SND` extraction facts preserved from the audit:

| Source slot | Voice selector byte | Target parameter byte |
| --- | ---: | ---: |
| 1 | `PAR_VOICE_LFO1` payload offset 161 | `PAR_TARGET_LFO1` payload offset 167 |
| 2 | `PAR_VOICE_LFO2` payload offset 162 | `PAR_TARGET_LFO2` payload offset 168 |
| 3 | `PAR_VOICE_LFO3` payload offset 163 | `PAR_TARGET_LFO3` payload offset 169 |
| 4 | `PAR_VOICE_LFO4` payload offset 164 | `PAR_TARGET_LFO4` payload offset 170 |
| 5 | `PAR_VOICE_LFO5` payload offset 165 | `PAR_TARGET_LFO5` payload offset 171 |
| 6 | `PAR_VOICE_LFO6` payload offset 166 | `PAR_TARGET_LFO6` payload offset 172 |

Distinct legacy target indices seen in `SD_CARD/P*.SND` and their current
descriptor meanings:

| Legacy target | Legacy symbol | Current meaning |
| ---: | --- | --- |
| 0 | `PAR_NONE` | off |
| 1 | `PAR_VOICE_DECIMATION_ALL` | global decimation; converts to off as non-instrument descriptor target |
| 2 | `PAR_COARSE1` | `osc1_pitch_coarse` |
| 8 | `PAR_MOD_EG1` | `pitch_envelope_decay` |
| 10 | `PAR_MODAMNT1` | `pitch_envelope_amount` |
| 15 | `PAR_FM_FREQ1` | `osc2_pitch_coarse` |
| 18 | `PAR_TRANS1_WAVE` | `transient_wave` |
| 24 | `PAR_FILTER_DRIVE_1` | `filter_drive` |
| 25 | `PAR_FREQ_LFO1` | `lfo_rate` |
| 26 | `PAR_SYNC_LFO1` | `lfo_sync` |
| 28 | `PAR_WAVE_LFO1` | `lfo_wave` |
| 33 | `PAR_VOICE_DECIMATION1` | `instrument_decimation` |
| 34 | `PAR_DRIVE1` | `instrument_drive` |
| 37 | `PAR_FINE2` | `osc1_pitch_fine` |
| 40 | `PAR_VELOD2` | `amp_envelope_decay` |
| 50 | `PAR_MOD_WAVE_DRUM2` | `osc2_wave` |
| 58 | `PAR_FILTER_DRIVE_2` | `filter_drive` |
| 59 | `PAR_FREQ_LFO2` | `lfo_rate` |
| 125 | `PAR_FILTER_DRIVE_4` | `filter_drive` |

Interpretation: the legacy target byte names a parameter identity; the paired
legacy voice-selector byte names the destination voice. Current firmware
preserves the local descriptor identity and rebuilds the canonical target for
the selected destination voice during normalization.

## Descriptor-Domain LFO Modulation

The user-reported bug was that LFO to `amp_envelope_decay` moved in the wrong
audible direction and saturated at tiny amount values. Root cause: descriptor
LFO targets were installed as direct raw runtime pointers and ModulationNode
was applying a full runtime-range delta. For envelope decay, the runtime field
is an inverted/shaped `SlopeEg2` decrement, not the 0..127 parameter byte.

Original LXR negative polarity checked in `LXR-master`:

```c
target = current * amount * lfo + (1.f - amount) * current;
```

Because original LXR restores targets to the base value before each block, the
effective formula is:

```text
neg(base, amount, lfo) = base * (1 - amount + amount * lfo)
```

Consequences:

- amount `0` returns base.
- lfo `1` returns base.
- lfo `0` returns `base * (1 - amount)`.
- full negative amount moves from base down toward zero; it is not a
  full-range subtract from base.

Session 035 design decision: do not teach `ModulationNode` every envelope,
filter, pitch, transient, distortion, or LFO-rate curve. Each descriptor now
declares a modulation domain in parameter/storage units, and InstrumentManager
adapts descriptor targets so temporary LFO values go through the normal runtime
writer that already owns the DSP curve.

Implemented pieces:

- `InstrumentManager.h` gained `instrument_mod_domain_t` and
  `ParamDescriptor::mod_domain`.
- Drum, Snare, Cymbal, and HiHat descriptor row macros now declare modulation
  domain explicitly. Continuous sound controls use domains such as 0..127 or
  +/-63; waveform/sample rows use dynamic waveform max expansion; selectors
  like filter type, LFO sync, polarity, retrigger, velocity on/off, transient
  wave, and attack-repeat use no LFO domain.
- `instrumentManager_descriptorSupportsModulationRange()` now uses
  descriptor-owned domains instead of dtype/runtime guesses.
- InstrumentManager stores per-source/pair LFO descriptor adapters containing
  source slot, target slot, local descriptor index, target id, descriptor
  pointer, domain, and descriptor-domain base value.
- Descriptor LFO target install clears the raw `ModulationNode` destination and
  installs the InstrumentManager adapter instead.
- `instrumentManager_writeRuntimeInternal(..., notify_base_change)` splits
  ordinary writes from temporary LFO overlay writes. Public writes still refresh
  modulation baselines; temporary overlays apply quietly so they cannot become
  the retained base.
- `instrumentManager_noteRuntimeValueChanged()` refreshes adapter base values
  on ordinary menu/load/morph/automation writes.
- Restore and block overlay helpers apply through the quiet runtime writer.
- `instrumentManager_updateLfoAdapters()` now owns descriptor adapters,
  slot-decimation, and Scene target LFO destinations; `lfo.c` remains the
  oscillator/source fan-out only.
- `modNode_shapeParameterU16()` shapes descriptor parameter values. Negative
  polarity implements original LXR value-relative math in parameter space.
  Positive moves from base toward max. Bipolar uses per-side headroom to avoid
  clipping bias.

Important invariant: LFO changes only the temporary descriptor value sent to
the existing descriptor writer. It does not replace descriptor writer math.
Amount zero, no target, and ordinary menu/load/morph writes keep the same
unmodulated behavior.

Remaining modulation follow-up: `modNode_resetTargets()` and
`modNode_directOriginalValueChanged()` still need dynamic InstrumentManager
runtime-pool enumeration for any remaining non-adapter direct paths. Step
automation also still needs descriptor/Scene target migration.

## Kit Save

Normal `Save:[Kit     ]` now writes the active Scene kit to the new text
directory shape accepted by the loader:

```text
Kit/
  <NNNxxxxx>/
    kitset.kcg
    <slot1>.drm
    <slot2>.drm
    ...
```

Logical shape:

- `kitset.kcg` contains format/version, generated slot-6/track-7 non-Choke
  decay settings, then six `[slotN]` sections with type, file, and audio_out.
- Each instrument file contains one metadata header, one `[params]` section,
  and one `[morph]` section.
- `[params]` writes every descriptor file key, including supplemental target
  selectors.
- `[morph]` writes morphable endpoint descriptors only.
- LFO target voice selectors emit `self` when the selector points at the saved
  instrument's own one-based slot.

Kit slots:

- `STORAGE_KIT_MAX_SLOTS` is now 999.
- Root Kit folder display numbers are 001..999.
- Firmware Kit slot indices are `uint16_t` 0..998 through filesystem,
  presetManager, menu, and kitBrowser.
- Voice slots remain `uint8_t` 0..5.

Instrument source names:

- SceneData now owns `instrument_display_name[6][9]` and
  `instrument_stem[6][17]`.
- Defaults initialize through one helper as `inst_vo1`..`inst_vo6`.
- Kit load records stems from `kitset.kcg file=` entries.
- Root Instrument load stages and commits the selected filename stem only when
  the staged payload succeeds.
- Kit Save generates member filenames from the retained 16-character stem,
  sanitized/truncated to current FAT short-name constraints.
- Duplicate generated member filenames are forced to include a voice suffix so
  kitset references stay unique.

Filesystem implementation:

- `filesystem_requestSaveKitDirectory(slot, cb)` posts the new save.
- Normal `preset_saveDrumset(slot, 0)` uses the directory save. Legacy morph
  save (`isMorph=1`) remains on the old flat `.SND` path.
- `filesystem_saveKitDirectory_tick()` is an async state machine: chdir root,
  create/open `Kit`, chdir, create/open target folder, chdir, write
  `kitset.kcg`, write six instrument files, return root, and update the Kit
  scan cache.
- Text files are streamed one line at a time using a bounded line buffer;
  storageTypes owns line contents and filesystem owns async write offsets.
- `storageTypes.c` uses bounded literal/assignment helpers instead of
  `snprintf`, because the first build with stdio formatting pulled embedded
  heap/syscall requirements and failed on `_sbrk`.

asyncfatfs boundary:

- No asyncfatfs core files changed in Session 035.
- Kit Save reuses existing primitives: `afatfs_mkdir()`,
  `afatfs_fopen(..., "w", ...)`, `afatfs_fclose()`, and `afatfs_chdir()`.
- `afatfs_funlink()` exists for files but is not a recursive directory replace.
- Current asyncfatfs can scan/display LFNs, but creation is short 8.3 entries
  only. Firmware-created Kit folders and instrument filenames are therefore
  sanitized short physical names such as `001SLAK` and `SLAKD1.DRM`.
- Missing core primitives before autosave/power-loss-safe replacement are true
  LFN creation, atomic rename/replace, and recursive directory delete/replace.
  Add those once at the asyncfatfs/filesystem boundary; do not recreate per
  caller.
- Because recursive directory replacement is unavailable, Kit Save overwrites
  authoritative files and may leave stale unreferenced files. The loader ignores
  them because `kitset.kcg` is authoritative.

## Documentation Updates

Durable documents updated:

- `knowledge_files/specification_reference/FILESYSTEM_SPEC.md`
- `knowledge_files/specification_reference/MODULE_INTERCHANGE_SPEC.md`
- `SCOPING_TARGETS.md`
- `MEMORY.md`
- `knowledge_files/log_archive/000_SESSION_INDEX.md`
- this `035_SESSION_HANDOFF_LOG.md`

Scratch audits now represented here and in the durable specs:

- `INST_LFO_SELF_AUDIT.md`
- `LFO_RANGE_MODULAR_AUDIT.md`
- `KIT_SAVE_AUDIT.md`

`LOAD_MORPH_AUDIT.md` also remains represented by the session work: KitMrp and
InstrumentMrp are implemented with same-type/no-change mismatch semantics.

## Verification

Local checks performed during implementation:

- `python3 -m py_compile tools/convert_legacy_kits.py` passed during the LFO
  `self` data/tooling pass.
- Converter/data audits confirmed `self` appears only on LFO voice selector
  keys, own-slot numeric selectors were migrated, cross-slot numeric selectors
  remain numeric, and root `SD_CARD/Instrument/` was refreshed from Kit data.
- `make -B -j4` passed after public Kit slot width changes.
- `make -j4` passed after final Kit Save formatter/comment polish.
- `git diff --check` passed.

Build size after final Kit Save implementation was approximately:

```text
text 305664, data 348, bss 129596, dec 435608
```

The usual nosys syscall warnings and LTO serial note remain. They are not new
Kit Save failures.

## Changed Files Of Interest

- `Core/Hardware/SD/filesystem.c/h`: KitMrp staging path, Kit Save directory
  writer, 999-slot APIs, root Instrument stem staging/accessor, Kit scan/cache
  updates, request routing.
- `Core/Hardware/SD/storageTypes.c/h`: `self` parser support, 001..999 folder
  parsing, type-to-text/extension helpers, descriptor text writers, 8.3 saved
  instrument filename helper, stdio-free line formatting.
- `Core/Hardware/SD/kitBrowser.c/h`: compatibility Kit browser widened to 999
  slots / `uint16_t`.
- `Core/Scene/SceneData.c/h`: retained 16-character instrument source stems,
  defaults `inst_vo1`..`inst_vo6`, shared source-name setters.
- `Core/Scene/Preset/presetManager.c/h`: KitMrp/InstrumentMrp commit paths,
  widened Kit slot request fields, directory Kit Save routing, Instrument stem
  commit, Morph-only apply path.
- `Core/Menu/menu.c/h`: KitMrp and InstrumentMrp UI, 999-slot display/scroll,
  widened `menu_currentPresetNr`.
- `Core/DSP/Instruments/InstrumentManager.c/h`: descriptor modulation domains,
  descriptor LFO adapters, quiet runtime write path, base refresh, LFO adapter
  fan-out API.
- Drum/Snare/Cymbal/HiHat parameter files: explicit modulation-domain metadata
  on descriptor rows.
- `Core/DSPAudio/modulationNode.c/h`: parameter-domain shaping helper and
  original-LXR negative formula.
- `Core/DSPAudio/lfo.c`: fan-out to `instrumentManager_updateLfoAdapters()`.
- `tools/convert_legacy_kits.py` and generated `SD_CARD/Kit` /
  `SD_CARD/Instrument`: `self` migration.

## Known Issues / Remaining Work

1. Standalone Instrument Save is still pending. It should reuse
   `storage_formatInstrumentLine()` and the same `self` serialization rule used
   by Kit Save.
2. Scene and Bank structures/load/save remain pending, including `sceneset.scg`,
   `bankset.bcg`, embedded `Kit <kit name>/`, `pattern.pat`, and `effect.fx`.
3. Descriptor-aware step automation remains pending. Do not route canonical
   descriptor/Scene IDs through legacy 8-bit MIDI CC/CC2 playback.
4. Dynamic modulation-node enumeration remains pending for any direct/non-adapter
   paths. Descriptor LFO targets now avoid raw DSP pointer writes; future code
   must preserve that owner-writer model.
5. asyncfatfs still lacks true LFN creation, atomic rename/replace, and
   recursive directory replace/delete. Autosave/reload `.tmp` promotion must not
   assume these exist.
6. Hardware smoke testing should cover saving default and loaded Kits, loading
   saved slots above 255, duplicate instrument names, LFO self round-trip, and
   morph-load mismatch no-change behavior.

## End Of Session Block

```text
DATE: 2026-07-13
SESSION GOAL: Complete Kit/Instrument Morph Load, LFO `self` storage,
descriptor-domain LFO scaling, and normal new-format Kit Save.
COMPLETED: KitMrp and InstrumentMrp load, `self` load/save/tooling/data,
descriptor-domain LFO adapters with original-LXR negative math, 001..999 Kit
slots, Scene-retained instrument stems, and directory-format Kit Save.
VERIFIED ON HARDWARE: partially; user reported the Morph Load work "seems ok."
Local `make -B -j4`, `make -j4`, and `git diff --check` pass.

CHANGES THIS SESSION:
- filesystem/storageTypes: Kit directory save writer, 999-slot Kit APIs, text
  writer helpers, LFO `self`, root Instrument stem staging.
- Preset/Menu/SceneData/kitBrowser: KitMrp/InstrumentMrp load, widened Kit slot
  state, source-name retention, save routing, morph-only apply paths.
- InstrumentManager/modulationNode/lfo/instrument descriptors: descriptor-owned
  modulation domains, LFO descriptor adapters, original-LXR negative shaping.
- converter/SD_CARD data: generated Kit and Instrument files migrated to use
  `self` where appropriate.
- docs/specs: scratch audits folded into durable specs and this handoff.

KNOWN ISSUES INTRODUCED: none known, but Kit Save physical names are 8.3-safe
short names because asyncfatfs cannot create LFNs yet; stale unreferenced files
may remain in saved Kit folders.
KNOWN ISSUES RESOLVED: LFO own-slot portability, descriptor LFO negative
polarity/amount scaling, Kit/Instrument Morph Load endpoint import, and normal
Kit Save being a no-op/legacy mismatch.

NEXT SESSION RECOMMENDED GOAL: implement standalone Instrument Save using the
same storage writer and `self` rules, then begin Scene/Bank structure work or
descriptor-aware step automation depending on priority.
BLOCKERS: hardware smoke testing of saved Kit folders, slots above 255, LFO
self round-trip, and morph-load mismatch behavior is still needed.

CRITICAL REMINDERS FOR NEXT SESSION:
- Do not recreate FAT writers in callers. Reuse filesystem state machines and
  existing asyncfatfs primitives; add missing LFN/rename/recursive primitives
  once at the asyncfatfs/filesystem boundary if needed.
- `self` is storage-only for LFO voice selector keys, never a runtime value.
- Descriptor LFO/automation work must go through owner runtime writers, not raw
  DSP member pointers.
- Pattern automation is still legacy and must be migrated before claiming
  descriptor target work is complete.
```
