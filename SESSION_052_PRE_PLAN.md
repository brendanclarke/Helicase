# Session 052 Pre-Plan — Bank Load Persistence and Verification

**Project**: LXR-02 firmware port (STM32F765VIH6)
**Branch/state**: dev-ph3-autosave-ph2, dirty worktree following the Session
051 Scene-follow-up implementation and repair.
**Scope**: Bank Load persistence only. Scene HCNAMES work from Session 051 is
considered done unless this plan finds it implicated.

This plan is written from the observed card state after a real Bank Load test:
load Bank 008 "Full", then exit to voice mode. Temp and trace files were
deleted before the test, so the evidence is .hcnames, .hcprms1, .hcprms2,
settings.cfg, and the Bank/ source tree.

---

## 1. Observed fixture

Bank 008 Full on the card:

- bankset.bcg: active_scene=6, scene_mask_voice_edit=0040.
- Sixteen children 00..15, all present.
- Scene names: 00..02 Slak, 03 Full, 04..15 Slak.
- Embedded Kit names per child, in order:
  Brezel, Forest, Barf, FilMod, Emott, SoyEared, DocWire, Machine, KitWool,
  FatMed, Beatmstr, Organity, Goa, Snap, Electro, Brezel.

---

## 2. What landed

### 2.1 HCNAMES is fully correct

After the Bank Load, .hcnames contains:

- Bank row 0: Full 008.
- Scene rows 1..16: Slak for every child except Scene 3 Full, exactly matching
  the Bank children.
- Kit rows 17..32: the child embedded Kit names listed above, in order.
- Instrument rows 33..128: the six kit member names for each child, matching
  each child's kitset file rows.

The Bank-owned HCNAMES writer and per-child Scene/Kit/Instrument overlay are
working. No change to HCNAMES is required.

### 2.2 AutoSave Bank identity landed

.hcprms1 is generation 5 and is the current winner; .hcprms2 is generation 4.
Both records contain the same Bank section:

- restore slot = 8.
- Bank name = Full.
- active scene = 6.
- voice edit mask = 0x0040.

These match bankset.bcg. A sampled Scene payload (Scene 0) matches the child
sceneset.scg byte-for-byte, so child Scene payloads are being serialized.

---

## 3. What did not land

### 3.1 settings.cfg active_bank is stale

settings.cfg still contains active_bank=12. After loading Bank 008, the boot
selection authority should be 8. The Bank Load updated the resident restore
slot (the AutoSave record proves it), but the settings writer never ran.

The settings writer already serializes active_bank from
bank_restoreBankSlot(), but it is gated on the filesystem settings-dirty flag,
and no Bank Load completion marks that flag. This is the first concrete gap.

### 3.2 AutoSave scene-present mask is zero

The Bank section in both records has scene-present mask = 0x0000, even though
Bank 008 contains sixteen children. The expected value is 0xFFFF.

This matters because bank_scenePresent() gates Scene payload capture and
interpretation. A restore with a zero present mask would treat every Scene as
absent even though the record contains their bytes. The setter, marker, and
getter plumbing all appear correct, so the zero is either a capture-timing
problem or a missing/consumed mask at the metadata-commit boundary. This is
the second concrete gap and the primary investigation target.

---

## 4. Root-cause hypotheses

### 4.1 settings.cfg gap (high confidence)

The Bank Load metadata commit in filesystem_loadBankDirectory_tick calls
bank_setRestoreBankSlot(op_slot), which updates BankData and marks the AutoSave
Bank field. It does not mark the settings file dirty. The one-second debounced
settings writer therefore never persists the new active_bank.

Likely fix location:

- filesystem_loadBankDirectory_tick metadata commit, after
  bank_setRestoreBankSlot(op_slot), call filesystem_markSettingsDirty(); or
- on_bank_load_complete in presetManager.c, only when
  filesystem_status() == FS_STATUS_DONE.

Prefer the filesystem metadata commit so both the non-empty and empty Bank
paths share one authority. Confirm Bank Save already does this; if not, treat
it as a symmetric follow-up.

### 4.2 present-mask gap (needs isolation)

Observations that constrain the cause:

- The non-empty Bank path ran: all sixteen children were loaded, proven by the
  complete HCNAMES overlay.
- bank_setScenePresentMask(existing | op_bank_scene_load_mask) is the only
  non-Save writer of that mask in the Bank Load flow.
- The AutoSave field was either never marked, or was marked while the live mask
  was zero, or a later write copy-forwarded zero over a correct value.

