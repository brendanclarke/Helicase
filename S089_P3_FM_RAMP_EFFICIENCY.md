# S089 P3 — Fold the Drum FM Depth Ramp into the FM Carrier Loops

**Status:** Direction plan (2026-10-30). Not implemented. **Decision (user):
option B on three carriers** (§5.1). A later session does the deep-dive and
writes the full schedule (as `S089_P2_IMPLEMENTATION.md` did for P2).
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
- **It only helps scenes with Drum voices in `fm`.** **Answered (user,
  2026-10-30):** the worst-case Scene uses exactly the three dearest FM
  carriers, `slp`, `smp` and `wtb` (sample and wavetable types). Reducing
  them is a real reduction of the worst case that sets the ceiling. The
  cheaper carriers do not matter, because they already cost less than the
  new worst case.

## 5. Options to decide in the implementation session

| Option | Saving per Drum-`fm` voice | ITCM | Risk |
|---|---:|---:|---|
| **A. Full fold-in:** ramped copies of every FM carrier loop | ≈ −9 | ≈ +3.0 KB (≈99 % full) | the most code; ITCM nearly exhausted for future work |
| **B. Partial fold-in:** ramped copies only for the carriers above the new worst case (`slp`, `wtb` blend with its plain-`wtb` fallback, `smp`; §5.1); keep the pass for the cheaper carriers | the worst case drops ≈ −9 (90 → 81); the cheaper carriers keep today's cost | ≈ +2.2 KB | two mechanisms in one branch; the worst case must be re-derived for every carrier type |
| **C. Cheaper pass, no fold-in:** an integer `SMULWB` ramp (≈6–7 per sample), or packed two-per-word (≈4.5) | ≈ −3 to −5.5 | 0 | tiny change; armed-block rounding differs slightly (S2); unarmed `s` = 1.0 must stay exact (Q16 65536 → identity) |
| D. Leave it | 0 | 0 | — |

### 5.1 Decision: option B, three carriers (user, 2026-10-30)

The carrier ranking, measured in the S089 P2 build. "Today" includes the
dearest modulator (Crash, 30) and the depth-ramp pass (10); "Folded" is the
carrier loop + 1, without the pass.

| Carrier (osc 1 type) | FM loop | Today | Folded | Covered by B |
|---|---:|---:|---:|:-:|
| `slp` (looped sample), `osc_slpFmBlock` | 50 | **90** | 81 | ✓ |
| `wtb` blending, `osc_wtbFmBlendBlock` | 46 | **86** | 77 | ✓ |
| `smp` (one-shot sample), `osc_smpFmBlock` | 42 | **82** | 73 | ✓ |
| Crash, `calcSampleOscFmBlock` | 38 | 78 | — | keeps the pass |
| Table waves, `calcFmBlock` | 35 | 75 | — | keeps the pass |
| `wtb` not blending, `osc_wtbFmBlock` | 32–34 | 74 | (73–75 as the blend fallback) | the fallback copy only |
| Sine (in `calcNextOscSampleFmBlock`) | 29 | 69 | — | keeps the pass |
| Noise (FM ignored) | 10 | 50 | — | keeps the pass |

**Why exactly these three.** Every carrier above the new worst case (≈81,
`slp` folded) must be covered; every carrier already at or below it may
keep the pass. Crash (78) was in the first draft of option B, but it does
not need a copy. The user's worst-case Scene uses exactly `slp`, `smp` and
`wtb`, so this is a real reduction of the ceiling.

**What to build (for the implementation session to confirm in its
deep-dive):**
- **Ramped copies** (new functions, own ITCM subsection, Drum `fm` only)
  of `osc_slpFmBlock`, `osc_smpFmBlock` and `osc_wtbFmBlendBlock`.
  `osc_wtbFmBlendBlock` falls back to `osc_wtbFmBlock` when no blend slot
  is free, so the copy needs a ramped fallback as well; otherwise those
  blocks would need the old pass, two mechanisms on one path. ITCM:
  604 + 324 + 860 + 412 B ≈ **2.2 KB** of the 3,352 B free (≈ 1.1 KB left).
- **A ramped typed-FM dispatcher**, in flash, that selects among the three
  by `oscType`.
- **Drum `fm` branch.** When osc 1 is `slp`/`smp`/`wtb`, call the ramped
  dispatcher with (d0, dStep) and **skip** the modulator-scaling pass. For
  every other carrier, keep today's pass and the existing loops unchanged.
  The choice depends on the oscillator type (a configuration), not on
  whether the ramp is armed, so it does not conflict with the
  constant-cost rule.
- **Unchanged:** Cymbal/HiHat and every existing loop. All existing ITCM
  addresses stay as they are (pin `.itcm.s089`, then the new subsection,
  in the linker script).
- **Gate:**
  - each ramped loop is at most its original + 1;
  - Drum `fm` with `slp`/`wtb`-blend/`smp` drops by about 9 per sample;
  - the uncovered carriers and Cymbal/HiHat are byte-identical;
  - unarmed blocks are S0 (a host law per covered carrier);
  - on hardware, the worst-case Scene's `cpu` reading is **below** today's
    100 %.

The other options remain recorded for reference only.

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
