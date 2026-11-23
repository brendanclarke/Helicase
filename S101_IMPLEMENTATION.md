# S101 — Implementation log (second round: the flash reductions)

**Date:** 2026-11-21. **Branch:** `dev-ph7-alkaline`, base `8476271a` ("delay
tweaks"). **Status:** implemented, uncommitted, built and gated; **not yet
on the device.**

**Scope:** every code change of this round.
- **The plan:** `S101_P2_REDUCTIONS.md` §B.3.
- **The user's instruction (2026-11-21):** "FS_OS=1 is fine, put that in
  directly, and the waveform tables savings and the transients. Put the
  transient waves in 'SD_CARD' named 'LXR*.wav' as I've done for the cymbal."
- **The first round** (B1 SD poll burst, B3 panel scan, D1 half-rate Alkaline
  chorus, S1 probe maxima) is logged in `S101_P1_IMPLEMENTATION.md`. This
  tree contains both rounds.

| § | Change | Flash | Status |
|---|---|---:|---|
| 1 | F1 `FS_OS=1` by default | −36,896 B (on its own) | done |
| 2 | F4 transients to the card (`LXR*.wav`) | −26,460 B and the click loop | done; needs `Load:[Samples]` (§6) |
| 3 | F3 stock tables: one array, saw/rec level 0 dropped | −4,096 B | done |
| 4 | Comment references to the renamed P1 documents | 0 | done |
| — | **Image** | **733,548 → 666,700 B (−66,848 B); ITCM 15,432 → 15,424 B** | `build/LXRV2_lxr02.img` |

The flash totals of §1–§3 overlap slightly (LTO layout moves about 1 KB
between builds), which is why the sum of the rows is not the image total.

---

## 1. F1 — `filesystem.c` at `-Os` by default

**File:** `Makefile`, the `FS_OS` block (before `ifeq ($(FS_OS),1)`).
- `FS_OS ?= 0` → `FS_OS ?= 1`.
- **The comment:** the S097 P3 history (the stumble was a baseline defect;
  the §11 image ran clean on the device, §12) and the S101 decision and
  figure. `make all FS_OS=0` builds the `-O2` filesystem.
- **Nothing else changed:**
  - `FS_OS_BUILD` keeps the TIM3/MIDI-reachable helpers at `-O2`
    (`FS_HOT_O2` in `filesystem.c`);
  - `COLD_LAST` keeps the cold objects after the DSP objects;
  - changing `FS_OS` needs `make clean` (the final build was `rm -rf build
    && make all`).

**Measured** (a scratch build of the S101 P1 tree with `FS_OS=1`): 696,652 B
against 733,548 B, **−36,896 B**. Gates on that image:
- `elf_p4_check`: 85/85 oscillator loops exact; the mixer's 59 hot loops
  equal;
- `elf_s099_check`: 15/15;
- the new chorus loops unchanged.

**Not measured yet:** the AutoSave drain duration at `-Os` (device step,
§6). The SD poll itself is `asyncfatfs.c`, already `-Os`.

---

## 2. F4 — the built-in transients move to the card

### 2.1 The card files

**New: `SD_CARD/samples/LXR*.wav`**, twelve files, one per former built-in
row. Each is a mono, 16-bit, 44,100 Hz PCM WAV of exactly 2,205 frames,
`value << 8` of the row's signed 8-bit samples, so the data is bit-exact
(verified by reading each file back).

| Row (old value) | File | sha256 (first 16) |
|---|---|---|
| 0 (2) Clk | `LXRClk.wav` | 4409db1087ad7f16 |
| 1 (3) Ck2 | `LXRCk2.wav` | 8d7265f7e590a9bc |
| 2 (4) Tik | `LXRTik.wav` | 7c495410c4b545ff |
| 3 (5) Kik | `LXRKik.wav` | 98aff0688dcd0a46 |
| 4 (6) Rim | `LXRRim.wav` | a3181fa85447bb43 |
| 5 (7) Drp | `LXRDrp.wav` | 6b343e106e3742b1 |
| 6 (8) Hat | `LXRHat.wav` | 8efacb6596b04c5e |
| 7 (9) Clp | `LXRClp.wav` | b6c39e430a7c4cf0 |
| 8 (10) Kk2 | `LXRKk2.wav` | 1b87f52fcc4f54f2 |
| 9 (11) Snr | `LXRSnr.wav` | afc83a52778ebcc6 |
| 10 (12) Tom | `LXRTom.wav` | 179710985356f657 |
| 11 (13) Sp2 | `LXRSp2.wav` | 0f3bb89a8dee5c62 |

- **Provenance:** `Core/DSPAudio/transientTables.c` at `8476271a`
  (`transientData[12][2205]`), dumped by a host build of that file.
- **The header layout** is the same as `LXRcym.wav`'s (RIFF, `fmt ` 16 B,
  PCM, `data`).
