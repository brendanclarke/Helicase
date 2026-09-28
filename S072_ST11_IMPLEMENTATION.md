# S072 Step 11: reference document, spec updates, finalization and handoff (implementation schedule)

This schedule implements Step 11 of `EFFECTS_BUS_FEATURE_PLAN.md` §17.1:

> `EFFECTS_BUS_REFERENCE.md` and spec updates; handoff.
> Gate: docs match the code.

It also covers the finalization Phase 5 needs before it can be closed.

- **Authority:** plan §1.1 (last bullet), §17.1 row 11, §17.3, and decisions
  G5 and A38; the Step 1–10 schedules and their assessments.
- **Baseline:** the reviewed Step 10 tree (`S072_ST10_IMPLEMENTATION.md` §12).
  - text 483,024 B; data 416 B; bss 426,336 B;
  - flash 483,440 / 491,520 B, **headroom 8,080 B**;
  - DTCM statics 4,448 B; FXBUF 126,624 B, margin 3,744 B.
- **Nature of this step:** documentation, plus comment-only source edits
  (§2). **No behavior change, no RAM, no flash.** The build gate (§11) is a
  byte-identical `text`/`data`/`bss` against Step 10.
- **Line numbers** refer to the current tree. The quoted old text is the
  authority.

---

## 0. Decisions and notes

### D1: two documents with two roles

- **`EFFECTS_BUS_FEATURE_PLAN.md` stays the design record:** decisions,
  alternatives and their reasons, and the step order. It gets a closeout
  header and its stale "as-of" statements are corrected (§5). It is not
  rewritten.
- **`knowledge_files/specification_reference/EFFECTS_BUS_REFERENCE.md`
  (new) is the as-built reference:**
  - what the code does;
  - where each thing lives;
  - how to use it;
  - how to add a type.

  Where the two disagree, the reference wins, and it says so in its first
  lines.

### D2: finalization included (beyond the §17.1 wording)

The deep dive found the following. Each item is documentation or a comment,
and each is needed for "docs match the code":

| # | Item | Why |
|---|---|---|
| F-a | 20 stale Phase 5 source comments ("later FX bus", "future .fx file", "Step 5 calls this", FX send "not audible", `fxm` "reserved", …) | They now contradict the code (§2) |
| F-b | AUTOSAVE.md: 5 stale Effect statements, including a wrong byte table (source bytes 430..431 are listed as reserved) | Authoritative format document (§4.1) |
| F-c | MODULE_INTERCHANGE_SPEC: missing the Step 8–10 API and still says "future"/"Step 10 adds" | Module ownership map (§4.3) |
| F-d | FILESYSTEM_SPEC: ID-space bounds stop at 390; "future full effect stack" | §4.2 |
| F-e | BANK_PRESET_ARCHITECTURE: `fxm` row still "Reserved"; no Effect-overlay entry | §4.4 |
| F-f | DEV_MODES: `DEV_EFFECT_FORCE_TYPE` does not mention Step 10 fan-out | §4.5 |
| F-g | SCOPING_TARGETS: Phase 5 has no closeout or plan pointer; G5 required logging the blank-stem naming defect and it is **not logged**; Phase 7 still uses the retired 8,820 B unit | §6 |
| F-h | MEMORY.md: layout tree has no `Core/DSP/Effects/`; the spec index lacks the new reference; two stale S072 lines | §7 |
| F-i | Session 072 has no handoff log or index entry | §8 |
| F-j | The outstanding hardware gates of Steps 6–10 are spread across five documents | a consolidated Phase 5 acceptance checklist (§9) |

### D3: as-built deviations from the plan

These are recorded in the reference document and the handoff. **None is
fixed here**, because none changes current behavior:

1. **Handoff on a same-type Scene switch (plan §12.6).**
   - The plan says the handoff "is still refreshed" on a same-type switch.
   - `effects_activateScene()` calls `effects_switchRuntime()` only when the
     type differs, so a same-type switch leaves the handoff describing the
     last *type change*.
   - No current type uses the arena (A8), so this has no effect.
   - **Follow-up** when the first buffer-using type lands: refresh
     `fxbuf_handoffBeginExit()` + `export_handoff` on same-type activation,
     without calling `init`.
2. **§7.2 item 7 (`fx` LFO re-validation at the `typ` click-out).**
   - This is satisfied functionally (S072_ST9 D7): an invalid token
     contributes nothing and displays `off`, and the retained cell is
     normalized at the next Scene-activation rebind.
   - There is no immediate rebind on `typ`.
3. **Plan §9 "Apply".** The rescan-every-block model is final (Step 4). There
   is no dirty-cell notification path.
4. **Plan §10.1.** `seq_effect_automation_dirty` is realized as
   `effects_automation.active` in EffectsManager (S072_ST9 D1).

### D4: dev knobs are kept

- `DEV_FXBUF_FORCE_VOICE_UNITS` is the only way to present the minimum
  Effect share before Phase 7.
- `DEV_EFFECT_FORCE_TYPE` remains a bench diagnostic.

Both are compiled out when `DEV_MODE_DIAGNOSTIC=0` and default to 0. Only
their comments are corrected (§2, C19; §4.5).

### D5: G5 check for comment-only edits outside `Core/DSP/Effects` and `menuEffects`

Every edited comment is a Phase 5 statement that the Phase 5 code has since
made false:

| File | Comments | Why they are in scope |
|---|---|---|
| `SceneModTargets.c/.h` | FX_SEND, `fxm`, "future effects parameters" | Phase 5 targets |
| `SceneData.h` | "future .fx file" | Phase 5 record |
| `Autosave.c` | "later Step 6 boot reader" | Phase 5 reader |
| `main.c` | DEV hook comment | Phase 5 hook |

Not touched, because the statements remain true:

- `filesystem.c:2599` (the Effect values are Scene-owned; the comment's point
  still holds);
- `asyncfatfs.h:275` ("future Effect directory-shaped save phases" means the
  library browser, which is still future).

### D6: disposition of the S072 step documents

- Keep `S072_ST1..ST11_IMPLEMENTATION.md` until the consolidated acceptance
  checklist (§9) passes. They hold the per-gate detail it references.
- After that, they may be deleted, like the S070/S071 task documents. The
  handoff log (§8), the reference (§3) and the specs carry everything
  durable.
- `S073_FLASH_EXPANSION.md` and `S073_CPU_USE_DSP_REDUCTION_REFACTOR.md` are
  the user's next-session plans. The handoff points at them, and this step
  does not edit them.

### D7: out of scope

- Any behavior change, including the D3 follow-ups.
- The deferred Phase 5 items (§8, "Deferred"):
  - library browser and Effect Load/Save item (A35);
  - buffer-using type (A8);
  - lock removal (A15);
  - Scene copy/clear of the Effect;
  - MIDI (A20);
  - live record (A22);
  - track step-scale playback (A10).
- The pre-Phase-5 staleness found in FILESYSTEM_SPEC lines 256–262 (the
  `AutomationNode` notes). That belongs to a general spec pass, not Phase 5
  (G5).

---

## 1. Change index

| # | File | Action | § |
|---|---|---|---|
| C1–C20 | 11 source files (comment-only) | modify comments | 2 |
| R | `knowledge_files/specification_reference/EFFECTS_BUS_REFERENCE.md` | **new** | 3 |
| S1 | `knowledge_files/specification_reference/AUTOSAVE.md` | modify | 4.1 |
| S2 | `knowledge_files/specification_reference/FILESYSTEM_SPEC.md` | modify | 4.2 |
| S3 | `knowledge_files/specification_reference/MODULE_INTERCHANGE_SPEC.md` | modify | 4.3 |
| S4 | `knowledge_files/specification_reference/BANK_PRESET_ARCHITECTURE.md` | modify | 4.4 |
| S5 | `knowledge_files/specification_reference/DEV_MODES.md` | modify | 4.5 |
| S6 | `knowledge_files/specification_reference/SRAM_MANIFEST.md` | modify | 4.6 |
| S7 | `knowledge_files/specification_reference/PATTERN_DYNAMIC_STACK.md` | verify only | 4.7 |
| P | `EFFECTS_BUS_FEATURE_PLAN.md` | modify | 5 |
| T | `SCOPING_TARGETS.md` | modify | 6 |
| M | `MEMORY.md` | modify | 7.1 |
| RD | `README.md` | modify | 7.2 |
| H1 | `knowledge_files/log_archive/072_SESSION_HANDOFF_LOG.md` | **new** | 8 |
| H2 | `knowledge_files/log_archive/000_SESSION_INDEX.md` | add entry | 8 |

---

## 2. Comment-only source corrections

Each row gives the file, the anchor line, the **old** comment text, and the
**replacement**. Only comment text changes; the code tokens around it are
unchanged.

**C1. `Core/DSP/Effects/EffectTypes.h` 44–50**

Old:

> "Effect data contract shared by SceneData, AutoSave, and the future
> EffectsManager registry, storage, Menu, and sequencer. … SceneData.h embeds
> effect_record_t while EffectsManager.c will include SceneData.h; …"

New:

```c
 * Effect data contract shared by SceneData, AutoSave, EffectsManager,
 * `.fx` storage, menuEffects, and the FX sequencer.
 *
 * What: constants, enums, and the retained per-Scene Effect record layout,
 * with no runtime or module dependencies. Why a separate header: SceneData.h
 * embeds effect_record_t while EffectsManager.c includes SceneData.h;
 * keeping this contract data-only avoids a header cycle. Authority:
 * EFFECTS_BUS_REFERENCE.md (as built) and EFFECTS_BUS_FEATURE_PLAN.md
 * sections 7, 10, and 11 (design).
```

**C2. `EffectTypes.h` 81**

Old:

> `/* Stored run modes; text tokens are introduced by the later file/UI steps. */`

New:

```c
/* Stored run modes. `.fx` and the Effect page use the tokens
 * fwd/rev/pip/rnd/sel in this order (storageTypes.c, menuEffects.c). */
```

**C3. `EffectTypes.h` 110–114**

Old:

> "… not part of this record or the future .fx file. …"

New:

> "… not part of this record or the `.fx` file (it is stored in
> `sceneset.scg` and as AutoSave Scene parameter 40). …"

**C4. `EffectTypes.h` 133–137**

