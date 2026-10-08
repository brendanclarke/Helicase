# S077 P5 — Bar-to-Step Copy (cross-kind paste)

**Parent:** S077 retest, user request 2026-10-08.
**Status (2026-10-08):** implemented — build clean, boot image written.

---

## Implementation Log

### 2026-10-08 — complete

All scheduled changes are applied, using Option A (`CC_KIND_BAR_TO_STEP`).
Every code change carries its comment block adjacent in the same file, and the
paired `.c`/`.h` descriptions agree. Build is clean and the boot image is
written.

**Build (DEV config, `make all` then `make img`):**

```
   text     data     bss      dec      hex   filename
 538576      420  427008   966004    ebd74   build/lxr02.elf
Flash : 538,996 / 753,664 B used, headroom 214,668 B
ITCM  : 4,168 / 16,384 B;  DTCM statics 4,472 B
FXBUF : 126,592 B at 0x20001180 (min 122,880, margin 3,712)
Written: build/LXRV2_lxr02.img (538996b) OK
```

Versus the S077 P4 close (`text=538,608`): **text −32 B**, `data`/`bss`
unchanged, RAM unchanged. The only warnings in the build are pre-existing
`-Wunused-function` warnings in `Core/Hardware/SD/filesystem.c` (functions
whose callers sit in a compiled-out DEV branch); they surfaced only because
Change 1 touched `copyClearSession.h`, which `filesystem.c` includes. No
warning comes from a P5-modified translation unit.

**Steps taken**

1. **Change 1** — `copyClearSession.h`: added `CC_KIND_BAR_TO_STEP` (6) to
   `cc_kind_t` with the full contract block, and updated the short enum
   comment above it.
2. **Change 2** — `copyClearSession.c` `cc_copySeq()`: added the
   `else if (cc_source.kind == CC_KIND_BAR)` branch that queues the
   bar-to-step paste and flashes the destination step range
   (`sourceLength * NUM_STEPS_PER_BAR`).
3. **Change 3** — `copyOps.c`: added `ccCopy_requestBarToStep()` (sets
   `job.kind = CC_KIND_BAR_TO_STEP`, skips the identical test, queues and
   fires the early triggers); declaration added to `copyOps.h`.
4. **Change 4** — `copyClearService.c` `ccSvc_pasteGeometry()`: added the
   `CC_KIND_BAR_TO_STEP` case (bar source geometry; `g->dst_first =
   job->start`).
5. **Change 5** — `copyClearService.c` `ccSvc_pasteTriggersNow()`: the gate
   now accepts `CC_KIND_BAR_TO_STEP`.
6. **Change 6** — `copyOps.c` `ccCopy_runJob()`: `CC_KIND_BAR_TO_STEP`
   routes to `ccSvc_runPatternPaste()`.
7. **Change 7** — `COPYCLEAR_UTILITIES.md`: §6.3 bar-to-step paragraph,
   §11.3 routing row, §12.5 heading, §20 history entry.
8. **Change 8** — comments: geometry header (`copyClearService.c`),
   `cc_job_t` start description and `ccSvc_runPatternPaste` line
   (`copyClearService.h`), `ccCopy_runJob` line (`copyOps.h`).

**Notes / deviations**

- The schedule's Change 7c referred to a "§13 executor table" row for the
  `ccSvc_runPatternPaste` description, but the spec has no such §13 row; the
  matching text is the **§12.5 heading** ("Paste engine
  (`ccSvc_runPatternPaste()`; step, range, bar, `copy track`)”). That heading
  was updated instead.
