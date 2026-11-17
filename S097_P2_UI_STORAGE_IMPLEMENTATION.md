# S097 P2 — UI and Storage Code for Size: Implementation Log

Date: 2026-11-17. Branch `dev-ph7-alkaline`, on top of `7c93b869` (S097 P1,
committed; 726,656 B used). Plan: `S097_P2_UI_STORAGE_CODE_OPTIMIZATION.md`.

User instruction: implement the P2 reductions, **conservatively**, and consider
the worst-case CPU users: does anything risk a spike?

## Result

| Build | Flash used | Δ vs `7c93b869` | Headroom | Status |
|-------|-----------:|----------------:|---------:|--------|
| `7c93b869` (S097 P1) | 726,656 | — | 27,008 | reference |
| step 1: five boot/install/FAT sources at `-Os` | 716,200 | −10,456 | 37,464 | in the build |
| step 1 + cold sources linked after DSP | 714,980 | −11,676 | 38,684 | in the build; **passed on the device** (§9) |
| + `filesystem.c` at `-Os` (`FS_OS=1`) | 678,740 | −47,916 | 74,924 | tried in §9; **off**: A/B test points to it for the Scene-load stumble (§11) |

Default build, against `7c93b869`:

- 86/86 oscillator loops exact; 77 oscillator and 58 mixer hot loops equal.
- Hot call sets unchanged (render function, mixer, TIM3, every ISR).
- The 21 ISR/render bodies checked: identical except `TIM3_IRQHandler` and
  `din_dout_exchange`, which differ only in the offsets of the variables they
  load (same instructions, same count; data placement moved).
- RAM, DTCM statics, FX arena and ITCM unchanged (`.data` +4 B alignment).
- A clean build in a fresh copy is byte-identical to the tree build. Host
  tests pass (`instrument_manager`, `special_tags`, `make_init_*`).

The 40–45 KB target is reached only by the opt-in `filesystem.c` step. That
step leaves every per-sample loop exact, but it lengthens fixed-work
filesystem steps in the main loop while playing, which only the device can
price (§5).

---

## 1. Re-measure and the conservative rule

The plan's per-file numbers came from the tree before P1 item 2, so tier 1
was measured again on `7c93b869`: −17,464 B with `MidiLearn.c`, −16,336 B
without, every loop exact.

Being conservative, the loop gate alone is not enough: a `-Os` function is
slower wherever it runs. **A source may go to `-Os` only if none of its
functions can run in an ISR, on the render path, in TIM3 or per MIDI message.**
I built a call graph from the ELF disassembly (`bl`/`b.w`, veneers folded),
took the roots `audio_check_and_render()`, `mixer_calcNextSampleBlock()`,
`voiceControl_processPending()`, `seq_rampService()`, every `*_Handler` /
`*_IRQHandler`, and `midiIn_dispatch()`, and listed each candidate file's
reachable functions. The static graph does not see calls through function
pointers, so files with pointer hooks on these paths count as reachable too.

Paths found (from the graph, each checked in the source):

- **TIM3 runs a full menu repaint.** `TIM3_IRQHandler →
  seq_processSchedulerTick → seq_handleMasterBoundary →
  led_notifyPatternChanged → menu_setShownPattern → menu_repaint`. With Follow
  on, a Scene change repaints the menu inside the ISR. That is an existing
  worst-case spike, and anything on the repaint path at `-Os` makes it larger.
- **The render path reaches AutoSave.** `audio_check_and_render →
  Tetrakt_triggerVoice → … → scene_setSlot6Track7AmpEnvelopeDecay →
  autosave_markPayloadOffsetDirty`, and the DEV cycle probe
  `mixer_probeEnd → autosaveTrace_record`.
- **TIM3 reaches** `pat_setStepActive → autosave_markPatternDirty` and
  `patternTrace_record`.
- **MIDI per message reaches** `midiLearn_applySource` and its lookups.

