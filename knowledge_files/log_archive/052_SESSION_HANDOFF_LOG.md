# Session 052 Handoff Log — Bank Load Persistence

**Project**: LXR-02 firmware port (STM32F765VIH6)
**Session goal**: Execute `SESSION_052_PRE_PLAN.md`: make a successful Bank Load
durable in two observable ways — persist `settings.cfg active_bank`, and make
the AutoSave hidden record's Bank `scene_present_mask` equal the effective
selected-child union.
**Working repository**: `C:\Users\brendan.clarke\proj\Helicase`, branch
`dev-ph3-autosave-ph2`. The Session 052 code landed in commit `ebfd971`
("bank load correction session 052 initial attempt") and the rebuilt-image
hardware capture landed in `47691ba` ("Bank test").

## End of session

```
DATE: 2026-08-18
SESSION GOAL: Make Bank Load durable: settings.cfg active_bank follows the
  committed restore slot, and the AutoSave Bank scene_present_mask equals the
  effective selected-child union.
COMPLETED: Implemented both corrections and the logging-only B witness, added
  the host validator, rebuilt the image, and verified the full 16-child Bank 008
  load on hardware. Root cause of the zero present mask was a change-aware
  setter no-op against the boot-time mask; Bank Load now re-marks the two
  present-mask bytes on an equal union. Also scoped a first-boot Kit-quarantine
  hang and two follow-up refactor targets into SCOPING_TARGETS.md.
VERIFIED ON HARDWARE: Yes (Bank 008 load). settings.cfg active_bank=8; both
  .hcprms records scene_present_mask=0xffff with correct Bank identity (slot 8,
  "Full", active 6, voice 0x0040); B trace commit 0xffff/0xffff and drain
  0xffff/offset 10; tools/verify_bank_autosave.py SD_CARD 8 PASS.

CHANGES THIS SESSION:
- Core/Bank/BankData.c/.h: bank_setScenePresentMask() now returns uint8_t
  (nonzero when the normalized mask changed and was marked); Bank Load re-marks
  on a no-op union.
- Core/Bank/Scene/Autosave.c: drain-site present-mask B witness in
  autosave_getLivePayloadByte() at payload offset 10.
- Core/Bank/Scene/AutosaveTrace.h: added AUTOSAVE_TRACE_STAGE_BANK_PRESENT ('B')
  and the BANK_PRESENT flag/value-layout macros.
- Core/Hardware/SD/filesystem.c: no-op present-mask re-mark, commit-site B
  witnesses, and filesystem_markSettingsDirty() after the restore-slot commit in
  the empty-load, non-empty-load, and Bank Save paths.
- tools/decode_devlogs.py: B stage decoder.
- tools/verify_bank_autosave.py: new read-only Bank/AutoSave validator.
- SCOPING_TARGETS.md: Session 052 discovery note plus deferred boot-sanitizer
  and Bank-Save/settings-mark refactor targets.
- knowledge_files/specification_reference/: AUTOSAVE.md, DEV_MODES.md,
  FILESYSTEM_SPEC.md, MODULE_INTERCHANGE_SPEC.md, SRAM_MANIFEST.md reconciled.
- knowledge_files/log_archive/000_SESSION_INDEX.md updated.
- SD_CARD/ and build/ LXRV2_lxr02.img rebuilt (378,372 -> 378,876 bytes).

KNOWN ISSUES INTRODUCED: None that remain. Two pre-existing items were surfaced
  and deferred, not introduced: (P1) Bank Save still overwrites the resident
  present mask, and (P2) the unconditional settings mark writes settings.cfg on
  every boot. Both are in SCOPING_TARGETS.md.
KNOWN ISSUES RESOLVED: settings.cfg active_bank was stale after Bank Load; the
  AutoSave Bank scene_present_mask was captured as zero after an equal-union
  Bank Load. Both are now corrected and hardware-verified.

NEXT SESSION RECOMMENDED GOAL: The boot Kit-quarantine refactor (KQ019KST)
  scoped in SCOPING_TARGETS.md, or apply Candidate C (Bank Save present-mask
  union) and revisit the boot settings-mark tradeoff. See the deferred targets.
BLOCKERS: The ARM toolchain is not installed in the current working environment;
  rebuilds and RAM (text/data/bss) regeneration must happen on a machine with
  arm-none-eabi-gcc.

CRITICAL REMINDERS FOR NEXT SESSION:
- Bank Load is a union and re-marks on a no-op; Bank Save is still a direct
  overwrite. Do not add a clearing write to the present mask without first
  applying the SCOPING_TARGETS.md Candidate C union.
- filesystem_markSettingsDirty() is called beside bank_setRestoreBankSlot() in
  all three Bank commit paths, including boot; the boot mark is intentional but
  produces one redundant settings.cfg rewrite per power-on.
- The B witness exists only in the DEV_MODE_LOGGING build; the logging-off image
  and its RAM totals have not yet been regenerated/verified.
- bootlog.bin is stale failure evidence: a successful boot does not clear a
  previous boot-failure token (KQ019KST).
```

