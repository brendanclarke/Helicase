# S103 P7 — One file type for Basic and Advanced Colloid / Alkaline (implementation plan)

**Date:** 2026-11-25. **Branch:** `dev-ph7-alkaline` (the S103 P6 tree,
uncommitted). **Status:** implementation plan, **revision 2. Not
implemented**: no source was changed (your instruction, 2026-11-25).

**History:**
- **revision 1 (2026-11-24):** the design, implementation issues I1–I7 and
  questions Q1–Q5.
- **revision 2 (2026-11-25):**
  - your answers: Q1 yes; Q2–Q5 as recommended;
  - a new requirement, R-HOLD: a Basic Colloid / Alkaline keeps the effect
    rows it loads, without showing, changing or using them, so a Basic load
    and re-save of an Advanced file does not overwrite them;
  - R-HOLD replaces revision 1's "a Basic save writes the effect as off";
  - two new issues (I8, I9) follow from it.

**Your requests:**
- **(2026-11-24):** Colloid and Alkaline each get a single library folder and
  a single file type for both their Basic and Advanced types, with the
  extensions `.col` and `.alk`. A Basic instrument ignores the effect slot
  when loading; otherwise both load and save the Advanced file.
- **(2026-11-25, R-HOLD):** "Colloid and Alkaline Basic should retain, but
  not display, modify, or use the effect row items so that they are not
  overwritten if it loads and re-saves an Advanced item."

**Labels:**
- **Code:** read from the tree, with a file and line (this tree, 2026-11-25).
- **Est.:** an estimate.

**Binding rules that apply** (`MEMORY.md`):
- **no backward compatibility, ever:** no migration, aliasing or converters;
- **the type registry is the storage schema;**
- **a slot's type only changes by loading a file** (S093 P4 F1);
- Load/Save never freezes, and runtime SD work stays asynchronous;
- **Names in RAM (S090 P1):** reuse only through the documented domains;
- **AutoSave (S074, S090 P2):** validators probe exact lengths; no wait on a
  drain;
- every code change carries an adjacent comment block (your standing
  instruction).

No step here edits `MEMORY.md` or `knowledge_files/` (§10 lists what goes
out of date).

---

## 0. Decisions

| # | Decision |
|---|---|
| **D1 (Q1)** | Two Load / Save type entries per family remain (**Colloid** / **ColAdv**, **Alkaline** / **AlkAdv**), both over one folder. The entry you load from sets the slot's class; the Advanced cap stays where it is |
| **D2 (Q2)** | The file header carries the family token (`type=col` / `type=alk`) for both classes. A header `clx` / `alx` is refused (the no-compatibility rule) |
| **D3 (Q3)** | The shipped `init.col` / `init.alk` carry the effect rows as `init.clx` / `init.alx` have them today: Juno I on (`fx_type` 1), rate 40, depth 0, mix 64 |
| **D4 (Q4, revised by R-HOLD)** | A Basic slot's held effect rows start from a fixed seed when nothing supplied them (a reset slot): `fx_type` 0 (off), `fx_rate` 40, `fx_depth` 0, `fx_mix` 64. A Basic **save** writes the **held** values, not a fixed "off" (R-HOLD) |
| **D5 (Q5)** | Kits / Scenes / Banks holding a ColAdv or AlkAdv slot saved since S099 stop loading (I7). Accepted under the standing rule; no converter |
| **D6 (R-HOLD)** | A Basic slot **holds** the four effect cells. It loads them from the file, keeps them through AutoSave, the Instrument-Load undo snapshot and the load-time Morph fallback, and writes them back on every save. It **never shows, changes or applies** them: every Menu, MIDI, modulation, automation, Morph and runtime path stays bounded by the Basic type's own row count, as today |

---

## 1. Summary

