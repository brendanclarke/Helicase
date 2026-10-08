# S078 Inter-Session Working Notes

Last updated: 2026-10-08 (Session 077 closure).

The full Session 077 record is
`knowledge_files/log_archive/077_SESSION_HANDOFF_LOG.md`. The copy/clear
reference is `knowledge_files/specification_reference/COPYCLEAR_UTILITIES.md`.

## Build metrics at S077 closure

| Metric | Value |
|---|---|
| text / data / bss (DEV config) | 538,416 / 416 / 427,008 |
| Flash payload | 538,832 B of 753,664 B; **headroom 214,832 B** |
| BSS growth from S076 | +264 B (256 B `ccSvc_snapTable[128]` + 8 B alignment) |
| Branch | `dev-ph6-cleanup`: S077 code changes uncommitted |
| Production build (`DEV_MODE_LOGGING 0`) | last measured at S075 F1: `text=516,688 data=408 bss=409,840`; re-measure |

## Carry-over from Session 077

1. **Hardware testing (top priority):** S077 P4–P6 await hardware verification.
   Use `S077_RETEST_CHECKLIST.md` in the root directory. Key areas:
   - P4 items 4.1–4.20: Scene morph fan-out correction (test that
     `copy morph` and `reset morph` fan out to edit-mask members);
   - P5 items 5.1–5.11: bar-to-step cross-kind copy (bar source →
     step destination, verify correct geometry);
   - P6 items 6.1–6.42: PERF mode morph automation underline (verify
     underline markers appear on PERF page for voices with morph
     automation);
   - P3 items 3.10, 3.14, 3.15: cleared for re-test after P4 fan-out
     correction;
   - Carry-over C1–C6 from earlier sessions.

2. **S076 P1–P4 carry-over (still needs hardware):**
   - P1: automation override clears (Rule A per-parameter, Rule B per-Scene);
   - P2: LFO retrigger `scn` Scene-change phase handoff, phase offset
     scaling;
   - P3: `reset morph` and `copy morph` at track and Scene level, fan-out
     behavior, `reset fx morph` fan-out;
   - P4: `reload scene`, bar chaselight, SHIFT+SELECT per-track pattern
     length.

3. **S075 carry-over (still open):**
   - the SHIFT+TRACK overlay follow-up re-test;
   - the combined case list in `COPYCLEAR_UTILITIES.md` §16;
   - F3 cases beyond "seems ok";
   - card preparation (delete `.hcprms1`/`.hcprms2` before first boot of
     this firmware on an older card);
   - production build not re-measured since S075 F1.

4. **Open defects (not fixed):**
   - Pattern Load fan-out `memcpy` can tear one playback tick;
   - O1: Settings Load bulk Global apply equalises per-voice Morph across
     the VOICE edit mask;
   - F4: AutoSave trace ring drops lifecycle records during dirty bursts;
   - underline limitations (live erase race, deferred clear race);
   - P2 known limitation: Scene-namespace LFO/velocity target tokens resolve
     through the active Scene in InstrumentManager, not the played Scene.

5. **Behaviours to remember (accepted):** Effects saved at pan 64 show `1`;
   MIDI-entered values reach the LCD only at the next repaint; copy/clear
   limits in `COPYCLEAR_UTILITIES.md` §18.

6. **S074 items still open:** the unexplained boot timeout; the `cpu` widget
   with `cmp` on; CrumpBit minimum-share run; BC11; saturator α 0.35.

7. **Stale tools and comments:** `decode_devlogs.py` misreads `pattrace.bin`;
   `BusCompressor.h` ("24 B"), `BusCompressor.c` ("+0.45 %"),
   `CrumpBitEffect.h` ("73,632 B"), the S074 C4 comment in `menu.c`,
   `main.c` about 532.

8. **Small carried debt:** Makefile echoed comments, bare-`make` default
   goal; duplicated comment lines in `mixer.c` and `ResonantFilter.c`;
   `presetManager.c` comment indentation; unreachable Scene Save phases
   33–36.

9. **CC_CLEAR_SEND moved:** from value 4 to value 5 due to S076 P3
   reordering (`CC_CLEAR_RESET_MORPH` is now 4). Any code that hardcodes
   the old value must be updated.

10. **Deferred features:** `/Effect/` browser and Load/Save item (A35); MIDI
    mapping (A20); live record of FX moves (A22) and of automation; track
    step-scale/shuffle playback (A10). FX lock removal (A15) and Scene
    copy/clear of the Effect were done in S075.

11. **Disposable:** the eleven root `S077_*.md` documents (after test results
    are recorded); the eight root `S076_*.md` documents; the ten root
    `S075_*.md` documents.

12. **Still open from earlier sessions:** Phase 5 hardware acceptance for
    S072 Steps 6–10; slow Load type switching; D-C1 (boot image check); LFO
    noise range (suspected); optional AutoSave old-format mask guard;
    filesystem budget primitive extraction; `AUTOSAVE_TRACE_RECORD_COUNT`
    still at temporary 2,048.

## Next session recommended goal

1. Hardware testing of S077 P4–P6 and S076 P1–P4 using
   `S077_RETEST_CHECKLIST.md`. Fix anything found.
2. Re-measure the production build.
3. Collect user's pending S075 hardware reports (item 3 above).
4. Agree the next feature/fix target with the user.
