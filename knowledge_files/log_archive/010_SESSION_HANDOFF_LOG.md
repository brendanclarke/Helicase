# Session 010 Handoff Log — LXR-02 Firmware Port

## How to start the next session

**Project**: LXR-02 firmware port (STM32F765VIH6)
**Session goal**: Implement non-blocking SD card state machine on low-priority timer ISR (Session N+1 of architecture plan established in Session 10)
**Last session summary**: See END OF SESSION block below.
**Current tarball**: `lxr02-037_port.tar.gz` — no code was written this session, tarball is unchanged from Session 9.
**Constraints**: Do not write any SD or FatFS code that blocks the main loop or any ISR that outranks the SD timer ISR. DSP render must never be starved. 1ms blocking is not acceptable under any circumstances.

Key files to be aware of:
- Original LXR source is at `/tmp/LXR-master/` on the server (AVR: `front/LxrAvr/`, STM32F4: `mainboard/LxrStm32/src/`)
- Current port lives in the working tarball, extracted to `/home/claude/lxr02/`
- Knowledge files: HARDWARE_MAP.md, AVR_TO_F765_MIGRATION.md, ENHANCED_FEATURES.md

---

## End of session block

```
DATE: 2026-05-05
SESSION GOAL: Diagnose DSP underruns and audio corruption after SD kit load.
              Establish architecture to prevent SD operations from ever blocking
              the DSP render path.
COMPLETED: Full architectural analysis and plan. No code written — pure
           fact-finding and design session. Plan agreed for next 3 sessions.
VERIFIED ON HARDWARE: No — no code changes this session.

CHANGES THIS SESSION:
- No files changed. Tarball remains lxr02-037_port.tar.gz from Session 9.

KNOWN ISSUES INTRODUCED: None.
KNOWN ISSUES RESOLVED: Root cause of post-kit-load underruns identified
  (blocking SD operations in main loop starving DSP render). Not yet fixed —
  fix is the subject of the next session.

NEXT SESSION RECOMMENDED GOAL: Implement non-blocking SD card state machine
  on a new low-priority timer ISR. See detailed plan below.
BLOCKERS:
  - Timer choice for SD ISR: TIM5 recommended (leave TIM2 for CLK IN BPM
    and MIDI RX timestamping per existing known issue #2). Confirm TIM5 is
    free before writing any init code.
  - SD ISR tick frequency decision: 10kHz gives ~9KB/s SD throughput
    (260-byte kit loads in ~29ms wall time). Confirm this is acceptable
    user-facing latency, or choose higher frequency with understood overhead.
  - buttonHandler_processEvents() calls voiceControl_noteOn/Off() directly
    from BAR1/BAR2 press handlers — this touches DSP voice state from the
    main loop while DSP render also runs from the main loop. This is a
    latent race condition to address in Session N+2.

CRITICAL REMINDERS FOR NEXT SESSION:
- 1ms blocking anywhere in the main loop or in any ISR at priority <= 4
  is completely unacceptable. There are no exceptions to this rule.
- f_open can block for 1-50ms. It MUST run inside the SD timer ISR,
  never in the main loop or any higher-priority ISR.
- The SD bit-bang SPI (PC12/PD2/PC8/PD0) is NOT re-entrant. It must
  only ever be called from one context — the SD timer ISR. Never call
  SPI_transmit() from the main loop or any other ISR after the SD ISR
  is active.
- SD card busy-wait polling (post-write card-ready loop in sd_routines.c)
  must become an explicit FSM state that returns each tick and re-polls
  next tick. It cannot be a while() loop inside the ISR.
- The internal STM32F765 DAC (PA4/PA5) must never be enabled. PA4 and
  PA5 are slider inputs (ADC1_IN4/IN5 for RV5/RV6). Accidental DAC
  peripheral clock enable or pin mode change would silently corrupt
  slider readings. Add a comment to timebase.c and AudioCodecManager.c
  making this explicit. The TIM6_DAC_IRQHandler name is a vector table
  artifact from TIM6/DAC sharing IRQ54 — it does not mean the DAC is
  or should be in use.
- EXTI_IMR = 0 must remain the very first operation in main().
- Do NOT add pull-down to PD4 or PD5.
- GetRngValue() calls must mask: uint16_t rnd = (int16_t)(GetRngValue() & 0x7FFF).
```

