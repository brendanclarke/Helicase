# S067 — Dtype Offset Bug Fix: Implementation Schedule

Reference: `S067_DTYPE_OFFSET_BUG.md` (root cause analysis).

## Summary

Remove all MIDI CC-style `/2` (encode) and `*2` (decode) conversions from
the four automation value code sites. Every automatable parameter today has a
descriptor-domain range ≤ 127 that fits in the 7-bit automation storage
field directly. The stored 7-bit value becomes the parameter value (identity
mapping), matching the step automation page which already works this way.

No RAM allocation changes. No new functions. No binary format changes.

---

## Change 1 — `va_expand7to8()`: convert to identity

**File:** `Core/Menu/menu.c`
**Lines:** 1815–1818 (definition), 1677 (forward declaration)

### Current code (line 1815–1818)

```c
static uint8_t va_expand7to8(uint8_t value)
{
    return (value == 127u) ? 255u : (uint8_t)(value * 2u);
}
```

### Change

Replace the function body with a straight identity return. The function name
becomes misleading (`expand7to8` when there is no expansion), so **rename**
it to `va_storedToParam` to reflect its new role: stored 7-bit automation
value → parameter-domain value. For all currently automatable dtypes this is
the identity; the name signals that future dtype-specific expansion (e.g.
DTYPE_0B255 for voice morph) would live here.

```c
/*
 * Convert a stored 7-bit automation value to the parameter domain.
 *
 * What: identity for all currently automatable dtypes (range ≤ 127). If a
 * future dtype exceeds 7-bit range (e.g. DTYPE_0B255 for voice morph), this
 * function is the sole expansion point.
 * Inputs: raw 7-bit value from Pattern pool. Output: parameter-domain value.
 * Affiliates: va_applyVoiceMarkers() display, va_writeAutomationFromKnob()
 * seed, and any future dtype-conditional expansion.
 */
static uint8_t va_storedToParam(uint8_t value)
{
    return value;
}
```

### Also update

- **Forward declaration (line 1677):** rename from `va_expand7to8` to
  `va_storedToParam`.
- **Call site (line 2216):** `va_expand7to8(value7)` → `va_storedToParam(value7)`.
- **Call site (line 2265):** `va_expand7to8(value7)` → `va_storedToParam(value7)`.
- **Call site (line 2372):** `va_expand7to8(stored7)` → `va_storedToParam(stored7)`.

---

## Change 2 — `va_writeAutomationFromKnob()`: remove halving on write

**File:** `Core/Menu/menu.c`
**Lines:** 2329–2400

### Current code (line 2387)

```c
    /* Convert to 7-bit for Pattern storage only. */
    stored7 = (value >= 255u) ? 127u : (uint8_t)(value / 2u);
```

### Change

Replace the halving with a saturating cast. After `menu_clampCellValue()` the
value is already within the dtype's range (≤ 127 for every automatable
dtype), so the 127 clamp is a defensive ceiling only.

```c
    /* Saturate to 7-bit for Pattern storage. For every currently automatable
     * dtype the clamped value is already ≤ 127; this guard is defensive. */
    stored7 = (value > 127u) ? 127u : (uint8_t)value;
```

### Comment block update (lines 2329–2344)

The function's contract comment references the "lossy 8→7→8 round-trip" as
the motivation for the working-value cache. With identity conversion the
round-trip is no longer lossy for any current dtype. Update the `What:` and
`Why:` to reflect the new situation:

```
 * What: seeds from the working-value cache (if mid-edit), else from the
 * stored automation value, else from the read-only displayed endpoint.
 * Applies the delta and clamps using the same menu_clampCellValue() path as
 * normal parameter editing. The clamped result is cached for the next detent,
 * then saturated to 7-bit and written to every held step.
 * Why: held edits are Pattern-only and must never call endpoint commit, DSP,
 * or Autosave code. The working-value cache avoids re-resolving the held
 * value between consecutive detents, keeping edit responsiveness consistent.
```

### Seed path comment update (line 2367–2368)

```
    /* Seed: working cache if validity bit set, else stored value,
     * else the read-only displayed endpoint for first creation. */
```

(Remove "7-bit expanded" since there is no expansion.)

---

## Change 3 — `seq_drainPendingAutomation()`: remove doubling on playback

**File:** `Core/Sequencer/sequencer.c`
**Lines:** 606–608

### Current code

