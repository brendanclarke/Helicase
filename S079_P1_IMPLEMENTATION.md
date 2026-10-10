# S079 P1 — Implementation Schedule

Full code implementation schedule for step conditions, roll, and micro-timing
specials per `S079_P1_STEP_CONDITIONS_SPECIALS.md`.

Flash estimate: ~4 KB text growth. RAM estimate: ~67 B new ISR-static SRAM1.
Current headroom: 213,392 B flash free, comfortable margins.

---

## Step 0 — Constants and types (compile-time, all downstream files rebuild)

### S0.1 — `PatternData.h:46-51` — MODIFY special-flags bit definitions

```
/* Special-flags byte assignments for one dynamic pool block.
 *
 * What: encodes which value bytes are present in a pool block. Bits 0..6
 * each gate one stored byte; bit 7 is reserved. The ordering determines the
 * byte sequence in the pool: note, velocity, condition, roll, timing,
 * rhythm, fill. Why: all block readers and writers must agree on bit
 * assignment so the positional byte stream is parsed consistently. Inputs:
 * compile-time masks only. Outputs: PatternData.c, PatternStackService.c,
 * and the Sequencer use these to agree on value-byte ordering. Affiliates:
 * pat_blockRead(), pat_blockWrite(), pat_readStepSpecials(),
 * pat_rawDecode(), pat_rawEncode(), pat_blockChunks(),
 * patSvc_blockChunksFor().
 */
```

Old:
```c
#define PAT_SPECIAL_NOTE_BIT     (1u << 0)
#define PAT_SPECIAL_VEL_BIT      (1u << 1)
#define PAT_SPECIAL_PROB_BIT     (1u << 2)
#define PAT_SPECIAL_FLAGS_MASK   (PAT_SPECIAL_NOTE_BIT | \
                                  PAT_SPECIAL_VEL_BIT | \
                                  PAT_SPECIAL_PROB_BIT)
```

New:
```c
#define PAT_SPECIAL_NOTE_BIT      (1u << 0)
#define PAT_SPECIAL_VEL_BIT       (1u << 1)
#define PAT_SPECIAL_COND_BIT      (1u << 2)
#define PAT_SPECIAL_ROLL_BIT      (1u << 3)
#define PAT_SPECIAL_TIMING_BIT    (1u << 4)
#define PAT_SPECIAL_RHYTHM_BIT    (1u << 5)
#define PAT_SPECIAL_FILL_BIT      (1u << 6)
#define PAT_SPECIAL_FLAGS_MASK    0x7Fu
```

Rename `PAT_SPECIAL_PROB_BIT` → `PAT_SPECIAL_COND_BIT` throughout the entire
codebase (PatternData.c, PatternStackService.c, any other reference).

### S0.2 — `PatternData.h:191-196` — MODIFY `pat_step_specials_t`

```
/* Resolved values read from one dynamic step block.
 *
 * What: holds the seven per-step special values plus a flags byte reporting
 * which were explicitly stored. Absent fields carry their default: note 63,
 * velocity 100, condition 0 (always fire), roll 0 (off), timing 0 (none),
 * rhythm 0 (1-1), fill 100 (full). Why: callers read one struct instead of
 * parsing the pool block. Inputs: Scene/track/step coordinate. Outputs:
 * usable values for Sequencer playback, STEP menu display/edit, and
 * copy/clear. Affiliates: pat_readStepSpecials(), pat_rawDecode(),
 * pat_blockRead(), seq_advanceTrackStep(), pat_applyStepToMenu().
 */
typedef struct {
    uint8_t note;       /* bit 0 */
    uint8_t velocity;   /* bit 1 */
    uint8_t condition;  /* bit 2: trigger condition index 0..36 */
    uint8_t roll;       /* bit 3: roll rate index */
    uint8_t timing;     /* bit 4: micro-timing delay 0..99 */
    uint8_t rhythm;     /* bit 5: roll rhythm packed byte */
    uint8_t fill;       /* bit 6: roll fill percent 0..100 */
    uint8_t flags;      /* which bits were present */
} pat_step_specials_t;
```

Remove the old `probability` field entirely. All callers of `.probability`
must be updated to `.condition`.

### S0.3 — `PatternData.h:255` — MODIFY `PAT_RAW_BLOCK_MAX`

```
/* Maximum encoded block size: 2-byte header + 1 flags byte + 7 special
 * value bytes + 63 × 2-byte automation entries = 136 bytes (34 chunks).
 * Why: PAT_RAW_BLOCK_MAX sizes every stack-allocated block buffer and must
 * be the ceiling across all callers. Affiliates: pat_rawReadBlock(),
 * pat_rawEncode(), pat_rawRegionCopiedBlock(), copy/clear paste buffers.
 */
```

Old: `#define PAT_RAW_BLOCK_MAX   132u`
New: `#define PAT_RAW_BLOCK_MAX   136u`

### S0.4 — `PatternData.h:248-250` — MODIFY comment on raw block API

Update the comment to reference 7 specials and 34 chunks instead of 3
specials and 33 chunks.

### S0.5 — `PatternData.h:297-300` — MODIFY `pat_rawEncode` signature

Old:
```c
uint8_t pat_rawEncode(uint8_t out[PAT_RAW_BLOCK_MAX], uint8_t flags,
                      uint8_t note, uint8_t velocity, uint8_t probability,
                      const pat_automation_entry_t *autos, uint8_t count);
```

New:
```c
/* Encode a block into a caller buffer.
 *
 * What: writes a complete pool block from the specials struct and automation
 * entries. Why: separating encode from placement lets copy/clear rewrite
 * blocks without placement. Inputs: specials struct (only fields gated by
 * flags are written), up to 63 automation entries. Output: allocated byte
 * size, or 0 when empty or invalid. The back-reference field is left zero;
 * placement stamps it. Affiliates: pat_rawPlace(), pat_rawPlaceViaSwap(),
 * copyClearService.c.
 */
uint8_t pat_rawEncode(uint8_t out[PAT_RAW_BLOCK_MAX], uint8_t flags,
                      const pat_step_specials_t *specials,
                      const pat_automation_entry_t *autos, uint8_t count);
```

This widens the encode API from three positional value args to one struct
pointer, matching the wider specials set. All callers of `pat_rawEncode()`
must be updated.

### S0.6 — `PatternData.h:430-436` — MODIFY step-special setter declarations

Old:
```c
uint8_t pat_setStepProbability(uint8_t scene_index, uint8_t track,
                               uint8_t step, uint8_t value);
```

Rename to:
```c
/* Set or clear one step's trigger condition.
 *
 * What: retain all other specials while changing the condition index.
 * Value 0 is the always-fire default and clears the condition flag bit.
 * Inputs: Scene/track/step and a condition index 0..36. Output:
 * pat_writeSpecials() updates or releases storage and returns nonzero
 * after commit. Affiliates: menu PAR_STEP_CONDITION dispatch,
 * pat_readStepSpecials().
 */
uint8_t pat_setStepCondition(uint8_t scene_index, uint8_t track,
                             uint8_t step, uint8_t value);
```

ADD four new setters:
```c
/* Set or clear one step's roll rate.
 *
 * What: retain all other specials while changing roll rate. Value 0 is
 * off (no roll) and clears the roll flag bit. Inputs: Scene/track/step
 * and a roll rate index 0..8. Output: pat_writeSpecials() updates or
 * releases storage. Affiliates: menu PAR_STEP_ROLL dispatch.
 */
uint8_t pat_setStepRoll(uint8_t scene_index, uint8_t track,
                        uint8_t step, uint8_t value);

/* Set or clear one step's micro-timing delay.
 *
 * What: retain all other specials while changing timing. Value 0 is no
 * delay and clears the timing flag bit. Inputs: Scene/track/step and a
 * delay value 0..99. Output: pat_writeSpecials() updates or releases
 * storage. Affiliates: menu PAR_STEP_TIMING dispatch.
 */
uint8_t pat_setStepTiming(uint8_t scene_index, uint8_t track,
                          uint8_t step, uint8_t value);

/* Set or clear one step's roll rhythm pattern.
 *
 * What: retain all other specials while changing rhythm. Value 0 is the
 * default 1-1 pattern and clears the rhythm flag bit. Inputs:
 * Scene/track/step and a packed rhythm byte. Output: pat_writeSpecials()
 * updates or releases storage. Affiliates: menu PAR_STEP_RHYTHM dispatch.
 */
uint8_t pat_setStepRhythm(uint8_t scene_index, uint8_t track,
                           uint8_t step, uint8_t value);

/* Set or clear one step's roll fill percentage.
 *
 * What: retain all other specials while changing fill. Value 100 is the
 * default (full step) and clears the fill flag bit. Inputs:
 * Scene/track/step and a fill value 0..100. Output: pat_writeSpecials()
 * updates or releases storage. Affiliates: menu PAR_STEP_FILL dispatch.
 */
uint8_t pat_setStepFill(uint8_t scene_index, uint8_t track,
                        uint8_t step, uint8_t value);
```

