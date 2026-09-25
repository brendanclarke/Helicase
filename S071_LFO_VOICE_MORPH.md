# Session 071 - LFO Voice-Morph Integration Plan

## Purpose

Integrate LFO modulation of per-voice morph with the Session 070 scene-automation design so that an LFO modulates from the **current effective voice-morph value** toward a morph endpoint without writing the retained Scene value.

For positive LFO polarity, the intended signal chain is:

```text
retained Scene voice-morph value
              |
              +-- replaced temporarily by the current step-automation value, when active
                                      |
                                      v
                           effective morph base B
                                      |
                                      +-- LFO depth D moves B toward the full-morph endpoint
                                      v
                           effective applied morph E
```

The older LXR firmware used the same two-layer model: calculate the ordinary/current voice-morph result first, then interpolate that result toward the full-morph endpoint using the LFO. This plan preserves that behavior while retaining Helicase's newer positive, negative, and bipolar LFO-polarity support.

## Important current-state finding

This is not a greenfield feature in the current Helicase tree. LFO-to-voice-morph routing, UI selection, runtime storage, bounded application, cleanup, Scene reload, and persistence already exist and were reported hardware-verified in Session 033 (`knowledge_files/log_archive/033_SESSION_HANDOFF_LOG.md`, under the accepted LFO and per-voice Morph work).

The existing route is:

```text
LFO oscillator
  -> lfo.c
  -> instrumentManager_updateLfoAdapters()
  -> VOICE_MORPH adapter
  -> presetMorph_setVoiceLfoModulation()
  -> bounded preset-morph worker
```

Relevant current-tree references are:

- `Core/Bank/Scene/SceneModTargets.c:20-52`: declares `1vm` through `6vm` and gives them `SCENE_MOD_TARGET_USE_LFO` capability.
- `Core/DSP/Instruments/InstrumentManager.c:912-953`: accepts Scene as an LFO target voice and resolves the `scn` target token.
- `Core/DSP/Instruments/InstrumentManager.c:983-1008`: enumerates Scene LFO targets for the picker.
- `Core/Menu/menu.c:3545-3660` and `Core/Menu/menu.c:3815+`: edits and displays the Scene target voice/parameter.
- `Core/DSPAudio/lfo.c:134-160`: forwards every source/pair LFO value to `instrumentManager_updateLfoAdapters()`.
- `Core/DSP/Instruments/InstrumentManager.c:2282-2341`: handles `INSTRUMENT_MANAGER_LFO_ADAPTER_VOICE_MORPH`.
- `Core/Bank/Scene/Preset/presetMorphEngine.c:22-29`: owns the existing per-target/per-source/per-pair LFO contribution table.
- `Core/Bank/Scene/Preset/presetMorphEngine.c:93-155`: resolves layered voice-morph amounts and services them through the bounded worker.
- `Core/Bank/Scene/Preset/presetMorphEngine.c:552-604`: installs and clears LFO voice-morph contributions.
- `Core/DSP/Instruments/InstrumentManager.c:2429-2475`: restores supplemental targets when an LFO route is removed.
- `Core/DSP/Instruments/InstrumentManager.c:1338-1371`: clears runtime modulation targets.
- `Core/Bank/Scene/Preset/presetManager.c`: clears/rebuilds the modulation graph during relevant Scene/load transitions.
- `knowledge_files/specification_reference/FILESYSTEM_SPEC.md:1607-1660`: documents the Scene target picker, the hidden LFO layer, signed-delta summation, and bounded worker.

Therefore Session 071 should change only the representation and composition of the existing hidden LFO layer. It must not create a second LFO-to-morph feature or duplicate its menu, route, persistence, or lifecycle machinery.

## Why Session 071 is needed

At the time this plan was written, `S070_PHASE4_Q1_SCENE_AUTOMATION_IMPLEMENTATION.md` is a plan rather than an applied source change: the tree does not yet contain `morph_step_override`, `presetMorph_setStepAutomationOverride()`, or `seq_scene_automation_dirty`.

Session 070 proposes choosing the live morph base inside the morph engine:

```c
base = morph_step_override[voice].active
     ? morph_step_override[voice].amount
     : scene->settings.voice_morph_amount[voice];
```

That is the right ownership boundary, but one additional change is required. The existing `InstrumentManager.c` voice-morph adapter first calculates and stores an **absolute shaped amount** using the retained Scene morph amount as its base. The morph engine then converts that stored absolute amount back to a delta relative to its selected base. Once step automation supplies a different base, those two calculations no longer agree.

Example of the failure:

```text
retained base used by InstrumentManager = 0
active step base used by morph engine    = 128
positive LFO at half travel              = absolute stored amount about 128

old resolver: 128 + (stored 128 - resolver base 128) = 128
expected:     128 + half of (255 - 128)              = about 192
```

The LFO would appear to disappear on that step. The fix is to store a **base-independent direction and normalized depth**, and let the morph engine convert it to a delta using the effective base at application time.

Session 071 therefore supersedes the morph/LFO math portion of Session 070 Change 1. The two changes should land as one coordinated implementation, or Session 070 should land first and Session 071 immediately afterward before hardware acceptance.

## Required behavior

### Layer ownership

The retained Scene value remains the saved/menu-owned value. Step automation is a temporary base override. LFO modulation is a separate temporary layer on top of whichever base is currently effective.

```text
B = step voice-morph amount, if that voice has an active step override
    otherwise retained Scene voice-morph amount
```

No LFO tick and no step playback tick may write `scene->settings.voice_morph_amount[]`.

### Positive polarity: current base to full morph

For an encoded positive depth `D` in `[0, 255]`:

```text
positive_delta = round((255 - B) * D / 255)
E              = clamp(B + positive_delta, 0, 255)
```

This exactly expresses the requested behavior: `D = 0` leaves the current menu/step value unchanged and `D = 255` reaches the full-morph endpoint.

### Existing polarity behavior

The LXR implementation only needed the positive route. Helicase already exposes and has accepted general LFO polarity behavior, so Session 071 must preserve it:

```text
positive: source 0..1      -> depth 0..amount, direction toward full morph
negative: source 0..1      -> depth amount..0, direction toward main endpoint
bipolar: source -1..1      -> magnitude 0..amount, direction selected by sign
```

For a contribution directed toward the main endpoint:

```text
negative_delta = -round(B * D / 255)
```

Each active LFO source/pair contribution is converted to a signed delta around the same current base. Signed deltas are summed and the final amount is clamped:

```text
E = clamp(B + sum(each signed contribution delta), 0, 255)
```

This retains the current multi-source composition model while eliminating dependence on whichever base happened to be present when an LFO sample was received.

### Clear and lifecycle semantics

- Clearing a step override removes only the step-owned base. If an LFO route remains active, the morph engine must immediately resolve that same LFO layer around the retained base.
- Clearing or retargeting an LFO removes only that source/pair contribution. The target resolves around the current step base if one is active, otherwise around the retained base.
- Stopping transport clears step overlays as specified by Session 070; it must not clear valid installed LFO routes.
- Scene/load graph rebuilds must continue to clear stale runtime contributions and reinstall saved LFO routes through the existing lifecycle.
- Neither runtime layer may mark the Scene dirty or cause continuous AutoSave traffic.

## Implementation design

### 1. Change the existing LFO contribution representation

File: `Core/Bank/Scene/Preset/presetMorphEngine.c`

The existing table has one two-byte entry for every target voice, source LFO, and target pair:

```text
6 target voices * 6 LFO sources * 2 pairs * 2 bytes = 144 bytes
```

Reinterpret the existing two bytes from `active + absolute amount` to `direction + normalized depth`. Declare the public direction type in `presetMorphEngine.h` (because the setter uses it), and use it in the private contribution record in `presetMorphEngine.c`:

```c
typedef enum {
    PRESET_MORPH_LFO_DIRECTION_NONE = 0,
    PRESET_MORPH_LFO_DIRECTION_MAIN,
    PRESET_MORPH_LFO_DIRECTION_MORPH
} PresetMorphLfoDirection;

typedef struct {
    uint8_t direction;
    uint8_t depth;
} PresetMorphLfoContribution;
```