---

## Session 10 — Full Design Notes

### Problem Statement

Symptoms observed on hardware:
1. No hi-hat voice at startup (separate DSP issue, not addressed this session)
2. After any kit load from SD card, underruns begin immediately and never stop
3. Transient underrun burst when adjusting some controls
4. Screen glitching correlated with control adjustments during underrun state

Root cause of symptoms 2-4: `preset_loadDrumset()` and `preset_saveGlobals()` are called synchronously from the main loop (via `buttonHandler_processEvents()` → `processPress()` → menu load/save dispatch). These call `f_open`, `f_read`, `f_write`, `f_close` which are blocking FatFS operations. `f_open` alone can block for 1–50ms. The audio render budget is `OUTPUT_DMA_SIZE=96` samples at 44108Hz = **2.18ms**. Any blocking operation longer than 2.18ms causes underruns.

The byte-by-byte write loop in `preset_writeDrumsetData()` makes this worse — 250+ individual `f_write` calls per save, each going through FatFS bookkeeping.

`preset_morph()` called at end of every `preset_loadDrumset()` loops 256 times calling `frontPanel_sendData()` — currently a stub so cost is negligible, but this loop will become a problem when `frontPanel_sendData` is wired.

The permanent underrun after load (not just during load) is a separate DSP issue — a loaded parameter value may be corrupting voice state. This was not investigated this session as the blocking SD issue takes priority.

### Why Hardware SPI Is Not Viable

Investigated whether SD card pins could be remapped to a hardware SPI peripheral for DMA-based non-blocking transfers:

- SPI1: PA7/PA6/PB3 — taken (LEDs/buttons)
- SPI2: PB12/PB13/PB15 — taken (I2S2/DAC2)
- SPI3: PB5/PC10 — taken (I2S3/DAC1)
- SPI4: PE11/PE12/PE13/PE14 — taken (LCD E/RS, encoder A/B)
- SPI5: PF6–PF9 — PF not bonded in TFBGA100 package
- SPI6: PG12/PG13/PG14 — PG not bonded in TFBGA100 package

SD card pins PC12/PD2/PC8/PD0 have no SPI alternate function mapping to any free peripheral. Hardware SPI is not possible. Bit-bang on these pins is the only option and is fixed in PCB copper (bootloader also uses it).

### Why "Chunking at FatFS Call Granularity in the Main Loop" Is Not Sufficient

Proposed and rejected: a state machine where each FatFS call (f_open, f_read, f_close) is a separate state, advanced one step per main loop iteration.

Rejection reason: `f_open` is one FatFS call and it blocks for 1–50ms. "One iteration" of the main loop that runs `f_open` blocks for the entire duration of `f_open`. During that time the audio render check is not reached. This is identical to the current problem — it only reduces the number of blocking calls from many to one, but the one remaining blocking call is still long enough to cause sustained underruns.

**There is no way to make `f_open` yield mid-execution without either modifying FatFS internals or running it in a separate execution context.** The separate execution context is the low-priority timer ISR.

### Why DSP Must Not Move to the DMA ISR

Also proposed and rejected: move `mixer_calcNextSampleBlock()` into the DMA Stream 4 ISR (priority 4) so the main loop can block freely on SD.

Rejection reason: the DMA ISR fires every 2.18ms on a fixed hardware clock. If DSP render ever takes longer than 2.18ms — which it will as voices, reverb, and other DSP features are added — the ISR re-pends immediately on exit and the system enters a state where the ISR never returns to the main loop. Everything below priority 4 dies. There is no graceful degradation. This ceiling is hard, invisible, and fatal.

The correct architecture keeps DSP in the main loop where it can expand freely and where underruns are the graceful degradation signal, and isolates SD I/O in a separate lower-priority ISR context.

### Agreed Architecture — Three Sessions

**Target ISR priority table:**

