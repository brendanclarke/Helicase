# Autosave Parameter Drain — targeted implementation plan

## Status

This document is an implementation plan only. No firmware source, autosave
fixture, or SD-card image is changed by creating this plan.

The plan is based on the source currently in this working tree. It does not use
the archived directory-scan proposal, historical autosave implementations, or
older specifications as implementation authority.

## Accepted scope

This milestone does exactly four things:

1. replaces the current 23,248-byte autosave geometry with the requested
   34,768-byte geometry;
2. reads the mutation mask from the newest valid ping-pong record;
3. snapshots a configured, bounded number of dirty live parameter bytes and
   copy-forwards them into the inactive record while clearing only the
   corresponding mask bits; and
4. prepares `SD_CARD/.hcprms1` and `SD_CARD/.hcprms2` as valid new-format test
   records with the first resident Scene's parameter allocations dirty.

This milestone does **not**:

- add runtime dirty-mark calls to Menu, Preset, BankData, SceneData, or load
  completion paths;
- restore parameters from either autosave file;
- require either record to contain a complete Bank snapshot;
- autosave Pattern data;
- implement Effect state;
- read or rewrite HCNAMES during an ordinary parameter drain;
- share storage with the existing 9,000-byte name cache;
- change the 512-byte SD streaming buffer;
- add a foreground wait, blocking runtime filesystem call, or second
  filesystem owner;
- change the existing generation, CRC32C, final-commit, duplicate-removal, or
  no-valid-record recovery policies except where the new record size/offsets
  require them to use the new geometry.

The current one-byte probe counter remains in the header and continues to
increment on each successfully committed background transaction. It remains a
diagnostic witness; generation remains the ping-pong winner selector.

## Important interpretation of the revised format

The latest requested layout explicitly puts the two-byte Bank slot back into
the Bank section. This supersedes the current code's "no Bank slot" layout for
this milestone. The source is `bank_restoreBankSlot()`, encoded little-endian.

The 64-byte control header remains unchanged in shape:

- magic;
- format version;
- final commit marker;
- generation;
- CRC32C;
- dummy/probe counter; and
- zero padding.

The phrase "validation, CRC, dummy counter, and padding section" is therefore
the existing 64-byte header, not another section after the payload.

The format version remains `1` in this targeted pass. Exact-size validation
rejects all old 23,248-byte records. No format migration or multi-version
reader is introduced.

## Exact revised wire geometry

The arithmetic is:

```text
Bank payload                         128
16 Scenes × 1,920                 30,720
                                     ------
payload                           30,848

payload / 8 mutation bits          3,856
control header                        64
                                     ------
record                            34,768 bytes
```

### Absolute top-level ranges

| Absolute range | Size | Contents |
| --- | ---: | --- |
| `0..63` | 64 | Existing validation/generation/CRC/probe header |
| `64..3919` | 3,856 | One mutation bit for every payload byte |
| `3920..4047` | 128 | Bank section |
| `4048..34767` | 30,720 | Sixteen 1,920-byte Scene sections |

The first Scene starts at absolute offset `4048`. Scene `N` starts at:

```text
4048 + (N × 1920)
```

### Mutation-bit convention

Mask bits address payload-relative offsets, not absolute file offsets:

```text
payload_offset = absolute_record_offset - AUTOSAVE_PAYLOAD_OFFSET
mask_byte      = payload_offset >> 3
mask_bit       = 1u << (payload_offset & 7u)
```

Bit zero of a mask byte covers the lowest-address payload byte in that group.
The mask never covers the 64-byte control header.

A set bit means the corresponding payload byte still needs to be sampled from
the live resident Bank. A clear bit means there is no pending live-byte sample
for that payload position.

### Bank section, relative to its 128-byte start

| Relative range | Size | Meaning |
| --- | ---: | --- |
| `0..1` | 2 | Bank restore slot, little-endian |
| `2..9` | 8 | Zero-padded Bank name |
| `10..11` | 2 | Scene-present mask, little-endian |
| `12` | 1 | Active Scene slot |
| `13..14` | 2 | VOICE edit Scene mask, little-endian |
| `15..127` | 113 | Reserved/zero padding |

The two Bank masks require two bytes each even though every stored cell is
addressed one byte at a time by the mutation mask. `bank_hasResidentBank()` is
not stored: the scheduler already requires it before autosave can run.

### One 1,920-byte Scene section

| Scene-relative range | Size | Meaning |
| --- | ---: | --- |
| `0..127` | 128 | Scene name, parameters, padding |
| `128..639` | 512 | Reserved Effect record |
| `640..1919` | 1,280 | Kit |

The Scene name occupies relative bytes `0..7`. Scene parameters begin at byte
`8` in this order:

| Scene parameter index | Size | Live source |
| --- | ---: | --- |
| `0` | 1 | `scene->settings.morph_amount` |
| `1..6` | 6 | `voice_morph_amount[0..5]` |
| `7` | 1 | `voice_decimation_all` |
| `8..13` | 6 | `audio_out[0..5]` |
| `14..19` | 6 | `fx_send_amount[0..5]` |
| `20..25` | 6 | `fader_setting[0..5]` |
| `26..32` | 7 | `midi_channel[0..6]` |
| `33..39` | 7 | `midi_note[0..6]` |
| `40..119` | 80 | No current Scene parameter; reserved padding |

