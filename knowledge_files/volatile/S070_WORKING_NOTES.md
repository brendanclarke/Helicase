# S073 Inter-Session Working Notes

Last updated: 2026-09-28 (Session 072 closure).

This file keeps its historical name. The Session 073 startup brief is
`S073_SESSION_STARTUP.md` in the repository root.

## Build metrics at S072 closure

| Metric | Value |
|---|---|
| text / data / bss | 483,024 / 416 / 426,336 |
| Flash payload | 483,440 B of 491,520 B (480 KiB window `0x08008000–0x0807FFFF`); **headroom 8,080 B** |
| ITCM | 3,768 / 16,384 B |
| DTCM statics | 4,448 B |
| FXBUF | 126,624 B at `0x20001160`; margin 3,744 B above the 120 KiB ASSERT |
| Branch | `dev-ph5-effects`: HEAD `58569ae` (Steps 1–8), Steps 9–11 uncommitted |

The `bss` figure from `size` includes the NOLOAD arena. Use
`python3 tools/link_budget.py arm-none-eabi-nm build/lxr02.elf` for region
truth.

## Carry-over from Session 072

1. **Phase 5 hardware acceptance** for Steps 6–10 is pending. The checklist
   is `072_SESSION_HANDOFF_LOG.md` §11 (storage fixtures, Effect page, FX
   sequencer remainder, automation/LFO, gate/fan-out, AutoSave robustness).
2. **Flash:** 8,080 B left; the budget warning has been active since ST7.
   S073 flash expansion is the planned remedy.
3. **Small carried debt** (SCOPING_TARGETS "Session 072 carried debt"):
   - `fxbuf_init()` handoff-reset order;
   - Makefile echoed comments and the bare-`make` default goal (use
     `make all`);
   - `modNode_waveInterpGeneration` initializer;
   - the SRAM1/SRAM2 stack wording;
   - the `mixer.c` duplicated comment;
   - `presetManager.c` comment indentation;
   - FX return ramp not reset while `off`;
   - stale `verify_bank_autosave.py`;
   - unreachable Scene Save phases 33–36.
4. **Deferred Phase 5 features:**
   - `/Effect/` browser and Load/Save item;
   - a buffer-using type (plus the same-type handoff refresh);
   - FX lock removal and Scene copy/clear of the Effect;
   - MIDI mapping;
   - live record;
   - track step-scale/shuffle playback.
5. **Still open from earlier sessions:**
   - Phase 4.5 copy operations (`pat_copyTrack`, `pat_copyPattern`,
     `pat_copyBar`);
   - optional autosave old-format mask guard (Fix 3);
   - possible extraction of the filesystem budget primitive.
6. **Disposable:** `S072_ST1..ST11_IMPLEMENTATION.md`. Their durable content
   is in `072_SESSION_HANDOFF_LOG.md`, `EFFECTS_BUS_REFERENCE.md` and the
   specs.

## Next session recommended goal

Session 073:

1. `S073_FLASH_EXPANSION.md` (bootloader study → probe tests → sector-7
   sample floor if Gate B passes);
2. `S073_CPU_USE_DSP_REDUCTION_REFACTOR.md` (profiler, ZDF division
   batching, string-free special writers, DMA pack, fused post-chain).

Read `S073_SESSION_STARTUP.md` first.
