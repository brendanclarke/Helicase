# Session 086 — Handoff Log

```
DATE: 2026-10-27 / 2026-10-28
SESSION GOAL: Begin the S086 oscillator type expansion (S086_P1_OSC_TYPE_
              EXPANSION.md) under the S085 "no CPU increase" rule. First
              target: the five new stock waveforms from the S084 spike,
              implemented the same way as Sin/Tri/Saw/Rec/Noi/Cym; then the
              `pos` parameter as the fourth cell of the first oscillator
              screen.
COMPLETED:
  P2 (first attempt, rejected): stock waves generated at Load:[Samples]
      into sample flash. Rejected by the user (not the same mechanism as
      the existing waves); reverted (§2).
  P2 (redone): plan S086_P2_ADD_STOCK_WAVES_CORRECTLY.md; implemented
      for Re3 and Sw3 only (user): [11][1024] program-flash tables from a
      new generator, played by the existing table loops (§3-§4).
  P3: plan S086_P3_WAV_OSC_POS_PARAMETER.md (pos, made continuous by
      user decision), implementation schedule S086_P3_IMPLEMENTATION.md
      with cited lines and comment blocks (§5-§7).
  P4: S086_P4_SAMPLE_CYM_POS.md: pos on the Cym sample, implemented with
      P3; the sample decisions D1-D10 for the next session (§8).
  Implementation (user/agent), assessment (findings F1-F6), fixes for
      F1/F2/F3/F5 including a new mixer hot-loop gate (§9-§10).
  S087 startup document S087_P0_OSC_TYPE_SAMPLES_WAVETABLES.md (§15).
  Session close: this log, the index entry, specification updates,
      MEMORY.md, volatile notes.
VERIFIED ON HARDWARE: yes. P2 H1-H8 implicitly (the P3 build includes P2),
  P3 H1-H10 and P4 C1-C4 passed on the fixed image ("that checks out on
  hardware, mark it as passed", user, 2026-10-28).

CHANGES THIS SESSION (detail in §13):
- Core/DSPAudio/Oscillator.h/.c: SQ3/SW3, OSC_STOCK_FIRST/LAST,
  OSC_SAMPLE_START 8, osc_isTableWave(), osc_stockTables[] and the stock
  case in six dispatchers, osc_syncWaveform()/osc_syncClockInc() rules;
  OscInfo.position/positionApplied/positionBendStep (padding), layout
  asserts, OSC_POS_BEND_CAP/PER_STEP, osc_positionIsPhase(),
  osc_positionTrigger(), osc_positionBendApply()/Restore().
- Core/DSPAudio/wavetableStock.c (new, generated), wavetable.h (externs),
  Samples.c/.h (crashSample guard byte, 32,769 entries).
- Engines DrumVoice.c, Snare.c, CymbalVoice.c, HiHat.c: trigger start
  phase via osc_isTableWave(); pos at the trigger; bend around the osc1
  render (Drum: not in snc).
- Parameter tables (4 x .c/.h): osc1_position row, enum, counts 40/39/40/40,
  page 1 cell 4 (Snare page reordered).
- Core/Menu/MenuText.h (waveformNames 8), menu.c (include + count assert).
- Makefile: wavetableStock.c.
- tools: gen_stock_tables.py (new), plots/ (copied from the spike, plus
  plot_stock_tables.py and its PNG), dsp_test/ (snapshot86.sh, frozen86/,
  elf_loopdump.py, elf_hotloops.py, test_osc_stock.c, test_osc_pos.c,
  extract.py --object for declarators, Makefile targets, DSP_TEST.md).
- build/LXRV2_lxr02.img rebuilt.
- Specifications: §14.

KNOWN ISSUES INTRODUCED: none open. Accepted behaviours: §12.
KNOWN ISSUES RESOLVED: crashSample one-past-the-end read (guard byte);
  stock-wave sync masters would have reset 32x too often (caught in the
  first P2 and fixed in the redone P2).

NEXT SESSION RECOMMENDED GOAL: Session 087 from
  S087_P0_OSC_TYPE_SAMPLES_WAVETABLES.md: stage A (step automation lands
  before its trigger), B (typ rows, modulator pos), C (catalogue v2 +
  installer), D (smp/slp with pos D1-D9), E (wtb).
BLOCKERS: the spike trees were removed from the working tree at the close
  (S087 P0 Q1: restore them outside the repository as references, or plan
  without them). Commit the close-out documents.

CRITICAL REMINDERS FOR NEXT SESSION:
- No state may stay live across a voice render: LTO inlines the renders
  into mixer_calcNextSampleBlock() (F1). Use OscInfo fields; gate with
  elf86_mixer_check.
- osc_positionBendApply() must always be paired with
  osc_positionBendRestore() after the same render: osc_setFreq()'s cache
  would keep a bend left in phaseInc.
- Host tests must compile the header's constants (extract them).
- tools/dsp_test osc_mod extracts Oscillator.h by line ranges (49-65,
  71-143); edits above or inside OscInfo move them.
- Free OscInfo padding left: 2 B at offsets 34-35, 1 B at 75.
- Flash headroom 94,368 B is all that remains for S087 and later.
```

---

## 1. Session overview and timeline

