# Session 046 Handoff — Boot/HCNAMES Hardening, AutoSave Trace, And Rollback Reconciliation

DATE: 2026-08-07 through 2026-08-10

SESSION GOAL: Reconcile the accepted Session 045 AutoSave baseline, build
non-perturbing observability before any Phase 2 feature work, diagnose
intermittent boot filesystem failures, harden the HCNAMES singleton, verify the
user-testable Phase 1 scalar path, and investigate settings/Bank Save behavior
without repeating Session 045's multi-boundary failure.

COMPLETED: The production source is closed at rollback commit `c9807fa`
(`autosave trace logger working, pre steps 2 and 3 implementation`). That
commit retains the Session 045 scalar AutoSave baseline; ten-second boot
filesystem timeout logging and recovery; detailed, non-rearming Kit-quarantine
and Bank-load substep labels; failure-transparent Kit validation and boot load
checks; duplicate-safe, case-insensitive HCNAMES creation guards; and the
corrected `D/S/A/V/M/C/P/T` AutoSave lifecycle trace. The complete
user-testable scalar parameter scope is accepted as tested. Later Session 046
firmware experiments—generic one-millisecond filesystem pacing, runtime Bank
active-Scene preservation, settings notification changes, CRC chunking, and
unified `devlog.bin` output—were reset and are not present in this source.

VERIFIED ON HARDWARE:

- Yes: the `c9807fa` lineage built, booted, played audio, and completed normal
  Load/Save smoke tests after the Kit/Bank diagnostic and HCNAMES changes.
- Yes: the original coarse boot captures identified real ten-second hangs in
  `KITQUAR ` and `BANKLOAD`; normal boot is comfortably under ten seconds, so
  these were treated as non-progress, not legitimate slow work.
- Partly: the detailed Kit/Bank diagnostic implementation did not break normal
  boot, playback, or Load/Save. The intermittent hang did not recur with a new
  detail code, so no lower-level AsyncFATFS or SD primitive is proven faulty.
- Yes: two byte-identical physical `/.hcnames` entries were captured, the
  duplicate-safe absence proof was implemented, and repeated testing did not
  reproduce the duplicate. Fault-injection of every scan/open failure remains
  pending.
- Yes: the Step-1 trace writer's first hardware defect was found and corrected.
  The original append used no completion callback, left the shared facade in
  terminal `DONE`, and prevented settings, AutoSave, and later trace work.
  The retained callback acknowledges the background trace operation back to
  `IDLE` without advancing the ring after a failed append.
- Yes: after that correction, hardware fixtures showed expected scalar changes
  from two Scenes in the hidden records and a working lifecycle trace.
- Yes, accepted complete: every user-testable scalar class was exercised.
  Scene values are covered; Kit and Instrument values are covered; MIDI
  channel/note are Scene-owned and covered by the Scene tests. The current UI
  exposes no separate user-editable Bank scalar value. No further vague
  “coverage matrix,” “repeated edit,” or “idle” task is required.
- Yes, on a later observability build: settings changed in the UI were written
  and survived reboot. Because the final reset removed the later settings
  patch, the current `c9807fa` notification path must be retested before any
  settings source edit is reintroduced.
- Yes: Bank Save while AutoSave was off completed without the reported audio
  glitch. With AutoSave on, two similar glitches occurred a few seconds after
  leaving Load/Save, consistent with deferred AutoSave work rather than the
  foreground Bank Save itself.
- Yes: the failed generic one-millisecond pacing experiment made boot and Bank
  operations dramatically worse. Hardware showed a roughly ten-second first-
  parameter freeze, `ERR HNkL01`, `FSErr` on Bank Load from a non-default
  Scene, unwanted active-Scene switching, Bank Save/Load taking up to about a
  minute, and the same two glitches merely delayed.
- Yes: the later unified-log/duplicate experiment caused a boot timeout and
  left one AutoSave target at exactly 32,768 bytes instead of 34,768 bytes,
  with no durable `devlog.bin`. That experiment was reset.

CHANGES THIS SESSION THAT SURVIVE IN `c9807fa`:

- `Core/Hardware/SD/filesystem.c/.h`: boot failure writer generalized for
  timeout and caller-confirmed boot filesystem failures; private detail labels;
  Kit tri-state validation; Bank/Scene boot-load detail labels and completion
  checks; HCNAMES case-insensitive singleton matching and read-only root absence
  proof; AutoSave trace flush operation and corrected self-acknowledging
  completion callback.
- `Core/Hardware/SD/asyncfatfs/sdcard_lxr02.c/.h`: logging-only transfer
  abandon used by bounded boot recovery.
- `main.c`: consumes boot index/load acceptance and completion failures before
  Menu acknowledgement and routes confirmed failures through the bounded log
  recovery path.
