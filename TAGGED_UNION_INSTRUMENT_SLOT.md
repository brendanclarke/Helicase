# Tagged-union instrument runtime slots

## Decision

Replace the present combination of fixed native engine globals and four
per-engine expansion pools with six tagged runtime slots.  A runtime slot owns
one active engine at a time.  It is deliberately reserved at **twice the size
of the largest current engine object**, rather than sized only to today’s
largest engine.

Current object sizes, from the linked image, are:

| Engine runtime type | Bytes |
| --- | ---: |
| `DrumVoice` | 588 |
| `HiHatVoice` | 484 |
| `CymbalVoice` | 476 |
| `SnareVoice` | 420 |

Therefore the explicit slot budget is `2 * sizeof(DrumVoice) = 1,176 B`, and
the six-slot allocation is **7,056 B**.  This is one larger per-slot payload,
not a double-buffer or permission to run two engines in one slot.  The current
old-and-new engines must never overlap in a slot; the existing quiet/retrigger
handoff remains the lifecycle rule.

The current ownership footprint is 13,188 B:

| Existing allocation to retire | Bytes |
| --- | ---: |
| `voiceArray[3]` | 1,764 |
| `snareVoice` | 420 |
| `cymbalVoice` | 476 |
| `hatVoice` | 484 |
| `runtime_drum_extra[3]` | 1,764 |
| `runtime_snare_slots[6]` | 2,520 |
| `runtime_cymbal_slots[6]` | 2,856 |
| `runtime_hihat_slots[6]` | 2,904 |
| **Total** | **13,188** |

The target is therefore a net reduction of **6,132 B**, before insignificant
tag/alignment changes.  `runtime_slot_type[6]` remains: it is the tag that
identifies which union member is currently valid, not an obsolete engine pool.

## Invariants and scope

1. A visible instrument slot has exactly one concrete runtime object.  Reads
   and writes must select the member that agrees with `runtime_slot_type[slot]`.
2. A type/parameter change may overwrite a slot only after the old amp
   envelope is quiet, or during the existing forced pre-trigger handoff.  No
   renderer, LFO, modulation node, MIDI handler, or deferred Scene operation
   may retain a pointer to the outgoing object after that point.
3. Before overwriting a member, outgoing LFO and velocity modulation targets
   must be restored/cleared.  Those targets can point into the outgoing member.
4. The reserve is a capacity ceiling for a *single* future engine.  An engine
   whose runtime state exceeds 1,176 B must fail a compile-time assertion and
   require an intentional capacity/budget review; it must not quietly add a
   new `[INSTRUMENT_SLOT_COUNT]` pool.
5. Scene parameter images stay independent of this design.  The existing
   descriptor-backed 64-parameter image remains persistent Scene data; this
   plan changes only live DSP engine-state storage.
6. Per-slot state that is not an engine object—`runtime_slot_type`, velocity
   modulators, installed-target metadata, descriptor-LFO adapters and related
   routing records—remains allocated.  It is not per-engine duplicate storage
   and must not be removed merely to meet this plan’s byte count.

## Target representation

Keep the union private to `InstrumentManager.c` unless a compile-time test has
a genuine need to see it.  Engine headers should expose their concrete types
and explicit-pointer DSP operations, but not reintroduce a global owner.

```c
#define INSTRUMENT_RUNTIME_SLOT_COUNT INSTRUMENT_SLOT_COUNT
#define INSTRUMENT_RUNTIME_CURRENT_MAX_BYTES sizeof(DrumVoice)
#define INSTRUMENT_RUNTIME_SLOT_BYTES \
    (2u * INSTRUMENT_RUNTIME_CURRENT_MAX_BYTES)

typedef union {
    DrumVoice drum;
    SnareVoice snare;
    CymbalVoice cymbal;
    HiHatVoice hihat;
    uint8_t reserved[INSTRUMENT_RUNTIME_SLOT_BYTES];
} InstrumentRuntimeSlot;

static InstrumentRuntimeSlot runtime_slots[INSTRUMENT_RUNTIME_SLOT_COUNT];
```

Add compile-time assertions for every engine member and for the resulting
union size.  They must prove that each current engine fits and that the union
is exactly the declared 1,176-byte reserve on the target compiler.  The typed
members give correct alignment; code must never cast the `reserved` bytes to an
engine type or use byte-storage aliasing to bypass the type tag.

`InstrumentManager` will provide private typed accessors such as
`instrumentManager_drumRuntime(slot)`.  Each returns the corresponding union
member for a valid slot.  Its callers must have selected the matching type in
the same dispatch operation.  `instrumentManager_runtimeInstance(slot)` stays
the descriptor/morph bridge and returns the active typed member as `void *`.
That preserves existing descriptor offsets while removing physical-slot
special cases.

## Implementation plan

### 1. Establish the bounded runtime owner

**Files:** `Core/DSP/Instruments/InstrumentManager.c` and
`Core/DSP/Instruments/InstrumentManager.h`.

