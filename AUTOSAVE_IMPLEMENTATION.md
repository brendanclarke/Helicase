# Parameter Blob Autosave — implementation plan

## Scope and outcome

This plan implements only the parameter portion of the autosave sketch:
one root-level `.hcbnksv` blob for the active Bank. It deliberately excludes
Pattern autosave (`APAT00.BIN` … `APAT15.BIN`), Pattern dirty marks, and the
dynamic-pattern log/compaction design.  The current 112-byte bitmap
`pattern.pat` stays committed-Bank data until the later Pattern design is
chosen.

The active Bank remains the only autosaved workspace.  Root `Scene/`, `Kit/`,
and `Instrument/` remain explicit import/export libraries.  Autosave must
never change DSP state; it snapshots already committed resident `BankData` /
`SceneData` data from the foreground filesystem state machine.

This document's implementation manifest is based on a current-source audit.
The chosen durability policy is **previous valid autosave record, then
committed Bank fallback**. No new static SRAM allocation is approved; the
ledger must borrow existing storage with an explicit lifetime contract.

## Current implementation facts

- `BankData` already owns the active Bank slot, active local Scene, present
  mask, and the 16-bit VOICE edit mask.  `SceneData` already retains all 16
  `scene_t` objects, with Scene settings, Kit settings/types, and six
  descriptor image pairs per Scene.
- `filesystem.c` is the sole asynchronous SD operation owner.  It has a
  single serialized operation and the main loop pumps `filesystem_tick()`.
  Any autosave writer must be another filesystem operation, not direct FAT
  calls from Menu, Preset, MIDI, or SceneData.
- Existing write helpers stream `sceneset.scg`, `kitset.kcg`, Instrument text,
  `bankset.bcg`, and the current placeholder `effects.fx` one line at a time.
  `storageTypes` owns the parsers/formatters; it must remain independent of
  asyncfatfs.
- There is no generic write-one-Bank-domain API, no dirty ledger, no CRC-16/
  CRC-32 utility, and no autosave apply phase.
- `scene_t` deliberately does not retain Scene, Kit, or Instrument names.
  Those identities are card-resident in `/.hcnames`; the only retained identity
  is the current 81-byte block.  This makes an autosave record that reuses the
  literal `kitset.kcg` text schema awkward: its `file=` member names are not
  available for all 16 resident Scenes without an additional HCNAMES read.
- `asyncfatfs` has `fseek`, `fread`, and `fwrite`, but `AFATFS_USE_FREEFILE`
  is deliberately disabled.  Consequently `"as"` parses but does **not** give
  the draft's contiguous/preallocated fast path; reopening an existing file
  also explicitly clears its contiguous flag. Keeping `.hcbnksv` open for
  a whole session would consume one of the five application handles and risks
  recreating the handle-exhaustion failures fixed in Session 042.
- Normal SRAM1 has 310,052 bytes technically unused but is reserved solely for
  future Pattern data. DTCM is reserved solely for delay lines. No new
  retained allocation is authorized.
- The existing 2,048-byte `fs_stage_workspace` is a typed union used only by
  Kit, Scene, Bank-child, and Instrument **payload loads**. The source shows
  no use of that union from the Kit/Scene/Bank/Instrument save writers,
  indexes, scans, or HCNAMES transactions. It can back autosave state while no
  typed payload load owns it, provided the hand-off is explicit.

## Review of draft v0.1

The fixed positional addressing, sector boundaries, root-level 8.3 name, and
single filesystem writer are good fits for the current system.  A parameter
record is small and can be streamed without staging the whole Bank.

The following parts need correction before the format is frozen.

1. **A one-slot record does not meet the stated torn-write guarantee.**  A
   later write overwrites the only earlier valid version at that offset.  If it
   tears, the CRC rejects the record, but recovery falls all the way back to
   the committed Bank file—not merely to the previous autosave update.  The
   claim that every earlier successful unit remains valid is therefore false
   for repeated writes to the same positional record.
2. **Bank reuse need not zero about 409 KiB**, but only if every record binds
   itself to the Bank slot (and an epoch/session identifier).  The draft puts
   Bank identity only in the header; changing the header alone would make old
   valid records appear to belong to the newly selected Bank.  Either clear
   all records, as drafted, or add Bank identity to every record.  This plan
   recommends the latter.
3. **The common record header size is inconsistent.**  Its listed fields total
   9 bytes (`1+1+1+2+4`), not 13.  The payload capacity must be recalculated
   when the final header is selected.
4. **Text payload reuse is not a clean default.**  `sceneset`, Instrument, and
   Bankset can be emitted from retained state, but `kitset` requires names that
   are purposely not resident.  Reading HCNAMES for each save would contend
   with the one shared 9,000-byte browser cache and make a background write
   depend on mutable card identity.  A compact binary payload expressed in
   retained-state terms avoids that dependency and is substantially smaller.
5. **The draft's preallocation performance premise is unavailable.**  The
   current build uses normal FAT chains.  Initial creation and random seeks
   must be measured on target hardware; the plan must not claim contiguous
   access until freefile support is consciously restored and tested.
6. **Dependent records need an explicit policy.**  A Kit replacement changes
   its kitset/type membership and up to six Instrument images.  Applying an
   arbitrary mixture of new and old records can pair an image with the wrong
   descriptor type.  The loader needs a Scene-level acceptance boundary for
   replacement/copy operations, not only individual CRC checks.

## Recommended v0.2 parameter format

Use one root `.hcbnksv`, addressed by active root Bank slot. The dot-prefixed
8.3 filename is an implementation-owned autosave register, not a user library
object. Include only
domains with retained parameter state now:

| Logical key | Count |
| --- | ---: |
| Bankset | 1 |
| Scene settings | 16 |
| Kit settings/type membership | 16 |
| Instrument endpoint images | 16 × 6 |
| Scene batch-commit guard | 16 |

Patterns and the `effects.fx` placeholder have no record and no dirty producer
in this session. Effects can receive a new domain when actual retained effect
state exists; this avoids freezing a meaningless placeholder into v1.

