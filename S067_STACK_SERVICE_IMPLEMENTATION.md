# S067 — Pattern Stack Service: Full Code Implementation Schedule

This document is the line-by-line code implementation schedule for Session 067.
Every change is called by file, line, and add/remove/modify. Each entry includes
a comment-block description suitable for documenting the change in-place.

Authority: `S067_STACK_SERVICE_DETAIL_PLAN.md`.

---

## Part B — Pool Usage Monitor Widget

Part B is self-contained, has zero coupling to Part A, and ships first.

### B-1. `config.h` — no changes

`PAT_STACK_SIZE` is already 256 at line 253. The pool usage function uses it
directly.

---

### B-2. `Core/Bank/Scene/Pattern/PatternData.h` — ADD function declaration

**File:** `PatternData.h`
**Location:** after `pat_autosaveSnapshot()` declaration (line 256), before the
closing `#endif`
**Action:** ADD

```c
/*
 * Compute dynamic-pool occupancy for one resident Scene.
 *
 * What: count the set bits in the first PAT_STACK_SIZE bytes of the Scene's
 * bitmap (the backed range = 256 bytes = 2,048 four-byte chunks at current
 * sizing) and return the percentage as 0..99. Why: the settings-menu pool
 * widget needs a one-shot occupancy reading without knowledge of bitmap
 * geometry. 99 means 99-100% — display saturation is intentional. Inputs:
 * validated resident Scene index 0..15. Output: 0..99; invalid index returns
 * zero. Affiliates: menu.c pool-use widget, pat_sceneRegion(),
 * PAT_STACK_SIZE.
 */
uint8_t pat_poolUsagePercent(uint8_t scene_index);
```

---

### B-3. `Core/Bank/Scene/Pattern/PatternData.c` — ADD function definition

**File:** `PatternData.c`
**Location:** after `pat_autosaveSnapshot()` (after line 106), before the
`pat_addrPtr()` static helper
**Action:** ADD

```c
/*
 * Compute dynamic-pool occupancy for one resident Scene.
 *
 * What: count the set bits in the first PAT_STACK_SIZE bytes of the Scene's
 * bitmap (the backed range = 256 bytes = 2,048 four-byte chunks at current
 * sizing), divide by the total backed chunk count (PAT_STACK_SIZE * 8), and
 * return the percentage 0..99. Why: the settings-menu pool widget needs a
 * bounded, alignment-safe one-shot occupancy reading. Uses memcpy to load
 * each 4-byte word from the __packed struct bitmap into a local uint32_t,
 * then applies __builtin_popcount. This avoids an unaligned-access
 * assumption and costs ~256 cycles worst case at menu-entry time. Display
 * saturation: 99 means 99-100% occupancy; the two-digit display never shows
 * 100. Inputs: resident Scene index 0..15. Output: 0..99; invalid index
 * returns zero. Affiliates: menu.c pool-use widget, pat_sceneRegion(),
 * PAT_STACK_SIZE.
 */
uint8_t pat_poolUsagePercent(uint8_t scene_index)
{
    const pat_scene_region_t *r = pat_sceneRegion(scene_index);
    uint32_t used = 0u;
    uint32_t word;
    int i;

    if (!r)
        return 0u;
    for (i = 0; i < (int)(PAT_STACK_SIZE / 4u); i++) {
        memcpy(&word, &r->bitmap[i * 4], 4);
        used += (uint32_t)__builtin_popcount(word);
    }
    {
        uint8_t pct = (uint8_t)((used * 100u) / (PAT_STACK_SIZE * 8u));
        return pct > 99u ? 99u : pct;
    }
}
```

---

### B-4. `Core/Menu/menu.h` — ADD text/short/long/parameter enums

**File:** `menu.h`
**Location:** three separate enum blocks; each addition immediately before the
closing sentinel of its block

**B-4a. NamesEnum** (line ~127, before `TEXT_AUTOSAVE`)
**Action:** ADD one entry

```c
    TEXT_PAT_STORE_USE,
```

Insert between `TEXT_AUTOSAVE` and `NUM_NAMES` — or more precisely, insert
immediately **before** `TEXT_AUTOSAVE` so that `TEXT_AUTOSAVE` remains last
before `NUM_NAMES`. This keeps the existing AutoSave cell stable.

**B-4b. shortNamesEnum** (in `menu.h`, line ~186 area, before the short-name
sentinel/count)
**Action:** ADD one entry

```c
    SHORT_PAT_STORE_USE,
```

**B-4c. longNamesEnum** (in `menu.h`, line ~158 area, before the long-name
sentinel/count)
**Action:** ADD one entry

```c
    LONG_PAT_STORE_USE,
```

**B-4d. Parameter sentinel** (in `menu.h`, near line 194 where
`PAR_RUNTIME_CPU_USE` is defined)
**Action:** ADD

```c
/*
 * Virtual parameter sentinel for the read-only Pattern pool-use widget.
 *
 * What: a non-real parameter id that menu rendering and editing recognize as
 * a widget cell with special display and read-only behavior, parallel to
 * PAR_RUNTIME_CPU_USE. Why: the cell must render its retained percentage
 * instead of looking up a ParameterArray value, and the encoder must ignore
 * edits. Inputs: none; compile-time value only. Output: the sentinel
 * distinguishes pool-use cells from normal parameters in every rendering and
 * editing path that already checks PAR_RUNTIME_CPU_USE. Affiliates:
 * menu_cpuUseWidgetVisible() pattern, menu_formatCpuUsePercent3(),
 * menuPages.h Global subpage 2.
 */
#define PAR_PAT_STORE_USE 0xFFFDu
```

---

### B-5. `Core/Menu/MenuText.h` — ADD short/long/cat name strings

**File:** `MenuText.h`

**B-5a. shortNames array** (line ~143, after the `{"ats"}` entry)
**Action:** ADD

```c
    {"pts"},
```

**B-5b. longNames array** (line ~174, after the `{"AutoSave"}` entry)
**Action:** ADD

```c
    {"Pattern StoreUse"},
```

Note: the 16-character display column can hold "Pattern StoreUse" (16 chars
including the NUL is 17, but the array is `[][16]` which truncates at 15+NUL;
use `"PatternStoreUse"` if it must fit 15 visible characters).

Correct fit: `"PtrnStoreUse"` (12 chars) or simply use the click-in display
helper to render the full title manually, matching the CPU widget.

---

### B-6. `Core/Menu/menu.c` — ADD valueNames entry

**File:** `menu.c`
**Location:** `valueNames[]` array (line ~1117 area), after the
`{SHORT_ATS, CAT_GLOBAL, LONG_ATS}` entry (for TEXT_AUTOSAVE), corresponding
to the newly inserted `TEXT_PAT_STORE_USE` enum position
**Action:** ADD one entry to the `valueNames[]` static initializer at the array
index matching `TEXT_PAT_STORE_USE`:

```c
    {SHORT_PAT_STORE_USE, CAT_PATTERN, LONG_PAT_STORE_USE},
```

The entry must be inserted at the same ordinal position as `TEXT_PAT_STORE_USE`
in the NamesEnum.

