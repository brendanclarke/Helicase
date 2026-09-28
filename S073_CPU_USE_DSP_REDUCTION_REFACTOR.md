# S073: DSP CPU reduction refactor (plan)

- **Goal:** lower the worst-case audio render cost so the Phase 5 Effect bus
  and the Phase 7 voices and Effects have headroom. Voices must sound the
  same: every recommended step is bit-identical, rounding-level, or
  requantisation-level. Steps that change the sound are optional and are
  labelled as such.
- **Authority:** the Session 073 section of
  `knowledge_files/specification_reference/CPU_USE_DSP_AUDIT.md`. It holds
  the findings F1–F10, the cost model, the evidence, and the rejected ideas.
  Sound-impact classes S0–S4 are defined there.
- **Baseline:** `dev-ph5-effects` after S072 Step 9. `text=482,632`,
  `data=416`, `bss=426,336`. 8,472 B free inside the 480 KiB application
  window. That window ends where sample flash starts (sector 6); it is not
  the chip's limit (see D2). ITCM 3,768 / 16,384 B. DTCM statics 4,448 B.
- **Scheduling:** start after S072 Step 10 closes. Steps 1 and 5 touch
  `SVF_calcBlockZDFFloat()` and the mixer FX path that S072 owns. Nothing
  here changes Effect behaviour, files, or AutoSave.
- **RAM policy:** Steps 1–6 add **no** RAM, and each step's gate confirms
  `bss`/`data` are unchanged. Step 0's profiler and Step 8's PRNG state need
  your acknowledgement of byte count, region, lifetime, and owner under
  `MEMORY.md`'s RAM Allocation Approval Policy (decisions D1, D4).

---

## 1. Where the time goes (summary)

The budget is 4,897 cycles per output frame (216 MHz / 44,108 Hz). In the
worst case (six sounding voices with drive, 12 LFOs on instrument
parameters, StereoFilter with every send open) the audio path is estimated
at **≈1,650–2,400 cycles/frame, 34–49 % of the CPU**. These are static
estimates from the disassembly, ±50 %, and Step 0 replaces them with
measurements.

| Rank | Cost centre | Est. share | Addressed by |
|---|---|---|---|
| 1 | ZDF filters: 6 voices + 2 FX channels, 5–6 `VDIV` per sample each | 16–21 % | Step 1 |
| 2 | LFO/Morph descriptor writes: string search per write, 1,378 Hz | 1.5–4.5 % | Step 2 |
| 3 | Oscillators | 4–6 % | Step 6 (small); already efficient |
| 4 | Distortion (`VDIV` per sample) and separate post-chain passes | 5–7 % | Step 4 |
| 5 | Mixer dry + send passes | 4–6 % | Step 5 |
| 6 | DMA pack to strongly-ordered memory | 1–1.6 % | Step 3 |
| — | Idle voices render in full | average only | Step 7 (optional) |

**Expected result of Steps 1–6:** about **10–15 % of the whole CPU**
recovered in the worst case, roughly a quarter to a third of the estimated
audio load. The output stays within rounding of today's.

---

## 2. Decisions needed from you

| ID | Question | Recommendation |
|---|---|---|
| D1 | Approve the Step 0 profiler: **120 B** normal SRAM1 `.bss` (15 stages × last/peak `uint32_t`), compiled only when `DEV_MODE_DIAGNOSTIC` and a new `DSP_PROFILE` flag are both 1, owned by `mixer.c`, lifetime = whole run in that build only? | Approve. Without it, per-stage savings cannot be shown. |
| D2 | None needed for S073. Its worst-case net flash growth is about +2 KB (Step 1 ≈ +700 B, Step 5 ≈ +1 KB, Step 2 expected to shrink the image), which fits the 8,472 B left in the current window. Growing the window itself is a separate decision (see the note below). | No S073 action. |
| D3 | Step 7 silence gating: implement it behind `DSP_IDLE_VOICE_SKIP`, default 0? | Defer until Steps 0–6 are measured. It does not lower the worst case you budget against. |
| D4 | Step 8 software noise PRNG: **4 B** DTCM static, owner `random.c`, whole-run lifetime. It changes noise statistics slightly (S3). | Only if Step 0 shows RNG reads matter. |
| D5 | Should a user-selectable "eco" filter (nonlinear integrators off, S4) ever be offered for Phase 7 voice budgets? | Not now; revisit with Phase 7 measurements. |

