# Session 052 Implementation Schedule - Bank Load Persistence and Verification

**Project**: LXR-02 firmware port (STM32F765VIH6)
**Branch/state**: dev-ph3-autosave-ph2, intentional dirty worktree after the Session 051 Scene-follow-up close.
**Scope**: Bank Load persistence only. Scene HCNAMES work from Session 051 is done unless this schedule proves it implicated.
**Document role**: This is the full implementation schedule for Session 052. It supersedes the prior pre-plan and contains the code deep-dive, every code change, the exact comment blocks to add, the host validator contract, the hardware fixture matrix, and the documentation close-out. No code is changed while this document is written; this document is the only file edited in this turn.

---

## 1. Session objective and acceptance boundary

### Session 052 progress notes

- 2026-08-17: Read `MEMORY.md`, the Session 051 handoff, and this schedule.
  The worktree is intentionally dirty from Session 051; no unrelated changes
  will be rewritten.
- 2026-08-17: Read-only verification of the local `SD_CARD` fixture confirms
  both `.hcprms1` and `.hcprms2` contain Bank identity bytes at absolute
  offsets 3920..3934 (`08 00`, `Full`, zero mask, active Scene `06`, voice
  mask `0040`). Scene payload bytes are present after offset 4048. There is no
  local `asavetrc.bin`, so the commit-versus-drain observation still requires
  the new B witness on hardware.
- 2026-08-17: Implementation begins with the definitive settings marks,
  B-stage witness/decoder, and host validator. Candidate present-mask
  semantics remain explicitly pending the witness; no blind capture/copy
  rewrite is authorized.
- 2026-08-17: The source correction is now Candidate B: `bank_setScenePresentMask`
  reports whether it changed the normalized value, and Bank Load explicitly
  re-marks the two present-mask bytes only when that setter is a no-op. This
  preserves the change-aware owner contract while refreshing a stale/zero
  record after an accepted Load. Bank Load/Save also mark the existing
  debounced settings writer after restore-slot commit. The B commit/drain
  witness remains in place for hardware confirmation.
- 2026-08-17: Host Python syntax and validator checks pass; the current
  fixture still fails only the two pre-fix durable fields, as expected because
  it was captured before this firmware change. `make` cannot run in this
  environment because `arm-none-eabi-gcc` is not installed.
- 2026-08-17: The validator now follows the firmware's wrapping generation
  comparison, validates CRC32C/header/commit state, checks the full 129-row
  HCNAMES projection, settings, Bank metadata, child mask, and bounded sampled
  Scene/Kit/Instrument payloads. Its expected pre-fix result remains the same
  two durable-field failures above.

Make a successful Bank Load durable in exactly two observable ways:

1. settings.cfg re-serializes active_bank from the committed Bank restore slot, so a reboot restores the newly loaded Bank rather than a stale slot.
2. The AutoSave hidden record's Bank scene_present_mask equals the effective selected-child union, so a later reader interprets every captured Scene payload as present.

In scope:

- settings persistence after Bank Load (and the symmetric Bank Save gap);
- the present-mask capture diagnosis and its smallest correction;
- a logging-only witness using the existing trace ring (no new RAM);
- the host validator tools/verify_bank_autosave.py;
- documentation reconciliation.

Out of scope (unchanged from Session 051):

- Scene Load HCNAMES (done);
- InstrumentMrp kit restore (done);
- recursive overwrite delete;
- runtime Bank Load active-Scene preservation;
- DSP debt;
- Pattern persistence.

---

## 2. Evidence summary from the Session 051 card audit

The audit loaded Bank 008 "Full", then exited to voice mode. Temp and trace files were deleted before the test, so the durable evidence is .hcnames, .hcprms1, .hcprms2, settings.cfg, and the Bank/ source tree.

### 2.1 Fixture

- Bank 008 Full has sixteen children 00..15, all present.
- bankset.bcg: active_scene=6, scene_mask_voice_edit=0x0040.
- Scene names: 00..02 Slak, 03 Full, 04..15 Slak.
- Embedded Kit names per child, in order: Brezel, Forest, Barf, FilMod, Emott, SoyEared, DocWire, Machine, KitWool, FatMed, Beatmstr, Organity, Goa, Snap, Electro, Brezel.

### 2.2 What landed (correct, leave alone)

- .hcnames is fully correct: Bank row 0 = Full 008; Scene rows 1..16 match the children; Kit rows 17..32 match the embedded Kit names; Instrument rows 33..128 match each child's kitset members. No HCNAMES change is required.
- .hcprms1 generation 5 is the winner; .hcprms2 generation 4 is the peer. Both records carry the same Bank identity: restore slot = 8, Bank name = Full, active scene = 6, voice edit mask = 0x0040. A sampled Scene payload matches the child sceneset.scg byte-for-byte.

