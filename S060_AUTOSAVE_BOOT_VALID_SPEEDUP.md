# Session 060 — Boot-Time AutoSave Validation and Runtime Winner Tracking

Status: implementation applied and verified by the final clean build/static
checks recorded below. Hardware/media acceptance remains unclaimed.

## Implementation notes — 2026-09-01

- The v2 record contract is implemented with the existing 30,848-byte payload,
  the 258-byte packed source table at offset 34,768, and an exact 35,026-byte
  record CRC/validation boundary. Initial-record formatting and runtime
  transforms now receive the mounted source/type snapshots explicitly.
- The filesystem facade now owns one exact 24-byte mounted authorization cache,
  one dedicated 129-row HCNAMES mirror, a single blocking V discovery pass, and
  the runtime Q discovery/revalidation path. Root folded-name duplicates and
  terminal setup errors are classified before candidate files are opened; the
  boot gate suppresses HCNAMES publication when setup evidence is unsafe.
- HCNAMES parsing accepts legacy OFF/name-only and two-field ON rows, while ON
  four-field rows enforce source/type/pending grammar. Runtime drains validate
  the selected source stream and target CRC before promotion; pending rows are
  cleared in a post-promotion HCNAMES continuation and reported with H flags.
- Boot preserve mode now reconstructs Scene -> Kit -> typed Instrument sources
  before ordinary payload loading can overwrite the resident state. Normal
  Load/Save completion uses the durable object -> index -> HCNAMES -> callback
  sequence, with settings.cfg following Bank HCNAMES publication.
- Menu exit-time resident-name batching was removed. Stable Kit/Scene/Instrument
  publication is targeted from the accepted request coordinates after bounded
  runtime apply, without adding a retained per-row cache.
- Final verification passed on 2026-09-01: `make clean`, `make -j2`, and
  `make img` succeeded. The logging-on link reports `text=395,476`,
  `data=404`, `bss=96,216`; the firmware binary is 395,880 bytes and the
  packaged image is 395,896 bytes. The map confirms the exact 24-byte mounted
  cache, 258-byte source register, 1,161-byte HCNAMES mirror, 2,048-byte stage
  workspace, 4,608-byte patch cache, and 16,384-byte DEV trace ring.
- Source contradictions now preserve the existing valid HCNAMES mirror through
  one folded singleton proof and one queued Q result; disappearance or root
  ambiguity remains a setup error and cannot trigger an identity-changing
  rebuild. Runtime contradictions enter Q directly, while boot ensure/recovery
  consumes the preserved mirror without rereading it into the same storage;
  captured payload offsets still roll back before any clear-pending failure is
  reported.
- `PYTHONPYCACHEPREFIX=/private/tmp/helicase_pycache python3 -m py_compile`
  passed for the decoder, verifier, and image builder; the verifier CLI smoke
  check and `git diff --check` passed. No card fixture or hardware reboot/
  remount test exists in this checkout, so those acceptance gates remain open.

## 0. How to use this plan

This recipe is based on the current tree, not the older CRC-split premise.
Line numbers below are the pre-implementation lines inspected for Session 060;
they will move as edits are applied, so the named function, enum, or comment is
the durable anchor. Every production edit must receive the adjacent comment
contract listed here in both its `.c` implementation and its `.h` declaration
when the symbol is public.

The required comment fields are:

- **What:** the state or operation the code owns.
- **Why:** the invariant or failure window that requires it.
- **Inputs:** caller state, on-card state, and validity preconditions.
- **Outputs:** RAM/on-card state, callback status, and durability boundary.
- **Accessors:** the ordinary functions used to read or change the state.
- **Affiliates:** the other functions/files whose contract depends on it.

The implementation is intentionally conservative about the existing systems:
AsyncFATFS remains the sole cooperative I/O layer; filesystem.c remains the
sole filesystem facade; the existing Bank/Scene/Kit/Instrument formats and
Preset load/save requests remain; Autosave.c remains the sole mutation-mask
owner; and the inactive-target full-copy, data sync, CRC sync, commit-last, and
final-sync safety envelope remains. The format addition is limited to source
metadata needed for boot reconciliation. There is no split CRC and no surgical
section update.

The following earlier proposals are superseded and are not implementation
options in this plan: per-section CRCs or stale-section tables; repeated A/B
validation in normal runtime transactions; a global HCNAMES revision counter;
synchronous AutoSave-file writes inside Load/Save completion; exit-time batched
HCNAMES publication; and OK-triggered normal Kit/Instrument Load. The retained
design is one boot discovery, one mounted source cache, full-copy A/B runtime
publication, per-row HCNAMES pending markers, and scroll-triggered coalesced
Kit/Instrument Load.

## 1. Final behavior and terminology

### 1.1 Normal boot

The **boot-time valid AutoSave dual-record discovery transaction** runs once on
an AutoSave-ON boot that resolves an effective Bank, after `settings.cfg` has
supplied `active_bank` and the AutoSave setting and after the root indexes can
resolve that Bank name, but before any Bank payload is loaded. AutoSave-OFF and
no-Bank fallback boots do not inspect the hidden records and emit no discovery
trace. A shortened reference such as **boot-time AutoSave discovery** or
**dual-record AutoSave validation transaction** means this same operation.

The operation reads `/.hcnames` when AutoSave is ON, validates both hidden A/B
records in full, and retains the selected structurally valid record in mounted-
session RAM. It does not apply AutoSave payload data in S060; that reader remains
a later session. It establishes the exact insertion point and metadata needed
by that reader.

After discovery, boot loads resident state using this temporary first-fallback
path:

1. Resolve the settings-selected Bank. If that slot is absent, choose the
   lowest numbered Bank (`000` when present), then the lowest Scene, then the
   lowest Kit, then leave the initialized empty state.
2. If AutoSave is OFF, ignore hidden records and HCNAMES recovery metadata and
   load the effective settings/fallback Bank through the ordinary library path.
   That path may preserve or repair HCNAMES names for later Save UI, but reads
   no source/type/pending authority and emits name-only rows.
3. If AutoSave is ON and HCNAMES is well formed and its Bank row agrees with
   the effective settings Bank, load that Bank as the baseline and reconstruct
   components from HCNAMES sources in Scene -> Kit -> Instrument order.
4. If HCNAMES is absent, malformed, or Bank-mismatched, load the effective
   settings Bank without HCNAMES overlays, then replace the unavailable/stale
   singleton with a register generated from the settled resident Bank before
   AutoSave ensure/recovery. That replacement uses the four-field form and
   marks every present row pending.
   A future reader may instead restore a complete matching AutoSave record at
   this point; S060 must not claim that it did so.
5. A failed HCNAMES component source invalidates only the affected resident
   Scene, resets it to empty defaults, reports the existing storage error, and
   continues the bounded boot policy. No partially loaded Scene is published.
6. The selected Bank/fallback load and all HCNAMES source overlays finish before
   boot is considered complete.
7. Only after resident state exists, and only if singleton classification left
   setup authorized, ensure/recover the two v2 hidden records, enable tracking,
   and synchronously mark the complete resident Bank dirty. A setup error still
   permits the bounded Bank/HCNAMES fallback above but leaves tracking off. The
   first authorized drain is ordinary asynchronous runtime work after the boot
   gate is released.

The Bank loader therefore has two explicit boot-only HCNAMES modes while
AutoSave is ON. **Preserve mode** is used when discovery found a well-formed,
Bank-matching register: Bank baseline Load must neither clear, reload, overlay,
nor rewrite that mirror before component reconstruction. **Rebuild mode** is
used for an absent, content-malformed, or Bank-mismatched singleton: Bank Load
starts from an empty mirror, loads the settings/fallback Bank, and durably
publishes Bank direct provenance plus child inheritance and pending markers.
A folded duplicate or terminal HCNAMES I/O error authorizes neither mode; Bank
fallback may continue only without AutoSave tracking, as described below.

### 1.2 Normal runtime AutoSave transaction

The normal runtime transaction uses the mounted-session selected source. It
does not reread and validate A and B before every generation. It still:

1. reopens the selected source and merges its complete on-file mutation mask;
2. atomically takes/captures the bounded current mutation set;
3. opens the source again for the one required full-copy stream;
4. accumulates a source-integrity CRC while accumulating the transformed target CRC;
5. verifies exact length, an extra-byte EOF check, the source file's physical
   stored-CRC field, and the recomputed source CRC against the retained expected
   CRC before target publication;
6. writes and syncs target data while commit is invalid, publishes/syncs the
   target CRC, writes commit last, and final-syncs;
7. promotes the target to the mounted-session source only after final sync; and
8. durably clears HCNAMES `pending_autosave` markers incorporated by that
   generation before reporting the complete transaction.

A subsequent generation in a multi-generation drain is a **continuation
AutoSave transaction**. It follows the same path from the newly promoted source.
The phrase “warm transaction” is not used.

### 1.3 Safety premise

Boot validation still rejects a missing, short, overlong, wrong-version,
malformed, uncommitted, or CRC-inconsistent record. Runtime still never modifies
the selected source. The selected source's retained CRC adds the four approved
RAM bytes and restores in-session silent-source-change detection without a
second streaming pass: the bytes already read for the full copy feed both the
source-integrity and transformed-target CRC accumulators.

If a source open/read/length/CRC contradiction occurs after AsyncFATFS has
finished its own cooperative retry behavior, the target remains uncommitted,
captured mutation bits are restored, the mounted source is invalidated, and an
exceptional rediscovery/setup transaction is queued. Powered-on card removal is
not supported; no card-hot-swap identity mechanism is introduced.

## 2. On-card contracts

### 2.1 AutoSave record v2

S060 bumps the binary version from 1 to 2 because source metadata is necessary
to distinguish a recently loaded/saved component from older AutoSave payload.
The existing offsets through the end of payload remain unchanged:

| Region | Absolute offset | Bytes | Contract |
|---|---:|---:|---|
| control header | 0 | 64 | magic, v2, commit, generation, CRC, probe |
| mutation mask | 64 | 3,856 | one bit per byte of the existing payload |
| Bank + 16 Scene payload | 3,920 | 30,848 | unchanged v1 payload geometry |
| source table | 34,768 | 258 | 129 little-endian `uint16_t` logical sources |
| **record total** | 0 | **35,026** | exact size checked by validation |

The source-table row order is identical to HCNAMES: Bank row 0, Scene rows
1..16, Kit rows 17..32, and six Instruments per Scene at rows 33..128. Stored
values are only `0..999`, `INHERIT=1000`, `UNKNOWN=1001`, or
`INSTRUMENT_DIRECT=1002`. HCNAMES parser flags, its pending bit, and its
transient rewrite bit are never serialized into the binary table. Instrument
type is already present in each 192-byte Instrument payload and is therefore
not duplicated in the source table.

The whole-record CRC32C covers all 35,026 logical bytes with header CRC bytes
12..15 treated as zero, exactly as before. The fixed CRC work budget remains
128 bytes per foreground pass. v1 records are invalid under v2; there is no
mixed-version selection or migration. After the resident Bank is loaded, the
existing safe B-then-A recovery sequence replaces an all-v1/all-invalid pair,
making B fully durable before touching A.

Every formatter and runtime transform refreshes all 129 source entries and all
Bank/Scene/Kit/Instrument name cells from the current HCNAMES mirror. This
metadata refresh is independent of payload mutation bits. Name cells written
from HCNAMES have their output mutation bits clear. A zero-payload baseline's
recovery mask uses exactly the existing `autosave_markResidentBankDirty()`
geometry: live Bank fields plus each present Scene's implemented non-Pattern
scope. HCNAMES-overlaid names are already current and clean; Pattern, reserved,
nonexistent, and currently unimplemented Effect bytes remain outside the mask.
“Complete resident dirty” below always means that existing geometry.

### 2.2 HCNAMES grammar and packed RAM representation

When AutoSave is ON, each of exactly 129 rows is:

```text
name<TAB>source<TAB>type<TAB>pending_autosave<LF>
```

- `name` is the current trimmed eight-cell display stem.
- `source` is `000..999`, `-` for inheritance, `?` for unknown, or `@` for a
  direct root Instrument file.
- `type` is `drm`, `snr`, `cym`, or `hat` only for `@`; it is `-` for Bank,
  Scene, Kit, inherited, and unknown rows. Parsing/formatting uses the existing
  `storage_instrumentTypeFromText()`, `storage_instrumentTypeToText()`, and
  registry definitions rather than adding a second type registry.
