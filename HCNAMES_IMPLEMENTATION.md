# HCNAMES Implementation Log

## Purpose

Track the `.hcnames` work separately from the 8-character sanitizer plan.
`.hcnames` is the resident-name register: it records object names that are
currently held in memory after load, not directory-browser cache rows and not a
replacement for the 9,000-byte shared scan/sort cache.

## Contract

- `/.hcnames` lives at the SD-card root for the first implementation pass.
- The file is generated after the initial boot load/fallback chain has finished
  and before audio starts.
- Boot writes the complete register; runtime Instrument and Kit Load/Save
  workflows consume and update only the resident-name rows they own.
- Blank rows mean the object was not loaded or only contains the initialized
  `none` placeholder.
- Row order is fixed:
  1. Bank name
  2. 16 resident Scene names
  3. 16 resident embedded Kit names, one per Scene
  4. 16 groups of 6 resident Instrument display names

## Implemented

- Added `filesystem_writeResidentNamesBlocking()` in
  `Core/Hardware/SD/filesystem.c`.
- Declared the writer in `Core/Hardware/SD/filesystem.h` with a detailed public
  contract comment.
- Added boot call in `main.c` after the initial Bank/Scene/Kit load/fallback
  loop and before globals load.
- Added blocking root file overwrite helper for boot-only register writes.
- `make -j2`, `make img`, and `git diff --check` passed after the first writer
  implementation.
- Hardware reported a boot hang after the first writer image. The suspected
  cause was opening dot-prefixed `.hcnames` through legacy SFN
  `afatfs_fopen()`, which can reject that component and leave the blocking open
  wrapper polling forever. The writer now uses `afatfs_fopen_lfn()` for boot
  register overwrites so `.hcnames` is preserved as an LFN display component
  with a generated short alias.
- Hardware still hung after the LFN-open fix, so the private blocking writer
  was removed. `/.hcnames` is now written by `FS_INTERNAL_OP_WRITE_HCNAMES`,
  a normal foreground-pumped filesystem operation that mirrors `.hcindex`:
  return to root, open the file, stream rows from `op_line_buf`, close, and
  finish through the standard filesystem flush gate. The boot wrapper only
  starts this operation and pumps `filesystem_tick()`.
- Hardware continued to freeze, so no further behavioral fix was guessed. A
  temporary read-only screen hook now reports `HPhs` and `HRow` during the
  existing blocking writer. Phase values are 0 root/open, 1 open wait, 2 SRAM
  row write, 3 close wait, 4 flush, 5 done, and 6 error. Updates are throttled
  to 100 ms and queued only when the complete two-line LCD frame fits; the hook
  does not alter filesystem state. The frozen display supplies the exact phase
  and next row required for the single targeted correction.
- The HCNAMES hook never appeared on hardware, proving boot did not reach the
  HCNAMES wrapper. A second temporary, read-only milestone hook now flushes a
  `Boot` stage to the OLED immediately before each existing blocking step:
  1 mount, 2 Kit scan, 3 Kit repair/index, 4 Scene scan, 5 Scene repair/index,
  6 Bank scan, 7 Bank repair/index, 8 Instrument repair/index, 9 Bank index
  load, 10 initial Bank/Scene/Kit request, 11 initial payload completion,
  12 HCNAMES, 13 globals, and 14 audio handoff. Row 2 displays the pre-step
  filesystem status. The explicit LCD drain guarantees the last stage is
  visible even when the following call never returns; it does not alter SD
  state or filesystem sequencing.
- Hardware stopped at `Boot +11 / FS +1`, proving the initial payload request
  remained busy. Stage 11 now also flushes `FOp`/`FPhs` before every distinct
  operation transition: `FOp` 1 is name repair, 2 Bank Load, 3 Scene Load,
  4 Kit Load, 5 final flush, and 0 any unexpected operation; `FPhs` is that
  operation's current private phase. Repeated coordinates do not repaint. The
  last visible pair therefore identifies the exact state entered before a
  filesystem pump failed to return or stopped advancing.
