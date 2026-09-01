# Session 060 — AutoSave v2 Per-Section CRC Record

Status: implementation-ready plan; no implementation has been performed.

This plan was reconciled against the live tree after Session 059, not copied
forward from the earlier draft. Current-line citations below refer to the
pre-Session-060 sources at HEAD 0c90434 plus the user's existing Session 059
working-tree changes. Line numbers will move during implementation; symbol
names and stated contracts are the durable references.

Authoritative context:

- MEMORY.md, especially Sessions 045–059.
- knowledge_files/specification_reference/AUTOSAVE.md.
- knowledge_files/specification_reference/FILESYSTEM_SPEC.md.
- knowledge_files/specification_reference/MODULE_INTERCHANGE_SPEC.md.
- knowledge_files/specification_reference/SRAM_MANIFEST.md.
- knowledge_files/drafts/AUTOSAVE_READ_PLAN.md, used only as a future-reader
  consumer; where it conflicts with live code or this plan, this plan wins.
- Core/Bank/Scene/Autosave.c and Autosave.h.
- Core/Hardware/SD/filesystem.c and filesystem.h.

## 1. Outcome and scope

Session 060 changes the hidden /.hcprms1 and /.hcprms2 wire format from v1's
one whole-record CRC to v2's authenticated header plus 17 independently
authenticated record sections:

- one Bank section;
- sixteen Scene sections;
- one CRC32C for each section;
- one header CRC32C which authenticates generation, probe, the complete section
  CRC table, and every reserved header byte.

The canonical SRAM representation does not change. Autosave.c continues to own
one 3,856-byte LSB-first dirty mask over one 30,848-byte logical payload.
Existing typed mutation producers, atomic take/merge/rollback behavior, live
getters, the 4,608-byte bounded patch cache, the five-second debounce, the
250 ms continuation, the HCNAMES mirror, the filesystem facade, AsyncFATFS,
and the inactive-peer delete/create transaction remain in place.

The runtime writer remains a full-record A/B copy in Session 060. It may copy a
clean section verbatim and forward that section's already-authenticated CRC,
but it still reads and writes the complete 34,832-byte target and validates
both candidates before selection. This is deliberately not the future
in-place/surgical writer.

The direct benefits in this session are:

- corruption is localized and observable at Bank/Scene granularity;
- a future boot reader can decide trust per section;
- transformed sections can be checksummed independently;
- clean copied sections do not need their CRC recomputed.

Do not claim 17 ms single-Scene writes, 280 ms Bank transitions, or materially
shorter SD ownership from this implementation. Those estimates belonged to an
unimplemented surgical design and are not results of the full-copy writer.

### 1.1 Non-goals

This session does not:

- implement AutoSave boot restore/replay;
- accept a partially valid record as the runtime writer's A/B source;
- combine valid Scenes from two different generations;
- change the dirty-producer call sites in BankData, SceneData, Preset, Menu, or
  the load/save commit paths;
- add a second retained mask or a record-sized buffer;
- change Pattern or Effect persistence;
- change AsyncFATFS, its seek/size fix, cache ownership, sync semantics, or
  duplicate-name rules;
- change ordinary Bank/Scene/Kit/Instrument wire formats;
- change the existing HCNAMES dedicated mirror;
- add new lifecycle trace stages or widen the trace record;
- perform a v1-to-v2 in-place migration.

### 1.2 Why surgical update is deferred

The inactive target is normally one generation behind the selected winner.
Overwriting only the currently dirty section would therefore publish a target
whose untouched sections can still belong to an older generation. A correct
surgical transaction must first prove which target sections equal the winner,
copy-forward every stale clean section, transform dirty sections, authenticate
the resulting table, and preserve a recoverable valid peer across target
invalidation and sync. That is a separate transaction design. Session 060
retains the already hardware-proven full-copy A/B safety envelope.

## 2. Exact v2 wire contract

All integers are little-endian. CRCs use the existing Castagnoli CRC32C
polynomial and begin/update/finalize convention. Section CRCs cover their exact
stored bytes without exceptions. The header CRC covers all 128 logical header
bytes while treating bytes 12..15 as zero.

### 2.1 Header: record offsets 0..127

| Offset | Bytes | Meaning | Validation |
| ---: | ---: | --- | --- |
| 0 | 4 | ASCII HCPR magic | exact |
| 4 | 1 | format version 2 | exact; v1 is rejected |
| 5 | 1 | commit marker | 0xA5 for a published record |
| 6 | 2 | reserved | must be zero |
| 8 | 4 | wrapping generation | authenticated by header CRC |
| 12 | 4 | header CRC32C | stored little-endian; zero while calculating |
| 16 | 1 | probe counter | authenticated; diagnostic only |
| 17 | 3 | reserved | must be zero |
| 20 | 68 | 17 section CRC32Cs | Bank first, then Scenes 0..15 |
| 88 | 40 | reserved | must be zero |

The old draft's XOR aggregate is rejected. XOR does not authenticate the
ordering of the table entries, has avoidable collision structure, and leaves
other header controls without a direct integrity check. Offset 12 is therefore
the header CRC32C, not a section-CRC aggregate.

The CRC is calculated for the final logical header with commit 0xA5. During
publication the writer first writes the same header with physical commit byte
zero, syncs it, and changes only byte 5 to 0xA5 last. A power loss before the
last step leaves the new target ineligible even if its header CRC and sections
are otherwise complete.

### 2.2 Section layout

| Section index | Absolute start | Mask bytes | Data start | Data bytes | Total |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 0, Bank | 128 | 16 | 144 | 128 | 144 |
| 1+n, Scene n | 272 + n×2160 | 240 | 512 + n×2160 | 1,920 | 2,160 |

The exact record end is:

    128 + 144 + (16 * 2160) = 34,832 bytes

The CRC table index is the section index: entry 0 covers Bank mask+data;
entry 1+n covers Scene n mask+data. No CRC includes any other section or the
header.

### 2.3 Canonical SRAM-to-wire mapping

The logical payload layout does not change:

- payload offsets 0..127 are Bank data;
- payload offsets 128 + n×1920 through 128 + (n+1)×1920 - 1 are Scene n data.

The logical mask layout also does not change:

- canonical mask bytes 0..15 describe Bank payload bits 0..127;
- canonical mask bytes 16 + n×240 through 16 + (n+1)×240 - 1 describe Scene n.

