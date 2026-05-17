# Session Handoff Log — Session 3

---

**Project**: LXR-02 firmware port (STM32F765VIH6)
**DATE**: 2026-04-23
**SESSION GOAL**: Resource audit, 24-bit audio output, encoder improvement, hardware investigation.

---

## Part 1 — Resource audit

**Flash (2016 KB available):**
- Current firmware: 20 KB
- After full port estimate (code + all wavetables + DSP): ~210 KB (~10% of flash)
- Sample flash region possible: sectors 8-11 (0x08100000–0x081FFFFF = 1 MB). Not implemented. Verify bootloader doesn't erase beyond app region before committing.

**RAM:**
- SRAM1 (368 KB): current use 6.4 KB. After full port (~65 KB, ~18% of SRAM1).
- DTCM (128 KB): currently unused. DSP hot data will occupy ~37 KB, ~91 KB remaining. DMA buffers must stay in SRAM1 — DTCM not DMA-accessible (same constraint as F4 CCM).
- Original F4 pattern storage (56 KB) was plain SRAM, not CCM. M7 data cache mitigates bus contention at sequencer rate.
- DTCM placement strategy deferred — do not implement until DSP port is underway.

**Original hardware:** STM32F407VGT6 (Cortex-M4F, 168MHz, 1MB flash, 112KB SRAM + 64KB CCM). Developed on STM32F4 Discovery board (`USE_STM32F4_DISCOVERY` confirmed in Makefile).

**Original DSP engine:** ~44.01 kHz (PLLI2S N=383/R=2/HSE=8MHz). Float32 internally, int16 output. Block size: OUTPUT_DMA_SIZE=16 samples (0.36ms). CS4344 DAC supports 16/18/20/24-bit.

---

## Part 2 — 24-bit audio output

**CS4344 confirmed 24-bit capable** (marking: 344C). Previously driven at 16-bit.

**Key lesson learned**: SPI_DR is always 16-bit wide even in 24-bit I2S mode. The peripheral generates two 16-bit DMA requests per channel (MSW then LSW). Setting PSIZE=32-bit was wrong — caused distortion on DAC1 and silence on DAC2.

**Correct 24-bit I2S DMA approach:**
- Buffers: `int16_t`, but double the size (TONE_FRAMES × 4 halfwords, not × 2)
- Fill loop packs each sample as MSW + LSW per channel
- DMA MSIZE/PSIZE: both 16-bit (unchanged from 16-bit mode)
- I2SCFGR: DATLEN=01 (24-bit), CHLEN=1 (32-bit frame)
- Fs unchanged: 44108 Hz

**Changes made to `audioTest.c`:** confirmed working on hardware, both DACs output clean tone.

**Dither note for DSP port:** Port `dither.c` with two output modes only:
- Mode 0: 24-bit, straight float→int24, no dither (default)
- Mode 1: 16-bit with dither — apply noise-shaped dither at 1 LSB (16-bit), truncate, write upper 16 bits to 24-bit frame (lower 8 bits zero)

I2S config and DMA stay identical between modes — always 24-bit frames. Global menu option, saved in global config not per-preset. `dither_process()` return type → `int32_t`, `DITHER_W` → `8388608.f` (2²³). The listener can hear the difference between modes; both are valid. No third mode needed.

**Wavetable storage:** keep at int16_t — promoted to float32 immediately on first DSP operation, no benefit to wider storage.

---

## Part 3 — Encoder investigation and final implementation

**Hardware confirmed:**
- SW42: 20-detent digital Gray code quadrature encoder + push switch
- PE13 (A): 1kΩ series (R73), 10kΩ external pull-up to VCC (R75). **No internal pull-up.**
- PE14 (B): 1kΩ series (R74), 10kΩ external pull-up to VCC (R76). **No internal pull-up.**
- PE15 (SW): internal pull-up required (no external pull resistor)
- No filter capacitors on A/B lines — bounce is purely mechanical
- Signals confirmed clean 0–3.3V by measurement

