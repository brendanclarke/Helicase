# Session 060 — Conservative AutoSave Validation and Speedup Plan

Status: general design and staging plan only. This document authorizes no
firmware change. It replaces `S060_AUTOSAVE_BOOT_VALID_SPEEDUP.md` as the
planning direction for a new implementation based on the end of Session 059,
commit `dcbd400`.

The over-scoped implementation at `3929745` and its follow-up analysis remain
useful evidence, but they are not the code base or architecture to repair in
place. Preserve those commits, `S060_POST_FIX.md`, and the copied test card as
references while new work starts from the known Session 059 implementation.

## 1. Objective

Improve normal AutoSave transaction time without redesigning the proven A/B
writer, and establish one explicit boot-time classification of the three
relevant authorities:

1. `settings.cfg` selects the intended Bank;
2. `/.hcnames` records the resident names and library provenance; and
3. `/.hcprms1` and `/.hcprms2` contain power-loss-protected AutoSave records.

The immediate performance change is deliberately small:

- validate both AutoSave records once during boot;
- retain the selected record for the mounted-card session;
- use that retained selection during ordinary runtime AutoSave transactions;
  and
- return to the existing complete validation/setup path only after an event
  that makes the retained selection unusable.

This does not make AutoSave a partial-file writer. Every published generation
remains a full streamed copy into the inactive A/B record.

This plan also records the settled settings/HCNAMES/AutoSave authority rules,
but their implementation is divided into later, independently tested stages.
The first speedup stage does not implement the future AutoSave payload reader,
change the HCNAMES grammar, change the AutoSave record format, or redesign
Menu Load/Save behavior.

## 2. Conservative scope boundary

### 2.1 Session 059 systems retained

The following remain in place unless a later focused plan proves a specific
change is necessary:

- AsyncFATFS remains the only SD/FAT implementation and continues to own
  cooperative retry and terminal I/O results.
- `filesystem.c` remains the serialized filesystem facade.
- The ordinary Bank, Scene, Kit, Instrument, settings, HCNAMES, and `.hcindex`
  formats remain unchanged during the initial speedup.
- AutoSave v1 remains exactly 34,768 bytes during the initial speedup.
- `Autosave.c` remains the sole owner of the canonical 3,856-byte mutation
  mask and its atomic take/re-dirty behavior.
- The existing 128-byte-per-foreground-pass CRC/work bound remains.
- The writer still reads the selected source, transforms it, and writes the
  complete inactive target.
- The target remains uncommitted through its data write and data sync; its CRC
  is then published and synced, the commit marker is written last, and the
  final sync is required before the target becomes the selected source.
- The selected source is never modified by its transaction.
- An operation already admitted is never cancelled because Menu state changes.
- Powered-on SD-card removal remains unsupported.

There is no split CRC, section CRC table, stale-section table, file rename/copy
phase, surgical section update, or record-sized RAM buffer.

### 2.2 Work explicitly excluded from the first speedup

Do not combine any of the following with the boot discovery/runtime cache
change:

- AutoSave record v2 or an appended source table;
- HCNAMES type or `pending_autosave` fields;
- applying an AutoSave payload at boot;
- reconstructing Scene/Kit/Instrument payloads from HCNAMES at boot;
- Kit/Instrument scroll-load coalescing or cancellation;
- changing Menu exit behavior;
- reorganizing Bank Load/Save state machines;
- replacing existing exceptional recovery with a new lifecycle machine;
- changing AsyncFATFS; or
- rewriting the verifier as part of the firmware patch.

Those remain valid follow-up requirements, but they must be implemented and
accepted separately. This separation is the central correction to the prior
S060 plan.

## 3. Terminology and performance premise

The long-form boot operation is the **boot-time valid AutoSave dual-record
discovery transaction**. “Boot-time AutoSave discovery” and “dual-record
AutoSave validation transaction” refer to the same operation.

An ordinary post-boot writer operation is simply a normal runtime AutoSave
transaction. A further generation required to finish a retained dirty mask is
a **continuation AutoSave transaction**. No other temperature-based terminology
is used.

