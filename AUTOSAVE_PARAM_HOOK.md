# Autosave parameter-change hooks

## Status

General architecture, the implemented Phase 1 single-parameter work, the
implemented Phase 2 successful-load work, and its narrow active-owner/Bank
Load-Save correction are documented below. Corrected Phase 2 is complete in
source and awaits the targeted hardware test matrix. It did not change any
SD-card fixture, Pattern/Effect format, or copy/paste path.

This plan is based on the current mutation and load paths in:

- `Core/Bank/BankData.c` and `.h`
- `Core/Bank/Scene/SceneData.c` and `.h`
- `Core/Bank/Scene/Autosave.c` and `.h`
- `Core/Bank/Scene/Preset/presetManager.c` and `.h`
- `Core/DSP/Instruments/InstrumentManager.c`
- `Core/Menu/menu.c`
- `Core/MIDI/MidiParser.c`
- `Core/Hardware/SD/filesystem.c`
- `Core/Hardware/SD/storageTypes.c`
- `config.h`

## Target behavior

Whenever a retained SRAM byte represented in the autosave payload actually
changes, set that byte's bit in the one canonical 3,856-byte mutation mask
owned by `Autosave.c`.

Repeated changes to the same byte only set the same bit again. No event list or
second mutation mask is needed. The existing writer later gets the final value,
stores it, and clears/carries the bit through the existing ping-pong commit.

Dirty marking must only touch SRAM. It must never directly start an SD
operation, wait, allocate, or print.

## Current code shape

The current code already has useful ownership boundaries:

- Bank metadata changes converge in `BankData.c`.
- Scene scalar and generated Kit-setting changes converge in `SceneData.c`.
- Instrument normal/Morph endpoint changes converge in `presetManager.c`.
- Menu descriptor edits already call Preset setters.
- Menu MIDI channel/note edits already call SceneData setters.
- Menu Scene selection and VOICE edit-mask changes already call BankData.
- MIDI Morph changes already call Preset.
- validated Scene, Kit, and Instrument loads bypass scalar setters by assigning
  complete staged structures; these need separate post-commit region hooks.

The correct approach is therefore to hook the retained owners and successful
load commits. It is not necessary to scatter raw mask operations throughout
Menu or MIDI code.

## Central dirty-mark interface

Add typed dirty-mark functions to `Autosave.c`/`.h`. All payload-offset
arithmetic must remain there so other modules do not learn the binary layout.

The interface needs to mark:

- one Bank field;
- one Scene parameter index;
- one Effect parameter index (reserved/no live owner yet);
- one Kit parameter index;
- one Instrument normal descriptor index;
- one Instrument Morph descriptor index;
- one complete implemented Scene, Kit, or Instrument data region after a
  successful load.

The typed functions accept coordinates such as Scene, instrument slot,
descriptor index, and endpoint image. Invalid coordinates do nothing. Setting
an already-set bit is an idempotent OR.

The wire mapping remains:

```text
Bank payload base                                      0
  restore slot                                         0..1
  Bank name                                            2..9
  Scene-present mask                                  10..11
  active Scene                                        12
  VOICE edit mask                                     13..14

Scene S base                            128 + (S * 1920)
  Scene parameter P                  scene base + 8 + P
  Kit base                          scene base + 640
  Kit parameter K                     kit base + 8 + K
  Instrument V base                 kit base + 128 + V * 192
    type token                                      + 0..2
    name                                            + 3..10
    normal descriptor D                            + 11 + D
    Morph descriptor D                             + 83 + D
```

The forty current Scene parameter indices are:

```text
0       overall Morph amount
1..6    per-voice Morph amounts
7       Scene-wide decimation
8..13   per-voice audio output
14..19  per-voice FX-send amount
20..25  per-voice fader setting
26..32  per-track MIDI channel
33..39  per-track MIDI note
```

Kit indices 0/1 are the normal/Morph generated slot-6/track-7 decay values.
Instrument descriptor cells stay ordered by the active type's descriptor enum.

The Effect section reserves byte 0 for type, bytes 1..8 for name, and bytes
9..511 for future Effect parameters/padding. Phase 1 must name those boundaries
and provide a zero-live-parameter hook/getter stub even though no Effect owner
exists yet.

## Preserved copy/paste scopes

The autosave dirty API must expose obvious region-marker affiliates for these
future non-Pattern copy/paste operations:

1. **Whole Instrument** — copies type plus its normal and Morph endpoint data.
2. **Instrument Normal** — copies only the normal endpoint and is legal only
   when source and destination Instrument types match.
3. **Instrument Morph** — copies only the Morph endpoint and is legal only when
   source and destination Instrument types match.
4. **Kit** — copies Kit settings and all six whole Instruments.
5. **Scene without Pattern** — copies Scene settings, Effect, and Kit. This is
   the initial Scene copy scope.
6. **Scene with Pattern** — reserved as the later extension of Scene copy; its
   non-Pattern dirty scope is the same, with Pattern persistence added by the
   future Pattern implementation.
7. **Effect** — reserved until an Effect owner and live parameter enum exist.

Phase 1 does not implement copy/paste behavior. It adds named region-marker
functions and adjacent comments for these scopes so future copy code has one
obvious post-copy call. Normal/Morph copy code must validate matching types
before changing retained bytes; a dirty marker is not permission to perform an
incompatible copy.

The intended marker affiliates are:

```text
autosave_markWholeInstrumentDirty(scene, slot)
autosave_markInstrumentNormalDirty(scene, slot)
autosave_markInstrumentMorphDirty(scene, slot)
autosave_markKitDirty(scene)
autosave_markEffectDirty(scene)              // no-op until Effect exists
autosave_markSceneWithoutPatternDirty(scene)
autosave_markSceneWithPatternDirty(scene)    // non-Pattern alias/TODO stub
```

These functions only mark existing/gettable cells. They never copy data. The
whole-Instrument marker includes the three-byte type token; normal and Morph
markers do not. Kit calls the whole-Instrument scope for all six slots. Scene
without Pattern includes all Scene parameters, Effect, and Kit. Scene with
Pattern must remain an explicit separate stub so Pattern support cannot be
mistaken for already implemented behavior.

## Interrupt-safe bit ownership

One real concurrency issue exists in the current code. Velocity modulation can
reach retained Scene setters from the Instrument trigger path, which can run
through the sequencer timer interrupt. The autosave classifier runs in the
foreground.

The current writer sequence is conceptually:

```text
test bit -> get value -> clear bit
```

That sequence can lose a re-dirty event if an interrupt changes the parameter
and sets the bit between the get and clear.

Before enabling producers, replace the separate test/clear calls with one
atomic `take` operation:

```text
atomically test-and-clear bit -> get value
```

If a new mutation occurs after the take, it sets the bit again and survives for
the next writer pass. If it occurs before the take, the subsequent get sees the
new retained byte. The critical section only protects the one mask-byte
read/modify/write and uses PRIMASK save/restore; it must not surround parameter
gets or any filesystem work.

The canonical mask should be volatile because it is shared with
interrupt-reachable producers. It remains the only 3,856-byte mask.

## Ordinary mutation hooks

### `Core/Bank/BankData.c`

After normalization, compare the old and final stored values and mark changed
bytes in:

- `bank_setDisplayName()`;
- `bank_setRestoreBankSlot()`;
- `bank_setScenePresentMask()`;
- `bank_setActiveSceneSlot()`;
- `bank_selectActiveSceneForEditMask()`;
- `bank_setSceneMaskVoiceEdit()`;
- `bank_toggleSceneMaskVoiceEdit()`.

`bank_ensureActiveInVoiceEditMask()` can itself alter the stored VOICE edit
mask, so that internal normalization must also produce the mask-field dirty
notification when it changes the value.

`bank_setHasResidentBank()` is a lifecycle flag and has no payload bit.

### `Core/Bank/Scene/SceneData.c`

Compare the normalized value with the retained value and mark the matching
Scene/Kit parameter in:

- `scene_setTrackMidiChannel()` and `scene_setTrackMidiNote()`;
- `scene_setVoiceMorphAmount()`;
- `scene_setAllVoiceMorphAmounts()`;
- `scene_setVoiceAudioOut()`;
- `scene_setVoiceFxSendAmount()`;
- `scene_setVoiceFaderSetting()`;
- `scene_setSlot6Track7AmpEnvelopeDecay()`;
- `scene_setSlot6Track7MorphAmpEnvelopeDecay()`.

`scene_initAll()` remains unmarked because it establishes defaults before a
resident autosave Bank exists.

### `Core/Bank/Scene/Preset/presetManager.c`

Compare retained endpoint bytes and mark the corresponding descriptor cells
in:

- `preset_setInstrumentParameter()` for normal or Morph endpoints;
- `preset_setSupplementalParameter()` for the normal non-morphable cell;
- `preset_morphScene()` for the direct overall Morph field;
- `preset_setVoiceDecimationAll()` for Scene-wide decimation;
- `preset_storeSupplementalCell()` when normalization changes a persisted
  normal selector.

The existing SceneData calls inside Preset cover per-voice Morph, mix settings,
and generated Kit values. Public setter return behavior need not change; dirty
marking merely compares old and final values.

`morph_interpolation[]` is derived runtime state and must not be marked.

## Successful load/replace hooks

Loaded data is a retained mutation even if a loaded byte matches its previous
value. Mark only after validation and resident commit succeed.

### `Core/Hardware/SD/filesystem.c`

- After `filesystem_commitSceneStage()` assigns settings and Kit to each
  selected destination, mark that Scene's implemented non-Pattern data.
- After normal Kit Load assigns `target_scene->kit = op_staged_kit`, mark that
  Scene's Kit data.
- Do not mark parsing stages, temporary workspaces, failed loads, Pattern
  fan-out, or save-only serialization.

