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

---

## 16. Code-site implementation schedule

This section specifies every code change required to implement the autosave
boot reader. Each change lists file, line, operation (ADD/MODIFY/REMOVE),
the descriptive comment block to include, and all affiliates. Changes are
grouped by implementation phase per §14's order. §6 (`.hcnames` Instrument
type field) is omitted — it shipped in commit `bc73206`.

Line numbers reference the code as of commit `95e6410`.

---

### Phase 1 — Deferred dirty-mark + notice latch (§8)

#### Change 1.1: Latch struct and static instance

- **File:** `Core/Hardware/SD/filesystem.c`
- **Line:** after line 1847 (after `fs_autosave_winner_cached`)
- **Operation:** ADD

```c
/*
 * Boot-reader deferred-mark and notice latch (§8, S061_AUTOSAVE_READER.md).
 *
 * What: 5-byte boot-scratch structure capturing the reader's decision for
 * each Scene before mutation tracking is enabled. Inputs: the per-Scene
 * Case 1/2/3 evaluation in filesystem_autosaveBootReaderBlocking(). Outputs:
 * replayed via autosave_markResidentBankDirty / markSceneWithoutPatternDirty
 * immediately after ensureAutosaveFilesBlocking enables tracking; Case-3
 * bits also feed the Menu post-boot notice sequencer. Cleared on every
 * reset/remount path. Lifetime: boot-scratch, static SRAM1, 5 bytes.
 * Approved 2026-09-05. Affiliates: autosave_setMutationTrackingEnabled(),
 * ensureAutosaveFilesBlocking() line 24946, menu_autosaveBootNotice*().
 */
typedef struct {
    uint8_t  bank_fallback;      /* 1 = canonical Bank Load used, not winner */
    uint16_t case2_scene_mask;   /* Scenes needing dirty-mark replay only   */
    uint16_t case3_scene_mask;   /* Scenes invalidated under P1             */
} fs_autosave_boot_latch_t;

static fs_autosave_boot_latch_t fs_boot_latch;
```

#### Change 1.2: Clear latch on reset/remount

- **File:** `Core/Hardware/SD/filesystem.c`
- **Line:** ~23045 (inside `filesystem_resetState()` or equivalent init, near
  where `fs_autosave_writer_boot_ready = 0u` is cleared)
- **Operation:** ADD after the existing autosave flag clears

```c
    memset(&fs_boot_latch, 0, sizeof(fs_boot_latch));
```

Also at ~23113 (inside `filesystem_initAfterCardReady()` or the remount
path, same pattern).

#### Change 1.3: Latch replay function

- **File:** `Core/Hardware/SD/filesystem.c`
- **Line:** before `filesystem_ensureAutosaveFilesBlocking()` (~24860)
- **Operation:** ADD

```c
/*
 * Replay deferred dirty marks after mutation tracking enables.
 *
 * What: drains the boot latch into the canonical autosave marker API so the
 * first runtime drain captures every boot-loaded component. Inputs: the
 * populated latch from the boot reader's Case 1/2/3 evaluation. Outputs:
 * autosave_markResidentBankDirty() for the bank-fallback flag;
 * autosave_markSceneWithoutPatternDirty() for every set bit in the
 * case2_scene_mask | case3_scene_mask union. Clears the dirty-mark
 * portion of the latch; preserves case3_scene_mask for Menu notice
 * consumption. Why: marker calls during boot are documented no-ops because
 * tracking is off; skipping the replay would leave the dirty mask clean for
 * rows the drain has never captured, causing objectFullyCaptured to
 * prematurely clear the refreshed flag. Affiliates: §8.3
 * S061_AUTOSAVE_READER.md, autosave_markResidentBankDirty(),
 * autosave_markSceneWithoutPatternDirty().
 */
static void filesystem_replayBootLatch(void)
{
    uint16_t combined;
    uint8_t i;

    if (fs_boot_latch.bank_fallback) {
        autosave_markResidentBankDirty();
        fs_boot_latch.bank_fallback = 0u;
    }
    combined = (uint16_t)(fs_boot_latch.case2_scene_mask |
                          fs_boot_latch.case3_scene_mask);
    for (i = 0u; i < AUTOSAVE_SCENE_COUNT; i++) {
        if (combined & (1u << i))
            autosave_markSceneWithoutPatternDirty(i);
    }
    fs_boot_latch.case2_scene_mask = 0u;
    /* case3_scene_mask preserved for Menu notice drain */
}
```

#### Change 1.4: Call replay after tracking enables

- **File:** `Core/Hardware/SD/filesystem.c`
- **Line:** 24946 (inside `filesystem_ensureAutosaveFilesBlocking()`, after
  `autosave_setMutationTrackingEnabled(1u)` and before
  `autosave_markResidentBankDirty()`)
- **Operation:** MODIFY — insert replay call, remove the existing
  unconditional `autosave_markResidentBankDirty()`

Replace:
```c
    autosave_setMutationTrackingEnabled(1u);
    autosave_markResidentBankDirty();
```
With:
```c
    autosave_setMutationTrackingEnabled(1u);
    filesystem_replayBootLatch();
```

The replay function already calls `markResidentBankDirty()` when the
bank_fallback flag is set. When the boot reader's winner path ran
instead, the latch's per-Scene masks handle per-Scene replay; the
Bank itself is Case 1 (validated by the winner) and does not need a
whole-Bank dirty mark.

#### Change 1.5: Public accessors for Menu notice mask

- **File:** `Core/Hardware/SD/filesystem.h`, after
  `filesystem_ensureAutosaveFilesBlocking()` (~line 268)
- **Operation:** ADD

