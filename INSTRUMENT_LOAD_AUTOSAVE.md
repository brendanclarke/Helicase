# Instrument Load AutoSave — Isolated Whole-Object Plan

## Status and scope

This is the first proposed whole-object AutoSave implementation after the
HCNAMES source extension.  It is deliberately limited to a successful normal
root Instrument Load.  It does not implement Kit/Scene/Bank marking, Pattern
or Effect persistence, Morph projection, AutoSave boot restore, or Load/Save
exclusion.

The goal is simple: once a root Instrument has successfully replaced one or
more resident Instrument destinations and its HCNAMES identity/source records
are durable, the AutoSave canonical mutation mask contains every implemented
payload coordinate needed to reproduce that Instrument replacement.

## Preconditions

- `HCNAMES_SOURCES_EXTENSION.md` is implemented far enough that an Instrument
  identity row has a durable direct-source (`@`) record and live name access is
  available without filesystem I/O.
- The AutoSave identity/completeness contract is documented: a name is an
  eight-byte atomic group; a set on-card name bit makes the whole group fall
  back to HCNAMES provenance during future boot restore.
- The current `D/S/A/V/M/C/P/T` lifecycle trace remains enabled for the
  hardware test build.  It must not change scheduler, mask, or facade policy.
- The user-approved 258-B HCNAMES source cache is recorded in the SRAM
  manifest before linking.
- No Bank Load/Save defect, recursive-delete repair, settings rewrite, or
  Load/Save exclusion change is included in the same pass.

## Current code boundary

`Autosave.c` already provides `autosave_markWholeInstrumentDirty(scene, slot)`.
Today it marks the three-byte type token plus live Normal and Morphable Morph
descriptor cells, but expressly excludes the HCNAMES-owned eight-byte name.
It is not called from the normal Instrument Load completion path.

`filesystem_requestLoadInstrument()` stages a candidate without mutating the
resident destination.  Preset/filesystem completion then commits the candidate
and publishes the selected identity row.  That final public completion—not
parse, staging, Menu preview, a `kit` temporary load, or an error path—is the
only legal AutoSave hook point.

## Required code changes

### 1. Add an atomic Instrument identity-group marker

Files: `Core/Bank/Scene/Autosave.c/.h`.

Add a helper equivalent to:

```c
void autosave_markInstrumentNameDirty(uint8_t scene_index, uint8_t slot);
```

It must:

- resolve the Instrument name payload base using the same shared mapping used
  by initial formatting and the live getter;
- mark all eight consecutive name bytes as one logical operation;
- honor mutation-tracking enable/range guards;
- never read HCNAMES, allocate, poll, or perform filesystem I/O.

Why: marking only changed characters would let a power interruption combine
part of a new name with part of an old AutoSave name.  Identity is a single
eight-byte value, so its mutation-mask semantics must be a group operation.

Add one composite helper, for example:

```c
void autosave_markWholeInstrumentLoadDirty(uint8_t scene_index, uint8_t slot);
```

It calls the existing whole-Instrument parameter/type marker and the new name
group marker.  Keep the existing `autosave_markWholeInstrumentDirty()` as the
name-excluding helper for future parameter-only copy semantics, or rename it
only in a separately reviewed mechanical pass.

Why: a normal root Instrument Load changes both payload and identity, whereas
future same-type copy/paste may intentionally change parameters without
renaming the destination.

### 2. Implement HCNAMES-backed AutoSave name capture

Files: `Core/Bank/Scene/Autosave.c/.h`; possibly narrow read-only accessors in
`filesystem.h/.c`.

Extend `autosave_getLivePayloadByte()` so the Instrument-name interval maps to
the correct HCNAMES identity row and returns the normalized character at the
requested byte.  This read must be RAM-only and must return unavailable for an
invalid/unpublished identity row.

Why: AutoSave currently formats initial names from HCNAMES but cannot capture
a later dirty Instrument name.  Without this getter, marking the name group
would only carry dirty bits forward and would never publish the new identity.

Use one shared mapping table/helper for:

- AutoSave Instrument payload coordinates;
- the HCNAMES logical row (Bank/Scene/Kit/Instrument);
- `autosave_markInstrumentNameDirty()`;
- the live payload getter; and
- the later boot reader.

Why: duplicated offset arithmetic is how an Instrument name can be captured
from the wrong Scene/slot while still passing ordinary CRC tests.

### 3. Hook only after durable public completion