### Payload encoding

Prefer a format-specific binary payload, versioned independently from the
committed text schemas.  Each decoder copies into a small, explicit value
object then validates and applies through the same bounded Scene/Bank owner
APIs used by load.  Do not serialize C structs wholesale: padding, runtime
interpolation images, and future ABI changes must not become disk format.

Suggested payload contents:

- Bankset: active Scene and VOICE edit mask.
- Sceneset: the existing retained Scene settings fields, in documented order.
- Kitset: both generated slot-6/track-7 endpoints plus six Instrument type
  tags.  It contains no filenames or names.
- Instrument: format version, slot/type guard, and the two persisted
  descriptor endpoint images.  The derived `morph_interpolation` image is
  never stored; the existing Morph worker rebuilds it after apply.
- Effects: no payload until effects have retained parameters.

This separates the active working snapshot from the external text interchange
format.  Explicit Bank SAVE continues to use the established text writers and
HCNAMES-derived names to generate committed Bank files.

### Record durability

Use two physical slots for every logical record (A/B), each sector-aligned.
On a flush, write the older/invalid physical slot with a monotonically
increasing generation; never overwrite the current valid winner.  On load,
validate both slots and take the highest valid generation matching the current
Bank slot, Bank epoch, and schema. The header assigns a new epoch whenever a
different committed Bank becomes active, so a later return to the same Bank
does not accidentally revive an earlier working session. This makes a torn
write lose at most its latest record update rather than the entire prior
autosave state of that record.

Each physical record contains, at minimum:

```text
bank_slot (u16), bank_epoch (u32), scene_index (u8; 0xff for Bankset),
domain (u8), instrument_slot (u8; 0xff unless Instrument), payload_len (u16),
generation (u32), batch_id (u32; zero for a standalone edit), payload,
crc32 (u32)
```

CRC-32 covers all header fields through the used payload. With the chosen
binary payloads, every current record fits in one 512-byte sector: an
Instrument needs two 64-byte endpoint images, type/format guards, the record
header, and CRC—well below 512 bytes. Make the record size uniformly one
sector and prove this with compile-time maxima assertions and host tests.

There are 145 logical records: one Bankset, then sixteen each of Sceneset,
Kitset, six Instruments, and Scene batch commit. Two physical sectors per
logical record plus two header sectors (the A/B header pair) is **149,504
bytes**.
This is materially smaller than duplicating the draft's text-oriented
4,096-byte Instrument records, while providing the chosen prior-record
recovery guarantee. Creation/extension time remains a target measurement
because freefile/contiguous allocation is unavailable.

The header is a small double-written sector containing magic, schema version,
active Bank slot, and CRC.  Record-level Bank identity means switching Banks
only requires a durable new header; old records are rejected without an
expensive whole-file zero pass.  The active Bank’s records overwrite their
alternate slots as needed.

### Cross-record Scene coherence

For ordinary parameter edits, individual valid records may apply independently.
For operations that replace/copy a Kit or a whole Scene, mark all affected
domains as one Scene batch and append a small, double-written Scene commit
record only after every member write succeeds.  Apply records from that batch
only when its commit record is valid.  This prevents new instrument images from
being accepted under an old Kit type map.  A failed/torn batch falls back to
the last accepted snapshot for its affected domains; it must never produce a
type/image mismatch.

This adds 16 logical Scene-commit records and must be included in final file
sizing.  If the product accepts losing the entire prior autosave state for an
interrupted Kit replacement, this can be simplified, but it must be an
explicitly accepted weaker guarantee.

## Runtime architecture

### 1. Dirty ledger

Add `Core/Bank/Scene/Autosave.{c,h}` as the sole owner of logical dirty state;
it owns no card I/O.  Provide only domain-oriented APIs, for example:

```c
autosave_markBankset();
autosave_markSceneSettings(scene);
autosave_markKitset(scene);
autosave_markInstrument(scene, slot);
autosave_markSceneBatch(scene, domain_mask);
autosave_tick(now);
```

The ledger rejects marks unless a Bank is resident and the addressed Scene is
present in that active Bank.  Root-library operations and the absent future
landing Scene are therefore excluded by construction.

For 145 currently addressable logical records (one Bankset plus sixteen times
Scene/Kit/six Instrument/Scene-commit), the exact ledger shape is:

```text
uint16_t first_dirty_tick[145]       290 B
uint16_t last_dirty_tick[145]        290 B
uint32_t pending_scene_batch_id[16]  64 B
uint8_t  dirty_bits[19]               19 B
uint16_t in_flight_index               2 B
uint16_t active_bank_slot              2 B
                               -------
                                  667 B before ordinary alignment padding
```

The single in-flight index implements the per-record `in_flight` condition
without duplicating a flag 145 times. `pending_scene_batch_id[]` gives each
multi-record replacement one stable nonzero identity until its commit record
is durable; a reader accepts a batch member only when its matching commit is
valid. This is **not** a new static allocation:
define `autosave_ledger_t` in the existing `filesystem_stage_workspace_t`
union, whose fixed 2,048-byte backing already exists. `Autosave` owns the
ledger contents, while `filesystem` owns the lease to its backing storage. The
current ledger consumes 667 bytes before alignment of those existing bytes
and requires no
SRAM1/DTCM growth.

The lease rules are non-negotiable:

- It is `AUTOSAVE` while the active Bank is interactive, while browsing
  Load/Save, and during non-payload filesystem work (scans, indexes, HCNAMES,
  and all current save writers). Autosave jobs may sleep while another
  filesystem operation owns the serialized SD writer; the ledger remains
  intact in the otherwise-unused stage workspace.
- Before Kit, Scene, Bank, or Instrument **payload load** starts, the
  coordinator forces all dirty records eligible, writes them one at a time,
  waits for the final flush, and only then relinquishes the lease to `LOAD`.
  A failed drain rejects the requested load; it never discards dirty state
  merely to reuse staging memory.
