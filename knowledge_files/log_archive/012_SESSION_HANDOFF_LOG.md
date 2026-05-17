# Session 12 — asyncfatfs Implementation + Bug Fixes (2026-05-07/08)

## Session Goal

Implement the asyncfatfs integration designed in Session 11: drop in the library, write the sdcard shim, sd_fsm state machine, rewrite presetManager and kitBrowser against the async API, wire into main.c boot and main loop.

## What Actually Happened

The implementation was completed and compiles. Multiple hardware test iterations were required to find and fix bugs. The session uncovered two pre-existing bugs in the DSP layer that had been masked by the boot-time kit (P000/P001) not exercising certain code paths.

### Implementation

1. **asyncfatfs library** dropped into `Core/Hardware/SD/`. Modifications from upstream: removed `AFATFS_USE_FREEFILE`, `AFATFS_USE_INTROSPECTIVE_LOGGING`, `AFATFS_MIN_MULTIPLE_BLOCK_WRITE_COUNT`, `stdlib.h`, `signal.h`. Changed include from `sdcard.h` to `sdcard_lxr02.h`.

2. **sdcard_lxr02.c/h** — SD card driver shim. Implements `sdcard_readBlock`/`sdcard_writeBlock`/`sdcard_poll` over `spi_sd.c` bit-bang. Sends CMD17/CMD24 directly via `SPI_transmit()` (not via `SD_sendCommand()` which deasserts CS after the response — incompatible with multi-byte data transfer). 16 bytes/burst per `sdcard_poll()` call. Handles SDHC block addressing via `SDHC_flag`.

3. **sd_fsm.c/h** — Operation state machine. Serializes all asyncfatfs calls. Six operations: LOAD_KIT, SAVE_KIT, LOAD_GLOBALS, SAVE_GLOBALS, SCAN_KITS, LOAD_NAME. Staging buffer for bulk writes (~280 bytes). Completion callbacks to presetManager.

4. **presetManager.c/h** — Rewritten. All FatFS calls replaced with `sd_request_*()` posts. Added `preset_status_t` enum (`PRESET_IDLE`, `PRESET_LOAD_IN_PROGRESS`, `PRESET_UPDATE_READY`) and `preset_op_type_t` for the menu to poll completion. Completion callbacks from sd_fsm set status to UPDATE_READY. Menu applies post-load logic (mod target validation, DSP parameter send, repaint) when it sees UPDATE_READY via `menu_pollPresetStatus()`.

5. **kitBrowser.c** — Rewritten. `kb_map[]` and `kb_numKits` exposed for direct write by `sd_fsm_scanKits_tick()`. Name reads async via `sd_request_load_name()`.

6. **menu.c** — Added `menu_pollPresetStatus()` called from main loop. Handles all post-load/save work that previously happened synchronously: mod target validation, `preset_sendDrumsetParameters()`, `menu_TargetVoiceGapIndex` update, `menu_repaintAll()`, `menu_sendAllGlobals()`, `menu_resetSaveParameters()`. Fixed load page to skip separate `preset_loadName()` when kit load follows (FSM is single-operation; the name load would block the kit load).

7. **main.c** — Boot sequence reordered: `SD_init()` → `sd_fsm_init()` → synchronous polling for MBR/VBR → kit scan → kit 0 load → globals load → `audioCodec_init()`. All boot SD operations use synchronous polling loops (safe: audio not running yet). Main loop: `sd_fsm_tick()` + `menu_pollPresetStatus()` added.

8. **sdTest.c** — `sd_do_init()` stubbed (FatFS removed). Raw CMD0 probe still works.

9. **Makefile** — Removed `ff.c`, `diskio.c`. Added `asyncfatfs.c`, `fat_standard.c`, `sdcard_lxr02.c`, `sd_fsm.c`.

### Bugs Found and Fixed

**Bug 1: `preset_morph()` index 127 memory corruption (pre-existing, newly exposed)**