This is the exact field order already emitted by
`filesystem_nextScenesetLine()`, expressed as binary bytes rather than text.
`scene_t.pattern` is never addressed.

### Reserved 512-byte Effect section

| Effect-relative range | Size | Meaning |
| --- | ---: | --- |
| `0` | 1 | Effect type; currently nonexistent/zero |
| `1..8` | 8 | Effect name; currently zero |
| `9..511` | 503 | Future parameters and zero padding |

Every dirty Effect byte is classified as nonexistent in this milestone. Its
bit is cleared without requesting a live value and its payload byte is
copy-forwarded unchanged. No Effect module or placeholder parser is changed.

### One 1,280-byte Kit

| Kit-relative range | Size | Meaning |
| --- | ---: | --- |
| `0..7` | 8 | Zero-padded Kit name |
| `8` | 1 | `slot6_track7_amp_envelope_decay` |
| `9` | 1 | `slot6_track7_morph_amp_envelope_decay` |
| `10..127` | 118 | No current Kit parameter; reserved padding |
| `128..1279` | 1,152 | Six 192-byte Instrument records |

### One 192-byte Instrument record

| Instrument-relative range | Size | Meaning |
| --- | ---: | --- |
| `0..2` | 3 | Lowercase type token: `drm`, `snr`, `cym`, or `hat` |
| `3..10` | 8 | Zero-padded Instrument name |
| `11..82` | 72 | Normal parameter cells |
| `83..154` | 72 | Morph parameter cells |
| `155..191` | 37 | Reserved/zero padding |

Both 72-byte parameter areas are descriptor-indexed rather than compact:

- normal cell `D` maps to
  `instrument_parameters[D]` when `D < entry->descriptor_count`;
- Morph cell `D` maps to `morph_instrument_parameters[D]` only when
  `D < entry->descriptor_count` and descriptor `D` carries
  `INSTRUMENT_PARAM_FLAG_MORPHABLE`;
- all other cells are nonexistent and have their dirty bits cleared without a
  live read.

This mapping is stable, cheap, and directly follows each instrument's
parameter enum because the current descriptor arrays are compile-time checked
against those enums. It also avoids writing `morph_interpolation[]`, which is
derived runtime state.

Current descriptor counts are Drum 39, Snare 38, Cymbal 39, and HiHat 39, so
72 cells provide explicit format reserve without changing the present mapping.

## Current-code facts governing the implementation

### Autosave format and validation

`Core/Bank/Scene/Autosave.c/.h` currently own:

- the 64-byte header constants;
- the old 2,576-byte mask and 20,608-byte payload geometry;
- deterministic name-only initial-record formatting;
- streaming full-record validation;
- CRC32C with bytes `12..15` treated as zero;
- generation comparison;
- the header-only chunk transform; and
- the whole-image validation wrapper.

The initial formatter currently knows Bank/HCNAMES names but no Bank slot or
live parameter source. The transform currently promises to preserve every mask
and payload byte. Both contracts must be revised explicitly for this
milestone.

### Runtime writer

`filesystem_autosaveDummyWrite_tick()` already:

1. validates A and B by streaming;
2. selects the newest valid record;
3. makes two deterministic passes over the winner:
   - one to calculate the transformed target CRC;
   - one to write the inactive target;
4. removes all duplicate/case variants of the inactive filename;
5. writes the target with commit clear;
6. syncs;
7. writes the commit marker last; and
8. syncs again through `filesystem_finish()`.

That transaction remains the base. The parameter drain is inserted after
winner selection and before the target CRC pass. Both later passes consume the
same cached mask and captured patch list.

The no-valid-record branch currently reads HCNAMES and regenerates B then A.
It remains name-only/zero-parameter recovery in this milestone, using only the
new geometry and Bank slot. This is intentionally not a complete-register
implementation.

### Filesystem ownership and buffers

- `filesystem.c` owns the only filesystem operation slot.
- `filesystem_tick()` owns all runtime `afatfs_poll()` progress.
- `staging_buf[512]` remains the only SD streaming chunk.
- `filesystem_stage_workspace_t` is a 2,048-byte union containing transient
  load stages and the current autosave scalar state.
- `fs_list_cache_name[1000][9]` is exactly 9,000 bytes and remains untouched by
  an ordinary parameter drain.
- `menu_activePage` is already exported, and the scheduler already suppresses
  autosave starts on `LOAD_PAGE` and `SAVE_PAGE`.

The new mutation/patch cache must therefore be a separate autosave-only BSS
object. It must not be added to the 2,048-byte stage union and must not alias
`fs_list_cache_name` yet.

### Live parameter ownership

- Bank values come from the public `BankData` getters.
- `scene_getConst(scene_index)` returns the authoritative retained Scene.
- Scene settings and Kit settings are byte-valued fields.
- each Instrument slot carries its `type`, main endpoint image, Morph endpoint
  image, and derived interpolation image;