- While `LOAD` owns the workspace, the same operation lock must reject
  retained parameter edits from Menu and MIDI/Preset mutation entry points.
  Otherwise a valid edit could occur with nowhere to retain its dirty state.
  The existing UI already locks most load gestures; MIDI and direct Preset
  callers need an explicit audit and gate.
- After the staged load commits or fails, initialize/reclaim the `AUTOSAVE`
  ledger for the resulting active Bank. Applying `.hcbnksv` after a
  committed Bank Load temporarily uses the same workspace while the ledger is
  clean, then initializes the ledger once the overlay is done.

An explicit save does not need the typed load stage. It may therefore keep the
ledger asleep while its own filesystem operation runs. A Bank SAVE waits for
an already in-flight autosave record (the filesystem is serialized), then
saves the same resident state directly; on a successful committed Bank save it
resets the blob header and clears the ledger. On failure it leaves the
ledger/blob intact.

CRC uses a bitwise IEEE CRC-32 routine with a fixed polynomial and no lookup
table, so it introduces no retained RAM allocation.  Its target CPU cost must
still be measured.

### 2. Mutation ownership and marks

Wire marks after a successful retained-state mutation, never in generic menu
painting or raw encoder handling.

- `preset_setInstrumentParameter()` and `preset_setSupplementalParameter()`:
  Instrument record for each changed Scene/slot.
- `preset_setSlot6Track7AmpEnvelopeDecay()`: Kitset record for each changed
  Scene.
- `preset_setVoiceAudioOut()`, `preset_setVoiceFxSendAmount()`, and
  `preset_setVoiceFaderSetting()`: Sceneset record.
- `preset_morphScene()`, `preset_morphVoiceScene()`, and
  `preset_setVoiceDecimationAll()`: Sceneset record.
- Scene MIDI channel/note mutation currently enters through `menu_parseGlobalParam`
  and `scene_setTrackMidi*`; move that retained mutation behind a Preset or
  SceneData owner API that also marks its Sceneset record.  Do not leave a
  direct Menu mutation untracked.
- Instrument/Kit/Scene copy/load/replace paths: use a Scene batch mark after
  their successful resident commit.  Their exact entry points need a focused
  audit during implementation because the filesystem applies some payloads
  directly.
- Bank active Scene or VOICE edit-mask setters: mark Bankset only for a
  user-visible retained edit, not for transient load normalization.

No PatternData API receives a mark in this phase.

### 3. Scheduling and filesystem boundary

`autosave_tick(systick_ticks)` is called in the foreground after user/mutation
services and before or next to `filesystem_tick()`.  It only starts work when
the filesystem is idle.  It selects one candidate using the settled policy:

1. eligible after 5 seconds of inactivity;
2. forced after 30 seconds since first dirtied;
3. oldest forced candidate first, then oldest idle candidate;
4. a successful completion clears exactly the written logical record;
5. failure leaves it dirty and records a diagnostic condition without an
   immediate tight retry loop.

The filesystem layer adds one public request such as
`filesystem_requestAutosaveRecord(key, completion_cb)`.  Its private state
machine opens `.hcbnksv` from root, seeks to the selected alternate slot,
streams header/payload/padding/CRC, closes, and waits for the existing final
flush gate.  It reopens for each job rather than retaining a session handle.
All offsets and generation selection live below this facade; Menu/Preset only
see ledger APIs.

### 4. Bank activation and resume

After committed Bank Load has completed its normal staging/HCNAMES transaction,
the autosave coordinator:

1. opens and validates the `.hcbnksv` header for the selected Bank slot;
2. scans only the fixed parameter records for that Bank, choosing valid A/B
   winners;
3. applies Bankset, then each accepted Scene batch or independent domain in
   dependency order: Scene settings, Kit membership, then Instrument images;
4. rebuilds the active Scene’s Morph/runtime state through existing Preset
   apply paths; inactive Scenes remain retained only;
5. initializes the ledger clean and publishes the Bank interactive only after
   the overlay finishes.

Missing, invalid, stale-Bank, or unsupported records are nonfatal and leave the
already loaded committed domain intact.  A valid autosave overlay must not
change HCNAMES, library indexes, committed Bank files, or root `settings.cfg`.

Before any typed payload load that replaces resident Bank data, the coordinator
must force a drain, block new marks, and hand the stage workspace to the
loader. Save operations keep the ledger asleep instead because they do not use
the typed stage. The UI must show the load barrier rather than silently
discarding a dirty record.

### 5. Explicit Bank SAVE and later RELOAD

This first phase must not silently claim a promotion/reload feature it cannot
yet implement. Explicit Bank SAVE already writes a complete temporary Bank
tree and promotes it by ordered renames, but it does not consume the blob. The
selected policy is:

- **Selected initial policy:** Bank SAVE waits only for an already in-flight
  autosave record, then writes committed files from current resident state
  using its existing serializers. On success it invalidates the matching blob
  header and clears the ledger. This needs no blob-to-tree copy/promotion;
  failed saves preserve the existing autosave state.
- **Later policy:** implement Scene RELOAD as a committed-Bank load followed by
  blob invalidation/reset for that Scene.  It needs a precise cancellation rule
  and is out of the first writer/apply milestone.

`settings.cfg` / `.settings.cfg` backer policy is not included in this blob
scope.  It should be designed independently, because global settings and the
active Bank pointer are not Bank-resident parameter domains and changing them
interacts with boot order.

## Delivery sequence

1. Apply the settled payload and input-policy decisions below.
2. Define the stage-workspace lease and load barrier first. Confirm every
   parameter mutation route—especially MIDI/direct Preset calls—is rejected
   while `LOAD` owns the workspace; rejected external MIDI is a silent no-op.
3. Add a host-testable CRC-32 implementation with known vectors; add format
   constants and 512-byte record-fit `static_assert`s.
4. Implement binary encode/decode/validation helpers and tests for every
   included domain.  Test bad magic/version/length/key/CRC, generation choice,
   and stale-Bank rejection.
