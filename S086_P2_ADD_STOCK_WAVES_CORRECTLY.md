# S086 P2 — Add the five stock waves as fixed tables (plan)

**Session:** 086, phase P2 (first implementation step of stage S1 in
`S086_P1_OSC_TYPE_EXPANSION.md`)
**Branch:** `dev-ph7-voice_expansion`, at `e56bb957` (the S085 close-out).
The tracked tree is clean.
**Written:** 2026-10-27
**Status:** approved, then implemented for **`Re3` and `Sw3` only**
(user, 2026-10-27). Those two are `wav` 6–7, and `OSC_SAMPLE_START` is 8.
`ReA`/`TrA`/`SwA` come later. The record is `S086_P2_IMPLEMENTATION.md`,
including the C1 finding that only `sawTable` follows the measured taper.
**Replaces:** `S086_P2_ADD_STOCK_WAVES.md`, which is withdrawn (§1).

---

## 0. Summary

- **Goal:** add `Re3 Sw3 ReA TrA SwA` to the `wav` list after
  `Sin Tri Saw Rec Noi Cym`, as `wav` values 6–10.
- **Method:** add them the same way as `Tri`, `Saw` and `Rec`.
  - Each wave is a `const int16_t xxxTable[11][1024]` in program flash: 11
    band-limited octave levels of 1,024 points.
  - The tables are generated offline from the spike's harmonic recipe by a
    Python script, and the generated `.c` file is committed.
  - The existing table loops play them: `calcWavetableOscBlock`,
    `calcFmBlock`, `calcWavetableOscSyncBlock` and
    `calcWavetableOscRingBlock`. The dispatchers pass the new table pointer.
- **What this step does not have:**
  - no install step, no sample-flash block, no recipe tag, no fallback;
  - no new oscillator loops, so no new ITCM section;
  - no change to `SampleMemory` or `filesystem.c`.
- **Cost:**
  - **CPU:** the same loops at the same cost as `Tri`/`Saw`/`Rec` (24
    plain, 35 FM, 34/39 sync, 31 ring, static).
  - **Program flash:** 5 × 11 × 1,024 × 2 B = **112,640 B**. Headroom
    falls from 139,704 B to about **26 KB** (§5). This is the main decision
    for review (Q1).

---

## 1. What went wrong in the first P2

`S086_P2_ADD_STOCK_WAVES.md` followed plan §4.1 (and S085 Q6 (a)) instead of
the existing stock-wave design. It:

- generated the waves at **Load:[Samples]** from about 10 KB of recipes into
  a 61 KB block at the sample-flash floor;
- added a recipe tag, a validity flag and fallback tables (the waves sounded
  like Rec/Saw/Tri until an install);
- moved the v1 sample data floor, so existing user samples were rejected
  until reinstalled;
- added four new reader loops with a variable shift and mask (the sync copy
  was +2/+3), and a new `.itcm.s086` section.

**Why it was wrong:** none of that is how `Tri`/`Saw`/`Rec` work. They are
fixed tables, always present, and read by one set of loops. The new waves
must behave the same way: available at power-on, the same cost, no install
dependency.

**State of its files now:** its changes to tracked files have been reverted,
so the tracked tree is at `e56bb957`. Its new files are still in the tree,
untracked. §6 step 1 lists which to delete and which to keep.

---

## 2. How the five waves were generated (spike review)

### 2.1 Tools (copied to `tools/plots/`)

The tools were copied from `Helicase-dev-ph7-spike/tools/plots/` (`26fb7c3`)
without `__pycache__/`.

- **Check:** `diff -r` against the spike shows the copies are identical.
  The only change is a provenance note added to `README.md` today.
- **Re-run check:** `plot_stock_waves.py` was re-run in a scratch copy
  today. It reproduces `stock_wave_harmonics.csv` byte for byte.
- **Requirements:** Python 3 with `numpy`, `matplotlib` and `pillow`. All
  three are present on this machine. They are not in `requirements.txt`.

