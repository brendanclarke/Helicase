# Session Handoff Template

## How to start a new session

Paste the following at the start of each conversation, filling in the bracketed fields:

---

**Project**: LXR-02 firmware port (STM32F765VIH6)  
**Session goal**: [e.g. "Port the sequencer engine from the original LXR AVR/STM32 source"]  
**Last session summary**: [paste the "End of session" block from the previous handoff, or "first session after hardware testing"]  
**Current tarball**: [confirm the tarball from the project files is the current source, or note if you have local changes]  
**Constraints today**: [e.g. "keep changes to sequencer files only", "don't touch USB", "15 minutes available"]

Key files to be aware of:
- Original LXR source is at `/tmp/LXR-master/` on the server (AVR: `front/LxrAvr/`, STM32F4: `mainboard/LxrStm32/src/`)
- Current port lives in the working tarball, extracted to `/home/claude/lxr02-037_port/`
- Knowledge files: HARDWARE_MAP.md, AVR_TO_F765_MIGRATION.md, FIRMWARE_STATE.md, ENHANCED_FEATURES.md

---

## End of session block

```
DATE: 2026-05-04 (refactor session — same day as Session 8)
SESSION GOAL: Consolidate and clean up files and directory structure that changed during the Session 8 DSP debug work. Re-order functions into a sensible structure without changing underlying behaviour. Move test-only code into named functions. Clean up in-file comments. Update README and knowledge files to reflect current state.
COMPLETED: AudioCodecManager.c fully consolidated (absorbed audioTest.c and sineBufferTest.c). Both DMA ISRs, all hardware init functions, SPSC queue, public API, and sine test utility now in one file in a documented section structure. audioCodec_init() is the single hardware entry point. CodecInit() retained as a no-hardware legacy wrapper. lcd_diagDisplayInt() and lcd_diagDisplayFloat() added to lcd.c/h. sdTest.c/h moved to Core/Hardware/SD/ with include paths corrected. Makefile updated. All stale copy/backup/superseded files deleted. random.c comments cleaned up. main.c updated to use audioCodec_init(), underrun display uses lcd_diagDisplayInt(), test functions moved to a dedicated commented-out block with usage notes. Repository directory renamed to lxr02-037_port.
VERIFIED ON HARDWARE: No hardware testing this session — pure refactor, no behaviour changes.

CHANGES THIS SESSION:
- Core/Hardware/AudioCodecManager.c: Fully rewritten and consolidated. Sections in order: (1) register definitions, (2) buffer declarations, (3) queue state, (4) public API (getRenderBuffer, queueFreeSlots, commitRenderBuffer, diagnostic counters), (5) pack helpers (pack_half static inline, pack_audio_half static), (6) DMA ISRs — DMA1_Stream4_IRQHandler and DMA1_Stream7_IRQHandler both now live here, (7) hardware init helpers — plli2s_init, gpio_init, dma_init, i2s_init all private static, absorbed from audioTest.c, (8) audioCodec_init() single public entry point, (9) CodecInit() legacy wrapper (no hardware), (10) audioCodec_renderSineBlock() test utility absorbed from sineBufferTest.c. Pipeline overview, 24-bit pack format, SPSC queue invariants, hardware assignments, and ISR scheduling documented in file header. ISR sync limitation noted (Stream 4/7 may drift by up to one ISR period — negligible for now, documented for future fix).
- Core/Hardware/AudioCodecManager.h: Stripped of stale declarations (audioCodec_packHalf public wrapper removed, audioCodec_canRender removed). audioCodec_init() declared. audioCodec_renderSineBlock() declared. Main-loop pattern, buffer layout, and 24-bit pack format documented in file header comment.
- Core/Hardware/frontPanel/lcd.c: lcd_diagDisplayInt(const char *label, int32_t val) and lcd_diagDisplayFloat(const char *label, float val) added. Float format: "LABL: +0.00000E0" (label truncated to 4 chars + colon + space + sign + mantissa with 5 decimal places + E + single exponent digit, fits 16 chars). No printf — manual digit extraction only (bare-metal safe).
- Core/Hardware/frontPanel/lcd.h: lcd_diagDisplayInt() and lcd_diagDisplayFloat() declared.
- main.c: sineBufferTest_init() replaced with audioCodec_init(). Underrun/render counter display uses lcd_diagDisplayInt(). Dedicated TEST FUNCTIONS block added (commented out) containing sine test loop and SD state machine test, each with usage notes.
- Core/Hardware/SD/sdTest.c: Moved from root. Include path fixed: SD/sd_routines.h → sd_routines.h (Makefile already has -ICore/Hardware/SD). NOTE: SD_ST_PROBE contains a 1-second blocking wait — documented in the commented-out test block in main.c.
- Core/Hardware/SD/sdTest.h: Moved from root.
- Core/DSPAudio/random.c: Comment cleanup only — RCC_AHB2ENR address error documented (was 0x40023830 = AHB1ENR, correct is 0x40023834), RNG_CR direct-write rationale explained, DRDY poll removal rationale documented.
- Makefile: sineBufferTest.c and Core/Hardware/audioTest.c removed from sources. Core/Hardware/SD/sdTest.c added.
- Repository directory: renamed from lxr02_new to lxr02-037_port. Archive going forward: lxr02-037_port.tar.gz.

DELETED FILES (examined for notable changes before deletion — none found):
- audioTest.c / audioTest.h (absorbed into AudioCodecManager.c)
- sineBufferTest.c / sineBufferTest.h (absorbed into AudioCodecManager.c)
- All *copy.c, *copy.h, *copy2.c, main copy*.c, config copy.h files
- lcd.c.bak / lcd.h.bak
- Root-level sdTest.c / sdTest.h (moved to Core/Hardware/SD/)

KNOWN ISSUES INTRODUCED: None.
KNOWN ISSUES RESOLVED: Scattered DMA ISRs (both now in AudioCodecManager.c). Stale audioTest.c and sineBufferTest.c (absorbed and deleted). audioCodec_packHalf() public wrapper (removed; ISRs call pack_audio_half() directly).

NEXT SESSION RECOMMENDED GOAL: buttonHandler/ledHandler audit — compare processPress() case-by-case against original AVR buttonHandler_buttonPressed(). Button numbering reversed from original (BUT_MODE1=31 vs original BUT_MODE1=35). LED index mapping needs verifying against shift-register chain order.
BLOCKERS: None.

CRITICAL REMINDERS FOR NEXT SESSION:
- ALWAYS extract a fresh tarball and verify the working directory before writing any code
- Current known-good base: lxr02-037_port.tar.gz. Working directory: /home/claude/lxr02-037_port/
- audioCodec_init() is the single hardware entry point — call once from main() before the main loop. Do NOT call sineBufferTest_init() or audioTest_init() — those files are deleted.
- CodecInit() is a no-hardware legacy wrapper only — it resets buffers and queue state but does not configure hardware. Do not use it as an init entry point.
- audioCodec_renderSineBlock() is the sine test utility — see TEST FUNCTIONS block in main.c for usage.
- sdTest_tick() is the only sdTest function that should appear in main.c — all other sdTest internals stay in Core/Hardware/SD/sdTest.c.
- GetRngValue() calls must mask the result: uint16_t rnd = (int16_t)(GetRngValue() & 0x7FFF) — do not remove this masking.
- VLAs are forbidden in DSP voice files — static int16_t buf[OUTPUT_DMA_SIZE] only. Silent stack corruption of all voices is the failure mode.
- Compiler must be -O2. DSP voices do not meet timing budget at -O1.
- FPU explicitly enabled in sysclk_init() via CPACR write — do not rely on bootloader.
- RCC_AHB2ENR = 0x40023834 (NOT 0x40023830 which is RCC_AHB1ENR).
- RNG_CR = RNG_CR_RNGEN (direct write, NOT |=).
- 78 startup underruns is expected and normal — fixed count, does not grow after boot.
- Main loop pattern: if (audioCodec_queueFreeSlots() > 0) { mixer_calcNextSampleBlock(audioCodec_getRenderBuffer(), audioCodec_getRenderBuffer2()); audioCodec_commitRenderBuffer(); }
- Stream 4 is the refill master; Stream 7 clears flags only.
- plli2s_init() HSERDY guard never entered after sysclk_init() — intentional. See AudioCodecManager.c header.
- EXTI_IMR = 0 must remain as the very first operation in main(), before sysclk_init()
- Do NOT add pull-down to PD4 or PD5
- lcd_init() before lcd_tim7_init()
- menu_init() calls memset on parameter_values — do not also memset in main()
- TIM7_SR defined locally in timebase.c at 0x40001410 — do not remove
- 24-bit audio: int16_t MSW+LSW packed, I2SCFGR DATLEN=01/CHLEN=1
- I2S3_SD = PB5 (PC12 is SD SCLK — was misdocumented in early sessions)
- SD card: bit-bang SPI on PC12/PD2/PC8/PD0 (NOT hardware SPI1). No SPI1 contention.
- PE13/PE14: AF1 (TIM1_CH3/CH4), external 10kΩ pull-ups, no internal pull-up
- TIM1 IRQ27, TIM6 IRQ54, TIM7 IRQ55
- DMA1 Stream 4 (I2S2/DAC2) = IRQ15; DMA1 Stream 7 (I2S3/DAC1) = IRQ47. Both in HISR/HIFCR.
- Encoder: Dannegger, last = new & 3, round-toward-zero divide in encode_read4(), read1/read2 permanently removed
- ts_dirs[] direction buffer in encoder.c — rebound suppression. Do not remove.
- while → if in buttonHandler_processEvents() — intentional, do not revert
- LCD ring is head/tail-only SPSC (NO lcd_q_count). Do not reintroduce a shared count variable.
- sendDisplayBuffer emits lcd_setcursor before every data byte — do not add position-tracking optimization
- menu_knobs_dirty flag + menu_serviceKnobRepaint() is RV1-4-only — do not extend to other input paths
- DO NOT TOUCH endlessPots (RV1-4) — atan2f(b, a), do not change argument order
- Saturation pattern in menu_encoderChangeParameter and menu_handleLoadSaveMenu: int16 sum + clamp, NOT uint8 wrap
- DO NOT ATTEMPT TIM7 idle gating — wakeup race confirmed broken (Session 6)
- DO NOT ATTEMPT broad repaint coalescing across all input paths — confirmed broken (Session 6)
- DO NOT use val >>= 2 in encode_read4() — asymmetric floor division, fixed with round-toward-zero divide
- DTCM is NOT DMA-accessible — never tag a DMA buffer with INDTCM/INDTCMZ
- Two systick counters: time_sysTick (uint16_t), systick_ticks (uint32_t). Both 1kHz. Don't merge.
- .SND and .GLO files are byte-compatible with original LXR
- Parameters_reference.h.bak in Core/Preset/ — left for reference. Delete in cleanup pass once full port verified.
```

