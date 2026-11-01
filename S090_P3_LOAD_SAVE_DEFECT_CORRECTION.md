# S090 P3 — Load/Save Defect Correction: Report

**Evidence:** `SD_CARD_TEST_3/` (card image after the S090 P1 + P2 test),
decoded with `tools/diagnostic_tools/decode_devlogs.py`.
**Baseline:** working tree of 2026-10-31 on `dev-ph7-voice_expansion` (HEAD
`ad4da190` + the uncommitted S090 P2 implementation). Line numbers are for
that tree.
**Status:** implemented 2026-10-31 (§8, §9); awaiting hardware re-test (§7, §9.4).

---

## 0. Summary

| # | Symptom | Cause | Correction | Confidence |
|---|---|---|---|---|
| D1 | Save:Scene names were slow to appear; they appeared at once when the slot number was turned | The Save page paints `preset_currentName`, but the index completion fills it only for Kit Load. Only a number turn copies the slot name. | The index completion copies the current slot's name on the Save page for Scene/Bank/Pattern, as a number turn does | Confirmed in code |
| D2 | After a type change, clicks, cursor moves and number turns are refused until the list loads | The P1 encoder gate admits only edit-mode type turns during an index read | Admit clicks and cursor moves between the type and number rows during an index read; admit number turns except where a turn loads (Kit/KitMrp Load, nested Instrument Load) | Confirmed in code |
| D3 | Scene Save stayed on `...` forever | **Root cause (SD_CARD_TEST_4, §9):** the card's root directory held 922 trailing deleted entries and no end marker, so every lookup that misses early read all 64 sectors at 16 bytes per main-loop pass. The HCNAMES update after a Scene Save took 8,296 polls instead of 1,187, so the save showed `...` for tens of seconds and was powered off mid-update. It was slow, not hung. | Boot-time directory tail trim for the root, /Scene, /Kit, /Bank, /Pattern, /Instrument and every /Instrument subdirectory (§9). C3-1/C3-2 stay as a safety net. | Measured (host replay with realistic SD cost) |

## 1. Evidence from SD_CARD_TEST_3

**One session, not two boots.**
- The trace tick is 16-bit. The jump from 65,446 to 4,911 at record #176977
  is a wrap: the `H` budget reports keep their 5 s cadence across it.
- The session had one boot, with one settle stage: `s` START (scalar, repair)
  at 2,988 ms, END after 247 ms, outcome ok. There was no AutoSave record
  after boot (P2 behaves as specified).

**The trace stops at about 100.4 s** (wrap + 34,931 ms). After that there is
no record at all, not even the 5 s `H` report. The trace flush runs only when
the facade is idle, so this matches a facade that became busy and never
released. The records for the Save request and everything after it were
still in RAM and were lost.

**The card at the moment of power-off:**

| Object | State | Meaning |
|---|---|---|
| `/Scene/019 FluxMax/` | `Empty.pat`, `Kit Flux/`, `effects.fx`, `sceneset.scg` (483 B), all complete | The Scene payload was written |
| `/Scene/.hcindex` | unchanged (row 019 empty) | The Scene index rebuild never ran |
| `/.hcnames` | **missing** | The live register was removed |
| `/.hcnamtmp` | 2,068 B: `#types` header + 161 rows | The temp register was complete |

Scene Save runs payload → `FS_INTERNAL_OP_UPDATE_HCNAMES_SCENE` → final
flush → Scene `.hcindex` rebuild. A complete temp with no live file means the
publication passed phase 11 (sync temp, then remove live) and never completed
phase 13 (rename temp to live).

## 2. D1: Save-page names do not appear when the index loads

### 2.1 Cause

- **The Save page paints a buffer, not the cache.**
  `menu_repaintLoadSavePage()` (`Core/Menu/menu.c` 13468–13489) paints
  `preset_currentName` on the Save page. The Load page reads the cache
  directly (`filesystem_sceneSlotName()` etc.).
- **Only a number turn fills that buffer for Scene/Bank/Pattern.**
  `menu_requestCurrentLoadSaveSelection()` (7382) copies the slot name
  (7576–7582) only when the type's index is already loaded. When it is not,
  it calls `menu_requestLibraryIndexLoad()` and returns (7459–7473).
