# S084 P2 — Voice Expansion 2: Sample Flash, Wavetables, Stock Waves, Transients, Drive Types

**Session:** 084, phase P2 (general plan; a full implementation schedule is
written when P2 starts, after P1 is on hardware)
**Branch:** `dev-ph6-midi`
**Date:** 2026-10-25
**Plan of record:** `S084_P1_VOICE_EXPANSIONS.md` (r7). "Plan §x" refers to
it.
**Depends on:** `S084_P1_IMPLEMENTATION.md`:

- the oscillator types and dispatch;
- `OscillatorSample.c` (Q20.12);
- `OscillatorPwm.c` (the `pq3`/`pw3` fallbacks this phase replaces);
- `InstrumentCellFormat`;
- the deferred swap.

**Followed by:** `S084_P3_VOICE_EXPANSION_3.md`.

---

## 1. Goal

Make the sample flash hold everything the voices need beyond firmware
tables:

- the generated stock waves;
- user samples with names and gains;
- user wavetables stored exactly as supplied.

Then build on that:

- the `wtb` oscillator type;
- the new stock waves;
- the transient type row (`trs`/`wav`);
- the 14 filter drive types.

---

## 2. Work packages

### P2-A Sample flash catalogue v2 (plan §4.1.4, §4.1.5, §6 Q7/Q8)

- **New file `Core/SampleRom/SampleInstall.c/h`:**
  - parses every accepted WAV form into catalogue entries: mono/stereo;
    8/16/24-bit; any rate for wavetable frames; mono 16-bit 44.1 kHz for
    samples, as today;
  - writes catalogue v2 at the top of sample flash.
- **Catalogue v2 contents:**
  - a version header;
  - sample entries (offset, size, display name, peak-normalisation gain
    byte);
  - wavetable entries (folder name, frame count, frame length, bit depth,
    offset);
  - the stock-wave block.
- **Old installs** are rejected by version. Nothing is retained (Q7).
- **`SampleMemory.c/h`:**
  - the read API over v2 (count, entry, name, gain, wavetable entry);
  - the SRAM1 caches (5,040 B) are removed and the memory-mapped catalogue
    is read directly (release, no reuse).
- **`filesystem.c`:**
  - the install walks `samples/` and then `wavetables/` (subfolders,
    recursively);
  - natural, numeric-aware order through `storage_naturalCompare()` (new in
    `storageTypes.c`);
  - file data goes to `SampleInstall`;
  - the `/loops` pass is removed.
- **Limits:**
  - up to 233 samples (transient `wav` list ≤ 256);
  - up to 128 frames per wavetable;
  - a wavetable that does not fit is skipped whole and reported on the LCD.
- **Install-time scratch:** none. Wavetables are copied as supplied (W1),
  and stock waves are programmed as they are computed.

### P2-B Stock waves `sq3 sw3 sqa tra swa` (plan §4.1.2, D2, W4)

- **Recipe constants:** `tools/gen_stock_recipes.py` reads
  `tools/plots/stock_wave_harmonics.csv` and writes
  `Core/SampleRom/StockWaveRecipes.c` (flash constants, ≤ 10 KB, quantised
  if needed).
- **Generation:** `Core/SampleRom/StockWaveGen.c/h` generates the
  oversampled-octave layout at install time: 11 levels; each level's length
  is the power of two ≥ 8 × its harmonic count, at most 1,024 points.
  - 12,224 B per wave, 61,120 B in total, at the start of sample flash.
- **Read access:** `Core/DSPAudio/StockWaves.c/h` provides level offsets and
  lengths, level selection and the fallback (`Rec`/`Saw`/`Tri` until the
  first install).
- **`Oscillator.c`:** the `wav` list grows to 11 (`OSC_STOCK_WAVE_COUNT`).
  The renderer reads stock tables of variable length per level, with the
  same 64-bit multiply index as `wtb`.
- **`OscillatorPwm.c`:** `pq3`/`pw3` switch from the `Rec`/`Saw` fallbacks
  to `sq3`/`sw3` with their natural split points (0.78, 0.42).
- **`MenuText.h`:** `waveformNames` gains `sq3 sw3 sqa tra swa`.

### P2-C `wtb` oscillator type (plan §4.1.4, W1–W3)

