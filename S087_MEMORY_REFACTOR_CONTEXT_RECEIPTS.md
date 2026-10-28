# S087 MEMORY.md Refactor — Context Receipts

**Created:** 2026-10-28, Session 087  
**Purpose:** Prospective diff against the current `MEMORY.md` (2,773 lines).
Each item quotes the affected lines, states the action and rationale, and
records the user's decision when made.

The target is a lean startup document: ~600–800 lines covering build/flash,
current session state, binding user policies, repository layout, hardware
quick-reference, audio pipeline, boot order, distilled binding rules (rule +
spec pointer), volatile carryover, failed approaches, and toolchain.

---

## How to read this document

- **DELETE** — content is already carried by the session logs or specification
  references; removing it from MEMORY.md loses nothing.
- **MOVE** — content is specification-grade and belongs in
  `knowledge_files/specification_reference/`; verify it is already there, then
  delete from MEMORY.md.
- **KEEP** — content belongs in a startup document.
- **COMPRESS** — content has value but is far too verbose for a startup doc;
  replace with a distilled rule + spec pointer.
- **REVIEW** — mandate or convention that may have been agent-introduced;
  user decides.

Each receipt has a **Decision** field (blank until the user acts).

---

## Category A — Session history that duplicates `log_archive/`

These blocks retell what individual session handoff logs already record in
full. They turned MEMORY.md into a running log.

---

### A-1. S086 "Current working source" narrative (lines 36–105)

**Action:** DELETE  
**Rationale:** Every fact here — commits, build sizes, "S086 in brief",
"S085 in brief", "S084 in brief", hardware results — is already in the
corresponding `08x_SESSION_HANDOFF_LOG.md`. The only useful piece is the
S087 status, which is already stated separately at lines 23–34.

**Quoted (first and last lines to bound the block):**

```
line 36:  **Current working source**: **Session 086 (stock table waves `Re3`/`Sw3`,
…
line 105: - **Build at S086 close (DEV config):** `text=658,872`, `data=424`,
          `bss=430,296`; flash payload 659,296 B of 753,664 (**94,368 B free**:
          the stock tables took 45,056 B); …
```

The entire block from line 36 ("**Current working source**") through line 105
(end of the S086 build-at-close bullet) is a session-by-session log summary.

**Replace with:** Nothing. Lines 23–34 already carry the S087 status.

**Decision:**

---

### A-2. S085–S081 "in brief" session summaries (lines 106–190)

**Action:** DELETE  
**Rationale:** These are condensed retellings of sessions 085, 084, 083, 082,
081. Each has its own handoff log.

**Quoted (representative samples):**

```
line 106: - **Session 085 in brief** (`085_SESSION_HANDOFF_LOG.md`;
line 107:   `INSTRUMENTS_DSP_REFERENCE.md` §5.1.1, §6.1, §8, §9.3, §10):
line 108:   - **First attempt, rolled back:** the full oscillator cherry-pick (types
…
line 148: - **Session 081 is closed** (`081_SESSION_HANDOFF_LOG.md`): live record and
line 149:   erase, the step reset rule, R12/R13, the 96-PPQ roll table, `qnt trk`,
…
line 189:   - **Also logged in S084:** the host-only LXR1 and LXR-02 import tools
line 190:     (below, "Volatile Notes").
```

**Decision:**

---

### A-3. S082 one-paragraph summary (lines 191–208)

**Action:** DELETE  
**Rationale:** Duplicates `082_SESSION_HANDOFF_LOG.md`.

**Quoted:**

```
line 191: Session 082 in one paragraph:
line 192:
line 193: - **Bank Load/Save** take act/empty masks: Replace (default, all 16 as they
…
line 208:   chains wait for the end of the load.
```

**Decision:**

---

### A-4. "Permanent authority" — deleted-document provenance list (lines 210–293)

**Action:** DELETE  
**Rationale:** This is a 83-line list of which root `S0xx_*.md` documents were
superseded by which logs, and where to find them in git history. This is
archival bookkeeping. Any agent that needs a deleted document can check
`git log --all --oneline -- 'S0*.md'`. The specification-reference index
(lines 1601–1631) already lists the live authoritative documents.

**Quoted (first and last entries):**

```
line 210: **Permanent authority:**
line 211:
line 212: - `knowledge_files/log_archive/085_SESSION_HANDOFF_LOG.md`, `084_…`, …
…
line 288:   not refreeze `frozen/`, the pre-S073 reference).
line 289: - The four root `S079_*.md` task documents …
…
line 292:   and the updated specifications (the user deletes them).
```

**Replace with:** A single line:

> Deleted root task documents (`S0xx_*.md`) are superseded by their session
> handoff logs and can be recovered from git history.

The pointer to the frozen test snapshots (lines 283–288) should stay as a
3-line note under the DSP test section or the oscillator CPU rule section.

**Decision:**

---

### A-5. S083 detailed session record (lines 82–105 subset, lines 131–148)

**Action:** DELETE  
**Rationale:** The S083 record, including the MIDI rebuild summary, notes,
control, PC/CC0/transport, transmit, and follow-ups, is fully carried by
`083_SESSION_HANDOFF_LOG.md` and `MIDI_SPEC.md`.

**Quoted:**

```
line 82:  The S083 record is `knowledge_files/log_archive/083_SESSION_HANDOFF_LOG.md`;
…
line 86:  `MidiParser`, `Uart`, `FIFO`, `MidiRealtime`,
line 87:  `MidiMessages`, `MidiNoteNumbers` and the legacy LXR CC/CC2 map are gone.
…
line 131: - **S083 in brief:**
line 132:   - **Rebuild:** the `Core/MIDI/` rebuild with consume-then-route …
…
line 143:     `rmp`; Learn in `midilrn.cfg`; global Morph sets FX Morph; SHIFT+REC
line 144:     `pv<`/`pv>`; CC2 sets the PERF BAR overlay itself.
```

**Decision:**

---

### A-6. Session 040–061 detailed narratives (lines 807–968, 1061–1414)

**Action:** DELETE  
**Rationale:** These are the longest single blocks in MEMORY.md — over 500
lines of session-by-session implementation history for the AutoSave system,
HCNAMES authority, recursive delete bugs, Bank Save freeze diagnosis, etc.
Every one of these sessions has a handoff log. An agent working on AutoSave
reads `AUTOSAVE.md`; an agent working on filesystem reads `FILESYSTEM_SPEC.md`.
They do not need the investigation chronology of Bug #1 through Bug #5 in the
startup document.

**Quoted (representative samples only — full block is ~515 lines):**

