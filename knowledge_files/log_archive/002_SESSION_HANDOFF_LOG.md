# Session Handoff Log — Session 2

---

**Project**: LXR-02 firmware port (STM32F765VIH6)
**DATE**: 2026-04-21
**SESSION GOAL**: Non-blocking LCD driver (TIM7), quad encoder atan2 port, frontPanelParser audit, menu stub, encoder direction/response fixes.

---

## Part 1 — Non-blocking LCD driver (TIM7)

**Completed**:
- `Core/Hardware/lcd.c` rewritten: `lcd_init()` stays blocking (pre-TIM7), all post-init functions enqueue into a 64-entry circular queue and return immediately
- `Core/Hardware/lcd.h`: same public API, adds `lcd_tim7_init()` and `lcd_tim7_tick()`
- `Core/Hardware/timebase.c`: added `TIM7_IRQHandler` (calls `lcd_tim7_tick()`), fixed duplicate `triggerJacks_isrTick()` bug, added `#include "lcd.h"`
- `Core/Hardware/timebase.h`: updated comments documenting both TIM6 and TIM7
- `Core/Src/startup_stm32f765xx.s`: broke `.rept 12` to expose IRQ55 as `TIM7_IRQHandler`, `.rept 11` for IRQ56-66
- `main.c`: added `lcd_tim7_init()` call after `time_initTimer()`
- `triggerJacks.c`: GPIOD state now shown for 2 seconds on change only (not permanently locked), with baseline snapshot on first call to suppress spurious boot display

**TIM7 configuration**:
- IRQ55, priority 2 (below TIM6 priority 1, above USB priority 5)
- PSC=107, ARR=99 → 10kHz (100µs/tick)
- State machine: 4 ticks per byte (E high 100µs, E low 100µs, repeat for low nibble, then wait)
- Normal post-byte wait: 1 tick (100µs). CLEAR/HOME wait: 20 ticks (2ms)
- TIM7_SR defined locally in timebase.c at 0x40001410 — do not remove

**Verified on hardware**: Working. Display shows menu correctly.

---

## Part 2 — Quad encoder atan2 rewrite (RV1-4)

**Hardware confirmed**: RV1-4 are analog sine/cosine encoders (not Gray code). One electrical cycle per full 360° mechanical revolution. Two continuous voltage outputs 90° apart, centred at ~1.67V (ADC midpoint 2048), amplitude ~2048 counts. PIN3 leads PIN1 by 90° clockwise.

**Completed**:
- `Core/IO/quadEnc.c`: complete rewrite replacing Gray code state machine with atan2-based tracking
  - `atan2f(b, a)` where a=A-2048, b=B-2048 — CW = positive delta
  - Scale factor 31.83 → 200 integer increments per full CW revolution
  - Fractional accumulator absorbs ADC noise, no false counts at rest
  - New API: `quadEnc_getDelta(i)` replaces `getValue()`/`getRawCount()`
  - `quadEnc_hasChanged(i)` retained
- `Core/IO/quadEnc.h`: updated API, added `QUAD_TOUCH_THRESHOLD 20`

**Snapshot/touch mechanism** (in main.c and menu layer):
- On page change (or when any encoder moves): snapshot raw ADC values of all OTHER encoders
- Snapshotted encoder's delta suppressed until either channel moves > QUAD_TOUCH_THRESHOLD (20 counts) from snapshot
- Prevents noise registering as movement on untouched encoders after page switch

**Verified on hardware**: RV1-4 working correctly. Good response, correct direction (CW = increment). DO NOT TOUCH.

---

## Part 3 — frontPanelParser audit

**Completed**: `FRONTPANEL_AUDIT.md` written to project root.

**Key findings**:
- The inter-processor serial protocol is entirely eliminated on F765 — all 178 `frontPanel_sendData()` calls become direct function calls
- NRPN encoding, SysEx 7-bit packing, LED query/response round-trip, SAMPLE_CC, ATOMIC_BLOCK, PROGMEM all eliminated
- Missing from F765 ledHandler: `led_setActive_step()`, `led_clearSequencerLeds()`, `led_clearSelectLeds()`, `led_initPerformanceLeds()`, `led_setActiveVoice()`
- Missing from F765 buttonHandler: `buttonHandler_getMode()`, `buttonHandler_getShift()`, `buttonHandler_setRunStopState()`
- Missing from Parameters.h: ~20 entries (added in Part 4)
- `paramToModTarget[]` + `paramToModTargetInit()` not yet ported — needed for LFO/velocity modulation target UI
- Preset system not started

**Priority order for porting**: Parameters.h → ledHandler additions → buttonHandler mode machine → menu shell → sequencer → DSP → presets → automation

---

## Part 4 — Menu stub

