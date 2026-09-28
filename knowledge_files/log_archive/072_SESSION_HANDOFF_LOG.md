# Session 072 Handoff Log

**Project**: LXR-02 firmware port (STM32F765VIH6)
**Branch**: `dev-ph5-effects`
**Session dates**: 2026-09-27 / 2026-09-28
**Base commit**: `ee8d371` ("effects bus general plan complete", after the
S071 closeout `08b5f83`)
**State at close**:
- HEAD `58569ae` ("step 8 implemented, step 9 plan in") holds Steps 1–8.
- Steps 9, 10 and 11 are uncommitted in the working tree. You manage commit
  timing.
- The session's step documents `S072_ST1..ST11_IMPLEMENTATION.md` are
  superseded by this log and may be deleted (§14).

**Authority**:

| Document | Role |
|---|---|
| `EFFECTS_BUS_FEATURE_PLAN.md` | Design record: decisions A1–A46, F1–F6, G1–G7 |
| `knowledge_files/specification_reference/EFFECTS_BUS_REFERENCE.md` | As-built reference |
| This log | Implementation record and the acceptance checklist |

---

## End of session

```
DATE: 2026-09-28
SESSION GOAL: Implement Phase 5, the Effects bus (EFFECTS_BUS_FEATURE_PLAN.md
              §17.1 Steps 1–11), one reviewed step at a time.
COMPLETED: Steps 1–11 in source, each clean-built and reviewed; Step 11
           closeout docs and the as-built reference.
VERIFIED ON HARDWARE: Partial.
  - Step 1 production gates PASS (sine stress, no CPU rise, smoke); the FxBf
    diagnostic screen was waived.
  - Step 2 PASS (user report).
  - Step 5 production dry-path fader topology PASS.
  - Step 8: five test points confirmed with `flt` live.
  - The Step 6 fixture matrix, the Step 7 walk-through, and the Step 8
    remainder, Step 9 and Step 10 matrices are pending (§11 checklist).

CHANGES THIS SESSION (details in §5–§9 and the file list in §13):
- Core/DSP/Effects/ (new): EffectTypes.h, EffectsManager.c/.h,
  EffectParamRows.h, FxBuffer.c/.h, StereoFilter/*
- Core/DSPAudio/mixer.c/.h: pre-volume send tap, fader modes, FX bus, return
- Core/DSPAudio/ResonantFilter.c/.h: SVF_calcBlockZDFFloat()
- Core/DSPAudio/wavetable.c/.h: sine_table moved to flash
- Four voice engines + InstrumentManager: volume relocated to the mixer;
  LFO `fx` namespace and Effect adapter; block-7 automation validation
- Core/Bank/Scene/SceneData.c/.h: Effect record, setters,
  effect_morph_amount, scene_editLayoutMatches()
- Core/Bank/Scene/Autosave.c/.h: Effect region, v3 header, Effect reader
- Core/Bank/BankData.c/.h: bank_revalidateVoiceEditMasks()
- Core/Bank/Scene/SceneModTargets.c/.h: `fxm` 404 (LFO + automation)
- Core/Hardware/SD/storageTypes.c/.h: `.fx` v2 parser/writer, LFO-voice clamp 8
- Core/Hardware/SD/filesystem.c/.h: HCNAMES 161, Scene/Bank `.fx`, boot readers
- Core/Menu/menuEffects.c/.h (new), menu.c/.h, menuPages.h, MenuText.h
- Core/Hardware/frontPanel/buttonHandler.c/.h, ledHandler.c
- Core/Sequencer/StepScale.c/.h (new), sequencer.c/.h
- Core/compat/cmsis_intrinsics.h: PRIMASK intrinsics
- main.c, config.h, Makefile, STM32F765VIHx_FLASH.ld, startup .s
- tools/link_budget.py (new), tools/decode_devlogs.py

KNOWN ISSUES INTRODUCED: none functional. The as-built differences from the
  plan and the carried items are in §10.
KNOWN ISSUES RESOLVED:
- Snare/Cymbal/HiHat applied `vol` before distortion (vol acted as drive).
  Fixed in Step 2: volume is now the last stage on every engine.
- FX_SEND automation (398..403) was a no-op. It is audible from Step 5.
- "Effect placeholders" open question: resolved (A37/A38).

NEXT SESSION RECOMMENDED GOAL: Session 073. First S073_FLASH_EXPANSION.md,
  then S073_CPU_USE_DSP_REDUCTION_REFACTOR.md. Start from S073_SESSION_STARTUP.md.
  Run the §11 Phase 5 acceptance checklist when the hardware is available.
BLOCKERS: Flash headroom is 8,080 B. The Phase 5 hardware matrices are
  pending.

CRITICAL REMINDERS FOR NEXT SESSION:
- Retained Effect bytes are written only by SceneData. UI and type hooks write
  only through the EffectsManager edit API, which fans out and marks AutoSave.
- Nothing in EffectsManager or FxBuffer may be called from an ISR. TIM3 only
  publishes latches and markers.
- The DTCM arena is never cleared by the system: "clear unless you adopt".
- Effect local indices are type-relative. The layout gate keeps VOICE-mask
  fan-out safe.
- Measure every change with `python3 tools/link_budget.py arm-none-eabi-nm build/lxr02.elf`.
- Use `make all` (not bare `make`) until the default-goal issue is fixed (§10.2).
```

---

## 1. Build metrics

| Point | text | data | bss | Flash payload | Headroom (491,520 B window) | DTCM statics | FXBUF |
|---|---:|---:|---:|---:|---:|---:|---:|
| S071 close / plan baseline | 456,748 | 416 | 291,900 | 457,164 | 34,356 | 12,280 (incl. 8,194 sine) | — |
| ST1 | 458,112 | 416 | 419,100 | 458,528 | 32,992 | 4,084 | 126,976 @ `0x20001000` |
| ST2 | 458,208 | 416 | 419,100 | 458,624 | 32,896 | 4,084 | 126,976 |
| ST3 | 458,664 | 416 | 425,852 | 459,080 | 32,440 | 4,084 | 126,976 |
| ST4 | 463,552 | 416 | 425,936 | 463,968 | 27,552 | 4,160 | 126,912 @ `0x20001040` |
| ST5 | 465,352 | 416 | 425,936 | 465,768 | 25,752 | 4,448 | 126,624 @ `0x20001160` |
| ST6 | 470,208 | 408 | 426,128 | 470,616 | 20,904 | 4,448 | 126,624 |
| ST7 | 475,592 | 416 | 426,144 | 476,008 | 15,512 | 4,448 | 126,624 |
| ST8 | 478,720 | 416 | 426,160 | 479,136 | 12,384 | 4,448 | 126,624 |
| ST9 | 482,632 | 416 | 426,336 | 483,048 | 8,472 | 4,448 | 126,624 |
| ST10 | 483,024 | 416 | 426,336 | 483,440 | 8,080 | 4,448 | 126,624 |
| ST11 (final) | 483,024 | 416 | 426,336 | 483,440 | 8,080 | 4,448 | 126,624 |

