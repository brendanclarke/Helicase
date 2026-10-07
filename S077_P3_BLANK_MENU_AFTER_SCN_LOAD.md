# S077 P3 — Blank VOICE menu and locked controls after a Load

**Status (2026-10-07):** root cause confirmed on hardware; fix direction
agreed with the user. The code implementation is not written yet.
`S077_P3_IMPLEMENTATION.md`, which blocks mode presses, describes a rejected
approach and is superseded by this document (§9).

---

## 1. Symptoms

Both symptoms appear for about 3 seconds after you leave the Load/Save page
following a load. They have a single cause (§3).

**A. Blank VOICE page (reported).**

Original repro (user):

1. Save a Scene (076 FluxScnR) with two LFO resets set to `scn`.
2. Play from the 11th Scene. Load Scene 076 into the 12th slot via the
   LOAD page SEQ buttons (select 12, deselect 11).
3. Press PERF straight away.
4. In PERF, press SEQ 12 to switch to the loaded Scene.
5. Press MODE VOICE.
6. Press SELECT 6 (LFO).
7. **Result:** the LCD is completely blank.

Minimal repro, confirmed on hardware after the analysis predicted it:

1. Load a Scene into the **active, playing** Scene.
2. The load completes. Press PERF; the PERF page is correct.
3. Press MODE VOICE. **The LCD stays on PERF.**
4. Press SELECT. **The LCD goes blank.**

Does not reproduce: a PERF Scene switch, then VOICE and SELECT, with no
load before it.

The LFO `scn` setting, the Scene switch in step 4 and the non-active target
slot are all incidental. The only requirement is a MODE press during the
window described in §3.

**B. Encoder lock (confirmed on hardware).** After a load into the active
Scene, the encoder does nothing on the PERF page for about 3 s. By code
inspection (not yet hardware-tested), the pots, the runtime widgets and the
copy/clear gesture are also dead during that window (§3.5).

---

## 2. Why the blank page looks the way it does

The blank page is a **mode/page desync**: `bh_state.selectButtonMode` is
`SELECT_MODE_VOICE`, but `menu_activePage` is still `PERFORMANCE_PAGE`.

- `handleModeButtons()` writes `selectButtonMode` unconditionally
  (`buttonHandler.c:1156`/`1158`, and `1142` on the SHIFT+VOICE path). It
  then calls `menu_switchPage(menu_getActiveVoice())`
  (`buttonHandler.c:1179`).
- `menu_switchPage()` returns at its busy gate (`menu.c:13039`,
  `menu_storageBusy || preset_getStatus() != PRESET_IDLE`). Only an exit
  from LOAD/SAVE is queued (`menu.c:13052–13057`). From PERF the request is
  silently dropped. The VOICE LED lights, but the page does not change.
- The next SELECT is handled as a VOICE SELECT
  (`case SELECT_MODE_VOICE: menu_switchSubPage(selectNr)`). Because
  `menu_isVoicePage(PERFORMANCE_PAGE)` is false, `menu_switchSubPage()`
  takes the generic branch and sets `menuIndex` to that PERF sub-page.
- `menu_resolveCellAbsolute()` (`menu.c:3146`) reads
  `menuPages[PERFORMANCE_PAGE][n]`. PERF has content only on sub-page 0
  (`menuPages.h`; rows 1–7 are `TEXT_EMPTY`/`PAR_NONE`), so SELECT 2–8
  show a blank screen. PERF has no custom renderer.
- The desync lasts until the next mode press. Pressing a track button in
  VOICE mode also clears it (`menu_switchPage(voiceNr)`,
  `buttonHandler.c:1542`).

Ruled out as causes:

- **The PERF Scene switch.** `preset_startDrumsetApply()`
  (`presetManager.c:1855`) arms only its worker flags. It never sets
  `pm_status` or `menu_storageBusy`. The same is true of
  `menu_perfModeSceneButtonPressed()`, `seq_selectActivePattern()` and
  `menu_setShownPattern()`.
- **VOICE cell resolution during a pending Scene apply.** VOICE cells come
  from the active Scene's **saved** slot type via a plain registry lookup
  (`instrumentManager_voicePageDescriptorIndex()`). They do not depend on
  the live runtime instrument.

---

## 3. Root cause

**The background `.hcnames` rewrite that runs after you leave Load/Save
raises `menu_storageBusy`, the flag meaning "the Menu is waiting on a
foreground storage operation".** The whole Menu is then gated for the ~3 s
of an SD write that no page outside Load/Save depends on.

