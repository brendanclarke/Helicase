# LFO Range Modular Audit

## Goal

Fix descriptor-backed LFO modulation so `neg` polarity matches original LXR
behavior exactly, while avoiding a hardcoded LFO parameter switch. The fix must
leave unmodulated runtime math unchanged: amount `0`, no target, and every
ordinary menu/load/morph/automation write must keep using the current
descriptor writer behavior.

## Original LXR Reference

Source checked:

- `LXR-master/mainboard/LxrStm32/src/DSPAudio/modulationNode.c`
- `LXR-master/mainboard/LxrStm32/src/DSPAudio/lfo.c`
- `LXR-master/mainboard/LxrStm32/src/DSPAudio/SlopeEg2.c`
- `LXR-master/mainboard/LxrStm32/src/MIDI/MidiParser.c`
- `LXR-master/mainboard/LxrStm32/src/MIDI/ParameterArray.c`

The original `modNode_updateValue()` negative-polarity formula is:

```c
target = current * amount * lfo + (1.f - amount) * current;
```

Original LXR calls `modNode_resetTargets()` before each block, so `current` is
the restored base/original parameter value. Therefore the actual contract is:

```text
neg(base, amount, lfo) = base * (1 - amount + amount * lfo)
```

Consequences:

- `amount=0` always returns `base`.
- `lfo=1` always returns `base`.
- `lfo=0` returns `base * (1 - amount)`.
- Full negative amount moves from `base` down to zero; it does not subtract a
  full descriptor range from `base`.

This is the behavior the new descriptor LFO path must reproduce for `neg`.

## Current Port Problem

Current descriptor-backed LFO targets are installed as direct `ModulationNode`
runtime pointers:

- `InstrumentManager.c` resolves a canonical descriptor target into a live
  `Parameter` pointer plus a cached `mod_node_range_t`.
- `modulationNode.c` applies a generic range-relative formula directly to that
  runtime pointer.

That fails for many descriptor targets because the runtime pointer is not the
same unit as the file/menu parameter:

- `amp_envelope_decay` user value is `0..127`, but runtime
  `SlopeEg2::decay` is an inverted per-sample decrement. Smaller runtime
  decrement means longer audible decay.
- Filter frequency, resonance, drive, oscillator pitch, transient pitch,
  distortion, LFO rate, and envelope times/slopes all have owner-specific
  scaling or side effects.
- Current `neg` subtracts a full runtime range from the runtime base; original
  LXR `neg` multiplied the base value by the LFO-shaped factor.

This is why `amp_envelope_decay` direction is reversed and why amount values
around `3..4` already saturate: the LFO is writing too large a delta into a
small, shaped runtime field.

## Current Call-Path Audit

The current install/update path is:

```text
Menu/storage target id
  -> instrumentManager_targetValid()
  -> instrumentManager_installLfoModulationTarget()
  -> instrumentManager_resolveModulationTarget()
  -> instrumentManager_buildModulationRange()
  -> modNode_setDirectDestination()
  -> lfo_dispatchNextValue()
  -> modNode_updateValuePolarity()
  -> modNode_rangeValue()
  -> raw runtime pointer write
```

Specific findings:

- `instrumentManager_targetValid()` uses
  `instrumentManager_descriptorSupportsModulationRange()` to decide whether a
  descriptor is selectable as a modulation target. That helper guesses from
  `runtime.parameter_type`, `dtype`, and a waveform-name special case.
- `instrumentManager_buildModulationRange()` turns the same guessed metadata
  into a `mod_node_range_t` used by `ModulationNode`.
- `instrumentManager_resolveModulationTarget()` returns a live
  `Parameter.ptr` and runtime type for descriptors with
  `INSTRUMENT_BIND_INSTANCE_OFFSET`.
- `instrumentManager_installLfoModulationTarget()` currently sends that live
  pointer into `modNode_setDirectDestination()`.
- `lfo_dispatchNextValue()` always calls `modNode_updateValuePolarity()` for
  both LFO pairs before calling
  `instrumentManager_updateLfoSceneTarget()` for supplemental targets.
- `modNode_rangeValue()` currently applies negative polarity as
  `base - amount * (1 - source) * width`, where `width` is the runtime range.
- `instrumentManager_writeRuntime()` and `instrumentManager_writeSpecialRuntime()`
  already contain the correct descriptor-value-to-DSP-owner write path for
  envelopes, filters, pitch, transient, distortion, LFO rate, and slot
  decimation.