In Session 059, every admitted runtime drain starts by streaming and validating
both complete records. It then reads the chosen record's mutation mask and, if
work exists, streams that record again while constructing the inactive target.
The repeated full-record validation reads are the avoidable cost.

The CRC calculation is not removed. The normal writer must still read the
complete selected source because the transaction publishes a complete target.
Those source bytes can simultaneously feed:

- a source-integrity CRC, proving that the cached source has not changed since
  it was selected; and
- the target CRC, calculated after applying the new header, mask, and captured
  payload substitutions.

The expected speedup is therefore the removal of approximately two complete
record-validation reads from each normal runtime transaction. It is not a
shorter target write, a partial-copy scheme, or avoidance of CRC arithmetic.

## 4. The three authorities

### 4.1 `settings.cfg`: top-level boot intent

`settings.cfg` remains the only persisted setting that chooses the Bank to use
at boot. It also supplies the AutoSave ON/OFF policy. It does not store Scene,
Kit, or Instrument provenance and must not gain a persisted AutoSave winner.

The effective Bank is resolved before AutoSave discovery:

1. use `active_bank` when that Bank exists in the refreshed root Bank index;
2. otherwise use Bank `000` when present, or the lowest numbered Bank;
3. if there is no Bank, use the existing lowest Scene, then lowest Kit, then
   initialized-empty fallback.

AutoSave discovery is skipped when no Bank can be resolved. All later Bank
agreement tests use the effective Bank actually selected by this ladder, not a
missing numeric value retained in `settings.cfg`.

### 4.2 `/.hcnames`: identity and provenance

At the Session 059 baseline, HCNAMES already records each row's name and source.
It is the durable description of which library object supplied the resident
Bank/Scene/Kit/Instrument identity. It is not a CRC-protected payload image.

HCNAMES can participate in recovery only when its Bank row agrees with the
effective settings Bank in both normalized name and numeric source. Component
rows from a different Bank must never be applied to the selected Bank.

Missing or content-malformed HCNAMES is unavailable recovery metadata. A
case-folded duplicate or terminal root/open/read/close error is more serious:
the firmware cannot safely choose or later rewrite a unique authority, so the
HCNAMES-dependent writer/recovery path must fail closed rather than creating or
overwriting a guessed object.

### 4.3 AutoSave A/B: recoverable payload generations

Each candidate is validated without FAT timestamps. Validation requires:

- one unique case-insensitive root object with the expected filename;
- exact record length, including a positive extra-byte EOF check;
- expected magic and supported format version;
- valid commit marker;
- structurally valid control fields; and
- a whole-record CRC32C match with the stored CRC field treated as zero during
  calculation.

A structurally valid record is not automatically usable for the effective
Bank. Its embedded Bank slot and normalized Bank name must also agree. Of the
Bank-matching candidates, wrapping-newer generation wins and record A wins an
exact generation tie. A structurally valid Bank mismatch is preserved on disk
but is not eligible for boot restoration of the effective Bank.

A **complete matching AutoSave record** means a structurally valid,
Bank-matching selected record whose implemented live payload has no remaining
dirty bits. This stronger condition is required whenever AutoSave is the only
source from which a future reader would restore the whole resident state.

## 5. Boot-time order and result

The conservative boot order is:

1. mount the card and load `settings.cfg`;
2. refresh/load the existing indexes needed to resolve the effective Bank and
   its normalized name;
3. when AutoSave is ON and an effective Bank exists, run one boot-time valid
   AutoSave dual-record discovery transaction before any Bank payload is
   loaded;
4. classify HCNAMES and its Bank agreement without applying component payloads;
5. validate A and B completely and retain a matching selected record when one
   exists;
6. load the Bank through the currently implemented Session 059 library path;
7. finish the existing Bank/Scene/Kit/empty fallback before boot is considered
   complete; and
8. perform only synchronous RAM authorization/full-dirty setup at the end of
   boot. The first actual mutation drain remains asynchronous runtime work
   after audio and normal filesystem scheduling begin.

The first conservative implementation is discovery, not a reader. It records
which authorities agree and retains a safe writer source, but it does not apply
AutoSave payload bytes or perform HCNAMES component-source reconstruction.
Until the reader/reconciliation stage lands, the visible boot result remains
the existing settings-selected/effective library Bank fallback.

