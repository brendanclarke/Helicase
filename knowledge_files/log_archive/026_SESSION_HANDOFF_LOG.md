# 026 Session Handoff Log

```
DATE: 2026-05-25
SESSION GOAL: Diagnose load/save button display glitch; implement malformed-file name
              fallthrough (show '-' for files too short to contain the 8-byte header)
COMPLETED: Full root-cause audit of load/save button glitch with fix documented but
           NOT yet applied to menu.c (menu.c was not in the uploaded file set);
           full call-chain audit for malformed-file name path; filesystem.c edited
           with both fixes (filesystem_loadName_tick phase 2 and
           filesystem_loadKit_tick phase 2); first-attempt fix was wrong and
           corrected after real-file regression testing.
VERIFIED ON HARDWARE: No

CHANGES THIS SESSION:
- filesystem.c: filesystem_loadName_tick() phase 2 — replaced single
  progression condition with three-way branch; uses afatfs_feof() for EOF
  detection; zero-byte or short files set loaded_name to "-       " and
  advance to close phase rather than hanging or displaying padded noise.
- filesystem.c: filesystem_loadKit_tick() phase 2 — same structural fix;
  EOF before 8 bytes sets preset_currentName to "-       ",
  op_close_status = FS_STATUS_ERROR, and jumps to phase 4 (CLOSE) to
  prevent clobbering parameter_values[] from an invalid file.
- LOAD_SAVE_GLITCH_ASSESSMENT.md: written (see below)
- FILE_FALLTHROUGH_AUDIT.md: written (see below)

KNOWN ISSUES INTRODUCED: None
KNOWN ISSUES RESOLVED: filesystem_loadName_tick zero-byte hang; filesystem_loadKit_tick
                        zero-byte hang; malformed/short file padded-name display.

NEXT SESSION RECOMMENDED GOAL: Apply the menu.c load/save button glitch fix (one
  two-line reorder in menu_switchPage() case LOAD_PAGE — documented in
  LOAD_SAVE_GLITCH_ASSESSMENT.md). Then hardware-test both the glitch fix and the
  filesystem malformed-file path.
BLOCKERS: menu.c load/save glitch fix is documented but not yet applied.

CRITICAL REMINDERS FOR NEXT SESSION:
- The menu.c load/save glitch fix is a two-line reorder ONLY — update menu_activePage
  BEFORE calling menu_resetSaveParameters() in case LOAD_PAGE. Do not restructure
  further. Full details in LOAD_SAVE_GLITCH_ASSESSMENT.md.
- filesystem.c malformed-file fix uses afatfs_feof(op_file) for EOF detection. Bare
  n == 0 is NOT an EOF signal in asyncfatfs — it means the buffer is not ready yet.
  Do not regress this.
- PAR_EXT_SYNC (midi auto-sync) now occupies the slot where PAR_FETCH lived in
  LXR037. Files saved on our system and opened in LXR037 (or vice versa) may have
  parameter offset mismatches at this location. Minor TODO; no fix required now but
  must not be forgotten. See Minor TODOs section below.
```

---

## Session Detail

### 1. Load/Save Button Display Glitch — Root Cause and Fix

**Symptom**: When pressing the load/save button from any normal voice or sequencer
page, the LCD briefly flashes the currently selected parameter in full edit-mode view
— identical in appearance to an encoder click — before the load/save page appears.

**Root cause identified in `menu_switchPage()`, `case LOAD_PAGE:`**

```c
// CURRENT (buggy) order:
case LOAD_PAGE:
    menu_resetSaveParameters();          // (1) fires repaint here ...
    if (menu_activePage == LOAD_PAGE)
        menu_activePage = SAVE_PAGE;
    else
        menu_activePage = LOAD_PAGE;     // (2) ... but page changes here
    menu_requestCurrentLoadSaveSelection(0);
    break;
```

**Execution trace**:

1. `menu_resetSaveParameters()` is called before `menu_activePage` is updated.
2. Because `menu_saveOptions.what` starts at `0` (`SAVE_TYPE_KIT < SAVE_TYPE_GLO`),
   the `else` branch executes: `editModeActive = 1` then `menu_repaintAll()`.
3. `menu_repaintAll()` → `menu_repaint()` → dispatches on `menu_activePage`, which
   is still the old voice/seq page → calls `menu_repaintGeneric()`.
4. With `editModeActive = 1`, `menu_repaintGeneric()` renders the full-screen
   category/long-name/value edit view for the currently selected parameter. This
   frame is queued to the TIM7 LCD drain and becomes briefly visible.
5. Control returns to `menu_switchPage()`, which then updates `menu_activePage` and
   calls `menu_repaintAll()` again — correctly painting the load/save page.