The important conclusion is that the correct writer already exists. The fix is
not to duplicate the scaling in `ModulationNode`; it is to stop bypassing
`instrumentManager_writeRuntime()` during descriptor LFO updates.

## Design Decision

Do not teach `ModulationNode` the curve for every parameter. Instead:

1. Descriptor/instrument code reports whether a parameter is modulatable and
   what its parameter-domain range is.
2. `InstrumentManager` resolves LFO targets into a small adapter containing:
   target ID, slot, descriptor index, base parameter value, min/max domain, and
   an apply/restore path.
3. `ModulationNode` shapes in parameter-domain units, using the original LXR
   `neg` formula.
4. The adapter applies the shaped temporary value through
   `instrumentManager_writeRuntime()`, so the DSP owner keeps the existing
   curve/scaling/side effects.

The key invariant:

```text
LFO changes only the temporary value sent to the existing descriptor writer.
It does not replace the descriptor writer's math.
```

## Required Files And Planned Changes

### `Core/DSP/Instruments/InstrumentManager.h`

Add descriptor-owned modulation-domain metadata.

Suggested public type:

```c
typedef struct {
    uint16_t min_value;
    uint16_t max_value;
    uint8_t flags;
} instrument_mod_domain_t;
```

Add domain field to `ParamDescriptor`:

```c
instrument_mod_domain_t mod_domain;
```

The new field should sit next to `flags`, before `runtime`, because it modifies
the targetability contract while `runtime` remains the owner-specific storage
binding. The descriptor row initializer shape becomes:

```c
{ key, short, long, category, dtype, flags, mod_domain, runtime }
```

Suggested flags:

- `INSTRUMENT_MOD_DOMAIN_NONE`: not LFO/velocity modulatable.
- `INSTRUMENT_MOD_DOMAIN_CONTINUOUS`: safe for LFO continuous updates.
- `INSTRUMENT_MOD_DOMAIN_INTEGER`: integer-domain but continuous enough, e.g.
  waveform when interpolation is enabled.
- optional future flag for signed display domains if needed.

Comment text to land near the struct:

> Modulation domain is the parameter-space contract for transient LFO and
> velocity overlays. It deliberately describes the value before owner-specific
> DSP shaping. Envelope, pitch, filter, and distortion curves remain inside the
> normal descriptor runtime writer; the modulation domain only tells
> ModulationNode which temporary parameter values are legal to ask the owner to
> apply.

Why this must happen:

`dtype` and `runtime.parameter_type` are not enough. They describe display and
storage/runtime scalar type, but they cannot distinguish continuous values from
selectors such as filter type, FM mode, on/off, sync, or retrigger. Without a
descriptor-owned domain, `ModulationNode` either keeps guessing or gains a
hardcoded target list.

Follow-up validator change:

`instrumentManager_descriptorSupportsModulationRange()` should either be
deleted or reduced to a compatibility wrapper around `descriptor->mod_domain`.
`instrumentManager_targetValid(..., INSTRUMENT_TARGET_MODULATION)` should stop
using dtype/runtime guesses and require a non-`NONE` modulation domain for
descriptor targets. Slot decimation can either use a normal descriptor domain
or keep the explicit supplemental check, but it should no longer depend on the
generic dtype rule.

Comment text for the validator:

> Modulation target validity is descriptor-declared, not inferred from C scalar
> type. A `TYPE_UINT8` row can be a continuous envelope byte, a waveform id, a
> sync selector, or an on/off switch; only the descriptor's modulation domain
> says whether an LFO may continuously overlay it. This keeps Menu target
> stepping and DSP target installation on the same owner-authored contract.

### Instrument parameter descriptor files

Files:

- `Core/DSP/Instruments/Drum/DrumParameters.c`
- `Core/DSP/Instruments/Snare/SnareParameters.c`
- `Core/DSP/Instruments/Cymbal/CymbalParameters.c`
- `Core/DSP/Instruments/HiHat/HiHatParameters.c`

Update descriptor row macros so each row declares its modulation domain.

Suggested macro families:

```c
#define MOD_NONE       { 0u, 0u, INSTRUMENT_MOD_DOMAIN_NONE }
#define MOD_0_127      { 0u, 127u, INSTRUMENT_MOD_DOMAIN_CONTINUOUS }
#define MOD_PM63       { 0u, 127u, INSTRUMENT_MOD_DOMAIN_CONTINUOUS }
#define MOD_WAVE       { 0u, 0u, INSTRUMENT_MOD_DOMAIN_INTEGER }
#define MOD_0_1        { 0u, 1u, INSTRUMENT_MOD_DOMAIN_CONTINUOUS }
```

