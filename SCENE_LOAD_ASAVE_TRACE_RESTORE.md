# SCENE_LOAD_ASAVE_TRACE_RESTORE.md - Scene Load Autosave and HCNAMES Registration Fix (Concrete Diff)

## Implementation notes

2026-08-16 — Implementation started after the prior source edits were
reverted. The source changes are being reapplied with the complete rationale
from each concrete-diff entry preserved beside the changed code. The
intentional AutoSave writer Load/Save-page admission guard remains unchanged;
the W witness will document that boundary without making the writer drain while
the shared name cache belongs to the browser. The approved 2-byte
`drumset_apply_stall_ticks` allocation and the logging-only evidence latches
remain the only new retained state.

2026-08-16 — Verification completed: `make clean && make -j2` passed with
`DEV_MODE_LOGGING=0`; the linked image contained no `autosave_trace` ring or
new filesystem witness symbols. The flag was restored to `1`, and a clean
logging-on build passed with the approved `drumset_apply_stall_ticks` (2 B),
`fs_autosave_suppress_witness` (1 B), `fs_trace_suppress_witness` (1 B), and
`fs_trace_reported_dropped` (2 B) symbols present. The only diagnostics were
pre-existing unused-function, linker syscall-stub, and LTO serialisation
warnings. Hardware Tests A/B remain the next validation step.

2026-08-16 — SD_CARD_NOEXIT field capture reviewed. The earlier assessment
that the card might predate the fix is **wrong**: `SD_CARD_NOEXIT/LXRV2_lxr02.img`
is byte-identical to the freshly packaged current-build image (SHA-256
`0a61f995…`), so the test genuinely ran this code. The trace grew by exactly
276 records between the old SD_CARD copy and SD_CARD_NOEXIT: two complete
tracking-off boot sweeps (128 records each) and two 3-byte Bank writer drains
publishing generations 4 and 5 (`.hcprms2` 2→4, `.hcprms1` 3→5 across the
two boots). The file ends at the second boot drain's `T` (tick 0x28fc)
and contains **zero** records from the Scene Load: no `R`, no tracking-on
`D/I/L`, no `S`, no `F`, no `W`. The `.hcnames` diff proves the load's
filesystem terminal work ran on the new build (Scene row 16 changed
`Forest/009` → `KitWool/016`), while the Kit row and six Instrument rows
remain stale as expected before session exit. What the capture cannot show:
`R/F/W` are RAM-ring records and only become card-visible after a successful
flush, which requires the command to have finalized before power-off. A
no-exit capture is therefore structurally unable to distinguish "completion
callback never ran" from "records were pending in RAM at power-off". The
decisive next pass is the exit leg: same Scene Load, wait for the busy
indicator to clear (record whether `...` actually cleared), leave the Load
page, wait ≥6 s, then copy. If `R/D/I/L/S/F/W` plus the writer `A/V/M/C/P/T`
land, the no-exit copy was taken too early; if the writer drain lands without
`R`, the callback chain is genuinely broken; if neither lands, dirty marking
never happened and the completion chain is the failure.

2026-08-16 — Root cause confirmed against SD_CARD_NOPLAY:
`menu_loadCommandFinalIndexComplete()` (Core/Menu/menu.c:2965) is the final
step of a root Scene/Bank Load and never calls `filesystem_ack()`. The shared
facade therefore remains at `FS_STATUS_DONE` after the read-only index
restore, and both idle-only schedulers (trace flush and AutoSave writer)
never run again until an unrelated later operation acknowledges. The Kit
Load in the same capture proved the differential: Kit Load has no final
index restore, its Preset completion acknowledges, and its full D/I/L burst
plus W landed on the card, while the Scene Load's R/D/L records stayed in
RAM and were lost at power-off. SCENE_LOAD_RECORDS_FIX.md's single
`filesystem_ack()` addition in that callback (after sampling status, before
`menu_finishLoadSaveCommand()`) is the correct, sufficient fix; it matches
the existing `menu_residentNameScratchFlushComplete()` precedent. Hardware
re-run after implementing it should reproduce the original Test A expected
sequence without leaving the Load page.

## 1. Purpose

This document is the implementation-ready plan for making root Scene Load
complete its identity registration and autosave mutation reporting, and for
instrumenting the two known publish gaps (trace publish, autosave mutation
register publish) so a field pass produces decisive evidence for any remaining
failure.

Revision 3's conclusions are retained:

- The AutoSave writer's Load/Save-page suppression is intentional (shared
  9,000-byte name-cache ownership contract; AUTOSAVE.md: "Load and Save pages
  suppress new background starts"). No writer-scheduler behavior change is
  made.
- The HCNAMES Kit/Instrument-row gap is real: the Scene Load completion never
  accumulates its destination into the name session.
