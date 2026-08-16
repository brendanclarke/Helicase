# Scene Load record-publication fix (implementation-ready)

## Implementation notes

2026-08-16 — Implementation started. The planned production change is
limited to one acknowledgement in `menu_loadCommandFinalIndexComplete()`;
the full What/Why/Inputs/Outputs/Affiliates explanation has been placed
adjacent to that call in `Core/Menu/menu.c`. No `.h` change or RAM allocation
is required.

2026-08-16 — Implemented FIX-1 in `Core/Menu/menu.c`. The terminal status is
captured into `index_ok`, then `filesystem_ack()` releases the direct final
Scene/Bank index callback from `DONE`/`ERROR` to `IDLE` before Menu teardown.
No other production code or header change was made. `git diff --check` passes;
the logging-on `make -j2` build and `make img` packaging pass. The packaged
image is `build/LXRV2_lxr02.img` (377,008 bytes including its 16-byte image
wrapper). Existing compiler/linker/LTO warnings remain unchanged. The
logging-off build was already verified before this unguarded acknowledgement
was added; this change is outside the logging conditional paths and adds no
RAM or logging dependency.

2026-08-16 — Review and completion note. FIX-1 is the only production change
owned by this document: `menu_loadCommandFinalIndexComplete()` now snapshots
the terminal result and acknowledges it with `filesystem_ack()` next to its
full documentation-in-place comment. No `.h` change, RAM allocation, or
symbol was added. The worktree build directory had been cleared after the
earlier pass, so a fresh `make clean && make -j2 && make img` was run to
verify the current source: it passes with `text=376,596`, `data=396`,
`bss=95,188`, and repackages `build/LXRV2_lxr02.img` (377,008 bytes including
the 16-byte wrapper). `git diff --check` is clean; only the pre-existing
compiler/linker/LTO warnings remain.

2026-08-16 — Hardware pass after flashing the packaged image: the user
loaded a Scene into the last Scene slot and exited to Voice mode. The
`SD_CARD/` capture confirms the acknowledgement fix works. See section 10 for
the record-level evidence and the one remaining HCNAMES gap logged for the
next session.

## Ongoing risks

1. Hardware confirmation is still outstanding. FIX-1 is source-verified but
   not field-verified. Tests A and B in section 6 must pass on the freshly
   packaged image before close-out.
2. If the next pass still shows no `R/D/I/L/S/F/W` while the user remains on
   the Load page after the busy indicator clears, the facade release was not
   the only failure and the completion chain must be revisited per the
   decision tables in `SCENE_LOAD_ASAVE_TRACE_RESTORE.md`.
3. The post-exit AutoSave drain was never observed in the earlier captures
   because of this leak. The exit leg of Test A must now show `A/V/M/C/P/T`
   plus exactly one `.hcprms` generation advance; otherwise a second
   admission failure remains and must be investigated separately.
4. Bank Load shares the corrected callback. Its behavior change is
   intentional but untested; include a Bank Load regression in the next
   hardware pass.
5. The failed-index-read path now also releases the facade (by design). The
   error overlay should be spot-checked once on hardware: `index_ok` is
   captured before the acknowledgement and `filesystem_errorCode()` is not
   cleared by `filesystem_ack()`.
6. Preserve `SD_CARD_NOEXIT/` and `SD_CARD_NOPLAY/` until the new pass is
   confirmed; they are the falsification evidence for any residual failure.
7. Temporary diagnostics remain active until their own close-out steps: the
   2048-record trace ring (`TRACE_EXTENSION.md`), the logging-only evidence
   latches (4 B), and the approved `drumset_apply_stall_ticks` (2 B). The
   `SRAM_MANIFEST.md` update for those allocations is still due.
8. The known duplicate-`asavetrc.bin` FAT-entry limitation from
   `DEV_MODES.md` still applies. If a later card shows a growing trace file
   without the expected records, preserve the card and check for a second
   root directory entry before concluding the producers failed.

## 1. Conclusion

