# S070 Phase 4 — Testing Closeout Framework

Session: S070 · Branch: `dev-ph5-effects` · Baseline: `e1a3223` (Phase 3
closeout), 2026-09-23.

Test image: `build/LXRV2_lxr02.img`, 455,980 bytes (`text=455,548`,
`data=416`, `bss=291,812`). It is byte-identical to
`SD_CARD_PHASE3_OUTPUT/LXRV2_lxr02.img`, so the Phase 3 hardware session
already ran this exact firmware.

Source: `S070_SYSTEMS_GENERAL_CHECK_AND_REVIEW_PLAN.md` Phase 4 (§4.1–§4.4).
Test items from the retired `AUTOSAVE_TEST_CASES_LOAD_SAVE_REVISIONS.md` and
`S070_AUTOSAVE_REENABLE.md` (both recoverable at `git show 65e73c6^:<file>`)
are folded in here, as UQ-6 of `S070_PHASE2_LOAD_SAVE_REVISION.md` required.

Status: plan only. No code changed. No fixtures or tools generated.

---

## 1. Approach

Phase 4 as written, plus the Phase 2 Appendix C matrix, lists roughly sixty
cases. This plan runs **three device tests (T1–T3)** and two host-only checks
(H1–H2). They were chosen by risk:

- T1 covers the Phase 3 behaviour, none of which has been checked on the
  device.
- T2 targets a Pattern persistence defect predicted from the code.
- T3 is the AutoSave OFF→ON check that §4.1 exists for.

Everything else is either covered by earlier hardware evidence or deferred
with a sketch and a reason (§7).

Each test states its propositions, runs from a fixture a host tool can
generate, follows numbered bench steps, and is judged from card evidence
(hidden A/B files, `.hcnames`, `settings.cfg`, `asavetrc.bin`) plus a few
direct observations.

Evidence rules carried over from `DEV_MODES.md` and `AUTOSAVE.md`:

- Never use FAT timestamps. Use generations, CRCs, decoded content, and trace
  order.
- The trace `tick16` is milliseconds and wraps every 65.5 s. Measure short
  intervals with it. For longer spans, count `H` budget reports: firmware
  emits one `H` record per budget class (repair, scalar, Pattern) every 5 s
  (`filesystem.c:24893`), and each carries that class's charged milliseconds.
  An `H` group with zero scalar and Pattern charge is a quiet 5 s window.
- `D` is emitted once per accepted payload byte (`Autosave.c:364`). A
  whole-Bank dirty mark therefore overflows the 2,048-record ring and shows up
  as a `D` burst plus `G` dropped-count reports. That burst is a usable
  landmark.

---

## 2. What Session 070 has already proven

| Item | Change | Build | Hardware evidence | Phase 4 |
|---|---|---|---|---|
| 1.1 | Makefile `-MMD -MP` + `-include *.d` | Yes | n/a | H1 (host) |
| 1.2 / 1.3 | Policy checks (DEV logging on, IWDG off) | n/a | n/a | None |
| R3 | Corrupt PAT4 inside a Bank child invalidates only that Scene | n/a | **PASS**: `SD_CARD_FULLBAD_TEST` (child 03 failed, 15 loaded, row `?`, no `.pat03*`) | None |
| LSR-01 | HCNAMES checkpoint at Kit↔Instrument boundaries, destination-first ordering, optimistic repaint | Yes | **PASS** for Kit Load → VOICE → reboot (`SD_CARD_LSR01-FINAL`, Scene 0 Kit row `FullBad 000`). 609 Instrument-entry records, none failed | Deferred (§7, D-C) |
| LSR-02/03/04 | Deferred HCNAMES rung, blank/`Empty`, generation-tagged async browsers | Yes | Used on hardware during the LSR01 sessions with clean traces. `S070_PHASE2_IMPLEMENTATION.md` records source/build verification only, and no individual scenario has a recorded result | Deferred (§7, D-C) |
| 3.1 + remediation | One step gate (probability) for trigger and automation; non-trigger automation restored | Yes | Final image ran on hardware; no behavioural result recorded | T1 |
| 3.2a–f | Scene targets 384–403 dispatched at runtime; `1vm`–`6vm` 7↔8-bit; `Nvm` cell; VOI `scn`/`fx`; D17 off sentinel `0x1ff` | Yes | **Storage PASS**: `.pat05a`/`.pat05b` gen 68/69, CRC valid, 38 Scene-target entries (384, 389, 392, 397, 403). **Runtime (indirect)**: the trace re-dirties exactly those five Scene 5 settings once per bar (see F3), so the drain reaches the Scene setters | T1 |
| R1/R2 remediation | Scene-target underlines; 8 Hz live Scene-value refresh | Yes | Ran on hardware; no result recorded | T1 |
| 3.3 | 41-byte per-LED layer bitmap, `led_renderFromStack()` | Yes | Ran on hardware; no result recorded | T1 (Part B) |
| S069 Pass 1/2 | Non-semantic Pattern maintenance, CPU budget, Load/Save repair gate | — | **PASS** 2026-09-22 | T3 quiet-tail check repeats 4.1.4 |

