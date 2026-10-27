# S085 post-mortem: oscillator run modes and option costs

This document covers:
- the run-mode behaviour agreed in S085, including the P2 retrigger fixes;
- the cost of every oscillator option in instructions per sample.

**Read the costs as follows:**
- **Static counts** from `osccost.py`: the longest path through one
  compiled loop iteration, at an assumed one instruction per cycle.
- **They are not cycles.** Divides take several cycles and flash reads can
  stall. On hardware, all-`pwm` instruments reached 100 % CPU while these
  counts predicted far less (`S085_POST_MORTEM.md` §1).
- Use them to **rank** options and to size the relative change, not as
  absolute load.

---

## 1. Run-mode decisions

### 1.1 The three modes

| Mode | On a retrigger | Oscillators | Filter | Amp envelope |
|---|---|---|---|---|
| `rst` | **Exactly as before S085** (user: "`rst` always stays exactly as is") | The engine's usual phase resets. Cymbal resets the carrier and both modulators. | Drum clears it (`SVF_reset`) | `slopeEg2_trigger()`, unchanged; every ramp runs flat, so the output is bit-identical (`osc_s0`, `postchain`) |
| `fre` | Nothing resets | Every oscillator slews to its new pitch over 132 samples | Keeps ringing; cutoff and resonance slew 0.28 per block (≈ 2 ms) | Starts at the current gain; a one-block ramp if that gain is audible |
| `itp` | The carrier is snapshotted and restarted, and crossfades | Carrier: a 132-sample linear crossfade from the old copy. Modulators slew as in `fre`. | As `fre` | As `fre` |

### 1.2 Decisions and their reasons

1. **`rst` is untouchable.**
   - Every new path runs with flat endpoints in `rst`, so it is
     bit-identical to the pre-S085 engines.
   - The frozen pre-S085 source lives in `tools/dsp_test/frozen85/`.
2. **Pitch slew (`fre`, and the `itp` modulators).**
   - `osc_armSlew()` arms 132 samples.
   - Per block: step = (target − increment) / **max(samples left, 32)**.
     Dividing by fewer samples than the block applies overshot the target
     by 20.5 % of the interval (fault A1).
   - The glide is linear for 128 samples; the last 3 % lands in a fifth
     block. It never passes the target.
   - The per-sample slew add runs in every mode (step 0 in `rst`).
3. **`itp` crossfades the carrier only** (user decision Q14).
   - **Why:** a click is an amplitude step. In FM, a modulator jump changes
     the carrier's frequency, not its amplitude. So only the carrier reset
     needs hiding.
   - The modulators are never reset in `itp`. The old carrier copy is
     rendered against the **same live modulator block**, so it carries the
     un-retriggered FM tail.
   - The spike crossfaded the whole oscillator section, with 3 snapshots per
     engine and a 4-unit pool. S085 uses 1 snapshot per engine and 2 units.
   - **Accepted consequence:** in `itp`, Cymbal's FM attack differs from hit
     to hit, because its modulators are not reset.
4. **The render pool (Q14).**
   - 2 units per 32-sample block, system-wide. A unit is one extra
     oscillator render: an `itp` old carrier, or the second wave of a
     `wav`-type waveform interpolation.
   - Claim order: ongoing `itp` crossfades (never cut) → waveform
     interpolation → new `itp` crossfades, lowest slot first.
   - A refused `itp` trigger restores the carrier's phase and arms its slew,
     so it behaves as `fre`.
   - The pool is budgeted as **always full**.
5. **Filter slew in `fre`/`itp`** (Q15). Cutoff and resonance move toward
   their targets by 0.28 per block; `rst` keeps `SVF_recalcFreq()` exactly.
6. **Amp envelope on a `fre`/`itp` retrigger (S085 P2; user, 2026-10-27).**
   - **The fault it fixed:** `slopeEg2_trigger()` keeps the raw value but
     switches from the decay curve to the attack curve. Halfway down the
     decay the gain jumped by up to ±1.0, depending on the slope.
   - **The rule:** "check if the envelope is fully decayed and only
     short-ramp if there is something left."
     - The attack starts at the gain the voice is producing
       (`slopeEg2_retriggerFrom()`).
     - When that gain is audible (≥ 1/32768, one output step), the next
       block ramps from the previous block's gain and velocity to the new
       ones.
     - The ramp is **one block** (32 samples).
     - Drum's ramp now runs on the armed block even at attack 0.
     - Repeat mode keeps `slopeEg2_trigger()`; the armed ramp covers the
       jump.
7. **Drum FM depth on a `fre` retrigger (S085 P2 F-A; user approved).**
   - **The fault:** Drum's FM depth = FM amount × pitch envelope, and the
     pitch envelope restarts at 1.0. The depth jumped, which moved the
     carrier's read phase by modulator × Δdepth: a sporadic click at high
     FM amount.
   - **The fix:** on the armed block, the modulator block is scaled from
     old depth / new depth to 1 across the block.
   - The ramp is flat on every other block, under an active `itp`
     crossfade, and in `mix`/`snc`/`rng`.
8. **Still restarted on a `fre`/`itp` retrigger, by design:**
   - the transient sample;
   - the pitch envelope (Drum, Snare) and the snap envelope. These change
     frequency, not amplitude, and the oscillator slew glides them.
9. **Accepted limit (A8):** a retrigger less than 3 ms into an active
   crossfade re-snapshots the live carrier, so the output steps by
   (1 − t)·(old − new).

---