- Change 3's function header reads `Outputs: 1 if a job was queued, 0 if
  dropped (queue full)` — the schedule's draft mentioned "or identical", but
  a bar-to-step paste is never identical (§2.3), so no identical path exists.
- The spec's §6.2 "Pastes and where they write" table was not extended with a
  bar-to-step row (not in the schedule); the new behavior is documented in
  §6.3, §11.3 and §12.5.

**Correctness review (before build)**

- `ccSvc_pasteGeometry()` is the only kind-dependent geometry site; the paste
  engine, `ccSvc_pasteOverlaps()` (skips only `CC_KIND_TRACK`) and the
  finish-phase track-length extension all consume the resolved
  `cc_paste_geo_t`, so `CC_KIND_BAR_TO_STEP` is transparent to them.
- `CC_KIND_BAR_TO_STEP` = 6 fits the 3-bit trace fields (`job->kind & 7u`).
- Reverse direction is untouched: a step source with a SELECT press still
  returns "navigate" from `cc_copySelect()`.

---

## 1. Problem Statement

When a bar or range of bars is selected as the copy source in MODE STEP, and
the user then presses a SEQ button (step) while still holding the copy button,
nothing happens — the step press is silently consumed.  The user wants this to
paste the bar source starting at that step.

The reverse is explicitly NOT true: if a step or range of steps is selected
as the copy source and the user presses a SELECT button (bar), this means
"navigate to view this bar."  This asymmetry is intentional:

- **Bar source → step destination:** paste the bar(s) starting at the pressed
  step.  A bar is 16 steps; a multi-bar range pastes all of its steps
  contiguously starting at the destination step.
- **Step source → bar destination:** navigate (view that bar).  Already the
  current behavior.

Bar-to-bar copy remains unchanged: a bar source followed by a SELECT press
still pastes bar-to-bar.

---

## 2. Affected Operations

### 2.1 Routing — `cc_copySeq()` (copyClearSession.c:328)

**Current:** when `has_source` is true and the source kind is not
`CC_KIND_STEP`, the press falls through to `return 1u` (consumed, no paste).
A `CC_KIND_BAR` source is silently ignored.

**Correct:** when `has_source` is true and `cc_source.kind == CC_KIND_BAR`,
call `ccCopy_requestPaste()` with `dst_start = buttonHandler_visibleStep(index)`
and `job->kind` overridden to `CC_KIND_STEP` (so the geometry function
interprets the destination as an absolute step, not a bar index).

### 2.2 Geometry — `ccSvc_pasteGeometry()` (copyClearService.c:318)

**Current:** when `job->kind == CC_KIND_BAR`, `dst_first` is computed as
`job->start * NUM_STEPS_PER_BAR`.  This assumes the destination is a bar
index.

**Correct:** a new `CC_KIND_BAR_TO_STEP` kind (or equivalent mechanism) tells
the geometry function to use bar-coordinate source geometry (multiply by
`NUM_STEPS_PER_BAR`) but step-coordinate destination geometry (use `job->start`
as an absolute step, not a bar index).

### 2.3 Identical-paste check — `ccCopy_requestPaste()` (copyOps.c:134)

**Current:** for `CC_KIND_BAR`, the identical test compares
`dst_start == src->start` (both bar indices).

**Correct:** for a bar-to-step paste the coordinates are in different spaces
(step vs bar), so the paste is never identical.  The existing check will
always fail because step values (0..127) rarely match bar values (0..7) in
a meaningful way, but this is an accident, not a design.  The check must be
made explicitly correct for the new kind.

### 2.4 Flash feedback — `cc_flashObject()` (copyClearSession.c:235)

**Current:** `CC_KIND_BAR` flashes SELECT LEDs and (if the destination bar is
the viewed bar) all 16 SEQ LEDs.

**Correct:** a bar-to-step paste should flash the destination SEQ LEDs (the
steps that will receive data), not the SELECT LEDs.  This means passing
`CC_KIND_STEP` to `cc_flashObject()` with the absolute step and the step
count derived from the bar source length × 16.

### 2.5 Track-length extension — `ccSvc_runPatternPaste()` finish phase

**Current:** for step/bar pastes, if `g.dst_first + g.count - 1` exceeds the
current track length, the track is extended.  This already uses the geometry's
`dst_first` and `count` which are in absolute steps for both CC_KIND_STEP and
CC_KIND_BAR.  No change needed here — the track-length logic works on the
already-resolved step geometry.

### 2.6 Early trigger bits — `ccSvc_pasteTriggersNow()`

**Current:** accepts `CC_KIND_STEP`, `CC_KIND_BAR`, and `CC_KIND_TRACK`.
Uses `ccSvc_pasteGeometry()` to resolve everything to steps.

**Correct:** the new kind must be accepted here too.  Since the geometry
function will handle the new kind correctly, no further change is needed
beyond accepting the kind value.

---

## 3. Design Decision: How to Encode Bar-to-Step

Two options:

**Option A — New kind constant `CC_KIND_BAR_TO_STEP`.**  Added to `cc_kind_t`
in copyClearSession.h.  The geometry function gets a new case that uses bar
source geometry and step destination geometry.  The routing code sets
`job->kind = CC_KIND_BAR_TO_STEP` when a bar source accepts a step
destination.  The early-trigger function accepts the new kind.

**Option B — Reuse `CC_KIND_STEP` for the job, keep `CC_KIND_BAR` for the
source.**  The routing code calls `ccCopy_requestPaste()` normally but the
geometry function checks both `job->kind` and `src->kind` when they differ.

**Chosen: Option A.**  A new kind constant is the cleanest path:
- The geometry function's switch already dispatches on kind; a new case is
  two lines.
- No existing code path is disturbed (every existing caller still passes the
  same kind values they always did).
- The identical-paste check, early-trigger gate, and trace encoding all key
  on `job->kind` and handle the new value explicitly.
- The flash function can treat `CC_KIND_BAR_TO_STEP` like `CC_KIND_STEP`
  (flash the destination steps).

---

## 4. What Does Not Change

- **Step-to-step copy:** unchanged.
- **Bar-to-bar copy:** unchanged (SELECT press with bar source).
- **Step source → SELECT press:** remains "navigate" (return 0).
- **Track copy:** unchanged.
- **Scene copy:** unchanged.
- **FX step copy:** unchanged.
- **Clear operations:** unchanged (clear never crosses kinds).
- **BAR1/BAR2 navigation:** unchanged.
- **Bar source selection:** unchanged (SELECT press with no source).
- **Menu labels, selection counts, selection enums:** unchanged.
- **Pattern merge/replace logic:** unchanged — `ccCopy_buildStep()` is
  kind-agnostic; it works on individual source/destination steps.
- **Snapshot/overlap detection:** unchanged — the overlap test in
  `ccSvc_runPatternPaste()` sub 0 compares absolute step ranges, which
  the geometry function already resolves.
- **RAM:** no new allocations (one new enum constant).
- **ISR/DSP path:** no changes.

---

## 5. Risk

Low.  The paste engine (`ccSvc_runPatternPaste()`) operates entirely on the
resolved geometry (`cc_paste_geo_t`) — it never inspects `job->kind` after
calling `ccSvc_pasteGeometry()`.  Adding a new kind that produces correct
geometry is transparent to the engine.  The early-trigger function also
operates on the resolved geometry after calling `ccSvc_pasteGeometry()`.

The only places that inspect `job->kind` directly:
1. `ccSvc_pasteGeometry()` — gets a new case (Change 4).
2. `ccSvc_pasteTriggersNow()` — its gate (`job->kind != CC_KIND_STEP &&
   job->kind != CC_KIND_BAR && job->kind != CC_KIND_TRACK`) must accept the
   new kind (Change 5).
3. `ccCopy_runJob()` — switch on `job->kind` to dispatch Pattern vs Scene
   pastes; the new kind falls through to `ccSvc_runPatternPaste()` (Change 6).
4. `ccSvc_pasteOverlaps()` — checks `job->kind == CC_KIND_TRACK` to skip
   (track copies never overlap); `CC_KIND_BAR_TO_STEP` is not `CC_KIND_TRACK`,
   so the overlap test runs correctly — no change needed.
5. `ccSvc_runPatternPaste()` finish phase — checks `job->kind == CC_KIND_TRACK`
   for track-settings copy; `CC_KIND_BAR_TO_STEP` falls into the `else if`
   track-length extension, which is correct — no change needed.
6. `ccCopy_requestPaste()` — identical-paste switch on `src->kind` (unchanged;
   the source is still `CC_KIND_BAR`; the new request function handles
   identity explicitly).
7. `cc_flashObject()` — the routing code passes `CC_KIND_STEP` for the flash,
   not the new kind — no change needed.
8. Trace encoding — `job->kind & 7u` fits in 3 bits; `CC_KIND_BAR_TO_STEP`
   = 6 ≤ 7.

---

## 6. Verification

| # | Check | Observe |
|---|-------|---------|
| 5.1 | Copy 1 bar (SELECT), then paste to a step (SEQ). Verify the 16 source steps appear at the destination starting at that step. | I, D |
| 5.2 | Copy a 2-bar range, paste to a step. Verify 32 steps appear starting at the destination step. Wrap-around: paste near step 128 boundary. | I, D |
| 5.3 | Copy a reversed bar range (press bar 4 then bar 2), paste to a step. Verify the bars paste in reverse order starting at the destination step. | I |
| 5.4 | Copy bars, paste to a step on the same track, overlapping the source. Verify the snapshot path is taken (no data loss). | I |
| 5.5 | Copy bars, paste to a step, verify track length extends if needed. | I |
| 5.6 | Copy bars, then press a SELECT button (bar). Verify bar-to-bar paste still works (not broken by the new path). | I |
| 5.7 | Copy steps, then press a SELECT button. Verify it navigates (not a paste). | I |
| 5.8 | Flash feedback: on bar-to-step paste, verify SEQ LEDs flash (not SELECT LEDs). | D |
| 5.9 | Copy bar, paste to step on same Scene/track where the step falls inside the source bar. Verify paste executes (not treated as identical). | I |

---

## Implementation Schedule

---

## Change 1 — New kind constant `CC_KIND_BAR_TO_STEP`

**File:** `Core/Menu/CopyClear/copyClearSession.h`
**Location:** line 42, inside the `cc_kind_t` enum, after `CC_KIND_FX_STEP`
**Action:** ADD one enum constant (value 6, fits in 3-bit trace field)

### Current enum (lines 36–43)

```c
typedef enum {
    CC_KIND_NONE = 0u,
    CC_KIND_STEP,           /* 1 */
    CC_KIND_BAR,            /* 2 */
    CC_KIND_TRACK,          /* 3 */
    CC_KIND_SCENE,          /* 4 */
    CC_KIND_FX_STEP         /* 5 */
} cc_kind_t;
```

### After

```c
/*
 * What:       CC_KIND_BAR_TO_STEP (6) is a cross-kind paste: the source is a
 *             bar or bar range (CC_KIND_BAR coordinates: bar index 0..7) but
 *             the destination is an absolute step (0..127). The paste engine
 *             resolves this through ccSvc_pasteGeometry() which uses bar-scale
 *             source geometry (src->start * 16, count * 16) and step-scale
 *             destination geometry (job->start as absolute step).
 * Why:        allows the user to paste a copied bar starting at any step, not
 *             only at a bar boundary. The source kind remains CC_KIND_BAR (the
 *             source was selected with a SELECT button); only the job kind
 *             changes to CC_KIND_BAR_TO_STEP to signal the cross-kind
 *             interpretation. Value 6 fits in the 3-bit trace field
 *             (ccTrace_job: job->kind & 0x07u).
 * Inputs:     set by cc_copySeq() when a bar source accepts a SEQ destination.
 * Outputs:    consumed by ccSvc_pasteGeometry(), ccSvc_pasteTriggersNow(),
 *             ccCopy_runJob(), and the trace encoder.
 * Affiliates: CC_KIND_BAR, CC_KIND_STEP, ccSvc_pasteGeometry().
 */
