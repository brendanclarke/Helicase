# COPY/CLEAR Audit (Session 024)

## Scope
- Reviewed:
  - `Core/Menu/copyClearTools.c`
  - `Core/Menu/menu.c`
  - `Core/Hardware/frontPanel/buttonHandler.c`
  - Reference: `knowledge_files/LXR-master/front/LxrAvr/Menu/copyClearTools.c`
  - Reference behavior path: AVR `frontPanel_sendData(...)` → STM32 parser copy/clear handlers.

## Reference Behavior Summary
- On original LXR, `copyClearTools.c` sends copy/clear commands through the inter-CPU protocol.
- Mainboard parser applies operations to the currently shown pattern (`frontParser_shownPattern`) and selected/packed source/destination.
- In clear mode, encoder movement selects target (`track`, `pattern`, `autom.1`, `autom.2`) before executing clear.

## Findings
1. `Core/Menu/copyClearTools.c` in this port is functionally aligned with the reference intent.
- It correctly replaced protocol sends with direct sequencer calls:
  - `seq_clearTrack(...)`
  - `seq_clearPattern(...)`
  - `seq_clearAutomation(...)`
  - `seq_copyTrack(...)`
  - `seq_copyPattern(...)`
- It uses `menu_getViewedPattern()` / `menu_getActiveVoice()`, which is consistent with the original shown-pattern workflow.

2. The actual regression causing copy/clear to appear broken was in `Core/Menu/menu.c`, not `copyClearTools.c`.
- In `menu_parseEncoder()`, the `copyClear_isClearModeActive()` branch had been left as a stub.
- Effect:
  - Encoder could not change clear target.
  - Clear mode UI looked active, but users were effectively locked to default clear target and got confusing behavior.

3. Copy mode press/release behavior matches original firmware.
- `COPY` must be held while selecting source/destination; release exits copy mode.
- This is original behavior, not a new port bug.

## Fix Applied
- Implemented missing clear-mode encoder handling in `Core/Menu/menu.c`:
  - Read current target with `copyClear_getClearTarget()`
  - Increment/decrement within bounds `CLEAR_TRACK..CLEAR_AUTOMATION2`
  - Apply with `copyClear_setClearTarget(...)`
  - Return early (as in reference) to avoid normal menu navigation while in clear mode.

## Verification
- Build completed successfully after fix: `make -j4`
- No new compile errors introduced.

## Conclusion
- `copyClearTools.c` direct sequencer wiring is correct.
- The user-facing failure came from an incomplete clear-mode control path in `menu.c`.
- With the encoder clear-target logic restored, copy/clear workflow now matches reference behavior more closely.
