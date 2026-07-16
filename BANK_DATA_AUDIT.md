# Bank Data Audit

Created 2026-07-15. This is a planning document only. It records the code
deep-dive for adding root `Bank/` scan/load/save support after Scene
load/save stabilized.

The code is the source of truth for current behavior. The specification files
under `knowledge_files/specification_reference/` describe the intended
architecture and must be updated where they disagree with the implementation
target below. In particular, the current filesystem spec still describes
Bank-local Scene folders as `000..015`; the required Bank-local shape is now
`00..15`.

## Captured Requirements

- Root `Bank/` is a library like root `Scene/` and root `Kit/`.
- Root Bank folders use direct three-digit slots: `Bank/000 <name>/` through
  `Bank/999 <name>/`.
- A single Bank may contain at most 16 Scene folders.
- Scene folders inside a Bank use two-digit slots: `00 <name>/` through
  `15 <name>/`. They are not three-digit root Scene library entries.
- `bankset.bcg` is the Bank-level validator/config file. For this phase it can
  contain only:

  ```text
  format=helicase.bankset
  version=1
  active_scene=0
  ```

- `bankset.bcg` must not store `name`. Bank identity comes only from the
  directory name.
- No file stores its own object name. The existing exception remains the
  instrument-file register in `kitset.kcg`.
- Boot should prefer loading the lowest-number valid Bank, starting with
  `Bank/000 Slak/` on the generated card.
- If Bank load cannot supply a Scene, fallback is:
  1. lowest-number root Scene folder,
  2. lowest-number root Kit folder,
  3. system defaults already initialized in SRAM.
- For now only one resident Scene exists (`SCENE_COUNT == 1`). Bank load/save
  therefore populates only that one active Scene and uses Bank-local Scene slot
  `00` as the initial Scene slot.
- Empty Bank slots and empty Bank folders are valid user states. Loading an
  empty Bank must not be a hard error; it should complete Bank selection and
  then initialize the active Scene through the fallback chain.
- Future Bank Save will use a 16-bit toggle mask to decide which Bank-local
  Scenes to write and will not overwrite untoggled Scene folders. This phase
  should not implement the final toggle UI yet. It should structure the writer
  so the initial hardcoded mask can become that toggle mask later.
- Generate `SD_CARD/Bank/000 Slak/` from the current root
  `SD_CARD/Scene/000 Slak/`, with the embedded Bank Scene folder named
  `00 Slak/`.
- Promote top-level `Load:[Bank    ]` and `Save:[Bank    ]` menu entries.
  They should behave like Scene for now: explicit OK/OW operations, no
  live-on-scroll replacement.
- Save Bank character editing must seed from the retained resident Bank name,
  not from the selected target slot cache. Empty target slots must not show
  `Empty` once the user enters character editing.

## Current Code Reality

### Resident Data

- `Core/Scene/SceneData.h` defines `SCENE_COUNT 1u` and one resident
  `scene_t scenes[SCENE_COUNT]`.
- `scene_t` already owns:
  - eight-character `display_name` for resident Scene identity,
  - Scene settings,
  - one `PatternSet`,
  - one embedded `kit_t`.
- There is no resident Bank data owner. Bank name retention cannot be placed in
  `bankset.bcg`, and it should not be inferred from the Bank browser cache
  while editing an empty target slot.

### Storage Layer

- `Core/Hardware/SD/storageTypes.h/c` owns text schema parsing and formatting.
- It already defines root `Kit` and `Scene` constants, `sceneset.scg`, Scene
  parser state, placeholder Pattern/Effect parser state, and the shared
  `storage_parseNumberedFolder()` helper for three-digit root folders.
- `storage_parseNumberedFolder()` is correct for root `Bank/NNN <name>/`.
  It is deliberately wrong for Bank-local Scene folders because those use
  two digits and only slots `00..15`.

### Filesystem Facade

- `Core/Hardware/SD/filesystem.h/c` owns asyncfatfs state machines, scan
  caches, and public request APIs.
- `fs_file_type_t` has `FS_FILE_SCENE` but no `FS_FILE_BANK`.
- `filesystem.c` has internal ops for `FS_INTERNAL_OP_LOAD_SCENE`,
  `FS_INTERNAL_OP_SAVE_SCENE`, and `FS_INTERNAL_OP_SCAN_SCENES`, but no Bank
  ops.
- Root Scene scan cache is:
  - `scene_slot_present[STORAGE_SCENE_MAX_SLOTS]`,
  - `scene_slot_name[STORAGE_SCENE_MAX_SLOTS][9]`,
  - `scene_slot_open_name[STORAGE_SCENE_MAX_SLOTS][13]`.
- Equivalent Bank scan cache does not exist.
- Scene Load already stages a whole `scene_t` and commits to selected resident
  Scenes only after `sceneset.scg`, embedded `Kit <name>/`, pattern, and
  effect files validate.
- Scene Save already writes a complete root Scene folder and has a corrected
  same-slot cleanup path that does not wipe the root `Scene/` folder.
- The Scene load/save helpers are root-Scene oriented. Bank needs to reuse the
  child Scene payload mechanics without treating Bank-local `00 <scene>` as a
  root `Scene/NNN <scene>` entry.

### Preset Layer

- `Core/Scene/Preset/presetManager.h/c` owns async operation posting and
  completion classification for Menu.
- `preset_op_type_t` has `PRESET_OP_SCENE_LOAD` and
  `PRESET_OP_SCENE_SAVE`, but no Bank operations.
- `preset_fileTypeFromSaveType()` maps `SAVE_TYPE_SCENE` to
  `FS_FILE_SCENE`; there is no Bank mapping.
- `preset_loadSceneForScenes()` posts root Scene load with a destination Scene
  mask. `preset_saveScene()` posts root Scene save from the active Scene.

### Menu Layer

- `Core/Menu/menu.h` sets `NUM_PRESET_LOCATIONS 7` and enumerates
  `SAVE_TYPE_SCENE` before `SAVE_TYPE_GLO`.
- `Core/Menu/menu.c` promotes types through explicit arrays:
  - `menu_loadSaveLoadTypes[]` contains File, Dir, Kit, KitMrp, Scene.
  - `menu_loadSaveSaveTypes[]` contains File, Dir, sDir, Kit, KitMrp, Scene.
- Save overwrite display checks Kit/KitMrp and Scene caches. It does not check
  Bank.
- Top-row label switch has `Scene` but not `Bank`.
- `menu_requestCurrentLoadSaveSelection()` gives Scene explicit-OK behavior:
  scrolling only copies `filesystem_sceneSlotName(slot)` into
  `preset_currentName`.
- Save character editor seeding handles Kit/KitMrp from
  `scene_kitDisplayName(active)` and Scene from
  `scene_sceneDisplayName(active)`. It has no Bank source.
