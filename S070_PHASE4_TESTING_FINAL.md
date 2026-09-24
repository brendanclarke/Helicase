# S070 Phase 4 — Testing Closeout

Session: S070 · Branch: `dev-ph5-effects` · Baseline: `e1a3223` (Phase 3
closeout, 2026-09-23).

Test image: `build/LXRV2_lxr02.img`, 455,980 bytes. Byte-identical to
`SD_CARD_PHASE3_OUTPUT/LXRV2_lxr02.img`, so the Phase 3 hardware session
already ran this exact firmware. No code changes in Phase 4.

Source: `S070_SYSTEMS_GENERAL_CHECK_AND_REVIEW_PLAN.md` §4.1–§4.4.

---

## 1. What Session 070 has already proven on hardware

| Change | Evidence | Result |
|---|---|---|
| 1.1 Makefile `-MMD -MP` | Build artifact | DONE |
| 1.2 DEV mode default, 1.3 IWDG inactive | Source review | DONE |
| R3 corrupt PAT4 inside Bank | `SD_CARD_FULLBAD_TEST` | PASS (child 03 failed, 15 loaded) |
| LSR-01 HCNAMES checkpoint | `SD_CARD_LSR01-FINAL` | PASS (Kit Load + VOICE + reboot) |
| LSR-02/03/04 browser/blank/generation | Used during LSR01 sessions | Clean traces, no recorded per-scenario result |
| 3.1 probability gate + remediation | `SD_CARD_PHASE3_OUTPUT`, commit `e1a3223` ("tested") | Ran on hardware at Phase 3 closeout |
| 3.2 Scene targets 384–403 | `SD_CARD_PHASE3_OUTPUT` `.pat05a`/`.pat05b` | Storage PASS (CRC valid, 38 Scene-target entries). Runtime confirmed: trace shows five Scene 5 params re-dirty once per bar |
| 3.3 LED layer bitmap | `SD_CARD_PHASE3_OUTPUT`, commit `e1a3223` ("tested") | Ran on hardware at Phase 3 closeout |
| R1/R2 underlines + 8 Hz refresh | `SD_CARD_PHASE3_OUTPUT`, commit `e1a3223` ("tested") | Ran on hardware at Phase 3 closeout |
| S069 Pass 2 CPU budget | 2026-09-22 | PASS |

Phase 3 features were hardware-tested at Phase 3 closeout. They are not
retested here.

---

## 2. Findings from code and evidence review

**F1 — Decoder payload offsets were wrong (FIXED).** `tools/decode_devlogs.py`
had `SCENE_PARAMS_OFF = 8`, `KIT_PARAMS_OFF = 8`, `INST_NORMAL_OFF = 11`,
`INST_MORPH_OFF = 83`. Session 060 Phase C added two source bytes after each
name field, shifting Scene/Kit parameters to offset 10 and Instrument
normal/morph to 13/85. Every decoded `D` record in those regions was off by
two positions.

**Fix applied**: `SCENE_PARAMS_OFF` → 10, `KIT_PARAMS_OFF` → 10,
`INST_NORMAL_OFF` → 13, `INST_MORPH_OFF` → 85. Added source-byte region
labels and named Scene parameters (`1vm`..`6vm`, `srt`, `1ou`..`6ou`,
`1fx`..`6fx`, etc.) and Kit parameters (`7dc`, `7dc_mrp`). Both
`decode_devlogs.py` and `devlog_unpack.py` (which imports the tables) are
corrected.

**Verified**: decoding `SD_CARD_PHASE3_OUTPUT/asavetrc.bin` now identifies the
five re-dirtied Scene 5 parameters as `1vm`, `6vm`, `1ou`, `6ou`, `6fx` —
matching the Pattern's Scene-target automation entries exactly.

**F2 — Scene-target automation keeps the scalar writer busy during playback.**
`seq_applySceneAutomation()` calls the ordinary Scene setters, which mark
scalar AutoSave dirty on every change. During playback with Scene-target
automation, the scalar writer never goes idle. The retained Scene value is
whatever the automation last wrote. This is a design question (Q1), not a
test failure.

**F3 — `SD_CARD_PHASE3_OUTPUT` contains `.hcnamtmp` with no `.hcnames`.**
Power was removed between "remove old" and "rename temp". The boot recovery
prelude for this state has not been exercised with a recorded result.

