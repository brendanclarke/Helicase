# Session 046 Plan — Autosave Phase 2 Remediation

## Purpose

Session 046 begins from the accepted Session 045 production baseline at commit `326a8a1`. It does not restore or mechanically compare failed-branch source. The working references are:

- [`AUTOSAVE_PHASE2_PLAN.md`](AUTOSAVE_PHASE2_PLAN.md) — parent sequence and failure analysis;
- [`AUTOSAVE_REMEDY_PA2ST1.md`](AUTOSAVE_REMEDY_PA2ST1.md) — diagnostic observability;
- [`AUTOSAVE_REMEDY_PA2ST2-3.md`](AUTOSAVE_REMEDY_PA2ST2-3.md) — Phase 1 and settings/provenance verification.

The governing rule is one change boundary at a time, with a saved hardware checkpoint before the next boundary is attempted.

## Implementation overview

### 1. Reconcile the baseline before code changes

The existing firmware build and playback/Load/Save smoke tests have already been run successfully, and the `SD_CARD/` working tree now reflects the post-test card state. Treat that as useful baseline evidence, while still recording the exact source/image and card metadata required below; it does not by itself satisfy the diagnostic-trace or full matrix checkpoints.

Confirm the source/build boundary, update stale project context, verify failed files are excluded from the build, and reconcile the retained plans with the 045 handoff. Capture clean A/B records with generation, CRC, commit, identity, and dirty-bit counts.

### 2. Obtain RAM sign-off and implement observability

The RAM condition is approved: these allocations are permitted only when
`DEV_MODE_LOGGING == 1`. They must compile out, and their logging/trace paths
must be inactive, when `DEV_MODE_LOGGING == 0`.

The mode distinction is simple: `DEV_MODE_DIAGNOSTIC` is for diagnostics that print to the screen; `DEV_MODE_LOGGING` is for diagnostics that do not print to the screen, including file logging. The autosave trace and its diagnostic snapshot therefore use `DEV_MODE_LOGGING`. Do not introduce a separate autosave trace mode.

Implement `AUTOSAVE_REMEDY_PA2ST1.md` first and test it to completion. Do not
implement the diagnostic additions in `AUTOSAVE_REMEDY_PA2ST2-3.md` at the
same time. After Step 1 has been tested, reassess Steps 2–3 and decide which,
if any, Step 1 interfaces or trace data can be reused without duplicating or
expanding the diagnostic surface.

Then add the smallest disabled-by-default lifecycle trace needed to distinguish:

`DIRTY → SCHEDULED → ADMITTED → VALIDATED → MASK_MERGED → CAPTURED → PUBLISHED → TERMINAL`.

The trace must be bounded, non-blocking, separate from screen diagnostics, and must not add incidental filesystem traffic during the autosave operation. Prove the trace itself with a normal Phase 1 scalar edit before using it to diagnose new features.

### 3. Close the accepted Phase 1 matrix

This is a verification and evidence task, not a Phase 2 feature decision. Its
purpose is to establish that the accepted Phase 1 scalar behavior is a stable
reference before whole-object hooks are added.

Prerequisite: Step 1's diagnostic implementation has passed its own test, and
the starting A/B records are valid, fully drained, and copied aside. If Step 1
does not produce trustworthy lifecycle evidence, stop and repair Step 1 before
using it for this matrix.

Test one coordinate at a time across:

- Bank fields;
- Scene parameters;
- Kit generated endpoints;
- Instrument Normal endpoints;
- Instrument Morph endpoints for Morphable descriptors;
- supplemental descriptor values; and
- MIDI channel/note fields.

For each coordinate, perform and record:

1. one value-changing edit;
2. an identical-value write, which must not dirty the mask;
3. repeated edits within one debounce window, which should coalesce to the
   final value; and
4. where applicable, an edit that re-dirties the same byte during an active
   drain, which must survive for a later generation.

Also perform one clean-mask idle observation after recovery is complete. It
must show no new autosave transaction or hidden-file I/O.

For every run, preserve the starting and ending `.hcprms` records and record
the expected payload offset(s), starting/ending generation, CRC, commit marker,
dirty-bit count, final payload value, and the Step 1 lifecycle events. The
matrix is passed only when the expected owner marks the expected bit, the
writer captures the final live value, identical writes produce no dirty work,
re-dirty is not lost, and idle state remains idle.

Decisions required before moving on are limited to test interpretation:

- If a value-changing test produces no dirty bit, determine whether the owner
  setter was not reached or the writer was not admitted; do not add a Phase 2
  hook to compensate.
- If the bit is marked but the payload is wrong, isolate capture/publication
  before changing ownership.
- If an identical write dirties, treat that as a Phase 1 regression to fix or
  explicitly explain before proceeding.
- If re-dirty is lost, stop Phase 2 work until atomic take/re-dirty behavior is
  repaired and retested.
- If clean idle starts I/O, stop and diagnose scheduler/recovery state before
  continuing.

The resulting fixtures and trace become the Phase 1 reference baseline for
Steps 4–7; no new whole-object behavior is authorized by this matrix alone.

### 4. Close settings and provenance gaps

Independently test root Scene Load/Save, partial Bank Load/Save, post-load `settings.cfg` rewriting, and the complete AutoSave OFF→ON lifecycle. Treat failures as isolated diagnosis tasks with one candidate owner change per pass. Separately perform the clean packaged-image size reconciliation.

### 5. Resolve semantic contracts before whole-object hooks

Document and obtain agreement on two rules: Bank name/slot and other metadata are payload, not record identity; and a clear definition of “valid” versus “complete” autosave records. Do not implement Phase 2 whole-object publication until these rules are settled.

### 6. Reintroduce Phase 2 hooks incrementally

Add and test successful-public-completion dirty marking in this order:

1. Whole Instrument load.
2. Whole Kit load.
3. Whole Scene load without Pattern, including a regression check that Menu preview/selection alone does not dirty autosave.
4. Partial Bank Load/Save using the actual selected/loaded child mask; Save must remain a resident-data read, not a whole-workspace replacement.
5. Morph projection, limited to the descriptors that actually support Morph.

Each item gets its own trace and fixture pair. Do not move publication points repeatedly without evidence, and do not change active-Scene or other cross-cutting ownership in the same pass.

### 7. Reattempt Load/Save exclusion last

First trace current behavior while Load/Save is open. Then, in separate passes, prevent only new autosave starts during Load/Save and verify that pre-existing dirty bits still drain afterward; only after that passes should physical Load/Save entry deferral be considered. Distinguish an operation already active at page entry from one that has not yet been admitted.

### 8. Close out with evidence

Record source commit, rebuilt image identity, A/B metadata, expected offsets, operation classification, Load/Save state, settings revision, runtime duration, returned fixtures, and trace results. Update `MEMORY.md` and write a new handoff only after the implementation outcome is genuinely established.

## Explicit non-goals

Session 046 does not restore `*.failed` source, redesign the autosave wire format, add Pattern/Effect persistence, infer success from Menu state or build size alone, or combine observability, ownership, scheduler exclusion, and publication changes into one hardware test.

## Completion condition

The session is complete only when each attempted boundary has an independently interpretable hardware result, the accepted Phase 1 baseline remains intact, and any Phase 2 behavior claimed as implemented is backed by matching trace and fixture evidence.
