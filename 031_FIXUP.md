# 031 Fixup Audit

## Problem

Session 031 introduced a boot hang in the async filesystem state machine. The
unit boot path blocks while synchronously loading kit 0:

1. `main.c` calls `preset_loadDrumset(0, 0)`.
2. `preset_loadDrumset()` starts `FS_INTERNAL_OP_LOAD_KIT`.
3. `filesystem_tick()` dispatches to `filesystem_loadKitDirectory_tick()`.
4. The loader opens `Kit/`, enters the selected `Kit/NNN Name/` directory, then
   tries to close the selected kit directory handle before opening
   `kitset.kcg`.

The close phase accidentally advanced back to itself instead of to its wait
phase. Because the boot loop waits until the preset callback completes, this
left boot spinning in `filesystem_tick()` forever before audio/UI startup.

## Code Changes

### `Core/Hardware/SD/filesystem.c`

Implementation note: explanatory comments were added beside each corrected
transition so the close-request/wait-close invariant is visible at the point of
maintenance. These comments are intentionally local because the bug was caused
by a one-number phase slip inside otherwise-valid async state-machine code.

#### Fix selected kit directory close phase

Changed `filesystem_loadKitDirectory_tick()` case 9 from:

```c
op_phase = 9;
```

to:

```c
op_phase = 10;
```

Why: case 9 is the close-request phase and case 10 is the wait-close phase.
After `afatfs_fclose()` accepts the close request, the state machine must wait
for `on_file_closed()` to set `op_close_done`. Re-entering case 9 reissues the
close request forever and prevents the kit load completion callback from firing.

#### Fix kitset parse/error close routing

Changed the two `kitset.kcg` read/parse error branches from jumping directly to
case 15 back to case 14.

Why: case 14 is the phase that actually calls `afatfs_fclose(op_file, ...)`;
case 15 only waits for `op_close_done`. Jumping straight to case 15 after an
open file error skips the close request, so malformed kitset data can also hang
waiting for a close callback that was never scheduled. The error status is still
preserved through `op_close_status = FS_STATUS_ERROR`.

#### Fix standalone pattern save close phase

Changed `filesystem_savePattern_tick()` case 9 from:

```c
op_phase = 9;
```

to:

```c
op_phase = 10;
```

Why: this is the same close-request/wait-close pattern as the kit loader. It is
not the boot hang, because boot does not save patterns, but any pattern save
would hang after writing the payload by repeatedly re-entering its close phase.

## Notes

This fix does not reintroduce legacy single-shuffle import/export. The shuffle
storage decision from Session 031 remains unchanged. The boot hang was caused by
filesystem phase transitions, not by the SHIFT+VOICE blink path.
