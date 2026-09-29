# S074 — AutoSave stops publishing; boots fall back to the Bank; boot timeout

**Status:** investigation only (2026-09-29). No code has been changed.

**Evidence:** the card copy in `SD_CARD_ATS_BOOT_BUG/`: `asavetrc.bin`,
`.hcprms1`, `.hcprms2`, `.pat*`, `.hcnames`, `settings.cfg` and the library
tree. The trace was decoded with `tools/decode_devlogs.py`, and the records
were checked by hand (CRC32C, headers, sizes).

**Firmware read:** the working tree of 2026-09-29, including the S074 bus
compressor and `xfd` changes.

---

## 1. Short answer

1. **One AutoSave file on the card is corrupt, and it is the *expected* kind
   of corruption.**
   - `.hcprms1` (record A) is a half-written generation 101: commit byte 0,
     no CRC.
   - It was cut off when the device lost power or was reset partway through
     an AutoSave publication.
   - The A/B design is meant to survive exactly this. Record B (generation
     100) stayed valid and is still the winner.
2. **What the power loss also did.** AsyncFATFS deliberately records a file's
   *allocated* size (whole 32 KiB clusters) in its directory entry while the
   file is open for writing. Only a completed `fclose` writes the true size.
   A was never closed, so on the card it is **65,536 bytes**: the 34,768-byte
   record plus 30,768 bytes of stale cluster data.
3. **The bug.** Both AutoSave validators check for an overlong file by
   reading the part past the record **one byte per poll until end-of-file**.
   - At boot this only costs about 0.4 s.
   - In the runtime drain it takes about 31,040 polls in the same phase. The
     development stall detector (`DEV_STALL_DETECTION`, on in this build)
     aborts any drain that stays in one phase for 30,000 polls, even while
     it is making progress.
   - So **every** drain aborts while still reading A, about 1,000 polls short
     of end-of-file. It never reaches the step that would delete and rewrite
     A.
4. **There is no escape.** An error discards the continuation shortcut, so
   every retry validates A again and aborts at the same place.
   - From trace session S10 onward (10 boots): **663 drains admitted, 662
     aborted, 1 cut off by power-off, 0 published.**
   - The failure is silent to the user.
5. **What you saw follows directly:**
   - your edits, including the Scene 13 Effect type change, were never saved,
     so each boot restored generation 100;
   - after you saved the Bank to a **new slot** (27 "NoBankOk"), the only
     valid record still belonged to Bank 25 "NoBankMd". Every boot then
     treated it as a Bank mismatch and reloaded all 16 Scenes from Bank 27.
     That is "boot continued to load directly from the Bank".
6. **The boot timeout is not in the evidence.** The trace stops about 31 s
   into the last traced session, and there is no `/bootlog.bin`. The delays
   this bug adds to boot, which I could measure, are small (§6). The
   timed-out boot needs to be reproduced to be diagnosed; §6 gives the steps.

**The fix** (§7) is small and local. A validator should reject a candidate on
the first byte past the record, as the Pattern validator already does. With
that change, the card as it is now repairs itself on the first drain. A
progress-aware stall detector, and a visible warning after repeated failures,
harden it further.

---

## 2. Evidence

| Item | Finding |
|---|---|
| `.hcprms2` (B) | 34,768 B, `HCPR` v3, commit `0xA5`, **generation 100**, stored CRC `0x1ACD34EB` = computed. **Valid.** Bank section: slot **25**, name `NoBankMd`. |
| `.hcprms1` (A) | **65,536 B** (2 × 32 KiB clusters). Header `HCPR` v3, commit **`0x00`**, generation **101**, CRC **0** (placeholder). Bytes 34,768..65,535 are 30,768 B of stale cluster data (15,845 nonzero, beginning `ffffffff ffffff0f ffffff0f …`). A torn, uncommitted publication. |
| `settings.cfg` | `active_bank=27` (`/Bank/027 NoBankOk`). |
| `.pat06b` | **32,768 B** (one cluster) instead of 10,656 B. It has the same torn-write signature; its peer `.pat06a` is intact. Harmless (see §5.4). |
| Other files | No other file on the card has a cluster-multiple size. |
| `asavetrc.bin` | 461,512 B = 57,689 eight-byte records across **20 boot sessions** (S00..S19, split at each boot-reader summary). The ring dropped 13,000–28,000 records per session before flushing (`G` records). |
| `/bootlog.bin` | **Absent.** |

