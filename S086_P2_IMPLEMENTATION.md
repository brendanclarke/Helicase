# S086 P2 — Stock table waves Re3 and Sw3 (implementation report)

**Session:** 086, phase P2
**Plan:** `S086_P2_ADD_STOCK_WAVES_CORRECTLY.md`, reduced by the user to the
first two waves. `ReA`, `TrA` and `SwA` are left for later.
**Branch:** `dev-ph7-voice_expansion`, on top of `e56bb957` (uncommitted)
**Written:** 2026-10-27
**Status:**
- implemented;
- host gates and linked-image gates pass;
- **hardware test pending** (§6).

---

## 0. Summary

**The waves:** `Re3` (303 square, code `SQ3`) and `Sw3` (303 saw, `SW3`)
are `wav` values 6 and 7.
- Each is a `const int16_t [11][1024]` octave table in program flash, the
  same layout as `sawTable`/`recTable`/`triTable`.
- They are read by the existing table loops: plain, FM, sync and ring.
- No loop was added or edited.
- There is no install step and no sample-flash use. They play from power-on.

**User samples** move from `wav` 6+ to 8+ (`OSC_SAMPLE_START` 6 → 8). There
is no kit migration (S086 Q6).

**CPU:** the new waves run the Tri/Saw/Rec loops, so they cost the same:

| Loop | Static instructions per sample |
|---|---:|
| plain | 24 (23 in the gain-1 clone) |
| FM | 35 |
| sync | the same `calcWavetableOscSyncBlock` function |
| ring | the same `calcWavetableOscRingBlock` function |

**Build (DEV):**

| | S085 end | Now | Change |
|---|---:|---:|---:|
| text | 613,536 | 658,664 | +45,128 B (tables 45,056 B) |
| flash headroom | 139,704 | 94,576 | −45,128 B |
| ITCM | 7,792 | 8,512 | +720 B (§4.3) |
| data / bss / DTCM | 424 / 430,296 / 4,472 | unchanged | 0 |

---

## 1. Files

**New:**

| File | What |
|---|---|
| `tools/gen_stock_tables.py` | the generator, with the built-in checks C1–C4 (§2) |
| `Core/DSPAudio/wavetableStock.c` | generated: `sq3Table`, `sw3Table` |
| `tools/plots/plot_stock_tables.py`, `tools/plots/stock_tables_levels.png` | the review plot (levels 0/4/8/10 and spectra beside Rec/Saw) |
| `tools/dsp_test/test_osc_stock.c` | the host test, D1–D5 (§3) |

**Copied from the spike** (`Helicase-dev-ph7-spike/tools/plots/`):
- `tools/plots/` is identical to the spike, apart from a provenance note in
  its README.
- `plot_stock_waves.py` reproduces `stock_wave_harmonics.csv` byte for byte.

**Changed:**

| File | Change |
|---|---|
| `Core/DSPAudio/Oscillator.h` | `SQ3 SW3 OSC_STOCK_FIRST OSC_STOCK_LAST`; `OSC_SAMPLE_START` 8; `osc_isTableWave()`; two comment lines |
| `Core/DSPAudio/Oscillator.c` | `osc_stockTables[]` with a `_Static_assert`; one `case SQ3: case SW3:` in each of `calcNextOscSampleFmBlock`, `calcNextOscSampleBlock`, `osc_renderPlainWave`, `osc_renderSyncWave`, `calcNextOscSampleRingBlock` and `osc_setFreq`; `osc_syncWaveform()` includes the stock waves; `osc_syncClockInc()`: `>= CRASH` → `== CRASH \|\| >= OSC_SAMPLE_START` |
| `Core/DSPAudio/wavetable.h` | two `extern`s |
| the four engines | `DrumVoice.c`, `Snare.c`, `CymbalVoice.c`, `HiHat.c`: the trigger start-phase test uses `osc_isTableWave()` |
| `Core/Menu/MenuText.h` | `waveformNames` count 8, adds `Re3 Sw3` |
| `Core/Menu/menu.c` | includes `Oscillator.h`; a `_Static_assert` that the name count equals `OSC_SAMPLE_START` |
| `Makefile` | `wavetableStock.c` in `DSP_SRCS` |
| `tools/dsp_test/Makefile` | `osc_mod` line ranges and `om_stock.inc`; new targets `stock_tables`, `osc_stock`, `osc86_identity`, `elf_osc86_*`, `elf_loops86_*` |
| `tools/dsp_test/test_osc_mod.c` | two host tables and one include |
| `tools/dsp_test/extract.py` | `--object` also matches a declarator such as `(* const name[])[1024]` |
| `tools/dsp_test/DSP_TEST.md` | the new targets |
| `tools/dsp_test/frozen86/identity_allowed.txt` | rewritten for these edits |
| `build/LXRV2_lxr02.img` | rebuilt |

