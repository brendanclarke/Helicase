# Effects Bus Reference

As-built reference for the Phase 5 Effect system: Session 072 Steps 1–11,
signal-path updates from Session 073, and the Session 074 additions. S074
added the second real type (CrumpBit, the first to use the DTCM arena), the
Effect-page framework extensions it needed, and the automation underlines.
It describes what the firmware does and how to extend it. **Where any plan or
log disagrees, this document describes the code**; §13 lists the known
differences from the plan.

- **Design history:** every Phase 5 decision (A1–A46, F1–F6, G1–G7) was in
  `EFFECTS_BUS_FEATURE_PLAN.md`. That file was deleted from the working tree
  in commit `ca77891`; read it with
  `git show f3a3105:EFFECTS_BUS_FEATURE_PLAN.md`. The S072 and S074 records
  are `072_SESSION_HANDOFF_LOG.md` and `074_SESSION_HANDOFF_LOG.md`.
- **The DSP side** (number formats, mixer and bus arithmetic, each type's
  DSP, the master bus compressor, the output pipeline, costs, and the rules
  for buffer-using types) is in `EFFECTS_MIXER_DSP_REFERENCE.md`, in this same
  folder.
- **New here?** Read §1 (concepts), §3 (one block), §4 (the type contract),
  §8 (the page), then the tutorial (§14). The two registered types are the
  worked examples: `flt` has no arena use and no custom UI; `cbt` uses both.

---

## 1. Concepts

- **One Effect per Scene.** Each of the 16 resident Scenes owns one
  `effect_record_t`. A Kit or Instrument load never changes it. Scene and Bank
  Load/Save carry it.
- **Type.** The registry id is `0` = `off` (default), `1` = `flt` (stereo
  filter) or `2` = `cbt` (CrumpBit: 8-bit bit-crush into an 8-bit tape delay,
  Session 074). Files and AutoSave store the three-character token, never the
  id.
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
  Effect (bottom) and future buffer voices (top). `cbt` uses 70,592 B of it
  for its delay loop; `flt` uses none.
- **Not part of the Effect system:** the master bus compressor (`cmp`, S074)
  is a Scene setting that processes an output pair after the Effect return.
  It has no registry row, send or lanes (`EFFECTS_MIXER_DSP_REFERENCE.md`
  §5A).
- **Edit fan-out.** Effect edits reach every Scene in the active VOICE edit
  mask. Masks only group Scenes with the same Effect and Instrument types.

---

## 2. Module map

| Area | File(s) | Owns |
|---|---|---|
| Data contract | `Core/DSP/Effects/EffectTypes.h` | `effect_record_t` (420 B), step shape, lane/run/length constants, block-7 ID helpers |
| Registry, runtime, resolution | `Core/DSP/Effects/EffectsManager.c/.h` | registry (`off`, `flt`, `cbt`), the runtime union, activation/type change and the handoff export (`effects_exportHandoff()`), the per-block §6 resolution, edit API + fan-out, FX-sequencer consumption, Pattern/LFO overlays, target validation; the hook and layout contract types |
| Row macros | `Core/DSP/Effects/EffectParamRows.h` | `EFFECT_COMMON_ROWS`, `EFFECT_ROW`, `EFFECT_ROW_MENU`, modulation-domain presets |
| Arena | `Core/DSP/Effects/FxBuffer.c/.h`, linker `.dtcm_fxbuf` | arena bounds, voice units, Effect share, handoff record |
| Stereo filter | `Core/DSP/Effects/StereoFilter/StereoFilterParameters.c/.h`, `StereoFilterEffect.c/.h` | descriptors/names, runtime + ops |
| CrumpBit (S074) | `Core/DSP/Effects/CrumpBit/CrumpBitParameters.c/.h`, `CrumpBitEffect.c/.h` | descriptors/names, layout and page hooks (bit row, SELECT/LED, value labels); runtime, ops, Rate/Sync helpers |
| Float SVF | `Core/DSPAudio/ResonantFilter.c` `SVF_calcBlockZDFFloat()` | float-I/O ZDF block for Effects |
| Bus | `Core/DSPAudio/mixer.c` | send taps, fader modes, bus accumulation, process call, return |
| Retained data | `Core/Bank/Scene/SceneData.c/.h` | the record, change-aware setters, `effect_morph_amount`, `scene_editLayoutMatches()` |
| Masks | `Core/Bank/BankData.c/.h` | per-Scene VOICE edit masks, `bank_revalidateVoiceEditMasks()` |
| Effect Morph target | `Core/Bank/Scene/SceneModTargets.c/.h` | `fxm` (ID 404, LFO + automation) |
| Clock and drain | `Core/Sequencer/sequencer.c/.h`, `StepScale.c/.h` | FX step latch, Effect step markers, reset latch, the shared scale table |
| LFO and validation | `Core/DSP/Instruments/InstrumentManager.c/.h` | LFO `fx` namespace (8), Effect LFO adapter, AUTOMATION validation of block 7 |
| UI | `Core/Menu/menuEffects.c/.h` (page, hold editor, type-hook dispatch, row-0 painter, SELECT-LED owner check, home screen); `menu.c` (delegation, the shared automation-presence search and markers, `fx` step-automation category, LFO `fx` display, `menu_effectShowHome()`); `buttonHandler.c`; `ledHandler.c` | the SHIFT+PERF Effect page |
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
      if fx_active and the send ramp is non-zero (this or last block):
         mixer_addVoiceInt16ToOutputAndFx()   one pass (S073):
            dry:  × mix ramp, int16, pan, route (jack fallback) → satAdd32
            send: ramp from mixer_send_last_gain[slot], int16 × 256 →
                  sample_mx_t (no int16 truncation), satAdd32 into the bus
                  stereo-in type: panned voice into L and R
                  mono-in type:   unpanned voice into L
      else:
         mixer_addVoiceInt16ToOutput()        dry only
      mixer_voice_last_gain / mixer_send_last_gain updated every block
  if fx_active:
      bus → float (× 1/8388352), R zeroed for mono-in/stereo-out
      effects_process(io)                       in place; io.share = current share
      return gains:
         stereo-out: balance gL = pan<=63 ? 1 : (127-pan)/64,
                             gR = pan>=63 ? 1 : pan/63
         mono-out:   constant power squareRootLut[127-pan], [pan]
         × level (vol/127); ramped from mixer_fx_return_last_gain[2]
      route(out) with mixer_checkOutJackAvailable() → saturated add
  else (`off`):
      mixer_fx_return_last_gain[0..1] = 0      (S074: the next type fades in from 0)
  busComp_processBlock(output2, output, scene) master bus compressor on St1 or
                                               St2 (not an Effect; no work while
                                               `cmp` is off; S074)
