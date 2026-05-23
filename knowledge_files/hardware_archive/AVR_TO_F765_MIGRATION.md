# AVR → STM32F765 Migration Map

## Original LXR Architecture

The original LXR had two processors communicating over UART at 500kbaud:

- **ATmega644 (AVR)** — "front panel": LCD, buttons, LEDs, encoders, sliders, SD card, menu system, preset management, frontPanelParser
- **STM32F4 (Cortex)** — "mainboard": audio DSP, sequencer, MIDI, trigger jacks, USB

They communicated via a structured serial protocol (frontPanelParser.h) using pseudo-MIDI messages (LED_CC, SEQ_CC, VOICE_CC, CODEC_CC, SET_BPM, etc.) over USART. The AVR sent button/encoder events; the STM32 sent LED state, sequencer state, and audio parameters.

## LXR-02 Architecture

Everything runs on a single **STM32F765VIH6**. The AVR is gone. All former AVR responsibilities are now handled directly by the F765.

---

## AVR Responsibility → F765 Implementation

### 1. LCD (4-bit parallel)
- **AVR**: Bit-banged on PORTC/PORTB, blocking `_delay_us()` in avr/delay.h
- **F765**: PE7-PE10 (DB4-7), PE11 (E), PE12 (RS). Non-blocking TIM7-driven async queue (128 entries, 10kHz drain rate). lcd_init() still blocking but runs before TIM7 starts — safe. **Status: working, robust under rapid input**.
- **Resolved pitfall**: The original blocking lcd_wait_ms() approach risked deadlock under TIM6 load. Replaced with TIM7 state machine (IRQ55, priority 2). All lcd_* calls enqueue ops and return immediately. lcd_init() remains blocking but only runs pre-TIM7.
- **Resolved pitfall (Session 6)**: Display corruption under rapid input was caused by a non-atomic RMW race on `lcd_q_count` shared between producer (main thread) and consumer (TIM7 ISR). Replaced with classic SPSC ring (head/tail only, no shared count). Race is now structurally impossible on Cortex-M7. Do not reintroduce a shared count variable.
- **Resolved pitfall (Session 6)**: `sendDisplayBuffer` previously had a position-tracking optimization that omitted setcursor on sequential writes. This depended on the LCD never dropping a command. Now emits setcursor before every data byte — verbatim original AVR behaviour. Cost is bounded (~32 extra enqueues per repaint, well within 128-entry queue).

### 2. Buttons (74HC165 ×5 shift registers)
- **AVR**: Bit-banged CLK/LOAD/DATA pins, polled in main loop via din_readNextInput(). Active HIGH. Debounced per-bit. Fired buttonHandler_buttonPressed/Released. **One button processed per main loop iteration.**
- **F765**: SPI1 full-duplex with 74HC595 (LEDs) simultaneously. PA6 (MISO), PA7 (MOSI), PB3 (SCK), PB2 (LATCH). Exchange happens in **TIM6_DAC_IRQHandler at 1kHz**. All 40 bits exchanged atomically. buttonHandler_buttonPressed() writes to an ISR-safe event ring — all menu/LCD actions deferred to buttonHandler_processEvents() in main loop. **processEvents() uses `if` not `while` — processes one event per main loop iteration to match original serialization.** **Status: working**.
- **Key difference**: On AVR, din was polled in the main loop ~10 bits at a time. On F765, all 40 bits are exchanged atomically in the ISR every 1ms. The one-event-per-iteration discipline in processEvents() is critical — without it, simultaneous button presses flood the LCD queue and corrupt the display.

### 3. LEDs (74HC595 ×5 shift registers)
- **AVR**: Bit-banged, dout_updateOutputs() in main loop. SW43 LED was separate.
- **F765**: Same SPI1 as buttons (full-duplex). Shadow array `dout_outputData[]`. LATCH pulse in TIM6 ISR loads new data. SW43 LED on PB8 (separate GPIO). **Status: working**.