Define the private tagged union, the fixed reserve constants, six
`runtime_slots`, and compile-time size assertions in the manager implementation.
Replace `runtime_drum_extra`, `runtime_snare_slots`, `runtime_cymbal_slots`,
and `runtime_hihat_slots` with this one array.  Change the four typed runtime
accessors so they address a union member at the supplied slot; none may select
a special native slot or an engine-specific fallback pool.

Inputs are a validated zero-based runtime slot and the type dispatch already
selected by the manager.  Outputs are a correctly typed live object pointer
or `NULL` for an invalid slot.  Affiliates are every manager render, trigger,
parameter, envelope, pan and LFO dispatch path, plus external callers that
currently assume an engine has a permanent global address.

Header comment text to add next to any exposed runtime access/initialization
contract:

> Each visible instrument slot owns one tagged, fixed-capacity DSP runtime
> object.  The active Scene type is mirrored by the manager’s runtime tag;
> callers must access state through the manager so an Instrument Load may move
> an engine type without creating per-type slot pools.

Implementation comment text to add next to the union:

> One union member is live in each slot.  The 1,176-byte reserve is twice the
> current largest engine state and is future-engine headroom, not a second
> concurrent engine.  `runtime_slot_type[slot]` selects the only member that
> may be read or rendered.  Adding an engine requires an explicit union member
> and compile-time fit assertion; no engine may allocate a six-slot pool.

### 2. Make initialization and replacement union-safe

**Files:** `InstrumentManager.c`, with contract comments in
`InstrumentManager.h`; inspect `Core/Bank/Scene/Preset/presetManager.c` and
`Core/Bank/Scene/Preset/presetMorphEngine.c` for transition callers.

Rewrite `instrumentManager_runtimeInit()` to initialize exactly one member per
slot from the boot Scene type.  The current implementation initializes several
engine pools for each slot; doing that to a union would overwrite prior members
and is exactly the obsolete allocation model this change removes.

Rewrite `instrumentManager_resetRuntimeSlot(slot)` as the sole union commit
point.  It must first ensure outgoing modulation references have been cleared,
clear the full union storage, copy the committed Scene type into
`runtime_slot_type[slot]`, initialize only that typed member, then return for
the existing descriptor/morph application to apply the incoming values.  The
type tag must not be published until the old member is no longer accessed, and
no code may run between tag selection and typed initialization that could
dispatch the new type against uninitialized state.

Inputs are a slot whose Scene assignment has committed and whose prior runtime
is quiet or about to be retriggered.  Outputs are a stopped, initialized new
engine with no inherited state.  Affiliates are
`instrumentManager_clearAllRuntimeModulationTargets()`,
`instrumentManager_ampEnvelopeQuiet()`, deferred Scene-slot application, the
pre-trigger apply path, descriptor writes, morph writes and render dispatch.

Implementation comment text to add at the reset function:

> This is the only operation that changes a slot’s union member.  Preset calls
> it only after the outgoing engine is quiet, or immediately before a forced
> retrigger.  Outgoing modulation destinations are restored before storage is
> cleared; the new tag and typed initializer are then installed atomically with
> respect to manager dispatch, after which descriptor and morph values rebuild
> the incoming runtime state.

### 3. Retire all eight old engine allocations and fixed-global APIs

**Files:**
`Core/DSP/Instruments/Drum/DrumVoice.c/.h`,
`Core/DSP/Instruments/Snare/Snare.c/.h`,
`Core/DSP/Instruments/Cymbal/CymbalVoice.c/.h`,
`Core/DSP/Instruments/HiHat/HiHat.c/.h`, and all callers found by a complete
source search.

Remove definitions and `extern` declarations for `voiceArray`, `snareVoice`,
`cymbalVoice` and `hatVoice`.  Retire their legacy fixed-slot wrapper APIs
when no caller remains.  The explicit-pointer engine APIs
(`*_initVoice`, `*_triggerVoice`, async/sync calculate, pan/phase setters)
are the retained engine boundary; they are already appropriate for union
members because they receive their state address explicitly.

Inputs are explicit typed pointers acquired by the manager.  Outputs are the
same engine DSP behavior, without hidden persistent storage.  Affiliates are
the manager dispatch, mixer, MIDI compatibility controls, LFO/modulation
helpers, engine startup calls, and any test code that mentions the globals.

Header comment text to add alongside retained pointer APIs:

> This operation acts on caller-owned runtime state.  InstrumentManager owns
> that state as one tagged union per visible slot, so this API must not depend
> on or recreate a fixed global engine instance.

Implementation comment text to add where a legacy wrapper is removed or a
remaining compatibility adapter is converted:

> Fixed engine globals were retired because an Instrument Load slot can host
> any engine type.  This path resolves the current slot runtime through
> InstrumentManager and passes the selected member explicitly, preventing a
> hidden per-engine allocation from surviving beside the tagged union.

### 4. Refactor direct fixed-global consumers

**Files:** `Core/MIDI/MidiParser.c` and
`Core/DSPAudio/modulationNode.c`; perform a final repository-wide `rg` audit
for all four retired symbols.