---

## 1. Scope

Two acceptance targets, both from `SESSION_052_PRE_PLAN.md`:

1. `settings.cfg` must re-serialize `active_bank` from the committed Bank
   restore slot, so a reboot restores the newly loaded Bank rather than a stale
   slot.
2. The AutoSave hidden record's Bank `scene_present_mask` must equal the
   effective selected-child union, so a later reader interprets every captured
   Scene payload as present.

In scope: settings persistence after Bank Load and the symmetric Bank Save gap;
the present-mask capture diagnosis and its smallest correction; the logging-only
`B` witness; the host validator `tools/verify_bank_autosave.py`; and
documentation reconciliation.

Out of scope (unchanged from Session 051): Scene Load HCNAMES, InstrumentMrp
kit restore, recursive overwrite delete, runtime Bank Load active-Scene
preservation, DSP debt, and Pattern persistence.

## 2. Root cause and fix

### 2.1 settings.cfg gap (Change 1/2)

`bank_setRestoreBankSlot(op_slot)` updated the resident restore slot, but nothing
marked the autonomous settings writer dirty, so the debounced
`settings.cfg` write never ran and `active_bank` stayed stale. Bank Save had the
same omission. Fix: call `filesystem_markSettingsDirty()` immediately after
`bank_setRestoreBankSlot(op_slot)` in the empty-load, non-empty-load, and Bank
Save commit paths. The writer reads `bank_restoreBankSlot()` live, so the next
`FS_INTERNAL_OP_SAVE_GLOBALS` serializes the committed slot.

### 2.2 present-mask gap (Candidate B)

`bank_setScenePresentMask()` only marks its two AutoSave bytes when the
normalized value changes. During a full Bank Load, the union
`bank_scenePresentMask() | op_bank_scene_load_mask` was an equal-value no-op
against the mask the boot Bank Load had already established, so the field was
never re-marked and the record kept its stale zero bytes even though the
restore-slot/name/active/voice fields were all captured. The fix makes
`bank_setScenePresentMask()` return whether it changed, and both Bank Load commit
branches explicitly re-mark `AUTOSAVE_BANK_FIELD_SCENE_PRESENT_MASK` when the
setter is a no-op.

## 3. Implementation details

- `Core/Bank/BankData.c` / `.h`: `bank_setScenePresentMask()` now returns
  `uint8_t` (1 = changed and marked, 0 = no-op). The single external declaration
  in `BankData.h` is updated; the other callers (`presetManager.c` and the Bank
  Save commit) ignore the return value.
- `Core/Hardware/SD/filesystem.c` empty commit (`~10302`) and non-empty commit
  (`~10454`): `if (!bank_setScenePresentMask(...)) autosave_markBankFieldDirty(
  AUTOSAVE_BANK_FIELD_SCENE_PRESENT_MASK);`.
- `Core/Hardware/SD/filesystem.c`: `filesystem_markSettingsDirty()` added at the
  empty-load (`~10346`), non-empty-load (`~10501`), and Bank Save (`~13959`)
  commit paths.
- `Core/Bank/Scene/AutosaveTrace.h`: `AUTOSAVE_TRACE_STAGE_BANK_PRESENT = 'B'`;
  `AUTOSAVE_TRACE_BANK_PRESENT_FLAG_DRAIN` (bit 0) and
  `AUTOSAVE_TRACE_BANK_PRESENT_MASK_SHIFT` (16) layout macros.
- `Core/Hardware/SD/filesystem.c` commit witnesses (`~10327` empty, `~10482`
  non-empty): `B` with drain flag clear; value32 packs resident mask in bits
  16..31 and effective load mask in bits 0..15.
- `Core/Bank/Scene/Autosave.c` drain witness (`~686`): `B` with drain flag set;
  value32 packs resident mask in bits 16..31 and payload offset 10 in bits
  0..15. Fires once per capture at `payload_offset == 10`.
- `tools/decode_devlogs.py`: `B` stage decode (commit vs drain, resident mask,
  effective load mask / offset).
- `tools/verify_bank_autosave.py`: read-only validator that parses `.hcnames`,
  `settings.cfg`, and both `.hcprms` records; checks CRC32C/header/generation
  selection, the full 129-row HCNAMES projection, Bank metadata, child mask, and
  bounded Scene/Kit/Instrument payloads, and prints the raw Bank section at
  offsets 3920..3934 on failure.

### Wire geometry (authoritative)

Bank section absolute offsets: restore slot 3920..3921, name 3922..3929, scene
present mask 3930..3931, active scene 3932, voice edit mask 3933..3934. Scene 0
payload begins at absolute 4048. These match `Autosave.h`
(`AUTOSAVE_PAYLOAD_OFFSET = 64 + 3856 = 3920`), the validator, and the firmware
getter/marker/drain offsets.

---

