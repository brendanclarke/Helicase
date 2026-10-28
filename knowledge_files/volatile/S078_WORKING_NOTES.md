# S086 Inter-Session Working Notes — carry-over after the Session 086 close

Last updated: 2026-10-28 (Session 086 closure; for Session 087). This file
was the S078 → S079 … S085 → S086 carry-over and has been rewritten in place
again. The full Session 086 record is
`knowledge_files/log_archive/086_SESSION_HANDOFF_LOG.md`. The S087 startup
document is `S087_P0_OSC_TYPE_SAMPLES_WAVETABLES.md` (root).

**Authorities:**
- `INSTRUMENTS_DSP_REFERENCE.md`:
  - §6.1.1 (`pos`);
  - §6.1.2 (stock table waves and their generator);
  - §5 (engines);
  - §8 (costs);
  - §9.3 (adding a stock wave, oscillator work under the CPU rule);
  - §10 (pitfalls: state live across an inlined render, the Apply/Restore
    pairing, header constants in tests);
- `CPU_USE_DSP_AUDIT.md` items 41–44;
- `STORAGE_SRAM_MANIFEST.md` (S086 rows, ITCM table, flash option 4:
  sector 7);
- `FILESYSTEM_SPEC.md` (`osc1_position`, `wav` values);
- `MIDI_SPEC.md` §5.5;
- `OSC_INTERP_AUDIT.md`;
- `tools/dsp_test/DSP_TEST.md`.

## Build metrics at S086 closure

| Metric | Value |
|---|---|
| text / data / bss (DEV config) | 658,872 / 424 / 430,296 |
| Flash payload | 659,296 B of 753,664 B; **headroom 94,368 B** (the stock tables took 45,056 B) |
| ITCM | 8,512 / 16,384 B (7,872 free) |
| DTCM statics / FX-arena margin | 4,472 B / 3,712 B (unchanged) |
| RAM | no change (`OscInfo` 72 B; voices 596 / 420 / 476 / 484 B) |
| Branch | `dev-ph7-voice_expansion`. Commits `703941cb` (P2 + P3 plans), `e67879e2` (P3/P4 implementation). Uncommitted: the assessment fixes, comment re-pointing, close-out documents |
| Production build (`DEV_MODE_LOGGING 0`) | last measured at S075 F1; re-measure (S081–S086 unmeasured) |
| Host checks | `make -C tools/dsp_test osc_mod osc_stock osc_pos osc86_identity engine86_identity special_tags stock_tables elf86_p3_check elf86_mixer_check`: all pass (159 tag rows; mixer 0 grew) |

## Carry-over from Session 086 (stock waves, `pos`)

1. **Hardware: everything passed** (user, 2026-10-28): P3 H1–H10 including
   the six-slot G-C with an LFO on every `pos`, and P4 C1–C4.
   - **As built:** the bend cap is off (64); the step-automation glide at
     the hit is kept for `pos`.
2. **Next: Session 087** from `S087_P0_OSC_TYPE_SAMPLES_WAVETABLES.md`.
   - **Stages:**
     - A: step automation lands before its trigger (D6; needed for sample
       slicing);
     - B: the `typ` rows and modulator `pos` rows, with the reset decision
       for Drum FM / HiHat modulators;
     - C: catalogue v2 + installer + RAM release (≈ −16 KB); the spike's
       61,120 B stock block at the sample-flash floor is obsolete;
     - D: `smp`/`slp` (Q20.12, padding, fixed loop region, copies,
       sync-master rules) with `pos` D1–D5, D7–D9;
     - E: `wtb` (power-of-two frames, one-frame loop, blending through the
       interpolation pool).
   - **Answer its Q1–Q13 first;** freeze the user-sample ceilings (§1.4
     there) before stage D removes those loops.
3. **The sample `pos` decisions** (S087 P0 §6.5; log §15.2):

   | # | Recommendation |
   |---|---|
   | D1 | Q20.12, start = ((`pos` × len) >> 7) << 12, bend = d × len per sample |
   | D2 | fixed loop region; `pos` over the whole sample |
   | D3 | linear difference for samples |
   | D4 | clamp at 0; past the end ends the hit; no revival |
   | D5 | a time-based cap |
   | D6 | stage A |
   | D7 | re-anchor on type/source change |
   | D8 | step in `positionBendStep`, length at restore |
   | D9 | sync reset target = the `pos` start |
   | D10 | stage B |

