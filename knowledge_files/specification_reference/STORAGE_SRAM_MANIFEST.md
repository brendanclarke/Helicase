# Storage and SRAM manifest

Where every byte of the STM32F765VIH6's on-chip storage goes: program flash,
sample flash, ITCM, DTCM, SRAM1 and SRAM2. It also records the rules for
changing any of it.

- **Current as of:** Session 075 F2 implementation (2026-10-03),
  `dev-ph6-copyclear`, uncommitted. DEV link: `text=531,936`, `data=416`,
  `bss=426,712`; raw binary 532,352 B and stamped image 532,368 B.
  Production (`DEV_MODE_LOGGING=0`) remains at the F1 snapshot until the
  production configuration is rebuilt. The F1 pass
  adds +124 B production SRAM1 net: early source masks +64 B, restore masks
  +64 B, early flags +1 B and governor credit +2 B, offset by the removed
  group-blink state −7 B. The 9,000 B name cache doubles as copy/clear
  working storage only during an intentional lazy loan (§8.2). Every Scene
  pool keeps a permanent 132 B swap block (§8.2). F2 adds the six-byte
  `fx_send_morph` endpoint array to each Scene, the 4-byte Effect-page voice
  mix overlay record, and the 1-byte overlay TRACK mask; the measured DEV
  `scenes` symbol is `0x65E0`.
- **S074 changes:** +64 B SRAM1 (Scene settings for the bus compressor);
  +32 B DTCM `.dtcmz` (bus compressor state), so the FX arena is −32 B;
  CrumpBit uses 0 B of static RAM (56 B inside the existing 76 B union)
  and 70,592 B of the arena share at run time; the image grew into sector 6
  (§3.2a).
- **Renamed in Session 073** from `SRAM_MANIFEST.md`. The flash and sample
  flash material came from the Session 073 flash expansion
  (`073_SESSION_HANDOFF_LOG.md` §4).
- **SD card storage** is covered by other documents (§9).

---

## 1. How to read and refresh these numbers

- Build clean before trusting any total: `make clean && make all`. Use
  `make all`, not bare `make`: in an incremental tree bare `make` can stop at
  `build/main.o`.
- Flash, ITCM, DTCM and arena use:
  `python3 tools/link_budget.py arm-none-eabi-nm build/lxr02.elf`

  ```
  Flash : 532,352 / 753,664 B used, headroom 221,312 B
  ITCM  : 4,168 / 16,384 B
  DTCM  : statics 4,472 B
  FXBUF : 126,592 B at 0x20001180 (min 122,880, margin 3,712)
  ```

  It warns when flash headroom falls below `LINK_BUDGET_WARN_FLASH`
  (default 16,384 B). The flash limit comes from the linker symbol
  `__sample_flash_start`.
- Sections: `arm-none-eabi-size -A build/lxr02.elf`. Symbols:
  `arm-none-eabi-nm -S --size-sort build/lxr02.elf`. Segments:
  `arm-none-eabi-readelf -l -W build/lxr02.elf`.
- **The `bss` column of `arm-none-eabi-size` includes the 126,592 B NOLOAD
  DTCM arena.** It is not SRAM1 use. Use the section ledger (§5).
- **Record the `.img` hash, not the `.bin`.** Since S074 `lxr02.bin` is the
  raw, unstamped objcopy output; the stamped payload exists only inside
  `build/LXRV2_lxr02.img` (§3.4).
- Individual C objects can be merged, removed or padded by LTO and
  alignment. Use section totals for capacity.

---

## 2. Memory map

| Region | Address range | Size | Contents |
|---|---|---:|---|
| Flash sector 0 | `0x08000000–0x08007FFF` | 32 KiB | LXRV2 bootloader (closed vendor binary; the only recovery path) |
| Flash sectors 1–6 | `0x08008000–0x080BFFFF` | 736 KiB | Application image (§3) |
| Flash sectors 7–11 | `0x080C0000–0x081FFFFF` | 1,280 KiB | User samples (§4) |
| ITCM | `0x00000000–0x00003FFF` | 16 KiB | Hot oscillator code copied from flash at boot (§6) |
| DTCM | `0x20000000–0x2001FFFF` | 128 KiB | DSP statics, then the FX/voice audio arena (§7). Not DMA-accessible. |
| SRAM1 | `0x20020000–0x2007BFFF` | 368 KiB | DMA buffers, `.data`, `.bss` (§5, §8) |
| SRAM2 | `0x2007C000–0x2007FFFF` | 16 KiB | The main stack, growing down from `_estack = 0x20080000`; an optional 12 B logging capsule |

Flash is single-bank (confirmed by memtest). Sectors 0–3 are 32 KiB, sector 4
is 128 KiB, sectors 5–11 are 256 KiB.

---

## 3. Program flash

### 3.1 Application window

- **Window:** `0x08008000–0x080BFFFF` (753,664 B), sectors 1–6. Session 073
  added sector 6 (it was sample storage before); the window was 480 KiB.
