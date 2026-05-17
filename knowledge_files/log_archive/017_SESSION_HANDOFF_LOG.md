# Session 017 Handoff Log

DATE: 2026-05-11

SESSION GOAL: Complete canonical load/save types, reshape the SD filesystem layer, restore original LXR DSP/system timebase behavior where the port had drifted, add a Global CPU-use widget, and clean up the remaining small UI/LED rough edges before Session 018.

COMPLETED: Full typed save/load support is implemented for Kit, MorphKit, Pattern, Performance, All, and Globals. `Core/Hardware/SD/` is reorganized behind a documented `filesystem.c/h` facade. The obsolete ChaN FatFS and SD test leftovers are gone from the active tree. The DSP logical block size is back to the canonical 16 frames while the F765 hardware DMA half remains 96 frames. `systick_ticks` is back to the original 4kHz mainboard clock and TIM6 remains the 1kHz front-panel/service tick. A DWT-based Global CPU-use widget was added. Boot splash, screensaver LCD on/off, load-page races, and PERF/VOICE SELECT LED behavior were polished.

VERIFIED ON HARDWARE: Partially. User hardware testing confirmed Phase 1, Phase 2, pattern load, performance/all load after fixes, the 16-frame DSP sub-slot timebase, the 4kHz system tick split, and the CPU-use widget as broadly good. Latest small PERF queued-pattern LED correction was build/sniff-tested in code but not separately called out as hardware-verified before wrap.

CHANGES THIS SESSION:
- `Core/Hardware/SD/filesystem.c`: New storage facade and async operation owner; filetype registry; add-a-filetype comments; typed filename/name loading; kit/morph load/save; pattern/performance/all/global serializers; bounded streaming operations; missing-file handling; diagnostics; asyncfatfs pump.
- `Core/Hardware/SD/filesystem.h`: Public storage boundary for non-SD clients; hides asyncfatfs, raw SPI, and operation phases.
- `Core/Hardware/SD/SPI/*`: Bit-bang SD transport moved under `SPI/`.
- `Core/Hardware/SD/asyncfatfs/*`: asyncfatfs library and LXR block-device shim moved under `asyncfatfs/`.
- `Core/Hardware/SD/kitBrowser.c/h`: Left intentionally kit-only; now uses the filesystem facade instead of lower-level SD internals.
- `Core/Preset/presetManager.c/h`: Added typed preset operations for pattern/all/performance and separate morph-kit semantics; maps menu requests to filesystem types; preserves post-load DSP/mod-target work.
- `Core/Menu/menu.c/h`: Wired all implemented load/save types; added canonical load-busy UI; fixed empty-slot no-op behavior; fixed typed async-name races and LCD repaint queue pressure; added CPU-use runtime widget service; added runtime pseudo-parameter ID.
- `Core/Menu/menuPages.h` and `Core/Menu/MenuText.h`: Replaced the final displayed Global trigger params with read-only `cpu` / `CPU use time` text.
- `Core/Sequencer/sequencer.c/h`: Added active-pattern reload arming so running pattern loads stage safely and swap at the sequencer boundary.
- `config.h`: Restored `TIMEBASE_HZ`/`SYSTICK_HZ` to 4000, added `FRONTPANEL_TICK_HZ`/conversion helpers, split `OUTPUT_DMA_SIZE=16` from `AUDIO_DMA_FRAMES=96`.
- `main.c`: Renders each 96-frame hardware slot as six canonical 16-frame DSP blocks; calls runtime widget service; boot splash path updated during cleanup.
- `Core/Hardware/AudioCodecManager.c/h`: Uses `AUDIO_DMA_FRAMES` for hardware buffers; queue now tracks `ready_count`; DWT cycle-counter queue-free accounting and `audioCodec_getQueueFreePercent()` added.
- `Core/Hardware/timebase.c/h`: SysTick owns 4kHz `systick_ticks`; TIM6 owns 1kHz `time_sysTick`; LCD init millisecond delay is divided from SysTick.
- `Core/Sequencer/sequencer.c`, `Core/Sequencer/clockSync.c`, `Core/MIDI/MidiParser.c`, `Core/Hardware/triggerJacks.h`: Sequencer/BPM, MIDI clock measurement, MTC timeout, and trigger pulse constants scaled/restored for 4kHz `systick_ticks`.
- `Core/Hardware/frontPanel/lcd.c/h`: Added explicit `lcd_turnOff()` and `lcd_turnOn()`.
- `Core/Menu/screensaver.c`: Uses explicit LCD off/on phases and clears on exit before menu repaint.
- `Core/Hardware/frontPanel/buttonHandler.c` and `Core/Hardware/frontPanel/ledHandler.c`: PERF pattern LEDs now keep the currently playing pattern solid while the queued pattern blinks; VOICE switching restores the SELECT LED to the current voice subpage.
- `AUDIT_SAVE_LOAD.md`, `AUDIT_DSP_TIMEBASE.md`, `AUDIT_SYSTEM_TIMEBASE.md`, `AUDIT_CPU_USE_WIDGET.md`: Working audits created/updated and rolled into this handoff.

