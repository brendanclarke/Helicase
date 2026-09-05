# `.hcnames` Instrument Type Field — Implementation Plan

Status: implemented 2026-09-05 (filesystem.c, spec, tooling); card
verification pending. Prerequisite for the AutoSave boot reader
(S061_AUTOSAVE_READER.md §6). Must land and be card-verified before any
reader work begins.

---

## Goal

1. Add a **header row** to `.hcnames` declaring the firmware's instrument
   type vocabulary in enum order, so a firmware/file mismatch is detected
   and the file is safely regenerated.
2. Extend Instrument rows (33..128) from three fields
   (`name<TAB>source[<TAB>R]`) to four fields
   (`name<TAB>source<TAB>type[<TAB>R]`). Bank/Scene/Kit rows (0..32) are
   unchanged.

## Why

The boot reader's Case 2 needs to know which of the four typed directories
(`Instrument/Drum|Snare|Cymbal|HiHat/`) an Instrument row belongs to. The
autosave payload's own type byte is not trustworthy as a substitute source
for a component the reader has already decided is unproven. The type must
be in `.hcnames` as durable, independently authoritative state.

The header row ensures forward compatibility: if a future firmware revision
adds a fifth instrument type or reorders the enum, old `.hcnames` files
are detected at read time and regenerated — never silently misinterpreted.

---

## Header row

The first line of `.hcnames` declares the instrument type vocabulary:

```
#types	drm	snr	cym	hat
```

Tab-separated. The `#types` marker distinguishes this from data rows (no
data row name starts with `#`). The remaining tokens are the instrument
file extensions in `instrument_type_t` enum order (DRM=0, SNR=1, CYM=2,
HAT=3), matching `storage_instrumentTypeToText()` and the instrument
registry's `type_text`.

**Validation:** on every `.hcnames` read, the parser reads the first line
and compares its tokens against the firmware's instrument type registry.
Walk `instrument_type_t` values 0, 1, 2, ... calling
`storage_instrumentTypeToText()` for each, and compare the returned token
against the corresponding tab field. If the token count differs, or any
token text doesn't match, `.hcnames` is invalidated: close the file,
delete it, and allow the bootstrap writer to regenerate it on the next
pass.

**Writing:** the bootstrap writer and every full-file HCNAMES rewrite
emits this header line before data row 0.

**File geometry:** current `.hcnames` has 129 lines (rows 0..128). With
the header, it has 130 lines: line 0 is the header, lines 1..129 are data
rows 0..128. `FS_RESIDENT_NAMES_ROW_COUNT` stays 129 (data rows only).

---

## Type token encoding

Reuse the existing three-character lowercase storage tokens already defined
in `storage_instrumentTypeToText()` (storageTypes.c:486): `drm`, `snr`,
`cym`, `hat`. These are the same tokens `kitset.kcg` uses for its
`type=` field and `instrumentManager_typeFromText()` already parses them
back. `INSTRUMENT_TYPE_UNKNOWN` has no valid token and fails the parse —
an Instrument row with an unknown type is corruption.

## Type storage — no new SRAM

The instrument type for each resident slot already lives in SceneData:
`scene_t.kit.instruments[slot].type` (`kit_instrument_slot_t.type`). It
is populated by `instrumentManager_resetSlot()` during Kit/Scene/Bank Load
(via the kitset.kcg parser) and persists for the lifetime of the resident
Scene. The autosave payload stores it as the same 3-byte text token
(Autosave.c:874-885), so it survives autosave capture and restore.

**No parallel register is needed.** The `.hcnames` formatter reads the
type directly from SceneData when serializing Instrument rows. The parser
validates the type token on read but does not need to store it — during
runtime, SceneData is authoritative; at boot, the boot reader
(S061_AUTOSAVE_READER.md) will extract the type from `.hcnames` rows
during its own evaluation pass.

---

## Enumerated changes

### Change 1 — Header validation in .hcnames readers (filesystem.c)

**File:** `Core/Hardware/SD/filesystem.c`, every `.hcnames` read path.

**What:** Before the first `filesystem_cacheResidentRecord()` call in each
`.hcnames` read sequence, read line 0 and validate it as the `#types`
header. If the header is missing, malformed, or doesn't match the
firmware's instrument type registry, the `.hcnames` file is invalid —
close it, delete it, and let the bootstrap writer regenerate it.

