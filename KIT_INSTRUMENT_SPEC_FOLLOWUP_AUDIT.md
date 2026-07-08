# Kit Instrument Spec — Follow-up Audit

## Status

This document is a plan only. No firmware code changes are made in this pass. It
follows on from `KIT_DIR_LOAD_AUDIT.md` (Phase 2 kit-directory loading, session 030)
and specifies the next architectural step: moving per-voice sound parameters out of
the shared flat `parameter_values[]` / `parameters2[]` arrays into per-instrument-type
definitions under `Core/Instrument/`, with a runtime indirection layer so any
instrument type can occupy any voice slot.

---

## 1. Audit of KIT_DIR_LOAD_AUDIT.md against the real code

This holds up well. Specific findings from cross-checking the audit against
`storageTypes.c/h`, `filesystem.c/h`, and `menu.c`:

- The parameter mapping table in `storageTypes.c` is not a guess — the audit's
  "2026-07-07 Parameter Mapping Audit" entry states every generated kit folder was
  simulated and diffed byte-for-byte against the source `Pxxx.SND` files with zero
  mismatches. Spot-checking the mapping against the real `PAR_` enum in
  `Core/Scene/Preset/ParameterArray.h` confirms the non-obvious cases, e.g.
  `PAR_NOISE_FREQ1`/`PAR_MIX1` correctly resolve to the snare voice's noise
  oscillator (voice slot 4), not drum1, despite the `1` suffix.
- The `storageTypes.c`/`filesystem.c` split is clean; the stated reason (`filesystem.c`
  was accumulating unrelated responsibilities) matches what's in the file today.
- The asyncfatfs directory-handle `->type` fix documented as a "Discovery Fix" is a
  real, hardware-found bug with a real fix — evidence this pass was tested, not just
  compiled.
- `menu.c` already routes sound-parameter writes through `preset_applySoundParameter()`
  rather than `frontPanel_sendData()` — Phase 1.2 (frontPanelParser removal, session
  028) is further along than the audit assumes in places.

One thing to flag explicitly rather than let pass silently: Step 6 of the audit
preserves the existing behavior of writing loaded instrument values into flat
`parameter_values[]`/`parameters2[]`. That was correct for a load-only first pass, but
it means the audit's data destination is exactly what this document proposes to
replace. Treat it as a known, temporary bridge, not something this follow-up needs to
unwind gently — Section 3 below replaces it directly.

---

## 2. Why a straight "split the array" refactor is not sufficient on its own

Before specifying the new layout, three real complications were found in the current
code that a naive per-type array split does not address by itself. Each is resolved
in Section 3; they're recorded here because they justify the shape of the design.

**Cross-voice modulation routing.** `PAR_VOICE_LFO{n}` / `PAR_TARGET_LFO{n}` are not
per-voice properties the way `PAR_VOL{n}` is. `Core/Menu/Cc2Text.c`'s `modTargets[]`
is a single flat, kit-wide, `uint16_t`-addressed table (205 entries today) that any
voice's LFO or velocity modulator can point into, resolved via
`voiceFromModTargValue()` and `ModTargetVoiceOffset`. A per-type C enum alias
(`#define`/`typedef`) cannot serve this purpose, because slot-to-type binding is a
runtime choice loaded from `kitset.kcg`, not a compile-time one. "Alias" has to mean
runtime indirection: each type owns its own parameter definitions, and a separate,
stable, kit-wide numbering gives every `(type, that type's own parameter)` pair a
canonical ID usable by modulation routing, step automation, and FX addressing alike.

**Voice-type is hardcoded outside parameter storage too.**
`preset_applyLfoModTarget()` in `Core/Scene/Preset/presetManager.c` (and the
equivalent `preset_applyVelocityModTarget()`) switches on a fixed slot→type mapping:

```c
switch (lfo) {
case 0: case 1: case 2: modNode_setDestination(&voiceArray[lfo].lfo.modTarget, value); break;
case 3: modNode_setDestination(&snareVoice.lfo.modTarget, value); break;
case 4: modNode_setDestination(&cymbalVoice.lfo.modTarget, value); break;
case 5: modNode_setDestination(&hatVoice.lfo.modTarget, value); break;
}
```

This is not the same problem as parameter storage layout, and splitting parameter
storage does not by itself fix these call sites. Scoped as a separate, later pass in
Section 6 rather than folded silently into this document.

