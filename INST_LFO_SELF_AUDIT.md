# Instrument LFO `self` Target Audit

## Purpose

Phase 3 save work needs instrument files to be slot-portable. If an
instrument's LFO destination voice points at the same kit slot the instrument
currently occupies, the file should save that voice selector as:

```text
lfo_target_voice=self
```

The same rule applies independently to the second destination pair:

```text
lfo_target_voice_2=self
```

`self` is storage syntax only. It is not a descriptor value, Menu value,
SceneData sentinel, packed `instrument_param_id_t`, or DSP runtime state. The
loader must resolve it immediately to the ordinary one-based destination slot
before Preset normalizes and applies the LFO voice/parameter pair.

## Correct Model

Each LFO destination pair is two stored cells:

- `lfo_target_voice` or `lfo_target_voice_2`: a one-based visible destination
  voice selector. This is the only cell that may use the file token `self`.
- `lfo_target_param` or `lfo_target_param_2`: a `uint16_t` selector containing
  either `INSTRUMENT_PARAM_INVALID` or a packed descriptor target ID. This
  remains numeric.

On load, Preset's existing pair normalization reads the numeric voice selector,
extracts the local descriptor index from `lfo_target_param`, and rebuilds the
packed target for the selected destination voice. That means the storage parser
should only translate `self` into a numeric voice value. It should not rewrite
the parameter selector directly and should not add any new runtime branch.

## Current Code Findings

- `storage_instrumentParseLine()` is the common parser for both Kit-member
  instrument files and root `Instrument/` loads.
- `storage_instrument_state_t.expected_slot` is already set from the final
  destination:
  - Kit loads pass `op_instrument_slot + 1`.
  - Root Instrument loads pass `op_instrument_load_destination_slot + 1`.
- The current parser handles LFO voice selectors only as numbers and clamps
  legacy `0` values into the valid `1..6` range.
- `preset_normalizeLfoTargetPair()` and
  `instrumentManager_writeRuntime()` already expect numeric selectors. They
  should remain unaware of `self`.
- `tools/convert_legacy_kits.py` owns both fresh legacy `.SND` conversion and
  the no-legacy-enum upgrade path for already-generated Kit folders.
- `SD_CARD/Instrument/` is a copied root pool derived from generated Kit member
  files. It should be refreshed from the Kit tree after the Kit tree is
  migrated, rather than edited as an unrelated second authority.

## Required Code Changes

### `Core/Hardware/SD/storageTypes.h`

Update the parser contract comment for `storage_instrumentParseLine()`.

Comment text to land near the declaration:

> `self` is accepted only for `lfo_target_voice` and
> `lfo_target_voice_2`. The token is resolved with
> `storage_instrument_state_t.expected_slot` before writing Scene-owned
> descriptor images, so every caller after storage sees the same numeric
> `1..6` selector it already handles today.

Why this change must happen:

The public header is the boundary contract for `filesystem.c`, the converter,
and future save code. Without this comment, a future change could incorrectly
add `self` to Menu display values or runtime descriptor storage.

### `Core/Hardware/SD/storageTypes.c`

Change `storage_instrumentParseLine()` only in the branch handling
`INSTRUMENT_BIND_LFO_TARGET_VOICE` and
`INSTRUMENT_BIND_LFO_TARGET_VOICE_2`.

Implementation shape:

1. Detect the LFO voice selector binding before calling `storage_parseU8()`.
2. If `value` is exactly `self`, validate that `expected_slot` is `1..6` and
   use `expected_slot` as the parsed numeric value.
3. Otherwise parse the existing numeric value and retain the current
   compatibility clamp for old `0` placeholders and out-of-range values.
4. Keep the existing section behavior: `[morph]` ignores supplemental selector
   writes after syntax validation, while `[params]` stores the normalized
   selector.

Comment text to land near the new branch:

> `self` is a file-only relocation alias for LFO destination voices. It is
> resolved at parse time because the filesystem state machine already knows the
> final destination slot, and Preset's LFO pair normalization intentionally
> works only with numeric voice selectors and packed parameter IDs.