```

`fxSend` is `preset_getEffectiveFxSendAmount(scene, slot)`, which honors the
FX-send step override. The fader never changes the stored send.

### Fader modes (`fader_setting`)

| Mode | Label | Dry mix | Send |
|---|---|---|---|
| 0 | `pre` | vol × fader | send × fader |
| 1 | `pst` | vol × fader | send |
| 2 | `fx` | vol | send × fader |
| 3 | `xfd` (S074) | vol × fader | send × fader′, the mirrored taper `taper(1 − x)` |

- **`xfd`** crossfades the voice between the FX bus (fader at the bottom:
  full send, no dry signal) and its normal output (top: normal dry, no send).
  Moving the fader down raises the send exactly as moving it up does in `fx`.
  Both taps are −16.4 dB at centre (log taper on each side). The mirrored
  gain is `adc_sliderGainMirrored()` (one division); the DSP details are in
  `EFFECTS_MIXER_DSP_REFERENCE.md` §3.2.
- Storage: `fader_setting[slot]` 0..`SCENE_FADER_SETTING_MAX` (3). Firmware
  older than S074 rejects a Scene file containing `3`.
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
| `select_layout` | NULL = default Effect-page layout (§8.1); otherwise an `effect_select_layout_t` (§8.4) |
| `ops` | DSP operations (§4.3); NULL for `off` |
| `ui` | optional page hooks, `effect_ui_hooks_t` (§8.4) |
| `runtime_bytes` | size of the type's runtime struct; must fit `effects_runtime_t` |
| `buffer_min_bytes`, `buffer_pref_bytes` | 0 = no arena use |

The registry is **append-only in token terms**: ids may be reordered in the
future, tokens never change meaning.

### 4.2a Registered types (Session 074)

| Id | Token / abbrev / full | I/O | Rows | Lanes 1..n | Layout / hooks | Runtime | Arena min = pref |
|---:|---|---|---:|---|---|---:|---:|
| 0 | `off` / `Off␣␣` / `Off` | none | 3 | — | default / none | 0 | 0 |
| 1 | `flt` / `StFlt` / `StFilter` | stereo in, stereo out | 7 | freq, reso, drive, type, vol, pan | default / none | 76 B (two filter states) | 0 |
| 2 | `cbt` / `CrmBt` / `CrumpBit` | stereo in, stereo out | 11 | bof, biv, mix, fbk, rte, snc, vol, pan, dpn | custom layout; `select`, `render_leds`, `paint_row0`, `format_value3`, flag `OWNS_SELECT_LEDS` | 56 B | 70,592 B |

`effects_runtime_t` is a union of every runtime struct and is **76 B**
(StereoFilter is the largest). `_Static_assert`s check each member against
the union and pin CrumpBit at 56 B, so only 20 B remain for a larger future
runtime before the union (and the arena beneath it) changes. The CrumpBit
rows, DSP and page are in §8.6 and `EFFECTS_MIXER_DSP_REFERENCE.md` §4.4.

### 4.3 Operations (`effect_type_ops_t`)

| Op | When | Contract |
|---|---|---|
| `init(rt, handoff)` | the type becomes active (Scene activation with a different type, or a `typ` change) | the runtime union is already zeroed; **clear or adopt** arena regions per the handoff (§7.3) |
| `export_handoff(rt, out)` | the type is exited; since S074 also on a same-type Scene switch (a refresh, no `init`) | describe the arena use (`state_flags`, bounds, rate, pointers [12..15]) |
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
  end of DTCM. It measures **126,592 B** at `0x20001180` since S074 (the bus
  compressor's 32 B of DTCM state moved the base; it was 126,624 B at
  `0x20001160`). A linker `ASSERT` keeps it ≥ 122,880 B (120 KiB); the
  margin is 3,712 B.
- It is never zeroed or cleared by the system (A31).
- **Voice units:** 2,208 samples × 2 B = 4,416 B (50.06 ms at 44,108 Hz);
  12 in total, at most 2 per slot. They are allocated from the top. No
  instrument claims them yet; the diagnostic knob
  `DEV_FXBUF_FORCE_VOICE_UNITS` can.
- **The Effect share** is contiguous from the bottom: the arena minus the
  claimed units. With all twelve units claimed it is **73,600 B**. A type
  must work at that minimum.

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
- At boot `state_flags = 0`, meaning nothing is valid. `fxbuf_init()` resets
  the record directly after `fxbuf_clearOwners()`, before any unit is
  claimed. That order was fixed in S074 (S072 debt 1), so diagnostic forced
  units carry valid rates.
- An entering owner must **clear any region it will read unless it
  deliberately adopts** content the handoff marks as valid for it.
- "Clear" does not have to mean a bulk `memset`. CrumpBit keeps a fill
  counter (`valid`) and mutes any read further back than what it has written
  since its loop was seated. That costs one compare per sample instead of
  clearing up to 126 KB in one foreground call (§8.6,
  `EFFECTS_MIXER_DSP_REFERENCE.md` §4.4).
- **Per type:**
  - `flt` uses no arena memory and ignores the handoff.
  - `cbt` never adopts (Q19). It exports 1 channel, 8 bits, 44,108 Hz, its
    write and read positions in pointer slot 12
    (`FXBUF_HANDOFF_EFFECT_POINTER_BASE`), and
    `FXBUF_STATE_EFFECT_WRITTEN` once anything was written.
- **Refresh points:** the handoff is exported when a type is exited (Scene
  activation with a different type, or a `typ` change). Since S074 it is also
  refreshed without `init` on a same-type Scene switch
  (`effects_exportHandoff()`, §16.2).

---

## 8. Effect page (SHIFT+PERF, `EFFECT_PAGE`)

### 8.1 Layout

- **SELECT 1** holds screen 1 `typ out vol pan` and screen 2
  `run len scl mrp`. Repeated SHIFT+PERF toggles between them. SELECT 1 is
  always manager-owned, even with a custom layout.
- **SELECT 2..8** hold the type rows 3..63 in order, 4 per screen, with
  `ceil(N/28)` screens per button.
- Re-pressing the current SELECT cycles its screens.
- The encoder scrolls through every screen; a click opens the full view.
- A type may supply `select_layout` instead (§8.4); CrumpBit does (§8.6).

### 8.2 Editing

- **`typ`** changes only through the encoder: click in, turn to browse, click
  out to commit. The pots are inert on it.
- **SHIFT** shows and edits Morph endpoints; single-valued cells ignore it.
- `scl` labels: `/64 32t /32 16t /16 /8t 16. /8 /4t /8. /4 /2 1br 2br`.

### 8.3 SEQ buttons

- **Hold** one or more steps, then edit a sequenceable cell: the value is
  written and locked on each held step.
  - The shown step is the **last step held** (the most recently pressed SEQ
    button still down). If it is released while others stay down, the
    highest-numbered remaining step is shown. This holds for every cell
    since S074, so one screen never mixes two steps; before S074 it was the
    lowest-numbered held step. It is tracked in spare bits of the existing
    hold byte (`MENU_FX_HOLD_ACTIVE 0x80`, `_LAST_VALID 0x40`,
    `_LAST_MASK 0x0F`): 0 B of RAM.
  - A locked lane shows the held step's value with its last glyph
    underlined.
  - `typ`/`run`/`len`/`scl` and non-sequenceable cells are inert while
    holding.
  - Hold start, held-mask change and release redraw with `menu_repaint()`
    (the action bit `MENU_FX_ACT_HOLD_REPAINT`). This keeps the LCD shadow
    valid so a CGRAM marker can move between the name row and the value row
    without flashing in the wrong row (§8.5).
- **Tap or hold** in `sel` jumps to the step.
- **LEDs:**
  - steps with any lock (within the length) are lit;
  - the FX chase runs while playing (not in `sel`);
  - the `sel` step blinks;
  - the Pattern chase is suppressed on this page.
- Locks are removed by copy/clear (S075, closes A15): SHIFT + copy/clear +
  SEQ clears that step's locks and values at once, `clear fx sequence` clears
  all 16 steps, and a pot clear over an Effect parameter or `mrp` clears its
  lane on every step. All fan out through the edit mask (§9).

### 8.4 TRACK, BAR, SELECT hooks and custom layouts

- **TRACK** mutes; **SHIFT+TRACK** selects the track.
- **SELECT/BAR with SHIFT, and BAR alone,** do nothing unless the type has a
  hook.
- **`effect_ui_hooks_t`** (`EffectsManager.h`; extended in S074). Any member
  may be NULL:

  | Member | Contract |
  |---|---|
  | `select` / `track` / `bar` `(button, shift, pressed)` | Nonzero = handled; zero falls back to the default page. `select` may return `EFFECT_UI_SHOW_HOME` (2): handled, show the layout's home screen. **Any nonzero `select` return abandons an open `typ` browse** without committing it. `EFFECT_UI_HANDLED` is 1. |
  | `render_leds()` | Runs after the page has drawn its own LEDs. With the flag below, it is also the only writer of the SELECT LED row. |
  | `paint_row0(sub_page, screen, row0)` | Writes columns 0..14 of the compact top row for screens flagged in `select_layout->custom_row0`. Column 15 keeps the page's scroll marker. Those screens show no automation name markers. |
  | `format_value3(index, value, out)` | May replace a type row's 3-character value text on the Effect page only: compact cells, the full view and held-step values. Storage and the STEP automation page always show raw values. Nonzero = handled. |
  | `flags` | `EFFECT_UI_FLAG_OWNS_SELECT_LEDS` (`0x01`): the page's active-SELECT LED is suppressed and `menuEffects_renderSelectLeds()` calls `render_leds` instead. |

  Hooks run in the foreground, must not block, must write retained data only
  through the EffectsManager edit API (`menuEffects_editParam()` for
  held-aware writes), and must never touch the filesystem.
- **`effect_select_layout_t`** (NULL = default):
  - `screen_count[b]` and `cells[b][screen][column]`: the screens behind
    SELECT b+1 (up to 4). Each cell holds a descriptor index,
    `EFFECT_LANE_NONE` (empty), or **`EFFECT_LAYOUT_CELL_MORPH` (`0xFD`)**,
    the manager `mrp` cell. Index 0 (SELECT 1) is ignored.
  - `custom_row0[b]`: bit *s* set means screen *s* of SELECT b+1 has a
    type-painted top row.
  - `home_sub_page` / `home_screen`: where `EFFECT_UI_SHOW_HOME` goes.
- **Helpers in `menuEffects`** that a hook may call:
  - `menuEffects_shownParam(index)`: the value a row shows, which is the last
    held step's lock where locked, else the retained value;
  - `menuEffects_editParam(index, value)`: `effects_setParameter()` with no
    hold, or `effects_setSeqLaneLock()` on **every** held step during a
    hold;
  - `menuEffects_home()`, `menu_effectShowHome()` (the menu-side jump,
    leaving any full view).
- **Routing rule:** every Effect-page SELECT LED write goes through
  `menuEffects_renderSelectLeds()`: page entry, cursor repair, encoder moves
  and the buttonHandler SELECT path. A direct `led_setActiveSelectButton()`
  would overwrite a type's LED display. `menuEffects_leave()` clears owned
  SELECT LEDs.
- **Live refresh:** when the active type has a `format_value3` hook,
  `menuEffects_liveRefreshWanted()` makes `menu_sceneLiveRefreshService()`
  repaint the Effect page on its bounded cadence while the transport runs
  (never while editing or under the screensaver). CrumpBit uses this so its
  Sync label follows tempo.

### 8.5 Automation underlines (Session 074)

**Rule (user, 2026-09-29):** a parameter's name is underlined when **any**
stored automation addresses it, in the FX sequence or in the viewed
Pattern, whether or not it would play with the current settings. The
underline reports stored data, not what is audible.

| Cell | Pattern automation (viewed Pattern, any step 0..127, any of 7 tracks) | FX lock (active Scene, any of 16 steps) |
|---|---|---|
| `typ`, `run`, `len`, `scl` | never | never |
| `mrp` | Scene target `fxm` (404) | lane 0 |
| PARAM rows without a lane (for example `out`) | block-7 ID `448 + index` | never |
| PARAM rows with a lane | as above | the row's lane (1..15) |

- **Not filters:** FX `len`, run mode, `sel` step, transport, track length,
  mute, trigger bit, probability, and the current type's AUTOMATABLE flags.
  An entry left over from an earlier type underlines the row with that local
  index. The SEQ LEDs still light only steps within `len`.
- **Ignored:** `PAT_AUTOMATION_TARGET_OFF` (`0x1FF`), which decodes as
  Effect local 63 (an "Add" left at `off`).
- **Precedence (one marker per cell):**
  1. a held step that locks this lane: the value on row 1, rightmost glyph
     underlined;
  2. otherwise, if automated: the name on row 0, first non-space character
     underlined. Compact view: the 3-character short name at columns
     `4i..4i+2`. Full view: the long name at columns 8..15 (`mrp` shows
     `Effect  Morph`, so `M` is underlined);
  3. otherwise none. A held but unlocked cell shows its held value **and**
     the name marker.
- **Sources:** FX locks are read live from the record on every repaint
  (`menuEffects_cellSeqLocked()`, 16 mask reads). Pattern automation comes
  from the search shared with the VOICE pages (`va_scanService()`), which
  here walks all 7 tracks at 4 step reads per foreground pass (224 passes)
  and repaints once when complete. The search state is shared and costs 0 B;
  every entry to either page restarts it.
- **Restarts:** Effect-page entry, Pattern change, Pattern or track clear.
  **Not** SHIFT+TRACK (the search already covers every track) and not `typ`
  (the search is type-independent).
- **Not handled** (shared with VOICE): live erase while recording, and a
  deferred Pattern/track clear racing the restarted search.
- **Screens with a type-painted row 0** (CrumpBit's overlay) show no name
  markers; held value markers still work.
- **CGRAM ordering:** marker changes go through
  `va_queueMarkerTransaction()` (restore the old cell, define the glyph, then
  write the frame diff). Any redraw that can move a marker between cells must
  use `menu_repaint()`, never `menu_repaintAll()`, which resets the shadow.
  A deferred transaction is retried once the LCD queue has room (shared
  VOICE/Effect retry, `VA_MARKER_RETRY_BIT`).

### 8.6 CrumpBit's page (`cbt`, Session 074)

| SELECT | Screen | Row 0 | Cells (row 1) |
|---|---|---|---|
| 1 | 0, 1 | `typ out vol pan`, `run len scl mrp` | manager (unchanged) |
| 2 | 0 = **overlay** (home) | bit states `- - - - - - - ->` | `mix fbk rte mrp` |
| 2 | 1 | `mix fbk rte sub` | the same rows, then the sub-type (`dly`) |
| 2 | 2 | `snc dpn` | two empty cells |
| 3..8 | — | (no screens) | SELECT 1..8 are bit buttons on this type |

- **Layout:** `screen_count = {0, 3, 0, …}`, `custom_row0[1] = 0x01`, home
  SELECT 2 screen 0. The encoder walks SELECT 1 s0 → s1 → overlay → page 2 →
  page 3. `mrp` on the overlay is the Scene Effect Morph amount (0..255),
  resolved from `EFFECT_LAYOUT_CELL_MORPH`.
- **Overlay row 0:** column 2*n* shows bit *n* (LSB left): `-` normal, `0`
  forced off, `!` inverted; odd columns are spaces; column 15 is the scroll
  marker. There is no cursor cue and no name underline on this row.
- **SELECT *n*:** bit *n−1* cycles normal → off → invert → normal (a stale
  off+invert pair reads as invert, as the DSP plays it). The whole new
  `bit off` / `bit invert` values are written with `menuEffects_editParam()`.
  With no hold they become the retained masks (fanned out to same-type masked
  Scenes, AutoSave marked). **During a SEQ hold** both mask lanes are locked
  on every held step with the same values, so all held steps end up equal.
  The page then jumps to the overlay: it leaves the full view, abandons an
  open `typ` browse without committing it, and repaints with
  `menu_repaint()`.
- **SHIFT+SELECT *n*:** resets bit *n−1* to normal.
- **SELECT LEDs:** LED *n* is lit when bit *n−1* is off or inverted, always,
  while `cbt` is on the page. The source is `menuEffects_shownParam()`: the
  retained masks, or the last held step's during a hold (never the live
  per-step playing value). They are re-rendered on each hold transition and
  after each SELECT edit.
- **Value labels (`format_value3`):** `sub` shows `dly`. With the shown
  Sync on, `rte` shows the snapped division label (`/16`, `16t`, `1br` …),
  computed by `crumpBit_syncDivision()` with the same rule the DSP uses at
  the current `seq_getBpm()`. The raw 0..127 stays underneath everywhere
  else.
- **Masks are not Pattern-automatable or LFO-modulatable** (7-bit Pattern
  values cannot reach bit 7; interpolated masks are meaningless). They are
  sequenced through FX lanes 1–2, which hold full 8-bit values.
- **Pan rule (S075 F2):** every pan stores 0..127; stored 63 is centre and
  displays 0 through `DTYPE_PM63`, while 0 displays -63 and 127 displays 64.
  Effect and CrumpBit defaults use 63; stereo balance laws are centred on 63
  and mono constant-power laws are unchanged. Effects saved at 64 display 1.

CrumpBit's F2 defaults are mix 0, feedback 64, rate 64, delay pan 63
(display 0), with the sync row labelled `snc`.

---

## 9. Edit fan-out and the layout gate

- **Reach.** Every Effect edit made on the active Scene reaches the active
  Scene's VOICE edit mask:
  - parameters and Morph endpoints;
  - `run`/`len`/`scl`;
  - lane locks;
  - `mrp`;
  - `typ`.
- **Non-active origin (S075).** `effects_fanoutMask()` uses the origin
  Scene's own directional entry: the active Scene through the self-repairing
  getter, any other Scene through `bank_sceneMaskVoiceEditForScene()`, so a
  PERF paste onto a Scene that is not active reaches that Scene's members.
- **Copy/clear (S075).** `effects_pasteRecord(dst, src)` (whole record; may
  change the destination's type; the mask is taken on the destination's
  current type), `effects_resetRecord(dst)` (`off` defaults),
  `effects_pasteSeqStep(dst, step, src)` (lock mask and 16 lane values) and
  `effects_clearSeqLanes(scene, step_mask, lane_mask)` (values 0, unlocked)
  fan out like edits. Record functions return the written Scene mask;
  sequence functions bump the sequence serial and drop the held Morph latch
  when the active Scene's Morph lane changed. An FX step paste whose
  destination type differs from the source is dropped.
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
  `storage_makeSavedEffectDisplayFilename()`. It calls the Instrument name
  builder, then swaps `.drm` for `.fx`.
- **A blank name saves as `none.fx`.** The Instrument builder maps an
  all-space stem to `none`, and loading `none.fx` restores a blank HCNAMES
  row.
  - An **empty** (NUL) stem becomes `inst` instead. Always pass eight
    explicit spaces for a blank row, as Scene Save does.
  - The plan's F4 wording (`' .fx'`) is superseded by G5 and this behavior.
- Load takes the first `*.fx`. Boot Case 2 opens exactly `<row name>.fx`, so
  hand-made stems must be ≤ 8 characters.
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

1. ~~A same-type Scene switch does not refresh the FxBuffer handoff.~~
   **Resolved in S074:** `effects_activateScene()` refreshes it through
   `effects_exportHandoff()` without calling `init`.
2. There is no immediate `fx` LFO rebind on a `typ` change (plan §7.2 item 7).
   It is handled at runtime and at the next rebind (§11.3).
3. `seq_effect_automation_dirty` (plan §10.1) is `effects_automation.active`.
4. Resolution rescans every block instead of using dirty-cell notification
   (plan §9 "Apply").
5. A blank Effect name saves as `none.fx`, not `' .fx'` (plan F4). It uses
   the unchanged Instrument naming path (G5, §12.2).

---

## 14. Tutorial: adding an Effect type

This checklist uses `flt` (no arena, no custom UI) and `cbt` (arena, custom
page) as worked examples. Before you start, settle the type's design with the
user as S074 did for CrumpBit: token and names (permanent once saved), I/O
shape, rows and flags, lanes, the arena format and minimum, tails across
Scene switches, how time changes behave, and RAM (the union has 20 B spare).

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
     A fill counter that mutes unwritten reads is an accepted way to "clear"
     (`cbt`).
   - `write_param`: rows 3.. only. Either convert here (`flt` copies the
     voice-filter shaping) or store raw values and convert once per block in
     `process` (`cbt`). The latter keeps the cost identical under an LFO,
     which rewrites a row every block.
   - `process`: float in place, respecting `io->channels` / NULL `r`.
   - Set `export_handoff`, `buffer_changed` and `effective_max` if the type
     uses the arena. Set `buffer_min_bytes`/`buffer_pref_bytes`. If the type
     must behave identically whatever voice units are claimed, size it to
     the **minimum** share (73,600 B), as `cbt` does, and declare min = pref.
   - If a row's useful maximum depends on the share, flag it
     `EFFECT_PARAM_FLAG_BUFFER_DEPENDENT` and implement `effective_max`
     (a runtime clamp only). A limit that is a *minimum* (for example a
     "rate" where higher means shorter) is not expressible yet: it would
     need a new op or a row stored as time.
4a. **Page (optional).** For a custom page, give the registry row a
   `select_layout` and a `ui` hook table (§8.4). Use `paint_row0` for a
   non-name top row, `format_value3` for value labels, and
   `EFFECT_UI_FLAG_OWNS_SELECT_LEDS` with `render_leds` if the SELECT LEDs
   show type state. Write retained values only through
   `menuEffects_editParam()` so SEQ holds lock every held step. `cbt` is the
   worked example (`CrumpBitParameters.c`).
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
   - `<Type>Effect.c` goes into `DSP_SRCS` **and** needs its own explicit
     `-Ofast` rule (the Makefile pattern rule covers only `Core/DSPAudio/`;
     see the StereoFilter and CrumpBit rules). `<Type>Parameters.c` goes into
     the normal sources; add `-ICore/DSP/Effects/<Type>`.
   - Run `link_budget.py` and record the flash, DTCM and FXBUF sizes in
     `STORAGE_SRAM_MANIFEST.md` (RAM approval policy in `MEMORY.md`).
   - Expect `-Ofast` to unroll the 32-frame loop: CrumpBit's `process` is
     4,804 B, and the whole type cost 11.6 KB of flash against a 3–4 KB
     estimate. `#pragma GCC unroll 1` trims it if flash ever matters (then
     re-check the sound).