| Item | Plan |
|---|---|
| **What merges** | **Storage only.** `clx` / `alx` keep their runtime types (Advanced class, the two-per-Scene cap, the effect), but read and write the Basic type's folder, extension and header: `Instrument/Colloid/*.col`, `Instrument/Alkaline/*.alk` |
| **What stays distinct** | The slot's type token in `kitset.kcg`, HCNAMES, AutoSave and MIDI Learn: `col` / `clx`, `alk` / `alx` |
| **The key idea** | Two row counts per type. `descriptor_count` (unchanged) bounds everything a user or the DSP touches. A new **storage row count**, the family's widest table, bounds what storage carries: 63 for both Colloid types and 35 for both Alkaline types. The Advanced types' two counts are equal, so their behaviour is unchanged. Basic and Advanced already share one descriptor array (`colloid_param_descriptors`, `alkaline_param_descriptors`), so the descriptors of the held rows exist for a Basic type too |
| **Load** | Both classes store all 63 (35) rows into the slot image. A Basic slot then applies only its own 59 (31) |
| **Save** | Both classes write all 63 (35) rows from the slot image. A Basic slot's held rows are written back unchanged |
| **CPU / RAM** | 0 DSP; 0 RAM. Flash: one family table (11 B) and code, est. < 1 KB |
| **Issues** | Nine (§3). Two are new with R-HOLD: the storage paths that would drop held cells (I8), and stale AutoSave bytes on the first boot after the update (I9) |

---

## 2. What exists (code)

### 2.1 The registry: folder, extension, token, rows

`instrument_registry[]` (`InstrumentManager.c:60–152`;
`instrument_registry_entry_t`, `InstrumentManager.h:495–524`):

| Type | Token | Extension | Folder | `descriptor_count` | Table |
|---|---|---|---|---:|---|
| `INSTRUMENT_TYPE_COL` | `col` | `.col` | `Colloid` | 59 | `colloid_param_descriptors` |
| `INSTRUMENT_TYPE_CLX` | `clx` | `.clx` | `ColAdv` | 63 | the same array (`fx_type fx_rate fx_depth fx_mix` at 59..62) |
| `INSTRUMENT_TYPE_ALK` | `alk` | `.alk` | `Alkaline` | 31 | `alkaline_param_descriptors` |
| `INSTRUMENT_TYPE_ALX` | `alx` | `.alx` | `AlkAdv` | 35 | the same array (the four at 31..34) |

- The `clx` row is at `InstrumentManager.c:120–124`, the `alx` row at
  `:131–135`.
- The effect rows follow the `@advanced-rows` marker
  (`ColloidParameters.c:395`, `AlkalineParameters.c:313`).
- `fx_type` is `ROW_SELECT_FIXED_SPECIAL` (not morphable); `fx_rate`,
  `fx_depth` and `fx_mix` are `ROW` (morphable).
- `ADV_FX_OFF` is 0 (`InstrumentManager.h:343`).
- The engine map: `instrument_engine_of[]`, `InstrumentManager.c:170–178`.

**The token has two roles:**
- **the slot's type:**
  - `kitset.kcg` `type=` (`storageTypes.c:2128`);
  - HCNAMES Instrument rows (`filesystem.c:6800–6850`);
  - AutoSave (`Autosave.c:1361`, `:1775`);
  - MIDI Learn (`MidiLearn.c:508`, `:621`);
- **the file's header:** written at `storageTypes.c:1094`, checked against
  the expected type at `:2285–2288`.

### 2.2 Folder and extension

- **The folder** always comes from `instrumentManager_storageDirectory()`
  (`filesystem.c:5099`, `:17625`, `:18039`, `:32407`, `:35151`).
- **The extension** comes from `instrumentManager_filenameMatchesType()` /
  `storage_instrumentTypeExtension()`. That includes the Kit member leaf
  (`storage_makeSavedInstrumentDisplayFilename()`, `storageTypes.c:879`).
- **The Kit check:** `storage_kitsetFinalize()` (`storageTypes.c:2200–2217`)
  rejects a whole Kit whose member's extension does not match its type; the
  Kit shows "-".

### 2.3 Loading

`storage_instrumentParseLine()` maps each key through the expected type's
own rows (`instrumentManager_descriptorIndexByKey()`,
`InstrumentManager.c:958`) and skips a key it does not find
(`storageTypes.c:2296–2301`). A Basic slot therefore drops `fx_*` today.

- **No `[morph]` section:** `storage_instrumentCopyMainToMorphFallback()`
  (`storageTypes.c:2395–2408`) copies `[params]` into Morph for the type's
  own morphable rows.
- **Before parsing,** the staging slot is reset through
  `instrumentManager_resetSlot()` (`InstrumentManager.c:1154`). It
  `memset`s all 64 cells to 0, then seeds the target-selector rows.

### 2.4 Saving

