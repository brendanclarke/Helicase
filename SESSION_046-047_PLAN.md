# Session 046 Plan — Closed; Remaining Work Carried to Session 047

## Closeout boundary

Session 046 is closed on 2026-08-10 at rollback commit `c9807fa` (`autosave
trace logger working, pre steps 2 and 3 implementation`). The authoritative
verbose record is `knowledge_files/log_archive/046_SESSION_HANDOFF_LOG.md`.

Later Session 046 firmware experiments were reset. Do not infer current source
behavior from a later SD-card fixture or from an untracked plan.

## Completed in Session 046

### 1. Baseline reconciliation

- Session 045 was consolidated into a deletion-safe post-mortem and handoff.
- Current source/build state was checked against the accepted scalar AutoSave
  architecture rather than restored from `*.failed` files.
- Development modes were reduced to the binding distinction:
  `DEV_MODE_DIAGNOSTIC` is screen-only; `DEV_MODE_LOGGING` is file-only.

### 2. Boot filesystem localization and failure transparency

- The timeout remains ten seconds per boot operation. A timeout means real
  non-progress because normal boot, including Bank Load, completes well inside
  that interval.
- Kit quarantine and Bank Load now retain detailed non-rearming substep labels.
- Kit I/O abort is distinct from malformed content and cannot authorize
  quarantine rename.
- Boot index/load request failures are consumed before Menu acknowledgement or
  empty-library fallback can hide them.
- Normal hardware smoke testing passed; the intermittent hang did not recur
  with a more specific detail code.

### 3. HCNAMES singleton correction

- All HCNAMES access uses one case-insensitive singleton match policy.
- Every create-capable HCNAMES path requires a complete root absence proof.
- A NULL read open, duplicate match, or scan/open/close/FAT error remains an
  error and cannot authorize creation.
- Hardware retesting did not reproduce the duplicate HCNAMES entry.

### 4. AutoSave Step-1 observability

- The approved 520-byte `DEV_MODE_LOGGING`-only lifecycle trace is implemented.
- The first background append defect was corrected: the trace writer now
  acknowledges its terminal facade status through its completion callback.
- `D/S/A/V/M/C/P/T` records can distinguish dirty production, scheduling,
  admission, validation, mask merge, capture, publication, and completion.

### 5. Phase 1 scalar acceptance

This work is complete and must not be reopened as a vague matrix:

- Scene values were tested.
- Kit and Instrument values were tested.
- MIDI channel/note are Scene-owned and were covered by Scene testing.
- There are no separate user-editable Bank scalar values in the current UI.

The acceptance does not claim whole-object Load/Save/copy publication, Pattern
or Effect persistence, or power-cut coverage.

### 6. Semantics settled

- Bank slot/name and other Bank metadata are payload, not record-selection
  identity.
- A current-format record can be structurally valid but incomplete as a
  resident-Bank snapshot; initial records contain zero parameter payload.
- Partial Bank operations do not imply replacement of all sixteen Scenes.

This settles the design rule, not the current validator implementation:
`c9807fa` still performs a live-Bank match during winner selection. Reconcile
that boundary before whole-object Bank session publication.

## Reverted Session 046 work

The following is explicitly not in `c9807fa`:

- generic one-millisecond filesystem pacing;
- byte-bounded CRC generation;
- runtime Bank Load active-Scene preservation;
- later settings-notification changes;
- unified `/devlog.bin` output or duplicate-safe log publication.

The one-millisecond pacing experiment caused severe boot and Load/Save
slowdowns and delayed rather than removed audio glitches. The unified-log
experiment caused a boot timeout, no durable `devlog.bin`, and a partial
32,768-byte `.hcprms2`. Neither may be restored mechanically.

## Session 047 plan

Use `SETTINGS_BANK_LOAD_REIMPLEMENT.md` as the immediate implementation plan
and `AUTOSAVE_PHASE2_PLAN.md` only for later whole-object sequencing.

### 1. Confirm the rollback baseline

Build unchanged `c9807fa`, record image/RAM, confirm the whole-record CRC call
still exists, confirm the failed pacing layer is absent, and preserve the
user-owned `SD_CARD/` fixtures.

### 2. Establish read-only fixture inspection

Use or add a host-only reader for exact record size, header, CRC32C,
generation/commit selection, dirty-mask population, relevant payload offsets,
and existing eight-byte trace records. It must never modify a fixture.

### 3. Byte-bound every AutoSave CRC path

One explicit per-tick byte budget must cover:

1. initial A/B creation;
2. recovery when neither candidate is valid;
3. validation of existing candidates; and
4. transformed-copy CRC generation.

Use retained operation cursors, no record-sized buffer, no blind delay, and no
new permanent SRAM. Test this change alone with valid-pair drain, missing-pair
creation, invalid-pair recovery, playback, exact 34,768-byte files, and CRCs.

### 4. Preserve active Scene during runtime Bank Load

After CRC testing passes, add one explicit request-time contract:

- boot Load restores the Bank's saved default Scene;
- user/runtime Load preserves the previously active Scene, including when the
  selected child mask excludes it;
- failed Load keeps the old active Scene and reports the failure.

Do not add pacing or new Bank timing diagnostics in this pass.

### 5. Retest settings before editing

Change AutoSave and one unrelated persistent setting, wait for the existing
writer, inspect `settings.cfg`, reboot, and verify both values. If it passes,
make no settings source change. If it fails, patch only the demonstrated dirty-
notification or filesystem boundary.

### 6. Integration and audio sign-off

Test Scene provenance, Bank Load preservation, Bank Save stopped/playing,
AutoSave OFF→ON, rebooted settings, and one scalar writer smoke edit. Confirm
no delayed AutoSave glitches and no hidden-record failure is acknowledged as
success.

### 7. Conditional 32 KiB boot-lock diagnosis

If a hidden record again stops at 32,768 bytes or boot times out, stop and
preserve the card. Capture the application write phase, exact byte progress,
actual cluster geometry, AsyncFATFS allocator/cache state, SD transport state,
and logger recovery result before any FAT/SD fix. CRC chunking is not proof of
a cluster-extension repair.

### 8. Resume Phase 2 only after the baseline passes

Add whole-object hooks one successful public completion boundary at a time:
Whole Instrument, Whole Kit, root Scene without Pattern, selective Bank
session replacement, then Morph projection. Reattempt Load/Save exclusion
last, with separate tests for already-active versus not-yet-admitted AutoSave
work.

## Session 047 completion condition

The immediate reimplementation is complete only when every AutoSave CRC path
is byte-bounded, runtime Bank Load preserves active Scene without changing boot
semantics, settings persistence is verified, stopped/playing Bank Save does not
produce deferred audio glitches, and every failed write remains visible and
retryable. Whole-object Phase 2 work is a later gate, not part of that claim.
