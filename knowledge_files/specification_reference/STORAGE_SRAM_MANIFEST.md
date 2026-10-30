# Storage and SRAM manifest

Where every byte of the STM32F765VIH6's on-chip storage goes: program flash,
sample flash, ITCM, DTCM, SRAM1 and SRAM2. It also records the rules for
changing any of it.

- **Current as of:** Session 089 P2 (2026-10-30, hardware pending),
  `dev-ph7-voice_expansion`, uncommitted on `b4d3db8f`. DEV link:
  `text=681,232`, `data=424`, `bss=412,884`; flash payload 681,656 B of
  753,664 B (**headroom 72,008 B**). ITCM 13,032 / 16,384 B; DTCM statics
  4,372 B; FXBUF arena margin 3,808 B. The entries below run newest first.
- **S089 P2 changes (2026-10-30):** the WFd wavefolder voice chain
  (`S089_P2_IMPLEMENTATION.md`).
  - Engine structs: `chainType` (1 B) after `ampRampArm`. `DrumVoice` 632,
    `SnareVoice` 452, `CymbalVoice` 508 B unchanged (existing padding);
    `HiHatVoice` 516 → **520 B**, still under the 2 × `DrumVoice` slot, so
    `runtime_slots` stays **7,584 B**. No new RAM object: the fold reuses
    `transGen.midiFreq` (last depth, Q11) and `snapEg.value` (decay).
  - ITCM: 12,656 → **13,032 B** (+376 B): the five fold loops in
    `.itcm.s089` (`voiceFold_triLoop` 52, `sinLoop` 96, `asnLoop` 92,
    `chbLoop` 60, `fd2Loop` 78 B), placed after `.itcm.s085`/`.itcm.s087`
    by the linker script, so no existing ITCM function moved (only the three
    linker veneers moved to the end). The per-block entry
    `voiceFold_process()` (1,036 B) is in flash.
  - Program flash: text 676,600 → 681,232 B (+4,632 B), including the fold
    entry, the 512 B `voiceFold_decayStep` table, the descriptor rows, the
    menu helpers and the name tables.
  - **SRAM1 layout shift (not growth).** LTO placed the 1-byte `.data`
    object `pm_instrument_request_type` on a 4-byte boundary, so `.data`
    is 420 → **424 B** and `.bss` (8-byte aligned) starts at `0x20020dc8`
    instead of `0x20020dc0`. Its size is unchanged (412,884 B), but every
    `.bss` symbol (851 SRAM1 data symbols in all) sits 1–8 B higher than in
    S089 P0. DTCM is unchanged. This is the layout-shift pattern named in
    `S089_P0_NEW_SRR_DISABLE.md` F4; if hardware misbehaves with every slot
    on `T+D`, suspect layout before the fold code.
- **S088 P1 changes (2026-10-29):** fader smoothing and float-through
  distortion.
  - DTCM: `mixer_fader_smooth[6]` (24 B, `INDTCMZ`) for the block-rate
    one-pole fader EMA. Arena margin unchanged (these are `static`, not arena).
  - Program flash: `distortion_curveSampleF()` is `static inline` (no new
    symbol); the three post-chain functions shrank slightly from the merged
    float path. Net text −80 B vs P0 close.
  - bss: −8 B vs P0 close (the LUT was restored to 1,024 entries after the
    failed 2,048-entry attempt).
- **S088 P0 changes (2026-10-29):** run modes (`osc1_run`). Measured with an
  ARM `sizeof` probe: `DrumVoice` 596 → **632 B** (+36: `runMode` 1,
  `ampRampArm` 1, `lastVeloGain` 4, `lastFmDepth` 4, `OscCarrierSnapshot` 28,
  alignment); `SnareVoice` **452 B**, `CymbalVoice` **508 B**, `HiHatVoice`
  **516 B**, all with the same added fields (their snapshot fits existing
  padding). `OscCarrierSnapshot` is 28 B on ARM, asserted `<= 32`.
  - SRAM1: `runtime_slots` 7,152 → **7,584 B** (6 × 2 × 632; +432 B), plus
    `osc_itpScratch[OUTPUT_DMA_SIZE]` in `Oscillator.c` (**+64 B** `.bss`,
    the one shared itp old-carrier block, plan I1). Net **+496 B SRAM1**.
  - DTCM: `modNode_itpReserved` (+1 B, `INCCMZ`), next to
    `modNode_interpReserved`. Arena margin 3,840 → 3,839 B.
  - ITCM: 12,640 → 12,656 B (+16 B: LTO re-specialised `osc_wtbBlendBlock`
    +8 and `osc_wtbFmBlendBlock` +8, `osc_wtbRingBlendBlock` -4, because the
    new helper calls the plain wavetable copy). The helper and
    `SVF_recalcFreqSlew()` are flash code.
  - Program flash: descriptor rows, writer case, menu branches, helper;
    re-measure with `make all`.
- **S087 changes (2026-10-28/29):** major oscillator rewrite. Net RAM:
  - SRAM1: −13,441 (removed `sample_manifest[120]` + count) −5,040
    (SampleMemory caches) +560 (installer walker) +1 (install_fault) +128
    (trigger batch stack in `voiceControl_processPending`) +1
    (`seq_pending_automation_done`) = **net ≈ −17,791 B SRAM1**
  - DTCM: −128 B (`osc_interp_a/b` removed) +2 B (`modNode_interpLastStamp`
    + `modNode_interpReserved`) = **net −126 B DTCM**, arena margin
    3,712 → 3,840 B
  - Program flash: +11,756 B net (new readers, type system, catalogue v2
    code, menu formatters; minus v1 loops, `stub_silence_buffer`)
  - ITCM: 8,512 → 12,640 B (+4,128 B; S087 sample/wavetable loops in
    `.itcm.s087`, minus v1 user-sample loops removed in P1)
- **S086 changes (2026-10-27/28):** no RAM. Program flash +45,336 B:
  - the stock tables `sq3Table` and `sw3Table` (`wavetableStock.c`,
    22,528 B each, `.rodata`, below the sample-flash floor);
  - the `pos` code and four descriptor rows;
  - `crashSample` +1 B (a guard entry, 32,769 B).

  ITCM 7,792 → 8,512 B (+720 B): the stock waves' generic table-loop copies
  were inlined into the ITCM dispatchers, and `calcFmBlock.constprop.3` is
  new (184 B). The `pos` state uses `OscInfo` padding: `position`/
  `positionApplied` at offsets 18–19 and `positionBendStep` at 33.
  `sizeof(OscInfo)` stays 72 and every voice keeps its size (asserted in
  `Oscillator.h`). text 613,536 → 658,872, headroom 139,704 → 94,368 B.
- **S085 P1 follow-up, `rng` depth ramp (2026-10-27):** `DrumVoice` 592 →
  596 B (`ringDepthQ15`, +4 B; `ringSnap` sits in padding), so
  `runtime_slots` 7,104 → 7,152 B (+48 B SRAM1 bss: 430,248 → 430,296).
  ITCM 7,560 → 7,792 B (+232 B: the ramped ring copies). text 613,232 →
  613,536 B (headroom 139,704 B). RAM approved by the user (S085 P1 plan
  r3).
- **S085 P1 changes (2026-10-27):** `DrumVoice` 588 → 592 B
  (`syncClock`, +4 B), so `runtime_slots` 7,056 → 7,104 B (+48 B SRAM1
  bss). ITCM 4,168 → 7,560 B (+3,392 B: the `.itcm.s085` sync/ring
  copies, their dispatchers, `osc_renderSyncWave`, `osc_renderPlainWave`
  and `osc_buildSyncList`). DTCM unchanged. The `snc` render puts
  `syncOver[32]` (128 B) on the audio stack (SRAM2) for one block. RAM
  approved by the user (S085 P1 plan r3).
- **S084 P0 changes (2026-10-26):** +88 B measured SRAM1 `.bss` (430,112 →
  430,200), text 608,744 → 609,288; image 609,712 B (headroom 143,952 B).
  Step roll state: `seq_rollRemaining[7]` u8 removed (−7 B),
  `seq_rollWindow[7]` u16 added (+14 B), and the three duck arrays removed
  (−21 B), a net −14 B. Live overlay engine:
  `seq_liveMode/Rate/Accum/Pos/Note/Vel[7]` (42 B) + `seq_liveRhythm` (1 B)
  + the TIM2 mailbox `seq_liveHitCount[7]`, `seq_liveFlamMask`,
  `seq_liveHitUs[7]` (36 B) = 79 B. `bh_overlay[7]` 8 B → 4 B per track
  (−28 B). LiveRecord `liveRec_roll[7]` 8 B per track (+56 B) replacing
  11 B of scalars (−11 B). `pat_record_spec_t` gains `rhythm` (+4 B in the
  four staging slots). `seq_extTicksOwed` (1 B, A1). Net +87 B approved,
  +88 B measured with alignment; the P1-only build measured 430,096 B
  (−16 from the S083 close). The S084 B1 fix (overlay re-configure order) changed
  no RAM or size.
- **S083 follow-up (2026-10-22):** −1 B SRAM1. `MidiNotes` no longer has
  a `midiNotes_action` CC2 state: CC2 sets `buttonHandler`
  `bh_overlayActive`. The overlay entry field `vel` (always 100) became the
  roll-rhythm position `pos`, so the size is unchanged. `pv<`/`pv>` use spare
  `parameter_values[]` ids. The measured DEV BSS is unchanged at 430,112 B.
- **S083 `midilrn.cfg` (2026-10-22):** +1 B approved SRAM1 `.bss`
  (`filesystem.c` `fs_midilearn_write`, the MIDI Learn write request,
  owner filesystem). The measured DEV BSS is unchanged at 430,112 B because
  the byte filled alignment padding. The boot recovery reuses the four
  `op_settings_recovery_*` bytes. Text 605,784 -> 608,424.