- Hardware then reported `FOp +1 / FPhs +42`. This proves the busy operation is
  Bank-load name repair at its final transition, before the Bank payload reader
  starts. Phase 42 currently combines returning to root with entry into the
  synchronous embedded-Kit quarantine helper, so this coordinate alone does
  not yet distinguish a non-advancing root change from a blocking quarantine
  substep. No behavioral correction has been made from this result.
- Phase 42 was split only for observation, retaining the original order and
  actions: phase 42 now returns to root, phase 43 runs the existing synchronous
  embedded-Kit quarantine helper, and phase 44 hands the successful repair to
  the existing Bank payload reader. The stage-11 `FOp`/`FPhs` hook will now
  leave the exact final substep visible without changing validation policy or
  load behavior.
- Hardware then stopped at `FOp +1 / FPhs +43`, proving the synchronous
  embedded-Kit quarantine helper was the non-advancing substep. A tri-state
  caller-result change was tested as a possible rescan-loop correction.
- Hardware remained at `FOp +1 / FPhs +43` after the tri-state correction.
  This disproves the caller-rescan loop as the active freeze on this card: the
  synchronous quarantine helper is not returning far enough to publish any of
  its three results. The next diagnostic must identify its internal blocking
  open/chdir/read/close/rename substep; no additional behavioral fix has been
  inferred from the unchanged phase. The disproven tri-state change was
  reverted so it does not remain as unrelated behavior.
- A temporary phase-43 `FSub` observer now flushes a marker immediately before
  each blocking component call. Codes: 1..5 enter/open/scan the selected Bank;
  code 6 releases the no-longer-needed Bank-root handle; 10..15 scan a
  Bank-local Scene and select its embedded Kit; 20..24 open, enter, release the
  explicit handle, validate, and return from a Kit; 30..32 open/read/close `kitset.kcg`;
  40..45 open Instrument members 0..5; 50..55 close those members; 60..65
  return/close/rename an invalid Kit and owning Scene; 71..73 close or return
  after the Scene result; and 80/82 close the selected Bank and return to root.
  The callback is registered only around the initial payload operation and is
  cleared afterward. It changes no filesystem state or call ordering.
- Hardware reported `FPhs +43 / FSub +20`. This identifies handle exhaustion,
  not an LFN lookup failure: asyncfatfs provides exactly three open-file
  handles, while the helper retained Bank root, selected Bank, and Scene before
  requesting a fourth handle for the embedded Kit. The single correction is to
  close Bank root immediately after the selected Bank handle opens. The current
  directory already copied Bank-root state and the selected Bank handle owns
  the child independently, so Bank root has no later role. The deepest descent
  now fits exactly three handles: selected Bank, Scene, and Kit. No handle-pool
  growth, name-policy change, or unrelated filesystem behavior was added.
- The same fixed pool requires releasing the explicit Kit handle immediately
  after entering it: currentDirectory already contains the copied Kit state,
  while retaining the pool handle would leave no slot for `kitset.kcg` or an
  Instrument member file. The handle is now closed before validation. The
  deepest payload-open state therefore uses selected Bank + Scene + payload
  file, again exactly three handles, with the Kit represented by
  currentDirectory rather than a redundant fourth pool owner.
- Hardware confirmation: boot now completes with the targeted Bank-root and
  Kit-handle lifetime correction. The diagnostic hooks remain available for
  future hardware investigation, but their OLED callbacks, stage displays, and
  deliberate LCD drains are now compiled only when `CONFIG_DEV_MODE` is
  nonzero. With the production value `0`, the same repair/load/HCNAMES sequence
  runs without altering the splash screen and the resident-name writer receives
  a NULL observer callback.
- asyncfatfs application-handle capacity is now five. This is additional
  concurrency headroom rather than a path-depth requirement: `currentDirectory`
  remains outside the pool and explicit directory handles should still be
  closed after `afatfs_chdir()`. The compiled `afatfsFile_t` size is 328 bytes,
  so the former three-to-five expansion adds exactly 656 zero-initialized SRAM1
  bytes. It permits retained reader/writer handles at different directory
  levels while leaving three slots for transient asynchronous work; it does not
  revert the confirmed lifetime fix or change FAT/on-card formats.
