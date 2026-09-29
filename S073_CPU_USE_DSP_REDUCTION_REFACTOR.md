# S073: DSP CPU reduction refactor (plan)

> **Revised on 2026-09-28 from the user's review and answers.** §0 records
> the governing rule, the per-step decisions and the verification method.
> §2 records the resolved follow-ups. All are closed; F-5 decided **no
> profiler**. The code-level schedule is `S073_CPU_REDUCTION_IMPLEMENTATION.md`.

- **Goal:** lower the worst-case audio render cost **and keep it constant
  and predictable**. Voices must sound the same.
- **Authority:** the Session 073 section of
  `knowledge_files/specification_reference/CPU_USE_DSP_AUDIT.md`. It holds
  findings F1–F10, the cost model, the evidence, and the rejected ideas.
  Sound-impact classes S0–S4 are defined there.
- **Baseline:** the S073 working tree after the flash expansion and the
  Load:[Samples] fix:
  - `text=483,936`, `data=416`, `bss=426,336`;
  - flash payload 484,352 B in the 736 KiB window (269,312 B free);
  - ITCM 3,768 / 16,384 B; DTCM statics 4,448 B.
  - **Flash is no longer a constraint for this plan.**
- **Scheduling:** S072 is closed. Measure on the post-flash-expansion image
  family, because S073 moved `Reset_Handler` and re-laid out `.text`.
- **Scope:** nothing here changes Effect behaviour, files, or AutoSave.

---

## 0. User rules and decisions (2026-09-28)

### 0.1 Governing rule: constant, predictable CPU

**No CPU saving may come from skipping, bypassing or switching off DSP work
because an element is currently inactive, silent, or set to zero.** Examples:
distortion at 0, a silent voice, a send at 0.

- **Why:** CPU freed that way gets filled by other features. Then, when every
  voice, every distortion and the new feature are all requested at once, the
  result is buffer underruns or features being dropped at random.
- **Budget:** headroom is only ever the **worst case with everything
  active**, never the average.
- **Any proposal** that would switch processing off for a feature that can be
  active must be raised with the user **specifically**. The expected answer
  is no.
- **Code-review gate for every step:** the step adds no data- or
  parameter-dependent skip, bypass or early-out.

This rule is also recorded in `MEMORY.md` (DSP CPU Policy).

### 0.2 Per-step decisions and required sound class

| Step | User decision | Required result |
|---|---|---|
| 1. ZDF filter: batch the divisions | Approved | S1 (rounding-level), as planned |
| 2. Descriptor special writers without strings | Approved | S0 |
| 3. DMA pack: word stores and MPU attribute | Approved | S0 |
| 4. Fused voice post-chain | Approved **only if it does not affect the sound** | **S0**. The shape-0 bypass is removed (§7). |
| 5. Mixer dry + send in one pass | Approved **as long as functionality is unchanged**; **D-5: implement** (user, 2026-09-28) | **S0** (§8) |
| 6. Octave selection without `log2f()` | Approved; **check that it is "good enough"** | Differences confined to the octave edges, with the size shown by a host check (§9) |
| 7. Silence gating for idle voices | **Rejected** (§0.1) | — |
| 8. Software noise PRNG | **Rejected**: do not change the noise character | — |

### 0.3 Verification without new tools

**The user decision:** no new utilities (no emulator install). S0 claims are
checked with what the project already has.

1. **Host harness** (`cc`), old against new over the test grid: zero
   differing samples.
2. **ARM codegen check:** disassemble the per-sample loops of the old and new
   code in the linked `build/lxr02.elf` (`arm-none-eabi-objdump`) and compare
   their floating-point operation sequences: kind, order, operands, and the
   conversions and saturations.
   - IEEE single-precision arithmetic is deterministic, so the same sequence
     means the same output bits.
   - If the sequences differ (for example `-Ofast` contracts a multiply-add
     differently in the fused loop), restructure the code until they match,
     or do not merge the step.
3. **User listening check** on the user's kits, as confirmation.

### 0.4 RAM (approved by the user, 2026-09-28)

- **No RAM is added by this plan.**
  - **D1** (the 120 B profiler) is **not needed**: the user declined the
    profiler (F-5).
  - **D4** (a 4 B DTCM PRNG) is **not needed**: Step 8 is rejected.
- Every step's gate confirms that `bss`/`data` are unchanged.
- **Recorded after implementation (2026-09-29): ITCM +400 B of code.**
  After Step 6, `osc_setFreq()` (`INITCM`) is linked as its own function in
  ITCM. Before, it was inlined into its callers in flash.
  - Size: 400 B, plus an 8 B flash veneer.
  - Region: ITCM, 3,768 → 4,168 / 16,384 B.
  - Lifetime: static code.
  - Owner: `Oscillator.c`.
  - `bss`/`data` are unchanged (§13.1, §13.6).

---

## 1. Where the time goes (summary)

The budget is 4,897 cycles per output frame (216 MHz / 44,108 Hz).

- **Worst case:** six sounding voices with drive, 12 LFOs on instrument
  parameters, and StereoFilter with every send open. The audio path is
  estimated at **≈1,650–2,400 cycles/frame, 34–49 % of the CPU**.
- These are static estimates from the disassembly, ±50 %.