- `pending_autosave` is exactly `0` or `1`.

Source validity is row-specific. The Bank row accepts a numbered Bank or `?`;
Scene and Kit rows accept a numbered source, `-`, or `?`; Instrument rows accept
`@`, `-`, or `?`. A numbered Instrument source and `@` on a non-Instrument row
are malformed because neither has an unambiguous library/type interpretation.

When AutoSave is OFF, HCNAMES remains a name register for Save UI and future
Save names, but source, type, and pending state have no recovery authority.
Readers take only the text before the first tab and ignore metadata; writers
emit name-only rows. Accepted runtime Loads may still update the existing
packed provenance words in volatile RAM because their request supplies that
information, but it is neither sourced from nor published to HCNAMES while OFF.
This lets OFF->ON setup describe the currently resident state before starting
its asynchronous full drain. No new AutoSave-specific name cache is created.

Compatibility rules while ON are explicit:

- name-only legacy rows parse as source UNKNOWN, neutral type, pending 0;
- the current two-field `name<TAB>source` form parses with neutral type and
  pending 0, except `@` without a type is unavailable/UNKNOWN;
- a four-field row is strict: invalid source, invalid registered type, an
  inappropriate non-`-` type, invalid pending text, too many fields, extra
  rows, or a row count other than 129 makes the whole HCNAMES file malformed;
- missing and malformed HCNAMES are both unavailable to boot recovery, but an
  AsyncFATFS operation still in progress is neither.

A content-malformed single HCNAMES file is closed, marked unavailable, and does
not prevent A/B structural classification. It is treated as missing recovery
metadata and replaced only after the effective Bank has loaded successfully; it
is never truncated merely because parsing failed. A folded duplicate HCNAMES
object or a terminal root/open/read/close error is a setup error: firmware cannot
choose or later clear one authoritative register safely. Boot may still load
the settings Bank if the facade remains usable, but AutoSave tracking remains
unauthorized and the existing filesystem error is shown/logged.

The existing `fs_resident_source[129]` remains exactly 258 bytes. Each word is
redefined as:

```text
bits  0..9   logical source (0..1002)
bits 10..12  type code (0 neutral; registered type + 1)
bit      13  reserved, always zero on parse/write
bit      14  durable pending_autosave
bit      15  transient HCNAMES rewrite-dirty flag
```

All code must use masks/accessors; no caller may compare the packed word
directly with a logical source token. This adds the approved type and pending
state without another 129-byte or 258-byte array.

### 2.3 Durable load/save handoff

A normal Load or Save that changes resident identity is not complete until its
payload operation and required index work have succeeded and HCNAMES has been
rewritten and synced with the new name/source/type and `pending_autosave=1`.
For Bank Load/Save, the same completion boundary also includes a synced
`settings.cfg` carrying the new `active_bank`; otherwise the next boot would
reject the newly synced HCNAMES Bank row under the required Bank-agreement rule.

For a Save that changes a namespace, the order is:

1. write and sync the saved object;
2. rescan/rebuild and sync that object's `.hcindex`;
3. mark the corresponding resident AutoSave scope dirty for normal saves;
4. write/sync HCNAMES with new identity and pending marker; and
5. for Bank Save, write/sync the existing settings schema with the new
   `active_bank`; and
6. invoke the final filesystem/Preset completion callback.

The `.hcindex` precedes HCNAMES so the final durable pending row never points to
a successfully reported Save whose index is still known stale. A Load does not
rewrite an unchanged namespace. Its entry index must already have loaded
successfully before selection; selected typed-Instrument load owns the existing
missing/malformed repair, while root indexes rely on the boot/Save refresh
contract described below. A stable Load marks the payload scope dirty, applies
it, then writes/syncs HCNAMES pending before UI completion. Bank Load additionally
chains the settings write after HCNAMES and before its callback; Scene, Kit, and
Instrument operations do not change `active_bank`.

Thus “required `.hcindex` work” is operation-specific and deliberate: every
normal Save physically rescans and rewrites the affected index before HCNAMES;
a Load proves its index before using the source row but does not rewrite an
unchanged namespace after every encoder selection. For root Bank/Scene/Kit,
“proves” means a successful read of the index already refreshed during boot (or
by the last Save); the selected typed-Instrument path additionally owns its
existing missing/malformed repair. The dedicated HCNAMES
mirror must also eliminate any old read-only index reload whose only purpose was
to recover a browser cache previously borrowed by HCNAMES.

Normal Instrument, Kit, Scene, and Bank Save update resident identity and force
an ordinary AutoSave generation even when musical values are byte-identical:
the relevant whole-object marker is set before HCNAMES publication. Morph Save
is an export projection and deliberately preserves resident identity, so it
requires its physical object and index durability but no HCNAMES/pending change
and no identity-only AutoSave generation.

After a target's final commit sync, the AutoSave transaction clears only the
pending markers represented by the HCNAMES image it placed in that generation.
The filesystem facade remains exclusively owned through this clear, so a new
Load/Save cannot interleave. If clear fails, the new AutoSave record remains the
selected valid source, the retained RAM image keeps its pre-clear pending bits,
captured payload offsets are restored, and the ordinary retry path writes
another generation and retries the clear. The on-disk singleton is not claimed
valid after a failed write-capable clear; its valid/invalid state is determined
at the next discovery, while the committed A/B record remains the recovery
authority for the future reader. The clear operation must not mark payload dirty
or set pending again.

The combined handoff records enough durable evidence to resolve these
immediate-power-off windows once the AutoSave payload reader is implemented:

- before HCNAMES sync, the foreground operation has not reported completion;
- for Bank Load/Save, after HCNAMES sync but before settings sync, the operation
  likewise has not reported completion;
- after HCNAMES sync (and required Bank settings sync) but before AutoSave,
  HCNAMES pending and source win at boot;
- after AutoSave commit but before HCNAMES clear, pending plus the record's
  payload mask/source table identify the safe source;
- after clear, the selected record contains the current names/sources and either
  current payload bytes or dirty bits requiring source reconstruction.

S060 itself applies only the HCNAMES side of that evidence. In particular, the
required “operation complete, power off without leaving Load/Save” case is
covered now because page suppression prevents AutoSave/pending clear and the
synced HCNAMES source reconstructs the component at boot. If power instead fails
during the later in-place pending-clear rewrite and leaves HCNAMES malformed,
S060 validates and retains the new A/B source but still takes the documented
settings-Bank fallback; applying that record is explicitly future-reader work.

## 3. Boot reconciliation details

### 3.1 Discovery result retained in RAM

Add one filesystem-owned mounted-session object, with field order fixed to make
the allocation auditable:

```c
typedef struct {
    uint32_t generation;
    uint32_t crc32c;
    uint16_t bank_slot;
    char bank_name[8];
    uint8_t source_index;
    uint8_t probe_counter;
    uint8_t discovery_state;
    uint8_t source_valid;
} filesystem_autosave_source_cache_t;
```

Require `_Static_assert(sizeof(filesystem_autosave_source_cache_t) == 24u)`.
The approved source CRC is the second `uint32_t`; it adds four bytes. The cache
is never serialized. It is populated only by completed discovery, a fully
durable ensure/recovery baseline, or a target final-sync success.

`discovery_state` is a private bitfield in this existing byte, not a second
flag/allocation: CLASSIFIED, HCNAMES_PRESERVE, HCNAMES_REBUILD, HCNAMES_ACTIVE,
SETUP_ERROR, and BANK_TRANSITION_PENDING. Preserve and rebuild are mutually
exclusive, but SETUP_ERROR
is orthogonal: an A/B duplicate or terminal candidate error disables the writer
without discarding an independently valid HCNAMES recovery image. The public
complete accessor reports CLASSIFIED; the blocking wrapper reports failure when
SETUP_ERROR is set. The setter adds HCNAMES_ACTIVE before baseline Bank Load,
and the owning completion clears it on every exit, changes successful REBUILD
to PRESERVE, or records SETUP_ERROR. `source_valid` means the remaining fields
identify one exact structurally valid file. Structural validity, HCNAMES mode,
Bank identity agreement, and writer authorization remain separate. No
additional persistent boot-suppression byte is allocated.

BANK_TRANSITION_PENDING is runtime-only. The Bank commit boundary first compares
its target to the old BankData identity, disables tracking/clears `source_valid`
on a change, and sets this bit before changing BankData. HCNAMES/settings success
converts it into the existing asynchronous setup request and clears the bit;
any operation error clears it without queuing setup. Boot mode never sets it.

Clear this cache at card/mount initialization, OFF-to-ON setup, a runtime Bank
identity transition's eventual Q admission, and a terminal source contradiction.
At the earlier Bank commit boundary clear only source authorization and set
BANK_TRANSITION_PENDING so the durability tail cannot lose its trigger. Do not
clear the cache at ordinary facade completion or target failure.
AutoSave OFF revokes authorization; re-enable starts asynchronous setup and does
not reuse the pre-disable cache.

### 3.2 Candidate selection

Perform one folded root enumeration that classifies HCNAMES and both hidden
candidates as absent, unique, or duplicate before opening any of them. Absence
is an ordinary invalid AutoSave candidate; a NULL open after unique presence or
a terminal enumeration/read/close result is an I/O setup failure, not a CRC
failure. Any folded duplicate of A or B is a setup error: even if the peer is
valid, alternating publication cannot safely select a unique target, so no
mounted source or writer authorization is published. Then validate unique A
followed by unique B with the existing bounded validator, including the
extra-byte overlong proof. A structurally valid record agreeing with the effective settings
Bank outranks a structurally valid mismatch. Within the same agreement class,
wrapping-newer generation wins; A wins equal-generation ties. Retain the
winner's stored CRC as well as its index/generation/probe/Bank identity.

A valid mismatch is not corruption and may remain the full-copy source after
the fallback Bank is made completely dirty. It is not eligible for the future
reader to restore as the settings Bank. If neither file is structurally valid,
post-load ensure creates missing v2 records; if invalid filenames prevent
creation, B-then-A recovery establishes the source only after resident state and
HCNAMES exist. Duplicate target names are never “recovered” by an ambiguous
write-capable open; they leave tracking unauthorized for manual card repair.

### 3.3 Component winner rules

Top-level Bank agreement requires settings/effective Bank slot and normalized
name to agree with HCNAMES Bank source/name. Once that is true, HCNAMES is loaded
top-down:

- an explicit numbered Scene source first reloads the boot-refreshed root Scene
  index and requires that slot's normalized eight-cell name to equal the HCNAMES row;
  only then does it override the Bank child. INHERIT keeps the loaded Bank child
  and requires its settled name to agree with the inherited row;
- an explicit numbered Kit source applies the same exact slot/name check against
  the boot-refreshed root Kit index before overriding the Scene's embedded Kit.
  INHERIT keeps it and requires the settled Kit name to agree;
- an Instrument `@` source uses that row's type token, loads the registered
  type index, finds the exact eight-cell stem, and loads that file; INHERIT keeps
  the parent Kit member;
- UNKNOWN is unavailable and leaves the already loaded parent result.

A numbered source/name mismatch or an INHERIT name that disagrees with its
settled parent is a component-source failure, not permission to load whatever
now occupies the slot; apply the whole-Scene reset/error rule. Blank names are
valid only for absent/UNKNOWN rows, never for a direct or inherited present
component.

If a matching AutoSave record is available for future-reader reconciliation,
HCNAMES wins a component whenever its name/source differs, whenever an
Instrument's registered type differs, whenever its pending bit is set, or
whenever the corresponding AutoSave payload bytes remain dirty. Explicit child
source wins over inheritance. S060 implements the metadata and HCNAMES
reconstruction path but does not apply clean AutoSave payload bytes.

Boot source loads run with AutoSave tracking disabled and the HCNAMES_ACTIVE
bit set before the baseline Bank request. In preserve mode, that bit also
prevents Bank/Scene/Kit/Instrument Load from preparing, invalidating,
reloading, overlaying, or rewriting the already validated HCNAMES mirror. In
rebuild mode, only the baseline Bank Load is allowed to construct and publish
the replacement register; subsequent component recovery is not entered. The
flag is cleared on every success/error exit before runtime authorization.

## 4. Runtime transaction state machine

### 4.1 Cached-source fast path

Remove candidate-validation phases 0..5 from
`filesystem_autosaveParameterDrain_tick()`. Admission copies the mounted source
cache into operation-local scalars, clears only transaction patch state, and
continues at the current winner-mask phases. A missing cache is a setup request,
not permission for the drain to guess or overwrite both files.