Naming may be adjusted to local conventions, but the representation must remain two bytes. `direction == NONE` is inactive; an active contribution with `depth == 0` is harmless and may either remain installed or be normalized to inactive as long as clearing/restoration stays deterministic.

Do not add a parallel table.

### 2. Give the morph engine one authoritative effective-base helper

File: `Core/Bank/Scene/Preset/presetMorphEngine.c`

Add a private helper that returns the effective base for a zero-based voice index:

```c
static uint8_t presetMorph_effectiveVoiceBase(uint8_t voice_index)
{
    if (morph_step_override[voice_index].active) {
        return morph_step_override[voice_index].amount;
    }

    return scene->settings.voice_morph_amount[voice_index];
}
```

Use the actual current Scene accessor/style rather than introducing a new global dependency. The important invariant is that every morph-engine path uses the same helper, including:

- pass snapshots;
- LFO amount resolution;
- voice prioritization/change detection;
- `presetMorph_applyVoiceNow()` and any equivalent synchronous trigger path;
- rebuilds queued after a step override is set or cleared.

This avoids a second class of bugs where queued work uses the new base but immediate trigger application still uses the retained base.

### 3. Resolve base-independent contributions inside the bounded worker

File: `Core/Bank/Scene/Preset/presetMorphEngine.c`

Rewrite `presetMorph_resolveLfoAmount()` so it:

1. obtains `B` from `presetMorph_effectiveVoiceBase()`;
2. visits the existing source/pair entries for the target voice;
3. converts each `direction + depth` to a signed delta using `B`;
4. sums in at least `int32_t` precision;
5. clamps the final result to `[0, 255]`.

Use rounded fixed-point division consistently. One suitable helper is conceptually:

```c
static int32_t presetMorph_scaleDelta(uint16_t distance, uint8_t depth)
{
    return ((int32_t)distance * depth + 127) / 255;
}
```

Then:

```c
if (direction == PRESET_MORPH_LFO_DIRECTION_MORPH) {
    delta = presetMorph_scaleDelta(255u - base, depth);
} else if (direction == PRESET_MORPH_LFO_DIRECTION_MAIN) {
    delta = -presetMorph_scaleDelta(base, depth);
}
```

Retain the current worker discipline: resolving an active LFO layer counts as bounded morph work, and parameter application remains amortized rather than moving whole-voice interpolation into the high-frequency oscillator path.

### 4. Change the morph-engine LFO setter contract

Files:

- `Core/Bank/Scene/Preset/presetMorphEngine.h`
- `Core/Bank/Scene/Preset/presetMorphEngine.c`

Change `presetMorph_setVoiceLfoModulation()` from receiving an absolute shaped morph amount to receiving the base-independent representation, for example:

```c
void presetMorph_setVoiceLfoModulation(
    uint8_t target_voice,
    uint8_t source_slot,
    uint8_t target_pair,
    PresetMorphLfoDirection direction,
    uint8_t depth);
```

Document index domains explicitly:

- whether `target_voice` is one-based or zero-based at the public boundary;
- source slot range `0..5`;
- target pair range `0..1`;
- depth range `0..255`.

The implementation must bounds-check before indexing, store the contribution, and queue/prioritize the target voice just as it does today. `presetMorph_clearLfoSource()` remains the lifecycle clearing API, but clearing an entry now stores `NONE/0` and queues restoration against the current effective base.

### 5. Encode polarity without reading the morph base

File: `Core/DSP/Instruments/InstrumentManager.c`

In `instrumentManager_updateLfoAdapters()`, change only the `VOICE_MORPH` adapter case. It must no longer:

- read `scene->settings.voice_morph_amount[target_voice]`; or
- call `modNode_shapeRangeU16()` to produce an absolute voice-morph amount.

Instead, add a small private helper that clamps the raw source and amount, computes a signed normalized modulation value independent of the morph base, and encodes it as direction plus 8-bit depth.

Conceptually, with normalized source `s` and amount `a`:

```text
positive: signed_depth =  a * s
negative: signed_depth = -a * (1 - s)
bipolar:  signed_depth =  a * (2*s - 1)
```