Files: `Core/Bank/Scene/Preset/presetManager.c` and/or the one filesystem
completion continuation that owns the final normal Instrument Load result.

After the following are all true:

1. the validated candidate has committed to every requested resident
   destination;
2. the runtime image/apply sequence has accepted the committed payload;
3. the selected Instrument name and `@` source have been overlaid into the
   relevant HCNAMES rows; and
4. the HCNAMES rewrite has closed/synced successfully;

call `autosave_markWholeInstrumentLoadDirty(scene, slot)` once for every
actually committed destination Scene in the captured request mask.

Do not call it from:

- the asynchronous parser or candidate staging;
- Menu cursor movement, selection preview, or type-browser entry;
- the reversible hidden `kit` temporary load;
- a failed/cancelled request;
- an HCNAMES read/write error; or
- a Morph-only projection path.

Why: the dirty mask represents committed resident state.  Earlier calls can
capture staged bytes, a name that was never made durable, or a source whose
fallback chain does not exist after a power interruption.

### 4. Trace behavior for large group marks

The existing trace emits one `D` record per payload offset.  One full
Instrument can exceed the 64-record ring, so a group mark may intentionally
drop early `D` records.  Do not mistake that for writer failure.

For this first hook, retain the trace format and prove exact coverage by
decoding the final AutoSave mutation mask/payload fixture.  The trace must
still show the later `S -> A -> V -> M -> C -> P -> T` lifecycle in order.

Only if that evidence cannot isolate a failure may a logging-only aggregate
whole-object event be proposed.  It must add no permanent logging-off RAM and
must be separately reviewed; do not change the writer merely to make a trace
prettier.

## Expected mask coverage

For each committed `(scene, slot)`, mark exactly:

- 8 Instrument name bytes;
- 3 Instrument type-token bytes;
- one Normal endpoint byte for every descriptor owned by the committed type;
- one Morph endpoint byte for every owned descriptor marked Morphable.

Do not mark:

- Scene/Kit names or fields;
- sibling Instrument regions;
- Pattern or Effect regions;
- non-Morphable Morph padding, descriptor padding, or runtime-only DSP state;
- a source/provenance token (it belongs to HCNAMES, not the AutoSave payload).

The HCNAMES source record is already durable before this mask is armed.  The
AutoSave name bytes are its later snapshot/overlay, not a second provenance
owner.

## Hardware fixture and acceptance test

Use one ordinary root Instrument load into one selected Scene/slot.  Begin
with both hidden records valid and clean; preserve copied pre-test files.

1. Enter Instrument Load and select an Instrument whose stem, type, and at
   least one Normal/Morphable endpoint differ from the destination.
2. Complete the command, leave the Load page, and wait through the five-second
   debounce and writer completion.  Do not make scalar edits.
3. Copy `.hcprms1`, `.hcprms2`, `asavetrc.bin`, and `.hcnames` from the card.
4. Verify HCNAMES shows the selected name with direct `@` provenance for the
   destination row, while unrelated rows/sources are unchanged.
5. Verify the selected new AutoSave generation is exactly 34,768 bytes, has a
   valid CRC/commit marker, and contains the expected captured name/type/live
   endpoint values.
6. Verify every expected Instrument coordinate is clear in the committed
   mutation mask and that no unrelated payload coordinate changed.  A clean
   final mask is expected when no concurrent mutation occurred.
7. Verify the lifecycle trace reaches successful terminal `T` after
   `S/A/V/M/C/P`.  Record a dropped-record count if the `D` burst overflowed.
8. Repeat with a forced failed/cancelled load.  HCNAMES, AutoSave generation,
   and mutation mask must remain unchanged.

Regression checks:

- existing scalar Scene/Kit/Instrument/MIDI mutation behavior still drains;
- a clean idle period produces no hidden-file write;
- a root Instrument Load while AutoSave is off changes HCNAMES/payload but
  creates no AutoSave work; turning AutoSave back on follows its existing
  complete-resident convergence rule;
- an already open Load/Save page still suppresses new writer starts; this plan
  does not introduce the rejected active-transaction exclusion behavior.

## Follow-on order

Only after this fixture passes twice with preserved evidence may the next
isolated hook begin: Whole Kit Load, using the same durable HCNAMES-before-mask
boundary.  Whole Scene without Pattern follows that, selective Bank after it,
and Morph projection last.  Load/Save exclusion remains a separate final
project.