Notes:

- **`bss` jump at ST1.** `size` counts the NOLOAD `.dtcm_fxbuf` arena in
  `bss`; SRAM1 did not grow by 127 KB. Use `link_budget.py` and
  `SRAM_MANIFEST.md` for real region use.
- **ITCM** stayed at 3,768 / 16,384 B throughout.
- **Flash cost per step.** The largest items:

  | Step | Cost | Largest items |
  |---|---|---|
  | ST4 | +4.9 KB | `SVF_calcBlockZDFFloat` 2,772 B |
  | ST6 | +4.9 KB | `.fx` parser/writer, boot reader |
  | ST7 | +5.4 KB | `menuEffects` |
  | ST9 | +3.9 KB | automation/LFO |
  | ST10 | +0.4 KB | |

- **Budget warning.** `link_budget.py` has warned (below 16 KiB) since ST7.
- **LTO effects.** ST1 grew flash by +1,364 B from LTO re-partitioning, not
  from the (flash-neutral) sine move. `mixer_calcNextSampleBlock` codegen
  shifted by +208 B with no source change.

### RAM ledger (Phase 5 total)

| Object | Region | Bytes | Step |
|---|---|---:|---|
| `scenes[16]`: 420 B Effect record + 1 settings byte + 1 pad per Scene (1,200 → 1,622 B each) | SRAM1 | +6,752 | ST3 (A1: +16 over the approved 6,736) |
| `fxbuf_state` | SRAM1 | 28 | ST1 |
| `fxbuf_handoffRecord` (`fxbuf_handoff_t`) | SRAM1 | 180 | ST1 (G7 approved about 176) |
| `effects_state` (76 → 84 at ST8) | SRAM1 | 84 | ST4/ST8 |
| `effects_automation` | SRAM1 | 184 | ST9 |
| HCNAMES mirror 161×9 and source register 161×2 (+144, +32) | SRAM1 | +176 | ST6 |
| `op_effect_display_name` / `op_effect_state` (3 → 7) | SRAM1 | +9 / +4 | ST6 |
| `seq_fxEvent` + `seq_effectAutomationTracks` + `seq_effectAutomationReset` | SRAM1 | 3 | ST8/ST9 |
| `menuEffects` page state (14 at ST7 → 21 at ST8) | SRAM1 | 21 | ST7/ST8 |
| `effects_runtime` union (`flt`) | DTCM `.dtcmz` | 76 | ST4 |
| `mixer_fx_bus` | DTCM `.dtcmz` | 256 | ST5 |
| `mixer_send_last_gain[6]` / `mixer_fx_return_last_gain[2]` | DTCM `.dtcmz` | 24 / 8 | ST5 (D2, +32 acknowledged) |
| `sine_table` | DTCM → flash | −8,194 DTCM | ST1 |
| `.dtcm_fxbuf` arena | DTCM NOLOAD | 126,624 | ST1 (elastic) |
| `fxbuf_selfTestResult`, `effects_registryCheckCode` | SRAM1 | 1 + 1 | diagnostic builds only |

All of these were approved under plan A46/G7 or acknowledged per step (ST3
A1, ST5 D2, ST6 D6, ST8 D5).

---

## 2. What Phase 5 delivered (summary)

For the full as-built description see `EFFECTS_BUS_REFERENCE.md`.

- **Per Scene:** one Effect (`off` or `flt`) with 64 normal + 64 Morph
  parameters, and a 16-step × 16-lane FX sequence. Effect Morph is Scene
  parameter 40.
- **FX bus:**
  - voice → decimate → parallel dry and **pre-volume** send taps, with fader
    modes `pre`/`pst`/`fx`;
  - saturated `sample_mx_t` bus → float in place → `effects_process()` →
    100 % wet routed return, with jack fallback and level + balance/pan.
- **Resolution** every render block:
  Morph base (Pattern `fxm` > held FX Morph lane > retained) → LFO `fxm` →
  interpolate → FX lock → Pattern overlay → LFO → clamps → `write_param` on
  change.
- **Storage:**
  - `.fx` v2 (one `<name>.fx` per Scene folder);
  - HCNAMES rows 145..160;
  - AutoSave v3 Effect region (512 B per Scene), with Case 1 apply and Case 2
    narrow reload.
- **UI:** the SHIFT+PERF Effect page (`menuEffects.c`), SEQ hold lock
  editing, the `fx` step-automation category, the LFO `fx` namespace, and
  `fxm` in `scn`.
- **Masks:** the VOICE edit-mask layout gate, with re-validation and Effect
  edit fan-out.

---

## 3. Working method used this session

Each step followed the same loop:

1. The assistant wrote a full implementation schedule `S072_STn` (every change
   by file, line and add/remove/modify, with comment blocks).
2. You implemented it.
3. The assistant reviewed the diff against the schedule, clean-rebuilt, and
   appended an assessment.
4. Findings were folded into the next schedule as prerequisites.

Standing rules:

- **G5:** no changes outside the Effects framework unless the plan requires
  them; no side quests.
- The assistant does not schedule or recommend commits.
- `.claude` auto-memory is not used.

---

## 4. Plan-level decisions recap

The full tables are in plan §18. Most-cited decisions:

| ID | Decision |
|---|---|
| A3 | PERF `mrp` bulk-sets Effect Morph. |
| A10 | One step-scale table shared by tracks and FX. |
| A12 | `sel` always applies. |
| A16/A18 | Pattern > FX lock > Morph-interpolated menu value. |
| A24/A25 | Pre-volume send and fader modes. |
| A31 | No system clears. |
| A37/A38 | Missing or placeholder `.fx` → `off`; no migration. |
| A44 | Fan-out with the layout gate. |
| F1 | Three restore rules; Effect overlays end with the automation. |
| F2 | 511 stays off; local 63 is not automatable. |
| F3 | A type change resets rows 3..63 and clears the whole sequence. |
| F5 | Gate + re-validation. |
| F6 | Same-type switches keep running; handoff record. |
| G1 | `fxm` follows the Scene rule. |
| G3 | Last writer owns an overlay. |
| G4 | Instrument-identical precedence. |
| G5 | Scope discipline; Instrument naming path unchanged. |

---

## 5. Steps 1–2: arena, sine move, voice volume