| Rank | Cost centre | Est. share | Addressed by |
|---|---|---|---|
| 1 | ZDF filters: 6 voices + 2 FX channels, 5–6 `VDIV` per sample each | 16–21 % | Step 1 |
| 2 | LFO/Morph descriptor writes: string search per write, 1,378 Hz | 1.5–4.5 % | Step 2 |
| 3 | Oscillators | 4–6 % | Step 6 (small); already efficient |
| 4 | Distortion (`VDIV` per sample) and separate post-chain passes | 5–7 % | Step 4: pass fusion only. The division stays (S0). |
| 5 | Mixer dry + send passes | 4–6 % | Step 5 |
| 6 | DMA pack to strongly-ordered memory | 1–1.6 % | Step 3 |
| — | Idle voices render in full | — | **By design** (§0.1). Keeps the cost flat. |

**Expected result of Steps 1–6: about 9–14 % of the whole CPU recovered in
the worst case.** Every recovered cycle is also recovered when everything is
active: none of it comes from skipping work.

---

## 2. Follow-ups

### 2.1 Resolved (user, 2026-09-28)

| ID | Question | Answer, folded in |
|---|---|---|
| F-1 | How to prove S0 on the target. | **No new utilities.** Use the host harness plus the ARM disassembly comparison (§0.3). For Step 6, check on the host that it is "good enough" (§9). |
| F-2 | The existing conditional-cost paths (§3.1). | **Keep them.** The user checks the Effect budget manually. |
| F-3 | Step 8, the software noise PRNG. | **Rejected.** Do not change the noise character. |
| F-4 | The worst-case Scene and the listening kits. | **The user owns both:** the user's own worst-case Scene is the measurement fixture, and the user does the kit listening tests. |
| F-5 | The per-stage profiler. | **Not built** (user: no extra CPU-measuring widgets). The user checks results on the existing `cpu` widget, the underrun count, and the worst-case Scene. Step savings are shown by static instruction counts from the disassembly (the ARM codegen check). |
| F-6 | Commit timing. | Not raised again; commits are the user's. |

---

## 3. Step 0 — Measurement and golden harness (prerequisite)

Nothing after this step is accepted on estimates alone.

### 3.1 Existing conditional-cost paths (kept, F-2)

These exist today and are kept by user decision. The plan adds none.

| Path | Where | Cheap state |
|---|---|---|
| FX bus off | `mixer.c` FX path (S072) | Effect `off`: no bus clear, sum, convert or process |
| Per-voice send skip | `mixer_calcNextSampleBlock()` slot loop (it was in `mixer_addVoiceToFxBus()`, which Step 5 removed) | `!fx_active`, or both send gains 0. Then the dry-only function runs instead of the combined dry + send function. The condition is unchanged. |
| Effect coefficient recompute | `effects_service()` → `stereoFilter_writeParam()` | parameter unchanged |
| Algorithm choice | filter type, oscillator waveform, Effect type | naive 2-pole filter; cheaper waveforms |

The user checks the Effect budget manually. The Drum velocity-gain stage
(`if (voice->volumeMod)`) was the one other conditional stage in the render.
Step 4 makes it constant-cost, bit-identically (§7).

### 3.2 0a. Per-stage cycle profiler — NOT BUILT (F-5)

The user declined it. The text below is kept only as the record of what
was proposed.

- **What:**
  - it reads the Cortex-M7 cycle counter (`DWT->CYCCNT`, already enabled for
    `audioCodec_getQueueFreePercent()`) before and after each render stage;
  - it keeps the **last** and **peak-since-reset** cycles per stage;
  - it shows them on a Menu widget beside the existing `cpu` widget, and an
    encoder press resets the peaks.
- **Why:** the existing `cpu` widget shows only overall audio-queue pressure.
  It cannot attribute cost to a stage, and it cannot reliably show a 1–2 %
  saving from a single step. The profiler shows the **peak** per stage, which
  is the number the constant-CPU rule budgets against.
- **Stages:**
  1. `modNode_resetTargets()` + `modNode_reassignVeloMod()`
  2. `instrumentManager_dispatchRuntimeLfos()`
  3. `recalcSlotFilter` + `calcSlotAsync` (6 slots)
  4. `effects_service()`
  5–10. `instrumentManager_calcSlotSyncBlock()` per slot
  11. mixer per-slot work (decimate + send + dry), summed over 6 slots
  12. FX convert + `effects_process()` + return
  13. the whole `mixer_calcNextSampleBlock()`
  14. `pack_audio_half()` (DMA ISR)
  15. `TIM3_IRQHandler()` (sequencer ISR)

  15 stages × last/peak × 4 B = **120 B**.
- **Cost when built in:** about 30 cycle-counter reads per 32-frame block, a
  few cycles each. That is negligible against the 156,707-cycle block
  budget.
- **Contract:** with the switch at 0 it compiles out completely, and the
  release `bss` is byte-identical to the baseline.

### 3.3 0b. Measurement fixture (user-owned, F-4)

- **Measurement:** the user's own worst-case Scene is the fixture for the
  underrun checks and the `cpu` widget.
- **Listening:** the user tests the kits.
- **Effects:** the user checks the Effect budget manually (F-2).

### 3.4 0c. Golden harness (`tools/dsp_golden/`)