- Save/Load OK dispatch has Scene branches. It has no Bank branches.
- `menu_pollPresetStatus()` has Scene Load/Save completion branches and resets
  OK/OW actions to the top row when they finish.

### Boot

- `main.c` currently initializes SceneData, mounts SD, initializes Menu, scans
  Kits/Scenes/Instruments, then loads root `Scene/000` through
  `preset_loadSceneForScenes(0, 1u)`.
- Boot does not scan Banks and does not attempt Bank load.

### Fixture/Tools

- `SD_CARD/Scene/000 Slak/` and `SD_CARD/Scene/001 Slak/` exist.
- `SD_CARD/Bank/` does not exist.
- `tools/populate_scene_directory.py` generates Scene folders. No Bank fixture
  generator exists.

## Implementation Plan

### 1. Add Resident BankData

Files:

- Add `Core/Scene/BankData.h`
- Add `Core/Scene/BankData.c`
- Update `Makefile`
- Include `BankData.h` in `main.c`, `presetManager.c`, and `menu.c` where
  needed.

Create a small Bank owner module instead of storing Bank state inside
`SceneData`.

Public constants and API:

```c
#define BANK_DISPLAY_NAME_LEN SCENE_OBJECT_DISPLAY_NAME_LEN
#define BANK_SCENE_SLOT_COUNT 16u

void bank_init(void);
void bank_setDisplayName(const char name[BANK_DISPLAY_NAME_LEN]);
const char *bank_displayName(void);
void bank_setActiveSceneSlot(uint8_t slot);
uint8_t bank_activeSceneSlot(void);
void bank_setHasResidentBank(uint8_t present);
uint8_t bank_hasResidentBank(void);
```

What this does:

- Retains the currently loaded/saved Bank display name in SRAM.
- Retains the current Bank-local active Scene slot, initially `0`.
- Retains whether the current runtime came from a Bank. This is useful later
  for autosave and lets debug/menu code distinguish "root Scene only" from
  "Bank selected but Scene fell back".

Why it must exist:

- Bank names cannot live in `bankset.bcg`.
- The root Bank scan cache is a browser cache, not resident object identity.
  Save Bank must seed from the loaded Bank even when the target slot is empty.
- Future Bank autosave needs a Bank owner that is not part of any one Scene.

Important variables and loops:

- Store the name as eight printable LCD cells plus NUL, matching Scene/Kit
  identity storage.
- `bank_setDisplayName()` should copy exactly eight cells. For each index
  `i = 0..7`, copy a printable `name[i]`, or use a space when the source byte
  is NUL or outside `0x20..0x7e`. This mirrors Scene name behavior and prevents
  a short C string from leaving old bytes in the tail.
- `bank_setActiveSceneSlot()` clamps any input `>= 16` to `0`. This makes
  malformed `active_scene` harmless after parse while still keeping the parser
  strict enough to report invalid cards.

Connected code:

- `main.c` calls `bank_init()` beside `scene_initAll()`.
- Filesystem Bank Load calls `bank_setDisplayName()` from the directory name
  and `bank_setActiveSceneSlot()` from `bankset.bcg`.
- Filesystem Bank Save calls `bank_setDisplayName()` only after a successful
  directory write.
- Menu Save Bank editor reads `bank_displayName()`.

### 2. Extend storageTypes for Bank Format and Two-Digit Scene Slots

Files:

- `Core/Hardware/SD/storageTypes.h`
- `Core/Hardware/SD/storageTypes.c`

Add constants:

```c
#define STORAGE_ROOT_BANK              "Bank"
#define STORAGE_BANKSET_FILENAME       "bankset.bcg"
#define STORAGE_BANK_MAX_SLOTS         1000u
#define STORAGE_BANK_SCENE_MAX_SLOTS   16u
#define STORAGE_BANK_SCENE_PREFIX_LEN  2u
```

Add Bank parser state:

```c
typedef struct {
    uint8_t seen_format;
    uint8_t seen_version;
    uint8_t seen_active_scene;
    uint8_t active_scene;
} storage_bankset_t;
```

Add schema functions:

```c
void storage_banksetInit(storage_bankset_t *bank);
storage_status_t storage_banksetParseLine(storage_bankset_t *bank,
                                          const char *line);
storage_status_t storage_banksetFinalize(const storage_bankset_t *bank);
uint16_t storage_formatBanksetLine(char *dst,
                                   uint16_t cap,
                                   const storage_bankset_t *bank,
                                   uint8_t line_index);
```

What this does:

- Gives filesystem.c a strict format/version guard for Bank folders.
- Keeps Bank text parsing out of the SD state machine layer.
- Provides the writer for the minimal v1 `bankset.bcg` file.

Why it must exist:

- A folder named `000 Slak` under `Bank/` is not enough to prove that the
  folder is a Bank.
- `bankset.bcg` is the place for Bank-level metadata such as active Scene slot.
- The name rule must be enforced by omission: parser/writer must not require
  or emit `name=`.

Parser details:

- `format=helicase.bankset` sets `seen_format`.
- `version=1` sets `seen_version`.
- `active_scene=<n>` parses decimal `0..15`, sets `active_scene`, and sets
  `seen_active_scene`.
- Unknown keys may be ignored for forward compatibility.
- Malformed known keys return `STORAGE_STATUS_BAD_VALUE` or
  `STORAGE_STATUS_INVALID_FORMAT`.
- `storage_banksetFinalize()` should require format and version. It should
  allow `active_scene` to be absent and leave the initialized default `0`.
  The v1 writer still emits `active_scene` every time; the looser reader keeps
  hand-authored or early placeholder Bank folders loadable.

Writer loop:

- `storage_formatBanksetLine(..., line_index)` should return one line per
  index:
  - `0`: `format=helicase.bankset`
  - `1`: `version=1`
  - `2`: `active_scene=<slot>`
  - `3`: blank or zero-length sentinel, depending on the existing writer style.
- The active Scene value is decimal, not padded, because it is a schema value,
  not a directory prefix.

Add two-digit Bank-local Scene helpers:

```c
uint8_t storage_parseBankSceneFolder(const char *name,
                                     uint8_t *slot_out,
                                     char display_name[STORAGE_SCENE_DISPLAY_NAME_LEN + 1u]);
void storage_formatBankSceneDir(char *dst,
                                uint16_t cap,
                                uint8_t slot,
                                const char display_name[STORAGE_SCENE_DISPLAY_NAME_LEN]);
```

What this does:

- Parses and formats `00 <scene name>` through `15 <scene name>`.
- Keeps Bank-local Scene numbering visibly and mechanically different from
  root Scene library numbering.

Important math:

- Formatting uses:
  - tens digit: `'0' + (slot / 10u)`
  - ones digit: `'0' + (slot % 10u)`
- The helper must reject `slot >= 16`, even though two decimal digits could
  represent `99`.
