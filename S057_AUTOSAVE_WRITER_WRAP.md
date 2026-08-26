# Session 057 — Pre-Reader Assessment

Status: started as assessment-only, now with most of the punch list
resolved or landed. Goal: read back through the logs, `SCOPING_TARGETS.md`,
and the current test card to produce a punch list of what needs to be
wrapped/bugfixed in load/save, HCNAMES, `.hcindex`, AutoSave, and the dev
logging/trace/diagnostic systems before starting the AutoSave boot reader
(`AUTOSAVE_READ_PLAN.md`). P1, P2, the trace-count reversion, and the two
open design questions (§11, §12) are now resolved — see "Suggested order"
at the bottom for what's actually left. Three companion plans spun out of
this session's work: `S057_SETTINGS_WRITE_SAFE.md` (settings.cfg safe-write
redesign, done and tested), `S057_SCENE_OVERWRITE_SAFE.md` (empty-Scene
overwrite guard, code landed, needs testing), and
`S057_BOOT_KIT_SANITIZE_REFACTOR.md` (boot Kit-directory sanitizer
refactor, the last remaining code-change item before the reader).

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

## 2. P1 — Bank Save present-mask overwrite — done

**Done.** `filesystem.c:14126` now unions instead of overwriting:

```c
bank_setScenePresentMask((uint16_t)(bank_scenePresentMask() |
                                     op_bank_scene_save_mask));
```

Previously the caller-supplied **subset** mask was assigned directly,
dropping any resident Scene not included in the current save's mask from
`scene_present_mask` — the exact bug and one-line fix already recorded in
`SCOPING_TARGETS.md:249-287` and `AUTOSAVE.md:106-110`. Bank Load already
did the union correctly (`filesystem.c:10753`); Save was the one
inconsistent writer, now fixed to match.

**Why this mattered beyond bookkeeping:** the SEQ LED toggle surface for
`Save:[Bank]` (`menu_loadSaveSelectableSceneMask()` →
`menu_residentPresentSceneMask()` → `bank_scenePresentMask()`,
`menu.c:4745-4765`) gates both which LEDs can light (`menu.c:4856`) and
which button presses are allowed to toggle a bit at all (`menu.c:4907`)
directly off this same present mask. With the union fix landed, a Scene
that's still genuinely resident in SRAM no longer becomes untoggleable
after a prior partial Bank Save. This also un-blocks
`S057_SCENE_OVERWRITE_SAFE.md`'s guard, whose empty-Scene check leans on
`bank_scenePresentMask()` as its "has real content" signal — that guard's
code was already implemented ahead of this fix (§3 below); it's now safe
to test.

---

## 3. Scene/Bank empty-overwrite guard — implemented, see `S057_SCENE_OVERWRITE_SAFE.md`

**Done.** Two guards in `filesystem.c`, both checking
`bank_hasResidentBank()` (boot-seed safety) and `bank_scenePresentMask()`/
`bank_scenePresent()` (per-Scene emptiness):

- `filesystem_requestSaveBank()` (`filesystem.c:22052-22055`): filters
  `bank_scene_save_mask` against the present mask before the state machine
  starts — empty Scenes are silently excluded from the child-write loop.
- `filesystem_saveSceneDirectory_tick()` case 0 (`filesystem.c:14298-14302`):
  refuses with `FS_STATUS_ERROR` if the source Scene is not present —
  defense-in-depth for root Scene Save.

Both depend on P1 (§2) for the present mask to be trustworthy. Full design
and test plan in the companion document.

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

## 5. P2 — redundant settings write on every boot — resolved

**Decision: accept as-is, no gating.** Three unconditional
`filesystem_markSettingsDirty()` calls (`filesystem.c:10645, 10800, 14042`)
still produce one value-idempotent `settings.cfg` rewrite on every boot with
a valid Bank (`SCOPING_TARGETS.md:289-317`). Now that the write itself is
safe and tested (§4), the extra idempotent rewrite is harmless — and it
keeps its real upside (reconciling `settings.cfg` if boot fell back to a
different Bank slot). No gating on `fs_settings_runtime_ready` needed.

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

## 7. `AUTOSAVE_TRACE_RECORD_COUNT` reversion — deferred