4. **Binding rules added in S086** (`MEMORY.md`, "Oscillator work under the
   CPU rule"):
   - no state live across a voice render (keep it in `OscInfo`; free
     padding at offsets 34–35, 75);
   - `osc_positionBendApply()` / `osc_positionBendRestore()` always pair
     around one render;
   - host tests compile the header's constants;
   - new waveforms use the existing mechanism.
5. **Reference material:** the spike trees and the S085/S086_P1 planning
   documents were removed from the working tree at the close.
   - **Recover:** `git show e56bb957:S086_P1_OSC_TYPE_EXPANSION.md`,
     `git show b450d76a:S085_SAMPLE_INDEX_SPEEDUP.md`,
     `git show 1ea234ea:S085_OSC_MOD_CHERRYPICK.md`.
   - **Spike code:** `26fb7c3` / `4ad2f850` / `ef7d5445` in their own
     repositories (S087 P0 Q1).
   - **`.gitignore`** still names `Helicase-dev-ph7-spike/`; the pwm-spike
     gitlink deletion is unstaged.
6. **Disposable (user):** the five root S086 task documents (carried by the
   086 log and the specifications; code comments point to the log).
7. **Deferred S086 items:**
   - `ReA`/`TrA`/`SwA`: one generator line + ID/name/table each, +22,528 B
     each;
   - the Re3/Sw3 level difference (≈ 8 / 2 dB quieter than Rec/Saw);
   - LFO depth wrap on `pos` (clamped today);
   - `OscInfo.startPhase` / `Drum_setPhaseVoice()` clean-up;
   - the sector-7 flash option if flash runs out
     (`STORAGE_SRAM_MANIFEST.md` §3.6).
8. **Accepted S086 limits** (086 log §12):
   - old kits' `wav` 6+ remapped (no migration);
   - Drum slot-5 seed = Re3;
   - large `pos` jumps blip and alias for 0.7 ms;
   - `snc` is trigger-only for `pos`;
   - a slot reload may glide silently from 0.

## Carry-over from Session 085 (Drum `snc`/`rng`)

1. **Hardware:** P1 passed. The S085 ramp re-test was not reported
   separately; S086 H9 exercised Drum `fm`/`mix`/`rng`/`snc` on the final
   image with no issue reported.
2. **S087 → later: run modes** `rst fre itp` and the render pool (no longer
   S087; after the sample/wavetable types). **The free-run mode is to try
   "ramp always"** for the `rng` depth (user).
3. **Accepted S085 limits** (`085_SESSION_HANDOFF_LOG.md` §16):
   - **`snc`:**
     - the FM-page `wav` has no audible effect; crash and user-sample
       masters act as noise;
     - noise and user-sample slaves are unsynced;
     - the first reset after a hit varies hit to hit;
     - an LFO on the FM-page `wav` claims an interpolation unit.
   - **Drum FM oscillator:** a one-shot user sample there plays once after
     selection or power-up (no `modOsc` reset).
   - **The row:** MIDI 2 = `snc`, 3–127 = `rng`.
4. **Unassigned spike items** (S084, still open):
   - LFO one-shots and the Scene-activation fire / `rnd` fix;
   - the deferred instrument swap fixes and DEV swap trace `v`;
   - the elastic Effect contract;
   - the effects chain (after the oscillator stages).

## Carry-over from Session 084 (roll fix-up)

1. **Hardware matrix V1–V7** (still recorded as open at the S085 close;
   confirm closed or waived) (`084_SESSION_HANDOFF_LOG.md` §9.2) on the
   S084 build, at 120 BPM internal and on an external MIDI clock at another
   tempo: step-roll carry at several scales with shuffle and micro-timing
   (V1, V1b fill 50, V1c empty step); live vs step phasing and all 15
   overlay types (V2); recorded rolls incl. the A2 `1/8` tap (V3);
   concurrent recorded rolls and release after REC off (V4); MIDI under a
   CC2 overlay incl. the A3 rhythm and B1 flam → roll checks (V5); stopped
   transport and 4-PPQ pulse sync (V6); external start/stop/Continue (V7).
   **S085 starts only after these pass** (post-mortem gate).
2. **The spike** (now `Helicase-dev-ph7-spike/` in the repository root; own repository, `26fb7c3`,
   branch `dev-ph7-voice-spike`) is reference only. Its documents
   (`S084_POST-MORTEM.md`, `S085_OSC_MOD_CHERRYPICK.md`,
   `S086_EFFECTS_CHAIN_POST_MORTEM_EVAL.md`, `S084_P1..P5_*.md`) are
   untracked files there; keep the directory until S086 is done.
   - **S085:** oscillator and modulation-mode cherry-pick (types
     `wav/pwm/smp/slp/wtb`, pwm families, run modes `rst/fre/itp`, render
     pool, five stock waves, sample catalogue v2 + installer + natural
     order, `tools/sampleoptimize.py`, `tools/gen_stock_recipes.py`, plots,
     Drum `mod` `snc`/`rng`, menu formatting). Gate: clean build, oscillator
     host tests, loop costs vs the pre-S084 oscillator baseline, hardware
     check of every type/family/run mode/`snc`/`rng`, sample and wavetable
     install, old kits loading.
   - **S086:** transient + filter drive + Mix FX as one swappable `chain`
     inside the pre-S084 budget of ≈ 88 cycles per voice per sample
     (transient ≈ 32, Mix FX ≈ 43, filter drive `sft` ≈ 13); first explain
     the spike's hardware freeze (any buffer-using Mix/transient type or
     any drive type other than `sft` locks the interface) with the DEV
     fault capture and render-overrun counter; also the second
     filter-drive parameter and the deferred architecture items.
   - **Unassigned spike work** (decide): LFO one-shot waves, Scene
     activation fire and the `rnd` 0..1 fix; the deferred instrument swap
     fixes; DEV swap trace stage `v`; elastic Effect contract, CrumpBit
     compaction, FxBuffer positions/leases (only if S086 keeps voice
     buffers).
   - **Rules:** start from the rolled-back tree; copy only the hunks the
     session document names; model → cost probe → code for new DSP; a test
     counts only if it fails on a deliberately broken version; costs on the
     compiled firmware (`armcheck`); RAM needs explicit approval.
