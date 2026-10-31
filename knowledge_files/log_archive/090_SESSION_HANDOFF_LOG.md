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