Only serialization changes. Each canonical mask slice is placed immediately
before its corresponding data slice. Existing typed markers and live getters
continue to speak only in canonical payload-relative coordinates.

### 2.4 Validation levels

A header is valid only when:

- all 128 bytes were received at the correct offsets;
- magic, version 2, and commit 0xA5 match;
- all reserved header bytes are zero;
- the stored header CRC equals the CRC of the logical header with bytes 12..15
  treated as zero.

A section is valid only when its exact declared byte count was received and its
computed CRC equals its authenticated table entry.

A complete record is valid only when:

- its length is exactly 34,832 bytes, with no short or trailing byte;
- its header is valid;
- all 17 sections are valid.

The stream validator records per-section validity for future readers, but the
Session 060 writer calls the complete-record predicate and never selects a
partially valid candidate. Bank identity matching is evaluated only after the
header and Bank section are valid.

## 3. Public Autosave interface after the split

The declarations and their adjacent comments in Autosave.h must make inputs,
outputs, ownership, and affiliates explicit. Use the following API contracts;
minor naming adjustments are acceptable only if all callers and docs use the
same final names.

### 3.1 Geometry and section accessors

Add section constants and pure accessors:

    AUTOSAVE_SECTION_COUNT                 17
    AUTOSAVE_BANK_SECTION_INDEX             0
    AUTOSAVE_SCENE_SECTION_FIRST_INDEX       1
    AUTOSAVE_SECTION_VALID_ALL_MASK  0x0001ffffu

    AUTOSAVE_BANK_MASK_BYTES                16
    AUTOSAVE_BANK_DATA_BYTES               128
    AUTOSAVE_BANK_RECORD_SECTION_BYTES     144
    AUTOSAVE_SCENE_MASK_BYTES              240
    AUTOSAVE_SCENE_DATA_BYTES             1920
    AUTOSAVE_SCENE_RECORD_SECTION_BYTES   2160

    uint32_t autosave_sectionRecordOffset(uint8_t section_index);
    uint16_t autosave_sectionMaskBytes(uint8_t section_index);
    uint16_t autosave_sectionDataBytes(uint8_t section_index);
    uint16_t autosave_sectionBytes(uint8_t section_index);
    uint16_t autosave_sectionCanonicalMaskOffset(uint8_t section_index);
    uint16_t autosave_sectionCanonicalPayloadOffset(uint8_t section_index);

Inputs are section indices 0..16. Outputs are the v2 absolute/canonical
coordinates or zero for an invalid index where zero cannot be mistaken for a
valid length. These helpers keep filesystem.c out of wire arithmetic. They are
affiliated with initial formatting, stream validation, mask import, transformed
copy, and the host verifier. Where compile-time constants are required, retain
named Bank/Scene macros as well as the runtime accessors.

Add:

    uint8_t autosave_maskSectionHasDirty(uint8_t section_index);

It reads only that section's canonical SRAM mask slice and returns nonzero when
any byte is set. It does not clear or snapshot bits. The runtime copy uses it
immediately before deciding whether a section may be copied verbatim or must
receive the canonical mask overlay and a fresh CRC.

### 3.2 Generic bounded CRC state

Replace the v1 whole-record CRC interface with a raw streaming CRC32C interface:

    uint32_t autosave_crc32cBegin(void);
    uint32_t autosave_crc32cUpdate(
        uint32_t state, const uint8_t *bytes, uint16_t byte_count);
    uint32_t autosave_crc32cFinish(uint32_t state);

Inputs are an unfinalized state and an ordinary byte interval; output is the
next unfinalized or finalized value. This API performs no record-offset
exception. Callers must cap update intervals through
AUTOSAVE_CRC_BYTES_PER_TICK. Affiliates are section validation, initial section
precomputation, transformed-section calculation, header formatting, and the
existing Instrument Save diagnostic fingerprint.

Retain autosave_crc32cByteUpdate() as the single-byte raw primitive used by the
diagnostic code, or implement it as a thin wrapper over the same internal
primitive. Remove the semantic name autosave_recordCrc* because v2 has no
whole-record CRC.

### 3.3 Header construction

Add:

    void autosave_formatCommittedHeader(
        uint8_t header[AUTOSAVE_HEADER_BYTES],
        uint32_t generation,
        uint8_t probe_counter,
        const uint32_t section_crc32c[AUTOSAVE_SECTION_COUNT]);

Inputs are the new generation/probe and final CRC of every section. Output is
one complete logical v2 header: zero-initialized reserved bytes, magic/version,
commit 0xA5, table entries, and the calculated header CRC. It performs no file
I/O. The filesystem writer copies this header, clears byte 5 only in the
physical staging image, writes/syncs it, and later publishes 0xA5.

If a separate header validator/helper is added, it must use the same zeroed
bytes-12..15 rule and require all reserved bytes to be zero. Do not expose or
compute an XOR aggregate.

### 3.4 Initial-section formatter

Replace autosave_formatInitialChunk() with:

    void autosave_formatInitialSectionChunk(
        uint8_t *dst,
        uint8_t section_index,
        uint16_t section_offset,
        uint16_t byte_count,
        uint16_t bank_slot,
        const char bank_name[AUTOSAVE_NAME_BYTES],
        const char resident_names[AUTOSAVE_HCNAMES_ROW_COUNT]
                                 [AUTOSAVE_HCNAMES_ROW_BYTES]);

Inputs identify one bounded interval wholly inside one Bank/Scene section plus
the Bank identity and stable HCNAMES mirror. Output is deterministic: the
section-local mask is zero; Bank slot/name and Scene/Kit/Instrument names occupy
their unchanged data-relative fields; parameters, Effects, and padding are
zero. Header bytes are not part of this API and are produced only by
autosave_formatCommittedHeader().

Replace autosave_initialRecordCrcUpdate() with:

    uint32_t autosave_initialSectionCrcUpdate(
        uint32_t state,
        uint8_t section_index,
        uint16_t section_offset,
        uint16_t byte_count,
        uint16_t bank_slot,
        const char bank_name[AUTOSAVE_NAME_BYTES],
        const char resident_names[AUTOSAVE_HCNAMES_ROW_COUNT]
                                 [AUTOSAVE_HCNAMES_ROW_BYTES]);