KNOWN ISSUES INTRODUCED: None known. The CPU-use widget is intentionally an audio queue-free pressure meter, not generic MCU utilization. It includes main-loop scheduling delays by design.

KNOWN ISSUES RESOLVED: Pattern/performance/all load/save stubs; incorrect MorphKit load/save path; `.snd` save length mismatch; short-kit stale tail bytes; typed-name always reading `.snd`; load-page empty-slot lock; number/name desync on fast spins; LCD half-pair corruption under queue pressure; kits loading on page entry; slow envelopes from 96-frame DSP control blocks; 1kHz `systick_ticks` drift from reference; screensaver display glitches; stale Global trigger display slots; PERF/VOICE SELECT LED repaint errors.

NEXT SESSION RECOMMENDED GOAL: Session 018 - Sample loading. Design and implement the sample load path carefully around flash sector 6-11 erase/write safety and audio interruption policy.

BLOCKERS: Sample loading still needs a flash-write design. `SampleMemory.c` is still a safe no-op stub; sector 6 is the erase floor. Decide whether sample upload halts audio like the original mainboard path or uses a staged/background design with explicit cache/IRQ policy.

CRITICAL REMINDERS FOR NEXT SESSION:
- `EXTI_IMR = 0` must remain the first operation in `main()`.
- Non-SD client code should include `filesystem.h`, not `asyncfatfs.h`, `spi_sd.h`, or `sd_routines.h`.
- `afatfs_poll()` / `filesystem_tick()` must run from one context only.
- `OUTPUT_DMA_SIZE` is the canonical 16-frame DSP/control block. `AUDIO_DMA_FRAMES` is the 96-frame hardware DMA half.
- `systick_ticks` is a 4kHz / 0.25ms original-mainboard tick. Use `time_sysTick` for UI/front-panel millisecond timing.
- Do not enable the internal DAC on PA4/PA5.
- Do not use blocking SD/file/flash work in the main loop while audio is running unless the operation explicitly stops audio first.
- Do not erase flash below sector 6.
- Do not re-add ChaN FatFS or `sdTest` to the active build.

## Audit Rollup - Save / Load

Primary current files audited and modified were `Core/Preset/presetManager.c/h`, `Core/Hardware/SD/filesystem.c/h`, `Core/Menu/menu.c/h`, `Core/MIDI/frontPanelParser.c`, and `Core/Sequencer/sequencer.c/h`. Reference material came from `knowledge_files/LXR-master/front/LxrAvr/Preset/presetManager.c`, `knowledge_files/LXR-master/front/LxrAvr/Menu/menu.c`, `knowledge_files/LXR-master/mainboard/LxrStm32/src/MIDI/frontPanelParser.c`, and `knowledge_files/LXR-master/mainboard/LxrStm32/src/Sequencer/sequencer.h`.

Reference behavior:
- Original AVR owns preset SD files. STM32 mainboard owns sequencer data. Pattern save/load used a pseudo-sysex bridge between AVR and STM32, but the single-MCU port can serialize `seq_patternSet` directly as long as file bytes match reference.
- Numbered extensions are `pNNN.snd`, `pNNN.pat`, `pNNN.all`, and `pNNN.prf`. Globals are `glo.cfg`.
- `.snd` contains an 8-byte name plus `END_OF_SOUND_PARAMETERS` sound bytes. Canonical save length in this port is now 236 bytes total. Short kit files remain loadable and missing sound bytes are zero-filled.
- Morph load reads sound bytes into `parameters2[]`, not `parameter_values[]`. Morph save writes interpolated morph values except velocity/LFO mod-target fields, which are saved from the base values.
- `.pat` contains 8-byte name, all `Step` records, main-step masks, pattern next/repeat pairs, shuffle byte, and pattern/track length bytes.
- Current constants make the pattern payload after name 50361 bytes: step data 50176, main steps 112, pattern settings 16, shuffle 1, lengths 56. Total `.pat` with name is 50369 bytes.
- `.all` and `.prf` contain name[8], version byte 2, 64-byte global/performance area, 512-byte kit area, then pattern payload without a second `.pat` name header. `.all` stores globals plus `0xFF` padding. `.prf` stores BPM and bar-reset mode plus `0xFF` padding.

