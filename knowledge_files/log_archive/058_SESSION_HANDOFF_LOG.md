# Session 058 — Handoff Log

```
DATE: 2026-08-29 to 2026-08-30 (code committed as 9da35c7
  "session 058 complete, fs speedups, document and planning in flight";
  doc trim + S059 plan as 124a6cf "s058 task logs complete". Branch
  dev-ph3-autosave-ph4. Session 057 closeout was ff7de56; a870189 deleted the
  057 disposable docs.)
SESSION GOAL: Investigate and implement the Bank Save/Load speedup planned in
  S058_BANK_LOAD_SPEEDUP_PROPOSAL.md, then follow the two review/check
  documents (S058_BANK_SPEEDUP_REVIEW.md, S058_LOAD_SAVE_NOPLAYBACK.md,
  S058_SPEEDUP_FINAL_CHECK.md) to their actual code + hardware conclusion.
COMPLETED: Option 1 (one-pass Bank child-name capture, parent-CWD retention,
  dedicated HCNAMES mirror, buffered text reader) implemented and hardware-
  confirmed faster Bank Load; Option 2 (session-scoped card-verified clean-
  Scene skip) implemented, hardware pending; Option 3 (retained-cluster
  rewrite) implemented then reverted as ~15 s slower; Option 3B rejected; the
  top-level stopped-playback Load/Save fast-drain + codec-suspend feature
  implemented; a stopped-Bank-Save livelock root-caused to SD-shim poll-count
  "timeouts" and fixed by switching both SD response waits to elapsed TIM6
  milliseconds (hardware-accepted full stopped Bank Save, slot 046); a Bank
  progress `NN.` repaint fix; and the AsyncFATFS directory-create inefficiency
  investigation deferred to Session 059 (S059_ASYNCFATFS_SPEEDUP.md).
VERIFIED ON HARDWARE:
  - Option 1 Bank Load speedup: YES — noticeably faster, no regressions
    (S058_BANK_LOAD_SPEEDUP_PROPOSAL.md "Option 1 implementation notes").
  - Option 2 clean-Scene skip: NO — build-verified only; fixtures listed in
    §5 remain pending.
  - SD real-time timeout fix / stopped Bank Save: YES — a full 16-Scene Bank
    Save with playback stopped completed in ~30 s and produced a byte-identical
    161-file/33-directory slot 046 tree (aggregate manifest hash
    214778579cd1a2d90e67a6e2f61bfc762eb34f19a0496b9071e73686a13b5c4c), with a
    mid-save START -> STOP transition also completing. This reverses the prior
    stopped-save livelock. (S058_LOAD_SAVE_NOPLAYBACK.md, final section.)
  - Bank progress repaint fix: NO (bounded one-image check described in
    S058_LOAD_SAVE_NOPLAYBACK.md "Build and acceptance").

CHANGES THIS SESSION (full file list in §14):
  Core/Bank/BankData.c/.h                   Option 2 clean-Scene authority
    (mask + slot + mutation-during-save mask) and its publish/invalidate/reset
    API.
  Core/Bank/Scene/SceneData.c                Scene/Kit scalar funnels call
    bank_invalidateSdCleanScene().
  Core/Bank/Scene/Pattern/PatternData.c      every Pattern mutator calls
    bank_invalidateSdCleanScene().
  Core/Bank/Scene/Preset/presetManager.c/.h  Instrument/KitMrp/InstrumentMrp/
    whole-instrument mutation funnels; Bank Load publish; preset_saveBank()
    gained a force_save parameter.
  Core/Hardware/SD/filesystem.c/.h           Option 1 (1A/1B/1C/1D), Option 2
    skip/publication, fast-drain selector/accessor + four-pass busy poll.
  Core/Hardware/SD/asyncfatfs/sdcard_lxr02.c/.h  SD response waits switched
    from poll-count to elapsed-millisecond deadlines; snapshot retry_count ->
    wait_ms.
  Core/Hardware/AudioCodecManager.c/.h       audioCodec_isSuspended() accessor.
  Core/Menu/menu.c                            no-playback scope predicate +
    enter/exit/reconcile lifecycle; menu_refreshBankChildProgress().
  main.c                                      renderer early return while codec
    suspended.
  config.h                                    HCPRMS_BOOT_CAPSULE_SCHEMA_VERSION
    1 -> 2.
  tools/decode_devlogs.py, tools/devlog_unpack.py  dual schema 1/2 E7 decoding.

KNOWN ISSUES INTRODUCED: None identified. Two review findings remain
  deliberately open (see §7): (4) Save-side afatfs_chdirParent() FAILURE at
  Scene Save phase 37 is still masked to a root-return fallback instead of the
  proposal's fatal Bank-Save error contract; (5) the SRAM manifest/interface
  closeout was deferred and is completed as part of this logging pass.

KNOWN ISSUES RESOLVED:
  - The stopped-playback Bank Save livelock: SD read-token/write-busy
    "timeouts" were raw sdcard_poll() call counts, which S058's codec suspend
    + four-pass fast drain accelerated to the point that a healthy busy card
    timed out every write and AsyncFATFS re-dirtied/retried the sector forever.
  - Bank Load's O(n^2) per-child directory rescan and the repeated root/Bank
    reopen cycles between children (Option 1).
  - HCNAMES reads destroying the 9,000-byte Bank/Kit/Scene index cache
    (dedicated mirror, Option 1C).
  - AutoSave initial-record creation/recovery reading stale shared-cache rows
    for HCNAMES (review Finding 1).
  - A failed Bank child being reported as back at the Bank while CWD was still
    inside the embedded Kit (review Finding 2).
  - The Bank Save/Load child counter (`00.`..`15.`) never repainting as the
    asynchronous filesystem cursor advanced.

NEXT SESSION RECOMMENDED GOAL: Hardware-verify Option 2's clean-Scene skip
  matrix (§5), then implement the separately-gated AsyncFATFS directory-create
  speedup documented in S059_ASYNCFATFS_SPEEDUP.md. Do not re-open Bank Load,
  Option 3, Option 3B, pack-file formats, compression, or hardware-SPI/DMA.
BLOCKERS: None. Option 2 and the repaint fix need hardware only. S059 work is
  self-contained and gated on Session 059.

CRITICAL REMINDERS FOR NEXT SESSION:
- An SD poll count is not a timeout. sdcard_lxr02.c's read-token and
  write-busy waits are now elapsed TIM6 milliseconds (1,000 ms / 5,000 ms);
  never reintroduce a poll-count ceiling there. Stopped playback and the
  four-pass fast drain are legitimate foreground-rate changes that a real-time
  deadline must survive.
- Option 2 clean authority is boot+mount volatile and never serialized. A
  cold boot, remount, or Autosave recovery must start with zero clean bits;
  only a successful Bank Load/Save on the current mount may publish them.
- Option 3 and Option 3B are closed. Any future Bank Save optimization must
  show a credible reduction in the first full Save after boot before it gets a
  plan.
- Bank Load is correct and out of scope for further speedup. Do not change its
  request, traversal, parsing, commit, completion, clean-authority, or UI
  paths.
- Finding 4 remains open: Scene Save phase 37 still converts an
  afatfs_chdirParent() FAILURE into a root-return fallback rather than failing
  the whole Bank Save. See §7 before touching that branch.
```

