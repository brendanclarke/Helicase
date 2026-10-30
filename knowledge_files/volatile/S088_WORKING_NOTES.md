# S088 Inter-Session Working Notes — carry-over after Session 088 close

Last updated: 2026-10-29 (Session 088 closure; for Session 089). The full
Session 088 record is `knowledge_files/log_archive/088_SESSION_HANDOFF_LOG.md`.

**Authorities (updated in S088):**
- `INSTRUMENTS_DSP_REFERENCE.md`:
  - §6.1 (run modes: `osc1_run`, `rst`/`fre`/`itp`);
  - §6.3 (envelopes: `slopeEg2_retriggerFrom()`);
  - §6.5 (post-chains: gain ramps, float-through distortion);
  - §6.6 (distortion: `distortion_curveSampleF()`);
  - §6.2 (filter: `SVF_recalcFreqSlew()`);
- `CPU_USE_DSP_AUDIT.md` items 50–56;
- `STORAGE_SRAM_MANIFEST.md` (S088 P0 + P1 entries);
- `FILESYSTEM_SPEC.md` (`osc1_run` key);
- `MIDI_SPEC.md` (§5.5 `osc1_run`);
- `EFFECTS_MIXER_DSP_REFERENCE.md` (§3 fader smoothing, header);
- `tools/dsp_test/DSP_TEST.md`.

## Build metrics at S088 closure

| Metric | Value |
|---|---|
| text / data / bss (DEV config) | 676,264 / 420 / 412,884 |
| Flash payload | 676,264 B of 753,664 B; **headroom 77,400 B** |
| ITCM | 12,656 / 16,384 B (3,728 free) |
| DTCM statics / FX-arena margin | 4,345 B / 3,839 B |
| RAM | +496 B SRAM1 (runtime_slots +432, osc_itpScratch +64); +1 B DTCM (modNode_itpReserved); +24 B DTCM (mixer_fader_smooth) |
| Branch | `dev-ph7-voice_expansion`. Commit `80029d31` (P0). Uncommitted: P1 changes (fader smoothing, float-through distortion) |
| Host checks | `run_modes`, `oscillator` (incl. itp), `modulation` (incl. L6), `instrument_manager`, `check_special_tags.py`, all prior targets: pass |

## Pending hardware testing (first priority for S089)

1. **S088 P0 run modes hardware matrix H1–H17** (from `S088_P0_OSC_RUN_MODES.md` §10):
   - H1: `rst` A/B (bit-identical to pre-S088 on real hardware)
   - H7/H8: `itp` audible / decayed retrigger
   - H11: carrier-only `itp` on Drum `fm`
   - H15: step automation of `run`
   - H16: worst-case CPU with all six voices in `itp`, both fade slots held
   - G1: Drum `fm` ceiling with depth ramp (hardware stress confirms)
   - G2: resonance step in `fre`/`itp` (if audible, extend
     `SVF_recalcFreqSlew()` to step `q`)
2. **S087 full hardware testing** — carried from S087/S088 (sample install,
   playback, type switching, etc. — see OPEN_TESTS.md)

## Open items from S088

1. **G2 (resonance step):** only the cutoff coefficient `g` is slewed. If
   a resonance step is audible in `fre`/`itp`, extend `SVF_recalcFreqSlew()`
   to step `q` as well. Hardware listening needed.
2. **Drum instant-attack ramp bypass:** the fix adds `!voice->ampRampArm` to
   the instant-attack condition. Snare/Cymbal/HiHat have no such shortcut.
3. **Float-through distortion at drive 0:** the round trip at `k = 0`
   through `distortion_curveSampleF()` can differ from `distortion_curveSample16()`
   by up to 1 LSB (float precision). The S073 golden was against the int16
   path. The difference is inaudible; the P0 `rst` bit-identity test uses
   `distortion_curveSampleF()` in the new post-chains, so the golden is
   now against the new path.
4. **Task handoff files to delete:** `S088_P0_OSC_RUN_MODES.md`,
   `S088_P0_IMPLEMENTATION.md`, `S088_P1_SLIDER_ZIP_FIX.md` — all content
   preserved in `088_SESSION_HANDOFF_LOG.md` and the specification documents.

## Deferred items (carried from S087 and earlier)

- `ReA`/`TrA`/`SwA`: one generator line + ID/name/table each, +22,528 B
  each. The recipe is ready.
- Re3/Sw3 level difference (≈ 8 / 2 dB quieter than Rec/Saw, crest factor).
- LFO depth wrap on `pos` (clamped today).
- `OscInfo.startPhase` / `Drum_setPhaseVoice()` clean-up.
- The sector-7 flash option if flash runs out.
- Zero-crossing bitmap (A13, deferred to later session with the optimizer).
- `rng` free-run "ramp always" trial (user request carried from S085).
- F12 (Snare noise loop +1): 14 → 15 instructions; within section worst case.
  Needs user decision — accept or investigate.
- F15 (stale host targets): `osc_mod`, `osc_pos`, `osc_stock` need
  `SampleMemory.h` extractions updated for v2.

## Binding reminders for S089

- `ampRampArm` arms ONE block only — never pass unequal ramp endpoints when clear.
- `rst` must remain bit-identical to pre-S088; host law B1 proves this.
- `seq_drainPendingAutomationTo()` runs inside `voiceControl_processPending()`,
  NOT in `main.c` — do not re-add it there.
- `osc1_type` is `INSTRUMENT_PARAM_FLAG_SELECTOR` (0x08) — not morphable,
  stored single-ended.
- `osc1_run` is `INSTRUMENT_PARAM_FLAG_SELECTOR` + `AUTOMATABLE` — not
  morphable, stored single-ended.
- Catalogue magic "LXC2" is programmed LAST.
- Interpolation slot stamps are 1..255 (never 0; 0 = "no claim").
- The v1 sample loops and wav interpolation code are deleted — do not
  restore them.
- MEMORY.md was refactored to 525 lines in S087 — do not re-pollute it
  with session history, specification content, or resolved-issue narratives.
- `mixer_fader_smooth[]` runs at block rate; alpha 0.15. Endpoint bypass at
  0.0 and 1.0.