**Flash note (outside S073).** The application window
`0x08008000–0x0807FFFF` (480 KiB) exists only because sample flash starts at
sector 6. The chip has 2 MiB, and sectors 6–11 (1.5 MiB) hold user samples,
which can be reinstalled from the card. Moving the sample floor to sector 7
gives the application another 256 KiB and leaves 1.25 MiB for samples. The
code change is small:

- the linker `FLASH` length and the `_etext <= 0x08080000` ASSERT;
- `SAMPLE_ROM_START_ADDRESS` (`SampleMemory.h:75`);
- the sector-6 floor in `sampleFlash.c` and the sample loader's erase range.

Sample metadata sits at the top of flash and does not move. The one open
question is the closed LXRV2 bootloader: nobody has tested what it does with
a payload over 0x78000 bytes (`S072_ST1_IMPLEMENTATION.md` §21, finding 4).
That test belongs to the SCOPING §5.5 growth decision, not to this CPU plan.
It is planned in `S073_FLASH_EXPANSION.md`.

---

## 3. Step 0 — Measurement, stress fixture, golden harness (prerequisite)

Nothing after this step is accepted on estimates alone.

### 0a. Per-stage DWT profiler (diagnostic build only; needs D1)

- **Where:** `mixer.c` (stage marks around the existing calls in
  `mixer_calcNextSampleBlock()`), `AudioCodecManager.c` (`pack_audio_half()`),
  `sequencerTimer.c` (`TIM3_IRQHandler()`), and a Menu diagnostic widget
  beside the existing `cpu` widget.
- **What:** `DWT->CYCCNT` deltas, which are already enabled for
  `audioCodec_getQueueFreePercent()`. Record **last** and **peak since
  reset** for these stages:
  1. `modNode_resetTargets()` + `modNode_reassignVeloMod()`
  2. `instrumentManager_dispatchRuntimeLfos()`
  3. `recalcSlotFilter` + `calcSlotAsync` (6 slots)
  4. `effects_service()`
  5–10. `instrumentManager_calcSlotSyncBlock()` per slot
  11. mixer per-slot work (decimate + send + dry), summed over 6 slots
  12. FX convert + `effects_process()` + return
  13. whole `mixer_calcNextSampleBlock()`
  14. `pack_audio_half()` (DMA ISR)
  15. `TIM3_IRQHandler()` (sequencer ISR)

  15 stages × last/peak × 4 B = **120 B**.
- **Display:** peak cycles per 32-frame block and percent of 156,707. An
  encoder press resets the peaks.
- **Contract:** the whole thing compiles out when the flag is 0, and the
  release `bss` must be byte-identical to the baseline.

### 0b. Worst-case stress Scene (card fixture)

- One Bank-local Scene (suggested name `CPU TORTURE`) with two Advanced voices
  (Cymbal, HiHat) and four Basic voices (Drum ×3, Snare), every Instrument
  type at least once.
- All filters in ZDF modes, covering LP/HP/BP/notch/peak, with reso ≥ 0.85
  and drive high. Distortion drive high on all six. Two oscillators with
  waveform interpolation active.
- All 12 LFO pairs targeting instrument descriptor parameters. Include
  instance-offset targets (e.g. `osc1_wave`, `mod_amount`), which currently
  walk the entire string chain, and one LFO on a voice Morph (`1vm`).
- StereoFilter Effect in stereo, every send at 127, all fader modes present.
- A Pattern that retriggers every voice on every 16th with long decays, so
  all six always overlap.
- This is the acceptance fixture for every step. The ordinary kits are the
  listening fixture.

### 0c. Host golden harness (`tools/dsp_golden/`)

- **Build:** host C program built with `cc` (no target RAM). It compiles the
  changed DSP functions and a frozen copy of the pre-change function side by
  side. Stubs cover `INITCM*`, `__SSAT`, and `__QADD16`.
- **Inputs:** a fixed grid of signals (saw, full-scale square, white noise,
  impulses, sine at 40/220/1,760/7,000 Hz) × parameter sweeps. For the
  filter: 7 types × f × reso × drive.
- **Outputs:** samples differing, max |Δ| in LSB, signal-to-difference ratio,
  and the list of configurations that diverge by more than 16 LSB.
- **Rounding baseline:** each report also runs the frozen code against
  itself built with `-ffp-contract=off`. That shows how much divergence
  rounding alone causes, so S1 changes can be judged against it.
- **Acceptance by class:**
  - **S0:** zero differing samples.
  - **S1:** divergent configurations ⊆ the rounding-baseline set (±10 %),
    SDR ≥ 80 dB, and ≤ 1 LSB everywhere outside self-oscillating
    configurations.
  - **S2:** ≤ 1 LSB at the point where truncation moved. The post-distortion
    difference is reported but not gated, because distortion scales the
    existing quantisation noise too.

