# Instrument Load Parameter Lock Bug

## Implementation Status

The Instrument-load transaction/lifecycle fixes were implemented after the
live Kit-versus-Instrument test isolated the one-slot path:

- `filesystem.c` now parses a root Instrument file into a private staged
  `kit_instrument_slot_t` and staged display name. It no longer resets or fills
  resident SceneData during asynchronous file I/O.
- `preset_loadInstrument()` publishes Scene/slot/type/index completion context
  only after filesystem accepts the request, so a rejected overlapping request
  cannot redirect the first operation's completion apply.
- active-Scene commit clears all six current LFO pairs and velocity targets
  before changing slot type, copies the staged slot, resets only the incoming
  runtime instance, then rebuilds all six retained Morph/runtime images.
- after runtime rebuild, Preset normalizes both LFO target pairs and velocity
  target for one source per tick and reinstalls all six source relationships.
- Menu keeps the transaction locked through file read, commit, Morph rebuild,
  and target rebind. Encoder, Scene selection, VOICE selection/preview, and mode
  changes are consumed without mutating destination context while locked.
- load-time LFO pair normalization now makes the selected target-voice cell and
  canonical parameter ID agree, or changes an invalid pair to Off.

The implementation is documented adjacent to declarations and logic in the
affected `.h` and `.c` files. `make -j4` completes successfully; only the
project's existing newlib syscall warnings are emitted. Hardware verification
of repeated/rapid Instrument loads is still required.

This implementation does not claim to repair the separate Pattern automation
representation mismatch or raw-float modulation semantics documented below.
Those remain follow-up correctness work, but they are no longer part of the
one-Instrument commit race/lifecycle fix.

## Scope And Conclusion

This investigation used the current source tree and checked-in `SD_CARD`
instrument files. No firmware code was changed.

### Live Evidence Update

The additional live-state observations narrow, but do not dismiss, the
modulation diagnosis:

- Instrument Load reports Voice 2 as `slakd2`.
- Assigning Voice 1 or Voice 2 LFO to Voice 2 Decay does not produce an
  apparent audible change.
- Loaded kits and individual instruments generally do not appear to restore
  valid LFO assignments.

The checked-in `SD_CARD/Instrument/slakd2.drm` and
`SD_CARD/Kit/001 Slak/slakd2.drm` files are byte-for-byte identical. Their
stored main and Morph `amp_envelope_decay` values are both `56`; their
`lfo_amount` is `0`; their `lfo_target_param` is the off sentinel `65535`; and
their velocity destination is also off. Therefore `slakd2` itself cannot load
the extreme Voice 2 decay or an active LFO assignment from its own file.

For a Drum in Voice 2, the menu, trigger, asynchronous DSP, mixer, Morph, and
normal parameter-write paths all resolve to the same native runtime object,
`voiceArray[1]`. There is no evidence of Voice 2's menu editing one Drum while
the mixer plays another. The leading candidates are now:

1. an LFO in another source slot whose loaded canonical target points to Voice
   2 Decay, despite its displayed target voice being inconsistent;
2. a stale modulation target/baseline left by an earlier instrument identity;
3. step automation writing through the incompatible legacy automation path;
4. runtime state corruption caused by that automation path or by an
   out-of-range stored value.

The report's original raw-float decay mechanism remains valid once a direct
LFO target is actually installed. However, the `slakd2` evidence means Voice
2's own loaded LFO is not the source. Voice 3 through Voice 6 assignments are
the most useful live evidence to inspect next.

The Voice 2 symptom has two source-confirmed failure mechanisms: an incorrectly
installed or stale modulation destination, and an incompatible Pattern
automation representation with an out-of-bounds-read route. The current live
state is not yet attributable to one mechanism without distinguishing stopped
manual preview from sequenced triggering. Neither mechanism requires the stored
Voice 2 decay endpoint itself to be corrupt. In the modulation case, a loaded
instrument can install an LFO destination that resolves to Voice 2 Drum
`amp_envelope_decay`; the LFO then writes the live DSP decay field every audio
block while the VOICE menu continues to display and edit the retained endpoint.