Keep the existing mask-open/seek/merge path. It imports the source record's
3,856-byte mask into the one canonical mask and completes read-only only when
both the mask and HCNAMES pending set are empty. A pending-only identity change
must have been paired with a whole-object dirty marker by the Load/Save contract;
there is no free-standing checkpoint writer.

Keep atomic take, bounded live gets, the 4,608-byte patch cache, rollback, and
continuation cadence unchanged.

Retain the existing Load/Save-page admission guard. Neither setup, a normal
transaction, a continuation, nor HCNAMES pending clear may begin while Menu is
on a Load or Save page. An operation already admitted is never cancelled. Work
that became due in-menu starts only after the page has been left, using the
existing short post-suppression deadline; this is the required “next normal,
out-of-menu AutoSave” boundary for clearing pending markers.

### 4.2 One copy, two CRCs

When the source is reopened for copy:

- initialize `source_crc32c` and `target_crc32c` independently;
- read at most the current 128-byte CRC budget;
- assemble the four physical source CRC-field bytes into
  `source_stored_crc32c` while their interval passes;
- update the source-integrity CRC before target transformation, using the
  source bytes exactly as stored except that header CRC field bytes 12..15 are
  fed as zero, matching the record CRC convention;
- transform header/mask/patch/name/source-table bytes;
- update target CRC from the transformed logical bytes;
- clear the physical target commit byte and write the chunk;
- after exactly 35,026 source bytes, request one additional byte and require
  immediate EOF; and
- require both `source_stored_crc32c` and the finalized recomputed source CRC to
  equal the mounted expected CRC before target CRC publication or commit.

Short input, extra input, source CRC mismatch, or terminal I/O failure uses the
source-contradiction error path: close handles, leave target ineligible, restore
captured offsets, invalidate the cache, revoke drain authorization, and schedule
exceptional discovery (`Q`). Target-write/data-sync/CRC-sync/commit-sync failure
does not invalidate the untouched source cache.

Mask bytes already OR-merged from a source that later fails its length/CRC proof
are retained as conservative over-dirty work. They are never subtracted, because
the canonical mask has no provenance bitmap and OR import cannot erase newer
RAM dirtiness. Q setup additionally marks the complete resident Bank dirty
before any newly selected peer can become a copy source.

### 4.3 Publication and HCNAMES clear

Replace the current “commit handle closed -> generic finish” handoff with an
explicit final-sync phase. Only successful `afatfs_sync()` at this phase:

- promotes target index, generation+1, probe+1, current Bank slot/name, and the
  target CRC into the mounted source cache;
- emits publication evidence without implying HCNAMES is already cleared; and
- enters the internal HCNAMES-clear chain while the facade is still owned.

If no pending rows were in the published mirror, finish normally. Otherwise
rewrite the same HCNAMES image with those pending bits cleared, final-sync it,
then report DONE. HCNAMES clear failure reports ERROR/retry but must not undo or
invalidate the already durable cache promotion. Pending bits are cleared in the
public RAM mirror only after final sync; a failed attempt retains the pre-clear
image for retry and marks physical HCNAMES durability unknown.

### 4.4 Setup and identity transitions

Boot discovery is synchronously pumped only before audio. Runtime OFF-to-ON
queues the validation/setup machine through `filesystem_tick()` immediately.
A runtime Bank slot/name change first invalidates the old source at resident
identity commit, then queues the same asynchronous machine only after its
HCNAMES and settings durability tail succeeds. A same-Bank Load/Save does not
queue setup. All runtime setup uses trace stage `Q`. Setup order is:

1. disable tracking and drain authorization;
2. clear mounted source assumptions;
3. complete the folded root singleton classification before any create-capable
   HCNAMES open, then establish one authoritative ON-form mirror according to
   the trigger:
   OFF-to-ON rebuilds and durably writes current resident names plus the volatile
   provenance established by actual OFF-mode Bank/Scene/Kit/Instrument loads,
   setting pending on every present row and UNKNOWN only where no accepted load
   supplied a source, rewriting a unique singleton or creating only after proven
   absence; a completed Bank transition reuses its just-synced pending register;
   source contradiction preserves the existing unique VALID/CLEAR_RETRY mirror
   without manufacturing an identity change; duplicate/error classification
   forbids all three write paths;
4. validate A/B, ensure/recover a safe source as needed;
5. enable tracking and mark the complete resident Bank dirty; and
6. arm the ordinary asynchronous writer.

No setup step performs a blocking runtime call. A Bank transition does not
delete otherwise valid old-Bank records; it revalidates them, retains a safe
copy source if necessary, and relies on full dirty state plus v2 sources/masks
until the new Bank is fully drained. If the trigger's required HCNAMES mode is
not safe—duplicate, terminal I/O, or a failed Bank handoff—setup fails with
tracking off rather than silently rebuilding an unconfirmed foreground action.

## 5. Complete file-by-file implementation recipe

### 5.1 `Core/Bank/Scene/Autosave.h`

#### A. v2 geometry and assertions — current lines 21–73 and 191–243

**Modify/remove:** change `AUTOSAVE_HEADER_FORMAT_VERSION` from 1 to 2; replace
the 34,768-byte record definition/assertion with source-table offset/entry/size
definitions and the exact 35,026-byte assertion. Keep mask and payload offsets
unchanged. Add assertions that 129 two-byte entries consume 258 bytes, begin at
the old record end, and end at the new record end. Define/assert the 96-entry
resident Instrument-type geometry used only for deterministic baseline masks.
Define the four wire-owned
logical source constants here (`DIRECT_MAX=999`, `INHERIT=1000`,
`UNKNOWN=1001`, `INSTRUMENT_DIRECT=1002`) so formatter, validator, filesystem,
and verifier cannot invent different numeric values.

**Comment contract:** What: fixed v2 wire geometry. Why: names alone cannot
identify the library source needed after a completed Load/Save precedes an
AutoSave. Inputs: unchanged header/mask/payload and 129 logical sources.
Outputs: exact offsets used by validation, formatter, transform, verifier, and
filesystem bounds. Accessors: format/validation/transform APIs below.
Affiliates: filesystem.c, HCNAMES row mapping, AUTOSAVE.md, verifier.

#### B. initial-image APIs — current lines 245–287

**Modify:** extend `autosave_formatInitialChunk()` and
`autosave_initialRecordCrcUpdate()` with
`const uint16_t resident_sources[129]` and
`uint16_t resident_scene_present_mask`, plus
`const uint8_t resident_instrument_types[96]`. Capture the presence mask and all
16×6 resident type enum values once when baseline preparation begins and pass
the identical snapshots to CRC preparation and physical formatting; neither
helper may reread mutable BankData/SceneData geometry during its bounded pass.
The sources argument is an immutable view of the existing packed
`fs_resident_source[]`; the formatter serializes only its low 10-bit logical
source and never pending/type/rewrite flags, so no second 258-byte array is
allocated. The implementation call sites must pass the same immutable
names/sources/mask policy to CRC preparation and physical formatting. A newly
established source whose payload is not serialized from SRAM must carry the
exact existing whole-Bank recovery-mask geometry described in section 2.1,
never falsely clean zero bytes for an implemented payload scope.

**Comment contract:** What: bounded deterministic v2 baseline synthesis. Why:
ensure/recovery must be readable after power loss before the async first drain.
Inputs: generation, Bank identity, HCNAMES names/sources, captured Scene-present
and Instrument-type geometry, CRC, interval. Outputs: matching CRC and bytes with dirty fallback
mask. Accessors: `autosave_formatInitialChunk()`,
`autosave_initialRecordCrcUpdate()`. Affiliates: post-Bank ensure/recovery,
whole-Bank marker geometry, HCNAMES mirror.

#### C. validation state — current lines 289–336

**Modify comments, retain fields:** validation now expects 35,026 bytes/v2 and
its `stored_crc32c` becomes the input copied into the mounted source cache.
`autosave_streamValidationMatchesBank()` remains the slot/name comparator.

**Comment contract:** What: one candidate's bounded structural proof and parsed
identity. Why: discovery must distinguish structural validity from settings
agreement and preserve expected source CRC. Inputs: sequential exact stream.
Outputs: validity, generation/probe/slot/name/stored CRC. Accessors: existing
begin/update/finish/match functions. Affiliates: discovery/Q setup and runtime
source-integrity CRC check.

#### D. whole-Bank dirty documentation — current lines 441–452

**Modify:** state that HCNAMES names/sources are refreshed as metadata, while
the marker covers the existing live Bank and present-Scene non-Pattern payload
required after boot setup/re-enable/Bank identity change. Remove wording that
implies baseline identity alone is enough or that Pattern/Effect ownership
changed in S060.

**Comment contract:** What: the one existing complete-resident mutation scope.
Why: setup must conservatively replace every currently implemented payload byte
without claiming Pattern/Effect support. Inputs: resident Bank/live Scene
geometry. Outputs: canonical mutation bits only. Accessors:
`autosave_markResidentBankDirty()`. Affiliates: baseline-mask synthesis and
filesystem setup completion.

#### E. transform API — current lines 475–495

**Modify:** extend `autosave_transformDrainChunk()` with the immutable HCNAMES
name mirror and logical source table. Its documented output includes name-cell
overlay, clearing corresponding output mask bits, and complete source-table
overlay in addition to header/mask/patch transformation.

**Comment contract:** What: produce one final logical target interval from one
raw source interval. Why: every committed generation must authenticate current
identity/source metadata even when only a payload subsection is dirty. Inputs:
source chunk, absolute interval, new header fields, canonical mask, patches,
names, sources. Outputs: exact bytes fed to target CRC; no I/O. Accessors:
`autosave_transformDrainChunk()`. Affiliates: filesystem full-copy loop and
HCNAMES packed-source accessors.

### 5.2 `Core/Bank/Scene/Autosave.c`

#### A. v2 initial byte/CRC generation — current lines 182–456

**Modify:** update `autosave_initialRecordByte()`, the formatter, and initial CRC
helpers for v2 size and the source table. Add a small logical-source byte helper
that masks each packed input to its low 10-bit logical source and emits the
little-endian wire word. Add a private pure live-scope predicate driven by the
captured present mask, captured 96 type values, and the existing Instrument
registry descriptor/morphable flags. Generate the baseline on-file recovery mask for all
bytes covered by the existing whole-Bank marker except the current HCNAMES-
overlaid name cells when the baseline contains zero payload, so a future reader
follows HCNAMES rather than accepting zeros. Keep Pattern, unimplemented Effect,
reserved, nonexistent, and other non-gettable bytes clean.

**Comment contract:** What: table-free deterministic byte generator for a v2
baseline. Why: CRC and physical creation must be byte-identical without a
35,026-byte buffer. Inputs: absolute byte, Bank identity, names, sources,
captured resident/live geometry. Outputs: one wire byte/updated CRC. Accessors: public
initial formatter/CRC functions. Affiliates: ensure and B-then-A recovery.

#### B. validator bounds/version — current lines 458–595

**Modify:** accept only v2 and exact 35,026-byte streams. Continue treating CRC
field bytes as zero and reject any update that crosses the record bound. Parse
each completed little-endian source-table word and enforce the same row-specific
source classes as HCNAMES: Bank number/UNKNOWN; Scene and Kit
number/INHERIT/UNKNOWN; Instrument INHERIT/UNKNOWN/INSTRUMENT_DIRECT. Keep Bank
slot/name parsing at unchanged payload offsets. No v1 compatibility path.

**Comment contract:** What: strict bounded v2 structural validation. Why: an
authenticated but semantically illegal source token cannot authorize a future
component load. Inputs: sequential bytes plus exact EOF. Outputs: valid parsed
header/Bank/source result or whole-candidate rejection. Accessors: existing
stream-validation functions. Affiliates: HCNAMES row rules, discovery, verifier.

#### C. whole-Bank marker — current lines 1367–1390

**Modify:** keep existing mutation producers and one canonical mask. Factor the
private live-scope/type-descriptor predicates used by the whole-object markers
so baseline synthesis and `autosave_markResidentBankDirty()` share the same Bank,
Scene, Kit, Instrument-type, descriptor, Morph, name-exclusion, Pattern, and
zero-Effect rules. Do not add a second mutation mask.

**Comment contract:** What: shared definition of currently gettable resident
scope. Why: the runtime marker and zero-payload recovery record must never
disagree. Inputs: payload offset/resident-present geometry. Outputs: dirty/live
classification only. Accessors: private predicate and existing marker.
Affiliates: initial formatter and future reader.

