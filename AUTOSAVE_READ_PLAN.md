# AutoSave Boot Reader — Implementation & Test Plan (Rev 2)

Status: **pre-implementation review, incorporating your corrections.**
Rev 1 findings that you confirmed are compressed; this revision expands the
areas where your answers changed the design, and adds items your answers
surfaced that weren't in Rev 1.

---

## 0. What changed since Rev 2

- No migration/back-compat path for the `.hcnames` Instrument type field
  (§6) — this is a single-hardware development project, not a shipped
  product with cards in the field. The writer is simply updated and the
  one existing `.hcnames` file gets flushed/regenerated once. A malformed
  or absent type field on an Instrument row *after* that cutover is treated
  as corruption (fails the read), not a legacy case to tolerate.
- Confirmed no `.hcnames` type field is needed for Effect either, and for a
  different reason than Bank/Scene/Kit (which simply have no type concept):
  Effect will have multiple types but only one library, so its numeric
  source slot is already unambiguous. Captured in the new companion doc.
- Post-boot notice overlays are non-interruptible — fixed ~2s, auto-advance,
  no early-dismiss handling needed in the Menu-side sequencer.
- The `scene_isEmpty()` / Bank Save empty-Scene guard (§10) is explicitly
  **out of scope** for this effort — deferred to its own later cleanup/
  refactor sub-phase, positioned ahead of Pattern structure work. Removed
  from this plan's implementation order.
- Section 9 (Effect/Pattern extension notes) is now its own document,
  `AUTOSAVE_EXTENSION.md`, per your instruction — not a section of this
  plan. Summarized and linked below.

## 0b. What changed since Rev 1

- Implementation order now starts with the deferred dirty-mark/notice latch,
  validated against the whole-Bank fallback path specifically, because that
  path is testable without the (much larger) apply-side payload reader.
- Component-source precedence is now a confirmed rule, not an open question,
  and it's the opposite of what I'd defaulted to: **child wins whenever it
  has its own defined source; parent/inheritance only fills gaps where a
  child is `-`.** This has a real consequence — the autosave reader must not
  reuse the existing bundled Scene/Kit Load transactions for Case 2, because
  those transactions cascade-empty child sources as a side effect, which is
  exactly the behavior autosave must avoid.
- `.hcnames` needs a new field (Instrument type) — a change to an
  already-shipped file format, not just new reader logic. New section 6.
- Notification architecture is inverted: **boot never blocks.** Section 8
  rewritten around a post-boot overlay queue.
- New named principle (section 1) for the "never partially trust a Scene"
  rule, since you called it the most important mandate and I want it
  visible at the top, not buried in the Case 3 algorithm.
- Confirmed one adjacent gap you asked me to check: there is no
  `scene_isEmpty()`-style helper anywhere in the codebase, and Bank Save
  today is explicitly a "one-Scene bridge form" (its own code comment) that
  does not yet loop over all 16 Scenes. Section 10.
- Confirmed the cascade-empty-children convention **is** already
  implemented, at least for Bank Load and root Scene Load (cited in section
  5.2) — your description of "should happen" turned out to already be
  "does happen," at those two call sites.
- RAM ask revised from 3 bytes to **5 bytes**, plus a second, smaller,
  separate ask for Menu-side overlay-sequencing state. You said to expect
  this; section 7.3.

---

## 1. Principles (new — stated once, referenced everywhere else)

**P1 — All-or-nothing per-Scene trust.** The reader must never assemble a
Scene from a mix of proven and unproven components and load it silently.
If any required component of a Scene cannot be proven correct, the entire
Scene is invalidated and emptied — never partially loaded. Rationale (yours,
verbatim in spirit): a partially-correct Scene loaded silently becomes a
partially-correct Scene the user might subsequently Save, permanently
overwriting a valid stored Scene elsewhere with a Frankenstein of old and
wrong data. This is stricter than "best effort" and is deliberate.