**The morph engine assumes one flat array.** `preset_morphTick()` walks a single
cursor `morph_index` from `0` to `END_OF_SOUND_PARAMETERS`, one parameter per tick,
across the whole flat parameter space, and `preset_getMorphValue()` /
`preset_morphSendParameter()` index directly into `parameter_values[]`/`parameters2[]`
by that flat index. Once parameters live in per-type storage, morph needs a
two-dimensional cursor `(voice, within-voice index)` instead of one flat bound. This
is a real, direct dependency on this refactor, not a surprise for later — and it's
a clean side effect for per-voice morph amounts, since a voice-at-a-time cursor
already knows which voice it's on at every step, for free.

---

## 3. Agreed design

### 3.1 Alias semantics

"Alias" means runtime indirection, confirmed: what the user sees in the menu, what
the DSP voice reads, and what the save/load system operates on all depend on which
instrument type currently occupies a given voice slot. There is no compile-time
enum aliasing.

### 3.2 Canonical kit-wide parameter ID space

One registry, not two. Phase 3's step-automation pool and Phase 6's FX addressing
already assume a flat, kit-wide parameter ID space; per-instrument parameters use the
same numbering rather than a second, independently-evolving one.

Layout:

- **64 parameter IDs per voice slot**, fixed offset (`slot × 64`), 6 voice slots =
  384 IDs.
- **128 IDs reserved for FX/general** parameters, addressed the same way: each FX
  type gets its own parameter definitions (general module-range lineup, plus
  modulation/automation validity flags) exactly like an instrument type does.
- Total: 512 IDs. Current real counts (35/34/35/35 params across the four existing
  instrument types, ~209 total including kit-level extras) are nowhere near this, so
  there is no near-term pressure on the space.
- A fixed `slot × 64` offset (rather than a variable-length per-slot lookup) keeps
  slot-start resolution O(1) and matches the existing style of `ModTargetVoiceOffset`
  in `Cc2Text.c`.

### 3.3 Module ranges (categories)

The spec defines general module ranges within each type's own 64-ID block —
e.g. "parameters 0–12 relate to the oscillator", filter, envelope, LFO, mod, etc.
Beyond that, it is left to instrument authors to line up parameters across types in a
way that makes sense, and to the user to review whether an old mod-target/automation
assignment is still meaningful after an instrument swap. The system does not attempt
semantic preservation across swaps — only structural validity (Section 3.4).

### 3.4 Modulation and automation validity, invalidation

Each instrument type defines its own list of which of its parameters are valid
modulation targets and which are valid step-automation targets — two independent
flags per parameter, since the sets are not required to match.

- **Modulation (LFO/velocity) validity**: checked when an instrument is swapped into
  a slot. If the LFO's or velocity modulator's current target parameter is not a
  valid mod target for the newly-loaded type (or is out of range for it), the
  destination reverts to `PAR_NONE` — a completely acceptable, expected result. This
  is a structural bounds/flag check against the target instrument's own descriptor
  table, not a semantic "does the old target still make sense" solver.
- **Step-automation validity**: same treatment, a separate `is_stepAutomatable` flag
  per parameter, checked at swap time. This is a different validity index from
  modulation validity, because a parameter can be a legal automation target without
  being a legal LFO/velocity target or vice versa.
- **Why swap-time invalidation is sufficient**: this scenario is specifically the
  "Pattern reused across kits independently of the instruments it was recorded
  against" case. `FILESYSTEM_SPEC.md`'s `Pattern/` pool exists to allow exactly that
  kind of reuse; `Scene/` bundling exists to avoid the problem entirely by saving
  instruments and their automation/routing together as one atomic unit. Users who
  save/load `Scene/` files never hit this path. Users who mix-and-match from
  `Pattern/` are explicitly opting into the risk, and are warned about it; the
  runtime's job is to fail safely (invalidate + fall back to none), not to preserve
  meaning across an arbitrary type swap.

### 3.5 LFO and velocity destination as a generic bank

LFOs and velocity-modulator destinations are pulled out of the per-instrument
struct and treated as a fixed, always-6-slot, type-independent resource, rather than
a field owned by whichever instrument occupies that slot. Reasoning: an LFO's
`modTarget` already has to be able to address any voice's any parameter through the
shared `modTargets[]`-style table — it is inherently a cross-cutting router, not part
of the instrument's own sound the way an oscillator or filter setting is. Making it
a uniform bank sidesteps the alias question for LFOs and velocity destinations
entirely, rather than solving it per-type. `preset_applyVelocityModTarget()` and
`preset_applyLfoModTarget()` have the identical cross-voice-addressing shape today
(both write into `modTargets[]`-resolved destinations), so both get carved out the
same way, not just LFO.