| File | Role |
|---|---|
| `plot_stock_waves.py` | builds the five waves; writes `stock_wave_harmonics.csv` (the recipe) and `stock_waves_303_analog.png` (the review plot) |
| `lxr_plotlib.py` | the reference-image digitisers, harmonic analysis and synthesis (`harmonics_of`, `from_harmonics`, `extend_harmonics`), plot style |
| `reference/303_square_saw.png`, `reference/tra_dirty_triangle.png` | the user's reference captures (2026-10-25) |
| `stock_wave_harmonics.csv` | per wave, harmonics 1–511: magnitude and phase, normalised so the full 511-harmonic cycle peaks at 1.0 |
| `plot_wavetable_formats.py` | wavetable format comparison (`--stock` prints the stock layouts); S3 reference |
| `plot_pwm.py`, `plot_current_drive.py`, `plot_waveshapers.py` and their PNGs | later stages (S5, effects); kept for reference only |

### 2.2 The recipe for each wave (`plot_stock_waves.py`)

| Wave | Code | Source | Harmonics |
|---|---|---|---|
| `Re3` | `SQ3` | the 303 square, digitised from `reference/303_square_saw.png` (left panel) | 1–40 measured, then extended to 511 with a fitted power-law roll-off (never brighter than 1/k) and a continued linear phase trend |
| `Sw3` | `SW3` | the 303 saw, the same image (right panel) | 1–40 measured, then extended |
| `ReA` | `SQA` | a modelled 50 % pulse through a 2-pole low-pass (40th harmonic, Q 0.9) and a 1-pole high-pass (0.1 × f0): rounded edges, a slight overshoot, a droop on the tops | 1–511 from the model |
| `TrA` | `TRA` | the dirty triangle, digitised from `reference/tra_dirty_triangle.png` | 1–30 measured, then extended |
| `SwA` | `SWA` | a falling ramp with exponential (capacitor) curvature and a 1.5 %-of-cycle reset, through the same chain (LP at the 48th harmonic, Q 0.75; HP at 0.08 × f0) | 1–511 from the model |

**How a trace is digitised:** the digitiser finds the trace pixels per
column, measures the period by autocorrelation, and averages all full cycles
into one cycle.

**Orientation** matches the stock tables:
- the saws fall, and index 0 is the middle of the reset jump;
- the squares are high for the first half;
- the triangle starts at its trough.

### 2.3 How the spike turned the recipe into octave levels, and why this plan does not copy it

- `tools/gen_stock_recipes.py` used **brick-wall** levels: harmonics
  1..`LEVEL_H` at full amplitude, with `LEVEL_H` = 511, 511, 337, 168, 84,
  42, 21, 10, 5, 2, 1.
- Its level lengths shrank above level 4 (512 … 16 points). The 6,112
  points per wave were made for the install-time generator.

**The existing tables are built differently.** I analysed
`sawTable`/`recTable`/`triTable` in `Core/DSPAudio/wavetable.c` (FFT per
level):

- every level is 1,024 points with a peak of exactly 32,767;
- the band limit is a smooth **raised-cosine (Hann) taper**, not a brick
  wall. Saw, gain against the ideal 1/k, as a fraction of the level's
  Nyquist harmonic count H:

| Level (H) | 0.25 H | 0.5 H | 0.75 H | 0.9 H | 1.0 H |
|---|---:|---:|---:|---:|---:|
| 0 (511) | −0.5 dB | −2.1 | −6.2 | −12.8 | −46 |
| 2 (337) | −1.3 | −5.9 | −16.5 | −31.6 | −75 |
| 5 (42) | −1.0 | −5.4 | −15.9 | −29.2 | −57 |
| 8 (5) | 0.0 | −0.9 | −9.2 | −9.2 | −20 |
| Hann, for comparison | −1.4 | −6.0 | −16.7 | −32.1 | −∞ |

