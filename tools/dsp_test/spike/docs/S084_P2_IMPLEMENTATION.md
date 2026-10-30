# S084 P2 — Implementation Schedule: Sample Flash, Wavetables, Stock Waves, Transients, Drive Types

**Session:** 084, voice-expansion phase 2 (schedule only; no code changed in
this turn)
**Branch:** `dev-ph6-midi`
**Date:** 2026-10-25
**Plan of record:** `S084_P1_VOICE_EXPANSIONS.md` (r7). "Plan §x" refers to
it.
**General plan for this phase:** `S084_P2_VOICE_EXPANSION_2.md`. "P2 §x"
refers to it.
**Depends on:** `S084_P1_IMPLEMENTATION.md`, including the §18 review fixes
(selector rows, request bytes, the pwm read). P2 starts after P1's hardware
checks H1–H7.
**Followed by:** `S084_P3_VOICE_EXPANSION_3.md`.

---

## 0. Read this first

### 0.1 Scope of P2

| WP | Work package | Plan / P2 |
|---|---|---|
| WP0 | Baseline measurements: ITCM, flash, the `sft` filter loop, the transient loop, the table oscillator loop | P2 §2 (cost targets) |
| WP1 | Sample-flash catalogue v2: layout, header, entries, `SampleMemory` read API without RAM caches | P2-A, plan §4.1.4–§4.1.5, Q7/Q8 |
| WP2 | `Core/SampleRom/SampleInstall.c/h` (new): WAV format acceptance, frame conversion, peak gain, item and catalogue writes | P2-A, W1 |
| WP3 | Installer: `filesystem.c` walk of `samples/` then `wavetables/` (recursive, natural order, batched selection), `/loops` removed; `storage_naturalCompare()`; the Load:[Samples] modal; the boot notice | P2-A |
| WP4 | Stock waves `sq3 sw3 sqa tra swa`: recipe tool and constants, install-time generator, read access with fallback, `Oscillator.c` list of 11, `pq3`/`pw3` on `sq3`/`sw3` | P2-B, W4 |
| WP5 | `wtb` oscillator type: `OscillatorWavetable.c/h` (new), type 4, dispatch, frequency, writers, engines | P2-C, W1–W3 |
| WP6 | Transient type row `trt` and the transient generator: `trs`/`wav`, sample transients at Q20.12 with gain, stock-wave bursts | P2-D, plan §4.3.1, Q13–Q15 |
| WP7 | Filter drive types: `DriveShaper.h/.c` (new), `SVF_calcBlockZDFShaped()`, engine fields and calls | P2-E, plan §4.4 |
| WP8 | Descriptor rows `trt` and `dty` (four instruments), menu pages, special writers, the generalised selector helper | P2 §3 |
| WP9 | Menu: `MenuText.h` names, `InstrumentCellFormat` kinds, formatter cases, automation range | P2-B/C/D/E |
| WP10 | Tools and host tests | P2 §6 |
| WP11 | Specification updates | P2 §7 |

**Not in P2** (P3): the transient `buf` options, Mix FX, voice buffers,
elastic Effects. The `trt` row reserves `buf` (value 2) but does not offer
it.

### 0.2 Line numbers

- **Baseline:** the working tree on 2026-10-25: `dev-ph6-midi` at `59406a4`
  plus the uncommitted S084 P0/P1 changes and the P1 review fixes
  (`S084_P1_IMPLEMENTATION.md` §18).
- **They drift** as earlier edits land. Every change also names its anchor
  (a function, a define, or quoted text); re-find the anchor before editing.
- **Order:** apply changes within a file from the bottom up, so earlier line
  numbers stay valid.

### 0.3 Change IDs and comment blocks

**Tag.** The specifications already use "S084 P2", "S084 P3" and "S084 P4"
for the roll/overlay work (`MIDI_SPEC.md`, `MODULE_INTERCHANGE_SPEC.md`,
`STORAGE_SRAM_MANIFEST.md`). To keep `grep` results unambiguous, every
change in this schedule is tagged **`S084 VX2`** (voice expansion 2), with
change IDs `VX2-<WP>.<n>`.

Every change carries a comment block, written to be pasted beside the code,
in the project's contract layout:

```
/*
 * <Name> (S084 VX2 VX2-<WP>.<n>).
 *
 * What:       ...
 * Why:        ...
 * Inputs:     ...
 * Outputs:    ...
 * Accessors:  ...
 * Affiliates: ...
 */
```

- Header declarations take the same block. The `.c` copy may add an
  implementation note.
- Removed code gets no block. The specification records the removal.
- Where this schedule writes "block as above", the block of the named
  change is reused word for word, with only the `Accessors` line adjusted.

### 0.4 Decisions to confirm before coding

The general plan leaves these open, or the deep dive found a reason to
refine it. Each has a recommendation; coding follows the recommendation
unless you say otherwise.

| ID | Decision | Recommendation and reason |
|---|---|---|
| D1 | **Install manifest.** `filesystem.c` keeps a 120-entry, 13,440 B static manifest (`sample_manifest`, SRAM1) to sort files before installing. At 233 samples plus wavetable frames it would roughly double. | **Batched natural-order selection** (WP3). Each pass over a directory keeps only the next 32 names after the last name installed, in a 2,048 B static batch. The manifest (13,440 B) and the `SampleMemory` caches (5,040 B) go, a net release of about 16 KB. Cost: about 8 directory passes for 233 files (well under a second on the card). The alternative, borrowing the Effect arena during the modal install, needs a new FxBuffer ownership state and an Effect re-init, against the "owners clear what they claim" contract; not recommended. |
| D2 | **Folder names.** `FILESYSTEM_SPEC.md` names a settled `Wavetable/` folder with numbered `000 <name>/` children and a `Sample/` folder. Plan D4 says `wavetables/`, and the card and code use `samples/`. | **`samples/` and `wavetables/`** (D4). The spec is corrected in WP11. A leading run of digits followed by a space, `_` or `-` orders the folder and is stripped from its display name, so a card that follows the old numbering still sorts and reads well. |
| D3 | **Recursion order.** P2 says "subfolders, recursively". | At each level, **files first, then subfolders**, each in natural order. **Depth limit 3** below `samples/` or `wavetables/` (bounded by the five asyncfatfs handles). In `wavetables/`, every folder that contains WAV files is one wavetable, named by that folder. |
| D4 | **Old kits and `trt`.** A missing `transient_type` key loads 0. | **0 = `trs`**. Old kits keep `Snp`/`Ofs`. An old PCM transient (stored byte 2–13) clamps to `Ofs` under `trs` until it is re-chosen under `wav`. This follows Q12 (no migration); the alternatives also change old kits, just differently. |
| D5 | **Stock-wave burst pitch and decay** (P2 §7). | Burst frequency `f = 440 Hz × pitch`, where `pitch` is today's 0.25–1.75 factor from `frq` (110–770 Hz). Decay **τ = 10 ms**, not the proposed 15 ms: at the 50 ms gate, 15 ms leaves −29 dB (an audible step); 10 ms leaves −43 dB. |
| D6 | **Transient read cost.** The cap is today's 22 instructions per sample, with 0 divisions. | Sample transients and stock bursts are written **with** linear interpolation and measured with `fpseq.py` (WP0/WP10). If a loop exceeds 22, that path drops interpolation (one read, as today's PCM path does). The measured figure is recorded either way. |
| D7 | **Drive-type Stage A.** P2-E says "Stage A computes only `1/D1` for non-Padé types". | **Keep the shared Stage A for every type.** Nine of the 14 types use the soft clip `s = u·N/D` as an intermediate (`rct art fdc bsw tsw lsw lsq chb rsb`), and the shared reciprocal supplies it with no extra division. The four folds ignore it. Still two divisions per sample for every type. |
| D8 | **Drive-type code shape.** P2-E: unswitched loops or a per-sample switch. | **One new function with a per-sample switch on the block-constant type** (`SVF_calcBlockZDFShaped()`), and `SVF_calcBlockZDF()` unchanged for `sft`. Thirteen unswitched copies of the ZDF loop would need about 13 KB of ITCM, and only 5,528 B is free. `sft` stays bit-identical by construction (the engines call the untouched function). |
| D9 | **ITCM priority.** 5,528 B is free after P1. | Order: `wtb` render, then the stock-wave reader, then the shaped ZDF loop. If `link_budget.py` shows the shaped loop does not fit, it is placed in flash (`.text`, ART cache) and its cost is re-measured. |
| D10 | **Gain code.** P2: "peak-normalisation gain byte". | `gain = 2^(code / 24)` (steps of 1/24 octave ≈ 0.25 dB, up to 64 dB). `code = floor(24·log2(32767 / peak))`, so normalisation never clips. `sampleMemory_gainFromCode()` converts it with a 24-entry mantissa table (96 B flash) and an exponent add, about 8 instructions, once per block per sample oscillator. Applied to `smp`/`slp` and sample transients (Q15). Wavetable frames get no gain (W1: stored as supplied). |
| D11 | **Frame format reductions.** P2 accepts mono/stereo, 8/16/24-bit frames. W1: never expand. | Stored 8-bit stays 8-bit (WAV 8-bit is unsigned: flipped to signed). 16-bit is copied. 24-bit becomes 16-bit (rounded). Stereo becomes mono `(L + R) / 2`. Every conversion makes the data smaller or keeps its size. |
| D12 | **Selector helper.** P1's `instrumentManager_isOscSelectorDescriptor()` recognises `typ`/`src`/`run`. `trt` and `dty` are also non-morphable special rows. | **Generalise and rename** it to `instrumentManager_isSelectorDescriptor()`: an INSTANCE_OFFSET binding, MORPHABLE clear, and any special writer. Today this set is exactly the P1 selectors, and P2 adds `trt` and `dty`. Four call sites in `presetManager.c`. |
| D13 | **LCD wording** (P2 §7). | Boot, when the flash holds a v1 install: `Samples invalid` / `reload samples` for 1.5 s. Install summary: `Smp NNN Wtb NN` / `Skip NN` (skipped files and tables). |

### 0.5 Approvals needed before coding

**RAM (SRAM1 `.bss`):**

| Item | Bytes | Owner | Lifetime |
|---|---:|---|---|
| `sample_manifest[120]` + `sample_manifest_count` removed | −13,441 | `filesystem.c` | released, no reuse |
| `sample_info_cache`, `sample_name_cache`, `sample_loop_cache`, `install_info`, `install_names` removed | −5,040 | `SampleMemory.c` | released, no reuse |
| `fs_install_batch[32]` (64 B each: 13 B short name + 49 B sort key + 2 B kind/pad) + `fs_install_batch_count` | +2,049 | `filesystem.c` | static; used only inside the modal install |
| `fs_install_last[4]` (last sort key 49 B + short name 13 B + pad, per recursion level, 64 B each) | +256 | `filesystem.c` | static; modal install |
| `sample_install` state (write address, item start, bytes, peak, carry word, counts, flags) | +32 | `SampleInstall.c` | static; modal install |
| `sample_wavetable_count`, `sample_catalogue_state` | +2 | `SampleMemory.c` | static |
| **Net** | **≈ −16,142** | | |

**Inside the existing 1,176 B runtime-slot union** (no new static RAM):

| Engine | After P1 + fixes | + drive type (1 B) + shaper state (28 B) | + transient fields (24 → 40 B) | After P2 | Margin |
|---|---:|---:|---:|---:|---:|
| Drum | 1,072 | +32 | +16 | ≈ 1,120 | ≈ 56 |
| Snare | 904 | +32 | +16 | ≈ 952 | ≈ 224 |
| Cymbal | 1,008 | +32 | +16 | ≈ 1,056 | ≈ 120 |
| HiHat | 1,016 | +32 | +16 | ≈ 1,064 | ≈ 112 |

- `OscInfo` does not grow:
  - the `wtb` state shares storage with `OscPwmState` (an anonymous union, VX2-5.2);
  - the pwm natural split reuses `OscPwmState.mix` (VX2-4.9);
  - the sample gain code sits in an existing padding byte (VX2-1.6).
- The `_Static_assert`s in `InstrumentManager.c` (line 197 onward) prove the
  fit. Measure the exact `sizeof` with the compile probe used in P1 §18.2.
- **Drum's margin after P2 is about 56 B.** P3 keeps its records outside the
  slot (P3 plan §2 P3-B), so P3 does not consume it.

**ITCM** (10,856 / 16,384 B used after P1; 5,528 B free):

| Item | Estimate |
|---|---:|
| `oscWavetable_render` / `_renderFm` (8- and 16-bit loops) | 1.6–2.2 KB |
| Stock-wave block reader (plain and FM) | 0.5–0.8 KB |
| `SVF_calcBlockZDFShaped()` | 1.8–2.8 KB (flash if it does not fit, D9) |

**Program flash** (128,092 B headroom after P1): about +30–40 KB. That is
`StockWaveRecipes.c` (≈ 10.2 KB) plus the generator, installer, reader,
shapers, transient code, menu text and the 96 B gain mantissa table.

**Sample flash** (sectors 7–11, 1,310,720 B):

| Region | Address | Bytes |
|---|---|---:|
| Stock-wave block (5 × 12,224) | `0x080C0000` | 61,120 |
| Item data (samples, wavetables) | `0x080CEEC0` … `0x081FE000` | 1,241,408 |
| Catalogue v2 (header + 233 sample + 128 wavetable entries, 20 B each) | `0x081FE000` … `0x08200000` | 8,192 (7,252 used) |

- **User space shrinks by 66,908 B**, from v1's 1,308,316 B: the stock block
  plus the larger catalogue.

### 0.6 Constants introduced in P2 (single source of truth)

| Constant | Value | File |
|---|---|---|
| `SAMPLE_CATALOGUE_MAGIC` | `0x3243584C` (`"LXC2"` little-endian) | `SampleMemory.h` |
| `SAMPLE_CATALOGUE_VERSION` | 2 | `SampleMemory.h` |
| `SAMPLE_CATALOGUE_START_ADDRESS` / `_BYTES` | `0x081FE000` / 8,192 | `SampleMemory.h` |
| `SAMPLE_STOCK_START_ADDRESS` / `_BYTES` | `0x080C0000` / 61,120 | `SampleMemory.h` |
| `SAMPLE_DATA_START_ADDRESS` | `0x080CEEC0` | `SampleMemory.h` |
| `SAMPLE_MAX_COUNT` | 233 (was 120) | `SampleMemory.h` |
| `WAVETABLE_MAX_COUNT`, `WAVETABLE_MAX_FRAMES` | 128, 128 | `SampleMemory.h` |
| `SAMPLE_GAIN_STEPS_PER_OCTAVE` | 24 | `SampleMemory.h` |
| `STOCK_WAVE_FIRST` | 6 (`sq3`) | `Oscillator.h` |
| `SQ3 SW3 SQA TRA SWA` | 6…10 | `Oscillator.h` |
| `OSC_STOCK_WAVE_COUNT` | 11 (was 6) | `Oscillator.h` |
| `OSC_TYPE_WTB`, `OSC_TYPE_COUNT` | 4, 5 | `Oscillator.h` |
| `STOCK_WAVE_COUNT`, `STOCK_WAVE_LEVELS`, `STOCK_WAVE_POINTS_PER_WAVE` | 5, 11, 6,112 | `StockWaves.h` |
| Level lengths | 1,024 ×5, 512, 256, 128, 64, 16, 16 | `StockWaves.c` |
| Level harmonic counts | 511, 511, 337, 168, 84, 42, 21, 10, 5, 2, 1 | `StockWaveRecipes.c` (generated) |
| `TRANSIENT_TYPE_TRS / _WAV / _BUF`, `TRANSIENT_TYPE_SELECTABLE` | 0 / 1 / 2, 2 (P3 raises to 3) | `transientGenerator.h` |
| `TRANSIENT_STOCK_PCM_COUNT` | 12 | `transientGenerator.h` |
| `TRANSIENT_WAV_LIST_MAX` | 256 | `transientGenerator.h` |
| `TRANSIENT_BURST_SAMPLES` | 2,205 (50 ms) | `transientGenerator.h` |
| `TRANSIENT_BURST_DECAY` | 0.99773 (τ = 10 ms at 44,108 Hz) | `transientGenerator.c` |
| `TRANSIENT_BURST_BASE_HZ` | 440 | `transientGenerator.c` |
| `DRIVE_TYPE_SFT … DRIVE_TYPE_RSB`, `DRIVE_TYPE_COUNT` | 0…13, 14 | `DriveShaper.h` |
| `DRIVE_SHAPER_HYST` | 0.03 (on `u`, the same scale as `plot_waveshapers.py`) | `DriveShaper.h` |
| `DRIVE_SHAPER_DC_R` | 0.9986 | `DriveShaper.h` |
| `IM_SPECIAL_TRANSIENT_TYPE`, `IM_SPECIAL_DRIVE_TYPE` | 23, 24 (count 25 ≤ 32) | `InstrumentManager.h` |
| `FS_INSTALL_BATCH`, `FS_INSTALL_MAX_DEPTH` | 32, 3 | `filesystem.c` |

**Stored zero is today's behaviour:**

- `trt` 0 = `trs`;
- `dty` 0 = `sft`;
- `typ` values 0–3 are unchanged, and 4 (`wtb`) is new;
- `wav` values 0–5 are unchanged, and 6–10 are new.

### 0.7 Order of work and gates

1. **WP0.** Record the baselines. Nothing else starts without them.
2. **WP1 → WP2 → WP3** (catalogue, installer core, walk). **Gate G1:**
   `test_catalogue` and `test_natural_order` pass, and a card install on
   hardware (H1) reads back.
3. **WP4** (stock waves). **Gate G2:** `test_stock_gen` matches the
   reference within 1 LSB, and the `wav` list plays all 11 waves (H2).
4. **WP5** (`wtb`). **Gate G3:** `test_wtb` passes, and `link_budget.py`
   shows ITCM within budget.
5. **WP6** (transients) and **WP8/WP9** for `trt`. **Gate G4:**
   `test_transient` passes, and every transient loop is ≤ 22 instructions or
   has dropped interpolation (D6).
6. **WP7** (drive types) and **WP8/WP9** for `dty`. **Gate G5:**
   `test_shaper` matches `plot_waveshapers.py`, `filter` S1 is unchanged,
   and the worst type is measured.
7. **WP10/WP11** throughout; each WP's spec lines land with its code.

---

## 1. WP0 — Baseline measurements

No code change. Record these figures in the implementation log (§15)
before WP1:

| Measure | How | Why |
|---|---|---|
| `text` / `data` / `bss`, flash headroom, ITCM, DTCM | `make all` (link_budget output) and `arm-none-eabi-size` | the approvals in §0.5 are checked against them |
| `sizeof` of `OscInfo`, `OscPwmState`, the four voices, `TransientGenerator`, `ResonantFilter` | the P1 §18.2 compile probe | runtime-slot margin |
| `SVF_calcBlockZDF` instructions per sample, per filter type | `fpseq.py --report --all` on a standalone `-Ofast` object (the `armcheck` pattern) | `sft` + 6 target (P2-E) |
| `transient_calcBlock` instructions per sample | the same | the 22-instruction cap (D6) |
| `calcWavetableOscBlock`, `calcFmBlock` | the same | reference for the stock reader and `wtb` |
| Install time of the current card (`samples/` + `loops/`) | stopwatch from OK to the repaint | to compare after WP3 |
| Worst-case Scene CPU | P1 WP0 procedure | the P2 cost increase is checked against it at H6 |

---

## 2. WP1 — Sample-flash catalogue v2 and the `SampleMemory` read API

### VX2-1.1 `Core/SampleRom/SampleMemory.h` lines 1–51 — modify (file history block)

Replace point 1's v1 layout text, from "Ours (F765, 2MB…" through "…below the new floor.", with the v2 map. Points 2–4 keep their text. Point 2 becomes "flash-direct reads with a guarded silence fallback".

```c
 *    Ours (F765, 2MB), catalogue v2 (S084 VX2 VX2-1.1):
 *      0x080C0000  stock-wave block, 61,120 B (5 waves x 11 levels,
 *                  generated at install by StockWaveGen.c)
 *      0x080CEEC0  item data: samples and wavetables, packed upward
 *      0x081FE000  catalogue v2: 32 B header, 233 sample entries and
 *                  128 wavetable entries of 20 B (7,252 of 8,192 B)
 *    A v1 install (count word at 0x080C0000, info table near the top) is
 *    not read: the v2 header magic is absent, so the catalogue reports
 *    STALE until the next Load:[Samples] (plan Q7: nothing is retained).
```

### VX2-1.2 `Core/SampleRom/SampleMemory.h` lines 72–131 — modify (layout, types, API)

**Remove:**

- `SAMPLE_INFO_SIZE`, `SAMPLE_NAME_SIZE`, `SAMPLE_NAME_START_ADDRESS`,
  `SAMPLE_INFO_START_ADDRESS`, `SAMPLE_ROM_SIZE` (lines 93–99);
- `SAMPLE_INFO_LOOP_FLAG`, `SAMPLE_INFO_SIZE_MASK` (lines 101–102);
- the v1 `SampleInfo` (lines 104–109);
- every `sampleMemory_install*` prototype, plus `sampleMemory_setNumSamples`,
  `sampleMemory_isLooped` and `sampleMemory_loadSamples` (lines 113–131).
  The install side moves to `SampleInstall.h` (WP2).

**Keep:** `SAMPLE_ROM_START_ADDRESS`, `SAMPLE_FIRST_SECTOR`,
`SAMPLE_PAGE_SIZE`, `SAMPLE_DISPLAY_NAME_LEN` (lines 80–84).
`sampleFlash.c` asserts on the first two.

**Add:**