---

### B-7. `Core/Menu/menuPages.h` — MODIFY Global subpage 2

**File:** `menuPages.h`
**Location:** line 44 — the Global subpage 2 row (the row beginning
`TEXT_BAR_RESET_MODE`)
**Action:** MODIFY — replace the `TEXT_EMPTY` / `PAR_NONE` pair at position 7
(the last cell on that subpage) with the new widget cell

Current (line 44, position 7):
```
TEXT_AUTOSAVE, TEXT_EMPTY,
...
PAR_AUTOSAVE_ENABLED, PAR_NONE
```

Changed to:
```
TEXT_AUTOSAVE, TEXT_PAT_STORE_USE,
...
PAR_AUTOSAVE_ENABLED, PAR_PAT_STORE_USE
```

This places the pool-use widget at the last cell of Global subpage 2, after
AutoSave.

---

### B-8. `Core/Menu/menu.c` — ADD retained value and compute-on-entry

**File:** `menu.c`
**Location:** near the `menu_cpuUseSamples[]` block (line ~1393)
**Action:** ADD static retained value

```c
/*
 * Retained Pattern pool-use percentage for the settings-menu widget.
 *
 * What: one byte holding the last computed pool occupancy 0..99. Why: the
 * widget renders this retained value on every repaint without recomputing.
 * Unlike the CPU widget, there is no rolling average, no periodic refresh,
 * and no sample array. The value is computed once when the Global settings
 * page is entered and is not updated during the menu session. Inputs:
 * pat_poolUsagePercent() at page entry. Output: read by the widget format
 * helpers. Affiliates: menu_patStoreUseWidgetVisible(),
 * menu_formatPatStoreUsePercent3(), menu_formatPatStoreUsePercent4().
 * RAM: +1 byte SRAM1 .bss.
 */
static uint8_t menu_patStoreUsePercent = 0u;
```

---

### B-9. `Core/Menu/menu.c` — ADD widget visibility predicate

**File:** `menu.c`
**Location:** after `menu_cpuUseWidgetVisible()` (line ~9814)
**Action:** ADD

```c
/*
 * Report whether the Pattern pool-use widget cell is currently visible.
 *
 * What: true when the Global page is active, and either the cursor is on the
 * pool-use cell in edit mode or the cell is visible on the current subpage.
 * Why: the widget must repaint its retained value instead of using the
 * standard parameter formatter, and the encoder must refuse edits. Modeled
 * on menu_cpuUseWidgetVisible(). Inputs: menu_activePage, menuIndex,
 * editModeActive, and the cell's static_param == PAR_PAT_STORE_USE. Output:
 * nonzero when the pool-use widget is on screen. Affiliates: menu_repaint(),
 * menu_encoderChangeParameter(), menu_endlessPotHandler(), PAR_PAT_STORE_USE.
 */
static uint8_t menu_patStoreUseWidgetVisible(void)
{
    uint8_t activePage = (uint8_t)((menuIndex & MASK_PAGE) >> PAGE_SHIFT);
    uint8_t activeParameter = menuIndex & MASK_PARAMETER;

    if (menu_activePage != MENU_MIDI_PAGE)
        return 0;
    if (editModeActive) {
        menu_cell_t cell = menu_resolveCell(activePage, activeParameter);
        return (uint8_t)(cell.kind == MENU_CELL_STATIC &&
                         cell.static_param == PAR_PAT_STORE_USE);
    }
    return (uint8_t)(activePage == 1u && activeParameter >= 6u);
}
```

---

### B-10. `Core/Menu/menu.c` — ADD format helpers

**File:** `menu.c`
**Location:** after `menu_formatCpuUsePercent4()` (line ~6753)
**Action:** ADD

```c
/*
 * Format the retained pool-use percentage into a 3-character buffer.
 *
 * What: right-justified "NN" or " N" with space padding, matching the CPU
 * widget's row-view format. Why: the Global subpage row renderer calls this
 * for the pool-use cell instead of the standard value formatter. Inputs:
 * menu_patStoreUsePercent (0..99). Output: three characters written to buf.
 * Affiliates: menu_repaintGeneric() PAR_PAT_STORE_USE branch.
 */
static void menu_formatPatStoreUsePercent3(char *buf)
{
    uint8_t pct = menu_patStoreUsePercent;
    if (pct > 99u) pct = 99u;
    numtostrpu(buf, pct, ' ');
}

/*
 * Format the retained pool-use percentage into a 4-character buffer with '%'.
 *
 * What: right-justified "NN%" with space padding, for the click-in edit
 * display. Why: the click-in view shows the full "Pattern StoreUse" title
 * with the percentage in the bottom-right corner. Inputs:
 * menu_patStoreUsePercent (0..99). Output: four characters written to buf.
 * Affiliates: menu_displayPatStoreUseEdit().
 */
static void menu_formatPatStoreUsePercent4(char *buf)
{
    uint8_t pct = menu_patStoreUsePercent;
    if (pct > 99u) pct = 99u;
    numtostrpu(buf, pct, ' ');
    buf[3] = '%';
}
```

---

### B-11. `Core/Menu/menu.c` — ADD click-in display helper

**File:** `menu.c`
**Location:** after `menu_displayCpuUseEdit()` (line ~6912)
**Action:** ADD

```c
/*
 * Render the click-in edit view for the Pattern pool-use widget.
 *
 * What: display "Pattern StoreUse" on the top line and the retained
 * percentage with '%' suffix on the bottom right, matching the CPU widget's
 * click-in layout. Why: clicking into the pool-use cell shows its long name
 * and current value without any editable parameter. Inputs:
 * menu_patStoreUsePercent. Output: editDisplayBuffer[0..1] are fully
 * formatted. Affiliates: menu_displayEdit() PAR_PAT_STORE_USE branch.
 */
static void menu_displayPatStoreUseEdit(void)
{
    static const char title[] = "Pattern StoreUse";
    uint8_t i;

    memset(&editDisplayBuffer[0][0], ' ', 16);
    memset(&editDisplayBuffer[1][0], ' ', 16);

    for (i = 0; i < sizeof(title) - 1u && i < 16u; i++)
        editDisplayBuffer[0][i] = title[i];
    menu_formatPatStoreUsePercent4(&editDisplayBuffer[1][12]);
}
```

---

### B-12. `Core/Menu/menu.c` — MODIFY rendering paths for PAR_PAT_STORE_USE

Every existing rendering and editing path that checks `PAR_RUNTIME_CPU_USE`
must also check `PAR_PAT_STORE_USE`. The locations are:

**B-12a. Click-in edit dispatch** (line ~8396)
**Action:** MODIFY — add a parallel branch

After:
```c
        if (cell.kind == MENU_CELL_STATIC &&
            cell.static_param == PAR_RUNTIME_CPU_USE) {
            menu_displayCpuUseEdit();
            return;
        }
```

ADD immediately after:
```c
        if (cell.kind == MENU_CELL_STATIC &&
            cell.static_param == PAR_PAT_STORE_USE) {
            menu_displayPatStoreUseEdit();
            return;
        }
```