### 2.3 What did not land (the two targets)

1. settings.cfg still contains active_bank=12. The Bank Load updated the resident restore slot (proved by the AutoSave record) but never marked the settings file dirty.
2. The AutoSave Bank scene_present_mask in both records is 0x0000 even though Bank 008 contains sixteen children; expected 0xFFFF.

---

## 3. Code deep-dive

The sections below derive behavior from the source, not from the specification documents. The specification references (AUTOSAVE.md, FILESYSTEM_SPEC.md, MODULE_INTERCHANGE_SPEC.md, SRAM_MANIFEST.md) are treated as "where it is", not "how it works"; every conclusion here is anchored to a line in the source.

### 3.1 Ownership and function index

| Function | File:line | Role |
| --- | --- | --- |
| bank_init() | Core/Bank/BankData.c:99 | Seeds present mask = 1, restore slot = 0, no resident Bank |
| bank_setRestoreBankSlot() | Core/Bank/BankData.c:139 | Stores 0..999 slot, marks restore-slot field on change |
| bank_setScenePresentMask() | Core/Bank/BankData.c:165 | Stores 16-bit mask, marks present-mask field on change |
| bank_scenePresentMask() | Core/Bank/BankData.c:188 | Live getter |
| bank_scenePresent() | Core/Bank/BankData.c:193 | Per-Scene bit test; gates Scene payload capture |
| filesystem_loadBankDirectory_tick() | Core/Hardware/SD/filesystem.c:9815 | Bank Load state machine |
| filesystem_requestLoadBank() | Core/Hardware/SD/filesystem.c:20994 | Validates and starts Bank Load |
| filesystem_requestSaveBank() | Core/Hardware/SD/filesystem.c:21048 | Validates and starts Bank Save |
| filesystem_markSettingsDirty() | Core/Hardware/SD/filesystem.c:19013 | Sets settings dirty + 1 s debounce deadline |
| filesystem_nextSettingsLine() | Core/Hardware/SD/filesystem.c:12258 | Serializes active_bank from bank_restoreBankSlot() at line index 2 |
| autosave_markBankFieldDirty() | Core/Bank/Scene/Autosave.c:896 | Maps Bank field enum to payload offsets |
| autosave_getLivePayloadByte() | Core/Bank/Scene/Autosave.c:654 | Live payload getter; present mask at offsets 10..11 |
| autosave_markResidentBankDirty() | Core/Bank/Scene/Autosave.c:1338 | Full Bank snapshot at setup/re-enable |
| autosave_transformDrainChunk() | Core/Bank/Scene/Autosave.c:1509 | Applies captured patches into the copied record |
| filesystem_autosaveSetupCompleted() | Core/Hardware/SD/filesystem.c:19425 | Enables tracking + marks resident Bank dirty |
| filesystem_autosaveWriterSchedule_tick() | Core/Hardware/SD/filesystem.c:19498 | Arms/debounces the drain |
| Bank Load empty commit | Core/Hardware/SD/filesystem.c:10281-10312 | Empty Bank metadata commit |
| Bank Load non-empty commit | Core/Hardware/SD/filesystem.c:10390-10422 | Non-empty Bank metadata commit |
| Bank Save metadata commit | Core/Hardware/SD/filesystem.c:13855-13870 | Bank Save metadata commit |
| Drain capture | Core/Hardware/SD/filesystem.c:5701-5731 | Phase 56: take bit + capture live byte |

### 3.2 AutoSave wire geometry (authoritative for the present-mask field)

From Core/Bank/Scene/Autosave.h:

- Header = 64 bytes, mutation mask = 3,856 bytes, payload = 30,848 bytes.
- AUTOSAVE_PAYLOAD_OFFSET = 64 + 3856 = 3920.
- Bank section is payload-relative 0..127, absolute 3920..4047:
  - restore slot: payload 0..1, absolute 3920..3921;
  - name: payload 2..9, absolute 3922..3929;
  - scene present mask: payload 10..11, absolute 3930..3931;
  - active scene: payload 12, absolute 3932;
  - voice edit mask: payload 13..14, absolute 3933..3934.
- Scene 0 region begins at payload 128, absolute 4048.

The marker (autosave_markBankFieldDirty() case AUTOSAVE_BANK_FIELD_SCENE_PRESENT_MASK) marks payload offsets 10..11. The getter (autosave_getLivePayloadByte()) reads the same offsets. The drain patch (autosave_transformDrainChunk()) writes patch values at AUTOSAVE_PAYLOAD_OFFSET + patch_offsets[cursor]. All three agree, so the field is not simply mis-addressed in firmware.

### 3.3 Bank Load commit sequence

The effective mask is established in phase 17:

    op_bank_scene_load_mask =
        (uint16_t)(op_bank_scene_load_mask & op_bank_child_present_mask);