#### D. metadata-aware transform — current lines 1498–1621

**Modify:** after header and canonical-mask overlay, overlay every intersecting
HCNAMES-owned name field and clear its corresponding output mask bit, apply the
sorted live patches, and overlay every intersecting v2 source-table byte. Bounds
must use 35,026 bytes. Source bytes come from the low 10 logical bits of the
existing packed row words; high HCNAMES flags never reach the record. Retain the
monotonic patch cursor and no-I/O contract.

**Comment contract:** What: metadata-aware full-record transform. Why: every
generation must authenticate current names/sources independently of payload
mutation. Inputs: source interval, header values, canonical mask, live patches,
HCNAMES mirror/packed sources. Outputs: exact target interval and advanced patch
cursor; no I/O or canonical-mask mutation. Accessors: public transform API.
Affiliates: filesystem copy/dual-CRC loop and v2 verifier.

### 5.3 `Core/Hardware/SD/filesystem.h`

#### A. discovery/setup public contract — current lines 221–305

**Add before `filesystem_ensureAutosaveFilesBlocking()`:** the following exact
boot-only blocking discovery entry point and read-only result accessors:

```c
uint8_t filesystem_discoverAutosaveBlocking(uint16_t bank_slot,
                                             const char bank_name[8]);
uint8_t filesystem_autosaveDiscoveryComplete(void);
uint8_t filesystem_autosaveSourceValid(void);
uint8_t filesystem_autosaveSourceMatchesBank(uint16_t bank_slot,
                                             const char bank_name[8]);
uint8_t filesystem_hcnamesRecoveryAvailable(uint16_t bank_slot,
                                            const char bank_name[8]);
void filesystem_setBootHcnamesReconstruction(uint8_t active);
```

These six APIs must not be collapsed into an ambiguous “valid” boolean. The
blocking wrapper returns nonzero only when root singleton classification and
both A/B candidate classifications reached a terminal non-I/O result and no
folded duplicate makes later selection/write ambiguous; ordinary CRC-invalid/
absent records and a unique content-malformed HCNAMES still count as completed
discovery. A zero return shows/logs the retained setup error, leaves tracking
unauthorized, and permits
the settings Bank fallback only if the facade remains usable. The wrapper is
pre-audio only; the internal state machine is reused asynchronously for Q.
Like the existing blocking boot helpers, it consumes/acknowledges its own
terminal DONE/ERROR before return so a nonfatal zero result leaves the facade
idle for the Bank fallback; fatal AsyncFATFS state remains distinguishable and
must not be papered over by another request.

`filesystem_setBootHcnamesReconstruction(1)` is set before baseline Bank Load,
not merely before child overlays. Internally, a true
`filesystem_hcnamesRecoveryAvailable()` selects preserve mode; false availability
after a safely classified absent/content-malformed/Bank-mismatched singleton
selects rebuild mode. A folded duplicate or terminal-I/O classification of
HCNAMES selects neither; an A/B-only setup error leaves the independently
classified HCNAMES mode usable for Bank fallback. The control is reset on all
terminal paths.

**Modify `filesystem_ensureAutosaveFilesBlocking()` comment:** v2 size, sources,
post-load placement, exact existing whole-Bank non-Pattern baseline mask, cache
population, and no runtime drain. Remove statements that normal runtime later
validates both files.

**Modify `filesystem_setAutosaveEnabled()` comment:** OFF ignores metadata and
runtime OFF-to-ON queues full invalidating async setup. Applying the ON value
from `settings.cfg` before `fs_settings_runtime_ready` only records policy,
clears authorization, and leaves main.c's boot `V` path in sole control; it must
not enqueue Q. Add that a completed runtime Bank transition uses the same
internal setup path.

**Comment contract:** What: boot discovery/results and runtime policy boundary.
Why: main.c must choose Bank sources before payload load without accessing
filesystem internals. Inputs: resolved effective Bank identity and mounted idle
facade. Outputs: terminal discovery/cache/HCNAMES availability only, never
payload application. Accessors: the APIs above plus ensure/set-enabled.
Affiliates: main.c boot ladder, scheduler, future AutoSave reader.

#### B. packed HCNAMES metadata — current include list around lines 45–47 and
source declarations at lines 623–639

**Replace:** high sentinel definitions `0x7ffd..0x7fff` with 10-bit logical
aliases of the wire constants in `Autosave.h`; include that header so the
filesystem facade does not duplicate wire values. Keep every high-bit packing
mask private; external callers use only logical accessors. Add these typed/
pending accessors:

```c
uint8_t filesystem_residentInstrumentType(uint16_t row,
                                          instrument_type_t *type);
uint8_t filesystem_residentPendingAutosave(uint16_t row);
uint8_t filesystem_setResidentMetadata(uint16_t row, uint16_t source,
                                       uint8_t type_valid,
                                       instrument_type_t type,
                                       uint8_t pending);
```

Keep `filesystem_residentSource()`, `filesystem_setResidentSource()`, and
`filesystem_resolveResidentSource()` as logical-source accessors that mask the
packed flags. Callers must use the compound setter when source/type/pending must
be published atomically. Add
`const char *filesystem_residentName(uint16_t row)` as the generic read-only
boot/reconciliation accessor over the dedicated mirror; its pointer has the
same lifetime as the existing row-specific name accessors.

**Comment contract:** What: one packed row of source, optional Instrument type,
durable pending, and private rewrite state. Why: durable handoff must fit the
approved existing 258-byte register. Inputs: fixed HCNAMES row and validated
logical metadata. Outputs: masked logical values; no I/O. Affiliates: HCNAMES
parser/formatter, AutoSave v2 table, menu/load/save state machines.

#### C. HCNAMES request contracts — current bootstrap declaration at lines
314–329 and targeted APIs at lines 641–735

**Modify:** remove all “deferred until menu exit” and generalized-cache wording
that is obsolete under the dedicated mirror. Retain the three read-only request
signatures, but revise the normal update signatures to make their direct source
identity request-stable:

```c
bool filesystem_requestUpdateResidentInstrumentNames(
    uint16_t scene_mask, uint8_t instrument_slot, const char name[8],
    instrument_type_t type, fs_completion_cb_t cb);
bool filesystem_requestUpdateResidentKitNames(uint16_t scene_mask,
                                              uint16_t source_slot,
                                              fs_completion_cb_t cb);
bool filesystem_requestUpdateResidentSceneNames(uint16_t scene_mask,
                                                const char name[8],
                                                uint16_t source_slot,
                                                fs_completion_cb_t cb);
```

The Instrument request publishes direct `@` plus the supplied registered type;
Kit and Scene require a direct source in 0..999. Every accepted normal update
copies/overlays the exact names and metadata into the dedicated mirror before
returning, so later encoder movement cannot retarget it. It sets pending=1 in
ON mode and writes all four fields; in OFF mode it retains the supplied source/
type only as volatile provenance, keeps pending=0, and writes name-only rows.
Kit also stages its six child rows as INHERIT from the settled identity block;
Scene always stages its child Kit and six Instruments as INHERIT from the
settled identity block. All rows in either replaced hierarchy receive the same
ON-mode pending=1 handoff.
The callback runs only after final sync. Read-only requests expose current names.
Pending clear is an internal continuation of the AutoSave drain and receives no
public or Menu request API.

**Comment contract:** What: request-stable targeted resident-identity
publication. Why: mutable Menu selection and the old exit batch cannot be the
durability authority for a completed Load/Save. Inputs: exact destination mask,
direct source/name/type, and completion callback. Outputs: synced ON-form
pending rows or OFF-mode name-only rows. Accessors: the revised requests and
row-name/metadata getters. Affiliates: Menu stable-load completion, filesystem
Save tails, packed HCNAMES codec, and AutoSave pending clear.

#### D. exact Instrument source lookup — current lines 1009–1043

**Add:** the following accessor, which searches only the currently loaded
registered-type index and succeeds only for one exact normalized eight-cell
stem:

```c
uint8_t filesystem_findExactInstrumentStem(instrument_type_t type,
                                           const char stem[8],
                                           uint16_t *browser_index);
```

It rejects an inactive/mismatched cache domain, an invalid type, zero matches,
and multiple matches; `browser_index` is written only on success. It never
chooses an ambiguous prefix or exposes a private max-slot sentinel.

**Comment contract:** What: exact HCNAMES `@` source resolver. Why: browser row
numbers are not durable Instrument identities. Inputs: a loaded type-specific
cache, registered type, eight-cell stem, and caller output. Outputs: success
plus one exact row, or failure with output unchanged; no I/O. Accessors: new
lookup plus existing type-index load and slot-name APIs.
Affiliates: boot HCNAMES reconstruction, storageTypes registry.

#### E. Bank completion contracts — current lines 531–583

**Modify:** document that normal Bank Load/Save success is not only payload
completion. Load success includes its pending HCNAMES sync and the existing
settings serializer's durable `active_bank`; Save success includes the Bank
object, root Bank `.hcindex`, pending HCNAMES, and durable `active_bank`, in that
order. A failure in either trailing write reports the whole foreground operation
as ERROR. The callback still fires exactly once. Also distinguish a same-Bank
operation from an actual Bank identity transition: compare the accepted target
slot/name to the still-current resident BankData identity immediately before
the commit setters run, not to the mounted
AutoSave source (which may legitimately describe an older Bank during a
full-dirty drain). Only a true resident identity change requires cache
invalidation and Q setup.

**Comment contract:** What: externally visible Bank operation durability. Why:
boot accepts HCNAMES only when its Bank identity agrees with settings. Inputs:
accepted Bank slot/name and original completion callback. Outputs: callback only
after the required final syncs. Accessors: existing Bank request APIs and
settings dirty/revision machinery. Affiliates: filesystem.c Bank state machines,
Preset completion, main boot discovery.

### 5.4 `Core/Hardware/SD/filesystem.c`

#### A. constants and packed-source helpers — current lines 105–145 and
5088–5243

**Modify:** define the 10-bit source mask, three 10-bit tokens, type mask/shift,
pending bit 14, and transient dirty bit 15. Replace raw equality/assignment in
the mapping, source validation, source getter/setter, and inheritance resolver
with masked helpers. Add type encode/decode and pending accessors. Enforce
neutral type outside direct Instrument rows and clear reserved bit 13. Add
compile-time equality assertions for filesystem versus AutoSave Bank/Scene/Kit/
Instrument row bases, row count, source token values, and 258-byte table size;
also assert that the largest logical source fits 10 bits and every registered
Instrument type+1 fits the three-bit type field. The text and binary formats may
not drift behind separate arithmetic.

**Comment contract:** What: canonical packed HCNAMES row manipulation. Why: raw
word comparisons would confuse flags with sources and corrupt inheritance.
Inputs: logical row/source/type/pending. Outputs: validated packed word or
failure without partial mutation. Accessors: public source/metadata functions
and private formatter helpers. Affiliates: lines 5272–5820 parser/writer,
AutoSave source-table snapshot.

#### B. operation enum and dispatch — current forward declarations at
1277–1318, enum lines 184–280, names at 2790–2800 and 3250–3270, and dispatch
at 22441–22472

**Add:** `FS_INTERNAL_OP_AUTOSAVE_DISCOVERY` at the end of the existing enum,
after `FS_INTERNAL_OP_LOAD_LIBRARY_INDEX`, with its operation label, short
diagnostic code, dispatcher case, stall/error classification, and matching
final decoder-list position. Appending preserves every existing numeric value
so already captured `E` records remain decodable. HCNAMES clear stays inside
`FS_INTERNAL_OP_AUTOSAVE_PARAMETER_DRAIN` after target promotion so facade
ownership cannot be released between publication and clear; do not add a public
or separately schedulable clear operation. Do not renumber silently in the
decoder.

**Comment contract:** What: exclusive facade owners for pre-Bank discovery and
post-commit pending clear. Why: neither may interleave with Load/Save or be
mistaken for normal drain. Inputs: setup mode/effective Bank or promoted target.
Outputs: discovery cache or durable clear. Accessors: blocking boot wrapper,
runtime scheduler, internal tick functions. Affiliates: generic completion and
Autosave trace E records.

#### C. writer scratch and mounted cache — current lines 795–829, 871–879,
928–1003, and 1721–1789

