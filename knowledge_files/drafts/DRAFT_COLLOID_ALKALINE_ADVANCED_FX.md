# S095 P5 — Advanced Colloid and Alkaline: per-voice FX (investigation)

- **Status:** investigation, written 2026-11-13 from the tree at `4602b3b3`.
  Nothing changed in the source. Cost figures marked *est.* are estimates
  from comparable loops in the tree; every one is measured before a
  schedule is approved.
- **Updated 2026-11-14 (budget):** the Colloid has been measured on the
  device (six Colloids on the S095 P4 image;
  `tools/dsp_test/baselines/s095p5/device_probe_colloid6.txt`, summarised
  in `S095_P4_FX_BUS_TWEAKS.md` §7). The Alkaline figures now come from the
  per-slot readings of the S094 P2 run 2 probe instead of a whole-block
  subtraction. §0, §1, §2, §3.3, §5 and §6 are revised to match. Step 1 of
  §5 is done for the Colloid.
- **Updated 2026-11-14 (Colloid `pwm` 8):** the Colloid now renders 8
  `pwm` oscillators instead of 6 (user; `S095_P4_IMPLEMENTATION.md` §17).
  `pwm` Sin 8 (≈ 18,300 cycles per block, ≈ 18,800 alone, *est.*) replaces
  `wtb` 12 (17,433, measured) as the Colloid's dearest mode, so every
  Colloid room below is smaller. §0, §2.2, §3.1, §3.3, the new §3.4 (the
  phaser on the Colloid) and §6 are revised to match.
- **Updated 2026-11-14 (Colloid warp forms back to 6):** `pwm` Sin / Tri
  render 6, Saw / Rec 8, `wav` / `wtb` 12, and rejoining oscillators land
  (`S095_P4_IMPLEMENTATION.md` §18). The dearest modes are `wtb` 12 and
  `pwm` Saw / Rec 8, both ≈ 17,500 (≈ 18,000 alone). The Colloid rooms in
  §0, §2.2, §3.3, §3.4 and §6 are revised again: Advanced ≈ 3,800 cycles
  per block (≈ 119 per sample), Basic ≈ 1,600.
- **Question:** which effects an Advanced Colloid and an Advanced Alkaline
  could carry. Candidates: phaser, chorus, basic reverb, the waveshapers,
  flanger, a "guitar doubler", pitch FX, and the sample-rate reduction (SRR)
  back.

---

## 0. Summary

- **What "Advanced" buys.** An Advanced slot has 21,803 cycles per block
  against a Basic slot's 19,620, at most two per kit, and up to two FxBuffer
  voice units (2 × 4,416 B: 100 ms mono or 50 ms stereo of int16 delay
  line). Both already exist in the tree; neither has a user yet.
- **Alkaline Advanced** has about **5,200 cycles per block** of room
  (≈ 163 cycles per sample), measured per slot (§2.1). That pays for **SRR
  plus one insert FX** from a shared modulated-delay kernel (chorus,
  flanger, doubler, pitch shift), or a phaser, or a waveshaper. On the
  estimates it may also leave room for a second FX in series (Q6).
- **Colloid Advanced** keeps 12 plain, 8 `pwm` Saw / Rec and 6 `pwm`
  Sin / Tri oscillators (`S095_P4_IMPLEMENTATION.md` §17–§18). Its
  dearest modes, `wtb` 12 and `pwm` Saw / Rec 8, cost ≈ 18,000 cycles per
  block for a lone voice (*est.*; §2.2). That leaves about **3,800 cycles
  per block (≈ 119 per sample)** in an Advanced slot. At the Colloid's own
  1.4 cycles per instruction that is room for **SRR plus one FX**, for
  any of the candidates including the full stereo phaser (SRR + phaser
  ≈ 115, the tightest; §3.3, §3.4). It is not room for a second FX, and
  at 2.0 cycles per instruction only SRR + the mid phaser (form C) fits.
  The FX loop's own cost decides (§5 step 2). The FX loop in ITCM (1,152 B
  left; needs your approval) would protect the 1.4.
- **Reverb per voice: no.** A small reverb costs 70–100 instructions per
  sample *est.*, more than the whole allowance, and the bus already has
  Reverb and Echo.
- **Recommended first set (both variants):** SRR (always in the chain) +
  one FX slot with a type row: `cho` chorus, `fla` flanger, `dbl` doubler,
  `pit` pitch shift (one kernel), `pha` phaser, `shp` waveshaper.

---

