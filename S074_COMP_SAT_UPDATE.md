# S074 — Bus compressor saturation: HF harshness assessment and plan

**Status:** **implemented and accepted on hardware** (user, 2026-09-30:
"saturator seems better; let's leave it like that for now"). Option A was
approved earlier the same day ("sounds ok, let's try it; RAM expansions
approved"). The build and host replica checks are in §6, and the hardware
result is in §6.6. The constants stay at their starting values (α = 0.25,
2 kHz crossover, knee from 0.75 FS).

**Request (user, 2026-09-30):** at high `cam` the saturation sounds harsh on
high-frequency content such as hi-hats. The amount is not necessarily the
problem; can a softer algorithm be used? A *teensy tiny* bit more CPU is
acceptable.

**Code assessed:** `Core/DSPAudio/BusCompressor.c` at HEAD `2ec72d6`,
including the S074 tuning revision (`d = 1 + 0.5·a²`, makeup
`× (1 − 0.3a)`).

---

## 1. Short answer

- **Why it is harsh:** the harshness is mostly **aliasing and
  intermodulation**, not the amount of saturation.
  - The cubic saturator runs at the base sample rate (44.1 kHz) with no
    oversampling.
  - A cubic produces a 3rd harmonic. For any content above 7.35 kHz that
    harmonic lies above the Nyquist limit (22.05 kHz) and **folds back as an
    inharmonic tone**: a 10 kHz hi-hat partial produces a folded tone at
    14.1 kHz, and a 12 kHz partial one at 8.1 kHz.
  - Hi-hats and cymbals are made of exactly those partials.
  - They also ride on the kick's waveform through the same curve, so the kick
    modulates them (intermodulation).
- **What to do:** saturate the lows and pass most of the highs around the
  saturator.
  - Split the signal just before the saturator with a one-pole crossover
    (about 2 kHz).
  - The low band goes through the saturator in full; only about a quarter of
    the high band does, and the rest bypasses it untouched.
  - The two bands sum back to exactly the input wherever the saturator is
    linear, so tone and level are unchanged for clean signals.
- **Result (simulated at `cam` 127, §2.2):**
  - distortion and aliasing in 2–20 kHz fall by **10–14 dB** on a kick-plus-hats
    mix;
  - the folded 3rd harmonic of a hi-hat tone falls by **26 dB**;
  - saturation of the low end (kick, bass, body) is unchanged, so the
    character stays.
- **A full-scale safety knee comes with it.** The bypassed highs no longer
  pass through the saturator's ceiling, so a smooth, constant-cost knee from
  −2.5 dBFS up to full scale keeps the output below the pack's hard 24-bit
  clip.
- **Cost:**
  - CPU: about **+0.6–0.8 %** of the 216 MHz core while the compressor is
    on, measured after implementation (the planning estimate was +0.45 %;
    see §6.3);
  - RAM: **+8 B DTCM**, taking the state from 24 to 32 B, which is the
    already approved ceiling. The FX arena is unchanged.

---

## 2. Assessment

### 2.1 The current per-sample stage

For each channel (`BusCompressor.c`, per-sample loop):

```
u   = clamp(x · g · d/1.5, −1, 1)          d = 1 + 0.5a²  (1.5 at cam 127)
out = 1.5/d · (u − u³/3)                   unity small-signal gain, ceiling 1/d
```

- **Only a 3rd harmonic below the clamp.** Below the clamp (`|u| < 1`) the
  curve is a pure cubic, so a single tone gets **only** a 3rd harmonic. At
  `cam` 127 and −2 dBFS that harmonic is at −24 dBc.
- **It folds for high content.** For content above `fs/6 ≈ 7.35 kHz` the
  3rd harmonic is above Nyquist and folds back **inharmonically**. It is no
  longer musically related to the note. This is the "fizz" heard on hats.
- **Lower-frequency content is fine.** Kick, bass and snare body produce
  harmonics that stay below Nyquist. That is the warm saturation you want.
- **Intermodulation.** When hats and kick are summed, the hats pass through
  the curve at whatever point the kick has pushed it to. The kick therefore
  modulates their gain and adds sidebands; these land in the treble too.
- **High `cam` makes all of this worse.** Drive rises (`d` up to 1.5), and
  makeup lifts quiet material such as hats between kicks, so they hit the
  curve harder.

### 2.2 Measurements (simulation of the firmware formula)

These are pure-Python models of the per-sample stage at `fs = 44,108 Hz`
(scratchpad scripts `satsim.py`, `satsim2.py`).

**Single tones at `cam` 127 (`d = 1.5`).** Levels of the folded 3rd harmonic,
and of the fundamental relative to the input:

| Tone, level | Folds to | Current cubic | ADAA-1 cubic | **Split, 35 % of highs at 2.5 kHz** | **Split, 25 % at 2.0 kHz** |
|---|---|---|---|---|---|
| 10 kHz, −2 dBFS | 14.1 kHz | −24.2 dBc (fund. −1.5 dB) | −41.4 dBc (**fund. −3.5 dB**) | −43.6 dBc (fund. −0.2 dB) | **−49.9 dBc** (fund. −0.1 dB) |
| 12 kHz, −4.4 dBFS | 8.1 kHz | −29.6 dBc | −47.1 dBc (fund. −4.2 dB) | −49.3 dBc | **−56.0 dBc** |
| 4 kHz, −2 dBFS (snare crack) | 12 kHz (true harmonic) | −24.2 dBc | −25.8 dBc | −36.5 dBc | −41.5 dBc |
| 100 Hz, −2 dBFS (low end) | 300 Hz (true harmonic) | −24.2 dBc | −24.2 dBc | **−24.2 dBc (unchanged)** | **−24.2 dBc (unchanged)** |

**Mixes.** The kick is 60 Hz; the hats are four partials between 7.1 and
13.9 kHz. The measure is all distortion, aliasing and intermodulation energy
in 2–20 kHz relative to the hat energy, with the fundamentals excluded. The
split columns include the §3.3 knee.

| Case | Current cubic | Split, 35 % at 2.5 kHz | **Split, 25 % at 2.0 kHz** | Hat level change | Output peak (current → split 25 %) |
|---|---|---|---|---|---|
| `cam` 127, kick −7 dBFS + hats | −19.9 dB | −27.9 dB | **−30.4 dB** | +0.7 dB | 0.66 → 0.82 FS |
| `cam` 127, hat-heavy | −22.0 dB | −32.7 dB | **−35.5 dB** | +0.8 dB | 0.67 → 0.94 FS |
| `cam` 48 (default), kick + hats | −26.4 dB | −34.0 dB | **−36.4 dB** | +0.4 dB | 0.78 → 0.85 FS |

**Reading the tables:**

- **The split removes most of the harsh products**, both the folded
  harmonics and the kick-driven intermodulation, and leaves low-end
  saturation exactly as it is.
- **The hats come out about 0.4–0.8 dB louder,** because their peaks are no
  longer rounded by the saturator. This is the audible face of "less harsh".
  If the top end feels louder at high `cam`, makeup can be trimmed a little
  more.
- **Output peaks rise towards full scale.** The bypassed highs skip the
  `1/d` ceiling, so the §3.3 knee is needed. Without it the hat-heavy case
  peaks at 0.97 FS, and louder material would reach the pack's hard clip,
  which is harsher still.

### 2.3 Options considered

| Option | HF harshness | Side effects | CPU (per stereo frame) | RAM | Verdict |
|---|---|---|---|---|---|
| **A. Band-split saturation + full-scale knee** | −10 to −14 dB on mixes; −26 dB on the folded 3rd harmonic | Hats about 0.4–0.8 dB brighter at high `cam`; peaks rounded only above −2.5 dBFS | planned about +20–25 cycles (+0.45 %); measured +37 instructions (about +0.6–0.8 %, §6.3) | +8 B (fits the approved 32 B) | **Recommended; implemented** |
| B. First-order anti-derivative anti-aliasing (ADAA-1) on the cubic | −17 dB on the folded 3rd harmonic | **Treble loss on the whole bus**, even when clean: a two-tap average, −2.4 dB at 10 kHz, −6 dB at 15 kHz (measured −2 to −3.5 dB at the fundamental). Half-sample delay. An ill-conditioning branch. | about +40 cycles (two divisions) | +8 B | Rejected: it dulls the hats |
| C. Pre-/de-emphasis around the cubic (treble cut before, boost after) | about −9 to −18 dB | The boost after the saturator **re-amplifies** the distortion that lands high; needs a knee as well | about +16 cycles | **+16 B (exceeds the 32 B approval)** | Worse than A at more RAM |
| D. 2× oversampling (polyphase half-band filters) | removes aliasing properly | Latency; treble ripple near the band edge | +150–250 cycles | +100–200 B | Rejected: far beyond "teensy" |
| E. A smoother curve alone (e.g. a C2 quintic, `tanh`) | none: below the clamp the cubic already makes only a 3rd harmonic; a smoother curve still makes it | More low-level 3rd harmonic for the quintic | +2 to +30 cycles | 0 | Does not address the cause |

---

## 3. Plan (option A)

### 3.1 Signal flow (per channel, per sample)

```
x ──► one-pole LP (fc ≈ 2.0 kHz) ──► L = LP(x)
      H = x − L                                   (exact complement: L + H = x)

s   = L + α·H                                     α = 0.25: 25 % of the highs saturated
u   = clamp(s · g · d/1.5, −1, 1)
sat = 1.5/d · (u − u³/3)                          (unchanged cubic, unchanged cam curves)
wet = sat + (1 − α)·H·g                           75 % of the highs bypass, linear
wet = knee(wet)                                   smooth ceiling at full scale (§3.3)
out = x + w·(wet − x)                             (unchanged one-block fades)
```

- **The split is invisible when clean.** Where the cubic is linear
  (`sat ≈ s·g`), `wet = (L + αH + (1 − α)H)·g = x·g`. The stage is
  transparent there, exactly as today.
- **Unchanged:** the detector (it still uses the input `x`), the cell, the
  makeup, the sidechain and the `cam`/`ctm` curves.

### 3.2 Constants (tuning starting points)

| Name | Value | Meaning |
|---|---|---|
| `BUS_COMP_SPLIT_HZ` | 2,000 Hz | Crossover corner, as a one-pole low-pass: `k = 1 − exp(−2π·fc/fs)` = 0.248; folds to a constant at compile time |
| `BUS_COMP_HF_SAT_SHARE` (α) | 0.25 | The share of the high band that goes through the saturator. 0.35 keeps more snare and cymbal "bite" (tested: still −8 to −11 dB). |
| `BUS_COMP_KNEE_START` | 0.75 FS (−2.5 dBFS) | Where the safety knee begins |
| `BUS_COMP_KNEE_CEIL` | 1.0 FS | The knee's ceiling: never above the pack's 24-bit range |

α could be made to follow `cam` (for example `0.35 → 0.2`), but a fixed
value is proposed because drive `d` already scales with `cam`.

### 3.3 Full-scale safety knee (constant cost, branchless)

- **Shape:** a C1 quadratic knee applied to `wet`, in the loop's int32-unit
  floats. It is linear below `T0` and reaches `C` with zero slope:

  ```
  a  = |wet|
  t  = min(max(a − T0, 0), 2·(C − T0))
  a  = min(a − t²/(4·(C − T0)), C)
  wet = copysign(a, wet)
  ```

  With `T0 = 0.75·FS` and `C = FS`, it is linear up to −2.5 dBFS, rounds from
  0.75 to 1.0 FS, and limits at 1.0 FS from 1.25 FS input upwards.
- **Branchless:** it is computed every sample, with no branch. This keeps
  the per-sample cost constant, in line with the DSP CPU policy (no work
  skipped because it is currently inactive).
- **Why it is needed:** it restores the "never reaches the pack's hard clip"
  guarantee that the cubic's `1/d` ceiling gave before. That guarantee now
  holds at full scale rather than at `1/d`. Low-band content is still held by
  the cubic's `1/d` ceiling.

### 3.4 Code outline (`busComp_processBlock()`, per-sample loop)

This is an outline for the implementation schedule, not final code:

```c
/* per block */
const float k_lp  = busComp_coefHz(BUS_COMP_SPLIT_HZ);   /* constant-folded */
const float c_hf  = c_out * (1.0f - BUS_COMP_HF_SAT_SHARE);
float lp_l = busComp.split_lp[0];
float lp_r = busComp.split_lp[1];

for (i = 0u; i < OUTPUT_DMA_SIZE; i++) {
    const float xl = (float)buf[2u * i];
    const float xr = (float)buf[2u * i + 1u];
    float hl, hr, ul, ur, g_hf;

    gk += gk_step;
    w  += w_step;
    g_hf = gk * c_hf;                         /* (1 − α)·g in int units */
    sum += xl * xl + xr * xr;                 /* detector: unchanged */

    lp_l += k_lp * (xl - lp_l);   hl = xl - lp_l;
    lp_r += k_lp * (xr - lp_r);   hr = xr - lp_r;

    ul = fminf(fmaxf((lp_l + BUS_COMP_HF_SAT_SHARE * hl) * gk, -1.0f), 1.0f);
    ur = fminf(fmaxf((lp_r + BUS_COMP_HF_SAT_SHARE * hr) * gk, -1.0f), 1.0f);
    ul = ul * (c_out + c_out3 * ul * ul) + hl * g_hf;
    ur = ur * (c_out + c_out3 * ur * ur) + hr * g_hf;
    ul = busComp_knee(ul);                    /* §3.3, branchless, inline */
    ur = busComp_knee(ur);

    buf[2u * i]      = (sample_mx_t)(xl + w * (ul - xl));
    buf[2u * i + 1u] = (sample_mx_t)(xr + w * (ur - xr));
}
busComp.split_lp[0] = lp_l;
busComp.split_lp[1] = lp_r;
```

**Fade-in reseed:** the §4.6 seed also zeroes `split_lp[]`. The low-pass
settles in about 4 samples, inside the dry→wet crossfade.

### 3.5 Resources

| Resource | Change |
|---|---|
| **RAM** | `float split_lp[2]` in `bus_comp_state_t`: **+8 B DTCM**, from 24 to **32 B**. This is the ceiling you approved for this state (answer 1), and the existing `_Static_assert(sizeof(bus_comp_state_t) <= 32u)` still holds. `_edtcmz` moves from 0x20001178 to 0x20001180, which is already 32-byte aligned, so **the FX arena stays 126,592 B**. |
| **CPU** | Planned: about 25 floating-point operations per stereo frame, about +0.45 %. **Measured after implementation (§6.3): the per-sample loop grew from 34 to 71 instructions per stereo frame, about +0.6–0.8 %** at 216 MHz. The compressor goes from about 0.8–1 % to about 1.4–1.8 % while on, and stays at 0 while `off`. The cost is constant (no data-dependent branches). |
| **Flash** | About +100 B. |
| **Latency** | None added. The crossover is complementary, so there is no delay mismatch. |

---

## 4. Verification

1. **Build:**
   - no new warnings;
   - `link_budget.py` shows DTCM statics 4,472 → 4,480 B and the FX arena
     unchanged at 126,592 B;
   - `nm` reports `bus_comp_state_t` at 0x20 bytes.
2. **Transparency:** with a clean signal (`cam` 0, low level), the output
   equals the input within rounding, as today.
3. **Listening (yours):**
   - hats and cymbals at `cam` 96–127: the fizz is gone;
   - kick and bass saturation is the same as now;
   - snare crack is a little cleaner;
   - A/B with α = 0.25 against 0.35 to choose how much bite remains.
4. **Level:** the top end is about 0.5–0.8 dB brighter at high `cam`. Trim
   makeup a little if needed.
5. **Clip guarantee:** drive a hat-heavy loop at `cam` 127 into the knee.
   With the output recorded, peaks stay at or below full scale and there is
   no hard-clip crackle.
6. **CPU:** read the worst-case Scene with `cmp` on, before and after (about
   +0.45 % expected).

---

## 5. Decisions

1. **Option A adopted** (band split plus the full-scale knee): user,
   2026-09-30.
2. **High-band share:** α = 0.25, the proposed starting point. It is a single
   constant, `BUS_COMP_HF_SAT_SHARE`, so trying 0.35 is a one-line change.
3. **RAM:** approved. The two crossover states take the compressor state
   from 24 to 32 B of DTCM.
4. The user asked for the change to be written directly into the code, with
   adjacent comment blocks and working notes here (§6), instead of a separate
   schedule.

---

## 6. Implementation notes (2026-09-30)

**Base:** HEAD `2ec72d6`. At the time, the AutoSave torn-record fix
(`S074_ATS_BUG_IMPLEMENTATION.md`) was being applied by the user to
`filesystem.c`, `AutosaveTrace.h` and `tools/decode_devlogs.py`. This change
touches only `Core/DSPAudio/BusCompressor.c`, the `BusCompressor.h` contract
comment, and this document.

### 6.1 Code changes

**`Core/DSPAudio/BusCompressor.c`** (every change has an adjacent comment
block):

1. **New constants** after `BUS_COMP_SAT_SPAN`:
   - `BUS_COMP_SPLIT_HZ` 2,000 Hz and `BUS_COMP_SPLIT_COEF`
     (`1 − expf(−2π·fc/REAL_FS)` = 0.2479, folded at compile time);
   - `BUS_COMP_HF_SAT_SHARE` 0.25 (α);
   - `BUS_COMP_CEIL_START` 0.75 (the knee start, as a fraction of full
     scale).

   These are distinct from the existing `BUS_COMP_KNEE_DB`, which is the
   compressor's static-curve knee.
2. **`bus_comp_state_t`:** new `float split_lp[2]` (crossover low-pass state
   per channel, int32-unit floats), placed before `active`. The size is now
   32 B, and the existing `_Static_assert(... <= 32u)` holds. The state
   comment is updated (32 B, user-approved 2026-09-30).
3. **New `busComp_ceilingKnee()`** (static inline, after
   `busComp_staticGainDb()`):
   - identity below 0.75 FS;
   - a C1 quadratic bend to exactly full scale at 1.25 FS input;
   - held at full scale above that;
   - sign preserved;
   - branchless, as the DSP CPU policy requires.
4. **`busComp_processBlock()`:**
   - new locals `c_hf`, `lp_l`, `lp_r`;
   - the fade-in seed zeroes `split_lp[]`;
   - per block: `c_hf = c_out·(1 − α)`, and the crossover states are loaded
     into locals;
   - per sample: `g_hf = gk·c_hf`; the crossover `L += k·(x − L)`,
     `H = x − L`; the cubic on `(L + α·H)·gk`; the bypass `+ H·g_hf`; the knee;
     the unchanged fade;
   - after the loop, the crossover states are stored back.

   The detector, the cell, makeup, the sidechain and the `cam`/`ctm` curves
   are unchanged.

**`Core/DSPAudio/BusCompressor.h`:** the `busComp_processBlock()` output
contract now describes the band-split saturation and the full-scale knee.
It is comment-only.

### 6.2 Build (`make all`, 2026-09-30)

The working tree also contained the user's in-progress AutoSave fix edits;
the build passed with them.

| Check | Result |
|---|---|
| Warnings | None from `BusCompressor.c`. Only the pre-existing newlib `_close`/`_lseek`/`_read`/`_write` linker notes. |
| `busComp` symbol | `busComp.lto_priv.0`, **0x20 = 32 B** (was 24) |
| DTCM statics | **4,472 → 4,480 B** (+8) |
| FX arena | **126,592 B** at 0x20001180, **unchanged** (the base was already 32-byte aligned); margin 3,712 B |
| `bss` | 426,384 → **426,392** (+8, the approved crossover state) |
| Module flash | `BusCompressor.o` `.text` 1,104 → 1,332 B (+228 B), from a standalone compile |
| Per-block library calls | Still only `log2f` and `exp2f`. **No `expf`**: the crossover coefficient is folded at compile time (checked in the disassembly). |

### 6.3 Code-generation check (standalone `-Ofast`, not LTO)

- **Per-sample loop:** **34 → 71 instructions per stereo frame**, of which
  63 are floating-point.
- **The knee is branchless.** `copysignf` compiles to `vabs` plus a
  predicated `vneglt` (`it lt`), with no jump. The only branch in the loop
  is its back-edge.
- **CPU estimate revised:** about +37 instructions per frame × 44,108 frames/s
  ≈ 1.6 M instructions/s, which is **about +0.6–0.8 %** of 216 MHz depending
  on dual issue. That is above the planning estimate of +0.45 %, because of
  the knee's sign handling and a few register copies the compiler inserted
  around the fused multiply-adds.
- **If that is too much**, the knee could start higher (0.85 FS) at the
  same cost, or be dropped at the price of the full-scale guarantee. Neither
  is recommended. Read the worst-case Scene on the `cpu` widget first.

### 6.4 Host replica of the loop arithmetic

`scratchpad/fwreplica.py` reproduces the firmware formulas exactly: int32
units, `c_in`/`c_out`/`c_hf`, the crossover, the cubic and the knee constants.

| Test | Result |
|---|---|
| Clean −40 dBFS tone at 100 Hz, 2 kHz and 10 kHz; `g` = 1, `d` = 1 | Output equals `g·x` within −97 to −121 dB (transparent) |
| The same at `g` = 3.55 (+11 dB makeup), `d` = 1.5 | Within −68 to −92 dB. What remains is the cubic's normal low-level curvature on the low band, as before. |
| Overdriven hats + kick (a 9 kHz peak at 3 FS plus 60 Hz at 1 FS), `d` = 1.5 | Maximum \|out\| = **1.000 FS**: the knee holds full scale |

The band-split distortion and aliasing figures in §2.2 were simulated with
the same formulas, so they apply to this implementation.

### 6.5 Remaining (user)

1. ~~**`make img`** and flash.~~ Done (§6.6).
2. ~~**Listening (§4).**~~ Done (§6.6). `BUS_COMP_HF_SAT_SHARE` 0.35 was not
   tried; α stays at 0.25.
3. **The `cpu` widget** on the worst-case Scene, with `cmp` on against off.
   No figure is recorded yet.
4. **Documentation follow-ups (now due, since the change is accepted):**
   - update `S074_BUS_COMP.md` §4.5 (the saturator is now band-split, with
     a full-scale knee instead of the `1/d` ceiling), §5 (CPU) and §6 (32 B
     state);
   - update the RAM line in `STORAGE_SRAM_MANIFEST.md`;
   - update the `MEMORY.md` session log.

### 6.6 Hardware acceptance (2026-09-30)

- **Your verdict:** "saturator seems better; let's leave it like that for
  now."
- **Result:** the band-split saturation with the full-scale knee is
  accepted as implemented in §6.1, with no further tuning for now.
- **Constants unchanged:**
  - `BUS_COMP_SPLIT_HZ` 2,000 Hz;
  - `BUS_COMP_HF_SAT_SHARE` 0.25;
  - `BUS_COMP_CEIL_START` 0.75.
- **Not recorded in this acceptance:**
  - an A/B test of α = 0.35;
  - a `cpu` widget reading on the worst-case Scene (§6.5 item 3).
