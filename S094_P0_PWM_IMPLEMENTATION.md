# DRAFT — PWM as an Oscillator Type

- **Status:** draft proposal for review (after S093 P8). **No firmware code is
  changed by this document.** Figures marked *measured* come from standalone
  scratch spikes compiled with the Makefile's DSP flags and counted with
  `elf_looppath.py --cond-heads --in-range` (§10). Each spike was calibrated
  against a loop whose linked count is on record, and matched it. Linked
  counts can still move by 1–3 under LTO, so every figure is re-measured on a
  linked image before a schedule is written. Figures marked *estimate* are not
  measured.
- **Scope (user, 2026-11-10):**
  - PWM as an oscillator type, with `pos` as the pulse width, on the four
    drum instruments (Drum, Snare, Cymbal, HiHat) and on Alkaline;
  - ideally working with the stock waves;
  - within the current oscillator budget, without large new RAM or flash.
- **Binding rules that apply:**
  - S085 / `INSTRUMENTS_DSP_REFERENCE.md` §12: no voice's dearest setting
    may rise. New loops are separate copies, in a new `.itcm.sNNN`
    subsection, `noinline, noclone`, and add no constant-argument calls into
    `calcNextOscSampleBlock()`. Nothing stays live across an inlined render.
  - S089: no per-sample cost may increase without a measured saving.
  - S093: comparisons include the per-block work.
  - Constant CPU: budget the dearest choice.
  - RAM and ITCM growth need sign-off: ITCM counts as RAM (S073).
  - `MEMORY.md` document rule: no step here edits `MEMORY.md` or
    `knowledge_files/`. §9 lists what would go out of date.

---

## 0. Summary

**Yes, on all five instruments, with no new RAM.**

- **Drum, Snare, Cymbal, HiHat:** every PWM loop measures at or below the
  stock table read it sits beside. Every one is far below the dearest choice
  for its role.
- **Alkaline:** PWM fits under the Basic ceiling in one of three forms. The
  form with the best sound is about 2 per sample over and needs a small
  saving (§2.4).

| | Result | Notes |
|---|---|---|
| **Sound** | `typ` gains a fifth value, `pwm`. Under it, `wav` picks Sin/Tri/Saw/Rec and `pos` sets the width | Rec is a band-limited pulse; the other three each get a width effect of their own (§1) |
| **CPU, drum voices** | plain 23–28, FM 31, FM ramp 31, ring 31, sync 35 (*measured*) | Role ceilings: plain 30 (crash), FM 50/51, ring 42, sync 41. **No voice's worst case rises** |
| **CPU, Alkaline** | fused PWM ladder loop 212 / 219 / 204 by read form, against today's 213 (*measured*) | The 212 form fits with ≈ 5 to spare; the 219 form is ≈ 2 over (§2.4) |
| **Interpolation pool** | unchanged | PWM never claims a slot; the `itp` old carrier plays the PWM plain copy (≤ 28 against the 30 that sets the pool) |
| **RAM** | **0 B** | The width is the existing `position` / `positionApplied` bytes; every other constant is derived per block inside the dispatcher |
| **ITCM** | ≈ 2.0 KB for all five uses (≈ 1.1 KB for plain + FM + FM ramp only) (*estimate from measured loop sizes*) | 12,128 → ≈ 14,100 / 16,384 B. ITCM counts as RAM: needs sign-off |
| **Flash** | ≈ 2.5 KB (*estimate*): an Alkaline ladder copy ≈ 1.35 KB, two small tables 640 B, dispatcher and helper cases | Headroom 42,828 B |
| **Stored data** | `typ` 4 (`pwm`) is appended; Alkaline `typ` 2 is appended | No stored value changes meaning. `OSC_TYPE_MODLIST` moves 4 → 5; it is never stored (§6) |

**The key finding.** The new loops interpolate in **integer** arithmetic and
apply the gain in Q15/Q14. The shipped loops interpolate in float and convert
twice. On the PWM loops, that saving pays for the second read or the phase
warp:

| Plain loop | Float interpolation, float gain (shipped style) | Integer interpolation, Q14/Q15 gain |
|---|---:|---:|
| Stock table read (today) | 24 linked; 23 with phase and row hoisted | — |
| PWM, two-read | 30 | **23** |
| PWM, phase warp | 31 | **23** |

---

## 1. What `pwm` does to each stock wave

### 1.1 Two mechanisms