## 4. Hardware evidence

The first attempt was confounded by a stale image: the `SD_CARD`/`build` image
was 378,372 bytes and timestamped 16:30, predating the 18:39-18:46 source edits,
so the first "load a Bank" run still showed `active_bank=12` and
`scene_present_mask=0x0000`. After rebuilding, the image is 378,876 bytes
(SHA-256 `B8FD2FDF...`), and the re-tested card confirms both targets.

Bank 008 "Full" fixture (16 children 00..15, `active_scene=6`,
`scene_mask_voice_edit=0x0040`):

- `settings.cfg`: `active_bank=8`.
- `.hcprms1` (generation 3) and `.hcprms2` (generation 4, winner) Bank section
  at 3920..3934: `08 00 | 46 75 6C 6C 00 00 00 00 | FF FF | 06 | 40 00` —
  restore slot 8, "Full", scene present mask `0xFFFF`, active Scene 6, voice
  edit mask `0x0040`.
- Trace `B` at Bank commit: resident mask `0xffff`, effective load mask `0xffff`.
- Trace `B` at AutoSave drain: resident mask `0xffff`, payload offset 10.
- `tools/verify_bank_autosave.py SD_CARD 8`: PASS.

A first boot with the rebuilt image failed before Bank Load with `bootlog.bin`
equal to `KQ019KST`: the boot Kit library index generation
(`filesystem_createLibraryIndexBlocking(FS_LIBRARY_INDEX_KIT)`) was streaming
root Kit slot 019 `kitset.kcg` one byte at a time under a single ten-second
`KITQUAR` deadline. The Kit 019 fixture is structurally valid, so this is an
unnecessarily broad/expensive boot gate, not a corrupt Kit. On retry boot
completed and produced the verified results. The `bootlog.bin` token is stale
failure evidence because a successful boot does not clear a prior boot-failure
record.

## 5. Build and RAM

Session 052 adds no retained RAM: the `B` witness reuses the existing eight-byte
trace record/ring, and the no-op dirty-mark fallback reuses the existing
canonical mutation mask. `SRAM_MANIFEST.md` notes the linked totals remain the
Session 051 baseline until the ARM toolchain is available to regenerate them.
The image grew 378,372 -> 378,876 bytes; exact text/data/bss figures and the
logging-off build have not been re-measured in this environment because
`arm-none-eabi-gcc` is not installed.

## 6. Remaining work and deferred refactor targets

`SCOPING_TARGETS.md` now owns these deferred items:

- Boot sanitation vs load validation (`KQ019KST`): remove root Kit
  `kitset.kcg` parsing / six-member opens / Kit quarantine from the boot index
  pass; move full content validation to the actual load attempt; canonicalize
  `NNN Name` folders and Instrument stems; generate `.hcindex` from the
  post-sanitization scan.
- P1 — Bank Save present-mask overwrite (`filesystem.c:13945`): apply the union
  `bank_setScenePresentMask(bank_scenePresentMask() | op_bank_scene_save_mask)`
  so a partial Save:[Bank] cannot shrink the resident mask.
- P2 — unconditional settings mark: the three `filesystem_markSettingsDirty()`
  calls produce one redundant value-idempotent `settings.cfg` rewrite per boot;
  accept it deliberately or gate it on `fs_settings_runtime_ready`.
- P3/P4 — the `B` witness is logging-only and the logging-off image must still
  be built/verified; `/asavetrc.bin` and `/bootlog.bin` are `*.bin`-gitignored,
  so the decoded results live in `SCOPING_TARGETS.md` rather than as artifacts.

- Outstanding Bank fixtures from `SESSION_052_PRE_PLAN.md` Section 7 remain:
  selective (partial-child) Bank Load present-mask union (fixture 2),
  reboot-restore of the newly loaded Bank (fixture 6), and Bank
  Save-then-reboot `active_bank` follow (fixture 7). Only the full 16-child
  Load (fixture 1) was run.

## 7. Documentation close-out

The disposable planning/analysis files `SESSION_052_PRE_PLAN.md` and
`SESSION_052_POST_ANALYSIS.md` are superseded by this handoff and the durable
reference set:

- `AUTOSAVE.md`: scene-present-mask wire contract, equal-union re-mark, and the
  settings-dirty mark after Bank Load/Save.
- `FILESYSTEM_SPEC.md`: Bank Load/Save now mark the settings writer, and Bank
  Load's present mask is a union that is re-marked on an equal result.
- `MODULE_INTERCHANGE_SPEC.md`: Bank Load/Save to
  `filesystem_markSettingsDirty()` affiliation.
- `DEV_MODES.md`: `B` trace stage and its commit/drain value layout.
- `SRAM_MANIFEST.md`: no Session 052 retained allocation; totals pending
  toolchain regeneration.
- `SCOPING_TARGETS.md`: Session 052 discovery/retry evidence and the deferred
  boot-sanitizer, Bank-Save, and settings-mark refactor targets.