### `Core/Bank/Scene/Preset/presetManager.c`

- After `preset_startInstrumentApplyImage()` replaces a selected resident
  Instrument, mark its type and implemented normal/Morph parameter data.
- When InstrumentMrp or KitMrp copies same-type normal values into resident
  Morph endpoints, mark the copied morphable descriptor cells.
- When KitMrp copies the generated slot-6/track-7 Morph decay value, mark Kit
  parameter index 1.

This should be implemented as a second test pass after individual setter hooks
are verified.

## Bank-session boundary

The canonical mask belongs to one resident Bank identity. A successful runtime
change to a different Bank slot/name must not carry pending bits from the old
Bank into the new one.

The expanded implementation plan must define one explicit Autosave-owned Bank
session transition called after a new Bank has successfully committed. It must
discard old-identity pending bits and mark the new Bank/loaded data required to
build the new record. Failed or merely staged Bank operations must leave the
old session untouched.

Initial boot is different: it must retain the existing recovery behavior and
merge the matching on-file mask, rather than forcing a full re-dirty on every
boot. This lifecycle decision belongs at successful filesystem Bank completion,
not inside ordinary BankData setters.

## Scheduling after hooks exist

Setters must not call filesystem code. `filesystem_tick()` should observe the
canonical SRAM mask while idle:

- after the one boot recovery check, an empty mask starts no filesystem
  operation;
- the first observed dirty state arms the existing five-second delay;
- later changes coalesce into the same mask;
- a successful partial drain with remaining bits retains the 250 ms
  continuation;
- Load/Save pages continue to pause new autosave starts;
- an active writer is never preempted.

This removes recurring SD validation cycles after the mask is fully clean while
keeping the existing debounce and continuation values in `config.h`.

## Paths that should not receive direct hooks

- `menu.c`: retained edits already route through BankData, SceneData, or
  Preset. Menu must not calculate autosave offsets.
- `MidiParser.c`: Morph reaches Preset. Historical tagged Instrument CC writes
  are explicitly runtime-only and are not retained Bank changes.
- `InstrumentManager.c`: velocity Scene targets inherit the retained Preset
  hooks. LFO adapters are runtime layers and must remain unmarked.
- `presetMorphEngine.c`: interpolation is derived.
- `PatternData`: Pattern is outside this autosave file.
- `storageTypes.c`: it writes staging objects; only successful resident commit
  is marked.
- Effect cells: still reserved with no live owner.

## Name-field limitation

Bank name is gettable from BankData and can be hooked now. Scene, Kit, and
Instrument names are different: their authority is root `/.hcnames`, and
`autosave_getLivePayloadByte()` currently reports those payload cells as
nonexistent.

This parameter-hook work must not pretend those name bytes are gettable.
Completing name updates requires a separate identity-source/patch plan at
successful HCNAMES publication. Instrument type is gettable and is covered by
whole-Instrument/Kit/Scene load hooks.

## Phase 1 detailed implementation plan: single parameters

Phase 1 implements only ordinary single-parameter dirty production, the
atomic mask protocol it requires, clean-mask scheduling, and unused region
marker stubs for the preserved copy/paste scopes. Whole-object load commits do
not begin using the region markers until Phase 2.

The planned Phase 1 source-file set is exactly:

- `Core/Bank/Scene/Autosave.c` and `.h`;
- `Core/Bank/BankData.c` and `.h`;
- `Core/Bank/Scene/SceneData.c` and `.h`;
- `Core/Bank/Scene/Preset/presetManager.c` and `.h`;
- `Core/Hardware/SD/filesystem.c` and `.h`;
- this planning document for implementation notes.

No Phase 1 change is planned for `main.c`, `config.h`, Menu, MIDI,
InstrumentManager, the four Instrument parameter-definition files,
`storageTypes`, Pattern, DSP runtime modules, or files under `SD_CARD/`.
The five-second debounce, 250 ms continuation, 1,536-get cap, and 256-bit
classification cap remain unchanged.

Every source change below must receive an adjacent comment block in both `.c`
and `.h` where an interface or ownership contract is exposed. Those comments
must state what the change does, why it exists, its inputs/outputs, and its
affiliates. Existing comments that become inaccurate must be corrected rather
than supplemented with a contradictory second description.

### Phase 1 change 1 — `Core/Bank/Scene/Autosave.h`: name every parameter
domain

Add format-owned identifiers for every presently serialized logical field.

#### Bank field identifiers

Define a Bank-field enum or equivalent typed constants for:

```text
restore slot                 payload 0..1
display name                 payload 2..9
Scene-present mask           payload 10..11
active Scene                 payload 12
VOICE edit mask              payload 13..14
```

Each identifier resolves to its existing offset and byte width inside
`Autosave.c`. Callers select a logical field; they do not pass raw offsets or
widths.

What this does: gives BankData one stable, format-owned marking vocabulary.

Why it must exist: slot and masks are multi-byte fields, while name is eight
bytes. Repeating offset/width arithmetic in BankData would make later format
changes unsafe.

Inputs: one Bank field identifier.

Outputs: the corresponding payload byte range can be marked atomically.

Affiliates: existing `AUTOSAVE_BANK_*_OFFSET` constants,
`autosave_getLivePayloadByte()`, and BankData setters.

#### Scene parameter identifiers

Replace magic number boundaries in `autosave_getSceneParameter()` with named
group bases and one count sentinel:

```text
AUTOSAVE_SCENE_PARAM_MORPH_AMOUNT                    0
AUTOSAVE_SCENE_PARAM_VOICE_MORPH_BASE               1  (6 cells)
AUTOSAVE_SCENE_PARAM_DECIMATION_ALL                  7
AUTOSAVE_SCENE_PARAM_AUDIO_OUT_BASE                  8  (6 cells)
AUTOSAVE_SCENE_PARAM_FX_SEND_BASE                   14  (6 cells)
AUTOSAVE_SCENE_PARAM_FADER_BASE                     20  (6 cells)
AUTOSAVE_SCENE_PARAM_MIDI_CHANNEL_BASE              26  (7 cells)
AUTOSAVE_SCENE_PARAM_MIDI_NOTE_BASE                 33  (7 cells)
AUTOSAVE_SCENE_PARAM_COUNT                          40
```

Add a static assertion that `AUTOSAVE_SCENE_PARAM_COUNT` equals
`AUTOSAVE_SCENE_PARAMETER_LIVE_BYTES` and does not exceed the 120-byte Scene
parameter allocation.

What this does: makes getter ordering and dirty-marker ordering use the same
symbols.

Why it must exist: a future Scene field must extend one count contract instead
of independently changing a struct, getter magic number, and dirty offset.

Inputs: Scene index plus a named parameter index.

Outputs: one fixed payload cell or an invalid-coordinate no-op.

Affiliates: `scene_settings_t`, SceneData setters, and
`autosave_getSceneParameter()`.

#### Kit parameter identifiers

Define:

```text
AUTOSAVE_KIT_PARAM_SLOT6_TRACK7_DECAY          0
AUTOSAVE_KIT_PARAM_SLOT6_TRACK7_MORPH_DECAY    1
AUTOSAVE_KIT_PARAM_COUNT                       2
```

Assert that the count equals `AUTOSAVE_KIT_PARAMETER_LIVE_BYTES` and fits the
120-byte Kit parameter allocation.

What this does: replaces the current anonymous `0`/`1` getter policy with
names shared by SceneData and Autosave.

Why it must exist: future Kit-owned parameters need an obvious append point
and one count assertion.

Inputs/outputs: Scene plus named Kit parameter maps to one payload cell.

Affiliates: `kit_settings_t` and its two current setters/getters.

#### Effect parameter stub identifiers

Add explicit Effect geometry:

```text
AUTOSAVE_EFFECT_PARAMETERS_OFFSET              9
AUTOSAVE_EFFECT_PARAMETER_ALLOC_BYTES        503
AUTOSAVE_EFFECT_PARAM_COUNT                    0
```

The count is initially zero because there is no retained Effect owner. Assert
that type byte + eight name bytes + parameter allocation ends at the 512-byte
Effect boundary.

What this does: gives future Effect parameters a fixed append point and count
contract without allocating fake Effect SRAM.

Why it must exist: leaving the entire Effect interval under a generic
“nonexistent” branch would encourage a later Effect implementation to bypass
dirty hooks.

Inputs/outputs: no valid parameter exists yet; every Effect parameter request
is a bounded no-op until the count and owner are implemented.

Affiliates: Effect type/name offsets, the placeholder `effects.fx` parser, and
future Scene Effect ownership.

#### Instrument assertions

Retain and document the current generic relationship:

```text
INSTRUMENT_PARAM_COUNT = 64
autosave normal allocation = 72
autosave Morph allocation = 72
```

The existing static assertion that `INSTRUMENT_PARAM_COUNT <= 72` remains the
future capacity gate. No per-type offset enum is added to Autosave: descriptor
indices from each registry entry are already the canonical ordering.

### Phase 1 change 2 — `Core/Bank/Scene/Autosave.h`: expose mutation and region
interfaces

Add declarations for:

```text
autosave_setMutationTrackingEnabled(enabled)
autosave_markBankFieldDirty(field)
autosave_markSceneParameterDirty(scene, parameter)
autosave_markKitParameterDirty(scene, parameter)
autosave_markInstrumentNormalParameterDirty(scene, slot, descriptor)
autosave_markInstrumentMorphParameterDirty(scene, slot, descriptor)
autosave_markEffectParameterDirty(scene, parameter)
autosave_maskBitTake(payload_offset)
```

The mark functions return no value and safely ignore invalid coordinates,
disabled tracking, absent Scenes, unknown Instrument types, descriptors beyond
the active registry count, non-morphable Morph descriptors, and the currently
empty Effect domain.

Also declare the preserved copy/paste region-marker functions listed earlier:

