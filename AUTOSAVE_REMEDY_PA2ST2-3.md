# AUTOSAVE_REMEDY_PA2ST2-3.md — Remediation Plan for Part 2, Steps 2–3

## How to read this document

This document is the code-level remediation plan for exactly two steps of
`AUTOSAVE_PHASE2_PLAN.md`'s Part 2:

- **Step 2 — Re-run and close out the accepted Phase 1 matrix**
- **Step 3 — Settings/provenance gaps left open by 045**

It was produced by reading `MEMORY.md`, `AUTOSAVE_PHASE2_PLAN.md`,
`knowledge_files/log_archive/045_SESSION_HANDOFF_LOG.md`, and then doing a
direct source read of the current accepted baseline (commit `326a8a1`, the
state this repository snapshot matches) — specifically
`Core/Bank/Scene/Autosave.c/.h`, `Core/Hardware/SD/filesystem.c/.h`,
`Core/Bank/BankData.c`, `Core/Bank/Scene/SceneData.c`,
`Core/Bank/Scene/Preset/presetManager.c`, `Core/Menu/menu.c`, `config.h`, and
`main.c`. Every claim below about "already implemented" or "not yet
implemented" is grounded in that reading, with exact function names and file
locations, not inference from the planning documents alone.

**This document does not authorize a source change by itself**, exactly like
`AUTOSAVE_PHASE2_PLAN.md` states about itself. It is staging for Steps 2–3
only; it does not touch Step 4 (identity/completeness rules), Step 5
(whole-object hooks), or Step 6 (Load/Save exclusion).

**Dependency on Step 1**: `AUTOSAVE_PHASE2_PLAN.md` orders Step 1
(observability) before Step 2 and requires Step 2's checkpoint to be
evidenced by "a trace consistent with Step 1's expectations." Step 1 itself
is out of this document's scope. Where Step 2/3 checkpoints cannot be met
without *some* runtime observability, this document specifies the smallest
possible diagnostic surface needed to unblock them (§2.2, §3.4) — this is
infrastructure, not a duplicate of Step 1's full six-stage trace, and it
should be revisited/merged with Step 1's design rather than built twice.

---

## 0. Executive summary of the audit

Before proposing changes, the current source was checked against every
coordinate `AUTOSAVE_PHASE2_PLAN.md` Step 2 lists (Bank, Scene, Kit,
Instrument Normal, Instrument Morph, supplemental descriptor, MIDI
channel/note, generated Kit endpoints) and every provenance/policy boundary
Step 3 lists (root Scene Load/Save, partial Bank Load/Save, settings.cfg
rewrite, AutoSave OFF→ON).

**Finding: Phase 1 scalar dirty-hook coverage and the Step 3 provenance/policy
call sites are already code-complete in the accepted baseline.** This
document did not find a missing hook, a missing call site, or a logic error
in:

- `Core/Bank/Scene/Autosave.c`'s `autosave_getLivePayloadByte()` coordinate
  coverage (§2.1),
- `Core/Bank/BankData.c` / `Core/Bank/Scene/SceneData.c`'s change-aware
  scalar setters (§2.1),
- `Core/Bank/Scene/Preset/presetManager.c`'s four completion callbacks that
  call `preset_setSceneSourcesFromMask()` and `filesystem_markSettingsDirty()`
  only on `FS_STATUS_DONE` (§3.1),
- `Core/Hardware/SD/filesystem.c`'s settings writer, which reads
  `scene_sourceValue()` and `parameter_values[]` live at line-generation time
  rather than from a stale snapshot (§3.2), or
- the AutoSave OFF→ON scheduler (`filesystem_setAutosaveEnabled()`,
  `filesystem_autosaveWriterSchedule_tick()`), which already re-arms setup
  asynchronously at runtime rather than depending only on the boot-only
  blocking wrapper (§3.3).

This matches Session 045's own framing: the BLOCKERS section says these
paths were "never proven on hardware," not "known broken." The `§6.6`
regression ("settings.cfg retained stale content after Scene Load") belongs
to the **later, explicitly rejected Phase 2 branch** (§6 of the 045 handoff),
not to this accepted baseline — that branch touched `presetManager.c.failed`
and `filesystem.c.failed`, which are not compiled and must not be confused
with the current source.

**What this document therefore proposes is not new feature code.** It is:

1. A small, explicitly-disclosed observability surface that Steps 2 and 3
   need in order to produce the recorded evidence their checkpoints require
   (§2.2, §3.4) — without it, "record exact generation/CRC/mask-bit-count"
   can only be done by pulling the SD card and hex-dumping it by hand, which
   does not scale across the full Topic C matrix.
2. A precise, coordinate-by-coordinate execution plan for Step 2's hardware
   matrix (§2.3), reusing the audit table so no coordinate is silently
   skipped.
