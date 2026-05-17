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
- Current port lives in the working tarball, extracted to `/home/claude/lxr02/`
- Knowledge files: HARDWARE_MAP.md, AVR_TO_F765_MIGRATION.md, FIRMWARE_STATE.md, ENHANCED_FEATURES.md

---

## End of session block

```
DATE: 2026-04-24
SESSION GOAL: Resolve encoder bounce/response issues, implement acceleration, 24-bit audio fix, hardware investigation.
COMPLETED: TIM1 Input Capture hardware filter adopted (retained permanently). LUT state machine tried and abandoned due to val>>2 asymmetry. Reverted to Dannegger difference algorithm with Dannegger seeded to last = new & 3. Encoder acceleration implemented with 8-entry timestamp buffer, decay, and ts_dirs[] rebound suppression. 24-bit audio SPI_DR width confirmed.
VERIFIED ON HARDWARE: TIM1 IC direction correct and rock solid. Acceleration correct — fast rotation ~4× counts, slow unaffected. Stop-then-slow-decrement correctly unaccelerated after 100ms decay.

CHANGES THIS SESSION:
- encoder.c/h: PE13/PE14 reconfigured as AF1 (TIM1_CH3/CH4). TIM1 CCMR2: CC3S=01, IC3F=0xF, CC4S=01, IC4F=0xF. TIM1 CCER: both edges on CH3 and CH4. TIM1_CC_IRQHandler (IRQ27) adopted. LUT state machine introduced then abandoned in favour of Dannegger difference algorithm. encode_init() seeds last = new & 3. ISR stores time_sysTick timestamp in 8-entry circular buffer (ts_dirs[]) on each accepted transition. encode_read4() computes average interval over last 8 timestamps and applies acceleration multiplier. encode_read1/read2 permanently removed.
- timebase.c: encoder_tick() retained for button debounce on PE15 only. TIM14 not used — encoder_tim14_tick() is empty stub.
- audioTest.c: 24-bit I2S DMA confirmed — SPI_DR is 16-bit even in 24-bit I2S mode, DMA must use 16-bit transfers.

KNOWN ISSUES INTRODUCED: None.
KNOWN ISSUES RESOLVED: Encoder bounce — TIM1 IC hardware filter provides 32-sample silicon debounce. Encoder direction asymmetry — val>>2 arithmetic shift bug (LUT path) identified and resolved by adopting Dannegger with correct seed. Encoder acceleration rebound — ts_dirs[] rebound suppression added.

NEXT SESSION RECOMMENDED GOAL: SD card / FatFS port + kit parameter loading + full menu system.
BLOCKERS: Before starting: read SPI1 contention notes in AVR_TO_F765_MIGRATION.md carefully. SD card shares SPI1 with TIM6 LED/button ISR. Simplest approach for initial port: spi1_sd_busy flag checked in TIM6_DAC_IRQHandler to skip SPI exchange during SD access.

CRITICAL REMINDERS FOR NEXT SESSION:
- EXTI_IMR = 0 must remain as the very first operation in main(), before sysclk_init()
- Do NOT add pull-down to PD4 or PD5
- SPI1 shared between SD and TIM6 — needs spi1_sd_busy flag before SD work
- lcd_init() must be called before lcd_tim7_init()
- TIM7_SR defined locally in timebase.c at 0x40001410 — do not remove
- menu_init() calls memset(parameter_values, 0, NUM_PARAMS) — do not also memset in main()
- DO NOT TOUCH RV1-4 quad encoder code — working correctly
- RV1-4 use atan2f(b, a) — b and a are already centre-subtracted. Do not change argument order.
- menu_dirty flag must be set (not menu_repaint() called directly) from any input handler
- 24-bit audio: DMA buffers are int16_t packed MSW+LSW, I2SCFGR DATLEN=01/CHLEN=1 — do not revert
- PE13/PE14 are AF1 (TIM1_CH3/CH4) — do NOT reconfigure as plain GPIO or enable internal pull-ups
- Encoder algorithm: Dannegger difference (NOT LUT) — the LUT was tried and abandoned this session due to val>>2 asymmetry
- Seed: last = new & 3 in encode_init(). The phase-offset seed (new + 3) & 3 was tried this session and discarded — it loses the first edge and produces a wrong-direction click on motion onset. Do not re-introduce.
- encode_read4() only — read1/read2 permanently removed. Do not re-add.
- ts_dirs[] direction buffer in encoder.c — rebound suppression. Do not remove.
- TIM1_CC_IRQHandler at IRQ27. TIM6 at IRQ54. TIM7 at IRQ55. These are all distinct.
- encoder_tim14_tick() is an empty stub — TIM14 not used for encoder
```