The current implementation has three defects that make this worse than an
ordinary visible modulation assignment:

1. Converted target voice and target parameter fields are not reconciled after
   loading an instrument.
2. Dynamically assigned instrument LFO nodes are dispatched, but are omitted
   from modulation reset and base-value refresh enumeration.
3. Direct modulation writes raw runtime floats and bypasses descriptor-specific
   shapers. For envelope decay, zero is not a short decay: it is a zero decay
   increment, so the envelope does not fall.

This can affect parameters other than decay. The exact multi-minute or infinite
tail is most likely on time/increment parameters such as amplitude-envelope
decay, but any direct mod-targetable parameter can appear resistant to menu
edits while a stale or hidden LFO overlay keeps rewriting its runtime field.

## Most Likely Live Failure Path

The following path matches the reported behavior:

1. An instrument with a nonzero LFO amount is loaded into any source slot.
2. Its stored `lfo_target_param` resolves to canonical ID `76`.
3. Canonical voice parameter IDs are `slot * 64 + descriptor_index`, so `76`
   resolves to zero-based slot 1 (Voice 2), descriptor index 12.
4. For a Drum in Voice 2, descriptor index 12 is
   `amp_envelope_decay`.
5. Instrument files generated before `lfo_polarity` was persisted leave the
   reset default of negative polarity.
6. Negative LFO modulation subtracts a range-relative amount from the captured
   runtime decay baseline and can clamp the live float to `0.0f`.
7. `SlopeEg2` decay processing performs `value -= decay`. A decay increment of
   zero leaves the envelope value unchanged, producing an indefinitely held or
   extremely slow tail.
8. Turning Voice 2 Decay edits the Scene endpoint and queues Morph apply, but
   the next LFO dispatch can overwrite the live DSP float again. The screen
   therefore changes without an audible correction.

This path does not require Voice 2's own LFO to be the source. Any of the six
source LFOs can target Voice 2.

## Evidence In The Current Tree

### Most converted LFO target pairs are internally inconsistent

A read-only scan of the root Instrument pool found 55 files whose first LFO
pair has a nonzero amount and a finite canonical target. Only 13 pairs have a
stored `lfo_target_voice` that agrees with the voice encoded in
`lfo_target_param`; 42 pairs disagree.

Representative mismatches include:

- `seawakc1.cym`: amount `73`, displayed/stored voice `5`, canonical target
  `76`, which decodes to Voice 2;
- `filmodd3.drm`: amount `120`, displayed/stored voice `3`, canonical target
  `76`, which decodes to Voice 2;
- `rollins1.snr`: amount `111`, displayed/stored voice `3`, canonical target
  `76`, which decodes to Voice 2;
- `beatmsh1.hat`: amount `79`, displayed/stored voice `5`, canonical target
  `73`, which decodes to Voice 2.

This directly explains why loaded LFO assignments appear invalid. Menu display
normalizes a stored target parameter against its companion target voice. A
mismatched pair is rendered as off or invalid until edited. Runtime apply does
something different: descriptor order reaches `lfo_target_voice` first, where
the value is only range-checked, then reaches `lfo_target_param`, where the
canonical ID is installed directly. Runtime therefore follows the voice packed
inside the canonical parameter while the menu follows the separate voice cell.
The loaded state has two contradictory accounts of the same destination.

This is not evidence that `storageTypes.c` failed to read the fields. The
values are present and parsed. It is a conversion/apply contract failure: the
two fields are loaded independently and never reconciled as one target pair.

### The converter does not preserve the current descriptor contract

`tools/convert_legacy_kits.py` converts legacy `target_lfo` list indices into
canonical `slot * 64 + descriptor_index` values, but copies legacy
`voice_lfo` bytes independently. It never verifies that the copied target
voice agrees with the slot encoded by the converted canonical target.

