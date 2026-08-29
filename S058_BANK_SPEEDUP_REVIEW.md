# Session 058 Option 1 implementation review

## Targeted implementation follow-up

Work authorized after this review is deliberately limited to four areas:

1. route AutoSave initial-record creation/recovery to the dedicated HCNAMES
   mirror;
2. restore root before Bank Load continues after a failed delegated Scene,
   including failures reached while CWD is still inside the embedded Kit;
3. reuse a valid HCNAMES mirror for Bank Save while preserving invalid-on-write
   and reload-after-failure coherency; and
4. bypass the post-Save physical Bank scan only under proved cache/namespace
   preconditions, retaining the existing scan-and-index fallback otherwise.

Implementation notes and verification results will be appended below; no
other review finding or Option 2/3 behavior is in scope for this follow-up.

Progress note 1:

- The four AutoSave initial/recovery CRC and formatting consumers now read
  `hcnames_name_mirror`; CRC input and emitted name bytes can no longer come
  from an unrelated shared `.hcindex` cache.
- Delegated Bank Scene Load now retains the Bank parent only for a successful
  child. Any failed child restores root first, including failures reached from
  inside the embedded Kit, so the existing failed-mask/reopen loop has the CWD
  it requires.

Progress note 2:

- Bank Save now skips its physical HCNAMES preload when the dedicated mirror is
  valid for the current mount; an invalid mirror still takes the complete
  reload path.
- Bank Load keeps its preload private/invalid while overlaying selected child
  rows. Both Bank-owned HCNAMES writers explicitly invalidate before their
  write-capable open. The existing validity byte is now tri-state: writer close
  arms publication, and only the facade's final `afatfs_sync()` promotes it to
  valid. Failed or partial writes/final syncs force a later physical reload.

Progress note 3:

- Bank Save's existing root preflight byte now records a saturated target-slot
  count plus an ambiguity bit for duplicate, malformed, or failed scans; no new
  retained RAM was added.
- A direct `.hcindex` selector is admitted only when the shared cache is still
  a complete Bank cache and preflight proves either a new slot or one exact,
  case-aware parsed display row with no rename. Every uncertain case keeps the
  physical `/Bank/` scan fallback. The direct path skips only the scan and still
  uses the complete index writer, close, sync, error, and callback chain.

Progress note 4:

- A final clean rebuild completes successfully at `text=380,100`, `data=404`,
  `bss=96,152`. BSS is unchanged from the pre-follow-up clean build, confirming
  that the added mirror publication state and Save eligibility evidence reuse
  existing retained bytes rather than increasing SRAM use.
- `git diff --check` passes. Static call-site review confirms that all four
  AutoSave initial-record creation/recovery consumers now use the dedicated
  HCNAMES mirror, and that every mirror reader requires the fully-valid state.

## Verdict

**Targeted follow-up implemented; hardware verification remains.** The two
high-severity findings and the requested 1C Save completions now have code and
clean-build verification. Finding 3's Bank-owned mirror-coherency gap was fixed
as a prerequisite to persistent reuse. Findings 4 and 5 remain open because
the user explicitly limited this follow-up to the named correctness and Save
speed changes.

## Findings

### 1. High: AutoSave creation and recovery still serialize the old shared name cache — resolved in follow-up

The HCNAMES reader now fills `hcnames_name_mirror`, and
`filesystem_prepareResidentNamesCache()` deliberately leaves
`fs_list_cache_name` untouched. Four AutoSave call sites nevertheless still
pass `fs_list_cache_name` as the authoritative 129-row HCNAMES image:

- `Core/Hardware/SD/filesystem.c:5650-5654`
- `Core/Hardware/SD/filesystem.c:5714-5718`
- `Core/Hardware/SD/filesystem.c:6781-6787`
- `Core/Hardware/SD/filesystem.c:6883-6889`

