# S066 — VOICE Page Held-Step Automation Overlay: Implementation Schedule

## Authority

This document is the line-by-line implementation companion to
`S065_DYN_PAT_VOICE_PARAM_UX.md` (the design spec). Every code change
required to fulfil that spec is listed here with its file, line number,
operation (ADD/MODIFY/REMOVE), and a detailed comment block suitable for
inclusion alongside the code.

Changes are grouped by the spec's §9 implementation order.

---

## Notation

- **Line numbers** are from the `dev-ph4-pattern` branch as of 2026-09-15.
- **ADD** = new code at the indicated location.
- **MODIFY** = change existing code at that line.
- **REMOVE** = delete the indicated code.
- Comment blocks use the project convention: why, what, inputs, outputs,
  affiliates, risks.

---

## Step 1 — Configuration Constants

### 1.1 config.h — ADD three constants after line 291

**File:** `config.h`  **Line:** after 291 (end of Clock section, before Autosave)
**Operation:** ADD

```c
/* -----------------------------------------------------------------------
** VOICE overlay and UI hold-gesture timing.
**
** What: three tunable constants governing the VOICE-page held-step
** automation overlay (S066). BUTTON_HOLD_DELAY_MS is the common short
** long-press threshold shared by every UI gesture that distinguishes a
** hold from a tap — it replaces the private BUTTON_TIMEOUT that was
** formerly in buttonHandler.h. VOICE_AUTOMATION_UNDERLINE_QUIET_MS is
** the quiet-period before reapplying a value underline after a rapid
** pot edit. VOICE_AUTOMATION_SCAN_STEPS_PER_PASS bounds the async
** Pattern-wide automation search to at most this many steps per
** foreground service pass (worst case: 63 entries per step = 252
** target comparisons at the default of 4).
**
** Why: centralise timing in config.h so a single make-clean rebuild
** applies each tunable consistently. The button-hold threshold was
** previously a private #define in buttonHandler.h with a different
** value (500 ms) and no config.h linkage.
**
** Inputs: none (compile-time constants).
** Outputs: consumed by buttonHandler.c (hold detection), menu.c
** (underline debounce and scan budget).
** Affiliates: buttonHandler.h BUTTON_TIMEOUT (removed), time_sysTick
** (1 kHz wrap-safe uint16_t).
** ----------------------------------------------------------------------- */
#define BUTTON_HOLD_DELAY_MS                100u
#define VOICE_AUTOMATION_UNDERLINE_QUIET_MS 100u
#define VOICE_AUTOMATION_SCAN_STEPS_PER_PASS 4u
```

### 1.2 buttonHandler.h — MODIFY line 23: replace BUTTON_TIMEOUT

**File:** `Core/Hardware/frontPanel/buttonHandler.h`  **Line:** 23
**Operation:** MODIFY

Remove:
```c
#define BUTTON_TIMEOUT          500u  /* ~500 ms; original AVR used 38 * 13.107 ms */
```

Replace with:
```c
/*
 * Common short hold-vs-tap threshold, sourced from config.h.
 *
 * What: BUTTON_HOLD_DELAY_MS (config.h) is the single hold threshold for
 * every UI gesture that distinguishes a hold from a tap: VOICE overlay
 * activation, STEP long-press, PERF mode holds, etc. The former private
 * BUTTON_TIMEOUT (500 ms) is retired; all callers now reference the common
 * config.h constant directly. The old name is preserved as an alias only
 * for readability in the existing timer-action path until that path is
 * refactored.
 *
 * Why: S066 requires a short 100 ms hold threshold for the VOICE overlay,
 * and maintaining two separate hold constants invites drift.
 *
 * Affiliates: config.h BUTTON_HOLD_DELAY_MS, buttonHandler_tick(),
 * buttonHandler_setTimeraction().
 */
#define BUTTON_TIMEOUT  BUTTON_HOLD_DELAY_MS
```

### 1.3 buttonHandler.c — MODIFY line 423: timer deadline uses common constant

**File:** `Core/Hardware/frontPanel/buttonHandler.c`  **Line:** 423
**Operation:** MODIFY — no text change needed if the alias is kept. The
existing `BUTTON_TIMEOUT` resolves through the alias to
`BUTTON_HOLD_DELAY_MS`. Verify that the hold semantics (now 100 ms
instead of 500 ms) are acceptable for all existing long-press gestures.

**Risk note:** the STEP-mode and VOICE-mode long-press timer both used
500 ms. Reducing to 100 ms makes all long-press gestures activate
faster. If this causes unintended short-tap misdetection in STEP mode,
an independent `BUTTON_TIMEOUT_STEP` may be needed — but the spec
explicitly calls for one common threshold.

---

## Step 2 — Font Table (496 bytes flash)

### 2.1 lcd.h — ADD underline glyph helper declaration after line 153

**File:** `Core/Hardware/frontPanel/lcd.h`  **Line:** after 153
**Operation:** ADD

```c
/*
 * Produce an underlined CGRAM glyph from a standard ASCII character.
 *
 * What: copies the 8-row WS0010 English/Japanese ROM bitmap for the given
 * alphanumeric character into out[8], then sets row 7 (the bottom pixel
 * row) to 0x1F (all 5 dots on) to produce a visible underline. Returns 1
 * on success, 0 if the character is outside the supported set (0-9, A-Z,
 * a-z). The caller must not request punctuation, whitespace, or any
 * non-alphanumeric character.
 *
 * Why: the WS0010 internal character ROM is write-only from firmware; its
 * glyph bitmaps cannot be read back at runtime. This function sources its
 * data from a compiled 496-byte const flash table (62 glyphs × 8 bytes)
 * matching the English/Japanese font selected by the current LCD
 * initialization path. The underline row is applied after the copy so the
 * base glyph data remains unmodified in flash.
 *
 * Inputs: ascii_char — the rendered display character to underline (must
 * be alphanumeric). out — pointer to an 8-byte buffer for the CGRAM row
 * image. Outputs: out[0..7] filled with the underlined glyph, or left
 * unmodified on rejection. Returns 1 (valid) or 0 (rejected).
 *
 * Affiliates: lcd_define_char() (programs the CGRAM slot with the result),
 * menu.c four-slot underline cache (slots 2..5), WS0010 English/Japanese
 * ROM font table.
 */
uint8_t lcd_underlineGlyph(uint8_t ascii_char, uint8_t out[8]);
```

### 2.2 lcd.c — ADD font table and helper implementation

**File:** `Core/Hardware/frontPanel/lcd.c`  **Line:** after line 150
(after splash character definitions, before lcd_tim7_init)
**Operation:** ADD

