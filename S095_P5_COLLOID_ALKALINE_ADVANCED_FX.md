# S095 P5 — Advanced Colloid and Alkaline: per-voice FX (investigation)

- **Status:** investigation, written 2026-11-13 from the tree at `4602b3b3`.
  Nothing changed in the source. Cost figures marked *est.* are estimates
  from comparable loops in the tree; every one is measured before a
  schedule is approved.
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
- **Alkaline Advanced** has an estimated 3,500 cycles per block of room
  (about 110 cycles per sample). That pays for **SRR plus one insert FX**
  from a shared modulated-delay kernel (chorus, flanger, doubler, pitch
  shift) or a phaser or a waveshaper.
- **Colloid Advanced** cannot be sized yet: the Colloid's device cycle count
  has not been taken (S095 P3 §10.9 step 3). At 382 instructions per sample
  on the host it may already sit above 19,620 cycles. If it does, an
  Advanced Colloid has to give up oscillators to carry FX (§3.3).
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
| Voice slot reserve | 1,264 B per tagged slot; Alkaline 560 B, Colloid 1,112 B (`InstrumentManager.c:222–223`) | Colloid has 152 B left |

---

## 2. The budget per variant

### 2.1 Alkaline

- Device: six Alkalines measured 126,853 cycles per block (S094 P2 §17).
  Less the outside-mixer (4,701) and shared-mixer (≤ 12,784) shares, that
  is ≤ 18,230 per Alkaline.
- Room in an Advanced slot: 21,803 − 18,230 ≈ **3,570 cycles per block,
  ≈ 110 per sample** *est.*
- The Alkaline runs about 2.2 cycles per instruction (263.7 instructions
  per sample, flash-resident, 3 serial VDIV). A small FX loop reading DTCM
  should do better, but its code adds to an I-cache that the Alkaline and
  the mixer already overfill (S094 P2 §3.3). Plan on **50–80 instructions
  per sample**, control work included.

### 2.2 Colloid

- Host: 382.1 instructions per sample in `plain12` stereo (283 in loops +
  3,172 per block / 32; `baselines/s095p3/colloid_cpu.txt`). Its
  oscillator loops are in ITCM.
- At 1.5 cycles per instruction that is ≈ 18,300 cycles per block (room
  ≈ 3,500); at 2.0 it is ≈ 24,500 (no room, and above the Basic 19,620).
- **First step for any Colloid work:** the cycle probe (DEV_MODE_LOGGING,
  trace stage `p`) on one Colloid in `plain12` stereo.

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
- **Flash:** 33,596 B of headroom in the current link (`link_budget.py`).
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
| **Phaser** | 4–6 first-order allpasses, feedback, coefficient per block | none | ≈ 34 (L = dry + ap, R = dry − ap) | ≈ 66 (two chains) | **Yes** | The cheapest stereo modulation on a mono voice |
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

### 3.3 Colloid: what pays if the measurement says there is no room

If the probe puts the Colloid above about 18,300 cycles per block:

- each oscillator costs about 21 instructions per sample *est.* (its loop
  share plus its per-block slewed pitch);
- an Advanced Colloid with **8 oscillators** instead of 12 frees about
  85 instructions per sample, enough for SRR + the kernel;
- the alternative is no FX on the Colloid, and an Advanced Alkaline only.

---

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

1. **Measure** (no source change): the cycle probe on one Alkaline (in its dearest
   `typ`/`wav` setting) and one Colloid (`plain12` stereo). Sets each
   variant's allowance.
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
| Q3 | Colloid: if the measurement says there is no room, 8 oscillators in the Advanced type, or no Advanced Colloid |
| Q4 | SRR in the Basic Alkaline too (≈ 10 per sample; it was left out by D4) |
| Q5 | Tokens, extensions and folder names (`alx`/`clx`, `Instrument/AlkalineX`, …) |
| Q6 | One FX slot, or two in series (only if the measurement leaves room for a second ≈ 35) |
