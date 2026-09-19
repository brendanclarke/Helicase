# Session 047 Handoff — Bounded AutoSave CRC, Trace Arbitration, and HCPRMS Capture

**Date:** 2026-08-10  
**Repository:** `/Users/bc/Helicase Project/Helicase-check-fs/Helicase`  
**Scope:** restore one bounded AutoSave baseline at a time after Session 046's
rollback; do not revive generic filesystem pacing, `old*` directory cleanup,
or the failed unified `/devlog.bin` experiment.

## End-of-session block

```text
DATE: 2026-08-10
SESSION GOAL: Make all AutoSave CRC work cooperative and bounded, then test
the smallest related Bank/settings/boot behaviors without stacking a general
filesystem timing or replacement change.

COMPLETED:
- Every AutoSave CRC path is bounded to 128 bytes per filesystem tick.
- Fixed the missing-pair selector defect found during the CRC boot test.
- Kept the accepted settings writer unchanged after user retest passed.
- Prevented optional trace-file admission from competing with a Load/Save page.
- Added a logging-only ASENSURE forensic capsule for a recurring 32 KiB boot
  write stop; later boots were successful, so the underlying lower-layer cause
  is captured but not claimed fixed.

VERIFIED ON HARDWARE:
- Repeated playing-time loads of 000 Full and 013 LoadTst: no audible glitch.
- Saves over slots 024 and 009: user reported normal behavior.
- settings.cfg persistence: user re-tested and accepted; no source change.
- Following the one 32 KiB timeout fixture, multiple later boots produced two
  complete 34,768-byte hidden records and no bootlog.

KNOWN ISSUES INTRODUCED: None intentionally. The 32 KiB initial-write stop is
an intermittent observed lower-layer problem, not a newly-created known cause.

KNOWN ISSUES RESOLVED:
- Unbounded initial/recovery/validation/copy CRC work.
- Missing A record caused by reusing the A/B selector scratch field as CRC state.

NEXT SESSION RECOMMENDED GOAL: Keep Bank Load/Save changes isolated. First
choose one evidence-backed Bank defect; do not start whole-object AutoSave
Phase 2 or recursive-delete work in the same pass.

BLOCKERS: A recurrence of the 32 KiB boot stop requires preserving SD_CARD and
decoding bootlog.bin before an AsyncFATFS/SD repair. Recursive overwrite needs
a dedicated AsyncFATFS deleteTree implementation/test session.

CRITICAL REMINDERS:
- Never restore generic one-millisecond filesystem pacing.
- Do not implement old-directory rename/boot cleanup; fix native recursive
  deletion for Bank, Scene, and Kit replacement.
- Runtime Bank Load preserving the active playing Scene is still deferred;
  boot must continue to select the Bank's saved default Scene.
- The settings writer passed: do not change it speculatively.
```

## Starting point and method

Session 046 ended at rollback baseline `c9807fa`, retaining the accepted
Session 045 scalar AutoSave architecture and `D/S/A/V/M/C/P/T` trace while
discarding later pacing, unified-log, active-Scene, and boot-lock experiments.
The immediate working plan was `SETTINGS_BANK_LOAD_REIMPLEMENT.md`. Its key
constraint was one isolated behavior per build/hardware test: byte-bound all
CRC work first; no delay loop, timing layer, record-sized buffer, new CRC
allocation, Bank-format change, or speculative settings rewrite.

The durable authorities after this session are `AUTOSAVE.md`, `DEV_MODES.md`,
`FILESYSTEM_SPEC.md`, `ASYNCFATFS_REFERENCE.md`, `SRAM_MANIFEST.md`,
`AUTOSAVE_PHASE2_PLAN.md`, and this handoff. The now-redundant detailed
working documents `SETTINGS_BANK_LOAD_REIMPLEMENT.md`,
`HCPRMS_BOOTLOCK_DIAGNOSIS.md`, `AUTOSAVE_REMEDY_PA2ST1.md`, and
`AUTOSAVE_REMEDY_PA2ST2-3.md` may be deleted after the user has preserved the
repository state.

## 1. Bounded AutoSave CRC implementation

### Requirement and implementation boundary

The hidden A/B record is exactly 34,768 bytes. The old creation and
no-valid-record recovery path calculated CRC32C across the entire creation
image in one foreground call. That monopolized the cooperative loop; one
observed interval was at least 77.7 ms. Candidate validation and transformed
copy already streamed parts of their work, but no uniform byte cap protected
all four CRC consumers.