filesystem_requestLoadBank() seeds op_bank_scene_load_mask with the caller's validated 16-bit request mask (Core/Hardware/SD/filesystem.c:21030). The discovered child mask op_bank_child_present_mask is built in phase 15. The child payload loop (phases 20..31) does not mutate op_bank_scene_load_mask; it only advances op_bank_child_cursor. Therefore the effective selected-child mask is still intact at commit time.

Non-empty commit (phase 20 fall-through, lines 10390..10422):

    bank_setDisplayName(op_bank_display_name);
    (void)filesystem_setResidentSource(FS_IDENTITY_BANK_ROW, op_slot);
    bank_setScenePresentMask((uint16_t)(bank_scenePresentMask() |
                                        op_bank_scene_load_mask));   /* 10413 */
    bank_selectActiveSceneForEditMask(op_bank_active_scene);
    bank_setSceneMaskVoiceEdit(op_bankset_state.scene_mask_voice_edit);
    bank_setRestoreBankSlot(op_slot);                                /* 10417 */
    bank_setHasResidentBank(1u);
    scene_selectActive(op_bank_active_scene);
    memcpy(preset_currentName, op_bank_display_name, 8u);
    filesystem_cacheResidentName(0u, op_bank_display_name);
    filesystem_bootLoggingSetDetail("BKHCWRIT");
    op_phase = 83u;

Empty commit (phase 17 empty branch, lines 10281..10312):

    bank_setDisplayName(op_bank_display_name);
    (void)filesystem_setResidentSource(0u, op_slot);
    bank_setScenePresentMask(bank_scenePresentMask());               /* 10302 */
    bank_selectActiveSceneForEditMask(op_bank_active_scene);
    bank_setSceneMaskVoiceEdit(op_bankset_state.scene_mask_voice_edit);
    bank_setRestoreBankSlot(op_slot);                                /* 10305 */
    bank_setHasResidentBank(1u);
    memcpy(preset_currentName, op_bank_display_name, 8u);
    if (!afatfs_chdir(NULL))
        return;
    filesystem_cacheResidentName(0u, op_bank_display_name);
    filesystem_bootLoggingSetDetail("BKHCWRIT");
    op_phase = 83u;

The empty-branch bank_setScenePresentMask(bank_scenePresentMask()) is a deliberate no-op that preserves the retained value (from bank_init() = 1). It must not be mistaken for a clearing write.

### 3.4 Settings writer and active_bank

filesystem_nextSettingsLine() line index 2 serializes:

    filesystem_formatAssignmentU16Line(dst, cap, "active_bank",
                                       bank_restoreBankSlot());

The value is read live at write time, so once bank_setRestoreBankSlot(op_slot) has run and fs_settings_dirty is set, the next FS_INTERNAL_OP_SAVE_GLOBALS operation writes the correct slot.

filesystem_markSettingsDirty() (line 19013) only sets fs_settings_dirty = 1u, bumps fs_settings_change_revision, and restarts the SETTINGS_AUTOWRITE_DEBOUNCE_MS (1000 ms) deadline. It opens no file and is safe to call while the facade is busy; the actual write is admitted later by filesystem_settingsWriterSchedule_tick() only when the facade is idle and the runtime gate is open.

The writer gate fs_settings_runtime_ready is opened by filesystem_enableRuntimeSettingsWrites() at the end of boot (main.c calls it after the blocking Bank-or-fallback ladder). A dirty mark taken during boot is therefore harmless: it stays latched and is written once runtime writes are authorized.

### 3.5 AutoSave drain lifecycle

After setup succeeds, filesystem_autosaveSetupCompleted() calls autosave_setMutationTrackingEnabled(1u) then autosave_markResidentBankDirty(). That helper loops every AUTOSAVE_BANK_FIELD_* (including the present-mask field) and every present Scene's non-Pattern scope, so a complete snapshot is dirty once tracking is live.

The drain's phase 56 (Core/Hardware/SD/filesystem.c:5701) scans payload offsets in ascending order. For each dirty bit it:

1. atomically takes the bit (autosave_maskBitTake());
2. captures the live byte via autosave_getLivePayloadByte();
3. records a patch (offset, value) when the get succeeds.

The copy pass (autosave_transformDrainChunk()) then writes the captured patch values into the copied peer record. The present-mask field is captured at offsets 10..11 and Scene payloads begin at offset 128, so both are within the first 256 offsets of the very first classification tick.

Relevant timing constants (config.h):

- AUTOSAVE_WRITER_INTERVAL_MS = 5000 (first drain debounce);
- AUTOSAVE_WRITER_CONTINUATION_INTERVAL_MS = 250 (continuation);
- SETTINGS_AUTOWRITE_DEBOUNCE_MS = 1000;
- AUTOSAVE_PARAMETER_GETS_PER_WRITE = 1536 (patches per copy pass);
- AUTOSAVE_MASK_BITS_PER_TICK = 256 (offsets classified per tick).