---

## Session 9 — 2026-05-04 (Refactor — Consolidation and Cleanup)

**Goal**: Consolidate files and directory structure changed during Session 8 DSP debug work. No behaviour changes.

**Working tarball at start**: `lxr02_new_refactored.zip` (Session 8 output)  
**Working tarball at end**: `lxr02-037_port.tar.gz` (repository renamed this session)

---

### Part 1 — AudioCodecManager.c consolidation

`AudioCodecManager.c` was rewritten to absorb all audio-related code that had been spread across three files during the Session 8 debug process: the original `AudioCodecManager.c`, `audioTest.c`, and `sineBufferTest.c`. No logic was changed — only reorganised and commented.

**New section structure:**

1. **Register definitions** — all bare-metal register macros in one place (RCC, GPIO, SPI/I2S, DMA1, NVIC)
2. **Buffer declarations** — `dma_buffer`, `dma_buffer2`, `audioOutBuffer`, `audioOutBuffer2`; SRAM1 placement note (DMA cannot reach DTCM)
3. **Queue state** — SPSC variables (`ready_queue`, `ready_head`, `ready_tail`, `render_slot`, `last_played_slot`), diagnostic counters
4. **Public API** — `getRenderBuffer`, `getRenderBuffer2`, `queueFreeSlots`, `commitRenderBuffer`
5. **Pack helpers** — `pack_half` (static inline), `pack_audio_half` (static, ISR-only)
6. **DMA ISRs** — `DMA1_Stream4_IRQHandler` (master, calls `pack_audio_half` on HT and TC) and `DMA1_Stream7_IRQHandler` (slave, clears flags only) — both now definitively in this file
7. **Hardware init helpers** — `plli2s_init`, `gpio_init`, `dma_init`, `i2s_init` — all `static`, absorbed from `audioTest.c`
8. **Initialisation** — `audioCodec_init()` single public entry point; zeroes all buffers, resets queue state, calls the four hardware helpers in order
9. **Legacy wrapper** — `CodecInit()` retained for DSP source compat; resets buffers and queue only, does not touch hardware
10. **Test utilities** — `audioCodec_renderSineBlock()` absorbed from `sineBufferTest.c`; generates 440Hz sine into current render slot and commits it