```text
autosave_markWholeInstrumentDirty(scene, slot)
autosave_markInstrumentNormalDirty(scene, slot)
autosave_markInstrumentMorphDirty(scene, slot)
autosave_markKitDirty(scene)
autosave_markEffectDirty(scene)
autosave_markSceneWithoutPatternDirty(scene)
autosave_markSceneWithPatternDirty(scene)
```

What this does: establishes one public mutation API for scalar owners and one
named post-copy API for future compound owners.

Why it must exist: single-byte setters and later copy/load operations need
different scopes, but neither should know wire offsets.

Inputs: typed Bank/Scene/Kit/Effect coordinates or Scene/slot/descriptor
coordinates.

Outputs: only bits in Autosave's canonical mask change; no parameter value,
filesystem state, or display state changes.

Affiliates: BankData, SceneData, Preset, filesystem classification, and future
copy/paste commands.

Remove the public combination of `autosave_maskBitIsSet()` plus
`autosave_maskBitClear()` once the writer uses `autosave_maskBitTake()`. Keeping
the separable mutation operations would preserve the race Phase 1 is meant to
remove.

### Phase 1 change 3 — `Core/Bank/Scene/Autosave.c`: make the canonical mask
interrupt-safe

Change the sole mask declaration to volatile and add a volatile tracking gate:

```text
volatile canonical 3,856-byte dirty mask
volatile one-byte mutation-tracking-enabled flag
```

Static BSS initialization leaves both zero at processor reset. Tracking is
enabled only by the filesystem boot lifecycle described below. File-mask merge
and transaction rollback remain internal recovery operations and are not
blocked by the producer tracking flag.

Add file-local PRIMASK save/disable and restore helpers following the existing
project pattern in `sampleFlash.c` and `AudioCodecManager.c`.

Add one private atomic mask-byte OR helper. Use it for:

- single-parameter dirty setting;
- file-mask chunk OR merge;
- captured-offset rollback.

Each critical section covers one mask byte only. Do not disable interrupts
across a 512-byte merge, a getter, a loop over descriptors, a CRC operation, or
filesystem work.

Implement `autosave_maskBitTake()` as one bounded PRIMASK-protected test and
clear of the addressed bit.

What this does: prevents foreground and timer-interrupt read/modify/write
operations from overwriting one another.

Why it must exist: velocity Scene targets can call retained setters from the
instrument trigger path while phase 56 classifies in foreground.

Inputs: a payload-relative offset or mask-byte/bits for private recovery
helpers.

Outputs: atomic OR, or one returned prior bit followed by its clear.

Affiliates: phase 56, `instrumentManager_applyVelocitySceneTarget()`, mask
merge, and error restoration.

Because `autosave_dirty_mask` becomes volatile, replace the transform's current
`memcpy()` from the mask with a bounded byte loop that copies each volatile
mask byte into the stable 512-byte staging chunk. Once copied, CRC and SD write
operate only on the staging bytes. A concurrent later set remains in SRAM for
continuation and cannot alter a chunk whose CRC is already being calculated.

### Phase 1 change 4 — `Core/Bank/Scene/Autosave.c`: implement typed offset
mapping and single-parameter markers

Add private helpers that calculate payload-relative bases only after validating
coordinates:

```text
Scene base       = 128 + scene * 1920
Kit base         = Scene base + 640
Instrument base  = Kit base + 128 + slot * 192
```

Implement every public marker through those helpers and the private atomic bit
OR. Instrument markers must ask `instrumentManager_registryEntry()` for the
destination type and descriptor count. The Morph marker additionally requires
`INSTRUMENT_PARAM_FLAG_MORPHABLE`.

The Effect parameter marker uses the same Scene-base validation but currently
rejects every index because `AUTOSAVE_EFFECT_PARAM_COUNT` is zero. Its comment
must say exactly where a future live Effect parameter will map.

What this does: guarantees every owner selects the same byte later read by
`autosave_getLivePayloadByte()`.

Why it must exist: typed coordinates prevent BankData/SceneData/Preset from
duplicating format arithmetic.

Inputs: existing retained-state coordinates.

Outputs: one canonical bit, or no-op for invalid/unimplemented data.

Affiliates: format constants, Instrument registry, and live getter.

### Phase 1 change 5 — `Core/Bank/Scene/Autosave.c`: make getters and future
counts share one source of truth

Rewrite `autosave_getSceneParameter()` to use the named bases/counts instead of
literal `0`, `7`, `14`, `20`, `26`, `33`, and `40` boundaries. Its returned
ordering remains byte-for-byte unchanged.

Replace the Kit getter's literal indices with the two named Kit identifiers.

Add `autosave_getEffectParameter(scene, parameter, value)` as a file-local,
documented stub that currently returns zero. Restructure the Effect interval in
`autosave_getLivePayloadByte()` so future parameter bytes route through this
helper rather than the current broad “everything before Kit is nonexistent”
return. Type and name remain separately unavailable until an Effect owner and
identity source exist.

What this does: makes current gets and future marker indices evolve together.

Why it must exist: a dirty hook is only correct if the writer's get maps the
same index to the retained owner.

Inputs: Scene/Kit/Effect parameter index and current resident structures.

Outputs: unchanged current values; Effect returns nonexistent until
implemented.

Affiliates: the new enums/counts and `autosave_getLivePayloadByte()`.

### Phase 1 change 6 — `Core/Bank/Scene/Autosave.c`: implement the future
copy/paste marker stubs

Implement the named region functions even though Phase 1 does not call them:

- Whole Instrument marks its three type bytes, every existing normal
  descriptor, and each morphable Morph descriptor. It does not mark the
  Instrument name because that value is not currently gettable from resident
  SRAM.
- Instrument Normal loops the active registry's `descriptor_count` and marks
  every normal cell. Its comment requires future copy code to validate matching
  source/destination types before the copy.
- Instrument Morph loops only descriptors carrying the morphable flag, with
  the same matching-type requirement documented for future callers.
- Kit marks both current Kit parameters and all six Whole Instrument regions.
- Effect is a deliberate no-op that loops the zero live count. Its body and
  comment are the append point when Effect type/state exists.
- Scene without Pattern marks all 40 Scene parameters, calls Effect, and calls
  Kit. It does not mark Scene name or Pattern.
- Scene with Pattern calls Scene-without-Pattern and contains an explicit TODO
  stub for the later Pattern persistence boundary.

What this does: preserves every requested compound copy scope as executable
dirty-region structure without implementing copying.

Why it must exist: later copy/paste should finish with one named marker rather
than rebuild parameter loops or forget a newly added subgroup.

Inputs: destination Scene/slot after a future successful copy.

Outputs: all currently live cells in that scope become dirty; no data is
copied.

Affiliates: future copy/paste operations and Phase 2 load-region marking.

### Phase 1 change 7 — `Core/Bank/BankData.c` and `.h`: hook every Bank field

Include `Autosave.h` in `BankData.c`. Keep the public BankData API unchanged.

Refactor the private display-name copy helper to report whether any normalized
eight-character cell changed. `bank_setDisplayName()` stores the normalized
name first and then marks the Bank name field only when that result reports a
change.

For each scalar/mask setter, calculate the normalized final value, compare it
with retained storage, store it, and mark its Bank field only on change:

- restore slot;
- Scene-present mask;
- active Scene;
- VOICE edit mask.

Change `bank_ensureActiveInVoiceEditMask()` to return whether it normalized or
replaced the retained VOICE edit mask. Its callers mark the VOICE-mask field
when that hidden normalization changes it. Active-Scene setters independently
mark active Scene when its byte changes.

`bank_sceneMaskVoiceEdit()` currently calls the invariant helper from a getter.
That behavior remains, but a resulting serialized mask repair must now be
marked rather than silently changing retained state.

Do not hook `bank_init()` or `bank_setHasResidentBank()`.

What this does: covers every one of the 15 currently gettable Bank bytes.

Why it must exist: Menu, Preset, and filesystem already converge on BankData,
and the invariant helper can mutate state without an external setter.

Inputs: existing normalized Bank setter inputs.

Outputs: unchanged Bank behavior plus changed-field dirty bits when mutation
tracking is enabled.

Affiliates: Bank getter projection, Menu Scene switching, VOICE edit fan-out,
and filesystem Bank metadata completion.

Update the matching declarations/comments in `BankData.h` to state that these
are retained serialized-owner setters and ordinary post-boot changes notify
Autosave. The header must also state that initialization and resident-presence
lifecycle do not represent payload mutations.

### Phase 1 change 8 — `Core/Bank/Scene/SceneData.c` and `.h`: centralize every
Scene and Kit scalar write

Include `Autosave.h` in `SceneData.c`.

Add two file-local store helpers:

```text
store one Scene parameter byte if changed, then mark its named Scene index
store one Kit parameter byte if changed, then mark its named Kit index
```

Both helpers validate the Scene and storage pointer, compare old/final byte,
store first, then call the typed Autosave marker. They perform no runtime apply.

Route all current Scene scalar setters through the Scene helper:

- overall Morph amount through a new `scene_setMorphAmount()` owner setter;
- six per-voice Morph amounts;
- Scene-wide decimation through a new
  `scene_setVoiceDecimationAll()` owner setter;
- six audio outputs;
- six FX-send amounts;
- six fader settings;
- seven MIDI channels;
- seven MIDI notes.

`scene_setAllVoiceMorphAmounts()` calls the single-voice setter six times so it
cannot bypass dirty detection.

Route both current generated Kit setters through the Kit helper:

- normal slot-6/track-7 decay;
- Morph slot-6/track-7 decay.

All existing clamping/default behavior occurs before the helper is called.
Getters and `scene_initAll()` retain their current behavior. Initialization
continues assigning defaults directly while mutation tracking is disabled.

What this does: covers all 40 current Scene parameters and both current Kit
parameters from one storage boundary.