typedef enum {
    CC_KIND_NONE = 0u,
    CC_KIND_STEP,
    CC_KIND_BAR,
    CC_KIND_TRACK,
    CC_KIND_SCENE,
    CC_KIND_FX_STEP,
    CC_KIND_BAR_TO_STEP
} cc_kind_t;
```

Also update the comment above `cc_kind_t` at line 35:

### Before (line 35)

```c
/* What a copy object button selected (also the kind of a held clear object). */
```

### After

```c
/* What a copy object button selected, the kind of a held clear object, or a
 * cross-kind paste (CC_KIND_BAR_TO_STEP: bar source, step destination). */
```

---

## Change 2 — Routing: `cc_copySeq()` accepts bar source

**File:** `Core/Menu/CopyClear/copyClearSession.c`
**Location:** `cc_copySeq()`, lines 337–346 (the `has_source` branch in
VOICE/STEP mode)
**Action:** MODIFY — add a `CC_KIND_BAR` case after the `CC_KIND_STEP` case

### Before (lines 337–346)

```c
        if (cc_source.kind == CC_KIND_STEP) {
            if (ccCopy_requestPaste(&cc_source, cc_state.selection,
                                    cc_activeScene(), menu_getActiveVoice(),
                                    buttonHandler_visibleStep(index)))
                cc_flashObject(CC_KIND_STEP, cc_activeScene(),
                               menu_getActiveVoice(),
                               buttonHandler_visibleStep(index),
                               cc_sourceLength());
        }
        return 1u;
