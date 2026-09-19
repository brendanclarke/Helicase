# Session 066 — VOICE-Page Held-Step Automation Overlay (Method 2)

```
DATE: 2026-09-15/16
SESSION GOAL: Implement VOICE-page held-step automation overlay (Method 2) from
  S065_DYN_PAT_VOICE_PARAM_UX.md, including all 12 implementation steps from
  S066_IMPLEMENTATION_SCHEDULE.md, plus post-hardware-test fixes.
COMPLETED: Full overlay system implemented and hardware-tested. Six post-test
  fixes applied and build-verified.
VERIFIED ON HARDWARE: Yes — initial implementation tested on hardware, six bugs
  found and fixed, fixes build-verified (hardware retest pending for fixes 3-6
  but user confirmed code is fine).

CHANGES THIS SESSION:
- config.h: +3 constants (BUTTON_HOLD_DELAY_MS=100u, VOICE_AUTOMATION_UNDERLINE_QUIET_MS=100u, VOICE_AUTOMATION_SCAN_STEPS_PER_PASS=4u)
- lcd.c: +496 B flash font table (62 glyphs), lcd_fontIndex(), lcd_underlineGlyph(), 'S' glyph fix
- lcd.h: lcd_underlineGlyph() declaration
- menu.c: ~660+ lines overlay system, 44-byte state block, all 6 post-test fixes
- menu.h: four overlay bridge declarations
- buttonHandler.c/h: seqHeldMask() export, visibleStep() extern, VOICE overlay routing
- ledHandler.c/h: led_updateAutomationStepView()
- copyClearTools.c: two menu_voiceAutoOverlayPatternDeleted() calls

KNOWN ISSUES INTRODUCED: None
KNOWN ISSUES RESOLVED: None (the 6 fixes resolved bugs introduced within S066 itself)

NEXT SESSION RECOMMENDED GOAL: S067 — Pattern dynamic stack defragmentation
  service agent and settings-menu pool usage monitor. See
  S067_DYN_PAT_STACK_SERVICE.md.
BLOCKERS: None. Five bugs identified during hardware testing are tracked in
  SCOPING_TARGETS.md for later sessions (AutoSave CPU usage, AutoSave off→on,
  probability-applies-to-trigger-only, chaselight, menu spec sheet).

CRITICAL REMINDERS FOR NEXT SESSION:
- Always make clean after editing config.h (or any header). No header dependency tracking.
- All uncommitted on-chip RAM is reserved. Get user approval before any new allocation.
- New code: detailed contract-level comments (why, what, inputs/outputs, clients).
- Use the same encoder read and inc/dec counting as for normal parameter editing.
- Held-step edits must NEVER mutate normal/Morph endpoints.
- seq_drainPendingAutomation() must run inside audio_check_and_render() immediately after voiceControl_processPending().
- instrumentManager_writeRuntime() is NOT ISR-safe.
- Restore from morph_interpolation[], not instrument_parameters[].
```

---

## 1. Design specification

The complete overlay design is `S066_DYN_PAT_VOICE_PARAM_UX.md` (803 lines),
written during Session 065 as part of the S065 step automation design. It
covers:

- §1 Overlay activation via configurable short long-press (100ms threshold)
- §2 Bounded single-marker underline system (CGRAM slots 2..5)
- §3 Value display and resolution (newest-to-oldest held step search)
- §4 Step illumination of automated steps
- §5 Async track-wide automation search agent (4 steps/pass, 128 total)
- §6 Writing automation via parameter adjustment (pot/encoder delta)
- §7 Integration points with existing menu/button/LED systems
- §8 Risk cases (endpoint safety, CGRAM queue capacity, debounce, etc.)
- §9 Implementation order (12 steps)
- §10 RAM/flash budget (40 B initially, extended to 44 B)
- §11 Resolved decisions
- §12 Implementation notes
- §13 Code review results

## 2. Implementation schedule

The line-by-line implementation companion is `S066_IMPLEMENTATION_SCHEDULE.md`
(1720 lines). It lists every code change with exact file, line number,
operation (insert/replace/delete), and detailed comment blocks. The 12 steps:

1. Config constants in `config.h`
2. 62-glyph font table and helpers in `lcd.c/h`
3. CGRAM cache types and state block in `menu.c`
4. Async search agent in `menu.c`
5. Overlay activation lifecycle in `buttonHandler.c` and `menu.c`
6. Held-value display resolution in `menu.c`
7. Debounced value-underline reapplication in `menu.c`
8. Step illumination in `ledHandler.c/h`
9. Automation write via pot/encoder in `menu.c`
10. Morph integration (endpoint safety) in `menu.c`
11. Edge cases (page change, track change, pattern delete) across all files
12. Hardware test plan

## 3. Technical details

### 3.1 Font system