```c
/*
 * Boot-reader notice mask for the Menu post-boot sequencer (§9).
 *
 * What: returns the Case-3 Scene invalidation mask and bank-fallback flag.
 * Inputs: the boot latch populated by the boot reader. Outputs: 16-bit
 * Scene mask (each set bit = one post-boot overlay) and bank_fallback byte
 * (1 = root notice for canonical Bank Load fallback). Why: Menu must not
 * access filesystem internal state directly. Affiliates:
 * menu_drainAutosaveBootNotices(), fs_boot_latch.
 */
uint16_t filesystem_bootReaderNoticeSceneMask(void);
uint8_t  filesystem_bootReaderNoticeBankFallback(void);
```

- **File:** `Core/Hardware/SD/filesystem.c`, near the latch definition
- **Operation:** ADD implementations

```c
uint16_t filesystem_bootReaderNoticeSceneMask(void)
{
    return fs_boot_latch.case3_scene_mask;
}

uint8_t filesystem_bootReaderNoticeBankFallback(void)
{
    return fs_boot_latch.bank_fallback;
}
```

---

### Phase 2 — Root-level case + main.c boot reorder (§4)

#### Change 2.1: Boot-time blocking validation/winner-selection function

- **File:** `Core/Hardware/SD/filesystem.c`
- **Line:** before `filesystem_ensureAutosaveFilesBlocking()` (~24860)
- **Operation:** ADD

```c
/*
 * Boot-time blocking .hcprms candidate validation and winner selection.
 *
 * What: validates .hcprms1 and .hcprms2 via streaming CRC32C, selects
 * a winner using the same generation/Bank-match rules as the runtime
 * drain (autosaveParameterDrain_tick phases 1-5, line 7325-7466).
 * Returns nonzero if a Bank-matching winner exists. Inputs: mounted
 * SD card with both .hcprms files potentially present; BankData
 * populated with active_bank from settings.cfg. Outputs: populates
 * fs_boot_winner statics (valid, record_index, generation,
 * bank_match). Why: the runtime drain's validation runs inside the
 * asynchronous state machine; boot needs a synchronous one-shot pass
 * that completes before preset_loadBank() decides the Bank Load path.
 * This function validates only — creates nothing, touches no writer
 * flags. Uses the same streaming CRC bounded-chunk pattern and
 * staging_buf as the drain phases 1-3. Affiliates:
 * autosave_streamValidationBegin/Update/Finish/MatchesBank(),
 * autosave_generationIsNewer(), filesystem_autosaveFilenameForIndex(),
 * main.c boot sequence (Change 2.3).
 */
static struct {
    uint8_t  valid;
    uint8_t  record_index;       /* 0 = .hcprms1, 1 = .hcprms2 */
    uint32_t generation;
    uint8_t  bank_match;
} fs_boot_winner;

uint8_t filesystem_validateAutosaveWinnerBlocking(void)
```

Implementation: for candidate_index 0 then 1:
1. `afatfs_chdir(NULL)` to volume root
2. `autosave_streamValidationBegin(&validation)`
3. Open via `afatfs_fopen_lfn(filesystem_autosaveFilenameForIndex(i), "r", ...)`
   — if NULL (file absent), candidate is invalid, advance to next
4. Blocking read loop: `afatfs_fread(op_file, staging_buf, chunk)` →
   `autosave_streamValidationUpdate()` until EOF, using
   `filesystem_autosaveCrcChunkBytes()` for chunk sizing
5. `autosave_streamValidationFinish()` → candidate_valid
6. `autosave_streamValidationMatchesBank(&validation,
   bank_restoreBankSlot(), bank_displayName())` → candidate_bank_match
7. `afatfs_fclose()`, blocking wait
8. Winner selection (identical to drain phase 5, line 7409-7427):
   - candidate beats current winner if: no winner yet, OR candidate
     matches Bank but winner doesn't, OR same match level and
     candidate's generation is newer per `autosave_generationIsNewer()`

Return `fs_boot_winner.valid && fs_boot_winner.bank_match`.

Since this runs pre-audio, single-threaded, the blocking loop pattern
`while (!ready) { afatfs_poll(); }` is safe — identical to every other
boot-blocking filesystem function (e.g. `createBootIndexBlocking` line
24718, `ensureAutosaveFilesBlocking` line 24862).

#### Change 2.2: Public declaration

- **File:** `Core/Hardware/SD/filesystem.h`, near
  `filesystem_ensureAutosaveFilesBlocking()` (~line 268)
- **Operation:** ADD

```c
/*
 * Boot-time .hcprms candidate validation and winner selection.
 *
 * Returns nonzero when a valid winner matching the resident Bank exists.
 * Must be called after preset_loadGlobals() (active_bank known) and
 * before preset_loadBank() (reader needs the result). Inputs: mounted
 * card, BankData with active_bank set. Outputs: internal winner statics.
 * Affiliates: filesystem_autosaveBootReaderBlocking(), main.c.
 */
uint8_t filesystem_validateAutosaveWinnerBlocking(void);
```

#### Change 2.3: main.c — insert winner validation before Bank Load

- **File:** `Core/main.c`
- **Line:** between ~792 (after Bank index load ack, end of stage 10) and
  ~819 (stage 11, before `preset_loadBank`)
- **Operation:** ADD new stage

```c
            /*
             * Stage 10b: validate autosave winner before Bank Load decision.
             *
             * What: streaming CRC32C validation of .hcprms1/.hcprms2 to
             * determine if a valid Bank-matching autosave winner exists.
             * Inputs: BankData with active_bank from settings.cfg (stage 2),
             * mounted card with index files (stages 3-10). Outputs: internal
             * filesystem winner state consulted by stage 11. Why: winner
             * must be known before the Bank Load decision; the existing
             * ensureAutosaveFilesBlocking (post-stage-12) only creates
             * files, never validates. Affiliates: §4 S061_AUTOSAVE_READER.md,
             * filesystem_validateAutosaveWinnerBlocking(),
             * filesystem_autosaveBootReaderBlocking().
             */
            if (filesystem_autosaveEnabled()) {
                boot_showFilesystemStage(10u);
                (void)filesystem_validateAutosaveWinnerBlocking();
                if (filesystem_bootLoggingTimedOut())
                    goto boot_filesystem_timeout;
            }
```