`MidiParser.c` contains many direct field writes to `voiceArray[0..2]`,
`snareVoice`, `cymbalVoice` and `hatVoice`.  Replace the hard-wired MIDI CC
mapping with manager-owned, slot/type-aware parameter application or a small
typed manager accessor layer.  Preserve the historical CC-to-visible-track
mapping where applicable, but make the operation a no-op or type-appropriate
dispatch when that visible slot now hosts another engine.  Do not preserve
globals merely for compatibility.

`modulationNode.c` has three legacy fan-out functions that walk all six old
LFO objects directly: `modNode_originalValueChanged`,
`modNode_directOriginalValueChanged`, and `modNode_resetTargets`.  Replace
these loops with a manager iterator/callback over the six current runtime LFO
nodes.  The iterator must use the runtime tag so exactly one LFO pair per
active slot is visited, including a drum/snare/cymbal/hat loaded into any slot.

Inputs are MIDI controls, a changed modulation target, or reset event.
Outputs are edits/restores applied to the active runtime object for each slot.
Affiliates are descriptor target routing, `velocityModulators`, LFO target
adapters, `instrumentManager_runtimeLfo()`, and all direct MIDI field updates.

Implementation comment text to add at the iterator boundary:

> Iterate the live LFOs through runtime tags rather than historical engine
> globals.  There is exactly one active engine—and therefore one LFO pair—per
> slot; walking fixed drum/snare/cymbal/hat objects would either miss a loaded
> engine or require retired duplicate storage.

### 5. Audit manager dispatch and remove obsolete special cases

**Files:** primarily `InstrumentManager.c`, and any callers identified by the
search in step 4.

Audit every `switch (instrumentManager_slotType(slot))`, all asynchronous and
synchronous render paths, `instrumentManager_runtimeInstance`, runtime pan,
amp-envelope, LFO, phase and trigger dispatch.  Convert any check such as
`slot < NUM_VOICES`, `slot == 3`, `slot == 4` or `slot == 5` that exists only
to choose a native global or expansion pool.  Retain true musical semantics:
the hi-hat’s open/closed behavior, including the track-7/open-hat mapping and
choke behavior, belongs to the active HiHat member and must not be inferred
from its historical fixed slot.

Inputs are current runtime type, visible slot and existing parameter/trigger
events.  Outputs are the exact engine-specific operation on the one selected
member.  Affiliates are the engine pointer APIs, Scene runtime type shadow,
descriptor offset metadata and audio mixer timing.

Compaction requirement: combine duplicated type-switch fan-outs only where a
single manager iterator/accessor makes ownership clearer.  Do not introduce a
generic untyped field-access API: engine-specific DSP behavior should remain
typed and compiler-checkable.

### 6. Future-instrument admission rule

**Files:** `InstrumentManager.c/.h`, new engine header/source, descriptors and
type registry whenever a future instrument is added.

A new instrument must add one typed union member and a compile-time
`sizeof(NewVoice) <= INSTRUMENT_RUNTIME_SLOT_BYTES` assertion.  It then adds
one case to the manager’s init, trigger, render, amp-envelope, LFO and
parameter-instance dispatch.  Its persistent parameters enter the existing
descriptor/Scene format; it adds no `[INSTRUMENT_SLOT_COUNT]` runtime array.

Inputs are the new type definition and its concrete runtime size.  Outputs are
a uniformly bounded loadable engine.  Affiliates are the instrument enum,
descriptor table, file/kit type mapping, engine pointer APIs and the manager
dispatch table.

Header comment text to add next to the capacity constant:

> `INSTRUMENT_RUNTIME_SLOT_BYTES` is the per-slot live-engine budget.  A new
> engine must fit this reserve and be represented as a tagged union member;
> increasing the budget changes all six slots and therefore requires an SRAM
> manifest update and explicit approval.

## Code-audited implementation ledger

This section supersedes the broad file lists above.  It was derived from the
current source tree, not from specification or planning material.  Line
references describe the source state at this audit; symbols and responsibilities
are the durable identifiers for implementation.

### Audit result: actual ownership and access points

`rg` finds 124 references to `voiceArray`, 57 to `snareVoice`, 59 to
`cymbalVoice`, and 60 to `hatVoice` in compiled C/header sources.  Apart from
their declaration, definition, and legacy engine wrappers, the only direct
external consumers are:

- `Core/MIDI/MidiParser.c`: the complete historical CC and CC2 instrument
  control map; and
- `Core/DSPAudio/modulationNode.c`: three LFO-node fan-out functions.

The mixer, LFO timing layer, Scene renderer, descriptor writer, and preset
type dispatch already use `InstrumentManager`; their relevant switches do not
reference the old allocations.  They still need regression testing, but they
are not independently rewritten merely because the underlying accessors change.

The existing manager has exactly these old allocation-selection sites:

| Current site | Current behavior that must disappear |
| --- | --- |
| `InstrumentManager.c:150-153` | Defines the four per-type expansion pools. |
| `InstrumentManager.c:170-198` | Chooses a native global for its historical slot and a pool element elsewhere. |
| `InstrumentManager.c:1227-1262` | Initializes four separate engine pools around the native globals at boot. |
| `InstrumentManager.c:1321-1354` | Resets a type-selected object; this becomes the union overwrite boundary. |

The special physical-slot checks in `instrumentManager_triggerTrack()` for
slot 6 / track 7 are *not* legacy allocation checks.  They encode the real
alternate trigger/choke behavior and remain, operating on whatever union
member is tagged as a HiHat.

### A. `Core/DSP/Instruments/InstrumentManager.c`: replace runtime ownership

**Exact code sites:** current declarations at 126-198; private typed accessors
at 170-198; `instrumentManager_runtimeInstance()` at 1187; runtime boot at
1227; `instrumentManager_resetRuntimeSlot()` at 1321.

**Change:** delete the four `runtime_*` arrays and replace both them and the
four fixed native objects with `static InstrumentRuntimeSlot
runtime_slots[INSTRUMENT_SLOT_COUNT]`.  Define the `InstrumentRuntimeSlot`
union in this translation unit with typed members for `DrumVoice`, `SnareVoice`,
`CymbalVoice`, and `HiHatVoice`, plus its 1,176-byte reserve.  Rewrite
`instrumentManager_drumRuntime`, `_snareRuntime`, `_cymbalRuntime`, and
`_hihatRuntime` to return the requested member of `runtime_slots[slot]` after
only slot-range validation; remove all `NUM_VOICES`, 3, 4, and 5 ownership
branching from these four helpers.

**Why it must exist:** these declarations are the cumulative allocation model.
Replacing only the four expansion pools would still retain the old native
objects and would not make runtime state bounded by one slot allocation.  The
typed accessors are the single existing funnel used by all manager render,
trigger, envelope, LFO, filter, transient, distortion, oscillator and
descriptor-offset paths, so changing them redirects that whole subsystem
without duplicate allocation.

**Inputs:** a validated zero-based slot, and an already-selected
`runtime_slot_type[slot]` in the calling manager dispatch.  **Outputs:** a
typed pointer to that slot’s union member, or `NULL` for an invalid slot.
**Affiliates:** `runtime_slot_type`, the four engine pointer APIs, every
manager `switch (instrumentManager_slotType(slot))`, descriptor `offsetof`
bindings, modulation target resolution, the mixer’s manager calls, and the
linked-image SRAM check.

**Required implementation detail:** add `_Static_assert`s that
`sizeof(DrumVoice)`, `sizeof(SnareVoice)`, `sizeof(CymbalVoice)`, and
`sizeof(HiHatVoice)` fit `INSTRUMENT_RUNTIME_SLOT_BYTES`, and that
`sizeof(InstrumentRuntimeSlot) == INSTRUMENT_RUNTIME_SLOT_BYTES`.  Define the
reserve from `2u * sizeof(DrumVoice)` so the present source/compiler determines
the first budget, but expose/record its resulting required value (1,176 B).
The byte reserve is capacity only; no code may treat it as a voice object.

**Comment text for the C declaration:**

> `runtime_slots` is the sole persistent owner of DSP engine state.  A slot
> contains exactly one live union member, selected by `runtime_slot_type`; the
> 1,176-byte member reserve is future-engine capacity, not a second active
> engine.  All historical fixed globals and per-engine slot pools are retired
> so adding a type cannot multiply SRAM by the number of engine families.

### B. `InstrumentManager.c`: initialize and replace only one union member

**Exact code sites:** `instrumentManager_runtimeInit()` at 1227-1262 and
`instrumentManager_resetRuntimeSlot()` at 1321-1354.

**Change:** make boot initialization iterate six slots, obtain the resident
active Scene type, zero that slot’s union, set its runtime tag, and call only
the matching `*_initVoice()` pointer API.  Remove the current four conditional
initializations (`slot >= NUM_VOICES`, `slot != 3`, `slot != 4`, `slot != 5`):
they initialize several different objects for one logical slot and cannot be
valid union behavior.

Make `instrumentManager_resetRuntimeSlot(slot)` the exclusive replacement
operation.  It will receive an incoming active-Scene type, clear one complete
union payload, install the tag, and initialize the matching member.  The
existing Preset code then applies routing, supplemental targets and morph
endpoint values as it does today.  Do not retain state by initializing only
selected fields: each engine’s `*_initVoice()` is the one authoritative
default-state routine.

**Why it must exist:** writing `SnareVoice`, `CymbalVoice`, and `HiHatVoice`
defaults consecutively into the same union would overwrite the prior object.
Conversely, a type replacement that does not clear the union can retain bytes
from an unrelated engine and makes uninitialized fields or accidental stale
reads non-deterministic.

**Inputs:** at boot, active Scene slot type; at replacement, a slot whose new
Scene type is committed and whose outgoing references have already been torn
down.  **Outputs:** one stopped, default-initialized engine in the selected
union member and a tag that agrees with it.  **Affiliates:**
`instrumentManager_runtimeInstance`, `instrumentManager_runtimeLfo`, the
quiet-envelope test, the Scene deferred worker, descriptor/morph application,
and every pointer-based engine initializer.

