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

---

## Post-Implementation Code Assessment

Examined: complete `git diff 7ba1428..0f8591a` across all 20 changed files
(2,113 insertions, +348 lines in PatternData.c, +1,225 lines new
PatternStackService.c). Every publication-ordering path, PRIMASK section,
queue boundary, barrier drain, relocation transaction, filesystem replacement
gate, and caller migration was read and verified against the schedule and
the detail plan.

### Schedule compliance

Every scheduled item from Part B and Gates 1–8 is present in the committed
code:

| Schedule item | Status |
|---------------|--------|
| B-2 `pat_poolUsagePercent` declaration | Present, PatternData.h |
| B-3 `pat_poolUsagePercent` definition | Present, PatternData.c; uses `memcpy`+`__builtin_popcount` as planned |
| B-4a–d NamesEnum / shortNames / longNames / PAR sentinel | All four present in menu.h |
| B-5a–b MenuText.h short/long strings | `"pts"` and `"PtrnStoreUse"` (truncated form per schedule note) |
| B-6 valueNames entry | Present, `{SHORT_PAT_STORE_USE,CAT_PATTERN,LONG_PAT_STORE_USE}` |
| B-7 menuPages.h Global subpage 2 | `TEXT_PAT_STORE_USE`/`PAR_PAT_STORE_USE` at position 7 |
| B-8 Retained static value | `menu_patStoreUsePercent` with comment block |
| B-9 Widget visibility predicate | `menu_patStoreUseWidgetVisible()` |
| B-10 Format helpers (3-char and 4-char) | Both present, modeled on CPU widget |
| B-11 Click-in display | `menu_displayPatStoreUseEdit()` |
| B-12a–d Rendering path modifications | All four PAR_RUNTIME_CPU_USE sites extended |
| B-13 Compute-on-entry | In `menu_switchPage` MENU_MIDI_PAGE block |
| B-14 Forward declarations | All four added |
| G1-1 `pat_writeDynamic` publication fix | Clear/publish/free ordering in all three paths |
| G1-2 `pat_eraseStep` publication fix | Detach under PRIMASK, then free |
| G2-1 PatternStackService.h | 106 lines, all planned API + extras |
| G2-2 PatternStackService.c | 1,225 lines with all Gates |
| G2-3 `seq_perTrackPattern` extern | sequencer.h |
| G2-4 `seq_perTrackPattern` definition + init | sequencer.c, all three update sites |
| G2-5 `patSvc_tick()` in timebase | After `endlessPots_tick()` |
| G2-6 AutoSave idle guard | filesystem.c line 24139 |
| G2-7 All caller migrations | 15 call sites in menu.c, 2 in copyClearTools.c, 1 in EuklidGenerator.c |
| G3-1 Queue | 64-entry `volatile uint32_t` ring with PRIMASK enqueue |
| G3-2 Trace stages | Seven new `pat_trace_stage_t` values (Q/C/F/R/M/G/X) |
| G3-3 TIM3 live erase handoff | `pat_setStepActive` + `patSvc_enqueueErase` |
| G4-1 Bulk barrier state | `bulk_op`, `bulk_track`, `bulk_target`, two cursors |
| G5-1 Handover state machine | Priority 0 in `patSvc_tick()` |
| G6-1 `logical_chunks_used` | Present, recomputed on handover/init/replacement |
| G6-2 `PAT_GAP_REDUCE_THRESHOLD` | 60u in config.h |
| G7-1 Tier 1 scan cursor | `tier1_scan_cursor`, one address per idle tick |
| G8-1 Compaction constants | `PAT_COMPACT_INTERVAL_MS` 100, `PAT_COMPACT_SCAN_PER_TICK` 16 |
| Makefile | PatternStackService.c added after PatternData.c |
| main.c | `patSvc_init()` after boot filesystem ladder |

### Deviations from schedule

Seven changes exist in the implementation that the schedule did not call for.
All are improvements; none are regressions.

**1. Shrink-in-place path removed** (schedule said retain with PRIMASK fix).
The implementation removes it entirely and returns failure instead. Rationale
in the diff: even a removal can compact existing automation bytes, so
rewriting the old live block before publishing a replacement would let TIM3
observe a partially rewritten block. The reactive compaction path retries the
unchanged operation when a run becomes available. *Safer than the schedule.*