```c
/*
 * WS0010 English/Japanese ROM font bitmaps for underline glyph synthesis.
 *
 * What: 62 alphanumeric glyph definitions (0-9, A-Z, a-z), each stored as
 * 8 bytes matching the CGRAM row format required by lcd_define_char().
 * Total: 496 bytes of const flash. The bitmaps are sourced from the WS0010
 * English/Japanese font table selected by the current lcd_init() function-
 * set command, NOT from a merely compatible HD44780 font whose glyph shapes
 * may differ subtly (serifs, descender heights, stroke widths).
 *
 * Why: the WS0010 internal character ROM cannot be read by firmware; the
 * only way to produce an underlined variant of a ROM character is to
 * program a CGRAM slot with the base bitmap plus a bottom-row underline.
 * The lookup is arithmetic over three contiguous ASCII ranges: digits
 * '0'..'9' (10 glyphs), uppercase 'A'..'Z' (26 glyphs), lowercase
 * 'a'..'z' (26 glyphs). No per-character index table is needed.
 *
 * Inputs: indexed by lcd_underlineGlyph() via arithmetic mapping.
 * Outputs: 8 bytes per glyph consumed by lcd_define_char().
 * Affiliates: lcd_underlineGlyph(), menu.c CGRAM cache (slots 2..5).
 *
 * Risk: if a future name/value formatter places punctuation at a marker
 * edge, leave that character un-underlined until its UX is specified rather
 * than silently growing this table.
 */
static const uint8_t lcd_font_alpha_numeric[62][8] = {
    /* '0' */ { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E, 0x00 },
    /* '1' */ { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E, 0x00 },
    /* '2' */ { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F, 0x00 },
    /* '3' */ { 0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E, 0x00 },
    /* '4' */ { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02, 0x00 },
    /* '5' */ { 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E, 0x00 },
    /* '6' */ { 0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E, 0x00 },
    /* '7' */ { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08, 0x00 },
    /* '8' */ { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E, 0x00 },
    /* '9' */ { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C, 0x00 },
    /* 'A' */ { 0x0E, 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x00 },
    /* 'B' */ { 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E, 0x00 },
    /* 'C' */ { 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E, 0x00 },
    /* 'D' */ { 0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C, 0x00 },
    /* 'E' */ { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F, 0x00 },
    /* 'F' */ { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10, 0x00 },
    /* 'G' */ { 0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F, 0x00 },
    /* 'H' */ { 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11, 0x00 },
    /* 'I' */ { 0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E, 0x00 },
    /* 'J' */ { 0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C, 0x00 },
    /* 'K' */ { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11, 0x00 },
    /* 'L' */ { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F, 0x00 },
    /* 'M' */ { 0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11, 0x00 },
    /* 'N' */ { 0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x00 },
    /* 'O' */ { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E, 0x00 },
    /* 'P' */ { 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10, 0x00 },
    /* 'Q' */ { 0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D, 0x00 },
    /* 'R' */ { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11, 0x00 },
    /* 'S' */ { 0x0E, 0x11, 0x10, 0x0E, 0x01, 0x11, 0x0E, 0x00 },
    /* 'T' */ { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x00 },
    /* 'U' */ { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E, 0x00 },
    /* 'V' */ { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04, 0x00 },
    /* 'W' */ { 0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11, 0x00 },
    /* 'X' */ { 0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11, 0x00 },
    /* 'Y' */ { 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04, 0x00 },
    /* 'Z' */ { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F, 0x00 },
    /* 'a' */ { 0x00, 0x00, 0x0E, 0x01, 0x0F, 0x11, 0x0F, 0x00 },
    /* 'b' */ { 0x10, 0x10, 0x16, 0x19, 0x11, 0x11, 0x1E, 0x00 },
    /* 'c' */ { 0x00, 0x00, 0x0E, 0x10, 0x10, 0x11, 0x0E, 0x00 },
    /* 'd' */ { 0x01, 0x01, 0x0D, 0x13, 0x11, 0x11, 0x0F, 0x00 },
    /* 'e' */ { 0x00, 0x00, 0x0E, 0x11, 0x1F, 0x10, 0x0E, 0x00 },
    /* 'f' */ { 0x06, 0x09, 0x08, 0x1C, 0x08, 0x08, 0x08, 0x00 },
    /* 'g' */ { 0x00, 0x00, 0x0F, 0x11, 0x0F, 0x01, 0x0E, 0x00 },
    /* 'h' */ { 0x10, 0x10, 0x16, 0x19, 0x11, 0x11, 0x11, 0x00 },
    /* 'i' */ { 0x04, 0x00, 0x0C, 0x04, 0x04, 0x04, 0x0E, 0x00 },
    /* 'j' */ { 0x02, 0x00, 0x06, 0x02, 0x02, 0x12, 0x0C, 0x00 },
    /* 'k' */ { 0x10, 0x10, 0x12, 0x14, 0x18, 0x14, 0x12, 0x00 },
    /* 'l' */ { 0x0C, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E, 0x00 },
    /* 'm' */ { 0x00, 0x00, 0x1A, 0x15, 0x15, 0x11, 0x11, 0x00 },
    /* 'n' */ { 0x00, 0x00, 0x16, 0x19, 0x11, 0x11, 0x11, 0x00 },
    /* 'o' */ { 0x00, 0x00, 0x0E, 0x11, 0x11, 0x11, 0x0E, 0x00 },
    /* 'p' */ { 0x00, 0x00, 0x1E, 0x11, 0x1E, 0x10, 0x10, 0x00 },
    /* 'q' */ { 0x00, 0x00, 0x0D, 0x13, 0x0F, 0x01, 0x01, 0x00 },
    /* 'r' */ { 0x00, 0x00, 0x16, 0x19, 0x10, 0x10, 0x10, 0x00 },
    /* 's' */ { 0x00, 0x00, 0x0E, 0x10, 0x0E, 0x01, 0x1E, 0x00 },
    /* 't' */ { 0x08, 0x08, 0x1C, 0x08, 0x08, 0x09, 0x06, 0x00 },
    /* 'u' */ { 0x00, 0x00, 0x11, 0x11, 0x11, 0x13, 0x0D, 0x00 },
    /* 'v' */ { 0x00, 0x00, 0x11, 0x11, 0x11, 0x0A, 0x04, 0x00 },
    /* 'w' */ { 0x00, 0x00, 0x11, 0x11, 0x15, 0x15, 0x0A, 0x00 },
    /* 'x' */ { 0x00, 0x00, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x00 },
    /* 'y' */ { 0x00, 0x00, 0x11, 0x11, 0x0F, 0x01, 0x0E, 0x00 },
    /* 'z' */ { 0x00, 0x00, 0x1F, 0x02, 0x04, 0x08, 0x1F, 0x00 },
};

/*
 * Map an ASCII character to its index in lcd_font_alpha_numeric[].
 *
 * Returns 0..61 for valid alphanumeric input, or 0xFF for anything else.
 * The three contiguous ASCII ranges are mapped arithmetically:
 *   '0'..'9' → 0..9
 *   'A'..'Z' → 10..35
 *   'a'..'z' → 36..61
 */
static uint8_t lcd_fontIndex(uint8_t ch)
{
    if (ch >= '0' && ch <= '9') return (uint8_t)(ch - '0');
    if (ch >= 'A' && ch <= 'Z') return (uint8_t)(ch - 'A' + 10u);
    if (ch >= 'a' && ch <= 'z') return (uint8_t)(ch - 'a' + 36u);
    return 0xFFu;
}

uint8_t lcd_underlineGlyph(uint8_t ascii_char, uint8_t out[8])
{
    uint8_t idx = lcd_fontIndex(ascii_char);
    if (idx == 0xFFu)
        return 0u;
    memcpy(out, lcd_font_alpha_numeric[idx], 8u);
    out[7] = 0x1Fu;
    return 1u;
}
```

**Risk:** The glyph data above is the standard WS0010 English/Japanese
table. Before merging, verify each row against the datasheet bitmap for
the selected ROM. If any glyph differs from the physical display, the
underlined character will look wrong but the system will not crash.

---

## Step 3 — Four-Slot CGRAM Cache and Transaction Writer

### 3.1 menu.c — ADD CGRAM cache state after line 1164

**File:** `Core/Menu/menu.c`  **Line:** after 1164
(after `menu_stepAutoNumberLocked`)
**Operation:** ADD

```c
/*
 * VOICE-page held-step automation overlay state (40 B static SRAM).
 *
 * What: the complete retained state for the overlay described in
 * S065_DYN_PAT_VOICE_PARAM_UX.md. It comprises five sub-blocks:
 *
 *   1. Held-step selection (20 B): 16-bit mask of physically held SEQ
 *      buttons, a 16-byte press-order ring recording which buttons are
 *      held and in what order (newest-first resolution), a held count,
 *      and the overlay-active flag.
 *   2. Pattern-wide async search (12 B): the track and Pattern being
 *      scanned, a step cursor 0..128, a completion flag, and a 64-bit
 *      (8-byte) descriptor-presence bitmask indexed by descriptor_index.
 *   3. Four-slot CGRAM underline cache (5 B): one byte per slot 2..5
 *      recording the ASCII base character currently loaded, plus a
 *      4-bit valid mask.
 *   4. Value-underline debounce (3 B): a 16-bit last-edit timestamp and
 *      a 4-bit suppression mask (one bit per visible parameter).
 *
 * Why: S066 introduces a real-time overlay on top of the existing VOICE
 * page rendering. All state is Menu-owned (foreground lifetime) because
 * Menu drives the LCD, LED, and parameter commit paths. No ISR writes
 * to any of these variables.
 *
 * Inputs: buttonHandler SEQ button press/release edges, pot/encoder
 * deltas, foreground service ticks.
 * Outputs: modified editDisplayBuffer content, CGRAM slot definitions
 * via lcd_define_char(), LED state via led_setValue(), and Pattern pool
 * writes via pat_writeStepAutomation().
 *
 * Affiliates: buttonHandler_seqHeldMask(), pat_readStepAutomations(),
 * pat_writeStepAutomation(), lcd_underlineGlyph(), lcd_define_char(),
 * lcd_queueFree(), led_setValue(), led_updatePatternTrackView(),
 * scene_instrumentSlotConst(), instrumentParam_make(),
 * instrumentManager_descriptor(), instrumentManager_targetValid(),
 * time_sysTick, seq_isRunning().
 *
 * Budget: exactly 40 bytes .bss, approved in S066 spec §10. Any
 * increase requires a new user acknowledgement per the RAM Allocation
 * Approval Policy.
 */

/* --- Sub-block 1: held-step selection (20 B) --- */

/* 16-bit mask: bit N set when BUT_SEQn+1 is physically held. Converted
 * to absolute step indices via buttonHandler_visibleStep(). */
static uint16_t va_heldMask = 0u;

/* Press-order ring: va_heldOrder[0] is the most recently pressed, still-
 * held button index (0..15); va_heldCount entries are valid. Used for
 * newest-to-oldest walk when resolving per-parameter value sources. */
static uint8_t va_heldOrder[16];
static uint8_t va_heldCount = 0u;

/* Nonzero when the overlay is active (at least one SEQ button has been
 * held past BUTTON_HOLD_DELAY_MS on a VOICE page in SELECT_MODE_VOICE). */
static uint8_t va_overlayActive = 0u;

/* --- Sub-block 2: Pattern-wide async search (12 B) --- */

/* Track and Pattern currently being scanned. A mismatch with the live
 * active track/Pattern triggers a restart. */
static uint8_t va_searchTrack = 0u;
static uint8_t va_searchPattern = 0u;

/* Next step to examine (0..127). When it reaches 128, the scan is
 * complete and va_searchComplete is set. */
static uint8_t va_searchCursor = 0u;
static uint8_t va_searchComplete = 0u;

/* 64-bit descriptor-presence bitmask. Bit N set means "at least one step
 * on va_searchTrack has automation targeting instrumentParam_make(slot,
 * N) where slot = menu_voicePageToSlot(menu_activePage)." Covers all
 * automatable descriptors for the current voice type without restarting
 * on SELECT-page or screen changes. */
static uint8_t va_searchTargetMask[8];

/* --- Sub-block 3: CGRAM underline cache (5 B) --- */

/* Per-slot base character. va_cgramBase[i] records the ASCII character
 * currently loaded into CGRAM slot (2 + i). If the slot is invalid or
 * unused, the corresponding bit in va_cgramValid is clear. */
static uint8_t va_cgramBase[4];

/* Bit N set when CGRAM slot (2 + N) contains a valid underlined glyph
 * matching va_cgramBase[N]. */
static uint8_t va_cgramValid = 0u;

/* --- Sub-block 4: value-underline debounce (3 B) --- */

/* Timestamp of the most recent held-step automation value edit. Used
 * with VOICE_AUTOMATION_UNDERLINE_QUIET_MS and wrap-safe uint16_t
 * subtraction against time_sysTick. */
static uint16_t va_lastEditTick = 0u;

/* Bit N set when parameter N's value underline is suppressed after a
 * recent edit. Cleared by the foreground debounce service after the
 * quiet period expires. */
static uint8_t va_underlineSuppressed = 0u;
```