7. **Verify.**
   - Build with `DEV_MODE_DIAGNOSTIC=1` and check that the registry
     self-check (`FxBf` screen) is 0. For an arena type, also build with
     `DEV_FXBUF_FORCE_VOICE_UNITS 12` to present the minimum share.
   - Select the type from `typ` and check the page layout, `.fx` round trip,
     AutoSave restore, automation/LFO on each flagged row, the underlines,
     a same-type Scene switch (tails continue), a switch to another type and
     back (no old tail unless the type adopts), and the CPU widget on the
     worst-case Scene with every send open and every modulated row moving.

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
- **Measure flash.** Run `link_budget.py` after every addition. Since
  Session 073 the application window is 736 KiB (250,736 B free at the S074
  close; `STORAGE_SRAM_MANIFEST.md` §3).
- **The runtime union has 20 B spare** (76 B, CrumpBit 56 B). Anything larger
  grows the union and shrinks the arena by the same amount: RAM approval
  first. Audio state belongs in the arena share.
- **SELECT LEDs:** route every Effect-page SELECT LED write through
  `menuEffects_renderSelectLeds()`, or a type that owns them loses its
  display.
- **Marker redraws:** use `menu_repaint()` for anything that can move an
  underline between rows (§8.5).
- **Arena content is undefined after `init`** and after a share change.
  CrumpBit's `valid` counter is the pattern: never read further back than
  you have written since the loop was seated.