### 4. Main Encoder (rotary + button)
- **AVR**: Interrupt-driven via Timer0 CTC at ~1kHz. PE13/PE14/PE15 on AVR.
- **F765**: PE13 (A), PE14 (B) as **AF1 (TIM1_CH3/CH4)**. TIM1 Input Capture with hardware digital filter ICxF=0xF — forces signal stable for 32 samples before acknowledging transition. Hardware debounce at silicon level. Dannegger difference algorithm in TIM1_CC_IRQHandler (IRQ27). encode_read4() only — read1/read2 permanently removed. Acceleration: 8-entry circular timestamp buffer, 1×–4× multiplier based on average interval. PE15 (SW): internal pull-up, polled in TIM6 at 1kHz for button debounce. **Status: working, rock solid**.
- **Critical**: PE13/PE14 have external 10kΩ pull-ups (R75/R76). Do NOT enable internal pull-ups — fights the external circuit. encode_init() seeds `last = new & 3` (actual current state, no phase offset). encode_read4() is verbatim from original LXR — do not modify.
- **Resolved pitfall (Session 6)**: Mechanical encoder rebound (1-2 transitions in wrong direction when stopped abruptly after a fast spin) was being amplified by the velocity multiplier into spurious 4× counts in the wrong direction. Added parallel `ts_dirs[]` direction buffer in encoder.c — `encode_read4` now suppresses acceleration when current `val` sign opposes the majority direction in the recent transition history. Genuine direction reversals get one unaccelerated read, then accelerate normally. Do not remove this suppression.
- **Resolved pitfall (Session 6)**: `menu_encoderChangeParameter` and `menu_handleLoadSaveMenu` originally used uint8_t boundary checks (`!= 255` for CW, `>= -inc` for CCW) that assumed `|inc|=1`. With acceleration `|inc|` can be up to 4. Fixed both with int16 saturating-add pattern. Do not revert to the boundary-check pattern.

### 5. Endless Pots / Analog Quadrature Encoders (RV1-4, dual-phase pots)
- **AVR**: 4-channel ADC, polled in main loop. Hysteresis-based edge detection.
- **F765**: ADC1 14-channel circular DMA scan. PB0/PB1, PC4/PC5, PC2/PC3, PC0/PC1. atan2-based delta tracking in **endlessPots.c** (formerly quadEnc.c). `atan2f(b, a)` where a=A-2048, b=B-2048. DO NOT change argument order. Scale factor 31.83 → 200 increments per full CW revolution. Fractional accumulator absorbs ADC noise. **Status: working**.
- **Note**: These are analog sine/cosine pots, not digital Gray code encoders. One electrical cycle per full 360° mechanical revolution.
- **Performance refactor (Session 6)**: Replaced compound-literal ADC index macros with static const lookup tables. Split struct so ISR-only fields (prev_angle, accumulator, initialised) are non-volatile and register-cacheable; cross-thread fields (delta, changed) remain individually volatile. Hot/cold path split bypasses init check after first few ticks. Behaviour identical, ~0.10% CPU savings.
- **Repaint collapse (Session 6)**: `menu_parseKnobDelta` no longer calls `menu_repaintAll()` directly — sets `menu_knobs_dirty=1` flag. Main loop calls `menu_serviceKnobRepaint()` once after the RV1-4 read for-loop. Multi-knob simultaneous spin produces one coalesced repaint instead of four. **This collapse is RV1-4-only — do not extend to other input paths without a fresh, careful design (see SESSION_LOG_merged.md Session 6 Part 5b for why broad coalescing failed).**

### 6. Sliders (RV5-10, single-phase pots)
- **AVR**: Same ADC, polled in main loop.
- **F765**: PA0-PA5 (ADC1 CH0-5), same DMA scan. adcPots.c, polled in main loop via adc_checkPots(). **Status: working**.
- **Note**: The sliders are a new hardware addition on LXR-02 not present on original LXR. Slider-to-parameter mapping not yet designed.