**Ordering requirement:** preserve the old tag until outgoing target teardown
has finished.  Within reset, clear storage first, set the new tag immediately
before the selected typed initializer, and perform no dispatch that could read
the new member until that initializer returns.  The main audio/control paths
are not to be re-entered halfway through this synchronous function.

**Comment text for reset:**

> This is the only union-member replacement point.  Its caller has already
> detached every outgoing modulation destination and waited for silence or is
> performing the trigger-time handoff.  The complete slot is cleared, the
> incoming tag selects one typed initializer, and Preset subsequently rebuilds
> descriptor, morph, routing and target state for that one new member.

### C. `main.c:dsp_init()`: retire fixed-engine boot ownership

**Exact code site:** `main.c:86-105`.

**Change:** remove calls to `initDrumVoice()`, `Snare_init()`, `Cymbal_init()`,
and `HiHat_init()`.  Keep `initRng()`, then call the rewritten
`instrumentManager_runtimeInit()` once to construct all six active slots.
Update the current comment, which says the manager initializes “non-native
per-slot runtime pools,” to describe all six tagged slots.

**Why it must exist:** the four engine init calls require the native global
objects to continue existing.  Leaving them makes the old allocation model
survive even after the manager uses unions, and constructs duplicate invisible
engines at boot.

**Inputs:** initialized RNG and boot-resident active Scene data.  **Outputs:**
all six tagged runtimes have one correctly seeded default engine before mixer,
parameter array and velocity modulator startup.  **Affiliates:** engine
initializers, `SceneData`, `mixer_init()`, `parameterArray_init()`, and
`modNode_init(&velocityModulators[i])`.

**Comment text:**

> Startup delegates all engine construction to InstrumentManager because it
> owns the six tagged slots.  No engine module owns a permanent native voice;
> the boot Scene type selects the single initialized member in each slot.

### D. Engine modules: remove global storage and obsolete fixed-slot wrappers

**Exact files and symbols:**

| File | Allocation to remove | Wrapper symbols to retire after callers move |
| --- | --- | --- |
| `Drum/DrumVoice.c/.h` | `voiceArray[NUM_VOICES]` | `initDrumVoice`, `Drum_trigger`, `calcDrumVoiceAsync`, `calcDrumVoiceSyncBlock`, `setPan`, `drum_setPhase` |
| `Snare/Snare.c/.h` | `snareVoice` | `Snare_init`, `Snare_trigger`, `Snare_calcAsync`, `Snare_calcSyncBlock`, `Snare_setPan` |
| `Cymbal/CymbalVoice.c/.h` | `cymbalVoice` | `Cymbal_init`, `Cymbal_trigger`, `Cymbal_calcAsync`, `Cymbal_calcSyncBlock`, `Cymbal_setPan` |
| `HiHat/HiHat.c/.h` | `hatVoice` | `HiHat_init`, `HiHat_trigger`, `HiHat_calcAsync`, `HiHat_calcSyncBlock`, `HiHat_setPan` |

**Change:** delete the four object definitions, all four `extern` declarations,
and the listed compatibility wrappers.  Retain only the explicit-instance APIs:
`*_initVoice`, `*_triggerVoice`, `*_calcAsyncVoice`,
`*_calcSyncBlockVoice`, and `*_setPanVoice`; retain
`Drum_setPhaseVoice` because its descriptor-special writer uses it.  Update
their headers/comments so they say the caller owns an explicit instance and
remove claims that wrappers or “dynamic pools” still exist.

**Why it must exist:** each wrapper reaches the old global, making it both an
allocation root and a route around the tag.  The explicit APIs are already the
complete engine-level abstraction needed by the manager; retaining duplicate
wrappers offers no current caller after `main.c` and MIDI are converted.

**Inputs:** typed pointer, logical source slot where trigger affiliation is
needed, and engine-specific control values.  **Outputs:** changes only to the
provided union member.  **Affiliates:** all manager dispatch functions;
`lfo_retrigger(source_slot)`; `velocityModulators[source_slot]`; special
descriptor writers for pan, phase, oscillator/filter/envelope/transient and
distortion; mixer rendering; and the MIDI conversion below.

**Comment text for every retained pointer API declaration:**

> The caller supplies the complete engine runtime object.  InstrumentManager
> owns that object as the active member of a tagged slot, so this function must
> neither assume nor recreate a fixed global voice instance.

### E. `Core/DSPAudio/modulationNode.c/.h` and `InstrumentManager.c/.h`:
iterate live LFO nodes instead of historical globals

**Exact code sites:** `modulationNode.c:363-383`
(`modNode_originalValueChanged`), 385-418
(`modNode_directOriginalValueChanged`), and 420-449
(`modNode_resetTargets`).  These functions currently visit three drum LFOs,
one snare, one cymbal, and one hihat directly, twice each for `modTarget` and
`modTarget2`.