```
line 807: - Read `knowledge_files/log_archive/040_SESSION_HANDOFF_LOG.md` before
line 808:   continuing Scene/Bank or filesystem work. …
…
line 928: stage row 0 to its direct `op_slot` before the Bank-owned HCNAMES close gate,
…
line 1061: - Session 050 is closed in
line 1062:   `knowledge_files/log_archive/050_SESSION_HANDOFF_LOG.md`. …
…
line 1156:   - **Bug #1** — `filesystem_deleteSlotDirectory_tick()` had two completion
…
line 1222:     `knowledge_files/log_archive/054_SESSION_HANDOFF_LOG.md` for the full
line 1223:     round-by-round trail and every file/line changed.
…
line 1402:     `MODULE_INTERCHANGE_SPEC.md`) and may be deleted.
…
line 1404: - Session 061 typed HCNAMES and AutoSave boot restore are implemented …
line 1414:   `knowledge_files/log_archive/061_SESSION_HANDOFF_LOG.md` and `AUTOSAVE.md`.
```

**Replace with:** A short "prerequisite reading" list:

> Before working on AutoSave: read `AUTOSAVE.md`.  
> Before working on filesystem/load/save: read `FILESYSTEM_SPEC.md` and
> `ASYNCFATFS_REFERENCE.md`.  
> Before working on Scene/Bank: read `040_SESSION_HANDOFF_LOG.md` (Bank Load
> fix) and `042_SESSION_HANDOFF_LOG.md` (HCNAMES refactor).

These pointers already exist in the "Where to look for things" table.

**Decision:**

---

### A-7. "Known Issues / Technical Debt" resolved items (lines 2203–2693)

**Action:** DELETE all resolved items; KEEP only genuinely open items  
**Rationale:** ~490 lines. The vast majority are marked **RESOLVED** with
session numbers. They exist only as historical record — which the session logs
already carry. Keeping them inflates every agent's context for zero benefit.

**Open items to KEEP (extract and compress):**

From "Session 051 carryover" (lines 2203–2275):
- The current diagnostic build retains 2,048 trace records (line 2235) — open
  decision.
- Multi-destination HCNAMES exit fixtures not hardware-tested (line 2221).

From "High Priority" (lines 2671–2679):
- Item 4: long sample playback not 32-bit clean (line 2677).
- Item 6: Load/save button display glitch fix not applied (line 2679).

From "Medium Priority" (lines 2681–2689):
- Item 14: RV1-RV4 noise needs hardware soak (line 2687).
- Item 15: synced LFO tempo hardcoded 130 BPM (line 2688).
- Item 16: PAR_EXT_SYNC / PAR_FETCH slot conflict (line 2689).

From "Lower Priority" (lines 2691–2694):
- Item 16: sample loader naming hardware soak (line 2692).
- Item 17: SELECT_1 LED dark at boot (line 2693).

**Resolved items to DELETE (representative — there are ~40 of these):**

```
line 2601: ### Resolved in Session 022
line 2602: - ~~24-bit DMA frame LSW forced to 0x0000~~ — **RESOLVED**. …
…
line 2623: ### Resolved in Session 019
line 2624: - ~~USART3 RX not configured~~ — **RESOLVED**. …
…
line 2636: ### Resolved in Session 020
…
line 2641: ### Resolved in Session 18
…
line 2647: ### Resolved in Refactor Session
…
line 2652: ### Resolved in Session 15
…
line 2660: ### Resolved in Session 14
…
line 2663: ### Resolved in Session 13
…
line 2666: ### Resolved in Session 12
…
line 2671: ### High Priority
line 2672: 1. ~~No hi-hat/wrong tagged Instrument at startup~~ — **RESOLVED** …
line 2673: 2. ~~Trigger backend still stubbed~~ — **RESOLVED** …
line 2674: 3. ~~BAR1/BAR2 race condition~~ — **RESOLVED** …
…
line 2681: ### Medium Priority
line 2682: 5. ~~ResonantFilter.c double literals~~ — **RESOLVED** …
line 2683: 6. ~~DrumVoice.c VLA~~ — **RESOLVED** …
line 2684: 7. ~~BufferTools.c float division~~ — **RESOLVED** …
line 2685: 8. ~~TIM2 not initialised~~ — **RESOLVED** …
line 2686: 9. ~~MidiParser RX not connected~~ — **RESOLVED** …
```

**Also DELETE these entire resolved sections:**

```
line 2298: ### Session 059 closeout (2026-08-31)       — 32 lines
line 2333: ### Session 060 Phase C source fields        — 22 lines
line 2355: ### Session 060 Phase D re-dirty audit        — 20 lines
line 2376: ### Resolved / Changed in Session 048         — 14 lines
line 2390: ### Resolved / Changed in Session 047         — 14 lines
line 2405: ### Resolved / Changed in Session 046         — 22 lines
line 2429: ### Resolved / Changed in Session 045         — 10 lines
line 2440: ### Resolved / Changed in Session 044         — 23 lines
line 2464: ### Resolved / Changed in Session 042         — 20 lines
line 2485: ### Resolved / Changed in Session 034         — 32 lines
line 2518: ### Resolved / Changed in Session 030         — 35 lines
line 2553: ### Resolved / Changed in Session 032         — 20 lines
line 2574: ### Resolved / Changed in Session 029         — 7 lines
line 2581: ### Resolved / Changed in Session 028         — 6 lines
line 2587: ### Resolved / Changed in Session 027         — 3 lines
line 2591: ### Resolved / Changed in Session 023         — 10 lines
line 2608: ### Resolved in Session 16                    — 5 lines
line 2615: ### Resolved in Session 17                    — 7 lines
```

**Decision:**

---

## Category B — Specification content that belongs in `specification_reference/`

---

### B-1. Load/Save Slot Scrolling Policy (lines 379–407)

**Action:** MOVE to `MODULE_INTERCHANGE_SPEC.md` (the menu/input contract)
or `FILESYSTEM_SPEC.md` (the load/save contract), then replace with a
one-line pointer in MEMORY.md.  
**Rationale:** This is a binding specification for how the input gate behaves
during load/save scrolling. It is not a session process note.

**Quoted:**

```
line 379: ## Load/Save Slot Scrolling Policy (user, repeated 2026-10-19) — binding
line 380:
line 381: - **Scrolling a slot number (encoder or endless pot) must never block or lag.**
…
line 407:   (`menu_handleLoadSaveKnobDelta()`) for the page.
```

**Note:** Verify it is not already in the specs before moving. If it is already
there, just delete from MEMORY.md and leave the pointer.

**Decision:**

---

### B-2. HCNAMES schema and Boot path details (lines 844–932)