**Description for adjacent comment block:**
```
Validate the .hcnames instrument-type header against the firmware
registry.

What: the first line of .hcnames must be a #types header listing the
instrument file extensions in enum order. Why: if the firmware's
instrument_type_t enum has changed since the file was written (a type
added, removed, or reordered), every Instrument row's type field is
potentially wrong; regenerating the file from SceneData is the only
safe recovery. Inputs: the first line of the physical file and the
instrument registry (via storage_instrumentTypeToText). Outputs:
success (proceed to data rows) or failure (invalidate and regenerate).
Affiliates: storage_instrumentTypeToText(), the bootstrap writer
(emits the header), filesystem_cacheResidentRecord() (reads data
rows after the header).
```

**Detail:** A small helper function validates one header line:
```c
static uint8_t filesystem_validateHcnamesHeader(const char *line)
{
    uint8_t type_index = 0u;
    const char *p;

    if (line[0] != '#' || line[1] != 't' || line[2] != 'y' ||
        line[3] != 'p' || line[4] != 'e' || line[5] != 's' ||
        line[6] != '\t')
        return 0u;
    p = &line[7];
    for (;;) {
        const char *token = storage_instrumentTypeToText(
            (storage_instrument_type_t)type_index);
        if (!token) break;
        if (p[0] != token[0] || p[1] != token[1] || p[2] != token[2])
            return 0u;
        type_index++;
        p += 3u;
        if (*p == '\t') { p++; continue; }
        if (*p == '\n' || *p == '\r' || *p == '\0') break;
        return 0u;
    }
    if (type_index == 0u) return 0u;
    const char *trailing = storage_instrumentTypeToText(
        (storage_instrument_type_t)type_index);
    return (trailing == NULL) ? 1u : 0u;
}
```

The six `.hcnames` read call sites (`residentNames_tick`, two in
`ensureAutosaveFiles_tick`, `autosaveParameterDrain_tick`,
`loadBankDirectory_tick`, `saveBankDirectory_tick`) each call
`filesystem_cacheResidentRecord()` in a line-reading loop. Each must
check: is this the first line? If so, pass it to
`filesystem_validateHcnamesHeader()` instead of `cacheResidentRecord`,
and skip to the next line (row 0) on success. On failure, abort the
read and trigger regeneration.

---

### Change 2 — Parser: type field validation (filesystem.c:5430)

**File:** `Core/Hardware/SD/filesystem.c`, function
`filesystem_cacheResidentRecord()` starting at line 5430.

**What:** For rows >= `FS_RESIDENT_NAMES_INSTRUMENT_BASE` (33), after
parsing the source column, parse a mandatory type field from the next
tab-separated column before checking for the optional `R` refresh flag.
Validate the token with `storage_instrumentTypeFromText()`; if it returns
`INSTRUMENT_TYPE_UNKNOWN`, return 0 (fail). For rows 0..32, behavior is
unchanged.

**Description for adjacent comment block:**
```
Validate the Instrument type field from HCNAMES Instrument rows (33..128).

What: after the source column, an Instrument row carries a mandatory
type token (drm/snr/cym/hat) as its third tab-separated field. The
optional R refresh flag, if present, follows as the fourth field.
Why: the boot reader needs durable type provenance independent of the
autosave payload; a missing or unrecognized type token on an Instrument
row is a malformed record and fails the read. Inputs: the physical
text line and the logical row number. Outputs: validated (the type is
confirmed present and recognized) or failed parse.
Affiliates: filesystem_formatResidentNameLine() (serializes the same
field), storage_instrumentTypeFromText() (validates the token).
```

**Detail:** The current parser structure at lines 5461-5478 already
isolates the second tab and parses what follows it as the `R` flag. The
change inserts a type-field validation between the source parse and the
`R` parse. For Instrument rows: find the second tab (already located);
the text between it and the next tab (or line end) is the type token;
validate it with `storage_instrumentTypeFromText()`; if it returns
`INSTRUMENT_TYPE_UNKNOWN`, return 0 (fail). Then look for the `R` flag
after the *next* tab, if any. For non-Instrument rows: the existing
2-or-3-field logic is unchanged — the second tab still leads directly to
the `R` check.

