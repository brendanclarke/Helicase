# S070 Systems General Check and Review Plan

Baseline: commit `9627f70` on `dev-ph5-effects` (Session 069 closure,
2026-09-20). Build: text=450,140, data=416, bss=291,756; image 450,572 bytes.
Session 069 Pass 2 hardware-validated 2026-09-22 (PASS).

## Purpose

Bounded systems-level fitness pass before Phase 5 Effects development. This
session works from background infrastructure outward: engineering hygiene first,
then Load/Save revision, then remaining feature behavior, then a testing
closeout that validates everything.

---

## Phase 1 — Engineering hygiene

Source: `S070_GENERAL_FITNESS_AGENDA.md` Gate 4.

### 1.1 Makefile header dependencies

Add `-MMD -MP` and inclusion of generated `.d` files. At present a `config.h`
edit can leave stale objects unless the developer remembers `make clean`.

### 1.2 Developer mode default

Developer mode (`DEV_MODE_LOGGING`) remains the default build configuration for
the foreseeable future. Do not change this default or add trace-ring-size
revert work to this session. Any future additions to the trace must be gated
behind the appropriate developer mode flags (`DEV_MODE_LOGGING`,
`DEV_MODE_PATTERN_TRACE`, etc.) — never unconditionally compiled.

### 1.3 IWDG — leave inactive

`DEV_LOGGING_IWDG` stays present and inactive. **Do not reactivate** without an
explicit discussion about developer watchdog or active trace logging — prior
activation caused massive problems (Session 044 boot-hang regression; Session
054 self-introduced IWDG regression). The define remains in the source for
future reference; it is not a release blocker and is not part of this session's
scope.

### 1.4 Spec hygiene

Update specification/backlog after each phase so historical "unresolved" notes
do not continue to direct work after their implementation has landed. This is
a rolling obligation, not a phase-terminal task.

---

## Phase 2 — Load/Save revision pass

Source: `S070_GENERAL_FITNESS_AGENDA.md` Gate 3. Four user-visible items under
one selection-coordinate architecture:

1. **LSR-01**: checkpoint dirty Kit/Instrument HCNAMES state before browser
   cache/domain handoff, asynchronously and without changing Morph identity.
2. **LSR-02**: detach page repaint/exit from HCNAMES persistence so the old
   Load screen cannot remain visible while storage drains.
3. **LSR-03**: standardize blank as "not ready" and `Empty` as "current index
   proved absent." Disable commit until the displayed coordinate is resolved.
4. **LSR-04**: make every browser publish selection immediately, request name
   and optional preview asynchronously, tag results with a generation, and
   discard stale callbacks.

### Risks and open architecture questions

- **HCNAMES mirror lifetime**: the dedicated HCNAMES mirror (Session 058) is a
  9,000-byte shared buffer. LSR-01 checkpointing would need to snapshot or
  stage dirty rows without blocking the active mirror. Clarify whether the
  checkpoint is a separate buffer or a mark-and-flush of the existing one.
- **Stale callback ordering**: LSR-04's async name requests can return out of
  order during rapid scrolling. The generation-tag approach is correct in
  concept but needs explicit handling for the case where the user has already
  committed a selection before the name arrives — the displayed name must not
  retroactively change after commit.
- **Page-exit vs. persistence race**: LSR-02 detaches repaint from storage but
  the current facade is single-occupancy. If the user exits Load/Save while an
  HCNAMES write is in flight, the facade must either complete the write before
  releasing or defer it to the next idle cycle. Which path? The page-exit
  expedite (Session 056) already handles a similar case for AutoSave; verify
  whether it covers HCNAMES writes too.
- **Bank identity agreement**: after any Load/Save, `settings.cfg` HCNAMES
  row 0, and HCPR must all agree on the active Bank identity. LSR items do not
  change this invariant but any refactoring of the Load/Save completion path
  must preserve it. Add explicit assertions or trace checks.
- **Corrupt/partial Pattern inside Scene loads**: when a Scene file contains an
  invalid or truncated PAT4 region, the current reader must not fail the entire
  Scene Load. Verify this is the case and that the HCNAMES Pattern row is not
  published with `@` provenance for a Pattern that failed validation.
- **Load/Save exclusion during active AutoSave transaction**: an active
  scalar or Pattern AutoSave transaction runs to its safe close boundary when
  Load/Save starts. Verify that HCNAMES publication from the Load/Save side
  does not race with the drain-completion HCNAMES convergence step.

### Testing