## 1. What exists in the tree

| Piece | Where | State |
|---|---|---|
| Advanced type flag | `INSTRUMENT_FLAG_ADVANCED` (`InstrumentManager.h:303`); the kit cap of two (`instrumentManager_typeSelectableForSceneSlot()`, `InstrumentManager.c:473`) | Works; no type sets it since S094 P2 §22 |
| Advanced cycle budget | 2 × 21,803 cycles per block of the 152,004 cap (S094 P2 §21; `CPU_USE_DSP_AUDIT.md` item 88) | Reserved, unused |
| Voice units | `fxbuf_voiceAcquire()` / `fxbuf_voiceUnit()` (`FxBuffer.h:286–312`): 12 units of 2,208 int16 samples (50 ms at 44.1 kHz), at most 2 per slot, taken from the top of the arena | No caller (S095 P1 A3). The Effect share shrinks by what is claimed; CrumpBit and the Space types already handle a shrink (keep the newest, reseat) |
| Stereo voice output | `instrumentManager_slotRight()` (`InstrumentManager.c:2247`): the mixer takes a right block from the voice | Colloid only |
| SRR | `mixer_decimateBlock()` (`mixer.c:174`): a sample-and-hold on the mono block, gated by `instrumentManager_slotUsesSrr()` | Drum, Snare, Cymbal, HiHat. Alkaline left it out (D4, `ALKALINE_MODULE_REFERENCE.md`); Colloid never had it. Mono only |
| Fold shapers | `voiceFold.c/.h`: 8 fold types (`tri trd sin snd asn chb tri-int fd2`) in ITCM, 22 + 2 per sample in the Drum WFd chain | Reusable |
| Voice slot reserve | 1,264 B per tagged slot; Alkaline 564 B, Colloid 1,112 B (`sizeof`, measured 2026-11-14 against the tree) | Alkaline has 700 B left, Colloid 152 B |

---

## 2. The budget per variant

### 2.1 Alkaline

- **Measured per slot** (S094 P2 run 2, the D-cached image;
  `baselines/s094p2/device_probe_run2.txt`). Each figure is the slot's
  async plus sync render, in cycles per block:

  | Where | Cost |
  |---|---|
  | In a mixed Scene (`pwm`, segments A/A2) | 15,678 … 15,687 |
  | Six Alkalines (`wtb`, LFO on `pos`, segment D) | 13,974 … 16,261 |
  | Highest seen (segment E, slot 0: `wtb`, LFO, the first Alkaline in the block) | **16,591** |

  The figure the earlier draft used (≤ 18,230) came from the whole block
  less the shared shares. That method charged the slot with part of the
  mixer's own work.
- **Room in an Advanced slot:** 21,803 − 16,591 ≈ **5,200 cycles per
  block, ≈ 163 per sample**. In a Basic slot: ≈ 3,000 (≈ 95 per sample).
- **The highest figure already includes the cold-cache cost.** Slot 0 in
  segment D cost about 2,200 more than slots 1–5 with the same settings,
  because the first Alkaline in each block loads the ladder code (flash)
  into the I-cache.
- **Cycles per instruction:** the Alkaline runs at about 2.0 (263.7
  instructions per sample against ≈ 518 cycles). It is flash-resident and
  has 3 serial VDIV. A small FX loop reading DTCM should do better, but its
  code still adds to the I-cache. Plan on **1.4 … 2.0 cycles per FX
  instruction**: 163 cycles is then 80 … 115 instructions per sample,
  control work included.
- **Not re-measured since S094 P2.** S094 P4 added the Tri wave at no
  per-sample cost, and the worst path is unchanged. A probe of the
  dearest setting confirms the figure before the schedule (§5 step 1).

### 2.2 Colloid

- **Measured** on the device: six Colloids on the S095 P4 image,
  2026-11-14. Record: `baselines/s095p5/device_probe_colloid6.txt`;
  summary in `S095_P4_FX_BUS_TWEAKS.md` §7. Slot cost in cycles per
  block, cycles per sample in brackets:

  | Mode | Cycles per block |
  |---|---|
  | `wtb` 12 `str` | 17,433 (545) |
  | `wav` Saw 12 `str` | 16,693 … 17,272 |
  | `pwm` Sin 6 `str` | 15,393 |
  | `pwm` Saw / Rec 6 `str` | ≈ 14,630 |
  | `pwm` Rec 6, mono | 14,117 |

  The highest window of any slot was 17,807.
