# S087 P1 — Samples and wavetables: follow-up implementation (code fixes)

**Purpose:** the exact code changes that make the S087 implementation
fulfil `S087_P0_OSC_TYPE_SAMPLES_WAVETABLES.md` (r8) on the device. They
correct the defects found in `S087_P0_IMPLEMENTATION.md` §11.
**Base:** the working tree as left by the S087 P0 implementation, on
`0e2d61dd`. Line numbers below are for that tree.
**Scope:** firmware source in `Core/` only.

---

## 0. How to apply

- **Apply each file's changes in the order listed.** Within a file the
  changes run from the highest line to the lowest, so every cited line
  number stays valid until it is used.
- **Each change gives:** an ID, a file, a line or range, and **ADD**
  (insert after the cited line), **MOD** (replace the cited lines) or
  **DEL** (delete the cited lines).
- **Each ADD or MOD that creates or changes a function, field, macro or
  type carries its comment block** in the project form (What / Why /
  Inputs / Outputs / Accessors / Affiliates). The block goes into the
  source with the code.
- **Code is given complete.** Replaced lines are replaced exactly by the
  code shown.

### 0.1 Fix index

| ID | Finding (P0 impl. §11) | File(s) | Changes |
|---|---|---|---|
| P1-1 | F1: the catalogue header is programmed twice; commit fails on hardware | `SampleInstall.c` | SI-1 … SI-8 |
| P1-2 | F2: an aborted item rewinds onto programmed flash; also any failed word write | `SampleInstall.c` | SI-1 … SI-8 |
| P1-3 | F3: a wavetable folder with > 128 frames loops forever | `filesystem.c` | FS-5 |
| P1-4 | F4: the third folder level writes out of bounds and never installs | `filesystem.c` | FS-1 … FS-4, FS-6 |
| P1-5 | F5, F16: backward `pos` glide on `slp`; the D4 limit | `Oscillator.c` | OC-9, OC-11, OC-12, OC-13, OC-14 |
| P1-6 | F6: `slp` FM reads relative to the loop start; lead-in frozen | `Oscillator.c` | OC-11 |
| P1-7 | F7: modulator `w` entries and `w` sync masters use the sample-law increment | `Oscillator.c` | OC-16 |
| P1-8 | F8: wavetable blending rounds to whole frames | `Oscillator.c` | OC-1, OC-2, OC-4, OC-5, OC-6 |
| P1-9 | F9: 8-bit wavetables rebind every block | `Oscillator.c` | OC-16 |
| P1-10 | F10: per-step sample change starts from the previous sample's length | `Oscillator.c` | OC-15 |
| P1-11 | F11, F18: slot stamp collides with 0; R-E1 is +5 B instead of +2 B | `modulationNode.c/.h`, `Oscillator.h` | MN-1, MN-2, MN-5, MN-6, OH-4 |
| P1-12 | F17: `osc_setType()` does not release the slot stamp | `Oscillator.c` | OC-17 |
| P1-13 | F20: loop sync builder after a master kind change | `Oscillator.c` | OC-7 |
| P1-14 | F21: `slp` sync reset target past a `smpl` loop end | `Oscillator.c` | OC-8 |
| P1-15 | A modulator `wav` value past the list end binds nothing (silence) instead of clamping to an entry that exists (P0 r8) | `Oscillator.c` | OC-19 |
| P1-16 | The S085 ring law is d_i = depthQ15 + (i + 1) × step; the S087 `smp`/`slp`/`wtb` ring copies apply i × step (one step late) | `Oscillator.c` | OC-2, OC-3, OC-9, OC-10 |
| P1-17 | F14, F19: the dormant `wav` interpolation, the v1 sample loops and their shims are still compiled (ITCM, DTCM R-D1, dead code) | `Oscillator.c/.h`, `SampleMemory.c/.h`, `modulationNode.c/.h`, `InstrumentManager.c` | OC-18, OC-20 … OC-36, OH-1, OH-2, OH-3, OH-5, SM-1 … SM-3, MN-3, MN-4, MN-7, IM-1, IM-2 |
| P1-18 | §10.7: the step-automation display of wave values is not type-aware | `menu.c` | MU-1 … MU-3 |

**Not in this schedule** (no code change required for device function):
- **F12:** the Snare noise loop's +1 instruction. This is a decision for
  the user; A1 allows it.
- **F13:** the `slp` FM cost, allowed by A1. §9 gives the effect of this
  schedule on it.
- **F15:** the host test bench (not firmware).
- **D-1:** the field renaming (no functional effect).

---

## 1. `Core/SampleRom/SampleInstall.c` (P1-1, P1-2)

**The rule these changes follow:**
- A flash word can be programmed once after the sector erase.
  `sampleFlash_writeWord()` reads every word back and returns 3 on a
  mismatch.
- So every word the installer writes must be written exactly once, and
  the write cursor must never move back onto a word that was, or may
  have been, programmed.
- A failed write ends the install's writing (`install_fault`). The
  commit still publishes every item finished before the fault.

Apply in this order: SI-8, SI-7, SI-6, SI-5, SI-4, SI-3, SI-2, SI-1.

### SI-8 `SampleInstall.c:379-410` — MOD `sampleInstall_commit()`

```c
/*
 * Publish the catalogue (S087 P1; S087 stage C).
 *
 * What:       closes an open item (abort), flushes the output buffer, then
 *             programs the catalogue header exactly once: words 1..7 (version,
 *             entry sizes, counts, flags, data_end, reserved = 0) first, and
 *             the magic word (word 0) last. Invalidates the D-cache over sample
 *             flash and refreshes the read API.
 * Why:        a flash word can be programmed once after the erase, and
 *             sampleFlash_writeWord() verifies each word: the header must not
 *             be written at begin and again here (S087 P0 impl. §11 F1). The
 *             magic last keeps an interrupted install readable as EMPTY: until
 *             the magic is programmed, word 0 is erased.
 * Inputs:     the counters of finished items (sample_items, wavetable_items),
 *             write_addr (the end of the item data).
 * Outputs:    the header in flash; the refreshed catalogue; 1 when the header
 *             and the magic were programmed, else 0.
 * Accessors:  filesystem_installUserContentBlocking().
 * Affiliates: sampleMemory_refresh(); SampleMemory.h header layout;
 *             sampleFlash_writeBytes()/writeWord().
 */
uint8_t sampleInstall_commit(void)
{
    sample_catalogue_header_t header;
    uint32_t addr;

    if (item_open)
        sampleInstall_abortItem();
    (void)sampleInstall_flush();
    memset(&header, 0, sizeof(header));
    header.magic = 0xFFFFFFFFu;            /* not written here; see below */
    header.version = SAMPLE_CATALOGUE_VERSION;
    header.sample_entry_bytes = SAMPLE_ENTRY_BYTES;
    header.wavetable_entry_bytes = WAVETABLE_ENTRY_BYTES;
    header.sample_count = sample_items;
    header.wavetable_count = wavetable_items;
    header.data_end = write_addr;
    addr = SAMPLE_CATALOGUE_START_ADDRESS + 4u;
    if (sampleFlash_writeBytes(&addr, (const uint8_t *)&header + 4u,
                               (uint32_t)sizeof(header) - 4u) != 0)
        return 0u;
    if (sampleFlash_writeWord(SAMPLE_CATALOGUE_START_ADDRESS,
                              SAMPLE_CATALOGUE_MAGIC) != 0)
        return 0u;
    sampleFlash_invalidateRange(SAMPLE_ROM_START_ADDRESS,
                                SAMPLE_CATALOGUE_END_ADDRESS -
                                SAMPLE_ROM_START_ADDRESS);
    sampleMemory_refresh();
    return 1u;
}
```

### SI-7 `SampleInstall.c:346-370` — MOD `sampleInstall_begin()`

```c
/*
 * Start an install (S087 P1; S087 stage C).
 *
 * What:       erases sectors 7..11 and resets the installer state. Nothing is
 *             programmed: the catalogue header is written once, by
 *             sampleInstall_commit(). A failed erase sets install_fault, so no
 *             item is written into flash that is not erased.
 * Why:        S087 P0 impl. §11 F1: a header written here could not be
 *             rewritten at commit. An interrupted install reads EMPTY because
 *             the whole catalogue, magic included, stays erased.
 * Inputs:     none.
 * Outputs:    erased sample flash; write_addr at the first item address;
 *             counters cleared; install_fault from the erase result.
 * Accessors:  filesystem_installUserContentBlocking().
 * Affiliates: sampleFlash_eraseAllSamples(); sampleInstall_commit();
 *             sampleMemory_refresh() (EMPTY/STALE detection reads word 0 of
 *             the item region, which an S087 install never programs).
 */
void sampleInstall_begin(void)
{
    sampleFlash_init();
    install_fault = (uint8_t)(sampleFlash_eraseAllSamples() != 0);
    out_len = 0u;
    write_addr = SAMPLE_ROM_START_ADDRESS + 4u;
    item_start = write_addr;
    frames_total = 0u;
    frames_written = 0u;
    sample_items = 0u;
    wavetable_items = 0u;
    item_open = 0u;
    item_peak = 0;
}
```

### SI-6 `SampleInstall.c:337-344` — MOD `sampleInstall_abortItem()`

```c
/*
 * Drop the open item (S087 P1; S087 stage C).
 *
 * What:       discards the buffered, unwritten bytes and closes the item. The
 *             write cursor is NOT moved back: the item's words already
 *             programmed stay programmed and are skipped (lost space), and the
 *             next item starts at the first word never written. No catalogue
 *             entry is written for the dropped item.
 * Why:        S087 P0 impl. §11 F2: rewinding made the next item program over
 *             programmed words; its read-back failed and it was lost too.
 * Inputs:     item_open, out_len.
 * Outputs:    item_open = 0; out_len = 0; write_addr unchanged.
 * Accessors:  filesystem_installSampleFile(), filesystem_installWavetableHere(),
 *             sampleInstall_commit().
 * Affiliates: sampleInstall_program() (always moves the cursor past every word
 *             it attempted).
 */
void sampleInstall_abortItem(void)
{
    if (item_open) {
        out_len = 0u;
        item_open = 0u;
    }
}
```

### SI-5 `SampleInstall.c:310-335` — MOD `sampleInstall_finishWavetable()`

```c
/*
 * Finish the open wavetable (S087 P1; S087 stage C).
 *
 * What:       flushes the last frame bytes, then programs the 20 B wavetable
 *             entry once, in the next free wavetable slot. A failed entry write
 *             sets install_fault; the slot index is not reused.
 * Why:        catalogue entries follow the program-once rule like the item
 *             data; after a fault no further item may claim the same slot.
 * Inputs:     item_start, item_frame_len, frames_written, item_stored_bits,
 *             item_name, wavetable_items.
 * Outputs:    the entry in flash; wavetable_items + 1 on success; 1/0.
 * Accessors:  filesystem_installWavetableHere().
 * Affiliates: SampleMemory.h wavetable entry layout; sampleInstall_program().
 */
uint8_t sampleInstall_finishWavetable(void)
{
    wavetable_catalogue_entry_t entry;
    uint32_t entry_addr;

    if (item_open != 2u)
        return 0u;
    if (sampleInstall_flush() != 0) {
        item_open = 0u;
        return 0u;
    }
    memset(&entry, 0, sizeof(entry));
    entry.offset = item_start;
    entry.frame_len = item_frame_len;
    entry.frame_count = (uint8_t)frames_written;
    entry.bits = item_stored_bits;
    memcpy(entry.name, item_name, SAMPLE_DISPLAY_NAME_LEN);
    entry_addr = SAMPLE_CATALOGUE_START_ADDRESS +
                 sizeof(sample_catalogue_header_t) +
                 SAMPLE_MAX_COUNT * SAMPLE_ENTRY_BYTES +
                 (uint32_t)wavetable_items * WAVETABLE_ENTRY_BYTES;
    item_open = 0u;
    if (sampleInstall_program(&entry_addr, (const uint8_t *)&entry,
                              sizeof(entry)) != 0)
        return 0u;
    wavetable_items++;
    return 1u;
}
```