```c
            uint8_t value7 = (uint8_t)((packed >> 9u) & 0x7Fu);
            uint8_t value8 = (value7 == 127u) ? 255u :
                             (uint8_t)(value7 * 2u);
```

Line 624 passes `value8` to `instrumentManager_writeRuntime()`.

### Change

Remove the `value8` intermediate. Pass `value7` directly to the runtime
writer. Rename the variable to `value` for clarity since it is no longer a
7-bit→8-bit conversion.

```c
            uint8_t value = (uint8_t)((packed >> 9u) & 0x7Fu);
```

Line 624: change `value8` → `value`.

### Comment update (lines 578–587)

The function comment does not mention the conversion, but the inline variable
name `value8` implies a domain shift. No contract-level comment change
needed, only the variable rename.

---

## Change 4 — `menu_stepAutomationAddDefault()`: remove halving on creation

**File:** `Core/Menu/menu.c`
**Lines:** 8044–8048

### Current code (line 8046–8047)

```c
    if (instrument && instrumentParam_isVoiceParameter(target)) {
        local = instrumentParam_local(target);
        value = instrument->parameter_images.instrument_parameters[local];
        value = (uint8_t)(value >= 255u ? 127u : (value / 2u));
    }
```

### Change

Replace the halving with a saturating clamp. The parameter image byte is
already in the descriptor domain (≤ 127 for every automatable dtype).

```c
    if (instrument && instrumentParam_isVoiceParameter(target)) {
        local = instrumentParam_local(target);
        value = instrument->parameter_images.instrument_parameters[local];
        if (value > 127u) value = 127u;
    }
```

---

## Change 5 — Working-value cache comment updates

The working-value cache (`va_workingValue[4]`) and the nibble-split
`va_underlineSuppressed` mechanism remain in place — the cache still provides
a consistent seeding path and display stability during rapid edits. However
several comments reference the now-obsolete "lossy 8→7→8 round-trip" as the
cache's reason for existence. These must be updated to reflect the actual
remaining purpose.

### 5a — Overlay state block comment (lines 1169–1188)

**Current** (lines 1174–1182):
```
 * …and four bytes of 8-bit working values that cache
 * the cell-domain edit value between consecutive pot/encoder detents.
 * …
 * The working-value cache breaks the lossy 8→7→8
 * round-trip that would otherwise cause missed increments and ±2 display
 * jumps.
```

**Replace** the working-value rationale lines with:
```
 * …and four bytes of working values that cache the edit value between
 * consecutive pot/encoder detents.
 * …
 * The working-value cache avoids re-resolving the held value and re-reading
 * Pattern storage between consecutive encoder detents, keeping edit
 * responsiveness consistent with the normal parameter edit path.
```

### 5b — Working-value declaration comment (lines 1207–1213)

**Current:**
```c
/* 8-bit working-value cache: breaks the lossy 8→7→8 round-trip between
 * consecutive pot/encoder edits. Valid when the corresponding upper-nibble
 * bit in va_underlineSuppressed is set (bit 4..7 = validity, bit 0..3 =
 * underline suppression). The underline suppression clears on debounce
 * expiry (100 ms) to let markers reappear; the validity persists so the
 * display keeps showing the working value until the held mask changes or
 * the overlay exits. */
```

**Replace with:**
```c
/* Working-value cache: holds the parameter-domain edit value between
 * consecutive pot/encoder detents so the overlay does not re-read Pattern
 * storage on every increment. Valid when the corresponding upper-nibble
 * bit in va_underlineSuppressed is set (bit 4..7 = validity, bit 0..3 =
 * underline suppression). The underline suppression clears on debounce
 * expiry (100 ms) to let markers reappear; the validity persists so the
 * display keeps showing the working value until the held mask changes or
 * the overlay exits. */
```

### 5c — Underline service debounce comment (line 2320–2323)

**Current:**
```c
        /* Clear suppression (lower nibble) so underline markers reappear.
         * Keep validity (upper nibble) so the working-value cache
         * continues to feed the display — avoids a ±1 snap for odd values
         * like PM63 +64 that don't round-trip through 7-bit storage. */
```

**Replace with:**
```c
        /* Clear suppression (lower nibble) so underline markers reappear.
         * Keep validity (upper nibble) so the working-value cache
         * continues to feed the display until the held mask changes or the
         * overlay exits. */
```

---

## Change 6 — `va_formatValue3()` comment update

**File:** `Core/Menu/menu.c`
**Lines:** 2133–2139

### Current comment