- `instrumentManager_registryEntry(type)` supplies the descriptor table,
  descriptor count, three-character type text, and Morph flags.

No DSP getter and no walk of the active-scene `parameter_values[]` array is
required. Raw C structs are never copied to disk.

## Configured drain budget

Add this tuning constant beside `AUTOSAVE_WRITER_INTERVAL_MS` in `config.h`:

```c
#define AUTOSAVE_PARAMETER_GETS_PER_WRITE 256u
```

It means at most 256 successful live payload-byte samples are captured during
one five-second autosave transaction. With the current 5,000 ms cadence, the
configured ceiling is approximately 51 live-byte samples per second, although
all I/O remains asynchronous and ordinary filesystem work may defer it.

Inspecting and clearing a set bit that maps to padding, an absent Scene, an
unknown Instrument type, an out-of-range descriptor, or a non-Morphable Morph
cell does not consume the live-get budget because no live owner is read.

Add a second cooperative-work constant:

```c
#define AUTOSAVE_MASK_BITS_PER_TICK 256u
```

The mask preparation phase examines no more than this many payload positions
per `filesystem_tick()` call. This prevents a single foreground pass from
walking all 30,848 bits, even when a fixture deliberately marks large reserved
regions dirty.

Both constants receive adjacent comments defining their units, their
relationship to the five-second transaction cadence, and the distinction
between examined mask positions and successful live parameter gets.

## Dedicated cache design

Add one new private type and static object in `filesystem.c`:

```text
updated_mask[3,856]                     3,856 bytes
payload_offsets[256] × uint16_t           512 bytes
payload_values[256]                       256 bytes
                                           -----
minimum explicit data                    4,624 bytes
```

The concrete type also carries only any alignment/padding required by those
arrays. Scalar cursors and counts remain in
`filesystem_autosave_writer_state_t`, which already lives in the mutually
exclusive 2,048-byte operation stage.

Add compile-time assertions that:

- `AUTOSAVE_PAYLOAD_BYTES <= UINT16_MAX`, allowing 16-bit patch offsets;
- the patch arrays have exactly `AUTOSAVE_PARAMETER_GETS_PER_WRITE` entries;
- the dedicated cache is at most 9,000 bytes; and
- it is a separate object from `fs_list_cache_name`.

The mask is loaded byte-for-byte from the selected winner before mutation.
The patch offsets are appended in ascending payload order, so a streaming
chunk can apply them without searching the entire list.

The cache is cleared/initialized only when the autosave operation starts. It
is not cleared by generic filesystem requests, is not exposed in
`filesystem.h`, and is not borrowed by Menu or HCNAMES.

## Parameter getter contract

Add a narrow public format helper in `Autosave.h/.c`:

```c
uint8_t autosave_getLivePayloadByte(uint16_t payload_offset,
                                    uint8_t *value);
```

Its behavior is:

- input: one payload-relative byte offset in `0..30847`;
- output `1`: the offset maps to a currently existing live Bank/Scene/Kit/
  Instrument value and `*value` receives its byte;
- output `0`: no live value exists for that offset in this milestone, and the
  caller may clear the dirty bit without making a parameter request.

The helper must:

- use explicit offset arithmetic and field access;
- return Bank scalar bytes in the documented little-endian order;
- reject absent Scene slots using `bank_scenePresentMask()`;
- skip Scene names, Kit names, Instrument names, and all padding because this
  milestone does not borrow HCNAMES;
- return the three type-token bytes for a registered Instrument slot;
- validate normal/Morph descriptor indices through the live type's registry
  entry;
- reject non-Morphable Morph cells;
- never return `morph_interpolation[]`;
- never access Pattern or Effect state;
- return zero safely for a null output pointer or out-of-range offset; and
- perform no I/O, allocation, dirty-bit mutation, or runtime DSP apply.

Although type bytes are metadata rather than parameters, they are included in
the getter because they are resident in `scene_t` and are necessary to
interpret the two following descriptor-indexed arrays.

Every region branch receives an adjacent comment identifying its input
subrange, output source, absent-value behavior, and relevant owner
(`BankData`, `SceneData`, or `InstrumentManager`).

## Pure mask and chunk helpers

Add small wire-format helpers in `Autosave.h/.c` rather than duplicating bit
math in the filesystem state machine:

1. `autosave_maskBitIsSet(mask, payload_offset)`
   - bounds-checks the 3,856-byte caller-owned mask;
   - returns the defined least-significant-bit-first state.
2. `autosave_maskBitClear(mask, payload_offset)`
   - clears exactly one bounded payload bit;
   - never changes a neighboring bit.
3. `autosave_transformDrainChunk(...)`
   - first performs the existing generation/probe/CRC/commit header transform;
   - substitutes bytes from the cached updated mask wherever the chunk
     intersects the mask range;
   - substitutes captured payload values wherever a sorted patch offset falls
     inside the chunk;
   - leaves every unpatched payload byte unchanged.

The transform receives the mask and patch list as caller-owned immutable input.
It never gets a live parameter itself. The CRC pass and copy pass reset their
own patch cursors and invoke exactly the same transformation, guaranteeing that
the bytes checksummed are the bytes written even if the user changes a
parameter between passes.