- The post-load sound apply has no bound, so during continuous playback the
  Load command can stall and hide all downstream evidence.

This revision adds the evidence instrumentation requested: every change site
below is either a fix or a bounded, logging-only record that documents where
the trace publish or the autosave register publish stops.

## 2. Field evidence (2026-08-16, logging-on 2048-ring build)

Repeated hardware passes:

- /.hcnames: Scene-name row updates (Scene 15 = Forest/009); Kit row stays
  Pop/- and the six Instrument rows stay popd1/beatmsd2/...
- /asavetrc.bin: only the boot Bank-Load sweep (tracking-off flags), S, and at
  most a three-byte Bank D burst. No Scene-15 D/I, no L witness, no second S,
  no writer A/V/M/C/P/T.
- /.hcprms: .hcprms1 generation 3 differs from generation 2 only in the three
  Bank bytes at payload offsets 12-14; every Scene-15 parameter byte is
  unchanged. The Scene Load was never captured.

A Kit Load pass on the same build worked completely: 456-byte bursts, six I
summaries with flags=0x07, an L kind=KIT witness, a published generation, and
the Kit row plus six Instrument rows registered in /.hcnames after leaving the
Load page.

## 3. Root-cause analysis

### R1 - HCNAMES Kit/Instrument rows: Scene Load completion omits the name-session accumulation

menu_pollPresetStatus() accumulates the committed destination mask for
PRESET_OP_KIT_LOAD via menu_refreshResidentNameScratchKit() but not for
PRESET_OP_SCENE_LOAD. The existing exit-time writer
(filesystem_requestUpdateResidentKitNames() from
menu_endResidentNameScratchSession()) therefore has no destination mask after
a Scene Load and never rewrites the Kit plus six Instrument rows. The Scene
loader already populates the identity block those rows are serialized from.
Fix: FIX-A.

### R2 - Writer suppression on Load/Save pages is intentional, not a defect

filesystem_autosaveWriterSchedule_tick() refuses admission while
menu_activePage is LOAD_PAGE or SAVE_PAGE. FILESYSTEM_SPEC.md owns the single
9,000-byte shared list/register (one .hcindex domain or the HCNAMES image,
never both); the AutoSave writer will eventually borrow that cache for its
mutation-bit image, and Load/Save browsing owns it for the selected .hcindex.
So a Scene Load's armed dirty bits drain to /.hcprms only after the user
leaves the page. No scheduler change is made. Evidence: EVID-4 (W record)
documents this suppression boundary so a field capture can distinguish
"suppressed by design" from every other drain failure.

The trace flush is separate: it uses the 512-byte staging buffer, never the
name cache, and its guard is already menu_isLoadSaveCommandActive() (5.3,
shipped). It drains once the accepted command finalizes, even on the page.

### R3 - The post-load apply has no bound and can strand the command

preset_tickDrumsetApply() waits for instrumentManager_ampEnvelopeQuiet(voice)
with no bound. During continuous playback the apply, the final /Scene/.hcindex
restore, and menu_finishLoadSaveCommand() can strand; the trace flush then
stays blocked by the command-active guard and the writer stays blocked by the
page guard. Fix: FIX-B.

By source inspection the marking chain is intact (filesystem_start stores
on_scene_load_complete, the phase-72 handoff preserves completion_callback,
filesystem_complete(DONE) invokes it with status DONE and the captured mask).
Whether the callback actually produces bits has not been directly observable
because R2/R3 hide its records. Evidence: EVID-2 (R record) makes the callback
execution, status, and mask observable in one record; EVID-5 (F record) and
EVID-4 (W record) document the two gates; EVID-6 (G record) documents ring
overflow.

## 4. Change inventory

| ID | File | Location | Kind |
| --- | --- | --- | --- |
| FIX-A | Core/Menu/menu.c | menu_pollPresetStatus(), case PRESET_OP_SCENE_LOAD (line 7639) | fix: name-session accumulation |
| FIX-B1 | Core/Bank/Scene/Preset/presetManager.c | drumset-apply statics (line 126) | fix: stall counter + bound define |
| FIX-B2 | Core/Bank/Scene/Preset/presetManager.c | preset_startDrumsetApply() (line 1310) | fix: reset stall counter |
| FIX-B3 | Core/Bank/Scene/Preset/presetManager.c | preset_tickDrumsetApply() (line 1371) | fix: bounded force-commit |
| EVID-1 | Core/Bank/Scene/AutosaveTrace.h | stage enum (line 70) and LOAD_MARK macro block (line 145) | evidence: new R/W/F/G stages |
| EVID-2 | Core/Bank/Scene/Preset/presetManager.c | on_scene_load_complete() entry (line 413) | evidence: R record |
| EVID-3 | Core/Hardware/SD/filesystem.c | logging-only trace statics (line 1436) | evidence: 4 bytes of logging-only latches |
| EVID-4 | Core/Hardware/SD/filesystem.c | filesystem_autosaveWriterSchedule_tick() admission guard (line 19546) | evidence: W record |
| EVID-5 | Core/Hardware/SD/filesystem.c | filesystem_autosaveTraceFlushSchedule_tick() guard (line 19638) and filesystem_autosaveTraceFlushCompleted() (line 19599) | evidence: F records |
| EVID-6 | Core/Hardware/SD/filesystem.c | filesystem_autosaveTraceFlushCompleted() (line 19599) | evidence: G record |
| DOC-1 | knowledge_files/specification_reference/DEV_MODES.md | /asavetrc.bin stage list | documentation: R/W/F/G |