### SI-4 `SampleInstall.c:251-257` — MOD the guard of `sampleInstall_beginWavetable()`

Replace line 256:
`    if (!fmt || item_open || wavetable_items >= WAVETABLE_MAX_COUNT)`
with
`    if (!fmt || item_open || install_fault || wavetable_items >= WAVETABLE_MAX_COUNT)`

The function's comment block gains:
`S087 P1: refused after a write fault (install_fault).`

### SI-3 `SampleInstall.c:212-249` — MOD `sampleInstall_finishSample()`

```c
/*
 * Finish the open sample (S087 P1; S087 stage C).
 *
 * What:       flushes the last sample bytes, programs the two zero frames
 *             after the data (the smp end pad, A4), then programs the 28 B
 *             sample entry once in the next free sample slot (loop points from
 *             smpl when valid, else the whole sample; the gain code from the
 *             peak). Any failed write sets install_fault and closes the item.
 * Why:        program-once rule (S087 P0 impl. §11 F1/F2); the pad must exist
 *             for the smp reader, and the entry is the only thing that makes
 *             the item visible.
 * Inputs:     loop_start, loop_end (exclusive), has_loop; item_start,
 *             frames_total, item_name, item_peak, sample_items.
 * Outputs:    pad and entry in flash; sample_items + 1 on success; 1/0.
 * Accessors:  filesystem_installSampleFile().
 * Affiliates: sampleInstall_gainCode(); SampleMemory.h sample entry layout;
 *             sampleInstall_program().
 */
uint8_t sampleInstall_finishSample(uint32_t loop_start, uint32_t loop_end,
                                   uint8_t has_loop)
{
    sample_catalogue_entry_t entry;
    static const uint8_t zeros[4] = { 0u, 0u, 0u, 0u };
    uint32_t entry_addr;

    if (item_open != 1u)
        return 0u;
    if (sampleInstall_flush() != 0 ||
        sampleInstall_program(&write_addr, zeros, 4u) != 0) {
        item_open = 0u;
        return 0u;
    }
    memset(&entry, 0, sizeof(entry));
    entry.offset = item_start;
    entry.frames = frames_total;
    if (has_loop && loop_start < loop_end && loop_end <= frames_total) {
        entry.loop_start = loop_start;
        entry.loop_end = loop_end;
        entry.flags = 1u;
    } else {
        entry.loop_start = 0u;
        entry.loop_end = frames_total;
    }
    memcpy(entry.name, item_name, SAMPLE_DISPLAY_NAME_LEN);
    entry.gain_code = sampleInstall_gainCode((uint32_t)item_peak);
    entry_addr = SAMPLE_CATALOGUE_START_ADDRESS +
                 sizeof(sample_catalogue_header_t) +
                 (uint32_t)sample_items * SAMPLE_ENTRY_BYTES;
    item_open = 0u;
    if (sampleInstall_program(&entry_addr, (const uint8_t *)&entry,
                              sizeof(entry)) != 0)
        return 0u;
    sample_items++;
    return 1u;
}
```

### SI-2 `SampleInstall.c:166` — MOD the guard of `sampleInstall_beginSample()`

Replace line 166:
`    if (!fmt || item_open || sample_items >= SAMPLE_MAX_COUNT)`
with
`    if (!fmt || item_open || install_fault || sample_items >= SAMPLE_MAX_COUNT)`

The function's comment block gains:
`S087 P1: refused after a write fault (install_fault).`

### SI-1 `SampleInstall.c:40` and `:88-108` — ADD `install_fault` and `sampleInstall_program()`; MOD `sampleInstall_flush()`

**ADD after line 40** (`static int32_t  item_peak;`):

```c
/*
 * Write fault latch (S087 P1).
 *
 * What:       1 after a failed sector erase or a failed word write during this
 *             install; 0 otherwise. While set, no new item may begin.
 * Why:        after a failed write it is unknown which words of the attempted
 *             range were programmed; writing further items could program over
 *             them. Items finished before the fault stay valid and are
 *             published by sampleInstall_commit().
 * Inputs:     sampleInstall_begin() (erase), sampleInstall_program().
 * Outputs:    read by sampleInstall_beginSample()/beginWavetable().
 * Accessors:  this file only.
 * Affiliates: sampleFlash_writeWord() (read-back check, return 3).
 * RAM:        1 B .bss (within the approved R-C3 installer state).
 */
static uint8_t  install_fault;
```

**MOD lines 90-108** (`sampleInstall_flush()`), and **ADD
`sampleInstall_program()`** directly before it (after line 88):

```c
/*
 * Program bytes at a cursor, program-once safe (S087 P1).
 *
 * What:       programs `bytes` bytes at *addr (4-aligned; a partial last word
 *             is padded with 0xFF by sampleFlash_writeBytes()) and ALWAYS
 *             advances *addr past every word attempted, whether the write
 *             succeeded or not. A failure sets install_fault.
 * Why:        S087 P0 impl. §11 F2: sampleFlash_writeBytes() leaves its cursor
 *             at the start of the range when a word fails, although earlier
 *             words of the range are already programmed; the installer must
 *             never write those words again.
 * Inputs:     addr (cursor, 4-aligned), data, bytes.
 * Outputs:    *addr = old *addr + bytes rounded up to 4; install_fault; 0 on
 *             success, the sampleFlash error otherwise.
 * Accessors:  sampleInstall_flush(), sampleInstall_finishSample(),
 *             sampleInstall_finishWavetable().
 * Affiliates: sampleFlash_writeBytes().
 */
static int sampleInstall_program(uint32_t *addr, const uint8_t *data,
                                 uint32_t bytes)
{
    const uint32_t end = *addr + ((bytes + 3u) & ~3u);
    uint32_t cursor = *addr;
    int rc = sampleFlash_writeBytes(&cursor, data, bytes);

    *addr = end;
    if (rc != 0)
        install_fault = 1u;
    return rc;
}

/*
 * Write the buffered item bytes (S087 P1; S087 stage C).
 *
 * What:       programs the whole words in out_buf, then a partial last word
 *             padded with zeros (sample and frame padding must read 0), each
 *             through sampleInstall_program(); empties the buffer.
 * Why:        the cursor must advance past every attempted word (F2).
 * Inputs:     out_buf, out_len, write_addr.
 * Outputs:    flash; write_addr; out_len = 0; 0 or the error.
 * Accessors:  sampleInstall_append(), sampleInstall_finishSample(),
 *             sampleInstall_finishWavetable(), sampleInstall_commit().
 * Affiliates: sampleInstall_program().
 */
static int sampleInstall_flush(void)
{
    const uint32_t whole = out_len & ~3u;
    int rc = 0;

    if (whole != 0u)
        rc = sampleInstall_program(&write_addr, out_buf, whole);
    if (rc == 0 && (out_len & 3u) != 0u) {
        uint8_t tail[4] = { 0u, 0u, 0u, 0u };

        memcpy(tail, &out_buf[whole], (uint32_t)(out_len & 3u));
        rc = sampleInstall_program(&write_addr, tail, 4u);
    }
    out_len = 0u;
    return rc;
}
```

---

## 2. `Core/Hardware/SD/filesystem.c` (P1-3, P1-4)

**The depth convention after this change:**
- `samples/` and `wavetables/` are depth 0; their subfolders are depth 1;
  the next level is depth 2.
- These are the three folder levels the `FS_INSTALL_DEPTH` (3) arrays
  hold, indices 0–2.
- `filesystem_installPick()` keeps its guard `depth >= FS_INSTALL_DEPTH`.

Apply in this order: FS-6, FS-5, FS-4, FS-3, FS-2, FS-1.

### FS-6 `filesystem.c:24963` and `:24969` — MOD the walk entry depth

- Line 24963: `filesystem_installSampleFolder(dir, 1u);` →
  `filesystem_installSampleFolder(dir, 0u);  /* S087 P1: samples/ is depth 0 */`
- Line 24969: `filesystem_installWavetableFolder(dir, "wavetables", 1u);` →
  `filesystem_installWavetableFolder(dir, "wavetables", 0u);  /* S087 P1 */`

### FS-5 `filesystem.c:24773-24776` — MOD the frame-count cap in `filesystem_installWavetableHere()` pass 1

Replace

```c
                if (count < WAVETABLE_MAX_FRAMES)
                    count++;
                else
                    continue;
```

with

```c
                /*
                 * Frame cap without a stall (S087 P1).
                 *
                 * What:       counts frames up to WAVETABLE_MAX_FRAMES; files past
                 *             the cap are still taken (the "last" name below
                 *             advances) but not counted, and pass 2 installs
                 *             only the first `count` frames in natural order.
                 * Why:        S087 P0 impl. §11 F3: `continue` skipped the
                 *             advance, so the same file was picked forever and
                 *             the modal install hung with audio suspended.
                 * Affiliates: filesystem_installPick(); WAVETABLE_MAX_FRAMES.
                 */
                if (count < WAVETABLE_MAX_FRAMES)
                    count++;
```

### FS-4 `filesystem.c:24903` — MOD the recursion guard in `filesystem_installWavetableFolder()`

`    if (depth >= FS_INSTALL_DEPTH)` →
`    if ((uint8_t)(depth + 1u) >= FS_INSTALL_DEPTH)  /* S087 P1: children must index < FS_INSTALL_DEPTH */`

### FS-3 `filesystem.c:24880-24891` — MOD the comment block of `filesystem_installWavetableFolder()`

Replace `depth (1 = wavetables/)` with `depth (0 = wavetables/; at most
FS_INSTALL_DEPTH - 1)`, and add the line `S087 P1: depth indexes the
per-level name arrays directly (F4).`

### FS-2 `filesystem.c:24858` — MOD the recursion guard in `filesystem_installSampleFolder()`

`    if (depth >= FS_INSTALL_DEPTH)` →
`    if ((uint8_t)(depth + 1u) >= FS_INSTALL_DEPTH)  /* S087 P1: children must index < FS_INSTALL_DEPTH */`

### FS-1 `filesystem.c:24829-24839` and `:23360` — MOD comments

- **In the comment block of `filesystem_installSampleFolder()`:** replace
  `depth (1 = samples/)` with `depth (0 = samples/; at most
  FS_INSTALL_DEPTH - 1)`, and add `S087 P1: depth indexes the per-level
  name arrays directly (F4).`
- **Line 23360:** the define's comment becomes
  `/* folder levels incl. samples/ and wavetables/ (depth 0..2) */`.

---

## 3. `Core/DSPAudio/Oscillator.c` (P1-5 … P1-10, P1-12 … P1-17)

Apply in the listed order, from OC-1 (line 3204) down to OC-36
(line 123).

### OC-1 `Oscillator.c:3204-3249` — MOD `osc_wtbSyncBlendBlock()` (P1-8)