It synthesizes exactly the same byte_count bytes as
autosave_formatInitialSectionChunk() and advances the raw CRC. It never visits
the header or another section. The caller retains section index,
section-relative cursor, and accumulator between ticks and finalizes each of
the 17 values separately before formatting the header.

### 3.5 Stream validator

Revise autosave_stream_validation_t to retain:

- uint8_t header_bytes[128];
- uint32_t stored_section_crc32c[17];
- uint32_t current_section_crc32c;
- uint32_t section_valid_mask;
- uint32_t bytes_seen;
- uint16_t current_section_bytes_seen;
- uint8_t current_section_index;
- generation, probe, Bank slot, and Bank name;
- header_valid and stream_error flags.

The existing begin/update/finish calls remain streaming and do not allocate a
record image. update accepts sequential absolute offsets only, parses the
header, begins/finalizes at each declared section boundary, and captures Bank
identity from the Bank data region. Each filesystem call passes at most 128
bytes, but an update may cross a header/section boundary and must split its own
work correctly.

Add regular accessors:

    uint8_t autosave_streamValidationHeaderValid(
        const autosave_stream_validation_t *state);
    uint8_t autosave_streamValidationSectionValid(
        const autosave_stream_validation_t *state,
        uint8_t section_index);
    uint32_t autosave_streamValidationSectionCrc(
        const autosave_stream_validation_t *state,
        uint8_t section_index);

The CRC accessor returns the authenticated stored table entry only after the
header is valid; otherwise it returns zero. The section-valid accessor returns
header_valid AND the requested section bit, so an unauthenticated CRC table
cannot confer trust. finish remains the strict complete-record predicate used
by the writer. matchesBank requires a valid header and Bank section, then
compares the captured two-byte restore slot and normalized eight-byte display
name.

### 3.6 Drain transform

Keep autosave_transformDrainChunk() as the writer-facing transformation, but
replace its v1 contiguous mask/payload arithmetic with v2 mapping. Inputs remain
one source record interval, the canonical mask, the sorted stable payload
patches, and a monotonic patch cursor. Output changes only:

- record bytes belonging to the intersecting section's canonical mask slice;
- payload bytes represented by captured patches.

Header generation/probe/CRC/commit are no longer transformed by this routine;
the final header is formatted separately. Callers pass section intervals, not
the header, and do not cross a section boundary. For a copied-clean section the
routine is not called.

The transform must translate each patch's canonical payload offset to the
corresponding interleaved v2 record offset. The live getter and marker
coordinates do not change.

## 4. File-by-file implementation recipe

Every source change required by this plan is listed here against current line
ranges.

### 4.1 Core/Bank/Scene/Autosave.h

Current lines 21–38 — replace the v1 header definition.

- Set AUTOSAVE_HEADER_FORMAT_VERSION to 2 and AUTOSAVE_HEADER_BYTES to 128.
- Keep magic/version/commit/generation/probe at offsets 0/4/5/8/16.
- Rename offset 12 to the unambiguous header-CRC constant.
- Add the table offset 20, entry width 4, entry count 17, reserved ranges, and
  logical-commit documentation.
- Adjacent comment must state the header CRC inputs, zero-field rule, output,
  and commit-last affiliate phases.

Current lines 40–105 — replace monolithic wire geometry.

- Preserve AUTOSAVE_MASK_BYTES == 3856 and AUTOSAVE_PAYLOAD_BYTES == 30848 as
  canonical SRAM geometry.
- Delete AUTOSAVE_MASK_OFFSET and AUTOSAVE_PAYLOAD_OFFSET as record-wide wire
  coordinates.
- Add Bank/Scene section mask, data, total, and absolute-start constants from
  section 2.
- Rename the old 128/1,920-byte AUTOSAVE_BANK_SECTION_BYTES and
  AUTOSAVE_SCENE_SECTION_BYTES constants to the unambiguous
  AUTOSAVE_BANK_DATA_BYTES and AUTOSAVE_SCENE_DATA_BYTES; use
  *_RECORD_SECTION_BYTES only for mask+data wire extents.
- Replace the current absolute Bank field macros with data-relative constants:
  restore slot 0, name 2, Scene-present mask 10, active Scene 12, and VOICE-edit
  mask 13. Scene/Kit/Instrument field offsets are already data-relative and
  remain so. Absolute wire positions are section data base plus these inner
  offsets, never one global payload base.
- Set AUTOSAVE_RECORD_BYTES to 34832.
- Add the pure section-coordinate/accessor declarations in section 3.1.

Current lines 191–243 — replace and extend static assertions.

- Assert header 128; table ends at 88; reserved tail ends at 128.
- Assert Bank section 16+128 == 144.
- Assert Scene section 240+1920 == 2160.
- Assert section starts and final record size 34832.
- Assert 16 + 16×240 == 3856 and 128 + 16×1920 == 30848, proving canonical
  SRAM coverage remains unchanged.
- Preserve all existing field-width/range assertions inside Bank, Scene, Kit,
  and Instrument data.
- Assert 17 section bits fit the chosen uint32_t mask.

Current lines 245–287 — revise initial-format and initial-CRC declarations.

- Replace the record-wide formatter with the exact section-local formatter in
  section 3.4; header formatting is a separate declaration.
- Replace the whole-record initial CRC state/API with the section-specific
  bounded contract in section 3.4.
- State explicitly that the HCNAMES pointer is the dedicated stable mirror,
  not fs_list_cache_name.

Current lines 289–336 — revise validation state and accessors.

- Add table storage, header state, section cursor/CRC, validity mask, and exact
  length state described in section 3.5.
- Preserve public generation/probe/Bank identity results because filesystem
  winner selection consumes them.
- Add header/section/CRC accessors and document partial-validation use as a
  future-reader facility, not writer authorization.

Current lines 352–381 — add autosave_maskSectionHasDirty().

- Keep merge, whole-mask has-dirty, atomic take, and rollback signatures
  unchanged.
- Document that the new accessor neither snapshots nor consumes bits.

Current lines 475–526 — revise transform and CRC declarations.

- Remove header generation/probe inputs from autosave_transformDrainChunk().
- Document section-bounded use and canonical-to-interleaved translation.
- Replace recordCrc begin/update/finish declarations with raw crc32c
  begin/update/finish; retain the byte updater for the diagnostic affiliate.

No marker declaration from current lines 394–465 changes.

### 4.2 Core/Bank/Scene/Autosave.c

Current lines 63–78 — keep the retained 3,856-byte allocation unchanged.

