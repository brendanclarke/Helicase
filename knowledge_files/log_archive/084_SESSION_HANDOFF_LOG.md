# Session 084 — Handoff Log

```
DATE: 2026-10-22 / 2026-10-26
SESSION GOAL: Fix live and step rolls (S084 P0 "Roll Fix-up"): the PERF
              roll list must match the STEP list, a recorded roll must play
              back at the rate it was played, and roll rate must follow the
              master clock, not the track's step scale. Also close the two
              host-only import-tool tasks done just before the session
              (LXR1 and LXR-02 converters).
COMPLETED:
  P0  Roll fix-up: assessment (findings F1-F8), plan (P1-P4), user
      decisions (Q1-Q9), a line-level schedule, implementation, two
      assessments and their fixes:
      P1  step roll engine: a time window on the master tick, not a hit
          count; carry rule Q4; parking; deferred-carry stretch;
      P2  PERF/MIDI overlay rolls and flam mains on the master tick (TIM3
          live engine, mailbox drained by the foreground); A3 drain before
          an overlay switch; A1 external clock tick interpolation;
      P3  one roll list: PERF types 0..14 = 4 singles, 2 flams, the 9 STEP
          rates;
      P4  LiveRecord: every held roll is a per-track span; `pv` rhythm
          recorded; take boundary ends rolls; A2 early-release rule;
      B1  overlay reconfigure drains before judging a held entry; B2 doc
          nits.
  Rollback: the first S084 tree (roll fix-up + voice expansions P1-P5) was
      abandoned (over the voice CPU budget, hardware freeze); the roll
      fix-up was re-implemented alone from the S083 base.
  Import tools (logged here; host only): tools/lxr1_converters/ (LXR1
      .snd/.pat/.all/.prf) and tools/lxr2_converters/ (LXR-02 kits,
      patterns, projects, songs).
  Session close: this log, the index entry, specification updates,
  MEMORY.md, volatile notes.
VERIFIED ON HARDWARE: no. The re-implemented roll fix-up builds clean and
  passes the host check H1 (§9.1); the hardware matrix V1-V7 (§9.2) is
  carried to the next session. The import tools have never been loaded on
  hardware (§12.9, §13.9). The abandoned spike build was flashed during the
  voice work; its freeze concerns the voice code only (§11).

CHANGES THIS SESSION (detail in §14):
- Core/Sequencer/sequencer.c/h: time-window step roll engine
  (seq_rollWindow, SEQ_ROLL_RUN/CARRY, seq_rollClearTrack(), carry
  eligibility, deferred-carry stretch); live overlay engine (seq_live*,
  seq_processLiveTicks(), mailbox); external clock interpolation
  (seq_extClockTicks(), seq_extTicksOwed).
- Core/Hardware/frontPanel/buttonHandler.c: overlay gesture table 4 B per
  track; ms timing retired; overlayRhythm/Drain/DrainAll; start, end,
  reconfigure and tick moved onto the live engine; 15 overlay types.
- Core/Sequencer/LiveRecord.c/h: per-track roll capture liveRec_roll[7];
  every held roll a span; rhythm recorded; endRoll per track +
  endAllRolls; newTake ends rolls; liveRec_rollRateChange(track, idx,
  rhythm); liveRec_trigger_t.rhythm.
- Core/Bank/Scene/Pattern/PatternData.c/h, PatternStackService.c:
  pat_record_spec_t.rhythm; pat_recordStep() stores the rhythm with a roll.
- Core/Menu/MenuText.h, menu.c; Core/Bank/Scene/Preset/ParameterArray.h:
  perfBtnTypeNames 15 entries, clamp 0..14, renderer count from [0][0].
- Core/MIDI/MidiNotes.c: lr.rhythm = 0.
- build/LXRV2_lxr02.img rebuilt.
- .gitignore: Helicase-dev-ph7-spike/ (the spike's old location inside
  the repository; now unused, harmless).
- tools/lxr1_converters/ (new, efea1bf) and tools/lxr2_converters/ (new,
  59406a4): host-only import tools with their manuals.
- Specifications: PATTERN_DYNAMIC_STACK, MODULE_INTERCHANGE_SPEC,
  MIDI_SPEC, FILESYSTEM_SPEC, STORAGE_SRAM_MANIFEST,
  BANK_PRESET_ARCHITECTURE (§16).

KNOWN ISSUES INTRODUCED: none open. Accepted behaviours: §15.
KNOWN ISSUES RESOLVED: F1-F8 (§3); external-clock tick bunching for step
  rolls, shuffle and micro-timing (pre-existing since S081, A1); the S083
  open product decision "should recorded overlay rolls carry the `pv`
  rhythm" (decided: yes, Q8).

NEXT SESSION RECOMMENDED GOAL: run the S084 hardware matrix V1-V7 (§9.2),
  then start Session 085 (oscillator and modulation-mode cherry-pick from
  the spike, `../Helicase-dev-ph7-spike/S085_OSC_MOD_CHERRYPICK.md`).
BLOCKERS: the S084 hardware results gate S085 (post-mortem rule: each
  session ends with a hardware check before the next one starts).

CRITICAL REMINDERS FOR NEXT SESSION:
- A roll is a time window on the master tick. Never go back to counting
  hits per step; a carry fires no hit and never touches the accumulator.
- Carry rule (user Q4): same rate and rhythm; refused when the PREVIOUS roll
  step's fill was below 100 %. Fill does not have to match.
- Overlay hits fire in TIM3 (seq_processLiveTicks()); LiveRecord is only
  ever called from the foreground drain. seq_processLiveTicks() runs first
  in seq_processSchedulerTick(), before the running gate.
- External clock edges run only the first tick of their period; seq_tick()
  interpolates the rest. Never run a period's ticks back to back again.
- Drain overlay entries before bh_overlayActive changes (A3) and before
  judging an entry held in buttonHandler_overlayReconfigure() (B1).
- The spike (../Helicase-dev-ph7-spike/, 26fb7c3) is reference only. Copy
  only the hunks a session document names; shared files carry several
  phases' changes.
- The S084 implementation is uncommitted at close (fact, not a prompt).
```

---

## 1. Session overview and timeline