| Candidate (tier 1 of the plan) | Reachable from | Decision |
|--------------------------------|----------------|----------|
| `asyncfatfs.c` | none (main loop only, inside filesystem operations) | **`-Os`** |
| `SampleInstall.c`, `sampleFlash.c` | none (`Load:[Samples]`, audio suspended; `sampleFlash_init()` at boot) | **`-Os`** |
| `flashImage.c`, `SplashAnimation.c` | none (boot only, from `main()`) | **`-Os`** |
| `fat_standard.c` | TIM3 repaint (`menu_currentSaveWouldOverwrite → fat_compareDisplayName`) | excluded |
| `storageTypes.c` | TIM3 repaint (`storage_makeSavedInstrumentDisplayFilename`) | excluded |
| `menuEffects.c` | TIM3 repaint (cell formatting) | excluded |
| `Autosave.c` | render path and TIM3 | excluded |
| `AutosaveTrace.c` | render path (DEV probe) | excluded |
| `PatternTrace.c` | TIM3 | excluded |
| `MidiLearn.c` | every MIDI message | excluded |
| nine `*Parameters.c` | repaint through registry function pointers (labels, inactive-cell hooks) | excluded |
| `screensaver.c`, `Cc2Text.c` | main-loop UI | excluded (≈ 200 B, not worth it) |
| `memtest.c` | not linked (`memtest_run()` commented out) | — |

## 2. Step 1 in the default build (−10,456 B)

`Makefile`: `OS_SRCS` lists `asyncfatfs.c`, `SampleInstall.c`,
`sampleFlash.c`, `flashImage.c`, `SplashAnimation.c`. The rule
`$(patsubst %.c,$(BUILD)/%.o,$(OS_SRCS)): CFLAGS += -Os` adds `-Os` after
`-O2`, and a comment states the admission rule and the gate.

Gate against `7c93b869`: loops exact, hot calls the same, **all 21 ISR/render
bodies identical**. 726,656 → 716,200 B.

**Worst case for `asyncfatfs.c`.** It is the one step-1 file that runs while
playing (AutoSave drains, background Bank loads, Load/Save). Each function's
own longest path (`elf_blockpath.py`, callees separate), `-O2` → `-Os`:

| Function | `-O2` | `-Os` |
|----------|------:|------:|
| `afatfs_poll` | 143 | 87 |
| `afatfs_fread` | 22 | 42 |
| `afatfs_fwrite` | 100 | 78 |
| `afatfs_findNext` | 35 | 28 |
| `afatfs_fileOperationContinue` | 340 | 78 |
| `afatfs_cacheSector` | 108 | 55 |
| `afatfs_flush` | 47 | 47 |
| `afatfs_fseekAtomic` | 100 | 53 |

At `-Os` less is inlined, so each function's own path is mostly shorter, with
more calls. Every change is tens of instructions per call. The SD transfer
itself is the bit-banged SPI in `spi_sd.c`/`sdcard_lxr02.c`, still at `-O2`,
at thousands of cycles per sector. No spike is expected. Device check: §5 (1).

## 3. Cold sources linked after the DSP objects (−1,220 B more)

**Finding.** LTO's balanced partitioner fills partitions in object order, and
the cold UI/storage objects came before the DSP objects. Any change in their
size moved the partition cuts through the voice renders and the mixer: that
is the ±1 loop "jitter" of P0 §3. The plan's pin
(`--param=lto-partitions=N --param=lto-min-partition=1000`) was swept over
N = 16, 17, 18, 19, 20 and 24. Every pin itself moves `osc_pwmWsFm`,
`osc_pwmWsFmRamp` and `osc_pwmWsRing` by +1 (24 is worse: 9 mixer loops +1).
**The pin was rejected**: no value was free.

Linking the DSP objects first changed the mixer's call set and 6 mixer loops
(rejected). **Linking the cold objects last** left every loop exact. It also
isolates them: under that order, `filesystem.c` at `-Os` leaves every loop
exact (`menu.c` still moves 9 mixer loops and stays excluded).

`Makefile`: a `COLD_LAST` list (`filesystem.c`, `storageTypes.c`,
`asyncfatfs.c`, `fat_standard.c`, `menu.c`, `menuEffects.c`, the four CopyClear
sources) is filtered out of `SRCS` in `OBJS` and appended after `DSP_SRCS`. A
comment explains why. No source moves; only the link order changes.

