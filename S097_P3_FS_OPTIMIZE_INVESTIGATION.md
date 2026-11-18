# S097 P3 — `filesystem.c` Size Optimisation: Investigation Log

Date: 2026-11-17. Branch `dev-ph7-alkaline`, on top of `04c5c004` (S097 P2
committed: `FS_OS=0`, 714,980 B, device-tested).

**Goal:** recover part of the −36 KB that `filesystem.c` at `-Os` (`FS_OS=1`)
gives, without the sequencer stumble seen on the device during a Scene Load into
non-playing slots (S097 P2 implementation log §10–§11).

**Plan (from P2 §11):**

1. Find the long pass. A DEV-only trace records the longest main-loop and
   filesystem passes, and each audio underrun, tagged with the filesystem
   operation and phase.
2. Then one of:
   - (a) keep only those phases' functions at `-O2` in the `FS_OS=1` build;
   - (b) bound that phase by time so it is safe at any optimisation level;
   - (c) `-Os` only the code that cannot run while playing.

**Sign-off (user, 2026-11-17):** "Start working on" the plan above, which
named the DEV RAM and the `main.c`/LTO concern. The probe uses **20 B of DEV
RAM** (SRAM1 `.bss`, owner `filesystem.c`, `DEV_MODE_LOGGING` builds only). It
lives entirely in `filesystem.c`, not `main.c`: `filesystem.c` is in
`COLD_LAST`, so it cannot move the DSP partitions.

## Status

| Step | State |
|------|-------|
| 1. Probe in firmware, decoder, gate | **done** (§1–§3) |
| 1. Device run, baseline (`FS_OS=0` + probe) | **done** (§7): the stumble and the OK problems are in the baseline |
| **Baseline fixes** (user: "needs to be fixed on the baseline first") | OK presses **confirmed on the device** (§9.1); timing drift after a Load into the playing Scene **fixed, waiting for the device** (§9.2) |
| 2(c) boot/install-only code at `-Os` | **measured, not applied**: −2,752 B (§5) |
| Baseline fixes on the device | **confirmed** by the user (§10): "the last test fixed the stumble and other scene load glitches" |
| `FS_OS=1` retest with the fixes | first run: OK needed several presses again (§11) |
| Held OK replay (§11) | **confirmed on the device** (§12, user: "seems ok") |
| `FS_OS=1` with all fixes | **device run clean** (§12); still off by default, waiting for the user's decision |
| Bank replace Save kept old children (B3) | **fixed, waiting for the device** (§13) |
| "AutoSave bank load" at boot; notice text; budget 3 % | **analysed, fixed, waiting for the device** (§14) |
| Load provenance at power-off (user rule) | **checked** (§15): Scene/Pattern Loads hold and are not regressed; Kit/Instrument Loads commit on exit, **as intended** (§15.6) |
| 2(a)/(b) | only if the `FS_OS=1` retest still shows a problem |

---

## 1. The probe

**Where.** `filesystem.c` only. The public `filesystem_tick()` becomes a
wrapper under `DEV_MODE_LOGGING`:

```c
void filesystem_tick(void)
{
    const uint32_t start_us = timebase_tim2Now();
    const uint8_t op = (uint8_t)current_op;
    const uint8_t phase = op_phase;

    filesystem_passProbeGap(start_us, op, phase);
    filesystem_tickBody();                     /* the former filesystem_tick() */
    filesystem_passProbe(start_us, op, phase);
}
```

With logging off, the body keeps the name `filesystem_tick()` and nothing is
added. Timing uses the free-running TIM2 µs counter
(`timebase_tim2Now()`/`timebase_tim2Delta()`).

**What it records** (new trace stage `'f'`, `AUTOSAVE_TRACE_STAGE_FS_PASS`,
documented in `AutosaveTrace.h`). value32 = µs or count (bits 0–15, saturated)
| `fs_internal_op_t` (16–23) | `op_phase` (24–31). The operation and phase are
the ones the call *entered* with, i.e. the phase that ran.

| flags | Record | When |
|------:|--------|------|
| 0 | slow pass | one `filesystem_tick()` call ≥ 1,000 µs (`FS_PASS_PROBE_SLOW_US`) |
| 1 | operation maximum | when the entered operation changes: the previous operation's longest call and its phase |
| 2 | underrun | every rise of `audioCodec_underrunCount`, with the increase |
| 3 | main-loop gap | once per second: the longest gap between two `filesystem_tick()` starts (one whole main-loop pass, render included) |

**Rules:**

- Nothing is recorded before audio starts (`audioCodec_isInitialized()`); boot
  work is blocking by design.
- Nothing is recorded for `FS_INTERNAL_OP_AUTOSAVE_TRACE_FLUSH`,
  `…_PATTERN_TRACE_FLUSH` or `…_WRITE_BOOT_LOG`, including the per-second gap
  record, which waits for the next other call. A record can never start the
  next trace flush (S090 P5 SD_CARD_TEST_6 boot freeze rule).
- Underruns are recorded in any operation. They are not caused by the flush,
  so they cannot make it reschedule itself.
- Cost: two timer reads and a few compares per main-loop pass; at most one
  `'f'` record per second in steady state (the gap record).

**RAM (DEV only, `.bss`):** `fs_probe_last_entry_us` 4, `fs_probe_gap_window_us`
4, `fs_probe_underruns` 4, `fs_probe_gap_max_us` 2, `fs_probe_max_us` 2,
`fs_probe_op`, `fs_probe_max_phase`, `fs_probe_gap_op`, `fs_probe_gap_phase` 1
each: 20 B (checked with `nm`).

**Decoder:** `tools/diagnostic_tools/decode_devlogs.py` gains the `'f'` branch.
Checked on synthetic records of every flag (operation names, saturation `+`).
Its `FS_INTERNAL_OPS` list was checked against the `fs_internal_op_t` enum:
54 entries, identical order.

## 2. Gate (both images, against `04c5c004`)