### 3.6 Bank Save metadata commit (symmetric gap)

Bank Save phase 45 (Core/Hardware/SD/filesystem.c:13855) commits:

    bank_setDisplayName(op_bank_display_name);
    bank_setScenePresentMask(op_bank_scene_save_mask);              /* 13861 */
    bank_selectActiveSceneForEditMask(op_bank_active_scene);
    bank_setSceneMaskVoiceEdit(op_bankset_state.scene_mask_voice_edit);
    bank_setRestoreBankSlot(op_slot);                               /* 13864 */
    bank_setHasResidentBank(1u);

It calls bank_setRestoreBankSlot(op_slot) but never filesystem_markSettingsDirty(). This is the same active_bank persistence gap on the save side and is corrected in the same scope.

Two further observations about line 13861 are recorded here because they bear on the present-mask diagnosis:

- It is a direct overwrite (bank_setScenePresentMask(op_bank_scene_save_mask)), not the union used by the load path. This is the only writer in the source tree that can clear present bits.
- If a future Bank Save were issued with a zero or partial save mask, it would shrink or zero the resident present mask. That is a distinct save-path concern, but it is the only static mechanism that can ever make the resident mask zero after bank_init() seeded it to 1.

### 3.7 Boot ladder (context for the diagnosis)

main.c calls bank_init() before any SD work (line 437), loads settings.cfg early (line 536), then loads the bank_restoreBankSlot() Bank with an all-Scenes mask through preset_loadBank(boot_bank_slot, 0xffffu) (line 806). That request reaches the same filesystem_loadBankDirectory_tick(), so the boot Bank Load also exercises the metadata commit. Mutation tracking is still disabled during boot; the resident values are set but their marks are ignored until runtime setup runs autosave_markResidentBankDirty().

---

## 4. Root-cause analysis

### 4.1 settings.cfg gap - confirmed, no further diagnosis needed

The Bank Load metadata commit updates the resident restore slot through bank_setRestoreBankSlot(op_slot) but never calls filesystem_markSettingsDirty(). The one-second debounced settings writer therefore never runs and settings.cfg keeps the old active_bank. Bank Save has the same omission. The fix is mechanical and is specified in Changes 1 and 2 below.

### 4.2 present-mask gap - diagnostic first, then the smallest correction

Static analysis constrains the possible causes and exposes one inconsistency that must be resolved on hardware before choosing a correction.

Established facts from the source:

1. bank_init() seeds the present mask to 1; the load commit unions the effective mask into it; the empty-branch commit preserves it. The only writer that can clear or zero it is the Bank Save direct overwrite (line 13861). Therefore, absent a zero-mask Bank Save, the resident mask should be nonzero for the entire session.
2. The marker, getter, and patch-application offsets for the present-mask field all agree (payload 10..11, absolute 3930..3931).
3. The present-mask field and Scene 0 payload (offset 128+) are classified in the same first drain tick, and Scene payload capture is gated on bank_scenePresent(). A record that simultaneously shows scene_present_mask = 0x0000 and a captured Scene 0 payload is internally inconsistent with this code unless the two fields were captured across different drain passes while the mask changed between them - which cannot happen during a single drain because no other filesystem operation can own the facade at the same time.

Consequence: before writing any present-mask fix, re-verify the raw record bytes (Step 0 in Section 5.1). The two audit observations may be reconciled by a misread offset rather than a firmware defect. The logging witness (Changes 3 and 4) then disambiguates the remaining cases.

Ranked correction decision tree (apply exactly one, after the witness):

- A. The raw dump already shows a nonzero present mask. No firmware change for the mask; only the settings fix and documentation apply.
- B. Commit-site witness shows a zero resident mask. The mask was already zero before/at commit. Apply Candidate E (Bank Save union) and re-audit for any other clearing write; there are none in static code.
- C. Commit-site shows nonzero, drain-site shows zero. The mask was reset between commit and drain. The only reset path is Bank Save; apply Candidate E and re-run.
- D. Commit-site and drain-site both show nonzero, yet the record reads zero. The defect is in capture/copy. Re-verify autosave_u16Byte(), autosave_transformDrainChunk() patch offsets, and the raw dump offset; do not change semantics before the byte trace isolates the loss.
- E. The drain-site witness never fires. The present-mask field was never dirty. Verify autosave_markResidentBankDirty() ran at setup and that the Bank Load setter actually changed the value (it may have been a no-op because boot already set the same value). If the no-op is the cause, add an explicit present-mask mark at Bank Load completion (Candidate B code below).

---

## 5. Implementation schedule

