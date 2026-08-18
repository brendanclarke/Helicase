# Session 052 Post-Analysis — Bank Load Persistence

**Date**: 2026-08-18 (updated after the rebuild and re-test)
**Prior analysis superseded**: the previous version of this file concluded the
`SD_CARD` fixture was stale (the firmware image predated the Session 052 source
edits). That was correct for the state at the time. The repository has since
been updated with a rebuilt image and a fresh card capture, and the Session 052
fixes are now hardware-confirmed.

**Headline conclusion**: after rebuilding the firmware, both Session 052 targets
are verified on hardware — `settings.cfg` persists `active_bank=8` and both
AutoSave records carry `scene_present_mask=0xffff`. The trace `B` witness fired
with the expected values and `tools/verify_bank_autosave.py SD_CARD 8` passes.
A separate, pre-existing boot-path failure (`KQ019KST`) surfaced on the first
boot attempt and is now scoped as a deferred refactor, not part of this fix.

---

## 1. What changed since the previous analysis

New commit:

    47691ba  Bank test
    Date:    Tue Aug 18 09:11:52 2026 +0200

It contains a rebuilt image and a re-captured card:

- `build/LXRV2_lxr02.img` and `SD_CARD/LXRV2_lxr02.img` are byte-identical
  (SHA-256 `B8FD2FDF...`) and grew from 378,372 to **378,876 bytes**, proving a
  genuine rebuild (the prior 16:30 image was 378,372 bytes).
- `SD_CARD/.hcprms1`, `SD_CARD/.hcprms2`, `SD_CARD/settings.cfg`, and the two
  `.img` files changed; `SCOPING_TARGETS.md` gained a Session 052 note.
- `SD_CARD/.hcnames` is unchanged (still 129 rows, row 0 = `Full<TAB>008`),
  consistent with re-loading the same Bank 008.

## 2. Hardware verification results (CONFIRMED)

### 2.1 `settings.cfg`

    active_bank=8

Previously `12`; now the Bank Load persisted the committed restore slot. Change
1 (mark settings dirty after Bank Load restore-slot commit) works.

### 2.2 AutoSave Bank section (absolute offsets 3920..3934)

    .hcprms1: 08 00 | 46 75 6C 6C 00 00 00 00 | FF FF | 06 | 40 00
    .hcprms2: 08 00 | 46 75 6C 6C 00 00 00 00 | FF FF | 06 | 40 00

Decoded (both records agree):

- restore slot 3920..3921 = `08 00` -> 8 (correct);
- name 3922..3929 = "Full" (correct);
- **present mask 3930..3931 = `FF FF` -> `0xFFFF` (correct, was `0x0000`)**;
- active scene 3932 = `06` (matches `bankset.bcg`);
- voice edit mask 3933..3934 = `40 00` -> `0x0040` (matches `bankset.bcg`).

Candidate B (re-mark the two present-mask bytes when the Bank Load union is a
no-op) works.

Header validity: both records parse as `HCPR`, version `01`, commit `A5`.
Generations are `.hcprms1 = 3` and `.hcprms2 = 4`, so `.hcprms2` is the newer
valid winner under the firmware's wrapping signed-difference rule. Both records
carry identical correct Bank data, and the host validator selected the winner
and reported PASS.

### 2.3 Trace `B` witness (decoded from `/asavetrc.bin` on the card)

Documented in `SCOPING_TARGETS.md`:

- `B` at Bank Load commit: resident mask `0xffff`, effective load mask `0xffff`;
- `B` at AutoSave drain: resident mask `0xffff`, payload offset `10`.

These are exactly the two values the commit-site and drain-site witnesses were
designed to prove, closing the "captured zero vs. never dirty" ambiguity.

### 2.4 Host validator

    tools/verify_bank_autosave.py SD_CARD 8  ->  PASS

This validates the full 129-row HCNAMES projection, settings, Bank metadata,
child mask, and bounded Scene/Kit/Instrument payloads, plus CRC/header/generation
selection.

---

## 3. New finding — first-boot Kit-quarantine failure (`KQ019KST`)