---

## 3. Findings from this review

These came out of reading the S070 records against the source and the
existing card captures. They shape the tests below.

**F1 — Predicted stale Pattern restore after an explicit load (tested by
T2).** Every explicit Pattern, Scene, or Bank load resets the Scene's hidden
Pattern generation to zero (`filesystem.c:14835`, `filesystem.c:23538`,
`presetManager.c:495`, `presetManager.c:549`). The next Pattern drain therefore
writes generation 1, and odd generations go to `.patNNb`
(`filesystem.c:2861`, `:24477`). Any older `.patNNa` with generation ≥ 2 stays
valid on the card. Once that drain completes, the Scene's Pattern row becomes
`@`. The boot reader then takes the higher valid generation
(`filesystem.c:27989`) and applies it because the row is `@` (`:28000`).
That is the Pattern from before the load. The window lasts until a second
drain writes `.patNNa`. The same happens after a boot where the row was not
`@` (`:28002`). This is code reading, not an observed failure.

**F2 — Scene-target automation rewrites retained Scene settings and keeps
HCPR busy during playback (open question Q1; measured in T1).**
`seq_applySceneAutomation()` calls the ordinary Scene setters
(`sequencer.c:651`). Those store the value into `scene_settings_t` and mark
scalar AutoSave dirty whenever it changes (`SceneData.c:44`). The Phase 3
capture shows the effect. In its last ~50 s, `P` records advance the HCPR
generation from 197 to 208 at about 4.5 s intervals. The dirty marks repeat
every 1,920 ms, one bar at 125 BPM. So while Scene-target automation plays, the
scalar writer never goes idle. The retained (and later saved) Scene value is
also whatever the automation last wrote.

**F3 — The trace decoders use pre-Session-060 payload offsets.**
`tools/decode_devlogs.py` (and `devlog_unpack.py`, which imports its tables)
still has Scene/Kit parameters starting at byte 8 and Instrument normal at 11.
Session 060 Phase C moved them to 10 and 13 (`Autosave.h:169,180,239`), with
source bytes in between. Every decoded `D` offset in those areas is off by
two. For example, the Phase 3 trace's "scene-parameter[21]" (which reads as a
fader) is actually parameter 19, `6fx`. With the correction, the five
re-dirtied Scene 5 settings are exactly `1vm`, `6vm`, `1ou`, `6ou`, and `6fx`,
matching the Pattern. Fix this before analysing any Phase 4 trace (§6.1).

**F4 — A real power-cut artifact already exists (deferred, §7 D-B).**
`SD_CARD_PHASE3_OUTPUT` contains `.hcnamtmp` (header plus 145 rows) and no
`.hcnames`. Power was removed between "remove old" and "rename temp". The boot
recovery prelude for this state has not been exercised with a recorded
result. It lives only in the AutoSave-on boot paths (`filesystem.c:6982`,
`:28498`, `:28646`).

**F5 — Three §4.1 items are code changes, not tests.** 4.1.1 (lifecycle
trace stages), 4.1.5 (setup-failure retry/UI), and 4.1.6 (active-Scene
Pattern priority) change firmware. The current setup-failure behaviour is
already clear from the code: a failed runtime ensure latches
`fs_autosave_setup_failed` (`filesystem.c:24169`) with no retry, no trace
stage, and no UI. See §7 and Q2.

**F6 — Code reading predicts LED phase slips when a pulse or flash ends on a
blinking LED (T1 Part B records it).** Blink state has no stored phase.
`led_renderFromStack()` returns without drawing when BLINK is set
(`ledHandler.c:292`), and blink ticks are skipped while a pulse or flash is
active (`ledHandler.c:849`). A pulse on a blinking LED can therefore last
50–250 ms and flip the blink phase. D12 accepts "last set wins" for chase
versus blink. Pulse and flash were not discussed.

---

## 4. Test set overview

Tests are numbered in execution order.

| ID | Propositions | Plan items | Fixture | Bench time |
|---|---|---|---|---|
| T1 | Step gate and Scene-target automation play as specified; LED layers fall back correctly | 4.4 for 3.1, 3.2, R1/R2, 3.3 | Generated `T1Gate.pat` | ~35 min |
| T2 | A newer explicit load or edit always wins over older hidden Pattern files; corrupt or ineligible candidates are rejected at boot | 4.2 wrong-generation, edits during snapshot, corrupt/truncated/missing PAT4, later-mutation-wins | Generated library Patterns, on-device preparation, host overlay | ~45 min (**(a) predicted FAIL**, F1) |
| T3 | AutoSave OFF→ON captures the complete current state, releases on Load/Save exit, then goes quiet; Bank identity agrees across the three files | 4.1.2, 4.1.3 (partial), 4.1.4, AS-ENABLE, S056 expedite, R2 | Generated (`001 Full`, clean hidden files) | ~40 min |