The order is: Step 0 (read-only re-verification), then Changes 1 and 2 (the definitive settings fix), then Changes 3 and 4 (the logging witness), then rebuild and run the fixture, then select one Candidate correction, then the host validator and documentation.

### Step 0 - Re-verify the raw AutoSave Bank bytes (read-only, no code change)

Before any present-mask firmware change, dump the winning record and confirm the absolute Bank section:

    offset 3920..3921  restore slot (LE, expected 0x08 0x00)
    offset 3922..3929  name (8 bytes, expected "Full    ")
    offset 3930..3931  scene present mask (LE, this is the disputed field)
    offset 3932        active scene (expected 0x06)
    offset 3933..3934  voice edit mask (LE, expected 0x40 0x00)
    offset 4048..      Scene 0 payload start

The host validator (Change 7) performs this dump and comparison automatically, so Step 0 can be executed by running the validator before the firmware witness is built.

### Change 0 - Add the B trace stage (no new RAM)

File: Core/Bank/Scene/AutosaveTrace.h.

Add one enum member after AUTOSAVE_TRACE_STAGE_TRACE_DROPPED:

    /*
     * B: Bank present-mask lifecycle witness for the Session 052 persistence
     * investigation. One retained RAM-only trace point is emitted at the Bank
     * Load metadata commit and at the writer drain's present-mask capture.
     * flags bit 0 selects the site (0 = commit, 1 = drain); value32 packs the
     * resident present mask in bits 16..31 and site-specific data in bits
     * 0..15. Why: the scalar D records prove only which payload offsets were
     * marked, not the mask values at the commit/capture boundary, so a
     * zero-captured present mask cannot otherwise be localized to a stale
     * value, a missed mark, or a consumed bit.
     */
    AUTOSAVE_TRACE_STAGE_BANK_PRESENT = 'B',

Add the flag and value layout macros beside the other stage layout macros:

    /* B flags: bit 0 selects the writer-drain capture site; clear means the Bank
     * Load metadata commit site. */
    #define AUTOSAVE_TRACE_BANK_PRESENT_FLAG_DRAIN (1u << 0u)
    /* B value32 layout: bits 16..31 are the resident Bank present mask at the
     * observation point; at the commit site bits 0..15 are the effective
     * selected-child load mask, and at the drain site bits 0..15 are the payload
     * offset of the present-mask field's first byte (10). */
    #define AUTOSAVE_TRACE_BANK_PRESENT_MASK_SHIFT 16u

This reuses the existing eight-byte trace record and the existing ring. It adds no retained RAM and is compiled to nothing when DEV_MODE_LOGGING is 0.

### Change 1 - Persist settings.cfg after a successful Bank Load

File: Core/Hardware/SD/filesystem.c, function filesystem_loadBankDirectory_tick().

Insert one call in both metadata commit branches, immediately after bank_setRestoreBankSlot(op_slot);.

#### 1a. Empty Bank commit (after line 10305)

    bank_setRestoreBankSlot(op_slot);
    /*
     * Persist the newly selected boot-restore Bank after a valid
     * empty-Bank identity load.
     *
     * What: mark the autonomous settings writer dirty so the next
     * debounced settings.cfg write re-serializes active_bank from
     * bank_restoreBankSlot(). Inputs: the committed restore slot just
     * stored above. Outputs: fs_settings_dirty, the change revision,
     * and a fresh one-second deadline; no file is opened here. Why: an
     * empty Bank is still the new boot-selection authority, so
     * settings.cfg must not retain a stale active_bank after a later
     * reboot. The debounce coalesces this mark with any simultaneous
     * Global edit instead of writing per keystroke. Affiliates:
     * filesystem_markSettingsDirty(),
     * filesystem_settingsWriterSchedule_tick(), and
     * filesystem_nextSettingsLine()'s active_bank case.
     */
    filesystem_markSettingsDirty();

#### 1b. Non-empty Bank commit (after line 10417)

    bank_setRestoreBankSlot(op_slot);
    /*
     * Persist the newly selected boot-restore Bank after a complete
     * non-empty Bank Load.
     *
     * What: mark the autonomous settings writer dirty so active_bank
     * follows the just-committed restore slot. Inputs: the committed
     * restore slot above. Outputs: dirty/revision/deadline state only;
     * no file is opened by this call. Why: the Bank Load updated the
     * resident restore slot, which is the sole authority the settings
     * writer reads for active_bank; without this mark the writer never
     * runs and a reboot restores a stale Bank. Affiliates:
     * filesystem_markSettingsDirty(), the settings writer scheduler, and
     * filesystem_nextSettingsLine().
     */
    filesystem_markSettingsDirty();

Why this placement and not a new function: both branches already converge on the same bank_setRestoreBankSlot(op_slot) authority, so the mark belongs directly beside it. No new accessor or callback is introduced; the existing filesystem_markSettingsDirty() already owns the debounce and the scheduler.