**Gate 0:** a baseline profile of the stress Scene is recorded in the audit.
The harness reproduces the Session 073 filter numbers (below). Release
`bss`/`data` are unchanged.

---

## 4. Step 1 — ZDF filter: batch the divisions (S1, ≈6–7 % CPU)

**Files:** `Core/DSPAudio/ResonantFilter.c` — `SVF_calcBlockZDF()` (line 154)
and `SVF_calcBlockZDFFloat()` (line 332).

**Why:** see F1. Five of the six divisions per sample are reciprocals whose
denominators are known at the same time. They can be inverted together with
one division and a few multiplies. That is exact algebra; only rounding
changes.

**Change (non-naive types, `ENABLE_NONLINEAR_INTEGRATORS == 1`,
non-shaper):**

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
   `E = D0 + 2fR·N0` (so `g0 = D0/E`),
   `P = f²·N0·t1` (so `f1 = P/E`),
   `F = P + E`,
   `Q = P·x + s2·E + f·D0·t1·s1` (so `y1 = Q/F`).
   Then `invB = 1/(D0·E·F)`, and
   `t0 = N0·E·F·invB`, `g0 = D0²·F·invB`, `y1 = Q·D0·E·invB`.
5. The rest of the loop is unchanged:
   `xx = t0(x−y1)`, `y0 = (s1·t1 + f·xx)·g0`,
   `s1' = s1·t1 + 2f(xx − 2R·t0·y0)`, `s2' = s2 + 2f·t1·y0`.
   Output switch unchanged; LP keeps its `fastTanh()`.
6. Keep `s1/s2/zi` in locals across the loop and write them back once, as
   GCC already does.
7. Mirror the identical change in `SVF_calcBlockZDFFloat()`.
8. Leave `FILTER_NAIVE_2_POLE` alone: one division per sample, and `q` is
   already hoisted.

**Evidence already gathered (Session 073 host run):**

- 28,800 configurations, 276 M samples: 99.77 % identical, SDR 81 dB.
- Larger deviations only in 56 self-oscillating high-reso configurations.
  The rounding-only baseline diverges in 54 of the same ones.
- ARM `-Ofast` build: 2 divisions per sample (LP 3) versus 6 today; about
  +340 B per variant.

**Gate 1:**

- Harness passes S1 acceptance for all 7 types, for both the int16 and
  float variants.
- The profiler shows the stage 5–10 slot costs and stage 12 dropping.
- The stress Scene runs 10 minutes with `audioCodec_underrunCount` unchanged
  from baseline or lower.
- Listening A/B on three ordinary kits plus a high-reso self-oscillating
  patch (chaotic cases differ in phase only, as they do between compiler
  versions).
- Flash delta recorded.

---

## 5. Step 2 — Descriptor special writers without strings (S0, ≈1.5–4.5 % CPU + foreground)

**Files:**

- `Core/DSP/Instruments/InstrumentManager.h` — `instrument_runtime_binding_t`
  and a new writer enum.
- `InstrumentManager.c` — `instrumentManager_writeSpecialRuntime()` (line
  2917) and `instrumentManager_osc()` (line 1820).
- The four `*Parameters.c` tables.

**Why:** see F2. The key → writer mapping is fixed per descriptor row, but it
is recomputed with up to 20 `strcmp`, 2 `strstr` and 7 `strncmp` on every
LFO block, Morph worker pass, velocity write and Scene activation.

**Change:**

1. Add `uint8_t special;` after `parameter_type`. It occupies the existing
   padding byte. Add `_Static_assert`s that
   `sizeof(instrument_runtime_binding_t)` and `sizeof(ParamDescriptor)` are
   unchanged, so flash tables do not grow.
2. Define the writer enum: `IM_SPECIAL_NONE = 0`, `NOISE_FREQ`,
   `PITCH_COARSE`, `PITCH_FINE`, `FILTER_FREQ`, `FILTER_RESO`,
   `FILTER_DRIVE`, `FILTER_TYPE`, `HAT_DECAY_CLOSED`, `HAT_DECAY_CHOKE`,
   `AMP_ATTACK`, `AMP_DECAY`, `AMP_SLOPE`, `PITCH_EG_DECAY`, `PITCH_EG_SLOPE`,
   `PITCH_EG_AMOUNT`, `TRANSIENT_WAVE`, `TRANSIENT_FREQ`, `INSTRUMENT_DRIVE`,
   `LFO_RATE`. Use bits 5–6 as the oscillator selector (osc1 / osc2 / osc3 /
   noise), replacing the `osc1_`/`osc2_`/`osc3_`/`noise_` prefix match.
