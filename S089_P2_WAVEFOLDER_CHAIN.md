# S089 P2 — Wavefolder Voice Chain (`WFd`)

**Status:** Initial plan (2026-10-30). Not implemented.
**Depends on:** S089 P0 (per-instrument SRR enable; hardware passed) and the
P1 chain architecture (`S089_P1_VOICE_CHAIN_PLAN.md` §2–§3: the `chn`
selector, the engine dispatch and the cell relabelling).
**Budget source:** `S089_P1_VOICE_CHAIN_PLAN.md` §1 (measured on the S089 P0
build) and §4.6 (fold costs).

---

## 0. Goal

The first alternative voice chain, `WFd` ("WavFldr"), keeps the default
chain except for one stage:

| Stage | Default `T+D` | `WFd` |
|---|---|---|
| Ahead of the filter | transient click, mixed in | **wavefolder** on the oscillator signal |
| Filter input clip `sft` (`Filter` `drv`) | kept | **kept** |
| ZDF filter | unchanged | unchanged |
| Post-chain: amp ramp, velocity ramp, distortion (`Voice` `drv`) | unchanged | unchanged |
| SRR (`srt`) | on | on (`instrumentManager_slotUsesSrr()` returns 1 for both chains) |

The transient page's three controls drive the fold:

| Cell | `T+D` meaning | `WFd` meaning |
|---|---|---|
| `wav` | transient waveform | **fold type** (§3) |
| `vol` | transient level | **fold depth** |
| `frq` | transient pitch | **a second control chosen per type:** bias/symmetry, asymmetry, harmonic blend or depth-decay time (§3) |

Because the fold sits ahead of the amp envelope, its timbre does not follow
the voice's decay on its own. The decay-type folds (§3) add that movement
deliberately.

---

## 1. Placement per engine

| Engine | Where the fold runs | What it replaces | Note |
|---|---|---|---|
| **Drum** | in place on `buf`, after the oscillator (and the `itp` old-carrier blend), before `SVF_calcBlockZDF()` | `transient_calcBlock()` + the `QADD16` mix-in | an exact match for "instead of the transient, ahead of the filter" |
| **Cymbal** | in place on `buf`, after the FM carrier (and `itp` blend), before the ZDF | `transient_calcBlock()` into `mod`, plus the add in `voicePost_addGainDist()` | **Hazard:** the post loop adds `mod`, which still holds the FM modulator signal when the transient does not run. The post loop needs a no-add variant, or `mod` must be zeroed (§2). |
| **HiHat** | the same as Cymbal (`mod1`) | the same | the same hazard |
| **Snare** | **on the tone oscillator block** (`transBuf` after the osc render, before `voicePost_mixAddGainDist()`) | `transient_calcBlock()` + the post-filter `QADD16` add | Snare's only pre-filter signal is noise, and folding noise does little. Its tone oscillator is unfiltered, so the fold sits ahead of the post-chain rather than the filter. **Decision D1.** |

**The transient-mode paths must be guarded.** All four engines read
`transGen.waveform` outside the generator: value 0 (`Snp`) drives the snap
pitch envelope in the async calc, and value 1 (`Ofs`) sets the oscillator
start offset at trigger. Under `WFd` the same stored byte means a fold type,
so both paths need `chainType == VOICE_CHAIN_DEFAULT &&` in front of them.
The four sites are:
- Drum: `DrumVoice.c:310/317/462`
- Snare: `Snare.c:213/309`
- Cymbal: `CymbalVoice.c:233/336`
- HiHat: `HiHat.c:243/355`

---

## 2. Budget (filter drive kept)

`WFd` gives up only the transient's cost. From P1 §1.1:

| Engine | Replaced | Pass budget |
|---|---|---:|
| Drum | click 22 + mix-in 2 | **24** |
| Snare | click 22 + add pass 2 | **24** |
| Cymbal / HiHat, with a no-add post-loop variant | click 22 + fused add 4 | **26** |
| Cymbal / HiHat, `mod` zeroed instead (`memset` 64 B per block ≈ 0.5 per sample) | click 22 − 0.5 | **≈21** |