- **Not copied** to the other card folders (`SD_CARD_S100`, `SD_CARD_S101`,
  …): a card used with this image needs them (§6).

### 2.2 The code

**`Core/DSPAudio/transientGenerator.c`:**
- **Removed:**
  - `transient_calcClickBlock()`: the built-in click loop (Q20 phase,
    nearest read of `transientData`, 22 instructions a sample, inlined into
    every Drum/Snare/Cymbal/HiHat render);
  - `transient_calc()`: the per-sample legacy form; no caller.
- **`transient_calcBlock()`:** 0/1 (Snp, Ofs) render silence as before;
  every other value is a card sample (`transient_calcUserBlock()`). The
  comment block records why, and that there is no migration.
- **`transient_calcUserBlock()`:** with no sample installed, the block is
  silent and the row value is **kept**.
  - Before, it clamped to the last built-in click.
  - With no built-in clicks left, a clamp to the entry below would select
    Ofs, the offset mode, which changes the oscillator start phase (Drum,
    Snare, Cymbal and HiHat test `transGen.waveform == 1`).
  - The comment block records this.
- **`transient_setWaveform()`'s comment and the sample block's comment:**
  the list layout (Snp, Ofs, then the installed samples).

**`Core/DSPAudio/transientGenerator.h`:**
- the `#include "transientTables.h"` and the `transient_calc()` declaration
  removed;
- `TRANSIENT_USER_FIRST` is `2u` (was `NUM_TRANSIENTS + 2u` = 14);
- the list-layout comment and the entry-count comment ("at least 2") updated.

**Deleted** (`git rm`; recoverable from `8476271a`):
- `Core/DSPAudio/transientTables.c`: the 12 × 2,205 int8 table and the
  unused `transientVolumeTable[69]`;
- `Core/DSPAudio/transientTables.h`.

The `Makefile` drops `Core/DSPAudio/transientTables.c` from `SRCS`.

**`Core/Menu/MenuText.h`:** `transientNames` is `{2}, Snp, Ofs`, with a comment
naming the former twelve and their files.

**`Core/Menu/menu.c`:**
- the `_Static_assert` on `transientNames` (its message now "Snp, Ofs and
  nothing else");
- the MENU_TRANS name comment: the installed one-shots show as `sNN` from
  value 2.

### 2.3 Behaviour changes (no migration: the standing rule)

1. **Stored transient values.**
   - A stored `transient_wave` of 2..13 now selects installed sample 0..11.
   - With the `SD_CARD` sample set (§2.4) those are `cb_01` … `LXRClk`, not
     the former click of that number.
   - Every Drum, Snare, Cymbal and HiHat preset that used a built-in click
     needs its transient re-selected (the clicks are `LXRClk` … `LXRTom`,
     shown as `sNN`).
2. **Levels.** The installer normalises each sample's peak (gain code, gain
   ≥ 1, 1/24-octave steps). Rows whose peak is 125–128 play at the former
   level within 0.1 dB. Three rows had lower peaks and play louder:

   | Row | Peak | Approximate gain |
   |---|---:|---:|
   | Drp | 115 | +0.9 dB |
   | Hat | 114 | +1.0 dB |
   | Snr | 96 | +2.3 dB |

   The gain law cannot attenuate, so this cannot be matched in the file.