- **Bus headroom.** The bus is `sample_mx_t` with saturation only at the
  int32 limit. Types must process in float and must not truncate to int16
  (this is why `SVF_calcBlockZDFFloat()` exists).


---

## 16. Lifecycle: boot, Scene activation, loads

### 16.1 Boot order (`main.c`)

1. `dsp_init()`:
   - `fxbuf_init()` captures `_sfxbuf`/`_efxbuf` and publishes the "nothing
     valid" handoff.
   - `instrumentManager_runtimeInit()`.
   - `effects_init()` registers the share callback and runs the DEV registry
     self-check.
   - `mixer_init()` seeds the voice ramps at `slider × volume`.
2. The pre-audio filesystem boot restores Scenes: AutoSave Case 1 applies
   the Effect region; Case 2 narrow-reloads `<name>.fx`.
3. The boot sound apply calls `preset_sendDrumsetParameters()` →
   `effects_activateScene()`.
4. `bank_revalidateVoiceEditMasks()` runs.
5. `preset_startDrumsetApply()` replays the runtime Scene switch, including
   `effects_activateScene()` and the all-source LFO rebind.
6. Optional `DEV_EFFECT_FORCE_TYPE` (diagnostic builds only).

### 16.2 Scene activation (`effects_activateScene()`)