Gate (step 1 + order) against `7c93b869`: loops exact, hot calls the same.
`TIM3_IRQHandler` and `din_dout_exchange` have the same instructions and
count, but different load offsets (`ldrb r0, [r3, #0]` → `[r3, #4]`;
`[r2, #2068]` → `[r2, #424]`): variable placement inside data blocks moved,
costing nothing. LTRANS partitions: 16 (was 17). 716,200 → 714,980 B.

**Build note:** neither `OS_SRCS` nor `OBJS` order is a prerequisite of the
objects or the ELF. After changing either, remove the affected objects and
`build/lxr02.elf` (or `make clean`); otherwise `make` reports the old image.

## 4. `filesystem.c` at `-Os` (`FS_OS=1`, −36,240 B more; opt-in at first, default since §9)

**How it works.**

- **`Makefile`:** `FS_OS ?= 0`. With `FS_OS=1`, `filesystem.c` joins `OS_SRCS`
  and gets `-DFS_OS_BUILD`.
- **`filesystem.c`:** `FS_HOT_O2` is `__attribute__((optimize("O2")))` under
  `FS_OS_BUILD` and empty otherwise. It marks the functions reachable from
  TIM3 or MIDI: `filesystem_sceneSlotName()`, `filesystem_bankSlotName()`,
  `filesystem_patternSlotName()`, `filesystem_copyInstrumentStemDisplay()`,
  `filesystem_setAutosaveEnabled()`, and the slot helpers' callee
  `filesystem_cachedLibraryName()`.
- **Default build:** byte-identical to the build before these lines were added
  (checked with `cmp` on `lxr02.bin`).

**To build it:** `make clean && make all FS_OS=1 && make img`. Then
`make clean && make all && make img` to go back. `make clean` removes the
tracked `build/LXRV2_lxr02.img`; `make img` writes it again.

**Measured** (scratch build directory): 714,980 → 678,740 B (−36,240). Against
`7c93b869`: 86/86 loops exact, all hot loops equal, hot calls the same; ISR
bodies as in §3 (offsets only); 15 LTRANS partitions.

The `FS_HOT_O2` functions: `copyInstrumentStemDisplay` is identical to `-O2`,
and `setAutosaveEnabled` differs by one tail-call form. The three slot-name
helpers are 42–44 instructions instead of 39. LTO keeps the 10-instruction
`filesystem_cachedLibraryName()` out of line in this build (it has `-Os`
callers too), so they make one call+return (≈ 6–8 cycles) where they used to
inline it. They run a few times per repaint.

**Why it is not in the default build.** `filesystem.c` runs in the main loop
while playing: AutoSave drains, Scene/Bank Load/Save, background Bank loads,
name caches. At `-Os` each step takes longer. Some of that work is already
bounded by time, so it gets slower but not heavier:

- the background AutoSave/repair work is charged against a measured-time share
  (`filesystem_backgroundBudgetCharge()`; 2.5 % while playing);
- the AutoSave CRC runs 128 B per tick (`AUTOSAVE_CRC_BYTES_PER_TICK`);
- index, HCNAMES, scan and text-line loops stop at a 15 µs batch budget while
  playing (`filesystem_batchHasTime()`).

The other state-machine phases do a **fixed** amount of work per main-loop pass
(open, parse a line, apply a child Scene, rename, …). Those passes get longer at
`-Os`. The audio queue has two 96-frame slots (≈ 2.18 ms each). If one pass
grew past the remaining slack on a 100 % Scene, the result would be an
underrun. That cannot be priced statically, so it stays off until §5 (2)
shows it is safe.

## 5. Device checks

**(1) Default build** (`build/LXRV2_lxr02.img`, 714,980 B):

- Boot to sound, with the flash image check passing (`flashImage.c` is `-Os`)
  and the splash animation.
- `Load:[Samples]` installs samples and wavetables, and they play.
- On the worst-case Scene (CPU widget near 100 %): edit parameters so AutoSave
  drains, save and load a Scene, load a Bank, all while playing. The CPU widget
  should not hit 0 % free more than before, and there should be no new clicks.
  These use the `-Os` FAT engine.

**(2) Opt-in build** (`make clean && make all FS_OS=1 && make img`), same
Scene and same actions, against (1):

- Listen for dropouts and watch the CPU widget during a Scene Save, a Scene
  Load, a Bank Load and an AutoSave drain while playing. For a numeric check,
  re-enable the commented `lcd_diagDisplayInt("UNR",
  (int32_t)audioCodec_underrunCount, …)` line in `main.c` in a test build and
  compare the counts.
