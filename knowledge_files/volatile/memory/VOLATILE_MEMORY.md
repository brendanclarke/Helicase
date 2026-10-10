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

## Current carryover after Session 078

Session 078 (2026-10-08/10, `dev-ph6-cleanup`) implemented per-track timing
(continuous 128-position step scale with Q8.8 DDA, FX sequencer on the same
curve, shuffle, run modes, morphable/automatable length/scale/shuffle), STEP
multi-step editing and the held-step track automation overlay, the STEP
underline/pot-clear fix, and the run-mode remediation.

- **Commits:** P1 `0c23def`, P2+P3 `b537e6f`; P4 and the close-out docs are
  uncommitted. The user manages commits.
- **Final link (DEV):** `text=539,856`, `data=416`, `bss=427,616`; payload
  540,272 B of 753,664 (213,392 B free).
- **Durable authorities:** `078_SESSION_HANDOFF_LOG.md`;
  `PATTERN_DYNAMIC_STACK.md` §6.3a/§6.4; `BANK_PRESET_ARCHITECTURE.md` §5/§8;
  `MODULE_INTERCHANGE_SPEC.md`.
- **Next session (079):** carry-over list in
  `knowledge_files/volatile/S078_WORKING_NOTES.md` (STEP marker retry
  decision, remaining retest rows, production build). S078 P1–P4 are all
  hardware PASS.

### Working preferences confirmed in Session 078

- **Root cause first, then a narrow fix document.** For a reported defect the
  user asks for the "precise, targeted reason" and a narrowly scoped fix
  written to a named root document, before any code. Read the code, rule out
  alternatives explicitly, and do not pad with speculative diagnostic plans
  when the code already proves the cause.
- **Plan → schedule → implement → review → test.** The user asks for a
  per-change implementation schedule (file, line, add/remove/modify, full
  comment blocks), then implements (or asks the assistant to), then asks for
  an assessment appended to the schedule, then tests on hardware and asks for
  results to be marked in the documents.
- **Deviations found in a deep dive are listed explicitly** and the plan
  document is updated to match the schedule when asked.
- **"Doesn't work" without detail:** ask which part, what was seen, and which
  image was flashed before changing code (S078: it was a wrong image).