| Build | Flash | Δ vs P2 | Loops | Hot calls | ISR/render bodies |
|-------|------:|--------:|-------|-----------|-------------------|
| `FS_OS=0` + probe (`build/`) | 715,524 | +544 | 86/86 exact; 77 osc + 58 mixer hot loops equal | same | **all 21 identical** |
| `FS_OS=1` + probe (`build/fsos/`) | 679,044 | −35,936 | 86/86 exact; all hot loops equal | same | `din_dout_exchange` data offsets only (as in P2 §3) |

The 21 bodies checked: every `*_Handler`/`*_IRQHandler`,
`audio_check_and_render()`, the mixer, `voiceControl_processPending()`,
`seq_rampService()`, `din_dout_exchange()`, `endlessPots_tick()` and
`encode_read4()`. The LTO isolation from P2 holds: a change inside
`filesystem.c` does not touch the audio code.

## 3. Files changed

| File | Change |
|------|--------|
| `Core/Hardware/SD/filesystem.c` | probe (§1) under `DEV_MODE_LOGGING`; `filesystem_tick()` wrapper and `filesystem_tickBody()` |
| `Core/Bank/Scene/AutosaveTrace.h` | stage `'f'` `AUTOSAVE_TRACE_STAGE_FS_PASS` with its layout |
| `tools/diagnostic_tools/decode_devlogs.py` | `'f'` stage name, producer and decode branch |
| `build/LXRV2_lxr02.img` | `FS_OS=0` + probe (715,524 B) |
| `build/fsos/LXRV2_lxr02.img` | `FS_OS=1` + probe (679,044 B; generated, untracked, removed by `make clean`) |

`FS_OS` stays 0 by default. The `fsos` image was built with
`make all FS_OS=1 BUILD=build/fsos && make img FS_OS=1 BUILD=build/fsos`.

## 4. Device test (user)

Use the same Scene and actions as the stumble report: the worst-case Scene
playing, a Scene Load of one Scene into non-playing slots (as 9–11 / 13–15
before).

1. **`build/fsos/LXRV2_lxr02.img`** (`FS_OS=1` + probe). Play; do the Scene Load
   two or three times (note whether it stumbles each time); also a Scene Save,
   a Bank Load, and a few edits so AutoSave drains. Power off. Copy the card to
   `SD_CARD_COL_4`.
2. **`build/LXRV2_lxr02.img`** (`FS_OS=0` + probe). The same actions. Copy the
   card to `SD_CARD_COL_5`.

**Reading it:**

- `f` records with flags 0 show every filesystem call ≥ 1 ms. The `FS_OS=1`
  run should show which operation and phase gets longer than in the
  `FS_OS=0` run.
- Flags 1 give each operation's longest call, for a phase-by-phase comparison.
- Flags 2 tie any underrun to what the filesystem was doing.
- Flags 3 show whether a whole pass got long even when `filesystem_tick()`
  did not (the long work is then elsewhere in the loop).

## 5. Option (c) measured: boot/install-only code at `-Os`

**Method.** Call graph from the ELF (as P2 §1). Roots: the calls `main()` makes
before `for (;;)`. Those reach a set of functions; subtract everything
reachable from the main-loop roots, every ISR, the render path and
`midiIn_dispatch()`. That leaves 48 `filesystem.c` functions (17,944 B linked):
the boot readers (`filesystem_bootReader*`, `…AutosaveBootReaderBlocking`),
`filesystem_settleAutosaveBlocking()` and helpers, the directory trim
(`filesystem_trim*Blocking`), boot/library index creation, the boot-log
writer, sample/wavetable install (`filesystem_install*`,
`filesystem_parseWav()`; modal, audio suspended) and the blocking SD helpers
(`filesystem_block*`).

Two were left out as not obviously boot-only (probably reached at runtime
through a path the static graph does not see):
`filesystem_requestSaveInstrumentMode()` and
`filesystem_makePatternLibraryFilename()`.

**Experiment** (scratch copy, not applied): `FS_COLD_OS`
(`__attribute__((optimize("Os")))`) on the other 46 definitions, default build:

| | Flash | Gate |
|---|------:|------|
| `FS_OS=0` + probe | 715,524 | — |
| + 46 boot/install functions at `-Os` | 712,772 (**−2,752**) | 86/86 loops exact, all hot loops equal, hot calls same, all 21 ISR/render bodies identical |

Runtime cost: none while playing. Boot, the boot readers and settle, and
`Load:[Samples]` get somewhat slower. It is safe, but small: most of the
36 KB is in runtime state machines, which is where the stumble came from.
**Not applied** until the device data decides between (a), (b) and (c).

## 6. Static notes on the Scene Load path (for when the data arrives)

`filesystem_loadSceneDirectory_tick()` (7,296 B) walks: open/chdir, read
`sceneset.scg` / `kitset` / each instrument file / `effects.fx` line by line
(`filesystem_readTextLine()` + `storage_*ParseLine()`, `storageTypes.c` at
`-O2`), the Pattern file (`filesystem_patternCrcFeed()`, region writes through
`pat_sceneRegionMut()`), then `filesystem_commitSceneStage()`. The commit copies
the staged settings, effect and Kit into each destination Scene and calls
`pat_initScene()` per destination: three destinations in the stumble case, all
in one pass. That one-pass, per-destination work, and any phase that loops over
a whole Pattern or Kit image, are the first suspects. The probe's flags-0/1
records name the phase.

---

## 7. Baseline device run (`SD_CARD_BASE`): the problems are not `-Os`

**User (2026-11-17), `FS_OS=0` + probe image (card `LXRV2_lxr02.img` 715,540 B):**
"something is fucked up in Scene load and possibly Scene save, the 'ok'
frequently does not respond, freezes after being clicked and I have to switch
to another mode, and I was able to get the sequencer to stumble in a Scene load
twice. That needs to be fixed on the baseline first."

Boot 86 (`SD_CARD_BASE/asavetrc.bin`, decoded to scratch). Five Scene Loads,
destinations {0,5,6,7}, {8,9,10,11}, {5,6,7}, {11,12,13}, {9,10,11}; one Scene
Save (slot 67).

