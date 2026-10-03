# S075 — Phase 6 Copy and Clear — F2: Implementation Schedule

Code schedule for `S075_PH6_COPYCLEAR_F2.md`, sections F2-A … F2-H, with the
user decisions F2-Q1 … F2-Q9 (F2 §12.1, 2026-10-03) folded in. This document
is the implementation record as well as the schedule; code and verification
notes are appended to §15.

- **Baseline:** working tree of `dev-ph6-copyclear` after the F1 + trace
  implementation (commit `dbc8104`). Every line number below was read from
  that tree on 2026-10-03 and is given with a function or text anchor. When
  lines move during the pass, use the anchor.
- **Comment blocks:** every code change carries its block. **(.h)** blocks go
  above the declaration in the header and are the contract. **(.c)** blocks go
  above the definition, or inside the function as shown, and state the
  implementation detail. Where both exist, the `.c` block points to the `.h`
  block instead of repeating it.
- **Change ids:** `<stage>-<nn>`, for example `F-07`. **ADD** = new code,
  **MODIFY** = replace the quoted lines, **REMOVE** = delete the quoted lines.
- **Order:** Stage A … Stage K. Each stage builds on its own. Stage F
  (FX-send Morph data) must land before Stage G (menu editing of it), and
  Stage I (overlay) uses the Stage H SHIFT handling.
- **No commits** are part of this schedule.

## Contents

0. Decisions and deviations from the plan
1. RAM, stack and flash ledger
2. Stage A — default voice route St1 (F2-A, F2-Q1)
3. Stage B — bus compressor defaults off / 0 / 0 / off (F2-B, F2-Q2)
4. Stage C — copy/clear header `Copy` / `Clear` (F2-C)
5. Stage D — Effect page full views: long name only (F2-D, F2-Q3)
6. Stage E — one pan rule; CrumpBit defaults; `snc` (F2-E, F2-Q4)
7. Stage F — FX-send Morph endpoint: data, storage, AutoSave, Preset (F2-H, F2-Q8, F2-Q9)
8. Stage G — Menu: show and edit the FX-send endpoints (F2-H)
9. Stage H — VOICE mode: hold SHIFT for the Morph view (F2-G, F2-Q7)
10. Stage I — Effect page: SHIFT+TRACK voice mix overlay (F2-F, F2-Q5, F2-Q6)
11. Stage J — tools
12. Stage K — documentation
13. Build and verification
14. Risks and notes
15. Work log

---

## 0. Decisions and deviations from the plan

Revision 2 (2026-10-03): SELECT under the overlay keeps the Effect type's
SELECT actions (0.5); all backward-compatibility code is removed (0.8);
`tools/verify_bank_autosave.py` is brought up to date with the firmware
(J-02).