### S0.7 — `config.h:314` — MODIFY swap block size

Old:
```c
#define PAT_POOL_SWAP_CHUNKS      33u
```

New:
```c
/* Permanent Pattern pool swap block (S075, updated S079).
 *
 * What: the top 34 chunks (136 B, one maximum dynamic block with 7 specials
 * and 63 automation entries) of every Scene pool are kept out of ordinary
 * allocation. Why: copy/clear paste requires one maximum block placement
 * even into a full pool. Inputs: PAT_RAW_BLOCK_MAX / 4 rounded up.
 * Outputs: allocator bound, swap byte offset. Affiliates: PatternData.c,
 * PatternStackService.c.
 */
#define PAT_POOL_SWAP_CHUNKS      34u
```

### S0.8 — `config.h:306` — MODIFY comment

Update the comment text from "33 chunks (132 B)" to "34 chunks (136 B, one
maximum dynamic block with 7 specials)".

---

## Step 1 — PatternData.c block reader/writer/encoder/decoder

All changes in `Core/Bank/Scene/Pattern/PatternData.c`.

### S1.1 — `PatternData.c:356-373` — MODIFY `pat_blockChunks()`

Add counting for the four new flag bits. The function currently counts
note/vel/prob bits to determine `value_count`. Add:

```c
/* Block size calculator for the 7-special pool format (S079).
 *
 * What: counts set bits in special_flags to determine how many value bytes
 * precede the automation tail: note (bit 0), velocity (bit 1), condition
 * (bit 2), roll (bit 3), timing (bit 4), rhythm (bit 5), fill (bit 6).
 * Why: allocator, free, reallocation, in-place append, and integrity check
 * must agree on exact block ownership. Inputs: masked special flags and
 * bounded automation count. Output: four-byte chunk count.
 * Affiliates: pat_blockWrite(), pat_blockRead(), pat_blockReadAutomations(),
 * pat_tryAppendAutomation(), pat_writeDynamic().
 */
static uint8_t pat_blockChunks(uint8_t special_flags, uint8_t auto_count)
{
    uint8_t value_count = 0u;
    uint8_t total;

    special_flags &= (uint8_t)PAT_SPECIAL_FLAGS_MASK;
    if (special_flags & PAT_SPECIAL_NOTE_BIT)   value_count++;
    if (special_flags & PAT_SPECIAL_VEL_BIT)    value_count++;
    if (special_flags & PAT_SPECIAL_COND_BIT)   value_count++;
    if (special_flags & PAT_SPECIAL_ROLL_BIT)   value_count++;
    if (special_flags & PAT_SPECIAL_TIMING_BIT) value_count++;
    if (special_flags & PAT_SPECIAL_RHYTHM_BIT) value_count++;
    if (special_flags & PAT_SPECIAL_FILL_BIT)   value_count++;
    if (auto_count > PAT_BLOCK_AUTO_COUNT_MASK)
        auto_count = PAT_BLOCK_AUTO_COUNT_MASK;
    total = (uint8_t)(PAT_BLOCK_HEADER_BYTES + 1u + value_count +
                      ((uint16_t)auto_count * 2u));
    return (uint8_t)((total + 3u) >> 2u);
}
```

### S1.2 — `PatternData.c:386-429` — MODIFY `pat_blockWrite()`

Extend to write all 7 specials in flag-bit order. Replace the three
positional args (`note`, `velocity`, `probability`) with a
`const pat_step_specials_t *sp` pointer.

```
/* Write one complete dynamic block at an allocated pool offset.
 *
 * What: encode the step back-reference, special flags/values, and packed
 * automation entries in the fixed block order. The 7-special byte order is:
 * note, velocity, condition, roll, timing, rhythm, fill — matching the
 * ascending bit order in the flags byte. Why: every reader needs one stable
 * layout. Inputs: region/offset, bounded track/step, the specials struct
 * (only fields gated by flags are written), and up to 63 decoded automation
 * entries. Output: the block is written at the allocated offset.
 * Affiliates: pat_blockRead(), pat_writeDynamic(), pat_rawEncode().
 */
```

Old write body (lines 416-421):
```c
    if (special_flags & PAT_SPECIAL_NOTE_BIT) p[idx++] = note;
    if (special_flags & PAT_SPECIAL_VEL_BIT)  p[idx++] = velocity;
    if (special_flags & PAT_SPECIAL_PROB_BIT) p[idx++] = probability;
```

New:
```c
    if (special_flags & PAT_SPECIAL_NOTE_BIT)   p[idx++] = sp->note;
    if (special_flags & PAT_SPECIAL_VEL_BIT)    p[idx++] = sp->velocity;
    if (special_flags & PAT_SPECIAL_COND_BIT)   p[idx++] = sp->condition;
    if (special_flags & PAT_SPECIAL_ROLL_BIT)   p[idx++] = sp->roll;
    if (special_flags & PAT_SPECIAL_TIMING_BIT) p[idx++] = sp->timing;
    if (special_flags & PAT_SPECIAL_RHYTHM_BIT) p[idx++] = sp->rhythm;
    if (special_flags & PAT_SPECIAL_FILL_BIT)   p[idx++] = sp->fill;
```

All callers of `pat_blockWrite()` must pass `const pat_step_specials_t *`
instead of three positional values. The only internal caller is
`pat_writeDynamic()` (S1.5 below).

### S1.3 — `PatternData.c:441-474` — MODIFY `pat_blockRead()`

Extend to read all 7 specials. Update defaults for the new fields.

```
/* Read resolved values from one dynamic block.
 *
 * What: parse the flags byte and value bytes in ascending flag order for all
 * 7 specials. Why: Sequencer and STEP menu must resolve the same defaults
 * and overrides. Inputs: valid allocated Scene region offset. Output:
 * specials struct with defaults for absent fields: note PAT_DEFAULT_NOTE,
 * velocity PAT_DEFAULT_VELOCITY, condition 0, roll 0, timing 0, rhythm 0,
 * fill 100. Affiliates: pat_readStepSpecials().
 */
```

Old defaults (lines 450-452):
```c
    out.note = PAT_DEFAULT_NOTE;
    out.velocity = PAT_DEFAULT_VELOCITY;
    out.probability = 127u;
```

New:
```c
    out.note = PAT_DEFAULT_NOTE;
    out.velocity = PAT_DEFAULT_VELOCITY;
    out.condition = 0u;
    out.roll = 0u;
    out.timing = 0u;
    out.rhythm = 0u;
    out.fill = 100u;
```

Old read body (lines 466-471):
```c
    if (flags & PAT_SPECIAL_NOTE_BIT) out.note = p[idx++];
    if (flags & PAT_SPECIAL_VEL_BIT)  out.velocity = p[idx++];
    if (flags & PAT_SPECIAL_PROB_BIT) out.probability = p[idx++];
```

New:
```c
    if (flags & PAT_SPECIAL_NOTE_BIT)   out.note = p[idx++];
    if (flags & PAT_SPECIAL_VEL_BIT)    out.velocity = p[idx++];
    if (flags & PAT_SPECIAL_COND_BIT)   out.condition = p[idx++];
    if (flags & PAT_SPECIAL_ROLL_BIT)   out.roll = p[idx++];
    if (flags & PAT_SPECIAL_TIMING_BIT) out.timing = p[idx++];
    if (flags & PAT_SPECIAL_RHYTHM_BIT) out.rhythm = p[idx++];
    if (flags & PAT_SPECIAL_FILL_BIT)   out.fill = p[idx++];
```

### S1.4 — `PatternData.c:486-523` — MODIFY `pat_blockReadAutomations()`

The automation tail offset calculation counts value bytes from set flag bits
(lines 503-508). Add the four new bits:

```c
    if (flags & PAT_SPECIAL_NOTE_BIT)   value_count++;
    if (flags & PAT_SPECIAL_VEL_BIT)    value_count++;
    if (flags & PAT_SPECIAL_COND_BIT)   value_count++;
    if (flags & PAT_SPECIAL_ROLL_BIT)   value_count++;
    if (flags & PAT_SPECIAL_TIMING_BIT) value_count++;
    if (flags & PAT_SPECIAL_RHYTHM_BIT) value_count++;
    if (flags & PAT_SPECIAL_FILL_BIT)   value_count++;
```

### S1.5 — `PatternData.c:539-607` — MODIFY `pat_tryAppendAutomation()`

Same value_count expansion at lines 557-562:

```c
    if (old_flags & PAT_SPECIAL_NOTE_BIT)   value_count++;
    if (old_flags & PAT_SPECIAL_VEL_BIT)    value_count++;
    if (old_flags & PAT_SPECIAL_COND_BIT)   value_count++;
    if (old_flags & PAT_SPECIAL_ROLL_BIT)   value_count++;
    if (old_flags & PAT_SPECIAL_TIMING_BIT) value_count++;
    if (old_flags & PAT_SPECIAL_RHYTHM_BIT) value_count++;
    if (old_flags & PAT_SPECIAL_FILL_BIT)   value_count++;
```