The Scene payload load, its targeted HCNAMES update, the Preset completion,
and the Scene dirty-marker path are not the terminal blocker. The blocker is
the last operation in the already-established root Scene Load mechanism: the
read-only `/Scene/.hcindex` cache restore.

`menu_loadCommandFinalIndexComplete()` reads the terminal filesystem status
and ends the visible Load command, but it never acknowledges that terminal
status. Consequently, `filesystem_complete()` leaves the shared filesystem
facade permanently at `FS_STATUS_DONE`. The UI is unlocked and can leave the
Load page, but both background publishers are admitted only while the facade
is `FS_STATUS_IDLE`. The RAM trace cannot append to `/asavetrc.bin`, and the
canonical AutoSave dirty mask cannot start its `/.hcprms1`/`/.hcprms2`
transaction.

The single correction is one `filesystem_ack()` call in that existing final
Scene/Bank Load index callback, immediately after its status result has been
captured. This document specifies that call, the exact diff, and the
documentation-in-place comment that must accompany it.

## 2. Root cause

### 2.1 The exact active call path

The runtime root Scene Load uses this path:

```text
preset_loadSceneForScenes()
  -> filesystem_requestLoadSceneForScenes()
  -> filesystem_loadSceneDirectory_tick()
  -> targeted UPDATE_HCNAMES_SCENE handoff and durable flush
  -> on_scene_load_complete()
       -> autosave_markSceneWithoutPatternDirty()
       -> PRESET_OP_SCENE_LOAD completion
  -> menu_startSoundApply()
  -> menu_finishSoundApply()
  -> menu_requestLoadCommandFinalIndexRestore()
  -> filesystem_requestReloadLibraryIndex(FS_LIBRARY_INDEX_SCENE, ...)
  -> filesystem_complete(FS_STATUS_DONE)
       -> status = FS_STATUS_DONE
       -> menu_loadCommandFinalIndexComplete()
```

The last callback is the actual terminal step added for root Scene/Bank Load.
It calls `menu_finishLoadSaveCommand()`, which clears the Menu busy/command
flags, but it does not call `filesystem_ack()`.

Current source anchors:

- defective callback: `Core/Menu/menu.c:2965`;
- final-index request helper: `Core/Menu/menu.c:2992`;
- post-apply handoff into that request: `Core/Menu/menu.c:431`;
- `filesystem_complete()` publishes the terminal status before the callback:
  `Core/Hardware/SD/filesystem.c:3129`;
- `filesystem_ack()` converts terminal `DONE`/`ERROR` back to `IDLE`:
  `Core/Hardware/SD/filesystem.c:20020`;
- `filesystem_tick()` admits the trace flush only under `FS_STATUS_IDLE`:
  `Core/Hardware/SD/filesystem.c:19807`;
- `filesystem_tick()` admits the AutoSave writer only under `FS_STATUS_IDLE`:
  `Core/Hardware/SD/filesystem.c:19819`.

### 2.2 Why the omission strands both publishers

- `filesystem_complete()` publishes `DONE` before invoking the callback, so
  by the time Menu runs its final UI step the facade is already terminal.
- `filesystem_ack()` is the only API that converts a terminal `DONE` or
  `ERROR` back to `IDLE`; it has no effect while the facade is `IDLE` or
  `BUSY`, so it is safe to call unconditionally from a completion callback.
- Both `filesystem_autosaveTraceFlushSchedule_tick()` and
  `filesystem_autosaveWriterSchedule_tick()` are called only when
  `status == FS_STATUS_IDLE`. With the facade parked at `DONE`, the RAM trace
  ring never appends to `/asavetrc.bin`, and the canonical AutoSave dirty mask
  never starts its `/.hcprms` transaction, even after the user leaves the
  Load page.

This also explains why parameters and HCNAMES can be correct while both
record sinks remain unchanged: all payload assignment, HCNAMES durability,
dirty marking, and runtime application occur before the final read-only index
restore leaks its terminal status.

### 2.3 The Kit Load differential proves the mechanism

