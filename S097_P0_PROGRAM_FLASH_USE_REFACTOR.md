# S097 P0 — Program Flash Use: Assessment and Refactor Candidates

Date: 2026-11-17. Branch `dev-ph7-alkaline` at `131f567e` (S096 P3).
Scope: §1–§7 are the assessment, measured on the unchanged tree. All
experiments ran on a copy of the tree in the session scratch folder. §8 records
the work done afterwards at the user's request: the SOM generator archived
(−8,536 B) and the stale comments of §6 fixed.

Goal set by the user: reduce program flash without changing function. Find
tables or functions that overlap, padding, dead space, and places where UI text,
names and discrete value tables can be merged and cleaned up.

---

## 1. Summary

**Baseline:** 742,696 of 753,664 B used, **10,968 B headroom**. That is already
under the 16 KiB `link_budget.py` warning. ITCM: 15,960 of 16,384 B.

**After §8 (SOM archived):** 734,160 B used, **19,504 B headroom**. ITCM unchanged.

| # | Candidate | Saving (B) | How known | Function | Gate risk |
|---|-----------|-----------:|-----------|----------|-----------|
| C | SOM generator archived (§8); was: store only the read columns (−7,056) | **−8,536** | **measured, done** | SOM page removed (generator never ran) | **passed gate** |
| D | `menuPages`: 122 of 136 rows are all-zero | ≈ −2,790 | table sizes | identical | jitter only |
| F | Descriptor labels as indexes, not pointers (28 → 20 B/row) | ≈ −3,200 | `sizeof` probe | identical | jitter only |
| G | One shared LFO descriptor block instead of 7 copies | ≈ −1,400 (after F) | row count | identical | jitter only |
| E | Fixed-width name tables trimmed to real width | ≈ −1,160 | measured padding | 3 long names shortened | jitter only |
| I | Value-name table merges, enum/padding fixes | ≈ −700 | table sizes | identical / label edits | jitter only |
| J | Merge the four value formatters | ≈ −1,000 to −2,000 | estimate | identical | jitter only |
| K | Filesystem clone families | ≈ −2,000 to −3,000 | estimate | identical | jitter only |
| S | Semantic label merges (pitch/tune, cutoff/freq, …) | small directly; enables F | — | label text changes | jitter only |
| **Subtotal, source refactors** | | **≈ −21,500** (≈ −13,000 still open) | | | |
| B1 | `DEV_STALL_DETECTION 0` | −1,856 | **measured** | dev feature off | **passes gate** |
| B2 | `DEV_MODE_LOGGING 0` | −17,820 | **measured** | dev feature off | 3 osc loops +1 |
| A | `-Os` on cold (UI/storage/USB/descriptor) sources | **−93,600** | **measured** | identical | 14 hot loops +1 (blocked) |
| M | `alk_zeroRow` (2 KB of zeros) to RAM | −2,048 | size | identical | needs RAM sign-off |

"Jitter only" is explained in §3: today, any change to the LTO unit moves some
hot loops by ±1 instruction, even a change in code the audio path never runs.
Every row above therefore needs the loop gate, including the pure-data ones.

**Recommendation, in order:**

1. First, find a way to keep DSP code generation from moving when cold code changes (§3.3).
   Every other item becomes cheap to verify after that. Until then, each change
   needs a loop-gate run and possibly a fix.
2. Batch the remaining source refactors (D, E, F, G, I, K, then J and S).
   ≈ 13 KB more (headroom ≈ 32 KB) with no change in behaviour. C is done (§8).
3. Decide the release configuration (B1/B2). B1 passes the gate as-is.
4. `-Os` on cold code (A) is the only lever that changes the picture by
   ~94 KB. It is blocked by the CPU ceiling until step 1 is solved.

---

## 2. Where the flash goes

### 2.1 Split by kind (from `build/lxr02.map`)

| Kind | Bytes |
|------|------:|
| Code in `.text` | 535,292 |
| Read-only data (`.rodata*`, merged into `.text`) | 195,739 |
|   of which string sections (`.rodata.str*`) | 16,165 |
| ITCM code (stored in flash, copied at boot) | 15,412 |
| Vectors, `.data`/`.dtcm` initialisers, stubs | ≈ 1,850 |
| libc / libm / libgcc (included above) | 7,031 |