- Runtime Instrument menu entry now reads the selected resident Scene/voice
  name from root `/.hcnames` before loading the selected type's `.hcindex`.
  The 129 HCNAMES rows temporarily borrow `fs_list_cache_name[1000][9]`; Menu
  copies the selected eight cells into its existing
  `menu_instrumentSaveName[9]` buffer, then the typed index replaces the same
  cache. Load's `kit` source display and Save's editable seed no longer read
  `scene->kit.instrument_display_name` directly.
- Successful normal or Morph Instrument Load and normal or Morph Instrument
  Save now pass through a targeted HCNAMES update before storage input is
  released. Because the file contains trimmed variable-length lines, the
  updater cannot grow one row safely in place: it reads all 129 rows into the
  existing generalized cache, replaces only the affected Scene/voice row(s),
  and streams the register back through the normal close/flush gate. Unrelated
  rows are preserved from the file rather than regenerated from resident SRAM.
  Normal multi-Scene Load updates each destination row changed by that action;
  Save and Morph Load target one row. Morph operations preserve the existing
  name but still complete through the same durable row-update boundary.
- This runtime reader/updater introduces no new persistent SRAM allocation.
  It reuses the generalized name cache, `op_line_buf`, existing filesystem
  counters/masks, and Menu's existing nine-byte name buffer. In the production
  linked image its addition did not increase `.data` or `.bss`.
- Top-level Kit Load/Save now uses the same serialized cache-borrow workflow as
  nested Instrument Load/Save. On Kit menu entry—and again immediately before
  Save name editing if the source Scene changed—firmware reads `/.hcnames`,
  copies the selected resident Kit row into the existing
  `preset_currentName[8]` editor, and then reloads `/Kit/.hcindex` into the same
  generalized cache. Menu no longer calls `scene_kitDisplayName()` to seed the
  Kit Save name.
- A successful normal full Kit Load now captures Preset's immutable request
  Scene mask, rewrites exactly seven HCNAMES rows per committed destination
  (one Kit row plus all six Instrument rows), reloads `/Kit/.hcindex`, and only
  then starts the existing bounded runtime sound apply. Scene, voice, type, and
  click controls remain locked across that chain, but plain encoder turns on
  the Load number field now update a Menu-owned desired slot immediately. Only
  the newest number is retained; its name stays blank and is posted after the
  older immutable action drains. KitMrp Load remains identity-preserving, so it
  reads the unchanged resident Kit name from HCNAMES after endpoint apply rather
  than rewriting the seven name rows.
- A successful normal or Morph-projected Kit Save already completes its
  physical `/Kit/NNN Name` write, physical `/Kit/` rescan, and full
  `/Kit/.hcindex` rewrite before Preset publishes completion. Menu now follows
  that durable boundary with a seven-row HCNAMES update for the request-time
  source Scene, then reloads the already-written Kit index because HCNAMES had
  borrowed the shared cache. Normal Save publishes the edited resident Kit
  identity; Morph Save preserves resident identity while reaffirming the same
  seven rows. The request-time source reuses Preset's existing single-Scene
  coordinate, and the Kit Load mask is exposed read-only from its existing
  field, so no new persistent operation state was added.
- The full-Kit extension adds no static SRAM. The linked production sections
  after implementation are `.dma_nocache` 3,100 bytes, `.data` 412 bytes,
  `.bss` 271,720 bytes, `.dtcm` 35,168 bytes, and `.dtcmz` 6,716 bytes. SRAM1
  static use is 275,232 bytes. The responsive-scroll correction introduced no
  static data symbol, counter, name buffer, or cache; it reuses the existing
  deferred-selection flag and Menu number/name fields. The four-byte `.data`
  section movement is LTO/alignment layout, not a new symbol.
- Static-SRAM reconciliation against the Session 042 manifest baseline:
  `.dma_nocache + .data + .bss` changed from 276,440 bytes to 275,232 bytes, a
  net reduction of 1,208 bytes. The session removed the linked 2,004-byte
  KitBrowser bridge and added 656 bytes for two asyncfatfs handles. Beyond the
  handle pool, the linked name-repair operation scratch introduced 130 named
  bytes and the phase-43 diagnostic callback pointer introduced 4 bytes; the
  remaining section-level difference is 6 bytes of small-symbol/alignment
  layout. The runtime Instrument and full-Kit HCNAMES menu/update work itself
  introduced zero new static data symbols.
