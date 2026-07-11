# LFO Expansion Audit

Date: 2026-07-11

Scope: plan a three-page LFO UI/runtime expansion with two target selector
pairs, per-target amounts, and shared LFO polarity. This is a planning document
only; no code was changed for this audit.

## Proposed User Shape

Each instrument voice keeps the existing LFO concept but expands it to three
menu pages:

1. LFO page 1: core oscillator controls.
   - `lfo_rate`
   - `lfo_sync`
   - `lfo_wave`
   - `lfo_offset`

2. LFO page 2: performance/modulation-shape controls.
   - `lfo_retrigger_voice`
   - new `lfo_polarity`
   - `lfo_amount`
   - new `lfo_amount_2`

3. LFO page 3: modulation destinations.
   - `lfo_target_voice`
   - `lfo_target_param`
   - new `lfo_target_voice_2`
   - new `lfo_target_param_2`

This interprets "four new LFO parameters per voice" as:

- `lfo_polarity`
- `lfo_amount_2`
- `lfo_target_voice_2`
- `lfo_target_param_2`

Short-name page layout:

- Page 1: `frq snc wav ofs`
- Page 2: `rtg pol am1 am2`
- Page 3: `vo1 ds1 vo2 ds2`

The existing target pair moves to the third LFO page, a second target pair is
added beside it, and polarity is shared by both target pairs.

## Current State

- LFO runtime state is `Lfo` in `Core/DSPAudio/lfo.h`.
- Each `Lfo` currently owns one `ModulationNode modTarget`.
- `lfo_dispatchNextValue()` calculates one `0..1` LFO value and sends it to
  `modNode_updateValue(&lfo->modTarget, val)`.
- Descriptor rows currently include one LFO target pair:
  `lfo_target_voice` / `lfo_target_param`.
- The new descriptor-backed modulation backend can already resolve a target
  selector into a live runtime `Parameter`, so the second target pair should
  reuse that backend rather than adding any hardcoded target list.

## Code-Dive Implementation Blueprint

This pass inspected the current runtime, menu, storage, and morph paths. The
implementation should be descriptor-driven throughout: no hardcoded lists of
modulatable parameters, and no assumptions that a voice slot contains a fixed
instrument type.

### 1. `Core/DSPAudio/lfo.h`

Required changes:

- Add an LFO polarity enum or constants visible to descriptor/menu writers and
  `lfo.c`. Recommended names:
  - `LFO_POLARITY_NEGATIVE = 0`
  - `LFO_POLARITY_POSITIVE = 1`
  - `LFO_POLARITY_BIPOLAR = 2`
- Add these fields to `Lfo`:
  - `ModulationNode modTarget2;`
  - `uint8_t polarity;`

Why:

- The second destination needs a separate `ModulationNode` because amount,
  original restore value, direct runtime pointer, waveform-interpolation state,
  cached range, and last dispatched value are destination-specific. Folding a
  second destination into the existing node would either duplicate half the node
  fields manually or make target 1 and target 2 fight over one restore baseline.
- Polarity belongs to `Lfo`, not to each `ModulationNode`, because the requested
  shape is one oscillator with shared wave/rate/sync/offset/retrigger/polarity
  and two independent destinations/amounts.

Inputs and outputs:

- Inputs: descriptor/runtime writes set `lfo.polarity`, `lfo.modTarget.amount`,
  and `lfo.modTarget2.amount`.
- Outputs: `lfo_dispatchNextValue()` reads the raw oscillator value and sends
  the shaped modulation request to both nodes.

Accessors, clients, affiliates:

- `lfo_init()` initializes both modulation nodes and default polarity.
- `lfo_dispatchNextValue()` applies target 1 and target 2.
- Instrument descriptors bind the new fields.
- InstrumentManager selects which `ModulationNode` receives target pair 1 or
  target pair 2.
- `modNode_resetTargets()` and descriptor original-value refresh must include
  `modTarget2`.

Comment text needed near the struct:

