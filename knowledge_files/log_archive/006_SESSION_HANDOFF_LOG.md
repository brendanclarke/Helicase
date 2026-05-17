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
DATE: 2026-04-29
SESSION GOAL: Resolve remaining display-corruption-under-load bugs from session 5; address encoder backspin/overflow reports; performance refactor of endless pots; collapse multi-knob repaints to reduce LCD queue saturation.
COMPLETED: Display corruption root cause found and fixed (lcd_q_count race → SPSC head/tail ring). Encoder backspin and parameter overflow fixed (three independent bugs: uint8 wrap, load-save underflow, rebound amplified by acceleration). EndlessPots performance refactor (compound-literal macros, volatile split, hot-path init guard). Knob repaint collapse for RV1-4 only (menu_knobs_dirty flag). SD card confirmed on bit-bang GPIO — SPI1 contention concern is moot. Two failed approaches documented (TIM7 idle gating, broad repaint coalescing).
VERIFIED ON HARDWARE: Rapid SELECT mashing stable. Single-press, multi-press, mode switching all clean. All three encoder backspin/overflow symptoms gone. Acceleration still correct. Pots feel identical after refactor. Multi-knob simultaneous spin smooth.

CHANGES THIS SESSION:
- lcd.c: lcd_q_count removed entirely. Replaced with SPSC head/tail ring (producer writes lcd_q_head only, consumer writes lcd_q_tail only). sendDisplayBuffer() now emits lcd_setcursor unconditionally before every data byte (position-tracking optimization removed).
- encoder.c: ts_dirs[ACCEL_BUF_SIZE] parallel direction buffer added. encode_read4() rebound suppression added — majority-direction check against ts_dirs[], returns val unaccelerated if majority opposes current val sign.
- menu.c: menu_encoderChangeParameter() boundary check replaced with int16 saturating add + clamp. menu_handleLoadSaveMenu() same fix. menu_parseKnobDelta() sets menu_knobs_dirty=1 instead of calling menu_repaintAll() directly. menu_serviceKnobRepaint() added — consumed once after RV1-4 for-loop in main loop.
- endlessPots.c: compound-literal index macros replaced with static const uint8_t qe_idx_a[]/qe_idx_b[]. Struct split: ISR-only fields (prev_angle, accumulator, initialised) made non-volatile; cross-thread fields (delta, changed) remain individually volatile. qe_all_initialised hot-path guard added.
- SD card: confirmed on bit-bang GPIO (PC12/PD2/PC8/PD0), NOT hardware SPI1. PA8 was a false positive — CMD0 0x00 response was 74HC165 driving MISO low. No spi1_sd_busy flag needed. All session 1-4 references to SPI1 contention are moot.

KNOWN ISSUES INTRODUCED: None.
KNOWN ISSUES RESOLVED: Display corruption under rapid SELECT mashing (lcd_q_count RMW race). Encoder backspin at param floor (rebound amplified by acceleration — ts_dirs[] fix). Parameter uint8 wrap on fast spin (int16 saturating add). Load/save preset number underflow on fast spin (int16 saturating add). EndlessPots volatile overhead.

NEXT SESSION RECOMMENDED GOAL: Audio integration — decide between (a) reshape circular DMA to call mixer fill on HT/TC interrupts, or (b) restructure to original double-buffer model. Then port pure DSP files (BufferTools, 1PoleLp, dither, Decay, SlopeEg2, snapEg, squareRootLut, transientGenerator, transientTables). Defer voice files until audio path ready.
BLOCKERS: None.

