# AutoSave Boot Reader — General Plan and Overview

Status: pre-implementation design, reconciled against the code as it stands
after Session 060 (Phases A/B/B2/C/D). Supersedes
`knowledge_files/drafts/AUTOSAVE_READ_PLAN.md`, which was designed before
the refreshed flag and source fields existed and independently invented a
mechanism (per-name mutation-bit inspection) that those two Phase B2/C
features now provide directly, in a durable and simpler form.
`knowledge_files/drafts/AUTOSAVE_EXTENSION.md` (Pattern/Effect extension
guidance) is unaffected by this rewrite and stays current.

This document is a general plan and overview, not a line-numbered
implementation schedule. When implementation actually starts, expand each
numbered work item below into an exact-code-site plan the way Session 060's
`S060PHASE_*.md` documents did, per the project's standing planning
practice.

---

## 0. Reader case structure (reference summary)

The reader's job is to decide, for each resident component, whether the
autosave winner record's payload is trustworthy for that component, and if
not, how to repair it. Every decision bottoms out in one of three cases.

### Root-level gate (§4)

Before per-Scene evaluation begins:

- **No valid winner**, or the winner's Bank slot disagrees with
  `settings.cfg`'s `active_bank`: proceed with the canonical
  `preset_loadBank()` exactly as today. Defer the dirty-mark through the §8
  latch so it replays once mutation tracking enables.
- **Valid winner whose Bank slot agrees with `active_bank`**: proceed to
  per-Scene Case 1/2/3 evaluation instead of the canonical Bank Load.

### `.hcnames` gate (§5.1)

`.hcnames` must be trustworthy before the per-Scene case logic means
anything:

- **Loaded successfully** (including via `.hcnamtmp` crash-recovery
  prelude): proceed to per-Scene evaluation.
- **Present-but-corrupt** while a valid winner exists: regenerate
  `.hcnames` from the winner record's own identity/source fields, then
  proceed. (Unimplemented, open work.)

### Per-Scene, per-row cases (§5.2)

For each of up to 8 identity rows per Scene (Scene-own, Kit, 6
Instruments), evaluate independently:

**Case 1 — Not refreshed (autosave proven caught-up).** The refreshed flag
(`FS_RESIDENT_SOURCE_REFRESHED_FLAG`, bit 13) is clear, meaning
`autosave_objectFullyCaptured()` confirmed every byte this component owns
in the canonical dirty mask was captured since its last load/save. Trust
the winner record's payload bytes for this row's range. Apply them to
resident SRAM via the payload-to-resident function (§10). Cross-check the
record's Phase C embedded source byte against `fs_resident_source[row]`'s
value bits — a mismatch despite a clear refreshed flag is evidence of a
bug; log it to `asavetrc.bin` if logging is on, but this remains Case 1:
autosave is valid, use the autosave payload and rewrite `.hcnames` with the
record's source.

**Case 2 — Refreshed, but source is resolvable on disk.** The refreshed
flag is set (autosave has not proven it captured this component), but
`filesystem_resolveResidentSource(row, &resolved_row)` yields a loadable
library source. Single-level reload of only this row's own fields from the
resolved source, using the narrow loaders in §7. No `.hcnames` write
needed — the row's source already correctly names where the data lives.
Set the deferred dirty-mark bit (§8 latch) so the next drain re-captures
it. The refreshed flag stays set (it already is) until that drain proves
the row clean through the existing Phase B2 convergence pipeline.

**Case 3 — Refreshed, source not resolvable.** The refreshed flag is set
and the resolved source does not exist on disk (deleted library slot,
corrupt path, etc.). P1 fires: invalidate and empty the **entire Scene**,
not just this one row. Stop evaluating other rows in this Scene. Queue a
post-boot notice (§9) for this Scene.

### Inheritance sub-cases

A row whose `.hcnames` source is `-` (INHERIT) inherits upward via
`filesystem_resolveResidentSource()`, which walks Instrument → Kit →
Scene → Bank. The `resolved_row` output tells the caller which ancestor
level actually supplied a direct source, so the §7 single-level loader
knows which physical object to open — e.g. an Instrument inheriting a
Kit's direct source loads from that Kit's bundled `instrument-N` member
file, not a standalone `Instrument/<type>/` file.

---

## 0.1 Writer and file-format freeze