The parser does not store the parsed type — SceneData already holds the
authoritative type for each resident slot. The boot reader
(S061_AUTOSAVE_READER.md) will extract the type from `.hcnames` text
during its own evaluation pass when it needs it for Case 2 resolution.

---

### Change 3 — Formatter: type from SceneData (filesystem.c:20423)

**File:** `Core/Hardware/SD/filesystem.c`, function
`filesystem_formatResidentNameLine()` starting at line 20423.

**What:** For rows >= `FS_RESIDENT_NAMES_INSTRUMENT_BASE`, after emitting
the source token, emit `<TAB>type` before the optional `<TAB>R`. The type
is read directly from SceneData via `scene_instrumentSlotConst()`. For
rows 0..32, behavior is unchanged.

**Description for adjacent comment block:**
```
Emit the Instrument type field for HCNAMES Instrument rows (33..128).

What: Instrument rows serialize as name<TAB>source<TAB>type[<TAB>R]\n.
The type token is read from the resident SceneData
(scene_instrumentSlotConst()->type), which is always authoritative for
the current session's instrument layout. Why: the boot reader requires
durable type provenance in .hcnames; the formatter must round-trip what
the parser validates. Inputs: the row number (to decide whether a type
field applies), decomposed into scene_index and slot to access
SceneData. Outputs: the type token appended to the line buffer between
source and the optional refresh flag. Affiliates:
filesystem_cacheResidentRecord() (the parser half),
scene_instrumentSlotConst() (the type source),
storage_instrumentTypeToText() (the token producer).
```

**Detail:** The function currently has two output paths that converge on
the `refreshed` flag and newline: one for numeric source tokens (lines
20481-20496) and one for special tokens (`-`, `?`, `@`, lines
20498-20510). Both paths must insert the type field for Instrument rows.
After emitting the source token and before checking `refreshed`:

```c
if (row >= FS_RESIDENT_NAMES_INSTRUMENT_BASE) {
    uint16_t offset = row - FS_RESIDENT_NAMES_INSTRUMENT_BASE;
    uint8_t scene_idx = (uint8_t)(offset / STORAGE_KIT_SLOT_COUNT);
    uint8_t slot = (uint8_t)(offset % STORAGE_KIT_SLOT_COUNT);
    const kit_instrument_slot_t *inst =
        scene_instrumentSlotConst(scene_idx, slot);
    const char *type_token = storage_instrumentTypeToText(
        inst ? inst->type : INSTRUMENT_TYPE_DRM);
    if (!type_token)
        return 0u;  /* unknown type is a format error */
    if (len + 1u + 3u >= cap)  /* tab + 3-char token */
        return 0u;
    dst[len++] = '\t';
    dst[len++] = type_token[0];
    dst[len++] = type_token[1];
    dst[len++] = type_token[2];
}
```

Then the existing `refreshed` block follows as before. This insertion
point must be duplicated (or the two paths refactored to share it) for
both the numeric-source and special-token output branches.

No new RAM required — the type comes from SceneData at format time.

---

### Change 4 — Header emission in HCNAMES writers (filesystem.c)

**File:** `Core/Hardware/SD/filesystem.c`, every `.hcnames` write path.

**What:** Before emitting data row 0 (the Bank row), emit the `#types`
header line.

**Description for adjacent comment block:**
```
Emit the .hcnames instrument-type header before data rows.

What: the first line of every .hcnames file is a #types header listing
the firmware's instrument file extensions in enum order. Why: a reader
can detect and safely regenerate an .hcnames file written by a firmware
with a different instrument_type_t layout, rather than silently
misinterpreting type fields. Inputs: the instrument registry via
storage_instrumentTypeToText(). Outputs: one text line written to the
file before data row 0. Affiliates: filesystem_validateHcnamesHeader()
(the reader half), the bootstrap writer, and every full-file rewrite.
```