The existing `autosave_transformChunk()` may be retained as the header-only
primitive called by the new helper, or replaced if all call sites move
atomically. Its declaration and comments must no longer claim that the runtime
writer always leaves the mask/payload untouched.

## Detailed file-by-file changes

### `config.h`

1. Add `AUTOSAVE_PARAMETER_GETS_PER_WRITE` with the value `256u`.
   - **What:** caps successful live-byte snapshots per completed debounce.
   - **Why:** bounds the parameter-side foreground work and patch-cache size.
   - **Input:** the dirty mask loaded from the winner.
   - **Output:** maximum patch count for one target generation.
   - **Affiliates:** autosave cache array dimensions and mask-drain state.
2. Add `AUTOSAVE_MASK_BITS_PER_TICK` with the value `256u`.
   - **What:** caps mask positions examined in one foreground tick.
   - **Why:** keeps deliberately dirty padding from creating one long CPU
     loop.
   - **Input:** retained scan cursor.
   - **Output:** next scan cursor or preparation completion.
   - **Affiliates:** `filesystem_autosaveDummyWrite_tick()`.
3. Keep `AUTOSAVE_WRITER_INTERVAL_MS` at 5,000 ms.
   - The new constants are budgets inside that existing cadence, not another
     timer.

All three declarations must have adjacent unit/ownership comments.

### `Core/Bank/Scene/Autosave.h`

1. Replace the old geometry constants with:
   - header `64`;
   - mask `3,856`;
   - Bank `128`;
   - Scene `1,920`;
   - Effect `512`;
   - Kit `1,280`;
   - Instrument `192`;
   - payload `30,848`;
   - record `34,768`.
2. Add named offsets/sizes for every field described above:
   - payload start;
   - Bank slot/name/parameter fields;
   - Scene parameter start/count;
   - Effect start/type/name;
   - Kit start/name/parameter fields;
   - Instrument type/name/normal/Morph/padding fields.
3. Add static assertions proving:
   - mask bits exactly equal payload bytes;
   - Bank plus sixteen Scenes exactly equals payload;
   - Scene + Effect + Kit equals 1,920;
   - Kit header plus six Instruments equals 1,280;
   - type + name + two 72-byte parameter images fits one 192-byte Instrument;
   - record size is exactly 34,768;
   - all live descriptor counts fit the 72-byte storage allocation; and
   - payload offsets fit the patch cache's `uint16_t`.
4. Extend initial formatter/CRC declarations with the Bank slot input.
   - **Why:** the new Bank slot cells must be deterministic in newly generated
     records and their CRC calculation.
5. Extend streaming validation state with the parsed Bank slot.
6. Replace the Bank-name-only identity helper with a Bank identity helper that
   compares both normalized Bank name and `bank_restoreBankSlot()`.
7. Declare the live-byte getter, mask helpers, and drain-aware chunk transform
   with full input/output/storage-ownership comments.
8. Update every stale 23,248-byte and delta-only comment adjacent to affected
   declarations.

No scheduler, filesystem handle, cache object, or static mutable state is added
to this header.

### `Core/Bank/Scene/Autosave.c`

1. Update `autosave_initialRecordByte()` to the new offsets.
   - Emit the Bank slot in bytes `0..1` of the Bank section.
   - Shift Bank name to bytes `2..9`.
   - Recalculate Scene/Kit/Instrument name positions from the new constants.
   - Continue zeroing all masks, parameters, Effects, and padding in a newly
     generated baseline.
2. Thread Bank slot through `autosave_initialRecordCrc()` and
   `autosave_formatInitialChunk()`.
3. Update streaming validation to parse the two Bank slot bytes and enforce
   the new total size.
4. Update the identity comparison to require the live Bank name and slot.
5. Implement the descriptor-indexed live payload getter exactly as specified
   above.
   - Include `BankData.h`, `SceneData.h`, and the registry contract required
     for read-only live access.
   - Do not include Menu or filesystem headers.
6. Implement the bounded mask-bit helpers.
7. Implement the drain-aware chunk transformer.
   - It applies only captured patch values.
   - It replaces the complete intersecting mask range with the cached updated
     mask, preserving all unprocessed dirty bits.
   - It remains safe for 512-byte chunks beginning at arbitrary record
     offsets.
8. Keep the existing table-free CRC32C rule unchanged except for the larger
   record length.
9. Update the file-level comment:
   - this module now owns the binary payload-to-live-value mapping;
   - it still owns no I/O, scheduler, or persistent cache.

Each added mapping branch and helper receives adjacent comments describing
what it maps, why the mapping belongs here, its inputs/outputs, and its code
affiliates.

### `Core/Hardware/SD/filesystem.c`

1. Rename the runtime operation and function from "dummy write" to
   "parameter drain" terminology.
   - Update the enum entry, prototype, dispatcher, error prefix, state-machine
     banner, and comments.
   - Keep the private operation; do not add a public request API.
2. Extend `filesystem_autosave_writer_state_t` with scalar preparation state:
   - mask read offset and seek target;
   - payload scan cursor;
   - current transaction get count;
   - CRC-pass and copy-pass patch cursors;
   - a flag recording whether the cached mask differs from the source.