```c
/*
 * Sample-flash map, catalogue v2 (S084 VX2 VX2-1.2).
 *
 * What:       fixed regions inside sectors 7..11: the stock-wave block at
 *             the floor, item data above it, and the catalogue in the top
 *             8 KB of sector 11. Every boundary is 32-byte aligned so cache
 *             invalidation never straddles a region.
 * Why:        P2-A: the flash must hold the generated stock waves, named and
 *             gain-tagged samples, and wavetables stored exactly as supplied
 *             (W1), with no RAM copy of the index (Q8).
 * Inputs:     none (compile-time constants).
 * Outputs:    addresses used by SampleMemory.c, SampleInstall.c,
 *             StockWaveGen.c and StockWaves.c.
 * Accessors:  the files above; tools/dsp_test/test_catalogue.c (host map).
 * Affiliates: sampleFlash.c floor checks (SAMPLE_ROM_START_ADDRESS,
 *             SAMPLE_FIRST_SECTOR), STORAGE_SRAM_MANIFEST.md flash map.
 */
#define SAMPLE_FLASH_END_ADDRESS        ((uint32_t)0x08200000)
#define SAMPLE_STOCK_START_ADDRESS      SAMPLE_ROM_START_ADDRESS
#define SAMPLE_STOCK_BYTES              ((uint32_t)61120)
#define SAMPLE_DATA_START_ADDRESS       ((uint32_t)0x080CEEC0)
#define SAMPLE_CATALOGUE_START_ADDRESS  ((uint32_t)0x081FE000)
#define SAMPLE_CATALOGUE_BYTES          ((uint32_t)8192)
#define SAMPLE_CATALOGUE_MAGIC          ((uint32_t)0x3243584Cu) /* "LXC2" */
#define SAMPLE_CATALOGUE_VERSION        2u
#define SAMPLE_MAX_COUNT                233u
#define WAVETABLE_MAX_COUNT             128u
#define WAVETABLE_MAX_FRAMES            128u
#define SAMPLE_GAIN_STEPS_PER_OCTAVE    24u

_Static_assert(SAMPLE_STOCK_START_ADDRESS + SAMPLE_STOCK_BYTES <=
               SAMPLE_DATA_START_ADDRESS, "stock block overlaps item data");
_Static_assert((SAMPLE_DATA_START_ADDRESS & 31u) == 0u &&
               (SAMPLE_CATALOGUE_START_ADDRESS & 31u) == 0u,
               "sample-flash regions must be cache-line aligned");

/*
 * Host-test address mapping (S084 VX2 VX2-1.2).
 *
 * What:       every catalogue and item read goes through SAMPLE_FLASH_PTR()
 *             so the host test can map flash addresses onto an array.
 * Why:        test_catalogue.c installs into a RAM image of sectors 7..11
 *             and reads it back through the unchanged read API.
 * Inputs:     an absolute flash address.  Outputs: a pointer.
 * Accessors:  SampleMemory.c, OscillatorSample.c, OscillatorWavetable.c,
 *             StockWaves.c, transientGenerator.c.
 * Affiliates: tools/dsp_test/test_catalogue.c (defines it first).
 */
#ifndef SAMPLE_FLASH_PTR
#define SAMPLE_FLASH_PTR(addr) ((const void *)(uintptr_t)(addr))
#endif

/*
 * Catalogue v2 records in flash (S084 VX2 VX2-1.2).
 *
 * What:       the 32 B header and 20 B entries, naturally aligned (no
 *             packing). The header's magic is programmed last, so a
 *             catalogue is valid only after a completed commit. Entries are
 *             programmed one by one as each item finishes, into erased
 *             flash; unwritten bytes read 0xFF.
 * Why:        P2-A: version header, sample entries (offset, size, name, gain
 *             byte), wavetable entries (name, frame count, frame length, bit
 *             depth, offset) and the stock block flag, read in place.
 * Inputs:     written by SampleInstall.c.  Outputs: read by SampleMemory.c.
 * Accessors:  SampleMemory.c, SampleInstall.c, test_catalogue.c.
 * Affiliates: FILESYSTEM_SPEC.md "Sample flash", STORAGE_SRAM_MANIFEST.md.
 */
typedef struct {
    uint32_t magic;            /* SAMPLE_CATALOGUE_MAGIC, programmed last */
    uint16_t version;          /* SAMPLE_CATALOGUE_VERSION */
    uint16_t entry_bytes;      /* 20 */
    uint8_t  sample_count;     /* 0..SAMPLE_MAX_COUNT */
    uint8_t  wavetable_count;  /* 0..WAVETABLE_MAX_COUNT */
    uint8_t  stock_valid;      /* 1 = stock block generated */
    uint8_t  reserved0;
    uint32_t data_end;         /* first byte after the last item */
    uint32_t stock_start;      /* SAMPLE_STOCK_START_ADDRESS */
    uint32_t reserved[3];
} sample_catalogue_header_t;

typedef struct {
    uint32_t offset;           /* absolute address of frame 0, 4-aligned */
    uint32_t frames;           /* 16-bit frames */
    char     name[SAMPLE_DISPLAY_NAME_LEN];
    uint8_t  gain_code;        /* gain = 2^(code/24), never clips */
    uint8_t  reserved[3];
} sample_catalogue_sample_t;

typedef struct {
    uint32_t offset;           /* absolute address of frame 0 */
    uint16_t frame_len;        /* points per frame, >= 2 */
    uint8_t  frame_count;      /* 1..WAVETABLE_MAX_FRAMES */
    uint8_t  bits;             /* 8 or 16 */
    char     name[SAMPLE_DISPLAY_NAME_LEN];
    uint8_t  reserved[4];
} sample_catalogue_wavetable_t;

_Static_assert(sizeof(sample_catalogue_header_t) == 32u, "header is 32 B");
_Static_assert(sizeof(sample_catalogue_sample_t) == 20u, "sample entry is 20 B");
_Static_assert(sizeof(sample_catalogue_wavetable_t) == 20u, "wavetable entry is 20 B");

#define SAMPLE_CATALOGUE_SAMPLES_ADDRESS \
    (SAMPLE_CATALOGUE_START_ADDRESS + sizeof(sample_catalogue_header_t))
#define SAMPLE_CATALOGUE_WAVETABLES_ADDRESS \
    (SAMPLE_CATALOGUE_SAMPLES_ADDRESS + \
     SAMPLE_MAX_COUNT * sizeof(sample_catalogue_sample_t))
_Static_assert(SAMPLE_CATALOGUE_WAVETABLES_ADDRESS +
               WAVETABLE_MAX_COUNT * sizeof(sample_catalogue_wavetable_t) <=
               SAMPLE_FLASH_END_ADDRESS, "catalogue exceeds its region");

/*
 * Resolved sample and wavetable descriptions (S084 VX2 VX2-1.2).
 *
 * What:       SampleInfo: size in 16-bit frames, absolute offset, gain code.
 *             WavetableInfo: offset, frame length, frame count, bit depth.
 *             An out-of-range index returns the silence buffer with size 0
 *             (SampleInfo) or 0 (getWavetableInfo).
 * Why:        the DSP binds read these once per catalogue generation or
 *             source change, never per sample.
 * Inputs:     an index.  Outputs: the structs.
 * Accessors:  OscillatorSample.c, OscillatorWavetable.c,
 *             transientGenerator.c, InstrumentManager.c (max rules), menu.c.
 * Affiliates: the catalogue records above.
 */
typedef struct {
    uint32_t size;      /* 16-bit frames; 0 = silence */
    uint32_t offset;    /* absolute flash address (or the silence buffer) */
    uint8_t  gain_code;
} SampleInfo;

typedef struct {
    uint32_t offset;
    uint16_t frame_len;
    uint8_t  frame_count;
    uint8_t  bits;
} WavetableInfo;

#define SAMPLE_CATALOGUE_EMPTY  0u   /* erased, or no completed install */
#define SAMPLE_CATALOGUE_V2     1u
#define SAMPLE_CATALOGUE_STALE  2u   /* a v1 install is present */

void       sampleMemory_init(void);
void       sampleMemory_refresh(void);
uint8_t    sampleMemory_catalogueState(void);
uint32_t   sampleMemory_getGeneration(void);
uint8_t    sampleMemory_getNumSamples(void);
SampleInfo sampleMemory_getSampleInfo(uint8_t index);
void       sampleMemory_getDisplayName(uint8_t index,
                                       char name[SAMPLE_DISPLAY_NAME_LEN + 1u]);
uint8_t    sampleMemory_getNumWavetables(void);
uint8_t    sampleMemory_getWavetableInfo(uint8_t index, WavetableInfo *out);
void       sampleMemory_getWavetableName(uint8_t index,
                                         char name[SAMPLE_DISPLAY_NAME_LEN + 1u]);
uint8_t    sampleMemory_stockValid(void);

/*
 * Peak-normalisation gain from the catalogue byte (S084 VX2 VX2-1.2).
 *
 * What:       2^(code/24): a 24-entry mantissa table plus an exponent add
 *             on the float's bit pattern; about 8 instructions, no libm.
 * Why:        Q15: the catalogue gain is applied wherever a sample plays
 *             (smp/slp oscillators and sample transients). Block rate.
 * Inputs:     code 0..255.  Outputs: gain 1.0 .. ~1,600.
 * Accessors:  oscSample_render(_Fm), transient_calcBlock().
 * Affiliates: SampleInstall.c (computes the code), sampleMemory_gainMantissa.
 */
extern const float sampleMemory_gainMantissa[SAMPLE_GAIN_STEPS_PER_OCTAVE];
static inline float sampleMemory_gainFromCode(uint8_t code)
{
    union { float f; uint32_t u; } g;

    g.f = sampleMemory_gainMantissa[code % SAMPLE_GAIN_STEPS_PER_OCTAVE];
    g.u += (uint32_t)(code / SAMPLE_GAIN_STEPS_PER_OCTAVE) << 23;
    return g.f;
}
```

Each prototype carries this block (contents per function as in VX2-1.3):

```c
/*
 * <function> (S084 VX2 VX2-1.2).
 *
 * What:       <one line per VX2-1.3 entry>
 * Why:        Q8: the catalogue is read in place; there is no RAM cache.
 * Inputs:     <index / out pointer>.  Outputs: <value>.
 * Accessors:  <as VX2-1.3>.
 * Affiliates: the catalogue records, SAMPLE_FLASH_PTR().
 */
```

### VX2-1.3 `Core/SampleRom/SampleMemory.c` — rewrite (lines 21–325)

**Remove:**

- the five RAM caches and the install state (lines 27–45);
- `sampleMemory_infoValid()` (line 56), replaced by the v2 checks below;
- every `sampleMemory_install*` function (lines 159–319);
- `sampleMemory_setNumSamples`, `sampleMemory_isLooped`,
  `sampleMemory_loadSamples`.

**Keep:** `stub_silence_buffer[256]` and `sampleMemory_silenceInfo()`.

**New statics:**

```c
static uint8_t  sample_count = 0u;
static uint8_t  sample_wavetable_count = 0u;
static uint8_t  sample_catalogue_state = SAMPLE_CATALOGUE_EMPTY;
static uint8_t  sample_stock_valid = 0u;
static uint32_t sample_generation = 1u;      /* existing semantics */

const float sampleMemory_gainMantissa[SAMPLE_GAIN_STEPS_PER_OCTAVE] = {
    /* 2^(i/24), i = 0..23 */
    1.000000f, 1.029302f, 1.059463f, 1.090508f, 1.122462f, 1.155353f,
    1.189207f, 1.224054f, 1.259921f, 1.296840f, 1.334840f, 1.373954f,
    1.414214f, 1.455653f, 1.498307f, 1.542211f, 1.587401f, 1.633915f,
    1.681793f, 1.731073f, 1.781797f, 1.834008f, 1.887749f, 1.943064f,
};
```

**Functions:**

| Function | Behaviour |
|---|---|
| `sampleMemory_entryValid(const sample_catalogue_sample_t *e)` (static) | `offset` in `[SAMPLE_DATA_START_ADDRESS, SAMPLE_CATALOGUE_START_ADDRESS)`, 4-aligned; `frames` ≥ 1; `offset + frames·2 ≤ SAMPLE_CATALOGUE_START_ADDRESS` (64-bit sum) |
| `sampleMemory_tableValid(const sample_catalogue_wavetable_t *e)` (static) | the same bounds with `frame_len·frame_count·bits/8`; `frame_len` ≥ 2; `frame_count` 1..128; `bits` 8 or 16 |
| `sampleMemory_refresh()` | `generation++` (skip 0). Read the header through `SAMPLE_FLASH_PTR`. If `magic`, `version` and `entry_bytes` match: state = V2, `stock_valid` from the header, then count the leading valid sample entries (≤ `header.sample_count`) and the leading valid wavetable entries. Otherwise both counts are 0 and the stock is invalid. State = STALE if the word at `SAMPLE_ROM_START_ADDRESS` is 1..120 (a v1 count word), else EMPTY. |
| `sampleMemory_init()` | `sampleFlash_init()`, `sampleMemory_refresh()` (unchanged shape) |
| `sampleMemory_getSampleInfo(i)` | silence if `i ≥ sample_count`; else `{frames, offset, gain_code}` read from the entry |
| `sampleMemory_getDisplayName(i, name)` | spaces if out of range (as today); else the entry's 8 bytes + NUL |
| `sampleMemory_getWavetableInfo(i, out)` | 0 if out of range; else fill `out` and return 1 |
| `sampleMemory_getWavetableName(i, name)` | as `getDisplayName`, from the wavetable entry |
| `sampleMemory_stockValid()` | `sample_stock_valid` |
| `sampleMemory_catalogueState()`, `_getNumSamples()`, `_getNumWavetables()`, `_getGeneration()` | return the statics |

Comment block for `sampleMemory_refresh()`:

```c
/*
 * Re-read catalogue v2 from flash (S084 VX2 VX2-1.3).
 *
 * What:       validates the header (magic, version, entry size), counts
 *             the leading valid sample and wavetable entries, records the
 *             stock flag and bumps the generation so every DSP binding
 *             refreshes. Without a v2 header it reports STALE when a v1
 *             count word is present, else EMPTY.
 * Why:        Q8: no RAM index; Q7: v1 installs are not read. The
 *             generation stamp is the only invalidation DSP caches need.
 * Inputs:     sample flash.  Outputs: counts, state, generation.
 * Accessors:  sampleMemory_init() (boot), sampleInstall_commit().
 * Affiliates: oscSample_bind(), oscWavetable_bind(), transient bind,
 *             main.c boot notice (VX2-3.12).
 */
```

### VX2-1.4 `Core/DSPAudio/OscillatorSample.c` lines 54–80 — modify (bind reads v2 and the gain code)

`oscSample_bind()`:

- **Keep:** the existing count-0 and index logic (P1 review fix F2,
  `sourceRequest`).
- The data pointer becomes `(const int16_t *)SAMPLE_FLASH_PTR(info.offset)`.
- The bind also stores `osc->sampleGainCode = info.gain_code` when the cache
  refreshes, and 0 for silence.

`oscSample_render` / `oscSample_renderFm`: after `oscSample_bind(osc)`, set
`gain *= sampleMemory_gainFromCode(osc->sampleGainCode);` once, before the
loop.

```c
/*
 * Catalogue gain on sample playback (S084 VX2 VX2-1.4).
 *
 * What:       the block gain is multiplied once by 2^(code/24), the
 *             catalogue's peak-normalisation gain for this sample.
 * Why:        Q15: matched sample levels everywhere a sample plays. One
 *             multiply per block; the per-sample loop is unchanged.
 * Inputs:     osc->sampleGainCode (set by oscSample_bind()).
 * Outputs:    the gain applied in the existing per-sample multiply.
 * Accessors:  Oscillator.c dispatch.
 * Affiliates: sampleMemory_gainFromCode(), SampleInstall.c peak tracking.
 */
```

### VX2-1.5 `Core/DSPAudio/Oscillator.h` line 173 — add `sampleGainCode` (padding byte)

`OscInfo` has one padding byte after `sampleCacheIndex` (before the 4-aligned
`sampleCacheGeneration`). Add `uint8_t sampleGainCode;` there.
`sizeof(OscInfo)` stays 120 B; verify with the probe.

```c
	/*
	 * Catalogue gain code of the bound sample (S084 VX2 VX2-1.5).
	 *
	 * What:       the gain byte of the bound sample's catalogue entry;
	 *             0 when nothing is bound.
	 * Why:        Q15 matched levels; stored as the byte so OscInfo does
	 *             not grow (it fills the padding before
	 *             sampleCacheGeneration).
	 * Writers:    oscSample_bind().  Readers: oscSample_render(_Fm).
	 */
	uint8_t		sampleGainCode;
```

### VX2-1.6 `Core/SampleRom/sampleFlash.c` — no change

The floor guards and word writes are reused as they are. Only the comment
at lines 131–142 gains one sentence: "S084 VX2: the catalogue v2 regions
(SampleMemory.h) all lie inside sectors 7..11; the guards are unchanged."

---

## 3. WP2 — `Core/SampleRom/SampleInstall.c/h` (new)

### VX2-2.1 `Core/SampleRom/SampleInstall.h` (new file)

Project licence header (Brendan Clarke, LXR02 Open-Source), then:

```c
/*
 * SampleInstall — write catalogue v2 from WAV data handed over by the
 * filesystem (S084 VX2 VX2-2.1).
 *
 * What:       accepts and converts every supported WAV form, programs item
 *             data upward from SAMPLE_DATA_START_ADDRESS, writes one 20 B
 *             catalogue entry as each item finishes, generates the stock
 *             block, and commits the header (magic last).
 * Why:        P2-A, W1: samples keep today's form (mono, 16-bit, 44.1 kHz);
 *             wavetable frames are stored as supplied, reduced only when the
 *             form is wider than the oscillator reads (24-bit -> 16-bit,
 *             stereo -> mono). There is no install-time scratch beyond the
 *             32 B state below.
 * Inputs:     format chunks and data bytes from filesystem.c (modal, audio
 *             suspended).
 * Outputs:    sample flash; SampleMemory refresh on commit.
 * Accessors:  filesystem.c installer (VX2-3.x).
 * Affiliates: SampleMemory.h (layout, records), sampleFlash.c (writes),
 *             StockWaveGen.c (stock block), tools/dsp_test/test_catalogue.c.
 */
#ifndef SAMPLEINSTALL_H_
#define SAMPLEINSTALL_H_

#include <stdint.h>
#include "SampleMemory.h"

typedef enum {
    SAMPLE_INSTALL_KIND_SAMPLE = 0,
    SAMPLE_INSTALL_KIND_FRAME
} sample_install_kind_t;

typedef struct {
    uint16_t channels;      /* 1 or 2 */
    uint16_t bits;          /* 8, 16 or 24 */
    uint32_t rate;
    uint16_t block_align;   /* bytes per source frame */
    uint8_t  stored_bits;   /* 8 or 16 after conversion */
    uint8_t  reserved;
} sample_install_fmt_t;

uint8_t  sampleInstall_acceptFormat(const uint8_t *fmt, uint32_t fmt_bytes,
                                    sample_install_kind_t kind,
                                    sample_install_fmt_t *out);
uint32_t sampleInstall_storedBytes(const sample_install_fmt_t *fmt,
                                   uint32_t data_bytes);
int      sampleInstall_begin(void);
int      sampleInstall_writeStockBlock(void);
int      sampleInstall_beginSample(const char name[SAMPLE_DISPLAY_NAME_LEN],
                                   const sample_install_fmt_t *fmt,
                                   uint32_t data_bytes);
int      sampleInstall_writeSampleData(const uint8_t *data, uint32_t bytes);
int      sampleInstall_finishSample(void);
int      sampleInstall_beginWavetable(const char name[SAMPLE_DISPLAY_NAME_LEN],
                                      const sample_install_fmt_t *fmt,
                                      uint16_t frame_len, uint8_t frame_count);
int      sampleInstall_writeFrameData(const uint8_t *data, uint32_t bytes);
int      sampleInstall_finishWavetable(void);
int      sampleInstall_commit(void);
uint32_t sampleInstall_bytesFree(void);
uint8_t  sampleInstall_sampleCount(void);
uint8_t  sampleInstall_wavetableCount(void);

#endif /* SAMPLEINSTALL_H_ */
```

Per-prototype blocks:

- **`sampleInstall_acceptFormat`.**
  - **What:** parses a `fmt ` chunk of 16 to 40 bytes. Accepts tag 1 (PCM)
    and tag 0xFFFE (extensible) with the PCM sub-format GUID (first 2
    bytes = 1 at offset 24).
    - **SAMPLE:** channels 1, bits 16, rate 44,100.
    - **FRAME:** channels 1 or 2, bits 8, 16 or 24, any rate.
    - Sets `stored_bits` to 8 for 8-bit sources, otherwise 16.
  - **Why:** P2-A accepted forms. Samples are unchanged from v1; frames are
    W1.
  - **Outputs:** 1 = accepted.
  - **Accessors:** `filesystem_parseWav()` (VX2-3.6).
- **`sampleInstall_storedBytes`:** `data_bytes / block_align` frames ×
  `stored_bits/8`. This is the flash cost after conversion, used for the
  wavetable fit check before any byte is written.
- **`sampleInstall_begin`.**
  - **What:** erases sectors 7..11 (`sampleFlash_eraseAllSamples()`),
    zeroes the state, and sets the write address to
    `SAMPLE_DATA_START_ADDRESS`.
  - **Why:** every Load:[Samples] is a full install. The v1 append mode is
    gone with `/loops`.
- **`sampleInstall_writeStockBlock`.**
  - **What:** `stockWaveGen_programAll(SAMPLE_STOCK_START_ADDRESS)`; on
    success, marks the stock valid for the commit.
  - **Why:** W4. Generated after the erase, so the block is always matched
    to this firmware's recipes.
- **`sampleInstall_beginSample` / `_writeSampleData` / `_finishSample`.**
  - **What:**
    - `beginSample` checks the count (< 233) and the fit (bytes + 4 B zero
      pad, rounded to 4).
    - `writeSampleData` passes 16-bit data through and tracks
      `peak = max |s|`. A carry word keeps the 4-byte programming
      alignment across calls of any even length.
    - `finishSample` writes the 4 B zero pad (the interpolation guard v1
      also had), then programs the entry with
      `gain_code = sampleInstall_gainCode(peak)`.
  - **Why:** samples as today plus the Q15 gain.
- **`sampleInstall_beginWavetable` / `_writeFrameData` / `_finishWavetable`.**
  - **What:**
    - `beginWavetable` checks the count (< 128), `frame_count` (1..128),
      `frame_len` ≥ 2, and the fit of the whole table:
      `frame_len · frame_count · stored_bits/8`, rounded to 4.
    - `writeFrameData` converts per source frame:
      - 8-bit: `s = u8 ^ 0x80`, stereo `(L + R) >> 1`, stored as int8;
      - 16-bit: copied, stereo `(L + R) >> 1`;
      - 24-bit: `(s24 + 128) >> 8`, saturated to int16, stereo averaged
        after the reduction.
      An input carry of up to 5 bytes holds a source frame split across
      calls; an output carry of up to 3 bytes keeps word programming
      aligned.
    - `finishWavetable` flushes the output carry (padding with 0) and
      programs the entry.
  - **Why:** W1 (stored as supplied, never expanded) and D11.
- **`sampleInstall_commit`.**
  - **What:** programs header bytes 4..31 (counts, `stock_valid`,
    `data_end`, `stock_start`, reserved = 0xFF), then the magic word,
    invalidates the D-cache over sectors 7..11, and calls
    `sampleMemory_refresh()`.
  - **Why:** the magic last makes a partial install read as EMPTY or STALE,
    never as a half catalogue.
- **`sampleInstall_bytesFree`, `_sampleCount`, `_wavetableCount`:** for the
  installer's skip decisions and the LCD summary.

### VX2-2.2 `Core/SampleRom/SampleInstall.c` (new file)

- **Includes:** `SampleInstall.h`, `SampleMemory.h`, `sampleFlash.h`,
  `StockWaveGen.h`, `<math.h>` (`log2f`, install only), `<string.h>`.
- **State** (≈ 32 B, the approved SRAM1 item):

  ```c
  static struct {
      uint32_t write_addr;       /* next data byte (4-aligned) */
      uint32_t item_start;       /* first byte of the open item */
      uint32_t item_bytes;       /* stored bytes expected for the item */
      uint32_t item_written;     /* stored bytes written so far */
      uint8_t  out_carry[4];     /* bytes waiting for a full word */
      uint8_t  in_carry[6];      /* one split source frame */
      uint8_t  out_carry_n, in_carry_n;
      uint16_t peak;             /* max |s| of the open sample */
      uint8_t  open_kind;        /* 0 none, 1 sample, 2 wavetable */
      uint8_t  samples, wavetables, stock_ok;
      sample_install_fmt_t fmt;  /* the open item's format */
  } sample_install;
  ```

- **`sampleInstall_gainCode(peak)`** (static):

  ```c
  /*
   * Peak-normalisation code for one sample (S084 VX2 VX2-2.2).
   *
   * What:       floor(24 * log2(32767 / peak)), clamped to 0..255; peak 0
   *             gives 0. The floor keeps gain x peak <= 32767.
   * Why:        D10 / Q15: matched levels, never clipped by the gain.
   * Inputs:     peak 0..32768.  Outputs: code.
   * Accessors:  sampleInstall_finishSample().
   * Affiliates: sampleMemory_gainFromCode() (the inverse).
   */
  ```

- **`sampleInstall_writeWords(const uint8_t *p, uint32_t n)`** (static):
  appends to `out_carry`, programs every full word through
  `sampleFlash_writeBytes(&write_addr, …)`, and keeps the remainder. This is
  the only flash-programming path for item data.
- **Entry programming:** `sampleFlash_writeBytes()` on the entry address
  (`SAMPLE_CATALOGUE_SAMPLES_ADDRESS + i·20` or the wavetable equivalent)
  with a stack-built record (5 words).
- **Error codes:** 0 ok; −1 state; −2 count limit; −3 no fit; ≥ 1
  `sampleFlash` status. Every failure leaves the item open state cleared,
  so the next item can still install (a failed file is skipped, as v1 did).

Comment block on `sampleInstall_writeFrameData()` (the non-trivial
converter):