The implementation uses `AUTOSAVE_CRC_BYTES_PER_TICK == 128` and retained
operation cursors. It applies that same cap to:

1. initial A/B record creation;
2. recovery when neither candidate is valid;
3. existing candidate validation; and
4. CRC generation for a transformed copy.

It does not allocate a record image, delay the main loop, add a filesystem
pacing mechanism, change record geometry/commit order, alter Bank Load
semantics, or change settings persistence. The initial/recovery CRC is
prepared before create/remove mutations; file formatting and partial-write
handling remain in the existing bounded stream state machine.

Both configurations built during the session. After the later capture work,
the final builds were:

| Configuration | text | data | bss |
| --- | ---: | ---: | ---: |
| `DEV_MODE_LOGGING=1` | 374,044 | 396 | 79,076 |
| forced `DEV_MODE_LOGGING=0` | 367,372 | 396 | 78,444 |

The normal logging-on configuration was restored and rebuilt. Existing
compiler warnings were not introduced by this work.

### Deterministic selector defect found by the missing-pair test

After the hidden pair was removed and the unit booted, the copied fixture
showed a complete committed `/.hcprms2` but no `/.hcprms1`. The eight-byte
trace contained only `S` (scheduled), consistent with successful boot setup
and a later armed writer rather than a child Bank-load error.

Cause: the boot ensure operation uses `op_stream_index` as its zero-based A/B
target selector, saves it to `op_file_version` before the CRC pass, and then
reuses `op_stream_index` as the CRC32C accumulator. Ensure phase 10 reopened
the destination with `filesystem_autosaveTargetName()`, which interprets a
nonzero value as B. Thus a missing A could be created as B; the next scan saw B
and falsely concluded that the pair existed.

Correction: phase 10 opens `filesystem_autosaveFilenameForIndex(op_file_version)`.
`op_file_version` is the deliberately retained selector and remains stable
through the CRC pass. No public interface or new storage was needed. Source
comments adjacent to the change explain both the reused scratch field and why
the saved selector is authoritative.

## 2. Load/Save trace arbitration and user testing

The first load of `Bank/013 LoadTst` displayed `FsErr` after its payload had
already committed: the copied `settings.cfg` showed `active_bank=13` and
source rows for all selected scenes, while `.hcnames` held Bank/Scene/Kit
identity rows. This pointed to a post-payload facade arbitration failure when
Menu requested a read-only `/Bank/.hcindex` restore, not evidence that the
bounded CRC or Bank child parser failed.

The targeted correction was intentionally small: while the Load or Save page
is active, the optional AutoSave trace flush leaves `asavetrc.bin` records in
its RAM ring and does not claim the one shared filesystem facade. It changes
neither the trace format, settings writer, AutoSave writer deadline, Bank
loader, final index request, nor generic operation timing. The trace can flush
normally after the page is no longer active.

Hardware result supplied by the user: repeatedly load `000 Full` and
`013 LoadTst` while playing; no audio glitch was heard. The user also saved
Full over 024 and LoadTst over 009. This is accepted as a focused
trace-arbitration/audio result only. It does **not** prove all Bank Save/Load
behavior is stable or authorize a global Load/Save exclusion feature.

## 3. Settings result

The current `settings.cfg` writer already has a one-second trailing debounce,
revision protection, retry behavior, and Menu dirty notifications for the
settings schema. The user re-tested it in Session 047 and explicitly reported
it fine and requested no change. No settings source file was altered. Keep
this as a hard regression boundary: a future change needs a specific fixture
and failing owner/callback before touching settings flow.

## 4. One 32 KiB HCPRMS boot timeout: evidence, limits, and capture build

### Captured failure fixture

After the selector correction, a boot creation run produced:

- `/.hcprms1`: 34,768 bytes, valid-looking committed generation 1 record;
- `/.hcprms2`: exactly 32,768 bytes, normal HCPR header, generation 0, invalid
  solely because the file was short;
- `/bootlog.bin`: exactly eight bytes, `ASENSURE`;
- no `/asavetrc.bin`, because boot did not reach normal runtime trace writing.