3. Add a `ROW_SPECIAL(...)` macro (or an extra argument to `BIND`) and tag
   exactly the rows whose keys match today's chain. Every other row keeps
   `special = 0`.
4. Rewrite `writeSpecialRuntime()` as a single `switch (descriptor->runtime.special)`.
   Each case calls exactly the function today's matching branch calls, with
   the same arguments. Keep the HiHat `amp_envelope_decay` closed-cache
   branch keyed by writer ID plus slot type, exactly as today.
5. **Self-check:** under `DEV_MODE_DIAGNOSTIC`, keep the old key matcher as a
   pure classifier. At boot, compare its result with every row's `special`
   across all four types. Show a mismatch on the existing diagnostic screen.
   No RAM needed.

**Gate 2:**

- The self-check passes on hardware.
- A trace or diagnostic comparison shows the same writer calls and values for
  the stress Scene's LFO, Morph and velocity targets.
- Hardware regression: Instrument Load, Kit Load, Scene switch, LFO rebind,
  per-voice Morph, the HiHat closed/choke decay pair, and the slot-6
  track-7 alternate decay.
- Profiler stage 2 drops.
- Flash delta recorded (expected ≤ 0).

---

## 6. Step 3 — DMA pack path (S0, ≈1 % CPU, shorter ISR)

**Files:**

- `Core/Hardware/AudioCodecManager.c` — `pack_half()` (line 382),
  `pack_audio_half()`, and the buffer declarations (lines 215–216).
- `Core/Hardware/clocks.c` — MPU region 1 (lines 187–193).

**3a. Word stores.**

- Declare `dma_buffer`/`dma_buffer2` `aligned(4)`.
- Write each channel frame as one 32-bit store
  `ror16(((uint32_t)(s24 & 0x00FFFFFF)) << 8)`. In little-endian memory this
  puts the MSW at the lower halfword, which is the same memory image as
  today's two halfword stores.
- DMA stays in halfword mode.
- This halves the non-bufferable store count.

**3b. MPU attribute.**

- Change region 1 from Strongly-Ordered (TEX=0 C=0 B=0) to Normal
  non-cacheable (TEX=1 C=0 B=0, S=1, XN=1). Stores then go through the write
  buffer.
- Coherency with DMA is unchanged because nothing is cached.
- End `pack_audio_half()` with `DSB` so the half is complete before the ISR
  returns.
- The ADC scan buffer shares the region. Its reads stay uncached, so they
  remain correct.

**Gate 3:**

- A DAC loopback or scope capture of a test tone shows bit-identical output,
  or the `I2S_TEST_TONE` path is unchanged.
- Sliders and endless pots read correctly (ADC DMA in the same region).
- Profiler ISR peak drops.
- Land 3a and 3b as separate commits, each measured.

---

## 7. Step 4 — Fused voice post-chain, distortion bypass at shape 0 (S2, ≈1.5–3 % CPU)

**Files:**

- `DrumVoice.c` (lines 340–350)
- `Snare.c` (line 262), `CymbalVoice.c` (line 272), `HiHat.c` (line 291)
- `distortion.c` / `distortion.h`: add an inline per-sample helper and keep
  `calcDistBlock()` for other callers.

**Change:**

- **Drum:** replace the three passes (interpolated amp EG, velocity gain,
  `calcDistBlock`) with one loop.
  - Per-sample gain = `lerp(lastGain, ampFilterInput) × (volumeMod ? velo : 1)`.
  - Then the distortion curve in float, one conversion, one saturating store.
- **Snare / Cymbal / HiHat:** move distortion into their existing fused
  mix × EG × velocity loop. Keep the `bufferTool_satAdd16()` saturation
  where it is today.
- **Bypass:** when `dist->shape == 0.0f` (the identity curve), take a
  loop-invariant branch that skips the division.
- **Sound:** one or two intermediate int16 truncations disappear (S2). With
  shape 0, today's `x/32767·32767` round trip can lose one LSB; the bypass
  does not.

**Gate 4:**

- Harness S2 acceptance per engine against frozen copies of today's engine
  functions: ≤ 1 LSB at the moved truncation; post-distortion statistics
  reported.
- Listening A/B, drive 0 and drive max, on all four engines.
- Profiler slot stages drop.

---

## 8. Step 5 — Dry path and FX send in one mixer pass (S0 expected, ≈1 % when FX active)