### Step 1: `sine_table` to flash, DTCM arena, `FxBuffer`, link budget (§5.5 investigation)

**Changes:**

- `wavetable.c/.h`: `sine_table` loses `INCCM`, which releases 8,194 B DTCM.
  Flash-neutral: the `.dtcm` load image shrinks by the same amount.
- **Linker:**
  - a NOLOAD `.dtcm_fxbuf` after `.dtcmz`, `ALIGN(32)` to the end of DTCM,
    with symbols `_sfxbuf`/`_efxbuf`;
  - `ASSERT(size ≥ 122,880)`;
  - the header comment was updated.

  The startup code does not zero it.
- **`FxBuffer.c/.h`** (new):
  - voice units of 2,208 × 2 B = 4,416 B (50.06 ms at 44,108 Hz), 12 in
    total, at most 2 per slot, allocated from the top;
  - the Effect share is contiguous from the bottom;
  - the handoff record (180 B) and the share-change callback;
  - a diagnostic self-test and the `DEV_FXBUF_FORCE_VOICE_UNITS` knob;
  - LTO inlines every `fxbuf_*` in production.
- `main.c`: the diagnostic `FxBf` boot screen. `config.h`: the knob.
  Makefile: the link-budget recipe. `tools/link_budget.py` (new; warns below
  16 KiB).

**Flash investigation (the SCOPING §5.5 deliverable).** This is also the
S073 flash plan's input; see `S073_SESSION_STARTUP.md`.

1. The linker asserts `_etext <= 0x08080000` and
   `_eflash_load <= ORIGIN(FLASH)+LENGTH(FLASH)`. An oversize link fails
   ("Application load image overlaps sample flash region"), so no oversize
   `.bin` can be produced.
2. `lxr02.bin` = `_eflash_load − 0x08008000`. `objcopy` emits loadable
   sections only, which is why the arena must be NOLOAD.
3. `tools/build_lxrv2_img.py` has no size check. It adds the 16-byte
   `LXRV2IMG` header and an 8-bit checksum.
4. **Bootloader behaviour on a payload over `0x78000` bytes is unknown**
   (closed sector-0 binary). It was not tested; testing risks sample sectors.
5. The sample floor is sector 6: `SampleMemory.h` `SAMPLE_ROM_START_ADDRESS
   0x08080000`, and the `sampleFlash.c` sector table rejects erases below 6.
6. Largest flash consumers:
   - constant tables of 126,812 B, about 28 %: `crashSample` 32,768,
     `transientData` 26,460, and `saw`/`tri`/`recTable` 22,528 each;
   - code: `main` 9,024, `filesystem_tick` 7,896, `menu_repaintGeneric`
     7,864, and the Scene/Bank load/save ticks 6.5–7.6 KB each.
7. The sine move is flash-neutral.

**Growth paths ranked (none implemented):**

1. `-Os` for cold control modules (`menu.c`, `filesystem.c`,
   `presetManager.c`, `storageTypes.c`) via per-file rules; several KiB.
2. Move large constant tables into the sample region as installed data; up
   to about 100–124 KiB.
3. Move the sample floor to sector 7 (+256 KiB app, −256 KiB samples). This
   needs finding 4 tested first.
4. Remove dead legacy code; small.

**Review findings:** see §10.2. The `fxbuf_init()` handoff-reset order, the
Makefile echoed comments and the bare-`make` default goal remain open.

**Hardware:**

- Production (sine stress, no CPU rise, no underruns, smoke): PASS.
- The diagnostic `FxBf` screen at knob 0/12 was waived by you.
- The local 40 KiB oversize-link ASSERT experiment was never run.

### Step 2: voice volume relocation (plan §8.1) and the D1 bug fix

- **D1 (your decision 2026-09-27): volume must be the last stage on every
  engine.** Snare, Cymbal and HiHat had multiplied `velo × vol × EG` before
  `calcDistBlock()`, so lowering `vol` also reduced saturation. That was a
  bug.
  - After the fix the engines render **pre-volume**: Drum's final
    `bufferTool_addGain(vol)` is removed, and the other three multiply
    `velo × EG` only.
  - Consequence: Snare/Cymbal/HiHat sounds with `drv > 0` and `vol < 127`
    are now more saturated at the same `vol` (the correct behaviour).
    Drum is unchanged.
- `instrumentManager_runtimeVolume(slot)` was added beside
  `instrumentManager_runtimePan()`.
- `mixer_slider_last_gain` was renamed to `mixer_voice_last_gain` (same
  24 B DTCM). It now ramps `slider × volume`, so volume changes are smoothed
  per block and LFO on `vol` loses its block stepping.
- `mixer_init()` seeds the ramp at the combined gain, which requires
  `instrumentManager_runtimeInit()` first.
- **Hardware:** user report "step 2 firmware checks out on the hardware; no
  major problems observed".

---

## 6. Steps 3–5: data model, EffectsManager, FX bus

### Step 3: data model

- **`EffectTypes.h`** (new): `effect_record_t`
  - `type`, `seq_run_mode`, `seq_length`, `seq_step_scale`;
  - `normal[64]`, `morph[64]`;
  - `steps[16]` of {`lock_mask` u16, `value[16]`}.

  It is 420 B (the step is 18 B), with static asserts. It also holds the
  block-7 helpers (`EFFECT_TARGET_ID_BASE 448`, local limit 63).
- **`SceneData`:**
  - `effect_record_t effect` between settings and Kit;
  - `effect_morph_amount` last in settings (41 B);
  - `scene_t` 1,200 → 1,622 B;
  - change-aware Effect setters (store, mark, invalidate the card-clean
    bit);
  - `scene_initAll()` seeds `off` defaults.
- **A1:** +16 B SRAM1 over the plan (one pad byte per Scene); acknowledged.
- **A2:** common defaults `out` 0 (St1), `vol` **127** (unity; the plan
  example had 100), `pan` 64.
- **Deviations from the plan:**
  1. The AutoSave Effect region has **419 live cells** at relative offset 11
     (3 sequence settings + 64 + 64 + 288). The token (0–2) and name (3–10)
     keep their own offsets.
  2. The token and name were not live until Steps 4 and 6.
  3. There was no header bump in Step 3 (Step 6 later bumped to v3).
  4. `fxm` ID 404 was reserved with use flags 0 until Step 9.
- The implementer also fixed a pre-existing static assert (the MIDI-note
  group was bounded by `AUTOSAVE_SCENE_PARAM_COUNT`; it now uses
  `…_EFFECT_MORPH`).
