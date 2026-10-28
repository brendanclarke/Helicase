# S086 P4 — `pos` on the Cym sample (thin plan)

**Session:** 086, phase P4 (follows `S086_P3_WAV_OSC_POS_PARAMETER.md`, Q2)
**Written:** 2026-10-28
**Status:** reviewed (user, 2026-10-28). Q1 is decided (`pos`/128); Q2–Q4
follow the recommendations unless changed. No code yet.

---

## 0. Summary

**What:** `pos` on `Cym` (the built-in crash sample, `wav` 5) sets where
in the sample the oscillator plays from, as a fraction of the sample's
length.

**Rule:** start point = `pos` × length / 128.
- `pos` 0 plays from the start, as today.
- `pos` 64 plays from the middle.
- `pos` 127 plays only the last 1/128 of the sample before it loops
  (decided, Q1).

**Continuous, as with the stock waves:**
- A change of `pos` while it plays (knob, LFO, velocity, automation,
  Morph) moves the play position by the change.
- The move is spread over the next audio block by the same per-block bend
  as P3 §2.3, so the position glides and never jumps.
- An LFO on `pos` scrubs through the sample smoothly.

**Cost:** essentially none.
- `Cym` already plays the whole sample over the whole 32-bit phase, so
  "a fraction of the sample" is the same phase arithmetic as "a fraction of
  a cycle".
- Adding `CRASH` to P3's `osc_positionIsPhase()` is the whole firmware
  change.
- No loop changes, no new state, no RAM.

---

## 1. Why `Cym` fits the P3 rule exactly

**How the crash sample is read** (`calcSampleOscBlock()`, `Oscillator.c`):
- `crashSample[]` is 32,768 8-bit points (`Samples.c`).
- The phase is a `<< 17` point index: `itg = phase >> 17`, and the
  remaining 17 bits are the interpolation fraction.
- 32,768 points × 2¹⁷ = 2³². **The whole sample is exactly the full
  32-bit phase range,** and the phase wraps at its end with no test. `Cym`
  is a loop over the full sample.
- `osc_calcSampleFreqValue()` sets the speed (1,024 points per nominal
  period, so the sample spans 32 nominal periods). It does not change how
  the phase maps onto the sample.

**Therefore `pos << 25` = `pos` × 256 points = `pos` / 128 of the sample.**
It is the same operation as on the stock waves:

| | Stock waves (P3) | `Cym` (P4) |
|---|---|---|
| full phase range 2³² | one cycle | the whole sample (32,768 points) |
| `pos << 25` | `pos`/128 of a cycle | `pos`/128 of the sample (`pos` × 256 points) |
| at a trigger | start phase + `pos << 25` | today's start (0) + `pos << 25` |
| while playing | the per-block bend `d << 20` | the same |
| circular difference (127 → 0 = +1 step) | phase wraps | the sample loops, so the same |

**What you hear:**
- **At a trigger:** the crash starts later in its decay; at high `pos` it
  starts in the quiet tail.
- **A slow change while it plays:** a smooth jump forward or back along the
  sample, heard as a brief speed-up or slow-down: a scrub.
- **One step of `pos`:** 256 points spread over 32 output samples. During
  that block the sample is read about 8× faster than nominal, on top of its
  normal speed.
- **An LFO on `pos`:** continuous scrubbing (varispeed around the playing
  position), smooth, with no clicks from the 7-bit steps.
- **A large jump** (for example 64 at once) is a 0.7 ms burst through half
  the sample: a short noise smear. The crash is not band-limited, so a fast
  bend aliases. P3 Q11's bend cap turns it into a slower glide.

---

## 2. Modes

| Context | `pos` on `Cym` |
|---|---|
| plain, Snare `osc1`, Cymbal/HiHat `osc1` | trigger + bend |
| Drum `fm` / `mix` / `rng` (carrier) | trigger + bend (`calcSampleOscFmBlock`, `calcSampleOscRingBlock` read the same phase) |
| Drum `snc` (slave; crash has a sync copy) | trigger only, no bend (P3 Q12) |
| `Cym` as the Drum FM-page **master** in `snc` | not affected: `pos` is an `osc1` row; the crash master remains "one reset per nominal period" (S085) |
| waveform interpolation with `Cym` as the base (`Cym → Re3`) | both partners share the phase, so both are shifted and bent; the base decides (`osc_positionIsPhase(CRASH)` = 1) |

---

## 3. Changes

1. **`Oscillator.h`:** `osc_positionIsPhase()` returns 1 for
   `SINE`, `CRASH` and the table waves. Update its comment.
2. **Nothing else.** P3's trigger line and per-block bend already cover it,
   and the engines start `Cym` at phase 0 today, so `pos` 0 is unchanged.

**Recommended:** implement P4 together with P3. It is one condition, and
P3's tests then cover both (Q3).

---

## 4. Tests (additions to P3 §7)

| Check | What |
|---|---|
| P1 (extended) | `osc_positionTrigger()` returns `pos << 25` for `CRASH` |
| P3 (extended) | the no-drift test also runs `calcSampleOscBlock`: after random `pos` sequences, the phase equals the free phase + `pos_final << 25` exactly; `phaseInc` is restored |
| P6 (changed) | `Cym` now bends; `Noi` and user samples still do not |
| Break proof | `CRASH` removed from the helper fails P1 and P3 |

**Hardware:**

| # | Check | Expect |
|---|---|---|
| C1 | `Cym` on each instrument, `pos` 0 | identical to before |
| C2 | `pos` 32 / 64 / 127 between hits | the hit starts a quarter / half / 127/128 of the way through the sample |
| C3 | slow LFO → `pos`, then a faster one | smooth scrubbing, no clicks; faster LFO = stronger smear |
| C4 | knob jump 0 → 64 while sounding | a short smear (decides P3 Q11's cap for samples) |

**A pre-existing issue, not caused by P4:** `crashSample[]` is declared
with 32,768 entries, but `calcSampleOscBlock()` reads `crashSample[itg + 1]`,
which at `itg` 32,767 is one byte past the end. The S085 host test pads its
copy to 32,769 for this reason. Since `pos` can now place the phase there
deliberately, add the guard byte (`crashSample[32769]`, last byte = first
byte) in the same change (Q4).

---

## 5. Questions

| # | Question | Recommendation |
|---|---|---|
| **Q1** | Scale `pos`/128 of the length (127 plays the last 1/128 of the sample), not `pos`/127 (where 127 would be the very end)? | **Decided (user, 2026-10-28): `/128`.** It is the same as the stock waves, 64 is exactly the middle, and every value is a distinct start. With `/127`, 127 would be the end of a looping sample, which is the start again (same as 0) |
| **Q2** | User samples (`wav` 8+): include them now? | **No: S2.** The v1 user samples are replaced by `smp`/`slp` in S2. The rule is worked out for S2: start = (`pos` × size) << 10; per-sample bend = d × size × 32, exact in the `<< 17` units. But one-shot samples need a block-level clamp at both ends: a backward bend near the start must not wrap the phase past 0, because the one-shot reader would then index far beyond the sample |
| **Q3** | Implement P4 with P3 (one condition in the helper)? | **Yes** |
| **Q4** | Add the crash guard byte (`crashSample[32769]`, wrap value) in the same change? | **Yes**: one byte of flash, and it removes a one-past-the-end read that `pos` would make reachable on purpose |