All three block closeout.

---

## 5. Tests

### T1 — Step gate, Scene-target automation, and LED layers

**Propositions — Part A (playback).**

1. Automation on non-trigger steps plays.
2. Probability gates the whole step (trigger and automation together), on
   trigger and non-trigger steps alike.
3. Scene targets apply at runtime, hold across voice retriggers, and the
   `1vm`–`6vm` 7↔8-bit conversion is exact.
4. The VOICE/`mix` Scene-setting cells show pattern-wide underlines (R1) and
   live values at about 8 Hz during playback (R2).
5. The step-edit list's VOI categories and the D17 "off" default behave as
   designed.

**Propositions — Part B (LEDs).**

- When a temporary layer (pulse, flash) ends on an LED that still has a lower
  active layer (blink, chase), the LED returns to that layer rather than to
  base.
- When all temporary layers end, the LED shows its latest base.
- `led_clearAll()` leaves nothing that can reappear later.

**Fixture `SD_CARD_P4_T1`.** Canonical card plus a generated
`Pattern/012 T1Gate.pat`: one bar, with all tracks at length 16.

| Track | Step(s) | Trigger | Probability | Automation (target = stored value) | Purpose |
|---|---|---|---|---|---|
| 1 | 1, 5, 9, 13 | on | — | — | Reference pulse |
| 1 | 3 | off | — | `1vm` = 127 | Non-trigger automation; 127 → 255 |
| 1 | 11 | off | — | `1vm` = 63 | 63 → 126 |
| 2 | 13 | on | 64 | `srt` = LOW | Whole-step gate on a trigger step (~50 %) |
| 3 | 1 | off | — | `srt` = CLEAN, plus one entry with target `0x1ff` | Bar reset; off sentinel must be ignored |
| 3 | 5 | off | 0 | `srt` = LOW | Non-trigger step that must never apply |
| 4 | 1–16 | on | — | — | Continuous carrier so decimation is audible |

Pick CLEAN and LOW before generating the Pattern. Turn PERF `srt` by hand,
choose two values that are clearly different by ear, and note which end is
clean.

**Workflow — Part A.**

1. Boot and select Scene 0. Load:[Pattern] `012 T1Gate` into Scene 0. Exit.
2. Play 32 bars (about 61 s at 125 BPM). For each bar, log two things: did
   the snare play at step 13 (Y/N), and was the last quarter crushed (Y/N).
   Also note whether steps 5–8 are ever crushed. Recording the audio output
   is recommended so you can count afterwards.
3. While playing, open the VOICE page for voice 1, `mix` sub-page. `1vm`
   should alternate between 255 and 126 twice per bar, and the `1vm` label
   should be underlined.
4. Stop. Open the step-edit list (MODE STEP) and check:
   - track 1 step 3 shows VOI `scn`, PAR `1vm`, amount 255;
   - track 3 step 1's sentinel entry shows as off/`---`;
   - VOI cycles through 1–6, `scn`, `fx`;
   - on `fx`, PAR shows `---` and no entry can be created;
   - any VOI category change resets PAR to off (D17).
5. Held-step overlay: VOICE page for voice 1, `mix`. Hold track 1 step 7 and
   turn the `1vm` pot to a displayed value of about 200. Release. The
   step-edit list for step 7 should show `scn` `1vm` ≈ 200.
6. Note the `1vm` value while stopped. Switch to another Scene and back in
   PERF, then note it again (D5; see Q1).
7. Play for 60 s more, stop, wait 10 s, power off. Copy → `P4_T1_A`.

**Workflow — Part B (same card, film at 240 fps).** Reinsert the card, boot,
and play `T1Gate` in Scene 0. A 50 ms pulse is 12 frames at 240 fps.

| Case | How to reach it | Layers | Pass |
|---|---|---|---|
| B1 | Load page → VOICE 1 (Instrument Load; the VOICE 1 LED blinks) while voice 1 plays | blink + pulse | Keeps blinking through 8 bars and is never steady for more than 300 ms. Blinks evenly after STOP. Shows its base after exit |
| B2 | STEP mode with a selected step blinking, transport running so the chase crosses it | blink + chase | Blinking resumes after each chase pass. No step left lit or dark behind the chase. No stray chase LED after STOP |
| B3 | Hold VOICE and press the SEQ button of the active Scene (it blinks in this view, `menu.c:6258`) | blink + flash | Blinks again after the flash |
| B4 | Hold VOICE and press SEQ of a non-active present Scene twice | flash + base change | After each flash the LED shows the new mask state (lit, then dark) |
| B5 | With B1 running, let the screensaver engage, then wake it | `led_clearAll()` | LEDs match the current page after wake. No blink on an unselected LED |
| B6 | VOICE page: hold a step (overlay) while playing, then release | overlay base + chase | The overlay shows automated steps and the chase continues. After release, the normal track view returns with no stuck LEDs |

**Pass criteria — Part A.**

- In every bar, a crushed last quarter occurs if and only if the snare fired
  at step 13. Zero mismatches across 32 bars.