**Deleted:** the first P2's untracked files, as listed in plan §6 step 1.

**Kept:**
- `tools/dsp_test/snapshot86.sh` and `frozen86/`;
- `elf_loopdump.py`.

A fresh `e56bb957` build (a `git archive` copy in the scratchpad)
regenerated `frozen86/elf_osc.txt` and `elf_loops.txt`. Both came out
byte-identical to the copies already there (25 symbols, 58 loops).

---

## 2. The tables

**Method** (plan §3.2):
1. Recipe: `stock_wave_harmonics.csv` (`sq3`, `sw3`).
2. Band limit per level: `g_lv(k)`, measured from `sawTable`.
3. Synthesis: 1,024 points per level.
4. Each level is scaled to a peak of 32,767 and rounded.

The output was compared with the spike's plot of the recipe
(`stock_waves_303_analog.png`): same shape and same orientation.

**C1 finding (deviation from the plan's expectation):** only the Saw
calibration reproduces.

| Max error, LSB, levels 0–10 | |
|---|---|
| `sawTable` | 0 0 2 4 4 4 4 5 4 4 3 |
| `recTable` | 1 1942 11185 … |
| `triTable` | 24 30 57 … 4095 |

- **What differs:** `recTable` and `triTable` share one band limit with each
  other, but not with the saw. Their roll-off is slower: level 5 is −4.1 dB
  at k = 21 and −16.7 dB at k = 41, where the saw is −5.4 and −45 dB.
- **Above H:** they also have a tail of −20 to −60 dB (re. the ideal 1/k)
  above the level's Nyquist harmonic count H. They are not cleanly
  band-limited above level 0.
- **Rec only:** about 200 LSB of even-harmonic residue at high levels.
- **Rule applied (plan §3.2 step 2):** the saw-measured curve, for both
  waves. C1 is now a hard gate on the saw only, which checks the method;
  Rec and Tri are printed for information.
- **Consequence:**
  - `Sw3` band-limits exactly like `Saw`.
  - `Re3` is cleaner than `Rec` at high notes (no tail) and slightly
    darker near the top of each octave.
- **Alternative:** a Rec-matched taper for `Re3`. It would copy the tail,
  which is aliasing, not a gain. Not done.

**C2 / C3 / C4:** pass.
- C2: every level peaks at 32,767; |DC| < 1 LSB.
- C3: no out-of-band content above the saw's own.
- C4: the committed file regenerates byte-identical.

**Level:**
- All tables peak at full scale, as the existing ones do.
- RMS of `Re3`: −8.4 dBFS (levels 0–6), against `Rec` at −0.0. Re3 plays
  about 8 dB quieter than `Rec`.
- RMS of `Sw3`: −7.0 dBFS, against `Saw` at −4.8 (about 2 dB quieter).
- Both rise toward the top levels (Re3 −3.0 dBFS at level 10). The existing
  tables do the same, less strongly.
- This follows from the 303 shapes' crest factor. It is not changed here
  (see §7).

---

## 3. Host tests

All pass.

| Target | Result |
|---|---|
| `stock_tables` | C1 (saw) / C2 / C3 / C4 OK |
| `osc_stock` (new) | `osc_stock failures: 0` |
| `osc_mod` | `osc_mod failures: 0` |
| `osc86_identity` | OK: the removed/changed lines are exactly the 8 allowed ones (3 code lines: `OSC_SAMPLE_START`, the two sync-rule returns/tests; 5 comment lines); `BufferTools` identical |
| `selftest`, `special_tags` (155 rows, 0 mismatches), `pack`, `postchain`, `octave` (16 / 2 ulps, as documented), `mixer` | pass |
| `filter` (S1 acceptance PASS, 0 failures), `armcheck-postchain` (3 MATCH), `armcheck-mixer` (6 MATCH; 55→44 … 87→78), `armcheck-filter` (vdiv 81 / 39) | pass, unchanged |

**`osc_stock` checks:**
- **D1:** each dispatcher routes `SQ3`/`SW3` to the Tri/Saw/Rec loop with
  the wave's own table. Output and oscillator state are bit-identical:
  plain, `osc_renderPlainWave`, FM, sync, ring; every level; 24 random
  cases each.
- **D2:** with Rec's data in the stock tables, the four top-level entry
  points (plain, FM, sync with its ratio, ring) are bit-identical to `REC`.
- **D3:** with Saw's data, interpolation `Tri → SQ3/SW3` is bit-identical
  to `Tri → Saw` in all four modes.
- **D4:** the partitions, waveforms 0–15: `osc_isTableWave`,
  `osc_syncWaveform`, and `osc_syncClockInc` (`<< 5` only for Crash and
  user samples).
- **D5:** `osc_setFreq` gives the stock waves Saw's `phaseInc` and
  `tableOffset` over 400 frequencies (5 Hz to about 690 kHz).

**Break proofs.** Each was applied to the real source, failed as listed,
and was reverted. The sources were confirmed byte-identical afterwards.

| Mutation | Fails |
|---|---|
| FM stock case removed | D1 fm |
| `osc_stockTables` order swapped | D1 plain / renderPlain / fm … |
| sync clock test back to `>= CRASH` | D4 `osc_syncClockInc` (6, 7) |
| `osc_setFreq` stock case removed | D5 |
| `osc_syncWaveform` without the stock waves | D4 |
| `osc_isTableWave` without the stock waves | D4 |

---

## 4. Linked-image gates

### 4.1 Whole functions (`elf_osc86_check`)

- 0 missing.
- **11 changed, all expected:**
  - **8 dispatchers** (they gained the case):
    - `calcNextOscSampleBlock` ×2,
    - `calcNextOscSampleFmBlock`,
    - `calcNextOscSampleRingBlock`,
    - `calcNextOscSampleSyncBlock`,
    - `osc_renderPlainWave`,
    - `osc_renderSyncWave`,
    - `osc_setFreq`.
  - **3 user-sample functions:** `calcUserSampleOscBlock`,
    `calcUserSampleOscFmBlock` and `calcUserSampleOscRingBlock`. Each
    differs in exactly one instruction, `subs r3, #6` → `#8` (the sample
    index).
- **1 added:** `calcFmBlock.constprop.3`, the FM loop with a variable
  table, used by the stock case.
- The exit status is 1 by design.

### 4.2 Per-sample loops (`elf_loops86_check`)

58 baseline loops; 61 now.

| Result | Count | Notes |
|---|---:|---|
| exact | 47 | |
| renamed | 1 | the `calcNextOscSampleFmBlock` table loop; registers only |
| "missing" | 10 | all are fragments of the inlined **noise** loop (the `[.., #2056]` RNG-state load), compared by hand below |

**The 10 noise fragments, by hand:**
- **`calcNextOscSampleBlock` ×2:** the same 28 / 26 instructions. Only a
  branch narrowed (`beq.w` → `beq.n`) and its target offset moved.
- **`calcNextOscSampleFmBlock`:** the same 26 instructions, with registers
  renamed and the equivalent narrow forms (`sub.w` → `subs`).
- **`osc_renderPlainWave`:** 10 instructions per sample before and after.
  One `mov` is scheduled one slot earlier, and the setup is one instruction
  shorter.
- **`osc_renderSyncWave`:** the same hold and refresh paths. The
  user-sample tail call now sits between them.

**The 3 new loops:**

| Loop | Instructions | Equals |
|---|---:|---|
| stock FM | 35 | the Tri/Saw/Rec FM loop, registers renamed |
| stock plain | 24 | the existing table loop (gain clone) |
| stock plain, gain 1 | 23 | the existing table loop (gain-1 clone) |

- Sync and ring call the unchanged out-of-line
  `calcWavetableOscSyncBlock` / `calcWavetableOscRingBlock`. Neither is
  changed.
- **`armcheck-oscmod`** (standalone object): byte-identical to
  `frozen86/armcheck_oscmod_baseline.txt`.

**Conclusion:**
- No existing per-sample loop gained an instruction.
- Each new wave's loop has the same instruction count as its Tri/Saw/Rec
  counterpart.
- Rule (a) holds: no voice's dearest setting changes.

### 4.3 Resources

**Tables:** `sq3Table` at `0x0807E094` and `sw3Table` at `0x08078894`,
22,528 B each, in program flash (`.rodata`, below the `0x080C0000`
sample-flash floor).

**ITCM +720 B.** The generic copies were inlined into the ITCM dispatchers:

| Symbol | Change (B) |
|---|---:|
| `calcFmBlock.constprop.3` | +184 (new) |
| `osc_renderPlainWave` | +148 |
| `calcNextOscSampleBlock` .0 | +144 |
| `calcNextOscSampleBlock` .1 | +136 |
| `osc_renderSyncWave` | +44 |
| `calcNextOscSampleFmBlock` | +28 |
| `calcNextOscSampleRingBlock` | +24 |
| `calcNextOscSampleSyncBlock` | +8 |
| `osc_setFreq` | +4 |

ITCM is now 8,512 / 16,384 B (7,872 free).

**Warnings:** the changed firmware files compile without warnings.

---

## 5. Behaviour changes

1. **Old kits:** `wav` values 6 and 7 used to select user samples 1 and 2.
   They now play `Re3` and `Sw3`. User sample *n* is now at `7 + n`.
   Installed samples are untouched; there is no reinstall.
2. **Drum slot 5's power-on seed** (`TRI + slot` = 6) is now `Re3`. Any kit
   load overwrites it.
3. **The Drum FM oscillator** still never resets on a trigger. The table
   start phase applies to `osc1` and the Cymbal/HiHat modulators.
4. **Level:** `Re3`/`Sw3` are quieter than `Rec`/`Saw` at the same settings
   (§2).

---

## 6. Hardware checks (user)

| # | Check | Expect |
|---|---|---|
| H1 | Flash and boot | `wav` lists `Sin Tri Saw Rec Noi Cym Re3 Sw3`, then the installed samples as `s01…`; no Load:[Samples] needed |
| H2 | `Re3`, `Sw3` at low/mid/high notes on every instrument | the 303 square/saw character; no brightness jump at octave switches |
| H3 | Drum `fm`: a stock carrier and a stock modulator | FM comparable to Tri/Saw/Rec |
| H4 | Drum `snc`: a stock slave; a stock master over a Tri slave | sync pitch follows the master (not 32× too many resets) |
| H5 | Drum `rng`: a stock carrier | ring; amount 0 = dry |
| H6 | Cymbal/HiHat, stock waves on all three oscillators | plays |
| H7 | `wav` sweep/LFO `Cym → Re3 → Sw3 → s01`, interpolation on | smooth; no silence at `Sw3 → s01` |
| H8 (**G-C**) | the six-slot worst-case Scene with Re3/Sw3 on every oscillator, plus the S085 baseline Scenes | `cpu` no worse than baseline; zero underruns |

---

## 7. Open points

| # | Point | Recommendation |
|---|---|---|
| P-A | Rec/Tri use a looser band limit than Saw (§2); Re3 follows Saw's | Keep; listen at H2 |
| P-B | Re3 about 8 dB and Sw3 about 2 dB quieter than Rec/Saw (peak-normalised, like every existing table) | Decide by ear at H2. Normalising to Rec/Saw RMS instead would clip the 303 shapes' peaks, so the option would be a fixed per-wave gain in the generator (headroom permitting) |
| P-C | `ReA`, `TrA`, `SwA` | Later. One `WAVES` line each in the generator, plus an ID, a name and a table entry (+22,528 B each) |
| Next | the rest of S1: the `typ`/`pos` rows | after H1–H8 |