- `Core/Bank/Scene/AutosaveTrace.c/.h`: logging-only 64-by-8-byte lifecycle
  ring and fixed record vocabulary.
- `Core/Bank/Scene/Autosave.c`: one dirty-producer trace call at the existing
  typed dirty funnel; the Session 045 wire format and scalar ownership remain
  unchanged.
- `config.h`: `DEV_MODE_DIAGNOSTIC` remains screen-only and disabled;
  `DEV_MODE_LOGGING` remains file-only and enabled; boot deadlines remain ten
  seconds.
- `Makefile`: includes the AutoSave trace source.

DOCUMENTATION CLOSEOUT CHANGES:

- `knowledge_files/log_archive/000_SESSION_INDEX.md`: appended Session 046
  source boundary, summary, and cross-session facts.
- `knowledge_files/log_archive/046_SESSION_HANDOFF_LOG.md`: this deletion-safe
  record.
- `knowledge_files/specification_reference/AUTOSAVE.md`: authoritative current
  AutoSave format, ownership, lifecycle, known CRC limitation, and extension
  rules.
- `knowledge_files/specification_reference/DEV_MODES.md`: authoritative
  two-mode diagnostic policy and current log-file behavior.
- Other specification-reference files received only the minimum corrections
  needed to defer AutoSave/diagnostic authority to those two documents and to
  remove stale pre-Session-045 claims.
- `SESSION_046_PLAN.md` and `AUTOSAVE_PHASE2_PLAN.md`: Session 046 is closed;
  completed testing is no longer presented as open work, and the remaining
  implementation is explicitly carried to Session 047.
- `MEMORY.md`: reconciled with `c9807fa`, the accepted hardware evidence, and
  failed approaches that must not be restored.

KNOWN ISSUES INTRODUCED: No known production-source issue was intentionally
introduced at the closeout boundary. Current logging still lacks duplicate-safe
singleton publication for `/bootlog.bin` and `/asavetrc.bin`; duplicate
physical entries have been observed. The current AutoSave trace file uses
append mode and must not be treated as crash-safe or duplicate-proof.

KNOWN ISSUES RESOLVED:

- Boot Kit I/O interruption can no longer be classified as malformed content
  and used as authorization to quarantine/rename a Kit.
- Boot Kit/Bank failures are consumed before later boot stages or Menu
  acknowledgement can hide them.
- Boot timeout detail can identify a concrete Kit, Bank, Bank-child, HCNAMES,
  or final-flush family without rearming the enclosing deadline.
- HCNAMES direct access is case-insensitive, and every create-capable path now
  requires a complete read-only root absence proof. A failed read/open is no
  longer permission to create a second singleton.
- The AutoSave trace background writer no longer strands the shared filesystem
  facade at terminal status after its first append.
- Session 045 scalar coverage is closed for every user-testable owner instead
  of remaining framed as an undefined matrix.

NEXT SESSION RECOMMENDED GOAL: Follow `SETTINGS_BANK_LOAD_REIMPLEMENT.md` from
`c9807fa`. First add/validate the read-only host fixture inspector if needed;
then bound every AutoSave CRC generation and validation path by bytes per
filesystem tick. Test that change alone. Only after it passes, preserve the
active Scene during user/runtime Bank Load while retaining the Bank's saved
default at boot. Retest settings before changing its notification path. Finish
with stopped/playing Bank Save integration and audio-glitch sign-off.

BLOCKERS:

- Every current AutoSave CRC path is not yet uniformly byte-bounded. The
  whole-record `autosave_initialRecordCrc()` remains at two active call sites.
- The 32,768-byte partial `.hcprms2` supports a separate FAT/cache/transport
  non-progress hypothesis. No lower-level fix is authorized without a capture
  that identifies the stalled application, allocator, cache, and SD states.
- Runtime Bank Load still follows the loaded Bank's saved active Scene. The
  explicit preserve-at-request contract is planned, not implemented.
- Winner selection still uses `autosave_streamValidationMatchesBank()` even
  though Bank slot/name are settled as mutable payload. That current/future
  contract gap must be resolved before whole-object Bank session publication.
- Settings persistence passed in a later build but has not been reconfirmed at
  the exact rollback boundary.
- Duplicate-proof logging is unresolved. Do not combine it with CRC, Bank
  Load, or settings work.
- Whole-object Phase 2 AutoSave hooks and Load/Save exclusion remain
  unimplemented and must wait for the Session 047 CRC/settings/Bank baseline.

CRITICAL REMINDERS FOR NEXT SESSION:

- Production authority is commit `c9807fa`, not a later working-tree image.
- Do not restore the generic one-millisecond pacing layer or any blind delay.
- Every AutoSave CRC producer and validator—not only initial creation—must use
  one explicit byte budget and yield between chunks.
- CRC computation must complete before a create/truncate mutation where the
  existing state machine permits it; pair recovery must publish one complete
  record before starting the other.
