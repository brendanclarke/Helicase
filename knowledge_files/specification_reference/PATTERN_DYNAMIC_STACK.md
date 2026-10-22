# Dynamic Pattern Stack Specification

## Authority and status

This is the authoritative live-memory, allocator, PAT4 interchange, Pattern
Stack Service, and Pattern AutoSave reference, **current as of the Session 081
close** (2026-10-16 code, `dev-ph6-cleanup`, documented 2026-10-21 after
Session 082; S082 Scene 17 notes are included where they apply). Session 073 changed
nothing. Session 074 changed no Pattern storage or allocator code; it added a
second reader of step automation (the Effect-page underline search reads every
step of every track of the viewed Pattern through
`pat_readStepAutomations()`, 4 steps per foreground pass) and saw a torn PAT4
file repair itself exactly as designed (§9).

**Session 081 changed this area in seven ways** (record:
`081_SESSION_HANDOFF_LOG.md`; written after S082, whose Scene 17 notes
appear below where they apply):

- **Step reset rule** (§6.1, §6.2): Instrument automation resets once per
  step at its first trigger, through a per-voice pending-reset mask;
  deferred steps queue their automation at the deferred fire; roll steps
  take shuffle and micro-timing and run their own length.
- **Automation priority R12** (§6.2a, amends S075 F3): a menu edit beats
  Instrument automation until the voice's next trigger; other targets take
  the menu value until their next automation. Effect parameters follow R13
  (`EFFECTS_BUS_REFERENCE.md` §10); the Effect step markers are gone.
- **Step roll table** (§6) corrected to 96-PPQ ticks, `64t`/`128` added,
  `48` dropped; `PAR_ROLL` retired.
- **`qnt trk`** (§6.5, §6.7): track-grid quantise for recording and
  per-track loop anchors.
- **Linear automation ramps `asm`** (§6.2b; labelled `rmp` / Automtn / Ramp
  since S083): per-track Pattern setting
  `track_auto_smooth`, PAT4 track header byte 5 (§7). Region 10,527 →
  **10,528 B**.
- **Live record and live erase** (§6.7): new `LiveRecord` module, Pattern
  Stack Service `RECORD_STEP`/`ERASE_SPECIALS` operations, staging table and
  target override (§12.3, §12.12), trace stage `W` (§12.16).
- **Looping while recording** (§6.5): the looper no longer locks record out;
  ring jump entries and loop-aware placement.

**Session 080 changed this area in five ways** (record:
`080_SESSION_HANDOFF_LOG.md`; PERF UI side: `MODULE_INTERCHANGE_SPEC.md`
"PERF performance surface"):

- **Trigger conditions** (§6): `lop` (23) / `!lp` (24) inserted after `!fl`;
  the table is 41 entries; `lst`/`!ls` move to 25/26 and the mute groups to
  27..33 / 34..40. No PAT4 migration (stored values ≥ 23 shift meaning).
- **PERF looper** (§6.5): a master-clock-anchored per-track loop engine in the
  TIM3 scheduler, driven by the PERF SELECT/VOICE/BAR buttons.
- **Scene change timing, queue and chaining** (§6.6): direct and queued
  scene changes at musical divisions, queue actions, the one PC reset, and
  per-Scene condition-driven chaining evaluated in TIM3.
- **Per-track looper exclusion** (§1, §7): `track_loop_exclude` region byte
  (bit N = track N not loopable), stored in PAT4 track header byte 4 (was
  reserved zero; PAT4 version unchanged). Region 10,526 → **10,527 B**.
- **Cursor reset no longer ends the looper** (§6.5): `seq_setStepIndexToStart()`
  rebases an engaged loop instead of clearing it; transport stop still ends
  it.

**Session 079 changed this area in five ways:**

- **7-special pool block format** (§4): expanded from 3 specials
  (note/velocity/probability) to 7 (note/velocity/condition/roll/timing/
  rhythm/fill) using flag bits 0–6. `PAT_SPECIAL_PROB_BIT` renamed to
  `PAT_SPECIAL_COND_BIT`; four new bit definitions. `PAT_SPECIAL_FLAGS_MASK`
  = `0x7Fu`. `pat_step_specials_t` widened to 8 fields (7 values + flags).
  `PAT_RAW_BLOCK_MAX` 132 → 136 (34 chunks). `pat_rawEncode()` signature
  widened from 3 positional values to `const pat_step_specials_t *`. Default
  fill = 100 (bit cleared when value == 100).
- **Swap block** (§3): `PAT_POOL_SWAP_CHUNKS` 33 → 34 (136 B) to match the
  wider maximum block.
- **Trigger condition system** (§6): probability replaced by a 39-entry
  condition dispatch (10 probability, 10 repeat-count X:Y, fill/!fill,
  last/!last, 14 mute/!mute across 7 tracks); self-mute bypass on muted
  tracks; `seq_trackRepeatCount[7]`, `seq_trackLastConditionFired[7]`,
  `seq_fillActive`.
- **Per-step roll engine** (§6): `seq_rollBegin()`, `seq_processRollTicks()`,
  `seq_rollComputeVelocity()` — 8 roll rates (absolute note values), packed
  rhythm byte encoding (21 ordered entries), fill percentage with ~5 ms
  output-gain duck via `instrumentManager_requestGateDuck()` /
  `instrumentManager_gateGain()` in the mixer. Carry-over across matching
  steps. Roll + duck state reset at all four sequencer reset sites.
- **PAT4 version** (§7): bumped from 1 to 2; reader rejects version 1.
  Offline converter `tools/convert_pattern_specials.py`.

**Session 078 changed this area in four ways:**

- **Per-track timing is now played** (§6.4): a continuous 128-position step
  scale with per-track Q8.8 DDA accumulators, per-track shuffle on the 1/16
  grid, and per-track run modes (`track_play_mode[7]`, §1, PAT4 track header
  byte 3, §7). The sequencer reads an effective (morphed/automated) cache, never
  the region directly, and never writes the region for timing.
- **Track timing Morph and automation** (§6.1, §6.4): Scene targets
  405..425 (length/scale/shuffle per track) are runtime-only overlays; Morph
  endpoints live in Scene settings (`BANK_PRESET_ARCHITECTURE.md`).
- **STEP-page editing** (§6.3): held SEQ steps broadcast velocity/note/
  probability; held steps + TRACK opens a held-step track-automation overlay;
  the STEP track-settings page has a Pattern-wide automation underline search
  and SHIFT+COPY pot clears.
- **Region size:** 10,519 → **10,526 B** (+7 B `track_play_mode`).

**Session 077 changed this area in two ways:**

- **`pat_background_region`** (§1): the standalone `pat_autosave_snapshot` is
now `pat_background_region`, an identical-type region outside
`pat_regions[]`. It serves the AutoSave snapshot, the copy/clear overlap
paste pool (`pat_backgroundPoolMut()`), and, since S082, Scene 17 (the
background Scene of a running Bank Load: index 16 resolves to this region
through PatternData's `pat_regionFor()` while `scene_backgroundExists()`).
These uses must never run concurrently: while Scene 17 exists the Pattern
AutoSave snapshot cannot run (AutoSave is gated) and an overlapping paste
drops (`SCENE17`); after the release both return. The copy/clear overlap path
  waits on `filesystem_patternSnapshotInUse()` instead of borrowing name-buffer
  space, and the paste source table is a static `uint16_t[128]` in
  `copyClearService.c` instead of being stored in the name buffer.
- **Per-track Scene playback** (§6): the sequencer reads
  `seq_perTrackPattern[track]` instead of `seq_activePattern` in all ISR/
  foreground step-advance, automation, and trigger paths, enabling independent
  track-to-Scene mapping during live performance.

**Session 075 (Phase 6) changed this area in three ways:**

- **Copy/clear** (`Core/Menu/CopyClear/`, reference `COPYCLEAR_UTILITIES.md`)
  replaced the Session 062 no-op copy APIs. It adds a permanent 132 B swap
  block to every pool (§3), an exclusive per-Scene write boundary for any
  resident Scene, a raw block API and sliding compaction (§12.17).
- **Automation priority** (§6.2a): a held step-automation value is never
  overwritten before the voice's next trigger; menu edits apply only their
  own parameter's interpolation; external MIDI was lowest priority (since
  S083 Q1 a MIDI write is a menu edit, §6.2a).
- **Documentation corrections** found while closing S075: the block header is
  big-endian (§4), the pending queue is a 128-record append queue (§6.1), and
  the drain writes voice values without 7→8-bit expansion (§6.1).

Historical Session 062/063/064 plans describe how the design was reached but
do not override this file.
Filesystem hierarchy and HCNAMES grammar are in `FILESYSTEM_SPEC.md`; scalar
and Pattern AutoSave scheduling/recovery are in `AUTOSAVE.md`; exact linked
memory totals are in `STORAGE_SRAM_MANIFEST.md`.

Implemented and hardware accepted:

- 16 independent resident Scene Patterns;
- 7 tracks × 128 steps per Scene;
- trigger state plus dynamic note, velocity, condition, roll, timing,
  rhythm, and fill specials (7 specials since S079; was 3 before);
- per-step automation entries (2-byte LE, 7-bit value + 9-bit target,
  up to 63 per step) with uniqueness invariant and dtype-aware editing;
- first-fit bit-packed pool allocator and block reclamation;
- Pattern/track settings storage, Menu edit, and PAT4 persistence; Sequencer
  condition playback (39-entry dispatch since S079, 41 since S080); per-track step-length playback (Session 068) and,
  since Session 078, per-track continuous step scale, shuffle and run modes
  with Morph and step automation (§6.4);
- sequencer automation playback: TIM3 copies to 32-entry debounced
  pending buffer, foreground drain via `instrumentManager_writeRuntime()`,
  per-slot dirty bitmap with morph-interpolation restore on voice trigger;
- step-edit automation pages (Method 1): cursor navigation, detail views,
  add/delete/clear, parameter cycling with uniqueness, dtype-aware display;
- exact binary PAT4 Scene/Bank/root Pattern Load and Save;
- scene-mask Pattern Load fan-out;
- per-Scene hidden A/B Pattern AutoSave and boot restore;
- HCNAMES Pattern identity rows 129..144;
- Phase 6 copy and clear of steps, ranges, bars, tracks, automation and whole
  Patterns (Session 075; partly hardware-tested by the user, see
  `COPYCLEAR_UTILITIES.md` §16).

Not implemented: live-record capture of automation, and real-time editing
guarantees while a snapshot is admitted during record/erase (admission is
instead deferred while those modes are active). Allocator
compaction/defragmentation is implemented via the Pattern Stack Service
(Session 067, see §12) and, for the copy/clear holder, as sliding compaction
(§12.17).

## 1. Resident object

`PatternData` owns:

```c
typedef struct __attribute__((packed)) {
    uint16_t address[7][128];       /* 1,792 B */
    uint8_t  pool[256 * 32];        /* 8,192 B */
    uint8_t  bitmap[512];           /* 512 B */
    uint8_t  track_length[7];       /* 7 B */
    uint8_t  track_scale[7];        /* 7 B */
    uint8_t  track_shuffle[7];      /* 7 B */
    uint8_t  track_play_mode[7];    /* 7 B, S078: 0 fwd .. 5 1fr */
    uint8_t  track_loop_exclude;    /* 1 B, S080: bit N = track N not loopable */
    uint8_t  track_auto_smooth;     /* 1 B, S081: bit N = track N asm on */
    uint8_t  pattern_change_bar;    /* 1 B */
    uint8_t  pattern_next;          /* 1 B */
} pat_scene_region_t;               /* 10,528 B */
```

The header `_Static_assert` (and its copy in PatternData.c) pins the size as
address + pool + bitmap + 32 (the parameter tail: four 7-byte track arrays,
the S080 loop-exclude mask, the S081 `asm` mask and 2 Pattern bytes; 23
before S078, 30 before S080, 31 before S081). `track_auto_smooth` (bit N =
track N ramps its automation, zero = `off`) has the accessors
`pat_setTrackAutoSmooth()` / `pat_trackAutoSmooth()`, is mirrored into
`PAR_TRACK_AUTO_SMOOTH`, copied with the region and by track copy, and reset
by track clear (§6.2b). `track_loop_exclude` is stored inverted so a zero-filled region or an
older PAT4 file means "every track loopable"; accessors
`pat_setTrackLoopable(scene, track, loopable)` (marks the Pattern dirty only
on change) and `pat_trackLoopable(scene, track)` (1 for invalid
coordinates); `pat_applyTrackSettingsToMenu()` mirrors the active track into
`PAR_TRACK_LOOPABLE`; `pat_rawRegionCopyBody()` copies it; copy/clear track
copy carries it and track clear sets loopable again. `pattern_change_bar` /
`pattern_next` are retired (no playback use) but still persisted. `track_scale` holds a step-scale **CC 0..127**
(S078; 76 = 1/16, §6.4), not the pre-S078 14-entry index.
`track_play_mode` values ≥ 6 are treated as `fwd`.

`pat_regions[16]` is exactly 168,448 bytes (S081; 168,432 in S080). Pattern storage is not
embedded in `scene_t`; `scenes[16]` is a separate 26,496-byte SceneData object
(S080: 1,656 B per Scene after the +4 B Scene chain settings; S078 had added
the 21 B track Morph endpoints).
`pat_sceneRegion(scene)` returns
read-only access and `pat_sceneRegionMut(scene)` is reserved for bounded owner
paths such as validated filesystem application. Ordinary clients use the
public operations so mutation tracking cannot be bypassed.

One additional `pat_background_region` is exactly 10,528 bytes (S077; it
replaces the former standalone `pat_autosave_snapshot` at identical size, and
grew with the region type in S078, S080 and S081). It serves four purposes that
must never run concurrently: the immutable source for an in-flight Pattern
AutoSave write (`pat_snapshotScene()` / `pat_autosaveSnapshot()`), the
copy/clear scratch pool for an overlapping paste
(`pat_backgroundPoolMut()`; the pool is a full PAT_STACK_SIZE * 32 bytes, so
it always fits what the source pool could hold), Scene 17 of a running Bank
Load, and the whole-region fast path of `preset_buildBackgroundScene()`.
Scene 17 is addressed only through `pat_regionFor(16)`;
`scene_indexValid()` stays 0..15, while `pat_patternValid(16)` is true only
while Scene 17 exists. AutoSave and an overlapping paste are gated off while
Scene 17 owns the region.

## 2. Address encoding

Every track/step owns one little-endian native `uint16_t`:

```text
bit 15       trigger active
bit 14       dynamic specials block exists
bits 13..0   4-byte-aligned byte offset into the Scene pool
0x3fff       no-data sentinel (never a valid aligned pool offset)
```

Turning a step off preserves bits 14..0, so an existing specials block can
survive off/on. Erasing a step, clearing a range, or initializing the Scene
frees any owned block and restores `PAT_ADDR_SENTINEL`.

An address is valid only if the offset is aligned, within the 8,192-byte pool,
the referenced chunks are occupied, the stored back-reference identifies the
same track/step, the special flags are known, and the block fits before the
pool end.

## 3. Pool and bitmap geometry

`PAT_STACK_SIZE == 256` is a historical sizing unit, not the number of
allocatable chunks. It creates an 8,192-byte pool (`256 × 32`). Allocation is
in 4-byte chunks, so the pool contains 2,048 chunks.

**Swap block (S075, widened S079).** The top 34 chunks (136 B, bytes
8,056..8,191, `PAT_POOL_SWAP_OFFSET`) of every Scene's pool are permanently
reserved: normal allocation (`pat_poolAlloc()`), service searches, repair and
reservations stop at `PAT_POOL_ALLOC_CHUNKS` (2,014 chunks, 8,056 B usable).
The block holds one maximum dynamic block (7 specials + 63 automations) and
is a guaranteed rewrite area: a step can be
republished without free pool space while its old block is still live.
Copy/clear is its first user; the reserve is kept for any later feature that
needs the same guarantee. PAT4 is unchanged (the reserve is free space);
Patterns saved before S075 may have blocks there, which copy/clear moves
below the reserve before first use (§12.17).