### 3.1 A completed load marks resident names dirty

Each of these completions ORs its destination Scenes into
`menu_residentNameDirtySceneMask`. None of them does card I/O at that
point.

| Completed action | Site |
|---|---|
| Scene Load (`PRESET_OP_SCENE_LOAD`) | `menu.c:12348` → `menu_refreshResidentNameScratchKit()` |
| Kit Load | `menu.c:12292` |
| Kit Save | `menu.c:12817` |
| Instrument Load (normal, root pool) | `menu.c:5838` (via `menu_requestAppliedInstrumentNameUpdate()`) |
| Instrument load-site bookkeeping | `menu.c:12618`, `12776` |
| PERF clear-menu `reload scene` | the same `PRESET_OP_SCENE_LOAD` completion |

The new names are already live in RAM by then. The filesystem completion
has written the Kit name and six Instrument names into the in-RAM name
block. The dirty mask only records that `/.hcnames` on the card still
needs those rows.

KitMrp and InstrumentMrp keep the existing names and mark nothing.

### 3.2 Leaving Load/Save defers the write

`menu_switchPage()` away from LOAD/SAVE sets `end_resident_name_session`
(`menu.c:13116`). The block at `menu.c:13319` then discards the session's
name scratch but keeps the dirty mask, so that "the destination page must
become visible on this pass". The new page paints straight away.

### 3.3 The background trigger takes the Menu busy flag

1. `filesystem_tick()` (`filesystem.c:25620–25623`) sees an idle facade, no
   copy/clear suspension, and a nonzero mask. It calls
   `menu_triggerDeferredHcnamesFlush()` (`menu.c:5695`), which runs only
   when the page is not LOAD/SAVE.
2. That function reuses `menu_endResidentNameScratchSession()`
   (`menu.c:5644`). This helper was written for name-session boundaries
   **inside** the Load page, where the next browser view needs the write's
   result. It sets **`menu_storageBusy = 1`** (`menu.c:5668`) and posts
   `filesystem_requestUpdateResidentKitNames()` with
   `menu_residentNameScratchFlushComplete()` as its callback.
3. The callback (`menu.c:5582`) runs when the write finishes. It
   acknowledges the facade, clears the mask, clears the browser name cache
   and sets `menu_storageBusy = 0`. If the page is LOAD/SAVE, it also
   requests the browser's entry names.

### 3.4 The write takes about 3 seconds

The rewrite reads all 161 rows, overlays the dirty Scenes' Kit and
Instrument rows, writes `.hcnamtmp`, renames it and syncs. The same writer
in `SD_CARD_S077_P2_OUTPUT/asavetrc.bin` (`N` records phase 4 → 5,
1 ms ticks) took **2,914 / 2,936 / 3,271 / 3,313 ms** while playing. The
read alone took about 160 ms.

### 3.5 What `menu_storageBusy` blocks outside Load/Save

| Gate | Site | Effect during the write |
|---|---|---|
| `menu_switchPage()` | `menu.c:13039` | Mode page switch dropped (not queued) → **symptom A** |
| `menu_parseEncoder()` | `menu.c:11309` | Encoder ignored → **symptom B** |
| `menu_parseKnobDelta()` | `menu.c:11704` | Pot edits ignored |
| `menu_serviceKnobRepaint()` | `menu.c:11821` | Knob repaints withheld |
| `menu_serviceRuntimeWidgets()` | `menu.c:11876` | VOICE overlay and runtime widget services paused |
| `copyClear_copyPressed()` | `copyClearSession.c:556` via `menu_isStorageBusy()` | Copy/clear gesture refused |

Every page outside Load/Save is affected in the same way. A LOAD/SAVE
mode press from PERF in this window desyncs too: the mode becomes
LOAD_SAVE, but the page stays on PERF.

---

## 4. Why nothing outside Load/Save needs to wait for this write

1. **The rewrite does not use the shared browser cache.** Since the
   "Option 1C" change, HCNAMES reads and writes go through a dedicated
   1,449-byte `hcnames_name_mirror` (`filesystem.c:1060–1086`), not the
   shared `fs_list_cache_name` array:
   - `filesystem_prepareResidentNamesCache()` (`filesystem.c:5887`): "The
     shared fs_list_cache_name storage is no longer touched".
   - `filesystem_clearNameCache()` (`filesystem.c:30762`) clears only the
     browser array. Load/Save entry calling it during the write cannot
     corrupt the rewrite.