- Parsing should require two leading decimal digits. The numeric value is:
  `(name[0] - '0') * 10u + (name[1] - '0')`.
- Accept a space separator as preferred. If compatibility with underscore is
  desired, accept `_` as the same separator, but do not document underscore as
  the generated form.
- Copy the name after the separator into exactly eight display cells using the
  existing display sanitization helpers.

Connected code:

- Root Bank scan uses the existing three-digit `storage_parseNumberedFolder()`.
- Bank-local child scan uses only `storage_parseBankSceneFolder()`.
- Bank Save child creation uses only `storage_formatBankSceneDir()`.
- Filesystem spec updates must replace `000..015` Bank-local examples with
  `00..15`.

### 3. Extend filesystem.h Public Bank API

File:

- `Core/Hardware/SD/filesystem.h`

Add `FS_FILE_BANK` to `fs_file_type_t`, preferably immediately after
`FS_FILE_SCENE` or before Scene if later code wants Bank first in file-type
tables. Preserve existing explicit switch handling so enum movement does not
silently change behavior.

Add a matching `fs_file_descs[]` entry in `filesystem.c`. Bank does not use a
legacy eight-byte name header; its display name comes from the directory scan
cache, just like root Kit/Scene directory saves.

Add public APIs:

```c
bool filesystem_requestScanBanks(fs_completion_cb_t cb);
uint8_t filesystem_bankSlotExists(uint16_t zero_based_slot);
const char *filesystem_bankSlotName(uint16_t zero_based_slot);
uint16_t filesystem_firstBankSlot(void);
uint16_t filesystem_firstSceneSlot(void);
uint16_t filesystem_firstKitSlot(void);
bool filesystem_requestLoadBank(uint16_t bank_slot,
                                uint16_t scene_mask,
                                fs_completion_cb_t cb);
bool filesystem_requestSaveBank(uint16_t bank_slot,
                                uint8_t source_scene,
                                const char display_name[8],
                                uint16_t bank_scene_save_mask,
                                fs_completion_cb_t cb);
uint8_t filesystem_lastBankLoadLoadedScene(void);
```

What this does:

- Gives Menu/Preset the same query/request surface as Scene.
- Adds first-slot helpers so boot/fallback code does not duplicate scan-cache
  loops in multiple layers.
- Reports whether the most recent Bank Load actually loaded a Bank-local Scene
  or completed with an empty-Bank fallback requirement.

Why it must exist:

- Menu needs overwrite status and displayed Bank slot names.
- Boot needs to find the lowest available Bank, Scene, and Kit.
- Empty Bank Load is a success at the Bank selection level, but it may not
  provide a Scene payload. That outcome needs an explicit status bit.

Important comments to put beside these declarations when implemented:

- Root Bank slots are `000..999`.
- Bank-local Scene slots are not represented by these root slot APIs.
- `bank_scene_save_mask` is the future toggle surface. This phase will pass
  `1u << 0` for the one implemented Bank Scene, but the filesystem writer must
  loop over the mask rather than hardcoding every delete/write operation.

### 4. Add Bank Scan Cache and Root Scan Operation

File:

- `Core/Hardware/SD/filesystem.c`

Add arrays:

```c
static uint8_t bank_slot_present[STORAGE_BANK_MAX_SLOTS];
static char bank_slot_name[STORAGE_BANK_MAX_SLOTS]
                          [STORAGE_KIT_DISPLAY_NAME_LEN + 1u];
static char bank_slot_open_name[STORAGE_BANK_MAX_SLOTS]
                               [STORAGE_KIT_FILENAME_MAX];
```

Add internal op:

```c
FS_INTERNAL_OP_SCAN_BANKS
```

Add error prefix:

```c
case FS_INTERNAL_OP_SCAN_BANKS: return "BnkSc";
```

Add dispatcher branch in `filesystem_tick()`.

What this does:

- Scans root `Bank/` and records one preferred folder per slot.
- Allows Menu to display `Empty` for absent Bank slots and `OW` for occupied
  Save targets.

Why it must exist:

- Bank is a top-level numbered library, not a single file.
- The Load/Save menu must not synthesize names by reading `bankset.bcg`.
  Directory names own identity.

Scan loop details:

- Clear all three Bank arrays at request time, exactly as Scene scan clears
  `scene_slot_*`.
- Open root `Bank/` using the LFN-aware directory open. Missing `Bank/` should
  be a successful empty scan, not a fatal boot error.
- Iterate asyncfatfs directory objects.
- For each directory object:
  - ignore non-directories;
  - parse `object->displayName` with `storage_parseNumberedFolder()`;
  - reject parsed slots `>= STORAGE_BANK_MAX_SLOTS`;
  - copy the eight display cells into `bank_slot_name[slot]`;
  - copy `object->shortName` or returned open alias into
    `bank_slot_open_name[slot]`.
- If more than one folder parses to the same slot, keep the lexically earliest
  display name using the existing `filesystem_displayPrecedesCached()` rule.
  This mirrors Kit/Scene duplicate handling and avoids unstable selection when
  the card has duplicates.

First-slot helper loops:

- `filesystem_firstBankSlot()` loops `slot = 0..999` and returns the first
  present slot. Use a sentinel such as `STORAGE_BANK_MAX_SLOTS` when none is
  present.
- `filesystem_firstSceneSlot()` and `filesystem_firstKitSlot()` do the same
  over their existing caches. Add them beside the cache accessors so all
  fallback code shares the same absent sentinel behavior.

### 5. Add Bank Load State Machine

File:

- `Core/Hardware/SD/filesystem.c`

Add internal op:

```c
FS_INTERNAL_OP_LOAD_BANK
```

Add error prefix:

```c
case FS_INTERNAL_OP_LOAD_BANK: return "BnkL";
```

Add operation state:

```c
static storage_bankset_t op_bankset_state;
static uint16_t op_bank_scene_mask;
static uint8_t op_bank_active_scene;
static uint8_t op_bank_child_present[STORAGE_BANK_SCENE_MAX_SLOTS];
static char op_bank_child_name[STORAGE_BANK_SCENE_MAX_SLOTS][9];
static char op_bank_child_open_name[STORAGE_BANK_SCENE_MAX_SLOTS][13];
static char op_bank_display_name[9];
static uint8_t op_bank_loaded_scene;
```

What this does:

- Opens one root Bank directory.
- Validates `bankset.bcg`.
- Scans up to 16 two-digit Bank-local Scene folders.
- Loads one selected Bank-local Scene into the current resident Scene.
- Completes successfully without Scene payload when the Bank has no selected
  or fallback child Scene.

Why it must exist:

- Root `Scene/` load cannot represent a Bank because it has no `bankset.bcg`,
  no Bank display name, no active Scene setting, and no nested slot namespace.
- Bank load needs to be valid even when zero Scene folders exist.