Old:

> "… so Pattern automation accepts 0..62 while future LFO/FX paths may still
> address local 63."

New:

> "… so Pattern automation accepts 0..62 while FX-sequencer lanes and the LFO
> `fx` namespace may still address local 63."

**C5. `Core/DSP/Effects/EffectsManager.h` 44–58.** In the module comment:

- replace "(Session 072, Effects Phase 5 step 4)" with "(Session 072, Effects
  Phase 5)";
- replace "publishes the common return settings used by the later FX bus."
  with "publishes the common return settings consumed by the mixer's FX
  return.";
- replace the last line ", and the later FX-bus steps." with ", and the
  mixer FX bus.".

**C6. `EffectsManager.h` 73**

Old:

> `/* Runtime I/O shape flags used by the future mixer bus. */`

New:

```c
/* Runtime I/O shape flags; the mixer sizes the FX bus from them (0 = off). */
```

**C7. `EffectsManager.h` 138**

Old:

> `/* Optional four-cell SELECT layout for future Effect pages. */`

New:

```c
/* Optional per-type SELECT layout for the Effect page (NULL = default, §13.2). */
```

**C8. `Core/DSP/Effects/EffectsManager.c` ~99–104 (the `effects_state_t` comment)**

Old:

> "… and the common return settings consumed by the later FX bus."

New:

> "… and the common return settings consumed by the mixer's FX return. The
> spare `reserved` byte keeps the 84-byte layout."

**C9. `EffectsManager.c` 1377 (`effects_process()`)**

Old:

> `/* Step 5 calls this after effects_service(); `off` has no process hook. */`

New:

```c
/* The mixer calls this after effects_service() in the same block; `off` has
 * no process hook and the mixer skips the bus entirely for it. */
```

**C10. `Core/DSP/Effects/StereoFilter/StereoFilterParameters.c` 24**

Old:

> "The frequency default is 64 so the diagnostic filter bench has an audible
> cutoff in Step 5."

New:

> "The frequency default is 64, so a freshly selected `flt` is audibly
> filtered at mid cutoff."

**C11. `Core/DSP/Effects/StereoFilter/StereoFilterEffect.c` (the `stereoFilter_process()` comment)**

Old:

> "… the later mixer return performs the one final bus saturation. A missing
> right channel is accepted so the operation remains safe when the Step 5 bus
> negotiates mono input."

New:

> "… the mixer return performs the one final bus saturation. A missing right
> channel is accepted so the operation stays safe if the registry row is ever
> changed to mono input."

**C12. `Core/DSP/Effects/FxBuffer.h` 174–175**

Old:

> "It is registered by EffectsManager in a later Phase 5 step and is NULL
> during Step 1."

New:

> "It is registered by effects_init(); it is NULL only before that call."

**C13. `Core/Bank/Scene/SceneData.h` 186–187**

Old:

> "This is a Scene parameter, not part of the retained Effect record or
> future .fx file."

New:

> "This is a Scene parameter, not part of the retained Effect record or the
> `.fx` file."

**C14. `Core/Bank/Scene/SceneModTargets.c` 81–86 (FX-send rows comment)**

Replace the block with:

```c
    /*
     * Step-only per-voice FX-send targets (IDs 398..403).
     *
     * Inputs: seven-bit Pattern values. Output: a runtime-only send overlay
     * (preset_setFxSendStepOverride()) that the mixer reads each block
     * through preset_getEffectiveFxSendAmount(), so the send is audible on
     * the live FX bus (Session 072 step 5). Keeping these entries in the
     * shared table makes Menu, Pattern validation, and the foreground drain
     * agree on one namespace.
     */
```

**C15. `SceneModTargets.c` 184–190 (`sceneModTarget_valid()` comment).**
Replace the sentence "Future effects can be added to the table without
changing Menu or InstrumentManager traversal logic." with:

```c
     * use. Scene-level targets (including Effect Morph `fxm`) are added to
     * the table without changing traversal logic; Effect parameters are the
     * separate block-7 namespace owned by EffectsManager.
```

**C16. `Core/Bank/Scene/SceneModTargets.h` 31–37 (the `FX_SEND` kind)**

Replace with:

```c
    /*
     * Per-voice FX send amount.
     *
     * Inputs: voice_slot and a 0..127 stored value. Output: step automation
     * sets the runtime send overlay; the mixer applies the effective send to
     * the live FX bus each block (Session 072 step 5).
     */
```

**C17. `SceneModTargets.h` 74–80 (module comment).** Replace "such as
per-voice Morph, global decimation, and future effects parameters." with:

> "such as per-voice Morph, global decimation, audio routing, FX sends, and
> Effect Morph (`fxm`). Effect parameters are block-7 IDs owned by
> EffectsManager."

**C18. `Core/Bank/Scene/Autosave.c` 927**

Old:

> "Affiliate: the SceneData Effect setters and the later Step 6 boot reader."

New:

> "Affiliates: the SceneData Effect setters and autosave_applyEffectPayload()
> (the boot reader)."

**C19. `main.c` 1307–1313 (the `DEV_EFFECT_FORCE_TYPE` block comment).**
Replace "This bench hook is absent from production builds and exists until
the Step 7 Effect page is available." with:

```c
     * This bench hook is absent from production builds. Since Step 10 the
     * change fans out through the active VOICE edit mask like a `typ` edit
     * (masks are self-only at boot unless restored), and it clears the
     * forced Scenes' FX sequences (F3). Keep DEV_EFFECT_FORCE_TYPE at 0
     * outside deliberate registry bench tests.
```

**C20. `Core/Menu/menuEffects.c`.** Run the §11 grep. If any comment still
says "Step 10 adds fan-out" or "active Scene only until Step 10", replace it
with "Edits fan out through EffectsManager (Session 072 step 10)." The Step
10 review found none in `menuEffects.c` itself, so this row is a
verification step.

---

## 3. New: `knowledge_files/specification_reference/EFFECTS_BUS_REFERENCE.md`

Create the file with the content below, copied verbatim. The outer `~~~`
fence is only for this schedule and is not part of the file. Every fact in it
was checked against the Step 10 tree; the facts it depends on are listed in
§11 gate 4.

~~~markdown
# Effects Bus Reference

As-built reference for the Phase 5 Effect system (Session 072, Steps 1–11).
It describes what the firmware does and how to extend it. The design history
and every decision (A1–A46, F1–F6, G1–G7) are in
`EFFECTS_BUS_FEATURE_PLAN.md`. **Where the two disagree, this document
describes the code**; §13 lists the known differences.

---

## 1. Concepts

- **One Effect per Scene.** Each of the 16 resident Scenes owns one
  `effect_record_t`. A Kit or Instrument load never changes it. Scene and Bank
  Load/Save carry it.
- **Type.** The registry id is `0` = `off` (default) or `1` = `flt` (stereo
  filter). Files and AutoSave store the three-character token, never the id.
- **Parameters.** Each type has up to 64 local parameters, and each has a
  normal and a Morph value:
  - `0 out` (route), `1 vol`, `2 pan` are common to every type;
  - `3..63` are type-specific.
- **Effect Morph.** A Scene setting (`effect_morph_amount`, 0..255) that
  interpolates normal → Morph for MORPHABLE rows. The PERF global `mrp` sets
  it too.
- **FX bus.** Each voice has a pre-volume send into one bus. The active type
  processes the bus in place, and the result returns 100 % wet to a routed
  output.
- **FX sequencer.**
  - 16 steps × 16 lanes (lane 0 = Effect Morph), each with a lock bit;
  - run modes `fwd rev pip rnd sel`;
  - length 1..16;
  - step scale from the table shared with Pattern tracks.
- **Modulation.**
  - Pattern-step automation (`fx` category, IDs 448..510);
  - LFO (`fx` namespace, value 8);
  - `fxm` (Scene target 404) for Effect Morph.
- **Shared DTCM arena.** The rest of DTCM after statics is shared between the
  Effect (bottom) and future buffer voices (top). No type uses it yet.
- **Edit fan-out.** Effect edits reach every Scene in the active VOICE edit
  mask. Masks only group Scenes with the same Effect and Instrument types.

---

## 2. Module map

| Area | File(s) | Owns |
|---|---|---|
| Data contract | `Core/DSP/Effects/EffectTypes.h` | `effect_record_t` (420 B), step shape, lane/run/length constants, block-7 ID helpers |
| Registry, runtime, resolution | `Core/DSP/Effects/EffectsManager.c/.h` | registry (`off`, `flt`), activation/type change, the per-block §6 resolution, edit API + fan-out, FX-sequencer consumption, Pattern/LFO overlays, target validation |
| Row macros | `Core/DSP/Effects/EffectParamRows.h` | `EFFECT_COMMON_ROWS`, `EFFECT_ROW`, `EFFECT_ROW_MENU`, modulation-domain presets |
| Arena | `Core/DSP/Effects/FxBuffer.c/.h`, linker `.dtcm_fxbuf` | arena bounds, voice units, Effect share, handoff record |
| Stereo filter | `Core/DSP/Effects/StereoFilter/StereoFilterParameters.c/.h`, `StereoFilterEffect.c/.h` | descriptors/names, runtime + ops |
| Float SVF | `Core/DSPAudio/ResonantFilter.c` `SVF_calcBlockZDFFloat()` | float-I/O ZDF block for Effects |
| Bus | `Core/DSPAudio/mixer.c` | send taps, fader modes, bus accumulation, process call, return |
| Retained data | `Core/Bank/Scene/SceneData.c/.h` | the record, change-aware setters, `effect_morph_amount`, `scene_editLayoutMatches()` |
| Masks | `Core/Bank/BankData.c/.h` | per-Scene VOICE edit masks, `bank_revalidateVoiceEditMasks()` |
| Effect Morph target | `Core/Bank/Scene/SceneModTargets.c/.h` | `fxm` (ID 404, LFO + automation) |
| Clock and drain | `Core/Sequencer/sequencer.c/.h`, `StepScale.c/.h` | FX step latch, Effect step markers, reset latch, the shared scale table |
| LFO and validation | `Core/DSP/Instruments/InstrumentManager.c/.h` | LFO `fx` namespace (8), Effect LFO adapter, AUTOMATION validation of block 7 |
| UI | `Core/Menu/menuEffects.c/.h`; `menu.c` (delegation, `fx` step-automation category, LFO `fx` display); `buttonHandler.c`; `ledHandler.c` | the SHIFT+PERF Effect page |
| Storage | `Core/Hardware/SD/storageTypes.c` (`.fx` v2), `filesystem.c` (Scene/Bank `.fx`, HCNAMES rows 145..160) | files and names |
| Persistence | `Core/Bank/Scene/Autosave.c/.h` | the 512-byte Effect region per Scene; Scene parameter 40 |
| Dev knobs | `config.h` `DEV_FXBUF_FORCE_VOICE_UNITS`, `DEV_EFFECT_FORCE_TYPE` | diagnostics only (`DEV_MODES.md`) |