_Static_assert to verify budget:
```c
/* Compile-time proof: overlay state fits the approved 40-byte budget.
 * 2 (mask) + 16 (order) + 1 (count) + 1 (active) = 20
 * 1 + 1 + 1 + 1 + 8 = 12
 * 4 + 1 = 5
 * 2 + 1 = 3
 * Total = 40 bytes. */
_Static_assert(
    sizeof(va_heldMask) + sizeof(va_heldOrder) + sizeof(va_heldCount) +
    sizeof(va_overlayActive) + sizeof(va_searchTrack) +
    sizeof(va_searchPattern) + sizeof(va_searchCursor) +
    sizeof(va_searchComplete) + sizeof(va_searchTargetMask) +
    sizeof(va_cgramBase) + sizeof(va_cgramValid) +
    sizeof(va_lastEditTick) + sizeof(va_underlineSuppressed) == 40u,
    "S066 overlay state must be exactly 40 bytes (.bss budget)");
```

### 3.2 menu.c — ADD CGRAM transaction helper functions

**File:** `Core/Menu/menu.c`  **Line:** after the state block above
**Operation:** ADD

```c
/*
 * Reserve the CGRAM slot range for the automation underline cache.
 *
 * What: the four-entry cache owns CGRAM slots 2..5. Slots 0 (ellipsis)
 * and 1 (pop) are never touched. Slots 6 and 7 remain available for
 * future runtime use. The splash animation (SplashAnimation.c) uses
 * slots 2..7 at boot only; by the time the main loop starts, those
 * definitions are dead.
 */
#define VA_CGRAM_SLOT_BASE  2u
#define VA_CGRAM_SLOT_COUNT 4u

/*
 * Program one CGRAM slot with an underlined glyph and update the cache.
 *
 * What: if the requested base character is already loaded in the target
 * slot, this is a no-op. Otherwise it calls lcd_underlineGlyph() to
 * build the glyph data, then lcd_define_char() to program the slot.
 * The cache entry and valid bit are updated. If lcd_underlineGlyph()
 * rejects the character (non-alphanumeric), the slot is invalidated
 * and the function returns 0.
 *
 * Inputs: param_index (0..3) selects which of the four cache slots to
 * use. base_char is the ASCII character to underline.
 * Outputs: returns the CGRAM slot code (2..5) on success, or 0xFF if
 * the character is not underlineable.
 *
 * Affiliates: lcd_underlineGlyph(), lcd_define_char(), va_cgramBase[],
 * va_cgramValid.
 */
static uint8_t va_cgramLoadSlot(uint8_t param_index, uint8_t base_char)
{
    uint8_t slot;
    uint8_t glyph[8];

    if (param_index >= VA_CGRAM_SLOT_COUNT)
        return 0xFFu;
    slot = (uint8_t)(VA_CGRAM_SLOT_BASE + param_index);

    if ((va_cgramValid & (uint8_t)(1u << param_index)) &&
        va_cgramBase[param_index] == base_char)
        return slot;

    if (!lcd_underlineGlyph(base_char, glyph)) {
        va_cgramValid &= (uint8_t)~(1u << param_index);
        return 0xFFu;
    }

    lcd_define_char(slot, glyph);
    va_cgramBase[param_index] = base_char;
    va_cgramValid |= (uint8_t)(1u << param_index);
    return slot;
}

/*
 * Invalidate the CGRAM cache (e.g., on VOICE page exit or context change).
 *
 * What: clears the valid mask so no slot is considered current. The
 * CGRAM definitions themselves stay in hardware but ordinary pages do
 * not emit their slot codes, so stale definitions are harmless.
 */
static void va_cgramInvalidate(void)
{
    va_cgramValid = 0u;
}

/*
 * Estimate the LCD queue cost for one complete CGRAM underline transaction.
 *
 * What: one lcd_define_char() call enqueues 10 ops (1 CGRAM-address
 * command + 8 data bytes + 1 DDRAM-return command). Additionally, the
 * caller must emit lcd_setcursor + lcd_data for each DDRAM cell that
 * references the slot (2 ops per cell). In the worst case, four slots
 * are redefined and four DDRAM cells are updated: 4×10 + 4×2 = 48 ops.
 * The preflight checks lcd_queueFree() >= this count before starting.
 *
 * Returns: the number of LCD queue ops needed for the given number of
 * CGRAM redefines and DDRAM cell writes.
 */
static uint8_t va_cgramTransactionCost(uint8_t redefines, uint8_t cells)
{
    return (uint8_t)(redefines * 10u + cells * 2u);
}
```

---

## Step 4 — Pattern-Wide Async Search

### 4.1 menu.c — ADD async search agent

**File:** `Core/Menu/menu.c`  **Line:** near the overlay state block
**Operation:** ADD

```c
/*
 * Restart the Pattern-wide automation search from step 0.
 *
 * What: resets the search cursor and result mask, records the current
 * track and Pattern as the search context, and clears the completion
 * flag. The search runs incrementally from menu_voiceAutoScanService()
 * in the foreground service loop.
 *
 * Why: must be called whenever the search context changes: VOICE page
 * entry, viewed Pattern change, Scene change, track change, or
 * automation deletion. Adding/updating an automation sets the
 * corresponding bit immediately and does not restart the scan.
 *
 * Inputs: none (reads menu_activeVoice, menu_shownPattern).
 * Outputs: resets va_search* state. The Pattern-wide name markers are
 * absent until the scan completes.
 * Affiliates: menu_voiceAutoScanService(), va_searchTargetMask[].
 */
static void va_searchRestart(void)
{
    va_searchTrack = menu_activeVoice;
    va_searchPattern = menu_shownPattern;
    va_searchCursor = 0u;
    va_searchComplete = 0u;
    memset(va_searchTargetMask, 0, sizeof(va_searchTargetMask));
}

/*
 * Set one bit in the Pattern-wide search result after a successful write.
 *
 * What: marks descriptor_index as "automation present on at least one step"
 * without restarting the scan. Safe to call at any time; adding an entry
 * can never invalidate any other set bit.
 *
 * Inputs: descriptor_index (0..63).
 * Outputs: sets va_searchTargetMask[descriptor_index / 8] bit.
 */
static void va_searchSetBit(uint8_t descriptor_index)
{
    if (descriptor_index < 64u)
        va_searchTargetMask[descriptor_index >> 3u] |=
            (uint8_t)(1u << (descriptor_index & 7u));
}

/*
 * Test one bit in the Pattern-wide search result.
 *
 * Returns nonzero if the descriptor_index has automation on at least one
 * step of the current track in the current Pattern (or if the search has
 * not yet completed, returns the partial result — possibly false-negative).
 */
static uint8_t va_searchTestBit(uint8_t descriptor_index)
{
    if (descriptor_index >= 64u)
        return 0u;
    return (uint8_t)(va_searchTargetMask[descriptor_index >> 3u] &
                     (uint8_t)(1u << (descriptor_index & 7u)));
}

/*
 * Foreground service: advance the async Pattern-wide automation search.
 *
 * What: processes up to VOICE_AUTOMATION_SCAN_STEPS_PER_PASS steps per
 * call. For each step, reads the automation entries and sets the
 * corresponding bits in va_searchTargetMask[]. When the cursor reaches
 * 128, sets va_searchComplete and stops. At the default budget of 4
 * steps per pass and a 63-entry ceiling, the worst-case cost is 252
 * target comparisons per foreground pass.
 *
 * Why: reading all 128 steps synchronously on every display update would
 * stall the foreground loop. The polled search amortises this cost over
 * ~32 service passes (~32 ms at 1 kHz).
 *
 * Inputs: va_searchCursor, va_searchTrack, va_searchPattern.
 * Outputs: va_searchTargetMask[] bits, va_searchComplete flag.
 * Affiliates: pat_readStepAutomations(), instrumentParam_local(),
 * VOICE_AUTOMATION_SCAN_STEPS_PER_PASS (config.h).
 *
 * Call site: menu_serviceRuntimeWidgets() when a VOICE page is active.
 */
static void va_scanService(void)
{
    uint8_t budget;
    pat_automation_entry_t autos[63];
    uint8_t slot;

    if (va_searchComplete)
        return;
    if (va_searchTrack != menu_activeVoice ||
        va_searchPattern != menu_shownPattern) {
        va_searchRestart();
        return;
    }

    slot = menu_voicePageToSlot(menu_activePage);

    for (budget = 0u;
         budget < VOICE_AUTOMATION_SCAN_STEPS_PER_PASS &&
         va_searchCursor < 128u;
         budget++, va_searchCursor++) {
        uint8_t count = pat_readStepAutomations(
            va_searchPattern, va_searchTrack, va_searchCursor,
            autos, 63u);
        uint8_t j;
        for (j = 0u; j < count; j++) {
            if (instrumentParam_slot(autos[j].target) == slot)
                va_searchSetBit(instrumentParam_local(autos[j].target));
        }
    }
    if (va_searchCursor >= 128u)
        va_searchComplete = 1u;
}
```