**Change:** add a narrow manager-owned visitor API, for example
`instrumentManager_visitRuntimeLfoNodes(visitor, context)`, declared in
`InstrumentManager.h` using a forward declaration of `ModulationNode`.  Its
implementation walks slots 0..5, obtains `instrumentManager_runtimeLfo(slot)`,
and invokes the visitor exactly once for the live `modTarget`/`modTarget2`
pair.  In `modulationNode.c`, replace each twelve-global fan-out with a small
file-local callback plus that visitor.  Keep the separate `velocityModulators`
loop unchanged.  Remove the four engine header includes from
`modulationNode.c` and include `InstrumentManager.h` instead.

**Why it must exist:** these three functions would otherwise retain unresolved
references to the removed globals and, more importantly, omit an engine loaded
into a non-historical slot.  The visitor preserves encapsulation: ModulationNode
gets target nodes to refresh/restore, while only InstrumentManager knows the
union/tag layout.

**Inputs:** either a legacy `ParameterArray` id, a descriptor target/range, or
the start of an audio-block reset.  **Outputs:** each live source slot’s two
LFO destination nodes capture a new baseline or restore their prior baseline;
velocity nodes continue to behave exactly as before.  **Affiliates:**
`Lfo::modTarget`, `Lfo::modTarget2`, `instrumentManager_runtimeLfo`,
`instrumentManager_clearAllRuntimeModulationTargets`, LFO target adapters and
the beginning of `mixer_calcNextSampleBlock()`.

**Comment text for the public visitor:**

> Visit the two ModulationNode destinations owned by each currently tagged
> slot LFO.  The callback sees one live LFO pair per slot; it must not cache
> either pointer beyond the callback because a later Scene/type commit can
> replace that union member.

### F. `Core/MIDI/MidiParser.c`: remove all live writes through fixed globals

**Exact code sites:** `midiParser_ccHandler()` first CC switch at roughly
300-915 and CC2 switch at roughly 920-1155.  It contains every direct use of
the four retired globals, including the old pan wrapper calls.  `MidiParser.c`
currently includes all four engine headers but not `InstrumentManager.h`.

**Change:** retain the legacy CC numbers and their visible-track mapping, but
split the large historical switch into type-safe helper groups:

1. Drum CC/CC2 operations for slots 0..2: envelope slope/attack/decay, pitch
   envelope, oscillators and FM, waveform, filter/drive, transient, pan,
   volume, velocity mode and LFO fields.
2. Snare operations for legacy slot 3: oscillator/noise, envelope, mix,
   filter, transient, pan, distortion and LFO fields.
3. Cymbal operations for legacy slot 4: three oscillators/FM, envelope/filter,
   transient, pan, distortion and LFO fields.
4. HiHat operations for legacy slot 5: three oscillators/FM, closed/open decay,
   envelope/filter, transient, pan, distortion and LFO fields.

Each helper first asks the manager for the slot’s current runtime type and
instance (a new public, read-only `instrumentManager_runtimeType(slot)` is the
smallest necessary addition; `instrumentManager_runtimeInstance(slot)` already
returns the tagged object).  It casts only after the expected type matches and
returns without a write otherwise.  The helpers preserve the current direct
MIDI behavior—live mutation and `modNode_originalValueChanged(paramNr)` after
the CC—not a descriptor/Scene write.  Do not route these legacy CCs through
`instrumentManager_writeRuntime()`: that would change persistence, morph and
descriptor semantics instead of simply replacing memory ownership.

The caller-level switch should be compacted to select a legacy slot and helper,
with common CC1/CC2 operations shared where their engine field semantics are
identical.  It must retain independent mixer-only controls (`VOICE_DECIMATION`,
audio routing), velocity-modulator amount, MIDI note override, mute, NRPN, and
raw `parameter_values` target selector handling; those are not engine global
ownership and must not be discarded.

**Why it must exist:** this is the largest direct-global client.  Simply
replacing expressions with casts would dereference a union member when the
loaded type differs and corrupt state.  Type-checked helpers make the existing
legacy mapping deterministic: an old drum-only CC is a no-op when its visible
slot now contains a snare, rather than silently modifying a nonexistent
historical drum object.

**Inputs:** MIDI status group, CC or CC2 number, 0..127 value, legacy visible
slot, and currently tagged slot type.  **Outputs:** the matching active engine
field changes exactly as the old direct assignment did, or no change on a type
mismatch; existing original-value notification remains once per accepted MIDI
message.  **Affiliates:** `midiParser_originalCcValues`, `parameter_values`,
`modNode_originalValueChanged`, `velocityModulators`, `mixer_decimation_rate`,
`mixer_audioRouting`, `instrumentManager_runtimeType`,
`instrumentManager_runtimeInstance`, and retained engine struct declarations.

**Comment text at the helper boundary:**

> Legacy MIDI CC numbers address historical visible tracks, not permanent
> engine objects.  Resolve the current tagged runtime before applying a
> type-specific field write; a type mismatch is intentionally a no-op so MIDI
> cannot create or modify a retired hidden engine.