Boot note: this mark is also taken during the boot Bank Load while fs_settings_runtime_ready is still clear. That latches one idempotent write after boot, which reconciles settings.cfg if the boot Bank differs from the stored slot, and is otherwise a no-op value. If the user prefers zero boot-time settings writes, gate the call on fs_settings_runtime_ready instead; the default in this schedule is the unconditional mark so both paths share one authority.

### Change 2 - Persist settings.cfg after a successful Bank Save

File: Core/Hardware/SD/filesystem.c, Bank Save phase 45, after line 13864.

    bank_setRestoreBankSlot(op_slot);
    /*
     * Persist the boot-restore Bank selected by a successful Bank Save.
     *
     * What: mark settings dirty so the next settings.cfg write re-emits
     * active_bank. Inputs: the committed restore slot above. Outputs:
     * dirty/revision/deadline state only; no file is opened here. Why:
     * Bank Save changes the boot-selection authority exactly as Bank
     * Load does, so the same symmetric mark is required; this closes the
     * gap that would otherwise leave active_bank stale after
     * Save:[Bank]. Affiliates: filesystem_markSettingsDirty(), the
     * settings writer scheduler, and filesystem_nextSettingsLine().
     */
    filesystem_markSettingsDirty();

This is a separate, same-scope correction as the pre-plan required it to be; it is not folded into Change 1 because the two functions live in different state machines.

### Change 3 - Commit-site present-mask witness

File: Core/Hardware/SD/filesystem.c, function filesystem_loadBankDirectory_tick().

Insert one autosaveTrace_record() call in each commit branch, immediately after bank_setScenePresentMask(...).

#### 3a. Empty Bank commit (after line 10302)

    bank_setScenePresentMask(bank_scenePresentMask());
    /*
     * Witness the empty-Bank present boundary in the retained trace.
     *
     * Inputs: the just-preserved resident mask and the (zero) effective
     * load mask. Output: one B record with the drain site bit clear;
     * value32 packs the resident mask in bits 16..31 and the effective
     * mask in bits 0..15. Why: the empty branch is a deliberate no-op
     * setter, so this record proves the preserved mask value for
     * comparison with the non-empty branch and with the drain-site
     * witness. Affiliates: AutosaveTrace B stage and
     * autosave_getLivePayloadByte().
     */
    autosaveTrace_record(
        AUTOSAVE_TRACE_STAGE_BANK_PRESENT, 0u,
        ((uint32_t)bank_scenePresentMask() <<
         AUTOSAVE_TRACE_BANK_PRESENT_MASK_SHIFT) |
            op_bank_scene_load_mask);

#### 3b. Non-empty Bank commit (after line 10413)

    bank_setScenePresentMask((uint16_t)(bank_scenePresentMask() |
                                        op_bank_scene_load_mask));
    /*
     * Witness the post-commit Bank presence boundary in the retained
     * trace.
     *
     * Inputs: the just-stored resident mask and the effective
     * selected-child mask. Output: one B record with the drain site bit
     * clear; value32 packs the resident mask in bits 16..31 and the
     * effective load mask in bits 0..15. Why: the D records emitted by
     * the setter prove only that offsets 10..11 were marked; this record
     * proves the exact merged mask value so a later zero-captured present
     * mask can be localized to the capture side rather than the commit
     * side. Affiliates: AutosaveTrace B stage and the drain-site witness.
     */
    autosaveTrace_record(
        AUTOSAVE_TRACE_STAGE_BANK_PRESENT, 0u,
        ((uint32_t)bank_scenePresentMask() <<
         AUTOSAVE_TRACE_BANK_PRESENT_MASK_SHIFT) |
            op_bank_scene_load_mask);

### Change 4 - Drain-site present-mask witness

File: Core/Bank/Scene/Autosave.c, function autosave_getLivePayloadByte(), inside the payload_offset >= 10u && payload_offset < 12u branch.

    if (payload_offset >= 10u && payload_offset < 12u) {
        bank_value = bank_scenePresentMask();
        /*
         * Witness the live present-mask value exactly when the drain
         * captures the field's first byte.
         *
         * Inputs: payload_offset and the resident mask. Output: one B
         * record with the drain site bit set; value32 packs the resident
         * mask in bits 16..31 and payload_offset in bits 0..15. Why: this
         * is the single point where the retained mask becomes record
         * bytes, so it proves whether a zero-captured value was already
         * zero in SRAM or was lost later in the patch/copy path. The
         * comparison only fires for the first byte of the two-byte field,
         * avoiding a duplicate record per capture pass. Affiliates: the
         * commit-site witness and filesystem's patch application.
         */
        if (payload_offset ==
            (AUTOSAVE_BANK_SCENE_PRESENT_MASK_OFFSET -
             AUTOSAVE_PAYLOAD_OFFSET)) {
            autosaveTrace_record(
                AUTOSAVE_TRACE_STAGE_BANK_PRESENT,
                AUTOSAVE_TRACE_BANK_PRESENT_FLAG_DRAIN,
                ((uint32_t)bank_value <<
                 AUTOSAVE_TRACE_BANK_PRESENT_MASK_SHIFT) |
                    payload_offset);
        }
        *value = autosave_u16Byte(
            bank_value, (uint8_t)(payload_offset - 10u));
        return 1u;
    }

