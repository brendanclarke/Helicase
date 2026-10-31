# S090 P1 — Load/Save Name Fetch Speed-Up While Playing

**Status:** Plan and decision record (2026-10-31). Not implemented.
**This file is the single authority** for S090 P1 decisions and findings.

**Symptom (user):** boot, press PLAY, open Load/Save, cycle a few pages: the
file-name rows stay blank for many seconds.

**Goal (user):** faster names on screen after entering Load/Save, and faster
changes between Load/Save items, **while playing**.

**Evidence:** `SD_CARD_TEST/asavetrc.bin` (176,790 records, many boots) and
`SD_CARD_TEST/pattrace.bin`, decoded with
`tools/diagnostic_tools/decode_devlogs.py`. Phase timings come from pairing
the `N` (Instrument entry) request/complete records.

---

## 0. Decision record

### 0.1 User decisions (2026-10-31), in order

| # | Question | User decision | Effect on the plan |
|---|---|---|---|
| U1 | First-round Q3: lazy `.hctmp` at first preview? | Accepted | **Superseded by U4.** Lazy `.hctmp` is no longer planned. |
| U2 | First-round Q8: CPU headroom? | "There is basically no constant headroom for CPU use at all." | Items that **remove SD work** come first. Items that use idle time (item 1) are measured and kept only if they help. |
| U3 | Toggle all dev logging off and test | Done on hardware: no improvement in Load/Save speed. Logging restored. | Logging is **not** the cause (§1.3). Keep the gating fixes made during the test. |
| U4 | May the +129 B be used for an Instrument snapshot? | Yes, if it helps | It does: item 2 replaces `.hctmp` with a RAM snapshot taken at entry. |
| U5 | 131 B: the 129 B snapshot plus 2 B validity state? | Approved | Validity state was redesigned to 1 B (item 5); 130 B used. |
| U6 | Q5: risk of stale names from reuse? | "No wrong names ever." | Item 5: a default-deny validity rule; reuse only when correctness is guaranteed by construction. |
| U7 | Q7: keep more than one index in RAM? | First "use the RAM", then corrected: caching every index is impossible and the 4,176 B pool is **not approved** | No index pool, no multi-index store (§0.3). One browser cache only. |
| U8 | Q8: boot seeds the active Scene's resident names | Approved ("make that addition now") | Item 6c. Index loading at boot is **not** part of it (§0.3). |
| U9 | RAM read-back | Approved items OK; withdrawn items "incorrect and should not be considered" | §0.2 ledger is final. |
| U10 | Keep the 1 B names-stale flag and the 4 B logging-only counters? | "That's fine." | Both kept (§0.2). |

### 0.2 Approved RAM (final)

| Item | Bytes | Region | Owner | Build |
|---|---:|---|---|---|
| Instrument snapshot (item 2) | 129 | SRAM1 `.bss` | `presetManager.c` | all |
| Resident names-stale flag (item 5) | 1 | SRAM1 `.bss` | `filesystem.c` | all |
| SD throughput counters (item 4c) | 4 | SRAM1 `.bss` | `filesystem.c` / `main.c` | logging only |
| **Static total** | **130 all builds + 4 logging-only** | | | |
| Restore staging slot (item 2) | 196 | stack, foreground, only during a restore | `presetManager.c` | all |

- Default `.bss` is SRAM1, earmarked for future Pattern data. About 94 KB is
  free there today with logging on (`_ebss` 0x2006508C, SRAM1 ends
  0x2007C000).
- Nothing else may be allocated without a new user sign-off (bytes, region,
  owner).

### 0.3 Withdrawn — do not reintroduce