### 2.1 Trace timeline

| Sessions | Drains admitted / published | Boot validation (`V`) | Boot reader | Notes |
|---|---|---|---|---|
| S00–S08 | 101 / 97 (a drain with nothing new to write completes without publishing); generations 2 → **100** | winner A or B, flags `0x01`/`0x03` | no reloads, done in about 1.2 s | Healthy. The last publication is **generation 100 → B** at `#023512` (S08, t = 42.8 s). |
| S09 | 0 / 0 (writer armed, deadline t = 8.2 s) | B gen 100 | no reloads | A short session of about 5–8 s. |
| S10–S16 | 642 / 0 | **B gen 100**, flags `0x03` (Bank matches) | no reloads, about 1.5–1.7 s | **Every drain stalls in phase 3.** Effect type changes to Scene 13 (S14, S15) are marked dirty but never published. S16: **Save:[Bank] to slot 27**. |
| S17–S19 | 21 / 0 | **B gen 100**, flags **`0x07`** (Bank **mismatch**: B is slot 25, `active_bank` is 27) | **Case 2: all 16 Scenes, 144 rows, reloaded from Bank 27**; done at about 4.6–4.8 s | Every drain still stalls. Boot loads the Bank. |

A failing drain looks the same every time (`#023526`–`#023532` shown):

```
A  t= 8425  parameter drain admitted
X  t=14295  PHASE_STALL site=runtime AutoSave drain, phase=3, slot=0
E  t=14295  OPERATION_ERROR current_op=AUTOSAVE_PARAMETER_DRAIN, op_phase=41
T  t=14295  terminal ERROR          → rearmed; admitted again ~5 s later
```

- **Timing:** the stall arrives 2.6–5.9 s after admission, about 30,000
  drain polls at the foreground pass rate.
- **Phase numbers:** phase 3 is "stream one bounded candidate interval
  through validation", and slot 0 is candidate A. `op_phase=41` is the error
  close-down after the stall.
- **No validation record:** no `V` is ever emitted by these drains. They
  never finish validating A.
- **Pattern side effect:** while the scalar drain spins, the Pattern drain is
  refused its budget: `H` reports `class=pattern denied_count` of 2,384 and
  4,406 in a 5 s window (`#057254`, `#057682`).

### 2.2 Where the torn write most likely happened

In S08 you were editing Scene 13's `fdr0` (voice 1's fader mode) until
t = 42.1 s. Generation 100 was published at t = 42.8 s with the mask still
dirty. A dirty mask arms the 250 ms **continuation drain**. That drain skips
validation (the cached winner) and writes generation 101 straight into A. The
very next trace record (`#023514`) is already a new boot. So the continuation
drain's own records were still in the RAM ring when power went, and A was
left open for writing.

This is an ordinary event: power removed, a reset, or a firmware-update
restart within a second or two of an edit. The design has to survive it. It
also could have happened in the short S09 session, when its post-boot drain
ran; the trace cannot tell the two apart.

---

## 3. Root cause

### 3.1 Why A is 65,536 bytes: AsyncFATFS keeps the allocated size while a file is open

`Core/Hardware/SD/asyncfatfs/asyncfatfs.c:1875–1881`,
`afatfs_saveDirectoryEntry()`:

```c
case AFATFS_SAVE_DIRECTORY_NORMAL:
    /* We exaggerate the length of the written file so that if power is lost, the end of the file will
     * still be readable (though the very tail of the file will be uninitialized data). … */
    entry->fileSize = file->physicalSize;
```

- While a file is open for writing, its on-card size is the allocated
  (cluster-rounded) size. Only `AFATFS_SAVE_DIRECTORY_FOR_CLOSE` writes the
  true size.
- The drain publishes by deleting the inactive record, creating it with
  `"w"` (phases 11/24) and streaming the whole 34,768 B record (phase 13).
  Only then does it close (phase 66/15), sync, and reopen with `"r+"` to
  write the CRC and, last of all, the commit byte (phases 17–22).
- A power loss anywhere in phase 13 or before that close leaves the
  inactive record at the rounded size: 65,536 B for this record, 32,768 B for
  a Pattern record.