The converter's hardcoded `DESCRIPTOR_KEYS` lists are also behind the actual C
descriptor arrays. Current instrument descriptors contain additions including
`lfo_amount_2`, `lfo_polarity`, and the second target voice/parameter pair.
Because canonical IDs depend on descriptor position, every converted target to
a descriptor after an inserted row can resolve to the wrong current
descriptor. Voice 2 Drum Decay at local index 12 precedes those insertions, so
canonical ID 76 remains a real Voice 2 decay target; later targets require a
full regenerated mapping from the actual current descriptor order.

Converted files omit the new second-pair and polarity fields. Slot reset makes
pair 2 inactive and leaves polarity at its enum-zero negative setting. This is
not by itself the Voice 2 source, but it means a nonzero migrated first LFO
amount uses negative polarity unless explicitly edited after load.

### Direct target ID 76 exists in shipped Instrument files

Several root Instrument-pool files have nonzero LFO amounts and target ID 76,
including:

- `SD_CARD/Instrument/organic1.cym`: `lfo_amount=82`,
  `lfo_target_voice=5`, `lfo_target_param=76`
- `SD_CARD/Instrument/beatmsc1.cym`: `lfo_amount=67`,
  `lfo_target_voice=5`, `lfo_target_param=76`

The runtime installer ignores the companion `lfo_target_voice` when installing
the destination. `instrumentManager_writeRuntime()` passes the stored target
parameter directly to `instrumentManager_installLfoModulationTarget()`, which
decodes the canonical ID itself. Therefore these files can display Voice 5 as
the target context while runtime ID 76 addresses Voice 2/local 12.

The converter did translate old modulation-list indices to canonical IDs; ID
76 is not an unconverted legacy number. The defect is that the independently
stored target-voice cell is preserved without being reconciled to the canonical
parameter ID during load. Menu reconciliation only runs when the user edits a
target cell.

The report's earlier examples are not isolated anomalies. The pool-wide scan
above shows that target-pair disagreement is the dominant converted-file case.

### Dynamic LFO enumeration is inconsistent

`instrumentManager_dispatchRuntimeLfos()` correctly resolves and dispatches the
current runtime LFO for every slot, including `runtime_snare_slots[]`,
`runtime_cymbal_slots[]`, `runtime_hihat_slots[]`, and extra Drum instances.

In contrast, `modNode_resetTargets()` and
`modNode_directOriginalValueChanged()` enumerate only:

- `voiceArray[0..2]`
- the fixed `snareVoice`
- the fixed `cymbalVoice`
- the fixed `hatVoice`
- all six velocity modulators

They do not enumerate LFO nodes in the dynamic runtime pools. This creates two
broken contracts:

- A dynamically assigned source LFO is applied each block but its direct target
  is not restored by the normal block reset.
- A VOICE/Morph/base edit calls `modNode_directOriginalValueChanged()`, but a
  dynamic source LFO targeting that parameter does not refresh its captured
  baseline. It continues calculating from the old value and overwrites the new
  edit on the next dispatch.

This is particularly relevant when a Snare, Cymbal, or HiHat is loaded into
Voice 2, because Voice 2 then uses a dynamic runtime pool rather than one of
the fixed native objects.

### Envelope modulation bypasses the byte-domain shaper

Normal `amp_envelope_decay` application goes through
`instrumentManager_writeSpecialRuntime()` and `slopeEg2_setDecay()`. The
user-facing byte is nonlinearly converted into the small positive decrement
consumed by `SlopeEg2`.

Descriptor-backed direct modulation instead resolves a pointer to
`SlopeEg2.decay`, gives `TYPE_FLT` a generic `0.0..1.0` range, and writes the
modulated float directly. It does not call `slopeEg2_setDecay()`. The generic
range is therefore the wrong semantic domain for this parameter:

- a runtime value near zero means a very long decay;
- exactly zero means no decay progress;
- modulation depth is scaled in raw runtime-float space rather than in the
  displayed `0..127` time-control domain.