- Update only its adjacent comment to distinguish canonical mask ordering from
  the new interleaved on-card placement.
- Do not add another static mask, payload image, or CRC table here.

Current lines 209–319 — replace autosave_initialRecordByte() with an
initial-section byte resolver and its mapping.

- Reject invalid section/section-relative coordinates; this resolver never
  emits a header byte.
- Bank and Scene masks serialize as zero in their local prefixes.
- Bank/Scene data fields use section data bases while all inner offsets remain
  unchanged.
- Bank slot/name and HCNAMES-derived Scene/Kit/Instrument names remain the
  only nonzero baseline data.
- Comment inputs/outputs and the deterministic baseline limitation: it is
  identity/name scaffolding, not a full resident snapshot.

Current lines 321–380 — generalize CRC helpers.

- Preserve the Castagnoli lookup/byte primitive.
- Rename the streaming API to autosave_crc32cBegin/Update/Finish.
- Remove the absolute-offset parameter and the v1 bytes-12..15 exception from
  raw update.
- Implement the header zero-field rule in header construction/validation, not
  in the generic CRC engine.
- Keep each caller responsible for the 128-byte per-tick budget.

Current lines 383–456 — replace whole-record initial CRC and formatter.

- Implement initial-section bounded CRC synthesis, one section at a time.
- Implement autosave_formatInitialSectionChunk() over the same byte resolver so
  CRC preparation and physical output cannot diverge.
- Add autosave_formatCommittedHeader(): zero all 128 bytes, populate fields and
  the 17 little-endian CRC entries, calculate the header CRC with bytes 12..15
  zero, then store it.
- Factor shared little-endian loads/stores rather than duplicating table logic.

Current lines 458–596 — rewrite streaming validation.

- Require strictly sequential offsets and exact 34,832-byte completion.
- Buffer/parse the 128-byte header, verify constants/reserved bytes/header CRC,
  and retain the authenticated table.
- Calculate and finalize a raw CRC at every Bank/Scene boundary; set one bit in
  section_valid_mask for each match.
- Preserve exact-length detection by allowing filesystem.c's existing one-byte
  trailing probe.
- Capture Bank identity only from v2 Bank data coordinates.
- finish returns true only for header_valid plus all 17 bits plus exact size and
  no stream error.
- matchesBank depends on header plus Bank-section validity.
- Implement the three read-only validation accessors in section 3.5.

Current lines 661–1380 — keep live projection and dirty producers logically
unchanged.

- Replace the few expressions which derive canonical offsets by subtracting
  AUTOSAVE_PAYLOAD_OFFSET (current lines 707–710 and 925–970) with direct
  canonical Bank/Scene data-relative constants.
- Do not change getter coverage, descriptor Morph eligibility, name
  normalization, atomic marker order, whole-object marker scope, or any caller.
- Update comments that currently call payload coordinates absolute record
  coordinates.

Current lines 1392–1462 — preserve merge/take/rollback behavior and add the
section query.

- Implement autosave_maskSectionHasDirty() by choosing the canonical mask slice
  with the section accessors and scanning only that slice.
- It must tolerate an invalid index by returning zero.
- Do not make take or merge section-local; their public payload-relative
  contract stays stable.

Current lines 1498–1621 — replace the v1 transform mapping.

- Remove autosave_transformHeaderChunk(); header publication now owns all
  control bytes.
- In autosave_transformDrainChunk(), identify the one declared section
  interval, overlay that section's canonical mask slice into its local prefix,
  and translate sorted canonical patch offsets into section data record
  offsets.
- Reject a header interval, invalid range, or a chunk crossing a section
  boundary.
- Preserve monotonic patch-cursor behavior across the writer's section order.
- State in the adjacent comment that clean sections bypass this function and
  forward the winner's authenticated bytes/CRC.

### 4.3 Core/Hardware/SD/filesystem.c

Current lines 795–829 — expand operation-local writer state inside the existing
2 KB fs_stage_workspace union.

- Replace target_crc32c with current_section_crc32c.
- Add winner_section_crc32c[17]. Whenever candidate selection promotes a
  candidate to winner, copy that validator's authenticated table here before
  the validator is reused for the next file.
- Add target_section_crc32c[17].
- Add a 17-bit updated_section_mask.
- Add current section index, section-relative cursor, current section size,
  mask-section cursor, header write cursor, and clean/transform classification
  flags.
- Retain generation/probe, candidate/winner flags, patch counters, file handles,
  seek state, partial-write state, and recovery selectors.
- Add/update comments listing inputs, outputs, and affiliates.
- Preserve the static assertion that the writer state fits
  FS_STAGE_CACHE_BYTES == 2048; do not create a new static allocation.
- Boot ensure and neither-valid recovery may reuse target_section_crc32c and
  the same section cursors because the facade makes these operations mutually
  exclusive with the runtime drain.

Current lines 969–1003 — update capacity assertions.

- Assert the expanded writer state still fits the union's 2,048-byte raw
  member and does not change its alignment/size.
- Keep the 4,608-byte patch cache and 512-byte staging buffer separate.

Current lines 5846–5871 — keep filesystem_autosaveCrcChunkBytes() and revise
the creation-selector scratch comment.

- Update its comment from one contiguous v1 record to any bounded header or
  section CRC interval. Keep AUTOSAVE_CRC_BYTES_PER_TICK == 128.
- Keep op_file_version as the proven-missing A/B selector used by
  filesystem_autosaveCreatedTargetGeneration(). Remove the claim that
  op_stream_index becomes one whole-record CRC; the 17 results live in
  op_autosave_writer.target_section_crc32c.

Current lines 5901–6248 — revise boot ensure creation without changing its
existence policy.

- Preserve HCNAMES mirror loading, complete case-insensitive root scan, and the
  rule that any existing target is left unopened and unmodified.
- For a proven-missing target, precompute 17 initial-section CRCs before CREATE:
  retain section index, section-relative cursor, and CRC across ticks, consume
  at most 128 synthesized bytes per pass, finalize into a 17-entry table, then
  format the v2 header.
- Preserve target selection separately from CRC scratch so missing A cannot be
  accidentally created as B.
- Stream exactly 34,832 v2 bytes through staging_buf, using physical commit zero
  in the header. Close and sync all data/header bytes, reopen/seek byte 5, write
  0xA5, close, and sync before advancing to the other target. This brings first
  creation under the same commit-last durability contract as runtime writes.
  The current v1 formatter writes an already committed file in one pass; that
  must not survive v2.