- **That is not the bug.** The torn record correctly fails validation (commit
  byte 0), and the peer stays authoritative.

### 3.2 The defect: validation reads the whole overlong tail, one byte per poll

The drain's validator is `filesystem.c:7943–7985` (phase 3). The boot
validator is identical at `filesystem.c:26017–26049`.

```c
read_bytes = (op_bytes_done < AUTOSAVE_RECORD_BYTES)
    ? filesystem_autosaveCrcChunkBytes(AUTOSAVE_RECORD_BYTES - op_bytes_done)
    : 1u;                                   /* ← past the record: 1 byte */
n = afatfs_fread(op_file, staging_buf, read_bytes);
if (n != 0u) {
    autosave_streamValidationUpdate(…);     /* ← invalidates on byte 34,769 */
    op_bytes_done += n;
    return;                                 /* ← one byte per poll, until EOF */
}
if (!afatfs_feof(op_file))
    return;
```

- `autosave_streamValidationUpdate()` (`Autosave.c:763–784`) already clears
  `header_valid` for the first byte past `AUTOSAVE_RECORD_BYTES`. So one
  extra byte proves the candidate invalid, and the other 30,767 reads prove
  nothing.
- **Cost for A:**
  - 34,768 B at `AUTOSAVE_CRC_BYTES_PER_TICK` = 128 B per poll = 272 polls;
  - then 30,768 single-byte polls;
  - **31,040 polls** in phase 3 in total.
- **The Pattern validator already does it right.**
  `filesystem_patternAutosaveCandidateValid()` (`filesystem.c:28183`) reads
  one byte after the payload and rejects the candidate unless that read
  returns 0 at end-of-file.

### 3.3 The stall detector counts polls in a phase, not polls without progress

`filesystem.c:7826–7843` (drain) with `filesystem_pollPhaseStall()`
(`filesystem.c:17304–17316`):

- **What it counts:** it counts calls while `op_phase` is unchanged. Bytes
  arriving do not reset it. At 30,000 it records `X`, sets the named error
  `DrSt03` and forces the error close-down.
- **Why that breaks here:** phase 3 legitimately loops once per poll while it
  streams, so 31,040 progress-making polls read as a stall.
- **Same pattern elsewhere:** the Bank-save and delete-slot observers
  (`filesystem.c:4108`, `11317`) use the same poll-count rule.
- **Build dependence:** with `DEV_STALL_DETECTION` at 0, the drain would take
  about 6 s once, reject A, and repair it (§3.4). The development build turns
  a slow path into a permanent failure. The slow path itself (§3.2) is the
  real defect.

### 3.4 Why it never recovers on its own

- **The repair step is unreachable.** Once A is rejected, a drain would pick
  B as the winner and publish generation 101 *into A*. Phase 11 deletes A,
  and phase 24 recreates it with `"w"`, which truncates. That would repair
  the file, but the drain never gets past validating A.
- **Every retry starts from scratch.**
  `filesystem_autosaveWriterCompleted()` (`filesystem.c:24021`) retries an
  error after 5 s. The error has already discarded the continuation shortcut
  (`fs_autosave_winner_cached`), so each retry does the full A → B
  validation, starting with A.
- **Nothing tells the user.** The background writer's error is not shown;
  the named code `DrSt03` is recorded but not displayed. It failed 663 times
  across 10 boots unnoticed.

---

## 4. How the symptoms follow

| What you saw | Mechanism |
|---|---|
| "AutoSave failed to take the Effect type change into account" | Scene 13's type-token bytes were marked dirty correctly in S14 and S15 (trace `D` records at `#027919`, `#029990`). No drain could publish after generation 100, so each boot restored generation 100's Effect type. I found **no Effect-type-specific fault**: the token is projected from the registry (`Autosave.c:1119–1129`), and `cbt` restores through `effects_typeFromToken()` like any type. "Especially with the new Effect types" is the timing: that is what you were editing when the drain stopped. |
| "After I saved the Bank … no AutoSave record … the boot loaded directly from the Bank" | S16's Bank save went to **slot 27** (`settings.cfg active_bank=27`). The only valid record (B, generation 100) belongs to **slot 25**. Boot validation checks the slot only, so from S17 it flags a mismatch (`V` flags `0x07`). The winner is not used, and the canonical Bank 27 load runs (Case-2 reload of all 144 rows). A working drain would have re-identified the record with Bank 27 on its first run (`autosave_markResidentBankDirty()` in phase 5); here it never ran. |
| Work lost | Everything after generation 100 that was not in the S16 Bank save is gone: edits in S10–S16 after the save, and all of S17–S19. It existed only in RAM. The Bank 27 folder holds the state at the S16 save. |
| Pattern edits possibly lost | The Pattern drain is budget-starved while the scalar drain spins (§2.1). |
| "Failed to boot in time; boot timeout" | Not captured. See §6. |

