# Session 027 Handoff Log — Main-Loop Burst Reduction

DATE: 2026-07-04

SESSION GOAL: Reduce operation burst in the main loop to lower DSP pressure. Start from `BURST_REDUCTION.md`, but treat it as suspicious and verify the actual code. Write `BURST_REDUCTION_AUDIT.md`, then implement everything in the accepted plan except the `AUDIO_DMA_FRAMES` change. Close the session with index/log/MEMORY updates.

COMPLETED: Audited the current main-loop and preset-completion code, wrote `BURST_REDUCTION_AUDIT.md`, implemented chunked runtime sound/mod-target apply for kit/all/performance load completion, regenerated `build/LXRV2_lxr02.img`, appended this session to `000_SESSION_INDEX.md`, and updated `MEMORY.md`.

VERIFIED ON HARDWARE: Partially. User reported no obvious regression and that it "doesn't seem to break anything." There was no direct instrumentation or hardware proof that the burst reduction is working as intended.

CHANGES THIS SESSION:
- `BURST_REDUCTION_AUDIT.md`: New detailed audit and implementation plan. Key correction: do not lower `AUDIO_DMA_FRAMES` in the first burst-reduction test; keep 96 and test scheduling changes separately. Also corrected the plan scope from kit-only to kit/all/performance load completion.
- `Core/Preset/presetManager.c`: Factored the old six-voice `preset_sendDrumsetParameters()` work into `preset_applyDrumsetVoice()`. Added `preset_startDrumsetApply()` and `preset_tickDrumsetApply()` so runtime load completion applies one voice's velocity/LFO modulation routing per menu foreground pass. Kept `preset_sendDrumsetParameters()` as a synchronous wrapper for boot/pre-audio behavior.
- `Core/Preset/presetManager.h`: Declared the new chunked drumset-apply API and documented that runtime load completion should use it instead of the synchronous wrapper.
- `Core/Menu/menu.c`: Added menu-side sound-apply state and helpers: `menu_startSoundApply()`, `menu_finishSoundApply()`, and `menu_tickSoundApply()`. Converted `PRESET_OP_KIT_LOAD`, `PRESET_OP_ALL_LOAD`, and `PRESET_OP_PERFORMANCE_LOAD` from direct `preset_sendDrumsetParameters()` calls to chunked sound apply. `menu_pollPresetStatus()` now ticks sound apply before global apply to preserve container-load ordering.
- `build/LXRV2_lxr02.img`: Regenerated after the firmware build.
- `knowledge_files/log_archive/000_SESSION_INDEX.md`: Added Session 027 quick-reference row, summary, and key cross-session fact.
- `knowledge_files/log_archive/027_SESSION_HANDOFF_LOG.md`: This handoff.
- `MEMORY.md`: Updated session summary, log list, audio-pipeline/runtime-load note, display/menu reminder, and resolved Session 027 note.

KNOWN ISSUES INTRODUCED: None known. Direct hardware verification of the actual reduced burst is still missing.

KNOWN ISSUES RESOLVED: Runtime kit/all/performance load completion no longer performs the six-voice modulation-destination apply as one uninterrupted foreground burst. The apply is now spread across foreground passes after audio has started.

NEXT SESSION RECOMMENDED GOAL: Hardware-test the chunked load-completion path with active playback: repeatedly load kits, `.all`, and performance files while watching/listening for underruns/glitches and verifying mod targets, pattern params, BPM/bar reset behavior, save UI, and stale-globals warnings.

BLOCKERS: No direct instrumentation exists yet for proving the burst reduction. If audible behavior is ambiguous, add temporary DWT cycle measurements around `menu_pollPresetStatus()` and/or counters around `preset_tickDrumsetApply()`.

CRITICAL REMINDERS FOR NEXT SESSION:
- `AUDIO_DMA_FRAMES` is still 96. Do not test 64-frame latency until the chunked apply behavior is confirmed stable at 96.
- Runtime kit/all/performance load completion should go through `menu_startSoundApply()` and `preset_tickDrumsetApply()`, not direct `preset_sendDrumsetParameters()`.
- Boot/pre-audio loads intentionally remain synchronous through `preset_sendDrumsetParameters()` when `audioCodec_renderCount == 0`.
- `preset_morph()` only arms work; `preset_morphTick()` remains the one-parameter-per-main-loop worker.
- User reported no obvious regression, but that is not the same as proving underrun reduction.

## Detailed Notes

### Audit Results

`BURST_REDUCTION.md` was mostly right about the shape of the problem: any long foreground path between two `audio_check_and_render()` calls can reduce DSP scheduling slack. However, the audit found several important corrections:

- The initial implementation should not change `AUDIO_DMA_FRAMES`. `config.h` already documents that 64 frames was tried and caused freeze/glitch behavior during pattern-load testing. This session left `AUDIO_DMA_FRAMES=96`.
- The target was not only `PRESET_OP_KIT_LOAD`. The same synchronous `preset_sendDrumsetParameters()` call existed in `PRESET_OP_ALL_LOAD` and `PRESET_OP_PERFORMANCE_LOAD`.
- The current burst is not a full 250-parameter dump. `preset_sendDrumsetParameters()` applies six velocity destinations and six LFO destinations, then arms morph. That is still an unchunked DSP-graph mutation sequence, but smaller than the earlier document implied.
- `menu_repaintAll()` is queue-heavy but non-blocking; the LCD queue drops operations instead of blocking audio. It still makes sense to defer repaint until after chunked sound apply, but LCD repaint was not the primary blocking risk.

### Preset-Layer Implementation

`presetManager.c` now has a one-voice helper:

