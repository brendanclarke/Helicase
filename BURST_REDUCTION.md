# Main-Loop Burst Reduction — Audio Latency Follow-Up

Continues the audio-latency investigation. Goal: reduce `AUDIO_DMA_FRAMES`
below 96 without reintroducing the buffer underruns seen at 64. Finds and
fixes the foreground bursts that ate the scheduling slack in the earlier
64-frame attempt.

## Corrected latency model

Earlier in this investigation, deepening the audio queue was proposed as a
way to cut latency. That was wrong, and worth restating so it isn't
re-proposed: the queue only ever holds rendered-but-not-yet-played audio, so
a deeper queue makes latency *worse*, not better. With the current
`while (audioCodec_queueFreeSlots() > 0)` fill strategy (`main.c:142`),
latency is approximately:

```
latency ≈ 2 × AUDIO_DMA_FRAMES / Fs
```

(one queued slot ahead, plus the DMA half currently playing). At
`AUDIO_DMA_FRAMES = 96`, that's **2 × 2.18ms = 4.35ms**. The only way to cut
latency is to shrink `AUDIO_DMA_FRAMES` itself — which also shrinks the
render deadline per slot, which is exactly what caused the 64-frame
underruns. So the real problem is not the queue depth; it's that some
foreground task between two `audio_check_and_render()` calls can exceed the
deadline.

## Main loop structure

`main.c:319-368` interleaves 14 `audio_check_and_render()` calls with the
foreground tasks, in this order:

```
audio_check_and_render()
seq_ledState_process()         audio_check_and_render()
timebase_serviceFrontPanel()   audio_check_and_render()
midi_service()                 audio_check_and_render()
main_encoder_check()           audio_check_and_render()
endless_pot_check()            audio_check_and_render()
service_knob_repaint()         audio_check_and_render()
menu_serviceRuntimeWidgets()   audio_check_and_render()
adc_checkPots()                audio_check_and_render()
screensaver_check()            audio_check_and_render()
led_tickHandler()              audio_check_and_render()
buttonHandler_processEvents()  audio_check_and_render()
buttonHandler_tick()           audio_check_and_render()
menu_pollPresetStatus()        audio_check_and_render()
preset_morphTick()             audio_check_and_render()
filesystem_tick()              audio_check_and_render()
```

Each task gets one `audio_check_and_render()` slot immediately after it, so
the question for each task is simply: can it run longer than the render
deadline (1.45ms at 64 frames; 1.09ms at 48; 0.73ms at 32) before that next
render call?

## Per-task findings

### `menu_pollPresetStatus()` → kit-load burst — **the real risk**

`menu_pollPresetStatus()` (`menu.c:1893`) checks `preset_getStatus() ==
PRESET_UPDATE_READY` and dispatches on `preset_getCompletedOp()`. The
`PRESET_OP_KIT_LOAD` case (`menu.c:1935-1953`) runs synchronously and
unconditionally on the iteration a kit finishes loading:

```c
menu_normalizeSoundModTargets(parameter_values);
preset_sendDrumsetParameters();
menu_TargetVoiceGapIndex = getModTargetGapIndex(...);
menu_repaintAll();
```

`preset_sendDrumsetParameters()` (`presetManager.c:268`) loops over the 6
mod-target voices, and for each one resolves a `modTargets[]` index and
calls `preset_sendModTarget()` twice (`presetManager.c:272-320`):

```c
for (i = 0; i < 6; i++) {
    preset_sendModTarget(CC_VELO_TARGET, upper, lower);
    preset_sendModTarget(CC_LFO_TARGET, upper, lower);
}
preset_morph(parameter_values[PAR_MORPH]);
```

`preset_sendModTarget()` (`presetManager.c:238`) calls
`modNode_setDestination()` directly — fast per call, but this is 12
back-to-back DSP graph mutations with no yield point. `preset_morph()`
itself (`presetManager.c:511`) is cheap; it only arms `morph_active` and
lets `preset_morphTick()` apply one parameter per main-loop iteration —
**this part is already correctly rate-limited and is not the burst.**

The actual unbounded piece is `menu_repaintAll()` immediately after: it
memsets the full 2×16 display buffer to a sentinel (`menu.c:1052-1059`),
forcing `sendDisplayBuffer()` to push all 32 characters through the LCD
queue instead of an incremental diff repaint. This is bounded but is by far
the largest single push in this path.

