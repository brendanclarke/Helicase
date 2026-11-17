# S097 P1 — Menu and Semantics Refactor: Implementation Record

Date: 2026-11-17. Branch `dev-ph7-alkaline`, on top of S097 P0 (SOM archived).
Source of the items: `S097_P0_PROGRAM_FLASH_USE_REFACTOR.md` §4 D, E, F, G, I,
J, K. This record is written as each item is done.

User decisions (2026-11-17):

| P0 item | Here | Decision |
|---------|------|----------|
| D `menuPages` | 1 | do about half; leave sensible padding for future menus |
| F labels as indexes | 2 | first: expand what it means so the user can decide; then (same day, after §2 was written up): **do it** |
| G shared LFO block | 3 | do |
| E name-table widths | 4 | do; renames "GateMode", "CPU use", "PtrnStor" |
| I, J, K smaller items | 5 | do |

**Reference image** for every check: the S097 P0 image (SOM archived), 734,160 B.
**Loop gate** after each item: `elf_loopdump.py` on the `P4_OSC_SYMS` set and
`elf_hotloops.py` on the oscillator set and the mixer, both against the
reference image's dumps. All output goes to scratch, so the tracked
`tools/dsp_test/build/` files are not written.
**Data check** after each data item: a script reads the old table from the
reference ELF and the new table from the new ELF, then compares every entry.

## Running totals

| Step | Flash used | Δ vs reference | Gate |
|------|-----------:|---------------:|------|
| reference (S097 P0) | 734,160 | — | — |
| 1 `menuPages` | 732,120 | −2,040 | all loops exact |
| 4 name-table widths | 730,960 | −3,200 | all loops exact |
| 5 K filesystem clones | 729,976 | −4,184 | all loops exact |
| 5 J value formatters | 729,688 | −4,472 | all loops exact |
| 2 labels as indexes | 726,656 | **−7,504** | all loops exact |
| 3 shared LFO block | — | — | **paused**, see §3 |

---

## 1. `menuPages` → static-page table (done, −2,040 B)

**Before:** `menuPages[NUM_PAGES][NUM_SUB_PAGES]` held 17 pages × 8 sub-pages ×
24 B = 3,264 B. Only six pages read cells from it: MENU_MIDI (sub-pages 0–3),
PERFORMANCE (0–1), SEQ (0–2), EUKLID (0), PATTERN_SETTINGS (0) and RECORDING
(0–1). The other eleven pages (VOICE1–7, LOAD, SAVE, SOM, EFFECT) resolve their
cells elsewhere (instrument descriptors, the Load/Save screens, `menuEffects.c`)
and had all-zero rows.

**Padding decision.** The user asked for about half, leaving room for menus to
grow. Each of the six table pages keeps all eight sub-pages (48 rows, 14 used,
34 spare), so every static menu can still grow to eight sub-pages without
touching the layout. Only the rows of the eleven pages that structurally cannot
use the table are gone: 88 rows, 2,112 B. The compact form proposed in P0
(14 rows) would have saved a further ≈ 680 B and was not taken.

**After:**

- `menuPages.h`: `static const Page menuStaticPages[6][NUM_SUB_PAGES]` (1,152 B)
  and `static const uint8_t menuStaticPageSlot[NUM_PAGES]`, which is 1-based with
  0 meaning "not a table page" (17 B). The pages appear in the same order with
  the same rows and comments. A new static page needs a block and a slot entry.
- `menu.c`: `menu_pageRow(page, subPage)` returns the page's row, or the shared
  all-zero `menu_emptyPageRow` for a page with no block. Because `TEXT_EMPTY` and
  `PAR_NONE` are both 0, that row equals every former zero row. The four
  former direct reads now call it: `menu_resolveCellAbsolute()`,
  `checkScrollSign()`, and `menu_devCheckGlobalSceneSubPage()` (two reads).
  All of them bound the sub-page to 0..7, so the per-page block of 8 is never
  overrun.
- `menu.h`: the `extern const Page menuPages[...]` is removed, since only
  `menu.c` uses the table, and the `EFFECT_PAGE` comment no longer refers to a
  "dense" table.

**Checks:**

- Data: all 136 (page, sub-page) rows rebuilt from the new ELF
  (slot map + static block, or zeros) equal the 136 rows of the reference
  `menuPages`: 0 mismatches.