- **Card evidence:** records healthy (`V`/`Q`/`T` clean); Scene parameter 40
  = 0. Effect-region capture (gate 7) was not exercised in the captured
  sessions; it is closed functionally by Steps 4–6.
- **Observation:** `.pat02b` was once seen truncated (5,000 B). A/B recovery
  tolerated it; it is not related to Phase 5.

### Step 4: EffectsManager, registry, `flt`, float SVF

- **Registry** (`off` built-in; `flt` from `StereoFilter/`):
  - descriptors from `EffectParamRows.h` macros;
  - lanes: 0 Morph, 1 freq, 2 reso, 3 drive, 4 type, 5 vol, 6 pan.
- **D1 `flt` defaults** (confirmed): `filter_freq` 64, `reso` 0, `drive` 0,
  `type` 0 = LP. The runtime SVF type is menu index + 1.
- **Rescan every block** instead of dirty tracking. Cost is about 10–15
  cycles per descriptor (about 100 for `flt`, at most about 1,000 for a
  64-row type). No writer can forget to notify.
- **Type switch** is immediate on Scene activation (F6). The same type
  continues; a different type runs `init`, so the outgoing tails stop.
- **In-place whole commit:**
  `scene_effectRecordForWholeCommit()` + `scene_finishEffectWholeCommit()`,
  which avoids a 420 B stack copy.
- `effects_interpolate()` duplicates the `presetMorph_interpolate()`
  arithmetic, with a keep-in-sync note.
- **Registry self-check:** codes 1–10, shown on the `FxBf` screen.
- **`SVF_calcBlockZDFFloat()`** (float I/O, no `__SSAT`): 2,772 B at -Ofast.
- **Hooks:**
  - `preset_sendDrumsetParameters()` and `preset_startDrumsetApply()` call
    `effects_activateScene()`;
  - `preset_morphScene()` bulk-sets Effect Morph (A3);
  - the mixer calls `effects_service()`;
  - `main.c` calls `effects_init()`;
  - `DEV_EFFECT_FORCE_TYPE` bench hook;
  - AutoSave type-token projection and marking;
  - `decode_devlogs.py` updated (Scene parameters 41, `fxm_amt`, Effect
    labels).
- **Build-host note:** one clean link hit `lto1: internal compiler error:
  Bus error: 10` and left an empty ELF. Deleting `build/lxr02.elf` and
  relinking fixed it. This is likely two builds sharing `build/`.

### Step 5: FX bus, sends, fader modes, return

- **`mixer.h`:** `MIXER_FADER_PRE/POST/FX` (0/1/2 = stored byte = labels
  `pre`/`pst`/`fx`).
- **`mixer.c`:**
  - `mixer_fx_bus` union (`sample_mx_t[2][32]` ↔ `float[2][32]`, 256 B DTCM);
  - `mixer_send_last_gain[6]`, `mixer_fx_return_last_gain[2]`;
  - `mixer_faderGains()`, `mixer_addVoiceToFxBus()` (×256 to
    `sample_mx_t`, panned for stereo-in only);
  - `mixer_floatToMx()` (clamp ±255 int16 full scales × 8,388,352);
  - `mixer_addFxReturnToOutput()` (per-block ramp; stereo route L/R;
    single-jack routes get `0.5 × (L·gL + R·gR)`).
- **`off`** skips every bus stage. Send ramps still update.
- **D1 balance law** (stereo-out return): `gL = pan<=64 ? 1 :
  (127−pan)/63`, `gR = pan>=64 ? 1 : pan/64` (unity at centre). Mono-out
  uses the voice constant-power law.
- **D2:** +32 B DTCM ramp state (acknowledged). The arena shrank to
  126,624 B.
- **FX_SEND automation became audible:** the mixer pulls
  `preset_getEffectiveFxSendAmount()` each block. Preset setters are
  store-only.
- **`effect_io_t`:** `r` is present for stereo-in OR stereo-out (zeroed for
  mono-in/stereo-out); `channels` = input count.
- **Review findings:**
  - a garbled duplicated comment line at `mixer.c:739–740` (**still
    present**, cosmetic);
  - tab/space mix in the five rewritten `presetManager.c` comment blocks
    (cosmetic);
  - the return ramp is not reset while `off`. This is acceptable today, but
    revisit for a type that outputs sound immediately on `init`.
- **Hardware:** production build normal, and the `fx` fader mode leaves the
  dry path un-attenuated (gates 3–4 PASS). The diagnostic listening gates
  were folded into the Step 6/8 sessions.

---

## 7. Step 6: storage, HCNAMES, AutoSave v3

**D1: atomic staging.**

- The Scene loader order became: scan (9) → `sceneset.scg` (12–16) →
  **`.fx` (56–60)** → Kit (17–32) → commit Scene+Kit+Effect together (33) →
  Pattern (44–53) → publish (61).
- The Effect is staged in `filesystem_scene_stage_t.effect` (stage 1,621 B
  inside the 2,048 B union).
- A malformed `.fx` fails before any resident byte changes.
- A missing `.fx` is valid (`off`, blank name).
- Bank children use the same path.

**D2:** Effect source at Effect-relative bytes 430..431 (the first reserved
bytes). The region and record size are unchanged.

**D3:** `.fx` `step_scale` tokens, index 0..13:
`1/64 1/32t 1/32 1/16t 1/16 1/8t 1/16. 1/8 1/4t 1/8. 1/4 1/2 1bar 2bar`.
Run-mode tokens are `fwd rev pip rnd sel`.

**D4:** a blank Effect name saves as **`none.fx`**.

- The shared Instrument helper maps an all-space stem to `none`; loading
  `none.fx` gives a blank row.
- An empty (NUL) stem becomes `inst`. That is why blank rows must pass
  explicit spaces; this was ST6 finding 1, fixed in ST9 P1.
- F4's `' .fx'` wording is superseded (G5: Instrument naming path
  unchanged).

**D5:** AutoSave header v2 → **v3**. v2 records are rejected, and a 145-row
`.hcnames` fails the exact row count.

- **Card preparation used:** delete root `.hcnames`, `.hcnamtmp`, `.hcprms1`
  and `.hcprms2` before the first v3 boot. The canonical Bank Load then
  rebuilds them.

**D6 RAM:** +176 B HCNAMES, +9 B stem, +4 B parser state.

**Other notes:**

- Effect rows are `-` (inherit) or `?`. A direct numeric Effect source is
  treated as unresolvable until the library exists.
- Case 2 narrow reload opens `<row name>.fx`, so hand-made stems must be ≤ 8
  characters.
- AutoSave name bytes are baseline-only.
- Runtime activation needed no change.
- Scene Save phases 33–36 are unreachable (nothing sets `op_phase = 33`);
  they were updated for consistency only.