- A 32,768-byte file is invalid evidence of a failed write. Never accept,
  pad, repair, or delete it before preserving the fixture.
- A NULL/failed open is not proof that a singleton is absent. Creation requires
  a complete case-folded directory scan; scan/open errors remain errors.
- Diagnostic detail may observe a failure but may not reset its deadline,
  convert it to success, clear dirty state, or change operation ownership.
- `DEV_MODE_DIAGNOSTIC` owns screen output only. `DEV_MODE_LOGGING` owns file
  output only, and logging-only RAM/file work must disappear when it is off.
- Current file sinks are `/bootlog.bin` and `/asavetrc.bin`; `/devlog.bin` is
  rejected historical work, not current behavior.
- Do not reopen the scalar parameter matrix. Future work concerns CRC pacing,
  settings verification, runtime Bank active-Scene semantics, whole-object
  publication, and later Load/Save exclusion.

---

## 1. Repository authority and worktree boundary

The retained firmware source is commit:

```text
c9807fa autosave trace logger working, pre steps 2 and 3 implementation
```

The working tree also contains user-owned SD-card fixtures, generated Scene and
Bank test data (including `Bank/013 LoadTst`), planning documents, and this
closeout documentation. Those artifacts are not evidence that firmware changes
after `c9807fa` survived. Do not reset, restore, clean, or normalize the
`SD_CARD/` tree as part of Session 047 source work.

The linked `build/lxr02.elf` inspected at closeout reports:

```text
text 372,908 B
data     400 B
bss   78,996 B
```

Section accounting is 75,824 bytes static SRAM1 (`.dma_nocache` 3,100 +
`.data` 400 + normal `.bss` 72,324), 12,280 bytes DTCM, and 88,104 bytes total
static allocated RAM. SRAM1 remaining is 301,008 bytes. With logging enabled,
the trace-specific static allocation is 520 bytes: 512-byte ring, six bytes of
cursor/drop state, and two-byte flush cadence.

## 2. Chronological Session 046 record

1. Session 045's many root planning files were classified and consolidated.
   `SESSION_045_CONSOLIDATED_POST_MORTEM.md` captured the accepted baseline,
   failed Phase 2 sequence, and document disposition. `SESSION_046_PLAN.md`
   established sequential remediation.
2. Diagnostic policy was simplified to exactly two modes: screen output under
   `DEV_MODE_DIAGNOSTIC`, file output—including trace—under
   `DEV_MODE_LOGGING`. The 520-byte trace allocation was approved only when
   logging is compiled in.
3. Preserved boot logs contained the exact coarse tokens `KITQUAR ` and
   `BANKLOAD`. Because normal complete boot takes well under ten seconds, the
   timeout was retained at ten seconds per operation and treated as evidence
   of indefinite non-progress.
4. `KITQUAR_FIX.md` and `BANKLOAD_FIX.md` were expanded through the actual
   source paths. The implementation added detail labels, explicit error
   propagation, and boot completion checks without changing SD/SPI timing or
   declaring a speculative low-level fix.
5. A later card copy contained two byte-identical HCNAMES files. The direct
   access policy was changed to case-insensitive singleton matching, followed
   by a shared root absence proof on every create-capable path. Hardware retest
   did not reproduce the duplicate.
6. Current source was re-audited against `AUTOSAVE_PARAM_HOOK.md`. Phase 1
   scalar hooks were present, and subsequent user tests covered every
   user-testable class. Stale matrix language was rejected.
7. `AUTOSAVE_REMEDY_PA2ST1.md` was implemented. The first hardware trace
   exposed a diagnostic scheduler defect: the trace flush completed without a
   callback and stranded the facade. The callback correction survives in
   `c9807fa`; later tests showed correct parameter updates and trace flow.
8. `AUTOSAVE_REMEDY_PA2ST2-3.md` was reconciled to use the Step-1 trace and
   copied hidden files, not a second 16-byte firmware snapshot. No Step-2/3 C/H
   additions were required. The user accepted scalar hardware validation as
   complete.
9. Settings were found not to survive one test, and audio glitches appeared
   after Bank Save. A targeted settings/Bank background plan was written.
   Settings later survived reboot in an observability build. With AutoSave on,
   two glitches occurred after leaving Load/Save, implicating deferred AutoSave
   work.
10. A generic one-millisecond filesystem pacing attempt was hardware-rejected.
    It delayed all work, caused severe boot/load/save regressions, and delayed
    rather than eliminated the glitches. The user reverted it.
11. The replacement conclusion was to byte-limit all CRC generation and
    validation, not only initial-record generation, and preserve the active
    Scene explicitly during runtime Bank Load. Those changes were planned but
    are not in `c9807fa`.
12. Test Scene fixtures were generated from the Kit library and a mixed
    `013 LoadTst` Bank was added to the copied `SD_CARD/` tree. These are test
    data, not firmware behavior.