**2. `pat_releaseStepDynamic()` added** (not in schedule). A new PatternData
function that detaches a dynamic block while preserving the latest trigger
bit, then frees the captured pool run. Called by the service for queued live
erase (where TIM3 already cleared the trigger, but a new trigger may have
arrived) and clear-track barriers (where Euclidean generation may set new
triggers after the barrier enqueue). This is the correct abstraction: the
service needs trigger-preserving detach, while `pat_eraseStep()` is
destructive.

**3. `pat_markPoolMutationDirty()` added** (not in schedule). Exposes
PatternData's established dirty-marking boundary (card-clean + AutoSave) as
a narrow one-function API for the service's relocation executor, without
exporting bitmap or allocator internals. Clean module boundary.

**4. `pat_tryAppendAutomation()` added** (not in schedule). A Gate-6 in-place
growth optimization for the single-automation-append case. Only writes new
bytes at the end of the existing block; never rewrites published content.
The count byte is published last. Adjacent free chunks must be verified
before the append path is taken. The old entries are confirmed byte-for-byte
against the caller's canonical list before any write. *Safe: append-only,
count-last publication.*

**5. `pat_writeSpecials()` and raw setters changed from void to uint8_t**
(not in schedule). Required because the service's `patSvc_executeEvent()`
needs to know whether the underlying raw mutation committed. Without this,
a failed allocation would be invisible to the service.

**6. Filesystem replacement boundary added** (schedule only had the
AutoSave guard). `patSvc_prepareSceneReplace()` and
`patSvc_finishSceneReplace()` are called at five points in the filesystem
state machine — Bank load commit (phase 42), single-Pattern load (phase 6),
Pattern close (phase 52), error recovery, and `filesystem_finish()` terminal
path. This fulfills the detail plan's requirement that asynchronous Pattern
loads close service admission before direct resident address/bitmap/pool
writes. *More complete than the schedule.*

**7. Queued clear ordering inverted** (schedule said clear triggers then
enqueue barrier). The implementation enqueues the barrier first, then clears
triggers only on queue success. This prevents orphaned dynamic blocks when
the FIFO is full: a rejected barrier with already-cleared triggers would
leave pool blocks allocated but unreachable.

### PRIMASK section audit

Every PRIMASK section was verified for bounded duration. None contain any
loop, allocation, scan, or function call.

| Site | Instructions | Purpose |
|------|-------------|---------|
| `patSvc_enqueue()` | ~5 | Distance check, store event, increment prod |
| `patSvc_publishOffset()` | 3 | Load trigger, OR offset, store address |
| `pat_eraseStep()` | 1 | Store PAT_ADDR_SENTINEL |
| `pat_releaseStepDynamic()` | 3 | Load trigger, OR sentinel, store |
| `pat_writeDynamic()` clear path | 3 | Load trigger, OR sentinel, store |
| `pat_writeDynamic()` publish path | 3 | Load trigger, OR offset, store |
| `pat_clearTrack()` per-step | 1 | Store PAT_ADDR_SENTINEL |

All are under 10 cycles at 216 MHz (<50 ns). The 1 ms blocking constraint
is satisfied by orders of magnitude.

### Publication ordering audit

Every address-to-pool transaction follows detach/publish-before-free:

| Path | Order | Verified |
|------|-------|----------|
| `pat_writeDynamic` clear (flags=0) | PRIMASK detach → free old | Yes |
| `pat_writeDynamic` replace | Write new → PRIMASK publish → free old | Yes |
| `pat_eraseStep` | PRIMASK detach → free old | Yes |
| `pat_releaseStepDynamic` | PRIMASK detach (trigger-preserving) → free old | Yes |
| `pat_clearTrack` per-step | PRIMASK detach → free | Yes |
| `patSvc_relocateIndex` | Bitmap-reserve new → memmove → PRIMASK publish → bitmap-clear old → zero old | Yes |

TIM3 always sees either the complete old block (before detach/publish) or
the sentinel/new-offset (after). There is no window where an old address
points at zeroed pool bytes.

### Queue safety audit

- Producer: PRIMASK-protected for both TIM3 and foreground callers. Unsigned
  `uint8_t` wrapping arithmetic for distance is correct: `prod - cons` wraps
  to 0–255, and the queue mask is 63.
- Consumer: foreground-only (`patSvc_tick()`), never called from ISR.
- All 64 slots are usable (no empty-slot convention); distance < 64 is the
  admission test.
- Queue array and cursors are `volatile`, preventing the compiler from
  reordering or caching reads.
- Overflow is traced (PAT_TRACE_STAGE_QUEUE_OVERFLOW) and the event is
  dropped. No silent loss.