- Explain that `modTarget` and `modTarget2` are not two LFOs; they are two
  destinations for one shared LFO signal. The separate nodes exist because each
  destination has its own amount, cached runtime pointer, restore baseline, and
  range contract.

CPU impact:

- Memory: one extra `ModulationNode` plus padding per voice LFO.
- Audio hot path: at most one extra `modNode_update...()` call per LFO per block
  when the second node exists. An off node returns after a pointer check.

### 2. `Core/DSPAudio/lfo.c`

Required changes:

- `lfo_init()`:
  - initialize `polarity` to negative,
  - call `modNode_init(&lfo->modTarget2)`.
- `lfo_dispatchNextValue()`:
  - keep `lfo_calc()` unchanged as the source of the raw `0..1` waveform,
  - call the new polarity-aware modulation API for both nodes:
    - target 1: `lfo.modTarget`
    - target 2: `lfo.modTarget2`

Why:

- `lfo_calc()` should stay a pure oscillator/value generator. It does not know
  target type, target range, destination base value, or whether the target is
  a waveform selector with interpolation. Polarity cannot be implemented
  correctly there.
- `lfo_dispatchNextValue()` is already the single point where the oscillator
  value becomes target modulation, so it is the correct place to fan out to two
  destinations.

Inputs and outputs:

- Input: an initialized `Lfo *`.
- Output: both active modulation targets receive the same oscillator sample for
  the current block, shaped according to shared polarity and each node's own
  amount/range.

Why no extra helper is needed:

- A tiny `lfo_dispatchTargetPair()` wrapper would only forward three arguments
  to `modNode_updateValuePolarity()` and would hide the fact that the two target
  nodes intentionally share one oscillator value. Keep the two calls adjacent.

Comment text needed near `lfo_dispatchNextValue()`:

- Explain that waveform generation and target shaping are deliberately split:
  the LFO emits `0..1`, while `ModulationNode` applies polarity because only the
  node knows the target's base value and cached min/max contract.

CPU impact:

- `lfo_calc()` cost is unchanged.
- Adds one additional node update per LFO block. The second call is cheap when
  destination is off.

### 3. `Core/DSPAudio/modulationNode.h`

Required changes:

- Add a small range contract type, for example:
  - `float min;`
  - `float max;`
  - `uint8_t valid;`
- Add range fields to `ModulationNode`, preferably as that small struct.
- Add polarity constants or a modulation-node polarity enum if the LFO enum is
  not visible here without circular includes. Keep values aligned with the LFO
  menu values.
- Extend the direct destination API so the install path supplies a cached range
  contract:
  - current: `modNode_setDirectDestination(vm, destination, parameter, waveInterpTarget)`
  - new: include a range-contract argument.
- Replace or extend `modNode_directOriginalValueChanged(destination)` so it can
  refresh both:
  - the restore baseline,
  - the cached range/base contract for active direct nodes targeting that
    descriptor.
- Add a polarity-aware update API:
  - `void modNode_updateValuePolarity(ModulationNode *vm, float val, uint8_t polarity);`
- Keep `modNode_updateValue(ModulationNode *vm, float val)` as the legacy/velocity
  wrapper using negative polarity.

Why:

- Stable modulation amount requires cached target range. The current value-
  relative formula makes modulation depth shrink when the base value is small.
  The new formula must use target range width, not `current * amount`.
- Range lookup must not happen inside the audio-block update for every active
  modulation tick. InstrumentManager already resolves descriptor metadata at
  target install and ordinary value apply time, so it should hand the range to
  ModulationNode once and refresh it only when the base value changes.
- `modNode_updateValue()` must remain because velocity modulation and existing
  callers should keep compiling and should preserve current negative behavior
  until velocity is intentionally redesigned.

Inputs and outputs:

- `modNode_setDirectDestination()` inputs: node, canonical descriptor id,
  resolved runtime `Parameter`, optional waveform interpolation target, cached
  target range. Output: returns nonzero when a live target was installed.