#### Change 2.4: main.c — gate preset_loadBank vs boot reader

- **File:** `Core/main.c`
- **Line:** ~819-830 (stage 11, the existing `preset_loadBank` call)
- **Operation:** MODIFY — wrap existing code in a conditional

```c
            boot_showFilesystemStage(11u);
            /*
             * Stage 11: autosave winner path or canonical library Bank Load.
             *
             * What: if a valid Bank-matching winner was found in stage 10b,
             * the boot reader populates resident SRAM from the winner record
             * and .hcnames. Otherwise the canonical preset_loadBank path
             * runs. Inputs: fs_boot_winner from stage 10b, boot_bank_slot.
             * Outputs: resident Scenes populated; boot latch (§8) populated.
             * Affiliates: §4 S061_AUTOSAVE_READER.md.
             */
            if (filesystem_hasBootWinner()) {
                if (!filesystem_autosaveBootReaderBlocking())
                    goto boot_filesystem_failure;
                while (filesystem_status() == FS_STATUS_BUSY &&
                       !filesystem_bootLoggingTimedOut())
                    filesystem_tick();
                if (filesystem_bootLoggingTimedOut())
                    goto boot_filesystem_timeout;
            } else {
                /* existing preset_loadBank block, unchanged */
                if (filesystem_bankSlotExists(boot_bank_slot)) {
                    ...
                }
                filesystem_setBootLatchBankFallback();
            }
```

#### Change 2.5: Helper functions

- **File:** `Core/Hardware/SD/filesystem.c` and `filesystem.h`
- **Operation:** ADD

```c
/* filesystem.h */
uint8_t filesystem_hasBootWinner(void);
void filesystem_setBootLatchBankFallback(void);

/* filesystem.c */
/*
 * Query boot-time winner validation result.
 *
 * What: returns nonzero when a valid Bank-matching winner was found.
 * Inputs: fs_boot_winner populated by validateAutosaveWinnerBlocking().
 * Outputs: boolean. Affiliates: main.c stage 11 decision gate.
 */
uint8_t filesystem_hasBootWinner(void)
{
    return fs_boot_winner.valid && fs_boot_winner.bank_match;
}

/*
 * Record that the canonical Bank Load fallback path was taken.
 *
 * What: sets the bank_fallback flag in the boot latch so the replay
 * function calls autosave_markResidentBankDirty() after tracking enables.
 * Inputs: called from main.c when no valid winner exists. Outputs:
 * fs_boot_latch.bank_fallback = 1. Affiliates: filesystem_replayBootLatch().
 */
void filesystem_setBootLatchBankFallback(void)
{
    fs_boot_latch.bank_fallback = 1u;
}
```

---

### Phase 3 — Payload-to-resident apply functions (§10)

#### Change 3.1: Bank payload apply

- **File:** `Core/Bank/Scene/Autosave.c`
- **Line:** after `autosave_getLivePayloadByte()` (~line 947)
- **Operation:** ADD

```c
/*
 * Apply a validated winner record's Bank section to resident BankData.
 *
 * What: the inverse of the Bank portion of autosave_getLivePayloadByte().
 * Reads payload bytes 0..14 and writes them into BankData via the same
 * setters the runtime mutation path uses. Inputs: pointer to the 128-byte
 * Bank section (payload-relative offset 0). Outputs: bank_setRestoreBankSlot,
 * bank_setDisplayName, bank_setScenePresentMask, bank_setActiveSceneSlot,
 * bank_setSceneMaskVoiceEdit all updated. Reserved bytes 15..127 ignored.
 * Why: Case 1 restore populates BankData from the winner with tracking OFF.
 * Affiliates: autosave_getLivePayloadByte() lines 707-759, BankData.h,
 * §10 S061_AUTOSAVE_READER.md.
 */
void autosave_applyBankPayload(const uint8_t *bank_section)
```

Implementation reads:
- bytes 0..1: LE uint16 → `bank_setRestoreBankSlot()`
- bytes 2..9: 8-byte name → `bank_setDisplayName()`
- bytes 10..11: LE uint16 → `bank_setScenePresentMask()`
- byte 12: → `bank_setActiveSceneSlot()`
- bytes 13..14: LE uint16 → `bank_setSceneMaskVoiceEdit()`

#### Change 3.2: Scene parameters apply

- **File:** `Core/Bank/Scene/Autosave.c`
- **Line:** after Change 3.1
- **Operation:** ADD

```c
/*
 * Apply a validated winner record's Scene parameters to resident SceneData.
 *
 * What: the inverse of autosave_getSceneParameter(). Reads the 40 live
 * Scene-parameter bytes from the payload and writes them into
 * scene->settings. Inputs: scene_index (0..15), pointer to the 1920-byte
 * Scene section. Outputs: morph_amount, voice_morph_amount[6],
 * voice_decimation_all, audio_out[6], fx_send_amount[6], fader_setting[6],
 * midi_channel[7], midi_note[7] all updated in scene_get(scene_index)->
 * settings. Why: each field's payload index must mirror the getter's
 * autosave_scene_parameter_t enum chain. Affiliates:
 * autosave_getSceneParameter() line 634, autosave_scene_parameter_t,
 * SceneData.h scene_settings_t.
 */
void autosave_applyScenePayload(uint8_t scene_index,
                                const uint8_t *scene_section)
```