**Recommendation: the no-add variant for Cymbal/HiHat.** It is one extra
`voicePost_gainDist()` loop shared by both engines, and it lets them use the
full type list. The zeroing route rules out the sine folds on Cymbal/HiHat.
The new loop goes into the mixer-inlined path, so it must pass the S089 P0
§4.2 gates (inlined-set diff and hot-loop comparison).

**Pass cost model.** These are estimates of instructions per sample, ±2,
until measured. Each pass costs:
- **Overhead, ≈12:** load, convert to float, a depth multiply with a
  per-sample depth ramp, convert back, store, and loop control.
- **The fold core.**

For the triangle families, bias and the DC compensation fold into per-block
constants, so they cost nothing per sample.

---

## 3. Fold types: suggested `wav` list with `vol` and `frq` assignments

**Depth (`vol`), every type.** `depth = 1 + 7 × vol`, the ×1–×8 mapping
measured on the S084 spike's `Fld`. At `vol` 0 the triangle folds are
identity (no folding) and the sine folds are gentle saturation. The ramp
runs per sample from last block's depth, kept in `transGen.midiFreq` as
Q12 (unused by the transient), so `vol` edits, Morph, LFO and velocity
never step. `transient_vol` is already `MOD_0_127`, so **fold depth is an
LFO and velocity target with no new code**, and velocity → depth gives
velocity-dependent brightness.

**`frq`, read back per block.** Its writer stays unchanged
(`pitch = 1 + v/33.9 − 0.75`). The fold recovers `v/127 = (pitch − 0.25) ×
33.9/127` once per block. Morph, automation and LFO on `frq` all work as
they do today.

| `wav` | Short / long | Algorithm | `vol` | `frq` | Est. pass | Drum / Snare (24) | Cym / HiHat (26) |
|---:|---|---|---|---|---:|:-:|:-:|
| 0 | `Tri` / `TriFold ` | triangle fold, `driveShaper_triFold()` | depth | **bias** (64 = symmetric; ±0.3 offset, DC-compensated) | ≈19 | ✓ | ✓ |
| 1 | `TrD` / `TriDecay` | triangle fold, depth envelope | peak depth | **depth decay**, ≈5 ms – 2 s | ≈19 | ✓ | ✓ |
| 2 | `Sin` / `SinFold ` | triangle → 5th-order sin(π/2·t) polynomial | depth | **bias** | ≈23 | ✓ (1 spare) | ✓ |
| 3 | `SnD` / `SinDecay` | sine fold, depth envelope | peak depth | **depth decay** | ≈23 | ✓ | ✓ |
| 4 | `Asn` / `AsymFold` | sine fold of x + a·x³ | depth | **asymmetry** a, 0 – 0.2 (spike fixed 0.05) | ≈25 | ✗ (+1) → see §4 | ✓ |
| 5 | `Chb` / `Chebyshv` | input clamped to ±1, then T3 = 3x − 4x³ | drive into the polynomial | **harmonic blend** (dry → T3) | ≈19 | ✓ | ✓ |

**Optional extras, if the menu should be longer:**

| `wav` | Short / long | Algorithm | `vol` | `frq` | Est. pass | Fits |
|---:|---|---|---|---|---:|:-:|
| 6 | `TrI` / `IntFold ` | integer triangle fold: bit-exact, cheapest, hardest corners | depth | bias | ≈12 | ✓ all |
| 7 | `Fd2` / `Fold2x  ` | two cascaded integer folds: very dense | depth | second-stage gain ratio | ≈18 | ✓ all |

**Notes on the types.**
- **Bias versus asymmetry.** `Tri` and `Sin` with bias already cover
  asymmetric folding (the S084 `asf` behaviour), so there is no separate
  `Asf` type. `Asn` stays because its cubic accent is a different
  asymmetry: it bends the fold points instead of shifting them.