- **Use at S075 F2 DEV link:** 532,352 B; **headroom 221,312 B**.
- **History:** 34,356 B free at S072 Step 1; 8,080 B at the S072 close;
  269,504 B after the S073 expansion; 266,560 B after the S073 DSP
  refactor; 250,736 B at the S074 close; 221,312 B at the S075 F2 DEV link.
  S074 added +15,824 B, of which CrumpBit was 11,600 B.

### 3.2 Image layout

The linker script is `STM32F765VIHx_FLASH.ld`. The image is, in order:

| Section | Size (S075 F2 DEV) | Notes |
|---|---:|---|
| `.isr_vector` | 456 B | At `0x08008000`; VTOR is set at startup |
| `.image_check` | 32 B | Per-sector CRC block (§3.4); `(READONLY)`, so `size` does not count it as data |
| `.text` | 526,768 B (S075 F2 DEV; 497,344 at S074) | `Reset_Handler` first (`KEEP(*(.text.Reset_Handler))`), then code and `.rodata` |
| `.itcm` load image | 4,168 B | Copied to ITCM by `Reset_Handler` |
| `.data` load image | 416 B | Copied to SRAM1 |
| `.dtcm` load image | 512 B | Copied to DTCM (`squareRootLut`) |

- **`_eflash_load`** `= LOADADDR(.dtcm) + SIZEOF(.dtcm)` is the end of the
  image. `lxr02.bin` is exactly `_eflash_load − 0x08008000` bytes, because
  `objcopy -O binary` emits loadable sections only. NOLOAD sections
  (`.dtcmz`, `.dtcm_fxbuf`, `.bss`) never enter the image.
- **`Reset_Handler`** stays at `0x080081E8`, in sector 1, however large the
  image grows.
- **Largest flash objects** (S074 close):

  | Object | Bytes |
  |---|---:|
  | `crashSample` | 32,768 |
  | `transientData` | 26,460 |
  | `sawTable`, `triTable`, `recTable` | 22,528 each |
  | `mixer_calcNextSampleBlock` (all four voice renders, the mixer and the bus compressor inlined by LTO) | 12,204 |
  | `menu_repaintGeneric` | 9,508 |
  | `main` | 9,048 |
  | `sine_table` (in flash since S072) | 8,194 |
  | `filesystem_tick` | 7,924 |
  | `filesystem_loadSceneDirectory_tick`, `filesystem_autosaveParameterDrain_tick` | 7,872 / 7,612 |
  | `crumpBit_process` / `crumpBit_syncDivision` (S074, `-Ofast` unrolled) | 4,804 / 3,684 |

### 3.2a What lives in sector 6 (S074)

The application reached sector 6 (`0x08080000`) for the first time with
CrumpBit (`_eflash_load` `0x08081F68`, 8,040 B in). At the S074 close
`_eflash_load` is **`0x08082C90`, 11,408 B into sector 6**:

| Range | Content |
|---|---|
| `0x08080000–0x080818A7` | the last 6,312 B of `.text`: libm tables (`__exp2f_data`, `__log2f_data`, `__powf_log2_data`), `_ctype_`, `atan` tables, `_init`/`_fini`, and the flash veneers into ITCM |
| `0x080818A8–0x080828EF` | the `.itcm` load image (4,168 B of oscillator code) |
| `0x080828F0–0x08082A8F` | the `.data` load image (416 B) |
| `0x08082A90–0x08082C8F` | the `.dtcm` load image (`squareRootLut`, 512 B) |

Every S074 image has booted and played, with the boot image check silent,
so **the LXRV2 bootloader erases and programs sector 6**. If it did not,
the oscillators, initialised data and pan law would be corrupt, and the
check would report `Img BAD s:.....6`. The user did not run a separate
test.

### 3.3 Linker guards

The link fails rather than produce a bad image:

| ASSERT | Guards |
|---|---|
| `ORIGIN(FLASH) + LENGTH(FLASH) == __sample_flash_start` | The application window ends exactly at the sample floor |
| `_etext <= __sample_flash_start`, `_eflash_load <= __sample_flash_start` | Code and load images never reach sample flash |
| `.image_check` is 32 B and ends below `0x08010000` | The check block stays in sector 1 |
| `Reset_Handler < 0x08010000` | The reset vector stays in sector 1 |
| `.itcm` fits 16 KiB | ITCM overflow |
| `.dma_nocache` ≤ 4,096 B | It must fit MPU region 1 |
| `.dtcm` + `.dtcmz` fit DTCM | DTCM overflow |
| FX arena 32-byte aligned, inside DTCM, ≥ 120 KiB | The approved arena minimum (§7) |
| `.devwdg_noinit` ≤ 32 B | The approved logging capsule |

The Makefile makes the linker script a prerequisite of the ELF, so a layout
edit relinks without `make clean`.

### 3.4 Boot image check

- **Why:** the closed bootloader had only ever written images up to about
  483 KB when the window grew to 736 KiB. If it fails to erase or program a
  sector the image reaches, the result would otherwise be silent code
  corruption.