- The snare fired in 9–23 of the 32 bars (the 99 % range for a 50 % gate). A
  count outside that range means the probability value is not being read.
- Steps 5–8 are never crushed (the probability-0 non-trigger step is
  suppressed).
- Crushing occurs at all, and it resets at each bar start. If non-trigger
  automation were broken (the original 3.1-A defect), the track 3 reset would
  never apply and the Pattern would stay crushed after the first snare.
- `1vm` shows 255/126 with its underline present (R1, R2, D7).
- The step-edit and overlay observations match steps 4–5.
- `P4_T1_A` decode:
  - track 1 step 7 carries target 384 with a stored value equal to half the
    displayed value;
  - the `0x1ff` entry is still present (it persisted and was not dropped).
- No `E`/`X`.

**Pass criteria — Part B.** Every row of the table holds.

**Record (not pass/fail).**

- For Q1: the number of HCPR generations during Part A step 7's 60 s run, and
  the step 6 `1vm` values.
- For Q3: whether B1 pulses look longer than 50 ms and whether the blink
  visibly changes phase (F6).

**Not covered.**

- BAR1: no caller puts a temporary layer on it. It is only set through
  `led_setValue()`, and `LED_FLASH_GROUP_BAR` has no caller, so it is covered
  by code review.
- Optional if the setup allows: `1ou` routing (needs both output pairs
  monitored), `7dc` (needs a non-Choke type in slot 6), and probability on
  track 7, which shares slot 5.

---

### T2 — Newer Pattern state always wins over older hidden Pattern files

**Propositions.**

- (a) After an explicit Pattern, Scene, or Bank load into a Scene, the next
  boot restores the loaded Pattern, never an older hidden AutoSave Pattern.
- (b) An edit made while a Pattern snapshot is being written is not lost: the
  final hidden winner equals the final edited state.
- (c) At boot, a truncated or CRC-bad newer candidate loses to its valid peer.
  A Scene with no candidates keeps its directory Pattern. Hidden files are
  ignored unless the Pattern row is `@`.

**Predicted result for (a): FAIL** (F1).

**Fixture `SD_CARD_P4_T2`.** Canonical card with:

- `settings.cfg`: `active_bank=1` (`001 Full`, 16 Scenes);
- all hidden files removed;
- two generated library Patterns, both generation 0 with track length 16 and
  all other tracks empty:
  - `Pattern/010 T2Full.pat`: track 1 steps 1–16 on;
  - `Pattern/011 T2Four.pat`: track 1 steps 1, 5, 9, 13 on.

The on-device preparation deliberately builds the dangerous A/B state for (a),
and creates the `@` rows that (c) needs. Only the (c) overlay is host-built.

**Workflow (a).**

1. Boot with the transport stopped. In PERF, visit Scenes 2, 3, 4, and 6 in
   turn. In each, toggle one step and wait 15 s; this gives those Scenes `@`
   Pattern rows for part (c). Finish on Scene 5.
2. Load:[Pattern] `010 T2Full` into Scene 5. Exit to a VOICE page. Wait 15 s.
   Expected: `.pat05b` gen 1 = 16 steps.
3. STEP mode, track 1: turn step 16 off. Wait 15 s. Expected: `.pat05a` gen 2
   = 15 steps.
4. Load:[Pattern] `011 T2Four` into Scene 5. Exit. Wait 15 s. Predicted:
   `.pat05b` gen 1 = 4 steps, and `.pat05a` gen 2 unchanged.
5. Power off. Copy → `P4_T2_A1`.
6. Reinsert and boot. Select Scene 5 and look at track 1's step LEDs.
7. Bound the window: load `011 T2Four` into Scene 5 again, exit, wait 15 s,
   turn track 1 step 2 on, and wait 15 s. Power off. Copy → `P4_T2_A2`.
   Reboot and look at track 1.

Optional variant: repeat steps 2–6 using a root Scene Load in place of the
Pattern Load in step 4. It resets through the same `presetManager.c` path.

**Pass criteria (a).**

- A1: the Scene 5 Pattern row (row 134) reads `T2Four<TAB>@<TAB>R`, and the
  decoded `.pat05b` (gen 1) is the four-step Pattern. Record `.pat05a`'s
  generation.
- Step 6: track 1 shows steps 1, 5, 9, 13. **Fifteen lit steps = FAIL**
  (stale `.pat05a` gen 2 won).
- Step 7: after reboot, four steps plus step 2, with `.pat05a` gen 2 holding
  that content. This should pass either way. It shows the stale window is
  exactly one drain.

**Workflow (b) — edit during snapshot.**

8. Continue on the same card. Select Scene 5 and run the transport. Go to the
   VOICE page for voice 1 and pick a 0..127 parameter cell. Hold track 1
   step 1 (held-step overlay). Turn that pot back and forth continuously for
   15 s, never pausing for more than 250 ms. Finish by turning it fully
   clockwise (127), then release the step. Wait 15 s. Stop. Power off. Copy
   → `P4_T2_B1`.