3. Add the dedicated approximately 4.7 KB cache described above.
   - It is a new static BSS object.
   - It is never a union member or alias of `fs_list_cache_name`.
   - Add the 9 KB ceiling assertions beside existing cache assertions.
4. After A/B validation and winner selection, add asynchronous phases to:
   - reopen the winner;
   - seek to `AUTOSAVE_MASK_OFFSET`;
   - wait for the seek cursor using the existing `afatfs_fseek()` /
     `afatfs_ftell()` pattern;
   - read exactly 3,856 mask bytes into the dedicated cache, accepting partial
     reads and advancing only by the accepted count;
   - close the mask reader before preparation.
5. Add cooperative mask preparation phases.
   - Examine at most `AUTOSAVE_MASK_BITS_PER_TICK` positions per tick.
   - Skip clear bits.
   - For a set bit, call `autosave_getLivePayloadByte()`.
   - If a live byte exists and fewer than 256 values have been captured,
     append its payload offset/value and clear its cached mask bit.
   - If no live byte exists, clear its cached mask bit without consuming the
     get budget.
   - If the get budget is exhausted, stop at that position and leave that bit
     and every later dirty bit set for a future generation.
   - When the payload end is reached, proceed even if no live value was found:
     cleared invalid bits still need to be persisted.
6. Modify the target CRC pass.
   - Reset its patch cursor.
   - Transform every winner chunk using the next generation/probe, updated
     cached mask, and captured patch list.
   - Calculate CRC over that transformed stream with commit valid and CRC bytes
     logically zero as today.
7. Modify the target copy pass identically.
   - Reset the second patch cursor.
   - Apply the cached mask and captured values while commit remains clear.
   - Preserve partial-write behavior and all existing duplicate-removal,
     close, intermediate-sync, reopen, commit-last, and final-sync phases.
8. Update no-valid-record recovery and boot creation call sites to pass
   `bank_restoreBankSlot()` and use the new size.
   - Recovery still emits names plus zero parameters/mask.
   - It does not attempt to synthesize a complete live Bank.
9. Preserve the existing Load/Save-page gate.
   - No new parameter-drain transaction starts while `menu_activePage` is
     `LOAD_PAGE` or `SAVE_PAGE`.
   - An operation accepted immediately before page entry is not abandoned
     midway through its CRC/commit transaction; it completes through the
     existing durability boundary.
10. Preserve autonomous completion behavior.
    - The callback schedules the next five-second interval and acknowledges
      only its own DONE/ERROR result.
    - Remaining dirty mask bits live in the newly committed winner and are
      discovered again at the next debounce; no separate persistent dirty
      ledger is added.

Every new field, cache, phase group, and non-obvious branch receives adjacent
comment text describing what it owns, why it exists, inputs, outputs, and
affiliates.

### `Core/Hardware/SD/filesystem.h`

No new public runtime API is needed. Update only existing autosave creation
documentation:

- exact 34,768-byte size;
- Bank slot now present;
- new section geometry;
- boot creation still writes names/slot with zero mask/parameters; and
- runtime parameter drain remains private to `filesystem_tick()`.

These are comment-only header changes but are required so the public boot
contract does not continue advertising the old record.

### Files intentionally unchanged

- `main.c`: boot already calls the creation wrapper in the correct place.
- `Core/Menu/menu.c/.h`: `menu_activePage`, `LOAD_PAGE`, and `SAVE_PAGE`
  already provide the required start gate.
- `Core/Bank/BankData.c/.h`: all required Bank getters already exist.
- `Core/Bank/Scene/SceneData.c/.h`: `scene_getConst()` and retained fields
  already exist.
- `Core/DSP/Instruments/InstrumentManager.c/.h`: registry entry, descriptor
  count, type text, and flags already exist.
- all four `*Parameters.c` files: their enum/descriptor ordering is already
  the authoritative map.
- Makefile: `Autosave.c` is already linked.
- asyncfatfs: the existing streamed read/write/seek/close/sync primitives are
  sufficient.

## Test-fixture preparation

The implementation turn will regenerate both:

```text
SD_CARD/.hcprms1
SD_CARD/.hcprms2
```

They must each be exactly 34,768 bytes and validate under the firmware's new
size/header/CRC rules. The baseline roles remain:

- `.hcprms1`: generation 1, current;
- `.hcprms2`: generation 0, previous;
- both: probe counter zero;
- both: Bank slot `000` and Bank name `Full`;
- both: identical initial mutation masks.

The current copied fixtures are not usable as the new baseline:

- `.hcprms1` is presently 32,768 bytes;
- `.hcprms2` is presently 23,248 bytes.

Because the new record crosses the 32 KiB boundary, hardware verification must
explicitly prove that both created/copied records reach byte 34,767 rather
than assuming a successful open/close implies the complete length.

### Meaning of "Scene 1" for this plan

This plan interprets Scene 1 as the first physical Scene/SEQ position:

```text
resident Scene index 0
Bank child directory 00
HCNAMES Scene row 1
```

Its payload starts at payload-relative offset `128` and absolute file offset
`4048`.