---

## Session 4 — 2026-04-24

**Goal**: Resolve encoder bounce/response issues, implement acceleration, 24-bit audio fix, hardware investigation.

---

### Part 1 — Encoder: TIM1 Input Capture with hardware digital filter

**Root cause of all previous encoder problems**: We were using PE13/PE14 as plain GPIO and relying on software debounce. The closed-source firmware uses TIM1 Input Capture on CH3/CH4 (PE13=TIM1_CH3, PE14=TIM1_CH4) with the hardware digital filter ICxF=0xF. This forces the signal to be stable for 32 samples before the timer acknowledges a transition — hardware debounce at silicon level before the CPU sees anything. Combined with a state machine lookup table that rejects illegal transitions, this gives rock-solid response matching the closed-source firmware.

**Approaches tried and abandoned (in order):**
- TIM14 fast sampler + N-sample debounce filter (3, 16, 2 samples): too aggressive or too weak
- EXTI both edges + Dannegger: bounce-back on bad contacts
- EXTI + TIM14 one-shot lockout (2ms, 5ms): B signal also bouncing at read time
- Bare Dannegger in TIM14/TIM6 at various rates: couldn't overcome mechanical bounce
- Post-read blanking: accidentally blocked intra-detent transitions

**TIM1 Input Capture adopted (retained permanently):**
- PE13/PE14 configured as AF1 (TIM1_CH3/CH4)
- TIM1 CCMR2: CC3S=01, IC3F=0xF, CC4S=01, IC4F=0xF
- TIM1 CCER: both edges on CH3 and CH4
- TIM1_CC_IRQHandler (IRQ27) adopted
- TIM6 unchanged at 1kHz — encoder_tick() retained for button debounce on PE15 only
- TIM14 not used — encoder_tim14_tick() is empty stub
- encode_read4() only — read1/read2 permanently removed

**Checkpoint tarball**: `lxr02_enc_tim1_ic_CHECKPOINT.tar.gz` — direction correct, no acceleration. Roll back here if acceleration causes issues.

---

### Part 2 — Encoder: algorithm selection and final implementation

**Hardware confirmed:**
- Rest state between detents: A=1, B=1, S=3 (both contacts open, pull-ups active)
- CW sequence from rest: S=3 → S=1 → S=0 (hump) → S=2 → S=3
- CCW sequence from rest: S=3 → S=2 → S=0 (hump) → S=1 → S=3
- Signals: clean 0–3.3V, no bounce or jitter visible, no skipped states

**LUT state machine tried and abandoned:**

Initially, a 16-entry state machine lookup table was placed on top of TIM1 IC:

```c
static const int8_t enc_table[16] = {
     0, -1,  1,  0,
     1,  0,  0, -1,
    -1,  0,  0,  1,
     0,  1, -1,  0
};
```

Direction: `cur = (B<<1)|A` — B and A swapped from naive mapping to get CW=positive.

**Root cause of LUT asymmetry:** `val >>= 2` in `encode_read4()` performs arithmetic right shift on ARM — `-1 >> 2 = -1`, so CCW fired on the very first transition. The original AVR `encode_read4()` had the same code but worked because the AVR encoder's detent rested at a Gray code position aligned with the accumulator's zero crossing. This encoder's detent (S=3) is not aligned.

A phase-offset seed of `last = (new + 3) & 3` was also tried as an attempt to center the fire point. This was also abandoned — it loses the first edge and produces a wrong-direction click on motion onset.

**Final solution — Dannegger difference algorithm:**
- Ditched LUT entirely, reverted to Dannegger's original difference algorithm verbatim from AVR source
- TIM1 Input Capture hardware filter (ICxF=0xF) retained — PE13/PE14 as AF1 (TIM1_CH3/CH4)
- Seed: `last = new & 3` in `encode_init()` — seed with actual current Gray code state
- Direction: standard Dannegger (no A/B swap needed with this algorithm)

**Verified on hardware:** symmetric feel, count changes at hump of detent in both directions.

---

### Part 3 — Encoder acceleration

**Architecture:** ISR stores `time_sysTick` timestamp in 8-entry circular buffer (`ts_dirs[ACCEL_BUF_SIZE]`) on each accepted Dannegger transition. Each entry also stores the transition sign (+1/-1). No velocity computation in ISR. `encode_read4()` computes average interval over last 8 timestamps and applies multiplier.