```c
/*
 * Wavetable sync slave with frame blending (S087 P1; S087 stage E).
 *
 * What:       with a granted interpolation slot, renders the block in at most
 *             two segments planned by osc_wtbPlanBlock(): each segment blends
 *             one frame pair with ff ramping through the fractional frame
 *             position; the cycle phase takes osc_syncPull() after every sample
 *             (reset target 0 + overshoot). Without a slot, or with one frame,
 *             the one-frame sync copy runs.
 * Why:        S087 P0 A3/A20: the frame index (pos) is the interpolation
 *             target; S087 P0 impl. §11 F8: the frame position must stay
 *             fractional, and a glide crossing a frame boundary inside the
 *             block changes pair at the crossing sample instead of
 *             extrapolating.
 * Inputs:     osc (bound to a wavetable), over[size], ratioQ16, depthQ30,
 *             buf, size.
 * Outputs:    buf (gain 1); osc->phase.
 * Cost:       per sample as before (two frame reads, two interpolations, the
 *             blend, the pull); the segment set-up is per block.
 * Accessors:  osc_renderTypedSync().
 * Affiliates: osc_wtbPlanBlock(), osc_wtbSyncBlock(), modNode_interpClaim().
 */
static OSC_S087_LOOP void osc_wtbSyncBlendBlock(OscInfo* osc,
                                                const uint32_t* over,
                                                uint32_t ratioQ16,
                                                int32_t depthQ30,
                                                int16_t* buf, uint8_t size)
{
	const wavetable_catalogue_entry_t *wt =
		sampleMemory_wavetableEntry(osc->sampleCacheIndex);
	uint32_t ph = osc->phase;
	const uint32_t inc = osc->phaseInc;
	const uint32_t count = wt ? wt->frame_count : 0u;
	osc_wtbPlan_t plan;
	uint8_t i = 0u;
	uint8_t s;

	if (!wt || !osc->sampleCacheData || count < 2u ||
	    !modNode_interpClaim(osc)) {
		osc_wtbSyncBlock(osc, over, ratioQ16, depthQ30, buf, size);
		return;
	}
	{
		const uint8_t bits = wt->bits;
		const uint32_t len = wt->frame_len;

		osc_wtbPlanBlock(osc, count, size, &plan);
		for (s = 0u; s < plan.segments; s++) {
			const uint8_t *a = osc_wtbFrame(osc, wt, plan.pair[s]);
			const uint8_t *b = osc_wtbFrame(osc, wt, plan.pair[s] + 1u);
			float ff = plan.ff[s];

			for (; i < plan.end[s]; i++) {
				const float x0 = osc_wtbRead(a, bits, len, ph);
				const float x1 = osc_wtbRead(b, bits, len, ph);

				buf[i] = (int16_t)(x0 + ff * (x1 - x0));
				ff += plan.step;
				ph = osc_syncPull(ph + inc, over[i], ratioQ16, depthQ30);
			}
		}
	}
	osc->phase = ph;
}
```

### OC-2 `Oscillator.c:3122-3169` — MOD `osc_wtbRingBlendBlock()` (P1-8, P1-16)

```c
/*
 * Wavetable ring carrier with frame blending (S087 P1; S087 stage E).
 *
 * What:       as osc_wtbSyncBlendBlock() for the frame plan; the blended value
 *             takes the ring law with d_i = depthQ15 + (i + 1) x depthStepQ15
 *             (the depth steps before it is used, as in every S085 ring copy).
 *             Without a slot, or with one frame, the one-frame ring copy runs.
 * Why:        S087 P0 A3/A20 and impl. §11 F8 (fractional frames, one pair per
 *             segment); the ring law of Oscillator.h
 *             calcNextOscSampleRingBlock() (the block ends at depthQ15 + size x
 *             depthStepQ15, the caller's next start).
 * Inputs:     osc (bound to a wavetable), modBuf[size], buf, size, depthQ15,
 *             depthStepQ15.
 * Outputs:    buf (gain 1); osc->phase.
 * Accessors:  osc_renderTypedRing().
 * Affiliates: osc_wtbPlanBlock(), osc_wtbRingBlock(), osc_ringSample(),
 *             modNode_interpClaim().
 */
static OSC_S087_LOOP void osc_wtbRingBlendBlock(OscInfo* osc,
                                                const int16_t* modBuf,
                                                int16_t* buf, uint8_t size,
                                                int32_t depthQ15,
                                                int32_t depthStepQ15)
{
	const wavetable_catalogue_entry_t *wt =
		sampleMemory_wavetableEntry(osc->sampleCacheIndex);
	uint32_t ph = osc->phase;
	const uint32_t inc = osc->phaseInc;
	const uint32_t count = wt ? wt->frame_count : 0u;
	int32_t depth = depthQ15;
	osc_wtbPlan_t plan;
	uint8_t i = 0u;
	uint8_t s;

	if (!wt || !osc->sampleCacheData || count < 2u ||
	    !modNode_interpClaim(osc)) {
		osc_wtbRingBlock(osc, modBuf, buf, size, depthQ15, depthStepQ15);
		return;
	}
	{
		const uint8_t bits = wt->bits;
		const uint32_t len = wt->frame_len;

		osc_wtbPlanBlock(osc, count, size, &plan);
		for (s = 0u; s < plan.segments; s++) {
			const uint8_t *a = osc_wtbFrame(osc, wt, plan.pair[s]);
			const uint8_t *b = osc_wtbFrame(osc, wt, plan.pair[s] + 1u);
			float ff = plan.ff[s];

			for (; i < plan.end[s]; i++) {
				const float x0 = osc_wtbRead(a, bits, len, ph);
				const float x1 = osc_wtbRead(b, bits, len, ph);
				const float out = x0 + ff * (x1 - x0);

				depth += depthStepQ15;
				buf[i] = osc_ringSample((int16_t)out, modBuf[i], depth);
				ff += plan.step;
				ph += inc;
			}
		}
	}
	osc->phase = ph;
}
```

### OC-3 `Oscillator.c:3114-3115` — MOD the depth order in `osc_wtbRingBlock()` (P1-16)

Replace

```c
			buf[i] = osc_ringSample((int16_t)out, modBuf[i], depth);
			depth += depthStepQ15;
```

with

```c
			/* S087 P1: S085 ring law, d_i = depthQ15 + (i + 1) x step. */
			depth += depthStepQ15;
			buf[i] = osc_ringSample((int16_t)out, modBuf[i], depth);
```

Add to the comment block above the wavetable ring copies (`:3084-3089`):
`S087 P1: the depth steps before use (S085 ring law).`

### OC-4 `Oscillator.c:3036-3082` — MOD `osc_wtbFmBlendBlock()` (P1-8)

```c
/*
 * Wavetable FM carrier with frame blending (S087 P1; S087 stage E).
 *
 * What:       as osc_wtbSyncBlendBlock() for the frame plan; both frames are
 *             read at ph + (ssat(mod x fmMod) << 19), the table-FM fraction of
 *             a cycle. Without a slot, or with one frame, the one-frame FM copy
 *             runs.
 * Why:        S087 P0 A3/A20, A17 addendum; impl. §11 F8.
 * Inputs:     osc (bound to a wavetable), mod[size], buf, size, gain.
 * Outputs:    buf; osc->phase.
 * Accessors:  osc_renderTypedFm().
 * Affiliates: osc_wtbPlanBlock(), osc_wtbFmBlock(), calcFmBlock() (the same
 *             FM scale), modNode_interpClaim().
 */
static OSC_S087_LOOP void osc_wtbFmBlendBlock(OscInfo* osc,
                                              const int16_t* mod,
                                              int16_t* buf, uint8_t size,
                                              float gain)
{
	const wavetable_catalogue_entry_t *wt =
		sampleMemory_wavetableEntry(osc->sampleCacheIndex);
	uint32_t ph = osc->phase;
	const uint32_t inc = osc->phaseInc;
	const uint32_t count = wt ? wt->frame_count : 0u;
	osc_wtbPlan_t plan;
	uint8_t i = 0u;
	uint8_t s;

	if (!wt || !osc->sampleCacheData || count < 2u ||
	    !modNode_interpClaim(osc)) {
		osc_wtbFmBlock(osc, mod, buf, size, gain);
		return;
	}
	{
		const uint8_t bits = wt->bits;
		const uint32_t len = wt->frame_len;

		osc_wtbPlanBlock(osc, count, size, &plan);
		for (s = 0u; s < plan.segments; s++) {
			const uint8_t *a = osc_wtbFrame(osc, wt, plan.pair[s]);
			const uint8_t *b = osc_wtbFrame(osc, wt, plan.pair[s] + 1u);
			float ff = plan.ff[s];

			for (; i < plan.end[s]; i++) {
				const uint32_t rp = ph + ((uint32_t)__SSAT(
					(int32_t)((float)mod[i] * osc->fmMod), 16) << 19);
				const float x0 = osc_wtbRead(a, bits, len, rp);
				const float x1 = osc_wtbRead(b, bits, len, rp);
				const float out = x0 + ff * (x1 - x0);

				buf[i] = (int16_t)(out * gain);
				ff += plan.step;
				ph += inc;
			}
		}
	}
	osc->phase = ph;
}
```

### OC-5 `Oscillator.c:2954-2996` — MOD `osc_wtbBlendBlock()` (P1-8)

```c
/*
 * Wavetable plain render with frame blending (S087 P1; S087 stage E).
 *
 * What:       with a granted interpolation slot, renders the block in at most
 *             two segments planned by osc_wtbPlanBlock(), each blending one
 *             frame pair with ff ramping through the fractional frame position.
 *             A pos held between two frames plays their mix. Without a slot,
 *             or with one frame, the one-frame copy plays the nearest frame
 *             (the Q9 fallback).
 * Why:        S087 P0 A3/A7/A20: wavetables interpolate the frame index, always
 *             while the global setting is on, until both slots are taken;
 *             impl. §11 F8: the frame position was rounded to whole frames, so
 *             no in-between frame was ever heard.
 * Inputs:     osc (bound to a wavetable; positionApplied/positionBendStep from
 *             osc_typedPositionApply()), buf, size, gain.
 * Outputs:    buf; osc->phase.
 * Accessors:  osc_renderTyped().
 * Affiliates: osc_wtbPlanBlock(), osc_wtbPlainBlock(), modNode_interpClaim().
 */
static OSC_S087_LOOP void osc_wtbBlendBlock(OscInfo* osc, int16_t* buf,
                                            uint8_t size, float gain)
{
	const wavetable_catalogue_entry_t *wt =
		sampleMemory_wavetableEntry(osc->sampleCacheIndex);
	uint32_t ph = osc->phase;
	const uint32_t inc = osc->phaseInc;
	const uint32_t count = wt ? wt->frame_count : 0u;
	osc_wtbPlan_t plan;
	uint8_t i = 0u;
	uint8_t s;

	if (!wt || !osc->sampleCacheData || count < 2u ||
	    !modNode_interpClaim(osc)) {
		osc_wtbPlainBlock(osc, buf, size, gain);
		return;
	}
	{
		const uint8_t bits = wt->bits;
		const uint32_t len = wt->frame_len;

		osc_wtbPlanBlock(osc, count, size, &plan);
		for (s = 0u; s < plan.segments; s++) {
			const uint8_t *a = osc_wtbFrame(osc, wt, plan.pair[s]);
			const uint8_t *b = osc_wtbFrame(osc, wt, plan.pair[s] + 1u);
			float ff = plan.ff[s];

			for (; i < plan.end[s]; i++) {
				const float x0 = osc_wtbRead(a, bits, len, ph);
				const float x1 = osc_wtbRead(b, bits, len, ph);
				const float out = x0 + ff * (x1 - x0);

				buf[i] = (int16_t)(out * gain);
				ff += plan.step;
				ph += inc;
			}
		}
	}
	osc->phase = ph;
}
```

