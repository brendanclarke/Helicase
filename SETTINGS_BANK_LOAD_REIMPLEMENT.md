# Settings, Bank Save, Bank Load, and AutoSave Reimplementation Plan

## Purpose

This document is the rollback-safe implementation plan for restoring the useful work described by:

- `SAVE_FIX_SETTINGS_BANK_BACKGROUND.md`
- `AUTOSAVE_REMEDY_PA2ST2-3.md`

The expected rollback target is commit `c9807fa` (`autosave trace logger working, pre steps 2 and 3 implementation`). Verify the actual rollback target before changing code; do not assume the commit if branch history has changed.

The central correction is that **all AutoSave CRC work must be byte-bounded across filesystem ticks**. The failed generic one-millisecond runtime pacing change must not be restored. It delayed every filesystem operation, made Bank Save and Bank Load take roughly a minute, delayed rather than eliminated the audio glitches, and introduced unrelated boot/load behavior changes.

This plan also restores the targeted runtime Bank Load behavior: a Bank Load initiated by the user preserves the currently active Scene, while boot loading continues to restore the Bank's saved default Scene.

## Preserve this plan before rollback

This file and `SAVE_FIX_SETTINGS_BANK_BACKGROUND.md` may be untracked at the rollback point. `AUTOSAVE_REMEDY_PA2ST2-3.md` also contains post-commit reconciliation notes that may be lost when its tracked copy is restored.

Before any reset or cleanup, preserve these three documents outside the worktree or by another user-selected, recoverable method:

- `SETTINGS_BANK_LOAD_REIMPLEMENT.md`
- `SAVE_FIX_SETTINGS_BANK_BACKGROUND.md`
- `AUTOSAVE_REMEDY_PA2ST2-3.md`

Do not rely on an untracked file surviving `git clean`. This plan does not authorize an automatic reset, clean, stash, or commit.

## Accepted evidence and scope

The following findings are already established and should not be reopened without contradictory hardware evidence:

1. The single-parameter AutoSave hook has been tested for every user-testable parameter class:
   - Scene parameters are covered.
   - Kit/Instrument parameters are covered.
   - MIDI channel/note values are Scene data and are covered by the Scene tests.
   - There are no user-editable Bank scalar values requiring a separate parameter-hook test.
   - No additional “repeated edit,” “idle,” or broad coverage matrix is required.

2. Commit `c9807fa` already contains the Step 1 AutoSave trace ring and trace writer. Do not reintroduce the superseded proposed 16-byte diagnostic structure, dirty-bit-count API, or combined Step 2/3 firmware diagnostics.

3. The measured long-running work was whole-record CRC generation over a 34,768-byte AutoSave record. One captured interval was at least 77.7 ms. This is sufficient evidence for byte-bounding the CRC work.

4. Generic one-millisecond filesystem pacing was a failed implementation. It must not be restored in any form.

5. The settings persistence test succeeded in the observability build: changed settings were written and survived reboot. Therefore the rollback baseline must be tested before changing settings control flow. A settings code change is conditional on reproducing a missing dirty notification after rollback.

6. A user-initiated runtime Bank Load must preserve the active Scene. A boot-time Bank Load must retain the existing behavior of restoring the Bank's saved default Scene.

7. The last failed boot left an AutoSave file at 32,768 bytes rather than the required 34,768 bytes. CRC chunking addresses long CPU monopolization; it must not be claimed to fix an SD/FAT allocation hang. Any recurrence of a truncated record or boot timeout is a stop condition for a separate, evidence-led filesystem investigation.

## Explicit exclusions

The reimplementation must not include any of the following unless a new, specific failure proves one is required:

- No generic `filesystem_runtimeTick()` pacing layer.
- No one-millisecond timer or blind delay between Bank Save/Load steps.
- No reduction of all filesystem activity to one operation every fixed time interval.
- No new SaveFix trace ring or `savefix.bin` file in the initial implementation.
- No DEV logging consolidation or logging-file duplicate fix.
- No HCNAMES, quarantine, directory-index, Bank format, SD-driver, or audio-buffer changes.
- No Bank Load timing instrumentation unless Bank Load remains slow after the failed pacing code is confirmed absent.
- No masking of load/save failures, automatic conversion of errors into success, or dirty-state clearing after a failed write.
- No new permanent SRAM allocation for CRC state or active-Scene preservation.
- No repetition of the already accepted parameter-hook coverage matrix.