**Files:** `Core/DSPAudio/mixer.c` — `mixer_addVoiceInt16ToOutput()`
(line 409), `mixer_addVoiceToFxBus()` (line 543), and the slot loop (lines
810–835).

**Change:**

- When the Effect is active and the send ramp is non-zero, use a combined
  per-destination loop. It reads `data[i]` once and produces both today's
  dry expression and today's send expression, keeping each expression's
  operand order.
- The dry-only path is unchanged when the send is off.

**Condition:** do this only if Step 0 shows the mixer stages are worth it.
The combined function multiplies the six routing cases by stereo/mono send,
so it costs about +1 KB of flash.

**Gate 5:** harness S0 on a mixer harness (any difference is treated as S1
and justified); profiler stage 11 drops.

---

## 9. Step 6 — Octave selection without `log2f()` (S0 except at exact boundaries, ≈0.5 %)

**Files:** `Core/DSPAudio/Oscillator.c` — `freqToTableIndex()` (line 63).

**Change:**

- Replace `(int)((69 + 12·log2f(f/440))/12)` with a count of how many of 10
  const thresholds `f_k = 440·2^(k − 5.75)`, for k = 1..10 (16.35 Hz …
  8,372 Hz, C-octave edges), are ≤ f.
- The table is 40 B of flash.
- Remove `log2f` from the image if it has no other user (a small flash
  gain).

**Gate 6:** host check that indices are equal for every f on a dense
0.01–22,050 Hz grid, except within 1 ulp of a threshold; pitch-sweep
listening check.

---

## 10. Optional steps (sound-changing or average-only)

### Step 7 — Silence gating for idle voices (S3, average only; decision D3)

**Where:** `instrumentManager_calcSlotSyncBlock()` plus a small
`*_isSilent()` / `*_advanceIdle()` inline per engine.

**Rule:**

- Skip the sync render and write zeros when this block's amplitude gain and
  the previous block's are exactly 0.0f.
- For Drum, both `lastGain` and `ampFilterInput`.
- For the others, `egValueOscVol`.
- Test the returned gain, not the envelope state: `slopeEg2_calc()` can
  return a small negative value for one block at the end of decay.

**While idle:**

- Advance every oscillator phase by `size × phaseInc`, so free-running phase
  stays exact.
- Envelopes, LFOs and async work keep running.

**Output:** identical during silence. What changes:

- Snare/Cymbal/HiHat filter state at retrigger. Their filters run through
  silence today; Drum resets its filter on trigger.
- Noise values.

**Warning:** this lowers only the average. It must never be used to judge
headroom. The stress Scene is designed to defeat it.

### Step 8 — Software PRNG for audio-rate noise (S3; decision D4)

- Replace the `GetRngValue()` read in `calcNoiseBlock()` with an xorshift32
  held in 4 B of DTCM.
- It removes an AHB peripheral read per wrap, and the stale values the
  un-gated RNG returns when read faster than its roughly 0.83 µs refresh.
- The noise becomes statistically whiter. Listen before accepting.
- LFO noise can keep the hardware RNG.

---

## 11. Not recommended (see the audit's "Checked and rejected" table)

- Filter or distortion in ITCM (measured worse).
- Write-back SRAM cache.
- Runtime slots in DTCM (reserved FX arena).
- Morph skip cache (Failed Approach, Session 16).
- A larger control block.
- Decimator short-circuit (declined).
- Nonlinear integrators off, or a half-rate LFO. These are S4 and belong
  only to D5.

---

## 12. Order, gates, and closeout

1. **Step 0** (0a needs D1). Record the baseline profile in the audit.
2. **Step 1**, then **Step 2**. These are the largest and they are
   independent. Land and measure each on its own.
3. **Step 3a**, then **3b**.
4. **Step 4**.
5. **Step 6**.
6. **Step 5**, if Step 0 shows it is worth it.
7. Re-profile. Then decide Steps 7 and 8.

**Every step:**

- `make` clean build.
- `tools/link_budget.py`: flash delta recorded; ITCM/DTCM unchanged.
- `arm-none-eabi-size`: `bss`/`data` unchanged, except approved items.
- Harness class acceptance.
- Stress Scene: 10 minutes with no new underruns.
- Profiler before/after, recorded in the audit's priority list (items
  16–25).
- Listening A/B on the reference kits.

**Closeout:**

- Update `CPU_USE_DSP_AUDIT.md` statuses and measured numbers.
- Update `SRAM_MANIFEST.md` if D1/D4 allocations land.
- Add a short volatile note to `MEMORY.md`.
- Write the `073_SESSION_HANDOFF_LOG.md`.
- This plan is then superseded by that log and the audit.
