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

## Current carryover after Session 064

Pattern AutoSave functional acceptance is closed. Do not recreate tests that
depend on interrupting a specific background phase or on unfinished Live
Record behavior. The durable authority is
`knowledge_files/log_archive/064_SESSION_HANDOFF_LOG.md`, `AUTOSAVE.md`, and
`PATTERN_DYNAMIC_STACK.md`; the S063/S064 working plans and raw SD-card fixture
directories are disposable. The next normal Pattern feature is Phase 4.5 copy
operations with real pool-block duplication. Fault-injection, record/erase
gate, CRC-corruption, and performance tests remain optional instrumented work,
not blockers or identified defects.