| Date | Work |
|---|---|
| 2026-10-22 | **LXR1 converters** (host only, `dev-ph6-midi`, commit `efea1bf`, merged by PR #11): `tools/lxr1_converters/` (§12). |
| 2026-10-23 | **LXR-02 converters** (host only, commit `59406a4`, merged by PR #12, `debbe72f` on `prime`): `tools/lxr2_converters/`; user follow-up on default track MIDI notes; task closed (§13). |
| 2026-10-24 | **S084 P0 assessment and plan** (`S084_P0_ROLL_FIXUP.md`): F1–F8, P1–P4, Q1–Q9; user decisions Q2, Q4, Q7. Line-level schedule (`S084_P0_IMPLEMENTATION.md`). First implementation of P1–P4 on `dev-ph6-midi`; assessment A1–A4 (user approved all recommended actions) and fixes. |
| 2026-10-25/26 | S084 voice expansions P1–P5 on the same tree (oscillators, sample/wavetable catalogue, transient and filter-drive types, Mix FX, voice buffers). Over the voice CPU budget; hardware freeze; abandoned. Spike committed as `26fb7c3` "voice feature spike over budget" on branch `dev-ph7-voice-spike`; post-mortem written (§11). |
| 2026-10-26 | Rollback to `59406a4`. The schedule was checked entry by entry against the spike's code, completed (A1–A4 folded in as entries, build order fixed, a missing comment entry added, a host check defined), and marked "not implemented". User approved the RAM (+87 B). Pre-implementation commit `91265e9c` on `dev-ph7-voice_expansion`. Re-implementation of P1–P4 + A1–A4; post-implementation assessment (B1, B2); fixes; close-out. |

**Method** (as S081–S083): plan with numbered questions → line-level
schedule with paste-ready comment blocks (What / Why / Inputs / Outputs /
Accessors / Affiliates, Context and RAM where they apply) → implementation →
assessment appended to the schedule (numbered findings) → targeted fixes →
close-out. The re-implementation followed the revised schedule exactly; the
schedule rule "comments must not cite entry IDs" (the schedule is deleted at
close-out) was added and kept.

**State at close:** branch `dev-ph7-voice_expansion`; `91265e9c` holds the
two S084 P0 documents and `.gitignore`; all S084 code and the close-out
documents are uncommitted. The spike lives at `../Helicase-dev-ph7-spike/`
(its own git repository, `26fb7c3`; the post-mortem and the S085/S086 plan
documents there are untracked files).

---

## 2. How rolls worked before S084 (code at `59406a4`)

### 2.1 Step rolls (TIM3)

- Storage: the step special `roll` (index 0..9) plus `rhythm` (`rtm`) and
  `fill` (`PatternData.h`).
- `seq_rollPpq()`: `{0, 48, 32, 24, 16, 12, 8, 6, 4, 3}` = off, 1/8, 8T, 16,
  16T, 32, 32T, 64, 64T, 128 (96-PPQ ticks; S081 table). Names
  `stepRollRateNames` (`MenuText.h`).
- `seq_rollBegin()` at the step's (possibly deferred) fire:
  window = `stepScale_ticksQ8(scale) >> 8` × fill / 100; `slots =
  ceil(window / ppq)` (cap 255); no carry: fire hit #1, `accum = ppq`,
  `remaining = slots − 1`; carry (`seq_rollCarries()`: active and the same
  rate, rhythm **and** fill): `remaining += slots − 1`, no hit, reset-only.
- `seq_processRollTicks()` (every master tick after the DDA) skipped a
  track whose `remaining == 0` **before** decrementing `accum`.
- A fill < 100 step armed a duck countdown (`seq_rollDuckDelta/Delay/
  Active[7]`, `seq_rollArmDuck()`) that fired
  `instrumentManager_requestGateDuck()` at the fill end.

### 2.2 PERF / MIDI overlay rolls (foreground)

- Types `perfBtnTypeNames`: `vmx v10 v64 v32 flm fls r/8 r16 r32 r64 r6t
  128` (0..11; menu clamp 11; `settings.cfg perf_btn_lt/rt`).
- `buttonHandler_overlayStart()`: rolls used `rollDiv = {3,4,5,6,6,7}` with
  `rollIdx = {1,3,5,7,8,9}`; the interval in ms was
  `buttonHandler_overlayDivisionMs()` from `15000 / PAR_BPM`, × 2/3 for
  `r6t`. The same two tables were copied in
  `buttonHandler_overlayReconfigure()`.
- `buttonHandler_overlayTick()` (from `buttonHandler_tick()`) fired due
  repeats on `time_sysTick` (TIM6, 1 ms) and reported each to LiveRecord.
- MIDI notes under an overlay used the same executor
  (`buttonHandler_overlayMidiNoteOn()`), so every finding applied to both.
- `bh_overlay[7]` was 8 B per track: `heldMidiNote`, `note`, `pos`,
  `count`, `interval`, `next` (S083 Q5).

### 2.3 Recording (LiveRecord, foreground)

- `liveRec_triggerIn()` classed a roll **slow** when `ppq ≥ step interval`
  and recorded it hit by hit as single steps; otherwise **fast**: the start
  step got the roll (fill 100), the track was live-held, each later ring
  entry wrote the step just ended as a span (timing 0, same rate, fill 100),
  and the release wrote the end step with fill = release offset
  (`liveRec_endRoll()`).
- `liveRec_rollPpqTicks()` duplicated `seq_rollPpq()` for that test.
- One roll slot (`liveRec_rollHeld/Track/Step/Index/StartDelay/Span/Hits/
  PendingIdx`, note, velocity, note-default) for all tracks.
- A recorded fast roll held for N steps was stored as N adjacent roll
  steps, which played back through the **carry** path.

---

## 3. Findings F1–F8 (assessment 2026-10-24, from reading the code)

| # | Finding | Severity | Explains |
|---|---|---|---|
| F1 | PERF offered 6 roll rates; STEP offers 9. `8tr`, `16t`, `32t` could not be played live. | Product gap | List mismatch |
| F2 | A roll carried into the next step dropped its boundary hit and started one roll period late; each carried step played `slots − 1` hits. Every recorded roll longer than one step is stored as carried steps. | **Defect, high** | Recorded rate; step-scale dependence |
| F3 | A recorded roll's last step has a different fill, so it did not carry; it restarted the roll at the step boundary (phase jump, rhythm reset). | Defect, medium | Recorded rate |
| F4 | Overlay rolls used a 1 ms foreground timer from `PAR_BPM` with integer truncation: 2–7 % fast, no external sync, render-load jitter. | **Defect, high** | Live ≠ recorded |
| F5 | LiveRecord kept one open roll; concurrent overlay rolls (S083 Q5) corrupted it. | Defect, medium | Multi-track recording |
| F6 | Slow rolls were recorded hit by hit and each hit quantised separately; a rate off the quantise grid (e.g. `8tr`) was lost. | Latent (real with F1) | Recorded rate |
| F7 | The `pv<`/`pv>` rhythm played live but was not recorded (rhythm cleared; flat velocity 100). | Open product decision | "Sounds different" |
| F8 | A take boundary (REC off, `rtk` change, …) did not end an open recorded roll; its release still wrote the end step. | Defect, low | — |

**F1 — the two lists.**

| Step index | STEP label | Ticks | PERF type before S084 |
|---:|---|---:|---|
| 1 | `1/8` | 48 | `r/8` (6) |
| 2 | `8tr` | 32 | — |
| 3 | `16` | 24 | `r16` (7) |
| 4 | `16t` | 16 | — |
| 5 | `32` | 12 | `r32` (8) |
| 6 | `32t` | 8 | — |
| 7 | `64` | 6 | `r64` (9) |
| 8 | `64t` | 4 | `r6t` (10) |
| 9 | `128` | 3 | `128` (11) |

Different labels for the same rate (`r6t`/`64t`, `r/8`/`1/8`); the mapping
was hand-maintained in three places (two buttonHandler tables and the
`PATTERN_DYNAMIC_STACK.md` §6.7.4 list).

**F2 — the carry lost the boundary hit (root cause).** When a chain's
`remaining` reached 0, `accum` was left at `ppq − 1` and then frozen (the
`remaining == 0` test came before the decrement). A carry at the next
boundary added `slots − 1` hits (it counted the step's own first hit, which a
carry never fires) and resumed from the frozen accumulator. Trace: 1/16 step
(24 ticks), roll `32` (12 ticks), fill 100, two adjacent roll steps:

| Tick | Expected | Actual (S083) |
|---:|---|---|
| 0 | hit | hit (step A, slots 2, remaining 1) |
| 12 | hit | hit (remaining 0, accum 11 frozen) |
| 24 | hit | — (carry: remaining 1; accum 11 → 10) |
| 35 | — | **hit** (late) |
| 36 | hit | — |
| 48 | hit | — (carry: remaining 1) |
| 59 | — | **hit** |

After the first step the roll played one hit per step, 11 ticks late: a
step-rate pulse. Roll `64` at 1/16 played `0 6 12 18 | 29 35 41 | 53 59 65 …`
(3 of 4 hits, gap at every boundary); the slower the step scale, the smaller
the relative loss (1/8 step: 7 of 8), which is why the recorded rate
"varied with the step scale". The same defect hit typed STEP rolls in
adjacent steps with identical rate, rhythm and fill; a single roll step
played correctly. Secondary issues of the count model: `slots` used the
truncated step interval, and a rate that does not divide the step (`16t` in
a 24-tick step = 1.5 hits) cannot be represented; a delayed start step's
window ran past the boundary and the carry added the next step's count on
top. (The 255 slot cap was checked and is never reached: the step-scale LUT
tops out below 256 ticks.)

**F3.** `liveRec_endRoll()` wrote the release step with fill < 100;
`seq_rollCarries()` required equal fill, so `seq_rollBegin()` fired a fresh
hit #1 at that boundary, resetting `accum` and the rhythm position. For an
unquantised start (delay d ≠ 0) the phase jumped from `d + k·ppq` to the
boundary on the last step: a hiccup or double hit, and with F2 a sudden
return to the full rate.

**F4 — overlay timing error** (`15000 / PAR_BPM` integer ms, then integer
division):

| BPM | Rate | Exact (ms) | Used (ms) | Error |
|---:|---|---:|---:|---|
| 120 | 128th | 15.625 | 15 | +4.2 % fast |
| 120 | 64T | 20.83 | 20 | +4.2 % fast |
| 137 | 16th | 109.49 | 109 | +0.4 % |
| 137 | 128th | 13.69 | 13 | +5.3 % fast |
| 175 | 128th | 10.71 | 10 | +7.1 % fast |

Also: under `usb`/`din`/`pls`/`aut` sync the overlay ran at the menu tempo
while the recording played at the real clock; the repeat was serviced from
the foreground on a 1 ms grid (render load delayed hits); hits drifted
against the master grid. The flam main (1/32 later) had the same dependency.

**F5.** A `ROLL_HIT` from any track only incremented `liveRec_rollHits`
without checking the track (another track's hits were never recorded and
inflated the first track's count); a `ROLL_START` on a second track
overwrote the slot without ending the first (its `seq_setLiveHold(…, 1)`
stuck, silencing its stored triggers); `liveRec_rollRelease(track)` ignored
`track`.

**F6.** The slow path quantised each hit (`seq_quantiseTick()`); with F1's
triplet rates a `qnt 16` recording would snap `8tr` to straight sixteenths;
the slow/fast threshold also depended on the track's `scl`.

**F7.** Listed as an open product decision since S083; same encoding as the
step `rtm` byte, so recording it was small once F3 was fixed.

**F8.** `PATTERN_DYNAMIC_STACK.md` §6.7.4 said a Scene change, stop, REC off
or `rtk` change ends an open roll without an end write; only the playhead
break (`liveRec_syncPosition()` → `liveRec_endRoll(0)`) did.
`liveRec_newTake()` (REC off, overdub, erase, `rtk`) left the roll held, and
the later release wrote the end step after recording had stopped.

---

## 4. Design and plan

**Principle:** one roll engine on the master 96-PPQ tick for step rolls and
live (PERF/MIDI) rolls. A roll's rate is a fixed tick interval; its length
is a time window in ticks; step scale decides only how long a step's window
is, never the spacing of hits.

| Phase | Fixes | Content |
|---|---|---|
| P1 | F2, F3 | `seq_rollRemaining` → `seq_rollWindow` (u16 ticks); carry fires no hit and leaves `accum`; the window is replaced by the new step's (F-R); window close with fill < 100 ducks (duck arrays folded in); carry rule Q4; fill-100 parking; deferred-carry stretch; LiveRecord's rate-table copy deleted. |
| P2 | F4 | Per-track live engine in the Sequencer, serviced before the running gate (and from `seq_tick()` under external sync while stopped); foreground API; TIM3 → foreground mailbox; `bh_overlay` loses its timing fields; flam main on the tick (12 ticks); hits spaced from the press. |
| P3 | F1 | 15 overlay types = 6 non-roll + 9 step rates; type `6 + (stepIdx − 1)`, step index `type − 5`, no table; STEP labels reused; range 0..14; no migration. |
| P4 | F5, F6, F7, F8 | Per-track roll slots; per-slot span and end writes; every held roll a span (no slow/fast, no per-hit quantise); rhythm recorded; take boundary ends rolls without an end write. |

Order: P1 (sequencer only, hardware-checkable alone) → P2 + P3 (shared
tables) → P4 (depends on P1's carry rule and P2's hit reports). Binding
rules kept: the step reset rule (hit #1 and each carried step's renewal
reset; retriggers 2+ never), F-R (only the next step's trigger cuts a roll),
LiveRecord never in TIM3, the TIM3 order (rolls after the DDA).

---

## 5. Decisions

### 5.1 Plan questions (2026-10-24)

| # | Question | Decision |
|---|---|---|
| Q1 | PERF roll labels: STEP labels or an `r` prefix? | STEP labels (`1/8 8tr 16 16t 32 32t 64 64t 128`); the `v`/`fl` prefixes already mark non-roll types. (Recommendation.) |
| Q2 | Stored `perf_btn_*` 6..11 change meaning: migrate? | **User: no migration; old settings and Patterns need not survive.** |
| Q3 | Live roll phase: from the press or grid-locked? | From the press (matches an unquantised recording). (Recommendation.) |
| Q4 | Carry when rate and rhythm match but fill differs? | **User: yes, unless the first (previous) step's roll length is below 100 %.** |
| Q5 | Concurrent recorded rolls: all 7 tracks or N slots? | All 7 (matches S083 Q5). (Recommendation.) |
| Q6 | Flam main on the master tick too? | Yes; retires the last `PAR_BPM` ms timing. (Recommendation.) |
| Q7 | Record every roll as a span (retire F-L for rolls)? | **User: yes, while the button or MIDI input note is held.** Flams keep F-L. |
| Q8 | Record the `pv` rhythm into `rtm`? | Yes (recommendation; separable entries). Closes the S083 open decision. |
| Q9 | External sync, stopped, no clock? | Live rolls follow `seq_tempo` from `seq_tick()` while stopped; the clock keeps it updated; with no clock they keep the last measured tempo. (Recommendation.) |

### 5.2 Assessment fixes

| # | Decision | Source |
|---|---|---|
| A1 | Interpolate external clock ticks (option 1 of two; §8). | User approved the spike assessment's recommended actions, 2026-10-24 |
| A2 | A held roll released on a later step before its second hit takes the normal later-step end write. | same |
| A3 | Drain held overlay entries before `bh_overlayActive` changes. | same |
| A4 | Comment/spec nits (LiveRecord RAM comment; a self-citation in §6.7.4). | same |
| B1 | `buttonHandler_overlayReconfigure()` drains before judging an entry held. | User, 2026-10-26 ("make those 2 narrow, targeted fixes") |
| B2 | Schedule paths and branch names corrected. | same |

### 5.3 Other user instructions this session

- RAM: "mark the ram as approved" (2026-10-26) — the +87 B in §6.
- Add the spike directory to `.gitignore` so it is never committed (done
  while it sat inside the repository; it has since moved next to it).
- The two converter tasks: their instructions are in §12.1 and §13.1.

---

## 6. Resources (RAM policy record)

All entries SRAM1 `.bss`, static, runtime-only, never persisted, firmware
lifetime. Approved by the user 2026-10-26 (first acknowledged for the spike
build 2026-10-24).

| Owner | Change | Bytes |
|---|---|---:|
| `sequencer.c` step roll | `seq_rollRemaining[7]` u8 removed | −7 |
| `sequencer.c` step roll | `seq_rollWindow[7]` u16 added | +14 |
| `sequencer.c` step roll | `seq_rollDuckDelta/Delay/Active[7]` removed | −21 |
| `sequencer.c` live engine | `seq_liveMode/Rate/Accum/Pos/Note/Vel[7]` (6 × 7 u8) | +42 |
| `sequencer.c` live engine | `seq_liveRhythm` u8 | +1 |
| `sequencer.c` live mailbox | `seq_liveHitCount[7]` u8, `seq_liveFlamMask` u8, `seq_liveHitUs[7]` u32 | +36 |
| `sequencer.c` external clock | `seq_extTicksOwed` u8 (A1) | +1 |
| `buttonHandler.c` | `bh_overlay[7]` 8 B → 4 B per track | −28 |
| `LiveRecord.c` | `liveRec_roll[7]` × 8 B | +56 |
| `LiveRecord.c` | 11 B of single-slot roll scalars removed | −11 |
| `PatternStackService.c` staging | `pat_record_spec_t.rhythm` in 4 slots | +4 |
| | **Net (before alignment)** | **+87** |

Measured: `bss` 430,112 → 430,200 (+88 B; 1 B alignment). Stack:
`liveRec_trigger_t` +1 B per record, `pat_record_spec_t` +1 B per local
copy. The released bytes do not authorise other reuse.

---

## 7. Implementation (as built, 2026-10-26)

### 7.1 P1 — step roll engine (`sequencer.c`)

- **State block:** `SEQ_ROLL_RUN` (0x01: window open, retriggers fire) and
  `SEQ_ROLL_CARRY` (0x02: the roll's step had fill 100, the next step may
  carry it) are the values of `seq_rollActive[]`. CARRY without RUN =
  **parked**. `seq_rollWindow[7]` (u16) = master ticks left in which
  retriggers may fire. `seq_rollTickAccum`, `seq_rollVelPos`,
  `seq_rollLastRate/Rhythm/Fill`, `seq_rollStepVel/Note` keep their roles.
- **`seq_rollClearTrack(track)`** replaces the ten-line clear at the four
  reset sites: `seq_init()`, `seq_setTrackPlayedScene()` (and the choke
  partner of tracks 5/6, braced clause), `seq_selectActivePattern()`,
  `seq_setStepIndexToStart()`. Live overlay state is never cleared there.
- **`seq_rollCarries()`:** CARRY set && `sp->roll != 0` && same rate &&
  same rhythm. Fill is not compared.
- **`seq_rollBegin()`:** window = `(stepScale_ticksQ8(scale) × fill +
  12800) / 25600` (whole ticks, rounded from Q8.8). No carry: `accum = ppq`,
  hit #1 (reset 1), rhythm position 1. Carry: `voiceControl_resetOnly()`
  only (S081 §5.8 renewal, no hit). Either way: window replaced, flags =
  RUN | (fill ≥ 100 ? CARRY : 0), last rate/rhythm/fill and payload stored.
  Fill 0 gives a zero window: one hit, ducked on the same tick.
- **F-R cut** (in `seq_advanceTrackStep()`): a plain firing trigger clears
  `seq_rollActive` and the window (no retrigger, duck or carry follows).
- **Carry eligibility** (new, before `pat_isStepActive()` in
  `seq_advanceTrackStep()`): at every step event, CARRY is cleared unless
  this step will carry — `rec_fired` (bit 15 + condition), not
  `trigger_suppressed` (echo mark / live hold), not being erased
  (`seq_eraseFullMask | seq_eraseSpecialsMask`), and `seq_rollCarries()`.
  An open window keeps running to its end (an empty step does not cut it);
  a parked roll ends here.
- **Deferred-carry stretch** (where a step is deferred by shuffle and/or
  micro-timing): if the step will carry, `seq_rollWindow` is raised to
  `total_delay` (never lowered) and RUN is set, so hits between the
  boundary and the deferred fire are not lost. A muted deferred fire carries
  nothing; the roll parks at the stretched end and the next step event ends
  it.
- **`seq_processRollTicks()`:** RUN with window 0 → clear RUN; fill < 100
  → `instrumentManager_requestGateDuck()`; if nothing left, done. Parked →
  count the accumulator down to 0 and hold (phase only). Running → window−−;
  accumulator−− until 0, then fire (reset 0, `seq_rollComputeVelocity()`),
  `accum = ppq − 1`, rhythm position++. Runs after the DDA, so a carrying
  step renews the window before the window-end test on the same tick.
- Removed: `seq_rollRemaining[]`, the duck arrays and `seq_rollArmDuck()`.

Result: hits at `first hit + k × interval` across any number of steps, at
any step scale, until a window closes (fill < 100, or a non-carrying step
event after parking) or a trigger cuts it. A hit that falls due while parked
fires on the carry's tick (at most a 1-tick delay from a fractional step).

### 7.2 P2 — live overlay engine (`sequencer.c/.h`, `buttonHandler.c`)

**Sequencer side.**
- State: `SEQ_LIVE_IDLE/ROLL/FLAM`; `seq_liveMode[7]` (volatile),
  `seq_liveRate/Accum/Pos/Note/Vel[7]`, `seq_liveRhythm` (one byte for all
  tracks: every held entry follows the one overlay); mailbox
  `seq_liveHitCount[7]` (saturating 255), `seq_liveFlamMask` (bit N = flam
  main of track N fired), `seq_liveHitUs[7]` (TIM2 µs of the latest hit).
  Not cleared by Scene changes or transport resets (a held gesture belongs
  to the panel or MIDI).
- `seq_processLiveTicks()` (TIM3): per armed track, accumulator down to 0;
  a flam fires its main (BH_OVERLAY_VEL, reset 1) and goes idle, setting
  its mask bit; a roll fires a repeat (reset 0) at
  `seq_rollRhythmVelocity(seq_liveRhythm, vel, pos)`, `pos` wraps 255 → 16
  (256 mod 60, same phase), `accum = ppq − 1`, hit count++. Each hit stamps
  `seq_liveHitUs`.
- API (foreground; multi-byte writes under PRIMASK):
  `seq_liveRollStart(track, rate, note, vel, pos)` (caller already fired hit
  0; first repeat after `ppq − 1` whole ticks from the next tick, so the
  first gap is between `ppq − 1` and `ppq` ticks; clears the mailbox; rate 0
  stops), `seq_liveRollSetRate(track, rate)` (roll only; keeps the time
  since the last hit: `accum += new − old`, clamped 0..new−1; S081 F-B),
  `seq_liveFlamArm(track, note, vel, ticks)` (main `ticks` after the press,
  12 = 1/32), `seq_liveStop(track)` (single byte; mailbox kept for a final
  take), `seq_liveSetRhythm(rhythm)`, `seq_liveTakeHits(track, &stampUs,
  &flamMain)` (returns and clears the count and the flam bit).
- Call sites: `seq_processLiveTicks()` first in `seq_processSchedulerTick()`,
  **before** `if (!seq_running) return;` (internal sync running or stopped;
  external sync while running); `seq_tick()` under external sync while
  stopped (no clock reaches the scheduler then; `seq_tempo` follows the
  incoming clock via `sync_calcBpm()` / `trigger_applyPulseTempo()`).
- `seq_rollPpq()` stays private; comments of `seq_rollPpq()`,
  `seq_triggerVoice()` and `seq_rollRhythmVelocity()` updated.

**buttonHandler side.**
- `bh_overlay_track_t` = `heldMidiNote` (note + 1, 0 = panel/none), `note`
  (bit 7 = `def`), `count` (0 idle, 1 flam main pending, 0xFF held roll),
  `rate` (step index 1..9, reported with every roll hit).
- Defines: `BH_FLAM_TICKS 12`, `BH_OVERLAY_TYPE_FLAM_FIRST 4`,
  `BH_OVERLAY_TYPE_ROLL_FIRST 6`, `BH_OVERLAY_TYPE_MAX 14`.
- Removed: `buttonHandler_overlaySixteenthMs()`,
  `buttonHandler_overlayDivisionMs()`, both `rollDiv`/`rollIdx` tables, the
  `pos/interval/next` fields.
- `buttonHandler_overlayRhythm(which)`: `PAR_PERF_VEL_LT` for 1, else
  `PAR_PERF_VEL_RT`.
- `buttonHandler_overlayReport()` gains `rhythm` (sets `t.rhythm`).
- `buttonHandler_overlayDrain(track)`: if the entry is active, take the
  mailbox; a fired flam main → one FLAM_MAIN report, entry ends (count 0,
  heldMidiNote cleared); roll hits → one ROLL_HIT carrying `e->rate` and the
  active overlay's rhythm. Age = now − latest stamp (capped 65,535 µs), the
  S083 Q6 arrival-age path. Source MIDI when `heldMidiNote` is set.
- `buttonHandler_overlayDrainAll()` (A3): drain every track; called
  immediately before each assignment of `bh_overlayActive`
  (`buttonHandler_setOverlay()` for CC2, the BAR1 and BAR2 presses in
  `processPress()`, the event-ring overflow recovery in
  `buttonHandler_processEvents()`).
- `buttonHandler_overlayStart(track, which, note, heldMidiNote, source,
  ageUs)`: track range guard; a new gesture first ends the old one (stop,
  drain, count 0, `liveRec_rollRelease()` if it was a roll); `note` stored
  for every type; 0..3 single (127/100/64/32); 4..5 flam (grace 64 now,
  `seq_liveFlamArm(…, BH_OVERLAY_VEL, BH_FLAM_TICKS)`); 6..14 roll (hit 0 at
  BH_OVERLAY_VEL, ROLL_START with index and rhythm, `seq_liveSetRhythm()`,
  `seq_liveRollStart(…, pos 1)`).
- `buttonHandler_overlayEndTrack()`: stop, drain (so LiveRecord's "second
  hit" test sees the hits that sounded), count 0, note-offs,
  `liveRec_rollRelease(track)`.
- `buttonHandler_overlayReconfigure(which)` (B1 order): per track, drain
  first, then judge `midiHeld`/`panelHeld`/`oldRoll`; skip unheld; overlay
  off → stop, count 0, `liveRec_rollRelease()`; non-roll type → stop, count
  0, `liveRec_rollRateChange(track, 0, 0)`; roll type → publish rhythm,
  `seq_liveRollSetRate()` (was a roll) or `seq_liveRollStart(…, pos 1)`
  (single/flam → roll; a pending flam main is dropped), `e->rate`,
  `liveRec_rollRateChange(track, idx, rhythm)`.
- `buttonHandler_overlayTick()` (every `buttonHandler_tick()`): publish the
  active overlay's rhythm and drain every track; it no longer fires hits.
- Clamps `> 11u` → `> BH_OVERLAY_TYPE_MAX` in `overlayTrigger()` and
  `overlaySwitchHeld()`.

### 7.3 P3 — one roll list

- `perfBtnTypeNames` = count 15: `vmx v10 v64 v32 flm fls 1/8 8tr  16 16t
  32 32t  64 64t 128` (labels identical to `stepRollRateNames`).
- `menu_clampCellValue()`: `PAR_PERF_BTN_LT/RT` clamp 14.
- `menu_getS080StaticName()`: `menu_nameTable3(perfBtnTypeNames,
  perfBtnTypeNames[0][0], …)` (count from the table itself).
- `ParameterArray.h`, `stepRollRateNames` and held-type comments updated.
- `filesystem.c`: no change. `perf_btn_*` are read unclamped and written as
  numbers; an out-of-range value is type 0 in `overlayStart()` and clamped
  by the menu on the first edit. No migration (Q2).

### 7.4 P4 — LiveRecord, PatternData, MidiNotes

- **`liveRec_roll_t liveRec_roll[7]`** (8 B): `flags` (`LIVEREC_ROLL_HELD`
  0x01, `_SPAN` 0x02 = playhead has reached the start step, `_NOTE_DEF`
  0x04), `step`, `index`, `startDelay`, `note`, `vel`, `hits`, `rhythm`.
  Replaces `liveRec_rollHeld/Step/Index/StartDelay/Span/Note/NoteDefault/
  Vel/Track/Hits/PendingIdx`. `liveRec_rollPpqTicks()` deleted.
- **`liveRec_triggerIn()` roll branch:** a ROLL_HIT on a track with an open
  roll only counts (`hits`, saturating). A ROLL_START, or a ROLL_HIT with no
  open roll (REC armed mid-roll, a new take, a single/flam → roll switch),
  opens the roll at this hit: a held roll on that track is first ended with
  its end write; ring synced; live hold on; flags HELD | (SPAN if `pl.rel ≤
  0`) | NOTE_DEF; start step written with rate, fill 100 and rhythm. Roll 0
  → plain single.
- **Span writer** (`liveRec_drainRing()`): per ring entry, if that track's
  roll is HELD and SPAN and the ended step is not the start step, write it
  with timing 0, the rate, fill 100, the rhythm and `fil`. LC-8: a
  forward-quantised start sets SPAN at its own ring entry.
- **`liveRec_endRoll(track, writeEnd)`** (per track) and
  `liveRec_endAllRolls(writeEnd)`. With `writeEnd` and the transport
  running: capture, sync the ring, place the release, then:
  - start step not yet reached (no SPAN), or release on the start step with
    no second hit → the start step becomes a plain trigger at its start
    delay (A2: only while no span can exist);
  - release on the start step after the second hit → start step keeps its
    delay, fill = release delay − start delay (1..100);
  - release on a later step → that step at timing 0, fill = release delay
    (1..99), rate and rhythm kept.
  Then the live hold is lifted and the entry cleared.
- **`liveRec_newTake()`** ends every roll **without** an end write (F8);
  `liveRec_syncPosition()` calls `liveRec_endAllRolls(0)`; `liveRec_init()`
  clears the table.
- **`liveRec_rollRateChange(track, rollIndex, rhythm)`:** 0 → end that
  track's roll with its end write; otherwise an open roll takes the new rate
  and rhythm (span and end writes use them, F-C); no open roll → nothing (the
  next ROLL_HIT opens one). **`liveRec_rollRelease(track)`** ends that
  track's roll.
- `liveRec_writeStep()` and `liveRec_commitHit()` gain `rhythm` (0 for non-
  roll writes; commitHit passes it only with a roll).
- `liveRec_trigger_t.rhythm` (LiveRecord.h); hit-kind comments updated
  (`FLAM_MAIN` fires in TIM3, reported by the drain).
- `pat_record_spec_t.rhythm`; `pat_recordStep()` stores the rhythm only with
  a roll and only when nonzero, clears it otherwise (the old unconditional
  "rhythm is never recorded" clear is gone).
- `MidiNotes.c`: `lr.rhythm = 0` for a plain MIDI note (the struct is built
  field by field).

### 7.5 Build trail (DEV link, `make all && make img`)

| Point | `text` | `data` | `bss` | Note |
|---|---:|---:|---:|---|
| S083 close (`59406a4`) | 608,744 | 424 | 430,112 | baseline |
| P1 | 608,736 | 424 | 430,096 | −16 (estimate −14, alignment) |
| P2 + P3 (+ P4-17..19 and the interim 3-argument `liveRec_rollRateChange()`) | 609,336 | 424 | 430,152 | +56 over P1 |
| P4 (final) | 609,288 | 424 | 430,200 | +88 vs S083; image payload 609,712 B |
| B1 fix | 609,288 | 424 | 430,200 | reorder only |

The image file is 609,728 B (payload + 16 B header); a rebuild reproduces
it byte for byte. Warnings: none from a changed line; pre-existing
`PatternData.c:201` (packed member), `EuklidGenerator.c:54` (sign compare)
and the `filesystem.c` unused functions remain. The P2 build had to take the
LiveRecord prototype and struct field with it, plus an interim
`(void)rhythm` body on the old `liveRec_rollRateChange()`; P4 replaced it.

---

## 8. External clock tick interpolation (A1)

**Problem.** Under external sync the scheduler ran all ticks of a clock
period back to back inside one ISR pass: `seq_resetDeltaAndTick()` (MIDI
clock via `sync_tickTimestamp()`) ran `SEQ_INTERNAL_TICKS_PER_MIDI_CLOCK` =
4; `seq_triggerNextMasterStep()` (jack pulse via
`trigger_handleClockEvent()`) ran `stepSize × 3` = 24 at `PRE_4_PPQ`. Every
hit inside a period fired at one instant. Under MIDI clock, multiples of 4
ticks stayed even, `64` (6 ticks) alternated 1- and 2-clock gaps (±25 %),
`128` (3 ticks) fired two hits in one burst every third clock (the second
replaced the first in the same audio block: lost hits). Under 4-PPQ pulse
sync every roll finer than 1/16, shuffle and micro-timing collapsed into one
burst per pulse. Pre-existing for step rolls since S081 (96-PPQ scheduler);
new for overlay rolls once P2 moved them onto the tick. Internal sync was
never affected.

**Fix.**
- `seq_extTicksOwed` (volatile u8): ticks of the current period not yet run.
- `seq_extClockTicks(ticks)`: run every tick still owed (catch-up when the
  clock outran the estimate), run the edge's tick, leave `ticks − 1` owed,
  re-anchor the tempo timer (`seq_lastTick = systick_ticks;
  seq_calcDeltaT(seq_tempo)`). Both clock entry points call it.
- `seq_tick()`: internal sync → drop owed ticks, run the scheduler; external
  sync stopped → live ticks only (P2/Q9); external sync running → run one
  owed tick per tempo-timer period.
- Cleared by `seq_setRunningEx()`, `seq_continueRunning()` and
  `seq_resetToPatternStart()`; deliberately **not** by
  `seq_resetStepScheduler()`, which also runs at the master-boundary Pattern
  switch inside a tick, where the period's remaining ticks must still run.

**Properties.** Every period still runs exactly the same number of ticks;
only their timing changes. All entry points run inside `TIM3_IRQHandler()`
(`midiTransport_processRealtime()` → `triggerJacks_tick()` → `seq_tick()`),
so there is one writer context. `systick_ticks` is 0.25 ms; the timer
carries the remainder. A last interpolated tick is caught up at the next
edge only if the estimate is more than ~4 % (pulse) or ~33 % (MIDI) slow.
LiveRecord placement gains real sub-clock periods (`seq_ppqPeriodUs` was
near 0 for burst ticks). The `triggerJacks.c` comment "derives intermediate
substeps from BPM until the next master pulse arrives" is now accurate.

Rejected alternative: always service the live engine from `seq_tick()`
under external sync (fixes only live rolls; loses phase lock with step rolls
and recordings).

---

## 9. Verification

### 9.1 Host check H1 (required by the S084 gate; passed)

A scratch harness (session scratchpad, not installed) extracts the real
`seq_rollPpq/RhythmVelocity/ComputeVelocity/Carries/Begin/ClearTrack` and
`seq_processRollTicks()` from each tree, with stubs for `seq_triggerVoice()`
(prints tick and reset), `voiceControl_resetOnly()`,
`instrumentManager_requestGateDuck()` (prints "duck") and
`stepScale_ticksQ8()` (fixed 24-tick step). The driver calls
`seq_rollBegin()` at each step boundary before `seq_processRollTicks()` on
the same tick, and applies the carry-eligibility rule between steps (new
code only). It counts only because the base build reproduces the defect.

| Case (24-tick step) | `59406a4` | S084 |
|---|---|---|
| two steps, roll `32`, fill 100 | 0, 12, 35 | 0, 12, 24, 36 (nothing at 48) |
| two steps, roll `64`, fill 100 | 0 6 12 18, 29 35 41 | every 6 from 0 to 42 |
| fill 50 then fill 100, roll `32` | 0, duck 12, 24 (reset), 36 | 0, duck 12, 24 (reset), 36 |
| roll, empty, roll (`32`) | 0, 12, **59** (stale carry) | 0, 12, 48 (reset), 60 |

Run by the implementer and re-run independently at the assessment; identical
results. Extra host sanity checks by the implementer: a 12-tick live roll
started at `accum = ppq − 1` repeats at 11, 23, 35; a 12-tick flam fires its
main at 11 with reset 1 and sets the mask bit.

### 9.2 Hardware matrix V1–V7 (pending; user)

Each at 120 BPM internal and on an external MIDI clock at a tempo different
from `bpm`.

| # | Check | Expect |
|---|---|---|
| V1 | Two adjacent STEP steps, roll `32`, fill 100, `scl` 1/16, 1/8, 1/32 and a triplet scale; then with shuffle on, and with micro-timing on the second step. | Even roll, no gap or late hit at any boundary; rhythm continues; equally even under MIDI clock. |
| V1b | Same, first step at fill 50. | First roll ducks at half the step; the second step starts a fresh roll (Q4). |
| V1c | Roll step, empty step, same roll step. | The roll ends at its window; the third step starts fresh. |
| V2 | Track 1 STEP roll `16t` over 4 steps; track 2 the same rate held from PERF. All 9 PERF rates and both flams. | No phasing; every PERF type at its STEP rate; labels match; under MIDI clock `64`/`128` even. |
| V3 | Hold each PERF rate for 2+ steps on tracks at `scl` 1/16 and 1/8, `qnt off` and `qnt 16`, with `pv` set. A2 case: on a 1/16 track, REC on, tap overlay `1/8` ~150 ms across one step boundary. | Playback at the played rate and velocity shape, no hiccup on the release step, roll ends at the release; the `1/8` tap replays as exactly one hit. |
| V4 | Rolls on two tracks while recording, released in either order; a release after REC off. | Both recorded; no stuck live hold; nothing written after REC off (F8). |
| V5 | V2 and V3 with MIDI notes under a CC2 overlay; overlay switch (BAR/CC2) while a roll is held with different `pv<`/`pv>`; B1: switch to a roll type repeatedly while tapping MIDI notes under a flam overlay. | As V2/V3; hits before the switch keep the old rhythm (A3); every roll stops at its note-off (B1). |
| V6 | Transport stopped: PERF rolls with internal sync, and with external sync while the clock runs. Running under 4-PPQ pulse sync: every roll rate, shuffle, micro-timing. | Repeats at the internal / external tempo; under pulse sync everything even within each pulse (A1). |
| V7 | External sync: start, stop, Continue repeatedly with a step roll and a held PERF roll. | No extra or missing tick at the first clock after a start or Continue. |

Also worth hearing: converted LXR1/LXR-02 Patterns with adjacent roll steps
(§12, §13) now carry evenly across steps (F2 fixed).

---

## 10. Assessments and fixes

### 10.1 Spike assessment (2026-10-24, of the first implementation)

Conformance clean; four findings, all approved and fixed (§5.2):
- **A1** (high for external sync): back-to-back external ticks (§8).
- **A2** (high for recording): with every roll a span, a slow roll (e.g.
  `1/8` = 48 ticks on a 24-tick track) released after the span writer had
  covered step n+1 but before the second hit was rewritten only on the start
  step as a plain hit; step n+1 then replayed as a roll that was never
  played. Fix: the plain rewrite only when no span can exist; a later-step
  release takes the normal end write, and P1's window carries hit 0 to the
  release (its tail ducked there, as at any recorded roll end). Rejected:
  backfilling spans at the first `ROLL_HIT` (exact, but needs a per-track
  pending-step list: more RAM and code). A quantised start can still shift
  the window by one hit at the end, as for any recorded roll.
- **A3** (low): `bh_overlayActive` changed before the re-configure drained,
  so hits played under the old overlay were reported with the new `pv`
  rhythm; mattered only for the stored `rtm` of a span opened by such a hit.
- **A4** (nit): LiveRecord's "RAM about 14 B" comment (now 11 B); a
  self-citation in `PATTERN_DYNAMIC_STACK.md` §6.7.4.

Spike build figures (for comparison): after P1 `text=608,736`,
`bss=430,096`; after P1–P4 `text=609,128`, `bss=430,200` (+88 B: estimate
+86 B + 2 B alignment); after the A1–A4 fixes `text=609,288`, `bss=430,200`
(`seq_extTicksOwed` fitted existing padding), image 609,712 B — the same
final figures the re-implementation reached.

Differences between the spike's code and the original schedule, folded into
the revised schedule before re-implementation: braces in the
`seq_setTrackPlayedScene()` partner clear (indentation warning); loop-scoped
`uint8_t track` in `seq_selectActivePattern()`; the `track >= NUM_TRACKS`
guard in `buttonHandler_overlayStart()` (GCC bounded-array warning);
comments citing schedule entry IDs replaced by function names; the
`liveRec_syncRing()` comment (named the removed `liveRec_rollSpan`; had no
entry); the stale `FLAM_MAIN … (fires in overlayTick)` header line; §8
documentation rows the spike changed but the old list missed.

### 10.2 Re-implementation assessment (2026-10-26)

- **Conformance:** every P0-only source file has the same code as the
  reviewed spike; differences are comments (the intended corrections plus
  two improvements: the carry-renewal comment no longer describes the
  removed slot count; the F-R comment names `seq_rollWindow`). Shared files
  (`MenuText.h`, `menu.c`, `ParameterArray.h`) carry only P0 hunks; no S084
  voice hunk was copied into code or documents. No entry IDs in code.
- **Build** re-run: identical numbers, image byte-identical.
- **H1** re-run independently (§9.1).
- **Context check (A1):** single TIM3 writer context; no race.
- **Flam paths:** a quick panel release still drops a pending flam main
  (as in S083); a MIDI note-off keeps it (B-12).
- **B1** (low likelihood, stuck-roll symptom):
  `buttonHandler_overlayReconfigure()` read `midiHeld` before its drain. If
  a MIDI-held flam's main fired in TIM3 between the A3 drain-all and that
  track's loop pass (a few µs), the drain ended the entry (heldMidiNote
  cleared) but the stale `midiHeld` let a roll type start a roll with no
  held note: no note-off could end it, later re-configures skipped it, and
  only a new gesture on that track replaced it. Panel entries were not
  affected (`bh_overlayVoiceMask`). Not possible before S084 (the flam main
  fired in the same foreground loop).
- **B2** (nit): schedule still said `prime` and used the spike's old path.

### 10.3 Fixes (2026-10-26)

- **B1:** `buttonHandler_overlayReconfigure()` drains first, then computes
  `midiHeld`, `panelHeld`, `oldRoll`, then skips unheld entries. Comment
  extended. No RAM; build unchanged.
- **B2:** paths `../Helicase-dev-ph7-spike/` and base `91265e9c` on
  `dev-ph7-voice_expansion` in both S084 P0 documents.

---

## 11. The abandoned voice spike and the rollback

- **What:** after the first P0 implementation, S084 continued on the same
  tree with voice expansions P1–P5 (oscillator types `wav/pwm/smp/slp/wtb`,
  pwm families, run modes `rst/fre/itp`, render pool, stock waves, sample
  flash catalogue v2 and installer, Drum `mod` `snc`/`rng`, LFO one-shots,
  deferred instrument swap fixes, transient types, filter-drive types, the
  Mix FX stage, voice buffers, elastic CrumpBit). Spike commit `26fb7c3`,
  2026-10-26, "voice feature spike over budget", 180 files.
- **Why rolled back** (`../Helicase-dev-ph7-spike/S084_POST-MORTEM.md`):
  the transient, filter-drive and Mix FX additions do not fit the voice
  budget; selecting any buffer-using Mix/transient type or any filter-drive
  type other than `sft` locks the interface on hardware (cause not found);
  several additions were wrong in behaviour. The oscillator and
  modulation-mode work tested well on hardware.
- **Reference budget** (pre-S084 per voice per sample, static loop count,
  cycles ≈ instructions + 13 per float divide): transient (PCM click +
  mix-in) 32 instr ≈ 32 cycles; Mix FX (distortion curve + decimator) 30
  instr + 1 VDIV ≈ 43; filter drive (`sft`) 13 ≈ 13; **total ≈ 88 cycles**,
  the budget S086 works within.
- **The plan (post-mortem §3):**
  - **S084 (this session):** P0 only; gate = builds clean, P0 host and
    hardware checks pass, roll behaviour confirmed on hardware.
  - **S085:** oscillator and modulation-mode cherry-pick
    (`S085_OSC_MOD_CHERRYPICK.md` in the spike directory: every change by
    its S084 change ID, the source files to copy, what to strip from shared
    files); gate = clean build, oscillator host tests, loop costs vs the
    pre-S084 oscillator baseline, hardware check of every type, family, run
    mode, `snc`/`rng`, sample and wavetable install, old kits loading.
  - **S086:** the effects chain re-scoped as one swappable unit (a `chain`
    parameter in the transient section selects which transient kinds,
    filter-drive types and Mix FX types are available together, each chain
    inside 88 cycles); evaluation `S086_EFFECTS_CHAIN_POST_MORTEM_EVAL.md`;
    also the second filter-drive parameter
    (`S084_P5_FILTER_DRIVE_TYPE_REMEDIATION.md`) and the deferred
    architecture items (`S084_P4_VOICE_ARCHITECTURE_REMEDIATION.md`); the
    hardware freeze must be explained first, on the spike build, with the
    DEV fault capture and render-overrun counter (P4 §2).
- **Unassigned spike work** (decide whether and when): LFO one-shot waves,
  Scene-activation fire and the `rnd` 0..1 fix; the deferred instrument
  swap (real level, real-time bound, declick, preview on the shared swap);
  DEV swap trace stage `v`; the elastic Effect contract, CrumpBit
  compaction, FxBuffer positions and leases (only if S086 keeps voice
  buffers).
- **Working rules for S085/S086:** start from the rolled-back tree, the
  spike is reference only; copy only the hunks the session document names;
  model first (Python or host simulation), then the cost probe, then the
  code, for any new DSP; a test counts only if it fails on a deliberately
  broken version; costs measured on the compiled firmware (`armcheck`); RAM
  needs explicit approval; descriptive naming.
- The spike directory's documents (`S084_P1..P5_*.md`, post-mortem, S085
  and S086 plans) are untracked files in the spike repository; keep the
  directory until S086 no longer needs it.

---

## 12. LXR1 converters (task closed 2026-10-22; host only)

### 12.1 Task and instructions

`tools/lxr1_converters/`: Python scripts converting legacy Sonic Potions
LXR SD-card files into Helicase files. References given by the user:
`http://sonic-potions.com/public/SdCardImage.zip` (factory content) and
`https://github.com/SonicPotions/LXR` (parameter definitions; master
`dee4968`, 2015-10-23; clone it again to repeat any check).

| When | Instruction | Effect |
|---|---|---|
| Start | `.snd` → Kit subdirectories; `.pat` → library `.pat`; `.all`/`.prf` → Bank directories with the same Kit in every Scene and all Scenes' voice edit masks tied together. | Three scripts. |
| Mid-task | "don't worry about BPM or settings from the legacy files." | `.all` globals and `.prf` settings block skipped. |
| Mid-task | "if the .all or .prf has scene settings (audio out etc), those should be converted in the banks." | Into every `sceneset.scg`: voice `audio_out` and track `midi_note` (from the kit), track `midi_channel` (from `.all` globals). BPM, `pcr` and other true globals stay skipped. |
| Mid-task | Handoff log at the root, manual in the tool directory, scripts documented internally. | Done. |

The standing "no backward compatibility" rule concerns firmware storage;
these are explicitly requested host import tools and add no compatibility
code to the firmware.

### 12.2 Sources used

| Source | Used for |
|---|---|
| LXR `front/LxrAvr/Parameters.h` (master) | Kit byte offsets; the enum is embedded in `lxr1_common.py`, checked identical with the same evaluator. |
| `front/LxrAvr/Preset/presetManager.c` | `.snd`, `.pat`, `.all`, `.prf` layouts (`preset_saveDrumset`, `preset_writePatternData`, `preset_saveAll`). |
| `front/LxrAvr/Menu/Cc2Text.c` `modTargets[]` | Kit LFO/velocity targets (Nov 2013+ files). |
| `mainboard/.../Sequencer/sequencer.c/.h` | Step order (`seq_sendStepInfoToFront`), main-step masks, length/rotate nibbles, pattern change rules, random next, rotation start, shuffle. |
| `mainboard/.../MIDI/frontPanelParser.c` | Step automation numbering (`FRONT_SET_P1_DEST`: below 128 stored +1). |
| `front/AVR/Menu/menuPages.h`, `menu.h`, `menu.c` at `74f5b23`/`0568533` | The 2013 page-reference target encoding. |
| `front/AVR/Parameters.h` history | The three kit generations. |
| Helicase `Core/DSP/Instruments/*/*Parameters.c` | Key order, morphable rows, target validity, automation IDs (parsed at run time). |
| Helicase `filesystem.c`, `storageTypes.c`, `PatternData.h`, `SceneData.c`, `sequencer.c`, `menu.c`, `MenuText.h`; FILESYSTEM_SPEC, PATTERN_DYNAMIC_STACK, MIDI_SPEC | PAT4 header/blocks, `sceneset.scg` order/defaults, `bankset.bcg`, roll/timing/condition semantics, chains, value-table equality, output contracts. |

### 12.3 Findings that shaped the design

- **Three kit generations** (scored against factory data: `VOICE_DECIMATIONn`
  = 127, pans near centre): V0.1x (≤ `62ac6ef`, May 2013) 225 sound bytes,
  different enum — **refused** (`snd_target_encoding()`); V0.2x (`74f5b23`
  Jul 2013 … `0568533`) 221 bytes, final offsets, **page-reference
  targets**; `14c016e`/`37a338e` (Nov 2013) → master 228 bytes (final + 7
  MIDI notes), `modTargets[]` targets. The factory `P000..P029.SND` are 229
  bytes = 8 + 221 → V0.2x.
- **Page-reference targets:** decoding factory LFO targets as `modTargets`
  indices gave nonsense; in V0.2x the byte is `(sub-page << 3) | entry` into
  `menuPages[voice−1][sub-page]` (velocity on the owning voice, LFO on
  `PAR_VOICE_LFOn`). `auto` picks by payload size; `--target-encoding`
  overrides it for V0.2x kits re-saved by later firmware (not detectable).
- **Upstream comment drift:** `PAR_BPM /*250*/` is 251, `PAR_BAR_RESET_MODE
  /*270*/` is 271 (`PAR_TRACK_ROTATION` inserted later); assertions pin the
  real values.
- **Automation numbering:** below 128 stored as `PAR + 1`, 128+ unchanged;
  `Firmware.automation_id()` undoes it; 128 can only be
  `PAR_FILTER_DRIVE_1` (127 = `PAR_BANK_CHANGE`, never a target).
- **Sub-steps vs specials:** LXR1 main steps hold 8 sub-steps (1/128).
  Micro-timing `ceil(12.5 k)` lands exactly on sub-step k (delay `floor(24 ×
  t / 100)` ticks at 1/16). A roll is exact only when hits start at sub-step
  0 and cover the step evenly (spacing 1/2/4 → `128`/`64`/`32`, fill 100).
  Delayed or partial rolls spill extra hits (fill 100) or cut the decay
  (fill < 100): default `--partial-rolls first` keeps the first hit with a
  note; `fill` writes the ducked roll with the exact hit count.
- **Mappings:** values copy 1:1 (Helicase menu tables equal LXR1's:
  waveform, filter, audio out, LFO wave, sync, retrigger); `lfo_polarity` 0
  (`neg`, the original LFO law); `lfo_amount_2` 0; `lfo_target_voice_2`
  `self`; `lfo_target_param_2` off; velocity targets on another voice → off
  (the Helicase picker is voice-local); `srt`/MIDI note/selector targets →
  off; `[morph]` = `[params]`; Scene track Morph endpoints = Pattern values;
  probability → the condition buckets of `tools/convert_pattern_specials.py`;
  shuffle copied 1:1 to all tracks (curves differ); rotation baked into step
  order; chains `b0N` (N = change bar + 1; nearest of 16/32/64 above 8,
  noted) and `s0P`/`r0N` (LXR1 next 8..14 = `r2..r8`); LXR1 track MIDI note
  (fixed note sent) → Helicase note root (`nte`), 0 → `on` (closest meaning,
  MIDI_SPEC §2.2); Banks always get all eight Scenes so chain targets keep
  their numbers; PAT4 `pattern_change_bar`/`pattern_next` written 0.

### 12.4 What was built

```
tools/lxr1_converters/
  lxr1_common.py      LXR1 enum/modTargets/V0.2x page tables, readers,
                      Firmware descriptor loader + key-coverage check,
                      Kit/Instrument writer, pattern conversion, PAT4
                      builder + verifier, sceneset/bankset writers, slot
                      allocator, per-file error handling
  snd_to_kit.py       .snd  -> Kit/NNN <name>/
  pat_to_pattern.py   .pat  -> Pattern/NNN <name> <n>.pat (non-empty ones)
  all_to_bank.py      .all/.prf -> Bank/NNN <name>/ (Scenes 00..07)
  LXR1_CONVERTERS.md  manual and reference (every option and mapping table)
```

Common behaviour: inputs are files or directories; default output
`SD_CARD/<Library>`; free slots from `--start-slot` (never overwrite) or
`--keep-numbers` (+ `--overwrite`); `--dry-run`; `--firmware-root`; each
file converted fully before writing; bad files reported (`ERROR <file>:
…`) and skipped; exit 1 on any failure. Firmware coupling: descriptor
tables parsed at run time; start-up fails with the exact keys if the key map
and the firmware disagree (`Firmware._check_key_coverage()`); parsed counts
Drum 39, Snare 38, Cymbal 39, HiHat 39 (5 target-selector rows each). Every
PAT4 is re-decoded and verified (CRC32C, back-references, bitmap, specials,
automation) before it is written; import-time assertions guard the tables.

### 12.5 Test record (host; output into the scratchpad)

| Test | Result |
|---|---|
| `snd_to_kit.py --keep-numbers`, 30 factory kits | 30 converted, 0 failed; all detected as `pages`. |
| Slak (`P000`) vs the existing `SD_CARD/Kit/001 Slak` | All `[params]` identical; only `lfo_target_voice` `0` → `self` (0 was an invalid LXR1 voice). New keys neutral. |
| Factory LFO targets after page decoding | Consistent with each kit's LFO voice (Rollin, Moch to, Emott). `P023` velocity target pointed at its own `VEL_DEST` cell → off. |
| Synthetic `.pat` (plain hits, two automations incl. a ≥128 target, full 1/128 roll, sub-step-3 hit with prob 64 and note 70, flam, 1/32 roll, length 8 + rotation 2, sub-step without main step, unautomatable target, chain data) | Automation IDs 7 (`filter_freq`) and 9 (`filter_drive`); timing 38; condition 6; roll 9; roll 5; flam → first hit (default) or roll 9 fill 25 (`fill`); rotation moved step 2 to 0; empty patterns skipped. |
| Synthetic `.all`/`.prf` | 8 Scenes each; tied masks `00ff`; MIDI channels 1..7 from `.all`, `glb` for `.prf`; audio out `2,0,0,0,0,1` from Slak; chains `15/5` (b02 → s03) and `17/21` (b04 → r03). |
| Re-run after refactor | Kit, Pattern and Bank output byte-identical. |
| Occupied slot with `--keep-numbers` | Refused; replaced with `--overwrite`. |
| 233-byte (V0.1x) `.snd` | `ERROR … V0.1x layout`, others continue, exit 1. |
| `--dry-run` | Nothing written, notes printed. |

No unit-test file was added (the PAT4 self-verification runs on every write).
No real LXR1 `.pat`/`.all`/`.prf` was available: those paths are tested only
with synthetic files built from the LXR source layout.

### 12.6 Relation to existing tools

`tools/convert_legacy_kits.py` (original factory-kit generator for
`SD_CARD/Kit`) depends on legacy `PAR_*` names no longer in
`ParameterArray.h` and only upgrades an existing tree; the new converter
supersedes it for fresh conversion (not modified or deleted: user's call).
`tools/populate_scene_directory.py` writes obsolete Scene content (text
pattern stub, `.fx` v1 placeholder, `midi_note=63`); not used.

### 12.7 Known limitations

Sub-step rhythms other than evenly spaced whole-step rolls are approximated
and per-sub-step volumes inside a roll are lost; shuffle curves differ
(values copied); LXR1 bars follow track 1's length while Helicase chains
count 16-step bars; LXR1 fixed MIDI notes become note roots; kits re-saved
by newer LXR firmware may need `--target-encoding pages`; no real
`.pat`/`.all`/`.prf` sample was available.

### 12.8 Open questions (no action taken; carried)

Whether `--partial-rolls first` (default) or the ducked roll (`fill`) is
preferred; whether empty LXR1 patterns should still produce Bank Scenes
(yes today, to keep chain numbering); whether `tools/convert_legacy_kits.py`
should be retired.

### 12.9 Hardware checks (pending)

1. Convert the factory kits into a card's `Kit/` (or a test slot range),
   boot; the Load:Kit list shows them; Slak, Rollin, Emott load and sound
   like the existing converted kits.
2. LFO destinations on the VOICE LFO page (`vo1`/`ds1`): Rollin voice 2 and
   voice 4 → voice 3 filter frequency, Rollin voice 1 → voice 3 LFO amount;
   Moch to voice 1 → voice 2 filter frequency; Emott voice 5 → voice 4
   transient volume.
3. Convert a real LXR1 `.pat` and `.all`; load a Pattern and a Bank; check
   step positions, micro-timing, rolls, automation lanes, track lengths,
   edit-mask fan-out (edit an Instrument in Scene 00, look at Scene 03) and
   chains.

---

## 13. LXR-02 converters (task closed 2026-10-23; host only)

### 13.1 Task and instructions

`tools/lxr2_converters/`: Python scripts converting Erica Synths LXR-02
SD-card content into Helicase files, "the same conversion for all the
filetypes" as the LXR1 tools. Reference given by the user:
`https://github.com/erica-synths/lxr-desktop-app/` (LXR-02 firmware,
GPL-3.0; five commits, all 2026-10-23, cloned at HEAD `a43cd4f`).

| When | Instruction | Effect |
|---|---|---|
| Start | Converters for all LXR-02 formats like the LXR1 tools, from the desktop-app source, in `tools/lxr2_converters/`; LXR1-level documentation; a root handoff. | Four scripts, manual, handoff. |
| Review | Bank grouping A..D, cutting songs at 16 Scenes, shared writers staying in `lxr1_common.py`: "ok". | Kept. |
| Review | After the step-note pitch reference (63, unchanged) was distinguished from the LXR-02 per-track MIDI note setting: treat the untouched default (48 on every track) as Helicase `on`, keep deliberately set notes; close the task. | `scene_midi_settings()` and docs changed; task closed. |

LXR1 standing decisions carried over: BPM and true globals not converted;
Scene settings (audio out, track MIDI channel/note) go into
`sceneset.scg`.

### 13.2 Sources used

| Source | Used for |
|---|---|
| `Core/Inc/Parameters.h` | Kit offsets and globals; embedded as `LXR2_PARAM_ENUM_SOURCE`, checked identical. `NUM_KIT_SLOTS` 64. |
| `Core/Src/Preset/presetManager.c` | Every layout: `preset_saveDrumsetSD`/`preset_readDrumsetData` (`.snd`), `preset_writePatternData`/`preset_readPatternData`/`preset_loadPatternSD` (`.pat` `PatV01..03`), `preset_saveSongSD` (`.sng`), `preset_saveProject`/`preset_loadProject` (project folder, `prj.nfo`, `glo.cfg`), `preset_saveTmpKit`/`preset_saveTmpPat`, `preset_makeFileName*` (`NN-NAME`, `pNNN`), `preset_init` (init-kit copy), `preset_loadDefaultGlobals`. |
| `Core/Inc/Preset/SeqStep.h`, `Core/Inc/Soundengine/Sequencer/sequencer.h` | `Step` (7 B), `StepTiming` bitfield, `NUM_STEPS` 64, `NUM_BARS` 4, `MAX_SONG_LENGTH` 64, `SONG_END`/`SONG_LOOP`, `PatternSet`. |
| `Core/Src/Soundengine/Sequencer/sequencer.c` | Playback: main-step bit only (`seq_intIsStepActive` returns 1), probability `rand(1..100) <= prob`, `getShiftValue`, `flamIdxToPattern`, `seq_getShiftFlamPattern`/`seq_processFlam`, length 0 = 16 and the 64 wrap, longest-track pattern advance, song END/LOOP, `seq_setPendingPatternActive` (kit autoload, track restart), `seq_resetNote`/`seq_clearTrack` defaults, `seq_triggerVoice` (automation of the current step). |
| `Core/Src/menu/Cc2Text.c` | `modTargets[]` (embedded as `LXR2_MOD_TARGETS`, checked identical), voice offsets. |
| `Core/Src/menu/menu.c` `parameter_dtypes[]`, `Core/Inc/menu/MenuText.h` | Value domains, table-by-table comparison with Helicase; 0-based slot display. |
| `CortexParser.c`, `DSPAudio/automationNode.c`, `MIDI/MidiParser.c`, `MidiMessages.h` | Automation destination numbering (+1 below 128), `NO_AUTOMATION` 0xFF, fixed-note MIDI receive. |
| `DSPAudio/Oscillator.c`, `DrumVoice.c` | Note pitch reference `baseNote − SEQ_DEFAULT_NOTE` (60). |
| `FxBus.c/.h` | FX types; `FX_OFF` passes the bus to the FX routing output. |
| `Core/Src/Preset/initKit.c` | Init kit (embedded as `LXR2_INIT_KIT`, checked identical). |
| Helicase `tools/lxr1_converters/lxr1_common.py` | Imported: writers, `Firmware`, key map, LXR1 `modTargets` (not modified). |
| Helicase `Oscillator.h`, `MidiPitch.h`, `MenuText.h`, `sequencer.c` (`seq_chainConditionWeight`, `seq_chainResolveNext`, `seq_chainAtRepetitionEnd`); FILESYSTEM_SPEC, PATTERN_DYNAMIC_STACK, MIDI_SPEC, EFFECTS_BUS_REFERENCE | Waveform list (5 = crash), `MIDI_DEFAULT_TRIGGER_NOTE` 63, chain tokens and semantics, output contracts. |

### 13.3 Findings that shaped the design

- **Storage model:** no `.all`/`.prf`. A project folder `/projNN` holds 64
  kit, 64 pattern and 64 song slots as `NN-NAME.ext` files plus `prj.nfo`
  and `glo.cfg`; a `.pat` holds **one** pattern of 7 × 64 steps (4 bars ×
  16), no sub-steps; songs are pattern-number lists. Hence project → Banks
  and a separate song script. Unsaved edits live in `/tmp` (repeats the last
  project's slots) and are read only when named.
- **Shared ancestry:** Helicase ports LXR 0.37; the LXR-02 grew from the
  same code. Offsets 0..220 agree (only NRPN placeholder names differ); the
  LXR-02 leaves 221..227 unused (MIDI notes moved to the globals) and
  appends PWM × 6, the FX block and a morph-kit byte (247 sound bytes).
  `lxr1_kit_image()` copies 0..220, fixes waveforms, re-encodes targets and
  runs the unchanged LXR1 kit pipeline. Whether the LXR-02's own DSP makes
  the same value sound different was **not** examined.
- **Comment drift:** after offset 220 (`PAR_ROLL /*228*/` is 247, `PAR_BPM
  /*250*/` is 272); the enum is evaluated and asserted.
- **Value domains:** waveform 5 `PWM` (Helicase 5 = `Cym`) → `Rec` (3),
  pulse width noted; audio out 6 (FX bus) → `PAR_FX_ROUTING` (exact with FX
  off); step note pitch = note − 60 vs Helicase note − 63 → note + 3;
  probability percent (`rand(1..100) <= p`) → nearest condition bucket,
  100 = always, 0 = never (step left out), 96..99 → r95; retrigger subset
  unchanged; FX types `off drv rng cmp del` not converted (note).
- **Flam and shift are exact:** `flamIdxToPattern()` spreads ×2/×3/×4/×6/×8
  hits over 24 ticks = spacing 12/8/6/4/3 = rolls `32`/`32t`/`64`/`64t`/`128`
  (fill 100). A shift unit is 3 ticks; micro-timing `ceil(12.5 u)` gives
  exactly `3u`. A negative shift plays `3u` ticks early (in the previous
  step): moved there with timing `8 − u` when that step is free, moving
  whole runs together (a fully shifted track rotates by one step exactly);
  a run blocked by a played step falls back to on-beat. Remaining
  inexactness: a forward-shifted flam spilling into a step with its own hit
  (the LXR-02 interleaves; a Helicase roll stops at the next trigger) —
  reported.
- **Automation is the played step's:** `seq_triggerVoice()` applies the
  automation of the step whose time it is, so a moved hit takes the
  automation at its new position (reproduces the LXR-02). The LXR-02
  applies automation only on triggers, Helicase also on empty steps, so
  automation is written only where a hit plays.
- **Kits, pattern mode, edit masks:** pattern mode `Aut` (default) loads the
  pattern's kit; `off` keeps the kit (kit 00 after project load). Each Scene
  embeds that kit; Scenes playing the same kit slot share a VOICE edit mask
  (editing a kit on the LXR-02 affects every pattern using it). Groups
  partition the Scenes, so masks stay mutually exclusive.
- **Songs → `xNN` chains:** the LXR-02 advances at the end of the pattern's
  longest track; `xNN` fires at the end of the Nth repetition (longest track
  × scale). Repeats merge (`x01..x08`, `x16`, `x32`, `x64`; other counts
  split); `nx1` → next Scene; `stp` ends; `s01` loops. Difference: the
  LXR-02 restarts all tracks at every pattern change; Helicase does so on a
  chain fire only with `pcr` on (global, not written; noted per song when
  lengths differ).
- **Untouched content skipped by default:** init-kit slots (a new project
  fills all 64; `preset_init()` copies parameters 0..238) and song 00 (`00,
  end`) — `--keep-init`, `--keep-default`; empty patterns — `--keep-empty`.
- **Default track MIDI notes (user decision):** `glo.cfg` bytes 22..28
  (`PAR_MIDI_NOTE1..7`) make a track send that note and answer only to it; a
  new project sets 48 on all seven. Converted literally only track 1 would
  receive notes (MIDI_SPEC §4.1). When all seven are 48, or `glo.cfg` is
  missing, `scene_midi_settings()` writes `midi_note` 0 (`on`) on every
  track; other settings are converted as stored; channels always copied.

### 13.4 What was built

```
tools/lxr2_converters/
  lxr2_common.py      LXR-02 enum/modTargets/init-kit tables + self-checks,
                      input/project discovery, readers (.snd, .pat V1..V3,
                      .sng, glo.cfg, prj.nfo, tmp buffers), kit translation
                      to the LXR1 layout, audio-out/MIDI Scene settings,
                      pattern conversion (flam, shift, probability,
                      automation), song planning and chains, Bank writer
                      (per-kit edit masks); imports the Helicase writers
                      from ../lxr1_converters/lxr1_common.py
  snd_to_kit.py       .snd -> Kit/NNN <name>/
  pat_to_pattern.py   .pat -> Pattern/NNN <name>.pat
  prj_to_bank.py      projNN/ -> Bank/NNN <project> A..D/ (patterns
                      00-15/16-31/32-47/48-63)
  sng_to_bank.py      .sng -> Bank/NNN <song>/ (chained; at most 16 Scenes)
  LXR2_CONVERTERS.md  manual and reference
```

Common behaviour as LXR1 (inputs: files, project folders or a card root).
`lxr1_common.py` was **not** modified; the LXR1 converters' output is
unchanged. It is a runtime dependency (imported through `sys.path`):
editing it changes both converter sets — re-run both test records.

### 13.5 Test record (host; synthetic card from an independent writer mirroring `presetManager.c`; generator `make_lxr2_card.py` and dump helper `decode_pat4.py` stayed in the scratchpad)

| Test | Result |
|---|---|
| Embedded tables vs upstream `a43cd4f` | Enum (306 names), `modTargets` (229), init kit (254 B): identical. |
| File sizes read | `.snd` 255, PatV03 3,663, PatV02 3,215, PatV01 3,208, `.sng` 72, `glo.cfg` 30, `tmp.snd` 248, `tmp.pat` 3,650. |
| `snd_to_kit.py --keep-numbers` + `tmp.snd` | Init kit skipped; `01-Boom` → `001 Boom`; `tmp.snd` → `000 Tmp 01`; all 191 mapped values (six slots) equal the source; PWM wave → 3; LFO 1 (voice 3 `FILTER_FREQ_3`) → voice 3 token 7; velocity 1 (`VELOD1`) → token 12; PWM and FX targets → 255 with notes; srt and FX notes; re-run refused the occupied slot. |
| `pat_to_pattern.py` | prob 50 → `r50`; flam ×3 → roll 6; shift +2 → timing 25; shift −1 → previous step, timing 88; two −2 shifts after a free step → both moved, timing 75; −3 after a played step → on the beat (note); prob 0 left out (note); automation IDs 7 and 9; FX target dropped (note); note 72 → 75, 125 → 127 (note); flam ×2 + shift +5 → roll 5, timing 63, next hit → cut note; length-4 track all −1 → rotated exactly; lengths 16/12/16/64/16/16/4; PatV02/PatV01 noted; unnamed `p017` → `TestP 17`. |
| `prj_to_bank.py` | Bank A Scenes 00, 05; Bank B Scenes 01 (pattern 17), 04 (pattern 20); `active_scene` 0/1; audio out `1,3,0,0,0,0` (voice 1 FX → St2, note); MIDI `10,10,2,3,4,5,10` / `36,0,0,0,0,0,36` with the shared-note note; missing kit 02 → init kit (note); pattern mode `off` → every Scene kit 00; `--keep-empty` → 4 Banks × 16 Scenes; no `glo.cfg`/`prj.nfo` → defaults, folder name. |
| `sng_to_bank.py` | `00 00 00 05 20 20 loop` → 3 Scenes, chains `5/4`, `3/5`, `4/3`, masks `0005`; 9 × 00 + 05 end → `10/4`, `3/5`, `3/43`; 20 alternating entries → 16 Scenes, last `stp`, masks `5555`/`aaaa`; default and empty songs skipped; `--keep-default` converts the default; one missing-kit note per pattern; pcr note when lengths differ. |
| Errors | Truncated pattern (alone; inside a project → project fails), `PatV04`, `PatVxx`, a folder without slot files: each `ERROR`, batch continues, exit 1. |
| `--dry-run` (all four) | Nothing written. |
| Fuzz | 300 random patterns (V1..V3, half all-steps-on) and kits: no exception; every PAT4 verified; triggers = steps that play. |
| Re-run after final edits | Kit, Pattern and project-Bank output byte-identical; 31 PAT4 files pass size/magic/CRC32C. |
| Default-note follow-up | Notes `36,0,0,0,0,0,36`: unchanged, Banks byte-identical; all 48 → `midi_note=0,…`, channels kept; no `glo.cfg` → `midi_channel=1,…`, `midi_note=0,…`. |

No real LXR-02 content was available.

### 13.6 Known limitations

PWM (waveform and amount) has no Helicase equivalent (`Rec`); the LXR-02
kit FX (distortion, ring mod, compressor, delay) is not converted (voices
routed to it play dry on the FX output); a forward-shifted flam spilling
into a hit step is cut there and a negatively shifted run after a played
step plays on the beat; shuffle curves differ; fixed track notes become note
roots (coinciding deliberate notes on one channel go to the lowest track;
the untouched default 48 becomes `on`); songs ≤ 16 Scenes per Bank and need
`pcr` on when pattern lengths differ; a project's `/tmp` overlay is not
merged; audible DSP equivalence not checked.

### 13.7 Resolved questions (2026-10-23)

Default track notes → `on` (deliberate notes kept); project Banks group
patterns A..D in 16s; songs longer than 16 Scenes are cut (the 16th stops);
the shared writers stay in `lxr1_common.py` (moving them to a neutral module
would remove the cross-folder import, not done); skipping untouched init
kits, the default song and empty patterns by default (covered by the "ok").

### 13.8 Relation to existing tools

`tools/convert_legacy_kits.py` predates both converter sets; untouched.

### 13.9 Hardware checks (pending)

1. Convert a real LXR-02 card (or published factory content) with
   `--dry-run` first; read the notes.
2. Load a converted Kit and compare by ear with the LXR-02 (filters,
   envelopes, FM, LFO depth): the LXR-02 DSP was developed separately.
3. Load a converted Pattern: flams (rolls), shifted steps (micro-timing and
   moved hits), probabilities, automation lanes, track lengths up to 64.
4. Load a project Bank: Scene-to-pattern numbering, kit per Scene, edit-mask
   fan-out (edit a Scene, look at another that used the same LXR-02 kit),
   audio-out routes, MIDI settings.
5. Load a song Bank with `pcr` on: start Scene 00; repeats, order, the
   stop/loop at the end.

---

## 14. Affected files and API changes

### 14.1 Files (S084 P0, uncommitted on `dev-ph7-voice_expansion`)

| File | Change |
|---|---|
| `Core/Sequencer/sequencer.c` | §7.1, §7.2, §8. |
| `Core/Sequencer/sequencer.h` | `seq_rollRhythmVelocity()` callers comment; live engine prototypes and contract. |
| `Core/Hardware/frontPanel/buttonHandler.c` | §7.2 (incl. A3, B1), P3 clamps and comments. |
| `Core/Sequencer/LiveRecord.c/.h` | §7.4. |
| `Core/Bank/Scene/Pattern/PatternData.c/.h` | `pat_record_spec_t.rhythm`, `pat_recordStep()`. |
| `Core/Bank/Scene/Pattern/PatternStackService.c` | Staging RAM comment (+4 B). |
| `Core/Menu/MenuText.h`, `Core/Menu/menu.c` | P3 table, clamp, renderer, comments. |
| `Core/Bank/Scene/Preset/ParameterArray.h` | `PAR_PERF_BTN_*` comment. |
| `Core/MIDI/MidiNotes.c` | `lr.rhythm = 0u`. |
| `build/LXRV2_lxr02.img` | Rebuilt. |

### 14.2 API added, changed or removed

| API | Change |
|---|---|
| `seq_liveRollStart(track, rate, note, vel, pos)`, `seq_liveRollSetRate(track, rate)`, `seq_liveFlamArm(track, note, vel, ticks)`, `seq_liveStop(track)`, `seq_liveSetRhythm(rhythm)`, `seq_liveTakeHits(track, &stampUs, &flamMain)` | **New** (sequencer.h). |
| `seq_processLiveTicks()`, `seq_extClockTicks(ticks)`, `seq_rollClearTrack(track)` | New, static. |
| `seq_rollArmDuck()` | Removed (static). |
| `liveRec_rollRateChange(track, rollIndex)` | → `(track, rollIndex, rhythm)`. |
| `liveRec_rollRelease(track)` | Now acts on that track only. |
| `liveRec_trigger_t` | + `rhythm`. |
| `pat_record_spec_t` | + `rhythm`. |
| `liveRec_endRoll(writeEnd)` → `liveRec_endRoll(track, writeEnd)`, + `liveRec_endAllRolls()`; `liveRec_rollPpqTicks()` removed; `liveRec_writeStep()`/`liveRec_commitHit()` + `rhythm` | Static. |
| `buttonHandler_overlayDivisionMs()`, `buttonHandler_overlaySixteenthMs()` removed; `buttonHandler_overlayRhythm()`, `_overlayDrain()`, `_overlayDrainAll()` added; `buttonHandler_overlayReport()` + `rhythm` | Static. |
| `perf_btn_lt/rt` (`settings.cfg`, `PAR_PERF_BTN_LT/RT`) | Range 0..14; values 6..11 changed meaning (no migration). |

---

## 15. Accepted behaviours and known limits (not bugs)

- Live roll hits are spaced from the press (Q3), not grid-locked; a
  quantised recording starts on the grid, so live and recorded phase differ
  by the quantise offset.
- A hit that falls due while a fill-100 roll is parked fires on the carry's
  tick (≤ 1 tick late from a fractional step interval).
- In playback a held-roll rate switch is a different roll: the switch step
  restarts the roll at its boundary (F-R).
- A quantised recorded start can shift the window by one hit at the end, as
  for any recorded roll; an A2 early release ducks the single hit's tail at
  the release point.
- A quick panel release of a flam drops its pending main (as in S083); a
  MIDI flam ignores its note-off (B-12).
- Under external sync with no clock arriving while stopped, live rolls keep
  the last measured tempo (Q9).
- `seq_liveHitCount` saturates at 255 per drain (1 ms drain, never reached).
- Old `perf_btn_*` values 6..11 play different rates (no migration, Q2).

---

## 16. Specification reference updates (S084 close)

| Document | Update |
|---|---|
| `PATTERN_DYNAMIC_STACK.md` | S084 summary block; §6 roll engine (time window, carry rule, parking, deferred stretch, worked example, rate table with PERF column); live overlay engine; external clock interpolation; §6.2 reset rule (flam main, live repeats); §6.7.4 roll recording (per-track spans, A2 end rule, take boundary, rhythm); §6.7.9 invariants; staging (`patSvc_recordStep`). |
| `MODULE_INTERCHANGE_SPEC.md` | PERF performance surface: new "BAR overlay trigger engine" subsection (types, lifecycle, drain, reconfigure rules incl. A3/B1, contexts); overlay rows; sequencer API rows (live engine, clock interpolation, `seq_triggerVoice` reset); LiveRecord rows and state. |
| `MIDI_SPEC.md` | §2.3 `pb<`/`pb>` 15 types; §4.3 overlay timing; §8 clock interpolation; §14.2/§14.3 sequencer and buttonHandler integration rewritten; §15 TIM3 row; §17.2 regression rows; §18 RAM; §20 S084 decisions and limits. |
| `FILESYSTEM_SPEC.md` | `perf_btn_*` 0..14, no migration; import tools (LXR1/LXR-02 converters) section. |
| `STORAGE_SRAM_MANIFEST.md` | S084 build, per-owner rows (step roll, live engine, `seq_extTicksOwed`, LiveRecord, staging, `bh_overlay`). |
| `BANK_PRESET_ARCHITECTURE.md` | `PAR_PERF_BTN_LT/RT` 0..14. |

---

## 17. Carry-forward

1. **Hardware:** run V1–V7 (§9.2) on this build; S085 starts after it
   (post-mortem gate). Record results in the next session log.
2. **Commit state:** the S084 code and close-out documents are uncommitted
   on `dev-ph7-voice_expansion` (base `91265e9c`).
3. **Delete** (user's plan) the root task documents `S084_P0_ROLL_FIXUP.md`,
   `S084_P0_IMPLEMENTATION.md`, `LXR1_CONVERTERS_IMPLEMENTATION.md` and
   `LXR2_CONVERTERS_IMPLEMENTATION.md`; this log and the specifications
   carry them (the converter manuals stay in `tools/`).
4. **Next:** S085 oscillator and modulation-mode cherry-pick
   (`../Helicase-dev-ph7-spike/S085_OSC_MOD_CHERRYPICK.md`), then S086 the
   effects chain (§11). Keep the spike directory until then.
5. **Decide when convenient:** the unassigned spike items (§11); the LXR1
   converter questions (§12.8).
6. **Converter hardware checks** (§12.9, §13.9) when a card is prepared.
7. **`.gitignore`:** the `Helicase-dev-ph7-spike/` entry is unused since the
   spike moved next to the repository; remove it at any time.
8. **Still open from earlier sessions:** F5 (Scene 2 Case-3 invalidation),
   S082 P3 A2, S081 P3 A-1 (decision), the production-build re-measure,
   pre-existing warnings (`volatile/S078_WORKING_NOTES.md`).
