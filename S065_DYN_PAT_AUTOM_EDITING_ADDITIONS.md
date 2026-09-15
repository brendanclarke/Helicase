# S065 — Automation Editing Additions

Post-testing amendments to `S065_DYN_PAT_STEP_AUTOM_EDITING.md`. Two
categories: (A) automation parameter reset on voice retrigger, (B) step-edit
menu UX fixes.

---

## A. Automation Parameter Reset on Voice Retrigger

### A1. Problem

When the sequencer applies an automation entry via `seq_drainPendingAutomation`
→ `instrumentManager_writeRuntime`, the DSP runtime value persists indefinitely.
No existing trigger path restores the Scene image value — the voice retrigger
calls `voiceControl_noteOn` → `voiceControl_enqueueTriggerLocked` →
`voiceControl_triggerNow` → `instrumentManager_triggerTrack`, which resets
oscillator phase, envelopes, and LFOs but never touches parameter values.

`preset_resetAndApplyKitVoiceImage` does reload the full Scene image, but it
is designed for Scene slot changes — it calls `instrumentManager_resetRuntimeSlot`
(memset + full voice init) and `presetMorph_applyVoiceNow` (iterate all
descriptors). This is far too expensive for a per-step trigger event.

### A2. Design: per-slot dirty bitmap + selective image restore

Track which descriptor indices have been overridden by automation using a
64-bit bitmap per slot (one bit per `INSTRUMENT_PARAM_COUNT` descriptor index).
On voice retrigger, restore only the dirty descriptors from the Scene image
before the trigger fires.

#### Data structure

```c
static uint64_t seq_automation_dirty[INSTRUMENT_SLOT_COUNT]; /* 6 × 8 = 48 B */
```

One bit per descriptor index (0..63). Bit N is set when automation writes to
descriptor index N on slot S. On retrigger of slot S, iterate set bits, write
the Scene image value to the runtime, and clear the bitmap.

#### Why 48 bytes

6 slots × 8 bytes = 48 bytes of static SRAM. Covers all 64 descriptor indices
per slot. The bitmap is never persisted — it is pure transient runtime state
that exists only while the sequencer is playing.

### A3. Implementation: dirty-mark on automation application

**File: `sequencer.c`, function `seq_drainPendingAutomation`**

After the existing `instrumentManager_writeRuntime(slot, descriptor, value8)`
call at line 568, set the dirty bit for this descriptor:

```c
/* current code at line 568: */
(void)instrumentManager_writeRuntime(slot, descriptor, value8);

/* add after: */
{
    uint8_t local = instrumentParam_local(target);
    seq_automation_dirty[slot] |= (1ULL << local);
}
```

The `local` variable is already derivable from `target` via
`instrumentParam_local()`. `slot` is already computed at line 559.

### A4. Implementation: restore on retrigger

The reset must happen BEFORE the voice trigger, not after. The trigger
reinitializes oscillator phase and envelopes from the current runtime values —
if the reset happened after, the trigger would start with the stale automated
value.

The correct insertion point is `voiceControl_triggerNow` at
`MidiVoiceControl.c:149`, which is the foreground consumer of the trigger ring.
This function already calls `preset_applyDeferredSceneSlotForTrigger(voice)`
and `instrumentManager_triggerTrack(voice, note, vel)` in sequence. The
automation reset goes between the deferred Scene apply and the trigger:

**File: `MidiVoiceControl.c`, function `voiceControl_triggerNow`, lines 149-165**

Current code:
```c
static void voiceControl_triggerNow(uint8_t voice, uint8_t note, uint8_t vel)
{
    preset_applyDeferredSceneSlotForTrigger(voice);
    instrumentManager_triggerTrack(voice, note, vel);
    led_pulseLed((uint8_t)(LED_VOICE1 + voice));
}
```

Add call after the deferred Scene apply, before the trigger:
```c
static void voiceControl_triggerNow(uint8_t voice, uint8_t note, uint8_t vel)
{
    preset_applyDeferredSceneSlotForTrigger(voice);
    seq_restoreAutomatedParameters(voice);
    instrumentManager_triggerTrack(voice, note, vel);
    led_pulseLed((uint8_t)(LED_VOICE1 + voice));
}
```

### A5. Implementation: `seq_restoreAutomatedParameters`

**File: `sequencer.c`** (new function, declared in `sequencer.h`)

```c
void seq_restoreAutomatedParameters(uint8_t trigger_track)
```

Logic:

1. Map `trigger_track` (0..6) to slot (0..5, track 6 maps to slot 5).
2. Read `seq_automation_dirty[slot]`. If zero, return immediately (fast path —
   no automation was applied since last trigger).
3. Get the Scene instrument slot:
   `scene_instrumentSlotConst(seq_activePattern, slot)`.
4. For each set bit `i` in the dirty bitmap:
   a. Get the descriptor: `instrumentManager_descriptor(slot_type, i)`.
   b. Read the Scene image value:
      `instrument->parameter_images.instrument_parameters[i]`.
   c. Call `instrumentManager_writeRuntime(slot, descriptor, image_value)`.
5. Clear: `seq_automation_dirty[slot] = 0`.

The worst case is 64 iterations × `instrumentManager_writeRuntime` each.
In practice, automation touches a few parameters per voice — the popcount
loop keeps this tight. For voices with zero automation, the function is
a single comparison and return.

#### Popcount iteration

Use a standard bit-scan loop:

```c
uint64_t mask = seq_automation_dirty[slot];
while (mask) {
    uint8_t i = (uint8_t)__builtin_ctzll(mask);
    /* restore descriptor i */
    mask &= mask - 1ULL;
}
```