### 4.2 menu.c — MODIFY menu_serviceRuntimeWidgets (line 8978)

**File:** `Core/Menu/menu.c`  **Line:** 8978-9011
**Operation:** MODIFY — add scan and debounce service calls

After the existing CPU-use widget service (which early-returns for
non-MIDI pages), add:

```c
    /*
     * VOICE overlay foreground services.
     *
     * What: advances the Pattern-wide automation search and expires the
     * value-underline quiet period. Both are lightweight polled tasks
     * that must run on every foreground service pass when a VOICE page
     * is active, independent of the CPU-use widget's refresh rate.
     *
     * Why: the async search amortises a 128-step scan into small per-pass
     * slices, and the debounce timer must run at the full 1 kHz service
     * rate to expire at the configured 100 ms resolution.
     *
     * Inputs: menu_activePage, va_overlayActive, time_sysTick.
     * Outputs: va_searchTargetMask[] advancement, underline reapplication.
     * Affiliates: va_scanService(), va_underlineService().
     */
    if (menu_isVoicePage(menu_activePage)) {
        va_scanService();
        if (va_overlayActive)
            va_underlineService();
    }
```

### 4.3 Scan invalidation hooks

**menu_switchPage()** (`menu.c:9986`): ADD `va_searchRestart()` call inside
the voice-page entry path (around line 10137), and `va_cgramInvalidate()`
on leaving a voice page.

**menu_setActiveVoice()** (`menu.c:10750`): ADD `va_searchRestart()` after
`menu_stepAutomationReset()` when the track changes.

**menu_setVoiceModeShowMorph()** (`menu.c:10759`): no search restart
needed (search covers all descriptors), but ADD a repaint request to
refresh the displayed values.

**Pattern/Scene change paths**: wherever `menu_shownPattern` or
`scene_getActiveIndex()` changes while a VOICE page is active, ADD
`va_searchRestart()`.

---

## Step 5 — Overlay Activation and Held-Step Tracking

### 5.1 buttonHandler.h — ADD export after line 65

**File:** `Core/Hardware/frontPanel/buttonHandler.h`  **Line:** after 65
**Operation:** ADD

```c
/*
 * Return the 16-bit mask of currently held SEQ buttons.
 *
 * What: bit N is set when BUT_SEQ(N+1) is physically held, as read from
 * the volatile btn_held[] array. The mask reflects the raw button state
 * without any overlay or timer processing.
 *
 * Why: the VOICE overlay in menu.c needs to detect held steps and
 * convert button indices to absolute step indices via
 * buttonHandler_visibleStep(). Providing a mask is cheaper than
 * exposing the entire btn_held[] array and encapsulates the non-
 * contiguous shift-register layout (BUT_SEQ1..16 are scattered across
 * four bytes: 32-35, 24-27, 16-19, 8-11).
 *
 * Inputs: btn_held[] (volatile, written by ISR button press/release).
 * Outputs: 16-bit mask. Bit 0 = BUT_SEQ1, bit 15 = BUT_SEQ16.
 * Affiliates: buttonHandler_visibleStep(), menu.c va_heldMask.
 */
uint16_t buttonHandler_seqHeldMask(void);

/*
 * Convert a SEQ button index (0..15) to an absolute step index (0..127).
 *
 * What: adds menu_currentBar * NUM_STEPS_PER_BAR to the button index.
 * Exported for use by the VOICE overlay in menu.c, which needs to
 * convert held-mask bits to absolute step indices for PatternData reads.
 *
 * Previously static; promoted to extern for S066.
 */
uint8_t buttonHandler_visibleStep(uint8_t seqButtonPressed);
```

### 5.2 buttonHandler.c — ADD seqHeldMask export

**File:** `Core/Hardware/frontPanel/buttonHandler.c`  **Line:** after line 132
**Operation:** ADD

```c
uint16_t buttonHandler_seqHeldMask(void)
{
    /*
     * Build a 16-bit mask from the scattered btn_held[] entries.
     *
     * BUT_SEQ1..4  = indices 32..35 → mask bits 0..3
     * BUT_SEQ5..8  = indices 24..27 → mask bits 4..7
     * BUT_SEQ9..12 = indices 16..19 → mask bits 8..11
     * BUT_SEQ13..16= indices  8..11 → mask bits 12..15
     */
    static const uint8_t seq_buttons[16] = {
        BUT_SEQ1,  BUT_SEQ2,  BUT_SEQ3,  BUT_SEQ4,
        BUT_SEQ5,  BUT_SEQ6,  BUT_SEQ7,  BUT_SEQ8,
        BUT_SEQ9,  BUT_SEQ10, BUT_SEQ11, BUT_SEQ12,
        BUT_SEQ13, BUT_SEQ14, BUT_SEQ15, BUT_SEQ16
    };
    uint16_t mask = 0u;
    uint8_t i;
    for (i = 0u; i < 16u; i++) {
        if (btn_held[seq_buttons[i]])
            mask |= (uint16_t)(1u << i);
    }
    return mask;
}
```

### 5.3 buttonHandler.c — MODIFY buttonHandler_visibleStep (line 239)

**File:** `Core/Hardware/frontPanel/buttonHandler.c`  **Line:** 239
**Operation:** MODIFY — remove `static` qualifier to make it externally
visible. The function body is unchanged.

Change:
```c
static uint8_t buttonHandler_visibleStep(uint8_t seqButtonPressed)
```
To:
```c
uint8_t buttonHandler_visibleStep(uint8_t seqButtonPressed)
```

### 5.4 buttonHandler.c — MODIFY buttonHandler_tick (lines 427-436)

**File:** `Core/Hardware/frontPanel/buttonHandler.c`  **Line:** 427-436
**Operation:** MODIFY — replace the non-wrap-safe `>` comparison with
wrap-safe unsigned subtraction, and make the hold threshold use
`BUTTON_HOLD_DELAY_MS`.

```c
void buttonHandler_tick(void)
{
    /*
     * Foreground poll for the short hold-vs-tap timer.
     *
     * What: when a SEQ button press arms the timer via
     * buttonHandler_setTimeraction(), this poll checks whether the
     * configured BUTTON_HOLD_DELAY_MS has elapsed. If so, it fires the
     * armed action (step automation editor in STEP/VOICE modes).
     *
     * The comparison uses wrap-safe unsigned subtraction against the
     * 16-bit time_sysTick counter (wraps every ~65.5 seconds). The old
     * code used a simple > comparison that was vulnerable to the
     * uint16_t wrap boundary.
     *
     * Inputs: time_sysTick (volatile, 1 kHz), buttonHandler_buttonTimer.
     * Outputs: calls buttonHandler_armTimerActionStep() when expired.
     * Affiliates: BUTTON_HOLD_DELAY_MS (config.h), time_sysTick.
     */
    if (buttonHandler_buttonTimerStepNr >= 0) {
        if ((uint16_t)(time_sysTick - buttonHandler_buttonTimer) <
            32768u) {
            buttonHandler_armTimerActionStep(buttonHandler_buttonTimerStepNr);
            buttonHandler_buttonTimerStepNr = TIMER_ACTION_OCCURED;
        }
    }
}
```