- **The completion does not fill it.** `menu_libraryIndexLoadComplete()`
  (8805) copies the slot name only for Kit/KitMrp **Load** (8878–8888), then
  repaints (8902). On Save:Scene, Save:Bank and Save:Pattern, that repaint
  shows whatever `preset_currentName` already held: the previous type's or
  slot's name, or blanks.
- **Why a turn fixed it at once.** A number turn re-runs 7576–7582 with the
  cache now loaded, so the name appeared.
- **What makes entry itself slow** (expected, no change): on Save:Scene,
  entry also writes rows owed for other Scenes first
  (`menu_flushForeignNameRows()`, 9011, A3), then reads the Scene HCNAMES row
  (from the mirror when it is VALID), then the index.

### 2.2 Correction

**C1-1, `menu_libraryIndexLoadComplete()`:** MODIFY, before
`menu_storageBusy = 0u;` (8889). On `index_ok`, Save page, not nested, copy
the current slot's name into `preset_currentName`:
- Scene: `filesystem_sceneSlotName(slot)`;
- Bank: `filesystem_bankSlotName(slot)`;
- Pattern: `filesystem_patternSlotName(slot)`;
- `slot = menu_currentPresetNr[what]`.

This is the same copy the number path makes (7576–7582), so the Save page
then shows exactly what a turn would show. Kit/KitMrp Save is not touched: its
field is seeded from the resident Kit row by design (8712–8775).

**C1-2, same function:** in the same accepted branch, clear
`menu_deferSelectionRequest` for Scene/Bank/Pattern.
- **Why:** a turn admitted during the read (D2) arms that retry. The
  completion of a current read satisfies it.
- **Otherwise:** the retry re-runs the selection after the repaint. For Bank,
  that would post a second child preview.

**Result:** the name appears in the same repaint that releases input. No
extra read, no new display element.

## 3. D2: Input locked during an index read after a type change

### 3.1 Cause

The gate in `menu_parseEncoder()` (16337–16404) refuses every encoder event
while `menu_storageBusy` or Preset is busy, except:
- **`allow_load_number_scroll` (16339–16348):** an edit-mode number turn on
  the **Load** page for Kit/KitMrp or nested Instrument Load. This exists so
  that load-on-scroll can run ahead of a payload load.
- **`allow_type_step` (16376–16387, S090 P1):** an edit-mode type turn while
  a Menu index read runs (`filesystem_browseReadRunning()`).

So after a type step, a click (leaving type edit), a cursor move to the
number row and a number turn are all refused until the read completes
(2.4–3 s while playing). On Kit/KitMrp Load and nested Instrument Load the
opposite also holds: `allow_load_number_scroll` does not check for an index
read, so the number can be changed before any name has loaded.

### 3.2 Required behaviour (user)

| Row type | During an index read |
|---|---|
| Every type except load-on-scroll | click, move to the slot number, change it |
| Kit/KitMrp **Load**, nested Instrument Load (same type: pool/kit source; also a type change: `chg`) | click and move to the slot number; **no** number change until the names load |

### 3.3 Correction

**C2-1, `menu_parseEncoder()`:** ADD, after `allow_type_step` (16387),
`allow_browse_nav`. All of the `allow_type_step` context guards apply:
- Load/Save page;
- `filesystem_browseReadRunning()`;
- Preset idle;
- no Load/Save command;
- no Instrument apply;
- copy/clear does not own the encoder;
- not nested Instrument Save.

It admits exactly:

| Event | Admitted when |
|---|---|
| Click (`button != lastEncoderButton`) | state is `SAVE_STATE_EDIT_TYPE` or `SAVE_STATE_EDIT_PRESET_NR`, `what < SAVE_TYPE_GLO`, and not nested `chg` on the number row (that click commits a type change, 15677–15683) |
| Cursor turn (edit mode off) | `EDIT_TYPE` with `inc > 0`, or `EDIT_PRESET_NR` with `inc < 0` (stays between the two rows) |
| Number turn (edit mode on, `EDIT_PRESET_NR`) | top level and `what` ∈ {Scene, Bank, Pattern}, either page. **As built, Save:Kit/KitMrp turns wait** (§8.2 deviation 1) |

