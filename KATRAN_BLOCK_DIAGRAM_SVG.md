# Katran — block diagram and parameter description

**Date:** 2026-11-26. **Tree:** `dev-ph7-alkaline`, the S105 P1 Katran
(the band filter, the four chaos rows, a type per oscillator, F3 after the
matrix, the voice drive, ring/FM and Osc 2's feedback; the diagrams and the
sections below are updated for it). **Status:** description.

**Your request (2026-11-25):**
- Katran sounds right, but many parameters do little, overlap, are badly
  ranged or badly organised.
- An SVG block diagram of every module, showing where and how each parameter
  acts and how the modules connect, including the buffer read.
- The source patch beside it, labelled with the matching Katran rows.
- This document: a detailed description of the parameters and the sound
  control.
- Your view: the `src` selector should become an oscillator type `buf`; the
  `dmp` control is right in idea, but its range is off.

**Files:**

| File | What |
|---|---|
| `katran_block_diagram.svg` | The diagram (3000 × 1470). Left: the modules and their connections, with every row as a yellow pill (`short · key · range`). Right: `knowledge_files/drafts/patch.png`, embedded, so the SVG opens on its own, with 42 numbered controls and a table mapping each to Katran. Bottom right: the Katran-only rows and ten review flags |
| `tools/katran/make_block_diagram.py` | The script that draws it (`python3 tools/katran/make_block_diagram.py`). The laws in the drawing are transcribed from the code, not parsed, so they must be updated by hand when the code changes |

**Sources:**
- `Core/DSP/Instruments/Katran/KatranVoice.c` / `.h`, `KatranOsc.c`,
  `KatranParameters.c`, `KatranTables.c` (generator
  `tools/katran/gen_kat_tables.py`);
- `SD_CARD/Instrument/Katran/init.kat`;
- `knowledge_files/drafts/DRAFT_KATRAN_DRONE_SYNTH.md` §1 (the patch and its
  routing);
- `S103_KATRAN_NOISE_SYNTH.md` §8 (the planned rows).

Line numbers are `KatranVoice.c` unless another file is named.

---

## 0. How to read the diagram

| Mark | Meaning |
|---|---|
| Black arrows | Audio, every sample |
| Blue boxes / text | Laws computed once per 32-sample block (0.73 ms) |
| Green boxes / dashed | Modulators and what they move |
| Purple | The buffer (arena) read |
| Yellow pill | A row: `short label · key · range` |
| Red text / ! | A review flag (§6) |
| Yellow numbered dots (patch) | A patch control; the table beside it names the Katran row |

Everything runs every block whatever the note state (constant CPU). A trigger
never restarts the sound: it **kicks** the loop.

---

## 1. The sound engine in one page

Katran is the AIRA patch's **distorted feedback delay** (the "loop") doubled
into two cores, plus a ring modulator, an unstable LFO and two chaotic
envelopes.

1. **The loop (per core).** A short delay (32–1,792 samples, 0.7–41 ms)
   feeds TORCIDO channel 1 (drive + tube soft-clip, dry/wet). That passes
   through **the band filter** (one-pole HP then LP: cutoff / width / offset,
   S105 P1 A4), and is written back
   into the delay with the feedback gain. With enough gain the loop
   self-oscillates at the comb pitch set by the delay time.
