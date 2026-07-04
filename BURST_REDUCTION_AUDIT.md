# Main-Loop Burst Reduction Audit

## Executive conclusion

`BURST_REDUCTION.md` correctly identifies the general class of problem:
foreground work can run between two `audio_check_and_render()` calls, so any
long uninterrupted foreground path reduces the slack available to the DSP
render pump. The document is also right that most steady-state foreground
work has already been shaped into small chunks.

The suspicious parts are:

1. The first test should not lower `AUDIO_DMA_FRAMES`. `config.h` already
   documents that 64 frames was tried and caused freeze/glitch behavior during
   pattern-load testing. Burst reduction should be tested at the current stable
   96-frame setting first. Shrinking to 64 is a later latency experiment, not
   part of the first burst-reduction patch.
2. The plan only calls out `PRESET_OP_KIT_LOAD`, but the same synchronous
   `preset_sendDrumsetParameters()` burst also runs in `PRESET_OP_ALL_LOAD`
   and `PRESET_OP_PERFORMANCE_LOAD`.
3. The real source-level burst is not a 250-parameter dump. Current
   `preset_sendDrumsetParameters()` applies the six velocity modulation
   destinations and six LFO modulation destinations, then arms the morph
   engine. That is much smaller than an all-parameter send, but it still runs
   as one uninterrupted DSP graph mutation sequence in the preset-completion
   path.
4. `menu_repaintAll()` is queue-heavy but non-blocking. It can add pressure
   to the same foreground pass, but it is not a blocking LCD wait. The LCD
   driver explicitly drops queue entries instead of stalling audio.

Recommended first implementation: chunk the sound/mod-target apply path across
main-loop iterations for kit, all, and performance load completions. Keep
`AUDIO_DMA_FRAMES=96` for the first test build. Only after that passes should
a separate test try `AUDIO_DMA_FRAMES=64`.

## Source facts

### Main loop render interleaving

`main.c` calls `audio_check_and_render()` before and after each major
foreground task. The render function fills all free codec slots:

- `main.c:135-153`: `audio_check_and_render()` loops while
  `audioCodec_queueFreeSlots() > 0`, renders one `AUDIO_DMA_FRAMES` slot, and
  subdivides it into `OUTPUT_DMA_SIZE` DSP blocks.
- `main.c:322-367`: foreground tasks are interleaved with render checks.
- `AudioCodecManager.c:309-312`: the codec queue exposes two ready slots.

This means a foreground task does not need to be "huge" to matter. It only
needs to be the longest piece of work between two render-pump opportunities.

### DMA frame size should stay stable for the first patch

`config.h:251-263` defines:

- `OUTPUT_DMA_SIZE = 32`, the canonical DSP/control block size.
- `AUDIO_DMA_FRAMES = 96`, the stable hardware half size.
- A comment stating 64 frames reduced slack enough to freeze/glitch during
  pattern-load testing.

So the first burst-reduction patch should not include a frame-size change. A
frame-size change would make the test ambiguous: if it glitches, we would not
know whether the chunking failed or the latency experiment is still too tight.

### Current synchronous sound apply path

`menu_pollPresetStatus()` applies completed preset operations:

- `menu.c:1935-1952`, `PRESET_OP_KIT_LOAD`:
  normalizes mod-target indices, calls `preset_sendDrumsetParameters()`,
  updates `menu_TargetVoiceGapIndex`, and calls `menu_repaintAll()`.
- `menu.c:2000-2010`, `PRESET_OP_ALL_LOAD`:
  normalizes mod-target indices, calls `preset_sendDrumsetParameters()`,
  starts global apply, requests pattern params, and clears storage busy.
- `menu.c:2013-2022`, `PRESET_OP_PERFORMANCE_LOAD`:
  normalizes mod-target indices, calls `preset_sendDrumsetParameters()`,
  applies BPM/bar-reset globals, requests pattern params, clears storage busy,
  resets the save UI, and repaints.

`preset_sendDrumsetParameters()` is in `presetManager.c:268-328`. It loops over
six voices. For each voice it:

1. Converts `PAR_VEL_DEST_1 + voice` from a `modTargets[]` index to a parameter
   destination.
2. Calls `preset_sendModTarget(CC_VELO_TARGET, ...)`.
3. Clamps `PAR_VOICE_LFO1 + voice` to 1..6.
4. Converts `PAR_TARGET_LFO1 + voice` from a `modTargets[]` index to a
   parameter destination.
5. Calls `preset_sendModTarget(CC_LFO_TARGET, ...)`.

After the loop it calls `preset_morph(parameter_values[PAR_MORPH])`.

`preset_sendModTarget()` is local to `presetManager.c:238-266` and calls
`modNode_setDestination()` directly. `modNode_setDestination()` at
`modulationNode.c:209-250` resets modulation targets, restores the previous
destination's original value, stores the new destination, and reads the new
original value out of `parameterArray[]`.

This is bounded work, but it is currently unchunked.

### Morph is already chunked

`preset_morph()` at `presetManager.c:511-523` only records the target morph
value and arms a generation counter. `preset_morphTick()` at
`presetManager.c:525-555` advances one sound parameter per main-loop pass.

Do not "fix" morph by expanding it into a synchronous send. Its existing shape
is the model the burst-reduction patch should preserve.

### Globals are already chunked

`menu_tickGlobalApply()` at `menu.c:126-143` applies two global parameters per
main-loop pass. `menu_startGlobalApply()` at `menu.c:109-124` keeps boot-time
behavior synchronous when audio has not rendered yet.

This is the correct pattern to copy for sound/mod-target apply:

- synchronous before audio starts,
- small fixed budget after audio starts,
- completion-side UI work deferred until the chunked apply is done.

### LCD repaint is not a blocking wait

`menu_repaintAll()` at `menu.c:1052-1059` clears the software display buffers
and calls `menu_repaint()`. `sendDisplayBuffer()` then emits changed
characters through `lcd_setcursor()` and `lcd_data()`.

The LCD queue behavior matters:

- `lcd.h:89` sets `LCD_QUEUE_SIZE` to 128.
- `lcd.c:231-238` documents that LCD enqueue drops an op if full because audio
  must not wait for LCD drain.
- `lcd.c:238-262` implements the non-blocking enqueue.

So full repaint is bounded queue production, not a blocking LCD transfer. It
can still be deferred until after chunked sound apply to keep one foreground
pass lighter, but it is not the same kind of risk as synchronous SD or a long
parameter loop.

### SD and front-panel services are already bounded

`filesystem_tick()` at `filesystem.c:2696-2745` polls asyncfatfs and advances
one filesystem operation state per call. When idle, it is rate-limited by
`FS_IDLE_POLL_MS`.

The SD-card shim clocks data in small bursts:

- `sdcard_lxr02.c:68`: `SDCARD_BURST_SIZE = 16`.
- `sdcard_lxr02.c:221-229`: read data advances at most 16 bytes per poll.
- `sdcard_lxr02.c:250-258`: write data advances at most 16 bytes per poll.
- `asyncfatfs.c:3482-3499`: `afatfs_poll()` only advances when
  `sdcard_poll()` says the card is ready.

`timebase_serviceFrontPanel()` at `timebase.c:141-169` runs only when TIM6 has
scheduled foreground service, clears the due flag, performs one shift-register
exchange, samples jack state, ticks the encoder, and ticks endless pots.

These areas do not need changes for the first burst-reduction patch.

## Implementation plan

### 1. Do not change `AUDIO_DMA_FRAMES` in the first test

File: `config.h`

No code change for the first patch. Leave:

```c
#define AUDIO_DMA_FRAMES     96
```

Why: this keeps the test focused on foreground burst shape. Reducing the buffer
to 64 at the same time would recreate the previously failing condition and make
debugging ambiguous.

Impact: no latency change in the first test build. The expected win is fewer
large foreground spikes and less pressure on the current DSP render budget.