**File header** documents: pipeline data flow, 24-bit pack format, SPSC queue invariants, hardware pin assignments, ISR scheduling rationale, and a note on the future Stream 4/7 sync limitation (DAC1 and DAC2 may drift by up to one ISR period; audibly negligible, documented for future fix).

**`plli2s_init()` guard note** (documented in file, previously undocumented): The `if (!(RCC_CR & HSERDY))` guard body is never entered after `sysclk_init()` — HSE is always up by then. The guard exists as a safe fallback. `sysclk_init()` sets `PLLM=16` and `PLLSRC=HSE` (shared fields), so `plli2s_init()` inherits them without re-writing. Intentional — see `AudioCodecManager.c` header.

---

### Part 2 — AudioCodecManager.h cleanup

Stale declarations removed:
- `audioCodec_packHalf()` public wrapper — removed (ISRs now call the static `pack_audio_half()` directly)
- `audioCodec_canRender()` — removed

Added:
- `audioCodec_init()` declaration
- `audioCodec_renderSineBlock()` declaration

Header comment updated with main-loop pattern, buffer layout, and 24-bit pack format summary.

---

### Part 3 — lcd_diagDisplayInt() and lcd_diagDisplayFloat()

Two diagnostic display functions added to `lcd.c` / `lcd.h` for use in test and debug contexts.