- Hardware feedback found that Kit Save could leave its targeted Kit row blank.
  The updater had reused the boot serializer's Bank-present-mask gate even
  though a successful Save is itself proof that its resident source Kit exists.
  The seven explicitly targeted rows now use the completed Kit action as their
  presence predicate. The Kit and six Instrument names are formatted directly
  from that source Scene, while every unrelated HCNAMES row is still preserved
  from the file.
- Kit and nested Instrument Load number scrolling no longer waits for an older
  payload, HCNAMES rewrite/read, runtime apply, and `.hcindex` restoration.
  During those phases, number-only encoder turns update the existing desired
  slot/index and set the existing defer flag; they do not call Preset or mutate
  its request coordinates. The LCD name is cleared immediately. When the old
  action finishes, its real resident rows are still serialized, the shared
  index is restored, and only the newest desired number is loaded. At this
  revision the completed HCNAMES row supplied the visible name; the later
  index-first preview ordering documented below supersedes that display timing
  while retaining the same post-commit HCNAMES update.
- Hardware subsequently reported a renewed boot hang. Neither the targeted Kit
  Save row formatter nor the number-only encoder path executes during the boot
  payload sequence, so no speculative filesystem correction has been made.
  `CONFIG_DEV_MODE` is temporarily enabled for the next diagnostic image. This
  restores the already-existing read-only `Boot/FS`, `FOp/FPhs`, `FPhs/FSub`,
  and `HPhs/HRow` OLED observers; the last displayed coordinate will determine
  whether the card is stalled in scanning/indexing, initial payload loading,
  HCNAMES writing, or globals loading before one behavioral change is selected.
- The dev-mode image then completed boot without freezing. No boot filesystem
  change was inferred from a non-reproducing stall, and `CONFIG_DEV_MODE` is
  back at zero for the production image.
- The next physical Kit Save provided a narrower HCNAMES failure signature.
  Source Scene 6's six Instrument rows (physical lines 70..75) were correctly
  replaced with the saved Emott member names, proving that the captured Scene
  coordinate, seven-row update request, and file rewrite all completed. Only
  its Kit row (physical line 24) remained blank, while `/Kit/.hcindex` already
  contained the successfully saved `069 Emott` identity. Normal Kit Save
  completion now reaffirms that source Scene's resident Kit name from the
  durable Kit index row immediately before HCNAMES borrows the shared cache.
  KitMrp Save does not rename resident identity and therefore skips this step.
- Load preview ordering now favors the library index without weakening the
  resident register. Kit/KitMrp loads copy and paint the selected
  `/Kit/.hcindex` row before posting payload I/O. Instrument Load retains its
  HCNAMES-derived direct `kit` source row, but when the encoder chooses a typed
  pool number it copies and paints that type's `.hcindex` name before starting
  the Instrument payload. Successful loads still update the correct HCNAMES
  rows afterward. If an older transaction temporarily owns the shared cache,
  a newer freely-scrolled number stays blank only until index restoration;
  restoration paints the newest index row before its deferred payload starts.
  The ordering change and Kit-name reaffirmation add no buffer or static state.
- The per-action resident-name transaction is now superseded by one combined
  Kit/Instrument menu session. On entry, a single root `/.hcnames` read copies
  one Scene's complete seven-row block—Kit plus all six Instruments—into
  `menu_residentNameScratch[7][9]` before `/Kit/.hcindex` or a typed Instrument
  `.hcindex` takes back the generalized cache. Switching Kit/Instrument views,
  selecting voices, scrolling numbers, and completing normal payload actions
  reuse those rows; none of those actions reopens or rewrites HCNAMES.
- Successful normal Kit Load/Save refreshes all seven scratch rows for the
  displayed affected Scene and accumulates the complete immutable Scene mask.
  Successful normal Instrument Load/Save refreshes only its displayed voice
  row and accumulates the affected Scene mask. Morph variants preserve resident
  identity and do not mark HCNAMES dirty. The scratch is display/edit state;
  exit serialization reads authoritative names from each dirty Scene's already
  committed SceneData, so several changed Scenes need no 16-by-7 name cache.