### OC-6 `Oscillator.c:2917` — ADD the fractional frame position and the block plan (after `osc_wtbFrameOf()`) (P1-8)

```c
/*
 * Fractional frame position of one pos value (S087 P1).
 *
 * What:       pos x (count - 1) / 127, in frames (0.0 .. count - 1).
 * Why:        S087 P0 A20: the frame index is the interpolation target; the
 *             blend needs the fraction, not the nearest frame (impl. §11 F8).
 * Inputs:     position (0..127), count (frames, >= 1).
 * Outputs:    frame position.
 * Accessors:  osc_wtbPlanBlock().
 * Affiliates: osc_wtbFrameOf() (the nearest frame, used by the one-frame
 *             fallback).
 */
static float osc_wtbFramePos(uint8_t position, uint32_t count)
{
	if (count <= 1u)
		return 0.0f;
	return (float)(position & 127u) * (float)(count - 1u) * (1.0f / 127.0f);
}

/*
 * One wavetable blend block, split at a frame crossing (S087 P1).
 *
 * What:       segments (1 or 2); for each segment its exclusive end sample,
 *             the lower frame of its pair, and ff at its first sample; step is
 *             the per-sample change of the frame position.
 * Why:        osc_typedPositionApply() limits one block's move to at most one
 *             frame (|d| <= 127 / (count - 1)), so a glide crosses at most one
 *             frame boundary per block; splitting there keeps every sample a
 *             blend of the two frames around its own position.
 * Accessors:  osc_wtbPlanBlock() (writes), the four blend copies (read).
 */
typedef struct {
	uint8_t  segments;
	uint8_t  end[2];
	uint32_t pair[2];
	float    ff[2];
	float    step;
} osc_wtbPlan_t;

/*
 * Plan one wavetable blend block (S087 P1).
 *
 * What:       the block glides from fs (the position at its start,
 *             positionApplied - positionBendStep) to fe (positionApplied), as
 *             fractional frames; sample i sits at fs + i x step, step =
 *             (fe - fs) / size. When the glide crosses frame floor(min) + 1
 *             strictly inside the block, the block splits at the first sample
 *             on or past the crossing. Each segment blends the pair
 *             floor(min of its first and last position), clamped to count - 2,
 *             starting at ff = its first position - that frame.
 * Why:        S087 P0 impl. §11 F8: fractional frames, with no extrapolation
 *             past a pair; a held pos (fs == fe) gives one segment with a
 *             constant ff = the fraction.
 * Inputs:     osc (positionApplied, positionBendStep), count (>= 2), size.
 * Outputs:    *plan.
 * Accessors:  osc_wtbBlendBlock(), osc_wtbFmBlendBlock(),
 *             osc_wtbRingBlendBlock(), osc_wtbSyncBlendBlock().
 * Affiliates: osc_wtbFramePos(); osc_typedPositionApply() (the one-frame cap).
 */
static void osc_wtbPlanBlock(const OscInfo* osc, uint32_t count, uint8_t size,
                             osc_wtbPlan_t* plan)
{
	const float fe = osc_wtbFramePos(osc->positionApplied, count);
	const float fs = osc_wtbFramePos(
		(uint8_t)((osc->positionApplied - (uint8_t)osc->positionBendStep) & 127u),
		count);
	const float step = (size != 0u) ? (fe - fs) / (float)size : 0.0f;
	const float lo = (fs < fe) ? fs : fe;
	const float hi = (fs < fe) ? fe : fs;
	const float cross = (float)((uint32_t)lo + 1u);
	uint8_t k = size;
	uint8_t s;

	plan->step = step;
	if (size == 0u) {
		plan->segments = 0u;
		return;
	}
	if (step != 0.0f && cross > lo && cross < hi) {
		const float t = (cross - fs) / step;
		uint32_t kk = (uint32_t)t;

		if ((float)kk < t)
			kk++;
		if (kk > 0u && kk < size)
			k = (uint8_t)kk;
	}
	plan->segments = (k < size) ? 2u : 1u;
	plan->end[0] = k;
	plan->end[1] = size;
	for (s = 0u; s < plan->segments; s++) {
		const uint8_t i0 = (s == 0u) ? 0u : k;
		const uint8_t i1 = plan->end[s];
		const float ps = fs + (float)i0 * step;
		const float pe = fs + (float)(i1 - 1u) * step;
		const float pmin = (ps < pe) ? ps : pe;
		uint32_t pair = (pmin > 0.0f) ? (uint32_t)pmin : 0u;
		float ff;

		if (pair > count - 2u)
			pair = count - 2u;
		ff = ps - (float)pair;
		if (ff < 0.0f)
			ff = 0.0f;
		else if (ff > 1.0f)
			ff = 1.0f;
		plan->pair[s] = pair;
		plan->ff[s] = ff;
	}
}
```

### OC-7 `Oscillator.c:2857` — ADD the clock clamp in `osc_buildLoopSyncList()` (after `inc %= lenPh;`) (P1-13)

```c
	/*
	 * Bring a stale clock into the loop (S087 P1).
	 *
	 * What:       a position at or past loop_end (left in DrumVoice.syncClock by
	 *             a master of another kind, in 2^32-per-cycle units) is folded
	 *             into the loop once, before the per-sample walk.
	 * Why:        S087 P0 impl. §11 F20: without it the walk subtracted one loop
	 *             per sample and published a reset on every sample for several
	 *             blocks after the master changed to an l entry.
	 * Inputs:     pos, the loop region.  Outputs: pos inside [loop_start,
	 *             loop_end) or still in the lead-in.
	 * Affiliates: Drum_calcVoiceSyncBlock() (owner of syncClock).
	 */
	if (pos >= loopEndPh)
		pos = loopStartPh + ((pos - loopStartPh) % lenPh);
```

Add to the function's comment block (`:2826-2836`):
`S087 P1: a stale clock is folded into the loop at entry (F20).`

### OC-8 `Oscillator.c:2653-2655` — MOD the reset target in `osc_slpSyncBlock()` (P1-14)

Replace

```c
	const uint32_t startPh = (uint32_t)(
		(((uint32_t)(osc->position & 127u) * frames) >> 7u) <<
		OSC_SAMPLE_PHASE_SHIFT);
```

with

```c
	/*
	 * Sync reset target (S087 P1; D9, A5).
	 *
	 * What:       the pos start point; a start at or past loop_end (a smpl loop
	 *             that ends before the file) is clamped to loop_start, as
	 *             osc_typedTriggerPhase() does at a trigger.
	 * Why:        S087 P0 impl. §11 F21: the reset and the trigger must start a
	 *             looped sample at the same point, inside its fixed loop.
	 * Affiliates: osc_typedTriggerPhase().
	 */
	const uint32_t posStartPh = (uint32_t)(
		(((uint32_t)(osc->position & 127u) * frames) >> 7u) <<
		OSC_SAMPLE_PHASE_SHIFT);
	const uint32_t startPh = (posStartPh >= loopEndPh)
		? (ls << OSC_SAMPLE_PHASE_SHIFT) : posStartPh;
```

### OC-9 `Oscillator.c:2543-2582` — MOD `osc_slpRingBlock()` (P1-5, P1-16)

```c
/*
 * slp ring carrier: one looped block (S087 P1; S087 stage D).
 *
 * What:       as osc_slpPlainBlock() (the per-block step from osc_slpBlockInc(),
 *             one upward wrap at loop_end, the seam read at loop_start), with
 *             the ring law d_i = depthQ15 + (i + 1) x depthStepQ15 and the item's
 *             gain code.
 * Why:        S087 P0 D-16, A6; impl. §11 F5 (a backward pos glide is a forward
 *             step modulo the loop, see osc_slpBlockInc()); the S085 ring law
 *             (Oscillator.h calcNextOscSampleRingBlock()).
 * Inputs:     osc (bound slp), modBuf[size], buf, size, depthQ15,
 *             depthStepQ15.
 * Outputs:    buf; osc->phase.
 * Cost:       per sample as the S087 copy before (the depth add moves).
 * Accessors:  osc_renderTypedRing().
 * Affiliates: osc_slpBlockInc(), osc_ringSample(), osc_slpPlainBlock().
 */
static OSC_S087_LOOP void osc_slpRingBlock(OscInfo* osc, const int16_t *modBuf,
                                           int16_t *buf, uint8_t size,
                                           int32_t depthQ15,
                                           int32_t depthStepQ15)
{
	const int16_t *data = (const int16_t *)osc->sampleCacheData;
	const sample_catalogue_entry_t *entry =
		sampleMemory_sampleEntry(osc->sampleCacheIndex);
	const uint32_t ls = entry ? entry->loop_start : 0u;
	const uint32_t le = entry ? entry->loop_end : osc->sampleCacheSize;
	const uint32_t len = le - ls;
	const uint32_t lenPh = len << OSC_SAMPLE_PHASE_SHIFT;
	const uint32_t loopEndPh = le << OSC_SAMPLE_PHASE_SHIFT;
	const float g = osc_bindGain(osc);
	uint32_t ph = osc->phase;
	uint32_t inc;
	int32_t depth = depthQ15;
	uint8_t i;

	if (!data || osc->sampleCacheSize == 0u || len == 0u) {
		osc_zeroBlock(buf, size);
		return;
	}
	inc = osc_slpBlockInc(osc->phaseInc, ph, ls << OSC_SAMPLE_PHASE_SHIFT,
	                      lenPh);
	for (i = 0u; i < size; i++) {
		const uint32_t idx = ph >> OSC_SAMPLE_PHASE_SHIFT;
		const uint32_t next = (idx + 1u == le) ? ls : idx + 1u;
		const int16_t a = data[idx];
		const int16_t b = data[next];
		const float out = (float)a + osc_frac12(ph) * (float)(b - a);

		depth += depthStepQ15;
		buf[i] = (int16_t)((float)osc_ringSample((int16_t)out, modBuf[i], depth) * g);
		ph += inc;
		if (ph >= loopEndPh)
			ph -= lenPh;
	}
	osc->phase = ph;
}
```

### OC-10 `Oscillator.c:2526-2527` — MOD the depth order in `osc_smpRingBlock()` (P1-16)

Replace

```c
		buf[i] = (int16_t)((float)osc_ringSample((int16_t)out, modBuf[i], depth) * g);
		depth += depthStepQ15;
```

with

```c
		/* S087 P1: S085 ring law, d_i = depthQ15 + (i + 1) x step. */
		depth += depthStepQ15;
		buf[i] = (int16_t)((float)osc_ringSample((int16_t)out, modBuf[i], depth) * g);
```

Add to its comment block (`:2493-2502`):
`S087 P1: the depth steps before use (S085 ring law).`

### OC-11 `Oscillator.c:2428-2491` — MOD `osc_slpFmBlock()` (P1-5, P1-6)