6. The TIM7 async drain means the erroneous frame was already dispatched before the
   correct frame overwrites it.

The display is visually identical to an encoder click because `editModeActive = 1`
is exactly what `menu_parseEncoder()` sets on button press, and the repaint is
immediate in both cases.

**Fix (NOT YET APPLIED — apply to menu.c next session)**:

```c
// CORRECTED order:
case LOAD_PAGE:
    if (menu_activePage == LOAD_PAGE)    // update activePage FIRST
        menu_activePage = SAVE_PAGE;
    else
        menu_activePage = LOAD_PAGE;
    menu_resetSaveParameters();          // repaint now sees the correct page
    menu_requestCurrentLoadSaveSelection(0);
    break;
```

With this order, the `menu_repaintAll()` inside `menu_resetSaveParameters()` sees
`menu_activePage` already set to `LOAD_PAGE` or `SAVE_PAGE` and calls
`menu_repaintLoadSavePage()` from the first frame. No spurious edit-mode frame is
ever queued. The second `menu_repaintAll()` at the bottom of `menu_switchPage()` is
harmless.

The `editModeActive = 1` assignment in `menu_resetSaveParameters()` is correct
load/save UI state (enables the bracket cursor on the preset number field). The bug
is not the assignment itself but the page not yet being updated when it fires.

**Full assessment written to**: `LOAD_SAVE_GLITCH_ASSESSMENT.md`

---

### 2. Malformed File Name Fallthrough — Full Audit

**Goal**: If a file exists on SD but is too short to contain the 8-byte ASCII name
header, show `-` in the slot-name display instead of hanging or showing padded noise.

#### Call chain

```
menu_requestCurrentLoadSaveSelection()
  └─ preset_loadName(slot, what)
       └─ filesystem_requestLoadName(type, slot, on_name_load_complete)
            └─ [async] filesystem_loadName_tick()      ← PRIMARY FIX SITE
                 on_name_load_complete()
                   └─ preset_completeFilesystemOp(PRESET_OP_NAME_LOAD)
menu_pollPresetStatus()
  └─ PRESET_OP_NAME_LOAD → preset_applyLoadedName()
       └─ memcpy(preset_currentName, filesystem_loadedName(), 8)
menu_repaintLoadSavePage()
  └─ memcpy(&editDisplayBuffer[1][5], preset_currentName, 8)

kitBrowser_tick() / kitBrowser_init()
  └─ filesystem_requestLoadName(FS_FILE_KIT, slot, kb_onNameLoaded)
       └─ [async] filesystem_loadName_tick()            ← SAME FIX SITE
            kb_onNameLoaded()
              └─ memcpy(kb_kitName, filesystem_loadedName(), 8)

preset_loadDrumset()
  └─ filesystem_requestLoad(FS_FILE_KIT, slot, on_kit_load_complete)
       └─ [async] filesystem_loadKit_tick()             ← SECONDARY FIX SITE
            phase 2: reads 8-byte name into preset_currentName
```

#### Bugs found

**Bug 1 — `filesystem_loadName_tick()` phase 2: zero-byte file hangs forever**

The progression condition was `(op_bytes_done >= 8 || (n == 0 && op_bytes_done > 0))`.
For a completely empty file, `n == 0` and `op_bytes_done == 0` on every tick — neither
branch fires. The FSM stalls in phase 2 permanently: `FS_STATUS_BUSY` never clears,
the completion callback never fires, `pm_status` stays `PRESET_LOAD_IN_PROGRESS`
indefinitely, `menu_storageBusy` never clears. UI locks.

**Bug 2 — `filesystem_loadName_tick()` phase 2: 1–7 byte file shows padded noise**

The `(n == 0 && op_bytes_done > 0)` branch advances to phase 3 but fills the
unread tail with spaces, producing a name made of whatever partial bytes were
present plus padding. For a malformed file this is indistinguishable from a valid
short name.

**Bug 3 — `filesystem_loadKit_tick()` phase 2: same zero-byte hang**

Identical structure and identical hang. Additionally: if somehow the phase 2 name
read progressed despite short data, phase 3 would silently zero-fill
`parameter_values[]` from an invalid file — clobbering the active kit.

#### Fix attempt 1 — WRONG (caused regression on all files)

Initial fix used bare `n == 0` as the EOF condition:

```c
} else if (n == 0) {   // ← WRONG in asyncfatfs context
```

