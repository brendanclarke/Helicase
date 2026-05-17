# Session 019 Handoff Log

```
DATE: 2026-05-14
SESSION GOAL: Audit and correct MIDI send/receive signals, including clock and realtime
              messages over both MIDI and trigger jack signals.  Implement all missing
              MIDI features from the audit plan (Phases 1–7).
COMPLETED: Phases 1–7 of AUDIT-CLOCK-MIDI.md implemented and built.
           BAR1/BAR2 → MIDI note path wired.
           CC1 mod wheel → MORPH wired and double-fire bug fixed.
           TIM3 sequencer timing owner established.
           TIM2 free-running 1 MHz timestamp counter initialised.
           Real trigger-jack backend (PC13/PD4/PD5) replaces PD3 diagnostic.
           OUTPUT_DMA_SIZE corrected to 32 (was erroneously 16 from Session 017).
VERIFIED ON HARDWARE: Build succeeds (make && make img → 279780-byte image).
                      Functional MIDI, clock sync, and jack timing have NOT yet been
                      bench-verified against hardware; they are bring-up implementations.

CHANGES THIS SESSION:
- Core/Src/startup_stm32f765xx.s: IRQ10 → EXTI4_IRQHandler (CLK IN), IRQ23 → EXTI9_5_IRQHandler (RST IN), IRQ29 → TIM3_IRQHandler (sequencer), IRQ39 → USART3_IRQHandler (MIDI DIN)
- Core/MIDI/Uart.c: Full interrupt-driven RX/TX.  Dual TX FIFO (realtime priority + normal channel).  USART3_IRQHandler timestamps every RX byte with TIM2, routes 0xF8–0xFF to MidiRealtime ring, channel bytes to fifo_midiRx.  TX drains realtime FIFO before normal FIFO.  uart_sendMidiByte() non-blocking.
- Core/MIDI/Uart.h: Updated declarations for new FIFO and drop-count accessors.
- Core/MIDI/MidiRealtime.c: NEW — 32-entry timestamped SPSC ring for MIDI_CLOCK, MIDI_START, MIDI_CONTINUE, MIDI_STOP.  midiRealtime_push() called from USART3_IRQHandler and USB MIDI callback; midiRealtime_pop() called from TIM3 context only.
- Core/MIDI/MidiRealtime.h: NEW — struct MidiRealtimeEvent (status, source, timestampUs), ring accessors.
- Core/MIDI/MidiParser.c: midiParser_getVoiceMidiNote() — returns override note if set, else LXR defaults 36..42.  midiParser_voiceMatchesNote() — channel + assigned note matching.  midiParser_playVoiceMidiNote() — BAR1/BAR2 trigger path.  midiParser_processRealtimeEvents() — drains MidiRealtime ring (32-event budget) in TIM3 context.  midiParser_handleRealtimeEvent() — routes to clockSync / midiParser_realtimeSourceAllowed(), TX filter, USB+DIN TX routing.  midiParser_realtimeSourceAllowed() — AUTO priority logic: jack > DIN > USB, 500 ms DIN hold window.  CC1 on global channel now calls midiParser_setMorphFromModWheel() in incoming channel-MIDI path (not inside midiParser_ccHandler to avoid double-fire from internal CC dispatch).  PAR_BPM no longer uses value 0 as external sync switch.
- Core/MIDI/MidiParser.h: New declarations for above functions.
- Core/MIDI/MidiVoiceControl.c: 32-entry VoiceTriggerEvent pending ring.  voiceControl_noteOn/Off() enqueue via voiceControl_enqueueTriggerLocked() with IRQ-off critical sections.  voiceControl_processPending() drained at every OUTPUT_DMA_SIZE audio boundary before mixer_calcNextSampleBlock().  active_voices bitmap protected with cpsid/cpsie.  seq_sendRealtime(), seq_sendMidiNoteOn(), and program-change send use local MidiMsg structs (not static) to avoid TIM3/foreground reentrancy.  DIN TX FIFO insertion and USB MIDI message writes protected with short critical sections.
- Core/MIDI/MidiVoiceControl.h: Declarations for pending ring and processPending.
- Core/Hardware/frontPanel/buttonHandler.c: BAR1 → midiParser_playVoiceMidiNote(voice, 127); BAR2 → midiParser_playVoiceMidiNote(voice, 64).  Both record through the MIDI-note path.
- Core/Hardware/triggerJacks.c: Full replacement of PD3 diagnostic.  PC13 CLK OUT, PD4 CLK IN (EXTI4 falling), PD5 RST IN (EXTI9_5 both edges).  16-entry trigger event ring.  EXTI handlers only clear pending bit, capture TIM2 timestamp, and enqueue.  triggerJacks_tick() drains ring in TIM3 context; dispatches pulse BPM (seq_triggerNextMasterStep), run/reset gate (active-low semantics matching reference LXR run-gate behaviour).  trigger_prescalerClockInput defined; SEQ_TRIGGER_IN_PPQ and SEQ_TRIGGER_OUT1_PPQ wired.  CLK OUT PC13 driven from sequencer/trigger path.
- Core/Hardware/triggerJacks.h: Declarations for init, tick, event types, and jack activity accessors.
- Core/Sequencer/sequencerTimer.c: NEW — TIM3 4kHz sequencer timing owner.  PSC=107 → 1 MHz base, ARR=249 → 4 kHz.  TIM3_IRQHandler: (1) midiParser_processRealtimeEvents(), (2) triggerJacks_tick(), (3) seq_tick().  Startup priority 2 in NVIC.
- Core/Sequencer/sequencerTimer.h: NEW — sequencerTimer_init() declaration.
- Core/Sequencer/clockSync.c: sync_tickTimestamp(uint32_t us) — accepts TIM2 microsecond deltas, 8-sample sliding BPM window, calls seq_setBpm().  sync_midiStartStop() for start/continue/stop.  PAR_EXT_SYNC source enum (off/usb/din/pls/aut).  AUTO mode: jack suppresses MIDI; DIN suppresses USB with 500 ms hold window; falls back to internal when no source active.
- Core/Sequencer/clockSync.h: Updated for PAR_EXT_SYNC, source enum, sync_tickTimestamp.
- Core/Hardware/timebase.c: TIM2 initialised as free-running 32-bit counter.  PSC=107 → 1 MHz (1 µs ticks).  timebase_tim2Now() returns TIM2->CNT without resetting.
- Core/Hardware/timebase.h: timebase_tim2Now() declaration.
- Core/Menu/menu.c: PAR_EXT_SYNC Global menu item added immediately after PAR_BPM.  Short name: SNC, long name: SyncInpt.  Values: off / usb / din / pls / aut.
- Core/MIDI/frontPanelParser.c: SEQ_TRIGGER_IN_PPQ wired to trigger_prescalerClockInput.  SEQ_TRIGGER_OUT1_PPQ wired to CLK OUT divider.  SEQ_TRIGGER_OUT2_PPQ and SEQ_TRIGGER_GATE_MODE remain compatibility no-ops.  PAR_EXT_SYNC dispatched to clockSync.
- main.c: seq_tick() removed from main loop (TIM3 owns it).  midiParser_processRealtimeEvents() removed from main loop.  triggerJacks_tick() removed from main loop.  midi_service() added: uart_processMidi() (16-byte budget) + usb_getMidi() (8-message budget) + usb_tick() flush.  voiceControl_processPending() called inside audio_check_and_render() at every OUTPUT_DMA_SIZE boundary.  sequencerTimer_init() called after audioCodec_init().
- config.h: OUTPUT_DMA_SIZE corrected to 32 (was 16, which was advancing EG/LFO at 2× reference rate; confirmed against AudioCodecManager.h include order and LXR-master block size).
- AUDIT-CLOCK-MIDI.md: Phase 4 and Phase 5 implementation status sections appended; follow-up fixes section appended.

KNOWN ISSUES INTRODUCED:
- Full MIDI functional parity (note/CC/PC/routing/filter on DIN and USB), MIDI clock send/receive, CLK IN/OUT timing, and RST IN semantics are all BRING-UP IMPLEMENTATIONS.  Hardware bench testing required before claiming correctness.
- Phase 6 (hardware-timed compare/one-shot scheduler) is not implemented.  TIM3 runs at a fixed 4kHz quantum, not a dynamic compare register.  Jitter under heavy load is unmeasured.
- Long samples still not solved: oscillator phase>>17 indexing path unchanged.

KNOWN ISSUES RESOLVED:
- USART3 RX not configured — RESOLVED.
- USB MIDI RX not consumed from main loop — RESOLVED.
- USB MIDI TX not flushed from main loop — RESOLVED.
- DIN MIDI TX blocking (polled) — RESOLVED; TX is now FIFO + TXE interrupt, non-blocking.
- BAR1/BAR2 race condition calling voiceControl_noteOn/Off directly from processPress() — RESOLVED; all triggers now deferred through voiceControl pending ring and drained at audio boundary.
- BAR1/BAR2 triggers not recording through MIDI note path — RESOLVED; midiParser_playVoiceMidiNote() used, with assigned/default note lookup.
- PD3 EXTI3 diagnostic code active in production triggerJacks.c — RESOLVED; replaced with PC13/PD4/PD5 backend.
- EXTI4 and EXTI9_5 vectors pointing at Default_Handler — RESOLVED.
- seq_tick() in main loop contributing jitter — RESOLVED; TIM3 4kHz ISR now owns it.
- MIDI realtime not separated from channel message parsing in ISR — RESOLVED; USART3_IRQHandler routes realtime bytes to timestamped ring without disturbing running-status parser.
- PAR_BPM = 0 as external sync toggle — RESOLVED; PAR_EXT_SYNC is now the dedicated global parameter.
- CC1 mod wheel causing MORPH stuck at ~127 (double-fire via midiParser_ccHandler) — RESOLVED; CC1 handling moved to incoming channel-MIDI path only.
- OUTPUT_DMA_SIZE = 16 (2× reference rate for EG/LFO) — RESOLVED; corrected to 32.

NEXT SESSION RECOMMENDED GOAL:
Hardware bench testing of:
1. DIN MIDI note in/out, USB MIDI note in/out, channel routing, CC, program change.
2. CC1 mod wheel MORPH range (0..127 → MORPH 0..254).
3. MIDI clock RX sync: start/stop/continue, BPM tracking, jitter under load.
4. CLK IN falling edge BPM estimation and sequencer sync.
5. CLK OUT PC13 at correct PPQ.
6. RST IN run/reset gate semantics.
7. BAR1/BAR2 note recording through MIDI path.
If any of the above expose sequencer jitter issues, then move to Phase 6 (TIM3 compare-register one-shot scheduler).

BLOCKERS:
- All Phase 1–7 features are bring-up only; hardware bench required.
- Phase 6 (jitter-free sequencer scheduling) depends on bench jitter measurements.
- No hi-hat at startup is still an open DSP init ordering issue (pre-existing).

CRITICAL REMINDERS FOR NEXT SESSION:
- EXTI_IMR = 0 must remain as the very first operation in main(), before sysclk_init().
- Do NOT add pull-down to PD4 or PD5 (CLK IN / RST IN).
- TIM2 is a SHARED free-running timestamp counter.  Do NOT reset it on each clock pulse; use unsigned subtraction for deltas.  Do NOT use TIM2 for SD ISR (TIM5 is reserved for that).
- TIM5 is still free and reserved for future afatfs_poll() ISR migration.
- OUTPUT_DMA_SIZE = 32 is now correct.  Do not revert to 16.
- PA4/PA5 are slider ADC inputs.  Internal DAC must never be enabled.
- seq_tick() is owned by TIM3_IRQHandler.  Do NOT add seq_tick() back to the main loop.
- triggerJacks_tick() and midiParser_processRealtimeEvents() are owned by TIM3.  Do NOT call them from the main loop.
- voiceControl_processPending() is called inside audio_check_and_render() only.  Do NOT call it from ISR context.
- DIN TX FIFO and USB MIDI writes from foreground code must use the short critical-section wrappers to avoid corruption from TIM3 clock/note output.
- BAR1/BAR2 now call midiParser_playVoiceMidiNote() — do NOT revert to direct voiceControl_noteOn/Off().
- CC1 on the global MIDI channel controls MORPH (value << 1); the mapping is intentional.
- PAR_BPM minimum is now 1 (not 0).  Value 0 no longer means "external sync".  Use PAR_EXT_SYNC.
- SyncInpt menu values in order: off / usb / din / pls / aut.
- RST IN semantics: active-low gate matches reference LXR run/reset; low = stop+reset, release = start.
- Sample flash erase floor is sector 6 (0x08080000).  sampleFlash.c must reject anything below that.
- GetRngValue() must always be masked: & 0x7FFF.
```