```c
/*
 * slp FM carrier: one looped block (S087 P1; S087 stage D).
 *
 * What:       the read position is the phase + (ssat(mod x fmMod) << 9).
 *             The block's read rule is chosen once, from where the block
 *             starts:
 *             - inside the loop (phase >= loop_start): the offset is bounded to
 *               half a loop and the read, relative to loop_start, wraps once
 *               each way; the index is loop_start + that relative point;
 *             - in the lead-in (phase < loop_start): the absolute read is
 *               clamped to [0, loop_end - 1].
 *             The seam reads loop_start. The phase takes the per-block step from
 *             osc_slpBlockInc() and wraps once at loop_end.
 * Why:        S087 P0 D-14, A5, A6; impl. §11 F6: the read was relative to
 *             loop_start but indexed data[] as absolute, so a smpl loop that
 *             does not start at 0 played the wrong region, and the lead-in read
 *             froze at loop_start; impl. §11 F5 (backward glides).
 * Inputs:     osc (bound slp), mod[size], buf, size, gain.
 * Outputs:    buf; osc->phase.
 * Edge:       a block that starts in the lead-in and enters the loop keeps
 *             the lead-in read rule (clamp) for its remaining samples.
 * Accessors:  osc_renderTypedFm().
 * Affiliates: osc_slpBlockInc(), osc_slpPlainBlock(), osc_smpFmBlock() (the
 *             same FM scale, A17 addendum).
 */
static OSC_S087_LOOP void osc_slpFmBlock(OscInfo* osc, const int16_t *mod,
                                         int16_t *buf, uint8_t size, float gain)
{
	const int16_t *data = (const int16_t *)osc->sampleCacheData;
	const sample_catalogue_entry_t *entry =
		sampleMemory_sampleEntry(osc->sampleCacheIndex);
	const uint32_t ls = entry ? entry->loop_start : 0u;
	const uint32_t le = entry ? entry->loop_end : osc->sampleCacheSize;
	const uint32_t len = le - ls;
	const uint32_t lenPh = len << OSC_SAMPLE_PHASE_SHIFT;
	const uint32_t loopStartPh = ls << OSC_SAMPLE_PHASE_SHIFT;
	const uint32_t loopEndPh = le << OSC_SAMPLE_PHASE_SHIFT;
	const float g = gain * osc_bindGain(osc);
	uint32_t ph = osc->phase;
	uint32_t inc;
	uint8_t i;

	if (!data || osc->sampleCacheSize == 0u || len == 0u) {
		osc_zeroBlock(buf, size);
		return;
	}
	inc = osc_slpBlockInc(osc->phaseInc, ph, loopStartPh, lenPh);
	if (ph >= loopStartPh) {
		const int32_t loopLen = (int32_t)lenPh;
		const int32_t half = loopLen >> 1;

		for (i = 0u; i < size; i++) {
			int32_t off = (int32_t)__SSAT(
				(int32_t)((float)mod[i] * osc->fmMod), 16) << OSC_FM_SAMPLE_SHIFT;
			int32_t rp;
			uint32_t idx;
			uint32_t next;
			int16_t a;
			int16_t b;
			float out;

			off = (off > half) ? half : off;
			off = (off < -half) ? -half : off;
			rp = (int32_t)(ph - loopStartPh) + off;
			rp = (rp < 0) ? rp + loopLen : rp;
			rp = (rp >= loopLen) ? rp - loopLen : rp;
			idx = ls + ((uint32_t)rp >> OSC_SAMPLE_PHASE_SHIFT);
			next = (idx + 1u == le) ? ls : idx + 1u;
			a = data[idx];
			b = data[next];
			out = (float)a + osc_frac12((uint32_t)rp) * (float)(b - a);
			buf[i] = (int16_t)(out * g);
			ph += inc;
			if (ph >= loopEndPh)
				ph -= lenPh;
		}
	} else {
		const int32_t lastPh = (int32_t)loopEndPh - 1;

		for (i = 0u; i < size; i++) {
			const int32_t off = (int32_t)__SSAT(
				(int32_t)((float)mod[i] * osc->fmMod), 16) << OSC_FM_SAMPLE_SHIFT;
			int32_t rp = (int32_t)ph + off;
			uint32_t idx;
			uint32_t next;
			int16_t a;
			int16_t b;
			float out;

			rp = (rp < 0) ? 0 : rp;
			rp = (rp > lastPh) ? lastPh : rp;
			idx = (uint32_t)rp >> OSC_SAMPLE_PHASE_SHIFT;
			next = (idx + 1u == le) ? ls : idx + 1u;
			a = data[idx];
			b = data[next];
			out = (float)a + osc_frac12((uint32_t)rp) * (float)(b - a);
			buf[i] = (int16_t)(out * g);
			ph += inc;
			if (ph >= loopEndPh)
				ph -= lenPh;
		}
	}
	osc->phase = ph;
}
```

### OC-12 `Oscillator.c:2333-2368` — MOD `osc_slpPlainBlock()` (P1-5)

```c
/*
 * slp plain: one looped block (S087 P1; S087 stage D).
 *
 * What:       Q19.12 linear read of the bound item with a fixed loop region:
 *             the seam reads loop_start (A6); the phase takes the per-block
 *             step from osc_slpBlockInc() and wraps once at loop_end.
 * Why:        S087 P0 D-12, A5, A6; impl. §11 F5: a backward pos glide made the
 *             block increment negative and the unsigned modulo turned it into an
 *             arbitrary forward step. osc_slpBlockInc() now returns a backward
 *             step inside the loop as the equal forward step modulo the loop
 *             (and the signed step in the lead-in), so the single upward wrap
 *             stays correct.
 * Inputs:     osc (bound slp), buf, size, gain.
 * Outputs:    buf; osc->phase.
 * Cost:       per sample as the S087 copy before.
 * Accessors:  osc_renderTyped(), osc_renderUserList().
 * Affiliates: osc_slpBlockInc(), osc_userBind(), osc_typedPositionApply().
 */
static OSC_S087_LOOP void osc_slpPlainBlock(OscInfo* osc, int16_t *buf,
                                            uint8_t size, float gain)
{
	const int16_t *data = (const int16_t *)osc->sampleCacheData;
	const sample_catalogue_entry_t *entry =
		sampleMemory_sampleEntry(osc->sampleCacheIndex);
	const uint32_t ls = entry ? entry->loop_start : 0u;
	const uint32_t le = entry ? entry->loop_end : osc->sampleCacheSize;
	const uint32_t len = le - ls;
	const uint32_t lenPh = len << OSC_SAMPLE_PHASE_SHIFT;
	const uint32_t loopEndPh = le << OSC_SAMPLE_PHASE_SHIFT;
	const float g = gain * osc_bindGain(osc);
	uint32_t ph = osc->phase;
	uint32_t inc;
	uint8_t i;

	if (!data || osc->sampleCacheSize == 0u || len == 0u) {
		osc_zeroBlock(buf, size);
		return;
	}
	inc = osc_slpBlockInc(osc->phaseInc, ph, ls << OSC_SAMPLE_PHASE_SHIFT,
	                      lenPh);
	for (i = 0u; i < size; i++) {
		const uint32_t idx = ph >> OSC_SAMPLE_PHASE_SHIFT;
		const uint32_t next = (idx + 1u == le) ? ls : idx + 1u;
		const int16_t a = data[idx];
		const int16_t b = data[next];
		const float out = (float)a + osc_frac12(ph) * (float)(b - a);

		buf[i] = (int16_t)(out * g);
		ph += inc;
		if (ph >= loopEndPh)
			ph -= lenPh;
	}
	osc->phase = ph;
}
```

### OC-13 `Oscillator.c:2283` — ADD `osc_slpBlockInc()` (after `osc_bindGain()`) (P1-5)

```c
/*
 * Per-block step of a looped sample (S087 P1).
 *
 * What:       the block's phase step (phaseInc, possibly bent negative by a pos
 *             glide), returned as the unsigned value the slp copies add each
 *             sample:
 *             - forward: reduced modulo the loop length when it is not shorter
 *               than the loop (A5);
 *             - backward, with the block starting inside the loop: the equal
 *               forward step modulo the loop, loop length - (magnitude modulo the
 *               loop length), so the single upward wrap at loop_end stays exact;
 *             - backward, with the block starting in the lead-in: the signed
 *               step unchanged; osc_typedPositionApply() keeps the block's last
 *               phase >= 0, and a backward step never reaches loop_end.
 * Why:        S087 P0 A2/A5 (pos moves the playhead, which wraps into the fixed
 *             loop); impl. §11 F5: the unsigned modulo of a negative step gave
 *             an arbitrary forward step. Converting once per block keeps the
 *             per-sample loop unchanged (no second wrap).
 * Inputs:     phaseInc (bent for this block), phase (block start),
 *             loopStartPh, lenPh (Q19.12, lenPh > 0).
 * Outputs:    the step to add per sample (two's complement when backward in the
 *             lead-in).
 * Accessors:  osc_slpPlainBlock(), osc_slpFmBlock(), osc_slpRingBlock().
 * Affiliates: osc_typedPositionApply() (the bend and the lead-in limit);
 *             SAMPLE_MAX_FRAMES (every step and phase fit int32).
 */
static inline uint32_t osc_slpBlockInc(uint32_t phaseInc, uint32_t phase,
                                       uint32_t loopStartPh, uint32_t lenPh)
{
	const int32_t inc = (int32_t)phaseInc;
	uint32_t mag;

	if (inc >= 0)
		return (phaseInc >= lenPh) ? (phaseInc % lenPh) : phaseInc;
	if (phase < loopStartPh)
		return phaseInc;
	mag = (uint32_t)(-(int64_t)inc) % lenPh;
	return (mag == 0u) ? 0u : (lenPh - mag);
}
```

### OC-14 `Oscillator.c:2161-2236` — MOD `osc_typedPositionApply()` and its comment block (P1-5)