**B-12b. Row value formatter** (line ~8582)
**Action:** MODIFY — add a parallel branch

After:
```c
            } else if (cell.kind == MENU_CELL_STATIC &&
                       cell.static_param == PAR_RUNTIME_CPU_USE) {
                menu_formatCpuUsePercent3(valueAsText);
```

ADD:
```c
            } else if (cell.kind == MENU_CELL_STATIC &&
                       cell.static_param == PAR_PAT_STORE_USE) {
                menu_formatPatStoreUsePercent3(valueAsText);
```

**B-12c. Encoder-change guard** (line ~8650)
**Action:** MODIFY — extend the read-only guard

Change:
```c
    if (cell.kind == MENU_CELL_STATIC &&
        cell.static_param == PAR_RUNTIME_CPU_USE)
        return;
```

To:
```c
    if (cell.kind == MENU_CELL_STATIC &&
        (cell.static_param == PAR_RUNTIME_CPU_USE ||
         cell.static_param == PAR_PAT_STORE_USE))
        return;
```

**B-12d. Endless-pot guard** (line ~9743)
**Action:** MODIFY — extend the read-only guard

Change:
```c
    if (cell.kind == MENU_CELL_STATIC &&
        cell.static_param == PAR_RUNTIME_CPU_USE) return;
```

To:
```c
    if (cell.kind == MENU_CELL_STATIC &&
        (cell.static_param == PAR_RUNTIME_CPU_USE ||
         cell.static_param == PAR_PAT_STORE_USE)) return;
```

---

### B-13. `Core/Menu/menu.c` — ADD compute-on-entry hook

**File:** `menu.c`
**Location:** the Global settings page entry path. Find where
`menu_activePage` is set to `MENU_MIDI_PAGE` or where Global page entry
initializes state. The retained value must be computed once at this transition.
**Action:** ADD at the Global page entry point

```c
    /*
     * Snapshot pool occupancy once on Global page entry.
     *
     * What: compute the active Scene's dynamic-pool usage and retain it for
     * the duration of the menu session. Why: pattern editing and scene changes
     * do not occur while the settings menu is accessed, so a single reading
     * is sufficient and avoids any periodic recomputation cost. Inputs:
     * seq_activePattern (the currently playing/viewed Scene). Output:
     * menu_patStoreUsePercent is set to 0..99. Affiliates:
     * pat_poolUsagePercent(), menu_formatPatStoreUsePercent3().
     */
    menu_patStoreUsePercent = pat_poolUsagePercent(seq_activePattern);
```

---

### B-14. Forward declarations

**File:** `menu.c`
**Location:** in the forward-declaration block (line ~1547 area)
**Action:** ADD

```c
static uint8_t menu_patStoreUseWidgetVisible(void);
static void menu_formatPatStoreUsePercent3(char *buf);
static void menu_formatPatStoreUsePercent4(char *buf);
static void menu_displayPatStoreUseEdit(void);
```

---

## Part A — Unified Pattern Stack Service

### Gate 1 — Correct Publication Ordering

#### G1-1. `Core/Bank/Scene/Pattern/PatternData.c` — MODIFY `pat_writeDynamic()`

**File:** `PatternData.c`
**Location:** lines 444–528, the `pat_writeDynamic()` function
**Action:** MODIFY — fix the publication ordering bug

The current code (lines 522–528) performs:
1. Write new block at `new_offset`
2. Free old block at `old_offset`  (line 525)
3. Publish new address to `*entry`  (line 526)

Between steps 2 and 3, TIM3 can preempt and read the old address, finding
zeroed pool bytes. The fix reorders to:

1. Write new block at `new_offset` (already correct, line 522)
2. Re-read trigger bit under short PRIMASK, compose new address with latest
   trigger + SPECIALS_BIT + new_offset, publish as a single aligned STRH
3. Free old block only after publication

**Replacement for lines 522–528:**

```c
    /*
     * Publish-then-free: write the complete new block, atomically swap the
     * address entry to point at it, then reclaim the old allocation.
     *
     * What: the new block is fully written before the address entry changes.
     * A short PRIMASK section re-reads the trigger bit (bit 15) from the live
     * entry so that a concurrent pat_setStepActive() or pat_toggleStep() from
     * TIM3 between the block-write and the address-swap is not silently
     * overwritten. The critical section is one load, one OR/AND, one store —
     * well under 1 us at 216 MHz. Why: TIM3 reads the 16-bit address entry
     * atomically (aligned halfword on Cortex-M7) and then dereferences the
     * pool offset. It must always see either the complete old block or the
     * complete new block, never zeroed pool bytes from a freed-but-not-yet-
     * replaced allocation. Inputs: new_offset with complete block content;
     * old_offset identifying the old allocation to free. Output: the address
     * entry carries the latest trigger bit, PAT_ADDR_SPECIALS_BIT, and
     * new_offset; the old allocation is returned to the bitmap. Affiliates:
     * pat_poolAlloc(), pat_poolFree(), TIM3 seq_advanceTrackStep() halfword
     * read at sequencer.c:517.
     */
    pat_blockWrite(r, new_offset, track, step, new_flags, note, velocity,
                   probability, autos, auto_count);
    {
        uint16_t published;
        __asm volatile("cpsid i" ::: "memory");
        trigger_bits = (uint16_t)(*entry & PAT_ADDR_TRIGGER_BIT);
        published = (uint16_t)(trigger_bits | PAT_ADDR_SPECIALS_BIT |
                               new_offset);
        *entry = published;
        __asm volatile("cpsie i" ::: "memory");
    }
    if (pat_poolOffsetValid(old_offset))
        pat_poolFree(r, old_offset, old_chunks);
    pat_markSceneDirty(scene_index);
    return 1u;
```

Additionally, the same-size in-place rewrite fast path (lines 491–498) must
be **removed** per the plan (Option A: always write-new/swap/free-old). Remove
the `if (old_chunks == new_chunks)` block entirely so same-size updates fall
through to the disjoint allocation path.

The `allow_shrink_in_place` path (lines 508–518) is retained because it is a
removal-only shrink that does not rewrite existing content — it writes the
*compacted* (smaller) block at the same base and frees only the tail chunks.
However, it also needs the PRIMASK trigger-bit re-read for the address
publication:

**Modify the shrink-in-place address write (line 514):**

Replace:
```c
            *entry = (uint16_t)(trigger_bits | PAT_ADDR_SPECIALS_BIT |
                                old_offset);
```

With:
```c
            {
                uint16_t published;
                __asm volatile("cpsid i" ::: "memory");
                trigger_bits = (uint16_t)(*entry & PAT_ADDR_TRIGGER_BIT);
                published = (uint16_t)(trigger_bits | PAT_ADDR_SPECIALS_BIT |
                                       old_offset);
                *entry = published;
                __asm volatile("cpsie i" ::: "memory");
            }
```

The "clear block" path (lines 473–484, `new_flags == 0 && auto_count == 0`)
already publishes `trigger_bits | PAT_ADDR_SENTINEL`. This path also needs
the re-read:

**Modify lines 479–481:**