This explains why the audible result can resemble an inaccessible value above
the normal Decay range even though no stored endpoint exceeds 127.

### Instrument text values are not descriptor-range validated

`storage_parseU8()` accepts `0..255` for ordinary instrument fields. The parser
only applies a special clamp to LFO target-voice selectors; it does not validate
normal values against descriptor dtype.

The checked-in Instrument files examined here do not contain an
`amp_envelope_decay` above 127, so this is not the leading explanation for the
current live state. It is still a real secondary route: a hand-edited or corrupt
file can load `128..255` into a `DTYPE_0B127` decay endpoint. The shaping
formula can then produce a negative decrement, causing the envelope value to
increase rather than decay.

### Step automation still uses the retired flat MIDI destination protocol

`Step.param1Nr` and `Step.param2Nr` are now declared as 16-bit
`instrument_param_id_t` values and their empty value is the 16-bit
`INSTRUMENT_PARAM_INVALID` sentinel. The playback backend has not migrated with
that storage type:

- `seq_parseAutomationNodes()` sends both fields to `automationNode.c`;
- `AutomationNode` still treats destination `255` as the only off value;
- destinations below/above 128 are packed as legacy MIDI CC/MIDI_CC2 messages;
- replacing a destination reads
  `midiParser_originalCcValues[node->destination]`, whose array has only 255
  elements;
- the MIDI parser then applies a legacy flat sound parameter, not a descriptor
  target resolved against the loaded instrument type.

Consequently, canonical values can automate unrelated legacy parameters,
`65535` is not recognized as off, and destination replacement can read far
outside `midiParser_originalCcValues`. This is a credible memory-corruption
route that can affect any runtime parameter, not only decay. It is especially
important if the fault began while a pattern was running or after an automated
step played.

PatternData itself currently mixes both contracts. Some paths and comments say
the Step fields hold canonical IDs, while `pat_setStepAutomationDestination()`
still truncates to eight bits and applies the old CC `+1` packing. Instrument
Load also changes a slot's descriptor table without clearing or reconciling
existing step destinations. The automation migration must be completed as one
end-to-end contract rather than patched only at playback.

## Why Instrument Load Exposes It

Instrument Load changes both source and destination runtime identities while
modulation nodes retain live pointers and captured baselines.

The one-slot apply path currently:

1. resets and parses the selected Scene slot;
2. applies that slot's supplemental target selectors;
3. queues that slot's Morph/image values;
4. does not clear the outgoing runtime LFO before changing type;
5. does not re-resolve all other source LFO/velocity destinations that point
   into the changed destination slot;
6. does not normalize the loaded target voice/parameter pair.

Consequences include:

- the new source instrument can immediately install a surprising absolute
  target retained from its original kit;
- a source target can point to a descriptor whose meaning changed when the
  destination slot changed type;
- direct target pointers from other sources can still point at the old runtime
  object for the replaced slot;
- dynamic source nodes are omitted from reset/baseline maintenance;
- an outgoing dynamic source can leave its last target value in place.

Instrument Load does not currently reconcile Pattern automation either. A
stored local descriptor identity can become invalid or change meaning when the
slot's instrument type changes, and the still-legacy playback backend cannot
validate that identity against the active Scene instrument registry.

## Can It Happen To Any Parameter?

There are three answers depending on failure class.

### Appears locked against menu edits

Yes, for any descriptor-backed direct modulation target. All normal `ROW(...)`
instrument descriptors currently carry the Morphable, Modulateable, and
Automatable flags. If an active or stale LFO node targets one of those runtime
fields, it can rewrite the field after Menu/Morph applies the edited endpoint.

Selector and supplemental fields declared with `ROW_NOBIND(...)` use separate
owners and are not exposed through the same raw direct-pointer path.

### Produces catastrophic stuck or runaway behavior

The highest-risk parameters are those whose runtime field is an increment,
coefficient, cached shaped value, enum, or other non-display-domain value.
Examples include:

- amplitude-envelope attack/decay;
- pitch-envelope decay;
- filter/tuning/distortion fields that normally use a special setter;
- any future descriptor whose displayed `0..127` byte is transformed before
  reaching DSP storage.

Plain normalized gains are less likely to create a multi-minute tail, but can
still sound locked or jump unexpectedly.

### Stored endpoint corruption

Potentially yes for all fields whose descriptor range is narrower than
`0..255`, because storage currently performs type-width parsing rather than
descriptor-dtype validation. The severity depends on the runtime writer.

## Other Explanations To Distinguish In The Live State

Two valid features can partially mimic the same symptom:

- If Voice 2 Morph (`2vm`) is 255, editing only the main endpoint has no effect
  on the active Morph endpoint. At intermediate Morph values the edit has a
  reduced effect. This should remain internally consistent and is not itself a
  parameter lock.
- Velocity modulation can rewrite a target at trigger time. Unlike an LFO it
  does not continuously overwrite every audio block, but a nonzero velocity
  amount and Voice 2 decay destination can still explain a trigger-dependent
  value.

These do not remove the modulation lifecycle defects above.

## Live-State Test

### Trigger-path discriminator

The first non-destructive observation was whether Voice 2 still produces the long
tail when the sequencer is stopped and Voice 2 is triggered manually. A normal
manual preview with a bad sequenced trigger strongly selects the Pattern
automation path. A bad manual preview keeps stale/cross-voice modulation or
already-corrupted runtime envelope state in scope.

**Observed:** the long tail occurs both from sequenced Voice 2 steps and from
the Voice 2 button while the sequencer is stopped. Step automation is therefore
not the immediate live writer. It remains a possible earlier corruption route,
but both trigger paths are consuming the same already-bad Voice 2 runtime
envelope state.

Further observations exclude the normal endpoint and polarity explanations:

- Voice 2 `2vm` was `0`;
- both main and Morph Voice 2 decay endpoints were manually set to values in
  the low thirties;
- LFO tests across all polarities and several amounts did not change the
  audible decay;
- loading multiple different Drum files into Voice 2 preserved the fault;
- loading a different complete Kit cleared the fault.

This is now specific to one-slot Instrument apply/replacement. A Kit apply
walks all six source slots, applies every supplemental off/target cell, and
rebuilds all six Morph/runtime images. Instrument apply touches only the loaded
slot. Repeated Voice 2 Instrument loads therefore cannot clear a stale current
source in another slot, cannot find an outgoing source orphaned by a previous
type change, and do not rebind all cross-slot destinations whose target runtime
identity changed. A Kit load performs enough global work to remove the bad
state, which is why changing the endpoint files alone does not recover it.

### Slak kit cross-check

`SD_CARD/Kit/001 Slak/kitset.kcg` assigns `slakd2.drm` to slot 2. Every one of
the six checked-in Slak instrument files has first-pair `lfo_amount=0` and
`lfo_target_param=65535`, plus `velo_mod_amount=0` and
`velo_mod_dest=65535`. The files predate the second LFO pair, so slot reset
supplies its inactive defaults. Voice 2's main and Morph decay are both `56`.

A complete, clean apply of this kit should therefore clear all current source
destinations and shape Voice 2 decay from `56`. The persistent live fault is
not encoded in the Slak files. It proves at least one of the following:

1. the current runtime was not fully rewritten from the parsed Slak images;
2. an outgoing source node survived an earlier Instrument type replacement and
   retained a direct pointer/baseline outside the current six-source lookup;
3. a prior out-of-bounds automation read damaged runtime or modulation state;
4. a later foreground/audio writer is restoring a stale runtime baseline even
   though the current stored source cells display off.

This strengthens the lifecycle diagnosis. Instrument Load changes SceneData's
slot type before the apply path asks InstrumentManager for the current LFO.
Without an explicit pre-replacement clear, an outgoing dynamic-pool LFO can no
longer be found through the slot after the type changes. The fixed-global reset
and baseline functions also do not enumerate every dynamic-pool instance, so
that orphan is not guaranteed to restore or forget its direct target.

