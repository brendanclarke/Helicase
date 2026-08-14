# LOAD_SCENE_TRACE_AUDIT.md

**Audit target:** Why AutoSave dirty-bit mutation tracing for root Scene Load
(`Load:[Scene   ]` → encoder OK) cannot be observed, and why previous attempts
to add trace/log instrumentation inside the Scene loader produced no visible
signal ("black hole").

**Scope:** Read-only source audit of the uploaded `Helicase` tree. No code was
changed. All line numbers refer to the files as uploaded.

**Headline finding:** There is **no separate/parallel state machine** at the
filesystem layer for Scene Load — it shares the identical dispatcher used by
Kit and Bank Load. The autosave dirty-bit marking code for Scene Load **already
exists and already runs**. The reason nothing is observable is two compounding,
structural bugs in the trace pipeline itself:

1. A single Scene-load dirty-mark call emits far more trace records in one
   synchronous burst than the 64-entry RAM ring can hold, so the interesting
   part is overwritten before anything can flush it to disk.
2. The trace-flush scheduler explicitly refuses to run while the Load/Save
   menu page is active — which is exactly where you are when you click OK and
   immediately go looking for the trace.

A secondary, unrelated hazard (stale `*.c.failed` shadow files covering the
exact same subsystem) is also documented below because it is an easy way to
silently "lose" any new edit.

---

## 1. Full call chain: encoder OK on `Load:[Scene   ]`

```
menu_parseEncoder(inc, button)                              menu.c:7104
 └─ menu_handleLoadSaveMenu(inc, btnClicked)                 menu.c:6628
     └─ (menu_saveOptions.what == SAVE_TYPE_SCENE, OK click)
         preset_loadSceneForScenes(slot, scene_mask)         menu.c:6887
                                                               → presetManager.c:1975
           filesystem_ack();
           pm_status = PRESET_LOAD_IN_PROGRESS;
           pm_request_type = SAVE_TYPE_SCENE;
           pm_kit_request_scene_mask = scene_mask;
           filesystem_requestLoadSceneForScenes(slot, mask,
                                                 on_scene_load_complete)
                                                               filesystem.c:20845
             filesystem_start(FS_INTERNAL_OP_LOAD_SCENE,
                               FS_FILE_SCENE, slot, cb)
             op_scene_load_scene_mask = valid_mask;
             filesystem_initSceneStage(&fs_stage_workspace.scene_stage);
     └─ commandAccepted = 1 → menu_beginLoadSaveCommand()    menu.c:6910
           menu_loadSaveCommandActive = 1;  menu_storageBusy = 1;

// ---- every subsequent main-loop iteration ----
filesystem_tick()                                            filesystem.c:19645
 └─ if (status == FS_STATUS_IDLE) { …scheduler taps… }
 └─ if (status != FS_STATUS_BUSY) return;
 └─ switch (current_op) {
        case FS_INTERNAL_OP_LOAD_SCENE:
            filesystem_loadSceneDirectory_tick();             filesystem.c:19755
                                                                 → filesystem.c:8511
    }
```

`filesystem_loadSceneDirectory_tick()` is a large (~70+ phase) cooperative
state machine (`filesystem.c:8511` onward). Representative phases:

| phase | action |
|---|---|
| 0 | validate captured key, init Scene stage, `afatfs_chdir(NULL)` |
| 1–5 | open/chdir/close root `Scene/` |
| 6–11 | open selected `Scene/NNN Name/`, scan children for `Kit <n>/`, `.pat`, `.fx` |
| 12–?? | stream-parse `sceneset.scg` into `fs_stage_workspace.scene_stage.settings` |
| (continues) | validate/commit embedded Kit, commit Scene stage, HCNAMES update |

Completion:

```
filesystem_finish(FS_STATUS_DONE)                             filesystem.c:3140
  → current_op = FS_INTERNAL_OP_FLUSH_FINISH; op_phase = 0    (defers publish
                                                                 until asyncfatfs
                                                                 sync completes)
filesystem_flushFinish_tick()  (subsequent ticks)              filesystem.c:3257
  → filesystem_complete(FS_STATUS_DONE)                        filesystem.c:3109
       status = FS_STATUS_DONE;
       current_op = FS_INTERNAL_OP_NONE;
       cb()  ==  on_scene_load_complete()                      presetManager.c:413
```

