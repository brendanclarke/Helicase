# S084 Inter-Session Working Notes — carry-over after the Session 084 close

Last updated: 2026-10-26 (Session 084 closure; for Session 085). This file
was the S078 → S079 … S083 → S084 carry-over and has been rewritten in place
again. The full Session 084 record is
`knowledge_files/log_archive/084_SESSION_HANDOFF_LOG.md`. Authorities:
- `PATTERN_DYNAMIC_STACK.md` §6 (time-window roll engine, live overlay
  engine, TIM3 order, external clock interpolation), §6.7.4 (roll
  recording), §6.7.9;
- `MODULE_INTERCHANGE_SPEC.md` "PERF performance surface" → "BAR overlay
  trigger engine", the sequencer and LiveRecord rows;
- `MIDI_SPEC.md` (overlay types, clock, buttonHandler integration);
- `FILESYSTEM_SPEC.md` (`perf_btn_*` 0..14, "Import tools");
- `STORAGE_SRAM_MANIFEST.md` (S084 rows).

## Build metrics at S084 closure

| Metric | Value |
|---|---|
| text / data / bss (DEV config) | 609,288 / 424 / 430,200 |
| Flash payload | 609,712 B of 753,664 B; **headroom 143,952 B** (image file 609,728 B with header) |
| S084 SRAM | +88 B measured vs S083 (approved +87 B, user 2026-10-26) |
| Branch | `dev-ph7-voice_expansion`; base `91265e9c` (code = `59406a4`); S084 code and close-out documents uncommitted |
| Production build (`DEV_MODE_LOGGING 0`) | last measured at S075 F1: `text=516,688 data=408 bss=409,840`; re-measure (S081–S084 unmeasured) |
| Host checks | S084 H1 roll harness (scratchpad; log §9.1) PASS; `make -C tools/midi_test run` (S083) |

## Carry-over from Session 084 (roll fix-up)

1. **Hardware matrix V1–V7** (`084_SESSION_HANDOFF_LOG.md` §9.2) on the
   S084 build, at 120 BPM internal and on an external MIDI clock at another
   tempo: step-roll carry at several scales with shuffle and micro-timing
   (V1, V1b fill 50, V1c empty step); live vs step phasing and all 15
   overlay types (V2); recorded rolls incl. the A2 `1/8` tap (V3);
   concurrent recorded rolls and release after REC off (V4); MIDI under a
   CC2 overlay incl. the A3 rhythm and B1 flam → roll checks (V5); stopped
   transport and 4-PPQ pulse sync (V6); external start/stop/Continue (V7).
   **S085 starts only after these pass** (post-mortem gate).
2. **The spike** `../Helicase-dev-ph7-spike/` (own repository, `26fb7c3`,
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

1. Run the S084 hardware matrix V1–V7 and record the results.
2. Then Session 085: the oscillator and modulation-mode cherry-pick
   (`../Helicase-dev-ph7-spike/S085_OSC_MOD_CHERRYPICK.md`).
3. Open items worth a look when convenient: F5 (Scene 2 Case-3
   invalidation), S082 P3 A2, S081 P3 A-1 (decision), the production build
   re-measure.