5. Implement the Autosave ledger in the existing stage union and mutation
   marks; unit-test debounce,
   forced eligibility, priority, error retention, and 16-bit tick wrap-around.
6. Add the filesystem writer/read-overlay state machines using one job at a
   time.  Test seek/write/close/flush ordering and use deliberate interrupted
   writes in a card-image or fault-injection harness to prove A/B recovery.
7. Integrate committed Bank Load overlay and Bank SAVE reset. Keep
   patterns excluded.
8. Build (`make && make img`), inspect linked SRAM against the manifest, run
   `git diff --check`, and conduct hardware tests: continuous edits during
   playback, multi-Scene VOICE fan-out, power cut during every record class,
   bank switch, full Bank save, and malformed/stale blob fallback.

## Settled decisions and remaining choice

1. **Compact binary payloads are selected.** The blob uses the explicit,
   versioned retained-state encoding described above; it does not reuse the
   human-readable committed-file schemas.
2. **Workspace reuse is selected:** a requested typed payload load shows a
   short autosave-drain wait before it begins, and retained parameter mutation
   is rejected rather than queued while that barrier/staged load owns the
   workspace. This is the required no-new-SRAM lease boundary. Rejected
   external MIDI is a **silent no-op**: it creates no retained mutation,
   parameter mirror update, Menu notification, or autosave mark.
3. Should a successful explicit Bank SAVE reset the active blob immediately
   (recommended), since its complete tree is already the committed image of
   the same resident state?

## Source-audited code-change manifest

This section replaces assumptions about the proposed feature with the behavior
present in the source tree at the time of this plan. It intentionally does not
derive implementation requirements from the historical logs, the draft schema,
or the reference specification. Names below are source symbols, rather than
line numbers, so the manifest remains useful after adjacent edits.

### Source facts that constrain the implementation

`filesystem.c` owns the only foreground-pumped AsyncFATFS context. Its
`filesystem_start()` resets one operation context, `filesystem_tick()` polls
AsyncFATFS and dispatches one private operation enum, and
`filesystem_finish()` routes success through `FS_INTERNAL_OP_FLUSH_FINISH`.
The latter calls `afatfs_sync()` before it invokes the captured completion
callback. Autosave must use this existing completion gate; a callback after
`fclose()` alone is not durable enough.

The current stream helpers are deliberately unsuitable for a blob sector:
`filesystem_writeStreamChunk()` and `filesystem_readStreamChunk()` take an
8-bit length and keep their partial offset in the 8-bit `op_item_offset`.
Passing 512 would truncate to zero. The new state machine needs a separate
sector helper with a `uint16_t` byte offset, even though the shared
`staging_buf[512]` remains the correct transient buffer for one on-card
sector.

`afatfs_fseek()` calls `MIN(requested_offset, file->logicalSize)`. It cannot
seek past EOF to form sparse fixed offsets. Consequently first creation must
append all **292** sectors (two headers plus 145 A/B record pairs) in ascending
order, writing invalid/zero sectors first and valid header sectors last. Only
after the 149,504-byte file has flushed may a later operation seek directly to
an A/B sector. A partial first format has no valid header and is therefore a
safe committed-Bank fallback, not an autosave image.

The 2,048-byte `fs_stage_workspace` union is used by the typed payload loaders
only: `filesystem_loadKitDirectory_tick()`,
`filesystem_loadSceneDirectory_tick()` (including its Bank child path), and
`filesystem_loadInstrument_tick()`. The exact `fs_stage_workspace` references
show no save, scan, index, resident-name, or generic menu use. In contrast,
the 9,000-byte `fs_list_cache_name` is concurrently needed by indexing and
HCNAMES flows and must never become a ledger. `staging_buf` is transient during
many unrelated state machines, so it can form a sector only while the one
filesystem operation owns it and cannot contain ledger state.

The existing Preset request fields already retain enough coordinates for a
deferred typed request: `pm_request_slot`, `pm_request_type`,
`pm_kit_request_scene_mask`, and the Instrument scene/slot/type/index fields.
This makes a load barrier possible without a second pending-request allocation.

### Required storage layout and ownership

Change `filesystem_stage_workspace_t` in `Core/Hardware/SD/filesystem.c` to
add an `autosave_workspace_t` member defined by the new autosave header. It is
a union member alongside `kit_stage`, `instrument_stage`, and `scene_stage`,
not a new file-static object. Add a compile-time assertion that the complete
autosave workspace is at most `FS_STAGE_CACHE_BYTES` and preserves the union's
existing `kit_t` alignment requirement.

`autosave_workspace_t` contains the 667-byte ledger described above plus
only transient scheduler/reader fields: the active epoch, header generation,
current logical key, current A/B candidate, sector byte offset, format-sector
cursor, and one 16-entry batch acceptance map. It must fit with margin below
2,048 bytes; the implementation must use a `sizeof` assertion rather than a
hand-maintained RAM claim. It does not contain a 512-byte second I/O buffer.
The already-existing `staging_buf` is that buffer.

The workspace has these states, derived from existing operation/status state
rather than a new global lease byte:

```text
Autosave owns union  -> interactive, browser/index/name/save work, blob I/O
Typed load owns union -> FS_INTERNAL_OP_LOAD_KIT/LOAD_KIT_MORPH/LOAD_SCENE/
                         LOAD_BANK child payload/LOAD_INSTRUMENT
No owner              -> boot before a resident Bank, or after an aborted load
```

The Preset status (`PRESET_AUTOSAVE_BARRIER`, added as another value of the
existing `preset_status_t`) closes the repair-name prelude gap before a Bank
loader changes into `FS_INTERNAL_OP_LOAD_BANK`. No additional static flag is
necessary. Mutators query that status/Autosave readiness and refuse to alter
retained data while it is a barrier or a typed load.

### 1. New `Core/Bank/Scene/Autosave.h`

**Change.** Add the small public coordinator interface and value-only record
key types. Export `autosave_workspace_t`, `autosave_record_key_t`, domain and
batch-mask enums, plus APIs for initialization, mark, load barrier, scheduler,
record serialization/validation, and filesystem completion notification.