The 512-byte bitmap is bit-packed: bit `chunk & 7` of byte `chunk >> 3` owns
one 4-byte chunk. Only its first 256 bytes correspond to the 2,048 backed
chunks. Initialization clears those 256 bytes and fills the upper 256 bytes
with `0xff`, permanently reserving the unbacked range. Do not treat the bitmap
as one byte per chunk or limit the live pool to 1,024 bytes.

Allocation is deterministic first-fit linear search. Blocks are contiguous;
free clears exactly the occupied span. Allocation failure leaves the old step
block/value intact. The Pattern Stack Service (§12) provides two-tier
maintenance: Tier 1 trailing-gap merge after frees, and Tier 2 paced
compaction that relocates live blocks toward pool start.

## 4. Dynamic block format

A block starts at a 4-byte-aligned pool offset:

```text
bytes 0..1  big-endian header (byte 0 = bits 15..8, byte 1 = bits 7..0)
             bits 15..6: track * 128 + step (10-bit back-reference)
             bits 5..0: automation count (0..63), so byte 1 & 0x3F
byte 2      special flags: bit0 note, bit1 velocity, bit2 condition,
            bit3 roll, bit4 timing, bit5 rhythm, bit6 fill
bytes 3..   present values in ascending flag-bit order:
            note, velocity, condition, roll, timing, rhythm, fill
next bytes  automation entries, 2 bytes each, auto_count entries:
             each entry little-endian: bits 15..9 = 7-bit value (0..127),
             bits 8..0 = 9-bit instrument_param_id_t target
padding     zero to a 4-byte boundary
```

Block size: `chunks = (2 + 1 + popcount(flags & 0x7F) + auto_count * 2 + 3) / 4`.
One special with no automation is one 4-byte chunk. Seven specials with no
automation is three chunks (10 bytes padded to 12). Seven specials with 63
automations is 34 chunks (136 bytes). An unknown flag bit (bit 7) or an
inconsistent back-reference makes a block invalid.

Each automation entry's 9-bit target is the canonical `instrument_param_id_t`:
`slot * INSTRUMENT_PARAM_COUNT + descriptor_index` for voice parameters
(IDs 0..383), a Scene target ID (384+; since S078 including the track
timing targets 405..411 length, 412..418 scale, 419..425 shuffle, one per
track), or an Effect parameter ID 448..510
(block 7, Session 072 step 9). A step must never contain two
entries with the same 9-bit target (uniqueness invariant, enforced at write
time). The 7-bit value is an identity mapping: stored value = parameter value.
Every automatable descriptor parameter has a range that fits in 7 bits
(DTYPE_0B127, DTYPE_PM63, DTYPE_MENU, DTYPE_ON_OFF, DTYPE_MIX_FM,
DTYPE_LFO_POLARITY, DTYPE_NOTE_NAME, DTYPE_1B16). No DTYPE_0B255 parameter
is currently automatable. If a DTYPE_0B255 parameter becomes automatable in
the future, its conversion must be handled as a dtype-conditional special case.

Default resolved values when a special is absent are: note 63, velocity 100,
condition 0 (always fire), roll 0 (off), timing 0 (none), rhythm 0 (off),
fill 100 (full step). `pat_readStepSpecials()` always returns usable values
plus flags saying which were explicitly stored.

Writers use read-modify-write semantics with publication-safe ordering
(Session 067): allocate new block, write complete content, PRIMASK-protect the
address-entry swap (re-read trigger bit to avoid race with TIM3 step advance),
then free old block. In-place rewrite of same-size blocks is not used: even a
removal can compact automation bytes, creating a window where TIM3 reads
partially updated content. Clearing the last special removes the block and
bit 14 only when no automation entries remain; a block with zero specials but
nonzero automation count is preserved (flags byte = 0, no special values
stored, automation entries follow immediately). Erase and clear paths detach
the address entry before freeing the pool block.

## 5. Public behavior

Core operations validate Scene/track/step coordinates and do no work for an
invalid coordinate:

- `pat_isStepActive`, `pat_setStepActive`, `pat_toggleStep`, `pat_eraseStep`
  (Session 068: `buttonHandler_setRemoveStep()` emits a `DEV_MODE_LOGGING`-
  gated `K` witness record, via `AutosaveTrace.h`/`autosaveTrace_record()`,
  immediately before calling `pat_toggleStep()` — packs track/absolute
  step/pattern/pre-toggle trigger state, so a future report can distinguish
  front-panel input delivery failing to reach this call from a failure in
  the mutation itself. `K` is an AutoSaveTrace stage code, not a
  PatternTrace one — see `DEV_MODES.md` and note the two trace systems share
  some letters with different meanings);
- `pat_clearTrack`, `pat_clearPattern`;
- `pat_readStepSpecials` and the note/velocity/condition/roll/timing/
  rhythm/fill setters;
- `pat_readStepAutomations` — read decoded entries for one step (returns
  count);
- `pat_writeStepAutomation` — add or update one entry (uniqueness enforced,
  returns 1 on success, 0 on pool exhaustion or 63-entry ceiling);
- `pat_removeStepAutomation` — remove one entry by 9-bit target (returns 1
  if found);
- `pat_removeTrackAutomationByTarget` — remove all entries with a given
  target from all 128 steps of a track (returns count removed);
- `pat_stepAutomationCount` — count entries without reading them;
- track length/scale/shuffle and Pattern change-bar/next setters;
- menu apply helpers for Pattern, track, and selected-step state.

`pat_writeSpecials` preserves existing automation entries across
specials-only edits. `pat_eraseStep` and `pat_clearTrack` include automation
entries in block-size calculations when freeing pool chunks.

The Session 062 no-op copy surface (`pat_copyTrack`, `pat_copyPattern`,
`pat_copyBar`) was removed in S075. Copies are made by the copy/clear service
(`Core/Menu/CopyClear/`) through the raw block API (§12.17), which duplicates
pool blocks and never aliases one Scene's or step's allocation from another.

All real Pattern mutations converge on PatternData's local dirty helper. It
invalidates the Bank clean-Scene witness and calls
`autosave_markPatternDirty(scene)`. Direct mutable-region clients must provide
equivalent complete-operation marking or they violate AutoSave ownership.

Since Session 067, all pool-mutating operations from Menu, Sequencer,
and EuklidGenerator route through the Pattern Stack Service
(`patSvc_*` API) rather than calling `pat_*` mutation functions directly. The
service guarantees exactly one mutation target at a time and serializes all
pool access. See §12. The S075 copy/clear service is the one exception: it
writes through the raw block API only while it holds the service's exclusive
boundary (§12.17).

## 6. Sequencer and menu integration

The Sequencer obtains trigger/special state through PatternData. For a
triggered step it evaluates the trigger condition via
`seq_evaluateStepCondition()` — a 41-entry dispatch table (S080) covering
probability (r10–r95 via hardware RNG, 1..10), repeat-count X:Y conditions
(11..20, `seq_trackRepeatCount[7]`, modulo comparison), fill/!fill (21/22,
`seq_fillActive`, armed by PERF hold past `DOUBLE_CLICK_TIMEOUT`),
loop/!loop (23/24 `lop`/`!lp`, S080: `seq_loopActive`, §6.5), last/!last
chaining (25/26, `seq_trackLastConditionFired[7]`), and mute/!mute (27..33
`1mu..7mu`, 34..40 `!1m..!7m`; dispatch `(cond − 27) % 7`, boundary
`cond <= 33`) across all 7 tracks (with self-mute bypass: a step on a muted track whose
condition tests its own track's mute state bypasses the mute gate). Condition
0 (default/absent) always passes. A failed condition suppresses both trigger
and automation. The sequencer then triggers with the resolved note and
velocity. If the step has a roll special, `seq_rollBegin()` starts a
per-step retrigger engine at the specified rate (9 absolute note values,
independent of track scale; table below), with rhythm-modified velocity via
the packed byte pattern and a fill percentage that controls how much of the
step duration the roll occupies. Fill < 100 ducks the voice at the fill
boundary via `instrumentManager_requestGateDuck()`. If the step has a
micro-timing delay, the trigger is deferred by
`(step_interval * timing_value) / 100` ticks, additive with shuffle. Since
S081 a roll step takes the same deferral (shuffle and micro-timing) as a plain
trigger, and its first hit fires at the deferred time. Roll carry-over
extends across adjacent steps with matching rate/rhythm/fill.

**Step roll rate table (S081 P1 §5.4).** `seq_processRollTicks()` runs on the
96-PPQ tick, and `seq_rollPpq()` returns the retrigger interval in 96-PPQ
ticks (guard `rate > 9` → 0). Before S081 the table held 24-PPQ values, so
every step roll played 4× fast on hardware.

| Index | Menu label (`stepRollRateNames`) | Interval (96-PPQ ticks) |
|---:|---|---:|
| 0 | `off` | 0 |
| 1 | `1/8` | 48 |
| 2 | `8tr` | 32 |
| 3 | `16` | 24 |
| 4 | `16t` | 16 |
| 5 | `32` | 12 |
| 6 | `32t` | 8 |
| 7 | `64` | 6 |
| 8 | `64t` | 4 |
| 9 | `128` | 3 |

The old `48` entry (the same rate as `32t`) was dropped, so stored indices
above it moved down by one with no migration (decision F-H: backward
compatibility is not a constraint). The PAT4 byte format is unchanged. The
legacy `PAR_ROLL` live-roll parameter and its `rollRateNames` /
`MENU_ROLL_RATES` table were retired in S081.

**Roll length and the next step (S081 F-R).** A roll runs its own length:
the step interval × fill, measured from its (possibly delayed) first hit.
Only the next step's **trigger** cuts it short: a plain hit ends it, a
matching roll carries it, a different roll replaces it. An empty or
automation-only next step does not cut it (before S081 any step without a
roll ended a running roll at its start).

### 6.1 Step automation playback

THE DEFAULT FOR ALL AUTOMATION IS THAT IT PLAYS AND AFFECTS SOUND UNTIL THE
NEXT VOICE TRIGGER. AUTOMATION ON A NON-TRIGGER STEP IS VALID AUDIO
AUTOMATION DATA, VALUE CHANGES BETWEEN NOTES, AND TIMBRAL MOTION INDEPENDENT
OF RHYTHM ARE A CRITICAL PRODUCT FEATURE. ANY CHANGE THAT CONDITIONS
AUTOMATION QUEUEING ON TRIGGER STATE (BIT 15) DESTROYS THIS CAPABILITY AND
IS UNCONDITIONALLY WRONG.

On each step advance, `seq_advanceTrackStep()` reads automation entries from
every step that has a pool block (bit 14 set, valid offset), regardless of
trigger state. Each entry (except the Pattern-only off target `0x1FF`) is
appended to the TIM3-to-foreground queue `seq_pending_automation[]`:
`SEQ_PENDING_BUF_COUNT` (128) four-byte records (identity = step id | type
bit, payload = the packed entry) plus two publication bytes (+514 B SRAM1).
There is no coalescing; a full queue drops the entry and records a
PatternTrace overflow witness (`H`). The ISR is the sole writer. (The Effect
step markers that used identity bit 11 were removed in S081, R13: see
`EFFECTS_BUS_REFERENCE.md` §10.)

**When a step's entries are queued (S081 §5.8).** Normally at the step start.
When the step's own trigger is deferred (shuffle or micro-timing, §6.4), the
entries are queued at the **deferred fire time**, in TIM3, by
`seq_fireDeferred(track)`, together with the resetting trigger. The
foreground order `voiceControl_processPending()` →
`seq_drainPendingAutomation()` then gives reset → trigger → that step's
automation, exactly as for an on-time step. Before S081 a delayed trigger
fired after its own step's automation had landed and restored it away.

- A step with automation but no trigger still queues at its step start
  (the call stays in the step-allowed block; it is skipped only when this
  step's own trigger was deferred).
- `seq_fireDeferred()` is the one routine for both the expiry path and an
  early flush: it queues the step's automation, then starts the roll
  (`seq_rollBegin()`) or fires the plain hit with reset = 1. A muted
  deferral (`seq_trackShuffleMuteMask`, set by live record when a live hit
  replaced the step) queues the automation and returns without a trigger.
- A deferral still pending from an earlier step is flushed early only when
  the new step itself queues automation, so entries stay in step order and a
  long delay is not cut by an empty next step.
- The deferral state is `seq_trackShufflePending/Delay/Vel/Note[7]` plus,
  since S081, `seq_trackShuffleStep[7]` (to re-read the block) and
  `seq_trackShuffleRoll[7]`.

Trigger condition gates the complete step: trigger and automation together,
for any step that has a condition special set (value 1–40), whether or not the
step has an active trigger (bit 15). The condition is read from the dynamic
block before the trigger-active check so it applies to non-trigger steps with
automation. Default condition (0, or absent special) always passes — a step
without a condition plays unconditionally. A step whose condition fails
produces no trigger and no automation entries; previously held automation
values persist through the skipped step. Erase is independent of condition.

The foreground drain (`seq_drainPendingAutomation()`) runs inside
`audio_check_and_render()` immediately after `voiceControl_processPending()`,
within the per-chunk render loop. For each entry, it validates the target and
dispatches by target range. Values stay in descriptor space (identity
mapping, no MIDI-CC-style 7→8-bit expansion); only voice Morph Scene targets
expand to 0..255:

- **Voice descriptor targets (IDs 0..383):** calls
  `instrumentManager_writeRuntime(slot, descriptor, value)` and sets the
  corresponding bit in `seq_automation_dirty[slot]` (a `uint64_t` per-slot
  bitmap, 48 B total). That bit is the "automation holds this value" record
  (§6.2a).
- **Scene targets (IDs 384+, Session 070; `fxm` is ID 404):** calls
  `seq_applySceneAutomation()` which dispatches to runtime-only overlays —
  `presetMorph_setStepAutomationOverride()` for Voice Morph (with 7→8 bit
  expansion via `menu_morphAutomationExpand()`),
  `slot6_track7_decay_step_value` for generated slot-6 track-7 decay,
  `preset_applyVoiceAudioOutRuntime()` plus `preset_setAudioOutStepOverride()`
  for Audio Out routing, `preset_setFxSendStepOverride()` for FX Send (the
  mixer reads the effective send each block; the override beats the
  Normal/Morph send endpoints), `effects_setMorphAutomation()` for `fxm`, or
  (S078) `presetMorph_setTrackParamStepOverride()` for the track
  length/scale/shuffle targets 405..425, which also writes the sequencer's
  effective cache directly so the next DDA tick follows (§6.4). Sets the corresponding bit in
  `seq_scene_automation_dirty` (`uint32_t`, 4 B). Scene automation never
  writes retained Scene/Kit setters — this prevents AutoSave thrashing and
  preserves user-set values.
- **Effect targets (IDs 448..510, Session 072 step 9; rules rewritten in
  S081, R13):** the drain calls `effects_applyAutomation(track, local,
  value7)`, which sets that parameter's track overlay (last writer wins,
  held) and clears its `menuHold`. The overlay is cleared by an FX-sequence
  lock on that parameter at its lock step start, by a menu edit, and by the
  common reset latch (Scene change, reload, realign, transport reset). The
  former step markers, owner tracks and "a failed-condition or muted step
  holds it" rule are gone. Full resolution order: `EFFECTS_BUS_REFERENCE.md`
  §10.

**Drain filters (S081).** Before applying an entry the drain skips it when:

- live record has the target **latched** (any owner track) or **erased**
  (owner track = the recording track), through the bitmaps LiveRecord
  registers with `seq_setAutomationFilter()` (§6.7);
- the entry is an Instrument target whose `seq_menuOverride[slot]` bit is set
  (R12, §6.2a).

It also ends automation ramps of tracks whose next step has applied and may
start new ramps (§6.2b).

### 6.2 Automation reset on voice retrigger and transport restart

All trigger sources funnel through `voiceControl_triggerNow()` in
`MidiVoiceControl.c`. When the trigger carries a **reset**, it calls
`seq_restoreAutomatedParameters(voice)` before
`instrumentManager_triggerTrack()`. The restore iterates set bits in the dirty
bitmap using `__builtin_ctzll`, writes the `morph_interpolation[]` value for
each dirty descriptor back to the runtime, and clears the bitmap. The dirty
bitmap is also cleared on `seq_init()`, transport stop, and
`seq_setStepIndexToStart()`.

**Step reset rule (S081 P1 §5.8, decision F-L).** Before S081 every trigger
restored, which caused two defects: a shuffled or micro-timed trigger undid
its own step's automation (the step start had already applied it), and the
second hit of every roll restored the base values. Now Instrument automation
(the held bits and the R12 menu override) resets **once per step, at the
step's first trigger**:

| Step | Reset at |
|---|---|
| Plain trigger | its hit |
| Micro-timed / shuffled trigger | its delayed hit (its automation is queued at the same moment, §6.1) |
| Roll step | its first hit; later hits of that roll never reset |
| Roll carried across steps | each step's renewal of the roll, which fires no new first hit (`voiceControl_resetOnly()`) |

- A roll whose hits run into the next step's time does not reset there; the
  next step's own trigger does.
- Live triggers (overlay presses, MIDI notes, preview) reset at each new
  press. Each flam hit counts as a press. Overlay roll repeats from the 1 ms
  timer do not reset.

**Mechanism: a per-voice pending-reset mask.** `voiceControl_pendingReset`
(`u8`, bits 0..6, `MidiVoiceControl.c`) records that a voice owes a reset.

- `seq_triggerVoice(voice, vol, note, reset)` passes `reset`: 1 for on-time
  step hits, deferred fires, `seq_rollBegin()`'s first hit, overlay presses,
  preview and SOM; 0 for `seq_processRollTicks()` retriggers; the overlay
  1 ms timer passes `bh_overlayCount != 0xFF` (1 for a flam main, 0 for roll
  repeats). `voiceControl_noteOn()` (MIDI, preview, overlay) always sets the
  bit; `voiceControl_noteOnNoReset()` serves roll retriggers 2+;
  `voiceControl_resetOnly(voice)` sets the bit with no trigger (roll
  renewal).
- `voiceControl_popPending()` tests and clears the voice's bit inside its
  existing PRIMASK section (TIM3 sets bits concurrently) and returns it as
  `reset`.
- `voiceControl_triggerNow(voice, note, vel, reset)` keeps the order
  `preset_applyDeferredSceneSlotForTrigger()` → restore (only if `reset`) →
  trigger, sidechain, LED. The restore must not move ahead of the deferred
  Scene-slot apply.
- After the ring is empty, `voiceControl_processPending()` takes the
  remaining mask under PRIMASK (copy, store 0) and restores each voice it
  took: reset-only renewals, and resets whose trigger entry was dropped.
- **Why a mask and not a flag in the queue entry:** `voiceControl_noteOff()`
  deletes every queued entry of the voice, and `seq_triggerVoice()` calls it
  before every `noteOn()`, so a flag stored in an entry is lost whenever a
  later trigger of the same voice is queued before the foreground drains. A
  stored step note or velocity byte ≥ 128 is also not range-checked on PAT4
  load, so it could read as a flag.
- Accepted edge: if the foreground falls behind, a reset bit set by step N+1
  can be consumed by a still-queued roll retrigger of step N; both run
  before the drain, so that retrigger only sounds with the base values.

All of this precedes `seq_drainPendingAutomation()` in `main.c`, so a step's
own automation always lands after its reset.

On transport restart (Session 070), `seq_setStepIndexToStart()` calls
`seq_restoreAllAutomation()` (walks all 6 voice dirty bitmaps and restores
from `morph_interpolation[]`) and `seq_restoreAllSceneAutomation()` (walks
`seq_scene_automation_dirty` and restores each Scene target from its retained
SceneData getter: clears morph step overrides, slot6 decay step state, and
audio routing overrides). Both restore functions run BEFORE
`seq_clearAutomationDirty()`. `seq_setRunning()` is structured so stop sets
`seq_running=0` first (preventing TIM3 ISR from advancing during cleanup)
and start sets `seq_running=1` last (after all initialization).

(S080 and earlier queued Effect step markers before each track's entries;
S081 removed them together with the Effect owner state. Effect overlays now
follow R13, `EFFECTS_BUS_REFERENCE.md` §10.)

### 6.2a Automation priority (S075 F3, amended by S081 R12)

**Rule (user, S075 F3):** a voice-parameter automation value written by the
drain holds until that voice's next trigger. Nothing else may change that
parameter's runtime value before then — **except a menu edit of that
parameter (S081 R12, below)**.

**R12 (S081):** a menu edit of an automated Instrument parameter sounds at
once and suppresses that parameter's automation until the voice's next
trigger (any source), a transport change or a Scene change. A menu value is
never reverted by anything (also not when recording or overdub ends).

- `menu_cellCommitValue()`, INSTRUMENT branch (Normal or Morph image), calls
  `seq_menuOverrideSet(cell->slot, cell->descriptor_index)` once, before the
  edit-mask Scene loop (the override belongs to the one runtime slot, not to
  each Scene in the edit mask). It clears the held bit in
  `seq_automation_dirty[slot]` and sets the bit in `seq_menuOverride[slot]`
  (6 × `u64`, 48 B, foreground only).
- The cleared held bit lets the edit's own interpolated write through
  `presetMorph_writeRuntimeBase()`, so the new value sounds at once.
- While the override bit is set, `seq_drainPendingAutomation()` skips that
  target, and an automation ramp on it is cancelled (§6.2b).
- The override is cleared by any trigger of the voice
  (`seq_restoreAutomatedParameters()` clears `seq_menuOverride[slot]`
  **before** its "held mask is zero" early return, because the menu edit has
  just cleared the held bit), by transport start/stop/reset
  (`seq_setStepIndexToStart()`), by a Scene change
  (`seq_selectActivePattern()`, `seq_alignActivePatternToScene()`), by a
  per-track Scene change of that slot (`seq_setTrackPlayedScene()`; tracks 6
  and 7 share slot 6) and by realign.
- The Morph sweep guard is unchanged. **S083 Q1:** a MIDI write is a menu
  edit, not a lower priority: `MidiControl` writes through
  `menu_commitTargetEdit()`, which calls the same `menu_cellCommitValueEx()`
  path (R12 for Instrument targets, R13 for Effects, the per-target clears
  for Scene targets) and records through `liveRec_paramIn()`. The retired
  S075 F3 "MIDI lowest" path (`preset_setInstrumentParameterFromMidi()`) no
  longer exists.

**Other (non-Instrument) targets: the menu value applies until the next
automation of that parameter.** A menu edit clears that target's runtime
override, and the next automation entry applies again:

| Target | Clear on a menu edit |
|---|---|
| Voice Morph `Nvm` | (already cleared before S081) |
| FX send `Nfx` | `preset_clearFxSendStepOverride(slot)` |
| Audio out `Nou` | `preset_clearAudioOutStepOverride(slot)`, which also re-applies the retained route |
| Track length/scale/shuffle (405..425) | `presetMorph_clearTrackParamStepOverride(track, param)` + `seq_refreshTrackEffectiveParams()` (Normal and Morph-endpoint commits, `menu_clearTrackTimingOverride()`) |
| `7dc` | its slot-6/track-7 decay override clear |
| `fxm` and Effect parameters | `menuHold` in EffectsManager (R13) |

Each clear pairs with `seq_clearSceneAutomationBit(index)`.

**Global Morph (`mrp`, CC1 on G; S083 follow-up).** The bulk Morph set
(`preset_morphScene()`) clears every voice's step Morph override (S076 Rule
A) and also sets the Effect Morph as an `fxm` menu edit
(`effects_setMorphAmountFromGlobal()`: the Pattern `fxm` override clears and,
on the active Scene, `menuHoldMorph` holds the new amount over an FX Morph
lock until the next Morph automation). Neither the voice nor the Effect
amounts it writes are recorded (G-1).