Original audit findings and closure:
- Menu exposed Pattern, MorphKit, Performance, All, Settings, and Samples, but only Kit/Morph/Settings had backend calls. Pattern/performance/all are now wired; Samples remain explicitly out of scope for Session 018.
- `preset_loadPattern()`, `preset_savePattern()`, `preset_saveAll()`, and `preset_loadAll()` were stubs. They are now backed by async filesystem operations.
- The old `sd_fsm` only knew kit, globals, scan, and name. It was replaced/absorbed by the `filesystem.c` facade with typed operations.
- MorphKit ignored the `isMorph` argument. It now has distinct load/save semantics.
- Name loading always used `.snd`. It is now type-aware for `.snd`, `.pat`, `.prf`, `.all`, and `glo.cfg` where applicable.
- Short kit load stopped at EOF without clearing tail bytes. Missing tail bytes now clear to zero.
- Kit save was 229 bytes from the Session 13 compatibility choice. It is back to canonical 236 bytes, while the loader still accepts short files.
- The current local parser does not implement reference sysex. This remains acceptable because direct sequencer serialization preserves the file format without recreating the two-chip transport.
- Direct sequencer structures are available and used: `seq_subStepPattern`, `seq_mainSteps`, `seq_patternSettings`, `seq_patternLengthRotate`, `seq_tmpPattern`, `seq_activePattern`, `seq_newPatternAvailable`, and `seq_isRunning()`.

Implemented phase details:
- Phase 1 type plumbing: filetype registry, typed filename helper, typed name loading, extended preset op enum, and menu dispatch for implemented non-sample types.
- Phase 2 kit/morph semantics: normal kit load writes `parameter_values[]`; morph load writes `parameters2[]`; normal kit save writes active kit; morph save writes interpolated kit with mod-target exceptions; short `.snd` tail zeroing is enabled; active-kit completion still validates mod target indices, sends targets and morphed sound params, updates `menu_TargetVoiceGapIndex`, and repaints. Morph completion repaints and can refresh audible morph at the current morph position without overwriting base kit parameters.
- Phase 3 pattern serializer: direct bounded streaming serializer writes name, `Step` records in track/pattern/step reference order, little-endian main-step masks, nextPattern/changeBar pairs, shuffle, and optional length bytes. Load validates name, reads the same blocks, defaults missing old-file length bytes to zero, calls `seq_setShuffle(value / 127.0f)`, and stages running active-pattern loads through `seq_tmpPattern` before `seq_armActivePatternReload()`.
- Phase 4 `.all`/`.prf`: async containers write/read 64-byte meta, 512-byte kit block, and pattern payload. `.all` applies kit, globals, and pattern state. `.prf` applies kit, BPM/bar-reset, and pattern state while leaving other globals unchanged. Version-1 `.prf` compatibility for missing bar-reset/padding is preserved.
- Phase 5 FSM shape: large files are not staged wholesale. The implementation uses small reusable buffers for step bytes, main masks, and padding while each `filesystem_tick()` does bounded work.
- Phase 6 verification targets were build, active kit save/load, short `.snd` zero-fill, MorphKit load into `parameters2[]`, MorphKit save at morph 0/127/255, pattern roundtrip, all roundtrip, performance roundtrip, and globals-not-in-performance preservation.

Menu/UI details:
- Pattern, performance, and all loads paint `Loading pattern` on the top row while async storage runs.
- Page changes are ignored while that storage-busy message is active; sequencer playback is not stopped.
- On completion, the load screen repaints and `menu_resetSaveParameters()` returns the cursor to the file-number field.
- OK on displayed `Empty` for pattern/performance/all is a no-op and does not start the storage-busy screen.
- Failed async opens clear the lock and return the cursor to the file number.
- Async name/load completions are tagged with requested type and slot; stale completions from fast encoder spins are ignored and the final visible slot is requested again.
- `filesystem_loadName()` stages into a loaded-name buffer; the menu copies to `preset_currentName` only after confirming the completion matches the visible type/slot.
- Full boot-time name caching was considered but deferred. The keyed async request path is the smaller fix.
- The final fast-spin corruption was LCD queue pressure: dropped set-cursor/data half-pairs could desynchronize the LCD address. Menu repaints now preflight `lcd_queueFree()` and either enqueue the whole frame or skip/retry without updating `currentDisplayBuffer`.
- Entering load/save via buttons now goes through the same keyed selection request helper as encoder changes.
- Kit and MorphKit load browsing only requests names on page entry/type change; turning the file-number encoder performs the canonical immediate kit/morph load.
- Failed async operations notify the menu so the storage-busy screen can unlock.