---

## Detailed Change Notes

### Architecture After Session 019

The main architectural shift is the introduction of the TIM3 sequencer timing owner and the separation of ISR edge capture from musical state advancement.

**Before:**
```
main loop:
  seq_tick()                       ← jittered by LCD, SD, USB, MIDI
  midiParser_processRealtimeEvents()
  triggerJacks_tick()
  voiceControl_noteOn() called directly from BAR1/BAR2 and MIDI RX
  DIN TX polled (blocking)
```

**After:**
```
TIM3_IRQHandler (4 kHz, priority 2):
  midiParser_processRealtimeEvents()
  triggerJacks_tick()
  seq_tick()

USART3_IRQHandler (priority 5):
  timestamp byte with TIM2
  if realtime: push to midiRealtime_ring
  else: push to fifo_midiRx

EXTI4_IRQHandler (priority 3):
  timestamp with TIM2, push TRIGGER_EVENT_CLOCK

EXTI9_5_IRQHandler (priority 3):
  timestamp with TIM2, read PD5 level, push TRIGGER_EVENT_RESET

main loop:
  audio_check_and_render():
    voiceControl_processPending()  ← safe audio boundary
    mixer_calcNextSampleBlock()
  midi_service():
    uart_processMidi()             ← 16-byte budget
    usb_getMidi() loop             ← 8-message budget
    usb_tick()
  seq_ledState_process()
  UI work (menus, buttons, LCD, SD)
```

