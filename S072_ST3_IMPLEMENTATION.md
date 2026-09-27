# S072 Step 3 — Implementation Schedule: Effect Data Model

**Scope:** `EFFECTS_BUS_FEATURE_PLAN.md` §7, §10.1, §15, and §17.1 Step 3.
This step adds:

- the Scene-retained Effect record (`effect_record_t` in `scene_t`);
- the Scene parameter `effect_morph_amount`;
- the SceneData setters and getters that are the only mutation boundary for
  that data;
- the AutoSave wire geometry, live getters, and dirty markers for the Effect
  region and the new Scene parameter;
- the automation ID block-7 constants;
- the reserved `fxm` Scene target row (ID 404).

**What it does not add:**

- the registry, EffectsManager, or DSP (Step 4);
- the bus (Step 5);
- file I/O, HCNAMES, or the AutoSave Effect *reader* (Step 6);
- UI (Step 7), the sequencer (Step 8), or the automation/LFO apply paths
  (Step 9).

After this step every Scene holds a default `off` Effect record. SceneData is
the only retained mutation boundary; future Effect registry/runtime code can
use these setters without adding a second owner.

**Status:** source/build implementation complete. The clean production link
passes the source-scope gates below. Hardware/card validation remains pending
because this session has no device/card test run.

**Line numbers** refer to the working tree after Step 2 (base `a0531ae` plus
the uncommitted Steps 1–2). Steps 1–2 were preserved while implementing this
schedule; the source/build notes at the end record the resulting working-tree
state.

---

## 0. Decisions and acknowledgements

### A1 — RAM: +16 B over the approved figure (acknowledged)

Measured with the target compiler (`arm-none-eabi-gcc`, project flags) using
the exact structs below:

| | Now | After Step 3 |
|---|---|---|
| `sizeof(scene_settings_t)` | 40 | 41 |
| `sizeof(effect_record_t)` | — | 420 |
| `offsetof(scene_t, effect)` | — | 42 (one pad byte after settings) |
| `offsetof(scene_t, kit)` | 40 | 462 (`kit_t` is 2-byte aligned; no further pad) |
| `sizeof(scene_t)` | 1,200 | **1,622** |
| `scenes[16]` | 19,200 | **25,952 (+6,752 B SRAM1 `.bss`)** |

The plan (§16, items 1–2) approved 6,720 + 16 = 6,736 B. The difference of
**+16 B** is one padding byte per Scene after the 41-byte settings block.
Owner: SceneData. Region: SRAM1 `.bss`. Lifetime: firmware. The user
acknowledged this SRAM1 increase during the implementation session; SRAM1
headroom stays about 81 KB.

### A2 — Common-parameter defaults (confirm)

The registry arrives in Step 4, so this step needs defaults for the three
common Effect parameters. They are written for every new or reset Scene and
will be reused by the Step 4 common-parameter macro:

| Index | Parameter | Default | Meaning |
|---|---|---|---|
| 0 | `effect_audio_out` | 0 | `MIXER_ROUTING_DAC1_STEREO` (St1), the same route domain as voices |
| 1 | `effect_level` | **127** | unity return (0..127 like voice `vol`) |
| 2 | `effect_pan` | 64 | centre (voice pan convention) |

The plan's §14.1 example used `effect_level=100`, but only as illustration.
127 (unity) is proposed. It is audible only once a type other than `off` is
selected and sends are raised.

### Deviations from the plan (for the record, no decision needed)

1. **AutoSave layout: 419 live parameter bytes, not 420, at region
   offset 11.**
   - The 3-byte type token and 8-byte name keep their own region offsets
     (0–2 and 3–10), as Instruments do.
   - The live "parameters" are the three sequence settings, 64 normal values,
     64 Morph values, and 16 × 18 step bytes. The plan's spare "reserved" byte
     is dropped.
   - The parameters end at offset 429, leaving 82 bytes spare. §4 has the
     table.
2. **The type token and name bytes are not live in Step 3.** The token needs
   the Step 4 registry, and the name needs the Step 6 HCNAMES rows. Their
   getters report "absent", so the drain leaves them zero.
3. **No AutoSave header version bump** (supersedes plan §15.2). Older records
   have zeros in the Effect region, and zero bytes are not a valid type token.
   The Step 6 Effect reader will therefore require a valid token and skip the
   region otherwise, keeping the file-loaded Effect. A bump would instead
   discard every existing AutoSave record on first boot. The Scene-parameter
   byte at index 40 reads as 0 from old records (zero-filled reserve), which is
   the correct default.
4. **`fxm` (ID 404) is reserved now with use flags 0.** It stays invisible in
   the `scn` automation, LFO, and velocity pickers until Step 9 gives it an
   apply path. All six `switch (descriptor->kind)` sites (`sequencer.c:307`,
   `:813`; `menu.c:1881`, `:8695`; `InstrumentManager.c:2317`, `:2353`) already
   have `default:` cases, and every enumeration filters by use flag, so no
   other code changes.

---

## 1. Change index

| # | File | Line(s) | Op | Summary |
|---|---|---|---|---|
| 1 | `Core/DSP/Effects/EffectTypes.h` | new | add | Effect constants, run modes, record structs, block-7 ID helpers |
| 2 | `Core/Bank/Scene/SceneData.h` | after 5 | add | `#include "EffectTypes.h"` |
| 3 | `Core/Bank/Scene/SceneData.h` | after 181 | add | `scene_settings_t.effect_morph_amount` |
| 4 | `Core/Bank/Scene/SceneData.h` | 216–226 | modify | Replace the "future Effect" comment with `effect_record_t effect;` |
| 5 | `Core/Bank/Scene/SceneData.h` | after 394 | add | Effect accessor and setter declarations |
| 6 | `Core/Bank/Scene/SceneData.c` | after 4 | add | ID-layout static asserts |
| 7 | `Core/Bank/Scene/SceneData.c` | after 83 | add | `scene_storeEffectByte()` helper |
| 8 | `Core/Bank/Scene/SceneData.c` | after 561 | add | Effect accessors and setters, `scene_effectRecordDefaults()` |
| 9 | `Core/Bank/Scene/SceneData.c` | after 596 | add | `scene_initAll()` seeds each Effect record |
| 10 | `Core/Bank/Scene/Autosave.h` | 173–179 | modify | Region comment: Scene 0..40, Effect layout |
| 11 | `Core/Bank/Scene/Autosave.h` | 184 | modify | `AUTOSAVE_SCENE_PARAMETER_LIVE_BYTES` 40 → 41 |
| 12 | `Core/Bank/Scene/Autosave.h` | 186–190 | modify | Effect region geometry |
| 13 | `Core/Bank/Scene/Autosave.h` | 199–212 | modify | Domain comment: Effect is now live |
| 14 | `Core/Bank/Scene/Autosave.h` | 232 | modify | Scene enum: `EFFECT_MORPH = 40`, `COUNT = 41` |
| 15 | `Core/Bank/Scene/Autosave.h` | after 239 | add | `autosave_effect_parameter_t` + step constants |
| 16 | `Core/Bank/Scene/Autosave.c` | after 22 (includes) | add | Effect geometry static asserts |
| 17 | `Core/Bank/Scene/Autosave.c` | 873–879 | modify | Scene getter: explicit index-40 branch |
| 18 | `Core/Bank/Scene/Autosave.c` | 884–903 | modify | Implement `autosave_getEffectParameter()` |
| 19 | `Core/Bank/Scene/Autosave.c` | 1029–1036 | modify | Router comment: live Effect bytes |
| 20 | `Core/Bank/Scene/Autosave.c` | 1300–1306 | modify | Boot reader: index-40 branch |
| 21 | `Core/Bank/Scene/Autosave.c` | 1661–1667 | modify | `markEffectParameterDirty` comment |
| 22 | `Core/Bank/Scene/Autosave.c` | 1936–1941 | modify | `markEffectDirty` comment |
| 23 | `Core/Bank/Scene/SceneModTargets.h` | 39 | modify | Rename unused `KIND_EFFECT_PARAMETER` → `KIND_EFFECT_MORPH` |
| 24 | `Core/Bank/Scene/SceneModTargets.c` | after 105 | add | Reserved `fxm` row (ID 404, flags 0) |
| 25 | `Core/Bank/Scene/SceneModTargets.c` | after 106 | add | Block-6 capacity static assert |
| 26 | `knowledge_files/specification_reference/AUTOSAVE.md` | 92, 153–154 + layout | modify | Effect region live, Scene 41 |
| 27 | `knowledge_files/specification_reference/SRAM_MANIFEST.md` | 60 | modify | `scenes` 25,952 B |
| 28 | `knowledge_files/specification_reference/BANK_PRESET_ARCHITECTURE.md` | 41, 279 | modify | Effect in the hierarchy; 404 reserved |
| 29 | `knowledge_files/specification_reference/MODULE_INTERCHANGE_SPEC.md` | SceneData / SceneModTargets sections | modify | Effect API, EffectTypes |
| 30 | `EFFECTS_BUS_FEATURE_PLAN.md` | §15 | modify | Deviations 1–3 |
| 31 | `MEMORY.md` | Volatile Notes | add | One-line Step 3 note |