### 2.2 Largest data

| Table | Bytes | Note |
|-------|------:|------|
| `sawTable`, `triTable`, `recTable`, `alkSqrTable` | 4 × 22,528 | 11 × 1,024 × int16 each |
| `transientData` | 26,460 | 12 × 2,205 int8; 2,483 B are zero tails |
| `SomData.c` `Node_0..8` | 8,064 | 93 % zero (§4 C); archived in §8 |
| `oscPwmTables.c` | 4,224 | |
| `alkResTable` | 3,744 | |
| `menuPages` | 3,264 | 96 % zero (§4 D) |
| `sine_table` | 2,050 | |
| `alk_zeroRow` | 2,048 | all zero (§4 M) |
| Instrument descriptors (7 types) | 9,100 | 325 rows × 28 B |
| Effect descriptors | ≈ 2,800 | 36 B rows |

The four stock wave tables were checked row by row. No two rows are identical,
including the `triTable` low octaves, which differ by 28..1,473 LSB. They cannot share
rows without changing sound. Folding them (half-wave symmetry, etc.) would
change the per-sample reader, which the CPU ceiling forbids. **Not a candidate.**

### 2.3 Largest code, by source file

LTO inlines across files, so a symbol is attributed to the file that defines its
name. Inlined callees count in the caller's file.

| Source | Bytes | Source | Bytes |
|--------|------:|--------|------:|
| `filesystem.c` | 131,240 | `copyClearService.c` | 16,934 |
| `menu.c` | 93,040 | `Oscillator.c` | 16,102 |
| `asyncfatfs.c` | 19,890 | `InstrumentManager.c` | 15,460 |
| `sequencer.c` | 19,198 | `main.c` | 13,996 |
| `buttonHandler.c` | 17,032 | `CrumpBitEffect.c` | 13,426 |

Storage and UI (`filesystem.c`, `menu.c`, `asyncfatfs.c`, copy/clear,
`buttonHandler.c`) account for ≈ 280 KB of the 535 KB of code. The biggest single
functions are `mixer_calcNextSampleBlock` (12,492), `buttonHandler_processEvents`
(10,364), `menu_repaintGeneric` (9,556), `main` (9,228), `ccSvc_tick` (8,884),
`filesystem_tick` (8,524), `menu_pollPresetStatus` (8,512) and the
Scene/Bank directory load/save ticks (4.4–7.8 KB each).

---

## 3. Structural finding: LTO jitter moves hot loops on every change

### 3.1 What was measured

Each build variant was compared against a loop snapshot of the current image.
The tools were the project's `elf_loopdump.py` and `elf_hotloops.py`, with the
`P4_OSC_SYMS` set and the mixer, which is the `elf_p4_check` gate. The
snapshot was written to scratch; the tracked `s096p1` baseline was not touched.

| Variant | Flash Δ | Osc loops (equal / shrank / grew) | Mixer hot loops (equal / shrank / grew) |
|---------|--------:|-----------------------------------|-----------------------------------------|
| `DEV_STALL_DETECTION 0` | −1,856 | all equal | 58 / 0 / 0 |
| `DEV_MODE_LOGGING 0` | −17,820 | 70 / 4 / 3 | 57 / 1 / 0 |
| `-Os` on cold sources (§4 A) | −93,600 | 68 / 4 / 5 | 49 / 0 / 9 |
| same, cold sources also `-fno-lto` | −83,860 | 68 / 4 / 5 | 49 / 0 / 9 |