Request validation:

- `filesystem_requestLoadBank(bank_slot, scene_mask, cb)` should reject:
  - `bank_slot >= STORAGE_BANK_MAX_SLOTS`,
  - `scene_mask == 0`,
  - absent root Bank slot.
- It starts `FS_INTERNAL_OP_LOAD_BANK`, captures the destination Scene mask,
  clears `op_bank_loaded_scene`, and copies the root Bank display name from the
  scan cache.

State-machine phases:

1. Chdir root and open `Bank/`.
2. Open the selected `Bank/NNN <name>/` using `bank_slot_open_name[slot]`.
3. Read and parse `bankset.bcg`.
4. Scan child directory objects inside the Bank folder.
5. Choose the Bank-local Scene slot to load.
6. Load that child Scene folder using the existing Scene payload loader in
   Bank-child mode.
7. Commit resident BankData and finish.

Child scan loop details:

- Use `storage_parseBankSceneFolder()`, not
  `storage_parseNumberedFolder()`.
- For each directory object:
  - ignore files;
  - reject parse failures;
  - reject slot `>= 16`;
  - keep one display/open name per slot;
  - duplicate slot tie-breaker should use the same lexical rule as root scans.
- The child arrays are operation-local because they describe only the selected
  Bank, not the root Bank browser.

Scene selection rules:

- If `bankset.bcg active_scene` is present and that child slot exists, choose
  it.
- If active_scene is absent or points to a missing child, choose the lowest
  present child slot.
- If no child Scene exists, set `op_bank_loaded_scene = 0`, commit BankData
  name/active slot, and finish `FS_STATUS_DONE`. Preset/Menu or boot will run
  the fallback chain.
- The chosen Bank-local slot updates `bank_activeSceneSlot()`.

Scene child load reuse:

- Do not copy the entire Scene state machine blindly.
- Refactor the existing Scene loader into a shared helper with a root mode and
  a child mode, or add a Bank-specific wrapper around the child payload phases.
- The shared payload must accept:
  - a Scene directory open name already discovered by the current parent scan,
  - an eight-character Scene display name from the child directory,
  - a destination Scene mask,
  - a flag saying whether the parent is root `Scene/` or current Bank folder.
- In root mode, it opens `Scene/` and uses `scene_slot_open_name[op_slot]`.
- In Bank-child mode, it stays in the selected Bank directory and opens
  `op_bank_child_open_name[chosen_slot]`.
- In both modes, the payload loader still validates `sceneset.scg`, discovers
  embedded `Kit <name>/`, loads kitset and instruments, reads/validates
  pattern and effects placeholders, then commits staged `scene_t` atomically.

Important variables:

- `op_scene_display_name` remains the resident Scene name source. For
  Bank-child load it comes from `00 <scene name>/`, not root `Scene/`.
- `op_scene_child_display_name` remains the embedded Kit name source captured
  from `Kit <kit name>/`.
- `op_bank_loaded_scene` is the explicit output bit read later by Preset/Menu.
  It prevents empty Bank success from being confused with a filesystem error.

Important comments to add in code:

- A valid empty Bank completes successfully because Bank selection and Scene
  initialization are separate layers.
- Bank-local Scene folders are two digits by product rule; using the root
  three-digit parser here would collapse library Scene identity into Bank
  workspace identity.

### 6. Add Bank Save State Machine

File:

- `Core/Hardware/SD/filesystem.c`

Add internal op:

```c
FS_INTERNAL_OP_SAVE_BANK
```

Add error prefix:

```c
case FS_INTERNAL_OP_SAVE_BANK: return "BnkS";
```

Add request API:

```c
bool filesystem_requestSaveBank(uint16_t bank_slot,
                                uint8_t source_scene,
                                const char display_name[8],
                                uint16_t bank_scene_save_mask,
                                fs_completion_cb_t cb);
```

What this does:

- Creates/opens `Bank/`.
- Creates/replaces the selected root Bank folder name as needed.
- Writes `bankset.bcg`.
- Writes selected Bank-local Scene child folders.
- For this phase, Preset passes `bank_scene_save_mask = 1u`, so only
  Bank-local Scene slot `00` is written.

Why it must exist:

- Bank Save needs one more directory level than Scene Save.
- Future toggles require the writer to leave untoggled child Scene folders
  untouched.
- The previous root folder wipe bug must not reappear inside `Bank/`.

Request capture:

- Validate `bank_slot < 1000`.
- Validate `source_scene < SCENE_COUNT`.
- Sanitize/copy the eight-character Bank display name into an operation buffer.
- Clamp `bank_scene_save_mask` to `(1u << 16) - 1u`.
- For the first implementation, Preset should pass `1u << 0`.
- If later UI passes zero, this writer should still create the Bank folder and
  write only `bankset.bcg`; zero selected Scenes is valid.

Root Bank directory formatting:

- Use the existing three-digit numbered folder formatter, generalized from
  `filesystem_makeNumberedKitDir()` to `filesystem_makeNumberedDir()` if the
  current helper name is Kit-specific.
- Important math is the existing root slot math:
  - hundreds digit: `'0' + ((slot / 100u) % 10u)`
  - tens digit: `'0' + ((slot / 10u) % 10u)`
  - ones digit: `'0' + (slot % 10u)`
- The formatter appends a space and the eight display cells.

Bank-local Scene child formatting:

- Use `storage_formatBankSceneDir()` for `00 <scene name>`.
- The initial child Scene display name should come from
  `scene_sceneDisplayName(source_scene)`, not from the Bank display name.
- This keeps Bank name and Scene name independent even in the `000 Slak/00 Slak`
  fixture where they happen to match.

Save phases:

1. Chdir root.
2. Create/open root `Bank/`.
3. If the target Bank slot is empty, create `NNN <edited name>/`. If a
   same-slot Bank already exists, open it and update in place so untoggled
   child Scenes survive. If the existing folder's display name differs from
   the edited name and asyncfatfs still has no safe rename primitive, leave the
   existing folder name authoritative for this phase and record that actual
   name back into the Bank cache/BankData after success. Do not delete the
   existing Bank folder just to rename it.
4. Write/replace `bankset.bcg`.
5. Loop `bank_scene_slot = 0..15`.
6. For each slot whose bit is set in `bank_scene_save_mask`, delete/rewrite
   only that same two-digit child Scene slot.
7. Use the Scene Save payload writer in Bank-child mode to write the selected
   resident `scene_t` into that child folder.
8. Record the root Bank cache entry and update resident BankData only after
   success.

Scoped cleanup rule:

- Bank Save must never recursively delete the whole root `Bank/` folder.
- Bank Save must never delete other root Bank folders.
- Bank Save must never delete untoggled Bank-local Scene folders in the target
  Bank.
