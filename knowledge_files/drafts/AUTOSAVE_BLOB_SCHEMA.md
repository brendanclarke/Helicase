# Autosave Blob Schema (draft v0.1)

## Relationship to SCOPING_TARGETS.md §3.7

This document defines the **on-disk container format** for Bank autosave. It does not change the policy layer already settled in §3.7 — the dirty-ledger keys, the 5s-idle/30s-forced debounce, the single-serialized-writer rule, and the Bank-as-the-only-autosaved-workspace scope all still apply and are assumed, not repeated, below.

What changes: §3.7 described dot-file backers (`.kitset.kcg` etc.) living next to their committed originals inside the Bank tree, promoted to real files on Bank SAVE. That approach requires filesystem primitives (atomic rename-over-existing, in particular) that asyncfatfs doesn't have and isn't close to having safely — `afatfs_renameObject_lfn()` renames a name-entry in place but doesn't replace a same-named target, and `afatfs_moveObject` / `afatfs_copyObjectTree` / tree-replace are explicitly unimplemented placeholders. This document replaces that mechanism with a small, fixed set of **root-level positional-register files** — no tree mirroring, no rename, no in-place file replacement anywhere in the design.

Two containers, addressing two very different mutation shapes:

1. **Parameter register** (`APARAMS.BIN`) — one file. Bankset, sceneset, kitset, instrument, and effects mutations for all 16 Scene slots. These domains are small, roughly fixed-size, text-schema records that get fully rewritten on each touch.
2. **Pattern registers** (`APAT00.BIN` … `APAT15.BIN`) — sixteen files, one per Scene slot. Pattern data is a ~18KB dynamic-stack structure (§4) where most edits touch a handful of bytes, not the whole structure, so it gets its own format built around partial writes.

Neither container ever needs a whole-file atomic replace. Crash safety comes from **per-unit checksums that make a torn write self-detecting and independently discardable** — see "Crash safety model" below for why that's the better fit here, not just the available one.

---

## Common conventions

