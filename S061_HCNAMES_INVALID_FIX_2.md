# S061 — HCNAMES Invalid Fix 2: Preserve All 96 Types in Existing Boot Scratch

Status: diagnosis confirmed from `SD_CARD_READER_4` as the input fixture and
`SD_CARD_READER_8` as the post-boot capture made with the current working-tree
image. The files are not invalid. The HCNAMES-authoritative boot path is
passing corrupted in-RAM Instrument types to otherwise valid files. The current
per-Scene six-type snapshot is too late for Scenes 1..15.

## 1. Verdict

The precise defect is the lifetime of
`fs_stage_workspace.boot_reader_type[96]`.

`filesystem_bootReaderParseRegisterFile()` correctly parses all 96 mandatory
Instrument type tokens from `/.hcnames`, but stores them in a member of
`fs_stage_workspace`, a union also used as `scene_stage`, `kit_stage`, and
`instrument_stage`. Every Case-2 narrow load writes one of those other union
members. The first Scene therefore destroys the saved HCNAMES types for every
later Scene.

The attempted fix currently in `filesystem.c` snapshots only six types at the
start of each Scene. That protects those six types from loads performed later
within the same Scene, but it does not protect the other 90 types from loads
performed by earlier Scenes. Scene 0 snapshots valid data before the first
narrow load and succeeds. Scene 1 snapshots bytes 6..11 from an already
overwritten staging object, and Scenes 2..15 do the same at later offsets.

The targeted fix is to place all 96 parsed types in storage outside the staging
union before the first narrow load and use that immutable image for the entire
traversal. No new RAM is required: give the existing 144-byte
`op_bank_child_display` operation scratch a second, mutually exclusive union
view for the 96 boot-reader types. Apply that view in both boot-reader entry
paths.

## 2. The input fixture is valid

`SD_CARD_READER_4` is the intended state produced by:

1. load Bank 001 `Full`;
2. without exiting the Load/Save menu, load root Scene 008 `Rollin` into
   resident Scenes 0, 1, 14, and 15;
3. power off before the guarded AutoSave writer can publish.

Its durable state is internally coherent:

- `settings.cfg` has `active_bank=1` and `autosave=1`;
- `/.hcnames` has the valid `#types drm snr cym hat` header, exactly 129 data
  rows, and `R` on every row;
- HCNAMES Bank row 0 is `Full 001 R`;
- Scene rows 1, 2, 15, and 16 are `Rollin 008 R`;
- their Kit rows are `Rollin - R`;
- their Instrument rows are `rollind1`, `rollind2`, `rollind3`, `rollins1`,
  `rollinc1`, and `rollinh1`, with types `drm/drm/drm/snr/cym/hat`, source `-`,
  and `R`;
- `Scene/008 Rollin/Kit Rollin/kitset.kcg` declares those same six types and
  filenames, and all six files exist;
- `.hcprms1` is generation 7 and `.hcprms2` is generation 8. The newer record
  still describes Bank 002 `LoadTst`, as expected when the Load/Save page guard
  prevented publication.

Consequently the matching-winner reader is not selected. The two HCNAMES
authority checks pass (row 0 agrees with settings Bank 001, and all rows have
`R`), so `filesystem_bootHcnamesAuthoritativeLoad()` is the path that must
reconstruct the resident Bank.

The `Scene/008 Rollin` and `Bank/001 Full` trees in `SD_CARD_READER_4` and
`SD_CARD_READER_8` compare byte-for-byte. In particular, all seven files in
`Scene/008 Rollin/Kit Rollin` are identical between the pre-boot and post-boot
copies. There is no malformed Scene, Kit, or Instrument file to repair.

## 3. Decisive hardware evidence

`SD_CARD_READER_8/LXRV2_lxr02.img` is SHA-256-identical to the current
`build/LXRV2_lxr02.img`. That image contains both the per-Scene type snapshot
attempt and the synchronous root-`chdir` change.