---

## 3. Signal flow (one 32-frame block)

Main loop order: `voiceControl_processPending()` →
`seq_drainPendingAutomation()` → `mixer_calcNextSampleBlock()`.

```
mixer_calcNextSampleBlock():
  instrumentManager_dispatchRuntimeLfos()     LFO samples → Effect LFO entries
  effects_service()                           §6 resolution; write_param on change
  fx_active = (effects_activeIoFlags() != 0)  `off` → no bus work at all
  if fx_active: clear bus (sample_mx_t L/R × 32)
  for each voice slot:
      render (pre-volume) → mixer_decimateBlock()
      mixer_faderGains(): mix = vol · F_mix     send = fxSend/127 · F_send
      dry:  × mix, pan, route (jack fallback) → satAdd32 into DAC buses
      send: mixer_addVoiceToFxBus(), ramped from mixer_send_last_gain[slot]
            stereo-in type: panned voice into L and R
            mono-in type:   unpanned voice into L
            int16 × 256 → sample_mx_t, satAdd32
  if fx_active:
      bus → float (× 1/8388352), R zeroed for mono-in/stereo-out
      effects_process(io)                       in place; io.share = current share
      return gains:
         stereo-out: balance gL = pan<=64 ? 1 : (127-pan)/63,
                             gR = pan>=64 ? 1 : pan/64
         mono-out:   constant power squareRootLut[127-pan], [pan]
         × level (vol/127); ramped from mixer_fx_return_last_gain[2]
      route(out) with mixer_checkOutJackAvailable() → saturated add
```

`fxSend` is `preset_getEffectiveFxSendAmount(scene, slot)`, which honors the
FX-send step override. The fader never changes the stored send.

### Fader modes (`fader_setting`)

| Mode | Label | Dry mix | Send |
|---|---|---|---|
| 0 | `pre` | vol × fader | send × fader |
| 1 | `pst` | vol × fader | send |
| 2 | `fx` | vol | send × fader |

- There is no bus mute; track mutes stop triggers and automation, so tails
  ring out.
- The DTCM cost is the bus (256 B) plus ramp state (32 B).

---

## 4. Type definition contract

### 4.1 Parameter descriptor (`effect_param_descriptor_t`)

| Field | Meaning | Why |
|---|---|---|
| `base` (`ParamDescriptor`) | file key, short/long/category labels, dtype, flags MORPHABLE/MODULATABLE/AUTOMATABLE, `mod_domain` | shared with Menu, target pickers and storage, so Effect rows behave like Instrument rows |
| `default_value` | the single default for both endpoints on a type change | F3: one default, no separate Morph default |
| `max_value` | inclusive retained/runtime clamp | every writer clamps to it |
| `effect_flags` | `EFFECT_PARAM_FLAG_WIDE8` (0..255 domain), `EFFECT_PARAM_FLAG_BUFFER_DEPENDENT` | automation expansion; runtime-only buffer clamp |
| `expand7` | Pattern 7-bit → stored value | **required** for an AUTOMATABLE WIDE8 row; `effect_expand7Linear()` = `v<127 ? 2v : 255` |

Rules:

- **Rows 0..2** come from `EFFECT_COMMON_ROWS(out_flags, vol_flags,
  pan_flags)`, keys `effect_audio_out`, `effect_level`, `effect_pan`.
- **Local 63** must never be AUTOMATABLE: its ID would be 511, the Pattern
  off sentinel. It may still be sequenced, modulated and morphed.
- **LFO** needs MODULATABLE **and** a `mod_domain` other than
  `INSTRUMENT_MOD_DOMAIN_NONE`.

### 4.2 Registry row (`effect_registry_entry_t`, in `effects_registry[]`)

| Field | Meaning |
|---|---|
| `token3` / `abbrev5` / `full8` | persisted token (unique, exactly 3 chars) / 5-char abbreviation / ≤8-char full name (the `typ` view) |
| `io_flags` | `EFFECT_IO_MONO_IN`/`STEREO_IN` and `MONO_OUT`/`STEREO_OUT`; 0 = `off` |
| `descriptors`, `descriptor_count` | rows 0..count−1 (3 ≤ count ≤ 64) |
| `lanes[16]` | lane 0 = `EFFECT_LANE_MORPH_SOURCE`; lanes 1..15 = descriptor index or `EFFECT_LANE_NONE`; no duplicates |
| `select_layout` | NULL = default Effect-page layout (§8.1) |
| `ops` | DSP operations (§4.3); NULL for `off` |
| `ui` | optional page hooks (§8.4) |
| `runtime_bytes` | size of the type's runtime struct; must fit `effects_runtime_t` |
| `buffer_min_bytes`, `buffer_pref_bytes` | 0 = no arena use |

The registry is **append-only in token terms**: ids may be reordered in the
future, tokens never change meaning.

### 4.3 Operations (`effect_type_ops_t`)

| Op | When | Contract |
|---|---|---|
| `init(rt, handoff)` | the type becomes active (Scene activation with a different type, or a `typ` change) | the runtime union is already zeroed; **clear or adopt** arena regions per the handoff (§7.3) |
| `export_handoff(rt, out)` | the type is exited | describe the arena use (`state_flags`, bounds, rate, pointers [12..15]) |
| `write_param(rt, index, value)` | an effective value changed | **type rows 3..63 only**; EffectsManager applies 0..2 itself |
| `process(rt, io)` | once per block when the type is not `off` | in place, float, `io->frames` = 32; `io->r` may be NULL for mono |
| `buffer_changed(rt, share)` | the share moved or resized (voice units acquired/released) | re-seat positions; a forced re-resolution follows |
| `effective_max(index, share)` | for BUFFER_DEPENDENT rows | runtime clamp only; stored values are never rewritten (A30) |

`effect_io_t`:

- `l` (always);
- `r` (stereo-in, or a zeroed output channel for mono-in/stereo-out);
- `frames`;
- `channels` (the *input* count);
- `share`.

### 4.4 Registry self-check (DEV builds)

`effects_registryCheckResult()` returns 0 when the registry passes. It is
shown on the `FxBf` boot screen. Codes:

| Code | Failure |
|---|---|
| 1 | `descriptor_count` < 3 or > 64 |
| 2 | rows 0..2 keys are not the common keys |
| 3 | an AUTOMATABLE row at local ≥ 63 |
| 4 | an AUTOMATABLE WIDE8 row without `expand7` |
| 5 | lane 0 is not the Morph source |
| 6 | a lane names a missing or duplicate descriptor |
| 7 | `default_value` > `max_value` |
| 8 | `runtime_bytes` > `sizeof(effects_runtime_t)` |
| 9 | a name length is wrong (token 3, abbreviation 5, full ≤ 8) |
| 10 | a duplicate token |

---

## 5. Retained data

### 5.1 `effect_record_t` (420 B, inside `scene_t`)

| Field | Bytes | Default |
|---|---|---|
| `type` | 1 | `off` |
| `seq_run_mode`, `seq_length`, `seq_step_scale` | 3 | `fwd`, 16, index 4 (`1/16`) |
| `normal[64]`, `morph[64]` | 128 | common 0/127/64; type rows = type default |
| `steps[16]` = {`lock_mask` u16, `value[16]`} | 288 | no locks |

- `effect_morph_amount` lives in `scene_settings_t` (Scene parameter 40),
  **not** in the record or the `.fx` file.
- **Only SceneData writes it.** Every setter is change-aware, marks the exact
  AutoSave cell, and invalidates the Scene's card-clean authority.
- Whole-record commits use `scene_effectRecordForWholeCommit()` +
  `scene_finishEffectWholeCommit()`.

### 5.2 Type change (`effects_changeType()`, the `typ` click-out)

For each reached Scene (§9):

1. The exiting type exports its handoff.
2. `type` is set.
3. Rows 3..63 are set to the new type's default in both images (0 where the
   type has no row).
4. All 16 steps are cleared, including the Morph lane.
5. Rows 0..2, the sequence settings and Effect Morph are kept.
6. The whole Effect region is marked for AutoSave.
7. On the active Scene: the held Morph and the parameter overlays are
   cleared, and the runtime switches (`init`).

Then the edit masks are re-validated.

---

## 6. Resolution order (`effects_service()`, every render block)

```
consume TIM3 FX latch (RESET / STEP index)
active step:  sel → selected step (always valid);  others → clock step while running
Morph base:   Pattern fxm overlay  >  held FX Morph lane  >  effect_morph_amount
Morph      =  base + Σ LFO(fxm)                          (clamp 0..255)
for each descriptor i:
   menu(i)  = MORPHABLE ? interpolate(normal, morph, Morph) : normal
   seq(i)   = active step locks lane(i) ? lane value : menu(i)
   held(i)  = Pattern overlay active ? overlay value : seq(i)
   eff(i)   = held(i) + Σ LFO(i)  clamped to mod_domain  (MODULATABLE rows only)
   eff(i)   = min(eff(i), max_value); BUFFER_DEPENDENT → min(·, effective_max)
   if eff(i) != last_applied[i] (or a forced pass): apply
            (0 → route, 1 → level/127, 2 → pan, else write_param)
```

- **Held Morph lane.** A Morph-lane lock holds through later unlocked steps
  until another Morph lock or a reset. In `sel`, selecting an unlocked step
  keeps the held value.
- **LFO math.** Each LFO entry is base-independent: a direction (toward max
  or toward min) and a depth 0..255, applied around the *current* held value.
  The math is the same as the Instrument LFO (`modNode_shapeParameterU16`).