| When | Step | Outcome |
|---|---|---|
| 2026-10-27 | Session start: read `MEMORY.md`, `S086_P1_OSC_TYPE_EXPANSION.md`; first target "bring the 5 new stock waveforms from the spike" | — |
| 2026-10-27 | First P2 (`S086_P2_ADD_STOCK_WAVES.md`, done before this conversation's restart): install-time generation into sample flash | **Rejected** by the user (§2); tracked changes reverted, untracked files left |
| 2026-10-27 | Copy the spike's `tools/plots/`; plan `S086_P2_ADD_STOCK_WAVES_CORRECTLY.md` (fixed tables, same loops) | Plan written; no code |
| 2026-10-27 | User question: why so large, alternatives, can Tri/Saw/Rec be compressed losslessly at zero CPU? | Answer recorded (§3.6): no; the only zero-cycle, bit-exact gain is more program flash (sector 7) |
| 2026-10-27 | Implement **Re3 and Sw3 only** ("~45 kB") | P2 implemented, all host/ELF gates pass; report `S086_P2_IMPLEMENTATION.md` |
| 2026-10-27 | Plan `pos` (`S086_P3_WAV_OSC_POS_PARAMETER.md`), trigger-time version | First draft |
| 2026-10-28 | User questions on LFO behaviour, continuous `pos`, block stepping, "latency" | `pos` made continuous via a per-block `phaseInc` bend (§5.3); latency explained (§5.4) |
| 2026-10-28 | Plan revised (continuous, Q11–Q14); P4 thin plan for `Cym`; decisions recorded (all recommendations accepted; P4 Q1 `/128` confirmed) | — |
| 2026-10-28 | Code deep-dive; `S086_P3_IMPLEMENTATION.md` schedule (C1–C17, tests, gates) | — |
| 2026-10-28 | Implementation (commit `e67879e2`), assessment (§9) | F1 (mixer register pressure), F2/F3 (test gaps), minors |
| 2026-10-28 | Fixes (§10): `positionBendStep`, Apply/Restore API, mixer hot-loop gate, test rewrite | All gates pass |
| 2026-10-28 | Hardware: everything passed (user) | — |
| 2026-10-28 | Review of P4 → sample decisions D1–D10; S087 startup document; session close | — |

---

## 2. The rejected first P2 (install-time generation)

`S086_P2_ADD_STOCK_WAVES.md` followed the S086 plan §4.1 (and S085 Q6 (a))
literally. It:

- **Generated the waves at install:** at every Load:[Samples], from about
  10 KB of recipes (`StockWaveRecipes.c`, `StockWaveGen.c`), into a
  61,120 B block at the sample-flash floor (`0x080C0000`).
  - **Layout:** 5 × 6,112 int16 points; level lengths 1024 ×5, then 512,
    256, 128, 64, 16, 16.
  - **Validity:** a SHA-256-prefix recipe tag after the block and a RAM
    validity flag.
- **Fallbacks:** the waves played fallback tables until an install
  (Re3/ReA → Rec, Sw3/SwA → Saw, TrA → Tri).
- **Sample floor:** it moved the v1 data floor (`SAMPLE_DATA_START_ADDRESS`),
  so existing user samples were rejected until reinstalled.
- **Readers:** four new reader loops with a variable shift and mask in a new
  `.itcm.s086` section.
  - The sync copy was +2/+3 (36/42); plain/FM/ring equalled the table loops.
  - ITCM +1,504 B; text +12,024 B.
- **The bug it caught:** `osc_syncClockInc()` used `>= CRASH`, which would
  have treated stock-wave masters as `<< 17` sample clocks and reset 32×
  too often. The fix survives in the redone P2.

**Why it was rejected (user):**
- None of that is how Tri/Saw/Rec work. They are fixed tables, always
  present, read by one set of loops.
- The new waves had to behave the same way: available at power-on, the
  same cost, no install dependency.

**Disposal:**
- Its tracked edits were reverted.
- Its untracked files were deleted in the redone P2: `StockWaves.c/.h`,
  `StockWaveGen.c/.h`, `StockWaveRecipes.c`, `tools/gen_stock_recipes.py`,
  `test_osc_stock.c`/`osc_stock_host.h`/`test_stock_gen.c` (the old
  versions), and the plan itself.
- **Kept:** `tools/dsp_test/snapshot86.sh`, `frozen86/` (taken from the
  clean `e56bb957` tree) and `elf_loopdump.py`.

**Lesson:** "the same way as the existing feature" means the same mechanism.
Read the existing implementation before planning a variant.

---

## 3. P2: stock table waves `Re3` and `Sw3`

### 3.1 Spike review: how the five waves were made (`tools/plots/plot_stock_waves.py`)

The tools are copied unchanged from `Helicase-dev-ph7-spike/tools/plots/`
(`26fb7c3`).
- A provenance note was added to `README.md`.
- `plot_stock_waves.py` re-run reproduces `stock_wave_harmonics.csv` byte
  for byte.
- **Requirements:** Python 3 with `numpy`, `matplotlib` and `pillow` (not in
  `requirements.txt`).

| Wave | Code | Source | Harmonics |
|---|---|---|---|
| `Re3` | `SQ3` | the TB-303 square, digitised from `reference/303_square_saw.png` (left panel) | 1–40 measured, extended to 511 with a fitted power-law roll-off (never brighter than 1/k) and a continued linear phase trend (`lxr_plotlib.extend_harmonics()`) |
| `Sw3` | `SW3` | the 303 saw, same image (right panel) | 1–40 measured, extended |
| `ReA` | `SQA` | a modelled 50 % pulse → 2-pole low-pass (40th harmonic, Q 0.9) → 1-pole high-pass (0.1 × f0): rounded edges, overshoot, droop | 1–511 from the model |
| `TrA` | `TRA` | the dirty triangle, digitised from `reference/tra_dirty_triangle.png` | 1–30 measured, extended |
| `SwA` | `SWA` | a falling ramp with exponential (capacitor) curvature and a 1.5 %-of-cycle reset, through the same chain (LP h48, Q 0.75; HP 0.08 × f0) | 1–511 from the model |

- **Digitiser:** it finds the trace pixels per column, measures the period
  by autocorrelation, and averages all full cycles.
- **Orientation** matches the stock tables: the saws fall (index 0 = middle
  of the reset jump), the squares are high for the first half, the triangle
  starts at its trough.
- **The CSV:** per wave, harmonics 1–511, magnitude and phase, normalised so
  the full 511-harmonic cycle peaks at 1.0.
- **Other plot scripts** (`plot_pwm.py`, `plot_current_drive.py`,
  `plot_waveshapers.py`, `plot_wavetable_formats.py`) are kept for later
  stages.

**The spike's level method, not copied:**
- `gen_stock_recipes.py` used **brick-wall** levels: harmonics 1..`LEVEL_H`
  at full amplitude, `LEVEL_H` = 511, 511, 337, 168, 84, 42, 21, 10, 5, 2, 1.
- Its level lengths shrank for the install generator.

### 3.2 The existing tables' band limit (measured this session)

FFT per level of `sawTable`/`recTable`/`triTable` (`wavetable.c`):
- every level is 1,024 points, peak exactly 32,767;
- the saw follows a smooth **raised-cosine (Hann) taper**.

Saw gain against the ideal 1/k, at fractions of the level's Nyquist harmonic
count H:

| Level (H) | 0.25 H | 0.5 H | 0.75 H | 0.9 H | 1.0 H |
|---|---:|---:|---:|---:|---:|
| 0 (511) | −0.5 dB | −2.1 | −6.2 | −12.8 | −46 |
| 2 (337) | −1.3 | −5.9 | −16.5 | −31.6 | −75 |
| 5 (42) | −1.0 | −5.4 | −15.9 | −29.2 | −57 |
| 8 (5) | 0.0 | −0.9 | −9.2 | −9.2 | −20 |
| Hann, for comparison | −1.4 | −6.0 | −16.7 | −32.1 | −∞ |

- **Hann fit:** levels 2–5 fit a Hann cutoff of H + 1.75. Levels 0–1 are
  wider (the 512-bin limit), and levels 6–10 are coarse.
- **Level frequencies:** level n covers the octave below 16.3516 × 2ⁿ Hz
  (`osc_octaveEdgeHz`); H = ⌊(FS/2) / top⌋, capped at 511; level 10 → 1.

**Rec and Tri do not follow the saw** (the C1 check, §3.3):
- **A slower roll-off:** at level 5, −4.1 dB at k = 21 and −16.7 dB at
  k = 41, where the saw is −5.4 and −45 dB.
- **A tail above H:** −20 to −60 dB re. the ideal 1/k. They are not cleanly
  band-limited above level 0; they look like a filtered naive waveform
  with aliasing.
- **Rec only:** about 200 LSB of even-harmonic residue at high levels.

### 3.3 Generator: `tools/gen_stock_tables.py`

**Inputs:** `tools/plots/stock_wave_harmonics.csv` and
`Core/DSPAudio/wavetable.c` (read only).

**Output:** `Core/DSPAudio/wavetableStock.c`, generated and "do not edit",
with the CSV SHA-256 in its header.

**Method:**
1. Read the recipe: c_k = mag·e^{jφ}, so x(m) = Σ a_k cos + b_k sin with
   a_k = 2·mag·cos φ, b_k = −2·mag·sin φ.
2. Measure the per-level gain g_lv(k) = |SAW_lv(k)|·k/|SAW_lv(1)|, set to 0
   below −80 dB.
3. Synthesise each level as `irfft(c·g_lv)` at 1,024 points, scale to a
   32,767 peak, round.
4. Write in `wavetable.c`'s layout: `//---- Table N ----`, the MIDI note and
   the frequency.

**Checks** (exit 1 on failure):

| Check | What it does |
|---|---|
| **C1** | rebuilds `sawTable` from 1/k through g: max error per level 0 0 2 4 4 4 4 5 4 4 3 LSB, the hard gate (limit 6). Rec (1, 1942, 11185 …) and Tri (24, 30, 57 … 4095) are printed for information |
| C2 | peak 32,767; \|DC\| < 1 LSB |
| C3 | no harmonic above H larger than max(2 × the saw's, 0.5 LSB) |
| C4 | `--check` regenerates and compares the committed file byte for byte |

- **`WAVES`:** the list in the script names the waves to emit. Today
  `("sq3", "sq3Table", "Re3")` and `("sw3", "sw3Table", "Sw3")`; adding
  `ReA`/`TrA`/`SwA` is one line each, plus the firmware ID, name and table
  entry.
- **Review plot:** `tools/plots/plot_stock_tables.py` writes
  `stock_tables_levels.png` (levels 0/4/8/10 beside Rec/Saw, and
  per-level spectra).

**Result:**
- `Sw3` band-limits exactly like `Saw`.
- `Re3` is cleaner than `Rec` at high notes (no tail) and slightly darker
  near the top of each octave.
- A Rec-matched taper was rejected: it would copy aliasing as if it were a
  gain.

**Level:** peak-normalised like every existing table.

| Wave | RMS (levels 0–6) | Against |
|---|---:|---|
| `Re3` | −8.4 dBFS | `Rec` −0.0 (≈ 8 dB quieter) |
| `Sw3` | −7.0 dBFS | `Saw` −4.8 (≈ 2 dB quieter) |

- Both rise toward level 10 (−3.0 dBFS); the existing tables do the same,
  less strongly.
- This follows from the 303 shapes' crest factor. It was left unchanged;
  a fixed per-wave gain in the generator is the option.

### 3.4 Firmware design

**IDs:**

| `wav` | Constant | Display | Table |
|---:|---|---|---|
| 0–5 | `SINE TRI SAW REC NOISE CRASH` | `Sin Tri Saw Rec Noi Cym` | as before |
| 6 | `SQ3` (= `OSC_STOCK_FIRST`) | `Re3` | `sq3Table` |
| 7 | `SW3` (= `OSC_STOCK_LAST`) | `Sw3` | `sw3Table` |
| 8+ | user samples (`OSC_SAMPLE_START` 8) | `s01…` | v1 sample flash |

- **Names:** `waveformNames` has 8 entries; `menu.c` asserts the count
  equals `OSC_SAMPLE_START`. The menu sample tail, its maximum and the
  sample names key off `waveformNames[0][0]`; `modulationNode.c`'s
  `maxWave` and the waveform-interpolation range use `OSC_SAMPLE_START`.
  All follow automatically.
- **`osc_isTableWave(w)`** (`Oscillator.h`): TRI–REC and
  `OSC_STOCK_FIRST`–`OSC_STOCK_LAST`.

**Dispatch (`Oscillator.c`):**
- `static const int16_t (* const osc_stockTables[])[1024] = { sq3Table,
  sw3Table };`, with a `_Static_assert` on its length.
- **One `case SQ3: case SW3:`** with the **variable** pointer
  `osc_stockTables[waveform − OSC_STOCK_FIRST]` in:

  | Function | Stock case calls |
  |---|---|
  | `calcNextOscSampleFmBlock` | `calcFmBlock` |
  | `calcNextOscSampleBlock` | `calcWavetableOscBlock` |
  | `osc_renderPlainWave` | `calcWavetableOscBlock(…, 1.f)` |
  | `osc_renderSyncWave` | `calcWavetableOscSyncBlock` |
  | `calcNextOscSampleRingBlock` | `calcWavetableOscRingBlock` |
  | `osc_setFreq` | `osc_calcWavetableFreqValue` |

- **Why one variable-pointer site:** five constant-argument sites would
  invite LTO to make more specialised copies and re-specialise the existing
  ones (S085 D8).
- **Sync rules:**
  - `osc_syncWaveform()` includes the stock waves (an interpolated stock
    slave takes the synced phase rule);
  - `osc_syncClockInc()`: `>= CRASH` → `== CRASH || >= OSC_SAMPLE_START`.
- **Not touched:** `calcFm()`/`calcNextOscSample()` (no callers);
  `calcPeriodicInterp*` (they render through the dispatchers, so
  `Rec ↔ Re3` interpolates with no new code).

**Engines:**
- The trigger start-phase test `waveform > SINE && waveform <= REC` became
  `osc_isTableWave(waveform)` in all four engines.
- So the stock waves restart at `0xff << 20` × offset like the tables.

**Build:** `Makefile` has `wavetableStock.c` in `DSP_SRCS`.
- `sq3Table` sits at `0x0807E094` and `sw3Table` at `0x08078894` in
  `.rodata`, below the `0x080C0000` sample-flash floor.

### 3.5 P2 measurements and tests

| | S085 end | P2 | Change |
|---|---:|---:|---:|
| text | 613,536 | 658,664 | +45,128 (tables 45,056) |
| flash headroom | 139,704 | 94,576 | |
| ITCM | 7,792 | 8,512 | +720 |
| data / bss / DTCM | 424 / 430,296 / 4,472 | unchanged | |

**ITCM growth** (the generic copies inlined into the ITCM dispatchers):

| Symbol | Change |
|---|---:|
| `calcFmBlock.constprop.3` (new) | +184 |
| `osc_renderPlainWave` | +148 |
| `calcNextOscSampleBlock` .0 / .1 | +144 / +136 |
| `osc_renderSyncWave` | +44 |
| `calcNextOscSampleFmBlock` | +28 |
| `calcNextOscSampleRingBlock` | +24 |
| `calcNextOscSampleSyncBlock` | +8 |
| `osc_setFreq` | +4 |

**CPU:**
- **New loops:** stock FM 35 (= the table FM loop, registers renamed);
  stock plain 24 / 23 (gain-1 clone), equal to the table loops.
- **Sync and ring:** the existing out-of-line functions.
- **Rule (a)** holds.

**ELF gates** (against `frozen86/`, regenerated from a pristine `e56bb957`
build, byte-identical to the stored copies, 25 symbols / 58 loops):
- **`elf_osc86_check`:** 0 missing; **11 changed**, all expected:
  - the 8 dispatchers that gained the case;
  - `calcUserSampleOscBlock`/`FmBlock`/`RingBlock`, each with exactly one
    changed instruction (`subs r3, #6` → `#8`, the sample index).
  - **1 added:** `calcFmBlock.constprop.3`.
- **`elf_loops86_check`:** 47 exact, 1 renamed, 10 "missing".
  - **The 10 "missing" loops** are all fragments of the inlined noise loop
    (the `[.., #2056]` RNG-state load), compared by hand.
  - **In each case the instruction counts are equal:** a branch narrowed in
    `calcNextOscSampleBlock`; registers renamed in the FM dispatcher; in
    `osc_renderPlainWave` one `mov` moved one slot (10/sample both);
    `osc_renderSyncWave` was re-laid out.
- **`armcheck-oscmod`:** byte-identical to the frozen baseline.

**Host test `osc_stock`** (`test_osc_stock.c`): 0 failures.

| Check | What |
|---|---|
| D1 | every dispatcher routes `SQ3`/`SW3` to the table loop with the wave's own table, bit-identical (plain, `osc_renderPlainWave`, FM, sync, ring; every level; 24 random cases) |
| D2 | with Rec's data the four top-level entry points equal `REC` |
| D3 | `Tri → SQ3/SW3` interpolation equals `Tri → Saw` (Saw's data) in all four modes |
| D4 | partitions for waveforms 0–15 |
| D5 | `osc_setFreq` gives the stock waves Saw's `phaseInc`/`tableOffset` over 400 frequencies |

**Break proofs**, each applied, failing, reverted, the source confirmed
byte-identical:

| Mutation | Fails |
|---|---|
| FM stock case removed | D1 |
| pointer order swapped | D1 |
| sync clock back to `>= CRASH` | D4 |
| `osc_setFreq` stock case removed | D5 |
| `osc_syncWaveform` without the stock waves | D4 |
| `osc_isTableWave` without the stock waves | D4 |

**Other targets:** `stock_tables` (C1–C4), `osc_mod`, `osc86_identity` (8
allowed lines), `special_tags` 155 and the rest of the bench all passed.

**Bench tooling changes:**
- `extract.py --object` now matches declarators like
  `(* const name[])[1024]`.
- `osc_mod` line ranges and `om_stock.inc`.

### 3.6 The flash discussion (user questions, recorded for later)

**Why a wave costs 22,528 B:**
- 11 levels × 1,024 points × 2 bytes. The loops index every table with a
  fixed `phase >> 22` and `& 1023`, so every level must be 1,024 points,
  even when it holds one harmonic.
- **Five waves** would be 112,640 B, leaving ≈ 26 KB of headroom. **Two
  waves:** 45,056 B (chosen).

**Shorter upper levels** (the spike layout, 12,224 B per wave, −46 %):
- **CPU:** needs a reader with a per-level shift and mask (new loop copies;
  the sync copy +2/+3).
- **Sound:** it is not exact. Linear interpolation over fewer points
  changes the output.

| Level | 5 | 6 | 7 | 8 | 9 | 10 |
|---|---|---|---|---|---|---|
| Saw, change vs today | −67 dB | −62 | −58 | −52 | −34 | −37 |

  Rec is similar. It adds image aliasing at the top levels.

**Lossless compression** needs per-sample decoding. The existing tables have
no duplicate rows and no exact symmetries to exploit (checked).

**The only zero-cycle, bit-exact way to gain flash:**
- grow the program region into sector 7 (app 736 → 992 KB; sample flash
  1,280 → 1,024 KB), as S073 did for sector 6.
- **Changes:** the image builder's `SECTOR_ENDS`, the image-check CRC block
  (7 sectors), `SAMPLE_ROM_START_ADDRESS`, `SAMPLE_FIRST_SECTOR`.
- **Needs:** a hardware check that the closed LXRV2 bootloader writes the
  longer image, and one reinstall of samples.
- **Not done;** kept as the option if flash runs out.

### 3.7 P2 behaviour changes (accepted)

1. **Old kits:** `wav` 6/7 were user samples 1/2 and now play `Re3`/`Sw3`;
   user sample *n* is at `7 + n`. No migration. Installed samples are
   untouched.
2. **Drum slot 5's power-on seed** `TRI + slot` = 6 is now `Re3`. Any kit
   load overwrites it.
3. **Levels:** `Re3`/`Sw3` are quieter than `Rec`/`Saw` (§3.3).

**P2 plan questions:**
- Q1 flash: two waves only (user);
- Q2 band limit: the saw-measured taper;
- Q3 names `Re3 Sw3 (ReA TrA SwA)`;
- Q4 no migration;
- Q5 delete the first P2's files;
- Q6 seed left;
- Q7 `typ`/`pos` later.

---

## 4. P3 design: `pos` (`osc1_position`, `Oscilltr` / `Position`)

### 4.1 Requirement and placement

**The fourth cell of the first oscillator screen** on every instrument:
- Drum/Cymbal/HiHat `coa fin wav –` → `coa fin wav pos`.
- **Snare** `coa fin noi mix` / `wav` → `coa fin wav pos` / `noi mix`
  (P3 Q3: every oscillator page gets the same first screen).

**Meaning per type** (S086 P1 §3.1):
- the start position for `smp`/`slp`;
- the frame position for `wtb`;
- possibly the `pwm` width;
- for today's waves, a phase offset.

**Changed by the user (2026-10-28):** P1 §3.3's "trigger time only" became
**continuous**.

### 4.2 Phase units and the trigger rule

**Units:** for the periodic waves one cycle is 2³² of phase (sine 4,096
points at `<< 20`, tables 1,024 at `<< 22`). So `pos << 25` = `pos`/128 of
a cycle for every wave and octave (Q5: `/128`, exact, 64 = half a cycle,
every value distinct).

**At a trigger,** after today's start phase:

```
osc1.phase += (pos & 127) << 25;   osc1.positionApplied = pos
```

**Today's start phases** are unchanged:

| Engine | Sine | Table waves | Other |
|---|---|---|---|
| Drum | `1024 + (0x3FF<<20 − 1024) × offset` | `0xFF<<20 × offset` | 0 |
| Snare/Cymbal/HiHat | `0x3FF<<20 × offset` | `0xFF<<20 × offset` | 0 |

- Here `offset` = 1, or 1 − transient volume in offset mode.
- `pos` is added after that scaling (Q6: an absolute shift).
- The hit never glides.

### 4.3 The per-block bend (the continuous part)

```
d    = circular difference (pos − positionApplied) in −64..+63, capped (OSC_POS_BEND_CAP)
bend = d × OSC_POS_BEND_PER_STEP   (= d << 20 for 32-sample blocks, exact)
phaseInc += bend; render the osc1 block; phaseInc −= bend; positionApplied += d
```

**Why a bend and not a jump:**
- Modulation arrives once per block: the LFO is dispatched at the start of
  `mixer_calcNextSampleBlock()`, and knob, automation, velocity and Morph
  values land between blocks.
- Adding the step to the phase at the block edge would click or zipper.
- Spreading it over the block's samples is the same as raising
  `phaseInc` by Δ/32 for one block. Every loop already adds `phaseInc`, so
  there is no loop change and no per-sample cost.

**Properties:**
- **Continuous:** no jump; it lands exactly on the offset (`d << 20` × 32 =
  `d << 25`), so there is no drift.
- **Pitch-modulation smoothness:** the pitch envelope and pitch LFO also
  change `phaseInc` per block.
- **No zipper** from the 7-bit `pos`.
- **A moving `pos` is a frequency change:** an LFO on `pos` is phase
  modulation, heard as vibrato. A 4 Hz LFO with ¼-cycle depth ≈ ±6 Hz. A
  held `pos` is a pure phase shift.

### 4.4 Timing ("latency", user question)

There is **no added latency** against pitch modulation:
- A change starts on the same sample a pitch change would (the first sample
  of the block in which it arrives).
- For pitch, the frequency has its new value at once. For `pos` the phase
  offset is what moves: a ramp from sample 0 to sample 32.
- Against a (rejected) jump at the block start, the ramp trails by 0 → 32
  samples, ≈ 16 on average (0.36 ms).
- Since modulation exists only once per block, a per-sample-smoothed path
  would produce the same ramp.

### 4.5 Edge rules

- **Circular difference:** 127 → 0 is +1 step, not −127 (it would bend hard
  for one block). Code: `u = (pos − applied + 64) & 127; d = u − 64`
  (unsigned arithmetic).
- **Large changes:**
  - A big step (fast knob, velocity, automation, Morph) is squeezed into
    0.7 ms: a short pitch blip.
  - It also **aliases**: the octave table is chosen from the frequency, not
    the bent increment. A 1-step bend adds only ≈ 11 Hz.
  - **The cap** (`OSC_POS_BEND_CAP`, 64 = off) turns large steps into
    multi-block glides. **Q11:** built, left off; hardware passed with it
    off.
- **Backward motion:** a falling `pos` can make the bent increment wrap
  below 0 at low pitch. Modular arithmetic then runs the phase backwards
  for the block, which the sine, table and crash loops handle. The sync
  ratio does not handle it, hence no bend in `snc`.
- **Restore after rendering:** `osc_setFreq()` returns early when the
  frequency is unchanged (the S073 cache), so a bend left in `phaseInc`
  would persist as a pitch error.
- **Non-phase waves** (noise, v1 samples) consume the change without
  bending, so a later `wav` switch cannot release a stale bend.

### 4.6 Per waveform and per mode

| Case | Trigger | While playing |
|---|---|---|
| Sin Tri Saw Rec Re3 Sw3 | start + `pos` | bend |
| `Cym` (P4) | 0 + `pos`/128 of the sample | bend |
| interpolated pair | the base wave decides | both partners render with the bent `phaseInc`; `calcPeriodicInterp*` ends at start + `phaseInc` × size, which matches |
| Noise, v1 samples | none | none (the delta is consumed) |
| Drum `fm`/`mix`/`rng` (osc1 = carrier) | as above | bend: the carrier moves against the modulator; `modOsc` is a separate `OscInfo`, never bent |
| Drum **`snc`** (Q12) | as above | **no bend.** Every master reset sets the slave phase to a target without `pos`, and `osc_syncRatioQ16()` reads the slave `phaseInc` |

**The Drum sync clock** (`syncClock`) is zeroed only at init. So `pos`
shifts the slave start relative to a free-running master until the first
reset.

### 4.7 Modulation sources (main-loop order per block)

**Order per block:** `voiceControl_processPending()` (triggers) →
`seq_drainPendingAutomation()` (step values) →
`mixer_calcNextSampleBlock()` (LFO dispatch, async voice code, renders).

| Source | Behaviour |
|---|---|
| Knob / menu | glides within a block; a fast turn = a pitch blip |
| Velocity | `modNode_updateValue()` runs in each `*_triggerVoice()` before the phase is set, so the hit **starts exactly** at the velocity value |
| LFO | written once per block before the render, so it glides: continuous phase modulation. Depth/polarity as any 0–127 row; **clamped** at 0/127, not wrapped (Q13: keep the clamp) |
| LFO with retrigger | the trigger runs before that block's LFO write, so the hit starts at the old value and glides to the restarted LFO value within 0.7 ms |
| Step automation | a step's first trigger restores the Morph base (`seq_restoreAutomatedParameters()`) and fires before the step's values are drained. So the hit starts at the base `pos` and glides to the step value within the first block (Q14: accepted for `pos`; it is the S087 stage A/D6 item for samples). Whether the sequencer publishes a step's values a tick early was not traced |
| Morph | the morphed byte; glides |

### 4.8 Where it acts; the HiHat correction

- **Reset on trigger today:** Drum/Snare/Cymbal/HiHat `osc1` yes; Drum FM
  `modOsc` never; Cymbal `modOsc`/`modOsc2` yes (to 0); **HiHat
  `modOsc`/`modOsc2` never**.
- **Correction:** the S086 P1 plan said "Cymbal/HiHat modulators are reset".
  Only `Cymbal_triggerVoice()` resets them.
- **Modulator `pos` rows** are deferred to the `typ` step (Q10). With the
  continuous rule a *change* would be audible even without a reset; a held
  value would not.

### 4.9 Storage decision (Q1)

The S086 P1 plan proposed the unused `OscInfo.startPhase` (`uint32_t`,
written only by the never-called `Drum_setPhaseVoice()`). It was replaced
by bytes in `OscInfo` padding.

| Field | Offset | Role |
|---|---:|---|
| `uint8_t position` | 18 | the row value |
| `uint8_t positionApplied` | 19 | the part already in `phase`; runtime only |
| `int8_t positionBendStep` | 33 (after `waveInterpNext`) | this block's step; added by the F1 fix (§10) |

**Why bytes and not `startPhase`:**
- `sizeof(OscInfo)` stays 72 B on ARM, so there is no RAM change.
- An ordinary `TYPE_UINT8` row needs no special writer or tag.
- **Velocity:** `instrumentManager_buildModulationRange()` has a range for
  `TYPE_UINT8` (the descriptor 0..127) but **none for `TYPE_UINT32`**, so a
  `startPhase` binding would fall back to legacy shaping.
- The later types need the byte, not a phase.

**`startPhase` and `Drum_setPhaseVoice()`** are kept for a later clean-up
(Q7).

### 4.10 Descriptor rows, menu, storage

**The row** (one line, so `check_special_tags.py` matches it):

```c
ROW("osc1_position", "Oscilltr", "Position", "pos", DTYPE_0B127, MOD_0_127, osc.position, TYPE_UINT8)
```

- It is appended last in each table (Drum/Cymbal/HiHat index 39, Snare 38),
  so every existing descriptor index, automation ID, LFO/velocity token,
  NRPN ID (slot × 64 + index) and MIDI Learn row is unchanged.
- **Counts:** Drum 40, Snare 39, Cymbal 40, HiHat 40 (capacity 64; no RAM
  change).
- **`FLAGS_IMAGE`:** morphable, LFO/velocity target (Q8), automatable.
- **Page 1 cell 4** = position. Every page row keeps 16 cells.
- **Display:** `DTYPE_0B127`: the number, as the rollback rule asks.
- **Kits:**
  - the key `osc1_position` is written by the generic writer in `[params]`
    and `[morph]`;
  - old files load 0, because `instrumentManager_resetSlot()` zeroes the
    slot before parsing;
  - old firmware skips the key.
- **Host converters:** unchanged (Q9).
- **Slot reset:** a runtime slot reset `memset`s the voice
  (`instrumentManager_resetRuntimeSlotFromScene()`), so `positionApplied`
  starts at 0. The first block after a reload may glide from 0 to the
  loaded `pos`. This is harmless: the voice is silent until triggered, and
  the trigger re-anchors.

### 4.11 P3 decisions (all recommendations accepted, 2026-10-28)

| # | Decision |
|---|---|
| Q1 | bytes in `OscInfo` padding, not `startPhase` |
| Q2 | `Cym` in P4 (implemented with P3); v1 user samples in S2 |
| Q3 | Snare `coa fin wav pos` / `noi mix` |
| Q4 | key `osc1_position`, `Oscilltr` / `Position` / `pos` |
| Q5 | `pos`/128, `<< 25` |
| Q6 | added after the offset-mode scaling |
| Q7 | leave `startPhase` / `Drum_setPhaseVoice()` |
| Q8 | LFO and velocity destination |
| Q9 | host converters unchanged |
| Q10 | modulator rows deferred to `typ` |
| Q11 | cap built, off |
| Q12 | no bend in Drum `snc` |
| Q13 | LFO clamp |
| Q14 | accept the automation glide for `pos` |

---

## 5. P4: `pos` on `Cym`

- **Why it fits:** `crashSample[]` is 32,768 8-bit points read at `<< 17`
  (`itg = phase >> 17`). 32,768 × 2¹⁷ = 2³², so the whole sample is the full
  phase range, and the reader loops at its end with no test.
- **The rule:** `pos << 25` = `pos` × 256 points = `pos`/128 of the sample.
  The same arithmetic, trigger rule, bend and circular difference as the
  stock waves.
- **Speed:** the speed comes from `osc_calcSampleFreqValue()` (1,024 points
  per nominal period, so the sample spans 32 nominal periods). The mapping
  is unaffected.
- **Q1 (user):** `/128`, so 127 plays the last 1/128 of the sample. With
  `/127`, 127 would be the end, i.e. the start again.
- **What you hear:**
  - at the hit, later starts into the decay;
  - a slow change scrubs, since one step is 256 points over 32 samples,
    about 8× speed for the block;
  - an LFO gives continuous varispeed scrubbing;
  - a large jump is a 0.7 ms smear that aliases.
- **Modes:** as §4.6. Cym as the Drum FM-page master is unaffected (an
  `osc1` row; the master stays "one reset per nominal period").
- **The firmware change** is `CRASH` in `osc_positionIsPhase()` (Q3:
  together with P3).
- **Guard byte (Q4):**
  - `calcSampleOscBlock()` and its FM/sync/ring copies read
    `crashSample[itg + 1]`, i.e. `[32768]`, one byte past the 32,768-entry
    array. This is pre-existing, and `pos` would place the phase there
    deliberately.
  - `crashSample` is now 32,769 entries; the last equals entry 0 (0x80)
    because the sample loops.
  - The host tests already used 32,769.
- **Hardware C1–C4 passed:** `pos` 0 identical; quarter/half/127 starts;
  LFO scrubbing; knob jump smear.

**The open sample decisions D1–D10** (for real samples) are carried in
`S087_P0_OSC_TYPE_SAMPLES_WAVETABLES.md` §6.5 and summarised in §15 here.

---

## 6. Implementation schedule (`S086_P3_IMPLEMENTATION.md`)

### 6.1 Measured facts it relied on (P2 tree)

| Fact | Value |
|---|---|
| `sizeof(OscInfo)`; `tableOffset` / `pitchMod` offsets | 72; 17 / 20 |
| Voices: Drum / Snare / Cymbal / HiHat | 596 / 420 / 476 / 484 B |
| Voice render block size | always `OUTPUT_DMA_SIZE` = 32 (`mixer.c` passes it to `instrumentManager_calcSlotSyncBlock()`) |
| `osc_setFreq()` callers | only the four async functions, before the render |
| Block order (`mixer.c`) | LFO dispatch :878 → async :882 → render :965 |
| Instance-offset `TYPE_UINT8` rows | written as a plain byte (`instrumentManager_writeRuntimeInternal()`) |
| Menu code tied to cell positions | none |
| Special-tag classifier | `osc1_position` → no tag (plain `ROW` matches) |

### 6.2 Change list as scheduled

| # | Change |
|---|---|
| C1 | helpers and constants after `osc_isTableWave()` |
| C2 | layout asserts after `} OscInfo;`, with `<stddef.h>` there so the bench's extracted line ranges do not move |
| C3 | the two fields after `tableOffset`; 22 lines, so `OscInfo` 71–124 |
| C4/C5 | crash guard |
| C6–C9 | engines: the trigger line, the bend add/restore around the osc1 render |
| C10–C17 | tables and counts |
| C-T1–C-T5 | bench: the `osc_mod` range, `osc_pos`, `engine86_identity`, `elf86_p2_snapshot`/`elf86_p3_check`, `DSP_TEST.md` |
| C-D1 | `FILESYSTEM_SPEC.md` |

- Every change carried a What/Why/Inputs/Outputs/Accessors/Affiliates
  comment block for documentation in place.
- Apply the changes bottom-up within each file.
- **Schedule error found later (F5b):** C4 said the last crash data row
  ended with a comma; it did not.

---

## 7. Implementation (commit `e67879e2`) and step 0

- **Step 0:** a P2 clean build (text 658,664, ITCM 8,512) and
  `elf86_p2_snapshot`: `frozen86/elf_osc_p2.txt` (26 symbols) and
  `elf_loops_p2.txt` (61 loops).
  - The assessment confirmed both are byte-identical to dumps of a pristine
    build of `HEAD` (`703941cb`).
- **Firmware:** as scheduled, with shorter reworded comment blocks.
  - The Snare edit replaced the `//calc next osc sample` line, so the engine
    allow-list carried it (F5a).
- **Delivered build:** text 658,832, headroom 94,408, ITCM 8,512.
- **Bench:** all targets passed. The implementer did not record break
  proofs (F4).

---

## 8. Assessment (`S086_P3_IMPLEMENTATION.md` §8)

**Verified:**
- the full diff against `703941cb`;
- a clean rebuild reproduces the delivered image byte for byte;
- the oscillator functions and all 61 loops are byte-identical to P2
  (`elf86_p3_check`);
- `armcheck-oscmod` is identical to its baseline;
- the whole bench passes;
- struct sizes and page cell counts are unchanged.

| # | Severity | Finding |
|---|---|---|
| **F1** | medium | **The mixer's inlined loops moved.** The four render functions are inlined into `mixer_calcNextSampleBlock()` (flash), which grew 12,576 → 13,036 B. Opcode-sequence pairing of its loops against P2 found two real per-sample regressions. (1) The inlined FM loop with a Sine carrier (`ubfx #20`, the `calcFmSineBlock` body) went 29 → 31: two literal-pool constant reloads per sample, from flash. (2) An inlined noise loop went 15 → 16: one stack-spill reload per sample. **Cause:** the local `posBend` was live across the inlined render, raising register pressure. The oscillator gates cover only the oscillator symbols, so they missed it. |
| F2 | low | `test_osc_pos.c`: the P2 identity case was missing (the report said it existed); P1 never tested `pos` 127 or > 127; P1's expected wave set was computed with the helper under test |
| **F3** | low | the test redefined `OSC_POS_BEND_PER_STEP`/`CAP`, so the header's constants were untested. The break proof "step size ×2 in `Oscillator.h`" **passed** the bench |
| F4 | info | break proofs not recorded. Run in the assessment: 6 of 7 failed as required; "step ×2" survived (F3) |
| F5 | info | (a) Snare comment line replaced; (b) the schedule's C4 comma error; (c) comment blocks shorter than the schedule's |
| F6 | info | trigger functions +20 B each; `Snare_triggerVoice.part.0` 812 → 324 B (LTO split it differently); per trigger, not per sample |

---

## 9. Fixes after the assessment (`S086_P3_IMPLEMENTATION.md` §9)

### 9.1 F1: no live state across the inlined render

- **New field** `int8_t positionBendStep` (`OscInfo` offset 33, padding
  after `waveInterpNext`), with asserts: it follows `waveInterpNext`, and
  `waveInterpFrac` does not move.
- **API change:** `osc_positionBend()` (which returned the bend) is replaced
  by two helpers.

  | Helper | What it does |
  |---|---|
  | `osc_positionBendApply(OscInfo*, uint8_t bend)` | circular, capped step; advances `positionApplied`; stores the step; adds step × `OSC_POS_BEND_PER_STEP` to `phaseInc`. With `bend = 0` or a non-phase wave it stores 0 and only consumes the delta |
  | `osc_positionBendRestore(OscInfo*)` | subtracts the stored step from `phaseInc` and clears it |

- **Engines:**
  - Drum: `osc_positionBendApply(&osc, modMode != DRUM_MOD_SNC)` before the
    mode switch, Restore after it.
  - Snare/Cymbal/HiHat: Apply(…, 1) / Restore around the osc1 render.
- **Result** in the linked image:

  | | P2 | P3 as delivered | After the fix |
  |---|---:|---:|---:|
  | mixer function size | 12,576 | 13,036 | 13,076 |
  | inlined FM-sine loop | 29 | 31 | **29** |
  | noise loops | 15 / 17 / 18 | 16 / … | **15 / 17 / 18** |

  - The remaining textual differences are register renaming and cold
    saturation stubs.
  - One extra stack store remains, in a per-block setup path.
- **New permanent gate:**
  - `tools/dsp_test/elf_hotloops.py` with `make elf86_mixer_snapshot`
    (baseline `frozen86/elf_mixer_p2.txt`, from the pristine P2 build) and
    `make elf86_mixer_check`.
  - It pairs the mixer's per-sample do-while loops (fragments ending in a
    conditional branch to their own start, `+0`) with P2 by opcode sequence
    (registers, offsets, literals, width and flag forms folded). It fails
    when a loop grew.
  - On the delivered P3 it fails (naming the two loops); on the fixed tree
    it reports 0 grew.

### 9.2 F3: the header's constants are what the test compiles

- **New asserts in `Oscillator.h`:**
  - `OSC_POS_BEND_PER_STEP * OUTPUT_DMA_SIZE == 1 << 25`;
  - `OSC_POS_BEND_CAP` in 1..64 (`positionBendStep` is `int8_t`).
- **Makefile:** `osc_pos` cuts `op_consts.inc` out of `Oscillator.h` with
  `sed` (from `#ifndef OSC_POS_BEND_CAP` to the cap-range assert) and checks
  it was found.
- **Break proof:** "step ×2" now stops the build, both in the firmware and
  in the host test.

### 9.3 F2: `test_osc_pos.c` rewritten for the new API

| Check | What |
|---|---|
| P1 | waveforms 0–15 × `pos` 0, 1, 64, 127, 128, 255 against an independent truth table |
| P2 | held `pos` renders bit-identically to the plain loop from start + `pos << 25` (16 values × 7 waves × 8 blocks) |
| P3 | no drift on all seven phase waves; distinct Re3/Sw3 data |
| P4 | circular steps, stored step, `phaseInc` change, exact restore |
| P5 | continuity on all seven waves, and landing on the offset |
| P6 | non-phase waves: no bend, delta consumed |
| P7 | cap build (8): 8 per block, checked each block, lands |
| P8 | consume only (`bend = 0`, Drum `snc`) |

**Break proofs** (each failed as required, sources restored byte-identical):

| Mutation | Fails |
|---|---|
| `pos << 24` | P1 |
| mask removed | P1 (128/255) |
| table waves dropped | P1 |
| `CRASH` dropped | P1 |
| linear difference | P4 |
| `positionApplied` not advanced | P3 |
| step ×2 | build (assert) |
| cap ignored | P4 (cap build) |
| Restore does not subtract | P3 |
| Restore keeps the step | P3 |
| `bend` argument ignored | P8 |

### 9.4 Minors

- **F5a:** the original Snare comment line is back; the engine allow-list is
  again exactly the four P2 lines.
- **F5b:** the schedule text corrected.
- **F5c:**
  - every P3 comment block rewritten to describe the fixed code (triggers,
    renders, `OscInfo` blocks, constants, helpers);
  - the four descriptor-row blocks expanded from one-liners to full blocks;
  - no stale `osc_positionBend()` reference remains.
- **Docs:** `DSP_TEST.md` rows; `MEMORY.md`.

### 9.5 Final verification (clean build of the fixed tree)

| Gate | Result |
|---|---|
| Build | text **658,872** (+208 on P2), data 424, bss 430,296, payload 659,296, headroom **94,368**, **ITCM 8,512** (unchanged), DTCM 4,472; no warnings in the changed files |
| Sizes | `OscInfo` 72; voices 596 / 420 / 476 / 484 |
| `elf86_p3_check` | 26 symbols, 0 changed; 61 loops exact (oscillator code still byte-identical to P2) |
| `elf86_mixer_check` | 0 grew |
| `armcheck-oscmod` | identical to the frozen baseline |
| `osc_mod`, `osc_stock`, `osc_pos` ×2 | 0 failures |
| `osc86_identity`, `engine86_identity` | OK |
| `special_tags` | 159 rows / 0 |
| `stock_tables` | OK |
| `selftest`, `pack`, `postchain`, `mixer` | 0 differing |
| `octave` | 16 / 2 ulps (documented) |
| `armcheck-postchain` / `-mixer` / `-filter` | MATCH / MATCH / 81→39 |
| `filter` | S1 PASS, SDR 91.6896 / 86.5468 |

---

## 10. Hardware record

- **P2 (Re3/Sw3):** included in the P3 image, which passed.
- **P3 H1–H10 and P4 C1–C4 passed** (user, 2026-10-28, on the fixed image).
  The checks:

  | # | Check |
  |---|---|
  | H1 | old kit = `pos` 0, sound identical |
  | H2 | layouts |
  | H3 | slow/fast/jump `pos` while sounding |
  | H4 | sweep between hits |
  | H5 | LFO incl. retrigger |
  | H6 | velocity and step automation |
  | H7 | noise / user sample unaffected |
  | H8 | Morph, save/reload/power cycle |
  | H9 | Drum `fm`/`mix`/`rng`/`snc` |
  | H10 (G-C) | six-slot worst case with an LFO on every `pos`: `cpu` as P2, zero underruns |
  | C1–C4 | `Cym` |

- **As built:** the cap stays off (64); the automation glide stays (Q11/Q14:
  no change asked for).

---

## 11. Resources (trail)

| | S085 end | P2 | P3 delivered | P3 fixed (final) |
|---|---:|---:|---:|---:|
| text | 613,536 | 658,664 | 658,832 | **658,872** |
| flash headroom | 139,704 | 94,576 | 94,408 | **94,368** |
| ITCM | 7,792 | 8,512 | 8,512 | **8,512** |
| bss / data / DTCM | 430,296 / 424 / 4,472 | same | same | same |
| `OscInfo` / voices | 72 / 596 420 476 484 | same | same | same |
| descriptors D/S/C/H | 39/38/39/39 | same | 40/39/40/40 | 40/39/40/40 |
| `mixer_calcNextSampleBlock` | — | 12,576 | 13,036 | 13,076 |

**No RAM was added:** all fields are in padding.

---

## 12. Accepted behaviours and known limits (not bugs)

1. **Old kits:** `wav` 6/7 now play Re3/Sw3; user sample *n* is at `7 + n`.
   No migration.
2. **Drum slot 5** power-on seed = Re3.
3. **Levels:** Re3 ≈ 8 dB and Sw3 ≈ 2 dB quieter than Rec/Saw
   (peak-normalised).
4. **Rec/Tri band limit** is looser than Saw's; Re3 follows Saw's.
5. **Large `pos` jumps:** a 0.7 ms pitch blip that aliases (the cap is
   available, off).
6. **LFO depth on `pos`** clamps at 0/127 rather than wrapping.
7. **Step-automated `pos`** starts at the base value and glides to the step
   value within the first block (S087 stage A).
8. **Drum `snc`:** `pos` acts at the trigger only.
9. **Modulators:** Drum FM and HiHat modulators never reset; no modulator
   `pos` rows yet.
10. **Slot reload:** the first block may glide from 0 to the loaded `pos`
    (silent).
11. **`startPhase` / `Drum_setPhaseVoice()`** are unused (clean-up later).

---

## 13. Affected files and API

### 13.1 Files

| File | Change |
|---|---|
| `Core/DSPAudio/Oscillator.h` | stock IDs and range, `OSC_SAMPLE_START` 8, `osc_isTableWave()`; `OscInfo.position`/`positionApplied`/`positionBendStep`; layout asserts (`<stddef.h>` after the struct); `OSC_POS_BEND_CAP`, `OSC_POS_BEND_PER_STEP` and asserts; `osc_positionIsPhase()`, `osc_positionTrigger()`, `osc_positionBendApply()`, `osc_positionBendRestore()` |
| `Core/DSPAudio/Oscillator.c` | `osc_stockTables[]`; stock case in six dispatchers; `osc_syncWaveform()`, `osc_syncClockInc()` |
| `Core/DSPAudio/wavetableStock.c` (new) | generated `sq3Table`, `sw3Table` |
| `Core/DSPAudio/wavetable.h` | two externs |
| `Core/DSPAudio/Samples.c/.h` | `crashSample[32769]`, guard 0x80 |
| `DrumVoice.c`, `Snare.c`, `CymbalVoice.c`, `HiHat.c` | `osc_isTableWave()` start phase; `osc_positionTrigger()` at the hit; Apply/Restore around the osc1 render |
| `*Parameters.c/.h` (×4) | `osc1_position` row, enum, counts, page 1 |
| `Core/Menu/MenuText.h`, `menu.c` | 8 waveform names; `Oscillator.h` include and count assert |
| `Makefile` | `wavetableStock.c` |
| `tools/gen_stock_tables.py` (new), `tools/plots/` (copied + `plot_stock_tables.py`, `stock_tables_levels.png`) | generator and plots |
| `tools/dsp_test/` | `snapshot86.sh`, `frozen86/` (sources, `elf_osc.txt`, `elf_loops.txt`, `elf_osc_p2.txt`, `elf_loops_p2.txt`, `elf_mixer_p2.txt`, `identity_allowed.txt`, `engine_identity_allowed.txt`, `armcheck_oscmod_baseline.txt`); `elf_loopdump.py`, `elf_hotloops.py`; `test_osc_stock.c`, `test_osc_pos.c`; `extract.py`; `test_osc_mod.c` (host tables, include); Makefile targets; `DSP_TEST.md` |

**New targets:** `stock_tables`, `osc_stock`, `osc_pos`, `osc86_identity`,
`engine86_identity`, `elf_osc86_baseline/check`, `elf_loops86_baseline/check`,
`elf86_p2_snapshot`, `elf86_p3_check`, `elf86_mixer_snapshot/check`.

### 13.2 API

| Symbol | Contract |
|---|---|
| `SQ3`, `SW3`, `OSC_STOCK_FIRST`, `OSC_STOCK_LAST` | stock table wave IDs and their range |
| `osc_isTableWave(w)` | 1 for TRI/SAW/REC/stock |
| `osc_positionIsPhase(w)` | 1 for SINE/CRASH/table waves |
| `osc_positionTrigger(osc)` | returns `(position & 127) << 25` for phase waves (else 0); sets `positionApplied` |
| `osc_positionBendApply(osc, bend)` / `osc_positionBendRestore(osc)` | the per-block bend pair; must surround exactly one osc1 render in the same function |
| `OSC_POS_BEND_CAP` (64 = off), `OSC_POS_BEND_PER_STEP` | asserted constants |
| `sq3Table`, `sw3Table` | `const int16_t [11][1024]` |
| `crashSample[32769]` | 32,768 points + guard |

---

## 14. Specification reference updates (S086 close)

- **`INSTRUMENTS_DSP_REFERENCE.md`:**
  - §5 engines: trigger `pos`, the render bend;
  - §5.5;
  - §6.1 oscillator: stock waves, generator, dispatch, `pos`, crash guard;
  - §8 costs;
  - §9.1/§9.3 how to add a stock wave and use the `pos` helpers;
  - §10 pitfalls (F1, restore, header constants);
  - §11 history.
- **`CPU_USE_DSP_AUDIT.md`:** items 41–44.
- **`STORAGE_SRAM_MANIFEST.md`:** build figures, ITCM table, flash tables,
  `OscInfo` padding.
- **`FILESYSTEM_SPEC.md`:** `osc1_position`, counts, `wav` values and the
  sample renumbering.
- **`MIDI_SPEC.md`** §5.5: `wav` values, `osc1_position`.
- **`OSC_INTERP_AUDIT.md`:** stock waves in the domain; `pos` with
  interpolation.
- **Tools:** `tools/dsp_test/DSP_TEST.md` (targets), `tools/plots/README.md`.
- **`MEMORY.md`;** volatile notes.

---

## 15. Carry-forward

### 15.1 Next session: `S087_P0_OSC_TYPE_SAMPLES_WAVETABLES.md`

| Stage | Content |
|---|---|
| **A** | step automation lands before its trigger (D6) |
| **B** | `typ` rows, modulator `pos` rows, the modulator reset decision (D10) |
| **C** | catalogue v2 + installer + RAM release (≈ −16 KB). The spike's 61,120 B stock block at the sample-flash floor is no longer needed |
| **D** | `smp`/`slp` (Q20.12, padding, fixed loop region, copies, sync-master rules) with `pos` D1–D5, D7–D9 |
| **E** | `wtb` (power-of-two frames, one-frame base loop, blending through the interpolation pool) |

### 15.2 The sample `pos` decisions (recommendations; detail in S087 P0 §6.5)

| # | Decision |
|---|---|
| D1 | Q20.12: start = ((`pos` × len) >> 7) << 12; bend per sample = d × len (exact ≤ 2²⁰ points) |
| D2 | fixed loop region; `pos` = fraction of the whole sample |
| D3 | linear difference for samples (circular only for periodic content) |
| D4 | clamp at 0; past the end ends the hit; no revival |
| D5 | a time-based cap for samples (e.g. ≤ 4× speed) |
| D6 | automation before trigger (stage A) |
| D7 | re-anchor on any type/source change |
| D8 | step in `positionBendStep`, length recomputed at restore |
| D9 | the sync reset target = the `pos` start |
| D10 | modulators that never reset (stage B) |

### 15.3 Other items

- **`ReA`/`TrA`/`SwA`:** one generator line plus ID, name and table entry
  each (+22,528 B each). Decide after the S087 flash budget. Also the level
  question (§3.3).
- **Flash:** 94,368 B headroom. The sector-7 option (§3.6) if it runs out.
- **The spike trees were removed** from the working tree at the close; the
  pwm spike's gitlink deletion is unstaged. The planning documents
  `S086_P1_OSC_TYPE_EXPANSION.md`, `S085_SAMPLE_INDEX_SPEEDUP.md` and
  `S085_OSC_MOD_CHERRYPICK.md` were deleted from the root. Read them with
  `git show e56bb957:…`, `b450d76a:…`, `1ea234ea:…`.
- **Disposable (user):** the five S086 task documents in the root (this log
  and the specifications carry them).
- **Still open from earlier sessions:** see the volatile notes (S084 V1–V7
  status, F5, S082 P3 A2, S081 P3 A-1, the production build re-measure).