- The only recursive child deletion allowed in this phase is a child directory
  in the target Bank folder whose visible name parses with
  `storage_parseBankSceneFolder()` and whose parsed slot equals the currently
  selected save bit.
- Do not use short-alias fallback for Bank-local Scene deletion. This matches
  the hardened Scene Save correction: visible numbered names are the authority
  for scoped cleanup.

Important loop and bit math:

```c
for (uint8_t bank_scene_slot = 0u;
     bank_scene_slot < STORAGE_BANK_SCENE_MAX_SLOTS;
     bank_scene_slot++) {
    uint16_t bit = (uint16_t)(1u << bank_scene_slot);
    if ((bank_scene_save_mask & bit) == 0u)
        continue;
    ...
}
```

- `bank_scene_save_mask` is 16-bit because there are exactly 16 Bank-local
  Scene slots.
- The loop must use `< STORAGE_BANK_SCENE_MAX_SLOTS`, not `<=`, to avoid
  shifting by 16 on a 16-bit mask.
- Slot `0` maps to folder prefix `00`, not `000`.

Scene payload writer reuse:

- Refactor Scene Save so its payload writer can write into either:
  - root `Scene/<NNN Name>/`, or
  - current Bank folder child `<SS Name>/`.
- Shared payload writes `sceneset.scg`, `Kit <kit name>/`, six instruments,
  `pattern.pat`, and `effects.fx`.
- Root Scene mode records `scene_slot_*` and resident Scene name after success.
- Bank-child mode must not record root Scene cache entries. It may update the
  resident Scene display name only when the source Scene is the active resident
  Scene and the save completed. For this first phase, saving Bank slot `00`
  from the active Scene may leave `scene_sceneDisplayName()` unchanged because
  the child Scene name is sourced from it.

### 7. Add Filesystem Fallback Outputs

Files:

- `Core/Hardware/SD/filesystem.h`
- `Core/Hardware/SD/filesystem.c`
- `Core/Scene/Preset/presetManager.h`
- `Core/Scene/Preset/presetManager.c`
- `main.c`
- `Core/Menu/menu.c`

What this does:

- Lets boot and menu-driven Bank Load fall back when the selected Bank is empty
  or has no usable Bank-local Scene.

Why it must exist:

- User explicitly allows loading empty Banks.
- Empty Bank Load should not leave stale SRAM without a visible initialization
  rule.

Recommended layering:

- Filesystem Bank Load completes `FS_STATUS_DONE` and sets
  `filesystem_lastBankLoadLoadedScene()` to `0` when the Bank had no child
  Scene to load.
- Preset exposes:

  ```c
  uint8_t preset_completedBankLoadedScene(void);
  uint8_t preset_loadFirstAvailableSceneOrKit(void);
  ```

- Menu's `PRESET_OP_BANK_LOAD` completion branch:
  - If Bank loaded a Scene, start the same sound apply path as Scene Load.
  - If Bank loaded no Scene, call `preset_loadFirstAvailableSceneOrKit()` and
    keep `menu_storageBusy = 1`.
  - The fallback load then completes as `PRESET_OP_SCENE_LOAD` or
    `PRESET_OP_KIT_LOAD` and runs the existing apply path.

Fallback selection loop:

- First loop root Scene slots from `0..999` using
  `filesystem_firstSceneSlot()`.
- If a Scene slot exists, call `preset_loadSceneForScenes(scene_slot, 1u)`.
- Otherwise loop root Kit slots from `0..999` using
  `filesystem_firstKitSlot()`.
- If a Kit slot exists, call `preset_loadKitForScenes(kit_slot, 1u)`.
- If neither exists, leave `scene_initAll()` defaults in place and return a
  success/no-op indication so boot can continue.

Boot selection:

- After scans complete, call `filesystem_firstBankSlot()`.
- If a Bank exists, call `preset_loadBank(first_bank, 1u)`.
- If no Bank exists, call the fallback helper.
- After Bank Load completes with no Scene, run the same fallback helper before
  starting audio.

Important comments:

- Bank Load success and Scene payload success are separate so empty Banks are
  user-valid.
- The fallback order intentionally starts at root Scene, not root Bank again,
  to avoid an empty-Bank loop.

### 8. Extend Preset Manager for Bank

Files:

- `Core/Scene/Preset/presetManager.h`
- `Core/Scene/Preset/presetManager.c`

Add ops:

```c
PRESET_OP_BANK_LOAD,
PRESET_OP_BANK_SAVE,
```

Add callbacks:

```c
static void on_bank_load_complete(void);
static void on_bank_save_complete(void);
```

Add public functions:

```c
uint8_t preset_loadBank(uint16_t presetNr, uint16_t scene_mask);
uint8_t preset_saveBank(uint16_t presetNr);
uint8_t preset_completedBankLoadedScene(void);
uint8_t preset_loadFirstAvailableSceneOrKit(void);
```

What this does:

- Keeps Menu from calling filesystem directly.
- Captures request slot/type for completion validation and stale-selection
  checks.
- Preserves the existing async lifecycle:
  `filesystem_ack()`, set status busy, set op none, post request, complete
  through callback.

Why it must exist:

- Menu completion logic is keyed by `preset_op_type_t`.
- Bank Load may be followed by Scene/Kit fallback. That orchestration belongs
  above filesystem and below Menu display/apply details.

Input/output details:

- `preset_loadBank(presetNr, scene_mask)`:
  - input `presetNr` is root Bank slot `000..999`;
  - input `scene_mask` is resident destination mask, currently `1u`;
  - output nonzero means filesystem accepted `FS_INTERNAL_OP_LOAD_BANK`.
- `preset_saveBank(presetNr)`:
  - input `presetNr` is root Bank slot `000..999`;
  - source Scene is `scene_getActiveIndex()`;
  - display name is `preset_currentName`;
  - initial Bank-local Scene save mask is `1u << 0`;
  - output nonzero means filesystem accepted `FS_INTERNAL_OP_SAVE_BANK`.
- `preset_completedBankLoadedScene()` returns the filesystem output bit
  captured at Bank completion so Menu can decide whether to run fallback.

Connected code:

- `preset_fileTypeFromSaveType()` maps `SAVE_TYPE_BANK` to `FS_FILE_BANK`.
- Save Bank callback sets `PRESET_OP_BANK_SAVE`.
- Load Bank callback sets `PRESET_OP_BANK_LOAD`.
- Fallback helper calls existing Scene/Kit load functions. It should not
  recurse into Bank.

### 9. Extend Load/Save Menu for Bank

Files:

- `Core/Menu/menu.h`
- `Core/Menu/menu.c`

`menu.h` changes:

- Increase `NUM_PRESET_LOCATIONS` from `7` to `8`.
- Add `SAVE_TYPE_BANK` to `enum loadSaveEnum`, next to `SAVE_TYPE_SCENE`.
  The preferred UI order is Kit, KitMrp, Scene, Bank, Settings.