## 2026-08-10 implementation record — Phase 2 CRC bound

Status: source implementation is in progress; no hardware claim is made in
this entry. The checked-out `f033574` worktree is a documentation-only
descendant of `c9807fa`: `Core/`, `config.h`, `main.c`, and `Makefile` remain
at the stated rollback-source boundary. User-owned `SD_CARD/` fixtures remain
untouched.

The isolated code change uses `AUTOSAVE_CRC_BYTES_PER_TICK == 128` and existing
operation cursors only. It replaces the whole-record initial CRC helper with
an incremental creation-image updater, bounds candidate-validation reads and
transformed-copy reads to the same cap, and prepares initial/recovery CRCs
before create/remove mutations. No fixed-delay pacing, Bank Load behavior,
settings path, logging path, hidden-file format, record-sized buffer, or new
permanent SRAM allocation is included. `make -B -j2` passed with logging on
(`text=373,076`, `data=400`, `bss=78,996`) and with logging off
(`text=367,372`, `data=396`, `bss=78,444`); the normal logging-on image was
then rebuilt and packaged. Hardware validation remains required before this
phase can be accepted.

### 2026-08-10 hardware observation — `013 LoadTst` post-load `FsErr`

The first CRC-only hardware run loaded `SD_CARD/Bank/013 LoadTst` far enough
to commit its payload: the copied card's `settings.cfg` now records
`active_bank=13` and every `scene_source_00` through `scene_source_15` as
`1013`, while `/.hcnames` contains the Bank and all sixteen selected Scene/Kit
identity rows. The literal on-screen `FsErr` is therefore not evidence of a
Bank child parse/load failure and must not be used to judge the bounded CRC
implementation.

Source review identifies a separate post-load race in the existing logging
path. After the payload's bounded DSP apply, Menu requests a read-only
`/Bank/.hcindex` restore; if the one filesystem facade is busy, that helper
immediately closes the command and shows generic `FsErr` without an operation
code. The optional AutoSave trace-flush scheduler, and after its debounce the
settings writer, can claim that facade during the Load page. The trace does
not identify which autonomous owner held it for this run, so the fixture is
evidence of the generic rejection boundary rather than proof of one owner.
This observation does not authorize a fix in the CRC-only image: preserve the
SD evidence and address the background-writer/index arbitration as a
separately scoped change after the CRC hardware result has been isolated.

### 2026-08-10 targeted trace guard — ready for retest

At the user's direction, the normal logging build now makes
`filesystem_autosaveTraceFlushSchedule_tick()` retain its in-RAM trace records
without opening `asavetrc.bin` while either Load or Save is the active page.
This is deliberately limited to optional trace-flush admission: it prevents
the observed post-payload facade collision before Menu restores the unchanged
root `.hcindex`, while preserving the writer's existing deadline, record
format, completion acknowledgement, and later best-effort flush. No change
was made to the settings writer, Bank loader, CRC bound, or generic final-index
request behavior. Hardware retest: load `013 LoadTst` with logging enabled,
then leave the Load page and confirm trace persistence resumes normally.

## Required implementation order

Each firmware behavior change is a separate build and hardware test gate. Do not combine the CRC correction, Bank Load semantics, and a conditional settings fix into one untested image.

### Phase 0 — Restore and audit the rollback baseline

After rollback:

1. Confirm the exact commit and inspect the worktree.
2. Build the unchanged rollback baseline before editing it.
3. Record the baseline firmware size and static RAM/BSS use for comparison.
4. Verify the expected baseline symbols:
   - `autosave_initialRecordCrc()` still performs a whole-record calculation.
   - Initial-record creation and no-valid-record recovery call that whole-record helper.
   - Candidate validation and transformed-copy CRC paths are identified and inspected, not assumed to be bounded.
   - The failed generic runtime pacing function and one-millisecond interval are absent.
   - Runtime Bank Load does not yet have an explicit preserve-active-Scene request flag.
   - The Step 1 AutoSave trace implementation from `c9807fa` is present.
