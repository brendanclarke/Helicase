# DRAFT — S-R PWM wave: the Alpha Juno chopped saw

- **Status:** draft proposal, 2026-11-18, tree `756b5b70` on
  `dev-ph7-alkaline`. **No firmware code is changed by this document.**
  Figures marked *prototype* come from a Python model that reads the real
  `sawTable` rows parsed from `Core/DSPAudio/wavetable.c` (§5, Appendix A).
  Figures marked *estimate* are not measured. Both kinds are measured again
  on a linked image before a schedule is written.
- **Decided (user, 2026-11-18):**
  - The Colloid must run **six** chop oscillators.
  - The **truncated-read form (T)** will be tried later (§6.3, D-8).
  - Form S and the ITCM moves (§6.3, "Freeing more ITCM") are held in
    reserve, for use if form T fails the ear check.
  - 2026-11-19: a second new wave, **`REx`**: a rising exponential saw,
    chopped exactly like `R-S`, with one fixed curve chosen by ear and no
    curvature control (§6.4, D-11).
  - 2026-11-19: names `R-S` (chopped saw) and `REx` (chopped exponential)
    (D-1). **Six oscillators for both in the Colloid**; eight was assessed
    and does not fit (§6.5).
  - No implementation is scheduled yet.
- **Brief (user, 2026-11-18):** add another PWM waveform, the Alpha Juno's
  PWM saw. The current PWM Saw (the two-saw sum) is good, but it is wrong
  for some sounds. The Alpha Juno's wave is said to be a plain rising saw
  chopped by a pulse at twice the frequency. Research it and propose what
  can be built.
- **Binding rules that apply:**
  - S085/§12 (`INSTRUMENTS_DSP_REFERENCE.md`): no voice's dearest setting
    may rise. New code goes in separate loop copies, one per role. Never
    edit an existing loop to add a feature.
  - S089 P2 / S094 P2 §21: the CPU ceiling, now gated in measured cycles
    per block (Basic budget 19,620).
  - S093: per-block work counts, and VDIVs are counted separately.
  - Constant CPU: budget the dearest mode, and never skip work.
  - S086 P2: a new wave "the same way" means the same mechanism. Here that
    is the shipped `sawTable` rows read by the PWM two-read pattern, with no
    new kind of table.
  - No backward compatibility and no migration.
  - ITCM: new code goes only in a new `.itcm.sNNN` subsection, and no
    existing ITCM function's section changes.

---

## 0. Summary

1. **What the Alpha Juno does.** Its DCO's "PWM saw" (Saw 3) is a rising
   ramp **chopped to ground** by a pulse at **+1 octave**. While the pulse is
   high the ramp passes. While it is low, the output sits at the ramp's
   floor. The pulse width is variable. 50 % gives the fixed Saw 2. Narrower
   widths give Saw 3 under PW/PWM. Saw 4 and Saw 5 chop at +3 octaves, or at
   both +1 and +3 octaves (§1).
2. **The wave is not a sum of saws.** Its flat sections put kinks (slope
   changes) into the wave, and no mix of saw reads can make a kink. An exact
   band-limited build needs about five table reads plus a band-limited
   parabola row (§3, option B). That is far over every role's budget.
3. **Proposed form (D): a plain ramp times a band-limited 2× gate**, with
   the ramp's wrap moved into the middle of the last chopped section:
   `y = ramp_w(φ) × (pulse_bl(2φ) + c) − dc`.
   - The gate is the shipped PWM Rec two-read (`osc_pwmReadD()`, sign −1) on
     a `sawTable` row one or two octaves up, read at twice the phase.
   - The ramp costs two integer instructions.
   - Moving the wrap is what makes this work. It cuts aliasing from −39/−21/
     −14 dB to −72/−73/−58 dB (110/440/1,760 Hz, *prototype*, §5.3).
   - Cost: the D loop plus about 4 instructions per sample (*estimate*).
4. **Quality (*prototype*).** With a one-octave row lift, alias energy
   matches the shipped PWM Saw/Rec at the same pitch, and harmonics below
   10 kHz are within 0.7 dB of the ideal wave. With a two-octave lift,
   aliasing falls to about −55…−73 dB up to 1.7 kHz, but the top octave of
   the spectrum is about 10 dB darker (§5).
5. **Hosts, in this order:**
   - **Stage 1: Alkaline**, recommended first. A new fused ladder mode 5 in
     flash, with no ITCM. Its cost (≈ pwm 256.6 + ~5, *estimate*) stays
     under the Alkaline's dearest setting (`wtb` with a slot held, 276.7).
   - **Stage 2: drum-family osc1** (Drum, Snare, Cymbal, HiHat). This is
     **blocked on ITCM**: 5 role loops need about 0.9 KB, and **424 B are
     free** (15,960 / 16,384, measured on the current `build/lxr02.elf`).
     A user decision is needed (§6.2).
   - **Colloid, 6 oscillators (§6.3).** It can stand alone; it does not
     need stage 2. The plan is one ITCM add-pair loop (≈ 300 B of the 424 B
     free), called three times, after a flash pass that starts the buses at
     the stack's DC.
     - Only the **truncated-read form** fits the CPU budget: about −30
       instructions per sample against today's pwm Saw/Rec 6, which is at
       19,136 of 19,620 cycles. That figure uses the spike's measured loop:
       54 per pair against the shipped pair's 66 (§6.5).
     - Interpolated reads would be about 1,300 cycles over.
     - **Eight oscillators do not fit** (§6.5): about 20,850 cycles, roughly
       1,250 over the budget.
     - **Freeing ITCM does not close that gap.** Nothing in ITCM is dead
       code. Moving the per-block helpers to flash frees about 810 B. The
       most ITCM placement can save the Colloid is its cold I-cache penalty,
       about 470 cycles, as measured.
     - The fallback is form S: one interpolation on a shared fraction. It is
       estimated at about 19,710 cycles, or about 19,240 with the ITCM
       moves. That needs a spike and a probe
       run. It is used only if form T fails the ear check.
6. **Decisions for the user:** §9. D-8 is decided: try form T first.

---

## 1. Research: what the Alpha Juno does

### 1.1 Sources and agreement

The main technical source is the KVR DSP forum thread "Roland Alpha Juno-2
pwm sawtooth" (2014–2023, pages 1–6). It includes hardware measurements by
aciddose and comments from u-he (Urs). Supporting sources: gearnews, and the
Juno-family DCO write-up by Thea Flowers (the comparator orientation of the
Juno 6/60/106; it does not cover the Alpha Juno). Points the sources agree
on:

- **Mechanism.** The ramp is **chopped**, not ring-modulated. "When the
  pulse is high, the original sawtooth is let through; when it's low, the
  waveform goes low." The likely circuit is a resistor feeding the ramp into
  an NPN's collector, with the emitter grounded and the pulse on the base. So
  "low" means **ground**, which is the ramp's floor: the DCO ramp is a
  capacitor charging up from 0 V.
- **Waveform list** (aciddose, measured): Saw 1 is the plain ramp. Saw 2 is
  chopped by a **square at +1 octave**. Saw 3 is Saw 2 with an **adjustable
  +1-octave pulse width** (the PWM saw, the subject here). Saw 4 is chopped
  by a square at +3 octaves. Saw 5 is chopped by both. "The waveforms follow
  a binary pattern, as if the bits were used directly to enable a chopper
  circuit."