- **Block:** 32 B right after the vector table, in sector 1: word 0 magic
  (`IMC?` placeholder in the ELF, `IMCK` when stamped), words 1–6 one CRC32
  per application sector 1–6 over that sector's share of
  `[0x08008000, _eflash_load)` with the block's own bytes skipped, word 7
  the image length. Sectors the image does not reach hold the CRC of nothing
  (0).
- **Stamping:** `tools/build_lxrv2_img.py` (`make img`) rewrites the block
  in the image payload (Python `zlib.crc32`: reflected CRC-32, polynomial
  `0xEDB88320`, init and final `0xFFFFFFFF`). Since S074 it is the only image
  script (the separate `stamp_image_check.py` was folded in) and `lxr02.bin`
  stays the raw, unstamped objcopy output. If the binary does not have the
  promised layout the script fails, deletes any previous `.img` and exits
  non-zero, so no unstamped or stale image can be copied to the card.
- **Checking:** `flashImage_verifyAtBoot()` (`Core/Hardware/flashImage.c`)
  runs in `main.c` after `din_init()` and `time_initTimer()`, before any
  sample, DSP or storage code. It uses a 16-entry nibble table, no RAM
  beyond locals, about 20 ms.
  - Silent on success.
  - Mismatch: `Img BAD s:<sector digits>` / `Reflash. BAR1=go`. For example
    `Img BAD s:.....6` means sector 6.
  - Unstamped image: `Img unstamped`.
  - After a report, boot waits for a BAR1 (PB7) press and release, then
    continues, so a checker fault can never stop a good image.
- **Keep in sync:** the sector table appears in the script (`SECTOR_ENDS`) and
  in `flashImage.c` (`flashImage_sectorEnd`).
- **Open decision (D-C1):** whether to keep the check permanently. It is
  kept for now.

### 3.5 Image packaging and the bootloader

- `make img` runs `tools/build_lxrv2_img.py`: the image check stamp (§3.4),
  then a 16-byte header (`LXRV2IMG`
  magic, payload size little-endian, an **8-bit** additive checksum as a
  32-bit word) and the payload. The packer has no size check; the linker
  ASSERTs are the guard.
- To update: copy `build/LXRV2_lxr02.img` to the card root, hold the main
  encoder, power on. Keep exactly one `.img` in the card root and delete
  macOS `._*` files, since a bootloader that matches the 8.3 alias or the
  first `*.IMG` could flash the wrong file.
- **Known:** a normal-size update leaves sample sectors intact. The Erica
  factory application (`LXRV2_update_v1.70.img`, 275,832 B) has no
  flash-writing code, so it tells nothing about larger images.
- **Settled in practice (S074):** the bootloader writes images that reach
  sector 6 (§3.2a). Whether it erases by image size or a fixed range is
  still not known in detail, and it does not matter while images keep
  booting. The boot image check stays as the guard. The unexecuted test plan
  is in `073_SESSION_HANDOFF_LOG.md` Appendix A.
- **Recovery:** power on holding the encoder with a known-good image on the
  card. It depends on sector 0 staying intact.

### 3.6 If flash runs short again

Ranked growth paths (S072 study):

1. `-Os` for cold control modules (`menu.c`, `filesystem.c`,
   `presetManager.c`, `storageTypes.c`) through per-file Makefile rules;
   several KiB; needs UI and filesystem timing checks.
2. Large constant tables (`crashSample`, the wavetables, `transientData`) as
   data installed into the sample region; up to about 124 KiB; needs an
   install path and a reserved span.
3. Dead legacy code: the Euklid and SOM generators are compiled in although
   `ENABLE_EUKLID_PAGE 0` hides their page.

Do not erase sector 6 before updates to work around a bootloader problem: it
only works while sector 6 holds no code.

---

## 4. Sample flash (sectors 7–11)

### 4.1 Layout

| Region | Range | Size |
|---|---|---:|
| Sample audio | `0x080C0000–0x081FF69F` | 1,308,320 B (about 14.8 s of 44.1 kHz 16-bit mono) |
| `SampleInfo[120]` | `0x081FF6A0–0x081FFC3F` | 1,440 B (12 B each: 3-char name, pad, 32-bit size in samples with bit 31 = loop flag, absolute offset) |
| Display names `[120][8]` | `0x081FFC40–0x081FFFFF` | 960 B |

- Before Session 073 the audio started at `0x08080000` (sector 6) and held
  1,570,464 B.
- Macros: `Core/SampleRom/SampleMemory.h` (`SAMPLE_ROM_START_ADDRESS
  0x080C0000`, `SAMPLE_FIRST_SECTOR 7u`, `SAMPLE_MAX_COUNT 120`).
- RAM caches of this table live in SRAM1 (§8, `SampleMemory.c`, 5,040 B).

### 4.2 Guards

- `sampleFlash.c` refuses any erase or program below `SAMPLE_FIRST_SECTOR`.
- `_Static_assert`s tie `SAMPLE_ROM_START_ADDRESS` to the base of
  `SAMPLE_FIRST_SECTOR`.