---

## 2. New file `Core/DSP/Effects/EffectTypes.h`

```c
/*
 * Core/DSP/Effects/EffectTypes.h
 *
 *  Created on: 27.09.2026
 *  (standard LXR02 Open-Source licence header, as in FxBuffer.h)
 */
#ifndef EFFECT_TYPES_H_
#define EFFECT_TYPES_H_

#include <stdint.h>

/*
 * Effect data contract shared by SceneData, AutoSave, and (from Step 4) the
 * EffectsManager registry, storage, Menu, and sequencer.
 *
 * What: constants, enums, and the retained per-Scene Effect record layout,
 * with no function dependencies. Why a separate header: SceneData.h must
 * embed effect_record_t in scene_t, while EffectsManager.c (Step 4) includes
 * SceneData.h. Keeping the data contract here avoids a header cycle, just as
 * InstrumentManager.h carries only types/declarations for SceneData.
 * Authority: EFFECTS_BUS_FEATURE_PLAN.md §7, §10, §11.
 */

/* Parameters ------------------------------------------------------------ */

/* Per-Effect parameter cells (normal and Morph images). Indices 0..2 are the
 * common parameters every type exposes; 3..63 are type-specific (plan A1). */
#define EFFECT_PARAM_COUNT              64u
#define EFFECT_COMMON_PARAM_AUDIO_OUT    0u   /* key effect_audio_out, short `out` */
#define EFFECT_COMMON_PARAM_LEVEL        1u   /* key effect_level,     short `vol` */
#define EFFECT_COMMON_PARAM_PAN          2u   /* key effect_pan,       short `pan` */
#define EFFECT_COMMON_PARAM_COUNT        3u

/* Common-parameter defaults (S072 ST3 §0 A2). Route 0 = St1, same domain as
 * voice audio_out; level 127 = unity return; pan 64 = centre. The Step 4
 * common-parameter macro must use these same values. */
#define EFFECT_COMMON_DEFAULT_AUDIO_OUT  0u
#define EFFECT_COMMON_DEFAULT_LEVEL      127u
#define EFFECT_COMMON_DEFAULT_PAN        64u

/* Types ---------------------------------------------------------------- */

/* Registry id stored in effect_record_t.type (runtime only; files and
 * AutoSave persist the 3-character token). Id 0 is always `off`; Step 4 adds
 * the registry and static-asserts EFFECT_TYPE_OFF == FXBUF_EFFECT_TYPE_NONE. */
typedef uint8_t effect_type_id_t;
#define EFFECT_TYPE_OFF                  0u

/* FX sequencer ---------------------------------------------------------- */

#define EFFECT_SEQ_STEP_COUNT            16u
#define EFFECT_SEQ_LANE_COUNT            16u
/* Lane 0 is always Effect Morph (plan A2); lanes 1..15 are type-defined. */
#define EFFECT_SEQ_LANE_MORPH            0u
#define EFFECT_SEQ_LENGTH_MIN            1u
#define EFFECT_SEQ_LENGTH_MAX            16u
#define EFFECT_SEQ_LENGTH_DEFAULT        16u

/* Run modes (plan A9: no `off`; default fwd). Values are stored in the
 * record/AutoSave; files use the text tokens fwd/rev/pip/rnd/sel (Step 6). */
typedef enum {
    EFFECT_SEQ_RUN_FWD = 0,
    EFFECT_SEQ_RUN_REV,
    EFFECT_SEQ_RUN_PIP,
    EFFECT_SEQ_RUN_RND,
    EFFECT_SEQ_RUN_SEL,
    EFFECT_SEQ_RUN_MODE_COUNT
} effect_seq_run_mode_t;

/* Step-scale index into the shared 96-PPQ table introduced in Step 8
 * (plan §11.3, A10; tracks and FX share it). Index 4 is 1/16 in that table's
 * duration order: 1/64, 1/32T, 1/32, 1/16T, 1/16, 1/8T, 1/16., 1/8, 1/4T,
 * 1/8., 1/4, 1/2, 1 bar, 2 bars. Step 8 static-asserts both values. */
#define EFFECT_SEQ_SCALE_COUNT           14u
#define EFFECT_SEQ_SCALE_DEFAULT         4u

/*
 * One FX-sequencer step.
 *
 * What: bit L of lock_mask means lane L is locked on this step; value[L] is
 * that lane's value in the target parameter's own 0..255 domain. Values are
 * retained even when unlocked, so re-locking restores them (plan §11.1).
 * AutoSave serialises lock_mask little-endian (low byte first), then the 16
 * values: 18 bytes per step.
 */
typedef struct {
    uint16_t lock_mask;
    uint8_t  value[EFFECT_SEQ_LANE_COUNT];
} effect_seq_step_t;

/*
 * Scene-retained Effect record (one per resident Scene, in scene_t).
 *
 * What: the Effect type id, the three sequence settings, the normal and Morph
 * parameter images, and the 16-step sequence. Why here: an Effect belongs
 * solely to its Scene (plan §5.1); Kit loads never touch it. Effect Morph
 * AMOUNT is a Scene setting (scene_settings_t.effect_morph_amount), not part
 * of this record or the .fx file (user rule).
 *
 * Ownership: SceneData setters are the only writers (AutoSave marking and
 * card-clean invalidation). Readers: AutoSave, and from Step 4 EffectsManager,
 * storage, Menu, and sequencer, through scene_effectConst().
 * Size: 420 bytes, 2-byte aligned (static-asserted below; see SRAM_MANIFEST).
 */
typedef struct {
    effect_type_id_t type;
    uint8_t seq_run_mode;                           /* effect_seq_run_mode_t */
    uint8_t seq_length;                             /* 1..16 */
    uint8_t seq_step_scale;                         /* 0..EFFECT_SEQ_SCALE_COUNT-1 */
    uint8_t normal[EFFECT_PARAM_COUNT];
    uint8_t morph[EFFECT_PARAM_COUNT];
    effect_seq_step_t steps[EFFECT_SEQ_STEP_COUNT];
} effect_record_t;

_Static_assert(sizeof(effect_seq_step_t) == 18u,
               "FX sequence step is 16 values + 16-bit mask (plan §11.1)");
_Static_assert(sizeof(effect_record_t) == 420u,
               "effect_record_t size is recorded in SRAM_MANIFEST.md");

/* Automation / modulation target IDs (block 7) ----------------------------- */

/*
 * The 9-bit Pattern target is a 3-bit block + 6-bit parameter (user rule
 * A23): blocks 0..5 voices, block 6 Scene (384..447), block 7 Effect
 * (448..511). ID 511 (0x1FF) is also PAT_AUTOMATION_TARGET_OFF, so Effect
 * local 63 can never be a Pattern automation target (user rule F2/G2: 63
 * automatable Effect parameters). Modulation (LFO) targets use local tokens
 * and may address local 63. SceneData.c static-asserts these against
 * InstrumentManager.h and PatternData.h.
 */
#define EFFECT_TARGET_ID_BASE               448u
#define EFFECT_TARGET_ID_COUNT              64u
#define EFFECT_TARGET_PATTERN_LOCAL_LIMIT   63u   /* Pattern-automatable locals 0..62 */

/* Nonzero when id lies in block 7 (448..511). Pattern callers must reject
 * PAT_AUTOMATION_TARGET_OFF before calling this. */
static inline uint8_t effectTarget_isEffectId(uint16_t id)
{
    return (uint8_t)(id >= EFFECT_TARGET_ID_BASE &&
                     id < EFFECT_TARGET_ID_BASE + EFFECT_TARGET_ID_COUNT);
}

/* Local Effect parameter index (0..63) of a block-7 id. */
static inline uint8_t effectTarget_local(uint16_t id)
{
    return (uint8_t)(id - EFFECT_TARGET_ID_BASE);
}

/* Canonical block-7 id for a local index 0..63 (no range check). */
static inline uint16_t effectTarget_id(uint8_t local)
{
    return (uint16_t)(EFFECT_TARGET_ID_BASE + local);
}

#endif /* EFFECT_TYPES_H_ */
```

