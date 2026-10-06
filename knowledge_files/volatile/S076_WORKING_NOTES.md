# S077 Inter-Session Working Notes

Last updated: 2026-10-06 (Session 076 closure).

The full Session 076 record is
`knowledge_files/log_archive/076_SESSION_HANDOFF_LOG.md`. The copy/clear
reference is `knowledge_files/specification_reference/COPYCLEAR_UTILITIES.md`.

## Build metrics at S076 closure

| Metric | Value |
|---|---|
| text / data / bss (DEV config) | 535,976 / 416 / 426,744 |
| Flash payload | 536,392 B of 753,664 B; **headroom 217,272 B** |
| RAM growth from S075 | +32 B (28-byte `lfo_scene_handoff` struct + alignment) |
| Branch | `dev-ph6-cleanup`: S076 code changes uncommitted |
| Production build (`DEV_MODE_LOGGING 0`) | last measured at S075 F1: `text=516,688 data=408 bss=409,840`; re-measure |

## Carry-over from Session 076

1. **Hardware testing (top priority):** all four S076 parts (P1–P4) await
   hardware verification. Use `S077_RETEST_CHECKLIST.md` in the root
   directory. Key areas:
   - P1: automation override clears (Rule A per-parameter, Rule B per-Scene);
   - P2: LFO retrigger `scn` Scene-change phase handoff, phase offset
     scaling (values 0, 64, 127 should produce distinct LFO starting phases);
   - P3: `reset morph` and `copy morph` at track and Scene level, fan-out
     behavior, `reset fx morph` fan-out;
   - P4: `reload scene` from the PERF clear menu, bar chaselight in STEP
     mode, SHIFT+SELECT per-track pattern length.

2. **S075 carry-over (still open):**
   - the SHIFT+TRACK overlay follow-up re-test;
   - the combined case list in `COPYCLEAR_UTILITIES.md` §16 (buttons ×
     modes, pastes on a playing Pattern, nearly full pools, Scene-level
     copies with fan-out, names, suspension, regression);
   - F3 cases beyond the user's "seems ok" (LFO on Morph plus pitch
     automation, `Nvm` on the same step, MIDI CC to an automated parameter);
   - card preparation (delete `.hcprms1`/`.hcprms2` before first boot of
     this firmware on an older card);
   - production build not re-measured since S075 F1.

3. **Open defects (not fixed):**
   - Pattern Load fan-out `memcpy` can tear one playback tick;
   - O1: Settings Load bulk Global apply equalises per-voice Morph across
     the VOICE edit mask;
   - F4: AutoSave trace ring drops lifecycle records during dirty bursts;
   - underline limitations (live erase race, deferred clear race).

4. **Behaviours to remember (accepted):** Effects saved at pan 64 show `1`;
   MIDI-entered values reach the LCD only at the next repaint; copy/clear
   limits in `COPYCLEAR_UTILITIES.md` §18.

5. **S074 items still open:** the unexplained boot timeout; the `cpu` widget
   with `cmp` on; CrumpBit minimum-share run; BC11; saturator α 0.35.

6. **Stale tools and comments:** `decode_devlogs.py` misreads `pattrace.bin`;
   `BusCompressor.h` ("24 B"), `BusCompressor.c` ("+0.45 %"),
   `CrumpBitEffect.h` ("73,632 B"), the S074 C4 comment in `menu.c`,
   `main.c` about 532.

7. **Small carried debt:** Makefile echoed comments, bare-`make` default
   goal; duplicated comment lines in `mixer.c` and `ResonantFilter.c`;
   `presetManager.c` comment indentation; unreachable Scene Save phases
   33–36.

8. **CC_CLEAR_SEND moved:** from value 4 to value 5 due to P3 reordering
   (`CC_CLEAR_RESET_MORPH` is now 4). Any code that hardcodes the old value
   must be updated.

9. **Deferred features:** `/Effect/` browser and Load/Save item (A35); MIDI
   mapping (A20); live record of FX moves (A22) and of automation; track
   step-scale/shuffle playback (A10). FX lock removal (A15) and Scene
   copy/clear of the Effect were done in S075.

10. **Disposable:** the eight root `S076_*.md` documents and
    `S077_RETEST_CHECKLIST.md` (after test results are recorded); the ten
    root `S075_*.md` documents.

11. **Still open from earlier sessions:** Phase 5 hardware acceptance for
    S072 Steps 6–10; slow Load type switching; D-C1 (boot image check); LFO
    noise range (suspected); optional AutoSave old-format mask guard;
    filesystem budget primitive extraction; `AUTOSAVE_TRACE_RECORD_COUNT`
    still at temporary 2,048.

## Next session recommended goal

1. Hardware testing of P1–P4 using `S077_RETEST_CHECKLIST.md`. Fix anything
   found.
2. Re-measure the production build.
3. Collect user's pending S075 hardware reports (item 2 above).
4. Agree the next feature/fix target with the user.