RAM accounting:

- FIX-B1: drumset_apply_stall_ticks, 2 bytes, uninitialized static in normal
  SRAM1 .bss, owner Preset drumset apply, process lifetime. Approved
  (2026-08-16).
- EVID-3: fs_autosave_suppress_witness (1 byte), fs_trace_suppress_witness
  (1 byte), fs_trace_reported_dropped (2 bytes) = 4 bytes total, all inside
  #if DEV_MODE_LOGGING. Per the RAM allocation policy, logging-only
  allocations are permitted while DEV_MODE_LOGGING is enabled; a logging-off
  build allocates none. No production-RAM growth.
- All other changes add no retained RAM.

## 5. Concrete diffs

Every hunk below is given with the exact current context read from the working
tree on 2026-08-16.

### FIX-A - menu.c: Scene Load completion accumulates the name-session mask

What it does:

Records every committed destination of a completed root Scene Load in
menu_residentNameDirtySceneMask, exactly as PRESET_OP_KIT_LOAD does. The
existing session-exit writer then rewrites one Kit row and six Instrument rows
per accumulated Scene from the identity block the Scene loader populated.

Why it must exist:

This is the R1 gap. Without it the exit-time writer has no destination mask
for a Scene Load, which is why the Kit and Instrument rows stay stale in the
field captures. It reuses the hardware-confirmed Kit Load mechanism and adds
no new writer.

Inputs: preset_getKitRequestSceneMask() (the immutable destination mask
captured by preset_loadSceneForScenes()); the already-populated filesystem
identity block; menu_residentNameDirtySceneMask.

Outputs: menu_residentNameDirtySceneMask gains the destination bits; no card
I/O occurs here. HCNAMES rows change later, at session exit, through
menu_endResidentNameScratchSession() -> filesystem_requestUpdateResidentKitNames().

Exact diff (Core/Menu/menu.c):

     case PRESET_OP_SCENE_LOAD:
     {
+        /*
+         * Retain the committed Scene's Kit plus six Instrument identities for
+         * the one Menu-exit HCNAMES write, exactly as a committed Kit Load
+         * does. The filesystem Scene loader has already populated the identity
+         * block and written the Scene-name row; this only accumulates the
+         * destination mask so the existing exit-time Kit-row updater covers
+         * this load. HCNAMES and /Scene/.hcindex stay untouched until the
+         * name session exits.
+         */
+        menu_refreshResidentNameScratchKit(
+            preset_getKitRequestSceneMask());
         if ((menu_activePage == LOAD_PAGE || menu_activePage == SAVE_PAGE) &&
             !menu_isLoadSaveSelectionCurrent()) {
             retrySelectionAfterAck = 1;
             retrySelectionLoadKit = 0;
             break;
         }

Placement note: the call is before the selection-retry check so a
deferred-selection retry still accumulates, matching the Kit case's
unconditional accumulation.

### FIX-B1 - presetManager.c: stall counter and force bound

What it does:

Adds one 16-bit counter for consecutive non-quiet foreground passes and a
1,000-tick (one second at the 1 kHz main tick) force bound.

Why it must exist:

The R3 stall has no bound today. The counter drives the bounded force-commit
in FIX-B3 and is reset by FIX-B2 and by every real commit.

Inputs: none at declaration; incremented per pass in preset_tickDrumsetApply().

Outputs: a retained counter owned by the Preset drumset-apply cursor.

Exact diff (Core/Bank/Scene/Preset/presetManager.c):

 static uint8_t drumset_apply_active = 0;
+/*
+ * Foreground passes a pending voice may stay non-quiet before the existing
+ * trigger-time force path commits it anyway. 1,000 passes at the 1 kHz main
+ * tick is one second; the bound exists so continuous playback cannot strand
+ * the post-load apply, the Load command, and therefore the trace flush.
+ */
+#define DRUMSET_APPLY_FORCE_TICKS 1000u
 static uint8_t drumset_apply_voice = 0;
+static uint16_t drumset_apply_stall_ticks = 0u;
 static uint8_t drumset_apply_scene = 0u;
 static uint16_t drumset_apply_pending_mask = 0u;

### FIX-B2 - presetManager.c: reset the stall counter per apply

What it does:

Zeroes the stall counter whenever a new drumset apply is armed.

Why it must exist:

A stale count from a previous worker could force-commit immediately in the
next worker.

Inputs: none.

Outputs: counter starts at zero for the new apply.

Exact diff (Core/Bank/Scene/Preset/presetManager.c):

     drumset_apply_active = 1u;
     drumset_apply_voice = 0u;
+    drumset_apply_stall_ticks = 0u;
 }