---

### Phase 1 — Safe MIDI Plumbing

**Files changed:** `Uart.c`, `Uart.h`, `MidiParser.c`, `MidiParser.h`, `MidiVoiceControl.c`, `MidiVoiceControl.h`, `buttonHandler.c`, `startup_stm32f765xx.s`, `main.c`

- PB11 configured as USART3_RX AF7.
- USART3 RE|TE|UE enabled, RXNE interrupt enabled, NVIC priority 5.
- `uart_processMidi()` now passes up to 16 bytes per call to `midiParser_parseUartData()`.
- `main.c` calls `midi_service()` each main-loop pass: `uart_processMidi()` + USB MIDI drain + `usb_tick()`.
- BAR1/BAR2 now call `midiParser_playVoiceMidiNote(voice, velocity)` instead of `voiceControl_noteOn/Off()` directly.
- Voice triggers from BAR1/BAR2, MIDI note-on, and sequencer all enter through `voiceControl_noteOn()` → pending ring → audio boundary drain.
- Startup vector IRQ39 now points at `USART3_IRQHandler` (was `Default_Handler`).

### Phase 2 — Non-Blocking and Realtime-Aware MIDI Send

**Files changed:** `Uart.c`, `Uart.h`, `MidiParser.c`

- Normal TX FIFO (`fifo_midiTx`) for channel messages.
- Priority TX FIFO (`fifo_midiRealtimeTx`) for single-byte realtime bytes.
- `USART3_IRQHandler` TX path drains realtime FIFO before normal FIFO.
- `uart_sendMidiByte()` is non-blocking: enqueues and enables TXEIE.
- Drop counters: `uart_midiTxDropCount` and `uart_midiRealtimeTxDropCount`.
- `midiParser_getVoiceMidiNote(voice)`: returns `midi_NoteOverride[voice]` if set, else `MIDI_DEFAULT_VOICE_NOTE_BASE (36) + voice` (Drum1=36 … Drum7=42).
- `midiParser_voiceMatchesNote(voice, channel, note)`: matches incoming note using channel + assigned/default note, enabling stacked voices on shared channel/note.
- BAR1 uses velocity 127; BAR2 uses velocity 64.