`on_scene_load_complete()`:

```c
preset_markRequestedScenesPresentOnSuccessfulLoad();
if (filesystem_status() == FS_STATUS_DONE) {
    for (scene_index = 0; scene_index < SCENE_COUNT && scene_index < 16;
         scene_index++) {
        if (pm_kit_request_scene_mask & (1u << scene_index))
            autosave_markSceneWithoutPatternDirty(scene_index);   // <-- the
    }                                                              //     dirty-bit
}                                                                  //     marking
preset_completeFilesystemOp(PRESET_OP_SCENE_LOAD);                //     you want
```
(`presetManager.c:439-450`)

Menu then polls Preset status and, for Scene/Bank Load specifically, performs
one final read-only step before releasing the page:

```
menu_requestLoadCommandFinalIndexRestore()                     menu.c:2992
  → filesystem_requestReloadLibraryIndex(FS_LIBRARY_INDEX_SCENE,
                                          menu_loadCommandFinalIndexComplete)
  → menu_finishLoadSaveCommand()   (menu_loadSaveCommandActive = 0;
                                     menu_storageBusy = 0)
```

**Conclusion of §1:** the dirty-bit marking you're trying to add is *already
present* in `on_scene_load_complete()`. It calls
`autosave_markSceneWithoutPatternDirty()` for every scene bit in the accepted
load mask. Whatever is failing is not "the feature doesn't exist" — it's that
its effects (and any trace you bolt onto it) are being destroyed before you
can see them. See §3.

---

## 2. Comparison with Kit scroll-load / leave-menu path

Scrolling a Kit slot on `Load:[Kit     ]`:

```
menu_handleLoadSaveMenu() → menu_requestCurrentLoadSaveSelection(1)  menu.c:3050
  → preset_loadKitForScenes(slot, mask)                              presetManager.c:1883
      filesystem_requestLoadKitForScenes(...)
        filesystem_start(FS_INTERNAL_OP_LOAD_KIT, ...)

filesystem_tick()
 └─ switch (current_op) {
        case FS_INTERNAL_OP_LOAD_KIT:
        case FS_INTERNAL_OP_LOAD_KIT_MORPH:
            filesystem_loadKitDirectory_tick();                      filesystem.c:19751-19753
                                                                        → filesystem.c:8071
    }
 └─ filesystem_finish(DONE) → … → cb() == on_kit_load_complete()      presetManager.c:235
       autosave_markKitDirty(scene_index);                            presetManager.c:262
```

This is **structurally identical** to the Scene path: same `filesystem_tick()`
dispatcher, same `switch(current_op)`, sibling case labels
(`FS_INTERNAL_OP_LOAD_KIT` sits immediately above `FS_INTERNAL_OP_LOAD_SCENE`
in the switch at `filesystem.c:19751-19757`), same completion-callback pattern,
same downstream `autosave_mark*Dirty()` call. There is no bypass, no second
FSM, no hidden queue that Scene Load uses and Kit Load doesn't.

**What Kit Load does *not* trigger that Scene Load does:**

- Scene Load additionally chains one more filesystem request afterward
  (`menu_requestLoadCommandFinalIndexRestore()`, §1) to reload
  `/Scene/.hcindex` read-only before releasing the page. Kit Load's index
  reload happens as part of ordinary Save-side rebuild, not this deferred
  Load-only step. This adds one more BUSY→DONE cycle after
  `on_scene_load_complete()`, but does not touch autosave marking.
- Scene Load's dirty-mark call (`autosave_markSceneWithoutPatternDirty`) marks
  a vastly larger payload than Kit Load's marker in the *same* call (Scene
  settings + Effect scope + **the full Kit scope**, since
  `autosave_markSceneWithoutPatternDirty()` calls `autosave_markKitDirty()`
  internally — `Autosave.c:1294-1295`). This is the proximate cause of §3.1.