```c
/*
 * Typed pos glide for one block (S087 P1; S087 stage D/E).
 *
 * What:       wtb: d = position - positionApplied, linear, capped so the
 *             block moves at most one frame (|d| <= 127 / (count - 1)); the
 *             frame position moves, phaseInc never bends.
 *             smp/slp: d = position - positionApplied (linear, D3), 0 once a
 *             one-shot has ended (D4, no revival), capped to the D5 speed limit
 *             (|d| x frames <= OSC_POS_SAMPLE_SPEED_CAP x phaseInc, at least 1).
 *             A backward step is further limited while the playhead is outside
 *             a loop (smp, or slp still in its lead-in) so the block's last
 *             phase stays >= 0: phase + size x (phaseInc + d x frames) >= 0.
 *             positionApplied += d; when `bend`, phaseInc += d x frames and
 *             positionBendStep = d, else positionBendStep = 0 (Drum snc).
 * Why:        S087 P0 D1/D3/D4/D5; impl. §11 F16: the old limit (points / 32)
 *             ignored the sample length. Inside a loop a backward glide is a
 *             forward step modulo the loop (osc_slpBlockInc()), so no limit
 *             applies there.
 * Inputs:     osc (bound), bend.
 * Outputs:    phaseInc, positionApplied, positionBendStep.
 * Accessors:  osc_positionBendApply() (typed branch).
 * Affiliates: osc_typedPositionRestore(); OSC_POS_SAMPLE_SPEED_CAP;
 *             osc_wtbPlanBlock() (reads the wtb step); OUTPUT_DMA_SIZE.
 */
void osc_typedPositionApply(OscInfo* osc, uint8_t bend)
{
	const uint32_t frames = osc->sampleCacheSize;
	int32_t d;
	int32_t maxStep;

	if (osc->oscType == OSC_TYPE_WTB) {
		const wavetable_catalogue_entry_t *wt =
			sampleMemory_wavetableEntry(osc->sampleCacheIndex);
		const int32_t count = wt ? (int32_t)wt->frame_count : 0;

		d = (int32_t)osc->position - (int32_t)osc->positionApplied;
		maxStep = (count > 1) ? (int32_t)(127 / (count - 1)) : 1;
		if (maxStep < 1)
			maxStep = 1;
		if (d > maxStep)
			d = maxStep;
		else if (d < -maxStep)
			d = -maxStep;
		osc->positionApplied =
			(uint8_t)(((uint32_t)osc->positionApplied + (uint32_t)d) & 127u);
		osc->positionBendStep = (int8_t)d;
		return;
	}
	d = (int32_t)osc->position - (int32_t)osc->positionApplied;
	if (frames == 0u) {
		osc->positionBendStep = 0;
		return;
	}
	if (osc->oscType == OSC_TYPE_SMP &&
	    osc->phase >= (frames << OSC_SAMPLE_PHASE_SHIFT)) {
		d = 0;   /* D4: a finished one-shot is not revived by pos */
	}
	maxStep = (int32_t)(((int64_t)OSC_POS_SAMPLE_SPEED_CAP *
	                     (int64_t)osc->phaseInc) / (int64_t)frames);
	if (maxStep < 1)
		maxStep = 1;
	if (d > maxStep)
		d = maxStep;
	else if (d < -maxStep)
		d = -maxStep;
	if (d < 0) {
		const sample_catalogue_entry_t *entry =
			(osc->oscType == OSC_TYPE_SLP)
			? sampleMemory_sampleEntry(osc->sampleCacheIndex)
			: (const sample_catalogue_entry_t *)0;
		const uint32_t loopStartPh = entry
			? (entry->loop_start << OSC_SAMPLE_PHASE_SHIFT) : 0u;

		if (osc->oscType == OSC_TYPE_SMP || osc->phase < loopStartPh) {
			const int64_t room = (int64_t)osc->phase +
				(int64_t)OUTPUT_DMA_SIZE * (int64_t)osc->phaseInc;
			const int64_t perStep = (int64_t)OUTPUT_DMA_SIZE * (int64_t)frames;
			const int32_t minD = (room > 0) ? (int32_t)(-(room / perStep)) : 0;

			if (d < minD)
				d = minD;
		}
	}
	osc->positionApplied =
		(uint8_t)(((uint32_t)osc->positionApplied + (uint32_t)d) & 127u);
	if (bend) {
		osc->phaseInc += (uint32_t)(d * (int32_t)frames);
		osc->positionBendStep = (int8_t)d;
	} else {
		osc->positionBendStep = 0;
	}
}
```

### OC-15 `Oscillator.c:2141` — ADD the bind in `osc_typedTriggerPhase()` (after `return 0u;` of the `!osc_isTyped()` test) (P1-10)

```c
	/*
	 * Bind before the start is computed (S087 P1).
	 *
	 * What:       re-reads the bound item when the wav value, the type or the
	 *             catalogue changed since the last block.
	 * Why:        S087 P0 impl. §11 F10: a step's own automation (a new sample)
	 *             is applied just before its trigger (stage A), but the bind
	 *             otherwise follows only in the next block's osc_setFreq(); the
	 *             pos start and the loop clamp below used the previous item's
	 *             length, and a shorter one-shot started past its end (silence).
	 * Affiliates: osc_userBind(); voiceControl_processPending() (stage A order).
	 */
	osc_userBind(osc);
```

Add to the function's comment block (`:2123-2133`):
`S087 P1: binds first, so a sample changed on the trigger's own step
starts correctly.`

### OC-16 `Oscillator.c:2021-2121` — MOD `osc_userBind()` and its comment block (P1-7, P1-9)

```c
/*
 * Bind the oscillator's catalogue item for this block (S087 P1; S087 stage
 * D/E; see the header).
 *
 * What:       resolves (kind, index) from oscType (osc1: smp/slp/wtb, the index
 *             clamped to the last item) or from the modulator's flat list
 *             (stock, s, l, w). A wavetable's kind is resolved to WTB8 or WTB16
 *             from its entry BEFORE the comparison with the stored bind. When
 *             kind, index or the catalogue generation differ, re-reads the item
 *             (base address, frames or frame length) and re-anchors (D7):
 *             positionApplied = position; a smp phase past the new end is
 *             clamped to the end; a slp phase past the loop end is folded into
 *             the loop once. Every block: phaseInc = the cycle law (2^32 per
 *             cycle, freq2PhaseIncr()) for a wavetable bind or a wtb osc1, else
 *             the sample law f x 2^22 / REAL_FS (Q19.12).
 * Why:        one bind for osc1 and the modulators (S087 P0 §6.3, D7, A23, A24).
 *             impl. §11 F9: an 8-bit table compared its stored WTB8 with a
 *             provisional WTB16 and rebound every block (no frame glide).
 *             impl. §11 F7: a modulator w entry (oscType MODLIST) got the
 *             sample law, 1,024 x too slow; its sync clock was wrong likewise.
 * Inputs:     osc, the catalogue.  Outputs: bind fields, phaseInc,
 *             positionApplied/phase on a change.
 * Accessors:  osc_setFreq(), osc_setType(), osc_typedTriggerPhase().
 * Affiliates: SampleMemory.h; osc_userCount()/osc_userListCount();
 *             osc_syncClockInc() (returns a wavetable master's phaseInc as is).
 */
void osc_userBind(OscInfo* osc)
{
	const uint32_t generation = sampleMemory_getGeneration();
	const float currentFreq = osc->freq * osc->pitchMod * osc->modNodeValue;
	uint8_t kind = OSC_BIND_NONE;
	uint8_t index = 0u;
	uint8_t count;

	if (osc->oscType == OSC_TYPE_SMP || osc->oscType == OSC_TYPE_SLP) {
		count = osc_userCount(osc->oscType);
		if (count != 0u) {
			index = (uint8_t)(osc->waveform < count ? osc->waveform
			                                        : count - 1u);
			kind = (osc->oscType == OSC_TYPE_SMP) ? OSC_BIND_SMP
			                                      : OSC_BIND_SLP;
		}
	} else if (osc->oscType == OSC_TYPE_WTB) {
		count = osc_userCount(OSC_TYPE_WTB);
		if (count != 0u) {
			index = (uint8_t)(osc->waveform < count ? osc->waveform
			                                        : count - 1u);
			kind = OSC_BIND_WTB16;
		}
	} else if (osc->oscType == OSC_TYPE_MODLIST &&
	           osc->waveform >= OSC_SAMPLE_START) {
		const uint8_t samples = sampleMemory_getNumSamples();
		const uint8_t entry = (uint8_t)(osc_userListCount() - 1u);
		const uint8_t wanted = (uint8_t)(osc->waveform < entry ? osc->waveform
		                                                       : entry);
		const uint8_t list = (uint8_t)(wanted - OSC_SAMPLE_START);

		if (list < samples) {
			kind = OSC_BIND_SMP;
			index = list;
		} else if (list < (uint8_t)(2u * samples)) {
			kind = OSC_BIND_SLP;
			index = (uint8_t)(list - samples);
		} else if (list < (uint8_t)(2u * samples +
		                            sampleMemory_getNumWavetables())) {
			kind = OSC_BIND_WTB16;
			index = (uint8_t)(list - 2u * samples);
		}
	}

	/* F9: the stored kind of a wavetable is its entry's depth. */
	if (kind == OSC_BIND_WTB16) {
		const wavetable_catalogue_entry_t *w =
			sampleMemory_wavetableEntry(index);

		if (!w) {
			kind = OSC_BIND_NONE;
			index = 0u;
		} else if (w->bits == 8u) {
			kind = OSC_BIND_WTB8;
		}
	}

	if (kind != osc->sampleCacheLooped ||
	    index != osc->sampleCacheIndex ||
	    generation != osc->sampleCacheGeneration) {
		const sample_catalogue_entry_t *entry = (kind == OSC_BIND_SMP ||
		                                        kind == OSC_BIND_SLP)
			? sampleMemory_sampleEntry(index)
			: (const sample_catalogue_entry_t *)0;
		const wavetable_catalogue_entry_t *wentry = (kind == OSC_BIND_WTB8 ||
		                                             kind == OSC_BIND_WTB16)
			? sampleMemory_wavetableEntry(index)
			: (const wavetable_catalogue_entry_t *)0;

		osc->sampleCacheIndex = index;
		osc->sampleCacheLooped = kind;
		osc->sampleCacheGeneration = generation;
		osc->sampleCacheSize = entry ? entry->frames
			: (wentry ? wentry->frame_len : 0u);
		osc->sampleCacheData = entry
			? (const int16_t *)SAMPLE_FLASH_PTR(entry->offset)
			: (wentry ? (const int16_t *)SAMPLE_FLASH_PTR(wentry->offset)
			          : (const int16_t *)0);
		osc->positionApplied = osc->position;
		if (entry) {
			const uint32_t endPh = entry->frames << OSC_SAMPLE_PHASE_SHIFT;
			const uint32_t loopStartPh = entry->loop_start << OSC_SAMPLE_PHASE_SHIFT;
			const uint32_t loopEndPh = entry->loop_end << OSC_SAMPLE_PHASE_SHIFT;
			const uint32_t loopLenPh = loopEndPh - loopStartPh;

			if (kind == OSC_BIND_SMP) {
				if (osc->phase > endPh)
					osc->phase = endPh;
			} else if (loopLenPh != 0u && osc->phase >= loopEndPh) {
				osc->phase = loopStartPh +
					((osc->phase - loopStartPh) % loopLenPh);
			}
		}
	}
	/* F7: the increment law follows what is bound, not the oscillator role. */
	osc->phaseInc = (kind == OSC_BIND_WTB8 || kind == OSC_BIND_WTB16 ||
	                 osc->oscType == OSC_TYPE_WTB)
		? freq2PhaseIncr(currentFreq)
		: (uint32_t)((currentFreq * 4194304.0f) / REAL_FS);
}
```

### OC-17 `Oscillator.c:1953` — ADD the slot release in `osc_setType()` (after `osc->freqCacheValid = 0u;`) (P1-12)

```c
	/*
	 * Release a held interpolation slot (S087 P1).
	 *
	 * What:       clears interpStamp, so the oscillator is no longer counted as
	 *             last block's slot holder.
	 * Why:        S087 P0 impl. §11 F17 (the header promised it); a stale stamp
	 *             could take a reservation that belongs to a real holder.
	 * Affiliates: modNode_interpClaim(); OscInfo.interpStamp.
	 */
	osc->interpStamp = 0u;
```

### OC-18 `Oscillator.c:1846-1848` — MOD the `default:` of `osc_setFreq()` (P1-17)

Replace

```c
		//samples
		default:
			osc_calcUserSampleFreqValue(osc, currentFreq);
```

with

```c
		/*
		 * S087 P1: user content never reaches this switch (osc_userBind()
		 * above sets its increment); the default only guards an impossible
		 * value with the table law.
		 */
		default:
			osc_calcWavetableFreqValue(osc, currentFreq);
```

### OC-19 `Oscillator.c:1787` — ADD the modulator list clamp in `osc_setFreq()` (after `osc->waveform = OSC_STOCK_LAST;`) (P1-15)

```c
		/*
		 * Modulator list clamp (S087 P1).
		 *
		 * What:       a modulator wav value past the end of its flat list (stock,
		 *             s, l, w; osc_userListCount()) is clamped to the last
		 *             entry.
		 * Why:        S087 P0 r8 / A23: the value is used as it is, clamped to an
		 *             entry that exists; a value past the list bound no item and
		 *             the modulator was silent.
		 * Inputs:     oscType, waveform, the catalogue counts.
		 * Outputs:    waveform.
		 * Affiliates: osc_userListCount(); osc_userBind().
		 */
		if (osc->oscType == OSC_TYPE_MODLIST) {
			const uint8_t last = (uint8_t)(osc_userListCount() - 1u);

			if (osc->waveform > last)
				osc->waveform = last;
		}
```