`preset_morph()` sends CCs for all parameters. At index 127, `frontPanel_sendData(MIDI_CC, 127, val)` encodes `data1 = (127+1) & 0x7f = 0`. `midiParser_ccHandler` then computes `paramNr = data1 - 1 = 0 - 1 = 65535` (uint16 underflow). This writes to `midiParser_originalCcValues[65536]` — a wild write past the 255-byte array.

In the original two-MCU LXR, this went over UART where the +1 encoding was consumed by the receiving side. In the single-MCU port, encoding and decoding happen in the same address space and the wrap causes memory corruption.

This bug existed since `frontPanel_sendData` was wired to call `midiParser_ccHandler` directly (Session 7/8), but was masked because `preset_morph()` was only called during blocking boot-time kit loads where the corruption target varied by BSS layout and often hit harmless memory. The asyncfatfs changes moved `preset_morph()` to be called from `menu_pollPresetStatus()` in the main loop, changing the BSS layout enough to make the corruption hit critical audio state.

**Fix**: Skip index 127 in the `preset_morph()` loop. Parameter 127 (`PAR_RESERVED4`) is unused.

**Bug 2: HiHat VLA stack corruption (pre-existing, newly exposed)**

`HiHat_calcSyncBlock()` had `int16_t mod1[size], mod2[size]` — VLAs on the stack. Session 8 fixed VLAs in Snare and Cymbal but missed HiHat. With `-O2`, the compiler doesn't emit proper variable-length stack frame allocation, causing silent stack corruption that overwrites the caller's frame.

This was masked because P000/P001 (the boot kits) have a hi-hat configuration that produces no audible output (the known "no hi-hat at startup" issue from README). Loading any other kit (P002+) activated the hi-hat render path, triggering the VLA corruption.

**Fix**: Replaced with `static int16_t mod1[OUTPUT_DMA_SIZE], mod2[OUTPUT_DMA_SIZE]`, same pattern as Snare/Cymbal from Session 8.

**Bug 3: sd_fsm read stuck at EOF**

When a .SND file is shorter than `END_OF_SOUND_PARAMETERS` (some original kits are 229 bytes = 221 data bytes, vs END_OF_SOUND = 228), `afatfs_fread()` returns 0 at EOF but the phase waited for `op_bytes_done >= END_OF_SOUND_PARAMETERS` which never happened. The FSM spun forever at phase 3.

**Fix**: Added `(n == 0 && op_bytes_done > 0)` check — if fread returns 0 after we've already read some data, we're at EOF. Phase advances. Same pattern applied to all read phases (kit params, kit name, globals, name-only load).

**Bug 4: FSM collision — loadName blocks loadKit**

On the load page, `menu.c` called `preset_loadName()` then immediately `preset_loadDrumset()`. The name load occupied the single-operation FSM, so the kit load request was silently rejected.

**Fix**: On the LOAD_PAGE, skip the separate name load when a kit load follows — the kit load FSM reads the name itself in phase 2. Save page still uses separate name load.

### What Was NOT the Problem

- **D-cache coherency**: Investigated extensively. D-cache is not explicitly enabled in our code or the bootloader. Disabling it had no effect. Not the issue.
- **BSS layout sensitivity**: Build-dependent audio corruption was observed (adding/removing diagnostic variables changed whether audio worked). Root cause was Bug 1 (wild write at index 65536) hitting different memory targets depending on BSS layout. Not a cache or alignment issue.
- **SD SPI speed**: Slow vs fast SPI was investigated. Slow SPI made boot loads slower but didn't fix the core issues. Not the root cause.
- **asyncfatfs idle overhead**: Measured at ~200 cycles per main loop iteration (the ~15,700 cycle peak measurement included TIM6 ISR landing inside the measurement window). Negligible vs the 471,288 cycle render budget.

## Hardware Verification

- ✅ Boot with SD card: kit 0 loads, globals load, menu functional
- ✅ Boot without SD card: no crash, menu works, no underruns
- ✅ Kit save from menu: parameter change persists across reboot
- ✅ Kit load from menu: parameters and sound change correctly (after all bugs fixed)
- ✅ Kit load of shorter files (229 bytes): EOF handled, no hang
- ✅ Zero underruns at idle after boot
- ⚠️ Small burst of underruns during kit load (~2 sector reads via blocking CMD17) — acceptable for now, will be eliminated when sdcard_readBlock is made fully non-blocking or moved to TIM5 ISR