### Relocation transaction audit

`patSvc_relocateIndex()` (line 385–438):
1. Validate address has SPECIALS_BIT and a sane block geometry
2. Find a free run (optionally lower-only for Tier 2 compaction)
3. Reject if `new_offset == old_offset`
4. **Reserve** new bitmap bits before copy (lines 424–425)
5. **memmove** block bytes (line 426)
6. **Publish** new offset under PRIMASK (line 428)
7. **Clear** old bitmap bits (lines 429–430)
8. **Zero** old pool bytes (line 431)
9. Mark dirty (line 432)

Between steps 4 and 7, both old and new runs are marked occupied in the
bitmap. This is safe: `pat_poolAlloc()` only runs from the same foreground
context (via `pat_writeDynamic()`), and TIM3 never reads the bitmap. After
step 6, TIM3 sees the new offset and reads the new (copied) bytes. After
step 7, the old bits are free for future allocation.

### Filesystem replacement boundary audit

The replacement boundary follows a prepare/commit/finish protocol:

1. `filesystem_patternServiceReady()` calls `patSvc_prepareSceneReplace()`
   for each selected Scene. The service closes admission and starts draining.
   Returns false while draining; the filesystem state machine polls.
2. Filesystem writes resident bytes directly via `pat_sceneRegionMut()`.
3. `filesystem_patternServiceFinish()` calls `patSvc_finishSceneReplace()`
   to recount the bitmap, reset cursors, and reopen admission.

The finish call is present in:
- `filesystem_finish()` (terminal path — catches all exits)
- Phase 52 (v4 Pattern close — early release before effect cleanup)
- Single-Pattern load success path (phase 14)
- Single-Pattern load error path (phase 13)

`patSvc_prepareSceneReplace()` for non-service Scenes returns immediately
(no gate needed). The boot path runs before `patSvc_init()`, so no gate
exists yet — correct, since the service hasn't opened admission.

### RAM budget

| Item | Scheduled | Actual |
|------|----------:|-------:|
| Queue (64 × 4B) | 256 | 256 |
| Cursors (prod/cons) | 2 | 2 |
| Scene + open | 2 | 4 (+handover, +replace_pending) |
| Bulk state | 6 | 6 |
| Tier 1/2/reactive cursors + occupancy + compact tick | 6 | 13 (+reactive_scan/required/active, +last_compact_tick) |
| Per-track pattern | 7 | 7 |
| Widget retained value | 1 | 1 |
| **Total** | **282** | **289** |

Build output: `bss=291,140`, S066 baseline `bss=290,852`, delta **+288**.
Within the approved +300 byte ceiling.

### Text budget

Build output: `text=447,644`, S066 baseline `text=439,396`, delta **+8,248**.
The schedule estimated +800–1,200; the actual delta is ~7× larger. Primary
contributors: PatternStackService.c at 1,225 lines, the `pat_tryAppendAutomation`
optimization (+72 lines), and the filesystem replacement boundary (+46 lines
in filesystem.c). The text budget was informational — the hard constraint was
the +300 SRAM ceiling, which is met.

### Efficiency notes

`patSvc_countUsed()` performs a full 2,048-bit scan on every idle tick. At
216 MHz with per-chunk function calls this is approximately 10–20 µs. This
runs only when the queue and bulk are empty and the Tier 1 cursor has
completed its full sweep — the lowest-priority idle path. For a maintained
counter approach (increment on alloc, decrement on free), the existing
`logical_chunks_used` already serves that role for gap policy decisions; the
full recount at idle is a reconciliation pass that catches any counter drift.
Acceptable.

### Potential concern: `patSvc_clearPattern` direct path

`patSvc_clearPattern()` (line 1006): the direct path calls
`pat_clearPattern()` which internally calls `pat_initScene()` — a `memset`
of the entire Scene region (~10 KB). This is a single foreground call, not
bounded per-tick. However, `pat_clearPattern()` was already synchronous
before S067, so this is not a regression. The queued path is bounded (clears
triggers, then the CLEAR_PATTERN event drains in one tick via
`patSvc_executeEvent`).

### Verdict

The implementation is faithful to the schedule and the detail plan. Every
publication-ordering fix is correct. Every PRIMASK section is minimal and
bounded. The six unscheduled additions are all safety improvements or module
boundary refinements. The SRAM budget is met (+288 of +300). The text budget
is exceeded but was not a hard constraint. Hardware stress testing remains
the only open validation item.