- `modNode_directOriginalValueChanged()` inputs: canonical descriptor id and
  the refreshed range contract for that descriptor. Output: all active direct
  nodes targeting that id recapture restore baseline and range.
- `modNode_updateValuePolarity()` inputs: node, raw LFO value `0..1`, polarity.
  Output: the live target is restored-relative modulated and clamped.

Accessors, clients, affiliates:

- InstrumentManager is the only direct target resolver and should be the only
  producer of descriptor-backed range contracts.
- `lfo_dispatchNextValue()` is the LFO client of polarity-aware updates.
- `modNode_reassignVeloMod()` remains a client of legacy negative updates.
- `modNode_resetTargets()` restores both legacy and direct nodes, including
  the new `Lfo::modTarget2` instances.
- Oscillator waveform interpolation remains affiliated through
  `waveInterpTarget` and the existing global interpolation budget.

Why not put range math in the morph engine:

- `presetMorph_tick()` already applies one descriptor value at a time through
  `preset_applyInstrumentRuntimeValue()`, which reaches
  `InstrumentManager_writeRuntime()`. Putting min/max rebuilding in the morph
  worker would duplicate the menu/load/automation write path and miss any future
  runtime writer that does not come from morph. The right contract point is the
  InstrumentManager runtime write notification.

Comment text needed near the new range struct/API:

- Explain that range is cached because modulation amount must be stable against
  small base values, and because descriptor/range resolution is too expensive
  and too cross-layer to perform in the audio-block update.

CPU impact:

- Adds a few floats/bytes to each node.
- Adds polarity branch/clamp math per active target update.
- Avoids descriptor scans in the audio path by caching range.

### 4. `Core/DSPAudio/modulationNode.c`

Required changes:

- Reset the new range fields in `modNode_resetIdentity()`.
- Capture both original value and range:
  - `modNode_captureOriginalValue()` should continue to capture the exact value
    that `modNode_resetTargets()` restores.
  - A separate private block should copy the range supplied by
    InstrumentManager into the node when installing or refreshing a direct
    target.
- Update `modNode_setDirectDestination()`:
  - accept the range argument,
  - restore old target,
  - install direct pointer,
  - cache range,
  - capture original value.
- Update direct original-value refresh:
  - include `voiceArray[0..2].lfo.modTarget2`, `snareVoice.lfo.modTarget2`,
    `cymbalVoice.lfo.modTarget2`, and `hatVoice.lfo.modTarget2`.
  - refresh range and original value together.
- Update `modNode_resetTargets()`:
  - restore all six `modTarget2` nodes in the same generation as target 1.
- Add `modNode_updateValuePolarity()`:
  - early return on null/off target,
  - save `lastVal`,
  - compute target base from `originalValue`,
  - use cached range width for amount scaling,
  - clamp to range,
  - write typed result.
- Keep `modNode_updateValue()` as:
  - `modNode_updateValuePolarity(vm, val, LFO_POLARITY_NEGATIVE)` or the
    modulation-node equivalent value.

Range-relative polarity formula:

- Base value is `originalValue` captured after the last ordinary runtime write.
- Range width is `range.max - range.min`.
- Amount is node-local `0..1`.
- Negative keeps the current phase convention: high LFO value is closer to the
  base, low LFO value moves downward.
  - `delta = -amount * (1.0f - val) * width`
- Positive:
  - `delta = amount * val * width`
- Bipolar:
  - `delta = amount * ((val * 2.0f) - 1.0f) * (width * 0.5f)`
- Final:
  - `modulated = clamp(base + delta, range.min, range.max)`

Why this fixes the stability issue:

- Amount is scaled by the target's usable range, not by the current/base value.
  A parameter sitting near zero still receives a musically comparable sweep
  until it hits the clamp boundary.

Typed write details:

- `TYPE_FLT`, `TYPE_SPECIAL_F`, `TYPE_SPECIAL_P`, `TYPE_SPECIAL_FILTER_F` write
  the clamped float.