Implementation reads `scene_section[AUTOSAVE_SCENE_PARAMETERS_OFFSET + i]`
for i = 0..39, dispatching by the same `autosave_scene_parameter_t` enum
ordering as the getter to write each field.

#### Change 3.3: Kit parameters apply

- **File:** `Core/Bank/Scene/Autosave.c`
- **Line:** after Change 3.2
- **Operation:** ADD

```c
/*
 * Apply a validated winner record's Kit parameters to resident SceneData.
 *
 * What: the inverse of the Kit portion of autosave_getLivePayloadByte().
 * Reads the 2 live Kit-parameter bytes. Inputs: scene_index, pointer to
 * the Kit sub-section (scene_section + AUTOSAVE_KIT_OFFSET). Outputs:
 * slot6_track7_amp_envelope_decay and slot6_track7_morph_amp_envelope_decay.
 * Why: these two Choke-related Kit parameters are the only live Kit-level
 * payload bytes. Affiliates: autosave_getLivePayloadByte() lines 831-854,
 * autosave_kit_parameter_t, SceneData.h kit_settings_t.
 */
void autosave_applyKitPayload(uint8_t scene_index,
                              const uint8_t *kit_section)
```

#### Change 3.4: Instrument parameters apply

- **File:** `Core/Bank/Scene/Autosave.c`
- **Line:** after Change 3.3
- **Operation:** ADD

```c
/*
 * Apply a validated winner record's Instrument parameters to SceneData.
 *
 * What: the inverse of the Instrument portion of
 * autosave_getLivePayloadByte(). Resolves the 3-byte type token via
 * storage_instrumentTypeFromText(), then copies descriptor-indexed Normal
 * and Morph endpoint bytes. Inputs: scene_index, slot (0..5), pointer to
 * the 192-byte Instrument record. Outputs: instrument->type set from type
 * text; Normal endpoints copied for indices 0..descriptor_count-1; Morph
 * endpoints copied only for Morphable descriptors. Returns nonzero on
 * success, zero if type token unrecognized (P1: caller invalidates Scene).
 * Why: type resolved by extension text, not enum ordinal (§10.1 forward
 * compatibility). The Choke slot-6 rule is implicit: the type's descriptor
 * layout determines which cells are live. Affiliates:
 * autosave_getLivePayloadByte() lines 856-945,
 * storage_instrumentTypeFromText(), instrumentManager_registryEntry(),
 * SceneData.h kit_instrument_slot_t/instrument_parameter_images_t.
 */
uint8_t autosave_applyInstrumentPayload(uint8_t scene_index,
                                        uint8_t instrument_slot,
                                        const uint8_t *instrument_record)
```

Implementation:
1. Read bytes 0..2: 3-byte type text, NUL-terminate for lookup
2. `storage_instrumentTypeFromText(type_text)` → if UNKNOWN, return 0
3. `instrumentManager_registryEntry(type)` → entry with descriptor_count
4. Set `instrument->type = type`
5. Copy Normal: for each i < entry->descriptor_count and i < 64,
   `instrument->parameter_images.instrument_parameters[i] =
   instrument_record[AUTOSAVE_INSTRUMENT_NORMAL_OFFSET + i]`
6. Copy Morph: for each i < entry->descriptor_count and i < 64 where
   `(entry->descriptors[i].flags & INSTRUMENT_PARAM_FLAG_MORPHABLE)`,
   `instrument->parameter_images.morph_instrument_parameters[i] =
   instrument_record[AUTOSAVE_INSTRUMENT_MORPH_OFFSET + i]`

#### Change 3.5: Source cross-check helper

- **File:** `Core/Bank/Scene/Autosave.c`
- **Line:** after Change 3.4
- **Operation:** ADD

```c
/*
 * Extract the embedded Phase C source value from a payload section.
 *
 * What: reads the 2-byte LE source field at a known offset within a Scene,
 * Kit, or Instrument sub-section. Inputs: pointer to section start,
 * section-relative source offset (AUTOSAVE_SCENE_SOURCE_OFFSET = 8,
 * AUTOSAVE_KIT_SOURCE_OFFSET = 8, or AUTOSAVE_INSTRUMENT_SOURCE_OFFSET
 * = 11). Output: 16-bit source value (value bits only; flag bits are not
 * stored in the payload). Why: boot reader cross-checks this against
 * .hcnames' live source column for Case 1 defense-in-depth (§5.2).
 * Affiliates: autosave_getSourceByte() (the getter inverse), Phase C
 * source geometry in Autosave.h.
 */
uint16_t autosave_extractPayloadSource(const uint8_t *section,
                                       uint8_t source_offset)
```

#### Change 3.6: Public declarations in Autosave.h

- **File:** `Core/Bank/Scene/Autosave.h`
- **Line:** after the mark-dirty function group (~line 509)
- **Operation:** ADD

```c
/*
 * Payload-to-resident apply functions (boot reader, §10).
 *
 * What: inverse of autosave_getLivePayloadByte() — writes winner-record
 * payload bytes into live BankData/SceneData. Inputs: pointers into
 * validated winner record payload sections. Outputs: BankData and SceneData
 * updated. applyInstrumentPayload returns 0 if type token unrecognized
 * (P1: invalidate the Scene). These run during boot with tracking OFF.
 * Affiliates: filesystem_autosaveBootReaderBlocking().
 */
void autosave_applyBankPayload(const uint8_t *bank_section);
void autosave_applyScenePayload(uint8_t scene_index,
                                const uint8_t *scene_section);
void autosave_applyKitPayload(uint8_t scene_index,
                              const uint8_t *kit_section);
uint8_t autosave_applyInstrumentPayload(uint8_t scene_index,
                                        uint8_t instrument_slot,
                                        const uint8_t *instrument_record);
uint16_t autosave_extractPayloadSource(const uint8_t *section,
                                       uint8_t source_offset);
```