- **`pwm` since 2026-11-14 (`S095_P4_IMPLEMENTATION.md` §17–§18): not yet
  measured.** Saw / Rec render 8 (host `pwm8-par` 387.6 instructions per
  sample, was 324.1 at 6); Sin / Tri stay at 6 (`pwmw6-par` 326.5).
  - Added to the measured `pwm` 6 figures at 1.4 cycles per instruction,
    that gives `pwm` Saw / Rec 8 ≈ 17,470 and `pwm` Sin / Tri 6 ≈ 15,400.
  - §18's rejoin test adds ≈ 1.4 per sample of control work to every
    mode (`plain12-par` 382.1 → 383.5), so `wtb` 12 is ≈ 17,500.
  - **The dearest modes are `wtb` 12 and `pwm` Saw / Rec 8, both
    ≈ 17,500.**
- **Cycles per instruction:** 1.37 … 1.41 against the host's 382.1
  instructions per sample (`plain12-par`). The oscillator loops are in
  ITCM.
- **A lone Colloid in a mixed Scene** pays the cold-cache cost on every
  block. In this run, the first of five identical voices cost about 400
  more than the others. Allow about 500: a lone Colloid in its dearest
  mode is then ≈ 18,000.
- **Room in a Basic slot:** ≈ 1,600 cycles per block (≈ 50 per sample).
- **Room in an Advanced slot:** 21,803 − 18,000 ≈ **3,800 cycles per
  block, ≈ 119 per sample**. At the Colloid's 1.4 cycles per instruction
  that is ≈ 85 instructions per sample; at 2.0 it is ≈ 59.
- **To confirm before the schedule:** a probe of `wtb` 12 and `pwm` Rec 8
  on the §18 image; a mixed-Scene probe (one Colloid with drums and an active Effect); and the
  cost of the FX loop itself (§5 step 2).

### 2.3 Rules that apply

- **Constant CPU** (`CPU_USE_DSP_AUDIT.md` §Binding Mandates): the FX runs
  whatever its mix or type; `mix` 0 still processes. The slot is charged
  for its dearest FX type.
- **Per-block work counts** (S093): every figure here is loops plus
  (control + entry/exit) / 32.
- **No other engine's loop may move:** the loop-length gate over the
  `P4_OSC_SYMS` set at each step, as in S095 P2/P3.
- **ITCM:** 15,232 of 16,384 B used (1,152 B left). New FX loops go to
  flash unless you approve ITCM growth.
- **Flash:** 37,020 B of headroom after S095 P4
  (`baselines/s095p4/after_fixes/link_budget.txt`).
- **RAM:** the voice units are DTCM inside the existing FxBuffer arena
  (claimed, not new). Anything in the voice struct counts against the slot
  reserve; anything outside it needs sign-off.

---

## 3. The candidates

### 3.1 Cost and fit

Instruction counts are per sample (per stereo frame for stereo output),
*est.*, from loops in the tree: the Space tape tap (`delayRun` 179 per
frame for three heads in stereo with drive and filters), CrumpBit `pit`
(106 per frame with the 8-bit ADC and output filter), the voice SRR (10),
the WFd fold (24).

| FX | Engine | Buffer | Alkaline (mono → stereo) | Colloid (stereo → stereo) | Fits Alkaline? | Notes |
|---|---|---|---|---|---|---|
| **SRR** | sample-and-hold, as `mixer_decimateBlock()` | none | 10 | ≈ 16 | **Yes** | The mixer's version is mono; Colloid needs a right-side hold (+2 B) |
| Bit depth | quantise | none | 5 | 8 | Yes | Could share the SRR row pair (rate, bits) |
| **Chorus** | modulated delay, 2 taps (L/R LFO phases), wet LP | 1 unit (Alk), 2 (Col) | ≈ 33 | ≈ 38 | **Yes** | Juno-style (2 taps, triangle LFO) or ensemble (3 taps) |
| **Flanger** | modulated delay, 1–2 taps, feedback ± | 1 / 2 | ≈ 36 | ≈ 40 | **Yes** | Through-zero needs a delayed dry: +1 tap |
| **Doubler** | modulated delay, 1–2 taps at 15–45 ms, slow random walk, tone on the double | 1 / 2 | ≈ 35 | ≈ 40 | **Yes** | One unit holds 50 ms; the double sits opposite the dry |
| **Pitch shift** | two crossfaded taps (the CrumpBit `pit` method, at 16 bits) | 1 / 2 | ≈ 40 (one voice) | ≈ 48 | **Yes**, one voice | Two voices (±detune "micro-shift", or an interval each side) ≈ 70: too dear with SRR |
| **Phaser** | 4–6 first-order allpasses, feedback, coefficient per block | none | ≈ 34 (L = dry + ap, R = dry − ap) | ≈ 66 (two 6-stage chains); ≈ 50 (two 4-stage); ≈ 38 (one chain on the mid, §3.4) | **Yes** | The cheapest stereo modulation on a mono voice. Colloid forms: §3.4 |
| **Waveshaper** | the `voiceFold` types, post-chain | none | 24 | ≈ 44 | **Yes** | Alkaline already has the OTA overdrive post-ladder; a fold *before* the ladder is a different, bigger job (§3.2) |
| Basic reverb | 4-line FDN at half rate | 2 units (≈ 200 ms of line at 22 kHz) | 70–100 | 90–120 | **No** | Above the allowance; the bus has Reverb and Echo |

