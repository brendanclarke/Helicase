# S089 P3 — Fold the Drum FM Depth Ramp into the FM Carrier Loops

**Status:** Direction plan (2026-10-30). Not implemented. A later session
does the deep-dive and writes the full schedule (as `S089_P2_IMPLEMENTATION.md`
did for P2).
**Constraint:** CPU is at its ceiling (`S089_P2_WAVEFOLDER_CHAIN.md` §8,
binding). This plan is allowed only because it **lowers** the cost; no
voice may get any dearer.

---

## 1. What we are removing

**The loop.** Drum's `fm` branch (`DrumVoice.c` ≈760–807, S088 P0 Layer 2e)
scales the modulator block by a per-sample ramp before the FM carrier
reads it:

```c
modBuf[k] = (int16_t)((float)modBuf[k] * s);   s += dstep;   /* start..1 */
```

**What it is for.** It smooths the FM-index jump on a `fre`/`itp` retrigger
into an audible tail.
- **When it changes anything:** only on the one block after such a
  retrigger (`ampRampArm`). On every other block, and always in `rst`,
  `s` = 1.0 exactly and the pass is the identity.
- **Cost:** 10 instructions per sample (measured), every block, `fm` mode
  only. It is part of Drum `fm`'s worst case of ≈90 per sample (crash
  modulator 30 + ramp 10 + `slp` FM carrier 50).

## 2. The idea

Every FM carrier loop already multiplies the modulator by a loop-invariant
depth each sample:

| Loop (`Core/DSPAudio/Oscillator.c`) | Line | Expression |
|---|---:|---|
| `calcFmSineBlock()` | 178 | `(uint32_t)(modBuffer[i]*osc->fmMod) << 17` |
| `calcFmBlock()` (saw/tri/rec/`Sq3`/`Sw3`) | 222 | `… << 19` |
| `calcSampleOscFmBlock()` (crash) | 554 | `… << 14` |
| `osc_smpFmBlock()` | 2052 | `ssat((float)mod[i] * osc->fmMod) << OSC_FM_SAMPLE_SHIFT` |
| `osc_slpFmBlock()` | 2126, 2154 | the same |
| `osc_wtbFmBlock()` | 2987 | `ssat(…) << 19` |
| `osc_wtbFmBlendBlock()` | 3042 | `ssat(…) << 19` |

**The change.** Replace `osc->fmMod` with a local depth `d` that ramps
per sample (`d += dStep`): about **+1 instruction per sample** in the
carrier. The separate 10-instruction pass goes away.
- **Expected:** about **−9 per sample per Drum voice in `fm`**. The worst
  case goes from ≈90 to ≈81, which puts `rng` (≈77) close behind.
- **Ramp endpoints:** `d0 = min(lastFmDepth, 4 × fmMod)` (only when armed
  and `fmMod > 0.0001`), `d1 = fmMod`, `dStep = (d1 − d0)/(size − 1)`.
  This keeps today's 0–4 start clamp and also **removes today's one VDIV
  per block** (no `lastFmDepth / newDepth`).
- **Sound class:**
  - **Unarmed (every `rst` block and almost every other):**
    `d0 = fmMod`, `dStep = 0`, so `d` is exactly `fmMod` for every sample
    and the product is the same expression. Bit-identical (S0); verify
    with a host law.
  - **Armed block:** S2. The scaled modulator is no longer truncated to
    int16 before the carrier uses it. That is less quantisation, and
    inaudible by design.

## 3. The main constraints (read these first)

1. **The FM loops are shared.** Cymbal (`CymbalVoice.c:429/431`) and HiHat
   (`HiHat.c:447/449`) call the same `calcNextOscSampleFmBlock()` /
   `osc_renderTypedFm()`. Adding the ramp to the shared loops would give
   Cymbal and HiHat +1 per sample for nothing, which **violates the
   ceiling rule**. So do not edit the existing loops (this is also the S087
   rule: new per-sample code is a new function, never an edit to an
   existing loop). Instead, add **Drum-only ramped copies** with their own
   dispatchers, and call them only from Drum's `fm` branch.
   - Do not rely on LTO constant propagation (a `dStep = 0` call site for
     Cymbal/HiHat) to remove the add. It is not guaranteed, and the gate
     requires Cymbal/HiHat's loops to stay byte-identical.
2. **ITCM may not fit — the deciding question.** ITCM is 13,032 /
   16,384 B (**3,352 B free**). The FM carrier code in ITCM today is about
   **3.5 KB**:

   | Function | Size (B) |
   |---|---:|
   | `calcFmBlock` × 4 LTO specialisations | 192 each |
   | `calcNextOscSampleFmBlock` (dispatcher + sine FM + noise) | 340 |
   | `calcSampleOscFmBlock` | 196 |
   | `osc_smpFmBlock` | 324 |
   | `osc_slpFmBlock` | 604 |
   | `osc_wtbFmBlock` | 412 |
   | `osc_wtbFmBlendBlock` | 860 |

   Ramped copies need about **3.0 KB** even with savings:
   - one `calcFmBlockRamp` with a variable table pointer instead of four
     specialisations (check that the per-sample count stays the same);
   - the two ramped dispatchers in flash, not ITCM.

   That would leave ITCM at about 99 %, which needs explicit user sign-off.
   The options to weigh are in §5.