| IRQ | Priority | Handler | Notes |
|-----|----------|---------|-------|
| TIM1_CC (IRQ27) | 1 | Encoder IC | Latency-sensitive edge capture |
| TIM6_DAC (IRQ54) | 2 | Hardware sampler | SPI exchange, encoder debounce, endlessPots, trigger jacks |
| TIM7 (IRQ55) | 3 | LCD drain | Must preempt audio to avoid queue stall |
| DMA1_S4 (IRQ15) | 4 | Audio pack master | Core audio guarantee |
| DMA1_S7 (IRQ47) | 4 | Audio pack slave | |
| OTG_FS (IRQ67) | 5 | USB MIDI | |
| TIM_SD (new) | 6 | SD byte pump + FatFS FSM | Lowest real-time requirement |
| EXTI9_5 (IRQ23) | 15 | CLK/RST jacks | |

Note: current priorities have TIM1 and TIM6 both at 1, TIM7 at 2, audio at 4. The revised table bumps TIM6 to 2, TIM7 to 3, and adds SD ISR at 6. All priority changes are single-constant edits to `NVIC_IPR[]` writes in each peripheral's init function.

**Session N+1 — Non-blocking SD ISR:**

New timer (TIM5 — leave TIM2 for CLK IN BPM and MIDI RX per known issue #2) at frequency TBD (10kHz suggested), priority 6.

The SD ISR implements a byte-level pump and FatFS FSM:

- `SPI_transmit()` becomes a one-byte-per-tick operation. The ISR clocks exactly one byte of bit-bang SPI and returns.
- SD card busy-wait polling (post-write card-ready `while (SPI_receive() != 0xFF)` loops in `sd_routines.c`) become explicit FSM states that return each tick and re-poll next tick. These cannot remain as `while()` loops.
- FatFS call sequencing is managed as FSM states: IDLE → OPEN → TRANSFER → CLOSE → DONE (with ERROR state).
- `diskio.c` disk_read/disk_write replaced with non-blocking equivalents that interact with the byte pump.
- `preset_loadDrumset()`, `preset_saveDrumset()`, `preset_loadGlobals()`, `preset_saveGlobals()` become request-posting functions only — they set a pending operation in a shared struct and return immediately. The SD ISR FSM sees the pending request on its next tick and begins processing.
- Completion signalled via a status flag that menu.c can poll.
- Byte-by-byte write loops in `preset_writeDrumsetData()` and `preset_writeGlobalData()` replaced with single bulk `f_write()` calls — the byte pump handles the actual byte-level chunking transparently.
- The main loop calls nothing SD-related directly, ever.

**Session N+2 — Non-blocking hardware sampler on TIM6:**

TIM6 ISR already does the right things (SPI exchange, encoder_tick, endlessPots_tick, triggerJacks_isrTick, tick counters). What changes:

- `encode_read4()` and `endlessPots_getDelta()` move from main loop into TIM6 ISR. Results posted to shared flags/variables that main loop reads passively. Both functions already do internal `cpsid i`/`cpsie i` — when called from TIM6 ISR these become nested interrupt disables, harmless on Cortex-M7 but should be replaced with direct flag reads since TIM6 ISR is already an exclusive context.
- `adc_checkPots()` and `led_tickHandler()` assessed for ISR safety and moved if clean.
- `buttonHandler_processEvents()` stays in main loop — it calls `menu_switchPage()`, `menu_repaintAll()`, `voiceControl_noteOn/Off()`. These are not ISR-safe without further audit. The event ring (ISR writes, main loop reads) already correctly separates ISR-safe recording from main-loop action dispatch.
- LATENT BUG to address this session: `buttonHandler_processEvents()` → `processPress()` → BAR1/BAR2 handlers call `voiceControl_noteOn/Off()` directly. This touches DSP voice state from the main loop at the same time the main loop DSP render reads it. No protection exists. Needs either: (a) a flag that defers voice trigger to start of next render, or (b) confirmation that `voiceControl_noteOn/Off()` only sets flags that the render loop reads atomically.

**Session N+3 — NVIC priorities and cleanup refactor:**

- Apply revised priority table above.
- Add DAC non-use comment to `timebase.c` and `AudioCodecManager.c`.
- Strip main loop to DSP render + passive queue consumption only.
- Update all NVIC_IPR writes. Each is a single-constant change in its respective init function.
- Verify `f_mount` in `preset_init()` — currently called once at boot. With async SD FSM this may need to move into the FSM init or be called from the SD ISR on first operation.

### File Assessments From This Session

Files read and assessed (not modified):

**`main.c`**: Main loop currently calls encode_read4, endlessPots_getDelta, menu_parseEncoder, menu_parseKnobDelta, menu_serviceKnobRepaint, adc_checkPots, led_tickHandler, buttonHandler_processEvents, buttonHandler_tick in addition to DSP render. All UI calls must eventually be either moved to TIM6 or become passive queue reads. Boot-time `preset_loadDrumset(0,0)` and `preset_loadGlobals()` are synchronous — acceptable at boot since audio is not running yet.

**`presetManager.c`**: `preset_loadDrumset()` calls f_open + f_read + f_close + preset_sendDrumsetParameters() + menu_repaintAll() in one blocking call stack. `preset_writeDrumsetData()` writes parameter_values[] one byte at a time in a loop — 250+ individual f_write calls. Both must be restructured for the async FSM. `preset_morph()` loops 256× calling `frontPanel_sendData()` — currently a stub, negligible cost, but document as future concern.

**`spi_sd.c`**: Fast-mode `SPI_transmit()` is a tight 8-iteration bit-bang loop — ~120 cycles/byte at 216MHz ≈ 0.55µs/byte. Slow-mode adds a 100-iteration NOP delay per half-bit — only used during card init, not during normal operation. The fast-mode loop is the target for the byte pump: each iteration becomes one ISR tick.

**`AudioCodecManager.c`**: OUTPUT_DMA_SIZE=96 confirmed from config.h. Render budget = 96/44108Hz = 2.18ms = 471,288 cycles at 216MHz. SPSC queue and ISR structure confirmed correct and untouched. `TIM6_DAC_IRQHandler` name noted — DAC peripheral is NOT in use. PA4/PA5 are ADC slider inputs, not DAC outputs. Internal DAC must never be enabled.

**`config.h`**: OUTPUT_DMA_SIZE=96 confirmed. TIM2 not listed as used — reserved for CLK IN BPM and MIDI RX (known issue #2). TIM5 not used — available for SD ISR.

**`timebase.c`**: TIM6 ISR body confirmed. Currently contains: dout_latch(), din_dout_exchange(), triggerJacks_isrTick(), encoder_tick(), endlessPots_tick(), tick counter increments. Clean and bounded. Priority currently 1 (same as TIM1) — will shift to 2 in Session N+3. TIM7 ISR delegates entirely to lcd_tim7_tick() — unchanged.

**`buttonHandler.c`**: ISR/main-loop split already correct by design. buttonHandler_buttonPressed/Released write to event ring only (ISR-safe). buttonHandler_processEvents() drains ring and calls menu/LED/DSP functions (main-loop only). The `while→if` in processEvents is intentional — documented in Session 5, do not revert. BAR1/BAR2 handlers call voiceControl_noteOn/Off() — latent race condition noted above.

**`encoder.c`**: encode_read4() does cpsid/cpsie internally to atomically read and clear enc_delta. Called from main loop currently. Moving to TIM6 ISR: the cpsid/cpsie becomes unnecessary (ISR is already exclusive) and should be removed to avoid nested disable. enc_delta is written by TIM1_CC_IRQHandler (priority 1) and read by encode_read4. If encode_read4 moves to TIM6 (priority 2), TIM1 can preempt TIM6 and corrupt enc_delta mid-read — the disable must stay or be replaced with a shadow copy approach.

**`endlessPots.c`**: endlessPots_getDelta() does cpsid/cpsie to atomically read and clear delta. Same consideration as encoder. The hot-path ISR body (endlessPots_tick) is already in TIM6 and confirmed at ~0.56µs for 4 encoders. Non-volatile ISR-only fields confirmed correct — do not add volatile back.

**`HARDWARE_MAP.md`**: SPI4/5/6 pin availability confirmed unavailable (SPI4 pins taken by LCD/encoder, SPI5/6 on unbonded TFBGA100 ports). Hardware SPI for SD confirmed impossible. TIM5 confirmed free (not listed in IRQ table, no known use).