- `TYPE_UINT8` writes the clamped value cast to byte. Waveform targets must
  keep the existing interpolation path and max-wave clamp.
- `TYPE_UINT32` should not be considered modulatable until an explicit range
  contract exists. The audit recommendation is to reject it in the modulation
  target resolver rather than inventing a huge generic range.

Why no pile of one-line helpers:

- A private `modNode_clampFloat()` is reasonable if clamp logic is repeated in
  several typed branches. Do not add wrappers that merely call one existing
  function with the same arguments.
- Keep the waveform interpolation branch inside the `TYPE_UINT8` write path
  because it shares clamped float, max-wave, and budget state with ordinary byte
  writing.

Comment text needed near `modNode_updateValuePolarity()`:

- Explain the range-relative amount contract, the negative phase convention,
  and why waveform interpolation remains bounded by the existing global budget.

CPU impact:

- Active target cost increases by branch, clamp, and several float operations.
- Second targets double target-update count only when installed; off nodes exit
  quickly.
- Range refresh cost is event-driven, not per block.

### 5. `Core/DSP/Instruments/InstrumentManager.h`

Required changes:

- Extend `instrument_binding_kind_t`:
  - `INSTRUMENT_BIND_LFO_TARGET_VOICE_2`
  - `INSTRUMENT_BIND_LFO_TARGET_PARAM_2`

Why:

- The menu and storage systems locate sibling target cells by binding kind, not
  by descriptor index. A second target pair needs distinct binding identities so
  target pair 1 and target pair 2 can coexist in the same descriptor table.

Inputs and outputs:

- Inputs: descriptor rows assign one of the new binding kinds.
- Outputs: InstrumentManager/Menu lookup functions can retrieve the correct
  local descriptor index for each target pair.

Accessors, clients, affiliates:

- Instrument descriptor files use the enum in `ROW_NOBIND()`.
- `menu_lfoTargetContext()` finds sibling voice/param cells by binding kind.
- `storageTypes.c` parses 16-bit target IDs and clamps target voice fields by
  binding kind.
- `instrumentManager_writeRuntime()` routes target pair 2 to `modTarget2`.

Comment text needed near the enum:

- Explain that target pair bindings are separate because voice and parameter
  cells are paired supplemental storage values, and descriptor order can change
  per instrument.

CPU impact:

- None.

### 6. `Core/DSP/Instruments/InstrumentManager.c`

Required changes:

- Replace hardcoded registry descriptor/page counts with the exported constants:
  - `drum_param_descriptor_count`, `drum_menu_page_count`, etc.
- Expand `instrument_runtime_target_t` with a cached modulation range contract.
- Add a non-trivial private helper to build that range contract from descriptor
  metadata and the resolved live parameter.
- Update target validation so modulation rejects target types whose range
  contract is not defined, especially `TYPE_UINT32`.
- Change `instrumentManager_noteRuntimeValueChanged()` to rebuild the target
  range for the changed descriptor and pass it with the direct original-value
  refresh.
- Change `instrumentManager_lfoModNodeForSlot()` to accept target pair index:
  - index 0 returns `lfo.modTarget`,
  - index 1 returns `lfo.modTarget2`.
- Change `instrumentManager_installLfoModulationTarget()` to accept target pair
  index and pass the cached range to `modNode_setDirectDestination()`.
- Update `instrumentManager_installVelocityModulationTarget()` to pass the new
  range argument too.
- Update `instrumentManager_writeRuntime()`:
  - `INSTRUMENT_BIND_LFO_TARGET_VOICE`: validate `1..6`,
  - `INSTRUMENT_BIND_LFO_TARGET_VOICE_2`: same validation,
  - `INSTRUMENT_BIND_LFO_TARGET_PARAM`: install pair 0,
  - `INSTRUMENT_BIND_LFO_TARGET_PARAM_2`: install pair 1.
- Update `instrumentManager_resetSlot()` defaults:
  - target voice bindings default to `1`,
  - target parameter bindings default to `INSTRUMENT_PARAM_INVALID`,
  - velocity target should also default to `INSTRUMENT_PARAM_INVALID` if not
    already handled elsewhere,
  - all new image parameters default to zero except polarity, which defaults to
    zero and therefore already means negative.