Later optional test: after chunking is proven stable, create a separate build
that changes only `AUDIO_DMA_FRAMES` from 96 to 64. That is the latency test,
not the burst-reduction test.

### 2. Factor one-voice sound apply in `presetManager.c`

File: `Core/Preset/presetManager.c`

Add a small internal helper that applies exactly one voice's two modulation
destinations. The body should be the current loop body from
`preset_sendDrumsetParameters()`, with `i` replaced by the helper argument.

Suggested shape:

```c
static void preset_applyDrumsetVoice(uint8_t voice)
{
    uint8_t value;
    uint8_t upper;
    uint8_t lower;

    if (voice >= 6u)
        return;

    value = (uint8_t)(modTargets[parameter_values[PAR_VEL_DEST_1 + voice]].param);
    upper = (uint8_t)(((value & 0x80u) >> 7) | ((voice & 0x3fu) << 1));
    lower = (uint8_t)(value & 0x7fu);
    preset_sendModTarget(CC_VELO_TARGET, upper, lower);

    if (parameter_values[PAR_VOICE_LFO1 + voice] < 1u ||
        parameter_values[PAR_VOICE_LFO1 + voice] > 6u) {
        parameter_values[PAR_VOICE_LFO1 + voice] = 1u;
    }

    value = (uint8_t)(modTargets[parameter_values[PAR_TARGET_LFO1 + voice]].param);
    upper = (uint8_t)(((value & 0x80u) >> 7) | ((voice & 0x3fu) << 1));
    lower = (uint8_t)(value & 0x7fu);
    preset_sendModTarget(CC_LFO_TARGET, upper, lower);
}
```

Add this comment above the helper:

```c
/* Apply one loaded kit voice's modulation routing.
**
** This helper is deliberately one voice wide so runtime kit/all/performance
** load completion can spread DSP graph mutations across main-loop passes.
** The synchronous wrapper below still calls all six voices before audio starts,
** preserving boot behavior and legacy call sites.
*/
```

Why: the current function is small but uninterruptible. Factoring one voice
gives the menu a fixed unit of work to schedule.

Impact: no behavior change by itself. This is a mechanical split.

### 3. Keep the synchronous wrapper for boot and simple callers

File: `Core/Preset/presetManager.c`

Rewrite `preset_sendDrumsetParameters()` to use the helper:

```c
void preset_sendDrumsetParameters(void)
{
    uint8_t voice;

    for (voice = 0; voice < 6u; voice++)
        preset_applyDrumsetVoice(voice);

    preset_morph(parameter_values[PAR_MORPH]);
}
```

Update the comment above it:

```c
/* Synchronous loaded-kit apply.
**
** Safe before audio starts and retained for boot-time loads. Runtime load
** completion should prefer preset_startDrumsetApply()/preset_tickDrumsetApply()
** so the six mod-target updates do not run as one foreground burst.
*/
```

Why: boot code in `main.c:271-284` calls `menu_pollPresetStatus()` before
`audioCodec_init()`. Synchronous apply is fine there because audio is not
running yet. Keeping this wrapper prevents the burst-reduction patch from
touching boot semantics.

Impact: existing code can still call the old API. Runtime menu completion will
move to the chunked API in the next steps.

### 4. Add a chunked drumset-apply API

Files:

- `Core/Preset/presetManager.c`
- `Core/Preset/presetManager.h`

Add state in `presetManager.c`:

```c
static uint8_t drumset_apply_active = 0;
static uint8_t drumset_apply_voice = 0;
```

Add public functions:

```c
void preset_startDrumsetApply(void)
{
    drumset_apply_active = 1u;
    drumset_apply_voice = 0u;
}

uint8_t preset_tickDrumsetApply(void)
{
    if (!drumset_apply_active)
        return 0u;

    if (drumset_apply_voice < 6u) {
        preset_applyDrumsetVoice(drumset_apply_voice);
        drumset_apply_voice++;
        return 1u;
    }

    preset_morph(parameter_values[PAR_MORPH]);
    drumset_apply_active = 0u;
    return 0u;
}
```