Secondary SD objective:
- Target layout is implemented:
  - `Core/Hardware/SD/filesystem.c/h`
  - `Core/Hardware/SD/kitBrowser.c/h`
  - `Core/Hardware/SD/SPI/spi_sd.c/h`
  - `Core/Hardware/SD/SPI/sd_routines.c/h`
  - `Core/Hardware/SD/asyncfatfs/asyncfatfs.c/h`
  - `Core/Hardware/SD/asyncfatfs/fat_standard.c/h`
  - `Core/Hardware/SD/asyncfatfs/sdcard.h`
  - `Core/Hardware/SD/asyncfatfs/sdcard_lxr02.c/h`
- Removed/recommended obsolete active-tree files: `ff.c`, `ff.h`, `ffconf.h`, `diskio.c`, `diskio.h`, `integer.h`, `sdTest.c`, and `sdTest.h`.
- `filesystem.h` is the only header non-SD clients should include. It hides asyncfatfs types, `sdcard_lxr02` state, raw SPI commands, filename construction, and internal phases.
- Public API shape is the `fs_file_type_t` / `fs_status_t` / request model: init/mount, tick, status, ack, requestLoad, requestSave, requestLoadName, requestScanKits, loadedName, and diagnostics.
- Filetype registry documentation at the top of `filesystem.c` says to add an enum, descriptor row, load/save phases or serializer route, completion op mapping, menu dispatch only after backend support exists, and verification for save/load/name/missing/busy cases.
- `kitBrowser.c/h` intentionally remains kit-only. If a future generalized scanner is needed, make a new `fileCatalog`/`presetCatalog` style module rather than broadening kitBrowser.

Storage constraints:
- `afatfs_poll()` must be called from one context only.
- `filesystem_tick()` remains the single post-boot storage pump until a future TIM5 migration.
- Raw SD init may remain blocking at boot before audio starts. Post-boot file work must remain async.
- `sdcard_lxr02.c` must use its keep-CS command path for block transfers, not `SD_sendCommand()`.
- Do not expose `asyncfatfs.h` through `filesystem.h`.
- Do not put menu save-type enums into the filesystem layer; map them in preset/menu glue.

## Audit Rollup - DSP Timebase

Reported symptom: envelopes, LFOs, and related modulation felt slower than original LXR.

Root cause: not primarily the original 4kHz `systick_ticks`; the direct audible slowdown came from `OUTPUT_DMA_SIZE` being changed from the reference 16-frame logical DSP/control block to the F765 96-frame hardware DMA half. Original LXR runs `SysTick_Config(... / 4000)` and has a 16-frame mixer block in DMA mode. At about 44.1kHz, that gives a DSP control/update rate around 2750Hz. The port's pre-fix 96-frame block gave about 459Hz, making block-rate envelopes and snap/pitch modulation about 6x slower.

Envelope audit:
- `Decay.c`, `SlopeEg2.c`, and `snapEg.c` formulas matched reference.
- Mixer async voice updates call EG functions once per mixer block.
- A 96-frame block advances EGs every 2.18ms instead of every 0.36ms.
- Oscillators, sample playback, filters, decimation, and output mixing still loop per sample and were not obviously slowed the same way.

LFO audit:
- LFO dispatch is once per mixer block.
- `LFO_SR` is `REAL_FS / OUTPUT_DMA_SIZE`; unsynced LFO frequency is scaled by dispatch rate and should not be 6x slow purely from the wider block, but modulation target updates were much coarser at 459Hz.
- Separate remaining LFO bug: synced LFO BPM is still hardcoded to `130` where the reference uses `seq_getBpm()`. Fixing synced LFO tempo should remain on the DSP cleanup list.