**Modify/add:** remove candidate validation fields from the normal writer state
or move them into a discovery-only union member. Add operation-local
`source_crc32c`, `source_stored_crc32c`, target CRC, exact-length/extra-byte
state, published/clear flags,
the immutable cached-source copy needed through final sync, and a captured
baseline Scene-present mask plus 96 captured Instrument type bytes in the
ensure/recovery union member. These snapshots are operation-local within the
existing 2 KB workspace and add no persistent SRAM. Add the 24-byte
static mounted cache and its static assert. Its `discovery_state` byte also owns
the temporary boot preserve/rebuild/active suppression mode; do not allocate a
separate boot or Bank-transition flag: BANK_TRANSITION_PENDING survives the
Bank HCNAMES/settings tail in this same byte. Keep
`fs_autosave_parameter_cache` exactly 4,608 bytes and
`fs_resident_source` exactly 258 bytes. Extend workspace assertions to prove the
union remains <=2,048 bytes.

**Comment contract:** What: volatile proof of one mounted source and bounded
transaction scratch. Why: operation-local winner state caused repeated A/B
reads, while final-sync promotion needs identity/CRC beyond a handle lifetime.
Inputs: discovery/ensure/publish success. Outputs: source for the next normal
transaction. Accessors: private reset/populate/match/promote helpers and public
read-only discovery accessors. Affiliates: mount init, scheduler, drain, SRAM
manifest.

#### D. HCNAMES parsing/formatting — current bootstrap writer at 4750–4855,
cache/parser/targeted writer at 5245–5820, and shared formatter/bootstrap-row
selection at 19511–19647

**Modify:** retain the 129×9 dedicated name mirror, but make prepare/parse/
format mode-aware. ON parsing accepts the compatibility forms and strict four-
field form from section 2.2; OFF takes only names. Require exactly 129 logical
rows and reject overflow/extra rows. ON formatting emits four fields using
canonical storage type tokens; OFF formatting emits name-only rows. Replace the
old transient-source clear so it clears only bit 15, preserving type and durable
pending. A normal targeted identity update sets source/type/pending together.
Enforce row-specific source validity in both parser and compound setter; in
particular, numbered Instrument rows and non-Instrument `@` rows are rejected.
A pending-clear pass clears bit 14 only and never sets bit 15 through the normal
dirty-marking path.

A safely classified unique but content-malformed register is not passed into an
ordinary read/modify/write path. The discovery result marks it unavailable;
boot fallback later replaces it from settled resident state. While OFF, source/
type/pending suffixes never make an otherwise valid name row malformed because
they are ignored, but invalid name text or row count is likewise handled as a
missing name register and rebuilt after a successful Bank Load. Duplicate and
terminal-I/O cases remain non-repairable automatically.

Extend the existing mirror-validity enum, without widening its byte, with an
internal CLEAR_RETRY state. It means the RAM name/source/type/pending image is
the retained pre-clear authority for another AutoSave attempt, but the physical
singleton may have been truncated and is not advertised to ordinary HCNAMES
readers as durable/VALID. Only the normal AutoSave retry may consume that image;
successful clear final sync returns it to VALID, while boot always reparses the
card rather than trusting this volatile state. A runtime source-contradiction Q
may retain an already mounted VALID mirror or the writer-owned CLEAR_RETRY image
after root enumeration has still proved that exactly one folded HCNAMES object
exists; it must not overwrite that in-session handoff by reparsing a possibly
interrupted clear. OFF-to-ON builds its explicit new image, and a Bank-transition
Q reuses the just-synced pending image.

**Comment contract:** What: one authoritative in-RAM HCNAMES image and strict
mode-dependent text codec. Why: source/pending is recovery metadata only while
AutoSave is ON, and a completed load/save needs a durable row without a second
cache. Inputs: text rows/current policy/targeted row metadata. Outputs: valid
mirror or whole-file malformed status; synced text rewrite. Accessors: packed
metadata helpers, targeted HCNAMES request APIs. Affiliates: boot discovery,
load/save state machines, runtime transform and pending clear.

#### E. boot discovery state machine — extract current drain lines 6362–6521;
add near current ensure lines 5822–6249

**Move/rework:** extract the current A/B open/read/finish/select loop into
`filesystem_autosaveDiscovery_tick()`. Use one read-only folded root enumeration
to classify HCNAMES, A, and B absence/uniqueness before any open. Parse the
unique HCNAMES during boot so main can query recovery availability before Bank
load. Runtime Q follows the trigger-specific HCNAMES ownership rules in section
4.4: OFF-to-ON uses its newly written image, Bank transition uses its durable
handoff image, and source contradiction preserves VALID/CLEAR_RETRY RAM after
singleton proof rather than replacing it from disk. Then validate the two unique
records. Candidate validation uses v2 exact
length and retains `stored_crc32c`; any folded A/B duplicate is a setup error,
not an invalid candidate eligible for overwrite.
Selection uses the caller-supplied effective Bank identity, not uninitialized
resident BankData.
Emit exactly one terminal `V` for a boot-mode run and one terminal `Q` for a
runtime-setup run, including an error result; the SETUP_ERROR trace flag and the
generic operation-error record distinguish error from an ordinary no-winner
classification. HCNAMES and A/B results are independent: a folded
duplicate or nonfatal terminal error in one class sets SETUP_ERROR and makes the
wrapper return zero, but the state machine still classifies and retains the
other class when AsyncFATFS remains usable. Only a fatal facade condition that
prevents subsequent I/O ends the sequence early. Missing/invalid records are
ordinary classifications. An error run publishes no success authorization even
though its independently proven HCNAMES mode or A/B source may be retained for
bounded fallback/diagnosis under the rules above.

**Remove from normal drain:** phase initialization and cases 0..5 at current
6363–6521, including its `V` producer and inline mismatch whole-Bank mark.

**Comment contract:** What: one complete two-record structural/identity
classification. Why: unknown card state must be proved once per mount/setup,
not before every generation. Inputs: mounted card, ON policy, effective Bank
slot/name, HCNAMES parser result. Outputs: discovery-complete flag, optional
mounted source including expected CRC, HCNAMES availability, V/Q trace.
Accessors: blocking wrapper/result getters and async setup scheduler. Affiliates:
validator, main boot ladder, future reader, ensure/recovery.

#### F. ensure and all-invalid recovery — current lines 5822–6249 and
7149–7394

**Modify/move:** update creation for v2 names/sources and the resident recovery
baseline mask (the existing whole-Bank non-Pattern geometry). Capture the
settled Scene-present mask and all resident Instrument types once before the
first CRC interval, validate each captured type against the registry, and reuse
that exact snapshot for every B/A CRC and write pass. Ensure still
creates only missing folded filenames and
never overwrites an existing valid/malformed object merely because open failed.
After each baseline's final sync, populate the mounted cache only if no already
selected valid source exists; creating a missing inactive peer never displaces
the discovery winner. When a winner exists, format a newly missing peer at
`winner_generation - 1u` with normal unsigned wrap so wrap-aware selection still
chooses the retained winner, including when the missing peer is A and equal-
generation ties would otherwise favor it.

Move B-then-A all-invalid recovery out of the normal drain cases 30..39 and into
post-Bank setup. It reads the already validated HCNAMES mirror, prepares each v2
CRC with bounded work, makes generation-0 B durable, then generation-1 A, and
populates A only after final sync. If exactly one name is absent and the other
is structurally invalid, create the absent baseline as generation 0 and retain
it without first overwriting the invalid object; the normal writer can later
replace that peer while the new baseline remains recoverable. Boot pumps this
setup blocking; OFF-to-ON/Q pumps it
asynchronously. Failure leaves tracking/authorization off.

**Comment contract:** What: establish at least one known v2 full-copy source
after resident state exists. Why: discovery cannot synthesize payload/source
metadata before Bank fallback, and both invalid names require recoverable
B-before-A replacement. Inputs: discovery result, resident Bank, HCNAMES mirror,
folded existence proof. Outputs: missing/recovered durable records and mounted
cache; no drain. Accessors: blocking ensure and async setup completion.
Affiliates: baseline formatter, main boot order, setAutosaveEnabled.

#### G. cached normal drain — current lines 6251–6298, 6329–7147, and 7396–7429

**Modify:** initialize writer scratch from the mounted cache and enter current
phase 50. Retain phases 50–56 mask merge/capture and phases 10 onward full-copy,
but use the cached index/generation/probe/CRC. Before each transaction require
source-valid, matching mounted lifecycle, current Bank setup authorization, and
either a durable VALID HCNAMES mirror or the private CLEAR_RETRY image from this
writer's preceding failed clear. No other caller may treat CLEAR_RETRY as valid.

At current copy phases 6773–6882, feed a source-integrity view of the input
chunk—with only absolute CRC-field bytes 12..15 substituted as zero—to the
source CRC, while separately assembling those four physical bytes,
transform with names/sources, feed the logical result to target CRC, clear only
the physical commit, and write. Add exact record-end plus extra-byte EOF state.
Require both physical stored CRC and finalized source CRC to match the cached
expected value before current target CRC/commit phases 6936–7147.

At current commit-close handoff 7122–7147, perform the explicit final sync,
promote the cache, then run pending clear. Keep data-sync, CRC publication-sync,
and commit-last ordering as three distinct durability boundaries. Move `P` to
the durable target-promotion point after final sync; `T` remains whole-operation
completion after HCNAMES.

Split error cleanup at current 7396–7429 into:

- target/pre-publication failure: restore captured offsets, keep old cache;
- source contradiction: restore offsets, invalidate cache, queue Q;
- post-promotion HCNAMES-clear failure: keep new cache, keep pending, restore
  offsets for ordinary retry, retain the pre-clear RAM image, and mark the
  physical HCNAMES publication state unknown.

No error path may select the peer without discovery or clear canonical bits that
were produced after atomic take. Source contradiction also retains every mask
bit already OR-imported from the rejected file and Q's completion adds the
whole-resident marker, making an older rediscovered peer safe to copy.

**Comment contract:** What: normal cached-source full-copy transaction and its
three terminal error classes. Why: runtime speedup must retain commit-last A/B
safety and never silently promote/replace a contradicted source. Inputs: mounted
source snapshot, canonical mask, HCNAMES mirror, live getters, terminal I/O.
Outputs: promoted cache plus durable H clear, conservative retry, or queued Q.
Accessors: internal drain tick/error helpers and scheduler callback. Affiliates:
Autosave transform/CRC, generic final sync, HCNAMES CLEAR_RETRY state.

#### H. generic completion/index/settings/final sync — current lines 3476–3550,
3563, index rebuild/completion at 3637–3842, final sync at 3844–3908, and
settings writer at 18185–18370

**Modify:** add an explicit AutoSave final-sync phase and chained HCNAMES writer
continuation so target promotion and HCNAMES clear occur before the external
writer callback. Do not make generic `filesystem_finish()` promote an AutoSave
target; other operations use the same sync owner. Preserve the rule that
HCNAMES mirror publication becomes VALID only after its final sync.

Save call sites park their HCNAMES continuation behind the existing successful
index-rebuild callback. The shared scan/sort/index serialization algorithm does
not change; only the save-specific continuation and final externally visible
completion point move. Index failure goes directly to the Save error callback
and must never publish the new HCNAMES identity.

Bank Load/Save then park their original Preset callback behind the existing
`FS_INTERNAL_OP_SAVE_GLOBALS` serializer after HCNAMES final sync. Keep the
already-installed `completion_callback` in place while `current_op` transitions
internally, so the chain adds no second callback pointer or public operation.
Reuse the serializer's
captured revision and generic completion acknowledgement so a concurrent newer
settings mutation remains dirty; do not invoke the autonomous scheduler or
release facade ownership between HCNAMES and settings. Settings error makes the
Bank operation terminal ERROR even though its earlier object/index/HCNAMES
writes remain durable. This direct continuation is allowed pre-audio and does
not depend on `fs_settings_runtime_ready`; only the autonomous scheduler retains
that gate. Scene/Kit/Instrument paths do not enter this continuation.

**Comment contract:** What: operation-specific post-sync continuations for
AutoSave handoff and Bank settings durability. Why: commit close is not
durability, yielding facade ownership before pending clear permits a newer
Load/Save to be cleared incorrectly, and Bank HCNAMES is unusable at boot until
`active_bank` agrees. Inputs: final sync success, immutable published metadata,
and retained original callback. Outputs: cache promotion/clear or Bank settings
sync followed by exactly one external callback. Accessors: internal finish/
continuation helper. Affiliates: generic flush, settings revision writer,
AutoSave writer completion, and HCNAMES mirror state.