| Probe record | Finding |
|--------------|---------|
| flags 0 (slow `filesystem_tick()`) | **none**: no filesystem call reached 1 ms |
| flags 1 (longest call per operation) | Scene Load 456–597 µs (phase 33); Scene Save 407 µs (phase 8); library index ≤ 426 µs |
| flags 2 (underrun) | **one**, just after boot (idle); **none** during any Load or Save |
| flags 3 (longest main-loop pass per second) | median 4.2 ms, p95 15 ms, max 26 ms. Not starvation: `main()` calls `audio_check_and_render()` between every task, so a long pass contains many renders |

**Conclusion:** the audio never starved, and no filesystem pass was long. The
stumble is not CPU time, and not `-Os`. Something a Load does changes what the
sequencer and voices are playing.

## 8. Baseline fixes

### 8.1 Stumble: a Load re-applied the active Scene even when it did not change

> **Correction (§9):** the user describes the stumble as *timing*: after a
> Load, track 5 of the playing Scene was off the beat until the PERF
> double-click realign. The fix below is still correct (it removes needless
> voice resets), but it was not the timing cause. §9.2 has that.

After every runtime Scene or Kit Load, `menu_pollPresetStatus()` →
`menu_startSoundApply()` → `preset_startDrumsetApply()` re-applied the
**active** Scene to the running engine, whatever Scenes the Load wrote:

- `instrumentManager_captureLfoPhases()`,
  `instrumentManager_clearAllRuntimeModulationTargets()`;
- `preset_applySceneSettings(active)`, which clears the active Scene's step
  overrides (`preset_clearAllAudioOutStepOverrides()`,
  `preset_clearAllFxSendStepOverrides()`,
  `instrumentManager_clearSlot6Track7StepDecayOverride()`);
- `effects_activateScene(active)`;
- then, voice by voice (`preset_tickDrumsetApply()`, when the amp envelope is
  quiet, forced at the next trigger or after `DRUMSET_APPLY_FORCE_TICKS`),
  `preset_resetAndApplyKitVoiceImage()` →
  `instrumentManager_resetRuntimeSlotFromScene()`.

A Load into other Scenes therefore reset the playing voices, modulation,
Effect and step automation overrides mid-pattern: the "stumble". Pattern data
was never at risk, because `pat_regions[]` is per Scene.

**Fix:**

- **`presetManager.c/.h`:** new `preset_startLoadApply(scene_mask)`. If the
  Load's destinations include the active Scene, it runs
  `preset_startDrumsetApply()` as before. Otherwise it applies nothing to the
  active Scene, and only re-arms a slot whose track plays one of the loaded
  Scenes through per-track Scene playback (S077), via the existing
  `preset_startSingleVoiceApply(slot, played)` (quiet-wait or trigger commit,
  other slots untouched).
- **`menu.c`:** `menu_soundApplyLoadMask` (default
  `MENU_SOUND_APPLY_ALL_SCENES`) is set to `preset_getKitRequestSceneMask()`
  just before `menu_startSoundApply()` in the `PRESET_OP_SCENE_LOAD` and
  `PRESET_OP_KIT_LOAD` completions. `menu_startSoundApply()` consumes and resets
  it, then calls `preset_startLoadApply()` for a mask or
  `preset_startDrumsetApply()` for the default. Every other caller (Bank,
  All/Performance, Kit Morph, copy/clear, boot) keeps the full apply.
- **Checked:** both Kit-load paths set the request mask before their filesystem
  request (`preset_loadDrumset()`: the target Scene; `preset_loadKitForScenes()`:
  the chosen mask), and `preset_loadSceneForScenes()` sets it to the destination
  mask. `on_kit_load_complete()` already uses the same mask for its AutoSave
  dirty marks.

A Load **into the active Scene** still re-applies it, because its sound
changes. The first Load of the device run had destinations {0,5,6,7}: if
Scene 0 (slot 1 on the panel) was playing, that Load replaced it and a change is
expected.

### 8.2 OK does nothing, needs several presses, or "freezes"

`menu_parseEncoder()` toggles `editModeActive` on **every** click and then
calls `menu_handleLoadSaveMenu()`. The OK action runs when the toggle has just
set it to 1 on the OK row. The handler drops the click without restoring the
flag when:

- a command is still running (`menu_loadSaveCommandActive`);
- the Load name is still blank (names stay blank while the browser reads, S090
  P1: frequent right after scrolling or a type change);
- a Bank copy is held;
- the request is **refused**: `preset_load…`/`preset_save…` returns 0 while the
  filesystem is busy, for example with a name or index read, a Bank preview
  (Bank Save OK cancels it and breaks), or, in DEV builds, the trace flush
  every 500 ms.

Each drop left `editModeActive` = 1 on the OK row, where the encoder does
nothing (the "freeze", cleared by a mode change). The next OK only toggled it
back, so a Load or Save took two or three presses.

**Fix (`menu.c`):** `menu_loadSaveDropClick(outcome)` toggles `editModeActive`
back at each of the four drop points, so the next OK acts. The Samples modal
(which starts its own command) is marked handled, not refused. New DEV trace
stage **`'k'`** (`AUTOSAVE_TRACE_STAGE_LOADSAVE_CLICK`, `AutosaveTrace.h`,
decoder branch added) records each Load/Save-page click's outcome:

| flags | Outcome |
|------:|---------|
| 0 | accepted |
| 1 | dropped: command running |
| 2 | dropped: blank name |
| 3 | dropped: copy held |
| 4 | refused |
| 5 | edit-mode toggle |

The value carries the page, type, row and filesystem status. The next device
run shows how often OK is refused and why.

**Still true after the fix:** a refused OK does nothing; the *next* OK works.
If refusals are frequent (the `'k'` records will tell), the follow-up is to hold
a refused OK and retry it when the filesystem is idle (cancelling a browse read
first, as a type turn does). That is not done yet; it changes the flow more.

### 8.3 Probe tidy-up

The flush-finish step after every trace flush produced an "operation ended"
record twice a second (§7 data). Its maximum is no longer recorded; a slow
flush-finish pass still is (flags 0).

### 8.4 Gate and build