Add missing top-level Load/Save request/refusal and latency observations
before behavior changes. Run the non-destructive AutoSave/HCNAMES/settings
matrix, followed by power-cut and failure injection. Particularly important:

- HCNAMES publication across Kit/Instrument browser-family changes
- Active transaction plus Load/Save exclusion and page-exit release
- Settings/HCNAMES/HCPR/PAT4 later-mutation-wins behavior
- Stale callback ownership after rapid reverse scrolling/type changes

---

## Phase 3 — Remaining Phase 4 feature behavior

### 3.1 Probability must gate the complete step

Source: `SCOPING_TARGETS.md` §4.6, `S070_GENERAL_FITNESS_AGENDA.md` §2.2.

`seq_advanceTrackStep()` currently applies probability only around
`seq_triggerVoice()` and queues step automation afterward regardless. Fix:
compute one step-play decision and use it to gate both trigger and automation
publication.

**Risks and open questions:**
- What is the correct behavior for a step that fails probability but has
  automation? Current code queues automation regardless — this is the defect.
  The fix gates both, but document the decision: a probabilistic step that
  doesn't fire should not apply its automation either, because the hold model
  (§4.3a) depends on active steps being the reset boundaries.
- Erase/record semantics when probability is active: does the sequencer's erase
  mode respect probability (only erase steps that actually fired) or does it
  always erase? Document explicitly.
- Interaction with the automation hold model: a step that is probabilistically
  skipped should not reset a held parameter value, since it didn't "fire."
  Verify this falls naturally from the fix.

### 3.2 Complete automation target runtime ownership

Source: `SCOPING_TARGETS.md` §4.4, `S070_GENERAL_FITNESS_AGENDA.md` §2.3.

`seq_drainPendingAutomation()` (sequencer.c:609) currently handles only
`instrumentParam_isVoiceParameter(target)` and silently drops Scene targets.
Fix: add a typed Scene-target dispatcher with the same runtime side effects as
an ordinary Scene parameter edit.

**Risks and open questions:**
- Scene target ID space: eight Scene targets currently follow the 384 voice
  descriptor IDs. Verify the target-to-Scene-parameter mapping is stable and
  documented in `SCOPING_TARGETS.md` §4.4.
- Per-voice Morph interaction: automation of a Morph parameter via step
  automation must go through the same descriptor/runtime path as direct edit.
  Verify `instrumentManager_writeRuntime()` is the correct entry point for both.
- Track-7 choke/base-decay ownership rule: automation of choke-group or
  base-decay Scene parameters from a track that isn't track-7 needs a clear
  ownership decision. Should it be silently dropped, applied, or rejected?
- Retrigger restore (`seq_automation_dirty` / `seq_restoreAutomatedParameters`)
  only covers voice parameters today. Scene-target automation must either
  participate in retrigger restore or explicitly opt out with documented
  rationale.

### 3.3 LED state consolidation

Source: `SCOPING_TARGETS.md` §4.11, `S070_GENERAL_FITNESS_AGENDA.md` §2.4.

Give `ledHandler` one explicit render priority and fallback model:
`base < blink < flash < pulse`, including BAR1 and chase.

**Risks and open questions:**
- The current layers (base/blink/flash/pulse/chase) restore state independently
  and are vulnerable to ordering bugs. The fix is a priority-based re-render on
  layer expiry, but this changes observable behavior for edge cases (e.g., a
  pulsed LED that was blinking falls back to blink, not base). Audit existing
  callers to confirm no code depends on the current fall-to-base behavior.
- Chase LED interaction with the held-step automation overlay (Session 066):
  the overlay writes LEDs directly. Does it interact with the blink/pulse layers?
  The overlay should be positioned in the priority stack explicitly.
- Session 068's second `buttonHandler_processEvents()` drain per main-loop pass
  changed LED timing. Verify the consolidation doesn't introduce visible
  flicker from the faster drain rate.

### 3.4 Deferred to later session

Per-track scale/shuffle, copy/paste, clear, live record, roll overhaul,
Patgen/Euklid revert, and automation hold reconciliation are deferred.
See `knowledge_files/drafts/PATTERN_TRACK_PROPERTIES_AND_WIDGETS_COMPLETION.md`
for the consolidated draft.

---

## Phase 4 — Testing closeout

### 4.1 AutoSave OFF-to-ON re-enable test matrix

Source: `S070_AUTOSAVE_REENABLE.md` items 1-6. S069 resolved the root cause
(Pattern maintenance churn) and Pass 2 confirmed budget enforcement.