---

## 1. Session scope and companion-document map

Session 058 produced four disposable planning/review documents, all of which
the user intends to delete. Their durable content is folded into this log and
the specification references below:

| Document | Role | Durable home |
| --- | --- | --- |
| `S058_BANK_LOAD_SPEEDUP_PROPOSAL.md` | Baseline measurements, Options 1-6, implementation notes for Options 1/2/3 | §2-§6 |
| `S058_BANK_SPEEDUP_REVIEW.md` | Post-Option-1 static review (5 findings) + targeted follow-up | §4 |
| `S058_LOAD_SAVE_NOPLAYBACK.md` | Stopped-playback Load/Save feature, SD timeout root cause + fix, progress repaint | §8-§10 |
| `S058_SPEEDUP_FINAL_CHECK.md` | Final decisions, Option 3 revert + Option 3B rejection, S059 hand-off | §6-§7, §11 |

The new `S059_ASYNCFATFS_SPEEDUP.md` (created at 124a6cf) is the Session 059
target document and is **kept**, not deleted.

The verified current build, config flags, and symbol inventory are in §12.

## 2. Speedup proposal decisions (baseline and option disposition)

Baseline (Session 057 trace): approximately 7.7-8.3 s per Scene during Bank
Save, so a full 16-Scene payload is roughly 125-135 s before HCNAMES/index
maintenance on the tested card. A complete legacy Bank is 161 data files
(`1 + 16 x 10`) plus the Bank and 16 Scene / 16 embedded-Kit directories;
on the 32,768-byte-cluster test volume the small files alone consume ~5 MiB
of physical clusters, and the dominant Save cost is that per-file
metadata/open/create/allocation/flush traffic, not the text byte count.

Disposition:

- **Option 1** (legacy traversal + dedicated HCNAMES cache, 1,320-byte SRAM1
  reservation): implemented and kept. See §3.