### S1.6 — `PatternData.c:622-753` — MODIFY `pat_writeDynamic()`

Update signature: replace positional `note, velocity, probability` with
`const pat_step_specials_t *sp`. Pass the struct pointer through to
`pat_blockWrite()`.

Old signature (line 622):
```c
static uint8_t pat_writeDynamic(uint8_t scene_index, uint8_t track,
                                uint8_t step, uint8_t new_flags,
                                uint8_t note, uint8_t velocity,
                                uint8_t probability,
                                const pat_automation_entry_t *autos,
                                uint8_t auto_count,
                                uint8_t allow_shrink_in_place)
```

New:
```c
/* Replace one step's complete dynamic block while preserving its trigger bit.
 *
 * What: free, reuse, or allocate the block selected by the special flags and
 * automation list, then swap the address entry. The specials struct supplies
 * all 7 value fields; only those gated by new_flags are written. Why:
 * special and automation edits share one ownership transaction. Inputs:
 * Scene/track/step, new flags, specials struct, automation list, shrink
 * policy. Output: nonzero on commit; allocation failure leaves old state.
 * Affiliates: pat_writeSpecials(), automation CRUD.
 */
static uint8_t pat_writeDynamic(uint8_t scene_index, uint8_t track,
                                uint8_t step, uint8_t new_flags,
                                const pat_step_specials_t *sp,
                                const pat_automation_entry_t *autos,
                                uint8_t auto_count,
                                uint8_t allow_shrink_in_place)
```

Update the `pat_blockWrite()` call (line 736):
Old: `pat_blockWrite(r, new_offset, track, step, new_flags, note, velocity, probability, autos, auto_count);`
New: `pat_blockWrite(r, new_offset, track, step, new_flags, sp, autos, auto_count);`

### S1.7 — `PatternData.c:764-784` — MODIFY `pat_writeSpecials()`

Update to pass a `pat_step_specials_t` pointer to `pat_writeDynamic()` instead
of three positional values.

```
/* Replace one step's special values while retaining every automation entry.
 *
 * What: reads the existing automations, then calls pat_writeDynamic() with
 * the new specials struct and the full automation list. Why: individual
 * setters only change one field; this helper preserves all others. Inputs:
 * Scene/track/step, desired flags, and the complete specials struct with
 * the new value already set. Output: commit or failure. Affiliates:
 * pat_setStepNote(), pat_setStepVolume(), pat_setStepCondition(),
 * pat_setStepRoll(), pat_setStepTiming(), pat_setStepRhythm(),
 * pat_setStepFill().
 */
static uint8_t pat_writeSpecials(uint8_t scene_index, uint8_t track,
                                 uint8_t step, uint8_t new_flags,
                                 const pat_step_specials_t *sp)
```

Update the `pat_writeDynamic()` call (line 782):
Old: `return pat_writeDynamic(scene_index, track, step, new_flags, note, velocity, probability, autos, auto_count, 0u);`
New: `return pat_writeDynamic(scene_index, track, step, new_flags, sp, autos, auto_count, 0u);`

### S1.8 — `PatternData.c:1014-1037` — MODIFY `pat_readStepSpecials()`

Update defaults for the new fields (mirroring S1.3):

Old (lines 1022-1024):
```c
    out.note = PAT_DEFAULT_NOTE;
    out.velocity = PAT_DEFAULT_VELOCITY;
    out.probability = 127u;
```

New:
```c
    out.note = PAT_DEFAULT_NOTE;
    out.velocity = PAT_DEFAULT_VELOCITY;
    out.condition = 0u;
    out.roll = 0u;
    out.timing = 0u;
    out.rhythm = 0u;
    out.fill = 100u;
```

### S1.9 — `PatternData.c:1398-1405` — MODIFY `pat_applyStepToMenu()`

Add the new step specials to the menu parameter buffer.

Old:
```c
    parameter_values[PAR_STEP_NOTE] = sp.note;
    parameter_values[PAR_STEP_VOLUME] = sp.velocity;
    parameter_values[PAR_STEP_PROB] = sp.probability;
```

New:
```c
/* Apply all 7 step specials to the menu parameter buffer.
 *
 * What: copies the resolved special values from PatternData into the flat
 * parameter_values[] array so the STEP page can display and edit them.
 * Inputs: the specials struct from pat_readStepSpecials(). Output: menu
 * parameter buffer updated for all step-special PAR_ entries. Affiliates:
 * menu_showStepEditPage(), PAR_ACTIVE_STEP handler.
 */
    parameter_values[PAR_STEP_NOTE] = sp.note;
    parameter_values[PAR_STEP_VOLUME] = sp.velocity;
    parameter_values[PAR_STEP_CONDITION] = sp.condition;
    parameter_values[PAR_STEP_TIMING] = sp.timing;
    parameter_values[PAR_STEP_ROLL] = sp.roll;
    parameter_values[PAR_STEP_RHYTHM] = sp.rhythm;
    parameter_values[PAR_STEP_FILL] = sp.fill;
```

### S1.10 — `PatternData.c:1417-1429` — MODIFY `pat_setStepNote()`

Update to build a `pat_step_specials_t` and call the new `pat_writeSpecials()`:

```c
uint8_t pat_setStepNote(uint8_t scene_index, uint8_t track, uint8_t step,
                        uint8_t value)
{
    pat_step_specials_t sp = pat_readStepSpecials(scene_index, track, step);
    uint8_t new_flags;

    sp.note = value;
    if (value == PAT_DEFAULT_NOTE)
        new_flags = (uint8_t)(sp.flags & (uint8_t)~PAT_SPECIAL_NOTE_BIT);
    else
        new_flags = (uint8_t)(sp.flags | PAT_SPECIAL_NOTE_BIT);
    return pat_writeSpecials(scene_index, track, step, new_flags, &sp);
}
```

### S1.11 — `PatternData.c:1441-1453` — MODIFY `pat_setStepVolume()`

Same pattern as S1.10, using `sp.velocity = value` and `PAT_SPECIAL_VEL_BIT`.

### S1.12 — `PatternData.c:1465-1477` — REMOVE `pat_setStepProbability()`, ADD `pat_setStepCondition()`

```c
/* Set or clear one step's trigger condition (S079).
 *
 * What: condition value 0 is the always-fire default and clears the
 * condition flag; nonzero values store the condition index. Inputs:
 * Scene/track/step and condition 0..36. Output: pat_writeSpecials()
 * commit. Affiliates: menu PAR_STEP_CONDITION dispatch.
 */
uint8_t pat_setStepCondition(uint8_t scene_index, uint8_t track,
                             uint8_t step, uint8_t value)
{
    pat_step_specials_t sp = pat_readStepSpecials(scene_index, track, step);
    uint8_t new_flags;

    sp.condition = value;
    if (value == 0u)
        new_flags = (uint8_t)(sp.flags & (uint8_t)~PAT_SPECIAL_COND_BIT);
    else
        new_flags = (uint8_t)(sp.flags | PAT_SPECIAL_COND_BIT);
    return pat_writeSpecials(scene_index, track, step, new_flags, &sp);
}
```

### S1.13 — `PatternData.c` after S1.12 — ADD `pat_setStepRoll()`

```c
/* Set or clear one step's roll rate (S079).
 *
 * What: value 0 is off (no roll) and clears the flag. Inputs:
 * Scene/track/step and roll index 0..8. Output: pat_writeSpecials() commit.
 * Affiliates: menu PAR_STEP_ROLL dispatch.
 */
uint8_t pat_setStepRoll(uint8_t scene_index, uint8_t track,
                        uint8_t step, uint8_t value)
{
    pat_step_specials_t sp = pat_readStepSpecials(scene_index, track, step);
    uint8_t new_flags;

    sp.roll = value;
    if (value == 0u)
        new_flags = (uint8_t)(sp.flags & (uint8_t)~PAT_SPECIAL_ROLL_BIT);
    else
        new_flags = (uint8_t)(sp.flags | PAT_SPECIAL_ROLL_BIT);
    return pat_writeSpecials(scene_index, track, step, new_flags, &sp);
}
```

### S1.14 — ADD `pat_setStepTiming()`

Same pattern: default 0 clears `PAT_SPECIAL_TIMING_BIT`.

### S1.15 — ADD `pat_setStepRhythm()`

Same pattern: default 0 clears `PAT_SPECIAL_RHYTHM_BIT`.

### S1.16 — ADD `pat_setStepFill()`

Default 100 clears `PAT_SPECIAL_FILL_BIT`.