#### I. lifecycle, scheduler, and callbacks — current lines 21256–21465,
21712–21988, 22352–22438, and 22951–23033

**Modify:** reset the mounted cache, including its discovery/boot-mode state, in
`filesystem_initAfterCardReady()`. Make `filesystem_setAutosaveEnabled(0)` gate
tracking/writes and ignore HCNAMES recovery metadata; make OFF->ON clear cache
and queue asynchronous Q/setup only after the runtime-ready gate. Before that
gate, ON is boot policy consumed by main's blocking discovery and queues no
runtime work. `filesystem_autosaveSetupCompleted()` enables
tracking, performs the synchronous RAM-only full-dirty mark, and arms the normal
writer only after a valid source exists.

`filesystem_autosaveWriterSchedule_tick()` must never start drain without
source-valid. Source contradiction and a successfully completed Bank-identity
change select Q/setup, not normal retry. A Bank handoff that fails after identity
commit remains unauthorized and does not let the scheduler infer completion.
Ordinary target error retains the old cache and normal retry.
Continuation uses the promoted cache. The blocking boot wrappers must remain
guarded against post-audio use and must not drain mutation bits.

Retain the existing `menu_activePage == LOAD_PAGE || SAVE_PAGE` guard for both
setup and drain admission, including the current suppression latch and short
deadline when the page is left. This guard is what makes pending clear occur in
the next normal out-of-menu transaction; it must not be deleted merely because
HCNAMES now has a dedicated mirror.

**Comment contract:** What: mounted-session authorization lifecycle. Why: cache
fields are assertions about one exact mount/Bank setup and cannot survive
policy/remount/source contradictions. Inputs: policy change, resident Bank,
setup/transaction terminal status. Outputs: tracking gate, setup request,
debounce/retry state. Accessors: set/get AutoSave, boot wrappers, scheduler.
Affiliates: main runtime gate, Bank completion, Autosave tracking API.

#### J. load/save state machines — current ranges listed below

Apply the durable handoff contract inside the owning state machine, not as a
Menu-exit side effect:

- **Kit Load, 9687–9770:** preserve payload staging/commit. A superseded scroll
  result may complete in RAM but exposes its request identity so Menu can skip
  DSP/HCNAMES and issue the latest desired load. The payload state machine does
  not publish HCNAMES itself; after stable apply, Menu posts the targeted Kit
  update with Kit direct source, six child INHERIT sources, and pending=1. Mark
  the committed destination Kit scope before that publication; a Kit result
  already committed before becoming superseded may remain conservatively dirty,
  while the latest load will overwrite/mark it again. When
  boot HCNAMES suppression is active, compare each inherited Instrument's
  discovered member stem to the preserved HCNAMES name before accepting the
  Kit; reuse the one-member parse/name scratch and allocate no six-name array.
- **Scene Load, 10444–10491 and 11424–11463:** retain hierarchy source staging
  but mark the committed Scene non-Pattern scope before setting pending on its
  Scene/Kit/Instrument rows. Keep HCNAMES sync inside the operation; boot
  suppression bypasses both normal dirty marking and its HCNAMES prepare/update phases and
  leaves the discovery mirror byte-for-byte intact. In that mode, compare each
  inherited Kit and Instrument name against its preserved row while the existing
  per-child folder/member key is still in operation scratch; a mismatch is a
  component source failure, not permission to substitute a same-slot object.
- **Bank Load, 11535–11715 and 12010–12521:** reuse the one HCNAMES preload;
  publish Bank direct source plus child hierarchy/pending for normal loads. In
  boot preserve mode, bypass `filesystem_prepareResidentNamesCache()`, the
  HCNAMES open/read, every child-row overlay, and the final HCNAMES writer so the
  parsed recovery image survives baseline Load. As each inherited Scene, Kit,
  or Instrument is discovered/staged in preserve mode, compare its settled name
  to the corresponding HCNAMES row before the scratch is reused. Numbered direct
  Scene/Kit rows are checked by the root-index lookup in `main.c`, and direct
  Instrument rows by the typed-index exact lookup there. In boot rebuild mode, bypass
  the old-file preload, clear the mirror, load the effective Bank, then write a
  replacement: Bank row = direct effective slot; every present Scene/Kit/
  Instrument row = INHERIT with its settled name and pending=1; absent rows =
  blank/UNKNOWN/pending=0. ON writes four fields; OFF repair writes names only.
  Even while OFF, the successful Bank Load installs the same direct/inherit
  provenance in the packed RAM words but never parses or serializes that
  metadata; a later OFF-to-ON setup can therefore describe actual resident
  state without trusting stale disk suffixes.
  Successful final sync clears HCNAMES_ACTIVE and changes HCNAMES_REBUILD to
  HCNAMES_PRESERVE; every failure clears active and sets SETUP_ERROR. Expose
  actual loaded/failed Scene masks. On a runtime Load whose target differs from
  the still-current resident BankData slot/name immediately before commit, the
  resident identity commit
  immediately disables tracking and invalidates the old source so no writer can
  run under the wrong Bank, but it queues Q only after HCNAMES and settings both
  sync. A trailing failure reports ERROR and leaves setup unauthorized. A
  same-Bank Load retains the cache and uses the ordinary drain; a boot baseline
  runs while tracking is already disabled and leaves selection/ensure to the
  boot path rather than queuing Q. An unsuccessful staged Bank Load leaves the
  old cache/session.
  Mark the committed normal Bank scope before entering HCNAMES; boot suppression
  skips this normal-load marker because post-load setup supplies the one full
  resident mark.
  After HCNAMES final sync, chain the existing settings serializer and report
  Load success only after `active_bank` is durable. Do not leave this Bank
  operation to the one-second autonomous debounce. For a true transition, a
  trailing HCNAMES/settings error leaves the old cache invalid and Q unqueued;
  it cannot authorize a writer from an incompletely reported Bank operation. A
  same-Bank trailing error reports ERROR without discarding an otherwise valid
  source cache, while the HCNAMES mirror-validity gate still prevents a drain
  from consuming a failed publication.
- **Instrument Load, 12880–12972:** stop staging an untyped plain `@` as if the
  payload read were already durable resident identity. Preserve the parsed
  direct source name/type in request-stable scratch; stable Menu completion
  commits and marks the Instrument scope before publishing `@` plus registered
  type and pending after apply. Superseded staged loads do not commit, mark, or
  publish.
- **Instrument Save, 13290–13419:** reorder the current chain to physical file ->
  typed index rebuild/sync ->
  `autosave_markWholeInstrumentDirty(source_scene, source_slot)` -> HCNAMES
  name/`@`/type/pending write/sync -> callback. Morph Save remains physical
  file/index only.
- **Kit Save, 15031–15085:** after physical directory and `/Kit/.hcindex` sync,
  call `autosave_markKitDirty(source_scene)` and publish Kit direct plus child
  inherit names/sources/pending before callback. Remove dependence on Menu exit.
  Morph Save skips resident HCNAMES.
- **Bank Save, 15160–15955:** reorder the existing HCNAMES/index tail so the Bank
  index is durable first; then call `autosave_markResidentBankDirty()` after the
  saved Bank identity is resident and publish Bank/child hierarchy pending.
  Callback is last. If the saved target differs from the still-current
  resident BankData slot/name immediately before commit,
  invalidate the old mounted source and disable tracking at the identity commit,
  but queue Q only after the HCNAMES and settings tail succeeds; the old record
  may then be rediscovered as a safe full-copy source but cannot remain
  authorized under the new Bank identity. Saving the same Bank retains the cache
  and follows the ordinary drain path.
  After HCNAMES final sync, chain the existing settings serializer and report
  Save success only after the new `active_bank` is durable. A trailing error
  likewise leaves Bank-transition AutoSave setup unauthorized.
- **Scene Save, 16510–16588:** likewise put root Scene index rebuild/sync before
  `autosave_markSceneWithoutPatternDirty(source_scene)`, the Scene hierarchy
  HCNAMES pending write, and the final callback.

Each state machine's adjacent block must state: exact request coordinates kept
immutable; which rows receive direct/inherit/type/pending; when mutation marking
occurs; whether namespace/index changes; boot suppression behavior; final
callback durability; and error behavior. A failure before HCNAMES final sync is
an operation error and must not be displayed as complete.

#### K. public request/accessor implementations — current lines 24011–24228,
blocking HCNAMES wrapper at 23172–23228, load/save requests at 24613–24783,
and cache accessors at 25072–25333

**Modify/add:** update targeted HCNAMES request staging to carry the complete
metadata from the exact revised declarations in section 5.3C rather than only
names/source. Overlay the request-stable mirror before starting the asynchronous
writer and reject invalid source/type coordinates without partial mutation. Add
the exact Instrument stem lookup from section 5.3D over the currently typed
cache. Keep first Bank/Scene/Kit slot helpers. Add discovery result and boot
suppression implementations. Ensure borrowed mirror pointers retain their
existing lifetime rules. In both Bank commit branches, compare `op_slot` and
the normalized target name with BankData before calling its identity setters;
when runtime mode observes a change, invalidate the mounted source/disable
tracking immediately and set BANK_TRANSITION_PENDING for the successful
HCNAMES/settings tail to consume. Clear that bit on the operation's error exit
without queuing Q.

**Comment contract:** What: public request validation/result access and Bank
transition capture at the implementation boundary. Why: UI selection, boot
orchestration, and the mounted source must share exact immutable coordinates.
Inputs: revised HCNAMES arguments, effective Bank identity, active typed/root
caches. Outputs: accepted operation state or failure without partial staging;
read-only result pointers/values. Accessors: the declarations in filesystem.h.
Affiliates: Menu, main, Bank state machines, discovery cache.

### 5.5 `Core/Bank/Scene/Preset/presetManager.c`

#### A. completion dirtying — current lines 250–295, 369–385, 440–555,
and 1849–1883

**Modify:** retain the existing whole-Kit, Instrument, Scene, and Bank marker
scopes but move each normal Load marker to its successful resident commit,
before HCNAMES publication. Scene/Bank commit inside filesystem.c and mark there;
Kit marks when its staged payload becomes resident; Instrument marks only when
the latest staged result is accepted and committed. Boot reconstruction runs
with tracking/suppression active and receives its one full-resident mark from
setup instead. Remove later completion-callback marking so no path double-marks.
For scroll-driven Kit/Instrument Load, expose completion identity to Menu and
defer final HCNAMES publication until the latest stable result/apply.

Call the existing public whole-object markers directly from the normal Save
state machines before their HCNAMES stage; do not double-mark in Preset
callbacks. Morph save callbacks remain identity-neutral.

**Comment contract:** What: mutation handoff for directly assigned resident
payload. Why: filesystem parsers bypass scalar setters, and pending must never
become durable before its payload scope is dirty. Inputs: successful resident
commit and immutable request mask/coordinates. Outputs: canonical dirty scope
before HCNAMES I/O; no I/O itself.
Accessors: existing `autosave_mark*Dirty()` family. Affiliates: filesystem
HCNAMES publication and Menu apply/coalescing.

#### B. request identity and boot source support — current lines 72–88,
171–207, and 2086–2349

**Add:** expose the already retained `pm_instrument_request_type` and
`pm_instrument_request_index` through
`preset_getInstrumentRequestType()` and
`preset_getInstrumentRequestIndex()`. Existing `preset_getRequestSlot()`,
`preset_getRequestScene()`, and `preset_getKitRequestSceneMask()` supply the
other immutable coordinates. No new Preset storage is allocated. Keep normal
`preset_loadBank()`, `preset_loadSceneForScenes()`, `preset_loadKitForScenes()`,
and typed Instrument requests as the boot reconstruction primitives. Expand the
fallback helper/comment so main's order includes lowest Bank before its existing
Scene -> Kit sequence; do not make the helper perform blocking I/O itself.

**Comment contract:** What: immutable accepted-request identity and existing
boot load primitives. Why: current encoder selection may differ before an async
request completes. Inputs: values captured at request acceptance. Outputs:
read-only type/index/slot/Scene/mask until acknowledgement; no new storage.
Accessors: new and existing Preset getters/load functions. Affiliates: Menu
coalescing and main boot reconstruction.

### 5.6 `Core/Bank/Scene/Preset/presetManager.h`

Current lines 109–137 and 200–314.

