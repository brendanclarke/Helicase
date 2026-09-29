# S074 — Fourth fader mode `xfd` (dry ↔ FX crossfade): implementation schedule

**Status:** implementation complete in source; hardware acceptance pending.
The complete single-stage XFD path is applied and build/review verified.

**Base:** the working tree of 2026-09-29: HEAD `060923e` plus the
**in-progress** bus compressor changes (18 files modified). The line numbers
below are from that tree. Six of the files here are also being edited for the
compressor:

- `SceneData.h/.c`
- `presetManager.c`
- `storageTypes.c`
- `menu.c`
- `mixer.c`

Every change therefore also quotes its **anchor text**. If a compressor edit
has moved a line, find it by the anchor. Within each file, apply the changes
from the highest line down (§3).

---

## 1. Behaviour

**Request (user, 2026-09-29):** a fourth fader mode after `pre`, `pst` and
`fx`, labelled `xfd`. It crossfades between sending to the FX bus (fader at
the bottom) and sending to the voice's normal output (fader at the top):

- **Fader at the bottom:** the voice goes to the FX bus as `fx` mode does
  with its fader at maximum: `send × 1`, set by the voice's FX send and not
  by its volume. **Nothing** goes to the normal output, whatever the volume.
- **Moving the fader up** (the "normal direction"): it is an ordinary volume
  fader for the dry output. At the top the voice reaches its output at its
  normal volume, and **nothing** goes to the FX bus.
- **The send uses the inverted log scale:** moving the fader *down* in `xfd`
  raises the send exactly as moving it *up* does in `fx` mode.

### 1.1 The rule, in the mixer's terms

`mixer.h` describes each mode by two factors: `mix = s × vol × F_mix` and
`send = s × send × F_send`, where `s` is the decimated voice signal before
volume.

| Mode | `F_mix` | `F_send` |
|---|---|---|
| `pre` (0) | fader | fader |
| `pst` (1) | fader | 1 |
| `fx` (2) | 1 | fader |
| **`xfd` (3)** | **fader** | **fader′** |

- `fader` = `slider_vol[slot]` = `taper(x)`, where `x` is the
  deadzone-normalized slider position and `taper()` is the 30 dB curve that
  `adcPots.c` bakes into its lookup table.
- `fader′` = `taper(1 − x)`: the same curve, read from the other end.

### 1.2 The mirrored gain without a new lookup table or RAM

`slider_raw_to_float()` builds `g = (R − m)/(1 − m)` with
`R = 10^((x − 1)·D/20)` and `m = 10^(−D/20)` (`D` = `SLIDER_LOG_TAPER_DB`).
Solving for the other end gives one closed form:

```
P  = m + g·(1 − m)                       (= R)
g′ = taper(1 − x) = m·(1 − P) / (P·(1 − m))
```

- It needs one division and no logarithm.
- **Measured in a scratch compile with this toolchain:** `powf(10, −D/20)`
  folds to a constant at `-O2` and `-Ofast`, leaving one `vdiv.f32`.
- **Checked numerically:** it matches `taper(1 − x)` to 5.6 × 10⁻¹⁶ over the
  whole range.
- A linear taper (`D ≤ 0`) gives `g′ = 1 − g`.
- Guards make the endpoints exact: `g ≤ 0 → 1` and `g ≥ 1 → 0`. With the
  deadzones, the bottom sends the full send and no dry signal, and the top
  sends exactly nothing to the FX bus.

### 1.3 What it sounds like (D = 30 dB)

| Fader position `x` | Dry (× volume) | Send (× FX send) |
|---:|---:|---:|
| 0 (bottom) | 0 (silent) | 1.0 (0 dB) |
| 0.25 | −27.0 dB | −7.9 dB |
| 0.5 | −16.4 dB | −16.4 dB |
| 0.75 | −7.9 dB | −27.0 dB |
| 1 (top) | 1.0 (0 dB) | 0 (silent) |

**Note: the middle dips.** Both taps sit at −16.4 dB at the fader's center,
because each follows the log taper, as you specified. A constant-power
crossfade would put both at about −3 dB there. If you would rather have that,
it is a one-line change of the `xfd` branch. This schedule implements the
behaviour as described.

### 1.4 Everything else stays as it is

