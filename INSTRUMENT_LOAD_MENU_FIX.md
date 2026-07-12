# Instrument Load Menu Fix Plan

## Issue Analysis
The Instrument Load menu suffers from a disconnect between the internal state machine (`menu_saveOptions.state`, `editModeActive`) and the visual rendering (`menu_repaintLoadSavePage()`).

### 1. Hardcoded UI Rendering
In `Core/Menu/menu.c:menu_repaintLoadSavePage()`, the `LOAD_PAGE` rendering for `menu_instrumentLoadActive` ignores `menu_saveOptions.state` and `editModeActive`. 
Currently, it unconditionally renders:
```text
Load: Drum    
[  0]Empty
```
It hardcodes `[` and `]` on the bottom row, and never displays the selection cursor `>`. This visually tricks the user into thinking they are always editing the bottom row, while the internal state machine defaults to editing the top row (`SAVE_STATE_EDIT_TYPE`).

### 2. Broken State Navigation
When the user clicks the encoder, `buttonHandler` (via `menu_encoderMoved`) toggles `editModeActive` from `1` to `0`. If the user then turns the encoder, `menu_handleLoadSaveMenu()` transitions `menu_saveOptions.state` to `SAVE_STATE_EDIT_PRESET_NR`. However, because the rendering never changes to show `> Drum` or `> 0`, the user is blind to this state change. Furthermore, if they click again to re-enter `editModeActive`, they might be editing the bottom row, but because the display never changes, they don't understand the interaction model.

### 3. "Empty" File List
The user reported that the names of the instrument files are never displayed (they see `[  0]Empty`). This happens for two reasons:
1. Since they are visually trapped on the top row, turning the encoder changes the Instrument Type (Drum -> Snare), not the file slot. 
2. The `[  0]Empty` display only occurs if `filesystem_instrumentCount(menu_instrumentLoadType)` returns `0`. This suggests that the boot-time scan (`filesystem_requestScanInstruments`) either failed to find files on the SD card, or the cache was never populated for that type. However, even if `count > 0` and it displayed a name, the user wouldn't be able to scroll through the files because of the broken navigation UX.

## Code Change Plan

### 1. Update `menu_repaintLoadSavePage()`
This is the only code change required. We replaced the hardcoded brackets in the `menu_instrumentLoadActive` block with state-aware bracket/arrow rendering, matching the logic from the standard Load menu.

**Files:**
- `Core/Menu/menu.c` (Modified successfully)

**Code changes:**
The `if (menu_activePage == LOAD_PAGE && menu_instrumentLoadActive)` block in `menu_repaintLoadSavePage()` has been replaced with the following code. Inline comments describe the formatting math and rendering logic:

```c
    if (menu_activePage == LOAD_PAGE && menu_instrumentLoadActive) {
        uint8_t count;
        uint8_t index;
        uint16_t display_index;

        /* Clamp the current type's index so it doesn't exceed the number of files available */
        menu_instrumentLoadClampIndex();
        count = filesystem_instrumentCount(menu_instrumentLoadType);
        index = menu_instrumentLoadIndex[menu_instrumentLoadType];
        display_index = filesystem_instrumentDisplayIndex(menu_instrumentLoadType, index);

        /* --- Top Row: Load Type --- */
        memcpy(&editDisplayBuffer[0][0], "Load:", 5);
        
        /* Render cursor/brackets for the top row if the state is editing the type */
        if (menu_saveOptions.state == SAVE_STATE_EDIT_TYPE) {
            if (editModeActive) {
                /* In edit mode, show brackets around the type name */
                editDisplayBuffer[0][5] = '[';
                editDisplayBuffer[0][14] = ']';
            } else {
                /* In selection mode, show a pointer arrow */
                editDisplayBuffer[0][5] = ARROW_SIGN;
            }
        }
        
        /* Copy the display label from the instrument registry (e.g., "Drum", "Snare") */
        menu_copyPaddedField(&editDisplayBuffer[0][6],
                             instrumentManager_typeDisplayLabel(menu_instrumentLoadType),
                             8u);

        /* --- Bottom Row: File Slot --- */
        /* Render cursor/brackets for the bottom row if the state is editing the preset/file number */
        if (menu_saveOptions.state == SAVE_STATE_EDIT_PRESET_NR) {
            if (editModeActive) {
                /* In edit mode, show brackets around the file index */
                editDisplayBuffer[1][0] = '[';
                editDisplayBuffer[1][4] = ']';
            } else {
                /* In selection mode, show a pointer arrow */
                editDisplayBuffer[1][0] = ARROW_SIGN;
            }
        }
        
        /* Format the display index as a 3-digit padded number.
           Max display index is visually capped at 999. 
           Math: integer division and modulo are used to extract hundreds, tens, and units digits.
           Padding with ' ' is used for leading zeros on hundreds and tens. */
        if (display_index > 999u)
            display_index = 999u;
        editDisplayBuffer[1][1] = (display_index >= 100u) ? (char)('0' + (display_index / 100u)) : ' ';
        editDisplayBuffer[1][2] = (display_index >= 10u) ? (char)('0' + ((display_index / 10u) % 10u)) : ' ';
        editDisplayBuffer[1][3] = (char)('0' + (display_index % 10u));

        /* Copy the file name to the display buffer. 
           If the count is > 0, we query the filesystem for the file name. 
           Otherwise, we display "Empty   " padded to 8 chars. */
        memcpy(&editDisplayBuffer[1][5],
               count ? filesystem_instrumentName(menu_instrumentLoadType, index)
                     : "Empty   ", 8);
        return;
    }
```

**Description block:**

- **What:** This change re-writes the visual representation of the Instrument Load menu within the LCD `editDisplayBuffer`. It respects `menu_saveOptions.state` and `editModeActive` to conditionally paint `[` `]` or `>` (`ARROW_SIGN`) on either row 1 (Type) or row 2 (File), mirroring the original Load menu design.
- **Why it must exist:** Without conditional cursor painting, the user is blind to the active navigation state (Type vs File slot) and edit state (navigating rows vs changing values). The current UX hardcodes edit brackets `[]` on the bottom row while defaulting interaction to the top row, which creates a fundamentally confusing and unusable menu.
- **Inputs:** `menu_instrumentLoadType`, `menu_instrumentLoadIndex`, `menu_saveOptions.state`, `editModeActive`, output from `filesystem_instrumentCount` and `filesystem_instrumentDisplayIndex`.
- **Outputs:** Modifies `editDisplayBuffer[0]` and `editDisplayBuffer[1]` directly, which the LCD async DMA loop flushes to hardware.
- **Clients:** Called by `menu_repaint()` and `menu_repaintAll()` whenever `menu_activePage == LOAD_PAGE` and a redraw is requested (e.g. after an encoder click or turn).
- **Accessors:** Uses `filesystem_instrumentCount`, `filesystem_instrumentDisplayIndex`, `filesystem_instrumentName`, `instrumentManager_typeDisplayLabel`, and `menu_instrumentLoadClampIndex`.
- **Affiliates:** `menu_handleLoadSaveMenu` is intrinsically linked as it drives the actual `menu_saveOptions.state` changes that this rendering change visually represents. `menu_instrumentLoadRequestSelection` is affiliated because it fires the immediate load upon the user editing the bottom row.

### 2. Immediate Load Semantics (No changes needed)
The standard Load menu requires the user to press OK to load (`SAVE_STATE_OK`). The Instrument Load menu is correctly designed to load immediately upon scrolling the file index via `menu_instrumentLoadRequestSelection()` inside `menu_handleLoadSaveMenu()`. By displaying `[]` around the file index during `editModeActive` in our display fix above, the UX clearly signals to the user that rotating the encoder directly changes the active value, naturally fulfilling the "immediate load" requirement without needing a distinct `SAVE_STATE_OK` state. No code changes are required here.

## Execution Notes
- Code changes applied to `Core/Menu/menu.c`.
- Verified compilation via `make`.