`__builtin_ctzll` is a single CLZ instruction on Cortex-M7.

### A6. Implementation: initialization

**File: `sequencer.c`, function `seq_init` (around line 164)**

Add after the existing `seq_pending_automation_count = 0u`:

```c
{
    uint8_t s;
    for (s = 0u; s < INSTRUMENT_SLOT_COUNT; s++)
        seq_automation_dirty[s] = 0ULL;
}
```

Also clear in `seq_setStepIndexToStart` / transport stop, so stale automation
doesn't persist across start/stop cycles.

### A7. Coverage: all trigger sources

The user says "prior to the voice playing when it is triggered by the
sequencer (or any other extant or future method, MIDI, interface, rolls,
etc)." All trigger sources funnel through `voiceControl_noteOn` →
`voiceControl_enqueueTriggerLocked` → `voiceControl_triggerNow`:

| Trigger source | Call path | Reaches `triggerNow`? |
|----------------|-----------|----------------------|
| Sequencer step | `seq_triggerVoice` → `voiceControl_noteOn` | Yes |
| Sequencer roll | `seq_triggerVoice` (roll path) → `voiceControl_noteOn` | Yes |
| MIDI note-on | `MidiParser.c:1461` → `voiceControl_noteOn` | Yes |
| Button preview | `seq_previewVoice` → `voiceControl_noteOn` | Yes |

Placing the reset in `voiceControl_triggerNow` covers all current and future
trigger sources automatically.

### A8. Edge case: automation on the triggering step itself

The sequencer calls `seq_triggerVoice` and then `seq_queueStepAutomations` in
`seq_advanceTrackStep` (lines 507, 511). The trigger happens first (ISR
context via `voiceControl_noteOn`), but `voiceControl_triggerNow` runs from
the foreground ring. The sequence is:

1. ISR: `seq_triggerVoice` enqueues a trigger event in the voice ring
2. ISR: `seq_queueStepAutomations` enqueues automation entries in the pending
   buffer
3. Foreground: `voiceControl_triggerNow` pops the trigger → calls
   `seq_restoreAutomatedParameters` → clears dirty bitmap → fires trigger
4. Foreground: `seq_drainPendingAutomation` applies this step's automation →
   sets dirty bits again

This ordering is correct: the trigger's reset clears the previous step's
automation, and the current step's automation is then applied fresh on top.

### A9. Scene target reset

The drain path at `sequencer.c:556` currently skips Scene targets
(`instrumentParam_isVoiceParameter` check). Scene target automation is not
yet applied during playback, so there are no Scene targets to reset. When
Scene target application is added, the dirty bitmap approach does not cover
Scene targets (they are not descriptor-indexed). A separate mechanism will be
needed at that time. This is explicitly out of scope for this change.

### A10. Files changed

| File | Change |
|------|--------|
| `sequencer.c` | Add `seq_automation_dirty[6]` static array. Add dirty-bit set in `seq_drainPendingAutomation`. Add `seq_restoreAutomatedParameters()`. Clear in `seq_init` and transport stop. |
| `sequencer.h` | Declare `void seq_restoreAutomatedParameters(uint8_t trigger_track)`. |
| `MidiVoiceControl.c` | Add `#include "sequencer.h"`. Call `seq_restoreAutomatedParameters(voice)` in `voiceControl_triggerNow` before `instrumentManager_triggerTrack`. |

### A11. RAM cost

48 bytes static SRAM (`uint64_t × 6`). No stack impact beyond the existing
trigger path.

---

## B. Step-Edit Menu UX Fixes

### B1. Display buffer not cleared when scrolling back to specials

**Symptom:** Scrolling from the automation editor back to the specials page
leaves stale characters from the automation renderer in `editDisplayBuffer`.

**Root cause:** `menu_moveToMenuItem` at line 7648-7651 sets
`menu_stepAutoActive = 0` and `menuIndex = (1 << PAGE_SHIFT) | 2` (subpage 1,
parameter 2 = probability). The next repaint calls `menu_repaint` (line 8457),
which calls `menu_repaintGeneric` (line 6394), which enters the overview path
at line 7494. The overview path writes 3 characters per column (positions 0-2)
and the selected parameter's uppercase (positions 0-2 of that column), but
never clears positions 3, 7, 11, 15 (the gap columns), nor does it clear the
full rows first. The stale automation characters persist in these gaps.

**Fix location:** `menu_repaintGeneric`, at the entry to the overview path
(line 7494, the `else` branch of `if (editModeActive)`).

**Change:** Add a full buffer clear before the overview rendering loop:

```c
} else {
    /* clear stale characters from custom renderers (automation editor) */
    memset(editDisplayBuffer[0], ' ', 16u);
    memset(editDisplayBuffer[1], ' ', 16u);
```

This matches what the edit-mode path already does at line 7370-7371. The cost
is negligible — two 16-byte memsets on a path that already does two loops of
4 iterations each with memcpy/numtostr calls.

**File:** `menu.c`, line 7494 (the `} else {` in `menu_repaintGeneric`).

### B2. Specials page missing ">" scroll indicator

**Requirement:** The specials page (vel/nte/prb on SEQ_PAGE subpage 1) should
always show `>` as the last character on the top row to indicate the user can
scroll right to the automation pages.

**Current behavior:** `checkScrollSign` at line 6211 returns the scroll
indicator based on the static page table. For SEQ_PAGE subpage 1, positions
3-7 are `TEXT_EMPTY`/`PAR_NONE`, so `checkScrollSign` returns `' '` (no scroll
indicator) when the cursor is on positions 0-2 (the specials).

**Fix location:** `checkScrollSign`, add a special case for SEQ_PAGE subpage 1.