5. Restore the reconciled planning documents and the read-only host inspector described in Phase 1.

Stop if the rollback baseline itself does not build. Do not mix baseline repair with the behavior changes below.

### Phase 1 — Restore PA2ST2-3 evidence tooling and document state

`AUTOSAVE_REMEDY_PA2ST2-3.md` does not require a new firmware diagnostic subsystem. Its remaining reimplementation is evidence handling and targeted integration testing.

Restore `tools/inspect_autosave_fixture.py` as a read-only host tool. It must:

- Accept copied `.hcprms1`, `.hcprms2`, and the existing AutoSave trace file as input.
- Require an exact AutoSave record size of 34,768 bytes.
- Decode and report header magic, version, generation, commit state, Bank slot/name provenance, and record-selection fields.
- Recalculate CRC32C with the stored CRC field treated according to the on-card format.
- Report the dirty-mask population and relevant Scene/Kit source rows.
- Decode the existing Step 1 AutoSave trace event records.
- Never modify, repair, truncate, rename, or rewrite an input fixture.
- Exit nonzero for structural, size, or CRC failure so it can be used as a test gate.

Update `AUTOSAVE_REMEDY_PA2ST2-3.md` so that it records:

- The user-testable scalar parameter coverage is complete and accepted.
- The previously proposed combined Step 2/3 diagnostic SRAM structure is superseded and must not be implemented.
- The host inspector and existing Step 1 trace are the evidence tools.
- New C/H diagnostics are permitted only after a specific unlocalized boundary failure.

Run the inspector against known valid fixtures before changing firmware. This validates the tool independently of the reimplementation.

### Phase 2 — Implement byte-bounded CRC work only

This is the first firmware behavior change and must be tested by itself.

#### `config.h`

Add one named per-tick CRC budget, initially:

```c
#define AUTOSAVE_CRC_BYTES_PER_TICK 128u
```

The adjacent comment must state that this is a CPU-work bound, not an SD transfer delay. It exists to prevent CRC generation or validation from monopolizing the cooperative main loop and starving audio work. It must not imply a one-millisecond interval.

#### `Autosave.h` and `Autosave.c`

Replace the whole-record initial CRC API with an incremental API such as:

```c
uint32_t autosave_initialRecordCrcUpdate(
    uint32_t accumulator,
    uint32_t absolute_offset,
    uint16_t byte_count,
    uint32_t generation,
    uint16_t bank_slot,
    const char *bank_name,
    const autosave_resident_names_t *resident_names);
```

The exact types must match the real code after rollback. The API and implementation comments must document:

- The accumulator is the in-progress CRC32C state.
- `absolute_offset` identifies the next logical byte in the serialized 34,768-byte record.
- `byte_count` is the actual number of bytes processed by this call and may not exceed `AUTOSAVE_CRC_BYTES_PER_TICK`.
- The function synthesizes only that bounded interval of the initial record; it does not allocate or scan a complete record image.
- Header fields whose serialized value is special during CRC calculation, including the stored CRC field, must follow the existing on-disk format exactly.
- The returned value is an updated accumulator, not the finalized stored CRC unless the caller explicitly invokes the existing finish operation.

Apply the same bound to every CRC-producing or CRC-validating path:

1. Initial A/B record creation.
2. Recovery when neither A nor B contains a valid committed record.
3. Validation of an existing A or B candidate.
4. CRC generation while producing the normal transformed/copy record.

Remove `autosave_initialRecordCrc()` once no call sites remain. Remove any unused whole-image validation helper made obsolete by the streaming implementation.

Low-level CRC helpers must defensively reject or clamp an oversized request without accidentally advancing the caller's cursor by bytes that were not processed. Normal callers must always calculate their requested count as:

```c
min(remaining_bytes, AUTOSAVE_CRC_BYTES_PER_TICK)
```

Important loops, cursor arithmetic, serialized-field substitutions, and CRC begin/finalize operations require adjacent comments. The comments must explain both the operation and why the cooperative scheduler requires the bound.

#### `filesystem.c`