Add this comment above the state:

```c
/* Runtime loaded-kit apply cursor.
**
** The foreground menu calls preset_tickDrumsetApply() once per main-loop pass
** after a kit/all/performance file has loaded. Each tick mutates at most one
** voice's velocity/LFO modulation routing. The final tick only arms morph,
** which is itself rate-limited by preset_morphTick().
*/
```

Header additions in `presetManager.h` near the existing synchronous
`preset_sendDrumsetParameters()` declaration:

```c
void    preset_startDrumsetApply(void);
uint8_t preset_tickDrumsetApply(void);
```

Header comment:

```c
/* Runtime chunked sound apply. preset_tickDrumsetApply() performs at most one
** voice of work and returns non-zero while more foreground work was consumed.
** Use this after audio has started; keep preset_sendDrumsetParameters() for
** boot-time synchronous apply. */
```

Return-value note: the final call returns `0` after arming morph and clearing
the active flag. Menu code should wrap this with its own active flag so it can
run completion UI work after the final tick.

Why: the preset layer owns the details of converting mod-target indices to DSP
modulation destinations. The menu should schedule the work, not duplicate the
translation.

Impact: runtime kit/all/performance completion spans about seven
`menu_pollPresetStatus()` calls: six voice ticks plus one morph-arm completion
tick. During those few passes, some voices may briefly still use old modulation
destinations while later voices have new ones. This is the intended tradeoff:
small bounded work instead of one foreground spike.

### 5. Add a menu-side pending sound-apply state

File: `Core/Menu/menu.c`

Add state near the existing globals-apply fields:

```c
static uint8_t menu_soundApplyActive = 0;
static uint8_t menu_soundApplyUpdateGap = 0;
static uint8_t menu_soundApplyResetSave = 0;
static uint8_t menu_soundApplyRepaintAll = 0;
static uint8_t menu_soundApplyStartGlobals = 0;
static uint8_t menu_soundApplyRequestPattern = 0;
static uint8_t menu_soundApplyApplyPerformanceGlobals = 0;
static uint8_t menu_soundApplyClearStorageBusy = 0;
static fs_stale_warning_source_t menu_soundApplyStaleWarning = FS_STALE_WARNING_NONE;
```

`FS_STALE_WARNING_NONE` already exists in `filesystem.h`, so this does not
require a filesystem enum change.

Add this comment:

```c
/* Runtime sound-apply completion flags.
**
** Kit, ALL, and performance loads all need the same six-voice modulation
** routing apply, but their follow-up work differs. These flags let the
** chunked apply path finish with the exact same UI/sequencer/global side
** effects that the old synchronous switch cases performed in one pass.
*/
```

Why: `menu_pollPresetStatus()` owns the operation-specific UI and sequencer
side effects. The preset layer should not know about repaint policy, storage
busy state, stale globals warnings, or load/save page retry behavior.

Impact: a little more menu state, but the behavior stays explicit per
completion type.

### 6. Add `menu_startSoundApply()`

File: `Core/Menu/menu.c`

Add a helper near `menu_startGlobalApply()`:

```c
static void menu_startSoundApply(uint8_t updateGap,
                                 uint8_t resetSave,
                                 uint8_t repaintAll,
                                 uint8_t startGlobals,
                                 uint8_t requestPattern,
                                 uint8_t applyPerformanceGlobals,
                                 uint8_t clearStorageBusy,
                                 uint8_t showStaleWarning,
                                 fs_stale_warning_source_t staleWarning)
{
    if (audioCodec_renderCount == 0u) {
        preset_sendDrumsetParameters();
        if (updateGap) {
            menu_TargetVoiceGapIndex = getModTargetGapIndex(
                parameter_values[PAR_TARGET_LFO1 + menu_activeVoice]);
        }
        if (applyPerformanceGlobals) {
            menu_parseGlobalParam(PAR_BPM, parameter_values[PAR_BPM]);
            menu_parseGlobalParam(PAR_BAR_RESET_MODE,
                                  parameter_values[PAR_BAR_RESET_MODE]);
        }
        if (startGlobals)
            menu_startGlobalApply(resetSave, repaintAll);
        if (requestPattern)
            frontPanel_sendData(SEQ_CC, SEQ_REQUEST_PATTERN_PARAMS, 0);
        if (clearStorageBusy)
            menu_storageBusy = 0;
        if (resetSave && !startGlobals)
            menu_resetSaveParameters();
        if (repaintAll && !startGlobals)
            menu_repaintAll();
        if (showStaleWarning)
            menu_showStaleSettingsWarning(staleWarning);
        return;
    }

    menu_soundApplyActive = 1u;
    menu_soundApplyUpdateGap = updateGap;
    menu_soundApplyResetSave = resetSave;
    menu_soundApplyRepaintAll = repaintAll;
    menu_soundApplyStartGlobals = startGlobals;
    menu_soundApplyRequestPattern = requestPattern;
    menu_soundApplyApplyPerformanceGlobals = applyPerformanceGlobals;
    menu_soundApplyClearStorageBusy = clearStorageBusy;
    menu_soundApplyShowStaleWarning = showStaleWarning;
    menu_soundApplyStaleWarning = staleWarning;
    preset_startDrumsetApply();
}
```

The exact stale-warning storage depends on the enum detail noted in step 5.

Add this comment:

```c
/* Start loaded-sound apply.
**
** Before audio starts, keep the old synchronous behavior: boot loading can do
** all post-load work immediately because no DMA deadline exists yet. After
** audio has rendered at least once, only arm the chunked preset apply and let
** menu_tickSoundApply() finish the operation over foreground passes.
*/
```

Why: this mirrors `menu_startGlobalApply()` and prevents boot-time code from
needing special cases.

Impact: runtime load completion no longer performs mod-target apply, sequencer
refresh, save reset, and repaint all in the same call stack.

### 7. Add `menu_finishSoundApply()` and `menu_tickSoundApply()`

File: `Core/Menu/menu.c`

Add helpers near the global-apply helpers:

```c
static void menu_finishSoundApply(void)
{
    menu_soundApplyActive = 0u;

    if (menu_soundApplyUpdateGap) {
        menu_TargetVoiceGapIndex = getModTargetGapIndex(
            parameter_values[PAR_TARGET_LFO1 + menu_activeVoice]);
    }

    if (menu_soundApplyApplyPerformanceGlobals) {
        menu_parseGlobalParam(PAR_BPM, parameter_values[PAR_BPM]);
        menu_parseGlobalParam(PAR_BAR_RESET_MODE,
                              parameter_values[PAR_BAR_RESET_MODE]);
    }

    if (menu_soundApplyStartGlobals)
        menu_startGlobalApply(menu_soundApplyResetSave,
                              menu_soundApplyRepaintAll);

    if (menu_soundApplyRequestPattern)
        frontPanel_sendData(SEQ_CC, SEQ_REQUEST_PATTERN_PARAMS, 0);

    if (menu_soundApplyClearStorageBusy)
        menu_storageBusy = 0u;

    if (!menu_soundApplyStartGlobals) {
        if (menu_soundApplyResetSave)
            menu_resetSaveParameters();
        if (menu_soundApplyRepaintAll)
            menu_repaintAll();
    }

    if (menu_soundApplyShowStaleWarning)
        menu_showStaleSettingsWarning(menu_soundApplyStaleWarning);

    menu_soundApplyUpdateGap = 0u;
    menu_soundApplyResetSave = 0u;
    menu_soundApplyRepaintAll = 0u;
    menu_soundApplyStartGlobals = 0u;
    menu_soundApplyRequestPattern = 0u;
    menu_soundApplyApplyPerformanceGlobals = 0u;
    menu_soundApplyClearStorageBusy = 0u;
    menu_soundApplyShowStaleWarning = 0u;
}

static uint8_t menu_tickSoundApply(void)
{
    if (!menu_soundApplyActive)
        return 0u;

    if (preset_tickDrumsetApply())
        return 1u;

    menu_finishSoundApply();
    return 1u;
}
```