- **Sector size:** 512 bytes. Every region and record is sector-aligned, so a torn write can never straddle into a neighboring record's sector.
- **Endianness:** little-endian throughout (native to the STM32F7, no conversion on read/write).
- **Checksums:** CRC-32 (standard IEEE 802.3 / zlib polynomial, `0xEDB88320`) for the parameter register's records and the pattern register's header/address-array snapshot. CRC-16/CCITT (`0x1021`) for pattern pool log entries, to keep per-entry overhead small across a region that may hold many entries. Neither exists in `Core/` today — this is new utility code, not a reuse of the existing 8-bit complement checksum in `tools/build_lxrv2_img.py` (that one's fine for a single boot-image sanity check, but too weak for detecting torn writes across many small records reliably).
- **Filenames:** plain 8.3, no LFN needed for autosave I/O at all.

| File | Purpose |
|---|---|
| `APARAMS.BIN` | Parameter register for the active Bank |
| `APAT00.BIN` … `APAT15.BIN` | Pattern register for Bank Scene slot 0–15 |

All seventeen files live at SD card root, alongside `settings.cfg`. They represent whichever Bank is currently active, not a specific Bank number — there is one live copy, not one per possible Bank slot.

---

## Crash safety model — why not atomic replace

Worth stating explicitly, since it's the design's central bet: **we are not trying to make a write atomic; we are trying to make a torn write cheap to detect and cheap to lose.**

An atomic whole-file replace (temp file + rename-over-original) guarantees a write is either fully visible or fully reverted to the *previous* full snapshot. That's a fine model for something you rewrite occasionally in full. It's a bad fit for the pattern register, where the natural unit of mutation is "one step's block changed," not "the whole 18KB structure changed" — an atomic-replace model would force choosing between (a) doing a full-structure rewrite-and-swap on every single step edit (expensive, and this hardware's SD path is bit-banged SPI per `Core/Hardware/SD/SPI/spi_sd.c`, not SDIO — there's no measured throughput figure in the repo, but a full-structure rewrite on every recorded note is a real risk against the live-recording real-time constraint in §4.3), or (b) batching edits and accepting "lose everything since the last swap" on a crash, which is a much bigger loss than "lose the last mutation."

Instead, every write unit below — a parameter record, an address-array snapshot, a pattern pool entry — carries its own checksum and is applied at boot only if that checksum is valid. A torn write invalidates exactly the unit that was mid-write when power was lost, nothing else, and every earlier successfully-written unit stays valid regardless.

---

## Part 1 — Parameter register (`APARAMS.BIN`)

### Layout

Fixed-size, positionally addressed. Every record's offset is computed from `(scene_index, domain[, instrument_slot])` — there is no index or directory to scan, at boot or at runtime.

```
offset 0        header                              1 sector    (512 B)
offset 512      bankset record                       1 sector    (512 B)
offset 1024     scene 0 block                        51 sectors  (26,112 B)
offset 27136    scene 1 block                        51 sectors
...
offset 1024 + N*26112   scene N block
```

**Record sizes:**

| Domain | Size | Basis |
|---|---|---|
| Header | 512 B (1 sector) | fixed |
| Bankset | 512 B (1 sector) | measured `bankset.bcg` ≈ 82 B; generous headroom |
| Sceneset | 512 B (1 sector) | measured `sceneset.scg` ≈ 249 B |
| Kitset | 512 B (1 sector) | measured `kitset.kcg` ≈ 353 B |
| Instrument (per slot) | 4,096 B (8 sectors) | measured legacy `.drm`/`.snr`/`.hat`/`.cym` ≈ 1.4–1.5 KB, **but** that sample has no `[morph]` section — Helicase's own schema adds one per the morph engine work, likely close to doubling per-parameter storage. Budgeted at roughly 2.5–3x the legacy sample as a hedge; **re-measure once the real Helicase instrument schema is finalized and tighten this number.** |
| Effects | 512 B (1 sector) | placeholder — Phase 6 hasn't defined FX-stack content yet (§6.5 discusses up to 64 arbitrary parameters per stack). **This will very likely need to grow; treat as unset until Phase 6 lands.** |

**Per-scene block** (51 sectors = 26,112 B): sceneset (512) + kitset (512) + 6 × instrument (4,096 × 6 = 24,576) + effects (512).

**Offset formula:**

```
scene_block_base(scene)              = 1024 + scene * 26112
sceneset_offset(scene)               = scene_block_base(scene)
kitset_offset(scene)                 = scene_block_base(scene) + 512
instrument_offset(scene, slot 0..5)  = scene_block_base(scene) + 1024 + slot * 4096
effects_offset(scene)                = scene_block_base(scene) + 25600
```

Total file size: `1024 + 16 * 26112 = 418,816 bytes` (≈ 409 KB), all illustrative pending the instrument/effects re-measurement above.

### Header (offset 0, 512 B)

| Field | Size | Notes |
|---|---|---|
| `magic` | 4 B | `"APRM"` |
| `schema_version` | 2 B | LE uint16 |
| `active_bank_number` | 2 B | LE uint16 — which Bank this register belongs to |
| `session_write_count` | 4 B | LE uint32, diagnostic only — incremented on every flush, not used for record arbitration (each record carries its own generation, see below) |
| reserved | 496 B | zero-filled |
| `header_crc32` | 4 B | CRC-32 over the preceding 508 bytes |

On Bank activation, if `active_bank_number` doesn't match the Bank actually being loaded, the whole file's content is stale — zero every record region (not just the header) and reuse the existing preallocated space rather than deleting/recreating the file. This also resets every record's generation counter, so there's no risk of an old Bank's stale generation number outranking a fresh write after the file is reused for a different Bank.

### Per-domain record

Every record — bankset, sceneset, kitset, instrument, effects — shares one shape:

| Field | Size | Notes |
|---|---|---|
| `scene_index` | 1 B | `0xFF` for bankset (not scene-scoped) |
| `domain_tag` | 1 B | enum: `BANKSET` / `SCENESET` / `KITSET` / `INSTRUMENT` / `EFFECTS` |
| `instrument_slot` | 1 B | `0xFF` unless `domain_tag == INSTRUMENT` |
| `payload_len` | 2 B | LE uint16, bytes actually used |
| `write_generation` | 4 B | LE uint32, local to this record — incremented each time this specific record is rewritten |
| `payload` | up to (record size − 13 B header − 4 B trailing CRC) | the serialized text-schema content (open question below) |
| `record_crc32` | 4 B | last 4 bytes of the record; CRC-32 over everything preceding it in the record, header through payload, not the zero-padding after `payload_len` |

**Boot-time validation:** a record is applied only if (a) `record_crc32` matches, and (b) `scene_index` / `domain_tag` / `instrument_slot` are consistent with the offset the record was read from. The second check catches the case where a CRC happens to validate against content that's simply in the wrong place — cheap insurance, since the position is already known from the read.

**Open question — payload encoding.** Should the payload be the exact same `[params]`/`[morph]` text schema already used by the committed files (simple, one serializer, human-inspectable on the card, but larger and costs a reformat pass on every debounced write), or a compact binary encoding specific to autosave (smaller, faster, but a second parser to maintain)? Records are debounced, not per-tick, so I'd default to reusing the text serializer unless profiling on real hardware says otherwise — flagging this as a decision, not making it here.

---

## Part 2 — Pattern register (`APAT00.BIN` … `APAT15.BIN`)

One file per Scene slot, same shape in each. Two regions: a whole-rewrite address-array snapshot, and an append-only log of individually-checksummed pool block writes.

### Layout

```
offset 0        header                     1 sector    (512 B)
offset 512      address-array region       4 sectors    (2,048 B)
offset 2560     pool log region             48 sectors   (24,576 B)
```

Total per file: 27,136 B (≈ 26.5 KB). Across 16 scenes: ≈ 424 KB.

### Header (512 B)

| Field | Size | Notes |
|---|---|---|
| `magic` | 4 B | `"APAT"` |
| `schema_version` | 2 B | LE uint16 |
| `scene_index` | 1 B | which of the 16 files this is — sanity check against the filename itself |
| `active_bank_number` | 2 B | LE uint16 |
| `pool_write_cursor` | 2 B | LE uint16 offset into the pool log region — where the *next* entry gets written. Only meaningful at runtime; boot-time apply scans the whole region regardless (see below), so a stale or corrupted cursor doesn't block recovery. |
| reserved | 500 B | zero-filled |
| `header_crc32` | 4 B | CRC-32 over the preceding 508 bytes |

Same stale-Bank handling as the parameter register: a mismatched `active_bank_number` means zero the whole file and reuse it.

### Address-array region (2,048 B, holds a 1,792 B payload)

A whole-rewrite snapshot of the resident 896 × 2-byte address array (§4.2's on/off / has-specials / offset-address encoding, byte-for-byte, opaque to this schema). Rewritten in full on essentially every edit — cheap, since 1,792 bytes is under 4 sectors regardless.

| Field | Size |
|---|---|
| `write_generation` | 4 B |
| `payload` | 1,792 B — raw copy of the resident array |
| `region_crc32` | 4 B |

This snapshot is a convenience artifact, not the sole source of truth — the pool log below is self-contained enough (it carries its own on/off and has-specials bits per entry) that the on/off state is in principle reconstructible from it if this snapshot's checksum ever fails. Building that reconstruction path is a nice-to-have, not part of the v1 scope — noting it here so it isn't lost.

### Pool log region (24,576 B) — append-only ring of self-contained block entries

This is the piece actually built to satisfy "lose only the last mutation." Each entry is a standalone record of one step's dynamic block, wrapping the exact §4.2 block encoding without modifying it:

| Field | Size | Notes |
|---|---|---|
| `entry_length` | 2 B | LE uint16, total bytes of this entry (this field through the trailing CRC) — lets a reader skip to the next entry without parsing an invalid one |
| `write_generation` | 4 B | LE uint32, local to this step_id — the winner among multiple entries for the same step is whichever has the highest value |
| `flags_and_step_id` | 2 B | bit 15: on/off · bit 14: has-specials · bits 13–10: reserved (0) · bits 9–0: step-ID (0–895). Mirrors the address-array entry's own bit-15/bit-14 convention, so the same reader logic recognizes both. Deliberately redundant with the step-ID embedded in the block payload below — this copy exists so the boot scan can route/filter entries by step without first parsing into the block body. |
| `block_payload` | variable | byte-exact copy of the resident §4.2 block: 2 B (step-ID + 6-bit automation count) + optional special-flags byte and special values if has-specials is set + `automation_count × 2 B` automation entries. Copied verbatim so it can be handed directly to the existing block-insert/relocate code at apply time. |
| `entry_crc16` | 2 B | CRC-16/CCITT over everything preceding it in this entry |

**Why append-only, not positional:** positionally addressing pool entries by the block's real offset address would mean the autosave copy has to exactly track wherever the live defragmenter currently has that block, which changes as a side effect of unrelated edits. Making every entry self-contained (own step-ID, own generation, own checksum) means position in the log is meaningless — a step can appear multiple times across a session, and only the highest-generation valid entry for a given step-ID is authoritative. That also directly implements the crash-safety goal: if the very last write is torn, its CRC fails, it's skipped, and whatever entry previously held that step-ID (already durable) is used instead.

**Wrap-around and compaction:** once the write cursor reaches the end of the 24,576-byte region, it wraps to the start and begins overwriting the oldest entries. Since only the newest valid entry per step matters, overwriting a superseded entry is harmless — the hazard is wrapping over the *only* remaining valid copy of a step that hasn't been superseded elsewhere in the log. Avoiding that needs a compaction pass (rewrite the log down to just the current winning entry per dirty step, reset the cursor to 0) triggered at natural pause points that are already allowed to be slower: leaving this scene's editing focus, Scene RELOAD/SAVE, or a write that's about to wrap over a still-needed entry. This defers the one expensive full-structure operation to boundaries the design already treats as acceptable to pay for, while keeping the common per-edit write a cheap append.

**Open questions on this region:**
- 24,576 bytes (≈1.5× the 16,383-byte worst-case resident pool) is a starting-point sizing, not a measured one — it needs to comfortably hold a session's worth of redundant pre-compaction entries, and the right multiplier depends on real editing/recording session telemetry that doesn't exist yet.
- A torn write that happens to corrupt `entry_length` itself (rather than the payload after it) could desync the scan for the remainder of the region, since the reader can no longer reliably find the next entry boundary. In practice, torn writes on sequential SD writes tend to cut off the *end* of the write rather than scramble the middle, which keeps an early, small field like this relatively low-risk — but it's a real limitation of this format, not one this draft closes out.
- Whether the compaction "about to overwrite the last valid copy of a still-needed step" check needs new bookkeeping, or can piggyback on whatever resident structure the live autosave-tracking already needs for generation arbitration — likely the latter, but not designed here.

### Boot-time apply (both regions)

1. Load the committed `pattern.pat` for this scene into resident memory, as normal.
2. Validate the header CRC; if invalid, stop here — nothing in this file is trusted.
3. Validate the address-array snapshot's CRC; if valid, note it as a fast sanity reference (not yet applied).
4. Scan the pool log region from byte 0 to the end unconditionally — the write cursor isn't needed for correctness, only for knowing where to write *next* at runtime. Garbage or never-written bytes simply fail their CRC and get skipped.
5. For every entry that parses within the region's bounds and passes its CRC-16, track the highest `write_generation` seen so far per step-ID.
6. Apply each step-ID's winning entry to resident memory via the existing block-insert/relocate path (the same one live editing already uses), and set that step's on/off and has-specials bits from the entry's `flags_and_step_id`.
7. If the address-array snapshot from step 3 was valid, cross-check it against the state now assembled from the pool log; a mismatch is worth logging, not fatal — the pool log is authoritative for anything the snapshot might have missed to its own torn write.

---

## Lifecycle summary

- **Bank activation:** create+preallocate any missing autosave files via `afatfs_fopen(name, "as", cb)` (contiguous mode, per the earlier discussion — this gives the fast `currentCluster + 1` cursor path with no FAT-table walk). If a file exists but its Bank-identity field doesn't match, zero it and reuse the preallocated space rather than deleting/recreating.
- Files stay open for the session; every debounced write is `fseek` + `fwrite` into an already-open handle — no directory traversal on the hot path, ever.
- **Boot / Bank activation apply:** slow path is fine here — validate and apply as described above, once, before the Bank becomes interactive.

## Open engineering questions (rollup)

- Parameter payload encoding: reuse the text schema serializer, or a compact autosave-specific binary form?
- Instrument and effects record sizes are provisional — re-measure against Helicase's actual (morph-inclusive) instrument schema and the Phase 6 effects-stack format once those exist.
- Pool log region size (24,576 B) is a starting point, not a measured figure.
- Whether reopening a file with `"as"` on a later boot still gets the fast contiguous-cursor path, or only the handle that did the original allocation does — flagged previously, still untested.
- `entry_length` corruption edge case in the pool log — accepted limitation, not solved here.
- Address-array reconstruction from pool-log content as a fallback if the snapshot's own CRC fails — worth having, not built in this draft.
