# Agent Behavioral Notes

Notes for Claude Code / LLM agent sessions on this project.
Project context lives at root `MEMORY.md`. Technical specs live in
`knowledge_files/specification_reference/`. Session history lives in
`knowledge_files/log_archive/`.

## Do not use `.claude/memory/`

All project memory is in the project directory only. Do not create files
in `.claude/projects/*/memory/`.

## Session log consolidation workflow

When consolidating session docs into the permanent record, follow the
sequence in `knowledge_files/SESSION_HANDOFF_TEMPLATE.md`: read all source
docs fully, verify real code state via `git log`/`git diff` (don't trust
planning docs when ambiguous), write terse index entry then verbose log
then spec updates then root `MEMORY.md`. See the template and prior
handoff logs for format.

## File creation policy

Never create a new file unless the user's message explicitly names the
filename. If a task seems to call for a new file but the user hasn't
specified one, ask first — don't assume.

## Capture budget

Do not recommend bumping `AUTOSAVE_PARAMETER_GETS_PER_WRITE` or adjusting
capture timing unless the user explicitly asks. The section-based CRC
format redesign is the chosen path for write performance.

## Current carryover after Session 066

Session 066 implemented VOICE-page held-step automation overlay (Method 2).
Session 065 implemented step automation Method 1 (step-edit menu and sequencer
playback). The durable authorities are
`knowledge_files/log_archive/066_SESSION_HANDOFF_LOG.md`,
`knowledge_files/log_archive/065_SESSION_HANDOFF_LOG.md`,
`PATTERN_DYNAMIC_STACK.md`, `MODULE_INTERCHANGE_SPEC.md`, and
`SRAM_MANIFEST.md`.
The S066 planning documents (`S066_DYN_PAT_VOICE_PARAM_UX.md`,
`S066_IMPLEMENTATION_SCHEDULE.md`, `S066_DYN_P-LOCK_FOLLOW-UP.md`) and the
S065 planning documents (`S065_DYN_PAT_STEP_AUTOMATION.md`,
`S065_DYN_PAT_STEP_AUTOM_EDITING.md`, `S065_DYN_PAT_AUTOM_EDITING_ADDITIONS.md`)
are disposable; their durable facts are in the handoff logs and spec references.

### Automation ordering invariant

`seq_drainPendingAutomation()` must run inside `audio_check_and_render()`
immediately after `voiceControl_processPending()`, within the per-chunk render
loop. This ensures triggers are processed before automations within the same
render chunk. Moving the drain outside the render loop caused a 50% automation
failure rate in Session 065 testing.

### Restore source invariant

`seq_restoreAutomatedParameters()` must restore from `morph_interpolation[]`,
not `instrument_parameters[]`. The latter is Scene A only; `morph_interpolation`
is the runtime-interpolated value that accounts for Morph position.

### ISR safety

`instrumentManager_writeRuntime()` is NOT ISR-safe. Automation values are
buffered in the TIM3 ISR pending buffer and drained in the foreground only.
Do not call `instrumentManager_writeRuntime()` from any interrupt context.

### 7-bit automation storage with 8-bit display

Automation values are stored as 7-bit (0..127). Display expansion to 8-bit:
`(v == 127) ? 255 : v * 2`. Inverse: `(v >= 255) ? 127 : v / 2`. The
working-value cache (`va_workingValue[4]`) stores 8-bit values to avoid lossy
8→7→8 round-trip during pot edits. Nibble-split suppression byte
(`va_suppress[4]`): lower nibble = underline suppression, upper nibble =
working value validity.

### CGRAM underline cache

Four-slot bounded cache in CGRAM slots 2..5. Each slot holds a 5×8 underline
glyph for one of the 4 voice automation parameters. Diff-based CGRAM
transactions with retry bit (`VA_MARKER_RETRY_BIT = 0x10`) prevent redundant
LCD writes. Slot allocation is deterministic (voice parameter index + 2).

### Next feature: S067 defragmentation service and pool monitor

`S067_DYN_PAT_STACK_SERVICE.md` covers: two-tier defragmentation (micro-
relocation + global compaction), bounded per-tick work budget, pool usage
monitor in settings menu (`pol:NN`). 33 bytes RAM budget estimated.
Phase 4.5 copy operations remain deferred.