Range contract helper details:

- Inputs: target slot, descriptor, resolved `Parameter`, optional waveform
  affiliation.
- Output: valid min/max range for the direct modulation node.
- Recommended ranges:
  - `TYPE_FLT`: `0.0f..1.0f`.
  - `TYPE_SPECIAL_F`: `0.0f..2.0f` with base captured separately as `1.0f`;
    this gives stable negative/positive amount independent of current edit
    value and keeps bipolar symmetric around the neutral multiplier.
  - `TYPE_UINT8` plus waveform affiliation: `0..modNode_getMaxWaveformIndex()`
    or a public max-wave helper if the current helper remains private.
  - `TYPE_UINT8` plus `DTYPE_MENU`: `0..getMaxEntriesForMenu(menu_id)-1` is
    tempting but `getMaxEntriesForMenu()` is private to Menu and should not be
    pulled into InstrumentManager. Prefer explicit runtime/type rules or keep
    menu-like byte rows modulatable as `0..127` only where that is musically
    acceptable. This is a design checkpoint before implementation.
  - `TYPE_UINT8` plus ordinary 7-bit dtypes: `0..127`.
  - `TYPE_UINT32`: invalid for modulation unless a descriptor-specific range is
    added.

Why the helper must exist:

- The target resolver currently resolves pointer/type/wave-interp state. Range
  building is related but more detailed: it needs descriptor dtype, runtime
  scalar type, current instrument type, and waveform affiliation. Keeping it as
  a private helper prevents range rules from being duplicated in install,
  refresh, and future normalization paths.
- It is not a single-line helper; it owns a real cross-field contract and should
  be documented.

Accessors, clients, affiliates:

- `instrumentManager_resolveModulationTarget()` is the producer.
- `modNode_setDirectDestination()` and direct refresh are consumers.
- `presetMorph_tick()`, menu edits, storage load, automation, and future MIDI
  all reach `instrumentManager_writeRuntime()` and therefore get range refresh
  without bespoke logic.
- `sampleMemory_getNumSamples()`/oscillator waveform constraints may be needed
  for waveform targets if `modNode_getMaxWaveformIndex()` remains private.

Comment text needed:

- Near the registry: explain why exported counts are used to avoid stale manual
  counts as descriptor rows are added.
- Near range helper: explain stable amount, why `TYPE_UINT32` is excluded, and
  why Menu's private table lookups are not a DSP-layer dependency.
- Near reset defaults: explain that supplemental target cells cannot default to
  zero because zero can be a valid descriptor id; off is the explicit invalid
  sentinel.

CPU impact:

- Target install/refresh may scan descriptors to recover the changed local id,
  but that already happens off the audio hot path.
- Audio path receives cached range and remains bounded.

### 7. Instrument Descriptor Files

Files:

- `Core/DSP/Instruments/Drum/DrumParameters.c`
- `Core/DSP/Instruments/Snare/SnareParameters.c`
- `Core/DSP/Instruments/Cymbal/CymbalParameters.c`
- `Core/DSP/Instruments/HiHat/HiHatParameters.c`

Required descriptor changes in all four instruments:

- Add enum values:
  - `*_PARAM_LFO_POLARITY`
  - `*_PARAM_LFO_AMOUNT_2`
  - `*_PARAM_LFO_TARGET_VOICE_2`
  - `*_PARAM_LFO_TARGET_PARAM_2`