### Phase 3 — TIM2 Timestamp Base

**Files changed:** `timebase.c`, `timebase.h`

- TIM2 initialised as free-running 32-bit counter.  PSC=107 → 1 MHz base (1 µs ticks).  Overflow at ~71 minutes.
- `timebase_tim2Now()` returns `TIM2->CNT` without resetting.
- All delta calculations use unsigned subtraction to survive wrap.
- TIM2 is intentionally shared: USART3 ISR, EXTI4/EXTI9_5 ISRs, and triggerJacks all read it without owning/resetting it.

### Phase 4 — MIDI Realtime RX Fast Path

**Files changed/added:** `Uart.c`, `MidiRealtime.c` (new), `MidiRealtime.h` (new), `MidiParser.c`, `clockSync.c`, `clockSync.h`

- `Core/MIDI/MidiRealtime.c/h`: 32-entry `MidiRealtimeEvent` SPSC ring.  Fields: `status` (0xF8/FA/FB/FC), `source` (DIN=0, USB=1), `timestampUs`.  Drop-oldest on overflow; `midiRealtime_getDropCount()` for diagnostics.
- `USART3_IRQHandler` RX path: read `USART3->RDR`, capture `timebase_tim2Now()`.  If byte ≥ 0xF8 and recognised as MIDI realtime: push to ring via `midiRealtime_push()`, do not disturb channel-message parser state.  Else: push to `fifo_midiRx`.
- USB MIDI OUT callback: similarly timestamps and pushes MIDI_CLOCK/START/CONTINUE/STOP to realtime ring.
- `midiParser_processRealtimeEvents()`: drains ring with 32-event budget.  Called from TIM3 only.
- `midiParser_handleRealtimeEvent()`: checks `midiParser_realtimeSourceAllowed()` (AUTO priority), applies TX routing, then dispatches: MIDI_CLOCK → `sync_tickTimestamp(event.timestampUs)`, MIDI_START/CONTINUE → `sync_midiStartStop(1)`, MIDI_STOP → `sync_midiStartStop(0)`.
- `midiParser_realtimeSourceAllowed()`: jack pulses (via `triggerJacks_clockInputRecently()`) suppress all MIDI sources.  DIN suppresses USB with a 500 ms hold window.  Internal free-run when no external source is active.
- `sync_tickTimestamp(uint32_t us)`: calculates interval from TIM2 delta, updates 8-sample sliding BPM window, calls `seq_setBpm()`, advances sequencer transport (4 internal steps per 3 MIDI clocks = 24→32 PPQN expansion).
- `PAR_BPM` clamped to minimum 1.  New `PAR_EXT_SYNC` global parameter owns the sync source selection.
- Global menu: `SNC` / `SyncInpt`, values `off` / `usb` / `din` / `pls` / `aut`.