### FIX-B3 - presetManager.c: bounded force-commit in the apply loop

What it does:

When the tested pending voice is not envelope-quiet, increments the stall
counter; at DRUMSET_APPLY_FORCE_TICKS it calls the existing
preset_applyDeferredSceneSlotForTrigger(voice) force path and resets the
counter. Any real quiet commit resets the counter.

Why it must exist:

This is the R3 fix. The force path already exists and is accepted for
trigger-time commits; this only adds a bounded timeout so a continuously
ringing voice cannot hold the command open forever.

Inputs: drumset_apply_pending_mask, the round-robin voice, the amp-envelope
quiet test, drumset_apply_stall_ticks.

Outputs: at most one forced commit per DRUMSET_APPLY_FORCE_TICKS passes of
non-progress; the pending mask drains deterministically, the rebind cursor
starts when it empties, and menu_finishSoundApply() can proceed.

Exact diff (Core/Bank/Scene/Preset/presetManager.c):

         if ((drumset_apply_pending_mask & bit) == 0u)
             continue;
-        if (!instrumentManager_ampEnvelopeQuiet(voice))
-            continue;
+        if (!instrumentManager_ampEnvelopeQuiet(voice)) {
+            if (++drumset_apply_stall_ticks >= DRUMSET_APPLY_FORCE_TICKS) {
+                drumset_apply_stall_ticks = 0u;
+                preset_applyDeferredSceneSlotForTrigger(voice);
+            }
+            continue;
+        }
+        drumset_apply_stall_ticks = 0u;
 
         preset_resetAndApplyKitVoiceImage(drumset_apply_scene, voice);

### EVID-1 - AutosaveTrace.h: R/W/F/G evidence stages

What it does:

Adds four logging-only stage codes and their flag macros. Producers are
no-op stubs in logging-off builds, so no production behavior or RAM changes.

- R: scene-load completion witness.
- W: autosave writer suppressed by the Load/Save page while dirty.
- F: trace flush suppressed by the command-active guard (bit 0) or a failed
  append (bit 1).
- G: ring dropped-count publication.

Why it must exist:

These are the minimal records that separate every failure mode in the two
publish pipelines (see the matrix in section 7). Without them, "callback never
ran", "records pending but flush blocked", "writer armed but page-suppressed",
and "ring overflow" are all indistinguishable from an empty log.

Inputs: none at declaration.

Outputs: four additional one-byte stage identifiers; no new storage.

Exact diff (Core/Bank/Scene/AutosaveTrace.h):

     AUTOSAVE_TRACE_STAGE_PUBLISHED = 'P',
     AUTOSAVE_TRACE_STAGE_TERMINAL = 'T',
+    /*
+     * One root Scene Load completion witness emitted before the Preset marker
+     * loop. flags bit 0 = filesystem status DONE observed; value = the
+     * destination Scene mask the callback will iterate. Absence of this record
+     * means the completion callback never ran.
+     */
+    AUTOSAVE_TRACE_STAGE_SCENE_LOAD_COMPLETE = 'R',
+    /*
+     * One bounded witness that the autosave writer was armed with dirty work
+     * but declined admission because the Load/Save page owns the shared name
+     * cache. flags bit 0 = the canonical mask had dirty bits; value = the
+     * armed debounce deadline.
+     */
+    AUTOSAVE_TRACE_STAGE_WRITER_SUPPRESSED = 'W',
+    /*
+     * One bounded witness for the trace flush pipeline. flags bit 0 = the
+     * command-active guard declined an append while records were pending
+     * (value = pending count); bit 1 = a started append reached ERROR.
+     */
+    AUTOSAVE_TRACE_STAGE_TRACE_SUPPRESSED = 'F',
+    /*
+     * One record per changed ring dropped-count, emitted after a successful
+     * trace append. value = autosaveTrace_droppedCount().
+     */
+    AUTOSAVE_TRACE_STAGE_TRACE_DROPPED = 'G',
 } autosave_trace_stage_t;