**Completed**:
- `Menu/menu.h`: full header with PageNames enum, Page struct (4 knobs), LabelNames, full public API
- `Menu/menu.c`: working stub:
  - `editDisplayBuffer[2][17]` / `currentDisplayBuffer[2][16]` diff engine
  - `sendDisplayBuffer()`: only changed characters sent to LCD (non-blocking via TIM7 queue)
  - `menu_repaintGeneric()`: row 1 = 4-char labels, row 2 = 3-digit values, sub-page indicator
  - `menu_parseEncoder()`: navigation (sub-page) and value editing (encoder push toggles edit mode)
  - `menu_parseKnobDelta()`: quad encoder delta → parameter adjustment
  - `menu_switchPage()`: page switching with MODE LED feedback
  - Stub page table: Voice1-3 (OSC/EG/Filter/Pan) and global BPM page with real parameters
  - Stub pages (Performance, Seq, Euklid etc.) show "page N/A"
  - `menu_dirty` flag: set by input handlers, consumed by rate-limited repaint in main loop
- `Parameters.h`: added ~20 missing entries (PAR_VELOA/D 1-6, PAR_VOL_SLOPE 1-6, PAR_DRIVE 1-3, PAR_FILTER_FREQ_1 fixed, PAR_AUTOM_TRACK, PAR_P1/P2_DEST/VAL, global MIDI/trigger params)
- `Makefile`: added `-IMenu` and `Menu/menu.c`
- `main.c`:
  - MODE buttons → `menu_switchPage()`
  - Main encoder → `menu_parseEncoder()` via `encode_read4()`
  - Quad encoders → `menu_parseKnobDelta()` with snapshot logic
  - Rate-limited repaint: `menu_repaint()` called at most once per 50ms when `menu_dirty=1`
  - All test display scaffolding removed
  - `menu_init()` called after `lcd_tim7_init()` in boot sequence

**Verified on hardware**: Display shows correctly. MODE buttons work. RV1-4 increment/decrement parameters correctly. LCD non-blocking confirmed.

---

## Part 5 — Main encoder (SW42) direction fix

**Hardware identified**: SW42 is a 20-detent digital Gray code quadrature encoder with push switch. Approximately equivalent to Bourns PEC12R series. PE13=A, PE14=B, PE15=SW (all pull-up, active LOW for switch).

**Direction fixed**: Swapped A/B pin extraction in `encoder_tick()` — PE13 used as A input, PE14 as B input in the `(a<<1)|b` Gray code state. This gives CW = positive delta.

**Encoder algorithm**: GRAY table lookup. `last_ab` only updates when `d != 0` (valid transition), preventing noise glitches from corrupting the decode baseline.

**Read function**: `encode_read4()` — 1 count per detent (4 Gray code transitions per detent, divided by 4). Remainder preserved between calls.

**LCD flooding fix**: `menu_parseEncoder()` sets `menu_dirty=1` instead of calling `menu_repaint()` directly. Repaint is rate-limited to 50ms in main loop. This prevents lcd_enqueue() stalling the main loop during fast encoder turns, which was causing enc_delta to accumulate large bursts.

**Verified on hardware**: Direction correct. Response confirmed improved over previous versions.

**Current tarball**: `lxr02_dannegger_enc.tar.gz`

---

## CHANGES THIS SESSION

- `Core/Hardware/lcd.c`: rewritten with 64-entry SPSC queue, non-blocking post-init
- `Core/Hardware/lcd.h`: updated API, added `lcd_tim7_init()` and `lcd_tim7_tick()`
- `Core/Hardware/timebase.c`: added `TIM7_IRQHandler`, fixed duplicate triggerJacks ISR call
- `Core/Hardware/timebase.h`: updated comments
- `Core/Src/startup_stm32f765xx.s`: exposed IRQ55 as `TIM7_IRQHandler`
- `Core/IO/quadEnc.c`: complete rewrite with atan2-based tracking
- `Core/IO/quadEnc.h`: updated API, added `QUAD_TOUCH_THRESHOLD`
- `Menu/menu.h`: new file, full header
- `Menu/menu.c`: new file, working stub
- `Parameters.h`: ~20 missing entries added
- `Makefile`: added `-IMenu`, `Menu/menu.c`
- `main.c`: TIM7 init, MODE/encoder/knob wiring, rate-limited repaint, snapshot logic
- `triggerJacks.c`: GPIOD display gated to 2s on change only

## KNOWN ISSUES INTRODUCED

- Main encoder (SW42) response not yet perfect — direction correct, counting improved, but may still miss occasional detents. Further tuning may be needed.
- Voice5-7 MODE LED feedback incomplete in `menu_switchPage()`

## KNOWN ISSUES RESOLVED

- LCD deadlock risk from Session 1 — LCD is now fully non-blocking (TIM7-driven queue)
- Duplicate `triggerJacks_isrTick()` call in TIM6 ISR removed

## NEXT SESSION RECOMMENDED GOAL

Not explicitly stated. Outstanding priorities per Known Issues: encoder response tuning, buttonHandler/ledHandler additions, SPI1/SD coordination, preset system start.

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
- `menu_dirty` flag must be set (not `menu_repaint()` called directly) from any input handler to avoid LCD queue stalling