**Why.** Menu, Preset, and filesystem need one vocabulary for retained-domain
changes but only filesystem may touch AsyncFATFS. Keeping the ledger API out of
`SceneData.h` prevents raw data setters from acquiring an SD dependency.

**Inputs and outputs.** Mark APIs accept a Scene index/slot/domain and the
foreground tick. They return whether the target is currently an autosavable
resident Bank object. The scheduler returns either “no job”, “need format”,
or one immutable key plus batch id. Encode functions write a bounded byte
sequence into a caller-provided buffer; decode functions validate before
changing resident data. No API exposes an AsyncFATFS file handle or a mutable
ledger pointer to Menu/MIDI.

**Affiliates.** `Autosave.c`, `filesystem.c`, `presetManager.c`, `menu.c`, and
`main.c`. `filesystem.h` includes this header only for the workspace accessor;
the public filesystem API remains the sole storage boundary.

### 2. New `Core/Bank/Scene/Autosave.c`

**Change.** Implement the allocation-free coordinator. It obtains the union
address on each call from a `filesystem_autosaveWorkspace()` accessor instead
of retaining its own static pointer. All persistent state lives in the passed
union member, and all sector bytes are supplied by filesystem.

**Dirty scheduling.** `autosave_mark*()` first verifies a resident Bank,
scene-present bit, legal scene/slot bounds, and mutation permission. It sets
one logical bit and records `first_dirty_tick` only on the clean-to-dirty
transition; every successful subsequent edit updates `last_dirty_tick`.
`autosave_tick(now)` selects one record only when no filesystem operation is
busy. It uses unsigned subtraction, `(uint16_t)(now - then)`, rather than
`now >= then + delay`, so the actual 16-bit `time_sysTick` counter wraps
correctly. A record is
idle-eligible at five seconds; it is forced at thirty seconds from its first
dirty tick. The selector scans all 145 fixed keys, retaining the oldest forced
candidate first and then the oldest idle candidate. A failed write leaves both
timestamps and the dirty bit intact; it records a retry deadline in the union
so a card error cannot create a foreground tight loop.

**Batch scheduling.** `autosave_markSceneBatch(scene, mask)` allocates a
nonzero monotonic batch id from the header generation/current scheduler value,
stores it in `pending_scene_batch_id[scene]`, marks the selected Scene/Kit/
Instrument records, and finally marks the Scene-commit record. The scheduler
does not offer the commit record until all of that batch's member dirty bits
have cleared. Every batch member record contains that batch id; the commit
record's payload repeats it and its member mask. A boot reader accepts a
batch member only if its matching commit survives CRC and key checks. This is
the required boundary that prevents a new instrument image being paired with
an old Kit type map after a torn replacement.

**Encode/decode.** Use explicit little-endian `put_u16`, `put_u32`, `get_u16`,
and `get_u32` helpers. Each helper advances a checked `uint16_t` cursor and
fails if `cursor + field_size > payload_len`; it must not cast a packed byte
array to a C struct. Bankset encode uses BankData's active Scene/edit mask.
Sceneset encode uses the retained `scene_settings_t` fields. Kitset encodes
the two generated decay endpoints and six type tags. Instrument encode writes
the source slot/type guard and the two retained endpoint images only; it never
writes `morph_interpolation`, names, Pattern data, or runtime pointers.

**CRC.** Add a bitwise IEEE CRC-32 routine with initial/final XOR
`0xffffffff` and reflected polynomial `0xedb88320`. The outer byte loop walks
exactly the fixed header bytes followed by `payload_len` bytes; its byte index
is bounded by 512. The inner loop executes eight shifts, one for each bit,
and conditionally XORs the polynomial when the old least-significant bit is
one. Comment both loop invariants and the reason for the reflected shift;
avoid a 1 KiB lookup table because the design has no RAM allocation headroom.
CRC coverage excludes sector padding and includes bank slot, epoch, domain,
key, generation, batch id, length, and payload. A valid CRC alone is never
enough: every semantic key and length is checked as well.

**Apply order.** The reader first validates both blob headers and chooses the
newest valid header for the currently committed Bank slot. It then scans each
logical key's two sectors. For each pair, it rejects bad magic/version/CRC,
wrong Bank/epoch/key, impossible payload length, and invalid type guard before
comparing unsigned generation values. It accepts a valid committed batch as a
base in this order: Sceneset, Kitset, then six Instruments. Later standalone
records (`batch_id == 0`) may overlay that base only after their instrument
type guard matches the accepted Kit type. After all retained bytes are valid,
it invokes existing Preset runtime rebuild/apply paths for the active Scene;
inactive Scenes receive no DSP calls. No decode path marks a record dirty.

### 3. `Core/Hardware/SD/filesystem.h`

**Change.** Add the narrow blob request/accessor surface:
`filesystem_requestAutosaveOpenOrFormat`, `filesystem_requestAutosaveWrite`,
`filesystem_requestAutosaveReadApply`, `filesystem_requestAutosaveReset`,
and `filesystem_autosaveWorkspace`. The first four take no raw file pointer;
their immutable key/job details come from Autosave.

**Why.** This makes all media operations visible in the same facade as the
existing typed loads/saves, preserves the single `filesystem_tick()` owner,
and prevents a future mutation owner from bypassing final flush semantics.

**Inputs and outputs.** Requests fail immediately unless `filesystem_status()`
is idle and Autosave owns the stage union. Each later reports done/error using
the standard callback plus `filesystem_errorCode()`. The workspace accessor is
for Autosave implementation only and must document that callers may not retain
the pointer across a typed-load request.

### 4. `Core/Hardware/SD/filesystem.c`: operation plumbing and sector I/O

**Change.** Add private operations for blob format/open, record write, record
read/apply, and header reset. Add their error prefixes and `filesystem_tick()`
switch cases. Reuse `filesystem_start()`, `op_file`, `op_file_ready`, close
callback plumbing, `filesystem_finish()`, and `FS_INTERNAL_OP_FLUSH_FINISH`;
do not add a parallel poller or invoke AsyncFATFS from Autosave.