`storage_formatInstrumentLineView()` (`storageTypes.c:1052`, its row walks at
`:1102`, `:1136`) is the one writer. It serves root Instrument Save, Kit
member files and InstrumentMrp Save (`filesystem.c:18653`, `:35670`), and
writes the type's own rows. A Basic save writes no `fx_*` today.

### 2.5 AutoSave

- **The record:** 72 bytes per endpoint (`AUTOSAVE_INSTRUMENT_PARAMETER_BYTES`),
  so the 63 / 35 rows fit.
- **The type's own rows bound it everywhere:**
  - `autosave_getLivePayloadByte()` reports cells at or past
    `descriptor_count` as nonexistent (`Autosave.c:1395–1430`);
  - `autosave_applyInstrumentPayload()` (`:1752`) copies `0..count-1`
    (`:1787`);
  - `autosave_instrumentMarkerBase()` (`:1994`, bound at `:2012`);
  - the whole-instrument dirty marks (`:2192`, `:2223`);
  - `autosave_markWholeInstrumentDirty()`'s counts (`:2230`, `:2263`,
    `:2284`).
- **The writer** patches only bytes the getter reports
  (`filesystem.c`, the AutoSave patch loop). A byte reported nonexistent is
  never written, so it keeps whatever the record held before (I9).

### 2.6 The Instrument-Load undo snapshot

- `pm_instrument_snapshot` (`presetManager.c:2976–2984`, 129 B) holds all 64
  cells of both endpoints.
- **Its restores copy only the type's own rows:**
  - `preset_restoreInstrumentSnapshot()`: reset, then `0..count-1`
    (`:3076–3090`);
  - `preset_restoreInstrumentMorphSnapshot()` (`:3146–3156`).

### 2.7 What keeps a Basic slot from showing, changing or using a row

Every lookup and write is bounded by `descriptor_count`:
- `instrumentManager_descriptor()` (`InstrumentManager.c:921–931`);
- `instrumentManager_descriptorIndexByKey()` (`:958`);
- `instrumentManager_menuDescriptorIndex()` (`:1016`, the Menu);
- `instrumentManager_stepTargetForSlot()` (`:1789`, automation);
- `instrumentManager_deltaOverlaySet()` (`:5704`, Learn deltas);
- `instrumentManager_writeRuntimeInternal()` (`:5795`);
- the Scene apply loops (`presetManager.c:1856`, `:1929`);
- the Basic pages list no effect cell (`ColloidParameters.c` /
  `AlkalineParameters.c` page tables).

The effect itself is applied only for `clx` / `alx`:
`instrumentManager_writeFxType()` switches on the slot type and returns 0
otherwise (`InstrumentManager.c:5118–5139`).

The user-side image copies are also bounded by the count:
- Normal → Morph (`presetManager.c:2494`, `:5495`);
- reset Morph to Normal (`:5437`).

### 2.8 The browser cache and `.hcindex`

- **One shared name cache** with one type tag (`fs_list_cache_type`,
  `filesystem.c:1952`).
- **`.hcindex`** rows are stems only, with no type
  (`filesystem_validateInstrumentIndexRow()`, `:11915–11945`).
- **Scans classify a file by extension to the first matching registry row**
  (`filesystem_instrumentTypeFromFilename()`, `:11678–11697`).
  - The record path (`filesystem_recordInstrumentFile()`, `:11991`) and the
    post-save refresh (`filesystem_updateInstrumentCacheAfterSave()`,
    `:12412`) clear and re-tag the cache when that type differs
    (`:12037–12041`, `:12460–12464`).
- **Registry-wide loops**, each treating a row's folder as its own:
  - the index rebuild (`:4796`, `:23689`);
  - the typed scans (`:29895`);
  - the name repair (`:30778`, `:30852`).
- **The boot directory trim** (`filesystem_trimInstrumentTreeBlocking()`,
  `:24306`) walks the card's real subfolders, not the registry.

---

## 3. Implementation issues