**Pass criteria (b).**

- The Scene 5 winner has exactly one automation entry for that target on
  track 1 step 1, with value 127.
- The winner generation advanced by at least 3 over A2. The 5 s max-latency
  path must have admitted drains while editing continued, plus a final drain.
- `H` groups during the edit window show Pattern-class charge. No `E`/`X`.

**Failure signatures (b).**

- An intermediate value in the winner (the post-snapshot edit was lost).
- Only one drain (max latency not exercised). Retest with a longer edit.

**Workflow (c) — ineligible and corrupt candidates.**

9. Mount the card read-write for this step only, with indexing off. Run
   `make_phase4_fixtures.py t2c --volume /Volumes/<card>`. The tool rewrites
   the pairs below with host-built fingerprints (A = only track 3 step 1 on;
   B = only track 3 step 16 on) and edits the Scene 6 Pattern row. Eject.

   | Scene | `.patNNa` | `.patNNb` | Pattern row | Expected after boot |
   |---|---|---|---|---|
   | 2 | valid, gen 10 | gen 11, truncated to 5,000 B | `@` | A |
   | 3 | valid, gen 10 | gen 11, one pool byte flipped (CRC bad) | `@` | A |
   | 4 | absent | absent | `@` | Boot completes; record what is resident |
   | 6 | valid, gen 10 | valid, gen 11 | changed to `-` | Bank child Pattern, not either fingerprint |

10. Boot. In PERF, check Scenes 2, 3, 4, and 6: track 3 LEDs, and that each
    Scene's Kit still sounds right. Power off. Copy → `P4_T2_C1`.

    Optional, for an exact host decode in place of reading LEDs: before
    power-off, do a Save:[Bank] with all 16 Scenes to an empty slot. That
    writes the resident RAM state to the card.

**Pass criteria (c).**

- Every "Expected" cell in the step 9 table holds.
- Pattern rejection never changes a Scene's Kit or settings.
- No `bootlog.bin`. No `E`/`X`. The `Q` records are consistent with the
  table.

---

### T3 — AutoSave OFF→ON captures the complete current state

**Propositions.**

- While AutoSave is OFF, edits produce no hidden-file writes.
- Turning it back ON captures every scalar and Pattern change made while it
  was OFF, and the result survives reboot.
- Once the system has converged, it writes nothing further while idle.
- Leaving a Load/Save page releases a suppressed scalar writer within about
  250 ms. This is the Session 056 expedite, which MEMORY.md records as never
  hardware-verified.
- `settings.cfg`, HCNAMES row 0, and the HCPR winner agree on the active Bank
  (the R2 invariant).

**Code path.** `filesystem_setAutosaveEnabled()` (`filesystem.c:23544`) queues
the ensure operation. `filesystem_autosaveSetupCompleted()` (`:24148`) then
enables tracking and calls `autosave_markResidentBankDirty()`, which marks
every Bank field and every present Scene including its Pattern bit.

**Fixture `SD_CARD_P4_T3`.** Canonical `SD_CARD/`, with:

- `settings.cfg`: `active_bank=1` (`001 Full`, 16 Scenes) and `autosave=1`;
- every hidden file (`.hcprms*`, `.pat*`, `.hcnames`, `.hcnamtmp`) and trace
  file removed.

`001 Full` has no Scene-target automation, so F2 cannot keep the writer busy.

**Workflow.**

*Part A — baseline*

1. Flash the test image, insert the card, boot. Leave the transport stopped
   for 3 min. Power off. Copy the card (read-only mount, §6.3) → `P4_T3_A0`.

*Part B — edits while OFF, capture on ON*

2. Reinsert the card and boot. Start the transport on Scene 0.
3. Global page: `AutoSave` → off.
4. Make four edits, writing each exact value on the log sheet:
   - E1: in Scene 0, change one VOICE-page parameter on voice 1 by a large
     step;
   - E2: in Scene 0, STEP mode, toggle track 1 step 16;
   - E3: switch to Scene 15 in PERF, then change one VOICE-page parameter on
     voice 3;
   - E4: in Scene 15, toggle track 2 step 16.

   Then switch back to Scene 0.
5. Wait 30 s.
6. Global page: `AutoSave` → on. Leave the transport running for 60 s, stop
   it, then wait 5 min without touching anything.
7. Power off. Copy → `P4_T3_B1`.
8. Reinsert and boot. Confirm E1–E4 on the device (VOICE-page values and step
   LEDs in both Scenes). Power off.

*Part C — Load/Save suppression and page-exit expedite*

9. Boot with the transport stopped. Change one VOICE-page parameter in
   Scene 0 (E5). Within 1 s, enter Load:[Kit]. Stay there for 20 s by
   stopwatch without touching anything. Press a mode button to go to a VOICE
   page. Wait 30 s. Power off. Copy → `P4_T3_C1`.

**Pass criteria.**