And after the existing LOAD_MARK macro block (near line 145):

+/* SCENE_LOAD_COMPLETE flags: bit 0 = filesystem status DONE observed. */
+#define AUTOSAVE_TRACE_SCENE_LOAD_COMPLETE_FLAG_STATUS_DONE (1u << 0u)
+/* TRACE_SUPPRESSED flags: bit 0 = command-active decline, bit 1 = append error. */
+#define AUTOSAVE_TRACE_TRACE_SUPPRESSED_FLAG_COMMAND_ACTIVE (1u << 0u)
+#define AUTOSAVE_TRACE_TRACE_SUPPRESSED_FLAG_APPEND_ERROR   (1u << 1u)

Stage-code check: R, W, F, G are unused in the current enum; the removed Q
stage must not be reintroduced.

### EVID-2 - presetManager.c: R witness at the completion callback entry

What it does:

Emits one R record as the first statement of on_scene_load_complete(), before
any presence promotion or marking.

Why it must exist:

The existing L kind=SCENE witness is emitted only after the marking loop is
entered. R separates the three indistinguishable cases: callback never
invoked (no R), invoked with a non-DONE status (R flags=0), or invoked with an
empty destination mask (R value=0).

Inputs: filesystem_status() and pm_kit_request_scene_mask at callback entry.

Outputs: one RAM-only trace record; logging-off builds emit nothing.

Exact diff (Core/Bank/Scene/Preset/presetManager.c):

 static void on_scene_load_complete(void)
 {
     uint8_t scene_index;
 
+    autosaveTrace_record(AUTOSAVE_TRACE_STAGE_SCENE_LOAD_COMPLETE,
+                         (uint8_t)(filesystem_status() == FS_STATUS_DONE
+                                       ? AUTOSAVE_TRACE_SCENE_LOAD_COMPLETE_FLAG_STATUS_DONE
+                                       : 0u),
+                         (uint32_t)pm_kit_request_scene_mask);
     /*
      * Complete a durable root Scene load.

### EVID-3 - filesystem.c: logging-only evidence latches

What it does:

Adds three logging-only statics next to the existing logging-only trace
deadline: a writer-suppression edge latch, a trace-suppression/error edge
latch, and the last-reported dropped count.

Why it must exist:

W and F must fire once per episode, not once per tick, or they would flood the
ring while the user browses. The last-reported dropped count prevents a G
record on every append once drops exist.

Inputs: none at declaration.

Outputs: 4 bytes of SRAM1 .bss only in DEV_MODE_LOGGING builds; zero bytes in
production.

Exact diff (Core/Hardware/SD/filesystem.c):

 #if DEV_MODE_LOGGING
 static uint16_t fs_autosave_trace_next_due_tick = 0u;
+static uint8_t fs_autosave_suppress_witness = 0u;
+static uint8_t fs_trace_suppress_witness = 0u;
+static uint16_t fs_trace_reported_dropped = 0u;
 #endif

### EVID-4 - filesystem.c: W witness at the writer's page suppression

What it does:

In filesystem_autosaveWriterSchedule_tick(), when the Load/Save-page guard
declines admission while dirty work exists, emits one W record per episode and
sets the edge latch. The latch clears whenever the writer is not
page-suppressed.

Why it must exist:

The page suppression is by design (R2), but the trace currently cannot prove
that the armed dirty bits were actually waiting there. W documents the
designed boundary so a field pass can distinguish it from a debounce wait, a
start failure, or a validation/capture failure.

Inputs: menu_activePage, autosave_maskHasDirty(),
fs_autosave_next_due_tick, fs_autosave_suppress_witness.

Outputs: at most one W record per armed dirty episode; value carries the armed
deadline for cross-checking against S.

Exact diff (Core/Hardware/SD/filesystem.c):

-    if (menu_activePage == LOAD_PAGE || menu_activePage == SAVE_PAGE ||
-        (uint16_t)(now - fs_autosave_next_due_tick) >= 0x8000u) {
-        return;
-    }
+    if (menu_activePage == LOAD_PAGE || menu_activePage == SAVE_PAGE) {
+#if DEV_MODE_LOGGING
+        /*
+         * Document the designed suppression boundary once per armed dirty
+         * episode: the canonical mask has work, the writer is armed, but the
+         * Load/Save page owns the shared name cache. flags bit 0 = dirty
+         * pending; value = the armed debounce deadline.
+         */
+        if (!fs_autosave_suppress_witness && autosave_maskHasDirty()) {
+            autosaveTrace_record(AUTOSAVE_TRACE_STAGE_WRITER_SUPPRESSED,
+                                 (uint8_t)(autosave_maskHasDirty() ? 1u : 0u),
+                                 (uint32_t)fs_autosave_next_due_tick);
+            fs_autosave_suppress_witness = 1u;
+        }
+#endif
+        return;
+    }
+#if DEV_MODE_LOGGING
+    fs_autosave_suppress_witness = 0u;
+#endif
+    if ((uint16_t)(now - fs_autosave_next_due_tick) >= 0x8000u)
+        return;