- **The send tap stays before volume and the dry tap stays after it,** as in
  every mode.
- **The FX send step override still applies,** through
  `preset_getEffectiveFxSendAmount()`.
- **Click-free:** mode changes and fader moves ramp through the existing
  per-block gain ramps (`mixer_voice_last_gain[]`, `mixer_send_last_gain[]`).
- **Not automatable,** like the other fader modes
  (`menu_sceneSettingAutomationTarget()` returns invalid for fader cells).
- **Storage is unchanged:** it is the same byte, `fader_setting[slot]`, now
  0..3.
  - AutoSave needs no change; its reader goes through the clamping setter.
  - `sceneset.scg` keeps the same key, with a new allowed value.
- **CPU:**
  - one division per `xfd` slot per block;
  - otherwise it costs the same as the `fx` mode it mirrors: when an Effect
    is active, the one-pass dry+send path runs at every fader position except
    the very top, where the send is exactly 0;
  - the worst case is unchanged.
- **RAM:** 0 B.

---

## 2. Change list

| ID | File | Line(s) now | Anchor | Action |
|---|---|---|---|---|
| D1 | `Core/Hardware/frontPanel/IO/adcPots.h` | after L73 | `extern float slider_vol[ADC_POT_COUNT];` | ADD declaration |
| D2 | `Core/Hardware/frontPanel/IO/adcPots.c` | after L147 | end of `adc_getPotValue()` | ADD function |
| X1 | `Core/DSPAudio/mixer.h` | L75–93 | the "Per-voice fader modes" comment and `#define MIXER_FADER_FX    2u` | REPLACE |
| X2 | `Core/DSPAudio/mixer.c` | L505–532 | `mixer_faderGains()` and its comment | REPLACE |
| S1 | `Core/Bank/Scene/SceneData.h` | L189–192 | "Fader mode is 0..2, currently interpreted" | MODIFY comment |
| S2 | `Core/Bank/Scene/SceneData.h` | after L435 | `uint8_t scene_getVoiceFxSendAmount(uint8_t scene_index, uint8_t slot);` | ADD constant |
| S3 | `Core/Bank/Scene/SceneData.c` | L517–529 | `scene_setVoiceFaderSetting()`: "Store one future per-voice fader mode." … `mode = 2u;` | MODIFY |
| S4 | `Core/Bank/Scene/SceneData.c` | L539–546 | `scene_getVoiceFaderSetting()`: "retained 0..2 mode" … `> 2u) {` | MODIFY |
| P1 | `Core/Bank/Scene/Preset/presetManager.c` | L1211–1222 | `preset_setVoiceFaderSetting()` comment and `mode = 2u;` | MODIFY |
| P2 | `Core/Bank/Scene/Preset/presetManager.h` | L417–418 | "FX send and fader mode deliberately have no runtime output" | MODIFY comment (stale) |
| F1 | `Core/Hardware/SD/storageTypes.c` | L690–702 | the `"fader_setting"` parse branch | MODIFY |
| M1 | `Core/Menu/menu.c` | L4436–4443 | `menu_clampCellValue()`: "fader mode uses 0..2" … `*value = 2u;` | MODIFY |
| M2 | `Core/Menu/menu.c` | L3513–3525 | `menu_sceneSettingFaderName()` body | MODIFY |

- **Unchanged, checked:**
  - AutoSave (`Autosave.c` getter and reader; the reader calls the clamping
    setter);
  - the Scene writer (`filesystem.c`, `fader_setting` line: it writes the
    stored byte);
  - the Menu dtype (`DTYPE_0B15` for the fader cell; the clamp is M1);
  - the full-view and compact display (both call
    `menu_sceneSettingFaderName()`);
  - `tools/verify_bank_autosave.py` (no domain check);
  - `tools/dsp_test` (it extracts other mixer functions, not
    `mixer_faderGains()`).
- **Leave alone:** `tools/dsp_test/frozen/`, the frozen reference copy.

---

## 3. Order within each file

| File | Order |
|---|---|
| `menu.c` | M1 (4436–4443), then M2 (3513–3525) |
| `SceneData.c` | S4 (539–546), then S3 (517–529) |
| `SceneData.h` | S2 (after 435), then S1 (189–192) |
| `presetManager.h` | P2 |
| `presetManager.c` | P1 |
| `mixer.h` | X1 |
| `mixer.c` | X2 |
| `adcPots.h` | D1 |
| `adcPots.c` | D2 |
| `storageTypes.c` | F1 |

