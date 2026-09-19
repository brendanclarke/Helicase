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

## Current carryover after Session 067

Session 067 implemented the Pattern Stack Service (unified pool mutation
dispatcher with SPSC queue, two-tier defragmentation, bulk barriers,
filesystem replacement handover, pool usage monitor, and elastic gap policy)
and fixed the dtype automation value offset bug (identity mapping at all
four code sites). The durable authorities are
`knowledge_files/log_archive/067_SESSION_HANDOFF_LOG.md`,
`PATTERN_DYNAMIC_STACK.md`, `MODULE_INTERCHANGE_SPEC.md`, and
`SRAM_MANIFEST.md`.
The S067 planning documents (`S067_STACK_SERVICE_DETAIL_PLAN.md`,
`S067_STACK_SERVICE_IMPLEMENTATION.md`, `S067_DTYPE_OFFSET_BUG.md`,
`S067_DTYPE_BUG_IMPLEMENTATION.md`) are disposable; their durable facts are
in the handoff log and spec references.

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

### 7-bit automation storage — identity mapping

Automation values are stored as 7-bit (0..127). The stored value equals the
parameter value directly (identity mapping). Every automatable descriptor
parameter has a range ≤127. The old MIDI CC-style `/2` `*2` conversion was
removed from all four code sites in Session 067 (dtype offset bug fix).
Do not reintroduce `/2` or `*2` conversions for automation values unless a
DTYPE_0B255 parameter becomes automatable in the future (none is today).

The working-value cache (`va_workingValue[4]`) stores values to avoid
re-reading from pool storage during pot edits. Nibble-split suppression byte
(`va_suppress[4]`): lower nibble = underline suppression, upper nibble =
working value validity.

### CGRAM underline cache

Four-slot bounded cache in CGRAM slots 2..5. Each slot holds a 5×8 underline
glyph for one of the 4 voice automation parameters. Diff-based CGRAM
transactions with retry bit (`VA_MARKER_RETRY_BIT = 0x10`) prevent redundant
LCD writes. Slot allocation is deterministic (voice parameter index + 2).

### Pattern Stack Service routing

All pool-mutating operations from Menu, Sequencer, copyClearTools, and
EuklidGenerator route through `patSvc_*` (PatternStackService), not direct
`pat_*` mutation calls. TIM3's automation read path reads address entries
directly (not through the service) — address entries are always consistent
due to the publication ordering fix. `patSvc_idle()` must be called at all
5 filesystem replacement boundary points.

### Next feature: Phase 4.5 copy operations

`pat_copyTrack`, `pat_copyPattern`, `pat_copyBar` are deliberate no-ops.
Their implementation requires independent pool-block duplication and must
route through the Pattern Stack Service.