The first boot with the rebuilt image failed before Bank Load. `bootlog.bin`
recorded `KQ019KST`: the boot-time Kit library index generation
(`filesystem_createLibraryIndexBlocking(FS_LIBRARY_INDEX_KIT)`) was traversing
root Kit slot 019 and streaming `kitset.kcg` under a single ten-second
`KITQUAR` deadline, reading one byte at a time. The Kit 019 fixture is
structurally valid on the copied card, so this is an unnecessarily broad and
expensive boot gate, not a corrupt Kit.

On retry, boot completed and the Bank 008 load produced the verified results in
Section 2. The `bootlog.bin` content was noted as stale failure evidence: a
successful boot does not clear the prior boot-failure record.

This is a **pre-existing boot-path design issue**, now scoped in
`SCOPING_TARGETS.md` as a deferred refactor (separate the "can I reconstruct an
index row" sanitation pass from full per-payload `kitset.kcg` parsing, which
belongs at load time). It is explicitly out of scope for the current session.

---

## 4. Remaining code-level concerns

The Session 052 settings and present-mask corrections are verified. The
following items from the code review remain open and are not contradicted by the
hardware result:

- **P1 — Bank Save present-mask overwrite (Candidate C) still unapplied.**
  `filesystem.c:13945` is still `bank_setScenePresentMask(op_bank_scene_save_mask)`,
  a direct overwrite. It is the only writer that can clear resident present bits
  after `bank_init()` seeded the mask to 1; a partial Bank Save would shrink the
  resident mask while unselected Scene payloads remain valid. The Session 052
  test was a Bank **Load**, so this path was not exercised.

- **P2 — Unconditional settings mark produces one redundant `active_bank`
  rewrite per boot.** Harmless and reconciles a stale stored slot, but it is a
  deliberate behavior change (an extra SD write each power-on) that should stay
  explicitly accepted.

- **P3 — The `B` witness is logging-only.** It exists only in the
  `DEV_MODE_LOGGING=1` build and occupies no production RAM. The logging-off
  build must still be confirmed to omit the `B`/trace additions and the expanded
  2048-record ring.

- **P4 — Trace artifacts are gitignored.** `/asavetrc.bin` and `/bootlog.bin`
  are `*.bin` and therefore not in the repository. They were decoded from the
  card (see Section 2.3) but are not retained here, so future readers depend on
  the `SCOPING_TARGETS.md` summary rather than a decodable artifact.

---

## 5. Assessment

- The previous "stale image" diagnosis was correct and is now resolved by a real
  rebuild (`378876`-byte image) and a genuine hardware pass.
- Both Session 052 acceptance targets are met: `active_bank` persists and the
  Bank present mask equals the effective selected-child union (`0xFFFF`).
- The `B` witness validated the Candidate B selection: the commit-site and
  drain-site resident masks are both `0xFFFF`, so the earlier zero was a
  no-op-not-remarked capture gap, exactly as the static analysis predicted.
- The only new defect is the pre-existing boot Kit-quarantine hang, which is a
  separate deferred refactor and not a regression from Session 052.

## 6. Remaining work / recommendations

1. **Apply or explicitly accept Candidate C** (Bank Save present-mask union) so
   a partial `Save:[Bank]` cannot shrink the resident mask; at minimum document
   the current overwrite as a known limitation.
2. **Build logging-off** and confirm the `B` stage, its layout macros, and the
   `AUTOSAVE_TRACE_RECORD_COUNT` expansion are absent; regenerate
   `SRAM_MANIFEST.md` totals for both builds.
3. **Track the boot sanitizer refactor** separately (move `kitset.kcg`
   parsing/six-member validation out of the boot index pass; keep per-slot
   one-object rename/duplicate/sync behavior), per `SCOPING_TARGETS.md`.
4. **Close out the session documentation**: write
   `knowledge_files/log_archive/052_SESSION_HANDOFF_LOG.md` with the new image
   hash, the verified `active_bank`/present-mask results, and the decoded `B`
   values; update `000_SESSION_INDEX.md` and `MEMORY.md` accordingly.

---

## 7. Bottom line