The condition AUTOSAVE_BANK_SCENE_PRESENT_MASK_OFFSET - AUTOSAVE_PAYLOAD_OFFSET is a compile-time constant equal to 10; it is written with the named macros so the witness cannot drift from the wire geometry.

### Change 5 - Decode the B stage

File: tools/decode_devlogs.py.

Add the stage to the two dictionaries:

    STAGE_ENUM = {
        ...
        "G": "AUTOSAVE_TRACE_STAGE_TRACE_DROPPED",
        "B": "AUTOSAVE_TRACE_STAGE_BANK_PRESENT",
    }

    STAGE_PRODUCER = {
        ...
        "G": "filesystem_autosaveTraceFlushCompleted()",
        "B": "filesystem_loadBankDirectory_tick() / autosave_getLivePayloadByte()",
    }

Add a decode branch in trace_record_text():

    elif ch == "B":
        drain = bool(flags & 0x01)
        resident = (value >> 16) & 0xFFFF
        low = value & 0xFFFF
        if drain:
            detail = (f"{enum_name} via {producer}: drain capture; "
                      f"resident present mask 0x{resident:04x} "
                      f"{scene_mask_text(resident)}, "
                      f"payload offset {low}")
        else:
            detail = (f"{enum_name} via {producer}: Bank Load commit; "
                      f"resident present mask 0x{resident:04x} "
                      f"{scene_mask_text(resident)}, "
                      f"effective load mask 0x{low:04x} "
                      f"{scene_mask_text(low)}")

### Change 6 - Ranked present-mask corrections (apply one after observation)

The decision tree in Section 4.2 selects exactly one of the following.

#### Candidate A - No mask change (observation was a misread)

No firmware change. Confirm the validator's raw dump shows a nonzero mask, then skip to documentation. This is the expected outcome if Step 0 reveals the audit read the wrong offset.

#### Candidate B - Force a present-mask mark at Bank Load completion (applied)

Applies when the present-mask field was never dirty because the setter was a no-op (boot already set the same value) and the runtime full-Bank mark did not run or did not cover it.

Implementation: `bank_setScenePresentMask()` now returns whether it changed
the normalized value. Core/Hardware/SD/filesystem.c re-marks the field in both
commit branches only when that return is zero, immediately beside the
commit-site witness. This is the smallest no-op-safe form of Candidate B; it
does not duplicate the ordinary changed-value mark.

File: Core/Hardware/SD/filesystem.c, both commit branches, immediately after the commit-site witness.

    /*
     * Guarantee the present-mask field is dirty even when the union
     * produced no change.
     *
     * What: explicitly mark the two-byte scene-present field. Inputs:
     * none beyond the retained mask. Outputs: canonical bits for payload
     * offsets 10..11 are set so the next drain re-captures the resident
     * mask. Why: bank_setScenePresentMask() marks only on a changed
     * normalized value; if boot already established the same union, the
     * field could otherwise stay unmarked and its record bytes would
     * never be refreshed. Affiliates: autosave_markBankFieldDirty() and
     * the drain.
     */
    autosave_markBankFieldDirty(
        AUTOSAVE_BANK_FIELD_SCENE_PRESENT_MASK);

This is a targeted mark, not a new accessor. It is only applied if the witness proves the field was never dirty; it must not be added unconditionally because the setter already owns the change-aware contract.

#### Candidate C - Bank Save union instead of overwrite

Applies when a zero or partial Bank Save reset the resident mask.

File: Core/Hardware/SD/filesystem.c, Bank Save phase 45, line 13861.

Replace:

    bank_setScenePresentMask(op_bank_scene_save_mask);

with:

    bank_setScenePresentMask((uint16_t)(bank_scenePresentMask() |
                                        op_bank_scene_save_mask));

Add the comment:

    /*
     * Preserve resident availability for Scenes outside the saved subset.
     *
     * What: union the save mask with the already-retained present mask
     * rather than overwriting it. Inputs: the current resident mask and
     * the caller-selected Bank Save mask. Outputs: saved children become
     * present while pre-existing unselected-but-valid Scenes stay
     * present, matching the Bank Load union rule. Why: the save writes
     * only the selected child folders, but the resident SRAM for
     * unselected Scenes remains valid, so dropping their present bits
     * would make Scene payload capture and Load/Save LEDs disagree with
     * retained data. Affiliates: bank_setScenePresentMask() and the Bank
     * Load union.
     */