Candidate checks, in order:

1. Log the effective op_bank_scene_load_mask at the metadata commit and at
   request completion. If it is zero by commit time, the loop/intersection is
   consuming it.
2. Log bank_scenePresentMask() immediately after the metadata commit and again
   when the writer drains. If it drops back to zero, locate the reset.
3. Verify the writer drain samples the present-mask bytes after the commit,
   not before, and that the mask take/re-dirty contract does not close the
   field before a later mark.
4. Check the boot path: if boot restores Bank 012 before this test, the prior
   present mask should have been 0xFFFF. The fact that the previous record also
   stored zero suggests the field has never been captured correctly, not just
   in this fixture.

The fix must make a successful full Bank Load persist the effective child mask
as the scene-present mask, and a selective load persist the union of previously
present and newly loaded Scenes. Do not publish children that were not loaded.

---

## 5. Implementation plan

### 5.1 Fix settings.cfg persistence

Change filesystem_loadBankDirectory_tick so both successful Bank metadata
commits (non-empty and empty) mark the settings file dirty after the restore
slot is committed. Add a comment documenting what/why/inputs/outputs and the
affiliates: bank_setRestoreBankSlot, the settings writer, and boot selection.

Verify Bank Save also marks settings dirty; add it if missing as a separate,
same-scope correction.

### 5.2 Fix the present mask

First add a logging-only Bank-present witness using the existing trace ring
(no new RAM): record the effective load mask and resident present mask at the
metadata commit and at writer drain. Run the fixture, decode, then apply the
smallest correction in the Bank Load path so the record's scene-present mask
matches the effective selected-child union.

Expected correction candidates once observed:

- retain the effective mask in op_bank_scene_load_mask through the metadata
  commit, or copy it into a local before the child loop mutates any shared
  scratch;
- ensure the present-mask setter is called with the union value and that the
  AutoSave field is marked and later captured;
- if a later reset exists, remove or reorder it.

### 5.3 Rebuild and image

Rebuild logging-on and logging-off, confirm no RAM movement, regenerate the
image, and copy it to the card.

---

## 6. Unit-test plan

There is no host unit-test harness for this target. Define a repeatable,
read-only host validator and a hardware fixture matrix so each assertion is
checkable from a copied card.

### 6.1 Host validator

Add tools/verify_bank_autosave.py, modeled on the read-only decode_devlogs.py
style. Given a card root and a selected Bank slot, it must:

- parse .hcnames and assert the Bank row, all sixteen Scene rows, all sixteen
  Kit rows, and all 96 Instrument rows equal the Bank directory children plus
  each child's kitset file rows;
- parse settings.cfg and assert active_bank equals the selected slot;
- parse .hcprms1 and .hcprms2, select the newer valid generation, and assert:
  - Bank restore slot, name, active scene, and voice edit mask match bankset;
  - scene-present mask equals the Bank child present mask;
  - one or more sampled Scene payloads match the source sceneset and kit member
    files;
- report a single pass/fail summary and the exact mismatched row/field.

This makes the Bank Load persistence contract testable without new firmware
state.

### 6.2 Hardware fixtures

Run these with logging on and copy the card after each:

1. Full sixteen-child Bank Load: all rows correct, active_bank persisted, and
   present mask = 0xFFFF in the winning record.
2. Selective Bank Load (fewer children): loaded children published; unselected
   present Scenes preserved; present mask is the union.
3. Empty Bank Load: identity/restore slot persist; present mask follows the
   empty-bank rule; no child rows invented.
4. Missing or malformed Bank: error path leaves settings.cfg and records
   unchanged.
5. Bank Load while playing: no audible glitch; publication still completes.
6. Reboot after a Bank Load: boot restores the newly loaded Bank slot, not the
   stale active_bank.

---

## 7. Close-out

Session 052 is complete when:

- the settings.cfg and present-mask fixes are in and build cleanly with no RAM
  change;
- the host validator passes against the fixture cards;
- the hardware matrix above is captured, especially the reboot-restore check;
- FILESYSTEM_SPEC.md, AUTOSAVE.md, MODULE_INTERCHANGE_SPEC.md, and
  SRAM_MANIFEST.md are updated for any changed behavior or API;
- a Session 052 handoff log and index entry are written.

Out of scope: Scene Load HCNAMES (done), InstrumentMrp kit restore (done in
051), recursive overwrite delete, runtime Bank Load active-Scene preservation,
and DSP debt.