The NOPLAY capture contains a complete Kit Load record set (456 Scene-15
dirty bytes, six `I` summaries with `flags=0x07`, a Kit-kind `L`, and a `W`
writer-suppression witness) but none of the Scene Load's records. Kit Load
has no final index restore: `menu_requestLoadCommandFinalIndexRestore()`
returns zero unless the command type is Scene or Bank. The Kit path's Preset
completion therefore acknowledges the facade and its trace drains, while the
Scene path parks the facade at `DONE` and its records die with the RAM ring
at power-off. This is the same capture-level signature the missing
acknowledgement predicts.

### 2.4 Existing precedent in the same file

`menu_residentNameScratchFlushComplete()` already performs this exact
contract for the exit-time HCNAMES transaction: it snapshots the terminal
result and then calls `filesystem_ack()` specifically so the trace flush and
AutoSave schedulers can resume. Its in-code comment names both schedulers.
The final Scene-index callback needs the same direct-callback contract.

## 3. Field evidence the fix must explain

- Both captured `LXRV2_lxr02.img` files match the current build exactly
  (SHA-256
  `0a61f99590943eec8abe5b5dd6bc33eb3eeddc547b2654ec1aa911e7264b9b36`),
  and the matching ELF contains the leading `R` producer in
  `on_scene_load_complete()`. A stale flashed image does not explain the
  missing durable `R` record.
- `SD_CARD_NOEXIT/.hcnames` registers destination Scene 15 as
  `KitWool<TAB>016`; `SD_CARD_NOPLAY/.hcnames` later registers the same
  destination as `Chip<TAB>007`. The root Scene loader therefore reached and
  durably completed its targeted Scene-row HCNAMES transaction on the new
  build.
- `SD_CARD_NOEXIT/.hcprms1` and `SD_CARD_NOPLAY/.hcprms1` are byte-for-byte
  identical, as are their `.hcprms2` counterparts. Their generations remain
  5 and 4 respectively. No AutoSave generation was published after either
  Scene Load.
- `SD_CARD_NOPLAY/asavetrc.bin` is 4,768 bytes larger than the NOEXIT copy
  (596 new records): a boot sweep, its three-bank-byte writer transaction,
  the Kit Load burst, and a final `W flags=0x01` that proves dirty work was
  armed while the Load page held the writer. The suffix has no `R`, no
  Scene-kind `L`, and no new `A/V/M/C/P/T` transaction.

The missing on-card Scene trace does not prove the Scene callback or RAM
producer failed. Trace records are first placed in the logging-only SRAM
ring and become card records only when the trace scheduler can acquire an
idle filesystem facade. The terminal `DONE` leak blocks that acquisition, so
the Scene records remain RAM-only and disappear when the unit is powered
down for card inspection.

## 4. The exact code change

### 4.1 Change inventory

| ID | File | Function | Change | Retained RAM |
| --- | --- | --- | --- | --- |
| FIX-1 | `Core/Menu/menu.c` | `menu_loadCommandFinalIndexComplete()` (line 2965) | Add one `filesystem_ack()` call plus its documentation-in-place comment, immediately after the terminal status is captured | None |

No other file changes. No new operation, callback, scheduler, state byte,
cache, overlay, or alternative load path is added.

### 4.2 Exact diff