Why it must exist: direct assignments for overall Morph and decimation are the
two current holes; moving them behind SceneData setters gives future Scene
fields an obvious pattern.

Inputs: normalized Scene/slot/track values.

Outputs: retained state changes first, then exactly one typed dirty mark per
changed byte.

Affiliates: Preset runtime apply, Menu track settings, velocity Scene targets,
and Autosave getter ordering.

Add declarations and detailed ownership comments for the two new public
setters in `SceneData.h`. Add an adjacent future-owner rule to
`scene_settings_t` and `kit_settings_t`: every new field intended for autosave
must receive a named Autosave parameter index and write through the matching
store/setter boundary. This is the obvious future hook; direct assignments are
reserved for initialization or validated whole-object commit paths that use a
region marker.

Also add a comment-only Effect placeholder adjacent to `scene_t` explaining
that the future retained Effect owner belongs between Scene settings and Kit
semantically, must define its live parameter count/getter, and must write
through the Effect dirty marker. Do not allocate a dummy Effect struct in
Phase 1.

### Phase 1 change 9 — `Core/Bank/Scene/Preset/presetManager.c` and `.h`: hook
all descriptor endpoint writes

Include `Autosave.h` in `presetManager.c`.

Add one file-local endpoint-store helper that receives Scene, slot, descriptor
index, normal/Morph selection, and value. After Preset has validated the active
type and descriptor policy, the helper compares the selected retained byte,
stores it first, then calls the matching typed Instrument marker.

Use the helper from:

- `preset_setInstrumentParameter()` for all morphable normal and Morph image
  cells;
- `preset_setSupplementalParameter()` for every non-morphable normal cell.

Change `preset_storeSupplementalCell()` to receive Scene and slot coordinates
in addition to the Instrument pointer. Its normal image assignment uses the
same store helper. Its mirrored non-morphable Morph and interpolation cells
remain direct internal repairs because `autosave_getLivePayloadByte()` does not
serialize those cells. Update all three current call sites in LFO/velocity
normalization with the coordinates they already own.

Replace the direct `scene->settings.morph_amount = morph` in
`preset_morphScene()` with `scene_setMorphAmount()`. Replace the direct
`voice_decimation_all` assignment in `preset_setVoiceDecimationAll()` with the
new SceneData setter. Existing runtime mirror/apply behavior stays after the
retained setter.

Do not add dirty calls to runtime-only application functions,
`presetMorphEngine`, or `morph_interpolation[]` writes.

What this does: covers all present and future registry descriptors without
per-instrument dirty code.

Why it must exist: normal and Morph arrays are owned by Preset's validated
descriptor setters, not by BankData or scalar SceneData setters.

Inputs: active destination type, descriptor index/flags, endpoint selector,
and byte value.

Outputs: changed retained descriptor byte and its exact normal/Morph dirty bit;
unchanged values generate no bit.

Affiliates: Menu descriptor edits, supplemental normalization, Instrument
registry, Morph worker, and Autosave descriptor getter.

Update the public setter comments in `presetManager.h` to document the retained
mutation notification and future-descriptor rule. No new public Preset API is
required for Phase 1.

The existing load/copy assignments below remain Phase 2 and must be called out
as intentionally not hooked during the Phase 1 test:

- `preset_copyInstrumentNormalToMorphIfSameType()`;
- `preset_commitStagedKitNormalToMorph()`;
- `preset_startInstrumentApplyImage()`.

### Complete Instrument coverage audit

All current normal Instrument descriptors are covered by the combination of
`preset_setInstrumentParameter()` (morphable image rows) and
`preset_setSupplementalParameter()` (non-morphable selector rows). All current
morphable Morph descriptors are covered by the former with Morph selected.

Current registry counts and expected coverage are:

```text
Drum:    39 normal descriptors, 34 live Morph descriptors
Snare:   38 normal descriptors, 33 live Morph descriptors
Cymbal:  39 normal descriptors, 34 live Morph descriptors
HiHat:   39 normal descriptors, 34 live Morph descriptors
```

The five non-morphable rows in each type are `velo_mod_dest`,
`lfo_target_voice`, `lfo_target_param`, `lfo_target_voice_2`, and
`lfo_target_param_2`. They have one persisted normal cell and no live Morph
cell.

The generic descriptor hook covers these complete current key sets:

**Drum (39):** `osc1_wave`, `osc1_pitch_coarse`, `osc1_pitch_fine`,
`osc2_wave`, `osc2_pitch_coarse`, `osc2_mod_amount`, `osc2_mod_type`,
`filter_freq`, `filter_reso`, `filter_drive`, `filter_type`,
`amp_envelope_attack`, `amp_envelope_decay`, `amp_envelope_slope`,
`pitch_envelope_decay`, `pitch_envelope_amount`, `pitch_envelope_slope`,
`instrument_vol`, `instrument_pan`, `instrument_drive`,
`instrument_decimation`, `lfo_rate`, `lfo_amount`, `lfo_amount_2`, `lfo_wave`,
`lfo_retrigger_voice`, `lfo_polarity`, `lfo_sync`, `lfo_offset`,
`velo_vol_on_off`, `velo_mod_amount`, `velo_mod_dest`, `lfo_target_voice`,
`lfo_target_param`, `lfo_target_voice_2`, `lfo_target_param_2`,
`transient_wave`, `transient_vol`, `transient_freq`.

**Snare (38):** `osc1_wave`, `osc1_pitch_coarse`, `osc1_pitch_fine`,
`noise_freq`, `osc1_noise_mix`, `filter_freq`, `filter_reso`, `filter_drive`,
`filter_type`, `amp_envelope_attack`, `amp_envelope_decay`,
`amp_envelope_slope`, `amp_attack_repeat`, `pitch_envelope_decay`,
`pitch_envelope_amount`, `pitch_envelope_slope`, `instrument_vol`,
`instrument_pan`, `instrument_drive`, `instrument_decimation`, `lfo_rate`,
`lfo_amount`, `lfo_amount_2`, `lfo_wave`, `lfo_retrigger_voice`,
`lfo_polarity`, `lfo_sync`, `lfo_offset`, `velo_vol_on_off`,
`velo_mod_amount`, `velo_mod_dest`, `lfo_target_voice`, `lfo_target_param`,
`lfo_target_voice_2`, `lfo_target_param_2`, `transient_wave`, `transient_vol`,
`transient_freq`.

**Cymbal (39):** `osc1_wave`, `osc1_pitch_coarse`, `osc1_pitch_fine`,
`osc2_wave`, `osc2_pitch_coarse`, `osc2_mod_amount`, `osc3_wave`,
`osc3_pitch_coarse`, `osc3_mod_amount`, `filter_freq`, `filter_reso`,
`filter_drive`, `filter_type`, `amp_envelope_attack`, `amp_envelope_decay`,
`amp_envelope_slope`, `amp_attack_repeat`, `instrument_vol`, `instrument_pan`,
`instrument_drive`, `instrument_decimation`, `lfo_rate`, `lfo_amount`,
`lfo_amount_2`, `lfo_wave`, `lfo_retrigger_voice`, `lfo_polarity`, `lfo_sync`,
`lfo_offset`, `velo_vol_on_off`, `velo_mod_amount`, `velo_mod_dest`,
`lfo_target_voice`, `lfo_target_param`, `lfo_target_voice_2`,
`lfo_target_param_2`, `transient_wave`, `transient_vol`, `transient_freq`.

**HiHat (39):** `osc1_wave`, `osc1_pitch_coarse`, `osc1_pitch_fine`,
`osc2_wave`, `osc2_pitch_coarse`, `osc2_mod_amount`, `osc3_wave`,
`osc3_pitch_coarse`, `osc3_mod_amount`, `filter_freq`, `filter_reso`,
`filter_drive`, `filter_type`, `amp_envelope_attack`, `amp_envelope_decay`,
`amp_envelope_decay_choke`, `amp_envelope_slope`, `instrument_vol`,
`instrument_pan`, `instrument_drive`, `instrument_decimation`, `lfo_rate`,
`lfo_amount`, `lfo_amount_2`, `lfo_wave`, `lfo_retrigger_voice`,
`lfo_polarity`, `lfo_sync`, `lfo_offset`, `velo_vol_on_off`,
`velo_mod_amount`, `velo_mod_dest`, `lfo_target_voice`, `lfo_target_param`,
`lfo_target_voice_2`, `lfo_target_param_2`, `transient_wave`, `transient_vol`,
`transient_freq`.

No Phase 1 change is required in the four `*Parameters.c/.h` files or
`InstrumentManager.c/.h`. Their descriptor-index/count contracts already make
a future descriptor use the generic Preset setter, provided it remains within
`INSTRUMENT_PARAM_COUNT`. The code-adjacent comments in Preset/Autosave must
state this dependency so a future direct endpoint assignment is recognized as
a contract violation.

### Phase 1 change 10 — `Core/Hardware/SD/filesystem.c` and `.h`: enable
tracking only after boot setup and stop clean polling

Add one filesystem-owned `recovery_pending` flag beside the existing writer
`armed` and `boot_ready` fields.

At the start of `filesystem_ensureAutosaveFilesBlocking()`:

- disable retained mutation tracking;
- clear runtime writer authorization/arming;
- mark recovery as not yet scheduled.

After both files are ensured and the final filesystem status is acknowledged:

- enable retained mutation tracking;
- set `boot_ready`;
- set `recovery_pending` so one delayed runtime transaction validates the
  records and OR-merges the winner's carried mask;
- leave the normal five-second first-attempt interval unchanged.

On boot/autosave setup failure, no-resident-Bank state, or diagnostic facade
reset, tracking remains disabled and recovery is not pending.

Change `filesystem_autosaveWriterSchedule_tick()` so:

1. pending initial recovery may start even when the SRAM mask is empty;
2. after recovery has succeeded, an empty canonical mask disarms and returns
   before `filesystem_start()`;
3. the first newly observed dirty mask arms the existing five-second due time;
4. already-armed dirty work keeps its existing due time so mutations coalesce;
5. Load/Save page suspension and filesystem-ready checks remain unchanged.

Change `filesystem_autosaveWriterCompleted()` so successful initial recovery
clears `recovery_pending`. If the successful operation leaves dirty bits, use
the existing 250 ms continuation. If it is clean, leave the writer disarmed
instead of scheduling another five-second file validation. An error retains
recovery pending or dirty work and retries only at the ordinary five-second
interval.

What this does: separates the required one-time boot mask import from later
SRAM-driven dirty scheduling.

Why it must exist: owner setters must not manufacture boot dirtiness, and a
fully drained mask must not cause recurring SD operations.

Inputs: boot ensure result, resident-Bank state, canonical mask, current menu
page, and existing timebase.

Outputs: one boot recovery attempt, then filesystem work only for dirty SRAM.

Affiliates: boot ensure, writer scheduler/completion, config intervals, and
Load/Save suspension.

In phase 56 replace:

```text
test bit -> get live value -> clear bit
```

with:

```text
atomic take bit -> get live value
```

A successful get still appends one patch. A nonexistent get consumes no patch
and leaves the already-taken bit closed. Transaction failure still restores
captured patch offsets; bits re-added concurrently remain set because restore
is OR.

Update `filesystem.h` comments for the boot ensure/runtime writer boundary to
document tracking enablement, one-time recovery, and clean-mask no-I/O behavior.
No new public filesystem function is required.

### Phase 1 change 11 — comments/stubs for future Effect ownership

Phase 1 must leave the following explicit code-adjacent trail:

- `Autosave.h`: Effect type/name/parameter geometry, zero live count, single
  marker, and whole-Effect marker declaration;
- `Autosave.c`: zero-live getter, single marker no-op, and whole-Effect marker
  no-op;
- `SceneData.h`: future retained Effect owner comment and requirement that all
  Effect setters use the Effect marker;
- Scene-without-Pattern region marker: always calls the Effect marker, even
  while it is a no-op;
- Scene-with-Pattern marker: explicitly calls the non-Pattern marker and leaves
  only Pattern as the future addition.

When Effect is implemented later, the required extension path is therefore:

1. add the retained Effect structure to Scene ownership;
2. add named Effect parameter indices and raise the live count;
3. implement the Effect getter branch;
4. route each Effect setter through `autosave_markEffectParameterDirty()`;
5. make `autosave_markEffectDirty()` mark type plus all live parameters and the
   name once its identity source is gettable.

No Effect-specific change to Menu or the writer transaction should be needed.

### Phase 1 change 12 — documentation record and verification

During implementation, append dated notes to this document for each completed
change, any deviation, build sizes, and hardware results. Do not rewrite the
approved plan to disguise deviations.

Run the firmware build with warnings enabled and `git diff --check`. Inspect
the linker map to verify:

- exactly one 3,856-byte mask symbol remains;
- making it volatile did not create a copy;
- region stubs add no persistent parameter buffer;
- the existing 4,608-byte patch cache remains unchanged.

No SD-card fixture is modified merely to build Phase 1. Hardware verification
uses the current fully drained definitive records supplied by the user.

## Future parameter addition checklist preserved in code comments

Phase 1 comments must preserve this checklist beside the relevant enums and
owner structures:

- **New Bank field:** add a Bank field identifier/width, implement its live
  getter branch, and write it only through a BankData setter that marks after
  change.
- **New Scene parameter:** append a named Scene parameter index before the
  count sentinel, add the field and SceneData owner setter using the common
  Scene store helper, and extend the live getter. The count assertion must
  continue to equal the live-byte constant.
- **New Kit parameter:** append a named Kit index, add a SceneData Kit-owner
  setter using the common Kit store helper, and extend the getter. The Kit and
  Scene region markers will include it by looping the count.
- **New Instrument descriptor:** add it to the type's descriptor enum/table
  and retain the existing per-type count assertion. Normal/Morph Menu edits
  automatically use the generic Preset endpoint store and marker. No Autosave
  offset case is added as long as the descriptor count fits
  `INSTRUMENT_PARAM_COUNT` and the 72-byte endpoint allocation.
- **New Effect parameter:** append its Effect index, increase the live count,
  add retained Effect ownership and a setter using the Effect marker, and
  implement its getter. Effect/Scene region markers will then include it.
- **New direct whole-object assignment:** do not call hundreds of scalar
  setters. Commit the validated object once, then call its named region marker.
- **New copy/paste command:** use exactly one of the preserved scopes. Normal
  and Morph Instrument copies validate matching types before copying. Scene
  copy uses the no-Pattern scope until Pattern persistence is genuinely added.

This checklist is the future-proofing mechanism. A new retained field is not
complete merely because it was added to a C struct or Menu; it needs a named
wire index, a live getter, and either a change-aware scalar setter or a
post-commit region marker.

## Phase 2 detailed implementation plan: successful whole-object commits

Phase 2 connects the region-marker interfaces implemented in Phase 1 to the
current successful Kit, Scene, Instrument, InstrumentMrp, KitMrp, and Bank
commit paths. It does not add copy/paste commands, Pattern autosave, Effect
state, HCNAMES-backed name gets, a second dirty mask, or any new filesystem
transaction.

The exact Phase 2 source-file set is:

- `Core/Bank/Scene/Autosave.c` and `.h`;
- `Core/Bank/BankData.c` and `.h`;
- `Core/Hardware/SD/filesystem.c` and `.h`;
- `Core/Bank/Scene/Preset/presetManager.c` and `.h`;
- this document for implementation notes.

No Phase 2 source change is planned for `Core/Menu/menu.c`, `SceneData.c/.h`,
`config.h`, `main.c`, `storageTypes.c/.h`, Instrument parameter enums/tables,
InstrumentManager, MIDI, Pattern, DSP runtime modules, or files under
`SD_CARD/`. Menu already invokes the Preset commit starters after filesystem
completion; SceneData's scalar hooks are complete; storage parsers write only
staging objects; and the existing Autosave region loops already follow the
shared parameter counts and Instrument registry descriptors.

Every implementation change below must receive an adjacent comment block in
both `.c` and `.h` wherever an interface or ownership contract is exposed.
Each comment must state what the change does, why it exists, its inputs,
outputs, and affiliates. Existing comments that incorrectly describe commit
timing must be corrected in place.

### Pass 2 sequencing decision from the current code

The raw assignment lines are not the public success boundary:

- normal Kit Load assigns every selected `scene_t.kit` after all six staged
  Instruments validate, but its completion callback runs only after the shared
  final FAT/data flush;
- root Scene Load commits staged settings/Kit at Scene phase 33, then reads
  Pattern and Effects and publishes the selected Scene HCNAMES rows before its
  completion callback;
- Bank Load delegates selected child payloads through that same Scene reader,
  so an early child may already be resident when a later child fails;
- normal Instrument and both Morph projections only stage data in filesystem;
  their actual resident assignments occur later in Preset's Menu-invoked apply
  starters;
- successful Kit, Scene, and Instrument callbacks promote their destination
  Scenes into `BankData` before later code may treat those Scenes as present.

The Phase 1 region markers intentionally reject absent Scenes. Therefore:

1. normal Kit and root Scene regions are marked in their Preset completion
   callbacks, after the existing success-only Scene-presence promotion;
2. Instrument/InstrumentMrp/KitMrp regions are marked immediately after their
   actual Preset-owned resident copy;
3. delegated Bank child Scenes are not marked individually; one successful
   Bank-session replacement marks the complete current resident Bank only
   after the outer Bank request has passed its final flush and callback gate;
4. no parse stage, raw Pattern fan-out, failed callback, save-only serializer,
   or runtime DSP apply step receives a region marker.

This placement means a failed Scene/Bank request produces no new region bits.
It does not redesign the existing non-atomic Scene/Bank loader: phase 33 may
still have changed resident non-Pattern data before a later Pattern, Effect,
HCNAMES, or sibling-Bank-child failure. Making those filesystem operations
fully transactional would require different staging/rollback work and is
outside this targeted pass.

### Phase 2 change 1 — `Core/Bank/Scene/Autosave.h`: expose one Bank-session
replacement operation

Add one public Autosave-owned operation, named to express replacement of the
canonical dirty record for the current resident Bank, for example:

```text
autosave_replaceResidentBankSession()
```

What this does: identifies a successful runtime Bank identity commit as an
ownership boundary for the one existing 3,856-byte SRAM mask.

Why it must exist: Bank Load and Bank Save can change the Bank slot and/or
display name. Pending offsets from the prior identity cannot be allowed to
patch a record whose identity now describes a different Bank.

Inputs: no duplicate Bank image or mask. The function reads the final
`BankData` identity/presence state and current resident Scenes after the
successful filesystem transaction.

Outputs: when Phase 1 mutation tracking is enabled, the old canonical mask is
discarded and the current Bank's complete gettable non-Pattern parameter space
is marked dirty. When tracking is disabled during boot, it is a no-op so the
existing matching-file recovery/import behavior remains authoritative.

Affiliates: the Phase 1 tracking gate, all typed Bank/Scene region markers,
Preset's Bank Load/Save completion callbacks, and the writer's existing
identity validation/regenerate path.

The header comment must explicitly say this function allocates no second mask,
does no file/display work, and is not the ordinary scalar Bank setter path.

### Phase 2 change 2 — `Core/Bank/Scene/Autosave.c`: clear and fully re-dirty
the single canonical record

Implement the new session operation entirely inside Autosave, where mask and
wire ownership already reside.

Add a private one-mask-byte atomic replace/clear helper using the same
PRIMASK-save/restore boundary as the existing OR and take helpers. The session
operation must:

1. return immediately when mutation tracking is disabled;
2. clear all 3,856 bytes of `autosave_dirty_mask` in place—never allocate or
   copy a second mask;
3. mark all five current Bank fields (15 payload bytes total);
4. read the final `bank_scenePresentMask()` and call
   `autosave_markSceneWithoutPatternDirty(scene)` for every present Scene;
5. return without arming the writer or touching filesystem state.

What this does: converts the same canonical mask from old-Bank ownership to a
complete work list for the newly committed resident Bank.

Why it must exist: a partial Bank load can preserve unselected resident Scenes,
and a Bank Save can select a different resident Scene subset. Marking only the
request mask would not create a complete register for the final Bank. Looping
the final present mask includes both newly loaded and deliberately preserved
resident Scenes.

Inputs: final BankData name, restore slot, Scene-present mask, active Scene,
VOICE edit mask, and all present Scene/Kit/Instrument owners reached through
the existing getters/markers.

Outputs: dirty bits for every currently gettable Bank byte and every currently
gettable non-Pattern byte in each present Scene. Scene/Kit/Instrument names,
Effect type/name, zero-live Effect parameters, Pattern, padding, non-morphable
Morph selector cells, and absent Scenes remain clear because they still have
no live getter in this milestone.

Affiliates: `autosave_markBankFieldDirty()`,
`autosave_markSceneWithoutPatternDirty()`, `bank_scenePresentMask()`, and the
existing scheduler that observes `autosave_maskHasDirty()` on a later tick.

Concurrency rule: do not disable interrupts across the 3,856-byte clear or the
full region walk. Each byte clear remains bounded by its own existing-style
critical section. A producer that sets a bit before its byte is cleared is
still covered by the following full re-dirty pass; a producer that runs after
that cell is re-marked only ORs the already-set bit. Thus the clear-then-full-
mark sequence retains the latest live value without a second mask or one long
interrupt blackout.

### Phase 2 change 3 — `Core/Bank/BankData.c` and `.h`: add an explicitly
unnotified staged-Bank metadata commit

Add one filesystem-facing BankData operation that commits the complete loaded
or newly saved Bank metadata image without issuing the ordinary per-field
Autosave notifications. Its inputs are:

```text
display name
restore Bank slot
final resident Scene-present mask
active Scene
VOICE edit mask
resident-Bank-present state (set true by this operation)
```

The implementation must reuse BankData's existing normalization rules:
printable/space-padded eight-cell name, slot 0..999, 16-bit Scene masks, active
Scene 0..15, and the invariant that the active Scene belongs to the VOICE edit
mask. It stores the whole metadata image directly through private BankData
helpers and deliberately ignores change-return flags instead of calling the
public change-aware setters.

What this does: separates a filesystem transaction's provisional resident
metadata commit from an ordinary user/system scalar mutation.

Why it must exist: current Bank Load phases 17/20 and Bank Save phase 45 call
ordinary Bank setters before HCNAMES, final sync, and (for Save) the Bank index
rebuild have completed. If one of those later steps fails, the Phase 1 setters
can leak new-identity scalar bits into the old Bank's canonical mask. The new
batch boundary leaves the old mask untouched until Preset receives actual
success.

Inputs: validated filesystem operation scratch after payload/promotion work.

Outputs: normalized BankData fields and `bank_has_resident_bank = 1`, with no
Autosave bit side effect. Existing ordinary public setters remain unchanged
and continue marking real scalar edits.

Affiliates: filesystem Bank Load empty/full metadata commits, Bank Save
promotion, the successful callback session replacement, and
`bank_ensureActiveInVoiceEditMask()`.

The `BankData.h` comment must warn that this is not a general bypass: only a
staged Bank Load/Save transaction may use it, and the caller must arrange the
post-success Autosave session replacement. It does not promise rollback of the
existing resident filesystem payload if a later operation step fails.

### Phase 2 change 4 — `Core/Hardware/SD/filesystem.c`: route both Bank Load
metadata commits through the staged boundary

Replace the individual BankData setter sequences in both Bank Load completion
branches:

- the empty/intersection-zero branch currently reached after child discovery;
- the selected-child branch reached at phase 20 after every requested child
  has completed.

For the empty branch, pass the existing resident Scene-present mask exactly as
the current code preserves it. For the selected-child branch, calculate the
same final `old_present_mask | op_bank_scene_load_mask` merge before calling
the batch API. Preserve the current active-Scene selection, `scene_selectActive`
behavior, `preset_currentName`, HCNAMES cache overlay, and phase-83 handoff.

What this does: keeps BankData's current final values and partial-load semantics
while suppressing premature old-session dirty production.

Why it must exist: selected Scene payloads are installed incrementally and
Bank metadata is installed before HCNAMES is written. Neither point is the
outer request's successful completion boundary.

Inputs: `op_bank_display_name`, `op_slot`, final present mask,
`op_bank_active_scene`, and parsed `scene_mask_voice_edit`.

Outputs: unchanged resident Bank/Scene and HCNAMES behavior; no region or
session bit is published from inside the child loop or metadata phases.

Affiliates: `filesystem_loadBankDirectory_tick()`, the shared Scene loader,
BankData's new staged commit, phase 83..86 HCNAMES writer, and Preset's
`on_bank_load_complete()`.

Correct the adjacent `filesystem.c` Scene-loader overview while editing this
area: the current text says the whole Scene folder validates before resident
memory changes, but the implementation commits non-Pattern data at phase 33
before direct Pattern/Effect work. The corrected comment must document the
real order without changing it.

### Phase 2 change 5 — `Core/Hardware/SD/filesystem.c`: route Bank Save's
identity commit through the same staged boundary

At Bank Save phase 45, after the temporary Bank tree is promoted, replace the
individual BankData setters with the same unnotified batch commit. Pass the
existing saved Scene mask, active Scene, parsed VOICE edit mask, destination
slot, and display name. Preserve cache publication, HCNAMES phases 83..86, the
Bank rescan/`.hcindex` rebuild, and all current error behavior.

What this does: treats a successful save-to-new-slot/name as the same canonical
Bank ownership transition as a load.

Why it must exist: Save:[Bank] changes `bank_restoreBankSlot()` and can change
the display name/present mask. Carrying old pending offsets across that identity
would be equally incorrect even though no Scene payload was loaded from disk.

Inputs: the already-promoted Bank Save transaction state.

Outputs: unchanged Bank Save data and browser behavior; Autosave ownership is
not changed until the original callback returns success after the final index
rebuild chain.

Affiliates: `filesystem_saveBankDirectory_tick()`,
`filesystem_startLibraryIndexRebuild()`, BankData's staged commit, and Preset's
`on_bank_save_complete()`.

### Phase 2 change 6 — `Core/Hardware/SD/filesystem.h`: document truthful
load/save commit and Autosave ownership boundaries

Update the public request comments for normal Kit Load, root Scene Load, Bank
Load, and Bank Save.

The comments must state:

- filesystem stages/commits resident data but does not calculate Autosave
  offsets;
- Kit/Scene dirty regions are published by Preset only after successful
  completion and Scene-presence promotion;
- Scene non-Pattern commit precedes Pattern/Effect validation in the current
  loader, even though public success waits for all later work;
- Bank child commits never publish individual dirty regions;
- successful Bank Load or Bank Save replaces Autosave's one canonical session
  in the Preset callback; failed operations do not;
- save-only Kit/Scene/Instrument serializers do not mark resident data.

What this does: makes the public API describe the actual cross-module
transaction instead of implying that raw resident assignment equals public
success.

Why it must exist: future load/copy code needs to attach to the same final
success boundary rather than reintroducing early filesystem-side markers.

Inputs/outputs: comment-only contract changes; request signatures and runtime
behavior are unchanged.

Affiliates: filesystem state machines, Preset completion callbacks, BankData's
staged metadata API, and Autosave region/session markers.

### Phase 2 change 7 — `Core/Bank/Scene/Preset/presetManager.c`: publish
normal Kit and root Scene regions after real success

Add a small file-local mask walker, or two equivalently bounded loops, which
iterates `pm_kit_request_scene_mask` over valid resident Scene indices and
calls the supplied existing region marker.

Change `on_kit_load_complete()` so that, only while
`filesystem_status() == FS_STATUS_DONE`, it:

1. calls the existing
   `preset_markRequestedScenesPresentOnSuccessfulLoad()`;
2. marks each requested Scene with `autosave_markKitDirty(scene)`;
3. then calls `preset_completeFilesystemOp(PRESET_OP_KIT_LOAD)` as before.

Change `on_scene_load_complete()` in the same order, using
`autosave_markSceneWithoutPatternDirty(scene)`.

What this does: marks every actual multi-Scene destination, including a Scene
that was absent before the successful load.

Why it must exist: marking at the raw filesystem assignment would be too early,
while marking before presence promotion would be rejected by
`autosave_scenePayloadBase()`.

Inputs: filesystem success state and Preset's immutable accepted destination
mask.

Outputs: the existing present-mask Bank field notification plus whole Kit or
non-Pattern Scene region bits. Loaded bytes are marked even when equal to the
previous resident values because the validated load is itself the replacement
event.

Affiliates: normal Kit/Scene filesystem callbacks, BankData presence promotion,
Phase 1 region markers, and the unchanged Menu runtime apply paths.

Do not add equivalent calls to KitMrp callbacks, filesystem staging, Pattern
callbacks, save callbacks, or Menu: those paths either have not committed
resident endpoint data yet or do not mutate this payload.

### Phase 2 change 8 — `Core/Bank/Scene/Preset/presetManager.c`: replace the
canonical session after successful Bank Load and Bank Save