| # | Issue | Effect if not handled | Resolution |
|---|---|---|---|
| **I1** | Two runtime types must remain: a slot's type only changes by loading a file | No way to make a slot Advanced | D1 |
| **I2** | The extension classifier picks the first row: every `.col` classifies as `col` | Opening or saving from ColAdv clears and re-tags the cache; the ColAdv browser counts **0 files** (`filesystem_cachedInstrumentCount()`, `filesystem.c:11653–11657`) | C9 |
| **I3** | Registry-wide loops would process the shared folder twice | Twice the boot SD work for those folders; a second, identical `.hcindex` write | C10 |
| **I4** | The header: `type=clx` in a `.col` file, and the strict equality check | Basic and Advanced could not read each other's files | D2, C3 |
| **I5** | A Basic save writes no effect rows; a Basic load drops them | A Basic load and re-save of an Advanced file would lose its effect (what R-HOLD forbids) | D6: the storage row count (C2, C4–C8) |
| **I6** | One stem is one file now (`ColAdv/foo.clx` and `Colloid/foo.col` were two) | A same-named save from either class overwrites the other's file | Intended. With R-HOLD a Basic re-save keeps the effect it loaded |
| **I7** | No backward compatibility: `.clx` / `.alx` are no longer recognised | (a) `ColAdv/` / `AlkAdv/` files disappear from the browser. (b) **Every Kit / Scene / Bank with a ColAdv or AlkAdv slot saved since S099 stops loading** (its member `name<n>.clx` fails `storage_kitsetFinalize()`). (c) HCNAMES rows naming such files no longer resolve at boot | D5; card preparation (§7). The shipped card image has none apart from `init.clx` / `init.alx` (checked) |
| **I8 (R-HOLD)** | **The storage paths that would drop held cells.** Four of them stop at `descriptor_count` or zero the cells: `resetSlot()` (`memset`), AutoSave (getter, apply, marks), the snapshot restores, and the load-time Morph fallback | Held values would vanish: at the next power cycle (AutoSave), at an Instrument-Load undo (snapshot), or in Morph for a file without `[morph]` | Each widened to the storage row count (C4–C8); `resetSlot()` seeds D4's values (C5) |
| **I9 (R-HOLD)** | **Stale AutoSave bytes on the first boot after the update.** Today the writer never patches a Basic slot's cells 59..62 (31..34), so the record holds whatever an earlier type in that slot left there | After the update, the first AutoSave restore reads those bytes as held effect values. They are invisible and unused on the Basic slot, but a Basic save would write them, and a ColAdv load of that file would get them (clamped to each row's range by the parser) | Accepted under the no-compatibility rule. Card preparation (§7 step 5): reload Basic Colloid / Alkaline slots from their files once, or clear AutoSave. From the first mark after the update, the record holds the slot's real held values (C6 marks them) |

**No issue found in:**
- the `.hcindex` format;
- MIDI Learn;
- HCNAMES (the slot token);
- the Advanced cap;
- `.hctmp.<ext>`;
- the DSP;
- every type outside the two families (its two counts are equal).

---

## 4. Design

### 4.1 The file family (C1)

`instrument_file_family_of[]` beside the engine map
(`InstrumentManager.c:170`). Each type is its own family, except:
- `clx → col`;
- `alx → alk`.

It is read through `instrumentManager_typeFileFamily(type)`. A separate
table from the engine map, because "the same engine" and "the same file"
are different promises.

### 4.2 The storage row count (C2)

`instrumentManager_storageRowCount(type)`: the largest `descriptor_count`
among the members of the type's family. It is 63 for `col` and `clx`, 35 for
`alk` and `alx`, and every other type's own count.
- **Valid only because** a family's members share one descriptor array.
  The registry self-check asserts this (C2).
- **Which bound is used where:**
  - **`descriptor_count`:** what is shown, changed or used (§2.7, unchanged);
  - **`storageRowCount()`:** what is stored, restored and written (§4.4–4.7).
- A **held row** is a row in `descriptor_count .. storageRowCount − 1`; only
  a Basic type has any.

### 4.3 Folder, extension, header (C2, C3)

- The `clx` / `alx` registry rows take the family's extension and folder
  (`.col` / `Colloid`, `.alk` / `Alkaline`).
- The header writer emits the family token; the header check compares
  families (D2).
- `storage_instrumentTypeToText()` keeps returning the slot token for Kit,
  HCNAMES, AutoSave and Learn.

### 4.4 Load (C4)

- **The key lookup:** a storage-scope variant of
  `instrumentManager_descriptorIndexByKey()` that searches
  `0..storageRowCount − 1`. A Basic slot then finds `fx_*` and stores them
  in both endpoints, clamped to the row's range like any other cell.