- `card_audit.py --baseline P4_T3_A0 P4_T3_B1`:
  - the HCPR winner generation advanced, and both HCPR records are valid;
  - the winner payload differs from A0 only at the E1 and E3 bytes.
- Every present Scene has a newer Pattern winner, because the whole Bank was
  marked. Decoded Pattern content equals A0 except Scene 0 track 1 step 16
  and Scene 15 track 2 step 16.
- In the B1 trace, the E2 and E4 `K` records are followed by no `D`, no
  `A`…`T`, and no Pattern-class `H` charge until the ON `D` burst.
- After the ON burst the trace shows `A/V/M/C/P/T` cycles, then Pattern work,
  then a quiet tail. The quiet tail must contain at least 36 `H` groups
  (≥ 3 min) with zero scalar and Pattern charge.
- B1: `settings.cfg` `active_bank`, the HCNAMES row 0 source, and the HCPR
  winner's Bank slot all equal 1.
- Step 8: all four edits are present after reboot.
- C1 trace: the E5 `D` at t0, at least one `W` during the dwell, and the first
  `A` at t1 with `t1 − t0` in `[dwell, dwell + 1.5 s]`.
- No `E` or `X` records in any capture.

**Failure signatures.**

- Any HCPR or Pattern generation while OFF.
- An edit missing after ON or after reboot.
- Generations still advancing in the quiet tail.
- `t1 − t0 ≈ dwell + 5 s` (the expedite is not working).

**Record as a result, not pass/fail:** the time from the ON burst to the last
Pattern drain. The S069 CPU budget (2.5 % playing / 5 % stopped) paces the
drains, so a slow convergence is expected. The proposition is completeness
and quiescence, not speed.

---

### Host-only checks

- **H1 — Makefile dependencies (1.1).**
  1. Run `make all`.
  2. `touch config.h`, then `make all` again: it must recompile every object
     that includes `config.h` and relink.
  3. A further `make all` with no changes must do nothing.

  Also note: plain `make` builds only `build/main.o`, because
  `-include $(OBJS:.o=.d)` comes before `all` (recorded in
  `S070_PHASE2_IMPLEMENTATION.md`). MEMORY.md's Quick Start still says
  `make && make img`. The fix is `.DEFAULT_GOAL := all`; that is a Makefile
  change and is not made here.
- **H2 — decoder correction (F3).** After the fix, decoding
  `SD_CARD_PHASE3_OUTPUT/asavetrc.bin` must name the Scene 5 dirty bytes as
  `1vm`, `6vm`, `1ou`, `6ou`, and `6fx`.

---

## 6. Prerequisites

### 6.1 Tools

All of these are host-side Python. None touches firmware.

| Tool | Kind | Purpose | Used by |
|---|---|---|---|
| `tools/decode_devlogs.py` (and `devlog_unpack.py` through its import) | Fix | Update payload geometry: Scene/Kit parameters start at 10, Instrument normal at 13, morph at 85. Label source bytes (Scene/Kit +8..9, Instrument +11..12). Name Scene parameters by `AUTOSAVE_SCENE_PARAM_*` (for example `vm[5]`, `fx[5]`). Add compact `H` decoding to `devlog_unpack.py` | All |
| `tools/pat4.py` | New | Decode and validate PAT4 (header, CRC32C, allocator audit of address/bitmap/back-reference/pool). List steps with triggers, specials, and named automation targets. Semantic diff and fingerprint read. Build PAT4 from a small JSON spec, using `Pattern/002 blankPat.pat` as the template for track parameters | T1–T3 |
| `tools/card_audit.py` | New | One report per copied card: settings; HCNAMES rows with `R`; HCPR A/B validity and winner; 16 Pattern pairs (validity, generation, winner, fingerprint); trace summary with landmarks (`K`, `D` burst, `W`, `A`, `P`, `G`, `Q`, `E`, `X`) and `H`-derived elapsed time. `--baseline` diffs HCPR winner bytes (mapped to field names) and Pattern semantics against an earlier copy | T1–T3 |
| `tools/make_phase4_fixtures.py` | New | Builds `SD_CARD_P4_T1`…`T3` from canonical `SD_CARD/`, and applies the T2(c) overlay to a mounted card | T1–T3 |

A generated PAT4 is only a valid fixture once the device accepts it. Each
Load step that uses one is also that fixture's acceptance check.

### 6.2 Fixtures

- Fixture directories sit at the repository root beside the existing
  `SD_CARD_*` captures. Name evidence copies `P4_T<n>_<step>`.
- `SD_CARD/Bank` contains two slot-005 folders (`005 NewWave` and
  `005 PtTst`). Any Load or Save of slot 005 takes the ambiguous-slot path.
  Fixtures built from `SD_CARD/` should drop one, since no Phase 4 test
  targets that case.
- Regenerate fixtures from the tool; never hand-patch them. Never edit
  evidence copies.

### 6.3 Card handling

- Flash `build/LXRV2_lxr02.img` from HEAD `e1a3223` for every test. If a fix
  lands during Phase 4, rebuild with `make all && make img`, record the new
  sizes, and rerun the affected tests.