It is one stage and builds as a unit. D1/D2 and S2 must be present for X2 to
compile.

---

## 4. Changes

### D1 — `Core/Hardware/frontPanel/IO/adcPots.h` after L73 — ADD

Anchor (L71–73):

```c
/* Slider volume as float [0.0, 1.0], index 0=RV5..5=RV10.
** Updated by adc_checkPots(). */
extern float slider_vol[ADC_POT_COUNT];
```

Insert after L73:

```c

/*
 * Slider gain at the mirrored fader position (S074, the xfd fader mode).
 *
 * What:     for a slider_vol[] gain g = taper(x), returns taper(1 - x)
 *           exactly, where x is the deadzone-normalized fader position and
 *           taper() is the SLIDER_LOG_TAPER_DB curve the LUT is built from.
 * How:      with m = 10^(-D/20) and P = m + g(1 - m), the mirrored gain is
 *           m(1 - P) / (P(1 - m)): one division, no logarithm; m folds at
 *           compile time. A linear taper (D <= 0) returns 1 - g.
 * Inputs:   g in [0, 1] (a slider_vol[] value).
 * Outputs:  [0, 1]; exactly 1 for g <= 0 and exactly 0 for g >= 1, so the
 *           fader's end deadzones give exact endpoints.
 * Why here: adcPots owns the taper (slider_raw_to_float()), so the inverse
 *           lives beside it and follows any change to SLIDER_LOG_TAPER_DB.
 * Callers:  mixer_faderGains() (xfd), once per xfd slot per block.
 */
float adc_sliderGainMirrored(float gain);
```

---

### D2 — `Core/Hardware/frontPanel/IO/adcPots.c` after L147 — ADD

Anchor (L143–147):

```c
uint8_t adc_getPotValue(uint8_t i)
{
    if (i >= ADC_POT_COUNT) return 0;
    return (uint8_t)(slider_vol[i] * 100.0f);
}
```

Insert after L147. `adcPots.c` already includes `config.h` and `<math.h>`
(L46–47).

```c

float adc_sliderGainMirrored(float gain)
{
    /*
     * Mirror a tapered slider gain (contract in adcPots.h).
     *
     * Derivation: slider_raw_to_float() gives g = (R - m) / (1 - m) with
     * R = 10^((x - 1)D/20), so R = P = m + g(1 - m). At 1 - x the raw gain is
     * 10^(-xD/20) = m / P, and the same normalization gives
     * (m/P - m) / (1 - m) = m(1 - P) / (P(1 - m)). The endpoint guards make
     * the ends exact regardless of float rounding, so an xfd voice at the top
     * of its fader sends exactly nothing and leaves the send path idle.
     */
    if (gain <= 0.0f)
        return 1.0f;
    if (gain >= 1.0f)
        return 0.0f;
    if (SLIDER_LOG_TAPER_DB <= 0.0f)
        return 1.0f - gain;
    {
        const float m = powf(10.0f, -SLIDER_LOG_TAPER_DB / 20.0f);
        const float p = m + gain * (1.0f - m);

        return (m / (1.0f - m)) * (1.0f - p) / p;
    }
}
```

`adcPots.c` builds at `-O2` (it is in `SRCS`). A scratch compile with the
project's toolchain showed `powf(10, -30/20)` folded to a constant at `-O2`;
no `powf` call remains.

---

### X1 — `Core/DSPAudio/mixer.h` L75–93 — REPLACE

Current:

```c
/*
 * Per-voice fader modes (Scene setting fader_setting[slot], 0..2; plan §8.3,
 * user rule A24). Values match the stored byte and the Menu labels
 * (menu_sceneSettingFaderName: pre / pst / fx).
 *
 * The voice signal s (decimated, pre-volume) feeds two parallel taps:
 *   mix  = s x vol x F_mix  -> pan -> voice route
 *   send = s x send x F_send -> FX bus
 * PRE : F_mix = fader, F_send = fader  (fader scales both)
 * POST: F_mix = fader, F_send = 1      (send ignores the fader)
 * FX  : F_mix = 1,     F_send = fader  (fader scales only the send; mix level
 *                                       is vol alone)
 * The fader never changes the stored FX Send parameter. Consumer:
 * mixer_calcNextSampleBlock(). Affiliates: SceneData fader_setting,
 * presetManager fader/send setters, Menu VOICE mix cells.
 */
#define MIXER_FADER_PRE   0u
#define MIXER_FADER_POST  1u
#define MIXER_FADER_FX    2u
```