Still refused during the read:
- an OK/execute click (15920);
- name rows and name editing;
- the File/Dir/Settings/Samples rows (they have no index read; their wait is
  only the short cancel tail of the previous type's read);
- pots (P1 decision, unchanged).

**C2-2, `allow_load_number_scroll` (16339):** MODIFY, add
`!filesystem_browseReadRunning()`. Kit/KitMrp Load and nested Instrument Load
number turns then wait for the names. Turns during a **payload** load stay
admitted, so load-on-scroll is unchanged once names exist.

**C2-3, encoder tail (16513):** MODIFY. When `menu_storageBusy` is set and the
event was admitted by `allow_browse_nav`, call `menu_repaintAll()` before
returning. Today the tail returns without painting while busy, so an admitted
click or move would not be seen. This is the same treatment the P1 type step
gets (`menu_paintTypeStepWhileBusy()`, 15556).

**C2-4, number turn during a read:** the turn is not re-read.
- **What happens now:** the number branch (16211–16218) bumps
  `menu_selectionGeneration` and re-requests the selection. The request is
  refused while busy and leaves one deferred retry. When the running read
  completes, the generation no longer matches (`!coordinate_current`, 8837),
  so the completion **clears the cache and reads the whole index again**:
  another 2.4–3 s for every number turn.
- **MODIFY the stale branch:** when `index_ok`, the type is Scene, Bank or
  Pattern (top level), and the cache's domain tag equals the current type's
  domain (`filesystem_libraryNameCacheLoaded()`), treat the read as current.
  The index of a numbered root is the same for every slot, so only the slot
  pointer changed.
- **Kit/KitMrp keep the existing stale handling** (deferred Kit load on the
  newest slot). C2-2 means a number turn can no longer reach it during a read.
- **Type steps are unaffected:** a step cancels the read, and a cancelled
  read has no tag (A2 / `filesystem_finishCancelledBrowseRead()`).

**C2-5, display while a read runs on the Save page** (needs your decision, Q1):
- **The problem:** on the Load page, an unloaded type shows blank names
  (blank means busy). On the Save page, the name field is
  `preset_currentName`, so after a type step or an admitted number turn it
  keeps the previous type's or slot's name until C1-1 fills it.
- **The proposal:** blank it on the Save page for Scene/Bank/Pattern when the
  shown type's index is not loaded. That means extending
  `menu_paintTypeStepWhileBusy()` (15556–15566) and the C2-3 repaint to
  include those types. The Save page then behaves like the Load page. This is
  the only display change in this report; nothing new is drawn.

## 4. D3: Scene Save frozen on `...`

### 4.1 Where it stopped

`filesystem_residentNames_tick()` (`Core/Hardware/SD/filesystem.c` 7157),
publication tail:

| Phase | Lines | Action | Card shows |
|---|---|---|---|
| 10 | 7434 | wait temp close | temp complete ✔ |
| 11 | 7447 | `afatfs_sync()`, then `afatfs_removeObjects_lfn(/.hcnames)` | live removed ✔ |
| 12 | 7460 | wait remove callback, then `afatfs_renameObject_lfn(.hcnamtmp → .hcnames)`. **A refused call returns and retries on every tick with no limit.** | rename not done ✘ |
| 13 | 7480 | wait rename callback. **Waits with no limit.** | — |
| 14 | 7490 | publish and enter final flush | not reached |

- **Phase 14 and later are excluded.**
  - The final flush has a stall detector (4505–4519, 50,000 polls,
    `DEV_STALL_DETECTION` = 1 in `config.h` 111). A hang there would have
    ended in an error overlay, not a permanent `...`.
  - AsyncFATFS flushes unlocked dirty cache sectors on every poll
    (`afatfs_flush()` from `afatfs_poll()`). So a completed rename would have
    reached the card, just as the remove did.
- **So the operation sat in phase 12 or 13.**

### 4.2 Why this is a permanent freeze

- **The one unbounded state machine.** `filesystem_residentNames_tick()` is
  the only HCNAMES writer with no stall bound (recorded in P2 §1 F3).
  - The drain's convergence tail (phases 74–76) is covered by the drain stall
    detector.
  - The final flush, settings, MIDI Learn and Bank preview have their own.
- **It holds everything behind it.** While it waits:
  - the facade stays BUSY;
  - the preset Save never completes, so Menu keeps `menu_loadSaveCommandActive`
    and shows `...`;
  - the trace flush, which needs an idle facade, never runs, so the card
    holds no record of the cause.

### 4.3 Investigation (2026-10-31): what is proven and what is left

**The main loop kept running (user).** After the freeze, buttons responded
and modes changed; only the LCD stayed on the Save page with `...`. Menu
defers page switches while a Load/Save command is active, so the save command
never completed: the facade stayed BUSY in phases 12–13 while everything else
ran.

**The card's root directory (raw dump, `sudo dump_rootdir.py`):**
- One cluster (cluster 2), 1,024 entries: 102 live, 922 deleted, **no
  end-of-directory marker**.
- `.hcnamtmp`: LFN at sector 6 entry 0, SFN at entry 1. That is sector-local,
  so the rename qualifies for the in-place rewrite.
- `/.hcnames` was in the same sector; macOS has since put `.fseventsd` in the
  freed entries 4–5.

**Host replay of the unmodified AsyncFATFS** (`asyncfatfs.c` +
`fat_standard.c` against a sparse FAT32 image rebuilt from the dump;
scratchpad `afs/`):

| Run | Result |
|---|---|
| Rename on the card's current state | completes (1,268 polls), in place |
| Full tail from the pre-save state: create temp, write 2,068 B, close, sync, remove, rename, sync | completes. The temp lands at sector 6 entries 0–1, exactly as on the card, which validates the rebuild. |
| Session mix: HCNAMES read, Kit/Scene index reads, 4 cancels at phase 5, 4 at phase 7, 3× Scene-Save-shaped sequence (`mkdir`, `chdir(handle)`, `fclose`, file creates, nested Kit folder, `chdirParent`, Pattern CRC seek) each followed by the publish | completes. After every step: no cache slot locked or retained, no open handle, state READY. |
| One failed SD read or one failed write injected at every point of the publish (162 runs, 91 actually hit the fault) | all complete. AsyncFATFS retries and recovers. |

**Ruled out:**
- **AsyncFATFS logic** on this layout, including scanning a directory with no
  end marker.
- **Cache-slot or handle leaks** on every path this session used that could be
  replayed.
- **Other AsyncFATFS callers:** there are none outside the facade.
- **The boot-log deadline:** ended before audio starts.
- **A dead state in the SD driver:** every wait times out back to idle.
- **All S090 P1/P2 code:** none of it is on this path.

**What is left (runtime-only, not reproducible offline):**
1. **The card stopped completing commands persistently.**
   - When the card rejects a read command, `sdcard_readBlock()` returns false.
     It has no timeout and no re-init, and AsyncFATFS treats a refusal as busy,
     so it retries forever.
   - The rename then waits on a root sector it can never read. Nothing it
     changes reaches the card, which matches the card.
2. **AsyncFATFS state reached only through a path the replay could not
   build:**
   - boot loaders;
   - a Bank preview over real child folders;
   - Kit payload loads;
   - FATAL through an `afatfs_assert()`.

   For example, a cache slot left held would leave allocation returning
   IN_PROGRESS forever. Note that `afatfs_chdir()` drops `currentDirectory`'s
   cache holds without releasing them (`afatfs_initFileHandle()` / `memcpy`),
   but no replayed path enters `chdir` while holding one.

Telling 1 from 2 needs the AsyncFATFS and SD state at the moment of the
stall. C3-2 records exactly that.

### 4.4 Correction

**C3-1, stall bound in `filesystem_residentNames_tick()`:** ADD at the top,
under `#if DEV_STALL_DETECTION`, the shared detector
`filesystem_pollPhaseStall(op_phase, …, 50000u)`, the same threshold as the
final flush.
- **On stall:**
  - `autosaveTrace_record(PHASE_STALL, site 11, phase | gate bits)`;
  - `filesystem_makeNamedErrorCode("HNst", op_phase)`;
  - `filesystem_finish(FS_STATUS_ERROR)`.
- **Consequences:**
  - the Save completes with an error, and Menu shows its existing error
    overlay;
  - the facade is released;
  - the trace flushes, with the existing `E` OPERATION_ERROR record
    (`current_op=UPDATE_HCNAMES_SCENE`, `op_phase`).
- **RAM: 0 B.** The detector reuses the final flush's observer statics
  `op_flush_stall_last_phase` / `op_flush_stall_ticks` (filesystem.c
  4213–4214). The two operations never run at the same time. The pair is
  re-armed at the HCNAMES operation's start, as the flush re-arms it
  (4285–4286).
- **Healthy work is not cut.** The detector is edge-triggered per phase, and
  fast pre-audio polls are uncounted (`filesystem_tickUncounted()`).

**C3-2, stall snapshot (decides §4.3 cause 1 vs 2):** ADD a read-only
AsyncFATFS query, and on a C3-1 stall write one extra trace record
(`PHASE_STALL`, site 11, second record) holding:
- **AsyncFATFS:** filesystem state (READY or FATAL), `lastError`;
- **rename:** active, internal phase;
- **remove:** active, internal phase;
- **`currentDirectory`:** busy, retain index, lock index;
- **cache:** count of slots locked / retained / dirty / reading / writing;
- **SD transport:** `sdcard_getTransportSnapshot()` (exists today): state,
  operation, block, wait_ms, plus a count of consecutive refused commands.

Reading the result:

| Snapshot shows | Cause |
|---|---|
| Refused-command count climbing, transport idle | cause 1, card |
| Held cache slots, or FATAL | cause 2, AsyncFATFS state |

**RAM:**
- the query and the record: 0 B (stack only);
- the refused-command counter: 2 B in `sdcard_lxr02.c` (**approved, user
  2026-10-31**).
  Without it, cause 1 is inferred only from the transport being idle while the
  rename waits on a read.

The decoder (`decode_devlogs.py`) gets the site 11 name and the layout
(`AutosaveTrace.h`: `AUTOSAVE_TRACE_PHASE_STALL_SITE_HCNAMES 11u`; the site
field is 4 bits wide and 0–10 are used).

**C3-3, root-cause fix:** decided from the first C3-2 snapshot.
- **If cause 1:** the SD driver needs a bounded refusal path plus a card
  re-init.
- **If cause 2:** the specific AsyncFATFS hold or assert.

Neither is proposed blind: the replay rules out every path that could be
built offline.

**State after a C3-1 abort in phase 12 or 13:**
- `/.hcnames` is missing and the complete temp exists.
- The next boot promotes the temp (existing boot prelude: `#types` header +
  161 rows).
- Before that reboot:
  - load-mode reads of `/.hcnames` (Kit/Instrument/Scene entry) fail with the
    overlay;
  - an update-mode write probes, finds the register absent, and bootstraps it
    (phase 7, 7504).
- The Scene payload is already durable. Only its `.hcindex` row and the
  register publication are missing; the next Scene Save or boot repairs them.
  See Q2.

## 5. Risks

| Risk | Handling |
|---|---|
| A click admitted during a read that would start storage work | Clicks are limited to the type/number rows; OK, `chg` commit and the Settings-type rows stay refused (C2-1) |
| A number turn during a read posts a request | It is refused non-destructively (`menu_requestLibraryIndexLoad()` / `menu_requestSceneEntryName()` busy guards) and leaves one deferred retry, which C1-2 clears when the read lands |
| The Save:Kit name buffer is overwritten | C1-1 excludes Kit/KitMrp |
| A stale result accepted as current (C2-4) | Accepted only on DONE, a matching domain tag and Scene/Bank/Pattern; a type step always cancels and clears the tag first |
| C3-1 aborts healthy slow work | 50,000 polls in one phase (the same as the final flush); a healthy rename takes a few polls |
| Audio | No change to any audio-path or budget code; all changes are Menu gating and one bounded check per HCNAMES tick |
| AutoSave | Untouched (no drain, format or schedule change) |

## 6. Open questions (decisions needed)

**Q1. Save-page blanking while a read runs (C2-5).** **Accepted (user, 2026-10-31).**
- **Question:** blank the Save:Scene/Bank/Pattern name while its index loads,
  as the Load page does?
- **Recommendation: yes.** Without it, the field shows the previous type's
  name for 2–3 s, which reads as a real name. With it, blank means busy on
  both pages.

**Q2. After a C3-1 abort with the live register gone.** **Accepted (user, 2026-10-31).**
- **Question:** accept "until reboot, menu entry reads fail; the next boot
  promotes the temp"?
- **Recommendation: accept for this step.** C3-1's purpose is to end the
  freeze and capture the mechanism. A runtime promotion would add a new
  recovery path before the cause is known; decide it with C3-3.

**Q3. Scope of the number-turn admission (C2-1).** **Accepted (user, 2026-10-31).**
- **Question:** limit it to the index-backed rows (Scene, Bank, Pattern;
  Save:Kit/KitMrp), or include File/Dir/Settings/Samples?
- **Recommendation: the index-backed rows only.** The others have no index
  read, so nothing is locked except the short cancel tail. Settings'
  `preset_loadName()` arms no retry when refused (7450–7452), so admitting
  it would risk a name that never loads.

## 7. Verification (after implementation)

| # | Test | Pass |
|---|---|---|
| V1 | Save page, step to Scene, wait | number shows at once, name blank (Q1) then filled when the read lands, without touching the encoder |
| V2 | V1 for Bank and Pattern | same; Bank child LEDs after the name |
| V3 | Step to Scene, at once click out, move to the number, turn it 3 detents | every event is painted at once; one index read only (no re-read in the trace); name of the final slot appears when the read lands |
| V4 | Load page, step to Kit, at once click out, move to the number, turn | click and move admitted; turn refused until the names show; afterwards load-on-scroll as before |
| V5 | Nested Instrument Load, step type, at once try the same | as V4; `chg` row: click on the number row refused until the read ends |
| V6 | During any read, try OK and name rows | refused (unchanged) |
| V7 | Save Scene to a new slot ×5 while playing | each completes; `.hcnames` present, no `.hcnamtmp`, `.hcindex` row present |
| V8 | If V7 ever freezes | it ends at the stall bound with the error overlay; the trace holds four `X` site-11 records (parts 0–3: phase and gates, cache slots, SD state, SD block) and `E` `UPDATE_HCNAMES_SCENE` phase N. Those records decide C3-3 (§4.4 table) |
| V9 | Listening throughout | no underrun |

## 8. Implementation record (2026-10-31)

### 8.1 Changes

| Item | File | Where | What |
|---|---|---|---|
| Helpers | `Core/Menu/menu.c` | 2035 (prototypes), 8832–8925 | `menu_loadSaveSlotIndexKind()` (Scene/Bank/Pattern → index kind), `menu_libraryIndexResident()` (tag set **and** no index read running), `menu_loadSaveSlotIndexReady()`, `menu_blankLoadSaveNameWhileUnloaded()` |
| C2-4 | `menu.c` | `menu_libraryIndexLoadComplete()` (8928) | `coordinate_current` also true for a DONE read whose tag matches the shown top-level Scene/Bank/Pattern type |
| C1-1, C1-2 | `menu.c` | 9031 | Save page: copy the current slot's name into `preset_currentName`; clear `menu_deferSelectionRequest` |
| C2-1 (selection) | `menu.c` | `menu_requestCurrentLoadSaveSelection()` (7462–7487) | Scene/Bank/Pattern use their rows only when the index is resident; otherwise the existing non-destructive request/defer path runs |
| C2-5 | `menu.c` | `menu_paintTypeStepWhileBusy()` (15747) | also blanks the Save page Scene/Bank/Pattern name while unloaded |
| C2-2 | `menu.c` | `menu_parseEncoder()`, `allow_load_number_scroll` | `!filesystem_browseReadRunning()` added |
| C2-1 (gate) | `menu.c` | `menu_parseEncoder()`, 16621 | `allow_browse_nav` (click / cursor move on type and number rows; number turn on Scene/Bank/Pattern); a click event drops a simultaneous turn |
| C2-3 | `menu.c` | encoder tail, 16805 | busy repaint (with blanking) for admitted browse events |
| C2-6 (added) | `Core/Hardware/SD/filesystem.c` | `filesystem_kitSlotName()` / `sceneSlotName()` / `bankSlotName()` / `patternSlotName()` (34717, 34757, 34797, 34827) | blank while a Menu index read runs (§8.2 deviation 2) |
| C3-1 | `filesystem.c` | `filesystem_residentNames_tick()` 7288; re-arm helper 4231; re-arm calls in `filesystem_start()` and the four HCNAMES chain sites (Pattern, Scene ×2, Instrument) | 50,000-poll bound per phase, sharing the Flush Finish observer statics (0 B); on stall: snapshot, release `op_file`, error `HNst`, `FS_STATUS_ERROR` |
| C3-2 | `filesystem.c` | `filesystem_residentNamesRecordStall()` 7199 | four `X` site-11 records (layout in `AutosaveTrace.h`) |
| C3-2 | `Core/Hardware/SD/asyncfatfs/asyncfatfs.c/.h` | `afatfs_getStallSnapshot()` 3250; `afatfsStallSnapshot_t` | read-only state, gate and cache-slot snapshot |
| C3-2 | `Core/Hardware/SD/asyncfatfs/sdcard_lxr02.c/.h` | `refused_run` 147; updates in `sdcard_readBlock()` / `sdcard_writeBlock()`; `refused_run` field in `sdcardTransportSnapshot_t` | consecutive refused READ/WRITE commands (2 B, approved) |
| C3-2 | `Core/Bank/Scene/AutosaveTrace.h` | 397–417 | `AUTOSAVE_TRACE_PHASE_STALL_SITE_HCNAMES 11u`, part field (flags bits 5..6), layout comment |
| C3-2 | `tools/diagnostic_tools/decode_devlogs.py` | site tables; `decode_hcnames_stall()` 244; `X` branch | site 11 name and behaviour; decodes parts 0–3 |

### 8.2 Deviations from §2–§4

1. **Save:Kit/KitMrp number turns wait for the read** (C2-1 table listed them
   as admitted).
   - **Why:** on the Save page their selection path runs the Kit name session
     (`menu_requestKitEntryNames()`) or `preset_loadName()`. During a read,
     the Kit name session sees the tag that the read set at its start, takes
     its "index loaded" branch and clears `menu_storageBusy` while the read
     is still running. That would open every control, OK included, against a
     busy facade.
   - **Effect:** clicks and moves on those rows are still admitted. This
     matches the user's wording, "except kit and same-instrument".
2. **C2-6 added: library slot names are blank while a Menu index read runs.**
   - **Why:** `filesystem_requestReloadLibraryIndex()` tags the cache when
     the read starts, so the readers returned `Empty` for rows not yet read.
     With P3 admitting repaints during a read, the Load page would show
     `Empty` for every unread slot. Blank means busy.
   - **Also:** `menu_requestCurrentLoadSaveSelection()` now treats such a
     domain as not resident (`menu_libraryIndexResident()`). A number turn
     during a read therefore re-requests the index (refused while busy,
     deferred) instead of copying a partial row or posting a Bank preview.

### 8.3 Build (verified)

- **`make all && make img`:** exit 0. No new warnings: the same pre-existing
  set; P3 code compiles clean.
- **Sizes:**
  - logging on: text 686,904, data 420, bss 413,020 (unchanged; the 2 B
    counter landed in padding);
  - payload 687,324 B (flash headroom 66,340 B);
  - `build/LXRV2_lxr02.img` 687,340 B.
- **Logging and stall detection off** (scratch copy): builds. Its warnings are
  all pre-existing and none is in P3 code. text 668,856, bss 396,116.
- **`decode_devlogs.py`:** compiles; site-11 parts 0–3 decoded correctly on
  synthetic values.
- **Not run:** hardware (§7). The host AsyncFATFS replay (`afs/` in the
  session scratchpad) covers only §4.3.

## 9. D3 root cause and fix (SD_CARD_TEST_4)

### 9.1 Evidence

- **Same failure, same steps.** TEST_4 repeated TEST_3:
  - `Scene/033 FluxMax/` written completely;
  - `.hcnamtmp` complete (`FluxMax 033 R`);
  - `/.hcnames` removed, rename not written;
  - the trace stopped while the facade was busy.
- **No site-11 `X` record.** The C3-1 bound is 50,000 polls. At the poll rate
  measured from this trace's own Instrument index reads (Drum 1.565 s,
  Snare 0.735 s, Cymbal 0.856 s, about 400–500 polls/s while playing), that
  is roughly 2 minutes. The §4.4 claim that a healthy operation "takes a few
  thousand polls" was right, but those polls take seconds, not milliseconds,
  while playing.