```

### After

```c
        if (cc_source.kind == CC_KIND_STEP) {
            if (ccCopy_requestPaste(&cc_source, cc_state.selection,
                                    cc_activeScene(), menu_getActiveVoice(),
                                    buttonHandler_visibleStep(index)))
                cc_flashObject(CC_KIND_STEP, cc_activeScene(),
                               menu_getActiveVoice(),
                               buttonHandler_visibleStep(index),
                               cc_sourceLength());
        }
        /*
         * What:       bar-to-step paste. When the copy source is a bar or bar
         *             range, a SEQ press pastes the bar content starting at
         *             the pressed step. The job kind is set to
         *             CC_KIND_BAR_TO_STEP so the geometry function uses bar
         *             source coordinates and step destination coordinates.
         *             The flash uses CC_KIND_STEP because the destination is
         *             a step range (the user sees step LEDs flash, not bar
         *             LEDs). The step count is sourceLength * NUM_STEPS_PER_BAR
         *             (each bar expands to 16 steps).
         * Why:        a bar is a contiguous block of steps; pasting to a step
         *             destination gives sub-bar placement precision. The
         *             reverse (step source → bar destination) is navigation,
         *             not a paste, by user rule.
         * Inputs:     cc_source (kind == CC_KIND_BAR), index (SEQ button).
         * Outputs:    queued CC_KIND_BAR_TO_STEP job via ccCopy_requestPaste.
         * Affiliates: cc_copySelect() (bar-to-bar paste, unchanged),
         *             CC_KIND_BAR_TO_STEP, ccSvc_pasteGeometry().
         */
        else if (cc_source.kind == CC_KIND_BAR) {
            if (ccCopy_requestBarToStep(&cc_source, cc_state.selection,
                                        cc_activeScene(),
                                        menu_getActiveVoice(),
                                        buttonHandler_visibleStep(index)))
                cc_flashObject(CC_KIND_STEP, cc_activeScene(),
                               menu_getActiveVoice(),
                               buttonHandler_visibleStep(index),
                               (uint8_t)(cc_sourceLength() *
                                         NUM_STEPS_PER_BAR));
        }
        return 1u;
```

Note: `ccCopy_requestBarToStep()` is a thin wrapper around the existing
`ccCopy_requestPaste()` that overrides `job->kind` to
`CC_KIND_BAR_TO_STEP`.  See Change 3.

---

## Change 3 — New request function `ccCopy_requestBarToStep()`

**File:** `Core/Menu/CopyClear/copyOps.c`
**Location:** after `ccCopy_requestPaste()` (after line 201)
**Action:** ADD new function

### New function

```c
/*
 * What:       bar-to-step paste request. Identical to ccCopy_requestPaste()
 *             except the job kind is CC_KIND_BAR_TO_STEP instead of
 *             src->kind (CC_KIND_BAR). This tells the geometry function to
 *             use bar source coordinates (src->start * 16) but step
 *             destination coordinates (job->start as an absolute step).
 * Why:        ccCopy_requestPaste() always sets job.kind = src->kind. For
 *             cross-kind pastes the job kind must differ from the source
 *             kind so the geometry function knows the destination is in step
 *             space, not bar space.
 * Inputs:     src (CC_KIND_BAR source), selection (cc_copy_step_sel_t),
 *             dst_scene, dst_track, dst_start (absolute step 0..127).
 * Outputs:    1 if a job was queued, 0 if dropped (queue full or identical).
 * Accessors:  ccSvc_enqueue(), ccSvc_pasteTriggersNow().
 * Affiliates: ccCopy_requestPaste(), cc_copySeq().
 */