These are paired CRC/serialization sites for missing-record creation and the
neither-record-valid recovery path. After Option 1C, the shared cache may still
contain a Bank/Scene/Kit `.hcindex`, an Instrument index, or unrelated stale
rows. The generated AutoSave record will therefore contain the wrong names
while still receiving a CRC that validates those wrong bytes. This is silent,
durable corruption of AutoSave recovery identity rather than a cosmetic cache
problem.

All four calls must consume `hcnames_name_mirror`, so the CRC input and emitted
record input remain the same dedicated image that the immediately preceding
HCNAMES read populated.

Follow-up result: all four CRC/format call sites now pass
`hcnames_name_mirror`, with adjacent comments preserving the CRC/emitted-byte
pairing contract. Hardware missing-record and neither-valid recovery fixtures
remain required.

### 2. High: a failed Bank child can be reported as back at the Bank while CWD is only back at the Scene — resolved in follow-up

Scene phase 72 applies the new `afatfs_chdirParent()` fast return whenever a
Bank payload is active (`filesystem.c:10609-10635`), regardless of whether the
child succeeded. That assumption is valid on the successful path: phase 33
already returned from the embedded Kit to the Scene before Pattern/Effects
processing. It is not valid for all failure paths.

For example, a malformed `kitset.kcg`, a missing Instrument member, or an
invalid Instrument file reaches phase 62 while CWD is still
`Bank/BBB/SS/Kit name/` (`filesystem.c:9523-9878`). Bank-local Kit failures
skip quarantine and go directly to phase 72 (`filesystem.c:10480-10505`). One
parent step then lands in `Bank/BBB/SS/`, but the implementation sets
`op_bank_cwd_at_parent = 1` as if it had landed in `Bank/BBB/`.

Consequences:

- if another selected child follows, phase 27/31 tries to open that sibling
  Scene from inside the failed Scene and terminates the Bank Load instead of
  honoring the existing one-bad-child/continue contract;
- if the failed child is last, Bank completion can advance to the HCNAMES
  writer with CWD still inside the Scene, so the relative `/.hcnames`-intended
  open is performed in the wrong directory.

The parent-retention optimization must be successful-child-only, as the
proposal states. Any child error should restore root and use the existing
reopen phases. A robust condition is the established payload success status,
not merely `current_op == LOAD_BANK && op_bank_payload_active`.

Follow-up result: Scene phase 72 now tests the completed payload status. Failed
delegated children restore root and clear the parent flag; only successful
children call `afatfs_chdirParent()` and admit the fast sibling loop. The
malformed embedded-Kit fixture still needs hardware execution.

### 3. Medium: the mirror is trusted during an uncommitted HCNAMES rewrite and remains trusted after write failure — resolved for Bank-owned writers

The proposal requires the mirror to be invalid while a rewrite is in flight,
to become valid only after the rewritten file closes successfully, and to
remain invalid after any failed transaction. The implementation instead marks
the mirror valid after the source read (for example
`filesystem.c:10847-10860` and `filesystem.c:14446-14457`) and does not clear
the flag before opening HCNAMES with `"w"` (`filesystem.c:11654-11721` and
`filesystem.c:15015-15078`).

The generic targeted updater already keeps the mirror invalid between its
source read and destination close; the regression is in the Bank-owned Load
and Save state machines, which explicitly publish validity after preload.

Keep the mirror available internally during Bank processing, but clear its
public validity gate immediately before each Bank-owned write-capable HCNAMES
open. Only the successful destination-close/flush boundary should set it
again. Every open, format, write, or close error must leave it clear and force
a later physical reload.

Follow-up result: Bank Load no longer publishes its preload before the targeted
rewrite. Bank Save may read a valid mirror while preparing child names, then
invalidates before overlay/write; both Bank writers defensively invalidate at
their write-capable open. Writer close sets a publish-pending state in the same
one-byte allocation, and only the shared final sync promotes it to valid.
Failure-injection verification remains required.