In `on_bank_load_complete()` and `on_bank_save_complete()`, check
`filesystem_status()` before `preset_completeFilesystemOp()` acknowledges the
facade. On success, call `autosave_replaceResidentBankSession()` exactly once;
on error, do not call it.

What this does: places the Bank ownership handoff after HCNAMES and the shared
flush gate; Bank Save also waits for its Bank rescan/index rebuild because the
original callback is parked until that chain completes.

Why it must exist: this is the first layer that can distinguish a publicly
successful runtime Bank transaction from staged/provisionally committed data.

Inputs: the final filesystem status and already-committed BankData/SceneData.

Outputs: one SRAM-only clear/full-redirty transition before Preset acknowledges
completion. The writer remains independently scheduled by a later idle
`filesystem_tick()`.

Affiliates: BankData's unnotified commit, filesystem completion/flush/index
chain, the Autosave session API, and the Menu Bank fallback/apply logic.

Boot behavior remains unchanged: the same callbacks may execute during initial
Bank restore, but Autosave mutation tracking is still disabled, so the session
call is a no-op. `filesystem_ensureAutosaveFilesBlocking()` later creates or
validates the pair and enables the existing one-time recovery path.

### Phase 2 change 9 — `Core/Bank/Scene/Preset/presetManager.c`: mark whole
normal Instrument replacements at the resident assignment

Inside `preset_startInstrumentApplyImage()`, immediately after each successful

```text
scene->kit.instruments[slot] = *staged
```

call `autosave_markWholeInstrumentDirty(target_scene_index, slot)`.

What this does: marks the three-byte type token, every active-type normal
descriptor, and every Morphable Morph descriptor for each actual destination.

Why it must exist: filesystem only stages a single Instrument. This Preset
helper is the sole resident commit shared by normal Instrument Load and the
hidden reversible `kit` restore, and inactive destination Scenes are valid
retained commits even though no DSP worker is armed for them.

Inputs: validated staged type/image, immutable destination mask, and slot.

Outputs: one Whole-Instrument dirty region per assigned Scene. Instrument name
remains excluded because HCNAMES is still its authority and no live name getter
exists.

Affiliates: `preset_startInstrumentApply()`,
`preset_loadInstrumentTemp()`, filesystem's staged Instrument accessor,
`autosave_markWholeInstrumentDirty()`, and the existing active-Scene runtime
reset/Morph/rebind worker.

The marker must remain inside the destination loop and before the
`active_scene_touched` early return. Placing it in the later DSP branch would
silently omit retained replacements in inactive selected Scenes.

### Phase 2 change 10 — `Core/Bank/Scene/Preset/presetManager.c`: mark
InstrumentMrp and KitMrp endpoint copies

For `preset_commitStagedInstrumentNormalToMorph()`:

- retain all existing staged/resident/requested type checks;
- after `preset_copyInstrumentNormalToMorphIfSameType()` reports a copy,
  call `autosave_markInstrumentMorphDirty(scene_index, slot)`;
- return the same success value used to decide whether active-Scene Morph work
  is queued.

For `preset_commitStagedKitNormalToMorph()`:

- after each same-type Instrument copy reports success, call
  `autosave_markInstrumentMorphDirty(scene_index, slot)` regardless of whether
  that Scene is active;
- keep `presetMorph_requestVoice()` gated to the active Scene exactly as it is;
- after the compatible slot-6 generated Morph decay assignment, explicitly
  mark `AUTOSAVE_KIT_PARAM_SLOT6_TRACK7_MORPH_DECAY` for that Scene, again
  independently of active runtime work.

What this does: marks only the destination cells that Morph projection is
allowed to replace.

Why it must exist: InstrumentMrp/KitMrp intentionally preserve type, normal
endpoint, supplemental selector, name, routing, and other Kit state. A whole
Instrument or whole Kit marker would overstate the commit. Conversely, tying
dirty production to `active_queued` would omit valid retained copies into
inactive Scenes.

Inputs: successfully staged source, same-type destination, descriptor Morphable
flags, selected Scene mask, and generated slot-6 compatibility check.

Outputs: all Morphable destination endpoint bits plus Kit parameter index 1
where copied. These regions are marked even when copied bytes equal old values,
matching the successful-load replacement rule. Type mismatch remains a total
no-change/no-dirty result for that slot.

Affiliates: the shared same-type copy helper, Instrument registry descriptors,
Morph worker queue, SceneData Kit geometry, and the two existing Autosave Morph
markers.

### Phase 2 change 11 — `Core/Bank/Scene/Preset/presetManager.h`: publish the
post-load dirty contracts

Update comments adjacent to:

- `preset_loadKitForScenes()`;
- `preset_loadSceneForScenes()`;
- `preset_loadBank()` and `preset_saveBank()`;
- normal Instrument load/apply declarations;
- `preset_loadKitMorphForScenes()`;
- `preset_loadInstrumentMorph()`;
- `preset_startKitMorphApply()` and
  `preset_startInstrumentMorphApply()`.

What this does: records which layer stages data, which layer commits it, and
which successful boundary marks the matching dirty scope.

Why it must exist: normal whole-object replacement and Morph projection have
different scopes and different commit times. Future callers must not infer that
filesystem success alone means a Morph stage was already resident.

Inputs/outputs: comment-only public-contract changes; no signature changes are
needed in Preset.

Affiliates: completion callbacks, Menu's existing apply starters, Autosave
region markers, and filesystem request contracts.

The comments must preserve the future copy/paste stubs: Whole Instrument,
same-type Instrument Normal, same-type Instrument Morph, Kit, Scene without
Pattern, future Scene with Pattern, and future Effect. Phase 2 consumes only
the scopes named above and does not implement copy/paste.

### Phase 2 change 12 — targeted verification and implementation record

During implementation, append dated notes here for each completed change and
any deviation. Do not rewrite this plan to disguise a different commit point.

Run `make -j4` with the existing warning flags and `git diff --check`. Inspect
the map/symbol output to verify:

- exactly one 3,856-byte `autosave_dirty_mask` remains;
- no new mask, record buffer, or filesystem cache was allocated;
- the existing 4,608-byte transaction patch cache is unchanged;
- no `SD_CARD/` fixture changed as part of implementation.

Perform the Pass 2 hardware tests from fully drained records, one operation at
a time:

1. normal Instrument Load into one Scene: expect type + active descriptor
   normal/Morph cells, no Instrument-name bit;
2. InstrumentMrp same type: expect Morphable Morph cells only; repeat with a
   type mismatch and expect no new region;
3. normal Kit Load with a multi-Scene mask including one formerly absent Scene:
   expect the present-mask field plus the full implemented Kit region in every
   destination;
4. KitMrp: expect only same-type Morphable Morph cells and compatible generated
   Kit Morph-decay cells;
5. root Scene Load: expect the present-mask field plus all current non-Pattern
   Scene/Kit/Instrument cells only after full success;
6. partial Bank Load: expect a fresh canonical session containing all Bank
   fields and every final present Scene, including preserved unselected Scenes;
7. Bank Save to a different slot/name: expect the same fresh-session behavior;
8. repeat each applicable load with unchanged source values and confirm the
   region is still marked/drained;
9. exercise a malformed/failed Kit, Scene, Instrument, and Bank request and
   confirm no corresponding region/session bits are published;
10. boot from the definitive valid pair and confirm the boot callback does not
    force a full re-dirty—the existing file-carried recovery mask alone governs
    startup work.

For each successful test, validate both ping-pong CRCs/generations, confirm the
newest valid record's remaining mask drains to zero, and compare payload bytes
against the retained Bank/root source as applicable. After the final mask is
clean, verify no further autosave filesystem transaction occurs.

## Recommended test passes

### Pass 1: ordinary changes

Start from fully drained records and change one field in each category:

1. Bank active Scene and VOICE edit mask;
2. one Scene parameter and one multi-Scene fan-out parameter;
3. one normal Instrument descriptor;
4. the same descriptor's Morph endpoint;
5. one supplemental descriptor;
6. both generated Kit decay endpoints;
7. one MIDI channel and note.

Verify exact payload offsets, clean CRC/commit/generation progression, and no
new write for an identical setter value. After the mask drains, verify idle
time produces no additional filesystem transaction.

Also change the same parameter while a drain is active and verify the re-dirty
bit survives into a continuation write. Repeat during playback with a retained
velocity Scene target to exercise the interrupt path.

### Pass 2: load commits

Test Instrument, InstrumentMrp, Kit, KitMrp, Scene, and partial Bank loads one
at a time. Verify only successfully committed non-Pattern data becomes dirty;
failed loads produce no region bits.

## Acceptance criteria

- There is still exactly one 3,856-byte canonical mask.
- Every currently gettable retained parameter change sets its exact bit.
- Unchanged writes do not create dirty work.
- Repeated changes coalesce.
- Foreground classification cannot erase an interrupt-side re-dirty event.
- Dirty producers perform no filesystem or display work.
- Multi-Scene edits mark every affected Scene.
- Normal and Morph endpoints remain distinct.
- Runtime-only, derived, Pattern, unimplemented Effect data, and staging writes
  remain excluded, while named Effect single/region stubs are present for the
  future owner.
- Successful loads mark committed regions; failed loads do not.
- A clean mask produces no background filesystem operation after boot recovery.
- Existing CRC publication, commit-last ordering, rollback, timing, and
  Load/Save suspension remain unchanged.

## Phase 1 implementation notes — 2026-08-01

Phase 1 was implemented against the source state described above. No Phase 2
whole-object load/copy hook was enabled, no Pattern or Effect storage was
invented, and no file under `SD_CARD/` was changed for this implementation.

### Completed format and canonical-mask work

- `Autosave.h` now owns named Bank-field, Scene-parameter, and Kit-parameter
  identifiers plus explicit Effect parameter geometry with a zero live count.
  Compile-time assertions bind those counts to the existing allocations.