## 2. Which options add instructions, and how many

### 2.1 Oscillator type (per oscillator)

The baseline is the S085 table wave (Tri/Saw/Rec): **25** plain, **36** as an
FM carrier, **35** as an `snc` slave. Pre-S085 the table wave was 24 plain;
the +1 is the slew add.

| Type | Plain | Δ | FM carrier | Δ | `snc` slave | Δ |
|---|---:|---:|---:|---:|---:|---:|
| `wav` Tri/Saw/Rec | 25 | 0 | 36 | 0 | 35 | 0 |
| `wav` Sin | 24 | −1 | 31 | −5 | 34 | −1 |
| `wav` Noi | 16 + call | −9 | — | — | 20 + call | −15 |
| `wav` Cym | 31 | +6 | 39 | +3 | 37 | +2 |
| `wav` stock `Re3 Sw3 ReA TrA SwA` | 24 | −1 | 34 | −2 | 35 | 0 |
| `pwm` `Rec`/`Saw` | 51 | **+26** | 56 | **+20** | 63 | **+28** |
| `pwm` `Tri`/`Re3`/`Sw3` | 42 | +17 | 47 | +11 | 53 | +18 |
| `pwm` `Swp` seg 0 / 1 / 2 / 3 | 48 / 50 / **58** / 52 | +23 / +25 / **+33** / +27 | 53 / 55 / **62** / 57 | +17 / +19 / **+26** / +21 | 60 / 63 / **70** / 65 | +25 / +28 / **+35** / +30 |
| `smp` | 40 | +15 | 45 | +9 | 53 | +18 |
| `slp` | 47 | +22 | 53 | +17 | 61 | +26 |
| `wtb` 8-bit / 16-bit | 40 / 43 | +15 / +18 | 44 / 48 | +8 / +12 | 51 / 54 | +16 / +19 |

**Notes:**
- The `pwm`, `smp`/`slp` and `wtb` plain figures include a 2-instruction
  lane test. Their plain and sync renders share one loop to save ITCM.
- **Excluded from the counts:** flash wait states on stock-wave, sample
  and wavetable reads.
- `pwm` `Swp` segment 2 is the dearest loop in every column. It sets the
  per-voice worst cases below.

### 2.2 Run mode

| Option | Added cost | When it is paid |
|---|---|---|
| Per-sample slew add | +1 per sample per table/sine/crash/stock loop | every mode, always (constant) |
| Slew step division | 1 divide per oscillator per block | every mode, always |
| `fre` / `itp` filter slew | `SVF_recalcFreqFrom()` per voice per block (not measured separately) | `fre`/`itp` |
| `itp` crossfade | one pool unit: old carrier render ≤ 82 + blend 23 = **≤ 105** per sample | while a crossfade runs (≈ 5 blocks); budgeted always |
| Pool, system-wide | 2 units × ≤ 105 = **≤ 210** per sample | budgeted always |
| P2 amp/velocity ramp (post-chain) | Drum +3 (32→35), Snare +5 (30→35), Cymbal/HiHat +5 (25→30); system ≤ +30 | every mode, every block (flat endpoints except the armed block) |
| P2 gain match | one gain match + one divide | per `fre`/`itp` trigger only |
| F-A Drum FM depth ramp | **+15** | Drum `fm`, every block |

### 2.3 Drum mod mode (oscillator section, worst case)

| Mode | Composition | Worst |
|---|---|---:|
| `fm` | modulator 58 + carrier FM 62 + F-A 15 | 135 |
| `mix` | 58 + 58 + saturating add | 116 + add |
| `snc` | lane 18 (sample master; 11 for a periodic master) + master 58 + slave 70 + mix 16 | **162** |
| `rng` | 58 + 58 + ring law 19 | 135 |

**`snc` extras:**
- the slave pays +10 to +16 over its plain loop, whatever the number of
  wraps;
- the mix pass is 28 instead of 16 when an `itp` copy also runs.

### 2.4 Per voice and system (oscillator section only)

| Voice | Pre-S085 | S085 worst | Notes |
|---|---:|---:|---|
| Cymbal / HiHat | 98 | **178** + add | 2 × 58 (`Swp` modulators) + 62 (`Swp` FM carrier) |
| Cymbal / HiHat, `pwm` excluded on the modulators | 98 | 156 + add (derived) | 2 × 47 (`slp` modulators) + 62 |
| Cymbal / HiHat, all `wav` types | 98 | 101 + add (derived) | 2 × 31 + 39: only the slew adds |
| Drum `fm` | 68 (derived) | 135 | |
| Drum `snc` | — | 162 | new mode |
| Snare | 46 (derived) | 74 | 58 + noise 16 |
| Pool | 2 × ≤ 35 | ≤ 210 | system-wide |

**System:**
- **S085 static worst case:** 6 voice slots × 178 + pool 210 ≈ **1,278
  instructions per sample**. That is ≈ 26 % of the 4,897-cycle sample
  period at one instruction per cycle.
- **Pre-S085:** the audit estimated all oscillators at 200–300 cycles per
  frame, inside a total audio load of ≈ 34–49 % (±50 %).
- **Hardware:** all-`pwm` instruments reach 100 % CPU.

**Sources:**
- `INSTRUMENTS_DSP_REFERENCE.md` §12.11;
- `CPU_USE_DSP_AUDIT.md` "S085 oscillator cost record";
- `armcheck-osc` output at the end of S085.

The "derived" rows are compositions of the measured per-loop figures, not
separate measurements.