- A runtime interlock refuses every erase and write if
  `SAMPLE_ROM_START_ADDRESS` differs from the linker's
  `__sample_flash_start`.
- `memtest.c` uses the same floor (`ERASE_SECTOR_FLOOR`).
- D-cache is invalidated over the erased or programmed range.

### 4.3 Installing

- Load:[Samples] (Load page only) runs `menu_loadSamplesModal()`: it waits up
  to 10 s for a storage operation already running, suspends audio, erases
  sectors 7–11, installs `/samples` then appends `/loops`, and reinitialises
  audio. Only mono 16-bit 44.1 kHz PCM WAV files are accepted.
- A pre-S073 install is rejected (0 samples): its entry 0 is at
  `0x08080004`, below the floor.
- **Rollback hazard:** a pre-S073 firmware on a unit that has a pre-S073
  sample index and a grown S073+ image in sector 6 would play program code as
  audio. Reinstall samples (at low volume) before rolling back past S073.

---

## 5. Static RAM ledger (Session 075 F2 DEV link)

| Region and section | Start | Capacity | Static bytes | Free |
|---|---|---:|---:|---:|
| SRAM1 `.dma_nocache` | `0x20020000` | part of SRAM1 | 3,100 | — |
| SRAM1 `.data` | `0x20020c1c` | part of SRAM1 | 416 | — |
| SRAM1 `.bss` | `0x20020dc0` | part of SRAM1 | 293,060 | — |
| **SRAM1 total** | `0x20020000` | **376,832** | **296,576** | **80,256** |
| DTCM `.dtcm` | `0x20000000` | part of DTCM | 512 | — |
| DTCM `.dtcmz` | `0x20000200` | part of DTCM | 3,960 | — |
| DTCM `.dtcm_fxbuf` (arena) | `0x20001180` | part of DTCM | 126,592 | 0 (reserved arena) |
| **DTCM total** | `0x20000000` | **131,072** | **131,064** | **8** |
| ITCM `.itcm` (code) | `0x00000000` | 16,384 | **4,168** | 12,216 |
| SRAM2 `.devwdg_noinit` | `0x2007c000` | 16,384 | 0 | see stack note |

- Static data RAM (SRAM1 + DTCM including the arena) is 427,640 B;
  including ITCM code, 431,808 B.
- **Session 075 changes (approved: +101 B F2 allocation ledger):** the F1
  implementation and F2 overlay/data additions produce the current DEV
  `.bss` 293,060 B, `.data` 416 B, DTCM `.dtcmz` 3,960 B (the FX arena is
  unchanged). F2's new owners are `SceneData.c:scenes` +96 B for
  `fx_send_morph[6]`, `menu.c` +4 B for the overlay record, and
  `buttonHandler.c` +1 B for its TRACK mask.
  The earlier F1 owners remain: `copyClearSession.c` 25 B
  (`cc_state` 6, `cc_source` 6, `cc_rowStack` 8, row count 1, edge masks 4);
  `copyClearService.c` 59 B (queue 24 + head/count 2, register 16 + count/Scene
  2, run state 6, flags/claim 2, retry counter 2, name-buffer pointer 4);
  `service_exclusive_scene` 1 B; `fs_name_cache_borrowed` 1 B; the F1
  retired-decimation transition reduced `scenes` by 32 B
  (`voice_decimation_all` removed: settings 45 → 44 B, 2 B per record with
  alignment) before F2 appended `fx_send_morph[6]` and restored the current
  50 B layout. `mixer_decimation_rate[]` (DTCM, 28 → 24 B) accounts for the
  `.dtcmz` change. The remainder is LTO placement and alignment.
- **Session 074 changes (all approved):**
  - SRAM1 `.bss` +56 B. `scenes` grew +64 B (`scene_settings_t` 41 → 45 B
    for `bus_comp[4]`); the section total moved 56 B after alignment
    and LTO placement.
  - DTCM `.dtcmz` +32 B (`busComp`, 24 B then 32 B with the saturation
    crossover state); `_edtcmz` `0x20001160` → `0x20001180`, so the arena
    shrank by 32 B.
  - ITCM unchanged.
- **Session 073 changes:** ITCM +400 B (`osc_setFreq()` is now its own ITCM
  function; before it was inlined into its callers in flash). SRAM1 and DTCM
  unchanged.
- **Stack:** `_estack = 0x20080000`, the top of **SRAM2**, growing down. It
  has no linker reservation and no measured high-water mark, and it is not in
  these totals. SRAM2 is therefore not free feature RAM. The disabled
  `DEV_LOGGING_IWDG` capsule would place 12 B at SRAM2's base; it is 0 B in
  this configuration.

---

## 6. ITCM contents

Code placed with `INITCM` (`config.h`) is copied from flash to ITCM at boot
and runs with zero wait states.

