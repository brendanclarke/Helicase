# S085 post-mortem: what failed

**Status:** S085 is being rolled back. On hardware, the CPU reaches 100 %
and the interface bogs down when every instrument uses the `pwm`
oscillator type.

**Companion documents:**
- `S085_POST_MORTEM_MENU.md` records the menu layout decisions.
- `S085_POST_MORTEM_OSC_RUN.md` records the run-mode decisions and the
  instruction cost of every option.

All figures below come from the S085 tree before the rollback:
- `CPU_USE_DSP_AUDIT.md` "S085 oscillator cost record";
- `INSTRUMENTS_DSP_REFERENCE.md` §12.11;
- `085_SESSION_HANDOFF_LOG.md`;
- `S085_P2_OSC_RESET_FOLLOWUP.md`.

---

## 1. The failure that ended the session: CPU

### 1.1 What happened

Setting every instrument's oscillators to `pwm` drives the CPU to 100 %, and
the interface stalls.

Earlier the same day a Cymbal/HiHat FM (modulator) oscillator set to `pwm`
"crashed". No code fault was found. The most likely cause is the same
overload: three `pwm` oscillators in one voice. This was not confirmed on
hardware.

### 1.2 The requirement that was not met

The user's rule, given during S085 planning: what matters is **peak usage
under the worst-case settings**. A saving under some particular setting does
not count. The oscillator types and modes had to stay within the
worst-case budget.

### 1.3 Why the budget did not hold

| # | Fault | Detail |
|---|---|---|
| 1 | **The budget was never measured on hardware** | Every S085 cost figure is a static instruction count from `osccost.py`: the longest path through each compiled loop, at an assumed one instruction per cycle. The audit itself says the counts are not cycles: `VDIV`/`UDIV` take several cycles, flash reads can miss the D-cache, and the pre-S085 estimates are ±50 %. The one test that measures real load, hardware row **H11** (the worst-case Scene with the DEV render-overrun counter), was left to the end of the session as a user task and never gated the work. |
| 2 | **There was no cap against the whole frame** | The plan only recorded the increase ("≈ +115 instructions per voice", "roughly 14 % of a 4,897-cycle frame at one instruction per cycle") and passed it to S086 to budget around. Nothing compared the new oscillator worst case with the frame's remaining headroom. The only cap in the plan, "FM carrier + 10", applied to `snc` alone. |
| 3 | **The oscillator worst case grew about five-fold** | The pre-S085 audit estimates all oscillators together at 200–300 cycles per frame, with total audio work at about 34–49 % of the frame (±50 %). The S085 static worst case is 6 voices × 178 + a 210-instruction pool ≈ **1,278 instructions per frame**, before stalls. |
| 4 | **`pwm` is the most expensive type, and Cymbal/HiHat run three oscillators** | The `pwm` loops cost 42–58 instructions per sample (plain), against 25 for a table wave. A Cymbal/HiHat voice with three `pwm` oscillators is the 178 worst case, against 98 before S085. |
| 5 | **The render pool is extra load** | Up to two extra oscillator renders per block (`itp` crossfade copies and waveform interpolation), budgeted at ≤ 210 per sample on top of the voices. |
| 6 | **Cost estimates were wrong** | The F-A Drum FM depth ramp was estimated at ≈ 4 instructions per sample; it measured 15. The first `snc` design called the renderer once per master wrap, which happens on every sample at high notes. The assessment caught that one (A2) and replaced it with a constant-cost loop. |

### 1.4 Per-voice worst cases (static instructions per sample, oscillator section only)

| Voice | Pre-S085 | S085 | Change |
|---|---:|---:|---:|
| Cymbal / HiHat | 98 | 178 (+ saturating add) | +80 |
| Drum `fm` | 68 (derived: crash 30 + crash FM 38) | 135 (incl. F-A 15) | +67 |
| Drum `snc` | — (new mode) | 162 | — |
| Drum `rng` | — (new mode) | 135 | — |
| Drum `mix` | 60 + add (derived) | 116 + add | +56 |
| Snare | 46 (derived: crash 30 + noise 16) | 74 | +28 |
| Pool (system-wide) | 2 interpolation renders ≤ FM 35 each | ≤ 210 | — |

