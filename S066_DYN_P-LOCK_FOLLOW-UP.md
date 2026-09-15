# S066 P-Lock Follow-Up Fixes

Post hardware-test fixes for the VOICE held-step overlay (S066).

---

## Fix 1 — Pot/Encoder Delta Handling (Critical)

### Symptom

Held-step pot edits (4-parameter overview) cannot increment — CW rotation
produces no visible change. CCW decrements by 2 instead of by 1. Encoder edits
(single-parameter view) increment and decrement by 2, and slow rotation
frequently produces no change at all.

### Root Cause

`va_writeAutomationFromKnob()` operated in an isolated 8-bit domain: it
expanded the stored 7-bit automation value to 8-bit (`v*2`), applied the ±1
delta from the pot/encoder, then converted back to 7-bit (`v/2`). This round
trip is lossy — odd 8-bit values truncate back to the same 7-bit value they
came from:

    stored=50 → expand=100 → +1=101 → /2=50 → no change (wrote=0)
    stored=50 → expand=100 → −1=99  → /2=49 → stored=49 (change!)

CW +1 from an even expanded value always round-trips to the same 7-bit value,
making increment appear non-functional. CCW works on every step but the 7-bit
decrement displays as −2 on the 8-bit scale (100→98→96). The encoder shows ±2
for the same reason.

### Fix (Approach B — approved)

Track four `uint8_t` working values that cache the clamped 8-bit result between
consecutive pot/encoder edits, breaking the lossy 8→7→8 feedback loop. On each
edit, seed from the working value (if mid-edit) instead of re-expanding from
the stored 7-bit. Convert to 7-bit only at the Pattern write step.

The working value is valid when the corresponding underline-suppression bit is
set (same bit that suppresses the value underline during rapid edits). It is
invalidated when the overlay exits, held steps change, or the debounce quiet
period expires — the next edit then re-seeds from the stored 7-bit (or endpoint)
with at most 1 LSB snap, which is imperceptible after a pause.

Delta application and clamping use the same `int32_t` arithmetic and
`menu_clampCellValue()` as the normal parameter editing path.

+4 bytes SRAM (40→44 total). Approved 2026-09-15.

### Additional fix: editMode suppression-bit index

The original `va_applyVoiceMarkers()` editMode branch checked
`va_underlineSuppressed & 0x01u` (always bit 0) instead of the bit for the
actual `activeParameter`. This caused incorrect underline behavior when the
user clicked into parameter 1, 2, or 3. Fixed to use
`(1u << (activeParameter & 3u))`.

### Changes

**File:** `Core/Menu/menu.c`

- State block: added `static uint8_t va_workingValue[4]` (4 B). Updated
  comment block budget and `_Static_assert` to 44 bytes.
- `va_writeAutomationFromKnob()`: rewritten to seed from working value cache
  (if suppression bit set), else expanded stored 7-bit, else endpoint. Applies
  delta via `int32_t` + `menu_clampCellValue()`. Caches the clamped 8-bit
  result, then converts to 7-bit only for `pat_writeStepAutomation()`.
- `va_applyVoiceMarkers()` editMode branch: suppression check changed from
  `0x01u` to `(1u << (activeParameter & 3u))`.
- `va_applyVoiceMarkers()` both branches: display value sourced from
  `va_workingValue[i]` when suppression bit set, else `va_expand7to8(stored7)`.

---

## Fix 2 — Non-Numeric Parameter Display (Important)

### Symptom

When holding steps, parameters with non-numeric display types (waveform, filter
type, on/off, ±63, note names, etc.) show as raw numbers instead of their
actual menu item names, in both the 4-parameter overview and single-parameter
view.

### Root Cause

The overlay display code in `va_applyVoiceMarkers()` always formatted the
automation value with `numtostrpu()` — a bare numeric formatter. It did not
consult the parameter's `dtype` for the correct display format.

### Fix

Added `va_formatValue3()` — a helper that mirrors the dtype switch in
`menu_formatCellValue3()` but accepts an explicit uint8_t value instead of
reading from the Scene image. Handles DTYPE_PM63 (±63 signed), DTYPE_MIX_FM,
DTYPE_ON_OFF, DTYPE_LFO_POLARITY, DTYPE_MENU (waveform/filter/etc.),
DTYPE_NOTE_NAME, DTYPE_0b1, and the numeric default.

Both the overview and editMode branches of `va_applyVoiceMarkers()` now call
`va_formatValue3()` instead of `numtostrpu()`.

### Changes

**File:** `Core/Menu/menu.c`

- Added `va_formatValue3()` definition before `va_applyVoiceMarkers()`.
- Added forward declaration with the other S066 overlay helpers.
- Both display branches in `va_applyVoiceMarkers()` replaced
  `numtostrpu(...)` with `va_formatValue3(&cell, display_val, ...)`.

---

## Build Verification

`make clean && make -j2 && make img` completed successfully.

| Metric | Before fixes | After fixes | Delta |
|--------|-------------|-------------|-------|
| text | 439,124 | 439,604 | +480 (va_formatValue3 + rewritten write path) |
| data | 412 | 412 | 0 |
| bss | 290,844 | 290,852 | +8 (4 B working values + 4 B alignment) |
| image | 439,536 | 440,016 | +480 |

`_Static_assert` confirms exactly 44 bytes overlay state.

---

## Status

| Fix | Priority | SRAM | Status |
|-----|----------|------|--------|
| Fix 1 — Delta handling | Critical | +4 B | **Implemented** — approach B (working values) |
| Fix 1b — editMode bit index | Minor | 0 | **Implemented** — found during Fix 1 |
| Fix 2 — Non-numeric display | Important | 0 | **Implemented** — va_formatValue3 |

All fixes are in the working tree, build-verified. Ready for hardware retest.