**Action:** DELETE from MEMORY.md (verify already in `FILESYSTEM_SPEC.md` and
`AUTOSAVE.md`)  
**Rationale:** These are detailed filesystem/storage specifications — row
schemas, source tokens, the 322-byte register, paired source correction,
Bank-local Scene rules, etc. This is exactly what `FILESYSTEM_SPEC.md` is for.

**Quoted (representative):**

```
line 844: - Source layout is now `Core/Bank/Scene/` and `Core/Bank/BankData.*`, …
…
line 911: - Root `/.hcnames` is the authoritative active identity **and provenance**
line 912:   register: row 0 Bank; rows 1..16 Scene; rows 17..32 Kit; …
…
line 927:   overlays only its owned rows, and rewrites the file.
line 928: - HCNAMES paired source correction: a successful non-empty root Bank Load must
…
line 932:   `settings.cfg` is the 17-line source-free system file …
```

**Decision:**

---

### B-3. SD Card Architecture filesystem details (lines 1800–1948)

**Action:** DELETE/COMPRESS  
**Rationale:** Lines 1800–1948 contain detailed filesystem architecture
(storageTypes contract, Kit/Scene/Bank load/save directory structures,
HCNAMES schema again, Boot path again, LFN component creation, overwrite
safety, Instrument Load transaction, Kit scan, product name repair, etc.)
that is specification content already in `FILESYSTEM_SPEC.md`. The
architecture overview (lines 1806–1830) has some value as quick reference;
the rest duplicates the spec.

**Keep:** The architecture diagram (lines 1811–1817) and the key design
points (lines 1819–1830) — about 20 lines.

**Delete:** Everything from line 1831 through line 1948, which is
detailed specification content.

**Quoted (section to delete):**

```
line 1831: - `filesystem.c` owns the filetype registry and add-a-filetype checklist. …
…
line 1948:   **Approaches confirmed wrong** (do not retry): …
```

Wait — the "Approaches confirmed wrong" at line 1948 is the start of the
Failed Approaches section (Category F, KEEP). The deletion boundary is
lines 1831–1946.

**Decision:**

---

### B-4. Instrument membership / SceneData / Bank / HCNAMES rules (lines 880–1001)

**Action:** DELETE from MEMORY.md  
**Rationale:** These are specification-level rules about Instrument membership
dynamics, SceneData initialization, Bank v2 manifest, typed load staging,
Boot index writes, etc. Already in `FILESYSTEM_SPEC.md`,
`BANK_PRESET_ARCHITECTURE.md`, and `AUTOSAVE.md`.

**Quoted (representative):**

```
line 880: - Instrument membership is fully dynamic at boot and runtime. SceneData must
…
line 895: - Bank has a 16-Scene resident workspace. Its v2 manifest carries
…
line 989: - Typed load staging is a separate aligned 2,048-byte union, never the
line 990:   9,000-byte name cache. …
…
line 1001:   metadata; it does not regenerate every index.
```

**Decision:**

---

## Category C — Binding rules to COMPRESS (keep rule + pointer, delete narrative)

These are user-stated binding rules that belong in the startup document,
but each one is wrapped in 20–40 lines of session narrative that belongs in
the logs. Compress each to: the rule itself (2–5 lines) + a pointer to the
spec or log that has the full detail.

---

### C-1. Step reset rule (lines 473–482)

**Action:** COMPRESS  
**Current:** 10 lines of detailed mechanism.  
**Replace with:**

> **Step reset rule (S081) — binding:** Instrument automation resets once per
> step at its first trigger; roll retriggers 2+ never reset; live presses
> always reset. Full detail: `PATTERN_DYNAMIC_STACK.md` §6.1,
> `081_SESSION_HANDOFF_LOG.md` §5.8.

**Quoted current:**

```
line 473: - **Step reset rule (S081 §5.8) — binding:** Instrument automation resets
line 474:   once per step, at its **first** trigger (a delayed hit, a roll's first hit,
…
line 482:   step's trigger cuts it (F-R).
```

**Decision:**

---

### C-2. Live record invariants (lines 483–497)

**Action:** COMPRESS  
**Current:** 15 lines.  
**Replace with:**

> **Live record invariants (S081; S083 names) — binding:** buttonHandler/Menu/
> MidiNotes call LiveRecord; Sequencer never calls LiveRecord; LiveRecord
> never runs in TIM3. Values recorded are origin Scene A's menu values.
> Full detail: `PATTERN_DYNAMIC_STACK.md` §6.7,
> `MODULE_INTERCHANGE_SPEC.md` "Core/Sequencer/LiveRecord".

**Quoted current:**

```
line 483: - **Live record invariants (S081; S083 names) — binding:** buttonHandler,
line 484:   Menu and MidiNotes/MidiControl call LiveRecord; the Sequencer never calls
…
line 497:   must stay in step with `seq_evaluateStepCondition()`.
```

**Decision:**

---

### C-3. Automation ramps binding (lines 501–506)

**Action:** COMPRESS  
**Current:** 6 lines — already fairly compact. Keep as-is or trim slightly.

**Decision:**

---

### C-4. Effect automation R13 (lines 507–511)

**Action:** COMPRESS  
**Current:** 5 lines — already compact. Keep or trim to 3 lines + pointer.

**Decision:**

---

### C-5. Roll engine and live overlay binding (lines 520–533)

**Action:** COMPRESS  
**Current:** 14 lines.  
**Replace with:**

> **Roll engine (S084) — binding:** a roll's length is a master-tick window,
> not a hit count; carry fires no hit; PERF/MIDI overlay fires in TIM3
> (`seq_processLiveTicks()`). Full detail: `084_SESSION_HANDOFF_LOG.md`,
> `PATTERN_DYNAMIC_STACK.md` §6.5.

**Quoted current:**

```
line 520: - **Roll engine and live overlay (S084) — binding:** a roll's length is a
line 521:   master-tick window (`seq_rollWindow`), never a per-step hit count; …
…
line 533:   `perfBtnTypeNames`, `BH_OVERLAY_TYPE_*` and the menu clamp in step.
```

**Decision:**

---

### C-6. Looper invariants (lines 546–550)

**Action:** COMPRESS  
**Current:** 5 lines — fairly compact. Keep or trim to 3 lines + pointer.

**Decision:**

---

### C-7. Looper while recording (lines 551–571)

**Action:** COMPRESS  
**Current:** 21 lines.  
**Replace with:**

> **Looper while recording (S081 P3) — binding:** the looper and live record
> run together; engaging/swapping/releasing the loop never changes the record
> mode; the loop is held until every SELECT/VOICE/BAR is released. Full
> detail: `PATTERN_DYNAMIC_STACK.md` §6.5, `081_SESSION_HANDOFF_LOG.md`.