### 4. Medium: Bank Save masks `chdirParent()` failure instead of applying the proposal's fatal error contract

Scene Save phase 37 handles `AFATFS_OPERATION_FAILURE` by returning to root,
clearing `op_bank_cwd_at_parent`, and continuing through Bank phase 12
(`filesystem.c:15633-15647`). If more children remain, phases 13-19 reopen the
Bank; if none remain, the save proceeds to normal completion.

The proposal's resolved and stated binding decision says a Save-side
`chdirParent()` failure invalidates the whole Bank Save. Such a failure is a
structural CWD/on-card failure, not the ordinary asynchronous case (which is
already represented by `IN_PROGRESS`). Silently converting it into successful
fallback can report a Bank Save as complete after a filesystem structural
error. Route this branch through the Bank Save error path after best-effort
root restoration.

### 5. Process/documentation: the authoritative SRAM manifest was not updated

The implementation adds 1,311 bytes of named SRAM1 payload and the clean linked
build reports `bss=96,152`, up from the Session 057 `bss=94,848` baseline.
`Core/Hardware/SD/filesystem.h:43-51` contains a useful allocation summary, but
`knowledge_files/specification_reference/SRAM_MANIFEST.md` is unchanged and
still describes `fs_list_cache_name` as the shared library/HCNAMES cache.

The project policy names `SRAM_MANIFEST.md` as the binding allocation record,
and the proposal explicitly requires it to be updated before implementation.
Regenerate its linked totals/symbol inventory and record the 1A-1D owners,
region, lifetime, exact sizes, and approval. The source assertion verifies
declared payload sizes; it does not replace the linked-image manifest.

## What looks correct

- 1A clears all 16 cached rows at request start, captures names in the original
  Bank scan, and uses `filesystem_displayPrecedesCached()` for the existing
  deterministic duplicate winner.
- The normal Bank Load path removes the per-child directory rescans, and the
  successful-child CWD fast path removes the repeated root/Bank reopen cycle.
- 1D retains the 16-byte per-call/tick consumption budget, coalesces reads, and
  resets its window at the common file-open callback. Newline read-ahead is
  retained for the next call on the same handle.
- The dedicated HCNAMES rows have the intended 129-by-9 layout, and the public
  resident-name accessors use the validity gate.
- The follow-up direct Bank `.hcindex` path retains the same complete writer and
  uses the physical scan whenever cache or namespace proof is uncertain.

## Verification performed

- `git diff --check` passes for the implementation files.
- `make clean` followed by `make -j4` completes successfully.
- Follow-up clean linked size: `text=380,100`, `data=404`, `bss=96,152`.
- The build emits only the repository's existing warnings; no warning points
  at the Option 1 changes.
- Linked symbols confirm the new allocations are in SRAM1 with sizes 1,161,
  144, 4, 1, and 1 bytes as documented.

No repository-hosted automated test suite covers these state machines, and no
hardware timing result was available for review.

## Bank Save speedup coverage

The observed result—Bank Load is materially faster while Bank Save is not
obviously faster—was consistent with the reviewed implementation. The targeted
follow-up now completes the two remaining 1C behaviors that can benefit Save,
without changing the dominant Scene-tree rewrite.

| Option 1 part | Bank Save status | Expected Save effect |
| --- | --- | --- |
| 1A: collect Bank-child names during one scan | Load-only by design | None. Bank Save derives child names from resident HCNAMES data. |
| 1B: retain selected Bank as CWD between Scenes | Implemented on the successful Save loop | Avoids phases 13-19 after each completed child: roughly fifteen repeated `/Bank/` plus selected-Bank reopen cycles for a full Save. |
| 1C: dedicated HCNAMES mirror | Completed in targeted follow-up | A valid mirror skips the Save preload; an exact retained Bank cache skips the physical post-Save root scan. Invalid/uncertain cases retain both fallbacks. |
| 1D: buffered text reader | Applied to the Save's HCNAMES preload | Small CPU/call-overhead reduction only. The Scene payload path writes text and already uses block writes. |