```c
uint8_t pat_setStepFill(uint8_t scene_index, uint8_t track,
                        uint8_t step, uint8_t value)
{
    pat_step_specials_t sp = pat_readStepSpecials(scene_index, track, step);
    uint8_t new_flags;

    sp.fill = value;
    if (value == 100u)
        new_flags = (uint8_t)(sp.flags & (uint8_t)~PAT_SPECIAL_FILL_BIT);
    else
        new_flags = (uint8_t)(sp.flags | PAT_SPECIAL_FILL_BIT);
    return pat_writeSpecials(scene_index, track, step, new_flags, &sp);
}
```

### S1.17 — `PatternData.c:1619-1663` — MODIFY `pat_rawDecode()`

Extend defaults and value-byte parsing for all 7 specials, same as S1.3.

Old defaults (lines 1629-1631):
```c
    specials->note = PAT_DEFAULT_NOTE;
    specials->velocity = PAT_DEFAULT_VELOCITY;
    specials->probability = 127u;
```

New:
```c
    specials->note = PAT_DEFAULT_NOTE;
    specials->velocity = PAT_DEFAULT_VELOCITY;
    specials->condition = 0u;
    specials->roll = 0u;
    specials->timing = 0u;
    specials->rhythm = 0u;
    specials->fill = 100u;
```

Old read (lines 1638-1649):
```c
    if (flags & PAT_SPECIAL_NOTE_BIT) { if (specials) specials->note = block[index]; index++; }
    if (flags & PAT_SPECIAL_VEL_BIT)  { if (specials) specials->velocity = block[index]; index++; }
    if (flags & PAT_SPECIAL_PROB_BIT) { if (specials) specials->probability = block[index]; index++; }
```

New: add 4 more lines for COND/ROLL/TIMING/RHYTHM/FILL using the same pattern.

### S1.18 — `PatternData.c:1672-1700` — MODIFY `pat_rawEncode()`

Update signature per S0.5 (take `const pat_step_specials_t *specials` instead
of three positional args). Write all 7 values:

Old (lines 1690-1692):
```c
    if (flags & PAT_SPECIAL_NOTE_BIT) out[index++] = note;
    if (flags & PAT_SPECIAL_VEL_BIT)  out[index++] = velocity;
    if (flags & PAT_SPECIAL_PROB_BIT) out[index++] = probability;
```

New:
```c
    if (flags & PAT_SPECIAL_NOTE_BIT)   out[index++] = specials->note;
    if (flags & PAT_SPECIAL_VEL_BIT)    out[index++] = specials->velocity;
    if (flags & PAT_SPECIAL_COND_BIT)   out[index++] = specials->condition;
    if (flags & PAT_SPECIAL_ROLL_BIT)   out[index++] = specials->roll;
    if (flags & PAT_SPECIAL_TIMING_BIT) out[index++] = specials->timing;
    if (flags & PAT_SPECIAL_RHYTHM_BIT) out[index++] = specials->rhythm;
    if (flags & PAT_SPECIAL_FILL_BIT)   out[index++] = specials->fill;
```

All callers of `pat_rawEncode()` must be updated to pass the struct pointer.
Search for `pat_rawEncode(` in PatternData.c and copyClearService.c to find
all call sites.

---

## Step 2 — PatternStackService.c

### S2.1 — `PatternStackService.c:45-56` — MODIFY op enum

Rename `PATSVC_OP_SET_PROBABILITY` → `PATSVC_OP_SET_CONDITION`.

ADD four new ops:
```c
    PATSVC_OP_SET_CONDITION,
    PATSVC_OP_SET_ROLL,
    PATSVC_OP_SET_TIMING,
    PATSVC_OP_SET_RHYTHM,
    PATSVC_OP_SET_FILL,
```

### S2.2 — `PatternStackService.c:481-498` — MODIFY `patSvc_blockChunksFor()`

Add 4 new flag-bit counts (same as S1.1 pattern):
```c
    if (flags & PAT_SPECIAL_COND_BIT)   value_count++;
    if (flags & PAT_SPECIAL_ROLL_BIT)   value_count++;
    if (flags & PAT_SPECIAL_TIMING_BIT) value_count++;
    if (flags & PAT_SPECIAL_RHYTHM_BIT) value_count++;
    if (flags & PAT_SPECIAL_FILL_BIT)   value_count++;
```

### S2.3 — `PatternStackService.c:925-973` — MODIFY `patSvc_requiredChunks()`

Extend the operation switch to handle the four new ops with their default
values. Add the new ops to the guard at line 949:

Old (line 949):
```c
    if (operation != PATSVC_OP_SET_NOTE &&
        operation != PATSVC_OP_SET_VOLUME &&
        operation != PATSVC_OP_SET_PROBABILITY)
```

New:
```c
    if (operation != PATSVC_OP_SET_NOTE &&
        operation != PATSVC_OP_SET_VOLUME &&
        operation != PATSVC_OP_SET_CONDITION &&
        operation != PATSVC_OP_SET_ROLL &&
        operation != PATSVC_OP_SET_TIMING &&
        operation != PATSVC_OP_SET_RHYTHM &&
        operation != PATSVC_OP_SET_FILL)
```

Add flag-toggle cases for each new op (lines 956-970):
```c
    } else if (operation == PATSVC_OP_SET_CONDITION) {
        if (payload == 0u)
            flags &= (uint8_t)~PAT_SPECIAL_COND_BIT;
        else
            flags |= PAT_SPECIAL_COND_BIT;
    } else if (operation == PATSVC_OP_SET_ROLL) {
        if (payload == 0u)
            flags &= (uint8_t)~PAT_SPECIAL_ROLL_BIT;
        else
            flags |= PAT_SPECIAL_ROLL_BIT;
    } else if (operation == PATSVC_OP_SET_TIMING) {
        if (payload == 0u)
            flags &= (uint8_t)~PAT_SPECIAL_TIMING_BIT;
        else
            flags |= PAT_SPECIAL_TIMING_BIT;
    } else if (operation == PATSVC_OP_SET_RHYTHM) {
        if (payload == 0u)
            flags &= (uint8_t)~PAT_SPECIAL_RHYTHM_BIT;
        else
            flags |= PAT_SPECIAL_RHYTHM_BIT;
    } else if (operation == PATSVC_OP_SET_FILL) {
        if (payload == 100u)
            flags &= (uint8_t)~PAT_SPECIAL_FILL_BIT;
        else
            flags |= PAT_SPECIAL_FILL_BIT;
    }
```

### S2.4 — `PatternStackService.c:976-1010` — MODIFY `patSvc_executeEvent()`

Add dispatch cases for the new ops:
```c
    case PATSVC_OP_SET_CONDITION:
        return pat_setStepCondition(service_scene, track, step, (uint8_t)payload);
    case PATSVC_OP_SET_ROLL:
        return pat_setStepRoll(service_scene, track, step, (uint8_t)payload);
    case PATSVC_OP_SET_TIMING:
        return pat_setStepTiming(service_scene, track, step, (uint8_t)payload);
    case PATSVC_OP_SET_RHYTHM:
        return pat_setStepRhythm(service_scene, track, step, (uint8_t)payload);
    case PATSVC_OP_SET_FILL:
        return pat_setStepFill(service_scene, track, step, (uint8_t)payload);
```

### S2.5 — `PatternStackService.c:1608-1617` — REMOVE `patSvc_setStepProbability()`, ADD new service setters

Rename the existing function and add four new ones with the same pattern:

```c
/* Submit a condition-special edit through the Pattern stack service.
 *
 * What: queues PATSVC_OP_SET_CONDITION with the condition index. Value 0
 * clears the condition. Inputs: Scene/track/step coordinates and condition
 * 0..36. Output: optimistic acceptance or 0 on rejection. Affiliates:
 * menu_broadcastStepSpecial() PAR_STEP_CONDITION arm.
 */
uint8_t patSvc_setStepCondition(uint8_t scene, uint8_t track,
                                uint8_t step, uint8_t value)
{
    if (!patSvc_validStepRequest(scene, track, step, 0u, 0u))
        return 0u;
    return patSvc_submit(scene,
                         patSvc_packEvent(PATSVC_OP_SET_CONDITION, track,
                                          step, value), 1u);
}
```

Same pattern for `patSvc_setStepRoll`, `patSvc_setStepTiming`,
`patSvc_setStepRhythm`, `patSvc_setStepFill`.

### S2.6 — `PatternStackService.h:134-135` — MODIFY declarations

Replace `patSvc_setStepProbability` with `patSvc_setStepCondition` and add
4 new declarations:

```c
uint8_t patSvc_setStepCondition(uint8_t scene, uint8_t track,
                                uint8_t step, uint8_t value);
uint8_t patSvc_setStepRoll(uint8_t scene, uint8_t track,
                           uint8_t step, uint8_t value);
uint8_t patSvc_setStepTiming(uint8_t scene, uint8_t track,
                             uint8_t step, uint8_t value);
uint8_t patSvc_setStepRhythm(uint8_t scene, uint8_t track,
                              uint8_t step, uint8_t value);
uint8_t patSvc_setStepFill(uint8_t scene, uint8_t track,
                           uint8_t step, uint8_t value);
```