- **Forced passes** happen after a type switch, a share change, or Scene
  activation.

---

## 7. Shared DTCM arena (`FxBuffer`)

### 7.1 Layout

- The arena is `.dtcm_fxbuf` (NOLOAD) from `ALIGN(32)` after `.dtcmz` to the
  end of DTCM. It measures **126,624 B**. A linker `ASSERT` keeps it
  ≥ 122,880 B (120 KiB).
- It is never zeroed or cleared by the system (A31).
- **Voice units:** 2,208 samples × 2 B = 4,416 B (50.06 ms at 44,108 Hz);
  12 in total, at most 2 per slot. They are allocated from the top.
- **The Effect share** is contiguous from the bottom: the arena minus the
  claimed units (minimum about 73.6 KB).

### 7.2 API

- `fxbuf_effectShare()`;
- `fxbuf_voiceAcquire(slot, n≤2)` / `fxbuf_voiceRelease(slot)`: a share
  change calls the registered callback, which calls `buffer_changed` and
  forces re-resolution;
- `fxbuf_voiceUnit()`;
- `fxbuf_handoffBeginExit()` / `fxbuf_handoff()` /
  `fxbuf_handoffSetVoiceUnit()`.

Everything is foreground-only.

### 7.3 Handoff rule: clear unless you adopt

- `fxbuf_handoff_t` (180 B) records the exited Effect's type, channels, bits,
  `state_flags` (`FXBUF_STATE_EFFECT_WRITTEN`, `FXBUF_STATE_VOICE_WRITTEN`),
  rate, share bounds, unit owners and rates, and read/write offsets (arena
  byte offsets, not pointers).
- At boot `state_flags = 0`, meaning nothing is valid.
- An entering owner must **clear any region it will read unless it
  deliberately adopts** content the handoff marks as valid for it.
- `flt` uses no arena memory and ignores the handoff.

---

## 8. Effect page (SHIFT+PERF, `EFFECT_PAGE`)

### 8.1 Layout

- **SELECT 1** holds screen 1 `typ out vol pan` and screen 2
  `run len scl mrp`. Repeated SHIFT+PERF toggles between them.
- **SELECT 2..8** hold the type rows 3..63 in order, 4 per screen, with
  `ceil(N/28)` screens per button.
- Re-pressing the current SELECT cycles its screens.
- The encoder scrolls through every screen; a click opens the full view.
- A type may supply `select_layout` instead.

### 8.2 Editing

- **`typ`** changes only through the encoder: click in, turn to browse, click
  out to commit. The pots are inert on it.
- **SHIFT** shows and edits Morph endpoints; single-valued cells ignore it.
- `scl` labels: `/64 32t /32 16t /16 /8t 16. /8 /4t /8. /4 /2 1br 2br`.

### 8.3 SEQ buttons

- **Hold** one or more steps, then edit a sequenceable cell: the value is
  written and locked on each held step.
  - A locked lane shows the held step's value underlined.
  - `typ`/`run`/`len`/`scl` and non-sequenceable cells are inert while
    holding.
- **Tap or hold** in `sel` jumps to the step.
- **LEDs:**
  - steps with any lock (within the length) are lit;
  - the FX chase runs while playing (not in `sel`);
  - the `sel` step blinks;
  - the Pattern chase is suppressed on this page.
- Locks cannot be removed yet (A15).

### 8.4 TRACK, BAR and hooks

- **TRACK** mutes; **SHIFT+TRACK** selects the track.
- **SELECT/BAR with SHIFT, and BAR alone,** do nothing unless the type has a
  hook.
- **`effect_ui_hooks_t`:** `select`/`track`/`bar` (button, shift, pressed →
  handled) and `render_leds`. Hooks must not block, must write only through
  the EffectsManager edit API, and must never touch the filesystem.

---

## 9. Edit fan-out and the layout gate

- **Reach.** Every Effect edit made on the active Scene reaches the active
  Scene's VOICE edit mask:
  - parameters and Morph endpoints;
  - `run`/`len`/`scl`;
  - lane locks;
  - `mrp`;
  - `typ`.
- **Same-type guard.** Parameter, lock and sequence edits skip any masked
  Scene of a different Effect type. `mrp` and `typ` reach all masked Scenes.
- **Gate.** In VOICE-held SEQ, adding a Scene to the mask requires
  `scene_editLayoutMatches()`: the same Effect type and the same six
  Instrument types. Removing is always allowed.
- **Re-validation.** `bank_revalidateVoiceEditMasks()` drops members that no
  longer match, and marks AutoSave. It runs after:
  - Kit, Scene, Bank, All and Performance Load (`menu_startSoundApply()`);
  - Instrument Load;
  - an empty-Bank load;
  - end of boot;
  - `effects_changeType()`.
- **PERF `mrp`** bulk-sets voice Morphs and Effect Morph on every masked
  Scene.

---

## 10. FX sequencer

- **Clock** (TIM3, 96 PPQ, `seq_elapsedPpqTicks`): `T = stepScale_ticks(scl)`
  and `n = ticks / T`. At each boundary:

  | Mode | Index |
  |---|---|
  | `fwd` | `n mod L` |
  | `rev` | `L-1-(n mod L)` |
  | `pip` | `p = n mod 2L`; `p<L ? p : 2L-1-p` (endpoints play twice) |
  | `rnd` | a hardware-RNG draw (repeats allowed) |
  | `sel` | no clock |

- **Latch.** TIM3 publishes only the newest RESET/STEP + index
  (`seq_fxTakeEvent()`). It is also published on the Pattern-boundary
  early-return path.
- **Scene switch.** Position is master-clock-aligned to the new Scene. Until
  the new Scene's next boundary, only menu values apply (except in `sel`,
  where the selected step applies at once), and the held Morph is cleared.
- **Transport.** While stopped, only `sel` applies. Start/stop/reset publishes
  RESET.
- **Shared scale table** (`StepScale.c`), ticks:
  `6 8 12 16 24 32 36 48 64 72 96 192 384 768` (default index 4 = 1/16).
  Pattern track scale shares the index but playback ignores it (A10).

---

## 11. Target IDs, automation and LFO

### 11.1 ID layout

| IDs | Owner |
|---|---|
| 0..383 | voice slots (6 × 64) |
| 384..447 | Scene targets; 384..404 in use; **404 = `fxm`** |
| 448..510 | **Effect local 0..62** (`448 + local`) |
| 511 | Pattern off sentinel |

### 11.2 Pattern automation (`fx` category)

- The STEP automation page lists the viewed Scene's AUTOMATABLE rows.
  Values are 7-bit; WIDE8 rows show and apply `expand7(v)`.
- **Apply.** `seq_drainPendingAutomation()` → `effects_applyAutomation(track,
  local, v)` sets an overlay owned by the writing track (last writer wins).
- **End.** TIM3 queues an **Effect step marker** (pending identity bit 11)
  before the entries of any track that owns an overlay. The drain ends that
  track's overlays that its new step does not rewrite. The value falls back
  to the FX lock on the current step, else to the menu value.
  - Steps whose probability fails, and muted tracks, keep their overlays.
- **Reset.** A reset latch, set with every FX RESET, is taken at the top of
  the drain. It clears all overlays and the `fxm` override before the new
  pass applies. Scene activation clears them too.
- **`fxm`.** The Pattern value expands like voice Morph (127 → 255) and holds
  until the Scene-rule reset.

### 11.3 LFO

- `lfo_target_voice` 8 = `fx`. `lfo_target_param` = the Effect local
  (MODULATABLE).
- `fxm` is reached through the `scn` namespace.
- The adapter stores direction/depth per source slot and pair (6 × 2
  entries, §6). Clearing or replacing a target removes its entry.
- **Re-validation.** Scene activation's all-source rebind re-validates `fx`
  tokens against that Scene's type. After a `typ` change, a now-invalid token
  contributes nothing and shows `off` until the next rebind.
- **Kit files** store the namespace byte (8 survives reload).
- Velocity cannot target Effects.

### 11.4 Priority

Automation, FX lock and LFO precedence is the same as for Instrument
parameters (§6).

---

## 12. Storage and persistence

### 12.1 `.fx` v2

```
format=helicase.effect
version=2
type=flt
[params]      <file_key>=<0..255>          (rows of this type)
[morph]       same keys                    (missing section → copy of [params])
[sequence]    run_mode=fwd|rev|pip|rnd|sel
              length=1..16
              step_scale=1/64|1/32t|1/32|1/16t|1/16|1/8t|1/16.|1/8|1/4t|1/8.|1/4|1/2|1bar|2bar
              lane.<key>=0x<mask>,v0,…,v15 (lane.effect_morph for lane 0)
```

The `step_scale` tokens are the `.fx` file's own spelling
(`storage_effectStepScaleTokens`, index-aligned with StepScale). They differ
from the on-screen long names (`1/32T`, `1 bar`).

- Unknown keys are skipped; missing keys take the defaults.
- An unknown type token fails the load.
- `format=…/version=1/placeholder=1` (legacy) loads as `off`.
- A missing file is valid: `off`, with a blank name.
- The file carries no self-name.

### 12.2 Names

- A Scene folder holds exactly one `<name>.fx` (8-character stem), built by
  `storage_makeSavedEffectDisplayFilename()` through the Instrument name path.
- A blank name writes `' .fx'`.
- Load takes the first `*.fx`.
- **HCNAMES** rows 145..160 (one per Scene) are `name<TAB>source[<TAB>R]`,
  with the usual source tokens.
- The Effect name is independent of the Scene directory name.
- **Loads:**
  - Scene Load and selected Bank children replace the record and
    `effect_morph_amount` all-or-nothing;
  - Kit/Instrument Load never touch them;
  - the library `/Effect/` browser is deferred.

### 12.3 AutoSave (HCPR v3)

The Effect region is 512 B per Scene, at Scene offset 128:

| Relative | Bytes | Field |
|---|---|---|
| 0..2 | 3 | type token (live, projected from the registry) |
| 3..10 | 8 | name: a baseline mirror of HCNAMES row 145+Scene, not a live dirty cell |
| 11..13 | 3 | run mode, length, step scale |
| 14..77 | 64 | normal |
| 78..141 | 64 | Morph |
| 142..429 | 288 | 16 × (mask lo, mask hi, 16 values) |
| 430..431 | 2 | Effect source (HCNAMES row 145+Scene) |
| 432..511 | 80 | reserved |