- Initial generations remain A=1 and B=0; initial probe remains zero.
- Preserve full-volume handling and close-before-error behavior.

Current lines 6250–6302 — keep error cleanup and recovery generation.

- Rollback still restores only successfully captured live offsets; section
  bookkeeping is transaction-local and requires no separate rollback.
- Recovery still writes B generation 0 first, syncs it, then A generation 1.

Current lines 6304–6527 — revise A/B validation and winner bookkeeping.

- Continue streaming both candidates in chunks no larger than 128 bytes.
- When a candidate becomes the current winner, copy its authenticated 17-entry
  table into winner_section_crc32c before reusing the validator.
- candidate_valid remains the strict full-record predicate.
- Preserve selection policy: Bank-matching record outranks mismatched; otherwise
  wrapping-newer generation wins; A wins equal-generation ties.
- Preserve whole-resident dirty marking when the only selected valid winner has
  a different Bank identity. Do not route identity mismatch into invalid-pair
  regeneration.
- Preserve trace meanings; the V record still reports complete candidate
  selection and does not imply partial-section recovery.

Current lines 6528–6653 — replace the one contiguous mask import.

- Reopen the validated winner once.
- Iterate Bank then Scenes. Seek to each section's mask start and read exactly
  16 or 240 bytes through staging_buf.
- Merge each chunk into the corresponding canonical mask offset through
  autosave_maskMergeChunk().
- Retain seek-in-progress/ftell verification and exact short-read errors.
- The M trace's value remains the total 3,856 mask bytes merged.
- Preserve the read-only completion when the merged canonical mask is empty.

Current lines 6655–6709 — add transaction-local section dirtiness tracking.

- Clear updated_section_mask before classification.
- When autosave_maskBitTake(payload_offset) returns set, determine its Bank or
  Scene section and set that section bit before calling the live getter.
- Set the bit even if the cell proves nonexistent and produces no patch:
  taking it changed the canonical mask that must be serialized, so forwarding
  the old section CRC would be wrong.
- Preserve the 1,536 successful-get cap, 256 examined positions per tick,
  sorted payload offsets, atomic take-before-get ordering, and continuation.
- Do not change any mutation producer or getter.

Current lines 6710–6920 — replace the monolithic source-to-target stream with
header placeholder plus 17 section copies.

- Preserve winner reopen and inactive case-folded variant removal/create.
- On a new target, write exactly 128 zero placeholder bytes first so commit is
  invalid and the first section begins at offset 128.
- Seek the reopened winner source to absolute offset 128 and verify queued seek
  completion with ftell before reading the Bank section; the source header was
  already validated and is replaced, not copied.
- Iterate sections in wire order. Before each section, classify it as transform
  when its bit is in updated_section_mask or
  autosave_maskSectionHasDirty(section) is currently true; otherwise classify
  it clean.
- Never let a read chunk cross the current section boundary.
- Clean section: copy source bytes unchanged, and copy the authenticated
  winner table entry into target_section_crc32c. The 512-byte staging buffer may
  be used for ordinary copy throughput; no CRC work is required.
- Transform section: initialize raw CRC state, read at most 128 source bytes per
  pass, call autosave_transformDrainChunk(), update CRC from those exact target
  bytes, then handle partial fwrite. Finalize at the section end into the target
  table.
- The patch cursor remains monotonic over canonical payload offsets across all
  17 sections. A clean section cannot contain a captured patch because capture
  set updated_section_mask before storing it; assert or fail if that invariant
  is violated.
- A mutation which occurs after a clean-section decision remains set in
  canonical SRAM for the next transaction. It does not authorize altering that
  section while forwarding its CRC. A dirty bit visible at classification
  forces transformation so the target section's mask and CRC agree.
- Preserve exact source EOF/length errors, zero-write back-pressure/full-volume
  handling, separate source and target close retries, and rollback on error.

Current lines 6921–7135 — replace four-byte whole-record CRC publication with
full-header publication.

- After all section bytes are written, close both handles and sync the invalid
  target data.
- Call autosave_formatCommittedHeader() with generation winner+1, probe
  winner+1, and target_section_crc32c.
- Copy the committed header to staging_buf, set only staged byte 5 to zero,
  reopen target r+, seek offset 0, and write all 128 bytes with a retained
  partial-write cursor.
- Close and sync that header while commit remains zero.
- Reopen, seek offset 5, write one 0xA5 byte, close, and enter the existing
  shared final sync.
- Remove the seek-to-offset-12/four-byte CRC phases and their whole-record CRC
  comments.
- Preserve the P trace point after commit close and before the final sync; its
  generation/target meaning does not change.

Current lines 7136–7395 — revise neither-valid pair recovery.

- Preserve dedicated HCNAMES mirror loading; do not borrow fs_list_cache_name.
- Share the boot-ensure section-CRC preparation helper/state rather than
  maintaining a second format implementation.
- Before removing each target, precompute all 17 section CRCs and the committed
  v2 header in bounded intervals.
- Remove/recreate/write B=0 first. Write the prepared authenticated header with
  physical commit zero, stream all sections, close/sync the file, then
  publish/close/sync commit. Unlike the runtime transform, recovery knows its
  complete section table before CREATE and needs no zero-header placeholder or
  second full-header write.
- Only after B is durably valid may recovery remove/recreate A=1.
- Preserve close-before-error, duplicate collapse authorization limited to the
  invalid target, and normal final completion cleanup.

Current lines 7396–7429 — preserve writer error close-down.

- Account for a target header publication handle exactly as for the existing
  CRC/commit handles.
- Restore captured live offsets once and leave concurrent/uncaptured canonical
  bits intact.

Current lines 13251 and 13280–13285 — rename the Instrument Save diagnostic CRC
affiliate.

- Replace autosave_recordCrcBegin() with autosave_crc32cBegin().
- Keep autosave_crc32cByteUpdate() and the intentionally unfinalized diagnostic
  fingerprint behavior.
- Correct the comment so it no longer compares against a whole-record-specific
  finalization API.

Current lines 21712–21769 — no functional scheduler change.

- Preserve success/error debounce, 250 ms continuation, tracking, and recovery
  flags.
- Update any nearby wording that says a whole-record CRC is published.