This caused all files to show `-` because `afatfs_fread()` routinely returns `n == 0`
while the SD buffer is being populated — it is not an EOF signal. Tested with real
`.SND`, `.PRF`, and `.ALL` files: all showed `-`. Files confirmed:
- `P030.SND` (236 bytes, name `Slak\xfe   `) — good file
- `P031.SND` (229 bytes, name `Slak3   `) — good file
- `P002.PRF` (50946 bytes, name `2prfm   `) — good file
- `P002.ALL` (0 bytes) — correctly malformed
- `P000.PRF` (0 bytes) — correctly malformed

#### Fix attempt 2 — CORRECT (applied to filesystem.c)

All other read phases in `filesystem.c` use `n == 0 && afatfs_feof(op_file)` for
EOF detection — 20+ call sites confirmed by grep. Corrected both patches to match:

**`filesystem_loadName_tick()` phase 2 (applied)**:
```c
case 2: /* READ */
{
    uint32_t n = afatfs_fread(op_file,
                              (uint8_t *)loaded_name + op_bytes_done,
                              8 - op_bytes_done);
    op_bytes_done += n;
    if (op_bytes_done >= 8) {
        /* Full name read — sanitize and proceed. */
        loaded_name[8] = '\0';
        uint8_t i;
        for (i = 0; i < 8; i++)
            if (loaded_name[i] < 0x20 || loaded_name[i] > 0x7E)
                loaded_name[i] = ' ';
        op_phase = 3;
    } else if (n == 0 && afatfs_feof(op_file)) {
        /* EOF before 8 bytes (including zero-byte file) — malformed.
        ** Show '-' so the display distinguishes this from an empty slot. */
        loaded_name[0] = '-';
        memset(loaded_name + 1, ' ', 7);
        loaded_name[8] = '\0';
        op_phase = 3;
    }
    /* else: async buffer not ready yet — wait for more data next tick. */
    return;
}
```

**`filesystem_loadKit_tick()` phase 2 (applied)**:
```c
case 2: /* READ_NAME */
{
    uint32_t n = afatfs_fread(op_file,
                              (uint8_t *)preset_currentName + op_bytes_done,
                              8 - op_bytes_done);
    op_bytes_done += n;
    if (op_bytes_done >= 8) {
        /* Full name read — sanitize and proceed to param read. */
        uint8_t i;
        for (i = 0; i < 8; i++)
            if (preset_currentName[i] < 0x20 || preset_currentName[i] > 0x7E)
                preset_currentName[i] = ' ';
        op_phase = 3;
        op_bytes_done = 0;
    } else if (n == 0 && afatfs_feof(op_file)) {
        /* EOF before 8 bytes — file is malformed; close and report error.
        ** Do not proceed to param read: parameter_values[] must not be
        ** clobbered with zeroes from an invalid file. */
        preset_currentName[0] = '-';
        memset(preset_currentName + 1, ' ', 7);
        op_close_status = FS_STATUS_ERROR;
        op_phase = 4; /* jump straight to CLOSE */
    }
    return;
}
```

`op_close_status = FS_STATUS_ERROR` causes `filesystem_finish(FS_STATUS_ERROR)` after
close, which routes through `preset_completeFilesystemOp()` with `FS_STATUS_ERROR` →
`pm_completed_op = PRESET_OP_NONE` → `menu_pollPresetStatus()` `default:` branch →
`menu_resetSaveParameters()` and repaint. No parameters are loaded from the invalid
file.

#### Files not requiring changes

| File | Reason |
|------|--------|
| `presetManager.c` | `preset_completeFilesystemOp()` error path already correct |
| `menu.c` | Renders `preset_currentName` verbatim — correct once FS layer fixed |
| `kitBrowser.c` | `kb_onNameLoaded()` copies `filesystem_loadedName()` verbatim — correct |
| `presetManager.h`, `filesystem.h`, `kitBrowser.h` | No API changes needed |

**Full audit written to**: `FILE_FALLTHROUGH_AUDIT.md`

---

### 3. Minor TODO — PAR_EXT_SYNC / PAR_FETCH Slot Conflict with LXR037

`PAR_EXT_SYNC` (the MIDI auto-sync parameter) in this port occupies the parameter
array slot where `PAR_FETCH` lived in the original LXR 0.37 firmware. No action
required now, but this creates a potential parameter offset mismatch: `.SND`, `.ALL`,
or `.PRF` files saved on LXR-02 and then loaded back on an LXR037 system (or vice
versa) may have the sync/fetch byte misinterpreted. Track and resolve before any
cross-system file interchange is needed.

---

## Summary of File Changes

| File | Changed | Notes |
|------|---------|-------|
| `filesystem.c` | **Yes** | Two phase-2 read-name fixes (loadName_tick + loadKit_tick) |
| `menu.c` | **No** | Fix documented in LOAD_SAVE_GLITCH_ASSESSMENT.md; apply next session |
| All other files | No | — |
