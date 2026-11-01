# Session 090 P1 — Handoff Log

**Date:** 2026-10-31
**Branch:** `dev-ph7-voice_expansion`
**Base commit:** `7f8b5d07` (S090 initial plan)
**Build:** `text 683,616`, `data 420`, `bss 413,020`; image payload
684,036 B, flash headroom 69,628 B
**ITCM:** 15,240 / 16,384 B. **DTCM statics:** 4,372 B. **FXBUF margin:**
3,808 B

**Plan:** `S090_P1_LOAD_SAVE_SPEEDUP.md` (single authority for decisions).
**Schedule:** `S090_P1_IMPLEMENTATION.md` (81 changes, all applied).

---

## Goal

Make Load/Save file-name rows appear sooner and stay browsable while audio
plays. The trace showed a nested Instrument entry spending 2.7 s on
`.hcnames`, 9-10 s writing `.hctmp.<ext>`, and 2.4-3 s on `.hcindex`, with one
16-byte bit-banged burst per main-loop pass. With no constant CPU headroom,
the fix is fewer card operations, not faster ones.

## User decisions applied

| # | Decision | Applied as |
|---|---|---|
| U4/U5/U9 | 129 B RAM snapshot replaces `.hctmp` | `pm_instrument_snapshot` (Preset, SRAM1 `.bss`) |
| U6 | No wrong names, ever | reuse only through `hcnames_mirror_valid` VALID or the browser-cache tag with an idle facade |
| U12 | Do not change shared-cache users unless needed | AutoSave, Pattern, Bank, copy/clear and boot uses unchanged |
| U13-U15 | 3a, 3b, 4a, 5e withdrawn | not implemented |
| U17 | Type changes must not wait | item 7: type steps admitted over an index read; stale read cancelled |
| U18 | Page display unchanged | names blank while busy; only the type label follows at once |
| U19 | No added underrun risk | no main-loop work added; card work removed or cancelled |
| U20 | One implementation, no side quests | trace format, decoder and `AutosaveTrace.h` unchanged |
| Q1 | Serve Menu `.hcnames` reads from a VALID mirror | `filesystem_residentNames_tick()` phase 0 + request keep-valid |
| Q2 | Existing gates replace item 5 | no op table, no build check, no 1 B stale flag |
| Q3 | 6a-i yes, 6a-ii no | same-type index reuse; cache still cleared on exit |
| Q4 | Cancel the stale index read | two cancel paths in `filesystem.c` |
| Q5 | `kit` restore keeps no-AutoSave-mark behaviour | apply-image mark argument `0u` |
| Q6 | Morph restore helper | shared `preset_armInstrumentMorphCommit()` |

## Implemented changes

- **Preset (`presetManager.h/.c`):** removed the three `PRESET_OP_*TEMP*`
  values and the four temp functions; added the 129 B snapshot, the cell rule
  `preset_snapshotCellRestored()`, `preset_captureInstrumentSnapshot()`,
  `preset_restoreInstrumentSnapshot()` and
  `preset_restoreInstrumentMorphSnapshot()`. The Morph commit tail is now
  `preset_armInstrumentMorphCommit()`, shared with
  `preset_startInstrumentMorphApply()`.
- **Filesystem:** removed the `.hctmp` request/save/load paths, the two temp
  flags and the temp-origin getters. `FS_INTERNAL_OP_SAVE_INSTRUMENT_TEMP` is
  kept as a retired value for decoder numbering. Added
  `filesystem_browseReadRunning()`, `filesystem_cancelBrowseRead()`,
  `filesystem_browseReadWasCancelled()` and
  `filesystem_instrumentNameCacheLoaded()`, plus callback-free cancel phases in
  both index readers. Menu `.hcnames` loads finish DONE from a VALID mirror at
  phase 0. The reserved `.hctmp.<ext>` name filter stays.
- **Storage types:** removed `STORAGE_INSTRUMENT_SAVE_MORPH_SNAPSHOT` and its
  serializer branch.
- **Menu:** RAM capture/restore for the `kit` row; snapshot restore waits for
  the same idle point the file completion had; index cache hit on same-type
  voice changes; type steps admitted over an index read with an immediate
  repaint; stale reads retired quietly; cache clears left over from before
  S058 Option 1C removed from the LSR-01 flush and the `.hcnames` request.
- **RAM:** +129 B snapshot, -2 B temp flags; one 196 B stack staging slot only
  during a restore; cancel flag reuses `op_bank_flags`.

## Verification completed

- All 81 schedule anchors, first/last quoted lines and code blocks validated
  against the tree before editing; applied bottom-up per file.
- `make all && make img`: exit 0; numbers equal the schedule exactly.
- Warnings: same set as baseline (20 lines, including the seven pre-existing
  `-Wunused-function` in `filesystem.c`).