- `tools/verify_bank_autosave.py` is stale (expects 129 rows); not touched.

**Review findings, both fixed in the Step 9 change set (ST9 P1–P2):**

- F1: blank-name Scene Save passed the raw cached cell (NUL → `inst.fx`).
- F2: Kit Save wrote the Effect row source.

**Fixture note:** a malformed `.fx` in a **root** Scene triggers the existing
quarantine rename of that folder (phase 62→68). Bank children clear their
present bit instead. Use disposable copies.

---

## 8. Steps 7–8: Effect page and FX sequencer

### Step 7: `menuEffects` Effect page

**Decisions:**

- **D1:** pressing a different SELECT goes to its first screen; re-pressing
  the current SELECT cycles its screens. SELECT 1 toggles `typ out vol pan`
  ↔ `run len scl mrp`, like repeated SHIFT+PERF.
- **D2:** provisional `scl` labels, moved to the shared StepScale table in
  Step 8.
- **D3:** active Scene only until Step 10. Every write goes through the new
  EffectsManager edit API.
- **D4:** PERF SHIFT no longer switches to the empty
  `PATTERN_SETTINGS_PAGE`. Euklid, rotation and the pattern-settings entry
  are compiled out behind `ENABLE_EUKLID_PAGE 0`; the source is kept.
- **D5:** 14 B state.

**Architecture:**

- One new cell kind, `MENU_CELL_EFFECT`, whose value/dtype/format/clamp/
  commit delegate to `menuEffects.c`.
- Cursor, screen memory and navigation are owned by `menuEffects` through
  early `EFFECT_PAGE` branches.
- The VOICE screen machinery is unchanged; only the four `is2ndPage`
  predicates moved to `menu_isScreenPage()`.
- UI hooks are defined and dispatched, but no type provides them yet.
- **`typ` transaction:** click in → browse candidates → click out commits
  `effects_changeType()`. Pots are inert on `typ`. A Scene change discards
  the candidate.
- **Finding 1**, fixed in ST9 P3: a SELECT press did not end an open `typ`
  candidate, which could later be committed from another cell. Now
  `menuEffects_selectPressed()` clears `menuEffects_typeEdit` first.
- **Flash:** 15,512 B headroom at ST7. This is when the budget warning
  started.

### Step 8: FX sequencer, StepScale, lock editing, LEDs

**New module and clock:**

- **`StepScale.c/.h`** (new, sequencer-owned): 14 entries, ticks
  `6 8 12 16 24 32 36 48 64 72 96 192 384 768`, default index 4.
  - Short labels: `/64 32t /32 16t /16 /8t 16. /8 /4t /8. /4 /2 1br 2br`.
  - Long labels: `1/64 … 1 bar, 2 bars`.
  - Stale bytes display as the default without rewriting.
- **TIM3 `seq_fxClockTick()`:** `n = seq_elapsedPpqTicks / ticks(scl)`; per
  mode: `fwd`, `rev`, `pip` (period 2L), `rnd` (masked `GetRngValue()`).
  - It publishes the one-byte `seq_fxEvent` (STEP 0x80 | index, RESET 0x40)
    under PRIMASK.
  - It also runs on both `seq_handleMasterBoundary()` early-return paths, so
    a Pattern switch bar does not drop the FX boundary.
  - `seq_setStepIndexToStart()` publishes RESET.
  - The CMSIS shim gained `__get_PRIMASK`/`__set_PRIMASK`/`__disable_irq`.

**EffectsManager and page:**

- **EffectsManager:** state 76 → 84 B.
  - `effects_seqConsume()` (foreground), the active step, lock override
    after interpolation, and the held Morph lane.
  - `effects_seqSelect()`, `effects_laneOfParam()`, `effects_getLaneLock()`,
    `effects_setSeqLaneLock()`.
- **Page:**
  - SEQ hold (same threshold as VOICE View B) edits locks on the held steps;
  - page-owned lock/chase/select LEDs;
  - `led_updateCurrentStep()` returns early on `EFFECT_PAGE`.

**Decisions:**

- **D1:** an unlocked held lane shows the value actually played; a locked
  lane shows its lock, underlined.
- **D2:** `typ`/`out`/`run`/`len`/`scl` are inert while holding.
- **D3:** after a Scene switch, only menu values apply until the new Scene's
  next boundary (except `sel`), and the held Morph clears.
- **D4:** track scale uses the shared table.
  - The old index meaning changed: the old `off` = 10 now reads `1/4`.
  - Values above 13 display as `/16`; the new default is `/16`.
  - Track playback still ignores scale.
- **D5 RAM:** +16 B.

**Review findings, both fixed:**

- **F1**, fixed in ST9 §3.1: `sel` shared a validity flag with the clock
  step and was lost on every RESET. `sel` now always applies.
- **F2**, fixed in ST9 §7.8: a held `mrp` lock did not display its value.

**Hardware:** you confirmed the first five test points with `flt` live:
lock editing, `typ` inert while holding, `fwd` chase, run modes, and `sel`.

---

## 9. Steps 9–11: automation/LFO, gate/fan-out, closeout

### Step 9: Effect automation and LFO

**D1: one owner.** `seq_effect_automation_dirty` is realized as
EffectsManager `effects_automation` (184 B):

- `active` u64 and `pending_end` u64;
- `value[64]` and `owner[64]`;
- 6×2 LFO entries {target, direction, depth};
- `pending_track`, `owner_tracks`, `morph_override(_valid)`.

The sequencer holds only two bytes.

**D2: end detection.**

- TIM3 queues an **Effect step marker** (pending identity bit 11,
  payload 0) before the automation entries of any track in
  `seq_effectAutomationTracks`.
- The drain calls `effects_automationStepBegin(track)`: the owned overlays
  become candidates, each rewrite by any track re-holds, and the next
  marker or pass end calls `effects_automationStepFlush()`.
- Ordered through the queue, so an "end" can never overtake an "apply".

**D3:** the marker uses the automation gate. Failed-probability or muted
steps hold overlays, like voice parameters.

**D4: reset ordering.**

- `seq_fxPublishReset()` also sets `seq_effectAutomationReset`.
- The drain takes it **before any record** and calls
  `effects_automationReset()`, which clears the parameter overlays, the
  pending group and the `fxm` override.
- `effects_activateScene()` also resets.
- A type change clears the parameter overlays only.

**D5: `fxm` live.**

- Flags LFO + AUTOMATION, no velocity (A21).
- The 7-bit value expands via `effect_expand7Linear()`
  (`v<127 ? 2v : 255`).
- Restore is a no-op in `seq_restoreAllSceneAutomation()`, because the
  reset latch handles it.