- Boot time (boot runs filesystem code blocking, before audio): expect slower,
  inaudible.
- If (2) shows no extra underruns, making it the default is a one-line change:
  move `filesystem.c` into `OS_SRCS` (keep `-DFS_OS_BUILD` for it).

## 6. Not done, and why

| Item | Saving (measured) | Reason |
|------|------------------:|--------|
| `menu.c` at `-Os` | ≈ −21 KB | the repaint runs inside TIM3 on a Follow Scene change; it also still moves 9 mixer loops with cold-last |
| CopyClear at `-Os` | ≈ −10 KB | runs during play (paste jobs); not needed for the target; candidate after (2) |
| `buttonHandler.c`, `ledHandler.c` | ≈ −6 KB | code runs in the panel exchange ISR (`buttonHandler_buttonPressed()`/`Released()` from `din.c`) and from the sequencer (`led_notifyPatternChanged()`) |
| `presetManager.c`, USB stack | ≈ −3.8 / −0.8 KB | mixer gains a call / `OTG_FS_IRQHandler` changes |
| `fat_standard.c`, `storageTypes.c`, `menuEffects.c`, AutoSave/trace files, `MidiLearn.c`, `*Parameters.c` | ≈ −7 KB together | reachable from TIM3, render or MIDI (§1) |
| partition pin (plan step 2) | — | every pin moved 3 PWM loops (§3) |
| `-flto-partition=one` | — | larger one-time change (P2 plan §4.4) |

## 7. Files changed (all of P2, including §9)

| File | Change |
|------|--------|
| `Makefile` | `OS_SRCS` + its `-Os` rule; `FS_OS` switch (default 0; 1 during §9); `COLD_LAST` and the new `OBJS` order |
| `Core/Hardware/SD/filesystem.c` | `FS_HOT_O2` macro (empty by default) on six functions |

## 8. Statements now out of date (for the user; not edited, per the S093 rule)

- `MEMORY.md` Toolchain: "DSP source files are compiled by the Makefile's
  more-specific `-Ofast` rule" is still true. Add that five cold sources are
  `-Os` (`OS_SRCS`), that UI/storage objects link last (`COLD_LAST`), and that
  `FS_OS=1` exists.
- `MEMORY.md` "Measuring flash": headroom is now 38,684 B (no longer under the
  16 KiB warning).
- `STORAGE_SRAM_MANIFEST.md` flash figures.
- `S097_P0_PROGRAM_FLASH_USE_REFACTOR.md` §3.3 proposed a partial LTO link or a
  pin. The link order (§3 here) is what worked.

---

## 9. Device result of the step-1 build, and `FS_OS=1` made the default

**Device test (user, 2026-11-17):** the default build of §2–§3 (714,980 B) ran
and "seems ok". Its card logs (`SD_CARD_COL/asavetrc.bin`, `pattrace.bin`) were
decoded with `tools/diagnostic_tools/decode_devlogs.py <file>` to scratch,
without writing into the card copy. No `bootlog.bin` exists: it is written
only on a boot timeout.

Last boot (boot 82 of 82 in the trace; the trace has no firmware-version
record, so this is identified by position):

| Check | Boot 82 | Boots 76–81 |
|-------|---------|-------------|
| Boot settle (stage 13) | END after 228 ms, outcome ok | 226–230 ms, ok |
| Mixer block average (DWT probe) | 139,115 cycles | 134,473–141,552 |
| Mixer worst block | 161,258 cycles | 157,626–165,498 |
| Operation errors | 10 | 0 |
| Pattern trace errors | 0 | — |

**The 10 operation errors are designed cancellations, not faults:**

- 9 × `FS_INTERNAL_OP_SCAN_BANK_SCENES` at phases 10/11/16, for Bank slots
  1, 5, 8, 10, 13 and 14 in rising order. That is scrolling Bank slots on the
  Load page: `filesystem_cancelScanBankScenes()` disposes the superseded
  preview and "finishes ERROR, so its completion is never mistaken for a valid
  preview".