#### Candidate D - Capture/copy path repair (only if witness proves it)

If the drain-site witness shows a nonzero resident mask yet the record still reads zero, the loss is in capture or copy. Re-verify, in order, without changing semantics first:

1. autosave_u16Byte() little-endian extraction;
2. autosave_transformDrainChunk()'s patch offset math (AUTOSAVE_PAYLOAD_OFFSET + patch_offsets[cursor]);
3. the exact absolute offset of the raw dump.

Any correction here must be paired with a fresh byte-level trace and a new record dump. No blind rewrite is authorized.

### Change 7 - Host validator tools/verify_bank_autosave.py

New read-only host tool modeled on tools/decode_devlogs.py. It must not write to the card and must not require new firmware state.

Command contract:

    python tools/verify_bank_autosave.py <card_root> <bank_slot>

Inputs:

- a copied card root containing .hcnames, .hcprms1, .hcprms2, settings.cfg, and the Bank/<NNN Name>/ tree;
- a selected Bank slot.

Assertions:

1. Parse .hcnames; assert the Bank row, all sixteen Scene rows, all sixteen Kit rows, and all 96 Instrument rows equal the Bank directory children plus each child's kitset.kcg rows.
2. Parse settings.cfg; assert active_bank equals the selected slot.
3. Parse .hcprms1 and .hcprms2; select the newer valid generation; assert:
   - Bank restore slot, name, active scene, and voice edit mask match bankset.bcg;
   - the Bank scene_present_mask (absolute offset 3930..3931) equals the Bank child present mask;
   - one or more sampled Scene payloads match the source sceneset.scg and kit member files.
4. Print one pass/fail summary and, on failure, the exact mismatched row or field (including the raw bytes of the Bank section at offsets 3920..3934).

The raw Bank-section dump from this validator also implements Step 0.

---

## 6. Build, verify, RAM, and image

1. Build logging-on (DEV_MODE_LOGGING=1, the Session 051 configuration with the temporarily approved 2,048-record trace ring): make && make img.
2. Record text/data/bss before and after. The B stage and the settings-dirty marks add no retained RAM and no new data section entries; assert that data and bss are unchanged and that any text delta is attributable to the added comment-compiled code paths only.
3. Copy build/LXRV2_lxr02.img to the card root, hold the main encoder, power on.
4. Run the hardware fixtures in Section 7 with logging on, copying the card after each.
5. Decode /asavetrc.bin with tools/decode_devlogs.py and run tools/verify_bank_autosave.py against the copied card.
6. After the present-mask correction is selected and verified, build logging-off and confirm the logging-only additions are absent from that image (no trace RAM).
7. Regenerate the image and copy it to the card for the final reboot-restore check.

---

## 7. Hardware fixture matrix

Run each with logging on and copy the card after each:

1. Full sixteen-child Bank Load: all rows correct; active_bank persisted; present mask = 0xFFFF in the winning record.
2. Selective Bank Load (fewer children): loaded children published; unselected present Scenes preserved; present mask is the union.
3. Empty Bank Load: identity/restore slot persist; present mask follows the empty-bank rule; no child rows invented.
4. Missing or malformed Bank: error path leaves settings.cfg and both records unchanged.
5. Bank Load while playing: no audible glitch; publication still completes.
6. Reboot after a Bank Load: boot restores the newly loaded Bank slot, not the stale active_bank.
7. Bank Save then reboot (symmetric check for Change 2): active_bank follows the saved slot.

---

## 8. Documentation close-out

After the fixes are verified:

- AUTOSAVE.md: record the present-mask capture contract and the B trace stage if it remains in the logging build.
- FILESYSTEM_SPEC.md: document that Bank Load and Bank Save now mark the settings writer dirty, and the Bank Save present-mask union rule (if Candidate C is applied).
- MODULE_INTERCHANGE_SPEC.md: note the Bank Load/Save to filesystem_markSettingsDirty() affiliation.
- SRAM_MANIFEST.md: confirm no RAM change; regenerate totals for the final logging-on and logging-off builds.
- Write the Session 052 handoff log under knowledge_files/log_archive/052_SESSION_HANDOFF_LOG.md and update 000_SESSION_INDEX.md and MEMORY.md.

---

## 9. Definition of done

Session 052 is complete when:

- the settings fixes (Changes 1 and 2) are in and build cleanly with no RAM change;
- the present-mask witness (Changes 3, 4, 5) ran, the raw dump and decoded B records selected one Candidate correction, and that correction is applied and verified;
- the host validator passes against the fixture cards;
- the hardware matrix in Section 7 is captured, especially fixtures 2, 6, and 7;
- the documentation in Section 8 is reconciled.