What this does:

- Gives Bank its own slot cursor in `menu_currentPresetNr[]`.
- Keeps Bank root slot browsing separate from Scene root slot browsing.

`menu.c` changes:

- Add `SAVE_TYPE_BANK` to both promoted type arrays:

  ```c
  static const uint8_t menu_loadSaveLoadTypes[] = {
      SAVE_TYPE_FILE,
      SAVE_TYPE_DIR,
      SAVE_TYPE_KIT,
      SAVE_TYPE_KIT_MORPH,
      SAVE_TYPE_SCENE,
      SAVE_TYPE_BANK
  };
  ```

  Save list also includes Bank after Scene.

- Update `menu_loadSaveTypeIsRestored()` to allow Bank on Load and Save.
- Update top-row label switch:

  ```c
  case SAVE_TYPE_BANK: toptxt = "Bank    "; break;
  ```

- Update Save overwrite check:

  ```c
  if (menu_saveOptions.what == SAVE_TYPE_BANK)
      return filesystem_bankSlotExists(menu_currentPresetNr[SAVE_TYPE_BANK]);
  ```

- Update `menu_requestCurrentLoadSaveSelection()`:
  - Bank uses explicit-OK behavior like Scene.
  - Encoder movement copies `filesystem_bankSlotName(slot)` into
    `preset_currentName`.
  - It must not start Bank Load on scroll.

- Update display-name selection for numbered Save/Load rows so Bank uses
  `filesystem_bankSlotName()`.

- Update Save OK dispatch:

  ```c
  case SAVE_TYPE_BANK:
      if (preset_saveBank(menu_currentPresetNr[SAVE_TYPE_BANK]))
          menu_storageBusy = 1u;
      break;
  ```

- Update Load OK dispatch:

  ```c
  case SAVE_TYPE_BANK:
      if (preset_loadBank(menu_currentPresetNr[SAVE_TYPE_BANK],
                          menu_kitLoadSceneMask))
          menu_storageBusy = 1u;
      else
          menu_storageBusy = 0u;
      break;
  ```

- Update Save character editor seeding:
  - When moving from slot-number row into character editing:
    - Kit/KitMrp seed from `scene_kitDisplayName(active)`.
    - Scene seeds from `scene_sceneDisplayName(active)`.
    - Bank seeds from `bank_displayName()`.

Important state behavior:

- Bank Load and Save are OK/OW operations. Completion must return the pointer
  to the top row, same as Scene.
- Bank must be included in the Load page guard that collapses lower-row
  movement to OK. Users do not edit names on Load.
- Empty Bank target slots show `Empty` while browsing but seed Save character
  editing from resident BankData.

Add completion handling:

```c
case PRESET_OP_BANK_LOAD:
    if (!preset_completedBankLoadedScene()) {
        if (preset_loadFirstAvailableSceneOrKit()) {
            menu_storageBusy = 1u;
            break;
        }
        menu_resetSaveParameters();
        menu_repaintAll();
        break;
    }
    menu_startSoundApply(... same flags as Scene Load ...);
    break;

case PRESET_OP_BANK_SAVE:
    ... same UI cleanup group as Scene Save ...
    break;
```

Connected code:

- Bank Load uses the existing `menu_isLoadSaveSelectionCurrent()` retry check.
- If Bank Load falls back into Scene/Kit Load, the original Bank completion
  should be acknowledged before posting fallback to avoid one active Preset
  completion blocking the next operation.

### 10. Update Boot Flow

File:

- `main.c`

What this does:

- Boot tries Bank first, then falls back through root Scene, root Kit, defaults.

Why it must exist:

- Bank is the next workspace level. The generated card should boot from
  `SD_CARD/Bank/000 Slak/`, not directly from root Scene once Bank exists.

Concrete changes:

- Include `BankData.h`.
- Call `bank_init()` after `scene_initAll()`.
- Add `filesystem_requestScanBanks()` to the pre-audio scan sequence.
- After scans, replace hardcoded `preset_loadSceneForScenes(0, 1u)` with:
  - get `first_bank = filesystem_firstBankSlot()`;
  - if present, call `preset_loadBank(first_bank, 1u)`;
  - otherwise call `preset_loadFirstAvailableSceneOrKit()`.
- If Bank Load completes but `preset_completedBankLoadedScene() == 0`, call
  fallback before audio starts.

Boot loop comments:

- The scan order should make all caches available before fallback selection:
  Kit, Scene, Bank, Instrument is acceptable, but Bank must be scanned before
  the first load choice.
- Do not hardcode slot 0 as the only possible fallback. The rule is
  lowest-number present slot.
- Defaults are already present after `scene_initAll()`, so the final fallback
  is to do nothing and continue.

### 11. Update Generated SD_CARD Fixture and Tools

Files:

- Add `SD_CARD/Bank/000 Slak/bankset.bcg`
- Add `SD_CARD/Bank/000 Slak/00 Slak/`
- Add or update tooling under `tools/`

Fixture shape:

```text
SD_CARD/
  Bank/
    000 Slak/
      bankset.bcg
      00 Slak/
        sceneset.scg
        Kit Slak/
        pattern.pat
        effects.fx
```

`bankset.bcg`:

```text
format=helicase.bankset
version=1
active_scene=0
```

What this does:

- Gives boot a valid Bank slot.
- Gives Bank Load a valid Bank-local Scene slot `00`.

Why it must exist:

- The firmware image and test SD tree need the same first-boot path.
- The two-digit child Scene rule should be visible in the checked-in fixture.

Tooling options:

- Add `tools/populate_bank_directory.py` that:
  - ensures `SD_CARD/Bank/000 Slak/`;
  - writes `bankset.bcg`;
  - copies or regenerates root `Scene/000 Slak/` into
    `Bank/000 Slak/00 Slak/`;
  - ignores `.DS_Store` and other dot files;
  - never writes `name=` into `bankset.bcg` or `sceneset.scg`.
- Or extend `tools/populate_scene_directory.py` with a `--bank-fixture` mode.
  A separate script is cleaner because Bank has different numbering and
  one-more-deep directory semantics.

### 12. Update Architecture Documents

Files:

- `MEMORY.md`
- `SCOPING_TARGETS.md`
- `knowledge_files/specification_reference/FILESYSTEM_SPEC.md`
- `knowledge_files/specification_reference/MODULE_INTERCHANGE_SPEC.md`

Required updates:

- Bank is now implemented for root scan/load/save in the initial one-Scene
  bridge form.
- Root Bank folders are `000..999`.
- Bank-local Scene folders are `00..15`, not `000..015`.
- `bankset.bcg` v1 contains format/version/active_scene only.
- Bank, Scene, and Kit names come from directory names.
- Empty Bank folders are valid and fall back to root Scene, then root Kit, then
  defaults.