### 3.2 One kernel for four effects

Chorus, flanger, doubler and pitch shift are the same machine: a ring the
voice writes, and one or two taps read back at a moving, fractional delay.
They differ only in:

| | Delay range | Modulation | Taps per side | Feedback | Window |
|---|---|---|---|---|---|
| Chorus | 2–25 ms | triangle / sine, 0.1–5 Hz | 1 (2 for ensemble) | no | no |
| Flanger | 0.3–10 ms | triangle / sine, 0.05–5 Hz | 1 | ± | no |
| Doubler | 15–45 ms | random walk, slow | 1 | no | no |
| Pitch shift | 10–50 ms window | sawtooth (the delay ramps at the pitch ratio) | 2 | optional | triangular crossfade |

One loop with a delay ramp, two taps per side, two window gains, a
feedback tap and a one-pole on the wet covers all four. It costs its
dearest setting (pitch shift, ≈ 40) whichever type is selected, which is
what the constant-CPU rule charges anyway, and it is one loop to gate and
one block of flash instead of four.

The phaser and the waveshaper are separate small loops. The type row
picks one of three loops; the slot is charged for the dearest (the kernel).

**A fold before the Alkaline's ladder** (the S094 P2 §3.7 "Alkaline variant
with wavefolders" idea) is not an insert FX. The oscillator read is fused
into the ladder loop (`DiodeLadder_renderB2Osc`, `ALK_FUSED_OSC 1`); a fold
between them means a new fused ladder mode per wave (as `ALK_TYP_PWM`
added mode 3, `DiodeLadder_renderB2OscPwm()`), or giving up the fusion and
its saving. It is a separate decision (Q2).

### 3.3 Colloid: what fits (12 / `pwm` 8 Saw/Rec, 6 Sin/Tri)

The Advanced Colloid's room is ≈ 119 cycles per sample (§2.2). The FX row
is charged for its dearest type (constant CPU), so the combination that
counts is SRR plus the dearest FX offered.

| Item, *est.* instructions per sample | Cycles at 1.4 … 2.0 | Fits ≈ 119? |
|---|---|---|
| SRR (≈ 16) + the kernel (≈ 48): 64 | 90 … 128 | **yes** at 1.4 (29 spare); not at 2.0 |
| SRR + phaser, two 6-stage chains (form A): 82 | 115 … 164 | **just** at 1.4 (4 spare) |
| SRR + phaser, two 4-stage chains (form B): 66 | 92 … 132 | yes at 1.4 |
| SRR + phaser, one chain on the mid (form C): 54 | 76 … 108 | **yes**, at 1.4 and at 2.0 |
| SRR + kernel + a second FX (≈ 35): 99 | 139 … 198 | no |

- **What a cut frees:** one plain oscillator pair costs 41.4
  instructions per sample on the host (`colloid_cpu.py` on the S095 P4
  ELF: `plain12-par` 382.1, `plain14-par` 423.5). At the device's 1.4
  cycles per instruction that is ≈ 58 cycles per sample, ≈ 1,850 per block
  per pair. The device agrees: `wav` 12 against `pwm` 6 is ≈ 2,400 on the
  device and 58 instructions (≈ 2,600) on the host. A `pwm` pair costs
  more (62.8 instructions), so that difference is not a per-oscillator
  figure.
- **So 8 plain oscillators** in place of 12 free about 3,700 cycles per
  block (≈ 116 per sample), close to the earlier estimate of 85
  instructions. `pwm` Saw / Rec 8 costs the same as `wtb` 12, so a plain
  cut frees room only if the pulse forms are cut too.