The existing row helpers currently expand to six descriptor fields plus
`BIND(...)`:

```c
{ key_, short_, long_, cat_, dtype_, FLAGS_IMAGE, BIND(member_, type_) }
```

They should be updated so `ROW`, `ROW_MENU`, `ROW_NOBIND`, `ROW_NOBIND_IMAGE`,
and `ROW_SLOT_DECIMATION` take or supply a modulation-domain argument. A
minimal churn version is to add explicit `mod_` parameters only to the normal
row helpers:

```c
#define ROW(key_, cat_, long_, short_, dtype_, mod_, member_, type_) \
    { key_, short_, long_, cat_, dtype_, FLAGS_IMAGE, mod_, BIND(member_, type_) }
```

`ROW_NOBIND` should always use `MOD_NONE`. `ROW_SLOT_DECIMATION` should use
`MOD_0_127` because it is a descriptor-owned supplemental sound parameter.
`MOD_WAVE` should carry a dynamic-max flag or sentinel; InstrumentManager can
expand it through the existing waveform affiliate logic so the descriptor does
not need to hardcode sample-count limits.

Rows that should be continuous parameter-domain targets:

- oscillator coarse/fine/noise/mod-osc frequency rows
- filter frequency/resonance/drive
- amp envelope attack/decay/slope, including HiHat closed/choke decay rows
- pitch envelope decay/slope/amount
- volume, pan, oscillator/FM amount, noise mix
- instrument drive, instrument decimation
- transient volume/frequency, and possibly waveform if interpolation is active
- LFO amount 1/2 and LFO rate

Rows that should not be continuous LFO targets unless explicitly decided later:

- filter type
- oscillator/FM mode selectors such as `osc2_mod_type`
- velocity on/off
- retrigger voice
- sync rate
- LFO polarity
- LFO target voice/param selectors
- `amp_attack_repeat` unless we intentionally want stepped count modulation

Comment text to land near the macros:

> Modulation domains are declared next to the descriptor row because the
> instrument file owns both storage identity and safe parameter-domain range.
> The domain does not duplicate DSP scaling. It only states what temporary
> descriptor value an LFO may request; `instrumentManager_writeRuntime()` still
> owns the curve and side effects for that value.

Why this must happen:

The current broad `TYPE_UINT8` rule admits discrete selectors and misses the
fact that many `TYPE_FLT` runtime fields are shaped/inverted representations.
Putting domain metadata in the descriptor files keeps the target list modular
and instrument-owned.

Descriptor row review work:

- Every current `FLAGS_IMAGE` row needs an explicit decision: `MOD_NONE`,
  `MOD_0_127`, `MOD_0_1`, `MOD_WAVE`, or another named domain.
- The review should happen in the descriptor files, not in
  `InstrumentManager.c`, because the descriptor rows are the only place where a
  reader can see storage key, menu meaning, dtype, flags, and runtime binding
  together.
- The comments should explain non-obvious exclusions. For example, `filter_type`
  remains morphable/automatable if desired, but its modulation domain is
  `MOD_NONE` because an LFO block should not sweep an enum selector as if it
  were a continuous filter parameter.

### `Core/DSP/Instruments/InstrumentManager.c`

Replace direct LFO pointer installation for descriptor targets with an adapter
installation path.

Planned private adapter type:

```c
typedef struct {
    uint8_t active;
    uint8_t slot;
    uint8_t descriptor_index;
    instrument_param_id_t id;
    instrument_mod_domain_t domain;
    uint16_t base_value;
} instrument_lfo_target_adapter_t;
```

Store adapters for both LFO pairs:

```c
static instrument_lfo_target_adapter_t
    lfo_descriptor_targets[INSTRUMENT_SLOT_COUNT][2];
```

Change `instrumentManager_installLfoModulationTarget()`:

- Keep off, slot-decimation, and Scene target handling.
- For direct descriptor targets, do not call `modNode_setDirectDestination()`.
- Validate descriptor domain instead.
- Capture base from the active Scene's current morph/interpolation parameter
  image, not from the raw runtime pointer.