Default build (`FS_OS=0`, DEV logging, probe + fixes): **716,072 B** (headroom
37,592 B; +1,092 B vs `04c5c004`). Against `04c5c004`: 86/86 loops exact, 77
oscillator and 58 mixer hot loops equal, hot call sets the same, all 21
ISR/render bodies identical. Host tests (`instrument_manager`, `special_tags`,
`make_init_*`) pass. The `'k'` decoder branch was checked on synthetic records.
`build/LXRV2_lxr02.img` is this image. The stale `build/fsos/` image from §2 was
removed.

**Files (in addition to §3):**

| File | Change |
|------|--------|
| `Core/Bank/Scene/Preset/presetManager.c/.h` | `preset_startLoadApply()` |
| `Core/Menu/menu.c` | `menu_soundApplyLoadMask` and its use in `menu_startSoundApply()`; Scene/Kit Load completions set it; `menu_loadSaveDropClick()`/`menu_loadSaveTraceClick()` and the four drop points |
| `Core/Bank/Scene/AutosaveTrace.h` | stage `'k'` |
| `tools/diagnostic_tools/decode_devlogs.py` | `'k'` branch |
| `Core/Hardware/SD/filesystem.c` | probe: no Flush Finish maximum |

### 8.5 Device check

With the current `build/LXRV2_lxr02.img`, on the worst-case Scene while
playing:

1. Scene Load into slots that do **not** include the playing Scene, several
   times. There should be no stumble.
2. A Scene Load **into** the playing Scene. A change of sound is expected; a
   timing stumble is not.
3. A Kit Load into a non-playing Scene. There should be no change in sound.
4. If a track plays another Scene per-track, a Load into that Scene: only that
   track's voice changes, at its next quiet point or trigger.
5. Scene Save and Scene Load OK presses, including right after scrolling: one
   press should act, or, if the name was not read yet or the card was busy,
   the *next* press. There should be no state where the encoder does nothing
   until a mode change.
6. Copy the card to `SD_CARD_BASE_2`. The `'k'` records show every OK outcome,
   and the `'f'` records show the passes again.

---

## 9. Second baseline run (`SD_CARD_BASE_2`) and the timing drift

**User (2026-11-17):** "incorrect, the timing of the playing scene was off
after loading. Specifically, the 5th voice was noticeably off and needed to be
corrected with the 'realign' (double-click the scene in perf mode)." Card:
`SD_CARD_BASE_2`, image from §8.4.

### 9.1 What the card shows

Last boot. Five Scene Loads with destinations {5,6,12,13,14}, {0}, {0},
{0,4,5,14,15} and {0,6,7,8}, plus one Scene Save. Four of the five Loads wrote
into **Scene 0**, the playing Scene. The probe saw no underrun during any Load
(one at boot, idle) and no slow filesystem pass; the longest was 738 µs
(Scene Load, phase 33).

The `'k'` records (17 presses) confirm the §8.2 fix:

| Outcome | Count |
|---------|------:|
| accepted | 6 |
| refused (card busy) | 1 (the next press, 3.6 s later, was accepted) |
| dropped: blank name, command active, copy held | 0 |
| edit-mode toggle (type/number rows) | 10 |

Every OK press either started the command or was refused and then accepted on
the next press. None left the row stuck.

### 9.2 Cause: the playing Pattern is rewritten in place, so a track's phase drifts

A Scene Load (phase 45) and a Pattern Load (phase 6) call
`pat_initScene(scene)` on the **resident** region and then read the file
straight into it. For a destination that is playing:

1. Until the header extension is read (phase 47), the region holds the Normal
   defaults: length 16, default scale and play mode.
2. `presetMorph_tick()` calls `seq_refreshTrackEffectiveParams()` every
   main-loop pass, so the TIM3 DDA picks up those defaults straight away.
3. A track with a non-default scale or length then advances at the wrong
   rate for those passes. The new Pattern can also carry a different length,
   scale or play mode.
4. Nothing repositions the track afterwards: `seq_stepIndex[]` and
   `seq_trackAccumulator[]` keep their drifted phase. Only a realign
   (`seq_realignTrackToMasterClock()`, the PERF double-click) puts it back on
   the master clock.

That fits "the 5th voice was off" (a track whose timing is not the default)
and "realign fixed it". Tracks with default timing do not visibly drift.
Pattern *content* playing partly empty during the read of the playing Scene
is a separate, expected effect of loading over it.

### 9.3 Fix

`filesystem.c`: new `filesystem_patternRealignPlayingTracks(scene_mask)`.
While the sequencer runs, it calls `seq_refreshTrackEffectiveParams()`, then
`seq_realignTrackToMasterClock(track)` for each track whose played Scene
(`seq_getTrackPlayedScene()`, per-track Scene playback included) is in the
mask. It runs where a Pattern replacement ends:

| Site | Mask |
|------|------|
| Scene Load phase 52 (after `filesystem_patternServiceFinish()`; also reached on Pattern read errors) | `op_scene_load_scene_mask` |
| Pattern Load phase 14, success (after mirroring to the other destinations) | `op_scene_load_scene_mask` |
| Pattern Load phase 14, error (region reset to empty) | `op_scene_load_scene_mask` |
| Bank Load phase 17, empty Scenes | `op_bank_scene_empty_mask` |

It is **not** called from `filesystem_finish()`. That function releases the
gate on every terminal path with `op_scene_load_scene_mask`, which can be left
over from an earlier operation, and a realign has side effects: a new
LiveRecord take, ended ramps, cleared menu override and echo marks for that
track. Tracks playing other Scenes, including a Load into non-playing slots
and runtime Bank Loads (tracks on Scene 17), are not touched. A stopped
sequencer is not touched either; start realigns.

The realign is the same per-track realign as the user's gesture. A track
that was already on the grid with unchanged timing lands where it was.

**Not covered:** a Scene Load aborted by stall detection (DEV,
`filesystem_finish(ERROR)` directly) skips the realign. The PERF double-click
recovers it.

### 9.4 Gate and build

Default build (`FS_OS=0`, DEV logging, probe + §8 + §9): **716,216 B**, +144 B
vs §8.4. No new RAM. `sequencer.c` is unchanged; only public calls from
`filesystem.c` (cold). Against `04c5c004`: 86/86 loops exact, 77 oscillator and
58 mixer hot loops equal, hot call sets the same, all 21 ISR/render bodies
identical. `build/LXRV2_lxr02.img` is this image.