- **The pulse is phase-locked to the ramp.** It comes from the same
  phase-counter bits ("Roland used the bits of the digital phase counter to
  'chop' the waveform, or the PWM signal", u-he).
- **Things that are not the oscillator:**
  - A fixed high-pass (shelving) filter before or after the chorus bends the
    ramp segments visibly, depending on pitch. u-he advises perfect waves
    plus a separate high-pass, not baking the curve into the wave.
  - The chips' DC offset (0.65 V while any wave is on).
  - Scope traces show "rounded" ramp starts. These come from that filter
    and the integrator, not from the chopping.

### 1.2 What is not settled, and what this draft assumes

- **Which part of each pulse cycle passes the ramp.** No source states it
  outright. This draft assumes **high first**: the gate opens at the start
  of each half-cycle, so the ramp passes over `[0, w/2)` and
  `[1/2, 1/2 + w/2)` of the cycle. Reasons:
  - The Juno-family comparator turns its output *off* when the ramp rises
    past the PW voltage, so the pulse is high at the start of the cycle.
  - The shipped PWM Rec (two-read with B at `ph − w`) is already high for
    the first `w` of the cycle, so the gate is that read with no change.
  - The other orientation (pass the upper part of each half) costs the same:
    B is read at `ph + w`. It is a by-ear choice (D-4).
- **The narrowest Alpha Juno width.** Unknown. §4.5 proposes the shipped
  Saw floor (2.54 %).
- **PW direction.** The Alpha Juno's PW 0 gives the square (Saw 2) and
  higher values narrow the pulse. That matches the shipped law: pos 0 =
  50 %, pos 127 = the narrowest.

---

## 2. The ideal wave

Phase `φ ∈ [0, 1)`, ramp `r = φ` (unipolar, floor 0), gate width `w`
(0 < w ≤ 1/2, the fraction of each 2× cycle that passes), and `ψ = frac(2φ)`:

```
g(φ) = 1 if ψ < w else 0
y(φ) = r(φ) · g(φ)
```

Over one cycle:
- the ramp passes over `[0, w/2)`, from 0 up to `w/2`;
- the output is 0 over `[w/2, 1/2)`;
- the ramp passes over `[1/2, 1/2 + w/2)`, from `1/2` up to `(1 + w)/2`;
- the output is 0 over `[1/2 + w/2, 1)`.

Features that matter for band-limiting:
- **Jumps:** −w/2 at `w/2`; +1/2 at `1/2`; −(1+w)/2 at `1/2 + w/2`. At
  `φ = 0` there is **no jump**: the gate opens where the ramp is 0.
- **Kinks** (slope 1 → 0 or 0 → 1) at all four gate edges.
- **Mean** (DC) `m(w) = w(1 + w)/4`. **Peak after DC removal**
  `(1 + w)(2 − w)/4`. This is 0.5625 at w = ½ and tends to 0.5 as w → 0.
- At w = ½ it is the Alpha Juno's Saw 2.

---

## 3. Ways to render it

| | Form | Per-sample cost | Verdict |
|---|---|---|---|
| A | A user wavetable: frames of the ideal wave over width, installed from `/wavetables` (up to 128 frames of 1,024 points) | the `wtb` reader (no new code) | **Usable today as an ear reference** (D-6). It has no per-octave band-limiting, so it aliases badly on high notes, and its width moves by frame scan, not by `pos`. |
| B | Exact band-limited sum: the 1× pulse (2 saw reads) + the 2× truncated ramp (a `sawTable` read + two reads of a **new band-limited parabola row**, the integral of a saw) | about 5 interpolated reads, about 40+ | Over every role's budget, and it needs a new table family (22.5 KB). Rejected. |
| C | Per-width band-limited tables in program flash | 1 read (2 for a width blend) | 128 widths × 11 octaves × 2 KB = 2.9 MB; even 16 widths are 360 KB, against **32,888 B** of flash headroom. Rejected. |
| **D** | **A naive ramp × a band-limited 2× gate, with the ramp wrapped inside a chopped section** (§4) | **D loop + about 4** | **Proposed.** It reuses `sawTable` and `osc_pwmReadD()`; only the loop is new. |

Why saw reads alone cannot work: every saw read adds a constant slope, so
any sum of saw reads has a constant slope between its jumps. The chopped
wave switches slope between 1 and 0. Those kinks need either integrated
(parabolic) components (option B) or a product, which is option D.

---

## 4. The proposed form (D)

### 4.1 The law

```
gate(φ) = pulse_bl(2φ) + c       band-limited, ≈ 1 while open, ≈ 0 while chopped
ramp_w(φ) = φ        for φ <  φw
          = φ − 1    for φ >= φw    (the same ramp, wrapped at φw instead of 0)
y(φ) = ramp_w(φ) · gate(φ) − dc
```

- **`pulse_bl(2φ)`** is the shipped two-read difference (`osc_pwmReadD()`
  with sign −1). It reads the descending `sawTable` row at `2φ` and at `2φ`
  minus `d` points, with `d = w × 1024`. Read at twice the phase, the pulse
  sits at +1 octave. The index and fraction come from `ph << 1`, and the
  compiler can fold that into the existing extract (`UBFX` from bit 21
  instead of bit 22). This is the same high-first orientation as the
  shipped Rec (plan finding 2 in `Oscillator.c`).
- **The row** is `sawTable[min(tableOffset + lift, 10)]` with `lift ≥ 1`,
  since the gate plays an octave higher than the note (§4.4).
- **The offset `c = w × span_r`** turns the zero-mean difference into a
  gate with a floor of 0. `span_r = 2 × A_r × 32,767`, where `A_r` is the
  row's naive saw amplitude, measured from its middle slope (*prototype*):
  rows 0–5 are 0.99–1.02; rows 6–10 are 1.05, 1.11, 1.22, 1.78 and 2.43,
  because the top rows are nearly sines. `span_r` is a generated table of
  11 entries.
- **The wrap point `φw = (3 + w)/4`** is the middle of the last chopped
  section `[1/2 + w/2, 1)`. In phase units this is
  `wPh = 0xC0000000 + d << 20`, computed from `d` per block with no table.
  - Why it is needed: the product's aliasing comes only from the ramp's
    wrap jump. Its size is `gate(φw) × 1`. At `φ = 0`, where the true ramp
    wraps, the band-limited gate is mid-edge (½), so a naive wrap there
    leaves a half-size naive jump.
  - In the middle of a chopped section the gate is ≈ 0, apart from ringing.
  - The ramp is still 0 at `φ = 0`, so the opening edge at `φ = 0` is a
    kink, as in the ideal wave.
  - *Prototype* (§5.3): wrap at the cycle end gives −39.1 / −21.0 / −14.2 dB;
    wrap mid-section gives −72.5 / −72.8 / −58.5 dB.
- **`dc = m(w) = w(1 + w)/4`** removes the wave's mean. The level table
  (§4.5) brings the peak to full scale.
- **Band-limiting.** The jumps of `y` are the gate's edges multiplied by the
  smooth ramp, so they are as band-limited as the gate. Multiplying by a
  locally linear ramp spreads the gate's spectrum by only a few low-order
  bins. The lift keeps that spread below Nyquist (§5).

### 4.2 Drum-family loop (integer; the D loops' pattern)

```c
/* per block, osc_pwmPlan() case CHOP: row, d, c, wPh, vOff, dc, gQ14 */
for (i = 0u; i < size; i++) {
	/* the 2x gate: the shipped Rec two-read at twice the phase, floor at 0 */
	const int32_t g = osc_pwmReadD(row, ph << 1, d, -1) + c;
	/* the ramp, Q15, wrapped at wPh (inside the last chopped section) */
	const int32_t v = (int32_t)((ph - wPh) >> 17) + vOff;   /* vOff = (wPh >> 17) - 32768 */
	/* (g x v) >> 16 - dc in one SMLAWB */
	const int32_t o = __SMLAWB(g, v, -dc);

	ph += inc;
	buf[i] = OSC_PWM_STORE(o, gQ14);
}
```

- **Added over `osc_pwmDPlainBlock()`:** SUB plus ADD-with-LSR for the
  ramp, ADD for `c`, and one SMLAWB. That is about +4 on the D form's 23,
  so ≈ 27 plain (*estimate*). The FM, FM ramp, ring and sync copies add the
  same: ≈ 34 / 35 / 35 / 39 against their role ceilings of 50 / 51 / 42 /
  41.
- **Range.** `v` stays within −8,192 … 32,766. That is because `wPh ≥ 3/4`
  of a cycle, so `vOff` is in −8,192 … −1, which fits SMLAWB's 16-bit
  operand. `g` reaches `span_r`, which is up to about 2^17.3 on row 10. SMLAWB keeps the top 32 bits of a 48-bit product, so the
  multiply cannot overflow. The store's `o × gQ14` must stay under 2^31.
  The plan folds `65,534 / span_r` into `gQ14` (one multiply per block), and
  a generator check (§8) proves the bound for every row and pos.

### 4.3 Alkaline (float; fused ladder mode 5)

A new branch `fused == 5` in `dl_renderB2Body()`, following the precedent of
S094 P4 mode 4. Other wrappers pass 0 and the compiler drops the branch.
There is a new wrapper `DiodeLadder_renderB2OscChop()` in flash (noinline),
and `alk_renderPwm()` dispatches `ALK_WAVE_RS` to it.

```c
} else if (fused == 5) {
	const uint32_t itg = (ph >> 21) & 0x3FFu;          /* the 2x gate's point */
	const uint32_t ib  = (itg - pwD) & 0x3FFu;
	/* form 2 reads, as mode 3: both points truncated */
	const float g = (float)(row[itg] - row[ib]) + chC;
	/* the ramp wrapped at wPh: (ph - wPh) UNSIGNED, x 2^-32, + vOff
	 * (chVOff = wPh x 2^-32 - 1: ph < wPh gives phi, ph >= wPh phi - 1;
	 * a signed conversion would wrap wrongly for wPh - 2^31 <= ph < wPh) */
	const float v = (float)(uint32_t)(ph - wPh) * 2.3283064e-10f + chVOff;

	ph += phaseInc;
	s = (v * g - chDc) * sScale;
}
```

- **Added over mode 3** (215 in the loop, linked, S094 P1 S5.8): about +5,
  namely VMOV/VCVT, VADD for `vOff`, VADD for `c`, and the FMA replacing
  mode 3's signed add. That gives about **256.6 + 5 ≈ 262 per sample**
  (*estimate*). This is below the Alkaline's dearest setting, `wtb` with a
  slot held (276.7), and below `wav` without a slot (263.7). The voice's
  worst case does not move.
- **Truncated reads.** Form 2 truncates the gate's points, which moves each
  band-limited edge to the nearest 1/2,048 of a cycle. Mode 3 accepted the
  same error (−29 dB RMS against the interpolated form, which the ladder
  hides). Stage 1 measures it with law AL-CH (§8). If it fails, the
  fallback is interpolating the gate (about +6, mode 3's form 0 premium),
  which is still under 276.7.
- **Polarity.** The Alkaline's `wav` Saw falls, like the 303's. The Alpha
  Juno's chopped ramp **rises**, so mode 5 renders it rising, as in §2. A
  sign flip of the whole wave sounds the same on its own, so this needs no
  decision.

### 4.4 Row lift

The gate is a pulse at `2f`, so its row is `tableOffset + 1` (the octave
`2f` falls in). Lift 2 halves the gate's bandwidth again. Both are measured
in §5:

- **Lift 1:** alias energy about equal to the shipped PWM Saw/Rec at the
  same pitch. Harmonics below 10 kHz are within 0.7 dB of the ideal, and
  10–20 kHz is down about 2.5 dB. It is weak at the narrowest widths on
  high notes: −15 dB at 1,760 Hz with w = 0.06, against the shipped Rec's
  −25.6 dB.
- **Lift 2:** −55…−73 dB of alias energy up to 1,760 Hz, at every width
  tested. The 5–10 kHz band is down 1–3 dB and 10–20 kHz about 10 dB.
- **Proposal (D-3):** a generated `oscPwmChopLift[128]`, following the
  `oscPwmTriLift[pos]` precedent. It uses 1 for wide gates and 2 where
  lift 1's alias at that pos exceeds the shipped Rec's at the same pos over
  the test pitches. One per-block lookup.
- **Alkaline.** The ladder's low-pass follows the oscillator, so the darker
  top of lift 2 matters little there. Lift 2 everywhere is the simpler
  choice for stage 1 (D-3).

### 4.5 The `pos` law and level

- **pos 0 = gate 50 %** (Saw 2). **pos 127 = the narrowest.**
- **Width:** reuse **`oscPwmDSaw[pos]`** (512 … 26 points, so w = 0.5 …
  0.0254). This needs no new width table, and its floor is the same as the
  shipped Saw and Tri. The Rec table goes down to 4 points (0.39 %), which
  is almost silent and is where the gate's two reads nearly cancel (D-5).
- **Level:** a new `oscPwmLevelChopQ14[128]` that keeps the peak after DC
  removal at full scale: `1 / ((1 + w)(2 − w)/4)`, from ×1.78 at pos 0 to
  ×2.0. It is stored as Q13 or clamped below 2.0, because the shipped level
  tables are `int16_t` Q14 and ×2.0 would overflow; the generator's T9 rule
  covers this.
  - The RMS level falls as the gate narrows, as on the original (narrow
    Saw 3 is thin).
  - Matching RMS instead is a by-ear option (D-5).
- **`dc`:** a generated `oscPwmChopDcQ*[128]`, or computed per block in the
  flash plan. It is a polynomial, so it needs no division. VDIV count
  unchanged.
- **Width slew:** unchanged (`OSC_PWM_SLEW_STEPS` 2 per block).
  - It limits full-depth PWM from an LFO to about 10.8 Hz (254 steps per
    LFO cycle against 2,756 steps/s).
  - The existing PWM forms have the same limit. It is recorded here because
    PWM-heavy patches are the use case.

### 4.6 Per-block plan

- **`osc_pwmPlan()` gains one case** (drum family): row with lift, `d`,
  `c = d × span_r >> 10`, `wPh`, `vOff`, `dc`, and `gQ14`.
- **Cost:** about 10 instructions, all in flash, once per block. That is
  about +0.3 per sample under the S093 rule. It is counted in both sides of
  the comparison.
- **Alkaline:** `alk_renderPwm()` gains the same lookups in float.

---

## 5. Prototype measurements (*prototype*)

The Python model (Appendix A) reproduces `osc_pwmReadD()`: integer indexes,
Q15 fraction, and `sawTable` parsed from `wavetable.c`. Settings: Fs
44,108 Hz, 32,768 samples, Blackman window. **Alias** = energy outside
±8 bins of each harmonic, relative to the harmonic energy. The figures are
total / below 10 kHz, in dB.

### 5.1 Alias: chop (lift 1, lift 2) vs naive and vs the shipped PWM forms

| f (Hz) | w | chop L1 | chop L2 | naive | shipped Rec | shipped Saw |
|---:|---:|---:|---:|---:|---:|---:|
| 110 | 0.50 | −71.0/−71.6 | −71.7/−71.8 | −33.4/−36.9 | −71.4/−72.3 | −68.6/−70.3 |
| 220 | 0.50 | −39.7/−71.3 | −73.0/−73.1 | −22.7/−27.0 | −42.2/−69.1 | −37.7/−67.8 |
| 220 | 0.06 | −32.9/−65.6 | −71.5/−72.5 | −15.3/−19.9 | −36.0/−65.7 | −42.6/−69.8 |
| 440 | 0.50 | −36.9/−66.8 | −74.6/−74.7 | −19.1/−23.1 | −38.6/−72.9 | −34.4/−66.5 |
| 440 | 0.25 | −33.8/−67.4 | −72.8/−72.9 | −17.1/−21.5 | −38.0/−69.8 | −36.3/−71.1 |
| 880 | 0.50 | −31.5/−63.2 | −68.7/−70.5 | −15.7/−20.1 | −36.4/−64.8 | −29.9/−64.7 |
| 880 | 0.06 | −26.7/−54.7 | −65.4/−68.6 | −9.0/−11.4 | −27.1/−58.3 | −38.3/−68.9 |
| 1,760 | 0.50 | −27.5/−61.3 | −65.4/−69.6 | −12.6/−16.9 | −29.7/−68.3 | −27.0/−54.4 |
| 1,760 | 0.06 | −15.1/−45.1 | −55.8/−59.5 | −2.4/−7.1 | −25.6/−56.5 | −29.8/−61.3 |
| 3,520 | 0.50 | −25.5/−37.0 | −37.3/−44.0 | −10.1/−12.9 | −26.4/−93.2 | −26.3/−46.8 |
| 3,520 | 0.06 | −11.2/−26.6 | −26.6/−32.0 | +2.6/−0.7 | −16.2/−43.7 | −38.8/−63.8 |

**A finding about the existing tables, not a defect of the chop:** each
`sawTable` row holds enough harmonics to reach Nyquist near the *bottom* of
its octave. For example, row 4 has 70 harmonics, and 70 × 523 Hz is about
36.6 kHz. So the shipped table waves already fold their top harmonics back
near the top of each octave. With lift 1 the chop inherits the same
behaviour. With lift 2 it is cleaner than the shipped forms.

### 5.2 Brightness against the ideal (harmonic level, dB)

| f, w | lift | < 2 kHz | 2–5 kHz | 5–10 kHz | 10–20 kHz |
|---|---|---:|---:|---:|---:|
| 220, 0.50 | 1 | 0.0 | −0.1 | −0.7 | −2.9 |
| 220, 0.50 | 2 | 0.0 | −0.5 | −2.8 | −11.3 |
| 880, 0.06 | 1 | 0.0 | −0.1 | −0.6 | −2.4 |
| 880, 0.06 | 2 | 0.0 | −0.3 | −2.3 | −10.4 |
| 1,760, 0.50 | 1 | 0.0 | 0.0 | −0.2 | −1.6 |
| 1,760, 0.50 | 2 | 0.0 | −0.1 | −0.8 | −5.6 |

### 5.3 Wrap point (w = 0.25, lift 2, total alias)

| Wrap | 110 Hz | 440 Hz | 1,760 Hz |
|---|---:|---:|---:|
| at the cycle end (at the gate's opening edge) | −39.1 | −21.0 | −14.2 |
| **mid last chopped section** (`φw = (3 + w)/4`) | **−72.5** | **−72.8** | **−58.5** |

---

## 6. Hosts and staging

### 6.1 Stage 1 — Alkaline (recommended first)

- **Wave:** `ALK_WAVE_RS` 3, **under `pwm` only**, name `R-S` (D-1).
  `alk_wave` is a SELECTOR, so Morph never blends it.
- **List size changes per type.**
  - `ALK_WAVE_COUNT` (3) currently serves both `wav` and `pwm` ("one list
    size"). This stage splits it into a `wav` count of 3 and a `pwm` count
    of 4.
  - **Touch points:** `Alkaline_setOscType()` (the clamp; a switch from
    `pwm` `R-S` to `wav` clamps to Tri), `instrumentManager_waveDomainMax()`
    (the Alkaline branch at `InstrumentManager.c:1077`), `alkWaveNames` plus
    the `menu.c:12909` assert, and `menu_formatWaveCell()`'s Alkaline
    branch.
  - Stored data: a stored `pwm` byte of 3 could not exist before (it
    clamped to Tri), so no stored value changes meaning.
- **DSP:**
  - `dl_renderB2Body()` mode 5 (§4.3), with three new parameters (`chC`,
    `chVOff`, `chDc`; `wPh` and `pwD` are reused).
  - The `DiodeLadder_renderB2OscChop()` wrapper.
  - The `alk_renderPwm()` dispatch.
- **Memory:**
  - Flash: the wrapper is about 1.4 KB (mode 3's is 1,368 B) and the tables
    about 0.7 KB, so about +2.1 KB against 32,888 B of headroom.
  - ITCM 0. RAM 0: `AlkalineVoice` is unchanged, and `wPh` and the gate
    constants are per-block locals in `alk_renderPwm()`.
- **Gate:**
  - The mode 5 loop, linked, with its per-block work, must stay ≤ the
    Alkaline's dearest (276.7 per sample, `wtb` slot held). The target is
    ≤ 263.7 (`wav`, no slot), so the cost-table ordering does not change.
  - The probe's cold first-instance cycles must stay ≤ 19,620. The
    Alkaline's measured worst is 19,609 (`wtb` blend), and mode 5 must not
    exceed it.
  - Other wrappers are unchanged (instruction-identical in the ELF check).
  - `elf86_mixer_check`.

### 6.2 Stage 2 — drum-family osc1 (Drum, Snare, Cymbal, HiHat)

- **Five loops** (§9.4 of the instruments reference: plain, FM, FM ramp,
  ring, sync) for a carrier-selectable wave, plus the plan case. The D
  loops are 166–194 B each (`osc_pwmD*Block`, from `nm`), so the chop
  copies need **≈ 0.9–1.0 KB** (*estimate*).
- **The ITCM blocker.** 424 B are free, and the rule "never edit an existing
  ITCM function's section attribute" rules out freeing space by moving a
  cold ITCM loop. Options (D-2):
  - **a.** Put the five loops in flash and gate them on the probe's cold
    first-instance cycles. The S094 P2 §21 gate is in cycles, so flash
    placement is measurable. The risk is the I-cache cold penalty.
  - **b.** Put plain and FM in ITCM (≈ 340 B, which fits) and the rest in
    flash. This is mixed placement with the same measurement.
  - **c.** Wait for an ITCM review, which needs the user to lift the
    section rule for named candidates.
- **The wave list.**
  - `OSC_PWM_WAVE_COUNT` 4 → 5 (Sin Tri Saw Rec **R-S**).
  - **The menu name path must change.** Under `pwm`, `menu_formatWaveCell()`
    names `waveformNames[value + 1]`, and index 4 there is `Noi`. It needs a
    pwm name list.
  - **Colloid is coupled:** `ColloidVoice.h:225` asserts
    `COL_PWM_WAVE_COUNT == OSC_PWM_WAVE_COUNT`. Stage 2 either gives
    Colloid its own count (keeping 4) or includes stage 3.
- **Gate:** each loop must stay ≤ its role ceiling (plain 27, FM 50, FM ramp
  51, ring 42, sync 41), so each voice's dearest is unchanged. Then
  `elf_osc_check` (old loops unchanged), `elf86_mixer_check`, and the probe.

### 6.3 Colloid: six chop oscillators (user, 2026-11-18: "got to run 6")

**Hard limits, from the tree:**
- **CPU.** pwm Saw/Rec 6 is the Colloid's dearest pwm mode. It measured
  **19,136 cycles per block** on the device (`tools/dsp_test/baselines/
  s096p3/device_probe_p3_basic.txt`, with windows up to 19,766), against
  the Basic budget of **19,620**. Host count: `pwm6-par` loops 343 + per
  block 3,042 / 32 = 438.1 (`colloid_cpu_p3c.txt`). So **chop 6 must cost
  no more than today's pwm Saw/Rec 6.**
- **ITCM.** 424 B are free. A Store + Add pair, like
  `col_pwmPairStore/Add` (330 + 320 B), does not fit.

**Design: one ITCM loop, called three times, after a flash pass that sets
the buses to the stack's DC.**

1. **`col_chopDcInit()`** (flash, about 5 instructions per sample,
   *estimate*). It writes `busM[n] = −dcM` and `busS[n] = −dcS`, each
   gliding linearly across the block from the previous block's value.
   - `dcM = Σ dc(w_i)` and `dcS = Σ dc(w_i) × pos_i / 2`, computed once per
     block in `col_oscControl()`, where `dc(w) = w(1 + w)/4` at the chop
     scale.
   - It replaces the Store form's job of starting the buses. It also takes
     each oscillator's DC out of the drive and filters, so there is no
     per-oscillator DC work in the loop.
2. **`col_chopPairAdd()`** in a new `.itcm.sNNN` subsection, **≈ 280–320 B**
   (*estimate*), which fits in 424 B. **This ITCM growth needs user sign-off
   (D-9).** It is called once per pair, three times. For each oscillator,
   each sample:

   ```c
   /* gate: A at twice the phase, B on its own gliding phase q (the F5 rule,
    * in the gate's domain: q advances by 2 inc - (width - width0) x 2^17) */
   D  = rowA[(p >> 21) & 0x3FF] - rowA[q >> 22];            /* read form T */
   c  = smmul((p << 1) - q, span);    /* = w x span, glides with q for free */
   g  = D + c;                                              /* floor at 0 */
   /* ramp: fixed wrap at 7/8 (inside the last chopped section for every
    * w <= 1/2), so K and a are compile-time constants shared by all six */
   v  = ((int32_t)(p + K) >> 16) - a;
   y  = smulwb(g, v);
   busM[n] += y0 + y1;  busS[n] = smlawt(y1, posPair, smlawb(y0, posPair, busS[n]));
   p += inc;  q += j;
   ```

   - **Nothing glides per oscillator except `q`.** `c` comes from the two
     phases, and `(2p − q)` is the gliding width itself. The wrap point is
     fixed, and the DC is handled by the init pass. That keeps the register
     load near the current pair loop's.
3. **The level.** The stack's mean of a new `oscPwmLevelChopQ14[width]`
   joins the existing pwm level sum. `COL_NORM_PWM` (1/√6) is unchanged,
   because the count stays 6.

**Read form: this is the cost decision (D-8).** Each form is the same loop
with a different read, behind `COL_CHOP_READ_FORM`, following the
`ALK_PWM_READ_FORM` precedent. Figures are per sample for the whole stack.
Form T's pair is a **measured** standalone loop count (§6.5): 53 linear, 54
through a curve table, plus a 6-instruction init pass. H and I are
*estimates* built on it. Today's loops are Store + 2 × Add, at most
3 × 66 = 198; 66 is the shipped Add pair, re-measured in the same spike.

| Form | Gate reads | Loops (3 pairs + init) | vs today | Device (×1.49) | Alias, lift 2 (*prototype*) |
|---|---|---:|---:|---:|---|
| **T** | both truncated | 3 × 54 + 6 = 168 (table); 3 × 53 + 6 = 165 (linear) | **≈ −30 / −33** | ≈ 17,700 | −41…−49 dB at 55–110 Hz; −42…−55 at 440–1,760 Hz |
| H | A interpolated, B truncated | ≈ 3 × 63 + 6 = 195 | ≈ −3 | ≈ 19,000 | about 1–2 dB better than T |
| I | both interpolated (the F5 form) | ≈ 3 × 73 + 6 = 225 | **≈ +27** | ≈ 20,430 (**over**) | −62…−74 dB |

- **Form T is the one that fits.** Its spurs come from the gate's edges
  landing on whole table points (1/2,048 of a cycle).
  - Above about 200 Hz they are **below the shipped pulse's own alias
    floor**. For example, at 440 Hz the shipped Rec is −38.6 dB and T at
    lift 2 is −48.5…−50.4 (§5.1, *prototype*).
  - At low notes T is 10–25 dB worse than the shipped Rec: −41…−49 dB
    against −50…−71 dB. Whether that is audible after the SVFs is an ear
    check.
- **Form I sounds clean but is about 1,300 cycles over.** No saving of
  that size is in view: the noise-SVF fallback in S096 P3 §3.2 is about −8
  instructions, roughly 400 cycles.
- **The F5 click fix is kept in every form.** B moves on its own gliding
  phase. A truncated read moves the gate's closing edge by at most a point
  per sample, never by up to 8 points at a block edge.
- **Gate row: lift 2** for every oscillator. Each oscillator has its own
  `tableOffset`, so `span` is packed per pair as `posPair` is.

**Wiring:**
- `COL_PWM_RS` 4 and `COL_PWM_WAVE_COUNT` 4 → 5.
  `COL_PWM_FORM_COUNT(R-S)` = `COL_PWM_COUNT` (6). The "past the last form
  plays the last" rule now lands on R-S.
- **The `ColloidVoice.h:225` assert changes.** The drum engines have no
  R-S until stage 2, so the Colloid's count is the platform's + 1.
- **The menu:** the pwm branch of `menu_formatWaveCell()` needs its own name
  list (`… Rec R-S`). The drum branch keeps its cap of 4, because index 4 in
  `waveformNames` is `Noi`.
- `col_oscControl()`: the chop row (`tableOffset + 2`, clamped to 10),
  `d` from `oscPwmDSaw[width]`, and `j = 2 inc − Δd × 2^17`. Plus the
  `dcM`/`dcS` sums.

**Freeing more ITCM (user question, 2026-11-18).**
- **No dead code is in ITCM.** All 63 functions (15,932 B) are reached.
  The five with no `bl` caller (`calcSineSyncBlock`, `calcSineRingBlock`,
  `calcNoiseRingBlock`, `osc_wtbFmBlock`, `osc_wtbFmRampBlock`) are
  tail-called with `b.n`. Freeing ITCM therefore means moving live code to
  flash. That needs the user to lift the "never edit an existing ITCM
  function's section" rule for named functions.
- **The cheapest candidates** are the per-block helpers, not the per-sample
  loops: `osc_setFreq` (544 B), `osc_buildLoopSyncList` (148),
  `osc_renderUserList` (64) and `osc_buildSyncList` (54). Together that is
  about 810 B, which brings free ITCM to about 1,230 B.
  - The cost moves to the drum family as cold I-cache misses on those calls
    (*estimate*: a few hundred cycles per drum voice per block). The probe
    measures it.
- **What it buys the Colloid:**
  - **Space:** a Store + Add chop pair (≈ 600 B) in place of the init pass
    plus the Add loop. That saves about 3 instructions per sample, roughly
    150 cycles.
  - **Speed, small:** the Colloid's flash code (`col_renderPar` 1,094 B,
    `col_oscControl` 2,940 B, and others) could move into ITCM. Its whole
    cold I-cache penalty is only about **470 cycles**. That is the first
    instance against identical warm instances: slot 1 at 17,160 against
    slots 2–5 at 16,693–16,850, all on wav Saw 12 (`s095p5/
    device_probe_colloid6.txt`, segment A). So no ITCM placement saves more
    than about 470.
- **So ITCM does not pay for form I** (≈ +1,300 cycles needed).
- **The best interpolated option** is a fourth read form, **S**: one
  interpolation of the difference `A − B` at a shared fraction, the old
  S095 D form.
  - B's whole-point offset steps by at most one point per sample while the
    width moves. That is 1/2,048 of a cycle, against F5's 8-point jumps at
    block edges.
  - At rest it has form I's quality (−62…−74 dB).
  - Its cost is ≈ +12 instructions per sample over today (*estimate*: the
    shipped pair minus one interpolation, plus the chop's measured extras).
    That gives about 19,710 on today's pwm Saw/Rec 6 basis, or about 19,240
    with
    the Colloid's hot code moved into ITCM. That is under 19,620 by about
    150, which is inside the estimate's ±1.5 % error.
  - It is worth a spike and a probe run only if form T fails the ear check.
    Its sweep behaviour also has to pass CO-PWG.

**Gates:**
- Host law **CO-CHP**: the loop against the float model in Appendix A
  (fixed wrap). Break proofs: wrap at 0, missing `c`, and sign +1.
- **CO-PWG** still passes on a width sweep (no block-edge step).
- `elf_osc_check` and `elf86_mixer_check`: the other Colloid loops are
  unchanged.
- **The device probe, cold, stereo route: chop 6 ≤ pwm Saw/Rec 6 measured
  on the same run, and ≤ 19,620.**
- An ear check of form T at low notes.

### 6.4 `REx`: the chop on a rising exponential (user, 2026-11-19)

**Decided (user, 2026-11-19):**
- A **rising exponential saw**, PWM-chopped exactly like the saw chop: the
  whole 1× ramp is the curve, and each passed segment is the part of the
  curve it covers.
- **One fixed curve, chosen by ear,** as a new wave. No curvature control.

**The wave.** `y = s(ramp_w(φ)) × gate(φ) − dc`, with
`s(x) = (e^{kx} − 1)/(e^k − 1)` and `k > 0`.
- The gate, the wrap point, the lift and the `pos` law are those of `R-S`
  (§4, §6.3), unchanged.
- The first passed segment sits low on the curve, and the second sits high
  and steep.

**Where the curve is heard (*prototype*, ideal spectra, harmonics 1–40,
level-normalised, `pos` 0…127 on the `oscPwmDSaw` widths):**

| k | dB-RMS distance from `R-S` at pos 0 / 24 / 48 / 72 / 96 / 120 / 127 | Even-harmonic share at pos 0 (`R-S` 0.54) |
|---:|---|---:|
| 2 | 1.0 / 5.2 / 3.8 / 1.0 / 0.4 / 0.4 / 0.0 | 0.47 |
| 3 | 1.6 / 6.1 / 4.6 / 1.7 / 0.6 / 0.7 / 0.1 | 0.44 |
| 4 | 2.3 / 6.8 / 5.3 / 2.3 / 0.9 / 1.0 / 0.1 | 0.42 |
| **5** | **2.9 / 7.4 / 5.9 / 2.8 / 1.2 / 1.3 / 0.1** | **0.40** |
| 6 | 3.4 / 7.9 / 6.3 / 3.3 / 1.6 / 1.6 / 0.1 | 0.40 |
| 8 | 4.4 / 8.7 / 7.2 / 4.1 / 2.2 / 2.0 / 0.1 | 0.39 |

- **The curve is heard mostly over the wide half of the `pos` range.**
  Below pos ≈ 96 the passed segments are short enough to be nearly straight
  pieces of the curve, so `REx` and `R-S` converge as the gate narrows. The
  difference is strongest around pos 24–48, at 6–9 dB RMS across the
  harmonics.
- **Even harmonics thin out** as k grows: an even share of 0.40 at k = 5,
  against 0.54 for `R-S`.
- **Both chops are far from the Rec PWM** at every k: about 15 dB RMS.
  The Rec has no even harmonics at pos 0.
- **Gains flatten above k ≈ 6.** There the first segment almost vanishes,
  and the wave tends to one exponential-topped pulse per cycle.
- **Proposal (D-11): k = 5 as the starting point,** with the final k chosen
  by ear from 4–6.
  - Rendered A/B files (`R-S`, `REx` at k = 4/5/6, Rec, each held at a few
    `pos` values and as a `pos` sweep) can be produced offline before any
    firmware work.
  - Or an SD-card wavetable for the same audition on the device (option A,
    §3).

**Computing a fixed curve: a table, not a polynomial.**
- With one fixed k, a 1,024-point int16 table of `s` is exact, and its
  truncated read is the cheapest form.
  - Index `(p + 2^29) >> 22` covers the wrapped ramp `[−1/8, 7/8)`. The
    points below 0 continue the curve smoothly, where the gate is about 0.
  - The read is one `UBFX` plus one `LDRSH`, **≈ +2–3 per oscillator per
    sample** (*estimate*).
- The polynomial stand-ins are not needed. They also fit poorly at k = 5:
  - the best pinned quadratic has β = 1.80, which dips below 0 near x = 0,
    and its chop spectrum is 2.5–4 dB from the exact one;
  - the cubic's curve error is about 4 %.
- **Quality (*prototype*, lift 2, total alias):**

  | Gate / curve read | k = 0 (`R-S`), 110 / 440 / 1,760 Hz, w = 0.5 | k = 5, same |
  |---|---|---|
  | interpolated gate, exact curve | −71.7 / −74.6 / −65.4 | −72.2 / −74.3 / −59.6 |
  | **form T gate, truncated table** | −48.7 / −50.2 / −54.3 | −47.8 / −48.9 / −51.8 |
  | interpolated gate, truncated table | — | −61.3 / −55.4 / −54.0 |
  | interpolated gate, interpolated table | — | −71.8 / −73.2 / −59.5 |

  - With form T the truncated table adds at most about 1.2 dB to the
    gate's own spur floor, so the cheap read is the right one for the
    Colloid.
  - Behind an interpolated gate (drum family), a truncated table would
    become the floor: −54…−65 dB. Those hosts use the interpolated table
    read (≈ +6–7).
- **Flash:** 2 KB for the `REx` table.

**One loop for both chops (Colloid).**
- The chop loop reads the curve through a table pointer in every form:
  - `REx` passes the exponential table;
  - `R-S` passes a **linear table**, the identity curve (2 KB more flash).
- One ITCM loop (≈ 300 B, D-9) then serves both waves. The 424 B free
  cannot hold two loops.
- `R-S` runs at `REx`'s cost. The slot is budgeted at its dearest wave
  anyway, so this costs the budget nothing.
- Through the linear table, `R-S`'s alias matches the exact ramp's to within
  0.5 dB in form T (k = 0 row above).

**DC and level** depend only on the width once the curve is fixed:
- two generated 128-entry tables per wave (`oscPwmRexDc`,
  `oscPwmLevelRexQ14`), as for `R-S`;
- no per-block division and no 2D table.

**Cost per host (*estimate*):**
- **Colloid, 6 oscillators, form T, shared loop:** 3 × 54 + 6 = 168
  against today's ≤ 198, so **≈ −30 per sample** (measured loop counts,
  §6.5). Today's pwm Saw/Rec 6 measures 19,136 of 19,620 cycles, so this
  comes to about 17,700.
  - **Six fit**, with both `R-S` and `REx` on the shared loop.
  - It costs a little more than the linear chop on its own (53 per pair,
    −33), because the curve read is +1 per pair, measured. But the
    truncated chop is so much cheaper than today's two-read pulse pair that
    it still lands below today.
  - The loop needs one more loop-invariant register (the table base).
- **Alkaline mode 5:** ≈ 262 + 3 (truncated table, as its gate) ≈ 265.
  This is under its dearest setting (276.7).
- **Drum-family osc1:** the plain loop goes from ≈ 27 to ≈ 34 with the
  interpolated table. That is over the plain role's 27, but the voice's
  dearest mode (`fm`) is unchanged. It is reported, not hidden (§6.2
  placement still applies).

**Wiring (Colloid):**
- `COL_PWM_REX` 5; `COL_PWM_WAVE_COUNT` 4 → 6 (Sin Tri Saw Rec R-S REx).
- The pwm name list gains `REx`.
- `COL_PWM_FORM_COUNT(REx)` = `COL_PWM_COUNT` (6).
- `gen_pwm_tables.py` gains the curve tables, `REx`'s DC and level tables,
  and checks:
  - T14: the curve table is monotonic, with s(0) = 0 at index 128 and
    s(1⁻) < 32,767;
  - T15: the REx range proof, as T12.
- The host law **CO-CHP** covers both waves, with the exact-curve model as
  the reference.

### 6.5 Eight oscillators? (user question, 2026-11-19)

**Measured (standalone spike, 2026-11-19).** The loops were compiled with
the Makefile's DSP flags (`-mcpu=cortex-m7 … -Ofast`) and counted with
`tools/dsp_test/elf_looppath.py --cond-heads --in-range`.

| Loop | Instructions per sample (one pair) | Size |
|---|---:|---:|
| `col_pwmPairAdd`, the shipped two-read pulse (copied verbatim, the calibration) | **66**, the documented figure exactly | 320 B |
| `R-S`/`REx` chop pair, form T, curve through a table pointer | **54** | 308 B |
| `R-S` chop pair, form T, exact linear ramp (no table) | **53** | 312 B |
| DC init pass (both buses, gliding) | **6** | 34 B |

- Either chop loop fits the 424 B of free ITCM on its own (D-9).
- The spike source is in the session scratchpad, not the tree. When the
  loop is implemented, its source and counts go to
  `tools/dsp_test/baselines/<session>/`.
- These are counts for the loop alone. The linked loop can differ, because
  LTO and the register allocation in place change it; the gate is the
  device probe.

**Six against eight**, per sample, against today's pwm Saw/Rec 6 (loops ≤
3 × 66 = 198; device 19,136 cycles):
- Each extra oscillator also carries its control-rate work, about 186
  instructions per block (≈ 5.8 per sample), plus 24 per block per pair
  (the S096 P3 §3.2 breakdown of "one pair ≈ 42").
- The cycle figures use the pwm mode's measured 1.49 cycles per
  instruction.

| Count | Loops | Control delta | vs today | Device (*estimate*) | Budget 19,620 |
|---|---:|---:|---:|---:|---|
| **6, table loop (`R-S` + `REx`)** | 3 × 54 + 6 = 168 | 0 | **≈ −30** | ≈ 17,700 | fits, ≈ 1,900 spare |
| 8, table loop | 4 × 54 + 6 = 222 | ≈ +12 | ≈ +36 | ≈ 20,850 | **≈ 1,250 over** |
| 8, linear loop (`R-S` only) | 4 × 53 + 6 = 218 | ≈ +12 | ≈ +32 | ≈ 20,660 | **≈ 1,050 over** |

**What would close the gap for 8 (none is enough on its own):**
- **Colloid hot code into ITCM** (§6.3): at most about −470 cycles, the
  measured cold penalty. It needs about 4 KB of ITCM freed, which does not
  exist (only about 810 B can be freed cheaply).
- **The noise-SVF fallback** (S096 P3 §3.2): about −8 instructions, roughly
  −380 cycles. It is a sound change.
- **A cheaper floor offset.**
  - Take `c = (2p − q) >> 16`, which assumes every gate row's span is
    2^16. That saves about 4 per pair.
  - It is exact only for rows 0–5. On rows 6–10 (fundamentals above about
    260 Hz, with lift 2) the gate's floor goes wrong by 5–140 % of `w`, so
    this is rejected.
- **The break-even pair cost for 8** is ≤ 45 instructions (4x + 6 + 12 ≤
  198), against the measured 54. No form in view takes 9 out of the pair
  without giving up the gliding width (the F5 click fix), the stereo
  position, or the exact floor.

**Answer:**
- **Eight do not fit**, for either wave. Six fit with about 1,900 cycles to
  spare.
- An odd count (7) is not available: the pair loops and the packed stereo
  positions need an even count (`ColloidVoice.h`).
- Eight would come back into view only together with a Colloid saving of
  about 1,300 cycles elsewhere. None is identified.

---

## 7. What this draft does not cover

- **Saw 4 / Saw 5** (+3-octave chop, and both together).
  - Saw 4 is the same loop with the gate read at `ph << 3` and lift ≥ 3. It
    is a separate loop copy, since the shift is a loop constant, at the
    same cost. It could be a later wave (`Ch8`).
  - Saw 5 is the product of two gates, about +10, which needs a role-budget
    check first.
- **The Alpha Juno's fixed HPF and chorus.** These are a large part of the
  "hoover" sound (§1.1) and are outside the oscillator. The Alkaline's
  ladder and post-FX, and the Effect bus's chorus-type modules, are where
  to approximate them.
- **Pulse 3** (the Alpha Juno's saw-modulated pulse). This is a different
  wave and would need its own proposal.

---

## 8. Tests and tools

- **`tools/plots/gen_pwm_tables.py`:** add `oscPwmChopLift[128]`,
  `oscPwmLevelChopQ14[128]` (or Q13), `oscPwmChopDc[128]` and
  `oscPwmChopSpan[11]` (measured from the `sawTable` rows it already
  reads). New checks:
  - T10: lift values are 1..2 and non-decreasing.
  - T11: at pos 0, the level gives the w = ½ peak.
  - T12: the store's range proof, `max|o| × gQ14 < 2^31` for every row and
    pos.
  - T13: the alias rule behind the lift choice (D-3), over the §5 pitch set.
  - `--check` byte-for-byte, as T8.
- **`tools/dsp_test/modules/alkaline.c`:** law **AL-CH**, mode 5 against a
  float reference (interpolated gate, exact ramp). Report the RMS error, as
  AL-PW does. Break proofs: wrap at `φ = 0` (the alias rises), sign +1
  (gate lost), missing `c` (floor not at 0).
- **`tools/dsp_test/modules/oscillator.c` group pwm** (stage 2): the chop
  loops against the same reference. The FM ramp at `dStep = 0` must equal
  FM (the PW6 pattern). Ring at depth 0 must equal the carrier.
- **Cost:**
  - `tools/alkaline/control_cost.py` (an `ITERS` entry for the new wrapper);
  - `elf_looppath.py`/`elf_blockpath.py` linked;
  - the probe's cold first instance (S094 P2 §21);
  - `elf86_mixer_check`.
- **Ear:** an A/B against a stage-A wavetable (D-6) and, if available, an
  Alpha Juno recording.

---

## 9. Decisions for the user

| # | Question | Recommendation |
|---|---|---|
| D-1 | Names, and which types list them | **Decided (user, 2026-11-19): `R-S` for the chopped saw and `REx` for the chopped exponential**, in the pwm wave list only (pos 0 of `R-S` is already the fixed Saw 2, so no `wav` entry is needed) |
| D-2 | Stage 2 placement (ITCM 424 B free) | Stage 1 first. For stage 2, option a (flash loops, cycle-gated) unless the user wants an ITCM review |
| D-3 | Gate row lift | Alkaline: lift 2 everywhere. Drum family: generated per-pos lift (1, or 2 where lift 1 aliases more than the shipped Rec) |
| D-4 | Gate orientation | High first (§1.2). Check by ear; the other orientation costs the same |
| D-5 | Narrowest width and level law | `oscPwmDSaw` widths (floor 2.54 %), peak-keeping level |
| D-6 | Make the ideal-wave wavetable folder now as an ear reference (offline script, no firmware change) | Yes, if an A/B is wanted before stage 1 |
| D-7 | Read the S089 P2 rule ("paid for by a measured saving") as met when the voice's dearest setting does not rise | Yes. This is the reading the S094 PWM type was accepted under |
| D-8 | Colloid gate read form (§6.3) | **Decided (user, 2026-11-18): try form T (both truncated) first**, later. It is the only form that fits 19,620. If it fails the low-note ear check, spike form S |
| D-9 | Colloid ITCM: one loop of ≈ 280–320 B from the 424 B free | Approve after the loop is built and its size is known; it leaves ≈ 100 B |
| D-10 | Free ITCM by moving the per-block helpers to flash (≈ 810 B; §6.3) | Not now. Only if form S is spiked, or for drum-family stage 2 |
| D-11 | `REx` curve (§6.4) | **Decided (user, 2026-11-19): one fixed rising-exponential curve as a new wave, no curvature control.** Start at k = 5 and choose by ear from 4–6. Use a 1,024-point curve table, read truncated in the Colloid and Alkaline and interpolated in the drum family |

---

## 10. Statements this change would make out of date (for the user to act on)

This proposal edits nothing in `MEMORY.md` or `knowledge_files/`. After
stage 1 (and 2):

- `MEMORY.md` §DSP, Alkaline: `alk_wave` … `ALK_WAVE_COUNT 3`, "fused mode
  3 … `ALK_TYP_COUNT` 3" (the per-type count and mode 5).
- `MEMORY.md` §DSP, "PWM oscillator type (S094 P1 S3)": the wave list
  (stage 2).
- `MEMORY.md` §DSP, Drum FM ramp twins: "ITCM **13,984** / 16,384 B after
  S094". **Already stale:** the current image is 15,960 B.
- `ALKALINE_MODULE_REFERENCE.md` §3.1 (the waves under `pwm`) and §7.1 (a
  `pwm R-S` cost row).
- Colloid (§6.3): the pwm wave list (Sin Tri Saw Rec **R-S**), the
  `COL_PWM_WAVE_COUNT` assert's new form, the ITCM total, and the pwm cost
  rows in the Colloid reference and `CPU_USE_DSP_AUDIT.md`.
- `INSTRUMENTS_DSP_REFERENCE.md` §5.5 (Alkaline waves), §8.1/§8.2 (the
  measured loop and per-voice rows), and the §6.1 PWM entry (stage 2).

---

## Appendix A — reference model (Python, prototype)

```python
# saw: int64[11][1024] parsed from wavetable.c (comments stripped)
A = [-np.polyfit(np.arange(256,768)/1024, saw[k][256:768]/32767, 1)[0]/2 for k in range(11)]

def readD(row, p, d, sign):                      # osc_pwmReadD(), exactly
    ia=(p>>22)&1023; na=(ia+1)&1023; ib=(ia-d)&1023; nb=(ib+1)&1023
    d0=row[ia]+sign*row[ib]; d1=row[na]+sign*row[nb]
    return d0+(((d1-d0)*((p>>7)&0x7fff))>>15)

def chop(f, w, n, lift):
    inc = round(f/FS*2**32); p = (np.arange(n)*inc) & 0xffffffff
    r = min(octave(f)+lift, 10); row = saw[r]; d = round(w*1024)
    g = readD(row, (p<<1)&0xffffffff, d, -1)/(2*A[r]*32767) + w     # gate, floor 0
    pw = int((3+w)/4*2**32); x = p/2**32
    v = np.where(p < pw, x, x-1.0)                                     # ramp wrapped at pw
    return v*g - w*(1+w)/4
```

Here `octave(f)` is `floor(log2(f / 16.3516))`, clamped to 0..10, which is
`osc_setTableFreq()`'s rule.

## Sources

- KVR DSP forum, "Roland Alpha Juno-2 pwm sawtooth", pages
  [1](https://www.kvraudio.com/forum/viewtopic.php?t=420194),
  [2](https://www.kvraudio.com/forum/viewtopic.php?start=15&t=420194),
  [3](https://www.kvraudio.com/forum/viewtopic.php?f=33&t=420194&start=30),
  [4](https://www.kvraudio.com/forum/viewtopic.php?t=420194&start=45),
  [5](https://www.kvraudio.com/forum/viewtopic.php?t=420194&start=60),
  [6](https://www.kvraudio.com/forum/viewtopic.php?t=420194&start=75)
- [gearnews, "Roland Alpha Juno: Classic Synths"](https://www.gearnews.com/roland-alpha-juno-synth/)
- [Thea Flowers, "The Design of the Roland Juno oscillators"](https://blog.thea.codes/the-design-of-the-juno-dco/)
  (Juno 6/60/106 comparator orientation)
- [HISE forum thread on PWM saw emulation](https://forum.hise.audio/post/111323)