3. **Disposable (user's plan):** root `S084_P0_ROLL_FIXUP.md`,
   `S084_P0_IMPLEMENTATION.md`, `LXR1_CONVERTERS_IMPLEMENTATION.md`,
   `LXR2_CONVERTERS_IMPLEMENTATION.md`; the 084 log and the specifications
   carry them.
4. **Import tools** (host only): hardware checks pending
   (`084_SESSION_HANDOFF_LOG.md` §12.9, §13.9); LXR1 open questions
   (`--partial-rolls first` vs `fill`, empty patterns as Bank Scenes,
   retiring `tools/convert_legacy_kits.py`). Converted adjacent roll steps
   now carry evenly (F2 fixed) — worth hearing.
5. **Accepted S084 limits:** live roll hits spaced from the press, not
   grid-locked; a hit due while a roll is parked fires ≤ 1 tick late; a
   held-roll rate switch restarts the roll at the switch step in playback;
   a quick panel flam release drops the pending main; old `perf_btn_*`
   values 6..11 play different rates (no migration).
6. **`.gitignore`:** unused `Helicase-dev-ph7-spike/` entry; remove any
   time.

## Carry-over from Session 083 (MIDI)

1. **Hardware:** the final build "checks out" (user, 2026-10-22). The
   per-phase MIDI matrices are the regression list in
   `083_SESSION_HANDOFF_LOG.md` §11.2 and `MIDI_SPEC.md` §17.2 (now with the
   S084 overlay and clock rows).
2. **Testing setup:** a card without `midi_filt_rx/tx` keys has RXF/TXF =
   off (consumes and sends nothing) and MRT off. Set RXF/TXF to `all`.
3. **Card files:** `midilrn.cfg` is created by the first Learn;
   `settings.cfg` is 26 lines (`lines=26`); `learn=`/`l` lines from the
   first S083 build are ignored and dropped by the next settings write.
4. **Product decisions owed (only if the user asks):** RXF/TXF/MRT defaults
   (off today); whether a BAR overlay set in PERF should also shape MIDI
   notes outside PERF (it does: one overlay state); a published NRPN chart
   per instrument type and Effect type (R7). (Recording the `pv` rhythm was
   decided in S084: yes.)
5. **Accepted limits:** a downstream hanging note after a mid-note map
   change (R9); per-track playback writes and Learn type matching use the
   active Scene (R10); MIDI flams ignore note-off; MIDI-held rolls are not
   looper holds; panel early-return edit paths (target-selection cells, roll
   rhythm, held-step overlays) send no parameter TX; `midilrn.cfg` write
   retries once, then waits for the next Learn.
6. **Small debt:** the 7 pre-existing `filesystem.c` unused-function
   warnings (`filesystem_applyLegacy22Globals`, `_blockRename`,
   `_reportBootSubstep`, `_bootLoggingSetKitDetail`,
   `_applyStaleGlobalsFallback`, `_staleMetaPrefixLen`,
   `_metaHasStoredGlobalsLen`); `PatternData.c:201` packed-member and
   `EuklidGenerator.c:54` sign-compare warnings.
7. **Disposable:** the three root `S083_*.md` documents (if still present).

## Carry-over from Session 082

1. **Hardware testing (S082): passed** (user, 2026-10-21, S083 Q3). The
   per-part matrices are in `082_SESSION_HANDOFF_LOG.md` §11.2 for
   regression use.
2. **P3 A1 fixed in S083 (Q2); A2 still open** (one-liners, not applied):
   - **A1 (done):** a queued action whose target is unavailable is skipped whole (`menu_serviceDeferredSceneChange()`).
   - **A2:** repaint the Scene LEDs in the frozen-screen re-press guard of `menu_switchPage()`.
   - **A3:** SHIFT+REC opened from Load/Save mode shows no W1 blink. Leave it unless the user asks.
3. **F5 Scene 2 Case-3 invalidation:** root cause open. Capture protocol:
   1. spin the Kit page with Scene 2 active;
   2. exit Load/Save and wait for the drain;
   3. copy the card before rebooting, then reboot and copy again;
   4. look for `h` 0x02/0x01/0x04 on rows 17..32 and `Q` 0x08.
4. **First-boot freeze** (not reproduced): if it recurs, capture with `DEV_LOGGING_IWDG 1` (§3.5 of the log).
5. **Not built:**
   - P2-4, a DEV self-check comparing cached Scene indices with the active Scene at the switch and at the end;
   - P2-5, runtime Bank Load trace records (request, switch, per-child commit, completion, underrun count).

   The trace flush is suppressed during a command, so power-off before the next flush loses a load's records.
6. **Optional, only if asked:** run `FS_FAST_DRAIN_POLL_PASSES` inside the idle window. The user declined further load speed-ups (W5).
7. **Card preparation:** delete `/.hcnames`, `.hcnamtmp`, `.hcprms1` and `.hcprms2` after flashing S082+ firmware on an older card.
8. **Disposable:** the eight root `S082_*.md` documents and `SD_CARD_082_P0/`, `SD_CARD_082_P1/` (if still present).
9. **Session 081 close-out: done** (2026-10-21): `081_SESSION_HANDOFF_LOG.md`, the index entry, the spec updates and `MEMORY.md`. Its open items are in the next section.

## Carry-over from Session 081 (live record, ramps, loop while record, bar pulse)

Record: `081_SESSION_HANDOFF_LOG.md`. Code `2a0fd86` (unchanged by S082 for
LiveRecord). Authorities: `PATTERN_DYNAMIC_STACK.md` §6.1–§6.2b, §6.5, §6.7,
§12; `EFFECTS_BUS_REFERENCE.md` §6/§10/§11; `MODULE_INTERCHANGE_SPEC.md`
"Core/Sequencer/LiveRecord (S081)".

9a. **Hardware testing (S081): passed (user, 2026-10-21, S083 Q3).**
   - **Phase A behaviour first** (existing patterns sound different by
     design): step reset at the first trigger (delayed, rolled, carried,
     spilled steps), R12 menu-vs-automation per parameter class, R13 FX
     lock/overlay/menu incl. `sel`, roll rates by count (rolls were 4× fast),
     BAR overlay LED across modes, overlay switch while held.
   - **P1B F6 rows** (`081_SESSION_HANDOFF_LOG.md` §13.2) and **P1C C5 rows**
     1–10, plus the **C-1 boundary roll check** (rolls started/released on
     step boundaries at 1/32, 160 BPM: no overwritten step before the start,
     no gap before the end).
   - **P2 matrix** (log §6.8) including the **64-ramp CPU worst case** on the
     `cpu` widget (fallback: 16-per-pass round-robin); run
     `tools/convert_pattern_autosmooth.py --dry-run` on the card.
   - **D12 first-change checks** 1–6 (log §7.5).
   - **P3-C rows** 1–15 (log §8.7) and **G-1 checks** 1–4 (log §8.6).
   - **P4 rows** 1–10 (log §9.4), including minutes of playback across the
     65.5 s `time_sysTick` wrap.
   - F-Q: SHIFT Morph-view Effect/Scene edits count as menu edits — try.
9b. **Decisions owed:**
   - ~~P1 item 29~~ **decided 2026-10-21:** correct as implemented — with
     `pcr` off, Scene changes keep the tracks and the FX sequence aligned to
     the master clock.
   - **P3 A-1:** tapping the VOICE of a track already in the loop drops it on
     release (S080 D1). Wanted?
9c. **Open defects / limits (low):**
   - first ring entry after arming REC while running ends a stale step from
     an earlier take (harmless with first-change 1; with 0 a dirty value can
     land there);
   - a change made while a Pattern replacement is pending is lost;
   - P4 B-2 (stop + start inside one LED drain, external MIDI only);
   - ramp limits (one extra sample after a TIM3 transport reset; ramp ends a
     little early when live record suppresses B's trigger);
   - LiveRecord includes `menu.h` (recorded invariant deviation);
   - stale "held Morph latch" wording in two `EffectsManager.c` comments
     (~lines 1227, 1279).
9d. **Small debt:** add stage `W` (causes 1–5) to `tools/decode_devlogs.py`;
    re-measure the production build (S081 and S082 both unmeasured).
9e. **MIDI items deferred from S081: done in S083** (arrival stamps,
    real target IDs for `liveRec_paramIn()`, MIDI = menu edit incl. R12,
    CC1 on G unrecorded). Still deferred: step record while stopped,
    recording FX-sequence locks, automation `hold` reconciliation
    (SCOPING §4.3a).
9f. **Disposable:** the ten root `S081_*.md` documents (superseded by the
    081 log; the user deletes them).

## Carried from Sessions 078–080 (unchanged unless noted)

10. **S080 hardware matrix** (`080_SESSION_HANDOFF_LOG.md` §12) row by row: looper sync (T7.x, T8.x, T14.x), chaining (T2.x), queue actions (T15.x), fast blink (T10.x). S082 changed the fast blink to two slots and suspends chains during Bank loads.
11. **Deferred S080 features:**
    - reload from source for `rld`/`r+r`;
    - ~~live recording of overlay triggers~~ (done in S081);
    - `ens`/`enl` track-length-aware boundaries;
    - `fls` variable flam spacing;
    - edit lock during scene changeover.
12. **Accepted limits:**
    - S080: looper projection uses the current scale for the whole span; and so on (the single overlay repeat slot was lifted in S083: per-track table).
    - S079: special-flags byte full; pool capacity under extreme use; roll quantization; shuffle + micro-timing.
    - S078: track length 128 cannot be automated; a queued removal can leave a stale underline; automation is sticky until stop (since S081 a menu edit of that track parameter clears the override, R12).
13. **S079 P2 checklist** (4 items) and the P1 addendum fix 3 (7mu/!7m) still need on-device confirmation.
14. **STEP-page deferred-marker retry gap** (`menu_serviceRuntimeWidgets()` retry excludes SEQ_PAGE): offered, not applied.
15. **Open defects carried:**
    - Pattern Load fan-out `memcpy` tear;
    - O1 Settings Load per-voice Morph equalisation;
    - F4 trace ring drops during dirty bursts;
    - underline races;
    - Scene-namespace LFO/velocity tokens resolve through the active Scene.
16. **S074 items:**
    - the unexplained boot timeout;
    - the `cpu` widget with `cmp` on;
    - the CrumpBit minimum-share run;
    - BC11;
    - the saturator α;
    - stale comments.
17. **Stale tools and small debt:**
    - `decode_devlogs.py` misreads `pattrace.bin`;
    - Makefile echo / bare-`make` default goal;
    - duplicated comments;
    - unreachable Scene Save phases 33–36;
    - `AUTOSAVE_TRACE_RECORD_COUNT` still at 2,048.
18. **Deferred features:**
    - `/Effect/` browser (A35);
    - live record of FX-sequence locks (automation and Effect-parameter recording done in S081);
    - ~~the roll overhaul~~ (done in S084: time-window engine, live overlay engine);
    - Patgen/Euklid revert;
    - triplet scale mode;
    - one-shot LFOs (built in the abandoned spike; unassigned, see S084 item 2);
    - external MIDI sequencing tracks.

    MIDI mapping (A20) was done in S083.
19. **Still open from earlier sessions:**
    - Phase 5 hardware acceptance for S072 Steps 6–10;
    - slow Load type switching;
    - D-C1;
    - the LFO noise range;
    - the optional AutoSave mask guard;
    - the budget primitive extraction;
    - retest rows 3.10/3.14/3.15 and C1–C6.

## Next session recommended goal

1. Session 087 from `S087_P0_OSC_TYPE_SAMPLES_WAVETABLES.md`:
   - answer Q1–Q13 (Q1: the spike references);
   - take the step-0 baseline (`snapshot87.sh`, the frozen user-sample
     ceilings, the mixer hot-loop baseline);
   - then stage A (automation before trigger).
2. Open items worth a look when convenient:
   - confirm the S084 V1–V7 status;
   - F5 (Scene 2 Case-3 invalidation);
   - S082 P3 A2;
   - S081 P3 A-1 (decision);
   - the production build re-measure.