If "Scene 1" instead means zero-based Scene index 1 / Bank child `01`, only the
fixture's marked mask range changes; no firmware mapping changes.

### Bits set in the initial test fixtures

For the first Scene, set bits for the complete allocated parameter/type
regions, including deliberately nonexistent cells:

- Scene parameter allocation: Scene-relative `8..127`;
- complete Effect section except its already-correct name cells:
  Scene-relative `128` and `137..639`;
- Kit parameter allocation: Scene-relative `648..767`;
- each Instrument type: Instrument-relative `0..2`;
- each Instrument normal allocation: Instrument-relative `11..82`;
- each Instrument Morph allocation: Instrument-relative `83..154`.

Do not set name bits. Initial formatting already writes the HCNAMES-derived
Scene, Kit, and Instrument names, and ordinary parameter drain deliberately
does not borrow HCNAMES in this milestone.

This fixture intentionally sets bits for:

- Scene padding after the 40 existing Scene parameters;
- all currently nonexistent Effect values;
- Kit padding after the two existing Kit parameters;
- normal descriptor indices beyond the live type's descriptor count; and
- Morph indices that are out of range or non-Morphable.

Those bits prove the required "nonexistent parameter closes without a live
request" behavior.

After setting the masks, recompute and store each record's CRC32C using the
normal rule. Do not change the generation or probe values merely because the
fixture was prepared.

## Expected hardware behavior

With Bank `000 Full` loaded:

1. after the normal interval, the writer selects `.hcprms1` generation 1;
2. it loads that record's 3,856-byte mask;
3. it clears all examined nonexistent cells and captures at most 256 existing
   live bytes;
4. it writes `.hcprms2` as generation 2/probe 1 with:
   - captured parameter/type bytes updated;
   - their dirty bits clear;
   - invalid examined bits clear;
   - any unprocessed valid dirty bits still set;
5. the next interval selects `.hcprms2` and continues into `.hcprms1`;
6. after enough intervals, the newest valid generation has no dirty bits in
   the marked first-Scene regions and its stored values match the resident
   Scene loaded from `Bank/000 Full/00 Slak`.

The older record is expected to lag by one transaction. Validation must always
identify the newest valid generation before comparing values or mask state; it
is incorrect to require both physical files to show identical cleared masks at
an arbitrary power-off time.

Entering Load or Save before a due interval prevents a new drain from starting.
Leaving the page allows the overdue drain to start once the filesystem facade
is idle. No name-cache aliasing occurs because this test uses the dedicated
autosave cache.

## Verification checklist

### Static/source verification

- all geometry assertions compile;
- record constant is 34,768;
- mask constant is 3,856;
- payload constant is 30,848;
- no raw `scene_t`, `kit_t`, enum, or descriptor struct is copied to disk;
- normal and Morph parameter regions are each descriptor-indexed for 72 cells;
- `morph_interpolation[]` has no storage path;
- the new cache is no larger than 9 KB and is separate from the name cache;
- no runtime dirty-mark producers were added;
- no Pattern or Effect implementation was added;
- no Menu source change was added;
- `git diff --check` passes.

### Pre-hardware fixture verification

- both fixture files are exactly 34,768 bytes;
- both have `HCPR`, version 1, and commit `0xA5`;
- `.hcprms1` generation is 1 and `.hcprms2` generation is 0;
- both Bank slots are zero and Bank names are `Full` plus zero padding;
- CRC32C validates for both;
- only the intended first-Scene parameter/type test regions are dirty;
- other Bank/Scene mask regions are clear;
- name payload cells match HCNAMES and their bits are clear.

### Post-hardware verification

- both files retain exact size;
- at least one valid generation/probe increment proves background writes;
- the newest valid record is selected before inspection;
- its remaining first-Scene dirty-bit count decreases by no more than 256
  existing live gets per transaction, while invalid bits may disappear without
  consuming that limit;
- remaining valid dirty bits survive copy-forward unchanged;
- drained Scene/Kit/Instrument bytes match the parsed values in
  `SD_CARD/Bank/000 Full/00 Slak`;
- normal descriptor cells match `[params]`;
- Morphable descriptor cells match `[morph]`;
- non-Morphable/out-of-range Morph cells remain payload-preserved but have no
  dirty bit;
- no Pattern bytes are present in the autosave mapping;
- Load/Save page residence suppresses new autosave starts;
- the older valid ping-pong record remains available if power is interrupted
  before the target commit marker is written.

## Implementation-note discipline

During implementation, append dated notes to this file recording:

- exact files changed;
- final constants and actual `sizeof` values;
- any departure from a phase or mapping above and the concrete code reason;
- build and `git diff --check` results;
- fixture generations, probes, sizes, CRCs, and dirty-bit counts; and
- hardware results supplied by the user.

All source changes in both `.c` and `.h` files must carry adjacent descriptive
comment blocks consistent with the existing codebase: what the change does,
why it must exist, inputs, outputs, and code affiliates.

## Implementation notes — 2026-07-31

### Changes landed

The targeted milestone is implemented in:

- `config.h`;
- `Core/Bank/Scene/Autosave.c`;
- `Core/Bank/Scene/Autosave.h`;
- `Core/Hardware/SD/filesystem.c`;
- `Core/Hardware/SD/filesystem.h`;
- `SD_CARD/.hcprms1`; and
- `SD_CARD/.hcprms2`.

No Menu, Preset, BankData, SceneData, InstrumentManager, instrument descriptor,
Pattern, Effect, Makefile, or asyncfatfs source was changed. Runtime mutation
producers and autosave recovery/apply remain outside this milestone.

The implementation uses the accepted interpretation that "Scene 1" is
resident Scene index 0 / Bank child `00`, and it reintroduces the requested
little-endian Bank restore slot at the start of the Bank payload.

### Final format and live mapping

- Header: 64 bytes.
- Mutation mask: 3,856 bytes.
- Payload: 30,848 bytes.
- Complete record: 34,768 bytes.
- Bank: 128 bytes.
- Scene: 1,920 bytes.
- Effect reserve: 512 bytes.
- Kit: 1,280 bytes.
- Instrument: 192 bytes.
- Normal endpoint allocation: 72 descriptor-indexed bytes.
- Morph endpoint allocation: 72 descriptor-indexed bytes.

`autosave_getLivePayloadByte()` now maps explicit BankData (including the
normalized Bank name), Scene settings, Kit settings, Instrument type text,
normal endpoints, and Morphable Morph endpoints. It rejects
Scene/Kit/Instrument names, padding, absent Scenes, Effects, Pattern, unknown
types, out-of-range descriptors, non-Morphable Morph cells, and
`morph_interpolation[]`. Every mapping and rejection boundary has adjacent
input/output/ownership commentary in `Autosave.c/.h`.

### Writer and SRAM result

The former private dummy operation is now the private autosave parameter drain.
After validating/selecting A/B, it:

1. asynchronously seeks to and reads the winner's full mask in chunks no larger
   than the existing 512-byte stream buffer;
2. examines at most 256 mask positions per foreground tick;
3. captures at most 1,536 successful live gets per transaction;
4. clears captured and nonexistent bits in the cached mask;
5. leaves every later/unprocessed dirty bit set;
6. applies the same immutable mask/patch cache to the CRC and target-copy
   passes; and
7. preserves the existing duplicate removal, partial I/O, intermediate sync,
   commit-last byte, and final sync ordering.

The new dedicated cache contains:

- 3,856 mask bytes;
- 1,536 `uint16_t` payload offsets; and
- 1,536 captured byte values.

Its linked size is exactly 8,464 bytes (`0x2110`), below the 9 KB ceiling. The
existing stage remains 2,048 bytes (`0x0800`) and the separate name cache
remains 9,000 bytes (`0x2328`). The prior logged boot-logging build used 69,980
BSS bytes; the completed build uses 78,444, an exact 8,464-byte increase.

### Fixture result

Both fixture records were regenerated from root `SD_CARD/.hcnames`:

| File | Size | Generation | Probe | Bank slot | Bank name | CRC32C | Dirty bits |
| --- | ---: | ---: | ---: | ---: | --- | --- | ---: |
| `.hcprms1` | 34,768 | 1 | 0 | 0 | `Full` | `0x0FEF6D7D` | 1,626 |
| `.hcprms2` | 34,768 | 0 | 0 | 0 | `Full` | `0xCD3C3A93` | 1,626 |

Independent readback verification proved:

- both CRCs match a fresh CRC32C calculation with stored CRC bytes zeroed;
- all 129 Bank/Scene/Kit/Instrument identity rows match HCNAMES at their new
  offsets;
- both masks contain exactly the 1,626 planned bits;
- the first set bit is payload-relative 136 and the last is 2,010;
- no Bank, later Scene, or name bit is set; and
- all intended nonexistent Scene/Effect/Kit/descriptor cells are included in
  the test mask.

The fixtures have not been run on hardware yet. After the device run, inspect
the newest valid generation rather than requiring both physical records to
have the same mask: the older ping-pong peer is expected to lag one committed
transaction.

### Build and source verification

`make -j4` completed successfully after the final source change:

```text
text     363716
data        400
bss       74604
```

The build emitted only the repository's existing unused legacy-filesystem
helper warnings and the standard newlib syscall/link-time notices; there were
no Autosave or parameter-drain compile warnings. `git diff --check` completed
without whitespace errors.

## Targeted follow-up — complete fixture mask, Bank name, empty-cache writes

This follow-up preserves the milestone boundary: it does not add Menu/System
dirty producers, retain the mutation mask across operations, dual-use the name
cache, load an autosave overlay, or implement Effects/Pattern storage.

### Mutation-mask ownership clarification

The intended completed design has one in-system SRAM mutation mask generated by
parameter changes. The mask stored in each ping-pong file is a completeness and
recovery record, not the normal source of new mutations. The root fixture masks
are being hand-written only to exercise the drain before mutation producers
exist.