### Phase 5 — Real Trigger-Jack Backend

**Files changed:** `triggerJacks.c`, `triggerJacks.h`, `frontPanelParser.c`, `startup_stm32f765xx.s`

- PD3/EXTI3 diagnostic code removed.
- PC13 as CLK OUT: output mode, push-pull, 50 MHz, initially low.
- PD4 as CLK IN: input, no pull, EXTI4 falling edge, SYSCFG EXTICR2 routing.
- PD5 as RST IN: input, no pull, EXTI9_5 both edges.
- Startup vectors: IRQ10 → `EXTI4_IRQHandler`, IRQ23 → `EXTI9_5_IRQHandler`.
- `EXTI4_IRQHandler`: clears EXTI line 4 only; captures TIM2 timestamp; marks jack activity; pushes `TRIGGER_EVENT_CLOCK`.
- `EXTI9_5_IRQHandler`: clears EXTI line 5 only; captures TIM2 timestamp; reads GPIOD IDR bit 5 for level; pushes `TRIGGER_EVENT_RESET`.
- `triggerJacks_tick()` (called from TIM3):
  - CLOCK events → `trigger_handleClockEvent()`: calculates BPM from interval and `trigger_prescalerClockInput`, calls `seq_triggerNextMasterStep()` for master steps.
  - RESET events → `trigger_handleResetEvent()`: active-low gate: asserted → stop and reset sequencer; released → start sequencer.