Replace with:

```c
/*
 * Per-voice fader modes (Scene setting fader_setting[slot], 0..3; plan §8.3,
 * user rule A24; xfd added in S074). Values match the stored byte and the
 * Menu labels (menu_sceneSettingFaderName: pre / pst / fx / xfd).
 *
 * The voice signal s (decimated, pre-volume) feeds two parallel taps:
 *   mix  = s x vol x F_mix  -> pan -> voice route
 *   send = s x send x F_send -> FX bus
 * PRE : F_mix = fader, F_send = fader  (fader scales both)
 * POST: F_mix = fader, F_send = 1      (send ignores the fader)
 * FX  : F_mix = 1,     F_send = fader  (fader scales only the send; mix level
 *                                       is vol alone)
 * XFD : F_mix = fader, F_send = fader' (crossfade; fader' is the slider gain
 *                                       at the mirrored position,
 *                                       adc_sliderGainMirrored(): bottom =
 *                                       send only at full send, top = dry
 *                                       only at vol)
 * The fader never changes the stored FX Send parameter. Consumer:
 * mixer_calcNextSampleBlock(). Affiliates: SceneData fader_setting and
 * SCENE_FADER_SETTING_MAX (asserted equal to MIXER_FADER_XFD in mixer.c),
 * presetManager fader/send setters, Menu VOICE mix cells.
 */
#define MIXER_FADER_PRE   0u
#define MIXER_FADER_POST  1u
#define MIXER_FADER_FX    2u
#define MIXER_FADER_XFD   3u
```

---

### X2 — `Core/DSPAudio/mixer.c` L505–532 — REPLACE

Anchor: the comment "Resolve the parallel dry-mix and FX-send gains for one
voice slot." and the whole of `mixer_faderGains()`. Current:

```c
/*
 * Resolve the parallel dry-mix and FX-send gains for one voice slot.
 *
 * Inputs: zero-based Scene slot, active Scene index, and the retained or
 * step-overridden fader/send values. Output: mix_gain feeds the normal routed
 * output and send_gain feeds the pre-volume FX bus. PRE scales both taps,
 * POST scales only the dry mix, and FX scales only the send. The voice volume
 * remains on the dry tap for all modes. Affiliate: mixer_calcNextSampleBlock().
 */
static void mixer_faderGains(uint8_t slot,
		uint8_t scene_index,
		float *mix_gain,
		float *send_gain)
{
	const float fader = slider_vol[slot];
	const float volume = instrumentManager_runtimeVolume(slot);
	const float send = (float)preset_getEffectiveFxSendAmount(scene_index, slot)
			/ 127.0f;
	const uint8_t mode = scene_getVoiceFaderSetting(scene_index, slot);

	*mix_gain = volume * fader;
	*send_gain = send * fader;
	if (mode == MIXER_FADER_POST) {
		*send_gain = send;
	} else if (mode == MIXER_FADER_FX) {
		*mix_gain = volume;
	}
}
```

Replace with:

```c
/*
 * Resolve the parallel dry-mix and FX-send gains for one voice slot.
 *
 * Inputs: zero-based Scene slot, active Scene index, and the retained or
 * step-overridden fader/send values. Output: mix_gain feeds the normal routed
 * output and send_gain feeds the pre-volume FX bus. PRE scales both taps,
 * POST scales only the dry mix, FX scales only the send, and XFD (S074)
 * crossfades: the dry tap follows the fader and the send follows the mirrored
 * fader. The voice volume remains on the dry tap for all modes. Affiliate:
 * mixer_calcNextSampleBlock().
 */
/* The Scene domain limit and the last mixer mode must agree (S074). */
_Static_assert(MIXER_FADER_XFD == SCENE_FADER_SETTING_MAX,
		"fader modes: SceneData domain and mixer modes differ");
static void mixer_faderGains(uint8_t slot,
		uint8_t scene_index,
		float *mix_gain,
		float *send_gain)
{
	const float fader = slider_vol[slot];
	const float volume = instrumentManager_runtimeVolume(slot);
	const float send = (float)preset_getEffectiveFxSendAmount(scene_index, slot)
			/ 127.0f;
	const uint8_t mode = scene_getVoiceFaderSetting(scene_index, slot);

	*mix_gain = volume * fader;
	*send_gain = send * fader;
	if (mode == MIXER_FADER_POST) {
		*send_gain = send;
	} else if (mode == MIXER_FADER_FX) {
		*mix_gain = volume;
	} else if (mode == MIXER_FADER_XFD) {
		/*
		 * xfd: crossfade between the FX bus (fader down) and the voice's
		 * normal output (fader up) (S074).
		 *
		 * The dry tap keeps the default volume x fader: at the bottom nothing
		 * reaches the output whatever the volume, and at the top the voice is
		 * at its normal level. The send uses the slider gain at the mirrored
		 * position: at the bottom it equals FX mode at full fader (send x 1,
		 * no volume, as in FX mode); moving the fader down raises it exactly
		 * as moving it up does in FX mode; at the top it is exactly 0. Both
		 * taps keep their existing per-block ramps, so fader moves and mode
		 * changes are click-free. Cost: one division per xfd slot per block.
		 * Affiliates: adc_sliderGainMirrored(), MIXER_FADER_XFD.
		 */
		*send_gain = send * adc_sliderGainMirrored(fader);
	}
}
```

`mixer.c` already includes `adcPots.h` (L60) and `SceneData.h` (L61).

---

### S2 — `Core/Bank/Scene/SceneData.h` after L435 — ADD

Anchor (L433–438):

```c
void scene_setVoiceFxSendAmount(uint8_t scene_index, uint8_t slot,
                                uint8_t amount);
uint8_t scene_getVoiceFxSendAmount(uint8_t scene_index, uint8_t slot);
void scene_setVoiceFaderSetting(uint8_t scene_index, uint8_t slot,
                                uint8_t mode);
uint8_t scene_getVoiceFaderSetting(uint8_t scene_index, uint8_t slot);
```

Insert after L435, directly above the fader accessors:

```c
/*
 * Largest stored fader mode (S074): 0 pre, 1 pst, 2 fx, 3 xfd.
 *
 * What: the single domain limit for fader_setting[]. The SceneData setter
 * and getter, Preset's setter, the sceneset.scg parser and the Menu clamp
 * all use it. mixer.c asserts that it equals MIXER_FADER_XFD, so SceneData
 * stays free of mixer includes while the two cannot drift. A future mode
 * raises this value and adds a mixer branch and a Menu label.
 */
#define SCENE_FADER_SETTING_MAX 3u
```

---

### S1 — `Core/Bank/Scene/SceneData.h` L189–192 — MODIFY (comment only)

Current:

```c
     * fx_send_amount and fader_setting are retained now for the Scene file/UI
     * contract. FX send is 0..127. Fader mode is 0..2, currently interpreted
     * as normal/pre-FX, post-FX, and FX-only by the mixer FX path. Preset
     * setters store the values and the live mixer applies the selected mode.
```

Replace with:

```c
     * fx_send_amount and fader_setting are retained now for the Scene file/UI
     * contract. FX send is 0..127. Fader mode is 0..SCENE_FADER_SETTING_MAX
     * (0..3): pre (normal/pre-FX), pst (post-FX), fx (FX-only) and xfd (dry
     * to FX crossfade, S074), interpreted by the mixer FX path. Preset
     * setters store the values and the live mixer applies the selected mode.
```

---

### S4 — `Core/Bank/Scene/SceneData.c` L539–546 — MODIFY

Anchor: `scene_getVoiceFaderSetting()`. Current:

```c
    /*
     * Read one retained fader mode.
     *
     * Inputs: resident Scene index and zero-based instrument slot. Output:
     * retained 0..2 mode, or 0 for invalid coordinates/stale storage.
     */
    if (!scene || slot >= INSTRUMENT_SLOT_COUNT ||
        scene->settings.fader_setting[slot] > 2u) {
```

Replace with:

```c
    /*
     * Read one retained fader mode.
     *
     * Inputs: resident Scene index and zero-based instrument slot. Output:
     * retained 0..SCENE_FADER_SETTING_MAX mode (0..3, S074 adds xfd), or 0
     * for invalid coordinates/stale storage.
     */
    if (!scene || slot >= INSTRUMENT_SLOT_COUNT ||
        scene->settings.fader_setting[slot] > SCENE_FADER_SETTING_MAX) {
```

---

### S3 — `Core/Bank/Scene/SceneData.c` L517–529 — MODIFY

Anchor: `scene_setVoiceFaderSetting()`. Current:

```c
    /*
     * Store one future per-voice fader mode.
     *
     * Inputs: resident Scene index, zero-based instrument slot, and mode in
     * the current Scene file domain: 0 normal/pre-FX, 1 post-FX, 2 FX-only.
     * Output: a changed retained mode is stored before its named Scene bit is
     * marked. Runtime behavior is applied by Preset and the mixer rather than
     * being hidden in SceneData.
     */
    if (!scene || slot >= INSTRUMENT_SLOT_COUNT)
        return;
    if (mode > 2u)
        mode = 2u;
```

Replace with:

```c
    /*
     * Store one per-voice fader mode.
     *
     * Inputs: resident Scene index, zero-based instrument slot, and mode in
     * the current Scene file domain: 0 normal/pre-FX, 1 post-FX, 2 FX-only,
     * 3 xfd dry/FX crossfade (S074); larger values clamp to
     * SCENE_FADER_SETTING_MAX. Output: a changed retained mode is stored
     * before its named Scene bit is marked. Runtime behavior is applied by
     * Preset and the mixer rather than being hidden in SceneData. AutoSave
     * restore also enters here, so a restored byte is always in domain.
     */
    if (!scene || slot >= INSTRUMENT_SLOT_COUNT)
        return;
    if (mode > SCENE_FADER_SETTING_MAX)
        mode = SCENE_FADER_SETTING_MAX;
```

---

### P1 — `Core/Bank/Scene/Preset/presetManager.c` L1211–1222 — MODIFY

Anchor: `preset_setVoiceFaderSetting()`. Current:

```c
	/*
	 * Retain one Scene fader mode for the block-rate mixer consumer.
	 *
	 * Inputs: resident Scene index, zero-based instrument slot, and 0..2 mode.
	 * Output: SceneData retains the mode; the active mixer pulls it on its next
	 * block and applies PRE/POST/FX topology beside the voice send tap. Menu and
	 * storage callers remain independent of mixer internals.
     */
    if (!scene_get(scene_index) || slot >= INSTRUMENT_SLOT_COUNT)
        return 0u;
    if (mode > 2u)
        mode = 2u;
```

Replace with:

```c
	/*
	 * Retain one Scene fader mode for the block-rate mixer consumer.
	 *
	 * Inputs: resident Scene index, zero-based instrument slot, and a
	 * 0..SCENE_FADER_SETTING_MAX mode (0..3). Output: SceneData retains the
	 * mode; the active mixer pulls it on its next block and applies the
	 * PRE/POST/FX/XFD topology beside the voice send tap (XFD, S074: the
	 * fader crossfades from the FX send at the bottom to the dry output at
	 * the top). Menu and storage callers remain independent of mixer
	 * internals.
     */
    if (!scene_get(scene_index) || slot >= INSTRUMENT_SLOT_COUNT)
        return 0u;
    if (mode > SCENE_FADER_SETTING_MAX)
        mode = SCENE_FADER_SETTING_MAX;
```

---

### P2 — `Core/Bank/Scene/Preset/presetManager.h` L417–418 — MODIFY (comment only)

This sentence has been wrong since Session 072 Step 5: the mixer now pulls
the FX send and fader mode every block. Current:

```c
 * applies the active Scene's mixer route immediately. FX send and fader mode
 * deliberately have no runtime output until the FX/fader backend exists.
```

Replace with:

```c
 * applies the active Scene's mixer route immediately. FX send and fader mode
 * (pre/pst/fx/xfd) have no push step: the mixer reads both every block.
```

---

### F1 — `Core/Hardware/SD/storageTypes.c` L690–702 — MODIFY

Anchor: `} else if (storage_streq(key, "fader_setting")) {` (L689). Current:

```c
        /*
         * Parse retained per-voice fader modes.
         *
         * Inputs: six comma-separated values in the 0..2 domain
         * (mixer.h MIXER_FADER_*). Output: staged Scene settings; the mixer
         * reads the mode every block (Session 072 Step 5).
         */
        if (!target_settings)
            return STORAGE_STATUS_BAD_VALUE;
        st = storage_parseCsvU8(value,
                                target_settings->fader_setting,
                                INSTRUMENT_SLOT_COUNT,
                                2u);
```

Replace with:

```c
        /*
         * Parse retained per-voice fader modes.
         *
         * Inputs: six comma-separated values in the
         * 0..SCENE_FADER_SETTING_MAX domain (0..3; mixer.h MIXER_FADER_*,
         * 3 = xfd since S074). Output: staged Scene settings; the mixer reads
         * the mode every block (Session 072 Step 5). A value above the
         * domain rejects the file, as before. Firmware older than S074
         * therefore rejects a Scene that uses xfd (downgrade only).
         */
        if (!target_settings)
            return STORAGE_STATUS_BAD_VALUE;
        st = storage_parseCsvU8(value,
                                target_settings->fader_setting,
                                INSTRUMENT_SLOT_COUNT,
                                SCENE_FADER_SETTING_MAX);
```

---

### M1 — `Core/Menu/menu.c` L4436–4443 — MODIFY

Anchor: `menu_clampCellValue()`, the Scene-setting block. Current:

```c
         * audio_out uses the six-entry mixer route menu, FX send uses 0..127,
         * fader mode uses 0..2, and Voice Morph uses its full 0..255 domain.
         */
        if (cell->scene_setting == MENU_SCENE_SETTING_AUDIO_OUT) {
            if (*value > 5u)
                *value = 5u;
        } else if (cell->scene_setting == MENU_SCENE_SETTING_FADER_SETTING) {
            if (*value > 2u)
                *value = 2u;
```

Replace with:

```c
         * audio_out uses the six-entry mixer route menu, FX send uses 0..127,
         * fader mode uses 0..SCENE_FADER_SETTING_MAX (pre/pst/fx/xfd, S074),
         * and Voice Morph uses its full 0..255 domain.
         */
        if (cell->scene_setting == MENU_SCENE_SETTING_AUDIO_OUT) {
            if (*value > 5u)
                *value = 5u;
        } else if (cell->scene_setting == MENU_SCENE_SETTING_FADER_SETTING) {
            if (*value > SCENE_FADER_SETTING_MAX)
                *value = SCENE_FADER_SETTING_MAX;
```

---

### M2 — `Core/Menu/menu.c` L3513–3525 — MODIFY

Anchor: the body of `static void menu_sceneSettingFaderName(uint8_t value, char *dst)`
(L3511). Current:

```c
    /*
     * Format the retained fader mode domain.
     *
     * Inputs: stored 0..2 fader mode. Output: compact user text. These labels
     * are live mixer behavior: pre = fader before both dry and FX taps, pst =
     * fader on the dry/post-FX mix only, and fx = fader on the FX send only.
     */
    if (value == 1u)
        memcpy(dst, "pst", 3);
    else if (value == 2u)
        memcpy(dst, "fx ", 3);
    else
        memcpy(dst, "pre", 3);
```

Replace with:

```c
    /*
     * Format the retained fader mode domain.
     *
     * Inputs: stored 0..3 fader mode. Output: compact user text. These labels
     * are live mixer behavior: pre = fader before both dry and FX taps, pst =
     * fader on the dry/post-FX mix only, fx = fader on the FX send only, and
     * xfd (S074) = fader crossfades from the FX send (bottom) to the dry
     * output (top). Used by both the compact row and the full edit view.
     */
    if (value == 1u)
        memcpy(dst, "pst", 3);
    else if (value == 2u)
        memcpy(dst, "fx ", 3);
    else if (value == 3u)
        memcpy(dst, "xfd", 3);
    else
        memcpy(dst, "pre", 3);
```

The fader cell's `DTYPE_0B15` (`menu_cellDtype()`) is unchanged. M1 clamps it
to 3, and the encoder and endless pot walk `pre → pst → fx → xfd`.

---