- Change existing `lfo_amount` short name from `amt` to `am1`.
- Add:
  - `ROW_MENU("lfo_polarity", "LFO", "Polarity", "pol", MENU_LFO_POLARITY, lfo.polarity, TYPE_UINT8)`
  - `ROW("lfo_amount_2", "LFO", "Amount 2", "am2", DTYPE_0B127, lfo.modTarget2.amount, TYPE_FLT)`
  - `ROW_NOBIND("lfo_target_voice_2", "LFO", "DstVoice2", "vo2", DTYPE_VOICE_LFO, INSTRUMENT_BIND_LFO_TARGET_VOICE_2)`
  - `ROW_NOBIND("lfo_target_param_2", "LFO", "DstParam2", "ds2", DTYPE_TARGET_SELECTION_LFO, INSTRUMENT_BIND_LFO_TARGET_PARAM_2)`
- Change existing target pair short names:
  - `lfo_target_voice`: `vo1`
  - `lfo_target_param`: `ds1`

Required menu layout changes:

- Replace the current single LFO page with three pages:
  - `frq snc wav ofs`
  - `rtg pol am1 am2`
  - `vo1 ds1 vo2 ds2`
- For hihat, update both `hihat_menu_pages` and `hihat_open_menu_pages`.
- Because every instrument adds two extra pages, each `*_menu_page_count` should
  grow by two through the existing `sizeof()` count calculation.

Why:

- The descriptors are the source of truth for file keys, menu labels, dtype,
  morph/mod/automation flags, runtime offsets, and supplemental bindings.
- Page layout must remain descriptor-indexed so future instruments can move or
  omit rows without Menu learning per-instrument arrays.

Inputs and outputs:

- Inputs: menu/render/storage lookup by descriptor index or file key.
- Outputs: runtime writes bind directly to new `Lfo` fields or supplemental
  target cells.

Accessors, clients, affiliates:

- InstrumentManager registry reads descriptor arrays and counts.
- Menu renders the new pages through `instrumentManager_voicePageDescriptorIndex()`.
- `storageTypes.c` loads the new file keys automatically through descriptor key
  lookup once parsing recognizes the new binding kinds.
- Future Kit save writes these keys using the same descriptor metadata.

Comment text needed:

- Near the LFO rows: explain that target pair 1 and pair 2 share one LFO
  oscillator and polarity but own independent amount/destination nodes.
- Near the split LFO pages: explain that the three pages are a layout decision,
  not a change in storage hierarchy.

CPU impact:

- No audio cost from descriptor/page rows themselves.
- Slightly larger descriptor tables and two more menu pages per instrument.

### 8. `Core/Menu/MenuText.h` And `Core/Menu/menu.c`

Required `MenuText.h` changes:

- Add `MENU_LFO_POLARITY`.
- Add table:
  - count `3`,
  - `neg`,
  - `pos`,
  - `bi`.

Required `menu.c` table changes:

- Add `MENU_LFO_POLARITY` to `getMaxEntriesForMenu()`.
- Add `MENU_LFO_POLARITY` to `getMenuItemNameForValue()`.

Required target-pair generalization in `menu.c`:

- Replace `menu_cellIsLfoTargetVoice()` and `menu_cellIsLfoTargetParam()` with
  logic that recognizes both binding pairs, or make them call a shared helper.
- Add a pair resolver that maps a cell binding to:
  - pair 0: `INSTRUMENT_BIND_LFO_TARGET_VOICE` /
    `INSTRUMENT_BIND_LFO_TARGET_PARAM`
  - pair 1: `INSTRUMENT_BIND_LFO_TARGET_VOICE_2` /
    `INSTRUMENT_BIND_LFO_TARGET_PARAM_2`
- Expand `menu_lfo_target_context_t` to store the selected pair's binding
  identities or pair index.
- Update `menu_lfoTargetContext()` so it finds sibling voice/param descriptor
  indices for the same pair, not always the first pair.
- Ensure existing display/edit helpers use the context's sibling indices; their
  core behavior remains:
  - voice clamps to `1..6`,
  - parameter list shows exactly one `off`,
  - non-modulatable descriptors are skipped,
  - changing target voice preserves the same local descriptor only when valid
    on the new target slot, otherwise resets to off.
- Update every call site that checks `menu_cellIsLfoTargetVoice()` or
  `menu_cellIsLfoTargetParam()` so the second pair receives the same clamp,
  display normalization, encoder edit, and knob edit behavior.