### OC-20 `Oscillator.c:1765-1769` — DEL `osc_calcUserSampleFreqValue()` (P1-17)

This deletes the separator line 1765 and the function at 1766-1769. Its
only caller was the `default:` that OC-18 replaced.

### OC-21 `Oscillator.c:1704-1708` — DEL the interpolation entry of `calcNextOscSampleRingBlock()` (P1-17)

This deletes:

```c
	if (osc_waveInterpActive(osc)) {
		calcPeriodicInterpRingBlock(osc, modBuf, buf, size, depthQ15, depthStepQ15);
		return;
	}

```

`wav` waveform interpolation is retired (S087 P0 A7). No claim is made
any more, so the path was unreachable.

### OC-22 `Oscillator.c:1694-1695` — MOD the ring dispatcher's comment

Replace

```c
 * Contract: Oscillator.h. It mirrors calcNextOscSampleBlock(), including the
 * user-sample range check that returns without writing buf. Every ring copy
```

with

```c
 * Contract: Oscillator.h. It mirrors calcNextOscSampleBlock()'s waveform
 * switch (S087 P1: no interpolation entry, no user samples). Every ring copy
```

### OC-23 `Oscillator.c:1572-1691` — DEL `calcUserSampleOscRingBlock()` and `calcPeriodicInterpRingBlock()` with their comment blocks (P1-17)

This deletes from the `/*` that opens "User-sample carrier with the ring
law (S085 P1)" (line 1572) through the blank line after the closing
brace of `calcPeriodicInterpRingBlock()` (line 1691).
- The v1 loop is unreferenced since stage D.
- The interpolation path is unreachable (A7). It also held two inlined
  copies of `osc_renderPlainWave()`, ≈ 1.3 KB of ITCM.

### OC-24 `Oscillator.c:1421-1424` — DEL the interpolation entry of `calcNextOscSampleSyncBlock()` (P1-17)

This deletes:

```c
	if (osc_waveInterpActive(osc)) {
		calcPeriodicInterpSyncBlock(osc, over, ratioQ16, depthQ30, buf, size);
		return;
	}
```

### OC-25 `Oscillator.c:1411-1412` — MOD the sync dispatcher's comment

Replace

```c
 * Contract: Oscillator.h. Order: ratio (one VDIV), then the interpolation
 * entry (as calcNextOscSampleBlock()), then the waveform switch.
```

with

```c
 * Contract: Oscillator.h. Order: ratio (one VDIV), then the waveform switch
 * (S087 P1: the waveform-interpolation entry is retired, A7).
```

### OC-26 `Oscillator.c:1356-1408` — DEL `calcPeriodicInterpSyncBlock()` with its comment block (P1-17)

This deletes from the `/*` of "Waveform-interpolated slave under sync
(S085 P1)" (1356) through the blank line after its closing brace (1408).

### OC-27 `Oscillator.c:1321` — MOD the comment of `osc_renderSyncWave()`

`* Accessors:  calcNextOscSampleSyncBlock(); calcPeriodicInterpSyncBlock().` →
`* Accessors:  calcNextOscSampleSyncBlock().`

### OC-28 `Oscillator.c:1114` — MOD the comment of `osc_renderPlainWave()`

`* Accessors:  osc_renderSyncWave(); calcPeriodicInterpRingBlock().` →
`* Accessors:  osc_renderSyncWave() (NOISE: unsynced).`

### OC-29 `Oscillator.c:1083-1097` — DEL `osc_syncWaveform()` with its comment block (P1-17)

Its only caller was `calcPeriodicInterpSyncBlock()` (OC-26). Delete from
the `/*` at 1083 through the blank line at 1097.

### OC-30 `Oscillator.c:993-995` — MOD the comment of `OSC_S085_DISPATCH`

Replace

```c
 *             their interpolated variants' blend loops are per-sample code
 *             and the cost gate assumes the same ITCM placement that
 *             calcPeriodicInterpBlock() gets through calcNextOscSampleBlock().
```

with

```c
 *             their waveform switches select per-sample loops, and the cost
 *             gate assumes ITCM placement for them (S087 P1: the interpolated
 *             variants are retired, A7).
```

### OC-31 `Oscillator.c:790-898` — DEL `calcUserSampleOscFmBlock()` and `calcUserSampleOscBlock()` (P1-17)

This deletes from the separator at 790 through the closing brace of
`calcUserSampleOscBlock()` at 898. The separator at 899 stays before
`calcSampleOscBlock()`. These are the v1 user-sample loops, unreferenced
since stage D.

### OC-32 `Oscillator.c:653-656` — DEL the interpolation entry of `calcNextOscSample()` (P1-17)

This deletes:

```c
	if (osc_waveInterpActive(osc)) {
		return calcPeriodicInterp(osc);
	}

```

### OC-33 `Oscillator.c:609-613` — DEL the interpolation entry of `calcNextOscSampleBlock()` (P1-17)

This deletes:

```c
	if (osc_waveInterpActive(osc)) {
		calcPeriodicInterpBlock(osc, buf, size, gain);
		return;
	}

```

### OC-34 `Oscillator.c:515-518` — DEL the interpolation entry of `calcNextOscSampleFm()` (P1-17)

This deletes:

```c
	if (osc_waveInterpActive(osc)) {
		return calcPeriodicInterpFm(osc, modOsc);
	}

```

### OC-35 `Oscillator.c:466-470` — DEL the interpolation entry of `calcNextOscSampleFmBlock()` (P1-17)

This deletes:

```c
	if (osc_waveInterpActive(osc)) {
		calcPeriodicInterpFmBlock(osc, modBuffer, buf, size, gain);
		return;
	}

```

### OC-36 `Oscillator.c:123-298` — DEL the waveform-interpolation block and the v1 sample cache (P1-17)

This deletes from `static INDTCMZ int16_t osc_interp_a[OUTPUT_DMA_SIZE];`
(123) through the separator line after `calcPeriodicInterpFm()` (298).
The separator at 122 stays before `calcSineBlock()`. The deletion covers:
- `osc_interp_a/b`, 128 B DTCM, releasing R-D1;
- the interpolation comment;
- `osc_clearWaveInterp()` and `osc_updateSampleCache()` (v1);
- `osc_waveInterpActive()` and `osc_evalWaveAtPhase()`/`Fm()`;
- `calcPeriodicInterpBlock()`, `calcPeriodicInterp()`,
  `calcPeriodicInterpFmBlock()` and `calcPeriodicInterpFm()`.

`freq2PhaseIncr32767()` (118-121) stays; CRASH uses it.

---

## 4. `Core/DSPAudio/Oscillator.h` (P1-11, P1-17)

Apply in this order: OH-1, OH-2, OH-3, OH-4, OH-5.

### OH-1 `Oscillator.h:641-642` — MOD the Affiliates of `osc_positionBendApply()`

Replace

```c
 *             calcPeriodicInterp*() (end the block at start + phaseInc x size,
 *             which includes the bend); 086 log §4.3.
```

with

```c
 *             osc_typedPositionApply() (typed osc1, S087); 086 log §4.3.
```

### OH-2 `Oscillator.h:311-314` — DEL the v1 user-sample declarations

This deletes:

```c
void calcUserSampleOscFmBlock(OscInfo* osc,int16_t* modBuffer, int16_t* buf, uint8_t size ,const float gain);
//-----------------------------------------------------------
void calcUserSampleOscBlock(OscInfo* osc, int16_t* buf, const uint8_t size ,const float gain);
//-----------------------------------------------------------
```

The functions are deleted by OC-31.

### OH-3 `Oscillator.h:214-215` — MOD the comments of `waveInterpFrac` / `waveInterpGeneration`

```c
	float		waveInterpFrac;			/* S087 P1: unused (wav interpolation retired, A7); kept for the OscInfo layout */
	uint32_t	waveInterpGeneration;	/* S087 P1: unused; kept for the OscInfo layout */
```

### OH-4 `Oscillator.h:204-205` — MOD the What of `interpStamp`

Replace

```c
	 * What:       low 8 bits of the block generation in which this oscillator
	 *             last held one of the two interpolation slots; 0 = none.
```

with

```c
	 * What:       the stamp (modNode_interpStampOf(), 1..255) of the block in
	 *             which this oscillator last held one of the two interpolation
	 *             slots; 0 = none (S087 P1: a stamp is never 0).
```

### OH-5 `Oscillator.h:166` — MOD the comment of `waveInterpNext`

```c
	uint8_t		waveInterpNext;			/* S087 P1: unused (A7); kept: positionBendStep, oscType and interpStamp follow it in the padding */
```

---

## 5. `Core/SampleRom/SampleMemory.c` / `.h` (P1-17)

### SM-1 `SampleMemory.c:238-260` — DEL the v1 read shims

This deletes the `/* S087 C (interim) … */` line, `sampleMemory_getSampleInfo()`,
`sampleMemory_isLooped()` and the blank line after it. Their only caller,
`osc_updateSampleCache()`, is deleted by OC-36.

### SM-2 `SampleMemory.c:45-46` — DEL `stub_silence_buffer`

This deletes `static const int16_t stub_silence_buffer[256] = { 0 };` and
the blank line after it. Its only user was SM-1. This saves 512 B of
flash.

### SM-3 `SampleMemory.h:240-249` — DEL the v1 `SampleInfo` type and declarations

This deletes the blank line, the `/* S087 C (interim) … */` line, the
`SampleInfo` typedef, and the `sampleMemory_getSampleInfo()` and
`sampleMemory_isLooped()` declarations.

---

## 6. `Core/DSPAudio/modulationNode.c` / `.h` (P1-11, P1-17)

**The stamp rule:**
- a block's stamp is `(generation % 255) + 1`, which is never 0;
- consecutive generations always differ, including across the 32-bit
  wrap that skips 0;
- the previous block's stamp is taken **before** the generation advances.

Apply in this order: MN-1 … MN-7.

### MN-1 `modulationNode.c:468-477` — MOD the start of `modNode_resetTargets()`

Replace lines 468-477 (from `void modNode_resetTargets()` through
`modNode_waveInterpActiveCount = 0u;`) with:

```c
void modNode_resetTargets()
{
	uint8_t i;
	/*
	 * Slot reservations for the new block (S087 P1; S087 stage E).
	 *
	 * What:       records the stamp of the block that just ended, advances the
	 *             generation (skipping 0), and reserves last block's grants for
	 *             their holders before clearing the grant count.
	 * Why:        sticky claims (S087 P0 A8). impl. §11 F11: the stamp was the
	 *             generation's low byte, 0 every 256 blocks, which is also
	 *             "no slot"; both holders lost their slots for one block.
	 * Affiliates: modNode_interpClaim(), modNode_interpStampOf().
	 */
	modNode_interpLastStamp = modNode_interpStampOf(modNode_waveInterpGeneration);
	modNode_waveInterpGeneration++;
	if (modNode_waveInterpGeneration == 0u)
		modNode_waveInterpGeneration = 1u;
	modNode_interpReserved = modNode_waveInterpActiveCount;
	modNode_waveInterpActiveCount = 0u;
```

The remaining lines of the function (the velocity and LFO restores,
478 onward) are unchanged.

### MN-2 `modulationNode.c:87-120` — MOD `modNode_interpClaim()` and its comment block