Then:

```text
signed_depth > 0 -> MORPH, round(abs(signed_depth) * 255)
signed_depth < 0 -> MAIN,  round(abs(signed_depth) * 255)
signed_depth = 0 -> NONE,  0
```

Prefer the same numeric conventions and polarity enum already used by `modNode_shapeParameterU16()` in `Core/DSPAudio/modulationNode.c:600-678`, so voice morph agrees with other targets at LFO extrema. Avoid float-to-integer edge drift beyond the existing path's behavior.

Decimation and track-7 adapters continue using their current `modNode_shapeRangeU16()` processing; this change is specific to voice morph.

### 6. Integrate the Session 070 step base in the same patch

Files and APIs from `S070_PHASE4_Q1_SCENE_AUTOMATION_IMPLEMENTATION.md` remain applicable, including:

- the per-voice transient `morph_step_override` state;
- `presetMorph_setStepAutomationOverride()`;
- `presetMorph_clearAllStepAutomationOverrides()`;
- sequencer dispatch and stop/restart cleanup;
- step-automation dirty-bit suppression.

Apply these clarifications:

- Setting a step override queues a rebuild using the new base while retaining all installed LFO contributions.
- Clearing step overrides queues affected voices; the result is retained base **plus any continuing LFO layer**, not unconditionally the retained base alone.
- The LFO contribution setter never captures the retained or step base.
- The effective-base helper is the only place that chooses between step and retained ownership.

All other Session 070 changes remain outside Session 071's scope.

### 7. Preserve existing UI, routing, and persistence

No functional changes are expected in:

- `Core/Bank/Scene/SceneModTargets.c`;
- LFO target picker/menu code;
- LFO destination Scene serialization;
- Scene file schema or version;
- source/pair route installation;
- ordinary retained voice-morph menu edits.

As a verification gate, confirm that both target pairs can still select target voice `scn` and parameters `1vm` through `6vm`, and that saved routes still rebuild after load. Do not add new target IDs or file fields.

## Memory and storage impact

Session 071 itself requires **0 bytes of additional persistent/static RAM**:

- existing LFO contribution storage: 144 bytes in SRAM1;
- representation before: two bytes per entry;
- representation after: two bytes per entry;
- lifetime: boot to power-off;
- owner: `presetMorphEngine.c`;
- no DTCM allocation;
- no Scene/file-format growth;
- no sample-flash or SD-layout change.

The prerequisite Session 070 plan proposes separate new SRAM1 state. Before implementing that source change, obtain the RAM approval required by `MEMORY.md` for the exact final compiler-visible allocation. The current plan-level estimate is:

| Owner | State | Estimated bytes | Lifetime |
|---|---|---:|---|
| preset morph engine | 6 voice step overrides, `active + amount` | 12 | boot to power-off |
| sequencer/Scene automation | track-7/slot-6 transient override | 2 | boot to power-off |
| sequencer/AutoSave bridge | Scene-automation dirty bitmap | 4 | boot to power-off |
| **Session 070 estimated total** |  | **18** |  |

Verify padding and linker placement from the implemented declarations/map before recording the final number. If Session 070 RAM has already been separately approved by implementation time, record that approval rather than requesting it again. Session 071 must not use the integration as justification for another table.

## Implementation sequence

1. Capture a clean baseline build and the current image/size output.
2. Implement the Session 070 transient voice-morph base state and the Session 071 base-independent LFO representation together.
3. Update the morph-engine public contract and the InstrumentManager voice-morph adapter.
4. Audit all morph application paths for use of the single effective-base helper.
5. Run static/math validation and a clean firmware build.
6. Run the hardware matrix below, starting with the retained-base regression before combining step automation.
7. Update the authoritative specifications and memory record only after observed behavior is accepted.

## Verification plan

### A. Static and math checks

Confirm there is still exactly one route from the LFO adapter to the morph engine and exactly one contribution table. Search for direct reads of `voice_morph_amount` in LFO voice-morph handling; only the morph engine's effective-base path should choose the live base.

Exercise at least these deterministic cases, either with a narrow host-side test/helper or carefully instrumented firmware assertions/log output that does not add release RAM:

| Base B | Direction | Depth D | Expected E |
|---:|---|---:|---:|
| 0 | morph | 0 | 0 |
| 0 | morph | 255 | 255 |
| 128 | morph | 0 | 128 |
| 128 | morph | 128 | about 192 |
| 128 | morph | 255 | 255 |
| 255 | morph | any | 255 |
| 128 | main | 255 | 0 |
| 255 | main | 128 | about 127/128 |

Also test:

- two positive contributions summing to the upper clamp;
- positive and negative contributions cancelling near the base;
- multiple source/pair clear operations leaving other contributions intact;
- `retained base = 0`, `step base = 128`, positive half-depth producing about `192`, not `128`;
- immediate trigger application and queued worker application producing the same amount;
- changing/clearing a step base without receiving a fresh LFO sample still recomputing correctly from the stored direction/depth.

### B. Build and static quality gates

Because a public header/API changes, use a clean build rather than relying on partial dependency tracking:

```text
make clean
make
make img
arm-none-eabi-size <the normal Helicase ELF target>
git diff --check
```

Compare image/section sizes with the baseline and inspect the map/declarations for the RAM accounting above. There must be no unexplained format/version change and no new static allocation attributable to Session 071.

### C. Retained-base hardware regression

Use one voice whose main and morph endpoint presets differ conspicuously in pitch, decay, or another audible parameter.

1. Select an LFO target voice of `scn` and target parameter `1vm` (then repeat with another voice).
2. Use a slow triangle LFO, positive polarity, and obvious amount.
3. Test retained bases `0`, `64`, `128`, `192`, and `255`.
4. Verify travel begins at the retained base and approaches the full-morph endpoint; base `255` remains stationary.
5. Verify the PERF/menu morph value does not move and AutoSave stays quiet during modulation.

### D. Step-base composition

1. Program alternating voice-morph step values such as `0`, `128`, and `224` on the same voice targeted by a slow LFO.
2. Verify each step changes the LFO's live starting point and the positive LFO moves from that point toward full morph.
3. Include the diagnostic case `retained = 0`, `step = 128`, half LFO depth; it must sound/measure near `192`.
4. Stop transport. Verify the step overlay clears and the still-installed LFO immediately continues around retained base `0`.
5. Restart and verify the first eligible step installs its base without stale prior-step state.
6. With no active step override, edit the retained morph value while the LFO is running and verify the new base takes effect without reinstalling the route.

### E. Polarity and endpoint checks

- Positive: base toward full-morph endpoint.
- Negative: base toward main endpoint.
- Bipolar: travel on both sides of the base, bounded by main/full endpoints.
- Amount zero: base only.
- Bases `0` and `255`: correct one-sided headroom and no wrap.
- LFO source at its exact extrema: endpoint calculations agree with the ordinary modulation convention.

### F. Pair, source, and target isolation

- Exercise both target pairs on one LFO source.
- Route two different sources to the same voice-morph target and verify signed-delta composition.
- Route sources to different voice-morph targets and verify no cross-voice leakage.
- Clear/retarget one pair and verify the other contribution remains.
- Turn the last route off and verify restoration to current step base or retained base, as applicable.

### G. Lifecycle and persistence regression

Test route removal, Scene change, Kit load, Instrument load, stop/restart, and reboot. There must be no stale contribution after graph teardown. Saved `scn`/`1vm..6vm` destinations and both target pairs must reload through the existing format without a migration or new field.

### H. Bounded-work/audio stress

Route all six LFO sources, using both pairs where practical, to voice-morph targets. Exercise high LFO rates while steps change morph bases. Observe the normal CPU diagnostic and listen for breakup, missed triggers, or worker starvation. Whole-voice recalculation must remain out of the high-frequency LFO callback; only the bounded morph worker may fan a morph amount out to voice parameters.

### I. AutoSave/retained-state checks

- LFO activity alone causes no Scene dirty churn.
- Step playback alone follows Session 070's dirty-bit suppression.
- Combined LFO plus step playback causes no dirty churn.
- A deliberate retained menu edit still dirties/saves normally.
- Runtime cleanup never writes the last applied effective amount into retained Scene storage.

