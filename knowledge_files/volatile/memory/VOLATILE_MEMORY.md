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

## Current carryover after Session 065

Session 065 implemented step automation Method 1 (step-edit menu and sequencer
playback). The durable authority is
`knowledge_files/log_archive/065_SESSION_HANDOFF_LOG.md`,
`PATTERN_DYNAMIC_STACK.md`, and `MODULE_INTERCHANGE_SPEC.md`.
The S065 planning documents (`S065_DYN_PAT_STEP_AUTOMATION.md`,
`S065_DYN_PAT_STEP_AUTOM_EDITING.md`, `S065_DYN_PAT_AUTOM_EDITING_ADDITIONS.md`)
are disposable; their durable facts are in the handoff log and spec references.
`S065_DYN_PAT_VOICE_PARAM_UX.md` is the Session 066 general plan — keep it.

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

### Next feature: Method 2 VOICE overlay (Session 066)

`S065_DYN_PAT_VOICE_PARAM_UX.md` covers: overlay activation via held-step +
pot turn, two-tier CGRAM underline (value bar tier 1, assigned dot tier 2),
live value display, step illumination of automated steps, async track-wide
target search, pot-to-automation write. CGRAM slots 2-7 are available for
runtime use (0-1 reserved). The `numtostrpu(buf, num, pad)` helper writes
3 characters to buf[0..2]; for 2-digit displays use manual formatting instead.