**Decision: defer.** Still 2,048 (`config.h:330`, "TEMPORARY approved
expansion"), normal default 64. `SCOPING_TARGETS.md:478-482` already flags
this as needing an explicit keep/revert decision now that the pinned
recursive-delete target is hardware-confirmed closed — but the expanded
trace window is useful headroom while the AutoSave reader lands. Revisit
after the reader is fully implemented and tested, not before.

---

## 8. Boot Kit-directory sanitizer — diagnosed here, refactor plan in `S057_BOOT_KIT_SANITIZE_REFACTOR.md`

The remaining real code-change item before the AutoSave reader. This
section documents the diagnosis; `S057_BOOT_KIT_SANITIZE_REFACTOR.md` is
the companion implementation plan.

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

## 11. ~~Open design question already flagged in `AUTOSAVE.md`~~ — resolved

**Done.** The writer's validation phase (`filesystem.c:5880-5893`) now
distinguishes "no winner" (→ case 30, regeneration) from "winner exists
but Bank doesn't match" (→ case 50, transformed copy-forward with all live
bytes marked dirty via `autosave_markResidentBankDirty()`). A legitimate
Bank-session transition reuses the structurally valid peer and overwrites
its content in one drain cycle, rather than forcing a full two-cycle
regeneration. `autosave_streamValidationMatchesBank()` itself still returns
0 for mismatches, but the caller now uses `winner_bank_match` to separate
the two cases. `AUTOSAVE.md:262-269` is now stale and should be updated to
reflect the settled contract.

---

## 12. AutoSave boot reader readiness (`AUTOSAVE_READ_PLAN.md`)

The plan is at **Rev 2, pre-implementation review** — not started.

- **§13 Question A (HCNAMES cutover mechanics) — resolved.** Doesn't
  matter which option: go with (a), delete `/.hcnames` and let the
  bootstrap writer regenerate it. **Action item for whoever lands the
  `.hcnames` format expansion: notify the user to delete `/.hcnames` from
  the dev card at that point**, since the regenerated file will emit blank
  Scene/Kit/Instrument rows until re-populated.
- **`main.c` boot-order reorder — no independent decision needed.** Leave
  it as currently sequenced, or change it only as the boot Kit-sanitizer
  refactor (`S057_BOOT_KIT_SANITIZE_REFACTOR.md`) requires — that refactor
  touches the same boot sequence (§8c above). Re-examine explicitly again
  during the reader's own implementation step, since
  `AUTOSAVE_READ_PLAN.md §3` ("candidate validation before the
  Bank/Scene/Kit scan-and-load ladder") also touches boot order and the
  plan itself flags this as needing to be "treat[ed] as its own reviewable
  unit given this project's boot-hang history" (`§11.3`).

Everything else in the plan (`§14` implementation order, 7 steps) is
internally consistent and appropriately sequenced — starts with the
latch/notice mechanism against the whole-Bank fallback path specifically
because it's testable without the larger apply-side reader existing yet.
One explicit prerequisite remains: the boot-fallback deferred-mark scope
(§10 above / `SCOPING_TARGETS.md`'s "Deferred boot-fallback scope,"
`SCOPING_TARGETS.md:355-372`) is new code the plan's §7/§14 step 1 builds
directly on top of.

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

Status: P1 (§2), P2 (§5), the trace-count reversion (§7), the live-Bank-match
validator question (§11), and the HCNAMES cutover question (§12) are all
resolved. `S057_SCENE_OVERWRITE_SAFE.md` (§3) and `S057_SETTINGS_WRITE_SAFE.md`
(§4) have code landed. The only remaining code-change item before the
AutoSave reader is the boot Kit-sanitizer refactor; everything else left is
testing.

1. Hardware-verify the Session 056 page-exit expedite (§1) — re-check even
   if believed already tested.
2. Test `S057_SCENE_OVERWRITE_SAFE.md` (§3, its own §8 test plan) — now
   unblocked by P1.
3. `S057_SETTINGS_WRITE_SAFE.md` (§4) — already implemented and tested.
4. Implement the boot Kit-directory sanitizer refactor — see
   `S057_BOOT_KIT_SANITIZE_REFACTOR.md`. The last piece of real code work
   before the reader.
5. Once #1, #2, and #4 are done (and their hardware tests pass), there are
   no remaining code-change prerequisites — proceed to the reader's
   implementation order (`AUTOSAVE_READ_PLAN.md §14`), remembering to
   prompt for `/.hcnames` deletion at implementation step 3 (§12 above).

Not gating the reader, do opportunistically alongside or after: the
sequencer chaselight glitch investigation (§6), the HCNAMES/settings.cfg
discrepancy trace capture (§9a), and the bootlog/asavetrc duplicate
re-verification (§13).