---

### Phase 4 — .hcnames regeneration from winner record (§5.1)

#### Change 4.1: Regeneration function

- **File:** `Core/Hardware/SD/filesystem.c`
- **Line:** near `filesystem_writeResidentNamesBlocking()` (~25088)
- **Operation:** ADD

```c
/*
 * Regenerate .hcnames from a validated winner record's identity fields.
 *
 * What: rebuilds all 129 rows of .hcnames using the winner record's
 * embedded name bytes and Phase C source fields, plus the Bank identity
 * from BankData. The #types header is emitted first via
 * filesystem_formatHcnamesHeader(). Inputs: the validated winner record,
 * read in bounded chunks from the card. Outputs: a new .hcnames written
 * via the existing atomic safe-write pattern (temp file → rename);
 * fs_resident_source[] and hcnames_name_mirror[] populated from written
 * content. All refreshed flags SET (worst-case safe: every row treated
 * as unproven until the drain proves otherwise). Why: if .hcnames is
 * absent or corrupt while a valid winner exists, the reader must not
 * fall back to a full canonical Bank Load — the winner record contains
 * enough identity data to reconstruct the register. Affiliates: §5.1
 * S061_AUTOSAVE_READER.md, filesystem_writeResidentNamesBlocking(),
 * autosave_extractPayloadSource(), filesystem_formatResidentNameLine(),
 * filesystem_formatHcnamesHeader().
 *
 * Record reading strategy: the 34,768-byte record is read in bounded
 * chunks, extracting only the name (8 bytes) and source (2 bytes) fields
 * from each section. For Instruments, the type text (3 bytes) is also
 * extracted. The relevant offsets are:
 *   Bank:       name at payload+2, slot at payload+0 (2 bytes LE)
 *   Scene N:    name at scene_base+0, source at scene_base+8
 *   Kit N:      name at kit_offset+0, source at kit_offset+8
 *   Inst N/S:   type at inst_offset+0, name at inst_offset+3,
 *               source at inst_offset+11
 * where scene_base = 128 + N*1920, kit_offset = scene_base + 640,
 * inst_offset = kit_offset + 128 + S*192.
 */
uint8_t filesystem_regenerateHcnamesFromWinnerBlocking(void)
```

Implementation:
1. Open the winner record file for reading
2. Seek/read through the record extracting identity fields per section
3. Populate `fs_resident_source[]` with extracted sources, all with
   `FS_RESIDENT_SOURCE_REFRESHED_FLAG` set
4. Populate `hcnames_name_mirror[]` with extracted names
5. Write .hcnames via temp-file-rename using `formatHcnamesHeader()`
   and `formatResidentNameLine()` for each row
6. Close and return

#### Change 4.2: Public declaration

- **File:** `Core/Hardware/SD/filesystem.h`
- **Operation:** ADD

```c
uint8_t filesystem_regenerateHcnamesFromWinnerBlocking(void);
```

---

### Phase 5 — Per-Scene Case 1/2/3 evaluation + boot reader (§5.2)

#### Change 5.1: Boot reader orchestration function

- **File:** `Core/Hardware/SD/filesystem.c`
- **Line:** before `filesystem_ensureAutosaveFilesBlocking()` (~24860)
- **Operation:** ADD

```c
/*
 * Boot-time autosave reader: populate resident SRAM from the winner record.
 *
 * What: the central orchestrator (§4-§10, S061_AUTOSAVE_READER.md). Called
 * from main.c stage 11 when a valid Bank-matching winner exists. Reads
 * .hcnames and the winner record, then evaluates each of up to 8 identity
 * rows per Scene independently:
 *
 *   Case 1 (not refreshed): trust the winner's payload. Apply via
 *     autosave_apply*() functions. Cross-check embedded source vs .hcnames;
 *     log mismatch but use autosave payload regardless.
 *   Case 2 (refreshed, source resolvable): narrow single-level reload from
 *     resolved source (§7). Set case2_scene_mask in boot latch.
 *   Case 3 (refreshed, source unresolvable): P1. Empty/invalidate entire
 *     Scene. Set case3_scene_mask in boot latch. Break inner loop.
 *
 * Inputs: fs_boot_winner from validateAutosaveWinnerBlocking(); .hcnames on
 * card (regenerated via §5.1 if corrupt); BankData with active_bank set.
 * Outputs: resident SceneData/BankData/fs_resident_source[]/
 * hcnames_name_mirror[] populated; fs_boot_latch populated for deferred
 * replay. Returns nonzero on success.
 *
 * Why: replaces canonical preset_loadBank() when a valid winner exists,
 * restoring the user's last-known parameter state. The boot latch (§8)
 * defers all dirty marks until ensureAutosaveFilesBlocking enables tracking.
 *
 * Affiliates: main.c stage 11, validateAutosaveWinnerBlocking(),
 * replayBootLatch(), autosave_apply*(), filesystem_resolveResidentSource(),
 * filesystem_bootReaderNarrowLoad*().
 *
 * SRAM: uses a 1920-byte stack buffer for one Scene section at a time
 * (boot-only, stack not contended). No new static SRAM beyond the 5-byte
 * latch.
 */
uint8_t filesystem_autosaveBootReaderBlocking(void)
```

Implementation:

**Step 1 — Read .hcnames.** Blocking open + validate `#types` header +
parse all rows via `cacheResidentRecord()`. Reuse the same pattern as
`ensureAutosaveFiles_tick` phases 0-3 (line 6458) but synchronously.
On failure → attempt `regenerateHcnamesFromWinnerBlocking()`.
On regeneration failure → set `bank_fallback = 1`, return 0.