The new folder is already on the include path: Step 1 added
`-ICore/DSP/Effects` to the Makefile. No Makefile change is needed.

---

## 3. `SceneData.h`

### 3.1 After line 5 (`#include "PatternData.h"`) — add

```c
#include "EffectTypes.h"
```

### 3.2 After line 181 (`uint8_t midi_note[NUM_TRACKS];`) — add

```c
    /*
     * Scene-level Effect Morph amount, 0..255.
     *
     * What: how far the Scene's Effect is interpolated from its normal image
     * toward its Morph image (effect_record_t.normal/morph). Why a Scene
     * setting and not part of the Effect record or .fx file: user rule —
     * Effect Morph is a Scene parameter, exactly like the per-voice Morph
     * amounts above, and the PERF global Morph bulk-set includes it (plan A3).
     *
     * Serialization: AutoSave Scene parameter index 40
     * (AUTOSAVE_SCENE_PARAM_EFFECT_MORPH); sceneset.scg key
     * effect_morph_amount from Step 6 (optional on read, default 0).
     * Writers: scene_setEffectMorphAmount() only. Readers: from Step 4,
     * EffectsManager resolution; Scene target `fxm` (ID 404) from Step 9.
     * Placed last so every existing settings offset is unchanged.
     */
    uint8_t effect_morph_amount;
```

### 3.3 Lines 216–226 — replace the "Future retained Effect ownership" comment

After:

```c
    /*
     * Scene-retained Effect (Session 072, Effects Phase 5 step 3).
     *
     * What: the Scene's Effect type, sequence settings, normal/Morph images,
     * and 16-step FX sequence (EffectTypes.h effect_record_t, 420 B). It sits
     * between Scene settings and the Kit because an Effect belongs to the
     * Scene, never to a Kit (plan §5.1): Kit loads never touch it.
     *
     * Mutation rule: only SceneData Effect setters or
     * scene_commitEffectRecord() write it. Each marks the matching AutoSave
     * Effect bytes (autosave_markEffectParameterDirty/_markEffectDirty) and
     * invalidates the Scene's card-clean bit. Readers use scene_effectConst().
     * Affiliates: Autosave Effect geometry, EffectsManager (Step 4), storage
     * (Step 6).
     */
    effect_record_t effect;
```

### 3.4 After line 394 (last declaration, before `#endif`) — add

```c
/*
 * Scene Effect accessors (Session 072, Effects Phase 5 step 3).
 *
 * What: the single boundary for reading and mutating a resident Scene's
 * Effect record and Effect Morph amount. Every setter commits a normalized
 * byte first, then marks exactly the matching AutoSave cell(s) and invalidates
 * the Scene's card-clean bit; equal values and invalid coordinates are no-ops.
 * Parameter values are stored as given (0..255): SceneData cannot know a
 * type's domain, so EffectsManager (Step 4) clamps against the registry
 * before calling. Clients from Step 4: EffectsManager (edits, fan-out, type
 * change), storage loaders (Step 6), Menu, and the FX sequencer.
 */

/* Read-only view of one Scene's Effect record; NULL for invalid scenes. */
const effect_record_t *scene_effectConst(uint8_t scene_index);

/*
 * Fill a record with the `off` defaults: type EFFECT_TYPE_OFF, run fwd,
 * length 16, scale 1/16, common parameters 0/127/64 in both images, all other
 * cells 0, no locks. Used by scene_initAll() and, later, by loaders
 * (missing/placeholder .fx) and the Step 4 type-change transaction as a base.
 * Pure: no AutoSave marking.
 */
void scene_effectRecordDefaults(effect_record_t *record);

/*
 * Replace a Scene's whole Effect record (type change, load, copy).
 * Inputs: Scene index and a complete record. Run mode, length, and scale are
 * normalized; the type id is stored as given (registry validation is the
 * caller's). Output: 1 on success. Marks the whole Effect region
 * (autosave_markEffectDirty) and invalidates card-clean even if the bytes
 * are identical, because a whole commit is an explicit user/load action.
 */
uint8_t scene_commitEffectRecord(uint8_t scene_index,
                                 const effect_record_t *record);

/* One normal/Morph image cell; index 0..63. */
void scene_setEffectNormalParameter(uint8_t scene_index, uint8_t index,
                                    uint8_t value);
void scene_setEffectMorphParameter(uint8_t scene_index, uint8_t index,
                                   uint8_t value);

/* Sequence settings. An invalid run mode or scale index is ignored; length
 * is clamped to 1..16. */
void scene_setEffectSeqRunMode(uint8_t scene_index, uint8_t mode);
void scene_setEffectSeqLength(uint8_t scene_index, uint8_t length);
void scene_setEffectSeqStepScale(uint8_t scene_index, uint8_t scale);

/* One step's lane value (does not change its lock bit) and lock bit.
 * step 0..15, lane 0..15. Lock removal UI is deferred (A15), but the setter
 * is complete so type change and loaders need no private path. */
void scene_setEffectSeqLaneValue(uint8_t scene_index, uint8_t step,
                                 uint8_t lane, uint8_t value);
void scene_setEffectSeqLaneLocked(uint8_t scene_index, uint8_t step,
                                  uint8_t lane, uint8_t locked);

/* Scene Effect Morph amount 0..255 (AutoSave Scene parameter 40). */
void scene_setEffectMorphAmount(uint8_t scene_index, uint8_t amount);
uint8_t scene_getEffectMorphAmount(uint8_t scene_index);
```

---

## 4. AutoSave Effect region — resulting wire layout (reference for §6–§7)

Scene section offset 128, 512 bytes:

| Region offset | Bytes | Content | Live in Step 3 |
|---|---|---|---|
| 0–2 | 3 | type token (text) | no (Step 4 getter) |
| 3–10 | 8 | name (HCNAMES mirror) | no (Step 6) |
| 11 | 1 | param 0: `seq_run_mode` | yes |
| 12 | 1 | param 1: `seq_length` | yes |
| 13 | 1 | param 2: `seq_step_scale` | yes |
| 14–77 | 64 | params 3..66: `normal[0..63]` | yes |
| 78–141 | 64 | params 67..130: `morph[0..63]` | yes |
| 142–429 | 288 | params 131..418: step s at 131 + 18s, holding `mask_lo`, `mask_hi`, `value[0..15]` | yes |
| 430–511 | 82 | reserved | — |

Scene parameter allocation: indices 0..40 are live. Index 40 is
`effect_morph_amount`, at Scene section byte 50.

---

## 5. `SceneData.c`

### 5.1 After line 4 (`#include <string.h>`) — add

```c
/*
 * Automation target-ID layout guards (user rule A23, F2/G2).
 *
 * Why here: SceneData.h is the one header that already sees
 * InstrumentManager.h (voice/Scene ID ranges), PatternData.h (the 0x1FF off
 * sentinel), and EffectTypes.h (block 7). If any range moves, the build
 * fails instead of stored Pattern automation silently retargeting.
 */
_Static_assert(EFFECT_TARGET_ID_BASE == INSTRUMENT_VOICE_ID_COUNT + 64u,
               "block 6 (Scene targets, 64 IDs) must precede block 7 (Effect)");
_Static_assert(EFFECT_TARGET_ID_BASE + EFFECT_TARGET_ID_COUNT ==
                   INSTRUMENT_TOTAL_ID_COUNT,
               "block 7 must end the 9-bit target space");
_Static_assert(PAT_AUTOMATION_TARGET_OFF ==
                   EFFECT_TARGET_ID_BASE + EFFECT_TARGET_PATTERN_LOCAL_LIMIT,
               "Effect local 63 aliases the Pattern off sentinel; locals 0..62 only");
_Static_assert(EFFECT_COMMON_PARAM_COUNT < EFFECT_PARAM_COUNT,
               "common Effect parameters must leave type-specific cells");
```

