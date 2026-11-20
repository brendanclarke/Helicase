# S097 Inter-Session Volatile Notes

## Open items carried forward from S097

- **FS_OS=1 pending user decision:** the `FS_OS` Makefile switch (default 0)
  compiles `filesystem.c`, `filesystem_helpers.c`, `MenuPageGroups.c`,
  `AutoSave.c` at `-Os`, saving an additional ~36 KiB (678,740 B total).
  Measured as instruction-neutral for DSP hot loops (±1 instruction LTO
  jitter). User must decide whether to enable by default.

- **Bank Save mask ordering fix (P3 §13) needs device confirmation:**
  `filesystem_start()` now runs before masks are set, fixing the race where
  masks were set after the operation started. Verify on hardware that Bank
  Save writes the correct set of Scenes.

- **Boot notice non-blocking overlay (P3 §14) needs device confirmation:**
  `menu_lcdOverlayRoom()` replaces the blocking boot draw. Background CPU
  budget raised 25 -> 30 us/ms (3% while playing). Verify boot notice
  appears correctly and does not delay audio start.

- **Held OK replay needs device confirmation:** `menu_loadSaveOkRetry`
  queues a replay when OK is held during a finishing operation. Verify on
  hardware with a held-OK during save.

- **LTO jitter from COLD_LAST link order:** cold UI/storage objects now
  link after DSP objects. This isolates DSP code generation but each cold
  code change can still move hot DSP loops +/-1 instruction due to LTO
  partition rebalancing. This is an inherent LTO property, not a bug.

## Flash budget after S097

- Default build (FS_OS=0): **714,980 B** (headroom **38,684 B**, above
  16 KiB warning threshold)
- With FS_OS=1: **678,740 B** (headroom **74,924 B**)
- ITCM: unchanged at 15,976 / 16,384 B (408 B free)
- SRAM1 .bss: 288,900 B (unchanged from S096)

## Toolchain notes

- `OS_SRCS` Makefile variable: five cold boot/install/FAT files compile
  at `-Os` instead of `-O2`. Currently: `filesystem.c`,
  `filesystem_helpers.c`, `MenuPageGroups.c`, `AutoSave.c` are candidates
  when FS_OS=1 is enabled; without it, `diskio.c`, `ff.c`,
  `flashImage.c`, `main_boot.c`, `sampleFlash.c` are the five active
  `-Os` files.
- `COLD_LAST` link order: cold objects link after DSP objects, isolating
  DSP code generation from cold-code churn.

## Decisions deferred

- FS_OS=1 default: awaiting user decision. The measurement shows no DSP
  regression, but the user may want a device trial first.
- SOM generator: archived to `archive/som/` (dead code). SOM_PAGE id
  kept to preserve EFFECT_PAGE enum value. If SOM is ever revived, source
  is in the archive.

## Notes for the next session

- `ParamDescriptor` is now 20 B (was 28 B). Fields `short_name`,
  `long_name`, `category` replaced by `uint8_t` indexes `short_id`,
  `long_id`, `cat_id` into pools in `MenuLabels.c`. Access via
  `menuLabel_short()`, `menuLabel_cat()`, `menuLabel_long()`.
- `menuPages` is now `menuStaticPages[6][8]` + `menuStaticPageSlot[17]`.
  Use `menu_pageRow()` accessor.
- `filesystem_saveGlobals_tick()` and `saveMidiLearn_tick()` merged into
  `filesystem_safeTextWrite_tick()`.
- `filesystem_blockOpenMode(name, mode)` replaces three block-open
  variants (`blockOpen`, `blockOpenAppend`, `blockOpenRewrite`).
- DEV trace stages `'f'` (filesystem pass timing) and `'k'` (Load/Save
  click outcomes) are new in S097.