- 1 × `FS_INTERNAL_OP_LOAD_LIBRARY_INDEX` at phase 9, which is
  `filesystem_finishCancelledBrowseRead()` (a type turn cancelling a running
  index read, S090 P1).

The same two kinds appear in earlier boots on older firmware (boots 0–7,
33–36, 56; 165 in total), whenever Banks were browsed. During this test the
card's Bank slot 014 changed from "LoadTst2" to a new "014 CollidSt", so Bank
work was done in this session. One `WRITER_SUPPRESSED` (the Load/Save page
guard) and one `TRACE_SUPPRESSED` (command-active gate) are normal.

**`FS_OS=1` is now the default** (user: "let's try the 'make FS_OS=1' build
and see if we can apply those savings"). `Makefile`: `FS_OS ?= 1`, with its
comment updated, including how to go back (`make clean && make all FS_OS=0`).
After `make clean && make all && make img` in the tree:

- 678,740 B, headroom 74,924 B, 15 LTRANS partitions;
- `lxr02.bin` is byte-identical to the gated scratch `FS_OS=1` build of §4;
- the gate in the tree against `7c93b869`: 86/86 loops exact, all hot loops
  equal, hot calls the same, ISR bodies differ only in data offsets (§3);
- `build/LXRV2_lxr02.img` is this image.

**Device check for this image** is §5 (2): on the worst-case Scene, a Scene
Save, a Scene Load, a Bank Load and an AutoSave drain while playing. Listen
for dropouts and compare the CPU widget with the step-1 build; then the boot
time. The card logs give the boot-settle time and the mixer probe for
comparison with the table above.

Two issues the user raised at this point are logged as future work in
`DRAFT_ONGOING_BUGS.md` (instant Scene change between identical Scenes; slow
AutoSave and possibly missed Colloid edits).

---

## 10. Device test of `FS_OS=1`, and back to `FS_OS=0`

**User report (2026-11-17, `FS_OS=1` image, 678,740 B):**

1. The sequencer "stumbled" or dropped a step while loading Scenes, although
   the load did not target the playing Scene.
2. A Scene Save took several encoder presses on OK.
3. Bank Load and Save seemed fine, but Scene slots 2–6 ended up populated
   without any merge option being used.
4. No CPU spike was seen on the widget (it cannot be watched during Load/Save).

The logs were read straight from the mounted card (`/Volumes/LXR02`,
`decode_devlogs.py <file>`, output to scratch; nothing written to the card).
`SD_CARD_COL_2` was still empty at the time. Two new boots (83, 84) follow the
step-1 boot 82.

| Check | Boot 82 (step 1) | Boots 83–84 (`FS_OS=1`) |
|-------|------------------|-------------------------|
| Boot settle | 228 ms | 225 / 232 ms |
| Mixer block average / worst | 139,115 / 161,258 | 140,517 / 162,340 and 139,157 / 162,400 |
| Background scalar slice, mean / max | 150 / 248 µs | 62 / 87 and **228 / 267 µs** |
| Background Pattern slice, mean / max | 144 / 197 µs | — and 158 / 169 µs |
| Scene Load (facade time) | 16,352 ms | 10,902 ms (into Scenes 9, 10, 11) |
| Scene Save | 1,744 ms | 1,719 ms |
| Bank Save | 10,893 ms | 17,845 ms (9 children) |
| Operation errors | 10 (cancellations) | 0 and 10: 8 browse cancellations, 1 parameter-drain and 1 Pattern-drain ERROR. The parameter one is followed at once by Load/Save page reads, consistent with the S090 P2 "entering Load/Save aborts a running drain" rule (not proven) |

**(1) Stumble: cause not established.**

- Neither operation durations nor the mixer cost show an `-Os` slowdown; the
  durations are SD-bound and vary with content.
- Background AutoSave slices are somewhat longer (boot 84 scalar mean 228 µs
  against 150 µs), but still far below one 2.18 ms audio block.
- The Scene Load itself is not time-sliced, and no trace records foreground
  pass length or underruns, so a long `-Os` pass can be neither shown nor
  excluded.
- The Scene-load completion path only updates the present mask (no sequencer
  calls), and the Pattern-service gate pauses only the destination Scenes.

Being conservative, **`FS_OS` is back to 0 by default** (`Makefile` comment
updated). After `make clean && make all && make img`, the image is **byte-identical
to the step-1 image tested in §9** (714,980 B). Next: repeat the same action on
this image, a Scene Load into non-playing slots while playing. If it still
stumbles, the stumble predates P2 (`DRAFT_ONGOING_BUGS.md` B4) and `FS_OS=1` can
come back. If it does not, `FS_OS=1` stays off and the long foreground phase has
to be found first.

**(2) Several OK presses: not the filesystem.** Before the one recorded Scene
Save request (slot 49, tick 50,156) the facade had been idle for 28.9 s, so no
filesystem work was refusing it. Earlier presses never reached
`filesystem_requestSave…`. This is menu-side (`menu.c` is not `-Os`); see
`DRAFT_ONGOING_BUGS.md` B5.

**(3) Populated slots: root cause found, unrelated to `-Os`.** The Bank Save
went to slot 024, which already held the 16-Scene Bank "NoBank" (copy in
`SD_CARD_COL`). It wrote only the 9 Scenes present in the resident Bank (00,
07–11, 13–15) and renamed the folder "CollidSt". The old Scenes 01–06 and 12
stayed on the card with their old Kits (SeaWaked, Eris, Flux, Beatmstr,
FilMod…). The Bank Load then found 16 children (present `0xffff`) and loaded
them all. `filesystem_saveBankDirectory_tick()` does this by design: "Non-masked
children are never touched and survive from previous saves" (it replaced a
total-tree delete that failed on quarantined directories, ErrS05). So a Bank
Save over an occupied slot acts like a merge. Logged as
`DRAFT_ONGOING_BUGS.md` B3.