The `SD_CARD_READER_8/asavetrc.bin` `Q` records show:

- Scene 0: Scene row 1, Kit row 17, and all six Instrument rows 33..38 complete
  Case 2 successfully from source 008;
- Scene 1: Scene row 2 and Kit row 18 complete successfully from the same
  source 008, then Instrument row 39 immediately becomes Case 3;
- the Scene-1 Kit success and Instrument failure both have tick 1424. The
  Instrument failure therefore occurs before a meaningful file transaction;
- the final summary is raw value `0xfffeffff`: Case-2 mask `0xffff`, Case-3
  mask `0xfffe`. Scene 0 survives; every later Scene is emptied;
- post-boot HCNAMES agrees exactly: Scene 0 retains `008/-/-`, while Scenes
  1..15 have all eight sources rewritten to `?` by
  `filesystem_bootReaderEmptyScene()`.

Scene 0 and Scene 1 resolve to the same physical `Scene/008 Rollin` tree.
Scene 0 successfully loading all six files proves that Scene 1's first-file
failure is not a bad file, path, name, type column, or kitset declaration.

The improvement from `SD_CARD_READER_7` is also diagnostic. Before the
six-type local snapshot, Scene 0 failed partway through its own Instrument
rows. With the snapshot, all of Scene 0 succeeds, but the first Instrument of
Scene 1 fails. This is the exact boundary predicted by an intra-Scene-only
snapshot of a cross-Scene lifetime.

## 4. Exact failure chain in the current source

1. At `filesystem.c:27295-27297`, each parsed Instrument type is written to
   `fs_stage_workspace.boot_reader_type[scene * 6 + slot]`.
2. That array is a member of the 2,048-byte staging union at
   `filesystem.c:952-977`.
3. At `filesystem.c:27893-27899`, the authoritative loader copies only the
   current Scene's six entries immediately before evaluating that Scene.
4. Scene 0 gets a valid copy because the union still contains the freshly
   parsed 96-byte type image.
5. Scene 0's narrow Scene load writes `fs_stage_workspace.scene_stage`; its
   narrow Kit load clears and fills `op_staged_kit`; each narrow Instrument
   load writes `op_staged_instrument`. All are aliases of
   `boot_reader_type`.
6. When the outer loop reaches Scene 1, bytes 6..11 of
   `boot_reader_type` are no longer parsed type tokens. They are bytes from the
   last staged payload. Some corrupted bytes can accidentally equal enum values
   0..3, explaining the varying number of apparent Instrument successes in
   later Scenes; the first out-of-range or wrong-but-valid value stops that
   Scene.
7. `filesystem_bootReaderNarrowLoadInstrument()` rejects an out-of-range type
   at its initial guard, or rejects a wrong-but-valid type against
   `op_kitset.instrument_type[instrument_slot]`. It returns zero.
8. The caller converts that false local failure into Case 3, invokes the
   all-or-nothing rule, empties the Scene, and changes its eight source rows to
   `UNKNOWN|REFRESHED` (`? R`).

The same latent defect exists in the matching-winner path. Its
`filesystem_bootReaderEvaluateScene()` currently takes the same too-late
six-byte snapshot at `filesystem.c:27518-27522`; after Scene 0 performs a
Case-2 load, later Scenes can read overwritten types in exactly the same way.

## 5. Precise targeted fix

Replace the standalone declaration of the existing 144-byte Bank-child display
cache with an explicitly shared operation-scratch union:

```c
#define FS_BOOT_READER_INSTRUMENT_TYPE_COUNT \
    (AUTOSAVE_SCENE_COUNT * AUTOSAVE_INSTRUMENTS_PER_KIT)

typedef union {
    char bank_child_display[STORAGE_BANK_SCENE_MAX_SLOTS]
                           [STORAGE_SCENE_DISPLAY_NAME_LEN + 1u];
    uint8_t boot_reader_type[FS_BOOT_READER_INSTRUMENT_TYPE_COUNT];
} filesystem_bank_child_scratch_t;

static filesystem_bank_child_scratch_t op_bank_child_scratch;
```