2. **It reads its new rows from the in-RAM name block, once, early.**
   `filesystem_cacheCurrentResidentKitNames()` (`filesystem.c:6280`) copies
   the in-RAM Kit and Instrument names into the mirror at phase 3 of
   `filesystem_residentNames_tick()` (`filesystem.c:6591`), about 160 ms
   into the write. After that, the file is streamed from the mirror alone.
3. **Nothing can change those inputs before phase 3.** The in-RAM names
   change only in filesystem load/save completions or in Menu's
   callbacks after a filesystem read. All of those need the filesystem,
   which is busy until the write ends. The one direct editor is the nested
   Instrument Save name field (§6.4).
4. **No new dirty bits can arrive during the write**, for the same reason.
   Clearing the whole mask on success is therefore exact, as it is today.
5. **The VOICE, PERF, STEP, FX, MENU and SOM pages read only SceneData,
   `parameter_values` and the in-RAM name block.** None of them depends on
   the card copy of `.hcnames`.

The only Menu feature that needs the card copy is the Load/Save browser.
It already copes with a busy filesystem (§6.2).

---

## 5. The fix

**Treat the post-exit rewrite as a background write. It holds the
filesystem (as AutoSave does) but never raises `menu_storageBusy`.**

### 5.1 Changes (description only; code to follow in the implementation doc)

1. **`menu_triggerDeferredHcnamesFlush()` (`menu.c:5695`)** keeps its
   current gates: nonzero mask and not on LOAD/SAVE. The filesystem must
   be idle and copy/clear not suspended, which `filesystem_tick()` already
   checks. Instead of calling `menu_endResidentNameScratchSession()`, it
   calls `filesystem_requestUpdateResidentKitNames(mask, <new callback>)`
   directly.
   - It does **not** touch `menu_storageBusy`.
   - The name scratch was already invalidated and the browser cache already
     cleared at Load exit (`menu.c:13319`), so this path does nothing else.
   - If the request is refused, the mask stays set and the next idle
     `filesystem_tick()` retries, as today.
2. **New static callback, `menu_residentNameDeferredFlushComplete()`.** It
   replaces `menu_residentNameScratchFlushComplete()` for this path only.
   - Read the result, then call `filesystem_ack()` exactly once on both
     success and failure. This is required: without it the filesystem stays
     DONE and blocks AutoSave and the trace flush, as documented in the
     existing callback.
   - **Success:** clear `menu_residentNameDirtySceneMask`.
   - **Failure:** keep the mask and show today's error overlay
     (`menu_showFilesystemErrorOverlay()`), so the failure behaviour is
     unchanged (§7).
   - It does **not** set or clear `menu_storageBusy`. It does not clear
     the browser cache, post browser requests or repaint. Load/Save entry
     has its own retry (§6.2), and the visible page is unaffected.
3. **Unchanged: the foreground boundaries inside Load/Save.** These still
   call `menu_endResidentNameScratchSession()` with `menu_storageBusy` and
   the existing callback, because the next browser view on that page needs
   the result:
   - top-row type change leaving Kit or Scene (`menu.c:11084`);
   - Scene change on Kit/Instrument Save (`menu.c:5776`, `7188` ff.);
   - nested Instrument entry, exit and type steps (`menu.c:6711–6763`,
     `7318`, `7421`, `7659`, `7734`, `7784`).

   Mode presses on the Load page are queued by `menu_pendingPageSwitch`,
   so these cannot desync.