- Mount the card read-only on the Mac for copying
  (`diskutil mount readOnly /dev/diskNsM`), and disable indexing
  (`mdutil -i off`). T2 step 9 is the only read-write mount.
  - Earlier captures contain `.Spotlight-V100` and `.fseventsd`, which means
    macOS wrote to the card's root directory between device boots. Several
    commits also include those Spotlight stores.
  - Every test here puts the same card back into the device, so host writes
    can change what the device sees.
- After the last action in a test, wait at least 10 s before power-off so the
  trace flushes.

---

## 7. Not tested in Phase 4

### 7.1 Deferred tests

Each sketch is enough to rebuild the full test when it is picked up.

**D-A — Partial Bank Load/Save touches only selected Scenes.**

- *Why deferred.* S057 hardware-tested the partial Bank Save fix (a
  byte-identical 161-file tree). Masked Bank Load has been in place since
  S040/S052. No S070 change touches either path. The Bank identity
  (R2) check has moved into T3.
- *Sketch.*
  - Generate two 16-child Banks, `040 P4FpA` and `041 P4FpB`, whose Patterns
    encode Bank and child: track 1 step NN+1, plus track 2 step 1 (A) or
    step 16 (B).
  - Load 041 with mask {1, 5, 9, 14} and expect `B` witness `0x4222`.
  - Save all 16 Scenes to an empty slot (captures RAM), then save mask {5}
    over 040.
  - Compare trees on the host: unselected children byte-identical, and 041
    unchanged.
- *Run when.* Bank Load/Save code changes, or before Phase 5 adds `effects.fx`
  as a live Scene component.

**D-B — HCNAMES temp promotion and the wrong-format Pattern candidate.**

- *Why deferred.* The `.hcnamtmp` recovery prelude did not change in S070. The
  stack-size variant fails in the same candidate validator as the truncated
  case already in T2(c).
- *Ready fixture.* `SD_CARD_PHASE3_OUTPUT` is a genuine orphan-temp card:
  writing it to a card and booting it once is the cheapest first check.
- *Sketch, one boot each, from a device-produced base:*
  - orphan temp (expect promotion);
  - temp beside live (the temp wins);
  - truncated temp beside live (temp discarded);
  - orphan temp with `autosave=0`, as a characterization: the prelude only
    runs on AutoSave-on paths (F4).

  Distinguish them with a Scene 0 name row (`TMPNAME` / `LIVENAME`).

**D-C — Phase 2 browser supersession and power loss.**

- *Why deferred.* The LSR01 sessions used these paths on hardware with clean
  traces. LSR-01 persistence is confirmed. What remains is edge cases.
- *Sketch.*
  - Fast Kit scroll A → B → C, then OK within 0.3 s: the committed Kit is C,
    5/5 times.
  - OK straight after a Kit → Scene type switch is refused, then works once
    the name resolves.
  - Kit Load → exit → power cut at 0.5, 2, and 10 s. Each time the identity
    must match the sound: old everything, new name with `R` plus a `Q`
    Case-2 narrow load, or new everything.
  - Instrument load → exit to Kit (or VOICE 3) → power cut within 1 s. This
    covers Supplement scenarios 2–3 and Appendix C #5, #8, #21, #23.
- *Run when.* Any further Load/Save browser change, or before closing the
  Load/Save revision backlog for good.

**D-D — Duplicate trace-file directory entries (§4.3).**

- *Why deferred.* The plan itself expects a non-issue after the S056 LFN fix
  in `afatfs_createFileContinue()`.
- *Sketch.*
  - Prepare a card root where a run of ≥ 96 deleted entries (`0xE5`) comes
    before the existing `asavetrc.bin` and `pattrace.bin`.
  - Run five boot/flush cycles.
  - `dd` the partition read-only and audit the raw root directory with a new
    `tools/fat_rootdir_audit.py`. Expect one live entry per name.
  - `bootlog.bin` shares the same `afatfs_fopen_lfn()` create path
    (`filesystem.c:5217`).
- *Run when.* A card is being imaged anyway, or if a duplicate is ever seen.

### 7.2 Plan items handled without a test