**`lcd_diagDisplayInt(const char *label, int32_t val)`**  
Displays on two LCD rows: label+colon on row 1, signed integer value on row 2. Label truncated to 4 characters. No printf — manual digit extraction (bare-metal safe).

**`lcd_diagDisplayFloat(const char *label, float val)`**  
Format: `LABL: +0.00000E0` — label (4 chars max) + colon + space + sign + mantissa with 5 decimal places + `E` + single exponent digit. Fits exactly in 16 characters. No printf. Lowercase `e` for negative exponents. Intended for DSP values (filter coefficients, LFO rates, etc.) where the 0..1 range needs visible precision.

These replace the ad-hoc diagnostic display code written inline in `main.c` during Session 8.

---

### Part 4 — sdTest.c/h moved to Core/Hardware/SD/

`sdTest.c` and `sdTest.h` moved from the repository root to `Core/Hardware/SD/`. Include paths corrected: `#include "SD/sd_routines.h"` → `#include "sd_routines.h"` (Makefile already carries `-ICore/Hardware/SD`).

The `SD_ST_PROBE` state in `sdTest_tick()` contains a 1-second blocking wait (`while ((uint16_t)(time_sysTick - sd_timer) < 1000u)`). This freezes the main loop during the CMD0 probe delay. Acceptable because the test is only ever active inside the commented-out TEST FUNCTIONS block in `main.c`. Documented with a prominent note in that block.