- `nm`: `pm_instrument_snapshot` = `0x81`; no
  `op_instrument_load_temporary`, `op_instrument_save_temporary`,
  `*InstrumentTemp*` or `*WasTemporary*` symbol remains except the kept
  `filesystem_isReservedInstrumentTemporaryName`.
- Logging off (`DEV_MODE_LOGGING 0`): text 666,920, data 420, bss 396,184; no
  additional warning. Restored to 1 and rebuilt to the numbers above.

## Pending

- Hardware matrix H1-H27 of `S090_P1_IMPLEMENTATION.md` §8.2 (user's check):
  no `.hctmp` activity, exact `kit`/Morph restore, no `.hcnames` read after
  boot, same-type voice changes with no SD access, type steps over reads,
  cancelled reads producing a complete fresh list, and no underrun.
- Remaining reported-only findings from schedule §1 D13 were left untouched.

## Documents updated

- `FILESYSTEM_SPEC.md`: RAM `kit` snapshot, VALID-mirror entry reads, same-type
  index reuse, type-step binding rule, stale `.hctmp` files safe to delete.
- `STORAGE_SRAM_MANIFEST.md`: S090 allocation and build numbers.
- `MEMORY.md`: status line, name-reuse rule, `kit` snapshot rule, type-step
  rule, rejected index-pool approach.
- `S090_P1_IMPLEMENTATION.md`: implementation progress notes.
- Not changed: `MODULE_INTERCHANGE_SPEC.md` (no boot-order change),
  `DEV_MODES.md`, `AutosaveTrace.h`, `decode_devlogs.py`.

## Files changed

- `Core/Bank/Scene/Preset/presetManager.h`
- `Core/Bank/Scene/Preset/presetManager.c`
- `Core/Hardware/SD/filesystem.h`
- `Core/Hardware/SD/filesystem.c`
- `Core/Hardware/SD/storageTypes.h`
- `Core/Hardware/SD/storageTypes.c`
- `Core/Menu/menu.c`

## Post-implementation assessment and fixes (2026-10-31)

- Assessment: `S090_P1_IMPLEMENTATION.md` §13. The implementation conforms;
  the tree image matched the source.
- Fixes applied (§13.5):
  - A1: four blank separator lines restored (the schedule's renderer had
    dropped them).
  - A2: `menu_libraryIndexLoadComplete()` clears the browser cache when the
    current read fails, so a failed Kit/Scene/Bank/Pattern read no longer
    leaves its domain tag over empty rows.
  - A3: the S090 arithmetic in `STORAGE_SRAM_MANIFEST.md` was corrected.
- Build after the fixes: text 683,648, data 420, bss 413,020; payload
  684,068 B; `build/LXRV2_lxr02.img` 684,084 B. Hardware H1–H27 pending.

## P2 — Load/Save drops AutoSave; start-up work before boot (2026-10-31)

**Goal:** `S090_P2_LOAD_SAVE_ATS_DROP_PRE-BOOT_WORK.md` (R1–R6, D1–D5).
Start-up AutoSave work must finish before `audioCodec_init()` (full CPU,
synchronous, power-loss safe); entering Load/Save must drop a running drain
as soon as its callbacks allow; no autonomous AutoSave write after boot until
the user changes something; a failed settle must not be silent (the DEV trace
is the evidence, D4).

**Implemented** from `S090_P2_IMPLEMENTATION.md` (all 30 changes):

- **Boot stage 13:** `filesystem_settleAutosaveBlocking()` between
  `liveRec_init()` and `audioCodec_init()`. It makes every AutoSave debounce
  due, uses uncounted ticks, runs `patSvc_tick()` and the filesystem tick until
  no class is pending and the facade is idle, and includes a pending
  `settings.cfg` write. `bank_revalidateVoiceEditMasks()` moved before it;
  `preset_startDrumsetApply()` stays after the codec.
- **Budget:** `filesystem_backgroundBudgetAvailable()` reports full budget and
  `filesystem_backgroundBudgetCharge()` does not deduct while
  `audioCodec_isInitialized()` is 0 (lifetime flag, not suspend).
- **Bounds:** 30 s without a progress-signature change (`BOOT_SETTLE_NO_PROGRESS_MS`),
  120 s overall (`BOOT_SETTLE_CAP_MS`); after either, admission stops, counted
  ticks let the existing detectors/hooks unwind. One attempt per class; the
  stage never cuts a callback.
- **Load/Save drop:** the scalar and Pattern drain abort checks also fire on
  `LOAD_PAGE`/`SAVE_PAGE`. The scalar cut set adds HCNAMES 70, 77 and 72 while
  rows remain; 73–76 are never cut (74 would leave a promotable temp).
- **Boot hold:** 1 B `autosave_boot_hold` (SRAM1, `Autosave.c`), set only when
  an AutoSave class is still owed at stage exit; cleared by the two scalar
  funnels, the semantic Pattern funnel and a tracking lifecycle change. The
  three schedulers honour it.
- **Trace:** `s` START/FAIL/END records and decoder support. No LCD notice
  (D4).

**Static verification:** `make all && make img` exit 0; text 684,968, data
420, bss 413,020; payload 685,388 B (+1,320); ITCM 15,240; DTCM statics
4,372; FX margin 3,808. Logging off: text 668,488, bss 396,176. `nm`:
`autosave_boot_hold` 1 B and `filesystem_settleAutosaveBlocking` present;
the two accessors are LTO-inlined. Decoder compiles and decodes synthetic
START/FAIL/END records. No new compiler warning (the 15 reported are all
pre-existing).

**Pending:** hardware P1–P17 (`S090_P2_IMPLEMENTATION.md` §10.2). No host test
exists for boot/menu/filesystem sequencing.

**Documents updated:** `AUTOSAVE.md` (settle, drop cut set, hold, no post-boot
work), `FILESYSTEM_SPEC.md` (session note + Load/Save binding bullet),
`MODULE_INTERCHANGE_SPEC.md` (boot order), `STORAGE_SRAM_MANIFEST.md`
(1 B hold), `DEV_MODES.md` (`s` record), `MEMORY.md` (status + rules),
`S090_P2_IMPLEMENTATION.md` §14.

**Files changed:** `config.h`, `Core/Hardware/AudioCodecManager.c/.h`,
`Core/Bank/Scene/Pattern/PatternStackService.c/.h`,
`Core/Bank/Scene/Autosave.c/.h`, `Core/Bank/Scene/AutosaveTrace.h`,
`Core/Hardware/SD/filesystem.c/.h`, `main.c`,
`tools/diagnostic_tools/decode_devlogs.py`.

## P3/P4 — defect re-tests (2026-10-31 / 2026-11-01)

- **P3** (`S090_P3_LOAD_SAVE_DEFECT_CORRECTION.md`): Save-page names, input
  during a list read, HCNAMES stall bound (DEV), boot directory tail trim. The
  trim worked on SD_CARD_TEST_5 (920 root entries cut), and the HCNAMES
  publish now completes.
- **P4** (`S090_P4_STILL_NOT_FIXED.md`, SD_CARD_TEST_5):
  - **User report:** type changes still take 5–6 s; Scene Save still never
    completes (well over 20 s, PLAY running).
  - **Cause of the slowness:** with audio running the facade gets one tick
    and one 16-byte poll per main-loop pass (~460/s). Text rows, lines and
    directory entries were one per tick: a 1,000-row `.hcindex` cost ≥1,000
    passes for three sectors.
  - **The Save stopped** between the HCNAMES publish and the `.hcindex`
    writer's open (Flush, `SCAN_SCENES`, or `CREATE_LIBRARY_INDEX` 0–4).
    None of those had a production bound.

## P5 — Load/Save speed and no-freeze (2026-11-01)

**Goal:** the user's Q1–Q5 answers to P4: as much speed as can never break
audio or freeze; "50% more" budget; CPU widget 99–100 % (no free render
time, so no extra SD polls per pass).

**Implemented** (`S090_P5_IMPLEMENTATION.md`):

- **F1 batching:** the index read/write, HCNAMES read/write, the text line
  writer and the four library scans loop over cached work per tick, until the
  first card wait or 15 µs (`FS_LOADSAVE_BATCH_BUDGET_US`; 500 µs pre-audio).
- **F2 idle window:** `main_serviceBackgroundLoadIdle()` also serves Load/Save
  (`menu_loadSaveStorageWaiting()`), 300 µs, only with both render slots full.
- **F4 watchdog (every build):** a covered Load/Save operation with no
  progress-signature change for 12 s finishes ERROR with code
  `W<op><phase>`; it closes the handles it holds. 2 B.
- **F5 (logging only):**
  - stage `w`, one record per facade owner change, with durations in the
    decoder;
  - the stall snapshot gains a site parameter (site 12) and an open-handle
    count. 2 B.
- **Not built:** F3 (extra polls per pass). The root-cause fix of the hang
  waits for the next `W…` code and snapshot.

**Static verification:** `make all && make img` exit 0; text 690,072, data
420, bss 413,028; flash headroom 63,172 B; logging/stall off builds clean;
host test of the batched list read on the four TEST_5 indexes passes; the
decoder decodes synthetic `w` and site-12 records.

**Pending:** hardware T1–T9 (`S090_P5_IMPLEMENTATION.md` §6).

**P5 hotfix (2026-11-01, SD_CARD_TEST_6):** the first P5 image froze at boot.
The `w` witness recorded the trace flush's own `FLUSH_FINISH -> idle` edge,
so each trace flush queued the next (every ~11 ms). The boot step after the
boot reader never got the facade, and the boot deadline was re-armed by every
flush. Fixed: only Load/Save-covered owners are recorded, and Flush Finish is
attributed to its entering operation. Host-tested against a
flush-on-pending model. `S090_P5_IMPLEMENTATION.md` §9.