**Change:** After the voice-page handling and before the static-page fallthrough,
add:

```c
if (menu_activePage == SEQ_PAGE && activePage == 1u && activeParameter <= 2u)
    return '>';
```

This puts `>` at position 15 of row 0 whenever the user is on the specials
page (subpage 1, parameters 0-2) of the SEQ_PAGE. The `>` correctly disappears
when the automation editor takes over rendering (the automation renderer owns
its own `>` logic).

**File:** `menu.c`, function `checkScrollSign` (around line 6270, before the
final return).

### B3. Automation page cursor navigation redesign

The current automation page has no cursor — the encoder scrolls between
automation pages and pots edit fields directly. The user wants a scrollable
cursor like other menu pages.

#### B3.1 New state variable

Replace `menu_stepAutoDeleteMode` (currently a 0/1 toggle) with a more
general cursor state:

```c
static uint8_t menu_stepAutoCursor = 0u;
```

Values:
- 0 = automation number (top-left)
- 1 = del/clr action
- 2 = voi (voice)
- 3 = par (parameter)
- 4 = amt (value)

Add a number-locked flag:

```c
static uint8_t menu_stepAutoNumberLocked = 0u;
```

When set, the encoder changes the page index directly instead of moving the
cursor.

Add the del/clr mode tracker (was previously `menu_stepAutoDeleteMode`, keep
the same variable name but it only affects the action label, not cursor):

```c
static uint8_t menu_stepAutoDeleteMode = 0u; /* 0=del, 1=clr — unchanged */
```

**File:** `menu.c`, near line 1158.

#### B3.2 New display layout

##### Assigned automation page (page < count)

```
Row 0: ">00 voi par amt>"
Row 1: " del  1  frq 064"
```

| Position | Col 0-2 | Col 3 | Col 4-6 | Col 7 | Col 8-10 | Col 11 | Col 12-14 | Col 15 |
|----------|---------|-------|---------|-------|----------|--------|-----------|--------|
| Row 0 | cursor + 2-digit number | space | `voi` | space | `par` | space | `amt` | `>` or space |
| Row 1 | space + `del`/`clr` | space | voice# | space | param 3ch | space | value 3ch | space |

The selected item is UPPERCASED on row 0 (except item 0 which shows cursor).
Cursor character for item 0: `>` when selected and not locked, `*` when
number-locked. Items 1-4 use uppercase on the three-letter label when
selected (no cursor character — same as every other menu page).

Two-digit number: `00`..`62` for assigned entries, right-justified with
zero-pad. Three digits is excessive for 0..63 range.

Row 0 position 15 shows `>` when there are more pages or the add page is
available (i.e., always except when on the add page itself).

##### Add page (page == count, count < 63)

```
Row 0: " 03 add            "
Row 1: "     off off off    "
```

| Position | Behavior |
|----------|----------|
| Col 0-2 | cursor + 2-digit number (the next available index) |
| Col 4-6 | `add` (or `ADD` when selected) |
| Col 8-10, 12-14 | not selectable until add executed |
| Row 0[15] | space (no `>` — this is the last page) |

When `add` is selected (cursor == 1) and encoder clicked: create the
automation, convert the page to an assigned page, reset cursor to 0.

The number on the add page is clickable and lockable like assigned pages.

When count == 63, the add page is not available — scrolling stops at entry 62.

#### B3.3 Cursor navigation in `menu_moveToMenuItem`

**File:** `menu.c`, the `menu_stepAutomationPageActive()` branch in
`menu_moveToMenuItem` (currently lines 7634-7653).

Replace the current logic that only changes `menu_stepAutoPageIndex` with:

```
if number-locked:
    change menu_stepAutoPageIndex by inc
    clamp to [0, count] (count = add page if count < 63)
    return

if on add page (page >= count):
    cursor items are: 0 (number), 1 (add). Items 2-4 not selectable.
    if inc > 0 and cursor == 1: do nothing (last item on last page)
    if inc < 0 and cursor == 0: exit automation, go back to prb
    else: move cursor

if on assigned page:
    cursor items are: 0 (number), 1 (del/clr), 2 (voi), 3 (par), 4 (amt)
    if inc > 0 and cursor == 4:
        advance to next page (menu_stepAutoPageIndex++), cursor = 0
    if inc < 0 and cursor == 0:
        if menu_stepAutoPageIndex > 0: go to previous page, cursor = 4
        else: exit automation, go back to prb (menu_stepAutoActive = 0)
    else: move cursor
```

#### B3.4 Encoder click behavior

**File:** `menu.c`, the encoder click handling in `menu_parseEncoder`
(currently lines 8421-8427).

Replace the current `(menuIndex & MASK_PARAMETER) == 0` check with
cursor-aware dispatch:

```
if cursor == 0 (number):
    toggle menu_stepAutoNumberLocked
    (no edit mode toggle — number lock IS the mode)

if cursor == 1 (del/clr or add):
    if on add page: execute add, convert to assigned page
    if on assigned page: execute del or clr action
    (same as current menu_stepAutomationExecuteItem0)

if cursor == 2 (voi):
    toggle editModeActive (shows single-parameter detail view)

if cursor == 3 (par):
    toggle editModeActive (shows single-parameter detail view)

if cursor == 4 (amt):
    toggle editModeActive (shows single-parameter detail view)
```

#### B3.5 Encoder turn in edit mode (cursor 2, 3, or 4)

When `editModeActive` is true and cursor is 2, 3, or 4, encoder turn changes
the selected field (same as current `menu_stepAutomationEdit` with field
mapping: cursor 2 → field 1 (voice), cursor 3 → field 2 (param), cursor 4 →
field 3 (amt)).