- **Library index pool** (4,096 B data + 80 B directory, "hold every recent
  index"). Rejected by the user.
  - **Why:** each slot-numbered type (Kit, Scene, Bank, Pattern) allows 1,000
    entries (up to ≈9 KB each at 9 B per name), and new Instrument types will
    keep adding indexes. A pool sized to one test card is not a design.
- **Loading indexes at boot.** Withdrawn with the pool.
  - **Why:** without the pool there is one browser cache holding one index
    at a time. Each index loaded at boot overwrites the previous one, and any
    Load/Save type change overwrites it again, so boot loading saves nothing.
    At best it would cover the first page, and only if nothing else used the
    shared buffer first; `.hcnames` reads and copy/clear also use it.
- **Lazy `.hctmp` at first preview** (former item 2 and U1). Superseded by
  the RAM snapshot (U4).
- **Earlier estimate "a second index costs about 1 KB".** That was wrong for
  slot-numbered indexes (up to ≈9 KB). Do not quote it.

### 0.4 In the plan but not individually approved

The user accepted the review read-back of these ("That looks ok"). Each is
still to be confirmed individually before it is implemented:
- item 6a: keep the browser cache across Load/Save exit and reuse it;
- item 3b: hold back `settings.cfg`/`midilrn.cfg` writes while a Menu request
  is pending (5 s ceiling);
- item 5e: the logging-only shadow check;
- item 4a: trace rotation at boot.

---

## 1. What the trace shows

### 1.1 The last boot (records #176741–#176789)

Nested Instrument entry runs three serialized facade operations, and the
names appear only after the last one (`menu.c`
`menu_requestInstrumentEntryNames()`):

1. `/.hcnames` read (resident Kit + six Instrument names);
2. `.hctmp.<ext>` write (hidden snapshot of the current voice, backs the
   `kit` row);
3. `Instrument/<type>/.hcindex` read (the name list).

| Phase (ticks = ms) | Start → end | This boot | Earlier median on this card |
|---|---|---:|---:|
| HCNAMES read | 8979 → 11654 | **2.7 s** | 0.20 s (n=37) |
| `.hctmp` write | 11654 → 20843 | **9.2 s** | 0.52 s (n=91) |
| `.hcindex` read | 20843 → 23848 | **3.0 s** | 0.17 s (n=113) |
| re-entry: two `temp request FAILED=1` (facade busy) | 24743, 26327 | ~5 s gap | — |
| second `.hctmp` write | 29684 → 39750 | **10.1 s** | — |
| second `.hcindex` read | 39750 → 42166 | **2.4 s** | — |

Entry to visible names: **15.3 s**. The three most recent boots are all slow
(HCNAMES 1.2–2.7 s, temp 2.9–10.1 s); the latest is about twice as slow as the
two before it.

### 1.2 What it is not

- **AutoSave:** never admitted in the window. A `W` record shows the writer
  held by the Load/Save page guard, and every `H` budget report shows
  `repair`, `scalar` and `pattern` at `charged_ms=0`.
- **Pattern work:** `pattrace.bin`'s last session holds only boot-time
  records. `decode_devlogs.py` has no `pattrace.bin` support and labels its
  records with AutoSave stage names; by `PatternTrace.h` the `V`/`L` records
  are `REPAIR_RESERVE`/`REPAIR_RELOC` (item 4b).
- **The dev trace, mostly:**
  - Both trace flush schedulers are gated by `menu_isLoadSaveCommandActive()`.
    That is set only by an accepted OK-press command
    (`menu_beginLoadSaveCommand()`), **not** by browse fetches.
  - So a flush cannot interrupt a phase, but it can take the idle facade
    between phases on a Load/Save page.
  - That is the most likely owner of the ~5 s busy gap above. The AutoSave
    writer is page-suppressed, and the deferred HCNAMES flush does not run on
    Load/Save pages.
  - Inferred, not witnessed: the trace has no record of its own flush
    start/end.

### 1.3 Logging off/on test (U3, 2026-10-31)

| Build | Flash (text) | data | bss |
|---|---:|---:|---:|
| `DEV_MODE_LOGGING 1` (before and after) | 683,512 | 424 | 412,884 |
| `DEV_MODE_LOGGING 0` | 667,072 | 420 | 396,064 |

- **Hardware result:** no improvement in names-on-screen or Load/Save item
  changes with logging off. Logging is not the cause; remaining slowness is
  the SD path.
- **Gating audit:** AutosaveTrace and PatternTrace have empty `#else` stubs;
  LTO removes every call. The trace rings and cursors, boot logger, HCPRMS
  boot capsule, trace flush ticks, and the drop counters in buttonHandler,
  LiveRecord, sequencer, menu and Autosave are all gated.
- **Fixed during the test (kept):** five logging-only helpers in
  `filesystem.c` were defined outside `#if DEV_MODE_LOGGING` while every
  caller was gated. Whole definitions (and two forward declarations) are now
  gated:
  - `filesystem_autosaveTraceSerialize()`
  - `filesystem_patternTraceSerialize()`
  - `filesystem_autosaveTraceFlushCompleted()`
  - `filesystem_patternTraceFlushCompleted()`
  - `filesystem_resetFacadeForBootLogRecovery()`

  The logging-on image is byte-for-byte the same size after the fix.
- **Pre-existing, untouched:** seven `-Wunused-function` warnings in
  `filesystem.c` in both builds:
  - four dead meta/globals helpers: `filesystem_metaHasStoredGlobalsLen`,
    `filesystem_applyLegacy22Globals`, `filesystem_staleMetaPrefixLen`,
    `filesystem_applyStaleGlobalsFallback`. They are candidates for removal
    under the no-backward-compatibility rule;
  - three helpers whose callers sit in `#if 0` blocks (lines ≈24096–24344,
    ≈25002–25857): `filesystem_reportBootSubstep`, `filesystem_blockRename`,
    `filesystem_bootLoggingSetKitDetail`.
- **`DEV_STALL_DETECTION` stays 1.** It is an operation-abort safety net,
  not a logger. `DEV_LOGGING_IWDG` and `DEV_MODE_DIAGNOSTIC` stay 0.
- **Current state:** `DEV_MODE_LOGGING 1`;
  `build/LXRV2_lxr02.img` = 683,936 B.

## 2. Root cause

While audio plays, foreground Load/Save I/O gets **one `afatfs_poll()` per
main-loop pass**, and one poll clocks one 16-byte bit-bang burst
(`SDCARD_BURST_SIZE`, `sdcard_lxr02.c:96`).

- **Fast drain is out of reach:** the four-pass fast drain
  (`FS_FAST_DRAIN_POLL_PASSES`, `filesystem_tick()`) runs only when
  `menu_beginNoPlaybackStorage()` has suspended audio, i.e. Scene/Bank/Pattern
  commands (`menu_loadSaveCommandInNoPlaybackScope()`). Instrument browsing
  and Kit-page name fetches are outside it by design.
- **The existing idle pump is gated elsewhere:** the S082 P2 pump
  (`main_serviceBackgroundLoadIdle()`, `main.c:249`, budget
  `BG_LOAD_IDLE_FS_BUDGET_US` = 200 µs, `config.h:868`) only serves
  background Bank loads.
- **Loop rate sets throughput:** SD throughput is set by how often the main
  loop comes round. With no constant CPU headroom (U2), that is roughly once
  per render. The worsening over the last three boots fits the current
  Oscillator/DrumVoice work raising render cost (inferred; the trace records
  neither transport state nor CPU load).
- **The overhead dominates, not the data:**

  | File | Size on `SD_CARD_TEST` |
  |---|---:|
  | `.hcnames` | 2,047 B |
  | `Kit/.hcindex` | 1,336 B |
  | `Scene/.hcindex` | 1,488 B |
  | `Bank/.hcindex` | 1,232 B |
  | `Pattern/.hcindex` | 1,032 B |
  | `Instrument/Drum/.hcindex` | 1,080 B |
  | other Instrument `.hcindex` | 360 B each |
  | `.hctmp.*` (stale files) | 676–1,389 B |

  - Each read also scans directories: the root (51 entries), `Instrument/`,
    then the type folder (`Instrument/Drum` has 124 entries).
  - Each read also does cluster and FAT lookups.
  - asyncfatfs caches only 8 sectors (`AFATFS_NUM_CACHE_SECTORS`, 4 KB),
    so every scan goes back to the card.
  - A `.hctmp` write adds create, allocate, write, close and sync, with card
    busy-waits. It is the largest single phase.
- **Consequence:** with no CPU headroom, the effective fix is **fewer card
  operations**, not faster ones.

## 3. Facts the implementation depends on

**Shared name cache.**
- `fs_list_cache_name[1000][9]` (≈9 KB, `filesystem.c` ≈1058), tagged by
  `fs_list_cache_kind` / `fs_list_cache_type` / `fs_list_cache_count`.
- It holds **one** index at a time, and `.hcnames` reads use it too
  (`FS_NAME_CACHE_HCNAMES`).
- Copy/clear borrows it for the 161-row HCNAMES remap
  (`FS_NAME_CACHE_COPYCLEAR`), and the deferred HCNAMES flush reuses it.

**Index layouts.**
- Slot-numbered (Kit, Scene, Bank, Pattern): 1,000 rows each, mostly empty.
  The cache is indexed by slot.
- Instrument: a dense sorted list per type (Drum 120, Snare/Cymbal/HiHat 40
  each on this card). New Instrument types will add more indexes.

**Reuse that exists, and where it is missing.**
- **Kit already skips a reload** when the cache holds the Kit index
  ("Branch B", `menu_requestKitEntryNames()` →
  `filesystem_libraryNameCacheLoaded(FS_LIBRARY_INDEX_KIT)`).
- **Instrument does not:** `menu_requestInstrumentIndexLoad()` always calls
  `filesystem_clearNameCache()` and reloads.
- **Load/Save exit discards everything:** `menu_switchPage()` (≈18814)
  clears `menu_residentNameScratchValid` and calls
  `filesystem_clearNameCache()`. Every new session therefore re-reads
  `.hcnames` + one index.
- **Within a session the seven-name scratch survives:** the trace's second
  entry had no HCNAMES read. A voice change re-does `.hctmp` + index.

**Instrument slot and restore path.**
- `kit_instrument_slot_t` (`SceneData.h:54`) = `instrument_type_t` (4 B
  enum) + three 64-byte images: Normal, Morph, `morph_interpolation`. That
  is ≈196 B; `INSTRUMENT_PARAM_COUNT` 64, `instrument_param_value_t` =
  `uint8_t`.
- The file loader resets the staged slot with `instrumentManager_resetSlot()`,
  then parses only Normal and Morph (`filesystem.c` ≈17083–17155). Nothing on
  the file path writes `morph_interpolation`. So a 129 B snapshot (type +
  Normal + Morph), expanded through `instrumentManager_resetSlot()`,
  reproduces today's file restore.
- **An in-RAM apply path already exists:**
  `preset_startInstrumentApplyImage(staged, mask, slot, source_slot, type,
  mark_autosave)` (`presetManager.c` ≈2664). It is used by the file load
  (`preset_startInstrumentApply()`) and copy/clear Instrument copy
  (`preset_startInstrumentCopy()`). It copies `*staged` into the Scene before
  returning, so a stack staging slot is safe.
- **Temp-path consumers to replace:**
  - `preset_saveInstrumentTemp`, `preset_saveInstrumentMorphTemp`,
    `preset_loadInstrumentTemp`, `preset_loadInstrumentMorphTemp`;
  - `filesystem_requestSaveInstrumentTemp`, `…SaveInstrumentMorphTemp`,
    `…LoadInstrumentTemp`, `…LoadInstrumentMorphTemp`;
  - `filesystem_loadedInstrumentWasTemporary()` (read in `menu.c` ≈17541,
    17898, 17923);
  - `PRESET_OP_INSTRUMENT_TEMP_SAVE` / `…MORPH_TEMP_SAVE` /
    `…MORPH_TEMP_LOAD` handling in `menu.c` ≈17513–17541 and ≈17844–17998;
  - `menu_prepareInstrumentLoadTemp()` (≈8296) and
    `menu_restoreInstrumentLoadTemp()`.
- **`menu_init()` clears identity rows** (≈19944), which matters for the
  boot seed (item 6c).

**Trace flush gates.**
- `filesystem_autosaveTraceFlushSchedule_tick()` (≈27260) and
  `filesystem_patternTraceFlushSchedule_tick()` (≈5980) check only
  `menu_isLoadSaveCommandActive()`.
- Flush intervals: `AUTOSAVE_TRACE_FLUSH_INTERVAL_MS` 500, and
  `PAT_TRACE_FLUSH_INTERVAL_MS` 1000.
- `AUTOSAVE_TRACE_RECORD_COUNT` is 2,048 (the temporary expansion).

## 4. Constraints that apply

- **Load/Save scrolling mandate** (`FILESYSTEM_SPEC.md` §Binding Mandates):
  number first, name next, LEDs last; no stage may hold `menu_storageBusy`
  while the user scrolls. Today the Instrument entry chain holds it across
  all three phases. Nothing may lengthen a hold. RAM restores and cache hits
  take none.
- **SD work stays asynchronous; no 1 ms block** in the main loop or ISRs at
  priority ≤ 4. Boot-only synchronous polling before audio is allowed (items
  4a, 6c).
- **Render keeps priority;** no DSP work is skipped (`CPU_USE_DSP_AUDIT.md`).
- **No wrong names, ever (U6).** Item 5 is the only way names in RAM may be
  reused.
- **RAM:** §0.2 only.
- **Logging allocations** exist only with `DEV_MODE_LOGGING 1`.
- **No backward compatibility:** no reader or migration for `.hctmp` files or
  old trace layouts.
- **Failed approaches (MEMORY.md):**
  - no blind 1 ms pacing (S046);
  - no bundled devlog consolidation, and no logger recovery that hides the
    original failure (S046);
  - never treat a failed open as proof of absence;
  - do not borrow `fs_list_cache_name` for unrelated data;
  - keep boot-reader type retention untouched;
  - durable name identity is corrected at the Load/Save commit boundary.

## 5. The items

### Item 1 — Idle pump for Load/Save I/O (measured, keep only if it helps)

**Change:** widen `main_serviceBackgroundLoadIdle()` from "background Bank
load running" to also cover:
- a Load/Save page open with the facade BUSY;
- the deferred HCNAMES flush running after exit.

The loop body is unchanged:

```c
while (filesystem_status() == FS_STATUS_BUSY &&
       audioCodec_queueFreeSlots() == 0u &&
       elapsed < BG_LOAD_IDLE_FS_BUDGET_US)
    filesystem_tickUncounted();
```

Rename the helper (e.g. `main_serviceStorageIdle()`) and add a Menu accessor
so `main.c` does not read Menu state. `!audioCodec_isSuspended()` stays; fast
drain owns the suspended case.

**Gate (U2):** it only runs while both render slots are full, which is rare
at the CPU ceiling. Keep it only if item 4c's counters show more polls/s with
no extra underruns. It costs no RAM.

**Logging on/off:** identical.

### Item 2 — Instrument snapshot in RAM (removes `.hctmp`) — U4

- **What:** the hidden `.hctmp.<ext>` write at entry and the file read on
  `kit` restore are replaced by a RAM image of the entry voice. Entry writes
  nothing, and the restore touches no file.
- **Snapshot moment:** entry, as today.

**Data (129 B, SRAM1 `.bss`, owner Preset, `presetManager.c`). One voice,
never a Kit:**

```c
static struct {
    uint8_t type;                                            /* instrument_type_t */
    instrument_param_value_t normal[INSTRUMENT_PARAM_COUNT]; /* 64 */
    instrument_param_value_t morph[INSTRUMENT_PARAM_COUNT];  /* 64 */
} pm_instrument_snapshot;
```

`morph_interpolation` is not stored (§3). Menu keeps its session flags and
the existing 9-byte original name (`menu_instrumentTempName`).

**API replacing the four temp functions:**

| New | Replaces | Behaviour |
|---|---|---|
| `preset_captureInstrumentSnapshot(scene, slot)` | `preset_saveInstrumentTemp()` | copy `type` + both images; synchronous |
| `preset_captureInstrumentMorphSnapshot(scene, slot)` | `preset_saveInstrumentMorphTemp()` | copy `type` + Morph image |
| `preset_restoreInstrumentSnapshot(scene, slot, type)` | `preset_loadInstrumentTemp()` | below |
| `preset_restoreInstrumentMorphSnapshot(scene, slot, type)` | `preset_loadInstrumentMorphTemp()` | Morph-only commit of Morphable cells through the existing Morph-only apply (`instrument_apply_morph_only`) |

**Restore.** Use a stack `kit_instrument_slot_t` (196 B):
1. `instrumentManager_resetSlot(&staged, type)`;
2. copy Normal/Morph for the type's `descriptor_count` entries;
3. `preset_startInstrumentApplyImage(&staged, 1u << scene, slot, slot,
   type, 1u)`.

**Completion contract:**
- Menu's completion switches keep their structure; Preset publishes the
  same completed-op codes.
- `filesystem_loadedInstrumentWasTemporary()` becomes a Preset-owned "last
  apply came from the snapshot" flag.
- Trace phases 6/7 (temp request/complete) are retired in favour of one
  "snapshot captured" record. `AutosaveTrace.h`, `COPYCLEAR_UTILITIES.md`
  §15 and the decoder change together.

**Remove** the four `filesystem_request*InstrumentTemp*` functions, their
operation phases, `filesystem_loadedInstrumentWasTemporary()` and the
`.hctmp` filename builders.

**Keep** `filesystem_instrumentStemIsReservedTemporary()`, so stale
`.hctmp.*` files on cards stay invisible. Document them as safe to delete;
no cleanup code.

**Live changes during browsing:** step automation, MIDI or live record
writes made while browsing are discarded by a `kit` restore. That is
today's behaviour with the file.

**Verification V1:** before deleting the file path, a temporary logging-only
check runs both restores on the same voice. The resulting `type`, Normal and
Morph must match byte for byte for all four types, in Normal and Morph
modes.

**Expected:** removes the 9–10 s phase at entry and on every voice change,
plus the restore file read.

**Logging on/off:** identical.

### Item 3 — Menu requests first on Load/Save pages

**3a. Trace flushes wait for exit.**
- Both trace flush schedulers also return while `menu_activePage` is
  `LOAD_PAGE`/`SAVE_PAGE`, matching the AutoSave writer's page guard.
- Keep the one-shot `TRACE_SUPPRESSED` witness, with a new flag bit for
  "page".
- A flush already running when the page opens finishes normally.
- **Spec change:** `DEV_MODES.md` §Scheduling ("after that command has
  finalized it may append on a Load/Save page") becomes "deferred while a
  Load/Save page is open".
- **Logging off:** compiled out.

**3b. Settings and MIDI Learn writers yield (§0.4).**
- `settings.cfg` and `midilrn.cfg` writers do not **start** while Menu has
  a pending or deferred storage request (new
  `menu_hasPendingStorageRequest()`).
- **Ceiling:** after 5 s of waiting they start at the next idle facade, so
  a setting is never postponed indefinitely.
- **Logging off:** this is product code, so it stays active.

**3c. Prompt retry.** Confirm a deferred Menu request
(`menu_deferSelectionRequest`) is retried on the first idle tick after the
facade frees; fix it if it waits for a later cadence.

**RAM:** none (existing witness byte).

**Risk:** trace ring overflow on long page sessions. Each Instrument preview
emits about 84 records, so about 24 previews fill the 2,048-record ring.
Overflow is counted by the `G` record (§8 Q5).

### Item 4 — Measurement, decoder, trace hygiene (logging only)

**4a. Boot rotation** (pre-audio):
- **When:** if `asavetrc.bin` exceeds `DEV_TRACE_ROTATE_BYTES` (proposed
  256 KiB), rotate one generation:
  1. delete `asavetr1.bin` if present;
  2. rename `asavetrc.bin` → `asavetr1.bin`;
  3. the next flush recreates `asavetrc.bin`.

  Same for `pattrace.bin` → `pattrac1.bin` (proposed 64 KiB).
- **Presence checks:** use a completed, closed, case-insensitive scan
  classifying zero / one / multiple matches. Multiple matches → skip and
  record an anomaly. Never use a failed open.
- **Failure handling:** a rotation failure never fails boot or hides
  another failure (S046). Leave the files, record one trace record,
  continue.
- **Cost:** directory-entry work only; measure the boot cost.
- **Speed effect:** small. This is hygiene and decode time, not the cause.

**4b. Decoder** (`decode_devlogs.py`, read-only):
- split per boot at the boot-reader summary (`Q` flags 0x80);
- per-boot phase timing for each name source (HCNAMES, snapshot, index,
  payload, cache hits): min / median / max and start ticks;
- `pattrace.bin` support from `PatternTrace.h`;
- `*1.bin` generations;
- every new record from items 2, 3, 5 and 6.

**4c. Throughput counters** (4 B, logging only, §0.2): count main-loop
passes and `afatfs_poll()` calls. Publish both once per 5 s budget report as
one extra `H`-family record. This is the evidence for item 1 and for the
optional items in §7.

### Item 5 — Name validity rule ("no wrong names ever", U6)

Every reuse of names already in RAM (items 6a–6c) goes through this rule
and nothing else. It is **default-deny**.

**5a. Two domains.**

| Domain | Covers | Invalidated by |
|---|---|---|
| Library | the browser-cache tag (item 6a) | anything that can change any `.hcindex`, any library folder or any library row |
| Resident | the seven-name scratch (6b) and the boot seed (6c) | anything that can change any Scene's resident Kit or Instrument name or content |

**5b. Every facade operation is classified at compile time.**
- **One table:** indexed by `FS_INTERNAL_OP_*` in `filesystem.c`, with two
  bits, `LIBRARY_NEUTRAL` and `RESIDENT_NEUTRAL`, and a one-line reason per
  row.
- **Zero means not neutral:** an entry left at zero invalidates both
  domains.
- **Build check:** the table is sized by the op count, so an unclassified
  new op fails the build.
- **Trigger point:** `filesystem_start()` invalidates at operation start, so
  a failed or cancelled op has still invalidated.

Proposed neutral set:

| Operation | Library neutral | Resident neutral | Reason |
|---|:-:|:-:|---|
| Index load / rebuild of type T | yes (re-validates T only on DONE) | yes | publishes T itself |
| HCNAMES read | yes | yes (re-validates the Scene read) | it is the resident source |
| Bank preview scan | yes | yes | read only |
| Trace flushes | yes | yes | root `*.bin` only |
| `settings.cfg`, `midilrn.cfg` | yes | yes | no names |
| Scalar AutoSave writer (`.hcprms*`) | yes | yes | parameters only |
| Pattern AutoSave drains (`.patNNx`) | yes | yes | Pattern data only |
| Instrument / Kit / Scene / Bank loads | yes | **no** | resident names change |
| Every Save, delete, rename, repair, deferred HCNAMES flush, sample install, copy/clear op | **no** | **no** | may change either |

**5c. Non-facade name changes** invalidate through
`filesystem_invalidateLibraryNames()` / `filesystem_invalidateResidentNames()`:
- the copy/clear raw-block holder, on acquire **and** release
  (`copyClear_backgroundSuspended()` edges);
- Preset RAM image applies that change Scene Instrument or Kit content.
  This includes `preset_startInstrumentApplyImage()`, and therefore the item
  2 `kit` restore. That costs one extra read next entry, which is accepted:
  no special cases;
- any card remount, error recovery or `afatfs` re-init.

**5d. No counters, no aliasing.**
- **Library:** invalidation sets the cache tag to `FS_NAME_CACHE_NONE`.
- **Resident:** invalidation sets `fs_resident_names_stale` (the approved
  1 B). Menu clears it when it **requests** a fresh HCNAMES read, not when
  the read completes, so a mutation during the read leaves it set.
- **Reuse condition:** the flag is clear **and** the Scene matches.
- There is no generation number that could wrap.

**5e. Shadow check (logging only, §0.4).**
- **What:** on every reuse, queue one low-priority read of the same source
  for the next idle facade. Compare it line by line while streaming, with no
  buffer beyond the existing line buffer.
- **On mismatch:** emit a `NAME_MISMATCH` trace record (domain, kind, type,
  row).
- **Pass criterion:** every hardware test passes only with **zero
  mismatches**.
- **Scheduling:** the read is classified neutral, follows item 3's page
  rules, and compiles out with logging off.

**Scope limit:** the rule guarantees RAM names match what the firmware last
read or wrote. A card edited on a computer is seen after a power cycle; the
card is not hot-swappable and nothing remounts. State this in
`FILESYSTEM_SPEC.md`.

**Standing rule:** propose a binding MEMORY.md rule, "names in RAM are
trusted only through the classified-op validity rule; every new facade op
must be classified".

### Item 6 — Reuse names already in RAM (within the one cache)

**6a. Browser cache: same type, and return to the same page (§0.4).**
- Add `filesystem_instrumentNameCacheLoaded(type)`: the kind is INSTRUMENT,
  the type matches, the cache was published by a DONE load, and it has not
  been invalidated by item 5.
- `menu_requestInstrumentIndexLoad()` takes the Kit-style Branch B with it.
- Audit the Scene/Bank/Pattern request helpers for the same unconditional
  clear.
- Stop clearing the cache on Load/Save exit (`menu_switchPage()` ≈18814);
  trust it only through its tag.
- **Limit:** the cache holds one index. **A → B → A costs one card read per
  switch** (U7). Same-type voice changes and returning to the same page cost
  none.

**6b. Seven-name scratch across exit.**
- Keep `menu_residentNameScratchValid` and its Scene on exit.
- On entry, reuse the scratch iff its Scene matches the source Scene and
  `fs_resident_names_stale` is clear; otherwise read `.hcnames` as today.
- A non-empty `menu_residentNameDirtySceneMask` at exit still runs the
  deferred flush. That op is non-neutral, so the next entry re-reads.

**6c. Boot seeds the active Scene's seven resident names (U8).**
- **Read last:** as the last pre-audio storage step (after the boot reader,
  its Case 2/3 reloads and any HCNAMES provenance refresh), do one HCNAMES
  read of the active Scene with boot-only synchronous polling.
- **Seed through the normal copy:** pass it through the same copy as
  `menu_residentNameScratchLoaded()`, so the identity rows, scratch Scene,
  scratch valid and the stale flag (cleared at request) are set.
- **Do not reuse the boot reader's buffer:** it is the shared cache, and
  later boot steps may rewrite rows.
- **Init order:** run after `menu_init()` (which clears identity rows), or
  make `menu_init()` leave a seed alone. Update `MODULE_INTERCHANGE_SPEC.md`
  "Boot / Init Order".
- **No index is loaded at boot (§0.3).**
- **Result:** the first Load/Save entry after boot skips the 2.7 s
  `.hcnames` read. It still pays one index read for the page it opens on.

### Expected result (after items 2, 3, 5, 6)

| User action | SD work after the plan |
|---|---|
| First entry after boot | one index read (no `.hcnames`, no `.hctmp`) |
| Voice change, same Instrument type | none |
| Voice change, different type | one index read |
| `kit` restore | none |
| Load/Save type change (Kit ↔ Scene ↔ Instrument …) | one index read |
| Exit and return to the same page, nothing changed | none |
| After any name-changing operation | one fresh read (item 5) |
| Any read | no `.hctmp` in front of it; no trace/settings write blocking it |

## 6. Order of work

1. **Item 4b/4c + baseline:** current tree, PLAY running, same Scene, CPU
   widget noted. Record three each of: first entry after boot, voice change,
   `kit` restore and type change, plus one exit/return. Copy the traces to
   `build/`. The decoder must reproduce §1.1 on `SD_CARD_TEST`.
2. **Item 5:** validity rule, build check and shadow check, run with zero
   mismatches on today's reads.
3. **Item 2:** V1 byte-identical, then remove the file path.
4. **Item 6a.**
5. **Items 6b and 6c.**
6. **Item 3.**
7. **Item 1:** keep only with a measured gain.
8. **Item 4a.**
9. **Docs (§9), then the logging toggle check (§6.2).**

Re-measure after every step and record it in the handoff.

### 6.1 Verification

**Build:** `make all && make img`; no new warnings; `link_budget.py`
before/after. The RAM delta must equal §0.2.

**Name correctness (U6).** For each action below:
1. enter Load/Save so the names are warm;
2. do the action;
3. exit and return, and also switch type and voice.

The names must equal a fresh card read, with zero shadow mismatches.

| # | Action |
|---|---|
| N1 | Kit Save as a new name |
| N2 | Kit Save overwriting a slot |
| N3 | Instrument Save (new typed row) |
| N4 | Scene / Bank / Pattern Save |
| N5 | Kit / Instrument / Scene / Bank loads |
| N6 | MIDI program-change load |
| N7 | copy/clear paste Kit / Instrument / Scene; clear a slot |
| N8 | delete a slot |
| N9 | `kit` restore, then exit |
| N10 | AutoSave write, Pattern drain, settings change (reuse must **survive**: neutral) |
| N11 | Load:[Samples] |
| N12 | power-cycle after editing names on a computer |

**Hardware (logging ON, PLAY running unless noted):**

| # | Test | Pass |
|---|---|---|
| H1 | Instrument entry, each type | no `.hctmp` activity |
| H2 | Preview rows, `000 → kit` | entry voice restored exactly |
| H3 | Morph entry, preview, `kit` | only Morphable Morph cells restored |
| H4 | `kit` before any preview | no-op |
| H5 | Fast scroll in every phase | mandate order; number never lags |
| H6 | Instrument Save | unchanged |
| H7 | Voice change, two Drum voices | no SD access |
| H8 | Drum → Snare → Drum | one index read per type change |
| H9 | Instrument Save adding a row, then browse | fresh read; new name shown |
| H10 | Boot → PLAY → Load/Save | no `.hcnames` read; one index read |
| H11 | Exit, return, nothing changed | no SD access |
| H12 | Exit, MIDI PC Kit load, return | fresh `.hcnames`; correct |
| H13 | Exit with dirty names (deferred flush), return | fresh read after flush |
| H14 | Change active Scene, enter | fresh read for the new Scene |
| H15 | Copy/clear during and after Load/Save | loan works; no stale cache |
| H16 | Long browse | no trace flush on the page; flush after exit |
| H17 | Change a Global, browse at once | name request first; setting saved ≤ 5 s later |
| H18 | Everything above, listening | no glitch; underruns ≤ baseline |
| H19 | Boot with an oversized trace | rotated; boot time recorded |
| H20 | Scene/Bank/Pattern Load (audio-suspended path) | unchanged; fast drain still owns it |
| H21 | Stopped transport: H1, H7, H10, H11 | no regression vs historical medians |

### 6.2 Logging toggle check

Builds L1 → L0 (`DEV_MODE_LOGGING 0`) → L1′; `DEV_LOGGING_IWDG` and
`DEV_MODE_DIAGNOSTIC` stay 0.

| Item | Logging ON | Logging OFF |
|---|---|---|
| 1 idle pump | active | identical |
| 2 snapshot | active | identical |
| 3a trace page gate | active | compiled out |
| 3b settings/MIDI-learn gate | active | identical |
| 4a rotation | active | compiled out; trace files untouched; none created |
| 4c counters | present | compiled out (`nm`) |
| 5 validity rule + build check | active | identical |
| 5e shadow check | active | compiled out |
| 6a–6c reuse and boot seed | active | identical |

**Checks:**
- **L0:** no unused-function warnings that L1 lacks (§1.3 lists the seven
  pre-existing ones).
- **L0 image:** `nm` shows no trace rings, cursors, flush ticks, rotation or
  shadow code.
- **L1′:** matches L1 in size and symbols.
- **L0 hardware:** repeat H1, H7, H10, H11 and H18 by stopwatch.

## 7. Optional — not approved, decide after measurement

Each of these needs a user decision. 7b needs RAM sign-off.
- **7a. Larger SD burst:** `SDCARD_BURST_SIZE` 16 → 32/64 B on Load/Save
  pages. This lengthens the step before a render can start, at the CPU
  ceiling, so it needs a measured underrun check.
- **7b. Larger FAT cache:** `AFATFS_NUM_CACHE_SECTORS` 8 → 12/16 (+2/+4 KB).
  Directory sectors would stay resident between operations.
- **7c. Remembered index positions:** store each index file's directory
  position so reopening skips the directory scan. Needs a new asyncfatfs
  entry point.

## 8. Open questions

**Q1. Target time.** With no CPU headroom the remaining cost is one index
read per type change (≈2.4–3 s while playing on this card).
*Recommendation:* set the acceptance target after items 2, 5 and 6 are
measured. If one read is still too slow, decide on §7.

**Q2. Idle pump scope (item 1).**
*Recommendation:* Load/Save pages plus the deferred HCNAMES flush only; not
every background writer.

**Q3. Rotation thresholds and names (item 4a).**
*Recommendation:* 256 KiB / 64 KiB, `asavetr1.bin` / `pattrac1.bin`.

**Q4. Coarse invalidation (item 5).** Any non-neutral op drops all reuse.
*Recommendation:* keep it coarse for P1, because it is easy to prove.
Per-type invalidation is a later refinement with its own shadow-check run.

**Q5. Trace ring overflow** during long page sessions (item 3a).
*Recommendation:* separate P2. Mirror S082 P2 so a whole-Instrument mark is
one `I` record with no per-byte `D`. `D` is 114,129 of 176,790 records (65%)
on this card.

**Q6. `menu_storageBusy` during cold reads.** A cold read still holds the
input gate.
*Recommendation:* measure after item 6; redesign the gate only if cold reads
remain noticeable.

**Q7. Underrun risk (item 1, 7a).** Load/Save writes include card busy-wait
polls.
*Recommendation:* H18 includes an Instrument Save and a Kit Save while
playing. If any underrun appears, lower `BG_LOAD_IDLE_FS_BUDGET_US` for the
Load/Save case first.

## 9. Documents to update (during implementation)

- **`FILESYSTEM_SPEC.md`:**
  - Instrument entry without `.hctmp`, and the RAM `kit` restore (≈145, 228,
    307, 425, 912, 2066, 2780);
  - the item 5 rule, its op table and scope limit;
  - cache and scratch reuse, and the boot seed;
  - stale `.hctmp` files are safe to delete.
- **`STORAGE_SRAM_MANIFEST.md`:** the §0.2 rows.
- **`MODULE_INTERCHANGE_SPEC.md`:** boot order (seed, rotation).
- **`DEV_MODES.md`, `COPYCLEAR_UTILITIES.md` §15, `AutosaveTrace.h` and the
  decoder** (changed together):
  - retired phases 6/7 and the snapshot record;
  - the page-suppression flag and shadow-check records;
  - the counters record and rotation.
- **`config.h`:** `BG_LOAD_IDLE_FS_BUDGET_US` users; rotation constants.
- **`MEMORY.md`:**
  - replace the S088 status line;
  - add the item 5 binding rule;
  - add "the snapshot owns `kit` restore";
  - add to Failed Approaches: "caching every library index in RAM / loading
    indexes at boot (S090, rejected: 1,000 entries per type, open-ended
    Instrument types, one browser cache)".
- **Session log:** `knowledge_files/log_archive/090_SESSION_HANDOFF_LOG.md`.