**Workspace and aliasing.** Add `autosave_workspace_t autosave_stage` to the
existing stage union and macros local to filesystem for the current workspace.
Typed-load request helpers must assert the Autosave barrier is complete before
they initialize `kit_stage`, `instrument_stage`, or `scene_stage`. The existing
comment that generic `filesystem_start()` never clears stage stays true: each
owner explicitly initializes its member at the hand-off.

**Sector helper.** Add a dedicated read/write helper accepting
`uint16_t sector_offset` and `uint16_t sector_length` (always at most 512).
It calls `afatfs_fread`/`afatfs_fwrite` with the remaining byte count and
increments the offset by the returned `uint32_t`, checking it cannot pass the
512-byte bound. This handles partial AsyncFATFS transfers without the
`uint8_t op_item_offset` overflow in the existing generic stream helpers.

**Format state machine.** Open root `.hcbnksv`, create it under the existing
AsyncFATFS open conventions if absent, then fill `staging_buf` with zeroes and
append sectors `0..291`. The format cursor is a `uint16_t` because 292 fits,
while `offset = cursor * FS_SECTOR_SIZE_BYTES` is computed in `uint32_t` to
make multiplication width explicit. The loop is one foreground state-machine
iteration per sector, never a blocking `for` loop. After sector 291 is fully
written, compose and write header B then header A with CRCs, close, and use the
normal final flush state. Header validity requires magic, schema, exact file
size/layout value, bank slot, epoch, header generation, and CRC. A failure at
any phase leaves the header invalid and falls back to the committed Bank.

**Record write state machine.** Read both physical sectors of the selected
logical key first; this chooses the invalid or lower-generation destination
without trusting stale RAM. Compose a complete 512-byte record in
`staging_buf`: explicit header, payload emitted by Autosave, CRC, then a fixed
pad byte. Seek only after the file-size/header check proves formatting is
complete; wait for `afatfs_ftell()` before issuing a read/write because seek
may be queued. Write, close, then enter the existing flush gate. Only that
completion calls `autosave_onWriteResult(key, batch_id, ok)`; it clears exactly
one dirty bit and makes the batch commit eligible only after all members are
durable.

**Read/apply state machine.** Read header A and B as full sectors, then loop
logical index `0..144`, mapping index to Bankset or `(scene, domain, slot)`.
For each index, seek/read candidate A and B one at a time into `staging_buf`,
validate it through Autosave, and retain only the small winner descriptor in
the union. Do not retain 145 sectors. A second bounded pass applies the
accepted winners in dependency order. The `logical_index -> scene/domain/slot`
math must be a documented helper using division/remainder with constants:
after index 0, `scene = (index - 1) / 9`, `within = (index - 1) % 9`, where
the nine Scene keys are settings, kit, six instruments, and commit. Bounds
assertions must prove scene < 16 and slot < 6 before a shift or array index.

**Reset.** A successful explicit Bank Save starts the reset request only after
that save's normal callback reports durable completion. Reset writes a new
invalid/other-epoch header pair (not a delete), flushes, and clears the
in-memory ledger only on success. If reset fails, the committed Bank save is
still valid; retain the blob/ledger state and surface the reset error for a
later retry rather than claiming it succeeded.

### 5. `Core/Bank/Scene/Preset/presetManager.h` and `.c`

**Change: ownership-safe edit APIs.** Separate user/runtime retained edits
from load-time application. The public user edit paths call Autosave only
after they detect an actual retained byte change; the load normalization and
runtime rebuild paths use private no-mark apply helpers. This is required
because `preset_applyKitVoiceSupplemental()` currently calls
`preset_setSupplementalParameter()` while re-binding a loaded image. Marking
inside that shared call would turn every load/rebuild into an autosave edit.

The affected retained setters are `preset_setInstrumentParameter`,
`preset_setSupplementalParameter`, `preset_setVoiceAudioOut`,
`preset_setVoiceFxSendAmount`, `preset_setVoiceFaderSetting`,
`preset_setSlot6Track7AmpEnvelopeDecay`, `preset_morphScene`,
`preset_morphVoiceScene`, and `preset_setVoiceDecimationAll`. Each compares
the normalized value with retained state before it writes, so repeated MIDI CC
or velocity values neither reset the five-second debounce nor cause SD churn.
Its mark is respectively Instrument, Kitset, or Sceneset. The generated
slot-6 decay belongs to Kitset, not an Instrument record.

**Change: parameter-owned track MIDI edits.** Add Preset wrappers for track
MIDI channel/note writes. They validate the Scene/track, compare old and new
SceneData value, call the existing SceneData setter, and mark Sceneset. This
removes the two untracked direct writes in `menu_parseGlobalParam()` without
making `SceneData` depend on Autosave.

**Change: typed load barrier.** Each typed load entry—Kit, Kit Morph, Scene,
Bank, normal Instrument, and temporary Instrument—first captures its existing
request fields, requests an Autosave barrier, and sets the existing Preset
status variable to new `PRESET_AUTOSAVE_BARRIER`. Add
`preset_tickAutosaveBarrier()`, called from the main foreground loop. Once
Autosave reports every dirty record flushed, it dispatches the original
`filesystem_requestLoad*` using those fields and the correct existing callback.
If the drain fails, it publishes the original Preset operation as an error;
Menu's existing `menu_pollPresetStatus()` error path releases its busy state.
No callback pointer or duplicate pending-coordinate object is required.

**Change: load commits and Bank lifecycle.** Add marks at the actual commit
boundaries, not merely at request acceptance. `filesystem_loadKitDirectory_tick()`
copies `op_staged_kit` directly into every target Scene before
`on_kit_load_complete()` runs; that callback must mark a batch containing
Kitset and all six Instrument records for each target. The root-Scene path
copies settings and kit in `filesystem_commitSceneStage()`; on a successful
root Scene completion, `on_scene_load_complete()` marks its Sceneset, Kitset,
and six Instruments as one batch. Its Bank-child use is excluded because a
Bank Load must first overlay/recover the blob, not create a fresh dirty image.