- **New file `Core/DSPAudio/OscillatorWavetable.c/h`:**
  - any frame length through one 32 × 32 → 64-bit multiply (index and
    fraction);
  - 8/16-bit frames;
  - two-frame read + blend, always both;
  - the position (`wav` 0..127 across the table's frames) is ramped per
    sample;
  - render in ITCM.
- **No mips:** single level (W3 moot).
- **`OSC_TYPE_WTB` = 4**, appended. `src` = wavetable index (short name
  `w01`…; the folder name in the single-parameter view, via
  `InstrumentCellFormat`).
- **Writers and domains:** the P1 writers and domains gain the `wtb` rules
  (wave maximum 127, source maximum = wavetable count − 1).

### P2-D Transient type row `trs` / `wav` (plan §4.3.1, §6 Q13–Q15, r4 Q27)

- **New row `transient_type` `trt` per instrument** (not modulatable, not
  automatable; one descriptor row each).
  - `trs`: `Snp`, `Ofs` (and future shaping modes).
  - `wav`: the 12 stock PCM transients, then samples (≤ 233), then the 11
    stock oscillator waves as 50 ms pitched bursts with a fixed decay (Q14).
  - `buf`: reserved; P3 fills it.
- **`transientGenerator.c/h`:**
  - samples at Q20.12 with the catalogue gain (peak-normalised; applied
    everywhere, Q15);
  - the stock-wave bursts.
  - Cost stays ≤ today's transient path (≈ 22 instructions, 0 divisions;
    measured with `fpseq.py`).
- **The `transient_wave` row's meaning depends on `trt`**
  (`InstrumentCellFormat`, as for the oscillator `wav` row). Automation stays
  inside the type.

### P2-E Filter drive types (plan §4.4, r3 naming)

- **New row `filter_drive_type` `dty`** per instrument (automatable, not
  Morphable or LFO).
- **New file `Core/DSPAudio/DriveShaper.h` (+ `.c`):**
  - the 14 shapers `sft rct art thf fdc bsw tsw lsw lsq sfd afd asf chb rsb`
    as `static inline` `g(u)` functions;
  - the shaper state struct (≤ 28 B, inside the engine);
  - the level trims from `tools/plots/plot_waveshapers.py`;
  - block-rate helpers (tracker `1/period`).
- **`ResonantFilter.c`:**
  - the voice ZDF loop takes the drive type and the shaper state;
  - type 0 is the existing code path, untouched;
  - Stage A computes only `1/D1` for non-Padé types (still two divisions per
    sample);
  - the float twin (Effects) keeps type 0 only (documented exception).
- **Code shape:** either one unswitched loop per type or a per-sample switch
  on a block-constant value. Measure both with `fpseq.py`; the target is
  ≤ `sft` + 6 instructions per sample.

---

## 3. Descriptor budget after P2

| Instrument | After P1 | `trt` | `dty` | After P2 (cap 64) |
|---|---:|---:|---:|---:|
| Drum | 44 | +1 | +1 | 46 |
| Snare | 41 | +1 | +1 | 43 |
| Cymbal | 46 | +1 | +1 | 48 |
| HiHat | 46 | +1 | +1 | 48 |

P3 adds `fxt` + `amt` (+2 each) for the plan's totals (48 / 45 / 50 / 50).

---

## 4. Memory (to approve at schedule time)

| Item | Region | Estimate |
|---|---|---|
| Sample caches removed | SRAM1 | −5,040 B (released) |
| Catalogue v2 index | sample flash (top) | ≈ 6–8 KB |
| Stock waves | sample flash | 61,120 B |
| Shaper state | inside the engine structs | 0 static |
| `OscillatorWavetable` render code | ITCM | +1–1.5 KB |
| Recipes and generator, installer, shapers, menu | program flash | ≈ 25–35 KB |

---

## 5. Risks

| Risk | Mitigation |
|---|---|
| Catalogue v2 invalidates every install after the flash | Clear boot and Load:[Samples] messages; `tools/sampleoptimize.py` prepares the card |
| Install time grows (stock generation, more files) | LCD progress during the modal install |
| Index-based references shift when the card's set changes | Documented; name references remain a follow-up |
| Drive-type loops grow flash | Measure unswitched against switch; pick by `fpseq.py` and flash cost |
| Aliasing (folders, swap, trackers) | Pre-filter placement; documented character |

---

## 6. Verification (outline)

- **Host:**
  - the installer parses every accepted WAV form;
  - catalogue round-trip;
  - generated stock tables match the recipe;
  - `wtb` reads at any frame length;
  - shapers match `plot_waveshapers.py`;
  - `sft` is bit-identical to today;
  - transient costs ≤ cap.
- **Hardware:**
  - install with samples + `wavetables/` (your `hvoice` set, raw and after
    `sampleoptimize.py`), then reboot;
  - the `wav` list shows the new waves;
  - `wtb` sweeps stay inside the table;
  - transient `trs`/`wav` with sample names and matched levels;
  - drive types by ear;
  - the worst-case Scene CPU.

---

## 7. Open items for the P2 schedule

- Confirm the LCD wording for "install invalid after update" and for skipped
  wavetables.
- Pick the drive-type code shape after measurement.
- Decide how the 50 ms stock-wave transient bursts decay (fixed exponential,
  proposed τ ≈ 15 ms).