| Function | Bytes |
|---|---:|
| `calcNextOscSampleBlock` (two specialisations) | 876 + 824 |
| `calcUserSampleOscFmBlock` | 444 |
| `calcNextOscSampleFmBlock` | 440 |
| `osc_setFreq` (standalone since S073) | 400 |
| `calcUserSampleOscBlock` | 396 |
| `calcSampleOscFmBlock` | 196 |
| `calcFmBlock` (three specialisations) | 192 × 3 |
| Veneers for the interpolation blocks | 8 × 2 |

- Only oscillator code is placed here. Filter and distortion ITCM placement
  (`ENABLE_EFFECT_INITCM_CODE`) is off: it measured worse in Session 023,
  because the forced out-of-line call costs more than it saves with I-cache
  on.
- ITCM is RAM under the approval policy (§10). Report code growth here with
  its byte count and owner.

---

## 7. DTCM

DTCM is single-cycle, uncached, and not reachable by DMA.

| Object | Bytes | Placement and owner |
|---|---:|---|
| `squareRootLut` | 512 | `.dtcm` (initialised); constant-power pan law |
| `audioOutBuffer`, `audioOutBuffer2` | 1,536 each | `.dtcmz`; the two render slots per DAC, `sample_mx_t`, written by the mixer and read by the DMA ISR's pack |
| `velocityModulators` | 264 | `.dtcmz`; six velocity modulation nodes |
| `mixer_fx_bus` | 256 | `.dtcmz`; two 32-frame channels, `sample_mx_t` while voices sum, float while the Effect runs |
| `effects_runtime` | 76 | `.dtcmz`; union holding the active Effect type's DSP state (StereoFilter 76 B: two filter states; CrumpBit 56 B, S074). Only 20 B spare before the union grows. |
| `busComp` | 32 | `.dtcmz` (S074); master bus compressor state: smoothed power, fast and memory gain reduction, previous gain, pending sidechain weight, crossover low-pass per channel, active pair. Approved up to 32 B; `_Static_assert(<= 32)`. Owner `BusCompressor.c`. |
| `osc_interp_a`, `osc_interp_b` | 64 each | `.dtcmz`; waveform-interpolation scratch |
| `mixer_decimation_rate` [7], `mixer_decimation_cnt` [6], `mixer_voice_samples` [6] | 28 + 24 + 12 | `.dtcmz`; per-slot decimators |
| `mixer_voice_last_gain`, `mixer_send_last_gain` | 24 each | `.dtcmz`; per-slot dry and send ramp origins |
| `mixer_fx_return_last_gain` | 8 | `.dtcmz`; Effect return ramp origins |
| `mixer_audioRouting` | 6 | `.dtcmz` |
| `modNode_waveInterp*` | 6 | `.dtcmz` |
| **`.dtcm_fxbuf` arena** | **126,592** | NOLOAD, never copied or zeroed; owned by `FxBuffer` |

- **The arena** is every DTCM byte after `.dtcmz`, 32-byte aligned. A linker
  ASSERT keeps it at least 120 KiB (122,880 B); the margin is 3,712 B.
  **Any new `INDTCM`/`INDTCMZ` static shrinks it**, in 32-byte steps (the
  arena base is 32-byte aligned). The S074 bus compressor state is the
  example: 24 B cost 32 B of arena, and the later +8 B cost nothing, because
  `_edtcmz` was already at a 32-byte boundary.
- **Arena ownership:** `FxBuffer` hands out one contiguous Effect share from
  the bottom and up to twelve 4,416 B voice units (2,208 16-bit samples,
  50.06 ms at 44,108 Hz; at most two per Instrument slot) from the top. The
  Effect share is at least 73,600 B with all twelve units claimed. CrumpBit
  (S074), the first arena user, takes 70,592 B of it. The
  system never clears the arena: an owner clears what it reads unless it
  adopts content the handoff record marks valid. Details:
  `EFFECTS_BUS_REFERENCE.md` §7 and `EFFECTS_MIXER_DSP_REFERENCE.md`.
- `sine_table` (8,194 B) moved from DTCM to flash in S072; `transientData`
  is flash-resident too.

---

## 8. SRAM1

### 8.1 DMA buffers (`.dma_nocache`, MPU region 1)

| Object | Bytes | Notes |
|---|---:|---|
| `dma_buffer` | 1,536 | I2S2 / DAC2 circular DMA, word-aligned |
| `dma_buffer2` | 1,536 | I2S3 / DAC1 circular DMA, word-aligned |
| `adc_dma_buf` | 28 | Slider ADC scan |

- MPU region 1 covers the first 4 KB of SRAM1 as **Normal non-cacheable**
  (TEX=001, C=0, B=0, S=1, XN=1) since Session 073; it was Strongly-Ordered
  before. Stores are bufferable, so the pack ISR ends with `DSB`
  (`EFFECTS_MIXER_DSP_REFERENCE.md`).
- The whole section must stay ≤ 4,096 B (linker ASSERT).

### 8.2 Resident SRAM1 owners

All sizes are bytes. Objects have firmware lifetime unless a shorter
useful-content lifetime is stated; clearing or reusing an object does not
release its linked storage. The section ledger (§5) includes every linked
byte, including alignment and small variables omitted here.

