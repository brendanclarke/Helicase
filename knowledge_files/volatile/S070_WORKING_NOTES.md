# S075 Inter-Session Working Notes

Last updated: 2026-09-30 (Session 074 closure).

This file keeps its historical name. The Session 075 startup brief is
`S075_PH6_COPY_CLEAR.md` in the repository root. The full Session 074 record
is `knowledge_files/log_archive/074_SESSION_HANDOFF_LOG.md`.

## Build metrics at S074 closure

| Metric | Value |
|---|---|
| text / data / bss | 502,512 / 416 / 426,392 |
| Flash payload | 502,928 B of 753,664 B; **headroom 250,736 B** |
| Image end | `_eflash_load` `0x08082C90`, 11,408 B into sector 6 (the bootloader writes sector 6) |
| ITCM | 4,168 / 16,384 B |
| DTCM statics | 4,480 B (+32 B bus compressor state) |
| FXBUF | 126,592 B at `0x20001180`; margin 3,712 B above the 120 KiB ASSERT; minimum Effect share 73,600 B |
| `scenes[16]` | 26,016 B (1,626 B per Scene) |
| Branch | `dev-ph5-effects`: HEAD `50610dd`; closeout docs and the `dsp_instruments_effects/` folder move uncommitted |
| `LXRV2_lxr02.img` | 502,944 B, SHA-256 `63eec2a602d80f54ea122a7977eb214c178f115be6c7e6a4117b02940663aeb0` |

The `bss` figure from `size` includes the NOLOAD arena. Use
`python3 tools/link_budget.py arm-none-eabi-nm build/lxr02.elf` for region
truth, and `STORAGE_SRAM_MANIFEST.md` for the map. Record the `.img` hash:
since S074 the `.bin` is unstamped.

## Carry-over from Session 074

1. **Session 075 goal (user):** start Phase 6 with copy and clear operations
   for step, bar, track, automation, Instrument, Scene and other Scene
   components. `S075_PH6_COPY_CLEAR.md` lists the components, a proposed
   operation model and the decisions (C1–C14) to take before scheduling.
2. **Boot timeout (open):** the user saw a boot fail to finish once around
   the AutoSave incident. It was not captured and no link to the torn record
   was shown. Reproduce with the card copy `SD_CARD_ATS_BOOT_BUG/` (without
   `.Spotlight-V100`/`.fseventsd`) and `DEV_MODE_DIAGNOSTIC 1`; if the screen
   freezes, repeat with `DEV_LOGGING_IWDG 1`. Keep `/bootlog.bin` if a
   timeout recurs.
3. **Not yet measured:** the `cpu` widget on the worst-case Scene with `cmp`
   on against off (expected 1.4–1.8 % while on). CrumpBit's minimum-share run
   (`DEV_FXBUF_FORCE_VOICE_UNITS 12`) and the `FxBf` self-check reading were
   not reported.
4. **Unconfirmed:** BC11. Track 7 counts as bus compressor sidechain voice 6;
   changing it is one line in `busComp_sidechainTrigger()`.
5. **Untried:** saturator `BUS_COMP_HF_SAT_SHARE` 0.35 (more bite).
6. **Existing defects logged, not fixed:**
   - O1: a Settings Load's bulk Global apply writes the active Scene's
     per-voice Morph and `srt` into every VOICE-edit-masked Scene;
   - `DTYPE_PM63` Effect pan rows show centre 64 as `1`;
   - F4: the AutoSave trace ring drops lifecycle records during dirty bursts
     (it hid the first repaired drain's `A`/`V`);
   - underline limitations: live erase while recording and a deferred clear
     race leave stale underlines until the next restart.
7. **Stale tools and comments:**
   - `tools/verify_bank_autosave.py` expects 129 HCNAMES rows (161 now);
   - `decode_devlogs.py` misreads `pattrace.bin` and labels `Q` "unknown
     producer";
   - comments: `BusCompressor.h` "24 B" (32 B), the `BusCompressor.c` loop
     comment "+0.45 %" (+0.6–0.8 %), `CrumpBitEffect.h` "73,632 B" (73,600),
     the C4 search comment placement in `menu.c`, and `main.c` about 532
     ("CRCs at the end of the load image").
8. **Small carried debt** (status in `074_SESSION_HANDOFF_LOG.md` §13.3):
   - Makefile echoed comments and the bare-`make` default goal (use
     `make all`);
   - duplicated comment lines in `mixer.c` and `ResonantFilter.c`;
   - `presetManager.c` comment indentation;
   - unreachable Scene Save phases 33–36;
   - blank-name stems;
   - AutoSave tracking state at the re-validation hook.
9. **Deferred Phase 5 features:**
   - `/Effect/` browser and Load/Save item (A35);
   - FX lock removal (A15), which fits the Phase 6 clear work;
   - Scene copy/clear of the Effect (Phase 6);
   - MIDI mapping (A20);
   - live record (A22);
   - track step-scale/shuffle playback (A10).
10. **Still open from earlier sessions:**
    - Phase 5 hardware acceptance for S072 Steps 6–10;
    - slow Load type switching (trace logger stays on);
    - D-C1 (keep the boot image check);
    - LFO noise range −1..1 (suspected);
    - optional AutoSave old-format mask guard (Fix 3);
    - possible extraction of the filesystem budget primitive.
11. **Repository hygiene:** the card copies `SD_CARD_ATS_BOOT_BUG/` and
    `SD_CARD_ATS_CORRECTION_OUTPUT/` (with macOS metadata) are committed;
    `EFFECTS_BUS_FEATURE_PLAN.md` was deleted in `ca77891` (retrieve with
    `git show f3a3105:EFFECTS_BUS_FEATURE_PLAN.md`).
12. **Disposable:** the ten root `S074_*.md` documents.

## Next session recommended goal

Session 075 (`S075_PH6_COPY_CLEAR.md`):

1. Review the component list and the operation model with the user, and get
   answers to the decisions (clipboard vs direct source→destination,
   gestures, RAM, what a Scene copy includes, AutoSave and edit-mask rules).
2. Then write the first line-level schedule, most likely the Pattern-level
   copies (`pat_copyTrack`, `pat_copyBar`, `pat_copyPattern`) through the
   Pattern Stack Service, since they are the long-standing no-ops.
