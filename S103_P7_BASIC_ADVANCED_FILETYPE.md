# S103 P7 — One file type for Basic and Advanced Colloid / Alkaline (plan)

**Date:** 2026-11-24. **Branch:** `dev-ph7-alkaline` (the S103 P6 tree,
uncommitted). **Status:** plan. **No source was changed.**

**Your request (2026-11-24):**
- Colloid and Alkaline each get a **single library folder** and a **single
  file type** for both their Basic and Advanced types, with the extensions
  **`.col`** and **`.alk`**.
- A **Basic** instrument **ignores the effect rows when loading** and **writes
  them as off when saving**.
- Otherwise Basic and Advanced load and save the same (Advanced) file.
- Report any implementation issues.

**Labels:**
- **Code:** read from the tree, with a file and line.
- **Est.:** an estimate.

**Binding rules that apply** (`MEMORY.md`):
- **no backward compatibility, ever:** no migration, aliasing or converters;
- **the type registry is the storage schema;**
- Load/Save never freezes, and runtime SD work stays asynchronous;
- **Names in RAM (S090 P1):** reuse only through the documented domains;
- **a slot's type only changes by loading a file** (S093 P4 F1).

No step here edits `MEMORY.md` or `knowledge_files/` (§9 lists what goes
out of date).

---

## 0. Summary

| Item | Plan |
|---|---|
| **What merges** | **Storage only.** `clx` and `alx` keep their own runtime types (Advanced class, the two-per-Scene cap, the effect), but read and write the Basic type's folder, extension and file header: `Instrument/Colloid/*.col` and `Instrument/Alkaline/*.alk` |
| **What stays distinct** | The slot's **type token** in Kit (`kitset.kcg`), HCNAMES, AutoSave and MIDI Learn: `col` / `clx`, `alk` / `alx`. A Kit must still record whether a slot is Advanced |
| **Load** | The Load type list keeps **Colloid** and **ColAdv** (and **Alkaline** and **AlkAdv**). Both browse the same folder; the entry you load from sets the slot's class. A Basic load skips the four `fx_*` rows (it already does, §1.3). An Advanced load reads them |
| **Save** | Both write the full file (Basic rows + the four effect rows). A Basic save writes the effect rows as **off** with fixed defaults, not the slot's cells |
| **Header** | `type=col` / `type=alk` for both classes |
| **Card** | `ColAdv/` and `AlkAdv/` and their `init.clx` / `init.alx` go. `init.col` / `init.alk` carry the effect rows |
| **CPU / RAM** | 0 DSP; 0 RAM. Flash: one 11-byte family table + small code (est. < 0.5 KB) |
| **Issues** | Seven, in §2. The main ones: a filesystem classifier that would empty the Advanced browser (I2); every existing Kit / Scene / Bank holding a ColAdv or AlkAdv slot stops loading (I7, the no-compatibility rule) |

---

## 1. What exists (code)

### 1.1 The registry owns folder, extension and token

`instrument_registry[]` (`InstrumentManager.c:60–152`) gives each type a
token, label, extension and folder (`instrument_registry_entry_t`,
`InstrumentManager.h:495–524`). Today:

| Type | Token | Extension | Folder | Rows |
|---|---|---|---|---:|
| `INSTRUMENT_TYPE_COL` | `col` | `.col` | `Colloid` | 59 (`COLLOID_PARAM_DESCRIPTOR_COUNT`) |
| `INSTRUMENT_TYPE_CLX` | `clx` | `.clx` | `ColAdv` | 63 (the same table, + `fx_type fx_rate fx_depth fx_mix` at 59..62) |
| `INSTRUMENT_TYPE_ALK` | `alk` | `.alk` | `Alkaline` | 31 |
| `INSTRUMENT_TYPE_ALX` | `alx` | `.alx` | `AlkAdv` | 35 (+ the four at 31..34) |

- Each Advanced type shares its Basic type's descriptor table. The four
  effect rows follow the `@advanced-rows` marker
  (`ColloidParameters.c:395`, `AlkalineParameters.c:313`).
- `fx_type` is `ROW_SELECT_FIXED_SPECIAL` (not morphable). `fx_rate`,
  `fx_depth` and `fx_mix` are `ROW` (morphable).
- `ADV_FX_OFF` is 0 (`InstrumentManager.h:343`).
- The engine map already ties `clx → col` and `alx → alk`
  (`instrument_engine_of[]`, `InstrumentManager.c:170–178`).