Note: the timer is now set as `time_sysTick + BUTTON_HOLD_DELAY_MS` in
`buttonHandler_setTimeraction()`, and we check if the elapsed time since
that deadline is < 32768 (positive half of uint16_t), meaning the
deadline has passed. This is the same wrap-safe pattern used elsewhere
in the codebase (config.h commentary at line 139).

### 5.5 buttonHandler.c — MODIFY seqButtonPressed/Released for VOICE overlay

**File:** `Core/Hardware/frontPanel/buttonHandler.c`  **Lines:** 509-567
**Operation:** MODIFY

In `buttonHandler_seqButtonPressed()`, the VOICE-mode non-SHIFT path
(line 527) currently calls `buttonHandler_setTimeraction()`. This must be
changed to notify the VOICE overlay instead:

```c
        case SELECT_MODE_VOICE:
            /*
             * VOICE mode SEQ press: arm the hold timer for overlay entry.
             *
             * What: arms the common short-hold timer. If the timer expires
             * (BUTTON_HOLD_DELAY_MS later), the overlay activates via
             * menu_voiceAutoOverlayEnter(). If the button is released before
             * expiry, the normal step-toggle occurs instead.
             *
             * The timer action path (buttonHandler_armTimerActionStep) is
             * replaced for VOICE mode by the overlay entry; the old
             * automation-arm behavior was for the STEP-mode per-step editor
             * and remains unchanged in STEP mode.
             */
            buttonHandler_setTimeraction(
                buttonHandler_visibleStep(seqButtonPressed));
            break;
```

The timer action must be modified to distinguish VOICE vs STEP mode.
In `buttonHandler_armTimerActionStep()` (line 325), the action currently
always arms the old automation editor. The overlay entry check must be
added:

```c
static void buttonHandler_armTimerActionStep(int8_t stepNr)
{
    if (bh_state.selectButtonMode == SELECT_MODE_VOICE) {
        /*
         * VOICE overlay activation: notify menu.c that a held-step
         * gesture is in progress. The overlay state (held mask, press
         * order) is managed by menu.c; buttonHandler only signals the
         * hold-threshold crossing. The step toggle is suppressed for
         * this button on release.
         */
        /* menu.c will poll buttonHandler_seqHeldMask() to detect the
         * transition; set a flag that the timer has fired so the release
         * path knows not to toggle. */
        return;
    }
    /* Original STEP-mode behavior: arm the per-step automation editor. */
    buttonHandler_armedAutomationStep = stepNr;
    led_setBlinkLed((uint8_t)(LED_STEP1 +
        ((uint8_t)stepNr % NUM_STEPS_PER_BAR)), 1);
}
```

In `buttonHandler_seqButtonReleased()` (line 555), the VOICE path must
suppress the step toggle when the overlay was active (timer had fired):

```c
    case SELECT_MODE_VOICE:
        if (buttonHandler_TimerActionOccured())
            return;  /* timer fired → overlay was entered, no toggle */
        buttonHandler_setRemoveStep(ledNr, seqButtonPressed);
        break;
```

This release path is unchanged in structure; the existing
`buttonHandler_TimerActionOccured()` check already handles the
suppression. The difference is that the timer now fires at 100 ms
instead of 500 ms, and `buttonHandler_armTimerActionStep()` no longer
arms the old per-step editor in VOICE mode.

### 5.6 menu.c — ADD overlay entry/exit and held-mask management

**File:** `Core/Menu/menu.c`  **Line:** near the overlay state block
**Operation:** ADD

```c
/*
 * Update the VOICE overlay held-step state from the physical button mask.
 *
 * What: called from the foreground service path (after
 * timebase_serviceFrontPanel has exchanged the shift registers). Reads
 * buttonHandler_seqHeldMask() and updates va_heldMask, va_heldOrder[],
 * va_heldCount, and va_overlayActive. New presses are inserted at the
 * front of va_heldOrder[]; releases remove the entry and compact the
 * array. The overlay activates when the first SEQ button's hold timer
 * fires (detected by TIMER_ACTION_OCCURED in buttonHandler), and
 * deactivates when all SEQ buttons are released.
 *
 * Why: the overlay must track the press order to resolve per-parameter
 * value sources from the newest matching held step. A simple mask is
 * insufficient because different parameters may source values from
 * different held steps.
 *
 * Inputs: buttonHandler_seqHeldMask(), va_heldMask (previous state).
 * Outputs: va_heldMask, va_heldOrder[], va_heldCount, va_overlayActive.
 * Affiliates: buttonHandler_seqHeldMask(), menu_voiceAutoResolveValues().
 *
 * Call site: menu_serviceRuntimeWidgets() or a dedicated overlay
 * service function, called once per foreground pass when a VOICE page
 * is active.
 */
static void va_updateHeldState(void)
{
    uint16_t newMask = buttonHandler_seqHeldMask();
    uint16_t pressed = (uint16_t)(newMask & ~va_heldMask);
    uint16_t released = (uint16_t)(va_heldMask & ~newMask);
    uint8_t i, j;

    /* Remove released buttons from the order array. */
    if (released) {
        for (i = 0u; i < va_heldCount; ) {
            if (released & (uint16_t)(1u << va_heldOrder[i])) {
                for (j = i; j + 1u < va_heldCount; j++)
                    va_heldOrder[j] = va_heldOrder[j + 1u];
                va_heldCount--;
            } else {
                i++;
            }
        }
    }

    /* Insert newly pressed buttons at the front (newest first). */
    if (pressed) {
        for (i = 0u; i < 16u; i++) {
            if (!(pressed & (uint16_t)(1u << i)))
                continue;
            if (va_heldCount < 16u) {
                memmove(&va_heldOrder[1], &va_heldOrder[0], va_heldCount);
                va_heldOrder[0] = i;
                va_heldCount++;
            }
        }
    }

    va_heldMask = newMask;

    if (newMask == 0u) {
        if (va_overlayActive) {
            va_overlayActive = 0u;
            va_underlineSuppressed = 0u;
            /* Restore normal step LEDs on overlay exit. */
            led_updatePatternTrackView(
                menu_activeVoice, menu_shownPattern, 0u, 0u);
        }
    }
    /* Overlay activation is set by the repaint path when a held mask is
     * nonzero and the hold timer has fired (detected via
     * TIMER_ACTION_OCCURED). See va_checkActivation(). */
}

/*
 * Reset all overlay state. Called on page exit, track change, etc.
 */
static void va_resetOverlay(void)
{
    va_heldMask = 0u;
    va_heldCount = 0u;
    va_overlayActive = 0u;
    va_underlineSuppressed = 0u;
    va_cgramInvalidate();
}
```

---

## Step 6 — Per-Parameter Held Value Resolution and Display

### 6.1 menu.c — ADD value resolution helper

**File:** `Core/Menu/menu.c`
**Operation:** ADD

```c
/*
 * Resolve the automation value for one displayed parameter from held steps.
 *
 * What: walks va_heldOrder[] from newest to oldest. For each held step,
 * reads its automation entries and checks whether the target matches the
 * given instrument_param_id_t. The first match is the value-source step
 * for this parameter. Returns the 7-bit automation value (0..127) via
 * *out_value and 1 on success, or 0 if no held step contains automation
 * for this target.
 *
 * Why: each displayed parameter independently resolves its own value-
 * source step. Different visible parameters may therefore show values
 * from different held steps. The match is specific to the exact
 * instrument_param_id_t (same voice slot AND same descriptor index).
 *
 * Performance: reads at most va_heldCount step automation lists (max 16).
 * Each step's list is read once and checked against the single target.
 * Total worst case: 16 steps × 63 entries = 1008 comparisons; in practice
 * far fewer because most steps have few entries and the walk stops at the
 * first match.
 *
 * Inputs: target (instrument_param_id_t), va_heldOrder[], va_heldCount.
 * Outputs: *out_value (7-bit), returns 1/0.
 * Affiliates: pat_readStepAutomations(), buttonHandler_visibleStep().
 */
static uint8_t va_resolveHeldValue(instrument_param_id_t target,
                                   uint8_t *out_value)
{
    pat_automation_entry_t autos[63];
    uint8_t i, j, count;
    uint8_t abs_step;

    for (i = 0u; i < va_heldCount; i++) {
        abs_step = buttonHandler_visibleStep(va_heldOrder[i]);
        count = pat_readStepAutomations(
            menu_shownPattern, menu_activeVoice, abs_step,
            autos, 63u);
        for (j = 0u; j < count; j++) {
            if (autos[j].target == target) {
                *out_value = autos[j].value;
                return 1u;
            }
        }
    }
    return 0u;
}

/*
 * Expand a 7-bit automation value to 8-bit for display.
 *
 * Uses the standard MIDI CC conversion: (v == 127) ? 255 : v * 2.
 * This matches MidiParser.c:312-319 and the inverse used at write time.
 */
static uint8_t va_expand7to8(uint8_t v7)
{
    return (v7 == 127u) ? 255u : (uint8_t)(v7 * 2u);
}
```

### 6.2 menu.c — MODIFY menu_repaintGeneric (lines 7582-7795)