- **The staging slot only:** nothing is applied here; the later apply walks
  `descriptor_count`, so the held rows never reach the voice.
- **No `[morph]` section:** the Morph fallback (C7) copies the held morphable
  rows too.
- **A file without `fx_*`** (none after this change, except hand-made files)
  leaves the seed of D4 (C5).

### 4.5 Reset (C5)

`instrumentManager_resetSlot()` seeds the held rows of a Basic family member
with D4's values: normal `0 / 40 / 0 / 64`; Morph `40 / 0 / 64` for the
three morphable rows; interpolation the same.
- An Advanced reset keeps zeros as today, because its load supplies the rows.
- The seed table is in the family's parameter file next to the
  `@advanced-rows` marker: `colloid_fx_hold_seed[4]`,
  `alkaline_fx_hold_seed[4]`, exported through the registry module.

### 4.6 Save (C4)

`storage_formatInstrumentLineView()` walks `0..storageRowCount − 1`:
- **`[params]`:** each cell;
- **`[morph]`:** the morphable rows, with the Morph-Save projection only for
  rows the type itself owns. A held row writes its Morph cell as stored: a
  Basic slot does not use it, so it must not interpolate it (D6).

One writer serves Instrument Save, Kit members and InstrumentMrp Save.

### 4.7 AutoSave (C6) and the snapshot (C7)

- **AutoSave:** the payload getter, the apply, the marker base and the three
  whole-instrument loops use `storageRowCount()` and the shared array's
  flags. An Advanced type is unchanged.
- **The snapshot restores** use `storageRowCount()`: an Instrument-Load undo
  returns the held cells with the rest.

### 4.8 What stays bounded by `descriptor_count` (D6, no change)

- The Menu, the pages and every editor;
- MIDI CC / NRPN and Learn;
- LFO / velocity targets and step automation;
- Morph, and the Normal ↔ Morph user copies (`presetManager.c:2494`,
  `:5437`, `:5495`): a Basic slot's user actions never touch a held cell;
- the Scene apply;
- `instrumentManager_writeFxType()` (type-gated).

### 4.9 Browser, scans, boot (C9, C10)

- **The classifier (C9):** the record and post-save paths classify against
  the active cache's type first, when the filename matches it.
- **The registry-wide loops (C10)** skip a row whose family is another row.
  A per-type request for `clx` / `alx` still runs on the family's folder.

### 4.10 Card image and generators (C11, C12)

- `make_init_col.py` / `make_init_alk.py` write one file each: `init.col`
  with 63 rows and `init.alk` with 35, the header `col` / `alk`, the effect as
  D3. The `OUT_ADV` jobs go.
- `SD_CARD/Instrument/ColAdv/` and `AlkAdv/` are removed.
- `check_special_tags.py` keeps its `CLX` / `ALX` tables; the `special_tags`
  target drops the two removed files.

---

## 5. Change list (every edit with its adjacent comment block)