**F4 — Predicted stale Pattern restore after explicit load.** Every explicit
Pattern/Scene/Bank load resets the Scene's hidden Pattern generation to zero.
The next drain writes generation 1 to the `b` file. Any older `a` file with
generation ≥ 2 stays valid. Boot takes the higher valid generation when the row
is `@`, restoring the pre-load Pattern. The window lasts until a second drain
writes the `a` file. This is code reading, not an observed failure.

**F5 — Automation missed on transport restart (observed, ~50% of restarts).**
`seq_clearAutomationDirty()` (`sequencer.c:186`) zeros the per-slot dirty
bitmaps at transport stop without restoring voice runtime parameters to their
morph_interpolation base values. On restart, `seq_restoreAutomatedParameters()`
finds a zero bitmap and skips the restore, leaving parameters at whatever the
previous run's automation last wrote. Step 0's automation then applies only its
own targets; parameters automated by later steps in the previous bar stay at
their stale values. The automation gesture (sweep from base to target) is absent
because the starting point is already at or near a previous automation value.
See `S070_PHASE4_AUTOMATION_MISSED.md` for full analysis and proposed fix.

**F6 — `seq_setRunning(1)` sets `seq_running = 1` before state initialization
completes.** TIM3 can preempt between `seq_running = 1` (`sequencer.c:1081`)
and `seq_setStepIndexToStart()` (`sequencer.c:1115`), firing the initial tick
prematurely and causing a double step-0 trigger. The race window is ~3 µs
(MIDI sends are non-blocking FIFO pushes), giving ~0.06% probability per
restart at 120 BPM. Secondary to F5 but should be fixed at the same time.
See `S070_PHASE4_AUTOMATION_MISSED.md` §3–§4B.

---

## 3. Test set

Three device tests plus a confirmation test. Chosen by risk and coverage gap.

| ID | What it proves | Bench time |
|---|---|---|
| T1 | AutoSave OFF→ON captures complete state and goes quiet; page-exit expedite works; Bank identity agrees across files | ~45 min |
| T2 | Stale Pattern generation after explicit load (predicted FAIL); edit-during-snapshot persistence; corrupt/truncated candidate rejection at boot | ~45 min |
| T3 | Automation restore on transport restart works after F5/F6 fix | ~10 min |

T1 and T2 block closeout. T2(a) is a predicted FAIL that opens Q2. T3 runs
after the F5/F6 code fix.

### What is not tested and why

- **Phase 3 features (3.1, 3.2, 3.3, R1/R2)**: hardware-tested at Phase 3
  closeout (commit `e1a3223`). Card evidence in `SD_CARD_PHASE3_OUTPUT`.
- **HCNAMES temp promotion (F3)**: the boot recovery prelude did not change in
  S070. The existing `SD_CARD_PHASE3_OUTPUT` is a ready fixture. Defer to a
  session that changes the boot HCNAMES path.
- **Phase 2 browser edge cases**: LSR-01 through LSR-04 ran on hardware with
  clean traces during the LSR01 sessions. No S070 code changed after those
  sessions. Defer rapid-scroll and power-cut browser edge cases.
- **Partial Bank Load/Save**: no S070 change touches either path. Defer.
- **AutoSave setup-failure retry/UI, active-Scene Pattern priority**: these
  are code changes, not tests. Defer decision (Q3).
- **Power-cut injection at exact write phases**: requires precise timing
  infrastructure or luck. Defer.
- **Duplicate trace-file directory entries**: expected non-issue after S056
  LFN fix. Defer.

### Deferred test sketches

**D-A — Partial Bank Load/Save.** Generate two 16-child Banks whose Patterns
encode Bank and child identity (track 1 step NN+1, track 2 step 1 vs 16).
Load with a mask, Save with a mask, compare trees. Run when Bank Load/Save
code changes.

**D-B — HCNAMES temp promotion.** `SD_CARD_PHASE3_OUTPUT` is a genuine
orphan-temp card. Sketch: orphan temp (expect promotion); temp beside live
(temp wins); truncated temp beside live (temp discarded); orphan temp with
`autosave=0`. Run when the boot HCNAMES path changes.

**D-C — Phase 2 browser supersession and power loss.** Fast Kit scroll, OK
after type switch, Kit/Instrument Load → power cut at 0.5/2/10 s. Run when
Load/Save browser code changes.

**D-D — Duplicate trace-file directory entries.** Prepare card with ≥ 96
deleted entries before `asavetrc.bin`/`pattrace.bin`, run five boot/flush
cycles, audit raw root directory. Run if a duplicate is ever observed.