Session 052's two durable targets are now hardware-verified: `settings.cfg`
persists `active_bank=8` and both `.hcprms` records report
`scene_present_mask=0xffff`, with the `B` trace witness and the host validator
both confirming the result. The earlier stale-card artifacts were explained by
the un-rebuilt image, not by a firmware defect. The remaining items are the
(separate, pre-existing) boot Kit-quarantine refactor and the not-yet-applied
Bank Save present-mask union.

---

## 8. Follow-up — P1 and P2 detail

This section expands the two code-level concerns from Section 4 with the exact
anchors, failure scenarios, and resolution options.

### P1 — Bank Save still overwrites the present mask (Candidate C not applied)

**Anchor**: `Core/Hardware/SD/filesystem.c:13945`

```c
bank_setScenePresentMask(op_bank_scene_save_mask);
```

`op_bank_scene_save_mask` is the caller-supplied save mask, validated only to 16
bits at `filesystem.c:21165-21167`. Bank Save is explicitly a **subset**
operation: the payload loop writes only children whose bits are set
(`filesystem.c:13687-13703`), and the active-scene relocation guard at
`filesystem.c:21175-21197` treats a partial mask as a first-class case
("Save:[Bank] may intentionally save only a subset of resident Scenes").

**Failure scenario**: start with 16 resident Scenes (`0xFFFF`), then
`Save:[Bank]` with only Scenes 0-3 selected (`0x000F`). The overwrite drops bits
4-15 even though those Scenes remain resident in SRAM. The consequences are not
cosmetic:

- `bank_scenePresent()` returns false for the dropped Scenes, so the AutoSave
  Scene-payload capture (`autosave_scenePayloadBase()` gates on it) silently
  stops persisting them.
- Load/Save SEQ LEDs and voice-edit fan-out no longer match the data still in RAM.
- A later Bank Load that does a *union* cannot restore those bits unless the new
  load happens to request them.

Bank Load does this correctly (union at `filesystem.c:10454`); Save is the lone
inconsistent writer and the only mechanism that can take a mask `bank_init()`
seeded to 1 and make it zero. The Session 052 test was a **Load**, so this path
was never exercised by the verification.

**Candidate C fix** (one line):

```c
bank_setScenePresentMask((uint16_t)(bank_scenePresentMask() | op_bank_scene_save_mask));
```

### P2 — Unconditional settings mark produces one redundant `active_bank` write per boot

**Anchors**: the three `filesystem_markSettingsDirty()` calls
(`filesystem.c:10346`, `10501`, `13959`).

The boot ladder makes this path fire every power-on:

1. `settings.cfg` parses `active_bank=12` -> `bank_setRestoreBankSlot(12)`
   (`filesystem.c:2071-2075`).
2. `main.c:806` runs `preset_loadBank(12, 0xffff)` -> the boot Bank Load commit
   executes `bank_setRestoreBankSlot(12)` (a no-op, already 12) followed by
   `filesystem_markSettingsDirty()`.
3. At that moment `fs_settings_runtime_ready` is still `0`; the gate is not
   opened until `main.c:942` calls `filesystem_enableRuntimeSettingsWrites()`.
   The mark only latches `fs_settings_dirty = 1`.
4. `filesystem_enableRuntimeSettingsWrites()` sees the dirty latch and restarts
   the one-second debounce deadline (`filesystem.c:19151-19154`).
5. `filesystem_settingsWriterSchedule_tick()` then starts
   `FS_INTERNAL_OP_SAVE_GLOBALS`, which re-serializes `active_bank` from
   `bank_restoreBankSlot()` — writing back the *same* value already on the card.

**Net effect**: one value-idempotent `settings.cfg` rewrite on every boot with a
valid Bank. It is harmless to correctness and has a real upside (if the boot Bank
fell back to a different slot than the stored one, it reconciles `settings.cfg`),
but it is an extra SD write plus one foreground filesystem operation per power-on.
The pre-plan explicitly chose this default ("the unconditional mark so both paths
share one authority") rather than gating the mark on `fs_settings_runtime_ready`.
That tradeoff should be accepted deliberately, not accidentally.