- Future 16-Scene resident workspace and toggle UI remain pending.

### 13. Test Plan

Build checks:

- `make`
- `make img`
- `git diff --check`

Filesystem fixture checks:

- Verify `SD_CARD/Bank/000 Slak/bankset.bcg` exists.
- Verify the child folder is `00 Slak`, not `000 Slak`.
- Verify `bankset.bcg` has no `name=`.
- Verify `sceneset.scg` still has no `name=`.

Menu checks on hardware:

- Boot with `SD_CARD/Bank/000 Slak/` present. It should load Slak through Bank.
- Immediately enter Save Bank, scroll to an empty slot, then enter character
  editing. It should seed `Slak`, not `Empty`.
- Load Bank should be explicit OK/OW and return pointer to the top row when
  finished.
- Save Bank should be visible and return pointer to the top row when finished.
- Bank Save to an occupied slot should show `OW`; empty slot should show `ok`.
- Bank Save must not delete other root Bank folders.
- Bank Save must not delete untoggled child Scene folders in the target Bank
  once extra children are manually placed there.
- Loading an empty Bank folder containing only valid `bankset.bcg` should fall
  back to the lowest root Scene, then to lowest root Kit if no root Scene
  exists.

Regression checks:

- Save Scene still does not wipe root `Scene/`.
- Save Kit still does not wipe root `Kit/`.
- Save Scene and Save Bank never write names inside `sceneset.scg` or
  `bankset.bcg`.
- Load Scene remains explicit OK and does not live-load while scrolling.
- Kit/KitMrp live-on-scroll behavior remains unchanged.

## Implementation Order

1. Add `BankData` module and build integration.
2. Add storageTypes Bank constants, bankset parser/writer, and two-digit
   Bank-local Scene folder parser/formatter.
3. Add root Bank scan cache and APIs in filesystem.
4. Add first-slot helpers for Kit/Scene/Bank fallback.
5. Refactor Scene payload load/save helpers so Bank can use child mode without
   root Scene cache side effects.
6. Add Bank Load state machine.
7. Add Bank Save state machine with mask-driven child Scene writing and scoped
   same-slot child cleanup.
8. Add Preset Bank operations and fallback helper.
9. Promote Bank in Menu Load/Save UI.
10. Change boot to Bank-first fallback.
11. Add `SD_CARD/Bank/000 Slak/` fixture and generator.
12. Update docs and run build/diff checks.

## Open Risks

- Scene payload load/save refactor is the highest-risk code move. The safest
  implementation is to keep current root Scene behavior byte-for-byte where
  possible and parameterize only the parent directory/name/cache side effects.
- asyncfatfs still lacks an atomic rename/replace primitive. Bank Save in this
  phase should be scoped and non-destructive, but it is not yet the final
  power-loss-safe autosave promotion path.
- Bank Save root folder rename semantics need care. If only the display name
  changes for an existing Bank slot, replacing the whole folder would violate
  the "do not overwrite untoggled Scenes" rule. The first implementation
  should prefer opening the existing same-slot Bank folder and updating its
  contents in place; folder renaming can wait for a safe rename primitive if
  needed.

## Implementation Notes While Coding

### BankData landed

- Added `Core/Scene/BankData.h/c` and `Makefile` integration.
- The module keeps only resident Bank identity:
  - eight printable display cells plus NUL,
  - active Bank-local Scene slot clamped to `00..15`,
  - a `has_resident_bank` bit for future autosave/debug logic.
- `main.c` calls `bank_init()` beside `scene_initAll()` so boot can cleanly
  distinguish a root Scene/Kit fallback from a loaded or empty Bank.
- Comments were placed beside the fixed eight-cell copy loop and active-slot
  clamp because those are the important invariants, not just helper trivia.

### Bank text and folder parsing landed

- Added `STORAGE_ROOT_BANK`, `STORAGE_BANKSET_FILENAME`,
  `STORAGE_BANK_MAX_SLOTS`, and `STORAGE_BANK_SCENE_MAX_SLOTS`.
- Added `storage_bankset_t` with parser/finalizer/writer functions.
- `bankset.bcg` v1 writes only:
  - `format=helicase.bankset`,
  - `version=1`,
  - `active_scene=<0..15>`.
- It never writes `name=`.
- Added `storage_parseBankSceneFolder()` and `storage_formatBankSceneDir()`.
  The parser accepts exactly two leading digits and a separator, rejects
  values above 15, and therefore keeps Bank-local Scene identity distinct from
  root `Scene/NNN` identity.

### Filesystem Bank path landed

- Added `FS_FILE_BANK`, root Bank scan cache, Bank scan request/accessors, and
  first-slot helpers for Bank/Scene/Kit fallback.
- Added Bank Load state machine:
  - opens root `Bank/`,
  - opens selected `NNN <name>/`,
  - validates `bankset.bcg`,
  - scans Bank-local `00..15` child Scene folders,
  - loads `active_scene` if present, else the lowest child slot,
  - completes successfully without a Scene payload when the Bank is empty.
- Bank child payload loading reuses the existing Scene directory payload phases
  only after the Bank loader has positioned asyncfatfs inside the selected Bank
  folder. The Scene commit path detects `FS_INTERNAL_OP_LOAD_BANK` and updates
  `BankData` after the child Scene commits.
- Added Bank Save state machine:
  - creates/opens root `Bank/`,
  - creates/opens selected root Bank folder,
  - writes `bankset.bcg`,
  - writes child `00 <scene>/` through the Scene payload writer when mask bit 0
    is set,
  - accepts a zero mask as a valid empty-Bank save.
- Bank Save does not invoke the root Scene same-slot cleanup path and does not
  update the root Scene scan cache.
- Current limitation: this first implementation creates/opens the target root
  Bank display directory directly. It does not yet implement a safe same-slot
  folder rename policy for changing `NNN Old` to `NNN New` while preserving
  untoggled child Scenes under the old directory name.

### Preset/Menu/Boot landed

- Added `PRESET_OP_BANK_LOAD`, `PRESET_OP_BANK_SAVE`, `preset_loadBank()`,
  `preset_saveBank()`, `preset_completedBankLoadedScene()`, and
  `preset_loadFirstAvailableSceneOrKit()`.
- Added `SAVE_TYPE_BANK` to Load and Save type arrays.
- Load/Save top row now displays `Bank`.
- Bank Load and Bank Save use the explicit OK/OW row like Scene, not the live
  Kit scroll behavior.
- Save Bank overwrite state uses the root Bank scan cache.
- Save Bank character editing seeds from `bank_displayName()`, so empty target
  slots start from the retained Bank name instead of `Empty`.
- `menu_pollPresetStatus()` handles empty Bank completion by acknowledging the
  Bank op first, then starting root Scene/root Kit fallback while Preset is idle.
