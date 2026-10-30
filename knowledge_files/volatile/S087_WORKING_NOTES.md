# S087 Inter-Session Working Notes — SUPERSEDED by S088_WORKING_NOTES.md

Last updated: 2026-10-29 (Session 087 closure; for Session 088). **Superseded
by `S088_WORKING_NOTES.md` after Session 088 close.** The full Session 087
record is `knowledge_files/log_archive/087_SESSION_HANDOFF_LOG.md`.

**Authorities (updated in S087):**
- `INSTRUMENTS_DSP_REFERENCE.md`:
  - §6.1 (type system, sample/wavetable readers, modulator wav list);
  - §6.1.1 (`pos` extended to samples/wavetables);
  - §6.4 (transient click sample list);
  - §2 (trigger/automation order D6);
  - §8 (S087 cost rows);
  - §12 (binding mandates: free padding at offset 75 only);
- `CPU_USE_DSP_AUDIT.md` items 45–49;
- `STORAGE_SRAM_MANIFEST.md` (S087 header, catalogue v2 layout, ITCM table);
- `FILESYSTEM_SPEC.md` (`osc1_type` key, `/samples` and `/wavetables` install);
- `PATTERN_DYNAMIC_STACK.md` §6.1/§6.2 (drain order);
- `MODULE_INTERCHANGE_SPEC.md` (prepare/fire split, drain entry);
- `BANK_PRESET_ARCHITECTURE.md` (SELECTOR flag);
- `OSC_INTERP_AUDIT.md` (wavetable frame blending);
- `tools/dsp_test/DSP_TEST.md`.

## Build metrics at S087 closure

| Metric | Value |
|---|---|
| text / data / bss (DEV config) | 670,628 / 420 / 412,392 |
| Flash payload | 670,628 B of 753,664 B; **headroom 83,036 B** |
| ITCM | 12,640 / 16,384 B (3,744 free) |
| DTCM statics / FX-arena margin | 4,344 B / 3,840 B |
| RAM | net ≈ −17,791 B SRAM1 (removed sample_manifest, SampleMemory caches); DTCM −126 B net |
| Branch | `dev-ph7-voice_expansion`. Commit `f3805478` (P0+P1). Uncommitted: P2+P3 changes |
| Host checks | `stepauto_order`, `drain_cursor`, `osc_type`, `catalogue`, `osc_sample`, `osc_wtb`, `interp_slots`, `special_tags`, `stock_tables`, `selftest`, `filter`, `pack`, `postchain`, `octave`, `mixer`: all pass |

## Pending hardware testing (first priority for S088)

1. **S087 P0+P1 full implementation:** install, playback, pos on samples,
   wavetable blending, modulator s/l/w list, type switching between
   wav/smp/slp/wtb, step-automation order (sample slicing).
2. **S087 P2 display/naming:** wav cell shows correct names under typed
   oscillators, modulator list s/l/w distinction, first 4 + last 4 name
   rule.
3. **S087 P3:** mod oscillator restart for one-shot samples, transient click
   sample playback and speed/volume.

## Open items from S087

1. **F12 (Snare noise loop +1):** the Snare noise loop grew from 14 to 15
   instructions per sample. A1 allows it (stays below the section's worst
   case). Needs user decision — accept or investigate.
2. **F15 (stale host targets):** `osc_mod`, `osc_pos`, `osc_stock` need
   their `SampleMemory.h` extractions updated for v2. Host-bench follow-up
   excluded from P1 schedule.
3. **Commit:** all S087 changes are uncommitted. The working tree is P2+P3
   changes on top of `f3805478`.

## Deferred items (carried from S086 and earlier)

- `ReA`/`TrA`/`SwA`: one generator line + ID/name/table each, +22,528 B
  each. The recipe is ready.
- Re3/Sw3 level difference (≈ 8 / 2 dB quieter than Rec/Saw, crest factor).
- LFO depth wrap on `pos` (clamped today).
- `OscInfo.startPhase` / `Drum_setPhaseVoice()` clean-up.
- The sector-7 flash option if flash runs out.
- Zero-crossing bitmap (A13, deferred to later session with the optimizer).
- `rng` free-run "ramp always" trial (user request carried from S085).

## Binding reminders for S088

- `seq_drainPendingAutomationTo()` runs inside `voiceControl_processPending()`,
  NOT in `main.c` — do not re-add it there.
- `osc1_type` is `INSTRUMENT_PARAM_FLAG_SELECTOR` (0x08) — not morphable,
  stored single-ended.
- Catalogue magic "LXC2" is programmed LAST. `sampleInstall_abortItem()`
  must NEVER rewind cursor onto programmed flash.
- Interpolation slot stamps are 1..255 (never 0; 0 = "no claim").
- The v1 sample loops and wav interpolation code are deleted — do not
  restore them.
- MEMORY.md was refactored to 525 lines in S087 — do not re-pollute it
  with session history, specification content, or resolved-issue narratives.