- There are 419 live cells, starting at relative offset 11.
- `effect_morph_amount` is Scene parameter 40.
- **Boot:** Case 1 applies the region with `autosave_applyEffectPayload()`
  (an unknown token → `off`). Case 2 narrow-loads the named `.fx`.

---

## 13. Known differences from the plan (as built)

1. A same-type Scene switch does not refresh the FxBuffer handoff (plan §12.6
   says it does). There is no effect while no type uses the arena. Add a
   refresh-without-`init` when the first buffer type lands.
2. There is no immediate `fx` LFO rebind on a `typ` change (plan §7.2 item 7).
   It is handled at runtime and at the next rebind (§11.3).
3. `seq_effect_automation_dirty` (plan §10.1) is `effects_automation.active`.
4. Resolution rescans every block instead of using dirty-cell notification
   (plan §9 "Apply").

---

## 14. Tutorial: adding an Effect type

This checklist uses `flt` as the worked example.

1. **Folder** `Core/DSP/Effects/<Type>/` containing:
   - `<Type>Parameters.c/.h`: tokens, the descriptor table, the
     `<TYPE>_PARAM_COUNT` enum, and a `_Static_assert` that the table size
     equals the count;
   - `<Type>Effect.c/.h`: the runtime struct and the `effect_type_ops_t`.
2. **Descriptors.**
   - Start with `EFFECT_COMMON_ROWS(out, vol, pan flags)`, then
     `EFFECT_ROW(...)` / `EFFECT_ROW_MENU(...)` for rows 3..
   - Reuse the voice keys and dtypes when the meaning matches, as `flt` does
     with `filter_freq` etc.
   - Give each row one `default_value` ≤ `max_value`.
   - Mark WIDE8 rows and supply `expand7` if they are AUTOMATABLE.
   - Never make local 63 AUTOMATABLE.
3. **Lanes.** Choose up to 15 sequenceable rows (lane 0 is always Morph).
   `flt`: freq, reso, drive, type, vol, pan.
4. **Ops.**
   - `init`: reset DSP state; clear or adopt arena regions per the handoff.
   - `write_param`: rows 3.. only; do the conversions here (`flt` copies the
     voice-filter shaping).
   - `process`: float in place, respecting `io->channels` / NULL `r`.
   - Set `export_handoff`, `buffer_changed` and `effective_max` if the type
     uses the arena. Set `buffer_min_bytes`/`buffer_pref_bytes`.
5. **Registry.** In `EffectsManager.c`:
   - add the type to the `effects_runtime_t` union;
   - add the registry row (tokens, `io_flags`, descriptors, lanes,
     `select_layout`/`ops`/`ui`, `sizeof(runtime)`, buffer bytes);
   - bump `EFFECT_TYPE_COUNT` and add `EFFECT_TYPE_<NAME>` in
     `EffectsManager.h`;
   - add a `_Static_assert(sizeof(<Type>Runtime) <= sizeof(effects_runtime_t))`.
   - The union lives in DTCM `.dtcmz`: a larger runtime shrinks the arena
     (the linker `ASSERT` guards 120 KiB).
6. **Build.**
   - `<Type>Effect.c` goes into `DSP_SRCS` (fast-math); `<Type>Parameters.c`
     into the normal sources; add `-ICore/DSP/Effects/<Type>`.
   - Run `link_budget.py` and record the flash, DTCM and FXBUF sizes in
     `SRAM_MANIFEST.md` (RAM approval policy in `MEMORY.md`).
7. **Verify.**
   - Build with `DEV_MODE_DIAGNOSTIC=1` and check that the registry
     self-check (`FxBf` screen) is 0.
   - Select the type from `typ` and check the page layout, `.fx` round trip,
     AutoSave restore, automation/LFO on each flagged row, and the CPU widget.

No other file needs changing: storage, AutoSave, Menu, automation and LFO are
all registry-driven.

---

## 15. Pitfalls

- **Foreground only.** Nothing in EffectsManager or FxBuffer may be called
  from an ISR. TIM3 only publishes latches and markers.
- **Clear unless you adopt.** Arena contents are undefined at boot and are
  never cleared by the system.
- **Local indices are type-relative.** Pattern entries, LFO tokens and masked
  fan-out all rely on "same type ⇒ same row". The layout gate protects masks;
  runtime paths re-check the type on every apply.
- **Index 63 / WIDE8:** see §4.1.
- **`INITCM_EFFECT` / `INITCM_EFFECT_NOINLINE`** (config/DSP placement macros)
  mean "place hot filter/distortion DSP in ITCM". They are unrelated to these
  Effects.
- **Flash is tight.** Measure every addition with `link_budget.py`.
  `S073_FLASH_EXPANSION.md` covers growth beyond 480 KiB.
- **Bus headroom.** The bus is `sample_mx_t` with saturation only at the
  int32 limit. Types must process in float and must not truncate to int16
  (this is why `SVF_calcBlockZDFFloat()` exists).
~~~

---

## 4. Specification updates

### 4.1 `AUTOSAVE.md`

1. **Lines 94–96.** Replace the first bullet with:

   > "Root Effect library promotion (the `/Effect/` browser and Effect
   > Load/Save item) remains deferred. Scene/Bank Effect name persistence,
   > the 512-byte Effect payload projection, the narrow Effect boot reader
   > (ST6), and the Effect page, FX sequencer, automation and fan-out edits
   > (ST7–ST10) are implemented. Every Effect edit is marked through
   > SceneData."

2. **Effect region table (~199–207).** Replace rows `3..10` and `430..511`
   with:

   | Relative offset | Bytes | Meaning |
   | ---: | ---: | --- |
   | 3..10 | 8 | Effect name; baseline mirror of HCNAMES row `145 + scene`, not a live dirty cell |
   | … | | (rows 11..429 unchanged) |
   | 430..431 | 2 | Effect source, projected from HCNAMES row `145 + scene` |
   | 432..511 | 80 | Reserved |

   Delete the now-duplicated sentence "The Effect source is projected at
   relative bytes 430..431 …" in the following paragraph, or keep it as a
   cross-reference.

3. **Line 399.** Replace "future Effect marker functions only after Effect
   ownership exists." with:

   > "`autosave_markEffectParameterDirty()` / `autosave_markEffectDirty()`
   > for one Effect cell or the whole Effect region (SceneData Effect setters
   > and whole-record commits)."

4. **Lines 830–831.** Replace "Effect registry, runtime, and boot-reader
   support remains a feature extension, not a scalar addition." with:

   > "Adding an Effect **type** needs no AutoSave change: the region is
   > registry-projected (see `EFFECTS_BUS_REFERENCE.md` §12.3). A new Effect
   > **region field** is a format change and follows the steps above."

5. **Lines 845–848.** Replace "ST6's Effect registry projection, … the root
   Effect browser and full FX UI are intentionally excluded." with:

   > "The S072 Effect projection, Scene Morph byte, `.fx` load/save and Effect
   > boot reader are implemented; their card gates are part of the Phase 5
   > acceptance checklist (`072_SESSION_HANDOFF_LOG.md`). The root Effect
   > browser remains deferred."

### 4.2 `FILESYSTEM_SPEC.md`

1. **Lines 263–264.** Replace with:

   > "New Scene modulation target IDs are runtime/menu IDs. Scene folders
   > persist Scene mix/routing settings in `sceneset.scg` and the
   > Scene-owned Effect in one `<name>.fx` child (v2, Session 072)."

2. **Lines 1427–1430 (current bounds).** Replace the last two bullets with:

   > "- Scene modulation IDs occupy block 6 from `INSTRUMENT_VOICE_ID_COUNT`
   >   (`384`): `384..389` `1vm..6vm`, `390` `srt`, `392..397` `1ou..6ou`,
   >   `398..403` `1fx..6fx`, `404` `fxm` (Effect Morph).
   > - Block 7 `448..510` addresses Effect-local parameters `0..62` of the
   >   Scene's Effect type; `511` is the Pattern off sentinel. See
   >   `EFFECTS_BUS_REFERENCE.md` §11."

3. **Near line 980 (`.fx` load rules).** Add one sentence:

   > "The as-built `.fx` grammar and naming are summarized in
   > `EFFECTS_BUS_REFERENCE.md` §12; this section remains the authoritative
   > storage contract."

### 4.3 `MODULE_INTERCHANGE_SPEC.md`

Also update the "through Session 070" note in `MEMORY.md`'s spec index
(§7.1).

1. **BankData table (after the `bank_setSceneMaskVoiceEditForScene` row).**
   Add:

   | API | Use | Usual callers / clients |
   |---|---|---|
   | `bank_toggleSceneMaskVoiceEdit(scene)` | Toggle one Scene in the active mask. Menu applies the layout gate before turning a bit on; the active bit cannot be removed. | Menu VOICE-held SEQ |
   | `bank_revalidateVoiceEditMasks()` | Drop mask members whose Effect/Instrument layout no longer matches their owner; changed entries mark AutoSave (S072 Step 10). | Menu load-completion funnels, `main.c` boot, `effects_changeType()` |