1. **Lifecycle trace stages**: add policy OFF/ON, setup admitted/success/failure,
   tracking enabled, full-Bank seed complete, scalar dirty count, and Pattern
   dirty mask summary stages.
2. **AS-ENABLE matrix**: run cases from
   `AUTOSAVE_TEST_CASES_LOAD_SAVE_REVISIONS.md` with known changes in a low
   and high Scene. Hash payloads before OFF, after OFF edits, after ON/setup,
   after each writer generation, and after reboot.
3. **Concurrent activity**: verify ON during playback, recording, Load/Save
   suppression, active scalar transaction, and active Pattern transaction.
4. **Non-semantic separation confirmed**: clean Pattern state produces no file
   generations after convergence (S069 fix validated in Pass 1/Pass 2).
5. **Setup failure retry**: expose or automatically retry
   `fs_autosave_setup_failed` with bounded cadence. Preserve the rule that no
   retry starts while Load/Save owns the facade. Consider visible "AutoSave
   error" UI status.
6. **Active Scene priority**: consider prioritizing the active Scene's Pattern
   after re-enable while retaining rotating cursor for fairness.

### 4.2 Pattern persistence boundary validation

Source: `S070_GENERAL_FITNESS_AGENDA.md` Gate 1.

- Root Scene Save/Load round trips
- Partial Bank Save/Load without modifying unselected resident Patterns
- Boot restore from every active Scene
- Active, shown, pending, service-owned, and `menu_playedPattern` alignment
- Corrupt, missing, truncated, wrong-generation, and power-cut PAT4 cases
- Edits during Pattern snapshot/write (proving the later edit remains dirty)
- One-time initialization at every load/retry phase
- Mixed AutoSave/HCNAMES/explicit Scene fixtures with Pattern rows and `@`
  provenance

Build reproducible card fixtures and semantic Pattern comparators. Use PAT4
generations, CRCs, decoded addresses/blocks, HCNAMES rows, and runtime
indices — not FAT timestamps.

### 4.3 Duplicate filename test

Re-check duplicate `bootlog.bin`/`asavetrc.bin` names on hardware now that the
LFN fix is present. Close the item if not reproducible. This is expected to be
a non-issue going forward.

### 4.4 Phase 1-3 regression validation

Hardware validation of all changes made in Phases 1-3 of this session.
Scope depends on what was actually implemented.

---

## Phase resolution status

| Phase | Item | Status | Notes |
|-------|------|--------|-------|
| 1.1 | Makefile header deps | DONE | `-MMD -MP` + `-include *.d` added |
| 1.2 | DEV mode default | DONE | Verified: `DEV_MODE_LOGGING 1` (config.h:88) |
| 1.3 | IWDG inactive | DONE | Verified: `DEV_LOGGING_IWDG 0` (config.h:200) |
| 2 | Load/Save revision | DONE | LSR-01 through LSR-04 implemented and hardware-tested. See `S070_PHASE2_LOAD_SAVE_REVISION.md`, `S070_PHASE2_IMPLEMENTATION.md` |
| 3.1 | Probability gating | DONE | Implemented and hardware-tested at Phase 3 closeout (`e1a3223`) |
| 3.2 | Scene automation targets | DONE | Targets 384–403 implemented. Hardware-verified in `SD_CARD_PHASE3_OUTPUT` |
| 3.3 | LED consolidation | DONE | LED layer bitmap implemented at Phase 3 closeout |
| 3.4 | Track properties deferred | DEFERRED | See drafts/ document |
| 4.1 | AutoSave re-enable | DONE | T1 PASS (2026-09-24). Q1 runtime overlay hardware-verified (2026-09-25) |
| 4.2 | Pattern persistence | DONE | T2(a) confirmed F4, fix applied, retest PASS. T2(b) PASS. T2(c) PASS |
| 4.3 | Duplicate filename test | DEFERRED | Expected non-issue, deferred to D-D sketch |
| 4.4 | Regression validation | DONE | T3 PASS (automation restart). Q1 trace clean. No regressions observed |

## S069 closure status

All Session 069 items are resolved:

- **Pass 2 hardware validation**: PASS (2026-09-22). CPU budget, Load/Save
  repair gate, quiet window, max latency all confirmed on hardware.
- **Budget extraction**: deferred review-only. Budget primitive stays in
  `filesystem.c` until a non-filesystem consumer beyond PatternStackService
  needs it. No action needed this session.
- **Load/Save repair gate**: implemented in S069 Pass 2. `patSvc_tick()` repair
  section suppressed when `menu_activePage == LOAD_PAGE || SAVE_PAGE`.
