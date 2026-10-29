# Helicase LXR — Project Context

This file is the working memory for Codex/Claude Code/LLM Agent sessions on this project.
Read it fully at the start of every session before touching any code.
Do not append session history or resolved-issue narrative to this file; those belong in `knowledge_files/log_archive/`.

---

## Quick Start

```
# Repository root is the working tree root

# Build
make all && make img   →   build/LXRV2_lxr02.img   (use `make all`: bare `make`
# can stop at build/main.o in an incremental tree; see SCOPING S072 carried debt)

# Flash: copy LXRV2_lxr02.img to SD card root, hold main encoder, power on
```

**Commits belong to the user.** Do not suggest or prompt when to commit.

**S087 complete** (2026-10-29, uncommitted on `f3805478`). Log:
`knowledge_files/log_archive/087_SESSION_HANDOFF_LOG.md`.
Working notes: `knowledge_files/volatile/S087_WORKING_NOTES.md`.
Host tests: `tools/dsp_test/modules/` (one file per module, `make -C tools/dsp_test all`);
effects retry kit (transient, filter drive, D+S): `tools/dsp_test/spike/README.md`.
No frozen snapshots are kept; copy the files to `build/` before an edit that needs a before/after.

## RAM and CPU Budget

Any new or enlarged RAM allocation needs user sign-off: identify the byte
count, memory region, and owner. Free DTCM is earmarked for future delay-line
buffers; free SRAM1 for future Pattern data — check before proposing reuse.

Logging/trace allocations require `DEV_MODE_LOGGING` enabled; a logging-off
build must not allocate those rings, cursors, or timing records.

## Binding Mandates

Before touching a module, read the mandates in its reference document.

| Policy | Document |
|--------|----------|
| DSP CPU: constant, predictable cost | `CPU_USE_DSP_AUDIT.md` §Binding Mandates |
| Oscillator work under the CPU rule | `INSTRUMENTS_DSP_REFERENCE.md` §12 |
| Load/Save slot scrolling | `FILESYSTEM_SPEC.md` §Binding Mandates |

---

## Binding Rules

Rules below are binding project constraints from confirmed sessions. Full detail
is in the referenced session logs and specification documents.

### Sequencer / Timing

- **Step reset (S081 §5.8):** once per step at first trigger; retriggers 2+
  never reset; live presses always reset. Reset in `voiceControl_pendingReset`.
  Render order: `voiceControl_processPending` (triggers + automation drain) →
  `seq_rampService` must not change. `PATTERN_DYNAMIC_STACK.md` §6.1.
- **Automation ramps (S081 P2):** held-bit writes, priority applies; end at
  next played step and every position break; never cross a loop wrap.
- **FX sequence = master clock (user, S081):** Scene change keeps FX aligned
  via `seq_fxRealignToMasterClock()`, never `seq_fxPublishReset()`.
- **Per-track timing (S078):** read only `seq_effectiveTrack*[]`; never write
  the Pattern region from Morph/automation. DDA seed `interval − 256`;
  shuffle stored `delay − 1`. `PATTERN_DYNAMIC_STACK.md` §6.4.
- **Roll engine (S084):** window-based length, not hit count. Carry fires no
  hit; carry = same rate+rhythm, refused when previous step fill < 100%.
  PERF/MIDI overlay rolls fire in TIM3 first, before running gate. Types
  0..14 = singles, flams, then STEP rates (type − 5).
- **Automation priority (S075/S081 R12/S083 Q1):** step auto → menu edit;
  a MIDI write is a menu edit. R12 suppresses until next
  trigger/transport/Scene change. Effects: R13. Morphable writes via
  `presetMorph_writeRuntimeBase()`.
- **External clock interpolation (S084 A1):** first tick on edge, rest via
  tempo timer from `seq_extTicksOwed`; never back-to-back.

### Live Record

- **Invariants (S081):** buttonHandler/Menu/MidiNotes/MidiControl call
  LiveRecord; Sequencer never does; never in TIM3. Live cursor for placement,
  no shuffle; echo marks carry deadlines; only Scene epoch = new take. Pattern
  writes never fan out; FX-sequence erase does.
- **First-change switch (S081 D12):** `LIVEREC_AUTOMATION_FIRST_CHANGE` in
  `config.h` (default 1).
- **Global Morph never recorded (S081 G-1):** `mrp` has no recording target.
  Each individual edit via `liveRec_morphValue[7]` / `liveRec_recordValue7()`.