### 9.5 Device check

While playing, on a Scene where a track has non-default scale or length (track 5
on the current card):

1. Scene Load into the **playing** Scene, several times, including the same
   Scene over itself. Every track, track 5 included, should stay on the beat
   afterwards without a realign.
2. Scene Load into non-playing slots. There should be no change at all.
3. Pattern Load into the playing Scene. Same as 1.
4. If a track plays another Scene per-track, load into that Scene. Only that
   track is realigned.
5. OK presses as in §8.5.
6. Copy the card to `SD_CARD_BASE_3`.

---

## 10. `FS_OS=1` retest with the baseline fixes

**User (2026-11-17):** "that's super hacky but let's leave it. The correct
method is to use the background 17th scene when loading, but we'll work on that
later. For now, the last test fixed the stumble and other scene load glitches,
give me the FS optimized image so I can test that, I assume your realign at the
end of a scene self-load is ok."

**Future work (user direction):** a Scene or Pattern Load into a playing Scene
should move playback onto the background Scene 17 for the duration of the
load, as runtime Bank Loads already do (`preset_tickBackgroundBankLoad()`
phases 3–5, `seq_switchToBackgroundScene()`). Playback then never reads a region
that is being rewritten, so neither the drift (§9.2) nor the partly empty
Pattern during the read can occur. When that exists,
`filesystem_patternRealignPlayingTracks()` (§9.3) should be removed. It stays
for now.

**Self-load and the realign:** a Load of a Scene over the playing Scene ends
with a per-track realign of every track that plays it. A track already on the
grid with the same timing lands on the step it would have reached anyway.
A track whose timing changed or drifted moves to the master-clock position, as
with the PERF double-click.

**Build:** the §1–§9 sources unchanged, `make all FS_OS=1` (only
`filesystem.o` rebuilt; the `FS_OS=0` ELF and image are kept in scratch).

| Build | Image | text + data | vs `FS_OS=0` (§9.4) | Loops | Hot calls | ISR/render bodies |
|-------|-------|------------:|--------------------:|-------|-----------|-------------------|
| `FS_OS=1` + probe + §8–§9 fixes | `build/LXRV2_lxr02.img` (679,664 B file) | 679,648 | −36,568 | 86/86 exact; 77 + 58 hot loops equal | same | `din_dout_exchange` differs in variable addresses and offsets only (as in §2 and P2 §3); the other 20 are identical |

`filesystem_patternRealignPlayingTracks()` is linked (7 call sites after LTO
inlining of the bracket), and the probe is present.

**Device check:** repeat §9.5 on this image, the same Scene Loads (into the
playing Scene and into non-playing slots) and Saves, while playing the
worst-case Scene. Copy the card to `SD_CARD_FSOS_2`. The `'f'` records give the
`-Os` filesystem pass times next to §7 and §9.1 (`FS_OS=0`: Scene Load longest
pass 456–738 µs).

**Makefile:** the `FS_OS` comment said the stumble was tied to `FS_OS=1`. It now
says the stumble was a baseline defect, fixed here. `FS_OS ?= 0` stays the
default until this retest passes.

---

## 11. OK needs several presses again (`FS_OS=1`): held OK replay

**User (2026-11-17), §10 image:** "it's taking me several encoder presses to
get 'ok' to register on Scene load and save pages again". No card copy yet;
the cause below is from the code.

### 11.1 Cause: a second gate drops OK presses without a trace

§8.2 fixed presses that reached `menu_handleLoadSaveMenu()`. An earlier gate in
`menu_parseEncoder()` drops **every click** while `menu_storageBusy` is set or
Preset is not idle. The only exceptions are browse moves admitted during an
index read (S090 P3), and those exclude OK/execute clicks. The press is
swallowed (`lastEncoderButton = button; return;`): no toggle, no request, and
no `'k'` record. That is why `SD_CARD_BASE_2` showed every recorded press
working.

Storage is busy on these pages for library index reads (2.4–3 s while
playing, S090 P1), `.hcnames` reads, the Bank preview and, in DEV builds, the
trace flush every 500 ms. At `-Os` the filesystem work runs longer, so a
larger share of presses lands in a busy moment. The handler's own refusals
(§8.2) also still needed another press.

### 11.2 Fix: hold the OK and replay it when idle (`menu.c`)

- `menu_loadSaveOkRetry` holds attempts left (`MENU_LS_OK_ATTEMPTS` = 4) for
  an OK press that could not act. It is a 1-byte static. **bss did not
  grow** (422,452 B before and after; it fits in existing padding).
- **Held when:** the busy gate drops an OK press
  (`menu_loadSaveHoldOk()`, `'k'` outcome 6); or the handler drops it for a
  blank name (2) or a refusal (4). It applies only on a top-level Load/Save OK
  row (`menu_loadSaveOkRowHoldable()`): types below Glo, not nested Instrument,
  no command running. So a press during a running Load never queues a second
  one.
- **Replayed:** `menu_loadSaveRetryOk()` at the top of
  `menu_pollPresetStatus()`, before its early-return workers (the same place
  as the owed `kit` restore). It runs once storage, Preset and the
  filesystem are idle and the cursor is still on the OK row. It sets edit
  mode as the click would and runs the ordinary OK path (`'k'` outcome 7, then
  the normal outcome). A further refusal re-holds it with one attempt fewer.
- **Dropped:** any encoder turn or click (including one swallowed by the busy
  gate), leaving the OK row or page, or attempts used up.

The user sees an OK that acts as soon as the card is free, up to the length
of the running read (seconds while playing), instead of nothing.

### 11.3 Build and gate

`FS_OS=1` + probe + §8–§11: **680,344 B** (+696 B vs §10), bss unchanged.
Against `04c5c004`: 86/86 loops exact, 77 + 58 hot loops equal, hot call sets
the same. `din_dout_exchange` has identical instructions; only addresses and
literal pool words differ, as in §10. The other 20 ISR/render bodies are identical.
`build/LXRV2_lxr02.img` is this image (`FS_OS=1`).

### 11.4 Device check

On this image while playing:

1. Scene Load and Scene Save: press OK once, including right after scrolling
   or changing type. The command should start when the card is free, without
   another press.
2. Press OK, then turn the encoder before it starts. Nothing should run.
3. Press OK twice quickly. Only one Load or Save should run.
4. Copy the card to `SD_CARD_FSOS_3`. The `'k'` records show held (6), replayed
   (7) and the final outcome; the `'f'` records show the `-Os` pass times.

---

## 12. Device run of the §11 image (`SD_CARD_FS_OPT`, `FS_OS=1`)

**User (2026-11-17):** "seems ok."

- **OK presses:** 7 commands, 5 Loads/Saves on the Scene pages and 2 on the
  Bank pages. Three OK presses were refused while the card was busy; each was
  replayed by `menu_loadSaveRetryOk()` 20–70 ms later and accepted (`'k'` 4 →
  7 → 0). All other OK presses were accepted on the first press. None was
  dropped or needed a second press.
- **Probe (`FS_OS=1`):** longest `filesystem_tick()`: Scene Load 740 µs
  (phase 33; 738 µs at `-O2` in §9.1, so this phase is not slowed by `-Os`),
  Scene Save 428 µs, Bank Save 257 µs, Bank Load 190 µs, previews ≤ 379 µs. No
  slow pass (≥ 1 ms). One underrun at boot (idle), none during any operation.
- **Loads:** Scene Loads {5,8,11,13,14} and {0,7,9}, one Bank Load; the user
  reports no stumble.
- **Bank Save:** to slot 076 (new; the folder did not exist), all 16 Scenes
  written (16 `CREATE_RESULT`). This does not exercise B3 (Save over an
  occupied slot with fewer Scenes), which is **not fixed** (DRAFT_ONGOING_BUGS
  B3).

---

## 13. Bank replace Save acted as a Merge (DRAFT_ONGOING_BUGS B3)

**User (2026-11-18):** "make the bank save fix now".

### 13.1 Cause: the request set two masks before `filesystem_start()` cleared them

The Menu computes the masks correctly (`menu_bankOperationMasks()`): a replace
Save sends `act = selected & present`, `empty = ~act`. `preset_saveBank()`
passes both on. `filesystem_requestSaveBank()` then set
`op_bank_save_written_mask` and `op_bank_scene_empty_mask` **before**
`filesystem_start()`, and `filesystem_start()` clears every `op_bank_*` mask
for the new request. Only `op_bank_scene_save_mask` (set after the start) kept
its value, so the children were written, but every Bank Save ran with:

- **empty mask 0:** the per-child loop (phases 11/12/20/21) never visited a
  removed child, so the slot's other child folders stayed. This is B3: a
  replace Save behaved as a Merge, and the next Bank Load brought the old
  Scenes back.
- **written mask 0:** phase 6 always read the old manifest (phases 61–64), and
  the manifest staging in the request and phase 64 treated every child as
  untouched. Written Scenes got their old on-card mask entries instead of the
  RAM ones; phase 45 kept every Scene's old source instead of inheriting from
  the new Bank. This also affected Merge Saves.

The ordering dates from the S082 P0 implementation; the delete and
manifest paths for removed children had never run on the device.

### 13.2 Fix (`filesystem.c`)

1. `filesystem_requestSaveBank()`: the written and empty masks are set after
   `filesystem_start()`, next to `op_bank_scene_save_mask` and before the
   manifest staging that reads them.
2. Bank Save phase 21: after deleting a removed child, `op_bank_cwd_at_parent =
   1`. The delete scanner never changes the working directory, so it is still
   the Bank directory. Without this, a removed child visited before any written
   one (for example saving Scenes 8–15 over a full Bank) sent phase 12 to the
   root-based `/Bank/` reopen (phase 13) from inside the Bank directory. That
   reopen would fail, ending the Save in an error. This is the first path that
   now runs for the first time.

**Behaviour now:**

| Save | Children in the slot afterwards | Manifest |
|------|---------------------------------|----------|
| Replace | exactly the saved Scenes (selected and present); every other `NN Name` child is deleted | written entries from RAM; removed entries self-only; no old manifest read |
| Merge | saved Scenes replaced; others untouched | old manifest read for the untouched entries only, as designed (S24) |

Clean-skip children (Option 2: unchanged since the last save to the same slot)
count as written and are neither rewritten nor deleted, as before.

**Still true:** a child whose delete fails (for example a quarantined or
damaged folder, the old ErrS05 case) ends the Save with an error, as it
already does for written children. Not changed here.

### 13.3 Gate and build

`FS_OS=1` + all fixes: **680,336 B** (−8 B vs §11), bss unchanged. 86/86 loops
exact, 77 + 58 hot loops equal, hot calls the same, `din_dout_exchange` with
identical instructions (addresses only), the other 20 ISR/render bodies
identical. `build/LXRV2_lxr02.img` is this image.

### 13.4 Device check

1. Load a Bank with 16 Scenes (or Save one into an empty slot), then Bank Save
   **replace** into that slot with fewer Scenes selected or present. The slot
   folder on the card should hold only the saved `NN` children plus
   `bankset.bcg`. A Bank Load of it brings back only those Scenes.
2. Bank Save replace where the first unsaved Scene comes before the first saved
   one (deselect Scene 1). It should complete without an error.
3. Bank Save **merge** with a subset. The other children stay.
4. Bank Save replace over slot 024 (the B3 slot with stale children 01–06, 12):
   they should be removed.
5. Copy the card to `SD_CARD_BANK_FIX` for the slot folders and the trace.

---

## 14. "AutoSave / bank load" at boot (`SD_CARD_ATS_TEST`)

**User (2026-11-18):** "I got an 'autosave bank loaded'. … Find out why and fix
it. I recommend also giving autosave 20% more cpu (3% max instead of 2.5 while
playback is running). Also, the message is misleading. Autosave has **not**
been loaded. When the last bank indicated by settings gets loaded it should say
'Last Bank loaded'. Fix that, check the files, update the budget."

### 14.1 Why the AutoSave was not restored

The previous session (the §12 run, same trace file):