| Item | Disposition |
|---|---|
| 4.1.1 lifecycle trace stages | Not needed for T3: `K`/`D` landmarks, the ON `D` burst, and `H` groups place every event. Add OFF/ON/setup stages only if T3 fails ambiguously |
| 4.1.3 ON during recording, or during an active scalar or Pattern transaction | Cannot be timed by hand. Record/erase admission is a code gate (`seq_recordActive`/`seq_eraseActive`). Defer until there is instrumentation |
| 4.1.5 setup-failure retry/UI | The behaviour is already clear from code (F5). Decision Q2 |
| 4.1.6 active-Scene Pattern priority | Latency only. S069 added a rotating cursor and a 5 s maximum latency. No evidence of a problem. Defer |
| 4.2 boot restore from every active Scene; active/shown/played alignment | S068 accepted chase after boot in every Scene. T2 and T3 reboot with non-zero Scenes in play |
| 4.2 one-time initialization at every load/retry phase | An asynchronous retry cannot be forced by hand. Code-review item |
| 4.2 power cuts at exact write phases | T2(c) generates the Pattern-side artifacts. HCNAMES-side: D-B |
| AS-BOOT HCPR matrix (wrap, equal generations, both invalid) | Scalar reader accepted in S061/S064 and not changed in S070. Defer |
| LS-DELETE low-level matrix; LS-SAVE interrupted Scene Save | Out of S070 scope; code unchanged |
| Phase 2 Appendix C #1–4, 6, 7, 9–13, 15–20, 22, 25 | Exercised during the LSR01 sessions with clean traces. #14 is confirmed by LSR01-FINAL. #24 is in T3. #5, #8, #21, #23 are in D-C |

---

## 8. Decisions needed

**Q1 — Scene-target automation and retained Scene settings (F2).** Today,
playing Scene-target automation has three effects:

- it overwrites the Scene's stored value;
- the value a Scene Save keeps depends on where playback stopped;
- the scalar writer rewrites HCPR about every 4.5 s for as long as it plays.

Options:

- (a) Accept and document the behaviour.
- (b) Apply Scene-target automation as a runtime overlay, as the LFO path
  does, so it never touches retained settings or AutoSave.
- (c) Keep writing the setting but suppress AutoSave marking for
  automation-originated writes.

Recommendation: (b). Automation is playback behaviour, and (c) would leave the
in-RAM and on-card values disagreeing. Decide before Phase 5 adds `fx`
targets. T1 records the numbers.

**Q2 — Setup-failure policy (4.1.5).** Recommendation: add a bounded retry
(for example every 30 s while mounted, never while a Load/Save page owns the
facade), one trace record per failure, and a Global-page indication.
Implement after T3 so T3 measures the unchanged path.

**Q3 — Pulse or flash over blink (F6).** Either accept the phase slip as
consistent with D12, or store one phase bit per blink slot so that expiry
redraws the correct phase. Decide from T1 Part B's video.

**Q4 — Only if T2(a) fails.** Keep each Scene's Pattern generation increasing
instead of resetting it to zero:

- at load commit, continue from the in-RAM generation;
- in the boot reader's non-`@` branch, seed from the winner's generation
  (`filesystem.c:28002`) instead of 0.

This needs no extra SD work. The "new epoch" rationale in the reset's comment
is cosmetic, because HCNAMES carries the Pattern identity. The alternative,
deleting both hidden candidates at load commit, adds SD operations to every
load.

---

## 9. Execution order and exit criteria

**Order.**

1. Prerequisites: the decoder fix and H2, the new tools, the fixtures, and H1.
2. T1. It needs no persistence, so it is a good first bench session.
3. T2. If (a) fails, still run (b) and (c), which do not depend on it, then
   decide Q4.
4. T3.

**Exit criteria.**

- T1–T3 pass. Any failure gets a fix that passes a rerun of that test and of
  T3's quiet-tail check.
- The user accepts the §7 deferrals.
- Q1–Q3 are decided, and Q4 as well if T2(a) fails.
- The phase-resolution table in `S070_SYSTEMS_GENERAL_CHECK_AND_REVIEW_PLAN.md`
  is updated for Phases 2–4, with D-A to D-D carried into `SCOPING_TARGETS.md`,
  and the §10 items are done.
- A Session 070 handoff log is written per `SESSION_HANDOFF_TEMPLATE.md`.

### Result record

| Test | Date | Evidence copies | Result | Notes |
|---|---|---|---|---|
| H1 | | | | |
| H2 | | | | |
| T1 | | | | |
| T2 | | | | |
| T3 | | | | |

---

## 10. Spec hygiene found during this review (rolling item 1.4)

- `tools/decode_devlogs.py` payload geometry (F3).
- `DEV_MODES.md` names the PatternTrace file `/pattrc.bin`; the firmware and
  the cards use `pattrace.bin` (`PatternTrace.h:24`).
- `S070_PHASE3_FEATURE_ADDITIONS.md` phase table still lists R1/R2 as
  PLANNED. `S070_PHASE3_FUCKUP_REMEDIATION.md` records them implemented and
  build-verified.
- `S070_PHASE2_LSR01_MENU_USER_FEEL.md` marks LSR-02/03/04 "verified in
  S070_PHASE2_IMPLEMENTATION.md". That document records source/build
  verification only.
- `MEMORY.md` Quick Start and `PATTERN_DYNAMIC_STACK.md` §11 still say S069
  Pass 2 hardware validation is pending. The S070 plan records PASS on
  2026-09-22.
- `MEMORY.md` Quick Start build command (see H1).
- Card-copy `.Spotlight-V100` stores are committed in several S070 commits.
  Add them to `.gitignore`.
- The S070 plan's phase-resolution table still shows Phases 2 and 3 as
  PLANNING / NOT STARTED.