The stock tables (checked in `wavetable.c`):
- `triTable`: starts at its trough, peaks at ½;
- `recTable`: high for the first half;
- `sawTable`: descends, with its jump at the wrap.

The 4,096-point sine has its positive lobe first. Every table has mean 0.

**W: the phase warp.** The cycle is split at the width point `W = p · 2^25`
(p = the applied width, 1..127):
- the first half-cycle of the table is played over `[0, W)`;
- the second half-cycle over `[W, 2^32)`;
- `p` 64 is the unwarped wave.

```
pw = (ph < W) ? umulh(ph,      RA) << 6              /* RA = 2^32 / p          */
              : umulh(ph - W,  RB) << 6 + 2^31       /* RB = 2^32 / (128 - p)  */
```

- `RA` and `RB` come from one 128-entry `uint32_t` reciprocal table (512 B
  flash), so there is no division per block.
- The `<< 6` is free: the loop only needs `pw >> 22` (the index) and
  `(pw >> 6) & 0xFFFF` (the fraction).
- The spike's loop (`m1b`) used variable shifts in place of the fixed
  `<< 6` and measured 23, so this form is at most that.

**D: the two-read form.** Two reads of the **saw** rows, offset by `d = p·8`
points, combined:
```
y = s(φ) ± s(φ − w),    level  g = 64 / max(p, 128 − p)   (0.5 .. 1)
```
- `w` is a whole number of table points. Both reads therefore share one
  fraction, and the loop interpolates the combined value once.
