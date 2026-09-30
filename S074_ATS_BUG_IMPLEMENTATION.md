# S074 — AutoSave torn-record fix: implementation schedule

**Status:** **implemented, hardware-tested and trace-verified**
(2026-09-30). The user reported "the autosave fix is in and tested; seems
ok". The card output in `SD_CARD_ATS_CORRECTION_OUTPUT/` confirms the fix
worked as designed (§9). §6.3 and §6.6 are met, and §6.2 is met apart from
one known trace-ring gap. §6.4 and §6.5 were not specifically exercised.

**Investigation:** `S074_AUTOSAVE_BOOT_BUG.md`. This schedule implements its
fix plan §7. The table maps each report item to this schedule:

| Report item | This schedule |
|---|---|
| F1 | **Implemented.** One-byte end-of-file probe, one helper shared by both validators. |
| F2 | **Implemented.** Progress-aware drain stall observer, 0 B net RAM. |
| F3 | **Replaced by a trace record.** See the policy below. |
| F4 | **Not included.** It is diagnostics only and not needed for the fix. |

**Base:** HEAD `2ec72d6` ("s074 comp updates, doc, bugfix"), clean working
tree. Every line number below is a line number in that tree, before any
edit. Apply the changes in each file **from the highest line number down**
(§3), so earlier numbers stay valid.

**User policy (2026-09-30).** If the fix makes AutoSave recover from a failure
without user interaction, there is no user error screen; the event is only
logged in the trace. The fix meets that condition:

- a torn record is rejected after one extra byte;
- the next drain republishes into it, which deletes and recreates the file at
  its exact size;
- a genuine stall still aborts, keeps the dirty mask, and retries after 5 s;
- **no UI is added.**

### Implementation notes — 2026-09-30

- Implemented T1/T2, W1, H1, S1/S2, D1/D2, B1/B2, and P1–P3 in
  `AutosaveTrace.h`, `filesystem.c`, and `tools/decode_devlogs.py`.
- Added an idempotence guard beyond the original schedule text: if an
  asynchronous `afatfs_fclose()` refuses the first close attempt after the
  one-byte overlong probe, phase 3 preserves the already-decided invalid
  verdict instead of probing EOF again and allowing `Finish()` to overwrite it.
- Updated the existing drain-admission rearm block to clear the new progress
  snapshot as well as the counter; the observer remains 5 B (`1 + 2 + 2`) in
  DEV builds and is absent when `DEV_STALL_DETECTION` is 0.
- `make all` passed: `text=502,152`, `data=416`, `bss=426,384`, DTCM statics
  4,472 B, FX arena 126,592 B, application headroom 251,096 B. `make img`
  also passed; the raw payload is 502,568 B and the packaged
  `build/LXRV2_lxr02.img` is 502,584 B including its 16-byte image header. The only
  warnings are the pre-existing unused-function and embedded-libc warnings.
- A direct `filesystem.c` compile with a temporary `DEV_STALL_DETECTION=0`
  config override passed with the same pre-existing warnings. The override
  was removed after the check.
- `tools/decode_devlogs.py` decoded the captured S074 trace and a synthetic
  `V flags=0x17` record as winner B, Bank mismatch, and overlong candidate A;
  synthetic `X` output uses the corrected four-bit site field and the named
  progress-aware drain behavior. Python bytecode compilation was not used
  because this workspace's Python cache directory is not writable.
- Hardware/card checks in §6.3–§6.6 are not claimed: the captured card and
  firmware cannot be exercised from this workspace.

The torn-record rejection is logged in the existing VALIDATED (`V`) trace
record as two new flag bits. All 26 uppercase stage letters are already in
use, so no new stage is created.

---

## 1. Overview

### 1.1 What changes

1. **One validation step, shared by both validators.**
   `filesystem_autosaveValidateCandidateStep()` reads the 34,768-byte record
   in the existing 128-byte CRC chunks, then reads **exactly one more byte**:
   - that byte exists: the file is overlong (torn), so it is rejected
     immediately;
   - end-of-file instead: the length is exact, and the header, commit and CRC
     decide.

   It replaces the two copies of the loop that read an overlong tail one byte
   per poll to end-of-file. Those are the runtime drain's phase 3 and the boot
   validator's phase 3.
2. **The rejection is recorded.** A new `overlong_mask` byte in the drain
   workspace, which is already part of the 2,048-byte operation union, records
   which candidate was rejected this way. Both VALIDATED emitters report it
   as flags bits 4..5.