Add this comment:

```c
/* Tick one bounded unit of loaded-sound apply.
**
** Returning 1 tells menu_pollPresetStatus() to stop for this pass, giving the
** main loop another audio_check_and_render() opportunity before any other
** preset completion work is handled.
*/
```

Why: the menu needs to defer operation-specific follow-up until all six
mod-target voices are applied and morph has been armed.

Impact: follow-up UI/sequencer work happens a few main-loop passes later. This
should be imperceptible on the panel but removes the single foreground burst.

### 8. Tick sound apply before global apply

File: `Core/Menu/menu.c`

At the top of `menu_pollPresetStatus()`, change:

```c
if (menu_tickGlobalApply())
    return;
```

to:

```c
if (menu_tickSoundApply())
    return;

if (menu_tickGlobalApply())
    return;
```

Comment to add:

```c
/* Sound apply runs before globals because ALL/performance completion first
** installs loaded modulation routing, then starts any global apply that belongs
** to the container. Keep both paths one bounded unit per foreground pass. */
```

Why: `PRESET_OP_ALL_LOAD` currently applies sound before starting global apply.
The chunked version should preserve that ordering.

Impact: if both sound and global apply are active, sound finishes first, then
globals continue at the existing two-parameter budget.

### 9. Convert `PRESET_OP_KIT_LOAD`

File: `Core/Menu/menu.c`

Replace the direct synchronous apply in `PRESET_OP_KIT_LOAD`:

```c
menu_normalizeSoundModTargets(parameter_values);
preset_sendDrumsetParameters();
menu_TargetVoiceGapIndex = getModTargetGapIndex(...);
menu_repaintAll();
```

with:

```c
menu_normalizeSoundModTargets(parameter_values);
menu_startSoundApply(1u, 0u, 1u, 0u, 0u, 0u, 0u, 0u,
                     FS_STALE_WARNING_GLO /* ignored when showStaleWarning=0 */);
```

If the stale-warning helper uses a separate boolean plus source, pass any valid
source when the boolean is false, or split the helper signature to avoid the
dummy argument.

Keep the retry-selection guard at `menu.c:1937-1942` unchanged.

Why: kit load is the direct user-facing case that triggered this audit.

Impact: the selected kit's sound-routing apply completes over several menu
polls, then the menu repaints once.

### 10. Convert `PRESET_OP_ALL_LOAD`

File: `Core/Menu/menu.c`

Replace:

```c
menu_normalizeSoundModTargets(parameter_values);
preset_sendDrumsetParameters();
menu_startGlobalApply(1u, 1u);
frontPanel_sendData(SEQ_CC, SEQ_REQUEST_PATTERN_PARAMS, 0);
menu_storageBusy = 0;
if (stale_src == FS_STALE_WARNING_ALL)
    menu_pendingAllStaleWarning = 1u;
```

with a chunked start:

```c
menu_normalizeSoundModTargets(parameter_values);
menu_startSoundApply(0u, 1u, 1u, 1u, 1u, 0u, 1u,
                     (uint8_t)(stale_src == FS_STALE_WARNING_ALL),
                     stale_src);
```

Then remove or stop using `menu_pendingAllStaleWarning` if it becomes redundant.
If you want the absolute smallest behavioral change, keep
`menu_pendingAllStaleWarning` and have `menu_finishSoundApply()` set it instead
of calling `menu_showStaleSettingsWarning()` directly. That preserves the old
extra one-pass separation between global application and warning display.

Preferred behavior: set `menu_pendingAllStaleWarning` after sound apply
finishes and after `menu_startGlobalApply()` has been called. Then the existing
top-of-`menu_pollPresetStatus()` stale-warning logic can continue to show the
warning after the global apply path has had its turn.

Why: all-file load has the same sound apply burst as kit load, plus global and
sequencer follow-up. It should not keep the synchronous sound apply.

Impact: all load applies sound routing first, then globals at the existing
two-parameter budget, then pattern params/UI/warnings as before.