Why this change must happen:

Root Instrument load can place the same `.drm/.snr/.cym/.hat` file into any
slot. If a file saved from slot 3 contains `lfo_target_voice=3`, loading that
file into slot 1 would keep targeting slot 3. The `self` token lets the file
preserve "this instrument's own voice" without inventing a new SceneData value.

### `tools/convert_legacy_kits.py`

Update the conversion and upgrade paths.

Fresh conversion:

- Let `instrument_values()` return string values as well as integers.
- When the emitted key is `lfo_target_voice` or `lfo_target_voice_2`, compare
  the legacy voice selector to the source slot.
- If the value equals the one-based source slot, emit `"self"`.
- Otherwise emit the original numeric value.
- Do not apply the `self` rule to `lfo_target_param`; that value remains the
  canonical parameter selector produced by `legacy_target_to_canonical()`.

Upgrade path:

- When current `ParameterArray.h` no longer exposes legacy sound `PAR_*`
  symbols, `upgrade_existing_kit_tree()` must still migrate existing generated
  Kit folders.
- Parse each `kitset.kcg` to associate a member file with its one-based slot.
- For each listed member file, rewrite `lfo_target_voice=<slot>` to
  `lfo_target_voice=self` in both `[params]` and `[morph]`.
- Include `_2` keys even though current generated files do not yet contain
  them, so the converter stays aligned with the two-target LFO schema.
- Do not rewrite `0` placeholders to `self`. Legacy `0` means old empty/default
  selector text, not an intentional self-reference.

Comment text to land near the converter helper:

> The `self` decision is made from the source Kit slot and the LFO voice
> selector only. The paired target parameter may contain a legacy voice-local
> canonical ID; firmware normalization later combines its local descriptor with
> the loaded numeric destination voice.

Why this change must happen:

The converter is the authority for regenerated `SD_CARD/Kit/` and the copied
root `SD_CARD/Instrument/` pool. If it keeps writing numeric self-targets, new
save/load behavior will be impossible to test against the shipped data set.

### Generated Data

After the converter is updated:

1. Run the converter.
2. Confirm Kit member files now use `self` where their LFO destination voice
   selector equals their Kit slot.
3. Rebuild `SD_CARD/Instrument/` from the migrated Kit tree using the
   converter's existing root-pool population path.
4. Confirm no root Instrument file diverges from its Kit source except for
   intentional duplicate filenames, if any are already part of the pool policy.

Why this change must happen:

Root `Instrument/` files are exactly the portability case this token is meant
to fix. Updating only Kit files would leave individual Instrument loads with
the old absolute self-slot behavior.

## Future Save Changes

When new-format Kit and Instrument save operations land, use one shared
serialization helper for LFO voice selector cells:

```text
stored_value == current_one_based_slot ? "self" : decimal voice number
```

That helper should be used for:

- Kit save, for each instrument member in its assigned slot.
- Individual Instrument save, for the slot being exported.
- Both LFO destination voice keys.

Comment text for the future save helper:

> LFO target voices are serialized with `self` only when the stored numeric
> selector equals the source instrument slot. Cross-slot modulation remains a
> decimal voice number so loading the file elsewhere preserves the explicit
> external target.

Why this future helper must exist:

Without one shared helper, Kit save and Instrument save can drift. The token is
not a property of an instrument type; it is a relationship between the saved
slot and the selector value at serialization time.

## Legacy `.SND` LFO Extraction Audit

Using the pre-descriptor sound enum from git history, the old flat `.SND`
payload offsets for LFO target routing are:

| Source slot | Voice selector byte | Target parameter byte |
| --- | ---: | ---: |
| 1 | `PAR_VOICE_LFO1` at payload offset 161 | `PAR_TARGET_LFO1` at payload offset 167 |
| 2 | `PAR_VOICE_LFO2` at payload offset 162 | `PAR_TARGET_LFO2` at payload offset 168 |
| 3 | `PAR_VOICE_LFO3` at payload offset 163 | `PAR_TARGET_LFO3` at payload offset 169 |
| 4 | `PAR_VOICE_LFO4` at payload offset 164 | `PAR_TARGET_LFO4` at payload offset 170 |
| 5 | `PAR_VOICE_LFO5` at payload offset 165 | `PAR_TARGET_LFO5` at payload offset 171 |
| 6 | `PAR_VOICE_LFO6` at payload offset 166 | `PAR_TARGET_LFO6` at payload offset 172 |