- **Host replay with realistic SD cost** (16-byte bursts, about 34 polls per
  sector), on the card's root directory as dumped:

| Root directory | HCNAMES tail (temp, sync, remove, rename, sync) | Scene Save + HCNAMES (approx.) |
|---|---|---|
| As on the card: 1,024 entries, 102 live, 922 deleted, no end marker | 8,296 polls, 197 sector reads | ~10,500 polls (≈ 25 s or more while playing) |
| Same, end marker after the last live entry | 1,187 polls, 15 sector reads | ~3,400 polls (≈ 7 s) |

- **The card state is a snapshot mid-update.** Both cards show the update
  partway through its slow scans (remove's full rescan, then the rename's
  full collision scan), where the power was cut.
- **§4.3 is superseded.** It concluded "runtime state, not reproducible";
  the replay ran with a 3-poll sector cost and so never showed the time
  cost.

### 9.2 Cause

- **Where the tail came from.** An older AsyncFATFS allocator placed every
  new name at the end marker. Each HCNAMES publication (temp create, then
  rename) moved the marker forward by two entries and left a deleted pair
  behind. Eventually the marker ran off the end of the single root cluster.
- **Why it hurts.** Without a marker, every lookup that does not find its
  target early reads the whole directory: creates, absence probes, remove
  rescans, rename collision scans.