Only `sdTest_tick()` should appear in `main.c` — all other sdTest internals remain in `Core/Hardware/SD/sdTest.c`.

---

### Part 5 — main.c updates

- `sineBufferTest_init()` replaced with `audioCodec_init()`
- `sineBufferTest.h` and `audioTest.h` includes removed
- Underrun/render counter display replaced with `lcd_diagDisplayInt()` calls
- **TEST FUNCTIONS block** added — a clearly delimited commented-out section containing:
  - Sine test loop (calls `audioCodec_renderSineBlock()` instead of `mixer_calcNextSampleBlock()`) with note to swap the render call and remove DSP init
  - SD state machine test (calls `sdTest_tick()`) with note about the 1-second blocking wait in `SD_ST_PROBE`

---

### Part 6 — Makefile and file deletions

**Makefile changes:**
- `sineBufferTest.c` removed from sources
- `Core/Hardware/audioTest.c` removed from sources
- `Core/Hardware/SD/sdTest.c` added to sources

**Files deleted** (examined for any changes not already captured elsewhere — none found):
- `audioTest.c` / `audioTest.h`
- `sineBufferTest.c` / `sineBufferTest.h`
- All `*copy.c`, `*copy.h`, `*copy2.c`, `main copy*.c`, `config copy.h` files
- `lcd.c.bak` / `lcd.h.bak`
- Root-level `sdTest.c` / `sdTest.h`

---

### Part 7 — random.c comment cleanup

No logic changes. Comments updated to document:
- Why `RCC_AHB2ENR` is at `0x40023834` (not `0x40023830` which is `RCC_AHB1ENR` — the address error that caused underruns in Session 8)
- Why `RNG_CR = RNG_CR_RNGEN` is a direct write rather than `|=` (RMW on an unpowered peripheral caused AHB bus stall)
- Why the DRDY polling loop was removed (RNG produces a new value every 40 cycles; repeated values are audibly irrelevant for noise; polling in the audio render path was causing DSP overload)

---

### Part 8 — Repository rename

Working directory renamed from `lxr02_new` to `lxr02-037_port` to match the project naming convention (clean port of LXR 0.37). Archive going forward: `lxr02-037_port.tar.gz`.

---

## Known Issues / Technical Debt (end of Session 9)

### Audio / RNG
- `GetRngValue()` calls must mask the result: `uint16_t rnd = (int16_t)(GetRngValue() & 0x7FFF)`. Root cause not fully investigated; masking is the confirmed working fix.
- Sequencer wiring to ISR: `sequencer.c` present but runs from main loop; dedicated TIM ISR needed for sub-millisecond step jitter.
- Stream 4 / Stream 7 sync: the two DMA ISRs are not synchronised — DAC1 and DAC2 may drift by up to one ISR period (~1ms). Negligible for now; documented in `AudioCodecManager.c` for future fix.

### High Priority
1. **buttonHandler/ledHandler audit**: Not yet done. Button numbering reversed from original (BUT_MODE1=31 vs original BUT_MODE1=35). Action logic in `processPress()` needs case-by-case audit against original `buttonHandler_buttonPressed()`.

### Medium Priority
2. TIM2 not initialised — needed for CLK IN BPM interval measurement and MIDI RX timestamping
3. MidiParser RX not connected to sequencer/parameter system
4. Preset save: pattern/all/performance stubs in place; real implementations need sequencer data structures
5. Morph buffer not connected (`parameters2[]` declared, unused)
6. Slider-to-parameter mapping not designed
7. Screensaver timing visibly off (clock recalculation needed)
8. `copyClearTools.c`: frontPanel calls commented out, need direct replacement

### Lower Priority / Future
9. SampleRom: `SampleMemory.c` is a safe no-op stub. Sector 6-11 layout confirmed by memtest.
10. SELECT_1 LED dark at boot — faithful reproduction of original LXR bug. Fix in repo 2 only.
11. `Parameters_reference.h.bak` in `Core/Preset/` — left for reference. Delete in cleanup pass.