- **A `pwm` cut:** each `pwm` pair costs ≈ 63 instructions, ≈ 2,800 cycles
  per block.
- **The other alternative** is no FX on the Colloid, and an Advanced
  Alkaline only.

### 3.4 The phaser on the Colloid

**What it needs:**
- a chain of first-order allpasses per channel, each
  `y = a (x − y1) + x1` with its two states;
- a feedback tap from the chain's end back to its input;
- the coefficient `a` set once per block from an LFO (rate, depth and
  centre), so the per-sample cost is the chain only;
- no delay buffer and no voice unit.

**Cost per sample,** *est.* from the tree's float loops:
- each stage is about 4 instructions, with the states held in registers;
- feedback and the wet mix are about 4 per chain;
- the int16 input and output (load, convert, add to the dry, saturate,
  store) are about 10 in stereo.

| Form | What you hear | *est.* | With SRR |
|---|---|---|---|
| A: two 6-stage chains (L, R), the LFO a quarter cycle apart | a true stereo phaser: each side sweeps its own notches; deepest sweep | ≈ 66 | 82 ≈ 115 cycles: **just** fits at 1.4 (§3.3) |
| B: two 4-stage chains | the same with two notches instead of three: a lighter, shallower sweep | ≈ 50 | 66 ≈ 92: fits at 1.4 |
| C: one 6-stage chain on the mid (L + R) / 2, wet added + to L and − to R (the Alkaline's form) | the dry stays fully stereo. The phased part is mono, notches on one side where the other has peaks: a wide swirl, less "per-side" motion than A | ≈ 38 | 54 ≈ 76: **fits**, also at 2.0 |

**The phaser without SRR:**
- **Form A alone (≈ 66 → ≈ 92 cycles) fits,** with about 27 cycles of
  margin at 1.4 cycles per instruction. With SRR (≈ 115) it fits with
  about 4.
- **Dropping SRR does not help the slot.** The slot is charged for its
  dearest FX type. SRR + form A is the dearest combination, so leaving
  SRR out of the phaser alone frees nothing unless the phaser is that
  dearest type.

**What keeps form A with SRR safe:**
1. **ITCM for the FX loop.** It brings the FX code to the oscillator
   loops' cycles per instruction (the Colloid's 1.4 is measured with its
   loops in ITCM) and keeps it out of the I-cache.
   - About 300 … 500 B of the 1,152 B left (*est.*). Needs your approval
     (ITCM growth counts as RAM).
2. **Form B or C** if the measured loop runs above 1.4: both fit with SRR
   at that point; C fits even at 2.0.

## 4. Proposed shape

### 4.1 Two new instrument types

The Basic/Advanced flag is per type, so each Advanced variant is its own
registry type sharing the base engine:

| | Alkaline Advanced | Colloid Advanced |
|---|---|---|
| Token / extension | `alx` / `.alx` (Q5) | `clx` / `.clx` (Q5) |
| Flags | `INSTRUMENT_FLAG_ADVANCED` | `INSTRUMENT_FLAG_ADVANCED` |
| Rows | the Alkaline's 30 rows unchanged, then the FX rows appended | the Colloid's rows unchanged, then the FX rows appended |
| Voice struct | `AlkalineVoice` + FX state + `right[32]` (+≈ 150 B, inside 1,264) | `ColloidVoice` + FX state (≤ 152 B left in the reserve) |
| Voice units | 1 (mono ring) | 2 (stereo ring) |
| Output | stereo through `instrumentManager_slotRight()` | stereo (as now) |

The base engines read their rows by the same indices, so the Basic types
stay bit-identical. Converting a Basic file to Advanced is a load into the
other type; no converter (standing rule).

### 4.2 The chain

```
Alkaline Advanced:  osc ─ ladder ─ OTA drive ─ tone ─ SRR ─ FX ─ L/R
Colloid Advanced:   12 osc ─ filter (L/R) ─ drive ─ tone ─ SRR (L/R) ─ FX ─ L/R
```

SRR before the FX, so the chorus or doubler smooths the stepped signal
instead of being stepped by it. The FX sits after the amp envelope, so a
doubler or chorus tail follows each note's end (≤ 50 ms).

### 4.3 The FX rows (aligned by role, as CrumpBit's sub-type rows)