- **Why it doesn't fix itself.** The current allocator reuses deleted holes
  first (`afatfs_noteDeletedDirectoryEntry()`,
  `afatfs_selectDirectoryRunAtTerminator()`), so it no longer grows a tail.
  But nothing restores a lost marker, so an affected card stays slow for
  ever.

### 9.3 Fix (implemented)

| File | Change |
|---|---|
| `Core/Hardware/SD/asyncfatfs/asyncfatfs.h/.c` | `afatfsTrimTail_t` (caller-owned) + `afatfs_trimDirectoryTailBegin()` / `afatfs_trimDirectoryTailContinue()`: scan `currentDirectory` to its marker or allocated end; if deleted entries follow the last live entry, zero the first of them (end marker). Only free entries change; no FAT change; one sector write when needed. |
| `Core/Hardware/SD/filesystem.c/.h` | `filesystem_trimDirectoryTailsBlocking()`: root, `/Scene`, `/Kit`, `/Bank`, `/Pattern`, `/Instrument`, then **every subdirectory found in `/Instrument`** (enumerated, so future instrument types are covered with no code change; bound 64). Blocking boot pump, deadline armed as `DIRTRIM `, final `afatfs_sync()`. Helpers: `filesystem_trimCurrentDirectoryBlocking()`, `filesystem_trimEnterRootChild()`, `filesystem_trimInstrumentTreeBlocking()`, `filesystem_trimRecord()`. |
| `main.c` | called right after the post-mount settle, before settings/Bank/HCNAMES reads; deadline expiry takes the existing timeout path. No display change. |
| `Core/Bank/Scene/AutosaveTrace.h`, `decode_devlogs.py` | trace stage `'d'`: one record per repaired or failed directory (id, flags, live entries, deleted entries cut). |