Why:

- Target pair behavior is not specific to descriptor names or positions. It is
  a relationship between two supplemental descriptor cells. The menu must infer
  that relationship from binding kind so instruments can change layouts.

Inputs and outputs:

- Inputs: a resolved menu cell and user delta from encoder/endless knob.
- Outputs: SceneData supplemental cells store a clamped voice value and a
  canonical target descriptor id or `INSTRUMENT_PARAM_INVALID`.

Why the pair resolver should exist:

- It is not a single-line convenience wrapper; it centralizes the binding-pair
  map that would otherwise be duplicated in voice checks, parameter checks, and
  context construction. This is the correct amount of abstraction.

Comment text needed:

- Explain that Menu never walks a hardcoded target list; it asks
  InstrumentManager to step valid descriptor targets for the selected slot.
- Explain that pair 1 and pair 2 share behavior but must not share storage
  indices.

CPU impact:

- Menu-only, foreground. Pair resolution scans descriptor bindings only during
  render/edit, not in the audio path.

### 9. `Core/Hardware/SD/storageTypes.c`

Required changes:

- Treat `INSTRUMENT_BIND_LFO_TARGET_PARAM_2` as a 16-bit descriptor target like
  the existing LFO target param and velocity target.
- Treat `INSTRUMENT_BIND_LFO_TARGET_VOICE_2` as a clamped `1..6` voice value.
- Ensure missing target-param keys default to `INSTRUMENT_PARAM_INVALID` through
  `instrumentManager_resetSlot()`, not zero.
- Ensure missing target-voice keys default to `1`.
- Do not store target selector values in `[morph]`; they are supplemental
  single-endpoint values.

Why:

- A canonical target id can exceed 255, so target parameter cells must parse
  as `uint16_t`.
- Zero is a valid canonical descriptor id for slot 0 descriptor 0. Therefore
  missing or off target params cannot use zero as an implicit off state.

Inputs and outputs:

- Inputs: file key/value text from instrument files.
- Outputs: generic per-slot SceneData cells, either main image values or
  supplemental routing values.

Accessors, clients, affiliates:

- `instrumentManager_descriptorIndexByKey()` identifies the descriptor row.
- `instrumentManager_resetSlot()` supplies correct defaults before keys load.
- `preset_applyInstrumentRuntimeValue()` later applies the loaded values.

Comment text needed:

- Extend the current parser comments to say both LFO target parameter bindings
  are canonical descriptor ids and both voice bindings are paired selector
  context.

CPU impact:

- File-load only.

### 10. `Core/Scene/Preset/presetMorphEngine.c`

Required changes:

- No direct morph engine change is recommended for range refresh.
- Confirm that `lfo_polarity` should be morphable if it uses `ROW_MENU`.
  Morphing menu values will effectively step through integer states as the
  interpolated byte crosses thresholds. If that is not wanted, make polarity a
  supplemental non-morphable binding instead. Current recommendation: keep it
  as `ROW_MENU` for consistency with LFO waveform/sync/retrigger unless testing
  shows it feels wrong.

Why:

- The morph worker already calls `preset_applyInstrumentRuntimeValue()`, which
  reaches `InstrumentManager_writeRuntime()`. Range refresh should happen below
  that common runtime writer so menu edits, morph, Kit load, automation, and
  future MIDI all share one path.

Inputs and outputs:

- Inputs: main/morph image values and morph amount.
- Outputs: one runtime value apply per morph tick.

CPU impact:

- No added morph-loop descriptor/range work. Range refresh stays in the common
  runtime apply path and only occurs for the descriptor value already being
  applied.

### 11. Future Kit Save And Scene/Bank Definitions

Required future save behavior:

- Kit save must write:
  - `lfo_polarity`
  - `lfo_amount`
  - `lfo_amount_2`
  - `lfo_target_voice`
  - `lfo_target_param`
  - `lfo_target_voice_2`
  - `lfo_target_param_2`