```c
/*
 * Convert and program wavetable frame bytes (S084 VX2 VX2-2.2).
 *
 * What:       consumes source bytes in any split, rebuilding whole source
 *             frames through in_carry; converts each frame to the stored
 *             form (8-bit signed or 16-bit, mono: stereo averaged; 24-bit
 *             rounded to 16) and appends it through the word writer.
 *             Stops counting at item_bytes, so trailing chunk bytes beyond
 *             frame_len x frame_count are ignored.
 * Why:        W1: frames are stored as supplied unless the oscillator cannot
 *             read the form; every conversion keeps or reduces the size.
 * Inputs:     data, bytes (any count), the open table's fmt.
 * Outputs:    flash bytes; item_written.
 * Accessors:  filesystem_installWavetable() (VX2-3.8).
 * Affiliates: sampleInstall_acceptFormat(), sampleInstall_writeWords().
 */
```

---

## 4. WP3 — Installer: the card walk, natural order, the modal, the boot notice

### VX2-3.1 `Core/Hardware/SD/storageTypes.h` before line 655 (`#endif`) — add

```c
/*
 * Natural, numeric-aware name order (S084 VX2 VX2-3.1).
 *
 * What:       compares two NUL-terminated names case-insensitively, but
 *             treats each run of decimal digits as one number: leading zeros
 *             are skipped, a longer remaining run is larger, equal lengths
 *             compare digit by digit, and on equal value the run with fewer
 *             leading zeros sorts first. Returns -1, 0 or 1. 0 means equal
 *             under these rules (case and zero-padding differences only).
 * Why:        P2-A / plan §4.1.4: frames and samples install in the order a
 *             person reads ("f2" before "f10"); wavetable frame order is the
 *             filename order.
 * Inputs:     a, b (may be empty, never NULL).  Outputs: -1/0/1.
 * Accessors:  filesystem.c installer (VX2-3.7).
 * Affiliates: tools/sampleoptimize.py (keeps file names, so its output
 *             sorts the same), tools/dsp_test/test_natural_order.c.
 */
int8_t storage_naturalCompare(const char *a, const char *b);

/*
 * Ordering prefix of a folder name (S084 VX2 VX2-3.1).
 *
 * What:       returns a pointer past a leading run of digits followed by
 *             one or more of ' ', '_' or '-', if a non-empty name remains;
 *             otherwise the name itself.
 * Why:        D2: "000 hvoice" orders by its number and displays "hvoice",
 *             so cards that follow the older numbered-folder convention keep
 *             working.
 * Inputs:     name.  Outputs: the display start.
 * Accessors:  filesystem_installWavetables() (display names).
 * Affiliates: storage_naturalCompare() (ordering still uses the full name).
 */
const char *storage_stripOrderPrefix(const char *name);
```

### VX2-3.2 `Core/Hardware/SD/storageTypes.c` — add (end of file)

Implementations of VX2-3.1, same blocks.

- `storage_naturalCompare`:
  - Two cursors. When both are on digits, both runs are measured (skipping
    zeros), then compared by length and digits.
  - Otherwise the characters are compared after ASCII lower-casing.
  - The leading-zero tie is remembered and returned only if everything else
    is equal.
  - No allocation, no recursion, bounded by the string lengths (≤ 48
    characters from asyncfatfs).

### VX2-3.3 `Core/Hardware/SD/filesystem.h` lines 1371–1372 — modify

Remove `filesystem_installLoopsBlocking()`. Change the samples entry point
and add the report and progress hooks:

```c
/*
 * Modal sample-flash install report (S084 VX2 VX2-3.3).
 *
 * What:       counts of installed samples and wavetables, skipped files
 *             (unsupported, mismatched frames, no fit) and skipped tables
 *             (no fit, no valid frame), and whether the catalogue committed.
 * Why:        D13: the Load:[Samples] summary on the LCD.
 * Inputs:     filled by filesystem_installSamplesBlocking().
 * Outputs:    read by menu_loadSamplesModal().
 * Accessors:  menu.c.
 * Affiliates: SampleInstall.c counts.
 */
typedef struct {
    uint8_t  samples;
    uint8_t  wavetables;
    uint16_t skipped_files;
    uint8_t  skipped_tables;
    uint8_t  committed;
} fs_install_report_t;

#define FS_INSTALL_STAGE_ERASE      0u
#define FS_INSTALL_STAGE_STOCK      1u
#define FS_INSTALL_STAGE_SAMPLES    2u
#define FS_INSTALL_STAGE_WAVETABLES 3u
#define FS_INSTALL_STAGE_COMMIT     4u
typedef void (*fs_install_progress_fn)(uint8_t stage, uint16_t count);

/*
 * Install samples/ then wavetables/ into catalogue v2, blocking
 * (S084 VX2 VX2-3.3).
 *
 * What:       erases sample flash, generates the stock block, installs
 *             samples/ recursively and then every wavetable folder under
 *             wavetables/, commits the catalogue, and reports.
 * Why:        P2-A; replaces the samples/ + loops/ pair (loops/ retired,
 *             plan §4.1.5).
 * Inputs:     the card; report (nullable); the progress hook.
 * Outputs:    1 when the catalogue committed; *report.
 * Accessors:  menu_loadSamplesModal() only; audio must be suspended.
 * Affiliates: SampleInstall.c, storage_naturalCompare().
 */
uint8_t filesystem_installSamplesBlocking(fs_install_report_t *report);
void    filesystem_setInstallProgress(fs_install_progress_fn fn);
```

### VX2-3.4 `Core/Hardware/SD/filesystem.c` lines 23357–23373 — modify (installer state)

**Remove:**

- `FS_SAMPLE_MANIFEST_MAX`, `FS_SAMPLE_LFN_MAX`;
- `fs_sample_manifest_t`, `sample_manifest[]`, `sample_manifest_count`.

**Keep:** `FS_SAMPLE_BUFFER_SIZE` (512), `FS_SAMPLE_ELLIPSIS`,
`sample_io_buf`.

**Add:**

```c
/*
 * Batched natural-order selection state (S084 VX2 VX2-3.4).
 *
 * What:       fs_install_batch holds up to FS_INSTALL_BATCH directory
 *             entries (8.3 alias for opening, verified long name as the sort
 *             key, directory flag), kept sorted; each pass over a directory
 *             fills it with the next names after the last one installed.
 *             fs_install_last[d] is that "last" name for recursion level d.
 * Why:        D1: the former 13,440 B manifest sorted a whole folder in RAM
 *             and capped it at 120 files. Selection in batches of 32 needs
 *             about 1/7 of the RAM for any folder size and makes about
 *             ceil(n / 32) directory passes.
 * Inputs:     afatfs object scans.  Outputs: install order.
 * Accessors:  filesystem_installCollect() and the walkers below.
 * Affiliates: storage_naturalCompare(), FS_INSTALL_MAX_DEPTH (asyncfatfs
 *             has 5 handles: the folder chain plus one open file).
 */
#define FS_INSTALL_BATCH      32u
#define FS_INSTALL_MAX_DEPTH  3u
#define FS_INSTALL_KEY_LEN    (AFATFS_LONG_FILENAME_MAX + 1u)   /* 49 */

typedef struct {
    char    short_name[13];
    char    key[FS_INSTALL_KEY_LEN];
    uint8_t is_dir;
    uint8_t pad;
} fs_install_entry_t;

_Static_assert(sizeof(fs_install_entry_t) == 64u, "install entry is 64 B");

static fs_install_entry_t fs_install_batch[FS_INSTALL_BATCH];
static uint8_t fs_install_batch_count;
static fs_install_entry_t fs_install_last[FS_INSTALL_MAX_DEPTH + 1u];
static fs_install_report_t *fs_install_report;
static fs_install_progress_fn fs_install_progress;
```

### VX2-3.5 `Core/Hardware/SD/filesystem.c` lines 24366–24514 — remove (v1 LFN and manifest helpers)

Remove `filesystem_sampleNameFromFilename`, `filesystem_copySampleSortName`,
`filesystem_lfnCharAt`, `filesystem_lfnReset`, `filesystem_lfnAppendEntry`,
`filesystem_sortLower`, `filesystem_compareSampleNameStrict` and
`filesystem_insertSampleManifest`.

The asyncfatfs object finder (`afatfs_findNextObject`, already used by the
library scans) returns the checksum-verified long name in
`id.displayName` and the 8.3 alias in `id.shortName`, so the hand-built LFN
reassembly is no longer needed.

**Keep:** `filesystem_le16`, `filesystem_le32`, `filesystem_chunkIdEquals`,
`filesystem_isWavName` (bound changes to `FS_INSTALL_KEY_LEN`) and
`filesystem_displayNameFromFilename` (bound changes, VX2-3.6).

### VX2-3.6 `Core/Hardware/SD/filesystem.c` lines 24516–24590 — modify `filesystem_parseWav()`

New signature and body:

```c
typedef struct {
    uint32_t data_offset;
    uint32_t data_bytes;
    sample_install_fmt_t fmt;
} fs_wav_info_t;

static uint8_t filesystem_parseWav(const char *short_name,
                                   sample_install_kind_t kind,
                                   fs_wav_info_t *out);
```

- **Chunk walk:** the existing walk is kept (RIFF/WAVE check, chunk loop,
  odd-size pad, seek to the next chunk).
- **`fmt ` chunk:** read `min(chunk_size, 40)` bytes into a 40 B stack
  buffer and hand them to `sampleInstall_acceptFormat(buf, n, kind,
  &out->fmt)`. This replaces the fixed mono/44.1 kHz/16-bit test at lines
  24557–24562.
- **`data` chunk:** accepted when `data_bytes` ≥ one `block_align` and is a
  multiple of it.
- The 3-char name and the display name leave this function; the callers
  set the display name from the long name.

```c
/*
 * Read a WAV file's format and data location (S084 VX2 VX2-3.6).
 *
 * What:       walks RIFF chunks; accepts the fmt chunk through
 *             sampleInstall_acceptFormat() for the requested kind; records
 *             the data chunk's offset and size.
 * Why:        P2-A: the accepted forms differ for samples (v1 form) and
 *             wavetable frames (W1, D11); one parser, one acceptance rule.
 * Inputs:     8.3 name in the current directory, kind.
 * Outputs:    1 and *out when acceptable; 0 otherwise (file skipped).
 * Accessors:  filesystem_installSampleFiles(), filesystem_installWavetable().
 * Affiliates: SampleInstall.c, the blocking open/read/seek helpers.
 */
```

### VX2-3.7 `Core/Hardware/SD/filesystem.c` lines 24592–24688 — replace `filesystem_manifestCanFit` and `filesystem_scanSamples` with `filesystem_installCollect()`

```c
/*
 * Collect the next batch of directory entries in natural order
 * (S084 VX2 VX2-3.7).
 *
 * What:       one afatfs object scan of the current directory. Entries are
 *             kept in fs_install_batch if they match want_dir (directory) or
 *             are .wav files, do not start with '.', and sort strictly after
 *             *after (natural order of the long name, then strcmp, then the
 *             8.3 alias as the final tie-break). Only the capacity smallest
 *             are kept, sorted. The scan always ends with
 *             afatfs_findLastObject().
 * Why:        D1: bounded RAM for any folder size; deterministic order for
 *             names equal under the natural rules.
 * Inputs:     dir handle, want_dir, after (key empty = from the start),
 *             capacity 1..FS_INSTALL_BATCH.
 * Outputs:    fs_install_batch[0..fs_install_batch_count); 0 on a card error.
 * Accessors:  filesystem_installSampleTree(), filesystem_installWavetable(),
 *             filesystem_installWavetableTree().
 * Affiliates: storage_naturalCompare(), afatfs_findNextObject().
 */
static uint8_t filesystem_installCollect(afatfsFilePtr_t dir, uint8_t want_dir,
                                         const fs_install_entry_t *after,
                                         uint8_t capacity);
```

Folder iteration uses `capacity = 1`, so the batch is free again before
recursing. That is why folders are visited one at a time and files in
batches.

### VX2-3.8 `Core/Hardware/SD/filesystem.c` lines 24689–24810 — replace `filesystem_installOneSample`, `filesystem_installSampleFolderBlocking`, `filesystem_installSamplesBlocking`, `filesystem_installLoopsBlocking`

**New statics:**

| Function | Behaviour |
|---|---|
| `filesystem_installCopy(const char *short_name, uint32_t offset, uint32_t bytes, uint8_t frames)` | opens, seeks, reads 512 B chunks into `sample_io_buf`, and hands them to `sampleInstall_writeSampleData` (frames = 0) or `sampleInstall_writeFrameData` (frames = 1); closes |
| `filesystem_installSampleFiles(dir, depth)` | loop: `installCollect(dir, 0, &last, 32)`. For each entry, `parseWav(SAMPLE)`. If it passes and `beginSample(display, fmt, bytes)` succeeds, copy and finish (`samples++`, progress); else `skipped_files++`. `last` = the batch's final entry. Ends on an empty batch. |
| `filesystem_installSampleTree(dir, depth)` | `installSampleFiles` first; then, if `depth < FS_INSTALL_MAX_DEPTH`, iterate subfolders with capacity 1. For each: open (`filesystem_blockOpenDirLfn` on the alias), `blockChdir`, recurse with `depth + 1`, `afatfs_chdirParent()` in a blocking loop (check the enum, `FILESYSTEM_SPEC.md` warning), close. |
| `filesystem_installWavetable(dir, display)` | **Pass A** (validate): batches of files; `parseWav(FRAME)`. The first valid file sets `fmt` and `frame_len = data_bytes / block_align`. Later files must give the same `stored_bits` and `frame_len`, or they are skipped (`skipped_files++`). Count up to 128. If count ≥ 1: `beginWavetable(display, fmt, frame_len, count)`; on refusal (no fit), `skipped_tables++`. **Pass B** (copy): the same batches and the same acceptance rule; `installCopy(…, frame_len·block_align, 1)` for each accepted file until `count`; `finishWavetable`; `wavetables++`. |
| `filesystem_installWavetableTree(dir, depth)` | iterate subfolders (capacity 1). For each: chdir in; `installWavetable(…)` with the display name built from `storage_stripOrderPrefix(long name)` through `filesystem_displayNameFromFilename`; recurse while `depth < FS_INSTALL_MAX_DEPTH`; chdir parent. Files directly in `wavetables/` belong to no table and are ignored. |

**New public body:**

```c
uint8_t filesystem_installSamplesBlocking(fs_install_report_t *report)
{
    /* status/busy/fs checks as the former folder installer (24749-24756) */
    /* report = report ? report : &local; memset(report, 0, ...) */
    /* progress(ERASE);  sampleInstall_begin()            -> fail: done   */
    /* progress(STOCK);  sampleInstall_writeStockBlock()  -> fail: done   */
    /* progress(SAMPLES): open "samples" (case-insensitive); if present:   */
    /*     chdir; installSampleTree(dir, 0); chdir(NULL); close            */
    /* progress(WAVETABLES): the same for "wavetables" with                */
    /*     installWavetableTree(dir, 0)                                    */
    /* progress(COMMIT): sampleInstall_commit() -> report->committed      */
    /* status = IDLE; return report->committed                             */
}
```

Comment block:

```c
/*
 * Install samples/ and wavetables/ into catalogue v2 (S084 VX2 VX2-3.8).
 *
 * What:       full install: erase, stock block, samples/ (files first,
 *             then subfolders, natural order, depth <= 3), then every folder
 *             under wavetables/ that holds valid frames, then the commit. A
 *             missing folder is not an error. A file that is unsupported or
 *             does not fit is skipped and counted; a wavetable that does not
 *             fit is skipped whole (plan §4.1.4).
 * Why:        P2-A; loops/ is retired (plan §4.1.5), so there is no append
 *             pass, and the flash always matches this firmware's layout.
 * Inputs:     the card (audio suspended by the caller).
 * Outputs:    sample flash, *report, the return value.
 * Accessors:  menu_loadSamplesModal().
 * Affiliates: SampleInstall.c, filesystem_installCollect(),
 *             FILESYSTEM_SPEC.md "Load:[Samples]".
 */
```

`filesystem_setInstallProgress(fn)` stores the hook. Every stage change and
item completion calls it if it is non-NULL.

### VX2-3.9 `Core/Hardware/SD/filesystem.c` line 78 — add include

`#include "SampleInstall.h"` after `#include "SampleMemory.h"`.

### VX2-3.10 `Core/Menu/menu.c` lines 1049–1112 — modify `menu_loadSamplesModal()`

- **Remove:** the `filesystem_installLoopsBlocking()` call (line 1093), the
  `Loop upload` message, `loopsOk` and `failedMessage`.
- **Add:** `static void menu_installProgress(uint8_t stage, uint16_t count)`,
  which writes `Sample upload` on row 1 and one of these on row 2:
  - `Erasing...`
  - `Stock waves`
  - `Samples NNN`
  - `Wavetbl NNN`
  - `Saving index`

  It goes through `menu_setStorageMessage()`, and only on a stage change or
  every 4th item, to keep the LCD waits short.
- Call `filesystem_setInstallProgress(menu_installProgress)` before the
  install and `filesystem_setInstallProgress(NULL)` after it.
- **After `audioCodec_resume()`:**
  - on success, show `Smp NNN Wtb NN` / `Skip NN` for 1.5 s, then
    `menu_repaintAll()`;
  - on failure, `Sample upload` / `Failed`, as today.

```c
/*
 * Load:[Samples] modal, catalogue v2 (S084 VX2 VX2-3.10).
 *
 * What:       one install of samples/ and wavetables/ with stage progress
 *             and a summary (installed and skipped counts); the loops/ step
 *             is removed.
 * Why:        P2-A, D13; the install is longer (stock generation, frames),
 *             so the user sees progress instead of one static line.
 * Inputs:     the confirmed Load:[Samples] command.
 * Outputs:    LCD text; menu_numSamples; the usual command completion.
 * Accessors:  the Load page OK handler.
 * Affiliates: filesystem_installSamplesBlocking(), fs_install_report_t.
 */
```

### VX2-3.11 `Core/Menu/menu.c` line 1099 — keep

`menu_setNumSamples(sampleMemory_getNumSamples())` is unchanged.
`menu_numSamples` is written only here.

### VX2-3.12 `main.c` after line 582 (`sampleMemory_init();`) — add the boot notice

```c
    /*
     * Stale sample flash after a firmware update (S084 VX2 VX2-3.12).
     *
     * What:       when sample flash holds a v1 install (no v2 header, a v1
     *             count word present), shows "Samples invalid" /
     *             "reload samples" for 1.5 s.
     * Why:        Q7: nothing is retained across the catalogue change; D13
     *             wording. Without the notice, smp/slp voices would fall
     *             silent with no explanation.
     * Inputs:     sampleMemory_catalogueState().
     * Outputs:    LCD only; no storage access.
     * Accessors:  boot, once.
     * Affiliates: boot_delayMs(), SampleMemory.c refresh.
     */
    if (sampleMemory_catalogueState() == SAMPLE_CATALOGUE_STALE)
        boot_showSampleCatalogueNotice();
```

`boot_showSampleCatalogueNotice()` is a new static beside
`boot_showFxBufDiagnostic()` (line 344): `lcd_clear`, two rows,
`boot_delayMs(1500u)`.

### VX2-3.13 `Makefile` line 142 — add sources

After `Core/SampleRom/sampleFlash.c \`:

- `Core/SampleRom/SampleInstall.c \`
- `Core/SampleRom/StockWaveGen.c \`
- `Core/SampleRom/StockWaveRecipes.c \`

These are control code (-O2), not DSP. `Core/DSPAudio/StockWaves.c` and
`Core/DSPAudio/OscillatorWavetable.c` go in `DSP_SRCS` (VX2-4.13, VX2-5.9);
`Core/DSPAudio/DriveShaper.c` goes there too (VX2-7.10).

---

## 5. WP4 — Stock waves `sq3 sw3 sqa tra swa`

### VX2-4.1 `tools/gen_stock_recipes.py` (new)

- **Inputs:**
  - `tools/plots/stock_wave_harmonics.csv`: rows `wave, harmonic,
    magnitude, phase_rad`. `magnitude` is |c_k| scaled so the
    full-harmonic table peaks at 1; `phase_rad` = arg c_k; k = 1..511;
    waves in the order `sq3 sw3 sqa tra swa`.
  - `Core/DSPAudio/wavetable.c` (`sine_table[4096]`, parsed so the
    emulation uses the firmware's exact values).
- **Model** (identical to `lxr_plotlib.from_harmonics`): `x[m] = Σ_{k=1..h}
  (A_k cos θ + B_k sin θ)` with `θ = 2πkm/L`, `A_k = 2|c_k| cos φ_k`,
  `B_k = −2|c_k| sin φ_k`.
- **Quantisation:** per wave, `amax = max(|A|, |B|)`; `A_q = round(A ·
  32767 / amax)`, likewise `B_q` (int16).
- **Level scale:** for each level (L, h from §0.6), emulate the device sum
  in float32 with the firmware sine values, find the peak, and store
  `levelScale = 32767 / peak` (in float, folding `amax/32767` and the sine
  table's 1/32767). Every level peaks at full scale, the same rule as
  today's `sawTable`/`recTable`/`triTable` (each of their 11 levels peaks at
  32,767, measured).
- **Outputs:**
  - `Core/SampleRom/StockWaveRecipes.c`:

    ```c
    const int16_t stockWaveRecipeA[5][511];
    const int16_t stockWaveRecipeB[5][511];
    const float   stockWaveLevelScale[5][11];
    ```

    10,220 B + 220 B, with a generated-file header naming the CSV and its
    SHA-256.
  - `--emit-ref tools/dsp_test/frozen/stock_ref.bin`: the 5 × 6,112 int16
    points the device must produce (the emulation), for `test_stock_gen`.
- **Self-checks:**
  - the emulated tables against a float64 `irfft` of the same recipe:
    every level within −70 dB RMS of the ideal;
  - the quantised recipe error below −90 dB.
  - On failure it prints the levels and exits 1.

### VX2-4.2 `Core/SampleRom/StockWaveRecipes.c` (new, generated)

Generated by VX2-4.1; never edited by hand. Header block:

```c
/*
 * Stock-wave harmonic recipes (S084 VX2 VX2-4.2) — GENERATED.
 *
 * What:       per wave (sq3 sw3 sqa tra swa) the quantised cosine/sine
 *             coefficients of harmonics 1..511 and the peak scale of each of
 *             the 11 octave levels.
 * Why:        W4 / plan §4.1.2: the tables are generated at install from
 *             about 10 KB of flash instead of storing 61 KB twice.
 * Inputs:     tools/plots/stock_wave_harmonics.csv (SHA-256 below).
 * Outputs:    read by StockWaveGen.c.
 * Accessors:  stockWaveGen_programAll().
 * Affiliates: tools/gen_stock_recipes.py (regenerate; do not edit),
 *             tools/dsp_test/test_stock_gen.c.
 */
```

### VX2-4.3 `Core/SampleRom/StockWaveGen.h` / `.c` (new)

```c
/*
 * Generate and program the stock-wave block (S084 VX2 VX2-4.3).
 *
 * What:       for each of the 5 waves and 11 levels, computes every point
 *             additively from StockWaveRecipes.c, two points at a time, and
 *             programs each 32-bit word as soon as it is computed (no
 *             scratch RAM). Point m of a level with length L and h harmonics:
 *             sum_k A_q[k]*cos(t) + B_q[k]*sin(t), t index = (k*m*4096/L)
 *             & 4095 into sine_table (exact, since L divides 4096; cos =
 *             sine_table[(t + 1024) & 4095]); x = sum * levelScale, rounded
 *             and saturated to int16.
 * Why:        W4: oversampled-octave layout, 12,224 B per wave, generated
 *             at Load:[Samples] (plan §4.1.2).
 * Inputs:     base (SAMPLE_STOCK_START_ADDRESS); erased flash.
 * Outputs:    61,120 B programmed; 0 ok, else the sampleFlash status.
 * Accessors:  sampleInstall_writeStockBlock().
 * Affiliates: StockWaves.c (the same level offsets), sampleFlash.c,
 *             tools/gen_stock_recipes.py (the same arithmetic, emulated).
 */