Replace:
```c
        *entry = (uint16_t)(trigger_bits | PAT_ADDR_SENTINEL);
```

With:
```c
        {
            uint16_t published;
            __asm volatile("cpsid i" ::: "memory");
            trigger_bits = (uint16_t)(*entry & PAT_ADDR_TRIGGER_BIT);
            published = (uint16_t)(trigger_bits | PAT_ADDR_SENTINEL);
            *entry = published;
            __asm volatile("cpsie i" ::: "memory");
        }
```

And move `pat_poolFree()` **before** this address write (it's already before
in the current code — verify the free happens at line 479 and the address at
481; the order here is free-then-detach which is wrong). Correct order:
detach-address-first, then free. So the block becomes:

```c
    if (new_flags == 0u && auto_count == 0u) {
        uint16_t published;
        uint8_t old_ch = 0u;
        uint16_t old_off = old_offset;
        if (pat_poolOffsetValid(old_off)) {
            old_ch = pat_blockChunks(
                r->pool[old_off + 2u],
                (uint8_t)(r->pool[old_off + 1u] &
                          PAT_BLOCK_AUTO_COUNT_MASK));
        }
        __asm volatile("cpsid i" ::: "memory");
        trigger_bits = (uint16_t)(*entry & PAT_ADDR_TRIGGER_BIT);
        published = (uint16_t)(trigger_bits | PAT_ADDR_SENTINEL);
        *entry = published;
        __asm volatile("cpsie i" ::: "memory");
        if (pat_poolOffsetValid(old_off))
            pat_poolFree(r, old_off, old_ch);
        pat_markSceneDirty(scene_index);
        return 1u;
    }
```

---

#### G1-2. `Core/Bank/Scene/Pattern/PatternData.c` — MODIFY `pat_eraseStep()`

**File:** `PatternData.c`
**Location:** lines 685–713, the `pat_eraseStep()` function
**Action:** MODIFY — detach address first, then free

Current code frees pool bytes (line 709) before writing `PAT_ADDR_SENTINEL`
(line 711). Fix: write sentinel first under PRIMASK, then free.

```c
/*
 * Erase one step's complete address state with safe publication ordering.
 *
 * What: atomically detach the address entry (write PAT_ADDR_SENTINEL with
 * latest trigger bit preserved under PRIMASK), then free the captured old
 * pool allocation using the locally saved offset. Why: TIM3 may preempt
 * between the free and the address write; reading an old address after
 * pool bytes are zeroed produces corrupt playback. The detach-first order
 * ensures the sequencer sees either the old complete block (before the
 * detach) or the sentinel (after), never zeroed bytes. Inputs: bounded
 * Scene/track/step coordinates. Output: the step has no trigger, no
 * specials, and no pool block; the old allocation is returned to the
 * bitmap. Affiliates: pat_poolFree(), sequencer.c TIM3 live erase path
 * (future: queue event instead of direct call).
 */
void pat_eraseStep(uint8_t scene_index, uint8_t track, uint8_t step)
{
    uint16_t *entry = pat_addrPtr(scene_index, track, step);
    uint16_t addr;
    uint16_t offset;

    if (!entry)
        return;
    addr = *entry;
    offset = (uint16_t)(addr & PAT_ADDR_OFFSET_MASK);

    /* Detach: write sentinel while preserving the latest trigger bit. */
    *entry = PAT_ADDR_SENTINEL;

    /* Free the captured old allocation after detachment. */
    if (pat_poolOffsetValid(offset)) {
        pat_scene_region_t *r = &pat_regions[scene_index];
        uint8_t chunks = pat_blockChunks(r->pool[offset + 2u],
                                         (uint8_t)(r->pool[offset + 1u] &
                                                   PAT_BLOCK_AUTO_COUNT_MASK));
        pat_poolFree(r, offset, chunks);
    }
    pat_markSceneDirty(scene_index);
}
```

Note: `pat_eraseStep()` unconditionally clears the trigger bit (writing
`PAT_ADDR_SENTINEL` which has bit 15 = 0). This is intentional — erase means
full destructive clear, unlike toggle which preserves bits 14..0.

---

### Gate 2 — Service Dispatcher and Foreground Routing

#### G2-1. NEW FILE: `Core/Bank/Scene/Pattern/PatternStackService.h`

**Action:** CREATE

```c
/*
 * PatternStackService.h — unified pool mutation dispatcher.
 *
 * What: the public service API that routes all dynamic-pool mutations
 * through one private executor. Why: ISR-originated pool work (live erase)
 * and background maintenance (gap creation, compaction) require deferred
 * execution, while foreground menu edits can execute synchronously when the
 * service is idle. One dispatcher owns admission, scene guards, queue drain,
 * and idle reporting for AutoSave coordination. Inputs: Scene/track/step
 * coordinates, target IDs, and special values from menu.c, sequencer.c,
 * copyClearTools.c, and EuklidGenerator.c. Output: nonzero on synchronous
 * success or optimistic queued acceptance; zero on rejection (wrong scene
 * or service closed). Affiliates: PatternData.c internal pool functions,
 * sequencer.c TIM3 live erase, filesystem.c autosave guard.
 */
#ifndef PATTERN_STACK_SERVICE_H_
#define PATTERN_STACK_SERVICE_H_

#include <stdint.h>
#include "PatternData.h"

/* Service lifecycle. */
void patSvc_init(void);
void patSvc_tick(void);

/*
 * Service idle query for AutoSave coordination.
 *
 * What: true when the queue is empty, no bulk cursor is active, no
 * relocation is in progress, and no handover is pending. Why: the
 * autosave scheduler must defer Pattern snapshots until deferred
 * pool work has drained. Inputs: internal service state. Output:
 * nonzero when the service scene is stable. Affiliates:
 * filesystem.c autosave pattern drain guard.
 */
uint8_t patSvc_idle(void);

/* Foreground mutation API — replaces direct pat_* calls from menu.c. */
uint8_t patSvc_writeStepAutomation(uint8_t scene, uint8_t track,
                                   uint8_t step, uint16_t target,
                                   uint8_t value);
uint8_t patSvc_removeStepAutomation(uint8_t scene, uint8_t track,
                                    uint8_t step, uint16_t target);
uint8_t patSvc_setStepNote(uint8_t scene, uint8_t track, uint8_t step,
                           uint8_t value);
uint8_t patSvc_setStepVolume(uint8_t scene, uint8_t track, uint8_t step,
                             uint8_t value);
uint8_t patSvc_setStepProbability(uint8_t scene, uint8_t track,
                                  uint8_t step, uint8_t value);
void patSvc_eraseStep(uint8_t scene, uint8_t track, uint8_t step);
void patSvc_clearTrack(uint8_t scene, uint8_t track);
void patSvc_clearPattern(uint8_t scene);
uint8_t patSvc_removeTrackAutomationByTarget(uint8_t scene, uint8_t track,
                                             uint16_t target);

/* TIM3 queue entry for live erase (ISR context). */
void patSvc_enqueueErase(uint8_t scene, uint8_t track, uint8_t step);

#endif /* PATTERN_STACK_SERVICE_H_ */
```