**The autosave writer and all file formats are complete and frozen.** This
reader changes only what ends up in resident SRAM at boot. It does not
modify, extend, or reformat anything the writer produces — not the
`.hcprms1`/`.hcprms2` wire format, not the `.hcnames` row format (except
the §6 Instrument type-field extension and `#types` header row, which are
reader prerequisites added to the existing writer), not the A/B validation or winner-selection
logic, not the dirty mask, not the capture pipeline, not the drain
scheduling. If implementation reveals an apparent need to change what any
writer writes into any file, or to change any file writer's behavior beyond
scheduling order, that change requires explicit approval before
proceeding — it is not authorized by this plan.

---

## 1. What already exists (do not re-design this)

Everything below is implemented, hardware-verified to the extent noted in
`060_SESSION_HANDOFF_LOG.md`, and should be *used*, not rebuilt, by the
reader:

- **`.hcprms1`/`.hcprms2` A/B validation and winner selection** — full
  dual-record CRC32C streaming validation, Bank-identity matching, wrapping-
  generation comparison, A-wins-ties. Wire format is v1: one whole-record
  CRC32C, 34,768 bytes.
- **The continuation-cycle winner cache** (Phase A) — writer-side only, not
  reader-relevant, but establishes the pattern that "the writer knows the
  current winner's identity without re-validating" is an accepted
  optimization; the reader's own boot-time validation is a separate,
  one-time pass and does not reuse this cache.
- **`.hcnames` atomic safe-write** (Phase B) — every writer already goes
  through the crash-safe `.hcnamtmp` temp-file pattern with a boot recovery
  prelude. The reader adds a new *reader*, not a new writer, to this file;
  no changes needed here.
- **The refreshed flag** (Phase B2) — bit 13 of `fs_resident_source[]`
  (`FS_RESIDENT_SOURCE_REFRESHED_FLAG`), serialized as an optional third
  `.hcnames` column (`name<TAB>source<TAB>R\n`). This is loaded into RAM as
  part of the ordinary `.hcnames` read, **before** mutation tracking starts,
  and is therefore available at boot for exactly the purpose this reader
  needs: **it already is the "is this component's autosave data trustworthy"
  witness the original draft was trying to build from raw mutation-mask
  inspection.** See §5 — this is the central simplification this rewrite
  makes.
- **`autosave_objectFullyCaptured(hcnames_row)`** (`Autosave.c`, Phase B2) —
  maps one HCNAMES row (Bank/Scene 1-16/Kit 17-32/Instrument 33-128) to its
  wire interval in the canonical dirty mask and reports whether every byte
  in that interval is currently clean. This is the *runtime* half of the
  refreshed-flag lifecycle (it drives when the flag gets cleared post-drain)
  and is not directly needed by the boot reader itself, but its row-mapping
  arithmetic is the reference the reader's own component-to-payload-range
  logic must match.
- **HCNAMES source fields in the autosave record** (Phase C) — every Scene,
  Kit, and Instrument section of the payload carries a 2-byte little-endian
  copy of its own HCNAMES source, absorbed into existing reserved space with
  zero record growth. Available as a cross-check: the reader can compare a
  winner record's embedded source byte for a component against `.hcnames`'s
  live source column for the same row. Disagreement is a defense-in-depth
  signal (see §6), independent of and in addition to the refreshed flag.