Implementation:
- `OUTPUT_DMA_SIZE` is now the canonical 16-frame DSP/control block.
- `AUDIO_DMA_FRAMES` is the 96-frame hardware DMA half.
- `main.c` fills each hardware render slot by calling `mixer_calcNextSampleBlock()` six times at 16-frame offsets.
- `AudioCodecManager.c/h` sizes DMA/render buffers with `AUDIO_DMA_FRAMES`.
- The audio ready queue tracks `ready_count`, so both render slots can actually be queued.
- This avoids coefficient hacks. Envelope shapes, filter/modulation cadence, and LFO control smoothness now follow the reference control quantum.

Validation targets:
- Known short drum amp envelope should match original feel.
- Pitch EG/snap transient should no longer feel stretched.
- Fast unsynced LFOs should have correct frequency and smoother target updates.
- Synced LFO should later be restored to actual sequencer BPM instead of fixed 130 BPM.
- Audio underrun count should remain stable with the higher logical mixer call rate.

## Audit Rollup - System Timebase

The port now separates the original LXR mainboard clock from the merged front-panel/service clock:
- `systick_ticks` is the canonical 4kHz / 0.25ms mainboard tick from `SysTick_Handler()`.
- `time_sysTick` is the 1kHz / 1ms front-panel/service tick from TIM6.
- `lcd_ms_ticks` is divided down from the 4kHz SysTick so blocking LCD init delays still run in milliseconds before TIM7 starts.
- TIM6 still owns LED/button SPI exchange, encoder button debounce, endless pot sampling, trigger-jack input sampling, and `screensaver_timer`.

Changes:
- `config.h`: `TIMEBASE_HZ` / `SYSTICK_HZ` set to 4000; `FRONTPANEL_TICK_HZ` and `SYSTICK_TICKS_PER_MS` added.
- `Core/Hardware/timebase.c/h`: SysTick increments `systick_ticks` at 4kHz and divides LCD milliseconds; TIM6 no longer increments `systick_ticks`.
- `Core/Sequencer/sequencer.c`: BPM delta and shuffle resync math multiply millisecond durations by `SYSTICK_TICKS_PER_MS`.
- `Core/Sequencer/clockSync.c`: MIDI clock BPM measurement converts quarter-ms pulse intervals back to milliseconds.
- `Core/MIDI/MidiParser.c`: MTC timeout is `100 * SYSTICK_TICKS_PER_MS`.
- `Core/Hardware/triggerJacks.h`: Trigger pulse length is `PULSE_LENGTH_MS * SYSTICK_TICKS_PER_MS`.
- `main.c`: Knob repaint rate limiting uses `time_sysTick` so the UI throttle remains 20ms.
- `Core/Hardware/memtest.c/h`: Boot memtest blocking delays use `time_sysTick` milliseconds.

Sniff test:
- `systick_ticks` call sites are now mainboard-timing code: sequencer internal clock/shuffle, MIDI clock input BPM, MTC bookkeeping/timeout, trigger constants, and optional/commented audio diagnostics.
- `time_sysTick` remains front-panel or human-scale timing: LED pulse/blink, button long press, encoder acceleration, screensaver, knob repaint throttle, and memtest display delays.

Remaining items:
- Documentation drift was addressed in this wrap-up pass, but future historical notes may still mention the temporary 1kHz sequencer rebase.
- New code must not assume `systick_ticks` is milliseconds. Use `time_sysTick` for UI/front-panel millisecond timeouts and `SYSTICK_TICKS_PER_MS` for explicit conversions.
- Trigger backend remains mostly stubbed; audit timing again when CLK OUT, trigger pulse, gate mode, and external clock input are implemented.
- SysTick now runs 4x more often, but the handler is intentionally tiny. TIM6 did not get sped up, so the heavy front-panel SPI/ADC work remains 1kHz.
- `time_sysTick` is 16-bit and wraps about every 65 seconds. `screensaver_timer` still has a nominal two-minute timeout on a 16-bit counter; fix with a 32-bit counter or seconds divider when revisiting screensaver timing.
- Future human-scale timers longer than about 60 seconds should not use raw 16-bit `time_sysTick` deltas.

Best practice:
- `systick_ticks`: original mainboard/sequencer/trigger/MIDI timing.
- `time_sysTick`: merged front-panel/service/UI timing.
- `lcd_ms_ticks`: LCD init-only millisecond delay counter.
- Preserve original quarter-ms math when porting reference mainboard code. Use named millisecond timing for new UI/storage code.