---

## Step 3 — Sequencer state and condition evaluator

All changes in `Core/Sequencer/sequencer.c` unless noted.

### S3.1 — `sequencer.c` near line 210 — ADD per-track ISR state

```c
/* Per-track repeat count for X:Y conditions (S079 §3.2).
 *
 * What: increments when a track's step index wraps (forward: len-1→0,
 * reverse: 0→len-1, pip: at each boundary reversal, random: per len steps).
 * Why: X:Y conditions need a modulo counter. Reset to 0 on: transport
 * start (seq_setRunning), Scene change (seq_selectActivePattern), per-track
 * Scene change (seq_setTrackPlayedScene). Once modes never wrap, so the
 * counter stays at 0. Inputs: play-mode wrap logic in seq_advanceTrackStep.
 * Outputs: seq_evaluateStepCondition() reads this. RAM: 7 B SRAM1 static.
 * Affiliates: seq_setStepIndexToStart(), seq_selectActivePattern().
 */
static uint8_t seq_trackRepeatCount[NUM_TRACKS];

/* Per-track last-condition-fired flag for lst/!ls chaining (S079 §3.4).
 *
 * What: set to 1 when the last evaluated conditional step (condition 1..36)
 * fired, 0 when it was suppressed. Default 0 (not fired). Updated by
 * seq_evaluateStepCondition() for any step with condition != 0. Why: lst/!ls
 * conditions chain off the most recent conditional step on this track.
 * Inputs: seq_evaluateStepCondition(). Outputs: condition indices 23/24 read
 * this. RAM: 7 B SRAM1 static. Affiliates: seq_setStepIndexToStart().
 */
static uint8_t seq_trackLastConditionFired[NUM_TRACKS];

/* Fill mode active flag (S079 §3.3).
 *
 * What: nonzero while the PERF button has been held past the double-click
 * timeout. Why: fil/!fl conditions read this flag. Written by
 * buttonHandler_tick() PERF hold logic. Inputs: buttonHandler foreground.
 * Outputs: seq_evaluateStepCondition() condition 21/22. RAM: 1 B SRAM1.
 * Affiliates: buttonHandler.c PERF press/release.
 */
uint8_t seq_fillActive;

/* Per-track roll playback state (S079 §4).
 *
 * What: runtime state for per-step roll retrigger timing. seq_rollTickAccum
 * is the sub-tick DDA remainder. seq_rollVelPos tracks the velocity pattern
 * position. seq_rollLastRate/Rhythm/Fill store the previous step's roll
 * parameters for carry-over detection. seq_rollStepVel is the step's
 * resolved velocity. seq_rollRemaining is the number of retriggers left in
 * the fill window. Why: roll retriggers fire from seq_processSchedulerTick
 * using the same tick-driven mechanism as shuffle. Inputs: set by
 * seq_advanceTrackStep() when a step has a roll special. Decremented/fired
 * each PPQ tick. Outputs: seq_triggerVoice() with rhythm-modified velocity.
 * RAM: ~56 B SRAM1 static. Affiliates: seq_processRollTicks().
 */
static uint8_t seq_rollTickAccum[NUM_TRACKS];
static uint8_t seq_rollVelPos[NUM_TRACKS];
static uint8_t seq_rollLastRate[NUM_TRACKS];
static uint8_t seq_rollLastRhythm[NUM_TRACKS];
static uint8_t seq_rollLastFill[NUM_TRACKS];
static uint8_t seq_rollStepVel[NUM_TRACKS];
static uint8_t seq_rollRemaining[NUM_TRACKS];
static uint8_t seq_rollActive[NUM_TRACKS];
```

### S3.2 — `sequencer.c:1264-1276` — MODIFY `seq_evaluateStepCondition()`

Replace the simple probability gate with the full condition dispatch table.

```c
/* Evaluate the per-step trigger condition (S079 §3).
 *
 * What: the single gate before a step fires. Returns 1 (fire) or 0
 * (suppress). Updates seq_trackLastConditionFired for conditional steps.
 * Why: all condition logic lives at one entry point. Condition 0 and
 * undefined values (37..255) always pass. Inputs: specials struct (for
 * condition index), track index (for mute/repeat/last state). Outputs:
 * gate result, side-effect on seq_trackLastConditionFired. Context: TIM3
 * ISR. Affiliates: seq_advanceTrackStep(), GetRngValue(), seq_mutedTracks,
 * seq_trackRepeatCount, seq_fillActive.
 */
static uint8_t seq_evaluateStepCondition(const pat_step_specials_t *sp,
                                          uint8_t track)
{
    uint8_t cond = sp->condition;
    uint8_t result = 1u;

    if (cond == 0u || cond > 36u)
        return 1u;

    /* Probability: values 1..10 → 10%..95%. */
    if (cond <= 10u) {
        uint8_t threshold;
        uint8_t rnd;
        static const uint8_t prob_thresholds[10] = {
            13, 25, 38, 51, 64, 76, 89, 102, 114, 121
        };
        threshold = prob_thresholds[cond - 1u];
        rnd = (uint8_t)(((uint16_t)(GetRngValue() & 0x7FFFu) * 127u) / 32767u);
        result = (uint8_t)(rnd < threshold);
    }
    /* Repeat count: values 11..20. */
    else if (cond <= 20u) {
        if (cond == 20u) {
            result = (uint8_t)(seq_trackRepeatCount[track] == 0u);
        } else {
            static const uint8_t xy_table[9][2] = {
                {1,2},{2,2},{1,3},{2,3},{3,3},{1,4},{2,4},{3,4},{4,4}
            };
            uint8_t x = xy_table[cond - 11u][0];
            uint8_t y = xy_table[cond - 11u][1];
            result = (uint8_t)((seq_trackRepeatCount[track] % y) == (x - 1u));
        }
    }
    /* Fill: values 21..22. */
    else if (cond <= 22u) {
        if (cond == 21u)
            result = seq_fillActive;
        else
            result = (uint8_t)(!seq_fillActive);
    }
    /* Last condition: values 23..24. */
    else if (cond <= 24u) {
        if (cond == 23u)
            result = seq_trackLastConditionFired[track];
        else
            result = (uint8_t)(!seq_trackLastConditionFired[track]);
    }
    /* Mute: values 25..36. */
    else {
        uint8_t voice_idx = (uint8_t)((cond - 25u) % 6u);
        uint8_t is_muted = (uint8_t)((seq_mutedTracks >> voice_idx) & 1u);
        if (cond <= 30u)
            result = is_muted;
        else
            result = (uint8_t)(!is_muted);
    }

    seq_trackLastConditionFired[track] = result;
    return result;
}
```

All callers of `seq_evaluateStepCondition()` must pass `track` as the second
argument. There is one call site: `sequencer.c:1427`.

### S3.3 — `sequencer.c:1300-1516` — MODIFY `seq_advanceTrackStep()`

Multiple changes within this function:

**A. Increment repeat counter on wrap (after play-mode switch).** Add
increment logic at each wrap point:

- Case 0 (fwd), line 1409: after `seq_stepIndex[track] = 0;`, add
  `seq_trackRepeatCount[track]++;`
- Case 1 (rev), line 1374: after `seq_stepIndex[track] = (int16_t)(len-1u);`,
  add `seq_trackRepeatCount[track]++;`
- Case 2 (pip), lines 1380 and 1386: increment at each boundary reversal.
- Case 3 (rnd): maintain a free counter; not counted directly by wrapping.
  ADD a static per-track rnd step counter or use a simple approach: increment
  repeat count every `len` random steps drawn. This requires a per-track
  `seq_trackRndStepCount[7]` counter.

**B. Mute bypass for self-mute conditions (line 1419).** Restructure the
mute gate:

Old (line 1419):
```c
    if (!(seq_mutedTracks & (1u << track))) {
```

New:
```c
    /* Mute gate with self-mute bypass (S079 §3.5).
     *
     * What: muted tracks normally skip all step processing. However, if a
     * step on a muted track has a mute condition that tests its own track,
     * the mute is bypassed and the condition is evaluated. Why: self-mute
     * conditions (e.g. "3mu on track 3") are meaningless without bypass —
     * the step would never be reached. Inputs: seq_mutedTracks bitmask,
     * address entry bit 14, pool condition byte. Output: for unmuted tracks,
     * proceed normally. For muted tracks: check address bit 14 → if specials
     * exist, read condition → if self-mute, evaluate. Affiliates:
     * pat_readStepSpecials(), seq_evaluateStepCondition().
     */
    {
        uint8_t track_muted = (uint8_t)((seq_mutedTracks >> track) & 1u);

        if (track_muted) {
            const pat_scene_region_t *mute_region =
                pat_sceneRegion(seq_perTrackPattern[track]);
            uint16_t addr = mute_region
                ? mute_region->address[track][(uint8_t)seq_stepIndex[track]]
                : PAT_ADDR_SENTINEL;
            uint8_t has_specials = (uint8_t)((addr & PAT_ADDR_SPECIALS_BIT) != 0u);
            uint8_t self_mute = 0u;

            if (has_specials) {
                pat_step_specials_t sp = pat_readStepSpecials(
                    seq_perTrackPattern[track], track,
                    (uint8_t)seq_stepIndex[track]);
                uint8_t cond = sp.condition;
                if ((cond >= 25u && cond <= 30u &&
                     (uint8_t)(cond - 25u) == track) ||
                    (cond >= 31u && cond <= 36u &&
                     (uint8_t)(cond - 31u) == track))
                    self_mute = 1u;
                if (self_mute) {
                    uint8_t step_allowed =
                        seq_evaluateStepCondition(&sp, track);
                    if (step_allowed) {
                        /* bypass mute; proceed to trigger + automation */
                        goto mute_bypassed;
                    }
                }
            }
            goto after_unmuted_block;
        }
    }
```