| # | File, place | Change |
|---|---|---|
| **C1** | `InstrumentManager.c` after `instrument_engine_of[]` (`:170`); `InstrumentManager.h` | `instrument_file_family_of[]`; `instrumentManager_typeFileFamily()` with its contract |
| **C2** | `InstrumentManager.c` / `.h` | `instrumentManager_storageRowCount()`; registry rows `[INSTRUMENT_TYPE_CLX]` (`:120–124`) and `[INSTRUMENT_TYPE_ALX]` (`:131–135`) take the family's extension and folder (their stale row-count comments corrected); the registry self-check (`instrumentManager_specialTagSelfCheck()`, `:5642`, or the registry check) asserts that each family's members share one descriptor array and that the family member is itself its own family |
| **C3** | `storageTypes.c` header writer (`:1094`) and header check (`:2285–2288`) | The family token; the family comparison (D2) |
| **C4** | `storageTypes.c` `storage_instrumentParseLine()` (`:2296`) and `storage_formatInstrumentLineView()` (`:1102`, `:1136`); `InstrumentManager.c` | `instrumentManager_storageDescriptorIndexByKey()` (new; the storage-scope lookup) used by the parser; the writer's two row walks to `storageRowCount()`, with the held-row Morph rule of §4.6 |
| **C5** | `InstrumentManager.c` `instrumentManager_resetSlot()` (`:1154`); `ColloidParameters.c/.h`, `AlkalineParameters.c/.h` | D4's seed for the held rows; the two seed tables beside `@advanced-rows` |
| **C6** | `Autosave.c` `:1395–1430` (getter), `:1787` (apply), `:2012` (marker base), `:2192`, `:2223` (whole-instrument dirty marks), `:2263`, `:2284` (the whole-instrument counts) | `storageRowCount()` and the shared array's flags in each |
| **C7** | `presetManager.c` `preset_restoreInstrumentSnapshot()` (`:3082`), `preset_restoreInstrumentMorphSnapshot()` (`:3151`); `storageTypes.c` `storage_instrumentCopyMainToMorphFallback()` (`:2401`) | `storageRowCount()` (I8) |
| **C8** | `presetManager.c:2494`, `:5437`, `:5495`; Menu; MIDI; Morph | **No change**, with one comment beside each of the three `presetManager.c` loops: why they stay at `descriptor_count` (D6, held cells are never user-modified) |
| **C9** | `filesystem.c` `filesystem_recordInstrumentFile()` (`:11991`), `filesystem_updateInstrumentCacheAfterSave()` (`:12412`) | Classify against `fs_list_cache_type` first (I2) |
| **C10** | `filesystem.c` `:4796`, `:23689`, `:29895`, `:30852` | Skip a row whose family is another row (I3) |
| **C11** | `tools/colloid/make_init_col.py`, `tools/alkaline/make_init_alk.py`; `SD_CARD/Instrument/` | §4.10 |
| **C12** | `tools/dsp_test/Makefile` `special_tags`; `check_special_tags.py` | §4.10 |
| **C13** | Host laws: a new `tools/dsp_test/modules/storage_family.c` and Makefile target. The functions under test are pulled one by one with `extract.py --func` (the `instrument_manager` target's method, `tools/dsp_test/Makefile`): the registry accessors and `instrumentManager_resetSlot()`, `storage_instrumentParseLine()` and `storage_formatInstrumentLineView()`, `autosave_getLivePayloadByte()`'s instrument part and `autosave_applyInstrumentPayload()`, the two snapshot restores. The registry and the Colloid / Alkaline descriptor tables are compiled in; the rest is stubbed | §6 |

**Not changed:** the Menu; HCNAMES; MIDI Learn; the DSP; every type outside
the two families.

---

## 6. Laws (C13)

| Law | What |
|---|---|
| **SF-FAM** | `typeFileFamily`: `clx → col`, `alx → alk`, every other type itself. `storageDirectory` and the extension equal within a family. `storageRowCount`: 63 / 63 / 35 / 35 for `col clx alk alx`, `descriptor_count` for every other type. The shared-array assertion holds |
| **SF-HDR** | A `clx` slot's header is `type=col`. Parsing `type=col` with expected `clx` passes; `type=clx` with expected `col` or `clx` fails; `type=alk` with expected `col` fails |
| **SF-HOLD-LOAD** | An Advanced file (effect `2 / 77 / 12 / 99`, Morph rate 50) parsed into a `col` slot: cells 59..62 hold exactly those values, the Morph cells hold 50 (and the fallback case), every other cell as for a `clx` parse |
| **SF-HOLD-SAVE** | That `col` slot saved: 63 `[params]` rows, the last four `fx_type=2 fx_rate=77 fx_depth=12 fx_mix=99`, and `fx_rate=50` in `[morph]`. A Morph Save at any amount writes the held Morph cells unchanged. A byte-for-byte round trip (load, save, load, save) is stable |
| **SF-SEED** | `resetSlot(col)`: 59..62 = `0 / 40 / 0 / 64`, the Morph cells `40 / 0 / 64`; `resetSlot(clx)`: zeros as today. The same for Alkaline at 31..34 |
| **SF-HOLD-USE** | On a `col` slot holding `fx_type` 2: `instrumentManager_descriptor(col, 59)` is NULL; a write through `instrumentManager_writeRuntimeInternal()` to local 59 is refused; `instrumentManager_writeFxType()` returns 0; the Basic Normal → Morph copy leaves Morph 59..62 unchanged |
| **SF-HOLD-AS** | AutoSave: the getter reports cells 59..62 of a `col` slot (and the three Morph cells); the apply restores them; `autosave_markWholeInstrumentDirty()` marks them |
| **SF-HOLD-SNAP** | Both snapshot restores return the held cells |
| **SF-KIT** | `kitset.kcg` `type=clx file=name1.col` finalises OK; `file=name1.clx` returns `STORAGE_STATUS_BAD_TYPE` (I7) |
| **SF-CACHE** | The record path with the cache tagged `clx` and a `.col` file keeps the tag and adds the row (I2) |
| **Init** | `make_init_col.py --check` / `make_init_alk.py --check` on one file each; `init.col`'s effect rows `1 / 40 / 0 / 64` (D3) |
| **Break builds** | `storage_family_break_hold` (the parser's storage lookup back to `descriptorIndexByKey()`) must fail SF-HOLD-LOAD; `storage_family_break_as` (the AutoSave getter back to `descriptor_count`) must fail SF-HOLD-AS |

---

## 7. Card preparation (yours, once, before the device test)

1. Delete `Instrument/ColAdv/` and `Instrument/AlkAdv/`. To keep a sound,
   rebuild it on the new firmware and save it (files are not converted).
2. Kits, Scenes and Banks saved since S099 with an Advanced slot show "-"
   (I7). Rebuild and re-save them, or remove them.
3. Copy the new `init.col` and `init.alk` into `Colloid/` and `Alkaline/`.
4. If files were moved by hand, delete `Colloid/.hcindex` and
   `Alkaline/.hcindex`, so the boot scan rebuilds them.
5. **I9:** after the first boot, reload every Basic Colloid / Alkaline slot
   from its file once, or clear AutoSave, so no stale byte is held.

---

## 8. Order of work

| Step | What | Gate |
|---|---|---|
| 1 | C1, C2 (family, storage count, registry rows, self-check), C3 | SF-FAM, SF-HDR; the firmware builds |
| 2 | C4, C5 (load, save, seed) | SF-HOLD-LOAD, SF-HOLD-SAVE, SF-SEED; the break build |
| 3 | C6, C7 (AutoSave, snapshot, Morph fallback); C8's comments | SF-HOLD-AS, SF-HOLD-SNAP, SF-HOLD-USE; the break build |
| 4 | C9, C10 (browser, boot loops) | SF-CACHE, SF-KIT |
| 5 | C11, C12 (init files, checks) | `special_tags`; `check_special_tags.py` |
| 6 | The whole host suite and the image | `make -k -C tools/dsp_test all` (only the two pre-existing failures); `make -C tools/midi_test run`; `link_budget.py`; `elf_s099_check`, `colloid_cpu_check`, `katran_cpu_check` unchanged (no DSP change) |
| 7 | Device (§9), after §7 | yours |

---

## 9. Device checks

- **ColAdv → Colloid → ColAdv:** save a ColAdv sound with the effect on;
  load it into a Colloid slot (no effect heard, no effect cells shown); save
  it; load it from ColAdv. The effect is back as it was.
- **The same through a power cycle (AutoSave) and an Instrument-Load undo.**
- **A new Colloid** (reset or `init.col` loaded from Colloid) saved, then
  loaded from ColAdv:
  - from a reset slot, the effect is off (D4's seed);
  - from `init.col`, Juno I is on (D3, held through the Basic slot).
- **Browsers:** both list the one folder, before and after a save from each.
- **Kits:** a Kit with a ColAdv slot saves and loads.
- **Boot time:** no longer than before.

---

## 10. Statements this plan would make out of date (for you to log, if adopted)

- `FILESYSTEM_SPEC.md`, `BANK_PRESET_ARCHITECTURE.md`,
  `STORAGE_SRAM_MANIFEST.md`:
  - the Instrument folders;
  - `.clx` / `.alx` gone;
  - the file family and the header rule;
  - the storage row count;
  - AutoSave carrying held rows.
- `INSTRUMENTS_DSP_REFERENCE.md` §9.2 (add-a-type): the family, the storage
  row count, the held rows of a Basic member.
- `MENU_UI_SPEC.md`: two Load / Save entries over one folder.
- `COLLOID_MODULE_REFERENCE.md`, `ALKALINE_MODULE_REFERENCE.md`: the files
  and the held rows.
- `MEMORY.md`:
  - the S099 Advanced-type lines;
  - the Basic/Advanced merge, now decided.
- `S103_KATRAN_NOISE_SYNTH.md` §9: Katran joins the `kat` family by one row
  of C1's table when it gets an Advanced type.