### G. `Core/Bank/Scene/Preset/presetManager.c`: detach old modulation before
deferred union overwrites

**Exact code sites:** `preset_applyKitVoice()` at 1055-1076 calls
`instrumentManager_resetRuntimeSlot()`; the Scene worker is armed by
`preset_startDrumsetApply()` at 1099-1108 and commits one quiet slot at
`preset_tickDrumsetApply()` 1110-1162 or one forced slot at
`preset_applyDeferredSceneSlotForTrigger()` 1164 onward.  The separate staged
Instrument-file replacement at 1353-1419 already calls
`instrumentManager_clearAllRuntimeModulationTargets()` before it changes
SceneData and resets a slot.

**Change:** when `preset_startDrumsetApply()` arms a newly selected Scene’s
six-slot deferred worker, call
`instrumentManager_clearAllRuntimeModulationTargets()` exactly once before any
pending slot may reach `preset_applyKitVoice()`.  Do not put a full-graph clear
inside `preset_applyKitVoice()` or `instrumentManager_resetRuntimeSlot()`:
those paths run one slot at a time and would repeatedly tear down destinations
that already-applied source slots have rebuilt.  Retain the existing clear in
the staged single-Instrument-file path.  Update the related comments in
`presetManager.c/.h` to state that the clear is mandatory before a tagged
runtime replacement, not merely before a dynamic-pool switch.

**Why it must exist:** a `ModulationNode` stores runtime pointers.  With old
separate pools, resetting one type pool did not overwrite another type’s
storage.  With a union, a pending Scene commit overwrites the exact bytes those
outgoing LFO/velocity nodes may still reference.  The existing full graph
clear is intentionally all-source, because any source may target the slot
being replaced.

**Inputs:** a newly active Scene, the current six runtime tags/LFO nodes, and
the deferred-worker pending mask.  **Outputs:** a detached old graph before
the first quiet or trigger-time union replacement, followed by normal
per-source rebuild as Preset applies incoming descriptor/morph values.
**Affiliates:** `instrumentManager_clearAllRuntimeModulationTargets`,
`instrumentManager_ampEnvelopeQuiet`, `instrumentManager_resetRuntimeSlot`,
`presetMorph_applyVoiceNow`, `preset_applyKitVoiceSupplemental`,
`MidiVoiceControl.c:161` (trigger-time handoff), and Scene selection callers
in `Menu/menu.c`.

**Comment text for `preset_startDrumsetApply()`:**

> Clear the outgoing all-source modulation graph before the deferred worker
> can overwrite any tagged slot.  The worker commits slots one at a time, so
> clearing at the per-slot reset point would erase destinations rebuilt by
> earlier slots; one pre-worker clear preserves pointer safety and bounded
> Scene application.

### H. `InstrumentManager.h`: public contract changes only

**Exact code sites:** runtime dispatcher declarations/comments at 409-467.

**Change:** revise the existing runtime-init and reset comments to remove
“non-native pool” and “preserved native globals” language.  Declare only the
minimal public additions required by the real external callers:

- `instrumentManager_runtimeType(uint8_t slot)` for MIDI’s safe
  type-before-cast decision; and
- the LFO-node visitor described in section E, using a forward declaration of
  `struct ModulatorStruct` rather than importing engine storage into the
  public header.

Keep typed union accessors private.  Keep `instrumentManager_runtimeInstance`
as the descriptor/morph address bridge; clarify that it returns a borrowed
pointer valid only while the slot tag is unchanged.

**Why it must exist:** MIDI and ModulationNode are the two external clients
that need dynamic ownership information.  Giving them per-engine global
substitutes would defeat the design; the narrow type query and visitor expose
only what is necessary.

**Inputs:** runtime slot and, for the visitor, a synchronous callback/context.
**Outputs:** current tag/borrowed node pair; no storage ownership transfers.
**Affiliates:** `MidiParser.c`, `modulationNode.c`, descriptor writes and all
private manager typed accessors.

**Comment text for `instrumentManager_runtimeInstance()`:**

> Returns a borrowed pointer to the member selected by the current runtime
> tag.  It is valid only for the immediate operation; callers must not retain
> it across a Scene/type commit, because the tagged slot may then be cleared
> and initialized as a different engine.

### I. Audited sites that require verification, not structural changes

These files contain no old storage reference and should not receive a
mechanical rewrite.  They are explicit regression targets because they depend
on the manager accessors whose backing storage changes:

| File/site | Current correct behavior to preserve |
| --- | --- |
| `Core/DSPAudio/mixer.c:505, 560-568` | LFO dispatch, filter/async update and sync rendering are already per slot through manager APIs; mixer owns routing, decimation and gain only. |
| `Core/DSPAudio/lfo.c:256-275` | Tempo-sync recalculation and trigger retrigger delegate to manager and therefore follow the runtime tag. Its historical-global mention is comment-only. |
| `Core/MIDI/MidiVoiceControl.c:161-162` | Performs the forced pending-Scene apply before `instrumentManager_triggerTrack`; this ordering is the valid retrigger-time union handoff. |
| `InstrumentManager.c:1418-1804, 2021-2050, 2660-2940` | Manager pan/render/trigger, filter/envelope/oscillator helpers, descriptor target resolution, special writes and generic offset writes already use the private typed accessors or `runtimeInstance`; they automatically address union members after sections A/B. |
| `Core/Bank/Scene/Preset/presetMorphEngine.c` | Does not hold engine globals; it writes through Preset/descriptor paths and must continue after reset initializes the incoming member. |