- **S083 changes (MIDI):** measured DEV BSS 430,928 -> 430,120 (**-808 B**
  net), data 420 -> 424, text 598,120 -> 603,896. New static SRAM1, all
  approved: `MidiLearn` table 128 x 4 B = 512 B + count/state/target/deadline
  16 B (528 B with alignment); `MidiControl` NRPN contexts 2 x 8 x 3 B =
  48 B (exact); `buttonHandler` per-track overlay table 7 x 8 B = 56 B
  replacing the 9 B single slot (R-A, +47 B aligned); `MidiOut` note-off
  record 7 x 2 B = 14 B replacing the 16 + 2 B channel cache (R-B) and the
  parameter TX state 8 B; `midiIn_config` 3 B and the 6 B in-progress DIN
  message replacing the legacy channel/filter/parser state (R-D); DIN RX
  stamps 64 x 2 B = 128 B; the USB ring is 128 x 6 B = 768 B (the pre-S083
  ring was 256 x 8 B = 2,048 B: the old `MidiMsg` measured 8 B, not the
  plan's 4 B). `MidiBits` is one byte of `uint8_t` bitfields so the new
  `MidiMsg` is 6 B. `seq_resumeTick` 4 B (R-C) and the 1 B PC-origin flag.
  Retired: `midiParser_originalCcValues[255]`, `midi_NoteOverride[7]`,
  `midi_MidiChannels[8]`, `midi_chan_notes[16]` + `midi_notes_on` (18 B),
  the 9 B overlay slot and the parser statics. A release does not
  authorise reuse (MEMORY.md policy).
- **S082 P3 changes:** +4 B approved SRAM1 (D47): BankData
  `bank_load_unavailable_mask` 2 B, `ledHandler.c` `led_fastBlinkLed[]`
  +1 B (1 → 2 slots), `menu.c` `menu_bgLoadAvailCount` 1 B. The measured DEV
  BSS grew by 8 B because the new bytes changed section alignment; no page or
  pool was enlarged. `PM_BG_UNLOCKED` is spare bit 3 of `pm_bg_phase`, and
  the frozen-screen Bank name reuses `op_bank_display_name`; both add no RAM.
- **S082 P2 changes:** no production SRAM growth. `filesystem.c`
  `fs_stall_poll_uncounted` is 1 B inside `DEV_STALL_DETECTION` builds only
  (idle-time Bank-load ticks stay invisible to the poll-count stall
  observers). The measured DEV BSS is unchanged from S082 P1 because the byte
  filled existing alignment padding. F1 and F2 add no static storage.
- **S082 P1 changes:** +93 B SRAM1 from S082 P0: 91 B BSS (`filesystem.c`
  `fs_background_name` 90 B, the 10 × 9 B name capture for Scene 17 identity
  names, §8.2; `presetManager.c` `pm_bg_result` 1 B, one-shot result
  register) and 2 B `.data` (`presetManager.c` `pm_bg_pending_slot`, deferred
  MIDI Bank load slot, initialiser `0xFFFF`, which also adds 2 B of flash
  initialiser). While a load is in flight `pm_bg_pending_slot` doubles as the
  bounded-wait start tick (S082 P1 assessment A8), with no further storage.
- **S081 changes** (logged after S082; `081_SESSION_HANDOFF_LOG.md` §10): DEV
  link `text=556,008 → 573,296`, `data=412 → 416`, `bss=427,976 → 429,112`
  (**+1,136 B BSS, +4 B data**; flash payload 573,712 B, headroom 179,952 B).
  All static SRAM1, user-approved. P1 live record ≈ +428 B every build and
  ≈ 80 B released (Effect owner/pending/held-Morph state, legacy roll bytes,
  `recordSubStep`); P1B ≈ +61 B; P1C +10 B; P2 automation ramps +686 B
  (`seq_ramps[64]` 640 B at 10 B per slot incl. the D11 `last` byte,
  `seq_rampCtx[7]` 28 B, `seq_rampBlockDiv` 1 B, `track_auto_smooth` 16 B in
  `pat_regions` + 1 B in `pat_background_region`); P3 none static (+12 B per
  `seq_track_time_t` stack local); G-1 +7 B (`liveRec_morphValue[7]`, BSS +8
  with padding); P4 +2 B `.data` (reported +4 B by `ALIGN(4)`). DEV-only:
  `liveRec_drops` 14 B, `seq_recRingDrops` 2 B, `seq_rampNoSlotDrops` 2 B.
  `effects_automation` 184 → 120 B, `effects_state` 84 → 80 B. Region size
  10,527 → **10,528 B**. Owners in §8.2. The production build was not
  re-measured in S081.
- **S080 changes:** +200 B BSS (+4 B data) from S079 (PERF performance
  surface, `080_SESSION_HANDOFF_LOG.md` §8). Owners: `sequencer.c` looper
  state 67 B (`seq_loopActive`, mask/division/modifier 3 B,
  `seq_loopStartStep[7]` 14 B, `seq_loopStartAccum[7]` 14 B,
  `seq_loopStartPlayState[7]` 7 B, `seq_loopAnchor[7]` 28 B), scene-change
  pending state 7 B, chain runtime 5 B (`seq_chainBarCount`,
  `seq_chainRepCount`, `seq_previousScene`, `seq_chainStopped`,
  `seq_pendingChainFire`); `buttonHandler.c` looper holds 5 B, overlay
  runtime 10 B (`bh_overlayActive`, voice mask, track, note, velocity,
  count, interval 2 B, next 2 B), SHIFT+REC 3 B, `bh_shiftReleaseConsume`,
  `bh_chainPlayArmed`; `ledHandler.c` fast blink 5 B (two `led_fastBlinkLed`
  slots since S082 P3, `led_fastBlinkPhase`,
  `led_fastBlinkPhase`, `led_fastBlinkNext`); `menu.c`
  `menu_chainMenuScene` 1 B; `scenes` +64 B (16 × 4 B chain settings,
  26,496 B); `pat_regions` +16 B and `pat_background_region` +1 B
  (`track_loop_exclude`, 10,527 B per region); plus alignment. The P2 table
  was presented under the allocation policy before implementation. Flash:
  +11,936 B text (looper/chain engines, PERF UI, name tables: 79 chain
  conditions, 44 next values, 18 qcd, 9 qac, 12 overlay types).
- **S079 changes:** +160 B BSS from S078. P1: `sequencer.c`
  `seq_trackRepeatCount[7]` + `seq_trackLastConditionFired[7]` +
  `seq_fillActive` + per-track roll state arrays (~67 B, padded to 80 B).
  P2: `InstrumentManager.c` `gateFade[6]` (24 B) + `gateFadeStep[6]`
  (24 B) + `gateDuck[6]` (6 B) = 54 B padded to 56 B; `sequencer.c`
  `seq_rollDuckDelta/Delay/Active[7]` = 21 B padded to 24 B; total +80 B.
  Flash: P1 +3,968 B text, P2 +168 B text (removals offset new code).
  S084 P1 removed the duck arrays and replaced `seq_rollRemaining` with the
  u16 `seq_rollWindow` (net −14 B before alignment).
- **S078 changes (approved at plan acceptance):** +608 B BSS from S077.
  `sequencer.c` +76 B ISR-static timing state (per-track Q8.8 accumulators
  14 B, FX accumulator 2 B, FX step counter 4 B, shuffle delay/pending/
  velocity/note 28 B, play state 7 B, effective length/scale/shuffle 21 B);
  `presetMorphEngine.c` +24 B track step-override state
  (`track_param_override_value[21]` + `track_param_override_mask[3]`);
  `scenes` +352 B (16 × 22 B: 21 B `track_morph_*` endpoints + alignment);
  `pat_regions` +112 B and `pat_background_region` +7 B
  (`track_play_mode[7]` per region). The P2 STEP overlay's 1 B presence byte
  was added and removed again in P3 (released, not reused). Flash: the
  256 B Q8.8 step-scale LUT, the 14-stop label table, run-mode names and
  21 Scene target rows; text +1,440 B over the session.
- **S077 changes:** +264 B BSS (256 B `copyClearService.c:ccSvc_snapTable[128]`
  static paste source table + 8 B alignment). `PatternData.c:pat_background_region`
  (10,519 B) replaces `pat_autosave_snapshot` at identical size — no net change
  from that substitution. The 9 kB name-buffer loan now carries only the
  161 B HCNAMES remap (paste table and source blocks moved to the static table
  and `pat_background_region.pool`). S077 text growth was +2,440 B (P1–P6).
- **S076 changes:** +264 B BSS (28-byte `lfo_scene_handoff` struct + alignment
  in S076 P2). +3,584 B text. The S075 F1 production measurement needs a
  rebuild.
- **S075 changes:** The F1 pass
  adds +124 B production SRAM1 net: early source masks +64 B, restore masks
  +64 B, early flags +1 B and governor credit +2 B, offset by the removed
  group-blink state −7 B. The 9,000 B name cache doubles as copy/clear
  working storage only during an intentional lazy loan (§8.2). Every Scene
  pool keeps a permanent 132 B swap block (§8.2). F2 adds the six-byte
  `fx_send_morph` endpoint array to each Scene, the 4-byte Effect-page voice
  mix overlay record, and the 1-byte overlay TRACK mask; the measured DEV
  `scenes` symbol is `0x65E0`.
- **S074 changes:** +64 B SRAM1 (Scene settings for the bus compressor);
  +32 B DTCM `.dtcmz` (bus compressor state), so the FX arena is −32 B;
  CrumpBit uses 0 B of static RAM (56 B inside the existing 76 B union)
  and 70,592 B of the arena share at run time; the image grew into sector 6
  (§3.2a).
- **Renamed in Session 073** from `SRAM_MANIFEST.md`. The flash and sample
  flash material came from the Session 073 flash expansion
  (`073_SESSION_HANDOFF_LOG.md` §4).
- **SD card storage** is covered by other documents (§9).

---

## 1. How to read and refresh these numbers

- Build clean before trusting any total: `make clean && make all`. Use
  `make all`, not bare `make`: in an incremental tree bare `make` can stop at
  `build/main.o`.
- Flash, ITCM, DTCM and arena use:
  `python3 tools/link_budget.py arm-none-eabi-nm build/lxr02.elf`

  ```
  Flash : 659,296 / 753,664 B used, headroom 94,368 B
  ITCM  : 8,512 / 16,384 B
  DTCM  : statics 4,472 B
  FXBUF : 126,592 B at 0x20001180 (min 122,880, margin 3,712)
  ```

  It warns when flash headroom falls below `LINK_BUDGET_WARN_FLASH`
  (default 16,384 B). The flash limit comes from the linker symbol
  `__sample_flash_start`.
- Sections: `arm-none-eabi-size -A build/lxr02.elf`. Symbols:
  `arm-none-eabi-nm -S --size-sort build/lxr02.elf`. Segments:
  `arm-none-eabi-readelf -l -W build/lxr02.elf`.
- **The `bss` column of `arm-none-eabi-size` includes the 126,592 B NOLOAD
  DTCM arena.** It is not SRAM1 use. Use the section ledger (§5).
- **Record the `.img` hash, not the `.bin`.** Since S074 `lxr02.bin` is the
  raw, unstamped objcopy output; the stamped payload exists only inside
  `build/LXRV2_lxr02.img` (§3.4).
- Individual C objects can be merged, removed or padded by LTO and
  alignment. Use section totals for capacity.

---

## 2. Memory map

| Region | Address range | Size | Contents |
|---|---|---:|---|
| Flash sector 0 | `0x08000000–0x08007FFF` | 32 KiB | LXRV2 bootloader (closed vendor binary; the only recovery path) |
| Flash sectors 1–6 | `0x08008000–0x080BFFFF` | 736 KiB | Application image (§3) |
| Flash sectors 7–11 | `0x080C0000–0x081FFFFF` | 1,280 KiB | User samples (§4) |
| ITCM | `0x00000000–0x00003FFF` | 16 KiB | Hot oscillator code copied from flash at boot (§6) |
| DTCM | `0x20000000–0x2001FFFF` | 128 KiB | DSP statics, then the FX/voice audio arena (§7). Not DMA-accessible. |
| SRAM1 | `0x20020000–0x2007BFFF` | 368 KiB | DMA buffers, `.data`, `.bss` (§5, §8) |
| SRAM2 | `0x2007C000–0x2007FFFF` | 16 KiB | The main stack, growing down from `_estack = 0x20080000`; an optional 12 B logging capsule |

Flash is single-bank (confirmed by memtest). Sectors 0–3 are 32 KiB, sector 4
is 128 KiB, sectors 5–11 are 256 KiB.

---

## 3. Program flash

### 3.1 Application window

- **Window:** `0x08008000–0x080BFFFF` (753,664 B), sectors 1–6. Session 073
  added sector 6 (it was sample storage before); the window was 480 KiB.
- **Use at the S086 close DEV link:** 659,296 B; **headroom 94,368 B**
  (S085 close 139,704 B; S086 added the 45,056 B stock tables).
- **Use at the S075 close DEV link:** 532,824 B; headroom 220,840 B.
- **History:** 34,356 B free at S072 Step 1; 8,080 B at the S072 close;
  269,504 B after the S073 expansion; 266,560 B after the S073 DSP
  refactor; 250,736 B at the S074 close; 221,312 B at the S075 F2 DEV link;
  220,840 B at the S075 close (F3). S075 added +29,896 B in total
  (copy/clear about 25 KB with the DEV trace).
  S074 added +15,824 B, of which CrumpBit was 11,600 B.

### 3.2 Image layout

The linker script is `STM32F765VIHx_FLASH.ld`. The image is, in order:

| Section | Size (S075 close DEV) | Notes |
|---|---:|---|
| `.isr_vector` | 456 B | At `0x08008000`; VTOR is set at startup |
| `.image_check` | 32 B | Per-sector CRC block (§3.4); `(READONLY)`, so `size` does not count it as data |
| `.text` | 527,240 B (S075 close DEV; 526,768 at S075 F2; 497,344 at S074) | `Reset_Handler` first (`KEEP(*(.text.Reset_Handler))`), then code and `.rodata` |
| `.itcm` load image | 4,168 B | Copied to ITCM by `Reset_Handler` |
| `.data` load image | 416 B | Copied to SRAM1 |
| `.dtcm` load image | 512 B | Copied to DTCM (`squareRootLut`) |

- **`_eflash_load`** `= LOADADDR(.dtcm) + SIZEOF(.dtcm)` is the end of the
  image. `lxr02.bin` is exactly `_eflash_load − 0x08008000` bytes, because
  `objcopy -O binary` emits loadable sections only. NOLOAD sections
  (`.dtcmz`, `.dtcm_fxbuf`, `.bss`) never enter the image.
- **`Reset_Handler`** stays at `0x080081E8`, in sector 1, however large the
  image grows.
- **Largest flash objects** (S074 close):

  | Object | Bytes |
  |---|---:|
  | `crashSample` | 32,768 |
  | `transientData` | 26,460 |
  | `sawTable`, `triTable`, `recTable` | 22,528 each |
  | `mixer_calcNextSampleBlock` (all four voice renders, the mixer and the bus compressor inlined by LTO) | 12,204 |
  | `menu_repaintGeneric` | 9,508 |
  | `main` | 9,048 |
  | `sine_table` (in flash since S072) | 8,194 |
  | `filesystem_tick` | 7,924 |
  | `filesystem_loadSceneDirectory_tick`, `filesystem_autosaveParameterDrain_tick` | 7,872 / 7,612 |
  | `crumpBit_process` / `crumpBit_syncDivision` (S074, `-Ofast` unrolled) | 4,804 / 3,684 |

### 3.2a What lives in sector 6 (S074)

The application reached sector 6 (`0x08080000`) for the first time with
CrumpBit (`_eflash_load` `0x08081F68`, 8,040 B in). At the S074 close
`_eflash_load` was **`0x08082C90`, 11,408 B into sector 6**, laid out as
below. **At the S075 close it is `0x0808A158`, 41,304 B into sector 6**
(sector 6 is 256 KiB); the same tail order applies, shifted up by the S075
code growth. The S074 table:

| Range | Content |
|---|---|
| `0x08080000–0x080818A7` | the last 6,312 B of `.text`: libm tables (`__exp2f_data`, `__log2f_data`, `__powf_log2_data`), `_ctype_`, `atan` tables, `_init`/`_fini`, and the flash veneers into ITCM |
| `0x080818A8–0x080828EF` | the `.itcm` load image (4,168 B of oscillator code) |
| `0x080828F0–0x08082A8F` | the `.data` load image (416 B) |
| `0x08082A90–0x08082C8F` | the `.dtcm` load image (`squareRootLut`, 512 B) |

Every S074 image has booted and played, with the boot image check silent,
so **the LXRV2 bootloader erases and programs sector 6**. If it did not,
the oscillators, initialised data and pan law would be corrupt, and the
check would report `Img BAD s:.....6`. The user did not run a separate
test.

### 3.3 Linker guards

The link fails rather than produce a bad image:

| ASSERT | Guards |
|---|---|
| `ORIGIN(FLASH) + LENGTH(FLASH) == __sample_flash_start` | The application window ends exactly at the sample floor |
| `_etext <= __sample_flash_start`, `_eflash_load <= __sample_flash_start` | Code and load images never reach sample flash |
| `.image_check` is 32 B and ends below `0x08010000` | The check block stays in sector 1 |
| `Reset_Handler < 0x08010000` | The reset vector stays in sector 1 |
| `.itcm` fits 16 KiB | ITCM overflow |
| `.dma_nocache` ≤ 4,096 B | It must fit MPU region 1 |
| `.dtcm` + `.dtcmz` fit DTCM | DTCM overflow |
| FX arena 32-byte aligned, inside DTCM, ≥ 120 KiB | The approved arena minimum (§7) |
| `.devwdg_noinit` ≤ 32 B | The approved logging capsule |

The Makefile makes the linker script a prerequisite of the ELF, so a layout
edit relinks without `make clean`.

### 3.4 Boot image check

- **Why:** the closed bootloader had only ever written images up to about
  483 KB when the window grew to 736 KiB. If it fails to erase or program a
  sector the image reaches, the result would otherwise be silent code
  corruption.
- **Block:** 32 B right after the vector table, in sector 1: word 0 magic
  (`IMC?` placeholder in the ELF, `IMCK` when stamped), words 1–6 one CRC32
  per application sector 1–6 over that sector's share of
  `[0x08008000, _eflash_load)` with the block's own bytes skipped, word 7
  the image length. Sectors the image does not reach hold the CRC of nothing
  (0).
- **Stamping:** `tools/build_lxrv2_img.py` (`make img`) rewrites the block
  in the image payload (Python `zlib.crc32`: reflected CRC-32, polynomial
  `0xEDB88320`, init and final `0xFFFFFFFF`). Since S074 it is the only image
  script (the separate `stamp_image_check.py` was folded in) and `lxr02.bin`
  stays the raw, unstamped objcopy output. If the binary does not have the
  promised layout the script fails, deletes any previous `.img` and exits
  non-zero, so no unstamped or stale image can be copied to the card.
- **Checking:** `flashImage_verifyAtBoot()` (`Core/Hardware/flashImage.c`)
  runs in `main.c` after `din_init()` and `time_initTimer()`, before any
  sample, DSP or storage code. It uses a 16-entry nibble table, no RAM
  beyond locals, about 20 ms.
  - Silent on success.
  - Mismatch: `Img BAD s:<sector digits>` / `Reflash. BAR1=go`. For example
    `Img BAD s:.....6` means sector 6.
  - Unstamped image: `Img unstamped`.
  - After a report, boot waits for a BAR1 (PB7) press and release, then
    continues, so a checker fault can never stop a good image.
- **Keep in sync:** the sector table appears in the script (`SECTOR_ENDS`) and
  in `flashImage.c` (`flashImage_sectorEnd`).
- **Open decision (D-C1):** whether to keep the check permanently. It is
  kept for now.

### 3.5 Image packaging and the bootloader

- `make img` runs `tools/build_lxrv2_img.py`: the image check stamp (§3.4),
  then a 16-byte header (`LXRV2IMG`
  magic, payload size little-endian, an **8-bit** additive checksum as a
  32-bit word) and the payload. The packer has no size check; the linker
  ASSERTs are the guard.
- To update: copy `build/LXRV2_lxr02.img` to the card root, hold the main
  encoder, power on. Keep exactly one `.img` in the card root and delete
  macOS `._*` files, since a bootloader that matches the 8.3 alias or the
  first `*.IMG` could flash the wrong file.
- **Known:** a normal-size update leaves sample sectors intact. The Erica
  factory application (`LXRV2_update_v1.70.img`, 275,832 B) has no
  flash-writing code, so it tells nothing about larger images.
- **Settled in practice (S074):** the bootloader writes images that reach
  sector 6 (§3.2a). Whether it erases by image size or a fixed range is
  still not known in detail, and it does not matter while images keep
  booting. The boot image check stays as the guard. The unexecuted test plan
  is in `073_SESSION_HANDOFF_LOG.md` Appendix A.
- **Recovery:** power on holding the encoder with a known-good image on the
  card. It depends on sector 0 staying intact.

### 3.6 If flash runs short again

Ranked growth paths (S072 study):

1. `-Os` for cold control modules (`menu.c`, `filesystem.c`,
   `presetManager.c`, `storageTypes.c`) through per-file Makefile rules;
   several KiB; needs UI and filesystem timing checks.
2. Large constant tables (`crashSample`, the wavetables, `transientData`) as
   data installed into the sample region; up to about 124 KiB; needs an
   install path and a reserved span.
3. Dead legacy code: the Euklid and SOM generators are compiled in although
   `ENABLE_EUKLID_PAGE 0` hides their page.
4. **Grow the application window into sector 7** (S086 study): app 736 →
   992 KiB, sample flash 1,280 → 1,024 KiB. Every table stays an ordinary
   `const` array (no code or CPU change). Changes: the linker `FLASH`
   length and `__sample_flash_start`, `SampleMemory.h`
   `SAMPLE_ROM_START_ADDRESS`/`SAMPLE_FIRST_SECTOR`, `build_lxrv2_img.py`
   `SECTOR_ENDS` and the image-check CRC block (seven sectors). Needs a
   hardware check that the closed LXRV2 bootloader writes the longer image,
   and one reinstall of samples. (Option 2's install-time generation was
   tried for the stock waves in S086 and rejected: not the existing
   mechanism.)

Do not erase sector 6 before updates to work around a bootloader problem: it
only works while sector 6 holds no code.

---

## 4. Sample flash (sectors 7–11)

### 4.1 Layout (catalogue v2, S087)

| Region | Range | Size |
|---|---|---:|
| Sample/wavetable audio | `0x080C0000–0x081FDFFF` | up to 1,302,528 B |
| Catalogue v2 | `0x081FE000–0x081FFFFF` | 8,192 B |

**Catalogue v2 structure (8 KB at `SAMPLE_CATALOGUE_START_ADDRESS`):**
- Header (32 B): magic `0x4C584332` ("LXC2", programmed last),
  version, `sample_entry_bytes` (28), `wavetable_entry_bytes` (20),
  `sample_count`, `wavetable_count`, `data_end`, flags, reserved
- Sample entries (128 × 28 B): `start_address`, `frame_count`,
  `loop_start`, `loop_end`, `gain_code`, `bits`, `flags` (bit 0 = loop
  present), `name[8]`
- Wavetable entries (128 × 20 B): `start_address`, `frame_len`,
  `frame_count`, `bits`, `flags`, `name[8]`, padding

Items from `0x080C0000` (sector 7) upward. The v1 layout
(`SampleInfo[120]` at `0x081FF6A0`, display names at `0x081FFC40`) is
removed; a v1 catalogue at boot shows `Samples invalid` / `reload
samples`. The S086 stock-wave flash block is not needed.

- Before Session 073 the audio started at `0x08080000` (sector 6).
- RAM caches of the v1 table (SRAM1, `SampleMemory.c`, 5,040 B) are
  removed in S087. The catalogue v2 entries live in flash and are read
  directly.

### 4.2 Guards

- `sampleFlash.c` refuses any erase or program below `SAMPLE_FIRST_SECTOR`.
- `_Static_assert`s tie `SAMPLE_ROM_START_ADDRESS` to the base of
  `SAMPLE_FIRST_SECTOR`.
- A runtime interlock refuses every erase and write if
  `SAMPLE_ROM_START_ADDRESS` differs from the linker's
  `__sample_flash_start`.
- `memtest.c` uses the same floor (`ERASE_SECTOR_FLOOR`).
- D-cache is invalidated over the erased or programmed range.

### 4.3 Installing (S087 catalogue v2)

- Load:[Samples] runs `menu_loadSamplesModal()`: suspends audio, erases
  sectors 7–11, walks `/samples` (files first, subfolders, natural order,
  depth limit 3) then `/wavetables` (each subfolder = one wavetable, `.wav`
  files inside = frames), and commits the catalogue.
- Only mono 16-bit 44.1 kHz PCM WAV files are accepted.
- `sampleInstall_begin()`: erases sectors 7–11; nothing programmed.
- `sampleInstall_commit()`: programs header words 1–7 first, magic word 0
  last. An interrupted install reads as EMPTY.
- `sampleInstall_abortItem()`: discards buffered bytes, never rewinds the
  cursor onto programmed flash. Lost space is accepted.
- `install_fault`: set by failed erase or write; stops further writes.
- Display names: numeric prefix stripped (`01_kick.wav` → `kick`); names
  over 8 chars show first 4 + last 4 characters.
- Wavetable limits: any frame length, at most 128 frames.
- Gain code: `2^(code/24)`, peak within 0.25 dB of full scale.
- A pre-S073 install is rejected (0 samples): its entry 0 is at
  `0x08080004`, below the floor.
- **Rollback hazard:** a pre-S073 firmware on a unit that has a pre-S073
  sample index and a grown S073+ image in sector 6 would play program code as
  audio. Reinstall samples (at low volume) before rolling back past S073.

---

## 5. Static RAM ledger (Session 080 close DEV link)

| Region and section | Start | Capacity | Static bytes | Free |
|---|---|---:|---:|---:|
| SRAM1 `.dma_nocache` | `0x20020000` | part of SRAM1 | 3,100 | — |
| SRAM1 `.data` | `0x20020c1c` | part of SRAM1 | 412 | — |
| SRAM1 `.bss` | `0x20020db8` | part of SRAM1 | 294,324 | — |
| **SRAM1 total** | `0x20020000` | **376,832** | **297,836** | **78,996** |
| DTCM `.dtcm` | `0x20000000` | part of DTCM | 512 | — |
| DTCM `.dtcmz` | `0x20000200` | part of DTCM | 3,960 | — |
| DTCM `.dtcm_fxbuf` (arena) | `0x20001180` | part of DTCM | 126,592 | 0 (reserved arena) |
| **DTCM total** | `0x20000000` | **131,072** | **131,064** | **8** |
| ITCM `.itcm` (code) | `0x00000000` | 16,384 | **12,640** (S087) | 3,744 |
| SRAM2 `.devwdg_noinit` | `0x2007c000` | 16,384 | 0 | see stack note |

- Static data RAM (SRAM1 + DTCM including the arena) is 428,900 B;
  including ITCM code, 433,068 B.
- **Session 080 changes:** `.bss` +200 B, `.data` +4 B over S079 (owners in
  the header and §8.2). DEV link `text=556,008`, `data=412`, `bss=427,976`;
  flash payload 556,420 B.
- **Session 081 changes:** `.bss` 427,976 → 429,112 (+1,136 B), `.data`
  412 → 416 (+4 B) over S080; DEV link `text=573,296`, `data=416`,
  `bss=429,112`, flash payload 573,712 B. Parts: P1 live record ≈ +428 B
  (≈ 80 B released), P1B ≈ +61 B, P1C +10 B, P2 automation ramps +686 B, P3
  none static but a +12 B stack frame for `seq_track_time_t` (approved), G-1
  **+7 B** `liveRec_morphValue[7]` (reported +8 B with alignment), P4 **+2 B**
  `.data` `led_barPulseTrack`/`led_barPulseBar` (0xFF-initialised; `.data`
  `ALIGN(4)` reports +4 B; the user accepted this as is). Owners in the
  header and §8.2. The table above is still the S080 section breakdown; the
  S081/S082 sections were not re-tabulated.
- **Session 079 changes:** `.bss` +160 B (owners in the header). DEV link
  `text=544,072`, `data=408`, `bss=427,776`.
- **Session 078 changes:** `.bss` +608 B (owners listed in the header and
  §8.2). DEV link `text=539,856`, `data=416`, `bss=427,616`; flash payload
  540,272 B.
- **Session 077 changes (approved: +256 B S077 allocation):** the only new
  owner is `copyClearService.c`: `ccSvc_snapTable` (256 B, the static paste
  source table). `pat_background_region` (10,519 B) replaces the former
  `pat_autosave_snapshot` at identical size, so the section moved +264 B
  (the 256 B table plus 8 B of section alignment). DEV link `text=536,040`,
  `data=416`, `bss=427,008`; flash payload 536,456 B.
- **Session 076 changes:** `lfo_scene_handoff` (28-byte struct plus
  alignment, +32 B `.bss`). DEV link `text=535,976`, `data=416`,
  `bss=426,744`.
- **Session 075 changes (approved: +101 B F2 allocation ledger):** the F1
  implementation and F2 overlay/data additions produce the current DEV
  `.bss` 293,060 B, `.data` 416 B, DTCM `.dtcmz` 3,960 B (the FX arena is
  unchanged). F2's new owners are `SceneData.c:scenes` +96 B for
  `fx_send_morph[6]`, `menu.c` +4 B for the overlay record, and
  `buttonHandler.c` +1 B for its TRACK mask.
  The earlier F1 owners remain: `copyClearSession.c` 25 B
  (`cc_state` 6, `cc_source` 6, `cc_rowStack` 8, row count 1, edge masks 4);
  `copyClearService.c` 59 B (queue 24 + head/count 2, register 16 + count/Scene
  2, run state 6, flags/claim 2, retry counter 2, name-buffer pointer 4);
  `service_exclusive_scene` 1 B; `fs_name_cache_borrowed` 1 B; the F1
  retired-decimation transition reduced `scenes` by 32 B
  (`voice_decimation_all` removed: settings 45 → 44 B, 2 B per record with
  alignment) before F2 appended `fx_send_morph[6]` and restored the current
  50 B layout. `mixer_decimation_rate[]` (DTCM, 28 → 24 B) accounts for the
  `.dtcmz` change. The remainder is LTO placement and alignment.
- **Session 074 changes (all approved):**
  - SRAM1 `.bss` +56 B. `scenes` grew +64 B (`scene_settings_t` 41 → 45 B
    for `bus_comp[4]`); the section total moved 56 B after alignment
    and LTO placement.
  - DTCM `.dtcmz` +32 B (`busComp`, 24 B then 32 B with the saturation
    crossover state); `_edtcmz` `0x20001160` → `0x20001180`, so the arena
    shrank by 32 B.
  - ITCM unchanged.
- **Session 073 changes:** ITCM +400 B (`osc_setFreq()` is now its own ITCM
  function; before it was inlined into its callers in flash). SRAM1 and DTCM
  unchanged.
- **Stack:** `_estack = 0x20080000`, the top of **SRAM2**, growing down. It
  has no linker reservation and no measured high-water mark, and it is not in
  these totals. SRAM2 is therefore not free feature RAM. The disabled
  `DEV_LOGGING_IWDG` capsule would place 12 B at SRAM2's base; it is 0 B in
  this configuration.

---

## 6. ITCM contents

Code placed with `INITCM` (`config.h`) is copied from flash to ITCM at boot
and runs with zero wait states.

| Function | Bytes |
|---|---:|
Sizes at the S087 close (12,640 B total; the v1 user-sample loops are
removed, replaced by the `.itcm.s087` readers):

| Function | Bytes |
|---|---:|
| `calcNextOscSampleBlock` (two specialisations) | 1,016 + 960 |
| `calcNextOscSampleFmBlock` | 468 |
| `osc_setFreq` (standalone since S073) | 404 |
| `calcSampleOscFmBlock` | 196 |
| `calcFmBlock` (three specialisations, plus S086's variable-table one) | 192 × 3 + 184 |
| Veneers for the interpolation blocks, `memcpy` | 8 × 3 |
| S085 P1: `calcNextOscSampleSyncBlock` / `calcNextOscSampleRingBlock` (dispatchers) | 404 / 576 |
| S085 P1: `osc_renderSyncWave` / `osc_renderPlainWave` | 276 / 876 |
| S085 P1: `osc_buildSyncList` | 54 |
| S085 P1: sync copies (table / sine / crash) | 176 / 172 / 176 |
| S085 P1: ring copies, depth ramped (table / sine / noise / crash) | 192 / 184 / 108 / 196 |
| S085 P1: interpolated sync/ring variants | inlined into the two dispatchers |
| S087: `osc_smp*Block` / `osc_slp*Block` (8 loops) | in `.itcm.s087` |
| S087: `wtb` blend copies (plain / FM / ring / sync) | in `.itcm.s087` |

Removed in S087 P1: `calcUserSampleOscBlock` (396 B),
`calcUserSampleOscFmBlock` (444 B), v1 user-sample ring copy (448 B),
`osc_syncWaveform()`. Net ITCM: 8,512 → 12,640 B.

- Only oscillator code is placed here. Filter and distortion ITCM placement
  (`ENABLE_EFFECT_INITCM_CODE`) is off: it measured worse in Session 023,
  because the forced out-of-line call costs more than it saves with I-cache
  on.
- ITCM is RAM under the approval policy (§10). Report code growth here with
  its byte count and owner.

---

## 7. DTCM

DTCM is single-cycle, uncached, and not reachable by DMA.

| Object | Bytes | Placement and owner |
|---|---:|---|
| `squareRootLut` | 512 | `.dtcm` (initialised); constant-power pan law |
| `audioOutBuffer`, `audioOutBuffer2` | 1,536 each | `.dtcmz`; the two render slots per DAC, `sample_mx_t`, written by the mixer and read by the DMA ISR's pack |
| `velocityModulators` | 264 | `.dtcmz`; six velocity modulation nodes |
| `mixer_fx_bus` | 256 | `.dtcmz`; two 32-frame channels, `sample_mx_t` while voices sum, float while the Effect runs |
| `effects_runtime` | 76 | `.dtcmz`; union holding the active Effect type's DSP state (StereoFilter 76 B: two filter states; CrumpBit 56 B, S074). Only 20 B spare before the union grows. |
| `busComp` | 32 | `.dtcmz` (S074); master bus compressor state: smoothed power, fast and memory gain reduction, previous gain, pending sidechain weight, crossover low-pass per channel, active pair. Approved up to 32 B; `_Static_assert(<= 32)`. Owner `BusCompressor.c`. |
| ~~`osc_interp_a`, `osc_interp_b`~~ | ~~64 each~~ | removed S087 P1; 128 B released to arena |
| `modNode_interpLastStamp`, `modNode_interpReserved` | 1 + 1 | `.dtcmz` (S087); wavetable interpolation slot management |
| `mixer_decimation_rate` [7], `mixer_decimation_cnt` [6], `mixer_voice_samples` [6] | 28 + 24 + 12 | `.dtcmz`; per-slot decimators |
| `mixer_voice_last_gain`, `mixer_send_last_gain` | 24 each | `.dtcmz`; per-slot dry and send ramp origins |
| `mixer_fx_return_last_gain` | 8 | `.dtcmz`; Effect return ramp origins |
| `mixer_audioRouting` | 6 | `.dtcmz` |
| `modNode_waveInterp*` | 6 | `.dtcmz` |
| **`.dtcm_fxbuf` arena** | **126,592** | NOLOAD, never copied or zeroed; owned by `FxBuffer` |

- **The arena** is every DTCM byte after `.dtcmz`, 32-byte aligned. A linker
  ASSERT keeps it at least 120 KiB (122,880 B); the margin is 3,712 B.
  **Any new `INDTCM`/`INDTCMZ` static shrinks it**, in 32-byte steps (the
  arena base is 32-byte aligned). The S074 bus compressor state is the
  example: 24 B cost 32 B of arena, and the later +8 B cost nothing, because
  `_edtcmz` was already at a 32-byte boundary.
- **Arena ownership:** `FxBuffer` hands out one contiguous Effect share from
  the bottom and up to twelve 4,416 B voice units (2,208 16-bit samples,
  50.06 ms at 44,108 Hz; at most two per Instrument slot) from the top. The
  Effect share is at least 73,600 B with all twelve units claimed. CrumpBit
  (S074), the first arena user, takes 70,592 B of it. The
  system never clears the arena: an owner clears what it reads unless it
  adopts content the handoff record marks valid. Details:
  `EFFECTS_BUS_REFERENCE.md` §7 and `EFFECTS_MIXER_DSP_REFERENCE.md`.
- `sine_table` (8,194 B) moved from DTCM to flash in S072; `transientData`
  is flash-resident too.

---

## 8. SRAM1

### 8.1 DMA buffers (`.dma_nocache`, MPU region 1)

| Object | Bytes | Notes |
|---|---:|---|
| `dma_buffer` | 1,536 | I2S2 / DAC2 circular DMA, word-aligned |
| `dma_buffer2` | 1,536 | I2S3 / DAC1 circular DMA, word-aligned |
| `adc_dma_buf` | 28 | Slider ADC scan |

- MPU region 1 covers the first 4 KB of SRAM1 as **Normal non-cacheable**
  (TEX=001, C=0, B=0, S=1, XN=1) since Session 073; it was Strongly-Ordered
  before. Stores are bufferable, so the pack ISR ends with `DSB`
  (`EFFECTS_MIXER_DSP_REFERENCE.md`).
- The whole section must stay ≤ 4,096 B (linker ASSERT).

### 8.2 Resident SRAM1 owners

All sizes are bytes. Objects have firmware lifetime unless a shorter
useful-content lifetime is stated; clearing or reusing an object does not
release its linked storage. The section ledger (§5) includes every linked
byte, including alignment and small variables omitted here.

| Owner / object | Bytes | Allocation and use |
| --- | ---: | --- |
| `SceneData.c`: `scenes` | 26,496 | Sixteen resident Scene records, 1,656 B each: 75 B settings including the S075 F2 `fx_send_morph[6]`, the S078 `track_morph_length/scale/shuffle[7]` and the S080 `chain_condition[2]`/`chain_next[2]`, 420 B Scene-owned Effect record, and the 1,160 B Kit (plus alignment); Pattern regions are separate. |
| `PatternData.c`: `pat_regions` | 168,448 | Sixteen packed regions of 10,528 B: each has 1,792 B step addresses, 8,192 B pool, 512 B bitmap, and 32 B Pattern/track settings (length, scale CC, shuffle, run mode × 7, the S080 loop-exclude mask, 2 Pattern bytes; run mode added in S078). Since S075 the top 132 B of each pool (33 chunks) is a permanent swap block outside normal allocation (8,060 B usable), kept as a guaranteed rewrite area for copy/clear and later features (`PATTERN_DYNAMIC_STACK.md` §3, §12.17). |
| `PatternData.c`: `pat_background_region` | 10,528 | One Scene-sized background region outside `pat_regions[]`, not a playable Scene (S077). Serves as the immutable snapshot for an in-flight Pattern AutoSave, the copy/clear scratch pool for an overlapping paste (`pat_backgroundPoolMut()`), and future Bank Load staging. Replaces the former `pat_autosave_snapshot` at identical size. |
| `PatternStackService.c`: `reservation_image` | 512 | One non-persisted bit image for the current service Scene's trailing pool reservations; three separate one-byte policy/rebuild flags accompany it. |
| `PatternStackService.c`: `service_queue` | 256 | Sixty-four 32-bit mutation entries; cursors and repair/handover state are additional small SRAM1 objects. |
| `Autosave.c`: `autosave_dirty_mask` | 3,856 | Sole canonical scalar dirty-bit mask. |
| `Autosave.c`: Pattern dirty masks | 4 | Two 16-bit Scene masks: semantic and non-semantic relocation work. |
| `Autosave.c`: `autosave_dirty_count`, `autosave_last_pattern_semantic_us` | 6 | Exact scalar dirty-bit count and latest semantic Pattern edit timestamp. |
| `filesystem.c`: `fs_pattern_generation`, `fs_pattern_drain_scene`, `fs_pattern_first_dirty_us`, `fs_pattern_scene_cursor` | 70 | Sixteen Pattern generation baselines, drain selector, first-dirty timestamp, and fair Scene cursor. |
| `filesystem.c`: `fs_autosave_parameter_cache` | 4,608 | Bounded scalar AutoSave patch offsets and values. |
| `filesystem.c`: `fs_stage_workspace` | 2,048 | One union shared by Kit, Instrument, Scene+Effect, AutoSave writer, and HCNAMES regeneration staging. The Scene+Effect peak is 1,625 B (the typed-load assert sums to 2,009 of 2,048 since S074); union members are not additive. The AutoSave writer member gained the 1-byte `overlong_mask` in S074 (0 B: inside the union). |
| `filesystem.c`: `staging_buf` | 512 | Shared streaming and trace-batch buffer. |
| `filesystem.c`: `fs_list_cache_name` | 9,000 | One 1,000 × 9 browser/index name cache. Since S075 it is also lent to copy/clear as working storage while an operation runs (`filesystem_borrowNameCacheScratch()`; tag `FS_NAME_CACHE_COPYCLEAR`): [0..160] HCNAMES row remap. Since S077 the paste source table and source blocks no longer use this buffer (the table is the static `ccSvc_snapTable[128]`; the blocks use `pat_background_region.pool`), so the loan carries the 161 B remap only. While lent, cache disposal is ignored and other filesystem ops are refused; the cache is cleared on return and Load/Save reloads its index. |
| `copyClearSession.c` / `copyClearService.c` | 215 | Copy/clear operation state, source, raw-index press stack, edge masks; queue of four 6 B jobs, eight-entry pot-clear register, run state, early-trigger masks, trickle credit and name-buffer pointer (S075 F1). The +131 B F1 owner delta is separate from the −7 B retired LED state in the net ledger. |
| `filesystem.c`: `hcnames_name_mirror`, `fs_resident_source` | 1,771 | Separate 161 × 9 HCNAMES names and 161 × 2 provenance sources; Effect rows are 145..160. |
| `copyClearService.c`: `ccSvc_snapTable` | 256 | Static 128 × `uint16_t` paste source table (S077, approved): bit 15 source trigger, bit 14 stored block, bits 11..0 pool chunk offset. Replaces the table formerly cast into the borrowed name buffer and covers a future `PAT_STACK_SIZE` of 512. |
| `filesystem.c`: `op_effect_display_name` | 9 | Cached Effect filename stem for the current Scene/Bank child save. |
| `filesystem.c`: `op_effect_state` | 7 | Bounded `.fx` parser state retained across async file-reader passes. |
| `filesystem.c`: `fs_identity_name`, `fs_identity_valid_mask` | 74 | Eight × 9 Scene/Kit/Instrument identity strings plus a 16-bit validity mask; the Bank's nine-byte name is held separately by BankData. |
| `filesystem.c`: `op_bank_child_scratch` | 144 | One union: 16 × 9 Bank-child names or 16 × 6 boot-reader Instrument types, with disjoint lifetimes. |
| `asyncfatfs.c`: `afatfs` | 6,984 | FAT state, caches, and five file handles in one owner. |
| `InstrumentManager.c`: `runtime_slots` | 7,584 | Six tagged 1,264 B engine slots; no parallel native engine array. The largest engine (`DrumVoice`) is 632 B (596 + the S088 P0 run-mode state: `runMode`, `ampRampArm`, `lastVeloGain`, `lastFmDepth`, the 28 B `OscCarrierSnapshot`); the reserve is twice that. |
| `adcPots.c`: `slider_lut` | 4,096 | 1,024 `float` slider conversion values. |
| `SampleMemory.c`: resident and install caches | 5,040 | 1,440 B `sample_info_cache`, 1,080 B `sample_name_cache`, 120 B loop flags, 1,440 B `install_info`, and 960 B `install_names`. |
| `usb_manager.c`: `USB_OTG_dev` | 1,524 | USB core/device handle. |
| `usb_midi_core.c`: `usb_MidiMessages` | 768 | USB MIDI input ring, `MIDI_USB_RX_RING_SIZE` (128) x 6 B `MidiMsg` with arrival stamp (S083 Q13; was 256 x 8 B = 2,048 B). |
| `sequencer.c`: pending automation + dirty bits | 560 | 128 four-byte pending records (512 B) and six per-voice 64-bit dirty maps (48 B). |
| `sequencer.c`: R12 menu override (S081) | 48 | `seq_menuOverride[6]` (`u64`): Instrument parameters whose automation a menu edit suppresses until the voice's next resetting trigger. Foreground only. |
| `sequencer.c`: step reset / deferral (S081) | 15 | `seq_trackShuffleStep[7]`, `seq_trackShuffleRoll[7]` (deferred step index and roll flag), `seq_trackShuffleMuteMask` (1 B, a live hit muted the pending deferral). |
| `sequencer.c`: live-record services (S081) | ≈ 94 | Boundary ring `seq_recRing[16]` (`u16`) + cursors (≈ 34 B), `seq_recTrack`/`seq_recWriting`, `seq_echoStep[7]` (`int8`) + `seq_echoDeadline[7]` (`u16`, 14 B), `seq_liveHoldMask`, `seq_eraseSpecialsMask`/`FullMask`, `seq_ppqStampUs`/`PeriodUs` (8 B), `seq_playbackEpoch`, `seq_sceneEpoch`, filter pointers (9 B). TIM3-shared fields are `volatile` bytes or PRIMASK-published pairs. |
| `sequencer.c`: live overlay engine (S084 P2) | 79 | `seq_liveMode/Rate/Accum/Pos/Note/Vel[7]` (42 B), `seq_liveRhythm` (1 B), and the TIM3-to-foreground mailbox `seq_liveHitCount[7]` + `seq_liveFlamMask` + `seq_liveHitUs[7]` (`u32`, 36 B). Runtime-only; the foreground arms/drains under PRIMASK and TIM3 services it (`PATTERN_DYNAMIC_STACK.md` §6). |
| `sequencer.c`: external-clock interpolation (S084 P0 A1) | 1 | `seq_extTicksOwed` (volatile u8): scheduler ticks of the current external clock period still to interpolate. Written in TIM3; cleared by transport start/stop/continue and the external reset. |
| `sequencer.c`: automation ramps (S081 P2) | 669 | `seq_ramps[64]` (`seq_ramp_t`, 10 B: id, from, to, last, start/end 1/16-tick times; 640 B), `seq_rampCtx[7]` (4 B TIM3 hand-off, 28 B), `seq_rampBlockDiv` (1 B). Static, written by the drain/ramp service (slots) and TIM3 (contexts). |
| `LiveRecord.c`: live record state (S081; S084 P4) | ≈ 291 | `latch`/`dirty`/`erase` bitmaps (3 × 64 B over the 9-bit target space), mode/`rtk`/target/origin/epochs, per-track roll capture `liveRec_roll[7]` (8 B each: flags, step, index, start delay, note, velocity, hits, rhythm; S084 P4), flam grace (delta, tick, velocity, valid), `liveRec_prevStep[7]`/`liveRec_prevFlags[7]`, erase gesture state (specials, full, knob FX lanes 2 B, FX SEQ count, SEQ count and track). Static runtime-only. |
| `PatternStackService.c`: live-record staging (S081; S084 P4) | 40 | `patSvc_recordStage[4]` (a `pat_record_spec_t` + scene/track/step/used each; the spec gained `rhythm` in S084, +1 B per slot) and `service_target_request` (recording-target override). |
| `MidiVoiceControl.c`: `voiceControl_pendingReset` (S081) | 1 | Per-voice pending-reset mask (bits 0..6), set by TIM3 and the foreground, tested and cleared under the queue's PRIMASK. |
| `EffectsManager.c`: R13 and live erase (S081) | ≈ 43 | `menuHold` (`u64`) + `menuHoldMorph` (inside `effects_automation`), `effects_eraseLanes` (2 B), `effects_erasedLanes[16]` (`u16`, 32 B). |
| `buttonHandler.c`: live record UI (S081) | ≈ 13 | REC hold timer (`bh_recPressTick` `u16`, `bh_recArmed`), `bh_shiftHeld`/`bh_clearHeld`, erase gesture masks `bh_eraseSeqMask`/`bh_eraseFxSeqMask`/`bh_eraseVoiceMask` (5 B), `bh_overlayHeldType`, `bh_overlayNoteDefault`. |
| `sequencer.c`: PERF looper (S080) | 67 | `seq_loopActive` (ISR gate), `seq_loopTrackMask`/`Division`/`Modifier`, per track `seq_loopAnchor[7]` (uint32 master tick, 28 B) and the snapshot `seq_loopStartStep[7]` (14 B), `seq_loopStartAccum[7]` (14 B), `seq_loopStartPlayState[7]` (7 B). Runtime only; written under PRIMASK, read by TIM3 (`PATTERN_DYNAMIC_STACK.md` §6.5). |
| `sequencer.c`: scene change / chain state (S080) | 12 | Pending direct/queued change (index, division, flags, fire flag, 7 B) and chain runtime (bar/repetition counts, previous Scene, `stp` freeze, chain-fire marker, 5 B) (`PATTERN_DYNAMIC_STACK.md` §6.6). |
| `buttonHandler.c` / `ledHandler.c` / `menu.c`: PERF UI state (S080) | 26 | Looper holds 5 B, BAR overlay runtime 10 B, SHIFT+REC 3 B, release-consume and chain chord 2 B, fast blink 5 B (second slot S082 P3), chain editor Scene 1 B. |
| `LiveRecord.c`: individual Morph capture (S081 P3 G-1) | 7 | `liveRec_morphValue[7]`: the 7-bit value of the last individual edit of each voice Morph (slots 1..6) and the FX Morph. Every dirty Morph write uses it, so a global Morph change (never recorded) cannot leak into a recorded Morph value. Runtime-only, foreground; written by `liveRec_paramIn()`, read by `liveRec_recordValue7()`. |
| `ledHandler.c`: VOICE-mode bar pulse (S081 P4) | 2 | `led_barPulseTrack` and `led_barPulseBar` (0xFF = none): the active track and bar last seen by `led_updateVoiceBarPulse()`, so the SELECT LED of a new bar pulses once in VOICE mode. Runtime-only, foreground, written only from the CHASE branch of `led_processSeqLedState()`. Initialised 0xFF, so `.data`, not `.bss`. |
| `sequencer.c`: per-track timing state (S078) | 76 | ISR-static: `seq_trackAccumulator[7]` (Q8.8, 14 B), `seq_fxAccumulator` (2 B), `seq_fxStepCounter` (4 B), `seq_trackShuffleDelay/Pending/Vel/Note[7]` (28 B), `seq_trackPlayState[7]` (7 B), `seq_effectiveTrackLength/Scale/Shuffle[7]` (21 B, the morphed/automated timing cache). Written by TIM3 and by foreground refresh points (`PATTERN_DYNAMIC_STACK.md` §6.4). |
| `presetMorphEngine.c`: track step overrides (S078) | 24 | `track_param_override_value[3][7]` + `track_param_override_mask[3]`: runtime-only length/scale/shuffle step-automation overlay. |
| `InstrumentManager.c`: `lfo_descriptor_targets` | 192 | Twelve descriptor LFO adapters for six slots and two target pairs. |
| `menu.c`: `parameter_values` | 384 | Legacy Menu/MIDI parameter cells. |
| `MidiLearn.c`: `midiLearn_table` + state (S083) | 528 | 128 × 4 B type-tagged Learn rows (ccKind, space, type, index) + count, state, target, `FUL` deadline (518 B, 528 with alignment). Global, runtime; persisted in `midilrn.cfg`. |
| `MidiControl.c`: `midiControl_nrpn` (S083) | 48 | NRPN receive contexts, 2 ports × 8 × 3 B (key, MSB, LSB). |
| `MidiOut.c`: note-off record + parameter TX (S083) | 22 | `midiOut_trackNote/Channel[7]` (14 B: sounding note and its channel per track) and `midiOut_nrpnKey[2]`, `midiOut_pending`, `midiOut_lastSendMs` (8 B). |
| `MidiDin.c`: DIN buffers (S083) | 334 | RX `midiBuffer_rx_t` 194 B (66 B + 64 × 2 B arrival stamps); TX normal and realtime lanes 2 × 66 B; two 4 B drop counters. |
| `MidiTransport.c`: realtime ring (S083, moved from MidiRealtime) | ≈ 270 | 32 × 8 B events + head/tail/drop counter, MTC and AUTO-sync state. |
| `MidiIn.c`: configuration and framer (S083) | 10 | `midiIn_config` (G, filters, MRT) 3 B, framer state 1 B, in-progress `MidiMsg` 6 B. |
| `buttonHandler.c`: `bh_overlay[7]` (S083 Q5; S084 P2) | 28 | Per-track overlay gesture table, 4 B per track (held MIDI note, note + `def`, count, rate); shared by panel and MIDI. The hit timing, rhythm position and repeats live in the Sequencer's live engine. |
| `sequencer.c`: `seq_resumeTick`, `seq_sceneChangeFromMidi` (S083) | 5 | Continue/SPP resume tick (4 B) and the MIDI-origin PC flag (1 B). |
| `filesystem.c`: `fs_midilearn_write` (S083) | 1 | `midilrn.cfg` write request (PENDING, RETRIED). |
| ~~`MidiParser.c`: `midiParser_originalCcValues`~~ | 0 | Retired in S083 with `MidiParser.c` (was 255 B). |
| `buttonHandler.c`: `evt_ring` | 64 | Sixty-four one-byte front-panel events; producer/consumer and overflow state are additional bytes. |
| `lcd.c`: `lcd_queue` | 384 | LCD command queue. |
| `FxBuffer.c`: `fxbuf_state` | 28 | Linker arena base/size, twelve unit owners, count, and share callback. |
| `FxBuffer.c`: `fxbuf_handoffRecord` | 180 | Effect/voice handoff metadata and arena-relative positions. |
| `EffectsManager.c`: `effects_state` | 80 | Active type/Scene, force flag, 64-byte last-applied image, FX step/selection state, sequence signature, and common runtime values (84 B before S081 removed the held-Morph latch). |
| `EffectsManager.c`: `effects_automation` | 120 | Effect Pattern overlays (active/value), `menuHold` (`u64`) + `menuHoldMorph`, `fxm` override, and 6 × 2 base-independent LFO contribution entries (184 B before S081 removed the owner/end masks). |
| `sequencer.c`: `seq_fxEvent` | 1 | TIM3-to-foreground newest-wins RESET/STEP latch; no Scene, DSP, or LED work occurs in the ISR. |
| `sequencer.c`: `seq_effectAutomationReset` | 1 | Reset latch for the foreground Effect overlay drain (also set by `seq_requestEffectOverlayReset()` at a realign, S081). `seq_effectAutomationTracks` was removed in S081 with the step markers. |
| `menuEffects.c`: page state | 21 | Eight SELECT screen cells, Morph-view flag, `typ` transaction state, last Scene/type tracking, SEQ hold mask, and LED repaint signature; SRAM1, foreground UI lifetime. S074 repacked the hold flag byte as `holdState` (active bit, last-held-valid bit, 4-bit last step held): 0 B change. |

Other SRAM1 state comprises filesystem operation cursors and text buffers,
HCNAMES/boot control fields, Menu and front-panel state, sequencer/MIDI state,
modulation metadata, USB/driver records, and section padding. Notable small
owners are `menu_pendingPageSwitch` (1 B), `fs_boot_latch` (6 B linked),
`fs_boot_winner` (12 B linked), and `drumset_apply_stall_ticks` (2 B). No
Pattern storage is embedded in `scene_t`; `PAT_STACK_SIZE=256` reserves
8,192 B of pool per Scene while the 512 B bitmap covers the full address
range.

### 8.3 Conditional diagnostic SRAM1

These exist only in development builds. Logging-only rows are compiled out
with their producers when `DEV_MODE_LOGGING=0`; the FxBuffer and registry
self-test rows exist only when `DEV_MODE_DIAGNOSTIC=1`. Measure a mode-off
total from a clean rebuild; subtracting this table from a mode-on total
misses alignment and other compile-time changes.

| Owner / object | Bytes | Use |
| --- | ---: | --- |
| `AutosaveTrace.c`: `autosave_trace_records` | 16,384 | Temporary 2,048 × 8 record ring (default 64 records). |
| `AutosaveTrace.c`: three 16-bit cursors/counter | 6 | Ring publication, flush, and dropped-record state. |
| `PatternTrace.c`: `pattern_trace_records` | 256 | 32 × 8 Pattern/automation diagnostic ring. |
| `PatternTrace.c`: three 16-bit cursors/counter | 6 | Pattern trace publication, flush, and dropped-record state. |
| `filesystem.c`: `fs_hcprms_boot_capsule` | 64 | Eight × 8 B frozen boot-ensure failure records; useful for one boot attempt. |
| `filesystem.c`: trace flush cadence and witness state | 3 | `fs_autosave_trace_next_due_tick` and `fs_trace_suppress_witness`; other logging control is included in the section total. |
| `buttonHandler.c`: `evt_drop_count` | 1 | Saturating front-panel overflow witness. |
| `FxBuffer.c`: `fxbuf_selfTestResult` | 1 | Diagnostic-only allocation self-test result. |
| `EffectsManager.c`: `effects_registryCheckCode` | 1 | Diagnostic-only registry invariant result. |
| `copyClearService.c`: `ccSvc_traceState` | 22 | Packed DEV-only copy/clear operation, queue, scratch, drop, register and name counters. |
| `LiveRecord.c`: `liveRec_drops[7]` (S081) | 14 | Live-record drop counters by cause (trace stage `W`). |
| `sequencer.c`: `seq_recRingDrops` (S081) | 2 | Boundary-ring overflow count (stage `W` cause 5). |
| `sequencer.c`: `seq_rampNoSlotDrops` (S081 P2) | 2 | Ramp pairs that jumped because all 64 slots were busy. |
| `filesystem.c`: `fs_cc_suspended_prev`, `fs_cc_refusal_reported` | 2 | DEV-only suspension-edge and first-refusal latches for stage `c`. |

`DEV_STALL_DETECTION=1` also keeps its phase/tick detector state; its
condition is `DEV_STALL_DETECTION`, not `DEV_MODE_LOGGING` alone. The runtime
AutoSave drain's observer is 5 B: `op_autosave_drain_last_phase` (u8),
`op_autosave_drain_stall_ticks` (u16) and `op_autosave_drain_last_progress`
(u16). That is the same total as its pre-S074 `u8` + `u32`. The S073
special-tag self-check (`instrumentManager_specialTagSelfCheck()`) uses no
RAM. S082 P2 adds `fs_stall_poll_uncounted` (1 B) to the same conditional
class; it is set only inside `filesystem_tickUncounted()`.

Configuration at S073 close: `DEV_MODE_LOGGING=1`, `DEV_MODE_DIAGNOSTIC=0`,
`DEV_LOGGING_IWDG=0`, `DEV_STALL_DETECTION=1`,
`AUTOSAVE_TRACE_RECORD_COUNT=2048`, `PAT_TRACE_RECORD_COUNT=32`,
`PAT_STACK_SIZE=256`, `MEMTEST_ENABLED=1` (compiled, not called).

---

## 9. SD card storage (referential)

The card holds the musical data and the persistent state; this manifest does
not describe it in detail.

| Topic | Authority |
|---|---|
| Directory layout, `.hcnames`, `.hcindex`, Kit/Instrument/Scene/Bank/Pattern/`.fx` files, load/save | `FILESYSTEM_SPEC.md` |
| AutoSave `.hcprms1/.hcprms2` (HCPR v3), PAT4 A/B, boot restore | `AUTOSAVE.md` |
| The async FAT/VFAT layer and its caller rules | `ASYNCFATFS_REFERENCE.md` |
| Trace and boot log files (`/asavetrc.bin`, `/bootlog.bin`, `/pattrace.bin`) | `DEV_MODES.md` |
| Sample source folders `/samples` and `/loops` | §4.3 above |

---

## 10. Allocation rules

- **Reservation policy.** Free DTCM, including capacity released by moving
  tables to flash, is reserved for delay-line and audio buffers (the arena).
  Free normal SRAM1 is reserved for Pattern data.
- **Approval.** Before adding or enlarging retained RAM, state the exact byte
  count, region, lifetime and owner, and get the user's acknowledgement. This
  covers globals, static storage, pools and unions, DMA buffers, linker
  sections, material stack growth, and ITCM code growth. Releasing RAM does
  not authorise its reuse by another subsystem.
- **Logging allocations** require their logging code to be compiled and must
  disappear from a logging-off build.
- **Flash** is no longer tight (250,736 B free at the S074 close), but every
  change is still measured with `link_budget.py` and recorded in the session
  log.
- **Record every change here** in the same change that makes it.

---

## 11. History

- **S043:** tagged runtime slots; slider LUT 1,024 entries; `transientData` to
  flash.
- **S057–S069:** filesystem, AutoSave and Pattern owners (details in those
  handoff logs).
- **S071:** +86 B (per-Scene edit masks, Bank staging, audio-out and FX-send
  step overrides).
- **S072 (Phase 5):** `sine_table` to flash; the 126,624 B DTCM arena;
  Scene records +6,752 B (the 420 B Effect record); HCNAMES +176 B;
  `effects_state` 84 B, `effects_automation` 184 B, `fxbuf_handoffRecord`
  180 B, `fxbuf_state` 28 B, `menuEffects` 21 B, sequencer latches 3 B;
  DTCM `effects_runtime` 76 B, FX bus 256 B, ramp state 32 B. Final link
  `text=483,024`, `data=416`, `bss=426,336`; payload 483,440 B.
- **S073:** program flash 480 → 736 KiB (sector 6), sample floor sector 7,
  boot image check (`.image_check` 32 B flash), DMA region Normal
  non-cacheable, ITCM +400 B (`osc_setFreq`). No SRAM1 or DTCM change.
- **S075:** copy/clear +84 B SRAM1, `srt` retired (−32 B `scenes`; DTCM
  `mixer_decimation_rate` −4 B), +2 B claim/borrow flags; permanent 132 B swap
  block per Scene pool (inside the existing pool); the 9,000 B name cache
  becomes copy/clear working storage during an operation. F1 adds +124 B
  production SRAM1 net and DEV adds 24 B for trace state/latches. DEV link
  `text=530,592`, `data=416`, `bss=426,616`; production link
  `text=516,688`, `data=408`, `bss=409,840`.
- **S074:**
  - `scenes` +64 B (bus compressor settings);
  - DTCM `busComp` 32 B, so the arena is 126,592 B at `0x20001180`;
  - CrumpBit: 0 B static, 56 B in the union, 70,592 B arena share at run
    time;
  - the image reaches sector 6 (the bootloader handles it);
  - `stamp_image_check.py` folded into `build_lxrv2_img.py` (the `.bin` is
    unstamped);
  - final link `text=502,512`, `data=416`, `bss=426,392`; payload
    502,928 B.
- **S076:** `lfo_scene_handoff` (28-byte struct plus alignment, +32 B
  `.bss`). DEV link `text=535,976`, `data=416`, `bss=426,744`.
- **S077:** the static paste source table `copyClearService.c:ccSvc_snapTable`
  (+256 B, approved); `PatternData.c:pat_background_region` (10,519 B)
  replaces `pat_autosave_snapshot` at identical size. Section +264 B (256 B
  table plus 8 B alignment). DEV link `text=536,040`, `data=416`,
  `bss=427,008`; flash payload 536,456 B.
- **S078:** per-track timing (+76 B sequencer, +24 B Morph engine), track
  Morph endpoints (+352 B `scenes`), run mode (+112 B `pat_regions`,
  +7 B `pat_background_region`); section +608 B. DEV link `text=539,856`,
  `data=416`, `bss=427,616`; flash payload 540,272 B (headroom 213,392 B).
- **S079:** step conditions/roll state and roll fill duck (+160 B). DEV link
  `text=544,072`, `data=408`, `bss=427,776`; payload 544,480 B.
- **S081:** live record and live erase (LiveRecord state, Sequencer ring /
  echo / epochs / filter, staging table, pending-reset mask, R12 override map,
  R13 `menuHold`, FX-erase per-step masks, REC/erase UI state), automation
  ramps (640 + 28 + 1 B, `track_auto_smooth` +16 B `pat_regions`, +1 B
  `pat_background_region`), G-1 Morph capture (+7 B), VOICE bar pulse (+2 B
  `.data`); released the Effect owner/pending/held-Morph state and the legacy
  roll bytes. `.bss` +1,136 B, `.data` +4 B. DEV link `text=573,296`,
  `data=416`, `bss=429,112`; payload 573,712 B (headroom 179,952 B).
- **S080:** PERF looper, scene change/queue/chain state, PERF UI state, Scene
  chain settings (+64 B `scenes`), loop exclude (+16 B `pat_regions`, +1 B
  `pat_background_region`); `.bss` +200 B, `.data` +4 B. DEV link
  `text=556,008`, `data=412`, `bss=427,976`; payload 556,420 B (headroom
  197,244 B).
## S082 Load/Save and Scene 17 allocations

All values are SRAM1 static storage. The Scene 17 allocation was approved
2026-10-17; the remaining items were approved in S082 S6/S15/S25 and the
user's 2026-10-18 statement that every expansion quoted in the plan is
approved.

| Owner | Object | Bytes | Lifetime | Purpose |
| --- | --- | ---: | --- | --- |
| SceneData | `scene_background` | 1,656 | permanent | Scene 17 record |
| SceneData | `scene_backgroundFlags` + `scene_backgroundHome` | 2 | permanent | exists / home Scene |
| Menu | `menu_bankPreviewContentMask` | 2 | permanent | Bank child content mask |
| Menu | `menu_bankUiFlags` | 1 | permanent | Merge, COPY held/peek |
| ButtonHandler | `bh_bankCopyPressTick` | 2 | permanent | COPY tap/hold |
| filesystem | `op_bank_child_content_mask` | 2 | operation | preview content |
| filesystem | `op_bank_scene_empty_mask` | 2 | operation | Load empty / Save removal |
| filesystem | `op_bank_save_written_mask` | 2 | operation | Merge Save edit-mask rule |
| filesystem | `op_bank_flags` | 1 | operation | BOOT / REPLACE / PROGRESSIVE |
| filesystem | `fs_autosave_yield` | 1 | permanent | AutoSave drain gate |
| Preset | `pm_bg_phase` | 1 | permanent | background Bank Load phase/flags |
| Preset | `pm_bg_pending_slot` | 2 (`.data`, init 0xFFFF) | permanent | deferred MIDI Bank slot (D33); wait-start tick while a load runs (A8) |
| Preset | `pm_bg_result` | 1 | permanent | background load result for CC0 TX (D34) |
| filesystem | `fs_background_name` | 90 | permanent | Scene 17 name capture, 10 × 9 B (D29) |
| Preset | `PM_BG_UNLOCKED` | 0 | permanent | spare bit 3 of `pm_bg_phase` |
| BankData | `bank_load_unavailable_mask` | 2 | permanent | Scenes a background load has not finished |
| LED | `led_fastBlinkLed[2]` | +1 | permanent | second fast-blink slot (Bank load mode LED) |
| Menu | `menu_bgLoadAvailCount` | 1 | permanent | SEQ LED fill repaint trigger |
| PatternData | Scene 17 Pattern region | 0 | reused | `pat_background_region` |

Totals by part: P0 16 B outside Scene 17 (within the acknowledged ~13 B
S6/S15 plus the plan's 2-3 B acknowledgement) plus Scene 17's 1,658 B; P1
93 B (91 B BSS + 2 B `.data`, plus 2 B flash initialiser); P2 none in
production (1 B DEV); P3 4 B. DEV-only S082 diagnostics:
`menu_loadSavePotDropBurst` 1 B, `op_bank_preview_start_tick` 2 B,
`fs_boot_fail_reason`/`_sub` 2 B, preview stall observer 5 B,
`fs_stall_poll_uncounted` 1 B. Decision history: `082_SESSION_HANDOFF_LOG.md`
§7. `preset_buildBackgroundScene()`
uses one 136-byte stack block buffer; no new large stack frame is added.