**Step 2 — Apply Bank payload.** Open winner record
(`filesystem_autosaveFilenameForIndex(fs_boot_winner.record_index)`),
read the 128-byte Bank section (absolute offset =
`AUTOSAVE_PAYLOAD_OFFSET`, payload bytes 0..127). Call
`autosave_applyBankPayload()`. Set `bank_setHasResidentBank(1)`.

**Step 3 — Per-Scene evaluation.** For scene_index = 0..15:
  - If `!(bank_scenePresentMask() & (1u << scene_index))`: skip
  - Read the 1920-byte Scene section from the winner record
    (absolute offset = `AUTOSAVE_PAYLOAD_OFFSET +
    AUTOSAVE_BANK_SECTION_BYTES + scene_index *
    AUTOSAVE_SCENE_SECTION_BYTES`) into a stack buffer
  - Evaluate 8 identity rows in order: Scene (row = 1+scene_index),
    Kit (row = 17+scene_index), Instruments (rows = 33 +
    scene_index*6 + 0..5):

    For each row:
    - `source = fs_resident_source[row]`
    - If `!(source & FS_RESIDENT_SOURCE_REFRESHED_FLAG)`: **Case 1**
      - Apply the appropriate payload section via autosave_apply*()
      - Cross-check: `embedded = autosave_extractPayloadSource(section, offset)`
        vs `live = source & FS_RESIDENT_SOURCE_VALUE_MASK`.
        If mismatch, log to trace; still Case 1.
    - Else: `filesystem_resolveResidentSource(row, &resolved_row)`
      - If resolved source is a loadable slot (0..999 or
        `FS_RESIDENT_SOURCE_INSTRUMENT_DIRECT`): **Case 2**
        - Call the appropriate narrow loader (Change 6.*)
        - `fs_boot_latch.case2_scene_mask |= (1u << scene_index)`
      - Else: **Case 3** — P1
        - Empty/invalidate the Scene (zero SceneData, clear sources)
        - `fs_boot_latch.case3_scene_mask |= (1u << scene_index)`
        - Break inner loop

**Step 4 — Conditional .hcnames rewrite.** If any Case 1 row's embedded
source differed from .hcnames, update `fs_resident_source[row]` with the
record's source value and rewrite .hcnames via the temp-file-rename
pattern.

**Step 5 — Close winner record file, return 1.**

The winner record file remains open throughout the per-Scene loop
(positioned via seek/sequential read). Each Scene section is read into
the 1920-byte stack buffer, processed, then the buffer is reused for
the next Scene.

#### Change 5.2: Public declaration

- **File:** `Core/Hardware/SD/filesystem.h`
- **Operation:** ADD

```c
uint8_t filesystem_autosaveBootReaderBlocking(void);
```

---

### Phase 6 — Case 2 narrow single-level loaders (§7)

These are new boot-time non-cascading load functions that read one level
of the hierarchy from disk without touching sibling/child `.hcnames`
sources. They reuse the existing text parsers (storage_scenesetInit/
ParseLine/Finalize, etc.) but do NOT call the existing Load request state
machines (which cascade sources to INHERIT).

#### Change 6.1: Narrow Scene loader

- **File:** `Core/Hardware/SD/filesystem.c`
- **Line:** before `filesystem_autosaveBootReaderBlocking()`
- **Operation:** ADD

```c
/*
 * Boot-time narrow Scene-level reload (Case 2, §7).
 *
 * What: loads only Scene-own fields (sceneset.scg settings) from the
 * resolved library source without cascading Kit or Instrument sources
 * to INHERIT. Inputs: scene_index (destination), source_slot (resolved
 * library Scene number from resolveResidentSource). Outputs:
 * scene->settings populated from sceneset.scg; NO .hcnames source
 * writes, NO Kit/Instrument source changes. Why: P2 requires that a
 * child carrying its own defined source must not be overwritten by a
 * parent reload. The existing loadSceneDirectory_tick (line 11153)
 * cascades all child sources to INHERIT at line 11664-11676 — the
 * narrow loader skips this entirely. Affiliates: §7
 * S061_AUTOSAVE_READER.md, storage_scenesetInit/ParseLine/Finalize(),
 * scene_get(), filesystem_resolveResidentSource().
 *
 * Path construction: opens Scene/NNN Name/sceneset.scg using the same
 * filesystem_makeNumberedDir() pattern as loadSceneDirectory_tick phase 6
 * (line 11266), but synchronously. Uses blocking afatfs_fopen/fread/fclose.
 */
static uint8_t filesystem_bootReaderNarrowLoadScene(
    uint8_t scene_index, uint16_t source_slot)
```

#### Change 6.2: Narrow Kit loader

- **File:** `Core/Hardware/SD/filesystem.c`
- **Operation:** ADD

```c
/*
 * Boot-time narrow Kit-level reload (Case 2, §7).
 *
 * What: loads Kit settings (kitset.kcg) and all 6 Instrument files from
 * the resolved library Kit source without cascading Instrument sources
 * to INHERIT. Inputs: scene_index, source_slot (resolved Kit library
 * number), resolved_row (the ancestor level that supplied the source, from
 * resolveResidentSource). Outputs: scene->kit.settings and
 * scene->kit.instruments[] populated; NO Instrument .hcnames source
 * writes. Why: the existing loadKitDirectory_tick cascades Instrument
 * sources to INHERIT at line 10913 — the narrow loader preserves each
 * Instrument's .hcnames source.
 *
 * Inheritance sub-case: when resolved_row is a Scene row (Kit inherits
 * from Scene), the Kit lives inside Scene/NNN Name/Kit <name>/. When
 * resolved_row is the Bank row, the Kit lives inside the Bank's child
 * Scene directory. The resolved_row output from resolveResidentSource()
 * tells the loader which directory structure to navigate. Affiliates: §7
 * S061_AUTOSAVE_READER.md, storage_kitsetInit/ParseLine/Finalize(),
 * storage_instrumentInit/ParseLine/Finalize(), scene_get().
 */
static uint8_t filesystem_bootReaderNarrowLoadKit(
    uint8_t scene_index, uint16_t source_slot, uint16_t resolved_row)
```