- The saved folder shape should match the current Kit load shape.

Scene/Bank note:

- These are per-instrument Kit values. Scene/Bank files should select Kits and
  manage higher-level scene/bank state; they should not duplicate per-kit LFO
  target settings unless a later spec intentionally moves ownership.

CPU impact:

- None in audio; save/load only.

## Overall CPU Assessment

Baseline:

- Current runtime does six LFO waveform calculations per audio block and one
  modulation target update per LFO.
- Modulation restore happens once per audio block through `modNode_resetTargets()`.

Expected added cost:

- Second target pair: potentially doubles modulation target update/restore work
  for LFOs, from 6 LFO targets to 12 LFO targets per block.
- Polarity: low per-active-target overhead if ranges are cached at install time.
- Per-target amount 2: essentially free beyond the second target's existing
  `ModulationNode` math, because each node already stores its own amount.

Worst case:

- All six LFOs enabled, each with two active targets, bipolar polarity, and
  waveform interpolation targets active.
- This adds six extra target updates per block and extra polarity math per
  active target.
- The waveform interpolation budget is already bounded by
  `OSC_WAVE_INTERP_MAX_ACTIVE`; second targets can compete for that budget but
  should not make it unbounded.

Risk level:

- Overall CPU risk is low to moderate if target ranges are cached.
- CPU risk becomes moderate if target range resolution happens inside every
  `modNode_updateValue()` or if second targets bypass the existing waveform
  interpolation budget.

Recommended guardrails:

- Keep target range and optional waveform interpolation affiliation cached in
  `ModulationNode` at target-install time.
- Keep off second targets very cheap.
- Keep waveform interpolation budget shared across both target pairs.
- Run `make`, `make img`, and then profile/observe runtime CPU on hardware with
  all six LFOs using two active targets and bipolar polarity.

## Feedback On `TYPE_SPECIAL_F` Ranging

You are right to challenge this. The current negative implementation does not
appear to use any special range table for `TYPE_SPECIAL_F`. It treats
`TYPE_SPECIAL_F` as a direct float overlay target and captures
`originalValue.flt = 1.0f`, then the existing modulation math pushes that
multiplier downward according to amount and LFO value.

The important correction is that amount must not be proportional to the current
base value. If a parameter is sitting near zero, value-relative modulation makes
the sweep almost disappear. The implementation should therefore re-establish a
min/max contract at descriptor target install time and again whenever the base
value changes through the normal runtime write path.

For `TYPE_SPECIAL_F`, the first-pass contract should be:

- range min: `0.0f`
- neutral/base: `1.0f`
- range max: `2.0f`

That gives the modulation node a stable range width of `2.0f` regardless of the
edited value that caused the multiplier overlay. Negative modulation moves down
from the captured base toward the lower clamp, positive moves up toward the
upper clamp, and bipolar uses half the range width around the base. The result
may clip near the range edges, but the amount control remains stable and
predictable.

This does not balloon morph calculation if the refresh lives below morph:

- `presetMorph_tick()` already applies one descriptor through
  `preset_applyInstrumentRuntimeValue()`.
- That reaches `InstrumentManager_writeRuntime()`.
- `InstrumentManager_writeRuntime()` should notify ModulationNode once for the
  descriptor that changed.
- ModulationNode refreshes cached base/range for active nodes that target that
  descriptor.

So morph does not need to scan every target or calculate min/max inside its
interpolation loop. The refresh is event-driven and touches only active nodes
that are already installed against the changed descriptor id.

## Open Design Questions

1. Decide whether `DTYPE_MENU` byte parameters should be modulatable with a
   generic `0..127` range, a descriptor-specific range, or excluded unless they
   have explicit range support. Pulling Menu's private menu table lookup into
   InstrumentManager would be the wrong dependency direction.
2. Decide whether `lfo_polarity` should be morphable as an image row. Keeping it
   as `ROW_MENU` is consistent with current LFO wave/sync/retrigger behavior,
   but mode interpolation will step through integer polarity states.