### EVID-5 - filesystem.c: F witnesses for the trace flush gates

What it does:

(a) In filesystem_autosaveTraceFlushSchedule_tick(), when the command-active
guard declines an append while records are pending, emits one F record with
bit 0 and the pending count, once per episode. The latch clears when an append
starts or when the ring drains.

(b) In filesystem_autosaveTraceFlushCompleted(), a terminal ERROR emits one F
record with bit 1, once per error episode; the ring retains and retries the
same records, so this documents a failing SD append rather than lost records.

Why it must exist:

These are the two places the trace publish pipeline can stop after the
producer has run: the command-active gate and the append itself. Without F, a
field capture cannot tell "produced but gated" from "never produced".

Inputs: menu_isLoadSaveCommandActive(), autosaveTrace_pendingCount(), the
append's terminal status, fs_trace_suppress_witness.

Outputs: at most one F per gate episode; value carries the pending count for
the suppression case and zero for the error case.

Exact diff (a), Core/Hardware/SD/filesystem.c:

-    if (menu_isLoadSaveCommandActive())
-        return;
+    if (menu_isLoadSaveCommandActive()) {
+#if DEV_MODE_LOGGING
+        /*
+         * Document the trace-publish gap once per pending episode: records
+         * exist in the ring but the accepted command still owns the facade.
+         */
+        if (!fs_trace_suppress_witness &&
+            autosaveTrace_pendingCount() != 0u) {
+            autosaveTrace_record(
+                AUTOSAVE_TRACE_STAGE_TRACE_SUPPRESSED,
+                AUTOSAVE_TRACE_TRACE_SUPPRESSED_FLAG_COMMAND_ACTIVE,
+                (uint32_t)autosaveTrace_pendingCount());
+            fs_trace_suppress_witness = 1u;
+        }
+#endif
+        return;
+    }
 
     if (autosaveTrace_pendingCount() == 0u)
         return;

And where the append starts (same function):

     if (filesystem_start(FS_INTERNAL_OP_AUTOSAVE_TRACE_FLUSH,
                          FS_FILE_SETTINGS, 0u,
                          filesystem_autosaveTraceFlushCompleted)) {
         fs_autosave_trace_next_due_tick = (uint16_t)(
             now + AUTOSAVE_TRACE_FLUSH_INTERVAL_MS);
+#if DEV_MODE_LOGGING
+        fs_trace_suppress_witness = 0u;
+#endif
     }

Exact diff (b), Core/Hardware/SD/filesystem.c:

 static void filesystem_autosaveTraceFlushCompleted(void)
 {
-    /* Re-arm immediately after a successful full-batch append while backlog remains. */
 #if DEV_MODE_LOGGING
-    if (status == FS_STATUS_DONE &&
-        autosaveTrace_pendingCount() >= AUTOSAVE_TRACE_FLUSH_BATCH_RECORDS)
-        fs_autosave_trace_next_due_tick = 0u;
+    if (status == FS_STATUS_DONE) {
+        /* Re-arm immediately after a successful full-batch append while
+         * backlog remains. */
+        if (autosaveTrace_pendingCount() >=
+            AUTOSAVE_TRACE_FLUSH_BATCH_RECORDS)
+            fs_autosave_trace_next_due_tick = 0u;
+        fs_trace_suppress_witness = 0u;
+    } else if (status == FS_STATUS_ERROR && !fs_trace_suppress_witness) {
+        /* flags bit 1 documents a failed append; the ring retains and
+         * retries the same records on the next cadence. */
+        autosaveTrace_record(AUTOSAVE_TRACE_STAGE_TRACE_SUPPRESSED,
+                             AUTOSAVE_TRACE_TRACE_SUPPRESSED_FLAG_APPEND_ERROR,
+                             0u);
+        fs_trace_suppress_witness = 1u;
+    }
 #endif
     filesystem_ack();
 }

### EVID-6 - filesystem.c: G dropped-count publication

What it does:

In filesystem_autosaveTraceFlushCompleted(), after a successful append, emits
one G record whenever autosaveTrace_droppedCount() differs from the last
reported value, and updates the last-reported latch.

Why it must exist:

autosaveTrace_droppedCount() currently has no reader, so ring overflow is
silently invisible. A nonzero G directly documents producer records that were
overwritten before they could be appended; with the 2048-record ring and a
single Scene Load it must stay zero, making any nonzero value decisive.