Use the existing operation workspace fields for the CRC accumulator and absolute cursor. Do not add a static record buffer or new permanent SRAM.

For each CRC phase:

1. Initialize the accumulator and cursor once on phase entry.
2. Process no more than `AUTOSAVE_CRC_BYTES_PER_TICK` bytes during one filesystem service tick.
3. Advance the cursor only by the count actually processed.
4. Yield to the main loop when bytes remain.
5. Finalize the CRC exactly once after the cursor reaches the full serialized record size.
6. Preserve the existing error result and retry behavior.

For initial creation and no-valid-record recovery, calculate the record CRC before opening, removing, or truncating the target file. This narrows the power-loss window: a reset during CRC preparation must leave the previous filesystem state untouched.

During pair recovery, completely write, synchronize, and close the B record before beginning the CRC/write sequence for A. A power loss must not leave both recovery targets intentionally in progress at once.

Candidate validation must read and CRC only a bounded interval per service tick. Transformed-copy CRC work must also obey the same byte budget; initial creation is not a special exception.

Do not add sleeps, timers, or fixed inter-operation delays around these phases.

#### Phase 2 source checks

Before hardware testing:

- Search for all CRC begin/update/finalize calls and account for every AutoSave record path.
- Confirm there are no remaining callers of the whole-record helper.
- Confirm every filesystem cursor advances by the actual bounded count.
- Confirm all error paths still report failure and retain dirty state where retry is required.
- Confirm no new permanent SRAM was added.
- Build with development logging enabled and disabled.
- Run formatting/diff checks appropriate to the repository.

#### Phase 2 hardware gate

Test this image before any Bank Load or settings control-flow change:

1. Boot with an existing valid A/B pair, play audio, edit one already-covered Scene or Kit parameter, and allow the writer to commit.
2. Delete both test-card AutoSave records, boot, and allow clean pair creation while audio is running.
3. Exercise the no-valid-record recovery path using deliberately invalid test fixtures.
4. After each case, power down normally, copy the SD state, and run the host inspector.
5. Require both records to be exactly 34,768 bytes when pair creation/recovery claims success.
6. Require valid CRC, coherent generation/commit state, and the expected changed parameter.
7. Require no boot timeout and no repeat of the two long audio glitches associated with whole-record CRC phases.

If a file stops at 32,768 bytes, boot times out, or an operation reports success with an invalid record, stop here. Preserve the entire SD image and existing trace. Do not work around it with a larger timeout or a delay; CRC chunking does not prove or repair a FAT cluster-allocation hang.

### Phase 3 — Preserve the active Scene during runtime Bank Load

Implement this only after Phase 2 passes.

#### `filesystem.h`

Extend the Bank Load request API with an explicit `preserve_active_scene` input. Its declaration comment must document:

- `true`: a user/runtime Bank Load keeps the Scene that was active when the request was accepted.
- `false`: boot loading restores the default active Scene stored in the Bank.
- The flag controls only active-Scene selection; it does not hide child-load failures or change which requested Scene/Kit files are validated.
- Completion remains asynchronous and is reported through the existing callback/result mechanism.

#### `filesystem.c`

Capture the active Scene index when a runtime request is accepted. Store the index and preservation flag in existing operation scratch storage if the real layout permits it; do not add permanent SRAM merely for this value.

The implementation must:

1. Distinguish runtime requests from boot requests at the call site rather than inferring intent from mutable global UI state.
2. Preserve the captured Scene even when a selective Bank Load mask does not include that Scene.
3. Validate every selected child load normally and surface the first real failure through existing logging/result paths.
4. Commit the final active Scene only after all selected children required by the request have succeeded.
5. Leave the prior active Scene unchanged if the Bank Load fails.
6. Continue using the Bank's saved default Scene for boot loading.

Adjacent comments must describe the packed flag/index representation if one is used, including masks, shifts, valid index range, and why existing scratch storage is reused.

#### Call sites

Audit every caller of the Bank Load request API:

- Boot path: pass `false`.
- User Load menu/runtime path: pass `true` and capture the current Scene.
- Any internal or test call: choose explicitly and document why.

Do not add Bank Load pacing, timing diagnostics, HCNAMES retries, or unrelated load behavior in this phase.

