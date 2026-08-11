# Autosave Phase 2 — Failure Analysis and Remediation Plan

## How to use this document

This is the retained failure analysis and later whole-object plan. Session 047
closed with the bounded-CRC and logging-capture work recorded in
`knowledge_files/log_archive/047_SESSION_HANDOFF_LOG.md`. That handoff
preserves the details formerly held in the two removable remedy documents.

The execution state is now:

- Steps 0–1 are complete: documentation was reconciled and the corrected
  `D/S/A/V/M/C/P/T` lifecycle trace is present, its append completion
  self-acknowledges the shared facade, and the trace-flush admission guard
  avoids colliding with an active Load/Save page.
- Step 2 is complete for the user-testable scalar owner classes. Do not reopen
  a vague matrix: Scene, Kit/Instrument, and Scene-owned MIDI channel/note were
  tested; there is no separate user-editable Bank scalar control.
- Step 3 static reconciliation is complete. The user re-tested the existing
  one-second `settings.cfg` debounce/write behavior in Session 047 and found
  it correct, so no settings source change is authorized. The broader four
  provenance/AutoSave-policy hardware cases remain separately recorded below.
- Step 4 semantics are settled: Bank slot/name are payload, and a structurally
  valid initial record can still be incomplete as a resident-Bank snapshot.
- Step 5 has begun and its normal root-Instrument plus InstrumentMrp boundary
  is hardware-accepted in Session 048. Whole Kit, root Scene without Pattern,
  and selective Bank Load remain separate future owner boundaries. Step 6
  Load/Save exclusion remains future work. The bounded-CRC baseline has passed
  its focused follow-up, but Bank Load/Save retains separate known defects and
  must stabilize before cross-cutting writer-admission changes.

Read this historical analysis together with:

- `knowledge_files/log_archive/045_SESSION_HANDOFF_LOG.md` for the original
  failure sections referenced as “§N” below;
- `knowledge_files/log_archive/046_SESSION_HANDOFF_LOG.md` for the deleted-safe
  pre-Session-047 status of `AUTOSAVE_PARAM_HOOK.md` and the remedy documents;
- `knowledge_files/specification_reference/AUTOSAVE.md` for current behavior;
- `knowledge_files/log_archive/047_SESSION_HANDOFF_LOG.md` for the completed
  CRC/settings/trace/boot-capture chronology and remaining gates.

Nothing in this document authorizes a source change by itself. It is the
review/staging document Topics A–E of the 045 handoff asked for, turned into
an executable order of operations.

---

## Part 1 — What actually happened in Session 045

### 1.1 What succeeded (do not re-litigate this)

Session 045 shipped a real, hardware-verified baseline at commit `326a8a1`.
That statement is historical; current repository authority is its later
`c9807fa` descendant after Session 046's boot/HCNAMES and trace work:

- A 34,768-byte A/B record pair (`/.hcprms1`, `/.hcprms2`) with a 64-byte
  header (magic, version, valid marker, generation, CRC32C, probe byte),
  written in a durable copy → CRC → commit-marker order (§2.2).
- A single 3,856-byte canonical dirty mask owned exclusively by `Autosave.c`,
  with atomic take/re-dirty semantics so a foreground clear cannot erase a
  newer interrupt-side change (§2.7).
- Bounded, non-blocking draining through `filesystem_tick()`
  (`AUTOSAVE_PARAMETER_GETS_PER_WRITE=1536`, `AUTOSAVE_MASK_BITS_PER_TICK=256`,
  5000 ms debounce / 250 ms continuation) — confirmed on hardware draining a
  fully dirty 34,768-byte record over two generations with bit-for-bit
  consistent output (§4.1).
- Phase 1 scalar dirty hooks for Bank/Scene/Kit/Instrument Normal+Morph
  scalar owners, confirmed against a live Scene-0 edit test (§4.2).
- Version-1 `settings.cfg` persistence, including the `AutoSave` on/off
  setting, confirmed on hardware (§4.3). Session 048 removed the former
  sixteen Scene-source keys; the current 17-line settings schema delegates
  provenance exclusively to paired HCNAMES records.
- Two real AsyncFATFS defects found and fixed under hardware load: the
  16,384-byte free-cluster-wrap stop, and a leading-dot short-alias collision
  between `.hcprms1`/`.hcprms2` (§2.3).