This generic-bank treatment does not remove the dispatch in
`preset_applyLfoModTarget()`/`preset_applyVelocityModTarget()` — it collapses many
scattered hardcoded index-to-type switches (in `presetManager.c`, and likely
`menu.c`/`sequencer.c`/`mixer.c`, per Section 6) into one generic, data-driven
`switch (voiceType[slot])` pattern reused everywhere those addresses are needed. Every
one of those call sites still needs rewriting to use it; this is a real simplification
of the pattern, not a claim that no code changes elsewhere are needed.

**Instrument-provided defaults.** An instrument type's definition may optionally
supply a default LFO fill (the parameters necessary to populate an LFO slot when
the instrument is loaded) and/or a default velocity-modulation fill, applied at load
time. This is optional per type — if the type provides no such definition, the
corresponding LFO/velocity slot resets to its default/none state for that voice
rather than erroring.

### 3.6 Descriptor struct

Each instrument type's `<Type>Parameters.c/.h` defines an array of descriptors, one
per parameter the type owns, replacing what are currently three separately
hand-maintained tables that must stay in sync by convention: `storageTypes.c`'s
file-key map, `menu.c`/`MenuText.h`/`Cc2Text.c`'s `Name`/`ModTarg` display tables, and
the not-yet-built automation/modulation validity rules.

```c
typedef struct {
    const char    *file_key;         // e.g. "osc_wave" — storageTypes.c kitset/instrument key
    uint8_t        category;         // module range: OSC / FILTER / ENV / LFO / MOD / ...
    uint8_t        short_name;       // existing Name.shortName convention (menu.h Name struct)
    uint8_t        long_name;        // existing Name.longName convention
    uint8_t        default_value;
    uint8_t        min, max;         // menu clamp range
    bool           is_modulatable;   // valid LFO/velocity destination (Section 3.4)
    bool           is_stepAutomatable; // valid step-automation target (Section 3.4)
    uint8_t        dtype_category;   // see Section 5 — replaces the packed 4-bit menuId nibble
    const void     *dtype_list;      // pointer to this type's own value-list/name table, or NULL
} ParamDescriptor;
```

`min`/`max` and `default_value` were not explicitly requested but are needed
constantly by the real code today (menu clamping, `preset_resetKitToDefaults()`-style
logic) — including them means instrument authors do not have to hand-write clamp
logic separately per type.

The struct is intentionally close to a flat, human-readable table —
`{param_name, category, short_name, long_name, is_modulatable, is_stepAutomatable, ...}`
— so instrument authors can define a whole type's parameter set as one literal array
without cross-referencing separate menu/storage/routing tables by hand.

### 3.7 Directory layout

```
Core/Instrument/
├── Drum/
│   ├── DrumVoice.c/.h        ← moved from Core/DSPAudio/
│   └── DrumParameters.c/.h   ← new: ParamDescriptor table + dtype lists for Drum
├── Snare/
│   ├── SnareVoice.c/.h
│   └── SnareParameters.c/.h
├── Cymbal/
│   ├── CymbalVoice.c/.h      ← was Core/DSPAudio/CymbalVoice.c/.h
│   └── CymbalParameters.c/.h
└── HiHat/
    ├── HiHat.c/.h            ← was Core/DSPAudio/HiHat.c/.h
    └── HiHatParameters.c/.h
```

The mechanical relocation itself is low-risk: `storageTypes.c`'s existing parameter
maps are already organized as one set of named fields per instrument type and are
already validated against real `.SND` files (Section 1), so moving that data into
`<Type>Parameters.c/.h` is mostly relocation of already-correct tables, not a
redesign. The redesign risk is entirely in the addressing/routing/descriptor
mechanism above, not in "did we get the field list right."

Memory shape for "any type in any slot" needs one deliberate choice — a
tagged-union-per-slot sized for the largest instrument type, or a fixed per-type pool
— but is not a driving constraint either way: all six current voice structs together
measure ~2.9KB in DTCM per the real `.map` file, so either approach is cheap.

### 3.8 16-bit parameter index — open point, not specified now

Not being written into this plan. Recorded for future reference:

- `menu.h`'s `ModTarg.param` is already `uint16_t`; the general "parameter ID"
  concept elsewhere in the codebase was never actually constrained to 8 bits, so
  growing past 256 does not strain menu/routing code specifically.
- The actual cost is Phase 3's dynamic-event-pool automation entries, packed as
  9-bit param + 7-bit value in exactly 2 bytes. If the registry in 3.2 needs to grow
  past 512 entries, that entry either grows to 3 bytes (param ID gets full room,
  value stays 7-bit) or the value field shrinks (unattractive — 128 levels is already
  the coarse end for smooth automation). At 3 bytes, the realistic "every step has at
  least one automation" capacity drops from roughly 7,295 to roughly 4,863 entries —
  about a third less. This is only worth revisiting if/when instrument-type count
  growth pushes total parameters toward 512; today's ~209 leaves substantial headroom.