The "derived" values use the pre-S085 column of the per-loop table. The
Cymbal 98 matches the audit's own figure (2 × 30 + 38).

---

## 2. Other failures during S085

They were found and fixed during the session, but each one is a place where
the first implementation was wrong.

| # | Component | Failure | Fix in S085 |
|---|---|---|---|
| 1 | Run modes `fre`/`itp` | Retriggers still clicked badly. The amp envelope restarted with `slopeEg2_trigger()`, which switches from the decay curve to the attack curve: up to a full-scale gain step. Drum ramped the step except at attack 0; Snare, Cymbal and HiHat did not ramp it at all. | P2: the attack starts at the current gain (`slopeEg2_retriggerFrom()`), with a one-block ramp when the gain is audible. |
| 2 | Drum FM in `fre` | Sporadic clicks at high FM amount. The FM depth follows the pitch envelope, which restarts at 1.0, so the carrier's read phase jumped. | F-A: one-block ramp of the modulator depth (+15 instructions per sample). |
| 3 | Pitch slew | The last block divided by the samples left (4), not by the block length, and overshot the target by 20.5 % of the interval (A1). | The divisor is max(left, 32). |
| 4 | Hard sync `snc` | One renderer call per master wrap. A sample master wraps on every sample, so the worst case exceeded the cap (A2). | A per-block wrap lane and one loop per slave family. |
| 5 | ITCM | The separate sync copies of every loop filled ITCM: 16,272 of 16,384 B. | `pwm`/`smp`/`wtb` share one plain/sync loop (`OSC_SHARED_LOOP`); 12,432 B. |
| 6 | RAM | The F-A field took the Drum struct to 844 B, over the approved +8 B. | An unused field was removed; 840 B. |
| 7 | Row layout | The selection (pwm family, sample, table) sat on a non-morphable `src` row and the continuous control on `wav`. The user had them swapped, and `src` renamed `mod`, at the end of the session. | Swapped at the field level (`S085_POST_MORTEM_MENU.md`). |
| 8 | Menu names | The stock-wave and pwm-family names were inconsistent (`sq3`, `prc`, …). | Renamed (`S085_POST_MORTEM_MENU.md`). |
| 9 | Cymbal/HiHat `pwm` modulators | They "crashed" (§1.1). | The user excluded `pwm` from those oscillators. No code fault was found. |
| 10 | Analysis | Wrong statements to the user: that the L1 caches were off (both are on); that Drum skips the retrigger ramp "during attack" (only at attack 0); that the pitch envelope restarts with no slew under `fre` (it glides). | Corrected in the session. |

---

## 3. Open at rollback

- **"User wavetables don't loop"** (user report, end of session). Nothing
  in the code was found to explain it:
  - the `wtb` renderer wraps every cycle, and the point after the last reads
    point 0;
  - `wtb` has no end test;
  - the `ak_hvox` card files are seamless single-cycle frames.

  The symptom was not described further.
- **The hardware matrix was never run:** S085 rows H1–H11 and P2-H1 to
  P2-H14. H11 is the CPU row.
- **A pwm Drum FM oscillator** was not reported to crash, but it has the
  same cost profile.

---

## 4. Requirements for any re-implementation

These follow directly from §1.

1. **Measure before accepting.** Every oscillator type, mode and pool unit
   is accepted only after the worst-case Scene is measured on hardware with
   the DEV render-overrun counter and the `cpu` figure:
   - all six voices at their dearest type and mode;
   - both pool units in use.

   A static instruction count is not a gate.
2. **Cap the whole frame.** The oscillator section's worst case is set as a
   number of cycles per frame, inside the frame's measured headroom. Each
   type must fit under that cap in the voice that runs the most oscillators
   (Cymbal/HiHat: three).
3. **Restrict types per oscillator where needed.** A type that only fits
   on one oscillator per voice is offered only there. The `pwm`-on-modulators
   exclusion is the example.
4. **One component at a time,** each with its own hardware CPU check:
   types, then run modes, then mod modes, then the pool. The cost of each
   option is in `S085_POST_MORTEM_OSC_RUN.md`.