### Looper

- **Invariants (S080):** master-clock anchored; never reintroduce shared loop
  counter, un-anchored snapshot, or resume via `seq_realignTrackToMasterClock()`.
  Wrap before DDA; transport stop ends it. FX/Effects never looped.
- **While recording (S081 P3):** runs with LATCH/OVERDUB. Held until every
  SELECT/VOICE/BAR released. VOICE adds/drops tracks only, never fires
  overlay or toggles mute. One producer `seq_recRingPush()`.

### Effects

- **R13 (S081):** menuHold > FX lock (own step) > track overlay > menu. Lock
  start clears overlay; held lane/markers/owners removed. Realign via
  `seq_fxRealignToMasterClock()`, never `seq_fxPublishReset()`.
- **Effect page (S074):** type UI in registry hooks, not `menu.c` branches;
  SELECT LEDs via `menuEffects_renderSelectLeds()`; underline via
  `menu_repaint()`; 20 B union spare.
- **Invariants:** SceneData only writes `scene_t.effect`. EffectsManager /
  FxBuffer foreground-only. DTCM arena never cleared; local indices
  type-relative; mask gate keeps fan-out safe.

### Menu / UI

- **SHIFT Morph-view (S078, binding, project-wide):** non-morphable params
  show/edit Normal value, never blanked or locked.
- **STEP Morph view (S078):** momentary (flag && SEQ_PAGE && shift). No latch;
  SHIFT+MODE STEP is only SOM entry.
- **LED deadlines (S081 P4):** signed 16-bit distance `led_timeReached()`;
  never `time_sysTick > end`.
- **PERF SEQ LEDs (S080):** queued Scene = `LED_LAYER_FAST`; never restart
  running blinks; never start/stop from per-beat path.
- **Latched menus (S080):** SHIFT+REC/chain stay open; SHIFT release ignored
  while open; SHIFT press closes (release consumed); MODE closes first.
- **Menu dtype full (S078):** all 16 ids/nibbles used. New cells: existing
  dtype + static-param special cases.
- **STEP underline search (S078):** `va_searchSceneMask` bits; every
  context/content change needs `va_searchRestart()`.
- **Active Scene (S075):** = viewed and written to, not playback.

### Scene / Bank

- **Edit-mask intent (S075):** Scenes in mask match; child edits fan out;
  Pattern data never does. Scene morph fans out like kit/fx (S077 P4).
- **One PC reset (S080):** `pcr` = `PAR_BAR_RESET_MODE`; direct/chain-fired
  restart; queued use qac reset bit. No second reset param.
- **Automated Scenes (S083 Q2):** only via voice edit mask from active
  Scene/track. Unavailable target skipped.
- **Per-track Scene playback (S077):** `seq_perTrackPattern[7]`, atomic SRAM1.
  Live record via Stack target override; mixer via
  `preset_getSlotPlayedScene()`. LFO/velocity resolve through active Scene.
- **Background .hcnames (S077):** must not raise `menu_storageBusy`; use
  `menu_residentNameDeferredFlushComplete()`.
- **pat_background_region (S077):** 10,528 B, three non-concurrent uses
  (AutoSave snapshot, overlap paste, Scene 17 Pattern);
  `filesystem_patternSnapshotInUse()` gates.
- **Parameter mirrors (S080):** `PAR_CHAIN_*` / `PAR_TRACK_LOOPABLE` after
  Globals; commit in `menu_cellCommitValue()`, not `menu_parseGlobalParam()`.

### Storage / Filesystem

- **Copy/clear:** exclusive holder for raw block API; top 132 B = swap block;
  publish before free; `copyClear_backgroundSuspended()` blocks
  AutoSave/trace/settings/repair. Name cache lent for 161 B HCNAMES remap.
- **No backward compatibility, ever (user, standing):** no migration, aliasing,
  or converters.
- **Wording (user, S081):** stored data are *files*, not "cards."
- **AutoSave (S074):** validators probe exact length; AsyncFATFS stores
  cluster-rounded size while open; self-recovering failures → trace only.
- **Bus compressor (S074):** St1 (panel) = `output2` = DAC1; St2 (panel) =
  `output` = DAC2. Settings page last (`MENU_GLOBAL_SCENE_SUBPAGE` 3, `+`);
  `PAR_BUS_COMP_*` never commit through `menu_parseGlobalParam()`.