### 5.2 After line 83 (end of `scene_storeKitParameterByte()`) — add

```c
/*
 * Commit one Effect-record byte and notify its AutoSave Effect cell.
 *
 * Inputs: owning Scene, address of the byte inside scenes[i].effect, its
 * AutoSave Effect parameter index (autosave_effect_parameter_t layout), and
 * the already-normalized value. Output: storage changes first, then exactly
 * that Effect bit is marked and the Scene's card-clean bit is invalidated;
 * invalid pointers and equal values do nothing. Why a third helper instead of
 * reusing the Scene one: Effect cells live in the Effect region with a
 * uint16_t index space (419 cells) and use the Effect marker. Affiliates:
 * Effect setters below, autosave_getEffectParameter().
 */
static void scene_storeEffectByte(uint8_t scene_index,
                                  uint8_t *storage,
                                  uint16_t parameter_index,
                                  uint8_t value)
{
    if (!scene_get(scene_index) || !storage || *storage == value)
        return;
    *storage = value;
    autosave_markEffectParameterDirty(scene_index, parameter_index);
    bank_invalidateSdCleanScene(scene_index);
}
```

### 5.3 After line 561 (end of `scene_getSlot6Track7MorphAmpEnvelopeDecay()`) — add

```c
const effect_record_t *scene_effectConst(uint8_t scene_index)
{
    const scene_t *scene = scene_getConst(scene_index);

    /* Read-only Effect view; contract in SceneData.h. */
    return scene ? &scene->effect : 0;
}

void scene_effectRecordDefaults(effect_record_t *record)
{
    /*
     * Build the `off` Effect defaults (contract in SceneData.h).
     *
     * The common parameters get the same value in both endpoint images
     * because there is a single default per parameter (user rule F3).
     */
    if (!record)
        return;
    memset(record, 0, sizeof(*record));
    record->type = EFFECT_TYPE_OFF;
    record->seq_run_mode = EFFECT_SEQ_RUN_FWD;
    record->seq_length = EFFECT_SEQ_LENGTH_DEFAULT;
    record->seq_step_scale = EFFECT_SEQ_SCALE_DEFAULT;
    record->normal[EFFECT_COMMON_PARAM_AUDIO_OUT] = EFFECT_COMMON_DEFAULT_AUDIO_OUT;
    record->normal[EFFECT_COMMON_PARAM_LEVEL] = EFFECT_COMMON_DEFAULT_LEVEL;
    record->normal[EFFECT_COMMON_PARAM_PAN] = EFFECT_COMMON_DEFAULT_PAN;
    record->morph[EFFECT_COMMON_PARAM_AUDIO_OUT] = EFFECT_COMMON_DEFAULT_AUDIO_OUT;
    record->morph[EFFECT_COMMON_PARAM_LEVEL] = EFFECT_COMMON_DEFAULT_LEVEL;
    record->morph[EFFECT_COMMON_PARAM_PAN] = EFFECT_COMMON_DEFAULT_PAN;
}

uint8_t scene_commitEffectRecord(uint8_t scene_index,
                                 const effect_record_t *record)
{
    scene_t *scene = scene_get(scene_index);

    /*
     * Whole-record replacement (contract in SceneData.h).
     *
     * The three sequence settings are normalized so an invalid source can
     * never leave an out-of-range byte for the sequencer or AutoSave; the
     * type id is validated by the caller (registry owner). Marking is
     * unconditional: a whole commit is an explicit action (type change, load,
     * copy) and must be persisted even when bytes happen to match.
     */
    if (!scene || !record)
        return 0u;
    scene->effect = *record;
    if (scene->effect.seq_run_mode >= EFFECT_SEQ_RUN_MODE_COUNT)
        scene->effect.seq_run_mode = EFFECT_SEQ_RUN_FWD;
    if (scene->effect.seq_length < EFFECT_SEQ_LENGTH_MIN)
        scene->effect.seq_length = EFFECT_SEQ_LENGTH_MIN;
    if (scene->effect.seq_length > EFFECT_SEQ_LENGTH_MAX)
        scene->effect.seq_length = EFFECT_SEQ_LENGTH_MAX;
    if (scene->effect.seq_step_scale >= EFFECT_SEQ_SCALE_COUNT)
        scene->effect.seq_step_scale = EFFECT_SEQ_SCALE_DEFAULT;
    autosave_markEffectDirty(scene_index);
    bank_invalidateSdCleanScene(scene_index);
    return 1u;
}

void scene_setEffectNormalParameter(uint8_t scene_index, uint8_t index,
                                    uint8_t value)
{
    scene_t *scene = scene_get(scene_index);

    /* One normal-image cell; AutoSave param 3 + index (SceneData.h). */
    if (!scene || index >= EFFECT_PARAM_COUNT)
        return;
    scene_storeEffectByte(scene_index, &scene->effect.normal[index],
                          (uint16_t)(AUTOSAVE_EFFECT_PARAM_NORMAL_BASE + index),
                          value);
}

void scene_setEffectMorphParameter(uint8_t scene_index, uint8_t index,
                                   uint8_t value)
{
    scene_t *scene = scene_get(scene_index);

    /* One Morph-image cell; AutoSave param 67 + index (SceneData.h). */
    if (!scene || index >= EFFECT_PARAM_COUNT)
        return;
    scene_storeEffectByte(scene_index, &scene->effect.morph[index],
                          (uint16_t)(AUTOSAVE_EFFECT_PARAM_MORPH_BASE + index),
                          value);
}

void scene_setEffectSeqRunMode(uint8_t scene_index, uint8_t mode)
{
    scene_t *scene = scene_get(scene_index);

    /* Run mode; invalid values are ignored rather than clamped so a stray
     * value can never silently select a different mode. */
    if (!scene || mode >= EFFECT_SEQ_RUN_MODE_COUNT)
        return;
    scene_storeEffectByte(scene_index, &scene->effect.seq_run_mode,
                          AUTOSAVE_EFFECT_PARAM_SEQ_RUN_MODE, mode);
}

void scene_setEffectSeqLength(uint8_t scene_index, uint8_t length)
{
    scene_t *scene = scene_get(scene_index);

    /* Sequence length, clamped to 1..16 (encoder edits saturate). */
    if (!scene)
        return;
    if (length < EFFECT_SEQ_LENGTH_MIN)
        length = EFFECT_SEQ_LENGTH_MIN;
    if (length > EFFECT_SEQ_LENGTH_MAX)
        length = EFFECT_SEQ_LENGTH_MAX;
    scene_storeEffectByte(scene_index, &scene->effect.seq_length,
                          AUTOSAVE_EFFECT_PARAM_SEQ_LENGTH, length);
}

void scene_setEffectSeqStepScale(uint8_t scene_index, uint8_t scale)
{
    scene_t *scene = scene_get(scene_index);

    /* Shared step-scale table index (Step 8); invalid indices ignored. */
    if (!scene || scale >= EFFECT_SEQ_SCALE_COUNT)
        return;
    scene_storeEffectByte(scene_index, &scene->effect.seq_step_scale,
                          AUTOSAVE_EFFECT_PARAM_SEQ_STEP_SCALE, scale);
}

void scene_setEffectSeqLaneValue(uint8_t scene_index, uint8_t step,
                                 uint8_t lane, uint8_t value)
{
    scene_t *scene = scene_get(scene_index);

    /* One lane value; its AutoSave cell is step base + 2 + lane. */
    if (!scene || step >= EFFECT_SEQ_STEP_COUNT ||
        lane >= EFFECT_SEQ_LANE_COUNT)
        return;
    scene_storeEffectByte(
        scene_index, &scene->effect.steps[step].value[lane],
        (uint16_t)(AUTOSAVE_EFFECT_PARAM_STEPS_BASE +
                   step * AUTOSAVE_EFFECT_STEP_BYTES +
                   AUTOSAVE_EFFECT_STEP_VALUES_OFFSET + lane),
        value);
}

void scene_setEffectSeqLaneLocked(uint8_t scene_index, uint8_t step,
                                  uint8_t lane, uint8_t locked)
{
    scene_t *scene = scene_get(scene_index);
    effect_seq_step_t *entry;
    uint16_t bit;
    uint16_t updated;

    /*
     * Set or clear one lane's lock bit.
     *
     * The 16-bit mask is serialized as two little-endian bytes; only the byte
     * holding this lane's bit changes, so only that AutoSave cell is marked.
     */
    if (!scene || step >= EFFECT_SEQ_STEP_COUNT ||
        lane >= EFFECT_SEQ_LANE_COUNT)
        return;
    entry = &scene->effect.steps[step];
    bit = (uint16_t)(1u << lane);
    updated = locked ? (uint16_t)(entry->lock_mask | bit)
                     : (uint16_t)(entry->lock_mask & (uint16_t)~bit);
    if (updated == entry->lock_mask)
        return;
    entry->lock_mask = updated;
    autosave_markEffectParameterDirty(
        scene_index,
        (uint16_t)(AUTOSAVE_EFFECT_PARAM_STEPS_BASE +
                   step * AUTOSAVE_EFFECT_STEP_BYTES +
                   ((lane < 8u) ? AUTOSAVE_EFFECT_STEP_MASK_LO_OFFSET
                                : AUTOSAVE_EFFECT_STEP_MASK_HI_OFFSET)));
    bank_invalidateSdCleanScene(scene_index);
}

void scene_setEffectMorphAmount(uint8_t scene_index, uint8_t amount)
{
    scene_t *scene = scene_get(scene_index);

    /* Scene Effect Morph amount through the Scene-parameter helper. */
    if (!scene)
        return;
    scene_storeParameterByte(scene_index,
                             &scene->settings.effect_morph_amount,
                             AUTOSAVE_SCENE_PARAM_EFFECT_MORPH, amount);
}

uint8_t scene_getEffectMorphAmount(uint8_t scene_index)
{
    const scene_t *scene = scene_getConst(scene_index);

    /* Retained Effect Morph amount, 0 for invalid scenes. */
    return scene ? scene->settings.effect_morph_amount : 0u;
}
```