The ordering is explicit in the asynchronous state machine. At phase 6,
`filesystem_loadInstrument_tick()` calls `instrumentManager_resetSlot()` on the
resident Scene slot before opening and parsing the selected file. Runtime type,
render, trigger, and LFO lookup all consult that same live Scene slot. Only
after parsing completes does Menu call `preset_startInstrumentApply()`, which
queues Morph/runtime work for the already-replaced identity. There is no call
between those points that captures the outgoing runtime pointer and clears both
of its modulation targets.

This is also an atomicity problem independent of the current tail: audio can
observe the new type while its runtime instance still contains initialization
or state from an earlier use. Instrument parsing should stage the replacement
away from live SceneData, then a commit operation should clear outgoing owners,
swap retained identity/images, initialize/apply incoming runtime state, and
rebind cross-slot targets in a defined order.

### Rapid input and overlapping request analysis

The intended UI path does not queue every encoder detent. Once
`menu_instrumentLoadRequestCurrent()` successfully posts a request, it sets
`menu_storageBusy`. `menu_parseEncoder()` then drops subsequent encoder input
until both filesystem completion and the one-slot Morph apply have finished.
The filesystem independently rejects `filesystem_start()` while its status is
busy. Under this normal path, rapidly scrolling instruments starts the first
load only; later movement during that load is ignored rather than queued.

VOICE buttons are not gated the same way. `buttonHandler_voiceSelect()` calls
`menu_loadInstrumentVoicePressed()` even while `menu_storageBusy` is set. That
function changes Menu's selected destination slot/type and active voice, but it
does not itself post another file request. The in-flight filesystem operation
keeps its own captured destination coordinates, and Preset completion normally
uses the coordinates captured when that operation was posted. Therefore merely
pressing another VOICE button during a load should change browser/LED context,
not redirect the file currently being parsed. It is still unsafe interaction
design because the displayed destination can disagree with the operation that
is finishing, and pressing the originally selected voice can preview the live
slot while asynchronous parsing has already reset its type but has not applied
its parameters.

There is a latent coordinate-overwrite defect if any caller does attempt a
second Instrument request while the first is busy. `preset_loadInstrument()`
calls `filesystem_ack()` and then overwrites `pm_instrument_request_scene`,
`pm_instrument_request_slot`, type, and index before checking whether
`filesystem_requestLoadInstrument()` accepted the request. A busy filesystem
rejects the second operation, but the first operation and its generic completion
callback remain alive. When that first callback fires, Menu can use the
overwritten Preset Scene/slot and run one-slot apply against a different slot
from the one the filesystem actually populated.

Current encoder gating makes that failed-second-request sequence difficult to
reach through ordinary scrolling, but the Preset API itself is not safe and a
button/deferred/future caller can expose it. Request coordinates must be
committed only after filesystem acceptance, or completion must receive an
immutable per-operation token. Menu should also freeze destination Scene/slot
selection and preview for the full read-plus-apply transaction, or explicitly
queue one latest desired selection after completion.

Preserve the current bad state until these values are recorded:

1. Voice 2 instrument type.
2. Voice 2 `2vm` Morph amount.
3. Both LFO amounts, destination voices, and destination parameters for Voice
   3 through Voice 6. Voice 1 and Voice 2 can also be recorded, but `slakd2`
   proves Voice 2's loaded pair starts off. A displayed `off` does not yet prove
   the runtime target is clear because mismatched loaded pairs normalize only
   for display.
4. Voice 2 velocity amount and destination.
5. Whether the tail occurs from a stopped manual Voice 2 preview, from a
   sequenced Voice 2 step, or both.

Minimal isolation sequence that preserves the current kit/instrument state:

1. Stop the sequencer and trigger Voice 2 manually. Record whether the long tail
   is still present. This separates step automation from continuous/runtime
   writers without changing Scene data.