3. **The drain's stall observer measures progress, not phase changes.** It
   counts only polls on which neither `op_phase` nor a 16-bit progress word
   (the sum of the drain's byte and item cursors) changed. Its 32-bit counter
   is split into a 16-bit counter and a 16-bit progress snapshot, so the
   observer keeps its existing 5 bytes of DEV-only storage.
4. **The trace decoder is updated.** `tools/decode_devlogs.py` decodes the
   new `V` bits and the existing, previously undecoded bits 2..3. It also
   corrects the `X` decoder to the 4-bit site field it has been out of step
   with since Session 057, and names all 10 stall sites.

### 1.2 How the captured card repairs itself

Using the `SD_CARD_ATS_BOOT_BUG/` state:

1. **Boot:** validation rejects A after 273 reads. `V` flags are `0x17`:
   winner B, Bank mismatch, A overlong. It completes at about 1.05 s instead
   of about 1.45 s.
2. **Canonical Bank 27 load**, unchanged.
3. **First runtime drain:** `V` flags `0x17`, then `M`, `B`, `C`, then `P`
   "newly active target A, generation 101", then `T DONE`.
   - Phase 11 removes the 65,536 B `.hcprms1`.
   - Phase 24 recreates it, and it closes at 34,768 B.
4. **Next boot:** `V` flags `0x01` (winner A, generation ≥ 101, Bank match).
   AutoSave restores normally, with no mass reload.

---

## 2. Change list

| ID | File | Line(s) | Action | Purpose |
|---|---|---|---|---|
| T1 | `Core/Bank/Scene/AutosaveTrace.h` | L199–206 | MODIFY (comment) | Record the drain site's progress-aware rule in the `X` layout |
| T2 | `Core/Bank/Scene/AutosaveTrace.h` | after L197 | ADD | `V` flag layout, with new bits 4..5 |
| W1 | `Core/Hardware/SD/filesystem.c` | after L927 | ADD | `overlong_mask` field in `filesystem_autosave_writer_state_t` |
| H1 | `Core/Hardware/SD/filesystem.c` | after L7784 | ADD | Result constants and `filesystem_autosaveValidateCandidateStep()` |
| S1 | `Core/Hardware/SD/filesystem.c` | L7805–7809 | REPLACE | Progress-aware observer state, threshold, progress word, poll helper |
| S2 | `Core/Hardware/SD/filesystem.c` | L7813–7842 | REPLACE | The drain calls the progress-aware observer |
| D1 | `Core/Hardware/SD/filesystem.c` | L7943–7983 | REPLACE | Drain phase 3 uses the shared step |
| D2 | `Core/Hardware/SD/filesystem.c` | L8023–8040 | REPLACE | Drain `V` record reports overlong candidates |
| B1 | `Core/Hardware/SD/filesystem.c` | L26017–26047 | REPLACE | Boot validator phase 3 uses the shared step |
| B2 | `Core/Hardware/SD/filesystem.c` | L26082–26096 | REPLACE | Boot `V` record reports overlong candidates |
| P1 | `tools/decode_devlogs.py` | L159–163 | REPLACE | All 10 stall sites, plus a behaviour table |
| P2 | `tools/decode_devlogs.py` | L569–575 | REPLACE | `V` bits 2..5 |
| P3 | `tools/decode_devlogs.py` | L641–656 | REPLACE | `X`: 4-bit site, bit-4 native delete, per-site behaviour |

**Not changed**, verified:

- **The drain's error close-down (phases 40–43)** already closes the open
  candidate. Its retry after 5 s restores the captured dirty offsets.
- **Phase 11's removal and phase 24's `"w"` recreation** already repair a
  torn target once validation completes.
- **The Pattern validator** (`filesystem_patternAutosaveCandidateValid()`,
  L28183) already uses a one-byte end-of-file probe.
- **The ensure step** (`filesystem_ensureAutosaveFiles_tick()`) leaves
  existing files alone.
- **`Autosave.c`'s streaming validator** already invalidates on the first
  byte past the record.

---

## 3. Order within each file

| File | Order (highest line first) |
|---|---|
| `filesystem.c` | B2 (26082), B1 (26017), D2 (8023), D1 (7943), S2 (7813), S1 (7805), H1 (after 7784), W1 (after 927) |
| `AutosaveTrace.h` | T1 (199), T2 (after 197) |
| `decode_devlogs.py` | P3 (641), P2 (569), P1 (159) |

W1, T2 and H1 must be present before D1, D2, B1 and B2 compile. The whole
change builds as one unit.

---

## 4. Resources

| Resource | Change |
|---|---|
| **RAM** | **0 B.** <br>`overlong_mask` is one byte inside `filesystem_autosave_writer_state_t`, a member of the fixed 2,048-byte `filesystem_stage_workspace_t` union (`raw[FS_STAGE_CACHE_BYTES]`). The existing `_Static_assert(sizeof(filesystem_stage_workspace_t) <= FS_STAGE_CACHE_BYTES)` still holds; the writer state is far below 2,048 B. <br>The stall observer goes from `uint8_t` + `uint32_t` to `uint8_t` + `uint16_t` + `uint16_t`: 5 B before and after, in `DEV_STALL_DETECTION` builds only. <br>No trace ring or other allocation changes. |
| **Flash** | About +150 B: one shared helper replacing two inline loops, the progress helpers, and two flag terms. |
| **CPU** | Validation of an overlong candidate falls from about 31,040 reads to 273. Exact-size candidates are unchanged: 272 reads plus one end-of-file probe instead of one end-of-file read. The observer adds seven additions per drain poll in DEV builds. |
| **Card format** | Unchanged: no on-card byte, offset or record size changes. |

---

## 5. Changes

### T2 — `Core/Bank/Scene/AutosaveTrace.h` after L197 — ADD

Anchor: L196–197, the end of `autosave_trace_stage_t`:

```c
    AUTOSAVE_TRACE_STAGE_BUDGET_REPORT = 'H',
} autosave_trace_stage_t;
```

Insert after L197:

```c

/*
 * V (VALIDATED) flags layout.
 *
 * What:      one record per complete A/B candidate decision, from the runtime
 *            drain (filesystem_autosaveParameterDrain_tick() phase 5, or
 *            phase 0 on the continuation path) and from the boot validator
 *            (filesystem_validateAutosaveWinner_tick() phase 5). value32 is
 *            the winner's generation, or 0 without a winner.
 * flags:     bit 0 WINNER          a valid winner exists;
 *            bit 1 winner index    0 = A (.hcprms1), 1 = B (.hcprms2), when
 *                                  bit 0 is set;
 *            bit 2 BANK_MISMATCH   the winner's Bank identity does not match
 *                                  (runtime: slot and display name; boot:
 *                                  settings.cfg slot only);
 *            bit 3 CACHED          continuation path: validation skipped, the
 *                                  winner restored from the previous drain;
 *            bit 4 A_OVERLONG      candidate A was rejected because its file
 *                                  continues past the 34,768-byte record;
 *            bit 5 B_OVERLONG      the same for candidate B (S074).
 * Why 4..5:  a publication interrupted while its target was open for writing
 *            leaves AsyncFATFS's cluster-rounded file size on the card. The
 *            record is invalid (commit byte 0) and the next drain republishes
 *            into it, so recovery needs no user action. These bits are the
 *            trace-only evidence that it happened (user policy: log, no error
 *            screen). Bits 6..7 are reserved as zero.
 * Producers: the two VALIDATED emitters in filesystem.c.
 * Consumers: tools/decode_devlogs.py (V branch). Bits 0..3 keep their
 *            pre-S074 meaning and numeric use at both emitters.
 * Affiliates: filesystem_autosaveValidateCandidateStep() (sets the per-
 *            candidate overlong bit), S074_AUTOSAVE_BOOT_BUG.md.
 */
#define AUTOSAVE_TRACE_VALIDATED_FLAG_WINNER         (1u << 0u)
#define AUTOSAVE_TRACE_VALIDATED_WINNER_INDEX_SHIFT  1u
#define AUTOSAVE_TRACE_VALIDATED_FLAG_BANK_MISMATCH  (1u << 2u)
#define AUTOSAVE_TRACE_VALIDATED_FLAG_CACHED         (1u << 3u)
#define AUTOSAVE_TRACE_VALIDATED_OVERLONG_SHIFT      4u
#define AUTOSAVE_TRACE_VALIDATED_FLAG_A_OVERLONG     (1u << 4u)
#define AUTOSAVE_TRACE_VALIDATED_FLAG_B_OVERLONG     (1u << 5u)
```

---

### T1 — `Core/Bank/Scene/AutosaveTrace.h` L199–206 — MODIFY (comment only)

Current:

```c
/*
 * X (PHASE_STALL) flags/value layout. Bits 0..3 select the observer site;
 * bit 4 identifies a stall inside native recursive delete. value32 stores
 * phase in bits 0..7, numbered slot in bits 8..17, and site-specific extra
 * data in bits 18..31. Why widened from 3 to 4 bits: §8.7 stall evidence
 * policy requires every state machine to carry its own stall detector, and
 * 3 bits (8 values) was insufficient for the full set of observers.
 */
```

Replace with:

```c
/*
 * X (PHASE_STALL) flags/value layout. Bits 0..3 select the observer site;
 * bit 4 identifies a stall inside native recursive delete. value32 stores
 * phase in bits 0..7, numbered slot in bits 8..17, and site-specific extra
 * data in bits 18..31. Why widened from 3 to 4 bits: §8.7 stall evidence
 * policy requires every state machine to carry its own stall detector, and
 * 3 bits (8 values) was insufficient for the full set of observers.
 *
 * Site 2 (runtime AutoSave drain) is progress-aware since S074: it fires
 * after 30,000 consecutive polls on which neither op_phase nor the drain's
 * progress word changed (filesystem_autosaveDrainStalled()). Every other site
 * still counts polls with an unchanged phase (filesystem_pollPhaseStall()).
 * For site 2, the extra field carries stream_offset / 16.
 */
```

---

### W1 — `Core/Hardware/SD/filesystem.c` after L927 — ADD

Anchor: L926–928, the end of `filesystem_autosave_writer_state_t`:

```c
    uint8_t recovery_target_index;
    uint8_t recovery_using_names;
} filesystem_autosave_writer_state_t;
```

Insert after L927 (before the closing `}`):

```c
    /*
     * Candidates rejected as overlong during this validation pass (S074).
     *
     * What:       bit n set = candidate n (0 = A `.hcprms1`, 1 = B
     *             `.hcprms2`) continued past AUTOSAVE_RECORD_BYTES, the
     *             signature of a publication interrupted while its target was
     *             open for writing (AsyncFATFS stores the cluster-rounded
     *             size until close).
     * Why:        the trace must show that a torn record was found and
     *             rejected, although no error follows: the drain republishes
     *             into it (user policy: log only, no error screen).
     * Lifetime:   zeroed with the whole workspace at phase 0 of both
     *             validators (memset of op_autosave_writer); lives in the
     *             existing 2,048-byte operation union, so it adds no RAM.
     * Writers:    filesystem_autosaveValidateCandidateStep().
     * Readers:    the two VALIDATED emitters (flags bits 4..5,
     *             AUTOSAVE_TRACE_VALIDATED_OVERLONG_SHIFT).
     */
    uint8_t overlong_mask;
```

---

### H1 — `Core/Hardware/SD/filesystem.c` after L7784 — ADD

Anchor: L7780–7786, the end of `filesystem_autosaveRecoveryGeneration()`
and the start of the "RUNTIME AUTOSAVE PARAMETER-DRAIN state machine"
banner:

```c
static uint32_t filesystem_autosaveRecoveryGeneration(void)
{
    /* Recovery writes B=0 first, then A=1, restoring the documented baseline. */
    return (op_autosave_writer.recovery_target_index == 0u) ? 1u : 0u;
}

/* -----------------------------------------------------------------------
```

Insert after L7784:

```c

/*
 * Result of one bounded AutoSave candidate-validation step (S074).
 *
 * PENDING: the caller returns and polls the same phase again (one chunk was
 * consumed, or an asynchronous sector read is outstanding). DECIDED:
 * op_autosave_writer.candidate_valid holds the final verdict; the caller
 * applies its own Bank-match rule and closes the candidate.
 * Accessors: filesystem_autosaveValidateCandidateStep() and phase 3 of both
 * validators.
 */
#define FS_AUTOSAVE_CANDIDATE_PENDING  0u
#define FS_AUTOSAVE_CANDIDATE_DECIDED  1u

/*
 * Advance the open AutoSave candidate's streaming validation by one read.
 *
 * What:       while fewer than AUTOSAVE_RECORD_BYTES have been validated,
 *             reads the next CRC-budgeted chunk (AUTOSAVE_CRC_BYTES_PER_TICK)
 *             into Autosave.c's streaming validator. Once the whole record
 *             has been consumed, reads exactly one more byte:
 *             - data: the file continues past the record, so the candidate is
 *               invalid at once and its bit is set in overlong_mask;
 *             - end-of-file: the length is exact, and the header, commit and
 *               CRC verdict from autosave_streamValidationFinish() stands.
 *             A file that ends early is rejected by the same Finish call
 *             (its byte count is not the record's).
 * Why:        a publication interrupted while its target was open for writing
 *             leaves AsyncFATFS's cluster-rounded size on the card (asyncfatfs.c
 *             afatfs_saveDirectoryEntry(), NORMAL mode "exaggerates the
 *             length"): 65,536 B for this 34,768 B record. The previous loop
 *             read that 30,768-byte tail one byte per poll. At runtime that
 *             was about 31,040 polls in one phase, so the drain's stall
 *             observer aborted every attempt before the publication that
 *             deletes and recreates the torn file, and AutoSave stopped
 *             permanently (S074_AUTOSAVE_BOOT_BUG.md). The first byte past
 *             the record already invalidates the candidate
 *             (autosave_streamValidationUpdate() rejects any interval beyond
 *             the record), so no later read can change the verdict.
 * Inputs:     op_file (the open candidate), op_bytes_done (validated bytes,
 *             zeroed at the candidate's open), op_autosave_writer.validation,
 *             and op_autosave_writer.candidate_index.
 * Outputs:    FS_AUTOSAVE_CANDIDATE_PENDING, or FS_AUTOSAVE_CANDIDATE_DECIDED
 *             with candidate_valid set. An overlong candidate also gets bit
 *             candidate_index in overlong_mask. Calling again after DECIDED
 *             (the caller's close was refused and phase 3 repeats) returns
 *             the same verdict; an overlong file simply yields another byte.
 * Cost:       at most ceil(34,768 / 128) + 1 = 273 reads per candidate,
 *             whatever the file's size.
 * Accessors:  filesystem_autosaveParameterDrain_tick() phase 3 (runtime) and
 *             filesystem_validateAutosaveWinner_tick() phase 3 (boot).
 * Affiliates: autosave_streamValidationUpdate()/Finish() (Autosave.c),
 *             filesystem_patternAutosaveCandidateValid() (the same one-byte
 *             end-of-file probe for PAT4 records),
 *             AUTOSAVE_TRACE_VALIDATED_* (AutosaveTrace.h), drain phases 11
 *             and 24 (which repair the torn file once validation completes).
 */
static uint8_t filesystem_autosaveValidateCandidateStep(void)
{
    uint32_t n;

    if (op_bytes_done < AUTOSAVE_RECORD_BYTES) {
        n = afatfs_fread(op_file, staging_buf,
                         filesystem_autosaveCrcChunkBytes(
                             AUTOSAVE_RECORD_BYTES - op_bytes_done));
        if (n != 0u) {
            autosave_streamValidationUpdate(&op_autosave_writer.validation,
                                            op_bytes_done, staging_buf,
                                            (uint16_t)n);
            op_bytes_done += n;
            return FS_AUTOSAVE_CANDIDATE_PENDING;
        }
        if (!afatfs_feof(op_file))
            return FS_AUTOSAVE_CANDIDATE_PENDING;
        /* Short file: Finish rejects any byte count but the record's. */
        op_autosave_writer.candidate_valid =
            autosave_streamValidationFinish(&op_autosave_writer.validation);
        return FS_AUTOSAVE_CANDIDATE_DECIDED;
    }

    /* The whole record is validated; one byte decides overlong vs exact. */
    n = afatfs_fread(op_file, staging_buf, 1u);
    if (n != 0u) {
        op_autosave_writer.candidate_valid = 0u;
        op_autosave_writer.overlong_mask |=
            (uint8_t)(1u << op_autosave_writer.candidate_index);
        return FS_AUTOSAVE_CANDIDATE_DECIDED;
    }
    if (!afatfs_feof(op_file))
        return FS_AUTOSAVE_CANDIDATE_PENDING;
    op_autosave_writer.candidate_valid =
        autosave_streamValidationFinish(&op_autosave_writer.validation);
    return FS_AUTOSAVE_CANDIDATE_DECIDED;
}
```

**Notes:**

- **Everything it uses is declared earlier in `filesystem.c`:** `op_phase`
  (L432), `op_file` (L526), `op_bytes_done` (L528), `staging_buf` (L537),
  `filesystem_autosaveCrcChunkBytes()` (L6983), `op_autosave_writer` (the
  L1142 macro), and `Autosave.h`'s streaming API. It needs no prototype.
- **The helper is not DEV-gated.** It is production validation logic.

---

### S1 — `Core/Hardware/SD/filesystem.c` L7805–7809 — REPLACE

Current:

```c
#if DEV_STALL_DETECTION
/* Diagnostic-only phase observer for the runtime AutoSave drain. */
static uint8_t op_autosave_drain_last_phase = 0u;
static uint32_t op_autosave_drain_stall_ticks = 0u;
#endif
```

Replace with:

```c
#if DEV_STALL_DETECTION
/*
 * Progress-aware stall observer for the runtime AutoSave drain (S074).
 *
 * What:       the phase and 16-bit progress word seen on the previous poll,
 *             and the number of consecutive polls on which neither changed.
 *             The drain aborts after FS_AUTOSAVE_DRAIN_STALL_POLLS such polls.
 * Why:        the former observer counted polls with an unchanged phase even
 *             while bytes were moving, so any long but progressing phase
 *             (phase 3 reading a torn file's tail was the S074 case) was
 *             killed. A true stall — a wedged read, a card removed, a close
 *             that is never accepted — shows no progress and is still caught.
 * Storage:    uint8_t + uint16_t + uint16_t = 5 B, the same as the former
 *             uint8_t + uint32_t (RAM policy: no growth). It exists in
 *             DEV_STALL_DETECTION builds only; the threshold fits 16 bits.
 * Lifetime:   static; self-resets whenever the phase or progress changes, so
 *             no explicit reset is needed at drain start (phase 0 differs from
 *             the previous drain's terminal phase).
 * Accessors:  filesystem_autosaveDrainStalled() only.
 * Affiliates: filesystem_autosaveDrainProgress(), the X record (site 2), and
 *             filesystem_pollPhaseStall(), which the other nine sites keep.
 */
#define FS_AUTOSAVE_DRAIN_STALL_POLLS 30000u
_Static_assert(FS_AUTOSAVE_DRAIN_STALL_POLLS < UINT16_MAX,
               "drain stall threshold must fit the 16-bit observer counter");
static uint8_t op_autosave_drain_last_phase = 0u;
static uint16_t op_autosave_drain_stall_ticks = 0u;
static uint16_t op_autosave_drain_last_progress = 0u;

/*
 * Fold every drain cursor into one 16-bit progress word.
 *
 * What:       the modulo-65,536 sum of the cursors the drain phases advance:
 *             op_bytes_done (validation, HCNAMES line writes),
 *             op_item_offset (HCNAMES rows), stream_offset and chunk_written
 *             (copy, recovery baseline, CRC write), mask_bytes_read (winner
 *             mask), payload_scan_offset and patch_count (classification).
 * Why:        each cursor only increases within its phase, so a changed sum
 *             is proof of progress. A momentarily equal sum (for example a
 *             new copy chunk resetting chunk_written) costs one poll of count,
 *             never a false abort. The truncation is harmless: per-poll
 *             advances are at most a few hundred.
 * Inputs:     the file-scope cursors above. Output: the progress word.
 * Accessors:  filesystem_autosaveDrainStalled().
 * Affiliates: every drain phase that moves one of these cursors.
 */
static uint16_t filesystem_autosaveDrainProgress(void)
{
    return (uint16_t)(op_bytes_done + op_item_offset +
                      op_autosave_writer.stream_offset +
                      op_autosave_writer.chunk_written +
                      op_autosave_writer.mask_bytes_read +
                      op_autosave_writer.payload_scan_offset +
                      op_autosave_writer.patch_count);
}

/*
 * Report a true drain stall, once, at the threshold.
 *
 * What:       resets the count whenever op_phase or the progress word
 *             differs from the previous poll; otherwise counts (saturating)
 *             and returns nonzero exactly once, on poll
 *             FS_AUTOSAVE_DRAIN_STALL_POLLS + 1 — the same edge-triggered
 *             contract as filesystem_pollPhaseStall().
 * Why:        see the state block above; the caller then records X, sets the
 *             named code `DrSt` and runs the existing error close-down, and
 *             the writer retries after five seconds with its dirty mask
 *             intact.
 * Inputs:     op_phase and filesystem_autosaveDrainProgress().
 * Outputs:    nonzero once per stall; updates the three observer statics.
 * Accessors:  filesystem_autosaveParameterDrain_tick() (first statement).
 * Affiliates: filesystem_autosaveWriterFinishError(),
 *             filesystem_autosaveWriterCompleted() (the retry).
 */
static uint8_t filesystem_autosaveDrainStalled(void)
{
    const uint16_t progress = filesystem_autosaveDrainProgress();

    if (op_phase != op_autosave_drain_last_phase ||
        progress != op_autosave_drain_last_progress) {
        op_autosave_drain_last_phase = op_phase;
        op_autosave_drain_last_progress = progress;
        op_autosave_drain_stall_ticks = 0u;
        return 0u;
    }
    if (op_autosave_drain_stall_ticks < UINT16_MAX)
        op_autosave_drain_stall_ticks++;
    return (uint8_t)(op_autosave_drain_stall_ticks ==
                     FS_AUTOSAVE_DRAIN_STALL_POLLS + 1u);
}
#endif
```

**Note:** `op_item_offset` is declared at L1821 and `UINT16_MAX` comes from
`<stdint.h>` (L88), so both are visible here.

---

### S2 — `Core/Hardware/SD/filesystem.c` L7813–7842 — REPLACE

Current (the first statement of `filesystem_autosaveParameterDrain_tick()`):

```c
#if DEV_STALL_DETECTION
    /*
     * Observe and recover a true cooperative drain stall.
     *
     * What: records one PHASE_STALL after 30,000 unchanged polls and routes
     * the operation through the existing asynchronous writer error close-down.
     * Why: unlike the delete and Bank observers, this state machine previously
     * had no bounded escape from a soft SD stall. Inputs: op_phase and the
     * current streamed byte offset. Outputs: one trace record and ERROR
     * completion; no blocking close, remount, or new storage. Affiliates:
     * filesystem_pollPhaseStall(), filesystem_autosaveWriterFinishError(),
     * and op_autosave_writer.stream_offset.
     */
    if (filesystem_pollPhaseStall(op_phase,
                                  &op_autosave_drain_last_phase,
                                  &op_autosave_drain_stall_ticks,
                                  30000u)) {
```

Replace **only that part** (L7813–7829; the body from `uint32_t value = …`
through `#endif` at L7830–7842 stays as it is) with:

```c
#if DEV_STALL_DETECTION
    /*
     * Observe and recover a true cooperative drain stall.
     *
     * What: records one PHASE_STALL after FS_AUTOSAVE_DRAIN_STALL_POLLS
     * (30,000) consecutive polls with neither a phase change nor any cursor
     * progress, then routes the operation through the existing asynchronous
     * writer error close-down. Why: this state machine has no other bounded
     * escape from a soft SD stall; since S074 it measures progress, so a long
     * but advancing phase is never aborted. Inputs: op_phase, the drain
     * progress word, and the current streamed byte offset. Outputs: one X
     * record (site 2), the named code `DrSt<phase>`, and ERROR completion;
     * the writer retries after five seconds with the dirty mask restored. No
     * blocking close, remount, user screen, or new storage. Affiliates:
     * filesystem_autosaveDrainStalled(), filesystem_autosaveWriterFinishError(),
     * and op_autosave_writer.stream_offset.
     */
    if (filesystem_autosaveDrainStalled()) {
```

---

### D1 — `Core/Hardware/SD/filesystem.c` L7943–7983 — REPLACE

Current: the whole `case 3:` block of `filesystem_autosaveParameterDrain_tick()`,
from `case 3: /* STREAM ONE BOUNDED CANDIDATE INTERVAL THROUGH VALIDATION */`
to its closing `}` at L7983. It contains the local `read_bytes`/`n`, the
`: 1u;` tail read, the validation update, `afatfs_feof`, Finish, the runtime
Bank match and `afatfs_fclose`.

Replace L7943–7983 with:

```c
    case 3: /* STREAM ONE BOUNDED CANDIDATE INTERVAL THROUGH VALIDATION */
        /*
         * Validate the open candidate through the shared bounded step (S074).
         *
         * What: one CRC-budgeted chunk per poll, then a single-byte end-of-file
         * probe that rejects an overlong (torn) record at once and marks it in
         * overlong_mask for the VALIDATED record. Why: reading an overlong
         * tail to end-of-file one byte per poll tripped this drain's stall
         * observer on every attempt, so the drain never reached the
         * publication (phases 11/24) that deletes and recreates the torn file.
         * Inputs/outputs: see filesystem_autosaveValidateCandidateStep(). On a
         * verdict the runtime Bank rule (slot and display name) is applied and
         * the candidate is closed (phase 4). A refused close repeats this phase,
         * which returns the same verdict. Affiliates: phase 5 winner selection
         * and VALIDATED record; filesystem_validateAutosaveWinner_tick() (the
         * boot twin of this phase).
         */
        if (filesystem_autosaveValidateCandidateStep() ==
            FS_AUTOSAVE_CANDIDATE_PENDING)
            return;
        op_autosave_writer.candidate_bank_match = (uint8_t)(
            op_autosave_writer.candidate_valid &&
            autosave_streamValidationMatchesBank(
                &op_autosave_writer.validation, bank_restoreBankSlot(),
                bank_displayName()));
        op_close_done = false;
        if (afatfs_fclose(op_file, on_file_closed))
            op_phase = 4u;
        return;
```

`case 4:` (L7985) follows unchanged.

---

### D2 — `Core/Hardware/SD/filesystem.c` L8023–8040 — REPLACE

Current:

```c
        /*
         * VALIDATED marks the complete two-candidate decision before either
         * recovery or copy-forward work begins. flags bit 0 says a winner
         * exists; bit 1 is its A/B index when present; bit 2 indicates the
         * winner's Bank identity does not match the current resident Bank
         * (a legitimate Bank-session transition, not corruption). value is its
         * generation (zero without a winner).
         */
        autosaveTrace_record(
            AUTOSAVE_TRACE_STAGE_VALIDATED,
            (uint8_t)((op_autosave_writer.have_winner ? 1u : 0u) |
                      (op_autosave_writer.have_winner
                           ? (uint8_t)(op_autosave_writer.winner_index << 1u)
                           : 0u) |
                      (op_autosave_writer.have_winner &&
                       !op_autosave_writer.winner_bank_match ? 4u : 0u)),
            op_autosave_writer.have_winner
                ? op_autosave_writer.winner_generation : 0u);
```

Replace with:

```c
        /*
         * VALIDATED marks the complete two-candidate decision before either
         * recovery or copy-forward work begins. flags bit 0 says a winner
         * exists; bit 1 is its A/B index when present; bit 2 indicates the
         * winner's Bank identity does not match the current resident Bank
         * (a legitimate Bank-session transition, not corruption); bits 4..5
         * (S074) mark candidate A/B rejected as overlong, a publication torn
         * by power loss. No error follows that rejection: the copy-forward
         * below republishes into the inactive target, deleting and
         * recreating the torn file, so the bits are the only record of it
         * (user policy: trace only). value is the winner's generation (zero
         * without a winner). Layout: AUTOSAVE_TRACE_VALIDATED_* in
         * AutosaveTrace.h.
         */
        autosaveTrace_record(
            AUTOSAVE_TRACE_STAGE_VALIDATED,
            (uint8_t)((op_autosave_writer.have_winner ? 1u : 0u) |
                      (op_autosave_writer.have_winner
                           ? (uint8_t)(op_autosave_writer.winner_index << 1u)
                           : 0u) |
                      (op_autosave_writer.have_winner &&
                       !op_autosave_writer.winner_bank_match ? 4u : 0u) |
                      (uint8_t)(op_autosave_writer.overlong_mask <<
                                AUTOSAVE_TRACE_VALIDATED_OVERLONG_SHIFT)),
            op_autosave_writer.have_winner
                ? op_autosave_writer.winner_generation : 0u);
```

The continuation-path `V` (phase 0, L7894–7899) needs no change. It skips
validation, and `overlong_mask` is zero after the L7855 `memset`.

---

### B1 — `Core/Hardware/SD/filesystem.c` L26017–26047 — REPLACE

Current: the whole `case 3:` block of `filesystem_validateAutosaveWinner_tick()`,
from `case 3: /* STREAM ONE BOUNDED CANDIDATE INTERVAL THROUGH VALIDATION */`
to its closing `}` at L26047. It contains the same `: 1u;` tail loop as the
drain, the boot Bank rule (`validation.bank_slot == bank_restoreBankSlot()`)
and `afatfs_fclose`.

Replace L26017–26047 with:

```c
    case 3: /* STREAM ONE BOUNDED CANDIDATE INTERVAL THROUGH VALIDATION */
        /*
         * Validate the open candidate through the shared bounded step (S074).
         *
         * What: the same chunked read and single-byte end-of-file probe as the
         * runtime drain, so an overlong (torn) record costs one extra read at
         * boot instead of about 31,000 (about 0.4 s of pre-audio boot time on
         * the S074 card). Why: boot and runtime validation must reach the same
         * verdict by the same rule; the shared helper removes the duplicated
         * loop that let them diverge. Inputs/outputs: see
         * filesystem_autosaveValidateCandidateStep(). On a verdict the boot
         * Bank rule is applied — slot agreement only, because BankData's
         * display name is not loaded yet at boot stage 10b — and the candidate
         * is closed (phase 4). Affiliates: this function's phase 5 winner
         * selection and VALIDATED record, main.c stage 10b,
         * filesystem_autosaveParameterDrain_tick() phase 3.
         */
        if (filesystem_autosaveValidateCandidateStep() ==
            FS_AUTOSAVE_CANDIDATE_PENDING)
            return;
        op_autosave_writer.candidate_bank_match = (uint8_t)(
            op_autosave_writer.candidate_valid &&
            op_autosave_writer.validation.bank_slot ==
                bank_restoreBankSlot());
        op_close_done = false;
        if (afatfs_fclose(op_file, on_file_closed))
            op_phase = 4u;
        return;
```

`case 4:` (L26049) follows unchanged.

---

### B2 — `Core/Hardware/SD/filesystem.c` L26082–26096 — REPLACE

Current:

```c
        /*
         * VALIDATED mirrors the drain's trace: bit 0 says a winner exists,
         * bit 1 is its A/B index, bit 2 marks a winner whose Bank slot does
         * not match settings.cfg's active_bank.
         */
        autosaveTrace_record(
            AUTOSAVE_TRACE_STAGE_VALIDATED,
            (uint8_t)((op_autosave_writer.have_winner ? 1u : 0u) |
                      (op_autosave_writer.have_winner
                           ? (uint8_t)(op_autosave_writer.winner_index << 1u)
                           : 0u) |
                      (op_autosave_writer.have_winner &&
                       !op_autosave_writer.winner_bank_match ? 4u : 0u)),
            op_autosave_writer.have_winner
                ? op_autosave_writer.winner_generation : 0u);
```

Replace with:

```c
        /*
         * VALIDATED mirrors the drain's trace: bit 0 says a winner exists,
         * bit 1 is its A/B index, bit 2 marks a winner whose Bank slot does
         * not match settings.cfg's active_bank, and bits 4..5 (S074) mark
         * candidate A/B rejected as overlong (a publication torn by power
         * loss). A torn candidate needs no boot action: the first runtime
         * drain republishes into it. The bits are the trace-only record of
         * the event. Layout: AUTOSAVE_TRACE_VALIDATED_* in AutosaveTrace.h.
         */
        autosaveTrace_record(
            AUTOSAVE_TRACE_STAGE_VALIDATED,
            (uint8_t)((op_autosave_writer.have_winner ? 1u : 0u) |
                      (op_autosave_writer.have_winner
                           ? (uint8_t)(op_autosave_writer.winner_index << 1u)
                           : 0u) |
                      (op_autosave_writer.have_winner &&
                       !op_autosave_writer.winner_bank_match ? 4u : 0u) |
                      (uint8_t)(op_autosave_writer.overlong_mask <<
                                AUTOSAVE_TRACE_VALIDATED_OVERLONG_SHIFT)),
            op_autosave_writer.have_winner
                ? op_autosave_writer.winner_generation : 0u);
```

`fs_boot_winner` assignments (L26097–26100) follow unchanged.

---

### P1 — `tools/decode_devlogs.py` L159–163 — REPLACE

Current:

```python
PHASE_STALL_SITES = {
    0: "delete-slot resolver (filesystem_deleteSlotDirectory_tick)",
    1: "Bank Save entry (filesystem_saveBankDirectory_tick)",
    2: "runtime AutoSave drain (filesystem_autosaveParameterDrain_tick)",
}
```

Replace with:

```python
# AUTOSAVE_TRACE_PHASE_STALL_SITE_* (AutosaveTrace.h). Session 057 widened the
# site field to four bits and added sites 3..9; this table lagged behind until
# S074. PHASE_STALL_BEHAVIOUR records what each observer does when it fires
# (DEV_MODES.md "Stall detection" table), so a decoded X record states whether
# the operation was aborted or only observed.
PHASE_STALL_SITES = {
    0: "delete-slot resolver (filesystem_deleteSlotDirectory_tick)",
    1: "Bank Save entry (filesystem_saveBankDirectory_tick)",
    2: "runtime AutoSave drain (filesystem_autosaveParameterDrain_tick)",
    3: "Kit Save (filesystem_saveKitDirectory_tick)",
    4: "Scene Save (filesystem_saveSceneDirectory_tick)",
    5: "Kit Load (filesystem_loadKitDirectory_tick)",
    6: "Scene Load (filesystem_loadSceneDirectory_tick)",
    7: "Bank Load entry (filesystem_loadBankDirectory_tick)",
    8: "settings write (filesystem_saveGlobals_tick)",
    9: "flush finish (filesystem_flushFinish_tick)",
}

PHASE_STALL_BEHAVIOUR = {
    0: ("partial abort: only the pre-delete scan phases abort; a native "
        "deleteTree() in progress is observed only"),
    1: "observation only (Session 057)",
    2: ("abort: 30,000 consecutive polls with no phase change and no cursor "
        "progress (progress-aware since S074) force FS_STATUS_ERROR; the "
        "writer retries after five seconds with the dirty mask intact"),
    3: "abort (FS_STATUS_ERROR)",
    4: "observation only (Session 057)",
    5: "abort (FS_STATUS_ERROR)",
    6: "abort (FS_STATUS_ERROR)",
    7: "abort (FS_STATUS_ERROR)",
    8: "abort (FS_STATUS_ERROR)",
    9: "abort (FS_STATUS_ERROR)",
}
```

---

### P2 — `tools/decode_devlogs.py` L569–575 — REPLACE

Current:

```python
    elif ch == "V":
        has_winner = bool(flags & 0x01)
        winner = "A (.hcprms1)" if (flags & 0x02) == 0 else "B (.hcprms2)"
        detail = (f"{enum_name} via {producer}: "
                  f"winner_exists={int(has_winner)}, "
                  f"winner={winner if has_winner else 'none'}, "
                  f"winner generation={value}")
```

Replace with:

```python
    elif ch == "V":
        # AUTOSAVE_TRACE_VALIDATED_* (AutosaveTrace.h): bit 0 winner exists,
        # bit 1 winner A/B, bit 2 winner Bank identity mismatch, bit 3
        # continuation cache (validation skipped), bits 4..5 candidate A/B
        # rejected as overlong - a publication torn by power loss; the next
        # drain republishes into it (S074). Records written before S074 never
        # set bits 4..5.
        has_winner = bool(flags & 0x01)
        winner = "A (.hcprms1)" if (flags & 0x02) == 0 else "B (.hcprms2)"
        detail = (f"{enum_name} via {producer}: "
                  f"winner_exists={int(has_winner)}, "
                  f"winner={winner if has_winner else 'none'}, "
                  f"winner generation={value}, "
                  f"bank_mismatch={int(bool(flags & 0x04))}, "
                  f"cached={int(bool(flags & 0x08))}")
        torn = [name for bit, name in ((0x10, "A (.hcprms1)"),
                                       (0x20, "B (.hcprms2)"))
                if flags & bit]
        if torn:
            detail += ("; overlong candidate rejected: " + ", ".join(torn) +
                       " (publication torn by power loss; AsyncFATFS left the "
                       "cluster-rounded size; the next drain republishes "
                       "into it)")
```

---

### P3 — `tools/decode_devlogs.py` L641–656 — REPLACE

Current:

```python
    elif ch == "X":
        site = flags & 0x07
        in_native_delete = bool(flags & 0x08)
        site_name = PHASE_STALL_SITES.get(site, f"unknown site {site}")
        phase = value & 0xFF
        slot = (value >> 8) & 0x3FF
        extra = (value >> 18) & 0x3FFF
        detail = (f"{enum_name} via {producer}: site={site_name}, "
                  f"phase={phase}, slot={slot}; "
                  f"IN_NATIVE_DELETE={int(in_native_delete)}")
        if site == 0 and in_native_delete:
            detail += f", afatfs_getDeleteTreePhase() subphase={extra & 0xFF}"
        elif site == 2:
            detail += f", stream_offset~={extra * 16} bytes"
        detail += (". Observation only unless site is the runtime drain, "
                   "where a stall also forces FS_STATUS_ERROR completion.")
```

Replace with:

```python
    elif ch == "X":
        # AUTOSAVE_TRACE_PHASE_STALL_* (AutosaveTrace.h): flags bits 0..3 are
        # the observer site and bit 4 marks a stall inside native recursive
        # delete (Session 057 layout; this decoder read the pre-057 3-bit
        # layout until S074). value32: phase bits 0..7, slot bits 8..17,
        # site-specific extra bits 18..31.
        site = flags & 0x0F
        in_native_delete = bool(flags & 0x10)
        site_name = PHASE_STALL_SITES.get(site, f"unknown site {site}")
        phase = value & 0xFF
        slot = (value >> 8) & 0x3FF
        extra = (value >> 18) & 0x3FFF
        detail = (f"{enum_name} via {producer}: site={site_name}, "
                  f"phase={phase}, slot={slot}; "
                  f"IN_NATIVE_DELETE={int(in_native_delete)}")
        if site == 0 and in_native_delete:
            detail += f", afatfs_getDeleteTreePhase() subphase={extra & 0xFF}"
        elif site == 2:
            detail += f", stream_offset~={extra * 16} bytes"
        detail += ". " + PHASE_STALL_BEHAVIOUR.get(
            site, "behaviour unknown for this site") + "."
```

**Compatibility:** every record with site 0, 1 or 2 and bit 4 clear decodes
the same as before. Only native-delete records (bit 4) and sites 3..9 change,
and both were misread before.

---

## 6. Verification

### 6.1 Build

- **`make all`** passes with no new warnings, and `link_budget.py` shows
  unchanged DTCM, the same FX arena and `bss` 426,384 (0 B change).
- **Flash** grows by about 150 B.
- **Observer sizes:** `arm-none-eabi-nm -S build/lxr02.elf | grep
  op_autosave_drain` shows sizes `1`, `2`, `2` (LTO may add a `.lto_priv`
  suffix). Before the change they were `1` and `4`.
- **`DEV_STALL_DETECTION 0`** builds cleanly. The observer statics and both
  progress helpers compile out, and the shared validation step remains.

### 6.2 Decoder

- **Old trace:** `python3 tools/decode_devlogs.py
  SD_CARD_ATS_BOOT_BUG/asavetrc.bin`.
  - `V` lines gain `bank_mismatch=` and `cached=`. The S17–S19 boot records
    show `bank_mismatch=1`; no pre-S074 record shows an overlong candidate.
  - `X` lines for site 2 now end with the progress-aware abort text.
- **New trace:** after 6.3, a `V` with flags `0x17` reads "…
  bank_mismatch=1 … overlong candidate rejected: A (.hcprms1) …".

### 6.3 The captured card (repair without user action)

Use a copy of `SD_CARD_ATS_BOOT_BUG/` on a card, without `.Spotlight-V100`
and `.fseventsd`, and boot the fixed firmware:

| Step | Expect |
|---|---|
| Boot validation | `V` flags `0x17`, winner B generation 100, at about 1.05–1.15 s (was about 1.45 s) |
| Boot path | Canonical Bank 27 load (as before) |
| First runtime drain | `A`, `V 0x17`, `M`, `B`, `C`, `P` "target A, generation 101", `T DONE`; **no `X`, `E` or error `T`** |
| Card | `.hcprms1` = 34,768 B, commit `0xA5` (check on a computer) |
| Next boot | `V` flags `0x01`, winner A, generation ≥ 101; no Case-2 mass reload; reader done in about 1.5 s |
| Edits | An Effect type change and a fader edit survive a power cycle |

### 6.4 Torn-write reproduction

1. Make an edit, then within 0.3–1.5 s (while `P`/continuation drains run)
   switch off. Repeat a few times.
2. At the next boot, a `V` with bit 4 or 5 set appears whenever a torn target
   was left.
3. The first drain of that session publishes into it; check file sizes
   afterwards.
4. There is no user-visible message at any point.

### 6.5 Genuine stall is still caught

- Remove the card during a drain. Expect `X` (site 2), then `E`, then `T`
  ERROR within about 30,000 no-progress polls, then retries every 5 s.
- Reinsert the card, following the existing remount path. The next drain
  succeeds, and the dirty work is published.

### 6.6 Pattern AutoSave is no longer starved

After 6.3, `H` records show `class=pattern` denied counts back near zero. The
scalar drain now finishes in normal time.

---

## 7. Documentation follow-ups (after acceptance)

| Document | Update |
|---|---|
| `knowledge_files/specification_reference/AUTOSAVE.md` | Validation: the length is proven by a one-byte end-of-file probe after the record, in both validators. §"power loss can leave": add "a torn target carrying AsyncFATFS's cluster-rounded size; rejected on its first extra byte and republished by the next drain". |
| `…/DEV_MODES.md` | Stall-detection table: the drain row reads "30,000 polls without phase change **or progress**" (S074). The stage list documents `V` bits 2..5. |
| `S074_AUTOSAVE_BOOT_BUG.md` | Status: fixed (F1, F2); F3 replaced by trace-only logging per the user policy; F4 not done. Record the hardware result of §6.3/6.4. |
| `MEMORY.md` | Session log entry (per the closeout practice). |

---

## 8. Not included

- **A user error screen or `ats` marker.** Recovery is automatic, and the
  event is logged in `V` bits 4..5 (user policy).
- **Trace-ring priorities** (report F4). This is diagnostics only.
- **`tools/verify_bank_autosave.py`**, which still expects 129 HCNAMES rows
  and so misreports valid records. It is unrelated to the fix and can be
  done as a separate tool update.
- **The boot timeout** (report §6). It is unproven and needs the bench
  reproduction described there. This fix removes the two boot-time costs
  that were measured: about 0.4 s for validation, and about 3.2 s for the
  Case-2 reload, which no longer happens once AutoSave publishes for the
  current Bank again.

---

## 9. Hardware result (2026-09-30)

**Evidence:** `SD_CARD_ATS_CORRECTION_OUTPUT/`, the card after the user
flashed the fixed firmware (`LXRV2_lxr02.img`, 502,944 B, 2026-09-30 07:33)
and worked on a Scene. The trace was decoded with the updated
`tools/decode_devlogs.py`.

### 9.1 Card state after the fix

| File | Before (`SD_CARD_ATS_BOOT_BUG/`) | After |
|---|---|---|
| `.hcprms1` (A) | **65,536 B**, torn generation 101 (commit `0x00`, CRC 0) | **34,768 B**, `HCPR` v3, commit `0xA5`, **generation 143**, CRC valid, Bank 27 `NoBankOk` |
| `.hcprms2` (B) | 34,768 B, generation 100, Bank **25** `NoBankMd` | **34,768 B**, commit `0xA5`, **generation 144**, CRC valid, Bank **27** `NoBankOk` |
| `.pat06b` | **32,768 B** (torn) | **10,656 B** (rewritten by a Pattern generation) |
| Files with a cluster-rounded size | 2 | **none** |
| `settings.cfg` | `active_bank=27`, `autosave=1` | unchanged |

`asavetrc.bin` grew from 461,512 to 607,912 B (+18,300 records). Its first
461,512 bytes are byte-identical to the old trace, so the new records are a
continuation.

### 9.2 Trace, session by session (new records only)

| Session | Firmware | Boot validation (`V`) | Boot reader done | Drains | Published | Stalls (`X`/`E`) |
|---|---|---|---|---|---|---|
| S20 (`#057834`–`#067464`) | old | flags `0x07` at 1,397 ms (no overlong bit: the old firmware cannot set it) | 4,574 ms, Case-2 reload of all 16 Scenes | 163 | **0** | **163**, every one at phase 3 |
| S21 (`#067465`–`#075970`) | **fixed** | **flags `0x17` at 1,227 ms**: winner B, generation 100, Bank mismatch, **A rejected as overlong** | 4,391 ms, Case-2 (expected, Bank still mismatched at boot) | 43 admitted (plus 1 whose `A` was dropped) | **44: generations 101 → 144** | **0** |
| S22 (`#075971`–`#075988`) | **fixed** | flags `0x03` at 1,236 ms: winner B, **generation 144, Bank match**, no overlong bits | **1,383 ms, no reload** (restored from AutoSave) | 1 | 0: canonical mask clean (`M dirty=0`), finished read-only | 0 |

### 9.3 Checks against the expected sequence (§1.2, §6.3)

| Check | Expected | Observed | Result |
|---|---|---|---|
| Boot validation of the torn A | `V 0x17`, winner B generation 100, about 1.05–1.15 s | `V 0x17` at **1,227 ms** (S21) | **Pass.** Validation completes. It is 170 ms faster than the old firmware on the same card; the absolute time includes unrelated boot stages. |
| Boot path | Canonical Bank 27 load | Case-2 reload of all 16 Scenes (`Q 0x80`, mask `0xffff`) | **Pass** (expected while the record still belonged to Bank 25) |
| First runtime drain | `A`, `V 0x17`, `M`, `B`, `C`, `P` "target A, generation 101", `T DONE`, no `X`/`E` | `M` (`#071646`, full on-card mask read), `B`, `C` (1,536 patches), **`P` "newly active target A, generation 101"** (`#071652`, t = 26.2 s), **`T DONE`**; no `X`/`E` | **Pass.** That drain's `A` and `V` records were lost to trace-ring overflow (§9.4). The `M` record proves it took the full-validation path. |
| Torn file repaired | `.hcprms1` 34,768 B, commit `0xA5` | 34,768 B, `0xA5`, generation 143 | **Pass** |
| Catch-up | The whole Bank republished for Bank 27 | Generations 101–144 in S21, all `T DONE`, alternating A/B through the continuation cache (33 × `V 0x09`/`0x0b`) plus 10 full validations (`0x01`/`0x03`), with the first drain's `V` dropped (43 recorded + 1 = 44), **none with an overlong bit** | **Pass** |
| Next boot | Winner with Bank match; no mass reload; reader in about 1.5 s | `V 0x03`, B generation 144, match; reader done at **1,383 ms**, no Case-2 | **Pass** (about 3 s faster than S17–S21) |
| Edits survive a power cycle | Yes | The last S21 drain captured the final edit (`C` patch_count 1) and published generation 144. The S22 drain found nothing left to write. | **Pass** (trace-level). The user reports it "seems ok". |
| No user-visible error | None | None reported, and no UI path exists | **Pass** |
| Pattern AutoSave not starved (§6.6) | Denials near zero | S21: `class=pattern` denials (824–1,842 per 5 s report) **only during the catch-up, generations 101–113**, none afterwards. S22: 0. S20 (old firmware): up to 2,786. | **Pass.** The S21 denials are the legitimate whole-Bank republication backlog, not a stall. |

### 9.4 Observations

1. **Trace-ring overflow hid the first drain's `A`/`V`.** The Case-2
   whole-Bank reload marks about 15,000 `D` records at once. The ring
   reported `dropped=28368` before the first S21 drain and `dropped=41708`
   after it. The drain's own records went with them. This is report item F4
   (trace-ring priorities), still not done. The fix's own evidence survived:
   the boot `V 0x17` and the drain's `P` "target A, generation 101".
2. **The first full validation after boot is still slow:** 2–8.5 s from `A`
   to `V` (for example S22: `A` 7,913 → `V` 16,405 ms). The healthy
   pre-bug sessions show the same (S08: 7.2 s), because CRC validation is
   budget-paced. It is not a regression, and it is no longer anywhere near
   the stall limit.
3. **§6.4 (torn-write reproduction) and §6.5 (card pulled mid-drain) were
   not run as separate tests.** The captured card was a real torn-write case
   and repaired itself (§9.3). The §6.5 path is unchanged apart from the
   progress-aware observer, and no `X` fired on the fixed firmware.

### 9.5 Remaining follow-ups

- The documentation follow-ups in §7 (`AUTOSAVE.md`, `DEV_MODES.md`,
  `S074_AUTOSAVE_BOOT_BUG.md` status, `MEMORY.md`) are now due.
- F4 (trace-ring priorities for lifecycle records) remains optional.
- The boot-timeout reproduction (`S074_AUTOSAVE_BOOT_BUG.md` §6) remains
  open. With both torn files repaired and AutoSave publishing for the
  current Bank, the two measured boot-time costs are gone: S22's boot reader
  finished at 1,383 ms.