int stockWaveGen_programAll(uint32_t base);
```

- **Cost:** about 8.4 M multiply-adds for all five waves (Σ L·h =
  1,678,192 per wave), roughly 0.4 s at 216 MHz, plus 15,280 word programs.
  Modal, with audio suspended; progress stage `STOCK`.
- **Accumulation:** float32, in the same harmonic order as the tool's
  emulation (k ascending). `test_stock_gen` allows ±1 LSB for compiler
  contraction differences.

### VX2-4.4 `Core/DSPAudio/StockWaves.h` / `.c` (new)

```c
/*
 * Stock-wave level access (S084 VX2 VX2-4.4).
 *
 * What:       maps (wave 0..4, level 0..10) to the level's flash pointer and
 *             length shift (32 - log2 L) inside the stock block, or to the
 *             1,024-point fallback table (sq3/sqa -> recTable, sw3/swa ->
 *             sawTable, tra -> triTable, shift 22) while the stock block is
 *             not valid (before the first install).
 * Why:        plan §4.1.2: until the first install the new waves fall back
 *             to Rec/Saw/Tri; one selection point keeps the readers simple.
 * Inputs:     wave, level; sampleMemory_stockValid().
 * Outputs:    stock_level_t {const int16_t *tab; uint8_t shift; uint16_t mask}.
 * Accessors:  Oscillator.c calcStockWaveBlock(_Fm), OscillatorPwm.c (pq3,
 *             pw3), transientGenerator.c (bursts).
 * Affiliates: StockWaveGen.c (the same offsets), SampleMemory.h.
 */
#define STOCK_WAVE_COUNT            5u
#define STOCK_WAVE_LEVELS           11u
#define STOCK_WAVE_POINTS_PER_WAVE  6112u

typedef struct {
    const int16_t *tab;
    uint8_t        shift;   /* index = phase >> shift */
    uint16_t       mask;    /* L - 1 */
} stock_level_t;

extern const uint16_t stockWave_levelOffset[STOCK_WAVE_LEVELS];
    /* 0, 1024, 2048, 3072, 4096, 5120, 5632, 5888, 6016, 6080, 6096 */
extern const uint8_t  stockWave_levelShift[STOCK_WAVE_LEVELS];
    /* 22, 22, 22, 22, 22, 23, 24, 25, 26, 28, 28 */

stock_level_t stockWave_level(uint8_t wave, uint8_t level);
```

- **Index form:** for power-of-two L, `phase >> shift` and the fraction
  `(phase << (32 − shift)) · 2^−32` are exactly the 32 × 32 → 64-bit
  multiply result the general `wtb` reader uses. The stock reader takes the
  cheaper shift form. (P2-B says "same 64-bit multiply index"; this is the
  same arithmetic for power-of-two lengths, at lower cost.)
- **Placement:** `stockWave_level` runs at block rate (once per oscillator
  per block). It is not ITCM.

### VX2-4.5 `Core/DSPAudio/Oscillator.h` lines 49–55 and 79 — modify (new wave ids, count)

After `#define CRASH 0x05`:

```c
/*
 * Generated stock waves (S084 VX2 VX2-4.5).
 *
 * What:       sq3 sw3 (303 captures), sqa swa (analog models), tra (dirty
 *             triangle), appended after CRASH so stored values 0..5 keep
 *             their meaning.
 * Why:        plan §4.1.2 / D2 names; W4 layout in sample flash.
 * Inputs:     the oscN_wave rows under type `wav`.
 * Outputs:    Oscillator.c dispatch.
 * Accessors:  Oscillator.c, OscillatorPwm.c (pq3 = SQ3, pw3 = SW3),
 *             transientGenerator.c (bursts), MenuText.h waveformNames[].
 * Affiliates: StockWaves.c.
 */
#define SQ3                 0x06
#define SW3                 0x07
#define SQA                 0x08
#define TRA                 0x09
#define SWA                 0x0A
#define STOCK_WAVE_FIRST    SQ3
```

Line 79: `#define OSC_STOCK_WAVE_COUNT (SWA + 1u)` (11).

Add after the type block:

```c
/*
 * Table-wave test for trigger phase offsets (S084 VX2 VX2-4.5).
 *
 * What:       1 for the band-limited table waves Tri, Saw, Rec and the five
 *             generated stock waves; 0 for Sin, Noi, Cym.
 * Why:        the engines start table waves at the (0xff << 20) offset;
 *             the former test `waveform > SINE && waveform <= REC` would
 *             start the new waves at phase 0.
 * Inputs:     waveform.  Outputs: 0/1.
 * Accessors:  the four engines' trigger paths (VX2-4.12).
 */
static inline uint8_t osc_isTableWave(uint8_t waveform)
{
    return (uint8_t)((waveform >= TRI && waveform <= REC) ||
                     (waveform >= SQ3 && waveform <= SWA));
}
```

### VX2-4.6 `Core/DSPAudio/Oscillator.c` — add `calcStockWaveBlock()` / `calcStockWaveFmBlock()` (after `calcWavetableOscBlock`, line ~768)

```c
/*
 * Render a generated stock wave (S084 VX2 VX2-4.6).
 *
 * What:       reads level osc->tableOffset of stock wave (waveform - SQ3)
 *             with linear interpolation: index = phase >> shift, next =
 *             (index + 1) & mask, frac = (phase << (32 - shift)) * 2^-32;
 *             per-sample slew as every table renderer. While the stock block
 *             is invalid it renders the fallback table through
 *             calcWavetableOscBlock() (same cost class).
 * Why:        plan §4.1.2: variable-length levels at the same cost as the
 *             existing 1,024-point tables (about 24 instructions; FM about
 *             35, modulation added as (mod x fmMod) << 19 like calcFmBlock).
 * Inputs:     osc (waveform, tableOffset, phase state), buf, size, gain.
 * Outputs:    buf; osc->phase, osc->phaseInc.
 * Accessors:  calcNextOscSampleBlock(), calcNextOscSampleFmBlock().
 * Affiliates: stockWave_level(), osc_calcWavetableFreqValue(). ITCM.
 */
INITCM void calcStockWaveBlock(OscInfo* osc, int16_t* buf, const uint8_t size,
                               const float gain);
INITCM void calcStockWaveFmBlock(OscInfo* osc, int16_t* modBuffer,
                                 int16_t* buf, uint8_t size, const float gain);
```

Prototypes go in `Oscillator.h` beside `calcWavetableOscBlock` (line ~205),
with the same block.

### VX2-4.7 `Core/DSPAudio/Oscillator.c` lines 498–528 and 658–686 — modify (dispatch)

In both `switch(osc->waveform)` blocks, add before `default:`:

```c
		case SQ3:
		case SW3:
		case SQA:
		case TRA:
		case SWA:
			/* S084 VX2 VX2-4.7: generated stock waves. */
			calcStockWaveBlock(osc, buf, size, gain);   /* FM: calcStockWaveFmBlock */
			break;
```

The single-sample `calcNextOscSample` / `calcNextOscSampleFm` (lines
705–735, 549–580) are not used by the engines. They get the same cases
through a one-sample block call, so the waveform-interpolation evaluator
(`osc_evalWaveAtPhase`, which renders one-sample blocks) stays correct.

### VX2-4.8 `Core/DSPAudio/Oscillator.c` lines 1006–1030 — modify (`osc_setFreq`)

In the `OSC_TYPE_WAV` switch, add `case SQ3: case SW3: case SQA: case TRA:
case SWA:` to the `SAW/TRI/REC` group (`osc_calcWavetableFreqValue`: the
same 2^32-per-cycle increment and octave level).

```c
/* S084 VX2 VX2-4.8: the generated waves use the table octave level. */
```

### VX2-4.9 `Core/DSPAudio/OscillatorPwm.c` — `pq3`/`pw3` on `sq3`/`sw3` with natural splits

**Line 90 `pwm_readWarp()` — modify** to take the level:

```c
static inline int32_t pwm_readWarp(const stock_level_t *lv, float p)
/* x = p * (mask + 1); i clamped to mask; n = (i + 1) & mask */
```

**Line 158 `pwm_warp()` — modify:** the constant `0.5f` becomes `s->mix`,
and the read takes the level:

```c
	const float p = (u < w) ? (u * s->warpA)
	                        : (s->mix + (u - w) * s->warpB);
	return pwm_readWarp(lv, p);
```

**Line 216 `pwm_familyTable()` — replace** with `pwm_familyLevel()`:

```c
/*
 * The family's table level (S084 VX2 VX2-4.9).
 *
 * What:       prc/psw/swp: sawTable level; ptr: triTable level (shift 22,
 *             mask 1023); pq3: stock sq3 level; pw3: stock sw3 level (their
 *             variable lengths, or the recTable/sawTable fallback before
 *             the first install).
 * Why:        P2-B: pq3/pw3 move from the P1 Rec/Saw stand-ins to the 303
 *             waves.
 * Inputs:     family, level.  Outputs: stock_level_t.
 * Accessors:  oscPwm_render(), oscPwm_renderFm(), oscPwm_prepare() (split).
 * Affiliates: stockWave_level().
 */
static inline stock_level_t pwm_familyLevel(uint8_t family, uint8_t offset);
```