Distinct `PAR_TARGET_LFO*` values found in `SD_CARD/P*.SND`:

| Legacy target index | Legacy target symbol | Current descriptor meaning |
| ---: | --- | --- |
| 0 | `PAR_NONE` | off |
| 1 | `PAR_VOICE_DECIMATION_ALL` | global decimation, currently converts to off because it is not an instrument descriptor target |
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

Important interpretation:

The legacy target index table names a parameter on a specific old voice, but
the stored `PAR_VOICE_LFO*` byte is the destination voice selector that the UI
used. Current firmware preserves the parameter's local descriptor identity and
then rebuilds the packed target for the selected voice during normalization.
Therefore the table above is best read as "parameter name", not as the final
target slot after load.

## Verification Plan

Required checks before merging implementation:

1. Run `python3 -m py_compile tools/convert_legacy_kits.py`.
2. Run the converter once and inspect the data diff.
3. Run the converter a second time and confirm it is idempotent.
4. Audit every generated Kit member:
   - `self` appears only on `lfo_target_voice` or `lfo_target_voice_2`.
   - A numeric value equal to that member's Kit slot no longer appears on those
     keys.
   - Numeric cross-slot values remain numeric.
   - Legacy `0` values remain numeric unless the converter later makes a
     separate, explicit compatibility decision.
5. Audit every root `SD_CARD/Instrument/` file against its Kit source copy.
6. Build with `make -j4`.
7. Run `git diff --check`.

## Open Questions

- Should future save output preserve numeric `0` from old imported files, or
  should all saved selector values be normalized before serialization? Current
  runtime already normalizes away `0`, so new save code will probably emit
  either `self` or a valid decimal voice, but the migration should not invent
  intent for old `0` values.
- Should Scene modulation voice value `7` ever gain a file token such as `scn`?
  Existing spec text mentions a display value, but current parser accepts only
  numbers for the LFO voice selector. This is separate from `self` and should
  not be mixed into the first implementation.

## Work Log

### 2026-07-12 implementation pass

- Updated `storage_instrumentParseLine()` so `self` is accepted only by
  `lfo_target_voice` and `lfo_target_voice_2`, resolved through the parser's
  one-based `expected_slot`, and stored as the same numeric selector Preset
  already normalizes.
- Updated the `storageTypes.h` parser contract comment to document that `self`
  is file syntax only and never a Menu, SceneData, descriptor, or DSP runtime
  value.
- Updated `tools/convert_legacy_kits.py` so fresh legacy conversion emits
  `self` when a legacy LFO voice selector equals the source slot, while
  leaving `lfo_target_param` numeric.
- Added an existing-tree upgrade helper for the current no-legacy-enum path.
  It reads each `kitset.kcg`, rewrites only the listed member file for that
  slot, and preserves numeric cross-slot targets plus legacy zero placeholders.
- Kept root `SD_CARD/Instrument/` filenames equal to their Kit member filenames
  while refreshing contents from the Kit tree. This avoids an unrelated
  Instrument browser rename from the converter's stale kit-number prefix logic.
- `python3 -m py_compile tools/convert_legacy_kits.py` passed.
- First converter run reported `touched 40 files` and copied 186 root
  Instrument files. The second run reported `touched 0 files`, confirming the
  Kit migration is idempotent.
- Slot-aware data audit passed: 80 `self` assignments in Kit files, 80 in root
  Instrument files, no `self` tokens on non-LFO-voice keys, no remaining
  numeric self-targets in Kit members, and every root Instrument file
  byte-matches its Kit source member.
- `make -j4` passed. The linker still reports the established nano-libc
  `_close`/`_lseek`/`_read`/`_write` warnings and serial-LTO note.
- `git diff --check` passed.