2. **SceneData section (198–227).**
   - Replace the second paragraph ("The Session 072 Effect record remains …
     Step 4 adds …") with:

     > "The Effect record's meaning is supplied by the EffectsManager
     > registry; its ordered AutoSave projection is defined by `Autosave.h`,
     > not by the C-struct layout."

   - Clients column updates:

     | Row | Clients |
     |---|---|
     | `scene_effectRecordDefaults` | Scene initialization, `.fx` parser, AutoSave reader |
     | `scene_effectRecordForWholeCommit` | EffectsManager type change |
     | normal/morph setters | EffectsManager edit API (fan-out workers) |
     | seq setters | EffectsManager edit API |
     | lane setters | EffectsManager lock editor |
     | `scene_setEffectMorphAmount` | EffectsManager, `preset_morphScene()` (PERF `mrp`), AutoSave |

   - Add a row:

     | API | Use | Usual callers / clients |
     |---|---|---|
     | `scene_editLayoutMatches(a, b)` | Same Effect type and six Instrument types; read-only. | Menu mask gate, BankData re-validation |

   - Replace the last paragraph with:

     > "The 420-byte record and the 16-record SRAM1 allocation are
     > firmware-lifetime SceneData storage. EffectsManager owns 84 B of
     > resolution state and 184 B of overlay/LFO state in SRAM1, plus the
     > 76-byte DTCM type runtime; the mixer owns the FX bus."

3. **menuEffects purpose paragraph (531–541).** Replace "All retained UI
   writes use the EffectsManager edit API, so Step 10 can add edit-mask
   fan-out without changing callers. Step 7 writes the active Scene only."
   with:

   > "All retained UI writes use the EffectsManager edit API, which fans them
   > out through the active VOICE edit mask (Step 10)."

4. **Sequencer table.**
   - Replace the `seq_drainPendingAutomation` row's Use text with:

     > "Foreground drain. It first takes the Effect reset latch (clearing
     > Effect overlays and `fxm`). Voice targets write
     > `instrumentManager_writeRuntime()` and set `seq_automation_dirty[]`.
     > Scene targets (384..404) go through `seq_applySceneAutomation()`
     > (`fxm` → `effects_setMorphAutomation()`). Effect targets (448..510) go
     > to `effects_applyAutomation()`. Effect step markers (identity bit 11)
     > open `effects_automationStepBegin()` groups, and the pass ends with
     > `effects_automationStepFlush()`."

   - Add rows:

     | API | Use | Usual callers / clients |
     |---|---|---|
     | `seq_fxTakeEvent()` | Take the newest FX RESET/STEP latch byte. | `effects_service()` |
     | `seq_setEffectAutomationTracks(mask)` | Publish the tracks owning Effect overlays; TIM3 queues step markers only for them. | EffectsManager |

   - In the StepScale paragraph, replace the last sentence ("`effects_seqSelect()`
     is … until the next boundary or selection.") with:

     > "`effects_seqSelect()` sets the `sel` cursor, which always applies and
     > survives RESET and Scene switches. Scene activation invalidates only
     > the clock position and held Morph until the next boundary."

5. **mixer purpose (686–696).** Replace "During Step 4 it also calls … Step 5
   taps …" with:

   > "Each block it calls `effects_service()`, taps each decimated voice
   > before volume, applies the PRE/POST/FX fader mode, sums the send into
   > the mono/stereo FX bus, calls `effects_process()`, and returns the
   > processed block through common level/pan and jack-resolved routing. See
   > `EFFECTS_BUS_REFERENCE.md` §3."

   In the `effects_process()` row, change "Step 5 mixer" to "mixer".

6. **EffectsManager section (745–772).**
   - Purpose: replace "Step 4 deliberately rescans …" with "It rescans the
     active descriptor table once per render block (no dirty notifications)."
   - Change the Affiliates line to "SceneData, BankData, FxBuffer, Preset,
     AutoSave, mixer, Sequencer, InstrumentManager, menuEffects/Menu, and the
     per-type implementations."
   - Replace the table with:

     | API / data | Use | Usual callers / clients |
     |---|---|---|
     | `effects_registryEntry()` / `effects_registryCount()` / `effects_typeToken()` / `effects_typeFromToken()` | Append-only type registry and persisted tokens; unknown → rejected (runtime falls back to `off`). | AutoSave, storageTypes, menuEffects |
     | `effects_descriptor()` / `effects_descriptorByKey()` / `effects_paramMorphable()` / `effects_paramAutomatable()` / `effects_paramModulatable()` | Descriptor lookup and capability rules (local 63, WIDE8 + `expand7`, modulation domain). | storage, Menu, automation/LFO |
     | `effects_recordDefaultsForType()` / `effects_laneFileKey()` / `effects_laneByFileKey()` | Unowned default record and lane ↔ `.fx` key mapping. | `.fx` parser, AutoSave reader, boot loader |
     | `effects_getParameter()` | Read a normal/Morph endpoint. | menuEffects, step-automation current value |
     | `effects_setParameter()` / `effects_setSeqRunMode()` / `effects_setSeqLength()` / `effects_setSeqStepScale()` / `effects_setMorphAmount()` / `effects_setSeqLaneLock()` / `effects_changeType()` | Retained edit API. Each fans out through the active VOICE edit mask (same-type guard for rows/locks/sequence; `mrp` and `typ` reach all masked Scenes); `changeType` then re-validates masks. Returns nonzero if any reached Scene changed. | menuEffects, type UI hooks, DEV hook |
     | `effects_seqActiveStep()` / `effects_seqSelectedStep()` / `effects_seqSelect()` / `effects_seqSerial()` / `effects_laneOfParam()` / `effects_getLaneLock()` | FX position/selection and lock inspection. | menuEffects |
     | `effects_targetValid()` / `effects_targetDescriptor()` / `effects_stepTarget()` | Validate/describe/walk block-7 targets for a Scene's type and use. | InstrumentManager, Menu |
     | `effects_automationReset()` / `effects_automationStepBegin()` / `effects_applyAutomation()` / `effects_automationStepFlush()` / `effects_setMorphAutomation()` | Pattern overlay lifecycle and the `fxm` override (third restore rule). | Sequencer drain, `seq_applySceneAutomation()` |
     | `effects_setLfoContribution()` / `effects_clearLfoSource()` | Base-independent LFO entries per source/pair. | InstrumentManager |
     | `effects_init()` / `effects_activateScene()` | Boot init (share callback); Scene activation (type switch through the handoff, clears FX position/held Morph/overlays). | `main.c`, Preset |
     | `effects_service()` / `effects_process()` / `effects_commonRuntime()` / `effects_activeType()` / `effects_activeIoFlags()` | Per-block resolution, process and return settings. | mixer |
     | `effects_registryCheckResult()` | DEV registry self-check result. | diagnostic boot screen |

7. **FxBuffer.** In the `fxbuf_voiceAcquire` row, keep "Phase 7 voice buffer
   owners". In the `fxbuf_handoff…` row, append:

   > "A same-type Scene switch does not currently refresh the record
   > (`EFFECTS_BUS_REFERENCE.md` §13)."

8. **InstrumentManager.**
   - In the paragraph "LFO selection supports self, voices, and the Scene
     namespace", append ", and the Effect namespace (`fx`, value 8)".
   - Add rows:

     | API / data | Use | Usual callers / clients |
     |---|---|---|
     | `INSTRUMENT_TARGET_VOICE_EFFECT` / `INSTRUMENT_TARGET_VOICE_NAMESPACE_LAST` | LFO namespace byte 8 (`fx`); the upper clamp for every picker/parser. | Menu, storageTypes |
     | `instrumentManager_targetValid()` (block 7) | AUTOMATION validation of Effect IDs via `effects_targetValid()`; MODULATION stays voice-only. | PatternData writer, Menu |

   - Extend the `updateLfoAdapters` row with:

     > "; Effect parameter and `fxm` destinations are encoded to
     > direction/depth by the shared `instrumentManager_lfoDirectionDepth()`
     > and stored in EffectsManager."

9. **SceneModTargets (841–881).**
   - Affiliates: replace "future FX modules" with "EffectsManager (`fxm`)".
   - Replace the `fxm` bullet with:

     > "Effect Morph `fxm` (ID 404; LFO + automation, no velocity) —
     > EffectsManager Morph-base overlay (Session 072 Step 9)"

   - Replace "The reserved `fxm` descriptor has no automation, velocity, or
     LFO use flags." with:

     > "`fxm` carries `USE_LFO | USE_AUTOMATION`; its Pattern value expands
     > like voice Morph and follows the Scene reset rule."

10. **main.c section.** Add a row or sentence:

    > "End-of-boot `bank_revalidateVoiceEditMasks()` precedes
    > `preset_startDrumsetApply()`; the `DEV_EFFECT_FORCE_TYPE` hook
    > (diagnostic builds) follows it."

11. **Title/scope line 4.** Extend "…Session 072 Step 6 …" to "…through
    Session 072 Step 11 (Effects Phase 5) …".

### 4.4 `BANK_PRESET_ARCHITECTURE.md`

1. **Target table, row 404.** Replace it with:

   | ID Range | Short Label | Per-Voice | Max | Apply Path | Status |
   |---|---|---|---|---|---|
   | 404 | fxm | No (Scene) | 127 stored / 255 expanded | `effects_setMorphAutomation()` overlay; LFO via EffectsManager | Live (Session 072 Step 9) |

2. **Runtime-overlay section** (the Morph/Audio Out/FX Send paragraphs,
   ~363–380). Add:

   > "**Effect parameters and `fxm`:** EffectsManager owns these overlays
   > (`effects_automation`). Parameter overlays end when the writing track's
   > automation ends (Effect step markers), and all of them clear on the
   > common reset path and at Scene activation. `fxm` is the first Effect
   > Morph base source. See `EFFECTS_BUS_REFERENCE.md` §6 and §11."

3. Verify that the Step 10 fan-out/gate text (items 8–9 and the mask
   section) is present. It is; no change.

### 4.5 `DEV_MODES.md` (lines 87–91)

Append to the `DEV_EFFECT_FORCE_TYPE` paragraph:

> "Since Session 072 Step 10 the forced change behaves exactly like a `typ`
> edit. It fans out through the active VOICE edit mask (self-only at boot
> unless restored), clears the FX sequence of every changed Scene, and marks
> AutoSave. Keep it at 0 outside deliberate registry tests."

### 4.6 `SRAM_MANIFEST.md`

- Add after the ST10 sentence:

  > "Step 11 is documentation and comment-only; its production link is
  > byte-identical to ST10."

- Add a one-line **Phase 5 summary**, taking its numbers from the manifest's
  own ledger lines:
  - SRAM1: Scene records +6,720 B; HCNAMES growth; Effect state 84 + 184;
    handoff 180; `fxbuf_state` 28; sequencer 3; menuEffects 21;
  - DTCM: runtime 76, bus 256, ramps 32, arena 126,624.
- Point to `EFFECTS_BUS_REFERENCE.md` §7 for arena ownership rules.

### 4.7 `PATTERN_DYNAMIC_STACK.md`

This is **verify only**. Step 9 already added the block-7 domain (line 130),
the Effect drain branch (255–262) and the marker rule (285–290). Check them
against `EFFECTS_BUS_REFERENCE.md` §11.2; there are no edits unless a
mismatch is found.

---

## 5. `EFFECTS_BUS_FEATURE_PLAN.md` closeout

1. **Status (lines 3–8).** Replace with:

   > "Status: **implemented (Session 072, Steps 1–11), revision 5.** This
   > document is the design record: decisions A1–A46, F1–F6 and G1–G7 and
   > their reasons. The as-built reference is
   > `knowledge_files/specification_reference/EFFECTS_BUS_REFERENCE.md`;
   > where they differ, the reference describes the code (its §13 lists the
   > differences). Hardware acceptance is tracked in
   > `knowledge_files/log_archive/072_SESSION_HANDOFF_LOG.md`. Where the
   > session direction differs from `SCOPING_TARGETS.md`, the session
   > direction wins (§2)."

2. **§3 heading.** Append " (Session 071 baseline, historical)". Leave the
   table alone; it is the design-time baseline.

3. **§12.6, "Same-type Scene switch" bullet.** Append:

   > "*(As built, the handoff is refreshed only on a type change; see
   > reference §13.)*"