**Lines 290–296 (`oscPwm_prepare` cases PQ3/PW3) — modify:** the natural
split `s0` is 0.78 (`sq3`) or 0.42 (`sw3`) when `sampleMemory_stockValid()`,
else 0.5 (the fallback tables' symmetric split):

```c
	case OSC_PWM_PQ3:
		s0 = sampleMemory_stockValid() ? 0.78f : 0.5f;
		w = s0 + (0.12f - s0) * c;
		warped = 1u;
		break;
	case OSC_PWM_PW3:
		s0 = sampleMemory_stockValid() ? 0.42f : 0.5f;
		w = s0 + (0.04f - s0) * c;
		warped = 1u;
		break;
```

**Line 326 (`if (warped)`) — add** `s->mix = s0;` (warped families store
their natural split in `mix`; `swp` keeps its blend there).

```c
/*
 * Natural split of warped families (S084 VX2 VX2-4.9).
 *
 * What:       c = 0 plays the wave unwarped (w = s0); c = 1 narrows to the
 *             family's minimum. The two slopes map [0, w) onto [0, s0) and
 *             [w, 1) onto [s0, 1). s0 is stored in OscPwmState.mix, which
 *             only swp otherwise uses, so OscInfo does not grow.
 * Why:        P2-B: sq3's edge sits at 0.78 of the cycle, sw3's reset at
 *             0.42; warping around 0.5 would shift and distort them at c = 0.
 * Inputs:     family, c, stock validity.  Outputs: w, warpA, warpB, mix.
 * Accessors:  oscPwm_prepare().
 * Affiliates: pwm_warp(), tools/plots/plot_pwm.py (pq3/pw3 curves).
 */
```

**Lines 367/426 (both renderers):** `tab = pwm_familyTable(…)` becomes
`const stock_level_t lv = pwm_familyLevel(family, osc->tableOffset);`.

- `prc`/`psw` keep `pwm_read1024(lv.tab, …)` (always 1,024 points).
- The warped cases pass `&lv`.
- `swp` is unchanged (it selects its own tables).

**`OscPwmState` comment in `OscillatorPwm.h` (line 63)** gains: "mix: swp
segment blend; warped families (ptr, pq3, pw3): natural split s0 (S084
VX2)".

### VX2-4.10 `Core/Menu/MenuText.h` line 133 — modify `waveformNames`

```c
/* S084 VX2 VX2-4.10: the 11-entry `wav` list (plan §4.1.2). */
static const char waveformNames[][4] = {
    {11}, {"Sin"},{"Tri"},{"Saw"},{"Rec"},{"Noi"},{"Cym"},
    {"sq3"},{"sw3"},{"sqa"},{"tra"},{"swa"},
};
```

The count must equal `OSC_STOCK_WAVE_COUNT` (asserted in VX2-9.6).

### VX2-4.11 `Core/DSPAudio/modulationNode.c` line 77 — no change

`modNode_getMaxWaveformIndex()` returns `OSC_STOCK_WAVE_COUNT − 1`, now
10.

- The `wav`-type domain in `instrumentManager_oscWaveMax()` (line 2351) and
  the waveform-interpolation bound in `osc_waveInterpActive()` (line 142)
  follow the constant.
- **Interpolation across the new waves:** `calcPeriodicInterpBlock()` calls
  the block renderer per neighbour, so it covers the new waves with no
  change.

### VX2-4.12 Engines' trigger phase offsets — modify

| File | Line | Change |
|---|---|---|
| `Drum/DrumVoice.c` | 230 | `else if(voice->osc.waveform > SINE && voice->osc.waveform <= REC)` → `else if(osc_isTableWave(voice->osc.waveform))` |
| `Snare/Snare.c` | 173 | same |
| `Cymbal/CymbalVoice.c` | 181 | same |
| `HiHat/HiHat.c` | 195 | same |

Comment (once per site):

```c
/* S084 VX2 VX2-4.12: table waves, including sq3..swa, start at the
** table offset (osc_isTableWave()). */
```

### VX2-4.13 `Makefile` `DSP_SRCS` (line 148 onward) — add

`Core/DSPAudio/StockWaves.c \` after `Core/DSPAudio/squareRootLut.c \`.

---

## 6. WP5 — `wtb` oscillator type

### VX2-5.1 `Core/DSPAudio/Oscillator.h` lines 57–78 — modify (type 4)

```c
#define OSC_TYPE_WTB          4u   /* S084 VX2 VX2-5.1, appended */
#define OSC_TYPE_COUNT        5u
```

In the type block comment, replace "OSC_TYPE_WTB (4) is appended in P2" with
"OSC_TYPE_WTB plays an installed wavetable (OscillatorWavetable.c): `src`
selects the table, `wav` 0..127 the frame position". Add
`OscillatorWavetable.c` to Accessors.

### VX2-5.2 `Core/DSPAudio/Oscillator.h` line 139 — modify (`pwm` becomes a union member)

```c
	/*
	 * Per-type block state (S084 VX2 VX2-5.2).
	 *
	 * What:       pwm (OscPwmState) for type pwm; wtb (OscWtbState) for type
	 *             wtb. An anonymous union: only the active type's member is
	 *             live, and each type re-initialises its member on bind or
	 *             prepare, so a type change never reads the other's bytes.
	 * Why:        keeps sizeof(OscInfo) at 120 B (runtime-slot margin, §0.5);
	 *             the two states are never needed together.
	 * Writers:    oscPwm_prepare() (pwm); oscWavetable_bind() and the wtb
	 *             renderers (wtb).
	 * Readers:    the matching renderers.
	 */
	union {
		OscPwmState	pwm;
		OscWtbState	wtb;
	};
```

`#include "OscillatorWavetable.h"` beside `#include "OscillatorPwm.h"`
(line 46). The header needs only `<stdint.h>` for `OscWtbState`, so there is
no include cycle; the render prototypes take `struct OscStruct *`, as
`OscillatorPwm.h` does.

### VX2-5.3 `Core/DSPAudio/OscillatorWavetable.h` (new)

```c
/*
 * OscillatorWavetable — installed wavetables for the wtb type
 * (S084 VX2 VX2-5.3).
 *
 * What:       plays one catalogue wavetable: frames of any length, 8- or
 *             16-bit, exactly as stored. Every sample reads two adjacent
 *             frames at the same phase and blends them by the frame
 *             position, which `wav` (0..127) maps onto 0..frame_count-1 and
 *             which ramps per sample across each block. The table index and
 *             fraction come from one 32 x 32 -> 64-bit multiply of the phase
 *             by the frame length, so every length costs the same.
 * Why:        plan §4.1.4, W1 (stored as supplied), W2 (optimised on the
 *             host), W3 (one level, no mips). Constant cost: both frames are
 *             always read; wtb never uses the render pool.
 * Inputs:     OscInfo (source, waveform, phase, phaseInc, phaseIncStep,
 *             fmMod, wtb state); the catalogue (SampleMemory).
 * Outputs:    rendered blocks.
 * Accessors:  Oscillator.c dispatch, InstrumentManager.c writers (bind).
 * Affiliates: SampleMemory.h (WavetableInfo), osc_setFreq() (2^32/cycle).
 */
#ifndef OSCILLATORWAVETABLE_H_
#define OSCILLATORWAVETABLE_H_
#include <stdint.h>

typedef struct {
    uint32_t base;          /* absolute address of frame 0; 0 = silent */
    float    pos;           /* current frame position */
    uint32_t frame_bytes;   /* frame_len * bits / 8 */
    uint32_t generation;    /* catalogue generation at bind */
    uint16_t frame_len;
    uint8_t  frame_count;
    uint8_t  bits;          /* 8 or 16 */
    uint8_t  index;         /* bound wavetable */
    uint8_t  primed;        /* pos is valid */
    uint16_t reserved;
} OscWtbState;              /* 24 B */

struct OscStruct;
void oscWavetable_bind(struct OscStruct *osc);
void oscWavetable_render(struct OscStruct *osc, int16_t *buf, uint8_t size,
                         float gain);
void oscWavetable_renderFm(struct OscStruct *osc, int16_t *modBuffer,
                           int16_t *buf, uint8_t size, float gain);
#endif
```

Plus, in `Oscillator.h` after the union (OscPwmState is visible there):
`_Static_assert(sizeof(OscWtbState) <= sizeof(OscPwmState), "wtb state must
fit the pwm union slot");`.

Per-prototype blocks:

- **`oscWavetable_bind`.**
  - **What:** when `source`, the type or the catalogue generation changed,
    resolve `sampleMemory_getWavetableInfo(min(sourceRequest, count − 1))`
    into the state and clear `primed`.
  - **No tables:** `base` = 0 (silent).
  - **Why:** block rate; mirrors `oscSample_bind()`, including the P1
    review fix F2 rule that the request byte survives a smaller install.
  - **Accessors:** the renderers (every block, a cheap compare) and the IM
    type/source writers.
- **`oscWavetable_render` / `_renderFm`.**
  - **What:** as the file block.
  - **Per block:** `target = waveform · (count − 1) / 127`. If not primed,
    `pos = target`. `step = (target − pos) / size`.
  - **Per sample:**
    1. `pos += step`; `fi = (uint32)pos`; `ff = pos − fi`;
       `fn = fi + (fi < count − 1)`.
    2. `m = (uint64)readPhase · frame_len`; `i = m >> 32`;
       `fr = (uint32)m · 2^−32`; `n = (i + 1 == len) ? 0 : i + 1`.
    3. `a = A[i] + fr·(A[n] − A[i])`; `b = B[i] + fr·(B[n] − B[i])`;
       `out = a + ff·(b − a)`.
    4. Slew add as every renderer.
  - **Depth:** 8-bit samples are sign-extended and shifted `<< 8`. Two loop
    copies (8- and 16-bit), selected once per block.
  - **FM:** `readPhase = phase + ((mod · fmMod) << 19)` (the `calcFmBlock`
    depth).
  - **Silent table:** `memset` 0, with phase and increment advanced exactly
    as the loop would (so sync and `itp` stay aligned).
  - **Cost:** about 45 (FM 60) + 2 for the multiply (plan §4.1.4). ITCM.

### VX2-5.4 `Core/DSPAudio/OscillatorWavetable.c` (new)

Implements VX2-5.3 with the same blocks. Includes `Oscillator.h`,
`OscillatorWavetable.h`, `SampleMemory.h`, `config.h`. Frame data is read
through `SAMPLE_FLASH_PTR(base)`. The renderers are `INITCM`.

### VX2-5.5 `Core/DSPAudio/Oscillator.c` lines 485–492 and 645–652 — modify (dispatch)

In `calcNextOscSampleFmBlock()` and `calcNextOscSampleBlock()`, after the
SMP/SLP test:

```c
	/* S084 VX2 VX2-5.5: wavetable type. */
	if (osc->oscType == OSC_TYPE_WTB) {
		oscWavetable_renderFm(osc, modBuffer, buf, size, gain); /* plain: oscWavetable_render */
		return;
	}
```

Update the dispatch comment block's What line: "pwm, sample and wavetable
types go to their own renderers".

### VX2-5.6 `Core/DSPAudio/Oscillator.c` lines 992–1004 — modify (`osc_setFreq`)

```c
			case OSC_TYPE_WTB:
				/* S084 VX2 VX2-5.6: 2^32 phase units per cycle, as the
				** table waves; no octave level (W3). */
				osc->phaseIncTarget = freq2PhaseIncr1024(currentFreq);
				break;
```

### VX2-5.7 `Core/DSP/Instruments/InstrumentManager.c` — writers and max rules

**Line 2370 `instrumentManager_oscSourceMax()` — add:**

```c
    case OSC_TYPE_WTB:
        /* S084 VX2 VX2-5.7: one installed wavetable per src value. */
        count = sampleMemory_getNumWavetables();
        return count ? (uint8_t)(count - 1u) : 0u;
```

**Line 2351 `instrumentManager_oscWaveMax()`:** no code change (non-`wav`
types are 127). The block's What line gains "wtb: frame position 0..127".

**Line 3875 (`IM_SPECIAL_OSC_TYPE`), 3896 (`_OSC_SOURCE`), 3908
(`_OSC_WAVE`):** where each calls `oscSample_bind(osc)` for SMP/SLP, add
`else if (osc->oscType == OSC_TYPE_WTB) oscWavetable_bind(osc);`.

```c
/* S084 VX2 VX2-5.7: wtb resolves its table on type/source changes, as the
** sample types do; the position needs no bind. */
```

### VX2-5.8 Engines' trigger phase — modify

Insert after the PWM branch in each engine (Drum line 227, Snare 170,
Cymbal 178, HiHat 192):

```c
		else if(voice->osc.oscType == OSC_TYPE_WTB)
			/* S084 VX2 VX2-5.8: wavetables start at the table offset;
			** `waveform` is a frame position, not a wave id. */
			voice->osc.phase = (0xffu<<20)*offset;
```

Without it a `wtb` oscillator at position 0 would take the `SINE` branch.
Cymbal's and HiHat's modulators keep phase 0 for every non-sample type (as
in P1).

### VX2-5.9 `Makefile` `DSP_SRCS` — add

`Core/DSPAudio/OscillatorWavetable.c \` after
`Core/DSPAudio/OscillatorSample.c \` (line 166).

---

## 7. WP6 — Transient type `trt` and the transient generator

### VX2-6.1 `Core/DSPAudio/transientGenerator.h` lines 49–69 — modify (struct, constants, API)

```c
/*
 * Transient types and the `wav` list (S084 VX2 VX2-6.1).
 *
 * What:       trs: non-PCM shaping, wav byte 0 = Snp (snap EG on pitch),
 *             1 = Ofs (phase offset). wav: byte 0..11 = the 12 firmware PCM
 *             transients, 12..12+S-1 = installed samples, then the 11 stock
 *             oscillator waves (Sin..swa) as 50 ms pitched bursts with a
 *             fixed decay; at most 256 entries (S <= 233). buf (2) is
 *             reserved for P3 and not selectable in P2.
 * Why:        plan §4.3.1, Q13-Q15: one type row fixes what automation and
 *             modulation of the wav row can reach; sample positions stay
 *             stable because the stock waves come after them.
 * Inputs:     the trt and transient_wave rows.
 * Outputs:    the resolved kind used by the engines and transient_calcBlock().
 * Accessors:  InstrumentManager.c writers, the four engines,
 *             InstrumentCellFormat.c.
 * Affiliates: transientTables.c (PCM), SampleMemory.h (samples, gain),
 *             StockWaves.h and Oscillator.h (bursts).
 */
#define TRANSIENT_TYPE_TRS          0u
#define TRANSIENT_TYPE_WAV          1u
#define TRANSIENT_TYPE_BUF          2u
#define TRANSIENT_TYPE_SELECTABLE   2u   /* P3 raises to 3 */
#define TRANSIENT_STOCK_PCM_COUNT   NUM_TRANSIENTS      /* 12 */
#define TRANSIENT_WAV_LIST_MAX      256u
#define TRANSIENT_BURST_SAMPLES     TRANSIENT_SAMPLE_LENGTH   /* 2,205 = 50 ms */

#define TRANSIENT_KIND_NONE   0u
#define TRANSIENT_KIND_SNAP   1u
#define TRANSIENT_KIND_OFFSET 2u
#define TRANSIENT_KIND_PCM    3u
#define TRANSIENT_KIND_SAMPLE 4u
#define TRANSIENT_KIND_TABLE  5u   /* Tri Saw Rec sq3..swa bursts */
#define TRANSIENT_KIND_SINE   6u
#define TRANSIENT_KIND_NOISE  7u
#define TRANSIENT_KIND_CRASH  8u

typedef struct TransientGeneratorStruct
{
	int16_t 	output;
	uint8_t		type;		/* TRANSIENT_TYPE_*  (VX2) */
	uint8_t		waveRequest;	/* unclamped wav byte (VX2, as OscInfo F2) */
	uint32_t	phase;		/* PCM/bursts: 2^32 per table; samples: Q20.12 */
	float	 	pitch;		/* 0.25 .. 1.75 from frq, unchanged */
	uint8_t		waveform;	/* live wav byte, clamped to the type */
	uint8_t		kind;		/* resolved TRANSIENT_KIND_* (VX2) */
	uint8_t		source;		/* PCM row, sample index or stock wave id */
	uint8_t		gainCode;	/* sample catalogue gain (VX2) */
	float		volume;
	uint16_t	midiFreq;	/* upper 8 bit coarse, lower 8 bit fine */
	uint16_t	burstLeft;	/* samples left in a stock burst (VX2) */
	uint32_t	dataAddr;	/* sample: absolute flash address (VX2) */
	uint32_t	frames;		/* sample frames (VX2) */
	uint32_t	generation;	/* catalogue generation at resolve (VX2) */
	float		env;		/* stock-burst envelope (VX2) */
} TransientGenerator;		/* 40 B (was 24 B) */

void    transient_init(TransientGenerator* transient);
void    transient_trigger(TransientGenerator* transient);
void    transient_calcBlock(TransientGenerator* transient, int16_t* buf,
                            const uint8_t size);
void    transient_setType(TransientGenerator* transient, uint8_t type);
void    transient_setWave(TransientGenerator* transient, uint8_t wave);
uint8_t transient_waveMax(uint8_t type);

static inline uint8_t transient_isSnap(const TransientGenerator* t)
{ return (uint8_t)(t->kind == TRANSIENT_KIND_SNAP); }
static inline uint8_t transient_isOffset(const TransientGenerator* t)
{ return (uint8_t)(t->kind == TRANSIENT_KIND_OFFSET); }
```

**Removed:** `transient_calc()` (no callers) and `transient_setWaveform()`
(replaced by the pair above).

Per-prototype blocks:

- **`transient_setType`.**
  - **What:** type = `min(type, TRANSIENT_TYPE_SELECTABLE − 1)`; waveform =
    `min(waveRequest, transient_waveMax(type))`; resolve.
  - **Why:** the P1 F2 rule; a type round trip keeps the chosen entry.
  - **Accessors:** the `IM_SPECIAL_TRANSIENT_TYPE` writer.
- **`transient_setWave`.**
  - **What:** `waveRequest = wave`; waveform = `min(wave, max(type))`;
    resolve.
  - **Accessors:** the `IM_SPECIAL_TRANSIENT_WAVE` writer.
- **`transient_waveMax`:** `trs` → 1; `wav` → `12 + S + 11 − 1` (S =
  installed samples); `buf` → 0. Used by the writers and by
  `InstrumentCellFormat`.
- **`transient_isSnap` / `transient_isOffset`:** replace the engines'
  `waveform == 0` / `== 1` tests, which no longer mean Snp/Ofs under `wav`.

### VX2-6.2 `Core/DSPAudio/transientGenerator.c` lines 39–105 — rewrite

**Statics:**

```c
#define TRANSIENT_BURST_DECAY    0.99773f   /* exp(-1 / (0.010 * 44108)) */
#define TRANSIENT_BURST_BASE_HZ  440.0f
```

**`transient_resolve(t)`** (static), called by both setters and by
`calcBlock` on a catalogue generation change:

| type / waveform | kind | source |
|---|---|---|
| `trs`, 0 / 1 | SNAP / OFFSET | — |
| `wav`, 0..11 | PCM | row = waveform |
| `wav`, 12..12+S−1 | SAMPLE | index; also `dataAddr`, `frames`, `gainCode` from `sampleMemory_getSampleInfo()`, plus `generation` |
| `wav`, after the samples | SINE / TABLE / NOISE / CRASH by stock id (0 Sin, 1–3 Tri/Saw/Rec, 4 Noi, 5 Cym, 6–10 sq3..swa) | stock id |
| `buf` or anything else | NONE | — |

**`transient_trigger`:** `phase = 0`, `env = 1`, `burstLeft =
TRANSIENT_BURST_SAMPLES`.

**`transient_calcBlock`:**

| kind | Loop |
|---|---|
| NONE / SNAP / OFFSET | `memset` 0 (as today for bytes ≤ 1) |
| PCM | **today's loop, unchanged** (table row = `source`), so the 12 PCM transients sound as before |
| SAMPLE | Q20.12: `inc = pitch · 4096` (block rate), `g = volume · gainFromCode(gainCode)` (block rate; the int16 sample is already full scale, like the PCM path's `<< 8`); per sample `idx = phase >> 12`, interpolated read of `idx` and `idx + 1`, output 0 and phase held once `idx + 1 ≥ frames` (one-shot) |
| SINE | `inc = (440 · pitch) · 2^32 / REAL_FS` (block rate); `sine_table[phase >> 20]` with interpolation; × `env · volume`; `env *= DECAY` |
| TABLE | level `osc_freqToTableIndex(440 · pitch)`; `stockWave_level()` for `sq3..swa`, or the 1,024-point `triTable`/`sawTable`/`recTable` level for Tri/Saw/Rec; read like `calcStockWaveBlock` |
| NOISE | 32-bit LCG per sample (`x = x·1664525 + 1013904223`, state in `phase`); `(int16)(x >> 16) · env · volume` |
| CRASH | `crashSample[phase >> 17]`, `(s − 127) · 256`, × `env · volume` |

- **Bursts:** each burst loop runs `n = min(size, burstLeft)` samples, then
  `memset`s the rest; `burstLeft −= n`.
- **Generation check:** at block start, a SAMPLE kind whose `generation`
  differs from `sampleMemory_getGeneration()` re-resolves (a reinstall
  while a voice holds a sample transient).

Comment block on `transient_calcBlock()`:

```c
/*
 * Render one transient block by kind (S084 VX2 VX2-6.2).
 *
 * What:       PCM: the original loop. SAMPLE: one-shot Q20.12 read with the
 *             catalogue gain, pitch from frq. Stock bursts: a 50 ms pitched
 *             read of the stock wave at f = 440 Hz x pitch, decaying with
 *             tau = 10 ms (-43 dB at the gate, D5). trs kinds output silence
 *             (their effect is applied by the engine).
 * Why:        plan §4.3.1, Q14 (bursts), Q15 (gain everywhere); cost cap
 *             = today's 22 instructions per sample, 0 divisions (D6): each
 *             loop is measured with fpseq.py and loses its interpolation if
 *             it exceeds the cap.
 * Inputs:     transient state, buf, size.
 * Outputs:    buf; phase, env, burstLeft.
 * Accessors:  the four engines' render functions.
 * Affiliates: transient_resolve(), sampleMemory_gainFromCode(),
 *             stockWave_level(), osc_freqToTableIndex().
 */
```

### VX2-6.3 `Core/DSP/Instruments/InstrumentManager.c` line 3782 — modify (`IM_SPECIAL_TRANSIENT_WAVE`) and add `IM_SPECIAL_TRANSIENT_TYPE`

```c
    /*
     * Transient type and wave writers (S084 VX2 VX2-6.3).
     *
     * What:       TYPE selects trs/wav (buf reserved); WAVE records the
     *             requested byte and clamps the live one to the type's list.
     *             Both re-resolve the transient kind.
     * Why:        plan §4.3.1; the P1 review fix F2 rule for type-dependent
     *             ranges (a type round trip keeps the choice).
     * Inputs:     byteValue.  Outputs: TransientGenerator type/wave/kind.
     * Accessors:  instrumentManager_writeRuntime() (selector pre-pass and
     *             menu/MIDI for TYPE; Morph base, automation and file apply
     *             for WAVE).
     * Affiliates: transient_setType(), transient_setWave().
     */
    case IM_SPECIAL_TRANSIENT_TYPE: {
        TransientGenerator *transient = instrumentManager_transient(slot);
        if (!transient) return 0u;
        transient_setType(transient, byteValue);
        return 1u; }
    case IM_SPECIAL_TRANSIENT_WAVE: {
        TransientGenerator *transient = instrumentManager_transient(slot);
        if (!transient) return 0u;
        transient_setWave(transient, byteValue);
        return 1u; }
```

Add a public accessor next to `instrumentManager_oscSection()` (line 2428),
with its prototype in `InstrumentManager.h`:

```c
/*
 * A slot's transient type (S084 VX2 VX2-6.3).
 *
 * What:       TRANSIENT_TYPE_* of the slot's live transient generator; trs
 *             when the slot has none.
 * Why:        InstrumentCellFormat formats the transient wav cell by type.
 * Inputs:     slot.  Outputs: type.
 * Accessors:  InstrumentCellFormat.c.
 * Affiliates: instrumentManager_transient() (static).
 */
uint8_t instrumentManager_transientType(uint8_t slot);
```

### VX2-6.4 `Core/DSP/Instruments/InstrumentManager.h` line 156 — modify (writer enum)

After `IM_SPECIAL_RUN_MODE,`:

```c
    /*
     * S084 VX2 VX2-6.4 / VX2-7.6: transient type (trt) and filter drive type
     * (dty). Both are non-morphable selector rows applied by the selector
     * pre-pass (instrumentManager_isSelectorDescriptor()).
     */
    IM_SPECIAL_TRANSIENT_TYPE,
    IM_SPECIAL_DRIVE_TYPE,
```

`IM_SPECIAL_WRITER_COUNT` becomes 25 ≤ 32; the existing `_Static_assert`
covers it.

### VX2-6.5 Engines — replace the Snp/Ofs tests

| File | Line(s) | Change |
|---|---|---|
| `Drum/DrumVoice.c` | 206, 213 | `voice->transGen.waveform==1` → `transient_isOffset(&voice->transGen)` |
| `Drum/DrumVoice.c` | 275 | `voice->transGen.waveform == 0` → `transient_isSnap(&voice->transGen)` |
| `Snare/Snare.c` | 162 / 223 | Offset / Snap, the same |
| `Cymbal/CymbalVoice.c` | 170 / 237 | the same |
| `HiHat/HiHat.c` | 184 / 249 | the same |

Comment (once per engine, at the trigger site):

```c
/* S084 VX2 VX2-6.5: Snp/Ofs are trs kinds; under wav, bytes 0/1 are PCM
** transients (transient_isSnap()/transient_isOffset()). */
```

The `transient_calcBlock()` calls (Drum 368, Snare 267, Cymbal 297, HiHat
308) are unchanged.

---

## 8. WP7 — Filter drive types `dty`

### VX2-7.1 `Core/DSPAudio/DriveShaper.h` (new)

```c
/*
 * DriveShaper — the filter input stage per drive type (S084 VX2 VX2-7.1).
 *
 * What:       14 shapers g(u) that replace the ZDF filter's input soft clip,
 *             u = input x drive (drive = 0.4 + 6 (drv/127)^2, unchanged):
 *             sft rct art thf fdc bsw tsw lsw lsq sfd afd asf chb rsb.
 *             Each output is level-trimmed so a sine at drv 64 has sft's RMS,
 *             capped so the drv-127 peak stays within sft's ceiling of 2.0.
 *             The stateful types (art fdc tsw lsw lsq rsb) detect zero
 *             crossings on u with hysteresis 0.03 and keep their state in
 *             DriveShaperState; rct art fdc are DC-blocked (one pole, R =
 *             0.9986, about 10 Hz). Trackers use 1/period computed once per
 *             block (driveShaper_blockInc()), so no shaper divides per
 *             sample.
 * Why:        plan §4.4, r3 naming; the definitions are those of
 *             tools/plots/plot_waveshapers.py, which is the reference.
 * Inputs:     type, state, u, and s = u N(u^2/4) / D(u^2/4) (the sft output
 *             already computed by the shared Stage A, D7).
 * Outputs:    the shaped filter input.
 * Accessors:  ResonantFilter.c SVF_calcBlockZDFShaped(); the engines
 *             (state reset); InstrumentManager.c (type writer).
 * Affiliates: tools/plots/plot_waveshapers.py (definitions, trims, the
 *             --export-ref arrays for tools/dsp_test/test_shaper.c).
 */
#ifndef DRIVESHAPER_H_
#define DRIVESHAPER_H_
#include <stdint.h>

#define DRIVE_TYPE_SFT 0u
#define DRIVE_TYPE_RCT 1u
#define DRIVE_TYPE_ART 2u
#define DRIVE_TYPE_THF 3u
#define DRIVE_TYPE_FDC 4u
#define DRIVE_TYPE_BSW 5u
#define DRIVE_TYPE_TSW 6u
#define DRIVE_TYPE_LSW 7u
#define DRIVE_TYPE_LSQ 8u
#define DRIVE_TYPE_SFD 9u
#define DRIVE_TYPE_AFD 10u
#define DRIVE_TYPE_ASF 11u
#define DRIVE_TYPE_CHB 12u
#define DRIVE_TYPE_RSB 13u
#define DRIVE_TYPE_COUNT 14u

#define DRIVE_SHAPER_HYST     0.03f
#define DRIVE_SHAPER_DC_R     0.9986f
#define DRIVE_SHAPER_ENV_REL  0.998867f   /* exp(-1 / (0.020 * 44108)) */

#define DRIVE_FLAG_ARMED      0x01u
#define DRIVE_FLAG_NEG_ARMED  0x02u
#define DRIVE_FLAG_TOGGLE     0x04u
#define DRIVE_FLAG_LATCHED    0x08u

typedef struct {
    float    ramp;      /* tracker ramp 0..1 (starts 1) */
    float    env;       /* latched cycle amplitude (|s|/2 scale) */
    float    curMax;    /* peak of the running cycle */
    float    dcX, dcY;  /* DC blocker state */
    uint16_t since;     /* samples since the last rising crossing (saturating) */
    uint16_t period;    /* last measured period in samples (starts 200) */
    uint8_t  flags;     /* DRIVE_FLAG_* */
    uint8_t  count;     /* fdc crossing counter, mod 3 */
    uint16_t reserved;
} DriveShaperState;     /* 28 B */

extern const float driveShaper_trim[DRIVE_TYPE_COUNT];

void driveShaper_reset(DriveShaperState *st);

static inline float driveShaper_blockInc(const DriveShaperState *st)
{
    const uint16_t p = (st->period < 4u) ? 4u : st->period;
    return 1.0f / (float)p;          /* one division per block */
}

static inline float driveShaper_apply(uint8_t type, DriveShaperState *st,
                                      float u, float s, float inc);
#endif
```

**`driveShaper_apply()`** (in the header, `static inline`).
`triFold(v, t) = t − 4t·|frac((v + t)/(4t)) − 0.5|`, where `frac` is
computed without `floorf`: `n = (int32)(w · 2^24) & 0xFFFFFF`,
`frac = n · 2^−24` (valid for |w| < 128). `sinFold(v)` is
`t·(1.5707963 + t²·(−0.6459640 + t²·(0.0796926 − t²·0.0046817)))` with
`t = triFold(v, 1)`.

| Type | Output before trim |
|---|---|
| `sft` | `s` (never reached: `sft` uses `SVF_calcBlockZDF()`) |
| `rct` | `dc(|s|)` |
| `art` | crossing; rising toggles; `dc((s > 0 && toggle) ? s : 0)` |
| `thf` | `triFold(u, 0.6) / 0.6` |
| `fdc` | rising or falling: `count = (count + 1) mod 3`; `y = (count == 0) ? |s| : 0`; `dc(min(y, 1.3) · (2 / 1.3))` |
| `bsw` | `q = (int)((0.5s + 1)·127.5 + 0.5)` clamped to 0..255; `r = ((q & 15) << 4) | (q >> 4)`; `r / 127.5 − 1` |
| `tsw` / `lsw` | crossing + amplitude latch (below); rising toggles; `tsw` (or `lsw` on toggle) sets `ramp = 0`; `ramp = min(1, ramp + inc)` (`lsw`: `0.5·inc`); `2·env·(1 − 2·ramp)` |
| `lsq` | crossing + latch; rising toggles; `2·env·(toggle ? 1 : −1)` |
| `sfd` | `sinFold(u)` |
| `afd` | `sinFold(u + 0.05u³)` |
| `asf` | `triFold(u + 0.35, 1) − 0.35` |
| `chb` | `x = 0.5s`; `3x − 4x³` |
| `rsb` | crossing; rising toggles; `s · (toggle ? 1 : −1)` |

- **Crossing** (the stateful types, exactly the script's `_crossings`):
  - rising if `ARMED && u > HYST` (then clear `ARMED`);
  - `u < −HYST` sets `ARMED`;
  - falling likewise with `NEG_ARMED`.
  - The script's extra `prev ≤ HYST` test is implied by the armed flag, so
    no previous sample is stored.
- **Latch** (`tsw lsw lsq`):
  - per sample: `since` saturating `++`;
    `curMax = max(curMax, 0.5·|s|)`;
  - on rising: `period = since`, `since = 0`, `env = curMax`, `curMax = 0`,
    set `LATCHED`;
  - before the first latch: `env = max(0.5·|s|, env · ENV_REL)`.
- **Return:** `y · driveShaper_trim[type]`.

### VX2-7.2 `Core/DSPAudio/DriveShaper.c` (new)

```c
/*
 * Drive-type level trims and state reset (S084 VX2 VX2-7.2).
 *
 * What:       driveShaper_trim[] in dtype order, the linear gains printed by
 *             tools/plots/plot_waveshapers.py level_trims() (exported with
 *             its --emit-c option); driveShaper_reset() sets the script's
 *             initial state (ramp 1, period 200, both arm flags, the rest 0).
 * Why:        plan §4.4 level trims; one table shared by the filter and the
 *             host test.
 * Inputs:     none.  Outputs: the table; a reset state.
 * Accessors:  driveShaper_apply(); the engines' init; the dty writer.
 * Affiliates: tools/plots/plot_waveshapers.py.
 */
const float driveShaper_trim[DRIVE_TYPE_COUNT] = {
    1.000000f, /* sft  +0.00 dB */  1.166908f, /* rct  +1.34 dB */
    1.259799f, /* art  +2.01 dB */  1.673173f, /* thf  +4.47 dB */
    1.369885f, /* fdc  +2.73 dB */  1.852455f, /* bsw  +5.35 dB */
    1.002718f, /* tsw  +0.02 dB */  1.002718f, /* lsw  +0.02 dB */
    0.752294f, /* lsq  -2.47 dB */  1.732458f, /* sfd  +4.77 dB */
    1.842502f, /* afd  +5.31 dB */  1.481852f, /* asf  +3.42 dB */
    1.469742f, /* chb  +3.34 dB */  1.000000f, /* rsb  +0.00 dB */
};
```

These values were measured from the script on 2026-10-25. They match the
plan's table within 0.05 dB.

### VX2-7.3 `Core/DSPAudio/ResonantFilter.h` after line 101 — add

```c
/*
 * ZDF voice filter with a drive-type input stage (S084 VX2 VX2-7.3).
 *
 * What:       SVF_calcBlockZDF() with one change: the input stage x =
 *             driveShaper_apply(driveType, state, u, s, inc) instead of the
 *             soft clip s. Stage A (the shared 1/(Dx D1)) is unchanged and
 *             still yields s and t1; Stage B and the outputs are unchanged.
 *             Two divisions per sample, plus one per block (inc). The naive
 *             2-pole path shapes softClipTwo(u) the same way.
 * Why:        plan §4.4; D7 (shared Stage A), D8 (one switched loop: 13
 *             unswitched copies would not fit ITCM). SVF_calcBlockZDF()
 *             stays the sft path, so sft is bit-identical by construction.
 * Inputs:     filter, filter type, drive type 1..13, shaper state, buf, size.
 * Outputs:    buf; s1/s2/zi and the shaper state.
 * Accessors:  the four engines when driveType != DRIVE_TYPE_SFT.
 * Affiliates: DriveShaper.h; the float twin SVF_calcBlockZDFFloat() keeps
 *             the soft clip only (Effects have no drive type; documented
 *             exception to the twin rule).
 */
void SVF_calcBlockZDFShaped(ResonantFilter* filter, const uint8_t type,
                            const uint8_t driveType, DriveShaperState* shaper,
                            int16_t* buf, const uint8_t size);
```

Add `#include "DriveShaper.h"` to `ResonantFilter.h`.

### VX2-7.4 `Core/DSPAudio/ResonantFilter.c` after `SVF_calcBlockZDF()` (ends line ~356) — add `SVF_calcBlockZDFShaped()`

- **Body:** a copy of `SVF_calcBlockZDF()` (lines 213–356) with these
  differences:
  1. `const float inc = driveShaper_blockInc(shaper);` before the loops.
  2. Naive path: `float x = driveShaper_apply(driveType, shaper, u,
     softClipTwo(u), inc);`, where `u = (buf[i]/32767) · drive`.
  3. ZDF path: after `invA`, `const float s = u*Nx*D1*invA;` and
     `const float x = driveShaper_apply(driveType, shaper, u, s, inc);`
     replace `const float x = u*Nx*D1*invA;`. `t1` is unchanged.
- **Placement:** `INITCM_EFFECT_NOINLINE` if `link_budget.py` allows (D9),
  else plain `.text`.
- **Implementation note** for the `.c` block: "Copy of
  SVF_calcBlockZDF(); keep the two in step (the golden filter harness checks
  sft only; test_shaper checks the shaped input stage)."

### VX2-7.5 Engine structs — add `driveType` and `shaper`

| File | Line | Insert after |
|---|---|---|
| `Drum/DrumVoice.h` | 92 | `uint8_t filterType;` (inside `#if ENABLE_DRUM_SVF`) |
| `Snare/Snare.h` | 61 | `uint8_t filterType;` (the fields are guarded `#if !USE_PEAK`, as the filter is) |
| `Cymbal/CymbalVoice.h` | 80 | `uint8_t filterType;` |
| `HiHat/HiHat.h` | 83 | `uint8_t filterType;` |

```c
	/*
	 * Filter drive type and its state (S084 VX2 VX2-7.5).
	 *
	 * What:       DRIVE_TYPE_* (0 = sft, today's input clip) and the shaper
	 *             state the stateful types use.
	 * Why:        plan §4.4: voice-only drive types with state in the engine
	 *             (no static RAM); inside the runtime-slot union (§0.5).
	 * Writers:    IM_SPECIAL_DRIVE_TYPE (type, reset on change);
	 *             *_initVoice() (reset).
	 * Readers:    the render function's filter call.
	 */
	uint8_t		driveType;
	DriveShaperState shaper;
```

`#include "DriveShaper.h"` comes in through `ResonantFilter.h`.

### VX2-7.6 Engine filter calls and init — modify

**Calls** (Drum 374, Snare 264, Cymbal 294, HiHat 305):

```c
	/* S084 VX2 VX2-7.6: sft keeps the untouched loop (bit-identical). */
	if (voice->driveType == DRIVE_TYPE_SFT)
		SVF_calcBlockZDF(&voice->filter,voice->filterType,buf,size);
	else
		SVF_calcBlockZDFShaped(&voice->filter, voice->filterType,
		                       voice->driveType, &voice->shaper, buf, size);
```

**Init:** in each `*_initVoice()`, after `SVF_init(&voice->filter)` (Drum
line 109; the others at their `SVF_init` call), add
`driveShaper_reset(&voice->shaper);`. `driveType` is 0 from the slot memset.

### VX2-7.7 `Core/DSP/Instruments/InstrumentManager.c` — `IM_SPECIAL_DRIVE_TYPE` writer (after `IM_SPECIAL_FILTER_DRIVE`, line 3709)

A static accessor `instrumentManager_driveFields(slot, uint8_t **type,
DriveShaperState **state)` switches on the slot type (beside
`instrumentManager_filter()`, line 2468).

```c
    /*
     * Filter drive type writer (S084 VX2 VX2-7.7).
     *
     * What:       clamps to 0..DRIVE_TYPE_COUNT-1; on a change, stores it and
     *             resets the shaper state so trackers and DC blockers start
     *             clean.
     * Why:        plan §4.4; automatable (a step lock can change the type per
     *             step), so a type change must never carry another type's
     *             state.
     * Inputs:     byteValue.  Outputs: driveType, shaper.
     * Accessors:  instrumentManager_writeRuntime() (selector pre-pass,
     *             supplemental menu/MIDI edits, step automation).
     * Affiliates: driveShaper_reset(), SVF_calcBlockZDFShaped().
     */
    case IM_SPECIAL_DRIVE_TYPE: {
        uint8_t *driveType;
        DriveShaperState *shaper;
        const uint8_t type = (byteValue < DRIVE_TYPE_COUNT)
            ? byteValue : (uint8_t)(DRIVE_TYPE_COUNT - 1u);

        if (!instrumentManager_driveFields(slot, &driveType, &shaper))
            return 0u;
        if (*driveType != type) {
            *driveType = type;
            driveShaper_reset(shaper);
        }
        return 1u; }
```

### VX2-7.8 `Core/DSP/Instruments/InstrumentManager.c` line 4010 area — classifier keys

In `instrumentManager_classifySpecialKey()` (line 3945):

```c
    /* S084 VX2 VX2-7.8: the transient and drive type selectors. */
    if (strcmp(key, "transient_type") == 0) return IM_SPECIAL_TRANSIENT_TYPE;
    if (strcmp(key, "filter_drive_type") == 0) return IM_SPECIAL_DRIVE_TYPE;
```

### VX2-7.9 `tools/plots/plot_waveshapers.py` — add `--emit-c` and `--export-ref DIR`

- **`--emit-c`:** prints the `driveShaper_trim[]` initialiser (VX2-7.2) from
  `level_trims()`.
- **`--export-ref DIR`:** for every type, writes the shaper input `u`, the
  matching `s` (`L.filter_input_stage(u)`), and the expected output (trim
  applied) for two signals, the 220 Hz sine at drv 40/80/127 and the kick
  glide at drv 110, as float32 `.bin` files plus a small JSON index. Read by
  `test_shaper.c`.
- The plotting is unchanged.

### VX2-7.10 `Makefile` `DSP_SRCS` — add

`Core/DSPAudio/DriveShaper.c \` after `Core/DSPAudio/distortion.c \`.

---

## 9. WP8 — Descriptor rows, pages and the selector helper (four instruments)

### VX2-8.1 Row macro — add (each `*Parameters.c`, after `ROW_SELECT_SPECIAL`, Drum/Snare/Cymbal line 101, HiHat 102)

```c
/*
 * Selector row without automation (S084 VX2 VX2-8.1).
 *
 * What:       as ROW_SELECT_SPECIAL but with no capability flags: not
 *             Morphable, not modulatable, not automatable. Applied by the
 *             selector pre-pass and stored as a single-ended [params] cell.
 * Why:        plan §4.3.1: the transient type is fixed per sound so
 *             automation and modulation can never cross between a buffer and
 *             a non-buffer option (P3).
 * Inputs:     key, category, labels, member, special tag.
 * Outputs:    one ParamDescriptor.
 * Accessors:  this table.
 * Affiliates: instrumentManager_isSelectorDescriptor(), check_special_tags.py
 *             (the macro name ends in _SPECIAL, so the tag is checked).
 */
#define ROW_SELECT_NOAUTO_SPECIAL(key_, cat_, long_, short_, member_, special_) \
    { key_, short_, long_, cat_, DTYPE_0B127, 0u, MOD_NONE, \
      BIND_SPECIAL(member_, TYPE_UINT8, special_) }
```

The name ends in `_SPECIAL` on purpose: `check_special_tags.py` `actual()`
reads a row's tag only for macros ending in `_SPECIAL` (line 73). Its
`expected()` map gains the two new keys (VX2-10.6).

### VX2-8.2 Descriptor rows — append (after the `osc_run_mode` row)

| File | Line | Rows |
|---|---|---|
| `Drum/DrumParameters.c` | 277 | `transient_type`, `filter_drive_type` |
| `Snare/SnareParameters.c` | 270 | the same |
| `Cymbal/CymbalParameters.c` | 281 | the same |
| `HiHat/HiHatParameters.c` | 284 | the same |

```c
    /*
     * Transient type and filter drive type (S084 VX2 VX2-8.2).
     *
     * What:       trt = TRANSIENT_TYPE_* (trs/wav; buf in P3) for the
     *             transient_wave row; dty = DRIVE_TYPE_* for the filter input
     *             stage.
     * Why:        plan §4.3.1 and §4.4. Appended, so existing indices,
     *             automation IDs, LFO tokens, MIDI Learn and NRPN IDs are
     *             unchanged. trt: not automatable or modulatable; dty:
     *             automatable, not Morphable or modulatable (P2 §2).
     * Inputs:     menu, MIDI (menu edits), Kit/Instrument files (keys
     *             transient_type, filter_drive_type; a missing key loads 0 =
     *             trs / sft, D4), step automation (dty only).
     * Outputs:    the IM_SPECIAL_TRANSIENT_TYPE / _DRIVE_TYPE writers.
     * Accessors:  InstrumentManager.c, InstrumentCellFormat.c, menu.c.
     * Affiliates: the transient and filter menu pages below.
     */
    ROW_SELECT_NOAUTO_SPECIAL("transient_type", "Transnt", "Type", "trt", transGen.type, IM_SPECIAL_TRANSIENT_TYPE),
    ROW_SELECT_SPECIAL("filter_drive_type", "Filter", "DriveTyp", "dty", driveType, IM_SPECIAL_DRIVE_TYPE),
```

### VX2-8.3 Index enums and counts — modify

| File | Line | Change |
|---|---|---|
| `Drum/DrumParameters.c` | 192 | after `DRUM_PARAM_RUN_MODE,` add `DRUM_PARAM_TRANSIENT_TYPE, DRUM_PARAM_FILTER_DRIVE_TYPE,` |
| `Drum/DrumParameters.h` | 14 | `DRUM_PARAM_DESCRIPTOR_COUNT 44u` → `46u` |
| `Snare/SnareParameters.c` | 188 | the same two (prefix `SNARE_`) |
| `Snare/SnareParameters.h` | 14 | `41u` → `43u` |
| `Cymbal/CymbalParameters.c` | 193 | the same (prefix `CYMBAL_`) |
| `Cymbal/CymbalParameters.h` | 14 | `46u` → `48u` |
| `HiHat/HiHatParameters.c` | 196 | the same (prefix `HIHAT_`) |
| `HiHat/HiHatParameters.h` | 14 | `46u` → `48u` |

The existing `_Static_assert(…_DESCRIPTOR_COUNT == …_PARAM_COUNT)` proves
each. All counts stay ≤ 64.

### VX2-8.4 Menu pages — modify

| File | Line | Page | Change |
|---|---|---|---|
| `Drum/DrumParameters.c` | 303 | transient | cell 3 (4th pot): `INSTRUMENT_MENU_EMPTY` → `DRUM_PARAM_TRANSIENT_TYPE` |
| `Drum/DrumParameters.c` | 304 | filter | cell 4 (second screen, first pot): → `DRUM_PARAM_FILTER_DRIVE_TYPE` |
| `Snare/SnareParameters.c` | 293 / 294 | the same | |
| `Cymbal/CymbalParameters.c` | 303 / 304 | the same | |
| `HiHat/HiHatParameters.c` | 308 / 309 | the same | |

The existing pots keep their parameters: `wav vol frq` and
`frq res typ drv`. Page comment line: "S084 VX2 VX2-8.4: trt on the
transient page's fourth pot; dty behind SELECT on the filter page."

### VX2-8.5 `Core/DSP/Instruments/InstrumentManager.h` line 843 / `.c` line 2404 — rename and generalise the selector helper (D12)

`instrumentManager_isOscSelectorDescriptor()` →
`instrumentManager_isSelectorDescriptor()`:

```c
uint8_t instrumentManager_isSelectorDescriptor(const ParamDescriptor *descriptor)
{
    if (!descriptor ||
        descriptor->runtime.kind != INSTRUMENT_BIND_INSTANCE_OFFSET ||
        (descriptor->flags & INSTRUMENT_PARAM_FLAG_MORPHABLE))
        return 0u;
    return (uint8_t)((descriptor->runtime.special & IM_SPECIAL_WRITER_MASK) !=
                     IM_SPECIAL_NONE);
}
```

The block (both files) replaces the P1-fix block's What line with:

```c
 * What:       1 for every single-ended row that binds a voice member
 *             through a special writer: INSTANCE_OFFSET binding, MORPHABLE
 *             clear, special tag != IM_SPECIAL_NONE. Today: typ, src, run
 *             (P1), trt, dty (S084 VX2 VX2-8.5).
```

It also appends to Why: "Recognising the class rather than a list keeps a
future selector row from silently missing the pre-pass (the P1 review F1
failure mode)."

`Core/Bank/Scene/Preset/presetManager.c` call sites: line 1141 (the
supplemental setter), 1844 (`preset_applyOscSelectorRows`), 1890 (the Kit
supplemental skip). Rename `preset_applyOscSelectorRows()` →
`preset_applySelectorRows()` and update its two callers (inside
`preset_resetAndApplyKitVoiceImage()` and the immediate Instrument apply).
The comment at line 1796 lists `typ`, `src`, `run`, `trt`, `dty`.

**Order inside the pre-pass:** descriptor order. `trt` lands before the
Morph image writes `transient_wave`, so the wave clamps against the incoming
type (the P1 F2 reason, now also for transients).

---

## 10. WP9 — Menu, `MenuText.h`, `InstrumentCellFormat`

### VX2-9.1 `Core/Menu/MenuText.h` lines 140–148 — modify and add name tables

```c
/* S084 VX2 VX2-9.1: wtb appended (stored value 4). */
static const char oscTypeNames[][4] = {
    {5}, {"wav"},{"pwm"},{"smp"},{"slp"},{"wtb"},
};
/*
 * Transient types (S084 VX2 VX2-9.1).
 * What: trs, wav; P3 appends buf. Count = TRANSIENT_TYPE_SELECTABLE.
 * Clients: the trt cell through InstrumentCellFormat (CELL_FMT_TRANSIENT_TYPE).
 */
static const char transientTypeNames[][4] = {
    {2}, {"trs"},{"wav"},
};
/*
 * Filter drive types (S084 VX2 VX2-9.1).
 * What: plan §4.4 names in DRIVE_TYPE_* order. Count = DRIVE_TYPE_COUNT.
 * Clients: the dty cell through InstrumentCellFormat (CELL_FMT_DRIVE_TYPE).
 */
static const char driveTypeNames[][4] = {
    {14}, {"sft"},{"rct"},{"art"},{"thf"},{"fdc"},{"bsw"},{"tsw"},
    {"lsw"},{"lsq"},{"sfd"},{"afd"},{"asf"},{"chb"},{"rsb"},
};
```

`transientNames` (line 68) is unchanged: entries 1–2 (`Snp Ofs`) name the
`trs` values and entries 3–14 the 12 PCM transients.

### VX2-9.2 `Core/DSP/Instruments/InstrumentCellFormat.h` lines 41–57 — modify

Append kinds:

```c
    CELL_FMT_WAVETABLE,       /* wtb source: short wNN, full = folder name */
    CELL_FMT_WTB_POSITION,    /* wtb wav: 0..127, full "frame NNN" (aux = frames) */
    CELL_FMT_TRANSIENT_TYPE,  /* index into transientTypeNames */
    CELL_FMT_TRANSIENT_SHAPE, /* trs wav: transientNames[1 + index] */
    CELL_FMT_TRANSIENT_PCM,   /* wav list 0..11: transientNames[3 + index] */
    CELL_FMT_DRIVE_TYPE       /* index into driveTypeNames */
```

Add `uint8_t aux;` to `instrument_cell_format_t`: the frame count for
`CELL_FMT_WTB_POSITION`, else 0.

The enum block's What line gains: "S084 VX2: wavetable source and position,
transient type, shape and PCM entries, drive type. Transient sample entries
reuse CELL_FMT_SAMPLE and stock-wave bursts CELL_FMT_STOCK_WAVE, so the
existing formatters name them."

### VX2-9.3 `Core/DSP/Instruments/InstrumentCellFormat.c` lines 42–103 — modify

**`IM_SPECIAL_OSC_SOURCE`:** `case OSC_TYPE_WTB: out->kind =
CELL_FMT_WAVETABLE; break;`. The max comes from
`instrumentManager_oscSourceMax()` (VX2-5.7).

**`IM_SPECIAL_OSC_WAVE`:** `case OSC_TYPE_WTB:` sets
`CELL_FMT_WTB_POSITION`, with
`out->aux = frame count of the bound table (sampleMemory_getWavetableInfo(osc->source))`.

**New cases:**

```c
    /*
     * Transient and drive selectors (S084 VX2 VX2-9.3).
     *
     * What:       trt: type name; transient_wave: under trs the Snp/Ofs
     *             name, under wav the list position resolved into a PCM
     *             name, a sample (CELL_FMT_SAMPLE, sample index) or a stock
     *             wave (CELL_FMT_STOCK_WAVE, 0..10), with max = the whole
     *             list; dty: drive type name.
     * Why:        plan §4.3.1 / §4.4: the transient wav row's meaning
     *             depends on trt, exactly as the oscillator wav row depends
     *             on typ; the DTYPE_MENU ids are exhausted, so these rows are
     *             recognised by their writer tags.
     * Inputs:     slot, descriptor, stored value.
     * Outputs:    kind, index, max.
     * Accessors:  menu.c formatted-cell sites.
     * Affiliates: transient_waveMax(), instrumentManager_transientType(),
     *             MenuText.h tables.
     */
    case IM_SPECIAL_TRANSIENT_TYPE:
        out->kind = CELL_FMT_TRANSIENT_TYPE;
        out->max = (uint8_t)(TRANSIENT_TYPE_SELECTABLE - 1u);
        out->index = (value > out->max) ? out->max : value;
        return 1u;
    case IM_SPECIAL_TRANSIENT_WAVE: {
        const uint8_t type = instrumentManager_transientType(slot);
        const uint8_t samples = sampleMemory_getNumSamples();

        out->max = transient_waveMax(type);
        value = (value > out->max) ? out->max : value;
        if (type == TRANSIENT_TYPE_TRS) {
            out->kind = CELL_FMT_TRANSIENT_SHAPE; out->index = value;
        } else if (value < TRANSIENT_STOCK_PCM_COUNT) {
            out->kind = CELL_FMT_TRANSIENT_PCM; out->index = value;
        } else if (value < TRANSIENT_STOCK_PCM_COUNT + samples) {
            out->kind = CELL_FMT_SAMPLE;
            out->index = (uint8_t)(value - TRANSIENT_STOCK_PCM_COUNT);
        } else {
            out->kind = CELL_FMT_STOCK_WAVE;
            out->index = (uint8_t)(value - TRANSIENT_STOCK_PCM_COUNT - samples);
        }
        return 1u; }
    case IM_SPECIAL_DRIVE_TYPE:
        out->kind = CELL_FMT_DRIVE_TYPE;
        out->max = (uint8_t)(DRIVE_TYPE_COUNT - 1u);
        out->index = (value > out->max) ? out->max : value;
        return 1u;
```

### VX2-9.4 `Core/Menu/menu.c` line 3298 `menu_instrumentCellText3()` — add cases

```c
    /* S084 VX2 VX2-9.4: wavetables, transient and drive selectors. */
    case CELL_FMT_WAVETABLE:
        menu_formatWavetableShortName(f.index, out);   /* w01..w99, then A0.. */
        return 1u;
    case CELL_FMT_WTB_POSITION:
        numtostrpu(out, f.index, ' ');
        return 1u;
    case CELL_FMT_TRANSIENT_TYPE:
        memcpy(out, transientTypeNames[f.index + 1u], 3);
        return 1u;
    case CELL_FMT_TRANSIENT_SHAPE:
        memcpy(out, transientNames[f.index + 1u], 3);
        return 1u;
    case CELL_FMT_TRANSIENT_PCM:
        memcpy(out, transientNames[f.index + 3u], 3);
        return 1u;
    case CELL_FMT_DRIVE_TYPE:
        memcpy(out, driveTypeNames[f.index + 1u], 3);
        return 1u;
```

`menu_formatWavetableShortName()` is new, beside
`menu_formatSampleShortName()` (line 11614): the same shape with `'w'`. Its
prototype goes beside line 2041.

### VX2-9.5 `Core/Menu/menu.c` line 3346 `menu_instrumentCellFull()` — add cases

- `CELL_FMT_WAVETABLE`: `sampleMemory_getWavetableName(f.index, name)` into
  `line[0..7]`, as `CELL_FMT_SAMPLE` does for samples.
- `CELL_FMT_WTB_POSITION`: `"frame "` followed by the 1-based frame
  `1 + (index·(aux − 1) + 63) / 127`.
- The transient and drive kinds take the default: the existing 3-character
  text at `line[13]`.

```c
/* S084 VX2 VX2-9.5: single-parameter view shows the wavetable folder name
** and the frame a position lands on. */
```

### VX2-9.6 `Core/Menu/menu.c` — count asserts (beside the `getMaxEntriesForMenu` forward declaration, line 2039)

```c
/*
 * Name tables follow their DSP counts (S084 VX2 VX2-9.6).
 *
 * What:       compile-time checks that each list's entry count (array rows
 *             minus the count row) equals its constant.
 * Why:        a list that silently lags its constant shows the wrong name
 *             or reads past the table.
 * Inputs:     MenuText.h tables, DSP constants.  Outputs: build errors.
 * Accessors:  the compiler.
 */
_Static_assert(sizeof(waveformNames) / sizeof(waveformNames[0]) - 1u ==
               OSC_STOCK_WAVE_COUNT, "waveformNames vs OSC_STOCK_WAVE_COUNT");
_Static_assert(sizeof(oscTypeNames) / sizeof(oscTypeNames[0]) - 1u ==
               OSC_TYPE_COUNT, "oscTypeNames vs OSC_TYPE_COUNT");
_Static_assert(sizeof(transientTypeNames) / sizeof(transientTypeNames[0]) - 1u ==
               TRANSIENT_TYPE_SELECTABLE, "transientTypeNames");
_Static_assert(sizeof(driveTypeNames) / sizeof(driveTypeNames[0]) - 1u ==
               DRIVE_TYPE_COUNT, "driveTypeNames vs DRIVE_TYPE_COUNT");
```

### VX2-9.7 `Core/Menu/menu.c` line 13084 `menu_automationValueMaxSlot()` — modify

```c
    /* S084 VX2 VX2-9.7: automation and MIDI are 7-bit; a formatted list
    ** longer than 128 (the transient wav list) is reachable to 127 only
    ** (plan §4.3.1). */
    if (descriptor && instrumentCellFormat_describe(slot, descriptor, 0u, &f))
        return (f.max > 127u) ? 127u : f.max;
```

`menu_clampCellValue()` (line 6544) already clamps menu edits to `f.max`, up
to 255, so the menu reaches the whole list. `menu_commitTargetEdit()`
already limits MIDI to 127.

### VX2-9.8 `Core/Menu/menu.c` lines 11594–11604 — comment only

The `MENU_WAVEFORM` block's What line: "the 11 stock waves (S084 VX2);
samples and wavetables are chosen on `src`".

---

## 11. WP10 — Tools and host tests

All new tests follow the bench rule (`DSP_TEST.md` §6): compile the real
firmware text, extracted by `extract.py`, against the host prelude. Each new
target is added to the `.PHONY` list at `tools/dsp_test/Makefile` line 29.

### VX2-10.1 `tools/dsp_test/test_natural_order.c` (new) + target `natural_order`

- **Extract:** `storage_naturalCompare`, `storage_stripOrderPrefix` from
  `Core/Hardware/SD/storageTypes.c`.
- **Cases:**
  - `f2 < f10`; `f02 = f2` value with `f2` first;
  - `A1 = a1` (case);
  - `frame9.wav < frame10.wav`; `x < x0`;
  - empty strings;
  - 48-character names;
  - the strip rule: `"000 hvoice"` → `"hvoice"`, `"12_a"` → `"a"`,
    `"123"` unchanged, `"2024 mix"` → `"mix"`;
  - an exhaustive check that the order is total and transitive on 200
    random names.

### VX2-10.2 `tools/dsp_test/test_catalogue.c` (new) + target `catalogue`

- **Build:** compiles `SampleInstall.c` and `SampleMemory.c` whole, with
  host stubs:
  - `sampleFlash_*` on a 1,310,720 B array at the sector-7 base. Erase sets
    0xFF. A word write ANDs into the array, so double programming of a
    non-erased word is caught as an error, as on the chip;
  - `SAMPLE_FLASH_PTR()` mapping onto the array;
  - `stockWaveGen_programAll()` linked for real (VX2-10.3).
- **Cases:**
  1. Mono 16-bit samples with peaks 32767 / 16383 / 1000 / 0: gain codes
     0 / 24 / 120 / 0, and `gain · peak ≤ 32767` (16384 gives 23: the
     floor keeps the product below full scale).
  2. Frames: stereo 24-bit → 16-bit mono (exact against a reference
     conversion); 8-bit unsigned → signed; 16-bit copied byte for byte.
  3. Split writes: the same table fed in random chunk sizes 1..517 gives
     identical flash.
  4. Fit: a table larger than the free space is refused before any byte is
     written, and the next item still installs.
  5. Limits: 233 samples accepted, the 234th refused; 128 tables, the 129th
     refused.
  6. Commit: without a commit the state is EMPTY; after it, V2 with the
     right counts; a corrupted entry ends the count there.
  7. A v1 image (count word 7 at the base, no v2 magic) reads STALE.
  8. `SampleInfo`/`WavetableInfo`/names read back exactly.

### VX2-10.3 `tools/dsp_test/test_stock_gen.c` (new) + target `stock_gen`

- **Prerequisite:** run `tools/gen_stock_recipes.py --emit-ref
  tools/dsp_test/frozen/stock_ref.bin` (once, and on any CSV change).
- **Test:** compiles `StockWaveGen.c` and `StockWaveRecipes.c` with the
  fake flash and runs `stockWaveGen_programAll()`.
- **Checks:**
  - every point within ±1 LSB of `stock_ref.bin`;
  - the level offsets of `StockWaves.c` (extracted) match the generator's
    layout;
  - every level peaks at 32,767 ± 1.

### VX2-10.4 `tools/dsp_test/test_wtb.c` (new) + target `wtb`

- **Extract:** `oscWavetable_render`, `oscWavetable_renderFm`,
  `oscWavetable_bind` (with a `sampleMemory_getWavetableInfo` stub returning
  host tables).
- **Tables:** frame lengths 600, 256, 64 and 2; 8- and 16-bit; 1, 2 and 104
  frames.
- **Checks:**
  - every sample against a float64 reference: the same index/fraction
    definition, the same position ramp; error ≤ 1 LSB;
  - position 0 and 127 reproduce the first and last frames exactly;
  - the index never leaves the frame, including at
    `phase = 0xFFFFFFFF`;
  - a silent table (no install) advances the phase exactly as a real table;
  - FM with a zero modulator equals the plain render;
  - a mid-block position change ramps linearly (no step).

### VX2-10.5 `tools/dsp_test/test_transient.c` (new) + target `transient`

- **Extract:** `transient_init`, `transient_trigger`,
  `transient_setType`, `transient_setWave`, `transient_waveMax`,
  `transient_resolve`, `transient_calcBlock`. Stubs: SampleMemory (2 host
  samples with gain codes), `stockWave_level`, the PCM table.
- **Checks:**
  - **PCM:** each of the 12 PCM entries is bit-identical to the frozen pre-P2
    loop (`frozen/Core/DSPAudio/transientGenerator.c`, snapshot taken at
    WP0);
  - **trs:** 0/1 resolve to SNAP/OFFSET and render silence;
  - **samples:** play one-shot to their end and then hold 0; the gain code
    is applied;
  - **bursts:** stop after exactly 2,205 samples; the level at sample 2,204
    is −43 dB ± 1 dB of the start (τ = 10 ms);
  - **round trip:** `wav` → `trs` → `wav` restores the chosen entry
    (request byte);
  - **reinstall:** a generation change re-resolves a sample entry.

### VX2-10.6 `tools/dsp_test/check_special_tags.py` lines 51–60 — modify

Add `'transient_type': 'TRANSIENT_TYPE', 'filter_drive_type':
'DRIVE_TYPE'` to `simple`. The two new keys start with neither `osc1_` nor
`osc_`, so the oscillator branch (lines 25–50) does not see them. Expected
result: 185 rows, 0 mismatches (177 + 2 × 4).

### VX2-10.7 `tools/dsp_test/test_shaper.c` (new) + target `shaper`

- **Prerequisite:** run `tools/plots/plot_waveshapers.py --export-ref
  tools/dsp_test/frozen/shaper` (VX2-7.9).
- **Extract:** `driveShaper_apply` (inline: the header is included),
  `driveShaper_reset`, `driveShaper_blockInc`, `driveShaper_trim`. The test
  feeds `(u, s)` per sample in 32-sample blocks, calling `blockInc` per
  block as the filter does.
- **Tolerances:**
  - memoryless types: |error| ≤ 2·10⁻³ (the sine-fold polynomial; thf/asf
    exact to 10⁻⁵);
  - stateful types: identical crossing sample indices, and outputs within
    10⁻⁴ (float32 against float64).
- Fails with the first differing sample per type.

### VX2-10.8 `tools/dsp_test/test_osc_pwm.c` — extend

- Stub `sampleMemory_stockValid()` and `stockWave_level()` with a host `sq3`
  level.
- **Fallback** (stock invalid): `pq3`/`pw3` output equals the P1 output
  (`s0` = 0.5).
- **Natural split:** with stock valid, `c = 0` equals the plain `sq3`/`sw3`
  read within 1 LSB.
- The prc/psw/ptr/swp checks are unchanged.

### VX2-10.9 `tools/dsp_test/test_osc_sample.c` — update

- The local `SampleInfo` typedef becomes `{size, offset, gain_code}`
  (VX2-1.2) and `struct OscStruct` gains `sampleGainCode`.
- New case: gain code 24 doubles the output (2·x within 1 LSB, saturating).

### VX2-10.10 `tools/dsp_test/armcheck/vx2.c` (new) + target `armcheck-vx2`

- **Unit:** one ARM translation unit with `calcStockWaveBlock`,
  `oscWavetable_render(_Fm)`, `transient_calcBlock`, `SVF_calcBlockZDF` and
  `SVF_calcBlockZDFShaped`.
- **Run:** `fpseq.py --report --all` prints per-loop instruction totals.
- **Gates** (§0.7):
  - stock reader ≤ the 1,024-point table loop + 2;
  - `wtb` ≤ 47 (FM ≤ 62);
  - every transient loop ≤ 22 (D6);
  - the shaped ZDF reported per drive type against `sft`, target ≤ `sft` +
    6.
- **Scope:** reports, not S0 comparisons (new DSP, `DSP_TEST.md` §6.3).
- **Over a target:** a type above `sft` + 6 is reported in the log and
  simplified before landing.

### VX2-10.11 `tools/dsp_test/test_filter` — run unchanged

`SVF_calcBlockZDF()` is not edited, so `filter` S1 must report exactly the
`S084_P1_IMPLEMENTATION.md` §16/§18 figures. Any change means the `sft` path was touched.

### VX2-10.12 `tools/sampleoptimize.py` — no change

It writes mono 16-bit samples and 16-bit frames that the v2 installer
accepts. The new 8-bit and 24-bit/stereo acceptance only widens what a raw
card can install.

---

## 12. WP11 — Specification updates (same change set)

| File | Section | Change |
|---|---|---|
| `FILESYSTEM_SPEC.md` | lines 136–137, "Root Layout" (365–385) | `Sample/`, `Wavetable/` → `samples/`, `wavetables/` (D2) |
| `FILESYSTEM_SPEC.md` | "Wavetable" (2290–2315) | rewrite: any folder under `wavetables/` with WAV frames is one table; natural order (`storage_naturalCompare`); numeric prefix stripped from the display name; files first, then subfolders, depth 3; frame forms (8/16/24-bit, mono/stereo, D11); one length and depth per table, mismatches skipped; ≤ 128 frames; a table that does not fit is skipped whole; short name `wNN` |
| `FILESYSTEM_SPEC.md` | `Load:[Samples]` (2430–2440) | one install of `samples/` + `wavetables/`; `/loops` retired; progress stages and summary; stock waves generated at install |
| `FILESYSTEM_SPEC.md` | new "Sample flash catalogue v2" | the §0.5 map, header and entry layouts, magic-last commit, STALE detection, D10 gain code |
| `STORAGE_SRAM_MANIFEST.md` | line 377 (sample flash map), row at 597 (`SampleMemory.c` caches), the `filesystem.c` sample-manifest row | v2 map; caches removed; manifest removed; the batch, last keys and `SampleInstall` state added; runtime-slot sizes after P2; net −16,142 B |
| `INSTRUMENTS_DSP_REFERENCE.md` | new "Session 084 VX2 — sample flash and voice expansion 2" after the P1 section (line ~806) | stock waves and levels; `wtb`; transient types and kinds; drive types (functions, trims, state, costs); measured loop costs from VX2-10.10 |
| `EFFECTS_MIXER_DSP_REFERENCE.md` | ZDF float twin paragraph | documented exception: the Effect twin keeps the soft clip only |
| `CPU_USE_DSP_AUDIT.md` | worst-case voice | Drum worst case with `wtb` FM, the worst drive type, sample transients |
| `MODULE_INTERCHANGE_SPEC.md` | SampleRom, filesystem install, InstrumentManager, ResonantFilter rows | the new APIs (VX2-1.2, VX2-2.1, VX2-3.3, VX2-6.3, VX2-7.3, VX2-8.5) |
| `BANK_PRESET_ARCHITECTURE.md` | instrument rows | `trt`/`dty` keys; the selector class (`instrumentManager_isSelectorDescriptor()`) and the pre-pass order |
| `MIDI_SPEC.md` | after "Session 084 P1 - appended instrument targets" (line 1558) | "Session 084 VX2 — appended instrument targets": `transient_type`, `filter_drive_type` by append; the transient wav list beyond 127 is menu-only; `oscN_wave` under `wtb` is 0..127 |
| `tools/dsp_test/DSP_TEST.md` | §5 targets, §7 costs | the new targets; the measured VX2 loop costs |
| `DEV_MODES.md` | special-tag self-check | 185 rows |

---

## 13. Verification and items to confirm during implementation

### 13.1 Confirm while coding

1. `sizeof` of the four voices, `OscInfo` (120), `OscWtbState` ≤
   `OscPwmState`, and `TransientGenerator` (40): the compile probe.
2. ITCM after each WP (`link_budget.py`); D9 fallback if the shaped loop
   does not fit.
3. **asyncfatfs handles at depth 3:** `samples/` + three subfolders + one
   file = 5 = `AFATFS_MAX_OPEN_FILES`. Confirm no other handle is open
   during the modal install (the facade is idle by contract).
4. **Stack use of the recursive walk:** < 300 B per level. The batch and
   the last keys are static.
5. `afatfs_findNextObject()` returns `.`/`..` entries for subfolders: skip
   them (VX2-3.7 rule "does not start with '.'").
6. **The STALE heuristic** on a freshly erased chip (0xFFFFFFFF → EMPTY)
   and after a failed v2 install (stock block at the base: a random word,
   which may read STALE; acceptable, since the advice is the same).
7. The menu's 255-value edit path for the transient wav cell (encoder and
   pots both reach the end of a 256-entry list).
8. Every `fpseq` gate in VX2-10.10, with the numbers logged.

### 13.2 Host

`make -C tools/dsp_test` with every target:

- the P1 targets unchanged (`filter` S1, `pack`, `postchain`, `mixer`,
  `octave`, `lfo_oneshot`, `osc_run`, `swap_level`);
- `special_tags` 185/0;
- the new `natural_order`, `catalogue`, `stock_gen`, `wtb`, `transient`,
  `shaper`;
- `osc_pwm` and `osc_sample` extended;
- `armcheck-vx2` reported.

### 13.3 Hardware

| ID | Check |
|---|---|
| X1 | Flash the firmware over a v1 install: the boot notice shows; `smp`/`slp` are silent; the new waves play their fallbacks. Load:[Samples] with `samples/` (with a subfolder), `wavetables/hvoice` raw (124,800 B) and optimised (53,248 B) as two tables, one stereo 24-bit table, and one table too large to fit: the progress stages, then `Smp NNN Wtb NN` / `Skip NN`. Reboot: everything still there. |
| X2 | `wav` list: all 11 waves across the keyboard; smooth octave changes; waveform interpolation across `Cym`→`sq3`. |
| X3 | `wtb`: sweep `wav` 0..127 by knob and by LFO; FM; `snc` with a `wtb` master and slave; `itp` retriggers; source names in the single-parameter view; frame display. |
| X4 | `pq3`/`pw3`: `c = 0` sounds like `sq3`/`sw3`; sweeps to the narrow end; compare with the fallback (before install). |
| X5 | Transients: `trs` Snp/Ofs unchanged; `wav` PCM entries unchanged; sample entries by name at matched levels; the bursts' pitch on `frq` and their decay; a list past 128 reachable in the menu; automation and MIDI stop at 127. |
| X6 | Drive types by ear at drv 40/80/127; trackers on a kick glide; `dty` step automation (no clicks on change: state reset); `sft` A/B against P1 identical. |
| X7 | Worst-case Scene CPU: six voices with `wtb` FM, the worst drive type and sample transients, against the WP0 baseline; no underruns. |
| X8 | Kit/Instrument save and reload with `wtb`, `trt` and `dty` set; an old kit loads (its PCM transient shows `Ofs`, D4). |

---

## 14. Open items

1. **D13 wording:** confirm the boot and summary texts.
2. **D5:** confirm the burst base (440 Hz × pitch) and τ = 10 ms by ear at
   X5.
3. **D6:** if a transient loop loses its interpolation, decide by ear
   whether the aliasing is acceptable or the cap should move.
4. **Sample display names:** the numeric-prefix strip applies to wavetable
   folders. Decide whether sample files should get it too (`01_kick.wav` →
   `kick`).
5. **Index references:** samples, wavetables and the stock bursts' list
   positions move when the card's set changes (P2 §5 risk). Name-based
   references remain a follow-up.
6. **From P1, unchanged here:**
   - the LFO adapter's `base_value` clamp on a type change is lossy (the
     same pattern P1 review F2 fixed for the writers;
     `instrumentManager_refreshWaveDomains()`, `InstrumentManager.c`
     line ~858);
   - the holding-`smp`-master `snc` behaviour (P1 §18.4).

---

## 15. Implementation log

### 15.1 WP0 — baselines (2026-10-26, working tree, DEV config)

| Measure | Value |
|---|---:|
| `text` / `data` / `bss` | 625,144 / 428 / 430,408 |
| Flash payload / headroom | 625,572 / 128,092 B |
| ITCM / DTCM statics / FXBUF | 10,856 / 4,468 / 126,592 B |
| `sizeof` OscInfo / OscPwmState / TransientGenerator / ResonantFilter | 120 / 28 / 24 / 36 B |
| `sizeof` Drum / Snare / Cymbal / HiHat voice | 1,072 / 904 / 1,008 / 1,016 B |

The figures match `S084_P1_IMPLEMENTATION.md` §18.2.

### 15.2 WP1–WP3 — catalogue v2, installer, walk (code landed)

- `SampleMemory.h/.c` rewritten to catalogue v2 with no RAM caches; the v1
  install/loop/append APIs are gone. The five RAM caches and the 13,441 B
  manifest are released.
- `SampleInstall.h/.c` added: format acceptance, sample and frame writers,
  peak gain code, stock block call, magic-last commit. The state is **44 B**
  (the schedule estimated 32 B): the extra 12 B retain the item name and the
  wavetable frame length/count so an entry is programmed only at finish.
- `filesystem.c`: the v1 LFN reassembly, manifest, scan and one-sample path
  are replaced by batched natural-order selection (`fs_install_batch[32]`,
  `fs_install_last[4]`), the recursive sample tree and the wavetable folder
  walk. `loops` and `filesystem_installLoopsBlocking()` are removed.
- `storageTypes.c/h`: `storage_naturalCompare()` and
  `storage_stripOrderPrefix()`.
- `menu.c`: stage progress and the `Smp NNN Wtb NN` / `Skip NNN` summary
  with a 1.5 s deferred repaint (`menu_installSummaryUntil`, 2 B) serviced
  from `menu_pollPresetStatus()` so audio is not starved.
- `main.c`: the STALE-catalogue boot notice.
- **Measured after WP1–WP3:** `text=639,488`, `data=428`,
  `bss=414,264` (net **−16,144 B**, the schedule estimated −16,142 B),
  flash payload 639,916 B (headroom 113,748), ITCM 10,440, DTCM 4,468.

### 15.3 WP4/WP5 — stock waves and `wtb` (code landed)

- `tools/gen_stock_recipes.py` generated `Core/SampleRom/StockWaveRecipes.c`
  (A/B int16 + 11 level scales) and `tools/dsp_test/frozen/stock_ref.bin`
  (61,120 B).
- **Deviation:** the firmware `sine_table` stores a negative cosine
  (`sine_table[0] = -32767`), not a sine. The generator therefore uses
  `cos = -sine_table[t]`, `sin = sine_table[(t + 1024) & 4095]`; the
  schedule's literal `cos = sine_table[(t + 1024)]` would have rotated the
  wave by 90 degrees. The float64 check now reports −55…−64 dB worst level
  (the 16-bit sine table and float accumulation), which is the expected floor
  for a 16-bit output.
- `StockWaves.h/.c`: level offsets/shifts and the Rec/Saw/Tri fallback;
  `StockWaveGen.h/.c`: additive generation straight into flash.
- `Oscillator.c`: `calcStockWaveBlock()` / `calcStockWaveFmBlock()`,
  dispatch (block and scalar), table frequency, and `osc_isTableWave()` in
  the four engine triggers.
- `OscillatorPwm.c`: `pq3`/`pw3` now read the sq3/sw3 levels with the
  natural splits 0.78/0.42 (0.5 fallback); `mix` carries s0.
- `OscillatorWavetable.h/.c`: `wtb` type 4, the 32x32 multiply index,
  two-frame reads and the ramped frame position; `OscInfo.pwm` is an
  anonymous union with `wtb` (OscInfo stays 120 B).
- `MenuText.h`: 11 `wav` names and `wtb`; `InstrumentCellFormat` and
  `menu.c` gained the wavetable source/position cells.
- `InstrumentManager`: `src` maximum for `wtb`, bind on type/source change,
  and the engine trigger phase offset.

### 15.4 WP6/WP7 — transient types and drive types (code landed)

- `transientGenerator.h/.c` rewritten: type row `trs/wav`, kinds
  SNAP/OFFSET/PCM/SAMPLE/TABLE/SINE/NOISE/CRASH, Q20.12 sample transients with
  the catalogue gain, and 50 ms stock-wave bursts (τ = 10 ms, 440 Hz x pitch).
  The PCM loop is unchanged.
- `DriveShaper.h/.c`: 14 shapers, the script's trims, crossings, DC
  blocker and trackers; `SVF_calcBlockZDFShaped()` in ITCM (sft keeps the
  untouched `SVF_calcBlockZDF()`).
- Engine structs gained `driveType` + `DriveShaperState`; the filter calls
  are conditional and init resets the shaper.
- `IM_SPECIAL_TRANSIENT_TYPE` / `_DRIVE_TYPE` writers, the public
  `instrumentManager_transientType()`, the classifier keys and the
  `isSelectorDescriptor()` generalisation.
- Descriptor rows `transient_type` / `filter_drive_type` appended to all
  four tables, counts 44→46 / 41→43 / 46→48 / 46→48, and the transient/filter
  page cells.
- **Measured after WP4–WP7:** `text=650,696`, `data=428`,
  `bss=414,264`, flash payload 651,124 B (headroom 102,540), ITCM 12,680,
  DTCM 4,468. The shaped ZDF loop is 3,452 B of ITCM (D9 fits).

### 15.5 WP8/WP9 — descriptors, pages, menu and formatting (code landed)

- `ROW_SELECT_NOAUTO_SPECIAL` added to all four tables; `transient_type`
  (not automatable) and `filter_drive_type` (automatable) appended;
  counts 44→46 (Drum), 41→43 (Snare), 46→48 (Cymbal/HiHat).
- The transient page gained `trt` on its fourth pot and the filter page
  `dty` on the second screen's first pot.
- `instrumentManager_isOscSelectorDescriptor()` became
  `instrumentManager_isSelectorDescriptor()` (INSTANCE_OFFSET, MORPHABLE
  clear, any special writer); `preset_applyOscSelectorRows()` became
  `preset_applySelectorRows()`. The pre-pass order is descriptor order.
- `InstrumentCellFormat` gained wavetable source/position, transient
  type/shape/PCM and drive kinds plus the `aux` frame count; `menu.c`
  renders them, formats `frame NNN`, clamps automation to 127 and has the
  four name-count asserts. `MenuText.h` has 11 `wav` names, `wtb`,
  `transientTypeNames` and `driveTypeNames`.
- `check_special_tags.py` maps the two new keys.

### 15.6 WP10 — host tests (all pass)

| Target | Result |
|---|---|
| `natural_order` | OK, 0 failures |
| `catalogue` | OK, 0 failures (gain codes 0/24/120/0/23, conversions, split writes, fit/count limits, magic-last, STALE, corruption) |
| `stock_gen` | OK, 0 failures (every point ±1; every level peaks at full scale) |
| `wtb` | OK, 0 failures (600/256/64/2 points, 8/16-bit, 1/2/104 frames) |
| `transient` | OK, 0 failures (PCM, trs, samples, bursts, round trip, reinstall) |
| `shaper` | OK, 0 failures (all 14 types, four signals) |
| `osc_pwm` | OK (pq3/pw3 fallback and natural splits) |
| `osc_sample` | OK (catalogue gain case) |
| `special_tags` | OK, 185 rows, 0 mismatches |
| existing suite | `selftest`, `lfo_oneshot`, `osc_run`, `swap_level`, `pack`, `postchain`, `octave`, `mixer` pass; `filter` S1 PASS (0 failures) |

`armcheck-vx2` reports 29/31 (stock), 64/70 (wtb), 29 (transient PCM),
37→39 (shaped ZDF).

### 15.7 WP11 — specifications

Updated `FILESYSTEM_SPEC.md` (root names, Sample/Wavetable sections, the
catalogue v2 map and the Load:[Samples] modal), `STORAGE_SRAM_MANIFEST.md`
(sample-flash map and the RAM ledger), `INSTRUMENTS_DSP_REFERENCE.md`,
`CPU_USE_DSP_AUDIT.md`, `EFFECTS_MIXER_DSP_REFERENCE.md`,
`MODULE_INTERCHANGE_SPEC.md`, `BANK_PRESET_ARCHITECTURE.md`,
`MIDI_SPEC.md` and `tools/dsp_test/DSP_TEST.md`.

### 15.8 Deviations recorded

1. **`SampleInstall` state is 44 B**, not 32 B: the item name and wavetable
   frame geometry are retained so an entry is programmed only at finish. The
   filesystem selection is 2,313 B (batch, last keys, pointers, failure flag),
   not 2,305 B. Net RAM change is still **−16,144 B**.
2. **Stock-wave sine mapping:** the firmware `sine_table` is a negative
   cosine, so the generator uses `cos = -sine_table[t]` and
   `sin = sine_table[t+1024]`; the schedule's literal formula would rotate
   the wave.
3. **`wtb` costs 64 (FM 70) instructions per sample**, above the schedule's
   47/62 target; the shaped ZDF is +2 (within +6).
4. **`menu_installSummaryUntil` is 2 B of new SRAM1**, holding the summary
   for 1.5 s without starving audio; serviced from `menu_pollPresetStatus()`.
5. The recipe self-check reports −55…−64 dB against the float64 ideal (the
   16-bit sine table and float32 accumulation), not the plan's −70 dB; the
   device/reference agreement is ±1 LSB.

### 15.9 Final build (DEV config)

| Measure | Value |
|---|---:|
| `text` / `data` / `bss` | 652,944 / 428 / 414,264 |
| Flash payload / headroom | 653,372 / 100,292 B |
| ITCM / DTCM statics / FXBUF | 12,688 / 4,468 / 126,592 B |
| Runtime slots Drum/Snare/Cymbal/HiHat | 1,116 / 948 / 1,052 / 1,060 B |

---

## 16. Assessment of the implementation (review, 2026-10-26)

**Reviewer scope:** the working tree against this schedule.

- **Code read:** `SampleMemory.c`, `SampleInstall.c`, `StockWaves.c`, the
  stock reader in `Oscillator.c`, `OscillatorWavetable.c`,
  `OscillatorPwm.c` (pq3/pw3), `transientGenerator.c`, `DriveShaper.h`,
  `SVF_calcBlockZDFShaped()` (diffed against the untouched
  `SVF_calcBlockZDF()`), the new `filesystem.c` installer, `storageTypes.c`
  natural order, the InstrumentManager writers, the four engines,
  `InstrumentCellFormat.c`, and the menu/boot hooks.
- **Re-measured:** the loop costs, independently of the `armcheck-vx2`
  report (§16.1).
- **Verified:** the asyncfatfs open semantics.

No code was changed by the review.

**Verdict:**

- The DSP and catalogue code is solid and matches the schedule.
- Every host test passes and the build sizes match §15.9.
- **Two installer defects block hardware check X1:** long-named subfolders
  are never installed (F1), and four-deep wavetable nesting hangs the
  device (F2).
- **The drive types miss their cost target by a wide margin** (F3). The
  logged cost figures are wrong for drive types and transients, so gates G4
  and G5 are not actually met.

### 16.1 Verification repeated

| Check | Result |
|---|---|
| Forced rebuild of every P2 file | builds; one **new** warning: `filesystem_le16` unused (N1); the rest are pre-existing |
| Sizes | `text` 652,944 / `data` 428 / `bss` 414,264; ITCM 12,688 (3,696 B free); headroom 100,292 — **match §15.9**. RAM net −16,144 B vs P1 — **confirmed** |
| Host suite | all 18 targets pass, including the 6 new ones, `special_tags` 185/0 and `filter` S1 unchanged — **matches §15.6** |
| `sft` path untouched | `git diff ResonantFilter.c` is insertions only; the shaped loop differs from the original only at Stage A's output and the naive input — **confirmed** |
| `sine_table` convention | `sine_table[0] = −32767`, `[1024] ≈ 0`, `[2048] = 32766`: a negative cosine. **The implementer's deviation 2 is correct; VX2-4.3's formula in this schedule was wrong.** |

**Loop costs, re-measured.** The `armcheck-vx2` report picks the wrong
loops in three places. Each loop below was identified by its loads; drive
types were measured by force-inlining the shaped loop with a constant filter
type (LP) and a constant drive type.

| Loop | Logged | Measured | Target | Status |
|---|---|---:|---|---|
| Stock reader, plain | 29 | **24** (`calcStockWaveBlock` loop 1; loop 0 contains a `bl` and is not the per-sample loop) | ≤ 1,024-point table + 2 (24 + 2) | pass |
| Stock reader, FM | 31 | 31 | ≤ 35 + 2 | pass |
| `wtb` / FM | 64 / 70 | 64 / 70 | 47 / 62 | over (F6) |
| Transient PCM | "29" | **18** (loop with `ldrsb`) | ≤ 22 | pass |
| Transient SAMPLE | not reported | **29** (the loop the log labelled "PCM") | ≤ 22 | **over (F5)** |
| Transient bursts | not reported | not measurable: a per-sample `switch (kind)` inside the loop | ≤ 22 | **open (F5)** |
| ZDF `sft`, LP | — | 100 (3 divisions with the LP output) | reference | — |
| ZDF shaped, LP | "37→39" (that is the **naive 2-pole loop**, not the ZDF loop) | rct 109 · art 151 · thf 109 · fdc 155 · bsw 117 · tsw 148 · lsw 147 · lsq 143 · sfd 118 · afd 121 · asf 109 · chb 107 · rsb 143; 3 divisions each | ≤ `sft` + 6 | **every type over (F3)** |

These are static loop totals: they include rarely taken branch bodies, so
the executed worst path is somewhat lower. No type adds a division.

### 16.2 Findings (most severe first)

#### F1 — HIGH: subfolders with long names are never installed (samples and wavetables)

- **Where:** `filesystem.c` `filesystem_installSampleTree()` (~line 24808)
  and `filesystem_installWavetableTree()` (~line 24985):
  `sub = filesystem_blockOpenDirLfn(chosen.short_name);`.
- **Cause:** `afatfs_opendir_lfn()` matches on the display name. When an
  entry has a valid long name it compares **only the long name**; the 8.3
  alias is compared only for entries without one (`asyncfatfs.c`
  ~4381–4400). Passing the alias (`HVOICE~1`) therefore misses every folder
  that has a long name.
- **Effect:**
  - Any folder whose name is longer than 8 characters, contains a space, or
    mixes case is silently skipped (`sub == NULL`, the card is OK, so
    `continue`).
  - That includes D2's own `000 hvoice` convention, `hvoice_opt`, and most
    real wavetable names.
  - Nothing is counted in `skipped_tables`, so the summary hides it.
  - Only single-case 8.3 folder names install.
  - The top-level `samples`/`wavetables` opens use display names and work.
- **Why the tests miss it:** `test_catalogue` drives `SampleInstall`
  directly; no host test runs the FAT walk.
- **Fix:**
  - open by the display name, `filesystem_blockOpenDirLfn(chosen.key)`.
    FAT forbids two entries equal case-insensitively, so the
    case-insensitive match is unique;
  - or by the alias through the 8.3 path, `filesystem_blockOpen(
    chosen.short_name)`, as v1 opened `samples`.
  - Count an open failure in `skipped_tables` / `skipped_files`.
  - X1 must include a long-named folder at each level.

#### F2 — HIGH: four-deep wavetable nesting hangs the modal install

- **Where:** `filesystem_installWavetableTree()`, `if (!fs_install_failed &&
  depth < FS_INSTALL_MAX_DEPTH) filesystem_installWavetableTree(sub,
  depth + 1)`.
- **Handle count:**
  - The tree at depth 3 still opens its subfolders, so the deepest chain is
    `wavetables` + 4 folders + 1 frame file = **6 handles**, but
    `AFATFS_MAX_OPEN_FILES` is 5.
  - The sample tree stops one level earlier and peaks at exactly 5 (VX2-3.7
    budget).
- **Why it hangs:** with no free handle, `afatfs_fopen()` calls back with
  NULL and returns false. `filesystem_blockOpen()` retries
  `while (!afatfs_fopen(…))` for as long as the card is OK, which is
  forever. The device freezes with audio suspended, and sample flash is
  already erased (EMPTY after a power cycle).
- **Fix:**
  1. Recurse only while `depth + 1 < FS_INSTALL_MAX_DEPTH`, so both trees
     peak at 5 handles.
  2. Make `filesystem_blockOpen`, `_blockOpenLfn` and `_blockOpenDirLfn`
     return NULL when the callback has already reported NULL
     (`block_file_ready && !block_file`) instead of retrying. This latent
     loop also threatens every other blocking open.

#### F3 — HIGH (CPU budget): drive types far above `sft` + 6; the logged figures are wrong

- **Measured:** +7 (`chb`) to +21 (`afd`) for the memoryless types, and
  **+43 to +55 for the stateful types** (`art fdc tsw lsw lsq rsb`), against
  the plan's `sft` + 6 (§16.1).
- **What the log measured:** the "37 → 39" in §15.6 and in
  `CPU_USE_DSP_AUDIT.md` item 36 is the naive 2-pole loop. The shaped ZDF
  loop was never measured.
- **Effect:** the constant-CPU worst case per voice rises by up to about
  55 instructions per sample in the filter (+55 %). For six voices that is
  roughly 14–15 M instructions/s, a few percent of the core. It must be
  carried in the worst-case Scene budget (X7).
- **Fix options** (any combination), then re-measure with the
  specialised-wrapper method in §16.1:
  - run the amplitude latch (`since`, `curMax`, `env`) only for
    `tsw`/`lsw`/`lsq`; `art`, `fdc` and `rsb` need only the crossings;
  - compute `|s|` once;
  - make the crossing and toggle updates branch-free;
  - accept a documented higher target for the stateful types.
- **Specs and log:** whatever is decided, correct the figures in §15.6,
  §15.8 item 3, `CPU_USE_DSP_AUDIT.md`, `INSTRUMENTS_DSP_REFERENCE.md` and
  `DSP_TEST.md`.
- **`armcheck-vx2`:** should gain the 13 per-type wrappers so the gate is
  repeatable.

#### F4 — MEDIUM: wavetable frames in a different source format are decoded with the first file's format

- **Where:** `filesystem_installWavetable()` passes A and B accept a later
  frame when its **stored** bit depth and frame length match the first.
  `sampleInstall_beginWavetable()` stores the first file's **source** format
  (channels, bits, block size) once, and `writeFrameData()` decodes every
  frame with it.
- **Effect:** a mono 16-bit first frame followed by a stereo 16-bit frame, or
  16-bit followed by 24-bit, passes acceptance and is decoded with the wrong
  block size. That frame turns to noise, and every frame after it shifts.
- **Root cause:** this schedule's VX2-3.8 acceptance rule; the
  implementation followed it.
- **Fix:**
  - either require equal `channels` and `bits` as well (a stricter table
    rule, reported as skipped);
  - or add `sampleInstall_setFrameFormat(fmt)`, called per file before its
    copy, so mixed source forms convert correctly (closer to W1).
  - Add a mixed-format case to `test_catalogue`.

#### F5 — MEDIUM: transient cost gate G4 not met

- **SAMPLE loop:** 29 instructions against the 22 cap. D6 says drop the
  interpolation (one read, as the PCM path does), or decide by ear at X5 to
  keep it and raise the cap.
- **Burst loop:** a `switch (transient->kind)` **inside** the per-sample
  loop. The kind is block-constant, so this costs a dispatch every sample
  and defeats measurement. Split it into one loop per kind
  (SINE / TABLE / NOISE / CRASH), as the PCM and SAMPLE cases already are;
  then measure each.
- **Log:** §15.6 calls the 29-instruction loop "transient PCM"; the PCM loop
  is 18.

#### F6 — LOW/MEDIUM: `wtb` costs 64 / FM 70 against 47 / 62

- **Status:** recorded honestly as deviation 3 and in the CPU audit.
- **Assessment:** acceptable if X7 shows headroom. It is +40 over a `wav`
  table oscillator (24), per `wtb` oscillator.
- **Possible saving:** the per-sample `fi · stride` / `fn · stride`
  multiplies can become two running frame pointers, recomputed only when
  `fi` changes. That is still constant-cost, because the compare runs
  every sample.

#### F7 — LOW: one unreadable file aborts the whole install

- **Behaviour:** any copy failure sets `fs_install_failed`, so there is no
  commit, and the flash is left erased (EMPTY).
- **Typical cause:** a truncated or streamed WAV whose `data` size exceeds
  the file (the header check passes; the read fails mid-copy).
- **Compared with v1:** v1 behaved the same, but P2 installs far more files.
- **Fix:** in `filesystem_parseWav()`, reject `data_offset + data_bytes >
  file size`; add `sampleInstall_abortItem()` (closes the open item; its
  bytes stay unused) so a read failure skips one file instead of the
  install.

#### F8 — LOW: the finish calls do not check that the item is complete

`sampleInstall_finishSample()` / `_finishWavetable()` program the entry for
the declared length without checking `item_written == item_bytes`. Today
every short write aborts the install first (F7), so this is latent; with an
item-level abort it becomes necessary.

#### F9 — LOW: after a reinstall, stock-burst transients keep a stale resolution

- **Where:** `transient_calcBlock()` re-resolves only `kind == SAMPLE` on a
  generation change.
- **Effect:** the stock waves follow the samples in the list, so when the
  sample count changes, a voice holding a burst entry plays the old wave
  while the cell shows the new mapping.
- **Fix:** re-resolve on any generation change when `type == wav`.

#### F10 — LOW: `pq3`/`pw3` keep the fallback split across an install

- **Where:** `s0` (0.78 / 0.42 versus 0.5) is set in `oscPwm_prepare()`,
  which reruns only on a frequency-cache miss, while `pwm_familyLevel()`
  switches tables every block.
- **Effect:** after Load:[Samples], a held note warps the new table around
  the old split until its pitch changes.
- **Fix:** invalidate the frequency cache on a catalogue generation change
  (one compare per block).

#### F11 — LOW: union hygiene on a pwm → wtb switch

`wtb` shares bytes with `OscPwmState`. The bind's mismatch test catches the
stale bytes in practice (`generation` overlaps the `warpA` float), but the
type writer should clear `osc->wtb.base` when it switches to `wtb`, so a
stale non-zero base can never be used.

#### Notes

- **N1:** `filesystem_le16()` is now unused (new `-Wunused-function`
  warning); remove it.
- **N2:** bursts start at table phase 0. The sine burst therefore starts at
  −full scale (`sine_table` is a negative cosine), and Rec/Saw bursts start
  at their edge: a hard click on every trigger. Decide by ear at X5, or
  start sine bursts at `phase = 1024 << 20` (zero crossing).
- **N3:** §15 has a duplicated `## 15. Implementation log` header (the
  placeholder was left above it) and stray `+` diff markers before it and
  before §15.5.
