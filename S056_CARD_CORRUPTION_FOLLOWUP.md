# S056 — SD card corruption and boot-logging failure: root cause and implementation plan

**Date:** 2026-08-23
**Captures:** SD_CARD3, SD_CARD4, SD_CARD5 (card intact), SD_CARD6, SD_CARD7 (card damaged)
**Status:** corruption mechanism **identified and proven from source**; boot freeze **still unidentified**

### How to read this document

§§1–6 are the diagnosis. **§§7–11 are the plan to implement.** §12 is the
rollback analysis, and it contains one finding that must be read before any
`git revert` is attempted (§12.1). §13 is the SRAM approval request, §14 the
verification procedure.

| Part | Contains | Gate |
|---|---|---|
| **F1** (§8) | Remove card I/O from the boot-failure path | None — implement now |
| **F2** (§9) | Fix the transport abort that corrupts sectors | None — implement now |
| **F3** (§10) | Retained-SRAM2 boot evidence ring | **Needs SRAM approval (§13)** |

F1 and F2 are independent of each other and of the revert decision. Both should
land regardless of what is decided about F3.

---

## 1. Verdict

Two distinct defects. Together they explain every observation, including why
the damage began exactly at SD_CARD6 and why SD_CARD6 and SD_CARD7 lost
different, nearly complementary sets of files.

| | Defect | Status |
|---|---|---|
| **D1** | `sdcard_abortTransferForBootLog()` releases chip select in the middle of an SD write data block, leaving the card to commit a corrupt sector | **Proven from source.** Pre-existing (session 047), latent until D2 |
| **D2** | The §15 trace drain performs directory-creating filesystem writes on the boot-failure path, before the transport has been recovered | **Proven from source.** Introduced by me in §15 |
| **D3** | The boot freeze itself | **Not identified.** See §6 |

D1 is the corruption engine. D2 is what loaded the gun: it put SD *writes* on
the failure path for the first time, and D1 only causes damage when a write is
in flight at the moment of the abort.

---

## 2. D1 — the corruption mechanism, proven

### 2.1 The write path holds CS asserted across the whole block