- From level 2 to level 5, the best-fit Hann cutoff is H + 1.75 (338.75,
  169.75, 85.75, 43.75).
- Levels 0 and 1 use a wider taper (the 512-bin limit truncates it).
- Levels 6–10 are coarse, because only a few harmonics remain.

**Consequence:** brick-wall levels would make the new waves brighter than
`Rec`/`Saw` near the top of each octave. They would also make each octave
switch more audible, and make a `Rec ↔ Re3` interpolation change brightness
as well as shape.

**This plan therefore generates the new tables with the band limit of the
existing tables** (§3.2).

---

## 3. Design

### 3.1 IDs, names and constants (`Oscillator.h`, `MenuText.h`)

| `wav` | Code | Display | Table |
|---:|---|---|---|
| 0–5 | `SINE TRI SAW REC NOISE CRASH` | `Sin Tri Saw Rec Noi Cym` | unchanged |
| 6 | `SQ3` | `Re3` | `sq3Table` |
| 7 | `SW3` | `Sw3` | `sw3Table` |
| 8 | `SQA` | `ReA` | `sqaTable` |
| 9 | `TRA` | `TrA` | `traTable` |
| 10 | `SWA` | `SwA` | `swaTable` |
| 11+ | user samples | `s01…` | `OSC_SAMPLE_START` 6 → **11** |

- `waveformNames` gets 11 entries.
- The menu's sample tail, its maximum and the sample names all key off
  `waveformNames[0][0]`, so they follow automatically. So do
  `modulationNode.c`'s `maxWave` and the waveform-interpolation range (both
  use `OSC_SAMPLE_START`).
- **One new inline helper in `Oscillator.h`:** `osc_isTableWave(w)`. It is
  1 for `TRI SAW REC` and `SQ3…SWA`.

### 3.2 The tables (generated, committed)

**New tool `tools/gen_stock_tables.py`** (stdlib + numpy).

**Inputs:**
- `tools/plots/stock_wave_harmonics.csv`;
- `Core/DSPAudio/wavetable.c` (the existing tables, read only).

**Output:** `Core/DSPAudio/wavetableStock.c` (generated; "do not edit"). Its
header records the CSV's SHA-256 and the command line.

**Method:**
1. Read the recipe. Use the spike's convention, which was checked against
   the device orientation in the first P2's `stock_gen` test:
   `a_k = 2·mag·cos φ`, `b_k = −2·mag·sin φ`, and
   `x(m) = Σ a_k cos(2πkm/1024) + b_k sin(2πkm/1024)`.
2. **The band limit per level, measured from `sawTable`:**
   `g_lv(k) = |SAW_lv(k)| · k / |SAW_lv(1)|`, for k = 1..512.
   - Values below −80 dB are set to 0 (the int16 floor).
   - The saw has every harmonic, so this curve is the existing tables'
     taper, by construction.
3. **Per level:** `x_lv(m) = Σ_k g_lv(k)·(a_k cos + b_k sin)`, evaluated
   in float64 at 1,024 points. Scale it to a peak of 32,767 (as every
   existing level is), round, and clip to int16.
4. Write `const int16_t sq3Table[11][1024]` and the other four, in
   `wavetable.c`'s layout and comment style (`//---- Table N ----`, MIDI
   note and frequency).

**Built-in checks (the script exits non-zero on failure):**
- **C1, calibration:** rebuild `sawTable`, `recTable` and `triTable` from
  their ideal harmonic series (1/k all k; 1/k odd k; 1/k² odd k with
  alternating sign) through the same `g_lv`. Report the per-level max
  absolute error in LSB.
  - The target is the int16/rounding floor at levels 0–5.
  - Levels 6–10 are reported. If they differ, the step-2 curve is still the
    one applied. A tighter rule, if found, is documented and used only if
    the user agrees.
- **C2:** every new level has a peak of 32,767 and DC below 1 LSB.
- **C3:** no energy above the level's Nyquist harmonic beyond what
  `sawTable` itself has.