- Store the adapter for the source slot/pair.
- Clear the source `ModulationNode` direct destination so the old pointer path
  does not also write the same target.
- Leave the selected pair's amount in the existing source `ModulationNode` so
  `lfo.c` does not need a second amount owner.

Base capture detail:

The base value should be read from:

```c
scene->kit.instruments[target_slot].parameter_images.morph_interpolation[local]
```

That is the same descriptor-domain image currently used for slot-decimation
base capture. It tracks the audible Scene/Morph base and avoids deriving a
parameter byte back from shaped runtime fields such as `SlopeEg2::decay`.

Add apply/restore helpers:

```c
static void instrumentManager_restoreLfoDescriptorTarget(...);
static void instrumentManager_applyLfoDescriptorTarget(...);
```

`instrumentManager_applyLfoDescriptorTarget()` calls the new ModulationNode
range shaper in parameter-domain units, then calls:

```c
instrumentManager_writeRuntime(target_slot, descriptor, shaped_value);
```

For amount `0`, this must apply `base_value` or skip the write; either way the
runtime result must equal the current unmodulated descriptor writer result.

Comment text for install helper:

> Descriptor LFO targets are installed as parameter-domain adapters, not direct
> runtime pointers. The adapter captures the Scene/Morph base value and the
> descriptor's modulation domain, then every LFO block asks the normal runtime
> writer to apply a temporary shaped descriptor value. This preserves owner
> scaling for envelopes, filters, pitch, and other shaped targets while keeping
> target selection descriptor-driven.

Comment text for apply helper:

> LFO shaping happens before DSP scaling. The shaped value is a temporary
> descriptor value in the same domain as file/menu/morph storage; applying it
> through `instrumentManager_writeRuntime()` guarantees that amount zero and
> unmodulated operation use exactly the same math as ordinary parameter writes.

Comment text for base capture:

> Descriptor LFO bases come from the Scene/Morph parameter image, not from the
> live runtime field. Several runtime fields are shaped or inverted relative to
> storage bytes, so reading them back would make the adapter depend on DSP
> internals and would break amount-zero parity with ordinary descriptor writes.

Change `instrumentManager_restoreLfoSupplementalTarget()` or add a sibling
restore helper:

- Restore descriptor adapters before a target is cleared or replaced.
- Apply `base_value` through `instrumentManager_writeRuntime()` instead of
  writing a raw pointer.
- Clear the adapter after restore.
- Keep the existing `presetMorph_clearLfoSource()` behavior for hidden Scene
  Morph contributions.

Comment text for restore:

> Clearing an LFO descriptor target restores the cached descriptor-domain base
> through the normal runtime writer. This mirrors the old
> `modNode_clearDestination()` restore step, but keeps restoration in
> parameter space so envelope/filter/pitch owners receive the same value they
> would receive from a menu or load operation.

Change `instrumentManager_noteRuntimeValueChanged()`:

- Keep the existing direct-node refresh for any legacy/direct users still
  installed.
- Add a small scan over `lfo_descriptor_targets[source][pair]` and refresh
  `base_value` when `adapter.id` matches the written canonical target id.
- Refreshing should be quiet and side-effect-free: it updates the next LFO
  block's base; the ordinary writer has already applied the new runtime value.

Comment text for base refresh:

> Ordinary parameter writes move the descriptor base under an installed LFO.
> The adapter keeps a descriptor-domain base value so amount-zero modulation and
> target restore continue to match the latest menu/load/morph/automation write.
> Refreshing this cache is the adapter equivalent of
> `modNode_directOriginalValueChanged()` for the old raw-pointer backend.

Why this must happen:

InstrumentManager already owns descriptor lookup, active slot type, runtime
instance selection, Morph interpolation images, supplemental slot decimation,
and Scene target adapters. It is the right boundary to translate a selected
canonical target into an owner-backed apply path without making `ModulationNode`
depend on instrument registry internals.

### `Core/DSP/Instruments/InstrumentManager.h`

Expose only the minimal LFO adapter update API needed by `lfo.c` or
`modulationNode.c`, if any.

Preferred shape:

- Keep source LFO fan-out in `lfo.c`.
- `lfo.c` already calls `instrumentManager_updateLfoSceneTarget()` for
  supplemental/Scene targets.
- Rename or extend that function to update all non-direct-pointer LFO adapters:

```c
void instrumentManager_updateLfoAdapters(uint8_t source_slot,
                                         uint8_t target_pair,
                                         float lfo_value_0_1,
                                         uint8_t polarity,
                                         float amount);
```