- Both results are band-limited, because each read is a band-limited saw.
- The level law is the same for the difference and the sum (derived from
  the saw's ramp). The level is a 64-entry Q14 table (128 B) and is
  constant within a block.
- Reading B at `φ − w` (not `+ w`) makes the pulse high for the first `w`
  of the cycle, as the warp is. So `p` 64 gives exactly the `Rec`
  orientation.

### 1.2 Per wave (recommended: the hybrid)

| `wav` | Under `pwm` | `pos` 1 → 64 → 127 | Mechanism | Loop (*measured*, plain) |
|---|---|---|---|---:|
| `Sin` | asymmetric sine (phase-distortion-like) | narrow peak → **sine** → narrow trough | W on the sine table, DC removed per block | 22 (sine copy) / 28 (one generic warp loop) |
| `Tri` | variable-symmetry triangle | falling ramp → **triangle** → rising ramp | W on the tri rows, octave lift (below) | 23 / 28 (generic) |
| `Saw` | two-saw ("saw PWM") | ≈ saw → **saw an octave up** → ≈ saw | D, sum | 23 |
| `Rec` | **pulse** | narrow high → **square** → narrow low | D, difference | 23 |
| `Noi`, `Cym` | not offered under `pwm` | — | the `wav` value clamps to `Rec` | — |

**Why the hybrid:**
- **The warp is the only way to give the sine a width.** The two-read form
  turns a sine into a phase-shifted sine of the same shape.
- **The warp is wrong for the pulse.** Compressing a band-limited table
  compresses its spectrum by `0.5/min(w, 1−w)`, which aliases. The cure is
  to lift the octave row by `ceil(log2(64/min(p, 128−p)))`:
  - at `p` 16 (12.5 %) that is two octaves duller;
  - a 100 Hz pulse would stop near 3–4 kHz (*estimate*).

  The two-read pulse is band-limited at every width for the same cost.
- **Tri under the warp** needs the same lift, but the triangle's harmonics
  fall as 1/n², so the dulling is mild.
- **Saw under the warp** would only bend the ramp (a subtle effect). The
  two-saw sum is a strong, well-known sound at the same cost (P-D2).

**DC:**
- The warped sine has mean `(2/π)(2w − 1)`. It is removed per block: one
  subtract in the loop, or folded into the gain step.
- Without it, a narrow width under an amp envelope would thump.
- The pulse, the saw sum and the warped triangle have mean 0.

**Overshoot:** the band-limited edges ring (Gibbs). The D loops store through
`SSAT16`; the warp stays within the table's own peak.

### 1.3 Width changes: smooth, no RAM

- `pos` arrives once per block, from the knob, LFO, Morph or automation.
- Under `pwm`, `osc_typedPositionApply()` moves `positionApplied` toward
  `position` by at most a few steps per block. This is a slew; the step
  limit is P-D7.
- The dispatcher derives that block's `W` / `d`, `RA` / `RB`, the level and
  the lift from `positionApplied`.
- Nothing is stored and nothing stays live across the render (S086 F1).
- A width change is a waveform step at the block rate, as a wavetable frame
  step is. It is not an amplitude step: the warp keeps the amplitude, and
  the D level changes by at most 1.6 % per width step near 64. The ≤ 2-step
  slew keeps both inaudible (*to confirm by ear*).

---

## 2. Cost

### 2.1 Calibration (standalone counts against linked records)

| Loop | Standalone (this spike) | On record |
|---|---:|---:|
| `calcWavetableOscBlock` form | 24 | 24 linked (`calcNextOscSampleBlock` loop, P8 image) |
| Table sync copy | 34 | 34 / 39 (S085, no reset / reset path) |
| `DiodeLadder_renderB2Osc` (scratch copy of `DiodeLadder.c`) | 213 | 213 standalone / 214 linked |
| `DiodeLadder_renderB2Osc2` (same copy) | 228 | 228 linked |

### 2.2 Drum, Snare, Cymbal, HiHat: every use against its role ceiling

The ceilings are the dearest choice for each role today (P8 image,
`DRAFT_SAMPLE_VOICE_REDUCTIONS.md` §1, `INSTRUMENTS_DSP_REFERENCE.md` §8.2).

| Use | Who | PWM loop (*measured*) | Role ceiling today | Margin |
|---|---|---:|---:|---:|
| Plain (D) | Snare tonal; Drum `mix`; the `itp` old carrier | 23 | 30 (crash) | 7 |
| Plain (W, generic: one loop serves sine and tables) | as above | 28 | 30 | 2 |
| Plain (W, separate sine / table copies) | as above | 22 / 23 | 30 | 7–8 |
| FM (W, table) | Cymbal / HiHat carrier | 31 | 50 (`slp` FM) | 19 |
| FM (D) | Cymbal / HiHat carrier | 30 | 50 | 20 |
| FM ramp (D; the S089 P3 twin form, +1 `vadd`) | Drum `fm` | 31 | 51 (`slp` FM ramp) | 20 |
| Ring (D) | Drum `rng` | 31 | 42 (`slp` ring) | 11 |
| Sync (D) | Drum `snc` slave | 35 | 41 (`slp` sync) | 6 |
| FM ramp / ring / sync (W) | Drum | not measured; ≈ FM 31 + 0–5 (*estimate*) | 51 / 42 / 41 | ≥ 6 (*estimate*) |

- **Whole voices.** The dearest Drum setting stays `fm` with a `slp`
  carrier (261.3). The dearest Cymbal/HiHat setting stays the `slp` FM
  carrier (≈ 292.9). Snare's tonal oscillator stays bounded by crash (30).
  No worst case moves.
- **If crash is removed** (`DRAFT_SAMPLE_VOICE_REDUCTIONS.md` C-D1), the
  plain ceiling drops to 27 (`slp`). The **generic** warp loop (28) would
  then be the new dearest plain read, by 1. Choose the separate sine/table
  copies (22/23) in that case (P-D4).
- **Per block (*estimate*):**
  - the dispatcher's width constants: two table reads, a level read, a lift
    from `clz`, the DC term; about 15–25 instructions, ≤ 0.8 per sample;
  - `osc_userBind()`'s PWM case: the stock frequency law, as `osc_setFreq()`
    does for a stock wave; about the same as the typed `slp` bind it
    replaces.

  The typed path already pays a bind each block, so the per-block total is
  about that of a typed `slp` / `wtb` oscillator today.

### 2.3 The interpolation pool

- PWM has no frames, so it never claims a slot.
- An `itp` retrigger on a PWM carrier snapshots `oscType`,
  `sampleCacheLooped` and `positionApplied` (all already in
  `OscCarrierSnapshot`). The old carrier plays through the PWM plain copy
  at its frozen width.
- 23–28 is below the 30 that sets the pool's per-slot 45, so the pool's
  ceiling (90) is unchanged.

### 2.4 Alkaline: PWM inside the fused ladder loop

The Alkaline's oscillator is read inside `dl_renderB2Body()` (fused modes
1 and 2). PWM would be a third fused mode in a new out-of-line wrapper,
`DiodeLadder_renderB2OscPwm()`. The existing wrappers keep their code, and
law AL-X is untouched.