Called from `preset_sendDrumsetParameters()` (pre-audio) and
`preset_startDrumsetApply()` (every Scene switch and Scene/Bank load apply).

- A different type goes through `effects_switchRuntime()`:
  1. handoff `BeginExit` → exited type and channels → `export_handoff`;
  2. zero the runtime union;
  3. `init(rt, handoff)`;
  4. force a full re-resolution.
- The same type keeps running (tails ring, F6). Since S074 the handoff is
  refreshed (`effects_exportHandoff()`) without `init`, so the record always
  describes the live arena owner.
- Either way it:
  - invalidates the FX clock step and held Morph;
  - clears the Pattern overlays and the `fxm` override;
  - bumps the sequence serial;
  - forces a pass.

### 16.3 Load paths and the Effect

| Operation | Effect record | `effect_morph_amount` | Mask re-validation |
|---|---|---|---|
| Kit Load / KitMrp | untouched | untouched | yes (`menu_startSoundApply()`) |
| Instrument Load | untouched | untouched | yes (`menu_startInstrumentApply()`) |
| Scene Load | replaced atomically with settings + Kit (staged, `.fx` parsed before the Kit phases) | from `sceneset.scg` | yes |
| Bank Load (selected children) | replaced per child, same path | per child | yes; the empty-Bank branch too |
| AutoSave boot | Case 1 region apply / Case 2 narrow reload | Scene parameter 40 | yes (end of boot) |