- **Not visited:** slot folders (Scene/Bank/Kit slots and their Kit
  subfolders). They are removed and recreated whole on overwrite, so they
  never gain a tail.
- **Within a session:** a tail can grow only when a new name fits no
  deleted hole; it is cut at the next boot.
- **RAM:** 0 B (state on the stack).
- **Build:** text 688,936, data 420, bss 413,020 (unchanged); payload
  689,356 B; image 689,372 B; no new warnings.
- **Host verification** (unmodified AsyncFATFS):
  - **the card's root:** trimmed (920 deleted entries cut, marker after
    entry 104); a second boot writes nothing; 20 HCNAMES read+publish cycles
    afterwards cost 1,486 polls each and leave the marker in place; all 49
    objects intact;
  - **`/Instrument` and four type folders with tails:** all trimmed, the
    stray file skipped; a second pass writes nothing; no handle or cache
    slot left held.

### 9.4 Re-test

| # | Test | Pass |
|---|---|---|
| T1 | First boot on the TEST_4 card | boot completes; trace has `d` records for the SD root (≈920 entries cut) and any other repaired directory |
| T2 | Second boot | no `d` records |
| T3 | Scene Save while playing (V7) | completes in seconds; `/.hcnames` present, no `.hcnamtmp`; `.hcindex` row present |
| T4 | Load/Save entry, list loads | noticeably faster than before (root lookups no longer scan 64 sectors) |