### 7. SD Card / FatFS
- **AVR**: SPI via dedicated AVR SPI peripheral. FatFS library (ff.c). CS on AVR GPIO.
- **F765**: **Bit-bang SPI on dedicated GPIO pins — NOT hardware SPI1.** PC12=SCLK, PD2=MOSI, PC8=MISO, PD0=CS. Implemented in Core/Hardware/SD/spi_sd.c. FatFS ported in Core/Hardware/SD/ff.c. **Status: working. Kit load (P000.SND) and globals (GLO.CFG) load on boot confirmed on hardware.**
- **Resolved pitfall**: Original documentation incorrectly stated SD was on SPI1/PA8. PA8 CMD0 response of 0x00 was a false positive — the 74HC165 button shift register was driving MISO low, not the SD card. Actual pins confirmed by physical tracing. **No SPI1 contention issue exists — SD is on completely separate GPIO pins.** All session-1-through-4 references to "SPI1 contention" or `spi1_sd_busy` flags predate the SD hardware confirmation and are obsolete.

### 8. Menu System / Parameter Management
- **AVR**: Full menu.c, parameter_values[], presetManager.c, copyClearTools.c. Ran entirely on AVR, sent parameter updates to STM32 via frontPanel_sendData().
- **F765**: Fully ported. Direct function calls replace serial protocol. parameter_values[] (NUM_PARAMS=275), full page table (16 pages × 8 sub-pages), all dtypes, value names, load/save page UI, encoder navigation, quad encoder parameter editing. frontPanel_sendData() stubbed until DSP connected. **Status: working**.

### 9. Preset Management
- **AVR**: presetManager.c, SD card read/write via FatFS.
- **F765**: Ported (load only). preset_loadDrumset() reads 8-byte name + END_OF_SOUND_PARAMETERS bytes. preset_loadGlobals() reads GLO.CFG. Both called on boot if SD card present. Save not yet implemented. **Status: load working**.
- **Critical observation for sequencer port**: Kit load is a flat byte-blob `f_read` into `parameter_values[]`. It does NOT touch pattern data, step data, or sequencer state. When sequencer is in place, kit-load-during-playback works via byte-atomic writes — uint8_t loads/stores are atomic on Cortex-M7, so no torn reads. DSP smoothing in the audio path will crossfade parameters; same musical behavior as original LXR.

### 10. MIDI DIN
- **AVR**: Received MIDI from STM32 via UART and processed it (MIDI channel routing, CC handling). Sent front-panel-generated notes back to STM32.
- **F765**: USART3, PB10 (TX), PB11 (RX), 31250 baud, BRR=0x06C0. Direct hardware MIDI. **Status: TX working. RX parsing (MidiParser) not yet connected**.

### 11. USB MIDI
- **AVR**: Not present on original LXR.
- **F765**: OTG_FS PA11/PA12 via ADUM3160 isolator. USB MIDI device class (Sonic Potions USB MIDI). **Status: enumerates, TX working**.

### 12. Trigger/Clock Jacks
- **Original LXR STM32F4**: TriggerOut.c — trigger outputs on GPIOD (PD8-PD14), CLK1 on PD15, CLK2/Reset on PA9/PA10. CLK IN on PC9, RST IN on PA8. EXTI9_5_IRQHandler with TIM2 for clock interval measurement.
- **F765**: CLK OUT = PC13. CLK IN = PD4 (EXTI4 rising edge). RST IN = PD5 (EXTI5 rising edge through EXTI9_5_IRQHandler). TIM2 is the free-running 1 MHz timestamp source for CLK/RST and MIDI realtime capture. **Status: hardware confirmed and wired into the sequencer timing owner.**
- **Critical**: EXTI4/EXTI5 are armed by the bootloader. EXTI_IMR must be cleared at the top of main() before any init. This is done.
- **Trigger outputs** (7 voice triggers + CLK1 + CLK2 + Reset): On original LXR these were GPIOD and GPIOA. On LXR-02 these jacks are NOT present — the LXR-02 only has CLK OUT, CLK IN, RST IN. The 7 voice trigger outputs and second clock output from the original expansion header do not have corresponding jacks on the LXR-02.

---

## Inter-processor Protocol — No Longer Needed

The entire frontPanelParser.h message protocol (LED_CC, SEQ_CC, VOICE_CC, SET_BPM, etc.) was the AVR↔STM32 communication layer. On LXR-02 this is replaced by **direct function calls**. Key implications:

- `frontPanel_sendData(LED_CC, LED_CURRENT_STEP_NR, step)` → direct call to ledHandler
- `frontPanel_sendData(SEQ_CC, SEQ_RUN_STOP, 1)` → direct call to sequencer
- `frontPanel_parseData()` / `frontPanel_sendMidiMsg()` → eliminated
- All the UART ISRs (USART0_RX_vect, USART0_UDRE_vect) → eliminated
- The AVR's `uart_checkAndParse()` main loop call → eliminated

When porting AVR source files, any call to `frontPanel_sendData()` or `frontPanel_sendMidiMsg()` must be replaced with the equivalent direct call into the STM32-side function it was invoking remotely.

---

## Timing Differences

| Function | AVR | F765 |
|----------|-----|------|
| Main timebase | Timer2 overflow ~76Hz (13ms) | TIM6 at 1kHz (1ms) |
| Encoder poll | Timer0 CTC ~1kHz | TIM1 IC event-driven (hardware filter) |
| Button poll | Main loop (~10 bits/call, one per iteration) | TIM6 ISR (all 40 bits), one event processed per main loop iteration |
| LED update | Main loop | TIM6 ISR |
| LCD update | Blocking, inline | TIM7 async queue, 10kHz drain |
| Audio | STM32F4 DMA | STM32F765 DMA (same) |

The F765 TIM6 runs 13× faster than the AVR timer. Any AVR code that uses `time_sysTick` for timing (delays, debounce counts, pulse widths) needs to be reviewed — AVR ticks were ~13ms each; F765 ticks are 1ms each.

---

## Architectural Notes for Sequencer/DSP Port

These were established in Session 6 discussions but not implemented yet. Recording them here as the design baseline for the sequencer port.

### Sequencer step processing
- Dedicated TIM ISR (TIM3 candidate — free 32-bit on PCLK1=54MHz), priority 0
- One-shot reloadable: ISR fires `seq_nextStep()`, computes next deadline, reloads timer
- Bounded jitter (tens of nanoseconds preemption latency on M7)
- **Do NOT run sequencer from main loop**. The original LXR did this (`seq_tick()` polled from main with self-correction), but worst-case main-loop blocking under multi-knob input or kit load can be 10s of ms — well beyond audible musical jitter (>1ms is audible). LXR-02 should target <100µs sequencer jitter.

### Trigger jack outputs
- CLK OUT pulse-on from same step ISR
- Pulse-off via TIM compare match for tight sub-µs gate-end timing

### External clock sync (CLK IN, PD4)
- EXTI ISR captures TIM2_CNT timestamp at moment of pulse arrival
- Sequencer step ISR reads timestamp asynchronously and phase-corrects deadline
- EXTI ISR itself only timestamps + sets a flag; doesn't change sequencer state directly

### MIDI clock IN/OUT
- Both ISR-driven for sub-100µs jitter
- Inbound: USART RX ISR parses byte-by-byte, recognizes 0xF8 (clock) and 0xFA/FB/FC (start/continue/stop) immediately. Should also timestamp via TIM2_CNT at start-of-byte to remove ISR latency contribution from external sync calculations.
- Outbound: same TIM ISR that drives sequencer steps generates clock pulses (24 ppqn locks 6:1 to 16th-step clock). Avoids any main-loop blocking issues.

### DSP audio engine
- Runs from I2S DMA half-complete + transfer-complete ISRs at priority 0
- Per-parameter smoothing inside DSP loop handles zipper noise from coarse parameter changes
- Parameter changes from main loop reach DSP via byte-atomic writes to `parameter_values[]` — no protocol, no queue, no blocking

### Kit load while sequencer runs
- Main loop calls `f_read(parameter_values, ...)` as a flat byte blob
- Each individual byte write is atomic on M7
- Sequencer ISR reads byte-atomic from same array — no torn reads possible
- DSP smoothing crossfades each parameter to its new value over ~10ms
- Brief audible crossfade between kits, no dropouts, no missed steps. Same musical behavior as original LXR.