This is the existing behavior — `menu_encoderChangeParameter` at line 7547
already delegates to `menu_stepAutomationEdit(activeParameter, inc)`. The
change is that `activeParameter` is now derived from `menu_stepAutoCursor`
instead of `menuIndex & MASK_PARAMETER`.

#### B3.6 Encoder turn in number-locked mode

When `menu_stepAutoNumberLocked` is true, encoder turn directly changes
`menu_stepAutoPageIndex`:

- Increment: if `page < count` (where count includes the add page when
  `count < 63`), advance.
- Decrement: if `page > 0`, go back.
- Clamp at both ends — cannot go below 0 or above the add page.

The user can still adjust voi/par/amt with the knobs while number is locked.

#### B3.7 Knob behavior with cursor

Pot 0 still toggles del/clr mode (unchanged).
Pots 1-3 still edit voice/parameter/value (unchanged).
The cursor position does not affect pot behavior — pots always map to their
column regardless of cursor.

#### B3.8 Single-parameter detail view

When the user clicks into voi (cursor 2), par (cursor 3), or amt (cursor 4),
`editModeActive` toggles on and the display shows the full detail view:

##### Voice detail (cursor 2, editModeActive)

```
Row 0: "Voice           "
Row 1: "  <voice_number> "
```

Uses the existing edit-mode renderer path. The voice number is shown as 1-6.
Encoder turn cycles voices.

##### Parameter detail (cursor 3, editModeActive)

```
Row 0: "Par Target      "
Row 1: "  <long_name>    "
```

Row 1 shows `descriptor->long_name` (from `ParamDescriptor.long_name`), left-
aligned, up to 16 characters. For Scene targets, show the
`scene_mod_target_descriptor_t.prefix` + `scene_mod_target_descriptor_t.label`
(e.g., "Voice 1 Morph"). Encoder turn cycles through automatable parameters.

For invalid targets, show `"Invalid"`.

##### Amount detail (cursor 4, editModeActive)

```
Row 0: "Amt             "
Row 1: "  <3-digit value>"
```

Shows the 7-bit value (0..127). Encoder turn adjusts the value.

**Implementation:** The edit-mode rendering for automation detail views goes in
`menu_repaintStepAutomation`. When `editModeActive` is true, check
`menu_stepAutoCursor`:

- If 2, 3, or 4: render the corresponding detail view.
- Otherwise: render the normal compact view (number lock doesn't use editMode).

**File:** `menu.c`, in `menu_repaintStepAutomation` (around line 7267).

### B4. Implementation: renderer changes

**File:** `menu.c`, function `menu_repaintStepAutomation` (line 7267-7325).

The current renderer is a single flat function. It needs to be restructured:

1. **Entry guard for edit-mode detail:** if `editModeActive` and cursor is
   2/3/4, render the detail view and return.

2. **Compact view for assigned pages:** change the top-left from 3-digit
   `numtostrpu` to:
   - Position 0: cursor character (`>` if cursor==0 and not locked, `*` if
     locked, space otherwise)
   - Positions 1-2: 2-digit zero-padded page number

3. **Cursor uppercasing:** for cursor positions 1-4, uppercase the
   corresponding 3-character label on row 0. For cursor position 0, the
   cursor character serves as the indicator.
   - cursor 1: uppercase "del"→"DEL" or "clr"→"CLR" at row 1 positions 1-3
     (the action IS on row 1 in the current layout, so uppercase it there;
     but user spec says ">CLR" — add cursor character at row 1 position 0
     when selected)
   - cursor 2: uppercase "voi"→"VOI" at row 0 positions 4-6
   - cursor 3: uppercase "par"→"PAR" at row 0 positions 8-10
   - cursor 4: uppercase "amt"→"AMT" at row 0 positions 12-14

   Wait — the user says: "the del/clr item (capitalize and show cursor when
   selected like '>CLR')". So cursor 1 shows `>CLR` or `>DEL` at row 1
   positions 0-3 (cursor char + 3 uppercase letters). When not selected, it
   shows ` del` or ` clr` (space + 3 lowercase letters). This matches the
   number format: `>00` selected vs ` 00` not selected.

   Revised cursor display rules:
   - Cursor 0 (number): `>00` or `*00` (locked)
   - Cursor 1 (action): `>DEL` or `>CLR` on row 1
   - Cursor 2 (voi): `VOI` uppercase on row 0
   - Cursor 3 (par): `PAR` uppercase on row 0
   - Cursor 4 (amt): `AMT` uppercase on row 0

4. **Row 0 position 15 `>` indicator:** show `>` when `page + 1 <= count`
   (there is a next automation page or the add page). Show space when on the
   add page. The current code at line 7296 already does
   `if (page + 1u < count) editDisplayBuffer[0][15] = '>'` — extend this to
   also show `>` when `page + 1 == count` (add page exists) and
   `count < PAT_BLOCK_AUTO_COUNT_MASK`.

5. **Add page:** same 2-digit number + cursor. Items 2-4 (voi/par/amt) not
   selectable and show lowercase labels + "off" values as before. When add is
   selected, show `>ADD`. Number is selectable and shows `>NN` or `*NN`.

### B5. Implementation: `menu_stepAutomationEdit` changes

**File:** `menu.c`, function `menu_stepAutomationEdit` (line 7121).

The `field` parameter currently maps 0=action, 1=voice, 2=param, 3=value.
It is called from `menu_encoderChangeParameter` with `activeParameter` as the
field.

Change: when `menu_stepAutomationPageActive()`, use `menu_stepAutoCursor`
instead of `menuIndex & MASK_PARAMETER` to determine the field:

```c
if (menu_stepAutomationPageActive()) {
    uint8_t field;
    if (menu_stepAutoNumberLocked) {
        /* encoder changes page number directly */
        ...
        return;
    }
    field = menu_stepAutoCursor;
    if (field >= 2u)
        field = (uint8_t)(field - 1u); /* cursor 2→field 1, 3→2, 4→3 */
    (void)menu_stepAutomationEdit(field, inc);
    return;
}
```

### B6. Implementation: knob handler changes

**File:** `menu.c`, function `menu_stepAutomationHandleKnob` (line 7212).

The knob handler currently maps pot 0 to del/clr toggle and pots 1-3 to
field edits. This behavior is unchanged — pots always map to their physical
column regardless of cursor position. No changes needed here.

### B7. Files changed

| File | Change |
|------|--------|
| `menu.c` | B1: add `memset` in overview path of `menu_repaintGeneric`. B2: add SEQ_PAGE subpage 1 case to `checkScrollSign`. B3-B6: restructure automation cursor, navigation, rendering, and click handling. |

### B8. Detailed change list

#### Change 1: `menu_repaintGeneric` overview buffer clear

**Location:** `menu.c:7494`, the `} else {` branch.

**Current:**
```c
    } else {
        const uint8_t is2ndPage = menu_isVoicePage(menu_activePage)
```

**New:**
```c
    } else {
        memset(editDisplayBuffer[0], ' ', 16u);
        memset(editDisplayBuffer[1], ' ', 16u);
        const uint8_t is2ndPage = menu_isVoicePage(menu_activePage)
```

Note: C89 compatibility — the `memset` calls must go before the local
declaration, or the declaration must be moved above the `if`. Since this is
an existing `const uint8_t` in the middle of a block, and the project uses
`-std=gnu11` (check Makefile), interleaving statements and declarations is
fine. If not, hoist the declaration to the top of the function.

#### Change 2: `checkScrollSign` specials `>` indicator

**Location:** `menu.c`, function `checkScrollSign`, before the final static
page logic (around line 6270).

**Add:**
```c
if (menu_activePage == SEQ_PAGE && activePage == 1u &&
    !menu_stepAutoActive && activeParameter <= 2u)
    return '>';
```

This returns `>` for the three specials fields on SEQ_PAGE subpage 1 when the
automation editor is not active. When the user scrolls right past probability,
`menu_stepAutoActive` becomes 1 and this path is no longer taken — the
automation renderer owns its own indicators.

#### Change 3: state variables

**Location:** `menu.c`, near line 1158.

**Current:**
```c
static uint8_t menu_stepAutoPageIndex = 0u;
static uint8_t menu_stepAutoDeleteMode = 0u;
static uint8_t menu_stepAutoActive = 0u;
```

**New:**
```c
static uint8_t menu_stepAutoPageIndex = 0u;
static uint8_t menu_stepAutoDeleteMode = 0u;
static uint8_t menu_stepAutoActive = 0u;
static uint8_t menu_stepAutoCursor = 0u;
static uint8_t menu_stepAutoNumberLocked = 0u;
```

#### Change 4: `menu_stepAutomationReset`

**Location:** `menu.c`, function `menu_stepAutomationReset` (line 6911).

**Current:**
```c
static void menu_stepAutomationReset(void)
{
    menu_stepAutoPageIndex = 0u;
    menu_stepAutoDeleteMode = 0u;
    menu_stepAutoActive = 0u;
}
```

**New:**
```c
static void menu_stepAutomationReset(void)
{
    menu_stepAutoPageIndex = 0u;
    menu_stepAutoDeleteMode = 0u;
    menu_stepAutoActive = 0u;
    menu_stepAutoCursor = 0u;
    menu_stepAutoNumberLocked = 0u;
}
```

#### Change 5: `menu_moveToMenuItem` automation branch

**Location:** `menu.c`, the `menu_stepAutomationPageActive()` branch in
`menu_moveToMenuItem` (lines 7634-7653).

**Current:**
```c
    if (menu_stepAutomationPageActive()) {
        uint8_t count = pat_stepAutomationCount(
            menu_getViewedPattern(), menu_getActiveVoice(),
            parameter_values[PAR_ACTIVE_STEP]);
        if (inc > 0) {
            if (menu_stepAutoPageIndex < count)
                menu_stepAutoPageIndex++;
        } else if (menu_stepAutoPageIndex > 0u) {
            menu_stepAutoPageIndex--;
        } else {
            menu_stepAutoActive = 0u;
            menuIndex = (uint8_t)((1u << PAGE_SHIFT) | 2u);
        }
        return;
    }
```

**New:**
```c
    if (menu_stepAutomationPageActive()) {
        uint8_t count = pat_stepAutomationCount(
            menu_getViewedPattern(), menu_getActiveVoice(),
            parameter_values[PAR_ACTIVE_STEP]);
        uint8_t page = menu_stepAutoPageIndex;
        uint8_t on_add = (uint8_t)(page >= count);
        uint8_t max_cursor = on_add ? 1u : 4u;

        if (menu_stepAutoNumberLocked) {
            if (inc > 0 && page < count &&
                count < PAT_BLOCK_AUTO_COUNT_MASK)
                menu_stepAutoPageIndex++;
            else if (inc > 0 && page < count)
                menu_stepAutoPageIndex = count;  /* no add if 63 */
            else if (inc < 0 && page > 0u)
                menu_stepAutoPageIndex--;
            return;
        }

        if (inc > 0) {
            if (menu_stepAutoCursor < max_cursor) {
                menu_stepAutoCursor++;
            } else if (!on_add) {
                uint8_t has_add = (uint8_t)(count < PAT_BLOCK_AUTO_COUNT_MASK);
                if (page + 1u < count || (page + 1u == count && has_add)) {
                    menu_stepAutoPageIndex++;
                    menu_stepAutoCursor = 0u;
                }
            }
        } else {
            if (menu_stepAutoCursor > 0u) {
                menu_stepAutoCursor--;
            } else if (page > 0u) {
                menu_stepAutoPageIndex--;
                menu_stepAutoCursor = 4u;
            } else {
                menu_stepAutoActive = 0u;
                menu_stepAutoNumberLocked = 0u;
                menuIndex = (uint8_t)((1u << PAGE_SHIFT) | 2u);
            }
        }
        return;
    }
```