- Boot now scans `Bank/` and tries the lowest Bank slot before root Scene/root
  Kit fallback. A bounded two-pass boot completion loop handles the valid
  empty-Bank case where Menu posts the fallback after Bank completion.

### Fixture and generator landed

- Added `SD_CARD/Bank/000 Slak/bankset.bcg`.
- Added `SD_CARD/Bank/000 Slak/00 Slak/` copied from
  `SD_CARD/Scene/000 Slak/`, excluding `.DS_Store`.
- Added `tools/populate_bank_directory.py` so the fixture can be regenerated
  from a root Scene directory. Its default output is the same `000 Slak/00 Slak`
  bridge fixture and it writes no object names into files.

### Verification so far

- `make` completed successfully.
- `python3 -m py_compile tools/populate_bank_directory.py` completed
  successfully.
- `git diff --check` completed successfully.
- `make img` completed successfully and rewrote `build/LXRV2_lxr02.img`.
- The only build output warnings were the existing/newlib syscall linker
  warnings and LTO serial-compilation note; no Bank code compiler warnings were
  emitted.

## Retest Fix Notes

### Bank-local Scene Boot Load

- Fixed the shared Scene payload loader's pattern/effect re-entry for Bank
  loads.
- Root Scene Load still reopens `/Scene/<NNN Name>/` after reading the embedded
  Kit, matching the existing library path.
- Bank Load now takes one parent step from
  `Bank/NNN Name/SS Scene/Kit Name/` back to
  `Bank/NNN Name/SS Scene/` before opening `pattern.pat` and `effects.fx`.
- This keeps root `Scene/NNN` and Bank-local `SS` child Scene namespaces
  separate during boot and manual Bank Load.

### Load Bank OK Affordance

- Added `SAVE_TYPE_BANK` to the Load-page OK label branch.
- Kit and KitMrp still hide OK because they live-load on scroll.
- Scene and Bank now both show OK because neither should load while scrolling.

### Universal `none` Defaults

- `bank_init()` now seeds resident Bank display identity as `none    `.
- `scene_initAll()` now seeds resident Scene and embedded Kit display identity
  as `none    `.
- Default Instrument save stems are now `none`, allowing the save filename
  formatter to produce `none   1.drm` through `none   6.hat`.
- `filesystem_makeSceneEmbeddedKitDir()` now falls back to `Kit none`, not
  `Kit Untitled`, when handed an all-blank display field.
- `storage_makeSavedInstrumentDisplayFilename()` now falls back to `none`,
  not `inst`, for null/empty/all-space stems.

### Source Layout Move

- Created `Core/Bank/`.
- Moved former `Core/Scene/` to `Core/Bank/Scene/`.
- Moved `BankData.c/h` to `Core/Bank/`.
- Updated Makefile include paths and source paths:
  - `-ICore/Bank`,
  - `-ICore/Bank/Scene`,
  - `-ICore/Bank/Scene/Preset`,
  - `-ICore/Bank/Scene/Pattern`.
- Updated `tools/convert_legacy_kits.py` to read
  `Core/Bank/Scene/Preset/ParameterArray.h`.

### Retest Verification

- `make` completed after the `Core/Bank` move.
- `python3 -m py_compile tools/convert_legacy_kits.py tools/populate_bank_directory.py`
  completed successfully.
- `git diff --check` completed successfully after the retest fixes.
- `make img` completed after removing host `.DS_Store` files from
  `SD_CARD/Bank/`.
- `rg -n "^name=" SD_CARD/Bank` found no file self-name entries.

### `ERR BnkL06` Fix

- `BnkL06` means Bank Load reached phase 6: the root Bank scan cache said slot
  000 existed, but opening the selected root Bank folder returned no handle.
- Cause: the Bank scanner cached `op_object.shortName` as the later open key.
  For host-created LFN directories such as `000 Slak`, `afatfs_opendir_lfn()`
  performs display-name matching, so attempting the read-only open by generated
  SFN alias can fail even though the object listed correctly.
- Fix: root Bank scan now stores `op_object.displayName` as both browser text
  and future `afatfs_opendir_lfn()` key.
- The same correction was applied to Bank-local child Scene discovery so
  `00 Slak` opens by display component after the selected Bank opens.
- Saved Bank cache updates also retain the display directory name as the future
  open key instead of the returned SFN alias.

## Draft Pattern Payload Fix Notes

### Scene/Bank `pattern.pat` v2 Text Draft

- Replaced the empty Scene/Bank-local Scene pattern save payload with a v2 draft
  text schema:
  - `format=helicase.pattern`
  - `version=2`
  - `track1=<length>,<scale>,<128 active bits>` through `track7=...`
- Only `STEP_ACTIVE_MASK` is serialized for each of the 128 steps on each of
  the seven tracks. Velocity, note, probability, automation, rotate, shuffle,
  next-pattern, and change-bar remain PatternData defaults on load.
- Each track row also stores the per-track playback length and track scale.
  Length is validated as `1..NUM_STEPS`; scale is validated as
  `0..TRACK_SCALE_COUNT-1`.
- The parser still accepts the older v1 placeholder file so existing fixture
  Scene/Bank folders continue to load.
- The v2 parser overlays active bits and length/scale into the already
  initialized `PatternSet`. That keeps all non-stored draft fields at their
  normal empty-pattern defaults.
- While parsing the 128-bit rows, the loader also rebuilds the legacy 16-bit
  `pat_mainSteps[]` shadow using `step % NUM_STEPS_PER_BAR`, matching
  `pat_recordNote()` and the current 16-button LED projection.
- `FS_TEXT_LINE_MAX` is intentionally larger than before because a single
  track row needs room for key text, two decimal fields, 128 bit characters,
  newline, and NUL termination.

### Menu Dev Mode Gate

- Added `CONFIG_DEV_MODE` to `config.h`.
- `Core/Menu/menu.c` now includes the `File`, `Dir`, and Save-only `sDir`
  diagnostic entries in the Load/Save type arrays only when
  `CONFIG_DEV_MODE != 0`.
- The dispatch branches and filesystem diagnostic functions remain compiled;
  only their normal menu appearance is gated.
- Load/Save reset now falls back to `Kit`, not `File`, when the current type is
  no longer visible. This prevents Dev Mode-off builds from parking the cursor
  on a hidden diagnostic type after an operation finishes.

### Parser/Writer Correctness Notes

- `storage_parseU8()` now returns `STORAGE_STATUS_OK` on success, which is
  required because the pattern draft parser compares its result against the
  storage status enum.
- The draft `trackN=` parser also returns `STORAGE_STATUS_OK` after a valid row,
  not boolean true, so valid v2 rows are not misreported as parse failures.
- `storage_appendDecimalU16()` returns boolean true on success, which is
  required because the pattern draft writer uses it as a bounded append helper
  and treats zero as "line overflow / stop".
