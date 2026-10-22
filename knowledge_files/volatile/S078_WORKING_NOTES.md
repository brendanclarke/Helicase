# S083 Inter-Session Working Notes — carry-over after the Session 083 close

Last updated: 2026-10-22 (Session 083 closure; for Session 084). This file
was the S078 → S079 … S082 → S083 carry-over and has been rewritten in place
again. The full Session 083 record is
`knowledge_files/log_archive/083_SESSION_HANDOFF_LOG.md`. Authorities:
- `MIDI_SPEC.md` (complete MIDI use and code reference);
- `FILESYSTEM_SPEC.md` (`settings.cfg` 26 lines, `midilrn.cfg`, `sceneset.scg`
  track MIDI rows);
- `MODULE_INTERCHANGE_SPEC.md` ("Core/MIDI", PERF surface, SHIFT+REC);
- `DEV_MODES.md` (trace stage `m`, `MlWr`);
- `STORAGE_SRAM_MANIFEST.md` (S083 rows).

## Build metrics at S083 closure

| Metric | Value |
|---|---|
| text / data / bss (DEV config) | 608,744 / 424 / 430,112 |
| Flash payload | 608,848 B of 753,664 B; **headroom 144,816 B** |
| S083 SRAM | net about −816 B vs S082 (USB ring 2,048 → 768 B; Learn 528, overlay 56, NRPN 48, DIN stamps 128, TX 22, flags) |
| Branch | `dev-ph6-midi`; planning `f9aaeec`, implementation `d007027`, fixes and follow-ups `bbea23f`; final close-out doc edits after it |
| Production build (`DEV_MODE_LOGGING 0`) | last measured at S075 F1: `text=516,688 data=408 bss=409,840`; re-measure (S081–S083 unmeasured) |
| Host test | `make -C tools/midi_test run` → PASS |

## Carry-over from Session 083 (MIDI)

1. **Hardware:** the final build "checks out" (user, 2026-10-22). The
   per-phase MIDI matrices (A–G and the F-1..F-7 checks) were not reported
   row by row; they are the regression list in `083_SESSION_HANDOFF_LOG.md`
   §11.2 and `MIDI_SPEC.md` §17.2.
2. **Testing setup:** a card without `midi_filt_rx/tx` keys has RXF/TXF =
   off (consumes and sends nothing) and MRT off. Set RXF/TXF to `all`.
3. **Card files:** `midilrn.cfg` is created by the first Learn;
   `settings.cfg` is 26 lines (`lines=26`); `learn=`/`l` lines from the first
   S083 build are ignored and dropped by the next settings write.
4. **Product decisions owed (only if the user asks):**
   - RXF/TXF/MRT defaults (off today);
   - whether a BAR overlay set in PERF should also shape MIDI notes outside
     PERF (it does: one overlay state);
   - whether recorded overlay rolls should carry the `pv` rhythm (the S081
     rule says rhythm is never recorded);
   - a published NRPN chart per instrument type and Effect type (R7).
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
7. **Disposable:** the three root `S083_*.md` documents
   (`S083_P1_MIDI_IMPLEMENTATION_PLAN.md`, `S083_P1_IMPLEMENTATION.md`,
   `S083_P2_MIDI_TRACK_FIXUPS.md`); the user deletes them. Their content is
   in the 083 log and `MIDI_SPEC.md`.

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
    - the roll overhaul;
    - Patgen/Euklid revert;
    - triplet scale mode;
    - one-shot LFOs;
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

1. Pick the next `SCOPING_TARGETS.md` Phase 6 item (MIDI §6.1 and A20 are
   done).
2. Open items worth a look when convenient: F5 (Scene 2 Case-3
   invalidation), S082 P3 A2, S081 P3 A-1 (decision), the production build
   re-measure.