2. Record both LFO target pairs for Voice 3 through Voice 6 before editing
   anything, including pairs displayed as off.
3. Set the suspected source destination parameter explicitly to Off, then edit
   Voice 2 Decay to a clearly short value and retrigger manually.
4. Do not use LFO amount zero as the sole isolation. A node with amount zero can
   remain installed and continue restoring/writing its captured baseline; only
   setting the destination Off exercises target restoration and clearing.
5. If the tail remains, set Voice 2 `2vm` to zero and retrigger manually.
6. If the manual preview is normal but sequenced playback is not, inspect or
   clear both automation lanes on the offending Voice 2 step. Avoid running
   additional automated steps until their destination values are recorded,
   because the legacy backend has an out-of-bounds-read risk.
7. If still stuck, set any velocity destination targeting Voice 2 Decay to Off
   and retrigger.
8. Load a known neutral instrument into Voice 2 only as the final test because
   that destroys the most useful live evidence.

A firmware diagnostic that prints, for one canonical parameter ID, the main
endpoint, Morph endpoint, interpolation value, shaped runtime field, and every
installed source node would make this class of bug directly observable.

## Required Fix Direction

### 1. Give InstrumentManager complete modulation lifecycle ownership

InstrumentManager already owns current slot-to-runtime resolution. Add owner
operations that iterate the six current runtime LFOs for:

- per-block target restore/reset;
- direct baseline/range refresh;
- source-target clear before type replacement.

`modulationNode.c` should not hardcode fixed Drum/Snare/Cymbal/HiHat globals
once runtime type assignment is dynamic.

### 2. Rebind all target relationships after a slot type change

Before replacing a slot, restore and clear the outgoing source LFO/velocity
targets. After the new type and descriptor images are committed:

- normalize the loaded source's target voice/parameter pairs;
- reinstall both LFO target pairs and velocity target for that source;
- revisit all other source slots whose stored canonical target addresses the
  changed destination slot;
- clear targets that are invalid for the destination's new descriptor table.

This must be one explicit InstrumentManager/Preset lifecycle operation rather
than incidental descriptor-order side effects in `preset_applyKitVoice()`.

The pair normalizer must treat `lfo_target_voice` and `lfo_target_param` as one
atomic stored relationship. It must either rebuild the canonical parameter for
the selected voice when the local descriptor is valid there, or set the pair to
Off. Menu display normalization is not an adequate substitute for load-time
storage and runtime reconciliation.

The rebind cannot be limited to the loaded slot. Instrument Load completion
must inspect all six source slots because any LFO or velocity destination can
address the replaced target slot. It must also clear the outgoing source node
using the old runtime identity captured before SceneData type mutation; looking
the source up after the type swap is too late.

### 3. Make one-Instrument load a staged transaction

Add an isolated `kit_instrument_slot_t` staging record plus staged display name
to the filesystem Instrument operation, parallel to `op_staged_kit`. Parse and
validate the complete root Instrument file there without changing resident
SceneData. On failure, discard staging and leave both retained and runtime state
untouched.

After successful parsing, Preset/InstrumentManager must perform one ordered
commit while holding the captured Scene/slot coordinates:

1. capture and clear the outgoing slot's source LFO/velocity nodes through its
   old runtime identity;
2. restore/clear every other source whose installed destination addresses the
   target slot;
3. copy the staged slot and display name into SceneData;
4. initialize or reset the incoming runtime instance so pool reuse cannot carry
   envelope/LFO state from an earlier assignment;
5. apply supplemental cells and the complete Morph/runtime image;
6. normalize and reinstall the loaded source's target pairs;
7. validate and reinstall all cross-slot sources against the new descriptor
   table;
8. release Menu input only when the transaction is complete.

Parsing cannot mutate the live Scene slot in phase 6 as it does now. This is
the key difference from Kit load, which already validates into `op_staged_kit`
before committing.

### 4. Make request ownership immutable