This part of the session followed a strict "smallest observable increment,
verify on hardware, only then extend" discipline, and it worked. **The plan
below preserves that discipline** — it is the model to replicate, not the
part that needs fixing.

### 1.2 What failed: Phase 2 (whole-object hooks)

Phase 2's goal was to dirty the canonical mask on whole-object events — Kit
load, Scene load, Instrument load, Morph projection, Bank Load/Save — rather
than only on the Phase 1 scalar setters. This is explicitly unimplemented and
explicitly rejected (§6, §8); its code survives only in `*.failed` /
`*_failed.*` files and must not be reused mechanically.

### 1.3 Root causes (why it failed, not just that it failed)

Reading §6 and §7 of the handoff together, five distinct, compounding causes
stand out. A plan that doesn't address all five will reproduce the failure.

**(a) Conflated identity with payload.** The first Phase 2 attempt treated
live Bank name/slot as record *identity* (i.e., grounds to reject a record),
when they are ordinary mutable *payload* bytes like any other Bank field
(§6.1, and the explicit prohibition in §7 "Do not repeat"). This is a
semantic error made once, then propagated into validator logic, and it
distorted every later boundary decision built on top of it.

**(b) Multiple simultaneous architecture changes per hardware test.** Each
Phase 2 iteration touched several ownership boundaries in one pass — e.g.
"validator policy, BankData ownership, SceneData compatibility access,
filesystem Bank phases, Preset completion, Menu active/pattern alignment, and
Autosave publication in one pass" (§6.2). When hardware behavior didn't match
expectations, there was no way to tell which of the six-plus changes caused
it. This directly violates the discipline that made Phase 1 work, and the
handoff calls it out explicitly as a "Do not repeat" (§7).

**(c) No independent observability.** There was, and still is, no runtime
trace that distinguishes dirty production, scheduler admission, validation,
mask merge, parameter capture, and final commit (§6.5, restated in
BLOCKERS). Every diagnosis in Phase 2 had to be inferred from *final file
state only* — "the files stopped advancing" — which cannot distinguish "the
producer never marked a bit" from "the scheduler never started a
transaction" from "the writer started but never reached the drain." The
handoff is explicit: "the failure was no longer only a whole-object load
notification problem... the common writer admission/lifecycle path was no
longer reaching the drain transaction" — a conclusion reached by elimination,
without proof, because there was nothing to prove it with (§6.5).

**(d) The publication point was moved six times without isolating a
variable.** §6.3 lists six different places the Scene-load notification was
attempted (public load completion, aggregate assignment, parameter-boundary
transfer, a two-byte register, Menu selection with LED toggling, a boot
diagnostic). A later "success" (the boot diagnostic test) turned out to be
an artifact of unrelated Menu reset logic overwriting the accepted Scene
selection, not evidence the architecture was correct (§6.3, final
paragraph). Moving the mechanism repeatedly, rather than root-causing why the
first placement didn't fire, is how a coincidental pass got mistaken for a
fix.

**(e) Ownership churn stacked on an unproven base.** Active-Scene ownership
was reassigned between BankData and SceneData while the Bank Load/Save
semantics it depended on were still unsettled (§6.2, §6.4). Each reassignment
was a plausible individual idea, but composing several unproven ownership
changes together made regressions undiagnosable by construction — there was
no stable baseline between changes to compare against.

**(f) The Load/Save exclusion feature was the terminal break, and it was
never actually diagnosed.** The final change — suspending new autosave starts
while Load/Save owns the filesystem, deferring physical Load/Save entry
during an active autosave transaction — caused output files to stop
advancing *at all*, including mutation bits that were already present before
the change (§6.5). This is a regression in the previously-working Phase 1
path, not just an incomplete Phase 2 feature, and the session ended without
finding out why, for the same reason as (c): nothing observable existed
between "writer admitted" and "writer committed."