**The token is used in two different roles:**
- **The slot's type** in `kitset.kcg` `type=` (`storageTypes.c:2128`), in
  HCNAMES Instrument rows (mandatory type field, `filesystem.c:6800–6850`),
  in AutoSave (`Autosave.c:1361`, `:1775`) and in MIDI Learn
  (`MidiLearn.c:508`, `:621`);
- **The instrument file's header** `type=` (written at `storageTypes.c:1094`,
  checked against the expected type at `storageTypes.c:2285–2288`).

### 1.2 The folder and the extension

- **The folder:** every storage path reaches it through
  `instrumentManager_storageDirectory()`:
  - the index load, `filesystem.c:5099`;
  - Instrument Load, `:17625`;
  - Instrument Save, `:18039`;
  - the boot reader, `:32407`;
  - the index request, `:35151`.
- **The extension:** every path reaches it through
  `instrumentManager_filenameMatchesType()` /
  `storage_instrumentTypeExtension()`. That includes the Kit member leaf
  (`storage_makeSavedInstrumentDisplayFilename()`, `storageTypes.c:879`) and
  the Kit check.
- **The Kit check:** `storage_kitsetFinalize()` (`storageTypes.c:2200–2217`)
  rejects a whole Kit whose member file does not match its slot's type's
  extension. The Kit is then shown as "-".

### 1.3 Loading already skips rows a type does not have

`storage_instrumentParseLine()` maps each key through the expected type's
descriptor table and ignores a key it does not find
(`storageTypes.c:2296–2301`: `if (!descriptor) return STORAGE_STATUS_OK;`).
A Basic Colloid's table stops at 59, so **`fx_*` keys are already skipped**
when a Basic slot reads a file that has them.

### 1.4 Saving walks the slot type's table

`storage_formatInstrumentLineView()` (`storageTypes.c:1052`) is the single
writer, used by Instrument Save, Kit member saves and InstrumentMrp Save
(`filesystem.c:18653`, `:35670`). It writes:
- `type=<the slot's token>`;
- `[params]` for every row of the slot type's table;
- `[morph]` for the morphable rows.

A Basic save therefore writes no `fx_*` rows today.

### 1.5 The browser cache and the `.hcindex`

- **One shared name cache**, tagged with one type
  (`fs_list_cache_type`, `filesystem.c:1952`).
- **`.hcindex`** is per folder, and its rows are **stems only**, with no type
  (`filesystem_validateInstrumentIndexRow()`, `filesystem.c:11915–11945`).
  Two types that browse one folder can share it.
- **Scans classify each file by extension** to the **first** registry row
  that matches (`filesystem_instrumentTypeFromFilename()`,
  `filesystem.c:11678–11697`).
  - The record path (`filesystem_recordInstrumentFile()`, `:11991`) and the
    post-save refresh (`filesystem_updateInstrumentCacheAfterSave()`,
    `:12412`) **clear the cache and re-tag it** when that type differs from
    the cache's (`:12037–12041`, `:12460–12464`).
- **Several boot and rebuild loops** walk every registry row and treat each
  row's folder as its own:
  - the all-types index rebuild (`:4796`, `:23689`);
  - the typed scans (`:29895`);
  - the name repair (`:30778`, `:30852`).

---

## 2. Implementation issues