**Why this is a real but smaller risk than first thought:** an earlier pass
over this code (during the interrupted research round) estimated this burst
at up to 252 parameter dispatches, by analogy with `NUM_PARAMS`. That
estimate was wrong — `preset_sendDrumsetParameters()` only touches the 6
mod-target voices (12 dispatch calls), not the full parameter array. The
real risk isn't dispatch count, it's that this whole sequence — mod targets,
`getModTargetGapIndex()`, and the full-buffer `menu_repaintAll()` — runs
between exactly *one* pair of `audio_check_and_render()` calls, with no
internal yield point, unlike every other multi-step operation in this
codebase.

**This codebase already has the right pattern for this, just not applied
here.** `menu_tickGlobalApply()` (`menu.c:126-143`) amortizes the
globals-load apply burst across main-loop iterations with a budget of 2
parameters per tick:

```c
static uint8_t menu_tickGlobalApply(void)
{
    uint8_t budget = 2u;
    while (budget-- && menu_globalApplyIndex < NUM_PARAMS) {
        menu_parseGlobalParam(menu_globalApplyIndex, parameter_values[menu_globalApplyIndex]);
        menu_globalApplyIndex++;
    }
    if (menu_globalApplyIndex >= NUM_PARAMS)
        menu_finishGlobalApply();
    return 1u;
}
```

This was added in Session 023 ("global apply after load is amortized") for
exactly this class of bug, but the kit-load path
(`PRESET_OP_KIT_LOAD` in `menu_pollPresetStatus()`) still applies its burst
synchronously. The fix below ports the same pattern.

### `menu_repaintAll()` — bounded by design, not a risk

`LCD_QUEUE_SIZE` is 128, explicitly sized with the comment "more than a full
32-char repaint" (`lcd.h:89`). `lcd_enqueue()` (`lcd.c:238`) is a
non-blocking ring-buffer push — full queue silently drops the op rather than
blocking, with the consumer side documented as intentionally favoring a
missed LCD update over a missed audio block (`lcd.c:231-237`). TIM7 drains
the queue in the background at 5kHz (per Session 023). **`menu_repaintAll()`
itself never blocks the main loop**, regardless of `AUDIO_DMA_FRAMES`. It's
included above only because it's part of the same uninterrupted call
sequence as the mod-target loop, not because the LCD push itself is risky.

### `filesystem_tick()` / `afatfs_poll()` / `sdcard_poll()` — bounded, not a risk

`sdcard_poll()` clocks a fixed-size burst per call (16 bytes, bit-banged
SPI, ~1.2µs/call). `afatfs_poll()` advances the filesystem state machine by
at most one step per call. Idle-time polling is already rate-limited to
once per `FS_IDLE_POLL_MS` (Session 023). A full 512-byte sector read is
spread across roughly 32 main-loop iterations rather than completed in one.
Worst realistic single-call cost is on the order of 10µs, with a rare
cache-cascade case up to ~80µs — both well inside even the tightest proposed
deadline.

### `timebase_serviceFrontPanel()` — bounded, not a risk

Runs at 500Hz (every other 1kHz TIM6 tick), so it only executes on roughly
half of main-loop passes. Cost is dominated by `endlessPots_service()`'s 4×
`atan2f()` calls (~15µs each), for a total of roughly 80-100µs. Shift
register I/O (`dout_shiftOut()`, `din_shiftIn()`) and jack/encoder polling
are GPIO-bound and add only a few µs more.

### `buttonHandler_processEvents()`, `buttonHandler_tick()`, `preset_morphTick()`, `led_tickHandler()`, `menu_serviceRuntimeWidgets()`, `menu_serviceKnobRepaint()`, `adc_checkPots()`, `seq_ledState_process()` — bounded, not a risk

All of these already follow the single-item-per-call or small-fixed-budget
pattern that should be applied to the kit-load burst:
`buttonHandler_processEvents()` uses `if` not `while` deliberately
(documented in `MEMORY.md`); `preset_morphTick()` advances one parameter per
call; `led_tickHandler()` and `seq_ledState_process()` drain small bounded
queues; the ADC/widget/knob-repaint paths are rate-limited or touch a
handful of fixed channels. None of these individually or combined approach
even the 0.73ms deadline of a 32-frame buffer.

## Summary

| Task | Worst case | Risk at 64/48/32-frame deadline? |
|---|---|---|
| Kit-load mod-target apply + repaint | Bounded but unchunked, single burst | **Yes — the only real risk** |
| `menu_repaintAll()` LCD push | Non-blocking enqueue, ~32 ops | No — queue sized for this |
| `filesystem_tick()` / SD poll | ~10µs typical, ~80µs rare cascade | No |
| `timebase_serviceFrontPanel()` | ~80-100µs, runs at 500Hz | No |
| All other foreground tasks | Single-item or small fixed budget | No |

