# Session 057 — Pre-Reader Assessment

Status: **assessment only, no code changed this session.** Goal: read back
through the logs, `SCOPING_TARGETS.md`, and the current test card to produce
a punch list of what needs to be wrapped/bugfixed in load/save, HCNAMES,
`.hcindex`, AutoSave, and the dev logging/trace/diagnostic systems before
starting the AutoSave boot reader (`AUTOSAVE_READ_PLAN.md`). Reordered per
discussion: hardware-verify the page-exit expedite first, then P1 next
(both fully diagnosed, no more investigation needed before acting). Two
companion plans spun out of this session's work: `S057_SETTINGS_WRITE_SAFE.md`
(settings.cfg safe-write redesign) and `S057_SCENE_OVERWRITE_SAFE.md`
(empty-Scene overwrite guard, which P1 gates).

---

## 1. Session 056 page-exit expedite — hardware-verify first

`fs_autosave_page_suppressed` (`filesystem.c:1613` decl, scheduler at
`~19846-19866`) is code-complete and builds clean but is explicitly flagged
in `056_SESSION_HANDOFF_LOG.md` as **pending hardware verification** and is
the stated blocker in that handoff ("BLOCKERS: Page-exit expedite needs
hardware test before closing"). This is the newest, least-verified change
sitting directly in the writer's scheduling path that everything else below
builds on top of — confirm it before touching anything else. (User recalls
possibly having already tested this — re-check and close out, don't
re-derive from scratch if evidence already exists.)

---

## 2. P1 — Bank Save present-mask overwrite (next)

Confirmed still present at `filesystem.c:14028`:

```c
bank_setScenePresentMask(op_bank_scene_save_mask);
```

Still the caller-supplied **subset** mask assigned directly, not unioned
with the retained resident mask — the exact bug and one-line fix
(`bank_setScenePresentMask((uint16_t)(bank_scenePresentMask() |
op_bank_scene_save_mask))`) already recorded in `SCOPING_TARGETS.md:249-287`
and `AUTOSAVE.md:106-110`. A partial `Save:[Bank]` still silently drops
resident Scenes from `scene_present_mask`, which stops their AutoSave
capture and desyncs Load/Save LEDs from what's actually in RAM. Small,
scoped, already fully diagnosed — good candidate to close first, since Bank
Load already does the union correctly (`filesystem.c:10753`) and Save is
the one inconsistent writer.

**Newly confirmed this session — P1 is not just a bookkeeping bug, it also
feeds the Scene-selection UI directly:** the SEQ LED toggle surface for
`Save:[Bank]` (`menu_loadSaveSelectableSceneMask()` →
`menu_residentPresentSceneMask()` → `bank_scenePresentMask()`,
`menu.c:4745-4765`) gates both which LEDs can light (`menu.c:4856`) and
which button presses are allowed to toggle a bit at all
(`menu.c:4907`) directly off the same present mask P1 corrupts. So until
P1 is fixed, a Scene that's still genuinely resident in SRAM but got
dropped from the mask by a prior partial Bank Save becomes untoggleable —
its SEQ LED can't be selected for the *next* save either. **This is why
P1 must land before `S057_SCENE_OVERWRITE_SAFE.md`'s guard work** — that
plan's proposed empty-Scene check leans on `bank_scenePresentMask()` as the
"has real content" signal, which is only trustworthy once P1 no longer
corrupts it. See that document for the full design.

---

## 3. Scene/Bank empty-overwrite guard — see `S057_SCENE_OVERWRITE_SAFE.md`

New companion plan (this session): make it structurally impossible for an
empty resident Scene to overwrite an occupied on-disk Scene, at the actual
save/write layer, not just the SEQ LED modal. Confirmed the destructive
step in both `filesystem_saveBankDirectory_tick()` and
`filesystem_saveSceneDirectory_tick()` is an unconditional whole-object
delete (`filesystem_deleteSlotDirectoryStart()` / `filesystem_deleteSceneSlotDirectoryStart()`,
both at each function's `case 4`) that runs *before* any new content is
written — so a correctness guard must land before that phase, not after.
Depends on P1 (§2). Full design in the companion document.

---

## 4. settings.cfg safe write — see `S057_SETTINGS_WRITE_SAFE.md`

New companion plan (this session): `settings.cfg` is currently a direct
in-place truncate-and-rewrite with no backup, unlike AutoSave's
`.hcprms1/2`. A power loss mid-write can leave it empty or torn, silently
reverting `active_bank` (and everything else) to firmware defaults with no
error surfaced. Agreed design: temp file + explicit `afatfs_sync()` +
validated remove-old/rename-promote, plus a boot-time recovery prelude and
a kept terminator line for future format-expansion completeness checks.
Independent of P1/P2 below; not entangled with Bank Load's own code path
(confirmed there's only one shared writer). Full design in the companion
document.

---

## 5. P2 — redundant settings write on every boot (still open, needs a decision)

Confirmed still present: three unconditional
`filesystem_markSettingsDirty()` calls (`filesystem.c:10645, 10800, 14042`).
As documented (`SCOPING_TARGETS.md:289-317`), this produces one
value-idempotent `settings.cfg` rewrite on every boot with a valid Bank.
Harmless to correctness, has a real upside (reconciles `settings.cfg` if
boot fell back to a different Bank slot), but is an extra SD write and one
foreground filesystem op per power-on that was never deliberately accepted
or rejected — it was simply left as the Session 052 default. Needs an
explicit "accept as-is" or "gate on `fs_settings_runtime_ready`" decision,
not more investigation. Independent of §4 — once the write itself is safe
(§4), this decision is purely about whether the extra write should happen
at all, not whether it's dangerous.

---

## 6. Sequencer chaselight can disappear — initial investigation

User-reported glitch, not yet root-caused. Traced the rendering path to
scope what to check next session; no fix attempted.

**Rendering logic** (`led_updateCurrentStep()`, `ledHandler.c:972-1010`,
driven by `led_processSeqLedState()` when `sequencer.c` sets
`SEQ_LED_DIRTY_CHASE`): the chase LED is shown only when *all* of —
`menu_getViewedPattern() == menu_playedPattern`, the step falls inside the
currently displayed `menu_currentBar` window, and `menu_activePage` is one
of the allowed pages (`< MENU_MIDI_PAGE`, or `SEQ_PAGE`, or `EUKLID_PAGE`)
— hold; any mismatch calls `led_clearActive_step()` and the light goes
dark. That's a lot of conditions any one of which silently hides the
light, by design, whenever the code's belief about them doesn't match the
user's.

**Producer side** (`sequencer.c:451-452, 488-489`): `seq_ledState.chaseStep`
is set from `seq_stepIndex[menu_getActiveVoice()]` — a **per-voice** step
index, not one global playback position.

**Hypotheses worth checking next session, roughly most-to-least likely:**

1. **Viewed/played pattern desync** — this is the exact class of bug
   already found and only partially fixed in the 2026-08-21 Scene-Pattern
   session (`MEMORY.md`, `SCENE_LOAD_PAT_RESTORE.md`): `seq_activePattern`/
   `menu_shownPattern` can drift from `scene_getActiveIndex()`.
   `SCOPING_TARGETS.md`'s "Single-source-of-truth Pattern/Scene index" item
   explicitly says that fix patched only "the one reachable seam," not the
   systemic issue, and was deliberately not generalized because it touches
   PERF queued-Scene-change semantics. A chaselight that vanishes and
   doesn't come back on a page that should show it is consistent with
   `menu_getViewedPattern()`/`menu_playedPattern` disagreeing without a UI
   cue that they've drifted. Strongest lead — same known-incomplete fix,
   different symptom.
2. **Per-voice step-index switch** — since `chaseStep` is read from
   `seq_stepIndex[menu_getActiveVoice()]`, switching the viewed/active
   voice could momentarily reference a step index that isn't valid/current
   for the newly active voice, especially relevant given per-track step
   timing is already a known area of active design work
   (`SCOPING_TARGETS.md` Phase 4.7, "Per-track step timing scale").
3. **`led_currentStepLed` bookkeeping desync from an unrelated repaint** —
   `led_setActive_step()`'s toggle/restore bookkeeping
   (`led_currentStepLed`) assumes it's the only writer of `LED_STEP1..16`.
   If any other full-repaint path (page switch, `menu_repaintAll()`, or
   similar) writes those same LED indices directly without going through
   `led_clearActive_step()` first, `led_currentStepLed`'s idea of "what's
   currently lit" goes stale, and a later `led_setActive_step()` call for
   the *same* step number becomes a no-op (`if (led_currentStepLed !=
   ledNr)`) even though the physical LED was already overwritten by
   something else — light silently stays dark until the step number
   actually changes. Would need to audit other callers that touch
   `LED_STEP1..16` directly.
4. **Page-boundary fragility** — `menu_activePage < MENU_MIDI_PAGE` is a
   magic-number stand-in for "every page that should still show chase."
   Any future page-table reordering (`menuPages.h`) could silently move a
   page across that boundary without anyone noticing the chase-visibility
   contract broke.

Recommend reproducing on hardware with `DEV_MODE_DIAGNOSTIC` on to watch
`menu_getViewedPattern()`/`menu_playedPattern`/`menu_activePage`/
`menu_currentBar` at the moment the light disappears, before assuming which
hypothesis above is the actual cause.

---

## 7. `AUTOSAVE_TRACE_RECORD_COUNT` reversion — cheap, independent, close whenever

Still 2,048 (`config.h:330`, "TEMPORARY approved expansion"), normal default
64. `SCOPING_TARGETS.md:478-482` already flags this as needing an explicit
keep/revert decision now that the pinned recursive-delete target is
hardware-confirmed closed. Pure housekeeping, no dependency on anything
else in this list.

---

## 8. Boot Kit-directory sanitizer (investigated this session)

### 8a. Card audit — no malformed Kit currently present

Checked every `SD_CARD/Kit/NNN Name/` folder (44 kits): every one has a
`kitset.kcg`, sizes cluster into five expected bands (324/379/385/391/397/399
bytes) with nothing anomalous. `019 Organity` — the slot named in the
`KQ019KST` boot-failure record (`SCOPING_TARGETS.md:186`) — is structurally
valid: six `[slotN]` sections, all six referenced instrument files present.
**This confirms `SCOPING_TARGETS.md`'s existing conclusion**: `KQ019KST` was
the sanitizer's own cost, not evidence of a bad Kit 019 file. There is
nothing on the current card that would reproduce a Kit-specific hang.

### 8b. Card hygiene found while looking (not sanitizer bugs, but relevant)

- **`Bank/old012-c76c`** — a non-canonical directory (`04 Slak` with
  `effects.fx`/`Kit Emott`/`sceneset.scg`/`pattern.pat` inside) that doesn't
  match the product's `NNN Name` folder contract. Naming pattern (`old` +
  slot + hash suffix) is consistent with the **pre-Session-053** tmp/old
  promotion overwrite implementation that Session 053 replaced with direct
  delete/recreate (`MEMORY.md` Session 053 entry). It's dead weight left
  over from before that switch — invisible to `.hcindex`/the menu, sitting
  on the card indefinitely. The deferred boot-sanitizer refactor
  (`SCOPING_TARGETS.md:211-246`) canonicalizes every numbered folder under
  `/Bank` but has no stated policy for a folder that **doesn't** parse as
  numbered at all. Worth a one-line policy decision (ignore permanently /
  flag for manual deletion) when that refactor is scoped, so it doesn't
  silently persist forever.
- **`Bank/015 LoadTst!`** — checked whether the `!` is a sanitizer problem.
  It isn't: `fat_lfnCharAllowed()` (`Core/Hardware/SD/asyncfatfs/fat_standard.c:73-110`)
  explicitly allows `!` in its firmware-writable character set alongside
  `@#$%&'`. This is a legal name, not card damage.

### 8c. The excessive-phase finding — confirms and quantifies the deferred refactor

`filesystem_quarantineKitLibraryBlocking()` (`filesystem.c:17476-17599`),
called from `filesystem_createLibraryIndexBlocking(FS_LIBRARY_INDEX_KIT)`
before every boot's Scene/Bank scan, does **full content validation** of
every present root Kit, not just a name/reconstructability check:

- `filesystem_validateCurrentKitBlocking()` (`filesystem.c:17182-17284`)
  opens `kitset.kcg` and streams it **one byte at a time**
  (`filesystem_blockRead(file, &byte, 1u)`), then for each of the six
  `STORAGE_KIT_SLOT_COUNT` member slots, opens and closes the referenced
  instrument file individually via `filesystem_blockOpenLfn()` /
  `filesystem_blockClose()` — each a full blocking LFN directory lookup.
- That's **1 + 6 = 7 blocking file operations per Kit**, all under **one
  shared `KITQUAR` 10-second deadline for the entire root Kit traversal**
  (`filesystem_bootLoggingArm("KITQUAR ")` at `filesystem.c:21148`, one arm
  per whole quarantine pass, not per Kit — `filesystem_bootLoggingArm()` at
  `filesystem.c:2893-2924` starts a fresh 10 s window only on that single
  call).
- On the current card that's **44 Kits × 7 opens = 308 blocking operations**
  sharing one 10-second budget. This scales linearly with total Kit count
  and is exactly the "unnecessarily broad/expensive boot gate" already
  identified in `SCOPING_TARGETS.md:192-194`. As the Kit library grows (or
  on a slower card), this is a live risk of a **false boot failure** — the
  code correctly distinguishes `IO_ABORT` (timeout) from
  `INVALID_CONTENT` and refuses to rename on timeout
  (`filesystem_validateCurrentKitBlocking()`'s IO_ABORT returns, lines
  17201-17203, 17232-17236, etc.), so it won't quarantine good data — but
  it still fails the whole boot (`goto boot_filesystem_timeout` in
  `main.c`) for a card that simply has more Kits or is slower, with no way
  to distinguish that from a genuine hang.

**Recommendation:** this is already fully scoped as a deferred refactor
(`SCOPING_TARGETS.md:211-246` — remove full-content parsing/quarantine from
boot, canonicalize-and-index instead, move real validation to the actual
load attempt). Given the reader work depends on trustworthy, fast boot
sequencing (`AUTOSAVE_READ_PLAN.md §3` explicitly reorders boot around a new
candidate-validation step), this is a good candidate to implement **as part
of this wrap**, before the reader's own boot-order change lands on top of
it — doing both boot-order changes at once is more error-prone than doing
this one first, on its own, with its own hardware test.

---

## 9. HCNAMES / `.hcindex` open items

### 9a. Live discrepancy found on the current test card — needs a trace-backed investigation before the reader lands

Cross-checked the three sources of Bank identity truth on `SD_CARD/`:

| Source | Value |
|---|---|
| `settings.cfg` | `active_bank=15` |
| `.hcprms1` (generation 1, the winner — `.hcprms2` is generation 0) | Bank payload: restore slot `0x000f` (15), name `"LoadTst!"` |
| `.hcnames` row 0 | `LoadTst<TAB>012` |

`settings.cfg` and the winning AutoSave record **agree** (Bank 15,
`"LoadTst!"`). `.hcnames` row 0 disagrees with both — it names Bank 12
(`"012 LoadTst"`, no `!`) instead of Bank 15 (`"015 LoadTst!"`). This is
exactly the class of divergence `AUTOSAVE_READ_PLAN.md §3-4` is designed to
detect (`settings.cfg` vs. record agreement) and `§P1/§5` depend on trusting
(`.hcnames` as the per-row provenance ground truth) — here the two would
disagree with each other in a way neither documented algorithm currently
covers explicitly (both settings.cfg and the winning record already agree
with each other; only HCNAMES's Bank row is stale).

No `/bootlog.bin` or `/asavetrc.bin` exists on this card copy, so there's no
trace evidence to root-cause this from — can't currently tell whether this
is (a) a genuine gap where some Bank-activation path updates
`settings.cfg`/AutoSave but skips the HCNAMES row-0 publish, (b) a later
operation reverting row 0 after it was correctly published, or (c) simple
test-session artifact (e.g., a manual `settings.cfg` edit during testing,
unrelated to firmware behavior). Given HCNAMES is the reader's whole-Scene
trust anchor (P1 in `AUTOSAVE_READ_PLAN.md`), this is worth a dedicated
logging-enabled boot capture to confirm before reader implementation starts,
rather than assuming it's test debris.

### 9b. Known, still-open, already-scoped items (re-confirmed present, not re-investigated in depth)

- **Name-cache ownership interlock** (`SCOPING_TARGETS.md:458-469`) — the
  AutoSave writer reads the shared 9,000-byte name cache live while
  serializing, and Menu's `filesystem_clearNameCache()` bypasses facade
  arbitration from 18 call sites; only the two hottest were closed in
  Session 055. Consequence is a torn AutoSave record, not a hang. Still
  open.
- **Top-level Load/Save entry trace gap** (`SCOPING_TARGETS.md:470-477`,
  `DEV_MODES.md:240-249`) — `menu_traceInstrumentEntry()`'s `'N'` record is
  gated on `menu_instrumentLoadActive`, so top-level Kit/Scene/Bank
  refusals are invisible, and `menu_requestSceneEntryName()` has no trace
  producer at all. Cost real investigation time twice already (Session
  055). Recommended before the next Load/Save-family investigation —
  directly relevant since HCNAMES work is next.

---

## 10. Known-incorrect boot Bank Load AutoSave write — deliberately deferred, now directly relevant

`AUTOSAVE.md` doesn't currently document this (it's in
`SCOPING_TARGETS.md:326-339`): on a boot that loads a Bank, `.hcprms1/2` end
up with `scene_present_mask=0x0000`, `active_scene=0`, empty Scene
payloads, because `autosave_setMutationTrackingEnabled(1)` only turns on
after the boot Bank Load already ran. This was deliberately left alone
("boot population must not be mistaken for user mutation") but is exactly
what `AUTOSAVE_READ_PLAN.md §3` and its deferred boot-fallback scope
(`SCOPING_TARGETS.md:355-372`) are built to correct. Not a new finding, but
worth flagging as **the** piece of already-scoped work that turns this from
a deferred note into an active blocker: the reader can't be implemented
without also implementing the boot-fallback latch this depends on.

---

## 11. Open design question already flagged in `AUTOSAVE.md` — resolve before/alongside the reader

`AUTOSAVE.md:262-269`: the writer's live-Bank-match validator
(`autosave_streamValidationMatchesBank()`) currently treats *any* Bank
slot/name mismatch as "foreign record → regenerate," which the doc itself
says "is not the settled future semantic contract" — a legitimate Bank
session transition (the exact scenario captured on hardware in
`056_SESSION_HANDOFF_LOG.md` §3 observation 5, Boot 2's Bank 014 replacing
Boot 1's Bank 015) currently forces a full two-cycle regeneration rather
than being recognized as an intentional transition. The reader's root-level
case (`AUTOSAVE_READ_PLAN.md §3`) sits directly on top of this validator —
worth resolving the semantic question explicitly as part of, not after,
the reader's root-level-case implementation step.

---

## 12. AutoSave boot reader readiness (`AUTOSAVE_READ_PLAN.md`)

The plan is at **Rev 2, pre-implementation review** — not started. It has
one explicit open question blocking implementation start:

- **§13 Question A** — cutover mechanics for the existing dev-card
  `.hcnames` once Instrument rows gain a third `type` field. Two options on
  the table: (a) delete `/.hcnames` and let the bootstrap writer
  regenerate it (zero new code, but the bootstrap writer currently emits
  **blank** Scene/Kit/Instrument rows, so current names would be lost), or
  (b) a small forced one-time rewrite path that keeps existing
  names/sources and derives type from current resident state. This needs
  an answer before implementation step 3 (`§14`, `.hcnames` format
  extension) can start.

Everything else in the plan (`§14` implementation order, 7 steps) is
internally consistent and appropriately sequenced — starts with the
latch/notice mechanism against the whole-Bank fallback path specifically
because it's testable without the larger apply-side reader existing yet.
Two things from this list are **explicit prerequisites** the plan already
depends on and that this session confirmed are still open:

- The boot-fallback deferred-mark scope (§10 above / `SCOPING_TARGETS.md`'s
  "Deferred boot-fallback scope," `SCOPING_TARGETS.md:355-372`) is new code
  the plan's §7/§14 step 1 builds directly on top of.
- The `main.c` boot-order reorder (`AUTOSAVE_READ_PLAN.md §3`, "candidate
  validation before the Bank/Scene/Kit scan-and-load ladder") is explicitly
  flagged in the plan itself as needing to be "treat[ed] as its own
  reviewable unit given this project's boot-hang history" (`§11.3`) — this
  is the same boot sequence the Kit-sanitizer refactor (§8c above) also
  touches. Sequencing both boot-order changes in the same session, with one
  combined hardware test, is lower-risk than landing them separately.

---

## 13. Dev logging / trace / diagnostic system

- **Current flag state confirmed:** `DEV_MODE_LOGGING=1`, `DEV_MODE_DIAGNOSTIC=0`,
  `DEV_LOGGING_IWDG=0` (`config.h`). `DEV_LOGGING_IWDG` remains disabled and
  unverified on hardware per its own documented history
  (`SCOPING_TARGETS.md:515-519`) — no change, just re-confirmed current.
- **No trace/boot-log files exist on the current card** (`SD_CARD/` has no
  `*.bin` at all) despite logging being on. This is why §9a above can't be
  root-caused right now — there's no lifecycle trace to check what wrote
  Bank 15/`"LoadTst!"` into `settings.cfg`/AutoSave without also updating
  HCNAMES row 0. Recommend a fresh logging-enabled boot+Bank-Load capture
  as the first concrete step of investigating §9a, before assuming a root
  cause.
- **`bootlog.bin`/`asavetrc.bin` duplicate-name limitation — needs a
  re-verification pass, not a re-fix.** `DEV_MODES.md:299-325` documents
  this as a still-open gap: the two log writers don't implement the same
  scan-first, distinguish-zero/one/multiple, create-only-after-proof
  discipline that `.hcprms1/2` and `.hcnames` do. But the **specific
  mechanism** that caused observed duplicates — `afatfs_createFileContinue()`
  exiting its directory scan early on the first viable free run
  (`056_SESSION_HANDOFF_LOG.md` §1) — was a shared low-level AsyncFATFS bug
  used by *every* LFN-capable creator, fixed and hardware-verified in
  Session 056 (commit `509115b`). It's plausible this incidentally closes
  or substantially reduces the exposure `DEV_MODES.md` still describes as
  fully open, since the writers' remaining gap (no full scan-then-create
  discipline of their own) was never the only path to a duplicate — the
  early-exit bug was the concrete trigger recorded in that section. Worth
  a deliberate check (does a deleted-entry-then-recreate sequence on
  `bootlog.bin`/`asavetrc.bin` still duplicate, post-509115b?) and an
  update to `DEV_MODES.md`'s wording either way, so the doc doesn't
  contradict a fix that already landed.
- **Top-level Load/Save entry trace gap** — see §9b above; same finding,
  cross-referenced here since it's owned by `DEV_MODES.md`.
- **`AUTOSAVE_TRACE_RECORD_COUNT` reversion** — see §7 above.

---

## 14. Minor / no action needed

- Commit `509115b` ("lfn duplicate safe and test") incidentally carried a
  large accidental macOS Spotlight-index blob
  (`SD_CARD_A/.Spotlight-V100/Store-V2/.../*.indexArrays` etc.) into the
  repo, apparently from a broad `git add`. It was already removed from the
  tree in the subsequent "doc cleanup" commit and `SD_CARD_A/` no longer
  exists on disk or in `git ls-files`. No working-tree action needed; the
  blobs remain in git history only. Flagging only so it isn't mistaken for
  a currently-tracked file if repo size is ever investigated.

---

## Suggested order for the wrap session

1. Hardware-verify the Session 056 page-exit expedite (§1) — re-check even
   if believed already tested.
2. Close P1 (§2, one line).
3. Work through `S057_SCENE_OVERWRITE_SAFE.md` (§3) — depends on P1.
4. Implement `S057_SETTINGS_WRITE_SAFE.md` (§4) — independent of the above,
   can be done in parallel/either order.
5. Decide P2 (§5, accept-or-gate) — fully diagnosed, no further
   investigation needed.
6. Initial hardware investigation of the sequencer chaselight glitch (§6)
   using the diagnostic-overlay approach described there.
7. Decide the `AUTOSAVE_TRACE_RECORD_COUNT` reversion (§7) — pure
   housekeeping, do whenever convenient.
8. Capture a fresh logging-enabled boot + Bank Load on real hardware and
   check it against §9a's HCNAMES/settings.cfg/AutoSave discrepancy before
   assuming a root cause; while that trace exists, also check whether
   `bootlog.bin`/`asavetrc.bin` still duplicate post-509115b (§13) and
   update `DEV_MODES.md` accordingly.
9. Implement the already-scoped boot-sanitizer refactor (§8c) — remove
   full-content Kit validation from boot, canonicalize-and-index instead.
10. Do the `main.c` boot-order reorder for AutoSave's root-level case
    (§12) in the same pass as #9, since both touch the same boot sequence —
    one combined hardware test instead of two.
11. Resolve `AUTOSAVE_READ_PLAN.md §13 Question A` (HCNAMES cutover
    mechanics) and the live-Bank-match validator semantic question (§11) —
    both are decisions, not investigation.
12. Only then start the reader's implementation order (`AUTOSAVE_READ_PLAN.md §14`).