- **Blank names:** `none.drm`/`none.fx`; empty stem → `inst`.
- **One pan rule (S075):** 0..127, display − 63 (`DTYPE_PM63`), stored 63 =
  centre. Stereo/mono laws may differ; display and default must match.

### DSP

- **S073 invariants:** new rows need `IM_SPECIAL_*` tag; int16 voice filter
  and float Effect filter are twins; fused chains keep every
  truncation/saturation; DMA Normal non-cacheable + `DSB` after pack; ITCM
  growth = RAM under approval policy.
- **Measuring flash:** `link_budget.py` warns < 16 KiB. Use `make all` first.

---

## Open Items

Unresolved decisions, code gaps, and deferred work.

- **Flash image check (D-C1):** keep permanently or remove — still undecided.
  `flashImage.c` checks a stamped CRC32 per sector; mismatch shows
  `Img BAD s:…` and waits for BAR1.
- **Rollback hazard:** pre-S073 firmware over a grown image plays program code
  as audio. Reinstall samples at low volume before rolling back past S073.
- **`AUTOSAVE_TRACE_RECORD_COUNT`** is still the temporary 2,048-record
  expansion in `config.h` (normal default 64). Needs a decision.
- **Production SD timing holds:** 250 ms pre-SD, paced ACMD41 (1 s timeout),
  50 ms post-mount, 50 ms pre-Bank. Motivating hang not reproducible.
- **Deferred tooling:** when logging formats settle, add a read-only `/tools/`
  script that turns copied card outputs into dated `dev_log_date.txt`.
- **Card preparation for older cards:** HCPR v2 and 145-row HCNAMES are
  rejected. Delete root `.hcnames`, `.hcnamtmp`, `.hcprms1`, `.hcprms2` once.
- **Carried small debt** (status in `074_SESSION_HANDOFF_LOG.md` §13.3):
  Makefile echoed comments and default goal; `mixer.c`/`ResonantFilter.c`
  duplicate comment lines; `presetManager.c` comment indentation; unreachable
  Scene Save phases 33–36.
- **S074 deferred:** BC11; F4 trace-ring priorities; saturator α 0.35 A/B;
  CrumpBit minimum-share run.

---

## References

- **Repository layout:** `knowledge_files/specification_reference/REPOSITORY_LAYOUT.md`
- **Session index / per-topic lookup:** `knowledge_files/log_archive/000_SESSION_INDEX.md`
- **Specification documents:** `knowledge_files/specification_reference/`
- **Open tests / hardware verification:** `knowledge_files/volatile/OPEN_TESTS.md`

---

## Project

Helicase LXR — new firmware for the LXR-02 hardware (STM32F765VIH6).
Not a port of preceding LXR-01 or LXR-02 firmware.

- This folder is the repository/codebase.
- `knowledge_files/LXR-master/` is read-only reference material from the
  original LXR (STM32F4 + ATmega644). Do not modify it.
- Session logs: `knowledge_files/log_archive/`. Specifications:
  `knowledge_files/specification_reference/`.

## General Process Reminders

- Always verify the local working repository directory before writing code.
- **Header dependency tracking is active (S070).** The Makefile uses
  `-MMD -MP` plus `-include $(OBJS:.o=.d)`, so editing a header triggers
  recompilation. `make clean` is no longer required after header edits.
- Blocking for 1 ms anywhere in the main loop or any ISR at priority ≤ 4 is unacceptable.
- Runtime SD/file work must remain asynchronous; boot-only synchronous polling is allowed before audio starts.

---

## Hardware

Full pin table, IRQ assignments and priorities, and encoder rules:
`knowledge_files/hardware_archive/HARDWARE_MAP.md`.

### MCU: STM32F765VIH6, TFBGA100

- SYSCLK = 216 MHz (HSE 16 MHz, PLLM=16, PLLN=432, PLLP=2)
- PCLK1 = 54 MHz (APB1/4) — TIM6, USART3, I2S2, I2S3
- PCLK2 = 108 MHz (APB2/2) — SPI1, ADC
- PLL48CLK = 48 MHz (PLLQ=9) — USB, RNG
- PLLI2S: N=271, R=2 → 135.5 MHz → Fs=44108 Hz
- Application origin: **0x08008000** — VTOR must be set at startup
- Stack top (SP): **0x20080000**
- DTCM 128 KB (not DMA-accessible) + SRAM1 368 KB + SRAM2 16 KB
- I-Cache / D-Cache enabled (16 KB each); MPU WT for SRAM, Normal
  non-cacheable for DMA (`.dma_nocache` region).