Implementation:
1. Navigate to the Kit directory (standalone or embedded, based on
   resolved_row telling which ancestor level supplied the source)
2. Open and parse kitset.kcg → kit_settings, instrument types/filenames
3. For each of 6 slots: open and parse the instrument file
4. Apply all to `scene_get(scene_index)->kit`
5. Do NOT write to `fs_resident_source[]` for any Instrument row

#### Change 6.3: Narrow Instrument loader

- **File:** `Core/Hardware/SD/filesystem.c`
- **Operation:** ADD

```c
/*
 * Boot-time narrow Instrument-level reload (Case 2, §7).
 *
 * What: loads a single Instrument from its resolved library source.
 * Equivalent to loadInstrument_tick (line 14115) but blocking, boot-only,
 * no .hcnames write. Inputs: scene_index, instrument_slot, type (from
 * .hcnames Instrument row type field), source (direct library slot or
 * FS_RESIDENT_SOURCE_INSTRUMENT_DIRECT, with resolved_row identifying
 * the ancestor directory). Outputs: scene->kit.instruments[slot]
 * populated with type and parameter images. Why: Instrument Load is
 * already single-level (no cascade) but runs through the async Menu
 * state machine unavailable during boot. Affiliates: §7
 * S061_AUTOSAVE_READER.md, loadInstrument_tick line 14115,
 * storage_instrumentInit/ParseLine/Finalize().
 *
 * Inheritance sub-cases: an Instrument inheriting from a Kit loads
 * from that Kit's bundled member file; inheriting from a Scene loads
 * from that Scene's embedded Kit's member file; inheriting from a Bank
 * loads from the Bank's child Scene's embedded Kit's member file. The
 * resolved_row output drives the path decision.
 *
 * Source FS_RESIDENT_SOURCE_INSTRUMENT_DIRECT: the Instrument was loaded
 * directly from Instrument/<type>/NNN, not from a Kit/Scene/Bank bundle.
 * The browser_index for the standalone directory is needed; it is already
 * stored in .hcnames (not applicable — INSTRUMENT_DIRECT is a sentinel,
 * the actual slot is not recoverable from .hcnames row format). In this
 * case, the Instrument's own on-disk data is the only source. The narrow
 * loader must locate the file by matching the .hcnames name against the
 * boot index (filesystem_instrumentSlotForName or equivalent).
 */
static uint8_t filesystem_bootReaderNarrowLoadInstrument(
    uint8_t scene_index, uint8_t instrument_slot,
    instrument_type_t type, uint16_t source_slot,
    uint16_t resolved_row)
```

---

### Phase 7 — Post-boot notice sequencer (§9)

#### Change 7.1: Menu-side notice state

- **File:** `Core/Menu/menu.c`
- **Line:** near `menu_staleWarningActive` (~line 153)
- **Operation:** ADD

```c
/*
 * Post-boot autosave notice sequencer state (§9, S061_AUTOSAVE_READER.md).
 *
 * What: 6 bytes of transient Menu runtime state for draining the boot
 * latch's Case-3 Scene mask and bank-fallback flag as sequential
 * non-blocking LCD overlays. Inputs: filesystem_bootReaderNoticeSceneMask()
 * and filesystem_bootReaderNoticeBankFallback() read once after boot.
 * Outputs: up to 17 sequential ~2-second overlays using the same timer-
 * comparison pattern as menu_showStaleSettingsWarning() (line 374). Why:
 * boot must never block on notices; audio starts on schedule regardless.
 * Affiliates: menu_showStaleSettingsWarning(), time_sysTick, fs_boot_latch.
 */
static uint16_t menu_bootNoticeSceneMask = 0u;
static uint8_t  menu_bootNoticeBankFlag = 0u;
static uint8_t  menu_bootNoticeActive = 0u;
static uint16_t menu_bootNoticeStart = 0u;
#define MENU_BOOT_NOTICE_MS 2000u
```

#### Change 7.2: Notice sequencer tick function

- **File:** `Core/Menu/menu.c`
- **Line:** after `menu_showStaleSettingsWarning()` (~line 395)
- **Operation:** ADD

```c
/*
 * Drain one pending autosave boot notice per tick.
 *
 * What: shows sequential non-blocking LCD overlays for each Case-3
 * invalidated Scene and the bank-fallback event. Bank notice first (if
 * pending), then each Scene in index order. Each overlay runs ~2 seconds
 * and auto-advances. Inputs: menu_bootNotice* state, time_sysTick.
 * Outputs: LCD overlays, menu_storageBusy toggled. No early-dismiss, no
 * input suppression beyond storageBusy. Affiliates: §9
 * S061_AUTOSAVE_READER.md, menu_showStaleSettingsWarning() (same pattern).
 */
static void menu_drainAutosaveBootNotices(void)
{
    if (menu_bootNoticeActive) {
        if ((uint16_t)(time_sysTick - menu_bootNoticeStart) >=
            MENU_BOOT_NOTICE_MS) {
            menu_bootNoticeActive = 0u;
            menu_storageBusy = 0u;
            menu_repaintAll();
        }
        return;
    }
    if (menu_bootNoticeBankFlag) {
        lcd_waitForIdle();
        lcd_clear();
        lcd_home();
        lcd_string("AutoSave");
        lcd_setcursor(0, 2);
        lcd_string("bank load");
        lcd_waitForIdle();
        menu_storageBusy = 1u;
        menu_bootNoticeActive = 1u;
        menu_bootNoticeStart = time_sysTick;
        menu_bootNoticeBankFlag = 0u;
        return;
    }
    if (menu_bootNoticeSceneMask != 0u) {
        uint8_t i;
        char line2[16];

        for (i = 0u; i < 16u; i++) {
            if (menu_bootNoticeSceneMask & (1u << i))
                break;
        }
        menu_bootNoticeSceneMask &= (uint16_t)~(1u << i);
        /* Format "Sc NN empty" */
        lcd_waitForIdle();
        lcd_clear();
        lcd_home();
        lcd_string("AutoSave");
        lcd_setcursor(0, 2);
        /* scene index display */
        lcd_string("Sc ");
        lcd_number(i);
        lcd_string(" empty");
        lcd_waitForIdle();
        menu_storageBusy = 1u;
        menu_bootNoticeActive = 1u;
        menu_bootNoticeStart = time_sysTick;
        return;
    }
}
```