---

## 4. Code dive: current menu text/list rendering system

Requested to determine what it takes to make instrument descriptors (Section 3.6) the
canonical source for menu rendering, and to surface risks/affiliates in doing so.

### 4.1 What exists today

- `Core/Menu/menu.h` already defines `Name { shortName, category, longName }` and
  `ModTarg { nameIdx, param }` structs, plus `ModTargetVoiceOffset { start, end }`.
  These are effectively an early, partial version of the descriptor concept in 3.6,
  but scoped only to mod-target display, not general parameter definition.
- `Core/Menu/Cc2Text.c` holds `modTargets[]` — the single flat, 205-entry, kit-wide
  table described in Section 2 — plus `modTargetVoiceOffsets[6]` (fixed
  per-voice start/end ranges into that table) and `voiceFromModTargValue()`, which
  resolves a mod-target index back to its owning voice slot via a binary-search-style
  cascade of range comparisons.
- `Core/Menu/MenuText.h` holds ~15 separate `static const char xxxNames[][4]` tables
  (`transientNames`, `filterTypes`, `lfoWaveNames`, `waveformNames`, `ppqNames`,
  etc.), each self-describing its own length in slot `[0][0]`.
- Every sound parameter has one packed byte in `parameter_dtypes[NUM_PARAMS]`
  (`Core/Menu/menu.c`): the low nibble is `enum Datatypes` (`DTYPE_0B255` … 
  `DTYPE_0B15`, 15 values), the high nibble is a `menuId` (1–15, defined in
  `MenuText.h` as `MENU_FILTER` … `MENU_EXT_SYNC`) used only when the low nibble is
  `DTYPE_MENU`. `getMaxEntriesForMenu(menuId)` and `getMenuItemNameForValue(menuId, …)`
  in `menu.c` are plain `switch (menuId)` statements dispatching into the static
  tables in `MenuText.h`.

### 4.2 The existing precedent for a runtime-populated list

`MENU_WAVEFORM` is the one case in the current code that already needs exactly what
the user asked about — a list populated at runtime, not fully known at compile time
(the wavetable-oscillator-name-list example). It works like this:

```c
case MENU_WAVEFORM:
    return (uint8_t)((uint8_t)waveformNames[0][0] + menu_numSamples);
```

`menu_numSamples` is a module-global (`Core/Menu/menu.c`), set via
`menu_setNumSamples(n)` whenever the sample library is (re)scanned. Values below the
static `waveformNames[]` count resolve to a compiled-in name; values at or above it
fall through to `menu_formatSampleShortName()`, which synthesizes a `s01`…`sZ9`-style
label from the runtime sample index instead of a table lookup.

This confirms the pattern is workable, but it is a one-off special case
hand-written into the `switch`, not a generic mechanism — there's no way today to add
a second runtime-populated list (e.g. a per-instrument wavetable list) without adding
another hardcoded branch of the same shape.

### 4.3 Risk found: the menuId nibble is already at capacity

`MENU_FILTER` through `MENU_EXT_SYNC` in `MenuText.h` already number 1–15 — every
value a 4-bit nibble can hold besides 0. This is the single largest blocker to
folding `dtype_category`/`dtype_list` (Section 3.6) into the existing
`parameter_dtypes[]` packed-byte scheme as-is: there is no room left in the current
encoding to add new dtype/list categories, whether general or per-instrument, without
widening the field. This needs to be resolved as part of this refactor, not treated
as pre-existing headroom.

**Recommended resolution**: don't keep a single kit-wide `menuId` nibble at all.
`ParamDescriptor.dtype_category` (Section 3.6) becomes an enum scoped per instrument
type (or a small shared set of generic categories — numeric ranges, on/off, note
name — plus an `INSTRUMENT_LIST` category), and `ParamDescriptor.dtype_list` is a
direct pointer to that type's own list/name table (or a resolver function pointer,
for genuinely runtime-populated lists like a per-kit wavetable directory scan) rather
than an index into one shared, capacity-limited registry. This removes the ceiling
entirely, since each instrument type owns its own list pointers instead of competing
for slots in one global nibble.

### 4.4 Affiliates that need to change to make descriptors canonical