Normal Instrument Load commits in `preset_startInstrumentApplyImage()`, after
the filesystem callback and during Menu's bounded apply flow. Mark there,
inside the destination-scene loop immediately after `scene->kit.instruments`
is assigned: a batch has Kitset plus the changed Instrument record because
the copy can change the slot type. Kit Morph and Instrument Morph commit later
in `preset_startKitMorphApply()` / `preset_startInstrumentMorphApply()`; mark
only the endpoint records whose compatible retained image actually changed.

For Bank Load, first let filesystem complete its normal Bank/child stage, then
start blob read/apply before publishing `PRESET_OP_BANK_LOAD` as update-ready;
this preserves the existing boot/Menu completion contract. `on_bank_save_complete`
starts the asynchronous reset only after it observes filesystem success, then
reports `PRESET_OP_BANK_SAVE` after reset success or reports its reset error.

**Mutation gate.** User edit APIs return failure/no-op while the barrier or
typed loader owns stage. Callers that mirror a value outside Preset must update
that mirror only after acceptance. In particular `MidiParser.c` currently
writes `parameter_values[PAR_MORPH]` before `preset_morph()`; change that path
to mirror/notify only when the retained edit is accepted. Calls from
`InstrumentManager.c` can continue to ignore an edit return if the intended
behavior during a load is “no retained velocity modulation”; its runtime-only
helpers remain untouched.

### 6. `Core/Menu/menu.c`

**Change.** Replace the direct `scene_setTrackMidiChannel()` and
`scene_setTrackMidiNote()` calls in `menu_parseGlobalParam()` with the new
Preset edit wrappers. Preserve the existing compatibility mirror of the track
channel only after the wrapper accepts the retained edit.

**Why.** These two source paths are the only Menu direct mutations of
SceneData settings found in the audit; leaving them direct would create a
silently non-autosaved parameter class.

**Bankset marks.** After `menu_perfModeSceneButtonPressed()` changes the active
Scene and after `menu_voiceHeldSceneButtonPressed()` toggles the VOICE edit
mask, compare the old BankData value and call `autosave_markBankset()` only if
it changed. Do not place marks inside `bank_selectActiveSceneForEditMask()` or
`bank_toggleSceneMaskVoiceEdit()`: filesystem Bank load/save also calls the
BankData setters and must not manufacture a user dirty record.

**Load/Save UI.** Existing callers already set `menu_storageBusy` when Preset
accepts a load and `menu_pollPresetStatus()` treats every non-update status as
busy. The new barrier therefore needs no second spinner allocation. Add the
barrier operation to the existing failure/completion mapping so an autosave
drain failure returns to the Load/Save type row with the existing filesystem
error overlay. A Bank Save remains busy through its blob-reset leg.

### 7. `Core/MIDI/MidiParser.c`

**Change.** Make CC1 global and per-voice Morph handling honor the new Preset
edit result before writing `parameter_values`, notifying Menu, or recording
the MIDI-originated value. This is a small but necessary call-site change,
because the current code mirrors before it calls `preset_morph()`.

**Why.** A typed-load barrier deliberately rejects external MIDI edits. Without
this change, a rejected retained edit would still repaint as a
successful one and diverge from the later autosave snapshot.

**Affiliates.** No raw filesystem or ledger call is added here; Preset remains
the mutation authority. `InstrumentManager.c` is audited but needs no source
edit if it accepts a no-op retained velocity modulation during the barrier.
Its calls already enter Preset setters, and normal performance behavior is
protected by the changed-value debounce.

### 8. `main.c` and `Makefile`

**`main.c` change.** Include `Autosave.h`. In the normal post-audio loop, call
`preset_tickAutosaveBarrier()` and `autosave_tick(time_sysTick)` after Menu and
Morph foreground services but immediately before `filesystem_tick()`. Keep the
existing `audio_check_and_render()` on both sides. The barrier dispatches a
typed load only after the autosave writer's final flush, while the scheduler
can post a blob operation and allow the existing filesystem tick to begin it
that same foreground pass.

**`Makefile` change.** Add `Core/Bank/Scene/Autosave.c` to `SRCS` beside
`SceneData.c`/`BankData.c`. Do not add libraries, generated sources, linker
regions, or an SRAM section: the union assertion is the RAM contract.

### Audited modules with no direct source change

`Core/Bank/Scene/SceneData.c/.h` and `Core/Bank/BankData.c/.h` remain raw
retained-data owners. Their public setters are used by both user paths and
filesystem load normalization; embedding autosave marks in them would make
imports, boot defaults, and recovery apply look like new edits. `storageTypes`
also remains unchanged: it owns current text import/export syntax, whereas the
blob's versioned binary codec belongs to Autosave. `PatternData`, Pattern file
state machines, HCNAMES/index cache code, and effects placeholders receive no
autosave path in this milestone.

### Required implementation comments and local invariants

Every new function needs a normal what/why/input/output/affiliate comment, but
the following internal operations must additionally be commented at their
loops and arithmetic rather than only at function entry:

- the 292-sector format cursor, including 16-bit cursor/32-bit byte-offset
  multiplication and why headers are written last;
- both partial sector I/O offsets, including their 0..512 invariant and the
  AsyncFATFS short-read/short-write retry behavior;
- the 145-key ledger scan and its forced-versus-idle ordering comparison;
- the `index - 1` quotient/remainder mapping to a Scene's nine logical keys;
- the two-slot A/B winner choice, generation comparison, key/epoch rejection,
  and lower-generation fallback after a torn write;
- CRC's outer byte and inner eight-bit loops, initial/final XOR, reflected
  polynomial, and exact coverage span;
- every Scene batch loop: member-mask traversal, nonzero batch-id allocation,
  commit-after-members condition, and reader acceptance condition; and