Current lines 21902–22008 and 22951–23032 — no functional change.

- Preserve admission, Load/Save page guard, trace/witness behavior, and the
  post-ensure whole-resident dirty boundary.

### 4.4 Core/Hardware/SD/filesystem.h

Current lines 233–268 — rewrite the public ensure/writer comment.

- State v2's exact 34,832-byte layout and format version.
- Explain section CRC table plus header CRC, interleaved masks, zero baseline,
  exact HCNAMES inputs, and A=1/B=0 initial generations.
- State that creation and runtime target publication both keep commit zero
  through data/header sync and write 0xA5 last.
- Preserve the existing policy descriptions: no-Bank no-op, existing objects
  untouched during ensure, tracking enable boundary, canonical mask ownership,
  bounded capture, read-only clean completion, continuation/retry behavior, and
  full-volume failure.

No public filesystem function signature changes.

### 4.5 config.h

Current lines 335–347 — comment-only correction.

- Keep AUTOSAVE_CRC_BYTES_PER_TICK at 128.
- Describe the input as one header or section interval, not a contiguous
  34,768-byte record.
- State that clean section copying is I/O but not CRC work.

### 4.6 Core/Menu/menu.c

Current lines 4278–4294 — comment-only stale ownership correction.

- Remove the claim that the AutoSave writer reads fs_list_cache_name live.
- Explain that Session 058's dedicated hcnames_name_mirror removed that alias,
  while the early busy guard remains required to avoid refused-request retry
  livelock and direct cache mutation during other filesystem owners.
- Do not change the guard's behavior.

Current lines 8158–8180 — comment-only geometry correction.

- Replace 34,768 with 34,832 and describe the retained full-record A/B writer.
- Do not imply per-section CRC makes facade ownership surgical in Session 060.

### 4.7 tools/verify_bank_autosave.py

Current lines 18–35 — replace v1 constants and CRC assumptions.

- Define header 128, record 34832, table offset/count, Bank/Scene section
  geometry, and canonical payload-relative inner offsets.
- Split raw CRC32C from header CRC calculation.

Current lines 114–124 — implement strict v2 validation.

- Require exact size, magic, version 2, commit 0xA5, zero reserved bytes, valid
  header CRC, and all 17 section CRCs.
- Return or print header validity and a 17-entry section status vector so a
  corrupt fixture is diagnosable even though winner selection remains strict.

Current lines 246–337 — update winner and field decoding.

- Keep Bank-match/new-generation/A-tie selection identical to firmware.
- Read Bank slot/name/present mask and Scene/Kit/Instrument fields through v2
  section data bases.
- Report each stored/calculated section CRC and identify the Bank/Scene number
  on mismatch.
- Preserve the tool's read-only behavior.

Add a read-only --self-test mode in this same script covering:

- valid initial v2 A/B records;
- header-bit corruption;
- table-entry corruption;
- one Bank byte corruption;
- one byte in each Scene section;
- short and one-byte-overlong records;
- v1 rejection;
- generation wrap comparison and equal-generation A tie.

### 4.8 No changes to tools/decode_devlogs.py

Its AutoSave payload decoder uses canonical payload-relative offsets and trace
stage meanings, neither of which changes. Do not churn it merely because the
wire record is interleaved.

### 4.9 No changes to AsyncFATFS

Core/Hardware/SD/asyncfatfs/asyncfatfs.c and .h are out of scope. Session 059's
live working-tree changes, seek size publication, cache behavior, scheduler,
sync rules, and public API remain untouched. The v2 state machines must use the
existing fopen/fread/fwrite/fseek/fclose/sync contracts.

## 5. Required state-machine sequences

These sequences are acceptance-level contracts, not illustrative pseudocode.

### 5.1 Boot ensure for a missing target

    load/validate HCNAMES mirror
      -> complete case-insensitive root absence scan
      -> calculate Bank section CRC in <=128-byte ticks
      -> calculate Scene 0..15 CRCs in <=128-byte ticks
      -> format authenticated committed header
      -> CREATE proven-absent canonical target
      -> write header with physical commit 0
      -> stream all 17 initial sections
      -> close
      -> sync data/header
      -> reopen + seek commit byte
      -> write 0xA5
      -> close
      -> sync
      -> advance A/B selector

Existing objects, including v1 objects, are not opened or rewritten by ensure.

### 5.2 Runtime candidate selection

    validate A header + all sections + exact length
      -> retain A metadata/table
      -> validate B header + all sections + exact length
      -> retain B metadata/table
      -> prefer Bank match
      -> otherwise prefer wrapping-newer generation
      -> A wins equal-generation tie

If no complete v2 record is valid, enter pair recovery. If a complete record is
valid but Bank identity differs, retain it as source and mark the whole
gettable resident Bank dirty as current code does.

### 5.3 Normal full-copy drain

    reopen winner
      -> seek/read/merge 17 mask slices into canonical SRAM
      -> if canonical clean, close and complete read-only
      -> atomically take/classify <=1536 live bytes
      -> mark every taken cell's section updated
      -> reopen winner source
      -> remove inactive target variants only
      -> create inactive target
      -> write 128-byte invalid placeholder
      -> for section 0..16:
           if updated or currently has canonical dirty bits:
               bounded read -> transform -> CRC -> write
           else:
               ordinary bounded read -> verbatim write -> forward CRC
      -> close source and target
      -> sync section data
      -> format/write complete header with commit 0
      -> close
      -> sync header
      -> reopen/seek/write commit 0xA5
      -> close
      -> shared final sync
      -> success/continuation callback

The prior valid winner remains untouched throughout.

### 5.4 Neither-valid recovery

    load HCNAMES mirror
      -> prepare B generation 0 section table/header
      -> replace B using prepared commit-zero header, file sync, commit-last
      -> sync B valid
      -> prepare A generation 1 section table/header
      -> replace A using the same publication order
      -> final sync
      -> clear temporary recovery ownership

At no point after B publication begins may both targets be intentionally in
destructive progress.

## 6. Version transition policy

There is no v1 migration reader in Session 060.

- A file with version 1 fails v2 stream validation.
- Boot ensure treats any case-folded existing object as present and does not
  modify it, exactly as today.
- Once the runtime writer is admitted, a pair of old v1 records produces no
  valid v2 winner and enters the existing neither-valid recovery path.
- Recovery writes B v2 generation 0 and makes it durable before replacing A
  with v2 generation 1.