## 5. Verification

### 5.1 Build

- `make all` and `make img` pass with no new warnings. The new
  `_Static_assert` in `mixer.c` holds.
- `link_budget.py` shows no RAM change and about +100 B of flash.
- Optional: in `arm-none-eabi-objdump -d build/lxr02.elf`,
  `adc_sliderGainMirrored` (or its inlined copy) contains no call to `powf`.

### 5.2 Hardware and listening (yours)

Use one voice with an active Effect (for example `flt` or `cbt`), FX send
127, and the voice's `fd` cell on its VOICE mix page.

| # | Check | Expect |
|---|---|---|
| H1 | Scroll `fd` | `pre`, `pst`, `fx `, `xfd`, and it stops at `xfd`; the full view shows `xfd` |
| H2 | `xfd`, fader at the bottom | No dry signal at any volume; the Effect gets the same level as `fx` mode with the fader at the top |
| H3 | `xfd`, fader at the top | Normal dry level for the volume; no FX input (a delay/CrumpBit tail decays and nothing new enters) |
| H4 | `xfd`, sweep bottom → top | Smooth handover from FX to dry; both at about −16 dB in the middle (§1.3) |
| H5 | Compare `fx` at fader position x with `xfd` at 1 − x | The same send level |
| H6 | `xfd`, FX send 0 | Bottom is silent; the top is normal dry |
| H7 | Switch `fx` ↔ `xfd` while playing | No click |
| H8 | Save Scene, reload; power-cycle with AutoSave | `xfd` persists; `sceneset.scg` shows `3` in `fader_setting` |
| H9 | `xfd` with an FX-send step override | The override's amount is used at the bottom |

---

## 6. Documentation follow-ups (after acceptance)

| Document | Update |
|---|---|
| `knowledge_files/specification_reference/EFFECTS_BUS_REFERENCE.md` §3 "Fader modes" table (L111–115) | Add a row: `3 \| xfd \| vol × fader \| send × fader′ (mirrored taper)` and one line on the crossfade |
| `…/EFFECTS_MIXER_DSP_REFERENCE.md` §3 item 3 (L113–115) | Add `- mode xfd: dry = volume × fader, send = send × fader′` with the mirrored-taper formula and the one division |
| `…/MODULE_INTERCHANGE_SPEC.md` L709 and L719 | "PRE/POST/FX" → "PRE/POST/FX/XFD"; `MIXER_FADER_XFD` and `SCENE_FADER_SETTING_MAX` |
| `…/FILESYSTEM_SPEC.md` (`sceneset.scg` `fader_setting`) | Domain 0..3, and the downgrade note (older firmware rejects 3) |
| `MEMORY.md` | Session log entry per the closeout practice |

## 7. Work notes

- 2026-09-29: Read `MEMORY.md` and this complete schedule. Confirmed the
  implementation is one integrated stage, adds no RAM, keeps the existing
  `fader_setting[]` byte and AutoSave wire index, and must preserve the
  in-progress S074 Bus Comp changes in shared files.
- 2026-09-29: Applied D1/D2, X1/X2, S1–S4, P1/P2, F1, and M1/M2. The mirrored
  gain helper uses the closed-form one-division taper inverse with exact
  endpoints; `xfd` is accepted, persisted, clamped, displayed, and consumed
  by the mixer. Added the Scene/mixer `_Static_assert` and kept each new or
  changed implementation adjacent to its contract comment.
- 2026-09-29: `make all` and `make img` pass. The linked production image is
  `text=502,072`, `data=416`, `bss=426,384`; link budget is 502,488/753,664 B
  with 251,176 B free, 4,472 B DTCM statics, and a 126,592 B FX arena. The
  generated `adc_sliderGainMirrored` has one `vdiv.f32` and no `powf` call;
  `git diff --check` is clean. Hardware checks H1–H9 remain for acceptance.
- 2026-09-29: Final live-source audit found no remaining fader-mode `0..2`
  clamp/validation: all Scene, Preset, sceneset, and Menu paths use
  `SCENE_FADER_SETTING_MAX`; the existing final-stage Bus Comp call remains in
  `mixer.c`. The image SHA-256 is
  `d6c8538822aec93ff95f1dc7fb69877e2c29875af678aadd0fd807d0329d465d`.