**Detail:** A small helper formats the header into the shared line buffer:
```c
static uint16_t filesystem_formatHcnamesHeader(char *dst, uint16_t cap)
{
    uint16_t len = 0u;
    uint8_t type_index;
    const char prefix[] = "#types";
    for (uint8_t i = 0u; prefix[i]; i++) {
        if (len >= cap) return 0u;
        dst[len++] = prefix[i];
    }
    for (type_index = 0u; ; type_index++) {
        const char *token = storage_instrumentTypeToText(
            (storage_instrument_type_t)type_index);
        if (!token) break;
        if (len + 4u >= cap) return 0u;
        dst[len++] = '\t';
        dst[len++] = token[0];
        dst[len++] = token[1];
        dst[len++] = token[2];
    }
    if (len + 1u >= cap) return 0u;
    dst[len++] = '\n';
    return len;
}
```

This must be called at the start of every full-file `.hcnames` write:
- `filesystem_nextResidentNameLine()` (bootstrap writer, line 20514)
- `filesystem_residentNames_tick()` (targeted update, case 5, line 5875)
- `filesystem_autosaveParameterDrain_tick()` (drain rewrite, case 72,
  line 7665)
- `filesystem_loadBankDirectory_tick()` (Bank Load rewrite, line 13158)
- `filesystem_saveBankDirectory_tick()` (Bank Save rewrite, line 16699)

Each of these already iterates rows 0..128 calling
`filesystem_formatResidentNameLine()`. The header is emitted before the
row-0 iteration begins.

---

### Change 5 — FILESYSTEM_SPEC.md update

**File:** `knowledge_files/specification_reference/FILESYSTEM_SPEC.md`,
the "Root resident-name register" section starting at line 355.

**What:** Update the row-format description to document the header row
and the 4-field Instrument row. Change the format description from:

```
Rows use fixed-order `name<TAB>source[<TAB>R]\n` text.
```

to:

```
The first line is a header declaring the instrument type vocabulary:
`#types<TAB>drm<TAB>snr<TAB>cym<TAB>hat\n`. The tokens are instrument
file extensions in instrument_type_t enum order. If the header does not
match the firmware's registry, the file is invalid and must be
regenerated.

Data rows follow (129 rows, 0..128):
Bank/Scene/Kit rows (0..32) use `name<TAB>source[<TAB>R]\n`.
Instrument rows (33..128) use `name<TAB>source<TAB>type[<TAB>R]\n`,
where `type` is a mandatory three-character token (drm/snr/cym/hat)
identifying the Instrument's typed directory. A missing or unrecognized
type token on an Instrument row fails the read.
```

---

## Changes NOT required

- **No new SRAM** — the formatter reads instrument type from SceneData
  (`scene_instrumentSlotConst()->type`), which is already resident. No
  parallel type register is needed.

- **No Load/Save completion updates** — unlike the source register, the
  type doesn't need a separate persistence path. SceneData is always
  authoritative for the current session's instrument types, and it is
  populated by the normal Kit/Scene/Bank Load paths (via
  `instrumentManager_resetSlot()` from kitset.kcg parsing) before any
  HCNAMES write runs.

- **Kit/Scene/Bank Save HCNAMES rewrite call sites** — these all call
  `filesystem_formatResidentNameLine()`, which Change 3 already handles.

- **`filesystem_ensureAutosaveFiles_tick()` HCNAMES read (line 6560)** and
  **autosave drain HCNAMES read (line 7804)** — these call
  `filesystem_cacheResidentRecord()`, which Change 2 already handles.

- **No new include needed** — `filesystem.c` already includes `SceneData.h`
  (line 67) for `scene_instrumentSlotConst()`, and `filesystem.h` already
  includes `InstrumentManager.h` (line 48) for `instrument_type_t`.

---

## Cutover

Delete the existing `.hcnames` from the dev card. The bootstrap writer
(Change 4) regenerates it on the next boot with the header row and
4-field Instrument rows.

## Verification

1. Boot with no `.hcnames` — the bootstrap writer produces a valid file
   with the `#types` header and correct type tokens derived from SceneData.
2. Instrument Load/Save, Kit Load/Save, Scene Load/Save, Bank Load each
   produce a correctly-typed 4-field Instrument row in `.hcnames`.
3. A malformed or missing type field on an Instrument row (33..128) fails
   the `.hcnames` read.
4. Bank/Scene/Kit rows (0..32) are unchanged at 2-3 fields.
5. Round-trip: `.hcnames` survives a full boot-load-save cycle with types
   intact and matching `scene_instrumentSlot()->type`.