Inputs: autosaveTrace_droppedCount(), fs_trace_reported_dropped, the append's
terminal status.

Outputs: at most one G record per changed dropped value; value = the dropped
count.

Exact diff (same function as EVID-5(b), inside the DONE branch):

     if (status == FS_STATUS_DONE) {
         /* Re-arm immediately after a successful full-batch append while
          * backlog remains. */
         if (autosaveTrace_pendingCount() >=
             AUTOSAVE_TRACE_FLUSH_BATCH_RECORDS)
             fs_autosave_trace_next_due_tick = 0u;
+        /*
+         * Publish ring-overflow evidence once per changed value. The 2048
+         * ring must never drop records for one Scene Load, so any nonzero G
+         * directly documents lost producer records.
+         */
+        {
+            uint16_t dropped = autosaveTrace_droppedCount();
+            if (dropped != fs_trace_reported_dropped) {
+                autosaveTrace_record(AUTOSAVE_TRACE_STAGE_TRACE_DROPPED,
+                                     0u, (uint32_t)dropped);
+                fs_trace_reported_dropped = dropped;
+            }
+        }
         fs_trace_suppress_witness = 0u;
     } else if (status == FS_STATUS_ERROR && !fs_trace_suppress_witness) {

### DOC-1 - DEV_MODES.md: document the evidence stages

File: knowledge_files/specification_reference/DEV_MODES.md
Location: the /asavetrc.bin stage paragraph ("Each record is eight bytes ...").

What it does:

Extends the stage documentation with R/W/F/G and their flag/value layouts.

Why it must exist:

DEV_MODES.md owns the trace stage vocabulary; a decoder must be able to
interpret the new records without reading this plan.

Exact edit: append after the existing L (LOAD_MARK) description:

    R is the root Scene Load completion witness: emitted before the Preset
    marker loop; flags bit 0 = filesystem status DONE observed; value = the
    destination Scene mask the callback will iterate. W documents the designed
    writer suppression: flags bit 0 = canonical mask dirty; value = the armed
    debounce deadline. F documents the trace flush gates: bit 0 = command-active
    decline (value = pending count); bit 1 = append ERROR. G documents ring
    overflow: value = autosaveTrace_droppedCount() at the last change.

## 6. Ordering and build phases

FIX-A and FIX-B are independent; EVID-1 must precede EVID-2 through EVID-6
because they name the new stages. DOC-1 is documentation only.

1. Apply EVID-1 (stages) first, then FIX-A, FIX-B1/B2/B3, then
   EVID-2/3/4/5/6.
2. Build with DEV_MODE_LOGGING=1: confirm the new stages compile, no warnings,
   and .bss grows by exactly 6 bytes (FIX-B1 2 bytes + EVID-3 4 bytes).
3. Build with DEV_MODE_LOGGING=0: confirm no autosave_trace_records symbol, no
   new evidence statics, and only the +2-byte FIX-B1 allocation remains.
4. Apply DOC-1 and archive this plan.

## 7. Verification matrix and evidence interpretation

### 7.1 Test A - Scene Load, play stopped

1. Boot, enter Load:[Scene   ], select destination Scene 15, load a known
   source Scene (slot 009 Forest).
2. Wait for the busy display to clear, then wait a few seconds without leaving
   the Load page.
3. Copy the card and check the trace only. Expected tail, in order:
   - R, flags=0x01 (status DONE), value=0x8000 (destination mask);
   - Scene-15 scene-parameter D records and the ~456-byte Scene-15
     Kit/Instrument D burst;
   - six I summaries with flags=0x07 and expected==accepted;
   - L kind=0 Scene15 (Kit), L kind=1 Scene15 (Scene);
   - S (writer armed) and F with bit 0 set (trace flush suppressed by the
     active command, value = pending count), then W with bit 0 set (writer
     armed-with-dirty, suppressed by the Load page, value = deadline).
   - G must be absent (dropped count 0).
   - No A is expected at this point: the writer suppression is by design.
   - /.hcnames Scene row 16 is already Forest/009; rows 32 and 123-128 are
     still old (session not yet exited).
4. Leave the Load page (one mode-button exit) and wait at least six seconds
   for the writer debounce and the session-exit HCNAMES write.
5. Copy the card again and check:
   - /asavetrc.bin tail gained the writer transaction A/V/M/C/P/T with C
     equal to the unique dirty-byte count.
   - /.hcprms winner generation is exactly one higher and Scene 15's parameter
     bytes equal the loaded Scene's values (name fields stay old by design;
     names are HCNAMES authority).
   - /.hcnames rows 32 and 123-128 are Forest and forestd1..foresth1 with
     source -.

### 7.2 Test B - Scene Load with play active