It should cover:

- descriptor parameter-domain adapters
- slot decimation adapters
- Scene target adapters

Comment text:

> Update every InstrumentManager-owned LFO destination backend for one source
> pair. Direct `ModulationNode` pointers are no longer used for descriptor
> instrument targets; InstrumentManager owns descriptor adapters, slot
> decimation, and Scene targets because all three require owner-specific apply
> functions rather than raw pointer writes.

Why this must happen:

The current name `instrumentManager_updateLfoSceneTarget()` hides that it also
handles slot decimation and should soon handle descriptor adapters. A clearer
API prevents the old direct-pointer backend from lingering beside the new
parameter-domain path.

### `Core/DSPAudio/modulationNode.h`

Deprecate direct descriptor range use for LFO targets and add a parameter-domain
shaping helper.

Possible helper:

```c
uint16_t modNode_shapeParameterU16(uint16_t base,
                                   uint16_t min_value,
                                   uint16_t max_value,
                                   float source_0_1,
                                   float amount_0_1,
                                   uint8_t polarity);
```

Negative polarity must implement original LXR exactly for zero-based domains:

```text
base * (1 - amount + amount * source)
```

For positive and bipolar:

- Positive should move from base toward `max_value` by source and amount.
- Bipolar should move around base using headroom on each side and clamp.
- These are not original-LXR parity requirements unless old source proves
  otherwise; document them as the port's explicit positive/bipolar policy.

Comment text:

> This helper shapes descriptor parameter values, not DSP runtime fields.
> Negative polarity deliberately matches original LXR's value-relative formula:
> after each block reset, the base value is multiplied by
> `(1 - amount + amount * lfo)`. Positive and bipolar use the descriptor domain
> limits because original LXR did not provide those modes for descriptor targets
> in this port's form.

Why this must happen:

The existing `modNode_shapeRangeU16()` uses a full-range subtractive negative
formula. That is the source of the reported reversed/overscaled envelope decay
behavior. Keeping the old helper around for Scene targets would preserve the
bug there; either replace its negative branch or add a new helper and migrate
all LFO callers.

### `Core/DSPAudio/modulationNode.c`

Implement the parameter-domain helper and stop using direct pointer range math
for descriptor LFO targets.

Planned changes:

- Add `modNode_shapeParameterU16()`.
- Change `modNode_shapeRangeU16()` negative branch to call the same original
  LXR-compatible math, or retire it if no remaining caller needs the old name.
- Leave `modNode_updateValue()` legacy path intact for velocity/old
  ParameterArray callers.
- Keep `modNode_setDirectDestination()` only for any legacy direct-pointer
  targets that still intentionally use raw runtime fields; descriptor LFO
  targets should no longer go through it.

Comment text for negative math:

> Original LXR negative modulation is value-relative: the block-restored base
> value is multiplied by `(1 - amount + amount * source)`. It is not a
> subtraction from the full legal range. This exact formula is required so a
> decay value of 80 with amount 0.5 modulates between 40 and 80 in parameter
> space before the envelope owner converts it to its inverted runtime
> decrement.

Why this must happen:

This is the compatibility core. If `neg` remains full-range subtractive, every
small or nonlinear runtime target will keep saturating too early and shaped
targets will keep moving in the wrong audible direction.

### `Core/DSPAudio/lfo.c`

Route descriptor adapters through InstrumentManager.

Planned change:

- Keep calculating one raw LFO value per source.
- Keep calling `modNode_updateValuePolarity()` only for true legacy/direct
  nodes that remain installed.
- Replace/rename the InstrumentManager call so both descriptor adapters and
  supplemental/Scene targets are updated with the same source value, polarity,
  and pair amount.

Comment text:

> The LFO oscillator remains only a source. Destination ownership is split:
> ModulationNode handles any legacy raw node still installed, while
> InstrumentManager handles descriptor-backed adapters and supplemental Scene
> targets that must be applied through owner-specific writers.

Why this must happen:

`lfo.c` should not know descriptor tables, envelope curves, or Scene target
semantics. Its job is to emit the waveform and fan it to owners.

### `Core/DSPAudio/lfo.h`

No data-layout change is required unless naming wants to clarify that
`modTarget` and `modTarget2` may be empty for descriptor adapters.

If updated, add a comment near `Lfo::modTarget` / `modTarget2`:

> Descriptor instrument targets may now be installed in InstrumentManager
> adapters rather than these raw ModulationNode slots. The slots remain for
> legacy/direct backends and for storing amount per pair.

Why this may happen:

The amount field currently lives inside `ModulationNode`. If descriptor
adapters stop using direct nodes, `lfo.c` still reads `lfo->modTarget.amount`
and `modTarget2.amount`, so the struct remains a convenient pair amount owner.

## Preserve Unmodulated Behavior

The implementation must prove these invariants:

- With no LFO target installed, runtime behavior is unchanged.
- With amount `0`, the shaped value equals the base parameter-domain value.
- Base value restoration goes through `instrumentManager_writeRuntime()`, not a
  raw pointer write.
- Ordinary menu/load/morph/automation writes still call
  `instrumentManager_writeRuntime()` exactly as they do today.
- Descriptor writer math remains untouched:
  - `slopeEg2_setDecay()` still maps decay bytes.
  - filter shapers still map filter bytes.
  - oscillator pitch writes still update MIDI-frequency fields and recalc.
  - distortion/transient/LFO-rate special writers still own their conversions.

## Target Eligibility Audit

The descriptor domain pass should intentionally remove these currently
questionable LFO targets unless a later musical decision re-adds them with a
stepped domain:

- `filter_type`
- `osc2_mod_type`
- `velo_vol_on_off`
- `amp_attack_repeat`
- `lfo_sync`
- `lfo_retrigger_voice`
- `lfo_polarity`
- LFO target selector rows

This is not a file-format migration. Existing files that point to now-invalid
targets should normalize to off using the existing target validation behavior.

Velocity scope note:

`INSTRUMENT_TARGET_MODULATION` is currently shared by LFO and velocity target
selection. Tightening it to descriptor-owned modulation domains will also affect
velocity target lists. That is probably desirable for selector cleanup, but the
runtime velocity backend still uses direct `ModulationNode` pointers for
descriptor targets. The implementation should choose one of these explicit
paths:

- Preferred later-cleanup path: keep this pass focused on LFO adapters, but
  accept that velocity target eligibility is described by the same domain
  metadata until velocity is migrated.
- Larger same-pass path: add a matching velocity descriptor adapter so velocity
  also applies shaped descriptor-domain values through
  `instrumentManager_writeRuntime()`.

Do not leave an implicit mixed design where LFO target browsing uses descriptor
domains while velocity quietly relies on old dtype guesses.

## Implementation Sequence

1. Add `instrument_mod_domain_t` and `ParamDescriptor::mod_domain`.
2. Update descriptor row macros and every descriptor row in Drum/Snare/Cymbal/
   HiHat to declare a modulation domain.
3. Replace `instrumentManager_descriptorSupportsModulationRange()` dtype
   guessing with descriptor-domain checks and dynamic waveform expansion.
4. Add LFO descriptor adapter storage, install, restore, apply, and base-refresh
   helpers in `InstrumentManager.c`.
5. Add `modNode_shapeParameterU16()` and migrate `modNode_shapeRangeU16()` or
   its callers so negative polarity uses original LXR value-relative math.
6. Rename/extend `instrumentManager_updateLfoSceneTarget()` to
   `instrumentManager_updateLfoAdapters()` and update `lfo.c` fan-out.
7. Verify amount-zero, no-target, ordinary descriptor writes, and target-clear
   restore before checking audible polarity.

## Verification Plan

1. Build and boot-compile checks:
   - `make -j4`
   - `git diff --check`

2. Formula unit/smoke checks, either as host-side helper tests or temporary
   debug assertions:
   - `neg(80, 0, any_lfo) == 80`
   - `neg(80, 1, 1) == 80`
   - `neg(80, 1, 0) == 0`
   - `neg(80, 0.5, 0) == 40`
   - `neg(80, 0.5, 0.5) == 60`

3. Runtime regression checks:
   - LFO to `amp_envelope_decay`, negative polarity: increasing amount shortens
     decay exactly as lower parameter values do.
   - LFO to `amp_envelope_decay`, amount `0`: no audible change and same runtime
     value as unmodulated base after block reset.
   - LFO to `osc1_pitch_coarse`: amount scale follows the same parameter-byte
     movement expected by the original LXR formula, while pitch curve remains
     owned by oscillator writer.
   - LFO to `filter_freq`: direction follows the visible parameter value, while
     filter curve remains shaped by `SVF_directSetFilterValue()`.
   - LFO to `instrument_decimation` and Scene Morph/Decimation still use the
     same new negative formula and restore base values when target clears.