For this temporary milestone, the selected file mask is still copied into the
dedicated 3,856-byte SRAM cache at the start of each drain transaction. All
classification and bit clearing operates on that cached copy. When the loaded
SRAM cache is already empty after its read handle closes, the operation now
publishes read-only completion immediately. It does not run the prospective
CRC pass, remove/recreate the inactive peer, advance generation/probe, write a
record, or enter a write flush. A nonempty cached mask continues through the
existing bounded capture and commit path so its parameter changes and updated
completeness mask become durable.

The temporary scheduler still performs its configured validation/mask-read
check. Replacing that repeated file bootstrap with the retained in-system mask
belongs to the future mutation-producer step and is deliberately not attempted
here.

### Bank-region live projection

`autosave_getLivePayloadByte()` now treats Bank payload bytes `2..9` as the
normalized, zero-padded live `bank_displayName()` bytes. Together with the
already mapped restore slot (`0..1`), Scene-present mask (`10..11`), active
Scene (`12`), and voice-edit mask (`13..14`), every currently existing Bank
field can now be captured when its mutation bit is set. Bank padding `15..127`
remains nonexistent and is closed without a live get.

This reads BankData only. It does not borrow or reinterpret HCNAMES, so an
in-system Bank rename can eventually be captured by the same mutation path as
other Bank parameters.

### Complete-mask fixtures

Both root test fixtures now mark every one of the 30,848 payload positions
dirty, including all names, live parameters, unimplemented Effects, absent or
reserved descriptor cells, and padding. This deliberately exercises both live
gets and the required close-without-get behavior across the complete format.

| File | Size | Generation | Probe | CRC32C | Dirty bits |
| --- | ---: | ---: | ---: | ---: | ---: |
| `.hcprms1` | 34,768 | 1 | 0 | `0xB946EF0B` | 30,848 |
| `.hcprms2` | 34,768 | 0 | 0 | `0x7B95B8E5` | 30,848 |

The generation, probe, Bank identity, names, and zero/name baseline payload
were not otherwise changed. Each CRC was recomputed with bytes `12..15`
logically zero, matching firmware validation.

### Verification

`make -j4` completed successfully after the source changes:

```text
text     362932
data        396
bss       74604
```

The SRAM footprint is unchanged. Build output contains only the existing
unused legacy-filesystem warnings and standard newlib syscall notices.
Independent fixture readback confirms exact size, all 3,856 mask bytes equal
`0xFF`, 30,848 dirty bits, and valid CRC32C for both files.

## Targeted follow-up — 1,536-get batches and dirty-backlog continuation

This follow-up changes drain throughput only. It does not add mutation
producers, retain the full mutation mask between operations, dual-use the name
cache, alter the 512-byte SD stream buffer, or change the ping-pong/CRC/commit
transaction.

### Configuration result

`AUTOSAVE_PARAMETER_GETS_PER_WRITE` is now `1536u`. The existing cache formula
is one 3,856-byte mask plus one `uint16_t` offset and one byte value per captured
get:

```text
3856 + (1536 * 2) + (1536 * 1) = 8464 bytes
```

This stays below the explicitly asserted 9,000-byte ceiling and is large enough
for approximately three current 496-live-byte Scenes per generation. The
per-foreground-call classifier remains capped at 256 mask positions, so the
larger transaction cap does not turn into one correspondingly larger main-loop
burst.

The ordinary `AUTOSAVE_WRITER_INTERVAL_MS` remains `5000u`. A separate
`AUTOSAVE_WRITER_CONTINUATION_INTERVAL_MS` is `250u`; both values are compile-
time asserted below the wrapping `time_sysTick` comparison limit of 32,768 ms.

### Continuation handoff and no-write boundary

After the inactive target has received its valid commit marker and closed,
drain phase 22 tests the exact updated SRAM mask that was placed in that target.
It retains only a one-byte `continue_pending` result across the existing final
sync gate. The autonomous completion callback consumes that result after the
transaction is durable:

- successful committed target with dirty bits remaining: next attempt is due
  after 250 ms;
- successful committed target with an empty outgoing mask: next check remains
  due after five seconds;
- already-empty mask discovered after the winner's read handle closes: the
  existing phase-55 read-only completion remains in force, with no write,
  generation/probe advance, target removal/create, or write flush;
- validation/recovery/error completion: the ordinary five-second cadence is
  retained and no stale continuation decision is inherited.

Load and Save pages continue to suppress new autonomous starts. A continuation
that becomes due while either page owns the UI/filesystem simply waits for the
normal idle scheduler gate; no active transaction is preempted.

### Expected complete-mask throughput

The current `000 Full` fixture has 7,951 live gets: fifteen Bank bytes plus 496
bytes for each of sixteen Scenes. The former 256 cap required 32 committed
generations. A 1,536 cap requires six. The first attempt still observes the
ordinary five-second boot/debounce interval; only the five durable backlog
continuations use the 250 ms cadence. Actual completion time remains dependent
on the full-record validation, CRC, copy, FAT metadata, and sync work in each
generation.

### Build verification

`make -j4` completed successfully:

```text
text     363068
data        396
bss       78444
```

The linked `fs_autosave_parameter_cache` symbol is exactly `0x2110`/8,464
bytes. The build emitted only the existing unused legacy-filesystem helper
warnings and standard newlib syscall/link warnings. `git diff --check` also
completed successfully.