The only foreground task in the entire main loop that runs an unbounded,
unchunked sequence between two `audio_check_and_render()` calls is the kit
apply path inside `menu_pollPresetStatus()`. Everything else was already
either inherently bounded or had already been fixed in a prior session.

## Proposed changes

### 1. Reduce `AUDIO_DMA_FRAMES`

#### [MODIFY] `config.h`
- Change `AUDIO_DMA_FRAMES` from 96 to **64** (1.45ms render deadline, a
  33% latency cut to 2.90ms with the existing fill-to-capacity queue
  strategy). 64 keeps the required `AUDIO_DMA_FRAMES % OUTPUT_DMA_SIZE == 0`
  invariant (`config.h:263`) with `OUTPUT_DMA_SIZE = 32`. Do not attempt 48
  without also revisiting `OUTPUT_DMA_SIZE` — Session 019 fixed
  `OUTPUT_DMA_SIZE` at 32 specifically to correct EG/LFO rates; reverting it
  to get a 48-aligned frame size would reintroduce that bug.
- No `.dma_nocache` linker pressure: usage drops from `2 × 96 × 2 = 3072`
  bytes to `2 × 64 × 2 = 2048` bytes, well inside the 4096-byte assert.

### 2. Chunk the kit-load apply burst

#### [MODIFY] `Core/Preset/presetManager.c`
- Split `preset_sendDrumsetParameters()` into a state-driven tick, mirroring
  `menu_tickGlobalApply()`. Replace the single synchronous 6-voice loop with
  a small index-based state machine (`preset_drumsetApplyVoice`,
  0..5) that applies one voice's two `preset_sendModTarget()` calls per
  invocation, and calls `preset_morph()` only after the last voice. Add
  `preset_tickDrumsetApply(void)` returning 1 while still in progress, 0
  when done.

#### [MODIFY] `Core/Menu/menu.c`
- In the `PRESET_OP_KIT_LOAD` case of `menu_pollPresetStatus()`
  (`menu.c:1935`), replace the direct
  `preset_sendDrumsetParameters()` call with the same
  active/pending-flag pattern `menu_startGlobalApply()` /
  `menu_tickGlobalApply()` already use: start the apply state machine here,
  defer `menu_TargetVoiceGapIndex` update and `menu_repaintAll()` until the
  tick function reports completion. Add a `menu_tickKitApply()` call at the
  same point `menu_tickGlobalApply()` is currently checked, near the top of
  `menu_pollPresetStatus()`.
- This keeps the visible behavior identical (kit parameters appear applied
  "all at once" from the user's perspective, since 6 voices complete in 6
  main-loop iterations — under 0.5ms even at the tightest deadline) while
  removing the only unbounded burst in the loop.

### 3. Update documentation

#### [MODIFY] `MEMORY.md`
- Update `AUDIO_DMA_FRAMES` value and the 2.18ms latency figures to 64
  frames / 1.45ms render budget / 2.90ms total latency.
- Add a line noting the kit-load apply burst is now chunked, matching the
  existing globals-load entry.

## Verification plan

### Automated
- `make clean && make && make img` — must build with no warnings.
- Linker `.dma_nocache` assert passes (2048 < 4096).

### Manual
- Flash and boot — confirm no startup underruns.
- Play a pattern continuously — confirm no audible glitches in normal use.
- Load a kit from SD while a pattern plays — this is the scenario that
  exercises the fixed code path; confirm no glitch coincides with kit load
  completion, and that mod targets/LCD update correctly within a few
  main-loop iterations (should be imperceptible to the user).
- Check the existing CPU-use widget for similar or better headroom versus
  the current 96-frame build.
- Watch `audioCodec_underrunCount` (`AudioCodecManager.c:235`) during a kit
  load — should stay at 0. This counter and `audioCodec_renderCount`
  already exist and are reset in `audioCodec_init()`
  (`AudioCodecManager.c:613-614`); the commented-out LCD diagnostic block at
  the bottom of `main.c` can be uncommented for an on-device readout if a
  debugger isn't convenient.

## Open items / not pursued here

- **48-frame / 1.09ms target**: would require either accepting a non-power-
  related `OUTPUT_DMA_SIZE` (touches Session 019's EG/LFO fix — avoid) or
  splitting `OUTPUT_DMA_SIZE` from the hardware DMA alignment requirement
  entirely. Worth a dedicated session if 64/1.45ms-2.90ms proves stable and
  more headroom is wanted.
- **I-cache/D-cache/MPU/NVIC priority work**: already done per
  `DSP_AUDIT.md`'s priority list (items 1-10 marked `DONE`). Not revisited
  here since this document is specifically about main-loop scheduling
  jitter, not DSP render cost.