**File:** `Core/Menu/menu.c`  **Lines:** 7746-7795 (overview branch)
and 7593-7745 (editMode branch)
**Operation:** MODIFY — after the normal render, apply underline markers.

At the END of the overview rendering (after line 7793, before the
closing `}`), ADD:

```c
        /*
         * Apply automation underline markers to the overview display.
         *
         * What: for each of the four visible parameters, determine whether
         * to underline the name (Pattern-wide presence) or the value
         * (held-step match). Held-step value markers have precedence per
         * parameter. When a held-step value marker is applied, the
         * displayed value is replaced with the automation value (after
         * 7-bit → 8-bit expansion) and the marker moves from the name
         * to the rightmost non-space character of the value.
         *
         * This code runs after the normal overview render has populated
         * editDisplayBuffer with short names (row 0) and values (row 1).
         *
         * The value-marker invariant: whenever a value is underlined, the
         * displayed value IS the automation value from the value-source
         * step. It is never an endpoint value. Conversely, an endpoint
         * value is never underlined as a held-step value.
         *
         * Inputs: editDisplayBuffer (already rendered), va_overlayActive,
         * va_searchComplete, va_searchTargetMask[], va_heldOrder[].
         * Outputs: editDisplayBuffer characters replaced with CGRAM slot
         * codes (0x02..0x05), CGRAM slots programmed via va_cgramLoadSlot().
         * Affiliates: va_resolveHeldValue(), va_expand7to8(),
         * va_searchTestBit(), va_cgramLoadSlot().
         */
        if (menu_isVoicePage(menu_activePage)) {
            uint8_t slot_idx = menu_voicePageToSlot(menu_activePage);
            uint8_t redefines = 0u;
            uint8_t cells = 0u;

            /* First pass: count how many CGRAM operations we need. */
            for (i = 0u; i < 4u; i++) {
                menu_cell_t cell_i = menu_resolveCell(activePage,
                    (uint8_t)(i + is2ndPage));
                if (cell_i.kind != MENU_CELL_INSTRUMENT)
                    continue;

                uint8_t v7;
                instrument_param_id_t target =
                    instrumentParam_make(slot_idx, cell_i.descriptor_index);

                if (va_overlayActive &&
                    va_resolveHeldValue(target, &v7)) {
                    /* Held-step value marker: rightmost non-space of value. */
                    uint8_t expanded = va_expand7to8(v7);
                    char val3[4];
                    numtostrpu(val3, expanded, ' ');
                    /* Find rightmost non-space in value. */
                    int8_t rc;
                    for (rc = 2; rc >= 0 && val3[rc] == ' '; rc--) {}
                    if (rc >= 0) {
                        uint8_t base = (uint8_t)val3[rc];
                        if (!(va_cgramValid & (uint8_t)(1u << i)) ||
                            va_cgramBase[i] != base)
                            redefines++;
                        cells++;
                    }
                } else if (va_searchComplete &&
                           va_searchTestBit(cell_i.descriptor_index)) {
                    /* Pattern-wide name marker: leftmost non-space of name. */
                    uint8_t col = (uint8_t)(4u * i);
                    int8_t nc;
                    for (nc = 0; nc < 3 &&
                         editDisplayBuffer[0][col + nc] == ' '; nc++) {}
                    if (nc < 3) {
                        uint8_t base =
                            (uint8_t)editDisplayBuffer[0][col + nc];
                        if (!(va_cgramValid & (uint8_t)(1u << i)) ||
                            va_cgramBase[i] != base)
                            redefines++;
                        cells++;
                    }
                }
            }

            /* Preflight the LCD queue for the complete transaction. */
            if (cells > 0u &&
                va_cgramTransactionCost(redefines, cells) <=
                    lcd_queueFree()) {

                /* Second pass: apply markers. */
                for (i = 0u; i < 4u; i++) {
                    menu_cell_t cell_i = menu_resolveCell(activePage,
                        (uint8_t)(i + is2ndPage));
                    if (cell_i.kind != MENU_CELL_INSTRUMENT)
                        continue;

                    uint8_t v7;
                    instrument_param_id_t target =
                        instrumentParam_make(slot_idx,
                                             cell_i.descriptor_index);

                    if (va_overlayActive &&
                        !(va_underlineSuppressed & (uint8_t)(1u << i)) &&
                        va_resolveHeldValue(target, &v7)) {
                        /* Replace value with automation value. */
                        uint8_t expanded = va_expand7to8(v7);
                        char val3[4];
                        numtostrpu(val3, expanded, ' ');
                        memcpy(&editDisplayBuffer[1][4u * i], val3, 3);

                        /* Underline rightmost non-space of value. */
                        int8_t rc;
                        for (rc = 2; rc >= 0 && val3[rc] == ' '; rc--) {}
                        if (rc >= 0) {
                            uint8_t cgslot = va_cgramLoadSlot(
                                i, (uint8_t)val3[rc]);
                            if (cgslot != 0xFFu)
                                editDisplayBuffer[1][4u * i + rc] =
                                    (char)cgslot;
                        }
                    } else if (va_overlayActive &&
                               va_resolveHeldValue(target, &v7)) {
                        /* Value exists but underline suppressed: show
                         * automation value in ROM characters only. */
                        uint8_t expanded = va_expand7to8(v7);
                        char val3[4];
                        numtostrpu(val3, expanded, ' ');
                        memcpy(&editDisplayBuffer[1][4u * i], val3, 3);
                    } else if (va_searchComplete &&
                               va_searchTestBit(
                                   cell_i.descriptor_index)) {
                        /* Pattern-wide name marker: leftmost non-space. */
                        uint8_t col = (uint8_t)(4u * i);
                        int8_t nc;
                        for (nc = 0; nc < 3 &&
                             editDisplayBuffer[0][col + nc] == ' ';
                             nc++) {}
                        if (nc < 3) {
                            uint8_t cgslot = va_cgramLoadSlot(
                                i,
                                (uint8_t)editDisplayBuffer[0][col + nc]);
                            if (cgslot != 0xFFu)
                                editDisplayBuffer[0][col + nc] =
                                    (char)cgslot;
                        }
                    }
                }
            }
        }
```

A similar but simpler block must be added to the single-parameter
(editModeActive) branch, where only one parameter is visible and only
one CGRAM slot (slot 2) is needed.

---

## Step 7 — Debounced Underline Reapplication

### 7.1 menu.c — ADD underline debounce service

**File:** `Core/Menu/menu.c`
**Operation:** ADD

```c
/*
 * Foreground service: expire the value-underline quiet period.
 *
 * What: after a held-step automation value is edited by a pot or encoder
 * delta, the value character is repainted immediately in ROM characters
 * (no underline) and the parameter's bit is set in va_underlineSuppressed.
 * This service checks whether VOICE_AUTOMATION_UNDERLINE_QUIET_MS has
 * elapsed since the last edit. If so, it clears the suppression mask,
 * re-resolves the current source values, and requests one repaint so the
 * underline reappears with the final stable value character.
 *
 * Why: repeated pot/encoder edits can change the underlined value character
 * on every detent. Reprogramming CGRAM on every edit would churn the async
 * LCD queue. The quiet period batches edits into one CGRAM update.
 *
 * Inputs: va_underlineSuppressed, va_lastEditTick, time_sysTick.
 * Outputs: clears va_underlineSuppressed, triggers menu_repaint().
 * Affiliates: VOICE_AUTOMATION_UNDERLINE_QUIET_MS (config.h), time_sysTick.
 *
 * Call site: menu_serviceRuntimeWidgets() when va_overlayActive is set.
 */
static void va_underlineService(void)
{
    if (va_underlineSuppressed == 0u)
        return;

    /* Validate context: if page, track, Pattern, or overlay changed,
     * discard the stale debounce request. */
    if (!menu_isVoicePage(menu_activePage) || !va_overlayActive) {
        va_underlineSuppressed = 0u;
        return;
    }

    if ((uint16_t)(time_sysTick - va_lastEditTick) >=
        VOICE_AUTOMATION_UNDERLINE_QUIET_MS) {
        va_underlineSuppressed = 0u;
        /* CGRAM slots will be updated on the next repaint pass. */
        menu_repaint();
    }
}
```

---

## Step 8 — Step Illumination

### 8.1 ledHandler.h — ADD automation step view function

**File:** `Core/Hardware/frontPanel/ledHandler.h`  **Line:** after 240
**Operation:** ADD

```c
/*
 * Set SEQ button LEDs to show automation presence for one parameter.
 *
 * What: for each of the 16 visible steps (current bar), lights the
 * corresponding STEP LED if that step carries an automation entry for
 * the given target on the given track and Pattern. Steps without
 * automation for that specific target are unlit. Every physically held
 * step (from heldMask) remains lit regardless, so the user's edit
 * selection is never hidden by the automation-presence LED mode.
 *
 * Why: when the VOICE overlay is active and the user is in single-
 * parameter view (encoder click-in), the SEQ LEDs switch from trigger-
 * based illumination to automation-presence illumination for the viewed
 * parameter. This gives immediate visual feedback about which steps
 * carry automation for the parameter being edited.
 *
 * Inputs: track (0..6), pattern (0..15, Scene index), target
 * (instrument_param_id_t), heldMask (16-bit, bit N = step N+bar*16 held).
 * Outputs: STEP1..STEP16 LED state via led_setValue().
 * Affiliates: pat_readStepAutomations(), led_setValue(),
 * led_updatePatternTrackView() (restores normal illumination on exit).
 */
void led_updateAutomationStepView(uint8_t track, uint8_t pattern,
                                  instrument_param_id_t target,
                                  uint16_t heldMask);
```