3. **The read.** The sample path interpolates linearly (Q19.12) where the
   built-in loop read the nearest sample (Q20): a tiny smoothing at
   non-unity pitch.
4. **The end.** The installer appends two zero frames after every sample, so
   the end hold is silent, as the built-in `(phase < length)` gate was.
5. **No samples installed:** every transient value ≥ 2 is silent (§2.2).

### 2.4 The sample order on `SD_CARD` (and every stored sample index)

`Load:[Samples]` sorts naturally and case-insensitively
(`storage_naturalCompare()`). The new files sort among the `l` names:

| Index | File | |
|---:|---|---|
| 0–9 | `cb_01` … `kick_01` | unchanged |
| 10–12 | `LXRCk2`, `LXRClk`, `LXRClp` | new |
| 13 | `LXRcym` | was 10 |
| 14–22 | `LXRDrp`, `LXRHat`, `LXRKik`, `LXRKk2`, `LXRRim`, `LXRSnr`, `LXRSp2`, `LXRTik`, `LXRTom` | new |
| 23–31 | `rim_01` … `snare_07` | were 11–19 (+12) |

**No migration:** a stored oscillator, modulator or Tetrakt sample selection
of `LXRcym` or anything after it (`rim_*`, `snare_*`) now plays a different
sample (the S094 crash precedent, where entries shifted by 1).

### 2.5 CPU

- **The click loop leaves the mixer:** the three `ldrsb … lsls #8` loops
  (23 instructions) inlined in the voice renders. The remaining transient
  path is `transient_calcUserBlock()` (20 instructions a sample, out of
  line, unchanged).
- **The dearest transient path per voice** was the click (22) and is now the
  sample path (20): a voice's worst case can only fall.

---

## 3. F3 — the stock wave tables in one array, saw/rec level 0 dropped

### 3.1 Design

**Finding:**
- the 1,024-point tables hold at most 511 harmonics;
- levels 0 and 1 both reach that limit (the generator's `level H`: 511,
  511, 337, …), so they differ only by edge ripple (saw 1,955 LSB, rec
  2,053 LSB);
- level 0 serves only fundamentals below 16.35 Hz (`osc_octaveEdgeHz[0]`).

**Layout** (defined C, every pointer inside one object):
- `stockTableRows[31][1024]`: triTable levels 0..10, recTable levels
  1..10, sawTable levels 1..10;
- `triTable` / `recTable` / `sawTable` become row pointers at rows 0, 10 and
  20 (`wavetable.h`), so every reader's `table[tableOffset][i]` is unchanged;
- `recTable[0]` and `sawTable[0]` name other tables' level 10: in bounds,
  never read;
- triTable keeps its level 0 because the array must start somewhere, which
  also keeps every triangle exact (§3.4).

**The rule:** a saw or rec read lifts octave 0 to 1 where its table is
chosen; a triangle (and the Alkaline square, a separate array) keeps 0.

### 3.2 Firmware files

**`Core/DSPAudio/wavetable.c`:**
- the three `[11][1024]` arrays replaced by `stockTableRows[STOCK_TABLE_ROWS][1024]`
  (31 rows, each labelled `[n] <table> level k (midi note, top Hz)`);
- the comment block: what, why, inputs, accessors, the lift sites;
- the numbers are levels 1..10 byte for byte (law STOCK-ROWS, §3.5).

**`Core/DSPAudio/wavetable.h`:**
- `STOCK_TABLE_ROWS 31u`, `extern const int16_t stockTableRows[…][1024]`;
- `#define triTable (stockTableRows)`, `recTable (stockTableRows + 10)`,
  `sawTable (stockTableRows + 20)`;