Then at the end of the existing unmuted block (line 1507), add label:
```c
mute_bypassed:
    /* ... the existing unmuted step processing ... */
after_unmuted_block:
```

Note: this requires restructuring the braces. An alternative to `goto` is
to extract the step-processing body into a helper called from both paths.
Either approach is acceptable; the schedule shows the goto for directness.

**C. Condition evaluator call (line 1427).** Add `track` argument:

Old: `uint8_t step_allowed = seq_evaluateStepCondition(&sp);`
New: `uint8_t step_allowed = seq_evaluateStepCondition(&sp, track);`

**D. Micro-timing deferral (lines 1466-1491).** Extend the shuffle deferral
to include micro-timing delay:

After computing `shuffle_val` and `is_odd_step`, add:
```c
    uint8_t microtiming = sp.timing;
    uint8_t total_delay = 0u;

    if (shuffle_val > 0u && is_odd_step && len > 0u)
        total_delay = (uint8_t)(((uint16_t)shuffle_val * 24u) / 256u);

    if (microtiming > 0u) {
        uint16_t interval = stepScale_ticksQ8(
            seq_effectiveTrackScale[track]);
        uint8_t mt_delay = (uint8_t)(
            ((uint32_t)(interval >> 8u) * microtiming) / 100u);
        total_delay = (uint8_t)(total_delay + mt_delay);
    }
```

Then replace the existing shuffle-only deferral with a combined deferral using
`total_delay`.

**E. Roll setup (after trigger, before automation).** When a step has a roll
rate, set up the per-track roll state:

```c
    if (sp.roll > 0u && step_allowed) {
        /* Roll setup (S079 §4).
         *
         * What: configure per-track roll state for retrigger ticks. If the
         * incoming step's roll parameters match the previous step's, carry
         * over the velocity pattern position and tick accumulator.
         * Otherwise reset. Why: carry-over produces seamless rolls across
         * adjacent steps. Inputs: sp.roll, sp.rhythm, sp.fill, previous
         * step state. Outputs: seq_rollActive, seq_rollTickAccum,
         * seq_rollVelPos, seq_rollRemaining, seq_rollStepVel,
         * seq_rollLast*. Affiliates: seq_processRollTicks().
         */
        uint8_t carry = (uint8_t)(
            seq_rollActive[track] &&
            seq_rollLastRate[track] == sp.roll &&
            seq_rollLastRhythm[track] == sp.rhythm &&
            seq_rollLastFill[track] == sp.fill);
        if (!carry) {
            seq_rollTickAccum[track] = 0u;
            seq_rollVelPos[track] = 0u;
        }
        seq_rollActive[track] = 1u;
        seq_rollLastRate[track] = sp.roll;
        seq_rollLastRhythm[track] = sp.rhythm;
        seq_rollLastFill[track] = sp.fill;
        seq_rollStepVel[track] = sp.velocity;
        /* Compute retrigger count from fill and step interval. */
        /* ... fill window calculation ... */
    } else {
        seq_rollActive[track] = 0u;
    }
```

**F. Suppress live roll for tracks with per-step roll (line 1509-1515).**

Old:
```c
    if (seq_rollRate != 0xffu && (seq_rollState & (1u << track))) {
```

New:
```c
    if (seq_rollRate != 0xffu && (seq_rollState & (1u << track)) &&
        !seq_rollActive[track]) {
```

### S3.4 — `sequencer.c:2056-2071` — MODIFY `seq_processShuffleDelays()`

Rename to `seq_processDeferredTriggers()` to reflect the combined
shuffle + micro-timing mechanism. The logic is unchanged; only the name
changes to clarify its expanded role.

### S3.5 — `sequencer.c` near line 2071 — ADD `seq_processRollTicks()`

```c
/* Tick-driven per-step roll retrigger engine (S079 §4).
 *
 * What: for each track with an active roll, decrement the tick accumulator.
 * When it reaches zero, fire one retrigger with rhythm-modified velocity,
 * reset the accumulator to the roll rate's PPQ interval, advance the
 * velocity pattern position, and decrement the remaining-retrigger count.
 * When remaining reaches zero, fire the final gated trigger (1/10th decay)
 * if fill < 100. Why: roll retriggers run from the PPQ tick clock, same as
 * shuffle. Absolute note values (S079 decided). Inputs: seq_rollActive,
 * seq_rollTickAccum, seq_rollVelPos, seq_rollRemaining, seq_rollStepVel.
 * Outputs: seq_triggerVoice() or seq_triggerVoiceGated() for the final
 * trigger. Context: TIM3 ISR via seq_processSchedulerTick(). RAM: uses
 * only the static arrays declared in S3.1. Affiliates:
 * seq_advanceTrackStep() roll setup, seq_processSchedulerTick().
 */
static void seq_processRollTicks(void)
{
    static const uint8_t rollPpqTable[9] = {
        0, 12, 8, 6, 4, 3, 2, 2, 1
    };
    uint8_t track;

    for (track = 0u; track < NUM_TRACKS; track++) {
        uint8_t ppq;
        uint8_t vel;

        if (!seq_rollActive[track])
            continue;
        if (seq_rollRemaining[track] == 0u) {
            seq_rollActive[track] = 0u;
            continue;
        }
        ppq = rollPpqTable[seq_rollLastRate[track]];
        if (ppq == 0u) {
            seq_rollActive[track] = 0u;
            continue;
        }
        if (seq_rollTickAccum[track] > 0u) {
            seq_rollTickAccum[track]--;
            continue;
        }
        /* Fire one retrigger. */
        vel = seq_rollComputeVelocity(track);
        seq_rollRemaining[track]--;
        if (seq_rollRemaining[track] == 0u &&
            seq_rollLastFill[track] < 100u) {
            /* Gate trigger: final retrigger at 1/10th decay. */
            seq_triggerVoiceGated(track, vel,
                                 seq_rollStepNote[track]);
        } else {
            seq_triggerVoice(track, vel,
                             seq_rollStepNote[track]);
        }
        seq_rollTickAccum[track] = (uint8_t)(ppq - 1u);
        seq_rollVelPos[track]++;
    }
}
```

### S3.6 — ADD `seq_rollComputeVelocity()` helper

```c
/* Compute the velocity for the current roll retrigger position (S079 §5).
 *
 * What: decode the rhythm byte (bits 7..4 = cycle_length, bit 3 =
 * direction, bits 2..0 = full_count) and return the appropriate velocity
 * based on the current position in the cycle. Within the full_count window,
 * return step velocity. Beyond it, halve (direction=0) or double
 * (direction=1). Halved velocity floors at 1; doubled clamps at 127.
 * Inputs: seq_rollVelPos[track], seq_rollStepVel[track],
 * seq_rollLastRhythm[track]. Output: computed velocity 1..127.
 */
static uint8_t seq_rollComputeVelocity(uint8_t track)
{
    /* ... decode rhythm byte, compute velocity ... */
}
```

### S3.7 — `sequencer.c:2073-2153` — MODIFY `seq_processSchedulerTick()`

Add `seq_processRollTicks()` call after `seq_processShuffleDelays()`:

After line 2107:
```c
    seq_processDeferredTriggers();
    seq_processRollTicks();
```

### S3.8 — `sequencer.c:2713-2780` — MODIFY `seq_setStepIndexToStart()`

Clear the new per-track state arrays in the reset loop (lines 2764-2766):

```c
    seq_trackRepeatCount[i] = 0u;
    seq_trackLastConditionFired[i] = 0u;
    seq_rollActive[i] = 0u;
    seq_rollTickAccum[i] = 0u;
    seq_rollVelPos[i] = 0u;
    seq_rollRemaining[i] = 0u;
    seq_rollLastRate[i] = 0u;
    seq_rollLastRhythm[i] = 0u;
    seq_rollLastFill[i] = 0u;
```

### S3.9 — `sequencer.c:979-1037` — MODIFY `seq_selectActivePattern()`