### Priority demotion
When DSP/sequencer ISRs go in:
- TIM6 (LED/button SPI) currently priority 1 → drop to 2 or 3
- TIM1 (encoder) currently priority 1 → drop to 2 or 3
- DSP DMA ISRs claim priority 0
- Sequencer step TIM ISR claims priority 0 or 1

---

## Failed Approaches — Do Not Retry Without Extreme Deliberation

### TIM7 idle gating (attempted Session 6)

**Idea**: Stop TIM7 in SM_IDLE when queue empty; restart from `lcd_enqueue`.

**Failure mode**: Genuine wakeup race between producer's `TIM7_CR1=1` and consumer's `TIM7_CR1=0`. Specific sequence:
1. ISR enters SM_IDLE, reads `head == tail` → true
2. Producer writes new entry, increments head
3. Producer writes `TIM7_CR1 = 1` (timer is still running, this is a no-op)
4. ISR writes `TIM7_CR1 = 0`
5. Result: queue has work, TIM7 is stopped, no further wake. Main loop deadlocks on full-queue spin in `lcd_enqueue`. LCD + LEDs + everything else freeze.

The race manifests intermittently under bursty input where the queue repeatedly transitions empty↔non-empty. Memory ordering doesn't save this — there's no atomic way to order producer-head++/producer-CR1=1 vs consumer-count-check/consumer-CR1=0.

**Lesson**: 0.23% CPU saving is not worth freeze risk. Proper gating requires (a) critical section around producer's CR1 write, (b) a "work-pending" bit distinct from queue indices, or (c) software-managed wake on a different mechanism. Defer until ISR architecture is settled by sequencer port.

### Broad repaint coalescing across all input paths (attempted Session 6)

**Idea**: Apply dirty-flag pattern to ALL input handlers — encoder, buttons, kit load, knobs. Single `menu_serviceRepaint()` at end of main-loop iteration.

**Failure mode**: Multiple symptoms — top row lagging behind input, underline cursor in wrong cells, eventual freezes. Cause was not narrowed precisely; suspected interactions between `editDisplayBuffer`/`currentDisplayBuffer`/cursor-state/incremental-vs-full-repaint at boundaries between flag-driven runtime path and synchronous boot path.

**Lesson**: Original AVR `menu_repaint*` callsite pattern was carefully designed around blocking LCD. Async TIM7 LCD has different boundaries. Broad coalescing requires extreme care: every input path interacts differently with shared display state.

The localized RV1-4-only collapse worked because it's a single call site, doesn't change behavior of any other input path, the flag is consumed in exactly one place, and it doesn't interact with cursor state, edit mode, page switching, or kit load.

When sequencer step LED updates need to be a fifth source of "screen needs repaint," design the dirty flag protocol from scratch with careful attention to cursor-state and boot-synchronous-init edge cases. Do not attempt to incrementally extend the current localized collapse.

---

## Key Unknowns / Risks

1. ~~**SPI1 contention** (SD vs LED/button)~~ — **RESOLVED**. SD card is on bit-bang GPIO (PC12/PD2/PC8/PD0), not SPI1. No contention.
2. ~~**TIM2 for CLK IN interval measurement**~~ — **RESOLVED Session 019/025**. TIM2 is initialised as a 1 MHz free-running timestamp counter. CLK IN on PD4 and RST IN on PD5 each use rising-edge EXTI and timestamp events without resetting TIM2.
3. ~~**Non-blocking LCD**~~ — **RESOLVED**. TIM7 async queue driver implemented. All lcd_* calls non-blocking post-init.
4. ~~**Display buffer desync under rapid input**~~ — **RESOLVED Session 6**. SPSC ring (no shared count) + unconditional setcursor.
5. **MidiParser RX**: USB MIDI RX and USART3 RX are plumbed but not connected to the sequencer/parameter system yet.
6. **Slider-to-parameter mapping**: Sliders are new hardware. No original mapping exists. Must be designed.
7. **GPIOD**: PD0=SD CS, PD1=SD DETECT, PD2=SD MOSI, PD4=CLK IN, PD5=RST IN, PD6=OUT1 L detect, PD7=OUT1 R detect — all confirmed. PD3 remains unknown; do not configure PD3 unless traced.
8. **buttonHandler/ledHandler audit**: Not yet done. Button numbering reversed from original (BUT_MODE1=31 vs original BUT_MODE1=35). Action logic needs case-by-case audit.
9. ~~**Preset save**: Load only.~~ — **RESOLVED**. Kit save (.SND) and globals save (.GLO) implemented and confirmed on hardware. Pattern/all/performance stubs in place.
10. ~~**Sequencer, DSP audio, Sample ROM**: Not started.~~ — **PARTIALLY RESOLVED**. DSP voice files ported to Core/DSPAudio/. Sequencer ported to Core/Sequencer/ (main-loop polling). SampleRom has a safe no-op stub (real flash impl blocked on F765 flash_if.c hardening). AudioCodecManager consolidated and wired. **See architectural notes for the remaining ISR-timing work.**