```c
/*
 * Format one automation value using the cell's dtype vocabulary.
 *
 * Mirrors the dtype switch in menu_formatCellValue3() but accepts an explicit
 * 8-bit value rather than reading from the Scene image. Produces the same
 * three-character compact text used by the overview and clicked-in value fields.
 */
```

### Change

The function no longer receives an "8-bit" value distinct from the
parameter value. It now receives the parameter-domain value directly (which
is the same as the stored 7-bit value for all current dtypes).

```c
/*
 * Format one automation value using the cell's dtype vocabulary.
 *
 * Mirrors the dtype switch in menu_formatCellValue3() but accepts an explicit
 * parameter-domain value rather than reading from the Scene image. Produces
 * the same three-character compact text used by the overview and clicked-in
 * value fields. Affiliates: va_applyVoiceMarkers() for both editMode and
 * overview display branches.
 */
```

No code change to the function body — the dtype switch is correct as-is.

---

## Verification Checklist

After applying changes 1–6, `make clean && make` must succeed. Then on
hardware:

1. **Voice overlay round-trip (DTYPE_MENU):** Hold step 12, turn filter_type
   to UBP, release, re-hold. Display must show "UBP" (not "BP"). Repeat for
   tri, rec, Pek, off, s+h on waveform/LFO wave.

2. **Voice overlay round-trip (DTYPE_PM63):** Hold step, turn pan to +10,
   release, re-hold. Display must show "+10" (not "+9"). Test +64, +1, −1,
   −63.

3. **Voice overlay round-trip (DTYPE_ON_OFF):** Hold step, turn velocity
   volume mod to ON, release, re-hold. Must show "on" (not "off").

4. **Voice overlay round-trip (DTYPE_MIX_FM):** Hold step, turn FM mode to
   Mix, release, re-hold. Must show "Mix" (not "FM").

5. **Sequencer playback (DTYPE_MENU):** Write filter_type = UBP via overlay,
   play pattern. The filter should audibly switch to UBP (bandpass 2-pole
   passthrough characteristic) on the automated step. Compare with UBP set as
   the endpoint.

6. **Sequencer playback (DTYPE_PM63):** Write pan = +64 via overlay, play
   pattern. Automation should hard-pan right on that step.

7. **Step auto page ↔ overlay agreement:** Write filter_type = UBP via the
   step automation page. Navigate to voice overlay, hold that step. Display
   must show "UBP". Previously would show "LP2" (doubled).

8. **Step auto page ↔ Sequencer agreement:** Write filter_type = UBP via the
   step automation page, play pattern. Sequencer must apply UBP. Previously
   would apply Nch (doubled).

9. **Add path (step auto page):** Navigate to step automation page, create
   new entry. The seeded value should match the current endpoint. E.g. if
   filter_type endpoint is UBP, the new automation entry shows "UBP" (not
   "HP" as before with halving).

10. **DTYPE_0B127 continuous parameters:** Write filter freq = 60 via overlay,
    release, re-hold. Must show 60 (not 59). Repeat with odd values.

11. **Regression: working-value cache still smooth.** Rapid encoder turns
    during held-step overlay must produce clean 1-per-detent changes, same
    as before.

---

## Not Changed

- **`MidiParser.c:319`** — The MIDI CC `value * 2` expansion is correct for
  `parameter_values[]` (the global 0..255 domain). This is unrelated to the
  automation descriptor-domain bug.

- **`menu_automationValueMax()`** (menu.c:7806) — Already returns correct
  per-dtype 7-bit maximums. No change needed.

- **`menu_formatAutomationValue3()`** (menu.c:7844) — Already formats raw
  7-bit values correctly. No change needed.

- **`menu_stepAutomationEdit()` field 3** (menu.c:8159) — Already operates on
  raw stored values. No change needed.

- **`seq_restoreAutomatedParameters()`** (sequencer.c:661) — Restores from
  `morph_interpolation[]` which is already in the parameter domain. No `/2`
  or `*2` present. No change needed.

- **`instrumentManager_writeSpecialRuntime()`** (InstrumentManager.c:2706) —
  The filter_type `+1` offset (line 2776) converts 0-indexed descriptor
  domain to 1-indexed DSP runtime. This is correct and independent of the
  automation value bug.

- **Pattern binary format (PAT4)** — The 7-bit field in the packed automation
  entry (`bits 9..15`) is unchanged. The semantic meaning of the stored value
  changes from "halved parameter" to "direct parameter" but the binary
  encoding and field width are identical.