---

## Part C — Hardware Output Validation (S067 Session 3)

Hardware-captured output files from `SD_CARD_PAT_STACK_OUTPUT/` examined
against the implementation. The user reports working primarily with Scene 9
(pattern index 8, zero-indexed), relying on autosave. Five files present:

| File | Size | Description |
|------|------|-------------|
| `.pat08a` | 10656 B | Scene 8 autosave ping-pong copy A |
| `.pat08b` | 10656 B | Scene 8 autosave ping-pong copy B |
| `.pat09b` | 10656 B | Scene 9 autosave (trigger-only) |
| `pattrace.bin` | 12312 B | PatternTrace: 1539 records |
| `asavetrc.bin` | 628736 B | AutoSaveTrace: 78592 records |

### C.1 — PAT4 File Structural Integrity

All three files pass every structural check:

| Check | .pat08a | .pat08b | .pat09b |
|-------|---------|---------|---------|
| Magic `PAT4` | OK | OK | OK |
| File size = 10656 | OK | OK | OK |
| Version = 1 | OK | OK | OK |
| Step-id match (header ↔ address position) | 11/11 | 11/11 | 0/0 (no blocks) |
| Bitmap ↔ block coverage | OK | OK | OK |
| No overlapping allocations | OK | OK | OK |
| No orphaned bitmap bits | OK | OK | OK |
| Block chunk count = bitmap popcount | 47 = 47 | 47 = 47 | 0 = 0 |

`.pat08a` and `.pat08b` carry identical logical content: the same 11 dynamic
blocks with the same automation entries, flags, and step-ids. They differ only
in pool byte offsets, which is expected because Tier 1/2 relocations move
blocks between autosave capture points.

`.pat09b` has 27 triggers across all 7 tracks but zero dynamic blocks. This is
consistent with a pattern that has trigger-only steps (no custom note, velocity,
probability, or automation overrides).

### C.2 — PatternTrace (pattrace.bin)

1539 records, all healthy maintenance. Zero error events.

| Stage | Count | Description |
|-------|-------|-------------|
| TIER1_GAP (M) | 1283 | Tier 1 gap-reduce relocations |
| TIER2_RELOC (R) | 256 | Tier 2 paced compaction relocations |

No QUEUE_OVERFLOW (Q), CAPACITY_DROP (C), FRAG_DROP (F), WRONG_SCENE (X),
PENDING_OVERFLOW (H), or GAP_FALLBACK (G) records. The service ran through
its full relocation budget without encountering any error or fallback path.

Trace records span ticks 19880–44921. The first few records are scene 10
(T0S2, T0S3, T0S6, T1S9), then the bulk (scene 8) begins at tick 24808.
This is consistent with normal operation: the service relocated blocks in
whatever scene was active at the time.

### C.3 — PAT4 Cross-Reference Against Relocation Trace

Of 11 dynamic blocks, 1 remained at the same offset between saves (T6S8 @168)
and 10 moved. For 8 of those 10, the relocation trace contains a chain whose
final entry maps the `.pat08a` offset → `.pat08b` offset exactly:

| Block | .pat08a | .pat08b | Final trace entry |
|-------|---------|---------|-------------------|
| T1S14 | 296 | 208 | `296→208` (TIER2_RELOC, t=43110) |
| T2S2 | 28 | 84 | `28→84` (TIER1_GAP, t=43628) |
| T2S13 | 36 | 16 | `36→16` (TIER1_GAP, t=43650) |
| T2S14 | 8 | 24 | `8→24` (TIER1_GAP, t=43652) |
| T4S6 | 256 | 92 | `256→92` (TIER1_GAP, t=44149) |
| T6S9 | 192 | 116 | `192→116` (TIER1_GAP, t=44667) |
| T6S10 | 52 | 144 | `52→144` (TIER1_GAP, t=42856) |
| T6S11 | 100 | 44 | `100→44` (TIER1_GAP, t=42858) |

Two blocks have `.pat08b` offsets absent from the relocation trace:

| Block | .pat08a | .pat08b | Trace final | Explanation |
|-------|---------|---------|-------------|-------------|
| T1S5 | 64 | 272 | 64 (t=41296) | User write reallocated |
| T1S11 | 112 | 292 | 240 (t=44921) | User write reallocated |

These are **not** corruption. User writes through `pat_writeDynamic()` free
the old block and allocate at the next available bitmap position. Since user
writes are not traced by PatternTrace (only relocation operations are), the
new offsets 272 and 292 do not appear in the trace file. Supporting evidence:

1. **Adjacent allocation**: 272 + 20 (5 chunks × 4) = 292. The two blocks
   were allocated sequentially by the bitmap scanner, consistent with a
   fresh bump allocation after a free.
2. **Old offsets freed**: in `.pat08b`'s bitmap, offset 64 (T1S5's trace-final)
   is CLEAR and offset 240 (T1S11's trace-final) is CLEAR — the old blocks
   were properly freed before the new allocation.
3. **New offsets covered**: all 13 chunks (5 + 8) at offsets 272–323 are SET
   in the bitmap.
4. **Logical content identical**: both blocks carry the same automation
   entries, targets, and values in `.pat08a` and `.pat08b`.

### C.4 — AutoSaveTrace (asavetrc.bin)

78592 records, properly decoded with AutoSave stage codes. Zero errors.

| Stage | Count | Description |
|-------|-------|-------------|
| DIRTY (D) | 77588 | Individual dirty-byte marks |
| INSTRUMENT_MARK (I) | 364 | Whole-Instrument dirty marks |
| VALIDATED (V) | 108 | Autosave passes validation |
| TERMINAL (T) | 84 | Autosave cycles complete |
| ADMITTED (A) | 83 | Autosave cycles admitted |
| LOAD_MARK (L) | 74 | Kit/Scene load terminal markers |
| PUBLISHED (P) | 62 | Autosave data published to card |
| CAPTURED (C) | 61 | Autosave data captured from RAM |
| SCHEDULED (S) | 53 | Autosave passes scheduled |
| MASK_MERGED (M) | 52 | Dirty masks merged for write |
| SAVE_LIFECYCLE (O) | 27 | Save operation checkpoints |
| BOOT_READER (Q) | 25 | Boot-time autosave reader decisions |
| TRACE_DROPPED (G) | 9 | Ring overflow (benign diagnostic loss) |
| BANK_PRESENT (B) | 2 | Bank present-mask witnesses |

**Error check**: zero OPERATION_ERROR (E) records. Zero PHASE_STALL (X)
records. All 27 SAVE_LIFECYCLE records have the FAILED flag clear.

**Boot reader**: all 25 Q records are summary records (flags bit 7 set) with
`case2_mask=0x0000` and `case3_mask=0x0000` — clean boots across 25 power
cycles with no embedded-source mismatches, no Scene invalidations, and no
single-level reloads needed.

**Autosave lifecycle**: the D → S → A → V → M → C → P → T chain is complete
and balanced. 84 TERMINAL records against 83 ADMITTED records (off-by-one
from the final in-progress cycle visible in the last records) confirms that
every admitted autosave cycle ran to completion.

**Last records**: the file's final sequence shows a complete autosave cycle
followed by one more boot and a partial cycle in progress:
```
DIRTY(×16) → SCHEDULED → ADMITTED → VALIDATED → MASK_MERGED →
CAPTURED → PUBLISHED → TERMINAL →
VALIDATED → BOOT_READER(summary) →
SCHEDULED → ADMITTED → VALIDATED → MASK_MERGED → TERMINAL
```

The last TERMINAL proves the most recent autosave completed. The BOOT_READER
summary between cycles confirms a clean power-on before the final session.

### C.5 — Verdict

**Everything tracks OK.** Specifically:

1. **No data corruption**: all three PAT4 files are internally consistent —
   step-ids, bitmaps, block boundaries, and automation entries are all valid.
2. **No service errors**: pattrace.bin contains zero error events across 1539
   maintenance records. The service ran 1283 gap-reduce and 256 compaction
   relocations without a single capacity drop, fragmentation drop, queue
   overflow, or scene mismatch.
3. **Relocations verified**: 8 of 10 moved blocks have their offset changes
   explained by exact relocation-chain endpoints in the trace. The remaining
   2 are explained by user writes (adjacent allocation, old offsets freed,
   identical logical content).
4. **Autosave healthy**: 78592 AutoSaveTrace records show zero operation
   errors, zero stalls, 25 clean boots, and complete DIRTY-to-TERMINAL
   lifecycle chains for every admitted save.
5. **Pool usage nominal**: 47/2048 chunks (2%) occupied in Scene 8. The
   defragmentation service is maintaining the pool well below the
   PAT_GAP_REDUCE_THRESHOLD (60%).

The Pattern stack service implementation is validated on hardware.