## Critical Reminders for Next Session

### General Process
- **ALWAYS extract a fresh tarball and verify the working directory before writing any code**
- Current known-good base: `lxr02-037_port.tar.gz`. Working directory: `/home/claude/lxr02-037_port/`

### Audio / AudioCodecManager
- `audioCodec_init()` is the **single hardware entry point** — `audioTest_init()` and `sineBufferTest_init()` are deleted
- `CodecInit()` resets buffers and queue only — does not configure hardware
- `audioCodec_renderSineBlock()` is the sine test utility — see TEST FUNCTIONS block in main.c
- Main loop pattern: `if (audioCodec_queueFreeSlots() > 0) { mixer_calcNextSampleBlock(audioCodec_getRenderBuffer(), audioCodec_getRenderBuffer2()); audioCodec_commitRenderBuffer(); }`
- Stream 4 is the refill master; Stream 7 clears flags only
- `plli2s_init()` HSERDY guard never entered after `sysclk_init()` — intentional, see `AudioCodecManager.c` header

### DSP / RNG
- `GetRngValue()` calls must mask: `uint16_t rnd = (int16_t)(GetRngValue() & 0x7FFF)`
- VLAs forbidden in DSP voice files — `static int16_t buf[OUTPUT_DMA_SIZE]` only
- Compiler must be `-O2`
- FPU explicitly enabled in `sysclk_init()` via CPACR — do not rely on bootloader
- `RCC_AHB2ENR` = `0x40023834` (NOT `0x40023830`)
- `RNG_CR = RNG_CR_RNGEN` (direct write, NOT `|=`)
- 78 startup underruns is normal — fixed count, does not grow after boot

### Boot / Init
- EXTI_IMR = 0 must remain as the very first operation in main(), before sysclk_init()
- Do NOT add pull-down to PD4 or PD5
- `lcd_init()` before `lcd_tim7_init()`
- `menu_init()` calls memset on parameter_values — do not also memset in main()

### Hardware
- TIM7_SR defined locally in timebase.c at 0x40001410 — do not remove
- 24-bit audio: `int16_t` MSW+LSW packed, I2SCFGR DATLEN=01/CHLEN=1
- I2S3_SD = PB5 (PC12 is SD SCLK — was misdocumented in early sessions)
- SD card: bit-bang SPI on PC12/PD2/PC8/PD0 (NOT hardware SPI1). No SPI1 contention.
- PE13/PE14: AF1 (TIM1_CH3/CH4), external 10kΩ pull-ups, no internal pull-up
- TIM1 IRQ27, TIM6 IRQ54, TIM7 IRQ55
- DMA1 Stream 4 (I2S2/DAC2) = IRQ15; DMA1 Stream 7 (I2S3/DAC1) = IRQ47. Both in HISR/HIFCR

### Encoder (SW42)
- Dannegger difference, `last = new & 3`, round-toward-zero divide in `encode_read4()`
- `encode_read1`/`read2` permanently removed
- `ts_dirs[]` rebound suppression — do not remove

### Menu / Display
- DO NOT TOUCH endlessPots (RV1-4) — `atan2f(b, a)`, do not change argument order
- LCD ring is head/tail-only SPSC — no `lcd_q_count`
- `sendDisplayBuffer` emits `lcd_setcursor` before every data byte
- `while → if` in `buttonHandler_processEvents()` — intentional
- Saturation pattern in `menu_encoderChangeParameter` / `menu_handleLoadSaveMenu`: int16 sum + clamp
- `menu_knobs_dirty` / `menu_serviceKnobRepaint()` is RV1-4-only

### Failed Approaches — Do Not Retry
- **DO NOT attempt TIM7 idle gating** — wakeup race confirmed broken (Session 6)
- **DO NOT attempt broad repaint coalescing across all input paths** — confirmed broken (Session 6)
- **DO NOT use `val >>= 2` in `encode_read4()`** — asymmetric floor division (Session 7)