## End of Session Block

```
DATE: 2026-05-08
SESSION GOAL: Implement asyncfatfs integration (Session 11 design)
COMPLETED:
  - Full asyncfatfs integration: sdcard_lxr02 shim, sd_fsm state machine,
    presetManager rewrite, kitBrowser rewrite, menu async completion polling,
    main.c boot sequence and main loop wiring
  - Fixed preset_morph() index 127 memory corruption (pre-existing)
  - Fixed HiHat VLA stack corruption (pre-existing)
  - Fixed sd_fsm EOF handling for short .SND files
  - Fixed FSM collision (loadName blocking loadKit)
  - Removed ChaN FatFS from build (ff.c, diskio.c)
VERIFIED ON HARDWARE: yes — boot, kit load/save, globals load/save, menu navigation

CHANGES THIS SESSION:
- Core/Hardware/SD/asyncfatfs.c: upstream + LXR-02 modifications (no freefile, no introspective logging, no multi-block write)
- Core/Hardware/SD/asyncfatfs.h: upstream, unchanged
- Core/Hardware/SD/fat_standard.c/h: upstream, unchanged
- Core/Hardware/SD/sdcard.h: upstream interface definition, unchanged
- Core/Hardware/SD/sdcard_lxr02.c/h: NEW — SD card driver shim over spi_sd.c
- Core/Hardware/SD/sd_fsm.c/h: NEW — operation state machine
- Core/Hardware/SD/kitBrowser.c: REWRITTEN — async via sd_fsm
- Core/Hardware/SD/sdTest.c: sd_do_init() stubbed (FatFS removed)
- Core/Preset/presetManager.c/h: REWRITTEN — async via sd_fsm, added preset_status_t
- Core/Menu/menu.c: added menu_pollPresetStatus(), fixed load page FSM collision
- Core/Menu/menu.h: added menu_pollPresetStatus declaration
- Core/DSPAudio/HiHat.c: VLA fix (mod1/mod2 → static arrays)
- main.c: boot sequence reordered (SD before audio), sd_fsm_tick + menu_pollPresetStatus in main loop
- Makefile: removed ff.c/diskio.c, added asyncfatfs/fat_standard/sdcard_lxr02/sd_fsm

KNOWN ISSUES INTRODUCED: none

KNOWN ISSUES RESOLVED:
- SD card operations no longer block the main loop (was Critical issue #0)
- HiHat VLA stack corruption fixed
- preset_morph() index 127 memory corruption fixed

NEXT SESSION RECOMMENDED GOAL: Investigate and fix the "no hi-hat at startup" bug.
  The hi-hat works after loading any kit from menu, but not after the boot-time
  kit load. Likely an initialization ordering issue — some hi-hat DSP state isn't
  set by the boot path that IS set by the CC dispatch in preset_morph(). Now that
  the VLA and index-127 bugs are fixed, this can be investigated cleanly.

BLOCKERS: none

CRITICAL REMINDERS FOR NEXT SESSION:
- preset_morph() skips index 127 — do not remove the skip. See Bug 1 above.
- HiHat VLA fix: mod1/mod2 are static arrays. Do not revert to VLAs.
- sd_fsm is single-operation. Do not post two requests without waiting for completion.
- sdcard_lxr02.c sends CMD17/CMD24 directly — does NOT use SD_sendCommand() (which deasserts CS).
- afatfs_fread already clamps reads to file size internally (line 3108). Do not request reads past EOF anyway — the fread-returns-0 check handles short files.
- ff.c/diskio.c still exist on disk but are NOT compiled. Do not re-add to Makefile.
- Boot sequence: all SD operations complete BEFORE audioCodec_init(). Do not reorder.
- preset_morph() is called from menu_pollPresetStatus() context, not from sd_fsm.
- menu_pollPresetStatus() is called every main loop iteration — it early-returns when status != UPDATE_READY.
```