- **Decay envelope (`TrD`, `SnD`).** On every trigger the depth jumps to
  its peak, then falls back to 1 (clean for `TrD`, gentle for `SnD`). That
  gives a pluck-like timbre sweep that works with the amp envelope.
  - **State:** the existing `snapEg.value` float. `SnapEg_trigger()` already
    runs on every trigger in all four engines; under `WFd` the snap pitch
    path is guarded off (§1), so that float is free.
  - **Shape:** like `SnapEg`, a linear fall of `value` with `value²` as the
    envelope.
  - **Range:** the decay range comes from a 128-entry per-block reduction
    table (512 B flash), indexed by the recovered `frq` and spanning
    ≈5 ms – 2 s. `SnapEg`'s own range is only ≈1–15 ms (`SNAP_REDUCTION`
    0.2 × pitch per block), which is click length.
  - **Cost:** the decay rides the existing depth ramp, so it adds nothing
    per sample.
- **Retrigger in `fre`/`itp`.** The jump back to peak depth is spread over
  one block by the same depth ramp, the S088 one-block approach, so it does
  not click.
- **Chebyshev.** T3 grows without bound past ±1, so the input is clamped
  (`vmaxnm`/`vminnm`) first. The `frq` blend `x + m·(T3 − x)` runs from
  clean to a pure added third harmonic. It is the most "tonal" option.
- **DC.** Biased and asymmetric folds subtract the fold of the bias (a
  per-block constant), the spike's method. The output is exactly 0 at
  silence, so there is no thump when a voice ends. Some DC remains while
  the sound plays. A one-pole DC blocker would cost ≈3–4 per sample and
  8 B of state, which pushes `Sin` and `Asn` over budget. **Decision D3.**
- **Aliasing.** Folding makes partials above Nyquist, and the budget has no
  room for oversampling. The low-pass filter after the fold tames the
  upper partials but not what has already aliased below the cutoff. The
  sine folds alias less than the triangle folds. (SRR already treats
  aliasing as character.)

---

## 4. Filter drive: keep or bypass

**Cost.** Every type keeps `sft` except `Asn` on Drum/Snare, which is
estimated one over budget (25 against 24). The options for `Asn`, in order
of preference:
1. **Measure first.** The estimate is ±2, so the 5th-order build may come
   in at 24.
2. **Offer `Asn` on Cymbal/HiHat only** (budget 26), with a per-engine type
   count.
3. **Use a cheaper cubic accent:** an `x·(1 + a·x²)` form fused with the
   polynomial's `t²`. That needs care, because the accent is applied before
   the fold, not after.
4. **Bypass `sft` for `Asn`.** This needs a clean ZDF loop variant: a new
   copy of the filter loop for every filter type. That is large in flash,
   and S084's filter-slot variants locked the interface on hardware. **Not
   recommended for P2.**

**Sound with the drive kept.** None of the types *needs* the bypass for its
sound. At its minimum (drive 0.4 at `drv` 0), `sft` is nearly linear for a
full-scale input (≈tanh 0.4).

| Type | What `sft` does after it | Recommendation |
|---|---|---|
| `Tri`, `TrD`, `TrI`, `Fd2` | rounds the fold's sharp corners at high `drv`, so `drv` works as a "tame/warm" control | keep |
| `Sin`, `SnD` | flattens the fold peaks further: fold plus soft clip, smooth and loud | keep |
| `Asn`, biased `Tri`/`Sin` | the symmetric clip of an offset signal adds even harmonics and moves the DC with `drv` | keep; DC compensation runs before the filter |
| `Chb` | adds odd harmonics on top of the pure third, which blurs its character at high `drv` | keep; suggest low `drv` in the factory sounds |

**What a bypass would buy.** It frees 13 per sample (Drum/Snare 37, P1 §1.2
budget A). That is enough for `Asn` with a DC blocker, or for two folds in
series, but not enough to also keep the click. Treat it as a later chain
(for example `WF2`) once the S084 filter-slot lock is understood, not as
part of P2.

---

## 5. Implementation outline

P2 builds on the P1 architecture. P1 §5.1 steps 1–5 and 8–9 (the `chn`
parameter, the engine field, the descriptor row, the special writer, the
menu tag and the init) are prerequisites, and are done in this phase if P1
has not landed yet.