2. **The exciter.** OSC A (the patch's saw), mixed with noise, goes through
   TORCIDO channel 2 (a bare soft clip) into the same loop input. It is never
   heard directly.
3. **The chaos.** OSC A is hard-synced whenever the loop's own output, plus a
   pulse from OSC B's wrap, crosses a threshold. The loop decides when its
   exciter restarts.
4. **What you hear.** TORCIDO channel 1's output, multiplied by OSC B (the
   patch's square: the ring modulator), then through a VCA.
   - Core A adds the raw loop (the patch's AMP2 / Out 1).
   - Core B swells the ring (AMP1 / Out 2).
5. **Two cores.**
   - **Core A** runs at the full rate.
   - **Core B** runs at half rate (darker, its loop band stops near 11 kHz).
   - Each has its own delay ring, OSC A copy (detunable) and loop.
   - They can be cross-coupled or spread apart (`spr`).
6. **Output.**
   - A tilt filter (F3);
   - a stereo matrix that places, rotates and widens the two cores (`rot`,
     `wid`) and applies the CE1 level envelope;
   - then the mixer.
7. **Life.**
   - The **run gain** (`run`) holds the loop on or lets it decay.
   - A **kick** (any trigger) re-arms it and injects a burst.
   - The **patch LFO** (rate modulated by the loop's own level and its own
     inverse) moves OSC B, F1, the tube, the feedback and the VCAs.
   - The **fast envelope** moves the level and the **slow one** the band and
     F3, both cycling freely; `chs` is the depths, `cyc` both rates, `skw`
     the rise pair and `imd` the relationship between the two (S105 P1 A3).

---

## 2. The modules, their rows and laws

### 2.1 Pitch (per block, `kat_oscControl()` `:582`)

| Row | Key | Law | Acts on |
|---|---|---|---|
| `coa` | `osc1_pitch_coarse` | semitones, the platform pitch row (n = coa + note − 63); its LFO / velocity target is the pitch **multiplier** (`osc.modNodeValue`) | OSC A **and** OSC B (both are note-relative) |
| `fin` | `osc1_pitch_fine` | ±50 cents (platform) | OSC A and OSC B |
| `coa` (fm page) | `kat_b_coarse` | OSC B offset = `bCoarse − 48` semitones (`:627`): **−48 .. +79** | OSC B; the delay time via `dtk` |
| `fin` (fm page) | `kat_b_fine` | ±0.5 semitone (`(bFine − 63)/63 × 0.5`) | OSC B; `dtk` |
| — | patch LFO | OSC B + `cv·lfl·0.3` semitone (`:629`) | OSC B |

- **The note** (step, pad, MIDI, chromatic) sets both oscillators' pitch.
- **Ceiling:** table forms are capped at 0.45 fs (core B's copy at
  0.45 fs/2). The arena form is uncapped.

### 2.2 OSC A — the exciter (one copy per core; `KatranOsc.c` `kat_aBody()`)

| Row | Key | Law |
|---|---|---|
| `typ` | `osc1_type` | Osc 1's own type: wav / wtb / pwm / **buf** (S105 P1 A8) |
| `wav` | `osc1_wave` | Osc 1's own wave; "---" under buf |
| `pos` | `osc1_position` | pwm width / wtb frame / arena window; inactive under wav while Osc 1 is not buf |
| `dtn` | `kat_spread_a` | **two effects:** detune ±(dtn'² × 25 cents) between the copies (core A −, core B +), **and** a `pos` spread of ±dtn' × 16 steps (`:599–600`) |
| `snc` | `kat_sync` | hard-sync threshold `th = sth × 258` in int16 units; 127 = never (`:1611`) |
| `lvA` | `kat_level_a` | the exciter's level into TORCIDO ch 2 (`exc`) |

- **The sync test (per sample):** `D + 0.5·32767·wrapB > th`, rising edge
  only.
  - D is the core's own delay output.
  - wrapB is OSC B's wrap flag; core B ORs the two full-rate samples.
  - On a reset the phase goes to 0 and the next row / frame / window is
    taken.
- **Core B's copy** runs at half rate with its phase step doubled.

### 2.3 OSC B — the ring/FM modulator (one, full rate)

Its own type and wave (`kat_b_type`, `kat_b_wave`: wav / wtb / pwm / buf,
S105 P1 A8), its own pitch (`coa`, `fin` on the fm page) and `pos`
(`kat_b_pos`, plus the patch LFO's `cv·lfl·24` steps). `r/f`
(`kat_ring_mode`: rng fmA fmB fAB) makes it ring a core or phase-modulate
that core's loop; `dpt` (`kat_ring_depth`) is the ring crossfade or the FM
depth; `sdb` (`kat_ring_sideband`) is its own feedback. Its wrap flag feeds
Osc 1's sync.

### 2.4 The buffer read ("arena"): where and how it enters

**What is read:**
- the FxBuffer arena (`.dtcm_fxbuf`, ≈ 126 KB, about 63 k int16 samples),
  through `fxbuf_arenaView()`, read as raw int16;
- it holds the Effect's delay and reverb lines (16-bit), CrumpBit's 8-bit
  lines, the Space float state, voice units (Tetrakt / Spc lines, chorus
  rings, **Katran's own two delay rings**) and stale or power-up memory;
- **float state and 8-bit data read as harsh noise.**

**How it enters:**
- **buf** is one of the four oscillator types (S105 P1 A8), chosen per
  oscillator (`osc1_type`, `kat_b_type`), so Osc 1, Osc 2 or both read the
  arena (`kat_oscControl()`
  `:586–587`, the loops `kat_aArenaCore*`, `kat_bArena`).
- For a selected oscillator, `typ` and `wav` are ignored.

**The read:**
1. `x = arena[S + (phase >> 22)]`: a 1,024-sample window, no interpolation.
2. **Position:** the oscillator's own `pos` sets the window start,
   S = pos × (N − 1,024)/127 (≈ 490 samples a step). It changes at the
   oscillator's next cycle start. OSC A's two copies are offset by `dtn`.
3. **Speed** is the oscillator's pitch: at the root note with `coa` 0, one
   sample per sample (≈ 43 Hz window rate). No upper limit; high settings
   skip through the window and alias.
4. **Conditioning** (always on in this form):
   - a DC block (one-pole HP ≈ 10 Hz);
   - the damping low-pass `dmp`;
   - a guard: the peak of every 8th sample sets a gain so the read stays
     ≤ 0.7 full scale (fast attack, 1 % a block release);
   - SSAT16.
5. Then into the core like any oscillator: OSC A through `lvA` / ch 2; OSC B
   into the ring.

**`dmp`'s law today** (`tools/katran/gen_kat_tables.py:66–71`):

| `dmp` | Low-pass corner |
|---|---|
| 0 | open (no filter) |
| 1 | **20 Hz** (the darkest) |
| 64 | ≈ 77 Hz |
| 127 | 300 Hz |

**See §6 F2 / F3 for the `buf` type proposal and the `dmp` range.**

### 2.5 Noise

- **Source:** the hardware RNG, read every sample in core A and every
  half-rate sample in core B.
- **Mix:** `noi` (`kat_noise`) crossfades the exciter between OSC A
  (`1 − nse`) and noise (`nse`) before ch 2.
- **The kick** adds `burst·kE·0.5` to `nse` for its 80 ms.

### 2.6 The core (per core; `kat_coreBody()`; core A full rate, core B half rate)

| Stage | Law | Rows |
|---|---|---|
| **Short Delay** | 2,048-sample ring (one FxBuffer voice unit). Timed tap at w − T (Q16 linear interpolation) plus a dry tap one block back; D = (timed + dry)/2 (the patch's delay dry/wet, fixed at 50 %) | `tim`, `fbk`, `wow`, `dtk`, `spr` |
| **Delay time** | T = katTimeSamples[`tim`] (32 × 56^(tim/127): **32..1,792 samples, 0.73..40.6 ms**) × 2^(−`dtk`' × OSC B's offset in octaves) × (1 ± 0.12·spr⁺) (A +, B −) × (1 + drift). Drift ±1.5 % × `wow`' drawn every 16 blocks, slewed. Clamped 32..2,040. Core B uses T/2 in half-rate samples (the same pitch) | `kat_time`, `kat_track`, `kat_spread`, `kat_drift` |
| **TORCIDO ch 1** (the loop) | x = D × tGain; x' = clamp(x, ±1.5); y = x' − 4/27 x'³ + tube·x'²; t1 = LV1 (wet·y + (1 − wet)·D). tGain = 2^(5·drv/127) (×1..×32). tube = 0.25·drv' + 0.15·cv·lfl. LV1 = 60/127 (fixed) | `drv` (`kat_drive`), `bln` (`kat_wet`) |
| **TORCIDO ch 2** (the exciter) | t2 = LV2 · cubic(tGain · exc · ((1 − nse)·A + nse·noise)); LV2 = 64/127 (fixed); the same drive as ch 1, no tube | `drv`, `lvA`, `noi` |
| **F1 loop filter** | TPT state-variable, coefficients ramped per sample. Types (`typ`, filter page): LP / BP / HP absolute (cutoff frq' × 115.7 semitones above 20 Hz: **20 Hz..16 kHz**), or the same three **tracked** (the loop's own pitch + frq' × 48 semitones). Plus CE2 × dp2 × **48** semitones, + cv·lfl·12 st, + spr⁺ × ±12 st (A +, B −). Damping k = 2 − 1.95·res. Core B: prewarped at fs/2, ceiling 8.1 kHz | `frq` `res` `typ` (`kat_loop_*`) |
| **F2 low-cut** | one-pole HP, 5..600 Hz log (core B: the same corner at half rate) | `lct` (`kat_low_cut`) |
| **Write** | ring[w] = SSAT16(gr × (u + xfb·D_other + fb·D)). fb = fbk' × 0.95 + 0.2·inv·lfl + kick swell, clamped at 0.95, and fb + xfb ≤ 1.1. xfb = 0.6 × (63 − spr)/63 below 63 (cross-coupling: core A reads core B's D held, core B core A's decimated) | `fbk`, `spr` |
| **Run gain gr** | the gain on every loop write (§2.9) | `run`, `kik` |

### 2.7 Ring modulator and the VCAs (the patch's RING MOD, AMP1, AMP2)

- **The ring:** r = t1 × ((1 − rk) + rk·lvB'·B).
  - rk = 1 while `lvB` > 0, 0 at `lvB` 0 (a one-block crossfade: the ring
    off, the carrier passes).
  - `lvB` (`kat_b_level`) is both the ring's on/off and its depth.
- **Core A (AMP2):** out = r + g2·t1 (adds the raw loop).
  g2 = min(1, 0.38(1 − v)) × clamp(0.42 + inv), with v = (`bal` − 63)/63
  (`:1562`).
- **Core B (AMP1):** out = r × (1 + g1) (swells the ring).
  g1 = min(1, 0.67(1 + v)) × clamp(0.42 + cv) (`:1561`).
- **`bal` (`kat_vca`)** sits at 63 for the patch balance. Lower values favour
  core A's raw loop; higher values favour core B's ring swell.
- **The output scale** is folded into these gains (S103 P4).

### 2.8 Output (`kat_outRender()`)

| Stage | Law | Rows |
|---|---|---|
| **F3 tilt** | per core, one coefficient: `cut` < 63 a one-pole LP from 16 kHz down to ≈ 190 Hz; 63 an exact pass; > 63 a one-pole HP up to ≈ 2 kHz; + CE2 × dp2 × 24 semitones | `cut` (`kat_out_cut`) |
| **Stereo matrix** | core A at pan p = −cos(2π·rotPh), core B opposite, equal-power gains. `wid` (bipolar, S103 P5): 0 no-collapse full spread; −12 the collapsing law; below, narrowing to mono at −63; above 0, M/S side up to ×2. × the CE1 gain × 0.5 (`KAT_OUT_NORM`); all four gains ramped per sample | `rot`, `wid`, `dp1` |
| **Mixer** | the slot's stereo path; `vol`, `pan` | `instrument_vol`, `instrument_pan` |

### 2.9 Life: run, kick, transport (`kat_runControl()`, `kat_kickControl()`)

| Row | Law |
|---|---|
| `run` (`kat_run`) | **127 = on:** gr rises to 1 in ≈ 20 ms. **0..126 = off:** gr decays −60 dB in T = 0.25 × 240^(run/126) s (**0.25 s .. 60 s**) |
| `kik` (`kat_kick`) | any trigger sets kE = velocity (decays −60 dB in 80 ms) and re-arms gr (≈ 5 ms). burst = min(1, 2·kik'): exc += burst·kE·0.8, nse += burst·kE·0.5. swell = max(0, 2·kik' − 1): fb += swell·kE·0.3 |
| (transport) | a stop edge decays gr at T(126) = **60 s**, even at `run` 127 (`:481`); a start re-arms it. Boot and Scene load are silent until a kick or a start. A pending Scene / Kit apply fades gr over 20 ms |

### 2.10 Modulators

**The patch LFO (`kat_plfoStep()` `:407`).**
- **Rate:** `frq` (`kat_plfo_rate`, 0.02..20 Hz log) × 2^(`mod`' ×
  (1.5·ringPrev + inv) + jitter).
  - ringPrev is the mean |D| of the two cores' last samples (the loop
    level).
  - jitter = `chs`' × 0.1 octave, redrawn each cycle.
- **Shape:** a sine whose phase is warped by w = 0.5 + 0.45·`mod`'·inv.
  `mod` 0 is a plain sine.
- **Output:** cv = shape × `dpt`' (`kat_plfo_level`), inv = −cv (`:433`).
- **Destinations:**

| Destination | Law | `dpt` applied |
|---|---|---|
| OSC B position | + cv·lfl·24 steps (`:607`) | **twice** |
| OSC B fine | + cv·lfl·0.3 semitone (`:629`) | **twice** |
| F1 cutoff | + cv·lfl·12 semitones (`:1507`) | **twice** |
| TORCIDO tube | + 0.15·cv·lfl (`:1549`) | **twice** |
| Feedback | + 0.2·inv·lfl (`:1554`) | **twice** |
| AMP1 offset (g1) | 0.42 + cv (`:1561`) | once |
| AMP2 offset (g2) | 0.42 + inv (`:1562`) | once |
| Its own rate and shape | inv | once |

**The chaos envelopes CE1 / CE2 (`kat_ceStart()`, `kat_ceStep()`).**
- **Free-running** rise / fall generators, never reset by a trigger, seeded
  apart.
- **Rows:**
  - `cy1`/`cy2`: the cycle, 10 ms..60 s log;
  - `sk1`/`sk2`: the rise share (each stage ≥ 3 blocks);
  - `dp1`: CE1 → the output level, 1 − dp1 + dp1·CE1;
  - `dp2` (bipolar): CE2 → F1 ±48 semitones and F3 ±24 semitones.
- **`chs` (`kat_chaos`):**
  - each new cycle's length × 2^(±chs');
  - its peak 1 − chs'·|r|/2;
  - above 64 the rise is hurried by the loop level;
  - it also jitters the patch LFO and the rotation.

**The rotation (`kat_stereoControl()`).** `rot` 0 holds; 1..127 turn the
cores at 0.01..4 Hz log; `chs` jitters each turn by up to ±0.5 octave.

**The platform modulation (every instrument):**
- **The LFO page** (12 rows): any MODULATABLE Katran row, two destinations;
  pitch through the multiplier.
- **Velocity** (`dst` / `amt` on the mod page): one row; velocity also
  scales the kick burst.
- **Morph, step automation, MIDI:** every MORPHABLE / AUTOMATABLE row.

---

## 3. Every row (index, page, law)

Pages: 0 osc, 1 aeg, 2 mod, 3 fm, 4 click, 5 filter, 6 lfo, 7 mix. "init" is
`init.kat`.

| # | Page.cell | Short | Key | Range / law | Acts on | init |
|---:|---|---|---|---|---|---:|
| 0 | osc.0 | coa | osc1_pitch_coarse | semitones (platform) | OSC A + B pitch | 36 |
| 1 | osc.1 | fin | osc1_pitch_fine | ±50 cents | OSC A + B | 63 |
| 2 | osc.2 | pos | osc1_position | width / frame / window; inactive under wav | OSC A | 46 |
| 3 | osc.3 | dtn | kat_spread_a | detune ±25 c (squared) + pos ±16 | OSC A copies | 0 |
| 4 | osc.4 | wav | osc1_wave | the shared wave | OSC A + B | 2 |
| 5 | osc.5 | typ | osc1_type | wav / wtb / pwm | OSC A + B | 2 (pwm) |
| 6 | osc.6 | src | kat_source | off / A / B / AB → the arena form | OSC A / B form | 0 |
| 7 | osc.7 | dmp | kat_damp | 0 open; 1..127 = 20..300 Hz LP | arena read | 40 |
| 8 | aeg.0 | run | kat_run | 127 on; 0..126 decay 0.25..60 s | run gain | 127 |
| 9 | aeg.1 | kik | kat_kick | 0..63 burst; 64..127 + swell | kick | 40 |
| 10 | aeg.2 | cy1 | kat_ce1_cycle | 10 ms..60 s | CE1 | 90 |
| 11 | aeg.3 | sk1 | kat_ce1_skew | rise share | CE1 | 63 |
| 12 | aeg.4 | dp1 | kat_ce1_depth | 0 steady..127 full swell | output level | 0 |
| 13 | aeg.5 | cy2 | kat_ce2_cycle | 10 ms..60 s | CE2 | 100 |
| 14 | aeg.6 | sk2 | kat_ce2_skew | rise share | CE2 | 63 |
| 15 | aeg.7 | dp2 | kat_ce2_depth | bipolar: F1 ±48 st, F3 ±24 st | F1, F3 | 63 (0) |
| 16 | mod.2 | chs | kat_chaos | CE / LFO / rotation jitter; loop-coupled > 64 | modulators | 20 |
| 17 | mod.4 | frq | kat_plfo_rate | 0.02..20 Hz | patch LFO | 74 |
| 18 | mod.5 | mod | kat_plfo_mod | self / loop rate mod + shape warp | patch LFO | 57 |
| 19 | mod.6 | dpt | kat_plfo_level | depth (squared for 5 destinations) | patch LFO | 51 |
| 20 | mod.7 | bal | kat_vca | bipolar VCA balance | AMP1 / AMP2 | 63 |
| 21 | fm.0 | coa | kat_b_coarse | −48..+79 semitones | OSC B pitch, `dtk` | 48 (0) |
| 22 | fm.1 | fin | kat_b_fine | ±0.5 st | OSC B | 64 |
| 23 | fm.2 | pos | kat_b_pos | width / frame / window | OSC B | 60 |
| 24 | fm.3 | lvB | kat_b_level | 0 ring off; depth | ring | 55 |
| 25 | fm.4 | lvA | kat_level_a | exciter level | ch 2 | 50 |
| 26 | fm.5 | noi | kat_noise | OSC A ↔ noise | exciter | 0 |
| 27 | fm.6 | snc | kat_sync | threshold; 127 never | OSC A sync | 60 |
| 28 | click.0 | drv | kat_drive | ×1..×32; tube follows | TORCIDO 1 + 2 | 56 |
| 29 | click.1 | bln | kat_wet | TORCIDO 1 dry / wet | ch 1 | 57 |
| 30 | click.2 | tim | kat_time | 32..1,792 smp (0.7..41 ms) | delay | 74 |
| 31 | click.3 | fbk | kat_feedback | 0..0.95 | loop gain | 61 |
| 32 | click.4 | wow | kat_drift | ±1.5 % wander | delay time | 0 |
| 33 | click.5 | dtk | kat_track | delay follows OSC B's offset | delay time | 0 |
| 34 | filter.0 | frq | kat_loop_cutoff | 20 Hz..16 kHz (abs) / + 0..48 st (tracked) | F1 | 53 |
| 35 | filter.1 | res | kat_loop_reso | k = 2 − 1.95 res | F1 | 30 |
| 36 | filter.2 | typ | kat_loop_type | LP BP HP, ×{abs, tracked} | F1 | 0 (LP) |
| 37 | filter.3 | lct | kat_low_cut | 5..600 Hz | F2 | 20 |
| 38 | filter.4 | cut | kat_out_cut | bipolar LP / HP tilt | F3 | 63 (open) |
| 39 | mix.0 | vol | instrument_vol | mixer | out | 100 |
| 40 | mix.1 | pan | instrument_pan | mixer | out | 63 |
| 41 | mix.2 | spr | kat_spread | bipolar: xfb / spread | cores | 63 |
| 42 | mix.3 | wid | kat_width | bipolar width (S103 P5) | matrix | 63 (0) |
| 43–50 | lfo | — | lfo_* | the platform LFO | any row | off |
| 51–52 | mod.0–1 | amt / dst | velo_mod_* | velocity | one row | off |
| 53–56 | lfo | — | lfo_target_* | the LFO's destinations | — | off |
| 57 | mod.3 | rot | kat_rotate | 0 hold; 0.01..4 Hz | matrix | 34 |
| 58–59 | — | — | — | free | — | — |
| 60–63 | (click.8–11) | — | fx_* | reserved for the later effect | — | — |

**37 own rows, 21 platform rows.** The signal chain is spread over six pages
(§6 F10).

---

## 4. The patch ↔ Katran (the 42 markers in the SVG)

| Patch module | Control → Katran |
|---|---|
| **RING MOD** | ON/OFF → `lvB` 0 (ring off). IN ← TORCIDO ch 1 (t1). MOD IN ← OSC B. OUT → both outputs and AMP1; its level (ringPrev) → the LFO rate |
| **SQR OSCILLATOR** → OSC B | range + coarse → `coa` (fm, `kat_b_coarse`). COLOR → `pos` (fm). FINE → `fin` (fm). LEVEL → `lvB`. SYNC OUT → OSC A sync (weight fixed 0.5). CV IN ← GRF2 |
| **SAW OSCILLATOR** → OSC A | range → `coa` (osc). COLOR → `pos` (osc). FINE → `fin` (osc). LEVEL → `lvA`. SYNC TRIG IN ← SQR sync + delay out → `snc` |
| **LFO** | RATE → `frq` (mod). MOD IN → `mod`. WAVE → folded into `mod` (a warped sine). OUTPUT → `dpt`. CV / INV → the eight destinations (§2.10) |
| **AMP** | AMP1 → `bal` (g1, core B). AMP2 → `bal` (g2, core A). Both OFFSETs → fixed 0.42 (+ cv / inv) |
| **TORCIDO** | DIST → `drv`. TONE → F1 (`frq`, with `res`, `typ` added). TUBE WARM → follows `drv` (0.25·drv'). DRY/WET → `bln`. LEVEL ×4 → fixed (LV1 60, LV2 64). LO BOOST, BYPASS → not carried |
| **SHORT DELAY** | TIME → `tim`. FEEDBACK → `fbk`. DRY/WET → fixed 50 % (the 1-block dry tap) |
| **GRF 1–6** | 1 → OSC A `coa`; 2 → OSC B `coa` + `dtk`; 3 → `frq`; 4 → `bln`; 5 → OSC A `pos`; 6 → `lvB` |
| **OUTPUT 1 / 2** | core A (AMP2 side) / core B (AMP1 side), now placed by `rot` and `wid` |

**Katran-only rows (no patch control):**
- `run`, `kik`;
- `cy1 sk1 dp1 cy2 sk2 dp2 chs`;
- `src dmp`;
- `noi`;
- `dtn`;
- `wow`;
- `spr`;
- `dtk`;
- `res typ lct` (F1, F2);
- `cut` (F3);
- `wid`, `rot`;
- the second core.

---

## 5. Fixed values (no row today)

| Value | Where | Patch control it replaced |
|---|---|---|
| LV1 = 60/127, LV2 = 64/127 | TORCIDO levels | LEVEL ×4 |
| tube = 0.25 × drv' | TORCIDO | TUBE WARM |
| AMP offsets 0.42 | VCAs | AMP1 / AMP2 OFFSET |
| delay dry / wet 50 % | D = (timed + dry)/2 | DELAY DRY/WET |
| sync weight P = 0.5 | OSC B wrap in the sync test | (the SQR → SAW sync cable) |
| kick decay 80 ms; burst targets 0.8 exc, 0.5 nse; swell 0.3 fb | kick | — |
| patch-LFO depth constants: 24 steps, 0.3 st, 12 st, 0.15, 0.2 | LFO destinations | the LFO cable set |
| LFO shape: sine warped by `mod` | patch LFO | LFO WAVE |
| F3 resonance none (one-pole) | F3 | — |
| output norm 0.5; arena guard 0.7, DC block 10 Hz | output; arena | — |
| delay ring 2,048; tim max 1,792 | delay | — |

---

## 6. Review findings (for the redesign; nothing changed)

Each finding cites its evidence. The suggestions are starting points for the
discussion, not decisions.

### F1 — The patch-LFO depth (`dpt`) is applied twice for five destinations

- **Evidence:** `cv = shape × lfl` (`:433`), then OSC B pos (`:607`), OSC B
  fine (`:629`), F1 (`:1507`), the tube (`:1549`) and the feedback (`:1554`)
  each multiply by `lfl` again.
- **Effect:** those depths scale with lfl². At `init` (`dpt` 51 → 0.40) they
  run at 0.16 of full: F1 ±1.9 semitones, OSC B fine ±0.05 semitone, the
  tube ±0.024, the feedback ±0.03. **This is a large part of "the LFO does
  little".** The VCA offsets and the LFO's own rate get `lfl` once.
- **Suggestion:** apply `lfl` once (drop the second factor). Then retune the
  five depth constants by ear, since the current sound was tuned with the
  squared law.

### F2 — The buffer read selector (`src`) versus a `buf` oscillator type

- **Today:** `src` (off / A / B / AB) overrides the shared `typ` per
  oscillator; `typ` has three values (wav / wtb / pwm).
- **Your proposal:** a fourth `typ`, **`buf`**. It is natural (the read *is*
  an oscillator form: a window, a position, a speed), and it would retire
  the `src` row.
- **The catch:** `typ` and `wav` are **shared** by OSC A and OSC B (K-D8). A
  `buf` value of the shared `typ` can only switch **both**. "One or both"
  needs a per-oscillator type. Options:
  - **(a)** Give OSC B its own `typ` row (one of the free indices 58–59).
    OSC A's `typ` then includes `buf`, and OSC B's `typ` (wav / wtb / pwm /
    buf) covers B alone, A alone, or both.
  - **(b)** `typ` = wav / wtb / pwm / buf, where `buf` means "both read the
    buffer", plus a bipolar or three-way `buf` target. This is close to
    today's `src`.
  - **(c)** `typ` `buf` switches OSC A only (the exciter, where the buffer is
    heard through the loop), and OSC B never reads it.
- **Leaning:** (a). It frees `src`'s cell, gives OSC B its own waveform (the
  draft's D§3.4 option), and lets OSC B keep a stable ring modulator while
  OSC A reads the buffer.
- **Also with a `buf` type:** `wav` under `buf` could choose the read form:
  the current raw read, an interpolated read, a reverse or ping-pong read.
  Today `wav` does nothing for an arena oscillator.

### F3 — `dmp` (the buffer's damping) is badly ranged

- **Evidence:** `damp_k()` (`gen_kat_tables.py:66–71`). 0 is open; 1 is a
  20 Hz low-pass (the darkest, next to "open"); 127 is only 300 Hz.
- **Effect:** almost all of the range is very dark; nothing sits between
  300 Hz and open.
- **Suggestion:** one monotonic law from open to dark, e.g. 127 = open and
  0 = ≈ 100 Hz, log, or a bipolar tilt. Also consider whether 0.7 FS for the
  guard is right once the range is fixed.

### F4 — OSC B coarse spans −48 … +79 semitones

- **Evidence:** `bCoarse − 48` (`:627`); the plan said −36 … +24.
- **Effect:** half the knob is far above any useful ring ratio.
- **Suggestion:** ±24 or ±36 semitones, centred at 63 like the other bipolar
  rows. Consider a "ratio" law (harmonic steps) instead of semitones for a
  ring modulator.

### F5 — The transport stop decays over 60 s

- **Evidence:** `katRunK[126u]` (`:481`): T(126) = 0.25 × 240 = 60 s.
- **Effect:** a drone at `run` 127 rings for about a minute after Stop.
- **Suggestion:** use the `run` row's own decay (or a fixed few seconds) for
  the stop edge, or let Stop follow `run` when `run` < 127.

### F6 — `dtn` moves two things at once

- **Evidence:** detune ±(dtn'² × 25 cents) and `pos` ± dtn' × 16 steps
  (`:599–600`).
- **Effect:** under wav (`pos` inactive) only the detune acts; under
  pwm / wtb / buf both act. The squared detune law makes the first half of
  the knob nearly inert (dtn 64 = ±6 cents).
- **Suggestion:** a linear (or log) detune to ±25–50 cents; and either a
  separate pos-spread, or none (the cores already differ in rate).

### F7 — CE2's ±48 semitones on F1

- **Evidence:** `dp2 × 48` (`:1507`).
- **Effect:** four octaves either way at full depth, so most of `dp2`'s
  range is extreme, and small depths are hard to set.
- **Suggestion:** ±24 semitones, or a squared depth law.

### F8 — `bal` (the VCA balance) is opaque

- **Evidence:** g1 = min(1, 0.67(1 + v))·clamp(0.42 + cv), g2 =
  min(1, 0.38(1 − v))·clamp(0.42 + inv) (`:1561–1562`).
- **Effect:** its centre is the patch, but its ends are not "core A only" or
  "core B only". It changes the asymmetry between the cores rather than a
  level anyone hears directly. Since S103 P4 the cores also rotate, so "L
  versus R" no longer describes it.
- **Suggestion:** split it into what it does:
  - a "loop vs ring" mix (core A's raw-loop amount g2);
  - a "ring swell" amount (core B's g1);
  - or one "dry loop" amount that applies to both cores.

### F9 — Fixed values hide levers the patch had

- **What is fixed:** the AMP offsets, the TORCIDO levels, the delay dry/wet,
  the tube tied to the drive, the patch-LFO destination depths (§5).
- **Effect:** some of these may be the "missing" sound controls (for
  example the delay dry/wet sets how resonant versus smeared the comb is).
- **Suggestion:** decide by ear which become rows, ideally by *replacing*
  weak rows (§F11), not adding.

### F10 — The page layout splits the signal chain

| Page | Holds |
|---|---|
| osc | OSC A's pitch, its form and the buffer |
| aeg | run / kick and both CEs |
| mod | the patch LFO and the VCA balance |
| fm | OSC B, the exciter and sync |
| click | the delay and TORCIDO |
| filter | F1 / F2 / F3 |
| mix | the cores' spread and the width |

- **Effect:** making one sound means visiting six pages.
- **A chain-ordered layout:**
  - **exciter:** OSC A, noise, sync;
  - **loop:** delay, TORCIDO, F1, F2, feedback;
  - **ring:** OSC B, the ring, the VCAs;
  - **space:** the two cores, `spr`, `dtn`, `wid`, `rot`;
  - **motion:** run / kick, the patch LFO, CE1, CE2, chaos;
  - the platform LFO and mix pages unchanged.

### F11 — Rows likely to "not do much" (to confirm by ear)

| Row | Why it may be weak |
|---|---|
| `dpt`, and with it `frq` / `mod` | F1 |
| `wow` | ±1.5 % at full is subtle for a 0.7–41 ms delay; the drift is redrawn every ~12 ms |
| `dtk` | acts only when OSC B's coarse / fine is off-centre |
| `pos` (osc) | inactive under `wav` (the `init` form is pwm, so it acts there) |
| `snc` | depends on the loop level reaching the threshold; at low feedback most values never fire, at high feedback most fire every cycle |
| `chs` | mostly acts on cycle-to-cycle timing of slow envelopes; its loop coupling only above 64 |
| `cut` vs `frq` | two lowpass-type tone controls (F3 on the output, F1 in the loop); F3 is a gentle one-pole tilt |
| `sk1` / `sk2` | shape the CEs, which are themselves often slow or shallow (`dp1` 0 at `init`) |

### F12 — Combinations that suggest themselves

- **`dp1` with `cy1`**, and **`dp2` with `cy2`**, are pairs that only matter
  together.
- **`snc` and the sync weight P:** one "re-clock" amount could set both.
- **`lvA` and `noi`:** "exciter level" and "exciter colour".
- **`spr` (core coupling / spread) and `dtn` (detune / pos spread):** both
  "make the cores differ"; one bipolar "cores: coupled ↔ apart" row could
  carry `spr`'s xfb, the detune and the time spread.

---

## 7. What this document does not cover

- **No measurements** of how loud or audible each row is. The findings come
  from the laws in the code; the by-ear pass is yours.
- **Nothing about CPU.** Any redesign that adds a per-oscillator type, an
  interpolated buffer read or new rows needs the S103 P4 CPU check (Katran
  is at 366.6 of the 380 gate) and the 1,200 B voice-size assert (1,100 B
  now).