- Loop gate: 86/86 oscillator loops exact; 77 oscillator and 58 mixer hot loops equal.
- Flash: 734,160 → 732,120 (−2,040 B; 2,112 B of rows less 17 B of slot map and
  accessor code).

---

## 4. Name tables trimmed to their real width (done, −1,160 B)

**Readers.** `longNames` and `catNames` are read only by
`menu_displayModTargetFull()` and `menu_repaintGeneric()`. Both copy at most 8
characters (`for (i = 0; i < 8 && s[i]; i++)`) into an LCD half that
`menu_repaintGeneric()` has already filled with spaces. `filterTypes` and
`midiFilterNames` are read as `p = table[value + 1]` (3-character cells) and
`table[0][0]` (the entry count). No reader uses the row width (`sizeof` or a
fixed-length copy).

| Table | Width | Size before → after | Δ |
|-------|------:|--------------------:|--:|
| `longNames` | 16 → 9 | 1,632 → 918 | −714 |
| `catNames` | 16 → 9 | 480 → 270 | −210 |
| `midiFilterNames` | 16 → 4 | 272 → 68 | −204 |
| `filterTypes` | 8 → 4 | 72 → 36 | −36 |

**Renames** (user's choices). The LCD already showed only the first 8
characters of each:

| Index | Before (stored → shown) | After | Visible change |
|------:|-------------------------|-------|----------------|
| 63 | "Gate Mode" → "Gate Mod" | "GateMode" | yes |
| 65 | "CPU use time" → "CPU use " | "CPU use" | no (the 8th cell stays a space) |
| 74 | "PtrnStoreUse" → "PtrnStor" | "PtrnStor" | no |

A comment above `catNames` records the 8 + NUL rule. The `PtrnStor` comment no
longer claims a 15-character field; only 8 characters were ever shown.

**Checks:**

- Data: every entry of the four tables compared old against new by the text
  the readers use (first 8 characters for long/category names, first 3 for the
  value names, plus the count byte). The only differences are the two above:
  [63] "Gate Mod" → "GateMode" and [65] "CPU use␠" → "CPU use".
- Loop gate: all loops exact; all hot loops equal.
- Flash: 732,120 → 730,960 (−1,160 B).

---

## 3. Shared LFO block (paused: the P0 premise was wrong)

P0 §4 G assumed the LFO rows could become one shared tail ("own rows + shared
tail"). Reading the tables shows they cannot:

- **The descriptor index is the storage cell.** A row's index is its cell in
  the Scene instrument image (`instrument_parameters[descriptor_index]`), in
  AutoSave, in the per-type enums (for example `DrumParameters.c` "These enum
  values must stay in the same order as drum_param_descriptors[]") and in the
  menu page maps. Rows cannot move.
- **The LFO rows are not a block.** In each instrument they sit mid-table, at a
  different index per type, and are split by other rows. In Drum (0-based):
  8 LFO rows (21–28, `lfo_rate` … `lfo_offset`), then 3 Velocity rows (29–31),
  then 4 LFO target rows (32–35). The first LFO row is at 21 in Drum, Cymbal
  and HiHat, 20 in Snare, 12 in Alkaline, 36 in Colloid and 48 in Tetrakt.
- **What differs between copies.** The 4 target rows are bound by kind
  (`ROW_NOBIND`) and are byte-identical in all seven types. The other 8 differ
  only in `runtime.offset`, because `lfo` sits at a different place in each
  voice struct.

The rows are read as a plain C array (`&entry->descriptors[i]`) at **41 sites
in 6 files** (`InstrumentManager.c`, `presetManager.c`, `storageTypes.c`,
`Autosave.c`, plus the effect paths). `runtime.offset` is applied at 7 sites
in `InstrumentManager.c`. Two arrays cannot share rows in the middle in C, so
sharing needs one of these:

| Option | Saving | Cost and risk |
|--------|-------:|---------------|
| a. Row accessor: each type lists where its two LFO runs start; `instrumentManager_rowAt(entry, i)` returns a shared row inside those runs. The 8 bound rows hold an offset relative to `lfo`, plus a per-type `lfo` base | ≈ 1.9 KB (2 KB of rows less the accessor) | all 41 sites change. Every descriptor walk gets a range check per row, including the Morph passes and the automation drain in the main loop (`voiceControl_processPending()` runs inside the render loop). That is per-block CPU, so it falls under the S093 cost rule |
| b. Share only the 4 identical target rows (option a without offset relocation) | ≈ 0.6 KB | the same 41 sites, for a third of the saving |
| c. Reorder each table so the LFO rows are a common tail | ≈ 2 KB | changes every descriptor index: the type enums, page maps, the Scene image and AutoSave layout. That is a storage change; old AutoSave images would read wrong (no migration, by the standing rule) |
| d. Leave as is | 0 | — |

**Recommendation: d (leave it), or a only together with item 2.** Item 2
changes the same row struct, and both need the same equality check. a costs
CPU on paths the S089 ceiling covers. The user approved G as P0 described it;
this is a different job, so it is paused for a decision.

---

## 5. Smaller items: value tables, formatters, filesystem clones

### 5.1 Padding and enum fixes (P0 §4 I): nothing to take

P0 said `parameter_dtypes[]` uses 4-byte enum entries (−288 B available) and
that `scene_mod_targets` pads a 3-byte struct to 4 (−252 B). Both claims are
wrong. A size probe with the project's headers and flags shows:

- `sizeof(enum Datatypes)` is **1** (this toolchain packs enums), and
  `NUM_PARAMS` is **384**. `parameter_dtypes[]` is already 384 × 1 B. Its 315
  zero bytes are `DTYPE_0B255` (0), the most common dtype.
- `scene_mod_targets[]` (1,008 B) is `scene_mod_target_descriptor_t`: 24 B ×
  42, already packed (2 + 1 + 1 + 2 + 2 + 1 B, padded to 12, plus 3 pointers).
  `ModTarg`'s `modTargets[]` is 4 B.

No change was made. P0 §4 I is corrected.

### 5.2 Value-name table merges: not done (about 100 B)

| Overlap | Saving | Why not |
|---------|-------:|---------|
| `colFiltTypeNames` = `colRouteNames` without "str" | 32 B | entry 0 of each holds its own count (7 vs 8), checked by two `_Static_assert`s; sharing breaks the count |
| `stepRollRateNames[1..9]` = `perfBtnTypeNames[6..14]` | ≈ 26 B net | roll rates and overlay types change for different reasons; the lookup needs an offset rule |
| `sceneChangeDivNames` ⊂ `sceneQueueDivNames` | ≈ 30 B | not a prefix ("ens"/"enl" sit at different positions); needs an index map |
| `menuEffects_runShort` = `storage_effectRunModeTokens` | 20 B | the storage table is a file format in `storageTypes.c`; the menu would depend on it |

These are better done as part of item 2 / P0 §4 S (one label pool), if that
goes ahead.

### 5.3 Value formatters (P0 §4 J): done, −288 B

P0 counted six formatters. Reading them shows that
`menu_formatEffectAutomationValue3()` already delegates to
`menu_formatAutomationValue3()`, `crumpBit_uiFormatValue3()` holds
CrumpBit-specific labels, and `menu_cellDisplayValue()` returns a value, not
text. The real duplication is in three switches:

| Branch | `menu_formatCellValue3()` | `va_formatValue3()` | `menu_formatAutomationValue3()` |
|--------|:-:|:-:|:-:|
| `DTYPE_PM63`, `NOTE_NAME`, `MIX_FM`, `LFO_POLARITY`, tagged `ON_OFF` | same | same | same |
| `DTYPE_MENU` (fold wave, waveform, menu table) | same | same | own (clamps to the automation max) |
| untagged `ON_OFF` | `value == 1` | `value == 1` | `value != 0` |

Added before `va_formatValue3()` in `menu.c`:

- `menu_formatDtype3(dtype, value, out)` renders the five shared cases and
  returns 1, or returns 0 for anything else, including untagged on/off.
- `menu_formatMenuCell3(cell, value, out)` renders the shared `DTYPE_MENU`
  branch.

Each formatter calls `menu_formatDtype3()` where its switch began, after all of
its own special cases, so the earlier checks still win. Each keeps its own
untagged on/off rule and its other cases. The two cell formatters call
`menu_formatMenuCell3()`; the automation formatter keeps its own clamped menu
case. The S085/S087/S088/S089 notes from the removed branches are in the
helpers' comment. Both helpers are `noinline`, so LTO cannot copy them back
into each caller.

Equivalence is by construction: the helpers hold the removed branches
verbatim, apart from reading the full dtype from an argument instead of
`menu_cellDtype(cell)` / `descriptor->dtype`, which are the values the callers
pass. Loop gate: all loops exact. Flash: 729,976 → 729,688 (−288 B).
Device check: the on/off, ±63, note, mix/FM, polarity and menu cells, plus the
step-automation value column.

### 5.4 Filesystem clone families (P0 §4 K): two merged, −984 B

**Done:**

- **`filesystem_saveGlobals_tick()` and `filesystem_saveMidiLearn_tick()`.**
  With comments removed, the bodies differ in exactly five values: the stall
  tag ("StWr"/"MlWr"), the settings-revision snapshot at phase 0 (settings
  only), the temp and live file names, and the line generator. They are now one
  `filesystem_safeTextWrite_tick(const fs_safe_text_write_t *w)` with two
  `static const` descriptors (`fs_settings_safe_write`,
  `fs_midilearn_safe_write`). The two tick functions remain as one-line
  wrappers, so the dispatcher is unchanged. Phase numbers 0–10, the stall
  observer (site SETTINGS, 30,000 polls), the error codes and every afatfs call
  and argument are the same. The function is `noinline, noclone`; without
  `noclone` LTO's constant propagation could make two copies again. Image: one
  676 B function in place of two.
- **`filesystem_blockOpen()`, `filesystem_blockOpenLfn()`,
  `filesystem_blockOpenDirLfn()`.** These were one loop with a different afatfs
  open call (`afatfs_fopen` / `afatfs_fopen_lfn` / `afatfs_opendir_lfn`, the
  last two case-insensitive). They are now `filesystem_blockOpenMode(name, mode)`
  (`noinline, noclone`, 344 B) with the three names kept as inline wrappers, so
  the 25 call sites are unchanged. The open is retried and the callback waited
  for exactly as before. Image: 344 B in place of 3 × 212–216 B. These helpers
  are boot and sample-install only.

Flash: 730,960 → 729,976 (−984 B). Loop gate: all loops exact. Device check:
a settings change and a MIDI Learn are each saved and survive a power cycle,
and the boot directory trim, boot readers and `Load:[Samples]` still work.

**Not done:**

| Pair | Similarity | Why not |
|------|-----------:|---------|
| `filesystem_loadGlobals_tick` ~ `filesystem_loadMidiLearn_tick` | 76 % | real behaviour differences: line-count saturation, terminator tracking, what a parse error does, the trace records, the defaults reset. A merge needs a flag at each point, in boot-critical recovery code |
| `filesystem_scanBanks/Kits/Scenes/Patterns_tick` | 60–79 % | the same kind of differences in the browse scanners; low reward for the risk |
| `preset_loadKitForScenes` ~ `preset_loadSceneForScenes` | 95 % | different filesystem request and completion callback; the shared preamble is ≈ 40–60 B, and both are public API |

---

## 2. Instrument and effect labels as indexes (done, −3,032 B)

**The user's question, "would it interfere with the file keys?": no.** File
keys are a separate field (`file_key`). It stays a pointer to the same string
literal. Only the three *display* fields changed, and nothing written to or
read from a file changed. That covers Kit, Instrument and Effect files,
`midilrn.cfg`, `settings.cfg`, `.hcnames` and AutoSave, and also descriptor
indices, row order and every binding.

### 2.1 What a descriptor row held before

`ParamDescriptor` (28 B, `InstrumentManager.h`, `_Static_assert` on the size):

| Field | Size | Used for |
|-------|-----:|----------|
| `file_key` | 4 (pointer) | **storage**: the `key=value` lines of Kit, Instrument and Effect files (`storageTypes.c`), the MIDI Learn `midilrn.cfg` key for instrument and effect targets (`MidiLearn.c`), lookups by key |
| `short_name` | 4 (pointer) | display only: the 3-character cell label |
| `long_name` | 4 (pointer) | display only: the clicked-in long label |
| `category` | 4 (pointer) | display only: the category label |
| `dtype`, `flags`, `mod_domain`, `runtime` binding | 12 | value type, modulation contract, DSP writer |

`effect_param_descriptor_t` embeds it (36 B per row).

### 2.2 What was done

**Pools in one place.** `shortNames[]`, `catNames[]` and `longNames[]` were
`static` tables in `MenuText.h`. That header is included by ≈ 30 files, so
reading them outside `menu.c` would have duplicated them. They are now defined
once in the new **`Core/Menu/MenuLabels.c`** (in `SRCS`), moved with their
comments. `MenuText.h` keeps `extern` declarations and three inline accessors:
`menuLabel_short(id)`, `menuLabel_cat(id)`, `menuLabel_long(id)`.

**Descriptor labels added to the pools.** Every instrument and effect label
already present in a pool reuses its existing id (`CAT_FILTER`, `LONG_FREQ`,
`SHORT_FREQ`, …). The rest are appended after the existing entries, so every
static-cell index (`valueNames`) is unchanged. Each enum in `menu.h`
(`shortNamesEnum`, `catNamesEnum`, `longNamesEnum`) gained the same names in
the same order plus a count (`SHORT_LABEL_COUNT`, `CAT_LABEL_COUNT`,
`LONG_LABEL_COUNT`). `MenuLabels.c` asserts at compile time that each table
matches its enum and that every count is ≤ 256.

| Pool | Before | Reused | Added | After | Width |
|------|-------:|-------:|------:|------:|------:|
| `shortNames` | 101 | 31 | 123 | 224 | 4 |
| `catNames` | 30 | 12 | 18 | 48 | 9 |
| `longNames` | 102 | 31 | 76 | 178 | **10** (was 9) |

New ids are the label text in the existing style (`LONG_CUTOFF`,
`SHORT_TUA`, `CAT_AMP_EG`, …). Four were renamed where the generated name
would have read like an existing one with a different text: `CAT_PITCH_ENV`
("Pitch EG"; the existing `CAT_PITCH_EG` is "Mod EG"), `LONG_DRIVE_WORD`
("Drive"; `LONG_DRIVE` is "Overdriv"), `LONG_PAN_WORD` ("Pan"; `LONG_PAN` is
"Panning") and `SHORT_E_TO_R` ("e>r").

**`longNames` is 10 wide, not 9.** Two descriptor long names have 9 characters
("DstVoice2", "DstParam2"). The step-automation target view prints category +
long name up to 14 characters, so the 9th character can reach the LCD. They are
kept whole rather than proving that view never shows them (+178 B).

**The row.** `short_name`, `long_name`, `category` → `uint8_t short_id`,
`long_id`, `cat_id`, in the same positions. `ParamDescriptor` is 20 B (the
assert now reads 20) and `effect_param_descriptor_t` 28 B. The row macros'
bodies are unchanged (positional), so the host test that reuses
`ROW_SELECT_SPECIAL` with its own struct still builds.

**Rows.** A script replaced the three label strings of every `ROW…(…)` and
`EFFECT_ROW…(…)` call with the ids: 325 instrument rows in the seven
`*Parameters.c` files, and 66 effect row calls (CrumpBit 22, Space 41, and the
three `EFFECT_COMMON_ROWS` in `EffectParamRows.h`), which expand to 78 rows.
For example:

    ROW_SPECIAL("filter_freq", CAT_FILTER, LONG_FREQ, SHORT_FREQ, DTYPE_0B127, ...)
    EFFECT_ROW("effect_level", CAT_EFFECT, LONG_LEVEL, SHORT_VOL, DTYPE_0B127, ...)

A misspelt id is now a build error, not a wrong label.

**Readers** (found by the compiler, changed line by line):

- `menu.c`, 12 lines: `menu_formatInstrumentTargetShort()`,
  `menu_displayInstrumentTargetFull()`, `menu_instrumentCellLabels()`,
  `menu_repaintStepAutomation()` (target name and target short label) and
  `menu_repaintGeneric()` (clicked-in category/long label, 4-cell overview).
- `menuEffects.c`, 1 line (`menuEffects_shortName()`); `EffectsManager.c`,
  3 lines (`effects_rowLabels()`).

Untouched:

- **Scene mod-targets** (`scene_mod_target_descriptor_t`, `SceneModTargets.c`)
  keep their pointer labels. Their `short_name` is the `midilrn.cfg` key.
- **CrumpBit's per-sub-type label overrides** (`crumpBit_labels`, its own
  pointer struct). Effect labels still pass through the type's label hook
  as before.

### 2.3 Checks

- **Labels, all 403 rows** (325 instrument, 78 effect, 12 tables): a script
  decodes each row's `cat_id`/`long_id`/`short_id` through the new ELF's pools
  and compares (table, row, file key, category, long, short) with the
  reference ELF's pointer-decoded rows. **0 mismatches.**
- **Every other field, all 403 rows:** file key, dtype, flags, modulation
  domain, binding (kind, offset, type, special) and, for effects, default,
  max, effect flags and `expand7` (compared by symbol name): **0 differences.**
- **Static pools:** the first 101/30/102 entries of the new pools equal the
  old tables entry for entry, so every static label is where it was.
- **Loop gate:** 86/86 oscillator loops exact; 77 oscillator and 58 mixer hot
  loops equal.
- **Host tests:** `instrument_manager` (with its three break checks),
  `special_tags` (325 rows, 0 mismatches) and the `make_init_alk/col/tkt`
  table checks all pass. The LXR-1 converter parses only the row keys, so it is
  unaffected.
- **Build:** no new warnings.

### 2.4 Size: why −3,032 B, not the ≈ −4.4 KB estimate

| Part | Before | After | Δ |
|------|-------:|------:|--:|
| 12 descriptor tables (rows 28 → 20 B, 36 → 28 B) | 11,908 | 8,684 | −3,224 |
| the three pools | 1,592 | 3,108 | +1,516 |
| descriptor label literals removed, code | — | — | ≈ −1,324 |
| **net** | 729,688 | 726,656 | **−3,032** |

The estimate counted the duplicated text twice. The labels only in descriptors
(217 of them) still have to be stored somewhere; they are now in the pools,
padded to the pool width, instead of as literals. Only the 74 labels that the
pools already had are saved outright.

### 2.5 What this enables

- **P0 §4 S (semantic label merges)** is now cheap. A label is chosen per row
  by id, so "Frequncy" → "Cutoff" on the filter rows is a one-word edit per
  row, and merging two pool entries (for example `LONG_RESONANCE`/`LONG_RESO`)
  frees a pool slot. The §2.3 check, with the chosen renames as the only
  allowed differences, proves it.
- **§3 option a** (shared LFO rows) would now share 20 B rows, not 28 B.

---

## 6. Summary of changes

**Result:** 734,160 → 726,656 B (**−7,504 B**; headroom 27,008 B). ITCM is
unchanged at 15,960 B. Every step passed the loop gate with all loops exact
against the reference image.

| File | Change | Item |
|------|--------|------|
| `Core/Menu/menuPages.h` | `menuStaticPages[6][8]` + `menuStaticPageSlot[NUM_PAGES]` replace `menuPages[17][8]` | 1 |
| `Core/Menu/menu.c` | `menu_pageRow()` + shared zero row; 4 reads converted | 1 |
| `Core/Menu/menu.c` | `menu_formatDtype3()`, `menu_formatMenuCell3()`; three formatter switches call them | 5 J |
| `Core/Menu/menu.h` | `menuPages` extern removed; `EFFECT_PAGE` comment | 1 |
| `Core/Menu/MenuText.h` | `longNames`/`catNames` `[9]`, `midiFilterNames`/`filterTypes` `[4]`; "GateMode", "CPU use", "PtrnStor" | 4 |
| `Core/Hardware/SD/filesystem.c` | `filesystem_safeTextWrite_tick()` + two descriptors; `filesystem_blockOpenMode()` | 5 K |
| `Core/Menu/MenuLabels.c` (new) | the three label pools, moved from `MenuText.h`, plus 217 descriptor labels; count asserts | 2 |
| `Core/Menu/MenuText.h` | pools → `extern` + `menuLabel_short/cat/long()` | 2 |
| `Core/Menu/menu.h` | label enums extended, `*_LABEL_COUNT` | 2 |
| `Core/DSP/Instruments/InstrumentManager.h` | `short_id`/`long_id`/`cat_id`; size assert 20 | 2 |
| seven `*Parameters.c`, `CrumpBitParameters.c`, `SpaceParameters.c`, `EffectParamRows.h` | row labels as ids (391 calls) | 2 |
| `Core/Menu/menu.c`, `menuEffects.c`, `Core/DSP/Effects/EffectsManager.c` | 16 reader lines use the accessors | 2 |
| `Makefile` | `MenuLabels.c` in `SRCS` | 2 |
| `README.md` | `menuPages.h` line; `MenuText.h`/`MenuLabels.c` lines in the tree | 1, 2 |

**Needs the device** (nothing here can be checked on the host):

1. MENU pages (all four Global sub-pages, including the compressor page and its
   `+`/`^`/`*`/`<` scroll cues), PERF (both sub-pages), STEP track settings and
   step specials, Euklid, SHIFT+PLAY+SEQ chain editor, SHIFT+REC settings (both
   sub-pages): every cell shows and edits as before.
2. Clicked-in labels: the gate mode cell reads "GateMode"; the CPU and
   Pattern-store widgets read "CPU use" and "PtrnStor".
3. Value text: on/off and tagged on/off (`run`, `chn`, Colloid route/filter),
   ±63 (pan, fine), note names, `mod` (fm/mix/snc/rng), LFO polarity, wave and
   menu cells, and the step-automation value column for a voice and an Effect
   target.
4. Change a Global (`settings.cfg`) and make a MIDI Learn (`midilrn.cfg`); power
   cycle; both are kept. Boot, the directory trim and `Load:[Samples]` work.
5. Item 2: on one voice page of each instrument type, and on the CrumpBit and
   Space Effect pages, the 3-character labels, the clicked-in category and long
   name, an LFO destination and a step-automation target name read as before.
   Save and reload a Kit and an Effect, and use an instrument MIDI Learn (file
   keys unchanged).

## 7. Statements now out of date (for the user; not edited, per the S093 rule)

- `knowledge_files/specification_reference/MENU_UI_SPEC.md:32–33`: "The static
  page table is `menuPages[NUM_PAGES][NUM_SUB_PAGES]`". It is now
  `menuStaticPages[]` + `menuStaticPageSlot[]`, read through `menu_pageRow()`.
  Line 131 ("static `menuPages` table") likewise.
- `knowledge_files/specification_reference/DEV_MODES.md:131`:
  `menuPages[MENU_MIDI_PAGE][MENU_GLOBAL_SCENE_SUBPAGE]` →
  `menu_pageRow(MENU_MIDI_PAGE, MENU_GLOBAL_SCENE_SUBPAGE)`.
- `knowledge_files/specification_reference/DEV_MODES.md:633–634`: the Settings
  and MIDI Learn write stall rows name `filesystem_saveGlobals_tick()` /
  `filesystem_saveMidiLearn_tick()`. The observer now lives in
  `filesystem_safeTextWrite_tick()`; codes, thresholds and the site are unchanged.
- `knowledge_files/specification_reference/REPOSITORY_LAYOUT.md:93`:
  "menuPages.h ← 16-page × 8-subpage table".
- `knowledge_files/specification_reference/OSC_INTERP_AUDIT.md:66`: names
  `filesystem_saveGlobals_tick()` as the writer (still true through the wrapper).
- `knowledge_files/specification_reference/dsp_instruments_effects/INSTRUMENTS_DSP_REFERENCE.md:248`
  (`short_name`, `long_name`, `category` fields) and `:259`
  (`sizeof(ParamDescriptor) == 28`): now `short_id`/`long_id`/`cat_id`
  (`uint8_t` pool ids) and 20 B. The add-a-type checklist should say that a
  new label needs an id in `menu.h` and an entry in `MenuLabels.c`.
- `knowledge_files/specification_reference/dsp_instruments_effects/EFFECTS_BUS_REFERENCE.md:87, 1002`:
  `EFFECT_ROW(...)` arguments 2–4 are now label ids.
- `knowledge_files/specification_reference/FILESYSTEM_SPEC.md:2318–2319`:
  "`short_name`" / "`category` and `long_name`" (now via the ids).
- `knowledge_files/specification_reference/REPOSITORY_LAYOUT.md:94`:
  "MenuText.h ← all label strings"; the pools are in `MenuLabels.c`.
- `manual/chapters/05_operation_modes.md:375`: "Trigger Gate Mode" (the LCD now shows "GateMode"; the manual source was not edited).