The HD44780/WS0010 display has 8 CGRAM slots for custom characters (5×8 pixel
glyphs). Slots 0-1 are reserved for existing use. Slots 2-5 are assigned to
the four visible VOICE-page parameters for automation underline markers.

To underline a character, the overlay needs to know the ROM glyph's pixel
pattern. The WS0010 English/Japanese ROM font is not readable at runtime, so
a 62-entry flash font table (`lcd_font_alpha_numeric[]`, 496 bytes) stores the
5×8 pixel patterns for ASCII 0x20-0x7E (space through tilde, excluding
control characters and DEL).

`lcd_fontIndex(ch)` maps an ASCII character to its table index (returns -1 for
out-of-range). `lcd_underlineGlyph(buf, ch, tier)` composites the ROM glyph
with the marker tier:

- Tier 1 (value bar): row 7 = 0x1F (all 5 pixels lit on the bottom row)
- Tier 2 (assigned dot): row 7 = 0x04 (center pixel only)

### 3.2 CGRAM cache and transaction

`va_cgramValid` is a bitmask tracking which of slots 2-5 currently contain
valid CGRAM definitions. The transaction function `va_queueMarkerTransaction()`
performs:

1. **Pre-scan**: simulates stale-ref restoration and counts actual character
   diffs between `editDisplayBuffer` and `currentDisplayBuffer`.
2. **Capacity check**: `needed = stale * 2 + changed * 10 + diff * 2 + cursor`.
   If the LCD SPSC ring doesn't have enough free slots, sets
   `VA_MARKER_RETRY_BIT` and `menu_lcdRefreshPending`, then returns.
3. **Stale restoration**: any position in `currentDisplayBuffer` still showing
   an old CGRAM slot code (2..5) is restored to its ROM character via a
   setCursor+data pair. `currentDisplayBuffer` is updated so the subsequent
   diff sees post-restoration state.
4. **CGRAM definition**: for each changed slot, 10 queue entries (setCGRAM
   address + 8 data bytes + return-to-DDRAM).
5. **Diff-based frame write**: only positions where `editDisplayBuffer` differs
   from `currentDisplayBuffer` are written (setCursor + data per changed
   position).

This replaced the original full 32-char frame write (~64 ops), bringing
typical transaction cost during scrolling from ~112 ops to ~16-24 ops.

### 3.3 Async search agent

The search agent is a polled state machine called from
`menu_serviceRuntimeWidgets()`. It scans all 128 steps of the current track,
`VOICE_AUTOMATION_SCAN_STEPS_PER_PASS` (4) steps per main-loop pass. For each
step with a pool block (bit 14 set, valid offset), it reads automation entries
and checks for matches against the four visible parameter targets. Matches set
the "assigned" tier underline for that parameter.

The agent self-invalidates on: track change, page/sub-page change, any
automation write (the write changes what the search would find), held mask
change, and overlay exit. Invalidation resets the scan position and clears
previously found results, so the next scan pass starts fresh.

### 3.4 Overlay lifecycle

**Entry**: `buttonHandler.c` arms a timer when a SEQ button is pressed during
VOICE mode. After `BUTTON_HOLD_DELAY_MS` (100ms), if the button is still held
and the step is active, `menu_voiceAutoOverlayEnter()` is called. The overlay
state block initializes, the async search starts, and the display switches to
overlay mode.

**Active**: while any SEQ button is held, pot turns and encoder clicks operate
on the held steps' automation values instead of the normal Scene image. The
display shows resolved automation values (or endpoint fallback) with CGRAM
underlines.

**Exit**: when all held SEQ buttons are released,
`menu_voiceAutoOverlayExit()` restores normal VOICE display. CGRAM is
redefined back to name-underline glyphs. The state block is cleared.

### 3.5 Value resolution

`va_resolveHeldValue()` walks the held-step bitmask from newest (highest bit)
to oldest (lowest bit), returning the first step that has an automation entry
matching the exact target parameter. If no held step has a match, the function
returns the current Scene image value (from `morph_interpolation[]`). This
"newest held step wins" policy matches the user's expectation when holding
multiple steps.

### 3.6 Write path

`va_writeAutomationFromKnob()` handles both pot and encoder input:

1. **Seed**: if the working-value cache is valid (upper nibble of
   `va_underlineSuppressed`), use it. Otherwise, expand the stored 7-bit
   automation value to 8-bit (`v*2`, with 127→255 for the top). If no
   automation exists, use the Scene endpoint value.
2. **Apply delta**: add the ±1 (pot) or ±N (encoder with acceleration) delta
   to the seed, clamp with `menu_clampCellValue()`.
3. **Cache**: store the clamped 8-bit result in `va_workingValue[knobNr]`.
   Set both the suppression bit (lower nibble) and validity bit (upper nibble).
