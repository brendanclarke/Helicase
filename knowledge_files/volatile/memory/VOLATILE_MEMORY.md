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

## Current carryover after Session 072

Session 072 implemented Phase 5, the Effects bus (`EFFECTS_BUS_FEATURE_PLAN.md`
Steps 1–11), on `dev-ph5-effects`.

- **Commits:** HEAD `58569ae` = Steps 1–8; Steps 9–11 are uncommitted. You
  manage commits.
- **Final link:** `text=483,024`, `data=416`, `bss=426,336`; flash payload
  483,440 B; **8,080 B flash headroom**.
- **Durable authorities:**
  `knowledge_files/log_archive/072_SESSION_HANDOFF_LOG.md` (step record,
  RAM ledger, carried debt, and the §11 Phase 5 hardware acceptance
  checklist) and
  `knowledge_files/specification_reference/EFFECTS_BUS_REFERENCE.md` (as
  built).
- **Disposable:** the `S072_ST1..ST11_IMPLEMENTATION.md` step documents.
- **Next session (073):** `S073_FLASH_EXPANSION.md`, then
  `S073_CPU_USE_DSP_REDUCTION_REFACTOR.md`. Read `S073_SESSION_STARTUP.md`
  first.
- **Hardware still pending:** the Step 6–10 matrices. Run the checklist when
  the hardware is available. It does not block the S073 flash work, but S073
  test images must keep Effect behaviour intact.

### Effect-system invariants (Session 072)

- **Writers.** Only SceneData writes `scene_t.effect` and
  `effect_morph_amount`. UI, type hooks and the FX lock editor write only
  through the EffectsManager edit API (`effects_setParameter`,
  `…SeqRunMode/Length/StepScale`, `…MorphAmount`, `…SeqLaneLock`,
  `effects_changeType`). Those fan out through the active VOICE edit mask
  and mark AutoSave.
- **Foreground only.**
  - EffectsManager and FxBuffer must never be called from an ISR.
  - TIM3 only publishes `seq_fxEvent`, Effect step markers (pending identity
    bit 11) and the automation reset latch.
- **Arena.** `.dtcm_fxbuf` is never cleared by the system. An owner clears
  what it reads unless it deliberately adopts content the handoff marks
  valid.
- **Type-relative indices.** A local Effect row index means the same
  parameter only within one type. The VOICE-mask layout gate plus
  `bank_revalidateVoiceEditMasks()` keep fan-out safe; runtime re-validates
  against the live type.
- **IDs.** 511 is never an Effect target; Effect local 63 is never
  automatable.
- **Blank names.** They save as `none.*` (all-space stem); an empty stem
  becomes `inst`. Pass explicit spaces.
- **Resolution order** (`effects_service()`, every block): Morph base
  (Pattern `fxm` > held FX Morph lane > retained) → LFO `fxm` → interpolate
  → FX lock → Pattern overlay → LFO → clamps.

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
Do not reintroduce `/2` or `*2` conversions for voice automation values.
The one deliberate exception is Effect rows flagged `EFFECT_PARAM_FLAG_WIDE8`,
which expand through their descriptor's `expand7` hook (Session 072). `fxm`
and voice Morph expand with `v<127 ? 2v : 255`.

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

### Working preference: commits

The user manages commit timing. Do not recommend, schedule, or gate work on
committing in reviews, implementation schedules, or replies.

### Working preference: scope discipline (Session 072)

Stay strictly inside the requested feature's framework. Do not modify code
outside it unless the plan requires it, and never "fix while passing"; the
user was explicit: no side quests. Report unrelated findings instead, and log
them in `SCOPING_TARGETS.md` or the session log for a later pass.

### Working preference: step schedules (Session 072 method)

For multi-step features the user implements the code. The assistant:

1. writes a per-step implementation schedule listing every change by file,
   line and add/remove/modify, with full comment blocks;
2. after implementation, reviews the diff against the schedule, runs a clean
   rebuild plus `link_budget.py`, and appends an assessment;
3. folds findings into the next schedule as prerequisites.

Every schedule states its flash estimate and RAM (byte count, region,
lifetime, owner).