## Acceptance criteria

The implementation is accepted when:

- positive LFO voice-morph modulation travels from the current effective menu/step base to the full-morph endpoint;
- negative and bipolar behavior remain consistent with current Helicase polarity semantics;
- step changes affect the LFO layer immediately without requiring a new LFO route or relying on a newly arriving sample;
- multiple source/pair contributions still combine as signed deltas and clamp safely;
- clearing either runtime layer reveals the other/retained layer correctly;
- no runtime LFO or step update mutates retained Scene voice-morph state or continuously dirties AutoSave;
- routing, both pairs, Scene load/rebuild, target persistence, and existing menu behavior regress cleanly;
- morph work remains bounded and the stress test produces no audible breakup;
- Session 071 adds no static RAM and no file-format fields;
- clean build, image generation, size inspection, and `git diff --check` pass.

## Documentation updates after implementation

After hardware acceptance, update:

- `MEMORY.md`: record that the LFO contribution is base-independent and composes over the active step/retained base; record approved Session 070 RAM using measured allocation facts.
- `knowledge_files/specification_reference/FILESYSTEM_SPEC.md`: document the effective-base rule and positive/negative/bipolar endpoint behavior.
- `knowledge_files/specification_reference/MODULE_INTERCHANGE_SPEC.md`: update the morph-engine LFO setter signature and ownership contract; audit the current entry because it may still describe the older absolute-amount API.
- the Session 071 handoff/test log: record exact build, size delta, routes, base values, polarity cases, stop/clear behavior, persistence results, and stress observations.
- `SRAM_MANIFEST.md`, if present/authoritative for the implemented Session 070 state: add only the measured Session 070 allocations. Session 071's contribution-table reinterpretation is zero-growth.

## Preserved LXR implementation reference

This section deliberately captures the old implementation in enough detail that the `LXR/` subdirectory can be deleted after this plan is written.

### Source identity

The inspected LXR checkout was at commit:

```text
2b7b7ca9c0bce5d23a14d9d67759f15bdcd2afb9
2b7b7ca fw build and let's call that v1.02
```

Line numbers below refer to that snapshot.

### Target exposure in the front-panel firmware

`LXR/front/LxrAvr/Menu/Cc2Text.c:19-70` defines the modulation target text tables. Each voice target list includes a morph entry. The first voice contains:

```c
{ TEXT_MORPH_VOICE, PAR_MORPH_DRUM1 },
```

and the corresponding voice lists map to `PAR_MORPH_DRUM2` through `PAR_MORPH_HIHAT`. This is the older UI/routing precedent for addressing morph as a modulation destination.

### Morph parameter IDs and storage

`LXR/mainboard/LxrStm32/src/Preset/ParameterArray.h:41-47` defines the dedicated morph parameter type:

```c
#define TYPE_UINT8_VMORPH 6
```

`LXR/mainboard/LxrStm32/src/Preset/ParameterArray.h:339-346` defines the six target IDs:

```c
PAR_MORPH_DRUM1
PAR_MORPH_DRUM2
PAR_MORPH_DRUM3
PAR_MORPH_SNARE
PAR_MORPH_CYM
PAR_MORPH_HIHAT
```

`LXR/mainboard/LxrStm32/src/Preset/ParameterArray.c:670-681` maps those IDs to `preset_vMorphAmount[1]` through `[6]`, all with `TYPE_UINT8_VMORPH`.

### Why LFO morph bypassed the generic high-frequency parameter path

`LXR/mainboard/LxrStm32/src/DSPAudio/modulationNode.c:69-79` identifies LFO modulation nodes and notes that LFO-to-voice-morph is serviced by the morph drain from the node's `lastVal`; recalculating a whole voice in the generic LFO service is too expensive.

`LXR/mainboard/LxrStm32/src/DSPAudio/modulationNode.c:248-307` restores an old morph target when a route changes and recognizes `TYPE_UINT8_VMORPH` when installing the new target. `modulationNode.c:311-324` stores the latest modulation value in `lastVal`. `modulationNode.c:438-465`, in `modNode_vMorph()`, returns immediately for LFO nodes; non-LFO modulation may call `preset_modulateVoiceMorphAmount()` directly. The architectural lesson is essential: store the latest LFO state cheaply and let a bounded/asynchronous morph engine perform the expensive parameter fan-out.