- `audioOutBuffer` lives in DTCM (`INDTCMZ`).

### Flash Sector Layout

| Sector | Range | Size | Use |
|--------|-------|------|-----|
| 0 | 0x08000000–0x08007FFF | 32 KB | LXRV2 Bootloader |
| 1–6 | 0x08008000–0x080BFFFF | 736 KB | Application |
| 7–11 | 0x080C0000–0x081FFFFF | 5×256 KB | Sample storage |

**Erase floor: sector 7** (`SAMPLE_FIRST_SECTOR`). `sampleFlash.c` rejects any
erase or write below sector 7 and refuses if `SAMPLE_ROM_START_ADDRESS` differs
from the linker's `__sample_flash_start`.

### Internal DAC — Must Never Be Enabled

PA4/PA5 are slider ADC inputs (RV5/RV6). The internal DAC must never be enabled;
`TIM6_DAC_IRQHandler` shares the vector by ST hardware design, not because the
DAC is in use.

---

## Audio Pipeline

Full signal path, costs, and render-loop reference:
`EFFECTS_MIXER_DSP_REFERENCE.md` (§3 mixer, §3.5 render loop).

**Main loop pattern** (`main.c` `audio_check_and_render()`):
```c
if (audioCodec_isSuspended()) return;
while (audioCodec_queueFreeSlots() > 0) {
    for (frame = 0; frame < AUDIO_DMA_FRAMES; frame += OUTPUT_DMA_SIZE) {
        voiceControl_processPending();        // triggers + automation drain
        // (seq_drainPendingAutomationTo() runs inside voiceControl)
        seq_rampService();                    // automation ramps (S081 P2)
        basepri = dsp_maskLowPriorityIrqs();  // BASEPRI 6
        mixer_calcNextSampleBlock(&buf[frame * 2], &buf2[frame * 2]);
        irq_setBasepri(basepri);
    }
    audioCodec_commitRenderBuffer();
}
```

- **OUTPUT_DMA_SIZE = 32** — DSP/control block (do NOT revert to 16).
- **AUDIO_DMA_FRAMES = 96** — DMA half; render budget 2.18 ms = 471,288 cycles.
- **SPSC queue**: 2 slots; `ready_head` ISR, `ready_tail` main loop.
- **DSP render must remain in the main loop.** DMA ISR = hard ceiling with no
  graceful degradation.
- **Mixer output names:** `output` = DAC2 = St2 (OUT2); `output2` = DAC1 = St1
  (MAIN/headphones). St1/St2 are panel labels; DAC1/DAC2 are the code names.
- **24-bit path:** `sample_mx_t` (int32_t) is signed-24; `int16 << 8` via
  `bufferTool_convertInt16ToSampleMix()`. Do NOT add `>> 8` at pack.
- **CPU-use widget:** `audioCodec_getQueueFreePercent()` — audio queue-free
  pressure, not generic MCU utilization.

---

## SD Card Architecture

**SD operations must never block the main loop or any ISR at priority ≤ 4.**

Architecture: `asyncfatfs` (Betaflight-derived), fronted by `filesystem.c`.
One operation at a time; request functions return immediately. Full spec:
`FILESYSTEM_SPEC.md`; low-level API: `ASYNCFATFS_REFERENCE.md`.

- **Hardware SPI impossible** — SD pins (PC12/PD2/PC8/PD0) have no free SPI
  mapping. Bit-bang only.
- **`afatfs_poll()`** from one context only (currently main loop). Future ISR
  migration: TIM5 at priority 6, 10 kHz.
- **Five open-file slots** (188 B each). Close directory handles after `chdir`.
  S057 disproved handle exhaustion; five suffice.

---

## Sample Flash

Samples and wavetables are installed from `Load:[Samples]`: walks `/samples`
(natural order, depth 3) and `/wavetables` (each subfolder = one wavetable),
erases sectors 7–11, installs, commits catalogue v2.

- **Accepted formats:** mono 16-bit 44.1 kHz PCM WAV.
- **Catalogue v2 (S087):** 8 KB at `0x081FE000`. 32 B header (magic "LXC2"
  programmed last), 128 × 28 B sample entries, 128 × 20 B wavetable entries.
  Details: `STORAGE_SRAM_MANIFEST.md` §4, `087_SESSION_HANDOFF_LOG.md` §5.