**Add/modify:** declare and document
`preset_getInstrumentRequestType()` and
`preset_getInstrumentRequestIndex()` beside the existing immutable request
getters. Clarify that
normal Save success means payload plus affected index plus required HCNAMES are
durable, while Morph Save does not change resident identity. Clarify that normal
Kit/Instrument Load filesystem completion may still be followed by Menu's
bounded apply and HCNAMES publication before user-visible completion. State
separately that Bank Load/Save completion also means the new `active_bank` has
passed the settings final-sync gate.

**Comment contract:** What: immutable async request/completion identity. Why:
encoder coalescing cannot compare against mutable UI selection. Inputs: accepted
request coordinates. Outputs: read-only getters until acknowledgement.
Accessors: proposed getters/existing request APIs. Affiliates: menu.c and Preset
callbacks.

### 5.7 `Core/Bank/Scene/SceneData.c`

Current lines 563–603.

**Modify:** factor the body of `scene_initAll()` into a private one-Scene
initializer and add `scene_resetOne(uint8_t scene_index)`. `scene_initAll()`
loops through that helper so defaults remain byte-identical. The public reset
validates range and resets only the selected Scene; the boot caller separately
clears Bank presence through the existing BankData API. Tracking is disabled
during boot reconstruction, so this does not manufacture AutoSave work.

**Comment contract:** What: deterministic reset of one resident Scene graph.
Why: one failed HCNAMES component source must not erase unrelated valid Scenes
or leave a partial stage. Inputs: Scene index. Outputs: the same defaults as
boot initialization for that Scene only. Accessors: `scene_resetOne()` and
`scene_initAll()`. Affiliates: main boot reconstruction error path and Bank
present mask.

### 5.8 `Core/Bank/Scene/SceneData.h`

Current lines 231–280.

**Add:** declaration and full public contract for `scene_resetOne()`, including
range behavior, tracking precondition at boot, and no Bank-present side effect.

### 5.9 `Core/Menu/menu.c`

#### A. replace exit-time resident-name batching — current lines 89–117,
198–248, 1069–1210, helper declarations at 1348–1356, 3292–3465, and
3577–4040

**Remove/repurpose:** remove `menu_residentNameDirtySceneMask` as the owner of
deferred normal Kit/Instrument/Scene publication, plus scratch-refresh and
exit-flush functions used only for that batching. Preserve the existing seven
name scratch rows if they are still required for editing/display, but they no
longer represent uncommitted HCNAMES work. Preserve pending-page state.

Do not allocate a second desired-coordinate object. The latest desired Kit is
already in `menu_currentPresetNr[]`/`menu_kitLoadSceneMask`; the latest desired
Instrument is already in `menu_instrumentLoadScene`, slot, type, source, and
per-type index; Preset's immutable getters identify the in-flight request.
Repurpose `menu_deferSelectionRequest`/`menu_deferSelectionLoadKit`, existing
apply-active state, `menu_storageBusy`, and the operation-specific HCNAMES
callbacks to distinguish load/apply/publication. Remove the two-byte deferred
dirty mask and add no Menu payload, name cache, or new retained phase byte.

**Comment contract:** What: coalesced latest Kit/Instrument Load intent. Why:
AsyncFATFS work is non-cancellable but encoder response must continue and stale
results must not become durable identity. Inputs: encoder/type/page/exit events
and immutable completed request identity. Outputs: at most one current load plus
one latest desired intent; stable apply/HCNAMES completion. Accessors: request,
completion, and pending-page helpers. Affiliates: Preset getters, filesystem
targeted HCNAMES APIs, apply state machines.

#### B. apply completion hooks — current lines 403–667

**Modify:** stable Instrument/Kit apply completion queues its targeted HCNAMES
pending update rather than calling the old deferred updater. A stale completed
Instrument stage is discarded before commit/apply. A stale Kit may already be
resident; skip DSP apply and immediately start the newest desired Kit load.
Only the final selection performs DSP apply and HCNAMES publication.

If selection changes while HCNAMES owns the facade, finish that safe write,
then start the latest desired load. HCNAMES error leaves the load not durably
complete, shows the existing error overlay, and does not release a queued exit
as success.

#### C. scroll/request handlers — current lines 4506–4555 and 7199–7581

**Modify:** Kit and Instrument Load start on scroll, not OK. Every encoder event
updates the latest desired coordinate and repaint while a request is in flight.
If idle, start it immediately; if busy, coalesce by replacing the desired
coordinate. Type/menu switches that require an index load are retained and
performed after the current load/HCNAMES boundary. Normal Save remains a finite
OK/OW command with input locked.

#### D. busy guard, redundant index restore, and completion poll — current
lines 3182–3267 and 7687–8830

**Modify:** retain input response for Kit/Instrument selection while their load,
apply, or HCNAMES state is active; keep all Save input locked. At Kit and
Instrument completions compare immutable completed request with latest desired:

1. finish/acknowledge the current filesystem operation safely;
2. if selection changed, discard/skip apply as allowed and start latest;
3. otherwise complete bounded apply;
4. publish stable HCNAMES name/source/type/pending;
5. if selection changed during publication, start latest after it syncs;
6. only when final desired selection is durable release busy and process exit.

Remove the current completion comments/actions at 8264–8298, 8546–8610,
8725–8795 that describe menu-exit HCNAMES. Instrument Save is already
filesystem-owned; Kit/Scene/Bank Save become filesystem-owned under this plan.
Save completion only refreshes the durable index display row and releases the
command.

Remove `menu_requestLoadCommandFinalIndexRestore()`, its private completion
status/callback plumbing, and the Scene/Bank post-Load read-only reload that
existed only because HCNAMES once borrowed the browser cache. The dedicated
mirror no longer displaces the already validated source index, so successful
Load completion leaves that cache resident. Do not remove pre-selection index
load/repair or any Save-owned physical rescan/rewrite.

#### E. exit/page/type deferral — current lines 8053–8062, 8940–9226

**Modify:** delete the `end_resident_name_session` predicate and exit-time flush
call. Keep `menu_pendingPageSwitch`, but its release condition includes no load,
no apply, and no HCNAMES publication plus Preset idle. A requested exit, Load/
Save toggle, or different type/menu is retained until the latest desired load
and its HCNAMES generation are complete. Priority is fixed:

1. finish the current non-cancellable operation;
2. load the latest changed selection;
3. publish HCNAMES for the final stable selection;
4. honor the deferred exit/type/menu request.

#### F. initialization — current lines 9850–9869

**Modify:** remove resets for retired exit-batch dirty state and initialize the
repurposed defer/apply/publication flags to their existing idle, no-request
values. Keep identity and name scratch initialization needed by display/editing.
The adjacent comment must state that no new Menu state was allocated and that
the retired two-byte dirty mask is removed from SRAM_MANIFEST.

**Comment contract for sections A–F:** What: one latest-desired asynchronous
Kit/Instrument selection pipeline plus finite Save commands. Why: non-cancellable
SD work must remain responsive without publishing a superseded resident source.
Inputs: page/type/encoder/exit intent, immutable Preset completion identity, and
HCNAMES callback status. Outputs: stable DSP apply and synced pending identity,
or retained deferred intent/error; no exit-batch state. Accessors: the existing
Menu request, apply, busy, and page-switch helpers plus revised filesystem
HCNAMES requests. Affiliates: Preset getters, filesystem page suppression,
AutoSave pending handoff.

### 5.10 `main.c`

#### A. settings/index prerequisites — current lines 537–568 and 575–729

**Modify comments only where necessary:** keep settings load and index creation
in place. The AutoSave setting must be applied before hidden-file access. Root
Bank index must be reloaded before resolving the effective Bank slot/name.

#### B. replace the initial load ladder — current lines 743–923

**Modify:** after Bank-index reload, compute the effective slot: settings slot
if present, otherwise `filesystem_firstBankSlot()`. If a Bank exists, copy its
eight-cell name before any cache reuse and call the boot discovery wrapper while
AutoSave is ON. Set boot reconstruction control before posting the baseline Bank
Load. A completed, unique, well-formed, Bank-matching HCNAMES result selects
preserve mode; safely absent/content-malformed/Bank-mismatched HCNAMES selects
rebuild mode. An A/B duplicate/terminal error leaves AutoSave unauthorized but
does not discard either safe HCNAMES choice. An HCNAMES duplicate/terminal error
leaves AutoSave unauthorized and uses the settings Bank fallback only if further
facade I/O is safe. Then choose
ordinary Bank baseline versus HCNAMES reconstruction using the result rules in
section 1.1.

Capture the pre-Bank `filesystem_hcnamesRecoveryAvailable()` result in one local
boot control byte. Only that captured preserve result permits child overlays;
do not query it again after a successful rebuild has changed the internal mode
to PRESERVE, or the newly generated baseline register would be mistaken for a
discovered recovery instruction.

Add a small pre-audio pump helper around existing Preset requests so each Bank,
Scene, Kit, or Instrument source request reaches terminal status, is applied/
acknowledged through the existing boot-safe path, and checks watchdog/error
state. This helper owns no storage and must not be callable after audio init.

Implement HCNAMES reconstruction top-down. For direct Instrument `@`, load or
repair the selected registered-type index, exact-match the HCNAMES stem, then
issue the existing typed Instrument Load. Before numbered Scene/Kit requests,
reload the corresponding root index already refreshed by the preceding boot
index phase and require its slot's eight-cell name to equal the
HCNAMES row; before retaining an inherited child, compare its settled resident
name to that row. On any component terminal failure call
`scene_resetOne(scene)`, clear that Scene's Bank-present bit, retain/display the
existing error, skip the remaining lower-level overlays for that Scene, and
continue reconstructing other Scenes. Escalate to the common boot filesystem
failure path only when the terminal facade error prevents subsequent I/O; a
missing/malformed declared component itself is the specified per-Scene empty
result. Set/clear boot HCNAMES suppression around this entire reconstruction,
including the baseline Bank Load and every error exit. Do not enter component
reconstruction after rebuild mode: the replacement register already describes
the Bank baseline that was loaded.

Fix the missing fallback: if settings Bank is absent but another Bank exists,
load the lowest Bank before current Scene/Kit fallback. If no Bank exists,
retain lowest Scene -> lowest Kit -> initialized empty order. AutoSave OFF skips
AutoSave discovery and all HCNAMES source/type/pending recovery; the ordinary
name-only register maintenance described in section 2.2 remains available to
the fallback Load/Save paths.

**Comment contract:** What: pre-audio source-selection and reconstruction
orchestrator. Why: validation/recovery policy belongs after settings but before
payload, while the future reader must occupy the same point. Inputs: effective
Bank identity, discovery/HCNAMES metadata, root indexes. Outputs: one fully
settled resident state or bounded fallback/error. Accessors: filesystem
discovery/metadata/index APIs, Preset loads, `scene_resetOne()`. Affiliates:
future reader and boot watchdog diagnostics.

#### C. post-load setup and runtime release — current lines 925–966

**Modify:** keep “do not regenerate HCNAMES from SRAM” but replace it with the
mode-aware rule: a valid matching register was preserved and reconciled; a
safely absent/malformed/mismatched singleton was rebuilt by the Bank baseline;
or recovery metadata was intentionally ignored while OFF. Before v2 ensure/
recovery while ON, require one unique, well-formed HCNAMES singleton matching
the settled resident Bank. Require a valid mounted source before enabling
tracking. Perform only RAM setup/full-dirty mark synchronously, then release
runtime settings/filesystem gates; do not pump the first AutoSave drain at boot.

**Comment contract:** What: post-load authorization boundary. Why: discovery is
not enough until resident fallback, HCNAMES mode, and at least one durable v2
source agree. Inputs: settled Bank/H classification/setup status. Outputs:
tracking plus full-dirty RAM state and asynchronous drain admission, or tracking
off. Accessors: blocking ensure/setup-complete/runtime-ready APIs. Affiliates:
boot orchestrator and runtime scheduler.

### 5.11 `Core/Bank/Scene/AutosaveTrace.h`

Current lines 39–145 and the flag-layout section after line 261.

**Modify/add:** retain `V` but redefine/document its sole normal producer as
boot discovery. Add `Q` for OFF->ON, Bank-transition, or source-contradiction
rediscovery using the same flags/value layout. Add `H` for HCNAMES handoff:

- V/Q flag bit 0 winner exists; bit 1 winner is B; bit 2 winner Bank mismatch;
  bit 3 SETUP_ERROR/classification incomplete; value is selected generation or
  zero.
- H flag bit 0 pending rows were present in the published image; bit 1 target
  final sync promoted the cache; bit 2 HCNAMES clear reached durable success;
  bit 3 clear failed/pre-clear RAM image retained and physical HCNAMES is
  uncertain; value is the promoted generation.