### 5.4 `scene_initAll()` — insert after line 596 (end of the per-slot mix loop)

After the per-slot mix-settings loop (line 596) and before the instrument
reset loop, insert:

```c
        /*
         * Seed the Scene's Effect with the `off` defaults (Session 072 step
         * 3). The preceding memset already set effect_morph_amount to 0.
         * Direct assignment is allowed here by the SceneData rule for boot
         * initialization; no AutoSave marking happens before tracking is on.
         */
        scene_effectRecordDefaults(&scenes[scene_index].effect);
```

---

## 6. `Autosave.h`

### 6.1 Lines 173–179 — region comment (modify)

After:

```c
/*
 * One Scene's relative regions and explicit parameter allocation.
 *
 * Scene source occupies bytes 8..9; parameters occupy bytes 10..127, of
 * which indices 0..40 exist (40 = Effect Morph amount, Session 072). The
 * Effect region (128..639) holds a 3-byte type token, an 8-byte name, and
 * 419 live parameter bytes from Effect-relative offset 11 (layout:
 * autosave_effect_parameter_t below; S072_ST3_IMPLEMENTATION.md §4). Kit
 * begins at 640: source at 8..9, parameters at 10..127, then six 192-byte
 * Instruments, ending at 1,920.
 */
```

### 6.2 Line 184 (modify)

`#define AUTOSAVE_SCENE_PARAMETER_LIVE_BYTES    40u` → `41u`

### 6.3 Lines 186–190 (modify)

Before:

```c
#define AUTOSAVE_EFFECT_TYPE_OFFSET             0u
#define AUTOSAVE_EFFECT_NAME_OFFSET             1u
#define AUTOSAVE_EFFECT_PARAMETERS_OFFSET        9u
#define AUTOSAVE_EFFECT_PARAMETER_ALLOC_BYTES  503u
#define AUTOSAVE_EFFECT_PARAM_COUNT              0u
```

After:

```c
/* Effect region (Session 072 step 3). The type token mirrors Instruments'
 * 3-byte text field; the name mirrors the HCNAMES Effect row (Step 6). The
 * live parameter cells follow; see autosave_effect_parameter_t. The
 * type/name bytes have no live getter until Steps 4/6 and remain zero. */
#define AUTOSAVE_EFFECT_TYPE_OFFSET             0u
#define AUTOSAVE_EFFECT_TYPE_BYTES              3u
#define AUTOSAVE_EFFECT_NAME_OFFSET             3u
#define AUTOSAVE_EFFECT_PARAMETERS_OFFSET      11u
#define AUTOSAVE_EFFECT_PARAMETER_ALLOC_BYTES  501u
#define AUTOSAVE_EFFECT_PARAM_COUNT            419u
```

The existing static assert (offset + alloc == 512) still holds (11 + 501).

### 6.4 Lines 199–212 — domain comment (modify)

Change "Effect deliberately has a zero live count" to "Effect has its own
uint16_t parameter space (autosave_effect_parameter_t)". Change the last
sentence "a future Effect parameter raises the zero live count…" to "a new
Effect cell must extend autosave_effect_parameter_t, its getter branch, and
a SceneData setter using scene_storeEffectByte()".

### 6.5 Line 232 — Scene enum (modify)

Before: `    AUTOSAVE_SCENE_PARAM_COUNT = 40`

After:

```c
    /* Scene Effect Morph amount (Session 072 step 3). Last, so every
     * earlier index keeps its wire position. */
    AUTOSAVE_SCENE_PARAM_EFFECT_MORPH = 40,
    AUTOSAVE_SCENE_PARAM_COUNT = 41
```

### 6.6 After line 239 (`} autosave_kit_parameter_t;`) — add

```c
/*
 * Effect-region live parameter indices (Session 072 step 3).
 *
 * What: a uint16_t index space for the 419 live Effect cells, starting at
 * Effect-relative offset AUTOSAVE_EFFECT_PARAMETERS_OFFSET. Order: three
 * sequence settings, 64 normal cells, 64 Morph cells, then 16 steps of
 * AUTOSAVE_EFFECT_STEP_BYTES (lock-mask low byte, lock-mask high byte, 16
 * lane values). Why a fixed order and not a struct copy: no C layout is
 * serialized (same rule as Scene/Kit), so padding or field reordering in
 * effect_record_t can never change the wire format.
 * Writers: SceneData Effect setters via autosave_markEffectParameterDirty().
 * Reader: autosave_getEffectParameter(); boot apply arrives in Step 6.
 */
typedef enum {
    AUTOSAVE_EFFECT_PARAM_SEQ_RUN_MODE = 0,
    AUTOSAVE_EFFECT_PARAM_SEQ_LENGTH = 1,
    AUTOSAVE_EFFECT_PARAM_SEQ_STEP_SCALE = 2,
    AUTOSAVE_EFFECT_PARAM_NORMAL_BASE = 3,
    AUTOSAVE_EFFECT_PARAM_MORPH_BASE = 67,
    AUTOSAVE_EFFECT_PARAM_STEPS_BASE = 131
} autosave_effect_parameter_t;

#define AUTOSAVE_EFFECT_STEP_BYTES              18u
#define AUTOSAVE_EFFECT_STEP_MASK_LO_OFFSET      0u
#define AUTOSAVE_EFFECT_STEP_MASK_HI_OFFSET      1u
#define AUTOSAVE_EFFECT_STEP_VALUES_OFFSET       2u

_Static_assert(AUTOSAVE_EFFECT_PARAM_STEPS_BASE +
                   16u * AUTOSAVE_EFFECT_STEP_BYTES ==
                   AUTOSAVE_EFFECT_PARAM_COUNT,
               "Effect live cells end after the 16th sequence step");
```

The existing `_Static_assert(AUTOSAVE_EFFECT_PARAM_COUNT <= ALLOC)` now checks
419 ≤ 501.

---

## 7. `Autosave.c`

### 7.1 After the includes (line 22, `#include "timebase.h"`) — add