#### Change 6: `menu_parseEncoder` click handling

**Location:** `menu.c`, the automation click block (lines 8421-8427).

**Current:**
```c
    if (btnClicked && !editModeActive && menu_stepAutomationPageActive() &&
        (menuIndex & MASK_PARAMETER) == 0u) {
        (void)menu_stepAutomationExecuteItem0();
        menu_repaintAll();
        menu_endlessPotMappingChanged();
        return;
    }
```

**New:**
```c
    if (btnClicked && menu_stepAutomationPageActive()) {
        if (menu_stepAutoCursor == 0u) {
            menu_stepAutoNumberLocked =
                (uint8_t)(!menu_stepAutoNumberLocked);
        } else if (menu_stepAutoCursor == 1u) {
            if (!editModeActive) {
                (void)menu_stepAutomationExecuteItem0();
                menu_endlessPotMappingChanged();
            }
        } else {
            editModeActive = (uint8_t)(1 - editModeActive);
        }
        menu_repaintAll();
        return;
    }
```

Cursor 0 toggles number lock. Cursor 1 executes the action (add/del/clr)
only when not in edit mode. Cursor 2-4 toggle edit mode for detail view.

#### Change 7: `menu_encoderChangeParameter` automation branch

**Location:** `menu.c`, lines 7547-7549.

**Current:**
```c
    if (menu_stepAutomationPageActive()) {
        (void)menu_stepAutomationEdit(activeParameter, inc);
        return;
    }
```

**New:**
```c
    if (menu_stepAutomationPageActive()) {
        if (menu_stepAutoNumberLocked) {
            uint8_t count = pat_stepAutomationCount(
                menu_getViewedPattern(), menu_getActiveVoice(),
                parameter_values[PAR_ACTIVE_STEP]);
            uint8_t has_add = (uint8_t)(count < PAT_BLOCK_AUTO_COUNT_MASK);
            uint8_t max_page = has_add ? count : (count > 0u ? (uint8_t)(count - 1u) : 0u);
            if (inc > 0 && menu_stepAutoPageIndex < max_page)
                menu_stepAutoPageIndex++;
            else if (inc < 0 && menu_stepAutoPageIndex > 0u)
                menu_stepAutoPageIndex--;
        } else if (menu_stepAutoCursor >= 2u) {
            uint8_t field = (uint8_t)(menu_stepAutoCursor - 1u);
            (void)menu_stepAutomationEdit(field, inc);
        }
        return;
    }
```

Cursor 0 and 1 are not editable via encoder turn in non-locked mode (cursor
0 only via click; cursor 1 del/clr toggle stays on pot 0). Cursor 2-4 map to
fields 1-3 (voice/param/amt).

#### Change 8: `menu_repaintStepAutomation` restructure

**Location:** `menu.c`, function `menu_repaintStepAutomation` (line 7267).

The full function is replaced. Key structural changes:

1. If `editModeActive` and cursor 2/3/4: render detail view and return.

2. Compact view for assigned pages:
   - Row 0 col 0: cursor char for number (`>`, `*`, or space)
   - Row 0 col 1-2: 2-digit zero-padded page number
   - Row 0 col 4-6: `voi` (uppercase if cursor==2)
   - Row 0 col 8-10: `par` (uppercase if cursor==3)
   - Row 0 col 12-14: `amt` (uppercase if cursor==4)
   - Row 0 col 15: `>` if not on add page
   - Row 1 col 0: cursor char for action (`>` or space)
   - Row 1 col 1-3: `del`/`clr` (uppercase if cursor==1)
   - Row 1 col 5: voice number digit
   - Row 1 col 9-11: parameter short_name or `inv`
   - Row 1 col 13-15: 3-digit value

3. Compact view for add page:
   - Row 0 col 0: cursor char for number
   - Row 0 col 1-2: 2-digit number (next index)
   - Row 0 col 4-6: `add` (uppercase if cursor==1)
   - Row 0 col 15: space (no `>`)
   - Row 1: ` add  off off off` (cursor on row 1 col 3-5 when selected:
     `>ADD` at col 0-3)

   Wait, re-reading the user spec: "Then 'add' is also a selectable target,
   it appears as '>ADD' when selected." The add text is at the cursor 1
   position. So:
   - Row 0 col 0: cursor char for number
   - Row 0 col 1-2: 2-digit number
   - Row 0 col 4-6: not used (or blank)
   - Row 1 col 0: `>` if cursor==1, else space
   - Row 1 col 1-3: `ADD` if cursor==1, else `add`
   - Row 1 col 5-7, 9-11, 13-15: `off off off`

   Actually the user says the voi/par/amt labels should still appear on row 0
   but are not selectable on the add page until add is executed. So:
   - Row 0: ` NN voi par amt ` (labels present but not selectable)
   - Row 1: `>ADD off off off` when cursor==1