| # | Item | Source | Where |
|---|---|---|---|
| 0.1 | All user decisions F2-Q1 … F2-Q9 | F2 §12.1 | throughout |
| 0.2 | **Pan (F2-Q4).** One rule for every pan: stored 0 = fully left = `-63`; stored 127 = fully right = `64`; default = absolute centre = stored **63** = `0`. `DTYPE_PM63` already displays value − 63 for every pan, so **no dtype change** (plan option A, `DTYPE_PM64`, is withdrawn). The Effect pan rows (`effect_pan`, CrumpBit `crump_dly_pan`) default to 63, and the two stereo balance laws (Effect return `mixer.c`, CrumpBit delay `CrumpBitEffect.c`) move their centre from 64 to 63, so 63 is unity on both sides and the ends stay exact. The mono laws (instrument `squareRootLut[127-pan]`/`[pan]` and the mono Effect return) are not changed; stereo and mono maths differ but display and default the same. **Instrument defaults are not touched.** | F2-Q4 + addendum | Stage E |
| 0.3 | **Overlay does not use `menu_switchPage()`.** A page switch runs `menuEffects_leave()` (clears the Effect SEQ blinks, SELECT LEDs, hold state and Morph view) and repaints the Pattern track LEDs, which would break "keep LEDs" (F2-Q6 d) and lose the Effect position. The overlay swaps `menu_activePage` / `menuIndex` directly and saves/restores the Effect position in its 4-byte record. | deep dive | I-01 … I-04 |
| 0.4 | **Effect LEDs stay live during the overlay.** `menu_serviceRuntimeWidgets()` keeps calling `menuEffects_service()` while the overlay is up (it draws the FX SEQ row, flashes and type-owned SELECT LEDs); its menu actions (`REPAIR`, `EXIT_EDIT`) are latched in the overlay record and applied when the Effect page returns. Three `ledHandler.c` gates that today test `menu_activePage == EFFECT_PAGE` also test the overlay, so Pattern chase, record and follow feedback do not draw over the Effect row. | deep dive; F2-Q6 d | I-05, I-06 |
| 0.5 | **SELECT under the overlay (user, revision 2).** SELECT functions that the Effect type assigns (its SELECT hook, with or without SHIFT, for example CrumpBit's data-line toggles) keep working while TRACK is held, and their LEDs update. Every menu screen change is suppressed while TRACK is held: a hook's "show home screen" request only re-renders the SELECT LEDs, and the default SELECT navigation (Effect screen cycling) does nothing. SEQ and BAR keep their Effect-mode paths; pots and encoder turns edit the shown voice cells. A real page/mode switch during the overlay ends it without restoring the Effect page (and runs `menuEffects_leave()` when the destination is not the Effect page). | user; deep dive | I-04, I-10 |
| 0.6 | **SHIFT in the overlay (F2-Q6 c):** SHIFT+TRACK is the entry; SHIFT held = voice Morph view; SHIFT released = Normal view while TRACK alone keeps the Scene-settings screen up. SHIFT pressed again while TRACK is held shows the Morph view again. | F2-Q6 c, F2-Q7 | I-11, I-12 |
| 0.7 | **FX-send display.** Normal view shows the step override while one is active, else the Normal endpoint; Morph view shows the Morph endpoint. Neither view shows the morphed (interpolated) send — the same rule as instrument cells, which show their endpoint images. | F2 §9 | F-16, G-01 |
| 0.8 | **No backward compatibility (user, revision 2).** The user deletes AutoSave records and other temporary records after this pass. AutoSave gets the six Morph cells 45..50 and nothing else (no marker, no restore migration, no boot-latch use); `sceneset.scg` gets the `fx_send_morph` key with no missing-key rule (a file without it loads the stage default 0). All 402 `SD_CARD` Scene fixtures have FX send 0, so they load identically. | user | F-07 … F-13 |
| 0.9 | The `fx_send_morph` writer line is appended as writer line 14 (after the bus compressor lines), so no existing line number moves. Parsing is key-based. | deep dive | F-12 |
| 0.10 | **Resolved voice Morph amount.** The send is interpolated with the amount the Morph worker uses: step override or retained base, plus active LFO contributions when the voice has an LFO layer (active Scene only). A new read-only `presetMorph_getResolvedVoiceAmount()` exposes the existing private resolver; no RAM. When Normal and Morph are equal the getter returns at once and never scans the LFO table. | F2 §9 | F-14, F-15 |
| 0.11 | F2-Q1 includes the existing legacy kitset import fallback: it only fires when an old file's route is missing or invalid, and then uses St1 (comment update only, A-03). | F2-Q1 | A-02, A-03 |
| 0.12 | `mixer_init()` already zeroes `mixer_audioRouting[]` (`mixer.c` line 140): no mixer change for F2-A. | deep dive | — |

---

## 1. RAM, stack and flash ledger

| Item | Section | Bytes | Approval |
|---|---|---:|---|
| `scene_settings_t.fx_send_morph[6]` × 16 Scenes | SRAM1 `.bss` (`scenes`) | **+96** | F2-Q8 |
| Overlay record `menu_fxVoiceMixOverlay` (flags, Effect `menuIndex`, Effect edit mode, saved mix screen) | SRAM1 `.bss` (menu.c) | **+4** | F2-Q5 |
| `buttonHandler_fxVoiceMixTrackMask` | SRAM1 `.bss` (buttonHandler.c) | **+1** | F2-Q5 |
| AutoSave cells 45..50 (inside the 118-byte Scene parameter reserve) | card record | 0 RAM | — |
| **Total** | | **+101** | approved |

**Layout check (measured 2026-10-03, arm-none-eabi-gcc 14.2, the build's
flags):** `sizeof(scene_t)` = 1624 B, `_Alignof(scene_t)` = 2,
`offsetof(scene_t, effect)` = 44, `sizeof(scene_settings_t)` = 44,
`sizeof(effect_record_t)` = 420, `instrument_type_t` = 1 B. All
`scene_settings_t` members are bytes, so the struct grows 44 → 50 B and
`scene_t` 1624 → 1630 B with no padding: `scenes` 0x6580 → **0x65E0**
(+96 B exactly). Build gate in §13: if `scenes` is not 0x65E0, stop and
report before continuing.

**Stage workspace:** the `filesystem.c` static assert at line ~1121
(`FS_STAGE_CACHE_BYTES >= 1536 + sizeof(scene_settings_t) + 2 + 6 + 420`)
becomes 2014 ≤ 2048: holds. `filesystem_scene_stage_t` (settings + Kit +
Effect) grows by 6 B inside the 2048 B union: holds.

**Stack:** `scene_commitSettings()` keeps two `scene_settings_t` copies on
the stack: 2 × 50 = 100 B (was 88 B). `preset_getEffectiveFxSendAmount()`
adds three bytes of locals. No other stack change.

**Flash:** estimated +1.0 kB (setter/getter pair, Preset accessors, AutoSave
branches, parser key, writer line, overlay begin/end, button handling).

**CPU:** `preset_getEffectiveFxSendAmount()` runs six times per audio block.
Equal endpoints: one extra byte compare per voice. Different endpoints: one
LFO-layer scan (6 × 2 contributions) plus one integer lerp per voice; with an
active LFO layer, one more 12-step resolver pass. Well under 1 µs per block
at 216 MHz.

---

## 2. Stage A — default voice route St1 (F2-A, F2-Q1)

### A-01 — `Core/Bank/Scene/SceneData.c` lines 28–44, `scene_defaultVoiceAudioOut()` — MODIFY

Replace the whole function (block and body):

```c
/*
 * Default Scene route of a voice (S075 F2-A, user decision F2-Q1).
 *
 * What: route 0 (St1, MenuText.h route ids: 0 St1, 1 St2, 2 L1, 3 R1, 4 L2,
 * 5 R2) for every instrument slot. It replaces the old boot-Kit convention
 * (voice 1 -> L1, voice 6 -> St2), which made a cleared Scene come up with
 * voices 1 and 6 on unexpected outputs.
 * Why: one default on every path: fresh Scenes (scene_initAll()), `clear
 * scene` and `clear scene settings` (scene_settingsDefaults()), and the
 * invalid-value fallbacks of scene_setVoiceAudioOut()/scene_getVoiceAudioOut().
 * Inputs: zero-based slot; kept so callers do not change, although the value
 * no longer depends on it. Output: 0.
 * Accessors: scene_initAll(), scene_settingsDefaults(),
 * scene_setVoiceAudioOut(), scene_getVoiceAudioOut().
 * Affiliates: filesystem_defaultVoiceAudioOut() (same value for the Scene
 * Load stage and the legacy kitset fallback), preset_applyKitAudioRouting(),
 * mixer_init() (runtime routes also start at 0).
 */
static uint8_t scene_defaultVoiceAudioOut(uint8_t slot)
{
    (void)slot;
    return 0u;
}
```

### A-02 — `Core/Hardware/SD/filesystem.c` lines 16745–16768, `filesystem_defaultVoiceAudioOut()` — MODIFY

Replace the whole function:

```c
static uint8_t filesystem_defaultVoiceAudioOut(uint8_t slot)
{
    /*
     * Local copy of the SceneData route default for filesystem staging
     * (S075 F2-A, user decision F2-Q1).
     *
     * What: route 0 (St1) for every slot, the same value as
     * scene_defaultVoiceAudioOut() in SceneData.c.
     * Why: filesystem.c initializes off-Scene staging memory and cannot route
     * through scene_setVoiceAudioOut(), which writes resident SceneData by
     * index. One default everywhere: a sceneset.scg without `audio_out`, the
     * boot empty-Scene path, and the legacy kitset import fallback (which
     * only fires when an old file's route is missing or out of range).
     * Inputs: zero-based voice slot (unused). Output: 0, inside the persisted
     * route domain 0..5.
     * Accessors: filesystem_initSceneStage(), the boot empty-Scene reset
     * (line ~28843), the legacy kitset fallback in Scene Load (line ~12693).
     * Affiliates: scene_defaultVoiceAudioOut(), preset_applyKitAudioRouting().
     */
    (void)slot;
    return 0u;
}
```

### A-03 — `Core/Hardware/SD/filesystem.c` line ~12670 block (legacy kitset import) — MODIFY text only

In the block above `if (!op_sceneset_state.seen_audio_out && …`, replace
"The per-slot loop clamps corrupt route bytes to the same defaults used by
new-format scenes." with:

```c
                 * domain 0..5. The per-slot loop replaces a corrupt route
                 * byte with the Scene default route, St1 (S075 F2-Q1).
```

### A-04 — `tools/populate_scene_directory.py` line 25 — MODIFY

```python
# S075 F2-A: every voice defaults to route 0 (St1); the firmware fallbacks
# (scene_defaultVoiceAudioOut(), filesystem_defaultVoiceAudioOut()) agree.
DEFAULT_AUDIO_OUT = [0, 0, 0, 0, 0, 0]
```

---

## 3. Stage B — bus compressor defaults off / 0 / 0 / off (F2-B, F2-Q2)

### B-01 — `Core/Bank/Scene/SceneData.h` lines 146–147 — MODIFY

```c
/*
 * Bus compressor defaults (S075 F2-B, user decision F2-Q2): mode off,
 * amount 0, time 0, sidechain off. A fresh, cleared or default-staged Scene
 * has no compression and neutral values. Used only through
 * scene_busCompDefault[] in SceneData.c, which feeds scene_busCompDefaults()
 * for every default path: scene_initAll(), scene_settingsDefaults() (`clear
 * scene`, `clear scene settings`), filesystem_initSceneStage() (missing
 * `bus_comp_*` keys) and the boot empty-Scene reset.
 */
#define SCENE_BUS_COMP_DEFAULT_AMOUNT  0u
#define SCENE_BUS_COMP_DEFAULT_TIME    0u
```

### B-02 — text-only updates "48, 48" → "0, 0"

| File | Line | New text |
|---|---|---|
| `Core/Bank/Scene/SceneData.h` | 498 | `scene_busCompDefaults() writes off, 0, 0, off into a settings image for` |
| `Core/Bank/Scene/SceneData.h` | 519 | `Morph 0, bus compressor off/0/0/off). scene_commitSettings(): copies a` |
| `Core/Bank/Scene/SceneData.c` | 840 | `* What:       maxima (St2, 127, 127, voice 6) and defaults (off, 0, 0,` |
| `Core/Bank/Scene/SceneData.c` | 1029 | `/* S074/S075 F2-B: bus compressor defaults are off, 0, 0, off. */` |
| `Core/Hardware/SD/filesystem.c` | 16663 | `* S074: stage bus compressor defaults (off, 0, 0, off; S075 F2-B) through the` |

No code other than B-01 changes: `scene_busCompDefault[]` (`SceneData.c`
line 848) already uses the macros.

---

## 4. Stage C — copy/clear header `Copy` / `Clear` (F2-C)

### C-01 — `Core/Menu/CopyClear/copyClearSession.c` lines 856–857, `copyClear_formatMenu()` — MODIFY

```c
    /*
     * Row 0: operation word, then the source indicator (S075 F2-C).
     *
     * What: `Copy` or `Clear` from column 0 (user: mixed case, full word),
     * then the source indicator from column 8, the 9th character (F1-D), so
     * the indicator never moves with the word length. Inputs: cc_state.phase.
     * Output: row0[0..15]. Affiliates: cc_formatIndicator(),
     * copyClear_menuVisible() contract in copyClearSession.h.
     */
    cc_put(row0, &pos, (cc_state.phase == CC_OP_CLEAR) ? "Clear" : "Copy");
    pos = 8u;
```

### C-02 — `Core/Menu/CopyClear/copyClearSession.h` lines 148–149 — MODIFY

```c
 * 16-character rows (NUL at index 16): row 0 `Copy`/`Clear` at column 0 and
 * the source indicator from column 8 (the 9th character), row 1 the
 * bracketed selection label.
```

---

## 5. Stage D — Effect page full views: long name only (F2-D, F2-Q3)

### D-01 — `Core/Menu/menuEffects.c` lines 540–547, `menuEffects_copyField()` — MODIFY

```c
/*
 * Copy a bounded, space-padded LCD field (S075 F2-D fix).
 *
 * What: copies `src` up to its terminator or `width` characters, then pads
 * the rest of the field with spaces. Why: the former loop tested src[i] for
 * every column, so after a short string's terminator it kept copying the
 * bytes that follow it in flash: `Off` + NUL + `1 Mo…` painted "Off 1 Mo" on
 * the `typ` full view. Inputs: destination (at least `width` bytes), source
 * (NULL paints spaces), width 0..16. Output: exactly `width` bytes written;
 * no terminator is added (the edit buffer rows carry their own).
 * Callers: menuEffects_paintEditView(). Affiliates: editDisplayBuffer.
 */
static void menuEffects_copyField(char *dst, const char *src, uint8_t width)
{
    uint8_t i = 0u;

    if (src) {
        for (; i < width && src[i] != '\0'; i++)
            dst[i] = src[i];
    }
    for (; i < width; i++)
        dst[i] = ' ';
}
```

### D-02 — `Core/Menu/menuEffects.c` line 549, block above `menuEffects_paintEditView()` — MODIFY

```c
/*
 * Paint the manager-owned Effect full views (TYPE, RUN, LENGTH, SCALE, MORPH).
 *
 * What: row 0 is the group/name pair; row 1 shows the value. Rule (S075 F2-D,
 * user): a full view never shows two forms of one value. Named values (TYPE,
 * RUN, SCALE) show only their long name from column 0; numeric values
 * (LENGTH, MORPH) show only the number at column 13. The `typ` browse has no
 * "changed" mark: it commits on the encoder click, which is the gate (F2-Q3).
 * Inputs: the resolved cell; the Effect record. Output: 1 when this function
 * painted editDisplayBuffer, 0 for PARAM/NONE cells (Menu paints those).
 * Callers: menu.c full-view repaint. Affiliates: menuEffects_copyField(),
 * effects_registryEntry(), menuEffects_runLong[], stepScale_longName().
 */
```

### D-03 — `menuEffects_paintEditView()` `MENU_FX_CELL_TYPE`, lines 565–568 — REMOVE

```c
        if (menuEffects_typeEdit && value != record->type)
            editDisplayBuffer[1][11] = '*';
        (void)menuEffects_formatValue3(cell, &editDisplayBuffer[1][13]);
```

`record` stays used by the function's NULL guard; `menuEffects_typeEdit`
keeps its other users (browse state).

### D-04 — `MENU_FX_CELL_RUN`, line 575 — REMOVE

```c
        (void)menuEffects_formatValue3(cell, &editDisplayBuffer[1][13]);
```

### D-05 — `MENU_FX_CELL_SCALE`, line 585 — REMOVE

```c
        (void)menuEffects_formatValue3(cell, &editDisplayBuffer[1][13]);
```

`menuEffects_formatValue3()` remains used by the compact (four-cell) view;
check with the build that it has no unused-static warning (it is non-static
in `menuEffects.h` today; no change expected).

---

## 6. Stage E — one pan rule; CrumpBit defaults; `snc` (F2-E, F2-Q4)

### E-01 — `Core/DSP/Effects/EffectTypes.h` lines 63–66 — MODIFY

```c
/*
 * Common defaults: St1 route, unity return, centred pan.
 *
 * Pan (S075 F2-E, user decision F2-Q4): every pan in the product uses one
 * rule. Stored 0 is fully left and shows -63; stored 127 is fully right and
 * shows 64 (DTYPE_PM63 displays value - 63); the default is the absolute
 * centre, stored 63, which shows 0. The Effect return's stereo balance law
 * (mixer.c) and the CrumpBit delay pan (CrumpBitEffect.c) are centred on 63
 * to match; mono pans keep their squareRootLut law. Users:
 * scene_effectRecordDefaults() (SceneData.c lines 643/647),
 * effects_init() runtime common state (EffectsManager.c line 1433), and the
 * `effect_pan` row default (EffectParamRows.h line 62).
 */
#define EFFECT_COMMON_DEFAULT_AUDIO_OUT  0u
#define EFFECT_COMMON_DEFAULT_LEVEL      127u
#define EFFECT_COMMON_DEFAULT_PAN        63u
```

### E-02 — `Core/DSPAudio/mixer.c` lines 1029–1037, Effect return gains — MODIFY

```c
		/*
		 * Effect return pan (S075 F2-E, user decision F2-Q4).
		 *
		 * What: stereo output uses a balance law centred on 63, the stored
		 * centre of every pan (shows 0 through DTYPE_PM63): at 63 both sides
		 * are unity, 0 silences the right side, 127 silences the left side.
		 * Mono output keeps the constant-power squareRootLut law used by the
		 * instrument pans. Why: one pan rule for display and default; the
		 * maths of stereo and mono may differ. Inputs: fx_pan 0..127 from
		 * fx_common (runtime Effect common state), fx_level. Outputs: gainL,
		 * gainR for mixer_addFxReturnToOutput(). Stored pans of 64 from
		 * earlier firmware now sit one step right (left gain 63/64).
		 * Affiliates: EFFECT_COMMON_DEFAULT_PAN (EffectTypes.h), CrumpBit
		 * delay pan (CrumpBitEffect.c), EFFECTS_MIXER_DSP_REFERENCE.md.
		 */
		if (fx_stereo_out) {
			gainL = fx_level * ((fx_pan <= 63u) ? 1.0f
					: (float)(127u - fx_pan) / 64.0f);
			gainR = fx_level * ((fx_pan >= 63u) ? 1.0f
					: (float)fx_pan / 63.0f);
		} else {
			gainL = fx_level * squareRootLut[127u - fx_pan];
			gainR = fx_level * squareRootLut[fx_pan];
		}
```

### E-03 — `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c` lines 329–332 — MODIFY

```c
    /*
     * Delay pan balance centred on 63 (S075 F2-E, F2-Q4): 63 = both sides
     * unity and shows 0, 0 = right silent, 127 = left silent. Same rule as
     * the Effect return's stereo law in mixer.c. Input: rt->pan_raw 0..127
     * (CRUMPBIT_PARAM_DLY_PAN). Output: block-ramp targets gain_l_to,
     * gain_r_to.
     */
    gain_l_to = (rt->pan_raw <= 63u) ? 1.0f
        : (float)(127u - rt->pan_raw) * (1.0f / 64.0f);
    gain_r_to = (rt->pan_raw >= 63u) ? 1.0f
        : (float)rt->pan_raw * (1.0f / 63.0f);
```

### E-04 — `Core/DSP/Effects/CrumpBit/CrumpBitParameters.c` lines 62–69 — MODIFY

```c
    EFFECT_ROW("crump_mix", "Delay", "Mix", "mix", DTYPE_0B127,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 0u, 127u),
    EFFECT_ROW("crump_feedback", "Delay", "Feedback", "fbk", DTYPE_0B127,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 64u, 127u),
    EFFECT_ROW("crump_rate", "Delay", "Rate", "rte", DTYPE_0B127,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 64u, 127u),
    EFFECT_ROW("crump_subtype", "CrumpBit", "SubType", "sub", DTYPE_0B127,
               0u, EFFECT_MOD_NONE, CRUMPBIT_SUBTYPE_DELAY, 0u),
    EFFECT_ROW("crump_sync", "Delay", "Sync", "snc", DTYPE_ON_OFF,
               INSTRUMENT_PARAM_FLAG_AUTOMATABLE, EFFECT_MOD_NONE, 0u, 1u),
    EFFECT_ROW("crump_dly_pan", "Delay", "DlyPan", "dpn", DTYPE_PM63,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 63u, 127u),
```

### E-05 — same file, table block lines 30–49 — ADD before "Why: the registry…"

```c
 * - Defaults (S075 F2-E, user): mix 0, feedback 64, rate 64, delay pan 63
 *   (the absolute centre, shows 0, see EFFECT_COMMON_DEFAULT_PAN). They
 *   apply when the type is chosen or reset (effects_recordDefaultsForType(),
 *   `clear fx`); saved Effects keep their values.
 * - Labels: the sync row's 3-letter label is `snc` (S075 F2-E); its file key
 *   `crump_sync` is unchanged, so `.fx` files and AutoSave are unaffected.
```

---

## 7. Stage F — FX-send Morph endpoint (F2-H, F2-Q8, F2-Q9)

### F-01 — `Core/Bank/Scene/SceneData.h` lines 178–192, `scene_settings_t` — MODIFY block, ADD field

Block (replace the paragraph starting "fx_send_amount and fader_setting are
retained now…"):

```c
     * fx_send_amount and fx_send_morph are the Normal and Morph endpoints of
     * the per-voice FX send (0..127; Morph endpoint S075 F2-H). The live send
     * is interpolated between them by the voice's resolved Morph amount
     * (step override or retained amount, plus any LFO layer) each audio
     * block in preset_getEffectiveFxSendAmount(); a step-automation `Nfx`
     * value overrides both while its step plays. A Scene file without the
     * `fx_send_morph` key loads Morph 0 (the stage default).
     * fader_setting is 0..SCENE_FADER_SETTING_MAX
     * (0..3): pre (normal/pre-FX), pst (post-FX), fx (FX-only) and xfd (dry
     * to FX crossfade, S074), interpreted by the mixer FX path. Preset
     * setters store the values and the live mixer applies the selected mode.
```

Field, after `fx_send_amount`:

```c
    uint8_t fx_send_amount[INSTRUMENT_SLOT_COUNT];
    uint8_t fx_send_morph[INSTRUMENT_SLOT_COUNT];
    uint8_t fader_setting[INSTRUMENT_SLOT_COUNT];
```

No C struct layout is serialized (AutoSave and `sceneset.scg` are
field-by-field), so the position is free; next to its Normal endpoint is the
readable place.

### F-02 — `Core/Bank/Scene/SceneData.h` after line 426 (`scene_getVoiceFxSendAmount`) — ADD

```c
/*
 * Morph endpoint of one voice's FX send (S075 F2-H).
 *
 * What: scene_setVoiceFxSendMorph() stores the Morph endpoint (clamped to
 * 0..127) through the change-aware Scene store, which marks AutoSave Scene
 * parameter AUTOSAVE_SCENE_PARAM_FX_SEND_MORPH_BASE + slot and the card-clean
 * bit only when the byte changes; scene_getVoiceFxSendMorph() returns it, or 0
 * for an invalid Scene/slot or an out-of-domain byte.
 * Why: SceneData is the only writer of retained Scene data; the mixer pulls
 * the interpolated send every block, so there is no runtime push.
 * Inputs: resident Scene index, zero-based slot (track 7 uses slot 6), amount.
 * Clients: preset_setVoiceFxSendMorph() (menu, clear send),
 * scene_commitSettings() (copy/clear Scene settings),
 * autosave_applyScenePayload() (boot restore), Preset effective/display
 * getters. Affiliates: fx_send_amount (Normal endpoint) and the same-named
 * `sceneset.scg` key.
 */
void scene_setVoiceFxSendMorph(uint8_t scene_index, uint8_t slot,
                               uint8_t amount);
uint8_t scene_getVoiceFxSendMorph(uint8_t scene_index, uint8_t slot);
```

### F-03 — `Core/Bank/Scene/SceneData.c` after `scene_getVoiceFxSendAmount()` (ends line ~492) — ADD

```c
void scene_setVoiceFxSendMorph(uint8_t scene_index, uint8_t slot,
                               uint8_t amount)
{
    scene_t *scene = scene_get(scene_index);

    /*
     * Store one FX-send Morph endpoint (contract in SceneData.h, S075 F2-H).
     *
     * Same owner path as scene_setVoiceFxSendAmount(): clamp, then
     * scene_storeParameterByte() writes the byte before notifying AutoSave
     * cell FX_SEND_MORPH_BASE + slot, and does nothing when the byte is
     * unchanged. No runtime write: the mixer reads the interpolated send
     * at its next block.
     */
    if (!scene || slot >= INSTRUMENT_SLOT_COUNT)
        return;
    if (amount > 127u)
        amount = 127u;
    scene_storeParameterByte(
        scene_index, &scene->settings.fx_send_morph[slot],
        (uint8_t)(AUTOSAVE_SCENE_PARAM_FX_SEND_MORPH_BASE + slot), amount);
}

uint8_t scene_getVoiceFxSendMorph(uint8_t scene_index, uint8_t slot)
{
    const scene_t *scene = scene_getConst(scene_index);

    /* Contract in SceneData.h: retained 0..127, or 0 when invalid. */
    if (!scene || slot >= INSTRUMENT_SLOT_COUNT ||
        scene->settings.fx_send_morph[slot] > 127u) {
        return 0u;
    }
    return scene->settings.fx_send_morph[slot];
}
```

### F-04 — `Core/Bank/Scene/SceneData.c` line 956, `scene_commitSettings()` — ADD after the `fx_send_amount` line

```c
        scene_setVoiceFxSendAmount(scene_index, i, image.fx_send_amount[i]);
        /* S075 F2-H: copy/clear carry both FX-send endpoints. */
        scene_setVoiceFxSendMorph(scene_index, i, image.fx_send_morph[i]);
```

Block above the function (line ~915), sentence "the before/after comparison
runs on a stack copy (sizeof(scene_settings_t), under 64 B)" stays true
(50 B).

### F-05 — `Core/Bank/Scene/SceneData.h` line 516–519, `scene_settingsDefaults()` contract — MODIFY text

```c
 * default audio route per slot (St1), FX send 0 (Normal and Morph
 * endpoints), fader pre, Morph amounts 0, Effect Morph 0, bus compressor
 * off/0/0/off).
```

`scene_settingsDefaults()` itself needs no code: its `memset()` zeroes
`fx_send_morph[]`.

### F-06 — `Core/Bank/Scene/SceneData.c` line 1042, `scene_initAll()` — ADD

```c
            scenes[scene_index].settings.fx_send_amount[track] = 0u;
            /* S075 F2-H: the Morph endpoint of a fresh Scene is also 0. */
            scenes[scene_index].settings.fx_send_morph[track] = 0u;
```

(Explicit for symmetry with the surrounding lines; the earlier `memset()`
already zeroes it.)

### F-07 — `Core/Bank/Scene/Autosave.h` line 191 and lines 245–253 — MODIFY

Line 191:

```c
#define AUTOSAVE_SCENE_PARAMETER_LIVE_BYTES    51u  /* S075 F2: +6 FX-send Morph */
```

Enum tail:

```c
    AUTOSAVE_SCENE_PARAM_BUS_COMP_BASE = 41,
    /*
     * S075 F2-H: Morph endpoint of the per-voice FX send, one cell per
     * instrument slot (45..50), in scene_settings_t.fx_send_morph[] order.
     * They occupy previously reserved cells: record layout, masks and format
     * version are unchanged. Getter: autosave_getSceneParameter(); restore:
     * autosave_applyScenePayload(); owner setter (marks the cell):
     * scene_setVoiceFxSendMorph().
     */
    AUTOSAVE_SCENE_PARAM_FX_SEND_MORPH_BASE = 45,
    AUTOSAVE_SCENE_PARAM_COUNT = 51
} autosave_scene_parameter_t;
```

The `Autosave.h` asserts at lines 368–372 (`COUNT == LIVE_BYTES`,
`COUNT <= ALLOC_BYTES`: 51 ≤ 118) hold.

### F-08 — `Core/Bank/Scene/Autosave.c` lines 97–100 (static asserts) — MODIFY

Replace the bus compressor group assert (which today ends the chain at
`AUTOSAVE_SCENE_PARAM_COUNT`) with:

```c
_Static_assert(AUTOSAVE_SCENE_PARAM_FX_SEND_MORPH_BASE -
                   AUTOSAVE_SCENE_PARAM_BUS_COMP_BASE ==
                   SCENE_BUS_COMP_FIELD_COUNT,
               "Scene bus compressor group must cover every field");
/* S075 F2-H: the FX-send Morph group closes the live Scene cells. */
_Static_assert(AUTOSAVE_SCENE_PARAM_COUNT -
                   AUTOSAVE_SCENE_PARAM_FX_SEND_MORPH_BASE ==
                   INSTRUMENT_SLOT_COUNT,
               "Scene FX-send Morph group must cover every instrument slot");
```

### F-09 — `Core/Bank/Scene/Autosave.c` lines 926–932, `autosave_getSceneParameter()` — MODIFY the final branch

```c
    } else if (parameter_index < AUTOSAVE_SCENE_PARAM_FX_SEND_MORPH_BASE) {
        /* Indices 41..44 are the S074 bus compressor settings (cmp..csc). */
        *value = scene->settings.bus_comp[
            parameter_index - AUTOSAVE_SCENE_PARAM_BUS_COMP_BASE];
    } else {
        /* Indices 45..50 are the FX-send Morph endpoints (S075 F2-H). */
        *value = scene->settings.fx_send_morph[
            parameter_index - AUTOSAVE_SCENE_PARAM_FX_SEND_MORPH_BASE];
    }
```

Block above the function (line ~885): add "45..50 FX-send Morph endpoints
(S075 F2-H)" to the index list.

### F-10 — `Core/Bank/Scene/Autosave.c` lines 1340–1430, `autosave_applyScenePayload()` — MODIFY

Block (replace "Reads the 45 live Scene-parameter bytes…" through "the S074
bus_comp[4] all updated"):

```c
 * What: the inverse of autosave_getSceneParameter(). Reads the 51 live
 * Scene-parameter bytes from the payload and writes them into
 * scene->settings through SceneData's change-aware setters (their dirty
 * notifications no-op while boot tracking is disabled). Inputs: scene_index
 * (0..15), pointer to the 1920-byte Scene section. Outputs: morph_amount,
 * voice_morph_amount[6], reserved cell 7, audio_out[6], fx_send_amount[6],
 * fader_setting[6], midi_channel[7], midi_note[7], effect_morph_amount,
 * the S074 bus_comp[4] and the S075 F2 fx_send_morph[6], all in
 * scene_get(scene_index)->settings.
```

Body: bound the bus compressor branch and add the Morph branch (the function
stays `void`):

```c
        } else if (parameter_index <
                   AUTOSAVE_SCENE_PARAM_FX_SEND_MORPH_BASE) {
            /* … existing bus compressor restore (41..44) unchanged … */
        } else {
            /* Indices 45..50: FX-send Morph endpoints (S075 F2-H). */
            scene_setVoiceFxSendMorph(
                scene_index,
                (uint8_t)(parameter_index -
                          AUTOSAVE_SCENE_PARAM_FX_SEND_MORPH_BASE),
                value);
        }
```

### F-11 — `Core/Hardware/SD/storageTypes.c` after the `fx_send_amount` branch (ends line 688) — ADD

```c
    } else if (storage_streq(key, "fx_send_morph")) {
        /*
         * Parse the per-voice FX-send Morph endpoints (S075 F2-H).
         *
         * Inputs: six comma-separated 0..127 values, one per instrument slot;
         * a value above 127 rejects the file, as for `fx_send_amount`.
         * Output: staged settings fx_send_morph[]; the mixer interpolates the
         * live send every block, so storage applies nothing at runtime. A
         * file without the key keeps the stage default 0
         * (filesystem_initSceneStage()). Writer: filesystem_nextScenesetLine()
         * line 14.
         */
        if (!target_settings)
            return STORAGE_STATUS_BAD_VALUE;
        return storage_parseCsvU8(value,
                                  target_settings->fx_send_morph,
                                  INSTRUMENT_SLOT_COUNT,
                                  127u);
```

`storage_sceneset_t` (`storageTypes.h`) and the `fx_send_amount` branch are
unchanged.

### F-12 — `Core/Hardware/SD/filesystem.c` `filesystem_nextScenesetLine()` lines 17284–17296 — ADD `case 14u` before `default:`

```c
    case 14u:
        /*
         * S075 F2-H: FX-send Morph endpoints, six values. Appended after the
         * bus compressor lines so no earlier writer line moves; the parser
         * is key-based. Parser: storageTypes.c `fx_send_morph`.
         */
        return filesystem_formatAssignmentCsvU8Line(
            dst, cap, "fx_send_morph", scene->settings.fx_send_morph,
            INSTRUMENT_SLOT_COUNT);
```

### F-13 — `Core/Hardware/SD/filesystem.c` line 16657 (`filesystem_initSceneStage()`) and line 28846 (boot empty Scene) — ADD

At each site, after the `fx_send_amount[slot] = 0u;` line:

```c
        stage->settings.fx_send_morph[slot] = 0u;   /* S075 F2-H */
```

```c
        scene->settings.fx_send_morph[slot] = 0u;   /* S075 F2-H */
```

and in the block of `filesystem_initSceneStage()` (line ~16648) replace
"staged Scene route, FX send, and fader mode bytes" with "staged Scene route
(St1), FX send Normal and Morph endpoints (0), and fader mode bytes".

### F-14 — `Core/Bank/Scene/Preset/presetMorphEngine.h` after line 119 — ADD

```c
/*
 * Read the resolved Morph amount of one voice (S075 F2-H).
 *
 * What: the amount the Morph worker interpolates a voice with: the step
 * override or retained base (presetMorph_getEffectiveVoiceAmount()), plus
 * the voice's active LFO contributions when it has an LFO layer and
 * scene_index is the active Scene. Read-only; it never changes SceneData,
 * AutoSave or worker state.
 * Why: the FX send follows the same Morph position as the voice's sound
 * parameters, LFO included, without adding a second copy of worker state.
 * Inputs: resident Scene index, zero-based slot. Output: 0..255 (0 for an
 * invalid Scene/slot).
 * Clients: preset_getEffectiveFxSendAmount() (mixer, every block).
 * Affiliates: presetMorph_effectiveVoiceBase(),
 * presetMorph_voiceHasLfoLayer(), presetMorph_resolveLfoAmount().
 */
uint8_t presetMorph_getResolvedVoiceAmount(uint8_t scene_index,
                                           uint8_t slot);
```

### F-15 — `Core/Bank/Scene/Preset/presetMorphEngine.c` after `presetMorph_getEffectiveVoiceAmount()` (ends line ~722) — ADD

```c
uint8_t presetMorph_getResolvedVoiceAmount(uint8_t scene_index,
                                           uint8_t slot)
{
    const scene_t *scene = scene_getConst(scene_index);

    /*
     * Contract in presetMorphEngine.h. The LFO contributions are runtime
     * state of the active Scene only, so another Scene resolves to its base.
     * The resolver is pure (it reads morph_lfo_contributions[] and the base);
     * it runs in the foreground mixer pass like the worker.
     */
    if (!scene || slot >= INSTRUMENT_SLOT_COUNT)
        return 0u;
    if (scene_index == scene_getActiveIndex() &&
        presetMorph_voiceHasLfoLayer(slot))
        return presetMorph_resolveLfoAmount(scene, slot);
    return presetMorph_effectiveVoiceBase(scene, slot);
}
```

### F-16 — `Core/Bank/Scene/Preset/presetManager.c` lines 1135–1150, `preset_getEffectiveFxSendAmount()` — MODIFY; ADD `preset_getFxSendDisplayAmount()` after it

```c
uint8_t preset_getEffectiveFxSendAmount(uint8_t scene_index, uint8_t slot)
{
    uint8_t normal;
    uint8_t morph;
    uint8_t amount;

    /*
     * Read one voice's effective (audible) FX-send amount.
     *
     * Inputs: resident Scene index and zero-based voice slot. Output: the
     * active step overlay amount when present; otherwise the Normal and
     * Morph endpoints interpolated by the voice's resolved Morph amount
     * (S075 F2-H): normal + (morph - normal) * amount / 255, rounded.
     * Equal endpoints (every Scene without a Morph send) return at once
     * without resolving the Morph amount. Caller: mixer_faderGains(), every
     * block (mixer.c line 526). Menu shows endpoints instead, through
     * preset_getFxSendDisplayAmount() / scene_getVoiceFxSendMorph().
     * Affiliates: presetMorph_getResolvedVoiceAmount(),
     * scene_getVoiceFxSendAmount(), scene_getVoiceFxSendMorph().
     */
    if (slot >= INSTRUMENT_SLOT_COUNT)
        return 0u;
    if (fx_send_step_override[slot].active)
        return fx_send_step_override[slot].amount;
    normal = scene_getVoiceFxSendAmount(scene_index, slot);
    morph = scene_getVoiceFxSendMorph(scene_index, slot);
    if (normal == morph)
        return normal;
    amount = presetMorph_getResolvedVoiceAmount(scene_index, slot);
    /* 127 * 255 + 127 fits uint16_t. */
    return (uint8_t)(((uint16_t)normal * (uint16_t)(255u - amount) +
                      (uint16_t)morph * amount + 127u) / 255u);
}

uint8_t preset_getFxSendDisplayAmount(uint8_t scene_index, uint8_t slot)
{
    /*
     * Value the VOICE mix FX-send cell shows in the Normal view (S075 F2-H).
     *
     * Output: the active step overlay while one plays (the live Scene
     * superpage follows automation, as before), otherwise the retained
     * Normal endpoint. Never the interpolated send: Menu shows endpoints,
     * like instrument cells. The Morph view reads
     * scene_getVoiceFxSendMorph() directly. Caller: menu.c
     * menu_cellDisplayValue().
     */
    if (slot >= INSTRUMENT_SLOT_COUNT)
        return 0u;
    if (fx_send_step_override[slot].active)
        return fx_send_step_override[slot].amount;
    return scene_getVoiceFxSendAmount(scene_index, slot);
}
```

### F-17 — `Core/Bank/Scene/Preset/presetManager.c` after `preset_setVoiceFxSendAmount()` (ends line ~1189) — ADD

```c
uint8_t preset_setVoiceFxSendMorph(uint8_t scene_index, uint8_t slot,
                                   uint8_t amount)
{
	/*
	 * Retain one Scene FX-send Morph endpoint (S075 F2-H).
	 *
	 * Inputs: resident Scene index, zero-based instrument slot, 0..127
	 * amount. Output: SceneData retains the value (change-aware AutoSave
	 * cell 45 + slot); returns 1 for a valid Scene/slot, 0 otherwise. No
	 * runtime push: the mixer interpolates on its next block. Clients: menu.c
	 * FX-send cell in the Morph view (fanned out over the VOICE edit mask),
	 * clearOps.c `clear send`.
	 */
    if (!scene_get(scene_index) || slot >= INSTRUMENT_SLOT_COUNT)
        return 0u;
    if (amount > 127u)
        amount = 127u;
    scene_setVoiceFxSendMorph(scene_index, slot, amount);
    return 1u;
}
```

### F-18 — `Core/Bank/Scene/Preset/presetManager.h` lines 395–428 — MODIFY blocks, ADD declarations

In the "Runtime-only Scene-setting step overlays" block, add after "…retained
SceneData.":

```c
 * preset_getEffectiveFxSendAmount() is the audible send: the step overlay,
 * else the Normal/Morph endpoints interpolated by the voice's resolved Morph
 * amount (S075 F2-H). preset_getFxSendDisplayAmount() is the Normal-view
 * cell value: the step overlay, else the Normal endpoint.
```

Declaration after `preset_getEffectiveFxSendAmount`:

```c
uint8_t preset_getFxSendDisplayAmount(uint8_t scene_index, uint8_t slot);
```

In the "Scene-owned per-voice mix setting setters" block, add "FX send has
two endpoints: preset_setVoiceFxSendAmount() (Normal) and
preset_setVoiceFxSendMorph() (Morph, S075 F2-H)." and the declaration after
`preset_setVoiceFxSendAmount`:

```c
uint8_t preset_setVoiceFxSendMorph(uint8_t scene_index, uint8_t slot,
                                   uint8_t amount);
```

### F-19 — `Core/DSPAudio/mixer.c` line 105 block — MODIFY text

Extend "Inputs: preset_getEffectiveFxSendAmount()" with "(the step overlay,
else the Normal/Morph send endpoints interpolated by the voice's resolved
Morph amount, S075 F2-H)". No code change in the mixer.

### F-20 — `Core/Menu/CopyClear/clearOps.c` lines 278–299, `ccClear_runSend()` — MODIFY

Block:

```c
/*
 * `clear send`: both FX-send endpoints 0 and fader `pre` on the track's
 * slot, fanned out over the destination Scene's edit mask (S075 F2-Q9: the
 * Morph endpoint is cleared too, so the send stays 0 at every Morph amount).
 */
```

Loop body:

```c
        (void)preset_setVoiceFxSendAmount(m, slot, 0u);
        (void)preset_setVoiceFxSendMorph(m, slot, 0u);
        (void)preset_setVoiceFaderSetting(m, slot, 0u);
```

`clear scene` / `clear scene settings` (via `scene_settingsDefaults()` +
`scene_commitSettings()`) and `copy scene` / `copy scene settings` (via
`scene_commitSettings()`) need no change: F-04 carries the field.

---

## 8. Stage G — Menu: show and edit the FX-send endpoints (F2-H)

### G-01 — `Core/Menu/menu.c` lines 3696–3716, `menu_cellDisplayValue()` Scene-setting branch — MODIFY block and FX-send case

Block (replace the first paragraph):

```c
        /*
         * Display Scene-owned VOICE mix settings.
         *
         * Inputs: active resident Scene, zero-based slot from the resolved
         * cell, and voiceModeShowMorph. Outputs: Morph, audio-out and
         * fader cells show the effective (step-automation or retained)
         * value as before. The FX-send cell shows an endpoint (S075 F2-H):
         * in the Morph view (SHIFT held, SHIFT+MODE VOICE latch, or SHIFT in
         * the Effect-page overlay) the retained Morph endpoint; otherwise
         * the step-automation value while one plays, else the Normal
         * endpoint. It never shows the interpolated send, like instrument
         * cells, which show their endpoint images.
         * menu_sceneLiveRefreshService() repaints this surface during
         * playback, so the Scene superpage follows the live runtime layer.
```

Case:

```c
        case MENU_SCENE_SETTING_FX_SEND_AMOUNT:
            return voiceModeShowMorph
                ? scene_getVoiceFxSendMorph(scene_index, cell->slot)
                : preset_getFxSendDisplayAmount(scene_index, cell->slot);
```

### G-02 — `Core/Menu/menu.c` lines 3820–3823, `menu_cellCommitValue()` Scene-setting loop — MODIFY

```c
            case MENU_SCENE_SETTING_FX_SEND_AMOUNT:
                /*
                 * S075 F2-H: the Morph view edits the Morph endpoint, the
                 * Normal view the Normal endpoint; both fan out over the
                 * VOICE edit mask like every Scene-setting cell.
                 */
                changed |= voiceModeShowMorph
                    ? preset_setVoiceFxSendMorph(scene_index, cell->slot,
                                                 (uint8_t)value)
                    : preset_setVoiceFxSendAmount(scene_index, cell->slot,
                                                  (uint8_t)value);
                break;
```

Other FX-send sites in `menu.c` need no change: the search bit
(`va_sceneSearchBitForCell()` line 1948), the automation target
(`menu_sceneSettingAutomationTarget()` line 3576), the short name
(line 3532) and the full-view label "FX Send" (line 10049). The Morph view
changes only the value resolution, as for instrument cells.

---

## 9. Stage H — VOICE mode: hold SHIFT for the Morph view (F2-G, F2-Q7)

### H-01 — `Core/Hardware/frontPanel/buttonHandler.c` lines 1392–1403, `processPress()` `case BUT_SHIFT` VOICE branch — MODIFY

```c
        case SELECT_MODE_VOICE:
            /*
             * Hold SHIFT in VOICE mode to show and edit Morph endpoints
             * (S075 F2-G).
             *
             * What: the current VOICE page resolves morphable cells, and the
             * FX-send cell (F2-H), against the Morph endpoint while SHIFT is
             * held, as the Effect page does. Release returns to the
             * SHIFT+MODE VOICE latch state (processRelease()), which is kept
             * (F2-Q7). SHIFT combinations keep working: SHIFT+TRACK mutes,
             * SHIFT+SELECT selects a bar, SHIFT+MODE toggles the latch,
             * SHIFT+COPY starts a clear; the page simply shows Morph values
             * meanwhile. Output: Menu's voiceModeShowMorph = 1 (pot mapping
             * refreshed, page repainted). No STEP overlay is entered.
             * Affiliates: menu_setVoiceModeShowMorph(),
             * buttonHandler_morphVoiceModeActive (latch).
             */
            menu_setVoiceModeShowMorph(1u);
            return;
```

### H-02 — same file, lines 1551–1561, `processRelease()` `case BUT_SHIFT` VOICE branch — MODIFY

```c
        case SELECT_MODE_VOICE:
            /*
             * End the momentary Morph view (S075 F2-G).
             *
             * Output: the VOICE page returns to the latch state
             * (buttonHandler_morphVoiceModeActive: SHIFT+MODE VOICE, F2-Q7),
             * and the selected voice LEDs and latch blink are restored. Do
             * not call buttonHandler_leaveSeqMode(): SHIFT does not enter the
             * STEP overlay in VOICE mode.
             */
            menu_setVoiceModeShowMorph(buttonHandler_morphVoiceModeActive);
            led_setActiveVoice(menu_getActiveVoice());
            if (buttonHandler_morphVoiceModeActive)
                led_setBlinkLed(LED_MODE1, 1u);
            break;
```

### H-03 — `Core/Menu/menu.c` lines 13800–13817, `menu_setVoiceModeShowMorph()` block — MODIFY text

```c
    /*
     * Set the voice-page Morph endpoint view.
     *
     * Why: buttonHandler owns the gestures, Menu owns the parameter buffer
     * used by repaint/edit code. Drivers: the SHIFT+MODE VOICE latch, SHIFT
     * held in VOICE mode (S075 F2-G, momentary; release restores the latch
     * state), and SHIFT held in the Effect-page voice mix overlay (F2-F).
     * Input onOff is boolean. Output: voiceModeShowMorph is updated and the
     * next repaint/edit resolves voice-page sound parameters and the FX-send
     * cell (F2-H) against the matching endpoint. Confederates: buttonHandler
     * also owns the MODE1 blink feedback for the latch.
     */
```

### H-04 — `Core/Menu/menu.h` line 56 block (voiceModeShowMorph) — MODIFY text

"Inputs/accessors: buttonHandler sets this through
menu_setVoiceModeShowMorph() (SHIFT+MODE VOICE latch, SHIFT held in VOICE,
SHIFT in the Effect-page voice mix overlay; S075 F2-F/F2-G)."

---

## 10. Stage I — Effect page: SHIFT+TRACK voice mix overlay (F2-F, F2-Q5, F2-Q6)

**Behaviour (decided).** In EFFECTS mode, SHIFT+TRACK *n*:

1. If the Effect type's TRACK hook takes SHIFT+TRACK, the type wins and
   nothing below happens (unchanged code order).
2. Otherwise the active track changes to *n* (as today: LED flash, optional
   stopped-transport preview) and the display shows VOICE *n*'s mix
   sub-page (SELECT 8) on its Scene-setting screen (`+`: out, FX send,
   fader, voice Morph), in the Morph view because SHIFT is held.
3. Releasing SHIFT shows the Normal view; pressing it again shows the Morph
   view. Pots and the encoder edit the shown cells with the normal edit-mask
   fan-out.
4. Releasing TRACK *n* restores the Effect page exactly: sub-page, screen,
   cursor, edit state; the Effect LEDs were kept (and stayed live) throughout.
5. A second SHIFT+TRACK *m* during the overlay moves it to voice *m*; the
   Effect page returns when every overlay TRACK is released.
6. SELECT functions assigned by the Effect type keep working while TRACK is
   held (with or without SHIFT) and their LEDs update; menu screen changes
   are suppressed until TRACK is released (I-10, user revision 2).

### I-01 — `Core/Menu/menu.c` after line 1211 (`menu_voiceSubPageScreen[]`) — ADD (+4 B, approved F2-Q5)

```c
/*
 * Effect-page voice mix overlay record (S075 F2-F; +4 B SRAM1, approved
 * F2-Q5).
 *
 * What: while SHIFT+TRACK holds the voice mix screen over the Effect page,
 * this keeps the Effect page position to restore on TRACK release: the
 * Effect menuIndex (sub-page and column) and edit mode, the VOICE mix
 * sub-page screen the overlay replaced, and the flags below. Why: the
 * overlay must not go through menu_switchPage(), which would run
 * menuEffects_leave() (clearing Effect LEDs, hold and Morph state) and
 * repaint Pattern LEDs; the user keeps the Effect LEDs (F2-Q6 d).
 * Lifetime: set by menu_fxVoiceMixOverlayBegin(); cleared by
 * menu_fxVoiceMixOverlayEnd() and by any menu_switchPage().
 * Accessors: menu_fxVoiceMixOverlayActive() (buttonHandler, ledHandler),
 * menu_serviceRuntimeWidgets() (latches Effect service actions).
 */
#define MENU_FX_OVERLAY_ACTIVE     0x01u  /* overlay is shown */
#define MENU_FX_OVERLAY_REPAIR     0x02u  /* menuEffects asked to re-seat the cursor */
#define MENU_FX_OVERLAY_EXIT_EDIT  0x04u  /* menuEffects abandoned the `typ` browse */
typedef struct {
    uint8_t flags;
    uint8_t fx_menu_index;
    uint8_t fx_edit_mode;
    uint8_t mix_screen;
} menu_fx_voice_mix_overlay_t;
static menu_fx_voice_mix_overlay_t menu_fxVoiceMixOverlay;
```

### I-02 — `Core/Menu/menu.h` after line 449 (`menu_effectShowHome`) — ADD

```c
/*
 * Effect-page voice mix overlay (S075 F2-F, user decisions F2-Q5, F2-Q6).
 *
 * What: SHIFT+TRACK on the Effect page shows that track's VOICE mix
 * Scene-setting screen (`+`: output, FX send, fader, voice Morph) for as
 * long as TRACK is held, then returns to the Effect page exactly as it was.
 *
 * menu_fxVoiceMixOverlayBegin(track): from the Effect page, saves the Effect
 * position (menuIndex, edit mode) and the VOICE mix screen memory, shows
 * VOICE `track` (0..6; track 7 = slot 6's settings) on its mix sub-page at
 * the first Scene-setting screen, in the Morph view when SHIFT is held, and
 * repaints. While the overlay is up, a call for another track moves it to
 * that track and keeps the saved Effect position. It does not change the
 * active track itself: the caller has already done that (SHIFT+TRACK
 * selects the track, F2-Q6 a). Output: 1 when the overlay is shown, 0 when
 * not on the Effect page (nothing changes).
 *
 * menu_fxVoiceMixOverlayEnd(): restores the Effect page position, applies
 * the Effect service actions latched during the overlay, re-asserts the
 * Effect Morph view from the physical SHIFT state, refreshes the pot mapping
 * and repaints. No-op when the overlay is not active (for example after a
 * page switch already ended it).
 *
 * menu_fxVoiceMixOverlayActive(): nonzero while the overlay is shown.
 *
 * SELECT: the Effect type's SELECT actions keep working while TRACK is held;
 * menu screen changes are suppressed (default SELECT navigation is skipped
 * and menu_effectShowHome() only re-renders the SELECT LEDs).
 *
 * LEDs: the Effect LEDs stay and stay live (menuEffects_service() keeps
 * running; ledHandler keeps Pattern feedback off the SEQ row).
 * Callers: buttonHandler.c (TRACK press/release, SHIFT edges, SELECT navigation gate,
 * event-ring overflow), ledHandler.c (SEQ-row gates). Affiliates:
 * menu_setVoiceModeShowMorph(), menuEffects_service(), menu_switchPage().
 */
uint8_t menu_fxVoiceMixOverlayBegin(uint8_t track);
void menu_fxVoiceMixOverlayEnd(void);
uint8_t menu_fxVoiceMixOverlayActive(void);
```

### I-03 — `Core/Menu/menu.c` after `menu_effectShowHome()` (lines 13846–13863) — ADD

```c
/*
 * Effect-page voice mix overlay (contract in menu.h, S075 F2-F).
 *
 * Begin swaps menu_activePage/menuIndex directly instead of calling
 * menu_switchPage(): the Effect page stays "entered" (no menuEffects_leave(),
 * no Pattern LED repaint), so its LEDs and type state survive. VOICE-page
 * services see a normal VOICE page: the automation-presence search and the
 * marker overlay are restarted for it, and the track-scoped
 * parameter_values are loaded as menu_switchPage() does for a VOICE page.
 */
uint8_t menu_fxVoiceMixOverlayBegin(uint8_t track)
{
    uint8_t screen;

    if (track >= NUM_TRACKS)
        return 0u;
    if ((menu_fxVoiceMixOverlay.flags & MENU_FX_OVERLAY_ACTIVE) == 0u) {
        if (menu_activePage != EFFECT_PAGE)
            return 0u;
        menu_fxVoiceMixOverlay.flags = MENU_FX_OVERLAY_ACTIVE;
        menu_fxVoiceMixOverlay.fx_menu_index = menuIndex;
        menu_fxVoiceMixOverlay.fx_edit_mode = editModeActive;
        menu_fxVoiceMixOverlay.mix_screen =
            menu_voiceSubPageScreen[MENU_VOICE_MIX_SUBPAGE];
    } else if (!menu_isVoicePage(menu_activePage)) {
        return 0u;
    }
    lockPotentiometerFetch();
    editModeActive = 0u;
    menu_activePage = (uint8_t)(VOICE1_PAGE + track);
    va_resetOverlay();
    va_searchRestart();
    pat_applyTrackSettingsToMenu(menu_shownPattern, track);
    /* First screen after the instrument's own mix screens is `+`. */
    screen = menu_voiceInstrumentScreenCount(MENU_VOICE_MIX_SUBPAGE);
    menu_voiceSubPageScreen[MENU_VOICE_MIX_SUBPAGE] = screen;
    menuIndex = (uint8_t)((MENU_VOICE_MIX_SUBPAGE << PAGE_SHIFT) |
                          menu_voiceFirstSelectableColumn(
                              MENU_VOICE_MIX_SUBPAGE, screen));
    /* F2-Q6 c: SHIFT held = Morph view (direct write; one repaint below). */
    voiceModeShowMorph = (uint8_t)(buttonHandler_getShift() != 0u);
    menu_endlessPotMappingChanged();
    menu_repaintAll();
    return 1u;
}

void menu_fxVoiceMixOverlayEnd(void)
{
    const uint8_t flags = menu_fxVoiceMixOverlay.flags;

    if ((flags & MENU_FX_OVERLAY_ACTIVE) == 0u)
        return;
    menu_fxVoiceMixOverlay.flags = 0u;
    lockPotentiometerFetch();
    va_resetOverlay();
    /* The Effect page never uses the voice Morph view (menu_switchPage()). */
    voiceModeShowMorph = 0u;
    menu_voiceSubPageScreen[MENU_VOICE_MIX_SUBPAGE] =
        menu_fxVoiceMixOverlay.mix_screen;
    menu_activePage = EFFECT_PAGE;
    menuIndex = menu_fxVoiceMixOverlay.fx_menu_index;
    editModeActive = (flags & MENU_FX_OVERLAY_EXIT_EDIT)
        ? 0u : menu_fxVoiceMixOverlay.fx_edit_mode;
    va_searchRestart();
    if (flags & MENU_FX_OVERLAY_REPAIR)
        menu_resetActiveParameter();
    menuEffects_setShowMorph(buttonHandler_getShift());
    menu_endlessPotMappingChanged();
    menu_repaintAll();
}

uint8_t menu_fxVoiceMixOverlayActive(void)
{
    return (uint8_t)(menu_fxVoiceMixOverlay.flags & MENU_FX_OVERLAY_ACTIVE);
}
```

Notes for the implementer:

- `menu_voiceFirstSelectableColumn()` and `menu_voiceInstrumentScreenCount()`
  are static in `menu.c` with forward declarations at lines 1660/1788;
  place the new functions after both definitions (line 13863 is after them).
- `menu_shownPattern`, `lockPotentiometerFetch()`,
  `pat_applyTrackSettingsToMenu()` are already used by `menu_switchPage()`
  in the same file.
- `va_searchRestart()` selects its geometry from `menu_activePage`, so it is
  called after the page is set in both directions.

### I-04 — `Core/Menu/menu.c` line 12952, `menu_switchPage()` — ADD after `menu_stepAutomationReset();`

```c
    /*
     * A real page switch ends the Effect-page voice mix overlay without
     * restoring the Effect page (S075 F2-F): the user chose a new page
     * while TRACK was held. The overlay never ran menuEffects_leave(), so
     * run it here when the destination is not the Effect page; entering the
     * Effect page again runs menuEffects_enter() below (old page is a VOICE
     * page). The later TRACK release finds the overlay inactive (no-op).
     */
    if (menu_fxVoiceMixOverlay.flags & MENU_FX_OVERLAY_ACTIVE) {
        menu_fxVoiceMixOverlay.flags = 0u;
        if (pageNr != EFFECT_PAGE)
            menuEffects_leave();
    }
```

Placement: after the busy-guard return (a deferred page switch must not end
the overlay early) and before `if (was_voice_page && …) va_resetOverlay();`.

### I-05 — `Core/Menu/menu.c` lines 11779–11784 and 11785, `menu_serviceRuntimeWidgets()` — MODIFY

```c
    if (menu_isVoicePage(menu_activePage)) {
        /*
         * In the Effect-page voice mix overlay (S075 F2-F) SEQ holds belong
         * to the Effect page's lock editor, so the VOICE held-step view is
         * not polled; the automation search and underlines run as on any
         * VOICE page.
         */
        if (!menu_fxVoiceMixOverlayActive())
            va_updateHeldState();
        va_scanService();
        va_underlineService();
    }

    if (menu_fxVoiceMixOverlayActive()) {
        /*
         * Keep the Effect page alive under the overlay (S075 F2-F, F2-Q6 d).
         *
         * What: menuEffects_service() still draws the FX SEQ row, its hold
         * flashes and type-owned SELECT LEDs, and still tracks Scene/type
         * changes. Its menu actions refer to the Effect page, which is not
         * shown: REPAIR and EXIT_EDIT are latched in the overlay record and
         * applied by menu_fxVoiceMixOverlayEnd(); repaint bits are dropped
         * (the VOICE page repaints itself). Inputs: the overlay record.
         * Output: live Effect LEDs, latched actions.
         */
        const uint8_t fx_actions = menuEffects_service();

        if (fx_actions & MENU_FX_ACT_REPAIR)
            menu_fxVoiceMixOverlay.flags |= MENU_FX_OVERLAY_REPAIR;
        if (fx_actions & MENU_FX_ACT_EXIT_EDIT)
            menu_fxVoiceMixOverlay.flags |= MENU_FX_OVERLAY_EXIT_EDIT;
    }

    if (menu_activePage == EFFECT_PAGE) {
        /* … unchanged … */
```

### I-06 — `Core/Hardware/frontPanel/ledHandler.c` lines 1187, 1251–1252, 1436 — MODIFY

Each test of `menu_activePage == EFFECT_PAGE` / `!= EFFECT_PAGE` also covers
the overlay:

```c
    /*
     * The Effect page owns the FX-sequencer chase layer, also while the
     * SHIFT+TRACK voice mix overlay is shown over it (S075 F2-F: the Effect
     * LEDs stay). Pattern chase must neither draw nor clear that layer.
     */
    if (menu_activePage == EFFECT_PAGE || menu_fxVoiceMixOverlayActive())
        return;
```

```c
    if (menu_activePage == PERFORMANCE_PAGE ||
        menu_activePage == EFFECT_PAGE ||
        menu_fxVoiceMixOverlayActive())     /* S075 F2-F: Effect LEDs stay */
        return;
```

```c
        /* Effect page's SEQ row is not a Pattern view (Session 072 step 7),
         * also under the S075 F2-F voice mix overlay. */
        if (menu_activePage != EFFECT_PAGE &&
            !menu_fxVoiceMixOverlayActive()) {
```

`ledHandler.c` already includes `menu.h` (line 44).

### I-07 — `Core/Hardware/frontPanel/buttonHandler.c` after line 204 (`buttonHandler_loadSceneSeqPressedMask`) — ADD (+1 B, approved F2-Q5)

```c
/*
 * TRACK buttons holding the Effect-page voice mix overlay (S075 F2-F; +1 B
 * SRAM1, approved F2-Q5).
 *
 * What: bit n is set when SHIFT+TRACK n opened (or moved) the overlay, so
 * its release is consumed and, when the last bit clears, the Effect page is
 * restored. Why: the release edge reaches processRelease() later than the
 * scan; a mask keeps press and release paired even when SHIFT was released
 * first (F2-Q6 c: the overlay stays while TRACK alone is held). Cleared by
 * the event-ring overflow block. Accessors: handleVoiceButton(),
 * processRelease(), buttonHandler_processEvents().
 */
static uint8_t buttonHandler_fxVoiceMixTrackMask = 0u;
```

### I-08 — `buttonHandler.c` lines 1161–1168, `handleVoiceButton()` FX branch — MODIFY

```c
        if (bh_state.selectButtonMode == SELECT_MODE_FX) {
            /*
             * SHIFT+TRACK on the Effect page (reached only with SHIFT held:
             * FX inverts muteModeActive, and the type's TRACK hook above
             * already had priority, F2-Q6 b).
             *
             * What: selects the active track without leaving FX mode, as
             * before (F2-Q6 a), then shows that track's VOICE mix `+` screen
             * while TRACK is held (S075 F2-F). Output: active voice, mute/
             * flash LEDs, overlay shown and the TRACK bit recorded for its
             * release. The stopped-transport preview of a re-pressed
             * selected voice is kept.
             * Affiliates: menu_fxVoiceMixOverlayBegin(),
             * buttonHandler_fxVoiceMixTrackMask, processRelease().
             */
            menu_setActiveVoice(voiceNr);
            buttonHandler_showMuteLEDs();
            led_flashLed((uint8_t)(LED_VOICE1 + voiceNr));
            if (menu_fxVoiceMixOverlayBegin(voiceNr))
                buttonHandler_fxVoiceMixTrackMask = (uint8_t)(
                    buttonHandler_fxVoiceMixTrackMask |
                    (uint8_t)(1u << voiceNr));
            if (shouldPreviewVoice)
                seq_previewVoice(voiceNr);
            return;
        }
```

### I-09 — `buttonHandler.c` line 1470, `processRelease()` — ADD before `if (copyClear_buttonReleased(buttonNr))`

```c
    {
        /*
         * Release of a TRACK that holds the Effect-page voice mix overlay
         * (S075 F2-F). Checked before the copy/clear router so a session
         * started during the overlay cannot swallow the release and leave
         * the overlay up. The last overlay TRACK released restores the
         * Effect page; the edge is consumed in every case.
         */
        int8_t voice = btn_to_voice(buttonNr);

        if (voice >= 0) {
            const uint8_t bit = (uint8_t)(1u << (uint8_t)voice);

            if ((buttonHandler_fxVoiceMixTrackMask & bit) != 0u) {
                buttonHandler_fxVoiceMixTrackMask = (uint8_t)(
                    buttonHandler_fxVoiceMixTrackMask & (uint8_t)~bit);
                if (buttonHandler_fxVoiceMixTrackMask == 0u)
                    menu_fxVoiceMixOverlayEnd();
                return;
            }
        }
    }
```

### I-10 — SELECT under the overlay: type actions run, screen changes are suppressed (user, revision 2)

**Rule:** SELECT functions assigned by the Effect type keep working while
TRACK holds the overlay (with or without SHIFT); any menu screen change is
suppressed until TRACK is released. Two changes:

**I-10a — `Core/Hardware/frontPanel/buttonHandler.c` lines 978–997, `handleSelectButton()` `case SELECT_MODE_FX` (no SHIFT) — MODIFY**

Add to the existing block, after "…which is the active SELECT LED for other
types.":

```c
         * S075 F2-F: under the SHIFT+TRACK voice mix overlay the type's
         * SELECT action still runs (a "show home" request then only
         * re-renders the SELECT LEDs, see menu_effectShowHome()), but the
         * default navigation below is skipped: it changes a menu screen,
         * and screens do not change while TRACK is held.
```

Body:

```c
        const uint8_t fx_action =
            menuEffects_hookSelect(selectNr, 0u, 1u);

        if (fx_action != 0u) {
            if (fx_action == EFFECT_UI_SHOW_HOME)
                menu_effectShowHome();
            break;
        }
        if (menu_fxVoiceMixOverlayActive())
            break;
        menu_switchSubPage(selectNr);
        menuEffects_renderSelectLeds(menu_getSubPage());
        menu_repaintAll();
        break; }
```

The SHIFT+SELECT branch (lines 946–955) is unchanged: it runs the type hook
and calls `menu_effectShowHome()` on `EFFECT_UI_SHOW_HOME`, which I-10b
handles. `buttonHandler_partButtonReleased()` needs no change (it only
clears the step timer).

**I-10b — `Core/Menu/menu.c` lines 13846–13863, `menu_effectShowHome()` — ADD at the top of the body; MODIFY its block**

Block, append:

```c
 * S075 F2-F: under the SHIFT+TRACK voice mix overlay the type has already
 * acted on the SELECT press; the screen move and repaint are suppressed
 * (screens do not change while TRACK is held), and only the SELECT LEDs are
 * re-rendered for the Effect sub-page that will return, so a type that owns
 * them (CrumpBit data lines) shows its new state at once. The Effect page
 * comes back at its saved position on TRACK release.
```

Body, first statements:

```c
    if (menu_fxVoiceMixOverlay.flags & MENU_FX_OVERLAY_ACTIVE) {
        menuEffects_renderSelectLeds((uint8_t)(
            (menu_fxVoiceMixOverlay.fx_menu_index & MASK_PAGE) >>
            PAGE_SHIFT));
        return;
    }
```

`menu_fxVoiceMixOverlay` (I-01) is defined at line ~1212, above this
function.

### I-11 — `buttonHandler.c` lines 1405–1408, `processPress()` `case BUT_SHIFT` FX branch — MODIFY

```c
        case SELECT_MODE_FX:
            /*
             * Holding SHIFT displays/edits Morph endpoints on the FX page;
             * under the SHIFT+TRACK voice mix overlay it shows the voice
             * Morph view instead (S075 F2-Q6 c). The Effect flag is still
             * set so it is current when the Effect page returns.
             */
            menu_setEffectShowMorph(1u);
            if (menu_fxVoiceMixOverlayActive())
                menu_setVoiceModeShowMorph(1u);
            break;
```

### I-12 — `buttonHandler.c` lines 1571–1575, `processRelease()` `case BUT_SHIFT` FX branch — MODIFY

```c
        case SELECT_MODE_FX:
            /*
             * End the momentary Morph view while keeping FX mute LEDs. Under
             * the voice mix overlay the voice page returns to its Normal
             * view and the overlay stays while TRACK is held (S075 F2-Q6 c).
             */
            menu_setEffectShowMorph(0u);
            if (menu_fxVoiceMixOverlayActive())
                menu_setVoiceModeShowMorph(0u);
            buttonHandler_showMuteLEDs();
            return;
```

### I-13 — `buttonHandler.c` line 1628, `buttonHandler_processEvents()` overflow block — ADD after `buttonHandler_loadSceneSeqPressedMask = 0u;`

```c
        /*
         * S075 F2-F: a lost TRACK release must not leave the voice mix
         * overlay up. End it and forget its TRACK bits; a later release of
         * that TRACK then takes the normal path (no action in FX mode).
         */
        buttonHandler_fxVoiceMixTrackMask = 0u;
        menu_fxVoiceMixOverlayEnd();
```

Update the overflow paragraph of the function block ("clears both pairing
masks") to "clears the pairing masks (including the S075 F2-F overlay TRACK
mask, ending the overlay)".

---

## 11. Stage J — tools

### J-01 — `tools/populate_scene_directory.py`

A-04 (default routes), and the generated `sceneset.scg` gains the writer's
new line after `fader_setting`, matching `filesystem_nextScenesetLine()`
(lines 79–90):

```python
            "fx_send_amount=0,0,0,0,0,0",
            "fader_setting=0,0,0,0,0,0",
            # S075 F2-H: Morph endpoint of the per-voice FX send.
            "fx_send_morph=0,0,0,0,0,0",
```

### J-02 — `tools/verify_bank_autosave.py` — MODIFY (user: correct after the implementation)

The validator reads the HCPR A/B records and compares them with the Bank
tree. Read against the firmware on 2026-10-03, four parts are stale and one
is incomplete; after J-02 every check matches the F2 firmware.

| # | Line | Today | Firmware (anchor) | Change |
|---|---|---|---|---|
| J-02a | 18–28 constants | no format constant; `data[4] != 1` in `valid_record()` (line ~139) | `AUTOSAVE_HEADER_FORMAT_VERSION` 3 (`Autosave.h` line 48) | add `FORMAT_VERSION = 3` and compare `data[4] != FORMAT_VERSION` |
| J-02b | 166 `len(rows) != 129` | 129 rows | 161 rows (`AUTOSAVE_HCNAMES_ROW_COUNT`; rows 129..144 Pattern, 145..160 Effect) | add `HCNAMES_ROW_COUNT = 161`, compare against it |
| J-02c | `parse_hcnames()` lines 64–86 | every row ≥ 33 must carry a type column | only Instrument rows 33..128 carry the type; Pattern/Effect rows use `name<TAB>source[<TAB>R]` (FILESYSTEM_SPEC "Root resident-name register") | add `INSTRUMENT_ROW_FIRST = 33`, `INSTRUMENT_ROW_END = 129`; the two-field check and `type_text` apply only inside that range |
| J-02d | 352 `record[scene_base + 8:scene_base + 48]` | parameters at Scene byte 8, 40 cells | parameters at Scene byte 10 (`AUTOSAVE_SCENE_PARAMETERS_OFFSET`; bytes 8..9 are the HCNAMES source), 51 live cells (F-07) | add `SCENE_PARAMETERS_OFFSET = 10`, `SCENE_PARAM_COUNT = 51`; slice `record[scene_base + SCENE_PARAMETERS_OFFSET: … + SCENE_PARAM_COUNT]` and compare all 51 |
| J-02e | `parse_scene_values()` lines 110–131 | 40 cells; every key required | 51 cells; only `format`/`version` are required, other keys keep the Scene Load stage defaults (`filesystem_initSceneStage()`) | rewrite as below |

J-02e, replacement for `parse_scene_values()`:

```python
MIDI_DEFAULT_TRIGGER_NOTE = 63            # MidiNoteNumbers.h
BUS_COMP_MAX = (2, 127, 127, 6)           # scene_busCompMax[] (SceneData.c)
BUS_COMP_KEYS = ("bus_comp_mode", "bus_comp_amount",
                 "bus_comp_time", "bus_comp_sidechain")


def parse_scene_values(path: Path) -> list[int]:
    """Expected AutoSave Scene cells 0..50 for one sceneset.scg.

    Mirrors the firmware after S075 F2: a missing optional key keeps the
    Scene Load stage default (filesystem_initSceneStage(): Morph 0, routes
    St1, sends 0, fader pre, MIDI channel track + 1, note 63, Effect Morph 0,
    bus compressor off/0/0/off, FX-send Morph 0). Bus compressor values are
    clamped like scene_busCompClamp(); cell 7 is the reserved constant 127.
    """
    values = parse_assignments(path)
    result = [0] * SCENE_PARAM_COUNT

    def list_values(key: str, count: int, default: list[int]) -> list[int]:
        if key not in values:
            return default
        parsed = [int(item.strip(), 0)
                  for item in values[key].split(",") if item.strip()]
        if len(parsed) != count:
            raise ValueError(f"{path}: {key} expected {count} values")
        return parsed

    result[0] = int(values.get("morph_amount", "0"), 0)
    result[1:7] = list_values("voice_morph_amount", 6, [0] * 6)
    # S075: Scene cell 7 (former `srt`) is reserved and always written as 127.
    result[7] = 127
    result[8:14] = list_values("audio_out", 6, [0] * 6)
    result[14:20] = list_values("fx_send_amount", 6, [0] * 6)
    result[20:26] = list_values("fader_setting", 6, [0] * 6)
    result[26:33] = list_values("midi_channel", 7, list(range(1, 8)))
    result[33:40] = list_values("midi_note", 7,
                                [MIDI_DEFAULT_TRIGGER_NOTE] * 7)
    result[40] = int(values.get("effect_morph_amount", "0"), 0)
    for field, key in enumerate(BUS_COMP_KEYS):
        result[41 + field] = min(int(values.get(key, "0"), 0),
                                 BUS_COMP_MAX[field])
    # S075 F2-H: FX-send Morph endpoints, cells 45..50.
    result[45:51] = list_values("fx_send_morph", 6, [0] * 6)
    return result
```

J-02d, the comparison (lines 351–355):

```python
            values = parse_scene_values(child / "sceneset.scg")
            params = scene_base + SCENE_PARAMETERS_OFFSET
            actual = list(record[params:params + SCENE_PARAM_COUNT])
            if actual != values:
                add_error(errors, f"{winner_name} Scene {scene:02d} settings "
                                 f"payload does not match sceneset.scg")
```

Checked and unchanged (they match the firmware): record size 34,768,
Bank offset 3920, Scene offset 4048 and size 1920, Kit at Scene + 640,
Instruments at Kit + 128 + 192 × slot (type 3 B, name at +3), the header
magic/commit/generation offsets, and the CRC32C rule (CRC field zeroed,
probe byte included). Module docstring: add "Offsets mirror Autosave.h as of
S075 F2 (format 3, 161 HCNAMES rows, 51 Scene cells)."

## 12. Stage K — documentation

| File | Anchor | Change |
|---|---|---|
| `knowledge_files/specification_reference/FILESYSTEM_SPEC.md` | line ~951 writer table | add row `14 \| fx_send_morph \| 6 × 0..127 \| S075 F2; a file without it loads 0` |
| same | line ~971 | bus compressor defaults "(`off`, 0, 0, `off`; S075 F2)" |
| same | lines ~183, ~936, ~1137 | Scene settings list: add `fx_send_morph[6]` (Morph endpoint of the FX send) |
| same | `audio_out` default text | missing `audio_out` = St1 for every voice (S075 F2) |
| `knowledge_files/specification_reference/AUTOSAVE.md` | lines 196–210 Scene index table | "currently 51 live"; row `45..50 \| per-voice FX-send Morph endpoint (S075 F2)`; reserved row becomes `51..117` |
| same | lines 277–284 append rule | add: "S075 F2 appended the FX-send Morph cells 45..50; records written before F2 are not migrated (they are deleted with the F2 update)." |
| `knowledge_files/specification_reference/BANK_PRESET_ARCHITECTURE.md` | line 169 FX send row | "Per-voice Normal and Morph send endpoints (0..127) \| 12 bytes; the mixer reads the step override, else the endpoints interpolated by the voice's resolved Morph amount, each block (S075 F2)" |
| same | line 171 | bus compressor defaults off/0/0/off |
| same | line ~403 FX Send | describe the interpolation and the display rule (0.7) |
| same | Scene settings defaults text | St1 for every voice |
| `knowledge_files/specification_reference/STORAGE_SRAM_MANIFEST.md` | after line ~303 | "S075 F2 (approved): `scenes` +96 B (`scene_settings_t` 44 → 50 B for `fx_send_morph[6]`; `scene_t` 1624 → 1630 B, alignment 2); menu.c overlay record +4 B; buttonHandler overlay TRACK mask +1 B." |
| `knowledge_files/specification_reference/dsp_instruments_effects/EFFECTS_BUS_REFERENCE.md` | lines 118–119 | balance law centred on 63: `gL = pan<=63 ? 1 : (127-pan)/64`, `gR = pan>=63 ? 1 : pan/63` |
| same | lines 200, 523 | `syn` → `snc` |
| same | lines 556–558 "Pan display quirk" | replace with "Pan rule (S075 F2): stored 0..127, 63 = centre = `0`, display value − 63 for every pan; Effect pan defaults 63; stereo balance centred on 63; mono laws unchanged. Effects saved at 64 show `1`." |
| same | CrumpBit row/default table | mix 0, fbk 64, rte 64, dpn 63 |
| `knowledge_files/specification_reference/dsp_instruments_effects/EFFECTS_MIXER_DSP_REFERENCE.md` | lines 194–195, 349 | same law change (Effect return and CrumpBit delay pan) |
| `SCOPING_TARGETS.md` | line ~1938 "Effect pan display" | mark resolved by S075 F2 (default 63, balance centre 63) |
| `S075_PH6_COPY_CLEAR_FULL_SPEC.md` | line 460 (§8.1 example) | `Copy    03T2s005        Clear   S03T2` |
| same | `clear send` text | "both FX-send endpoints 0 and fader `pre`" |
| `tools/verify_bank_autosave.py` docstring | — | see J-02 |
| `S075_PH6_COPYCLEAR_IMPLEMENTATION.md` | §14 | add "§14.8 F2 (2026-10-xx)" pointing to this schedule and its §15 |

---

## 13. Build and verification

### 13.1 Build gates

1. `make -j` clean, no new warnings (watch `menuEffects_formatValue3`,
   `record` in `menuEffects_paintEditView()`, unused `slot` parameters).
2. `arm-none-eabi-nm -S build/lxr02.elf | grep -w scenes` → size **0x65E0**
   (was 0x6580). Any other value: stop and report (RAM approval is +96 B).
3. `.bss` growth ≈ +101 B (plus at most a few bytes of LTO placement);
   record the before/after `arm-none-eabi-size` lines in §15.
4. Static asserts: Autosave Scene groups (F-08), `COUNT == LIVE_BYTES`,
   `COUNT <= ALLOC_BYTES`, filesystem stage cache.
5. Before the first boot of the new firmware the user deletes the AutoSave
   records (`.hcprms1`, `.hcprms2`) and other temporary records (user,
   revision 2); no old record is read by F2 firmware.
6. `python3 tools/verify_bank_autosave.py <card> <bank>` on a card written
   by the F2 firmware after a drain: PASS.

### 13.2 Hardware

| # | Case | Expected |
|---|---|---|
| 1 | `clear scene` on another Scene; `clear scene settings` | all six voices route St1; compressor off / 0 / 0 / off; FX send Normal and Morph 0 |
| 2 | Boot with an empty Scene slot | St1, compressor off/0/0/off |
| 3 | Copy and Clear menus | top-left `Copy` / `Clear`; indicator from the 9th column |
| 4 | Effect `typ` full view, browse `Off`, `StFilter`, `CrumpBit` | long name only, nothing else on row 1; encoder click commits |
| 5 | Effect `run`, `scl` full views | long name only |
| 6 | New Effect / `clear fx` | `pan` shows 0; sound centred |
| 7 | Choose CrumpBit | mix 0, fbk 64, rte 64, dpn 0 (stored 63); sync label `snc` |
| 8 | Effect pan full left / right | stored 0 shows -63 (right silent); 127 shows 64 (left silent); same for `dpn` |
| 9 | Instrument pan | unchanged display and default |
| 10 | VOICE page, hold SHIFT | Morph endpoints shown and edited (instrument cells and FX send); release → Normal, or Morph if the SHIFT+MODE VOICE latch is on |
| 11 | VOICE: SHIFT+TRACK, SHIFT+SELECT, SHIFT+COPY while SHIFT held | mute, bar select and clear still work |
| 12 | FX send Morph: voice 1 Normal 0, Morph 127; sweep voice 1 Morph | send follows the sweep; with an LFO on voice 1 Morph the send moves with it |
| 13 | Step automation `1fx` on a step | that value overrides the send while the step plays; the Normal view shows it live |
| 14 | Edit mask with three Scenes, edit FX send in the Morph view | all three Scenes get the Morph endpoint |
| 15 | `clear send` | both endpoints 0 in every masked Scene; fader `pre` |
| 16 | Save Scene, reload; power cycle | both endpoints restored; `sceneset.scg` has `fx_send_morph=` |
| 17 | Effect page, SHIFT+TRACK 3 held | type hook first (no current type uses SHIFT+TRACK); active track = 3; VOICE 3 `+` screen in the Morph view; Effect LEDs unchanged and the FX chase still runs |
| 18 | …release SHIFT, keep TRACK | same screen, Normal view; pots edit out/send/fader/Morph |
| 19 | …release TRACK | same Effect sub-page, screen, cursor and full-view state as before |
| 20 | Overlay, press SHIFT+TRACK 5 too | overlay moves to voice 5; Effect page returns after both TRACKs are released |
| 21 | Overlay, press a MODE button | the new mode's page; TRACK release does nothing more |
| 22 | CrumpBit, overlay up, press SELECT (with and without SHIFT) | the data-line action happens and the SELECT LEDs show it; the `+` screen stays; after TRACK release the Effect page is at its saved screen, not the home screen |
| 23 | StFilter (no SELECT hook), overlay up, press SELECT | nothing happens; the screen stays |
| 24 | Overlay during a Scene switch / Effect type change | on TRACK release the Effect page returns with a valid cursor (latched REPAIR) |

---

## 14. Risks and notes

- **Pan of saved Effects (0.2).** Effects saved at 64 on the card show `1`
  and are 0.14 dB down on the left; re-set them to 0 if wanted.
- **Overlay and copy/clear.** COPY or CLEAR pressed during the overlay opens
  the copy/clear menu over the VOICE page as on any VOICE page. TRACK release
  is consumed by the overlay first (I-09).
- **Automation recording in the Morph view.** Turning the FX-send pot while
  recording into held steps (`va_writeAutomationFromKnob()`) records the
  turned value as `Nfx`, as for other Scene-setting cells; unchanged by F2.
- **LFO race.** The LFO contributions are byte fields read by the
  foreground mixer pass; the worst case is one block using the previous
  contribution, as for the Morph worker.

---

## 15. Work log

(To be appended during implementation: per-stage status, build sizes,
deviations, hardware results.)

### 2026-10-03 — Stages A–E applied

- Applied A-01..A-04: fresh/staged/invalid Scene voice routes now default to
  St1 (stored route 0), and the fixture generator uses the same fallback.
- Applied B-01..B-02: bus compressor defaults are now off/0/0/off in the
  shared Scene defaults and their adjacent contracts/comments.
- Applied C-01..C-02: copy/clear row 0 now reads `Copy`/`Clear`; the source
  indicator remains anchored at column 8.
- Applied D-01..D-05: full Effect views safely space-pad short names and show
  only the long name for named TYPE/RUN/SCALE values; stale numeric overlays
  and the TYPE browse mark were removed.
- Applied E-01..E-05: common/Effect and CrumpBit pan defaults use stored 63,
  both stereo balance laws are centred on 63, CrumpBit defaults are mix 0 /
  feedback 64 / rate 64 / pan 63, and the sync label is `snc`.
- No build run yet; Stage F follows.

### 2026-10-03 — Stages F–H core paths applied

- Applied F-01..F-13: `scene_settings_t` now owns
  `fx_send_morph[6]`; SceneData setters/getters, defaults, copy/clear commit,
  Scene Load staging, AutoSave cells 45..50, sceneset parsing/writing, and
  the fixture writer all carry the endpoint. No compatibility marker or
  migration path was added, per revision 2.
- Applied F-14..F-20: the read-only resolved-Morph bridge includes active
  Scene LFO contributions; the mixer getter interpolates Normal/Morph send
  endpoints with step overrides taking priority; Menu-facing display/edit
  accessors separate endpoint display from audible interpolation; `clear send`
  clears both endpoints and fader mode.
- Applied G-01..G-02: VOICE mix cells display/edit the endpoint selected by
  `voiceModeShowMorph` while retaining edit-mask fan-out.
- Applied H-01..H-04: holding SHIFT in VOICE shows Morph and release restores
  the persistent SHIFT+MODE latch state.
- Overlay stages I-01..I-13 and tooling/docs remain.

### 2026-10-03 — Stages I–K applied

- Applied I-01..I-13: SHIFT+TRACK now owns the approved +4 B Effect-page
  voice-mix overlay record; Effect page state is swapped directly and restored
  on the last TRACK release. Effect service, SELECT hooks/LEDs, SEQ/BAR
  behavior, chase/record/follow ownership, SHIFT Morph selection, real-mode
  abandonment, and lost-release cleanup are all wired.
- Applied J-01..J-02: generated Scene fixtures now write
  `fx_send_morph=0,0,0,0,0,0`; the AutoSave validator matches format 3,
  161 HCNAMES rows, Scene parameters at byte 10, all 51 live cells, optional
  Scene keys, reserved cell 7 = 127, MIDI note 63, and bus-compressor
  clamping.
- Applied K-01..K-08: filesystem, AutoSave, Bank/Preset, SRAM, DSP, full
  spec, scoping, memory, and implementation-log references now describe the
  F2 contracts. `S075_PH6_COPYCLEAR_IMPLEMENTATION.md` has the corresponding
  §14.8 pointer.

### 2026-10-03 — First F2 build and layout gate

- `make all` passed with the existing unrelated warnings only. DEV link:
  `text=531,936`, `data=416`, `bss=426,712`; payload `532,352` B; ITCM
  `4,168` B; DTCM statics `4,472` B; FXBUF `126,592` B at
  `0x20001180`, margin `3,712` B.
- `arm-none-eabi-nm -S build/lxr02.elf | grep -w scenes` reports
  `20051e28 000065e0 B scenes`, the required `0x65E0` size. The new symbols
  also show the approved overlay record and TRACK mask; SRAM growth is within
  the approved +101 B ledger.
- `git diff --check` passed. Hardware cases in §13.2 remain pending; the
  user must delete old AutoSave/temporary records before first F2 boot.

### 2026-10-03 — Scripted verification

- `PYTHONPYCACHEPREFIX=/private/tmp/helicase-pycache python3 -m py_compile
  tools/verify_bank_autosave.py tools/populate_scene_directory.py` passed.
  `make -C tools/dsp_test mixer special_tags` passed
  (`mixer differing samples: 0`; `special tags OK (155 rows, 0 mismatches)`).
- `make all && make img` passed again after the final source/comment pass,
  with only the pre-existing unused-filesystem, packed-member, libc-nano, and
  LTO-serial warnings. Final DEV measurements remain
  `text=531,936`, `data=416`, `bss=426,712`, raw payload
  `532,352` B, stamped image `532,368` B, and `scenes=0x65E0`.
- The checked-in `SD_CARD/` fixture is not a coherent post-F2-drain Bank
  snapshot: the validator reaches the new format/offset checks but reports
  pre-existing HCNAMES/active-scene/edit-mask mismatches. Therefore the
  required post-F2-drain validator PASS and all hardware cases stay pending;
  no fixture or AutoSave record was modified.

### 2026-10-03 — Final consistency pass

- Clarified the SRAM manifest's historical F1 decimation note so it does not
  read as a current F2 struct-size claim.
- Re-ran `git diff --check`, both Python syntax checks, and the mixer/special-
  tag DSP harness; all passed. The final build and image measurements above
  are unchanged.

### 2026-10-03 — Overlay follow-up: last TRACK pressed, no mutes while held (user)

- Rule: after SHIFT+TRACK opens the voice mix overlay on the Effect page,
  every TRACK press (with or without SHIFT) joins the held set and switches
  the Scene-settings screen to the track pressed last (the active track
  follows). The overlay stays while at least one TRACK is held. No TRACK
  press mutes until every held TRACK is released and the overlay is gone.
- `buttonHandler.c` `handleVoiceButton()`: new branch after the Effect type's
  TRACK hook and before the mute path. While `buttonHandler_fxVoiceMixTrackMask`
  is nonzero the press sets its bit (so `processRelease()` consumes its
  release); in FX mode it selects the voice, flashes its LED and calls
  `menu_fxVoiceMixOverlayBegin(voiceNr)` (which already moves an active
  overlay). In any other mode (MODE pressed while TRACKs were held) the press
  is consumed with no action, so it cannot mute or clear mutes. The mask's
  block comment is updated. No RAM change.
- `make all`: clean apart from the existing newlib/LTO warnings;
  `text=532,096` (+160), `data=416`, `bss=426,712` (unchanged).
- Hardware: pending (user re-test).