---

## Audio Codec / DSP Port — F765 Implementation Notes

### AudioCodecManager consolidation (Refactor session, 2026-05-04)

The original LXR STM32F4 had `AudioCodecManager.c` plus `cs4344_cs5343.c` for hardware init. On the F765 port, everything was first scattered across `AudioCodecManager.c`, `audioTest.c`, and `sineBufferTest.c` during the debug sessions. The refactor consolidates all of it into a single `Core/Hardware/AudioCodecManager.c`.

**Section order in consolidated file:**
1. Register definitions (DMA1_BASE, HISR/HIFCR bit masks)
2. Buffer declarations (dma_buffer*, audioOutBuffer*)
3. SPSC queue state (ready_queue[], head/tail, render_slot, underrun counter)
4. Public API (getRenderBuffer, queueFreeSlots, commitRenderBuffer)
5. Internal pack helpers (static pack_half, pack_audio_half)
6. DMA ISRs — Stream 4 (DAC2, refill master) and Stream 7 (DAC1, slave/flags-only)
7. Hardware init (PLLI2S, GPIO, DMA, I2S — absorbed from former audioTest.c)
8. audioCodec_init() — single entry point replacing CodecInit() + audioTest_init()
9. CodecInit() legacy wrapper (thin wrapper for source compatibility)
10. audioCodec_renderSineBlock() — test utility absorbed from sineBufferTest.c

**Key changes vs. original F4:**
- `bCurrentSampleValid` flag replaced by 2-slot SPSC ready queue (no mutex needed — byte-atomic on M7)
- `audioCodec_packHalf()` public wrapper removed; ISRs call `pack_audio_half()` directly
- Both DMA Stream ISRs now in this file; `audioTest_init()` absorbed; `sineBufferTest_init()` absorbed
- `audioCodec_init()` is the sole boot-time entry point
- `lcd_diagDisplayInt()` and `lcd_diagDisplayFloat()` added to lcd.c for diagnostic display of counters/floats

### DMA stream flag notes (F765 sharp edge)
LISR/LIFCR control streams 0-3. HISR/HIFCR control streams 4-7. Stream 4 (DAC2) and Stream 7 (DAC1) both live in HISR/HIFCR:
- Stream 4 flags: bits 0-5 (TCIF4=5, HTIF4=4) in HISR
- Stream 7 flags: bits 22-27 (TCIF7=27, HTIF7=26) in HISR

### GetRngValue() masking requirement
All calls to `GetRngValue()` must apply the mask `& 0x7FFF`:
```c
uint16_t rnd = (int16_t)(GetRngValue() & 0x7FFF);
```
Root cause not fully investigated. The masking is the confirmed working approach; removing it causes DSP corruption. All call sites in the refactored codebase have this mask applied.

### ISR scheduling note (for future investigation)
With the current implementation, DMA Stream 4 is the refill master and drives the pack step for both buffers. Stream 7 is a slave that only clears flags. There may be timing interactions between the two ISRs and the render buffer that warrant investigation when moving to a tighter sequencer integration. The design is currently working (78 startup underruns, then stable), but this is worth revisiting when the sequencer ISR comes in and priority levels are adjusted.