Repeat Test A with the sequencer running. The apply must finalize within
roughly one second past the first non-quiet pass, the busy display must clear,
and the same outcomes must appear. W is expected to record the longer
page-suppressed window.

### 7.3 Evidence decision table - trace publish failure

| Observed in /asavetrc.bin | Conclusion | Next step |
| --- | --- | --- |
| No R | on_scene_load_complete() never ran | investigate the completion chain after the UPDATE_HCNAMES_SCENE handoff (filesystem_complete -> completion_callback) |
| R with flags=0x00 | callback ran with a non-DONE status | inspect the HCNAMES flush/ack path feeding filesystem_complete |
| R with value=0x0000 | destination mask empty | inspect Menu's scene-mask selection/OK path |
| R present, L kind=SCENE absent | marking loop not entered | inspect the status/mask gate and autosave_scenePayloadBase |
| R+D/I+L present but F absent and no flush output | flush never attempted | inspect the idle-facade/cadence path |
| F bit 0 present with no later records | command never finalized | FIX-B did not bound the apply; inspect menu_finishSoundApply / index restore |
| F bit 1 present | append reached ERROR | SD/FAT append failure; preserve the card |
| G nonzero | ring overflow lost records | enlarge/verify the 2048-ring math or reduce burst size |

### 7.4 Evidence decision table - autosave mutation register publish failure

| Observed in /asavetrc.bin | Conclusion | Next step |
| --- | --- | --- |
| No D/I/L for Scene 15 | producer never marked | resolve the trace-publish table first |
| I flags lack TRACKING_ENABLED | mutation tracking was off | inspect filesystem_autosaveSetupCompleted / enable timing |
| S absent | writer never armed | dirty mask empty or scheduler never saw it |
| S present, W present, no A while on page | designed suppression | leave the page and re-check (expected) |
| S present, W absent, no A after leaving page | admission failed for another reason | inspect filesystem_start() return, facade state, deadline math |
| A present, M flags=0x00 | empty merged mask | producer marked nothing durable; revisit I/L flags |
| A..C present, no P/T | transform failed mid-flight | existing V/M/C/P/T flags localize the phase |
| P/T present but no generation change on card | publication lost | CRC/commit/flush failure; preserve both records |

### 7.5 Regressions

- Kit Load: unchanged trace, writer, and exit-time HCNAMES behavior; the new
  W/F/G records may appear only if the corresponding gates actually decline.
- Scene Save: unchanged; it never enters the name-session accumulation.
- Bank Load: untouched; no Bank code is referenced.
- Writer suppression on Load/Save pages: unchanged by design.

## 8. Risks

| # | Severity | Risk | Mitigation |
| --- | --- | --- | --- |
| R1 | Medium | FIX-B3 force-commits a ringing voice, which can audibly click, the same tradeoff the trigger path already accepts. | Bound is one second and only fires when a voice never goes quiet. |
| R2 | Low | Kit/Instrument HCNAMES rows commit only at session exit, and one session shares the single identity block across accumulated destinations. | Pre-existing Kit-family semantics; the verification uses one load per session. |
| R3 | Low | W/F evidence records could flood the ring if a latch is missed. | Each witness is edge-latched in EVID-3; G is change-latched. |
| R4 | Low | New stage codes could collide with future work. | R/W/F/G are reserved by this plan and documented in DOC-1; Q stays retired. |
| R5 | Info | The evidence stages are diagnostic; if they must be retired later, only EVID-1/2/3/4/5/6 and DOC-1 are removed. | Keep the retirement boundary documented in the session handoff. |

## 9. Close-out

After Tests A/B pass and the decision tables produce a clean run:

- Update knowledge_files/specification_reference/SRAM_MANIFEST.md with the
  2-byte drumset_apply_stall_ticks allocation and, if a logging-on snapshot is
  taken, the 4-byte logging-only evidence latch allocation.
- Keep DEV_MODES.md's stage list in sync (DOC-1).
- Archive this plan with the Session 050/051 handoff notes.
- Keep the temporary 2048-record ring per TRACE_EXTENSION.md until that
  experiment's own revert step.

## 10. Definitions of done

- A root Scene Load accumulates its destination in the name session, so the
  existing exit-time writer registers the Kit plus six Instrument rows.
- A root Scene Load reports R, the D/I burst, L kind=KIT, L kind=SCENE, S, and
  the appropriate F/W evidence records; G remains zero.
- After the user leaves the Load page, the armed dirty bits drain and
  /.hcprms advances one generation with the loaded Scene's parameter values,
  including when the load was performed while play was active.
- The writer's Load/Save-page suppression is unchanged and documented by W.
- Logging-off builds contain no evidence stages, no evidence latches, no ring,
  and retain only the approved 2-byte FIX-B1 counter.