`preset_loadInstrument()` must not overwrite retained completion coordinates
until `filesystem_requestLoadInstrument()` accepts the request. Prefer one
immutable operation context shared from post through callback, or at minimum
stage the proposed Scene/slot/type/index in locals and publish them only after a
successful request return.

Menu's busy transaction must cover encoder, Scene, VOICE destination, nested
Load exit, and preview behavior. The simplest contract is to consume but ignore
those controls until apply completes. A more responsive alternative may retain
one latest desired browser/destination state, but it must post that request only
after the current transaction has committed and must never alter the current
completion coordinates.

### 5. Modulate in descriptor value space

Direct pointer modulation is only valid when the runtime field already uses the
same linear domain as the descriptor. Shaper-backed fields need an owner
adapter that:

1. starts from the current descriptor/Morph byte-domain value;
2. applies modulation within the descriptor dtype range;
3. calls a runtime writer that performs the normal parameter-specific shaping;
4. does not rewrite retained Scene endpoints or recursively refresh the
   modulation baseline on every LFO block.

Amplitude and pitch envelope time parameters should not expose their internal
decrement floats as generic `0.0..1.0` modulation targets.

### 6. Validate storage values by descriptor contract

After parsing numeric text, validate or clamp according to descriptor dtype and
binding kind. At minimum:

- reject/clamp `DTYPE_0B127` above 127;
- validate menu/enumeration ranges;
- validate canonical target IDs against the selected target voice/type;
- preserve `INSTRUMENT_PARAM_INVALID` only for target selector fields;
- apply the same checks to `[params]` and `[morph]`.

### 7. Add regression coverage

Required cases:

- load a Cymbal/Snare/HiHat into Voice 2 with an active negative LFO targeting
  Voice 2 Drum decay;
- edit Voice 2 Decay while modulation is active and after amount is set to zero;
- swap source and target slot types while a cross-voice target is installed;
- verify all current dynamic LFO nodes are reset and receive baseline updates;
- reject or clamp a file decay value of 128 and 255;
- verify Morph 0/255 behavior separately from modulation;
- verify no outgoing runtime node retains a live target after Instrument Load.
- rapidly spin the encoder during one Instrument read/apply and verify only one
  immutable destination is committed;
- press every VOICE and Scene selector during a pending load and verify neither
  filesystem nor completion coordinates change;
- request a second load directly while busy and verify rejection does not alter
  the first request's completion context;
- swap a source slot's type while it targets another slot, then load only the
  target slot and verify no orphan node survives;
- verify a failed Instrument parse leaves resident SceneData and runtime audio
  unchanged.

### 8. Complete the Pattern automation migration

Choose and enforce one Step destination representation. Given the current Scene
and Instrument APIs, the coherent choice is canonical descriptor/Scene target
IDs plus `INSTRUMENT_PARAM_INVALID` for off. Playback must validate each target
against the active Scene and apply through Preset/InstrumentManager typed
writers, never through fake MIDI CC messages. Destination replacement needs an
owner-level base restore that cannot index a legacy 255-byte array with a
16-bit ID.

Instrument type replacement must also reconcile automation destinations whose
target slot is being replaced. Tests must cover off (`65535`), all six slot
ranges, Scene targets, a target invalidated by a type swap, and repeated
destination changes without any out-of-bounds access.

## Priority

This should be treated as a runtime correctness issue before implementing save.
Saving the current Scene/Kit state without fixing target normalization and
runtime lifecycle can persist a configuration whose displayed endpoints do not
describe its audible state. The first corrective slice should make Instrument
Load staged and transactional, clear the outgoing runtime identity before the
swap, and rebuild the complete six-source target graph after commit. That slice
must also make request coordinates immutable. The live Kit-versus-Instrument
result gives these changes priority over the other confirmed defects.

Load-time target-pair reconciliation and complete dynamic LFO reset/baseline
enumeration belong in that same transaction. The Pattern automation
representation break remains a separate high-severity memory-safety issue. The
descriptor-domain modulation adapter and storage validation should follow
because they independently permit stuck envelope behavior.