---

## 4. Prerequisites

### 4.1 Card audit tool

A Python tool (`tools/card_audit.py`) that produces one report per copied
card directory:

- `settings.cfg` summary.
- HCNAMES rows with `R` flag.
- HCPR A/B validity and winner (generation, CRC).
- Per-Scene Pattern pair: validity, generation, winner.
- Trace summary: `K` and `D` burst landmarks, `W`/`A`/`P`/`G`/`Q` records,
  `E`/`X` error records, and `H`-derived budget reports with elapsed time.
- `--baseline` mode: diff HCPR winner payload bytes and Pattern semantics
  against an earlier copy.

### 4.2 Card handling rules

- Flash `build/LXRV2_lxr02.img` from HEAD `e1a3223` for every test. If a fix
  lands during Phase 4, rebuild, record new sizes, and rerun.
- Mount the card read-only on the Mac for copying
  (`diskutil mount readOnly /dev/diskNsM`) with indexing off
  (`mdutil -i off`). Never mount read-write except for fixture injection.
- After the last action in a test, wait at least 10 s before power-off so the
  trace flushes.
- Never use FAT timestamps. Use generations, CRCs, decoded content, and trace
  order.

---

## 5. Tests

### T1 — AutoSave OFF→ON captures complete state

**Propositions.**

1. While AutoSave is OFF, edits produce no hidden-file writes.
2. Turning it back ON captures every scalar and Pattern change made while OFF.
3. Once converged, the system writes nothing further while idle.
4. Leaving a Load/Save page releases a suppressed scalar writer within ~250 ms
   (Session 056 page-exit expedite — never hardware-verified).
5. `settings.cfg`, HCNAMES row 0, and the HCPR winner agree on the active Bank
   identity (R2 invariant).

**Fixture: `SD_CARD_P4_T1`.**

Canonical `SD_CARD/` with:
- `settings.cfg`: `active_bank=1` (`001 Full`, 16 Scenes), `autosave=1`.
- All hidden files removed (`.hcprms*`, `.pat*`, `.hcnames`, `.hcnamtmp`).
- All trace files removed (`asavetrc.bin`, `pattrace.bin`).

`001 Full` has no Scene-target automation, so F2 cannot keep the writer busy.

**Workflow.**

*Part A — baseline*

1. Flash test image, insert card, boot. Leave transport stopped for 3 min.
   Power off. Copy → `P4_T1_A0`.

*Part B — edits while OFF, capture on ON*

2. Reinsert, boot. Start transport on Scene 0.
3. Global page: `AutoSave` → off.
4. Make four edits, noting exact values on a log sheet:
   - E1: Scene 0, change one VOICE-page voice 1 parameter by a large step.
   - E2: Scene 0, STEP mode, toggle track 1 step 16.
   - E3: Switch to Scene 15 in PERF, change one VOICE-page voice 3 parameter.
   - E4: Scene 15, toggle track 2 step 16.
   Then switch back to Scene 0.
5. Wait 30 s.
6. Global page: `AutoSave` → on. Leave transport running for 60 s, stop, then
   wait 5 min without touching anything.
7. Power off. Copy → `P4_T1_B1`.
8. Reinsert, boot. Confirm E1–E4 on device (VOICE values, step LEDs). Power off.

*Part C — Load/Save suppression and page-exit expedite*

9. Boot, transport stopped. Change one VOICE parameter in Scene 0 (E5).
   Within 1 s, enter Load:[Kit]. Stay there 20 s without touching anything.
   Exit to VOICE page. Wait 30 s. Power off. Copy → `P4_T1_C1`.

**Pass criteria.**

- `card_audit.py --baseline P4_T1_A0 P4_T1_B1`:
  - HCPR winner generation advanced; both records valid.
  - Winner payload differs from A0 only at E1 and E3 bytes.
- Every present Scene has a newer Pattern winner. Decoded content equals A0
  except Scene 0 track 1 step 16 and Scene 15 track 2 step 16.
- B1 trace: no `D`, `A`…`T`, or Pattern-class `H` charge between the OFF `K`
  records and the ON `D` burst.
- After the ON burst: `A/V/M/C/P/T` cycles, Pattern work, then a quiet tail
  with at least 36 `H` groups (≥ 3 min) of zero scalar and Pattern charge.
- B1: `settings.cfg` `active_bank`, HCNAMES row 0 source, and HCPR winner
  Bank slot all equal 1.