3. **Gates (as S089 P0 §4.2 / P2 §15):**
   - **Cymbal/HiHat:** their FM loops are byte-identical, and the inlined
     set of the mixer is unchanged.
   - **ITCM:** every existing ITCM function keeps its address. The new
     subsection must be pinned *after* `.itcm.s089` in the linker script.
     Today `.itcm.s089` is only reached through the catch-all
     `*(.itcm.*)`, so name it explicitly before adding a new one.
   - **Per-sample counts:** each ramped loop is at most its original + 1
     (+2 tolerated only if the net saving is still ≥ 8).
   - **Hardware:** the `cpu` widget on the worst-case Scene reads **lower**
     than today. There is no "equal" pass, given the ceiling.

## 4. Expected benefit (be honest about the size)

- **Per voice:** the render budget is about 4,909 cycles per frame
  (471,288 per 96 frames). 9 instructions per sample per Drum-`fm` voice is
  about **0.2 % CPU per voice**, and at most about **1.1 %** with all six
  slots on Drum `fm`.
- **It only helps scenes with Drum voices in `fm`.** **Before starting,
  find out the worst-case Scene's composition** (types, mod modes, osc
  types, run modes, which Effect). If its slots are not Drum `fm`, P3
  frees nothing that matters for the ceiling, and the effort belongs
  elsewhere.

## 5. Options to decide in the implementation session

| Option | Saving per Drum-`fm` voice | ITCM | Risk |
|---|---:|---:|---|
| **A. Full fold-in:** ramped copies of every FM carrier loop | ≈ −9 | ≈ +3.0 KB (≈99 % full) | the most code; ITCM nearly exhausted for future work |
| **B. Partial fold-in:** ramped copies only for the carriers that set the worst case (`slp` FM, `wtb` FM blend, `smp` FM, crash FM); keep the pass for the cheap carriers (sine, table) | the worst case drops ≈ −9; the cheap carriers keep today's cost | ≈ +2.0 KB | two mechanisms in one branch; the worst case must be re-derived for every carrier type |
| **C. Cheaper pass, no fold-in:** an integer `SMULWB` ramp (≈6–7 per sample), or packed two-per-word (≈4.5) | ≈ −3 to −5.5 | 0 | tiny change; armed-block rounding differs slightly (S2); unarmed `s` = 1.0 must stay exact (Q16 65536 → identity) |
| D. Leave it | 0 | 0 | — |

Recommendation for the next session:
1. Measure the worst-case Scene composition (§4).
2. If Drum `fm` is in it, prototype **C** first (an hour, no ITCM, no shared
   code), then decide whether **A**/**B**'s extra ≈4–5 per sample is worth
   the ITCM.

## 6. Where to look

- **`Core/DSP/Instruments/Drum/DrumVoice.c`:**
  - the `fm` branch and the ramp pass (≈755–812);
  - `lastFmDepth` (init line 188);
  - `ampRampArm` (set in `Drum_triggerVoice()`);
  - `Drum_modEnvDepth()`.
- **`Core/DSPAudio/Oscillator.c`:** the loops in the §2 table; the
  dispatchers `calcNextOscSampleFmBlock()` (288) and `osc_renderTypedFm()`
  (2429); the `INITCM` / `OSC_S085_SECTION` / `OSC_S087_LOOP` placement
  macros and the S085/S087 ITCM notes (≈674, ≈1497).
- **`Core/DSPAudio/Oscillator.h`:** the declarations; `OscInfo.fmMod`.
- **`STM32F765VIHx_FLASH.ld`:** the `.itcm` input order (line ≈150, S089 P2
  §3).
- **History:** `knowledge_files/log_archive/088_SESSION_HANDOFF_LOG.md`
  §1.5 (why the ramp exists; D1 constant cost);
  `knowledge_files/log_archive/085_SESSION_HANDOFF_LOG.md` §7 (the FM / ring
  / sync cost tables); `CPU_USE_DSP_AUDIT.md` items 36–39, 52 and 57–60.
- **Tests:**
  - `tools/dsp_test/modules/oscillator.c` (the place for an S0 law:
    unarmed ramped loop == original loop, sample for sample, for every
    carrier type);
  - `tools/dsp_test/modules/run_modes.c` (the S088 ramp laws; note its
    pre-existing compile gap: it declares `distortion_curveSample16`, not
    `...F`);
  - `tools/dsp_test/elf_loopdump.py`, `elf_hotloops.py`.
- **Do not repeat** (`MEMORY.md` Failed Approaches): a local variable kept
  live across a voice render (S086 P3) made inlined loops reload. Keep any
  per-block ramp state in the voice or `OscInfo`, not in a local spanning
  the render.