**Spec:**
- ≤10 inc/sec (avg interval ≥25ms): multiplier = 1× (no acceleration)
- ~80 inc/sec / 1 rotation in 0.25s (avg interval ≤3ms): multiplier = 4×
- Linear interpolation between
- Decay: if most recent transition >100ms ago, return unaccelerated (prevents stale fast-spin velocity from accelerating subsequent slow movement)
- Rebound suppression: after decay check, count majority direction in the ts_dirs[] buffer. If majority opposes the sign of current `val`, return `val` unaccelerated (rebound shows up as ±1 max, not ±4). Genuine direction reversals get rebound-suppressed for one read, then accelerate normally.
- Symmetric: applied to signed `val`, so CW and CCW get identical treatment

**Constants (all in encoder.c):**
```c
#define ACCEL_BUF_SIZE   8
#define ACCEL_SLOW_MS    25
#define ACCEL_FAST_MS    3
#define ACCEL_DECAY_MS   100
#define ACCEL_MAX_MULT   4
```

**Verified on hardware:** acceleration correct. Fast rotation gives ~4× counts. Slow rotation unaffected. Stop-then-slow-decrement correctly unaccelerated after 100ms decay.

**Current tarball:** `lxr02_enc_accel_v2.tar.gz`

---

### Part 4 — 24-bit audio (carried from Session 3)

**Note on SPI_DR width**: SPI_DR is always 16-bit even in 24-bit I2S mode. DMA must use 16-bit transfers. Buffer is `int16_t` packed as MSW+LSW pairs (4 halfwords per stereo frame). I2SCFGR DATLEN=01/CHLEN=1. Both DACs verified working.

---

### Part 5 — Communication style note

Added to session_open.txt: "Communication style: Be direct and technical. No affirmations, no sycophantic openers, no dramatic declarations. State findings and ask questions without preamble. Treat this as a professional engineering collaboration."

---

## Known Issues / Technical Debt (as of end of Session 4)

- TIM2 not initialised for CLK IN BPM interval measurement
- SPI1 contention (SD vs TIM6): needs mutex before SD work begins
- MidiParser RX not connected to sequencer/parameter system
- `buttonHandler_getMode()`, `buttonHandler_getShift()`, `buttonHandler_setRunStopState()` not yet added
- `led_setActive_step()`, `led_clearSequencerLeds()`, `led_initPerformanceLeds()` not yet added to ledHandler
- `paramToModTarget[]` / `Cc2Text.c` not ported
- Preset system not started
- Slider-to-parameter mapping not designed
- Voice5-7 MODE LED feedback incomplete in `menu_switchPage()`

## Critical Reminders for Next Session

- EXTI_IMR = 0 must remain as the very first operation in main(), before sysclk_init()
- Do NOT add pull-down to PD4 or PD5
- SPI1 shared between SD and TIM6 — needs `spi1_sd_busy` flag before SD work
- `lcd_init()` must be called before `lcd_tim7_init()`
- TIM7_SR defined locally in timebase.c at 0x40001410 — do not remove
- `menu_init()` calls `memset(parameter_values, 0, NUM_PARAMS)` — do not also memset in main()
- DO NOT TOUCH RV1-4 quad encoder code — working correctly
- RV1-4 use `atan2f(b, a)` — b and a are already centre-subtracted. Do not change argument order.
- `menu_dirty` flag must be set (not `menu_repaint()` called directly) from any input handler
- 24-bit audio: DMA buffers are `int16_t` packed MSW+LSW, I2SCFGR DATLEN=01/CHLEN=1 — do not revert
- PE13/PE14 are AF1 (TIM1_CH3/CH4) — do NOT reconfigure as plain GPIO or enable internal pull-ups
- Encoder algorithm: Dannegger difference (NOT LUT) — the LUT was tried and abandoned this session due to `val>>2` asymmetry
- Seed: `last = new & 3` in `encode_init()`. The phase-offset seed `(new + 3) & 3` was tried this session and discarded — it loses the first edge and produces a wrong-direction click on motion onset. Do not re-introduce.
- `encode_read4()` only — `encode_read1`/`read2` permanently removed. Do not re-add.
- `ts_dirs[]` direction buffer in encoder.c — rebound suppression. Do not remove.
- TIM1_CC_IRQHandler at IRQ27. TIM6 at IRQ54. TIM7 at IRQ55. These are all distinct.
- `encoder_tim14_tick()` is an empty stub — TIM14 not used for encoder

## Next Session Recommended Goal

SD card / FatFS port + kit parameter loading + full menu system.

Before starting: read SPI1 contention notes in AVR_TO_F765_MIGRATION.md carefully.
SD card shares SPI1 with TIM6 LED/button ISR. Simplest approach for initial port:
`spi1_sd_busy` flag checked in TIM6_DAC_IRQHandler to skip SPI exchange during SD access.