AutoSave OFF is a strict bypass:

- do not open or validate either hidden record;
- do not interpret HCNAMES source, type, or pending metadata;
- do not emit the boot discovery trace; and
- load the effective settings Bank through the ordinary library path.

HCNAMES names may still exist for Save UI behavior, but AutoSave-specific
metadata is neither read as authority nor written while AutoSave is OFF.

## 6. Minimal mounted-session source cache

The first implementation should retain only the information needed to resume
the already selected A/B source:

- whether a source is authorized;
- record A or B;
- generation;
- probe counter; and
- expected stored CRC32C.

The expected CRC is the previously approved four bytes of additional retained
RAM. Do not carry over the old 24-byte cache, Bank-name duplicate, HCNAMES mode
bitfield, or runtime reconciliation flags. Reuse existing retained state where
possible and audit the exact linked RAM delta before implementation. If any net
retained allocation beyond the already approved CRC and the unavoidable
winner scalars is required, identify its owner, lifetime, region, and byte count
for approval before coding.

Bank agreement is established by discovery, checked again from the source
header during the copy stream, and revoked explicitly when resident Bank
identity changes. There is no need to duplicate the eight-byte Bank name in a
mounted cache merely to optimize the ordinary path.

The cache is populated only by:

- successful boot discovery of a Bank-matching winner;
- the existing full validation path when it finds an authorized winner; or
- final-sync success of a newly published target.

It is invalidated by:

- fresh card/mount initialization;
- AutoSave disable or subsequent re-enable;
- a successful operation that changes resident Bank slot or name;
- a source open, length, header, Bank-identity, or CRC contradiction; or
- any explicit filesystem/card reset that invalidates mounted-card state.

It is not invalidated by an ordinary target write/sync failure because the
selected source was not modified. It is also not cleared by generic facade
completion, Menu entry/exit, a clean transaction, or a continuation.

## 7. Normal runtime AutoSave transaction

When the mounted source is authorized, the existing writer enters at the
winner-mask step rather than at A/B candidate discovery:

1. open the cached source and OR its complete mutation mask into the canonical
   SRAM mask;
2. if the combined mask is clean, close and finish without a generation;
3. atomically take and capture the existing bounded set of live values;
4. open the cached source for the existing complete copy;
5. create the inactive peer with commit invalid;
6. stream the source once, accumulating the source CRC before transformation
   and the target CRC after transformation;
7. require exact length and immediate EOF after the expected final byte;
8. require the physical source CRC field, recomputed source CRC, and cached
   expected CRC all to agree;
9. complete the existing target data sync, CRC publication/sync, commit-last,
   and final-sync sequence; and
10. only after final sync, promote the target index, generation, probe, and CRC
    into the mounted cache.

The source CRC calculation uses the same convention as record validation: the
physical CRC field bytes are logically zero. The target CRC is calculated from
the exact transformed logical target, also with its CRC field zero. This keeps
the existing protection against partial writes and adds detection of a cached
source altered after boot without another full pre-copy validation pass.

If source validation fails during the copy, the inactive target remains
uncommitted, all captured mutation positions are restored, the cache is
invalidated, and the existing slow validation/setup path is scheduled. Mask
bits already ORed from the suspect source remain conservatively dirty; they are
never subtracted from newer SRAM work.

If the target fails while the source remains proven intact, captured mutation
positions are restored but the source cache remains authorized for retry.

## 8. Exceptional runtime paths stay slow

The first implementation optimizes only the normal case. A missing cache uses
the existing Session 059 complete A/B validation and recovery path rather than
a new generalized setup machine.

### 8.1 Bank identity change

A successful Bank Load or Bank Save that changes slot or normalized name must
invalidate the old cache and mark the complete resident Bank dirty. The slow
path may validate and copy-forward a structurally valid old-Bank record exactly
as Session 059 already does, provided the complete dirty mask makes the target
describe the new Bank before publication. Its successful target final sync
then establishes the new cache.

Do not start this work until the foreground Bank operation has reached its
existing durable completion boundary. A same-Bank Load or Save retains the
cache.

### 8.2 AutoSave OFF and re-enable