#### Phase 3 hardware gate

1. Start on a non-default Scene and load the test Bank with a full Scene mask. The active Scene must not switch.
2. Repeat with a selective mask that excludes the active Scene. The active Scene must still not switch.
3. Load a deliberately bad child fixture. The operation must report failure and must not silently switch the active Scene.
4. Reboot into the Bank. Boot must still restore the Bank's saved default Scene.
5. Confirm normal Bank Load duration remains comparable to the pre-pacing baseline.

### Phase 4 — Recheck settings persistence; patch only if it fails

The previous hardware run showed settings persistence working. Do not change the settings path merely because its page-coupled notification looks fragile.

First test the rollback image plus the passed Phase 2/3 changes:

1. Change AutoSave ON/OFF and at least one unrelated persistent setting.
2. Leave the menu normally and allow the existing settings debounce/write to complete.
3. Copy and inspect `settings.cfg` before reboot if practical.
4. Reboot and verify both settings are restored.

If this passes, make no settings C/H change. Record the passing result in `SAVE_FIX_SETTINGS_BANK_BACKGROUND.md` and close the settings branch as verified on the restored baseline.

If the edited value changes in RAM but no dirty revision is issued, implement only the missing notification boundary:

#### Conditional `filesystem.h` / `filesystem.c` change

Add a narrowly named API such as:

```c
void filesystem_notifyPersistentSettingChanged(uint16_t parameter_id);
```

The API must contain an explicit allowlist matching the settings parser/writer schema. The expected persistent parameter IDs are:

- `PAR_BPM`
- `PAR_EXT_SYNC`
- `PAR_QUANTISATION`
- `PAR_MIDI_CHAN_GLOBAL`
- `PAR_MIDI_FILT_TX`
- `PAR_MIDI_FILT_RX`
- `PAR_MIDI_ROUTING`
- `PAR_SCREENSAVER_ON_OFF`
- `PAR_BAR_RESET_MODE`
- `PAR_PRESCALER_CLOCK_IN`
- `PAR_PRESCALER_CLOCK_OUT1`
- `PAR_FOLLOW`
- `PAR_OSC_WAVE_INTERP`
- `PAR_AUTOSAVE_ENABLED`

Reconcile these names against the rollback code before implementation. The adjacent comment must identify the settings parser and writer as affiliates that must be updated together when the schema changes.

The API ignores non-persistent parameters and marks settings dirty only for an accepted persistent ID. It must preserve the existing revision, debounce, asynchronous write, flush, retry, and failure-reporting behavior.

#### Conditional `menu.c` change

After a static parameter commit actually changes the stored value, call the notification API without relying on `menu_activePage == MENU_MIDI_PAGE`. The API's allowlist, not mutable page identity, determines persistence.

The AutoSave policy update remains a separate immediate call to `filesystem_setAutosaveEnabled()`. Persistence notification writes the setting for the next boot; the policy call changes current runtime behavior. Comments must explain that both effects are required and neither substitutes for the other.

Bulk settings application through `menu_sendAllGlobals()` must not manufacture a user dirty event while boot is applying `settings.cfg`.

If the failure occurs later than dirty notification—open, write, sync, rename, close, or retry—do not apply the menu notification patch. Preserve the evidence and fix only the demonstrated filesystem boundary.

#### Phase 4 hardware gate

Repeat the two-setting test, inspect `settings.cfg`, and reboot. Also verify AutoSave OFF stops new AutoSave work and AutoSave ON permits the next real edit to be written. A failed settings write must remain visible and retryable; it must not be acknowledged as saved.

### Phase 5 — PA2ST2-3 integration checks

After the separate CRC, Bank Load, and conditional settings gates pass, run one combined integration image.

This is not a new broad parameter matrix. Carry forward the accepted Scene and Kit/Instrument hook evidence. Perform only one scalar edit as a regression smoke test for the writer.

Exercise:

1. Scene Load and Scene Save provenance updates.
2. Bank Load with active-Scene preservation.
3. Bank Save while stopped.
4. Bank Save while playing.
5. AutoSave OFF, then ON, with reboot confirmation through `settings.cfg`.
6. One Scene or Kit scalar edit after AutoSave is re-enabled.