| Owner / object | Bytes | Allocation and use |
| --- | ---: | --- |
| `SceneData.c`: `scenes` | 26,080 | Sixteen resident Scene records, 1,630 B each: 50 B settings including the S075 F2 `fx_send_morph[6]`, 420 B Scene-owned Effect record, and the 1,160 B Kit; Pattern regions are separate. |
| `PatternData.c`: `pat_regions` | 168,304 | Sixteen packed regions of 10,519 B: each has 1,792 B step addresses, 8,192 B pool, 512 B bitmap, and 23 B Pattern/track settings. Since S075 the top 132 B of each pool (33 chunks) is a permanent swap block outside normal allocation (8,060 B usable), kept as a guaranteed rewrite area for copy/clear and later features (`PATTERN_DYNAMIC_STACK.md` §3, §12.17). |
| `PatternData.c`: `pat_autosave_snapshot` | 10,519 | One Scene-sized snapshot for an in-flight Pattern AutoSave. |
| `PatternStackService.c`: `reservation_image` | 512 | One non-persisted bit image for the current service Scene's trailing pool reservations; three separate one-byte policy/rebuild flags accompany it. |
| `PatternStackService.c`: `service_queue` | 256 | Sixty-four 32-bit mutation entries; cursors and repair/handover state are additional small SRAM1 objects. |
| `Autosave.c`: `autosave_dirty_mask` | 3,856 | Sole canonical scalar dirty-bit mask. |
| `Autosave.c`: Pattern dirty masks | 4 | Two 16-bit Scene masks: semantic and non-semantic relocation work. |
| `Autosave.c`: `autosave_dirty_count`, `autosave_last_pattern_semantic_us` | 6 | Exact scalar dirty-bit count and latest semantic Pattern edit timestamp. |
| `filesystem.c`: `fs_pattern_generation`, `fs_pattern_drain_scene`, `fs_pattern_first_dirty_us`, `fs_pattern_scene_cursor` | 70 | Sixteen Pattern generation baselines, drain selector, first-dirty timestamp, and fair Scene cursor. |
| `filesystem.c`: `fs_autosave_parameter_cache` | 4,608 | Bounded scalar AutoSave patch offsets and values. |
| `filesystem.c`: `fs_stage_workspace` | 2,048 | One union shared by Kit, Instrument, Scene+Effect, AutoSave writer, and HCNAMES regeneration staging. The Scene+Effect peak is 1,625 B (the typed-load assert sums to 2,009 of 2,048 since S074); union members are not additive. The AutoSave writer member gained the 1-byte `overlong_mask` in S074 (0 B: inside the union). |
| `filesystem.c`: `staging_buf` | 512 | Shared streaming and trace-batch buffer. |
| `filesystem.c`: `fs_list_cache_name` | 9,000 | One 1,000 × 9 browser/index name cache. Since S075 it is also lent to copy/clear as working storage while an operation runs (`filesystem_borrowNameCacheScratch()`; tag `FS_NAME_CACHE_COPYCLEAR`): [0..160] HCNAMES row remap, [256..511] paste source table, [512..] source blocks (≤ 8,060 B), Kit/Effect/FX-range copies, and at the end the original HCNAMES names/sources (1,771 B at 256). Worst case 8,572 B. While lent, cache disposal is ignored and other filesystem ops are refused; the cache is cleared on return and Load/Save reloads its index. |
| `copyClearSession.c` / `copyClearService.c` | 215 | Copy/clear operation state, source, raw-index press stack, edge masks; queue of four 6 B jobs, eight-entry pot-clear register, run state, early-trigger masks, trickle credit and name-buffer pointer (S075 F1). The +131 B F1 owner delta is separate from the −7 B retired LED state in the net ledger. |
| `filesystem.c`: `hcnames_name_mirror`, `fs_resident_source` | 1,771 | Separate 161 × 9 HCNAMES names and 161 × 2 provenance sources; Effect rows are 145..160. |
| `filesystem.c`: `op_effect_display_name` | 9 | Cached Effect filename stem for the current Scene/Bank child save. |
| `filesystem.c`: `op_effect_state` | 7 | Bounded `.fx` parser state retained across async file-reader passes. |
| `filesystem.c`: `fs_identity_name`, `fs_identity_valid_mask` | 74 | Eight × 9 Scene/Kit/Instrument identity strings plus a 16-bit validity mask; the Bank's nine-byte name is held separately by BankData. |
| `filesystem.c`: `op_bank_child_scratch` | 144 | One union: 16 × 9 Bank-child names or 16 × 6 boot-reader Instrument types, with disjoint lifetimes. |
| `asyncfatfs.c`: `afatfs` | 6,984 | FAT state, caches, and five file handles in one owner. |
| `InstrumentManager.c`: `runtime_slots` | 7,056 | Six tagged 1,176 B engine slots; no parallel native engine array. The largest engine (`DrumVoice`) is 588 B; the reserve is twice that. |
| `adcPots.c`: `slider_lut` | 4,096 | 1,024 `float` slider conversion values. |
| `SampleMemory.c`: resident and install caches | 5,040 | 1,440 B `sample_info_cache`, 1,080 B `sample_name_cache`, 120 B loop flags, 1,440 B `install_info`, and 960 B `install_names`. |
| `usb_manager.c`: `USB_OTG_dev` | 1,524 | USB core/device handle. |
| `usb_midi_core.c`: `usb_MidiMessages` | 2,048 | USB MIDI input ring. |
| `sequencer.c`: pending automation + dirty bits | 560 | 128 four-byte pending records (512 B) and six per-voice 64-bit dirty maps (48 B). |
| `InstrumentManager.c`: `lfo_descriptor_targets` | 192 | Twelve descriptor LFO adapters for six slots and two target pairs. |
| `menu.c`: `parameter_values` | 384 | Legacy Menu/MIDI parameter cells. |
| `MidiParser.c`: `midiParser_originalCcValues` | 255 | Legacy MIDI CC baseline cells. |
| `buttonHandler.c`: `evt_ring` | 64 | Sixty-four one-byte front-panel events; producer/consumer and overflow state are additional bytes. |
| `lcd.c`: `lcd_queue` | 384 | LCD command queue. |
| `FxBuffer.c`: `fxbuf_state` | 28 | Linker arena base/size, twelve unit owners, count, and share callback. |
| `FxBuffer.c`: `fxbuf_handoffRecord` | 180 | Effect/voice handoff metadata and arena-relative positions. |
| `EffectsManager.c`: `effects_state` | 84 | Active type/Scene, force flag, 64-byte last-applied image, FX step/selection/held-Morph state, sequence signature, and common runtime values. |
| `EffectsManager.c`: `effects_automation` | 184 | Effect Pattern overlays, owner/end masks, `fxm` override, and 6 × 2 base-independent LFO contribution entries. |
| `sequencer.c`: `seq_fxEvent` | 1 | TIM3-to-foreground newest-wins RESET/STEP latch; no Scene, DSP, or LED work occurs in the ISR. |
| `sequencer.c`: `seq_effectAutomationTracks` / `seq_effectAutomationReset` | 2 | Owner-track publication and reset latch for the foreground Effect overlay drain. |
| `menuEffects.c`: page state | 21 | Eight SELECT screen cells, Morph-view flag, `typ` transaction state, last Scene/type tracking, SEQ hold mask, and LED repaint signature; SRAM1, foreground UI lifetime. S074 repacked the hold flag byte as `holdState` (active bit, last-held-valid bit, 4-bit last step held): 0 B change. |