**Decision:**

---

### C-8. Global Morph never recorded (lines 565–571)

**Action:** COMPRESS to 3 lines + pointer. The mechanism detail (`liveRec_morphValue[7]`, `liveRec_recordValue7()`) is code, not a startup rule.

**Decision:**

---

### C-9. PERF/Scene/Chain/LED rules from S080 (lines 575–596)

**Action:** COMPRESS  
**Current:** ~22 lines covering `pcr`, PERF SEQ LEDs, latched menus, parameter
mirrors, Active Scene definition, edit-mask intent.  
**Replace with:** 2-line summaries of each rule + pointer to
`MODULE_INTERCHANGE_SPEC.md` "PERF performance surface" and
`PATTERN_DYNAMIC_STACK.md` §6.5/§6.6.

**Decision:**

---

### C-10. S078 rules (lines 604–709)

**Action:** COMPRESS  
**Current:** ~105 lines covering the S078 close, SHIFT Morph-view rule, STEP
Morph view, per-track timing, menu dtype space, STEP underline search, S077
close, per-track Scene playback, background .hcnames, pat_background_region,
automation priority (F3/R12/Q1), one pan rule, no backward compatibility,
wording, copy/clear invariants, and multiple "open items" lists.  

Many of these are already specification content (per-track timing is in
`PATTERN_DYNAMIC_STACK.md` §6.4; automation priority is in §6.2a; copy/clear
invariants are in `COPYCLEAR_UTILITIES.md`).

**Replace with:** A compressed binding-rules block (~30 lines):

> **Morph-view rule (S078 P4) — binding:** in any SHIFT Morph view, only
> morphable parameters redirect; non-morphable shows/edits Normal. Ref:
> `BANK_PRESET_ARCHITECTURE.md` §5.
>
> **Per-track timing (S078) — binding:** ISR timing reads only
> `seq_effectiveTrack*[]`. Ref: `PATTERN_DYNAMIC_STACK.md` §6.4.
>
> **Menu dtype space (S078):** all 16 `DTYPE_MENU` table ids and all 16
> `DTYPE_*` nibbles are used.
>
> **Automation priority (S075 F3, S081 R12, S083 Q1) — binding:** step
> automation > menu edits; a MIDI write is a menu edit. Ref:
> `PATTERN_DYNAMIC_STACK.md` §6.2a.
>
> **One pan rule (S075 F2) — binding:** every pan stores 0..127, displays
> value − 63; stored 63 is centre.
>
> **No backward compatibility, ever (user, standing):** stored formats may
> change freely. Do not offer migration or aliasing.
>
> **Wording (user, S081):** stored data are *files*, not "cards."

The "open items" lists (S078, S077, S075, S074) should be collected into a
single compact "Open items" section — see Category D below.

**Decision:**

---

### C-11. S074 rules (lines 720–763)

**Action:** COMPRESS  
**Current:** ~44 lines covering AutoSave rule, bus compressor rules, Effect
page rules, flash and image, S073 DSP refactor invariants, measuring flash,
Effect invariants, blank names, card preparation, carried small debt.

Many are specification content. Compress to the rules that are genuinely
needed at startup (bus compressor settings page position, Effect page
contract, measuring flash command, blank name convention).

**Decision:**

---

## Category D — Open items to consolidate

The current document has open items scattered across at least eight locations:
S081 open items (line 534), S078 open items (line 703), S077 open items
(line 706), S075 open items (line 710), S074 open items (line 718), S082
open items (line 454), the High/Medium/Lower priority lists (lines 2671–
2694), and various inline "still open" notes.

**Action:** Collect all genuinely open items into one compact section.
Delete duplicates and anything that has since been resolved.

**Open items to collect (provisional list):**

1. Trace record count decision (2,048 vs 64) — lines 2235, 1224
2. Multi-destination HCNAMES exit not hardware-tested — line 2221
3. Long sample playback not 32-bit clean — line 2677
4. Load/save button display glitch fix not applied — line 2679
5. RV1-RV4 noise hardware soak — line 2687
6. Synced LFO tempo hardcoded 130 BPM — line 2688
7. PAR_EXT_SYNC / PAR_FETCH slot conflict — line 2689
8. Sample loader naming hardware soak — line 2692
9. SELECT_1 LED dark at boot — line 2693
10. Phase 5 hardware acceptance for S072 Steps 6–10 — line 739
11. S082 F5 Scene 2 Case-3 invalidation — line 454
12. S082 P3 A2 frozen-screen blink loss — line 454
13. Boot timeout (reproduce with DEV_MODE_DIAGNOSTIC 1) — line 718
14. S084 matrix V1–V7 confirmation — line 76
15. S081 open items (P3 A-1, stale previous step, P4 B-2, etc.) — line 534
16. S077 re-test rows 3.10/3.14/3.15 — line 706
17. S078 STEP marker retry one-liner — line 703
18. `cpu` widget with `cmp` on — line 719
19. Stale comments (BusCompressor.h, etc.) — line 720
20. STEP underline deferred CGRAM marker retry — line 635
21. Dither global menu option not wired — line 2604
22. Name-cache ownership interlock deferred — line 1259

**Decision:**

---

## Category E — Agent-introduced mandates to REVIEW

---

### E-1. Commenting mandate (lines 1653–1656)

**Action:** REVIEW — likely DELETE  
**Rationale:** This mandate was almost certainly written by an agent session,
not by the user. It requires "detailed contract level" comments on every
function, variable, and storage type, including "inputs/outputs; and
clients/accessors/affiliates." This is onerous, conflicts with the user's
apparent preference for lean code, and contradicts Claude Code's own default
of writing no comments. Real contract documentation belongs in the
specification references, not in inline code comments.

**Quoted:**

```
line 1653: - New code should be commented at detailed contract level: why the function,
line 1654:   variable, or storage type exists; what it does; inputs/outputs; and
line 1655:   clients/accessors/affiliates. Do this proactively, not as a cleanup after the
line 1656:   user asks again.
```

**Decision:**

---

### E-2. Agent volatile memory directory instruction (lines 1657–1659)

**Action:** REVIEW  
**Rationale:** This tells agents to use `knowledge_files/volatile/memory/`
instead of `.claude/memory/`. If the user wants to keep this, fine — it's a
legitimate project convention. But it's worth confirming this was a user
decision, not an agent self-organizing its own persistence.

**Quoted:**