- The menu treats it like voice Morph (`menu_sceneTargetIsMorph()`, max
  127).

**D6: LFO `fx` namespace (byte 8).**

- New `INSTALLED_MOD_TARGET_EFFECT`.
- The shared encoder `instrumentManager_lfoDirectionDepth()` was extracted
  from the voice-Morph case; behaviour unchanged, with a static assert tying
  the encodings together.
- Entries resolve around the held value in the domain. The math is the same
  as `modNode_shapeParameterU16()`.

**D7: validation timing.**

- Pickers and the Pattern writer use the viewed Scene's type.
- Runtime re-checks the live type every apply.
- Retained LFO tokens are normalized at the Scene-activation rebind (no
  presetManager change).

**D8: WIDE8.** `expand7` at the drain; the picker seeds with
`menu_morphAutomationStore()`. No `flt` row is WIDE8, so this path is
review-only.

**D11:** the `storageTypes.c` Kit parser clamps `lfo_target_voice` to 8, so
`fx` survives reload.

**Other changes:**

- `instrumentManager_targetValid()` accepts block 7 for **AUTOMATION only**.
- The menu `fx` category gets helpers (`menu_stepAutomationIsEffect()`
  excludes 511), `FilterFrequncy` labels, and the LFO-voice upper clamps
  moved to `INSTRUMENT_TARGET_VOICE_NAMESPACE_LAST`.

**Prerequisites landed with Step 9:**

- ST6 F1 (explicit spaces for blank stems);
- ST6 F2 (Kit Save leaves the Effect row);
- ST7 F1 (`typ` candidate cleared on SELECT).

**Review findings (fixed in ST10 P1/P2):**

- `effects_seqSelect()` invalidated instead of latching, so an unlocked
  selection dropped the held Morph;
- the `fx` detail label separator truncated.

**Build:** +3,912 B, leaving 8,472 B.

### Step 10: edit-mask gate and fan-out

- **D1:**
  - Each public EffectsManager setter became a wrapper over a static
    `*Scene` worker.
  - Workers: `setParameter`, `setSeqRunMode`/`Length`/`StepScale`,
    `setMorphAmount`, `setSeqLaneLock`, `changeType`.
  - An active-Scene edit reaches `bank_sceneMaskVoiceEdit()`; an inactive
    edit reaches only its origin.
  - `menuEffects.c` is unchanged.
- **D2:** `effects_fanoutMask(scene, match_type)` drops masked Scenes of a
  different Effect type for parameter, lock and sequence edits. `mrp` and
  `typ` reach all of them.
- **D3:** `scene_editLayoutMatches()` compares the Effect type and six
  Instrument types (raw bytes). The gate is in
  `menu_voiceHeldSceneButtonPressed()`: turning on requires a match (else
  silently consumed, LED unlit); turning off is always allowed.
- **D4:** `bank_revalidateVoiceEditMasks()` runs in:
  - `menu_startSoundApply()` (Kit, Scene, Bank, All, Performance, and the
    boot-sync path);
  - `menu_startInstrumentApply()` (the Instrument commit is synchronous);
  - the empty-Bank branch;
  - `main.c` before `preset_startDrumsetApply()`;
  - `effects_changeType()` (masks are directional).
- **D5:** if AutoSave tracking is off at the boot hook, the repair is
  RAM-only and deterministic. Whether tracking is on there is still a
  runtime check.
- **Build:** +392 B, leaving 8,080 B.
- **Note:** VOICE-mask fan-out does not skip non-present masked Scenes. This
  is pre-existing behaviour.

### Step 11: closeout

- **Comment-only source corrections:** 20 stale Phase 5 statements
  (EffectTypes, EffectsManager, FxBuffer, StereoFilter, SceneData,
  SceneModTargets, Autosave, `main.c`), plus one wording tweak in
  `presetMorphEngine.c`. The build is byte-identical to ST10.
- **New `EFFECTS_BUS_REFERENCE.md`.**
- **Updated:** AUTOSAVE, FILESYSTEM_SPEC, MODULE_INTERCHANGE_SPEC,
  BANK_PRESET_ARCHITECTURE, DEV_MODES, SRAM_MANIFEST, the plan (closeout
  header, §15 byte table, §16 measured values, §18.4 follow-ups),
  SCOPING_TARGETS (Phase 5 status, blank-stem entry, Phase 7 unit size),
  MEMORY.md, README.
- **This closeout finished the remaining items:**
  - both logs;
  - the reference's blank-name rule (`none.fx`), design notes and
    debugging section;
  - the MIS stale namespace line and the Menu/LED Effect rows;
  - the SRAM Phase 5 summary;
  - MEMORY/README trees;
  - volatile notes;
  - `S073_SESSION_STARTUP.md`.

---

## 10. As-built differences, open items and carried debt

### 10.1 Differences from the plan (recorded in reference §13)

1. **Handoff on a same-type Scene switch.** It is not refreshed
   (`effects_activateScene()` switches only on a type change). There is no
   effect while no type uses the arena. Refresh without `init` when the
   first buffer type lands.
2. **No immediate `fx` LFO rebind on a `typ` change.** This is handled at
   runtime and at the next Scene rebind.
3. **`seq_effect_automation_dirty`** is `effects_automation.active`.
4. **Resolution** rescans every block (no dirty cells).
5. **F4's `' .fx'` blank-name wording** → the firmware writes `none.fx`
   (ST6 D4).

### 10.2 Open items carried (none blocks S073)