- **Host program** built with `cc` (no target RAM). It compiles the changed
  DSP functions and a frozen copy of the pre-change functions side by side.
  Stubs cover `INITCM*`, `__SSAT` and `__QADD16`.
- **Inputs:** a fixed grid of signals (saw, full-scale square, white noise,
  impulses, sine at 40/220/1,760/7,000 Hz) × parameter sweeps.
  - Filter: 7 types × f × reso × drive.
  - Post-chain: every engine × drive 0..max × velocity-to-volume on/off × EG
    ramps.
  - Mixer: every routing case × fader mode × stereo/mono type × ramp.
- **Outputs:**
  - samples differing;
  - max |Δ| in LSB;
  - signal-to-difference ratio;
  - the configurations that diverge by more than 16 LSB.
- **Rounding baseline (S1 only):** each report also runs the frozen code
  against itself built with `-ffp-contract=off`, to show how much divergence
  rounding alone causes.
- **Acceptance by class:**
  - **S0:** zero differing samples on the host, **and** the ARM codegen check
    (§0.3) shows the same per-sample float operation sequence.
  - **S1:** ≤ 1 LSB everywhere outside the self-oscillating configurations;
    every configuration over 16 LSB lies in the same self-oscillating family
    where the rounding-only baseline also diverges; SDR ≥ 80 dB; no NaN/Inf.
    This replaces the earlier "subset ±10 %" count rule; pre-validation
    measured 13 against 10 configurations, all in that family
    (`S073_CPU_REDUCTION_IMPLEMENTATION.md` §13).

**Gate 0:**

- the harness reproduces the Session 073 filter numbers (§4);
- release `bss`/`data` are unchanged;
- the ARM codegen check tooling runs on the unchanged tree and reports
  identical sequences for old against old.

---

## 4. Step 1 — ZDF filter: batch the divisions (S1, ≈6–7 % CPU) — approved

**Files:** `Core/DSPAudio/ResonantFilter.c`:

- `SVF_calcBlockZDF()` (line 154);
- `SVF_calcBlockZDFFloat()` (~332). This is the float-I/O twin used only by
  Effects. It has no `__SSAT`, uses float-suffixed literals, and carries a
  `#error` guard because it mirrors only the non-shaper configuration.

**Why:** see F1. Five of the six divisions per sample are reciprocals whose
denominators are known at the same time. They can be inverted together with
one division and a few multiplies. That is exact algebra; only rounding
changes.

**Change** (non-naive types, `ENABLE_NONLINEAR_INTEGRATORS == 1`,
non-shaper):

1. Normalise the Padé pieces so every denominator is ≥ 1. The ratio is
   unchanged, and products of denominators stay far from float overflow:
   `tanhXdX(v) = N/D` with `a = v²`,
   `N = (a·(1/945) + 105/945)·a + 1` and
   `D = (a·(15/945) + 420/945)·a + 1`.
2. **Stage A (one division).** Soft-clip the input and compute
   `t1 = tanhXdX(s1/2)` together:
   `u = in·drive/32767`, `invA = 1/(Dx·D1)`, `x = u·Nx·D1·invA`,
   `t1 = N1·Dx·invA`.
3. Make the existing CSE explicit: `softClipTwo(s1) ≡ s1·t1`.
4. **Stage B (one division).** With `v0 = ½(½(x+zi) − 2R·s1 − s2)` and
   `N0, D0` from it:
   - `E = D0 + 2fR·N0` (so `g0 = D0/E`);
   - `P = f²·N0·t1` (so `f1 = P/E`);
   - `F = P + E`;
   - `Q = P·x + s2·E + f·D0·t1·s1` (so `y1 = Q/F`).

   Then `invB = 1/(D0·E·F)`, and
   `t0 = N0·E·F·invB`, `g0 = D0²·F·invB`, `y1 = Q·D0·E·invB`.
5. The rest of the loop is unchanged:
   `xx = t0(x−y1)`, `y0 = (s1·t1 + f·xx)·g0`,
   `s1' = s1·t1 + 2f(xx − 2R·t0·y0)`, `s2' = s2 + 2f·t1·y0`.
   The output switch is unchanged; LP keeps its `fastTanh()`.
6. Keep `s1/s2/zi` in locals across the loop and write them back once, as
   GCC already does.
7. Mirror the identical change in `SVF_calcBlockZDFFloat()`.
   - The StereoFilter runs **two** instances (L/R) with linked
     coefficients: `stereoFilter_linkCoefficients()` copies `f`, `g`, `q` and
     `drive`. Mirror any state or coefficient layout change there.
8. Leave `FILTER_NAIVE_2_POLE` alone: one division per sample, and `q` is
   already hoisted.
9. **No new conditional path:** every sample of every ZDF type runs the same
   batched sequence.

**Evidence already gathered (Session 073 host run):**

- 28,800 configurations, 276 M samples: 99.77 % identical, SDR 81 dB.
- Larger deviations appear only in 56 self-oscillating high-reso
  configurations. The rounding-only baseline diverges in 54 of the same
  ones.
- ARM `-Ofast` build: 2 divisions per sample (LP 3) versus 6 today; about
  +340 B per variant.

**Gate 1:**