- Leaving the combined Kit/Instrument family performs at most one HCNAMES
  update. A clean session is discarded with no card I/O; a dirty session reads
  the variable-length register once, replaces the full seven-row block for
  every accumulated Scene, preserves every unrelated file row, then closes and
  flushes once. Changing the selected resident Scene is an explicit old-session
  exit/new-session entry boundary so the old dirty mask is durable before the
  next seven rows are read.
- Kit and Instrument Load scrolling now keeps the applicable `.hcindex` cache
  resident through ordinary payload and runtime-apply work. Number turns copy
  and paint the newly selected index row immediately while the older immutable
  action drains; only the short HCNAMES family-entry/family-exit cache window
  can show a temporary blank. This removes the former per-action HCNAMES and
  `.hcindex` restoration latency from the name-refresh path.
- SRAM note for this revision: the old initialized 9-byte standalone
  `menu_instrumentSaveName` was replaced by 63 bytes of seven-row scratch plus
  one scratch-Scene byte, one validity byte, and a two-byte dirty-Scene mask.
  The named net increase is therefore 58 bytes. The clean production link is
  `.dma_nocache` 3,100, `.data` 404, and `.bss` 271,800 bytes, for 275,304
  bytes of SRAM1 static use. Against the preceding production image's 275,232
  bytes, the exact linked increase is 72 bytes: 58 named bytes plus 14 bytes of
  LTO/alignment layout. DTCM sections remain `.dtcm` 35,168 and `.dtcmz` 6,716
  bytes. This is volatile Menu session state; no new on-card or other
  non-volatile format/storage field was introduced.

### 2026-07-22 — Scene rows and mask-selective Bank transactions

- `scene_t.display_name[9]` and its setter/accessor were removed. Scene
  identity is now owned only by HCNAMES rows 1..16. This releases 144 bytes
  from the sixteen resident Scene records. Menu adds one nine-byte
  operation-scoped Scene scratch, populated from the selected HCNAMES row on
  top-level Scene Load/Save entry (and when the Scene Save source changes),
  before `/Scene/.hcindex` takes back the generalized cache. No 16-Scene Menu
  array or new filesystem cache was introduced.
- Successful root Scene Load/Save now transfers directly into the existing
  HCNAMES read/preserve/rewrite state machine. It changes only the selected
  destination/source Scene row(s), then performs the existing `/Scene/` scan
  and `.hcindex` rewrite before the original callback completes. A missing
  register is bootstrapped as blank rows plus only the successfully changed
  rows, so the first real Scene operation can create it without a SceneData
  name mirror.
- Bank Load remains mask-selective. After its preflight repair has consumed
  the root Bank browser row, it borrows the general 1,000-row cache for the
  complete HCNAMES image. Each successfully committed selected child overlays
  exactly its Scene, Kit, and six Instrument rows; unselected Scene blocks are
  never changed. The Bank row is then updated, the complete preserved/selected
  register is written once, and `/Bank/.hcindex` is rebuilt to restore the
  shared cache. A missing register starts blank and is created from selected
  children only.
- The selected-child intersection has no fallback-to-all behavior. If the
  requested mask contains no child present in the selected Bank, no Scene
  payload or HCNAMES block is changed. The current resident Scene-present mask
  is preserved in that case; a non-empty request merges only successfully
  loaded child bits into it. This keeps availability consistent with the
  preserved unmasked data and names.
- Bank Save similarly reads HCNAMES once before creating the temporary Bank
  tree. Bank-local Scene and embedded Kit directory display names come from
  the cache; Instrument member filenames still use their resident 16-byte
  source stems because HCNAMES intentionally stores only eight display cells.
  After promotion only row zero changes, the register is written once, and the
  normal Bank rescan/index rewrite restores the cache.
- The boot-time `filesystem_writeResidentNamesBlocking()` snapshot call was
  removed from `main.c`. It could no longer regenerate Scene rows after their
  SRAM mirror was retired and would erase unselected names after a selective
  Bank boot load. Runtime Scene/Bank updates are now the only publishers of
  Scene identity; the old blocking helper remains available for diagnostic or
  explicit bootstrap work but is not in the normal boot path.

## Not Implemented Yet

- Versioning or keyed schema. The first pass intentionally uses fixed row order.