- **N4:** frames beyond the 128th valid one in a folder are ignored but not
  counted as skipped.
- **N5:** `SampleInstall` state 44 B (deviation 1) and
  `menu_installSummaryUntil` 2 B (deviation 4): accepted. Net RAM is still
  −16,144 B.

### 16.3 The implementer's §15.8 deviations

| Deviation | Assessment |
|---|---|
| 1. `SampleInstall` state 44 B | **Accepted** |
| 2. Stock-wave sine mapping | **Confirmed correct**; this schedule's VX2-4.3 formula was wrong (corrected by this note) |
| 3. `wtb` 64/70; shaped ZDF +2 | **`wtb` confirmed; shaped ZDF wrong**: +7…+55 per type (F3) |
| 4. `menu_installSummaryUntil` 2 B | **Accepted** |
| 5. Recipe floor −55…−64 dB | **Accepted** (the 16-bit table and float32 floor); the device matches the reference ±1 LSB |

### 16.4 Reviewed and found correct

- **Catalogue v2:** layout, validation, STALE detection, magic-last commit,
  and the gain code (floor; 16384 → 23 is correct).
- **Frame conversions:** 8-bit flip, stereo average, 24-bit rounding, and
  carries across any split.
- **Natural order and the prefix strip.**
- **Stock reader:** level offsets and shifts, level 0 brightest as the
  octave index expects, the fallback, and the engine `osc_isTableWave()`
  sites.
