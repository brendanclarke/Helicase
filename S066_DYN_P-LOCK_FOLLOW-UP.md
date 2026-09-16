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

---

## Fix 3 — Capital 'S' Font Glyph (Minor)

### Symptom

The underlined capital 'S' has hooked end pixels (serifs on both
sides of the letter's openings) that the WS0010 CGROM 'S' does not show.
The mismatch is noticeable when an underlined 'S' sits next to a
non-underlined one.

### Root Cause

Font table entry for 'S' (`lcd_font_alpha_numeric[28]`) had rows 1 and 5
set to `0x11` (X...X) — pixels on both left and right endpoints. The
WS0010 CGROM 'S' only has a pixel on the side the curve runs from:
row 1 = `0x10` (X....) and row 5 = `0x01` (....X).

### Fix

Changed the 'S' entry from
`{ 0x0E, 0x11, 0x10, 0x0E, 0x01, 0x11, 0x0E, 0x00 }` to
`{ 0x0E, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x0E, 0x00 }`.

### Changes

**File:** `Core/Hardware/frontPanel/lcd.c`

- `lcd_font_alpha_numeric[28]` ('S'): row 1 `0x11` → `0x10`,
  row 5 `0x11` → `0x01`.

---

## Fix 4 — PM63 ±64 Snap After Debounce (Important)

### Symptom

Fine tune and pan (DTYPE_PM63) reach +64 in the held-step overlay, but
after ~100 ms (the debounce quiet period) the display snaps to +63.

### Root Cause

Value +64 = 8-bit 127. The 8→7-bit conversion `127/2 = 63` stores 63;
expansion `63*2 = 126` = +63. The working-value cache (Fix 1) held 127
during the quiet period, but the cache was invalidated when
`va_underlineSuppressed` cleared at debounce expiry, causing the next
repaint to re-expand from stored 7-bit and show 126 (+63).

### Fix

Split `va_underlineSuppressed` into two nibbles:

- **Lower nibble (bits 0..3):** underline suppression — controls whether
  the value marker is hidden during rapid edits. Cleared on debounce
  expiry so the marker reappears.
- **Upper nibble (bits 4..7):** working-value validity — controls whether
  `va_workingValue[]` is used for display and knob seeding. Persists
  across the debounce boundary, invalidated only when the held mask
  changes or the overlay exits.

This lets the working value survive debounce expiry: the display keeps
showing the cached 8-bit value (127 = +64) while the underline marker
reappears. The value snaps to +63 only on a discrete user action (release
and re-hold), matching what automation storage actually contains.

Zero additional SRAM — both nibbles share the existing `va_underlineSuppressed`
byte within the approved 44-byte budget.

### Changes

**File:** `Core/Menu/menu.c`

- Comment on `va_workingValue[]` updated to document the nibble layout.
- `va_applyVoiceMarkers()` editMode branch: added `validity_bit` variable;
  display-value source checks upper nibble, underline-suppression check
  unchanged (lower nibble).
- `va_applyVoiceMarkers()` overview loop: display-value source check
  changed from `(1u << i)` to `(0x10u << i)`.
- `va_underlineService()`: early-exit test changed from `== 0u` to
  `& 0x0Fu`. Debounce-expiry clear changed from `= 0u` to `&= 0xF0u`.
- `va_writeAutomationFromKnob()`: seed check changed from `(1u << knobNr)`
  to `(0x10u << knobNr)`; set-on-write changed from `|= (1u << knobNr)`
  to `|= (1u << knobNr) | (0x10u << knobNr)`.
- All other `va_underlineSuppressed = 0u` sites (held-change, overlay exit,
  reset, page/track change, editMode toggle) unchanged — clearing the full
  byte correctly invalidates both suppression and validity.

---

## Fix 5 — Overlay Exit CGRAM Ordering Glitch (Important)

### Symptom

When releasing all held step buttons, the underlined character from the
value row briefly appears at the short-name position on the top row
before the correct name-underline glyph appears.

### Root Cause

The exit path called `menu_repaintAll()`, which fills
`currentDisplayBuffer` with 0x7F before formatting the new frame. The
subsequent `va_queueMarkerTransaction()` scans `currentDisplayBuffer`
for stale CGRAM slot references (codes 2..5) to restore them to ROM
characters before redefining the CGRAM. With 0x7F in every position,
no stale refs are found. The CGRAM is redefined while the LCD's DDRAM
still contains the old slot codes — the hardware immediately displays
the new name-underline glyph at the old value-row position, producing a
one-frame glitch until the frame write overwrites it.

### Fix

Changed the exit branch in `va_updateHeldState()` from `menu_repaintAll()`
to `menu_repaint()`. This preserves the real `currentDisplayBuffer` state
so `va_queueMarkerTransaction()` correctly detects stale CGRAM references,
restores them to ROM characters first, then redefines the CGRAM and writes
the new frame.

`menu_repaintGeneric()` already clears both rows of `editDisplayBuffer`
before formatting, so the explicit buffer clear in `menu_repaintAll()` is
redundant for this path.

### Changes

**File:** `Core/Menu/menu.c`

- `va_updateHeldState()`, all-released branch: `menu_repaintAll()` →
  `menu_repaint()`.

---

## Fix 6 — Fast Encoder Scroll Underline Disappearance (Important)

### Symptom

In the 4-parameter voice overview, rapidly scrolling the encoder with
acceleration causes all automation underlines to disappear and not
recover after scrolling stops.

### Root Cause (two issues)

**A. Excessive queue cost.** The marker transaction wrote a full 32-char
frame (64 ops) on every CGRAM change, regardless of how many characters
actually differed. Combined with stale-ref restores and CGRAM definitions,
the worst case was ~112 ops per repaint. Fast encoder scrolling generates
repaints faster than TIM7 drains the 128-entry queue, causing the
transaction to fall back on nearly every event.

**B. Lost retry signal.** The fallback set `menu_lcdRefreshPending = 1`,
but the `sendDisplayBuffer()` call at the end of `menu_repaint()`
succeeded on the (underline-free) frame diff and cleared
`menu_lcdRefreshPending = 0`. After scrolling stopped, no trigger
remained to retry the marker transaction — the underlines stayed missing.

### Fix

**A. Diff-based frame write.** Replaced the fixed 32-char full-frame
write with a diff against `currentDisplayBuffer`. Typical cost during
scrolling: ~6–12 changed characters (16–24 ops) instead of 32 (64 ops).
Also syncs `currentDisplayBuffer` during stale-ref restoration so the
diff sees the post-restoration state correctly.

**B. Retry bit.** Added `VA_MARKER_RETRY_BIT` (bit 4 of `va_cgramValid`)
that is set in the fallback and cleared on successful transaction. The
`menu_serviceRuntimeWidgets()` VOICE-page block checks this bit and
triggers `menu_repaint()` when the queue has drained (≥72 free slots).
This bit survives `sendDisplayBuffer()` clearing `menu_lcdRefreshPending`
and ensures underlines recover after a burst.

### Changes

**File:** `Core/Menu/menu.c`

- Added `#define VA_MARKER_RETRY_BIT 0x10u` beside the CGRAM slot
  constants.
- `va_queueMarkerTransaction()` rewritten:
  - Marker positions set to CGRAM refs before the pre-scan (reverted on
    fallback).
  - Single combined pre-scan loop: simulates stale restoration and counts
    actual diffs, replacing the separate stale-count + fixed 64.
  - `needed` formula: `stale * 2 + changed * 10 + diff * 2 + cursor`.
  - Fallback: sets `VA_MARKER_RETRY_BIT` alongside `menu_lcdRefreshPending`.
  - Stale restoration: now updates `currentDisplayBuffer` so the diff-write
    sees post-restoration state.
  - Frame write: diff-only — skips positions where
    `currentDisplayBuffer` already matches `editDisplayBuffer`.
  - Success: clears `VA_MARKER_RETRY_BIT`.
- `va_queueMarkerTransaction()` early return (no CGRAM changes): also
  clears `VA_MARKER_RETRY_BIT`.
- `menu_serviceRuntimeWidgets()`: added retry check — if
  `VA_MARKER_RETRY_BIT` set and queue ≥72 free, triggers `menu_repaint()`.

---

## Build Verification (Round 2)

`make clean && make -j2 && make img` completed successfully.

| Metric | Before (round 1) | After (round 2) | Delta |
|--------|-------------------|------------------|-------|
| text | 439,604 | 439,396 | −208 (diff-based write smaller than full frame) |
| data | 412 | 412 | 0 |
| bss | 290,852 | 290,852 | 0 |
| image | 440,016 | 439,808 | −208 |

`_Static_assert` confirms exactly 44 bytes overlay state. No new SRAM.

---

## Status

| Fix | Priority | SRAM | Status |
|-----|----------|------|--------|
| Fix 1 — Delta handling | Critical | +4 B | **Implemented** — approach B (working values) |
| Fix 1b — editMode bit index | Minor | 0 | **Implemented** — found during Fix 1 |
| Fix 2 — Non-numeric display | Important | 0 | **Implemented** — va_formatValue3 |
| Fix 3 — 'S' glyph | Minor | 0 | **Implemented** — removed hook pixels |
| Fix 4 — PM63 ±64 snap | Important | 0 | **Implemented** — nibble split |
| Fix 5 — Exit CGRAM ordering | Important | 0 | **Implemented** — repaint (not repaintAll) |
| Fix 6 — Scroll underline loss | Important | 0 | **Implemented** — diff writes + retry bit |

All fixes are in the working tree, build-verified. Ready for hardware retest.