CRITICAL REMINDERS FOR NEXT SESSION:
- ALWAYS extract a fresh tarball before making changes. Verify working directory matches stated base before writing any code.
- EXTI_IMR = 0 must remain as the very first operation in main(), before sysclk_init()
- Do NOT add pull-down to PD4 or PD5
- lcd_init() must be called before lcd_tim7_init()
- TIM7_SR defined locally in timebase.c at 0x40001410 — do not remove
- menu_init() calls memset(parameter_values, 0, NUM_PARAMS) — do not also memset in main()
- DO NOT TOUCH endlessPots (RV1-4) — atan2f(b, a), do not change argument order
- 24-bit audio: int16_t MSW+LSW packed, I2SCFGR DATLEN=01/CHLEN=1
- PE13/PE14: AF1 (TIM1_CH3/CH4), external 10kΩ pull-ups, no internal pull-up
- Encoder: Dannegger, last = new & 3 in encode_init(), encode_read4() only — read1/read2 permanently removed
- Encoder rebound suppression: ts_dirs[] direction buffer in encoder.c — do not remove or simplify
- Saturation pattern in menu.c: int16 sum + clamp, NOT uint8 wrap + boundary check. Correct for any |inc| value.
- TIM1 IRQ27, TIM6 IRQ54, TIM7 IRQ55
- while → if in buttonHandler_processEvents() — intentional, do not revert
- LCD ring is head/tail-only (NO lcd_q_count). Do not reintroduce a shared count variable.
- sendDisplayBuffer emits setcursor before every data byte (no position-tracking optimization). Do not remove.
- SD card: bit-bang SPI on PC12/PD2/PC8/PD0 (NOT hardware SPI1). PA8 was a false positive. No SPI1 contention exists.
- I2S3_SD = PB5 (PC12 is SD SCLK — was misdocumented in early sessions)
- DO NOT ATTEMPT TIM7 idle gating — wakeup race causes intermittent freezes. See Part 5a.
- DO NOT ATTEMPT broad repaint coalescing across all input paths — caused intermittent freezes and lag. The current localized RV1-4 collapse is correct. See Part 5b.
- DTCM is NOT DMA-accessible — never tag a DMA buffer with INDTCM/INDTCMZ
- menu_knobs_dirty flag + menu_serviceKnobRepaint() is RV1-4-only. Do not extend dirty-flag coalescing to other input paths.
```

---

## Session 6 — 2026-04-29 (Display Race Fix, Encoder Bug Hunt, EndlessPots Refactor, Knob Repaint Collapse)

**Goal**: Resolve remaining display-corruption-under-load bugs from session 5; address encoder backspin/overflow reports; performance refactor of endless pots; collapse multi-knob repaints to reduce LCD queue saturation.

**Working tarball at start**: `lxr02_reorganized.tar.gz`  
**Working tarball at end**: `lxr02_knob_collapse.tar.gz` — current known-good base going forward.

---

### Part 1 — Display corruption under rapid input (resolved)

**Symptom**: Mashing SELECT buttons rapidly froze the top menu row showing content from a previous mode. Top row writes for new pages would land at wrong column offsets — e.g. cells expected at cols 0/4 appeared at cols 8/12. Persisted through subsequent input. The session 5 `while→if` fix had already removed the worst case but a residual case remained.

**Investigation path**: The visible offset pattern (writes shifted by 8 columns) initially suggested LCD address-counter desync from missed setcursor commands. Removing the position-tracking optimization in `sendDisplayBuffer` (always emit setcursor before each data write) was applied first — partial fix only.

**Root cause**: Non-atomic read-modify-write race on the `lcd_q_count` shared variable in lcd.c. Producer (main thread) did `lcd_q_count++`; consumer (TIM7 ISR) did `lcd_q_count--`. Both compile to `ldrb / adds / strb`. If TIM7 fires between the producer's load and store, the consumer's decrement is silently lost when the producer writes back its incremented value. Count overstates by one. Eventually the consumer dequeues a phantom slot containing stale bytes — these are interpreted as commands when RS is wrong, scrambling the LCD state.

**Fix**: Removed `lcd_q_count` entirely. Replaced with classic SPSC ring: producer only writes `lcd_q_head`, consumer only writes `lcd_q_tail`. Emptiness = `head == tail`. Fullness = `(uint8_t)(head - tail) >= LCD_QUEUE_SIZE`. uint8_t single-byte loads/stores are atomic on Cortex-M7. No shared RMW target — race is structurally impossible.

**Why setcursor-unconditional was also kept**: With the count race fixed alone, the LCD was still occasionally corrupting under multi-knob load. Combining the two changes resolved it. Position-tracking optimization that omitted setcursor on sequential writes is fragile — depends on the LCD never dropping a command. Cost of always emitting setcursor: ~32 extra enqueues per repaint, well within the 128-entry queue.

**Verified on hardware**: rapid SELECT mashing now stable. Single-press, multi-press, mode switching all clean.

---

### Part 2 — Encoder backspin and parameter overflow (resolved)

**Reported symptoms**:
- Spin CCW fast from value=0 → value lands at 4 (should stay at 0)
- 3 CCW clicks from kit=0 → arrives at kit 1
- Fast CCW from kit=6 → arrives at kit 217

**Three independent bugs**, all from the encoder acceleration patch (session 4) interacting with code that was written assuming `|inc|=1`:

**Bug A — `menu_encoderChangeParameter` boundary check fails for |inc| > 1.** Original code:
```c
if (inc > 0) {
    if (*paramValue != 255) *paramValue = (uint8_t)(*paramValue + inc);
}
```
With `paramValue=250, inc=+10`: `!= 255` passes → `(uint8_t)(260) = 4`. uint8 wrap into low range that the dtype clamps don't see as out-of-range.

**Fix**: Replaced with int16 saturating add:
```c
int16_t sum = (int16_t)*paramValue + (int16_t)inc;
*paramValue = (sum > 255) ? 255 : (uint8_t)sum;
```
Same pattern for CCW. Works for any |inc| value.

**Bug B — `menu_handleLoadSaveMenu` preset number same family.** `kit > 0` check passes when `kit=6, inc=-16`, then `(uint8_t)(6 + -16) = (uint8_t)(-10) = 246` underflow. Few more reads under acceleration → 217. Same int16 saturating fix applied.

**Bug C — Encoder rebound amplified by acceleration.** Mechanical encoders rebound 1-2 transitions in the wrong direction when stopped abruptly after a fast spin. Bare Dannegger reports these as `val=+1` (or `-1`). The acceleration buffer is still full of fast-interval timestamps from the spin, so the velocity multiplier is high. Result: a `+1` rebound gets multiplied by `4` and squirts through against the floor clamp that's blocking the legit CCW counts. User sees value=4 instead of value=0.

**Fix**: Added parallel `ts_dirs[ACCEL_BUF_SIZE]` buffer in encoder.c — each ISR-accepted transition stores its sign (+1/-1). In `encode_read4`, after the existing decay check, count majority direction in the buffer. If majority opposes the sign of current `val`, return `val` unaccelerated (so a rebound shows up as ±1 max, not ±4). Genuine direction reversals (CCW→CW with the buffer still showing CCW) get rebound-suppressed for one read, then accelerate normally — acceptable feel cost.

**Verified on hardware**: All three reported symptoms gone. Acceleration still feels right for fast spins. No regressions on slow turns.

---

### Part 3 — EndlessPots performance refactor (no behavior change)

**Goal**: Reduce TIM6 ISR overhead from `endlessPots_tick()`. Working baseline was ~560 cycles/tick = 0.26% CPU.

**Issues found**:
- `ADC_IDX_QE_A(i)` / `ADC_IDX_QE_B(i)` were compound-literal macros allocating fresh 4-byte stack arrays on every call. ~16 cycles wasted per index, 8 indices/tick = ~128 cycles wasted.
- `qe[]` was declared `volatile`, forcing every field read/write to memory. Accumulator math touched fields 7+ times per encoder × 4 encoders = ~30 forced memory accesses that could be register-cached.
- `if (!qe[i].initialised)` checked every tick forever, despite only being meaningful at startup.

**Refactor**:
- Replaced compound-literal macros with `static const uint8_t qe_idx_a[QUAD_ENC_COUNT]` and `qe_idx_b[QUAD_ENC_COUNT]`. Single LDRB per index.
- Split struct: `prev_angle`, `accumulator`, `initialised` are ISR-only and now non-volatile. Compiler keeps them in FPU/integer registers across the loop. `delta` and `changed` remain individually `volatile` (cross-thread access from main-loop `getDelta`).
- Added `qe_all_initialised` counter incremented when an encoder transitions out of uninitialised state. Once it reaches `QUAD_ENC_COUNT`, `endlessPots_tick` takes a hot path without the per-iter init check.

**Estimated savings**: ~220 cycles/tick = 0.10% CPU. Small absolute number, but the change is small and safe.

**Behavioural equivalence**: Confirmed identical math sequence, same atan2f argument order, same scale factor, same wrap thresholds at ±π, same integer-emit threshold. The unconditional fractional-retention statement (`q->accumulator = acc - (float)inc`) is equivalent to the original conditional form because when `inc==0`, `acc - 0.0f == acc`.

**Verified on hardware**: Pots feel identical. No drift, no stuck deltas, no direction issues.

---

### Part 4 — Knob repaint collapse (RV1-4 only)

**Problem**: Turning multiple endless pots simultaneously produces multiple `menu_repaintAll()` calls per main loop iteration (one per knob with delta). Each repaint enqueues 30+ LCD ops. With 4 knobs spinning, the LCD queue saturates, `lcd_enqueue` spins on full, and the screen visibly lags behind input.

**Solution**: Localized collapse on the RV1-4 read path only.
- `menu_parseKnobDelta` sets `menu_knobs_dirty = 1` instead of calling `menu_repaintAll()` directly.
- New `menu_serviceKnobRepaint()` consumes the flag and calls `menu_repaintAll()` once.
- Main loop calls `menu_serviceKnobRepaint()` once after the RV1-4 read for-loop.

**Result**: Multi-knob simultaneous spin is smooth. Single-knob and stationary cases unchanged.

**Verified on hardware**: smooth response to all 4 knobs spinning simultaneously. No regressions on single-knob, button, encoder, or kit load paths.

---

### Part 5 — FAILED ATTEMPTS (do not retry without extreme deliberation)

Two approaches were attempted during this session and abandoned. Both seemed reasonable in the abstract but caused worse problems than they solved. Documented here so future sessions don't repeat them.

#### 5a. TIM7 idle gating

**Idea**: In `lcd_tim7_tick` SM_IDLE, when queue is empty, write `TIM7_CR1 = 0` to stop the timer. In `lcd_enqueue`, write `TIM7_CR1 = 1` to restart it. Saves ~0.23% CPU continuously.

**Why it broke**: Genuine wakeup race between producer's `TIM7_CR1=1` and consumer's `TIM7_CR1=0`. Specific failure sequence:
1. ISR enters SM_IDLE, reads `head == tail` → true
2. Producer writes new entry, increments head
3. Producer writes `TIM7_CR1 = 1` (timer is still running, this is a no-op)
4. ISR writes `TIM7_CR1 = 0`
5. Result: queue has work, TIM7 is stopped, no further wake. `lcd_enqueue` then deadlocks the main loop on full-queue spin. LCD + LEDs + everything else freeze.

**Memory ordering doesn't save this** because there's no way to atomically order the producer's two writes (head++, then CR1=1) with the consumer's two reads (count check, then CR1=0) without a critical section.

**The race is rare** in steady state but reliable under bursty patterns where the queue repeatedly transitions empty↔non-empty (multi-knob spin with incremental repaint).

**Lesson**: Do not gate TIM7. The 0.23% CPU saving is not worth the freeze risk. Proper gating would require either (a) a critical section around the producer's CR1 write, (b) a "work-pending" bit distinct from the queue indices, or (c) a software-managed wake on a different mechanism. Defer until ISR architecture is settled by the sequencer port.

#### 5b. Broad repaint coalescing across all input paths

**Idea**: Apply the dirty-flag pattern to ALL input handlers — encoder turns, button presses, kit loads, knob turns. Single `menu_serviceRepaint()` at end of main loop iteration consumes both `menu_dirty` and `menu_dirty_full` flags.

**Why it broke**: Multiple symptoms appeared, including:
- Top row of display lagging behind input by 1-3 events ("VOK dec slp" after pressing OSC, etc.)
- Underline cursor in save page appearing in wrong cells, or only after dozens of clicks
- Eventually outright freezing on certain input sequences

**Suspected cause**: Mix of issues. The `menu_dirty_full = 1` setting from knob deltas was overkill — knob value changes are layout-stable and should only need incremental repaint, but a previous version of the patch set `_full` on all knob events causing currentDisplayBuffer to be cleared to 0x7F repeatedly. Cursor on/off state interleaving across queue boundaries became inconsistent. The cascade of interactions between the boot path (which uses synchronous `menu_repaintAll`) and the runtime flag-driven path created edge cases that were hard to reproduce systematically.

**Lesson**: The original AVR `menu_repaint*` callsite pattern was carefully designed around the original's blocking LCD. F765 with async TIM7 LCD has different behaviors at the boundaries. Broad coalescing requires extreme care: every input path interacts with `editDisplayBuffer`, `currentDisplayBuffer`, cursor state, repaint type (incremental vs full), and the boot synchronous path differently.

The localized RV1-4-only collapse worked because:
- It's a single call site (one for-loop in main.c)
- It doesn't change behavior of any other input path
- The flag is consumed in exactly one place
- It doesn't interact with cursor state, edit mode, page switching, or kit load

**Future approach**: When the sequencer is in place and step-LED-update needs to be a fifth source of "screen needs repaint", design the dirty flag protocol from scratch with careful attention to the cursor-state and boot-synchronous-init edge cases. Do not attempt to incrementally extend the current localized collapse to other paths without a clear design.

---

## Known Issues / Technical Debt (end of Session 6)

- buttonHandler/ledHandler audit not yet done (compare against original AVR sources for action logic)
- TIM2 not initialised for CLK IN BPM interval measurement
- MidiParser RX not connected to sequencer/parameter system
- Preset save not implemented (load only)
- Pattern load/save not implemented
- Morph buffer not connected (parameters2[] declared, unused)
- Slider-to-parameter mapping not designed
- Sequencer, DSPAudio, SampleRom directories empty

## Critical Reminders for Next Session

- **ALWAYS extract a fresh tarball before making changes. Verify working directory matches stated base before writing any code.**
- EXTI_IMR = 0 must remain as the very first operation in main(), before sysclk_init()
- Do NOT add pull-down to PD4 or PD5
- `lcd_init()` must be called before `lcd_tim7_init()`
- TIM7_SR defined locally in timebase.c at 0x40001410 — do not remove
- `menu_init()` calls `memset(parameter_values, 0, NUM_PARAMS)` — do not also memset in main()
- DO NOT TOUCH endlessPots (RV1-4) — `atan2f(b, a)`, do not change argument order
- 24-bit audio: `int16_t` MSW+LSW packed, I2SCFGR DATLEN=01/CHLEN=1
- PE13/PE14: AF1 (TIM1_CH3/CH4), external 10kΩ pull-ups, no internal pull-up
- Encoder: Dannegger, `last = new & 3` in encode_init(), encode_read4() only — read1/read2 permanently removed
- Encoder rebound suppression: `ts_dirs[]` direction buffer in encoder.c — do not remove or simplify
- Saturation pattern in `menu_encoderChangeParameter` and `menu_handleLoadSaveMenu`: int16 sum + clamp, NOT uint8 wrap + boundary check
- TIM1 IRQ27, TIM6 IRQ54, TIM7 IRQ55
- `while → if` in buttonHandler_processEvents() — intentional, do not revert
- LCD ring is head/tail-only SPSC (NO `lcd_q_count`). Do not reintroduce a shared count variable.
- `sendDisplayBuffer` emits `lcd_setcursor` before every data byte. Do not add position-tracking optimization.
- SD card: bit-bang SPI on PC12/PD2/PC8/PD0 (NOT hardware SPI1). PA8 was a false positive. **No SPI1 contention exists.**
- I2S3_SD = PB5 (PC12 is SD SCLK — was misdocumented in early sessions)
- **DO NOT ATTEMPT TIM7 idle gating.** Wakeup race between producer's `TIM7_CR1=1` and consumer's `TIM7_CR1=0` causes intermittent main-loop freezes. Confirmed broken. See Part 5a.
- **DO NOT ATTEMPT broad repaint coalescing across all input paths.** Caused intermittent freezes and display lag. See Part 5b.
- DTCM is NOT DMA-accessible — never tag a DMA buffer with INDTCM/INDTCMZ
- `menu_knobs_dirty` flag + `menu_serviceKnobRepaint()` is RV1-4-only. **Do not extend dirty-flag coalescing to other input paths.**

## Architectural Notes for Sequencer/DSP Port (recorded for next session)

These were discussed but not implemented this session. Recording them now so the next session has the design baseline before writing code.

**Sequencer step processing**: Dedicated TIM ISR (TIM3 candidate, free 32-bit on PCLK1=54MHz), priority 0. One-shot reloadable: ISR fires `seq_nextStep()`, computes next deadline, reloads timer. Bounded jitter (tens of nanoseconds preemption latency on M7). Trigger jack outputs from same ISR; gate-off via TIM compare match for tight pulse width.

**External clock sync (CLK IN, PD4)**: EXTI ISR captures TIM2_CNT timestamp. Sequencer step ISR reads timestamp asynchronously and phase-corrects its deadline. EXTI ISR itself only timestamps + sets a flag; doesn't change sequencer state directly.

**MIDI clock IN/OUT**: Both ISR-driven for sub-100µs jitter. Inbound: USART RX ISR parses byte-by-byte, recognizes 0xF8 (clock) immediately. Should also timestamp via TIM2_CNT at start-of-byte to remove ISR latency contribution. Outbound: same TIM ISR that drives sequencer steps generates clock pulses (24 ppqn locks 6:1 to 16th-step clock). 1ms jitter is musically unacceptable; original LXR's main-loop polling has 100-300µs (mediocre). Target <100µs.

**DSP**: Runs from I2S DMA half-complete + transfer-complete ISRs at priority 0. Per-parameter smoothing inside DSP loop handles zipper noise from coarse parameter changes.

**Kit load while sequencer runs**: Works via byte-atomic writes to `parameter_values[]` from main loop; sequencer ISR reads byte-atomic. No torn reads (uint8_t loads/stores are atomic on M7). DSP smoothing crossfades each parameter to its new value over a few ms — same musical behavior as original LXR.

**Priority demotion when DSP/sequencer go in**: TIM6 (LED/button SPI) currently priority 1. Should drop to 2 or 3 to let DSP DMA ISRs claim 0/1. TIM1 (encoder) similarly.

**SPI1 contention concern is moot**: SD is on bit-bang GPIO, not SPI1. No `spi1_sd_busy` flag needed. The session 1 through session 4 references to "SPI1 contention" all predate the SD hardware confirmation.

## Next Session Recommended Goal

Sequencer port. Read original `mainboard/LxrStm32/src/Sequencer/sequencer.c` first. Plan ISR architecture before writing code. The architectural notes above should be the design baseline. Defer DSP and audio engine until sequencer is wired and tested with stub triggers.

Alternative if sequencer port feels too large: buttonHandler/ledHandler audit. Compare against `front/LxrAvr/buttonHandler.c` case-by-case. Our `processPress()` switch needs verification against original `buttonHandler_buttonPressed()` for each button mode combination, and the LED index mapping needs verifying against the shift-register chain order.