| Read form inside the ladder loop | Loop (*measured*) | vs today (213) | Whole voice (*estimate*: 256.5 − 213 + loop + ≈ 0.3 per block) | Against 261.3 |
|---|---:|---:|---:|---|
| Both reads interpolated (integer, shared fraction) | 219 | +6 | ≈ 262.8 | **over by ≈ 1.5** (≈ 2.5 if linked is +1) |
| **A interpolated, B truncated at the same point** | **212** | **−1** | **≈ 255.8** | **fits, ≈ 5 to spare** |
| Both truncated | 204 | −9 | ≈ 247.8 | fits |
| Soft comparator on the Saw read (`clamp(g·saw − t)`), for reference | 218 | +5 | ≈ 261.8 | over |

- **Sound of the three read forms:**
  - On the ramps both reads share one fraction, so dropping the
    interpolation cancels exactly in the difference.
  - What is lost is the time placement of each band-limited edge: an edge
    read without interpolation lands on a table point, 1/1,024 of a cycle.
  - "A interpolated, B truncated" keeps one edge exact and quantises the
    other. "Both truncated" quantises both.
  - Both errors sit under the 303 ladder's low-pass. They are measured by a
    host law before a choice is made (P-D6).
- **Stack references** in the loop: 14 for today's fused loop and for the
  219 form; 12 for the 204 form. No new spills.
- **No slot:** `pwm` and `wtb` are different `typ` values, so a PWM
  Alkaline never holds an interpolation slot. Only the no-slot figure
  applies, against 261.3.
- **The 219 form** is possible if paid for: `DRAFT_SAMPLE_VOICE_REDUCTIONS.md`
  §4.4 A3 (drive and make-up from a table, ≈ −1) plus A2 (knob-only expo
  terms from tables, ≈ −1 to −1.5). Both are *estimates*.

---

## 3. Memory

| | Bytes | Where | Notes |
|---|---:|---|---|
| RAM | **0** | — | `position` / `positionApplied` (`OscInfo` bytes 18–19) carry the width; the bind kind uses `sampleCacheLooped` |
| Width reciprocal table `RA/RB` | 512 | flash | `uint32_t[128]`; or two integer divides per block (UDIV, not VDIV) |
| Level table | 128 | flash | Q14, 64 entries |
| ITCM, D loops: plain, FM, FM ramp, ring, sync | ≈ 880 (*sizes of the measured loops*: 148, ≈ 190, 190, 190, 162) | ITCM | new `.itcm.sNNN` |
| ITCM, W loops (generic): plain, FM, FM ramp, ring, sync | ≈ 940 (plain 160 measured, the rest ≈ 190 each) | ITCM | separate sine/table copies: about twice this |
| ITCM, dispatcher cases | ≈ 150–250 (*estimate*) | ITCM | one `case OSC_BIND_PWM` in each of six dispatchers |
| Alkaline wrapper `DiodeLadder_renderB2OscPwm()` | ≈ 1,350 (the existing `DiodeLadder_renderB2Osc` is 1,340) | flash | the Alkaline's ladder loops are flash-resident today |
| Code in flash helpers (`osc_userBind`, `osc_setType`, `osc_typedPositionApply`, `osc_typedTriggerPhase`, menu) | ≈ 300–500 (*estimate*) | flash | |

**ITCM by scope:**

| Scope | ITCM added | ITCM total |
|---|---:|---:|
| All five uses, generic warp | ≈ 2.0 KB | ≈ 14.1 / 16.4 KB |
| All five uses, separate sine/table warps | ≈ 2.9 KB | ≈ 15.0 / 16.4 KB |
| Plain + FM + FM ramp only (`snc` / `rng` fall back, P-D5) | ≈ 1.1 KB | ≈ 13.2 / 16.4 KB |

**Interactions with the reductions draft:**
- Crash removal frees ≈ 540 B ITCM (`DRAFT_SAMPLE_VOICE_REDUCTIONS.md`
  §2.2).
- Its option S-C (dropping the never-dearest carrier blends) frees
  ≈ 2.2 KB.
- Colloid (`DRAFT_COLLOID.md`) needs ≈ 0.9 KB of ITCM for its oscillator
  loops.

---

## 4. Integration: Drum, Snare, Cymbal, HiHat