Every "grew" and "shrank" is ±1 instruction. Logging off changes no DSP source,
yet three PWM loops in `.itcm.s094p1` grew. This is the effect already recorded in the
Makefile for Colloid/Tetrakt (S095 P2, S096 P1) and in MEMORY.md ("LTO re-spends
freed inline budget"). It applies just as much to shrinking cold code as to
adding new code.

### 3.2 Consequence for this refactor

The S089 ceiling forbids any per-sample increase. A flash refactor that touches
only menu or file code can still fail the gate by chance. Batching changes and
gating once per batch is cheaper than gating each change. A failing batch needs
a fix at the DSP side, not a change in the cold code.

### 3.3 Proposed first step (not measured)

Freeze DSP code generation against the rest of the program, so cold refactors
cannot move it. Options, in order of preference:

- **Partial LTO link of the DSP set.** Link `DSP_SRCS` plus the few non-DSP
  sources the audio path inlines (`SceneData.c` accessors, `StepScale.c`, the
  render-loop part of `main.c`, `InstrumentManager.c` writers; see §4 A) into one
  relocatable object with `-flto -r -flinker-output=nolto-rel`. Then link that
  object into the main LTO link as ordinary code. Hot code then depends only on
  its own sources. Cost: one re-baseline, and loss of any inlining across the
  boundary, which the gate will show.
- **`-flto-partition=one`.** This removes partition-boundary moves but not
  global inline-budget moves, so it is probably not enough on its own.
- **Move cold code out of LTO.** This is the `-fno-lto` row above. Measured: it
  did not stop the jitter, because the remaining LTO unit still changed.

---

## 4. Candidates

### A. `-Os` on cold sources — −93,600 B (measured)

The Makefile comment at the Space kit says "a per-file -Os was tried and has no
effect under -flto". That held for `SpaceKit.c`, but it does not generalise.
Compile-time `-Os` is kept per function through LTO:

| Variant | Flash |
|---------|------:|
| baseline | 742,696 |
| link-time `-Os` only | 741,872 (−824) |
| `-Os` on every non-DSP source (`SRCS`) | 607,520 (−135,176) |
| `-Os` on the cold set below | 649,096 (**−93,600**) |

Cold set: `filesystem.c`, `storageTypes.c`, `asyncfatfs.c`, `fat_standard.c`,
`menu.c`, `menuEffects.c`, `Cc2Text.c`, `screensaver.c`, `SplashAnimation.c`,
the four CopyClear sources, `buttonHandler.c`, `ledHandler.c`, `BankData.c`,
`Autosave.c`, `AutosaveTrace.c`, `PatternTrace.c`, `MidiLearn.c`,
`SampleInstall.c`, `sampleFlash.c`, `memtest.c`, `flashImage.c`, the USB
library and app sources except `usb_it.c`/`usb_bsp.c`/`usb_midi_core.c`, and
all nine `*Parameters.c`.

**Not cold, despite appearances.** A first cold set included `SceneData.c`
and `StepScale.c`. With them at `-Os`, `scene_getActiveIndex`,
`scene_getVoiceFaderSetting`, `scene_getVoiceFxSendAmount/Morph` and
`stepScale_ticksQ8` stopped inlining into `mixer_calcNextSampleBlock`,
`audio_check_and_render` and `TIM3_IRQHandler` and became calls. They must stay
at `-O2`. Any final cold list needs the same check: compare the call sets of the
hot functions before and after.

Effects of `-Os` beyond size: the menu, file system and USB stack run slower.
Load/Save loops are bounded by `filesystem_batchHasTime()`, so behaviour holds,
but throughput per tick drops. This needs a hardware Load/Save timing check.
**Blocked** by §3 (9 mixer loops and 5 oscillator loops +1) until §3.3 is done.

### B. Release configuration — −19,676 B (measured)

- `DEV_STALL_DETECTION 0`: −1,856 B, gate clean.
- `DEV_MODE_LOGGING 0`: −17,820 B. 3 oscillator loops +1, 4 shrank.
- `MEMTEST_ENABLED 0`: 0 B. `memtest_run()` is commented out in `main.c`, so
  `memtest.c` is compiled but never linked.

These remove dev functions, so they are a policy decision for the user, not a
refactor. If the shipped image keeps logging, there is nothing to take here.

### C. SOM node tables — −7,056 B, identical output (superseded: archived, §8)

The user chose to archive the whole SOM generator instead (§8): −8,536 B
measured. The analysis below is kept because it applies to any
re-implementation (`archive/som/README.md`).

`SomData.c` holds `Node_0..Node_8` as `[7][128]` uint8 (896 B each, 8,064 B
total). Every non-zero byte sits at a step that is a multiple of 8.
`som_interpolate()` reads `som_nodes[n][i*128 + stepNr]`. Its only caller is
`som_tick()` (`SomGenerator.c:113`), which returns at once unless
`stepNr % 8 == 0`. `som_tick()` is called only from `sequencer.c:3326` with
`seq_stepIndex[0]`, which can range 0..127 (track length is `DTYPE_1B128`,
`NUM_STEPS` 128). Storing `[7][16]` and reading `[i*16 + (stepNr >> 3)]` returns
the same bytes on every reachable call. Size: 9 × 112 = 1,008 B, down from 8,064.

The comment at `sequencer.c:211` says `seq_stepIndex` is "fixed 0..15". That
contradicts the 1..128 track length. The comment is stale; nothing in this item
depends on it.

### D. `menuPages` — ≈ −2,790 B, identical

`menuPages[NUM_PAGES][NUM_SUB_PAGES]` is 17 × 8 rows × 24 B = 3,264 B.
**122 of the 136 rows are all-zero.** Voice pages now come from instrument
descriptors; only a handful of static pages remain. Four access sites
(`menu.c:4769`, `13405`, `20817`, `20827`) index it. Proposal: keep the 14
real rows (336 B) and add a 136-byte row index (0 = empty, which reads as an
all-`TEXT_EMPTY` row). The page/subpage pairs that are actually used can be
listed from the table itself.

### E. Fixed-width name tables — ≈ −1,160 B

`static const char name[][N]` tables pad every entry to the longest one. Measured padding:

| Table | Width | Size | Text | Padding | Proposal |
|-------|------:|-----:|-----:|--------:|----------|
| `longNames` | 16 | 1,632 | 808 | 824 | `[9]`, −714 B |
| `catNames` | 16 | 480 | 205 | 275 | `[9]`, −210 B |
| `midiFilterNames` | 16 | 272 | 52 | 220 | `[4]`, −204 B |
| `filterTypes` | 8 | 72 | 31 | 41 | `[4]`, −36 B |

`longNames` has three entries over 8 characters: "Gate Mode" (9), "CPU use time"
and "PtrnStoreUse" (12). Each needs an 8-character name, for example "GateMode",
"CPU use" and "PtrnUse". Every `catNames` entry already fits 8. The readers
(`menu.c:13099–13100`, `15422–15423`) take `const char *` from these tables, so
the width is not baked into them. Each width change still needs a check for
16-byte copies.

### F. Descriptor labels as indexes — ≈ −3,200 B

`ParamDescriptor` is 28 B: four `const char *` (key, short, long, category),
then dtype, flags, mod domain and binding. A size probe with the real headers
gives **20 B** when short/long/category are `uint8_t` indexes into shared label
pools (the file key stays a pointer: it is the storage key).

- Instruments: 325 rows × 8 B = −2,600 B.
- Effects: `effect_param_descriptor_t` embeds `ParamDescriptor` (36 B → 28 B).
  About 78 rows: −620 B.
- The label pools mostly exist already. `shortNames`, `longNames` and `catNames` in
  `MenuText.h` hold the same words that the descriptors repeat as literals
  ("Frequncy", "Overdriv", "Waveform", "Oscilltr" each appear twice in the
  image). Merging them removes ≈ 1.2 KB of duplicate text inside the pools.
  This part overlaps with E and S.

Readers to convert: `menu.c` (20 sites), `menuEffects.c`, `EffectsManager.c`
(`effects_*` label accessors), `CrumpBitParameters.c` (4),
`SceneModTargets.c` (3). Scene mod-targets have their own descriptor type.
**Their `short_name` is the `midilrn.cfg` key** (`MidiLearn.c:522`, `645`).
Instrument and effect MIDI Learn entries use `file_key`, so instrument and
effect labels are display-only. A Scene-target label rename changes stored
MIDI Learn lines.

### G. One shared LFO descriptor block — ≈ −1,400 B (after F)

Each of the 7 instruments carries the same 12 LFO rows (`lfo_rate` …
`lfo_target_param_2`). They differ only in the binding offset, because `lfo` sits
at a different place in each voice struct. Binding them relative to the `lfo`
member, plus one per-type `lfo` base offset, makes the 12 rows identical. One
shared block then replaces six copies: 72 rows × 20 B ≈ 1,440 B (2,016 B at
today's 28 B rows). Cost: descriptor lookup becomes "own rows + shared tail",
which touches `instrumentManager_*` row access and the descriptor-index maps.
This is medium effort. The row indices and file keys stay as they are.

### I. Value tables and padding — ≈ −700 B

- `parameter_dtypes[NUM_PARAMS]` is `const enum Datatypes` (4 B per entry,
  384 B, 315 B zero). As `uint8_t`: −288 B. It is the only enum-typed const table.
- `scene_mod_targets` uses `ModTarg {uint8_t nameIdx; uint16_t param;}`, which
  pads to 4 B (252 entries, 1,008 B). Split arrays or a packed 3-byte form: −252 B.
- Identical or overlapping value-name tables:
  - `colFiltTypeNames` is `colRouteNames` without its first entry ("str"): share one table.
  - `stepRollRateNames[1..9]` equals `perfBtnTypeNames[6..14]`: share one table.
  - `sceneChangeDivNames` (10) is a subset of `sceneQueueDivNames` (18): use one table and an index map.
  - `menuEffects_runShort` and `storage_effectRunModeTokens` are the same
    five strings. Keep the storage table (it is a file format) and point the
    menu at it.
  - Three filter label sets with different order and padding: `filterTypes`
    (LP HP BP UBP Nch Pek LP2 off), `colFiltTypeNames` (LP␠ BP␠ HP␠ Nch Pek UBP off),
    `colRouteNames`. Proposal: one canonical filter label pool, with each
    instrument listing indexes into it (see S).
  - Wave labels: `waveformNames` (Sin Tri Saw Rec Noi), `alkWaveNames` (Saw **Sqr** Tri),
    `colWaveNames` (Saw Rec Tri Sin). Proposal: one pool; "Rec" or "Sqr", not both.
  - Type labels: `oscTypeNames`, `alkTypeNames`, `colTypeNames` (wav smp slp wtb pwm wts):
    one pool plus per-type index lists.

  These are small in bytes (≈ 150 B), but they are where the consistency work lands.

### J. Value formatters — ≈ −1,000 to −2,000 B (estimate)

Values are turned into text by several separate dtype switches:
`menu_cellDisplayValue` (1,104), `va_formatValue3` (716),
`menu_formatAutomationValue3` (356), `menu_formatEffectAutomationValue3` (200),
`crumpBit_uiFormatValue3` (756) and `getMenuItemNameForValue` (468). Each is
also inlined into larger repaint functions. One `dtype → text` formatter, called
by all of them, removes the parallel switches. This needs a display-equivalence
check per dtype. A host test over every dtype × 0..255, old against new, makes
it mechanical.

### K. Filesystem clone families — ≈ −2,000 to −3,000 B (estimate)

Pairwise similarity of normalised instruction streams:

| Pair / family | Similarity |
|---------------|-----------:|
| `filesystem_saveGlobals_tick` ~ `filesystem_saveMidiLearn_tick` | 98 % |
| `filesystem_blockOpen` / `blockOpenLfn` / `blockOpenDirLfn` | 97 % |
| `preset_loadKitForScenes` ~ `preset_loadSceneForScenes` | 95 % |
| `filesystem_scanBanks/Kits/Scenes/Patterns_tick` | 60–79 % |
| `filesystem_loadGlobals_tick` ~ `filesystem_loadMidiLearn_tick` | 76 % |
| `filesystem_loadKit_tick` ~ `filesystem_loadName_tick` | 63 % |

Each pair becomes one function taking a small per-type descriptor. These sit in
the async state machines, so phase numbering and watchdog/stall coordinates must be kept.
`-Os` (A) would capture part of the same saving automatically.

### S. Semantic label merges (user request)

Constraints: short labels are 3 characters and long labels 8, set by the LCD
cell. `file_key` never changes (storage). Scene-target short names are MIDI
Learn keys (F). Label changes make the manual and `MIDI_NRPN_TABLE.md` out of date.

Clusters found in the 325 instrument rows (current → proposal for user decision):

| Concept | Current labels (category / long / short) | Proposal |
|---------|-------------------------------------------|----------|
| Filter cutoff | Drum family `Filter/Frequncy/frq`; Colloid `Filter/Cutoff/frq`; Alkaline `Filter/Cutoff/cut` | `Cutoff` / `cut` everywhere |
| Resonance | `Resnance/res`; Colloid `Reso/res` | one long form |
| Drive | `Overdriv/drv` (filter drive *and* voice drive in the Drum family, Alkaline); Colloid `Drive/drv`; Tetrakt `Drive/drA..D` | `Drive`; the filter and voice rows differ by category only |
| Pitch | `Coarse/coa` + `Fine/fin`; Alkaline `Tuning/tun`; Tetrakt `Tune/tuA..D`; FM `Frequncy/frq` | `Tune` for a single semitone control; `Coarse`/`Fine` where there are two; drop `Tuning` |
| Frequency spellings | `Frequncy`, `Freqcy 1`, `Freqcy 2` | one spelling |
| Pan | `Panning/pan`; Colloid noise and Tetrakt layers `Pan` | `Pan` |
| Sustain | Colloid `SustLvl/slv`, `Sustain/sus`, `SustLvl/fsl`, `psl` | `Sustain`; `slv` also means Tetrakt *Space* `Level` (clash) |
| Filter type | `Type/typ`; Colloid `FiltType/typ` | `Type` (the category says "Filter") |
| Amp envelope category | Drum family `Veloc EG`; Colloid `Amp EG`; Tetrakt `Envelope` | `Amp EG` |
| Pitch envelope category | Drum family `PitchMod`; Colloid `Pitch EG` | `Pitch EG` |
| Velocity | category `Velocity`; long `Vol mod/vel` | `Velocity` / `Vel Vol` |
| Repeat | `RepeatCt/rpt` | `Repeat` |
| Decay pair | HiHat `Dcy Clsd/d1`, `Dcy Open/d2` | keep (distinct meaning) |

Direct byte saving is small (each unique long label ≈ 9 B). The value is that F and
I then index one consistent pool. Fewer unique labels also keep the pool
indexes in a `uint8_t`.

### M. `alk_zeroRow` — −2,048 B flash, +2,048 B RAM

`AlkalineVoice.c:77`: a 1,024-point all-zero `int16_t` row, bound as the
wavetable when no table is installed. Moving it to `.bss` frees flash but costs
RAM, which needs sign-off under the RAM policy (owner Alkaline, region SRAM1
or DTCM). Otherwise keep it.

---

## 5. Looked at and not recommended

- **Stock wave tables and `transientData`:** all rows are distinct. Any
  compression changes the per-sample reader (CPU ceiling).
- **`DiodeLadder_renderB2Osc*` (73 % alike) and the oscillator `*Ramp` twins
  (95–97 %):** deliberate ITCM specialisations (S089 P3, S094 P1). These are binding.
- **`Drum_triggerVoice` ~ `Cymbal_triggerVoice` (90 %), the other triggers
  (67–73 %), `Cymbal/HiHat_calcAsyncVoice` (identical opcodes):** these are
  mergeable, but they are `-Ofast` engine code under the S093 per-block cost rule.
  ≈ 1–2 KB at most, so a low priority after §3.3.
- **libm:** `sinf`/`cosf` are used only by `SampleInstall.c` and the dev tone
  generators. They pull in `__aeabi_l2d` and soft double add/sub (888 B).
  Replacing them changes installed wavetable bytes, which is a function change.
  `powf` (adcPots init, CrumpBit `pit` label) is ≈ 1.1 KB with its data.
  Leave both.
- **Link-time `-Os` alone:** −824 B.

---

## 6. Stale comments and tooling notes found

Stale code comments (all three **fixed in §8**):

- `sequencer.c:211`: `seq_stepIndex` "fixed 0..15" — the track length is 1..128.
- `AlkalineSquareTable.c` header: "levels 7-10 repeat level 6". The generator
  (`gen_alk_tables.py` `square_table()`) builds levels 7–10 from the level 6
  SPICE cycle, then band-limits each to its own octave. That is why the linked
  rows differ (up to 12,942–18,440 LSB between adjacent levels). The data is
  right and the comment was imprecise.
- Makefile, Space kit comment: "-Os … has no effect under -flto" is true for that
  file only; see §4 A.

Tooling notes (not changed):

- `make -C tools/dsp_test elf_p4_check` writes `tools/dsp_test/build/p4_osc_now.txt`
  and `p4_mixer_now.txt`, which are **tracked** in git, so every gate run dirties
  the tree. This session restored them with `git checkout`, and ran the tools
  directly with scratch `--out` paths afterwards.
- The tracked gate baseline `baselines/s096p1/p4base` no longer matches HEAD.
  The unchanged `131f567e` image already reports 27 MISSING oscillator loops
  (4 renamed) and 2 grew / 2 shrank mixer hot loops against it, so
  `elf_p4_check` fails before any S097 change. S096 P2/P3 were not
  re-baselined. Gate S097 work against a snapshot of the pre-change image (as
  §3 and §8 do), or re-baseline once the S096 P3 image is accepted.

---

## 7. Method (for repeating the measurements)

- Build: `make all` (arm-none-eabi-gcc 14.2.1, `-O2 -flto`, DSP `-Ofast`).
  The baseline was rebuilt in a scratch copy and matched the tree's image
  (742,696 B).
- Code vs data: `.rodata*` input sections from `build/lxr02.map`. Symbol sizes
  from `arm-none-eabi-nm -S`. Attribution to sources is by matching symbol names
  to definitions in `SRCS`/`DSP_SRCS`.
- Duplicate/zero data: byte comparison of every flash symbol ≥ 32 B (whole-symbol and 2,048-B row hashes,
  zero counts).
- Clone search: `objdump` instruction streams with addresses, literals and
  register-free branch targets normalised, then pairwise `difflib` ratio.
- Variants: Makefile/config edits in scratch copies only, built with `make all`.
- Loop gate: `elf_loopdump.py` (P4_OSC_SYMS and mixer) on the scratch baseline,
  then `elf_hotloops.py --compare` on both dumps.

---

## 8. Work done: SOM generator archived, stale comments fixed

### 8.1 Why SOM could go

The SOM ("self-organising map") Pattern generator could never run.
`som_tick()` was called only under `if (seq_SomModeActive)` in
`seq_advanceTrackStep()`. `seq_SomModeActive` was a `static uint8_t` that nothing
set, so the branch was dead. The page (SHIFT+MODE STEP → `SELECT_MODE_SOM_GEN`
→ `SOM_PAGE`) still opened and edited X/Y/flux/frequency state that nothing read.

### 8.2 Result (measured, `make all`)

| | Before | After | Δ |
|---|---:|---:|---:|
| Flash used | 742,696 | 734,160 | **−8,536** |
| Flash headroom | 10,968 | 19,504 | +8,536 |
| `.bss` | 422,540 | 422,508 | −32 (`somGenerator` state) |
| ITCM | 15,960 | 15,960 | 0 |

The saving is the 8,064 B of node tables plus ≈ 470 B of generator, setter and
menu code. `nm` shows no SOM symbol left in the image. The build has no new
warnings. `make img` wrote `build/LXRV2_lxr02.img` (734,160 B). That file is
tracked, so it shows as modified.

**Loop gate**, against a snapshot of the unchanged `131f567e` image, using
`elf_loopdump.py`/`elf_hotloops.py` on the `P4_OSC_SYMS` set and the mixer:

- 86 of 86 oscillator loops exact, 0 renamed, 0 missing;
- 77 oscillator hot loops equal, 0 shrank, 0 grew;
- 58 mixer hot loops equal, 0 shrank, 0 grew.

No per-sample cost changed. Against the tracked `s096p1` baseline the result is
the same as at unchanged HEAD (§6).

### 8.3 Changes

| File | Change |
|------|--------|
| `Core/Bank/Scene/Pattern/SomGenerator.c/.h`, `SomData.c/.h` | moved with `git mv` to `archive/som/` (renames are staged); not in any include path |
| `archive/som/README.md` | new: what the module was, why it is archived, the `[7][16]` note, and how to restore it |
| `Makefile` | `SomGenerator.c`, `SomData.c` removed from `SRCS`; Space kit `-Os` comment corrected (§6) |
| `main.c` | `#include "SomGenerator.h"` and `som_init()` removed |
| `Core/Sequencer/sequencer.c` | include, `seq_SomModeActive`, the dead `som_tick()` branch and the always-true `!seq_SomModeActive &&` term removed; `seq_stepIndex` comment corrected (§6) |
| `Core/Menu/menu.c` | include and the four `menu_parseGlobalParam()` cases (`PAR_POS_X`, `PAR_POS_Y`, `PAR_FLUX`, `PAR_SOM_FREQ`) removed; a comment says the ids have no action |
| `Core/Menu/menuPages.h` | `SOM_PAGE` first row blanked (`TEXT_EMPTY`/`PAR_NONE`), marked unused |
| `Core/Menu/menu.h` | `SOM_PAGE` kept and marked unused, so `EFFECT_PAGE` keeps its id |
| `Core/Bank/Scene/Preset/ParameterArray.h` | the four SOM `PAR_*` ids kept and marked unused, so later ids and the globals block keep their values |
| `Core/Hardware/frontPanel/buttonHandler.h` | `SELECT_MODE_SOM_GEN` removed; 0x06 noted as unused |
| `Core/Hardware/frontPanel/buttonHandler.c` | SHIFT+MODE STEP now returns before any mode, page, LED or latched-surface change; `SELECT_MODE_SOM_GEN` case removed; two erase-router comments no longer list SOM |
| `Core/MIDI/MidiPitch.h`, `Core/Menu/CopyClear/copyClearSession.c/.h` | comments no longer list SOM |
| `Core/DSP/Instruments/Alkaline/AlkalineSquareTable.c`, `tools/alkaline/gen_alk_tables.py` | header comment corrected in both (§6); `gen_alk_tables.py --check` passes |
| `README.md` | `SomData`/`SomGenerator` removed from the tree; `archive/som/` added |

**Kept on purpose:** the `PAR_*` ids (`ParameterArray.h`), the `TEXT_POS_X`,
`TEXT_POS_Y`, `TEXT_FLUX`, `TEXT_SOM_FREQ` name ids with their `valueNames`
entries, the four `parameter_dtypes` entries and the `SOM_PAGE` id. Removing them
would renumber every later id. That includes the `parameter_values[]` layout,
`parameter_dtypes[]`, and the page-indexed `menuPages` rows, for a few bytes. They
can be reclaimed together with §4 D/I if those items renumber anyway.

**Behaviour change:** SHIFT+MODE STEP does nothing (it used to open the SOM
page). Nothing else that can be reached changes. Copy/clear already refused
mode 0x06, so its gating is the same.

Needs the device: one press of SHIFT+MODE STEP in each mode (VOICE, STEP, PERF,
FX, Load/Save, MENU), to confirm the panel stays where it was.

### 8.4 Statements now out of date (for the user; not edited, per the S093 rule)

- `knowledge_files/specification_reference/MENU_UI_SPEC.md`: line 47
  (`SOM_PAGE` "SOM generator" in the page table), lines 392 and 509 ("SHIFT+MODE STEP
  is only SOM entry"). It now has no action.
- `knowledge_files/specification_reference/BANK_PRESET_ARCHITECTURE.md:507`:
  "SHIFT+MODE STEP is the only SOM entry ((2+4)&7 = 6)".
- `knowledge_files/specification_reference/MODULE_INTERCHANGE_SPEC.md`: the
  `Core/Bank/Scene/Pattern/SomGenerator` section (≈ lines 832–845), the
  affiliate list at ≈ 964, and the boot order line 2195 (`som_init()`).
- `knowledge_files/specification_reference/REPOSITORY_LAYOUT.md:122–123`:
  `SomData.c/h`, `SomGenerator.c/h` under `Pattern/`; `archive/som/` is new.
- `knowledge_files/specification_reference/STORAGE_SRAM_MANIFEST.md:559`:
  "the Euklid and SOM generators are compiled in". SOM no longer is.
- `knowledge_files/hardware_archive/AVR_TO_F765_MIGRATION.md:7` lists
  `SomGenerator.c/h` (historical; may be left as is).
- The MEMORY.md and `STORAGE_SRAM_MANIFEST.md` flash figures, if any quote the
  S096 total: now 734,160 B used, 19,504 B headroom.