**What was tried (in order):**
1. TIM14 fast sampler + 3-sample debounce filter → fed Dannegger in TIM6: blocked valid transitions, missed 3-4 clicks
2. Combined (A,B) 16-sample uint32 debounce: 1.6ms window too long, almost no response
3. EXTI13 rising edge + B-change gate: B also bouncing, 50% nothing / 50% up-then-down
4. EXTI13 + TIM14 one-shot 2ms/5ms lockout: B read after timeout, still bouncing
5. Bare Dannegger in TIM14 at 10kHz with encode_read1: too many counts, stutter
6. EXTI both edges A+B + Dannegger + encode_read4: good but occasional bounce-back
7. Post-read blanking in encode_read4: accidentally blocked intra-detent transitions
8. Straight AVR port — Dannegger in TIM6 1kHz poll, encode_read4: best so far

**Final implementation:** Bare Dannegger polled at 2kHz from TIM14 ISR. TIM6 unchanged at 1kHz — `encoder_tick()` retained there for button debounce only. encode_read4() only — read1/read2 removed permanently.

**encode_read1/read2 removed:** These existed in original LXR to support unknown DIY encoder hardware across different builder configurations. LXR-02 has exactly one fixed encoder (SW42, 20-detent, 4 Gray code transitions per detent). Only encode_read4() is correct and the others are a liability. Do not re-add them.

**Current tarball**: `lxr02_enc_tim14_2khz.tar.gz`

**Encoder status**: functional, occasionally misses a click. Acceptable for now — sequencer and DSP port are higher priority. Revisit if needed.

---

## Part 4 — Hardware map corrections

**XP_CONNECTOR_MAPS.md corrected:**
- R73/R74: confirmed 1kΩ series resistors (not 200Ω — photo was misleading)
- R75/R76: confirmed 10kΩ external pull-ups
- No filter capacitors — ~150pF was parasitic capacitance on multimeter
- Critical note added: do NOT enable internal pull-ups on PE13/PE14

**HARDWARE_MAP.md corrected:**
- PE13/PE14 now documented as external 10kΩ pull-up, no internal pull
- PE15 documented as internal pull-up only

---

## CHANGES THIS SESSION

- `Core/Hardware/audioTest.c`: 24-bit DMA buffer packing; DATLEN=01/CHLEN=1 confirmed
- `Core/IO/encoder.c` (or equivalent): Dannegger in TIM14 at 2kHz; encode_read1/read2 removed
- `HARDWARE_MAP.md`: PE13/PE14/PE15 pull resistor documentation corrected
- `XP_CONNECTOR_MAPS.md`: R73/R74 corrected to 1kΩ; R75/R76 confirmed 10kΩ; no filter caps noted

## KNOWN ISSUES INTRODUCED

- None. Encoder status downgraded to "occasionally misses a click — acceptable for now."

## KNOWN ISSUES RESOLVED

- 24-bit audio distortion/silence from Session 2 (PSIZE=32-bit was wrong) — corrected
- Hardware pull resistor values misdocumented — corrected

## NEXT SESSION RECOMMENDED GOAL

Not explicitly stated. Known outstanding items: encoder response (revisit after sequencer/DSP port), SPI1/SD coordination, buttonHandler/ledHandler additions, preset system.

## BLOCKERS

None stated.

---

## CRITICAL REMINDERS FOR NEXT SESSION

- EXTI_IMR = 0 must remain as the very first operation in main(), before sysclk_init()
- Do NOT add pull-down to PD4 or PD5
- SPI1 shared between SD and TIM6 — needs `spi1_sd_busy` flag before SD work
- `lcd_init()` must be called before `lcd_tim7_init()`
- TIM7_SR defined locally in timebase.c at 0x40001410 — do not remove
- `menu_init()` calls `memset(parameter_values, 0, NUM_PARAMS)` — do not also memset in main()
- DO NOT TOUCH RV1-4 quad encoder code — working correctly
- RV1-4 use `atan2f(b, a)` — b and a are already centre-subtracted. Do not change argument order.
- `menu_dirty` flag must be set (not `menu_repaint()` called directly) from any input handler
- 24-bit audio: DMA buffers are `int16_t` packed as MSW+LSW pairs, I2SCFGR DATLEN=01/CHLEN=1 — do not revert
- PE13/PE14 have external 10kΩ pull-ups — do NOT enable internal pull-ups on these pins
- encode_read1/read2 are removed — do not re-add. Only encode_read4() is correct for SW42.
- TIM14 owns encoder position poll at 2kHz. TIM6 owns button debounce at 1kHz. Keep separate.
- TIM7_SR at 0x40001410 and TIM14 ISR in encoder.c — do not move these.