**The typed path is already built for this.** The four engines branch on
`osc_isTyped()` and call the typed dispatchers. The dispatchers switch on
the bind kind (`sampleCacheLooped`). PWM becomes a fourth bind kind on that
path, so the four engines' render code is not edited (only their
`osc_isTyped()` inlines change one constant, below):
- `DrumVoice.c`: l. 688, 719, 751, 812, 843;
- `Snare.c`: l. 389;
- `CymbalVoice.c`: l. 429;
- `HiHat.c`: l. 446.

| File | Change |
|---|---|
| `Core/DSPAudio/Oscillator.h` | `OSC_TYPE_PWM 4u`; `OSC_TYPE_MODLIST` 4 → 5 (l. 99, never stored); `OSC_TYPE_COUNT` 4 → 5; `OSC_TYPE_SELECTABLE` 4 → 5 (l. 94–100); `OSC_BIND_PWM` (value 3 is free: "3 stays unused", l. 202–205); `osc_isTyped()` range SMP..PWM (l. 823) |
| `Core/DSPAudio/Oscillator.c` | `osc_userCount()` (l. 1493): PWM → 4 entries (Sin..Rec). `osc_setType()` (l. 1418): clamp `wav` to `REC`; set the bind; re-anchor `positionApplied`. `osc_userBind()` (l. 1554): PWM case = the stock frequency law (the static `osc_calcSineFreqValue()` / `osc_calcWavetableFreqValue()`), bind `OSC_BIND_PWM`. `osc_typedTriggerPhase()` (l. 1689): `startPhase`, no `pos` offset. `osc_typedPositionApply()` / `Restore()` (l. 1753 / 1832): the width slew, no phase bend. A `case OSC_BIND_PWM` in `osc_renderTyped()` (l. 2389), `osc_renderTypedFm()` (l. 2411), `osc_renderTypedSync()` (l. 2430), `osc_renderTypedRing()` (l. 2452), `oscSection_renderOldCarrier()` (l. 2537) and `osc_renderTypedFmRamp()` (l. 3670). The new loops go in a new `.itcm.sNNN` subsection |
| `STM32F765VIHx_FLASH.ld` | pin the new subsection after `.itcm.s089p3` (l. 180) |
| `Core/Menu/MenuText.h` / `menu.c` | `oscTypeNames` gains `pwm` (count 5; l. 162; assert `menu.c` l. 12541). `menu_formatWaveCell()` (l. 12149) names Sin/Tri/Saw/Rec under `pwm`. `instrumentManager_waveDomainMax()` (`InstrumentManager.c` l. 839) gives 3 under `pwm` |
| Parameter files | none: the `typ`, `wav` and `pos` rows exist on all four instruments |

**What `osc_setFreq()` sees.** It reaches `osc_userBind()` through its
existing `osc_isTyped()` test. The PWM frequency law lives in the flash
helper, so `osc_setFreq()`'s ITCM body is not edited. Only the inlined
constant in `osc_isTyped()` changes (same encoding size). The gates in §7
confirm no loop moved.

**Per mode:**