- `SEQ_TRIGGER_IN_PPQ` wired to `trigger_prescalerClockInput` (maps 1/4/8/16/32 PPQ to prescaler values).
- `SEQ_TRIGGER_OUT1_PPQ` wired to the single real CLK OUT divider on PC13.
- `SEQ_TRIGGER_OUT2_PPQ` and `SEQ_TRIGGER_GATE_MODE` remain compatibility no-ops (LXR-02 has one physical clock output and no individual voice trigger outs).
- RST IN semantics chosen: active-low run/reset gate, matching reference LXR behaviour.

### Phase 6 — TIM3 Sequencer Timing Owner

**Files added/changed:** `sequencerTimer.c` (new), `sequencerTimer.h` (new), `main.c`, `startup_stm32f765xx.s`

- `Core/Sequencer/sequencerTimer.c/h`: TIM3 at 4 kHz (PSC=107, ARR=249).  NVIC priority 2.  IRQ29 → `TIM3_IRQHandler`.
- `TIM3_IRQHandler` owns strict ordering: realtime events → jack events → sequencer.
- `seq_tick()`, `midiParser_processRealtimeEvents()`, and `triggerJacks_tick()` removed from `main.c`.
- `sequencerTimer_init()` called in `main.c` after `audioCodec_init()` and after menu/USB/MIDI initialisation is complete.
- Note: this is a fixed-interval 4 kHz owner, not yet the compare-register one-shot scheduler described in Phase 6 of the audit.  Phase 6 jitter improvement is a future step pending bench measurement.

### Phase 7 — Voice Trigger Race Cleanup

**Files changed:** `MidiVoiceControl.c`, `MidiVoiceControl.h`, `buttonHandler.c`, `MidiParser.c`, `sequencer.c`

- `VoiceTriggerEvent` struct: `voice`, `note`, `vel` (uint8_t each).
- 32-entry SPSC ring `voiceTriggerRing[]` with `voiceTriggerHead`/`voiceTriggerTail`.
- `voiceControl_enqueueTriggerLocked()`: IRQ-off section; drops oldest on overflow to keep newest gesture.
- `voiceControl_noteOn(voice, vel)` and `voiceControl_noteOff(voice)` enqueue events; never write DSP state directly.
- `voiceControl_processPending()`: drains ring; called inside `audio_check_and_render()` before each `mixer_calcNextSampleBlock()` call.
- `active_voices` bitmap protected with `cpsid`/`cpsie` in `noteOn`/`noteOff` and `processPending`.
- `seq_sendRealtime()`, `seq_sendMidiNoteOn()`, program-change send use local `MidiMsg` structs (not static/global temporaries) to avoid TIM3/foreground reentrancy.
- DIN TX FIFO insertion and USB MIDI message writes use short critical sections so TIM3 clock/note output cannot interleave with foreground-routed writes.

### BAR1/BAR2 MIDI Note Recording

Per the session's additional requirement (Session 019, 07:11 UTC):

- BAR1 and BAR2 now trigger the active voice through `midiParser_playVoiceMidiNote(voice, vel)`.
- The function resolves the voice's MIDI channel (`midi_MidiChannels[voice]`) and assigned note (`midiParser_getVoiceMidiNote(voice)`).
- Notes route through `midiParser_handleNoteMessage()` exactly as if they arrived over DIN MIDI.
- Recording flag is set so notes are captured by the active track when the sequencer is armed.
- Users can stack voice triggers by assigning the same channel and note number to multiple voices; all matching voices fire.
- LXR defaults: Drum1=36, Drum2=37, Drum3=38, Drum4=39, Drum5=40, Drum6=41, Drum7=42.

### CC1 Mod Wheel → MORPH