3. A **contingency map** for Step 3 (§3.5): since the audit found the
   provenance/policy code already correct on paper, the plan is "run the
   test; if and only if it fails, change exactly this function, for exactly
   this reason." This keeps the discipline Session 045's own postmortem
   demands — do not touch working code speculatively (root cause (b): "too
   many simultaneous architecture changes per hardware test").
4. A build-only verification task for the packaged-image size discrepancy
   (§3.6), which is not a `.c`/`.h` change at all.

---

## 1. RAM allocation disclosure (read before implementing §2.2/§3.4)

`MEMORY.md`'s RAM Allocation Approval Policy requires every new or enlarged
static allocation to state its exact byte count, memory region, lifetime,
and owner, and requires the user's explicit acknowledgement before
implementation — this applies to this session as much as any other. The two
new diagnostic structures proposed in §2.2 and §3.4 are new static
allocations. Both are collected here for one sign-off decision instead of
being scattered through the document.

| Proposed allocation | Bytes | Region | Lifetime | Owner |
|---|---:|---|---|---|
| `autosave_maskDirtyBitCount()` (§2.2.1) | 0 | — | — | `Autosave.c` — pure function, no new storage; iterates the existing 3,856-byte `autosave_dirty_mask` |
| `filesystem_autosave_diagnostic_t fs_autosave_diagnostic` (§2.2.2 / §3.4) | 16 | normal SRAM1 (not DTCM, not `.dma_nocache`) | process lifetime while `DEV_MODE_LOGGING == 1`; compiles out when `DEV_MODE_LOGGING == 0`; explicitly **not** part of the `fs_stage_workspace_t` union, see §2.2.2 rationale | `filesystem.c`, exposed read-only via `filesystem.h` |

Total new static allocation: **16 bytes** in a logging-enabled diagnostic build, SRAM1, owned by `filesystem.c`; it is zero bytes in the normal `DEV_MODE_LOGGING == 0` build.
This is far below delay-line/Pattern-reserved DTCM or SRAM1 headroom and is
not DMA-visible, but per policy it still requires explicit acknowledgement
before `filesystem.c` is edited. The user has approved the allocation subject
to the `DEV_MODE_LOGGING == 1` gate, but this document remains a later,
separate implementation step: do not implement its diagnostic snapshot during
the Step 1 work in `AUTOSAVE_REMEDY_PA2ST1.md`. Reassess whether it is still
needed, and whether Step 1 interfaces can be reused, after Step 1 hardware
testing.

---

## 2. Step 2 — Re-run and close out the accepted Phase 1 matrix

### 2.1 Coordinate-by-coordinate audit (no code change required here)

This is the "Bank, Scene, Kit, Instrument Normal, Instrument Morph,
supplemental descriptor, MIDI channel/note, generated Kit endpoints" list
from `AUTOSAVE_PHASE2_PLAN.md` Step 2.1, matched against the current source.

| Coordinate | Owner setter(s) | Change-aware guard | Dirty marker called | Live getter in `Autosave.c` |
|---|---|---|---|---|
| Bank restore slot | `BankData.c` (setter around line 156) | compares before store | `autosave_markBankFieldDirty(AUTOSAVE_BANK_FIELD_RESTORE_SLOT)` | `autosave_getLivePayloadByte()`, offset 0..1, `bank_restoreBankSlot()` |
| Bank display name | `BankData.c` (line 131) | compares before store | `AUTOSAVE_BANK_FIELD_DISPLAY_NAME` | offset 2..9, `bank_displayName()` |
| Bank Scene-present mask | `BankData.c` (line 183) | compares before store | `AUTOSAVE_BANK_FIELD_SCENE_PRESENT_MASK` | offset 10..11, `bank_scenePresentMask()` |
| Bank active Scene | `BankData.c` (lines 218, 249) | compares before store | `AUTOSAVE_BANK_FIELD_ACTIVE_SCENE` | offset 12, `bank_activeSceneSlot()` |
| Bank VOICE-edit mask | `BankData.c` (lines 222, 253, 274, 288, 320) | compares before store | `AUTOSAVE_BANK_FIELD_VOICE_EDIT_MASK` | offset 13..14, `bank_sceneMaskVoiceEdit()` |
| Scene parameters 0..39 (includes MIDI channel base at index 26 and MIDI note base at index 33 — `AUTOSAVE_SCENE_PARAM_MIDI_CHANNEL_BASE` / `_MIDI_NOTE_BASE` in `Autosave.h`) | `SceneData.c`'s per-field setters (e.g. `scene_setTrackMidiChannel()`), all routed through the shared `scene_storeParameterByte()` helper (SceneData.c, ~line 130) | `scene_storeParameterByte()` early-returns on `*storage == value` | `autosave_markSceneParameterDirty(scene_index, parameter_index)` | `autosave_getSceneParameter()`, called from the Scene-section branch of `autosave_getLivePayloadByte()` |
| Kit generated endpoints (slot6/track7 decay, normal + Morph) | `SceneData.c`'s `scene_storeKitParameterByte()` helper (~line 150) | same equal-value guard | `autosave_markKitParameterDirty(scene_index, parameter_index)` | direct `scene->kit.settings.slot6_track7_amp_envelope_decay` / `_morph_amp_envelope_decay` reads in `autosave_getLivePayloadByte()` |
| Instrument Normal endpoint (any type, any descriptor) | `presetManager.c`'s `preset_storeInstrumentEndpoint()` (~line 774), reached from `preset_setInstrumentParameter()` and the supplemental-descriptor path alike — one generic store for every current/future type | `if (*storage == value) return;` before write | `autosave_markInstrumentNormalParameterDirty(scene_index, slot, descriptor_index)` | descriptor-indexed read in the Instrument branch of `autosave_getLivePayloadByte()`, bounded by `entry->descriptor_count` |
| Instrument Morph endpoint | same `preset_storeInstrumentEndpoint()`, `image == INSTRUMENT_IMAGE_MORPH` branch | same guard | `autosave_markInstrumentMorphParameterDirty(...)` | same branch, additionally requires `INSTRUMENT_PARAM_FLAG_MORPHABLE` on the descriptor, matching the Instrument text writer's `[morph]` section rule |
| Supplemental serialized descriptor values | Same `preset_storeInstrumentEndpoint()` path — there is no separate "supplemental" store; supplemental values are ordinary descriptor cells reached through `preset_setInstrumentParameter()` | same guard | same as Normal/Morph above | same |

**Conclusion for §2.1**: every coordinate Step 2.1 asks the matrix to
exercise already has exactly one owner, one change-aware guard, and one
correctly-scoped dirty marker. There is no coordinate in the Step 2 matrix
that requires a new hook to be written. The matrix's job is to *exercise*
this code on hardware and record evidence, not to complete it.

This also means the edge cases in Step 2.2 are already handled by *design*,
not merely by accident, and the matrix should confirm that design holds
under real timing:

- **Identical-value writes must not dirty**: guaranteed by the `*storage ==
  value` early-return present in every one of the setters/stores listed
  above (`scene_storeParameterByte()`, `scene_storeKitParameterByte()`,
  `preset_storeInstrumentEndpoint()`, and the individual `BankData.c`
  setters). Nothing needs to change for this to be true; the matrix needs to
  *prove* it stays true under real menu/encoder input timing.
- **Repeated coalesced writes within one debounce window**: the writer only
  reads `autosave_maskHasDirty()` / drains via `autosave_maskBitTake()`
  (`Autosave.c` lines ~1264–1310) on its own 5,000 ms /
  `AUTOSAVE_WRITER_CONTINUATION_INTERVAL_MS` schedule
  (`filesystem_autosaveWriterSchedule_tick()`), so any number of same-cell
  edits inside one window collapse to the single latest stored value with
  one dirty bit — this is inherent to bit-per-byte marking, not something
  that needs new code.
- **Re-dirty during an active drain**: `autosave_maskBitTake()` clears the
  bit only inside its own IRQ-guarded critical section immediately before
  the value is captured (Autosave.c ~line 1283–1310); a producer that sets
  the same bit again after that critical section leaves it set for the next
  generation. This is the exact mechanism MEMORY.md and the 045 handoff both
  describe as already-verified Phase 1 behavior (§2.7 of the handoff); the
  matrix should re-confirm it, not re-implement it.
- **Clean-mask idle observation**: `filesystem_autosaveWriterSchedule_tick()`
  falls through before calling `filesystem_start()` when
  `!fs_autosave_recovery_pending && !autosave_maskHasDirty()`
  (filesystem.c ~line 18028) — no file is opened. Confirming *zero* hidden
  file I/O on a truly idle mask is a hardware observation task, not a code
  task, **provided §2.2's diagnostic exists to prove no `filesystem_start()`
  call fired** (see below — this is exactly why the observability gap
  matters even for a "no code needed" coordinate).

### 2.2 Required code additions: the minimum observability Step 2 needs

Everything in §2.1 is correct by inspection, but Step 2's checkpoint is not
"the code looks correct" — it is "record exact generation/CRC/mask-bit-count/
payload-offset before and after every run" and "confirm which stage did the
work, not just that the output file changed" (`AUTOSAVE_PHASE2_PLAN.md`,
Step 2.3, echoing the 045 handoff's root cause (c)). The current source has
**no public accessor for any of that data**:

- `Autosave.h` exposes `autosave_maskHasDirty()` (a boolean), but no bit
  *count*.
- `filesystem.h` exposes `filesystem_autosaveEnabled()` (the policy bit) but
  nothing about generation, CRC, probe, or the writer's transaction/lifecycle
  state.
- The one place generation/CRC/probe *do* exist at runtime,
  `op_autosave_writer` (a `#define` alias for `fs_stage_workspace.autosave_writer`,
  filesystem.c line ~740), is a **member of the `filesystem_stage_workspace_t`
  union** that Kit, Instrument, and Scene staging also occupy (filesystem.c
  ~lines 628–650). The instant any other typed load/save runs after an
  autosave transaction — which will usually be true by the time a human
  checks — that memory has been overwritten by unrelated staging data. There
  is currently no way to read "what generation/CRC did the last autosave
  write actually produce" after the fact, on-target, at all.

Two small, additive APIs close this gap. Both are read-only, both are
gated so they cost nothing when not needed, and both reuse names/patterns
already established in the module (matching MEMORY.md's project-wide
"General Process Reminders" requirement that new code document why it
exists, what it does, and its inputs/outputs/affiliates).

#### 2.2.1 `Autosave.h` / `Autosave.c` — dirty-bit population count

```c
/* Autosave.h, placed immediately after autosave_maskHasDirty()'s declaration */

/*
 * Count currently dirty payload bits without changing the canonical mask.
 *
 * Input: none. Output: the exact number of set bits across all 3,856 mask
 * bytes, 0..246,848 (AUTOSAVE_PAYLOAD_BYTES * 8). Why: autosave_maskHasDirty()
 * only reports presence, but Step 2/3 hardware evidence requires an exact
 * before/after bit count per run, and the matrix has no other way to obtain
 * one without pulling the SD card and parsing a raw record. This is an
 * unsynchronized snapshot with the same tolerance as autosave_maskHasDirty():
 * a concurrent producer may change the true count by a small amount during
 * the scan, which is acceptable for diagnostic use and must never be used to
 * gate writer admission or completion logic. Affiliates:
 * filesystem_autosaveWriterSchedule_tick() and
 * filesystem_autosaveWriterCompleted() diagnostic capture (filesystem.c).
 */
uint16_t autosave_maskDirtyBitCount(void);
```

```c
/* Autosave.c, placed immediately after autosave_maskHasDirty() */

uint16_t autosave_maskDirtyBitCount(void)
{
    uint16_t byte_index;
    uint16_t count = 0u;

    /*
     * Same unsynchronized byte-at-a-time scan as autosave_maskHasDirty(),
     * extended to a population count instead of an early-exit boolean. Each
     * individual uint8_t read is atomic on this target; the function does not
     * claim a consistent whole-mask snapshot under concurrent mutation, only
     * a bounded, safe, advisory count. See the Autosave.h contract comment.
     */
    for (byte_index = 0u; byte_index < AUTOSAVE_MASK_BYTES; byte_index++) {
        uint8_t byte = autosave_dirty_mask[byte_index];

        while (byte) {
            count = (uint16_t)(count + (byte & 1u));
            byte = (uint8_t)(byte >> 1u);
        }
    }
    return count;
}
```

Inputs/outputs/affiliates: no inputs; output is a scalar 0..246,848; the only
affiliate is the diagnostic snapshot in §2.2.2, which is the sole intended
caller. This function adds zero new storage (§1) and touches no existing
control flow — it is a read path appended beside an existing read path of
the same shape.

#### 2.2.2 `filesystem.h` / `filesystem.c` — retained autosave diagnostic snapshot

This is the 16-byte allocation disclosed in §1. It must live **outside**
`filesystem_stage_workspace_t` specifically because that union is reused by
Kit/Instrument/Scene staging the moment any other operation runs — the
snapshot needs to survive past the end of the autosave transaction that
produced it, which the union cannot guarantee (this is the exact mechanism
explained above).

```c
/* filesystem.h, placed near filesystem_autosaveEnabled() */

/*
 * Lifecycle-flag bits for filesystem_autosave_diagnostic_t.lifecycle_flags.
 * Each bit mirrors one existing private scheduler flag at the moment the
 * snapshot was captured; they are read-only copies, not a second owner.
 */
#define FS_AUTOSAVE_DIAG_ENABLED            0x01u /* fs_autosave_enabled */
#define FS_AUTOSAVE_DIAG_RUNTIME_READY      0x02u /* fs_autosave_runtime_ready */
#define FS_AUTOSAVE_DIAG_WRITER_BOOT_READY  0x04u /* fs_autosave_writer_boot_ready */
#define FS_AUTOSAVE_DIAG_TRANSACTION_ACTIVE 0x08u /* fs_autosave_transaction_active */
#define FS_AUTOSAVE_DIAG_DISCARD_PENDING    0x10u /* fs_autosave_discard_pending */
#define FS_AUTOSAVE_DIAG_SETUP_FAILED       0x20u /* fs_autosave_setup_failed */
#define FS_AUTOSAVE_DIAG_RECOVERY_PENDING   0x40u /* fs_autosave_recovery_pending */

typedef enum {
    FS_AUTOSAVE_DIAG_RESULT_NONE = 0,   /* no drain has completed this session */
    FS_AUTOSAVE_DIAG_RESULT_SUCCESS,
    FS_AUTOSAVE_DIAG_RESULT_ERROR
} fs_autosave_diag_result_t;

/*
 * One retained, non-union snapshot of the last completed (or last attempted)
 * autosave drain transaction, plus the current scheduler lifecycle flags.
 *
 * Inputs: filesystem_autosaveWriterSchedule_tick() captures dirty_bits_before
 * and the target generation/probe at transaction start;
 * filesystem_autosaveWriterCompleted() captures crc32c, winner_index,
 * dirty_bits_after, and result at transaction close. Output: a caller-owned
 * copy via filesystem_getAutosaveDiagnostic(); the retained struct itself is
 * private to filesystem.c. Why: op_autosave_writer lives inside
 * filesystem_stage_workspace_t, a union shared with Kit/Instrument/Scene
 * staging, so its fields are only valid strictly during the writer's own
 * operation; this struct is the durable copy Step 2/3 hardware evidence
 * needs after the fact. It performs no I/O and changes no scheduling
 * decision — it is observation-only, matching the 045 CRITICAL REMINDERS
 * constraint that diagnostics must not perturb the mechanism they describe.
 * Affiliates: Autosave.c's autosave_maskDirtyBitCount(),
 * filesystem_autosaveWriterSchedule_tick(), filesystem_autosaveWriterCompleted().
 */
typedef struct {
    uint32_t generation;
    uint32_t crc32c;
    uint16_t dirty_bits_before;
    uint16_t dirty_bits_after;
    uint8_t  probe_counter;
    uint8_t  winner_index;      /* 0 = record A (.hcprms1), 1 = record B (.hcprms2) */
    uint8_t  result;            /* fs_autosave_diag_result_t */
    uint8_t  lifecycle_flags;   /* FS_AUTOSAVE_DIAG_* bits, see above */
} filesystem_autosave_diagnostic_t;

/*
 * Copy the current autosave diagnostic snapshot for test/log use.
 *
 * Input: caller-owned destination. Output: the most recent captured values,
 * which persist until the next transaction start/completion overwrites them;
 * NULL is a no-op. This function never opens a file, polls asyncfatfs, or
 * changes any scheduler flag it reads. Affiliates: Step 2/3 hardware test
 * procedures and any later Step 1 trace consumer that wants a stable summary
 * in addition to per-stage events.
 */
void filesystem_getAutosaveDiagnostic(filesystem_autosave_diagnostic_t *out);
```

```c
/* filesystem.c: static storage, placed beside the existing fs_autosave_*
 * scheduler flags (~line 1328), NOT inside filesystem_stage_workspace_t. */
static filesystem_autosave_diagnostic_t fs_autosave_diagnostic;

/* filesystem.c: capture at transaction start, inside
 * filesystem_autosaveWriterSchedule_tick(), immediately around the existing
 * filesystem_start(FS_INTERNAL_OP_AUTOSAVE_PARAMETER_DRAIN, ...) call
 * (~line 18045). Only the two added lines are new; everything else in this
 * excerpt is existing code shown for placement context. */
    if (filesystem_start(FS_INTERNAL_OP_AUTOSAVE_PARAMETER_DRAIN,
                         FS_FILE_SETTINGS, 0u,
                         filesystem_autosaveWriterCompleted)) {
        fs_autosave_diagnostic.dirty_bits_before =
            autosave_maskDirtyBitCount();          /* NEW */
        fs_autosave_transaction_active = 1u;
    }

/* filesystem.c: capture at transaction completion, inside
 * filesystem_autosaveWriterCompleted() (~line 17847). Placed after the
 * existing generation/probe/CRC become final (the same values already
 * consumed there to decide re-arm/discard), before the union fields they
 * read could be reused by a later operation. */
static void filesystem_autosaveWriterCompleted(void)
{
    /* ... existing body that reads op_autosave_writer.winner_generation,
     * .winner_probe, .target_crc32c, .winner_index, and decides
     * fs_autosave_writer_armed / fs_autosave_recovery_pending / etc. stays
     * unchanged above this point ... */

    fs_autosave_diagnostic.generation     = op_autosave_writer.winner_generation;
    fs_autosave_diagnostic.probe_counter  = op_autosave_writer.winner_probe;
    fs_autosave_diagnostic.crc32c         = op_autosave_writer.target_crc32c;
    fs_autosave_diagnostic.winner_index   = op_autosave_writer.winner_index;
    fs_autosave_diagnostic.dirty_bits_after = autosave_maskDirtyBitCount();
    fs_autosave_diagnostic.result = (status == FS_STATUS_ERROR)
        ? FS_AUTOSAVE_DIAG_RESULT_ERROR
        : FS_AUTOSAVE_DIAG_RESULT_SUCCESS;         /* NEW, 5 lines above this comment */

    fs_autosave_transaction_active = 0u;
    /* ... existing body continues unchanged ... */
}

/* filesystem.c: public accessor, placed beside filesystem_autosaveEnabled() */
void filesystem_getAutosaveDiagnostic(filesystem_autosave_diagnostic_t *out)
{
    uint8_t flags = 0u;

    if (!out)
        return;
    *out = fs_autosave_diagnostic;
    if (fs_autosave_enabled)            flags |= FS_AUTOSAVE_DIAG_ENABLED;
    if (fs_autosave_runtime_ready)      flags |= FS_AUTOSAVE_DIAG_RUNTIME_READY;
    if (fs_autosave_writer_boot_ready)  flags |= FS_AUTOSAVE_DIAG_WRITER_BOOT_READY;
    if (fs_autosave_transaction_active) flags |= FS_AUTOSAVE_DIAG_TRANSACTION_ACTIVE;
    if (fs_autosave_discard_pending)    flags |= FS_AUTOSAVE_DIAG_DISCARD_PENDING;
    if (fs_autosave_setup_failed)       flags |= FS_AUTOSAVE_DIAG_SETUP_FAILED;
    if (fs_autosave_recovery_pending)   flags |= FS_AUTOSAVE_DIAG_RECOVERY_PENDING;
    out->lifecycle_flags = flags;
}
```

**Exact placement discipline** (this matters given root cause (b) — one
owner boundary per change): this touches only `filesystem.c`/`.h`. It does
not touch `Autosave.c`'s dirty-mask ownership, does not touch BankData or
SceneData, and does not touch any scheduling decision — every added line
either reads an existing value that was already computed for another
purpose, or writes into the new struct. `filesystem_getAutosaveDiagnostic()`
performs no I/O and must never be called from an ISR context (it is intended
for the main-loop test/log path only, same constraint as the rest of
`filesystem.c`'s public facade).

**What this buys Step 2 concretely**: a test procedure can now, at any point
between hardware actions, call `filesystem_getAutosaveDiagnostic()` and get
the exact generation/CRC/probe/winner/dirty-bit-count/result/lifecycle-state
the Step 2.3 checkpoint asks for, without needing the full Step 1 trace to
exist first, and without pulling the SD card. It is deliberately a coarser
tool than Step 1's six-stage trace (it cannot distinguish "producer never
dirtied" from "scheduler never admitted" the way Step 1's per-stage events
can) — it exists to make Step 2 executable now, and should be treated as a
subset of Step 1's eventual design, not a replacement for it.

### 2.3 Execution plan tying §2.1's table to §2.2's new APIs

For each row of the §2.1 table, in the order Step 2.1 lists (Bank, Scene,
Kit, Instrument Normal, Instrument Morph, supplemental descriptor, MIDI
channel/note, generated Kit endpoints):

1. Start from a known-clean record pair: call `filesystem_getAutosaveDiagnostic()`
   and confirm `dirty_bits_after == 0` and `result != FS_AUTOSAVE_DIAG_RESULT_ERROR`
   from the prior run, or wait for `autosave_maskHasDirty()` to go false (this
   getter already exists; no new code needed to observe it, only to act on it
   in a test script).
2. Perform exactly one edit at that coordinate through the normal Menu/UI
   path (not a direct struct write, so the test exercises the same code path
   production input takes).
3. Record `filesystem_getAutosaveDiagnostic()`'s `dirty_bits_before` the next
   time a transaction starts, and `dirty_bits_after` / `crc32c` / `generation`
   /`winner_index` when it completes.
4. Compare the winning record's payload byte at that coordinate's known wire
   offset (from `Autosave.h`'s offset constants, e.g.
   `AUTOSAVE_SCENE_PARAM_MIDI_CHANNEL_BASE`) against the edited value.
5. Repeat step 2 with the *same* value (identical-value case), and confirm no
   transaction starts at all (`dirty_bits_before` unchanged from the prior
   idle observation).

This satisfies Step 2.3's requirement ("record exact generation/CRC/mask-
bit-count/payload-offset before and after every run") using only the two
small additions in §2.2, with no change to Bank/Scene/Kit/Instrument/Preset
ownership.

---

## 3. Step 3 — Settings/provenance gaps left open by 045

### 3.1 Item 1 — Scene-source persistence after runtime Load/Save

Audit result: **already implemented for all four operations** Step 3.1 asks
to test independently.

| Operation | Completion callback | Source encoding call | Settings dirty call |
|---|---|---|---|
| Root Scene Load | `on_scene_load_complete()` (presetManager.c ~line 411) | `preset_setSceneSourcesFromMask(pm_kit_request_scene_mask, pm_request_slot, 0u)` — library encoding | `filesystem_markSettingsDirty()` |
| Root Scene Save | `on_scene_save_complete()` (~line 439) | `scene_setSourceLibrarySlot(pm_instrument_request_scene, pm_request_slot)` | `filesystem_markSettingsDirty()` |
| Bank Load (partial or full) | `on_bank_load_complete()` (~line 460) | `preset_setSceneSourcesFromMask(filesystem_lastBankLoadSceneMask(), pm_request_slot, 1u)` — **uses the actual loaded-child mask, not a fixed "all sixteen" mask** | `filesystem_markSettingsDirty()` |
| Bank Save (partial or full) | `on_bank_save_complete()` (~line 486) | `preset_setSceneSourcesFromMask(pm_kit_request_scene_mask, pm_request_slot, 1u)` — the retained *selected* mask, not a derived "all resident" mask | `filesystem_markSettingsDirty()` |

All four are additionally guarded by `if (filesystem_status() ==
FS_STATUS_DONE)`, so a failed operation updates neither source nor settings
dirty state — exactly what Step 3.1 and the 045 handoff's §3.2 require. The
Bank Load/Save cases specifically already avoid the two semantic errors
Session 045's root cause (a) and the "Do not repeat" list call out (treating
Bank name/slot as identity, and marking all sixteen Scenes for a partial
Bank operation) — `filesystem_lastBankLoadSceneMask()` and
`pm_kit_request_scene_mask` are the real bounded masks, not `0xffff`.