---

#### G2-2. NEW FILE: `Core/Bank/Scene/Pattern/PatternStackService.c`

**Action:** CREATE

This file contains all service state, the queue, the dispatcher, bulk
barriers, handover, gap maintenance, and compaction. The full file is large;
this schedule specifies its structure and every function/variable with
descriptions. The implementation order follows the Gates.

**Static state (Gate 2):**

```c
/*
 * Current mutation target Scene.
 *
 * What: the Scene index whose pool the service is permitted to mutate.
 * Why: exactly one Scene may have deferred work at a time; edits for other
 * Scenes are rejected. Initialized to seq_activePattern at boot; updated
 * by the handover state machine when seq_activePattern changes. Inputs:
 * patSvc_init() and the Gate 5 handover. Output: every admission check
 * compares the requested scene against this value. Affiliates:
 * seq_activePattern, patSvc_tick() priority 0.
 */
static uint8_t service_scene;

/*
 * Admission gate for the service.
 *
 * What: nonzero when the service accepts new queue events and foreground
 * edits. Why: the gate is closed during mutation-target handover (Gate 5)
 * and during filesystem bulk replacement. Inputs: patSvc_init() opens it;
 * handover closes/reopens it. Output: zero causes immediate rejection of
 * offered edits. Affiliates: patSvc_writeStepAutomation() and siblings.
 */
static uint8_t service_open;
```

**Public routing stubs (Gate 2) — each follows this pattern:**

```c
uint8_t patSvc_writeStepAutomation(uint8_t scene, uint8_t track,
                                   uint8_t step, uint16_t target,
                                   uint8_t value)
{
    /*
     * Route one step-automation write through the service dispatcher.
     *
     * What: if the service is open, the scene matches, and no queued work
     * is pending, execute synchronously through the existing
     * pat_writeStepAutomation(). Otherwise enqueue the event for the 500 Hz
     * drain. Why: foreground edits must share the single-owner pool mutation
     * contract with ISR-originated and background work. Inputs: Scene/track/
     * step/target/value from menu.c. Output: nonzero on synchronous success
     * or optimistic queued acceptance; zero on rejection. Affiliates:
     * pat_writeStepAutomation(), patSvc_enqueue().
     */
    if (!service_open || scene != service_scene)
        return 0u;
    if (patSvc_idleForDirectWrite())
        return pat_writeStepAutomation(scene, track, step, target, value);
    return patSvc_enqueue(/* WRITE_AUTOMATION event */);
}
```

Repeat for each of the public API functions listed in the header.

---

#### G2-3. `Core/Sequencer/sequencer.h` — ADD per-track pattern stub

**File:** `sequencer.h`
**Location:** after `extern uint8_t seq_eraseActive;` (line ~39)
**Action:** ADD

```c
/*
 * Per-track Scene/Pattern assignment stub.
 *
 * What: seven bytes, one per track, each holding a resident Scene index.
 * Why: all entries are initialized to seq_activePattern at boot and on scene
 * switch. No per-track assignment logic exists yet — all tracks always play
 * the active Scene. The array exists so the data path is in place for future
 * per-track pattern assignment (can reference the hidden 17th scene). RAM:
 * +7 bytes SRAM1. Inputs: seq_init(), seq_selectActivePattern(). Output:
 * read by the sequencer step-advance path. Affiliates: Phase 5.5 per-track
 * scene assignment.
 */
extern uint8_t seq_perTrackPattern[7];
```

---

#### G2-4. `Core/Sequencer/sequencer.c` — ADD per-track pattern array

**File:** `sequencer.c`
**Location:** near other `seq_*` globals
**Action:** ADD definition and initialization

```c
uint8_t seq_perTrackPattern[7];
```

In `seq_init()`, add:
```c
    for (uint8_t t = 0; t < 7u; t++)
        seq_perTrackPattern[t] = seq_activePattern;
```

In `seq_selectActivePattern()` and `seq_alignActivePatternToScene()`, add the
same loop to realign all entries.

---

#### G2-5. `Core/Hardware/timebase.c` — MODIFY `timebase_serviceFrontPanel()`

**File:** `timebase.c`
**Location:** end of `timebase_serviceFrontPanel()` (after line 187,
`endlessPots_tick()`)
**Action:** ADD the service tick call

```c
    /*
     * Pattern stack service tick at the 500 Hz foreground cadence.
     *
     * What: one additional function call per 2 ms foreground pass, after the
     * LED/button SPI exchange and encoder/pot ticks. Why: the service drains
     * queued edit events, advances bulk barriers, runs gap maintenance, and
     * performs compaction at this bounded cadence. Inputs: internal service
     * state. Output: at most one mutation per tick for queue/bulk work; at
     * most one relocation per tick for maintenance. Affiliates:
     * PatternStackService.c.
     */
    patSvc_tick();
```

Add `#include "PatternStackService.h"` at the top of `timebase.c`.

---

#### G2-6. `Core/Hardware/SD/filesystem.c` — MODIFY autosave guard

**File:** `filesystem.c`
**Location:** `filesystem_autosavePatternDrainSchedule_tick()` (line ~24075)
**Action:** MODIFY — add `patSvc_idle()` to the existing guard

After line 24075:
```c
    if (seq_recordActive || seq_eraseActive)
        return;
```

ADD:
```c
    /*
     * Defer Pattern AutoSave while the stack service has deferred work.
     *
     * What: the pattern drain must not snapshot the live region while queued
     * edits, bulk barriers, or relocations are pending. Why: a non-idle
     * service scene has in-flight pool mutations that would produce an
     * inconsistent snapshot. The service drains bounded per tick, so this
     * guard cannot remain true forever. Inputs: patSvc_idle(). Output:
     * the drain is deferred until the service is quiescent. Affiliates:
     * PatternStackService.c, pat_snapshotScene().
     */
    if (!patSvc_idle())
        return;
```

Add `#include "PatternStackService.h"` at the top of `filesystem.c`.

---

#### G2-7. Caller migration — MODIFY call sites from `pat_*` to `patSvc_*`

Every direct pool-mutation call in menu.c, copyClearTools.c, EuklidGenerator.c,
and sequencer.c must change its call target. The complete list:

| File | Line | Current call | New call |
|------|------|-------------|----------|
| `menu.c` | 2370 | `pat_writeStepAutomation(...)` | `patSvc_writeStepAutomation(...)` |
| `menu.c` | 7915 | `pat_writeStepAutomation(...)` | `patSvc_writeStepAutomation(...)` |
| `menu.c` | 7917 | `pat_removeStepAutomation(...)` | `patSvc_removeStepAutomation(...)` |
| `menu.c` | 7920 | `pat_removeStepAutomation(...)` | `patSvc_removeStepAutomation(...)` |
| `menu.c` | 7922 | `pat_writeStepAutomation(...)` | `patSvc_writeStepAutomation(...)` |
| `menu.c` | 7924 | `pat_writeStepAutomation(...)` | `patSvc_writeStepAutomation(...)` |
| `menu.c` | 7973 | `pat_writeStepAutomation(...)` | `patSvc_writeStepAutomation(...)` |
| `menu.c` | 8108 | `pat_writeStepAutomation(...)` | `patSvc_writeStepAutomation(...)` |
| `menu.c` | 8161 | `pat_removeTrackAutomationByTarget(...)` | `patSvc_removeTrackAutomationByTarget(...)` |
| `menu.c` | 8166 | `pat_removeStepAutomation(...)` | `patSvc_removeStepAutomation(...)` |
| `menu.c` | 11498 | `pat_setStepProbability(...)` | `patSvc_setStepProbability(...)` |
| `menu.c` | 11507 | `pat_setStepNote(...)` | `patSvc_setStepNote(...)` |
| `menu.c` | 11516 | `pat_setStepVolume(...)` | `patSvc_setStepVolume(...)` |
| `copyClearTools.c` | 63 | `pat_clearPattern(pattern)` | `patSvc_clearPattern(pattern)` |
| `copyClearTools.c` | 128 | `pat_clearTrack(pattern, voice)` | `patSvc_clearTrack(pattern, voice)` |
| `copyClearTools.c` | 157 | `pat_copyTrack(...)` | no change (copy is a no-op stub) |
| `copyClearTools.c` | 183 | `pat_copyPattern(...)` | no change (copy is a no-op stub) |
| `copyClearTools.c` | 203 | `pat_copyBar(...)` | no change (copy is a no-op stub) |
| `EuklidGenerator.c` | 283 | `pat_clearTrack(...)` | `patSvc_clearTrack(...)` |
| `sequencer.c` | 519 | `pat_eraseStep(...)` | **Gate 3: becomes queue event** |

Add `#include "PatternStackService.h"` to: `menu.c`, `copyClearTools.c`,
`EuklidGenerator.c`. `sequencer.c` needs it only at Gate 3.

**Static trigger-bit operations remain unchanged:**
- `pat_setStepActive()` — called from sequencer.c, EuklidGenerator.c, menu.c
- `pat_toggleStep()` — called from menu.c button handlers
- `pat_isStepActive()` — called from sequencer.c, menu.c

These are direct static-array operations per plan constraint #4 and do not
route through the service.

---

### Gate 3 — Edit Event Queue and TIM3 Handoff

#### G3-1. `Core/Bank/Scene/Pattern/PatternStackService.c` — ADD queue

**Action:** ADD to `PatternStackService.c`

```c
/*
 * 64-entry SPSC ring buffer for deferred pool mutations.
 *
 * What: a compact 4-byte-per-entry queue holding operation type, step
 * identity, and packed payload. Why: TIM3-originated pool work (live erase)
 * and foreground edits that arrive while the service is busy are queued for
 * drain at 500 Hz. The queue is SPSC: foreground or TIM3 produces under
 * PRIMASK; the foreground service tick consumes. Unsigned distance
 * prod - cons identifies count; array index uses cursor & 63. All 64
 * entries are usable (no empty-slot convention). Inputs: patSvc_enqueue()
 * from foreground or ISR. Output: patSvc_tick() drains through the private
 * executor. RAM: 256 bytes .bss (64 x 4B) + 2 bytes cursors. Affiliates:
 * PatternTrace overflow recording.
 */
static uint32_t service_queue[64];
static uint8_t  service_queue_prod;
static uint8_t  service_queue_cons;
```

**Queue publication function:**

```c
/*
 * Enqueue one pool mutation event under PRIMASK protection.
 *
 * What: write a 4-byte event into the ring and advance the producer cursor
 * while interrupts are disabled. Why: TIM3 live erase and foreground edits
 * share the producer side; the PRIMASK section excludes concurrent access.
 * The critical section is at most ~5 instructions — read distance,
 * conditional store, increment — well under 1 us at 216 MHz. No pool scan,
 * allocation, or block copy ever runs with interrupts disabled. Inputs:
 * packed 4-byte event word. Output: nonzero on success; zero on buffer
 * overflow (traced, event dropped). Affiliates: PatternTrace.
 */
static uint8_t patSvc_enqueue(uint32_t event)
{
    uint8_t dist;
    __asm volatile("cpsid i" ::: "memory");
    dist = (uint8_t)(service_queue_prod - service_queue_cons);
    if (dist >= 64u) {
        __asm volatile("cpsie i" ::: "memory");
        patternTrace_record(PAT_TRACE_STAGE_QUEUE_OVERFLOW, 0u,
                            (uint32_t)event);
        return 0u;
    }
    service_queue[service_queue_prod & 63u] = event;
    service_queue_prod++;
    __asm volatile("cpsie i" ::: "memory");
    return 1u;
}
```

---

#### G3-2. `Core/Bank/Scene/Pattern/PatternTrace.h` — ADD trace stage codes

**File:** `PatternTrace.h`
**Location:** inside the `pat_trace_stage_t` enum (line ~39)
**Action:** ADD new stages

```c
    PAT_TRACE_STAGE_QUEUE_OVERFLOW    = 'Q',
    PAT_TRACE_STAGE_CAPACITY_DROP     = 'C',
    PAT_TRACE_STAGE_FRAG_DROP         = 'F',
    PAT_TRACE_STAGE_TIER2_RELOC       = 'R',
    PAT_TRACE_STAGE_TIER1_GAP         = 'M',
    PAT_TRACE_STAGE_GAP_FALLBACK      = 'G',
    PAT_TRACE_STAGE_WRONG_SCENE       = 'X',
```

---

#### G3-3. `Core/Sequencer/sequencer.c` — MODIFY TIM3 live erase

**File:** `sequencer.c`
**Location:** `seq_advanceTrackStep()` (lines 518–521)
**Action:** MODIFY — replace direct `pat_eraseStep()` with queue event

Current:
```c
                if (seq_eraseActive && track == menu_getActiveVoice()) {
                    pat_eraseStep(seq_activePattern,
                                  menu_getActiveVoice(),
                                  (uint8_t)seq_stepIndex[track]);
```

Changed to:
```c
                /*
                 * Live erase: immediate trigger-bit clear, deferred pool
                 * reclamation.
                 *
                 * What: the trigger bit is cleared immediately as a static
                 * array operation (safe from ISR context), and a
                 * DELETE_DYNAMIC queue event is published for foreground pool
                 * reclamation. Why: TIM3 must not mutate pool/bitmap state
                 * under S067. The sequencer has already stopped playing the
                 * step once the trigger bit is clear; pool reclamation is
                 * deferred to the service tick. Inputs: active scene, track,
                 * step. Output: trigger bit clear immediately; pool freed on
                 * next service drain. Affiliates: PatternStackService.c.
                 */
                if (seq_eraseActive && track == menu_getActiveVoice()) {
                    pat_setStepActive(seq_activePattern, track,
                                      (uint8_t)seq_stepIndex[track], 0u);
                    patSvc_enqueueErase(seq_activePattern, track,
                                        (uint8_t)seq_stepIndex[track]);
```

Add `#include "PatternStackService.h"` to `sequencer.c`.

---

### Gate 4 — Bulk Operations as Bounded Barriers