| Row | `cho` | `fla` | `dbl` | `pit` | `pha` | `shp` |
|---|---|---|---|---|---|---|
| `fxt` type | selector | | | | | |
| `mix` | dry/wet | dry/wet | dry/wet | dry/wet | dry/wet | dry/wet |
| `rte` | LFO rate | LFO rate | drift rate | window | LFO rate | — |
| `dep` | depth | depth | spread | interval (st) | depth | drive |
| `fbk` | — | feedback ± | — | feedback | feedback ± | — |
| `tim` | centre delay | centre delay | delay | fine (cents) | stage count / centre | fold type |
| `ton` | wet LP | wet LP | wet tilt | wet LP | — | post LP |
| `wid` | stereo width | width | width | width | width | — |
| `srr` | SRR rate (all types) | | | | | |
| `bit` | bit depth (all types; optional) | | | | | |

Unlike the bus effects (S095 P4 T1), these are **inserts**: the dry signal
goes nowhere else, so `mix` is needed here.

Rows keep the 0..127 / ±63 plain display unless a label is needed to play
them (the type names, the interval in semitones).

### 4.4 Memory: the first voice-unit user

- The Advanced voice claims its unit(s) when the type is assigned to a slot
  (Instrument Load, Scene change), never when `fxt` changes, so a type turn
  never moves memory.
- Two Advanced voices with two units each take 17,664 B from the Effect
  share (126,656 → 108,992 B). The Space Reverb ring (64 KB) still fits;
  the Delay and CrumpBit `dly` maximum times drop with the share (their
  `tim` / `rte` clamps already follow the share).
- This exercises the shrink and growth paths the S095 P1 tests S-1 and
  S-3 could not reach on the device (P1 §13 A3).
- Owners clear what they claim (FxBuffer contract): the voice clears its
  unit on claim, outside the render (≈ 2,200 stores; S095 P1 SD5 measured a
  32 KB clear at about 5 % of a block, so 4.4 KB is about 0.7 %).

---

## 5. Order of work

1. **Measure** (no source change). Sets each variant's allowance.
   - **Colloid: done** (2026-11-14, §2.2), on a six-Colloid Scene.
   - **Still open:** a Scene of Colloids in `wtb` 12 and `pwm` Rec 8, the
     dearest modes since `S095_P4_IMPLEMENTATION.md` §18 (estimated,
     §2.2).
   - **Still open:** a mixed Scene with one Colloid, one Alkaline in its
     dearest setting (`wtb` with an LFO on `pos`), drums and an active
     Effect. It checks the cold-cache allowance of §2.1–§2.2 on the
     current image.
2. **Host:** the kernel, the phaser and SRR as a `tools/dsp_test` module:
   laws (delay ramp, window sum = 1, feedback bound, bit-identical dry at
   `mix` 0 through the same arithmetic), loop counts with `elf_looppath.py`.
3. **Voice units:** the claim and release at type assignment; host tests
   S-1 and S-3 of S095 P1 against the Space types and CrumpBit.
4. **Alkaline Advanced** type, rows, pages; device run against its
   allowance.
5. **Colloid Advanced**, sized by step 1 (12 or 8 oscillators, Q3).

---

## 6. Questions

| # | Question |
|---|---|
| Q1 | The first set: SRR + kernel (`cho fla dbl pit`) + `pha` + `shp`? Anything to drop or add? |
| Q2 | Alkaline: insert FX only, or also a pre-ladder fold (a new fused ladder mode per wave) |
| Q3 | Colloid Advanced (12 / `pwm` 8 Saw/Rec, 6 Sin/Tri; §2.2, §3.3, §3.4): about 119 cycles per sample of room, enough for SRR + any one FX at 1.4 cycles per instruction (the phaser's form A the tightest). Which phaser form, and the FX loop in ITCM? |
| Q4 | SRR in the Basic types too. Alkaline: ≈ 10 instructions per sample (≈ 500 … 650 cycles per block against ≈ 3,000 of room; it was left out by D4). Colloid: ≈ 16 in stereo (≈ 700 … 1,000 cycles per block) against ≈ 1,600 of Basic room (since §18) |
| Q5 | Tokens, extensions and folder names (`alx`/`clx`, `Instrument/AlkalineX`, …) |
| Q6 | One FX slot, or two in series. On the estimates a second ≈ 35 fits the Alkaline only at the low end: SRR + kernel + 35 ≈ 85 instructions ≈ 119 … 170 cycles against ≈ 163 of room. It does not fit the Colloid (≈ 119 of room; SRR + kernel + 35 ≈ 139 at 1.4) |