13. Duplicate `asavetrc.bin` entries prompted a later attempt to unify all file
    logging under `devlog.bin` and prevent duplicate creation. That experiment
    combined too many boundaries, booted unsuccessfully, produced no durable
    unified log, and left a 32,768-byte `.hcprms2`. The repository was reset to
    `c9807fa`.
14. `SETTINGS_BANK_LOAD_REIMPLEMENT.md` and
    `HCPRMS_BOOTLOCK_DIAGNOSIS.md` were written to preserve the corrected
    Session 047 order and the separate 32 KiB write-lock hypothesis. The latter
    still names the rejected `devlog.bin` experiment and must be reconciled
    before any diagnostic implementation.

## 3. Deletion-safe consolidation of the named historical documents

This section is intended to preserve the material needed after the user
deletes the named root planning documents.

### 3.1 `SESSION_045_CONSOLIDATED_POST_MORTEM.md`

The document reconciled Session 045 to its accepted production boundary and
classified the root AutoSave plans. Its enduring facts are:

- The accepted foundation is two exact 34,768-byte hidden records, one
  3,856-byte canonical SRAM mask, a bounded foreground drain, Phase 1 scalar
  hooks, a 33-line settings schema, and the AsyncFATFS free-cluster-wrap and
  leading-dot alias fixes.
- The failed Phase 2 branch combined validator semantics, Bank ownership,
  Scene/Kit/Instrument publication, Menu alignment, and Load/Save exclusion in
  unisolated passes. It must not be copied from `*.failed` source.
- Bank slot/name are mutable payload, not grounds to reject an otherwise valid
  record as a foreign identity.
- Broad Bank operations cannot substitute for exact successful commit
  boundaries; partial Bank masks preserve unselected resident data.
- Menu preview/selection is not proof of resident publication. A previous
  apparent Scene success was caused by unrelated Menu reset behavior.
- The final failed exclusion pass stopped the common writer path, including
  pre-existing dirtiness, because starts/completion ownership was changed
  without lifecycle observability.
- Initial records contain identity plus zeroed parameter content. This is a
  valid current-format record but not a complete resident-Bank snapshot.
- Pattern and live Effect persistence remain outside the current AutoSave
  payload.
- Session 046 superseded the post-mortem's open scalar-matrix caveat: all
  user-testable scalar classes are now accepted as tested.

Historical file disposition recorded there:

- `AUTOSAVE_FILES.md`, `AUTOSAVE_WRITER.md`, `AUTOSAVE_PARAMETERS.md`,
  `AUTOSAVE_IMPLEMENTATION.md`, `AUTOSAVE_SETTINGS.md`,
  `AUTOSAVE_SINGLE_RECORD.md`, and `AUTOSAVE_PARAM_HOOK.md` were historical
  implementation records whose durable facts should move into logs/specs.
- `AUTOSAVE_PARAM_HOOK_failed.md` was a post-mortem/reference, not source.
- `AUTOSAVE_PARAM_HOOK_FOLLOWUP_failed.md` was stale and abandoned.
- The live forward plans were `AUTOSAVE_PHASE2_PLAN.md` and its remedy plans;
  Session 047 now uses `SETTINGS_BANK_LOAD_REIMPLEMENT.md` before returning to
  the remaining Phase 2 work.

### 3.2 `BOOT_LOGGING.md`

The plan introduced the cooperative pre-audio filesystem watchdog and bounded
recovery logger. Its current contract is:

- Logging starts immediately before SD mount and ends before runtime/audio
  filesystem work. Runtime operations are not subject to dirty-abandon boot
  policy.
- Each boot filesystem operation owns one ten-second deadline using wrapping
  16-bit `time_sysTick`; ten seconds is below the safe 32,768 ms half-range.
- Public arm operations copy exactly eight bytes. Detail hooks may replace only
  the retained label and never reset the operation start tick.
- Both facade polling (`filesystem_tick()`) and private blocking FAT polling
  observe the deadline, so nested Kit validation/quarantine loops can unwind.
- Timeout does not enter the normal success/flush path. The recovery sequence
  abandons the private SD transfer without callback, destroys dirty AsyncFATFS
  state, clears facade ownership, remounts once, and makes one bounded best-
  effort log write. Whether logging succeeds or fails, boot continues to audio.
- The retained current sink is root `/bootlog.bin`, exactly eight bytes, direct
  write/create/truncate. A successful boot leaves an existing file untouched.
- Recovery can leave a timed-out filesystem mutation partially represented on
  card; this behavior exists only in a logging build and is diagnostic, not a
  general cancellation API.
- Logging-disabled builds compile out timeout state/recovery behavior and must
  not allocate logger RAM or perform logger file I/O.