Turning AutoSave OFF disables mutation tracking/writer admission, invalidates
the cache, and discards AutoSave work according to the existing policy. It does
not delete either record.

Re-enabling AutoSave invalidates every prior source assumption and begins from
the complete current resident state:

- immediate policy/setup state changes and the complete-dirty mark are bounded
  RAM operations;
- every filesystem validation, ensure, recovery, and drain step is queued and
  pumped asynchronously through `filesystem_tick()`; and
- the first drain follows the same normal mutation path once setup has found or
  created a safe source.

No runtime re-enable helper may pump a blocking filesystem loop.

### 8.3 Source contradiction

AsyncFATFS continues to handle in-progress work and its own retry behavior.
AutoSave classifies only a terminal open/read/seek/close result, wrong length,
changed header/Bank identity, or CRC mismatch. Those outcomes invalidate the
cache and enter the existing slow path. Unsupported powered-on card removal is
not turned into a new recovery protocol.

## 9. Final authority and reconciliation rules

These rules are settled design requirements even though applying AutoSave
payloads is deferred beyond the first speedup.

| AutoSave policy and boot evidence | Required boot authority |
|---|---|
| AutoSave OFF | Load only the effective settings/fallback Bank. Ignore hidden records and HCNAMES recovery metadata. |
| Matching HCNAMES Bank and complete matching AutoSave | Future reader uses AutoSave as the payload baseline; resolvable HCNAMES component differences override it. |
| Matching HCNAMES Bank but no usable matching AutoSave | Load the effective library Bank, then reconstruct resolvable components from HCNAMES sources. |
| Missing/malformed HCNAMES and complete matching AutoSave | Future reader may restore the complete AutoSave record. Until that reader exists, load the effective library Bank. |
| Missing/malformed HCNAMES and no complete matching AutoSave | Load the effective library Bank. |
| HCNAMES and/or AutoSave Bank identity disagrees with the effective Bank | Do not apply the disagreeing authority; use the remaining matching authority or the effective library Bank. |
| No effective Bank | Use the existing lowest Scene, lowest Kit, then initialized-empty fallback; do not run AutoSave discovery. |

A duplicate HCNAMES or hidden-record filename, or terminal I/O that prevents a
unique classification, is not equivalent to absent content. Do not create or
overwrite an object based on that ambiguity. Continue only through a safe
ordinary library fallback when the facade and indexes remain usable, report the
storage error, and leave the affected AutoSave writer/recovery path
unauthorized.

### 9.1 Component precedence

Once settings, HCNAMES, and the selected AutoSave record agree on the Bank:

- HCNAMES wins for a component whenever its name or source differs from the
  AutoSave metadata;
- for a direct Instrument source, its registered type/file extension is also
  part of the comparison;
- HCNAMES also wins when the corresponding AutoSave live payload remains
  dirty; and
- the HCNAMES source must resolve to the exact expected slot, normalized name,
  and registered Instrument type. A different object occupying the same number
  is not an acceptable substitute.

The future `pending_autosave` field records whether a completed foreground
operation is known not yet to have been incorporated into a later AutoSave
generation. It is durable handoff and clear evidence, not the component-winner
gate. A resolvable HCNAMES/AutoSave component difference remains meaningful and
HCNAMES wins even when the pending field is zero.

If an HCNAMES Scene, Kit, or Instrument source selected by these rules cannot
be resolved or loaded, invalidate the affected resident Scene, load that Scene
as empty, and show the existing storage error. Do not silently substitute the
older AutoSave component after HCNAMES has identified a newer foreground
operation.

## 10. Later focused stage: durable Load/Save handoff

The Session 059 behavior that defers some HCNAMES updates until Menu exit is a
real bug, but repairing it does not belong in the first autosave speedup patch.
It receives its own implementation and hardware-acceptance stage.

The settled completion contract is:

- a Save is not complete until its object is durable, its affected `.hcindex`
  has been rebuilt and synced, the corresponding resident scope is marked
  dirty, and HCNAMES has been rewritten and synced;
- a Load does not change its source library namespace, so it must have loaded
  and validated the index used for selection rather than rewriting an
  unchanged index after every selection; it is not complete until the payload
  is resident/applied, its scope is dirty, and HCNAMES is rewritten and synced;