The Scene loader phase order: scan (9) → `sceneset.scg` (12–16) → `.fx`
(56–60) → Kit (17–32) → one commit of settings + Kit + Effect (33) →
Pattern (44–53) → publish (61).

- A malformed `.fx` fails before any resident byte changes.
- A root Scene that fails is quarantine-renamed by the existing path
  (phase 62→68). A Bank child clears its present bit instead.

---

## 17. Design rationale (why it is built this way)

- **Pre-volume send and volume last.**
  - The send must include each voice's distortion but not its level (A24/A25).
    So the engines render pre-volume, and the mixer applies `vol` after
    decimation.
  - This also fixed Snare/Cymbal/HiHat applying `vol` before distortion,
    where `vol` had acted as a hidden drive. Volume changes now ramp per
    block (`mixer_voice_last_gain`).
- **`sample_mx_t` bus, float Effect.**
  - Voices sum with the mixer's saturating int32 add (×256 from int16), so
    six loud sends cannot wrap.
  - The Effect sees normalized float (÷ 8,388,352) and can exceed 1.0; its
    own soft clip shapes the overshoot.
  - The return converts back with a ±255 full-scale clamp
    (`mixer_floatToMx()`) and is saturated once into the DAC buses.
- **Balance law.** A centred stereo return at `vol` 127 must equal its bus,
  so stereo outputs use a linear balance with unity at centre. Mono outputs
  use the voices' constant-power law.