**No code change is proposed for this item.** The remaining work is
hardware execution: for each of the four rows, perform the operation with a
partial mask (not just Bank slot 0 / full 16-Scene Bank, which is the only
case 045 actually exercised per its handoff §4.3), and confirm via
`scene_sourceValue()` (already public, `SceneData.h`) and a `settings.cfg`
readback that only the intended Scene rows changed.

### 3.2 Item 2 — settings.cfg post-load rewrite

Audit result: the specific regression Session 045 §6.6 describes ("the file
retained its old timestamp/content") belongs to the **later failed branch**
(`presetManager.c.failed`), not this accepted baseline. In the current
source:

- `filesystem_nextGlobalSettingsLine()` (the per-line generator reached
  during the chunked settings write, filesystem.c ~line 10960) reads
  `scene_sourceValue(scene_index)` and `parameter_values[...]` **live, at
  the moment each line is streamed**, not from a value captured earlier in
  the operation. There is no snapshot to go stale.
- `filesystem_complete()`'s dirty-clear guard (filesystem.c ~line 2671) only
  clears `fs_settings_dirty` when `op_settings_change_revision ==
  fs_settings_change_revision` — i.e., only if no further change was queued
  after the write started. A Scene Load's `filesystem_markSettingsDirty()`
  call that lands *during* an in-flight settings write correctly leaves the
  file dirty for a follow-up write rather than being silently dropped.
- The single-owner AsyncFATFS facade (`current_op`) makes it structurally
  impossible for a Scene Load and a settings write to be "in flight"
  simultaneously in the first place — they cannot interleave the way a
  stale-snapshot bug would require.

**No code change is proposed for this item either**, on the current
evidence. This is explicitly flagged as the item most likely to still
surprise on real hardware, because Session 045 never isolated *why* the
failed branch saw stale content, and this document's static read cannot
rule out a timing issue that only appears under real SD latency (e.g., a
write that begins before `on_scene_load_complete()`'s
`filesystem_markSettingsDirty()` call executes, versus after). See §3.5 for
the exact contingency if hardware testing reproduces the symptom.

### 3.3 Item 3 — AutoSave OFF→ON full lifecycle

Audit result: **already implemented**, and already exercises the runtime
(non-boot) path, not just the blocking boot wrapper:

- `filesystem_setAutosaveEnabled(0)` (filesystem.c ~line 17578): revokes
  `autosave_setMutationTrackingEnabled(0)`, clears `fs_autosave_setup_pending`
  /`_setup_failed`/`_recovery_pending`/`_writer_armed`/`_writer_boot_ready`,
  and either discards the dirty mask immediately or defers that discard via
  `fs_autosave_discard_pending` if a transaction is mid-flight — matching the
  045 CRITICAL REMINDER "OFF/new-session transitions must not carry stale
  owner bits into a later enable, but clearing beneath an active CRC/copy is
  forbidden."
- Neither hidden file is opened or deleted by the OFF transition — confirmed
  by reading the function body: it only touches SRAM flags and (conditionally)
  calls `autosave_discardDirtyMask()`, which itself only does a `memset()`
  (Autosave.c ~line 856), never file I/O.
- `filesystem_setAutosaveEnabled(1)` at **runtime** (i.e. `fs_autosave_writer_boot_ready`
  already true from an earlier boot ensure, not the cold-boot case) sets
  `fs_autosave_setup_pending = 1u` when `fs_autosave_runtime_ready &&
  bank_hasResidentBank()`. `filesystem_autosaveWriterSchedule_tick()` then
  starts `FS_INTERNAL_OP_ENSURE_AUTOSAVE_FILES` **asynchronously** through the
  ordinary `filesystem_start()` path (filesystem.c ~line 18003) — this is not
  the boot-only blocking `filesystem_ensureAutosaveFilesBlocking()` wrapper,
  so a runtime re-enable does not need to reuse boot's synchronous call.

**No code change is proposed for this item.** The four sub-cases in Step 3.3
(OFF stops new setup/validation/drain without touching existing records; an
in-flight transaction reaches its own close boundary; ON re-arms cleanly at
runtime; hidden files are untouched throughout OFF) are each traceable to a
specific guard in the functions above. Hardware testing needs to confirm
they hold under real SD timing, particularly the deferred-discard path
(`fs_autosave_discard_pending`), which is the one sub-case that depends on
*when* OFF arrives relative to an in-flight write and is hardest to reason
about statically.

### 3.4 Additional diagnostic needed for Items 2 and 3

The same `filesystem_getAutosaveDiagnostic()` from §2.2.2 is the mechanism
Step 3.2 and 3.3 need too — the plan explicitly says to "diagnose this with
Step 1's tracing rather than re-guessing publication points" for item 2, and
the `fs_autosave_discard_pending` sub-case in item 3 is exactly the kind of
"which internal flag was set at the moment of interest" question the
snapshot answers. No further additions beyond §2.2.2 are proposed here —
this section exists only to record that Steps 2 and 3 share one
observability addition rather than needing two.

### 3.5 Contingency map — what to change *if* hardware testing fails

Per root cause (b), this is written as a decision table, not a proposed
edit, so that if a test in §3.2 or §3.3 fails, exactly one function is
touched per finding, matching the "change exactly one owner boundary per
hardware test" rule from `AUTOSAVE_PHASE2_PLAN.md` Step 5's closing
paragraph (the same discipline applies here even though this is Step 3, not
5).

| Symptom | Most likely cause given the code read | Function to change | Nothing else in this table should change at the same time |
|---|---|---|---|
| `settings.cfg` unchanged after a successful Scene Load | `on_scene_load_complete()`'s `filesystem_status() == FS_STATUS_DONE` guard is reading `status` before `filesystem_complete()` has actually transitioned it (ordering hazard between the generic completion and the typed callback) | `filesystem_complete()` (~line 2671) — verify `completion_callback` is invoked strictly after `status = final_status;`, which the current source already does; if not, this is the one line to reorder | Do not also touch `preset_setSceneSourcesFromMask()` or the writer scheduler in the same pass |
| `settings.cfg` contains the *previous* Scene's source instead of the new one | `filesystem_nextGlobalSettingsLine()`'s line generator is somehow being reached before `scene_setSourceLibrarySlot()`/`scene_setSourceBankSlot()` commits — i.e. the settings write was already streaming past line 17+scene_index when the source changed | `filesystem_markSettingsDirty()`'s revision-compare guard in `filesystem_complete()` (~line 2671) — confirm `op_settings_change_revision` is captured *after* `preset_setSceneSourcesFromMask()` runs, not before, in the request-acceptance path (`filesystem_start()`, ~line 14480) | Do not change the per-line generator; the bug would be in *when* the revision is captured, not in what the generator reads |
| `settings.cfg` writer never starts after Scene Load | `fs_settings_runtime_ready` was not yet 1 at boot-load time, or `menu_activePage` was LOAD_PAGE/SAVE_PAGE at every subsequent idle tick | `filesystem_settingsWriterSchedule_tick()`'s idle-page suppression check | Do not add a bypass for Load/Save page suppression — that suppression is intentional and shared with the autosave scheduler; if this is the cause, the fix is in test sequencing (leave the page before expecting the write), not in the guard |
| AutoSave OFF does not stop an autosave file from being touched | A transaction was already inside `filesystem_start()`'s accepted-but-not-yet-`fs_autosave_transaction_active`-flagged window when OFF arrived (the one-tick gap between `filesystem_start()` returning nonzero and the `fs_autosave_transaction_active = 1u;` assignment in `filesystem_autosaveWriterSchedule_tick()`) | `filesystem_autosaveWriterSchedule_tick()` — move `fs_autosave_transaction_active = 1u;` to be set atomically with (immediately adjacent to, same tick as) the `filesystem_start()` call, or confirm no intervening tick can observe the gap | Do not change `filesystem_setAutosaveEnabled()`'s own logic; the gap, if real, is in the scheduler's bookkeeping order, not the policy function |
| Re-enabling ON does not resume autosave until next boot | `fs_autosave_setup_failed` was left set from an earlier failure and nothing clears it on a fresh ON | `filesystem_setAutosaveEnabled()` — confirm the existing `fs_autosave_setup_failed = 0u;` reset on the enable path (~line 17612) actually executes for the runtime-ON branch being tested, not only the boot branch | Do not add a second failure-clearing path elsewhere; there should be exactly one owner for this flag's reset |

Each row is scoped to one function so that, consistent with root cause (d)
("the publication point was moved six times without isolating a variable"),
a fix attempt here changes one place, is hardware-tested, and is reverted if
it doesn't resolve the observed symptom before trying the next candidate —
it does not chain multiple candidate fixes into one untested pass.

### 3.6 Item 4 — packaged image size reconciliation (not a `.c`/`.h` change)

`AUTOSAVE_SETTINGS.md` documents a 368,132-byte image; the checked-in
`build/LXRV2_lxr02.img` is 368,012 bytes (120-byte difference). This is a
build/tooling verification task:

1. `make clean && make && make img` from a clean tree at the current
   accepted commit.
2. Compare the freshly produced `build/LXRV2_lxr02.img` size against both
   the documented 368,132 and the checked-in 368,012.
3. If the fresh build matches 368,012, update `AUTOSAVE_SETTINGS.md`'s
   documented figure to match reality (a documentation fix, not a source
   fix). If the fresh build matches neither, that is a build-reproducibility
   defect and needs its own isolated investigation — do not fold that
   investigation into Phase 2 autosave work.

No changes to `tools/build_lxrv2_img.py` are proposed unless step 3 above
finds an actual reproducibility defect in the packaging step itself; the
tool's role here is read-only verification, not modification.

---

## 4. Summary of every proposed source change

| File | Change | New/modified | Bytes of new static storage | Depends on user sign-off (§1)? |
|---|---|---|---:|---|
| `Core/Bank/Scene/Autosave.h` | Add `autosave_maskDirtyBitCount()` declaration | New declaration | 0 | No |
| `Core/Bank/Scene/Autosave.c` | Add `autosave_maskDirtyBitCount()` definition | New function | 0 | No |
| `Core/Hardware/SD/filesystem.h` | Add `filesystem_autosave_diagnostic_t`, `FS_AUTOSAVE_DIAG_*` bits, `fs_autosave_diag_result_t`, `filesystem_getAutosaveDiagnostic()` declaration | New types + declaration | 0 (types only) | No |
| `Core/Hardware/SD/filesystem.c` | Add static `fs_autosave_diagnostic`; capture fields in `filesystem_autosaveWriterSchedule_tick()` and `filesystem_autosaveWriterCompleted()`; define `filesystem_getAutosaveDiagnostic()` | New static + 3 small edits to existing functions | 16 | **Yes — see §1** |

No changes are proposed to `BankData.c/.h`, `SceneData.c/.h`,
`presetManager.c/.h`, `menu.c/.h`, `config.h`, or `main.c` for Steps 2–3.
Every coordinate and provenance/policy boundary those files own was found
already correct by direct reading (§2.1, §3.1–§3.3); §3.5 specifies the one
function to touch for each plausible hardware-test failure, to be applied
only if and when that specific failure is actually observed.