#### G4-1. `Core/Bank/Scene/Pattern/PatternStackService.c` — ADD bulk state

```c
/*
 * Bulk barrier state for track-clear and track-wide target removal.
 *
 * What: when a bulk barrier event reaches the queue head, the service
 * enters cursor-driven drain mode and processes a bounded number of steps
 * per tick (8 for track clear, 8 for target removal). Why: converting 128
 * synchronous loop iterations into bounded per-tick work prevents foreground
 * stalls and allows interleaving with scene-switch handover. Inputs: the
 * barrier event type and parameters. Output: the cursor advances until the
 * operation completes, then the barrier is consumed and the queue resumes.
 * RAM: 6 bytes .bss. Affiliates: patSvc_clearTrack(), Euclidean compat.
 */
static uint8_t  bulk_op;
static uint8_t  bulk_track;
static uint16_t bulk_target;
static uint8_t  bulk_step_cursor;
static uint8_t  bulk_track_cursor;
```

---

#### G4-2. Euclidean compatibility

**Verified safe:** `euklid_generate()` at `EuklidGenerator.c:283` calls
`patSvc_clearTrack()` then immediately calls `pat_setStepActive()`. Under
the barrier protocol: the service immediately clears all trigger bits (via the
barrier enqueue path), then the Euclidean `pat_setStepActive()` calls
immediately set the new trigger bits. The barrier drain later reclaims old pool
blocks, re-reading trigger bits at execution time so new triggers survive.

---

### Gate 5 — Mutation-Target Handover

#### G5-1. `Core/Bank/Scene/Pattern/PatternStackService.c` — ADD handover

The handover is the highest-priority item in `patSvc_tick()`. When
`seq_activePattern != service_scene`, the tick advances a small state machine:
close admission, drain queue, complete bulk, switch target, recount pool,
reopen admission.

```c
/*
 * Automatic mutation-target changeover state machine.
 *
 * What: when seq_activePattern changes (TIM3 instant playback switch), the
 * service detects the mismatch on its next tick, closes admission, drains
 * accepted work, switches target, recounts pool state, and reopens
 * admission. Why: playback scene changes are instant (TIM3) and never wait
 * for the service, but the service must close out the old scene before
 * accepting work for the new one. Changeover is bounded at ~160 ms worst
 * case (64 queue entries + 128-step bulk drain). During changeover, edits
 * are rejected; the sequencer is already playing the new scene. Inputs:
 * seq_activePattern and internal queue/bulk state. Output: service_scene
 * tracks seq_activePattern; admission reopens. Affiliates: seq_activePattern,
 * service_queue, bulk barrier state.
 */
```

---

### Gate 6 — Gap Maintenance and Elastic Gap Policy

#### G6-1. `Core/Bank/Scene/Pattern/PatternStackService.c` — ADD gap state

```c
/*
 * Running logical-chunk occupancy counter.
 *
 * What: tracks the number of logical block chunks in the service scene's
 * pool. Incremented on alloc, decremented on free. Why: the elastic gap
 * policy uses this to decide the gap target (2 chunks below
 * PAT_GAP_REDUCE_THRESHOLD, 1 chunk at or above). Initialized from a
 * bitmap popcount at boot and on target acquisition. RAM: 2 bytes .bss.
 * Affiliates: pat_poolUsagePercent() for the widget (reads bitmap
 * directly), patSvc_tick() Tier 1/2 maintenance.
 */
static uint16_t logical_chunks_used;
```

#### G6-2. `config.h` — ADD gap threshold constant

**File:** `config.h`
**Location:** after `PAT_DEFAULT_VELOCITY` (line ~255)
**Action:** ADD

```c
/*
 * Pool occupancy threshold for reduced gap target.
 *
 * What: above this percentage, the Tier 1 gap maintenance targets 1 chunk
 * instead of 2. Why: at high occupancy, preserving capacity takes priority
 * over absorbing multi-edit bursts without relocation. The service naturally
 * stops creating gaps when no space remains for relocation; no separate
 * "compact mode" flag is needed. Inputs: compile-time constant. Output:
 * compared against logical_chunks_used / total_backed_chunks * 100.
 * Affiliates: PatternStackService.c Tier 1 scan.
 */
#define PAT_GAP_REDUCE_THRESHOLD  60u
```

---

### Gate 7 — Tier 1 Gap Maintenance

#### G7-1. `Core/Bank/Scene/Pattern/PatternStackService.c` — ADD Tier 1 scan

```c
/*
 * Round-robin address-array scan cursor for Tier 1 gap maintenance.
 *
 * What: a 10-bit cursor (0..895) that inspects one address entry per idle
 * tick, checking whether the block at that step has fewer free trailing
 * chunks than the current gap target. Why: gap maintenance distributes free
 * space as trailing gaps after blocks, reducing how often the writer's
 * grow-in-place path must fall back to write-new/swap/free-old. Inputs:
 * the address array and bitmap of the service scene. Output: at most one
 * gap-creating relocation per idle tick. RAM: 2 bytes .bss. Affiliates:
 * patSvc_tick() priority 3.
 */
static uint16_t tier1_scan_cursor;
```

---

### Gate 8 — Tier 2 Global Compaction

#### G8-1. `config.h` — ADD compaction constants

**File:** `config.h`
**Location:** after `PAT_GAP_REDUCE_THRESHOLD`
**Action:** ADD

```c
/*
 * Tier 2 global compaction configuration.
 *
 * What: PAT_COMPACT_INTERVAL_MS controls the minimum interval between
 * background compaction attempts. PAT_COMPACT_SCAN_PER_TICK controls how
 * many address entries the candidate scan inspects per tick (split scan).
 * Why: compaction consolidates scattered free chunks into contiguous runs
 * for new allocations. The scan is split across ticks to avoid foreground
 * stalls. Inputs: compile-time constants. Output: patSvc_tick() priority 4
 * uses these to pace its work. Affiliates: PatternStackService.c.
 */
#define PAT_COMPACT_INTERVAL_MS        100u
#define PAT_COMPACT_SCAN_PER_TICK       16u
```

---

### Makefile — ADD new source file

**File:** `Makefile`
**Location:** in the `C_SOURCES` list, after the existing
`Core/Bank/Scene/Pattern/PatternData.c` entry
**Action:** ADD

```
Core/Bank/Scene/Pattern/PatternStackService.c \
```

---

## Boot Initialization Sequence

### `main.c` — ADD service init

**File:** `main.c`
**Location:** after `pat_initScene()` for all 16 scenes and after the boot
Bank Load completes, before `audioCodec_init()`
**Action:** ADD

```c
    /*
     * Initialize the Pattern stack service after all Scene regions are live.
     *
     * What: sets service_scene to seq_activePattern, initializes the
     * logical_chunks_used counter from a bitmap popcount, and opens
     * admission. Why: the service must know the initial target and pool
     * state before the first foreground tick. Inputs: seq_activePattern and
     * the initialized pat_regions[]. Output: the service is ready to accept
     * edits. Affiliates: PatternStackService.c.
     */
    patSvc_init();
```

---

## RAM Budget Summary