- The original plan used the temporary name `DEV_LOGGING` and placed OLED
  diagnostics under it. Current source deliberately supersedes that naming:
  file logging is `DEV_MODE_LOGGING`; screen diagnostics are separately gated
  by `DEV_MODE_DIAGNOSTIC`.

The operation vocabulary includes mount, Kit/Scene/Bank scans and index work,
name repair, Kit quarantine, Instrument scans/indexes, Bank/Scene/Kit index
loads, Bank/Scene/Kit payload loads, globals, AutoSave ensure, HCNAMES, and
final flush. Later Session 046 detail codes narrow Kit and Bank families.

### 3.3 `KITQUAR_FIX.md`

Evidence was one exact `KITQUAR ` boot record after ten seconds. The active
path is boot Kit index creation: root-name repair, Kit validation/quarantine,
root scan, then `.hcindex` publication. Only root Kit quarantine is active;
recursive Bank-tree quarantine helpers are compiled out.

The source-backed indefinite-hang hypothesis is a non-advancing AsyncFATFS/SD
wait, not a parser loop or proven handle exhaustion. Candidate waits are root
or Kit directory open/chdir/close, object finder progress, `kitset.kcg` or one
of six member opens/reads/closes, quarantine rename, and sync. The active path
uses at most three application handles against the five-handle pool.

Retained detail labels are:

| Label | Boundary |
| --- | --- |
| `KQROOT  ` | FAT root and `/Kit` entry/return/close |
| `KQSCAN  ` | next root Kit object |
| `KQnnnDIR` | numbered Kit directory open/chdir/close |
| `KQnnnKST` | that Kit's `kitset.kcg` open/read/parse/close |
| `KQnnnI0x` | one declared Instrument member, index 0–5 |
| `KQnnnREN` | invalid-content quarantine rename/sync |

The important functional correction is tri-state validation:
`VALID`, `INVALID_CONTENT`, and `IO_ABORT`. Missing/malformed required content
while FAT remains healthy can authorize quarantine. Timeout, lost FAT-ready
state, failed blocking primitive, or interrupted stream is `IO_ABORT` and can
never reach rename. Quarantine/index failure propagates to the boot caller;
partial traversal is not published as a valid empty library.

Ordinary non-timeout boot failure also uses the bounded boot failure writer.
The main caller no longer discards the index result with `(void)`. No SD-driver,
SPI timing, AsyncFATFS retry, or handle-count change was justified. The build
passed with logging on and off; the historical measured BSS difference for the
Kit/Bank logging state was 32 bytes. Hardware normal-boot smoke passed, but
malformed/interrupted FAT fixtures and recurrence of a concrete `KQ...` label
remain untested.

### 3.4 `BANKLOAD_FIX.md`

Evidence was two exact `BANKLOAD` timeout records while boot loading `000 Full`
with the full `0xffff` Scene mask. The loader reads HCNAMES, `/Bank`, the
selected Bank directory and `bankset.bcg`, scans local `00..15` children,
delegates selected payloads through the shared Scene/Kit/Instrument/Pattern/
Effect reader, rewrites merged HCNAMES, and completes a final sync.

The indefinite-hang hypothesis is the same class of non-progress at a more
complex state machine: accepted open without callback, close without callback,
zero-byte non-EOF text reads, object finder remaining in progress, chdir/parent
transition, write back-pressure, or sync never completing. A coarse Bank token
does not justify changing all child loaders or Bank ownership.

Retained labels are:

| Label | Boundary |
| --- | --- |
| `BKHCREAD` | pre-load root HCNAMES read |
| `BKROOT  ` | root `/Bank` directory work |
| `BKnnnDIR` | selected root Bank directory |
| `BKnnnSET` | selected `bankset.bcg` |
| `BKnnnSCN` | local Scene-child enumeration |
| `BnnnSssO` | concrete Bank `nnn` child Scene `ss` open/discovery |
| `BnnnSssK` | that child's Scene metadata/embedded Kit family |
| `BnnnSssI` | that child's Instrument sequence |
| `BnnnSssP` | that child's Pattern/Effect/parent-return family |
| `BKHCWRIT` | final merged HCNAMES write |
| `BKFLUSH ` | successful Bank Load's separate final-flush deadline |

Shared Scene labels are emitted only when the current owner is Bank Load;
ordinary root Scene Load retains its own taxonomy. `BKFLUSH ` changes only the
label of the existing fresh final-flush deadline; it does not extend or reset
the enclosing Bank deadline and cannot overwrite an error.

Boot now checks Bank-index request acceptance and terminal status, Bank-load
request acceptance, and Preset completion before Menu acknowledgement. Only a
successful empty Bank/index may enter the ordinary fallback ladder. Root
Scene/Kit fallback index requests follow the same acceptance/status rule.
Every normal malformed/open/read/close/chdir/HCNAMES error remains an error;
diagnostics cannot turn it into an empty/success case.