#### Change 7.3: Initialize notice state after boot

- **File:** `Core/Menu/menu.c`
- **Line:** in post-boot initialization, after
  `filesystem_ensureAutosaveFilesBlocking()` returns in main.c
  (call from `menu_init()` or `menu_pollPresetStatus()` at line 919)
- **Operation:** ADD

```c
    menu_bootNoticeSceneMask = filesystem_bootReaderNoticeSceneMask();
    menu_bootNoticeBankFlag = filesystem_bootReaderNoticeBankFallback();
```

#### Change 7.4: Call sequencer from menu tick

- **File:** `Core/Menu/menu.c`
- **Line:** in `menu_tick()`, near the existing `menu_staleWarningActive`
  check (~line 8143)
- **Operation:** ADD — insert before the stale-warning check

```c
    if (menu_bootNoticeActive ||
        menu_bootNoticeBankFlag ||
        menu_bootNoticeSceneMask) {
        menu_drainAutosaveBootNotices();
        return;
    }
```

---

### Phase 8 — Documentation updates

#### Change 8.1: FILESYSTEM_SPEC.md

- **File:** `knowledge_files/specification_reference/FILESYSTEM_SPEC.md`
- **Operation:** ADD — document the boot reader decision tree (Cases 1/2/3),
  the deferred latch, narrow loaders, and the relationship to the existing
  .hcprms create-only and drain validation paths.

#### Change 8.2: AUTOSAVE.md

- **File:** `knowledge_files/specification_reference/AUTOSAVE.md`
- **Operation:** ADD — document the `autosave_apply*()` family and their
  relationship to the existing `getLivePayloadByte()` getter.

---

### Summary: all new/modified files

| File | Changes | Nature |
|------|---------|--------|
| `Core/Hardware/SD/filesystem.c` | 1.1-1.5, 2.1, 2.5, 4.1, 5.1, 6.1-6.3 | ADD ~800-1200 lines |
| `Core/Hardware/SD/filesystem.h` | 1.5, 2.2, 2.5, 4.2, 5.2 | ADD ~35 lines |
| `Core/Bank/Scene/Autosave.c` | 3.1-3.5 | ADD ~250 lines |
| `Core/Bank/Scene/Autosave.h` | 3.6 | ADD ~20 lines |
| `Core/main.c` | 2.3, 2.4 | MODIFY ~40 lines |
| `Core/Menu/menu.c` | 7.1-7.4 | ADD ~60 lines |
| `FILESYSTEM_SPEC.md` | 8.1 | ADD documentation |
| `AUTOSAVE.md` | 8.2 | ADD documentation |

### New static SRAM

| Item | Size | Lifetime | Location |
|------|------|----------|----------|
| `fs_boot_latch` | 5 bytes | boot-scratch | `filesystem.c` |
| `fs_boot_winner` | 8 bytes | boot-scratch | `filesystem.c` |
| `menu_bootNotice*` | 6 bytes | transient runtime | `menu.c` |
| **Total** | **19 bytes** | | |

No new heap. The 1920-byte Scene-section buffer in the boot reader is
stack-allocated (boot-only, not contended).

### Payload geometry reference (from Autosave.h)

```
Record (34,768 bytes):
  Header:  0..63    (64 bytes: magic/version/commit/generation/CRC/probe)
  Mask:    64..3919  (3,856 bytes: 1 bit per payload byte)
  Payload: 3920..34767 (30,848 bytes)

Payload (30,848 bytes, offset = AUTOSAVE_PAYLOAD_OFFSET = 3920):
  Bank:    +0..+127    (128 bytes)
    slot:           +0   (2 bytes LE)
    name:           +2   (8 bytes)
    present_mask:   +10  (2 bytes LE)
    active_scene:   +12  (1 byte)
    voice_edit:     +13  (2 bytes LE)
    reserved:       +15..+127

  Scene N: +128 + N*1920  (1,920 bytes each, N=0..15)
    name:           +0   (8 bytes)
    source:         +8   (2 bytes LE)
    parameters:     +10  (118 alloc, 40 live)
    Effect:         +128 (512 bytes: type+name+params; 0 live params)
    Kit:            +640 (1,280 bytes)
      name:         +0   (8 bytes)
      source:       +8   (2 bytes LE)
      parameters:   +10  (118 alloc, 2 live)
      Instruments:  +128 (6 × 192 bytes)
        Instrument:
          type_text: +0  (3 bytes ASCII)
          name:      +3  (8 bytes)
          source:    +11 (2 bytes LE)
          normal:    +13 (72 bytes, descriptor-indexed)
          morph:     +85 (72 bytes, descriptor-indexed, Morphable only)
          padding:   +157..+191
```