```diff
 static void menu_loadCommandFinalIndexComplete(void)
 {
     uint8_t index_ok = (uint8_t)(filesystem_status() == FS_STATUS_DONE);

+    /*
+     * Return the shared filesystem facade to IDLE after the terminal index read.
+     *
+     * What: the terminal Scene/Bank `.hcindex` result is captured first, then
+     * acknowledged with filesystem_ack() before any Menu state changes. The
+     * acknowledgement converts the facade from FS_STATUS_DONE (or
+     * FS_STATUS_ERROR) back to FS_STATUS_IDLE; it does not alter the captured
+     * index_ok byte, does not clear filesystem_errorCode(), and has no effect
+     * if the facade is already IDLE or BUSY.
+     * Why: filesystem_complete() publishes the terminal status before invoking
+     * this callback, and this callback deliberately bypasses Preset's
+     * acknowledgement helper. Without this call the facade stays DONE
+     * indefinitely, and filesystem_tick() admits both the AutoSave trace
+     * flush and the AutoSave writer only while status == FS_STATUS_IDLE. The
+     * result is the observed failure: every RAM-ring trace record the
+     * completed Scene Load produced (R, D, I, L, F, S, W) remains unflushed,
+     * and the armed `.hcprms` mutation drain can never start, even after the
+     * user leaves the Load page. Both sinks must resume here because this is
+     * the last operation the accepted Scene/Bank Load command performs.
+     * Inputs: filesystem_status() is still this operation's terminal result
+     * when the callback runs; index_ok snapshots it before acknowledgement.
+     * Outputs: FS_STATUS_IDLE with current_op already NONE, so the next
+     * filesystem_tick() pass can run the trace flush scheduler and, on a
+     * later idle pass, arm/admit the AutoSave writer. Menu command teardown,
+     * error overlay, and cache handling below are unchanged.
+     * Affiliates: filesystem_ack(), filesystem_complete(),
+     * filesystem_tick()'s idle-only scheduler gates, and
+     * menu_residentNameScratchFlushComplete(), which already performs this
+     * acknowledgement for the exit-time HCNAMES transaction.
+     */
+    filesystem_ack();

     /*
      * Publish the terminal result of a post-DSP browser-cache restoration.
      *
      * Inputs: the read-only Scene or Bank `.hcindex` request posted only after
      * the loaded active Scene finished its shared runtime apply. Output: end
      * `...`, release the input gate, and return to the bracketed type row. On
      * failure the payload and DSP state remain committed, but the unusable
      * cache is cleared and the existing filesystem error overlay is shown.
      *
      * This callback is intentionally separate from
      * menu_libraryIndexLoadComplete(): that entry/browse callback starts a
      * Bank child preview, whereas this callback must terminate the accepted
      * command without posting any new selection work.
      */
     if (!index_ok)
         filesystem_clearNameCache();
     menu_finishLoadSaveCommand();
     if (!index_ok)
         menu_showFilesystemErrorOverlay();
     else
         menu_repaintAll();
 }
```

### 4.3 Placement rules the diff satisfies

- The status must be sampled first because the callback still needs to
  distinguish success from failure; `filesystem_ack()` changes what a later
  `filesystem_status()` read would return.
- The acknowledgement must precede `menu_finishLoadSaveCommand()` so the
  facade is `IDLE` by the time the visible command ends and the next main
  loop pass runs `filesystem_tick()`.
- The acknowledgement is unconditional. A failed index read leaves
  `FS_STATUS_ERROR`, which strands the schedulers exactly as badly as
  `FS_STATUS_DONE`; both must be released.
- `filesystem_ack()` does not erase `filesystem_errorCode()`, so the existing
  error overlay remains valid on a failed index read.
- The call is idempotent. If a future path already left the facade `IDLE`,
  the call changes nothing and cannot acknowledge an unrelated in-flight
  operation, because this callback runs synchronously inside the completion
  of the one operation whose terminal status it consumed.

## 5. Why this one change is sufficient

### 5.1 Trace publication resumes

The trace flush is gated only by the accepted command, not by the Load page.
During the sound-apply window the facade is `IDLE` and the command is still
active, so the flush scheduler already emits the one-edge `F` witness
(command-active, pending records) into the RAM ring. After this fix the final
index completion returns the facade to `IDLE`, the command-active gate lifts
at `menu_finishLoadSaveCommand()`, and the next idle tick appends the pending
ring batch to `/asavetrc.bin` on the existing 500 ms cadence. The Scene
Load's `R`, `D`, `I`, `L`, `F`, `S`, and `W` records all become durable
while the user can still be sitting on the Load page.

### 5.2 AutoSave mutation publication resumes

The canonical dirty mask was populated by
`autosave_markSceneWithoutPatternDirty()` before the index restore ran. On
the first idle tick after the fix the writer scheduler arms the existing
five-second debounce (`S`), then documents the intentional Load/Save page
guard (`W`) on every idle pass while the user stays on the page. After the
user leaves the page and the debounce elapses, the existing writer admits and
publishes the ordinary `A/V/M/C/P/T` sequence into the next `.hcprms`
generation. No page-guard or writer-policy change is involved.