A component is **proven** only when the autosave payload's own name for it
is trustworthy (present, and not covered by a set mutation bit — "mutation
bits in the name prove something incorrect happened during a component
load/save"). A component is **unproven** when either the name is missing
from the autosave entry, or its mutation bits are dirty, **or** it disagrees
with `.hcnames`.

Whether `.hcnames` happens to carry a concretely resolvable direct source
for that component is a *separate* question, and its absence is not itself
disqualifying: **an on-disk source is not required for a component to be
correct via autosave — it only matters once a fallback is actually needed.**
So the precise Case 3 trigger is a conjunction, not either condition alone:

> **Case 3 fires exactly when: (autosave's name for this component is
> unproven) AND (`.hcnames`'s resolved source for this component does not
> exist / cannot be loaded).**

This matches the Case 3 algorithm I had in Rev 1 already — I want to flag
that explicitly rather than imply I'm changing the algorithm, since what
you gave me is the rationale and the precise boundary condition, and both
confirm what was already sketched. What *does* change relative to Rev 1 is
precision about "proven": it's specifically about the autosave-side name
(missing/dirty/mismatched), not about the mere existence of a `.hcnames`
row.

**P2 — Autosave must never cascade-empty a child's source.** Standard
Load/Save already does this deliberately (a fresh Load at level L
legitimately supersedes everything beneath it, so children become `-`
/inherit). AutoSave restoring or repairing one level must not reproduce
that side effect, because a child that currently carries its own defined
(non-`-`) source is telling you it was independently loaded/saved at a
*different, more recent* location than its parent — exactly the kind of
information P1 depends on being able to trust. See section 5.2 for the
concrete implementation consequence.

---

## 2. Ground truth about the wire format (unchanged from Rev 1, compressed)

- Payload: 128B Bank + 16×1,920B Scenes (name 8B, settings 120B/40 live,
  Effect 512B/**0 live today**, Kit 1,280B: name 8B + 2 live params + 6×192B
  Instruments). Pattern has **no** wire allocation.
- `autosave_getLivePayloadByte()` has **no live branch** for Scene, Kit, or
  Instrument name bytes — only Bank name is live. Those names are written
  once at record creation and copy-forwarded unchanged on every ordinary
  drain. Confirmed by tracing every branch; matches the marker comments
  ("HCNAMES-owned name is deliberately excluded").
- `filesystem_resolveResidentSource()` already implements the `-`
  inheritance walk (Instrument → Kit → Scene → Bank) and was built, per its
  own comment, anticipating this reader. Reuse it for source resolution;
  don't reimplement it.

---

## 3. Root-level case (settings.cfg vs. last valid generation)

Unchanged from Rev 1's algorithm. Restating the sequencing decision from
your point 1: **this is implementation step 1**, together with the
deferred-mark latch (section 7), because the whole-Bank-fallback outcome
(numbers disagree → canonical Bank load → defer `autosave_markResidentBankDirty()`)
is fully testable without the apply-side payload reader existing yet. It
exercises the hardest new plumbing — boot reordering, tracking-enable
timing, deferred-mark replay — against the simplest possible case.

Still requires the `main.c` boot-order reorder described in Rev 1 (§2.2):
candidate validation/winner-selection must run before the Bank/Scene/Kit
scan-and-load ladder, not after.

---

## 4. `.hcnames` validity + regeneration

Unchanged from Rev 1 (§3), and now explicitly consistent with P1/P2: full
regeneration is the one case where cascading `-` into every non-Bank row
*is* correct, because regeneration is semantically "as if the Bank had just
been Loaded from the autosave record" — there's no pre-existing per-row
provenance to protect, unlike the partial per-Scene Case 2 path in section 5.

Regeneration remains strictly binary per Scene (load-whole via P1, or
empty-whole) — confirmed, no partial-component salvage during regeneration,
per your answer to the structural-asymmetry question in Rev 1 §3.2.

---

## 5. Per-Scene cases (`.hcnames` valid) — Cases 1/2/3

### 5.1 Confirmed: every row resolves independently, child wins over parent

For a Scene whose `.hcnames` is valid, each of the up to 8 identity rows
(Scene, Kit, 6 Instruments) is evaluated **completely independently**:

```
for each row (scene-own, kit, instrument[0..5]):
    if autosave name for this row is proven (present, matches .hcnames,
       no dirty name bits):
        this row's data comes from autosave — done for this row.
    else:
        resolve this row's source via filesystem_resolveResidentSource().
        if resolved source exists (file/slot present on disk):
            load ONLY this row's own data from that source (see 5.2 —
            never the bundled parent/child payload).
        else:
            Scene fails P1 -> invalidate and empty the WHOLE Scene,
            stop evaluating the other rows in this Scene.
```

A row whose own `.hcnames` source is `-` inherits upward through
`filesystem_resolveResidentSource()` exactly as documented; if that walk
lands on an ancestor's direct source, the row's data comes from *within*
that ancestor's bundled library entry (e.g. an Instrument inheriting a
Kit's direct source is loaded from that Kit-library slot's bundled
instrument-N file, not a standalone Instrument file) — `resolved_row` from
that function tells you which level actually supplied the source.

### 5.2 Consequence: Case 2 needs new, narrow, single-level loaders

This is the concrete implementation impact of P2 and your correction. The
*existing* Load transactions cascade by design — confirmed in code, not
assumed:

- `filesystem_cacheCurrentBankSceneNameBlock()` (Bank Load): sets a Scene
  row's source to inherit and forces its Kit row **and all 6 Instrument
  rows** to `FS_RESIDENT_SOURCE_INHERIT`, unconditionally, as part of
  committing one Bank child Scene.
- The root Scene Load handler (`FS_INTERNAL_OP_LOAD_SCENE`, `filesystem.c`
  ~9270-9292): identical pattern — sets the Scene row's source, then forces
  Kit row + all 6 Instrument rows to inherit.

If the autosave reader reused either of these transactions to satisfy a
Kit-level or Scene-level Case 2 event, it would silently flatten every
child row beneath it to `-`, destroying exactly the "this child was
independently sourced more recently" information P1/P2 depend on — even
though the reader only needed to repair *one* level.

**New requirement, not previously scoped:** single-level read paths that
pull *only* that level's own fields from a resolved source, without
touching (or requiring the presence of) child levels, and without writing
anything to `.hcnames` at all (see 5.3):

- Read-Scene-settings-only from a `Scene/NNN Name/sceneset.scg` (ignore its
  embedded Kit/Pattern/Effect).
- Read-Kit-fields-and-name-only from a `Kit/NNN Name/` folder (ignore its
  bundled 6 Instruments).
- Instrument load is already effectively single-level
  (`filesystem_requestLoadInstrument()` loads one Instrument given an
  explicit type — see 5.4), so this item is really only new work for the
  Scene and Kit levels.

### 5.3 Case 2 never writes `.hcnames`

Worth stating plainly since it wasn't obvious in Rev 1: a successful Case 2
component reload requires **no** `.hcnames` write. The row already
correctly recorded that component's true current source — that's precisely
why the reader trusted and resolved it. The reader's job is only to bring
resident SRAM into agreement with what `.hcnames` already says, then defer
an autosave-payload dirty-mark (section 7) so the *next* write captures it.
`.hcnames` itself needs no correction in this path.

### 5.4 Instrument type resolution in Case 2 (confirmed, see section 6)

Rev 1 flagged that a numeric/`@` Instrument source has no type information
of its own, and the autosave record's own type byte was the only fallback
authority, itself possibly untrustworthy. You've resolved this by requiring
`.hcnames` to carry type directly (section 6) — once that lands, Case 2 no
longer depends on the autosave payload's type byte at all for source
resolution; `.hcnames` is self-sufficient. The autosave payload's type byte
remains relevant only for Case 1 (interpreting an already-trusted autosave
Instrument record), where the existing pinned type-first/Choke-slot-6 rule
in `SCOPING_TARGETS.md` continues to apply unchanged.

---

## 6. New requirement: `.hcnames` needs an Instrument type field

This is a change to an already-shipped file format, not just new reader
logic — it needs its own implementation slice, separate from (and probably
before) the per-Scene Case 2 work in section 5, since 5.4 depends on it.

### 6.1 Precedent for the shape of the change, no precedent needed for migration

`FILESYSTEM_SPEC.md`, "Root resident-name register": rows are currently
fixed `name<TAB>source\n`, and the format has already been extended once
before — from name-only to name+source. That precedent shapes *how* to add
the field (append after a tab, scope the parser change to the row classes
that need it), but its backward-compatibility rule ("a legacy name-only
line remains readable as unknown") does not need to be reproduced here:
**there is no deployed install base to support.** This is single-hardware
development; the one `.hcnames` file that exists gets updated directly.

- Instrument rows (33..128 only) become `name<TAB>source<TAB>type\n`. Bank/
  Scene/Kit rows (0..32) stay two-field — confirmed no type concept needed
  there, and confirmed (see `AUTOSAVE_EXTENSION.md`) Effect won't need one
  either when it eventually gets identity tracking.
- No "legacy 2-field reads as unknown" tolerance. Once the writer is
  updated, an Instrument row with a missing or malformed type field is
  corruption, not a legacy case — it fails the read, same as any other
  malformed extended record ("malformed extended records fail the read
  rather than silently inheriting" already covers this; just apply it to
  the type field too).

### 6.2 Scope of the change, and the one-time cutover step

This is wider than the autosave reader itself: every existing writer of an
Instrument HCNAMES row needs to be updated to also write type, since type
is already known resident state (`kit_instrument_slot_t.type`) at every one
of those call sites — Instrument Load, Instrument Save, Kit Load/Save
(which cascades Instrument rows), Scene Load/Save, Bank Load, and the
bootstrap writer (`filesystem_writeResidentNamesBlocking()`). The line
parser/writer becomes row-class-aware (2-field vs. 3-field) rather than
uniform, which touches the shared 129-row read/write helpers directly.

Once the writer side lands, the current dev-card `.hcnames` needs one
explicit cutover so every Instrument row actually carries a type value
(otherwise every existing row fails the read under the new no-tolerance
rule the moment anything touches it). See Open Question A below for
whether this should be a manual file deletion (regenerate via the existing
bootstrap writer, which already knows current resident types) or a small
forced one-time rewrite path in firmware.

### 6.3 Documentation obligation

`FILESYSTEM_SPEC.md`'s "Root resident-name register" section needs the row
format table and prose updated to describe the row-class-conditional field
count, mirroring how the source field's own addition is documented there
today. This should land in the same session as the code change so the two
never drift apart, the way the existing text already keeps `.hcnames`'s
prior extension documented.

---

## 7. Deferred dirty-mark + post-boot notice latch (implementation step 1)

### 7.1 Revised content — 5 bytes, not 3

The notice-queue requirement (section 8) reuses most of this latch, but
needs to distinguish "needs a dirty-mark replay" from "needs a post-boot
notice," which are overlapping but not identical sets (every Case 3 Scene
needs both; a Case 2 Scene needs only the dirty-mark, no notice):

| Field | Size | Purpose |
|---|---|---|
| Bank-fallback flag | 1 byte | Set when the root case (§3) loads canonical/fallback instead of autosave. Doubles as the trigger for replaying `autosave_markResidentBankDirty()` **and** for queuing the one post-boot root notice. |
| Case-2 Scene mask | 2 bytes (16 bits) | Scenes needing `autosave_markSceneWithoutPatternDirty()` replayed. No notice. |
| Case-3 Scene mask | 2 bytes (16 bits) | Scenes needing `autosave_markSceneWithoutPatternDirty()` replayed **and** a post-boot "Scene N invalidated" notice. |

**Total: 5 bytes, static, normal SRAM1, boot-scratch lifetime.** Per
`MEMORY.md`'s RAM Allocation Approval Policy, this needs your explicit
sign-off (superseding Rev 1's 3-byte proposal). Cleared on every
reset/remount path, same rule as Rev 1 §5.3.

Over-marking (`autosave_markSceneWithoutPatternDirty()` on a Case-2 Scene
also re-marks that Scene's Kit and all 6 Instruments even if only one
sub-component changed) remains explicitly sanctioned by the existing
project policy ("over-marking is safe... under-marking is the bug"), so no
finer per-Kit/per-Instrument granularity is needed in the latch itself.

### 7.2 Replay timing (unchanged from Rev 1)

Applied once, immediately after `filesystem_ensureAutosaveFilesBlocking()`
enables tracking and before the first drain, then the dirty-mark portion of
the latch is cleared. The notice portion (section 8) persists a little
longer — until each queued notice has actually been displayed post-boot.

### 7.3 Second, smaller, separate ask: Menu-side overlay-sequencing state

Distinct from the latch above (which lives in the AutoSave/filesystem
layer and is boot-scratch), the post-boot notice *display* needs its own
small piece of runtime state in Menu — a cursor into the pending-notice
queue plus a deadline timestamp for the currently-showing overlay, on the
order of 3-4 bytes. This is ordinary transient UI runtime state (comparable
to what `menu_showStaleSettingsWarning()` already carries), not something I
think needs the same rigor as the boot-scratch latch, but I'm flagging it
now rather than adding it silently later, per your note to expect a
follow-up ask here.

---

## 8. Post-boot notification architecture (rewritten — no boot blocking)

### 8.1 The correction

Boot must never hold for these notices. The only existing precedent for a
fixed-duration hold (`timebase_holdPreAudioMs()`) is a bare busy-wait with
no watchdog feed, and — independent of that — a blocking architecture here
was simply the wrong call for a data-dependent (card-content-dependent),
potentially large number of notices. Corrected design:

- Boot **never** waits on notices. It only ever sets bits in the section 7
  latch and moves on; audio starts on schedule regardless of how many
  Scenes were invalidated.
- Once runtime is up (post-boot, tick-driven main loop active), a small
  Menu-layer sequencer drains the Case-3 mask (plus the Bank flag) one bit
  at a time, displaying each as a ~2-second overlay using the same
  non-blocking timer-comparison pattern already proven in
  `menu_showStaleSettingsWarning()` (poll `time_sysTick` across ticks, no
  busy-wait), rather than a new mechanism.
- No coalescing, per your instruction — up to 17 sequential overlays
  (1 root + 16 Scene) is acceptable as-is, since it should essentially
  never actually reach double digits in normal operation.
- **Non-interruptible**, confirmed: each overlay runs its full fixed ~2
  seconds and auto-advances to the next queued notice regardless of user
  input. Simplifies the Menu-side sequencer — no early-dismiss handling,
  no input-suppression-vs-passthrough decision to make during the overlay.

This eliminates the IWDG-interaction and boot-hang-history risk raised in
Rev 1 entirely — it no longer applies once nothing blocks pre-audio.

---

## 9. Future storage types — now `AUTOSAVE_EXTENSION.md`

Per your instruction this is its own companion document, not a section
here: **`AUTOSAVE_EXTENSION.md`**, intended to eventually live alongside
`AUTOSAVE.md` in `knowledge_files/specification_reference/`. Summary of its
contents:

- **Pattern A (extend the existing wire format)** — Effect's path. Effect
  already has its 512-byte reservation and a positioned
  `autosave_getEffectParameter()` stub; adding it later means filling in
  the live-getter, the apply-side inverse, and a paired marker, without
  moving the already-reserved offsets.
- **Pattern B (separate file)** — Pattern's path. Needs its own
  validation/generation/CRC concept, and — because Pattern has no
  `.hcnames` row — an entirely separate notion of "provable" than the
  name-comparison model this whole reader is built on. Not designed here.
- **Identity/`.hcnames` guidance**, generalizing what Instrument needed and
  why: a type field is only needed when a numeric source is ambiguous
  across multiple type-partitioned libraries. Instrument needed it (one
  library *per* type). Effect will not, even though it has multiple types,
  because it has only **one** library — confirmed by you, and captured
  there with the reasoning so it isn't re-litigated later. Also flags that
  Effect currently has no `.hcnames` row at all, as an open item for
  whoever adds Effect's live parameters.

---

## 10. Deferred: Bank Save empty-Scene overwrite guard

You asked me to check whether Bank Save already guards against writing an
empty/blank resident Scene over a non-empty on-disk Scene.

**Confirmed not implemented.** There is no `scene_isEmpty()` (or
equivalently-named) helper anywhere in `SceneData.h/.c` or `filesystem.c`.

**Additional context:** Bank Save today is explicitly a "one-Scene bridge
form" per its own code comment in `filesystem_saveBankDirectory_tick()` —
it does not yet loop over all 16 Scenes; it writes one child Scene
directory and explicitly does not delete/touch other "untoggled child
Scenes." So the full *multi-Scene* version of this risk isn't reachable yet
in the current implementation, though the single-Scene version already is,
for whichever Scene Bank Save currently writes.

**Scope decision (yours):** this is real motivation directly created by
Case 3 (which now systematically produces genuinely-empty resident Scenes),
but it is **explicitly out of scope for this reader effort**. Deferred to
its own later cleanup/refactor sub-phase, scheduled ahead of Pattern
structure work. Removed from this plan's implementation order (§14) and
from the consolidated new-code list (§11) accordingly — tracked here only
so it isn't lost.

---

## 11. New code required (consolidated)

1. **Payload-byte → resident-state apply function(s)** — inverse of
   `autosave_getLivePayloadByte()`, including type-first Instrument
   resolution (Choke slot-6 exception) for Case 1. Confirmed "not
   implemented, not to be inferred" per `AUTOSAVE.md`. Still the largest
   single new piece of code.
2. **Boot-time blocking candidate-validation/winner-selection**, distinct
   from the writer's runtime state machine, sharing only the low-level
   `autosave_stream*` helpers.
3. **`main.c` boot-order reorder** — candidate validation before the Bank/
   Scene/Kit scan-and-load ladder. Treat as its own reviewable unit given
   this project's boot-hang history.
4. **`.hcnames` row-class-aware parser/writer** (2-field vs. 3-field), plus
   updates to every existing Instrument-row writer, per section 6.
5. **Narrow single-level Scene-settings-only and Kit-fields-only readers**,
   distinct from the existing bundled Scene/Kit Load transactions, per
   section 5.2.
6. **`.hcnames` regeneration writer** for the present-but-mismatched case
   (section 4), distinct from `filesystem_writeResidentNamesBlocking()`.
7. **Deferred dirty-mark + notice latch** (5 bytes) and its replay/drain
   logic, section 7.
8. **Menu-side post-boot notice sequencer**, reusing the
   `menu_showStaleSettingsWarning()` non-blocking pattern, section 8 —
   non-interruptible, no early-dismiss handling needed.
9. **`FILESYSTEM_SPEC.md` HCNAMES row-format update** — documentation task,
   section 6.3.
10. **`AUTOSAVE_EXTENSION.md`** — already drafted as a companion document,
    section 9.

**Explicitly not part of this effort:** `scene_isEmpty()` / Bank Save
empty-Scene guard — deferred, section 10.

---

## 12. Reconciliation with `SCOPING_TARGETS.md`

Per your instruction, this stays a self-contained artifact and I'm not
editing `SCOPING_TARGETS.md`. The divergence from its pinned "Deferred
refactor target — AutoSave boot-load durability and read model" note is now
larger than in Rev 1, specifically:

- Pinned note: binary reader (valid record → restore everything; invalid →
  old ladder), whole-Bank granularity only. This plan: settings.cfg-vs-
  generation gate, `.hcnames` cross-validation, independent per-row
  (Scene/Kit/Instrument) granularity.
- Pinned note's deferred-fallback latch: "fallback type enum, destination
  Scene mask, 16-bit child mask... a few bytes total," implying one
  fallback event per boot. This plan: two independent 16-bit Scene masks
  (Case 2 / Case 3) plus a Bank flag, because multiple different outcomes
  can coexist across the 16 Scenes in a single boot.
- Pinned note is silent on `.hcnames` format changes and on notification
  UX. This plan requires both (sections 6 and 8).
- Pinned note doesn't anticipate Effect/Pattern extension documentation
  (section 9) or the Bank-Save empty-Scene question (section 10) — both
  surfaced only once per-Scene granularity was on the table.

Flagging per your instruction so you can decide how/when to fold this into
`SCOPING_TARGETS.md`'s subsequent phases, rather than me editing it now.

---

## 13. Open follow-up questions

Rev 2's questions A-F are resolved (§0). One new question arises directly
from your answer to B:

**A. Cutover mechanics for the existing dev-card `.hcnames`.** You said
"just update the writer, flush the file" — I want to pin down what that
means concretely so it's an actual implementation step rather than a manual
one-off I might forget to mention when this lands: (a) delete `/.hcnames`
on the dev card and let the existing bootstrap writer
(`filesystem_writeResidentNamesBlocking()`) regenerate it from scratch
(simplest, no new code, but the bootstrap writer currently writes **blank**
Scene/Kit/Instrument rows, not a full re-derivation of current resident
names — need to confirm that's acceptable for a one-time dev-card reset, or
whether current names would be lost this way and need re-establishing by
hand afterward); or (b) a small forced one-time full-rewrite path that
reads the current (2-field) file, keeps every name/source, and adds the
now-resolvable type for each Instrument row from current resident state.
(b) is more code for a one-card problem; (a) is zero new code but loses
current names. Which do you want?

---

## 14. Implementation order (revised)

1. **Deferred dirty-mark + notice latch (§7)**, wired to the whole-Bank
   fallback outcome only. Test: force settings.cfg/generation disagreement,
   confirm canonical Bank load happens, confirm
   `autosave_markResidentBankDirty()` replays correctly once tracking
   enables, confirm the next writer drain actually corrects the record.
2. **Root-level case + `main.c` boot reorder (§3)** — same test as above,
   now via the real gate logic instead of a forced condition, plus the
   agree-case passthrough (numbers agree → proceed to §4/§5 instead of
   short-circuiting).
3. **`.hcnames` format extension — Instrument type field (§6)** — can
   proceed in parallel with 1-2; needed before any Case 2 Instrument work.
4. **Apply-side payload reader (§11.1)** — largest single piece, unit-
   tested off the boot path against synthetic record buffers, including
   the Choke slot-6 exception.
5. **`.hcnames` regeneration, binary Case 1/Case 3 only (§4)** — depends on
   4.
6. **Per-Scene independent Case 1/2/3 with narrow single-level parsers
   (§5)** — depends on 3, 4, and the narrow parsers (§11.5).
7. **Post-boot notice sequencer (§8)** — depends on 1 (the latch already
   carries the notice queue content) and can otherwise proceed
   independently of 4-6.

(`scene_isEmpty()` / Bank Save guard is explicitly not part of this
sequence — §10.)

---

## 15. Test plan (additions to Rev 1; Rev 1's fixture list still applies)

- **HCNAMES back-compat:** a `.hcnames` containing a mix of 2-field
  (legacy) and 3-field Instrument rows; confirm 2-field rows read as
  type=unknown and don't crash the row-class-aware parser; confirm a
  malformed 3-field type token fails the read rather than defaulting.
- **No-cascade verification:** force a Kit-level Case 2 event where at
  least one of that Kit's 6 Instruments independently carries its own
  defined (non-`-`) source; confirm the Kit reload does **not** flatten
  that Instrument's `.hcnames` source to `-`, and that the Instrument is
  still independently evaluated on its own terms afterward.
- **Case 2 writes no `.hcnames`:** confirm a successful Case 2 component
  reload leaves every `.hcnames` row byte-for-byte unchanged; only the
  deferred dirty-mark latch and resident SRAM change.
- **Latch dual-purpose correctness:** confirm a Case-3 Scene both replays
  its dirty-mark AND produces exactly one post-boot notice; confirm a
  Case-2 Scene replays its dirty-mark but produces **no** notice.
- **No boot blocking, any Scene count invalidated:** measure actual
  elapsed pre-audio boot time with 0, 1, and 16 Scenes invalidated —
  confirm no measurable difference at the audio-start boundary in any case.
- **Post-boot sequencer under load:** trigger 16 Case-3 invalidations plus
  the root notice, confirm all 17 overlays eventually display without
  interfering with normal Menu/audio operation, per whatever you decide on
  Open Question D.
