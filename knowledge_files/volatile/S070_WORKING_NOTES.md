# S074 Inter-Session Working Notes

Last updated: 2026-09-29 (Session 073 closure).

This file keeps its historical name. The Session 074 startup brief is
`S074_EFFECT_BUGS_BUFFER_USE.md` in the repository root. The full Session 073
record is `knowledge_files/log_archive/073_SESSION_HANDOFF_LOG.md`.

## Build metrics at S073 closure

| Metric | Value |
|---|---|
| text / data / bss | 486,688 / 416 / 426,336 |
| Flash payload | 487,104 B of 753,664 B (736 KiB window `0x08008000–0x080BFFFF`); **headroom 266,560 B** |
| ITCM | 4,168 / 16,384 B (+400 B in S073: `osc_setFreq` standalone) |
| DTCM statics | 4,448 B |
| FXBUF | 126,624 B at `0x20001160`; margin 3,744 B above the 120 KiB ASSERT |
| Branch | `dev-ph5-effects`: HEAD `692abf8`; S073 closeout edits uncommitted |
| `lxr02.bin` SHA-256 | `1bd8be5201eecf0222d3cdc6c36bf272ce38a33aee535ca5fcfd0a47c155fc82` |

The `bss` figure from `size` includes the NOLOAD arena. Use
`python3 tools/link_budget.py arm-none-eabi-nm build/lxr02.elf` for region
truth, and `STORAGE_SRAM_MANIFEST.md` for the map.

## Carry-over from Session 073

1. **Session 074 goals** (user): the Effect-page automation-underline bug,
   then the first Effect type that uses the DTCM arena. See
   `S074_EFFECT_BUGS_BUFFER_USE.md`.
2. **Phase 5 hardware acceptance** for S072 Steps 6–10 is still not
   reported (`072_SESSION_HANDOFF_LOG.md` §11).
3. **Deferred:** slow Load type switching, while the trace logger stays on
   (`knowledge_files/drafts/MENU_LOAD_SPEEDUP_SMOOTHNESS.md`).
4. **Unproven:** the bootloader past `0x08080000`. The first image that grows
   into sector 6 is the test; the boot image check reports failures.
5. **Open decision D-C1:** keep the boot image check (kept for now).
6. **Suspected, unverified:** LFO noise spans −1..1 (`SCOPING_TARGETS.md`).
7. **Small carried debt** (status in `073_SESSION_HANDOFF_LOG.md` §9.3):
   - `fxbuf_init()` handoff-reset order (diagnostic only);
   - Makefile echoed comments and the bare-`make` default goal (use
     `make all`);
   - duplicated comment lines in `mixer.c` and `ResonantFilter.c`;
   - `presetManager.c` comment indentation;
   - FX return ramp not reset while `off` (relevant to the S074 buffer type);
   - stale `verify_bank_autosave.py`;
   - unreachable Scene Save phases 33–36.
8. **Deferred Phase 5 features:** `/Effect/` browser and Load/Save item; FX
   lock removal and Scene copy/clear of the Effect; MIDI mapping; live
   record; track step-scale/shuffle playback.
9. **Still open from earlier sessions:** Phase 4.5 copy operations; optional
   AutoSave old-format mask guard (Fix 3); possible extraction of the
   filesystem budget primitive.
10. **Disposable:** the five root `S073_*.md` documents and
    `S072_ST1..ST11_IMPLEMENTATION.md`.

## Next session recommended goal

Session 074 (`S074_EFFECT_BUGS_BUFFER_USE.md`):

1. Effect page: underline parameter names that are automated in the current
   Pattern or locked in the FX sequence.
2. The first buffer-using Effect type, closing the same-type handoff refresh
   and the FX return ramp gaps.