Use the host inspector to verify:

- Exact record sizes and valid CRCs.
- Expected generation/commit selection.
- Correct active Bank and Scene/Kit source rows.
- The single regression edit reached the selected record.
- No dirty bits were cleared after a failed operation.

The settings writer keeps its existing priority over AutoSave work. Byte-bounded CRC work yields between ticks; it does not add another scheduler or timer.

### Phase 6 — Bank Save/audio sign-off

The Bank Save issue is closed only when the correctly chunked CRC build passes on hardware:

- Bank Save completes while stopped.
- Bank Save completes while playing.
- The saved Bank can be loaded.
- No long audio glitch occurs a few seconds after leaving the Save menu when AutoSave drains.
- No new audio-underrun evidence appears.
- Settings and AutoSave records remain structurally valid.

Do not add generic pacing if a glitch remains. Preserve the exact SD image and existing trace, identify the specific phase/function that exceeds the cooperative-loop budget, and split only that demonstrated CPU or SD operation. Any such follow-up requires a new targeted plan before code changes.

## File-by-file implementation map

| File | Planned role |
| --- | --- |
| `config.h` | Define the CRC bytes-per-tick CPU budget. No timing interval. |
| `Autosave.h` | Declare the incremental initial-record CRC API and document its bounded contract. |
| `Autosave.c` | Implement bounded serialized CRC updates; remove obsolete whole-record helpers. |
| `filesystem.c` | Convert initial creation, recovery, candidate validation, and transformed-copy CRC phases to cursor-driven bounded work; implement runtime active-Scene preservation; conditionally implement settings notification. |
| `filesystem.h` | Document the Bank Load preservation input and, only if evidence requires it, the persistent-setting notification API. |
| `menu.c` | Pass runtime Bank Load preservation intent; conditionally notify persistent setting edits independently of page identity. |
| Boot/load caller file(s) | Pass `preserve_active_scene = false` explicitly for boot. Reconcile actual call sites after rollback. |
| `tools/inspect_autosave_fixture.py` | Restore read-only structural, CRC, provenance, and trace inspection. |
| `AUTOSAVE_REMEDY_PA2ST2-3.md` | Record accepted coverage and the evidence-only Step 2/3 disposition. |
| `SAVE_FIX_SETTINGS_BANK_BACKGROUND.md` | Record each reimplementation phase, hardware result, and any stop condition. |

All C and H changes require detailed comments adjacent to the changed declarations, state fields, phase transitions, loops, cursor math, CRC operations, packed bits, and error behavior. Comments must explain what the code does, why it exists, its inputs/outputs, and the related call sites or data structures.

## Final source and release checks

Before declaring the reimplementation complete:

1. Build the normal release configuration.
2. Build with `DEV_MODE_LOGGING` enabled and disabled.
3. Confirm diagnostic storage and writers are absent/inactive when their controlling flag is disabled.
4. Compare firmware size and static RAM/BSS with the rollback baseline; explain any change.
5. Confirm there is no one-millisecond runtime pacing symbol or equivalent fixed-delay wrapper.
6. Confirm there is no whole-record AutoSave CRC call left in a cooperative filesystem phase.
7. Confirm every Bank Load caller chooses preserve/default behavior explicitly.
8. Confirm all load/save failures remain observable and are not converted into success.
9. Run the host inspector over the final hardware fixtures.
10. Run `git diff --check` and review the complete targeted diff before packaging firmware.

## Completion criteria

The work is complete when:

- All four AutoSave CRC paths are bounded to a fixed byte count per filesystem tick.
- Clean creation and no-valid-record recovery produce complete, valid 34,768-byte records without long audio stalls.
- Runtime Bank Load preserves the active Scene, while boot restores the saved default Scene.
- Settings persistence is verified; code is changed only if the rollback test proves the notification boundary is missing.
- Bank Save works while stopped and playing without the delayed AutoSave audio glitches.
- PA2ST2-3 uses the existing trace plus the read-only host inspector, with the already accepted scalar coverage carried forward.
- No failed generic pacing, unrelated logging change, or other out-of-scope subsystem change has been reintroduced.