```c
/*
 * Claim one of the two interpolation slots for this block (S087 P1; S087
 * stage E; see the header).
 *
 * What:       with the global toggle on: an oscillator already granted in this
 *             block keeps its grant; a holder of last block (its stamp equals
 *             modNode_interpLastStamp) takes one of the reserved slots; a
 *             newcomer is granted only when grants plus unclaimed reservations
 *             leave room under OSC_WAVE_INTERP_MAX_ACTIVE. A grant stamps the
 *             oscillator with this block's stamp (1..255).
 * Why:        S087 P0 A3/A7/A8: wavetable blending takes one of the two slots
 *             and steps without one; sticky grants avoid block-rate flipping;
 *             S088's run mode shares the slots. impl. §11 F11: stamps are never
 *             0, so a holder is always recognised.
 * Inputs:     osc (OscInfo*); the generation, the counters.
 * Outputs:    grant flag; osc->interpStamp; the counters.
 * Accessors:  osc_wtbBlendBlock(), osc_wtbFmBlendBlock(),
 *             osc_wtbRingBlendBlock(), osc_wtbSyncBlendBlock().
 * Affiliates: modNode_resetTargets(), modNode_interpStampOf();
 *             OscInfo.interpStamp; osc_setType() (clears the stamp).
 */
uint8_t modNode_interpClaim(void* oscp)
{
	OscInfo* osc = (OscInfo*)oscp;
	const uint8_t stamp = modNode_interpStampOf(modNode_waveInterpGeneration);

	if (!osc || !modNode_waveInterpEnabled)
		return 0u;
	if (osc->interpStamp == stamp)
		return 1u;
	if (osc->interpStamp == modNode_interpLastStamp &&
	    modNode_interpReserved != 0u) {
		modNode_interpReserved--;
		modNode_waveInterpActiveCount++;
		osc->interpStamp = stamp;
		return 1u;
	}
	if ((uint16_t)modNode_waveInterpActiveCount + modNode_interpReserved <
	    OSC_WAVE_INTERP_MAX_ACTIVE) {
		modNode_waveInterpActiveCount++;
		osc->interpStamp = stamp;
		return 1u;
	}
	return 0u;
}
```

### MN-3 `modulationNode.c:77-86` — DEL `modNode_getMaxWaveformIndex()`

This deletes the function and the blank line after it. Its last caller is
changed by IM-1; its value (8 + the v1 sample count − 1) describes the
retired v1 list.

### MN-4 `modulationNode.c:71-76` — DEL `modNode_getWaveTargetOsc()`

This deletes the function and the blank line after it. It has been
unused since the `wav` interpolation claim was removed (an S087
compiler warning).

### MN-5 `modulationNode.c:69` — MOD the last-generation counter to a stamp; ADD `modNode_interpStampOf()`

Replace line 69 (`static INCCMZ uint32_t modNode_interpLastGen = 0u;`) with:

```c
/*
 * Stamp of the block that just ended (S087 P1).
 *
 * What:       modNode_interpStampOf() of the previous block's generation, taken
 *             by modNode_resetTargets() before the generation advances.
 * Why:        a holder of last block is recognised by this stamp (sticky
 *             claims, A8); one byte is the approved R-E1 size (impl. §11 F18:
 *             a uint32_t made R-E1 +5 B).
 * Inputs:     modNode_resetTargets().  Outputs: read by modNode_interpClaim().
 * RAM:        1 B INCCMZ (R-E1, with modNode_interpReserved: +2 B).
 */
static INCCMZ uint8_t modNode_interpLastStamp = 0u;

/*
 * Slot stamp of a block generation (S087 P1).
 *
 * What:       (generation % 255) + 1: 1..255, never 0; consecutive generations
 *             (also across the 32-bit wrap that skips 0) give different stamps.
 * Why:        OscInfo.interpStamp uses 0 for "no slot" (impl. §11 F11).
 * Inputs:     generation.  Outputs: stamp.
 * Accessors:  modNode_resetTargets(), modNode_interpClaim().
 * Affiliates: OscInfo.interpStamp.
 */
static inline uint8_t modNode_interpStampOf(uint32_t generation)
{
	return (uint8_t)((generation % 255u) + 1u);
}
```

### MN-6 `modulationNode.h:243-254` — MOD the comment block of `modNode_interpClaim()`

Replace lines 246-249 with:

```c
 * What:       returns 1 and stamps osc->interpStamp with this block's stamp
 *             (1..255) when a slot is granted; 0 otherwise. The global toggle
 *             off grants nothing; a grant already made this block is kept; a
 *             holder of last block keeps a reserved slot; a newcomer needs a
 *             free slot (S087 P1).
```

### MN-7 `modulationNode.h:232-242` — DEL the declaration of `modNode_getMaxWaveformIndex()` with its comment block

The function is deleted by MN-3.

---

## 7. `Core/DSP/Instruments/InstrumentManager.c` (P1-17)

Apply in this order: IM-1, IM-2.

### IM-1 `InstrumentManager.c:812` — MOD the fallback of `instrumentManager_waveDomainMax()`

`    return modNode_getMaxWaveformIndex();` →
`    return OSC_STOCK_LAST;   /* S087 P1: only wave rows reach here (above); the stock list is the safe range */`

### IM-2 `InstrumentManager.c:768-779` — MOD the comment block of `instrumentManager_waveDomainMax()`

```c
/*
 * Highest legal value of an oscillator wave row (S087 stage D; S087 P1).
 *
 * What:       osc1_wave: the last entry of osc1's current type (wav:
 *             OSC_STOCK_LAST; smp/slp: samples - 1; wtb: tables - 1; 0 when
 *             none); a modulator wave row: the last entry of the flat list
 *             (osc_userListCount() - 1). Any other row: OSC_STOCK_LAST.
 * Why:        LFO and velocity ranges, the menu clamp and the automation
 *             display must agree on what exists (S087 P0 r8, §12.4).
 * Inputs:     slot (INSTRUMENT_SLOT_COUNT = unknown), descriptor.
 * Outputs:    the highest value.
 * Accessors:  instrumentManager_descriptorModDomain(),
 *             instrumentManager_waveCellCount().
 * Affiliates: osc_userCount(), osc_userListCount().
 */
```

---

## 8. `Core/Menu/menu.c` (P1-18)

Apply in this order: MU-3, MU-2, MU-1.

### MU-3 `menu.c:14138-14139` — MOD the voice-parameter automation value display (the automation edit line)

Replace

```c
            menu_formatAutomationValue3(descriptor, autos[page].value,
                                        &editDisplayBuffer[1][13]);
```

with

```c
            menu_formatVoiceAutomationValue3(target, descriptor,
                                             autos[page].value,
                                             &editDisplayBuffer[1][13]);
```

### MU-2 `menu.c:14031` — MOD the voice-parameter automation amount display

Replace

```c
                    menu_formatAutomationValue3(desc, amt_value, amt_out);
```

with

```c
                    menu_formatVoiceAutomationValue3(target, desc, amt_value,
                                                     amt_out);
```

### MU-1 `menu.c:13218` — ADD `menu_formatVoiceAutomationValue3()` (after the closing brace of `menu_formatAutomationValue3()`)

```c
/*
 * Format a voice parameter's 7-bit automation value (S087 P1).
 *
 * What:       for an oscillator wave row (DTYPE_MENU, MENU_WAVEFORM) of a voice
 *             target: the 3-character code of the entry the value selects for
 *             that slot's oscillator - sNN/lNN/wNN for user content, the stock
 *             name for a stock wave, the number when the value selects nothing
 *             (menu_formatWaveCell() on a cell built for the target). Every
 *             other row: menu_formatAutomationValue3().
 * Why:        S087 P0 §12.4 and impl. §10.7: the automation display read every
 *             wave value as stock or sNN, so osc1 under slp/wtb and the
 *             modulators' l/w entries showed the wrong item.
 * Inputs:     target (canonical voice parameter ID), descriptor, value, buf[3].
 * Outputs:    buf (3 characters).
 * Accessors:  the step-automation amount display and the automation edit line.
 * Affiliates: menu_formatWaveCell(), menu_formatAutomationValue3(),
 *             instrumentParam_slot()/instrumentParam_local().
 */
static void menu_formatVoiceAutomationValue3(uint16_t target,
                                             const ParamDescriptor *descriptor,
                                             uint8_t value, char *buf)
{
    if (descriptor &&
        (descriptor->dtype & 0x0fu) == DTYPE_MENU &&
        (uint8_t)(descriptor->dtype >> 4) == MENU_WAVEFORM &&
        instrumentParam_isVoiceParameter(target)) {
        menu_cell_t cell;
        char name[SAMPLE_DISPLAY_NAME_LEN];
        char code[3];

        memset(&cell, 0, sizeof(cell));
        cell.kind = MENU_CELL_INSTRUMENT;
        cell.slot = instrumentParam_slot(target);
        cell.descriptor_index = instrumentParam_local(target);
        cell.descriptor = descriptor;
        menu_formatWaveCell(&cell, value, name, code);
        if (code[0] != ' ')
            memcpy(buf, code, 3u);
        else if (name[0] != ' ')
            memcpy(buf, name, 3u);
        else
            numtostrpu(buf, value, ' ');
        return;
    }
    menu_formatAutomationValue3(descriptor, value, buf);
}
```

---

## 9. Effect of this schedule

These figures come from applying every change above to a copy of the tree
and linking it with the firmware toolchain (DEV configuration). The
changes compile cleanly; the only warnings left are the ones the tree had
before S087.

**Resources:**

| Item | S087 P0 tree | After this schedule |
|---|---:|---:|
| text / data / bss | 672,080 / 424 / 412,396 | 670,328 / 420 / 412,392 |
| Flash used (headroom) | 672,504 (81,160) | 670,748 (82,916) |
| ITCM | 15,032 / 16,384 | 12,640 / 16,384 |
| DTCM statics | 4,476 | 4,344 (R-D1 released) |
| FX arena margin | 3,712 | 3,840 |

**RAM:**
- R-E1 is +2 B as approved (MN-5).
- `install_fault` is 1 B within R-C3.

**CPU** (static per sample, linked loop bodies):

| Loop | S087 P0 | After | Note |
|---|---:|---:|---|
| `osc_slpPlainBlock` | 27 | 27 | the backward step is converted once per block (OC-13) |
| `osc_slpFmBlock` | 47 / 51 / 53 | 49 / 50 inside the loop; 39 in the lead-in | the read rule is chosen per block (OC-11) |
| `osc_slpRingBlock` | 42 | 42 | |
| `osc_smpRingBlock` | 38 | 38 | the depth add moves |
| `osc_wtbRingBlock` | 32 / 34 | 32 / 34 | |
| `wtb` blend inner loops (plain / FM / ring / sync) | 35–39 / 43–47 / 45–49 / 45–50 | 34–38 / 42–46 / 43–47 / 44–48 | split in at most two segments per block (OC-6); per-slot cost +10 … +13 over one frame, against ≈ +66 today |
| `smp` loops, `osc_slpSyncBlock`, `osc_buildLoopSyncList` | unchanged | unchanged | |

**Section worst cases:** none rises; every section stays below its frozen
worst case (A1):
- Drum `fm` with an `slp` modulator and an `slp` FM carrier: 27 + 50 = 77,
  against ≈ 92;
- Cymbal/HiHat: 27 + 27 + 50 = 104, against ≈ 131.

**The mixer-inlined loops** are as in the S087 P0 tree: 37 equal, 1
shrank, and the Snare noise loop 14 → 15 (F12, unchanged by this
schedule).