- Bank Load/Save also requires a synced `settings.cfg` whose `active_bank`
  agrees before reporting completion; and
- Morph Save is an export projection and does not change resident identity or
  HCNAMES provenance.

This deliberately refines the earlier shorthand that both metadata files must
be rewritten after every Load. Save changes a library namespace and therefore
must rebuild its affected `.hcindex`. Load does not change that namespace; its
index obligation is the successful, validated index read that supplied the
selected source. Rewriting an unchanged index after every scroll-driven Load
would add unrelated SD work and latency without creating a new durable fact.

Stage B uses the existing HCNAMES form. When AutoSave is ON, its completion
publication makes the current name and source durable. Stage C later extends
that same publication with direct-Instrument type where needed and
`pending_autosave=1`. When AutoSave is OFF, source/type/pending are neither read
as boot authority nor written; HCNAMES continues only its name-register role
for later Save UI.

Scene and Bank Load remain finite accepted operations. Kit and Instrument Load
remain scroll-driven:

- begin loading the highlighted selection without requiring OK;
- continue observing encoder/input while a load is in flight;
- when the desired selection changes, allow the safe current I/O boundary to
  finish and start the newest desired selection;
- publish/apply and rewrite HCNAMES only for the latest stable selection; and
- defer exit, type change, or another Load/Save menu request until the current
  stable payload and HCNAMES publication have completed.

All Saves, including Kit and Instrument Save, begin with OK/OW and retain their
finite input-locked behavior through object, index, HCNAMES, and any Bank
settings durability tail.

This stage should add only the smallest foreground-transaction admission guard
needed to prevent settings, trace, or AutoSave from claiming the filesystem
between one user operation's payload and HCNAMES halves. It must not introduce
a second general queue or a new filesystem ownership layer.

## 11. Later focused stage: source metadata and future reader

Only after the v1 speedup and foreground HCNAMES completion have independent
hardware acceptance should the on-card provenance contract change.

That later stage may:

- append a fixed 129-row source table to a new AutoSave format version while
  preserving all existing header/mask/payload offsets;
- add HCNAMES direct-Instrument type and `pending_autosave` fields, using spare
  bits in the existing packed source register rather than adding a per-row RAM
  array;
- write current names and sources into each full-copy AutoSave generation;
- clear only the HCNAMES pending fields incorporated by a successfully
  final-synced generation; and
- implement the future boot reader at the already established point after
  settings/index resolution and AutoSave discovery but before Bank loading is
  declared complete.

Pending is cleared only after the new AutoSave record is durable. If power is
removed after a Load/Save HCNAMES sync but before AutoSave runs, HCNAMES remains
pending and wins on the next boot. If power is removed after target final sync
but before pending clear, both authorities describe a safe result and the
still-pending HCNAMES entry is harmless. A failed pending-clear rewrite must
not invalidate the already committed AutoSave record.

Do not introduce this format/read/reconciliation stage merely to make the
runtime winner cache work. The cache requires only the existing v1 header and
CRC contract.

## 12. Tracing and diagnostics

Retain trace stage `V` as the boot-time dual-record validation result. With
AutoSave ON and an effective Bank, it should occur once during boot. It must no
longer be emitted by every normal runtime transaction.

If exceptional runtime full validation needs its own retained trace, use a
separate symbol such as `Q`; do not reuse `V` and make boot discovery ambiguous.
A normal runtime transaction and its continuations need no discovery symbol.

The boot trace should be able to distinguish:

- no valid record;
- selected A or B;
- valid record present but no effective-Bank match;
- HCNAMES absent/malformed/mismatched;
- duplicate/terminal setup error; and
- AutoSave OFF/no effective Bank, for which no `V` is emitted.

Diagnostics must observe the actual implementation without adding synchronous
file writes or changing its scheduling behavior.

## 13. Staged implementation order

### Stage A — Session 059 boot discovery and runtime cache

1. Start from `dcbd400`.
2. Extract/reuse the existing bounded A/B validator as one boot-only operation.
3. Resolve the effective Bank before validation and choose only a matching
   winner for the fast cache.
4. Classify HCNAMES Bank agreement without changing its grammar or applying
   component sources.