- **`wtb`:** index never leaves a frame, last-frame clamp, silent-table
  phase advance, FM depth, and bind on type/source.
- **Transients:** PCM loop unchanged; `trs` request round trip; one-shot
  sample end hold; burst gate and decay.
- **Drive types:**
  - `DriveShaper` functions match `plot_waveshapers.py`, and the crossing
    logic is equivalent to the script without a stored previous sample;
  - the `dty` writer resets state on change;
  - `sft` is bit-identical.
- **Descriptors and Preset:** rows, counts, pages, the generalised selector
  helper and the Preset renames.
- **`InstrumentCellFormat`:** the transient list segmentation.
- **Menu and boot:** automation capped at 127, and the boot notice.

### 16.5 Before hardware checks

1. **Fix F1 and F2** (both small). Re-run X1 with long-named and nested
   folders, including a four-deep wavetable.
2. **Fix F4** (either variant) and add the mixed-format test.
3. **F3 and F5:** decide (optimise or re-target), split the burst loop,
   add the per-type wrappers to `armcheck-vx2`, and correct every cost
   figure in §15 and the specs.
4. **Optionally** fix F7–F11 and N1/N2 in the same pass.
5. **Then X1–X8** (§13.3). X7 (worst-case CPU) must use the worst drive
   type (`fdc`), a `wtb` FM oscillator and a sample transient per voice.