- The sole 3,856-byte canonical mask is now volatile. A separate one-byte
  tracking gate remains disabled during boot population and becomes enabled
  only after successful autosave-pair setup.
- All mask OR operations used by scalar producers, file-mask recovery, and
  transaction rollback now protect one mask byte with a PRIMASK save/restore
  critical section.
- The writer-facing test/clear pair was removed. Phase 56 now uses one atomic
  `autosave_maskBitTake()` before performing the live get, so a later
  interrupt-side mutation survives as a re-dirty bit.
- The streamed transform copies volatile mask bytes into its stable write chunk
  with an explicit bounded byte loop before CRC/write processing.

### Completed scalar owner hooks

- `BankData` compares normalized final values, stores first, and marks changed
  restore slot, display name, Scene-present mask, active Scene, and VOICE edit
  mask fields. The private invariant repair now reports hidden mask changes so
  its setters and self-repairing getter cannot bypass notification.
- `SceneData` has common change-aware Scene and Kit byte stores. They cover the
  complete current 40-byte Scene parameter order and both generated Kit decay
  endpoints. New owner setters close the former direct-write holes for overall
  Morph and Scene-wide decimation; the six-value Morph fan-out reuses the
  single-value setter.
- `Preset` has one generic normal/Morph endpoint store used by morphable
  endpoint edits and non-morphable supplemental selector edits. It compares,
  stores, and marks by active-type descriptor index; derived interpolation and
  nonserialized selector mirrors remain unmarked.
- Existing Instrument registry coverage remains generic: 39 Drum, 38 Snare,
  39 Cymbal, and 39 HiHat normal descriptors, with Morph marking gated by the
  registry's Morphable flag. No type-specific dirty switch was added.

### Completed future scope and Effect stubs

- Source-level region markers now exist for Whole Instrument, same-type Normal,
  same-type Morph, Kit, Effect, Scene without Pattern, and Scene with Pattern.
  Phase 1 does not call these from load/copy commits.
- Whole Instrument marks type plus live endpoint cells but deliberately not its
  HCNAMES-owned name. Kit composes both live Kit parameters and six Whole
  Instruments. Scene without Pattern composes all Scene settings, Effect, and
  Kit. Scene with Pattern remains an explicit non-Pattern alias with a TODO.
- The Effect single marker, whole-Effect marker, and live getter are explicit
  no-ops behind a zero live count. `SceneData.h` records the required retained
  owner/getter/setter extension path without allocating dummy Effect state.

### Completed scheduler/lifecycle work

- Successful boot pair setup enables mutation production and schedules one
  delayed recovery validation/mask import even when SRAM starts clean.
- Successful recovery clears the recovery flag. A clean canonical mask then
  disarms at the earliest scheduler boundary and starts no filesystem request.
- The first later dirty bit receives the unchanged five-second debounce;
  successful remaining backlog retains the unchanged 250 ms continuation.
  Errors retain the five-second retry. Load/Save suspension is unchanged.
- No-resident-Bank state, setup failure, and diagnostic facade reset leave
  tracking disabled and recovery unscheduled.

### Verification performed

- `make -j4` completed successfully with the existing firmware flags
  (`-Wall -Wextra`, Cortex-M7 hard-float, LTO). The only compiler warnings were
  the five pre-existing unused static helpers in `filesystem.c`; linker output
  retained the existing newlib syscall warnings.
- `git diff --check` passed.
- `arm-none-eabi-nm -S --size-sort build/lxr02.elf` reports exactly one
  `autosave_dirty_mask` at `0x0f10` bytes (3,856), one one-byte tracking gate,
  and `fs_autosave_parameter_cache` unchanged at `0x1200` bytes (4,608).
- `arm-none-eabi-size build/lxr02.elf` reports text 364,836 bytes, data 400
  bytes, and BSS 78,436 bytes for this build.
- Repository-wide assignment review found only initialization, staging/parser,
  derived selector mirror, and explicitly deferred Phase 2 whole-object paths
  outside the new scalar owner boundaries. Those paths remain intentionally
  unchanged for the Pass 1 hardware test.

Hardware behavior remains to be verified with the definitive fully drained
`.hcprms1`/`.hcprms2` start pair using the Pass 1 matrix above.

## Phase 2 implementation notes — 2026-08-02

Phase 2 was implemented exactly within the eight source files listed by the
detailed plan. No code was changed in Menu, SceneData, config, main,
storageTypes, Instrument parameter tables, Pattern/DSP/MIDI modules, or under
`SD_CARD/`. No copy/paste command, Pattern persistence, live Effect owner, or
additional filesystem transaction was introduced.

### Completed single-record Bank-session ownership

- `Autosave` now exposes `autosave_replaceResidentBankSession()`. With mutation
  tracking enabled it clears the sole canonical mask in place, one byte per
  short PRIMASK critical section, then marks every Bank field and every final
  present Scene's complete currently gettable non-Pattern scope. With boot
  tracking disabled it is a no-op.
- The replacement uses no second mask and does no SD, display, scheduling, or
  scalar-setter work. The existing scheduler sees the resulting canonical mask
  on a later tick.
- `BankData` now has one filesystem-only staged metadata commit. It normalizes
  the eight-byte name, 0..999 slot, present mask, active Scene, VOICE edit mask,
  and active-in-edit-mask invariant together, sets resident-Bank presence, and
  deliberately emits no individual dirty bits.
- Both Bank Load metadata branches and Bank Save's post-promotion metadata
  boundary use that staged API. Existing partial-load presence merge,
  active-Scene selection, HCNAMES publication, Bank index rebuild, flush, and
  error flow remain in place.
- Successful Bank Load and Bank Save callbacks perform exactly one session
  replacement after provenance/settings publication. Failed callbacks do not
  replace the canonical mask. This pass intentionally retains the documented
  pre-existing non-transactional payload/metadata behavior if a later
  filesystem phase fails.

### Completed successful object-region publication

- Normal Kit Load success first promotes every requested destination Scene to
  the Bank present mask, then a shared bounded mask walker marks each whole-Kit
  region. Failure publishes neither the presence promotion nor Kit scope.
- Root Scene Load success uses the same ordering and marks each complete
  Scene-without-Pattern region. Pattern and the zero-live Effect owner remain
  unchanged. Filesystem comments now accurately state that the current loader
  commits its non-Pattern image before its later Pattern/effect/public-success
  work.
- Normal Instrument and reversible `kit` image commits mark the whole
  Instrument immediately after each retained destination assignment and before
  the active-Scene-only DSP branch. The scope includes type, Normal, and Morph
  payload but not the HCNAMES-owned display name.
- InstrumentMrp marks the whole Morph region only after a successful same-type
  descriptor copy. KitMrp does the same for every successful same-type slot in
  every selected resident Scene, independent of active DSP status, and marks
  the separate generated slot-6/track-7 Morph decay Kit parameter.
- Save-only Kit, Scene, and Instrument serializers remain unmarked because they
  do not replace retained parameter storage. Public `.h` contracts now name all
  commit/dirty boundaries and preserve the future Whole Instrument,
  same-type Normal, same-type Morph, Kit, Scene-without-Pattern, later
  Scene-with-Pattern, and future Effect extension points.

### Verification performed

- `make -j4` completed successfully with the existing Cortex-M7 hard-float,
  LTO, `-Wall`, and `-Wextra` flags. The only compiler warnings were the five
  pre-existing unused static helpers in `filesystem.c`; linker output retained
  the existing newlib `_close`, `_lseek`, `_read`, and `_write` warnings.
- `git diff --check` passed.
- `arm-none-eabi-nm -S --size-sort build/lxr02.elf` reports exactly one
  `autosave_dirty_mask` at `0x0f10` bytes (3,856), the one-byte mutation gate,
  and `fs_autosave_parameter_cache` unchanged at `0x1200` bytes (4,608). No
  second dirty mask or new persistent object cache was allocated.
- `arm-none-eabi-size build/lxr02.elf` reports text 367,732 bytes, data 400
  bytes, and BSS 78,468 bytes for this build.
- A source search confirms the three filesystem Bank Load/Save metadata commit
  sequences no longer call the public autosave-notifying Bank setters. The
  remaining filesystem Bank setter calls are unrelated boot/settings/name
  scalar paths and retain their Phase 1 behavior.

The Pass 2 hardware matrix above remains outstanding. Begin from a fully
drained valid pair so each successful region/session publication and each
failure no-publication case can be distinguished unambiguously.

## Phase 2 narrow ownership correction — 2026-08-02

This section supersedes the earlier Phase 2 Bank-session interpretation while
preserving that implementation record as history. The canonical mutation mask
is not discarded when Bank name/slot changes: those fields are ordinary
CRC-covered payload, and retained mutations continue to coalesce in the same
single SRAM record.

- Streamed record validity is now exact size, magic/version, final commit, and
  CRC only. Bank name/slot equality is no longer a validation key.
- `bank_active_scene_slot` is now the only retained active-Scene owner;
  SceneData exposes compatibility get/select calls without retaining a second
  byte.
- Bank Load retains the current active Scene while playing. While stopped it
  resolves parsed stored active, retained current active, then lowest final-
  present Scene. Only successfully loaded Scene regions are ORed dirty after
  public success.
- Bank Save's active fallback is file-only `bankset.bcg` scratch. Save leaves
  live active/presence/VOICE and Scene parameter regions unchanged, then adopts
  only durable name/slot through normal setters plus existing provenance.
- The mutation record, writer cache, format geometry, CRC/commit transaction,
  Pattern/Effect scope, and `SD_CARD/` fixtures were not expanded or changed.

`make -j4` and `git diff --check` pass. The ELF retains exactly one 3,856-byte
dirty mask, one authoritative active byte, and the unchanged 4,608-byte writer
transaction cache. Targeted hardware testing of the corrected Bank Load/Save
behavior remains outstanding.