uint8_t ccCopy_requestBarToStep(const cc_source_t *src, uint8_t selection,
                                uint8_t dst_scene, uint8_t dst_track,
                                uint8_t dst_start)
{
    cc_job_t job;
    uint8_t slot;

    if (!src || src->kind != CC_KIND_BAR)
        return 0u;
    /*
     * Identical-paste check: a bar-to-step paste is never identical to its
     * source because the destination is in step space and the source is in
     * bar space. Even if the step happens to fall inside the source bar(s),
     * the operation is meaningful (it copies the bar content to a potentially
     * different alignment).
     */
    job.op = (uint8_t)(CC_JOB_PASTE | (selection & CC_JOB_SEL_MASK));
    job.kind = CC_KIND_BAR_TO_STEP;
    job.scene = dst_scene;
    job.track = dst_track;
    job.start = dst_start;
    job.end = dst_start;
    slot = ccSvc_enqueue(&job);
    if (slot == 0u)
        return 0u;
    (void)ccSvc_pasteTriggersNow((uint8_t)(slot - 1u));
    return 1u;
}
```

### Declaration in copyOps.h

**File:** `Core/Menu/CopyClear/copyOps.h`
**Location:** after the `ccCopy_requestPaste()` declaration
**Action:** ADD declaration

```c
/*
 * What:       bar-to-step paste request. Like ccCopy_requestPaste() but the
 *             job kind is CC_KIND_BAR_TO_STEP: bar source coordinates, step
 *             destination coordinates.
 * Why:        cross-kind paste for bar source → step destination.
 * Inputs:     src (CC_KIND_BAR), selection, dst_scene, dst_track,
 *             dst_start (absolute step).
 * Outputs:    1 if queued, 0 if dropped.
 * Affiliates: ccCopy_requestPaste(), CC_KIND_BAR_TO_STEP.
 */
uint8_t ccCopy_requestBarToStep(const cc_source_t *src, uint8_t selection,
                                uint8_t dst_scene, uint8_t dst_track,
                                uint8_t dst_start);
```

---

## Change 4 — Geometry: `ccSvc_pasteGeometry()` handles `CC_KIND_BAR_TO_STEP`

**File:** `Core/Menu/CopyClear/copyClearService.c`
**Location:** `ccSvc_pasteGeometry()`, lines 325–352 (the switch on
`job->kind`)
**Action:** MODIFY — add a new case before the default

### Before (lines 332–343, the CC_KIND_BAR case)

```c
    case CC_KIND_BAR:
        /* Bar ranges reverse completely (spec §4.2). */
        if (src->start <= src->end) {
            g->src_first = (uint8_t)(src->start * NUM_STEPS_PER_BAR);
            g->dir = 1;
        } else {
            g->src_first = (uint8_t)(src->start * NUM_STEPS_PER_BAR +
                                     NUM_STEPS_PER_BAR - 1u);
            g->dir = -1;
        }
        g->count = (uint8_t)((hi - lo + 1u) * NUM_STEPS_PER_BAR);
        g->dst_first = (uint8_t)(job->start * NUM_STEPS_PER_BAR);
        break;
```

### After (CC_KIND_BAR unchanged, new case added after it)

```c
    case CC_KIND_BAR:
        /* Bar ranges reverse completely (spec §4.2). */
        if (src->start <= src->end) {
            g->src_first = (uint8_t)(src->start * NUM_STEPS_PER_BAR);
            g->dir = 1;
        } else {
            g->src_first = (uint8_t)(src->start * NUM_STEPS_PER_BAR +
                                     NUM_STEPS_PER_BAR - 1u);
            g->dir = -1;
        }
        g->count = (uint8_t)((hi - lo + 1u) * NUM_STEPS_PER_BAR);
        g->dst_first = (uint8_t)(job->start * NUM_STEPS_PER_BAR);
        break;
    /*
     * What:       bar-to-step cross-kind paste geometry. The source uses bar
     *             coordinates (identical to CC_KIND_BAR: src->start * 16,
     *             direction and count scaled by 16). The destination uses
     *             step coordinates (job->start is an absolute step, used
     *             directly — NOT multiplied by 16).
     * Why:        the user copied a bar source and pasted to a step
     *             destination. The source data is the same bar(s); only the
     *             destination alignment changes.
     * Inputs:     job->start (absolute step 0..127), src->start/end (bar
     *             indices 0..7).
     * Outputs:    g->dst_first = job->start (absolute step); source fields
     *             identical to CC_KIND_BAR.
     * Affiliates: CC_KIND_BAR (source geometry), CC_KIND_STEP (destination
     *             geometry model).
     */
    case CC_KIND_BAR_TO_STEP:
        if (src->start <= src->end) {
            g->src_first = (uint8_t)(src->start * NUM_STEPS_PER_BAR);
            g->dir = 1;
        } else {
            g->src_first = (uint8_t)(src->start * NUM_STEPS_PER_BAR +
                                     NUM_STEPS_PER_BAR - 1u);
            g->dir = -1;
        }
        g->count = (uint8_t)((hi - lo + 1u) * NUM_STEPS_PER_BAR);
        g->dst_first = job->start;
        break;