4. Detail view rendering:
   - Cursor 2 (voice): `"Voice          "` row 0, `"  N             "` row 1
     where N is 1-6.
   - Cursor 3 (param): `"Par Target     "` row 0, row 1 shows `long_name`
     left-aligned up to 16 chars. For Scene targets show
     `prefix + " " + label` (e.g., "Voice 1 Morph"). For invalid, show
     `"Invalid"`.
   - Cursor 4 (amt): `"Amount         "` row 0, `"  NNN           "` row 1
     where NNN is 000..127.

**Full replacement function:** approximately 100 lines. The existing function
is ~60 lines. The detail-view branches add ~40 lines.

#### Change 9: `menu_stepAutomationExecuteItem0` add-page conversion

**Location:** `menu.c`, function `menu_stepAutomationExecuteItem0` (line 7233).

After a successful add on the add page, the page becomes an assigned page.
The cursor should move to position 0 (number) on the now-assigned page:

In the `page >= count` branch (line 7243-7245), after the successful add:

```c
    if (page >= count) {
        if (!menu_stepAutomationAddDefault())
            return 0u;
        menu_stepAutoCursor = 0u;
        menu_stepAutoNumberLocked = 0u;
    }
```

This is a minor addition to the existing code.

---

## C. Summary of All Changes

### Files

| File | Section | Changes |
|------|---------|---------|
| `sequencer.c` | A | `seq_automation_dirty[6]`, dirty-bit set in drain, `seq_restoreAutomatedParameters()`, init/clear |
| `sequencer.h` | A | Declare `seq_restoreAutomatedParameters` |
| `MidiVoiceControl.c` | A | Include `sequencer.h`, call restore in `voiceControl_triggerNow` |
| `menu.c` | B1 | `memset` in overview path of `menu_repaintGeneric` |
| `menu.c` | B2 | SEQ_PAGE subpage 1 `>` in `checkScrollSign` |
| `menu.c` | B3-B8 | State vars, reset, navigation, click, encoder, repaint restructure |

### RAM

| Resource | Cost |
|----------|------|
| `seq_automation_dirty[6]` | 48 B static SRAM |
| `menu_stepAutoCursor` | 1 B static |
| `menu_stepAutoNumberLocked` | 1 B static |
| **Total new** | **50 B** |

## D. Implementation Notes

### 2026-09-15 — Additions implementation

- Added `seq_automation_dirty[INSTRUMENT_SLOT_COUNT]` in `sequencer.c`: six
  64-bit slot bitmaps, 48 B of static SRAM. The foreground automation drain
  marks successful voice-descriptor runtime writes by local descriptor index.
- Added `seq_restoreAutomatedParameters()` to `sequencer.c/.h` and invoked it
  from `MidiVoiceControl.c` after deferred Scene-slot application and before
  `instrumentManager_triggerTrack()`. Visible track index 6 (track 7) maps to
  zero-based descriptor slot 5. Scene targets remain outside this
  descriptor restore boundary.
- Dirty bits are cleared during sequencer initialization, fixed-grid restart,
  and transport stop. This prevents runtime overlays from crossing a new
  transport or Scene/Pattern context.
- Added the requested STEP menu cursor state (cursor and number lock),
  cursor-aware encoder navigation/clicks, assigned/add-page rendering,
  single-field detail views, add-page cursor conversion, the specials-page
  scroll marker, and overview-buffer clearing in `menu.c`. The two new Menu
  bytes are included in the 50 B total above. Existing endless-pot mappings
  remain physical-column based.
- All new public `.h` declarations and `.c` implementations have adjacent
  comment blocks describing inputs, outputs, ownership, lifetime, or display
  behavior as applicable.

### Validation

- `make -j2` passes on the Cortex-M7 target. Link summary: text 433,492 B,
  data 412 B, BSS 290,796 B, total 724,700 B.
- `make img` passes and regenerates `build/LXRV2_lxr02.img` with a 433,904 B
  firmware payload (433,920 B including the 16-byte image header).
- `git diff --check` passes.

---

## C. Implementation Audit

Audit of the user's code changes against Sections A and B above, performed
after the implementation was committed. Build baseline: text 433,492 B
(pre-fix), 433,604 B (post-fix).

### C1. Section A — Automation parameter reset

| Spec item | File | Status |
|---|---|---|
| A2 `seq_automation_dirty[6]` (48 B bitmap) | `sequencer.c` | Correct |
| A3 `seq_clearAutomationDirty()` static helper | `sequencer.c` | Correct |
| A4 dirty-bit marking in `seq_drainPendingAutomation` | `sequencer.c` | Correct |
| A5 `seq_restoreAutomatedParameters()` bit-scan restore | `sequencer.c` | Correct |
| A6 declaration in `sequencer.h` | `sequencer.h` | Correct |
| A7 insertion in `voiceControl_triggerNow` | `MidiVoiceControl.c` | Correct — after deferred Scene apply, before trigger dispatch |
| A8 clear in `seq_init()` | `sequencer.c` | Correct |
| A9 clear in `seq_setRunning()` (transport stop) | `sequencer.c` | Correct |
| A10 clear in `seq_setStepIndexToStart()` | `sequencer.c` | Correct |

All Section A items match the spec. Trigger ordering is correct: ISR enqueues
trigger then automation; foreground processes trigger (with reset) then drains
automation — previous step's overlays are cleared before current step's are
applied.

### C2. Section B — Step-edit menu UX