- Step 8: all four edits present after reboot.
- C1 trace: E5 `D` at t0, at least one `W` during the Load-page dwell, and
  the first `A` at t1 with t1 − t0 in [dwell, dwell + 1.5 s].
- No `E` or `X` records in any capture.

**Failure signatures.**

- Any HCPR or Pattern generation while OFF.
- An edit missing after ON or after reboot.
- Generations still advancing in the quiet tail.
- t1 − t0 ≈ dwell + 5 s (expedite not working).

---

### T2 — Pattern persistence: stale generation, edit-during-snapshot, corrupt candidates

Three parts. Part (a) is a predicted FAIL (F4). Parts (b) and (c) are
independent of (a).

#### T2(a) — Stale Pattern generation after explicit load

**Proposition**: after an explicit Pattern load into a Scene, the next boot
restores the loaded Pattern, not an older hidden AutoSave Pattern.

**Predicted result: FAIL** (F4). If it fails, the fix is to continue from
the in-RAM generation instead of resetting to zero at load commit (Q2).

**Fixture**: reuse the T1 card after Part B. The card already has `@` Pattern
rows and hidden A/B files from the ON convergence.

**Workflow.**

1. Boot, select Scene 5. Load:[Pattern] any library Pattern into Scene 5.
   Exit. Wait 15 s.
2. Power off. Copy → `P4_T2_A1`.
3. Reinsert, boot. Check Scene 5's Pattern — track 1 step LEDs.

**Pass criteria.**