| Tick | Event |
|------|-------|
| 10348 | boot settle publishes AutoSave generation **2666** (the last AutoSave write of the session) |
| 27448 | a change arms the writer; immediately **held by the Load/Save page guard** (`'W'`) |
| … | Scene Loads, Scene Save, Bank Load (whole Bank, tick 27506 after wrap), all on the Load/Save pages |
| 43939–62021 | Bank Save to slot 076, all 16 Scenes; the saved slot becomes the boot Bank (phase 45) |
| 63855 | `settings.cfg` written (last Bank = 076) |
| ~2 s later | power off; the writer never ran again after tick 27448 |

At the next boot the reader found the newest record (`.hcprms2`, generation
2666) but `bank_mismatch=1`: the record describes the older Bank and
`settings.cfg` names 076. The reader declined, the HCNAMES-authoritative load
declined, and the canonical ladder loaded Bank 076 (`main.c`, sets the
bank-fallback latch, which shows the notice).

**Nothing was lost:** the session ended with a whole-Bank Load followed by a
16-Scene Bank Save, so Bank 076 on the card equalled the RAM state. The boot
fallback is the designed recovery for "AutoSave behind `settings.cfg`".

**Why the writer was behind:** the Load/Save page guard (MEMORY.md "Load/Save
drops AutoSave (S090 P2)"; `filesystem_autosaveWriterSchedule_tick()`) holds
the writer for as long as a Load/Save page is open, while Bank Save and the
settings write proceed. This is a recorded design rule and is **not changed**
here. Its code comment's reason ("Load/Save owns the shared 9,000-byte name
cache") does not match the code: the AutoSave patch cache is a separate
4,608 B static (`fs_autosave_parameter_cache`). The rule's real effect is to
keep the card free for browsing and OK. Remaining exposure: Scene or Kit Loads
on the Load/Save page followed by power off *without leaving the page* are not
in the AutoSave. Relaxing the guard is a decision for the user (§14.5).

### 14.2 Notice text (`menu.c`)

`menu_drainAutosaveBootNotices()`: "AutoSave / bank load" → **"Last Bank /
loaded"**. It shows for both bank-fallback paths (canonical Bank Load and the
HCNAMES-authoritative load); both load the Bank `settings.cfg` names. The
per-Scene "AutoSave / Sc NN empty" notices (Case 3) are unchanged.

### 14.3 New finding: the boot notices stalled audio (63 underruns)

The probe logged **+1 and then +63 underruns** ~0.2 s after this boot's settle
ended (tick 11945/12126). Every other boot in the trace file shows +1. The
notice code called `lcd_waitForIdle()` twice per overlay after audio start.
That function spins until TIM7 has drained the LCD queue and renders no
audio; its header says it is only for "modal operations that suspend audio
anyway".

**Fix:** `menu_lcdOverlayRoom()`. Before audio starts it waits as before. Once
audio runs it never waits: an overlay is drawn only when the queue has room
for it (40 ops; the queue holds 64), otherwise the drain tries again next pass.
It is used by the "Last Bank" and "Sc NN empty" notices and by the
"old settings" warning (whose second wait now runs only before audio). The
modal sample-install messages (audio suspended) and the DEV menu check at init
keep their waits.

### 14.4 Budget

`config.h`: `BACKGROUND_CPU_BUDGET_US_PER_MS_PLAYING` 25 → **30** µs/ms
(3 % while playing; stopped stays 5 %). Comments in `config.h` and
`filesystem.h` updated. The budget meters foreground background work
(AutoSave drains, Pattern drain and repair). Positive credit stays capped at
1 ms of allowance (30 µs), so the largest burst grows by 5 µs per pass.
No DSP code changes.

### 14.5 Other records on the card

- 6 operation errors in the previous session, all cancellations, which end
  ERROR by design: 4 library index reads stopped by type steps, and 2 Bank
  previews of slot 075 (moving on, and the Bank Save OK).
- This boot's settle replayed the whole Bank (fallback latch), published
  generations 2667–2677, and ended OK after 8.7 s.
- **Open decision for the user:** to keep AutoSave current while a Load/Save
  page stays open, the writer could be allowed to run there when no command
  and no browse read is running, and be aborted by any page action (as on page
  entry today). That changes the S090 P2 rule, so it is not done.

### 14.6 Gate and build

`FS_OS=1` + all fixes: **680,384 B**, bss unchanged. 86/86 loops exact, hot
loops equal, hot calls the same, ISR/render bodies identical
(`din_dout_exchange` instructions identical, addresses only).
`build/LXRV2_lxr02.img` is this image.

### 14.7 Device check

1. Reproduce: on the Load/Save pages, Bank Save, then power off within a few
   seconds. Boot shows **"Last Bank / loaded"**, with no audio glitch while it
   shows.
2. Normal case: make an edit, leave the Load/Save page, wait ~6 s, power off.
   Boot restores the AutoSave without a notice.
3. Copy the card to `SD_CARD_ATS_TEST_2`. The probe should show no underrun
   burst after the settle.

---

## 15. Load provenance at power-off: check for regressions

**User (2026-11-18), answering §14.5:** "Scene and kit loads should mark the
.hcnames file that they are refreshed from the card and if there is a power
cycle at that point, they should be loaded from the card at boot. That should
be sufficient until something about what has been loaded changes (a
parameter, etc), at which point the load/save will have been exited and the
autosave gets an opportunity to run. Check that this is still the case and we
haven't regressed."

So the §14.5 proposal (AutoSave on the Load/Save page) is **not needed** and is
dropped. No code was changed in this step.

### 15.1 The mechanism in the code

- **Witness.** Each `.hcnames` row's source word carries bit 13,
  `FS_RESIDENT_SOURCE_REFRESHED_FLAG` ("refreshed from the card"), in RAM
  (`fs_resident_source[]`). It reaches the card with the next `.hcnames`
  rewrite.
- **Scene Load:** phase 61 (successful terminal, after Pattern and Effect)
  calls `filesystem_setResidentSceneRefreshed()` for each destination: Scene,
  Kit, Pattern, Effect and six Instrument rows. The same operation continues
  into `FS_INTERNAL_OP_UPDATE_HCNAMES_SCENE` (`filesystem.c` l. 14793), so the rows
  are on the card **before the Load completes**, whatever page is open.