```

The only difference from `CC_KIND_BAR` is the last line:
`g->dst_first = job->start` (not `job->start * NUM_STEPS_PER_BAR`).

---

## Change 5 — Early triggers: `ccSvc_pasteTriggersNow()` accepts the new kind

**File:** `Core/Menu/CopyClear/copyClearService.c`
**Location:** `ccSvc_pasteTriggersNow()`, line 471 (the kind gate)
**Action:** MODIFY — add `CC_KIND_BAR_TO_STEP` to the acceptance list

### Before (line 471)

```c
    if ((job->op & CC_JOB_CLASS_MASK) != CC_JOB_PASTE ||
        (job->kind != CC_KIND_STEP && job->kind != CC_KIND_BAR &&
         job->kind != CC_KIND_TRACK))
        return 0u;
```

### After

```c
    /*
     * What:       early trigger gate. Accepts step, bar, bar-to-step and
     *             track paste jobs; other kinds (Scene, FX step) have no
     *             Pattern triggers to preview.
     * Why:        CC_KIND_BAR_TO_STEP is a Pattern paste and needs the same
     *             immediate trigger-bit write as CC_KIND_BAR and CC_KIND_STEP.
     * Affiliates: ccSvc_pasteGeometry(), ccCopy_requestBarToStep().
     */
    if ((job->op & CC_JOB_CLASS_MASK) != CC_JOB_PASTE ||
        (job->kind != CC_KIND_STEP && job->kind != CC_KIND_BAR &&
         job->kind != CC_KIND_BAR_TO_STEP && job->kind != CC_KIND_TRACK))
        return 0u;