### J. Exact retirement proof

The post-change source audit must return no compiled-code occurrence of these
eight symbols:

```
voiceArray
snareVoice
cymbalVoice
hatVoice
runtime_drum_extra
runtime_snare_slots
runtime_cymbal_slots
runtime_hihat_slots
```

Documentation may mention their names only in this migration plan and SRAM
history.  The linked image must also have no symbols with those names.  This
is stricter than simply checking total SRAM: a leftover native object could be
small enough to hide in an otherwise favourable total, but would reintroduce
the unbounded per-engine ownership model.

## Required validation and acceptance criteria

## Execution notes

### 2026-07-25 — ownership, lifecycle, and direct-client conversion

- Replaced `voiceArray`, `snareVoice`, `cymbalVoice`, `hatVoice`, and all four
  `runtime_*` type pools with `runtime_slots[6]`, a 1,176-byte tagged union per
  slot.  Compile-time fit assertions cover all four current engine structs.
- Removed fixed-engine boot initialization and all fixed-wrapper APIs; engine
  modules now retain only explicit-instance operations used by the manager.
- Converted ModulationNode’s three historical twelve-node fan-outs to a
  synchronous InstrumentManager tagged-LFO visitor.
- Replaced the active legacy MIDI CC/CC2 fixed-object map with descriptor-key
  runtime writes.  It remains live-only; mixer/sequencer controls continue in
  their caller path.  The former direct-field switch is retained under `#if 0`
  temporarily as a controller-coverage reference and must be deleted after the
  final MIDI regression pass confirms every retained controller mapping.
- Added one full outgoing modulation-graph teardown when the deferred Scene
  worker is armed.  This closes the union-specific stale-pointer hazard before
  its first quiet or trigger-time slot replacement.

Next: compile the full image, remove any remaining compiled fixed-global
references, then measure the final linked SRAM symbols and update the manifest.

### 2026-07-25 — linked-image verification

- `make -j2` completed successfully. The toolchain retains its expected
  newlib `_close`/`_lseek`/`_read`/`_write` linker warnings only.
- `arm-none-eabi-nm -S --size-sort build/lxr02.elf` reports exactly one engine
  runtime owner: `runtime_slots.lto_priv.0`, `0x1b90` = **7,056 B**.
- The linked image has no `voiceArray`, `snareVoice`, `cymbalVoice`,
  `hatVoice`, `runtime_drum_extra`, `runtime_snare_slots`,
  `runtime_cymbal_slots`, or `runtime_hihat_slots` symbol.
- `SRAM_MANIFEST.md` now records 109,616 B total static writable RAM and the
  measured tagged-runtime replacement. The former direct MIDI switch remains
  compile-disabled only as a coverage reference; it owns no storage and emits
  no linked reference to any retired symbol.

1. Build the target image with the normal firmware build.  Compile-time
   assertions prove all current members fit and `sizeof(InstrumentRuntimeSlot)`
   is 1,176 B.
2. Inspect the linked image (`nm --print-size --size-sort` or equivalent).
   It must contain a single `runtime_slots` object of 7,056 B (subject only to
   symbol naming), and must contain **none** of:
   `voiceArray`, `snareVoice`, `cymbalVoice`, `hatVoice`,
   `runtime_drum_extra`, `runtime_snare_slots`, `runtime_cymbal_slots`, or
   `runtime_hihat_slots`.
3. Update `SRAM_MANIFEST.md` from the new linked image.  It must show the old
   13,188-B engine-state ownership replaced by the 7,056-B tagged allocation,
   with the expected approximately 6,132-B reduction clearly stated.
4. Run a repository-wide source search for the four retired global names and
   the four old pool names.  Only historical documentation/changelog text may
   remain; no compiled source may declare, define or access them.
5. Boot and load kits that place each current engine type into each legal slot.
   Confirm parameter edits, morph, velocity/LFO modulation, pan and rendering
   use the loaded type rather than the historical slot type.
6. Exercise deferred Scene changes while each engine rings, then trigger it;
   verify a quiet handoff and forced pre-trigger handoff both change type with
   no stale modulation or old-engine rendering.
7. Specifically test closed and open hi-hat, track-7 mapping and choke when a
   HiHat is loaded outside its former native slot, plus MIDI controls for
   drum/snare/cymbal/hat after the fixed-global MIDI path is removed.

## Non-goals

- No two-engine crossfade, double runtime bank or parallel rendering within a
  slot.
- No reduction of Scene parameter fidelity or descriptor storage.
- No unbounded per-engine pool reintroduction for a future instrument.
