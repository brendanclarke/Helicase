# Menu LFO Misnamed Audit

Date: 2026-07-10

## Scope

This audit traces the hihat LFO menu row that is still rendering `lfo_*` file
keys in the single-parameter view. The failed menu-side fallback was removed;
the current code path renders descriptor fields directly.

The audited hihat LFO row is present in both closed and open hihat layouts:

```c
{{ HIHAT_PARAM_LFO_RATE, HIHAT_PARAM_LFO_SYNC, HIHAT_PARAM_LFO_AMOUNT, HIHAT_PARAM_LFO_WAVE, HIHAT_PARAM_LFO_RETRIGGER_VOICE, HIHAT_PARAM_LFO_OFFSET, HIHAT_PARAM_LFO_TARGET_VOICE, HIHAT_PARAM_LFO_TARGET_PARAM }}
```

Sources:

- `Core/DSP/Instruments/HiHat/HiHatParameters.c:180`
- `Core/DSP/Instruments/HiHat/HiHatParameters.c:191`

## Removed Failed Workaround

Removed from `Core/Menu/menu.c`:

- `menu_descriptorLongName()`
- the edit-title call through that helper
- the descriptor-target full-display call through that helper

Removed from the instrument parameter files:

- the designated-initializer descriptor macro layer

Current descriptor source lines remain in the requested readable order:

```c
ROW("lfo_rate", "LFO", "Frequncy", "frq", ...)
```

Current macros map that source order into the existing `ParamDescriptor` ABI
layout:

```c
typedef struct {
    const char *file_key;
    const char *short_name;
    const char *long_name;
    const char *category;
    ...
} ParamDescriptor;
```

For hihat, `ROW(key_, cat_, long_, short_, ...)` expands to:

```c
{ key_, short_, long_, cat_, dtype_, FLAGS_IMAGE, ... }
```

So the intended compiled descriptor for `lfo_rate` is:

- `file_key`: `lfo_rate`
- `short_name`: `frq`
- `long_name`: `Frequncy`
- `category`: `LFO`

## Encoder Button Trace

`encoder_tick()` is not itself a menu action. It only samples/debounces the
encoder button into `button_val`.

1. `Core/Hardware/timebase.c:167` calls `encoder_tick()` from the TIM6 service.
2. `Core/Hardware/frontPanel/IO/encoder.c:234-241` reads `GPIOE_IDR`, derives
   PE15 active-low button state, and writes `button_val` after two matching
   samples.
3. `main.c:173-180` runs `main_encoder_check()` in the foreground loop.
4. `main_encoder_check()` reads:
   - rotation with `encode_read4()`
   - button state with `encode_readButton()`
5. If rotation changed or button state differs from `prevBtn`,
   `main_encoder_check()` calls `menu_parseEncoder(delta, btn)`.
6. `Core/Menu/menu.c:1999-2001` detects the button edge, and
   `Core/Menu/menu.c:2034-2035` toggles edit mode:
   - `button != lastEncoderButton` sets `btnClicked = button`
   - on the press, `btnClicked == 1`
   - `editModeActive = 1 - editModeActive`
7. `Core/Menu/menu.c:2059-2060` calls `menu_repaintAll()` for the click.
8. `Core/Menu/menu.c:1464-1470` clears display caches and calls
   `menu_repaint()`.
9. `Core/Menu/menu.c:1473-1480` calls `menu_repaintGeneric()` for normal pages,
   then `sendDisplayBuffer()`.
10. `Core/Menu/menu.c:1400-1447` writes changed characters from
    `editDisplayBuffer` to the LCD.

## LFO Cell Resolution Trace

Entering single-parameter view uses the existing `menuIndex`.

1. `Core/Menu/menu.c:1599-1604` computes:
   - `activeParameter = menuIndex & MASK_PARAMETER`
   - `activePage = (menuIndex & MASK_PAGE) >> PAGE_SHIFT`
2. For the LFO subpage, `activePage == 6`.
3. For the selected LFO cell, `activeParameter` is `0..7`.
4. `menu_repaintGeneric()` calls `menu_resolveCell(activePage, activeParameter)`.
5. `Core/Menu/menu.c:787-800` sees that `menu_activePage` is a voice page and:
   - maps `VOICE6_PAGE` to slot `5`
   - maps `VOICE7_PAGE` to slot `5`
   - fetches the active Scene slot with `scene_instrumentSlotConst()`
   - calls `instrumentManager_voicePageDescriptorIndex(slot->type,
     menu_activePage, subPage, position, &descriptor_index)`
6. `Core/DSP/Instruments/InstrumentManager.c:236-241` selects the instrument
   layout. For `INSTRUMENT_TYPE_HAT` on physical `VOICE7_PAGE`, it uses
   `hihat_open_menu_pages`; otherwise it uses `hihat_menu_pages`.
7. `Core/DSP/Instruments/InstrumentManager.c:243-257` reads:
   - `descriptor_index = pages[page].descriptor_index[position]`
   - returns `&entry->descriptors[descriptor_index]`

The hihat closed and open LFO pages use the same descriptor-index row, so both
resolve the same labels.

## Exact Single-Parameter Header Render

For instrument cells, `Core/Menu/menu.c:1637-1641` renders the edit header as:

```c
menu_copyPaddedField(&editDisplayBuffer[0][0],
                     cell.descriptor->category, 8u);
menu_copyPaddedField(&editDisplayBuffer[0][8],
                     cell.descriptor->long_name, 8u);
```

Therefore the expected top row for each hihat LFO position is:

| Position | Descriptor index | File key | Category | Long name | Expected top LCD row |
| --- | --- | --- | --- | --- | --- |
| 0 | `HIHAT_PARAM_LFO_RATE` | `lfo_rate` | `LFO` | `Frequncy` | `LFO     Frequncy` |
| 1 | `HIHAT_PARAM_LFO_SYNC` | `lfo_sync` | `LFO` | `ClockSnc` | `LFO     ClockSnc` |
| 2 | `HIHAT_PARAM_LFO_AMOUNT` | `lfo_amount` | `LFO` | `Amount` | `LFO     Amount  ` |
| 3 | `HIHAT_PARAM_LFO_WAVE` | `lfo_wave` | `LFO` | `Waveform` | `LFO     Waveform` |
| 4 | `HIHAT_PARAM_LFO_RETRIGGER_VOICE` | `lfo_retrigger_voice` | `LFO` | `Retriggr` | `LFO     Retriggr` |
| 5 | `HIHAT_PARAM_LFO_OFFSET` | `lfo_offset` | `LFO` | `Offset` | `LFO     Offset  ` |
| 6 | `HIHAT_PARAM_LFO_TARGET_VOICE` | `lfo_target_voice` | `LFO` | `DstVoice` | `LFO     DstVoice` |
| 7 | `HIHAT_PARAM_LFO_TARGET_PARAM` | `lfo_target_param` | `LFO` | `DstParam` | `LFO     DstParam` |

## Exact Value-Row Render

After the header, `menu_repaintGeneric()` renders row 2 by dtype:

- `HIHAT_PARAM_LFO_RATE`: `DTYPE_0B127`, numeric value at columns 13-15.
- `HIHAT_PARAM_LFO_SYNC`: `DTYPE_MENU | MENU_SYNC_RATES`, menu text at columns
  13-15.
- `HIHAT_PARAM_LFO_AMOUNT`: `DTYPE_0B127`, numeric value at columns 13-15.
- `HIHAT_PARAM_LFO_WAVE`: `DTYPE_MENU | MENU_LFO_WAVES`, menu text at columns
  13-15.
- `HIHAT_PARAM_LFO_RETRIGGER_VOICE`: `DTYPE_MENU | MENU_RETRIGGER`, menu text
  at columns 13-15.
- `HIHAT_PARAM_LFO_OFFSET`: `DTYPE_0B127`, numeric value at columns 13-15.
- `HIHAT_PARAM_LFO_TARGET_VOICE`: `DTYPE_VOICE_LFO`, numeric value at columns
  13-15.
- `HIHAT_PARAM_LFO_TARGET_PARAM`: `DTYPE_TARGET_SELECTION_LFO`, full target
  label rendered by `menu_displayInstrumentTargetFull(curParmVal)`.

For `DTYPE_TARGET_SELECTION_LFO`, `Core/Menu/menu.c:945-964` renders:

- `off` when the canonical target is invalid
- otherwise `VoiceN  ` plus the target descriptor's `long_name`

That target display also reads `descriptor->long_name` directly. It does not
read `descriptor->file_key`.

## What This Trace Proves

The current menu render path has no intentional use of `descriptor->file_key`
for the single-parameter edit header. The only source for the observed
`lfo_*` text in that header should be one of these:

1. The descriptor pointer returned by
   `instrumentManager_voicePageDescriptorIndex()` has `long_name` equal to the
   file key at runtime.
2. The firmware image being tested does not contain the current descriptor
   layout/macros.
3. Memory is corrupting or misaligning the `ParamDescriptor` string fields
   before render.
4. The observed `lfo_*` text is on row 2 target display, not the row 1 edit
   header; that path still also reads target `descriptor->long_name`, but from
   the selected target descriptor rather than the currently edited LFO target
   selector descriptor.

The direct next proof point should be a runtime diagnostic at the point of
render, logging or temporarily displaying the four descriptor pointers/strings
for the selected LFO cell:

- `descriptor_index`
- `descriptor->file_key`
- `descriptor->category`
- `descriptor->long_name`
- `descriptor->short_name`

That diagnostic should be added only after deciding where debug output belongs
for this firmware build; this audit does not add another display workaround.

## Root Cause Found

Follow-up hardware observation: the display showed `LFO lfo_Frequncy`, not
`LFO     lfo_rate` or `LFO     lfo_Frequncy`.

That means the right half of the header was already using
`descriptor->long_name` correctly. The extra text was in the left eight-column
category field.

Root cause: the earlier fixed-width descriptor helper padded each character
position by checking `src[i]` independently:

```c
dst[i] = (src && src[i]) ? src[i] : ' ';
```

For a short C string such as `"LFO"`, index 3 is the NUL terminator, but index
4 reads past the terminator into adjacent string-literal storage. The linker had
adjacent bytes beginning with `lfo_`, so the eight-column category copy became:

```text
LFO lfo_
```

Fix: the display path now uses `menu_copyPaddedField()`, which copies until the
first NUL or the field width, then fills the remaining width with spaces without
reading past the string terminator.

## Generalized Fix

Follow-up hardware observation: another short category showed the same failure:
`FM  osc2Amount  `. This confirms the bug was not LFO-specific. Any descriptor
display field shorter than its fixed LCD width could leak adjacent string data.

The display helper is now `menu_copyPaddedField()`:

- reads no more than the destination field width
- stops at the first NUL terminator
- pads the remainder with spaces
- accepts `NULL` by rendering an all-space field

This handles future descriptor strings consistently:

- short strings render as the string plus spaces
- exact-width strings render as-is
- longer strings truncate to the field width
- strings without a NUL inside the field width still cannot leak beyond the
  field