The build passed with logging on/off. Normal hardware smoke passed; repeated
cold boots, known empty/malformed Bank fixtures, and a reproduced concrete
`BK...` timeout remain the evidence gate before any AsyncFATFS/SD change.

### 3.5 `DUAL_HCNAMES_FIX.md`

The captured duplicate entries had been renamed to `.hcnames1` and
`.hcnames2`, so their original LFN/SFN chains were unavailable. Both copies
were exactly 1,063 bytes, contained the required 129 rows, and had identical
hashes. This proved a directory-entry duplication, not conflicting identity
content.

The first correction made every HCNAMES reader/writer use one private
case-insensitive match policy. FAT is case-insensitive; a host-created case
variant cannot be treated as a missing singleton eligible for creation.

The stronger correction addressed the failed-open ambiguity. A NULL open can
mean absence or I/O failure. Before any bootstrap/create path, the shared
`filesystem_hcnamesProbeBegin()` / `filesystem_hcnamesProbe_tick()` performs
one asynchronous, read-only root scan, reusing ordinary operation scratch. It
counts case-folded display-name matches, saturating at two, and produces:

- absent only after complete scan and successful close—creation may proceed;
- exactly one match—one normal folded re-read is allowed;
- duplicate—report error and perform no card mutation;
- root-open/finder/close/FAT failure—report error and perform no card mutation.

The unique-match retry occurs once, not in a loop. A second NULL is an error.
The proof guards the boot blocking writer, targeted Scene/Kit/Instrument
updates, and Bank Load's first-use HCNAMES path. Existing rewrite paths that
already opened the file successfully remain ordinary rewrites.

Bank proof labels add `BKHCROOT`, `BKHCSCAN`, and `BKHCDUP ` without rearming
`BANKLOAD`; direct read/write retain `BKHCREAD` and `BKHCWRIT`. No automatic
delete, rename, merge, or repair policy was added. Build and diff checks passed;
hardware retesting found no recurrence, while controlled failed-read/finder
and deliberate duplicate fixtures remain pending.

This is the project-wide singleton lesson: case-insensitive open is necessary
but insufficient. A create-capable path requires positive absence proof, and a
failed read must remain visible rather than bootstrap a replacement.

### 3.6 `AUTOSAVE_PARAM_HOOK.md`

This is the completed Phase 1 scalar implementation record plus an unimplemented
whole-object Phase 2 design. The Phase 1 architecture that survives is:

- One 3,856-byte volatile canonical dirty mask in `Autosave.c` and one-byte
  mutation-tracking gate. Producers perform SRAM-only typed marks.
- Wire-offset arithmetic remains private to AutoSave. Other owners select a
  Bank field, Scene/Kit parameter, or Instrument descriptor coordinate.
- Foreground classification uses one PRIMASK-protected atomic bit take. A
  producer after the take re-sets the bit for the next generation; a producer
  before the take is reflected by the subsequent live getter. Interrupts are
  never disabled around getters, CRC, loops, or filesystem work.
- BankData owns changed-value marks for restore slot, display name,
  Scene-present mask, active Scene, and VOICE edit mask, including hidden edit-
  mask invariant repair.
- SceneData owns all 40 current Scene scalar cells: global/per-voice Morph,
  Scene decimation, six audio outputs, six FX sends, six faders, seven MIDI
  channels, and seven MIDI notes. It also owns the two generated Kit decay
  endpoints.
- Preset owns generic changed-value marks for Instrument normal and Morph
  descriptor images and supplemental non-morphable normal selectors. Current
  registry coverage is 39 Drum, 38 Snare, 39 Cymbal, and 39 HiHat normal
  descriptors; Morph cells are gated by each descriptor's Morphable flag.
- Runtime-only interpolation, selector mirrors, LFO overlays, Pattern, staging
  objects, and nonexistent Effect state remain unmarked.
- Scene/Kit/Instrument names remain HCNAMES-owned and are not falsely projected
  as live AutoSave bytes. Instrument type is gettable and belongs to whole-
  Instrument scope.
- Named future region markers exist for Whole Instrument, same-type Instrument
  Normal, same-type Instrument Morph, Kit, Effect, Scene without Pattern, and
  Scene with Pattern. Effect has zero live parameters; Scene-with-Pattern is a
  documented non-Pattern alias/TODO. Marker existence is not proof that a
  load/copy/paste caller publishes that region.
- Successful boot pair setup enables tracking and schedules one delayed record
  recovery/mask import. After recovery, a clean mask disarms and performs no
  recurring hidden-file operation. First later dirtiness uses five seconds;
  backlog uses 250 ms; errors use the normal retry path.

Future parameter rule: every retained field needs a named wire index, a live
getter, and either a changed-value owner setter or one successful post-commit
region marker. Direct whole-object assignment should commit validated data once
and invoke the matching region marker; it should not call hundreds of scalar
setters.

