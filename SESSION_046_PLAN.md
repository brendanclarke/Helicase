# Session 046 Plan — Autosave Phase 2 Remediation

## Purpose

Session 046 begins from the accepted Session 045 production baseline at commit `326a8a1`. It does not restore or mechanically compare failed-branch source. The working references are:

- [`AUTOSAVE_PHASE2_PLAN.md`](AUTOSAVE_PHASE2_PLAN.md) — parent sequence and failure analysis;
- [`AUTOSAVE_REMEDY_PA2ST1.md`](AUTOSAVE_REMEDY_PA2ST1.md) — diagnostic observability;
- [`AUTOSAVE_REMEDY_PA2ST2-3.md`](AUTOSAVE_REMEDY_PA2ST2-3.md) — Phase 1 and settings/provenance verification.

The governing rule is one change boundary at a time, with a saved hardware checkpoint before the next boundary is attempted.

## Implementation overview

### 1. Reconcile the baseline before code changes

Confirm the source/build boundary, update stale project context, verify failed files are excluded from the build, and reconcile the retained plans with the 045 handoff. Capture clean A/B records with generation, CRC, commit, identity, and dirty-bit counts.

### 2. Obtain RAM sign-off and implement observability

Before adding diagnostic storage, obtain acknowledgement for the allocations specified by the detailed remedy plans: the optional 518-byte trace ring in `AUTOSAVE_REMEDY_PA2ST1.md` and the 16-byte diagnostic snapshot proposed for Steps 2–3 in `AUTOSAVE_REMEDY_PA2ST2-3.md`. Resolve whether these should be implemented together so duplicate diagnostic surfaces are not created.

Then add the smallest disabled-by-default lifecycle trace needed to distinguish:

`DIRTY → SCHEDULED → ADMITTED → VALIDATED → MASK_MERGED → CAPTURED → PUBLISHED → TERMINAL`.

The trace must be bounded, non-blocking, separate from screen diagnostics, and must not add incidental filesystem traffic during the autosave operation. Prove the trace itself with a normal Phase 1 scalar edit before using it to diagnose new features.

### 3. Close the accepted Phase 1 matrix

Starting from known clean valid records, test one coordinate at a time across Bank, Scene, Kit, Instrument Normal, Instrument Morph, supplemental descriptor values, MIDI channel/note, and generated Kit endpoints. Include identical-value no-ops, coalesced edits, re-dirty during an active drain, and clean-mask idle behavior. Save exact file and trace evidence for every result.

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