- the two Instrument endpoint loops, each exactly `INSTRUMENT_PARAM_COUNT`
  (currently 64) bytes, including why the derived interpolation image is
  skipped and why the 6-slot Kit loop uses an unsigned slot index bounded
  before it indexes the retained type array; and
- all payload cursor increments and range checks before converting a byte to a
  scene/slot/type/mask index.

Do not add a generic one-line “autosave dirty” helper that hides these
conditions. The code is small enough that owner-specific changed-value checks
are clearer and cheaper than a callback registry. Conversely, avoid separate
one-client wrappers that only forward a static key; use typed domain helpers
where they also enforce ownership or normalization, and local static mapping
helpers where they merely encode fixed index math.

### Verification manifest

1. Build the production target and inspect the map: the stage union must stay
   within 2,048 bytes and no new `.bss` ledger, CRC table, or filesystem handle
   is introduced.
2. Add a host-side codec/ledger test target only if it can compile the pure
   Autosave value helpers without the MCU runtime. Cover known CRC vectors,
   payload round trips, bounds, malformed length/key/epoch/CRC, A/B selection,
   batch rejection, and unsigned tick wrap.
3. Add AsyncFATFS fault-injection/card-image tests for partial sector write,
   close, and flush interruption. Verify recovery chooses the previous valid
   record and then the committed Bank—never a mixed Kit type/image batch.
4. On hardware, measure first 292-sector format time, per-record write time,
   boot overlay scan time, and maximum audio impact. Exercise repeated MIDI
   CC, velocity-owned retained targets, sixteen-Scene VOICE fan-out, typed
   load during dirty state, Bank Save reset, wrong-Bank header, and power cuts
   at every format/write/commit/flush boundary.

## Implementation notes — 2026-07-26

The first implementation pass is now in the source tree. It deliberately uses
the existing `fs_stage_workspace` union: the linker still reports that object
as exactly 2,048 bytes, and `Autosave.c` contains no file-static ledger or CRC
table. `autosave_workspace_t` has a compile-time fit assertion in
`filesystem.c`.

- Added `Core/Bank/Scene/Autosave.{h,c}` and the Makefile source entry. The
  module supplies the compact explicit binary payload codec, IEEE CRC-32,
  145-key dirty ledger, five-second/30-second scheduling, A/B record keys,
  batch IDs, and wrap-safe 16-bit tick arithmetic.
- Added `.hcbnksv` operations to `filesystem.{h,c}`. Initial creation writes
  all 292 sectors in sequence because AsyncFATFS cannot seek past EOF; it then
  writes the B and A header sectors. Record writes alternate sectors, close,
  and use the existing final flush state. Restore reads both headers and makes
  a commit-only pass before applying batch-compatible winners.
- Added the Autosave barrier status to Preset. Existing request coordinates are
  reused while dirty records drain; current Kit, Scene, Bank, Instrument,
  Instrument Morph, and temporary Instrument loaders dispatch only after that
  drain. The boot loop advances the same barrier.
- Routed the direct STEP track MIDI settings through Preset, marked actual
  retained changes by domain, marked Bank active/edit-mask changes in Menu,
  and made MIDI mod-wheel updates silent no-ops while a barrier or typed load
  is active.
- A successful Bank Save now reformats the fixed register after the committed
  Bank tree has reached its normal durable completion, making that tree the
  fresh baseline.

`make -j2` succeeds after this pass. The pre-existing unused-function and
newlib syscall linker warnings remain; no new compile warnings were emitted by
the Autosave sources. Hardware/card fault-injection coverage is still required
before treating the format as release-ready.

### Follow-up correction — 2026-07-26

Hardware boot exposed three lifecycle defects in the first pass. The barrier
now ignores the stage union until it has been initialized as an autosave
workspace, so the first Bank Load cannot treat arbitrary loader bytes as dirty
flags and wait forever. A missing or invalid blob is now formatted immediately
after the committed Bank has loaded, so `/.hcbnksv` exists before the first
edit. Finally, the unchanged supplemental-parameter path again installs its
active DSP binding during startup/load; it bypasses only the dirty mark, not
the runtime apply. The corrected firmware passes `make -j2`, `make img`, and
`git diff --check`.

### Hardware-report correction — 2026-07-26

The reported `FsERR` on Bank slot 000 and absent root blob identified a second
directory-lifetime bug: a completed Bank/Kit transaction can leave AsyncFATFS
inside a Bank subdirectory, whereas the autosave contract names the file
`/.hcbnksv`.  Every autosave open/create now first asynchronously changes to
the volume root.  This makes both the read path and first-create path
independent of the directory used by the preceding load.

The same report showed that `autosave_mutationAllowed()` had conflated two
separate questions: whether a typed loader owns the shared stage union, and
whether an initialized, resident Bank currently has an autosave ledger.  The
function now answers only the former (`PRESET_IDLE` and the brief pending-load
handoff permit normal edits).
Each dirty-marker independently requires an initialized workspace and a
resident Bank.  Consequently voice-mode edits remain functional after a
missing/failed Bank/blob operation, while they do not reinterpret loader bytes
as ledger state during a typed load.

### Foreground-latency correction — 2026-07-26

Autosave is a slot-delta register, not a prerequisite for editing or loading.
The initial barrier implementation incorrectly drained every dirty record
before dispatching a Kit/Scene/Instrument/Bank load, which made user-visible
loads scale with unrelated pending deltas. The dispatcher now waits only for
an already-active filesystem transaction, then gives the requested typed load
the shared stage workspace immediately. While that short handoff is pending,
the workspace remains a ledger and live edits remain legal.

Typed loaders necessarily overwrite that ledger union. After every successful
normal Kit, Scene, or Instrument import, the firmware therefore rebuilds it
and marks a complete parameter-only Bank snapshot for background convergence.
This deliberately trades occasional background writes for the required user
experience: no foreground load waits for a dirty-register flush, and an
unrelated pre-load dirty slot cannot disappear when the shared workspace is
reclaimed. KitMrp and InstrumentMrp now do the same after consuming their
typed image, including a type-mismatch no-op, before releasing the workspace.