### 8.2 ledHandler.c — ADD implementation

**File:** `Core/Hardware/frontPanel/ledHandler.c`  **Line:** after
led_updatePatternTrackView (line 1132)
**Operation:** ADD

```c
void led_updateAutomationStepView(uint8_t track, uint8_t pattern,
                                  instrument_param_id_t target,
                                  uint16_t heldMask)
{
    uint8_t start = (uint8_t)(menu_currentBar * NUM_STEPS_PER_BAR);
    uint8_t i;
    pat_automation_entry_t autos[63];

    for (i = 0u; i < NUM_STEPS_PER_BAR; i++) {
        uint8_t abs_step = (uint8_t)(start + i);
        uint8_t on = 0u;

        /* Held steps are always lit. */
        if (heldMask & (uint16_t)(1u << i)) {
            on = 1u;
        } else {
            /* Check automation presence for this target. */
            uint8_t count = pat_readStepAutomations(
                pattern, track, abs_step, autos, 63u);
            uint8_t j;
            for (j = 0u; j < count; j++) {
                if (autos[j].target == target) {
                    on = 1u;
                    break;
                }
            }
        }
        led_setValue(on, (uint8_t)(LED_STEP1 + i));
    }
}
```

### 8.3 menu.c — ADD LED update calls in overlay path

When the overlay is active AND editModeActive (single-parameter view),
the repaint path must call `led_updateAutomationStepView()` with the
viewed parameter's target. On overlay exit or editMode exit, call
`led_updatePatternTrackView()` to restore normal illumination. These
calls are placed in the repaint and state-transition functions described
in Steps 5 and 6.

---

## Step 9 — Automation Write via Parameter Adjustment

### 9.1 menu.c — MODIFY menu_parseKnobDelta (line 8870)

**File:** `Core/Menu/menu.c`  **Lines:** 8892-8944
**Operation:** MODIFY — insert overlay write intercept before the normal
cell resolve

After line 8891 (the step automation early return) and before line 8893
(the normal cell resolve), ADD:

```c
    /*
     * VOICE overlay automation write intercept.
     *
     * What: when the overlay is active (at least one SEQ button held past
     * the hold threshold on a VOICE page), pot deltas are redirected to
     * write automation entries on every held step instead of committing
     * to the normal or Morph endpoint. The edit starts from the current
     * displayed automation value (if a value-source step exists) or from
     * the visible endpoint value (for first creation). The resulting
     * value is broadcast to all held steps. After at least one successful
     * write, the display updates to show the new automation value and the
     * value-underline quiet period is armed.
     *
     * Endpoint safety: this path must NEVER call menu_cellCommitValue(),
     * menu_sendEditedParameter(), preset_setInstrumentParameter(), or any
     * other endpoint/Autosave mutator. It calls only
     * pat_writeStepAutomation() for Pattern pool writes, and optionally
     * instrumentManager_writeRuntime() for stopped-playback DSP preview.
     *
     * Inputs: knobNr (0..3), delta, va_heldMask, va_heldOrder[].
     * Outputs: Pattern pool writes, display update, underline debounce.
     * Affiliates: pat_writeStepAutomation(), va_resolveHeldValue(),
     * va_expand7to8(), seq_isRunning(), menu_repaint().
     */
    if (va_overlayActive && menu_isVoicePage(menu_activePage)) {
        va_writeAutomationFromKnob(knobNr, delta);
        return;
    }
```

### 9.2 menu.c — ADD va_writeAutomationFromKnob helper

**File:** `Core/Menu/menu.c`
**Operation:** ADD

```c
/*
 * Write automation to all held steps from a pot delta.
 *
 * What: resolves the target parameter from the pot's column, reads its
 * current automation value (or endpoint fallback), applies the delta,
 * converts to 7-bit, and writes to every held step via
 * pat_writeStepAutomation(). Best-effort: failed steps are skipped
 * silently.
 *
 * Why: this is the Method 2 write path from S066 §6. It must bypass
 * every endpoint commit path and only touch the Pattern pool.
 *
 * Inputs: knobNr (0..3 matching the four visible parameters), delta.
 * Outputs: Pattern pool writes, va_underlineSuppressed, va_lastEditTick.
 * Affiliates: pat_writeStepAutomation(), va_resolveHeldValue(),
 * va_searchSetBit(), menu_repaint().
 */
static void va_writeAutomationFromKnob(uint8_t knobNr, int8_t delta)
{
    uint8_t slot_idx = menu_voicePageToSlot(menu_activePage);
    uint8_t activePage = (uint8_t)((menuIndex & MASK_PAGE) >> PAGE_SHIFT);
    menu_cell_t cell = menu_resolveCell(activePage, knobNr);
    instrument_param_id_t target;
    uint8_t v7;
    int16_t v8;
    uint8_t i, abs_step, wrote_any;

    if (cell.kind != MENU_CELL_INSTRUMENT)
        return;

    target = instrumentParam_make(slot_idx, cell.descriptor_index);
    if (target == INSTRUMENT_PARAM_INVALID)
        return;

    /* Seed value: from held-step automation if available, else endpoint. */
    if (va_resolveHeldValue(target, &v7)) {
        v8 = (int16_t)va_expand7to8(v7);
    } else {
        v8 = (int16_t)menu_cellDisplayValue(&cell);
    }

    /* Apply delta and clamp to 0..255. */
    v8 = (int16_t)(v8 + (int16_t)delta);
    if (v8 < 0) v8 = 0;
    if (v8 > 255) v8 = 255;

    /* Convert 8-bit → 7-bit for storage. */
    v7 = (uint8_t)((v8 >= 255) ? 127u : (uint8_t)((uint8_t)v8 / 2u));

    /* Write to every held step. */
    wrote_any = 0u;
    for (i = 0u; i < va_heldCount; i++) {
        abs_step = buttonHandler_visibleStep(va_heldOrder[i]);
        if (pat_writeStepAutomation(
                menu_shownPattern, menu_activeVoice,
                abs_step, target, v7))
            wrote_any = 1u;
    }

    if (wrote_any) {
        /* Update the Pattern-wide result bit immediately. */
        va_searchSetBit(cell.descriptor_index);

        /* Suppress the value underline and arm the quiet period. */
        va_underlineSuppressed |= (uint8_t)(1u << knobNr);
        va_lastEditTick = time_sysTick;

        /* Repaint to show the new value (in ROM characters). */
        menu_knobs_dirty = 1u;
    }
}
```

### 9.3 menu.c — MODIFY menu_encoderChangeParameter (line 7800)

**File:** `Core/Menu/menu.c`  **Lines:** 7800-7883
**Operation:** MODIFY — add overlay intercept at the top

After the initial cell resolve (line 7805), before the delta application,
ADD:

```c
    if (va_overlayActive && menu_isVoicePage(menu_activePage) &&
        cell.kind == MENU_CELL_INSTRUMENT) {
        /*
         * Encoder edit in single-parameter view with overlay active.
         *
         * Same write path as the pot intercept, but operates on the single
         * clicked-in parameter (activeParameter) instead of a pot column.
         * Uses the same va_writeAutomationFromKnob() helper with the
         * activeParameter index.
         */
        va_writeAutomationFromKnob(activeParameter % 4u, inc > 0 ? 1 : -1);
        menu_repaint();
        return;
    }
```

---

## Step 10 — Morph Integration

### 10.1 Value display path

The overlay display logic in Step 6 applies identically in both normal
and SHIFT+VOICE Morph modes because:

- The automation target ID is independent of the endpoint display mode
  (`instrumentParam_make(slot, descriptor_index)` is the same regardless
  of `voiceModeShowMorph`).
- The Pattern-wide search covers all descriptors for the track's voice,
  so switching Morph mode does not invalidate the search result.
- When a held-step match exists, the displayed value is the automation
  value, which is the same value regardless of Morph mode.
- When no held-step match exists, the normal `menu_cellDisplayValue()`
  path already switches between `instrument_parameters[]` and
  `morph_instrument_parameters[]` based on `voiceModeShowMorph`.

No additional code is needed for the display path.

### 10.2 Write path

The write path in Step 9 calls `pat_writeStepAutomation()` which writes
to the Pattern pool only. It never calls `menu_cellCommitValue()` or
any endpoint setter, so neither the normal nor Morph endpoint is modified.
This is correct in both normal and Morph display modes.

### 10.3 menu_setVoiceModeShowMorph — MODIFY (line 10759)