### 5.3 Scheduler ordering

`filesystem_tick()` already gives the facade to settings persistence first,
then the trace flush, then the AutoSave writer. The released facade therefore
flushes diagnostic records before a long drain can claim the one facade, so
the evidence is durable before the `.hcprms` transform runs.

### 5.4 Bank Load

`menu_requestLoadCommandFinalIndexRestore()` intentionally shares this
callback for Bank Load. The same acknowledgement makes that identical
terminal path well-formed, with no Bank-specific code touched.

## 6. Expected field results after the fix

### 6.1 Test A - Scene Load, play stopped, capture before page exit

1. Boot, enter Load:[Scene], load a known source Scene into Scene 15.
2. Wait for the busy indicator to clear, then wait a few seconds without
   leaving the Load page.
3. Copy the card and decode `/asavetrc.bin`. Expected tail, in production
   order:
   - `R`, flags=0x01 (status DONE), value=0x8000 (destination mask);
   - the Scene-15 `D` burst (Scene settings, two Kit values, and six
     76-byte Instrument images);
   - six `I` summaries with flags=0x07 and expected==accepted;
   - `L` kind=0 Scene15 (Kit) and `L` kind=1 Scene15 (Scene);
   - `S` (writer armed) with its debounce deadline;
   - `F` with bit 0 set (flush suppressed while the command was active) and
     the pending count in value;
   - `W` with bit 0 set (writer armed-with-dirty, suppressed by the Load
     page) and the deadline in value;
   - no `G` (dropped count 0); no `A` yet (page suppression is by design).
4. `/.hcnames` Scene row 16 already shows the loaded name; Kit row 32 and
   Instrument rows 123-128 remain old until session exit.

### 6.2 Test A continuation - after leaving the Load page

1. Leave the Load page with one mode-button exit and wait at least six
   seconds for the writer debounce and the session-exit HCNAMES write.
2. Copy the card again. Expected:
   - the trace tail gained the writer transaction `A/V/M/C/P/T`, with `C`
     equal to the unique dirty-byte count;
   - the `.hcprms` winner generation is exactly one higher and Scene 15's
     parameter bytes equal the loaded Scene's values;
   - `/.hcnames` rows 32 and 123-128 now carry the loaded Scene's embedded
     Kit name and six Instrument names with source `-`.

### 6.3 Test B - Scene Load with play active

Repeat Test A with the sequencer running. The bounded drumset apply must
finalize, the busy display must clear, and the same record sequence and
`.hcprms`/`.hcnames` outcomes must appear. `W` may document a longer
page-suppressed window.

### 6.4 Regressions

- Kit Load: unchanged. It never enters this callback, and its existing trace,
  writer, and HCNAMES behavior already worked in the NOPLAY capture.
- Bank Load: gains the same facade release through the shared callback;
  otherwise unchanged.
- Scene Save: untouched. The Save path terminates through its own
  index-rebuild chain, not this callback.
- The intentional Load/Save-page writer suppression: unchanged.
- Boot Scene Load: untouched. The pre-audio path does not post the command
  final-index restore.

## 7. What not to change

Do not add an AutoSave call to `filesystem_loadSceneDirectory_tick()`, move
the existing marker into another layer, invoke either publisher directly from
Menu, relax the intentional Load/Save page guard, or add another completion
path. Those changes would duplicate ownership and conceal the unacknowledged
terminal operation. Releasing the existing final callback is sufficient:
pending trace records can append, and after Load-page exit and debounce the
existing dirty mask publishes the normal `A/V/M/C/P/T` sequence and next
AutoSave generation.

## 8. Risks and mitigations