- **Option 2** (session-scoped card-verified clean-Scene skip, "about eight
  bytes"): implemented and kept; actual cost 10 bytes (documented deviation).
  See §5.
- **Option 3** (in-place retained-cluster overwrite, ~160 extra bytes):
  implemented, measured ~15 s slower than the Option 2 baseline, and reverted.
  See §6.
- **Option 3B** (narrower same-session witness variant): rejected, not
  implemented. See §6.
- **Options 4-6** (pack-file formats; hardware-SPI/DMA) and the explicitly
  not-recommended items (more AsyncFATFS cache sectors, larger foreground
  burst/line budgets, whole-file pre-serialization, compression): all closed.

## 3. Option 1 — legacy traversal and dedicated HCNAMES cache (implemented)

Implemented 2026-08-29 in the recommended order 1A -> 1D -> 1C -> 1B. All four
parts remain in the current tree (verified against current source, not assumed
from the proposal notes).

### 3.1 1A — one-pass Bank child-name capture

- `op_bank_child_display[16][9]` (144 B) declared beside
  `op_bank_child_present_mask`; cleared with the operation-start reset.
- The existing Bank child scan (phases 15-17) now captures each slot's
  lexical-winning display name in the same loop that sets the presence bit,
  using the existing `filesystem_displayPrecedesCached()` duplicate winner
  (casefold-first, then raw case, via `fat_compareDisplayNameCasefoldThenCase()`).
- The old per-child rescan (phases 27-30) is removed. Phase 27 copies the
  cached row into `op_scene_display_name`, satisfies the phase-31 guards, and
  jumps directly to phase 31.

### 3.2 1B — retain the selected Bank as parent CWD

- `op_bank_cwd_at_parent` (1 B) records whether the last delegated child return
  left CWD at the selected Bank directory.
- Scene Load phase 72: for a Bank-delegated load, a **successful** child calls
  `afatfs_chdirParent()` (three-way `afatfsOperationStatus_e` handling); on
  SUCCESS it sets `op_bank_cwd_at_parent = 1`; on FAILURE it falls back to
  `afatfs_chdir(NULL)` and clears the flag. Standalone Scene Load keeps its
  root-return contract.
- Scene Save phase 37: same parent-return pattern for Bank-delegated saves;
  standalone Scene Save keeps its root-return contract.
- Bank Load phase 20 / Bank Save phase 12 choose the fast path (jump to phase
  27 / phase 20) when `op_bank_cwd_at_parent` is set, otherwise fall through to
  the existing root-reopen phases 21-26 / 13-19.

**Important correctness note (review Finding 2, resolved):** Scene Load phase
72 only retains the parent for a *successful* child. A failed child (including
one that failed while CWD was still inside `SS Name/Kit Name/`) restores root
unconditionally and clears the flag, so the sibling loop and the final HCNAMES
writer get the CWD they require.

### 3.3 1C — dedicated HCNAMES mirror

- `hcnames_name_mirror[129][9]` (1,161 B) + `hcnames_mirror_valid` (1 B),
  declared beside `fs_resident_source[]`.
- `filesystem_prepareResidentNamesCache()` no longer clears/retags the shared
  9,000-byte `fs_list_cache_name`; HCNAMES read/overlay paths write the mirror
  instead, so a normal Bank operation no longer destroys a resident
  Kit/Scene/Bank `.hcindex` cache.
- `filesystem_cachedResidentName()` and the three public accessors
  (`filesystem_residentInstrumentName/KitName/SceneName`) gate on
  `hcnames_mirror_valid` and read the mirror.
- The validity byte is **tri-state**: `FS_HCNAMES_MIRROR_INVALID`,
  `FS_HCNAMES_MIRROR_PUBLISH_PENDING`, `FS_HCNAMES_MIRROR_VALID`. A writer
  invalidates before its write-capable open; writer close arms publication;
  only the facade's final `afatfs_sync()` promotes to VALID. Failed/partial
  writes or syncs leave it invalid and force a later physical reload.
- `filesystem_initAfterCardReady()` invalidates the mirror on card mount.

### 3.4 1D — buffered text reader

- `text_buf_pos`/`text_buf_len` (two `uint16_t`, 4 B total) are module-scope
  cursors; `filesystem_resetTextReader()` is called from the common
  `on_file_opened` transition so every text-reading path (kitset, instrument,
  HCNAMES, settings, index) resets its window at file open.
- `filesystem_readTextLine()` refills the existing 512-byte `staging_buf` with
  one bounded block when its window is exhausted, preserving the unchanged
  16-character-per-tick parse budget; leftover bytes after a newline carry to
  the next tick.

### 3.5 Option 1 SRAM and result

| Allocation | Bytes | Part |
| --- | ---: | --- |
| `hcnames_name_mirror[129][9]` | 1,161 | 1C |
| `hcnames_mirror_valid` | 1 | 1C |
| `op_bank_child_display[16][9]` | 144 | 1A |
| `text_buf_pos` + `text_buf_len` | 4 | 1D |
| `op_bank_cwd_at_parent` | 1 | 1B |
| **Total** | **1,311** | within the 1,320-byte reservation |

Compile-time `_Static_assert`s verify each array size and the total.
Hardware result: Bank Load noticeably faster (O(n^2) rescan + root/Bank reopen
cycles removed); Bank Save only slightly faster, consistent with the proposal
(the dominant Save cost is the 161-file create/truncate metadata traffic).

## 4. Option 1 review and targeted follow-up

The static review (against commit `a870189`) found five issues. The user
scoped the follow-up to four areas and the review closed as "targeted follow-up
implemented; hardware verification remains":

1. **High — AutoSave still used the shared name cache (resolved).** Four
   paired CRC/serialization sites for missing-record creation and
   neither-valid recovery passed `fs_list_cache_name` as the HCNAMES image.
   All four now pass `hcnames_name_mirror`, so CRC input and emitted name bytes
   come from the same dedicated image. (Hardware missing/neither-valid
   recovery fixtures remain.)
2. **High — failed child CWD (resolved).** See §3.2: parent retention is
   successful-child-only; failed children restore root first.
3. **Medium — mirror trust during rewrite (resolved for Bank writers).**
   Bank Load/Save no longer publish the preload before the targeted rewrite;
   both writers invalidate at write-capable open and only the final sync
   promotes to VALID (tri-state validity, §3.3).
4. **Medium — Save-side `chdirParent()` failure masked (still open).** See §7.
5. **Process — SRAM manifest not updated (open at the time, closed by this
   logging pass).** The binding `SRAM_MANIFEST.md` is updated here (§15).

The follow-up also completed the two 1C behaviors that benefit Bank Save:

- **Persistent mirror reuse:** a valid mirror lets Bank Save skip its HCNAMES
  preload; an invalid mirror still takes the full physical read.
- **Direct `.hcindex` update with conservative fallback:** the existing
  Bank-root preflight byte now records a saturated same-slot match count plus
  an ambiguity bit (no new RAM). The direct selector is admitted only when the
  shared cache is still a complete Bank cache and preflight proves either a new
  slot or one exact case-aware parsed display row with no rename; every
  uncertain case keeps the physical `/Bank/` scan fallback. The fast path skips
  only the scan — the complete index writer, close, sync, error, and callback
  chain remain.

Follow-up clean build: `text=380,100`, `data=404`, `bss=96,152`.

## 5. Option 2 — session-scoped card-verified clean-Scene skip (implemented)

Implemented 2026-08-29 after Option 1 hardware confirmation.

Authority state:

- `Core/Bank/BankData.c`: `bank_scene_sd_clean_mask` (2 B), `bank_scene_sd_clean_slot`
  (2 B, `BANK_SD_CLEAN_SLOT_NONE = 0xffff` = no authority),
  `bank_sd_save_mutated_mask` (2 B operation-scoped mutation-during-save).
- `Core/Hardware/SD/filesystem.c`: `op_bank_sd_clean_candidate_mask` (2 B) and
  `op_bank_sd_clean_candidate_slot` (2 B). The candidate slot is retained
  because the deferred `.hcindex` rebuild re-enters `filesystem_start()` and
  clobbers `op_slot` before clean publication.

Lifecycle:

- `bank_init()` and `filesystem_initAfterCardReady()` clear all authority, so
  cold boot, remount, or reinsertion starts non-clean. Autosave recovery never
  publishes clean bits.
- `bank_invalidateSdCleanScene()` clears one persistent clean bit and also sets
  the matching mutation-during-save bit. Every retained-data mutation funnel
  calls it: Scene/Kit scalar setters (`SceneData.c`), Instrument endpoint store
  plus KitMrp/InstrumentMrp/whole-instrument commit (`presetManager.c`), and
  every Pattern mutator (`PatternData.c`). Equal-value no-ops are rejected
  before these funnels.
- Bank Load's commit is a direct struct copy that intentionally does **not**
  invalidate clean; `on_bank_load_complete()` publishes the completed effective
  child mask via `bank_publishSdCleanAuthority()`. Same-slot loads OR into
  existing bits; a different slot replaces the old authority.

Save skip and publication:

- `filesystem_requestSaveBank()` computes `skip_mask` only when the target slot
  equals the retained clean slot and the request is not a force-save; the
  effective child write mask is `full & ~skip`.
- `bank_resetSdSaveMutationWindow()` runs at request acceptance;
  `bank_resetSdSaveMutationScene()` clears one child's bit immediately before
  that Scene writer starts.
- `filesystem_completeLibraryIndexRebuild()` publishes candidates only after
  the whole operation (primary write, final sync, deferred `.hcindex` chain)
  succeeds, excluding any child whose mutation-during-save bit survived.
- `preset_saveBank()` / `filesystem_requestSaveBank()` gained a `force_save`
  selector; the current Menu caller passes `0u` (normal).

**Documented deviation:** the proposal's "about eight bytes" table omitted the
operation-scoped candidate-slot retention. Actual volatile cost is **10 bytes**
(6 B BankData + 4 B filesystem), not 8 B.

Option 2 build: `text=381,108 data=408 bss=96,160` per the proposal note
(net +8 B bss / +4 B data); see §12 for the final committed ELF figures, which
are authoritative.

Hardware verification is pending. Required fixtures: repeated same-Bank saves
in one mount (skips), cold boot before a save (no skips), remount in one power
cycle (no skips), root Scene/Kit/Instrument/Pattern mutation (bit clears), and
mutation while a Bank Save is in flight (child stays non-clean).

## 6. Option 3 (reverted) and Option 3B (rejected)

### 6.1 Option 3 — implemented, slower, reverted

The retained-cluster in-place Scene rewrite (`afatfs_finalizeRetainedSize()`,
the 11 x 13 alias table, and `filesystem_saveBankSceneInPlace_tick()`) was
fully removed after hardware testing showed a Bank Save roughly **15 s slower**
than the Option 2 baseline. The regression is consistent with the
canonical-tree proof frequently rejecting the existing child and paying for a
full Scene + embedded-Kit directory scan plus a root/Bank reopen cycle before
the ordinary delete/recreate path ran; that overhead dominated the intended
cluster-retention saving on the bit-banged SD path. The implementation also
omitted the proposal's Scene/Kit/Instrument rename handling, and all rejection
predicates were collapsed into one `op_bank_inplace_bad` byte, so the
historical run cannot identify which predicate rejected. The reverted
`afatfs_finalizeRetainedSize()` was also incomplete as a general primitive
(no writable-mode/first-cluster/one-cluster/cursor-size agreement/overflow
guards); it must not be restored independently.

Post-revert build matched the Option 2-only build (`text=381,108 data=408
bss=96,160`).

### 6.2 Option 3B — rejected

Option 3B would have witnessed, in volatile current-mount state, that this
firmware already created a given Bank Scene layout, then attempted a retained
rewrite only for a later dirty save of that same witnessed child with unchanged
names/types. It was rejected because its witness starts empty after every
boot/mount, so it saves **no time on the first full Bank Save after a fresh
boot** — the workflow that needs improvement. It only overlaps Option 2
(unchanged card-proven Scenes are already skipped) for the narrow remaining
dirty same-name/type case, and its authority/fingerprint/alias/failure matrix
is not justified. Do not implement.

## 7. Deferred / still-open items

- **Review Finding 4 (still open):** Scene Save phase 37 converts an
  `afatfs_chdirParent()` `FAILURE` into a root-return fallback and continues
  the Bank Save, rather than applying the proposal's binding fatal-error
  contract (fail the whole Bank Save after best-effort root cleanup). Verified
  present in current source at `filesystem.c` (Scene Save `case 37`). It was
  explicitly excluded from the targeted follow-up; decide it before touching
  that branch.
- **Option 2 hardware matrix** (§5) — build-verified only.
- **Bank progress repaint** — bounded one-image check (§10) pending.
- **AutoSave missing-record / neither-valid recovery hardware fixtures**
  (review Finding 1) — code is fixed, fixtures pending.
- **AsyncFATFS directory-create inefficiency** — moved to Session 059
  (`S059_ASYNCFATFS_SPEEDUP.md`). LFN creation scans past the FAT `0x00` end
  marker and retires the unused remainder of each newly created directory
  cluster, and new-directory initialization zero-writes every sector of the
  allocated cluster even when only one or two become visible. Both costs apply
  to any AsyncFATFS caller and are not Bank-specific.

## 8. Top-level Load/Save while playback is stopped (fast drain)

This feature (the `S058_LOAD_SAVE_NOPLAYBACK.md` work) has two coordinated
runtime effects, gated by the Menu-owned predicate
`menu_loadSaveCommandInNoPlaybackScope()`:

1. suspend codec hardware and stop foreground DSP while an eligible accepted
   command owns the Load/Save UI and transport is stopped;
2. call `afatfs_poll()` four times (`FS_FAST_DRAIN_POLL_PASSES = 4`) instead of
   once per busy `filesystem_tick()` pass.

Eligible: stopped top-level Save Kit / KitMrp / Scene / Bank and Load Scene /
Bank. Excluded: Load Kit / KitMrp, Settings/test types, Samples (which keeps
its own independent modal suspend/resume path), and nested Instrument
Load/Save (the Instrument-session guard also closes the `what == KIT` alias).

Filesystem pieces:

- `fs_fast_drain_active` (1 B, filesystem-owned, foreground-only, not
  reference-counted); `filesystem_setFastDrain()` / `filesystem_fastDrainActive()`.
- The BUSY/non-READY branch of `filesystem_tick()` performs four consecutive
  `afatfs_poll()` calls in fast mode, one otherwise; idle polling keeps its
  existing rate limit, and the facade operation `_tick()` switch remains once
  per foreground call.

Codec/menu/main pieces:

- `audioCodec_isSuspended()` exposes the pre-existing `audio_hw_suspended`
  byte (no new state).
- `menu_beginNoPlaybackStorage()` suspends the codec first and enables fast
  drain only after the accessor confirms suspension; `menu_endNoPlaybackStorage()`
  clears fast drain before resuming (so a foreign Samples suspension is never
  claimed or resumed). A dynamic reconciler at the top of
  `menu_pollPresetStatus()` lets STOP enter and START/CONTINUE leave the
  offline state without adding a transport latch.
- `main.c`'s `audio_check_and_render()` returns before queue/trigger/DSP/buffer
  work whenever the codec is suspended.

## 9. SD response-timeout root cause and fix (the closing hardware defect)

### 9.1 Symptom and on-card evidence

With S058's codec-offline/fast-drain state active, a stopped Bank Save never
completed (neither started-stopped nor STOP-during-running), while a
playing-only save still completed. Partial trees (`Bank/040`, `041`, `042 Full`
in `SD_CARD_BANK_NOPLAY_SAVE/`) left only `bankset.bcg` and, at most, the first
child's `sceneset.scg`, placing the failure in the Scene writer's
close/create/cache-flush boundary.

### 9.2 Root cause

`sdcard_lxr02.c`'s `SDCARD_TOKEN_TIMEOUT` (5000) and `SDCARD_BUSY_TIMEOUT`
(50000) were **call-count ceilings**, incremented once per `sdcard_poll()`,
not timeouts. Suspending codec DMA/I2S + renderer and running four polls per
facade pass accelerated the poll rate by orders of magnitude, so a healthy
card's program-busy interval consumed the 50,000-count ceiling far sooner in
wall time. The write-timeout callback supplied a null buffer;
`afatfs_sdcardWriteComplete()` re-dirtied the cache sector, and every retry hit
the same shortened ceiling — DIRTY -> WRITING -> timeout -> DIRTY forever. The
facade stayed BUSY, Menu stayed on `...`, and later children never started.
The read-token count had the same latent unit bug.

### 9.3 Fix

Only the SD shim changed for this defect. `retry_count` was repurposed as
`wait_started_tick` (one `uint16_t`; no new SRAM). The two waits now use
elapsed TIM6 `time_sysTick` milliseconds:

```c
#define SDCARD_TOKEN_TIMEOUT_MS 1000u
#define SDCARD_BUSY_TIMEOUT_MS  5000u
static uint8_t sdcard_waitTimedOut(uint16_t timeout_ms) {
    return (uint8_t)((uint16_t)(time_sysTick - wait_started_tick) >= timeout_ms);
}
```

The timestamp is armed on accepted CMD17 (before `READING_WAIT_TOKEN`) and on
accepted write data-response (before `WRITING_WAIT_BUSY`) — command/payload/CRC
transmission does not consume the card's response allowance. Every timeout/
success branch clears the timestamp before invoking its callback so an
admitted successor owns its own deadline. No delay, pacing, burst-size, poll,
or callback change was made. The 16-byte `SDCARD_BURST_SIZE`, IRQ placement,
cache retry policy, Bank/Scene state machines, and stall detectors were left
unchanged. The write-rejected path needs no extra clear (it never armed a
wait).

### 9.4 Diagnostic ABI (HCPRMS schema 2)

The transport snapshot member `retry_count` was renamed `wait_ms` (same
`uint16_t` type/order/geometry). `sdcard_getTransportSnapshot()` returns
elapsed wait milliseconds only in `READING_WAIT_TOKEN` or `WRITING_WAIT_BUSY`,
else zero. `filesystem_hcprmsCapsuleFreeze()` packs it into the unchanged E7
bytes 5..6; `HCPRMS_BOOT_CAPSULE_SCHEMA_VERSION` went 1 -> 2 in `config.h`.
`tools/decode_devlogs.py` and `tools/devlog_unpack.py` both accept schemas 1/2
and label E7 as `retry_count` (schema 1) or `wait_ms` (schema 2), with raw
fallback for unknown versions.

### 9.5 Hardware acceptance

The elapsed-time correction is hardware-confirmed on the reporting card: a full
Bank Save with playback stopped completed in approximately 30 s, and a
START -> STOP -> continue transition during slot 046 also completed. The
supplied `SD_CARD_BANK_NOPLAY_SAVE/Bank/046 Full/` tree is complete: one valid
v2 `bankset.bcg`, 16 numbered children with per-Scene `sceneset.scg` +
`pattern.pat` + `effects.fx`, one embedded Kit per Scene, six nonempty
Instrument files per Kit, 33 directories / 161 files / zero empty files, and a
recursive comparison identical to completed slot 045 (aggregate manifest hash
above). Fault-injected 1,000/5,000 ms timeout tests and the broader
Load/Scene/exclusion matrix remain useful closeout coverage but are no longer
blockers to concluding the reported stopped-save livelock is fixed.

## 10. Bank progress repaint fix

The filesystem cursor and renderer were already correct:
`op_bank_child_cursor` advances at child selection,
`filesystem_bankChildCursor()` exposes `0..15` only during a Bank operation
(else `0xFF`), and `menu_paintLoadSaveConfirmation()` already formats it as
`00.`..`15.` in bottom-row cells 13-15. The missing boundary was display
invalidation: an asynchronous child transition does not itself request a Menu
repaint, so the LCD retained the first queued frame. Screensaver exit repainted
everything, which is why that revealed the current number.

Fix (`Core/Menu/menu.c` only):

- new private `menu_refreshBankChildProgress()` (after `menu_repaint()`);
- called near the start of `menu_pollPresetStatus()`, before any worker can
  return early.

The helper reuses existing state and allocates no byte: it requires an accepted
command on the physical Load/Save page, leaves an active screensaver as sole
LCD owner, defers to an already-pending `menu_lcdRefreshPending` frame, reads
`filesystem_bankChildCursor()`, compares the formatted `NN.` against
`currentDisplayBuffer[1][13..15]` (the frame last queued), and calls an
ordinary incremental `menu_repaint()` only when those cells differ. Unchanged
children cost no LCD traffic. Applies equally to Bank Save and Bank Load.

Build: +192 bytes text, no data/BSS change (see §12 for the final figures).
Hardware check pending: flash, run a multi-child Bank Save and Load, confirm
`00.`..`15.` advances without input, and that a screensaver exit shows the
current child and transitions continue.

## 11. Final decisions (S058_SPEEDUP_FINAL_CHECK.md)

Session 058 closes with these decisions unchanged:

- No further load/save speedup is implemented from the proposal or review.
- Bank Load works correctly and is explicitly out of scope; do not change its
  request, traversal, parsing, commit, completion, clean-authority, or UI paths.
- Option 3 remains reverted; Option 3B remains rejected.
- Do not alter the Bank schema, serializers, playback-running SD burst size, or
  filesystem ownership merely to pursue this speedup.
- The current stopped-playback four-poll drain is not a proven physical limit,
  but poll-count tuning is secondary to eliminating redundant directory I/O.
- No general AsyncFATFS source change is authorized by Session 058; implement
  and validate the separately-gated work in `S059_ASYNCFATFS_SPEEDUP.md`.

## 12. Verified current code state (authoritative build facts)

Measured directly from `build/lxr02.elf` at HEAD `124a6cf` (toolchain present;
`arm-none-eabi-size -A` / `arm-none-eabi-nm -S --size-sort`). No clean rebuild
was needed — the committed build is source-current for the changed files.

- `text=382,700`, `data=404`, `bss=96,160` (`dec=479,264`).
- Sections: `.text=369,768`, `.itcm=3,768`, `.dma_nocache=3,100`, `.data=404`,
  normal SRAM1 `.bss=89,488`, `.dtcm=8,708`, `.dtcmz=3,572`,
  `.devwdg_noinit=0`.
- `build/lxr02.bin` = 383,104 B; `build/LXRV2_lxr02.img` = 383,120 B.
- `config.h`: `DEV_MODE_DIAGNOSTIC=0`, `DEV_MODE_LOGGING=1`,
  `DEV_STALL_DETECTION=1`, `HCPRMS_BOOT_CAPSULE_SCHEMA_VERSION=2`,
  `DEV_LOGGING_IWDG=0`.

Session 058 retained symbols (all normal SRAM1):

| Symbol | Size | Where | Purpose |
| --- | ---: | --- | --- |
| `hcnames_name_mirror` | 1,161 B | BSS | Option 1C HCNAMES name mirror |
| `hcnames_mirror_valid` | 1 B | BSS | Option 1C validity gate |
| `op_bank_child_display` | 144 B | BSS | Option 1A child names |
| `text_buf_pos` + `text_buf_len` | 4 B | BSS | Option 1D reader cursors |
| `op_bank_cwd_at_parent` | 1 B | BSS | Option 1B parent flag |
| `bank_scene_sd_clean_mask` | 2 B | BSS | Option 2 clean mask |
| `bank_scene_sd_clean_slot` | 2 B | `.data` | Option 2 clean slot |
| `bank_sd_save_mutated_mask` | 2 B | BSS | Option 2 mutation window |
| `op_bank_sd_clean_candidate_mask` | 2 B | BSS | Option 2 save candidate |
| `op_bank_sd_clean_candidate_slot` | 2 B | `.data` | Option 2 candidate slot |
| `fs_fast_drain_active` | 1 B | BSS | fast-drain selector |
| `wait_started_tick` | 2 B | BSS | SD wait start (repurposed from `retry_count`, net 0) |
| `audio_hw_suspended` | 1 B | BSS | pre-existing; only an accessor added |

Net new retained RAM: Option 1 = 1,311 B, Option 2 = 10 B (6 BSS + 4 data),
fast drain = 1 B; the SD timeout change adds no net SRAM. Individual symbols
are exact; the linked `.data`/`.bss` aggregates can differ slightly from a
source-level sum due to alignment — the section totals above are the binding
linked-image record.

Note on intermediate doc figures: the Option 2 proposal note (`data=408`) and
the noplayback build notes were produced against a dirty, uncommitted worktree
at different points in the session and do **not** all match the final committed
state. The ELF figures above are authoritative.

## 13. AsyncFATFS / SD layer notes carried forward

- `AFATFS_MAX_OPEN_FILES` remains 5; the Session 057 handle-pool bump to 8 was
  disproven and reverted, and `afatfs_countOpenHandles()` remains a read-only
  diagnostic.
- `afatfs_chdirParent()` returns an `afatfsOperationStatus_e` enum, not a
  boolean; never test it with `if (!afatfs_chdirParent())`.
- `sdcard_getTransportSnapshot()` / `afatfs_getDiagnosticSnapshot()` remain
  logging-only observers for the boot `ASENSURE` capsule.

## 14. Files changed this session

Code (committed 9da35c7):

- `Core/Bank/BankData.c`, `Core/Bank/BankData.h` — Option 2 clean authority.
- `Core/Bank/Scene/SceneData.c` — Scene/Kit scalar funnel invalidation.
- `Core/Bank/Scene/Pattern/PatternData.c` — Pattern mutation invalidation.
- `Core/Bank/Scene/Preset/presetManager.c`, `presetManager.h` — mutation
  funnels, Bank Load publish, `preset_saveBank(..., force_save)`.
- `Core/Hardware/SD/filesystem.c`, `filesystem.h` — Option 1/2, fast drain.
- `Core/Hardware/SD/asyncfatfs/sdcard_lxr02.c`, `sdcard_lxr02.h` — SD real-time
  timeouts and `wait_ms` snapshot.
- `Core/Hardware/AudioCodecManager.c`, `AudioCodecManager.h` —
  `audioCodec_isSuspended()`.
- `Core/Menu/menu.c` — no-playback lifecycle + progress repaint.
- `main.c` — renderer suspended early return.
- `config.h` — HCPRMS schema version 2.
- `tools/decode_devlogs.py`, `tools/devlog_unpack.py` — dual schema 1/2 E7.

Documents (disposable, to be deleted after this logging pass):

- `S058_BANK_LOAD_SPEEDUP_PROPOSAL.md`
- `S058_BANK_SPEEDUP_REVIEW.md`
- `S058_LOAD_SAVE_NOPLAYBACK.md`
- `S058_SPEEDUP_FINAL_CHECK.md`

Kept (new Session 059 plan): `S059_ASYNCFATFS_SPEEDUP.md`.

## 15. Documentation updated as part of this logging pass

- `knowledge_files/log_archive/000_SESSION_INDEX.md` — Session 058 row + summary.
- `knowledge_files/log_archive/058_SESSION_HANDOFF_LOG.md` — this file.
- `knowledge_files/specification_reference/FILESYSTEM_SPEC.md` — Option 1/2,
  Bank Load one-scan/parent-retention, Bank Save clean-Scene skip, fast drain,
  SD real-time timeout boundary.
- `knowledge_files/specification_reference/ASYNCFATFS_REFERENCE.md` — SD
  response-wait elapsed-time rule and `wait_ms` snapshot field.
- `knowledge_files/specification_reference/DEV_MODES.md` — HCPRMS schema 2 E7
  `wait_ms` vs schema 1 `retry_count`.
- `knowledge_files/specification_reference/SRAM_MANIFEST.md` — Session 058
  allocations and final linked totals.
- `knowledge_files/specification_reference/MODULE_INTERCHANGE_SPEC.md` —
  `preset_saveBank` force-save, `audioCodec_isSuspended`, fast-drain facade,
  BankData clean-authority API.
- `MEMORY.md` — current working source + Session 058 volatile note.