The two views have non-overlapping lifetimes:

- `boot_reader_type[96]` is owned by the Stage-11 boot reader from HCNAMES
  parsing/regeneration through the last Scene row evaluation;
- `bank_child_display[16][9]` is owned by a normal asynchronous Bank Load from
  its Bank-child scan through consumption of the cached child names.

These lifetimes cannot overlap. The boot readers use blocking narrow loaders,
and `filesystem_bootNarrowLoadBank()` uses its own local `display` buffer rather
than `op_bank_child_display`. If a boot reader declines, the canonical Bank
Load starts a new filesystem operation; `filesystem_start()` clears the Bank
child scratch before the Bank scan repopulates it. The root Bank existence
check is unaffected because it uses the separate shared library-name cache.

Remove `boot_reader_type[]` from `filesystem_stage_workspace_t`. Change
`filesystem_bootReaderApplyRowType()` and
`filesystem_bootReaderSeedInstrumentTypes()` to write directly to
`op_bank_child_scratch.boot_reader_type`. The complete parsed type image then
never occupies the destructive payload-staging union and does not require a
second copy.

### 5.1 Matching-winner path

In `filesystem_autosaveBootReaderBlocking()`:

1. let a successful register parse populate the new scratch view directly;
2. after winner-based HCNAMES regeneration, let
   `filesystem_bootReaderSeedInstrumentTypes()` populate that same view from
   the regenerated resident Scene types;
3. remove `filesystem_bootReaderEvaluateScene()`'s six-byte local array and
   copy loop;
4. select each Case-2 Instrument type directly from the immutable scratch:

```c
instrument_type_t type = (instrument_type_t)
    op_bank_child_scratch.boot_reader_type[
        (uint16_t)scene_index * AUTOSAVE_INSTRUMENTS_PER_KIT + slot];
```

No function signature change or full-image `memcpy()` is necessary.

### 5.2 HCNAMES-authoritative path

In `filesystem_bootHcnamesAuthoritativeLoad()`:

1. let the successful register parse populate the new scratch view directly;
2. remove the six-byte array and copy loop from inside the per-Scene loop;
3. select each Instrument type only from the immutable full image:

```c
instrument_type_t type = (instrument_type_t)
    op_bank_child_scratch.boot_reader_type[
        (uint16_t)scene_index * AUTOSAVE_INSTRUMENTS_PER_KIT + slot];
```

### 5.3 Existing Bank Load references, reset, and assertions

Change the existing runtime Bank Load users of `op_bank_child_display` to the
`op_bank_child_scratch.bank_child_display` member. In generic request setup,
clear the whole union:

```c
memset(&op_bank_child_scratch, 0, sizeof(op_bank_child_scratch));
```

Retain the existing 144-byte SRAM accounting and add compile-time checks for
both views:

```c
_Static_assert(
    sizeof(op_bank_child_scratch.boot_reader_type) == 96u,
    "boot reader must retain exactly 16 x 6 Instrument types");
_Static_assert(
    sizeof(op_bank_child_scratch) == 144u,
    "shared Bank-child/boot-reader scratch must remain 144 bytes");
```

Update the aggregate Option-1 SRAM assertion and SRAM manifest to name the
shared scratch while retaining the same 144-byte total. The final 48 bytes of
the union are simply unused while the boot-reader type view owns it.

### 5.4 Comments and invariant

Update the scratch declaration and the two reader comments to state the real
lifetime invariant:

> All 96 HCNAMES Instrument types live in the Bank-child operation scratch,
> outside the payload-staging union, from register parsing through the last
> Scene row evaluation. A per-Scene copy from payload staging is insufficient
> because loading an earlier Scene destroys the still-needed entries of later
> Scenes. Normal Bank Load may reuse the scratch only after the boot reader
> returns; `filesystem_start()` resets it before the Bank-child scan.

No public header or file-format change is needed.