```c
/*
 * Effect wire geometry must match the EffectTypes.h record contract
 * (Session 072 step 3). Autosave.h cannot include EffectTypes.h without
 * widening its dependency surface, so the cross-check lives here.
 */
_Static_assert(AUTOSAVE_EFFECT_PARAM_MORPH_BASE ==
                   AUTOSAVE_EFFECT_PARAM_NORMAL_BASE + EFFECT_PARAM_COUNT,
               "Effect normal image is 64 cells");
_Static_assert(AUTOSAVE_EFFECT_PARAM_STEPS_BASE ==
                   AUTOSAVE_EFFECT_PARAM_MORPH_BASE + EFFECT_PARAM_COUNT,
               "Effect Morph image is 64 cells");
_Static_assert(AUTOSAVE_EFFECT_STEP_BYTES ==
                   AUTOSAVE_EFFECT_STEP_VALUES_OFFSET + EFFECT_SEQ_LANE_COUNT,
               "Effect step = 2 mask bytes + 16 lane values");
_Static_assert(EFFECT_SEQ_STEP_COUNT == 16u,
               "Effect wire layout assumes 16 steps");
```

`EffectTypes.h` reaches `Autosave.c` through its existing
`#include "SceneData.h"` (line 17).

### 7.2 Lines 873–879 — `autosave_getSceneParameter()` (modify)

Before:

```c
    } else if (parameter_index < AUTOSAVE_SCENE_PARAM_MIDI_NOTE_BASE) {
        *value = scene->settings.midi_channel[
            parameter_index - AUTOSAVE_SCENE_PARAM_MIDI_CHANNEL_BASE];
    } else {
        *value = scene->settings.midi_note[
            parameter_index - AUTOSAVE_SCENE_PARAM_MIDI_NOTE_BASE];
    }
```

After:

```c
    } else if (parameter_index < AUTOSAVE_SCENE_PARAM_MIDI_NOTE_BASE) {
        *value = scene->settings.midi_channel[
            parameter_index - AUTOSAVE_SCENE_PARAM_MIDI_CHANNEL_BASE];
    } else if (parameter_index < AUTOSAVE_SCENE_PARAM_EFFECT_MORPH) {
        *value = scene->settings.midi_note[
            parameter_index - AUTOSAVE_SCENE_PARAM_MIDI_NOTE_BASE];
    } else {
        /*
         * Index 40: Scene Effect Morph amount (Session 072 step 3).
         * This branch MUST precede any open-ended else: the previous final
         * else mapped every index >= 33 to midi_note[], so index 40 would
         * have read midi_note[7], one past the seven-track array.
         */
        *value = scene->settings.effect_morph_amount;
    }
```

### 7.3 Lines 884–903 — `autosave_getEffectParameter()` (modify: stub → implementation)

Replace the stub comment and body with:

```c
/*
 * Project one live Effect cell into its ordered wire index.
 *
 * Inputs: resident Scene, Effect parameter index (autosave_effect_parameter_t
 * layout), and result cell. Output: the byte and success, or 0 for indices at
 * or beyond AUTOSAVE_EFFECT_PARAM_COUNT (the reserved tail stays closed).
 * The 16-bit lock mask is emitted little-endian. Why explicit projection: no
 * C struct layout is serialized. Affiliates: SceneData Effect setters (the
 * only markers of these cells), EffectTypes.h effect_record_t, and the Step 6
 * boot reader, which must decode this exact order.
 */
static uint8_t autosave_getEffectParameter(const scene_t *scene,
                                           uint16_t parameter_index,
                                           uint8_t *value)
{
    const effect_record_t *effect;

    if (!scene || !value ||
        parameter_index >= AUTOSAVE_EFFECT_PARAM_COUNT) {
        return 0u;
    }
    effect = &scene->effect;
    if (parameter_index == AUTOSAVE_EFFECT_PARAM_SEQ_RUN_MODE) {
        *value = effect->seq_run_mode;
    } else if (parameter_index == AUTOSAVE_EFFECT_PARAM_SEQ_LENGTH) {
        *value = effect->seq_length;
    } else if (parameter_index == AUTOSAVE_EFFECT_PARAM_SEQ_STEP_SCALE) {
        *value = effect->seq_step_scale;
    } else if (parameter_index < AUTOSAVE_EFFECT_PARAM_MORPH_BASE) {
        *value = effect->normal[
            parameter_index - AUTOSAVE_EFFECT_PARAM_NORMAL_BASE];
    } else if (parameter_index < AUTOSAVE_EFFECT_PARAM_STEPS_BASE) {
        *value = effect->morph[
            parameter_index - AUTOSAVE_EFFECT_PARAM_MORPH_BASE];
    } else {
        uint16_t step_relative = (uint16_t)(
            parameter_index - AUTOSAVE_EFFECT_PARAM_STEPS_BASE);
        const effect_seq_step_t *step = &effect->steps[
            step_relative / AUTOSAVE_EFFECT_STEP_BYTES];
        uint8_t field = (uint8_t)(step_relative % AUTOSAVE_EFFECT_STEP_BYTES);

        if (field == AUTOSAVE_EFFECT_STEP_MASK_LO_OFFSET)
            *value = (uint8_t)(step->lock_mask & 0xffu);
        else if (field == AUTOSAVE_EFFECT_STEP_MASK_HI_OFFSET)
            *value = (uint8_t)(step->lock_mask >> 8);
        else
            *value = step->value[field - AUTOSAVE_EFFECT_STEP_VALUES_OFFSET];
    }
    return 1u;
}
```

### 7.4 Lines 1029–1036 — router comment (modify)

After:

```c
    /*
     * Route the Effect parameter interval to its live getter.
     *
     * Inputs: Scene-relative bytes 139..639 (Effect offset 11..511). Output:
     * the 419 live Effect cells (Session 072 step 3); the reserved tail
     * reports absent. The Effect type token (128..130) and name (131..138)
     * are not yet live and fall through as absent (Steps 4 and 6). Pattern
     * remains outside this wire layout entirely.
     */
```

The condition and call below it are unchanged; they already use the
geometry macros.

### 7.5 Lines 1300–1306 — `autosave_applyScenePayload()` (modify)

Before:

```c
        } else {
            scene_setTrackMidiNote(
                scene_index,
                (uint8_t)(parameter_index -
                          AUTOSAVE_SCENE_PARAM_MIDI_NOTE_BASE),
                value);
        }
```

After:

```c
        } else if (parameter_index < AUTOSAVE_SCENE_PARAM_EFFECT_MORPH) {
            scene_setTrackMidiNote(
                scene_index,
                (uint8_t)(parameter_index -
                          AUTOSAVE_SCENE_PARAM_MIDI_NOTE_BASE),
                value);
        } else {
            /*
             * Index 40: Scene Effect Morph amount (Session 072 step 3).
             * Records written before this step hold 0 here (zero-filled
             * reserve), which is the correct default. Without this branch the
             * open else would call scene_setTrackMidiNote(track 7), which that
             * setter rejects silently, dropping the value.
             */
            scene_setEffectMorphAmount(scene_index, value);
        }
```

### 7.6 Lines 1661–1667 — `autosave_markEffectParameterDirty()` comment (modify)

After:

```c
    /*
     * Mark one live Effect cell dirty (Session 072 step 3).
     *
     * Inputs: Scene and Effect parameter index (autosave_effect_parameter_t).
     * Output: bit SceneBase + 128 + 11 + index when tracking is enabled and
     * the Scene exists in the payload; out-of-range indices are ignored.
     * Callers: SceneData Effect setters only (scene_storeEffectByte,
     * scene_setEffectSeqLaneLocked) and autosave_markEffectDirty().
     */
```

The body is unchanged; `live_count` now evaluates to 419.

### 7.7 Lines 1936–1941 — `autosave_markEffectDirty()` comment (modify)

After:

```c
    /*
     * Mark every live Effect cell of one Scene (Session 072 step 3).
     *
     * Input: destination Scene. Output: all 419 Effect parameter bits. The
     * type token and name join this marker in Steps 4 and 6. Callers:
     * scene_commitEffectRecord() and autosave_markSceneWithoutPatternDirty()
     * (Scene/Bank load completion), so Scene scope never omits the Effect.
     */
```

The body is unchanged.

---