| # | Issue | Effect if not handled | Resolution in this plan |
|---|---|---|---|
| **I1** | **Two runtime types must remain.** A slot's type only changes by loading a file, so the choice "Basic or Advanced" has to be made somewhere | If the Load list showed one "Colloid" entry, there would be no way to make a slot Advanced | Keep both Load type entries (Colloid / ColAdv, Alkaline / AlkAdv), both browsing one folder (Q1). Only storage merges |
| **I2** | **The extension classifier picks the first row.** With `clx` on `.col`, every `.col` file classifies as `col` | Opening the **ColAdv** browser, or saving from a ColAdv slot, clears the cache, re-tags it `col`, and the ColAdv browser then counts **0 files** (`filesystem_cachedInstrumentCount()`, `:11653–11657`) | The record and post-save paths classify against the **active cache's type** when its extension matches (C5) |
| **I3** | **Registry-wide boot loops would process the shared folder twice** (index rebuild, typed scan, name repair) | Twice the boot SD work for those folders, and a second rewrite of the same `.hcindex` (identical content, but a wasted write) | Those loops skip a row whose **file family** is another row (C6) |
| **I4** | **The header token.** A ColAdv save would write `type=clx` into a `.col` file, and the parser rejects any header not equal to the expected type | A Basic slot could not load a file saved by ColAdv, and the reverse | The header carries the family token (`col` / `alk`); the parser compares families (C3). Strict: a `type=clx` header is refused (Q2, the no-compatibility rule) |
| **I5** | **A Basic save writes no effect rows**, and a Basic slot's image cells 59..62 (31..34) are not effect values | The file would not be "the Advanced file". Loaded into ColAdv, missing keys load 0, so `fx_mix` would be 0 (an inaudible effect once switched on) | The writer walks the family's full table; for a Basic slot the effect rows come from a fixed table: `fx_type` 0 (off), `fx_rate` 40, `fx_depth` 0, `fx_mix` 64 (`init.clx` / `init.alx`'s values), in `[params]` and (rate, depth, mix) in `[morph]` (C4, Q4) |
| **I6** | **One stem, one file now.** `ColAdv/foo.clx` and `Colloid/foo.col` were two files; now they are one | A Basic save over a file an Advanced slot saved **resets its effect to off** (your rule), and a same-named ColAdv save overwrites a Basic one | Intended; recorded here so it is not a surprise. A Basic save that kept an existing file's effect would need a read-before-write in the async save chain; not proposed |
| **I7** | **No backward compatibility.** `.clx` / `.alx` files are no longer recognised anywhere | (a) Files in `ColAdv/` / `AlkAdv/` disappear from the browser. (b) **Every Kit, and so every Scene and Bank, that holds a ColAdv or AlkAdv slot saved since S099 stops loading**: its member is `name<n>.clx`, which fails `storage_kitsetFinalize()`, and the Kit shows "-". (c) HCNAMES rows that name such a file no longer resolve at boot | Card preparation (§6). The shipped card image has none: the only `.clx` / `.alx` files in `SD_CARD/` are `init.clx` and `init.alx` (checked) |

**No issue found in:**
- the `.hcindex` format (stems only: one index serves both browser types);
- AutoSave (it stores the slot's token and its cells, not files);
- MIDI Learn (the slot token);
- the Advanced cap (`instrumentManager_typeSelectableForSceneSlot()`,
  unchanged);
- the reserved `.hctmp.<ext>` names (same extension);
- the DSP.

---

## 3. Design

### 3.1 The file family

A new table beside the engine map, `instrument_file_family_of[]`
(`InstrumentManager.c`). Every type is its own family except:
- `clx → col`;
- `alx → alk`.

It is reached through `instrumentManager_typeFileFamily(type)`.
- **Why a table, not the engine map:** the two coincide today, but "the same
  engine" and "the same file" are different promises. A later Advanced type
  could have its own file, and one table per promise keeps each test one
  load and one compare.
- **Why not a registry field:** the registry rows are positional, and
  `switch_row_plus1` is their optional trailing field.

### 3.2 Folder and extension

The `clx` and `alx` registry rows change their extension and folder to the
family's: `".col"` / `"Colloid"`, `".alk"` / `"Alkaline"`.
- Every path that goes through `instrumentManager_storageDirectory()` and
  the extension accessors then follows: load, save, index, boot reader and
  Kit member leaf.
- The Kit check (`storageTypes.c:2212`) then accepts `type=clx` with
  `name<n>.col`.

### 3.3 The header

- **Written:** `type=` the family's token.
- **Read:** accepted when the token's family is the expected type's family.
- A ColAdv slot loads a file a Colloid saved, and the reverse.
- A file whose header is `clx` or `alx` is refused (Q2).

### 3.4 Load (no change in the parser)

- **A Basic expected type** maps only its own rows, so the `fx_*` keys are
  skipped (§1.3): "ignores the effect slot".
- **An Advanced expected type** reads them.
- **The browser entry** you load from (Colloid or ColAdv) sets the slot's
  type, through the existing Load path and the Advanced cap.

### 3.5 Save

`storage_formatInstrumentLineView()` walks the **family's widest table**: the
Advanced type's (63 Colloid rows, 35 Alkaline rows).
- **Rows the slot's type has:** the slot's cells, as now.
- **The effect rows of a Basic slot:** the fixed "off" table (I5).

One writer covers root Instrument Save, Kit member files and
InstrumentMrp Save.

### 3.6 Card image and generators

- **`tools/colloid/make_init_col.py`, `tools/alkaline/make_init_alk.py`:**
  write one file each, `init.col` (63 rows) and `init.alk` (35 rows), with
  the header `type=col` / `type=alk`. The `OUT_ADV` jobs go.
- **The effect values in the shipped `init`:** Q3.
- **`SD_CARD/Instrument/ColAdv/` and `AlkAdv/`:** removed.
- **`--check`:** the same files.

---

## 4. Change list (est.; every edit with its comment block)

| # | File, place | Change |
|---|---|---|
| C1 | `InstrumentManager.c` after `instrument_engine_of[]` (`:170`) | `instrument_file_family_of[]`; `instrumentManager_typeFileFamily()` (declared in `InstrumentManager.h` with its contract) |
| C2 | `InstrumentManager.c` registry rows `[INSTRUMENT_TYPE_CLX]` (`:120–124`) and `[INSTRUMENT_TYPE_ALX]` (`:131–135`) | Extension and folder → the family's; their comments (and the stale "seven chorus rows (38)" / "five chorus rows (64)" counts) corrected |
| C3 | `storageTypes.c` | The header writer (`:1094`) writes `storage_instrumentTypeToText(instrumentManager_typeFileFamily(type))`. The header check (`:2285–2288`) compares families. `storage_instrumentTypeToText()` keeps returning the slot token for Kit, HCNAMES, AutoSave and Learn |
| C4 | `storageTypes.c` `storage_formatInstrumentLineView()` | Iterate the family's widest table (a new accessor, `instrumentManager_typeFileTable()`: the family member with the most rows); for a row past the slot type's own count, write the Basic-off value from `instrumentManager_basicFxDefault()` (a four-value table per family in `ColloidParameters.c` / `AlkalineParameters.c`, next to the `@advanced-rows` marker) |
| C5 | `filesystem.c` `filesystem_recordInstrumentFile()` (`:11991`) and `filesystem_updateInstrumentCacheAfterSave()` (`:12412`) | Classify against `fs_list_cache_type` first when the filename matches it (`instrumentManager_filenameMatchesType(name, fs_list_cache_type)`), else the first-match rule as now (I2) |
| C6 | `filesystem.c` the registry-wide loops (`:4796` all-types index creation, `:23689` subdirectory loop, `:29895` typed scans, `:30852` name repair). The boot directory trim (`filesystem_trimInstrumentTreeBlocking()`, `:24306`) enumerates the card's real subfolders, not the registry, and needs no change | Skip a row whose `instrumentManager_typeFileFamily(entry->type) != entry->type` (I3). A per-type request for `clx` / `alx` still runs (on the family's folder) |
| C7 | `tools/colloid/make_init_col.py`, `tools/alkaline/make_init_alk.py`; `SD_CARD/Instrument/` | §3.6 |
| C8 | `tools/dsp_test/check_special_tags.py`, the `special_tags` target | Drop the `init.clx` / `init.alx` checks; keep `CLX` / `ALX` `TABLES` (the descriptor rows are unchanged) |
| C9 | Host laws (`tools/dsp_test/modules/instrument_manager.c` or a new `storage_family.c`, over the extracted `storageTypes.c`) | §5 |

**Not changed:**
- the Menu (the Load / Save type lists still name both classes);
- AutoSave;
- HCNAMES (the slot token);
- MIDI Learn;
- the DSP;
- every other type.

---

## 5. Laws

| Law | What |
|---|---|
| SF-FAM | `typeFileFamily`: `clx → col`, `alx → alk`, every other type itself; `storageDirectory(clx) == storageDirectory(col)`; the extensions equal |
| SF-HDR | A `clx` slot's file header reads `type=col`. Parsing `type=col` with expected `clx` passes; `type=clx` with expected `col` or `clx` is refused (Q2); `type=alk` with expected `col` is refused |
| SF-SAVE | A Basic Colloid's file has 63 `[params]` rows, the last four `fx_type=0 fx_rate=40 fx_depth=0 fx_mix=64`, and 3 of them in `[morph]`, whatever the slot's cells 59..62 hold. An Advanced Colloid's file has its own cells. The same for Alkaline (35) |
| SF-LOAD | The ColAdv file loaded into a `col` slot leaves cells 59..62 untouched and every other cell equal; loaded into `clx`, all 63 equal |
| SF-KIT | A `kitset.kcg` with `type=clx file=name1.col` finalises OK; with `file=name1.clx` it returns `STORAGE_STATUS_BAD_TYPE` (I7) |
| SF-CACHE | The record path with the cache tagged `clx` and a `.col` file keeps the tag and adds the row (I2) |
| Init | `make_init_col.py --check` / `make_init_alk.py --check` pass on the one file each |

**Device:**
- a Colloid save loaded from ColAdv, and the reverse;
- the effect is off after a Basic save;
- both browsers list the one folder, before and after a save from each;
- a Kit with a ColAdv slot saves and loads;
- boot time is not longer than before.

---

## 6. Card preparation (yours, once)

1. Delete `Instrument/ColAdv/` and `Instrument/AlkAdv/`. To keep a `.clx` /
   `.alx` file, re-create it: load its sound, then save from the new firmware.
   Files are not converted (the standing rule).
2. **Kits, Scenes and Banks** saved since S099 with an Advanced slot show
   "-" (I7). Re-build and re-save them on the new firmware, or remove them.
3. Copy the new `init.col` and `init.alk` into `Colloid/` and `Alkaline/`.
4. **If files were moved by hand:** delete `Colloid/.hcindex` and
   `Alkaline/.hcindex` so the boot scan rebuilds them (an edited card is
   only seen after a power cycle).

---

## 7. Questions

| # | Question | Recommendation |
|---|---|---|
| Q1 | Keep two Load type entries per family (Colloid / ColAdv, Alkaline / AlkAdv) over the one folder, the entry choosing the slot's class (I1)? | Yes: it is the only way a slot becomes Advanced, and it keeps the Advanced cap where it is |
| Q2 | Header: accept only the family token (`col` / `alk`), refusing `clx` / `alx`? | Yes: the no-compatibility rule; nothing on the shipped card carries the old tokens after C7 |
| Q3 | The shipped `init.col` / `init.alk`: the effect as `init.clx` / `init.alx` have it now (Juno I on: `fx_type 1`, rate 40, depth 0, mix 64), or off? | **On**: a ColAdv load of `init` sounds as today; a Colloid load ignores the rows anyway. Only Basic *saves* write it off |
| Q4 | A Basic save's other three effect rows: the fixed defaults (40 / 0 / 64) rather than the slot's cells? | Yes (I5): a Basic slot's cells there are not effect values |
| Q5 | The I7 consequence (Kits / Scenes / Banks with Advanced slots since S099 stop loading) is acceptable under the standing rule? | Yes; the alternative would be a converter, which the rule forbids |

---

## 8. Order of work

| Step | What | Gate |
|---|---|---|
| 1 | C1–C3 with SF-FAM, SF-HDR | the host laws; the firmware builds |
| 2 | C4 with SF-SAVE, SF-LOAD | the host laws |
| 3 | C5, C6 with SF-CACHE, SF-KIT | the host laws; `make -k -C tools/dsp_test all` as before (the two pre-existing failures only) |
| 4 | C7, C8 | `special_tags`; `check_special_tags.py` |
| 5 | Image | `link_budget.py`; `elf_s099_check` and the Katran / Colloid CPU checks unchanged (no DSP change) |
| 6 | Device (§5 device list, §6 first) | by you |

---

## 9. Statements this plan would make out of date (for you to log, if adopted)

- `FILESYSTEM_SPEC.md`, `BANK_PRESET_ARCHITECTURE.md`,
  `STORAGE_SRAM_MANIFEST.md`: the Instrument folders (no `ColAdv` /
  `AlkAdv`), the `.clx` / `.alx` extensions gone, the file family and its
  header rule.
- `INSTRUMENTS_DSP_REFERENCE.md` §9.2 (add-a-type): the file family; an
  Advanced type writes its Basic type's file.
- `MENU_UI_SPEC.md`: Load / Save type lists, two entries over one folder.
- `COLLOID_MODULE_REFERENCE.md`, `ALKALINE_MODULE_REFERENCE.md`: the files.
- `MEMORY.md`:
  - the S099 Advanced-type lines (`clx`, `alx`);
  - the S103 plan's "Basic/Advanced merge" note, now decided.
- `S103_KATRAN_NOISE_SYNTH.md` §9 (merge-ready). Katran has no Advanced type
  yet; when it gets one, it joins the `kat` family by one row of C1's table.