| # | Item | Where | Origin |
|---|---|---|---|
| 1 | `fxbuf_init()` calls `fxbuf_handoffResetAll()` after the forced-unit loop. Forced diagnostic units carry handoff rate 0 instead of 44,108. Fix: reset right after `fxbuf_clearOwners()`, then self-test, forced units, `BeginExit`. Diagnostic-only. | `FxBuffer.c:227–237` | ST1 §26.3 |
| 2 | Makefile link-budget recipe comments are TAB-indented, so they are echoed every build. Prefix with `@#`. | `Makefile:166–168` | ST1 |
| 3 | Bare `make` in an incremental tree builds `build/main.o` only, because `-include $(OBJS:.o=.d)` precedes `all:`. Add `.DEFAULT_GOAL := all` or move the include. Use `make all` meanwhile. | `Makefile:161` | ST1 (pre-existing) |
| 4 | `modNode_waveInterpGeneration` is `INCCMZ` with `= 1u`. `.dtcmz` is zero-filled, so the value starts at 0. Unverified whether anything relies on 1. | `modulationNode.c:67` | ST1 §24 (pre-existing) |
| 5 | The linker comment says "Stack lives at top of SRAM1 (0x20080000)". It is the top of SRAM2; MEMORY.md repeats this. Documentation only. | `STM32F765VIHx_FLASH.ld:48` | ST1 §24 |
| 6 | Garbled duplicated comment line in `mixer_calcNextSampleBlock()`. | `mixer.c:739–740` | ST5 F1 |
| 7 | Mixed tab/space indentation in five `presetManager.c` comment blocks. | `presetManager.c` ~1116–1217 | ST5 F2 |
| 8 | The FX return ramp is not reset while `off`. Revisit for a type that outputs immediately on `init`. | `mixer.c` | ST5 F3 |
| 9 | `tools/verify_bank_autosave.py` is stale (expects 129 HCNAMES rows). | tools | ST6 note |
| 10 | Scene Save phases 33–36 are unreachable dead code. | `filesystem.c` | ST6 note |
| 11 | The blank/space-only name stem trimming concern (G5). Logged in SCOPING_TARGETS, with the as-built `none` mapping noted. | filesystem/storageTypes | plan §14.2 |
| 12 | The local 40 KiB oversize-link ASSERT experiment was never run. It is useful to run before S073 Phase C. | local only | ST1 §21 |
| 13 | AutoSave tracking state at the `main.c` re-validation hook is unobserved. | runtime check | ST10 D5 |

### 10.3 Deferred Phase 5 features (plan §18.4)

- root `/Effect/` browser and Effect Load/Save item (A35; screen in plan
  §14.5);
- a buffer-using test type (A8), together with 10.1 item 1;
- FX lock removal (A15) and Scene copy/clear of the Effect (copy pass);
- MIDI mapping (A20; `expand7` is the hook);
- live record of FX moves (A22);
- track step-scale/shuffle playback (A10).

---

## 11. Phase 5 hardware acceptance checklist

Record PASS/FAIL, date and firmware `text` for each item. Use a production
image unless noted.

**A. Already confirmed:**

- Step 1: sine stress, CPU and smoke.
- Step 2: volume relocation.
- Step 5: `fx` fader mode dry path.
- Step 8: lock editing, `typ` inert on hold, `fwd` chase, run modes, `sel`
  (with `flt`).

**B. Storage fixtures** (card prep: delete root `.hcnames`, `.hcnamtmp`,
`.hcprms1`, `.hcprms2`; boot once; `.hcnames` should have 161 data rows).
Hand-written stems ≤ 8 characters:

| Fixture | Setup | Expected |
|---|---|---|
| F1 legacy | existing `effects.fx` v1 placeholder | loads `off`; Effect row `effects	-` |
| F2 v2 | `filter.fx`: `format=helicase.effect`, `version=2`, `type=flt`, `[params]` out 0 / level 127 / pan 64 / freq 40 / reso 90 / drive 0 / type 0 (no `[morph]`, no `[sequence]`) | loads `flt`; row `filter	-`; Morph copies params; sequence defaults |
| F3 missing | `.fx` deleted | loads; `off`; blank row |
| F4 malformed | `bad.fx` with `type=zzz` | Scene Load fails; resident Scene unchanged (atomicity). A root Scene folder gets the quarantine rename: use a copy. |
| F5 partial Bank | children 00=F2, 01=F3, 02=F4 | 00 and 01 load; 02 fails |

Then check:

- Scene Save of F2: `filter.fx` has v2, 7 `[params]`, Morphable-only
  `[morph]`, `[sequence]` with `run_mode=fwd`/`length=16`/`step_scale=1/16`
  and 7 lane lines; `sceneset.scg` has `effect_morph_amount=0`.
- Blank name saves `none.fx` and reloads blank.
- Bank Save/Load round trip.
- Reboot Case 1: filter live; `.hcprms` version 3; Effect region `66 6C 74`
  + cells; source bytes `FF 1F`.
- Optional Case 2: power-cycle before the AutoSave drain; the narrow reload
  works.
- Kit/Instrument Load never change the Effect.

**C. FX bus (with F2 loaded, a send raised):**

- Filtered copy on St1; send 0 silences it; smooth send ramps.
- `pre` fades both; `pst` fades dry only; `fx` fades the send only.
- Lowering voice `vol` drops the dry but not the send (pre-volume tap).
- A hard-panned voice's send is panned on the return.
- `1fx..6fx` step automation follows steps and restores at stop.
- Jack fallback: the Effect route follows OUT2 when MAIN is empty.
- Six full sends: no wrap or crackle.
- Record the `cpu` widget.
- Switching to an `off` Scene stops the return at once.

**D. Effect page walk-through:**

- Entry shows `TYP OUT VOL PAN`; SELECT1 toggles screens.
- SELECT2 shows `FRQ RES DRV TYP`; SELECT3..8 are ignored.
- The encoder scrolls across screens with the LED following.
- Pots edit (pot on `typ` inert; `mrp` double rate).
- Full view: `Filter  Frequncy`.
- `typ` gesture: to `Off` then back restores defaults and clears the
  sequence. Click in/out without turning = no change. **Repeat with a SELECT
  press mid-transaction: no commit.**
- A Scene switch discards the candidate.
- SHIFT Morph view edits endpoints only.
- TRACK mutes; SHIFT+TRACK selects.
- BAR/SHIFT+SELECT inert.
- Other modes unaffected; PERF SHIFT stays on PERF.
- Edits persist (AutoSave, Scene Save).
- No Euklid page is reachable.

**E. FX sequencer remainder:**

- Every mode × lengths 1/5/16 × scales `/64`, `/16`, `/8.`, `1br`, `2br`:
  chase speed matches; steps beyond `len` are dark.
- `sel` while stopped applies at once and survives play/stop and a Pattern
  change.
- Morph lane: lock 255 on step 1 and 0 on step 9. It holds between locks,
  and returns to retained on stop/start and Scene switch. In `sel`,
  selecting an unlocked step keeps the held value.
- A17 alignment: `fwd` A → `rev` B lands on index 11 after step 4.
- A type change clears locks and the held Morph.
- Track `scl` shows the 14 labels (default `/16`).
- Locks persist (AutoSave, `.fx` `lane.*`).
- A held `mrp` lock shows its lock value.
- CPU with 6 locked lanes at `/64`.

**F. Automation and LFO:**

- `fx` category: PAR cycles `out vol pan frq res drv typ`; duplicates are
  skipped; backward past the first returns to off. AMT domains: `typ` shows
  names, `pan` ±63. The detail label reads `FilterFrequncy`.
- **End rule:**
  - track 1 step 1 `frq`=10 → step 2 returns to the menu value;
  - steps 1–4 hold;
  - non-trigger steps apply.