The token is direct evidence that the pre-audio ten-second watchdog fired
while the AutoSave ensure operation was active. It is not evidence that the
deadline did *not* fire, and it does not identify the blocked subphase.
Comparison of the common file prefix found the expected generation/stored-CRC
header differences but otherwise matching formatted content. The application
stream had therefore reached exactly 32 KiB, before the final 2,000 bytes,
close/sync, and full-size publication. That endpoint is compatible with a
cluster-extension/cache/SD transport stop but card geometry was not captured;
it is an inference, not a diagnosis. Bounded CRC prevents CPU monopolization;
it must never be represented as a proven FAT allocation fix.

### Logging-only diagnostic added

At user direction, the session added a minimal capture mechanism, not a
lower-layer repair:

- `config.h` declares logging-only schema/version, eight records, 8 bytes per
  record, and a 64-byte total.
- `filesystem.c` owns a 64-byte normal-SRAM1
  `fs_hcprms_boot_capsule`, compiled only with `DEV_MODE_LOGGING`. It starts
  empty for ensure phase 0, records the target at scan phase 4, and replaces
  state in place as each write advances—no trace ring and no diagnostic file
  I/O during the failed operation.
- On deadline expiry while the active op is `ASENSURE`, it freezes the capsule
  before the existing abort/remount path destroys live state. Ordinary boot
  failures still write only their eight-byte printable token. A frozen
  `ASENSURE` writes that token followed by the 64 raw bytes: exactly 72 bytes
  if the best-effort recovery write itself succeeds.
- Stages `E0` through `E7` are fixed eight-byte, little-endian records:
  context/flags; application byte progress and chunk; last `fwrite` result;
  cursor/logical size; free-cluster search geometry/flags; append cluster
  ownership; cache counters; and SD driver/operation/offset/retry/callback
  state. `DEV_MODES.md` is the binary-layout authority.
- `afatfs_getDiagnosticSnapshot()` and `sdcard_getTransportSnapshot()` copy
  the relevant live state only. They allocate nothing, poll nothing, issue no
  I/O, mutate no callback/retry/ownership field, and must be used only by the
  boot freeze path. The header/source comments document this non-interference
  contract adjacent to the APIs.
- `filesystem.h`, `DEV_MODES.md`, `AUTOSAVE.md`, `SRAM_MANIFEST.md`, and
  `MODULE_INTERCHANGE_SPEC.md` were reconciled. The normal build reserves
  64 bytes, with an observed 80-byte BSS delta due to alignment/layout.
- `tools/decode_bootlog.py` is a read-only host decoder for either ordinary
  eight-byte boot tokens or the 72-byte `ASENSURE` E0..E7 capsule. It is not a
  general AutoSave-record decoder and does not modify fixtures.

No pacing, retry, allocator algorithm, FAT/cache behavior, fallback logger,
or recursive-delete behavior changed. Logger failure continues not to mask the
original boot failure.

### Follow-up hardware state and disposition

The final copied card after subsequent boots contained both `.hcprms1` and
`.hcprms2` at exactly 34,768 bytes with committed headers. Recorded examples
were A generation 1 / CRC `0x10d2a0bf` and B generation 0 / CRC
`0xd201f751`. There was no `bootlog.bin`, so no timeout/capsule was produced.
`asavetrc.bin` was eight bytes containing `S`, consistent with boot setup
having completed and the normal runtime writer having been scheduled before
shutdown. The user then reported several boots appeared normal.

Leave this in the field as observability. If the stop returns, copy the card
without attempting repair, retain `.hcprms1`, `.hcprms2`, `bootlog.bin`, and
`asavetrc.bin`, run `python3 tools/decode_bootlog.py SD_CARD/bootlog.bin`, and
use the capsule to choose exactly one targeted AsyncFATFS/SD investigation.
Do not make a speculative fix because the last few boots passed.

## 5. Phase-2 remedy status preserved from removable plans

`AUTOSAVE_REMEDY_PA2ST1.md` and `AUTOSAVE_REMEDY_PA2ST2-3.md` were planning
and audit documents. Their live conclusions are now in `AUTOSAVE_PHASE2_PLAN.md`:

- **Step 1 complete:** the logging-only 64-record `D/S/A/V/M/C/P/T` trace is
  present. The original trace flush left the shared facade terminal because it
  used a NULL callback; the private completion self-ack correction is in the
  baseline. Pending records are retained on append/sync error.
- **Step 2 accepted complete for scalar owners:** Scene, Kit/generated,
  Instrument Normal/Morph/supplemental descriptors, and Scene-owned MIDI
  channel/note have one change-aware scalar-owner path. There is no separate
  user-editable Bank scalar control. This does not cover whole-object load,
  Pattern, Effect, copy/paste, or power-cut publication.