---

## 17. Review fixes applied (2026-10-26)

**Scope:** the §16 findings, applied in the working tree.

- **Fixed:** F1, F2, F4, F5, F7, F8, F9, F10, F11, N1, N2, N3, N4.
- **Optimised and re-measured:** F3. The plan target is still not met for
  any type; a re-target decision is needed (§17.4).
- **Unchanged:** F6 (`wtb` 64/70, the accepted deviation).
- No commit was made.

### 17.1 Changes by finding

#### F1 — long-named subfolders install

- **Where:** `filesystem.c` `filesystem_installSampleTree()` and
  `filesystem_installWavetableTree()`.
- **Change:** both open each subfolder with
  `filesystem_blockOpenDirLfn(chosen.key)`, the verified display name from
  the object scan, instead of the 8.3 alias.
  - asyncfatfs compares only the long name for entries that have one, so the
    alias never matched a long-named folder.
  - FAT forbids two names equal case-insensitively, so the case-insensitive
    match is unique.
  - A failed open on a healthy card is now counted (`skipped_files` for a
    sample folder, `skipped_tables` for a wavetable folder) instead of
    vanishing.

#### F2 — no hang at depth

- **Depth:** `filesystem_installWavetableTree()` recurses only while
  `depth + 1 < FS_INSTALL_MAX_DEPTH`. The deepest open chain is now
  `wavetables/` + 3 folders + 1 frame file = 5 handles, the same as the
  sample tree.
- **Retries:** `filesystem_blockOpen()`, `_blockOpenLfn()` and
  `_blockOpenDirLfn()` no longer retry a refused open forever.
  - asyncfatfs refuses only when no handle is free.
  - A new helper, `filesystem_blockOpenGiveUp()`, allows
    `FS_BLOCK_OPEN_HANDLE_RETRIES` (2000) polls for an asynchronous close to
    release a handle, then returns NULL.
  - `block_file_ready` is reset after the refusal's NULL callback, so the
    wait that follows sees only the real open.
  - This protects every blocking open, not only the installer.

#### F3 — drive-type cost

- **`DriveShaper.h` `driveShaper_apply()`, stateful part:**
  - the crossings still run for all six stateful types, but the amplitude
    latch (`since`, `curMax`, `period`, `env`) now runs only for
    `tsw`/`lsw`/`lsq`, the only types that read it;
  - `|s|` is computed once;
  - the toggle is `flags ^= rise << 2`; signs come from the toggle bit;
  - the `fdc` counter wraps with a compare instead of `% 3`.
  - **Outputs are unchanged:** the skipped latch state is read only by the
    trackers, and a type change resets the state. `test_shaper` still
    passes.
- **`ResonantFilter.c` `SVF_calcBlockZDFShaped()`:** works on a stack copy
  `sh` of the voice's shaper state, written back on all three exits (the
  naive path, the off-type early return, the ZDF end). Through the pointer,
  the `uint8_t` stores forced the compiler to keep the state in memory every
  sample.
- **Tried and reverted:** branch-free crossings measured worse (+13…+22
  static instructions per type), because the branches are rarely taken; the
  branched form stays.

#### F4 — per-frame source format

- **`SampleInstall.c/.h`:** new `sampleInstall_setFrameFormat(fmt)`.
  - It switches the open table's source decoding (channels, source bits,
    block size) to the next frame file's own format, at a frame boundary
    only, and refuses a different stored depth.
- **`filesystem_installWavetable()` Pass B:** calls it before every frame
  copy.
- **Rule:** a table still requires one stored depth (8/16) and one frame
  length (Pass A). Source forms may now differ per file, as W1 allows.
- **Comment fix:** the function's comment block now states this rule.

#### F5 — transient cost

- **`transientGenerator.c` `transient_calcBlock()`:**
  - the SAMPLE loop reads one frame per sample (D6). It still holds silence
    after the last frame; the end test is now `index >= frames`, so the last
    frame plays;
  - the burst kinds have one loop each: table (Sin/Tri/Saw/Rec/sq3..swa,
    with the sine as a 4,096-point level), noise and crash. The per-sample
    `switch (kind)` is gone;
  - the table burst also reads one point per sample (D6; interpolated it
    measured 26).

#### F7 / F8 — one bad file no longer aborts the install

- **`SampleInstall.c/.h`:**
  - new `sampleInstall_abortItem()` closes the open item without an entry.
    Its bytes stay unused; `write_addr` already points past the programmed
    words.
  - `sampleInstall_finishSample()` / `_finishWavetable()` return −4 and
    abort when fewer bytes than declared were written (F8).
- **`filesystem.c` `filesystem_installCopy()`:** now returns
  `FS_COPY_OK` / `FS_COPY_READ_FAIL` / `FS_COPY_WRITE_FAIL`.
  - A read failure aborts that item: one sample file is skipped, or one
    table is skipped in Pass B. The install continues.
  - Only a flash or state failure aborts the install.
- **`filesystem_parseWav()`, data chunk:**
  - seeks to the exact data end (asyncfatfs clamps seeks to EOF), so a
    truncated or streamed file is skipped at parse time;
  - stops there when the format is already known, so a missing trailing
    pad byte on an odd-length chunk no longer rejects a valid file.

#### F9 — burst entries re-resolve after a reinstall

`transient_calcBlock()` re-resolves a `wav`-type transient on **any**
catalogue generation change, not only a SAMPLE kind. The stock waves follow
the samples in the list, so a changed sample count moves the burst entries
too.

#### F10 — `pq3`/`pw3` split after an install

- **`Oscillator.c` `osc_setFreq()`:** the frequency-cache key for pwm
  oscillators carries `sampleMemory_stockValid()` in bit 7 (types are
  < 0x80). A change in stock validity misses the cache once, and
  `oscPwm_prepare()` re-derives the natural split.
- **Cost:** no RAM; one compare per block for pwm only.

#### F11 — union hygiene

`InstrumentManager.c` `IM_SPECIAL_OSC_TYPE` writer: on a change *into*
`wtb`, `osc->wtb.base` and `primed` are cleared before the bind. The reverse
switch is safe because `freqCacheValid = 0` makes `oscPwm_prepare()` rewrite
every pwm field.

#### Notes

- **N1:** `filesystem_le16()` is removed. No new warnings remain; the
  remaining `filesystem.c` warnings are pre-existing.
- **N2:** `transient_trigger()` starts a sine burst at `phase = 1024 << 20`,
  the zero crossing of the `−cos` table. Table bursts keep phase 0: their
  starts are the waveforms' own edges, a deliberate click. Confirm at X5.
- **N3:**
  - the duplicated §15 header (with the old placeholder) and the stray `+`
    markers in this file are removed;
  - lone `+` diff markers removed from `CPU_USE_DSP_AUDIT.md`,
    `BANK_PRESET_ARCHITECTURE.md`, `MIDI_SPEC.md`,
    `MODULE_INTERCHANGE_SPEC.md` and `INSTRUMENTS_DSP_REFERENCE.md`.
- **N4:** frames beyond the 128th valid one are counted in `skipped_files`.

### 17.2 Tools, tests and specifications

- **`tools/dsp_test/armcheck/vx2_spec.c`** (new) and the `armcheck-vx2`
  target:
  - force-inlined, renamed copies of `SVF_calcBlockZDF`,
    `SVF_calcBlockZDFShaped` and `transient_calcBlock` (generated with `sed`
    from the extracted source);
  - one wrapper per drive type and per transient kind, with the switch
    value constant, so each wrapper is exactly one loop;
  - the target prints the stock reader's per-sample loop (loop 1), `wtb`,
    six transient kinds, `sft` and the 13 drive types;
  - the misleading "loop 0" comparisons are removed.
- **`test_catalogue.c`:**
  - the table-limit loop now writes each declared frame (F8 made the empty
    finish an error);
  - new cases:
    - **F4:** one table from mono 16-bit, stereo 16-bit and mono 24-bit
      frames reads back exactly; a different stored depth is refused;
    - **F8:** an incomplete sample returns −4 with no entry;
    - **F7:** items install after an abort or a refusal; only the complete
      sample has an entry.
  - **Proof:** with `setFrameFormat()` made a no-op (as the old code
    behaved) the F4 cases fail (2 failures); with the fix, 0.
- **`test_transient.c`:** new cases:
  - **F9:** a burst entry becomes sample 2 after the sample count changes;
  - **N2:** the sine burst trigger phase.
- **Not host-tested:** F1/F2 (the FAT walk), F10 and F11 (`osc_setFreq` and
  the IM writer are not in the harness). Covered by inspection and X1/X4.
- **Specifications:**
  - `CPU_USE_DSP_AUDIT.md` item 36, the `INSTRUMENTS_DSP_REFERENCE.md` cost
    table and the `DSP_TEST.md` `armcheck-vx2` row now carry the corrected
    figures;
  - `FILESYSTEM_SPEC.md` "Sample" and "Wavetable" state the long-name opens,
    the wavetable depth, mixed frame forms, and the per-file and per-table
    skip rules.

### 17.3 Measured

**Build:** clean; only the pre-existing warnings.

| Item | §15.9 | After fixes | Delta |
|---|---:|---:|---:|
| `text` | 652,944 | 651,392 | −1,552 |
| `data` / `bss` | 428 / 414,264 | 428 / 414,264 | 0 |
| Flash headroom | 100,292 | 101,844 | +1,552 |
| ITCM | 12,688 | 12,720 | +32 |
| DTCM statics | 4,468 | 4,468 | 0 |

`make img` was rebuilt.

**Host suite:** all 18 targets pass:

- `filter` S1 unchanged;
- `special_tags` 185/0;
- `catalogue`, `transient` and `shaper` with the new cases.

**Loop costs** (`make -C tools/dsp_test armcheck-vx2`; static totals, one
specialised loop each):

| Loop | Before (§16.1) | After | Target |
|---|---:|---:|---|
| Stock reader / FM | 24 / 31 | 24 / 31 | pass |
| `wtb` / FM | 64 / 70 | 64 / 70 | 47 / 62 (F6, accepted) |
| Transient PCM | 18 | 18 | ≤ 22, pass |
| Transient sample | 29 | **15** | ≤ 22, pass |
| Transient sine / table burst | unmeasurable | **15** (interpolated: 26) | ≤ 22, pass |
| Transient noise / crash | unmeasurable | **14 / 16** | ≤ 22, pass |
| ZDF LP `sft` | 100 | 100 | reference |
| rct thf asf chb bsw sfd afd | 109 109 109 107 117 118 121 | 108 109 109 107 117 118 121 | `sft` + 6: miss (+7…+21) |
| art fdc rsb | 151 155 143 | **124 126 120** | `sft` + 6: miss (+20…+26) |
| tsw lsw lsq | 148 147 143 | **132 134 132** | `sft` + 6: miss (+32…+34) |

No drive type adds a division.

### 17.4 Decision needed: the drive-type cost target (F3)

The plan's `sft` + 6 cannot be met by these definitions:

- **Crossing-based types:** crossing detection alone is about 8
  instructions, and the trackers add a ramp and an amplitude latch.
- **Folds:** `sfd`/`afd` are a triangle fold plus a 4th-order polynomial.

**Recommendation:** re-target to the measured figures, as static totals:

- memoryless ≤ `sft` + 21;
- crossing ≤ `sft` + 26;
- trackers ≤ `sft` + 34.

Then let X7 (worst-case Scene with `lsw`, a `wtb` FM oscillator and the PCM
transient on every voice) decide whether the budget holds.

**Alternatives:**

- drop or simplify the costliest types (`lsw`, `tsw`, `lsq`);
- move the tracker latch to block rate, which would change their sound
  slightly.

### 17.5 Remaining before P2 closes

1. **Decide §17.4.**
2. **Hardware X1–X8** (§13.3), with these additions:
   - **X1:** long-named folders (`000 hvoice`, `my kicks`) at every level, a
     four-deep wavetable nest (it must be skipped, not hang), a table with
     mixed mono/stereo/24-bit frames, and a deliberately truncated WAV (it
     must be skipped, the install must complete);
   - **X5:** burst onset clicks (N2) and the un-interpolated sample and
     burst reads (D6) by ear;
   - **X7:** the worst case uses `lsw`.