4. **Comment corrections only, no behaviour change.** These texts still
   describe HCNAMES operations borrowing the shared browser cache:
   - `filesystem.h:903–920` and the Kit-name request block near
     `filesystem.h:946–966`;
   - `filesystem_requestUpdateResidentKitNames()` (`filesystem.c:30483`);
   - `filesystem_residentNames_tick()` (`filesystem.c:6606–6618`, "fills
     the existing generalized cache").

   Correct them to the mirror. Also correct the `menu_switchPage()` comment
   at `menu.c:13319`, which says the write is deferred so the page paints
   now. It should also say the write must not lock the Menu.

**Footprint:** one new static function, one changed function body, and
comment edits. **No new RAM** (code only, so no RAM approval is needed).
No ISR or DSP path, no file format change, and no change to the write's
content or duration.

### 5.2 What the user sees after the fix

- After any Scene/Kit/Instrument Load or Kit/Instrument Save, leaving
  Load/Save gives a fully working page at once: mode buttons, encoder,
  pots, widgets and copy/clear.
- The ~3 s card write still happens in the background. Its only visible
  effect is that other card work waits for it, as it does behind an
  AutoSave write today (§6).

---

## 6. Interactions checked

### 6.1 Kit, Instrument and Save paths

Kit Load, Instrument Load, Kit Save and Instrument Save leave Load/Save
through the same `end_resident_name_session` → background trigger path as
Scene Load. Today all of them have the same ~3 s lock and the same
blank-page risk; the fix covers them all through the single trigger.
KitMrp and InstrumentMrp mark nothing, so they never start the write.

### 6.2 Going straight back into Load/Save during the write

Today this is impossible: the LOAD/SAVE press from PERF is dropped and the
mode desyncs. After the fix the page opens immediately, and the existing
busy-filesystem paths take over:

- `menu_switchPage(LOAD_PAGE)` clears the browser cache, which is safe
  (§4.1), and requests the current selection.
- **Scene/Bank/Pattern browser:** `menu_requestLibraryIndexLoad()`
  (`menu.c:6337`) is refused because the filesystem is busy. It drops
  `menu_storageBusy` back to 0 and arms `menu_deferSelectionRequest`.
- **Kit and nested Instrument entry:** `menu_requestKitEntryNames()` and
  `menu_requestInstrumentEntryNames()` both use
  `menu_requestResidentNameScratch()` (`menu.c:5757`). That function finds
  the scratch invalid, sees the busy filesystem and arms
  `menu_deferSelectionRequest` without touching scratch or cache state.
- The retry in `menu_pollPresetStatus()` (`menu.c:12181`) runs once the
  filesystem is idle and `!menu_storageBusy`. The write's callback has run
  by then and cleared the mask, so the Kit/Instrument entry read gets the
  freshly written `/.hcnames`.
- **What the user sees:** slot and entry names appear up to ~3 s late, as
  when re-entering during an AutoSave drain. OK cannot load a selection
  until its browser list is loaded, so no load runs against an unloaded
  list.
- **Re-entering before the write has started:** the trigger does not fire
  on LOAD/SAVE pages, so the name session continues exactly as today.
- **Leaving Load/Save a second time while the write runs:**
  `end_resident_name_session` is set again because the mask is still
  nonzero. It only invalidates the already-invalid scratch and clears the
  browser cache. The running write is unaffected.

### 6.3 Copy/clear during the write

The gesture is no longer refused (§3.5). `ccSvc_tick()` already runs at a
reduced rate while the filesystem is busy (`copyClearService.c:1515`), and
the borrow of the scratch buffer waits for the filesystem to go idle
(`ccSvc_ensureScratch()`, `filesystem_borrowNameCacheScratch()`). This is
the same behaviour as starting copy/clear behind an AutoSave write.
`copyClear_backgroundSuspended()` only stops a new trigger; it does not
pre-empt a running write.

One exception: the PERF clear-menu **`reload scene`** job calls
`preset_loadSceneForScenes()`. That request is refused while the filesystem
is busy, and the job is fire-and-forget, so a reload issued during the
write is dropped silently. Today the whole gesture is refused in that
window, so the user-visible result is the same; the same drop already
happens behind an AutoSave write. Recorded under §8.

### 6.4 Nested Instrument Save name editor (theoretical only)

`menu_instrumentSaveName` edits an Instrument row of the in-RAM name block
directly (`menu.c:1426`, `10724`). It is not gated on the entry read having
completed.

For this to race the rewrite, the edit must land before the rewrite copies
those rows at phase 3 (about 160 ms after the write starts). In that time
the user would have to:

1. leave Load/Save;
2. re-enter it;
3. open nested Instrument Save;
4. move to the name field;
5. turn the encoder.

That is not reachable by hand. An edit after phase 3 does not affect the
write. No guard is proposed, but if the user wants this window closed, the
smallest option is to refuse name-character edits while
`menu_residentNameScratchValid == 0`.

### 6.5 AutoSave and trace scheduling

Unchanged. The HCNAMES rung still runs ahead of the AutoSave writer in
`filesystem_tick()` and holds the filesystem until its callback calls
`filesystem_ack()`.

---

## 7. Failure behaviour (unchanged)

On a failed write, the new callback does what the existing one does:

- it calls `filesystem_ack()`;
- it keeps the dirty mask;
- it shows the FsErr overlay.

`filesystem_tick()` retries on the next idle pass, so a card that keeps
failing will keep showing the overlay, exactly as today.
`menu_showFilesystemErrorOverlay()` itself writes `menu_storageBusy = 0`
(`menu.c:5055` ff.). On this path nothing else can hold the flag at that
moment, because every Menu request made during the write was refused and
released it. Changing the failure policy is out of scope.

---

## 8. Out of scope (same class, by code inspection, not hardware-tested)

These can also leave the button layer and the page out of step, but they
are not caused by the HCNAMES write and are not changed here:

- **PERF `reload scene` load:** `preset_loadSceneForScenes()` sets
  `pm_status = PRESET_LOAD_IN_PROGRESS` while you are on a non-Load page.
  A mode press during the file load is dropped by the same
  `menu_switchPage()` gate. After this fix, the HCNAMES write that follows
  that load no longer locks the Menu, but the load itself still does.
- **Timed overlays** that set `menu_storageBusy` on the current page: the
  stale-settings warning (`menu.c:428`) and the AutoSave boot notices
  (`menu.c:464`, `499`).
- **The underlying fragility:** `handleModeButtons()` changes mode state
  before learning whether the page switch will run. Blocking or queuing
  mode presses was considered and rejected as the fix for this bug,
  because it would keep the Menu locked for no reason. Any hardening there
  is a separate decision.
- **The ~3 s write duration** itself (a candidate for `SCOPING_TARGETS.md`).

---

## 9. Superseded material

- `S077_P3_IMPLEMENTATION.md` (the `menu_pageSwitchBlocked()` guard) is
  **rejected**. It would have dropped mode presses for ~3 s after every
  load and left the encoder and pot lock in place. A new implementation
  schedule will follow this document.
- The earlier versions of this file blamed the PERF Scene switch, then
  proposed the same guard. Both are replaced by §3–§5.

---

## 10. Verification plan (after implementation)

DEV build, card with Scenes 11 and 12 present, playing from Scene 11.

**Primary (each straight after the load completes, then leaving Load
with PERF):**

| # | Action | Expected |
|---|---|---|
| 1 | Load a Scene into the active Scene → PERF → MODE VOICE at once → SELECT 2–8 | The VOICE page opens immediately. Every SELECT shows real parameters. |
| 2 | Original repro: load into slot 12 → PERF → SEQ 12 → MODE VOICE → SELECT 6 | The LFO page is shown. |
| 3 | Load → PERF → turn the encoder and pots at once | They respond immediately; no ~3 s lock. |
| 4 | Load → PERF → copy/clear gesture at once | It is accepted; the operation completes after the background write. |
| 5 | Repeat 1 and 3 after Kit Load, Instrument Load (nested), Kit Save, Instrument Save | Same as 1 and 3. |

**Load/Save re-entry:**

| # | Action | Expected |
|---|---|---|
| 6 | Scene Load → PERF → LOAD/SAVE straight away (Scene type) | The page opens at once. Slot names fill in within ~3 s. OK works after that. |
| 7 | Kit Load → PERF → LOAD/SAVE straight away (Kit type) | The page opens. The Kit name and slot list fill in after the write. The resident Kit row shows the **newly loaded** Kit name. |
| 8 | Kit Load → PERF → LOAD/SAVE → nested Instrument Load for a voice | The Instrument entry fills in after the write, showing the new names. |
| 9 | Re-enter Load/Save within ~3 s, then leave again before the names appear | No error. The second exit is clean. |

**Persistence:**

| # | Action | Expected |
|---|---|---|
| 10 | After 1, 2, 5: wait > 5 s, power-cycle | `/.hcnames` holds the loaded Scene's Kit and six Instrument rows; boot shows them. |
| 11 | Read `/.hcnames` from the card after test 2 | Row block for Scene 12 matches 076 FluxScnR's Kit and Instrument names; all other rows unchanged. |

**Regression:**

| # | Action | Expected |
|---|---|---|
| 12 | Inside Load/Save: Kit → Scene type change after a Kit Load; Scene change on Kit Save | Unchanged: these still wait for the foreground flush on the Load page (`...` behaviour as today). |
| 13 | Plain PERF Scene switches → VOICE | Unchanged. |
| 14 | AutoSave after a load (trace `S`/`A`/`T`) | The writer is admitted after the HCNAMES write, as today; no stranded DONE status. |