- **`MenuText.h`**: the ~15 static tables that are genuinely generic (filter types,
  LFO waveforms, sync rates, MIDI modes, PPQ, etc. — not instrument-specific) stay
  as shared lists referenced by `dtype_list`. Tables that are really per-instrument
  concerns (e.g. any list describing a specific voice's oscillator waveform set)
  move into that type's `<Type>Parameters.c/.h` instead.
- **`Cc2Text.c`**: `modTargets[]`/`modTargetVoiceOffsets[]`/`voiceFromModTargValue()`
  are superseded by the per-type `is_modulatable` flag (3.4/3.6) plus the kit-wide ID
  space (3.2) — a target's "which voice does this belong to" becomes `id / 64`
  instead of a fixed-range table walk, and "is this a legal target" becomes a
  descriptor-table lookup on whatever type currently owns that slot, instead of a
  static, compile-time-fixed list.
- **`menu.c`**: `getMaxEntriesForMenu()` / `getMenuItemNameForValue()` /
  `menu_displayModTargetFull()` / `menu_displayModTargetShort()` all need to resolve
  through "whatever instrument type is loaded in this voice slot's descriptor table"
  instead of a flat switch on a global id. `menu_numSamples` /
  `menu_setNumSamples()` / `menu_formatSampleShortName()` generalize into the
  `dtype_list`-as-resolver-function case described in 4.3, rather than remaining a
  wavetable-only special case.
- **`Core/Scene/Preset/presetManager.c`**: `preset_applyLfoModTarget()` and
  `preset_applyVelocityModTarget()` become the generic-bank, data-driven
  `switch (voiceType[slot])` described in Section 3.5, and `preset_morphTick()`
  needs the two-dimensional cursor described in Section 2.

This is scoped work, not a small edit — flagged explicitly as its own migration
step rather than something that falls out for free once `ParamDescriptor` exists.

---

## 5. Scope not covered by this document

Per Section 2, splitting parameter storage does not by itself fix every hardcoded
`switch (voiceSlot)`-style assumption elsewhere in the codebase. A dedicated later
audit — "find every place that assumes a fixed type occupies a fixed voice slot,"
covering at minimum `menu.c`, `sequencer.c`, and `mixer.c` beyond what Section 4.4
already lists for the menu/preset layer — is needed before slot-flexible instrument
swapping is fully realized. This document intentionally does not claim to deliver
that pass.

---

## 6. Summary of decisions locked in this pass

| Question | Decision |
|---|---|
| Alias meaning | Runtime indirection via per-type descriptor tables; not a compile-time C alias |
| Parameter ID space | One canonical, kit-wide, flat ID space shared by parameters, mod routing, step automation, and FX addressing |
| ID layout | 64 IDs/voice × 6 voices = 384, + 128 for FX/general = 512 total, fixed `slot × 64` offset |
| Mod-target/automation validity | Two independent per-parameter flags (`is_modulatable`, `is_stepAutomatable`) owned by each instrument type |
| Invalidation timing | Swap-time only; falls back to `PAR_NONE`/off. Not a semantic-preserving remap |
| Invalidation scope | Applies to `Pattern/`-pool reuse across kits; `Scene/` bundling avoids the issue by design |
| LFO / velocity destinations | Generic, always-6-slot, type-independent banks; instrument types may optionally supply load-time defaults |
| Descriptor struct | `ParamDescriptor` per Section 3.6, replacing `storageTypes.c` key map + menu display tables + (new) validity rules as three independently-synced tables |
| Directory layout | `Core/Instrument/<Type>/{<Type>Voice.c/.h, <Type>Parameters.c/.h}` |
| 16-bit parameter index | Explicitly deferred; not blocked by anything found; revisit only if total parameter count approaches 512 |
| Menu dtype/list encoding | The existing packed 4-bit `menuId` nibble is at capacity (15/15 used) and must be replaced by per-type `dtype_category`/`dtype_list` pointers, not extended in place |

---

## 7. Open questions for next session

1. `dtype_list` as a raw pointer vs. a small tagged union (`{static table}` vs.
   `{resolver function}`) — the wavetable-name case (Section 4.2/4.3) needs the
   latter; most others need only the former. Worth deciding the exact shape before
   `ParamDescriptor` is implemented, since it affects every instrument author's table.
2. Whether FX types (Section 3.2's 128-ID reserved block) are specified in this same
   pass or deferred to a Phase 6-scoped follow-up — this document assumes the same
   descriptor shape applies but does not enumerate any FX parameters.
3. Confirm the Section 5 "hardcoded voiceSlot switch" audit is tracked as a
   named follow-up item (not silently assumed to fall out of this refactor) before
   implementation begins.