- Scene 5 shows the loaded Pattern (library Pattern's track 1 layout).
- **If Scene 5 shows the pre-load Pattern (15 steps or different layout):
  FAIL.** This confirms F4 — the older `.patNNa` with a higher generation
  won over the loaded Pattern's generation-1 `.patNNb`.

#### T2(b) — Edit during Pattern snapshot

**Proposition**: an edit made while a Pattern snapshot is being written is
not lost: the final hidden winner equals the final edited state.

**Workflow.**

4. Continue on the same card. Select Scene 5 and run the transport. Go to
   VOICE page, voice 1, pick a 0..127 parameter cell. Hold track 1 step 1
   (held-step overlay). Turn that pot back and forth continuously for 15 s,
   never pausing for more than 250 ms. Finish fully clockwise (127), release.
   Wait 15 s. Stop. Power off. Copy → `P4_T2_B1`.

**Pass criteria.**

- Scene 5 winner has exactly one automation entry for that target on
  track 1 step 1, with value 127.
- Winner generation advanced by at least 3 over T2(a). The 5 s max-latency
  path must have admitted drains while editing continued.
- `H` groups during the edit window show Pattern-class charge. No `E`/`X`.

#### T2(c) — Ineligible and corrupt candidates

**Proposition**: at boot, a truncated or CRC-bad newer candidate loses to
its valid peer. Hidden files are ignored unless the Pattern row is `@`.

**Workflow.**

5. Mount the card read-write (indexing off). Generate or manually create
   the overlay below. Eject.

   | Scene | `.patNNa` | `.patNNb` | Pattern row | Expected |
   |---|---|---|---|---|
   | 2 | valid, gen 10 | gen 11, truncated to 5,000 B | `@` | A wins |
   | 3 | valid, gen 10 | gen 11, one pool byte flipped (CRC bad) | `@` | A wins |
   | 4 | absent | absent | `@` | Boot completes; record what is resident |
   | 6 | valid, gen 10 | valid, gen 11 | changed to `-` | Bank child Pattern, not either hidden file |

6. Boot. In PERF, check Scenes 2, 3, 4, and 6: track 3 LEDs, Kit sounds.
   Power off. Copy → `P4_T2_C1`.

**Pass criteria.**

- Every "Expected" cell holds.
- Pattern rejection never changes a Scene's Kit or settings.
- No `E`/`X`. The `Q` records are consistent with the table.

---

### T3 — Automation restore on transport restart

**Proposition**: after the F5/F6 fix (`S070_PHASE4_AUTOMATION_MISSED.md`
§4A–§4B), automation at the beginning of a pattern fires correctly on every
transport stop/restart.

**Prerequisite**: apply fixes 4A (restore-before-clear in
`seq_setStepIndexToStart`) and 4B (defer `seq_running = 1` until after state
init). Rebuild and flash.

**Workflow.**

1. Load any Scene with automation at step 0 and at least one later step
   (e.g. Scene 6 from the user's working set).
2. Play for several bars. Stop playback during a bar where later-step
   automation has been applied (i.e. not at step 0).
3. Restart. Listen for step 0's automation gesture.
4. Repeat 10 times.

**Pass criteria.**

- Step 0 automation is audible and correct on all 10 restarts.
- No audible double trigger at the start.

---

## 6. Execution order

1. **Prerequisites**: card audit tool, T2(c) overlay script.
2. **T1**: requires clean fixture card.
3. **T2**: (a) reuses T1's card state after Part B; (b) continues on same
   card; (c) uses host overlay on the card from (b).
4. **F5/F6 code fix**: apply fixes from `S070_PHASE4_AUTOMATION_MISSED.md`,
   rebuild, flash.
5. **T3**: any Scene with step-0 automation.

---

## 7. Decisions needed

**Q1 — Scene-target automation and retained Scene settings (F2).**

Playing Scene-target automation overwrites retained Scene values, keeps the
scalar writer busy, and makes Scene Save capture whatever automation last wrote.
Options: (a) accept and document; (b) apply as runtime overlay like LFO;
(c) suppress AutoSave marking for automation writes. Recommendation: (b).

**Q2 — Stale Pattern generation after explicit load (F4, if T2 fails).**

Keep each Scene's Pattern generation increasing instead of resetting to zero
at load commit. Continue from the in-RAM generation at load; in the boot
reader's non-`@` branch, seed from the winner's generation. No extra SD work.

**Q3 — AutoSave setup-failure policy.** Add bounded retry (every 30 s while
mounted, never while Load/Save owns the facade), one trace record per failure,
and a Global-page indication. Implement after T1.

**Q4 — Automation restore at transport boundaries (F5/F6).** Proposed fix
in `S070_PHASE4_AUTOMATION_MISSED.md`: (4A) restore all dirty voice parameters
from `morph_interpolation[]` before clearing the bitmap in
`seq_setStepIndexToStart()`; (4B) defer `seq_running = 1` until state init is
complete. Recommendation: apply both. Verified by T3.

---

## 8. Spec hygiene (rolling item 1.4)

These were found during this review and should be resolved regardless of test
outcomes:

- `tools/decode_devlogs.py` payload offsets (F1) — **FIXED** this session.
- `DEV_MODES.md` names the PatternTrace file `/pattrc.bin`; firmware uses
  `pattrace.bin` (`PatternTrace.h:24`).
- `S070_PHASE3_FEATURE_ADDITIONS.md` phase table lists R1/R2 as PLANNED;
  `S070_PHASE3_FUCKUP_REMEDIATION.md` records them implemented.
- `MEMORY.md` Quick Start says `make && make img` — plain `make` builds only
  `build/main.o` (the `.d` include ordering issue).
- `SD_CARD_PHASE3_OUTPUT` contains `.Spotlight-V100`-style macOS artifacts
  from previous card copies. Add to `.gitignore`.
- Phase-resolution table in `S070_SYSTEMS_GENERAL_CHECK_AND_REVIEW_PLAN.md`
  still shows Phases 2 and 3 as PLANNING/NOT STARTED.
- `S070_PHASE2_LSR01_MENU_USER_FEEL.md` marks LSR-02/03/04 "verified in
  `S070_PHASE2_IMPLEMENTATION.md`". That document records source/build
  verification only, not individual per-scenario hardware results.
- `PATTERN_DYNAMIC_STACK.md` §11 still says S069 Pass 2 hardware validation
  is pending. The S070 plan records PASS on 2026-09-22.

---

## 9. Exit criteria

- T1, T2, and T3 complete. Any T1 failure gets a fix, a new image, and a
  rerun including the quiet-tail check. T2(a) is a predicted FAIL that opens
  Q2. T3 runs after the F5/F6 code fix.
- Q1–Q4 decided (Q2 contingent on T2 result; Q4 confirmed by T3).
- Phase-resolution table updated for Phases 2–4.
- Session 070 handoff log written per `SESSION_HANDOFF_TEMPLATE.md`.

### Result record

| Test | Date | Evidence | Result | Notes |
|---|---|---|---|---|
| T1 | 2026-09-24 | `SD_CARD_ATS_OFF_ON` | PASS | No `E`/`X`; `.hcnames` present; both HCPR records valid; all 16 Scenes have Pattern pairs; `settings.cfg` `active_bank=0` agrees with HCNAMES row 0 (`FullBad 000`) |
| T2(a) | | | | |
| T2(b) | | | | |
| T2(c) | | | | |
| T3 | | | | |