- the comment: never index recTable or sawTable with 0.

**`Core/DSPAudio/Oscillator.c`:**
- **`osc_calcWavetableFreqValue(osc, f, triCode)`:**
  - sets `tableOffset = (idx == 0 && waveform != triCode) ? 1 : idx`;
  - `triCode` is `TRI` from `osc_setFreq()` (wav and the modulator list) and
    `OSC_PWM_TRI` from the pwm bind in `osc_userBind()`;
  - the redundant `> 10` clamp is gone (the count never exceeds 10).
- **`osc_setFreq()` (ITCM):** the three identical `case SAW` / `TRI` / `REC`
  labels are folded into `default:`, one call site.
- **`freqToTableIndex()`:** unchanged (0..10); its "Outputs" comment notes
  the lift.
- **`osc_setTableFreq()`:** unchanged in effect (0..10); a comment says the
  Alkaline lifts at its saw reads.

**`Core/DSP/Instruments/Alkaline/AlkalineVoice.c`:**
- **`alk_renderWav()`:** the fused read lifts octave 0 to 1 for Saw only
  (not `ALK_WAVE_SQUARE` or `ALK_WAVE_TRI`);
- **the `ALK_FUSED_OSC 0` branch** (not built) lifts `osc.tableOffset` before
  the generic reader;
- **the pwm Saw / Sqr read** (`DiodeLadder_renderB2OscPwm`, which reads
  `sawTable`): `tableOffset ? tableOffset : 1`;
- **not changed:** the pwm triangle (`triTable[r]`, r = offset + lift) and
  the chop (r = offset + 2).