## 8. `SceneModTargets.h` — line 39 (modify)

Before: `    SCENE_MOD_TARGET_KIND_EFFECT_PARAMETER`

After:

```c
    /*
     * Scene Effect Morph amount, `fxm` (Session 072 step 3).
     *
     * Inputs: stored value 0..255 (Pattern values use the Morph 7-to-8-bit
     * rule). Output from Step 9: runtime overlay/LFO on the Scene's Effect
     * Morph. Effect PARAMETERS are not Scene targets; they own block 7
     * (IDs 448..511, EffectTypes.h). This enumerator replaces the unused
     * placeholder KIND_EFFECT_PARAMETER, which had no references.
     */
    SCENE_MOD_TARGET_KIND_EFFECT_MORPH
```

## 9. `SceneModTargets.c`

### 9.1 After line 105 (the `"6fx"` row) — add

```c
    /*
     * Scene Effect Morph amount, ID 404 (Session 072 step 3, reserved).
     *
     * Inputs/outputs: none yet. use_flags is deliberately 0, so every picker
     * (scn automation, LFO scn, velocity) and every validity check skips it
     * until Step 9 installs its apply paths and assigns its use flags (LFO
     * and automation; velocity eligibility is decided there). Reserving the
     * row now fixes its ID, so stored LFO tokens and Pattern targets never
     * move.
     */
    { SCENE_MOD_TARGET_ID(20u), SCENE_MOD_TARGET_KIND_EFFECT_MORPH, 0xffu,
      0u, 255u, 0u,
      "Effect", "FX Morph", "fxm" },
```

### 9.2 After line 106 (`};` closing the table) — add

```c
/*
 * Block-6 capacity guard (user rule A23): Scene targets own exactly the 64
 * IDs 384..447. Past that, a new row would collide with Effect block 7.
 */
_Static_assert(SCENE_MOD_TARGET_COUNT <= 64u,
               "Scene target table exceeds its 64-ID block");
```

---

## 10. Documentation checklist (completed)

- **`AUTOSAVE.md`:**
  - Line 92: remove "live Effect persistence (`AUTOSAVE_EFFECT_PARAM_COUNT` is
    zero)" from the unimplemented list. Replace it with "Effect type
    token/name bytes (Steps 4/6) and the Effect boot reader (Step 6)".
  - Lines 153–154: "118 Scene-parameter bytes, currently 41 live (index 40 =
    Effect Morph amount)" and "512 Effect bytes: 3 type + 8 name + 419 live
    parameter cells + 82 reserved".
  - Add the §4 layout table.
  - Record deviation 3 (no header bump; the Step 6 reader requires a valid
    token).
- **`SRAM_MANIFEST.md` line 60:** `scenes` 19,200 → **25,952**: "Sixteen
  resident Scene records (1,622 B each: 41 B settings, 1 B pad, 420 B Effect,
  1,160 B Kit)". Regenerate the SRAM1 ledger from the new link.
- **`BANK_PRESET_ARCHITECTURE.md`:**
  - Line 41: "Effects (not yet implemented)" → "Effect (Scene-owned record;
    runtime from Step 4)".
  - Target table (near line 279): add row `| 404 | fxm | No (Scene) | 255 |
    Reserved, no apply path | Reserved (Session 072) |`.
- **`MODULE_INTERCHANGE_SPEC.md`:**
  - SceneData section: add the Effect accessor/setter family, and the rule
    that only these write `scene_t.effect`.
  - SceneModTargets section: add `fxm` (404) reserved with flags 0, and the
    block-6 64-ID limit.
  - Add a new short `Core/DSP/Effects/EffectTypes` entry: data-only contract
    and block-7 ID helpers.
- **`EFFECTS_BUS_FEATURE_PLAN.md` §15:** replace the table with the §4 layout,
  and record deviations 1–3.
- **`MEMORY.md` Volatile Notes:** "S072 Step 3: scene_t carries
  effect_record_t (Scene-owned, SceneData setters only) and
  effect_morph_amount (AutoSave Scene param 40); AutoSave Effect region
  live (419 cells, type/name pending Steps 4/6); fxm ID 404 reserved (flags
  0)."

---

## 11. Build and verification gates

**Build:**

1. `make clean && make all` succeeds with no new warnings. Every new
   `_Static_assert` passes: record 420/18, the ID layout, the Effect wire
   geometry, and the Scene-target capacity.
2. `arm-none-eabi-nm -S build/lxr02.elf | grep " scenes$"` reports `0x6560`
   (25,952).
3. `link_budget.py`: flash grows by roughly 1–2 KiB (setters, getter, init);
   record it. DTCM statics and the arena are unchanged.
4. SRAM1 `.bss` grows by 6,752 B ± alignment; record the exact figure for the
   RAM ledger.

**Hardware / card:**

5. **Boot on the existing card** (AutoSave records written before this step).
   Boot restore completes, and Scene parameters, Kit, and Pattern are
   restored as before. `effect_morph_amount` reads 0; it is not visible yet.
6. **Regression:** edit a voice Morph, a MIDI channel, and a MIDI note in a
   Scene, and confirm AutoSave persists them across reboot. This exercises the
   Scene-parameter getter and reader around the new index 40, and specifically
   that MIDI note track 7 still round-trips.
7. **Effect region capture:** perform a root Scene Load into Scene *s*, wait
   for AutoSave to complete, copy the card, and read the newer `.hcprms`
   record.
   - The Effect parameters start at byte offset
     `64 + 3856 + 128 + s*1920 + 128 + 11`.
   - Expect: `00 10 04` (fwd, 16, 1/16), then normal
     `00 7F 40 00 …`, Morph `00 7F 40 00 …`, and zero step bytes.
   - The type bytes (region offset 0–2) stay `00 00 00`.
   - A throwaway local script, like `verify_bank_autosave.py`, is fine; do not
     commit it unless wanted.
8. **`fxm` invisibility:**
   - The STEP automation `scn` category list, the LFO `scn` target list, and
     the velocity target list are unchanged: no `fxm` entry.
   - Existing `1vm..6vm`, `srt`, `7dc`, `1ou..6ou`, and `1fx..6fx` targets
     behave as before.
9. **Smoke:** Scene switch, Bank Load, and Kit Load while playing. Behaviour
   is unchanged.

**Rollback:** revert the commit. AutoSave records written by this step carry
Effect bytes that older firmware ignores (its getter reported them absent),
and Scene parameter 40 sits in a reserve cell that older firmware never
reads. No card-format hazard.

---

## 12. Working implementation notes

### 12.1 Source completed

- Added `Core/DSP/Effects/EffectTypes.h` with the 420-byte retained
  `effect_record_t`, 18-byte sequencer step, common defaults, sequence
  constants, and block-7 target helpers. The record and every public helper
  carry adjacent contract comments.
- Added the Scene-owned Effect record and `effect_morph_amount` to
  `SceneData.h`, with SceneData-only accessors, defaults, scalar setters,
  whole-record commit, sequence/lane setters, AutoSave marking, and card-clean
  invalidation in `SceneData.c`.
- Expanded AutoSave Scene geometry to 41 live Scene parameters and projected
  the 419 live Effect cells at relative Effect offset 11. Type/name bytes and
  the boot reader remain intentionally deferred to Steps 4/6.
- Added the block-7 Effect target constants and reserved `fxm` Scene target
  ID 404 with zero use flags. The Scene target table has a compile-time
  64-ID capacity guard.
- Updated the authoritative AutoSave, SRAM, Bank/Preset, module-interchange,
  feature-plan, and memory notes to match the implementation and its deferred
  hardware/card gates.

### 12.2 Allocation acknowledgement and link evidence

The acknowledged allocation is +6,752 B SRAM1 `.bss`: `scene_t` is 1,622 B,
and `scenes[16]` is 25,952 B (`0x6560`). Owner is SceneData; lifetime is the
firmware lifetime. The production clean link reports:

| Measurement | Result |
|---|---:|
| `arm-none-eabi-size` text | 458,664 B |
| `arm-none-eabi-size` data | 416 B |
| `arm-none-eabi-size` bss | 425,852 B |
| Flash image allocation | 459,080 / 491,520 B; 32,440 B headroom |
| SRAM1 `.bss` | 292,204 B |
| SRAM1 normal (`.data` + `.bss`) | 292,620 B |
| SRAM1 total | 295,720 / 376,832 B; 81,112 B free |
| DTCM statics | 4,084 B |
| `.dtcm_fxbuf` arena | 126,976 B; unchanged |
| `scenes` symbol | `0x6560` / 25,952 B |