- **`filesystem_resolveResidentSource(row, *resolved_row)`**
  (`filesystem.c:5322`) — already implements the `-`/INHERIT walk
  (Instrument -> Kit -> Scene -> Bank) exactly as this reader needs it, per
  its own comment ("AutoSave boot recovery must consult the same durable
  register semantics ... rather than recreating parent-offset arithmetic in
  a future reader"). Reuse directly; do not reimplement.
- **`autosave_getLivePayloadByte()` has no name-byte dispatch, by design**
  (confirmed and reaffirmed in Phase D, `S060PHASE_D_RE_DIRTY.md` §3) —
  Scene/Kit/Instrument name bytes in the autosave record are stale
  scaffolding from initial record creation. The reader must get every
  component's *name* from `.hcnames`, never from the autosave payload. This
  was already true before Session 060 and remains true.
- **Cascade-to-INHERIT is real, current behavior** — confirmed present at
  `filesystem_cacheCurrentBankSceneNameBlock()` (`filesystem.c:5693`, forces
  a Bank-loaded child Scene's Kit + all 6 Instrument rows to
  `FS_RESIDENT_SOURCE_INHERIT`) and in the root Scene Load handler
  (`filesystem.c` ~11130-11150, same pattern). **P2 below is still fully
  necessary** — this has not changed since the original draft.
- **Bank Save is no longer a "one-Scene bridge form"** — the original
  draft's §10 claim is now outdated. `filesystem_saveBankDirectory_tick()`
  is a per-child delete-then-write loop over all 16 Scenes (Session 057; see
  the "per-child" comments at `filesystem.c:24868` and surrounding). This
  matters for §11 below: the empty-Scene-overwrite risk the draft deferred
  in its §10 is now reachable across all 16 Scenes on every Bank Save, not
  just one bridge slot.

---

## 2. Principles (carried forward unchanged)

**P1 — All-or-nothing per-Scene trust.** The reader must never assemble a
Scene from a mix of proven and unproven components and load it silently. If
any required component of a Scene cannot be proven correct, the entire
Scene is invalidated and emptied — never partially loaded. A
partially-correct Scene loaded silently becomes a partially-correct Scene
the user might subsequently Save, permanently overwriting a valid stored
Scene elsewhere with a mix of old and wrong data.

**P2 — Autosave must never cascade-empty a child's source.** Standard
Load/Save cascades deliberately (a fresh Load at level L legitimately
supersedes everything beneath it). AutoSave restoring or repairing one level
must not reproduce that side effect: a child that currently carries its own
defined (non-`-`) source is telling you it was independently loaded/saved
at a different, more recent point than its parent — exactly the kind of
information P1 depends on being able to trust. Concrete consequence: the
reader's own component-level repair (Case 2, §5) must never call the
existing bundled Bank/Scene/Kit Load transactions, because those transactions
cascade by design (§1's confirmed cascade sites). It needs new, narrow,
single-level loaders — see §7.

---

---

## 4. Root-level case: settings.cfg vs. `.hcprms` winner generation

Unchanged in shape from the original draft. Today, boot order in `main.c` is:

```
preset_loadGlobals()                          -- settings.cfg, line ~560
filesystem_setAutosaveEnabled(...)             -- line ~567
...
filesystem_createBootIndexBlocking()           -- line ~727
preset_loadBank(boot_bank_slot, 0xffff)        -- line ~830  <- canonical Bank Load happens HERE, unconditionally
...
filesystem_ensureAutosaveFilesBlocking()       -- line ~950  <- .hcprms candidate validation/winner-select happens HERE, only for autosave's own bookkeeping, AFTER the Bank is already loaded from the library
filesystem_enableRuntimeSettingsWrites()       -- line ~966
```

This is the confirmed current state: **candidate validation runs after the
canonical Bank Load, not before it**, so today's boot always uses the
library-source path (the "Case 3" path in the original draft's terms) and
`.hcprms` winner selection only informs whether the writer needs to
re-mark the whole resident Bank dirty (Bank-identity-mismatch handling
already implemented and documented in `AUTOSAVE.md`).

**Required reorder:** move `.hcprms` candidate validation/winner selection
(a new *boot-blocking* variant, distinct from the runtime writer's state
machine, sharing only the low-level `autosave_stream*` helpers) to before
`preset_loadBank()`. Then:

- If no valid winner exists, or a valid winner exists but its Bank slot
  disagrees with `settings.cfg`'s `active_bank`: proceed with the existing
  canonical Bank Load exactly as today. Defer
  `autosave_markResidentBankDirty()` through the latch (§8) so it replays
  once tracking enables, rather than running immediately (immediately would
  no-op — mutation tracking is off until `ensureAutosaveFilesBlocking()`
  finishes, per the existing documented boot-load marker gap in
  `SCOPING_TARGETS.md` Session 052).
- If a valid winner exists and its Bank slot agrees with `active_bank`:
  proceed to §5's per-Scene evaluation instead of the canonical Bank Load.

This reorder is the largest single piece of new boot-sequencing risk in this
plan — treat it as its own reviewable unit, per this project's boot-hang
history (`SCOPING_TARGETS.md` has several prior boot-pacing/timeout
investigations).

---

## 5. `.hcnames` validity, then per-Scene Case 1/2/3

### 5.1 `.hcnames` itself must be trustworthy first

The refreshed-flag/source-field machinery in §5.2 only means anything if
`.hcnames` itself loaded successfully. `.hcnames`'s own read/recovery
(`filesystem_ensureAutosaveFiles_tick()` phases 15-19, Phase B) already
handles a crashed write via the `.hcnamtmp` recovery prelude — that part
needs no new work. What's new: if `.hcnames` is present-but-generically-
unreadable (not a temp-file recovery case, just genuinely absent or
corrupt) while a valid `.hcprms` winner exists, the reader needs a
regeneration path that rebuilds `.hcnames` wholesale from the winner
record's own identity fields (names + Phase C source fields) rather than
falling all the way back to a canonical library Bank Load. This
regeneration is safe regardless of whether the record's embedded source
bytes are truly correct and present on disk: source can only potentially
change resident data at boot, and on *this* boot the reader is populating
SRAM from the autosave payload, not from the library. It is then incumbent
on the user to re-save the Bank or its components, which resets sources in
any case. This is still open, unimplemented work.

### 5.2 Per-Scene, per-row evaluation — now driven by the refreshed flag

For a Scene whose `.hcnames` rows are valid, evaluate each of up to 8
identity rows (Scene, Kit, 6 Instruments) independently:

```
for each row (scene-own, kit, instrument[0..5]):
    refreshed = (fs_resident_source[row] & FS_RESIDENT_SOURCE_REFRESHED_FLAG) != 0
    if not refreshed:
        # Case 1: autosave already proven caught-up for this component.
        # Trust the winner record's payload bytes for this row's range.
        # Cross-check the record's Phase C source byte against
        # fs_resident_source[row]'s value bits -- a mismatch is evidence
        # of a bug: log it to asavetrc.bin if logging is on, but this is
        # still Case 1. Use the autosave payload and rewrite .hcnames
        # with the record's source.
    else:
        # unproven -- Case 2 or Case 3:
        resolved_source = filesystem_resolveResidentSource(row, &resolved_row)
        if resolved_source is loadable on disk:
            # Case 2: single-level reload of ONLY this row's own fields
            # from resolved_source (see 7). No .hcnames write needed --
            # the row's source already correctly names where this data
            # lives, which is exactly why the reader trusted it enough to
            # resolve. Set the deferred dirty-mark bit for this row (8) so
            # the next drain re-captures it; the refreshed flag stays set
            # (it is already set) until that drain proves the row clean.
        else:
            # Case 3: P1 fires. Invalidate and empty the WHOLE Scene.
            # Stop evaluating the other rows in this Scene. Queue a
            # post-boot notice (9) for this Scene.
```

A row whose own `.hcnames` source is `-` inherits upward exactly as
`filesystem_resolveResidentSource()` already implements; `resolved_row`
tells the caller which ancestor level actually supplied a direct source, so
the single-level loader (§7) knows which physical object to open (e.g. an
Instrument inheriting a Kit's direct source loads from that Kit's bundled
instrument-N member file, not a standalone `Instrument/<type>/` file).

### 5.3 Why the refreshed flag is a strictly better "proven" signal than the original draft's design

The original draft defined "proven" as: the autosave payload's name for a
component is present, matches `.hcnames`, and has no dirty mutation bits
covering it. That last condition cannot actually be implemented as
described — `autosave_getLivePayloadByte()` and every compound marker
deliberately never touch name-byte dirty state (§1, confirmed by Phase D).
The refreshed flag is a purpose-built replacement that is *more* correct
than what the draft was asking for: it does not merely say "the name looks
right," it says "every byte this component owns in the canonical dirty mask
— parameters, source field, everything — has been captured by autosave
since this object was last loaded/saved" (`autosave_objectFullyCaptured()`'s
exact definition). It is also already durable across boots (serialized in
`.hcnames`), whereas the draft's design would have needed the reader to
inspect the live in-RAM dirty mask, which does not exist yet at the point
in boot where this decision must be made (mutation tracking, and therefore
the dirty mask's meaningful state, does not start until
`ensureAutosaveFilesBlocking()` finishes near the end of the current boot
sequence — see §4).

### 5.4 Case 2 never writes `.hcnames`

Worth stating plainly: a successful Case 2 component reload requires no
`.hcnames` write. The row already correctly recorded that component's true
current source — that is precisely why the reader trusted and resolved it.
The reader's job is only to bring resident SRAM into agreement with what
`.hcnames` already says, then arm the deferred dirty-mark (§8) so the next
writer drain captures it and eventually clears the (already-set) refreshed
flag through the existing Phase B2 convergence pipeline.

---

## 6. `.hcnames` needs an Instrument type field (still open, unchanged need)

Confirmed still absent: the current row format is `name<TAB>source[<TAB>R]\n`
for every row class (`FILESYSTEM_SPEC.md`, "Root resident-name register").
Instrument rows still have no type of their own — a bare numeric/`@` source
does not say which of the four typed directories
(`Instrument/Drum|Snare|Cymbal|HiHat/`) it belongs to, and the autosave
payload's own type byte is not trustworthy as a substitute source of truth
for a component the reader has already decided is unproven (Case 2/3) —
using it would be circular.

Extend Instrument rows (33..128 only) to four fields:
`name<TAB>source<TAB>type<TAB>R\n`, with `type` and the trailing `R`-flag
both optional in the sense that a 2-field legacy line remains readable
(source-only) — but per this project's stated policy (single-hardware
development, no shipped install base), there is no need to *tolerate*
missing type indefinitely: once the writer side is updated, a missing or
malformed type field on an Instrument row is corruption and fails the read,
same as any other malformed extended record. Bank/Scene/Kit rows (0..32)
stay unchanged at up to 3 fields (`name<TAB>source[<TAB>R]`) — they have no
type concept, and per `AUTOSAVE_EXTENSION.md`, Effect will not need one
either when it eventually gets identity tracking (multiple types, one
library, no disambiguation needed).

This widens the row-class-aware parser/formatter
(`filesystem_cacheResidentRecord()` / `filesystem_formatResidentNameLine()`)
from the current 2-vs-3-field split to a 2/3-vs-4-field split, and requires
every existing Instrument-row writer (Instrument Load/Save, Kit Load/Save,
Scene Load/Save, Bank Load, the bootstrap writer) to also supply type, which
is already known resident state (`kit_instrument_slot_t.type`) at every one
of those call sites. `FILESYSTEM_SPEC.md`'s row-format table needs the
matching update, in the same change, per this project's standing practice
of never letting that document drift from the code.

**Cutover — resolved:** delete the existing `.hcnames` from the dev card
and allow the bootstrap writer to regenerate it on the next boot. No
migration code needed.

---

## 7. Case 2 component reloading (P2 consequence)

The reader's Case 2 repair must not reuse the bundled Bank/Scene/Kit Load
transactions (§2, P2) because they cascade child sources to `-`. The rule
is universal and simple: within a given Scene's Case 2 rows, if a child's
resolved source differs from its parent's resolved source, load the parent
first, then load the child over the top. This does not require a new
partial parser or a "settings-only" read mode — it uses the existing
full-object load paths but applied per-level without cascading `.hcnames`
writes:

- **Scene**: load from its resolved `Scene/NNN Name/` source using the
  existing Scene payload reader. Do not cascade Kit or Instrument sources.
- **Kit**: load from its resolved `Kit/NNN Name/` source (or the parent
  Scene's embedded Kit if inheriting) using the existing Kit payload
  reader. Do not cascade Instrument sources.
- **Instrument**: load is already effectively single-level
  (`filesystem_requestLoadInstrument()` loads one Instrument given an
  explicit type) — no new work needed at this level, only a boot-time
  (non-Menu-transaction) entry point into the same underlying read.

Each of these, on success, calls the same immediate dirty-marking pattern
every ordinary Load already uses (`autosave_markSceneWithoutPatternDirty()`
/ `autosave_markKitDirty()` / `autosave_markWholeInstrumentDirty()` as
appropriate) and `filesystem_setResidentRefreshed()` on its own row only —
never the compound Scene-wide refresh helper, since that would also touch
sibling rows this reload did not resolve. Per §4/§8, these marks will no-op
during boot (tracking still off) and must be captured by the deferred
latch instead.

---

## 8. Deferred dirty-mark + notice latch

### 8.1 Why this is still needed despite Phase D's "immediate marking is enough" finding

Phase D (`S060PHASE_D_RE_DIRTY.md` §2.2, §6.2) found that *runtime*
load/save completions marking dirty state immediately (rather than through
a deferred per-Scene request mask) is correct and strictly better than a
deferred design — because at runtime, mutation tracking is already on, so
an immediate `autosave_markPayloadOffsetDirty()` call actually takes effect.
That finding does not apply to the boot reader: the reader's own Case
2/3 single-level reloads (§7) necessarily run *before* or *as part of*
`filesystem_ensureAutosaveFilesBlocking()`, while mutation tracking is still
disabled — every mark call during this window is a documented no-op (see
the "Known-incorrect boot Bank Load write" passage in `SCOPING_TARGETS.md`
Session 052, which describes exactly this gap for the existing canonical
boot Bank Load and remains unresolved pending this reader). Skipping the
latch would silently under-mark: `autosave_objectFullyCaptured()` would see
an all-clean mask for a row that was never actually re-captured with live
values, and the very first post-boot drain would clear that row's refreshed
flag *without the record ever having been updated* — a correctness bug, not
a cosmetic one.

### 8.2 Latch contents (5 bytes, needs explicit RAM-approval sign-off)

| Field | Size | Purpose |
|---|---|---|
| Bank-fallback flag | 1 byte | Set when §4's root case takes the canonical Bank Load path instead of the autosave winner. Triggers a deferred `autosave_markResidentBankDirty()` replay and queues the one post-boot root notice. |
| Case-2 Scene mask | 2 bytes (16 bits) | Scenes with at least one row needing its single-level-loader dirty-mark replayed. No notice. |
| Case-3 Scene mask | 2 bytes (16 bits) | Scenes invalidated/emptied under P1. Needs both the dirty-mark replay (so the writer captures the now-empty state) and a post-boot notice (§9). |

Total 5 bytes, static SRAM1, boot-scratch lifetime, cleared on every
reset/remount path. **Approved** (2026-09-05, 5 bytes SRAM1, boot-scratch
lifetime, owner: autosave reader). This is the same shape the Session 052
"deferred boot-fallback scope" note in `SCOPING_TARGETS.md` already
anticipated (its own words: "fallback type enum, destination Scene mask,
... a few bytes total") — this plan refines that sketch into an exact
layout and should be treated as its concrete implementation, not a second,
parallel mechanism.

Over-marking (replaying a whole-Scene compound marker even when only one
sub-row actually needed repair) is explicitly sanctioned by this project's
existing policy ("over-marking is safe... under-marking is the bug"), so no
finer per-Kit/per-Instrument granularity is needed in the latch itself.

### 8.3 Replay timing

Applied once, immediately after `filesystem_ensureAutosaveFilesBlocking()`
enables tracking and before the first drain, then the dirty-mark portion of
the latch clears. The notice portion (§9) persists until each queued notice
has actually displayed.

### 8.4 Second, smaller, separate ask: Menu-side overlay-sequencing state

Distinct from the boot-scratch latch above, the post-boot notice *display*
needs its own small piece of ordinary transient Menu runtime state — a
cursor into the pending-notice queue plus a deadline timestamp for the
currently-showing overlay, on the order of 3-4 bytes, comparable to what
`menu_showStaleSettingsWarning()` (`Core/Menu/menu.c:374`, confirmed
present) already carries for an analogous non-blocking warning display.

---

## 9. Post-boot notification architecture

Boot must never block on these notices — the only existing fixed-duration
boot hold (`timebase_holdPreAudioMs()`) is a bare busy-wait with no
watchdog feed, and this project's boot-hang history (`SCOPING_TARGETS.md`)
is reason enough on its own to avoid adding a second one, independent of
the data-dependent (potentially up-to-17-notice) count involved here.

- Boot only ever sets bits in the §8 latch and moves on; audio starts on
  schedule regardless of how many Scenes were invalidated.
- Once runtime is up, a small Menu-layer sequencer drains the Case-3 mask
  (plus the Bank-fallback flag) one bit at a time, each as a fixed ~2-second
  non-blocking overlay using the same timer-comparison pattern already
  proven in `menu_showStaleSettingsWarning()` (poll `time_sysTick` across
  ticks, no busy-wait).
- No coalescing — up to 17 sequential overlays (1 root + 16 Scenes) is
  accepted as-is; it should essentially never reach double digits in normal
  operation.
- Non-interruptible: each overlay runs its full ~2 seconds and
  auto-advances regardless of user input. No early-dismiss handling, no
  input-suppression-vs-passthrough decision needed.

This design was already corrected once (from an earlier blocking design) in
the source draft and needs no further rework here.

---

## 10. Payload-to-resident apply function (the actual restore work)

Still, as in the original draft, the single largest new piece of code: the
inverse of `autosave_getLivePayloadByte()` — a payload-byte-range-to-
resident-state apply function (or family of them, one per Bank/Scene/Kit/
Instrument section) used by Case 1 restore. Confirmed "not implemented, not
to be inferred" per `AUTOSAVE.md`'s "Not implemented and not to be inferred
from the A/B writer" list. Must include type-first Instrument resolution
(the pinned Choke-slot-6 rule already documented in `SCOPING_TARGETS.md`
Session 047 — resolve the stored 3-byte type token before interpreting that
slot's Normal/Morph cells; a Choke type's alternate track-7 decay lives in
its own descriptor, not a second generated Kit value).

This function only ever runs against a row this reader has already decided
is Case 1 (not refreshed) for a Bank/winner-matched record — it is not
itself responsible for any of the proof logic in §5.

### 10.1 Instrument type matching by extension text, not enum ordinal

The autosave payload stores each Instrument's type as a 3-byte ASCII
extension token (`drm`, `snr`, `cym`, `hat`) sourced from the instrument
registry's `type_text`, not the raw `instrument_type_t` enum ordinal
(`Autosave.c:874-885`). The payload-to-resident apply function must
resolve the type by matching that text against the firmware's current
registry — via `storage_instrumentTypeFromText()` or equivalent — never
by casting the ordinal position to `instrument_type_t`. This ensures
forward compatibility: if a future firmware revision reorders the enum
or adds a fifth instrument type, the 3-byte text token in an existing
autosave record still resolves correctly (or fails cleanly) rather than
silently mapping to the wrong type.

**If the 3-byte extension text does not match any known instrument type,
the Instrument is a failed child.** P1 fires: invalidate and empty the
entire Scene that contains this Instrument. This is the same disposition
as Case 3 (§5.2) — an unresolvable instrument is structurally equivalent
to an unresolvable source.

---

## 11. Reconciliation with `SCOPING_TARGETS.md`

- **Session 052 "deferred boot-fallback scope"** — this plan's §8 latch is
  the concrete implementation of that sketch, not a competing design. When
  this reader lands, that Session 052 note should be marked resolved and
  point here.
- **Session 052 "Known-incorrect boot Bank Load write"** — directly
  resolved by §4's boot reorder plus §8's latch; the boot Bank Load's
  currently-silent marker gap is exactly what the latch exists to fix.
- **Bank Save empty-Scene overwrite guard (`scene_isEmpty()`)** — the
  original draft deferred this as out of scope, reasoning that only one
  Bank Save bridge slot could reach the risk. That premise is now false:
  Bank Save loops over all 16 Scenes (§1). Case 3 (§5.2) systematically
  produces genuinely-empty resident Scenes, which is new, real motivation
  for this guard, and the risk surface is now the full 16-Scene Bank, not
  one slot. **Still recommend deferring implementation to its own later
  cleanup sub-phase** (this reader is large enough on its own), but this
  should be re-flagged as higher priority once the reader ships, not left
  at its current low-urgency framing in `SCOPING_TARGETS.md`.
---

## 12. New code required (consolidated)

1. Boot-time blocking `.hcprms` candidate-validation/winner-selection,
   distinct from the runtime writer's state machine, sharing only the
   low-level `autosave_stream*` helpers (§4).
2. `main.c` boot-order reorder — candidate validation before
   `preset_loadBank()` (§4). Its own reviewable unit given this project's
   boot-hang history.
3. `.hcnames`-present-but-corrupt regeneration path, distinct from
   `filesystem_writeResidentNamesBlocking()`, deriving rows from the
   winner record's own identity/source fields (§5.1).
4. Per-Scene Case 1/2/3 evaluation loop, driven by the refreshed flag and
   `filesystem_resolveResidentSource()` (§5.2) — new orchestration code,
   but the two hard primitives it depends on already exist.
5. Row-class-aware `.hcnames` parser/formatter extended to a 4-field
   Instrument row (type), plus updates to every existing Instrument-row
   writer (§6).
6. Boot-time non-cascading per-level component reload paths using existing
   full-object load readers without `.hcnames` cascade side effects (§7).
7. Deferred dirty-mark + notice latch (5 bytes) and its replay/drain logic
   (§8).
8. Menu-side post-boot notice sequencer, reusing the
   `menu_showStaleSettingsWarning()` pattern (§9).
9. Payload-to-resident apply function(s), including type-first Instrument
   resolution (§10).
10. `FILESYSTEM_SPEC.md` HCNAMES row-format update for the type field (§6).

**Not part of this effort:** `scene_isEmpty()` / Bank Save empty-Scene
guard — deferred, §11, but re-flag priority once this reader ships.

---

## 13. Resolved questions

**A. `.hcnames` type-field cutover mechanics** — **Resolved (2026-09-05):**
delete the existing `.hcnames` from the dev card and allow the bootstrap
writer to regenerate it on the next boot. No migration code needed. See §6.

**B. Case 1 restore and the Phase C source-byte cross-check** —
**Resolved (2026-09-05):** a mismatch between the winner record's embedded
Phase C source byte and `.hcnames`'s live source column (despite a clear
refreshed flag) remains Case 1. Autosave is valid; use the autosave payload
and rewrite `.hcnames` with the record's source. Log the mismatch to
`asavetrc.bin` if logging is on — it is evidence of a bug in the
refreshed-flag lifecycle that should be investigated, but it does not
change the case disposition.

---

## 14. Implementation order

1. **Deferred dirty-mark + notice latch (§8)**, wired to the whole-Bank
   fallback outcome only at first. Test: force settings.cfg/generation
   disagreement, confirm canonical Bank load happens, confirm
   `autosave_markResidentBankDirty()` replays correctly once tracking
   enables, confirm the next writer drain actually corrects the record.
   This is the same starting point the original draft chose and remains the
   right one — it is testable without the (much larger) payload-apply
   function existing yet.
2. **Root-level case + `main.c` boot reorder (§4)** — same test as above via
   the real gate logic instead of a forced condition, plus the
   agree-case passthrough (winner matches `active_bank` -> proceed to §5
   instead of short-circuiting to canonical Bank Load).
3. **`.hcnames` Instrument type field (§6)** — can proceed in parallel with
   1-2. Needed before any Case 2 Instrument work in step 6.
4. **Payload-to-resident apply function(s) (§10)** — largest single piece,
   unit-tested off the boot path against synthetic record buffers,
   including the Choke slot-6 exception.
5. **`.hcnames` regeneration for the present-but-corrupt case (§5.1)** —
   depends on 4.
6. **Per-Scene independent Case 1/2/3 with narrow single-level parsers
   (§5.2, §7)** — depends on 3 and 4.
7. **Post-boot notice sequencer (§9)** — depends on 1 (the latch already
   carries the notice queue content) and can otherwise proceed
   independently of 4-6.

(`scene_isEmpty()` / Bank Save guard is explicitly not part of this
sequence — §11.)

---

## 15. Test plan

- **Whole-Bank fallback path** (step 1/2 above): force settings.cfg/
  generation disagreement; confirm canonical Bank load happens; confirm the
  deferred mark replays and the next drain corrects the record.
- **Refreshed-flag-driven Case 1 vs Case 2**: force one Scene's Kit row
  refreshed (simulate a mid-session Kit Load, reboot before a drain
  captures it) and confirm the reader takes Case 2 for that row only, Case 1
  for every sibling row.
- **No-cascade verification**: force a Kit-level Case 2 event where at
  least one of that Kit's 6 Instruments independently carries its own
  defined (non-`-`) source; confirm the Kit reload does not flatten that
  Instrument's `.hcnames` source to `-`, and the Instrument is still
  independently evaluated afterward.
- **Case 2 writes no `.hcnames`**: confirm a successful Case 2 reload
  leaves every `.hcnames` row byte-for-byte unchanged; only the deferred
  latch and resident SRAM change.
- **Case 3 / P1 whole-Scene invalidation**: force an unresolvable source on
  one row of a Scene; confirm the entire Scene empties, not just that row,
  and exactly one post-boot notice queues for it.
- **Latch dual-purpose correctness**: confirm a Case-3 Scene both replays
  its dirty-mark and produces exactly one post-boot notice; confirm a
  Case-2 Scene replays its dirty-mark but produces no notice.
- **No boot blocking**: measure actual elapsed pre-audio boot time with 0,
  1, and 16 Scenes invalidated; confirm no measurable difference at the
  audio-start boundary in any case.
- **Post-boot sequencer under load**: trigger 16 Case-3 invalidations plus
  the root notice; confirm all 17 overlays eventually display without
  interfering with normal Menu/audio operation.
- **`.hcnames` type-field round trip**: Instrument Load/Save/Kit Load/Save/
  Scene Load/Save/Bank Load each produce a correctly-typed 4-field
  Instrument row; a malformed or missing type field fails the read per §6's
  no-tolerance rule.