The S075 table below is still the rule for every writer other than a menu
edit:

| Writer | What it does to a held parameter | Mechanism |
|---|---|---|
| Morph sweep (`presetMorph_tick()`), synchronous voice apply (`presetMorph_applyVoiceNow()`) | updates `morph_interpolation[local]` only; the runtime value is left alone | every Morph-base runtime write goes through `presetMorph_writeRuntimeBase()`, which skips an inactive Scene and any parameter for which `seq_automationHoldsParameter(slot, local)` is set |
| Menu edit of any instrument endpoint (`preset_setInstrumentParameter()`) | stores the endpoint; re-interpolates **only that parameter** at the voice's resolved Morph amount (`presetMorph_applyParameterNow()`). Since S081 R12 the commit first calls `seq_menuOverrideSet()`, so the parameter is no longer held and the new value sounds at once | no whole-voice queue; the runtime never receives the raw edited value |
| External MIDI CC/NRPN (`MidiControl` → `menu_commitTargetEdit()`) | exactly a menu edit (S083 Q1): stores the active Scene's clamped Normal endpoint, calls `seq_menuOverrideSet()` and fans out; the value sounds at once | the menu edit's own guarded write (`presetMorph_applyParameterNow()` at the Morph amount) |

At the trigger, `seq_restoreAutomatedParameters()` writes
`morph_interpolation[local]` — which by then holds every edit and Morph
change made while the value was held — and clears the bitmap. Before S075 F3
a menu edit queued the whole voice and the worker overwrote every held
parameter mid-note; the same happened on Morph changes, an LFO on a voice's
Morph, `Nvm` automation and Instrument/Kit applies. Effect parameters and the
Scene targets (`Nfx`, `Nou`, `Nvm`, `fxm`, `7dc`) were never affected: they
are layered overlays read every block. An Instrument type change while values
are held is out of scope (a held bit may name the old type's descriptor until
the next trigger). Details: `075_SESSION_HANDOFF_LOG.md` §11.

### 6.2b Linear automation ramps: `rmp` / Automtn / Ramp (S081 P2)

**What the user gets.** A per-track **Pattern** setting `rmp` (category
`Automtn`, long name `Ramp`; labelled `asm` / Track / `Automtn` until the
S083 follow-up of 2026-10-22; code and storage names keep `auto_smooth`
and the PAT4 "asm" byte), values `off`/`on`, STEP mode track parameters, SEQ_PAGE sub-page 0
cell 7, after `lop`). `off`: every step's value is applied as a jump, as
before. `on`: an Instrument parameter ramps linearly from the current step's
value to the next played step's value, **only if both steps hold an entry for
that parameter**. Menu value → automated step and automated step → menu value
still jump. At most 64 ramps run at once; a pair without a free slot jumps.

**Storage.** `pat_scene_region_t.track_auto_smooth` (bit N = track N `on`;
zero = `off`), PAT4 track-header byte 5 (§7), `pat_setTrackAutoSmooth()` /
`pat_trackAutoSmooth()`; the Sequencer reads the region byte directly from
TIM3. Menu mirror `PAR_TRACK_AUTO_SMOOTH` (plain `DTYPE_ON_OFF`, a no-op in
`menu_parseGlobalParam()`, committed in `menu_cellCommitValue()` for the
viewed Pattern and the active track). Copy/clear track settings copy it;
track clear resets it. It is not a SceneModTarget: not automatable, not
morphable, not recordable.

**Terms.** Step A is the step whose automation is being applied; step B is
the step the track plays next in normal (unlooped) play. A ramp runs from A's
**apply time** (the step start, or the deferred fire time when A's trigger is
deferred, §6.1) to B's apply time, so it ends exactly where B's value would
land as a jump.

**Which targets ramp (D2).** Automatable Instrument descriptor parameters of
the track's slot with a numeric dtype: `DTYPE_0B127`, `DTYPE_PM63` (and
`DTYPE_0B255`, `0B15`, `1B16`, `1B128` should a descriptor use them),
valid in the track's played Scene. Never: `DTYPE_MENU` (waveforms, filter
types), `DTYPE_MIX_FM`, `DTYPE_ON_OFF`, `DTYPE_0b1`, `DTYPE_LFO_POLARITY`,
LFO/velocity targets, any Scene target (`Nvm`, `Nou`, `Nfx`, `7dc`,
`len/scl/shf`, `fxm`) and Effect parameters. Their in-between values are
other choices, not intermediate settings.

**Pieces** (`sequencer.c`, section `S081 P2 automation ramps`, directly after
`seq_restoreAutomatedParameters()`):

| Piece | Context | Role |
|---|---|---|
| `seq_ramps[64]` (`seq_ramp_t`, 10 B: `id` = 0 free or `1 + (track << 9 | target)`, `from`, `to`, `last`, `startQ4`, `endQ4`) | foreground | the running ramps; times in 1/16 master ticks, low 16 bits, wrap-safe |
| `seq_rampCtx[7]` (`seq_rampCtx_t`, 4 B: `a` = A step + bit 7 "stepped", `b` = B + 1 or 0, `endQ4`) | TIM3 → foreground | per-track hand-off, read under PRIMASK |
| `seq_rampStepEvent(track, applied, deferred)` | TIM3, every step of an `on` track | applied now → stepped + prediction; deferred → prediction only (`seq_fireDeferred()` sets the stepped flag when the entries queue); neither (condition failed, muted, full-erased) → stepped, no B |
| `seq_rampPredict()` / `seq_rampNextStep()` / `seq_rampConditionPredict()` | TIM3 | predict B and B's apply time (below) |
| `seq_rampBreakTrack()` / `seq_rampBreakAll()` | TIM3 or foreground (PRIMASK) | write "stepped, no B" at every position break |
| `seq_rampConsumeTrack()` / `seq_rampConsumeSteps()` | foreground | consume the stepped flag and free that track's ramps (values stay where they are) |
| `seq_writeInstrumentAutomation(scene, target, value)` | foreground | the drain's Instrument write (descriptor in the played Scene, `instrumentManager_writeRuntime()`, held bit) — shared by the drain and the ramp service |
| `seq_rampStartIfPaired()` | foreground, drain | start a ramp for a just-written entry of A |
| `seq_rampService()` | foreground, `main.c` render loop right after `seq_drainPendingAutomation()` | write running ramps |
| `seq_rampNowQ4()` | foreground | master position ×16 plus the sub-tick fraction from the PPQ µs stamp |