4. **§15.**
   - In the table, split the row `430 | 82 | reserved` into
     `430 | 2 | Effect source (HCNAMES row 145+Scene)` and
     `432 | 80 | reserved`.
   - Replace the bullet "The three type-token cells are live … Name cells
     remain unavailable until the HCNAMES/storage step. …" with:

     > "The type-token cells are live and projected from the registry token.
     > The eight name cells are a baseline mirror of HCNAMES row 145+Scene
     > (not live dirty cells). An absent or unknown token resolves to
     > `off`."

5. **§16.1 table.** Add a "Measured (ST10)" column, or append the measured
   value in parentheses:
   - item 3: HCNAMES arrays now 1,771 B total (161 rows);
   - item 4b: 180;
   - item 6: 28 (`fxbuf_state`);
   - item 7: 3;
   - item 8: 21;
   - item 9: 76;
   - item 10: 256 (+32 ramp state);
   - item 11: 126,624.

6. **§17.1 row 11, Gate column.** "Docs match the code (ST11 §11)".

7. **§17.2 risk 1.** Append:

   > "*(Measured after Step 10: 8,080 B headroom; see
   > `S073_FLASH_EXPANSION.md`.)*"

8. **§18.4.** Replace "None for the general plan. …" with:

   > "Deferred and follow-up items, carried to later sessions:
   > - root `/Effect/` browser and Effect Load/Save item (A35, §14.5);
   > - a buffer-using test type (A8), together with the same-type handoff
   >   refresh (reference §13 item 1);
   > - FX lock removal (A15) and Scene copy/clear of the Effect (§7.3), with
   >   the copy pass;
   > - MIDI mapping (A20) and live record (A22);
   > - track step-scale playback (A10);
   > - the blank-stem naming defect (G5), logged in `SCOPING_TARGETS.md`;
   > - the Phase 5 hardware acceptance checklist (handoff log)."

---

## 6. `SCOPING_TARGETS.md`

1. **Overview paragraph (25–31).** Replace "…the Scene Effect file remains a
   placeholder." and "Phase 5 now establishes Effect files, the audio bus
   and shared buffer, its fixed sequencer, and the related Scene/Bank fixes."
   with:

   > "…Phase 5 (Session 072) then implemented Scene-owned Effects: `.fx` v2
   > files, the FX bus and fader modes, the shared DTCM arena, the FX
   > sequencer, Effect automation/LFO, and edit-mask fan-out. See
   > `EFFECTS_BUS_FEATURE_PLAN.md` and `EFFECTS_BUS_REFERENCE.md`."

2. **Line 43 (phase list).** Append " — **implemented (S072)**" to the Phase 5
   item.

3. **Lines 748–749 and 759–760 (§3.x storage notes).** Replace with:

   > "- The Scene folder holds one Scene-owned `<name>.fx` Effect (v2; a
   >   legacy `effects.fx` placeholder loads as `off`), implemented in
   >   Session 072.
   > - Scene/Bank Effect load/save is implemented; the standalone `/Effect/`
   >   library browser and Effect Load/Save item remain deferred (A35)."

4. **Lines 969–970 (the "Effect placeholders" bullet).** Replace with:

   > "- **Effect placeholders:** resolved in Session 072. `placeholder=1`
   >   files and missing `.fx` load as `off` (FILESYSTEM_SPEC)."

5. **§4.4 (1078–1088).** Replace the paragraph's last three sentences with:

   > "Phase 5 assigned block 7 (448..510) to Effect-local parameters 0..62,
   > with 511 kept as the off sentinel, and 404 to `fxm`. The FX sequence
   > uses 16 lanes per step (lane 0 = Effect Morph) named by each type's
   > registry, not 24 free values. See `EFFECTS_BUS_REFERENCE.md` §10–§11."

6. **Phase 5 section (1175–1352).** Insert at the top, directly under the
   heading:

   > "**Status: implemented in Session 072.** The plan
   > (`EFFECTS_BUS_FEATURE_PLAN.md`) superseded several details below (§2 of
   > the plan):
   > - a 288-byte sequence of 16 lanes instead of 16 × 24 values;
   > - `fwd/rev/pip/rnd/sel` run modes;
   > - 4,416-byte buffer units;
   > - the first type is a stereo filter without a buffer.
   >
   > The as-built behavior is in `EFFECTS_BUS_REFERENCE.md`. **Carried
   > forward:**
   > - `/Effect/` browser and Effect Load/Save item (A35);
   > - buffer-using template type (A8);
   > - Scene copy/clear of the Effect and FX lock removal (copy pass);
   > - MIDI mapping of Effect parameters (A20);
   > - live record of FX moves (A22);
   > - track step-scale/shuffle playback (A10).
   >
   > The 480 KiB flash finding is in `S072_ST1_IMPLEMENTATION.md` §21; the
   > growth path is planned in `S073_FLASH_EXPANSION.md`."

   Leave the historical subsections in place, as other completed phases do.

7. **Open Engineering Questions (1331–1344).** Prefix each question with its
   answer pointer:

   | Question | Answer |
   |---|---|
   | resident/24 values | all 16 resident; 16 registry lanes (plan §7.1) |
   | migration | no migration (A38); placeholders → `off` |
   | priority | Pattern > FX lock > menu; LFO on top (plan §9) |
   | buffer size | 126,624 B measured; handoff (plan §12) |
   | flash | S073 plan |

8. **New entry: G5 blank-stem defect.** Add under Phase 6 known debt, or the
   nearest "known issues" list in SCOPING_TARGETS:

   > "- **Blank/space-only name stems (logged per S072 G5).** Trailing-space
   >   trimming at `filesystem.c` ~2280, ~10054, ~10371 and ~16516 may
   >   mishandle blank or space-only Instrument stems (`' .drm'`). Effects
   >   inherit the same behavior through the shared name path (`' .fx'`).
   >   Investigate as a general naming bugfix; it was out of Phase 5 scope."

   The line numbers are the plan's (§14.2); re-verify them at fix time.

9. **Phase 7 (1473–1475 and 1487).** Replace "8,820-byte units" with
   "4,416-byte (50 ms) units" in both places. Replace "It requests up to two
   8,820-byte units" with "It requests up to two 4,416-byte units". In 1460,
   replace "template type" with "stereo-filter type (no buffer use yet)".

---

## 7. `MEMORY.md` and `README.md`

### 7.1 `MEMORY.md`

1. **Volatile Notes.**
   - Replace the S072 Step 4 line's "76-byte runtime state" with "84-byte
     runtime state (was 76 before Step 8)".
   - Replace the Step 7 line's "edits affect only the active Scene until
     Step 10" with "edits fan out through the active edit mask (Step 10)".
2. **Add after the Step 10 line:**

   > "- S072 Step 11 (Phase 5 closeout): `EFFECTS_BUS_REFERENCE.md` is the
   >   as-built reference; specs, plan and SCOPING are updated; the Session
   >   072 handoff log holds the Phase 5 hardware acceptance checklist.
   >   Flash headroom is 8,080 B (see `S073_FLASH_EXPANSION.md`). The
   >   `S072_ST*_IMPLEMENTATION.md` documents may be deleted once that
   >   checklist passes."

3. **Repository Layout tree.**
   - Under `├── Menu/`, add:

     ```
     │   ├── menuEffects.c/h          ← SHIFT+PERF Effect page: layout, typ transaction, SEQ lock editing/LEDs
     ```

   - Under `├── Sequencer/`, add:

     ```
     │   ├── StepScale.c/h            ← shared 14-entry 96-PPQ track/FX step-scale table
     ```

   - Change the `SceneModTargets.c/h` comment to "Scene-level target
     namespace: 1vm..6vm, srt, 1ou..6ou, 1fx..6fx, fxm".
   - Under `├── DSP/`, add before `└── Instruments/`:

     ```
     │   ├── Effects/
     │   │   ├── EffectTypes.h         ← retained Effect record contract, block-7 IDs
     │   │   ├── EffectsManager.c/h    ← registry, resolution, edit API/fan-out, automation/LFO overlays
     │   │   ├── EffectParamRows.h     ← common/type descriptor row macros
     │   │   ├── FxBuffer.c/h          ← DTCM arena, voice units, Effect share, handoff
     │   │   └── StereoFilter/         ← `flt` descriptors, runtime and ops
     ```

     and change `└── Instruments/` to `├── Instruments/` if the tree ordering
     requires it.

4. **Specification-reference index.**
   - Change "These are the eight authoritative/reference documents" to the
     correct count.
   - Add `PATTERN_DYNAMIC_STACK.md` if it is missing.
   - Add a row:

     | File | What it contains | Use it when |
     |---|---|---|
     | `EFFECTS_BUS_REFERENCE.md` | As-built Phase 5 Effect system: module map, signal flow and fader modes, type contract, resolution order, arena and handoff, Effect page, fan-out, FX sequencer, IDs/automation/LFO, `.fx`/HCNAMES/AutoSave, known plan differences, add-a-type tutorial, pitfalls. | Adding an Effect type or changing anything in the Effect path. |

   - Update the `MODULE_INTERCHANGE_SPEC.md` row's "through Session 070" to
     "through Session 072".
   - Update the `FILESYSTEM_SPEC.md` row's "through Session 072 ST6" to
     "through Session 072".