## 6. Scope exclusions

Do not fix this by weakening
`filesystem_bootReaderNarrowLoadInstrument()`'s type checks. Those checks
prevent a descriptor image from being interpreted as the wrong Instrument
type and are correct when given the real HCNAMES token.

Do not derive an Instrument row's type from the just-loaded parent kitset or
from the resident slot after the Kit load. An independently sourced
Instrument row may legitimately override its parent's type; the mandatory
HCNAMES type token is the authority for that row.

Do not add another HCNAMES rewrite, directory scan, fallback Bank load, or
Case-3 exception. HCNAMES names/sources/types are correct before boot, and P1
is acting correctly after it receives a false `load_ok == 0` result.

Do not borrow `fs_list_cache_name`. Stage 10 deliberately reloads the root Bank
index into that cache, and `main.c` calls `filesystem_bankSlotExists()` after a
reader decline but before canonical Bank Load starts. Repurposing or disposing
that cache during the reader would make a valid fallback Bank appear absent or
would require an unnecessary index reload. `op_bank_child_scratch` has the
required non-overlapping lifetime without disturbing that decision.

The current `afatfs_chdir(NULL)` synchronization change is not the cure for
this failure: it is already present in the byte-identical Reader-8 image, yet
Scenes 1..15 still fail at the predicted type-lifetime boundary. Retain or
revert that lower-layer change only on its own evidence; it is outside this
targeted fix.

## 7. RAM impact and ownership boundary

The reader needs exactly 96 stable bytes: 16 resident Scenes x 6 Instruments x
one `uint8_t` type token. Names and source/refreshed metadata already have
separate durable storage and do not belong in this image.

The fix adds **zero bytes** to `.bss`, `.data`, DTCM, heap, or the main stack.
It overlays those 96 bytes on the existing, already-accounted 144-byte
`op_bank_child_display` allocation and removes the current six-byte per-Scene
automatic arrays. The resulting source-level stack use therefore decreases by
six bytes in each affected reader frame rather than increasing by 90 bytes.

Owner and lifetime are explicit:

- boot-reader view: pre-audio Stage 11, from HCNAMES parse/regeneration through
  completion of all Scene row evaluations;
- Bank-child display view: a canonical/runtime Bank Load, from its child scan
  through the delegated Scene opens;
- transition: the reader returns before canonical fallback starts, and every
  new filesystem request clears the shared operation scratch.

No new-RAM acknowledgement is required because the retained 144-byte
allocation and its peak remain unchanged. The ownership/comment/assertion
changes must nevertheless be included so a future edit cannot make the two
views live concurrently.

## 8. Acceptance test

Re-run a fresh copy of `SD_CARD_READER_4`; do not use the already-mutated
post-boot Reader-7/8 HCNAMES files.

Required result:

- no `AutoSave Sc NN empty` notice;
- resident Scenes 0, 1, 14, and 15 load and play root Scene 008 `Rollin`;
- resident Scenes 2..13 load and play their Bank-001 child content;
- every per-row boot `Q` record is flags `0x04`; there are no flags `0x02`
  records;
- the summary has Case-2 mask `0xffff` and Case-3 mask `0x0000`.

The raw summary value must be **`0x0000ffff`**, because the implementation
packs `case2_scene_mask` into bits 0..15 and `case3_scene_mask` into bits
16..31. The `0xffff0000` expected value written in
`061_READER_LOADED_SCENES_INVALID.md` sections 18.2 and 19 has the halves
reversed and should not be used as the test oracle.

After the runtime drain, `R` flags may legitimately clear, but the four Rollin
Scene rows must remain source `008`, their Kit rows source `-`, and their 24
Instrument rows source `-`; no row may become `?`.

Also run one matching-winner mixed Case-1/Case-2 fixture. It must prove the
same stable 96-byte scratch view is used by
`filesystem_autosaveBootReaderBlocking()`, not only by the
HCNAMES-authoritative path exercised by `SD_CARD_READER_4`.