**Predicting B (TIM3, at A's step start).** `seq_rampNextStep()` mirrors the
play-mode switch of `seq_advanceTrackStep()` without side effects — **keep
the two in step**:

| Play mode | B |
|---|---|
| `fwd` | next index, wrapping at the effective length |
| `rev` | previous index, wrapping at 0 |
| `pip` | next index in the current direction; the boundary step repeats |
| `onc`, `1fr` | next index; none after the last step |
| `rnd` | none (always jumps, D7) |

`*repeatInc` is 1 when the advance to B increments the repeat count (wrap or
pip turn), so B's repeat conditions are evaluated with the count B will see.
No B (b = 0) when A has no automation, the track is muted or full-erased,
there is no B, B has no automation, or **B's condition is not predictably
passing** (D1): predictable are no condition, the repeat counters
`1:2`..`4:4` and `1:-` (`seq_conditionRepeatPasses()`, shared with playback),
and `lst`/`!ls` (A's result is already in `seq_trackLastConditionFired`);
not predictable are `rNN`, `fil`/`!fl`, `lop`/`!lp` and the mute
conditions, so B jumps. B's start is the next DDA advance,
`ceil((interval − remainder) / 256)` ticks away; when B holds a trigger its
automation is deferred like the trigger (shuffle on odd B plus micro-timing,
the same arithmetic as §6.4). The looper is ignored on purpose (D8).

**Pairing (foreground drain).** Each Instrument entry the drain writes on an
`on` track goes through `seq_rampStartIfPaired()`: dtype OK; the context
still describes this A with a B; B holds the same target
(`pat_findStepAutomation()`, a single-target lookup that copies nothing); the
predicted end is in the future → take a slot (or reuse the slot of the same
track/target) with `from` = A's value just written, `to` = B's value,
`last = from`. No free slot → jump (DEV `seq_rampNoSlotDrops`). The drain
follows the live producer count, so TIM3 can queue A's entries and set A's
stepped flag after the drain's top-of-pass consume; pairing therefore
consumes the flag itself, inside its PRIMASK section, when it still belongs
to this A, and ends the track's older ramps before starting the new one.

**Running (`seq_rampService()`).** Every DSP block (0.73 ms) it consumes the
stepped flags; every third block (`SEQ_RAMP_BLOCK_DIV`, ~2.2 ms, D6) it
computes `from + (to − from) × elapsed / duration`, rounded to the 7-bit
descriptor value (D9), and **writes only when the value differs from `last`**
(D11). After the predicted end it holds `to` and sets `from = to` (so a ramp
held more than 2,048 ticks cannot wrap negative). Elapsed time is master
ticks, so a tempo change stretches the ramp; `seq_rampNowQ4()` returns 15/16
of a tick at most past the stamp, so a stalled external clock holds the ramp
just before the next tick. A ramp is **cancelled** (value stays) when the
track's `asm` turns off, live record latches or erases the target, an R12
menu override is set, or the target is no longer valid in the played Scene.

**Ending.** All of a track's ramps end when that track's next played step
applies (its stepped flag is consumed): at the top of the trigger restore
for its own track, at the top of the drain for all tracks, and in the ramp
service. B's own entries then apply its exact values, and any of them that
pair with the step after B start the next ramps at once. Ending at B's
arrival (not at the computed time) keeps a late ramp sample from overwriting
B's value.

**Position breaks** end ramps ("stepped, no B"): `seq_setTrackPlayedScene()`
(tracks 5 and 6 together), `seq_selectActivePattern()`,
`seq_alignActivePatternToScene()`, both realigns, `seq_setRunning()`,
`seq_setStepIndexToStart()`, the loop wrap (`seq_loopProcessTick()`; no ramp
across the wrap back to the loop start, D8), `seq_loopResumeTrack()`, and the
chain `stp` freeze. Inside a loop the track ramps as in normal play; the last
loop step ramps toward the step after the loop end when that step holds the
parameter.

**Triggers during a ramp (D3).** `seq_restoreAutomatedParameters()` first
consumes its own track's flag, then neither restores nor un-marks the
parameters of its slot held by a running ramp, so a live press, MIDI note,
flam main or roll renewal mid-step does not dip to the menu value. Ramps of
the other slot-6 track keep running. Ramp samples go through
`seq_writeInstrumentAutomation()`, so they are held automation values under
§6.2a: the Morph sweep and menu apply never overwrite them before the next
trigger.

**Known limits.** A transport reset in TIM3 during a service pass can let
that pass write one more sample after `seq_restoreAllAutomation()` (it holds
until the voice's next trigger). B's apply time assumes B's trigger is
deferred when B is active; if live record suppresses B's trigger, B's
entries land at its nominal start and the ramp ends there, a little early.
The writer that bypasses the priority rule (supplemental parameters) holds
until the ramp value next changes. (The internal legacy MIDI writer is gone
since S083; MIDI writes are menu edits, §6.2a.)

**Cost.** TIM3: one prediction per step of an `on` track. Drain: one
`pat_findStepAutomation()` per paired entry. Service: at most 64 runtime
writes per pass (worst case 64 × ~450 passes/s); the 64-ramp CPU worst case
has not been measured yet (DSP CPU policy: budget the worst case; the
fallback is a 16-per-pass round-robin). RAM: 640 + 28 + 1 B, plus 1 B per
Pattern region (`STORAGE_SRAM_MANIFEST.md`).

**Live record and ramps.** Recording on an `on` track writes per-step values
as usual; latched targets play the menu value and erased targets on the
recording track are filtered, so neither ramps.

### 6.3 Step-edit automation pages

After the step-specials subpages (subpage 1: velocity, note, condition,
micro-timing; subpage 2: roll rate, rhythm, fill), the selected-step edit
page shows dynamically counted automation pages. Each
page displays one entry with a 5-item cursor: number (with number-lock
mode), del/clr action, voice, parameter (with uniqueness-filtered cycling),
and amount (with dtype-aware display and bounds clamping). An "add" page
follows the last assigned entry. Detail views show category+long_name for
parameter targets and dtype-aware named values for amounts.

The selected-step edit page reads the live dynamic block. Setting note,
velocity, condition, or any roll/timing special performs a tracked pool
read-modify-write and repaints the menu. Track and Pattern setting pages
read/write the fields in the resident region; PAT4 persists them.

The automation pages are entered by scrolling right from subpage 2 position 2
(roll fill). `menu_stepAutomationPageActive()` returns true when
`menu_stepAutoActive && activePage == 2u` (S079; was `== 1u` before S079).
Left-exit from automations returns to subpage 2 position 2. Step-button
re-press toggles sub 1 ↔ sub 2 via `menu_toggleStepEditSubpage()` (S079 P2). Every target change goes through
`menu_stepAutomationReplaceTarget()` and every delete through
`menu_stepAutomationExecuteItem0()`; since S078 both restart the
automation-presence search unconditionally (the former
`menu_isVoicePage()` guards were dead), so the STEP track-settings underline
(§6.3a) follows editor changes. Value-only edits and Add (`off` sentinel) do
not change presence.

### 6.3a STEP-page multi-step editing and track automation (S078)

**Multi-step specials broadcast.** In STEP mode with two or more SEQ buttons
held, a velocity/note/condition/timing/roll/rhythm/fill edit is written to every held step of the
visible bar (`menu_broadcastStepSpecial()` → `patSvc_setStepVolume/Note/
Probability()`; `buttonHandler_seqHeldMask()` + `buttonHandler_visibleStep()`).
The display seeds from the last-pressed step; one held step is the old path;
the automation pages stay single-step. Triggerless steps receive specials;
a missing block is allocated; pool exhaustion drops writes silently.

**Held-step track automation overlay.** Hold SEQ steps, press TRACK
(`handleVoiceButton()` STEP arm → `menu_enterStepTrackAutomationOverlay()`):
the track-settings page becomes a per-held-step automation editor for the
three automatable cells (`len`/`scl`/`shf` → Scene targets 405/412/419 +
track via `menu_seqCellToTrackTarget()`, which re-validates through
`sceneModTarget_descriptor()`). Pot/encoder turns write the 7-bit value to
every held step (`patSvc_writeStepAutomation()`; length 128 clamps to 127);
non-automatable cells (`run`, `mch`, `not`) edit their retained value. With
SHIFT+COPY held (clear mode, no menu shown) a pot turn removes that target
from the held steps only (`copyClear_isClearMode()`). The overlay shares the
VOICE overlay's held/marker/search state (`menu.c` va_* block, 46 B) and
exits when all steps are released, the page/mode changes, or the track
changes.

**Underline.** On SEQ_PAGE subpage 0 the VOICE one-marker-per-cell rule
applies to `len`/`scl`/`shf`: a held step carrying the target shows and
underlines its value; otherwise the name is underlined when the active-track
Pattern search found the target (`VA_SEARCH_SCENE_TRACK_BIT()` in
`va_searchSceneMask`, set by `va_seqTrackSearchBit()` in `va_scanService()`).
The search runs on every SEQ subpage and restarts on SEQ entry, track change,
Scene change, copy/clear completion, editor target changes/deletes and
held-step clears. Without held steps, SHIFT+COPY + pot over a track cell is an
ordinary whole-Scene pot clear (`menu_knobClearTarget()` SEQ arm,
`COPYCLEAR_UTILITIES.md` §8). Reader count: this search is a third reader of
step automation (4 step reads per pass, active track only).

### 6.4 Per-track timing: length, scale, shuffle, run mode (S068, S078)

All four per-track settings (§1) are stored, Menu-editable, dirty-marked,
PAT4-persisted **and played** since Session 078. Implementation:
`Core/Sequencer/sequencer.c` (ISR side), `Core/Sequencer/StepScale.c/.h`
(curve), `Core/Bank/Scene/Preset/presetMorphEngine.c` (effective values).
They compose independently: length selects which step indices exist, scale
how fast they are visited, shuffle offsets odd steps in time, and the run
mode where the index goes next.

**Effective cache — the sequencer never reads timing from the region
directly.** `seq_effectiveTrackLength/Scale/Shuffle[NUM_TRACKS]` (21 B,
file-static in sequencer.c) hold Normal (region) interpolated against the
Scene's Morph endpoint at the retained Morph amount of the track's voice
(tracks 1–5 → voices 1–5, tracks 6–7 → voice 6), or a step-automation
overlay value when one is active. `seq_refreshTrackEffectiveParams()`
recomputes them per track from each track's **played** Scene
(`seq_perTrackPattern[track]`, S077), calling
`presetMorph_getTrackEffectiveLength/Scale/Shuffle()`. Refresh points:
`presetMorph_tick()` every foreground worker tick (before its idle
early-out, so endpoint edits propagate within one main-loop pass),
`seq_init()`, `seq_selectActivePattern()`, `seq_alignActivePatternToScene()`,
`seq_setTrackPlayedScene()`, `seq_clearPerTrackOverrides()`,
`seq_setStepIndexToStart()`. Morph and automation never write the region and
never mark the Pattern dirty.

**Length (S068).** The effective length is each track's wrap boundary
(fallback `NUM_STEPS_PER_BAR` when 0). A Morph sweep that shortens the loop
parks an out-of-range index at L−1 before the mode advance, so the change takes
effect at once. `seq_handleMasterBoundary()` intentionally stays on the fixed
16-step master grid (pattern-change commit, beat LED, clock out): it is a
bar-level concept independent of track length. The playback bar shown on the
SELECT LEDs follows the viewed track (`seq_ledState.chaseStep` =
`seq_stepIndex[menu_getActiveVoice()]`); `menu_currentBar` is only the
user-selected viewed bar.

**Scale — 128-position continuous log curve (S078).** `track_scale` is a CC
0..127; `multiplier(cc) = 0.25 × 2^(cc / (127/5))` (0.25× = 1/64 … 8× = 1/2,
relative to the 1/16 step of 24 PPQ ticks), with 14 positions nudged onto
exact musical values: 0 `/64`, 16 `32t`, 38 `/32`, 54 `16t`, 60 `d32`,
**76 `/16` (default)**, 83 `8Tr`, 86 `d16`, 93 `/8`, 100 `4Tr`, 103 `d/8`,
110 `/4`, 120 `d/4`, 127 `/2`. The display shows the symbolic name at a stop
and the raw CC elsewhere (`stepScale_formatShort()`).
`stepScale_ticksQ8(cc)` returns the Q8.8 tick interval
`round(multiplier × 24 × 256)` from a 256 B flash LUT (1,536..49,152).

Per-track DDA (`seq_processSchedulerTick()`): each PPQ tick every track adds
256 to `seq_trackAccumulator[track]` (`uint16_t[7]`) and, while the
accumulator ≥ interval, subtracts it and calls `seq_advanceTrackStep()`.
The remainder carries, so fractional rates are exact on average with no
drift; the loop runs at most once per tick (minimum interval 1,536 > 256).
`seq_setStepIndexToStart()` seeds every accumulator to `interval − 256` so
step 0 fires on the first tick (a zero seed would lose it). A scale change
mid-playback keeps the remainder (no reset). The master 1/16 counter is
still the 24-tick modulo and is used only for boundaries, beat, clock and
MIDI.

Realignment (`seq_realignActivePatternToMasterClock()`,
`seq_realignTrackToMasterClock()`): phase = `(seq_elapsedPpqTicks << 8) %
interval`; the step comes from the same Q8.8 timeline mapped through
`seq_stepIndexForMode()` (so rev/pip/onc/1fr land where their mode would be,
and pip's direction bit is set from the cycle phase). Stopped once-mode
tracks are skipped.

The FX sequencer uses the same curve and a DDA of its own
(`EFFECTS_BUS_REFERENCE.md`). Old PAT4 files (bytes 0..13 from the
14-index era) are not migrated on device and play near the fast end of the
curve; `tools/convert_scene_scale.py` remaps them offline (old index →
CC 0,16,38,54,76,83,86,93,100,103,110,127,127,127) and recomputes the CRC32C.

**Shuffle (S078).** Odd-indexed steps (0-indexed 1, 3, 5, …) with nonzero
effective shuffle are deferred by `(shuffle × 24) / 256` PPQ ticks (0..11):
always on the 1/16 grid, never scaled by the track's scale (a slow track gets
a proportionally tiny swing; musically intended). `seq_advanceTrackStep()`
stores velocity/note in `seq_trackShuffleVel/Note[7]` and sets
`seq_trackShuffleDelay[7] = delay − 1` and `seq_trackShufflePending[7]`
instead of triggering; `seq_processShuffleDelays()` runs each tick **before**
the DDA advance, decrements and fires at 0 (hence `− 1`). A pending deferral
is flushed before a new one is set, and fires even if the track then stops.
28 B ISR-static. Since S081 roll steps take the same deferral, the step's
automation is queued when the deferral fires (§6.1), and the deferral also
stores the step index and a roll flag (`seq_trackShuffleStep/Roll[7]`, 14 B).
Live record never uses shuffle for placement; it applies only at playback
(§6.7).

**Run mode (S078).** `track_play_mode[track]` (Menu cell `run`, category
`Track`, long name `RunMode`):

| Value | Token | Index movement on each step boundary |
|---:|---|---|
| 0 | `fwd` | +1, wrap L−1 → 0 (default) |
| 1 | `rev` | −1, wrap 0 → L−1 |
| 2 | `pip` | ping-pong, ends play twice (cycle 2L; same as the FX sequencer) |
| 3 | `rnd` | `(GetRngValue() & 0x7FFF) % L` |
| 4 | `onc` | once, entered aligned to the master clock; stops after L−1 |
| 5 | `1fr` | once, always from step 0; stops after L−1 |

`seq_trackPlayState[7]`: bit 0 `SEQ_PLAY_STATE_STOPPED`, bit 1
`SEQ_PLAY_STATE_PIP_REV` (pip needs no cycle counter: direction bit plus
index). A stopped once-track advances no further. **Retrigger** (STOPPED
cleared): global Scene change (`seq_selectActivePattern()`,
`seq_alignActivePatternToScene()`), per-track Scene reassignment of that
track (`seq_setTrackPlayedScene()`; voice 6 reassigns tracks 6 and 7
together), transport start/stop and Pattern boundaries
(`seq_setStepIndexToStart()`). **Not** retriggered by the double-click
realign or a reassignment of another track. The DDA decides **when** a step
fires; the run mode decides **where** the index goes.

**Step automation of track timing.** Scene targets 405..425 (§4) apply
through `presetMorph_setTrackParamStepOverride()` (mask + value, 24 B in the
Morph engine) and write the effective cache directly;
`seq_restoreAllSceneAutomation()` clears them
(`presetMorph_clearAllTrackParamStepOverrides()`) and
`seq_setStepIndexToStart()` recomputes the cache. The overlay holds until
transport stop, a Pattern restore, or (S081 R12) a menu edit of that track's
length, scale or shuffle (`presetMorph_clearTrackParamStepOverride()`, then
the next automation applies again); clearing the automation does not undo
the last applied value. The run mode is not automatable or morphable.

**Adding a per-track timing parameter** (how-to): add the region byte
(update the size assert and PAT4 track header byte), init/setter/menu apply
in PatternData, PAT4 writer + three readers in filesystem.c, copy/clear track
copy/clear in copyClearService.c, a `PAR_*` id + SEQ_PAGE cell (menuPages.h)
+ dispatch case in `menu_parseGlobalParam()`, and — if morphable — a Scene
endpoint, AutoSave cell, `sceneset.scg` key, effective cache entry, getter
in presetMorphEngine.c, and a Scene target kind. Read timing only through the
effective cache in ISR code.

### 6.5 PERF looper engine (S080)

The PERF SELECT buttons loop tracks over a musical division while held
(gesture rules: `MODULE_INTERCHANGE_SPEC.md` "PERF performance surface").
The engine lives in `Core/Sequencer/sequencer.c`; it reuses the DDA,
effective cache and realign mapping of §6.4 and never touches Pattern data.

**State** (67 B SRAM1, runtime-only, file-static except the gate):

| Symbol | Meaning |
|---|---|
| `seq_loopActive` (extern) | ISR gate: 1 while the looper is engaged (sustained by any held SELECT, VOICE or BAR since S081 P3; S080: a looped VOICE); may be 1 with an empty mask. Read by `lop`/`!lp` and the chain conditions `lox`/`lob`/`!lx`/`!lb` |
| `seq_loopTrackMask` | tracks currently looping |
| `seq_loopDivision` / `seq_loopModifier` | SELECT index 0..7 (1 bar..1/128); 0 none, 1 dot ×3/2, 2 triplet ×2/3 |
| `seq_loopAnchor[7]` | per track, the master tick (`seq_elapsedPpqTicks` units) where its loop window starts |
| `seq_loopStartStep/StartAccum/StartPlayState[7]` | per track, the snapshot: cursor, Q8.8 accumulator and play-state bits **just before** the anchor tick's DDA advance |

Loop length `seq_loopLengthTicks()`: 384, 192, 96, 48, 24, 12, 6, 3 ticks for
divisions 0..7 (96 PPQ, 1 bar = 16 × 24), ×3/2 dot, ×2/3 triplet, minimum 1.

**Wrap** (`seq_loopProcessTick()`, TIM3, every tick, **before** the DDA
advance and after deferred triggers): for each looped track with
`elapsed > anchor` and `(elapsed − anchor) % length == 0`, restore the
snapshot. The DDA advance in the same tick then replays the anchor tick's
step, so the step that sounded at the loop start repeats on every wrap.
Wraps stay on each track's own anchor grid; changing the division or
modifier never moves an anchor (a shrinking length takes effect at the next
multiple). The wrap and the DDA are both skipped while a chain `stp` freeze
is active (§6.6).

**Position maths** (`seq_loopShiftPosition(track, pos, ticks)`, foreground
only, 64-bit): moves a position image (cursor, accumulator, play state) by a
signed tick count: accumulator ± ticks·256, floor-divided by the effective
Q8.8 interval into step advances, then the cursor walked per run mode:
fwd/rev modulo the effective length (from the pre-start cursor −1 the first
forward step is 0 and the first reverse step is L−1), pip along its 2L cycle
setting `SEQ_PLAY_STATE_PIP_REV`, rnd a fresh random step, onc/1fr clamped
with `SEQ_PLAY_STATE_STOPPED` set past the end and cleared when rewound
inside. It uses the current effective scale and length for the whole span,
so a Morph/automation change during the span is approximated.

**Continue and Song Position Pointer (S083 T1).** A Stop stores the first
unplayed master tick in `seq_resumeTick` (`seq_elapsedPpqTicks + 1`, or 0 when
no tick was played; S083 assessment F-1 fixed a replayed tick); an SPP while stopped stores
`pos x 24` there; Continue (`seq_continueRunning()`) starts the ordinary
transport sequence and then sets `seq_elapsedPpqTicks = resume - 1` and
calls `seq_realignActivePatternToMasterClock()` (the §6.5 anchor mapping),
so the next scheduler tick plays the resume position. Start clears it.

**Loop start** (`seq_loopEventAnchor(mask)`): one anchor per gesture. With
recording quantisation on (`seq_quantisation` 8/16/32/64 → 48/24/12/6 ticks)
it is the last quantise boundary `now − now % q` (may be more than one loop
length back; the track then jumps back at the next wrap). With it off it is
the tick at which the fastest track of the gesture last advanced,
`now − (accumulator >> 8)` (after an advance the residual is < 256, so the
whole ticks in the accumulator are the ticks since that step). Before the
first scheduler tick the anchor is 0.

**`qnt trk` (S081 P1 R16, decision L1).** With `seq_quantisation` =
`QUANT_TRACK` (5), `seq_loopEventAnchor()` is not used: inside the per-track
loop of `seq_loopSnapshotTracks()` each looped track gets its **own** anchor,
its own last step start (`now − (accumulator >> 8)`), and its own `back`
offset. Every track then catches its current step at the loop start (with
`off`, a slower track can be anchored mid-step and its current hit is not
caught). Anchors only choose which window each track replays: all tracks
share the period L, so steps that sounded together still sound together.
Existing edge, unchanged: a track whose step is longer than the loop is
anchored more than one loop length back and replays that step's start every
loop.

**Snapshot** (`seq_loopSnapshotTracks(mask)`): each track's live state
(post-advance at `now`) rewound by `now − anchor + 1` ticks = pre-advance at
the anchor (0 ticks before the first scheduler tick, whose seeded state is
already pre-advance at tick 0).

**Resume** (`seq_loopResumeTrack(track)`): the snapshot projected forward by
`now − anchor + 1` ticks and written to `seq_stepIndex`,
`seq_trackAccumulator`, `seq_trackPlayState`, `seq_lastMasterStep` (chase
LED for the selected voice). This is exactly where the track would have been
without the loop, whether or not it was on its canonical master-clock
position — the reason the looper does not resume through
`seq_realignTrackToMasterClock()`.

**API** (foreground; every publish holds PRIMASK for its whole body, and
`seq_loopActive` is written last on engage / first on disengage):

| Function | Behaviour |
|---|---|
| `seq_loopEngage(mask, div, mod)` | returns 0 and does nothing while stopped (recording is not touched: S081 P3, L-2); filters `mask` by `seq_loopableTrackMask()`; snapshots at one anchor; returns the mask now looping |
| `seq_loopUpdateMask(newMask)` | engaged only: tracks leaving resume (inside the call, before the mask is committed), tracks joining get one fresh anchor, tracks staying keep theirs; returns the committed mask |
| `seq_loopDisengage()` | clears gate and mask, resumes every looped track (if running) |
| `seq_loopUpdateDivision(div, mod)` | single-byte stores; anchors untouched |
| `seq_loopableTrackMask()` | bit N set unless track N's played Scene region has the exclude bit |

**Scene changes** (`seq_selectActivePattern()`,
`seq_alignActivePatternToScene()`, `seq_chainRepeatSelf()`): after
`seq_refreshTrackEffectiveParams()`, one PRIMASK section runs
`seq_realignActivePatternToMasterClock()` and
`seq_loopRefreshSceneSnapshots()`: anchors are kept and each snapshot becomes
the incoming Scene's canonical pre-advance position at `anchor − 1`
(`seq_loopCanonicalPreAdvance()`: the realign mapping of §6.4; anchor 0 →
seed cursor −1, accumulator `interval − 256`). The loop start therefore
stays at the same master-clock position with the incoming Scene's own scale
(1/16 loop start at step 5 = tick 96 → a 1/8 incoming track starts at step 3;
tick 120 → mid step 3). Between the change and the next wrap the track plays
the incoming Scene from its realigned position. Resume after a scene change
projects the refreshed snapshot, which equals the incoming Scene's
master-clock position.

**Cursor reset** (`seq_setStepIndexToStart()`): ends with
`seq_loopRebaseToStart()`: anchors 0, snapshots = the freshly seeded state.
This covers a resetting scene change (`seq_resetToPatternStart()`: pcr for
direct/chain changes, the qac reset bit for queued ones), external resets and
boundary pattern commits — the loop start becomes the beginning of the Scene
and the loop stays engaged. `seq_setRunning(0)` clears `seq_loopActive` and
the mask before the reset, so a transport stop ends the loop.

**Invariants:** never reintroduce a shared loop tick counter or an
un-anchored snapshot taken at the press (both drift off the grid); never
publish looper state outside PRIMASK; keep the wrap before the DDA advance;
FX sequencer and Effects are never looped.

**Looping while recording (S081 P3).** Live record (LATCH or OVERDUB) and live
erase run while the loop is engaged; the looper and the record modes no longer
exclude each other (plan L-2). The looper never touches Pattern data, so the
record engine simply follows the looped playhead:

- **Jump entries.** A wrap that lands mid-step (with `qnt off` or a 1/8..1/64
  grid, when the track's step does not start at the anchor) pushes a ring
  entry with the jump flag (`SEQ_REC_RING_JUMP_BIT`), because no step advance
  follows on that tick; `seq_loopResumeTrack()` does the same when a resume
  moves the track to another step, and drops that track's echo mark. The
  step a jump entry begins is a partial replay: its trigger did not fire on
  this pass, so its step end runs dirty writes and erase but no LATCH
  re-stamp or clear.
- **Placement.** `seq_placeFromSnapshot()` maps a captured hit that crosses a
  wrap onto the pass the target falls in (`seq_placeLoopImage()`): a forward
  snap across the wrap lands on the replayed step after it, a backward one at
  the end of the previous pass, or in the unlooped history before the loop's
  first pass. `rel` is decided by time when the shift crossed a wrap.
- **Echo.** No echo mark is cleared at the wrap; the P1B deadline expires it,
  measured through the wrap, so exactly the one replay after the capture is
  silenced. A resume clears the track's mark directly.
- **Ring producer.** Every ring entry comes from one function,
  `seq_recRingPush()`: TIM3 for a step advance and a mid-step wrap, the
  foreground under the looper's PRIMASK for a resume.

### 6.6 Scene change timing, queued changes and scene chaining (S080)

**Divisions.** A direct (SEQ) change uses `scd` (`PAR_SCENE_CHANGE_DIV`,
0..9 → master 1/16 units 0, 2, 4, 8, 16, 32, 48, 64, and 16 for `ens`/`enl`,
`seq_sceneChangeDivUnits()`); a queued (SHIFT+SEQ) change uses `qcd`
(`PAR_SCENE_QUEUE_DIV`, 0..17 → 0, 2, 4, 8, 16, 32, 48, 64, 80, 96, 112, 128,
144, 192, 240, 256, 16, 16, `seq_queueChangeDivUnits()`). Units 0 = instant.
`ens`/`enl` (end of shortest/longest track) are approximated by one bar.

**State** (file-static): `seq_pendingSceneChange/Index/Div` (direct),
`seq_pendingSceneQueue/QueueScene/QueueDiv` (queued), `seq_pendingChainFire`
(the direct pending change was raised by a chain), `seq_sceneFireFlag`
(TIM3 → foreground). API: `seq_requestSceneChange()` (clears a stale chain
marker), `seq_queueSceneChange()` (instant or stopped transport raises the
fire flag at once), `seq_sceneChangeFirePending()`, `seq_pendingSceneTarget()`
(queue wins), `seq_pendingSceneIsQueue()`, `seq_pendingSceneIsChain()`,
`seq_queuedSceneTarget()` (0xFF none; the fast-blink LED owner reads it),
`seq_clearPendingSceneChange()` (all; used when a fire is consumed and on
transport stop), `seq_cancelPendingDirectChange()` (direct + chain only; a
queued change survives a manual change, and an instant queue re-arms).

**Boundary detection** (`seq_handleMasterBoundary()`, TIM3, every master
1/16): with a change pending, `units != 0 && clock % units == 0` raises the
fire flag (queue units if a queue is pending, else direct). The foreground
executor and queue actions are in `MODULE_INTERCHANGE_SPEC.md` (menu).

**PC reset.** A resetting change runs `seq_resetToPatternStart()` after
`seq_selectActivePattern()`: master clock, bar counter and every cursor
restart (and the looper rebases, §6.5). Direct and chain changes reset when
pcr (`PAR_BAR_RESET_MODE`) is on; queued changes ignore pcr and reset only
for qac `res`, `u+r`, `o+r`, `mmr`, `r+r`. Non-resetting changes realign to
the running master clock (`seq_selectActivePattern()` never resets).

**Scene chaining.** Per Scene, `chain_condition[2]` (0..78) and
`chain_next[2]` (0..43) in `scene_settings_t`
(`BANK_PRESET_ARCHITECTURE.md`). Engine state (5 B): `seq_chainBarCount`,
`seq_chainRepCount` (saturating 255, reset when a Scene is selected or
repeated, on Bank Load alignment and on transport start/stop),
`seq_previousScene` (set only when the active Scene really changes; 0xFF on
stop/Bank Load), `seq_chainStopped`, `seq_pendingChainFire`.

`seq_chainServiceTick()` runs in `seq_processSchedulerTick()` after the
master boundary and before deferred triggers, on every tick after tick 0:

1. Bar end = `elapsed % 384 == 0`. Repetition end =
   `seq_chainAtRepetitionEnd()`: the longest track cycle
   `max(effective length × Q8.8 interval)`; true when
   `((elapsed << 8) % cycle) < 256` (the first tick at or after each cycle
   multiple, exact for fractional scales).
2. Counters advance on their boundary.
3. Evaluation is skipped while a direct, queued or chain change is pending,
   and (S082 P3 D50/D52) while a background Bank load runs
   (`preset_backgroundBankLoadRunning()`, a one-byte ISR-safe read): the
   counters keep advancing, but no condition is evaluated, because a chain
   target (`sNN`, `nxt`, `prv`, random ranges) could resolve differently
   depending on which Bank children have loaded. Evaluation resumes at the
   next bar/repetition boundary after the load with the counts as they stand,
   so a condition already met can fire at the first boundary. Manual
   (direct/queued) changes are not suspended; they are limited to available
   Scenes (`bank_sceneAvailable()`). Scene 17 itself has no chain: its chain
   fields are cleared at the copy and `seq_chainResolveNext()` refuses cur ≥ 16.
4. `seq_chainEvaluate()`: weight per condition (`seq_chainConditionWeight()`:
   0 nul; 1 xal / 2 bal = 100 at their boundary; 3..13 / 14..24 = 100 when the
   repetition / bar count equals 1..8, 16, 32, 64; 25..33 / 34..42 = 10..90 at
   repetition / bar ends; 43..46 / 47..54 = 100 at a bar end when the master
   bar just completed is X of 4 / 8 (`((elapsed/384) − 1) % n`); 55..70 = 100
   at a repetition end when `seq_previousScene == NN−1`; 71..78 fill / loop
   active / inactive at repetition / bar ends). Condition 1 priority:
   w1 = 100 → 1; two probabilities summing over 100 → proportional draw (one
   always fires); otherwise one draw r ∈ 0..99: r < w1 → 1, r < w1 + w2
   (capped at 100) → 2, else no change.
5. `stp` (43) sets `seq_chainStopped`: the DDA and loop wraps stop; the
   transport, deferred triggers and rolls continue; counts continue.
   Otherwise `seq_chainResolveNext()` (0 slf, 1 nxt, 2 prv, 3..18 sNN, 19..34
   random 1..NN, 35..38 random next N excluding current, 39..42 previous N;
   absent target → no change) and the change is published as a direct
   pending change with `seq_pendingChainFire = 1`, `div` 0, fire flag last —
   it executes at this boundary, not re-quantised by `scd`.

The foreground executor treats a chain fire whose target is the playing Scene
as a repeat: `seq_chainRepeatSelf(pcr)` resets the chain counters and the
`stp` freeze, retriggers once modes and the per-track condition counters,
realigns (keeping loop anchors), or restarts when pcr is on; there is no
DSP re-apply. Any executed change clears `stp` through
`seq_selectActivePattern()`.

**Adding a chain condition or next value:** extend the SceneData clamp
constants (`SCENE_CHAIN_CONDITION_MAX` / `SCENE_CHAIN_NEXT_MAX`), the name
table in MenuText.h (index = stored value, numeric order), the weight or
resolve switch in sequencer.c, the sceneset parser maximum and the menu clamp.
Conditions must be ISR-safe single-byte reads.

### 6.7 Live record and live erase (S081)

Live record writes what the user plays and turns while the transport runs
onto one **recording track** (`rtk`) of one **target Scene**. Live erase
removes stored content as the playheads pass. Both live in
`Core/Sequencer/LiveRecord.c/.h` (prefix `liveRec_`); the Sequencer provides
timing services, the Pattern Stack Service performs the writes. Decision
history: `081_SESSION_HANDOFF_LOG.md` §2–§8.

**Dependency direction.** buttonHandler, Menu and MidiNotes/MidiControl call
LiveRecord; LiveRecord calls the Sequencer, PatternStackService,
EffectsManager and SceneData getters. The Sequencer never calls LiveRecord:
the drain reads bitmaps LiveRecord registers (`seq_setAutomationFilter()`).
LiveRecord never calls buttonHandler or the MIDI modules. The one deviation is
`menu_automationTargetRecorded()` (the automation underline), for which
LiveRecord includes `menu.h`. Gesture timing (REC hold, SHIFT/CLEAR chord)
stays in buttonHandler; LiveRecord receives decided events.
`liveRec_service()` runs in the 500 Hz foreground pass immediately before
`patSvc_tick()` (`timebase.c`), so an idle-direct write executes in the same
pass; `liveRec_init(menu_getActiveVoice())` runs at boot after
`copyClear_init()`.

**Arrival time (S083 Q6).** `liveRec_trigger_t` carries `age_us` (stack
only); a MIDI hit sets it from the message stamp, panel and repeat hits
pass 0. `liveRec_triggerIn()` moves the captured position back by
`age_us x 256 / snap.periodUs` Q8 ticks (at most one step interval) before
quantising, so a note that waited for the foreground records on the step it
actually arrived on. Ages above `MIDI_ARRIVAL_MAX_AGE_US` (20 ms) are
treated as processing time.

#### 6.7.1 Modes and gestures

| Mode | Gesture | Meaning |
|---|---|---|
| OFF | — | idle |
| LATCH | REC tap (toggles **on release**, before `DOUBLE_CLICK_TIMEOUT` = 300 ms) | the first adjustment of a parameter latches it for the take; latched parameters own their automation on `rtk` (§6.7.5) |
| OVERDUB | REC held past 300 ms (`buttonHandler_tick()`); release restores the prior LATCH/OFF state | only adjusted parameters are written; nothing latches or clears |
| ERASE | SHIFT+CLEAR (either order) while LATCH or OVERDUB; ends when **both** are released | live erase (§6.7.7); ends overdub; REC does nothing while erase is on |

- Arming while stopped is allowed; recording (or erasing) starts with
  playback. `liveRec_armed()` = mode ≠ OFF; `liveRec_patternWriting()` =
  armed and running.
- Every overdub entry and exit, REC off, a track or `rtk` change and any
  Scene change start a **new take** (latches and dirty bits cleared).
- REC LED: on in LATCH; **blinks** (`led_pulseLed()`) on overdub entry and
  exit; **flashes** while the SHIFT+REC menu is open and armed. (User words:
  *blink* = one transient pulse, *flash* = continuous; the code names are the
  reverse.)
- Fill (PERF hold) may be held together with REC: written steps get `fil`.
- The looper does not lock record out (S081 P3): REC, OVERDUB and erase work
  while looping (§6.5).

#### 6.7.2 Recording track, target Scene and ownership

- **`rtk`** (`PAR_AUTOM_TRACK`, runtime only, RECORDING_PAGE sub-page 2 cell
  0, `rtk`/`RecTrk`, shown 1..7) follows every active-track change
  (`menu_setActiveVoice()` → `liveRec_onActiveTrackChanged()`, only when the
  track changes). SHIFT+REC can override it until the next track selection.
- **Target T** = P(rtk), the played Scene of `rtk` (`seq_getTrackPlayedScene()`);
  A = the active Scene. While writing and T ≠ A, LiveRecord holds a Pattern
  Stack Service target override (`patSvc_requestTarget(T)`, §12.12); while
  the target is not open (handover), every write is dropped and counted.
- **Ownership** (`liveRec_targetOwned()`): a trigger on track t records only
  if P(t) == T and the slot type in T matches A; an Instrument parameter of
  slot s only if the track playing s plays T (slot 6: `rtk` when `rtk` is
  track 6 or 7) and the types match; an Effect or Scene parameter only if
  T == A; erase on track t only if P(t) == T.
- **Values come from A**, the Scene the pages display and edit, never from T
  (under per-track playback the edit writes A's image and the active-Scene
  runtime write puts A's value on the sounding slot).
- **Q13 refusal:** while writing into T ≠ A
  (`liveRec_activePatternLocked()`), edits that write the viewed Pattern
  (step specials, track timing/run mode/loopable, the step-automation
  editor, held-step overlays, STEP toggles) are refused silently before the
  display changes.
- **Two epochs.** `seq_getSceneEpoch()` changes only when a track's Pattern
  changes (direct/queued/chain change, per-track reassignment, load
  alignment): a new take. `seq_getPlaybackEpoch()` changes at every playhead
  discontinuity (those plus transport start/stop, PC reset, realign): only
  per-step capture state ends (previous ring step, open roll, pending flam
  grace); latches and held erase gestures survive, so arming or erasing
  while stopped carries into playback.
- A filesystem Pattern replacement (`patSvc_replacePending()`) releases the
  target and suspends writing.

#### 6.7.3 Step boundaries: the ring

TIM3 tells LiveRecord where each recorded track's steps begin through a
16-entry `u16` ring, `seq_recRing[16]`:

```
bits 0..6  step     bits 7..9  track
bit 10     fired    (the step holds a trigger and its condition passed,
                     known at its start; a muted rtk evaluates the condition
                     without side effects and also needs bit 15)
bit 11     jump     (S081 P3: pushed by a looper wrap mid-step or a resume,
                     not by a step advance; the step is a partial replay)
```

- **One producer function**, `seq_recRingPush()`: TIM3 for a step advance
  and a mid-step wrap; the foreground only inside the looper's PRIMASK
  sections (resume). It pushes only while writing and only for `rtk` or a
  live-held track. A full ring drops the entry (DEV: `seq_recRingDrops`,
  trace `W` cause 5).
- **One consumer**, `liveRec_drainRing()` via `seq_takeRecBoundary()`. An
  entry marks the **start** of a step; the track's next entry ends the
  previous step, and the step-end work runs with the flags stored when that
  step began (`liveRec_prevFlags[7]`: VALID, FIRED, JUMP). Before S081 P3
  the drain used the new entry's fired bit, so LATCH treated step N by
  whether N+1 fired.
- The 500 Hz drain can lag the playhead by up to 2 ms. Where a decision
  needs a current view (a roll start, a roll end, the first-change write),
  LiveRecord first calls `liveRec_syncRing()` (position-epoch check + drain).

#### 6.7.4 Trigger recording

Sources: the BAR VOICE-trigger overlay in PERF (`buttonHandler_overlayReport()`
→ `liveRec_triggerIn()`; the grace and single hits from
`buttonHandler_overlayTrigger()`, the flam main and roll hits from
`buttonHandler_overlayTick()`, releases and rate switches through
`liveRec_rollRelease()` / `liveRec_rollRateChange()`), and MIDI notes
(`midiNotes_handle()`, kind SINGLE, or through the overlay executor when
the BAR overlay is on; S083). Each hit is reported **when it sounds**; a MIDI
hit carries its arrival age (see "Arrival time" below). Gates: LATCH or OVERDUB, running, ownership.

**Placement** (`seq_captureTrackTime()` + `seq_placeFromSnapshot()`):

1. Capture, in one PRIMASK section, the elapsed tick, the PPQ µs stamp and
   period, the capture's own µs time, and the track's **live** cursor,
   accumulator, effective interval/length/shuffle, play mode/state, played
   Scene and loop state. The sub-tick fraction is `(nowUs − stampUs) /
   periodUs`.
2. The shift from the capture: un-quantised = the sub-tick fraction;
   `qnt` 1/8..1/64 = `seq_quantiseTick()` (nearest 48/24/12/6-tick master
   grid boundary, half test with the fraction) minus the capture; `qnt trk`
   (`QUANT_TRACK`, 5) = snap to the nearest step start of the track's own
   grid (step 4).
3. Shift the live cursor with `seq_shiftPositionQ8()` (signed Q8, current
   effective scale and length, run-mode walk). `rnd` never shifts forward. A
   looped track whose shift crosses a wrap is mapped onto the replayed pass
   instead (`seq_placeLoopImage()`, §6.5).
4. `trk`: round to the nearer step start (`snapQ8` records the shift).
5. `delay = round(off × 100 / interval)` from the step's **nominal** start; a
   delay that rounds to 100 goes to the run mode's next step at 0. Shuffle is
   never used: playback adds the step's shuffle as for any step.
6. `rel` (behind / current / ahead) and `ahead` (ticks to the landed step
   start) for the echo rule.

Placement follows the track that is actually playing; mapping a tick through
the canonical master-clock position (the first P1 version) is wrong as soon
as scale or length changed during playback.

**Commit** (`pat_recordStep()` through `patSvc_recordStep()`): bit 15 on;
note (`def` → no note special); velocity (100 stored as absent); timing =
delay; roll/fill (fill 100 stored as absent) or cleared; rhythm cleared;
condition kept, or `fil` (21) while fill is active; automation kept. A step
landed **ahead** of the playhead in this pass gets an echo mark
(`seq_suppressStepOnce(track, step, ahead)`: TIM3 skips that stored trigger
once — automation still plays — and the mark expires at capture + ahead +
one interval + 1 ticks, so it can never silence a later hit). A write onto
the **current** step mutes that step's pending deferral
(`seq_cancelPendingTrigger(track, step)`: its automation still queues, no
second hit). Steps behind get nothing. Last write wins in a pass; a grace
never replaces its own main.

**Flams** (grace velocity 64, main 100, 12 ticks apart): the grace is placed
from the main's capture. Quantised: main on the snap, grace 1/32 before the
snapped main; both written if they land on different steps, otherwise the
main only. Un-quantised: each at its own time; if both land in one step,
the main.

**Rolls** (overlay `r/8 r16 r32 r64 r6t 128` = step indices 1 3 5 7 8 9):

- A roll whose interval is at least the track's step interval is recorded
  **hit by hit** as single steps.
- A faster roll: the start step is placed like a hit and written with the
  roll; the rolled track is **live-held** (`seq_setLiveHold()`: its stored
  triggers are skipped while held; automation plays); each later ring entry
  writes the previous step as a span (trigger + rate + fill 100 + `fil`),
  never the start step and never a step before a forward-quantised start.
- Release: before the second hit → the start step becomes a plain trigger
  at its start delay; on the start step → fill = release − start; on a later
  step → that step at timing 0, fill = the release offset. Then the live
  hold ends.
- Rate switches while held: to slow ends the recorded roll (later hits are
  singles); to fast (from slow or single) starts a recorded roll at the next
  hit; fast → fast takes the new rate from the step containing the switch.
  Toggling the overlay off ends the roll at that step.
- A Scene change, stop, REC off or `rtk` change ends an open roll without an
  end write.

#### 6.7.5 Automation recording

**Recordable:** VOICE-page Instrument cells, `Nvm`, `Nou`, `Nfx`, `7dc`;
Effect AUTOMATABLE rows and the Effect page's Morph cell (the FX Morph,
labelled `mrp` there); SEQ `scl`, `shf` (only while the viewed Pattern is A);
PERF Morph cells `1VM`..`6VM` and `fxm`. Resolution uses the pot-clear
resolver `menu_cellClearTarget()`; the hook `menu_liveRecRecordCell()` runs
after the pot and encoder commits, also on a detent clamped at min or max.
**Not recordable:** the master morph `mrp` (`PAR_MORPH`, no target; never
recorded or automatable, and the voice/FX Morph values a global change writes
are never recorded through it), RECORDING/global/MIDI/Load/Save pages, step
editors and held-step overlays, FX lane locks, LFO/velocity targets, `typ`,
Morph-view edits, `len`, non-automatable targets. MIDI writes (NRPN, Learn,
CC1 on a voice channel) record through `menu_commitTargetEdit()` with the real
target ID (`liveRec_paramIn(id, LIVEREC_SRC_MIDI)`, S083 Q1); the panel-view
exclusions do not apply to MIDI, which always writes the active Scene's
Normal endpoint. CC1 on G (global Morph) is never recorded (G-1).

**State:** three 512-bit bitmaps over the 9-bit target ID space, indexed
directly: `latch` (owns its automation this take), `dirty` (adjusted since
its last write; with the first-change rule, "this step has its value"),
`erase` (being erased from `rtk`). Plus `liveRec_morphValue[7]`: the value of
the last individual edit of each voice Morph and the FX Morph, which every
dirty Morph write uses, so a global Morph change cannot leak into a pending
write.

**Own vs foreign:** an own Instrument parameter belongs to the slot `rtk`
plays; everything else is foreign. A **fired step** is a step of `rtk` whose
first trigger fired this pass (incl. every step a multi-step roll spans, an
echo-suppressed recorded step, and, on a muted `rtk`, a step that would have
fired).

**Which value, when (`LIVEREC_AUTOMATION_FIRST_CHANGE`, `config.h`).**

- **1 (default):** the **first change** of a target in a step is written at
  once into the live step of `rtk` (after `liveRec_syncRing()`), as the menu
  value just after the commit; later changes in that step are not written.
  With no step known (stopped, replacement pending, or no ring entry since a
  break) the change waits as a dirty bit and is written into the next step
  that starts. Reason: a step's automation plays at its start, so the start
  of the step's motion belongs there; the step-end rule played every move
  back up to one step early.
- **0:** the S081 P1 rule: the value the parameter holds at the step's end
  is written into that step.

**LATCH step-end table** (end of step N of `rtk`, every latched target):

| End of step N | Own Instrument parameter | Foreign |
|---|---|---|
| dirty | written (switch 0) / bit cleared (switch 1) | same |
| not dirty, N fired | write the menu value (re-stamp) | clear an existing entry |
| not dirty, N not fired, entry exists | clear | clear |
| not dirty, N begun by a looper jump | nothing | nothing |

While a parameter is latched its menu value always sounds (the drain skips
every track's automation of it). **OVERDUB:** dirty targets are written
(first change or step end); nothing latches, clears or re-stamps; playback
automation plays and R12 applies to the adjusted parameter.

**Only real changes are written (L3):** one `pat_readStepAutomations()` read
per ended step answers all targets; a write is queued only when the entry is
missing or different, a clear only when an entry exists. Writes go through
`patSvc_writeStepAutomation()`, clears through
`patSvc_removeStepAutomation()`; a written step gets `fil` while fill is
active. Each accepted write calls `menu_automationTargetRecorded()` so the
automation underline appears at once. The menu value is never reverted when
recording ends; R12 then applies as after any menu edit.

#### 6.7.6 Linked Scenes

Menu edits fan out at commit as outside record. Pattern data (recorded
triggers, automation writes and clears, Pattern erase) never fans out. The
FX-sequence part of live erase fans out (§6.7.7).

#### 6.7.7 Live erase

| Gesture | Duration | As the playhead passes |
|---|---|---|
| Knob over an automatable target (selection; works in the Morph view and on PERF Morph cells) | until erase ends | the target joins `erase`; the drain skips it on `rtk`; each passed step of `rtk` loses its entry. In FX mode (and `fxm` in PERF) it also erases that FX lane's locks |
| SEQ hold, VOICE/STEP mode | while held | the active track's triggers and specials are removed and not played; the condition is kept on steps with automation; automation still plays (TIM3 clears bit 15, `patSvc_enqueueEraseSpecials()`) |
| SEQ hold, FX mode | while held (counted per button) | every lock of each FX step the FX playhead reaches is removed before it applies |
| TRACK hold (VOICE, STEP, PERF when not looping, FX) | while held | that track: trigger, specials, condition and automation removed, not played (TIM3 clears bit 15, `patSvc_enqueueErase()`, skips the step's automation) |

- SEQ in PERF (Scenes), Load/Save, global settings and SOM_GEN: presses are
  consumed and nothing is erased. All erase presses are consumed (no step
  toggle, voice select, mute, overlay trigger, Scene change); each release
  ends exactly the gesture its press started.
- While erase is enabled: PLAY is a plain transport toggle (never the chain
  chord) and SEQ never opens the chain menu. A MODE change keeps erase but
  drops held SEQ/TRACK gestures (`liveRec_eraseReleaseHeld()`); the knob
  targets stay until erase ends.
- Erase masks are pushed through the ownership gate on every pass
  (`seq_setEraseMasks()` for tracks playing T; FX lanes only when T == A).
- **FX-sequence erase:** LiveRecord publishes a 16-bit lane mask
  (`effects_setLiveEraseLanes()`: knob lanes OR all lanes while an FX SEQ
  hold is down). At each FX step start EffectsManager excludes those lanes'
  locks from resolution (they never sound) and records them per step
  (`effects_erasedLanes[16]`); `liveRec_service()` drains one step at a time
  (`effects_takeErasedLocks()`) into `effects_clearSeqLanes(origin, step,
  lanes)`, which clears the retained locks in the origin Scene and every
  linked same-type Scene. The flush also runs when erase or recording ends.

#### 6.7.8 Service pass (`liveRec_service()`)

1. Scene epoch changed → new take.
2. Playback epoch changed → end per-step capture state.
3. Replacement pending → release the target, writing off, filters off.
4. Target and publication: T, the override, `seq_setRecordTarget(rtk,
   writing)`, `seq_setAutomationFilter()` while armed, erase masks.
5. Ring drain: span writes, the step-end tables, pending first-change
   writes.
6. FX-erase drain.

#### 6.7.9 Invariants and risks

- LiveRecord never runs in TIM3, never writes a Scene other than T (Pattern)
  or the origin Scene's fan-out set (FX sequence).
- TIM3 never touches pool bytes for live record or erase; it uses the
  service's TIM3-safe enqueues. Every TIM3-shared live-record field is a byte
  or a PRIMASK-published pair, and `volatile`.
- Recording is best effort; playback always has priority. Drops are
  counted in DEV builds (`liveRec_drops[]`, trace `W`, §12.16). Many latched
  parameters on a fast `rtk` can exceed the busy drain (one event per 2 ms,
  64-entry queue); a composite per-step op is the fallback if measured.
- The side-effect-free condition path (`seq_conditionPassesPure()`) must
  stay in step with `seq_evaluateStepCondition()` when conditions are added.
- Every Scene-change entry point must reach the FX-erase flush, or linked
  Scenes diverge.
- Known open items: the first ring entry after arming REC while running ends
  a stale step from an earlier take (harmless with first-change 1); a
  Pattern Load during recording loses a change made while the replacement
  is pending.

## 7. PAT4 wire format

All current Scene/Bank/root Pattern Save operations write PAT4. Legacy text
v1-v3 files may be accepted for import but are never emitted.

One PAT4 file is exactly 10,656 bytes:

| Region | Offset | Bytes | Contents |
| --- | ---: | ---: | --- |
| Fixed header | 0 | 32 | magic/version/stack/generation/CRC/reserved |
| Pattern parameters | 32 | 16 | change-bar, next, 14 reserved |
| Track parameters | 48 | 112 | 7 × (length, scale CC, shuffle, run mode, loop exclude, `asm`, 10 reserved) |
| Address array | 160 | 1,792 | 7 × 128 little-endian encoded entries |
| Bitmap | 1,952 | 512 | exact bit-packed allocator state |
| Pool | 2,464 | 8,192 | dynamic blocks/padding |

Fixed-header fields:

```text
0..3    "PAT4"
4..5    uint16 LE format version = 2 (bumped from 1 in S079; reader rejects v1)
6..7    uint16 LE stack size = 256 (must equal firmware PAT_STACK_SIZE)
8..9    uint16 LE header size = 160
10..13  uint32 LE generation
14..17  uint32 LE CRC32C
18..31  reserved zero
```

CRC32C uses the Castagnoli polynomial, covers the entire exact-size file, and
treats bytes 14..17 as zero. Readers require current version, exact stack size,
exact EOF and matching CRC before committing. Firmware does not add a semantic
allocator graph audit during load; host validators should check address,
bitmap, back-reference, and pool consistency. Root/library saves use generation
zero.

Track header bytes per track (16 each, base = 48 + 16 × track): 0 length,
1 scale (CC 0..127 since S078; older files carry the 14-index value and are
not migrated on device), 2 shuffle, 3 run mode (S078; pre-S078 files carry
0 = `fwd`), 4 looper exclusion (S080: 1 = track not loopable; written
0/1 from bit N of `track_loop_exclude`, read back nonzero → bit set; older
files carry 0 = loopable), 5 automation ramps `asm` (S081 P2: 1 = `on`,
written 0/1 from bit N of `track_auto_smooth`, read back nonzero → bit set;
older files carry 0 = `off`), 6..15 reserved zero. The writer
(`filesystem_patternBuildHeader()`) and all three readers (Scene load, root
Pattern Load, AutoSave Pattern boot read) handle bytes 4 and 5. The file
size, CRC rules and version (2) are unchanged: byte 5 was reserved and zero
in every card file, the same precedent as byte 4 (S081 decision F1).
`tools/convert_pattern_autosmooth.py` normalises byte 5 to 0 in every PAT4
file of a card tree (validates header and CRC32C, rewrites the CRC at
offset 14, `--dry-run`, skips non-PAT4 `.pat` files such as the legacy text
`SD_CARD/Bank/*/*/pattern.pat`, nonzero exit on any failure).

Scene directories contain exactly one `<Pattern name>.pat`, not a fixed
`pattern.pat`. The root library uses `Pattern/NNN <name>.pat`. Pattern Load
accepts a destination Scene mask, streams into the first selected Scene, then
copies the validated `pat_scene_region_t` to every other selected Scene.

## 8. Pattern identity

HCNAMES rows 129..144 own one Pattern name/source/refreshed witness per Scene.
They have the non-Instrument grammar:

```text
name<TAB>source[<TAB>R]
```

Accepted sources are inherit `-`, unknown `?`, direct library slot `000` to
`999`, and Pattern AutoSave `@`. The RAM value for Pattern `@` is
`FS_RESIDENT_SOURCE_PATTERN_AUTOSAVE == 0x1ffc`; it must not be confused with
Instrument-direct `@ == 0x1ffd`. Pattern rows have no type column.

Scene/Bank directory load commits cache and publish the Pattern identity with
the Scene's other child rows. Root Pattern Load publishes the selected Pattern
name/source to every destination row. A Pattern mutation clears its row's
refresh witness before later AutoSave publication.

## 9. Pattern AutoSave

Each Scene has an independent hidden root pair:

```text
/.patNNa   generation even
/.patNNb   generation odd
```

`NN` is decimal `00`..`15`. Files are complete PAT4 images. The higher valid
generation wins and A wins a tie. Generations never intentionally use zero;
wrap skips zero.

Pattern dirtiness is a separate 16-bit mask. When all ordinary filesystem and
policy gates are open, Pattern is admitted only after higher-priority settings,
trace, and scalar AutoSave work declines. The scheduler chooses the lowest
dirty Scene, refuses admission during `seq_recordActive` or `seq_eraseActive`,
clears the bit before copying to the snapshot, advances generation, and starts
the whole-file writer.

Clear-before-snapshot is binding: a later edit re-sets the canonical bit and
must survive the in-flight completion. An admission/write/close/sync error
re-arms the bit. Successful durable completion publishes the Pattern row as
`name<TAB>@<TAB>R`, but the completion may set `R` only if no post-snapshot
edit made the Pattern dirty again.

**Torn files.** A Pattern write interrupted before its first close leaves
the target at AsyncFATFS's cluster-rounded size: 32,768 B instead of
10,656 B (`ASYNCFATFS_REFERENCE.md`, "Open-file size on the card").
`filesystem_patternAutosaveCandidateValid()` reads one byte past the payload
and rejects the candidate unless that read returns end-of-file. The next
Pattern generation for that Scene recreates the file with `"w"`. The S074
card showed this: `.pat06b` was 32,768 B, was rejected, and was rewritten at
10,656 B. The scalar HCPR validators were brought in line with this probe in
S074 (`AUTOSAVE.md`).

At boot the Pattern reader evaluates present Scenes independently after the
scalar Bank/Scene restore. It applies the winning hidden candidate only when
the HCNAMES Pattern row is Pattern-AutoSave `@` and generation is nonzero.
Missing/invalid candidates leave the current initialized or library-loaded
Pattern intact and do not invalidate scalar Scene data. Explicit root Pattern
Load and Scene/Bank directory Pattern load reset the destination generation
baseline to zero.

## 10. Concurrency and failure invariants

- PatternData owns live regions and the snapshot; filesystem owns handles and
  Pattern generations; Autosave owns the dirty mask.
- Snapshot copy is a plain `memcpy`; it does not mask TIM3 or any interrupt.
- Record/erase activity gates admission because those paths can mutate the
  Pattern asynchronously.
- The snapshot is immutable until its writer completes.
- Only one filesystem facade operation is active; Pattern is not a second
  concurrent SD writer.
- FAT timestamps are irrelevant because the hardware has no RTC.
- Boot selects by exact format, size/EOF, CRC, generation, and provenance—not
  filename timestamp. Host diagnostics additionally validate allocator
  structure.
- A corrupt Pattern candidate never empties an otherwise valid scalar Scene.
- HCPR never contains Pattern identity or payload bytes. Its v2 APIs align
  with the 145-row HCNAMES schema, while HCNAMES/PAT4 own Pattern identity and
  payload durability.

## 11. Accepted validation and remaining work

Session 064 hardware acceptance covered single-Scene restore, ping-pong
parity, full 16-Scene drains, HCNAMES `@|R` publication, HCPR v2 coexistence,
absent rows, and content changes in every Scene. The full card contained 19
valid hidden candidates and no identified Pattern defect. See
`../log_archive/064_SESSION_HANDOFF_LOG.md` for exact evidence.

Session 065 implemented step automation editing (Method 1) and sequencer
playback: pool block automation read/write/remove APIs, step-edit menu with
cursor navigation and detail views, sequencer pending buffer with foreground
drain, per-slot dirty bitmap and trigger-time morph-interpolation restore.
Hardware-tested. See `../log_archive/065_SESSION_HANDOFF_LOG.md`.

Session 066 implemented the VOICE-page held-step automation overlay (Method 2):
overlay activation via configurable short long-press, four-slot bounded CGRAM
underline cache (slots 2..5), held-value resolution with endpoint fallback,
async track-wide automation search agent (4 steps/pass), pot/encoder-to-
automation write with working-value cache, debounced value-underline
reapplication, step illumination of automated steps, and Morph integration.
44 bytes overlay state in `menu.c` (approved). 496 bytes flash font table in
`lcd.c`. Six post-hardware-test fixes applied: delta handling working-value
cache, editMode bit index, non-numeric dtype display, 'S' glyph, PM63 nibble
split, diff-based CGRAM transactions with retry bit. Hardware-tested.
See `../log_archive/066_SESSION_HANDOFF_LOG.md`.

Session 067 implemented the Pattern Stack Service (§12): unified pool mutation
dispatcher with SPSC queue, two-tier defragmentation (trailing-gap merge and
paced compaction), bulk barriers for track/pattern clear, filesystem
replacement handover, pool usage monitor widget, and elastic gap policy.
Publication ordering fix applied to all address-to-pool transactions
(write-new/swap/free-old with PRIMASK trigger-bit re-read). Dtype offset bug
fixed: automation value domain changed from halved MIDI CC-style to identity
mapping across all four code sites (menu write, menu read, sequencer drain,
step automation add). Hardware-validated: PatternTrace zero errors across
6,172 records (5,185 gap + 987 relocation), AutoSaveTrace zero errors across
157,207 records, PAT4 structural integrity confirmed, automation values
confirmed in identity domain post-fix. See
`../log_archive/067_SESSION_HANDOFF_LOG.md` for exact evidence.

Session 068 confirmed the Pattern Stack Service was not implicated in a
reported VOICE-mode step-toggle failure (the `pattrace.bin` trace showed
zero error-class records; ordinary trigger toggling never enters the
service, see §12.14 item 1) — the actual defect was in front-panel event
delivery (`buttonHandler.c`'s event ring, pairing masks, and hold timer),
fixed with no Pattern Stack Service changes. The same session implemented
per-track step-length playback consumption (§6.4) and fixed a
`menu_playedPattern` UI-mirror desync that suppressed the sequencer chase
LED after boot (Menu/Sequencer concern, not a Pattern-storage defect). Session
069 then implemented the settled plan: physical relocation is non-semantic
AutoSave work, and the Tier 1/Tier 2 periodic chase is replaced by owned
trailing-slack repair plus reactive-only compaction. Source/build verification
passed; hardware fixtures remain pending. See
`../../S069_SLACK_REACTIVE_COMPACTION_IMPLEMENTATION.md` and
`../log_archive/068_SESSION_HANDOFF_LOG.md` §4 for the plan and closeout.

Session 069 Pass 1 also made scalar dirty detection constant-time through a
maintained population count, removed the clean idle-tick occupancy recount
while retaining mutation/lifecycle reconciliation, and added semantic Pattern
AutoSave coalescing (250 ms quiet window, 5 s maximum latency, rotating Scene
fairness). Hardware-accepted (2026-09-20).

Session 069 Pass 2 added a shared elapsed-time background CPU budget
(2.5% playback / 5% stopped, signed credit with one-millisecond accumulation
cap), integrated it into the repair section and both AutoSave drain
schedulers, added a Load/Save repair gate (`menu_activePage` check suppresses
repair during Load/Save menu), and added DEV-only per-class `H` trace
reports. Source/build verified; hardware validation pending.

Deferred supplemental cases are deterministic mid-write power interruption,
record/erase admission instrumentation, injected CRC fallback, and performance
measurement. They do not reopen the functional closeout. Live-record capture
is a future feature.

Session 075 implemented Phase 6 copy and clear (`COPYCLEAR_UTILITIES.md`):
the swap block (§3), the exclusive boundary, raw block API and sliding
compaction (§12.17), whole-Pattern copy with the sentinel-first publication
order, early trigger writes and Pattern clears; and the automation-priority
fix (§6.2a). The user tested the base pass, F1 and F2 on hardware and fed back
each round; the remaining case list is `COPYCLEAR_UTILITIES.md` §16. See
`../log_archive/075_SESSION_HANDOFF_LOG.md`. The Session 068
self-generated relocation/dirty-work-at-idle finding is closed in source by
the S069 repair/reactive design and confirmed by Pass 1 hardware acceptance.

## 12. Pattern Stack Service

### 12.1 Architecture

`PatternStackService.c` and `PatternStackService.h`
implement a unified dispatcher that serializes all pool-mutating operations
through a single service tick. This guarantees exactly one mutation target at
a time, preventing concurrent access between foreground callers, the TIM3
ISR's automation reads, and the filesystem replacement boundary.

Admission policy: direct-when-idle (immediate foreground execution with
service lock), queued-when-busy (PRIMASK-protected enqueue into the SPSC ring
for later drain).

### 12.2 Service tick priority order

Evaluated every call to `patSvc_tick()` (called from `timebase.c` after
`endlessPots_tick()`). The repair section (item 4) is suppressed when
`menu_activePage == LOAD_PAGE || menu_activePage == SAVE_PAGE` (Session 069
Pass 2 Load/Save repair gate). All sections are subject to the shared
elapsed-time background CPU budget (`filesystem_backgroundBudgetAvailable()`,
Session 069 Pass 2) — the repair section checks budget before proceeding:

1. **Handover** — check and complete filesystem replacement boundary
   transitions.
2. **Reactive recovery** — when the queue head is blocked by fragmentation,
   inspect a bounded set of address entries and relocate one live block toward
   a lower destination; surplus reservations may be reclaimed only when the
   density latch is inactive.
3. **Queue/bulk work** — advance one bounded track/pattern clear barrier or
   dequeue and execute one pending mutation.
4. **Finite bounded repair** — scan up to `PAT_REPAIR_SCAN_IDLE` (or
   `PAT_REPAIR_SCAN_BUSY` under AutoSave pressure) address entries per tick,
   creating or verifying one trailing-chunk reservation per occupied block.
   The cursor sleeps at `PATSVC_ADDRESS_COUNT` between epochs and wakes only
   on mutation, reservation consumption, density restore, handover, or
   filesystem replacement. Occupancy is reconciled at those mutation/lifecycle
   boundaries and cached during clean idle ticks; the service does not rescan
   the unchanged bitmap on every pass.

### 12.3 Queue format

64-entry volatile `uint32_t` SPSC ring (`service_queue[64]`, 256 B SRAM1,
plus the producer and consumer bytes). Each event packs
(`patSvc_packEvent()` / `patSvc_eventOperation()` / `patSvc_eventStepId()` /
`patSvc_eventPayload()`, `PatternStackService.c`):

```text
bits 5..0     operation (6 bits, patSvc_operation_t)
bits 15..6    track/step identity (10 bits): track * NUM_STEPS + step
bits 31..16   operation payload (16 bits): target and value for automation
              writes, the note/volume/condition/... byte for specials, a
              record-staging slot index for RECORD_STEP (S081)
```

The event carries **no Scene**: every queued event applies to the service's
one mutation Scene (`service_scene`, §12.5), which since S081 follows
`patSvc_desiredScene()` (§12.12). This section corrects an older description
(3-bit operation, 4-bit Scene, 3/7/9/7 fields) that never matched the code
(S081 plan §3.3-7).

Operations: `NONE`, `DELETE_DYNAMIC`, `WRITE_AUTOMATION`,
`REMOVE_AUTOMATION`, `SET_NOTE`, `SET_VOLUME`, `SET_CONDITION`, `SET_ROLL`,
`SET_TIMING`, `SET_RHYTHM`, `SET_FILL`, `CLEAR_TRACK_BARRIER`,
`REMOVE_TRACK_TARGET_BARRIER`, `CLEAR_PATTERN`, and (S081) `RECORD_STEP`,
`ERASE_SPECIALS`. The 16-bit payload cannot hold a full live-record spec, so
`RECORD_STEP` carries the index of a slot in the service-owned 4-slot staging
table (`patSvc_recordStage[4]`, each a `pat_record_spec_t` plus scene, track,
step and a used flag); `patSvc_recordStep()` copies the spec in (a full table
rejects), execution frees the slot, and a dropped event releases it.

Enqueue uses PRIMASK to protect head/tail consistency (TIM3 and foreground
producers). Dequeue is foreground-only. Queue full is a silent drop (counted
by the caller where it matters).

### 12.4 Bulk barriers

Track clear and pattern clear are bounded operations, 16 steps per service
tick:

- **Track clear**: 128-step sweep, completes in 8 ticks.
- **Pattern clear**: 7-track × 128-step sweep, completes in 56 ticks.

### 12.5 Filesystem replacement handover

`patSvc_idle()` is called from 5 filesystem replacement boundary points in
`filesystem.c`. When a Scene Load, Bank Load, or Pattern Load completes, the
service completes or abandons any in-flight bulk barrier, drains remaining
queue entries for the old Scene, switches `service_scene` to the new target,
and clears internal repair/recovery cursors.
The handover-complete boundary also clears the non-persisted reservation image
and lazily rebuilds it through the next repair epoch.

### 12.6 Owned trailing-slack reservation

The reservation image is a 512-byte bit-packed array with the same geometry as
the occupancy bitmap. A set reservation bit means the chunk is reserved as
trailing slack for the immediately-preceding occupied block and is refused to
ordinary allocation. The repair pass creates reservations; the Gate-6 growth
path (`pat_tryAppendAutomation`) consumes them; reactive recovery reclaims
surplus ones under allocation pressure. The image is not persisted or included
in PAT4 payloads.

The density latch disables new reservations at or above
`PAT_RESERVATION_REDUCE_THRESHOLD` (70% occupancy) and re-enables them below
`PAT_RESERVATION_RESTORE_THRESHOLD` (50%). Hysteresis prevents oscillation.
An adaptive per-tick budget (`PAT_REPAIR_SCAN_IDLE` versus
`PAT_REPAIR_SCAN_BUSY`) yields foreground cycles to AutoSave I/O when dirty
work is pending.

The reservation image is cleared and lazily rebuilt at init, handover
completion, and filesystem replacement. During the rebuild window, allocation
falls back to the occupancy bitmap alone. Ordinary block frees and service
relocations clear the former positional trailing claim so the image cannot
retain stale reservations after a block changes owner.

Physical relocations are non-semantic AutoSave work. Reactive recovery runs
only after a blocked allocation; there is no periodic Tier-2 sweep.

### 12.8 Elastic gap policy

The former elastic-gap policy is retired. Reservation-density hysteresis now
controls owned trailing slack, and `PAT_COMPACT_SCAN_PER_TICK` (16 address
entries) is retained only as the reactive-recovery scan bound.

### 12.9 Pool usage monitor

`pat_poolUsagePercent()` in PatternData.c reads the Scene bitmap with
`memcpy` (packed-bitmap-safe), counts set bits with `__builtin_popcount`
across 64 words, and returns `(occupied × 100) / 2015` (usable chunks below
the swap block, S075) clamped to 99.
Displayed as `"pts:NN"` on the Settings menu Global subpage, computed once
on page entry and retained in a static byte.

### 12.10 Publication ordering contract

All address-to-pool transactions follow detach/publish-before-free:

1. **Replace**: allocate new, write content, PRIMASK swap address entry
   (re-read trigger bit), free old.
2. **Clear last special**: PRIMASK detach address (clear bit 14, sentinel
   offset), free old.
3. **Erase step**: detach entire address word, free old.

In-place rewrite of same-size blocks is not used. All PRIMASK sections are
under 50 ns: one load, one bit-field insert/clear, one store, no allocation
or loop.

### 12.11 Config constants

```c
#define PAT_COMPACT_SCAN_PER_TICK          16u /* reactive entries/tick */
#define PAT_RESERVATION_REDUCE_THRESHOLD   70u /* % occupancy: disable */
#define PAT_RESERVATION_RESTORE_THRESHOLD  50u /* % occupancy: re-enable */
#define PAT_REPAIR_SCAN_IDLE               16u /* entries/tick */
#define PAT_REPAIR_SCAN_BUSY                4u /* entries/tick under AutoSave */
```

### 12.12 Public API

```c
void     patSvc_init(void);
void     patSvc_tick(void);
void     patSvc_idle(void);
uint8_t  patSvc_writeStepAutomation(scene, track, step, target9, value7);
uint8_t  patSvc_removeStepAutomation(scene, track, step, target9);
void     patSvc_setStepNote(scene, track, step, note);
void     patSvc_setStepVolume(scene, track, step, volume);
void     patSvc_setStepProbability(scene, track, step, prob);
void     patSvc_eraseStep(scene, track, step);
void     patSvc_clearTrack(scene, track);
void     patSvc_clearPattern(scene);
void     patSvc_removeTrackAutomationByTarget(scene, track, target9);
void     patSvc_enqueueErase(scene, track, step);
/* S081 live record (§6.7) */
uint8_t  patSvc_recordStep(scene, track, step, const pat_record_spec_t *spec);
void     patSvc_enqueueEraseSpecials(scene, track, step);   /* TIM3-safe */
uint8_t  patSvc_requestTarget(scene);    /* 0 = refused */
void     patSvc_releaseTarget(void);
uint8_t  patSvc_targetOpen(scene);
uint8_t  patSvc_replacePending(void);
uint8_t  patSvc_isChunkReserved(chunk);
void     patSvc_consumeReservation(chunk);
/* S075 copy/clear holder (§12.17) */
uint8_t  patSvc_beginExclusive(scene);
void     patSvc_endExclusive(scene);
uint8_t  patSvc_exclusiveCompactStep(scene);
uint8_t  patSvc_exclusiveEvacuateSwapStep(scene);
```

**Live-record operations (S081).**

- `patSvc_recordStep()` stages the spec and submits `RECORD_STEP` (direct
  when idle). Execution calls `pat_recordStep(scene, track, step, spec)`
  (PatternData): it reads the existing specials and automation, applies
  note/velocity/timing/roll/fill, clears rhythm, keeps the condition or sets
  `fil`, keeps automation, and publishes with **trigger ON** in the same
  PRIMASK publish (`pat_writeDynamic()`'s `trigger_policy` KEEP/ON/OFF, the
  `PAT_RAW_TRIGGER_*` values; every older caller passes KEEP). Velocity 100
  and fill 100 are stored as absent; with no specials and no automation it
  only sets bit 15 (`pat_setStepActive()`). Returns success for the
  service's failure classification.
- `patSvc_enqueueEraseSpecials()` (modelled on `patSvc_enqueueErase()`; TIM3
  calls it during a live SEQ-hold erase after clearing bit 15) queues
  `ERASE_SPECIALS` → `pat_eraseSpecials()`: with automation present it keeps
  the condition and the automation and drops the other specials; otherwise it
  releases the block (trigger KEEP).
- `patSvc_requiredChunks()` sizes record ≤ the current block plus the new
  specials, erase-specials ≤ the current block.

**Target override (S081).** `service_target_request` (`0xFF` = none) and
`patSvc_desiredScene()` = the request, else `seq_activePattern`. It replaced
the eight direct `seq_activePattern` comparisons (submit, the three submit
variants, both `patSvc_tick()` checks, `patSvc_idle()`,
`patSvc_finishSceneReplace()`, `patSvc_endExclusive()`), so the service can
mutate a track's per-track Scene while it is not the active one.

- `patSvc_requestTarget(scene)` returns 0 when refused: an exclusive claim
  or a filesystem replacement is pending, the Scene is not resident, or the
  queue is busy. A change of target drains the old work first (handover,
  §12.5); actions in that window are dropped.
- `patSvc_targetOpen(scene)` is true when the service is open on that Scene
  and it is the desired Scene. `patSvc_replacePending()` is true while a
  filesystem Pattern replacement is prepared or applied (LiveRecord's Load
  suspend; it must not call Menu).
- `patSvc_idle()` returns false while an override is held, so Pattern
  AutoSave never snapshots the overridden Scene mid-take. One target at a
  time; publish-before-free still holds.

### 12.13 Integration points

| Location | Call |
|----------|------|
| `main.c` | `patSvc_init()` after boot filesystem ladder |
| `timebase.c` | `patSvc_tick()` after `endlessPots_tick()` |
| `filesystem.c` | `patSvc_idle()` at 5 replacement boundary points |
| `timebase.c` | `ccSvc_tick()` (copy/clear service) right after `patSvc_tick()` (S075) |
| `timebase.c` | `liveRec_service()` immediately **before** `patSvc_tick()` (S081), so a live-record write submitted while idle executes in the same pass |

### 12.14 Binding constraints

1. No pool mutation path may bypass the service without explicit justification.
2. Exactly one mutation target Scene at a time.
3. TIM3 reads address entries directly; they must always point at valid or
   sentinel data.
4. PRIMASK sections must stay under 50 ns.
5. Bulk barriers must be bounded per-tick.
6. `patSvc_idle()` must be called at every filesystem replacement boundary.
7. Queue drop on full is the accepted failure mode.

### 12.15 Background CPU budget

Session 069 Pass 2 added a shared elapsed-time budget primitive in
`filesystem.c` that bounds background work across three consumer classes:
repair (`patSvc_tick()`), scalar AutoSave drain, and Pattern AutoSave drain.

- **Budget rate**: 2.5% during playback (25µs/ms), 5% stopped (50µs/ms).
- **Refill**: `filesystem_backgroundBudgetRefill()` adds credit based on
  wall time elapsed since last refill, capped at one millisecond of
  accumulated time to prevent idle buildup.
- **Charge**: `filesystem_backgroundBudgetCharge(start, end)` subtracts
  elapsed work time. Signed credit allows overshoot tracking.
- **Query**: `filesystem_backgroundBudgetAvailable()` returns `credit > 0`.

The repair section of `patSvc_tick()` checks budget availability before
proceeding. The Load/Save repair gate additionally suppresses repair when
`menu_activePage == LOAD_PAGE || menu_activePage == SAVE_PAGE`.

DEV-only `H` AutosaveTrace reports every ~5 seconds show per-class
cumulative microseconds (repair, scalar drain, pattern drain).

RAM: 8 bytes always-on (credit + last refill timestamp) + 28 bytes DEV
accounting.

### 12.16 PatternTrace stage codes

Twelve stage codes in `PatternTrace.h` for service and live-record
diagnostics:

| Code | Meaning |
|------|---------|
| `H` | Pending-buffer overflow witness |
| `Q` | Queue event (enqueue/dequeue/drop) |
| `C` | Capacity drop |
| `F` | Fragmentation drop |
| `R` | Reactive relocation (historical enum name: `TIER2_RELOC`) |
| `M` | Retired Tier-1 gap relocation |
| `G` | Retired gap fallback |
| `X` | Service state change (handover, mode transition) |
| `V` | Repair created an in-place trailing reservation |
| `L` | Repair relocated a block to create a reservation |
| `D` | Direct-path mutation retained for reactive recovery |
| `W` | Live-record drop (S081, `PAT_TRACE_STAGE_LIVEREC_DROP`, DEV only): cause in flags, `track << 8 | step` in value. Causes: 1 handover (target not open), 2 ownership, 3/4 refused write (the code uses 4 for every refused record or automation write), 5 boundary ring full (pushed from `seq_recRingPush()`, also counted in `seq_recRingDrops`). `liveRec_drops[7]` counts causes. `tools/decode_devlogs.py` does not decode `W` yet |

These are PatternTrace codes in `PatternTrace.h`, distinct from the
AutoSaveTrace codes in `AutosaveTrace.h` that use the same single-letter
convention.

### 12.17 Copy/clear: exclusive boundary, raw block API, compaction (S075)

**Exclusive boundary.** `patSvc_beginExclusive(scene)` /
`patSvc_endExclusive(scene)` give one caller sole write access to one Scene's
Pattern region, on any resident Scene (the active Scene may differ from the
playback Scene). Begin records the claim, closes admission on the service
Scene and returns 1 once the FIFO, bulk barrier and reactive recovery have
drained (0 while a filesystem replacement or another claim is pending); it
clears the reservation image. While the claim exists the handover branch of
`patSvc_tick()` neither reopens admission nor adopts the claimed Scene. End
recounts occupancy, restarts the repair epoch and reopens admission or
resumes the handover. One holder at a time.

**Raw block API** (`PatternData.h`, legal only inside the boundary):
`pat_rawReadBlock`, `pat_rawBlockBytes`, `pat_rawDecode`, `pat_rawEncode`,
`pat_rawPlace`, `pat_rawPlaceViaSwap`, `pat_rawSwapReturn`,
`pat_rawPublishEmpty`, `pat_rawFreeChunks`, `pat_rawSwapFree`, and the
whole-region set `pat_rawRegionSilence`, `pat_rawRegionCopyBody`,
`pat_rawRegionPublishSteps`, `pat_rawRegionCopiedBlock`,
`pat_rawRegionPublishRewritten`, `pat_rawRegionReset`. Every step write keeps
§12.10: new bytes into unreferenced space, one PRIMASK store of the complete
address entry (trigger policy KEEP re-reads the live trigger bit), then free
the old run. A step rewrite that does not grow goes through the swap block:
place there, publish, free the old run, then `pat_rawSwapReturn()` moves it
into the freed run and publishes again.

**Whole-region copy order** (`copy pattern`, `copy scene`): publish every
destination entry as empty (trigger off), copy pool, bitmap, track settings
and Pattern globals, rewrite retargeted blocks in place while still
unreferenced (they only shrink; freed tail chunks return to the bitmap), and
publish the address entries last.

**Compaction for the holder.** `patSvc_exclusiveCompactStep()` is a sliding
compaction: one call moves the block just above the lowest free chunk down
into it, through the empty swap block when the runs overlap. Repeated calls
leave all free space as one run below the swap block, so a growing step
whose new size fits the free chunks always finds its run.
`patSvc_exclusiveEvacuateSwapStep()` moves a pre-S075 block out of the swap
block (or clears orphan swap bits); if a full pool makes that impossible the
copy/clear job that needed the swap block is dropped.

Copy/clear emits only service-level relocation summaries to the S075 stage-`c`
trace; `patSvc_exclusiveCompactStep()` and
`patSvc_exclusiveEvacuateSwapStep()` do not emit one record per moved block.
Pattern paste selections that change triggers capture the source and previous
destination trigger masks at button time, before the queued exclusive job,
and restore only entries still containing the service's write if the job is
dropped. Clears that end trigger-off write those bits at acceptance too. These
early writes are ordinary foreground trigger edits (`pat_setStepActive()`, one
halfword RMW each) outside the claim; the job later publishes the same
trigger state with its blocks.

**Rates.** The copy/clear engines do at most 8 step placements, 16 snapshot
reads or 32 region entries per 2 ms tick. While a filesystem writer that
started before the operation is still running, a whole-call governor limits
them to 0.1 % CPU on average (`COPYCLEAR_UTILITIES.md` §11.5).

**Suspension.** While a copy/clear operation runs, the repair epoch does not
start (`copyClear_backgroundSuspended()` gate beside the Load/Save gate);
queue drain, barriers and handover continue.

**In-place append bound.** `pat_tryAppendAutomation()` never grows a block
into the swap block.

Full behaviour, engine phases and trace layouts:
`COPYCLEAR_UTILITIES.md` §12 and §15.

### 12.18 Session 075 finding: Pattern Load fan-out

Pattern Load's fan-out `memcpy` into Scenes other than the loaded one is not
publication-ordered and can tear one playback tick of a playing destination.
Logged in `SCOPING_TARGETS.md` ("Session 075 findings"); not changed in S075.