## Audit Rollup - CPU Use Widget

Goal: add a read-only Global widget showing the fraction of time the audio DMA ready queue has at least one free slot. In code, this means `ready_count < 2` in `AudioCodecManager.c`. Queue full means audio is caught up; at least one free slot means the main loop has audio work available or is late getting to it. This is an audio refill pressure meter, not generic MCU utilization.

Implementation facts:
- Main loop renders audio when `audioCodec_queueFreeSlots() > 0`.
- `AudioCodecManager.c` owns a two-slot SPSC queue.
- Main loop commits rendered slots in `audioCodec_commitRenderBuffer()`.
- DMA1 Stream 4 consumes slots in `pack_audio_half()`.
- `ready_count == 2` means queue full/caught up.
- `ready_count < 2` means at least one slot available.
- `AUDIO_DMA_FRAMES` is 96 frames, about 2.18ms per queued slot.
- SysTick is 4kHz mainboard tick, TIM6 is 1kHz front-panel tick, TIM7 is a 10kHz LCD drain that can idle.

Measurement decision:
- Do not use the 1ms tick; it aliases badly against the 2.18ms DMA cadence.
- Do not draft TIM7; it is display-owned, can idle, and coupling audio diagnostics to the LCD ISR would make bugs harder to reason about.
- A dedicated 10kHz timer would be workable but still sampled and adds another ISR.
- Chosen approach: Cortex-M7 DWT cycle counter with event-based accounting at queue state changes.

Implemented shape:
- DWT CYCCNT is enabled in `audioCodec_init()`.
- `last_cycle`, total window cycles, and free-window cycles are tracked.
- Queue state is accounted before any possible `ready_count` change in `audioCodec_commitRenderBuffer()` and `pack_audio_half()`, including underrun paths.
- `audioCodec_getQueueFreePercent()` snapshots/resets the window with brief interrupt masking and performs expensive division in main context.
- Unsigned cycle subtraction is safe across DWT wrap because accounting events occur much more frequently than the roughly 20-second wrap period at 216MHz.

Menu widget:
- Added pseudo parameter `PAR_RUNTIME_CPU_USE = 0xFFFEu`, outside `NUM_PARAMS`, so globals and ALL file serialization are not changed.
- Global menu no longer displays `Trigger Out2 PPQ` and `Trigger Gate Mode` in the last two displayed positions.
- First slot on the fourth Global page shows short name `cpu`, long name `CPU use time`.
- Click-in parameter view shows `CPU use time` and bottom-right `NNN%`.
- Encoder turn is ignored; click exits normally.
- `menu_serviceRuntimeWidgets()` samples every 0.5 seconds, keeps a 10-sample rolling average, and repaints only if the widget is visible and screensaver is inactive.

Risks/notes:
- The meter intentionally includes main-loop scheduling delay from SD, menu, USB, LED, or other foreground work. That is useful because it reflects real audio refill headroom.
- A future TIM5 10kHz asyncfatfs pump should remain separate.

## UI / LED Cleanup Notes

Boot:
- Desired splash after LCD/TIM7 init is exactly:

```text
Sonic Potions
LXR Drums V0.37
```

- Menu should finish on the first page of the first voice (`COA fin wav`), not on the first parameter edit display. User fixed final menu/LCD ordering during the session.

Screensaver:
- `lcd_turnOff()` and `lcd_turnOn()` were added.
- Screensaver turns LCD off when it enters and during hidden phases, turns it on when a character is visible, and clears on exit before menu repaint.

PERF LEDs:
- Canonical behavior is solid LED for the currently playing pattern and blinking LED for the next queued/viewed pattern.
- Do not clear the current solid SELECT LED when queuing a new pattern. Clear/repaint on sequencer ACK via `led_initPerformanceLeds()`.

VOICE LEDs:
- When switching voices, the LCD retains the current voice subpage. SELECT LED must match that subpage via `menu_getSubPage()`, not reset to SELECT 1.

## End State For Session 018

Start Session 018 by reading `MEMORY.md`, `README.md`, this handoff, and the session index. The highest-value next task is sample loading. Treat flash erase/write as high risk:
- Sector 6 is the sample erase floor.
- `SampleMemory.c` is currently a safe no-op stub.
- F765 flash erase/write will need IRQ/audio policy, D-cache/ICache considerations, and hard address validation.
- Original LXR mainboard sample upload halted audio; decide whether to preserve that behavior or design a more ambitious background writer.