4. Target browser checks:
   - Discrete selectors no longer appear as continuous LFO targets unless their
     descriptors explicitly declare a stepped modulation domain.
   - Existing valid converted targets remain available where their descriptors
     declare domains.

## Open Questions Before Implementation

- Should positive polarity match a known LXR-02 convention already introduced
  in Session 033, or should it be defined now as base-to-max headroom?
- Should bipolar use symmetric full-domain depth around base or clamp by
  per-side headroom? Per-side headroom avoids clipping bias and is probably the
  more musical behavior, but it is not original-LXR parity.
- Should velocity modulation move to the same parameter-domain adapter in this
  pass? It shares some direct-target machinery, but the user-visible fault is
  LFO polarity/amount. Recommendation: fix LFO first, then decide whether
  velocity should follow once LFO is stable.
- Should waveform modulation remain continuous only when interpolation is
  enabled, or always be a stepped integer target? The current interpolation
  budget suggests the descriptor domain should carry a waveform/dynamic-max
  flag and let ModulationNode/InstrumentManager preserve the existing budget.

## Implementation Notes

2026-07-12:

- Added `instrument_mod_domain_t` and `ParamDescriptor::mod_domain` in
  `InstrumentManager.h`. The adjacent header comment explains that the domain
  is a descriptor-space overlay contract and that DSP scaling remains owned by
  `instrumentManager_writeRuntime()` and each DSP setter.
- Updated Drum, Snare, Cymbal, and HiHat descriptor row macros so every image
  row declares a modulation domain explicitly. Continuous sound controls use
  `MOD_0_127` or `MOD_PM63`, oscillator sample-wave rows use `MOD_WAVE`, and
  enum/selectors such as filter type, LFO sync, LFO wave, retrigger, polarity,
  velocity on/off, transient wave, and attack-repeat use `MOD_NONE`.
- Flattened `ROW_MENU` so it emits the descriptor initializer directly. The
  first build caught the nested macro form because forwarding `MOD_*` through
  `ROW_MENU -> ROW` let struct-initializer commas be re-parsed as macro
  arguments.
- Replaced dtype/runtime guessing in
  `instrumentManager_descriptorSupportsModulationRange()` with descriptor-owned
  domain expansion. Dynamic waveform max is expanded in InstrumentManager with
  `modNode_getMaxWaveformIndex()`, keeping sample-memory bounds out of the
  instrument parameter files.
- Added `instrument_lfo_target_adapter_t` and
  `lfo_descriptor_targets[slot][pair]`. Descriptor-backed LFO targets now store
  target id, slot, local descriptor index, descriptor pointer, domain, and
  descriptor-domain base value.
- Changed `instrumentManager_installLfoModulationTarget()` so descriptor
  targets clear the raw `ModulationNode` destination and install an
  InstrumentManager adapter instead of calling `modNode_setDirectDestination()`.
  Supplemental slot-decimation and Scene targets keep their existing adapter
  path.
- Added quiet/runtime writer split:
  `instrumentManager_writeRuntimeInternal(..., notify_base_change)`. Public
  writes still notify modulation baselines; temporary LFO adapter writes use the
  same DSP-owner math quietly so the temporary shaped value cannot become the
  next base.
- Updated `instrumentManager_noteRuntimeValueChanged()` to refresh installed
  LFO descriptor adapter base values on ordinary menu/load/morph/automation
  writes. LFO overlay writes do not call this refresh path.
- Added descriptor adapter restore/apply helpers. Restores and block overlays
  both call the quiet runtime writer, preserving owner math while avoiding
  retained/base-state mutation.
- Renamed the public LFO owner update API to
  `instrumentManager_updateLfoAdapters()` and updated `lfo.c` dispatch. The LFO
  oscillator remains only the source; InstrumentManager now owns descriptor,
  slot-decimation, and Scene destinations.
- Added `modNode_shapeParameterU16()` and changed `modNode_shapeRangeU16()` to
  forward to it. Negative polarity now matches original LXR value-relative math
  in parameter space: zero-based domains use
  `base * (1 - amount + amount * source)`. Positive moves base toward max;
  bipolar uses per-side headroom.
- Verification run:
  - `make -j4` passed, with the usual nano syscall and serial LTO warnings.
  - `git diff --check` passed.