- Sort: natural order (`storage_naturalCompare()`). Display names: first 4 +
  last 4 for names over 8 chars; numeric prefix stripped.
- Audio payload from `0x080C0000` (sector 7) upward; catalogue at top of
  sample flash.
- Flash writes use `sampleFlash.c`; audio suspended before writes,
  reinitialized after. Pre-S073 installs are rejected.

---

## Encoder (SW42)

Full operational rules in `HARDWARE_MAP.md` "Main Encoder" section.
Dannegger difference algorithm; `encode_read1/read2` permanently removed.

---

## Display / Menu

- Full menu system wired: voice pages, global/MIDI, load/save.
- **LCD queue**: head/tail-only SPSC — do NOT reintroduce `lcd_q_count`.
- `sendDisplayBuffer` emits `lcd_setcursor` before every data byte.
- `buttonHandler_processEvents()`: `if` not `while` — intentional.
- **Knob repaint**: `menu_knobs_dirty` + `menu_serviceKnobRepaint()` is RV1-4 only.
- **endlessPots**: `atan2f(b, a)` — do NOT change argument order.
- Saturation in `menu_encoderChangeParameter` / `menu_handleLoadSaveMenu`: int16 sum + clamp.
- Runtime load: `menu_startSoundApply()` / `preset_tickDrumsetApply()`;
  do not reintroduce direct `preset_sendDrumsetParameters()` after audio.
  Worker stays active until the all-source LFO/velocity rebind cursor drains.

---

## DSP / RNG Rules

- **Compiler**: global flags are `-O2 -flto`; DSP source files use the Makefile's more-specific `-Ofast` rule.
- **FPU**: explicitly enabled in `sysclk_init()` via CPACR
- **VLAs**: forbidden in DSP voice files. Use `static int16_t buf[OUTPUT_DMA_SIZE]`.
- **`GetRngValue()`**: returns `int16_t` (truncated RNG data register).
  Audio-rate noise uses the full signed value; sequencer masks `& 0x7FFF`.
  Hardware RNG is the accepted noise character (software PRNG rejected S073).
- **Constant CPU** (DSP CPU Policy above) and **special-writer tags**
  (`INSTRUMENTS_DSP_REFERENCE.md` §4.4) apply to every DSP change.
- **`RCC_AHB2ENR`**: address is `0x40023834` — NOT `0x40023830`
- **`RNG_CR`**: direct write `RNG_CR = RNG_CR_RNGEN` — NOT `|=`
- **DTCM**: not DMA-accessible

---

## Boot / Init Order

Full 58-step annotated boot sequence: `MODULE_INTERCHANGE_SPEC.md`
"Boot / Init Order". Critical ordering constraints:

1. `EXTI_IMR = 0` must be first.
2. `lcd_init()` before `lcd_tim7_init()`.
3. All SD boot ops complete before `audioCodec_init()`.
4. `sequencerTimer_init()` (TIM3) after `audioCodec_init()`.

---

## Sequencer / PATGEN Reminders

- `seq_tick()` is owned by `TIM3_IRQHandler`. **Do NOT add it to the main loop.**
- `midiTransport_processRealtime()` and `triggerJacks_tick()` also TIM3-owned.
- `voiceControl_processPending()` only inside `audio_check_and_render()`.
- Sequencer LED feedback: `seq_ledState` → foreground `led_processSeqLedState()`.
- **S080/S084 TIM3 order:** live overlay ticks (first) → master boundary →
  chain service → deferred triggers → looper → per-track DDA → step rolls →
  FX clock.
- New pattern/track/step work through `pat_*` APIs in `PatternData.c/h`.
  `seq_patternSet`, `seq_tmpPattern`, `seq_selectedStep` are gone.
- **Per-track timing (S078):** read `seq_effectiveTrack*[]` only; never write
  the region from Morph/automation.
- DIN TX and USB MIDI writes from foreground use short critical sections.
- `EuklidGenerator.c` depends on `__CLZ(0) == 32`; do not replace the shim.
- `BUTTON_TIMEOUT` is milliseconds (`500u`), not original AVR ticks.

---

## MIDI / Clock Reminders

Full specification: `MIDI_SPEC.md` (Parts I–III). Module reference:
`MODULE_INTERCHANGE_SPEC.md` "Core/MIDI".

