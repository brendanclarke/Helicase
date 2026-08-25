# Extending AutoSave to a New Storage Type

Companion to `AUTOSAVE.md`. Living checklist for whoever next adds a
live-parameter owner to AutoSave, or defines Pattern's eventual autosave
format. Two patterns exist/are planned; use whichever matches what you're
adding.

---

## Pattern A — extend the existing `.hcprms` wire format

Use this when the new data belongs to Bank/Scene/Kit/Instrument and can fit
inside the already-defined per-Scene 1,920-byte section (this is Effect's
path).

1. Confirm the byte range is already reserved in `Autosave.h`. Effect's
   512-byte allocation and `AUTOSAVE_EFFECT_PARAMETERS_OFFSET` already
   exist for exactly this — do not move or resize existing offsets.
2. Implement the live-value side: replace the
   `autosave_getEffectParameter()` stub (or the equivalent stub for
   whatever's being added) with real reads from resident state, following
   the same descriptor-index pattern already used for Instrument Normal/
   Morph cells.
3. Implement the inverse: a payload-byte → resident-state apply function
   for the new range (see `AUTOSAVE_READ_PLAN.md` — this is new
   infrastructure being built for the boot reader; extend it here rather
   than creating a second one).
4. Add a paired marker function (`autosave_markEffectParameterDirty()` or
   equivalent), following the existing Bank/Scene/Kit/Instrument marker
   pattern — same atomic-OR discipline, same "identity/provenance stays
   HCNAMES-owned, only live values get marked" rule if the new type has a
   name.
5. Decide whether the new type needs a `.hcnames` row (see "Identity and
   `.hcnames`" below) before wiring up the boot reader's Case 1/2/3 logic
   for it.

## Pattern B — separate autosave file

Use this when the data doesn't fit the per-Scene wire model, or has a
fundamentally different write cadence/size (this is Pattern's path — it
autosaves to its own file(s), not inside `.hcprms1/2`).

1. Define an independent validation/generation/CRC scheme for the new
   file(s) — do not assume `.hcprms`'s A/B-record, 3,856-byte-mask
   machinery applies; it was sized and shaped for the Bank/Scene/Kit/
   Instrument model specifically.
2. Define how boot decides whether to trust the new file(s) at all — this
   is a new question, not an extension of `AUTOSAVE_READ_PLAN.md`'s
   Case 1/2/3 model, because that model is built entirely on name
   comparison against `.hcnames`, and Pattern has no `.hcnames` row and no
   name-based identity concept in the current 129-row register.
3. Decide how a Pattern-autosave outcome interacts with the per-Scene
   Case 1/2/3 outcome for that same Scene's other components (e.g. can a
   Scene be Case-1-loadable for its Scene/Kit/Instrument identity while its
   Pattern independently succeeds or fails to restore?). Not decided here —
   flagging that it needs its own explicit answer when Pattern's format is
   defined, not an assumption inherited from this document.

---

## Identity and `.hcnames`

Whether a new type needs its own `.hcnames` row (and whether that row needs
more than `name<TAB>source`) depends on whether its numeric/`@` source is
ambiguous on its own:

- **Instrument** needed a third `type` field because it has **one library
  per type** (`Instrument/Drum/`, `Instrument/Snare/`, `Instrument/Cymbal/`,
  `Instrument/HiHat/`) — a bare numeric slot doesn't say which directory it
  belongs to.
- **Effect**, when it gets identity/provenance tracking, will **not** need
  a type field the same way. Effect has multiple types but only **one**
  library — the numeric/source slot alone already selects both identity
  and type, no disambiguation required. If Effect ever gets a `.hcnames`
  row, it stays two-field (`name<TAB>source`), same as Bank/Scene/Kit.
- Effect has **no `.hcnames` row at all today**. Open item for whoever
  implements live Effect parameters: decide whether Effect gets a row when
  provenance tracking is added, or continues without one (e.g. if Effect
  state is always considered part of its owning Scene with no independent
  save/load identity of its own).
- Pattern: no row exists or is currently planned; see Pattern B step 2
  above.

---

## `.hcnames` row-format precedent (for reference)

Rows are fixed `name<TAB>source\n`, except Instrument rows (`row = 33 +
scene*6 + voice`), which are `name<TAB>source<TAB>type\n` as of the
AutoSave boot reader work. This is the second time the row format has been
extended (source was added first). If a third extension is ever needed for
some other row class, follow the same shape: new field appended after a
tab, parser change scoped to only the row classes that need it, and a
one-line update to `FILESYSTEM_SPEC.md`'s row-format table so the doc never
drifts from the code.