4. **Write**: convert to 7-bit (`v >= 255 ? 127 : v / 2`) and call
   `pat_writeStepAutomation()` for every held step.
5. **Refresh**: invalidate the async search agent, trigger repaint.

### 3.7 Debounce

`va_underlineService()` runs in `menu_serviceRuntimeWidgets()`. It watches
`va_underlineSuppressed` (lower nibble). When set, it starts a quiet-period
timer. After `VOICE_AUTOMATION_UNDERLINE_QUIET_MS` (100ms) with no new edits,
it clears the lower nibble (making the value-bar underline reappear) while
preserving the upper nibble (keeping the working-value cache valid). This
means:

- During rapid edits: no underline (clean display), working value maintained
- After 100ms pause: underline appears, working value still used for display
  and as seed for next edit
- On held mask change or overlay exit: full byte cleared, working value
  invalidated, next edit re-seeds from stored 7-bit

### 3.8 State block

```c
static uint8_t  va_overlayActive;       /* overlay on/off flag */
static uint8_t  va_activeParameter;     /* which param has focus (0..3) */
static uint8_t  va_searchStep;          /* async search cursor (0..127) */
static uint8_t  va_searchRunning;       /* search in progress flag */
static uint8_t  va_cgramValid;          /* bitmask: which CGRAM slots valid + retry bit */
static uint8_t  va_underlineSuppressed; /* lower nibble: suppress, upper: validity */
static uint8_t  va_workingValue[4];     /* 8-bit working value cache per param */
static uint16_t va_debounceTimestamp;   /* last-edit time for debounce */
/* ... plus other tracking fields totaling 44 bytes */
```

Verified by `_Static_assert(sizeof(state_block) == 44, ...)`.

## 4. Post-hardware-test fixes

Full detail in `S066_DYN_P-LOCK_FOLLOW-UP.md`.

### Fix 1 — Pot/Encoder Delta Handling (Critical)

**Symptom**: CW pot rotation produced no visible change; CCW decremented by 2;
encoder edits incremented/decremented by 2 with frequent no-change events.

**Root cause**: `va_writeAutomationFromKnob()` operated in an isolated 8-bit
domain: expanded stored 7-bit to 8-bit (`v*2`), applied ±1 delta, converted
back to 7-bit (`v/2`). The round trip is lossy — odd 8-bit values truncate:
`stored=50 → expand=100 → +1=101 → /2=50 → no change`. CW from even expanded
values always round-trips to the same 7-bit. CCW works but shows as -2 on the
8-bit scale.

**Fix**: working-value cache (`va_workingValue[4]`, +4 B SRAM) that caches the
clamped 8-bit result between consecutive edits. Seeds from cache (if mid-edit)
instead of re-expanding. Converts to 7-bit only at the Pattern write step.
Invalidated on overlay exit, held mask change, or debounce expiry.

### Fix 1b — editMode suppression-bit index (Minor)

**Root cause**: `va_applyVoiceMarkers()` editMode branch checked
`va_underlineSuppressed & 0x01u` (always bit 0) instead of the bit for the
actual `activeParameter`.

**Fix**: changed to `(1u << (activeParameter & 3u))`.

### Fix 2 — Non-Numeric Parameter Display (Important)

**Symptom**: parameters with non-numeric display types (waveform, filter type,
on/off, ±63, note names) showed raw numbers in both overview and detail views.

**Root cause**: display code always used `numtostrpu()` without consulting the
parameter's `dtype`.

**Fix**: added `va_formatValue3()` — dtype-aware formatter handling DTYPE_PM63,
DTYPE_MIX_FM, DTYPE_ON_OFF, DTYPE_LFO_POLARITY, DTYPE_MENU, DTYPE_NOTE_NAME,
DTYPE_0b1, and numeric default. Both display branches now use it.

### Fix 3 — Capital 'S' Font Glyph (Minor)

**Symptom**: underlined 'S' had hooked end pixels not present in the WS0010
CGROM 'S'.

**Fix**: `lcd_font_alpha_numeric[28]` ('S'): row 1 `0x11→0x10`, row 5
`0x11→0x01`.

### Fix 4 — PM63 ±64 Snap After Debounce (Important)

**Symptom**: fine tune and pan (DTYPE_PM63) reached +64 during editing but
snapped to +63 after the 100ms debounce quiet period.

**Root cause**: value +64 = 8-bit 127. The 8→7→8 conversion `127/2=63`,
`63*2=126` = +63. Working-value cache held 127 during editing but was
invalidated when `va_underlineSuppressed` cleared at debounce expiry.

**Fix**: split `va_underlineSuppressed` into two nibbles. Lower nibble
(bits 0..3) = underline suppression, clears on debounce expiry. Upper nibble
(bits 4..7) = working-value validity, persists until held mask change or
overlay exit. Zero additional SRAM.