| # | Severity | Risk | Mitigation |
| --- | --- | --- | --- |
| R1 | Low | Acking while another operation owns the facade | Impossible by construction: the callback runs synchronously inside `filesystem_complete()` for this exact operation, and the facade is still terminal. |
| R2 | Low | Double acknowledgement | `filesystem_ack()` only converts `DONE`/`ERROR` to `IDLE`; a second call is a no-op. |
| R3 | Low | Error overlay loses its code | `filesystem_errorCode()` is not cleared by `filesystem_ack()`; `index_ok` was captured before the call. |
| R4 | Low | UI reset consults the now-IDLE status | `menu_finishLoadSaveCommand()` and the cache/overlay branches below the new call use only `index_ok` and Menu flags. |

## 9. Close-out

- No RAM allocation changes: no new state exists, so
  `knowledge_files/specification_reference/SRAM_MANIFEST.md` needs no update.
- The `R/W/F/G` stage documentation added to
  `knowledge_files/specification_reference/DEV_MODES.md` remains current.
- After Test A and Test B pass, record the fix and the field results in the
  session handoff log and archive this document alongside
  `SCENE_LOAD_ASAVE_TRACE_RESTORE.md`.
- Keep the temporary 2048-record trace ring until the `TRACE_EXTENSION.md`
  experiment's own revert step.

## 10. Hardware verification result and next-session log

### 10.1 What the post-fix capture proved

The Scene Load (SeaWaked, slot 024, into Scene 15) followed by a page exit to
Voice mode produced the complete expected record set in `SD_CARD/`:

- `R` flags=0x01 value=0x8000: the Scene completion callback ran with a DONE
  terminal status and the Scene-15 destination mask;
- the Scene-15 dirty burst: 496 payload bytes (40 Scene settings + 2 Kit
  values + 454 Instrument bytes), six `I` summaries with flags=0x07 and
  expected==accepted, plus both `L` records (Kit kind=0 and Scene kind=1);
- `S` (writer armed, deadline), `F` flags=0x01 value=505 (trace flush held
  while the command was active), and `W` flags=0x01 value=deadline (writer
  armed with dirty work while the Load page held it);
- after the page exit the writer admitted: `A/V/M/C/P/T` with `C=499`
  (3 boot Bank bytes + 496 Scene-15 bytes) publishing generation 6 into
  `.hcprms2`; `.hcprms1` remains generation 5 and the winner is now 6.

The facade acknowledgement is therefore field-verified: trace publication,
writer admission, and the mutation-register generation advance all work
after a root Scene Load.

### 10.2 Remaining gap logged for next session: Scene-session HCNAMES names

The `.hcnames` capture still shows Scene row 16 as `SeaWaked<TAB>024` but Kit
row 32 as `Pop` and Instrument rows 123-128 as the boot-time `pop*` names,
even though the user exited the page. The exit-time write never ran for this
session type. Mechanism located in `menu_switchPage()`
(`Core/Menu/menu.c:8282`): `end_resident_name_session` is true only for
Instrument, Kit, or KitMrp sessions. A Scene or Bank session exits through
the page boundary without calling `menu_endResidentNameScratchSession()`, so
FIX-A's accumulated `menu_residentNameDirtySceneMask` (0x8000 here) is
discarded and the register retains the boot Bank's identity rows.

Direction for next session: make the physical page-exit flush run whenever
the accumulated Scene mask is nonzero, regardless of the session type (e.g.,
OR the Scene/Bank predicate into the same boundary), so the existing
`filesystem_requestUpdateResidentKitNames()` path serializes the committed
Scene's Kit and six Instrument names. Do not create a second writer.

### 10.3 Source-token spec clarification for that fix

When a Kit arrives embedded in a Scene, its Kit row and six Instrument rows
must carry no source entry of their own: the empty field defaults upward to
the enclosing Scene row's source token. Only the names are registered. The
Scene loader's current identity publication
(`Core/Hardware/SD/filesystem.c:8973`) stamps
`FS_RESIDENT_SOURCE_INHERIT` for the Kit and Instrument rows, which renders
as `-`; the next-session fix must replace that with the empty default for
scene-embedded identity rows, and the register formatter/reader must treat
the empty field as upward inheritance as specified.

The autosave/trace work described by this document and
`SCENE_LOAD_ASAVE_TRACE_RESTORE.md` is complete for this session; the
HCNAMES name gap and its source-token rule are the next session's entry
point.