- `Core/MIDI/` rebuilt in S083 as single-role modules. `MidiParser`, `Uart`,
  `FIFO`, `MidiRealtime`, `MidiMessages`, `MidiNoteNumbers` are gone.
- **Consume-then-route:** `midiIn_dispatch()` offers to consumers first;
  forwards per MRT only if unconsumed. Never forward before parsing.
- **Channels 1..16, 0 = off** in MIDI code. G 0..16; track `ch` stores
  0 glb, 17 off.
- **Never cache a Scene index in MIDI code.**
- **A MIDI write is a menu edit (Q1):** through `menu_commitTargetEdit()`.
- **Note map:** `nte` is root of receive range, not a pitch. Lowest track
  wins. Default internal note 63. `MidiNoteMap` authority.
- **Track `ch`/`nte` (S083):** `ch` off/glb/1..16; `nte` off/on/notes.
  Scene stores `ch` 0..17, `nte` 0..128.
- **CC1:** on G = global Morph (fan-out, never recorded); on voice = that
  Morph (recorded). **CC2** (G only): sets PERF BAR overlay.
- **MIDI Learn:** 128 rows in `midilrn.cfg` (safe write); gesture SHIFT+PLAY+REC.
- **Transport:** Start restarts, Continue resumes from `seq_resumeTick`, SPP
  while stopped; gated by `snc`. Received Start/Stop/Continue forwarded per
  MRT but never re-sent.
- **TIM2:** shared 1 µs counter. Never reset; unsigned subtraction for deltas.
- TIM5 reserved for future `afatfs_poll()` ISR migration.
- Realtime bytes (0xF8/FA/FB/FC) route to MidiTransport ring; must not
  disturb running-status parser.

---

### Morph / Endless-Pot Reminders
- `preset_morph()` is rate-limited by `preset_morphTick()`. Descriptor Morph now runs per voice from Scene-owned instrument images through `presetMorphEngine`; global `mrp` bulk-sets all six per-voice values and (S083) the Effect Morph as an `fxm` menu edit (`effects_setMorphAmountFromGlobal()`), and LFO Morph modulation is a hidden overlay centered on the retained per-voice base.
- Do not add a morph skip cache. The request/pass generation scheduler must send a full final pass at the latest morph value.
- Legacy MorphKit load/save still uses `parameters2[]` and flat `.SND` behavior. Do not treat it as the final descriptor instrument morph persistence path.
- RV1-RV4 are analog endless pots, not the digital Gray-code encoder. The driver uses raw A/B snapshot baselines, `ENDLESS_POT_DEADZONE = 20`, `ENDLESS_POT_TIMEOUT_MS = 5000`, and `ENDLESS_POT_DELTA_TIMEOUT_MS = 20`.
- Endless-pot double angular speed applies only to Morph amounts (`menu_paramIsMorphAmount()`: `PAR_MORPH`, `PAR_VOICE1..6_MORPH`, and since S075 `PAR_EFFECT_MORPH`/PERF `fxm`) and to Effect-page 0..255 cells. Do not apply it to all `DTYPE_0B255`; BPM drift exposed that as too broad.

---

## Failed Approaches — Do Not Retry

- **Stock waveforms generated at Load:[Samples] into sample flash** (S086
  first P2): recipes in program flash, a generator, a validity tag,
  fallback tables and new variable-shift reader loops. Rejected by the user:
  not the mechanism of the existing waves. Use fixed `[11][1024]` tables
  played by the existing table loops (`tools/gen_stock_tables.py`).
- **A local variable live across a voice render** (S086 P3 as first
  delivered): LTO inlines the renders into `mixer_calcNextSampleBlock()`;
  the extra live register made two unrelated inlined per-sample loops
  reload values (+1/+2). Keep the state in the oscillator structure.

- **Boot-reader Instrument types in `fs_stage_workspace`, or six-type snapshots
  taken one Scene at a time**: Case-2 payload loads overwrite the shared union.
  Scene 0 may pass while later Scenes consume corrupt types. Retain all 96 in
  `op_bank_child_scratch.boot_reader_type` before the first load and through the
  last Scene. Do not borrow `fs_list_cache_name`; canonical fallback still
  needs its Bank index.
- **Reader-side scanning to hide stale HCNAMES child names**: durable identity
  must be corrected at the Load/Save commit boundary. Every hierarchy commit
  publishes all committed Scene/Kit/Instrument name, source, and `R` fields;
  the reader does not guess around an inconsistent register.