- The harness passes S1 acceptance for all 7 types, for both variants.
- The user's worst-case Scene runs 10 minutes with `audioCodec_underrunCount`
  unchanged or lower.
- User listening check on the kits, plus a high-reso self-oscillating patch.
  Chaotic cases differ in phase only, as they do between compiler versions.
- Flash delta recorded.

---

## 5. Step 2 — Descriptor special writers without strings (S0, ≈1.5–4.5 % CPU + foreground) — approved

**Files:**

- `Core/DSP/Instruments/InstrumentManager.h`: `instrument_runtime_binding_t`
  and a new writer enum;
- `InstrumentManager.c`: `instrumentManager_writeSpecialRuntime()` (line
  2917) and `instrumentManager_osc()` (line 1820);
- the four `*Parameters.c` tables.

**Why:** see F2. The key → writer mapping is fixed per descriptor row, but it
is recomputed on every LFO block, Morph worker pass, velocity write and Scene
activation, with up to 20 `strcmp`, 2 `strstr` and 7 `strncmp`.

**Context from S072:**

- `INSTALLED_MOD_TARGET_EFFECT` and the shared
  `instrumentManager_lfoDirectionDepth()` were added.
- Effect parameters never go through `writeSpecialRuntime()`; they are
  applied by `effects_service()` → `write_param`. So this step does not touch
  the Effect path.

**Change:**

1. Add `uint8_t special;` after `parameter_type`, in the existing padding
   byte. Add `_Static_assert`s that `sizeof(instrument_runtime_binding_t)`
   and `sizeof(ParamDescriptor)` are unchanged, so the flash tables do not
   grow.
2. Define the writer enum:
   - `IM_SPECIAL_NONE = 0`, `NOISE_FREQ`, `PITCH_COARSE`, `PITCH_FINE`,
     `FILTER_FREQ`, `FILTER_RESO`, `FILTER_DRIVE`, `FILTER_TYPE`,
     `HAT_DECAY_CLOSED`, `HAT_DECAY_CHOKE`, `AMP_ATTACK`, `AMP_DECAY`,
     `AMP_SLOPE`, `PITCH_EG_DECAY`, `PITCH_EG_SLOPE`, `PITCH_EG_AMOUNT`,
     `TRANSIENT_WAVE`, `TRANSIENT_FREQ`, `INSTRUMENT_DRIVE`, `LFO_RATE`;
   - bits 5–6 select the oscillator (osc1 / osc2 / osc3 / noise), replacing
     the `osc1_`/`osc2_`/`osc3_`/`noise_` prefix match.
3. Add a `ROW_SPECIAL(...)` macro (or an extra argument to `BIND`). Tag
   exactly the rows whose keys match today's chain; every other row keeps
   `special = 0`.
4. Rewrite `writeSpecialRuntime()` as a single
   `switch (descriptor->runtime.special)`.
   - Each case calls exactly the function that today's matching branch
     calls, with the same arguments.
   - Keep the HiHat `amp_envelope_decay` closed-cache branch, keyed by writer
     ID plus slot type, exactly as today.
5. **Self-check:** under `DEV_MODE_DIAGNOSTIC`, keep the old key matcher as
   a pure classifier. At boot, compare its result with every row's `special`
   across all four types, and show any mismatch on the existing diagnostic
   screen. No RAM needed.

**Gate 2:**

- The self-check passes on hardware.
- A trace or diagnostic comparison shows the same writer calls and values
  for the worst-case Scene's LFO, Morph and velocity targets.
- **Hardware regression:**
  - Instrument Load, Kit Load, Scene switch;
  - LFO rebind and per-voice Morph;
  - the HiHat closed/choke decay pair;
  - the slot-6 track-7 alternate decay.
- Flash delta recorded (expected ≤ 0).

---

## 6. Step 3 — DMA pack path (S0, ≈1 % CPU, shorter ISR) — approved

**Files:**

- `Core/Hardware/AudioCodecManager.c`: `pack_half()` (line 382),
  `pack_audio_half()`, and the buffer declarations (lines 215–216);
- `Core/Hardware/clocks.c`: MPU region 1 (lines 187–193).

**3a. Word stores.**

- Declare `dma_buffer` and `dma_buffer2` `aligned(4)`.
- Write each channel frame as one 32-bit store:
  `ror16(((uint32_t)(s24 & 0x00FFFFFF)) << 8)`. In little-endian memory this
  puts the MSW at the lower halfword, which is the same memory image as
  today's two halfword stores.
- DMA stays in halfword mode.
- This halves the non-bufferable store count.

**3b. MPU attribute.**

- Change region 1 from Strongly-Ordered (TEX=0 C=0 B=0) to Normal
  non-cacheable (TEX=1 C=0 B=0, S=1, XN=1). Stores then go through the write
  buffer.
- Coherency with DMA is unchanged, because nothing is cached.
- End `pack_audio_half()` with `DSB`, so the half is complete before the ISR
  returns.
- The ADC scan buffer shares the region. Its reads stay uncached, so they
  remain correct.

**Gate 3:**

- **Bit-identical memory image:** the host harness packs a fixed reference
  block with the old and new pack functions and compares the bytes (0
  differences). The ARM codegen check confirms the stores write the same
  words.
