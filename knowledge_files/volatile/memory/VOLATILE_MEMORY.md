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

## Current carryover after Session 074

Session 074 (2026-09-29/30, `dev-ph5-effects`) largely completed Phase 5:
Effect-page automation underlines, CrumpBit (the first arena Effect), the
master bus compressor, the `xfd` fader mode, and the AutoSave torn-record
fix. All were accepted on hardware by the user.

- **Commits:** HEAD `50610dd` holds every S074 code change. The closeout
  docs and the user's move of the four DSP specs into
  `knowledge_files/specification_reference/dsp_instruments_effects/` are
  uncommitted. The user manages commits.
- **Final link:** `text=502,512`, `data=416`, `bss=426,392`; payload
  502,928 B of 753,664 (250,736 B free); ITCM 4,168 B; DTCM statics 4,480 B;
  FX arena 126,592 B. `LXRV2_lxr02.img` SHA-256 `63eec2a6…aeb0` (record the
  `.img`, not the unstamped `.bin`).
- **Durable authorities:** `074_SESSION_HANDOFF_LOG.md`; the
  `dsp_instruments_effects/` references; `AUTOSAVE.md`; `DEV_MODES.md`;
  `ASYNCFATFS_REFERENCE.md`; `STORAGE_SRAM_MANIFEST.md`.
- **Disposable:** the ten root `S074_*.md` documents.
- **Next session (075):** `S075_PH6_COPY_CLEAR.md`, Phase 6 copy/clear. It
  opens with decisions; do not start implementing before the user answers.
- **Open (details in `knowledge_files/volatile/S070_WORKING_NOTES.md`):** the
  boot timeout; the `cpu` widget with `cmp` on; BC11; O1; the `PM63` pan
  display; F4 trace priorities; stale tools and comments.
- **Hardware still pending from S072:** the Phase 5 Step 6–10 matrices.

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

All pool-mutating operations from Menu, Sequencer, copyClearTools, and
EuklidGenerator route through `patSvc_*` (PatternStackService), not direct
`pat_*` mutation calls. TIM3's automation read path reads address entries
directly (not through the service) — address entries are always consistent
due to the publication ordering fix. `patSvc_idle()` must be called at all
5 filesystem replacement boundary points.

### Next feature: Phase 6 copy/clear (Session 075)

`pat_copyTrack`, `pat_copyPattern`, `pat_copyBar` are deliberate no-ops.
Their implementation requires independent pool-block duplication and must
route through the Pattern Stack Service. `S075_PH6_COPY_CLEAR.md` (root)
widens this to step, bar, track, automation, Instrument, Scene and Scene
components, and lists the decisions to take first.

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