- If only one file is missing, ensure may create that missing peer as v2 while
  leaving the existing v1 file untouched. Runtime validation then selects the
  valid v2 peer; the next normal copy replaces the inactive v1 peer.
- Removing AutoSave files manually remains a supported clean-reset procedure,
  but firmware must not require it.

This is explicit rejection plus safe regeneration, not silent reinterpretation
of reserved v1 bytes.

## 7. Invariants implementation must preserve

### 7.1 Power-loss safety

- The selected winner is never modified by a normal drain.
- A new target is ineligible until section data and its authenticated header
  are durable while commit remains zero. Runtime transform uses separate data
  and final-header sync gates; precomputed initial/recovery images may make
  both durable in one closed-file sync.
- Commit 0xA5 is the final byte changed and receives the final sync.
- A header table cannot be trusted unless the header CRC passes.
- A section cannot be trusted unless its authenticated table entry and its own
  bytes pass.

### 7.2 Dirty-state ownership

- Autosave.c owns the sole retained 3,856-byte canonical mask.
- Producers only OR bits through typed marker APIs.
- The writer atomically takes before reading live data.
- A mutation after take re-dirties for a later continuation.
- Successful live captures are restored on every writer error.
- A nonexistent format cell clears without consuming patch capacity, but its
  section is still marked updated so stale mask bytes/CRC cannot be forwarded.
- No operation-local section bitset replaces or clears canonical SRAM state.

### 7.3 Memory and foreground bounds

- No record-sized buffer and no second 3,856-byte mask.
- fs_stage_workspace remains exactly 2,048 bytes.
- fs_autosave_parameter_cache remains exactly 4,608 bytes.
- staging_buf remains 512 bytes.
- CRC work is at most 128 bytes per filesystem tick.
- Mask classification remains at most 256 payload positions per tick.
- Live capture remains at most 1,536 successful gets per generation.
- No blind delay or blocking SD loop is added.

### 7.4 Filesystem policy

- One facade owner at a time.
- Case-insensitive names and complete absence scans remain mandatory.
- Existing ensure targets are never opened for write.
- Normal drain removes variants of the inactive target only.
- Neither-valid recovery is the only path authorized to replace both, B then A.
- All opens, seeks, partial reads/writes, closes, and syncs retain asynchronous
  retry/error handling.
- HCNAMES input comes from hcnames_name_mirror, never the shared .hcindex name
  cache.

## 8. Verification recipe

### 8.1 Static inspection

- Search for AUTOSAVE_MASK_OFFSET and AUTOSAVE_PAYLOAD_OFFSET: no record-wide
  wire use may remain.
- Search for autosave_recordCrc and autosave_initialRecordCrc: no v1 API use may
  remain.
- Search active source/docs for 34768, 34,768, format version 1, contiguous
  mask, or whole-record CRC and update every current-contract occurrence.
  Historical session logs and historical failure evidence remain unchanged.
- Confirm no diff in asyncfatfs.c/.h or dirty-producer call sites.
- Confirm all public .h declarations have matching .c definitions and adjacent
  comments describing input, output, why, and affiliates.

### 8.2 Build and size gates

- Run the repository's normal firmware clean build.
- Treat any warning as a failure.
- Record text/data/bss and image size in the Session 060 handoff.
- Compare against Session 059's accepted baseline (text 385,420; data 404;
  bss 96,176; image 385,840) after first reproducing that baseline from the
  same toolchain/configuration.
- Verify fs_stage_workspace remains 2,048 bytes and that v2 adds no retained
  static array. Any unexpected BSS growth blocks completion.

### 8.3 Host-format tests

Use the updated read-only verifier and generated fixtures to prove:

- exact 34,832-byte size;
- header CRC calculation and bytes-12..15 zero rule;
- all 17 section bounds and CRCs;
- section table ordering Bank, Scene 0..15;
- interleaved masks map back to canonical bytes 0..3855;
- inner Bank/Scene/Kit/Instrument offsets retain the same logical values;
- one corrupt Scene does not mark another section invalid;
- strict writer validity still rejects the whole candidate when any section
  fails;
- header/table corruption invalidates header before any stored section CRC is
  trusted;
- v1, short, and overlong inputs reject;
- winner selection matches firmware, including wrap and A tie.

### 8.4 State-machine fault injection

Exercise or instrument failures at:

- candidate open/read/close;
- every per-section mask seek/read;
- inactive remove/create;
- placeholder-header write;
- clean and transformed section partial read/write;
- source and target close;
- first data sync;
- full-header seek/write/close/sync;
- commit seek/write/close/final sync;
- recovery B and A at the same boundaries;
- full-volume zero-write termination.

After each injected failure verify:

- old winner remains valid for normal copy;
- unpublished target is rejected;
- captured offsets are restored;
- unprocessed/concurrent dirty bits remain;
- no handle is handed to another operation before close;
- retry cadence remains five seconds unless successful continuation work
  remains, in which case it is 250 ms.

### 8.5 Hardware acceptance

On representative SD cards:

1. Delete both hidden records and boot. Confirm A=1 and B=0 are each exactly
   34,832 bytes, committed, header-valid, and all 17 sections valid.
2. Reboot with both v2 records. Confirm ensure leaves them untouched and the
   first clean drain is read-only.
3. Boot with the current Session 059 v1 pair. Confirm runtime recovery writes
   durable B v2 before replacing A v2 and does not freeze pre-audio boot.
4. Mutate one Bank field and one field in selected Scenes. Confirm generation
   advances, target sections/table validate, untouched sections are byte-for-
   byte equal to the source, and their stored CRC entries are forwarded.
5. Force more than 1,536 gettable mutations. Confirm the short continuation,
   multiple generations, rollback safety, and eventual empty canonical mask.
6. Switch Banks so winner identity mismatches. Confirm the existing full-
   resident dirty path is used, not pair recovery.
7. Interrupt power separately during section data, header publication, and
   commit publication. On restart at least the prior winner must remain
   selectable.
8. Confirm no new AutoSave-related audio glitch/freeze regression. Record
   measured durations as observations only; do not substitute draft estimates.

## 9. Documentation and closeout changes

These are part of implementing Session 060, after source behavior is verified.

### 9.1 knowledge_files/specification_reference/AUTOSAVE.md

Current lines 64–125 and 275–341:

- Replace v1 geometry with the exact v2 header/section tables from section 2.
- Define header CRC versus section CRC validation and strict writer selection.
- Document full-copy clean-forward/transformed-section behavior honestly.
- Update record-size statements to 34,832 where they describe current format.
- Retain 34,768 and 32,768 only in explicitly historical Session 056 evidence.
- Add the v1 rejection/regeneration policy.
- Preserve lifecycle, dirty producers, duplicate rules, HCNAMES mirror,
  bounded work, power-loss, and reader-not-implemented statements.
- Remove the stale known Bank Save mask limitation if the live Session 059 code
  and closeout evidence confirm it is already resolved; do not reintroduce it
  as a v2 task.

### 9.2 knowledge_files/specification_reference/FILESYSTEM_SPEC.md

Current lines 1857–1877:

- Keep AUTOSAVE.md as sole detailed format authority.
- Update the boundary summary to v2 authenticated sections and retained
  full-copy A/B ownership.
- Preserve the statement that no AutoSave reader or per-object dot-backer is
  implemented.

### 9.3 knowledge_files/specification_reference/MODULE_INTERCHANGE_SPEC.md

Current lines 646–663:

- Change “serialize/validate v1 records” to v2 header/section format and CRC
  helpers.
- State filesystem owns file/state-machine sequencing while Autosave owns wire
  mapping and canonical dirty state.
- Add the per-section validation accessor as future-reader-facing affiliate.

### 9.4 knowledge_files/specification_reference/SRAM_MANIFEST.md

Current lines 92–106 and totals below:

- Do not add a new allocation row if the expanded writer state remains within
  fs_stage_workspace.
- Update the workspace description to mention the operation-local 17-entry CRC
  tables.
- Record measured build totals and explicitly confirm the canonical mask and
  patch cache sizes are unchanged.

### 9.5 knowledge_files/drafts/AUTOSAVE_READ_PLAN.md

Current lines 113–151 and implementation-order item near line 580:

- Point to S060_AUTOSAVE_CRC_SPLIT.md, not the nonexistent/stale Session 058
  plan name.
- Replace XOR aggregate with authenticated header CRC.
- Replace performance claims with the verified full-copy limitation.
- Keep per-section reader trust as a future consumer and keep the reader itself
  out of Session 060.

### 9.6 SCOPING_TARGETS.md

Current lines 374–386:

- Update the durability shorthand from a monolithic CRC to v2 authenticated
  header/section publication while preserving the canonical-mask explanation.

Current lines 458–470:

- Remove or mark resolved the stale claim that AutoSave reads
  fs_list_cache_name live. Session 058's dedicated HCNAMES mirror is the real
  current state. Preserve any independently valid Menu busy/livelock target.

### 9.7 Session records

- Expand MEMORY.md with a concise Session 060 summary only after verification:
  exact format, transaction model, migration behavior, memory/build result, and
  hardware status.
- Create knowledge_files/log_archive/060_SESSION_HANDOFF_LOG.md with commands,
  results, commit/worktree state, source locations, known limitations, and next
  steps.
- Add Session 060 to knowledge_files/log_archive/000_SESSION_INDEX.md.
- Do not rewrite historical logs or change their historical 34,768-byte facts.

## 10. Complete change matrix

| File | Kind | Required result |
| --- | --- | --- |
| Core/Bank/Scene/Autosave.h | API/wire | v2 constants, section accessors, validation state/accessors, raw CRC API, updated formatter/transform declarations |
| Core/Bank/Scene/Autosave.c | implementation | interleaved formatter, header/section CRC, per-section validator, coordinate translation, section dirty query |
| Core/Hardware/SD/filesystem.c | implementation | bounded v2 ensure/recovery, per-section validation/mask import/copy, header publication, retained A/B safety |
| Core/Hardware/SD/filesystem.h | public docs | exact v2 lifecycle/format contract |
| config.h | comment | 128-byte header/section CRC budget wording |
| Core/Menu/menu.c | comments | new record size/full-copy wording and corrected HCNAMES cache ownership |
| tools/verify_bank_autosave.py | host verifier | strict v2 parser, section diagnostics, fixtures |
| knowledge_files/specification_reference/AUTOSAVE.md | authority | complete v2 format and migration truth |
| knowledge_files/specification_reference/FILESYSTEM_SPEC.md | authority | filesystem boundary summary |
| knowledge_files/specification_reference/MODULE_INTERCHANGE_SPEC.md | authority | module ownership/API summary |
| knowledge_files/specification_reference/SRAM_MANIFEST.md | authority | unchanged allocations/measured totals |
| knowledge_files/drafts/AUTOSAVE_READ_PLAN.md | future plan | corrected v2 prerequisite, no XOR/timing claim |
| SCOPING_TARGETS.md | backlog | corrected CRC durability and resolved cache-alias claim |
| MEMORY.md | closeout | verified Session 060 decisions/results |
| knowledge_files/log_archive/060_SESSION_HANDOFF_LOG.md | closeout | detailed evidence and remaining work |
| knowledge_files/log_archive/000_SESSION_INDEX.md | closeout | discoverable Session 060 entry |

Files intentionally absent from this matrix include asyncfatfs.c/.h,
BankData.c, SceneData.c, Preset.c, ordinary load/save implementations,
AutosaveTrace.c/.h, and tools/decode_devlogs.py. A diff in those files requires
a separately documented reason and scope review.

## 11. Recommended implementation order

1. Update Autosave.h geometry, assertions, types, and API contracts.
2. Implement raw/header/section CRC and the v2 initial formatter in Autosave.c.
3. Implement the v2 streaming validator and its per-section accessors.
4. Implement section coordinate translation, dirty-section query, and drain
   transform; run host geometry/CRC tests.
5. Convert filesystem boot ensure to bounded v2 precompute and commit-last
   creation.
6. Convert runtime validation, winner table retention, and per-section mask
   import.
7. Convert capture bookkeeping and the full-copy section loop.
8. Convert full-header publication and neither-valid recovery.
9. Update the host verifier and run corruption/winner-selection fixtures.
10. Build, inspect memory, run fault tests, then perform hardware acceptance.
11. Update authority docs and Session 060 closeout records from measured facts.

Implementation is complete only when the full-copy writer, ensure, recovery,
host verifier, current authority documents, and hardware evidence all agree on
one exact 34,832-byte v2 format and the source tree contains no active v1
record-layout path.