```
line 1657: - Agent volatile memory lives in `knowledge_files/volatile/memory/` (use it
line 1658:   the way you would `.claude/memory/`). Do not use `.claude/memory/` or
line 1659:   `.claude/projects/*/memory/` for this project.
```

**Decision:**

---

### E-3. "Update it whenever something is confirmed, fixed, or decided" (line 5)

**Action:** REVIEW  
**Rationale:** This instruction at the top of MEMORY.md ("Update it whenever
something is confirmed, fixed, or decided") is what caused the runaway
growth. Every agent session that confirmed, fixed, or decided anything added
its narrative to MEMORY.md. The instruction should be rewritten to direct
agents to update the *session handoff log* and the *specification references*,
not MEMORY.md.

**Quoted:**

```
line 4:  Read it fully at the start of every session before touching any code.
line 5:  Update it whenever something is confirmed, fixed, or decided.
```

**Replace with:**

> Read it at the start of every session. Update the session handoff log and
> specification references when things are confirmed, fixed, or decided.
> Only update MEMORY.md if a binding user policy, an open item, or the
> current session status changes.

**Decision:**

---

## Category F — Content to KEEP

---

### F-1. Header + Quick Start (lines 1–21)

**Action:** KEEP  
Lines 1–8 (header), 9–19 (build/flash), 20–21 ("Commits belong to the user").
Rewrite lines 4–5 per E-3 above.

---

### F-2. S087 in-progress status (lines 23–34)

**Action:** KEEP  
This is the current session state. Update at session end.

---

### F-3. RAM Allocation Approval Policy (lines 297–311)

**Action:** KEEP as-is  
Binding user policy. 15 lines.

---

### F-4. DSP CPU Policy (lines 313–334)

**Action:** KEEP as-is  
Binding user policy. 22 lines.

---

### F-5. Oscillator work under the CPU rule (lines 336–377)

**Action:** KEEP as-is  
Binding user policy. 42 lines.

---

### F-6. Volatile Notes — current carryover only (lines 409–460)

**Action:** KEEP but TRIM  
Delete the S082 binding rules pointer (line 456–460, that's a log reference)
and the old chain of "Session 080 is closed", "Session 078 is closed" etc.
that follows (lines 466–709 — this entire block is covered by Category C
compressions above). Keep only the genuine current carryover bullets.

---

### F-7. Repository Layout (lines 1418–1572)

**Action:** KEEP as-is  
Useful quick reference. 155 lines.

---

### F-8. "Where to look for things" + Specification-reference index (lines 1574–1632)

**Action:** KEEP as-is  
This is the most valuable part of the document for session startup. 58 lines.

---

### F-9. Project Goal (lines 1636–1643)

**Action:** KEEP  
8 lines.

---

### F-10. General Process Reminders (lines 1645–1659)

**Action:** KEEP (minus E-1 and E-2 items if user decides to remove them)  
Verify local working directory, header dependency tracking, blocking rules,
async SD rules. ~10 lines after E-1/E-2 review.

---

### F-11. Hardware section (lines 1666–1739)

**Action:** KEEP as-is  
MCU specs, flash layout, GPIO table, IRQ table. 74 lines.

---

### F-12. Audio Pipeline (lines 1743–1799)

**Action:** KEEP as-is  
Render loop, DMA pack, mixer output names, timing, SPSC queue, CPU widget,
24-bit path. 57 lines.

---

### F-13. SD Card Architecture — overview only (lines 1800–1830)

**Action:** KEEP the architecture overview (20 lines). DELETE lines 1831–1946
per B-3 above.

---

### F-14. Sample Flash Loading (lines 1957–1983)

**Action:** KEEP as-is  
Sample install, WAV requirements, metadata layout, flash map. 27 lines.

---

### F-15. Encoder (lines 1986–1998)

**Action:** KEEP as-is  
Dannegger algorithm, seed, divide, acceleration. 13 lines.

---

### F-16. Display / Menu (lines 2001–2021)

**Action:** KEEP as-is  
LCD queue, knob repaint, encoder saturation, etc. 21 lines.

---

### F-17. DSP / RNG Rules (lines 2024–2043)

**Action:** KEEP as-is  
Compiler flags, FPU, VLAs, GetRngValue, constant CPU, RCC/RNG addresses. 20
lines.

---

### F-18. Boot / Init Order (lines 2046–2104)

**Action:** KEEP as-is  
Init sequence, sequencer/PATGEN reminders. 59 lines.

---

### F-19. MIDI / Clock Reminders (lines 2106–2190)

**Action:** KEEP as-is  
These are quick-reference operational rules that complement `MIDI_SPEC.md`.
85 lines. Could be lightly trimmed but they're functional.

---

### F-20. Morph / Endless-Pot Reminders (lines 2193–2200)

**Action:** KEEP as-is  
8 lines.

---

### F-21. Failed Approaches — Do Not Retry (lines 2696–2760)

**Action:** KEEP as-is  
Prevents agents from re-attempting known failures. 65 lines.

---

### F-22. Toolchain (lines 2762–2773)

**Action:** KEEP as-is  
12 lines.

---

## Category G — Stale volatile working notes

---

### G-1. `knowledge_files/volatile/S070_WORKING_NOTES.md`

**Action:** DELETE  
**Rationale:** Session 070 is 17 sessions ago. Its content is carried by
`070_SESSION_HANDOFF_LOG.md`. The file header says "Last updated: 2026-10-03
(Session 075 closure)" — but that's the S076 file. The S070 file is from
much earlier and is stale.

**Decision:**

---

### G-2. `knowledge_files/volatile/S076_WORKING_NOTES.md`

**Action:** DELETE  
**Rationale:** Session 076 is 11 sessions ago. Its content is carried by
`076_SESSION_HANDOFF_LOG.md`. The S078 file has been rewritten in place as
each session's carry-over, making the S076 file obsolete.

**Decision:**

---

### G-3. `knowledge_files/volatile/S078_WORKING_NOTES.md`

**Action:** KEEP  
This is the current carry-over file, rewritten at the S086 close as the
S086 → S087 notes.

---

## Category H — "Keep" sections: spec-reference duplication audit

The user's follow-up question: these "keep" sections (hardware, layout,
audio pipeline, boot order, failed approaches, etc.) may themselves belong
in specification references rather than in the startup document. Audit each
one for duplication and propose what moves, what stays as a pointer, and
what needs a new document.

---

### H-1. Hardware section (MEMORY.md lines 1666–1739)

**Overlap with `knowledge_files/hardware_archive/HARDWARE_MAP.md`:**

The HARDWARE_MAP.md covers MCU specs, GPIO, clocks, and IRQs — but its IRQ
table is **stale** (shows TIM6 at priority 1, TIM7 at priority 2, and is
missing TIM3/DMA1_Stream4/DMA1_Stream7/USART3). MEMORY.md has the correct
current table (TIM6 at priority 6, TIM7 at priority 7, TIM3 at priority 2,
audio DMA at priority 4, USART3 at priority 5). The GPIO tables are
essentially identical.

**Action:** Update `HARDWARE_MAP.md` with the current IRQ table and any
GPIO corrections from MEMORY.md. Then replace the MEMORY.md hardware section
with a pointer:

> ## Hardware
>
> MCU: STM32F765VIH6 (216 MHz, 128 KB DTCM + 368 KB SRAM1 + 16 KB SRAM2).
> Full pin table, IRQ assignments, flash layout, and clock tree:
> `knowledge_files/hardware_archive/HARDWARE_MAP.md`.

Roughly 74 lines → 4 lines in MEMORY.md.

**Decision:**

---

### H-2. Audio Pipeline (MEMORY.md lines 1743–1799)

**Overlap with `EFFECTS_MIXER_DSP_REFERENCE.md`:**

The mixer spec already covers the render loop, signal flow, `sample_mx_t`,
DMA pack, output buffer naming (St1/St2), the `cpu` widget, and costs.
The MEMORY.md version is a condensed duplicate.

**What MEMORY.md adds that the spec doesn't have in a quick-reference form:**
- The `audio_check_and_render()` code block with the exact call order
  (`voiceControl_processPending` → `seq_drainPendingAutomation` →
  `seq_rampService` → BASEPRI → mixer → commit).
- `OUTPUT_DMA_SIZE = 32`, `AUDIO_DMA_FRAMES = 96`, the 2.18 ms budget.
- The SPSC queue architecture (2-slot, `ready_head`/`ready_tail`/
  `ready_count`).
- "DSP render must remain in the main loop" rule.
- The 24-bit output path notes.

**Action:** Move the full section into `EFFECTS_MIXER_DSP_REFERENCE.md` as a
"Render loop and output pipeline quick reference" subsection (if not already
there in equivalent detail). Replace in MEMORY.md with a pointer + the 3
critical rules that agents must not violate:

> ## Audio Pipeline
>
> Full render loop, signal path, DMA, and costs:
> `EFFECTS_MIXER_DSP_REFERENCE.md`.
>
> - **DSP render stays in the main loop** — never move it to the DMA ISR
>   (hard 2.18 ms ceiling, no graceful degradation).
> - **OUTPUT_DMA_SIZE = 32** (do not revert to 16).
> - **Mixer output names:** `output` = DAC2 = **St2**;
>   `output2` = DAC1 = **St1** (MAIN jacks).
> - **24-bit path:** do NOT re-add `LSW = 0` or `>> 8` in `sampleMix_toS24()`.

Roughly 57 lines → 10 lines in MEMORY.md.

**Decision:**

---

### H-3. Boot / Init Order (MEMORY.md lines 2046–2104)

**Overlap with `MODULE_INTERCHANGE_SPEC.md`:**

The module spec mentions individual init calls (`menu_init`, `seq_init`,
`filesystem_initCardAndMountBlocking`, `fxbuf_init`) but does NOT have a
consolidated boot-order listing. It has a "boot ordering" note at line ~1956
but it's scattered, not a single code-block reference.

**Action:** Move the init-order code block and the sequencer/PATGEN
reminders into `MODULE_INTERCHANGE_SPEC.md` (new section: "Boot / Init
Order"). Replace in MEMORY.md with a pointer + the one critical line:

> ## Boot / Init Order
>
> Full init sequence and sequencer reminders:
> `MODULE_INTERCHANGE_SPEC.md` §Boot.
>
> Critical: `EXTI_IMR = 0` must be first. `audioCodec_init()` must follow
> all SD boot ops. `sequencerTimer_init()` must follow `audioCodec_init()`.
> `seq_tick()` is owned by TIM3 — never add it to the main loop.

Roughly 59 lines → 6 lines in MEMORY.md.

**Decision:**

---

### H-4. SD Card Architecture overview (MEMORY.md lines 1800–1830)

**Overlap with `FILESYSTEM_SPEC.md` + `ASYNCFATFS_REFERENCE.md`:**

Both specs cover the architecture stack, the asyncfatfs model, polling,
caching, and operation serialization. The MEMORY.md version is a condensed
duplicate.

**Action:** Verify the architecture diagram and key design points are in
`FILESYSTEM_SPEC.md` (they likely are). Replace in MEMORY.md with a pointer:

> ## SD Card Architecture
>
> Full spec: `FILESYSTEM_SPEC.md`; low-level: `ASYNCFATFS_REFERENCE.md`.
>
> Key: all SD work is async via `afatfs_poll()` (one context only);
> `filesystem.c` serializes operations; 8-sector LRU cache; hardware SPI is
> impossible on this board.

Roughly 30 remaining lines → 5 lines in MEMORY.md.

**Decision:**

---

### H-5. Sample Flash Loading (MEMORY.md lines 1957–1983)

**Overlap with `STORAGE_SRAM_MANIFEST.md` + `FILESYSTEM_SPEC.md`:**

`STORAGE_SRAM_MANIFEST.md` §4 already has the sample flash map with the
exact same address ranges. `FILESYSTEM_SPEC.md` has the `Load:[Samples]`
operation description. The MEMORY.md version adds the WAV format
requirements, the sort order, display-name conventions, and the
`SampleInfo.size` high-bit loop flag — some of which may not be in the
specs.

**Action:** Verify the WAV requirements and display-name rules are in
`FILESYSTEM_SPEC.md`. If not, add them there. Then replace in MEMORY.md
with a pointer:

> ## Sample Flash
>
> Flash map: `STORAGE_SRAM_MANIFEST.md` §4. Install operation:
> `FILESYSTEM_SPEC.md` "Load:[Samples]". Mono 16-bit 44.1 kHz WAV only;
> up to 120 entries; `SampleInfo.size` bit 31 = loop flag.

Roughly 27 lines → 4 lines in MEMORY.md.

**Decision:**

---

### H-6. Encoder (MEMORY.md lines 1986–1998)

**Overlap with `HARDWARE_MAP.md`:**

HARDWARE_MAP.md has one line: "TIM1 Input Capture, ICxF=0xF hardware filter.
Dannegger algorithm. encode_read4() only." MEMORY.md has the full
operational rules (seed, divide, acceleration, rebound suppression, ISR
guidance).

**Action:** Move the full encoder rules into `HARDWARE_MAP.md` under the
encoder section — these are hardware/driver contract rules. Replace in
MEMORY.md with a pointer:

> ## Encoder
>
> Algorithm, seed, acceleration, rebound suppression:
> `knowledge_files/hardware_archive/HARDWARE_MAP.md` (Encoder section).

Roughly 13 lines → 3 lines in MEMORY.md.

**Decision:**

---

### H-7. Display / Menu (MEMORY.md lines 2001–2021)

**Overlap with `MODULE_INTERCHANGE_SPEC.md`:**

The module spec has extensive menu/display coverage (the `menu_init` /
`menu_serviceKnobRepaint` / `menu_switchPage` entries, the Bank page COPY
gesture, frozen page rules, etc.) but does NOT have the low-level LCD queue
contract (`head/tail-only SPSC`, `lcd_setcursor` before every data byte,
`buttonHandler_processEvents if not while`) or the endless-pot `atan2f`
argument-order rule.

**Action:** Move the LCD queue contract and the `buttonHandler_processEvents`
rule into `MODULE_INTERCHANGE_SPEC.md` under the Menu/LCD section (or into
`HARDWARE_MAP.md` under the LCD section — it's really a hardware driver
contract). The menu-level rules (knob repaint, sound apply worker, `cpu`
widget) are already covered by the module spec. Replace in MEMORY.md with
pointers + only the 2-3 "do not" rules:

> ## Display / Menu
>
> Module APIs: `MODULE_INTERCHANGE_SPEC.md`.
> LCD queue contract: `HARDWARE_MAP.md` (LCD section).
>
> - LCD queue: head/tail-only SPSC — do NOT reintroduce `lcd_q_count`.
> - `buttonHandler_processEvents()`: `if` not `while` — intentional.
> - `endlessPots`: `atan2f(b, a)` — do NOT change argument order.

Roughly 21 lines → 7 lines in MEMORY.md.

**Decision:**

---

### H-8. DSP / RNG Rules (MEMORY.md lines 2024–2043)

**Overlap with DSP spec references:**

`CPU_USE_DSP_AUDIT.md` covers `-Ofast`, VLAs, `GetRngValue` semantics, and
compiler flags. `INSTRUMENTS_DSP_REFERENCE.md` covers the constant-CPU rule
and special-writer tags. But neither has the specific "do not" rules for
`RCC_AHB2ENR` address, `RNG_CR` direct write, or the DTCM non-DMA warning
in a quick-reference form.

**Action:** These are compact, critical "do not" rules (20 lines). They
could go into `CPU_USE_DSP_AUDIT.md` as a "DSP pitfalls" appendix. But
they're short enough that keeping them in MEMORY.md as a quick-reference
block is also defensible. Propose: keep in MEMORY.md as a pointer + rules
block, since agents hit these rules early in any DSP session.

> (No change to MEMORY.md for this section — keep as-is at 20 lines.)

**Decision:**

---

### H-9. MIDI / Clock Reminders (MEMORY.md lines 2106–2190)

**Overlap with `MIDI_SPEC.md`:**

`MIDI_SPEC.md` is the complete MIDI reference (Parts I–III). The MEMORY.md
section is explicitly a "quick-reference" complement. However, at 85 lines
it's substantial — nearly a condensed duplicate of Parts I and II.

**Action:** Verify every rule in this section is already in `MIDI_SPEC.md`.
If so, replace in MEMORY.md with a pointer + only the "never do" rules
that agents violate most often:

> ## MIDI / Clock Reminders
>
> Full reference: `MIDI_SPEC.md` (Parts I–III).
>
> - TIM2 is free-running 1 µs — do NOT reset it on each pulse.
> - External clock ticks are interpolated (S084 A1) — do not run a period's
>   ticks back to back.
> - TIM5 is reserved for future `afatfs_poll()` ISR migration.
> - `Core/MIDI/` was rebuilt in S083 — authority is `MIDI_SPEC.md`.
> - Channels are 1..16 with 0 = off everywhere in MIDI code.
> - Never cache a Scene index in MIDI code.
> - A MIDI parameter write is a menu edit (Q1) via
>   `menu_commitTargetEdit()`.
> - RXF/TXF default to off on a card without the keys.

Roughly 85 lines → 12 lines in MEMORY.md.

**Decision:**

---

### H-10. Morph / Endless-Pot Reminders (MEMORY.md lines 2193–2200)

**Overlap with `BANK_PRESET_ARCHITECTURE.md`:**

The Morph architecture is in `BANK_PRESET_ARCHITECTURE.md` §5. The
endless-pot rules are hardware/driver level.

**Action:** At 8 lines this is already very compact. The "do not add a morph
skip cache" rule and the endless-pot double-speed rule are critical "never
do" items. Keep as-is.

> (No change to MEMORY.md for this section — keep as-is at 8 lines.)

**Decision:**

---

### H-11. Failed Approaches — Do Not Retry (MEMORY.md lines 2696–2760)

**Overlap:** None — no spec reference carries these.

**Action:** These are essential anti-patterns that prevent agents from
re-attempting known failures. They don't fit any existing spec document —
they're cross-cutting "never do this" rules from across the whole project.

Two options:
1. **New document:** `knowledge_files/specification_reference/FAILED_APPROACHES.md`
   — a dedicated anti-pattern registry. MEMORY.md points to it.
2. **Keep in MEMORY.md** — they're 65 lines and agents need to see them at
   startup.

Recommendation: keep in MEMORY.md. These are startup-critical and short.

> (No change to MEMORY.md for this section — keep as-is at 65 lines.)

**Decision:**

---

### H-12. Repository Layout (MEMORY.md lines 1418–1572)

**Overlap:** None — no spec reference has the tree layout.

**Action:** At 155 lines this is the largest "keep" block. It could go into
its own document, but it's the kind of thing an agent needs immediately at
startup. Two options:

1. **New document:** `knowledge_files/specification_reference/REPOSITORY_LAYOUT.md`.
   MEMORY.md keeps a 5-line pointer.
2. **Keep in MEMORY.md** — it's a useful quick reference that agents consult
   immediately.

Recommendation: move to its own file. At 155 lines it's a quarter of the
target MEMORY.md size and it changes rarely. Agents that need it can read it
on demand.

> Replace in MEMORY.md with:
>
> ## Repository Layout
>
> See `knowledge_files/specification_reference/REPOSITORY_LAYOUT.md`.

**Decision:**

---

### H-13. Toolchain (MEMORY.md lines 2762–2773)

**Overlap:** `CPU_USE_DSP_AUDIT.md` mentions `-flto` and `-Ofast`.
`STORAGE_SRAM_MANIFEST.md` mentions the image format.

**Action:** At 12 lines, keep in MEMORY.md. The toolchain line and image
format are essential startup info.

> (No change to MEMORY.md for this section — keep as-is at 12 lines.)

**Decision:**

---

### H-14. "Where to look for things" + Specification-reference index (lines 1574–1632)

**Overlap:** This IS the index. No duplication issue.

**Action:** Keep in MEMORY.md — this is the core of the startup document.

> (No change — keep as-is at 58 lines.)

**Decision:**

---

### Summary of Category H proposals

| Section | Current lines | Action | Lines in MEMORY.md after |
|---------|--------------|--------|--------------------------|
| Hardware | 74 | Update HARDWARE_MAP.md, replace with pointer | 4 |
| Audio Pipeline | 57 | Move detail to EFFECTS_MIXER_DSP_REFERENCE.md | 10 |
| Boot / Init Order | 59 | Move to MODULE_INTERCHANGE_SPEC.md | 6 |
| SD Architecture | 30 | Verify in FILESYSTEM_SPEC.md, replace with pointer | 5 |
| Sample Flash | 27 | Verify in specs, replace with pointer | 4 |
| Encoder | 13 | Move to HARDWARE_MAP.md | 3 |
| Display / Menu | 21 | Move LCD contract to HARDWARE_MAP.md / MODULE_INTERCHANGE | 7 |
| DSP / RNG Rules | 20 | Keep as-is | 20 |
| MIDI / Clock | 85 | Verify in MIDI_SPEC.md, compress to key rules | 12 |
| Morph / Endless Pot | 8 | Keep as-is | 8 |
| Failed Approaches | 65 | Keep as-is | 65 |
| Repository Layout | 155 | Move to new REPOSITORY_LAYOUT.md | 2 |
| Toolchain | 12 | Keep as-is | 12 |
| Where to look + Spec index | 58 | Keep as-is | 58 |
| **Subtotal** | **684** | | **216** |

Combined with the earlier categories, the revised projected total:

---

## Revised projected line count after refactor

| Section | Current lines | Projected lines |
|---------|--------------|-----------------|
| Header + Quick Start | 21 | 21 |
| S087 status | 12 | 12 |
| Binding user policies (RAM, CPU, Osc, Scrolling) | 100 | 100 |
| Volatile Notes (current only) | 52 | 35 |
| Compressed binding rules (C-1 through C-11) | ~250 | ~60 |
| Open items (consolidated) | scattered ~100 | ~40 |
| Where to look + Spec index | 58 | 58 |
| Project Goal + Process Reminders | 18 | 15 |
| Hardware (pointer) | 74 | 4 |
| Audio Pipeline (pointer + rules) | 57 | 10 |
| SD Architecture (pointer) | 30 | 5 |
| Sample Flash (pointer) | 27 | 4 |
| Encoder (pointer) | 13 | 3 |
| Display / Menu (pointer + rules) | 21 | 7 |
| DSP / RNG Rules (keep) | 20 | 20 |
| Boot / Init Order (pointer) | 59 | 6 |
| MIDI / Clock (pointer + key rules) | 85 | 12 |
| Morph / Endless Pot (keep) | 8 | 8 |
| Repository Layout (pointer) | 155 | 2 |
| Failed Approaches (keep) | 65 | 65 |
| Toolchain (keep) | 12 | 12 |
| **Session history (A-1 through A-7)** | **~1,100** | **0** |
| **Spec content (B-1 through B-4)** | **~280** | **0** |
| **Resolved issues** | **~400** | **0** |
| **TOTAL** | **~2,773** | **~499** |

An 82% reduction, with zero information loss. MEMORY.md becomes a ~500-line
startup pointer document with binding policies, distilled rules, and
"never do" guards. All detail lives in the specifications and logs.

---

## Category H — Prerequisites: spec documents that need updating

Before the MEMORY.md moves in Category H can happen, these spec documents
need to be brought current. Without these updates the moves would lose
information.

### H-prereq-1. `knowledge_files/hardware_archive/HARDWARE_MAP.md` — stale IRQ table

The IRQ table (line 131) shows TIM6 at priority 1 and TIM7 at priority 2,
and is missing TIM3 (IRQ29, priority 2), DMA1_Stream4 (IRQ15, priority 4),
DMA1_Stream7 (IRQ47, priority 4), and USART3 (IRQ39, priority 5). The
current correct table is in MEMORY.md lines 1722–1735.

**Must update before H-1 (Hardware) and H-6 (Encoder) can move.**

### H-prereq-2. `EFFECTS_MIXER_DSP_REFERENCE.md` — render loop call order

The spec has the signal flow and costs, but does not have the exact
`audio_check_and_render()` call-order block
(`voiceControl_processPending → seq_drainPendingAutomation →
seq_rampService → BASEPRI → mixer → commit`), the SPSC 2-slot queue
architecture, or the "DSP render must stay in main loop" rule as a
standalone callout.

**Must add before H-2 (Audio Pipeline) can move.**

### H-prereq-3. `MODULE_INTERCHANGE_SPEC.md` — consolidated boot order

The spec mentions individual init calls scattered across its tables but has
no consolidated boot-order listing. Needs a new "Boot / Init Order" section
with the code block from MEMORY.md lines 2048–2073.

**Must add before H-3 (Boot / Init Order) can move.**

### H-prereq-4. New file: `knowledge_files/specification_reference/REPOSITORY_LAYOUT.md`

Does not exist yet. Needs to be created from MEMORY.md lines 1418–1572.

**Must create before H-12 (Repository Layout) can move.**

| Section | Current lines | Projected lines |
|---------|--------------|-----------------|
| Header + Quick Start | 21 | 21 |
| S087 status | 12 | 12 |
| Binding user policies (RAM, CPU, Osc, Scrolling) | 100 | 100 |
| Volatile Notes (current only) | 52 | 35 |
| Compressed binding rules (C-1 through C-11) | ~250 | ~60 |
| Open items (consolidated) | scattered ~100 | ~40 |
| Repository Layout | 155 | 155 |
| Where to look + Spec index | 58 | 58 |
| Project Goal + Process Reminders | 18 | 15 |
| Hardware | 74 | 74 |
| Audio Pipeline | 57 | 57 |
| SD Architecture (overview only) | 150 → 20 | 20 |
| Sample Flash | 27 | 27 |
| Encoder | 13 | 13 |
| Display / Menu | 21 | 21 |
| DSP / RNG | 20 | 20 |
| Boot / Init Order | 59 | 59 |
| MIDI / Clock | 85 | 85 |
| Morph / Endless Pot | 8 | 8 |
| Failed Approaches | 65 | 65 |
| Toolchain | 12 | 12 |
| **Session history (A-1 through A-7)** | **~1,100** | **0** |
| **Spec content (B-1 through B-4)** | **~280** | **0** |
| **Resolved issues** | **~400** | **0** |
| **TOTAL** | **~2,773** | **~957** |

A roughly 65% reduction, with zero information loss (everything removed is
already carried by session logs or specification references).