- **Labels follow existing vocabulary.** The user rejects poor abbreviations
  (`mod`) and wants a concept to read the same everywhere (track `run` /
  `Track` / `RunMode`, matching the FX sequencer's `run` / `RunMode`).
- **SHIFT Morph-view rule (binding):** a non-morphable parameter shows and
  edits its Normal value in every Morph view; never `---`, never locked.
- **Session closeout also updates `SCOPING_TARGETS.md`** for what was
  accomplished (this or earlier sessions).

### Working preferences confirmed in Session 075

- **Use the user's terms.** Do not introduce new semantic terms the user has
  not used; when a code name is needed, mark it as a code name. (The S075
  spec carried a terms table for this reason.)
- **Comment blocks beside every change in both `.c` and `.h`,** at detailed
  contract level (what, why, inputs, outputs, accessors, affiliates); schedules
  give the exact block for each change.
- **"No code change this turn"** means a schedule only. When the user says
  "implement this yourself", implement directly (the S075 overlay
  follow-up).
- **Decision rounds:** each plan lists numbered questions with a
  recommendation; the user answers tersely (e.g. "Q1: yes 2: approve"); fold
  the answers into the plan, then report any further follow-ups before
  scheduling.
- **The user rejects workarounds that feel hacky** (an inert SELECT block
  under the overlay) and asks for the behaviour to be stated as a rule
  ("type SELECT functions keep working; only screen changes are
  suppressed").
- **No backward compatibility** when the user will wipe temporary records:
  do not add migration code for AutoSave or sceneset changes unless asked.
- **One UX for one concept** (all pans display and default the same; maths
  may differ).
- **Automation always wins**, menu edits only set endpoints, MIDI is lowest
  priority (binding product rule).
- **Hardware feedback arrives as numbered lists;** treat each item as a rule
  to restate in the plan's feedback table, find the root cause in code before
  proposing a change, and keep a verification table per item.
- **Session closeout:** terse index entry, verbose handoff log, spec updates
  (new reference documents only when the user names them), `MEMORY.md`,
  volatile notes; preserve every detail of the root task documents because
  the user deletes them.

### Working preferences confirmed in Session 074

- **"Do not change code files yourself this turn"** means schedule only;
  the user (or an implementing agent they direct) applies it. The assistant
  wrote code directly only when asked (the compressor tuning and the
  saturator).
- **No user error screens for failures that recover by themselves**; log
  them in the trace instead (AutoSave torn-record policy).
- **RAM approvals are explicit and scoped** ("ram expansions approved" for
  the saturator's +8 B). Still state bytes, region, lifetime and owner.
- **Tuning requests are relative and smooth across the range** ("just
  slightly more", "extremely mild at low values to fairly extreme at the
  top"). Implement them as curves over the control, show before/after tables,
  and keep the change small.
- **Correct your own earlier numbers visibly** when evidence contradicts
  them (the drain poll counts, the validation count, the CPU estimate).
- **Keep documents consistent with the code at every step:** status lines,
  work notes and acceptance sections were updated in each S074 document as
  work landed.

### Working preferences confirmed in Session 073

- **Constant CPU.** Never propose saving CPU by skipping DSP work when
  something is inactive, silent or at zero. If an idea does, raise it
  specifically; the expected answer is no (`MEMORY.md`, DSP CPU Policy). The
  bus compressor's "no work while off" is the one user-approved exception
  (S074).
- **No new utilities, no profiler, no extra CPU widgets.** Prove DSP changes
  with `tools/dsp_test/` (host `cc`, `python3` standard library, the ARM
  toolchain).
- **Do not add unrequested features, knobs or test builds.** The S073 growth
  drill knob was added without a request and had to be removed ("i never
  asked for that"). Ask before adding anything outside the request.
- **Hardware checks are the user's** (listening, the worst-case Scene,
  controls, the Effect budget). Hand over the list; do not claim them.
- **Big refactors:** the assistant writes a line-level implementation
  schedule; the user implements; the assistant reviews the diff, reruns every
  gate, reports concisely, and corrects documentation that misstates things.
- **Report RAM changes of every kind,** including ITCM code growth, with
  byte count, region, lifetime and owner.

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
glyph for one of the 4 visible cells (VOICE and, since S072/S074, Effect
page). Diff-based CGRAM transactions with retry bit
(`VA_MARKER_RETRY_BIT = 0x10`) prevent redundant LCD writes; one shared retry
serves both page families since S074. Slot allocation is deterministic (cell
index + 2). **A redraw that can move a marker between cells must use
`menu_repaint()`**, not `menu_repaintAll()`, or the new glyph flashes in the
old cell (S066 Fix 5; S074 Effect hold/release fix).

### Pattern Stack Service routing

All pool-mutating operations from Menu, Sequencer and EuklidGenerator route
through `patSvc_*` (PatternStackService), not direct `pat_*` mutation calls.
The one exception (S075) is copy/clear's service, which writes through the
PatternData raw block API only while it holds `patSvc_beginExclusive()`. TIM3's automation read path reads address entries
directly (not through the service) — address entries are always consistent
due to the publication ordering fix. `patSvc_idle()` must be called at all
5 filesystem replacement boundary points.

### Phase 6 copy/clear (Session 075, implemented)

The Session 062 no-op copy APIs are gone. Copy/clear lives in
`Core/Menu/CopyClear/` and is documented in `COPYCLEAR_UTILITIES.md`. Pastes
duplicate pool blocks step by step through the swap block (never alias
another step's block) and publish before free.

### Automation priority (Session 075 F3)

Automation, then menu edits, then MIDI. Every Morph-base runtime write goes
through `presetMorph_writeRuntimeBase()`, which skips a parameter while
`seq_automationHoldsParameter()` reports it held; the trigger restore then
applies the latest `morph_interpolation[]`. Menu edits call
`presetMorph_applyParameterNow()` for one parameter only; external MIDI
stores an endpoint (`preset_setInstrumentParameterFromMidi()`).

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