In [sdcard_lxr02.c](Core/Hardware/SD/asyncfatfs/sdcard_lxr02.c), a single-block
write asserts CS in `send_cmd_keep_cs()` at
[line 168](Core/Hardware/SD/asyncfatfs/sdcard_lxr02.c#L168) and **never
releases it** through the entire write sequence:

| State | Line | What it transmits | CS |
|---|---|---|---|
| `WRITING_TOKEN` | [296-300](Core/Hardware/SD/asyncfatfs/sdcard_lxr02.c#L296-L300) | `0xFE` start-block token | held |
| `WRITING_DATA` | [302-311](Core/Hardware/SD/asyncfatfs/sdcard_lxr02.c#L302-L311) | 512 payload bytes, 16 per poll | held |
| `WRITING_CRC` | [313-332](Core/Hardware/SD/asyncfatfs/sdcard_lxr02.c#L313-L332) | 2 CRC bytes, reads data-response token | held |
| `WRITING_WAIT_BUSY` | [334-346](Core/Hardware/SD/asyncfatfs/sdcard_lxr02.c#L334-L346) | polls until card leaves busy | **released here** |

CS is released only at
[line 339](Core/Hardware/SD/asyncfatfs/sdcard_lxr02.c#L339) (success),
[348](Core/Hardware/SD/asyncfatfs/sdcard_lxr02.c#L348) (busy timeout), or
[321](Core/Hardware/SD/asyncfatfs/sdcard_lxr02.c#L321) (card rejected the
write). That is correct: the block is atomic and must not be interrupted.

### 2.2 The abort ignores all of that

`sdcard_abortTransferForBootLog()`
([lines 116-146](Core/Hardware/SD/asyncfatfs/sdcard_lxr02.c#L116-L146)) is
unconditional:

```c
SD_CS_DEASSERT;
SPI_transmit(0xFF);
state = SDCARD_STATE_IDLE;
xfer_buffer = NULL;
xfer_block  = 0u;
...
xfer_callback = NULL;
```

It does not look at `state`. Called while the machine is in `WRITING_TOKEN`,
`WRITING_DATA`, or `WRITING_CRC`, it drops CS after the `0xFE` start token has
already gone out and before the 512-byte payload plus CRC is complete.

### 2.3 What the card does next

The card has an open CMD24 and a partial data block. The SD SPI specification
does not define host recovery from this, so the outcome is card-dependent, and
**both plausible behaviours corrupt the target sector**:

- The card remains in its data-receive state and consumes whatever is clocked
  next as the remainder of the block. The very next thing this code path does
  is `spi_sd_set_slow()` and `SD_init()`
  ([filesystem.c:20188-20192](Core/Hardware/SD/filesystem.c#L20188-L20192)),
  which clocks out CMD0/CMD8/ACMD41 sequences. Those command bytes get written
  into the block as data, the card takes the CRC as satisfied or ignores it,
  and **programs the result to `xfer_block`**.
- Or the card aborts the programming operation internally, leaving the target
  sector partially programmed — indeterminate contents.

Either way one **real, addressed sector** is destroyed, and it is whichever
sector asyncfatfs happened to be writing when the boot stalled.

### 2.4 Reads are harmless — which is why this stayed hidden

Dropping CS mid-read (`READING_WAIT_TOKEN`, `READING_DATA`, `READING_CRC`)
changes nothing on the card. The card stops driving DO and no card-side state
is modified. The abort has therefore been safe for every boot failure that
stalled on a read — which is every capture before SD_CARD6.

This asymmetry is the whole reason D1 sat latent since session 047.

---

## 3. D2 — what changed at SD_CARD6

`sdcard_abortTransferForBootLog()` is unmodified this session (`git log` shows
its last change was `8cbcef2`, session 047; `git diff` on that file is empty).
So D1 was present for SD_CARD3, 4, and 5 as well, and those cards survived.

What changed is that §15 put **writes** on the failure path.

| Build | Failure path before the abort | Card |
|---|---|---|
| SD_CARD3/4/5 | §7's plain `filesystem_autosaveTraceFlushBlocking()` — returned immediately, because the deadline latch stopped `filesystem_tick()` (§14.4). **No I/O attempted.** | intact |
| SD_CARD6/7 | §15's `filesystem_autosaveTraceFlushAfterBootFailureBlocking()` — recovery framing lets the pump run, so it attempts **real I/O** for up to 20 s | **destroyed** |

Specifically, §15's drain calls `filesystem_start(FS_INTERNAL_OP_AUTOSAVE_TRACE_FLUSH, …)`,
which reaches `afatfs_fopen_lfn(AUTOSAVE_TRACE_FILENAME, "a", …)` at
[filesystem.c:4410](Core/Hardware/SD/filesystem.c#L4410). Mode `"a"` **creates
the file if absent** — and §17.5 had just instructed the user to delete
`/asavetrc.bin` from the card. So the drain:

1. scans the root directory,
2. **allocates a new root directory entry**, and
3. updates the FAT,

all against an asyncfatfs whose cache and allocator state belong to the mount
that just failed — then spins for 20 seconds pumping `filesystem_tick()` →
`afatfs_poll()` → `sdcard_poll()`, which keeps the write state machine
advancing.

That is how a write comes to be in flight when
`sdcard_abortTransferForBootLog()` fires. §15 did not create D1; it made D1
reachable, and it aimed it at the root directory.

**This is my error.** §15's premise — that the drain wrote nothing only because
the deadline latch blocked the tick — was correct as far as it went, and the
fix for that was right. What I never asked was whether the drain *should* be
performing filesystem writes at that point at all. It should not, and §17.6's
proposal to move it after the remount was still the wrong shape of answer: it
kept card I/O on a failed-card path.

---

## 4. The evidence fits this exactly

### 4.1 Metadata destroyed, file data untouched

| Capture | Survived at root | Lost |
|---|---|---|
| SD_CARD5 | everything | — |
| SD_CARD6 | `Bank/`, `INSTR~31/`, `loops/`, `.hcnames`, img | `.hcprms1`, `.hcprms2`, `Kit/`, `Scene/`, `samples/`, `settings.cfg` |
| SD_CARD7 | `.hcprms1`, `.hcprms2`, `Instrument/`, `Kit/`, `Scene/`, `settings.cfg`, `.hcnames` | `Bank/`, `loops/`, `samples/`, img |

Every surviving directory is **fully intact**: SD_CARD7's `Kit/` has 42 entries
and `Scene/` 48, matching SD_CARD5 exactly; SD_CARD6's `INSTR~31/` held all 190
files (Cymbal 31, Drum 97, HiHat 31, Snare 31). Cluster data was never touched.
Only directory entries and FAT chains were destroyed — exactly the sectors a
library-index build is writing.

### 4.2 The complementary pattern is the signature

SD_CARD6 and SD_CARD7 lost almost disjoint sets; the only common survivor is
`.hcnames`. That rules out a systematic bug that always damages the same
structure, and it rules out an unclean host unmount (which loses recent
metadata, not arbitrary interior directory entries).

It is precisely what D1 predicts: the destroyed sector is `xfer_block`, whose
value depends on exactly where in the index build the stall landed. A different
stall point means a different sector, which means a different set of directory
entries lost. **The randomness is the fingerprint.**

### 4.3 `INSTR~31`

SD_CARD6 showed `Instrument/` as `INSTR~31` — a mangled short name. A normal
8.3 alias would be `INSTRU~1`. This is a directory sector in which the LFN
chain and part of the short-name field were overwritten, leaving enough for the
host to mount the directory but not enough to recover its name. A partially
overwritten 512-byte directory sector produces exactly this.

---

## 5. Why no evidence ever reached the card

Three attempts (§7, §10, §15 of `S056_BOOT_HANG_FOLLOWUP.md`) tried to get the
trace ring onto the card at boot failure. `asavetrc.bin` contains **zero `Z`
and zero `Q` records** across all 69,243 events — it has not grown since before
§7 landed.

The reason is structural, and it is the same reason D1 is dangerous:

> The failure path is trying to write to an SD card whose transport has just
> failed, using a filesystem layer that has just failed, in order to record why
> it failed.

`bootlog.bin` is the only channel that ever worked, and it works only because
it tears the whole stack down and rebuilds it (`abort` → `destroy` → `SD_init`
→ remount) before writing 8 bytes. That teardown is exactly where D1 lives.
Every attempt to make the richer channel work meant doing more I/O in the one
state where I/O is least safe.

**No refinement of that approach can succeed.** It is the wrong architecture,
not a buggy implementation of the right one. F3 (§10) replaces it.

---

## 6. D3 — the boot freeze is still not identified

I want to state this plainly rather than dress it up.

**What is known:**

- The last-armed operation code is `LIBINDEX`
  (`FS_INTERNAL_OP_CREATE_LIBRARY_INDEX`), from the SD_CARD5 watchdog capsule.
  That covers boot stages 4 (Kit), 6 (Scene), and 7 (Bank) —
  [main.c:748](main.c#L748), [809](main.c#L809), [872](main.c#L872).
- The Instrument scan is a *different* operation
  (`filesystem_createBootIndexBlocking()`, stage 9,
  [main.c:909](main.c#L909)), so the stall is at or before stage 7.
- A **write** was in flight when the stall was aborted — that is what D1
  requires in order to have caused the damage. This is new and it narrows
  things: the stall is in the index *write* phase, not the directory scan.
- The transport driver cannot hang forever: `SDCARD_TOKEN_TIMEOUT` (5000) and
  `SDCARD_BUSY_TIMEOUT` (50000) are poll-count bounded
  ([sdcard_lxr02.c:70-71](Core/Hardware/SD/asyncfatfs/sdcard_lxr02.c#L70-L71)),
  and both exit paths deassert CS and fire the callback. So the non-returning
  wait is **above** the transport — in asyncfatfs (a cache sector that never
  unlocks, an allocator that never finds a cluster) or in the facade's own
  state machine.

**What is not known:** which of those, and why. And with `LIBINDEX` as the only
localisation, I cannot narrow it further from the captures in hand.

There is also a real possibility, which the D1 finding now raises, that the
boot freeze and the corruption are the **same phenomenon on successive boots**:
a boot that corrupts a FAT or directory sector leaves a card that the *next*
boot's index build stalls on, which corrupts another sector, and so on. The
card has been degrading across captures. If so, restoring a clean card may make
the freeze disappear without the underlying trigger having been found — that
would be a false all-clear, and it should be treated as one.

---

## 7. Implementation plan — overview and ordering

**Implement in this order.** F1 and F2 are both required before the unit is
booted again on a restored card.

| Step | Change | Files | Risk |
|---|---|---|---|
| 1 | **F1** C1–C5 — remove the drain | `main.c`, `filesystem.c`, `filesystem.h`, `DEV_MODES.md` | none; deletes an uncalled failure path |
| 2 | **F2** C6–C7 — quiesce the abort | `sdcard_lxr02.c` | low; bounded loop in a `DEV_MODE_LOGGING`-only function |
| 3 | Build + card restore + verification | — | §14 |
| 4 | **F3** C8–C15 — SRAM2 evidence ring | 6 files | gated on §13 approval |

**Do not** reorder so that a boot happens between step 1 and step 2. F1 alone
returns the failure path to SD_CARD5 behaviour, which did not damage a card in
practice — but D1 remains live, and any boot that stalls on a write would still
corrupt a sector. F2 is what actually closes the hole.

---

## 8. F1 — Remove card I/O from the boot-failure path

### C1 — Delete the drain call and its rationale block

**File:** [main.c](main.c) — the block currently at lines 1252–1265, inside the
`boot_filesystem_failure` label.

**Delete entirely:**

```c
        /*
         * Drain boot evidence before recovery can remount or halt the current
         * filesystem owner. The normal 500 ms scheduler is intentionally not
         * trusted here: a failure route may never return to another idle pass.
         * This is a logging-only synchronous append of the existing ring and
         * does not change the boot failure token or startup continuation.
         *
         * The recovery-framed variant is required, not stylistic: this label
         * is reachable only after the cooperative deadline has latched, and
         * after that latch filesystem_tick() returns immediately on every
         * call. The plain blocking drain cannot make progress here and
         * silently writes nothing -- which is exactly what SD_CARD5 recorded.
         */
        (void)filesystem_autosaveTraceFlushAfterBootFailureBlocking();
```

leaving the label body to begin at `filesystem_setBootSubstepDiagnostic(NULL);`.

**What it does:** removes every filesystem operation from the boot-failure path
ahead of the transport teardown, so nothing can put an SD write in flight when
`sdcard_abortTransferForBootLog()` runs.

**Why it must exist:** this call is the D2 amplifier. It is the only thing on
this path that opens a file, and `afatfs_fopen_lfn(…, "a", …)` creates a root
directory entry when the file is absent. It has never written a single record
in three revisions (§5), so nothing is lost by removing it.

**Do not replace it with the plain `filesystem_autosaveTraceFlushBlocking()`.**
That was §7's original form; it is harmless only because the deadline latch
makes it a no-op, and restoring it would leave a call that looks functional,
is not, and would resume doing real I/O the moment anyone "fixed" the latch
again. That is exactly the trap §15 fell into.

**Inputs:** none. **Outputs:** none. **Affiliates:** §15 Changes 1–3 and §17.6
of `S056_BOOT_HANG_FOLLOWUP.md`, both superseded by this document.

### C2 — Repair the `boot_traceLadder()` header comment

**File:** [main.c:241-248](main.c#L241-L248).

The §15 edit changed this comment's affiliate list to name the function C1
deletes. Restore it:

```c
 * AUTOSAVE_TRACE_STAGE_BOOT_LADDER, filesystem_getBootDiagnostic(),
 * and S056_CARD_CORRUPTION_FOLLOWUP.md §8.
```

**Why:** a comment that names a deleted function is the same class of
misdirection this whole investigation kept tripping over — the reader trusts
it and does not check. Note that `boot_traceLadder()` **itself stays**; see
§12.2.

### C3 — Delete `filesystem_autosaveTraceFlushAfterBootFailureBlocking()`

**File:** [filesystem.c:21496-21556](Core/Hardware/SD/filesystem.c#L21496-L21556)
— the whole function including its comment block.

**What it does:** removes the recovery-framed drain wrapper.

**Why it must exist:** the wrapper's own documented purpose is to make the
drain able to perform I/O after the boot deadline has latched. That capability
is precisely what must not exist. Its internal reasoning about
`fs_boot_logging_recovery` framing was correct and is not the problem; the
problem is what it enabled.

**Affiliates:** `filesystem_autosaveTraceFlushBlocking()` (retained, see C5
note), `filesystem_bootLoggingPollDeadline()`,
`filesystem_writeBootFailureLogBlocking()`.

### C4 — Delete the declaration

**File:** [filesystem.h:283-293](Core/Hardware/SD/filesystem.h#L283-L293) — the
declaration at line 293 and the doc comment above it, including the sentence
"Use this, not `filesystem_autosaveTraceFlushBlocking()`, anywhere
downstream…" at line 286, which directs future readers to the wrong thing.

### C5 — Retain `filesystem_autosaveTraceFlushBlocking()`, uncalled

**File:** [filesystem.c:21477](Core/Hardware/SD/filesystem.c#L21477),
[filesystem.h:281](Core/Hardware/SD/filesystem.h#L281). **No edit.**

After C1–C4 this function has **zero callers** (verified:
`grep -rn "autosaveTraceFlushBlocking" main.c Core/` returns only its own
definition, declaration, and the §15 wrapper being deleted). It is documented
as a bench-harness helper for an explicit pre-power-cycle evidence boundary,
it is safe when called from an idle facade, and it is not on any automatic
path. Keep it.

Add one line to its header comment so its status is not mistaken later:

```c
 * No firmware path calls this; it exists for bench use from an idle facade.
 * It must never be called from a boot-failure path -- see
 * S056_CARD_CORRUPTION_FOLLOWUP.md sections 3 and 8.
```

### C6 — Correct `DEV_MODES.md`

**File:** [knowledge_files/specification_reference/DEV_MODES.md](knowledge_files/specification_reference/DEV_MODES.md),
the paragraph added by §15 Change 6 near line 353.

**Delete** the sentence added by §15:

> After a cooperative boot timeout, the failure path drains through
> `filesystem_autosaveTraceFlushAfterBootFailureBlocking()`, not the plain
> blocking helper, because the timeout latch short-circuits `filesystem_tick()`
> and would otherwise make the drain unable to progress.

**Replace with:**

> After a cooperative boot timeout the failure path performs **no** filesystem
> I/O at all. The RAM ring is not drained to card on a failed boot: the card's
> transport and the asyncfatfs mount have both just failed, and attempting an
> append there corrupted the card twice (SD_CARD6, SD_CARD7). Boot evidence
> reaches the card only through the retained SRAM2 capsule, replayed on the
> next healthy boot. See `S056_CARD_CORRUPTION_FOLLOWUP.md`.

**Keep** the library-index phase-code paragraph §15 added near line 80 — those
codes remain accurate and the labels that produce them are retained (§12.2).

---

## 9. F2 — Make the transport abort quiesce instead of amputate

### C7 — Add the quiesce bound

**File:** [sdcard_lxr02.c](Core/Hardware/SD/asyncfatfs/sdcard_lxr02.c), with
the other tuning constants at
[lines 69-71](Core/Hardware/SD/asyncfatfs/sdcard_lxr02.c#L69-L71).

```c
/* Upper bound on polls spent finishing an in-flight write during a boot-log
** abort. A block needs 512/SDCARD_BURST_SIZE = 32 data polls plus one CRC
** poll; the card's program-busy window is already bounded internally by
** SDCARD_BUSY_TIMEOUT, whose own expiry deasserts CS and returns to IDLE.
** The margin covers the token and CRC states. */
#define SDCARD_ABORT_QUIESCE_POLLS  (SDCARD_BUSY_TIMEOUT + 64u)
```

### C8 — Rewrite `sdcard_abortTransferForBootLog()`

**File:** [sdcard_lxr02.c:116-146](Core/Hardware/SD/asyncfatfs/sdcard_lxr02.c#L116-L146)
— replace the whole function.

```c
void sdcard_abortTransferForBootLog(void)
{
    /*
     * Quiesce the transport without interrupting an in-flight write block.
     *
     * DEV_MODE_LOGGING writes operation codes to file for use in debugging. It
     * must never print anything to the screen or otherwise delay operations
     * unnecessarily since logging may be used to assess timing failures in
     * other modules that might otherwise be obscured by screen write delays.
     *
     * What: clears the stale completion callback, pumps the existing state
     * machine to its own idle boundary with a bounded budget when a write is
     * in flight, and only then releases CS and clears the transfer
     * coordinates. A read in flight is dropped immediately, as before.
     *
     * Why it must exist: CS is asserted by send_cmd_keep_cs() and held across
     * WRITING_TOKEN, WRITING_DATA, WRITING_CRC and WRITING_WAIT_BUSY, because
     * an SD SPI write block is atomic -- 0xFE start token, 512 payload bytes,
     * two CRC bytes, then the data-response token. Releasing CS partway
     * through leaves the card holding an incomplete block. It then either
     * consumes the following SD_init() command bytes as the remainder of that
     * block, or aborts the program internally, and in both cases commits
     * indeterminate contents to xfer_block. Because the boot ladder is
     * writing directory and FAT sectors when it stalls, that destroys
     * filesystem metadata: SD_CARD6 and SD_CARD7 each lost a different,
     * nearly complementary set of root directory entries while every file's
     * cluster data survived intact. Reads need no quiescing at all -- CS low
     * during a read changes no card state, which is why this defect stayed
     * latent from session 047 until a write first reached this path.
     *
     * Ordering matters. The callback is cleared BEFORE pumping so the
     * completion path cannot re-enter a facade that is being torn down;
     * sdcard_poll() null-checks xfer_callback at every one of its exits.
     * xfer_buffer is still valid here because afatfs_destroy(true) runs only
     * after this function returns, so the block completes with its correct
     * data rather than with padding.
     *
     * Inputs: the shim's private transfer state. Outputs: an idle card with
     * CS released, idle clocks supplied, every transfer coordinate cleared,
     * and no callback invoked. Bounded by SDCARD_ABORT_QUIESCE_POLLS, which
     * cannot be exceeded because SDCARD_BUSY_TIMEOUT terminates the only
     * open-ended state.
     *
     * Affiliates: sdcard_poll(), filesystem_writeBootFailureLogBlocking(),
     * afatfs_destroy(true), and the subsequent full SD_init() protocol reset.
     */
#if DEV_MODE_LOGGING
    uint32_t budget = SDCARD_ABORT_QUIESCE_POLLS;

    xfer_callback     = NULL;
    xfer_callbackData = 0u;

    while (budget-- != 0u &&
           (state == SDCARD_STATE_WRITING_TOKEN ||
            state == SDCARD_STATE_WRITING_DATA  ||
            state == SDCARD_STATE_WRITING_CRC   ||
            state == SDCARD_STATE_WRITING_WAIT_BUSY)) {
        (void)sdcard_poll();
    }

    SD_CS_DEASSERT;
    SPI_transmit(0xFF);
    state = SDCARD_STATE_IDLE;
    xfer_buffer = NULL;
    xfer_block = 0u;
    xfer_offset = 0u;
    retry_count = 0u;
    xfer_operation = SDCARD_BLOCK_OPERATION_READ;
#endif
}
```

**Scope check — no forward declaration is needed.** `sdcard_lxr02.c` includes
`sdcard_lxr02.h` at line 59, which includes `sdcard.h` at
[line 49](Core/Hardware/SD/asyncfatfs/sdcard_lxr02.h#L49), which declares
`bool sdcard_poll();` at
[sdcard.h:74](Core/Hardware/SD/asyncfatfs/sdcard.h#L74). `sdcard_poll()` is
therefore in scope at line 116 even though it is defined at line 247.

**Behaviour notes, all verified against `sdcard_poll()`:**

- The loop exits as soon as `sdcard_poll()` drives `state` out of the write
  group, which every terminal write path does
  ([339](Core/Hardware/SD/asyncfatfs/sdcard_lxr02.c#L339),
  [348](Core/Hardware/SD/asyncfatfs/sdcard_lxr02.c#L348),
  [321](Core/Hardware/SD/asyncfatfs/sdcard_lxr02.c#L321)).
- Those paths already call `SD_CS_DEASSERT` themselves. The unconditional
  deassert after the loop is therefore redundant on the normal exit and
  necessary on budget expiry and on the read path. Redundant deassert is
  harmless.
- `retry_count` is *not* reset before the loop, deliberately: if the machine
  was already deep into `WRITING_WAIT_BUSY`, its remaining budget should be
  what is left, not a fresh 50,000.
- `SDCARD_STATE_SENDING_CMD` exists in the enum but is never observed by
  `sdcard_poll()` — it is transient inside `send_cmd_keep_cs()` — so it is
  correctly absent from the loop predicate.

**Effect:** the in-flight block completes with correct data and the card is
left idle and consistent for `SD_init()`. No sector is corrupted.

---

## 10. F3 — Retained-SRAM2 boot evidence ring (gated on §13)

The architecture fix. It delivers what §7/§10/§15 tried and failed to deliver,
and it cannot corrupt anything because **it performs no card I/O on the failing
boot at all.**

**The design principle:** the only diagnostic channel that has ever worked is
the retained SRAM2 capsule. `LIBINDEX` came from it, via
`filesystem_devIwdgBootCheck()` at [main.c:626](main.c#L626), which reads
`fs_devwdg_capsule` after a watchdog reset and acts on it during the **next**
boot, when the filesystem is healthy. Widen that channel; change nothing about
when it touches the card.

**The key simplification over §17.6 and every earlier attempt:** the consumer
does not write a file. It replays the captured records into the existing RAM
trace ring via `autosaveTrace_record()`, and the ordinary idle flush scheduler
(`filesystem_autosaveTraceFlushSchedule_tick()`,
[filesystem.c:20657](Core/Hardware/SD/filesystem.c#L20657)) carries them to
`/asavetrc.bin` during normal runtime, when the facade is IDLE and the card is
healthy. **No new file I/O code is introduced anywhere.**

### C9 — Raise the linker section cap

**File:** [STM32F765VIHx_FLASH.ld:156](STM32F765VIHx_FLASH.ld#L156).

```
    ASSERT(_edevwdg_noinit - _sdevwdg_noinit <= 1024,
```

was `<= 32`. Also update the descriptive comment at
[line 147](STM32F765VIHx_FLASH.ld#L147) from "12 bytes" to "976 bytes".

**Why:** the capsule grows from 12 bytes to 976. The `ASSERT` is a real guard —
it is what keeps `.devwdg_noinit` from silently growing into unmapped space —
so it must be raised deliberately, not removed.

### C10 — Widen the capsule

**File:** [filesystem.c:402-409](Core/Hardware/SD/filesystem.c#L402-L409).

```c
#if DEV_LOGGING_IWDG
#define DEV_IWDG_CAPSULE_MAGIC 0x49574447u /* ASCII "IWDG" */
/*
 * Depth of the retained boot-evidence ring. 120 records x 8 bytes = 960 B,
 * giving a 976-byte capsule against the 1024-byte .devwdg_noinit cap. Boot
 * emits roughly 30-60 Z/Q records on a healthy ladder, so 120 covers a full
 * boot plus the stalled operation's repeats without wrapping away the start.
 */
#define DEV_BOOT_RING_RECORDS 120u
typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t  code[8];
    uint16_t seq;      /* total records ever written; wraps naturally */
    uint16_t count;    /* valid records, saturating at DEV_BOOT_RING_RECORDS */
    uint8_t  rec[DEV_BOOT_RING_RECORDS][8];
} devIwdgCapsule_t;
```

**What it does:** adds a fixed-depth ring of 8-byte records alongside the
existing 8-byte code, in the same never-zeroed SRAM2 section.

**Why the record is 8 bytes:** it is byte-identical to an `asavetrc.bin`
record (`stage`, `flags`, `tick16`, `value32`), so the existing decoder
(`tools/decode_devlogs.py`) reads replayed records with no change, and so the
replay in C13 is a straight `autosaveTrace_record()` call.

**Why `count` saturates but `seq` wraps:** `count` tells the consumer how many
slots are valid on a partially-filled ring; `seq` tells it where the oldest
record is once the ring has wrapped. Both are needed to replay in order.

**Lifetime:** one boot attempt. Survives a watchdog warm reset because
`.devwdg_noinit` is excluded from `Reset_Handler`'s zero-fill; lost on a power
cycle, which is not the failure this exists to catch.

### C11 — Producer

**File:** [filesystem.c](Core/Hardware/SD/filesystem.c), immediately after
`filesystem_bootLoggingSetDetail()` (~line 2967). New public function,
declared in `filesystem.h`.

```c
void filesystem_devBootRingRecord(uint8_t stage, uint8_t flags,
                                  uint16_t tick, uint32_t value)
{
    /*
     * Append one boot-evidence record to the retained SRAM2 ring.
     *
     * What: writes an 8-byte record in the same layout as an asavetrc.bin
     * record, into never-zeroed SRAM2, and advances the ring cursor.
     *
     * Why it must exist: the RAM trace ring cannot reach the card on a failed
     * boot -- the transport and the mount have both failed, and three attempts
     * to drain it there produced zero records and corrupted the card twice
     * (S056_CARD_CORRUPTION_FOLLOWUP.md sections 3 and 5). This ring survives
     * the watchdog reset instead, and is replayed on the next healthy boot by
     * filesystem_devIwdgBootCheck().
     *
     * Inputs: the same four fields autosaveTrace_record() takes. Output: SRAM
     * only. This function performs no card I/O, allocates nothing, calls no
     * FAT code, and cannot block -- those properties are the entire point and
     * must be preserved by any future edit.
     *
     * Affiliates: boot_traceLadder(), preset_setStatus(),
     * filesystem_devIwdgBootCheck(), AutosaveTrace.h record layout.
     */
#if DEV_MODE_LOGGING && DEV_LOGGING_IWDG
    uint16_t slot;

    if (!fs_boot_logging_active)
        return;
    if (fs_devwdg_capsule.magic != DEV_IWDG_CAPSULE_MAGIC)
        return;   /* armed by filesystem_bootLoggingArm() only */

    slot = (uint16_t)(fs_devwdg_capsule.seq % DEV_BOOT_RING_RECORDS);
    fs_devwdg_capsule.rec[slot][0] = stage;
    fs_devwdg_capsule.rec[slot][1] = flags;
    fs_devwdg_capsule.rec[slot][2] = (uint8_t)(tick & 0xFFu);
    fs_devwdg_capsule.rec[slot][3] = (uint8_t)(tick >> 8);
    fs_devwdg_capsule.rec[slot][4] = (uint8_t)(value & 0xFFu);
    fs_devwdg_capsule.rec[slot][5] = (uint8_t)((value >> 8) & 0xFFu);
    fs_devwdg_capsule.rec[slot][6] = (uint8_t)((value >> 16) & 0xFFu);
    fs_devwdg_capsule.rec[slot][7] = (uint8_t)((value >> 24) & 0xFFu);
    fs_devwdg_capsule.seq++;
    if (fs_devwdg_capsule.count < DEV_BOOT_RING_RECORDS)
        fs_devwdg_capsule.count++;
#else
    (void)stage; (void)flags; (void)tick; (void)value;
#endif
}
```

Also reset `seq`/`count` where the capsule is armed, in
`filesystem_bootLoggingArm()` at
[filesystem.c:2934-2937](Core/Hardware/SD/filesystem.c#L2934-L2937) — but only
on the **first** arm of a boot, since every later arm must not discard the
ladder recorded so far. Guard it on the magic already being unset:

```c
#if DEV_LOGGING_IWDG
    if (fs_devwdg_capsule.magic != DEV_IWDG_CAPSULE_MAGIC) {
        fs_devwdg_capsule.magic = DEV_IWDG_CAPSULE_MAGIC;
        fs_devwdg_capsule.seq   = 0u;
        fs_devwdg_capsule.count = 0u;
    }
    memcpy((void *)fs_devwdg_capsule.code, code, sizeof(fs_devwdg_capsule.code));
#endif
```

Note this changes the existing unconditional `magic = …` assignment into a
conditional one. `filesystem_devIwdgBootCheck()` already clears `magic` at
[filesystem.c:20902](Core/Hardware/SD/filesystem.c#L20902) once consumed, so
each boot still re-arms exactly once.

### C12 — Call sites

Two, both already emitting the records we want:

**C12a —** [main.c:250](main.c#L250), inside `boot_traceLadder()`, alongside
the existing `autosaveTrace_record()` call:

```c
    filesystem_devBootRingRecord(AUTOSAVE_TRACE_STAGE_BOOT_LADDER,
                                 event, tick, value);
```

**C12b —** [presetManager.c:178](Core/Bank/Scene/Preset/presetManager.c#L178),
inside `preset_setStatus()`, alongside its existing `Q` record.

**Why only these two:** they are the boot ladder and the Preset state machine —
the two things the last four captures needed and never got. Mirroring the whole
trace ring into 960 bytes would wrap it away; these two are what matter.

### C13 — Consumer: replay on the next healthy boot

**File:** [filesystem.c:20897-20902](Core/Hardware/SD/filesystem.c#L20897-L20902),
inside `filesystem_devIwdgBootCheck()`.

```c
    if (was_iwdg_reset && capsule_valid) {
        uint16_t i, n, first;

        memcpy(fs_boot_logging_code, (const void *)fs_devwdg_capsule.code,
               sizeof(fs_boot_logging_code));
        (void)filesystem_writeBootFailureLogBlocking();

        /*
         * Replay the retained ladder into the RAM trace ring. This performs
         * no file I/O: the ordinary idle flush scheduler carries these to
         * /asavetrc.bin later in this boot, once the facade is IDLE and the
         * card is known good. That deferral is the whole safety property --
         * see S056_CARD_CORRUPTION_FOLLOWUP.md section 5.
         */
        n = fs_devwdg_capsule.count;
        first = (uint16_t)((fs_devwdg_capsule.seq - n) % DEV_BOOT_RING_RECORDS);
        for (i = 0u; i < n; i++) {
            const volatile uint8_t *r =
                fs_devwdg_capsule.rec[(first + i) % DEV_BOOT_RING_RECORDS];
            autosaveTrace_record(
                (uint8_t)r[0],
                (uint8_t)(r[1] | AUTOSAVE_TRACE_FLAG_REPLAYED),
                ((uint32_t)r[4]) | ((uint32_t)r[5] << 8) |
                ((uint32_t)r[6] << 16) | ((uint32_t)r[7] << 24));
        }
    }
    fs_devwdg_capsule.magic = 0u;  /* consumed, or never valid; start clean */
```

**Note on the tick field:** `autosaveTrace_record()` stamps its own `tick16`
from the current clock, so a replayed record's original tick would be lost. The
original is preserved in the `value32` field only if the caller put it there.
Either accept that replayed records carry replay-time ticks and rely on their
*order* (sufficient for a ladder), or add a variant that takes an explicit
tick. **Recommend the former** — it adds no API surface, and ordering is what
the ladder needs.

### C14 — Replay flag

**File:** [Core/Bank/Scene/AutosaveTrace.h](Core/Bank/Scene/AutosaveTrace.h),
with the other flag constants.

```c
/* Set on records replayed from the retained SRAM2 capsule after a watchdog
** reset. Their tick16 is replay-time, not capture-time; read them in order,
** not by timestamp. */
#define AUTOSAVE_TRACE_FLAG_REPLAYED 0x80u
```

Check for a collision with existing flag bits in that header before using
`0x80`; pick the highest free bit if it is taken.

### C15 — Documentation

- [knowledge_files/specification_reference/SRAM_MANIFEST.md](knowledge_files/specification_reference/SRAM_MANIFEST.md)
  — update the `.devwdg_noinit` row from 12 to 976 bytes and note the ring.
- [DEV_MODES.md](knowledge_files/specification_reference/DEV_MODES.md) — document
  the replay flag and that boot evidence arrives one boot late.
- `tools/decode_devlogs.py` — surface `AUTOSAVE_TRACE_FLAG_REPLAYED` in output
  so replayed records are not mistaken for same-boot records.

---

## 11. What F3 does *not* do

It does not find D3. It makes the next stall produce a full `Z`/`Q` ladder plus
the `LIX?…` phase label, which is what is needed to find D3 — but the finding
is still ahead, and a boot that hangs without ever triggering the watchdog
still yields nothing. `DEV_LOGGING_IWDG` must stay `1` and
`DEV_LOGGING_IWDG_EXPIRE` at `25000u` for the capsule to be reached at all.

---

## 12. Rollbacks

### 12.1 Do not `git revert 8b9bf50` — it bundles unrelated good work

This is the most important item in this section.

Commit `8b9bf50` ("IWDG pre-expand with plan") contains **both** the boot-trace
instrumentation **and** the HCNAMES §11/§12 remediation:

```
Core/Bank/Scene/AutosaveTrace.h          147 +-
Core/Bank/Scene/Preset/presetManager.c   164 +-
Core/Bank/Scene/Preset/presetManager.h     6 +
Core/Hardware/SD/filesystem.c            936 +++++++---
Core/Hardware/SD/filesystem.h             68 +-
main.c                                   356 +++-
config.h                                  31 +-
```

That `filesystem.c` change is overwhelmingly the `.hcnames` publication fix —
the Kit Load / Kit Save / Instrument Load / Scene Load/Save coverage repair,
the loud-failure witnesses, and the retirement of the dead Menu owner. **That
work is good, it fixed a real and separately-reported bug, and it is unrelated
to the card corruption.**

Reverting the commit would destroy it. Every rollback below is therefore
surgical.

### 12.2 What must NOT be rolled back

This corrects the keep/discard table in the previous revision of this document,
which was wrong about the instrumentation.

| Item | Why it stays |
|---|---|
| `boot_traceLadder()` and the `Z` stage | SRAM-only. Writes to the RAM ring; performs no card I/O on any path. Harmless now, and it is F3's producer. |
| `preset_setStatus()`'s `Q` record | Same. |
| The eight `LIX?…` labels and `filesystem_libraryIndexDetail()` ([filesystem.c:3972](Core/Hardware/SD/filesystem.c#L3972), 4022–4135) | **SRAM-only** — they write `fs_boot_logging_code` and the capsule, never the card. They are the one part of §15 that can still pay off, because the capsule channel works. Keeping them costs nothing. |
| `boot_waitFilesystemPump()`, `boot_waitPresetPump()` | Bounded cooperative waits; not implicated. |
| `DEV_LOGGING_IWDG` and the capsule | The only channel that has ever produced evidence. |
| The HCNAMES §11/§12 remediation | Unrelated to this defect; see §12.1. |
| `S056_BOOT_HANG_FOLLOWUP.md` | Keep as the record, retractions included. Add a pointer to this document at its head. |

The earlier revision of this section proposed discarding the `LIX?…` labels and
the `Z`/`Q` stages unless F3 was adopted. That was wrong: none of them touch the
card, so none of them can cause this class of harm, and removing them would
throw away the producer F3 needs.

### 12.3 What is rolled back

Exactly C1–C6 (§8). Nothing else. Summarised:

| Rollback | Target | Effect |
|---|---|---|
| R1 | `main.c` drain call + comment block (C1) | no filesystem I/O on the failure path |
| R2 | `boot_traceLadder()` comment affiliate (C2) | comment stops naming a deleted function |
| R3 | `filesystem_autosaveTraceFlushAfterBootFailureBlocking()` (C3, C4) | the capability itself is gone |
| R4 | `DEV_MODES.md` §15 paragraph (C6) | spec stops prescribing the dangerous path |

After R1–R4, `git diff` against `HEAD` for `main.c`, `filesystem.c`, and
`filesystem.h` should be **empty except** for the C5 comment addition — because
§15 is the entire uncommitted delta (`git diff --stat` shows filesystem.c +108,
filesystem.h +12, main.c +11, all of it §15).

### 12.4 `config.h` decisions

| Symbol | Line | Current | Recommendation |
|---|---|---|---|
| `DEV_LOGGING_IWDG` | [191](config.h#L191) | `1` | **Keep at 1** until D3 is named. Revert per its own config policy afterwards. |
| `DEV_LOGGING_IWDG_EXPIRE` | [211](config.h#L211) | `25000u` | **Keep.** Correct value; the 120000u original made the capsule unreachable in a normal bench wait. |
| `AUTOSAVE_TRACE_RECORD_COUNT` | [347](config.h#L347) | `2048u` | **Separate decision.** Marked "TEMPORARY approved expansion" and it predates this session (present at `09deaec`). It costs 16,384 B of SRAM1 versus 512 B at the `_DEFAULT` of 64. Nothing in F1/F2/F3 needs 2048. Reverting it frees 15,872 B of SRAM1 — worth doing once runtime tracing is no longer needed, but it is not part of this fix. |

### 12.5 Card restoration

`SD_CARD5/` is git-tracked (3,511 files) and is a known-good image.

1. Reformat the card **FAT32**. Do not attempt an in-place repair — the FAT and
   root directory both have unknown damage across two corruption events, and
   `fsck` cannot recover directory entries that were overwritten.
2. Restore from `SD_CARD5/`, **omitting** `bootlog.bin` and `asavetrc.bin` so
   the clean-slate property holds: any file present after the next boot was
   written by that boot.
3. Verify all 16 root entries mount before booting the unit.

`SD_CARD6/` and `SD_CARD7/` should be committed as evidence and never restored
from.

### 12.6 If a full revert to session 055 is still preferred

Understandable, but it costs the HCNAMES fix (§12.1) and it does **not** fix
D1 — which predates session 055 and would still corrupt a card on any boot that
stalls mid-write. If that route is taken:

1. `git revert` is still not the mechanism; branch from `09deaec` and
   cherry-pick the HCNAMES hunks forward, or accept losing them.
2. **Apply F2 regardless.** It is the only change here that fixes a defect
   older than this session's work.

---

## 13. SRAM statement — approval required for F3 only

**F3 requires 976 bytes of SRAM2 and zero bytes of SRAM1.** F1 and F2 require
none and change no allocation.

SRAM2 is `0x2007C000`–`0x2007FFFF`, 16 KB, and the linker script records it as
"not currently mapped" and "left free"
([STM32F765VIHx_FLASH.ld:46-48](STM32F765VIHx_FLASH.ld#L46-L48),
[84](STM32F765VIHx_FLASH.ld#L84)). The only thing in it today is the 12-byte
capsule. Nothing else in the firmware allocates from SRAM2; the stack lives at
the top of SRAM1 (`_estack = 0x20080000`).

So this costs nothing any other subsystem is using, and it does not move `bss`,
the heap, or the stack. Net SRAM2: 12 B → 976 B, out of 16,384 available.

The one required edit outside the capsule is raising the `ASSERT` at
[STM32F765VIHx_FLASH.ld:156](STM32F765VIHx_FLASH.ld#L156) from 32 to 1024.

**120 records is a proposal, not a constraint.** 976 B is 6% of SRAM2; 500
records (4,016 B) would still be 25%. **Please confirm the depth before I
implement F3.**

---

## 14. Verification

### 14.1 After F1 and F2, before any boot

1. `grep -rn "autosaveTraceFlushAfterBootFailure" main.c Core/ knowledge_files/`
   → expect **0 hits**.
2. `grep -rn "autosaveTraceFlushBlocking" main.c Core/`
   → expect exactly **2 hits**: the definition
   ([filesystem.c:21477](Core/Hardware/SD/filesystem.c#L21477)) and the
   declaration ([filesystem.h:281](Core/Hardware/SD/filesystem.h#L281)). No
   callers.
3. `grep -n "SD_CS_DEASSERT" Core/Hardware/SD/asyncfatfs/sdcard_lxr02.c`
   → the abort's deassert must now be **after** the quiesce loop.
4. Confirm no filesystem call remains between `boot_filesystem_failure:` and
   `sdcard_abortTransferForBootLog()`. Read
   [main.c:1231](main.c#L1231)–1269 and check that the first statement is
   `filesystem_setBootSubstepDiagnostic(NULL);`.
5. `make clean && make` — **`make clean` is mandatory**, there is no `-MMD`
   header dependency tracking. Then `make img`, which `make clean` deletes.
6. Expect `bss` **unchanged** for F1+F2 — neither adds a file-scope static.
   Record the actual figure; do not assert it in advance.

### 14.2 The boot test

7. Restore the card per §12.5 and verify 16 root entries mount.
8. Boot the unit.
9. **Before reading any log, `diff -rq` the card against `SD_CARD5/`.** If any
   root entry is missing or renamed, F2 is incomplete and testing must stop —
   that is the only result that matters on this boot.
10. Only if step 9 is clean, read `bootlog.bin`. A `LIX?…` code now localises
    the stall to one library-index phase.

### 14.3 Standing procedure

Delete `bootlog.bin` and `asavetrc.bin` from the card before every capture, and
`diff -rq` against `SD_CARD5/` after every boot. The first makes an absent file
a real negative result; the second catches a regression in F2 on the boot it
happens rather than three captures later.

---

## 15. What I got wrong

Recorded so the next reader does not repeat it.

- **§15 was the wrong fix.** I diagnosed why the drain wrote nothing (the
  deadline latch) and fixed that correctly, without asking whether the drain
  should be doing filesystem writes on that path at all. Making a dangerous
  operation *work* is not the same as making it *correct*.
- **§17.6 was still wrong.** Moving the drain after the remount kept card I/O
  on a failed-card path. It would have reduced the damage, not removed it.
- **I read the SD_CARD7 loss set as "the same folders" without checking.** It
  is nearly the complement of SD_CARD6's, and that single fact — randomness,
  not systematic damage — is what identified D1. It was available immediately.
- **I did not read the transport layer until the fourth capture.** Every
  earlier assessment reasoned about the facade and asyncfatfs while treating
  `sdcard_abortTransferForBootLog()` as a black box, on the strength of its own
  comment. The comment describes what the author intended, not what the code
  does to a card mid-write.
- **The first revision of this document's keep/discard table was wrong** — it
  proposed discarding SRAM-only instrumentation that cannot cause this class of
  harm. Corrected in §12.2.