```

---

## Change 6 — Dispatch: `ccCopy_runJob()` routes bar-to-step to the paste engine

**File:** `Core/Menu/CopyClear/copyOps.c`
**Location:** `ccCopy_runJob()`, lines 1154–1157 (the switch on `job->kind`)
**Action:** MODIFY — add `CC_KIND_BAR_TO_STEP` alongside `CC_KIND_STEP` and
`CC_KIND_BAR`

### Before (lines 1154–1157)

```c
    switch (job->kind) {
    case CC_KIND_STEP:
    case CC_KIND_BAR:
        return ccSvc_runPatternPaste(job);
```

### After

```c
    /*
     * What:       CC_KIND_BAR_TO_STEP is a Pattern paste (bar source, step
     *             destination) and uses the same engine as CC_KIND_STEP and
     *             CC_KIND_BAR.
     * Why:        ccSvc_runPatternPaste() dispatches entirely on the resolved
     *             geometry; the new kind produces correct geometry through
     *             ccSvc_pasteGeometry(). No per-kind logic inside the engine
     *             depends on the value.
     * Affiliates: ccSvc_pasteGeometry(), CC_KIND_BAR_TO_STEP.
     */
    switch (job->kind) {
    case CC_KIND_STEP:
    case CC_KIND_BAR:
    case CC_KIND_BAR_TO_STEP:
        return ccSvc_runPatternPaste(job);
```

---

## Change 7 — Spec update: `COPYCLEAR_UTILITIES.md`

**File:** `knowledge_files/specification_reference/COPYCLEAR_UTILITIES.md`

### 7a — §11.3 copy routing table (line 600)

**Before:**

```
| SEQ (VOICE/STEP) | source | **paste** | ignored | ignored | ignored | ignored |
```

**After:**

```
| SEQ (VOICE/STEP) | source | **paste** | **paste** (bar-to-step: bar content starting at the pressed step) | ignored | ignored | ignored |
```

### 7b — §6.3 (line ~273): add bar-to-step paragraph

Insert after the "Step and bar family" heading or after the bar paste
description:

```
**Bar-to-step paste (S077 P5):** when a bar or bar range is the copy source,
a SEQ press in VOICE/STEP mode pastes the bar content starting at the pressed
step (cross-kind paste). The source geometry is identical to a bar-to-bar
paste (bar indices × 16); only the destination is in step space (absolute
step, not multiplied). The reverse (step source → SELECT press) is
navigation, not a paste. The job kind is `CC_KIND_BAR_TO_STEP`.
```

### 7c — §13 executor table: update the `ccSvc_runPatternPaste` description

**Before (line ~109 of copyClearService.h, mirrored in §13):**

```
step, range, bar and `copy track` pastes
```

**After:**

```
step, range, bar, bar-to-step and `copy track` pastes
```

### 7d — §20 History: new entry

```
- **S077 P5 (2026-10-08):** bar-to-step cross-kind paste. A bar/bar-range
  copy source can be pasted to a step destination (SEQ press in VOICE/STEP
  mode). New kind `CC_KIND_BAR_TO_STEP`, new request function
  `ccCopy_requestBarToStep()`. Geometry, early triggers, dispatch and flash
  updated.
```

---

## Change 8 — Comment updates

### 8a — `copyClearSession.h` `cc_kind_t` enum comment (line 35)

Already covered in Change 1 — the comment above the enum is updated there.

### 8b — `copyClearService.c` `ccSvc_pasteGeometry()` header comment (line 309)

**Before (line 309):**

```c
/* Geometry of a step/range/bar/track paste. */
```

**After:**

```c
/* Geometry of a step/range/bar/bar-to-step/track paste. */
```

### 8c — `copyClearService.h` `cc_job_t` comment (lines 32–37)

**Before (lines 35–36):**

```c
 * Scene/track (clear). start: destination start (paste: absolute step, bar,
 * track, Scene or FX step) or object start. end: object end (clear); unused
```

**After:**

```c
 * Scene/track (clear). start: destination start (paste: absolute step, bar,
 * bar-to-step absolute step, track, Scene or FX step) or object start.
 * end: object end (clear); unused
```

### 8d — `copyClearService.h` `ccSvc_runPatternPaste` comment (line 109)

**Before (line 109):**

```c
 * ccSvc_runPatternPaste(): step, range, bar and `copy track` pastes (spec
```

**After:**

```c
 * ccSvc_runPatternPaste(): step, range, bar, bar-to-step and `copy track` pastes (spec
```

### 8e — `copyOps.h` `ccCopy_runJob` comment (lines 156–160)

**Before (lines 156–158):**

```c
 * Run one queued paste (the job at the queue head). Dispatches by source
 * kind and selection: Pattern pastes to the service engine, `copy
 * instrument`, `copy inst -> morph`, the PERF Scene pastes (including
```

**After:**

```c
 * Run one queued paste (the job at the queue head). Dispatches by source
 * kind and selection: Pattern pastes (step, bar, bar-to-step, track) to the
 * service engine, `copy instrument`, `copy inst -> morph`, the PERF Scene
 * pastes (including
```

---

## Summary of all changed files

| # | File | Lines | Action |
|---|------|-------|--------|
| 1 | `Core/Menu/CopyClear/copyClearSession.h` | 35–43 | ADD `CC_KIND_BAR_TO_STEP` to `cc_kind_t`; update enum comment |
| 2 | `Core/Menu/CopyClear/copyClearSession.c` | 337–346 | MODIFY `cc_copySeq()`: add bar-to-step paste routing |
| 3a | `Core/Menu/CopyClear/copyOps.c` | after 201 | ADD `ccCopy_requestBarToStep()` function |
| 3b | `Core/Menu/CopyClear/copyOps.h` | after 98 | ADD `ccCopy_requestBarToStep()` declaration |
| 4 | `Core/Menu/CopyClear/copyClearService.c` | 332–343 | ADD `CC_KIND_BAR_TO_STEP` case in `ccSvc_pasteGeometry()` |
| 5 | `Core/Menu/CopyClear/copyClearService.c` | 471 | MODIFY `ccSvc_pasteTriggersNow()`: accept new kind |
| 6 | `Core/Menu/CopyClear/copyOps.c` | 1154–1157 | MODIFY `ccCopy_runJob()`: route `CC_KIND_BAR_TO_STEP` to paste engine |
| 7 | `knowledge_files/.../COPYCLEAR_UTILITIES.md` | 600, ~273, ~109, §20 | MODIFY spec: routing table, bar-to-step note, executor, history |
| 8a | `copyClearSession.h` | 35 | (covered by Change 1) |
| 8b | `copyClearService.c` | 309 | MODIFY geometry function header comment |
| 8c | `copyClearService.h` | 35–36 | MODIFY `cc_job_t` comment: bar-to-step start |
| 8d | `copyClearService.h` | 109 | MODIFY `ccSvc_runPatternPaste` comment |
| 8e | `copyOps.h` | 156–158 | MODIFY `ccCopy_runJob` comment |

**Total: 5 source files (2 .c, 2 .h, 1 .md spec); 1 new function, 1 new
enum constant, 1 geometry case, 1 dispatch case, 1 routing change, 1
early-trigger gate update, comment and spec updates.**

No new RAM beyond one enum constant.  No new ISR/DSP path.  No file-format
change.  The paste engine (`ccSvc_runPatternPaste()`) body is not modified —
it operates on the resolved geometry which the new kind produces correctly.
The overlap detection, snapshot path, check/place phases, and track-length
extension all work transparently on the resolved geometry.

---

## Post-Implementation Assessment

**Date:** 2026-10-08
**Reviewer:** Claude (automated code verification)

### Verification method

Every file named in the 8-change schedule was read and compared against the
schedule's before/after blocks plus the implementation log's noted deviations.

### Results by file

| File | Changes | Status |
|------|---------|--------|
| `copyClearSession.h` — `cc_kind_t` enum (change 1) | `CC_KIND_BAR_TO_STEP` appended as value 6; full contract comment block present; short enum comment updated to mention cross-kind paste | **OK** |
| `copyClearSession.c` — `cc_copySeq()` (change 2) | `else if (cc_source.kind == CC_KIND_BAR)` branch added after the `CC_KIND_STEP` block; calls `ccCopy_requestBarToStep()` with `buttonHandler_visibleStep(index)` as destination; flash passes `CC_KIND_STEP` with count `cc_sourceLength() * NUM_STEPS_PER_BAR`; comment block present | **OK** |
| `copyOps.c` — `ccCopy_requestBarToStep()` (change 3) | New function at line 219; guards `src->kind != CC_KIND_BAR`; sets `job.kind = CC_KIND_BAR_TO_STEP`; no identical-paste check (correct — cross-kind is never identical); enqueues and fires early triggers; comment block present; `Outputs` line correctly says "queue full" not "or identical" | **OK** |
| `copyOps.h` — declaration (change 3b) | Declaration at line 110 with comment block matching the `.c` twin | **OK** |
| `copyClearService.c` — `ccSvc_pasteGeometry()` (change 4) | `CC_KIND_BAR_TO_STEP` case at line 361; source geometry identical to `CC_KIND_BAR` (bar × 16, reverse handling); `g->dst_first = job->start` (not × 16); comment block present | **OK** |
| `copyClearService.c` — `ccSvc_pasteTriggersNow()` (change 5) | Gate at line 506–508 now includes `job->kind != CC_KIND_BAR_TO_STEP`; comment block present | **OK** |
| `copyOps.c` — `ccCopy_runJob()` dispatch (change 6) | `CC_KIND_BAR_TO_STEP` falls through to `ccSvc_runPatternPaste(job)` at line 1212; comment block present | **OK** |
| `COPYCLEAR_UTILITIES.md` — §6.3 bar-to-step paragraph (change 7b) | Present at line 294, describes cross-kind paste, mentions `CC_KIND_BAR_TO_STEP` | **OK** |
| `COPYCLEAR_UTILITIES.md` — §11.3 routing table (change 7a) | Line 607: bar/range column now reads `**paste** (bar-to-step: ...)` | **OK** |
| `COPYCLEAR_UTILITIES.md` — §12.5 heading (change 7c) | Line 779: heading includes "bar-to-step" in the list | **OK** |
| `COPYCLEAR_UTILITIES.md` — §20 history (change 7d) | Line 1199: S077 P5 entry present, dated 2026-10-08 | **OK** |
| `copyClearService.c` — geometry header comment (change 8b) | Line 309: reads "step/range/bar/bar-to-step/track paste" | **OK** |
| `copyClearService.h` — `cc_job_t` comment (change 8c) | Line 36: "bar-to-step absolute step" added | **OK** |
| `copyClearService.h` — `ccSvc_runPatternPaste` comment (change 8d) | Line 110: "bar, bar-to-step and `copy track`" | **OK** |
| `copyOps.h` — `ccCopy_runJob` comment (change 8e) | Line 171: "Pattern pastes (step, bar, bar-to-step, track)" | **OK** |

### Correctness notes

1. **Geometry is the only kind-sensitive point in the paste engine.** The paste
   engine body (`ccSvc_runPatternPaste()`) never reads `job->kind` after
   calling `ccSvc_pasteGeometry()`. The `CC_KIND_BAR_TO_STEP` case produces
   the correct `cc_paste_geo_t`: bar source coordinates (src × 16, dir, count
   × 16) and step destination coordinates (`job->start` as absolute step).
   Every downstream consumer — overlap detection, snapshot, check, place,
   track-length extension — operates on the resolved geometry.

2. **Overlap detection works correctly.** `ccSvc_pasteOverlaps()` skips only
   `CC_KIND_TRACK`; for `CC_KIND_BAR_TO_STEP` it runs the step-level overlap
   test on the resolved geometry, which is correct — a bar-to-step paste on
   the same track can overlap the source bars.

3. **Track-length extension works correctly.** The finish phase checks
   `job->kind == CC_KIND_TRACK` for the track-settings path; anything else
   (including `CC_KIND_BAR_TO_STEP`) falls into the track-length extension
   using `g.dst_first + g.count - 1`, which is in absolute steps for all
   non-track kinds.

4. **Early triggers work correctly.** The gate now accepts the new kind, and
   `ccSvc_pasteTriggersNow()` operates entirely on the resolved geometry from
   `ccSvc_pasteGeometry()`.

5. **Flash feedback uses `CC_KIND_STEP`.** The routing code in `cc_copySeq()`
   passes `CC_KIND_STEP` to `cc_flashObject()`, which flashes the destination
   SEQ LEDs (not SELECT LEDs). The step count is `cc_sourceLength() *
   NUM_STEPS_PER_BAR`, correctly expanding bars to steps.

6. **No identical-paste false positive.** `ccCopy_requestBarToStep()` has no
   identical-paste check because a bar-to-step paste is never identical (the
   source is in bar space, the destination in step space). The existing
   `ccCopy_requestPaste()` is not called for bar-to-step, so its bar-vs-bar
   identical check is not triggered.

7. **Reverse direction unchanged.** A step source with a SELECT press still
   returns 0 (navigate) from `cc_copySelect()` line 392–394. A bar source
   with a SELECT press still triggers bar-to-bar paste from
   `cc_copySelect()` line 395–401.

8. **Trace encoding fits.** `CC_KIND_BAR_TO_STEP` = 6, which fits in the
   3-bit `job->kind & 0x07u` trace field in `ccTrace_job()`.

9. **Build delta.** text −32 B vs P4 close — the new function and geometry
   case are compact; no data or bss change.

### Implementation log deviation notes (confirmed correct)

- Schedule Change 7c referenced "§13 executor table" — the actual spec
  section is §12.5 (a heading, not a table row). Updated correctly.
- The `Outputs` line in the function comment correctly says "queue full"
  without "or identical", since no identical path exists.

### Verdict

All 8 scheduled changes plus comment updates are correctly applied. The build
is clean with a net text reduction. Ready for hardware verification of
checklist items 5.1–5.9.
