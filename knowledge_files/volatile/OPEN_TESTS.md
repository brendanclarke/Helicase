# Open Tests and Hardware Verification Items

Extracted from MEMORY.md during the S087 refactor. These are tests that should
be performed when the opportunity arises, not blocking items for current work.

---

## Hardware soak / confirmation

- **RV1-RV4 endless-pot noise** — long idle hardware soak needed, especially
  on global BPM page.
- **Sample loader naming/display/order** — hardware soak after LFN changes
  (sort is whole-filename lexicographic with ASCII case folded).
- **S084 matrix V1–V7 confirmation** — recorded as open since S084; confirm
  closed or waived.
- **S081 hardware matrix** — every row from `081_SESSION_HANDOFF_LOG.md` §13.2.
- **Phase 5 hardware acceptance for S072 Steps 6–10** — still not reported
  (`072_SESSION_HANDOFF_LOG.md` §11).

## Untested code fixes

- **Load/save button display glitch** — fix is a two-line reorder in
  `menu_switchPage()` `case LOAD_PAGE:`: update `menu_activePage` BEFORE
  calling `menu_resetSaveParameters()`. Full details in
  `LOAD_SAVE_GLITCH_ASSESSMENT.md`. Not yet applied.
- **S078 STEP marker retry one-liner** — deferred CGRAM marker retry in
  `menu_serviceRuntimeWidgets()` excludes SEQ_PAGE (one-line fix not applied).

## Multi-destination / edge-case testing

- **Multi-destination HCNAMES exit** — single-destination Scene 015 is
  hardware-confirmed; multi-destination, deferred-exit, toggle, hazard, and
  failure fixtures still need evidence.
- **S082 F5 Scene 2 Case-3 invalidation** — instrumented, not tested.
- **S082 P3 A2 frozen-screen blink loss** — low priority.
- **S077 re-test rows 3.10/3.14/3.15** — carry-over from S077 P4.

## Diagnostic / reproduction

- **Boot timeout** — unexplained, not reproducible. Reproduce with
  `DEV_MODE_DIAGNOSTIC 1`, then `DEV_LOGGING_IWDG 1`. The IWDG code is
  implemented but UNVERIFIED on hardware.
- **`cpu` widget with `cmp` on** — verify reading accuracy.

## Unimplemented features

- **Dither global menu option** — dither exists per-voice in DrumVoice but no
  `PAR_DITHER` or menu option is wired. Complete plan in DITHER_AUDIT.md
  Steps 1–8.

## Code cleanup (non-blocking)

- **Stale comments** — `BusCompressor.h` 24 B, `BusCompressor.c` +0.45 %,
  `CrumpBitEffect.h` 73,632 B, the S074 C4 comment placement in `menu.c`,
  `main.c` about 532.
- **Name-cache ownership interlock** — the AutoSave writer reads the shared
  9,000-byte name cache live while Menu clears it from 18 call sites. Only
  the two hottest callers were closed. General hazard (torn AutoSave record)
  needs a proper interlock. Deliberately deferred.
- **S081 P3 A-1** — tapping an already-looped VOICE drops it on release
  (confirm behaviour).
- **S081 stale previous step** — when REC is armed while running.
- **S081 P4 B-2** — carry-over.
- **Stage `W` missing** from `tools/decode_devlogs.py`.