**`Core/DSP/Instruments/Colloid/ColloidVoice.c`:**
- **`col_octave()`:** unchanged (0..10);
- **`col_oscControl()`'s stock row:** `stock[(o == 0 && stock != triTable) ? 1
  : o]`;
- **the pwm triangle row** (`col_octave + lift`): unchanged;
- **the two default rows** `sawTable[0]` → `sawTable[1]` (the `wtb` fallback
  when no table is bound, and the init);
- **the chop's row arithmetic** `(row - sawTable[0]) >> 10`: unchanged
  (pointer arithmetic inside the one array, on rows ≥ 1).

### 3.3 Tools and benches

**New: `tools/plots/archive/stock_tables_s100.c`:** the three full
`[11][1024]` tables as they were at `8476271a`, verbatim, with a header.
It is the reference copy outside the build.

**Repointed to that archive:**
- `tools/plots/gen_stock_tables.py` (`TABLE_PATH`, and its error message);
- `tools/plots/gen_pwm_tables.py` (`WAVETABLE_C`);
- `tools/plots/plot_303_raw_vco.py` (the saw/rec reads);
- `tools/plots/plot_stock_tables.py` follows `gen_stock_tables.py`.

They measure band limits from every level, so their outputs are unchanged:
`gen_stock_tables.py --check` C4 OK, `gen_pwm_tables.py --check` OK.

**`tools/dsp_test/Makefile`:**
- `STOCK_FULL := …/stock_tables_s100.c`;
- the `alkaline`, `colloid` and `pwm_dump` targets extract `sawTable`,
  `triTable` and `recTable` from it (`sine_table` still from `wavetable.c`);
- the new `stock_rows` target, run by `stock_tables`.

**New: `tools/dsp_test/stock_rows_check.py`: law STOCK-ROWS.**
- **What:** the firmware's `stockTableRows` must equal tri 0..10 + rec 1..10
  + saw 1..10 of the archive, byte for byte, in order.
- **Break:** `--break` swaps two rows and must fail (`stock_rows_break:
  caught`).

**`tools/dsp_test/modules/alkaline.c`, law AL-O:**
- **The law:** `osc_setTableFreq()` must give `osc_setFreq()`'s phaseInc and
  tableOffset for SAW.
- **The change:** `osc_setFreq()` now lifts SAW's 0 to 1, while
  `osc_setTableFreq()` keeps 0..10 (the Alkaline lifts at its saw reads). The
  law now compares the lifted offset, in both its note loop and its dense
  sweep, and its header says so.
- **Proof that it still discriminates:** the bench built with the pre-S101
  rule (`tableOffset = idx` patched into the generated `Oscillator.host.c`)
  fails AL-O.

### 3.4 What the device-free checks caught on the way

1. **The first form clamped every table** (triangle included) at the three
   index computations. **CO-PWG failed 4 of 4 runs:**

   | Pwm Tri swept third difference | Before | First form |
   |---|---:|---:|
   | At the bench's sub-16 Hz note | 0.0026–0.0028 | 0.0040–0.0051 |

   - The bench's note was below 16.35 Hz, and the pwm triangle's lift then
     reached higher triangle levels, whose steps are larger.
   - The triangle never needed the clamp (it keeps level 0). The lift was
     moved to the saw/rec table choices only: CO-PWG 0 failures in 3 runs, and
     its two break builds still caught.
2. **ITCM.**
   - **A runtime triangle test** inside `osc_calcWavetableFreqValue()`
     (inlined into the ITCM `osc_setFreq()`) made GCC build a second copy of
     the unrolled ten-compare count (20 `vcmpe`; `osc_setFreq` 544 → 780 B,
     ITCM +232 B).
   - **Constants per switch case** did the same; an `asm` barrier made it
     worse (1,016 B).
   - **Folding the three identical cases into one call site** fixed it:
     `osc_setFreq` 540 B (−4 B), ten compares, ITCM 15,424 B (−8 B against
     the baseline).

### 3.5 Sound

- **Saw and rec below 16.35 Hz** (sub-audio fundamentals: very low FM
  modulators and drones) read level 1, which differs from level 0 only by
  edge ripple (both hold 511 harmonics).
- **The triangle** (wav, modulator, pwm, Alkaline, Colloid) and **the
  Alkaline square** are exactly as before.

---

## 4. Comment references to the renamed P1 documents

You renamed `S101_CPU_INVESTIGATION.md` → `S101_P1_CPU_INVESTIGATION.md` and
`S101_IMPLEMENTATION.md` → `S101_P1_IMPLEMENTATION.md`. The references were
updated:
- **in code comments:** `Core/Hardware/timebase.c`,
  `Core/Hardware/SD/filesystem.c`, `Core/DSP/Instruments/Chorus/BbdChorus.h`,
  `main.c`, `config.h`;
- **in the documents:** `S101_P2_REDUCTIONS.md`,
  `S101_P1_IMPLEMENTATION.md`, `S101_P1_CPU_INVESTIGATION.md`.

No code changed.

---

## 5. Gates

### 5.1 Build

`rm -rf build && make all && make img` (FS_OS changed), then incremental
rebuilds per §3.4 step:

| | S101 P1 image (B1 B3 D1 S1) | **This image** |
|---|---:|---:|
| Flash | 733,548 B | **666,700 B** (headroom 86,964 B) |
| ITCM | 15,432 B | **15,424 B** |
| DTCM statics | 4,396 B | 4,396 B |
| FXBUF margin | 3,776 B | 3,776 B |
| `.data` / `.bss` | 436 / 288,996 B | 436 / 288,916 B |
| RAM symbols (name + size) | 977 | 978: one new 2-byte static `op_instrument_load_index` (`filesystem.c` at `-Os` keeps it out of line); every other symbol identical. `.bss` −80 B is layout. No new RAM. |

- **Warnings:** none in the changed files beyond the pre-existing ones.
- **Image:** `build/LXRV2_lxr02.img` (666,700 B payload).

### 5.2 DSP loops

Against the snapshot of the pre-S101 baseline image (session scratchpad):

**`elf_p4_check` (oscillators): 85 baseline loops, 83 exact.**
- **renamed:** `transient_calcUserBlock`, registers only, 20 instructions
  both;
- **"missing":** `osc_setFreq`'s backward-branch fragment (a per-block
  switch fragment, not a per-sample loop; the new `osc_setFreq` has none).

**`elf_s099_check` (engines): 15/15 exact.**

**The mixer** (`mixer_calcNextSampleBlock`, `elf_hotloops.py` plus a
register-normalised pairing):
- 52 loops are identical in shape;
- **removed:** the three 23-instruction inlined click loops (`ldrsb`, the
  int8 read);
- **one instruction shorter:** some SRR decimator copies;
- **equal counts:** the rest (literal offsets only);
- a per-block FxBuffer switch chain (`mov.w r, #0x1140; mla …`, 7–27
  instructions) appears as backward-branch fragments (not per-sample);