| Spec item | File | Status |
|---|---|---|
| B1 `menu_stepAutoCursor` + `menu_stepAutoNumberLocked` state | `menu.c:1163-1164` | Correct (5 B total) |
| B2 state reset in `menu_stepAutomationReset()` | `menu.c:6929-6930` | Correct |
| B3 `checkScrollSign` '>' on specials subpage | `menu.c:6292-6294` | Correct |
| B4 overview `memset` stale-character fix | `menu.c:7613-7614` | Correct |
| B5 `menu_stepAutomationExecuteItem0` post-add cursor/lock reset | `menu.c:7265-7266` | Correct |
| B6 compact renderer restructure | `menu.c:7290-7439` | Correct — detail views, compact layout, add page, scroll indicator |
| B7 `menu_encoderChangeParameter` number-lock + field edit | `menu.c:7667-7688` | Correct |
| B8 `menu_moveToMenuItem` cursor navigation | `menu.c:7773-7813` | Correct — wrapping, exit to probability |
| B9 `menu_parseEncoder` click dispatch | `menu.c:8582-8596` | Correct |

### C3. Bug found and fixed

**`numtostrpu` 3-character overflow in compact renderer**

`numtostrpu(&editDisplayBuffer[0][1], page, '0')` at line 7381 wrote 3
characters to positions 1–3, but the compact layout needs only 2 digits
(positions 1–2) with position 3 as a space separator before "voi" at
position 4. The third digit bled into the separator gap.

Visible effect: page 0 rendered `>000voi` instead of `>00 voi`.

Fix applied — replaced with manual 2-digit formatting:
```c
editDisplayBuffer[0][1] = (char)('0' + (page / 10u));
editDisplayBuffer[0][2] = (char)('0' + (page % 10u));
```

Post-fix build: text 433,604 B, data 412 B, BSS 290,796 B. Clean, no
warnings.

---

## D. Post-Testing Fixes — Automation Ordering and Restore Source

Two bugs found in testing after the Section A/B implementation landed.

### D1. Bug: automation applied ~50% of the time (ordering race)

**Root cause:** `voiceControl_processPending()` (which calls `voiceControl_triggerNow`
→ `seq_restoreAutomatedParameters`) runs inside `audio_check_and_render()` at
main.c:191, called ~8 times per main-loop iteration. `seq_drainPendingAutomation()`
sat at a single fixed point at main.c:1250, between two `audio_check_and_render()`
calls.

When TIM3 enqueued both a trigger and automation entries, and the ISR landed
between the `audio_check_and_render()` before the drain and the drain itself:

1. Drain ran first → applied automation → set dirty bits
2. Next `audio_check_and_render()` → processed the trigger → restore cleared
   the dirty bits that were **just set** → trigger fired with non-automated values

Whether the ISR landed before or after the drain was timing-dependent, giving
the observed ~50% success rate.

**Fix:** Moved `seq_drainPendingAutomation()` into `audio_check_and_render()`
immediately after `voiceControl_processPending()`, inside the per-chunk render
loop. Removed the standalone call from the main loop. The trigger ring is now
always consumed before the automation buffer within the same render chunk, and
automation values are applied before `mixer_calcNextSampleBlock()` reads them.

Files changed:
- `main.c:192` — added `seq_drainPendingAutomation();` after
  `voiceControl_processPending();`
- `main.c:1245-1251` (old) — removed standalone drain call and its comment
  block

### D2. Bug: restore writes wrong value (raw Scene A instead of morph interpolation)

**Root cause:** `seq_restoreAutomatedParameters()` at sequencer.c:657 read from
`instrument->parameter_images.instrument_parameters[local]` — the raw Scene A
endpoint. When the morph crossfader is not at 0%, the actual current default
value is `morph_interpolation[local]`, the interpolated result of the morph
worker. The restore wrote the wrong value, causing parameters to snap to the
Scene A endpoint rather than returning to the morph-interpolated position.

This also caused the "freeze" symptom when changing automation targets: if
the restored value happened to differ from the morph-interpolated value, the
morph system would later overwrite it on its own schedule, creating a race.
Parameters that lost the race stayed at their last automated value.

**Fix:** Changed the restore source to `morph_interpolation[local]`. This
writes the value the morph crossfader would currently apply, directly to the
runtime, without routing through the morph drain. Updated the function's
doc comment to reflect the new source.

File changed:
- `sequencer.c:657` — `instrument_parameters[local]` →
  `morph_interpolation[local]`
- `sequencer.c:628` — comment updated

### D3. Display: detail view labels and amount formatting

**Row 0 labels** for cursor 2/3/4 detail views changed to show both category
and long name in the same format the user sees on normal VOICE pages:

| Cursor | Old label | New label |
|---|---|---|
| 2 (Voice) | `Voice` | `Target  Voice` |
| 3 (Par Target) | `Par Target` | `Target  Parametr` |
| 4 (Amount) | `Amount` | `Autom.  Amount` |

**Row 1 for voice parameter target (cursor 3):** now concatenates
`descriptor->category` + `descriptor->long_name` (e.g., `OscilltrCoarse`)
instead of showing only `long_name`.

**Amount value (cursor 4 detail and compact row):** changed zero-padding to
space-padding (consistent with all other parameter displays). Detail view
now formats the value by the target descriptor's dtype:

- `DTYPE_MENU` — shows the short name from the menu text table (waveform
  names, filter type names, transient names, LFO wave names, retrigger names,
  sync rate names). Bounds-checked: values exceeding the table count fall
  through to numeric display. Waveform OOB is handled by the existing sample
  name path in `getMenuItemNameForValue`.
- `DTYPE_ON_OFF` — `on`/`off`
- `DTYPE_MIX_FM` — `mix`/`fm`
- `DTYPE_LFO_POLARITY` — `neg`/`pos`/`bi`
- `DTYPE_PM63` — signed ±63
- `DTYPE_NOTE_NAME` — note name
- Default — space-padded numeric

Files changed: `menu.c` (detail view renderer, compact view amount pad char).

### D4. Build

Post-fix build: text 434,012 B, data 412 B, BSS 290,788 B. Clean, no
warnings.