1. **The fold code: `Core/DSPAudio/voiceFold.c/.h`.**
   - **One non-inlined dispatcher, `voiceFold_block(type, buf, size, depth0,
     depth1, p)`, with one loop per type.** The type switch runs once per
     block, outside the loops. One shared copy keeps four engines × six
     types out of `mixer_calcNextSampleBlock()`, which matters given LTO's
     inlining sensitivity (P0 F3). This is the S087 pattern: the `smp`/`slp`
     reader functions called from the engines.
   - **Build and placement.** Compile with `-Ofast` (a Makefile rule like
     the other DSP files). Place the code in ITCM like the S087 readers.
     ITCM is 12,656 / 16,384 B, and the estimate is ≈0.8 KB. **ITCM growth
     counts as RAM and needs user sign-off (D4).**
   - **The decay table:** 128 floats in flash (512 B).
2. **Engine dispatch (P1 §3.2), in all four engines.** Under `WFd`:
   - skip `transient_calcBlock()` and the mix-in or add;
   - compute `depth0`/`depth1` per block from `vol`, the `snapEg` value
     (decay types) and the Q12 last depth in `transGen.midiFreq`;
   - call `voiceFold_block()` at the §1 position.
3. **Guards:** `Snp`/`Ofs` at the eight sites in §1.
4. **Cymbal/HiHat post loop:** add `voicePost_gainDist()`, the no-add twin
   of `voicePost_addGainDist()`, and use it under `WFd`.
5. **Menu and labels:**
   - under `WFd`, the `wav` cell uses a fold-name table and the fold-type
     count (render and menu clamp);
   - `vol` reads "Depth";
   - `frq`'s long name follows the type: "Bias", "Decay", "Asymmtry",
     "Harmonic";
   - the stored `transient_wave` byte is never rewritten, so switching back
     to `T+D` restores the original click.
6. **Host tests (`tools/dsp_test/modules/test_voicefold.c`):**
   - each type against a Python reference
     (`tools/plots/plot_waveshapers.py` from the S084 spike, extended);
   - a depth sweep, and silence → 0;
   - the bias DC compensation;
   - continuity of the depth ramp across blocks.
7. **Cost gate:** `elf_loopdump.py --sym voiceFold_block` per type against
   the §2 budgets, plus the S089 P0 §4.2 gates for the mixer.

---

## 6. Verification

- **Static:** the §5 step 7 gates. With `chn` at `T+D` everywhere, the
  firmware's audio path must stay bit-identical to the S089 P0 build (S0).
- **Hardware:**
  - **`T+D` regression:** existing kits sound the same, including
    `Snp`/`Ofs` transients.
  - **Every fold type, on all four engines:** depth sweep; `frq` sweep;
    `frq` at its extremes and centre.
  - **Drive:** `Filter drv` 0 and 127 with each type.
  - **Decay types:** short and long decays; retriggers in `rst`, `fre` and
    `itp` without clicks.
  - **Modulation:** LFO and velocity → `vol` (depth) and → `frq`.
  - **Chain switching:** step-automate `chn` between `T+D` and `WFd`.
  - **Load:** the worst-case six-slot Scene with all slots on `WFd`; the
    `cpu` reading must not exceed the `T+D` reading.

---

## 7. Decisions

| # | Decision | Recommendation |
|---|---|---|
| D1 | Snare: fold the tone oscillator (unfiltered) or the noise ahead of the filter? | tone oscillator |
| D2 | `Asn` on Drum/Snare if it measures over 24 | Cymbal/HiHat only |
| D3 | DC blocker on the biased/asymmetric types (+3–4 per sample, 8 B) or compensation only | compensation only |
| D4 | `voiceFold` code in ITCM (≈0.8 KB of the ≈3.7 KB free) | yes |
| D5 | The type list: the six core types, or with `TrI`/`Fd2` | six core types first |
| D6 | Depth mapping: linear ×1–×8 (spike) or squared, for finer control near clean | linear first; adjust by ear |
| D7 | The `frq` display for bias: raw 0–127 or centred ±63 | 0–127 first (no new display type) |