- **C4:** `--check` regenerates into `build/` and `cmp`s the committed file.

**New plot `tools/plots/plot_stock_tables.py`:** it reads the generated
`.c`. For each new wave it plots levels 0, 4, 8 and 10 beside its stock
counterpart (Rec/Saw/Tri), and the per-level spectra. Output:
`stock_tables_levels.png`. This is the review picture for the levels the
device will play.

**Declarations:** five `extern const int16_t …[11][1024];` lines in
`wavetable.h`, after `triTable`.

**Build:** `Makefile` adds `Core/DSPAudio/wavetableStock.c` beside
`wavetable.c`, with the same flags. The tables land in `.rodata`
(program flash), as the existing tables do.

### 3.3 Oscillator dispatch (`Oscillator.c`)

**No loop is added or edited.**

Each dispatcher gets one case group for the five waves, with a
**non-constant** table pointer from a small const pointer array:

```c
static const int16_t (* const osc_stockTables[5])[1024] =
    { sq3Table, sw3Table, sqaTable, traTable, swaTable };
...
case SQ3: case SW3: case SQA: case TRA: case SWA:
    calcWavetableOscBlock(osc, osc_stockTables[osc->waveform - SQ3], buf, size, gain);
    break;
```

**Why one case group with a variable pointer:**
- Rule §1.3.3 forbids adding constant-argument call sites. Five constant
  cases would invite LTO to make five more specialised or inlined copies
  (ITCM) and to re-specialise the existing ones.
- One variable-pointer site adds at most one generic copy per dispatcher.
  The `Tri`/`Saw`/`Rec` call sites are untouched.

| Function (line today) | Change |
|---|---|
| `calcNextOscSampleFmBlock` (449) | stock case → `calcFmBlock` |
| `calcNextOscSampleBlock` (584) | stock case → `calcWavetableOscBlock` |
| `osc_renderPlainWave` (1085) | stock case → `calcWavetableOscBlock(…, 1.f)` |
| `osc_renderSyncWave` (1280) | stock case → `calcWavetableOscSyncBlock` |
| `calcNextOscSampleRingBlock` (1656) | stock case → `calcWavetableOscRingBlock` |
| `osc_setFreq` (1726) | stock case → `osc_calcWavetableFreqValue` (the `<<22` increment and the octave level) |
| `osc_syncWaveform` (1058) | also 1 for `SQ3…SWA`, so an interpolated stock slave takes the synced phase rule |
| `osc_syncClockInc` (1123) | `>= CRASH` → `== CRASH \|\| >= OSC_SAMPLE_START` |

**`osc_syncClockInc` (row above):** without this change, a stock master
would be treated as a `<<17` sample clock, which resets 32× too often. The
first P2 found this, and the fix is kept.

**Not touched:**
- `calcFm()` and `calcNextOscSample()` (the per-sample versions). Nothing
  calls them; grep finds no callers.
- `calcPeriodicInterp*`. These render their partners through the
  dispatchers above, so `Rec ↔ Re3` and `Cym → Re3` interpolate with no
  extra code.

### 3.4 Engines: trigger start phase (one line each)

`DrumVoice.c:209`, `Snare.c:147`, `CymbalVoice.c:155` and `HiHat.c:169`:

```c
waveform > SINE && waveform <= REC
```

becomes

```c
osc_isTableWave(waveform)
```

The new waves then restart at `0xff<<20` × the offset, as the tables do.
Without this change they would restart at phase 0.

---

## 4. CPU

- **Rule (a) (the gate):** each new wave runs an existing table loop, so
  its per-sample cost equals `Tri`/`Saw`/`Rec` in every mode (plain 24, FM
  35, sync 34/39, ring 31). That is below the frozen per-oscillator ceiling
  (the v1 user-sample loops, 40–51). No voice's dearest setting changes.