5. **"Where to look for things".** Add a row: "Effect system (types, bus,
   sequencer, automation)? | `…/EFFECTS_BUS_REFERENCE.md`".

### 7.2 `README.md`

README's tree is older than MEMORY's in several places, so this is a minimal
Phase 5 addition only:

- In the `Menu/` list, add the `menuEffects.c/h` line.
- Add a `DSP/Effects/` block, the same as §7.1 item 3.
- In "Where to look for things", add the Effect-system row.

---

## 8. Session 072 handoff

### 8.1 New `knowledge_files/log_archive/072_SESSION_HANDOFF_LOG.md`

Follow the 071 log's structure. The content comes from the eleven step
documents; write summaries, not copies:

```markdown
# Session 072 Handoff Log

## End of session
DATE: 2026-09-27 .. 2026-09-28 (update if the session continued)
SESSION GOAL: Phase 5 — Effects bus (EFFECTS_BUS_FEATURE_PLAN.md §17.1 Steps 1–11)
COMPLETED: Steps 1–11 in source; Step 8 hardware points confirmed with `flt`;
           Step 9/10 hardware matrices pending (see Acceptance checklist)
VERIFIED ON HARDWARE: partial (Step 2 and Step 5 user reports; Step 8 test
           points with `flt`); see checklist
CHANGES THIS SESSION: see "Steps" and "Files changed"
KNOWN ISSUES INTRODUCED: see "Known differences and follow-ups"
KNOWN ISSUES RESOLVED: Snare/Cymbal/HiHat pre-distortion volume (Step 2 D1);
           FX_SEND now audible; `sel` held-Morph rule (ST9 F1)
NEXT SESSION RECOMMENDED GOAL: run the Phase 5 acceptance checklist, then
           S073 (flash expansion; CPU reduction)
BLOCKERS: flash headroom 8,080 B
CRITICAL REMINDERS:
- Effect writes only through the EffectsManager edit API (fan-out + AutoSave)
- Nothing in EffectsManager/FxBuffer from ISR context
- Arena: clear unless you adopt
- Local Effect indices are type-relative; the layout gate protects masks

## Build metrics
| Point | text | data | bss | flash payload | headroom |
| S071 close | 456,748 | 416 | 291,900 | ~457,180 | 34,356 (ST1 measure) |
| ST1..ST10 | (one row per step from each ST §11/§12) |
| ST10/ST11 | 483,024 | 416 | 426,336 | 483,440 | 8,080 |

## Steps (one short section each, with key decisions and review findings)
ST1 sine table/arena/FxBuffer · ST2 volume relocation · ST3 data model ·
ST4 EffectsManager/flt · ST5 FX bus · ST6 storage/HCNAMES/AutoSave ·
ST7 Effect page · ST8 FX sequencer/StepScale · ST9 automation/LFO ·
ST10 gate/fan-out · ST11 reference/spec closeout

## Known differences and follow-ups
(EFFECTS_BUS_REFERENCE.md §13 items 1–4; deferred list from plan §18.4)

## Phase 5 acceptance checklist
(section 9 of S072_ST11_IMPLEMENTATION.md, with PASS/FAIL columns)

## Files changed (complete list)
(`git diff --stat 8538bbf^..HEAD` at close, grouped by module; include new
files: Core/DSP/Effects/*, Core/Menu/menuEffects.*, Core/Sequencer/StepScale.*,
EFFECTS_BUS_REFERENCE.md)
```

The build-metrics rows come from each step document's measured link:

| Step | Link (text / flash payload / headroom) | Source |
|---|---|---|
| ST4 | text 463,552 | SRAM_MANIFEST |
| ST5 | text 465,352 | SRAM_MANIFEST |
| ST6 | text 470,208 / payload 470,616 | SRAM_MANIFEST |
| ST7 | text 475,592 / payload 476,008 | SRAM_MANIFEST |
| ST8 | text 478,720 / 479,136 / 12,384 | ST8 §12 |
| ST9 | text 482,632 / 483,048 / 8,472 | ST9 §12 |
| ST10 | text 483,024 / 483,440 / 8,080 | ST10 §12 |

Take ST1–ST3 from their documents' §11 notes.

### 8.2 `knowledge_files/log_archive/000_SESSION_INDEX.md`

Append the entry in the existing style:

```markdown
### 072 — Phase 5 Effects Bus: registry, FX bus, storage, Effect page, FX sequencer, automation/LFO, fan-out (2026-09-27/28)

Session 072 implemented `EFFECTS_BUS_FEATURE_PLAN.md` Steps 1–11 on
`dev-ph5-effects` (text 456,748 → 483,024; flash headroom 34,356 → 8,080 B).
[One paragraph per step group: arena + sine move + volume relocation (1–2);
data model, EffectsManager, `flt`, FX bus (3–5); `.fx` v2, HCNAMES 161,
AutoSave v3 (6); Effect page, FX sequencer, StepScale (7–8); `fx` automation,
end markers, `fxm`, LFO `fx` (9); layout gate and fan-out (10); reference and
closeout (11).] Hardware: Step 8 points confirmed; Steps 9–10 matrices pending.

- **Find here**: [072_SESSION_HANDOFF_LOG.md](072_SESSION_HANDOFF_LOG.md),
  `EFFECTS_BUS_REFERENCE.md`, `EFFECTS_BUS_FEATURE_PLAN.md`, `AUTOSAVE.md`,
  `FILESYSTEM_SPEC.md`, `MODULE_INTERCHANGE_SPEC.md`, `SRAM_MANIFEST.md`.
```

---

## 9. Phase 5 hardware acceptance checklist (goes into the handoff log)

Each row points at the step document that holds the full procedure. Rows
already confirmed on hardware are marked.

| # | Area | Procedure | Status |
|---|---|---|---|
| A1 | Sine-in-flash stress, arena size | ST1 gates | per ST1 notes |
| A2 | Volume relocation A/B on every instrument | ST2 gates | confirmed (user report 2026-09-27, ST2 notes) |
| A3 | FX bus, three fader modes, jack fallback, CPU | ST5 gates | per ST5 notes |
| A4 | `.fx` fixtures F1–F5 (legacy, v2, missing, malformed, partial Bank), Scene/Bank round trip, reboot restore Case 1/2 | ST6 §11 | pending |
| A5 | ST6 prerequisite fixes (blank-name save `' .fx'`, Kit Save leaves the Effect row) | ST8 §12.5 / ST9 | pending |
| A6 | Effect page walk-through (layout, `typ`, SHIFT Morph, TRACK/SHIFT+TRACK, SELECT cycling) | ST7 §11 | pending |
| A7 | FX sequencer matrix (modes × length × scale, `sel`, Morph lane, A17 alignment, persistence, CPU) | ST8 §10 | 5 points confirmed; remainder pending |
| A8 | Automation/LFO matrix (`fx` category, end rule, fallback, two tracks, probability hold, reset, `fxm`, LFO `fx`/`fxm`, "together", rebind, Kit round trip) | ST9 §10 | pending |
| A9 | Gate and fan-out matrix (mismatch rejection, Effect and type fan-out, directional re-validation, load/boot repair, VOICE regression) | ST10 §9 | pending |
| A10 | Step 9/10 prerequisite checks (`sel` Morph hold, `FilterFrequncy` label, mrp held display) | ST10 §9 gate 12 | pending |
| A11 | AutoSave OFF/ON and power-interrupt across an Effect edit | AUTOSAVE.md validation list | pending |
| A12 | CPU widget: `flt` + 6 sends + 2 LFO → `fx` + FX sequencer at `/64` | ST8/ST9 CPU gates | pending |

Record PASS/FAIL, date and firmware `text` for each row in the handoff log.

---

## 10. Notes

- After §2 the source has no remaining Phase 5 forward references. §11 gate 3
  is the grep that proves it.
- Nothing here changes behavior. If the build differs from ST10 in
  `text`/`data`/`bss`, a non-comment token was touched: find and revert it.

---

## 11. Verification gates

1. **Build.** `make clean && make all` succeeds with no new warnings.
   `text=483,024`, `data=416` and `bss=426,336` are **identical** to ST10
   (comment-only change set). `link_budget.py` shows the same flash
   (483,440), DTCM and FXBUF.
2. **Diff hygiene.** `git diff --check` is clean. `git diff -U0 -- '*.c'
   '*.h'` shows only comment lines. Review it by eye, or run a filter that
   strips `/* */` and `//` hunks and confirm nothing remains.
3. **No stale forward references remain:**

   ```
   grep -rn -i "later fx\|later step\|future .fx\|future mixer\|future effect pages\|step 5 calls\|until step 10\|step 10 adds\|reserved until its apply\|without an audible\|ahead of the phase 5" Core main.c
   ```

   This returns nothing except the two deliberate exceptions in D5
   (`filesystem.c:2599`, `asyncfatfs.h:275`).
4. **The reference matches the code.** Spot-check each numbered fact against
   the source:
   - §3 gains and flow: `mixer.c` ~500–540, ~770–890;
   - §4.4 codes: `effects_registrySelfCheck()`;
   - §5.2 order: `effects_changeTypeScene()` + wrapper;
   - §6 order: `effects_service()`;
   - §7 sizes: `FxBuffer.h` / link budget;
   - §9 sites: grep `bank_revalidateVoiceEditMasks`;
   - §10 formulas: `seq_fxClockTick()`;
   - §11 bits and flow: `sequencer.c` drain/marker;
   - §12.3 offsets: `Autosave.h` `_Static_assert`s (source at 430).
5. **The specs match the code.**
   - Every API row added in §4.3 exists: `grep -n` each function name in its
     module header.
   - No spec says "future" for an implemented Effect client:

     ```
     grep -n -i "future" MODULE_INTERCHANGE_SPEC.md | grep -i effect
     ```

     This leaves only FxBuffer "Phase 7 voices" and library-browser items.
6. **Cross-links.** Every file named in the reference, the plan status, the
   SCOPING Phase 5 status and the handoff exists at the stated path.
7. **Hardware.** None for this step. The §9 checklist is the Phase 5
   acceptance run.

---

## 12. Implementation notes

*(For the implementer: record the measured link, whether any comment
anchor had moved, and any spec section that needed more than the listed
edits.)*