The document's Phase 2 section is not implemented. It proposed a single
`autosave_replaceResidentBankSession()` boundary, an unnotified staged Bank
metadata commit, and success-only publication for whole Kit, root Scene, whole
Instrument, Morph projection, and Bank Load/Save. Those ideas must be reviewed
against current source after Session 047's CRC/settings/Bank baseline; they are
not authorization to patch now. Its key constraint remains valid: failed or
merely staged operations publish no new region/session bits, and partial Bank
operations must derive the final resident state rather than assume all sixteen
Scenes were replaced.

Build evidence recorded by the historical Phase 1 implementation: one exact
3,856-byte mask, unchanged 4,608-byte patch cache, no second mask or payload
buffer, successful build/diff checks, and BSS 78,436 bytes at that earlier
checkpoint. Session 046 later hardware testing supersedes its “hardware
pending” status for the complete user-testable scalar classes.

### 3.7 `AUTOSAVE_REMEDY_PA2ST1.md`

The Step-1 goal was observability before whole-object features. It added no
Phase 2 publication or Load/Save exclusion. RAM approval was exactly 520 bytes
under `DEV_MODE_LOGGING`: 512-byte record ring, six bytes of ring cursors/drop
count, and a two-byte filesystem flush cadence. Logging-off builds retain no
trace storage or file activity.

Ownership is split deliberately:

- `AutosaveTrace.c/.h` owns fixed-width records, ring cursors, overwrite/drop
  accounting, and no I/O.
- `Autosave.c` emits `DIRTY` from its single payload-offset funnel after the
  tracking/range guard.
- `filesystem.c` emits lifecycle edges and owns the lowest-priority append,
  close, sync, retry, and durable flush-cursor advancement.

Each trace record is eight bytes:

```text
byte 0      stage ASCII
byte 1      stage flags/outcome
bytes 2..3  low 16 bits of time_sysTick, little-endian
bytes 4..7  stage-specific uint32 value, little-endian
```

The 64-record ring is power-of-two sized and exactly fills the existing
512-byte staging buffer when serialized. On overflow, the oldest undurable
record is dropped, the flush cursor advances to retained data, and a saturated
drop counter records that the trace is incomplete. Whole-region marking can
legitimately flood `DIRTY`; the ring is optimized for isolated scalar tests.

Stage meanings are:

| Stage | Meaning |
| --- | --- |
| `D` | one real payload offset was marked dirty; value is the offset |
| `S` | scheduler observed first dirtiness and armed debounce |
| `A` | filesystem accepted the drain and transaction ownership became active |
| `V` | both candidates were validated; flags identify winner/index, value generation |
| `M` | winner mask was fully merged/closed; flags show remaining dirtiness |
| `C` | bounded classify/capture exited; flags show budget exhaustion, value patch count |
| `P` | commit byte was durable and closed; flags identify target, value new generation |
| `T` | writer completion callback ran; flags distinguish success/error |

The trace appends to `/asavetrc.bin` in case-insensitive append mode. It
serializes at most one ring batch, advances the ring's durable cursor only
after sync, and runs after settings and AutoSave scheduling on idle ticks. A
failed append retains pending records. The public blocking flush helper exists
for deliberate bench boundaries only and acknowledges the terminal facade
state it consumes.

The first implementation omitted a completion callback on the autonomous
append. Generic completion therefore left `status == DONE`, while all three
background schedulers require `IDLE`; one `S` record became durable, but later
AutoSave, settings, and trace operations stopped. The retained
`filesystem_autosaveTraceFlushCompleted()` callback acknowledges both success
and error to `IDLE`. It does not mask append failure because the ring cursor
advances only after durable sync.

Build evidence: logging-on text/data/BSS was 372,876/400/78,996 at the Step-1
checkpoint; logging-off was 367,212/396/78,444 and contained no trace symbols.
The current closeout build remains BSS 78,996. Hardware testing after the
callback correction demonstrated that scalar records and later trace batches
could progress.

## 4. Additional accepted behavior and unresolved work

### 4.1 Scalar coverage is closed

Do not carry the old Phase 1 matrix language into Session 047. The user tested
the parameter classes that can actually be changed:

- Scene values: complete for the purpose of scalar-hook acceptance.
- Kit/Instrument values: complete.
- MIDI channel/note: part of Scene and complete.
- Bank scalar values: no separate user-editable UI values exist.

This acceptance does not claim whole-object load/copy/paste publication,
Pattern persistence, Effect persistence, power-cut coverage, or every possible
descriptor value. It closes the ownership classes, not unrelated Phase 2 work.

### 4.2 Current settings state

The source contains a version-1 33-line keyed settings writer with one-second
debounce, change revision, retry behavior, `autosave`, and sixteen
`scene_source_NN` values. Successful Scene/Bank provenance callbacks mark
settings dirty only after `FS_STATUS_DONE` and use the actual operation masks.