Clear repeat counters and last-condition flags on Scene change:
```c
    memset(seq_trackRepeatCount, 0, sizeof(seq_trackRepeatCount));
    memset(seq_trackLastConditionFired, 0, sizeof(seq_trackLastConditionFired));
```

Add near line 1017 (after `memset(seq_trackPlayState, ...)`).

### S3.10 — `sequencer.h` near line 115 — ADD extern declaration

```c
/* Fill mode flag, written by buttonHandler PERF hold logic (S079 §3.3).
 *
 * What: nonzero while fill mode is active. Why: the condition evaluator
 * reads this for fil/!fl conditions. Written by foreground buttonHandler,
 * read by TIM3 ISR. Volatile single-byte store/load is atomic on Cortex-M7.
 * Affiliates: seq_evaluateStepCondition(), buttonHandler.c PERF hold.
 */
extern uint8_t seq_fillActive;
```

### S3.11 — ADD `seq_triggerVoiceGated()`

```c
/* Trigger one voice with the gated-decay flag (S079 §6.1, F6).
 *
 * What: identical to seq_triggerVoice() but passes a gated flag to
 * voiceControl_noteOn() so the voice engine applies 1/10th effective decay
 * for this one trigger. Why: the roll fill gate trigger must produce a
 * choked sound to mark the end of the roll region. Inputs: voice, velocity,
 * note. Output: gated synth/MIDI/trigger. Affiliates:
 * seq_processRollTicks(), voiceControl_noteOnGated().
 */
static void seq_triggerVoiceGated(uint8_t voiceNr, uint8_t vol, uint8_t note)
{
    /* Same body as seq_triggerVoice but calls voiceControl_noteOnGated. */
}
```

---

## Step 4 — Voice control gated trigger

### S4.1 — `MidiVoiceControl.h:74` — ADD declaration

```c
/* Gated note-on: identical to voiceControl_noteOn but the voice applies
 * 1/10th effective decay for this trigger cycle (S079 §6.1). Used by the
 * roll fill gate trigger. Inputs: voice 0..6, MIDI note, velocity.
 * Output: enqueued trigger with gated flag. Affiliates: seq_triggerVoiceGated().
 */
void voiceControl_noteOnGated(uint8_t voice, uint8_t note, uint8_t vel);
```

### S4.2 — `MidiVoiceControl.c:187-212` — ADD `voiceControl_noteOnGated()`

Add a parallel function that sets a gated flag before enqueuing the trigger.
The pending trigger struct needs a `gated` field, or the gated flag can be
packed into the velocity byte's MSB (velocity is 0..127, bit 7 is free).

```c
/* Gated note-on implementation (S079).
 *
 * What: enqueue a trigger with bit 7 of the velocity byte set as a gate
 * flag. voiceControl_processPending() strips bit 7 and applies 1/10th decay
 * when set. Why: avoids adding a separate field to the pending trigger
 * struct. The gate flag never reaches the synth directly; it is consumed at
 * dequeue time. Inputs: voice, note, velocity (0..127). Output: enqueued
 * trigger with vel | 0x80 when gated. Affiliates:
 * voiceControl_processPending(), seq_triggerVoiceGated().
 */
void voiceControl_noteOnGated(uint8_t voice, uint8_t note, uint8_t vel)
{
    uint32_t primask;

    if (voice >= 7u)
        return;
    primask = voiceControl_irqSave();
    active_voices |= (1 << voice);
    voiceControl_enqueueTriggerLocked(voice, note, (uint8_t)(vel | 0x80u));
    voiceControl_irqRestore(primask);
}
```

### S4.3 — `MidiVoiceControl.c` in `voiceControl_processPending()` — MODIFY

At the dequeue site, check bit 7 of the velocity byte:
```c
    uint8_t gated = (uint8_t)((vel >> 7u) & 1u);
    vel &= 0x7Fu;
    /* If gated, apply 1/10th decay to the voice's envelope. */
    if (gated)
        voiceControl_applyGatedDecay(voice);
```

### S4.4 — ADD `voiceControl_applyGatedDecay()`

```c
/* Apply 1/10th decay for a gated trigger (S079 §6.1).
 *
 * What: temporarily scale the voice's AmpEnvDecay (or equivalent) by 0.1
 * for the duration of this trigger cycle. The next normal trigger restores
 * the full decay. Why: the roll fill gate trigger must produce a choked
 * sound. Inputs: voice index. Output: voice decay parameter scaled.
 * Affiliates: voiceControl_processPending(), DrumVoice/Snare/HiHat/Cymbal
 * envelope decay parameters.
 */
static void voiceControl_applyGatedDecay(uint8_t voice)
{
    /* Implementation depends on voice type: each voice type stores decay
     * differently. Read the current decay, divide by 10, write it, and
     * mark the voice as gated so the next normal trigger restores it. */
}
```

---

## Step 5 — Button handler fill mode

All changes in `Core/Hardware/frontPanel/buttonHandler.c`.

### S5.1 — ADD fill hold timer

In the static variables section:

```c
/* PERF button fill hold timer (S079 §3.3).
 *
 * What: counts milliseconds since the PERF button was pressed. When it
 * exceeds DOUBLE_CLICK_TIMEOUT, seq_fillActive is set. On release,
 * seq_fillActive is cleared and the timer resets. Why: fill mode must not
 * activate during a normal mode-switch tap. The delay prevents inadvertent
 * fill triggers. Inputs: buttonHandler_tick() 1ms tick. Outputs:
 * seq_fillActive write. RAM: 2 B SRAM1 static. Affiliates:
 * handleModeButtons() PERF arm, processRelease() PERF arm.
 */
static uint16_t bh_fillHoldTimer;
static uint8_t bh_fillArmed;
```

### S5.2 — `buttonHandler.c` in `buttonHandler_tick()` — ADD fill timer tick

```c
    if (bh_fillArmed) {
        bh_fillHoldTimer++;
        if (bh_fillHoldTimer >= DOUBLE_CLICK_TIMEOUT && !seq_fillActive)
            seq_fillActive = 1u;
    }
```

### S5.3 — `buttonHandler.c:1164-1171` — MODIFY PERF mode entry

After `case SELECT_MODE_PERF:`, add fill arm:

```c
    case SELECT_MODE_PERF:
        /* Arm fill hold timer (S079 §3.3). */
        bh_fillArmed = 1u;
        bh_fillHoldTimer = 0u;
        /* ... existing LED/page code ... */
```

### S5.4 — In the PERF button release handler — ADD fill deactivation

Search for the BUT_MODE2 release handler (processRelease). Add:

```c
    if (buttonNr == BUT_MODE2) {
        /* Deactivate fill on PERF release (S079 §3.3). */
        bh_fillArmed = 0u;
        bh_fillHoldTimer = 0u;
        seq_fillActive = 0u;
    }
```

---

## Step 6 — Menu: parameter IDs, dtypes, page layout, display

### S6.1 — `ParameterArray.h:82-83` — MODIFY/ADD step parameter IDs

Old:
```c
    PAR_STEP_PROB,
    PAR_STEP_NOTE,
```

New:
```c
    PAR_STEP_CONDITION,
    PAR_STEP_NOTE,
    PAR_STEP_TIMING,
    PAR_STEP_ROLL,
    PAR_STEP_RHYTHM,
    PAR_STEP_FILL,
```

Remove `PAR_STEP_PROB`. All references to `PAR_STEP_PROB` throughout the
codebase must be updated to `PAR_STEP_CONDITION`.

### S6.2 — `menu.h:296-322` — ADD dtype

```c
    DTYPE_CONDITION,
    DTYPE_ROLL_RATE,
    DTYPE_ROLL_RHYTHM,
```

These three new dtypes format condition labels, roll rate names, and rhythm
`N±M` notation respectively.

### S6.3 — `menu.c:1120-1123` — MODIFY dtype table

Old:
```c
    [PAR_STEP_VOLUME] = DTYPE_0B127,
    [PAR_STEP_PROB] = DTYPE_0B127,
    [PAR_STEP_NOTE] = DTYPE_NOTE_NAME,
```

New:
```c
    [PAR_STEP_VOLUME] = DTYPE_0B127,
    [PAR_STEP_CONDITION] = DTYPE_CONDITION,
    [PAR_STEP_NOTE] = DTYPE_NOTE_NAME,
    [PAR_STEP_TIMING] = DTYPE_0B127,
    [PAR_STEP_ROLL] = DTYPE_ROLL_RATE,
    [PAR_STEP_RHYTHM] = DTYPE_ROLL_RHYTHM,
    [PAR_STEP_FILL] = DTYPE_0B127,
```

### S6.4 — `menu.h` TEXT_ enum — ADD new text IDs

Near line 114, add:
```c
    TEXT_CONDITION,
    TEXT_MICROTIMING,
    TEXT_ROLL_RATE,
    TEXT_ROLL_RHYTHM,
    TEXT_ROLL_FILL,
```

