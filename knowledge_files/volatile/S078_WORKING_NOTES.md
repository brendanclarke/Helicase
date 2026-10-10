# S078 Inter-Session Working Notes — carry-over after the Session 078 close

Last updated: 2026-10-10 (Session 078 closure; for Session 079). This file
was the S077 → S078 carry-over and has been rewritten in place.

The full Session 078 record is
`knowledge_files/log_archive/078_SESSION_HANDOFF_LOG.md`. Per-track timing
authority: `PATTERN_DYNAMIC_STACK.md` §6.4 (timing) and §6.3a (STEP-page
editing). Morph architecture and the Morph-view rule:
`BANK_PRESET_ARCHITECTURE.md` §5, §8.

## Build metrics at S078 closure

| Metric | Value |
|---|---|
| text / data / bss (DEV config) | 539,856 / 416 / 427,616 |
| Flash payload | 540,272 B of 753,664 B; **headroom 213,392 B** |
| BSS growth from S077 | +608 B (76 B sequencer timing, 24 B track overrides, 352 B Scene track Morph endpoints, 119 B run-mode bytes incl. background region; approved at plan acceptance) |
| Branch | `dev-ph6-cleanup`: P1 `0c23def`, P2+P3 `b537e6f`; P4 + close-out docs uncommitted |
| Image | `build/LXRV2_lxr02.img` 540,384 B, SHA-256 `ecc0eb0b…3bd8bd` |
| Production build (`DEV_MODE_LOGGING 0`) | last measured at S075 F1: `text=516,688 data=408 bss=409,840`; re-measure |

## Carry-over from Session 078

1. **Hardware testing:** S078 P1, P2, P3 and P4 are all PASS (user,
   2026-10-10). Still open:
   - Retest rows still blank: 3.10/3.14/3.15 (S077 P4 Scene morph fan-out),
     C1–C6. S077 P5 (bar-to-step) and P6 (PERF morph underline) never had
     checklist rows.
2. **Decision:** the STEP-page deferred-marker retry gap
   (`menu_serviceRuntimeWidgets()` retry excludes SEQ_PAGE; fix: add
   `|| menu_activePage == SEQ_PAGE`). Offered, not applied.
3. **Accepted limits (S078):** track length 128 cannot be step-automated
   (7-bit); a queued patSvc removal can leave a stale STEP name underline
   until the next restart; clearing automation does not undo the last applied
   value (sticky until transport stop / Pattern restore); old PAT4 scale bytes
   play fast until converted (`tools/convert_scene_scale.py`).
4. **Possible follow-ups the user may want:** move `len`/`scl`/`shf` to
   category `Track` (three `valueNames` edits); a STEP Morph latch would need
   a gesture other than SHIFT+MODE STEP (the SOM entry).
5. **Card preparation:** delete `.hcprms1`/`.hcprms2` after flashing S078
   firmware on an older card (AutoSave Scene cells 51..71 are not migrated;
   old records restore scale 0 for the track Morph endpoint).
6. **Open defects carried (not fixed):** Pattern Load fan-out `memcpy` can
   tear one playback tick; O1 Settings Load per-voice Morph equalisation; F4
   AutoSave trace ring drops lifecycle records during dirty bursts; underline
   limitations (live erase race, deferred clear race); S077 P2 limitation:
   Scene-namespace LFO/velocity target tokens resolve through the active
   Scene in InstrumentManager.
7. **Behaviours to remember (accepted):** Effects saved at pan 64 show `1`;
   MIDI-entered values reach the LCD only at the next repaint; copy/clear
   limits in `COPYCLEAR_UTILITIES.md` §18.
8. **S074 items still open:** the unexplained boot timeout; the `cpu` widget
   with `cmp` on; CrumpBit minimum-share run; BC11; saturator α 0.35; stale
   comments (`BusCompressor.h` "24 B", `BusCompressor.c` "+0.45 %",
   `CrumpBitEffect.h` "73,632 B", the S074 C4 comment in `menu.c`, `main.c`
   about 532).
9. **Stale tools/small debt:** `decode_devlogs.py` misreads `pattrace.bin`;
   Makefile echoed comments and bare-`make` default goal; duplicated comment
   lines in `mixer.c` and `ResonantFilter.c`; `presetManager.c` comment
   indentation; unreachable Scene Save phases 33–36;
   `AUTOSAVE_TRACE_RECORD_COUNT` still at the temporary 2,048.
10. **Deferred features:** `/Effect/` browser and Load/Save item (A35); MIDI
    mapping (A20); live record of FX moves (A22) and of automation; roll
    overhaul; Patgen/Euklid revert; triplet `12a/12b` scale mode
    (SCOPING §4.10); one-shot LFOs; looper; external MIDI sequencing tracks.
11. **Still open from earlier sessions:** Phase 5 hardware acceptance for
    S072 Steps 6–10; slow Load type switching; D-C1 (boot image check); LFO
    noise range (suspected); optional AutoSave old-format mask guard;
    filesystem budget primitive extraction.
12. **Disposable:** the root `S078_*.md` documents (P1–P4 plans and
    schedules, `S078_RETEST_CHECKLIST.md`); earlier `S077_*`, `S076_*` and
    `S075_*` root documents if still present.

## Next session recommended goal

1. Decide the STEP marker retry one-liner.
2. Clear the remaining retest rows (3.10/3.14/3.15, C1–C6) and give S077 P5/P6
   a quick check.
3. Re-measure the production build.
4. Agree the next feature/fix target (SCOPING §6.0 remainder: roll overhaul,
   Patgen/Euklid revert, live record of automation; or Phase 6 MIDI work).
