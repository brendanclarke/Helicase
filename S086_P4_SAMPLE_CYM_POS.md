# S086 P4 — `pos` on the Cym sample (thin plan)

**Session:** 086, phase P4 (follows `S086_P3_WAV_OSC_POS_PARAMETER.md`, Q2)
**Written:** 2026-10-28
**Status:** **implemented with P3 and passed on hardware (user, 2026-10-28)**;
record `S086_P3_IMPLEMENTATION.md` (C1-C5, §8-§9). Q1-Q4 decided as below.
§6 lists the architectural decisions `pos` on samples still needs (for S2).

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
| **Q2** | User samples (`wav` 8+): include them now? | **Decided: no, S2.** The v1 user samples are replaced by `smp`/`slp` in S2. *(Revised 2026-10-28: the rule first written here used the v1 `<< 17` phase; S2 uses the Q20.12 phase of S086 P1 §4.2. See §6 D1.)* |
| **Q3** | Implement P4 with P3 (one condition in the helper)? | **Decided: yes** (done) |
| **Q4** | Add the crash guard byte (`crashSample[32769]`, wrap value) in the same change? | **Decided: yes** (done) |

---

## 6. Open architectural decisions: `pos` on samples (for S2)

**`Cym` needs no further decisions:** it is implemented and passed. The
crash sample is exactly one full 2³² phase range and loops, so it behaves
like a periodic wave.

**Real samples (`smp`/`slp`, S2) do not have those properties:**
- arbitrary length;
- a one-shot end;
- a loop region that is not the whole range.

So the P3 rule cannot be reused unchanged. These decisions belong in the S2
plan, before any sample code. Each has a recommendation.

| # | Decision | Why it is open | Recommendation |
|---|---|---|---|
| **D1** | **Phase format and arithmetic.** The S2 reader uses Q20.12 (S086 P1 §4.2; the spike's `OscillatorSample.c`), not the v1 `<< 17` units. | P4 Q2's first rule used the v1 units, which also cap samples at 32,768 points. | Start = ((`pos` × len) >> 7) << 12. One step held over one 32-sample block is exactly **d × len** per sample (len/128 points × 2¹² / 32). It is exact for every length up to 2²⁰ points, and \|d × len\| ≤ 2²⁶ fits `int32`. Record it in the S2 plan. |
| **D2** | **Which length `pos` refers to in `slp`.** The spike loops from the `pos` start to the end, so its loop moves with `pos`. S086 P1 Q7 recommends a fixed loop region. | With continuous `pos`, a moving loop region shifts on every LFO step. | **Fixed loop region** (P1 Q7). `pos` is a fraction of the **whole sample**. A start before the loop plays the lead-in, then loops. A bend moves the playhead and then wraps it into the loop region. |
| **D3** | **Circular or linear difference.** P3 treats 127 → 0 as +1 step, because a phase wraps. | A one-shot is not periodic: 127 → 0 must be a jump back of almost the whole sample, not 1/128 forward past the end. | **Per type:** circular for periodic content (stock waves, `Cym`); **linear** (−127..+127) for `smp` and `slp`, followed by the edge rule (D4) and the loop wrap (D2). The `typ`/waveform chooses the rule inside the apply helper. |
| **D4** | **One-shot edges.** Behaviour when a bend crosses the end or the start, and whether a finished one-shot can be revived by moving `pos` back. | The v1 reader stops advancing at the end. A backward bend near 0 must not wrap the phase into an out-of-range index. | Clamp at the start (0). Moving past the end ends the hit early. **A finished one-shot stays finished until the next trigger** (no revival by `pos`). The clamp is applied to the block's total bend, at block level. |
| **D5** | **Bend size scales with sample length.** One `pos` step is len/128 points. | For a 1 s sample one step plays 344 points in 32 samples (≈ 11× speed); for 10 s, ≈ 108×. An LFO on a long sample becomes a fast smear that also aliases (samples are not band-limited). | **A time-based cap for samples** (points per block, e.g. ≤ 4× playback speed), separate from P3's phase cap. A larger change glides over several blocks. Tune by ear in S2 hardware tests. |
| **D6** | **Step automation must be exact at the hit** (P3 Q14). Today the trigger fires before the step's values are drained, so the hit starts at the base `pos` and glides within 0.7 ms. | Inaudible for a phase offset (hardware passed). For samples, **per-step start positions (slicing)** are the main use; each slice would start at the wrong place and smear. | **Fix the ordering before S2's `smp`**, as its own small step: a step's own automation must land before its trigger (drain first, or let the trigger consume the voice's pending values). It affects every trigger-time parameter, so it needs its own plan and tests. |
| **D7** | **Re-anchoring on a type or source change.** `positionApplied` means a phase offset for `wav`, a point offset for `smp`/`slp`, and nothing for `wtb` (`pos` is the frame position there). | A `typ`, waveform or sample change in the middle of a hit could release a stale bend in the wrong units. | **Rule:** any change of `typ`, waveform family or sample re-anchors: `positionApplied = position`, no bend that block. Today's non-phase waves already consume the delta, which is the same rule. |
| **D8** | **No live state across the render** (assessment F1). | The sample bend needs the length factor at restore. | Keep the step in `positionBendStep` (`int8_t`, already there) and recompute step × len at restore from `sampleCacheSize`, which cannot change inside a block. No new field, nothing live across the render. Gate with `elf86_mixer_check`. |
| **D9** | **Sync slave reset target.** The spike restarts a synced sample at its start + overshoot. | With continuous `pos`, "start" could mean the trigger start or the current offset. | The reset target is the **`pos` start point** (as at a trigger). No bend in `snc` (P3 Q12), so the two agree. Decide with the S2 sync master rules (S086 P1 §2.2). |
| **D10** | **Modulators that never reset** (Drum FM, HiHat; P3 Q10, P1 Q5). | For `smp` on those oscillators the trigger start never happens. | Decide together with `typ` and the modulator `pos` rows (the next step), not in S2. |

**Order:**
1. D6 (automation order): a small standalone step.
2. D10, with the `typ` rows.
3. D1–D5 and D7–D9 in the S2 plan.