- **no loop is longer.** The heuristic "1 grew" pairs a 14-instruction loop
  with the decimator's 19-instruction path, which is textually identical in
  both images.

**Out-of-line DSP functions:** none became inlined (the function-set diff
shows only UI and storage symbols moving, from `-Os`).

### 5.3 Host suite

`make -k -C tools/dsp_test all`: the only failures are the two pre-existing
ones, `voice_control` 14 and the `alkaline` golden file (AL-OD0). 89 break
proofs are caught. The new laws pass:
- **STOCK-ROWS,** and its break caught;
- **gen_stock_tables C4,** pwm tables OK;
- **CO-PWG:** 0 failures, both breaks caught;
- **AL-O:** 0 failures, failing under the old rule (§3.3).

`crumpbit_break25` reports a bus error inside its break build; that is the
break failing as intended ("caught (law CB23 fails as expected)"), as
before.

---

## 6. On the device

1. **The card.** Copy the twelve `SD_CARD/samples/LXR*.wav` files to the
   card's `samples/` folder (`SD_CARD_S101` does not have them), then flash
   `build/LXRV2_lxr02.img`.
2. **Run `Load:[Samples]` once.**
   - The transients are now card samples; without an install the transient
     rows ≥ 2 are silent.
   - Expect the order of §2.4: sample selections after `kick_01` shift by
     12, `LXRcym` by 3.
3. **Check:**
   - a Drum/Snare/HiHat transient on `LXRClk` … `LXRTom` against your
     memory of the old click (Drp, Hat and Snr up to 2.3 dB louder);
   - the transient `frq` row at several pitches;
   - an AutoSave save while playing (the `-Os` drain: duration in the `f`
     records);
   - Load/Save browsing (the `-Os` filesystem).
4. **The S1 records** (first round) are in this image too: the worst-case
   Scene of `S101_P2_REDUCTIONS.md` §A.9 can be measured now.

---

## 7. Statements this makes out of date (for you to act on)

- **`MEMORY.md` "Sample Flash":** the card now carries the transients
  (`samples/LXR*.wav`); the transient list is Snp, Ofs, then the installed
  one-shots (`TRANSIENT_USER_FIRST` 2).
- **`MEMORY.md` "Measuring flash" / Open Items "Flash headroom (S099)":**
  666,700 B (headroom 86,964 B); `FS_OS=1` is the default.
- **`MEMORY.md` "Sine table" / stock tables:** `stockTableRows[31][1024]`;
  sawTable/recTable have no level 0 (lift to 1 at the saw/rec table
  choices).
- **`INSTRUMENTS_DSP_REFERENCE.md`, `MENU_UI_SPEC.md`:** the transient list
  (§2) and the stock-table layout (§3).
- **`STORAGE_SRAM_MANIFEST.md` / `FILESYSTEM_SPEC.md`:** the default
  `FS_OS=1`; the `samples/` folder now holds required firmware data (the
  `LXR*` set).
- **`tools/dsp_test/DSP_TEST.md`:** law STOCK-ROWS, the archived tables, the
  AL-O wording.
- **`S097_P3_FS_OPTIMIZE_INVESTIGATION.md` status line:** `FS_OS=1` is now
  the default.