- **Fallback:** an FX lock on step 2 is used after the Pattern overlay ends.
- **Two tracks:** last writer wins.
- **Probability-fail step:** holds the overlay.
- **Stop/start and Pattern change:** clear all overlays and `fxm`; the first
  step of the new pass still applies.
- **`fxm`:** in the `scn` list; 127 → 255; held until reset; masks FX Morph
  locks.
- **LFO `fx` `frq`:** positive, negative and bipolar polarity; amount 0 =
  none; DstVoice cycles `1…6 scn fx`.
- **Together:** Pattern `frq`=20 on step 1, FX lock 110 on FX step 5, LFO
  bipolar small. The wobble is centred on 20, then 110, then the menu value.
- **LFO `fxm`.**
- **Rebind:** an `off` Scene normalizes its own pair; `typ`→`off` stops the
  modulation and shows `off`.
- **Kit round trip** keeps `lfo_target_voice 8`.
- **CPU** with `flt` + 2 LFO → `fx` + 6 entries.

**G. Gate and fan-out** (Scenes 01/02 same Kit + `flt`; 03 `off`; 04 with a
different slot-3 Instrument):

- VOICE-held SEQ2 lights; SEQ3 and SEQ4 are rejected silently; SEQ2 again
  removes it; the active Scene can't be removed.
- With the mask 01+02, every Effect edit (parameters, SHIFT endpoints,
  run/len/scl, `mrp`, locks) reaches 02; 03 and 04 are untouched.
- `typ` fans out: 02 also becomes `off`, with its sequence cleared and
  common rows kept.
- **Directional:** 02's mask contains 01, 01's mask is self; change `typ` on
  01 → 02 loses the 01 bit and AutoSave marks the Bank field.
- A Kit, Instrument or Scene Load that changes a type drops mismatched
  members.
- **Boot:** a matched mask is kept; a hand-edited mismatched `bankset.bcg`
  bit is dropped.
- **VOICE fan-out regression:** VOICE parameter, track-7 decay, Scene
  settings, PERF `mrp`/voice Morph/`srt`.
- **Self-only mask:** single-Scene behaviour.

**H. AutoSave robustness:** OFF→ON and a power interrupt across an Effect
edit (AUTOSAVE.md validation list).

---

## 12. Architectural invariants introduced

- **Mutation boundary.**
  - Only SceneData writes `scene_t.effect` and `effect_morph_amount`.
  - UI, type hooks and the lock editor write only through the EffectsManager
    edit API.
  - Preset and AutoSave loaders use SceneData directly.
- **Foreground only.**
  - EffectsManager and FxBuffer are foreground.
  - TIM3 publishes `seq_fxEvent`, Effect step markers and the reset latch,
    and reads `seq_effectAutomationTracks`.
  - The drain runs after `voiceControl_processPending()` and before the
    mixer.
- **Arena.** No system clear, ever. The handoff `state_flags` tells an
  entering owner what is valid; otherwise clear before reading.
- **Type-relative indices.**
  - The same type implies the same row and lane meaning.
  - Runtime paths re-validate against the live type.
  - The mask gate and re-validation keep fan-out safe.
- **ID space.** 511 is never an Effect target; local 63 is never
  automatable.
- **Blank names.** Always pass explicit spaces to name builders (an empty
  stem becomes `inst`).

---

## 13. Files changed (complete list, `ee8d371` → working tree)

| Area | Files |
|---|---|
| New Effects framework | `Core/DSP/Effects/EffectTypes.h`, `EffectsManager.c/.h`, `EffectParamRows.h`, `FxBuffer.c/.h`, `StereoFilter/StereoFilterParameters.c/.h`, `StereoFilter/StereoFilterEffect.c/.h` |
| New UI/sequencer | `Core/Menu/menuEffects.c/.h`, `Core/Sequencer/StepScale.c/.h` |
| DSP | `Core/DSPAudio/mixer.c/.h`, `ResonantFilter.c/.h`, `wavetable.c/.h`; `Drum/DrumVoice.c/.h`, `Snare/Snare.c/.h`, `Cymbal/CymbalVoice.c/.h`, `HiHat/HiHat.c/.h`; `InstrumentManager.c/.h` |
| Retained data | `SceneData.c/.h`, `Autosave.c/.h`, `BankData.c/.h`, `SceneModTargets.c/.h`, `Pattern/PatternData.c/.h` (track-scale default), `Preset/presetManager.c/.h`, `Preset/presetMorphEngine.c` (comment) |
| Storage | `Core/Hardware/SD/storageTypes.c/.h`, `filesystem.c/.h` |
| UI | `Core/Menu/menu.c/.h`, `menuPages.h`, `MenuText.h`; `Core/Hardware/frontPanel/buttonHandler.c/.h`, `ledHandler.c` |
| Sequencer | `Core/Sequencer/sequencer.c/.h` |
| Platform | `main.c`, `config.h`, `Makefile`, `STM32F765VIHx_FLASH.ld`, `Core/Src/startup_stm32f765xx.s`, `Core/compat/cmsis_intrinsics.h` |
| Tools | `tools/link_budget.py` (new), `tools/decode_devlogs.py` |
| Docs | `EFFECTS_BUS_FEATURE_PLAN.md`, `SCOPING_TARGETS.md`, `MEMORY.md`, `README.md`; specs `EFFECTS_BUS_REFERENCE.md` (new), `AUTOSAVE.md`, `FILESYSTEM_SPEC.md`, `MODULE_INTERCHANGE_SPEC.md`, `BANK_PRESET_ARCHITECTURE.md`, `PATTERN_DYNAMIC_STACK.md`, `SRAM_MANIFEST.md`, `DEV_MODES.md`; logs `000_SESSION_INDEX.md`, this file; volatile notes; `S073_SESSION_STARTUP.md` (new) |

`CPU_USE_DSP_AUDIT.md` gained your Session 073 audit section in parallel
(input to `S073_CPU_USE_DSP_REDUCTION_REFACTOR.md`); it is not a Phase 5
change.

---

## 14. Disposable documents

These are superseded by this log, `EFFECTS_BUS_REFERENCE.md` and the updated
specs, and may be deleted:

- `S072_ST1_IMPLEMENTATION.md` … `S072_ST11_IMPLEMENTATION.md`.

`EFFECTS_BUS_FEATURE_PLAN.md` stays as the design record.
`S073_FLASH_EXPANSION.md` and `S073_CPU_USE_DSP_REDUCTION_REFACTOR.md` are
the next session's plans. They cite `S072_ST1_IMPLEMENTATION.md` §21, whose
content is preserved in §5 above and in `S073_SESSION_STARTUP.md`.