Wired and then corrected during the session (Sessions 019 at 07:38 and 07:44 UTC):

- **First attempt**: CC1 handling added inside `midiParser_ccHandler()`.  This caused MORPH to stay near 127 because `midiParser_ccHandler()` is also called from internal parameter/automation dispatch paths, which kept reasserting a mid-range MORPH value.
- **Corrected approach**: CC1 → MORPH mapping placed in the incoming channel-MIDI path only, after the global-channel check, before `midiParser_ccHandler()`.  The shared CC handler ignores CC1.
- Mapping: `MORPH = CC1_value << 1` — incoming 0..127 maps to MORPH 0..254 (full-range parameter).
- `midiParser_setMorphFromModWheel()` sets `parameter_values[PAR_MORPH]` and calls `preset_morph()`.

### OUTPUT_DMA_SIZE Correction

Discovered during Phase 5/6 follow-up audit of DSP block cadence:

- `AudioCodecManager.h` includes `config.h` before defining `DMA_MODE_ACTIVE`.  The effective block size as seen by the original LXR reference is `OUTPUT_DMA_SIZE = 32`.
- The port had been using `OUTPUT_DMA_SIZE = 16` since Session 017, which ran EG/LFO/control updates at 2× the reference rate.
- Corrected to 32.  Each 96-frame `AUDIO_DMA_FRAMES` hardware DMA half now contains exactly three 32-frame DSP blocks, matching the reference.
- This makes envelope and LFO timing correct.

### Global Menu SyncInpt Item

- Short name: `SNC`
- Long name: `SyncInpt` (encoder-click title: `Global SyncInpt`)
- Values in order: `off` / `usb` / `din` / `pls` / `aut`
- `aut` implements AUTO priority: jack > DIN > USB; falls back to internal when no recent external source.
- `PAR_BPM` minimum is 1 regardless of sync source.

### STEP Mode Substep Toggle

- In STEP mode, pressing a main sequencer step or SELECT/substep button now toggles the step parameter page between `vel/nte/prb` and `dst/val/dst/val` halves.
- VOICE buttons retain existing toggle behaviour.

---

## Key Facts for Future Sessions

| Fact | Detail |
|------|--------|
| TIM3 owns sequencer | 4 kHz, priority 2, IRQ29.  seq_tick() + processRealtimeEvents() + triggerJacks_tick() |
| TIM2 is shared timestamp | PSC=107 → 1 µs ticks, free-running, never reset on pulse.  Do NOT use for SD ISR |
| TIM5 still free | Reserved for future afatfs_poll() ISR migration |
| MIDI realtime ring | MidiRealtime.c, 32 entries, push in USART3/USB ISR, pop in TIM3 |
| Jack event ring | triggerJacks.c, 16 entries, push in EXTI4/EXTI9_5, pop in TIM3 |
| Voice trigger ring | MidiVoiceControl.c, 32 entries, push from any context, pop at audio boundary |
| Startup vectors added | IRQ10 EXTI4, IRQ23 EXTI9_5, IRQ29 TIM3, IRQ39 USART3 |
| OUTPUT_DMA_SIZE | 32 (corrected; was 16) |
| PAR_EXT_SYNC | New global: off/usb/din/pls/aut.  PAR_BPM min is now 1, not 0 |
| CC1 on global channel | MORPH = CC1_value << 1; handled in incoming path, not in ccHandler |
| BAR1/BAR2 | midiParser_playVoiceMidiNote(voice, 127/64); records through MIDI path |
| Voice defaults | Drum1=36 … Drum7=42 (MIDI note); overridden by CC2_MIDI_NOTE if set |
| CLK OUT | PC13, active high through DD1 |
| CLK IN | PD4, active low via VT1, EXTI4 falling edge |
| RST IN | PD5, active low via VT2, EXTI9_5 both edges; semantics: active-low run/gate |
| DIN TX FIFO | Non-blocking, dual FIFO (realtime priority + normal), TXE interrupt |
| Phase 6 still pending | TIM3 is fixed-interval, not compare-register one-shot.  Jitter unmeasured |