6. Header mismatch: manually edit the `#types` header (add or remove a
   token, or reorder) and confirm the file is invalidated and regenerated
   on the next read.
7. Missing header: remove the `#types` line from `.hcnames` and confirm
   the file is invalidated and regenerated.

## Session notes — implementation, 2026-09-05

Implemented Changes 1-5 plus two adjacent-code/document updates, all
uncommitted on `dev-ph3-autosave-ph6` (after planning commits `919cfcc`,
`ce796ae`):

- Change 1: new static `filesystem_validateHcnamesHeader()` (defined
  immediately above `filesystem_cacheResidentRecord()`, comment block
  adjacent) plus a header-validation phase before every `.hcnames`
  line-reading loop. Six read sites covered: `residentNames_tick` (cases
  15-17), `ensureAutosaveFiles_tick` main live read (20,22,23) and temp
  prelude (21), `autosaveParameterDrain_tick` recovery read (44-46),
  `loadBankDirectory_tick` preload/retry (86,95,96),
  `saveBankDirectory_tick` preload (56,58,59).
- Change 2: `filesystem_cacheResidentRecord()` validates a mandatory type
  column on rows >= 33 between the source column and any `R` witness; a
  missing or unrecognized token fails the read. Legacy no-tab name-only
  rows keep their pre-existing acceptance (unchanged branch); a 1-tab
  Instrument row without a type now fails.
- Change 3: `filesystem_appendInstrumentTypeField()` (comment adjacent,
  defined directly above `filesystem_formatResidentNameLine()`) emits
  `<TAB><type>` from `scene_instrumentSlotConst()->type` in both formatter
  output branches (numeric-source and special-token).
- Change 4: `filesystem_formatHcnamesHeader()` (comment adjacent) writes
  the header first in all five full-file writers: bootstrap
  (`writeResidentNames_tick` case 7), targeted update (case 18), drain
  post-commit rewrite (case 77), Bank Load (case 97), Bank Save (case 60).
- Change 5: FILESYSTEM_SPEC.md row-format section updated (header +
  4-field Instrument rows + temp-prelude header note). AUTOSAVE.md
  refreshed-flag/safe-write paragraphs updated to match.
- `tools/verify_bank_autosave.py` now requires the `#types` header,
  tolerates the 2-4 field rows, and cross-checks each Instrument row type
  against the kitset member type.
- No `.c`/`.h` public-surface change was needed: both helpers are static;
  `filesystem.h` prototypes/comments are unchanged. Comment descriptions
  sit adjacent to every new/changed code block in filesystem.c.

Deviations from the plan text (all conservative, flagged for review):

- The header validator rejects trailing tokens beyond the registry (the
  plan snippet accepted an extra tab-separated field after `hat`); the
  plan sentence says any token-count difference must invalidate.
- Formatter type insertion is centralized in one helper instead of being
  duplicated in both output branches (the plan allows either).
- The plan snippet indexes a `uint16_t` line length; the existing
  formatter uses `uint8_t`, preserved by the helper taking `uint8_t *`.
- Read failure (I/O) during the header read closes with ERROR and never
  deletes the register; only a successfully read header that fails
  validation (or EOF with no header) triggers delete-and-regenerate.
- Per-site invalidation continuation reuses each machine's existing
  absent-register semantics: update-mode residentNames and Bank Load
  regenerate inline; ensure/drain/saveBank/read-only LOAD delete then
  report the normal absence outcome (error), and the next write-capable
  pass (boot Bank Load, later load/save commits) regenerates.
- Mirror validity is retracted (INVALID) at each header-failure site so a
  stale-but-VALID mirror image can never outlive the deleted register.
- Empty `.hcnames`/`.hcnamtmp` files (EOF before any line) count as
  header-missing and are invalidated like any headerless file.

Build state: clean logging-on build `text=392,148 data=404 bss=96,184`
(no new SRAM; all new state is code). RAM-allocation policy untouched.
Hardware verification steps 1-7 above remain outstanding; the dev-card
`/.hcnames` (headerless) is expected to be deleted and regenerated by the
first boot of this build — no manual card edit needed for cutover.