Helicase already follows this lesson through `presetMorph_setVoiceLfoModulation()` plus the bounded worker. Session 071 must preserve it.

### Old KitState split between base, endpoint, and live values

`LXR/mainboard/LxrStm32/src/Preset/KitState.h:47-84` defines:

- `PresetAutomationTargets`, including `lfoDestination[6]` and validity state;
- `kitEndpointParams` for the main endpoint;
- `morphEndpointParams` for the full-morph endpoint;
- `interpolatedParams` for the ordinary/current morph result;
- `voiceMorphBaseAmount[6]`;
- `voiceMorphAmount[6]`.

That separation is why the LFO could operate as a second layer rather than replacing the menu/automation-owned value.

### Destination ingestion and image coherence

`LXR/mainboard/LxrStm32/src/Preset/ParameterIngress.c:325-408`, especially `preset_storeLfoDestinationIngress()`, stores the paired target voice/selector and resolved destination. It also keeps the endpoint/interpolated parameter images coherent when destination-related data enters the preset system. Helicase does not need to copy this storage model, because its target/persistence plumbing already exists, but route/load testing must preserve the same coherence guarantee.

### LFO assignment and phased morph work

`LXR/mainboard/LxrStm32/src/Preset/MorphEngine.c:305-378` contains `preset_lfoMorphAssignmentForSource()`, checks whether a voice has an LFO morph overlay, and schedules morph phases. Phase zero performs the normal voice morph; later phases apply active source-LFO overlays.

`LXR/mainboard/LxrStm32/src/Preset/MorphEngine.c:628-720` contains the core precedent. Its essential behavior can be preserved as the following excerpt/pseudocode:

```c
// Later LFO phase:
travel = lfoNode->amount * lfoNode->lastVal;
travel = clamp(travel, 0.0f, 1.0f);
modulationAmount = round(travel * 255.0f);

liveValue = kit->interpolatedParams[param];
if (modulationAmount) {
    liveValue = preset_interpolateMorphValue(
        liveValue,
        kit->morphEndpointParams[param],
        modulationAmount);
}
apply(liveValue);
```

In the same region, phase zero first computes the ordinary baseline:

```c
interpolated = lerp(
    kitEndpointParams[param],
    morphEndpointParams[param],
    voiceMorphAmount);
kit->interpolatedParams[param] = interpolated;
```

If a voice has an LFO morph overlay, phase zero retains that interpolated baseline and the later LFO phase applies the second interpolation. Thus, in 8-bit morph-amount terms, the old positive-only behavior is algebraically:

```text
E = B + round((255 - B) * D / 255)
```

where `B` is the current ordinary/menu/automation morph amount and `D` is the current LFO modulation depth. This is the controlling semantic reference for Session 071.

`LXR/mainboard/LxrStm32/src/Preset/MorphEngine.c:220-234` contains the interpolation helper used by those two stages.

### Old base/live automation precedent

`LXR/mainboard/LxrStm32/src/Preset/MorphEngine.c:435-495` separates a base morph amount from the live modulated amount and restores the base when the transient modulation ends. The data layout differs from Helicase, but the ownership rule is the same: transient automation changes the live layer and cleanup reveals the retained/base layer.

### LXR lessons carried forward

The LXR implementation establishes four requirements that survive deletion of the old tree:

1. Voice morph is a routable modulation destination.
2. Ordinary/menu/automation morph is evaluated first; LFO is a second interpolation toward the morph endpoint.
3. The latest LFO state is captured cheaply; whole-voice morph recalculation is drained asynchronously/bounded to protect audio.
4. Removing a transient route restores the correct underlying base rather than persisting the last modulated value.

Helicase already implements items 1, 3, and most lifecycle parts of 4. Session 071 makes item 2 correct when the underlying base can be supplied by Session 070 step automation, while preserving Helicase's later multi-source and polarity extensions.