- **Rescan every block.** At most about 1,000 cycles for a 64-row type (about
  100 for `flt`). No writer can forget to notify: loaders, AutoSave,
  fan-out, overlays and LFO all "just work".
- **In-place whole commit** for type changes. It avoids a 420 B stack copy.
- **One-byte TIM3 latch.** Position is a pure function of
  `seq_elapsedPpqTicks` and the *current* Scene's settings, so a Scene switch
  lands where the new Scene "would be" (A17). TIM3 only publishes; all work
  is foreground.
- **Effect step markers through the pending queue.** An overlay "end" is
  ordered with "apply" in one queue, so an end can never overtake the value
  it ends. The reset latch is taken before any record, so a new pass's first
  values survive the reset.
- **Base-independent LFO entries** (direction + depth). The same math as the
  Instrument LFO shaper, applied around the current held value. Automation or
  a lock under an LFO never leaves a stale base (the S071 voice-Morph
  pattern).
- **Fan-out inside the setters, and a gate on the mask.**
  - Every caller gets fan-out for free.
  - The gate plus re-validation at completion funnels is robust, because slot
    types change through many internal writers but only a few completion
    points.
- **Arena "clear unless you adopt"** instead of system clears (A31, F6). A
  delay can keep its tails across same-type Scene switches, and the next
  owner decides what is valid.

