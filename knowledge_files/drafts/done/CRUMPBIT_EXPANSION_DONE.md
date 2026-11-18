# CrumpBit Expansion: Assessment

- **Date:** 2026-10-04 (after the Session 075 close, `dev-ph6-copyclear`).
- **Request (user):** add to CrumpBit (`cbt`)
  1. stereo filters on the output;
  2. a non-resonant tone control in the feedback path;
  3. feedback above unity that can be driven into self-oscillation;
  4. a longer delay (the user's estimate: about 2.4 s from the ~126 KB arena).
- **Constraint (user):** about 4 % CPU left. The worst-case Scene reads
  94–95 % on the Settings `cpu` widget.
- **Status:** assessment only. No code is changed. The prototype loops used
  for the counts below are scratch files and are not in the repository.
- **Sources:** `CrumpBitEffect.c/.h`, `CrumpBitParameters.c/.h`,
  `EffectsManager.c/.h`, `AudioCodecManager.c`, `EFFECTS_MIXER_DSP_REFERENCE.md`
  §4.4, §5 and §6, `074_SESSION_HANDOFF_LOG.md` §4.4–§4.6 and §7,
  `DSP_TEST.md` §7, and `MEMORY.md` (RAM and DSP CPU policies).

---

## 1. Summary

- **CPU is not the binding constraint.** All four features together cost
  about **+52 to +61 instructions per stereo frame**: roughly **1.1–1.3 %**
  of the CPU, plus about 0.1–0.15 % for per-block coefficient work. That is
  about a third of the 4 % left.
- **Expanded, CrumpBit stays cheaper than StereoFilter.** `flt` costs about
  215–290 cycles per frame because of its divisions. The fully expanded
  CrumpBit is about 170 instructions with no division. If the 94–95 %
  worst case was measured with `flt` as the Effect, the expansion does not
  raise the worst case at all.
- **The longer delay costs no CPU.** It is a design decision, not a
  performance one. The 1.60 s ceiling is deliberate (S074 decision B6): it is
  sized to the **minimum** arena share, so a Scene sounds the same whatever
  voice units future instruments claim. The Rate curve also spends most of
  its travel on short times, which is probably why the maximum feels far
  shorter than 1.6 s (§3.4).
- **RAM can stay at 0 B.** The new state (about 28 B) overflows the 76 B
  runtime union by 8 B. Two small trims bring it back under 76 B (§5).
- **One feature has a hard prerequisite:** feedback above unity needs a DC
  blocker *inside* the loop. Without one, the loop latches to a rail
  (full-scale DC on the output) (§3.1).

---

## 2. Starting point

### 2.1 What CrumpBit does per sample today

Stereo 8-bit ADC → data-line masks → DAC → 10 Hz AC coupling (per channel),
then a mono 8-bit tape loop: glide, interpolated read, `valid` mute, and a
write of `adc(½(hL + hR) + fb·wet)` with `fb ≤ 0.99`. The output is
`h + mix·(g·wet − h)` per channel. Every stage runs on every sample (the
constant-CPU rule).

### 2.2 Measured cost

Instruction counts come from `fpseq.py --report --all` on standalone
`-Ofast` ARM objects with the firmware DSP flags (`DSP_TEST.md` §4.3):

| Loop | Instructions per stereo frame | VDIV |
|---|---:|---:|
| `crumpBit_process`, current source | **111** | 0 |
| `SVF_calcBlockZDFFloat` (one channel; `flt` runs two) | 80–103 | 2–3 |

- **CrumpBit is about 111 instructions, not the documented 75–95**
  (`EFFECTS_MIXER_DSP_REFERENCE.md` §4.4 and §6.2a should be corrected). About
  21 of them are register moves, a sign of register pressure.
- `flt` is 160–206 instructions plus 4–6 `VDIV` (about 14 cycles each), so
  roughly **215–290 cycles per frame**.
- Only one Effect is active at a time, so **the worst case is set by the
  more expensive type.** Today that is `flt`, by about 100–180 cycles per
  frame.

### 2.3 What "4 %" buys

- The frame budget is 4,897 cycles (216 MHz / 44,108 Hz). 4 % is about
  **196 cycles per stereo frame**.
- The counts here are instructions, treated as cycles 1 : 1. That is
  conservative for Cortex-M7 FP code with no divisions: dual issue usually
  makes it faster, and loads or stalls sometimes make it slower.
- **The `cpu` widget is queue pressure, not pure DSP load.** It reports the
  share of time the 2-slot ready queue was not full (`AudioCodecManager.c`,
  `audioCodec_getQueueFreePercent()`). That includes foreground work done
  while a slot is free. The real ceiling is the queue *emptying*, which shows
  as underruns. 94–95 % therefore leaves about 5 % of real time, with a
  4.35 ms (192-frame) cushion against foreground spikes. Keeping at least
  1 % in reserve leaves about **150 cycles per frame for the expansion**.

### 2.4 First step, before any code (no cost)

On hardware, run the worst-case Scene three times: Effect `off`, `flt`, and
`cbt` with every send open. Read the `cpu` widget and the underrun count.
This shows how much of today's 94–95 % is `flt`. If the reading was taken
with `flt`, the budget for CrumpBit is the `flt` − `cbt` difference plus the
4 %.

---

## 3. The four features

### 3.1 Feedback above unity and self-oscillation

**What is needed.**

1. **A new feedback curve.** Today `fb = row·0.99/127`. Proposed: piecewise
   linear, 0..100 → 0..1.0 and 100..127 → 1.0..1.5, so unity sits at a known
   point and the top quarter of the knob is the "runaway" zone.
   - Alternatives: a smooth curve with unity near 96; or keep the current
     curve and add a `x1`/`x1.5` "over" row.
   - Saved Scenes with a high `fbk` will sound different (§7, D6).
2. **A DC blocker inside the loop (mandatory).** The existing AC coupling
   sits before the loop. The ADC's round-half-up and the masks leave small
   offsets; at loop gain ≥ 1 they integrate, and the loop latches to a rail.
   The wet signal is not AC-coupled after the read, so that would put
   full-scale DC on the Effect return. A one-pole, about 10 Hz, on the write
   (`dc += k·(w − dc); w −= dc`) costs 2–3 instructions and 4 B of state.
3. **A deliberate limiter on the write.** The 8-bit ADC already hard-clips
   at ±1.0, so the loop is bounded today. The choice is about sound:
   - **Hard clip (existing, 0 extra):** oscillation squares off at the rails.
     Raw and very "8-bit".
   - **Soft knee (+3–5 instructions):** linear below about 0.75, bending to
     exactly 1.0 (the bus compressor's knee shape, `BusCompressor.c`). Smoother
     howl, and Scenes with feedback below unity stay unchanged.
   - Avoid a plain cubic (`x − x³/3`-style) over the whole range. It
     compresses and distorts small signals too, so every existing Scene's
     repeats would change.

**How it will behave.**

- Oscillation is bounded. `wet` comes out of an int8 loop, so it is always
  within ±1.0. The Effect output is at most about 2.0 × full scale before the
  `vol` row, the return's ±255 clamp and the pack clip. It will be loud, not
  unstable.
- **The 8-bit dead zone shapes the onset.** Below ½ LSB, signals round to
  zero. Just above unity, quiet tails sustain at a fixed low level instead of
  growing. A tail of *a* LSB grows only when `a·(fb − 1) ≥ ½`: at `fb = 1.1`,
  about 5 LSB (−28 dBFS). Louder material blooms and quiet residue hangs.
  That is characterful, and a sound check should confirm the user likes it.
- Together with the tone control (§3.2), the loop gain is highest at low
  frequencies, so runaway settles into a darkening dub-style howl. Moving
  Rate during oscillation gives tape-speed pitch sweeps through the existing
  150 ms glide.
- **Cost:** about +3 instructions (the DC blocker) plus 0–5 for the limiter.
  The curve is per block.

### 3.2 Tone in the feedback path (non-resonant)

- **One-pole low-pass on the feedback return:** `tone += k·(wet − tone)`,
  then the write uses `fb·tone`. Cost: 2 instructions and 4 B.
  - The heard first echo is unfiltered; repeat *n* is filtered *n − 1* times
    (classic tape or BBD darkening).
  - Alternative placement: filter the whole write (`input + fb·wet`), so the
    first echo is darker too. Same cost.
- **Variant: tilt.** One knob, low-pass below centre and high-pass above
  (`hp = wet − lp`), flat at centre. +2–3 instructions. Lets repeats get
  thinner (dub) as well as darker.
- **Coefficient:** `k = 1 − e^(−2π·fc/fs)` once per block, with an
  exponential map of about 150 Hz–18 kHz. That is one `expf` per block, or a
  128-entry float table (512 B flash, no RAM).
  - At the top of the knob `k` is about 1. The filter still runs (constant
    CPU); it is simply open.
- **It does three jobs at once:** the requested tone; the loop-gain shaping
  that makes self-oscillation musical (§3.1); and the anti-alias filter for
  the half-rate tape option (§3.4, L3).

### 3.3 Stereo filters on the output

These options are measured with the §3.1 and §3.2 loop changes already in
place:

| Option | What | Δ vs feedback core | Notes |
|---|---|---:|---|
| **F1 — TPT state-variable filter, stereo** (recommended) | Zavalishin/Simper linear SVF per channel, after the mix; resonant; LP/BP/HP/notch from one `(m0, m1, m2)` mix | **+35** | No per-sample division. Coefficients once per block: `g = tan(π·fc/fs)`, `k = 2 − 2·res`, `a1 = 1/(1 + g(g + k))` (one division per block). Stable under per-block modulation, so LFO on cutoff is safe. 16 B of state. |
| F1 + stereo spread | Separate L/R cutoff (an offset row) | about +3–6 more | Same per-sample math; a second coefficient set adds register pressure. |
| **F2 — one-pole "DJ" tilt, stereo** | Low-pass below centre, high-pass above, non-resonant | +18 | Cheapest real stereo option. 8 B of state. |
| F3 — SVF on the wet only (mono) | Filters the delay, not the crushed dry signal | +22 | Cheaper, but not an *output* filter. |
| F4 — reuse the `flt` ZDF filter twice | `SVF_calcBlockZDFFloat` on L and R | +160–206 and 4–6 VDIV (about +4.5–6 %) | **Does not fit.** It would also make `cbt` heavier than `flt`, which raises the worst case. |

- **Recommendation: F1.** A resonant low-pass after an 8-bit converter is
  the classic sampler reconstruction filter, so it suits the type's identity.
  The same block also gives HP and BP sweeps.
- The F1 code should be its own small `static inline` in
  `CrumpBitEffect.c`, not a second copy of the ResonantFilter twins
  (`SVF_calcBlockZDF*` must stay identical to each other; this filter is a
  different, linear structure).

### 3.4 Longer delay

**Why it is 1.60 s today.**

- `CRUMPBIT_DELAY_MAX_SAMPLES` = 70,573 and `CRUMPBIT_BUFFER_BYTES` = 70,592,
  declared as both the minimum and the preferred share. That fits the
  **minimum** Effect share (73,600 B, with all twelve voice units claimed).
  It was a deliberate choice (S074 B6): a Scene must sound the same whatever
  voice units future instruments claim.
- **Nothing claims voice units today** (`fxbuf_voiceAcquire()` has no
  caller). The share is always the whole arena: **126,592 B, which is
  2.87 s** at 8-bit mono, more than the 2.4 s estimate. About 56 KB of it is
  unused.
- **The Rate curve hides most of the range.** `t = 1.60 s · 80^(−r/127)`:

  | `rte` | 0 | 8 | 13 | 20 | 32 | 48 | 64 (default) | 96 | 127 |
  |---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
  | Delay (s) | 1.60 | 1.21 | 1.02 | 0.80 | 0.53 | 0.31 | 0.18 | 0.06 | 0.02 |

  Only `rte` 0..13 (about a tenth of the knob) is above 1 s.
- **Sync shortens it further.** Only divisions ≤ 1.60 s are candidates. At
  120 BPM the longest is `/2` (1.0 s); `1br` (2.0 s) is excluded. At 140 BPM,
  `1br` is 1.71 s and is still excluded.
- If `rte 0`, `snc off` sounds clearly shorter than 1.6 s on hardware, that
  would be a bug, and worth a quick check (a clap, Mix 127, `fbk 0`).

**Options (none changes the per-sample cost, except L3):**

| Option | Max delay | Cost | Trade-off |
|---|---|---|---|
| **L1 — ceiling scales with the share** | 2.87 s today; 1.67 s if all twelve voice units are ever claimed | 0 CPU, 0 RAM | Reverses B6: a long-delay Scene would shorten if a future instrument claims units. Needs `buffer_pref_bytes` = the whole arena (min stays 70,592), the Sync candidate test against the live length, and the Effect-page Sync label given that length (today `crumpBit_syncDivision()` uses the compile-time maximum). The DSP already clamps the target to `length − 2`, and the glide makes a clamp change a pitch bend, not a click. |
| **L2 — fixed 2.4 s ceiling** | 2.40 s always (105,888 B) | 0 CPU, 0 RAM | Keeps B6's "same sound always". In exchange, the minimum share rises from 73,600 B, so future instruments can claim at most **4** voice units in total (today's limit is 12). |
| **L3 — half-rate tape mode** (`x1`/`x½`) | Doubles any of the above: 3.20 s in today's 70,592 B; 5.74 s with the whole arena | **+9 instructions** (measured; the branchless form costs the same in both modes) | Fits the theme: about 11 kHz bandwidth plus imaging, like a sampler at half rate. The loop writes the running average of each sample pair and advances every second sample; the read uses loop-sample units. The §3.2 tone filter is the anti-alias filter. Switching speed with content in the loop gives an octave tape-speed jump. +6 B state and one row. |
| **L4 — reshape the Rate curve** | (unchanged maximum) | 0 | Can be combined with any of the above. For example, with a 2.87 s ceiling, `t = 2.87 s · 143.5^(−r/127)` puts 1 s at `rte` 27 instead of 13. A two-segment curve could give the bottom half of the knob to 0.25 s and longer. |

**Recommendation:** L1 + L4 if the user is willing to revisit B6 now that
no instrument uses voice units. L2 + L4 if Scene stability matters more
than future voice-unit capacity. L3 is a separate creative choice: worth
having if the lo-fi long mode appeals, since its cost is small.

---

## 4. Cost summary (measured prototypes)

Prototypes are restatements of `crumpBit_process` with each addition,
measured with `fpseq.py --report --all` (`-Ofast`, Cortex-M7, firmware DSP
flags). They are representative, not final code. The restatement of the
current loop measures 108 against the real 111.

| Variant | Instructions per frame | Δ vs today | ≈ CPU of the Δ |
|---|---:|---:|---:|
| Today | 108 (real 111) | — | — |
| **B — feedback core:** tone LP, in-loop DC block, limiter, `fb` up to 1.5 | 125 | +17 | 0.35 % |
| B + F2 one-pole tilt, stereo | 143 | +35 | 0.7 % |
| B + F3 SVF on the wet only | 147 | +39 | 0.8 % |
| **B + F1 stereo SVF** | 160 | +52 | **1.1 %** |
| B + F1, with the trims of §5 | 155 | +47 | 1.0 % |
| **B + F1 + L3 half-rate tape** | 169 | +61 | **1.25 %** |
| Reference: `flt` | 160–206 + 4–6 VDIV ≈ 215–290 cycles | | |

- **Per-block additions:** one `expf` (tone) and one `tanf` plus a division
  (SVF), about 150–250 cycles per 32-frame block. That is 5–8 cycles per
  frame, about 0.1–0.15 %.
- **Full package:** about **1.4 % of the CPU**, against about 3 % usable
  (§2.3). The prototypes keep the constant-CPU shape: no per-sample division,
  no data-dependent branch, only compare-selects. "Filter off", "tone open"
  and `x1` speed do the same work as any other setting.
- **Flash:** about +2–4 KB (the S074 `-Ofast` code growth suggests the larger
  end). 220 KB is free.

---

## 5. RAM

`CrumpBitRuntime` is 56 B inside the 76 B `effects_runtime_t` union (the
union size is pinned by an assert in `EffectsManager.c`).

| New state | Bytes |
|---|---:|
| Tone filter state | 4 |
| In-loop DC blocker | 4 |
| Stereo SVF state (`s1`, `s2` × 2) | 16 |
| Raw rows: tone, cutoff, resonance, mode | 4 |
| *Optional:* spread row | +1 (pads to +4) |
| *Optional:* L3 half-rate accumulator, phase, speed row | +6 (pads to +8) |
| **Core + F1** | **+28 → 84 B** |

Two trims keep it at or under 76 B, so the RAM change is **0 B**:

- **T1 — ramp origins as raw bytes (−12 B).** `mix`, `feedback`, `gain_l`
  and `gain_r` are floats, but each is exactly `f(previous raw row)`, since
  the end-of-block value is stored as the exact target. Storing the previous
  `mix_raw`, `feedback_raw` and `pan_raw` and recomputing the origins per
  block is bit-identical (S0).
- **T2 — one-state AC coupling (−8 B).** Use `lp += k·(y − lp); h = y − lp`
  instead of the two-state form. It is a different first-order 10 Hz
  high-pass realisation: inaudible, but not bit-identical (S1/S2).
- **With T1:** core + F1 = 72 B. **With T1 and T2:** everything, including
  the spread and L3, fits in about 72–76 B.

**If the trims are not wanted:** the union would grow 76 → 84 B. The
current link has `_edtcmz` at `0x20001178` and the arena at `0x20001180`,
an 8 B alignment pad, so +8 B would probably cost the arena nothing, but it
uses up the last of that pad. Either way it is a RAM change that needs the
user's approval (policy in `MEMORY.md`). Any larger growth moves the arena
down by 32 B.

Not recommended: keeping the extra state in the tail of the Effect's own
arena share (the minimum share has 3,008 B spare beyond the loop). It costs
no union bytes, but it ties DSP state to the share geometry, and L1 or L2
would consume that tail.

---

## 6. Parameters and the Effect page

- **New rows (append only; indices are permanent):** `ton` (tone), `cut`,
  `res`, `flt` mode (or a continuous LP→BP→HP morph), and optionally `spr`
  (spread) and `spd` (`x1`/`x½`). File keys are `crump_*`, as for the
  existing rows.
- **Lanes:** 9 of 16 are used (lane 0 is Effect Morph), so 6 are free:
  enough for all six rows.
- **Page:** SELECT 2 has 3 of its 4 screens in use, and screen 3
  (`snc dpn`) has 2 empty cells. That gives 6 free cells, for example screen
  3 → `snc dpn ton spd` and a new screen 4 → `cut res flt spr`. SELECT 3..8
  must stay at zero screens, because those buttons are the bit toggles.
- **Modulation:** tone, cutoff, resonance and spread as Morph, LFO and
  Pattern rows (like `mix` and `fbk`). Mode and speed as Pattern-automatable
  and sequenceable, but not Morph (interpolating an enum is meaningless).
  `write_param` keeps storing raw bytes only, so an LFO costs nothing extra
  per block beyond the coefficient update, which runs every block anyway.

---

## 7. Decisions for the user

| # | Question | Default suggestion |
|---|---|---|
| D1 | Which Effect was active for the 94–95 % reading? (§2.4) | Measure `off` / `flt` / `cbt` first. |
| D2 | Long delay: L1 (share-scaled, revisits B6), L2 (fixed 2.4 s, limits future voice units to 4), or neither | L1 + L4 |
| D3 | Half-rate tape mode (L3)? | Yes, if the lo-fi long mode appeals (+9 instructions). |
| D4 | Output filter: F1 resonant SVF, F2 tilt, or F3 wet only; with L/R spread? | F1, spread optional |
| D5 | Tone: low-pass or tilt; feedback return only, or the whole write? | Low-pass on the return |
| D6 | Feedback curve (unity at 100, max 1.5?), and accept that existing Scenes with a high `fbk` change | Piecewise, unity at 100 |
| D7 | Limiter: hard rail (raw) or soft knee | Soft knee (existing Scenes below unity unchanged) |
| D8 | RAM: trims T1 (+T2) to stay at 76 B, or approve +8 B | T1; add T2 only if L3 and spread are both wanted |

---

## 8. Suggested order and verification

1. **Measure first** (§2.4): no code.
2. **Longer delay** (D2, L4): constants, the curve, the share-aware Sync and
   label. 0 CPU. Listen for clamps when the share changes, using the
   diagnostic `DEV_FXBUF_FORCE_VOICE_UNITS`.
3. **Feedback core** (D5–D7): DC blocker, tone, curve, limiter, plus T1.
4. **Output filter** (D4).
5. **Optional L3.**

For each step:

- **Host first** (`DSP_TEST.md` §6.3), with a CrumpBit runner: bounded
  output at `fb` 1.5 with silence, DC and full-scale input; no DC lock-up
  over 60 s of oscillation; no NaN; parameter jumps; mono input; `io->r`
  NULL.
- **ARM:** `fpseq.py --report --all` on `crumpBit_process`, checked against
  the §4 table.
- **Build:** `make clean && make all`, `arm-none-eabi-size`,
  `tools/link_budget.py` (union size, arena start and margin).
- **Hardware:** the worst-case Scene with `cbt` at its heaviest settings,
  every send open, and LFOs on cutoff, tone and feedback. Check the `cpu`
  widget, the underrun count, and listen (keep the monitor level low for
  self-oscillation tests).
- **Afterwards,** update `EFFECTS_MIXER_DSP_REFERENCE.md` §4.4 and §6, and
  correct the documented cost to the measured figures.