A secondary, process-level issue: **`MEMORY.md`'s Volatile Notes were never
reconciled with the 045 outcome.** The Volatile Notes still say "The
committed Autosave module/plans are explicitly rejected work... Autosave
remains target-only" (MEMORY.md lines 133–135), which contradicts the
045-verified reality that a real, hardware-tested Autosave baseline is
checked in and running. Nothing in `MEMORY.md`'s per-session "Resolved /
Changed" list references Session 045 or 044's actual autosave content at all
— the most recent entry there is Session 044's unrelated cold-boot topic.
Left as-is, this note will mislead the next session into either re-doing
already-verified work or trusting a stale "target-only" status. **This must
be fixed as part of Step 0**, before any code work, or the next session
inherits the same ambiguity that made Phase 2 hard to reason about.

---

## Part 2 — Plan

The plan is deliberately sequential. Each step has one purpose, one owner
boundary, and one hardware checkpoint. Do not start a step until the previous
step's checkpoint has passed on real hardware. This mirrors what worked in
Phase 1/baseline and directly targets root causes (b), (d), and (e) above.

### Step 0 — Reconcile documentation before touching code

Purpose: eliminate ambiguity that caused root cause (e)/(f)-style confusion
about what the "current accepted state" even is.

1. Update `MEMORY.md` Volatile Notes: replace or annotate the stale "Autosave
   remains target-only" bullet (lines 133–135) with an accurate one-line
   status plus a pointer to `045_SESSION_HANDOFF_LOG.md`, in the same style
   as the existing Session 040/042/043/044 pointers.
2. Add a "Resolved / Changed in Session 045" subsection to `MEMORY.md`'s
   Known Issues list summarizing the accepted baseline (A/B record format,
   Phase 1 hooks, settings/provenance, the two AsyncFATFS fixes) so it's
   discoverable the same way every other session's outcome is.
3. Confirm `AUTOSAVE_PARAM_HOOK_failed.md`, `AUTOSAVE_PARAM_HOOK_FOLLOWUP_failed.md`,
   and all eight `*.failed`/`*_failed.*` source files listed in §8 are present,
   read-only in intent, and excluded from the Makefile (verify
   `grep -i failed Makefile` returns nothing — they must never be compiled).
4. Re-read `AUTOSAVE_PARAM_HOOK.md` end-to-end against the *current* source
   (it predates the failed branch and was never corrected against what was
   learned in §6/§7). Mark any section that assumes single, unconditional
   active-Scene ownership, or that treats Bank name/slot as identity, as
   **stale** rather than deleting it — the next step needs to rewrite those
   sections, not discover them mid-implementation.

Checkpoint: no hardware needed. Done when `MEMORY.md` and
`AUTOSAVE_PARAM_HOOK.md` agree with each other and with the 045 handoff about
what exists today.

### Step 1 — Build observability before building features

This directly answers root causes (c) and (f): the reason Phase 2 couldn't be
debugged is that nothing distinguished its stages. Build the instrument
before building the thing it needs to instrument. This is 045's own Topic B,
made mandatory and moved first instead of "recommended."

1. Define a small, fixed set of autosave lifecycle stages to make observable,
   at minimum: dirty-bit-set (producer), transaction-admitted (scheduler),
   winner-validated, mask-merged, parameter-capture-complete, target-published
   (commit). This is the same list already enumerated in the 045 BLOCKERS
   section — use it verbatim as the spec.
2. Implement this as `DEV_MODE_LOGGING`-gated eight-byte-per-event records
   (reusing the existing boot-timeout logger's format/mechanism from §2.4,
   since that pattern — fixed small record, one bounded write, no screen
   interaction — is already hardware-proven), *not* as a new ad hoc mechanism.
3. Hard constraint, carried over from the 045 CRITICAL REMINDERS: this must
   never print to the display, never add filesystem traffic beyond the
   logging write itself, and must not measurably change autosave timing. If a
   logging call would need to block or poll, it doesn't belong here — buffer
   and flush on the existing tick, the same way the writer itself is bounded.
4. Verify the instrumentation is trustworthy before trusting anything it
   reports: run one full Phase 1 debounce/commit cycle from a known clean
   mask, and confirm every stage fires exactly once with correct ordering,
   with **zero** feature code changed. If the trace doesn't match known-good
   Phase 1 behavior, the instrumentation is wrong — fix it before proceeding,
   don't proceed and assume it's right.

Checkpoint (hardware): a normal Phase 1 scalar edit produces a trace showing
all six stages in order, with no extra or missing events, twice in a row.

### Step 2 — Re-run and close out the accepted Phase 1 matrix (045 Topic C)