| Item | Bytes | Region | Owner |
|------|------:|--------|-------|
| Edit event queue (64 x 4B) | 256 | .bss (SRAM1) | PatternStackService.c |
| Producer/consumer cursors | 2 | .bss (SRAM1) | PatternStackService.c |
| Service scene + open flag | 2 | .bss (SRAM1) | PatternStackService.c |
| Bulk operation state | 6 | .bss (SRAM1) | PatternStackService.c |
| Tier 1/2 cursors + recovery | 6 | .bss (SRAM1) | PatternStackService.c |
| Logical chunks used counter | 2 | .bss (SRAM1) | PatternStackService.c |
| Per-track pattern array | 7 | .bss (SRAM1) | sequencer.c |
| Pool widget retained value | 1 | .bss (SRAM1) | menu.c |
| **Total** | **282** | | |

Approved ceiling: +300 bytes SRAM1.

---

## Implementation Order

1. **Part B: Pool usage monitor** — B-2 through B-14
2. **Gate 1: Publication ordering fix** — G1-1, G1-2
3. **Gate 2: Service dispatcher** — G2-1 through G2-7 (new files, routing, autosave)
4. **Gate 3: Edit queue + TIM3 handoff** — G3-1 through G3-3
5. **Gate 4: Bulk barriers** — G4-1, G4-2
6. **Gate 5: Mutation-target handover** — G5-1
7. **Gate 6: Gap maintenance** — G6-1, G6-2
8. **Gate 7: Tier 1 gap scan** — G7-1
9. **Gate 8: Tier 2 compaction** — G8-1
10. **Hardware stress test**

---

## Files Changed Summary

| File | Action | Gates |
|------|--------|-------|
| `config.h` | ADD constants | G6, G8 |
| `Core/Bank/Scene/Pattern/PatternData.h` | ADD declaration | B |
| `Core/Bank/Scene/Pattern/PatternData.c` | ADD function, MODIFY 2 functions | B, G1 |
| `Core/Bank/Scene/Pattern/PatternStackService.h` | CREATE | G2 |
| `Core/Bank/Scene/Pattern/PatternStackService.c` | CREATE | G2–G8 |
| `Core/Bank/Scene/Pattern/PatternTrace.h` | ADD stages | G3 |
| `Core/Sequencer/sequencer.h` | ADD extern | G2 |
| `Core/Sequencer/sequencer.c` | ADD array, MODIFY erase path | G2, G3 |
| `Core/Menu/menu.h` | ADD enums, ADD sentinel | B |
| `Core/Menu/menu.c` | ADD 7 functions/statics, MODIFY 8+ sites | B, G2 |
| `Core/Menu/MenuText.h` | ADD 2 strings | B |
| `Core/Menu/menuPages.h` | MODIFY 1 line | B |
| `Core/Menu/copyClearTools.c` | MODIFY 2 call sites | G2 |
| `Core/Bank/Scene/Pattern/EuklidGenerator.c` | MODIFY 1 call site | G2 |
| `Core/Hardware/timebase.c` | ADD 1 call | G2 |
| `Core/Hardware/SD/filesystem.c` | ADD 1 guard | G2 |
| `main.c` | ADD init call | G2 |

---

## Session 067 implementation notes

### 2026-09-18 — Part B and Gates 1–8 landed

- Added `pat_poolUsagePercent()` with packed-bitmap-safe `memcpy`/popcount
  accounting and the retained Global `pts`/Pattern StoreUse widget. The value
  is sampled only when entering the Global page; it is not recomputed during
  the session.
- Corrected dynamic-block publication ordering in `PatternData.c`: clear and
  erase paths detach the address before freeing bytes; replacement paths write
  the new block, atomically publish the latest trigger plus offset, then free
  the old run. Same-size whole-block rewrites no longer happen in place.
- Added the narrow PatternData service hooks needed by the separate service
  translation unit: relocation dirty marking and trigger-preserving dynamic
  detach/free. This preserves PatternData ownership of block geometry and
  dirty registers without exposing allocator helpers.
- Added `PatternStackService.c/.h` with a 64-entry PRIMASK-protected FIFO,
  direct-when-idle/queued-when-busy admission, live-erase handoff, bounded
  clear/target-removal barriers, target handover, logical occupancy recount,
  Tier 1 trailing-gap relocation, and paced reactive/background Tier 2 moves.
- Routed Menu, copy/clear, and Euclidean pool mutations through `patSvc_*`;
  static trigger-bit operations remain direct as required by the plan. TIM3
  now clears the trigger immediately and only publishes deferred reclamation.
- Added S067 trace stage codes, service cadence, AutoSave idle gating, boot
  initialization after the boot filesystem ladder, per-track assignment stub,
  and Makefile/config integration.

### Source-grounded implementation decisions

- The existing PatternData pool helpers are file-local, so the service calls
  the now service-exclusive raw `pat_*` mutation entrypoints rather than
  duplicating their block serialization. The service remains the only caller
  outside PatternData; all application callers use `patSvc_*`.
- The legacy shrink-in-place fallback was removed along with same-size
  rewrite-in-place. A removal can compact automation bytes, so rewriting the
  old live block after allocation failure would violate the playback safety
  invariant; reactive compaction retries the unchanged operation instead.
- Relocation dirty marking is exposed as a one-function PatternData boundary,
  not by exporting bitmap/allocator internals. The service only owns the
  relocation transaction and its queue/maintenance policy.

### Verification status

- Source edits are complete through the planned service integration. Hardware
  stress tests remain pending.

### 2026-09-18 — Safety hardening and final build verification

- Corrected the Gate-6 append verifier so it checks the existing automation
  entries from the first entry byte, then writes the appended value before
  publishing the new count. The append path now remains disjoint from any
  live rewrite of previously published bytes.
- Reactive compaction now permits any validated lower relocation to coalesce
  fragmented free space; it no longer skips smaller blocks that can still
  create the run required by the blocked queue head.
- Added the filesystem replacement boundary required by the detail plan:
  asynchronous Pattern loads close service admission before direct resident
  address/bitmap/pool writes, wait for queued work to drain, and reconcile the
  bitmap/cursors before reopening. Boot-time replacement remains before
  `patSvc_init()` as required.
- Changed queued track/pattern clears to admit the barrier before clearing
  static triggers. A full FIFO therefore leaves the existing trigger and pool
  ownership intact instead of creating an orphaned dynamic block.
- `make -j2` completed and linked `build/lxr02.elf`: `text=447,644`,
  `data=412`, `bss=291,140`. Compared with the S066 baseline in MEMORY.md,
  this is `+8,248` text bytes, `0` data bytes, and `+288` SRAM bytes; the
  SRAM delta is within the approved +300-byte ceiling. `git diff --check`
  is clean.
- Build warnings are limited to the existing packed-member warning in
  `PatternData.c`, the corresponding packed address-member warning in the new
  relocation helper, existing unused filesystem helpers, standard `nosys`
  syscall stubs, and the known LTO serial-compilation note. No hardware
  playback/SD stress test was available in this workspace.
| `Makefile` | ADD 1 source | G2 |