---

## 5. Secondary findings

1. **The trace ring overflows badly.** Whole-Scene dirty bursts (2,000+ `D`
   records at a time) push out lifecycle records: 13,000–28,000 dropped per
   session. This is why the torn publication's own records are missing.
   Consider collapsing whole-object `D` bursts into their `L` summary record,
   or flushing lifecycle stages (`A V C P T X E`) first.
2. **`tools/verify_bank_autosave.py` is stale.** It expects 129 HCNAMES rows
   (the format now has 161) and reports "neither AutoSave record is valid"
   for this card, although B is valid (CRC checked by hand).
3. **`tools/decode_devlogs.py` misreads `pattrace.bin`** as the AutoSave
   format, and labels boot-reader `Q` records "unknown producer". Cosmetic.
4. **`.pat06b` is torn the same way but is harmless.** The Pattern validator
   rejects it on its first extra byte, and the next Pattern generation for
   Scene 6 recreates it with `"w"` (`filesystem.c:15098`).

---

## 6. The boot timeout

**What is known:**

- **No evidence from that boot.** The trace's last flush is S19 at
  t = 31.0 s (`#057688`). The later part of S19 and the timed-out boot were
  never written to the card. There is no `/bootlog.bin`. The cooperative
  timeout path (`main.c:1223–1260`) makes exactly one best-effort attempt to
  write it after a remount, so either that attempt failed, or the boot hung
  in a way the cooperative deadline cannot see. `DEV_LOGGING_IWDG` is 0, so
  a hard hang leaves nothing at all.
- **The deadline:** `BOOT_FILESYSTEM_TIMEOUT_MS` = 20,000 ms per armed boot
  operation (`config.h:145`).
- **What this bug adds to boot, measured on S10–S19:**
  - boot validation of the overlong A: about +0.35–0.45 s (`V` at
    1.39–1.52 s against 1.04–1.15 s before);
  - after the Bank mismatch: the full Case-2 reload, about +3.2 s (reader
    done at 4.6–4.8 s against 1.2–1.7 s).
  - Both are far inside 20 s, and S17–S19 booted successfully from
    essentially this card state.

**Conclusion:**

- **Not proven** by this evidence. I have not confirmed a link between the
  AutoSave failure and the timed-out boot.
- The copied card is the state that boot saw, apart from anything written
  in S19 after t = 31 s. Among the files, only `.pat06b` shows a torn write,
  and it is rejected in one read.
- The quickest way to settle it is to boot this exact image on the bench.

**How to capture it (no code change needed):**

1. Copy `SD_CARD_ATS_BOOT_BUG/` back onto a card, without `.Spotlight-V100`
   and `.fseventsd`. Keep the original copy untouched.
2. Boot **the current firmware** with `DEV_MODE_DIAGNOSTIC 1`, so the boot
   stage number is shown on the LCD.
   - If it reproduces, the stuck stage names the operation, and
     `/bootlog.bin` should appear. Decode it with
     `tools/decode_devlogs.py`.
   - If the screen freezes with no timeout, repeat once with
     `DEV_LOGGING_IWDG 1` (a hang-hunting session only; see its
     `config.h` note) to capture a hard hang.
3. If it does **not** reproduce, the timed-out boot saw something this copy
   lacks. Keep `/bootlog.bin` whenever a timeout recurs.

---

## 7. Fix plan (not applied)

### F1 — reject an overlong candidate on its first extra byte (required; the root fix)

**Where:** the drain validator (`filesystem.c:7943–7985`) and the boot
validator (`filesystem.c:26017–26049`), the same edit in both.

**What:** once `op_bytes_done` has reached `AUTOSAVE_RECORD_BYTES`:

- a one-byte read that **returns data** proves the file overlong: the
  candidate is invalid, so close it now;