5. Add the minimal mounted-session winner state and approved expected CRC.
6. Enter the existing writer at mask merge when the cache is authorized.
7. Compute source and target CRCs during the one required full-copy stream.
8. Promote the cache only after target final sync.
9. Use the existing complete validator/setup for every cache-miss exceptional
   path.
10. Reserve `V` for boot and distinguish any exceptional validation as `Q`.

Stage A ends after build, RAM, card-fixture, timing, corruption, and power-cut
acceptance. Do not begin the next stage because Stage A merely builds.

### Stage B — foreground Load/Save durability

Repair HCNAMES/index completion and Kit/Instrument scroll transaction behavior
on top of the accepted Stage A result. Keep this patch independent from the
future reader. Test every Load/Save type, immediate power-off after reported
completion, rapid scroll/supersession, exit deferral, and injected metadata
failure.

### Stage C — provenance format and pending handoff

Introduce the AutoSave source table and extended HCNAMES fields as one explicit
format change. Make a normal out-of-menu AutoSave generation capture the
pending component and clear pending only after target final sync. Test each
power-loss boundary before enabling a reader.

### Stage D — boot reader and component reconciliation

Implement payload application and HCNAMES component overlays using the
authority table in section 9. This stage occupies the already established boot
position and must not reopen Stage A's runtime writer design.

Each stage receives a separate detailed code plan, code review, build/map
record, card verifier update where relevant, and hardware acceptance before the
next begins.

## 14. Acceptance criteria for Stage A

### 14.1 Correctness

- AutoSave ON with an effective Bank performs exactly one complete boot A/B
  validation before Bank payload loading.
- AutoSave OFF and no-effective-Bank boots perform no hidden-record I/O and
  emit no `V`.
- Missing, short, overlong, uncommitted, wrong-version, malformed, and
  CRC-corrupt records are rejected.
- Matching records outrank valid Bank mismatches; wrapping generation order and
  the A tie rule remain correct.
- The settings/HCNAMES/AutoSave Bank classifications are visible and do not
  alter the temporary Session 059 library fallback.
- A normal mutation transaction does not reread and validate A and B.
- A clean cached source does not produce an empty generation.
- A target becomes the cache source only after final sync.
- Target failure preserves the old source cache and restores captured work.
- Source length/header/Bank/CRC contradiction invalidates the cache, leaves the
  target uncommitted, restores captured work, and reaches the existing slow
  validation path.
- Bank transition and AutoSave re-enable use the slow path and a complete dirty
  mask; same-Bank operations preserve the fast cache.
- The first post-boot and post-enable drain is asynchronous.

### 14.2 Retained protection

Cut power during target write, after data sync, during CRC publication, after
CRC sync, after commit write, and before/after final sync. On the next boot,
the old source or the completely published new target must validate. The
mounted cache is volatile and supplies no boot authority after reset.

After boot discovery, separately change a non-CRC source byte and the physical
stored CRC field in a card fixture. The runtime copy must reject both before
target CRC/commit publication because the recomputed source CRC, physical
stored field, and cached expected CRC no longer all agree.

### 14.3 Performance and regression

- Compare Session 059 and Stage A traces from writer admission through mask
  merge and final completion.
- Confirm the normal trace contains no runtime `V` and no two full candidate
  validation streams.
- Confirm the full source stream and full target write remain; do not claim a
  section-write speedup.
- Repeat existing Scene, Kit, Instrument, and Bank Load/Save tests to prove the
  speedup patch did not alter Menu or payload behavior.
- Record the exact logging-on and logging-off image/RAM deltas. No Pattern-
  reserved SRAM1 or delay-line-reserved DTCM may be consumed.

## 15. Completion boundary

The conservative S060 speedup is complete when Stage A is hardware-accepted.
It does not claim that AutoSave payloads are restored at boot or that the
HCNAMES foreground-completion bug is fixed.

The overall settings/HCNAMES/AutoSave design is complete only after Stages B,
C, and D are separately implemented and accepted. Keeping those completion
claims separate prevents another working validation optimization from becoming
a simultaneous record-format, Menu, Bank lifecycle, and boot-reader rewrite.