| Mode / engine | PWM behaviour |
|---|---|
| Drum `fm` | `osc_renderTypedFmRamp()` → PWM FM ramp copy (the modulator is unchanged) |
| Drum `mix` | `osc_renderTyped()` → PWM plain copy, gain `1 − amt` |
| Drum `rng` | `osc_renderTypedRing()` → PWM ring copy (or, under P-D5's narrow scope, the plain copy with no ring) |
| Drum `snc` | `osc_renderTypedSync()` → PWM sync copy; `osc_syncPull()` resets the phase, and the width warp/offset is applied to the pulled phase every sample (or unsynced under P-D5) |
| Snare | tonal oscillator through the plain copy |
| Cymbal / HiHat | carrier through `osc_renderTypedFm()` (HiHat gain 0.5); modulators unchanged |
| Run modes | `rst`: the phase resets to `startPhase`. `fre`: free. `itp`: the old carrier at its frozen width (§2.3) |
| Modulators | unchanged: they have no `typ` or `pos` rows (S087 A22). A modulator PWM entry would be a later list extension |

---

## 5. Integration: Alkaline

| File | Change |
|---|---|
| `AlkalineVoice.h` | `ALK_TYP_PWM 2u`, `ALK_TYP_COUNT` 3 |
| `AlkalineVoice.c` | `Alkaline_setOscType()` (l. 617): 2 → `OSC_TYPE_PWM`, `wav` list Saw/Sqr. `Alkaline_calcVoiceSyncBlock()` (l. 589): a third per-block branch → a new `alk_renderPwm()` (noinline, like `alk_renderWav()`). The trigger: under `pwm`, no note-start phase (`pos` is the width) |
| `DiodeLadder.c/.h` | fused mode 3 in `dl_renderB2Body()` (l. 273) and the noinline wrapper `DiodeLadder_renderB2OscPwm()`; modes 0–2 compile unchanged (constant `fused` per wrapper) |
| `MenuText.h` / `menu.c` | `alkTypeNames` gains `pwm` (count 3; assert `menu.c` l. 12533 → 3u) |

**What Saw and Sqr mean under `pwm`:**
- `Sqr` → the pulse. `Saw` → the two-saw sum. Both come from the `sawTable`
  rows, through the same loop with the sign as a per-block constant. The
  spike measured the sum's sign as free on the drum loop (23); the ladder
  copy is re-measured.
- The 303 square's tilt (`alkSqrTable`) is not reproduced: the PWM pulse is
  flat.
- The pulse scale at `pos` 64 is matched to `ALK_SQR_SCALE_V × Sqr`'s level
  by measurement.

---

## 6. Stored data

- **`typ` 4 = `pwm`, appended.** No stored value changes meaning: `typ` was
  0..3. An older firmware clamps 4 to `wtb` (`osc_setType()` clamps to
  `OSC_TYPE_SELECTABLE − 1`).
- **`OSC_TYPE_MODLIST` 4 → 5.** It is a RAM-only marker set at engine init
  (`DrumVoice.c` l. 132, `CymbalVoice.c` l. 115–116, `HiHat.c` l. 126–127)
  and is never stored.
- **Alkaline `typ` 2 = `pwm`, appended.**
- Under `pwm`, `wav` is 0..3 (Sin..Rec). A stored value above that clamps
  to `Rec`, by the S087 r8 rule.
- No backward compatibility work is needed or offered (standing rule).

---

## 7. Tests and gates

1. **Freeze first:** the P8 linked loop counts (`elf_osc_baseline`) and the
   mixer baseline.
2. **Old code unchanged:** `oscmod_identity`, `elf_osc_check` (0 missing, 0
   changed), `elf86_mixer_check` / `elf_p4_check` (the mixer: 0 grew).
3. **New loops, linked:** `elf_looppath.py` on each PWM copy against its
   role ceiling (§2.2); the Alkaline wrapper against the 213 baseline plus
   the chosen form.
4. **Per block:** `elf_blockpath.py` on the dispatchers' PWM path;
   `control_cost.py --elf` for Alkaline.
5. **Host laws (new module, `tools/dsp_test/modules/`):**
   - at `p` 64: W equals the plain wave (Sin/Tri) to the integer
     interpolation's rounding; D-difference equals a band-limited square;
   - alias floor of the D pulse against an ideal band-limited pulse, per
     octave row;
   - DC of the warped sine below a bound after the per-block correction;
   - the width slew lands on the target with no overshoot;
   - break proofs (wrong sign, wrong offset direction, missing DC term);
   - Alkaline: the 212 / 219 / 204 forms against an oversampled reference
     (the P-D6 evidence).
6. **Host constants:** the test extracts the header's constants (S086 F3).
7. **Device:** the six-slot worst-case Scene (`cpu` widget, underruns),
   then a listening pass (width sweeps by LFO; narrow pulses at low notes).

---

## 8. Decisions

| # | Question | Recommendation |
|---|---|---|
| P-D1 | Mechanism | **Hybrid:** warp for Sin and Tri, two-read for Saw and Rec. Pure warp is simpler but gives a dull narrow pulse; pure two-read gives the sine nothing |
| P-D2 | `Saw` under `pwm` | Two-saw sum (saw → octave-up saw at 64). Alternative: warp (a bent ramp; subtle) |
| P-D3 | Width range | `p` = clamp(`pos`, 1, 127); `pos` 0 behaves as 1 |
| P-D4 | Generic warp loop (28, one copy) or sine and table copies (22/23) | Generic, unless crash is removed (then 28 > 27): separate copies |
| P-D5 | Scope of the first step | All five uses (plain, FM, FM ramp, ring, sync). Narrow alternative: plain + FM + FM ramp, with `snc` unsynced and `rng` plain (the noise precedent, §5.1.1 of the reference), ≈ 0.9 KB less ITCM |
| P-D6 | Alkaline read form | 212 (A interpolated, B truncated), if the host law shows its error under the ladder is acceptable; else 219 with A2 + A3 |
| P-D7 | Width slew per block | ≤ 2 steps per block (64 steps in ≈ 23 ms); to be set by ear |
| P-D8 | Sine DC correction | Yes (removes the thump at narrow widths) |
| P-D9 | Modulator PWM entries (`OSC_TYPE_MODLIST` list) | Not now; a later list extension |

---

## 9. Statements this draft would make out of date (for the user to log, if adopted)

- `INSTRUMENTS_DSP_REFERENCE.md`:
  - §6.1 (the `typ` table and `OSC_TYPE_MODLIST`);
  - §6.1.1 (`pos` meaning per type: add `pwm` = width);
  - §6.1.6 neighbours (new loop family);
  - §8.1 (new rows);
  - §5.5 (Alkaline `typ` list, `pos` under `pwm`).
- `OSC_INTERP_AUDIT.md`: PWM never claims; the `itp` old carrier under
  `pwm`.
- `CPU_USE_DSP_AUDIT.md`: new items for the PWM loops and the integer
  interpolation finding (§0).
- `STORAGE_SRAM_MANIFEST.md`: ITCM growth, the two flash tables.
- `FILESYSTEM_SPEC.md`: the `typ` value list.
- `MEMORY.md`:
  - the Alkaline line "`typ`/`run` are not offered" and the `typ` list;
  - the Alkaline table-wave rule (a PWM read is still a table read from
    `sawTable`, through a new fused mode, not `osc_setFreq()`).

---

## 10. Measurement record (2026-11-10)

- **Where:** scratch spikes outside the tree, not kept, as for the
  reductions draft's `fxspike`.
- **Flags:** `-mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard
  -Ofast -std=gnu11 -ffunction-sections` (the Makefile's DSP flags, no LTO).
- **Counter:** `tools/dsp_test/elf_looppath.py --cond-heads --in-range`
  (longest path).

| Spike | What | Result |
|---|---|---|
| `p0` / `p0h` | shipped float table read, as-is / hoisted | 24 / 23 |
| `m1`, `m1c` | warp, float interpolation, branchy / branch-free | 31 / 31 |
| `m1b` | warp, integer interpolation, Q15 gain | 23 |
| `m1sin` | warp, sine table (`<<20`, guard point) | 22 |
| `m1fm` | warp, FM carrier | 31 |
| `w_plainGen` | warp, one loop for sine and tables (variable shift and mask) | 28 |
| `m2`, `m2b` | two-read, float / integer interpolation with float gain | 30 / 26 |
| `m2c` | two-read, integer interpolation, Q14 gain, `SSAT16` | 23 |
| `d_plainSigned` | two-read with a ±1 sign (sum or difference) | 23 |
| `m2fm` / `d_fmRamp` | two-read FM / FM with per-sample depth ramp | 30 / 31 |
| `d_ring` / `d_sync` | two-read ring (S085 law, ramped depth) / sync (`osc_syncPull()`) | 31 / 35 |
| `m3`, `m3b` | soft comparator, float / integer | 27 / 20 (not recommended: a pulse from every wave, soft edges) |
| scratch `DiodeLadder.c` | modes 1, 2 (calibration); 3 (two-read, both interpolated); 5 (both truncated); 6 (A interpolated, B truncated); 4 (comparator) | 213, 228; 219; 204; 212; 218 |
| sizes (`nm -S`) | PWM loops | plain 130–160 B, FM / ring 190–200 B, sync 162 B |
| P8 image | `link_budget.py` | flash 710,836 / 753,664 (headroom 42,828); ITCM 12,128 / 16,384 |

**Loop sketch (D, plain; the form measured as `m2c`):**

```c
const int16_t *row = sawTable[tableOffset];         /* per block */
for (i = 0; i < size; i++) {
    const uint32_t ia = ph >> 22, na = (ia + 1u) & 0x3FFu;
    const uint32_t ib = (ia - d) & 0x3FFu, nb = (ib + 1u) & 0x3FFu;
    const int32_t d0 = row[ia] - row[ib], d1 = row[na] - row[nb];
    const int32_t o  = d0 + (((d1 - d0) * (int32_t)((ph >> 6) & 0xFFFFu)) >> 16);
    ph += inc;
    buf[i] = (int16_t)__SSAT((o * gQ14) >> 14, 16);
}
```