Other SRAM1 state comprises filesystem operation cursors and text buffers,
HCNAMES/boot control fields, Menu and front-panel state, sequencer/MIDI state,
modulation metadata, USB/driver records, and section padding. Notable small
owners are `menu_pendingPageSwitch` (1 B), `fs_boot_latch` (6 B linked),
`fs_boot_winner` (12 B linked), and `drumset_apply_stall_ticks` (2 B). No
Pattern storage is embedded in `scene_t`; `PAT_STACK_SIZE=256` reserves
8,192 B of pool per Scene while the 512 B bitmap covers the full address
range.

### 8.3 Conditional diagnostic SRAM1

These exist only in development builds. Logging-only rows are compiled out
with their producers when `DEV_MODE_LOGGING=0`; the FxBuffer and registry
self-test rows exist only when `DEV_MODE_DIAGNOSTIC=1`. Measure a mode-off
total from a clean rebuild; subtracting this table from a mode-on total
misses alignment and other compile-time changes.

| Owner / object | Bytes | Use |
| --- | ---: | --- |
| `AutosaveTrace.c`: `autosave_trace_records` | 16,384 | Temporary 2,048 × 8 record ring (default 64 records). |
| `AutosaveTrace.c`: three 16-bit cursors/counter | 6 | Ring publication, flush, and dropped-record state. |
| `PatternTrace.c`: `pattern_trace_records` | 256 | 32 × 8 Pattern/automation diagnostic ring. |
| `PatternTrace.c`: three 16-bit cursors/counter | 6 | Pattern trace publication, flush, and dropped-record state. |
| `filesystem.c`: `fs_hcprms_boot_capsule` | 64 | Eight × 8 B frozen boot-ensure failure records; useful for one boot attempt. |
| `filesystem.c`: trace flush cadence and witness state | 3 | `fs_autosave_trace_next_due_tick` and `fs_trace_suppress_witness`; other logging control is included in the section total. |
| `buttonHandler.c`: `evt_drop_count` | 1 | Saturating front-panel overflow witness. |
| `FxBuffer.c`: `fxbuf_selfTestResult` | 1 | Diagnostic-only allocation self-test result. |
| `EffectsManager.c`: `effects_registryCheckCode` | 1 | Diagnostic-only registry invariant result. |
| `copyClearService.c`: `ccSvc_traceState` | 22 | Packed DEV-only copy/clear operation, queue, scratch, drop, register and name counters. |
| `filesystem.c`: `fs_cc_suspended_prev`, `fs_cc_refusal_reported` | 2 | DEV-only suspension-edge and first-refusal latches for stage `c`. |