---

## 18. Debugging and verification toolkit

- **Link budget.** Run
  `python3 tools/link_budget.py arm-none-eabi-nm build/lxr02.elf` after every
  change. It prints flash headroom (warns below 16 KiB), ITCM, DTCM statics,
  and the FXBUF size, address and margin. Use `make all`: a bare `make` in an
  incremental tree may only rebuild `build/main.o`.
- **Diagnostic boot screen** (`DEV_MODE_DIAGNOSTIC 1`):
  - `FxBf <KiB> u<units>` / `Shr <KiB> st<self-test>`;
  - the final hex digit is the registry self-check (§4.4).
  - `DEV_FXBUF_FORCE_VOICE_UNITS` presents the minimum share.
  - `DEV_EFFECT_FORCE_TYPE 1` forces `flt` after boot. It fans out and
    clears sequences exactly like a `typ` edit.
- **AutoSave trace** (`asavetrc.bin`, `tools/decode_devlogs.py`): Effect
  region cells are labelled, and Scene parameter 40 prints as `fxm_amt`.
- **Card checks:**
  - `.hcprms` header version byte 3;
  - an Effect region starting with the token (`66 6C 74` = `flt`,
    `63 62 74` = `cbt`, `6F 66 66` = `off`);
  - source bytes at relative 430..431 (`FF 1F` = inherit);
  - `.hcnames` with 161 data rows.
- **Fixture files:**
  - hand-written v2 `.fx` in Scene/Bank folders, with stems ≤ 8 characters;
  - a legacy `placeholder=1` file (→ `off`);
  - a missing file (→ `off`, blank);
  - a malformed file (→ load fails with the resident Scene untouched; use a
    disposable copy because of the root-Scene quarantine rename).
- **Build-host quirk.** An `lto1` "Bus error" internal compiler error can
  leave an empty `build/lxr02.elf`, which is likely when two builds share
  `build/`. Delete the ELF and relink.

---

## 19. History

- Phase 5 was implemented in Session 072, Steps 1–11.
- The per-step record (decisions, measurements, review findings and the
  hardware acceptance checklist) is in
  `knowledge_files/log_archive/072_SESSION_HANDOFF_LOG.md`.
- The design decisions were in `EFFECTS_BUS_FEATURE_PLAN.md` §18 (deleted in
  `ca77891`; `git show f3a3105:EFFECTS_BUS_FEATURE_PLAN.md`).
- Session 073: one-pass mixer dry + send, batched float filter.
- **Session 074** (`074_SESSION_HANDOFF_LOG.md` §5, §7, §10):
  - the automation underlines (§8.5) and the hold/release ordering fix;
  - CrumpBit (`cbt`), the first buffer-using type, accepted on hardware as
    v1;
  - the page framework extensions (§8.4) and the last-step-held rule;
  - the three arena gaps closed (§7.3, §13 item 1, and the mixer's `off`
    branch);
  - the `xfd` fader mode.
  - This largely completes Phase 5. Still deferred: the `/Effect/` browser
    and Load/Save item (A35), MIDI
    mapping (A20), live record of FX moves (A22), and track
    step-scale/shuffle playback (A10).
- **Session 075** (Phase 6 copy/clear, `S075_PH6_COPY_CLEAR_FULL_SPEC.md`):
  FX lock removal (A15 closed), Effect and FX-sequence copy/clear with
  fan-out (§9), PERF `fxm` cell for Effect Morph in the former `srt` slot.