- **Rejecting all numeric Instrument resolutions**: only a numeric token
  directly on the Instrument row is invalid. A numeric slot inherited from its
  Kit, Scene, or Bank ancestor is valid; check the resolver's `resolved_row`.
- **Weakening the embedded-Kit filename contract to accept overlength stems**:
  `file=` stems are at most eight characters. Repair data explicitly with a
  collision-safe rename/reference update; never silently truncate.
- **Blind one-millisecond filesystem runtime pacing**: slowed Bank operations
  to tens of seconds/about a minute and delayed rather than eliminated two
  audible glitches. Bound the actual CRC/work loop by bytes across retained
  filesystem passes; do not sleep around unbounded work. Session 046, rolled
  back.
- **Bundled `/devlog.bin` consolidation and duplicate-safe append rewrite**:
  the hardware test timed out during boot, produced no DEVLOG, and left
  `/.hcprms2` at 32,768 bytes. Do not reapply the patch wholesale or allow
  logger recovery to hide the original operation failure. Session 046, rolled
  back.
- **Treating failed open as proof of absence**: AsyncFATFS append/write modes
  include CREATE, so error fall-through can create duplicate same-visible-name
  FAT entries. Complete and close a case-insensitive scan, classify
  zero/one/multiple matches, and preserve every scan/open error.
- **TIM7 idle gating**: wakeup race → freeze. Session 6.
- **Broad repaint coalescing**: freezes and lag. Session 6.
- **`val >>= 2` in `encode_read4()`**: asymmetric floor divide. Session 7.
- **`last = (new+3)&3` encoder seed**: wrong direction click. Session 4/5.
- **`RNG_CR |= RNG_CR_RNGEN`**: RMW on unpowered peripheral → stall. Session 8.
- **`ready_count` shared RMW in audio queue**: non-atomic. Session 8.
- **VLAs in DSP voice files**: silent stack overflow. Session 8.
- **DSP render in DMA ISR**: hard 2.18ms ceiling, no graceful degradation, system locks on overrun. Session 10.
- **Blocking SD calls in main loop**: f_open blocks 1–50ms, kills audio render. Session 10.
- **SD chunking at FatFS-call granularity in main loop**: f_open is still one blocking call, insufficient. Session 10.
- **Forking ChaN FatFS for async conversion**: 20+ functions need state machine conversion, 28 yield points, 6 levels of call nesting — unacceptable risk and complexity. Session 11.
- **NB_FatFS library**: C++ with heap allocation (new/delete), lambdas, global voidPtr for context, no _FS_TINY support, no FAT16, 9500 lines, designed for hardware DMA callbacks. Session 11.
- **preset_morph() sending index 127 through the old MIDI_CC packing**: `(127+1) & 0x7f = 0` → `paramNr = 0 - 1 = 65535` → wild write to `midiParser_originalCcValues[65536]`. Memory corruption. Must skip index 127. Session 12.
- **VLAs in HiHat_calcSyncBlock**: `int16_t mod1[size], mod2[size]` — silent stack corruption with -O2. Must use static arrays. Session 12.
- **SD_sendCommand() from sdcard_lxr02.c**: Deasserts CS after R1 response — breaks data transfer. Must use send_cmd_keep_cs() instead. Session 12.
- **Two back-to-back filesystem requests**: storage FSM is single-operation. Must wait for completion or ack. Session 12/17.
- **Old CC_VELO_TARGET/CC_LFO_TARGET bridge during kit load**: routing modulation-target changes through the removed parser path did not properly establish modulation targets in the merged single-MCU context. Use Preset modulation-target helpers instead. Session 13.
- **Morph skip cache**: can skip necessary DSP restores after returning to morph 0. Morph must send a complete final pass at the latest value. Session 16.
- **Endless-pot double speed for all `DTYPE_0B255`**: made global BPM drift more visible. Only `PAR_MORPH` gets double angular speed. Session 16.
- **Bare `n == 0` as asyncfatfs EOF test**: `afatfs_fread()` returns 0 while the SD buffer is still being populated — it is not an EOF signal. All EOF checks must use `n == 0 && afatfs_feof(op_file)`. Session 026.

---

## Toolchain

```
arm-none-eabi-gcc -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -O2 -flto
```

DSP source files are compiled by the Makefile's more-specific `-Ofast` rule.

Image format: `[8B "LXRV2IMG"][4B payload size LE][4B checksum LE][payload]`
Boot: hold main encoder button while powering on.