`DEV_STALL_DETECTION=1` also keeps its phase/tick detector state; its
condition is `DEV_STALL_DETECTION`, not `DEV_MODE_LOGGING` alone. The runtime
AutoSave drain's observer is 5 B: `op_autosave_drain_last_phase` (u8),
`op_autosave_drain_stall_ticks` (u16) and `op_autosave_drain_last_progress`
(u16). That is the same total as its pre-S074 `u8` + `u32`. The S073
special-tag self-check (`instrumentManager_specialTagSelfCheck()`) uses no
RAM.

Configuration at S073 close: `DEV_MODE_LOGGING=1`, `DEV_MODE_DIAGNOSTIC=0`,
`DEV_LOGGING_IWDG=0`, `DEV_STALL_DETECTION=1`,
`AUTOSAVE_TRACE_RECORD_COUNT=2048`, `PAT_TRACE_RECORD_COUNT=32`,
`PAT_STACK_SIZE=256`, `MEMTEST_ENABLED=1` (compiled, not called).

---

## 9. SD card storage (referential)

The card holds the musical data and the persistent state; this manifest does
not describe it in detail.

| Topic | Authority |
|---|---|
| Directory layout, `.hcnames`, `.hcindex`, Kit/Instrument/Scene/Bank/Pattern/`.fx` files, load/save | `FILESYSTEM_SPEC.md` |
| AutoSave `.hcprms1/.hcprms2` (HCPR v3), PAT4 A/B, boot restore | `AUTOSAVE.md` |
| The async FAT/VFAT layer and its caller rules | `ASYNCFATFS_REFERENCE.md` |
| Trace and boot log files (`/asavetrc.bin`, `/bootlog.bin`, `/pattrace.bin`) | `DEV_MODES.md` |
| Sample source folders `/samples` and `/loops` | §4.3 above |

---

## 10. Allocation rules

- **Reservation policy.** Free DTCM, including capacity released by moving
  tables to flash, is reserved for delay-line and audio buffers (the arena).
  Free normal SRAM1 is reserved for Pattern data.
- **Approval.** Before adding or enlarging retained RAM, state the exact byte
  count, region, lifetime and owner, and get the user's acknowledgement. This
  covers globals, static storage, pools and unions, DMA buffers, linker
  sections, material stack growth, and ITCM code growth. Releasing RAM does
  not authorise its reuse by another subsystem.
- **Logging allocations** require their logging code to be compiled and must
  disappear from a logging-off build.
- **Flash** is no longer tight (250,736 B free at the S074 close), but every
  change is still measured with `link_budget.py` and recorded in the session
  log.
- **Record every change here** in the same change that makes it.

---

## 11. History

- **S043:** tagged runtime slots; slider LUT 1,024 entries; `transientData` to
  flash.
- **S057–S069:** filesystem, AutoSave and Pattern owners (details in those
  handoff logs).
- **S071:** +86 B (per-Scene edit masks, Bank staging, audio-out and FX-send
  step overrides).
- **S072 (Phase 5):** `sine_table` to flash; the 126,624 B DTCM arena;
  Scene records +6,752 B (the 420 B Effect record); HCNAMES +176 B;
  `effects_state` 84 B, `effects_automation` 184 B, `fxbuf_handoffRecord`
  180 B, `fxbuf_state` 28 B, `menuEffects` 21 B, sequencer latches 3 B;
  DTCM `effects_runtime` 76 B, FX bus 256 B, ramp state 32 B. Final link
  `text=483,024`, `data=416`, `bss=426,336`; payload 483,440 B.
- **S073:** program flash 480 → 736 KiB (sector 6), sample floor sector 7,
  boot image check (`.image_check` 32 B flash), DMA region Normal
  non-cacheable, ITCM +400 B (`osc_setFreq`). No SRAM1 or DTCM change.
- **S075:** copy/clear +84 B SRAM1, `srt` retired (−32 B `scenes`; DTCM
  `mixer_decimation_rate` −4 B), +2 B claim/borrow flags; permanent 132 B swap
  block per Scene pool (inside the existing pool); the 9,000 B name cache
  becomes copy/clear working storage during an operation. F1 adds +124 B
  production SRAM1 net and DEV adds 24 B for trace state/latches. DEV link
  `text=530,592`, `data=416`, `bss=426,616`; production link
  `text=516,688`, `data=408`, `bss=409,840`.
- **S074:**
  - `scenes` +64 B (bus compressor settings);
  - DTCM `busComp` 32 B, so the arena is 126,592 B at `0x20001180`;
  - CrumpBit: 0 B static, 56 B in the union, 70,592 B arena share at run
    time;
  - the image reaches sector 6 (the bootloader handles it);
  - `stamp_image_check.py` folded into `build_lxrv2_img.py` (the `.bin` is
    unstamped);
  - final link `text=502,512`, `data=416`, `bss=426,392`; payload
    502,928 B.