- **Pattern Load:** sets the Pattern row and writes it in its own
  `UPDATE_HCNAMES_PATTERN`, also within the Load.
- **Kit Load:** `filesystem_loadKitDirectory_tick()` sets the Kit and six
  Instrument rows refreshed (and the Kit source) in RAM. The card copy is
  written by the Menu's name session, see §15.4.
- **Retiring the witness:** a scalar AutoSave drain clears bit 13 only for
  rows whose object is fully captured (`filesystem_clearResidentRefreshedCaptured()`),
  in the same transaction as its `.hcnames` rewrite. A Pattern edit clears the
  Pattern row's bit immediately (`autosave_markPatternDirty()`).
- **Boot:** with a valid AutoSave winner (Bank matches), the reader takes a
  refreshed row with a library source as Case 2 and reloads that object from
  the card; other rows come from the AutoSave (Case 1).

### 15.2 Device evidence (`SD_CARD_ATS_TEST/asavetrc.bin`, the `SD_CARD_BASE` session)

| Session (`FS_OS=0` baseline image) | Next boot |
|------------------------------------|-----------|
| AutoSave published nothing after the boot settle (generation 2638); the writer was held by the page guard throughout | winner generation 2638, `bank_mismatch=0` |
| Scene Loads {0,5,6,7}, {8,9,10,11}, {5,6,7}, {11,12,13}, {9,10,11}, each followed by its `UPDATE_HCNAMES_SCENE` | `case2_scene_mask=0x3fe1` = **{0,5–13}**, exactly the union of the Loads |
| | 9 Case 2 rows reloaded per Scene (Scene, Kit, Effect, six Instruments); Pattern children by the reader's Pattern step |

So "Scene Load on the page, power off, boot reloads it from the card" works
on the device. That session ran on the `FS_OS=0` image with the §7 probe,
before the §8 fixes. Later images do not touch this path (§15.3).

### 15.3 Regression check

Code changes since `131f567e` (before S097) in the files on this path:

- **`filesystem.c`:** the `FS_HOT_O2` markers (P2), the merged
  settings/MIDI-learn safe writer and boot block-open helpers (P1), the DEV
  probe (§1), the Pattern realign calls (§9), and the Bank Save mask/CWD fixes
  (§13). None touches the refreshed bit, the source rows, the
  `UPDATE_HCNAMES_*` operations, the Scene/Kit Load terminals or the boot
  reader.
- **`Autosave.c`:** unchanged. **`menu.c`:** the S097 changes (OK handling,
  sound-apply mask, boot notices) do not touch the resident-name session.
  **`presetManager.c`:** only `preset_startLoadApply()` (runtime apply).

**No regression** on the Scene and Pattern Load path.

### 15.4 Kit (and nested Instrument) Loads reach `.hcnames` only at session end (as intended, §15.6)

Not a regression: this is the S042/S050 design. After a Kit Load,
`menu_refreshResidentNameScratchKit()` only adds the destination Scenes to
`menu_residentNameDirtySceneMask` ("record one committed normal full-Kit
change **without touching the card**"). Nested Instrument Loads do the same
(`menu_refreshResidentNameScratchInstrument()`). The rows are written once by
`menu_endResidentNameScratchSession()` at a UI boundary (leaving the page,
changing type or context) or by `menu_triggerDeferredHcnamesFlush()`, which
skips the Load/Save pages.

**Consequence (from the code; not seen on the device yet, since the trace has
no Kit-only Load followed by a power cycle):** power off while still on the
page after a Kit Load (including load-on-scroll), and the card's `.hcnames`
still shows the earlier Kit source without the witness. AutoSave is also
behind (page guard), so boot restores the AutoSave's earlier Kit (Case 1). The
loaded Kit is lost, against the user's rule.

**Options considered (both declined, §15.6):**

| Option | What | Cost |
|--------|------|------|
| (a) | Write the Kit/Instrument rows at the end of every Kit Load, as Scene Load does | one full `.hcnames` rewrite per Load (longest pass ~150 µs in the trace); with load-on-scroll that is one per encoder detent, so the card is busy more often for OK and name reads |
| (b) | Keep the session, but flush it once the browse settles: Kit unchanged for ~1–2 s and the card idle, still on the page | no per-detent cost; worst case the last ~2 s of browsing are lost on power off |

The recommendation was (b). The user declined both (§15.6).

### 15.5 Bank level (for completeness)

A Bank Load or Bank Save on the page changes the boot Bank in
`settings.cfg` while AutoSave is behind, so at boot the winner is rejected
(`bank_mismatch=1`, §14). Then the HCNAMES-authoritative load runs. It needs
the register's Bank row to equal the boot Bank and **all 161 rows refreshed**.
A Bank Load satisfies this by design ("load Bank in menu, power off before
menu exit"). After §13 a full replace Bank Save should too: every written child
is refreshed by the Scene writer, removed children by
`filesystem_stageEmptySceneRows()`, and the Bank row in phase 45. A Bank
Save that leaves present Scenes unwritten (Merge, or deselected) keeps them
unrefreshed, so the canonical Bank Load runs instead. Either path shows "Last
Bank / loaded". This is from the code and not verified on the device.

### 15.6 Decision: Kit commits on exit (no change)

**User (2026-11-18):** "no, leave it the way it is. 'waiting to settle' would
lock another kit selection in that interval and cause glitchiness. The
intended workflow is to commit the kit on exit, so it's working as intended."

- **Kit and nested Instrument Loads** write their `.hcnames` rows once, when the
  name session ends at page exit or a context change
  (`menu_endResidentNameScratchSession()`). That is the intended commit point.
  Browsing on the Load:Kit page is provisional until exit; a power cycle before
  exit returns the AutoSave's earlier Kit by design.
- Option (b) is rejected because a settle-time flush makes the card busy in that
  interval, which locks the next Kit selection and is heard as glitches.
  Option (a) is rejected for the same reason, worse (a write per detent).
- §15.4 is therefore **not a defect**. Nothing is changed, and nothing is
  carried to DRAFT_ONGOING_BUGS.