`V` never appears in a normal runtime or continuation transaction. `P` denotes
durable target promotion (move it after final sync if currently earlier), and
`T` denotes the whole writer/H clear result. Emit `H` only when the published
image actually contained pending rows; ordinary parameter-only generations use
`P` and `T` without redundant H records. Add enum comments and flag macros, not
magic literals in filesystem.c.

Retain numeric Instrument-entry phases 4/5
(`HCNAMES_FLUSH`/`HCNAMES_FLUSHED`) only as retired decode compatibility for
already captured logs. Remove their Menu producers with the exit-time batch;
do not reuse those phase numbers for the new targeted stable-selection write.

**Comment contract:** What: stable trace wire symbols and flag layouts. Why:
captured logs must distinguish boot discovery, exceptional runtime setup,
target promotion, and pending clear without reinterpreting historical values.
Inputs: terminal discovery/setup and writer handoff state. Outputs: eight-byte
records only; no transaction control. Accessors: `autosaveTrace_record()` and
named flag macros. Affiliates: filesystem producers and Python decoder.

### 5.12 `tools/decode_devlogs.py`

Current lines 94–150, 189–220, and 489–511.

**Modify:** add Q/H enum names, producer descriptions, and exact flag decoding;
change V producer from the runtime drain to discovery. Insert new internal-op
names at the identical enum positions used by filesystem.c. Decode V/Q mismatch
and SETUP_ERROR bits explicitly, and distinguish target promoted/H clear
durable/H retained on failure. Keep old stages decodable, including labeling Instrument-entry phases
4/5 as retired exit-time HCNAMES batching rather than current behavior.

### 5.13 `tools/verify_bank_autosave.py`

Current lines 18–25, 62–69, 114–124, 149–235, and 246–331.

**Modify:** use v2/version/35,026-byte constants and parse the 258-byte source
table. Parse name-only, two-field, and strict four-field HCNAMES forms with the
same ON compatibility rules, including canonical Instrument type and pending.
Select A/B using firmware's Bank-match preference, wrapping generation order,
and A tie. Verify source rows against Bank/Scene/Kit/Instrument library
relationships: numbered Scene/Kit sources require an exact normalized index
name, inherited rows require the exact settled parent name, and Instrument `@`
requires an exact type/extension plus stem match. Explain when pending or dirty
payload makes HCNAMES the component winner. Compare an HCNAMES direct Instrument
type with the type stored in that AutoSave Instrument payload, not only its
name/source token. Do not require stale AutoSave payload bytes to equal a newer
pending HCNAMES source.

Add fixtures/manual cases for v1 rejection, short/overlong/v2 CRC failure,
pending reconciliation, wrong type, malformed row count, A tie, wrapping newer,
and settings mismatch.

### 5.14 Specification and project documents

These are implementation-closeout changes, not code in this planning turn.

- **`knowledge_files/specification_reference/AUTOSAVE.md:20–46, 48–62,
  64–125, 127–190, 192–304, 306–327`:** replace v1/repeated-validation text
  with v2 map, boot discovery, mounted cache/source CRC, runtime flow, HCNAMES
  reconciliation, pending handoff, power windows, re-enable/Bank transition,
  and future-reader boundary.
- **`knowledge_files/specification_reference/FILESYSTEM_SPEC.md:316–364,
  1560–1735, 1857–1876`:** specify ON/OFF HCNAMES grammar, source/type/pending,
  immediate Load/Save/index/H aftercare, Bank `active_bank` completion sync,
  scroll coalescing, boot fallback order, and filesystem/AutoSave boundary.
- **`knowledge_files/specification_reference/MODULE_INTERCHANGE_SPEC.md:650–663,
  674–711, 775–791`:** document Autosave.c mutation ownership, filesystem cache/
  discovery ownership, Preset/filesystem resident-commit dirty markers, Menu
  final-stable publication, and no AsyncFATFS cancellation.
- **`knowledge_files/specification_reference/SRAM_MANIFEST.md:70–106 and
  154–189`:** add the measured 24-byte mounted cache, explicitly identify its
  approved +4-byte source CRC, record that type/pending reuse the existing
  258-byte source words, remove the retired two-byte Menu dirty-mask owner,
  confirm the 96-byte baseline type snapshot and discovery scratch remain inside
  the pre-existing 2,048-byte union, confirm coalescing reused existing state,
  and replace linked-size totals only after an implementation build/map inspection.
- **`knowledge_files/specification_reference/DEV_MODES.md:206–320`:** define V
  boot-only, Q exceptional setup, H post-promotion handoff, and their decoder.
- **`MEMORY.md` (current AutoSave/HCNAMES/Session-058/059 sections around
  119–145, 214+, and 1440+):** record only implemented final behavior after
  verification; remove superseded “validate each drain”/v1 facts.
- **`knowledge_files/log_archive/060_SESSION_HANDOFF_LOG.md` and
  `knowledge_files/log_archive/000_SESSION_INDEX.md`:** add implementation,
  build, verifier, and hardware results at closeout. Do not claim S060 complete
  from this plan alone.

### 5.15 Files intentionally unchanged

- **`Core/Hardware/SD/asyncfatfs/asyncfatfs.c/.h`:** no cancellation, retry, or
  sync API change. It already owns cooperative in-progress handling; source
  contradictions are classified only after its terminal result.
- **`Core/Hardware/SD/storageTypes.c/.h` and
  `Core/DSP/Instruments/InstrumentManager.c/.h`:** no format registry change.
  Reuse canonical type text/extension/registry APIs.
- **`settings.cfg` schema and settings serializer:** no new setting or persisted
  winner field. `active_bank` and `autosave` remain the only inputs.
- **ordinary Bank/Scene/Kit/Instrument payload formats:** no member-file layout
  change; only HCNAMES and the hidden AutoSave record change.
- **mutation producer architecture:** no second mutation mask and no direct
  filesystem ownership of scalar setters. Only whole-object completion markers
  are added/reordered where direct load/save assignment bypasses setters.
- **`config.h`:** keep CRC bytes/tick, debounce, continuation, and capture budget
  unless later measured evidence independently justifies a tuning session.
- **boot-wide typed-index regeneration:** the separate finding in
  `S060_HCINDEX_REGENERATION.md`—fail-fast registry traversal plus main.c's
  ignored result—is not caused by the Load/Save HCNAMES deferral and is not
  silently folded into this transaction redesign. S060 does require every
  individual Save's affected index to be durable before its HCNAMES completion,
  and boot HCNAMES `@` reconstruction uses the existing selected-type
  load-or-rebuild path. Repairing the independent all-types boot policy remains
  a separately reviewable change.

## 6. Verification and acceptance

### 6.1 Static/build checks

1. Compile with warnings treated as currently configured and inspect every new
   static assertion: v2 size/offsets, 24-byte cache, 258-byte source register,
   4,608-byte patch cache, 96 captured type bytes inside, and <=2,048-byte stage
   workspace.
2. Inspect map/size output and update SRAM_MANIFEST with measured deltas, not
   estimates. Confirm no record-sized buffer and no new per-row array exists.
3. Run/update the Python verifier fixtures and decoder syntax checks.
4. Search for stale `34768`, format version 1, raw packed-source comparisons,
   “deferred HCNAMES/menu exit”, runtime `V` producers, and any AutoSave start
   that bypasses the Load/Save-page suppression gate.

### 6.2 Boot matrix

Test AutoSave OFF; A/B valid/newer/tied/mismatched; one missing; both missing;
one or both v1; short/overlong/uncommitted/CRC-corrupt; folded A/B duplicate;
HCNAMES absent, name-only, two-field, valid four-field, content-malformed,
folded duplicate, wrong Bank, pending component, typed Instrument direct source,
illegal numbered Instrument source, numbered Scene/Kit slot with a wrong name,
inherited-name mismatch, missing component source, HCNAMES-versus-AutoSave
component disagreement with pending 0 and 1, absent settings Bank, empty Bank,
Scene-only, Kit-only, and no-library card.

Required observations:

- settings and indexes precede one V transaction on an AutoSave-ON boot with an
  effective Bank; OFF/no-Bank boots emit no V and inspect no hidden record;
- ordinary all-invalid/no-winner discovery and SETUP_ERROR discovery each still
  emit one terminal V, with bit 3 distinguishing the latter;
- no payload Load precedes discovery;
- lowest Bank is tried before Scene/Kit;
- matching HCNAMES survives baseline Bank Load unchanged, while absent,
  content-malformed, or Bank-mismatched HCNAMES is replaced only after the Bank
  has settled and before ensure;
- duplicate HCNAMES or duplicate A/B names never authorize an ambiguous write;
- one failed HCNAMES component resets only its Scene and shows an error;
- when Bank identity agrees, a resolvable HCNAMES component difference wins over
  AutoSave metadata regardless of pending value; exact-name/type failure never
  substitutes another object from the same numeric slot;
- Bank/fallback settles before boot completion;
- ensure/recovery never overwrites a candidate before another recoverable v2
  source is durable;
- tracking/full-dirty setup is synchronous RAM work, but first drain begins only
  after the runtime gate;
- AutoSave OFF performs no A/B hidden-record I/O and reads/writes no HCNAMES
  source/type/pending metadata; ordinary name-only HCNAMES maintenance remains
  allowed for Save UI.

### 6.3 Runtime/power-cut matrix

Test single mutations and multi-generation continuations while alternating A/B;
normal Instrument/Kit/Scene/Bank Load and Save; Morph Save; rapid Kit/Instrument
scroll, type change, menu switch, and exit; HCNAMES write failure; target write/
sync failure; source short/overlong/CRC contradiction; AutoSave OFF->ON; and
runtime same-Bank Load/Save versus a true Bank slot/name transition.

Cut power after HCNAMES sync, target data sync, target CRC sync, commit write,
target final sync, during the HCNAMES-clear rewrite, and after HCNAMES clear. At
next boot, confirm either the untouched peer or the new generation validates;
use the verifier to confirm the future reader's pending/mask/source winner. For
S060 live behavior, confirm a parseable matching HCNAMES reconstructs its
sources, while malformed/missing HCNAMES takes the explicitly temporary
settings-Bank fallback even if discovery retained a matching record. A completed
Load/Save followed by shutdown without leaving the menu must not revert, because
the page guard leaves its synced HCNAMES pending source intact.

For scrolling, confirm old Kit loads may be briefly resident but are never DSP-
applied or HCNAMES-published when superseded; old Instrument stages are
discarded; the latest selection remains responsive; and exit/type/menu intent
runs only after final stable HCNAMES durability. Saves remain input-locked and
finish only after object, index, and required HCNAMES sync.

For normal Bank Load and Bank Save, cut power immediately after the reported
success without waiting for the autonomous settings debounce. On reboot,
`settings.cfg active_bank`, the HCNAMES Bank row, and the completed Bank/index
must already agree. Inject HCNAMES and settings-tail failures separately and
confirm neither path reports the Bank operation as complete or authorizes Q.
Confirm same-Bank operations retain the mounted cache and emit no Q; a true
transition invalidates at resident commit and emits Q only after both durability
tails succeed. A failed transition tail must leave Q unqueued and tracking off.

While either Load or Save page remains active, confirm due setup/drain work does
not start and pending HCNAMES rows remain set. After leaving, confirm the short
existing post-suppression delay admits the normal transaction, target final sync
precedes pending clear, and no in-flight writer is cancelled by page entry.

### 6.4 Performance/trace acceptance

An AutoSave-ON boot with an effective Bank emits one V; OFF/no-Bank boots do not.
Normal transactions and continuations emit no V/Q and begin mask merge directly
from the mounted source. Q appears only for re-enable, Bank transition, or
source contradiction. H appears only for a generation carrying pending HCNAMES
rows and shows its target-promotion/pending-clear outcome.
Compare admitted-to-mask-merged and total transaction time with Session 059
traces; the expected saving is the removal of roughly two whole-record
validation reads, not reduced target-copy length or CRC arithmetic.
Corrupt the cached source's physical CRC field and, separately, a non-CRC source
byte after discovery; the copy must reject both before target CRC publication,
restore work, and queue Q. A normal unmodified source must promote with its
recomputed CRC equal to both its physical stored field and the cached expected
CRC.

The implementation is complete only when the v2 verifier, build/map checks,
boot/runtime matrices, power-cut tests, and hardware trace timing are recorded
in the Session 060 handoff log.