### Fix 5 — Overlay Exit CGRAM Ordering Glitch (Important)

**Symptom**: releasing all held step buttons caused a one-frame glitch — the
value-row underlined character appeared at the name-row position.

**Root cause**: exit path called `menu_repaintAll()` which fills
`currentDisplayBuffer` with 0x7F, defeating stale-ref detection in
`va_queueMarkerTransaction()`. CGRAM was redefined while DDRAM still contained
old slot codes.

**Fix**: changed exit branch from `menu_repaintAll()` to `menu_repaint()`.
This preserves real `currentDisplayBuffer` state so stale refs are correctly
detected and restored before CGRAM redefinition.

Key insight: `menu_repaintAll()` clears `currentDisplayBuffer` to 0x7F to
force a full redraw. `menu_repaint()` preserves real LCD state, enabling
correct stale-ref detection. The diff-based CGRAM transaction depends on
`currentDisplayBuffer` reflecting actual LCD DDRAM content.

### Fix 6 — Fast Encoder Scroll Underline Disappearance (Important)

**Symptom**: rapidly scrolling the encoder with acceleration caused all
automation underlines to disappear and not recover after scrolling stopped.

**Root cause (two issues)**:
- **(A)** Excessive queue cost: marker transaction wrote full 32-char frame
  (64 ops) on every CGRAM change plus stale restores and CGRAM definitions
  (~112 ops worst case). Fast scrolling generated repaints faster than TIM7
  drained the 128-entry queue.
- **(B)** Lost retry signal: fallback set `menu_lcdRefreshPending = 1` but
  `sendDisplayBuffer()` succeeded on the underline-free frame diff and cleared
  it to 0. No trigger remained to retry after scrolling stopped.

**Fix**:
- **(A)** Diff-based frame write: only positions where `editDisplayBuffer`
  differs from `currentDisplayBuffer` are written. Also syncs
  `currentDisplayBuffer` during stale-ref restoration. Typical cost: ~16-24
  ops vs ~112.
- **(B)** `VA_MARKER_RETRY_BIT` (bit 4 of `va_cgramValid`): set in fallback,
  cleared on successful transaction. `menu_serviceRuntimeWidgets()` checks
  this bit and triggers `menu_repaint()` when queue has ≥72 free slots.
  Survives `sendDisplayBuffer()` clearing `menu_lcdRefreshPending`.

## 5. Build

```
text=439,396  data=412  bss=290,852  image=439,808
```

Net delta from S065 baseline (`text=434,036 data=412 bss=290,788`):
- text: +5,360 (overlay logic + font table + helpers)
- bss: +64 (44 B overlay state + 20 B buttonHandler state)

The 44-byte overlay state block was approved as 40 B initially (2026-09-15)
and extended to 44 B (+4 B for `va_workingValue[4]`, also 2026-09-15) during
Fix 1.

## 6. Specification documents

The following S066 task documents contain the complete implementation details
and should be consulted for any future work on the overlay system:

- **`S066_DYN_PAT_VOICE_PARAM_UX.md`** (803 lines) — Complete design
  specification with 13 sections covering every aspect of the overlay.
- **`S066_IMPLEMENTATION_SCHEDULE.md`** (1720 lines) — Line-by-line
  implementation companion with file/line/operation/comment for every change.
- **`S066_DYN_P-LOCK_FOLLOW-UP.md`** (333 lines) — All 6 post-hardware-test
  fixes with symptom/root-cause/fix/changes detail.

These documents are disposable after this handoff log preserves their content.

## 7. Known bugs identified during S066 testing

Five bugs were identified during hardware testing and added to
`SCOPING_TARGETS.md` for later sessions:

1. **AutoSave CPU usage**: ~4-5% CPU constantly even when no changes exist;
   disappears when autosave is 'off'.
2. **AutoSave off→on**: switching autosave off and back on doesn't pick up
   changes until reboot.
3. **Probability applies to trigger only**: the `prb` special currently
   controls note trigger but should control the whole step including automation.
4. **Chaselight missing**: particularly after reboot; tends to come back after
   switching scenes. Partially traced in S057, unresolved.
5. **Menu specification sheet**: need a comprehensive menu reference document.
   Dedicated future session.

## 8. Next session — S067

`S067_DYN_PAT_STACK_SERVICE.md` outlines:

1. **Pattern dynamic stack defragmentation service agent** — two-tier polled
   state machine: Tier 1 micro-relocation (slack refill for individual steps),
   Tier 2 global compaction (consolidate fragmented free runs). Bounded per-tick
   work, AutoSave snapshot guard, stale-request detection via address comparison.
   33 B new static SRAM (pending approval).

2. **Pattern pool usage monitor** — settings-menu widget showing `pol:NN` (0-99
   percentage of current Scene's pool occupancy). Computed on demand from
   existing bitmap data. Zero RAM cost.