### Completed 1C work that applies to Bank Save

**Persistent HCNAMES mirror reuse is now implemented.**
The proposal says to fill the mirror on the first valid read after mount and to
fall back to a physical read when it is invalid. That makes the validity byte a
mounted-session cache authority. Bank Save now branches around its HCNAMES
preload when the mirror is valid. Invalid mirrors still clear partial rows and
take the complete physical reader. Before either Bank-owned HCNAMES rewrite,
validity is cleared; writer close only arms publication, and final sync
republishes it.

**The direct Bank-index update is now implemented with the conservative
fallback retained.** The existing Bank-root preflight byte now records a
saturated same-slot match count and an ambiguity bit, without adding RAM. The
direct path requires a complete Bank cache plus either a proved-new slot or one
case-aware matching parsed display row. Duplicate/malformed/failed scans, any
rename attempt, case-only mismatch, or cache-domain loss select the original
physical scan. Even on the fast path, only the scan is skipped: the complete
`.hcindex` writer, close, final sync, error propagation, and parked callback
remain.

More importantly, Option 1 does not change the dominant Save work. Every
selected child still goes through `filesystem_deleteBankChildSlotDirectoryStart()`
and then the Scene writer recreates its ten-file tree
(`filesystem.c:14863-14962`). A full Bank therefore still deletes and rewrites
the 16 Scene subtrees and their 160 Scene files, plus `bankset.bcg`; cluster
allocation/truncation, directory metadata, close, and flush traffic remain.
Avoiding approximately 15 parent reopen cycles is real but small beside that
work, so it may not be perceptible against the roughly 125–135 second baseline.

Accordingly:

- **1B was applied to the normal Bank Save child loop.** Its phase-count gain
  should be verifiable even if the wall-clock gain is small.
- **The complete proposed Save-side 1C optimization is now present with
  fallbacks.** Fast-path admission must be confirmed on ordinary exact-name
  overwrites, and fallback behavior must be exercised separately.
- **A large Bank Save improvement was never expected from Option 1 alone.**
  The proposal assigns the dominant overwrite reduction to Option 3's
  retained-cluster rewrite, while Option 2 helps only repeated same-session
  saves by skipping card-verified clean Scenes.

For hardware verification, trace or instrument the successful Save loop and
confirm that after the first child phases 13-19 are never entered. If that is
true, the implemented Save optimization is active and the small measured gain
is workload economics, not a missed 1B branch. Separately time the final
HCNAMES/index chain to quantify the completed 1C contribution.

## Required focused tests after correction

1. Remove one AutoSave record and verify boot creation preserves all 129
   HCNAMES rows; then invalidate both records and verify the recovery path does
   the same. Validate record CRCs and compare the embedded name block.
2. Load a multi-child Bank with a malformed embedded Kit/Instrument in a
   non-final selected child. Verify later children still load, the failed mask
   is correct, CWD returns to root at completion, and no nested `.hcnames` is
   created.
3. Repeat with the malformed child as the last selected child.
4. Inject/fault a HCNAMES destination open/write failure and verify the mirror
   remains invalid until a later physical read succeeds; then verify a valid
   mirror causes the next Bank Save to skip phases 80-82.
5. Inject Save-side `chdirParent()` failure and verify the Bank Save ends in
   error after root cleanup rather than continuing.
6. Verify an exact-name Bank overwrite takes the direct index-writer selector
   without a post-Save root scan. Separately prove duplicate, malformed/failed
   scan, rename/case mismatch, and non-Bank-cache cases take the physical scan
   fallback and publish the correct `.hcindex`.
7. On valid media, confirm a full 16-Scene Load performs one Bank child scan,
   zero per-child rescans, and zero root/Bank reopen pairs after the first
   child; then measure the proposal's timing gates.