**Card note:** `.hcnamtmp` (2,181 B) sits beside `.hcnames` (2,179 B) on the
card, a pending names-register promotion. Boot recovery handles it; it is noted
in the bugs draft in case it recurs.

---

## 11. A/B result: the stumble follows `FS_OS=1`

**User (2026-11-17):** with the `FS_OS=0` image (`SD_CARD_COL_3`; the card's
`LXRV2_lxr02.img` is 714,996 B = 714,980 + 16 B header), "the sequencer
stumble did not re-occur".

Boot 85 (this test), from `SD_CARD_COL_3/asavetrc.bin`:

| | Boot 84 (`FS_OS=1`) | Boot 85 (`FS_OS=0`) |
|---|---|---|
| Scene Load | 1 Scene → Scenes 9, 10, 11; facade 10,902 ms; **stumble** | 1 Scene → Scenes 13, 14, 15; facade 13,146 ms; **no stumble** |
| Scene Save | 1,719 ms | 1,970 ms (slot 59) |
| Mixer average / worst | 139,157 / 162,400 | 140,719 / 161,298 |
| Errors | 8 cancellations + 2 drain aborts | 1 parameter-drain abort (phase 41, at Load/Save entry, as boot 84) |
| Boot settle | 232 ms | 2,105 ms, `pending=scalar,repair`: the boot promoted the leftover `.hcnamtmp` noted in §10 (it is gone from the card); not a regression |

**Conclusion:** the same action (a Scene Load into three non-playing slots
while playing) stumbled on `FS_OS=1` and did not on `FS_OS=0`. This is one trial
each, but it matches the risk named in §4: a fixed-work `filesystem.c` phase
that runs longer per main-loop pass at `-Os` delays the render and the trigger
drain. **`FS_OS` stays 0** (the default since §10). The tree builds the
device-tested step-1 image (714,980 B, headroom 38,684 B).

**To recover part of the −36 KB later** (not started):

1. Find the long pass first. A DEV-only trace record of the longest main-loop
   pass with `current_op`/`op_phase`, and of each `audioCodec_underrunCount`
   change, would show which Scene-load phase it is. It needs sign-off: a few
   bytes of DEV RAM, and `main.c` is in the LTO unit, so the loop gate applies.
2. Then either keep those phases' functions at `-O2` under `FS_OS=1` (the
   `FS_HOT_O2` mechanism), or bound the phase by time (`filesystem_batchHasTime()`)
   so it is safe at any optimisation level.
3. Or take only the code that cannot run while playing: boot readers, the
   boot-time trim/settle, sample install, repair. That needs those functions
   identified by hand; the static call graph misses dispatch through function
   pointers.
