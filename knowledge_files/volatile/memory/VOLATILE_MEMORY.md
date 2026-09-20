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

## Current carryover after Session 068

Session 068 closed and hardware-accepted three independent bugs: (1) the
front-panel button-event ring rebuilt as a 64-entry monotonic SPSC ring with
overflow reconciliation, a physical-held check on the shared hold timer, a
second bounded main-loop drain, and a `K` diagnostic witness before
`pat_toggleStep()` — fixes silently-dropped VOICE-mode step-toggle taps that
were previously being lost in front-panel event delivery, not the Pattern
Stack Service (which the trace evidence proved healthy throughout); (2) the
missing chase-light-after-boot bug, fixed with a new side-effect-free
`menu_setPlayedPattern()` called at all three filesystem Scene/Bank
realignment sites; (3) per-track sequencer step length now read from
`pat_scene_region_t.track_length` instead of a hardcoded 16-step wrap.
`BUTTON_HOLD_DELAY_MS` is now 200ms. Final build `text=447,860`, `data=412`,
`bss=291,196` (+56 bytes `.bss` in `buttonHandler.c` from the event-ring
expansion, approved as a front-panel-integrity exception, not drawn from
either reserved RAM pool). The durable authorities are
`knowledge_files/log_archive/068_SESSION_HANDOFF_LOG.md`,
`PATTERN_DYNAMIC_STACK.md` (§6.4 track settings, §12.7 known Tier 1/Tier 2
oscillation issue), `DEV_MODES.md` (new `U`/`K` AutoSaveTrace stage codes),
`SRAM_MANIFEST.md`, and `MODULE_INTERCHANGE_SPEC.md`.
The five S068 planning documents that fed this closeout
(`S068_PAT_ASSIGN_BUG.md`, `S068_PAT_ASSIGN_BUG_IN_DEPTH.md`,
`S068_PAT_ASSIGN_BUG_IMPLEMENTATION.md`, `S068_MISSING_CHASELIGHT.md`,
`S068_TRACK_SETTINGS_IGNORED.md`) are disposable; their durable facts are in
the handoff log and spec references above.

**Not implemented — Session 069 starting point.**
`S069_ATS_PAT_BOUNDED_CPU.md` is a fully settled plan (all open questions
resolved) diagnosing why Pattern Stack Service maintenance manufactures
continuous, self-generated Pattern-AutoSave dirtiness and CPU work even at
idle — proactive Tier 2 removes the trailing gap Tier 1 just created
(perpetual chase), and every relocation (physically identical bytes, new
pool offset) calls the same dirty-marking path as a real semantic edit. Six
ordered implementation items are specified (physical/semantic dirty split;
replace Tier 1/Tier 2 with owned-slack repair + reactive-only compaction;
remove two O(n) clean-state scans; add a background CPU budget; add Pattern
quiet-window/max-latency scheduling; only then retest AutoSave OFF-to-ON
convergence). **Zero `Core/` code was changed for this plan.** It is
preserved in full in `068_SESSION_HANDOFF_LOG.md` §4 in case the source
document is deleted before Session 069 begins — read that section (or the
still-present `S069_ATS_PAT_BOUNDED_CPU.md`) before starting Session 069.
Per-track step scale and shuffle sequencer consumption are also still
unimplemented (stored/edited/persisted only) and are ordered after the
bounded-CPU work — see `SCOPING_TARGETS.md` § Session 068 deferred items.

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

### buttonHandler concurrency model — foreground scan, not ISR

`buttonHandler_buttonPressed()`/`buttonHandler_buttonReleased()` and the
event ring run from the foreground 500 Hz `din_dout_exchange()` scan via
`timebase_serviceFrontPanel()`, **not** from an ISR. Comments calling this
"TIM6 ISR" context were stale through Session 067 and corrected in Session
068 (`buttonHandler.c`/`.h`). The `volatile` qualifiers on `btn_held[]` and
the event ring remain correct regardless — the foreground scan and the
foreground consumer (`buttonHandler_processEvents()`, called twice per
main-loop pass since Session 068) still interleave around audio rendering.

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