- Sliders and endless pots read correctly (ADC DMA shares the region).
- Audio output is unchanged by ear on the user's kits.
- Land 3a and 3b as separate commits, each measured.

---

## 7. Step 4 — Fused voice post-chain (S0) — approved on condition

**User condition:** the distortion work must **not** affect the sound.

**Revised from the original plan:**

- **Removed: the distortion bypass at shape 0.** It would make the cost
  depend on a control setting (§0.1). It would also change today's output:
  at shape 0 the `x/32767·32767` round trip can lose one LSB, and the bypass
  would not.
- **Removed: any intermediate-truncation change** (the old S2 class). The
  fused loop reproduces every current int16 conversion exactly.
- **Kept: the distortion division.** No reciprocal or other rewrite of
  `(1+k)x/(1+k|x|)`.

**Files:**

- `DrumVoice.c` (lines 340–350);
- `Snare.c` (line 262), `CymbalVoice.c` (line 272), `HiHat.c` (line 291);
- `distortion.c` / `distortion.h`: move the per-sample curve into one
  `static inline` helper, used by `calcDistBlock()` and by the fused loops,
  so both evaluate the identical expression.

**Change:**

- **Rule:** per sample, the fused loop computes exactly the int16 value each
  of today's passes would have stored, with the same conversion or
  saturation. It feeds that value to the next stage, in registers instead
  of through memory.
- **Drum:** one loop replaces the three passes (interpolated amp EG,
  velocity gain, `calcDistBlock`):
  1. `t = (int16)(x · lerp(lastGain, ampFilterInput))`, as
     `bufferTool_addGainInterpolated()` does;
  2. `t = (int16)(t · g_velo)`, as `bufferTool_addGain()` does;
  3. `t = (int16)(dist(t/32767.f) · 32767)`, as `calcDistBlock()` does.
  - **Constant cost:** `g_velo = volumeMod ? velo : 1.0f`, and the velocity
    multiply always runs. This is bit-identical, because an int16 value
    times 1.0f converts back unchanged. It removes today's conditional
    `if (voice->volumeMod)` stage rather than adding one.
- **Snare / Cymbal / HiHat:** move the distortion into their existing fused
  mix × EG × velocity loop, applied to the value that loop stores today. Keep
  `bufferTool_satAdd16()` saturation exactly where it is.
- **Expected saving:** about 0.5–1.5 %, smaller than the original 1.5–3 %
  because the division stays. The fusion removes 2 of 3 load/convert/store
  passes on Drum and 1 on the other engines.

**Gate 4:**

- S0 per §0.3:
  - host harness: zero differing samples per engine, across drive 0..max,
    velocity-to-volume on/off, EG attack/decay ramps and the signal grid;
  - the ARM codegen check shows the same per-sample float sequence.
  - Any difference means the step is not merged.
- User listening check on the kits at drive 0 and drive max.
- Code review: no data- or parameter-dependent branch in the fused loops.

---

## 8. Step 5 — Dry path and FX send in one mixer pass (S0) — approved on condition

**User condition:** functionality must not change.

**Files:** `Core/DSPAudio/mixer.c`:

- `mixer_addVoiceInt16ToOutput()` (line 409);
- `mixer_addVoiceToFxBus()` (line 543);
- the slot loop (lines ~810–835).

Re-verify the line numbers: S073 edits may move them.

**Contract to preserve exactly (from S072):**

- The send taps the **decimated, pre-volume** block.
- Dry gain = `vol × F_mix`; send gain = `fxSend/127 × F_send` (fader modes
  in `mixer.h` `MIXER_FADER_*`).
- The send converts `float × 256` directly to `sample_mx_t`, **without** the
  dry path's int16 truncation. The two expressions differ on purpose.
- **Stereo-input types** get the voice panned into L and R with
  `squareRootLut`. **Mono-input types** get it unpanned in L only.
- `mixer_send_last_gain[slot]` is updated every block, even when the send is
  skipped. The skip happens when `!fx_active` or when both gains are 0.
  Re-enabling an Effect depends on that update.
- The bus accumulates with `bufferTool_satAdd32()`.

**Change:**

- When the Effect is active and the send ramp is non-zero, use a combined
  per-destination loop. It reads `data[i]` once and produces both today's
  dry expression and today's send expression, keeping each expression's
  operand order.
- The dry-only path is unchanged when the send is off. That is an existing
  conditional path, kept by user decision (§3.1).

**Decision D-5 (user, 2026-09-28): implement.**

- It is done last in the order.
- The combined function replaces `mixer_addVoiceToFxBus()`, which is then
  removed. The dry-only function stays for voices with an inactive send.
- The static instruction counts from the ARM codegen check are recorded in
  the audit as the measured saving.
- Flash is about +0.7–1 KB net. The combined function multiplies the six routing cases by stereo/mono
send, so it costs about +1 KB of flash.

**Gate 5:**

- S0 per §0.3: host harness zero differing samples for every routing case ×
  fader mode × stereo/mono type × ramp, plus the ARM codegen check. **Any
  difference means the step is not merged.** No S1 fallback.
- The user's manual FX check: send and return in all fader modes, stereo
  panning of sends, `flt` at high reso/drive, a Scene switch to `off`, and
  the Effect budget.

---

