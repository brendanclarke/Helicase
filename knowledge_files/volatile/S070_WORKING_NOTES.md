# S076 Inter-Session Working Notes

Last updated: 2026-10-03 (Session 075 closure).

This file keeps its historical name. The full Session 075 record is
`knowledge_files/log_archive/075_SESSION_HANDOFF_LOG.md`; the copy/clear
reference is `knowledge_files/specification_reference/COPYCLEAR_UTILITIES.md`.
There is no Session 076 startup document yet: the next goal is chosen with
the user (below).

## Build metrics at S075 closure

| Metric | Value |
|---|---|
| text / data / bss (DEV config) | 532,408 / 416 / 426,712 |
| Flash payload | 532,824 B of 753,664 B; **headroom 220,840 B** |
| Image end | `_eflash_load` `0x0808A158`, 41,304 B into sector 6 |
| ITCM | 4,168 / 16,384 B |
| DTCM statics | 4,472 B (−8 B: `srt` multiplier removed) |
| FXBUF | 126,592 B at `0x20001180`; margin 3,712 B above the 120 KiB ASSERT |
| `scenes[16]` | 26,080 B (1,630 B per Scene; `scenes` `0x65E0`) |
| SRAM1 `.bss` section | 293,060 B (S074: 292,732 B) |
| Branch | `dev-ph6-copyclear`: HEAD `76aef20`; closeout docs and three comment-only pointer edits uncommitted |
| `LXRV2_lxr02.img` | 532,840 B, SHA-256 `d1c0aac283e0631ccb074bafa1aa43a6884da0bba9493c4ebe685b78c5709ceb` |
| Production build (`DEV_MODE_LOGGING 0`) | last measured at F1: `text=516,688 data=408 bss=409,840`; re-measure |

The `bss` figure from `size` includes the NOLOAD arena. Use
`python3 tools/link_budget.py arm-none-eabi-nm build/lxr02.elf` for region
truth and `STORAGE_SRAM_MANIFEST.md` for the map. Record the `.img` hash.

## Carry-over from Session 075

1. **Hardware checks still to report (user):**
   - the SHIFT+TRACK overlay follow-up: while any overlay TRACK is held,
     every TRACK press moves the screen to that track and nothing mutes; the
     last release restores the Effect page;
   - the combined case list in `COPYCLEAR_UTILITIES.md` §16 (buttons ×
     modes, pastes on a playing Pattern, nearly full pools, Scene-level
     copies with fan-out, names, suspension, regression);
   - F3 cases beyond the user's "seems ok" (`075_SESSION_HANDOFF_LOG.md`
     §11; e.g. LFO on Morph plus pitch automation, `Nvm` on the same step,
     MIDI CC to an automated parameter, MIDI 127 to an on/off parameter).
2. **Card preparation:** before the first boot of this firmware on an older
   card delete `.hcprms1`/`.hcprms2` and other temporary records (no
   AutoSave migration for FX-send Morph cells 45..50, user). Then run
   `python3 tools/verify_bank_autosave.py <card> <bank>` after a drain: a
   PASS is still pending (the checked-in `SD_CARD/` is not a coherent
   post-F2 Bank snapshot).
3. **Production build** not re-measured since F1.
4. **Open defects, not fixed:**
   - Pattern Load fan-out `memcpy` can tear one playback tick of a playing
     destination (fix: the sentinel-first order copy/clear already uses);
   - O1: a Settings Load's bulk Global apply writes the active Scene's
     per-voice Morph into every VOICE-edit-masked Scene (`srt` part gone);
   - F4: the AutoSave trace ring drops lifecycle records during dirty bursts;
   - underline limitations: live erase while recording and a deferred clear
     race leave stale underlines until the next restart.
5. **Behaviours to remember (accepted):** Effects saved at pan 64 show `1`;
   MIDI-entered values reach the LCD only at the next repaint; copy/clear
   limits in `COPYCLEAR_UTILITIES.md` §18.
6. **S074 items still open:** the unexplained boot timeout (reproduce with
   `SD_CARD_ATS_BOOT_BUG/` and `DEV_MODE_DIAGNOSTIC 1`, then
   `DEV_LOGGING_IWDG 1`); the `cpu` widget with `cmp` on (expected 1.4–1.8 %);
   CrumpBit minimum-share run; BC11 (track 7 as sidechain voice 6);
   saturator α 0.35 untried.
7. **Stale tools and comments:** `decode_devlogs.py` misreads `pattrace.bin`
   and labels `Q` "unknown producer"; comments in `BusCompressor.h` ("24 B",
   now 32 B), the `BusCompressor.c` loop ("+0.45 %", measured +0.6–0.8 %),
   `CrumpBitEffect.h` ("73,632 B", now 73,600), the S074 C4 comment placement
   in `menu.c`, `main.c` about 532. (`verify_bank_autosave.py` was fixed in
   S075 F2.)
8. **Small carried debt** (status in `074_SESSION_HANDOFF_LOG.md` §13.3):
   Makefile echoed comments and the bare-`make` default goal (use `make
   all`); duplicated comment lines in `mixer.c` and `ResonantFilter.c`;
   `presetManager.c` comment indentation; unreachable Scene Save phases
   33–36; blank-name stems; AutoSave tracking state at the re-validation
   hook.
9. **Deferred features:** `/Effect/` browser and Load/Save item (A35); MIDI
   mapping (A20); live record of FX moves (A22) and of automation; track
   step-scale/shuffle playback (A10). FX lock removal (A15) and Scene
   copy/clear of the Effect were done in S075.
10. **Still open from earlier sessions:** Phase 5 hardware acceptance for
    S072 Steps 6–10; slow Load type switching (trace logger stays on); D-C1
    (keep the boot image check); LFO noise range −1..1 (suspected); optional
    AutoSave old-format mask guard (Fix 3); possible extraction of the
    filesystem budget primitive; `AUTOSAVE_TRACE_RECORD_COUNT` still at the
    temporary 2,048 (user D2).
11. **Disposable:** the ten root `S075_*.md` documents (pre-implementation
    versions in `00bd078`, `b1216db`, `822bbc9`, `b8f08db`).

## Next session recommended goal

1. Collect the user's hardware results for item 1 and the validator run
   (item 2); fix anything they report first.
2. Re-measure the production build.
3. Agree the next Phase 6 item with the user. `SCOPING_TARGETS.md` Phase 6
   §6.0 puts the deferred Phase 4 items first (manual roll triggering,
   dot/triplet subdivisions, automation hold reconciliation, per-track
   scale/shuffle playback, live record of automation), then MIDI rework,
   looper, one-shot LFOs and external MIDI sequencing tracks. Start with a
   plan and numbered decisions, as in S075.