**File:** `Core/Menu/menu.c`  **Line:** 10759-10771
**Operation:** MODIFY — request repaint but do not restart search

```c
void menu_setVoiceModeShowMorph(uint8_t onOff)
{
    voiceModeShowMorph = (uint8_t)(onOff != 0u);
    menu_endlessPotMappingChanged();
    /*
     * Repaint to refresh non-overlay endpoint values. The Pattern-wide
     * search result and held-step overlay state are deliberately retained
     * because automation target identity is independent of the endpoint
     * display mode.
     */
    if (menu_isVoicePage(menu_activePage))
        menu_repaint();
}
```

---

## Step 11 — Integration and Edge Cases

### 11.1 menu_switchPage — MODIFY (line 9986)

**File:** `Core/Menu/menu.c`
**Operation:** MODIFY — add overlay reset and search control

In the voice-page entry path (around line 10134):
```c
    /* On entering a VOICE page, restart the Pattern-wide search and
     * reset any stale overlay state from the previous page. */
    va_resetOverlay();
    va_searchRestart();
```

On leaving a VOICE page (any non-voice page case):
```c
    /* On leaving a VOICE page, reset overlay and invalidate CGRAM. */
    if (menu_isVoicePage(prev_page)) {
        va_resetOverlay();
        va_cgramInvalidate();
    }
```

### 11.2 Bar change handler

When `menu_currentBar` changes while the overlay is active, the held-mask
button indices map to different absolute steps. The value resolution
runs against the new absolute steps on the next repaint, which happens
automatically. Step illumination (if in single-parameter view) must be
re-evaluated:

```c
    /* On bar change with overlay active: re-evaluate step LEDs. */
    if (va_overlayActive && editModeActive) {
        menu_cell_t cell = menu_resolveCell(activePage, activeParameter);
        if (cell.kind == MENU_CELL_INSTRUMENT) {
            instrument_param_id_t target =
                instrumentParam_make(menu_voicePageToSlot(menu_activePage),
                                     cell.descriptor_index);
            led_updateAutomationStepView(
                menu_activeVoice, menu_shownPattern,
                target, va_heldMask);
        }
    }
```

### 11.3 Pattern change / Scene change

When `menu_shownPattern` changes or `scene_getActiveIndex()` changes
while a VOICE page is active, restart the search and repaint. The held-
step value resolution automatically reads the new Pattern's pool blocks.

### 11.4 SELECT page change / screen change

When the user presses a SELECT button or scrolls the screen within a
VOICE page, the visible parameters change but the search result covers
all descriptors and does not need restarting. The repaint path
automatically re-evaluates which of the new visible parameters have
markers.

Step illumination in single-parameter view must be re-evaluated for the
new parameter. On editMode exit (encoder click-out), step LEDs must
restore to normal:

In `menu_parseEncoder()` (line 8640), where editModeActive toggles:
```c
    /* On editMode toggle with overlay active: update step LEDs. */
    if (va_overlayActive) {
        if (editModeActive) {
            /* Entering single-param view: show automation presence. */
            /* ... led_updateAutomationStepView() ... */
        } else {
            /* Exiting single-param view: restore normal step LEDs. */
            led_updatePatternTrackView(
                menu_activeVoice, menu_shownPattern, 0u, 0u);
        }
    }
```

### 11.5 Overlay exit cleanup

On `va_heldMask` reaching 0 (all SEQ buttons released):
1. `va_overlayActive` is cleared.
2. `va_underlineSuppressed` is cleared (cancel pending debounce).
3. Step LEDs are restored to normal trigger-based illumination.
4. CGRAM definitions may remain but ordinary pages stop emitting their
   slot codes (the repaint path only writes CGRAM slot codes when the
   overlay conditions are met).
5. A repaint is triggered to restore normal endpoint values and any
   Pattern-wide name markers.

This is handled in `va_updateHeldState()` (Step 5.6).

---

## Step 12 — Hardware Test Plan

No code changes. The following must be verified on the physical LCD:

1. Four simultaneous underline markers in overview (worst-case CGRAM).
2. Overview and clicked-in views in normal and SHIFT+VOICE Morph modes.
3. Different source steps per parameter (press SEQ1, then SEQ5 — if
   they automate different parameters, those parameters show different
   values).
4. Rapid pot sweeps: immediate value text update, one delayed underline
   reapplication after 100 ms quiet.
5. Step LEDs in single-parameter view: only automation-bearing steps lit.
6. Pattern-wide scan: name markers appear after ~32 ms scan completion.
7. Both endpoint images and Autosave dirty state unchanged after held-
   step edits (snapshot/compare Scene image before and after).
8. Playback running: no DSP preview, only Pattern writes.
9. Playback stopped: Pattern writes, optional DSP preview if implemented.
10. Async LCD queue busy: verify no torn CGRAM/DDRAM display.

---

## File Change Summary

| File | Operation | Lines Affected | Description |
|------|-----------|----------------|-------------|
| `config.h` | ADD | after 291 | Three timing/budget constants |
| `buttonHandler.h` | MODIFY | 23 | Replace BUTTON_TIMEOUT with alias |
| `buttonHandler.h` | ADD | after 65 | Two new exports: seqHeldMask, visibleStep |
| `buttonHandler.c` | MODIFY | 239 | Remove static from visibleStep |
| `buttonHandler.c` | ADD | after 132 | buttonHandler_seqHeldMask() |
| `buttonHandler.c` | MODIFY | 325-353 | armTimerActionStep: VOICE mode branches |
| `buttonHandler.c` | MODIFY | 427-436 | tick: wrap-safe comparison |
| `lcd.h` | ADD | after 153 | lcd_underlineGlyph declaration |
| `lcd.c` | ADD | after 150 | 496 B font table + helpers |
| `ledHandler.h` | ADD | after 240 | led_updateAutomationStepView declaration |
| `ledHandler.c` | ADD | after 1132 | led_updateAutomationStepView implementation |
| `menu.c` | ADD | after 1164 | 40 B overlay state (5 sub-blocks) |
| `menu.c` | ADD | — | CGRAM cache helpers (3 functions) |
| `menu.c` | ADD | — | Async search agent (5 functions) |
| `menu.c` | ADD | — | Held-value resolution (2 functions) |
| `menu.c` | ADD | — | Overlay held-state management (2 functions) |
| `menu.c` | ADD | — | Underline debounce service (1 function) |
| `menu.c` | ADD | — | Automation write from pot/encoder (1 function) |
| `menu.c` | MODIFY | 7746-7795 | repaintGeneric: add underline markers |
| `menu.c` | MODIFY | 7593-7745 | repaintGeneric editMode: add single marker |
| `menu.c` | MODIFY | 8870-8944 | parseKnobDelta: overlay write intercept |
| `menu.c` | MODIFY | 7800-7883 | encoderChangeParameter: overlay intercept |
| `menu.c` | MODIFY | 8978-9011 | serviceRuntimeWidgets: scan + debounce |
| `menu.c` | MODIFY | 9986-10179 | switchPage: search restart, overlay reset |
| `menu.c` | MODIFY | 10750-10754 | setActiveVoice: search restart |
| `menu.c` | MODIFY | 10759-10771 | setVoiceModeShowMorph: repaint |

---

## Resource Budget Verification

| Resource | Spec (§10) | This Schedule | Status |
|----------|------------|---------------|--------|
| Font table | 496 B flash | 496 B flash (62 × 8) | Match |
| Held selection | 20 B SRAM | 20 B (2+16+1+1) | Match |
| Pattern search | 12 B SRAM | 12 B (1+1+1+1+8) | Match |
| Underline cache | 5 B SRAM | 5 B (4+1) | Match |
| Debounce | 3 B SRAM | 3 B (2+1) | Match |
| CGRAM work buffer | 8 B stack | 8 B (local in lcd_underlineGlyph) | Match |
| Step automation read | 252 B stack | 252 B (pat_automation_entry_t[63]) | Match |
| **Total new static SRAM** | **40 B .bss** | **40 B .bss** | **Match** |

Compile-time `_Static_assert` in menu.c verifies the 40-byte bound.

---

## Implementation Dependencies

```
Step 1 (config constants) ──────────────────────────────┐
Step 2 (font table)  ─────────────────────┐             │
Step 3 (CGRAM cache) ←────────────────────┤             │
Step 4 (async search) ←───────────────────┼─────────────┤
Step 5 (overlay activation) ←─────────────┼─────────────┘
Step 6 (held value display) ←── Step 3 ←──┤←── Step 5
Step 7 (debounce) ←───────────── Step 6 ──┘
Step 8 (step illumination) ←── Step 5
Step 9 (automation write) ←── Step 5, Step 6
Step 10 (Morph integration) ←── Step 6, Step 9
Step 11 (edge cases) ←── all above
Step 12 (hardware test) ←── all above
```

Steps 1-4 can be implemented and tested independently before the overlay
activation in Step 5. Step 2 (font table) is the most labour-intensive
because the 62 glyph bitmaps must be verified against the actual WS0010
datasheet.