**What genuinely runs in parallel (and is likely what "parallel state
machine" referred to):** the post-load DSP/runtime apply worker
(`menu_startSoundApply()` / `preset_tickDrumsetApply()`, per project memory on
Scene activation: clear outgoing targets → image-apply six instrument types →
one all-source LFO/velocity rebind). This runs chunked across many main-loop
passes *after* filesystem completion and is completely independent of
`filesystem_tick()`/`on_scene_load_complete()`. It is real, it is concurrent,
and it *is* a second system — but it has nothing to do with autosave
dirty-bit production, which already finished synchronously before this worker
even starts. If a trace call was added inside this worker expecting it to
correlate with dirty-bit marking, it would appear to have no effect on
autosave state — a plausible source of the "parallel state machine bypasses
normal load" theory.

---

## 3. Where the trace actually goes: two compounding bugs

Every byte marked by `autosave_markSceneWithoutPatternDirty()` already funnels
through:

```c
static uint8_t autosave_markPayloadOffsetDirty(uint16_t payload_offset)
{
    if (!autosave_mutation_tracking_enabled ||
        payload_offset >= AUTOSAVE_PAYLOAD_BYTES) {
        return 0u;
    }
    autosave_maskByteOr(...);
    autosaveTrace_record(AUTOSAVE_TRACE_STAGE_DIRTY, 0u,
                         (uint32_t)payload_offset);   // <-- trace IS emitted
    return 1u;
}
```
(`Autosave.c:131-155`)

So a trace record genuinely is generated per marked byte. It just never
survives to be readable.

### 3.1 Ring overflow — guaranteed on every Scene Load

The trace ring is fixed at 64 records:

```c
#define AUTOSAVE_TRACE_RECORD_COUNT  64u     // AutosaveTrace.h:34
```

`autosave_markSceneWithoutPatternDirty()` (`Autosave.c:1277-1296`) is one
plain C function call — no filesystem ticks occur *inside* it — that marks:

- 40 Scene parameters (`AUTOSAVE_SCENE_PARAM_COUNT`, `Autosave.h:164`)
- 0 Effect parameters today (`AUTOSAVE_EFFECT_PARAM_COUNT = 0`, `Autosave.h:122`)
- the **full Kit scope**, via the nested `autosave_markKitDirty()` call:
  - `AUTOSAVE_KIT_PARAM_COUNT` (2) Kit-level parameters
  - **6 instruments**, each contributing 3 type bytes plus one dirty byte per
    live descriptor (normal), plus a second byte for every descriptor flagged
    `MORPHABLE` (morph) — `autosave_markWholeInstrumentDirty()`,
    `Autosave.c:1236-1256`

Depending on descriptor table sizes (HiHat alone is flagged
`Advanced|Choke`), this routinely produces **on the order of 150–500+
individual `AUTOSAVE_TRACE_STAGE_DIRTY` (`'D'`) records emitted back-to-back
in one synchronous call**, with zero opportunity for anything to flush the
ring in between.

The ring is a plain overwrite-oldest circular buffer:

```c
if (autosaveTrace_pendingCountUnsafe() > AUTOSAVE_TRACE_RECORD_COUNT) {
    autosave_trace_flush_cursor = (uint16_t)(
        autosave_trace_write_cursor - AUTOSAVE_TRACE_RECORD_COUNT);
    if (autosave_trace_dropped != UINT16_MAX)
        autosave_trace_dropped++;
}
```
(`AutosaveTrace.c:73-78`)

So the ring wraps multiple times during a single Scene Load, and everything
except the last ~64 raw per-byte offsets is silently discarded. Confirmed by
source inspection: **`autosave_trace_dropped` / `autosaveTrace_droppedCount()`
is never read anywhere in the codebase outside `AutosaveTrace.c` itself**
(only referenced in a comment at `filesystem.c:4153`). Nothing surfaces the
loss counter to a screen, a log, or a return value today.

**Contrast with Instrument Load**, which hit this exact same problem earlier
and was fixed by adding two dedicated *summary* trace stages that survive the
wrap regardless of how many raw `D` records are lost:

```c
AUTOSAVE_TRACE_STAGE_INSTRUMENT_MARK   = 'I',   // AutosaveTrace.h:45
AUTOSAVE_TRACE_STAGE_INSTRUMENT_COMMIT = 'J',   // AutosaveTrace.h:52
```
These pack scene/slot/expected-count/published-count into one 32-bit value
and are recorded once per instrument commit (`Autosave.c:1224-1233`),
independent of how many `D` records preceded them in the same ring.

**Scene Load has no equivalent summary stage.** There is nothing that records
"Scene N load reached the marker and published X of Y expected bytes" as a
single durable fact. This is the direct, structural reason a Scene-load trace
attempt looks like a black hole: even if your added instrumentation runs, it
is statistically almost certain to be overwritten by the flood of `D` records
from the same call before anyone reads the ring.

### 3.2 Flush scheduler is disabled while the Load/Save page is active

Even the ~64 records that do survive can't reach `asavetrc.bin` on the card
while you're still parked on the page where you'd naturally go looking for
them:

```c
static void filesystem_autosaveTraceFlushSchedule_tick(void)
{
#if DEV_MODE_LOGGING
    ...
    if (menu_activePage == LOAD_PAGE || menu_activePage == SAVE_PAGE)
        return;                                    // filesystem.c:19620
    if (autosaveTrace_pendingCount() == 0u)
        return;
    ...
    filesystem_start(FS_INTERNAL_OP_AUTOSAVE_TRACE_FLUSH, ...);
#endif
}
```
(`filesystem.c:19602-19643`)

This is intentional — it keeps the one filesystem facade free for the
in-flight Load/Save command chain (including the deferred index-restore step
in §1) rather than letting an optional diagnostic append contend for it. But
the practical effect is: **`asavetrc.bin` is never written to while
`menu_activePage` is `LOAD_PAGE` or `SAVE_PAGE`.** The flush only happens once
you navigate to a different menu page, or on the periodic idle cadence after
that.

This scheduler is only invoked from an idle tick in the first place:

```c
if (status == FS_STATUS_IDLE)
    filesystem_autosaveTraceFlushSchedule_tick();      // filesystem.c:19714-19715
```

### 3.3 Net effect

If your test procedure is "click OK on `Load:[Scene   ]`, then immediately
inspect the card or power off while still on that page," you will observe
nothing durable, **regardless of what instrumentation you add**, because:

1. Almost all of the per-byte trace evidence from the Scene marking call is
   already gone from RAM (overwritten) before the operation even finishes.
2. The scheduler that would write the surviving remainder to disk explicitly
   refuses to run until you leave the Load/Save page.

This matches the reported symptom exactly: the program doesn't crash, doesn't
hang, doesn't error — it just produces no observable trace signal, over and
over, regardless of where instrumentation is added inside the loader.

---

## 4. Secondary hazard: stale `*.c.failed` shadow files

The uploaded tree contains full duplicate copies of the exact files this
subsystem touches, with a `.failed` suffix, sitting in the same directories
as the live sources:

```
Core/Bank/BankData.c.failed
Core/Bank/BankData.h.failed
Core/Bank/Scene/Preset/presetManager.c.failed
Core/Bank/Scene/Preset/presetManager.h.failed
Core/Bank/Scene/SceneData.c.failed
Core/Bank/Scene/SceneData.h.failed
Core/Hardware/SD/filesystem.c.failed
Core/Hardware/SD/filesystem.h.failed
Core/Menu/menu.c.failed
Core/Menu/menu.h.failed
```

Confirmed via `Makefile` (`SRC` list, lines ~53-140): **only the non-`.failed`
files are compiled.** These shadow copies are not currently linked into the
firmware image and are not the cause of the trace loss above. However:

- They are same-directory, same-basename as the live files (differ only by
  the trailing `.failed`), which makes them trivially easy to open/edit by
  mistake, especially for a human or agent scanning the directory casually.
- If any future edit lands in the `.failed` twin instead of the live file, it
  will compile clean (i.e., not compile at all, silently, since it's not in
  the `SRC` list) and produce **exactly** the "I added a trace and got
  nothing back" experience, independent of the two structural bugs above.
- The `.failed` versions are also *larger* than the live versions for
  `BankData`, `presetManager`, `SceneData`, and `menu` (though *smaller* for
  `filesystem.c`), consistent with being a pre-revert snapshot of a more
  complex, abandoned implementation attempt (matching git history: commit
  `71823d5 autosave mark-dirty for all load types: attempt` precedes
  `ae7349b close session 049: testing for autosave write kit, morph, scene
  done, issues with bank identified`).

**Recommendation:** delete these files or move them out of `Core/` entirely
(e.g. into a `knowledge_files/` reference/archive location) once confirmed
nothing in them is still needed, so they stop being a plausible place for a
future edit to silently vanish into.

---

## 5. Recommended fix path

1. **Add a dedicated Scene-load summary trace stage**, mirroring the existing
   Instrument pattern instead of relying on raw `D` records:

   ```c
   // AutosaveTrace.h
   AUTOSAVE_TRACE_STAGE_SCENE_MARK = 'E',   // pick an unused code
   ```

   Record it once, at the end of `autosave_markSceneWithoutPatternDirty()`
   (`Autosave.c:1277`), packing scene index / expected byte count / published
   byte count into the 32-bit value field, exactly like
   `AUTOSAVE_TRACE_INSTRUMENT_MARK` does today (`Autosave.c:1224-1233`). This
   survives the `D`-record wrap because it's one record, emitted last.

2. **Instrument the mask-loop in `on_scene_load_complete()`** itself
   (`presetManager.c:439-450`), not inside `filesystem_loadSceneDirectory_tick()`
   — the filesystem tick function only loads data into the stage; it does not
   own the autosave marking call at all, so trace calls placed there cannot
   observe marking outcomes.

3. **Never test by reading the card while still on `Load:[Scene   ]` or
   `Save:[...]`.** Navigate to any other menu page first (this triggers
   `filesystem_autosaveTraceFlushSchedule_tick()` to actually run), or add a
   temporary bench-only override of the `menu_activePage` guard at
   `filesystem.c:19620` while diagnosing.

4. **Print/expose `autosaveTrace_droppedCount()`** before and after a Scene
   Load (e.g. via the existing `DEV_MODE_DIAGNOSTIC` screen path, or a one-off
   printf/log at the Menu level) to directly confirm the ring-wrap magnitude
   predicted in §3.1 before trusting any `D`-record-level trace from a Scene
   Load.

5. Once (1)–(4) are in place, re-attempt the original goal: verifying that
   `Load:[Scene   ]` → OK produces correct, observable mutation/dirty bits in
   the `.hcprms` autosave registers ahead of the next drain cycle.

---

## 6. Summary table

| Question | Answer |
|---|---|
| Is there a parallel/hidden Scene-load state machine bypassing Kit/Instrument load? | **No**, at the filesystem layer. `FS_INTERNAL_OP_LOAD_SCENE` and `FS_INTERNAL_OP_LOAD_KIT` are dispatched from the same `switch` in the same `filesystem_tick()`. |
| Is there *anything* that runs concurrently and could look like a second system? | Yes — the post-load DSP/runtime apply worker (`menu_startSoundApply()`/`preset_tickDrumsetApply()`), but it is unrelated to autosave dirty-bit marking, which already completed earlier in `on_scene_load_complete()`. |
| Does Scene Load already mark autosave dirty bits? | **Yes** — `on_scene_load_complete()` already calls `autosave_markSceneWithoutPatternDirty()` per masked scene (`presetManager.c:439-448`). |
| Does that marking already emit trace records? | **Yes**, one `AUTOSAVE_TRACE_STAGE_DIRTY` record per byte, via `autosave_markPayloadOffsetDirty()` (`Autosave.c:147`). |
| Why can't you see it? | (a) ~150–500+ records emitted synchronously in one call overflow the 64-slot ring before anything can flush it, with no Scene-level summary stage to survive the wrap; (b) the flush scheduler explicitly refuses to run while `menu_activePage` is `LOAD_PAGE`/`SAVE_PAGE` (`filesystem.c:19620`). |
| Is the `.failed` file set the cause? | Not currently (not compiled), but it's a live hazard for any future edit landing in the wrong twin. |