### 11. Convert `PRESET_OP_PERFORMANCE_LOAD`

File: `Core/Menu/menu.c`

Replace:

```c
menu_normalizeSoundModTargets(parameter_values);
preset_sendDrumsetParameters();
menu_parseGlobalParam(PAR_BPM, parameter_values[PAR_BPM]);
menu_parseGlobalParam(PAR_BAR_RESET_MODE, parameter_values[PAR_BAR_RESET_MODE]);
frontPanel_sendData(SEQ_CC, SEQ_REQUEST_PATTERN_PARAMS, 0);
menu_storageBusy = 0;
menu_resetSaveParameters();
menu_repaintAll();
```

with:

```c
menu_normalizeSoundModTargets(parameter_values);
menu_startSoundApply(0u, 1u, 1u, 0u, 1u, 1u, 1u, 0u,
                     FS_STALE_WARNING_GLO /* ignored when showStaleWarning=0 */);
```

Why: performance load also calls the synchronous sound apply and therefore has
the same burst shape. BPM and bar-reset are only two global-style operations,
so they can remain in the finish step after the sound apply completes.

Impact: the loaded performance's modulation routing settles over a few passes,
then BPM/bar reset and pattern-menu refresh happen.

### 12. Leave morph-load alone for now

File: `Core/Menu/menu.c`

Do not change `PRESET_OP_MORPH_LOAD` in the first patch. It calls
`preset_morph(parameter_values[PAR_MORPH])`, which only arms the existing
one-parameter-per-pass morph engine.

Why: changing morph-load would expand the scope without addressing the known
burst.

Impact: none.

### 13. Leave LCD, SD, front-panel service, and button event draining alone

Files not to modify for this patch:

- `Core/Hardware/frontPanel/lcd.c`
- `Core/Hardware/SD/filesystem.c`
- `Core/Hardware/SD/asyncfatfs/sdcard_lxr02.c`
- `Core/Hardware/timebase.c`
- `Core/Hardware/frontPanel/buttonHandler.c`

Why: these are already bounded or non-blocking in the relevant paths. Changing
them at the same time would make the test wider and harder to interpret.

Impact: none.

## Verification plan

### Static/source checks

1. `rg -n "preset_sendDrumsetParameters\\(" Core` should show the synchronous
   wrapper only in boot-safe or explicitly synchronous contexts. Runtime
   `PRESET_OP_KIT_LOAD`, `PRESET_OP_ALL_LOAD`, and
   `PRESET_OP_PERFORMANCE_LOAD` should use `menu_startSoundApply()`.
2. Confirm `menu_pollPresetStatus()` ticks `menu_tickSoundApply()` before
   `menu_tickGlobalApply()`.
3. Confirm `AUDIO_DMA_FRAMES` remains 96 for the first test patch.
4. Confirm `buttonHandler_processEvents()` still drains one event per call.
5. Confirm no new `lcd_waitForIdle()` calls were added to runtime load
   completion paths.

### Build

Run:

```sh
make clean
make
make img
```

Expected result: clean build and image generation. The `.dma_nocache` linker
assert should be unchanged because this patch adds only a few bytes of normal
state, not DMA buffers.

### Manual hardware tests at `AUDIO_DMA_FRAMES=96`

1. Boot with a known-good MBR-FAT32 SD card.
2. Play a dense pattern and repeatedly scroll/load kits from the load page.
3. Load an `.all` file while the sequencer is running.
4. Load a performance while the sequencer is running.
5. Confirm no audible repeated-block glitch or freeze at load completion.
6. Confirm loaded kit mod targets work after load.
7. Confirm all/performance loads still update pattern parameters, BPM/bar reset
   behavior, save UI state, and stale-global warnings.
8. Watch the existing CPU/queue diagnostic if available:
   `audioCodec_underrunCount` should not increment during load completion.

### Optional instrumentation test

If hardware behavior is still ambiguous, add temporary counters guarded by a
compile-time define:

- Increment a counter each time `preset_tickDrumsetApply()` applies a voice.
- Record `DWT_CYCCNT` deltas around `menu_pollPresetStatus()` to compare
  pre-patch and post-patch worst-case cycles.

Remove the instrumentation after measuring. Do not leave LCD diagnostic prints
inside the hot path for the final patch.

### Later latency test

Only after the 96-frame burst-reduction build passes, make a separate test
patch:

```c
#define AUDIO_DMA_FRAMES 64
```

Retest the same kit/all/performance load scenarios. If it still glitches at 64,
the remaining pressure is not this mod-target burst alone. At that point, the
next work should be measurement-driven rather than another speculative
foreground refactor.

## Risk notes

1. Brief mixed modulation-routing state: during the six sound-apply ticks, some
   voices can have new mod destinations while others still have old ones. This
   window should be very short, but it is real. It is the cost of avoiding a
   foreground burst.
2. Completion ordering must stay explicit. All/performance loads do more than
   sound apply, so the menu finish flags must preserve the old order of global
   apply, pattern request, storage-busy clearing, reset-save, repaint, and stale
   warnings.
3. Do not move this work into an ISR. `modNode_setDestination()` touches DSP
   graph state and parameter pointers. The safe pattern is foreground chunking
   with frequent returns to the audio render pump.
4. Do not deepen the audio queue to reduce latency. The current queue holds
   rendered audio ahead of playback. More queued audio increases latency.
5. Do not change `OUTPUT_DMA_SIZE`. Session history and current comments state
   that 32 is the correct LXR-master DSP/control cadence.

## Final recommendation

Implement only the chunked sound-apply path first, covering kit, all, and
performance load completions. Keep `AUDIO_DMA_FRAMES=96` for that validation
build. After that build is confirmed stable on hardware, treat
`AUDIO_DMA_FRAMES=64` as a separate latency experiment with the same manual
load-completion tests.

## Implementation Notes

### Applied changes

The first implementation pass follows the audit's core recommendation and
does not change `AUDIO_DMA_FRAMES`.

Files changed:

- `Core/Preset/presetManager.c`
- `Core/Preset/presetManager.h`
- `Core/Menu/menu.c`
- `BURST_REDUCTION_AUDIT.md`

Preset-layer changes:

- Added a one-voice `preset_applyDrumsetVoice()` helper that contains the old
  velocity/LFO modulation-destination apply body.
- Reworked `preset_sendDrumsetParameters()` into a synchronous wrapper around
  the one-voice helper, preserving boot-time behavior.
- Added `preset_startDrumsetApply()` and `preset_tickDrumsetApply()` so runtime
  load completion can apply one voice per menu foreground pass.
- Left `preset_morph()` asynchronous/rate-limited: the final drumset-apply tick
  only arms morph, and `preset_morphTick()` still performs the parameter walk.

Menu-layer changes:

- Added `menu_startSoundApply()`, `menu_finishSoundApply()`, and
  `menu_tickSoundApply()`.
- Added explicit menu-side flags for follow-up work because kit, ALL, and
  performance loads share the same sound apply but differ in repaint, global,
  pattern-request, storage-busy, and stale-warning behavior.
- `menu_pollPresetStatus()` now ticks sound apply before global apply so ALL
  load preserves the old ordering: sound routing first, then globals.
- Converted `PRESET_OP_KIT_LOAD`, `PRESET_OP_ALL_LOAD`, and
  `PRESET_OP_PERFORMANCE_LOAD` to start chunked sound apply instead of calling
  `preset_sendDrumsetParameters()` directly.
- Preserved the existing delayed ALL stale-warning behavior by setting
  `menu_pendingAllStaleWarning` after sound apply starts global apply.

### Build status

Completed after implementation:

```sh
make clean
make
make img
```

Result: build and image packaging passed. `build/LXRV2_lxr02.img` was written
successfully. The build still emits existing warnings from asyncfatfs,
USB/packed-pointer handling, newlib syscall stubs, and LTO serial compilation;
no new warning was introduced by the chunked sound-apply changes.