### S6.5 — `MenuText.h` — ADD condition label table

```c
/* Trigger condition display names (S079 §3).
 *
 * What: 37-entry table mapping condition index 0..36 to 3-char LCD labels.
 * Why: the STEP condition cell displays these labels instead of a numeric
 * value. Inputs: condition index from PatternData. Outputs: LCD 3-char
 * string. Affiliates: DTYPE_CONDITION formatter in menu.c.
 */
static const char conditionNames[][4] = {
    {37},
    {"  -"},{"r10"},{"r20"},{"r30"},{"r40"},{"r50"},{"r60"},{"r70"},
    {"r80"},{"r90"},{"r95"},
    {"1:2"},{"2:2"},{"1:3"},{"2:3"},{"3:3"},{"1:4"},{"2:4"},{"3:4"},
    {"4:4"},{"1:-"},
    {"fil"},{"!fl"},{"lst"},{"!ls"},
    {"1mu"},{"2mu"},{"3mu"},{"4mu"},{"5mu"},{"6mu"},
    {"!1m"},{"!2m"},{"!3m"},{"!4m"},{"!5m"},{"!6m"},
};
```

ADD roll rate table:
```c
/* Per-step roll rate display names (S079 §4.1).
 *
 * What: 9-entry table mapping roll index 0..8 to 3-char LCD labels.
 * Affiliates: DTYPE_ROLL_RATE formatter in menu.c.
 */
static const char stepRollRateNames[][4] = {
    {9},
    {"off"},{"1/8"},{"8tr"},{" 16"},{"16t"},{" 32"},{"32t"},{" 48"},{" 64"},
};
```

### S6.6 — `menuPages.h:88-98` — MODIFY SEQ_PAGE subpage layout

Old (subpage 1, line 92):
```c
  {TEXT_STEP_VELOCITY,TEXT_NOTE,TEXT_PROBABILITY,TEXT_EMPTY,..., PAR_STEP_VOLUME,PAR_STEP_NOTE,PAR_STEP_PROB,PAR_NONE,...},
```

New (subpages 1 and 2):
```c
  /* Step specials subpage 1 (S079): vel, note, condition, micro-timing. */
  {TEXT_STEP_VELOCITY,TEXT_NOTE,TEXT_CONDITION,TEXT_MICROTIMING,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY, PAR_STEP_VOLUME,PAR_STEP_NOTE,PAR_STEP_CONDITION,PAR_STEP_TIMING,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE},
  /* Step specials subpage 2 (S079): roll rate, roll rhythm, roll fill. */
  {TEXT_ROLL_RATE,TEXT_ROLL_RHYTHM,TEXT_ROLL_FILL,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY, PAR_STEP_ROLL,PAR_STEP_RHYTHM,PAR_STEP_FILL,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE},
```

This extends SEQ_PAGE from 2 populated subpages to 3.

### S6.7 — `menu.c:15191-15231` — MODIFY menu_parseGlobalParam step cases

Replace `PAR_STEP_PROB` case with `PAR_STEP_CONDITION` and add cases for the
four new step parameters:

```c
    case PAR_STEP_CONDITION:
        menu_broadcastStepSpecial(menu_getViewedPattern(),
                                  menu_getActiveVoice(),
                                  (uint8_t)value, patSvc_setStepCondition);
        break;

    case PAR_STEP_ROLL:
        menu_broadcastStepSpecial(menu_getViewedPattern(),
                                  menu_getActiveVoice(),
                                  (uint8_t)value, patSvc_setStepRoll);
        break;

    case PAR_STEP_TIMING:
        menu_broadcastStepSpecial(menu_getViewedPattern(),
                                  menu_getActiveVoice(),
                                  (uint8_t)value, patSvc_setStepTiming);
        break;

    case PAR_STEP_RHYTHM:
        menu_broadcastStepSpecial(menu_getViewedPattern(),
                                  menu_getActiveVoice(),
                                  (uint8_t)value, patSvc_setStepRhythm);
        break;

    case PAR_STEP_FILL:
        menu_broadcastStepSpecial(menu_getViewedPattern(),
                                  menu_getActiveVoice(),
                                  (uint8_t)value, patSvc_setStepFill);
        break;
```

### S6.8 — `menu.c` display formatter — ADD DTYPE_CONDITION, DTYPE_ROLL_RATE, DTYPE_ROLL_RHYTHM

In the value-display switch (search for `case DTYPE_NOTE_NAME:` and add
nearby):

```c
    case DTYPE_CONDITION:
        /* Display condition label from conditionNames[] table.
         *
         * What: look up the 3-char label by index. Values > 36 display
         * as "-" (always fire). Affiliates: conditionNames[], MenuText.h.
         */
        if (value <= 36u)
            /* format conditionNames[value + 1] */;
        else
            /* format "  -" */;
        break;

    case DTYPE_ROLL_RATE:
        /* Display roll rate label from stepRollRateNames[].
         *
         * Affiliates: stepRollRateNames[], MenuText.h.
         */
        break;

    case DTYPE_ROLL_RHYTHM:
        /* Display rhythm as N±M (e.g., "2-5").
         *
         * What: decode bits 7..4 (cycle_length), bit 3 (direction),
         * bits 2..0 (full_count). Format as "{full}{dir}{cycle}".
         * Value 0 displays "1-1". Affiliates: S079 §5.1.
         */
        break;
```

### S6.9 — `menu.c:12926-12939` — MODIFY pot clear guard

The comment at line 12937 says "SEQ subpage 1 positions 0..2 (velocity, note,
probability) are also STATIC and must stay non-clearable." Update the comment
to reflect the new layout: subpages 1 and 2 positions are step specials and
must stay non-clearable.

---

## Step 7 — Copy/clear

`Core/Menu/CopyClear/copyClearService.c` — all callers of `pat_rawEncode()`
must be updated to pass the new `const pat_step_specials_t *` instead of three
positional args. Search for all `pat_rawEncode(` call sites and update.

The raw decode/encode round-trip in paste operations automatically supports
the wider struct because `pat_rawDecode()` and `pat_rawEncode()` use the
flags byte to determine which bytes to read/write.

---

## Step 8 — Pattern file format version

### S8.1 — `PatternData.h:117` — MODIFY version

Old: `#define PATTERN_FILE_VERSION  1u`

This is the internal version check. Bump to 2 or use a PAT4 format version
field — verify the filesystem reader/writer's version check path.

### S8.2 — `filesystem.c` Pattern reader — MODIFY version guard

Add a version check that rejects old-format files. The reader must reject
PAT4 files that contain the old 3-bit probability encoding.

---

## Step 9 — Converter tool

### S9.1 — ADD `tools/convert_pattern_specials.py`

New Python script. Reads PAT4 v4 files, converts probability bytes to
condition encoding using the mapping in S079 §11, writes PAT4 v5 files.
Keep separate from `tools/convert_scene_scale.py`.

---

## Affected files summary

| File | Changes |
|------|---------|
| `PatternData.h` | S0.1–S0.6 (flags, struct, MAX, encode sig, setter decls) |
| `PatternData.c` | S1.1–S1.18 (block R/W, specials, encode/decode, setters) |
| `PatternStackService.h` | S2.6 (setter decls) |
| `PatternStackService.c` | S2.1–S2.5 (ops, chunks, dispatch, setters) |
| `sequencer.c` | S3.1–S3.11 (state, condition, advance, roll, defer, reset) |
| `sequencer.h` | S3.10 (extern seq_fillActive) |
| `MidiVoiceControl.h` | S4.1 (gated decl) |
| `MidiVoiceControl.c` | S4.2–S4.4 (gated trigger, pending decode, decay) |
| `buttonHandler.c` | S5.1–S5.4 (fill timer, arm, release) |
| `ParameterArray.h` | S6.1 (PAR_STEP_ enum) |
| `menu.h` | S6.2, S6.4 (DTYPE_, TEXT_ enum) |
| `menu.c` | S6.3, S6.7–S6.9 (dtype table, parse, display, clear guard) |
| `MenuText.h` | S6.5 (label tables) |
| `menuPages.h` | S6.6 (SEQ_PAGE subpage layout) |
| `copyClearService.c` | S7 (rawEncode call sites) |
| `config.h` | S0.7–S0.8 (swap chunks) |
| `filesystem.c` | S8.2 (version guard) |
| `tools/convert_pattern_specials.py` | S9.1 (new) |

---

## Build verification

After all changes: `make clean && make`, then `python3 tools/link_budget.py`.
Expected: text growth ~4 KB, BSS growth ~67 B, no linker errors.

Stack budget: `PAT_RAW_BLOCK_MAX` grows by 4 bytes (132 → 136). Every
stack-allocated `uint8_t buf[PAT_RAW_BLOCK_MAX]` grows by 4 B. The deepest
caller is copy/clear paste which already sits well within the stack budget.
Verify with `arm-none-eabi-size` and manual worst-case trace.