## 9. Step 6 — Octave selection without `log2f()` — approved on condition

**User condition:** check that the sound impact is extremely minimal ("good
enough"). No new tools.

**Files:** `Core/DSPAudio/Oscillator.c`, `freqToTableIndex()` (line 63).

**Today:** `clamp((int)((69 + 12·log2f(f/440)) / 12), 0, 10)`. That is the
count of octave edges `T_k = 440·2^(k − 5.75)` (k = 1..10, 16.35 Hz …
8,372 Hz) at or below f, up to float and `log2f` rounding at each edge.

**Change:**

- Replace the expression with a count of how many of the 10 constant
  thresholds `T_k` are ≤ f. The table is 40 B of flash, and the step removes a
  `log2f` call per wavetable oscillator per block during pitch sweeps.
- The count runs for every call (no new conditional path).
- Remove `log2f` from the image only if it has no other user (a small flash
  gain; not a goal).

**What can differ:**

- Only which of two adjacent band-limited octave tables is used, and only for
  f within a few float ulps of one of the 10 edges: a relative band of about
  1e-7, roughly 0.0002 cents.
- Both tables are valid at the edge, and today's code already switches
  tables there. The switch point moves by that fraction; nothing else
  changes.

**Gate 6 ("good enough" check, host only):**

- **Exhaustive comparison** of old and new over every float in the reachable
  range (the lowest reachable f to 22,050 Hz), using the host `log2f` for the
  old expression.
- **Report:**
  - the number of mismatching inputs;
  - which edges they sit at;
  - the maximum distance from an edge, in ulps and in cents.
- **Accept** if every mismatch lies within a few ulps of an edge (target:
  ≤ 8 ulps, well under 0.001 cents).
- The host `log2f` may round differently from the target's in the last ulp,
  which can shift a mismatch by about one ulp. This does not change the
  conclusion.
- A pitch-sweep listening check by the user, as confirmation.

---

## 10. Steps outside the plan

### Step 7 — Silence gating for idle voices: **REJECTED** (§0.1)

It skipped rendering for silent voices. That lowers only the average load,
and it frees CPU that would later be filled. That is exactly the failure the
governing rule forbids. Recorded here only; do not implement.

### Step 8 — Software PRNG for audio-rate noise: **REJECTED** (user, F-3)

It would have changed the noise character. The hardware RNG read in
`calcNoiseBlock()` stays as it is.

---

## 11. Not recommended (see the audit's "Checked and rejected" table)

- Filter or distortion in ITCM (measured worse).
- A write-back SRAM cache.
- Runtime slots in DTCM (reserved FX arena).
- The Morph skip cache (Failed Approach, Session 16).
- A larger control block.
- The decimator short-circuit (declined: it hides the real budget; also
  against §0.1).
- Nonlinear integrators off, a half-rate LFO, or an "eco" filter mode (old
  D5). These are audible (S4) and, as automatic savings, against §0.1. Not
  planned; any such idea goes to the user first.
- Any other skip, bypass or early-out keyed on a control value or on silence
  (§0.1).

---

## 12. Order, gates, and closeout

1. **Step 0:** the golden harness and the ARM codegen check (no profiler).
2. **Step 1**, then **Step 2**. These are the largest and they are
   independent. Land and measure each on its own.
3. **Step 3a**, then **3b**.
4. **Step 4** (S0).
5. **Step 6** (good-enough check).
6. **Step 5** (S0; D-5 decided: implement).
7. Re-measure.

**Every step:**

- `make clean` + `make all` build.
- `tools/link_budget.py`: flash delta recorded; ITCM/DTCM unchanged.
- `arm-none-eabi-size`: `bss`/`data` unchanged, except approved items.
- Acceptance at the class in §0.2, verified per §0.3.
- **Code review: no new data- or parameter-dependent skip, bypass or
  early-out (§0.1).**
- The user's worst-case Scene: 10 minutes with no new underruns.
- The user's listening check on the kits.

**Closeout:**

- Update `CPU_USE_DSP_AUDIT.md`: statuses and measured numbers. Record
  Steps 7 and 8 as rejected, and Step 4 as S0 without the bypass.
- Write the `073_SESSION_HANDOFF_LOG.md`.
- This plan is then superseded by that log and the audit.

---

## 13. Implementation review (commit `692abf8`, 2026-09-29)

**Verdict:** the code matches `S073_CPU_REDUCTION_IMPLEMENTATION.md` step by
step, and every host and ARM codegen gate passes. The user's hardware test
passed (2026-09-29, §13.7).

### 13.1 Build and memory

| Item | Baseline | After | Delta |
|---|---|---|---|
| `text` | 483,936 | 486,688 | +2,752 |
| `data` / `bss` | 416 / 426,336 | 416 / 426,336 | 0 (§0.4 holds) |
| Flash payload (736 KiB window) | 484,352 | 487,104 of 753,664 | +2,752 (266,560 B free) |
| ITCM | 3,768 / 16,384 | 4,168 / 16,384 | **+400** (§13.4, finding 2) |
| DTCM statics | 4,448 | 4,448 | 0 |

- The build (`make clean` + `make all`) finishes with no new warnings.
- The `DEV_MODE_DIAGNOSTIC` build also compiles and links. Its only warning
  is the unused `boot_showHcnamesDiagnostic`, which was there before.
- Per-function flash deltas, measured against a scratch build of the
  pre-change tree:
  - `mixer_calcNextSampleBlock`: +3,060 B (LTO inlines the combined mixer
    loop and the fused post-chains into it);
  - `SVF_calcBlockZDF`: +416 B;
  - `SVF_calcBlockZDFFloat`: +304 B;
  - the runtime writer (`writeRuntimeInternal`): +352 B;
  - `pack_half`: −16 B;
  - `log2f`: −208 B, because it is gone from the image;
  - the descriptor tables are unchanged in size.

### 13.2 Results per step

All results come from the `tools/dsp_golden` targets. `selftest` (frozen
code against itself) reports identical output, so the harness is valid.
The 14 frozen files are byte-identical to `05bbd83`.

| Step | Target(s) | Result | Class |
|---|---|---|---|
| 1. ZDF filter, batched divisions | `filter`, `armcheck-filter` | S1 PASS. int16 SDR 91.69 dB, float 86.55 dB. Every case over 16 LSB is in the self-oscillating family (cutoff 0.8, resonance 0.98). `vdiv` count 81 → 39. | S1, as approved |
| 2. Descriptor special writers | `special_tags` | 155 rows, 0 mismatches against the old classifier. The `_Static_assert`s hold (binding 6 B, `ParamDescriptor` 28 B). `strcmp`/`strstr`/`strncmp` calls in the writer path: 22 → 0. | S0 |
| 3. DMA word stores + MPU | `pack` | 0 differing bytes. The final ELF's `pack_half` has 2 `str` + 2 `ror` and no `strh`. `dsb` ends `pack_audio_half`. The buffers are 4-byte aligned (`0x2002001c`, `0x2002061c`). MPU region 1 is TEX=001, C=0, B=0, S=1, XN=1. | S0 |
| 4. Fused voice post-chain | `postchain`, `armcheck-postchain` | 0 differing samples. ARM MATCH for Drum, Snare, and Cymbal/HiHat. The distortion division stays (`mixer_calcNextSampleBlock` `vdiv` 7 → 7). `strh` 26 → 18. | S0 |
| 5. Mixer dry + send in one pass | `mixer`, `armcheck-mixer` | 0 differing samples. ARM MATCH for all four dry × send combinations and for the default (send-only) case (§13.6). Instructions per sample, dry + send against the combined loop: single-output dry 55 → 44 (mono-input send) and 69 → 59 (stereo-input send); stereo dry 73 → 63 and 87 → 78. `mixer_addVoiceToFxBus()` is removed; the `default` routing case is reproduced. | S0 |
| 6. Octave selection by table | `octave` | Exhaustive check: 16 mismatches, all within 2 ulps of an edge (0.000404 cents). The acceptance target was ≤ 8 ulps; it is met. `log2f` is gone from the image. | S0 except those edge choices, as approved |

### 13.3 §0.1 code review (constant CPU)

- **No new data- or parameter-dependent skip, bypass or early-out.**
- The Drum velocity stage is now constant-cost: `volumeMod ? velo : 1.0f`
  goes into a multiply that always runs. The conditional
  `if (voice->volumeMod)` stage is gone.
- The only conditional at the mixer call site is the existing send condition
  (`fx_active` and a non-zero send ramp), kept by user decision (§3.1, F-2).
  The combined function runs exactly when the old send used to run.
  `mixer_voice_last_gain` and `mixer_send_last_gain` still update every
  block.
- The filter's type switch is the existing algorithm choice (§3.1).
- Steps 7 and 8 are not implemented.

### 13.4 Findings

1. **DONE (§13.6): audit item 21 had the wrong label.**
   `CPU_USE_DSP_AUDIT.md` item 21 calls the "55 old versus 44 combined" pair
   the "DAC1-stereo loop". The loops it measured (`old_dry:1` +
   `old_send:0` against `new_combined:0`) are a single-output routing: the
   dry loop has one pan multiply. A later check found that this pair also
   has a mono-input send. The full set of four combinations is in §13.6.
2. **DONE (§13.6): ITCM +400 B.** `osc_setFreq` (placed in ITCM) now has a standalone copy
   instead of being inlined into every caller. §12 expected ITCM to be
   unchanged. Details for the RAM Allocation Approval Policy:
   - 400 B;
   - region: ITCM;
   - lifetime: static code;
   - owner: `Oscillator.c` `osc_setFreq()`.

   That leaves 12,216 B of ITCM free.
3. **DONE (§13.6). Cosmetic:** at `mixer.c:966`, the `sampleData` argument
   line was one tab short of its neighbours.
4. **Tracked binaries:** `LXRV2_update_v1.70.img` (the Erica factory
   firmware) is tracked by git. `build/LXRV2_lxr02.img` is also tracked,
   and it is currently deleted in the working tree.
5. **DONE (§13.6): `fpseq.py --shared-op`.** The implementation added this option. It
   removes named conversions from the reference multiset before the
   comparison. It is used only for the mixer, for one `vcvt.f32.s32` and one
   `vcvt.f32.u32`. The old dry and send functions each converted the same
   input sample and the same loop index; the combined loop converts them
   once. This is legitimate, because the conversions are exact and
   identical, but it does loosen that one check.

### 13.5 User-owned hardware gates (result in §13.7)

- **Step 1:** worst-case Scene for 10 minutes with no new underruns; kit
  listening, plus a high-resonance self-oscillating patch.
- **Step 2:** the diagnostic self-check on hardware (the `s` digit on the
  `FxBf` row); Instrument Load, Kit Load, Scene switch, LFO rebind,
  per-voice Morph, the HiHat closed/choke decay pair, and the slot-6 track-7
  alternate decay.
- **Step 3:** sliders and endless pots after the MPU change (the ADC DMA
  shares the region); audio unchanged by ear.
- **Step 4:** kit listening at drive 0 and drive max.
- **Step 5:** the manual FX check (send and return in all fader modes, stereo
  send panning, `flt` at high resonance and drive, Scene switch to `off`)
  and the Effect budget.
- **Step 6:** a pitch-sweep listening check.
- **Closeout (§12):** write `073_SESSION_HANDOFF_LOG.md`. The audit item
  21 correction and the ITCM record are done (§13.6).
- **Outside this plan:** the `FLASH_GROWTH_DRILL_KB` knob is removed
  (§13.6).

### 13.6 Corrections done (2026-09-29)

Findings 1, 2, 3 and 5, the send-condition documentation, and the drill
knob. Finding 4 (tracked binaries) is unchanged.

| Item | What was wrong | Correction |
|---|---|---|
| Finding 1: mixer instruction counts | The audit and the implementation notes called the measured pair "DAC1-stereo". It was a single-output routing with a mono-input send. The gate also checked only that one combination. | `CPU_USE_DSP_AUDIT.md` item 21 now has a table of all four combinations: single-output dry 55 → 44 / 69 → 59 and stereo dry 73 → 63 / 87 → 78 (mono / stereo-input send). The Makefile `armcheck-mixer` target now gates one loop of each combination plus the default case, 6 MATCH lines, and reports the four instruction counts. Its comment names each loop index. `S073_CPU_REDUCTION_IMPLEMENTATION.md` has corrections in its progress notes and in §9.4. |
| Finding 2: ITCM +400 B | §0.4, the implementation notes ("No RAM was added") and the audit did not record it. | Recorded in §0.4, in audit item 22 (with the 8 B veneer and the before/after ITCM totals), and in the implementation notes. The cause is confirmed from the symbol tables: the baseline has no `osc_setFreq` symbol, and the new image has `osc_setFreq` at ITCM `0x00000000`, 400 B. |
| Finding 3: indentation | `mixer.c:966` was one tab short. | Fixed. |
| Finding 5: `--shared-op` | The allowance removed a named operation whenever the combined reference had one, even if only one old loop performed it. | `fpseq.py` now removes a shared operation only while at least one other `--ref` loop still performs it. Misuse exits with an error, which was tested with a send-only reference and with a doubled flag. The header and the in-code comment say that operand identity is the caller's claim, backed by the host harness. `armcheck-postchain` (3 MATCH) and `armcheck-filter` (`vdiv` 81 → 39) still pass. |
| Send condition | §3.1 named the removed `mixer_addVoiceToFxBus()` as the location of the per-voice send skip. | §3.1 now names the slot loop in `mixer_calcNextSampleBlock()`. The condition itself is unchanged and existing (F-2). |
| Drill knob | `FLASH_GROWTH_DRILL_KB` was still in `config.h`. | Removed from `config.h`, `flashImage.c` (the table, the hex helper and the OK screen) and the `flashImage.h` contract. `MEMORY.md`, `SCOPING_TARGETS.md`, `S073_FLASH_EXPANSION.md` §11 and `S073_POST_FLASH_MENU_BUGFIXES.md` (item C done) are updated. D-C1 (the boot image check) has not been decided; the check is left in place. |
| Also | The `freqToTableIndex()` comment gave a stale line number (`Oscillator.c:904`). | The line number is removed. |

**Verification:**

- `make clean` + `make all`: the same 20 warnings as before, none from the
  edited files.
- Sizes: `text=486,688`, `data=416`, `bss=426,336`; flash 487,104 /
  753,664 B; ITCM 4,168 B; DTCM statics 4,448 B.
- `lxr02.bin` is byte-identical to the reviewed `692abf8` build (SHA-256
  `1bd8be52…5fc82`). None of these edits changes the firmware image.
- `make -C tools/dsp_golden armcheck-mixer`: host mixer 0 differing
  samples, and 6 MATCH.

### 13.7 Hardware test result (user, 2026-09-29)

- **Result: OK.** The user's words: the hardware test "seems ok".
- **CPU:** about **10 % less CPU use** on the user's worst-case Scene with
  the dual-filter Effect (StereoFilter: two float ZDF instances, L/R).
- The §1 estimate for Steps 1–6 was 9–14 % of the whole CPU recovered in the
  worst case.
- The saving comes from doing the same work with fewer operations, not from
  skipping work (§0.1, §13.3). It therefore holds with every voice, send and
  Effect active.
- **Image:** the §13.6 corrections leave `lxr02.bin` byte-identical to the
  `692abf8` build, so the result covers both trees.
- **Still open:** the closeout (§12): write `073_SESSION_HANDOFF_LOG.md`.