`make clean && make all` passed. The first clean compile caught and corrected
one pre-existing Scene MIDI-note group assertion that still used the old
40-parameter terminal count; the corrected assertion now ends that group at
the explicit Effect Morph index. No new warnings remain in the changed ST3
modules.

### 12.3 Remaining gates

`make img` completed after the clean link and wrote
`build/LXRV2_lxr02.img` with a 459,080-byte firmware payload (459,096-byte
image including its 16-byte wrapper). SHA-256 is
`ed114c0169697f1ef0f944958e7efaa94305e12d2ccbc6dfcc6fc22f2efd7be6`.
Final whitespace/status checks also pass. Hardware/card validation is pending:
boot restore on an existing card, Scene MIDI-note regression, Effect-region
capture, `fxm` picker invisibility, and Scene/Bank/Kit playback smoke testing
must be run on the target.

---

## 13. Review assessment (2026-09-27, post-implementation)

**Verdict:** source and build are **accepted**; the design is wired correctly.
The card evidence is **partial**. The captured sessions never exercised a
whole-Scene marker, so gate 7 (Effect-region capture) is still open. §13.4
gives the one-minute test that closes it. Step 4's dev hook (ST4 §15) also
closes it, because a type commit marks the whole region.

### 13.1 Source review (diff against the Step 2 working tree)

- **`EffectTypes.h`** matches §2: the 420-byte record, 18-byte step, common
  defaults, run modes, scale constants, and block-7 helpers. The static asserts
  are present.
- **`SceneData.h/.c`:**
  - The include, `effect_morph_amount` (last in settings), and
    `effect_record_t effect` between settings and Kit are present.
  - All eleven accessors/setters match §5.3; `scene_storeEffectByte()` stores,
    marks, and invalidates in that order.
  - The `scene_initAll()` seed and the ID-layout static asserts are present.
- **`Autosave.h/.c`:**
  - Geometry matches §6 (11/501/419; Scene live bytes 41; `EFFECT_MORPH = 40`).
  - The Effect enum and step constants are present, as are the §7.1 static
    asserts.
  - **Both open-`else` hazards are closed:** the getter's index-40 branch and
    the boot reader's `scene_setEffectMorphAmount()` branch.
  - `autosave_getEffectParameter()` matches §7.3, with little-endian mask bytes
    and the reserved tail closed.
  - The implementer also corrected a pre-existing static assert that bounded
    the MIDI-note group by `AUTOSAVE_SCENE_PARAM_COUNT`. It now uses
    `AUTOSAVE_SCENE_PARAM_EFFECT_MORPH` (`Autosave.c:83`). Correct.
- **`SceneModTargets.h/.c`:** the kind rename, the `fxm` row (ID 404, flags 0,
  `"Effect"/"FX Morph"/"fxm"`), and the 64-ID block-6 assert match §8–§9.

### 13.2 Build (independent clean rebuild)

| Measurement | Result |
|---|---|
| `text` / `data` / `bss` | 458,664 / 416 / 425,852 (matches §12.2) |
| Flash | 459,080 B; **32,440 B headroom** (+456 B over Step 2) |
| `scenes` | `0x6560` = 25,952 B at `0x200519F8` |
| DTCM statics / arena | 4,084 B / 126,976 B (unchanged) |

No new warnings in the Step 3 modules.

### 13.3 Card evidence (`SD_CARD_ST3_OUTPUT/`)

- **The Step 3 firmware ran.** `LXRV2_lxr02.img` on the card is 459,096 B,
  identical to the Step 3 image, so the most recent boots in `asavetrc.bin`
  are Step 3 boots.
- **No boot failure:** `bootlog.bin` is absent.
- **Records healthy:** `.hcprms1` (generation 521, active Scene 13) and
  `.hcprms2` (generation 520, active Scene 12) both have magic `HCPR`, format 2,
  commit `0xA5`, and are exactly 34,768 B. The trace shows the ordinary
  lifecycle on every recent boot:
  - `V` (winner validated, generations 519–521),
  - `Q` (reader summary; case-2/3 masks 0, i.e. matching-winner restore),
  - `S` → `A` → `M` → `C` → `T` (`DONE`).

  The trace contains no `E`/`X` records. So the Step 3 reader loop over Scene
  parameters 0..40 and the writer both ran cleanly.
- **Scene parameter 40:** byte 50 of every Scene section in both records is 0,
  the correct default. No `D` record ever marked it, which is expected:
  `effect_morph_amount` has no editor until later steps, and the boot reader's
  `scene_setEffectMorphAmount(0)` is an equal-value no-op.
- **Effect region: not yet exercised.** Every Scene's 512-byte Effect region is
  all zero in both records, and no `D` record in the entire 211,106-record
  trace falls in an Effect region. The captured sessions contain only Scene
  switches (Bank `active_scene`/edit-mask marks), Instrument parameter edits,
  and Kit Save lifecycles. There is no root Scene or Bank Load, so
  `autosave_markSceneWithoutPatternDirty()` — the only Step 3 path that marks
  the Effect region — never ran.

### 13.4 Gate status and closing test

| Gate (§11) | Status |
|---|---|
| 1–4 build | **PASS** |
| 5 boot on existing card | **PASS** (V/Q/T clean, no bootlog) |
| 6 Scene-parameter regression | **Partial.** The reader/writer ran cleanly around index 40; no MIDI-note edit was captured to prove the track-7 round trip explicitly |
| 7 Effect-region capture | **OPEN.** No whole-Scene mark in the captured sessions |
| 8 `fxm` invisibility | Not evidenced by the card; user UI check |
| 9 smoke | Scene switching and Kit Save seen in trace; no errors |

**Closing test for gate 7:**

1. Load any root Scene into Scene *s* (Load:[Scene], OK).
2. Wait about 6 seconds for AutoSave (a `T DONE` record).
3. Copy `.hcprms1` and `.hcprms2`.
4. In the higher generation, bytes
   `64 + 3856 + 128 + s*1920 + 128 + 11` onward must read
   `00 10 04 00 7F 40 00 …` (fwd, 16, 1/16, then normal
   `out`/`vol`/`pan` = 0/127/64). The same pattern repeats 64 bytes later for
   the Morph image. The type bytes stay `00 00 00` until Step 4.

### 13.5 Findings and follow-ups

1. **Tooling:** `tools/decode_devlogs.py` still describes the Step 2
   geometry. It bounds Scene parameters at 40 (index 40 prints as
   `scene reserved byte50`) and labels the Effect region only as
   `effect byteN`. It is updated in Step 4 together with the type token (ST4
   §14) so a single decoder change covers both.
2. **Design note for Step 6 (no defect):** the AutoSave Effect region is
   filled only when bytes are marked. A Scene that never had a whole-Scene
   mark keeps an all-zero region. Its type token is zero, which the Step 6
   reader must treat as "absent" (deviation 3). Any non-`off` Effect reaches
   its region through `scene_commitEffectRecord()`, which marks every cell
   and, from Step 4, the token. A region with a valid token is therefore always
   fully captured.
3. **Unrelated observation (not changed):** `SD_CARD_ST3_OUTPUT/.pat02b` is
   5,000 B, not 10,656 B. It begins with a valid `PAT4` header, so it is a
   truncated Pattern AutoSave B record for Scene 2; `.pat02a` is complete. The
   A/B design tolerates this (A wins), and the next Scene-2 Pattern AutoSave
   rewrites B. Nothing in Steps 1–3 touches Pattern AutoSave, and FAT
   timestamps are not evidence of when it happened. It is recorded here only as
   an observation, in case it recurs.
4. **Still uncommitted:** Steps 1–3 remain uncommitted on `dev-ph5-effects`.
   Commit them, as three commits if the working tree can be split, before
   Step 4 changes the render path and the AutoSave token.
5. **Carry-over:** the ST1 §26.3 `fxbuf_init()` ordering fix and Makefile `@#`
   are still open.