Purpose: establish a known-good, fully-characterized baseline to diff Phase 2
against. Without this, a Phase 2 regression in Bank/Scene/Kit/Instrument
scalar behavior would be invisible.

1. From known valid, fully-drained starting records (record exact starting
   generation/CRC per the Topic C spec), exercise one coordinate at a time:
   Bank, Scene, Kit, Instrument Normal, Instrument Morph, supplemental
   descriptor, MIDI channel/note, generated Kit endpoints.
2. Explicitly include the edge cases 045 flagged as untested: identical-value
   writes (must not dirty), repeated coalesced writes within one debounce
   window, a re-dirty *during* an active drain (must survive to the next
   generation per §2.7's atomic take semantics), and a clean-mask idle
   observation (must produce zero hidden-file I/O, per the CRITICAL REMINDER
   in the 045 handoff).
3. Record exact generation/CRC/mask-bit-count/payload-offset before and after
   every run, using the Step 1 instrumentation to confirm which stage did the
   work, not just that the output file changed.

Checkpoint (hardware): every item above passes with a trace consistent with
Step 1's expectations. This becomes the new reference baseline — save the
fixtures and traces as the comparison point for every later step.

### Step 3 — Settings/provenance gaps left open by 045 (finish, don't expand)

Purpose: close out the two items 045 explicitly left unresolved in the
*already-shipped* settings feature, before adding Phase 2 surface area on top
of it (§3.2, §6.6, and Topic A).

1. HCNAMES-source propagation after a runtime root Scene or partial Bank Load:
   Session 048 moved provenance out of settings and into the paired 129-row
   HCNAMES register. Future fixtures must test those source transitions there,
   independently of parameter mutation marking.
2. `settings.cfg` post-load rewrite: 045 observed the file retaining stale
   content after Scene Load and did not isolate why (§6.6). Diagnose this
   with Step 1's tracing rather than re-guessing publication points the way
   §6.3 did.
3. AutoSave OFF→ON full lifecycle on real hardware (never proven per
   BLOCKERS): confirm OFF stops new hidden-file setup/validation/drain
   without touching existing records, an in-flight transaction is allowed to
   reach its own close boundary, and ON re-arms cleanly.
4. Reconcile the documented 368,132-byte packaged image against the
   368,012-byte checked-in artifact via a clean rebuild (Topic A) — a small,
   isolated task, useful as a "does the toolchain still produce what the docs
   claim" sanity check before bigger changes.

Checkpoint (hardware): each of the four items above has an independent
pass/fail result with a saved trace and fixture, not a combined "seems fine"
result.

### Step 4 — Identity and completeness contracts (Topic E), written down as rules

Purpose: fix root cause (a) at the design-document level, before it's
reachable by code again.

1. Write an explicit, short rule in `AUTOSAVE_PARAM_HOOK.md` (replacing the
   stale assumptions flagged in Step 0.4): Bank name, Bank slot, active
   Scene, and all other metadata are ordinary payload bytes unless a
   specifically-reviewed format exception says otherwise. A record must never
   be rejected merely because its payload name differs from live display
   identity.
2. Write down, in the same document, what distinguishes a "complete" record
   from a merely "valid" one (§6.6/Topic E asks for this and it was never
   answered) — this determines whether a freshly-created, zero-payload record
   is safe to treat as authoritative before its first drain finishes.
3. Get explicit user sign-off on both rules before Step 5. These are exactly
   the kind of ownership/durability decisions the existing MEMORY.md Volatile
   Note (pre-Step-0 wording) said Autosave work must be "restarted from."

Checkpoint: no hardware needed — this is a documentation/decision gate. Do
not proceed to Step 5 without explicit sign-off; this is what "documented
ownership/durability requirements" means in that Volatile Note.

### Step 5 — Re-implement Phase 2, one object type and one boundary at a time

Purpose: get the actual whole-object hooks in, without repeating root causes
(b), (d), (e). This is Topic D, executed instead of just reviewed, plus the
explicit "Use only with careful, staged testing" guidance from §7.

Ordering rationale: implement in order of *fewest dependent owners first*, so
each step's blast radius is small and diagnosable with Step 1's tracing
before the next step adds complexity.

1. **Whole-Instrument load — complete in Session 048.** The root-pool commit
   in `presetManager.c` marks type plus owned Normal/Morphable Morph bytes
   immediately after the retained assignment; the temporary `kit` path passes
   a disabled mark decision. Hardware accepted `J=0x03`, `I=0x07`, 76/76
   accepted bytes, and a successful generation-2 writer transaction.
2. **InstrumentMrp projection — complete in Session 048.** A compatible
   staged source copies only Morphable normal values to the destination Morph
   image, then marks precisely those Morph payload cells. Generation-3 A/B
   comparison confirmed a Drum-2 Morph-only change, with type, Normal, and
   name allocation unchanged. This does not claim KitMrp publication.
3. **Whole-Kit load.** Depends on the completed Instrument boundary (Kit contains
   Instruments). Same single-boundary discipline: successful public Kit load
   completion only.
4. **Whole-Scene load, without Pattern** (Pattern stays explicitly excluded,
   consistent with the existing "future Scene with Pattern" stub in §2.8 and
   the Pattern-redesign note in MEMORY.md). Before touching this, re-verify
   the Menu-selection-vs-committed-load distinction that broke §6.3's test:
   add an explicit hardware check that Menu preview/selection state alone
   never produces a dirty mark — only a publicly completed load does. This
   was the specific bug (§6.3, "Menu reset logic overwrote the accepted
   selection") — write a regression test for exactly that scenario before
   calling this step done.
5. **Selective Bank Load**, using the *actual* selected/loaded child mask,
   not "all sixteen Scenes" (§6.1's explicit error, and the explicit
   prohibition in §7: "Do not mark all sixteen Scenes for a partial Bank
   Load"). Bank Save reads resident data and is not a Session-049 mutation
   producer. Do not add Save-side marking in this boundary.
6. **KitMrp or other remaining Morph projection.** This remains separate from
   the accepted InstrumentMrp scope and needs its own endpoint-domain review.

For every remaining Step-5 boundary: change exactly one owner boundary per hardware
test pass (the direct fix for root cause (b)); do not move the publication
point more than once without first writing down, in the session log, why the
previous placement didn't fire, using Step 1's trace to show *which stage*
failed (the direct fix for root cause (d)); do not reassign ownership of
active-Scene, Bank masks, or any other cross-cutting state as part of the
same pass that adds a new hook (the direct fix for root cause (e)) — if
ownership genuinely needs to change, that is its own isolated step with its
own hardware checkpoint, done before or after, never inside, a Phase 2
boundary step.

Checkpoint per sub-step: Step 1 trace shows the dirty mark firing exactly at
the intended public-completion boundary, never at staged/preview time; the
Step 2 baseline matrix still passes unchanged (no Phase 1 regression);
result saved as a fixture+trace pair the same way Step 2 was.

### Step 6 — Load/Save exclusion, reattempted last and in isolation

Purpose: this was the terminal regression in 045 (§6.5) and touches the
writer's admission path, which every other step depends on. Do it last, once
Steps 1–5 give you the tracing and stable baseline needed to see what breaks.

1. Before writing any suspension/exclusion logic, use Step 1 tracing to
   capture the *current* (post-Step-5) admission path for a Phase 1 edit made
   while Load/Save is open, and confirm today's actual behavior (does a
   writer start? does it complete? is anything currently broken here?)
   Do not assume the pre-045 behavior is unchanged after Step 5.
2. Add the suspension flag as a single isolated change: prevent a *new*
   writer start while Load/Save is active. Nothing else. Hardware test:
   confirm existing dirty bits already present before Load/Save opens still
   drain normally afterward (this exact case is what broke in §6.5 — "did not
   drain mutation bits already present in the input records" — make it the
   first thing you check, not the last).
3. Only after step 6.2 passes cleanly, add deferral of physical Load/Save
   entry while a transaction is active, as its own separate change with its
   own hardware test.
4. Explicitly distinguish, and test separately, "prevent a new start" from
   "handle an operation already active before page entry" — 045 names this
   exact distinction as unresolved (§7, "Use only with careful, staged
   testing").

Checkpoint (hardware): dirty bits present before Load/Save opens still drain
after Load/Save closes; a Load/Save session started while a transaction is
mid-flight behaves per the rule decided in 6.3/6.4, with a trace proving
which path was taken.

### Step 7 — Close-out

1. Update `MEMORY.md` Volatile Notes and Known-Issues list with the Phase 2
   outcome, the same way Step 0 asked you to do for Session 045.
2. Write a session handoff following `SESSION_HANDOFF_TEMPLATE.md`, using the
   045 handoff's own end-of-session evidence checklist (§9) item-by-item —
   it was written specifically so the *next* session wouldn't have to
   reconstruct this same context from scratch.
3. Retire or clearly re-mark the `*.failed` files' status once their lessons
   are fully absorbed into the new implementation and this plan — but only
   after Step 6 passes, not before.

---

## Summary: what changes about *how* this work is done

The technical content of Phase 2 (whole-object hooks, Load/Save exclusion) is
not new — `AUTOSAVE_PARAM_HOOK.md` already scoped most of it. What failed was
process: too many simultaneous variables per hardware test, no way to observe
intermediate state, and a couple of semantic errors (identity-vs-payload,
"all sixteen Scenes") that went uncorrected because nothing caught them early.
Steps 0–1 fix the process gap first; Steps 2–4 rebuild a trustworthy baseline
and settle the semantic questions on paper; Steps 5–6 re-implement the actual
features one boundary at a time against that baseline, using the
instrumentation to prove each step before moving to the next.

---

## Implementation note — boot-time A/B baseline creation selector (2026-08-10)

Hardware fixture review after deleting both root AutoSave records and booting
found a deterministic setup defect: `/.hcprms2` was created as a complete
34,768-byte committed generation-1 record, but `/.hcprms1` was absent. The
eight-byte trace contained only the scheduled (`S`) event, which is consistent
with setup returning successfully and the writer being armed, not with a
filesystem write error.

Cause: the boot ensure operation ordinarily uses `op_stream_index` as the
zero-based A/B selector. Before creating a missing record it saves that
selector in `op_file_version`, then reuses `op_stream_index` as the bounded
CRC32C accumulator. The subsequent open incorrectly called
`filesystem_autosaveTargetName()`, which interprets its now-nonzero CRC value
as the B selector. A missing A therefore created B; the next root scan found
that B and reported the pair ensured without ever creating A.

Implemented correction: phase 10 opens
`filesystem_autosaveFilenameForIndex(op_file_version)` instead. The saved
selector remains stable through the bounded CRC pass, so each missing member
of the A/B pair receives its intended filename. No interface or header change
is needed: the index-to-filename helper already exists in `filesystem.c`, and
the fix only corrects the internal caller that had selected the wrong source
of truth.

Required hardware retest: remove both `/.hcprms1` and `/.hcprms2` plus the
trace, boot with AutoSave enabled and a valid resident Bank, then inspect the
card. Both files must be present at exactly 34,768 bytes with committed
headers. Leave the unit powered long enough for the normal five-second writer
deadline only if testing recovery/idle behavior too; pair creation itself is
boot-time work and does not require waiting for that deadline.

---

## Session 047 progress and remedy-document disposition (2026-08-10)

This section is the deletion-safe disposition of `AUTOSAVE_REMEDY_PA2ST1.md`
and `AUTOSAVE_REMEDY_PA2ST2-3.md`. Those planning files may be removed after
this closeout; their remaining decisions and evidence are preserved here and
in `047_SESSION_HANDOFF_LOG.md`.

### Completed from PA2ST1 — Step 1 observability

- The `DEV_MODE_LOGGING` lifecycle trace is complete: `AutosaveTrace` keeps a
  bounded 64-record / 512-byte RAM ring and records `D/S/A/V/M/C/P/T` at the
  scalar dirty, schedule, admission, validation, mask-merge, capture,
  publication, and terminal boundaries. `filesystem.c` alone owns its
  lower-priority `asavetrc.bin` append/close/sync and acknowledges records only
  after durable completion.
- The first hardware fixture exposed a trace-writer scheduler bug rather than
  a dirty-hook defect: a `NULL` append callback left the shared filesystem
  facade terminal, preventing later background work. The implemented private
  completion callback returns `DONE` or `ERROR` to `IDLE`; failed appends keep
  the undurable ring records. The bench-only blocking helper observes and
  acknowledges its own terminal state separately.
- Session 047 additionally held optional trace-file admission while a Load or
  Save page owns the facade. Repeated playback-time `000 Full` / `013 LoadTst`
  loads and saves over 024/009 produced no audible glitch. This is a narrow
  trace-arbitration result, not the rejected generic pacing experiment and not
  a general Load/Save-exclusion implementation.
- The Step-1 trace is usable evidence for later work. No broader trace ring,
  screen logging, scheduler delay, or proposed Step-2/3 snapshot structure is
  authorized.

### Completed and remaining from PA2ST2-3 — Steps 2 and 3

- **Step 2 scalar owner coverage is accepted complete.** The source audit and
  hardware work cover Scene values, Kit/generated endpoints, Instrument Normal
  and Morph descriptors (including supplemental descriptors), and Scene-owned
  MIDI channel/note. Equal-value guards, debounce coalescing, and atomic
  take/re-dirty behavior remain properties to protect when changing an owner,
  not a reason to reopen an unbounded matrix. There is no separately editable
  Bank scalar UI control. Whole-object publication, Pattern/Effect state, and
  power-cut behavior were explicitly outside that acceptance.
- **Step 3 settings result:** the existing revision-protected, one-second
  `settings.cfg` writer was user re-tested in Session 047 and passed. No
  settings implementation change was made or is pending. The old failed-branch
  report of a stale post-load settings file must not be used as a reason to
  move current callbacks speculatively.
- **Step 3 still needs focused hardware evidence** if/when AutoSave work is
  resumed: root Scene Load, root Scene Save, partial Bank Load, and partial
  Bank Save provenance each need independent `settings.cfg`/source-row
  fixtures; AutoSave OFF→ON needs idle and in-flight checks. Use the existing
  trace plus copied record/settings fixtures. Do not implement the superseded
  16-byte combined diagnostic or a new C/H evidence API merely to run them.
- The former proposed read-only AutoSave record inspector remains optional
  future host tooling. The new `tools/decode_bootlog.py` only decodes the
  boot-failure capsule; it is not a replacement for a full A/B record/trace
  inspector. A general human-readable log converter is deferred until all
  AutoSave/log formats are final.

### Session 047 bounded-CRC and boot-capture boundary

- All four CRC consumers—initial record creation, no-valid-record recovery,
  candidate validation, and transformed copy—now share the 128-byte-per-tick
  retained-cursor limit. This replaces the remaining foreground whole-record
  CRC; it adds no delay, record-sized staging buffer, or permanent CRC store.
- The selector correction above was found during the missing-pair test. It
  prevents the CRC accumulator from selecting B when creation meant A.
- One subsequent initial-B creation stopped at exactly 32,768 bytes and hit the
  real ten-second `ASENSURE` boot watchdog. The capture build freezes a
  64-byte E0..E7 capsule containing application progress, AsyncFATFS
  allocator/cache/file state, and SD transport state before normal recovery.
  It is emitted only after the ordinary eight-byte boot token, never performs
  diagnostic I/O while the failed transaction owns the filesystem, and does
  not alter retry/allocator/recovery policy. Both logging configurations built;
  later boots produced two complete 34,768-byte records and no bootlog.
- Therefore the 32 KiB lower-layer failure is an intermittent observation, not
  a diagnosed CRC fault or a resolved defect. If it returns, preserve the SD
  card and decode the 72-byte `ASENSURE` bootlog before attempting an
  AsyncFATFS or SD-driver change.

### Session 048 acceptance and Session 049 gate

Session 048 completed the first two low-coupling Step-5 boundaries without
changing record geometry, adding production RAM, or broadening writer
admission: normal root Instrument and InstrumentMrp. The direct HCNAMES flush
acknowledgement and the approved one-byte Menu exit queue were necessary to
allow the existing deferred schedulers to start after the normal physical page
exit; they are part of the verified boundary, not a general Load/Save
exclusion redesign.

Session 049 begins with normal Kit Load, root Scene Load without Pattern, then
selective Bank Load. Keep each in its own implementation and hardware fixture.
The active-playing-Scene switch and stale overwrite folder remain independent
known bugs; the latter requires native recursive-delete correctness, while the
former requires a separate runtime-vs-boot selection contract. Reattempt
Load/Save exclusion last and in isolation.