One rollback-lineage test reported changes not surviving reboot. A later
observability build did persist and reload changed settings. Session 047 must
test `c9807fa` before editing. If RAM changes but no revision is issued, add one
allowlisted changed-setting notification at the owner boundary. If open/write/
close/sync fails, fix only that proven storage boundary; do not patch Menu as a
guess.

### 4.3 Bank Save/audio pressure evidence

The glitch was conditional on AutoSave being enabled and occurred seconds
after leaving Load/Save. Captured timing isolated whole-record CRC work of at
least 77.7 ms. The evidence supports cooperative byte-bounded CRC generation
and validation. It does not support delaying every filesystem phase.

The reverted one-millisecond pacing experiment made both foreground operations
and boot drastically slower while preserving the same audible glitch shape.
This is strong negative evidence against blind delays. Session 047 must bound
CPU work by bytes and leave ordinary SD request polling unchanged.

### 4.4 Active Scene during Bank Load

Current runtime Bank Load restores the loaded Bank's saved default Scene. The
requested product behavior is:

- boot Bank Load: restore the Bank's saved active Scene;
- user/runtime Bank Load: preserve the Scene active when the request was
  accepted, even if the selective mask excludes it;
- failed Bank Load: keep the prior active Scene and report the failure.

The planned fix is an explicit request parameter/captured operation-local
coordinate, not inference from mutable Menu state and not a timing change.

### 4.5 Partial 32 KiB AutoSave record

The failed post-`c9807fa` experiment left `.hcprms1` valid at 34,768 bytes and
`.hcprms2` at exactly 32,768 bytes with a deterministic initial-record prefix.
This supports a cluster-extension/cache/transport non-progress theory but does
not identify which layer stalled. CRC monopolization and physical write
extension are separate problems.

If it recurs, capture target, ensure phase, bytes requested/completed, file
cursor/size, actual cluster geometry, AsyncFATFS operation and append subphase,
cache ownership, SD state/block/offset/retry, primary timeout label, and logger
recovery outcome before changing FAT or SD code. `HCPRMS_BOOTLOCK_DIAGNOSIS.md`
contains a detailed prospective capsule design, but its `devlog.bin` assumptions
are stale after rollback and must first be reconciled with `DEV_MODES.md`.

## 5. Failed approaches and prohibitions

- Do not restore any `*.failed` Phase 2 source by diff or copy.
- Do not use Bank slot/name mismatch as record rejection identity.
- Do not move publication repeatedly between filesystem, Preset, Menu, and boot
  without a trace showing which accepted boundary was missed.
- Do not combine whole-object hooks, scheduler exclusion, active-Scene
  ownership, settings, and logging in one hardware pass.
- Do not add generic one-millisecond or fixed-interval filesystem pacing.
- Do not make a diagnostic writer's terminal status depend on a foreground
  caller that does not exist; autonomous operations must release facade
  ownership while preserving failed data for retry.
- Do not combine log-file consolidation/duplicate prevention with AutoSave CRC
  or writer changes.
- Do not interpret absent `bootlog.bin`, `asavetrc.bin`, or `devlog.bin` as
  proof that no failure occurred. The same SD subsystem may prevent its own
  failure record from becoming durable.
- Do not create a singleton after a NULL open without a complete folded root
  absence proof.
- Do not hide malformed/open/read/close/chdir/sync failures behind an empty
  library, fallback, successful callback, or diagnostic acknowledgement.
- Do not accept a partial AutoSave file, clear dirty bits after failure, or
  claim CRC chunking fixes a FAT cluster-extension lock.

## 6. Session 047 execution order

1. Confirm `c9807fa`, build it unchanged, record size/RAM, and preserve the
   user-owned SD fixtures.
2. Use or add a read-only host inspector for exact 34,768-byte records, CRC32C,
   commit/generation, mask population, relevant payload offsets, and existing
   eight-byte trace records. It must never modify fixtures.
3. Implement one byte budget for all AutoSave CRC creation, recovery,
   validation, and transformed-copy paths. No sleep and no new permanent SRAM.
4. Hardware-test valid-pair scalar drain, missing-pair creation, invalid-pair
   recovery, audio playback, exact file sizes, CRCs, and absence of timeout.
5. Implement explicit runtime preserve-active-Scene Bank Load semantics and
   test full/selective/failure/boot cases separately.
6. Test settings persistence at the current boundary. Patch only the proven
   missing notification or filesystem boundary.
7. Run Scene/Bank provenance, stopped/playing Bank Save, AutoSave OFF→ON, and
   one scalar regression integration pass.
8. If the 32 KiB boot lock recurs, stop and capture the exact lower-level state
   before any FAT/SD change.
9. Only after this baseline passes, return to whole-object Phase 2 hooks one
   successful public completion boundary at a time; Load/Save exclusion remains
   last.