- Applies one voice's velocity mod destination.
- Clamps `PAR_VOICE_LFO1 + voice` into 1..6 before resolving the LFO destination.
- Applies one voice's LFO mod destination.

The new runtime cursor is:

- `drumset_apply_active`
- `drumset_apply_voice`

The public API is:

- `preset_startDrumsetApply()`: arms the cursor at voice 0.
- `preset_tickDrumsetApply()`: applies at most one voice per call and returns non-zero when it consumed work. After voice 5, the next call arms morph with `preset_morph(parameter_values[PAR_MORPH])`, clears active state, and returns 0.

`preset_sendDrumsetParameters()` still exists and remains synchronous. It loops over all six voices and then calls `preset_morph()`. This is intentional for boot-time/pre-audio loads.

### Menu-Layer Implementation

`menu.c` now has menu-owned follow-up flags because kit/all/performance loads share the same sound apply but differ in UI, global, pattern, storage-busy, and stale-warning behavior.

New helpers:

- `menu_startSoundApply(...)`
- `menu_finishSoundApply()`
- `menu_tickSoundApply()`

Before audio starts (`audioCodec_renderCount == 0`), `menu_startSoundApply()` preserves old synchronous behavior. After audio has started, it starts the preset chunked apply and returns. `menu_pollPresetStatus()` now ticks sound apply before global apply, giving the main loop another `audio_check_and_render()` opportunity between each unit of work.

Converted operations:

- `PRESET_OP_KIT_LOAD`: normalize mod targets, chunk sound apply, update active-voice gap index, repaint after completion.
- `PRESET_OP_ALL_LOAD`: normalize mod targets, chunk sound apply, then start existing chunked global apply, request pattern params, clear storage busy, and preserve delayed stale `.all` warning behavior.
- `PRESET_OP_PERFORMANCE_LOAD`: normalize mod targets, chunk sound apply, then apply BPM/bar-reset globals, request pattern params, clear storage busy, reset save UI, and repaint.

Unchanged:

- `PRESET_OP_MORPH_LOAD` still calls `preset_morph()` directly because morph is already rate-limited by `preset_morphTick()`.
- `AUDIO_DMA_FRAMES` remains 96.
- `OUTPUT_DMA_SIZE` remains 32.

### Verification

Commands run:

```sh
make clean
make
make img
git diff --check -- Core/Preset/presetManager.c Core/Preset/presetManager.h Core/Menu/menu.c BURST_REDUCTION_AUDIT.md
```

Result:

- Firmware build passed.
- Image packaging passed: `build/LXRV2_lxr02.img` written successfully.
- Diff whitespace check passed.
- Existing warnings remain from asyncfatfs unused parameter, USB packed-pointer attributes, newlib syscall stubs, and LTO serial compilation. No new warning was introduced by this work.

### Working Tree Notes

Focused files changed by this session:

- `Core/Menu/menu.c`
- `Core/Preset/presetManager.c`
- `Core/Preset/presetManager.h`
- `BURST_REDUCTION_AUDIT.md`
- `build/LXRV2_lxr02.img`
- `knowledge_files/log_archive/000_SESSION_INDEX.md`
- `knowledge_files/log_archive/027_SESSION_HANDOFF_LOG.md`
- `MEMORY.md`

There were pre-existing unrelated worktree changes visible outside the focused status, including `BURST_REDUCTION.md` modified and many `knowledge_files/LXR-master` deletions. Those were not touched.

## End of Session Block

```
DATE: 2026-07-04
SESSION GOAL: Reduce operation burst in the main loop to lower DSP pressure, using BURST_REDUCTION.md as a suspicious guideline.
COMPLETED: Wrote BURST_REDUCTION_AUDIT.md; implemented chunked runtime sound/mod-target apply for kit/all/performance load completion; left AUDIO_DMA_FRAMES at 96; built and packaged LXRV2_lxr02.img; updated index, handoff log, and MEMORY.
VERIFIED ON HARDWARE: Partially. User reported no obvious regression, but no direct instrumentation/hardware proof of burst reduction.

CHANGES THIS SESSION:
- BURST_REDUCTION_AUDIT.md: source-grounded audit, implementation plan, and build notes.
- Core/Preset/presetManager.c/h: one-voice drumset apply helper plus preset_startDrumsetApply()/preset_tickDrumsetApply().
- Core/Menu/menu.c: menu sound-apply scheduler and kit/all/performance completion conversion.
- build/LXRV2_lxr02.img: regenerated firmware image.
- knowledge_files/log_archive/000_SESSION_INDEX.md: Session 027 index entry and key fact.
- knowledge_files/log_archive/027_SESSION_HANDOFF_LOG.md: verbose session log.
- MEMORY.md: session/context updates.

KNOWN ISSUES INTRODUCED: None known.
KNOWN ISSUES RESOLVED: Runtime kit/all/performance load completion no longer applies all six modulation-routing voices in one foreground burst.

NEXT SESSION RECOMMENDED GOAL: Hardware-test kit/all/performance load completion during active playback, watching for underruns/glitches and verifying loaded mod targets/UI/global/pattern behavior.
BLOCKERS: No direct burst instrumentation yet; add temporary DWT timing/counters if audible testing is inconclusive.

CRITICAL REMINDERS FOR NEXT SESSION:
- Keep AUDIO_DMA_FRAMES at 96 until the chunked apply path is confirmed stable.
- Runtime load completion should use menu_startSoundApply() / preset_tickDrumsetApply().
- Boot-time pre-audio apply intentionally remains synchronous.
- preset_morphTick() is still the rate-limited morph worker.
```