- **Rule (b) (reported only):** equal by construction. The host test D1
  (§7) proves the routing goes to the same loop, bit for bit.
- **Flash reads:** program flash, the same path and cache behaviour as the
  existing tables. There is no sample-flash read.
- **What can move:** the dispatchers' code, because LTO re-allocates
  registers when a case is added. The first P2 measured this: loops
  instruction-identical, some registers renamed.
- **Gates:**
  - `elf_osc_check` against `frozen86/elf_osc.txt`: only the 8 dispatchers
    listed in §3.3 may change;
  - `elf_loopdump.py` against `frozen86/elf_loops.txt`: every baseline
    per-sample loop is exact or register-renamed with an equal count;
  - `armcheck-oscmod`: the existing figures are unchanged;
  - the static count of the new generic copy equals the `Tri` copy;
  - device G-C (§7, H8).

---

## 5. Resources

| | S085 end | Expected | Notes |
|---|---:|---:|---|
| Program flash used | 613,960 | ≈ 727,000 | +112,640 B tables, plus a few hundred B of dispatch |
| **Flash headroom** (of 753,664) | 139,704 | **≈ 26,000** | see Q1 |
| ITCM | 7,792 | ≈ 8,000–8,800 | the generic copies inlined into the ITCM dispatchers, if LTO inlines; measured, not estimated |
| bss / DTCM / `DrumVoice` | | unchanged | |
| Sample flash | | unchanged | user samples keep the whole region |

**Flash risk:** after this step, about 26 KB of program flash is left for
S2–S5: the installer and catalogue v2, the `wtb` and `smp`/`slp` loops, and
the optimizer's flag handling. S2 removes the v1 sample loops and the
manifest code, which returns some flash. The S2 plan must budget flash
explicitly.

---

## 6. Order of work (after approval)

1. **Clean up the first P2** (untracked files only).
   - **Delete:**
     - `Core/DSPAudio/StockWaves.c/.h`
     - `Core/SampleRom/StockWaveGen.c/.h`
     - `Core/SampleRom/StockWaveRecipes.c`
     - `tools/gen_stock_recipes.py`
     - `tools/dsp_test/test_osc_stock.c`, `osc_stock_host.h`,
       `test_stock_gen.c`
     - `S086_P2_ADD_STOCK_WAVES.md`
   - **Keep:**
     - `tools/plots/` (§2.1);
     - `tools/dsp_test/snapshot86.sh` and `frozen86/`. They were taken from
       the clean `e56bb957` tree; all 11 frozen sources are byte-identical
       to the working tree today. `identity_allowed.txt` is rewritten for
       this plan's lines.
     - `tools/dsp_test/elf_loopdump.py` (the per-loop comparison).
2. **Baseline:** `make clean && make all && make img`. Confirm text 613,536
   and ITCM 7,792. Confirm `elf_osc_check` and `armcheck-oscmod` reproduce
   `frozen86/`.
3. **Generator:** write `tools/gen_stock_tables.py`; run C1–C4; write
   `wavetableStock.c`; write `plot_stock_tables.py` and its PNG. Pause
   here so the user can review the C1 numbers and the plot before any
   firmware edit.
4. **Firmware:** apply §3.1, §3.2 (header and Makefile), §3.3 and §3.4.
5. **Host tests and gates** (§7), then the build figures (§5).
6. **Report:** `S086_P2_IMPLEMENTATION.md`, then the hardware checks.

---

## 7. Tests

**Host (`tools/dsp_test/`):**

