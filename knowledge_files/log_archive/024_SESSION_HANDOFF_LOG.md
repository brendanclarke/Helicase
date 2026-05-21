# Session 024 Handoff Log

DATE: 2026-05-21
SESSION GOAL: Audit and fix `Core/Menu/copyClearTools.c` not seeming to work; document findings; then clean up README/MEMORY by moving historical/detail material into MEMORY.
COMPLETED: Copy/clear behavior was audited against LXR-master, a root-cause audit was written, clear-mode encoder target selection/execution was fixed, clear-mode lifetime was corrected for SHIFT/COPY release order, README was trimmed at the requested marker, MEMORY was kept as the verbose canonical technical context, and this session was indexed.
VERIFIED ON HARDWARE: No. Build verification only: `make -j4` passed after the copy/clear code changes. README/MEMORY/index/log edits are documentation-only and were not build-relevant.

CHANGES THIS SESSION:
- `Core/Menu/menu.c`: fixed `menu_parseEncoder()` clear-mode handling. The previous `copyClear_isClearModeActive()` branch was effectively a stub for encoder turns, and encoder button clicks toggled normal menu edit mode before clear mode could own them. Clear mode now intercepts both encoder actions before edit-mode toggling: turns change `CLEAR_TRACK` / `CLEAR_PATTERN` / `CLEAR_AUTOMATION1` / `CLEAR_AUTOMATION2`, and encoder click calls `copyClear_executeClear()`.
- `Core/Hardware/frontPanel/buttonHandler.c`: fixed clear-mode lifetime. SHIFT release no longer cancels `MODE_CLEAR` while COPY is still held (`copyClear_Mode == MODE_CLEAR && !btn_held[BUT_COPY]`). This preserves the user-facing requirement that after SHIFT+COPY/CLEAR, clear mode owns encoder turn/click until both combo buttons are released.
- `COPYCLEAR_AUDIT.md`: added root-level audit. It compares the current `Core/Menu/copyClearTools.c` to the original AVR-side `knowledge_files/LXR-master/front/LxrAvr/Menu/copyClearTools.c` and explains that direct `seq_*` calls were mostly correct; the active breakage was in `menu.c` and clear-mode input ownership.
- `README.md`: removed the content below `# MOVE EVERYTHING AFTER THIS TO MEMORY.md`; reduced the confirmed hardware section to enabled hardware-facing functions plus memory/cache/layout confirmations; left README as the concise entry point.
- `MEMORY.md`: retained the verbose technical context and folded in the moved README details without duplicating the full old README tail. Added/kept details for process reminders, cache/MPU/DTCM layout, boot kit/global load notes, menu/display state, `.SND` length, refactor-session resolutions, MIDI realtime timestamping, and current toolchain flags (`-O2 -flto`, DSP-specific `-Ofast`).
- `knowledge_files/log_archive/000_SESSION_INDEX.md`: appended Session 024 row, summary, and cross-session facts.
- `knowledge_files/log_archive/024_SESSION_HANDOFF_LOG.md`: this log.

COPY/CLEAR AUDIT NOTES:
- Original LXR path: AVR `copyClearTools.c` packed commands into `frontPanel_sendData(SEQ_CC, ...)`; STM32 mainboard parser applied `seq_clearTrack`, `seq_clearPattern`, `seq_clearAutomation`, `seq_copyTrack`, or `seq_copyPattern`.
- Current port path: `Core/Menu/copyClearTools.c` already calls sequencer functions directly using `menu_getActiveVoice()` and `menu_getViewedPattern()`.
- Track copy path: COPY held, voice buttons choose source/destination, `copyClear_copyTrack()` calls `seq_copyTrack(src, dst, pattern)`.
- Pattern copy path: COPY held, part/select buttons choose source/destination, `copyClear_copyPattern()` calls `seq_copyPattern(src, dst)`.
- Clear target path: SHIFT+COPY arms `MODE_CLEAR`; encoder turn selects target; encoder click now executes selected clear.
- Important behavioral note: COPY copy mode is original hold-to-copy behavior. Release COPY exits copy mode via `copyClear_reset()`.

README/MEMORY NOTES:
- README now intentionally stops after the clock configuration section.
- MEMORY is the canonical verbose source for known issues, critical reminders, failed approaches, and moved historical details.
- The old README marker was removed after moving its content.
- The "Current known issues and reminders?" table row now points to MEMORY only.
- A conflicting/stale line from MEMORY was corrected: Session 15's "Trigger backend remains stubbed" note now says it was true at Session 15 and resolved later in Session 019.
- The toolchain note now matches the active Makefile shape: global `-O2 -flto`; DSP sources compile through a specific `-Ofast` rule.

KNOWN ISSUES INTRODUCED: None known.
KNOWN ISSUES RESOLVED:
- Clear-mode encoder turns now change the selected clear target instead of falling through to normal menu behavior.
- Clear-mode encoder click now executes the selected clear instead of toggling standard edit mode / showing step velocity behavior.
- Clear mode is no longer preempted by releasing SHIFT while COPY is still held.

NEXT SESSION RECOMMENDED GOAL: Hardware-test copy/clear workflows end-to-end: clear track, clear pattern, clear automation 1/2, copy track, and copy pattern. Also confirm the README/MEMORY cleanup still has enough detail for new-session startup.
BLOCKERS: Copy/clear fix is build-verified but not hardware-verified. Existing unrelated dirty build artifacts are present in `build/` from `make -j4`.

CRITICAL REMINDERS FOR NEXT SESSION:
- Clear mode must own both encoder turn and click before normal edit-mode toggling.
- Clear mode must remain armed until both SHIFT and COPY are released.
- `knowledge_files/LXR-master/` is read-only reference material.
- README is now concise; MEMORY is the verbose technical context.
- Do not revert `OUTPUT_DMA_SIZE=32` or the TIM3 sequencer timing ownership.

## End of session block

```
DATE: 2026-05-21
SESSION GOAL: Audit/fix copy-clear tools, then clean up README/MEMORY.
COMPLETED: Fixed clear-mode encoder ownership and SHIFT/COPY release lifetime; wrote COPYCLEAR_AUDIT.md; moved README tail content into MEMORY; updated README, MEMORY, and session index.
VERIFIED ON HARDWARE: No. `make -j4` passed after code changes.

CHANGES THIS SESSION:
- Core/Menu/menu.c: clear mode now handles encoder turn and click before edit-mode toggling.
- Core/Hardware/frontPanel/buttonHandler.c: SHIFT release no longer exits clear mode while COPY remains held.
- COPYCLEAR_AUDIT.md: copy/clear audit and root-cause assessment.
- README.md: condensed to intro/tree/hardware/clock front matter.
- MEMORY.md: retained moved details and canonical verbose context.
- knowledge_files/log_archive/000_SESSION_INDEX.md: added Session 024.
- knowledge_files/log_archive/024_SESSION_HANDOFF_LOG.md: verbose session handoff.

KNOWN ISSUES INTRODUCED: None known.
KNOWN ISSUES RESOLVED: Clear target selection, clear execution by encoder click, and clear-mode preemption by SHIFT release.

NEXT SESSION RECOMMENDED GOAL: Hardware-test all copy/clear modes and confirm docs are satisfactory after cleanup.
BLOCKERS: Hardware verification still needed.

CRITICAL REMINDERS FOR NEXT SESSION:
- Clear mode owns encoder turn/click until both SHIFT and COPY are released.
- MEMORY.md is the canonical verbose context; README.md is concise.
```