- only a read that returns 0 **at end-of-file** lets
  `autosave_streamValidationFinish()` decide.

This mirrors `filesystem_patternAutosaveCandidateValid()`. Sketch:

```c
if (op_bytes_done >= AUTOSAVE_RECORD_BYTES) {
    n = afatfs_fread(op_file, staging_buf, 1u);
    if (n == 0u && !afatfs_feof(op_file))
        return;                                  /* read still pending */
    /* Any byte past the record means a torn/overlong file (AsyncFATFS
     * keeps the cluster-rounded size of a file that was never closed). */
    op_autosave_writer.candidate_valid = (uint8_t)(n == 0u &&
        autosave_streamValidationFinish(&op_autosave_writer.validation));
    … unchanged bank-match test and close (phase 4) …
}
```

AsyncFATFS has no public file-size accessor, so this one-byte probe is the
cheapest test available.

**Effect on this card:**

1. The first drain after boot rejects A after 273 polls.
2. It picks B (Bank mismatch, so the whole resident Bank is marked).
3. It copy-forwards into A: phase 11 deletes the 65,536 B file, and phase 24
   recreates it at 34,768 B.
4. It publishes generation 101 for Bank 27.
5. The next boot restores from AutoSave again, with no mass reload and a
   boot about 3.5 s faster.

No manual repair is needed.

### F2 — make the drain's stall detector progress-aware (recommended)

- **The change:** reset `op_autosave_drain_stall_ticks` whenever
  `op_bytes_done` (or `op_autosave_writer.stream_offset`) advances, not only
  when `op_phase` changes. For example, pass a progress word into
  `filesystem_pollPhaseStall()` and compare it with the last one.
- **What it still catches:** a real stall (a card pulled, a wedged read) is
  still caught after 30,000 polls *without progress*.
- **Scope:** apply the same rule to the other two poll-count observers when
  convenient.
- **Why:** with F1 alone the validation is short again, but any other long,
  progressing loop could hit the same trap.

### F3 — make repeated AutoSave failure visible (recommended; your call on the UI)

- **The change:** after N consecutive drain errors in one session (for
  example 3, about 15–30 s), show the existing filesystem error overlay once
  with the named code (`DrSt03`), or mark the `ats` cell.
- **Why:** this failure ran silently for 10 boots and 663 attempts.

### F4 — trace ring priorities (optional, diagnostics only)

See §5.1. It would have recorded the torn publication directly.

### What not to do

- **Do not skip or trust-by-default a candidate after repeated errors.** If
  that candidate were the newer valid record, publishing over it would lose
  its content. F1 removes the need.
- **Do not pre-truncate or size-check records at boot.** It is not needed:
  the publication path already deletes and recreates the inactive record.

### Immediate workaround on the current firmware (optional)

Delete `.hcprms1` from the card on a computer.

- It holds nothing valid.
- At the next boot the ensure step creates a fresh baseline A for Bank 27,
  and drains publish normally again.
- The next interrupted publication can recreate the problem until F1 is in.

---

## 8. Verification plan for the fix

1. **Build:**
   - `make all` passes with no new warnings;
   - `link_budget.py` is unchanged apart from a few bytes of flash.
2. **This card (a copy):** boot the fixed firmware and check, in order:
   - **boot validation** `V` is back to about 1.05–1.15 s, with winner B
     generation 100 and flags `0x07`;
   - **first runtime drain:** `A`, then `V` with flags `0x07` and **no
     `X`**, then `C`, then `P` "newly active target A, generation 101", then
     `T DONE`;
   - **`.hcprms1`** is 34,768 B with commit `0xA5`;
   - **next boot:** `V` shows winner A, generation ≥ 101, flags `0x01`; the
     boot reader does no Case-2 mass reload and finishes in about 1.5 s;
   - **your edits** (for example an Effect type change) survive a power
     cycle.
3. **Torn-write test:**
   - make an edit, then power off during the following 1–2 s, repeated a few
     times;
   - after each, confirm with the trace and file sizes that the next boot
     validates quickly and the first drain repairs the torn record;
   - check `ls -l` sizes on a computer afterwards.
4. **F2:** remove the card during a drain. The observer must still abort
   with `X`, `E`, then `T ERROR`, and the drain must recover after the card
   returns.
5. **Boot-timeout reproduction** (§6), independently of the fix.