| Target | Checks |
|---|---|
| `stock_tables` (new) | `gen_stock_tables.py --check` (C1–C4; the committed file regenerates byte-identical) |
| `osc_stock` (new, `test_osc_stock.c`) | **D1:** for every new wave, every level, random phases and increments, each dispatcher's output is bit-identical to a direct call of the matching table loop with that wave's table (plain, FM with a random modulator, sync with random reset lists, ring with a depth ramp). **D2:** `osc_syncWaveform`, `osc_syncClockInc` and `osc_isTableWave` for waveforms 0–15. **D3:** `Rec → Re3` interpolation is the blend of the two renders. **D4:** `osc_setFreq` gives the stock waves the same `phaseInc`/`tableOffset` as `Saw` at the same frequency. |
| `osc86_identity` | `Oscillator.c/.h` against `frozen86/`: only the listed lines change; `BufferTools` identical |
| `osc_mod` | the S085 laws still hold (its extraction line ranges updated) |
| `selftest`, `special_tags` (155 rows), `octave`, `mixer`, `filter`, `postchain`, `armcheck-*` | unchanged |

**Break proofs:** each is applied, must fail, then is reverted:
- a stock case removed from one dispatcher (D1);
- the pointer array order swapped (D1);
- the sync clock test back to `>= CRASH` (D2);
- `osc_setFreq` stock case falls to `default` (D4).

**Hardware (user):**

| # | Check | Expect |
|---|---|---|
| H1 | Flash and boot, no Load:[Samples] | `wav` lists `Sin Tri Saw Rec Noi Cym Re3 Sw3 ReA TrA SwA`, then the installed samples as `s01…` |
| H2 | Each new wave, low/mid/high notes, every instrument | the intended 303/analog sound immediately (no install needed); no jump in brightness at octave switches, comparable to `Rec`/`Saw` |
| H3 | Drum `fm`, a stock carrier and a stock modulator | FM depth comparable to Tri/Saw/Rec |
| H4 | Drum `snc`: a stock slave; a stock master over a Tri slave | sync pitch follows the master |
| H5 | Drum `rng`: a stock carrier | ring sound; amount 0 = dry |
| H6 | Cymbal/HiHat, stock waves on all three oscillators | plays |
| H7 | `wav` sweep/LFO across `Cym → Re3 … SwA → s01`, interpolation on | smooth; no silence at `SwA → s01` |
| H8 (**G-C**) | six-slot worst-case Scene with stock waves on every oscillator, plus the S085 baseline Scenes | `cpu` no worse than baseline; zero underruns |

---

## 8. Behaviour changes

1. **Old kits:** user samples move from `wav` 6+ to 11+. A kit that stored
   user sample *n* (`6 + n`) now plays a stock wave (*n* ≤ 4) or user
   sample *n* − 5. There is no migration (S086 Q6, still to confirm).
   Installed samples themselves are untouched.
2. **Drum slot 5's power-on seed:** `TRI + slot` was user sample 0 and is
   now `Re3`. Any kit load overwrites it.
3. **The Drum FM oscillator** still never resets on a trigger, so the
   start phase (§3.4) applies to `osc1` and the Cymbal/HiHat modulators.

---

## 9. Questions for review

| # | Question | Recommendation |
|---|---|---|
| **Q1** | Accept 112,640 B of program flash for the tables, which leaves about 26 KB of headroom for S2–S5? | Accept. It is what "the same way as the existing waves" costs; S2 plans its flash budget against the remainder |
| **Q2** | Band limit: match the existing tables' taper (measured from `sawTable`, §3.2), or the spike's brick-wall levels? | Match the existing tables |
| **Q3** | Display names `Re3 Sw3 ReA TrA SwA` (code `SQ3 SW3 SQA TRA SWA`), in that order at 6–10? | Yes (plan §4.1, the S085 rollback names) |
| **Q4** | `OSC_SAMPLE_START` 6 → 11, no kit migration? | Yes (S085 Q8 / S086 Q6) |
| **Q5** | Delete the first P2's untracked files as listed in §6 step 1? | Yes |
| **Q6** | Drum slot-5 seed: leave it, or seed `TRI + slot % 3`? | Leave it |
| **Q7** | `typ`/`pos` rows and the §3 menu layout of the S086 plan: keep them out of this step? | Yes, the next step after H1–H8 pass |