- **Step 3 code audit complete; settings retest passed:** current root
  Scene/Bank source/provenance callbacks are guarded on successful completion;
  settings serialization reads live source/global values and revision-protects
  re-dirties. The user accepted the settings writer in Session 047. No new
  settings implementation is pending.
- **Still to test before further Phase-2 behavior:** independent fixtures for
  root Scene Load/Save and partial Bank Load/Save provenance, plus AutoSave
  OFF→ON while idle and during an admitted transaction. Use current trace and
  copied fixtures; do not implement the superseded proposed 16-byte Step-2/3
  diagnostic structure or invent new C/H diagnostics without a demonstrated
  boundary failure.
- **Steps 4–6 remain future:** semantic rule is Bank slot/name are payload;
  a structurally valid initial record can be incomplete. Then implement
  whole-object hooks one public-completion boundary at a time (Instrument,
  Kit, Scene without Pattern, selective Bank, Morph). Load/Save exclusion is
  last, separately testing dirty work already active versus not yet admitted.

## 6. Known defects and intentionally deferred work

1. **Overwrite Save may leave the old folder in place.** This affects Bank,
   root Scene, and Kit. It requires a correct native AsyncFATFS recursive
   delete: captured exact identity, recursive children, VFAT/SFN runs, parent
   return, cluster chains, cache/handle release, and exactly-once callback.
   `afatfs_deleteTree()` must be repaired and tested with populated fixtures.
   Do not reinstate `old*` renames, root promotion, or boot-time deletion.
2. **Runtime Bank Load may switch the playing active Scene.** Preserve the
   pre-load active Scene only after ordinary Bank Load/Save is stable enough to
   isolate the behavior. Boot retains the saved Bank default Scene. A failed
   runtime load must keep the old active Scene.
3. **Human-readable development-log conversion is deferred.** After AutoSave
   is complete and every logging format is settled, add one read-only script
   under `/tools/` that consumes copied `SD_CARD/` logs and writes a dated
   `dev_log_date.txt` in the project root. It must understand the final
   `asavetrc.bin` lifecycle records and `/bootlog.bin` token/capsule contract.
   Do not write it while formats are changing.
4. **Intermittent 32 KiB boot stop remains unlocalized.** The capture build is
   adequate first evidence; no lower-layer code is authorized until a capsule
   or another preserved fixture selects a cause.

## 7. Documentation/specification reconciliation

Updated this session:

- `SCOPING_TARGETS.md`: pins recursive delete—not old-directory cleanup—as the
  overwrite solution; records runtime active-Scene and future log-converter
  deferrals.
- `MEMORY.md`: replaces stale rollback carryover with Session 047 bounded CRC,
  capture, settings, audio-test, and known-bug state.
- `AUTOSAVE_PHASE2_PLAN.md`: absorbs PA2ST1 and PA2ST2-3 completion/remaining
  gates and the 32 KiB capture boundary.
- `SESSION_046-047_PLAN.md`: is now a closeout/deferred-to-048 record.
- `DEV_MODES.md`, `AUTOSAVE.md`, `SRAM_MANIFEST.md`, and
  `MODULE_INTERCHANGE_SPEC.md`: describe the current bootlog capsule,
  read-only snapshot contract, tool, and RAM/build impact.
- `FILESYSTEM_SPEC.md` and `ASYNCFATFS_REFERENCE.md`: no longer overclaim
  reliable recursive overwrite; they document it as the known native-delete
  correctness target.

## Recommended Session 048 order

1. Read this handoff, `MEMORY.md`, `SCOPING_TARGETS.md`, and the current
   storage/AutoSave/dev-mode specifications. Confirm the actual worktree;
   preserve user-owned `SD_CARD/` changes.
2. Do not touch settings unless a new hardware fixture contradicts the accepted
   retest. Do not touch the boot capture path unless it has produced data.
3. If choosing Bank work, select *one* known bug. Recursive delete is a
   low-level AsyncFATFS implementation/test project; active-Scene runtime Bank
   Load is a separate request-time behavior project. Never combine them.
4. If no Bank work begins, finish the listed independent Phase-2 provenance
   and AutoSave OFF→ON hardware fixtures using existing trace/card evidence.
5. Only after logging formats and AutoSave are settled, scope the requested
   human-readable log converter.
