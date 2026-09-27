# S072 Step 4 — Implementation Schedule: EffectsManager, Registry, StereoFilter

**Scope:** `EFFECTS_BUS_FEATURE_PLAN.md` §5, §6, §9, §12.6, and §17.1 Step 4.

**This step adds:**

- the Effect type registry: `off` (built in) and `flt` (StereoFilter);
- `EffectsManager`: registry lookups, per-block parameter resolution,
  the type-change transaction, Scene activation with the FxBuffer handoff, and
  the common-parameter runtime that the mixer consumes in Step 5;
- the StereoFilter type (descriptors, lanes, runtime, DSP), including a
  float-I/O variant of the resonant filter;
- Scene-activation hooks in Preset, and the Effect Morph bulk-set from the
  PERF global Morph (plan A3);
- the live AutoSave Effect type token, which completes Step 3 deviation 2;
- a `DEV_MODE_DIAGNOSTIC` dev hook that forces an Effect type at boot;
- the trace decoder update for the Step 3/4 AutoSave geometry.

**Not in this step:**

- the FX bus, sends, and return. `effects_process()` exists but nothing calls
  it until Step 5, so **nothing is audible from Step 4**;
- files and HCNAMES (Step 6);
- UI (Step 7);
- sequencer overlays (Step 8);
- automation and LFO overlays (Step 9);
- edit-mask fan-out (Step 10).

**Status:** implementation complete in the working tree; clean-build and
documentation verification are recorded in §18 below. Source changes were
made with their adjacent descriptive comment blocks, and this document is the
working implementation log.

**Line numbers** refer to the working tree after Step 3. Steps 1–3 remain in
the same working tree and are intentionally preserved as the prerequisites
for this implementation log; no commit boundary is required to verify ST4.

---

## 0. Decisions and notes

### D1 — StereoFilter defaults (confirmed by the user, 2026-09-27)

A type change resets type-specific parameters to their single registry
default (user rule F3). Proposed `flt` defaults:

| Parameter | Default | Why |
|---|---|---|
| `filter_freq` | **64** | Mid cutoff, audibly filtered, so the Step 5 dev-hook listening test hears the filter immediately |
| `filter_reso` | 0 | Neutral |
| `filter_drive` | 0 | Neutral (drive gain 0.4 from `SVF_setDrive`) |
| `filter_type` | 0 (LP) | Menu index 0 = `LP`; the runtime SVF type is index + 1 |

The alternative is `filter_freq` 127, a near-transparent LP. That is safer
musically, but the Step 5 test would then need an edit path that does not
exist until Step 7.

### Notes (no decision needed)

1. **Resolution rescans every block instead of tracking dirty cells**
   (supersedes the plan's §9 dirty-mask mechanism).
   - `effects_service()` re-resolves each descriptor of the active type
     (at most 64) every 32-frame block and calls `write_param` only when the
     value changed.
   - Cost is about 10–15 cycles per descriptor: about 100 cycles for `flt`
     (7 rows), and at most about 1,000 cycles for a 64-row type. That is under
     0.6 % CPU in the worst case and negligible for `flt`.
   - What it buys: no writer (SceneData setters, AutoSave reader, loaders,
     fan-out, later overlays) has to remember to notify the manager, so a
     missed update is impossible by construction.
   - If a future heavy type needs it, dirty tracking can be added behind the
     same API.
2. **The Effect type switches immediately on Scene activation.** Unlike voice
   slots, it does not wait for a quiet envelope. User rule F6: same type
   continues (tails ring); a different type runs the incoming `init`. Tails of
   a different outgoing type therefore stop at the switch.
3. **The type change is committed in place.** Step 3's
   `scene_commitEffectRecord()` needs a complete record, which would put a
   420-byte copy on the foreground stack. Step 4 instead adds a two-call
   in-place whole commit (`scene_effectRecordForWholeCommit()` +
   `scene_finishEffectWholeCommit()`). This follows SceneData's existing rule
   that direct assignment is allowed "for a validated whole-object commit
   followed by the named region marker". There is no stack growth and no new
   static scratch.
4. **Interpolation mirrors the voices exactly.** `effects_interpolate()`
   duplicates the arithmetic of `presetMorph_interpolate()`
   (`presetMorphEngine.c:82`, `static`) instead of exporting it from the voice
   Morph engine, so voice code is untouched. Both carry a keep-in-sync note.
5. **The `off` type has no folder.** It is a built-in registry row in
   `EffectsManager.c`: three common descriptors, the Morph lane, and all-NULL
   operations. Real types follow the `<Name>/<Name>Parameters.c/.h` +
   `<Name>Effect.c/.h` layout.

### RAM (within the A46 approval; exact figures recorded at link)

| Object | Region | Bytes | Approval |
|---|---|---|---|
| `effects_state` (runtime type, active Scene, force flag, `last_applied[64]`, common runtime) | SRAM1 `.bss` | 76 (static-asserted) | plan §16 item 4 (≤ ~282) |
| `effects_runtime` union (`flt`: two `ResonantFilter` + type byte) | DTCM `.dtcmz` | 76 | plan §16 item 9 (~96) |
| `effects_registryCheckResult` | SRAM1 | 1, `DEV_MODE_DIAGNOSTIC` only | dev |

The DTCM statics grow 4,084 → 4,160 B. The arena start moves from
`0x20001000` to `0x20001040`, so the arena shrinks by 64 B to **126,912 B**,
still 4,032 B above the 120 KiB minimum. `link_budget.py` reports it.

---

## 1. Change index

| # | File | Line(s) | Op | Summary |
|---|---|---|---|---|
| 1 | `Core/DSP/Effects/EffectsManager.h` | new | add | Registry/descriptor/ops types, type ids, manager API |
| 2 | `Core/DSP/Effects/EffectsManager.c` | new | add | Registry table, lookups, self-check, resolution, activation, type change |
| 3 | `Core/DSP/Effects/EffectParamRows.h` | new | add | Row-building macros + shared common rows for type descriptor tables |
| 4 | `Core/DSP/Effects/StereoFilter/StereoFilterParameters.h` | new | add | Registry exports |
| 5 | `Core/DSP/Effects/StereoFilter/StereoFilterParameters.c` | new | add | 7 descriptors, lanes, names |
| 6 | `Core/DSP/Effects/StereoFilter/StereoFilterEffect.h` | new | add | Runtime struct + ops export |
| 7 | `Core/DSP/Effects/StereoFilter/StereoFilterEffect.c` | new | add | init/write_param/process |
| 8 | `Core/DSPAudio/ResonantFilter.h` | after 101 | add | `SVF_calcBlockZDFFloat()` declaration |
| 9 | `Core/DSPAudio/ResonantFilter.c` | after 320 | add | Float-I/O ZDF block (Effect use only) |
| 10 | `Core/Bank/Scene/SceneData.h` | after 414 | add | In-place whole-commit pair |
| 11 | `Core/Bank/Scene/SceneData.c` | after 665 | add | In-place whole-commit pair |
| 12 | `Core/Bank/Scene/Autosave.c` | after 18 | add | `#include "EffectsManager.h"` |
| 13 | `Core/Bank/Scene/Autosave.c` | before 1088 | add | Router: live type-token bytes |
| 14 | `Core/Bank/Scene/Autosave.c` | 1983–1999 | modify | `autosave_markEffectDirty()` marks the token too |
| 15 | `Core/Bank/Scene/Preset/presetManager.c` | after 50 | add | `#include "EffectsManager.h"` |
| 16 | `Core/Bank/Scene/Preset/presetManager.c` | after 1516 | add | `effects_activateScene()` in the sync apply |
| 17 | `Core/Bank/Scene/Preset/presetManager.c` | after 1549 | add | `effects_activateScene()` in the Scene worker start |
| 18 | `Core/Bank/Scene/Preset/presetManager.c` | after 3000 | add | Global Morph bulk-sets Effect Morph (A3) |
| 19 | `Core/DSPAudio/mixer.c` | after 56, after 526 | add | Include + `effects_service()` per block |
| 20 | `main.c` | after 80, after 126 | add | Include + `effects_init()` in `dsp_init()` |
| 21 | `main.c` | 302, after 308 | modify | Diagnostic screen shows the registry self-check code |
| 22 | `main.c` | after 1286 | add | Dev hook: force the Effect type after boot activation |
| 23 | `config.h` | after 245 | add | `DEV_EFFECT_FORCE_TYPE` knob |
| 24 | `Makefile` | includes, `SRCS`, `DSP_SRCS`, rules | add | New sources, `-ICore/DSP/Effects/StereoFilter`, -Ofast rule |
| 25 | `tools/decode_devlogs.py` | 350, 371–382, 443–446 | modify | Scene param 40 + Effect region labels |
| 26 | Docs | — | modify | `MODULE_INTERCHANGE_SPEC`, `AUTOSAVE`, `SRAM_MANIFEST`, `DEV_MODES`, `BANK_PRESET_ARCHITECTURE`, plan, `MEMORY` |

---

## 2. New file `Core/DSP/Effects/EffectsManager.h`

```c
/*
 * Core/DSP/Effects/EffectsManager.h
 *
 *  Created on: 27.09.2026
 *  (standard LXR02 Open-Source licence header, as in FxBuffer.h)
 */
#ifndef EFFECTS_MANAGER_H_
#define EFFECTS_MANAGER_H_

#include <stdint.h>
#include "EffectTypes.h"
#include "InstrumentManager.h"   /* ParamDescriptor, mod-domain and flag constants */
#include "FxBuffer.h"

/*
 * Effect registry and runtime manager (Session 072, Effects Phase 5 step 4).
 *
 * What: (1) the immutable registry of Effect types, with their parameter
 * descriptors, sequencer lanes, names, and DSP operations; (2) the single
 * runtime owner of the active Scene's Effect: which type's DSP state is live,
 * resolution of retained values into DSP writes, Scene activation and the
 * FxBuffer handoff, and the type-change transaction.
 *
 * Why one module: plan §4 mirrors InstrumentManager: only the registry knows a
 * type's layout, and only the manager writes Effect DSP state. SceneData
 * (retained bytes) and FxBuffer (arena) stay independent of type layouts.
 *
 * Context: foreground only (main loop, render, Menu, Preset). Never call from
 * an ISR. Audio render also runs in the foreground, so activation, type
 * change, and resolution never race the DSP.
 *
 * Affiliates: SceneData (record/setters), FxBuffer (handoff, share
 * callback), Preset (activation hooks, Morph bulk-set), mixer (service now;
 * process and common runtime in Step 5), Autosave (type token), Menu (Step 7),
 * sequencer (Steps 8–9), storage (Step 6).
 */

/* Registry ids (append-only; persisted only as the 3-character token) ---- */

#define EFFECT_TYPE_STEREO_FILTER        1u
#define EFFECT_TYPE_COUNT                2u   /* off + flt */

/* Descriptor flags beyond ParamDescriptor.flags ------------------------- */

/* Parameter domain is 0..255. Pattern automation (7-bit) and MIDI need an
 * expand7 converter or the registry refuses AUTOMATABLE (plan §5.3). */
#define EFFECT_PARAM_FLAG_WIDE8              0x01u
/* Legal maximum depends on the current FxBuffer share (plan §12.5). */
#define EFFECT_PARAM_FLAG_BUFFER_DEPENDENT   0x02u

/* Sequencer lane table values ------------------------------------------- */

/* lanes[0] must be EFFECT_LANE_MORPH_SOURCE (plan A2); unused lanes NONE. */
#define EFFECT_LANE_MORPH_SOURCE         0xFEu
#define EFFECT_LANE_NONE                 0xFFu

/* I/O shape flags -------------------------------------------------------- */

#define EFFECT_IO_MONO_IN                0x01u
#define EFFECT_IO_STEREO_IN              0x02u
#define EFFECT_IO_MONO_OUT               0x04u
#define EFFECT_IO_STEREO_OUT             0x08u

/*
 * 7-bit to parameter-domain converter for WIDE8 rows (Pattern automation and,
 * later, MIDI). effect_expand7Linear() implements the Morph rule.
 */
typedef uint8_t (*effect_expand7_fn)(uint8_t value7);
uint8_t effect_expand7Linear(uint8_t value7);

/*
 * One Effect parameter descriptor.
 *
 * base: the shared ParamDescriptor (key, labels, dtype, MORPH/MOD/AUTO flags,
 * modulation domain; runtime binding unused, INSTRUMENT_BIND_NONE), so Menu's
 * dtype rendering and target browsers work unchanged. default_value: the
 * single default for both endpoints (user rule F3; unset rows default 0).
 * max_value: inclusive stored maximum, used for clamping by the manager,
 * the Step 6 file parser, and FX-sequencer lanes. effect_flags: WIDE8 /
 * BUFFER_DEPENDENT. expand7: converter for WIDE8 automatable rows, else NULL.
 */
typedef struct {
    ParamDescriptor base;
    uint8_t default_value;
    uint8_t max_value;
    uint8_t effect_flags;
    effect_expand7_fn expand7;
} effect_param_descriptor_t;

/*
 * One processing block handed to a type's process().
 *
 * l/r: 32-bit float samples, in place, where 1.0 equals int16 full scale
 * (the mixer's int16<<8 bus divided by 32767*256 in Step 5). r is NULL when
 * channels == 1 (mono-input type). frames: OUTPUT_DMA_SIZE. share: current
 * FxBuffer share for buffer-using types.
 */
typedef struct {
    float *l;
    float *r;
    uint8_t frames;
    uint8_t channels;
    const fx_share_t *share;
} effect_io_t;

/* Type UI hooks: layout is defined in Step 7; registry rows carry NULL until
 * then. Forward declaration keeps the registry entry shape frozen now. */
struct effect_ui_hooks;
typedef struct effect_ui_hooks effect_ui_hooks_t;

/* Custom SELECT layout: up to 4 screens of 4 descriptor indices per SELECT
 * button (plan §13.2, A39). NULL selects the Step 7 default layout. */
typedef struct {
    uint8_t screen_count[8];
    uint8_t cells[8][4][4];
} effect_select_layout_t;

/*
 * DSP operations of one type (plan §5.5). Every pointer may be NULL; the
 * manager treats NULL as "nothing to do". rt points at the type's member of
 * the manager-owned runtime union.
 *
 * init: type becomes live (boot, Scene activation with a different type,
 *   type change). Receives the FxBuffer handoff; adopts or disposes of buffer
 *   contents by its own rules (no system clear, F6).
 * export_handoff: type is exited; describes its buffer use (FxBuffer.h).
 * write_param: one resolved parameter value (index 3..63; the manager owns
 *   the common indices 0..2) in the descriptor's stored domain.
 * process: one block in place (effect_io_t).
 * buffer_changed: the FxBuffer share moved or resized.
 * effective_max: optional runtime clamp for BUFFER_DEPENDENT rows.
 */
typedef struct {
    void (*init)(void *rt, const fxbuf_handoff_t *handoff);
    void (*export_handoff)(const void *rt, fxbuf_handoff_t *out);
    void (*write_param)(void *rt, uint8_t index, uint8_t value);
    void (*process)(void *rt, effect_io_t *io);
    void (*buffer_changed)(void *rt, const fx_share_t *share);
    uint8_t (*effective_max)(uint8_t index, const fx_share_t *share);
} effect_type_ops_t;

/*
 * One registry row (plan §5.4). token3 is the persisted identity (files,
 * AutoSave); abbrev5 is for the Load/Save screen (A4); full8 is for the
 * single-parameter view of `typ`. descriptors[0..2] must be the shared
 * common rows (EffectParamRows.h). runtime_bytes is sizeof the type's runtime
 * struct (checked against the manager's union).
 */
typedef struct {
    const char *token3;
    const char *abbrev5;
    const char *full8;
    uint8_t io_flags;
    const effect_param_descriptor_t *descriptors;
    uint8_t descriptor_count;
    uint8_t lanes[EFFECT_SEQ_LANE_COUNT];
    const effect_select_layout_t *select_layout;
    const effect_type_ops_t *ops;
    const effect_ui_hooks_t *ui;
    uint16_t runtime_bytes;
    uint32_t buffer_min_bytes;
    uint32_t buffer_pref_bytes;
} effect_registry_entry_t;

/*
 * Common-parameter runtime for the mixer return (Step 5).
 * route: mixer route 0..5; level: 0..1 (effect_level / 127); pan: 0..127.
 * Updated only by effects_service().
 */
typedef struct {
    uint8_t route;
    uint8_t pan;
    float level;
} effects_common_runtime_t;

/* Registry lookups ------------------------------------------------------ */

uint8_t effects_registryCount(void);
/* Row for a type id, or NULL for an unknown id. */
const effect_registry_entry_t *effects_registryEntry(effect_type_id_t type);
/* 3-character token (not NUL-guaranteed beyond 3) or NULL for unknown ids. */
const char *effects_typeToken(effect_type_id_t type);
/* Map a 3-character token to its id; returns 1 on success. */
uint8_t effects_typeFromToken(const char token[3], effect_type_id_t *type_out);
/* Descriptor by index, or NULL beyond the type's descriptor_count. */
const effect_param_descriptor_t *effects_descriptor(effect_type_id_t type,
                                                    uint8_t index);
/* Descriptor by file key; index_out written on success. */
const effect_param_descriptor_t *effects_descriptorByKey(
    effect_type_id_t type, const char *file_key, uint8_t *index_out);
/* Capability queries (flags plus rules: index 63 never Pattern-automatable;
 * WIDE8 needs expand7). */
uint8_t effects_paramAutomatable(effect_type_id_t type, uint8_t index);
uint8_t effects_paramModulatable(effect_type_id_t type, uint8_t index);

#if DEV_MODE_DIAGNOSTIC
/* Boot registry self-check code (0 = pass); see EffectsManager.c table. */
uint8_t effects_registryCheckResult(void);
#endif

/* Runtime --------------------------------------------------------------- */

/* Boot: after fxbuf_init() and instrumentManager_runtimeInit(). */
void effects_init(void);
/* Make scene_index the Effect runtime's source; switch type if it differs. */
void effects_activateScene(uint8_t scene_index);
/* Type-change transaction (plan §7.2, F3); returns 1 when committed. */
uint8_t effects_changeType(uint8_t scene_index, effect_type_id_t type);
/* Once per render block, before processing: resolve and write changes. */
void effects_service(void);
/* Process one block with the live type (Step 5 caller); no-op for `off`. */
void effects_process(effect_io_t *io);
/* Live type id and its I/O flags (Step 5 bus sizing; `off` returns 0). */
effect_type_id_t effects_activeType(void);
uint8_t effects_activeIoFlags(void);
/* Common-parameter runtime for the Step 5 return path. */
const effects_common_runtime_t *effects_commonRuntime(void);

#endif /* EFFECTS_MANAGER_H_ */
```

---

## 3. New file `Core/DSP/Effects/EffectParamRows.h`

```c
/*
 * Core/DSP/Effects/EffectParamRows.h
 *
 * Row-building macros for Effect descriptor tables (Session 072 step 4).
 *
 * What: compact initializers for effect_param_descriptor_t rows, and the
 * three shared common rows every type places at indices 0..2. Why a
 * separate header: descriptor tables need Menu dtype constants (DTYPE_*,
 * MENU_*) exactly like Instrument *Parameters.c files, while
 * EffectsManager.h must stay free of Menu headers. Include only from
 * *Parameters.c files and EffectsManager.c, after "menu.h" and "MenuText.h".
 * Affiliates: EffectTypes.h common indices/defaults, InstrumentManager.h
 * ParamDescriptor layout (field order: file_key, short, long, category,
 * dtype, flags, mod_domain, runtime).
 */
#ifndef EFFECT_PARAM_ROWS_H_
#define EFFECT_PARAM_ROWS_H_

#include "EffectsManager.h"

#define EFFECT_BIND_NONE  { INSTRUMENT_BIND_NONE, 0u, 0u }
#define EFFECT_MOD_NONE   { 0u, 0u, INSTRUMENT_MOD_DOMAIN_NONE }
#define EFFECT_MOD_0_127  { 0u, 127u, INSTRUMENT_MOD_DOMAIN_CONTINUOUS }

#define EFFECT_FLAGS_IMAGE \
    (INSTRUMENT_PARAM_FLAG_MORPHABLE | INSTRUMENT_PARAM_FLAG_MODULATABLE | \
     INSTRUMENT_PARAM_FLAG_AUTOMATABLE)

/* Generic 7-bit row: key, category, long (<=8), short (3), dtype, flags,
 * mod domain, default, max. */
#define EFFECT_ROW(key_, cat_, long_, short_, dtype_, flags_, mod_, def_, max_) \
    { { key_, short_, long_, cat_, (uint8_t)(dtype_), (uint8_t)(flags_), mod_, \
        EFFECT_BIND_NONE }, (uint8_t)(def_), (uint8_t)(max_), 0u, 0 }

/* Menu-table row (dtype encodes the table, as ROW_MENU in DrumParameters.c). */
#define EFFECT_ROW_MENU(key_, cat_, long_, short_, menu_, flags_, mod_, def_, max_) \
    EFFECT_ROW(key_, cat_, long_, short_, (DTYPE_MENU | ((menu_) << 4)), \
               flags_, mod_, def_, max_)

/*
 * The three common rows (indices 0..2; plan A1). Every type uses this macro
 * so keys, labels, dtypes, defaults, and maxima are identical; each type
 * chooses only the capability flags. out uses the voice route table
 * (MENU_AUDIO_OUT, 0..5); vol is 0..127 (127 = unity); pan is PM63 (64 =
 * centre). Defaults come from EffectTypes.h and match
 * scene_effectRecordDefaults().
 */
#define EFFECT_COMMON_ROWS(out_flags_, vol_flags_, pan_flags_) \
    EFFECT_ROW_MENU("effect_audio_out", "Effect", "AudioOut", "out", \
                    MENU_AUDIO_OUT, out_flags_, EFFECT_MOD_NONE, \
                    EFFECT_COMMON_DEFAULT_AUDIO_OUT, 5u), \
    EFFECT_ROW("effect_level", "Effect", "Level", "vol", DTYPE_0B127, \
               vol_flags_, EFFECT_MOD_0_127, EFFECT_COMMON_DEFAULT_LEVEL, 127u), \
    EFFECT_ROW("effect_pan", "Effect", "Panning", "pan", DTYPE_PM63, \
               pan_flags_, EFFECT_MOD_0_127, EFFECT_COMMON_DEFAULT_PAN, 127u)

#endif /* EFFECT_PARAM_ROWS_H_ */
```

---

## 4. New files `Core/DSP/Effects/StereoFilter/StereoFilterParameters.h/.c`

### 4.1 `StereoFilterParameters.h`

```c
#ifndef STEREO_FILTER_PARAMETERS_H_
#define STEREO_FILTER_PARAMETERS_H_

#include "EffectsManager.h"

/*
 * StereoFilter registry exports (Session 072 step 4; plan §6, A7).
 * Imported by EffectsManager.c's registry table. Counts are macros so the
 * registry initializer stays a constant expression (same pattern as
 * DRUM_PARAM_DESCRIPTOR_COUNT).
 */
#define STEREO_FILTER_PARAM_COUNT 7u

extern const effect_param_descriptor_t stereoFilter_descriptors[];
extern const char stereoFilter_token3[];   /* "flt"      */
extern const char stereoFilter_abbrev5[];  /* "StFlt"    */
extern const char stereoFilter_full8[];    /* "StFilter" */

/* Local descriptor indices (common rows 0..2 come first). */
typedef enum {
    STEREO_FILTER_PARAM_AUDIO_OUT = EFFECT_COMMON_PARAM_AUDIO_OUT,
    STEREO_FILTER_PARAM_LEVEL     = EFFECT_COMMON_PARAM_LEVEL,
    STEREO_FILTER_PARAM_PAN       = EFFECT_COMMON_PARAM_PAN,
    STEREO_FILTER_PARAM_FREQ      = 3,
    STEREO_FILTER_PARAM_RESO,
    STEREO_FILTER_PARAM_DRIVE,
    STEREO_FILTER_PARAM_TYPE,
    STEREO_FILTER_PARAM_ENUM_COUNT
} stereo_filter_param_t;

#endif
```

### 4.2 `StereoFilterParameters.c`

```c
#include "StereoFilterParameters.h"
#include "menu.h"
#include "MenuText.h"
#include "EffectParamRows.h"

/*
 * StereoFilter parameter source of truth (plan §6).
 *
 * The four filter rows use the same keys, dtypes, and 0..127 domains as the
 * voice filter rows (DrumParameters.c), so users see identical controls and
 * the same conversions apply in StereoFilterEffect.c. Flags: freq/reso/drive
 * are morphable, LFO-modulatable, and automatable; type is morphable
 * (stepwise) and automatable but not an LFO target (voice rule MOD_NONE);
 * out is automatable only. Defaults: S072_ST4 §0 D1.
 */
const char stereoFilter_token3[]  = "flt";
const char stereoFilter_abbrev5[] = "StFlt";
const char stereoFilter_full8[]   = "StFilter";

const effect_param_descriptor_t stereoFilter_descriptors[] = {
    EFFECT_COMMON_ROWS(INSTRUMENT_PARAM_FLAG_AUTOMATABLE,
                       EFFECT_FLAGS_IMAGE,
                       EFFECT_FLAGS_IMAGE),
    EFFECT_ROW("filter_freq", "Filter", "Frequncy", "frq", DTYPE_0B127,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 64u, 127u),
    EFFECT_ROW("filter_reso", "Filter", "Resnance", "res", DTYPE_0B127,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 0u, 127u),
    EFFECT_ROW("filter_drive", "Filter", "Overdriv", "drv", DTYPE_0B127,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 0u, 127u),
    EFFECT_ROW_MENU("filter_type", "Filter", "Type", "typ", MENU_FILTER,
                    INSTRUMENT_PARAM_FLAG_MORPHABLE |
                    INSTRUMENT_PARAM_FLAG_AUTOMATABLE,
                    EFFECT_MOD_NONE, 0u, 7u),
};

_Static_assert(sizeof(stereoFilter_descriptors) /
                   sizeof(stereoFilter_descriptors[0]) ==
                   STEREO_FILTER_PARAM_COUNT,
               "STEREO_FILTER_PARAM_COUNT must match the descriptor table");
_Static_assert(STEREO_FILTER_PARAM_ENUM_COUNT == STEREO_FILTER_PARAM_COUNT,
               "stereo_filter_param_t must match the descriptor table");
```

`filter_type` max 7 covers the eight menu entries `LP HP BP UBP Nch Pek LP2
off`. Runtime SVF type = value + 1, where 8 is the SVF pass-through default.

The lane table lives in the registry row (§6.2).

---

## 5. New files `Core/DSP/Effects/StereoFilter/StereoFilterEffect.h/.c`

### 5.1 `StereoFilterEffect.h`

```c
#ifndef STEREO_FILTER_EFFECT_H_
#define STEREO_FILTER_EFFECT_H_

#include "EffectsManager.h"
#include "ResonantFilter.h"

/*
 * StereoFilter runtime (Session 072 step 4; plan §6).
 *
 * What: two resonant-filter states (left, right) sharing one coefficient set
 * (linked stereo, A7), and the SVF type (1..8). Lives in the manager's DTCM
 * runtime union; never touched outside StereoFilterEffect.c and the manager.
 * No FxBuffer use (plan A8): init ignores the handoff and export is NULL.
 */
typedef struct {
    ResonantFilter left;
    ResonantFilter right;
    uint8_t svf_type;
} StereoFilterRuntime;

extern const effect_type_ops_t stereoFilter_ops;

#endif
```

### 5.2 `StereoFilterEffect.c` (in `DSP_SRCS`, -Ofast)

```c
#include "StereoFilterEffect.h"
#include "StereoFilterParameters.h"
#include "globals.h"        /* FILTER_SHAPER */
#include "valueShaper.h"    /* valueShaperF2F() */
#include <stddef.h>

/*
 * Copy the left filter's coefficients to the right channel.
 *
 * Why: linked stereo (A7). The coefficient setters (SVF_directSetFilterValue,
 * SVF_setReso, SVF_setDrive) write one ResonantFilter; the state variables
 * (s1, s2, a, b, zi) must stay per channel, so only f, g, q, drive are copied.
 */
static void stereoFilter_linkCoefficients(StereoFilterRuntime *rt)
{
    rt->right.f = rt->left.f;
    rt->right.g = rt->left.g;
    rt->right.q = rt->left.q;
    rt->right.drive = rt->left.drive;
}

/*
 * init: type becomes live. Resets both channel states and coefficients to the
 * SVF defaults; the manager then forces a full parameter write on the next
 * service pass. The handoff is ignored: this type never uses the arena.
 */
static void stereoFilter_init(void *rt_void, const fxbuf_handoff_t *handoff)
{
    StereoFilterRuntime *rt = (StereoFilterRuntime *)rt_void;

    (void)handoff;
    SVF_init(&rt->left);
    SVF_init(&rt->right);
    SVF_reset(&rt->left);
    SVF_reset(&rt->right);
    rt->svf_type = 1u;   /* FILTER_LP until the forced write arrives */
}

/*
 * write_param: apply one resolved type-specific value (indices 3..6).
 *
 * Conversions are identical to InstrumentManager's voice filter writer
 * (InstrumentManager.c filter_freq/reso/drive/type branches) so the Effect
 * filter responds exactly like a voice filter at the same setting.
 */
static void stereoFilter_writeParam(void *rt_void, uint8_t index,
                                    uint8_t value)
{
    StereoFilterRuntime *rt = (StereoFilterRuntime *)rt_void;

    switch (index) {
    case STEREO_FILTER_PARAM_FREQ:
        SVF_directSetFilterValue(&rt->left,
            valueShaperF2F(value / 127.0f, FILTER_SHAPER));
        break;
    case STEREO_FILTER_PARAM_RESO:
        SVF_setReso(&rt->left, value / 127.0f);
        break;
    case STEREO_FILTER_PARAM_DRIVE:
        SVF_setDrive(&rt->left, value);
        break;
    case STEREO_FILTER_PARAM_TYPE:
        rt->svf_type = (uint8_t)(value + 1u);
        return;
    default:
        return;
    }
    stereoFilter_linkCoefficients(rt);
}

/*
 * process: filter the block in place, left and (if present) right, with the
 * float-I/O ZDF variant so bus headroom above int16 full scale is shaped by
 * the filter's own soft clipper instead of being hard-clipped at its input.
 */
static void stereoFilter_process(void *rt_void, effect_io_t *io)
{
    StereoFilterRuntime *rt = (StereoFilterRuntime *)rt_void;

    if (!io || !io->l)
        return;
    SVF_calcBlockZDFFloat(&rt->left, rt->svf_type, io->l, io->frames);
    if (io->channels > 1u && io->r)
        SVF_calcBlockZDFFloat(&rt->right, rt->svf_type, io->r, io->frames);
}

/*
 * StereoFilter operations. No handoff export and no buffer callbacks: the
 * type never claims arena memory.
 */
const effect_type_ops_t stereoFilter_ops = {
    stereoFilter_init,
    NULL,
    stereoFilter_writeParam,
    stereoFilter_process,
    NULL,
    NULL,
};
```

Verify when implementing: `SVF_init()` is declared `void SVF_init();` in
`ResonantFilter.h` but defined with a `ResonantFilter *` parameter. The call
compiles today, as it does for the voices. Do not change that header line in
this step.

---

## 6. New file `Core/DSP/Effects/EffectsManager.c`

### 6.1 Includes, the `off` rows, and the runtime union

```c
#include "EffectsManager.h"
#include "menu.h"
#include "MenuText.h"
#include "EffectParamRows.h"
#include "SceneData.h"
#include "StereoFilterParameters.h"
#include "StereoFilterEffect.h"
#include <string.h>

_Static_assert(EFFECT_TYPE_OFF == FXBUF_EFFECT_TYPE_NONE,
               "registry id 0 (off) is the FxBuffer handoff 'no effect' id");
_Static_assert(EFFECT_TYPE_COUNT <= 255u, "type ids are bytes");

/*
 * Built-in `off` type (plan A5, A37): the three common rows, the Morph lane,
 * no DSP. The mixer skips send accumulation and return while it is active
 * (Step 5). It has no folder because it has no DSP or type-specific
 * parameters.
 */
static const effect_param_descriptor_t effects_off_descriptors[] = {
    EFFECT_COMMON_ROWS(INSTRUMENT_PARAM_FLAG_AUTOMATABLE,
                       EFFECT_FLAGS_IMAGE,
                       EFFECT_FLAGS_IMAGE),
};

/*
 * Runtime union: one member per type with DSP state, sized automatically
 * (plan §16 item 9). DTCM because process() touches it every sample.
 * INDTCMZ zero-inits at boot; each type's init() establishes its own state.
 * Only the member of effects_state.runtime_type is meaningful.
 */
typedef union {
    StereoFilterRuntime stereo_filter;
} effects_runtime_t;

static INDTCMZ effects_runtime_t effects_runtime;
```

### 6.2 Registry table

```c
/*
 * Registry, indexed by effect_type_id_t (append-only; ids are not persisted,
 * tokens are). Lane tables name descriptor indices; lane 0 is always the
 * Effect Morph source (A2). StereoFilter lanes: Morph, freq, reso, drive,
 * type, vol, pan (plan §6).
 */
static const effect_registry_entry_t effects_registry[EFFECT_TYPE_COUNT] = {
    {   /* EFFECT_TYPE_OFF */
        "off", "Off  ", "Off",
        0u,
        effects_off_descriptors,
        (uint8_t)(sizeof(effects_off_descriptors) /
                  sizeof(effects_off_descriptors[0])),
        { EFFECT_LANE_MORPH_SOURCE,
          EFFECT_LANE_NONE, EFFECT_LANE_NONE, EFFECT_LANE_NONE,
          EFFECT_LANE_NONE, EFFECT_LANE_NONE, EFFECT_LANE_NONE,
          EFFECT_LANE_NONE, EFFECT_LANE_NONE, EFFECT_LANE_NONE,
          EFFECT_LANE_NONE, EFFECT_LANE_NONE, EFFECT_LANE_NONE,
          EFFECT_LANE_NONE, EFFECT_LANE_NONE, EFFECT_LANE_NONE },
        NULL, NULL, NULL,
        0u, 0u, 0u
    },
    {   /* EFFECT_TYPE_STEREO_FILTER */
        stereoFilter_token3, stereoFilter_abbrev5, stereoFilter_full8,
        EFFECT_IO_STEREO_IN | EFFECT_IO_STEREO_OUT,
        stereoFilter_descriptors, STEREO_FILTER_PARAM_COUNT,
        { EFFECT_LANE_MORPH_SOURCE,
          STEREO_FILTER_PARAM_FREQ, STEREO_FILTER_PARAM_RESO,
          STEREO_FILTER_PARAM_DRIVE, STEREO_FILTER_PARAM_TYPE,
          STEREO_FILTER_PARAM_LEVEL, STEREO_FILTER_PARAM_PAN,
          EFFECT_LANE_NONE, EFFECT_LANE_NONE, EFFECT_LANE_NONE,
          EFFECT_LANE_NONE, EFFECT_LANE_NONE, EFFECT_LANE_NONE,
          EFFECT_LANE_NONE, EFFECT_LANE_NONE, EFFECT_LANE_NONE },
        NULL, &stereoFilter_ops, NULL,
        (uint16_t)sizeof(StereoFilterRuntime), 0u, 0u
    },
};

_Static_assert(sizeof(StereoFilterRuntime) <= sizeof(effects_runtime_t),
               "runtime union must hold every registered type");
```

The `off` names are space-padded to the display widths only where a fixed
width is rendered. Keep `"Off  "` (5 characters) for the abbreviation; the
full name is `"Off"`. `effects_typeToken()` returns exactly 3 characters.

### 6.3 Manager state

```c
/*
 * Manager-owned runtime state (SRAM1, 76 B; plan §16 item 4).
 *
 * runtime_type: the type whose effects_runtime member is live.
 * scene_index: the Scene whose record drives resolution (the active Scene).
 * force_all: next service pass writes every row even if unchanged (after
 *   init or activation, because DSP state no longer matches last_applied).
 * last_applied[]: last value written per descriptor (change detection).
 * common: resolved out/vol/pan for the mixer return (Step 5).
 */
typedef struct {
    effect_type_id_t runtime_type;
    uint8_t scene_index;
    uint8_t force_all;
    uint8_t reserved;
    uint8_t last_applied[EFFECT_PARAM_COUNT];
    effects_common_runtime_t common;
} effects_state_t;

_Static_assert(sizeof(effects_state_t) == 76u,
               "effects_state_t size is recorded in SRAM_MANIFEST.md");

static effects_state_t effects_state;

#if DEV_MODE_DIAGNOSTIC
static uint8_t effects_registryCheckCode;
#endif
```

### 6.4 Helpers: interpolation, expand7, registry lookups

```c
/*
 * Endpoint interpolation, identical to presetMorph_interpolate()
 * (presetMorphEngine.c:82). KEEP IN SYNC: user rule A16/G4 requires Effect
 * Morph to behave exactly like instrument Morph. Exact endpoints at 0/255;
 * rounded linear interpolation otherwise.
 */
static uint8_t effects_interpolate(uint8_t a, uint8_t b, uint8_t amount)
{
    int32_t numerator;

    if (amount == 0u)
        return a;
    if (amount == 255u)
        return b;
    numerator = (int32_t)a * 255 + ((int32_t)b - (int32_t)a) * amount + 127;
    if (numerator < 0)
        return 0u;
    return (uint8_t)(numerator / 255);
}

uint8_t effect_expand7Linear(uint8_t value7)
{
    /* Morph 7-to-8-bit rule: 0..126 -> x2, 127 -> 255 (endpoint reachable). */
    return (value7 < 127u) ? (uint8_t)(value7 * 2u) : 255u;
}

uint8_t effects_registryCount(void)
{
    return EFFECT_TYPE_COUNT;
}

const effect_registry_entry_t *effects_registryEntry(effect_type_id_t type)
{
    return (type < EFFECT_TYPE_COUNT) ? &effects_registry[type] : NULL;
}

const char *effects_typeToken(effect_type_id_t type)
{
    const effect_registry_entry_t *entry = effects_registryEntry(type);

    return entry ? entry->token3 : NULL;
}

uint8_t effects_typeFromToken(const char token[3], effect_type_id_t *type_out)
{
    effect_type_id_t type;

    /* Linear scan; the registry is tiny. Exact 3-character match. */
    if (!token)
        return 0u;
    for (type = 0u; type < EFFECT_TYPE_COUNT; type++) {
        if (memcmp(effects_registry[type].token3, token, 3u) == 0) {
            if (type_out)
                *type_out = type;
            return 1u;
        }
    }
    return 0u;
}

const effect_param_descriptor_t *effects_descriptor(effect_type_id_t type,
                                                    uint8_t index)
{
    const effect_registry_entry_t *entry = effects_registryEntry(type);

    if (!entry || index >= entry->descriptor_count)
        return NULL;
    return &entry->descriptors[index];
}

const effect_param_descriptor_t *effects_descriptorByKey(
    effect_type_id_t type, const char *file_key, uint8_t *index_out)
{
    const effect_registry_entry_t *entry = effects_registryEntry(type);
    uint8_t i;

    if (!entry || !file_key)
        return NULL;
    for (i = 0u; i < entry->descriptor_count; i++) {
        if (strcmp(entry->descriptors[i].base.file_key, file_key) == 0) {
            if (index_out)
                *index_out = i;
            return &entry->descriptors[i];
        }
    }
    return NULL;
}

uint8_t effects_paramAutomatable(effect_type_id_t type, uint8_t index)
{
    const effect_param_descriptor_t *d = effects_descriptor(type, index);

    /* Rules: AUTOMATABLE flag; local 63 aliases the off sentinel (F2/G2);
     * WIDE8 rows need a 7-to-8-bit converter (plan §5.3). */
    if (!d || index >= EFFECT_TARGET_PATTERN_LOCAL_LIMIT)
        return 0u;
    if ((d->base.flags & INSTRUMENT_PARAM_FLAG_AUTOMATABLE) == 0u)
        return 0u;
    if ((d->effect_flags & EFFECT_PARAM_FLAG_WIDE8) && !d->expand7)
        return 0u;
    return 1u;
}

uint8_t effects_paramModulatable(effect_type_id_t type, uint8_t index)
{
    const effect_param_descriptor_t *d = effects_descriptor(type, index);

    return (uint8_t)(d &&
        (d->base.flags & INSTRUMENT_PARAM_FLAG_MODULATABLE) != 0u &&
        d->base.mod_domain.flags != INSTRUMENT_MOD_DOMAIN_NONE);
}
```

### 6.5 Registry self-check (`DEV_MODE_DIAGNOSTIC`)

```c
#if DEV_MODE_DIAGNOSTIC
/*
 * Validate every registry row against the plan's contract rules that a
 * _Static_assert cannot express (strings, function pointers, tables).
 * Output: 0 pass, else the first failing rule (hex digit on the FxBf boot
 * screen, main.c):
 *   1 descriptor_count < 3 or > 64
 *   2 common rows 0..2 keys differ from effect_audio_out/_level/_pan
 *   3 AUTOMATABLE flag on local index 63
 *   4 WIDE8 + AUTOMATABLE without expand7
 *   5 lanes[0] is not EFFECT_LANE_MORPH_SOURCE
 *   6 a lane names a missing descriptor, or a descriptor twice
 *   7 default_value > max_value
 *   8 runtime_bytes > sizeof(effects_runtime_t)
 *   9 token not exactly 3 chars, abbrev not 5, full name > 8
 *   A duplicate token
 * Pure: reads const tables only.
 */
static uint8_t effects_registrySelfCheck(void)
{
    static const char *const common_keys[EFFECT_COMMON_PARAM_COUNT] = {
        "effect_audio_out", "effect_level", "effect_pan"
    };
    effect_type_id_t t;
    effect_type_id_t u;
    uint8_t i;
    uint8_t lane;

    for (t = 0u; t < EFFECT_TYPE_COUNT; t++) {
        const effect_registry_entry_t *e = &effects_registry[t];
        uint64_t seen = 0u;

        if (e->descriptor_count < EFFECT_COMMON_PARAM_COUNT ||
            e->descriptor_count > EFFECT_PARAM_COUNT)
            return 1u;
        for (i = 0u; i < EFFECT_COMMON_PARAM_COUNT; i++)
            if (strcmp(e->descriptors[i].base.file_key, common_keys[i]) != 0)
                return 2u;
        for (i = 0u; i < e->descriptor_count; i++) {
            const effect_param_descriptor_t *d = &e->descriptors[i];
            uint8_t autom = (uint8_t)((d->base.flags &
                              INSTRUMENT_PARAM_FLAG_AUTOMATABLE) != 0u);

            if (autom && i >= EFFECT_TARGET_PATTERN_LOCAL_LIMIT)
                return 3u;
            if (autom && (d->effect_flags & EFFECT_PARAM_FLAG_WIDE8) &&
                !d->expand7)
                return 4u;
            if (d->default_value > d->max_value)
                return 7u;
        }
        if (e->lanes[0] != EFFECT_LANE_MORPH_SOURCE)
            return 5u;
        for (lane = 1u; lane < EFFECT_SEQ_LANE_COUNT; lane++) {
            uint8_t idx = e->lanes[lane];

            if (idx == EFFECT_LANE_NONE)
                continue;
            if (idx >= e->descriptor_count ||
                (seen & ((uint64_t)1u << idx)) != 0u)
                return 6u;
            seen |= (uint64_t)1u << idx;
        }
        if (e->runtime_bytes > sizeof(effects_runtime_t))
            return 8u;
        if (strlen(e->token3) != 3u || strlen(e->abbrev5) != 5u ||
            strlen(e->full8) > 8u)
            return 9u;
        for (u = 0u; u < t; u++)
            if (memcmp(effects_registry[u].token3, e->token3, 3u) == 0)
                return 10u;
    }
    return 0u;
}

uint8_t effects_registryCheckResult(void)
{
    return effects_registryCheckCode;
}
#endif
```

### 6.6 Runtime switching, activation, and the share callback

```c
/* Borrow the live runtime member (opaque to the manager). */
static void *effects_runtimeMember(void)
{
    return &effects_runtime;
}

/*
 * Replace the live runtime type (Scene activation with a different type, or
 * a type change on the active Scene).
 *
 * Order (plan §12.6, F6):
 *   1. FxBuffer opens the exit snapshot (fxbuf_handoffBeginExit).
 *   2. The manager records the exiting type id and channel count.
 *   3. The exiting type describes its buffer use (export_handoff).
 *   4. The runtime member is zeroed. This is DSP state, NOT arena audio, so
 *      it does not violate the no-buffer-clear rule.
 *   5. The incoming type's init() receives the handoff.
 *   6. The next service pass forces a full parameter write.
 */
static void effects_switchRuntime(effect_type_id_t incoming)
{
    const effect_registry_entry_t *old_entry =
        effects_registryEntry(effects_state.runtime_type);
    const effect_registry_entry_t *new_entry =
        effects_registryEntry(incoming);
    fxbuf_handoff_t *handoff = fxbuf_handoffBeginExit();

    if (!new_entry) {
        incoming = EFFECT_TYPE_OFF;
        new_entry = effects_registryEntry(EFFECT_TYPE_OFF);
    }
    handoff->effect_type = effects_state.runtime_type;
    handoff->effect_channels = (old_entry &&
        (old_entry->io_flags & EFFECT_IO_STEREO_OUT)) ? 2u :
        ((old_entry && old_entry->io_flags) ? 1u : 0u);
    if (old_entry && old_entry->ops && old_entry->ops->export_handoff)
        old_entry->ops->export_handoff(effects_runtimeMember(), handoff);

    memset(&effects_runtime, 0, sizeof(effects_runtime));
    effects_state.runtime_type = incoming;
    if (new_entry->ops && new_entry->ops->init)
        new_entry->ops->init(effects_runtimeMember(), fxbuf_handoff());
    effects_state.force_all = 1u;
}

/*
 * FxBuffer share-change callback (registered in effects_init). Forwards to
 * the live type and forces re-resolution so BUFFER_DEPENDENT runtime clamps
 * (effective_max) apply on the next block.
 */
static void effects_onShareChanged(const fx_share_t *share)
{
    const effect_registry_entry_t *entry =
        effects_registryEntry(effects_state.runtime_type);

    if (entry && entry->ops && entry->ops->buffer_changed)
        entry->ops->buffer_changed(effects_runtimeMember(), share);
    effects_state.force_all = 1u;
}

void effects_init(void)
{
    /*
     * Boot initialization (main.c dsp_init, after fxbuf_init and before any
     * Scene activation). The live type starts as `off`; the first
     * effects_activateScene() (preset_startDrumsetApply at boot) selects the
     * active Scene's record type. The registry self-check runs in diagnostic
     * builds only.
     */
    memset(&effects_state, 0, sizeof(effects_state));
    effects_state.runtime_type = EFFECT_TYPE_OFF;
    effects_state.scene_index = scene_getActiveIndex();
    effects_state.common.route = EFFECT_COMMON_DEFAULT_AUDIO_OUT;
    effects_state.common.pan = EFFECT_COMMON_DEFAULT_PAN;
    effects_state.common.level = EFFECT_COMMON_DEFAULT_LEVEL / 127.0f;
    effects_state.force_all = 1u;
    fxbuf_setShareChangedCallback(effects_onShareChanged);
#if DEV_MODE_DIAGNOSTIC
    effects_registryCheckCode = effects_registrySelfCheck();
#endif
}

void effects_activateScene(uint8_t scene_index)
{
    const effect_record_t *record = scene_effectConst(scene_index);
    effect_type_id_t type;

    /*
     * Point the runtime at a (newly) active Scene (called from
     * preset_startDrumsetApply and preset_sendDrumsetParameters, so every
     * Scene switch, Scene/Bank load completion, and boot passes here).
     * Same type: the runtime continues (tails ring, F6), only the parameter
     * source changes. Different or unknown type: handoff + init. Either way,
     * the next service pass writes every parameter.
     */
    if (!record)
        return;
    type = effects_registryEntry(record->type) ? record->type
                                               : EFFECT_TYPE_OFF;
    effects_state.scene_index = scene_index;
    if (type != effects_state.runtime_type)
        effects_switchRuntime(type);
    effects_state.force_all = 1u;
}
```

### 6.7 The type-change transaction

```c
uint8_t effects_changeType(uint8_t scene_index, effect_type_id_t type)
{
    const effect_registry_entry_t *entry = effects_registryEntry(type);
    effect_record_t *record;
    uint8_t i;

    /*
     * Commit a new Effect type on one Scene (plan §7.2; user rules A6, F3).
     *
     * Inputs: resident Scene and registry id. Output: 1 when committed, 0 for
     * an unknown type, invalid Scene, or no change (same type: click-out with
     * the original type is a no-op).
     *
     * Record changes, in one in-place whole commit:
     *  - type := new type;
     *  - type-specific cells 3..63 := the new type's single default, in both
     *    normal and Morph images (0 where the type has no descriptor);
     *  - the whole step sequence is cleared (every lane mask and value,
     *    including the Morph lane);
     *  - UNCHANGED: out/vol/pan (0..2) in both images, run/len/scl, and the
     *    Scene's effect_morph_amount.
     * Then, if the Scene is the runtime's Scene, the live DSP switches type
     * (handoff + init). Fan-out to masked Scenes is Step 10.
     */
    if (!entry)
        return 0u;
    record = scene_effectRecordForWholeCommit(scene_index);
    if (!record || record->type == type)
        return 0u;
    record->type = type;
    for (i = EFFECT_COMMON_PARAM_COUNT; i < EFFECT_PARAM_COUNT; i++) {
        uint8_t value = (i < entry->descriptor_count)
            ? entry->descriptors[i].default_value : 0u;

        record->normal[i] = value;
        record->morph[i] = value;
    }
    memset(record->steps, 0, sizeof(record->steps));
    scene_finishEffectWholeCommit(scene_index);
    if (scene_index == effects_state.scene_index)
        effects_switchRuntime(type);
    return 1u;
}
```

### 6.8 Resolution service, process, and accessors

```c
void effects_service(void)
{
    const effect_record_t *record =
        scene_effectConst(effects_state.scene_index);
    const effect_registry_entry_t *entry =
        effects_registryEntry(effects_state.runtime_type);
    fx_share_t share;
    uint8_t morph;
    uint8_t i;

    /*
     * Resolve retained values into DSP writes (plan §9; Step 4 layers only).
     *
     * Once per render block (mixer_calcNextSampleBlock), foreground. For every
     * descriptor of the live type:
     *   value = MORPHABLE ? interpolate(normal, morph, effect_morph_amount)
     *                     : normal
     *   value = min(value, max_value); BUFFER_DEPENDENT rows are further
     *   clamped by effective_max(share) (runtime only, A30).
     * Only a changed value (or force_all) is written: indices 0..2 update the
     * common runtime; 3..63 go to the type's write_param().
     * Rescanning every row each block replaces dirty tracking (S072_ST4 §0
     * note 1). Later layers slot in at the marked point: Pattern overlay
     * (Step 9), then FX-sequencer lock (Step 8), then LFO (Step 9).
     * Effect Morph itself resolves to the retained Scene amount in Step 4.
     */
    if (!record || !entry)
        return;
    morph = scene_getEffectMorphAmount(effects_state.scene_index);
    fxbuf_effectShare(&share);
    for (i = 0u; i < entry->descriptor_count; i++) {
        const effect_param_descriptor_t *d = &entry->descriptors[i];
        uint8_t value = (d->base.flags & INSTRUMENT_PARAM_FLAG_MORPHABLE)
            ? effects_interpolate(record->normal[i], record->morph[i], morph)
            : record->normal[i];

        /* [Step 8/9 layers: Pattern overlay, FX-seq lock, LFO go here.] */
        if (value > d->max_value)
            value = d->max_value;
        if ((d->effect_flags & EFFECT_PARAM_FLAG_BUFFER_DEPENDENT) &&
            entry->ops && entry->ops->effective_max) {
            uint8_t limit = entry->ops->effective_max(i, &share);
            if (value > limit)
                value = limit;
        }
        if (!effects_state.force_all && value == effects_state.last_applied[i])
            continue;
        effects_state.last_applied[i] = value;
        if (i == EFFECT_COMMON_PARAM_AUDIO_OUT)
            effects_state.common.route = value;
        else if (i == EFFECT_COMMON_PARAM_LEVEL)
            effects_state.common.level = value / 127.0f;
        else if (i == EFFECT_COMMON_PARAM_PAN)
            effects_state.common.pan = value;
        else if (entry->ops && entry->ops->write_param)
            entry->ops->write_param(effects_runtimeMember(), i, value);
    }
    effects_state.force_all = 0u;
}

void effects_process(effect_io_t *io)
{
    const effect_registry_entry_t *entry =
        effects_registryEntry(effects_state.runtime_type);

    /* One block with the live type; `off` (NULL process) is a no-op. The
     * mixer (Step 5) calls this after effects_service() in the same block. */
    if (entry && entry->ops && entry->ops->process && io)
        entry->ops->process(effects_runtimeMember(), io);
}

effect_type_id_t effects_activeType(void)
{
    return effects_state.runtime_type;
}

uint8_t effects_activeIoFlags(void)
{
    const effect_registry_entry_t *entry =
        effects_registryEntry(effects_state.runtime_type);

    return entry ? entry->io_flags : 0u;
}

const effects_common_runtime_t *effects_commonRuntime(void)
{
    return &effects_state.common;
}
```

---

## 7. `ResonantFilter.h` / `.c` — float-I/O block

### 7.1 `ResonantFilter.h` — insert after line 101

```c
//------------------------------------------------------------------------------------
/*
 * Float-I/O variant of SVF_calcBlockZDF (Session 072 step 4; Effects only).
 *
 * What: identical ZDF math, but samples are floats where 1.0 equals int16
 * full scale, and the output is not saturated. Why: the Effect bus sums six
 * voices and can exceed int16 full scale. The int16 path would hard-clip at
 * its input (__SSAT), whereas here the filter's own soft clipper
 * (softClipTwo x drive) shapes the overshoot. The bus return saturates once,
 * after the Effect (Step 5). Types 1..7 filter; any other type (8 = `off`
 * menu entry) passes the buffer through untouched without advancing the
 * state. Voices keep using SVF_calcBlockZDF; this function exists so voice
 * DSP is not modified. KEEP THE MATH IN SYNC with SVF_calcBlockZDF.
 * Clients: StereoFilterEffect.c.
 */
void SVF_calcBlockZDFFloat(ResonantFilter* filter, const uint8_t type, float* buf, const uint8_t size);
```

### 7.2 `ResonantFilter.c` — insert after line 320 (end of `SVF_calcBlockZDF`)

```c
//------------------------------------------------------------------------------------
#if USE_SHAPER_NONLINEARITY
#error "SVF_calcBlockZDFFloat mirrors only the non-shaper configuration"
#endif
INITCM_EFFECT_NOINLINE void SVF_calcBlockZDFFloat(ResonantFilter* filter, const uint8_t type, float* buf, const uint8_t size)
{
	/* Contract: ResonantFilter.h. Body mirrors SVF_calcBlockZDF with
	** x = input (already normalized) instead of buf/32767, and float output
	** scaled like the int path (LP: fastTanh(y1); others: value*FILTER_GAIN/
	** 32767) without __SSAT. */
	const float out_gain = (float)FILTER_GAIN / 32767.0f;
	const float f 	= filter->g;
	const float R 	= filter->f >= 0.4499f ? 1 : filter->q;
	const float ff 	= f*f;
	uint8_t i;

	if (type == FILTER_NAIVE_2_POLE)
	{
		const float f_lp2 = filter->f * 2.21f;
		for (i = 0; i < size; i++)
		{
			const float x = softClipTwo(buf[i] * filter->drive);
			const float q = (1-filter->q) *1.4 + (1-filter->q) / (1.0f - f_lp2);

			filter->a += f_lp2 * ((x - filter->a)  + q * (filter->a - filter->b ));
			if (filter->a > 1) filter->a = 1;
			else if (filter->a < -1) filter->a = -1;
			filter->b  += f_lp2 * (filter->a - filter->b );
			if (filter->b > 1) filter->b = 1;
			else if (filter->b < -1) filter->b = -1;
			buf[i] = filter->b * out_gain;
		}
		return;
	}
	if (type < FILTER_LP || type > FILTER_PEAK)
		return;		/* pass-through (menu `off`), state untouched */

	for (i = 0; i < size; i++)
	{
		const float x = softClipTwo(buf[i] * filter->drive);
#if ENABLE_NONLINEAR_INTEGRATORS
		const float ih = 0.5f * (x + filter->zi);
		filter->zi = x;
		const float scale = 0.5f;
		const float t0 = tanhXdX(scale* (ih - 2*R*filter->s1 - filter->s2 ) );
		const float t1 = tanhXdX(scale* (filter->s1 ) );
#else
		const float t0 = 1;
		const float t1 = 1;
#endif
		const float g0 = 1.f / (1.f + f*t0*2*R);
		const float s1 = filter->s1;
		const float s2 = filter->s2;
		const float f1 = ff*g0*t0*t1;
		const float y1 = (f1*x+s2+f*g0*t1*s1)/(f1+1);
		const float xx = t0*(x - y1);
		const float y0 = (softClipTwo(s1) + f*xx)*g0;

		filter->s1   = softClipTwo(filter->s1) + 2*f*(xx - t0*2*R*y0);
		filter->s2   = (filter->s2)    + 2*f* t1*y0;

		switch (type)
		{
		case FILTER_LP:
			buf[i] = fastTanh(y1);
			break;
		case FILTER_HP:
			buf[i] = (x - 2*R*y0 - y1) * out_gain;
			break;
		case FILTER_BP:
			buf[i] = y0 * out_gain;
			break;
		case FILTER_UNITY_BP:
			buf[i] = 2*R*y0 * out_gain;
			break;
		case FILTER_NOTCH:
			buf[i] = (x - 2*R*y0) * out_gain;
			break;
		case FILTER_PEAK:
			buf[i] = (y1 - (x - 2*R*y0 - y1)) * out_gain;
			break;
		default:
			break;
		}
	}
}
```

When implementing, diff this body line by line against `SVF_calcBlockZDF`
(`ResonantFilter.c:157–320`). The only intended differences are:

- the input read `buf[i]` in place of `buf[i]/32767.0f`;
- the output expressions without `__SSAT`/int conversion;
- the pre-loop pass-through for invalid types, where the int version returns
  after processing sample 0.

With `ENABLE_NONLINEAR_INTEGRATORS` 1 (the current value) the `ih`/`zi` line
order matches the int version.

---

## 8. `SceneData.h` / `.c` — in-place whole commit

### 8.1 `SceneData.h` — insert after line 414 (after `scene_commitEffectRecord` declaration)

```c
/*
 * In-place whole-record commit, in two calls (Session 072 step 4).
 *
 * What: scene_effectRecordForWholeCommit() returns the Scene's mutable Effect
 * record; the caller rewrites it and MUST then call
 * scene_finishEffectWholeCommit(), which normalizes the sequence settings,
 * marks the whole Effect region (incl. type token) for AutoSave, and
 * invalidates the Scene's card-clean bit. Why: the type-change transaction
 * rewrites most of the 420-byte record; this avoids a 420-byte stack copy
 * for scene_commitEffectRecord(). It is the SceneData rule's permitted
 * "validated whole-object commit followed by the named region marker".
 * Clients: EffectsManager (type change), Step 6 loaders. No other writer may
 * use it, and no other call may sit between the pair.
 */
effect_record_t *scene_effectRecordForWholeCommit(uint8_t scene_index);
void scene_finishEffectWholeCommit(uint8_t scene_index);
```

### 8.2 `SceneData.c` — insert after line 665 (end of `scene_commitEffectRecord()`)

```c
effect_record_t *scene_effectRecordForWholeCommit(uint8_t scene_index)
{
    scene_t *scene = scene_get(scene_index);

    /* Mutable Effect record for one whole commit; contract in SceneData.h. */
    return scene ? &scene->effect : 0;
}

void scene_finishEffectWholeCommit(uint8_t scene_index)
{
    scene_t *scene = scene_get(scene_index);

    /*
     * Close an in-place whole commit: the same normalization and marking as
     * scene_commitEffectRecord(), without the copy.
     */
    if (!scene)
        return;
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
}
```

Optionally, `scene_commitEffectRecord()` may be reduced to
`scene->effect = *record; scene_finishEffectWholeCommit(scene_index);`,
removing the duplicated normalization. Do that only if it keeps the Step 3
behaviour byte-identical; it does.

---

## 9. `Autosave.c` — live type token

### 9.1 After line 18 (`#include "InstrumentManager.h"`) — add

```c
#include "EffectsManager.h"
```

### 9.2 Before line 1088 (the Effect-parameter `if (relative >= AUTOSAVE_EFFECT_OFFSET + …PARAMETERS…`) — add

```c
    /*
     * Effect type token, Effect-relative bytes 0..2 (Session 072 step 4).
     *
     * Inputs: Scene-relative bytes 128..130. Output: the registry's
     * three-character token for the Scene's Effect type, the Effect analogue
     * of the Instrument type token. An unknown id reports absent. Why live
     * now: the Step 6 boot reader treats a valid token as "this region holds
     * a captured Effect" (Step 3 deviation 3), and the token is marked
     * together with every whole-Effect commit (autosave_markEffectDirty).
     * The name bytes (131..138) remain absent until Step 6.
     */
    if (relative >= AUTOSAVE_EFFECT_OFFSET + AUTOSAVE_EFFECT_TYPE_OFFSET &&
        relative < AUTOSAVE_EFFECT_OFFSET + AUTOSAVE_EFFECT_TYPE_OFFSET +
                       AUTOSAVE_EFFECT_TYPE_BYTES) {
        const char *token = effects_typeToken(scene->effect.type);

        if (!token)
            return 0u;
        *value = (uint8_t)token[relative - AUTOSAVE_EFFECT_OFFSET -
                                AUTOSAVE_EFFECT_TYPE_OFFSET];
        return 1u;
    }
```

Also update the router comment above it (Step 3 text "type token … not yet
live") to say that the token is live from Step 4.

### 9.3 Lines 1983–1999 — `autosave_markEffectDirty()` (modify)

After:

```c
void autosave_markEffectDirty(uint8_t scene_index)
{
    uint16_t parameter_index = 0u;
    uint16_t live_count = AUTOSAVE_EFFECT_PARAM_COUNT;
    uint16_t scene_base;
    uint8_t i;

    /*
     * Mark every live Effect cell of one Scene: the 3-byte type token
     * (Session 072 step 4) and all 419 parameter cells (step 3). The name
     * bytes join in Step 6. Callers: SceneData whole commits and
     * autosave_markSceneWithoutPatternDirty(). Marking the token with the
     * cells guarantees that a record region holding a valid token was fully
     * captured (ST3 §13.5 item 2).
     */
    if (autosave_scenePayloadBase(scene_index, &scene_base)) {
        for (i = 0u; i < AUTOSAVE_EFFECT_TYPE_BYTES; i++)
            (void)autosave_markPayloadOffsetDirty((uint16_t)(
                scene_base + AUTOSAVE_EFFECT_OFFSET +
                AUTOSAVE_EFFECT_TYPE_OFFSET + i));
    }
    while (parameter_index < live_count) {
        autosave_markEffectParameterDirty(scene_index, parameter_index);
        parameter_index++;
    }
}
```

Check `autosave_scenePayloadBase()`'s gating when implementing (`Autosave.c:395`):
it is the same helper `autosave_markEffectParameterDirty()` uses, so tracking
and presence gating stay identical.

---

## 10. `presetManager.c`

### 10.1 After line 50 (`#include "AutosaveTrace.h"`) — add

```c
#include "EffectsManager.h"
```

### 10.2 `preset_sendDrumsetParameters()` — after line 1516 (`preset_applySceneSettings(scene_getActiveIndex());`)

```c
    /*
     * Point the Effect runtime at the active Scene (Session 072 step 4).
     * Same type continues; a different type is switched with the FxBuffer
     * handoff. Kept beside the Scene-settings apply so both Scene-apply
     * paths (this synchronous one and preset_startDrumsetApply) activate the
     * Effect identically.
     */
    effects_activateScene(scene_getActiveIndex());
```

### 10.3 `preset_startDrumsetApply()` — after line 1549 (`preset_applySceneSettings(scene_getActiveIndex());`)

Insert the same block as §10.2. This path covers:

- PERF Scene switches (`menu.c:6270`);
- Scene and Bank load completion (`menu_startSoundApply`, `menu.c:586`);
- the boot replay (`main.c:1286`).

The Effect switch is immediate, not deferred to quiet voices (§0 note 2).

### 10.4 `preset_morphScene()` — after line 3000 (`scene_setAllVoiceMorphAmounts(scene_index, morph);`)

```c
    /*
     * The global Morph bulk-set also sets the Scene's Effect Morph (user rule
     * A3). Retained only: EffectsManager resolution reads the Scene amount
     * every block, so no runtime notify is needed. Inactive Scenes store the
     * value until they are activated.
     */
    scene_setEffectMorphAmount(scene_index, morph);
```

---

## 11. `mixer.c`

- After line 56 (`#include "InstrumentManager.h"`): add `#include "EffectsManager.h"`.
- After line 526 (the `instrumentManager_calcSlotAsync(slot);` loop), add:

```c
	/*
	 * Resolve the Scene Effect's retained values into its DSP state once per
	 * block (Session 072 step 4). Cheap when nothing changed: one compare per
	 * descriptor, and three rows for `off`. Step 5 adds the send bus and
	 * effects_process() after the voices; the service stays here so
	 * parameters are current before that block is processed.
	 */
	effects_service();
```

---

## 12. `main.c`

### 12.1 After line 80 (`#include "FxBuffer.h"`) — add

```c
#include "EffectsManager.h"
```

### 12.2 `dsp_init()` — after line 126 (`instrumentManager_runtimeInit();`)

```c
    /*
     * Initialize the Effect registry/runtime manager (Session 072 step 4).
     *
     * Inputs: FxBuffer bookkeeping (fxbuf_init above) and the SceneData
     * records (scene_initAll ran before dsp_init). Output: live type `off`,
     * the share-change callback registered, and (diagnostic builds) the
     * registry self-check recorded. The first real type selection happens at
     * the boot Scene activation (preset_startDrumsetApply). Affiliates:
     * Core/DSP/Effects/EffectsManager.c.
     */
    effects_init();
```

### 12.3 Diagnostic screen — line 302 and after line 308 (modify)

- Line 302: `char row2[17] = "Shr  000K st0   ";` → `char row2[17] = "Shr  000K st0 r0";`
- After line 308, add:

```c
    /* Registry self-check code (hex digit, 0 = pass; EffectsManager.c). */
    {
        uint8_t rc = effects_registryCheckResult();
        row2[15] = (char)((rc < 10u) ? ('0' + rc) : ('A' + rc - 10u));
    }
```

- Update the function comment's example rows to show `"Shr  124K st0 r0"`,
  and add "r = Effect registry self-check".

### 12.4 After line 1286 (`preset_startDrumsetApply();`, boot replay) — add

```c
#if DEV_MODE_DIAGNOSTIC && (DEV_EFFECT_FORCE_TYPE != 0u)
    /*
     * Development hook: force the active Scene's Effect type after boot
     * activation (Session 072 step 4; used for Step 4/5 bench tests until the
     * Step 7 Effect page exists). Runs the normal type-change transaction, so
     * AutoSave captures the token and defaults (closes ST3 gate 7) and the
     * runtime switches with the handoff. A no-op when the Scene already has
     * that type. Never compiled into production (DEV_MODE_DIAGNOSTIC 0).
     */
    (void)effects_changeType(scene_getActiveIndex(),
                             (effect_type_id_t)DEV_EFFECT_FORCE_TYPE);
#endif
```

---

## 13. `config.h` — after line 245 (end of the `DEV_FXBUF_FORCE_VOICE_UNITS` guard)

```c
/*
 * DEV_EFFECT_FORCE_TYPE — screen-diagnostic test knob, default 0.
 *
 * What: when DEV_MODE_DIAGNOSTIC is 1 and this is nonzero, boot commits
 * this registry id as the active Scene's Effect type right after the boot
 * Scene activation (main.c), through effects_changeType(). 1 = StereoFilter
 * (`flt`). Ignored when DEV_MODE_DIAGNOSTIC is 0.
 *
 * Why: until the Step 7 Effect page exists there is no user path to select
 * an Effect type; Steps 4–5 need one to verify AutoSave capture and the FX
 * bus. Inputs: this constant. Outputs: the active Scene's Effect record and
 * runtime. The change persists through AutoSave from Step 6 onward.
 * Affiliates: EffectsManager.h type ids, main.c boot hook, DEV_MODES.md.
 */
#define DEV_EFFECT_FORCE_TYPE 0u
```

(An unknown id is refused at runtime by `effects_changeType()`; no
preprocessor range check is possible against the registry count.)

---

## 14. `tools/decode_devlogs.py` (trace decoder update; ST3 §13.5 item 1)

- **Line 350:** `SCENE_PARAM_COUNT = 40` → `41`.
- **Line 381:** after `39: "mnt6",` add `40: "fxm_amt",`.
- **Lines 443–444:** replace the single Effect label with the Step 3/4 layout:

```python
    if rel < EFFECT_OFF + EFFECT_BYTES:
        e = rel - EFFECT_OFF
        if e < 3:
            return f"{name} effect type-token byte{e}"
        if e < 11:
            return f"{name} effect name byte{e - 3}"
        p = e - 11                      # Effect parameter index (Autosave.h)
        if p == 0:
            return f"{name} effect seq run_mode"
        if p == 1:
            return f"{name} effect seq length"
        if p == 2:
            return f"{name} effect seq step_scale"
        if p < 67:
            return f"{name} effect normal[{p - 3}]"
        if p < 131:
            return f"{name} effect morph[{p - 67}]"
        if p < 419:
            s, f = divmod(p - 131, 18)
            if f == 0:
                return f"{name} effect step{s} mask_lo"
            if f == 1:
                return f"{name} effect step{s} mask_hi"
            return f"{name} effect step{s} lane{f - 2}"
        return f"{name} effect reserved byte{e}"
```

Also update the decoder's module docstring or format-authority note to
mention Session 072 Steps 3–4. This is a read-only tool change.

---

## 15. `Makefile`

- **Includes** (after `-ICore/DSP/Effects \`): add
  `-ICore/DSP/Effects/StereoFilter \`.
- **`SRCS`** (after `Core/DSP/Effects/FxBuffer.c \`): add
  `Core/DSP/Effects/EffectsManager.c \` and
  `Core/DSP/Effects/StereoFilter/StereoFilterParameters.c \`.
- **`DSP_SRCS`:** add `Core/DSP/Effects/StereoFilter/StereoFilterEffect.c \`.
- **Rules** (after the HiHat `-Ofast` rule): add

```make
$(BUILD)/Core/DSP/Effects/StereoFilter/StereoFilterEffect.o: Core/DSP/Effects/StereoFilter/StereoFilterEffect.c | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS_DSP) $< -o $@
```

`EffectsManager.c` runs at control rate (-O2). Its `effects_service()` loop
is small; if profiling later shows it matters, move it to `DSP_SRCS`.

---

## 16. Documentation (same commit)

- **`MODULE_INTERCHANGE_SPEC.md`:**
  - New section `Core/DSP/Effects/EffectsManager` (API table from §2, the
    foreground contract, clients).
  - StereoFilter under it.
  - SceneData: the whole-commit pair.
  - Preset: the activation hooks and Effect Morph bulk-set.
  - mixer: `effects_service()`.
- **`AUTOSAVE.md`:** the Effect type token is live (Effect region bytes 0–2,
  registry token); `autosave_markEffectDirty()` marks it; the name is still
  pending Step 6.
- **`SRAM_MANIFEST.md`:**
  - `effects_state` 76 B SRAM1;
  - `effects_runtime` 76 B DTCM `.dtcmz`;
  - DTCM statics 4,160 B, arena 126,912 B at `0x20001040`;
  - `effects_registryCheckCode` 1 B, diagnostic only.
  - Regenerate from the link.
- **`DEV_MODES.md`:** `DEV_EFFECT_FORCE_TYPE`; the FxBf row 2 `r` self-check
  code table (§6.5).
- **`BANK_PRESET_ARCHITECTURE.md`:** Effect runtime ownership (EffectsManager);
  Effect Morph bulk-set from global Morph.
- **`EFFECTS_BUS_FEATURE_PLAN.md` §9:** full-rescan resolution (note 1);
  immediate type switch on activation (note 2).
- **`MEMORY.md` Volatile Notes:** "S072 Step 4: EffectsManager + registry
  (`off`, `flt`); effects_service() per block in mixer; activation via
  preset_startDrumsetApply; AutoSave Effect token live; DEV_EFFECT_FORCE_TYPE
  bench hook; nothing audible until the Step 5 bus."

---

## 17. Build and verification gates

**Build:**

1. `make clean && make all` succeeds with no new warnings. The static asserts
   pass: `effects_state_t` 76, runtime union, StereoFilter counts, and
   `EFFECT_TYPE_OFF == FXBUF_EFFECT_TYPE_NONE`.
2. `link_budget.py`: DTCM statics 4,160 B, FXBUF 126,912 B at `0x20001040`.
   Record the flash delta (expected +2–4 KiB).
3. `arm-none-eabi-nm` shows `effects_runtime` in DTCM (`0x20000xxx`) and
   `effects_state` in SRAM1.

**Hardware, production build** (`DEV_MODE_DIAGNOSTIC 0`):

4. **No audible change** in normal play, Scene switching, and Bank Load. There
   is no bus yet; `effects_service()` runs every block on `off` (three rows).
5. The `cpu` widget matches Step 3 within noise.
6. **Global Morph:** move PERF `mrp` and wait for AutoSave. Scene parameter
   40 (`fxm_amt` in the updated decoder) is now marked and captured. This is
   the first real exercise of index 40.

**Hardware, diagnostic build** (`DEV_MODE_DIAGNOSTIC 1`,
`DEV_EFFECT_FORCE_TYPE 1u`):

7. The boot screen shows `Shr  124K st0 r0` (registry self-check passes).
8. **ST3 gate 7 and the type-token capture:** after boot, wait for AutoSave
   `T DONE`, then copy the card. In the newer `.hcprms`, the active Scene
   *s* Effect region reads:
   - bytes 0–2 `66 6C 74` (`flt`);
   - from offset 11: `00 10 04` (run/len/scale), then normal
     `00 7F 40 40 00 00 00 00 …` (out, vol, pan, freq 64, reso, drive,
     type LP);
   - Morph identical;
   - steps zero.

   The updated decoder labels these as `effect type-token byte0..2`,
   `effect normal[..]`, and so on.
9. **Scene switching** between the forced Scene and an `off` Scene, and back,
   is silent and stable (the runtime switches; the audio path is unused).
10. **Same-type continuity:** force the type on two Scenes (switch Scenes and
    reboot, or use the hook twice across boots). Switching between them
    exercises the same-type path. It is not audible yet; confirm stability.
11. Restore `DEV_MODE_DIAGNOSTIC 0` and `DEV_EFFECT_FORCE_TYPE 0u` before
    committing.

**Rollback:** revert the commit. AutoSave records written by Step 4 carry a
type token, which Step 3 firmware reports as absent and ignores. No card
format hazard.

---

## 18. Implementation notes

### 18.1 Source completed

- Added the `off`/`flt` registry and `EffectsManager` runtime owner, including
  descriptor lookup, capability checks, type-change defaults, in-place Scene
  whole-record commit, FxBuffer handoff, share-change notification, and the
  full descriptor rescan used by `effects_service()`.
- Added the StereoFilter descriptor/runtime pair and the float-I/O ZDF helper
  in `ResonantFilter.c`. The helper preserves the existing filter type
  semantics and is not called by the audio path until Step 5.
- Added Scene activation hooks, global-Morph Effect propagation, live AutoSave
  type-token projection/dirty marking, the diagnostic registry result, the
  `DEV_EFFECT_FORCE_TYPE` hook, Makefile sources/rules, and decoder labels.
- Updated the module, AutoSave, development-mode, preset, Effects plan,
  memory, and SRAM-reference documentation to describe the implemented
  boundary and the remaining deferred phases.

### 18.2 Linked verification

The integrated build passed with the existing unrelated warnings only. The
linked image measured `text=463,552`, `data=416`, `bss=425,936`.

- `effects_state`: 76 B in SRAM1 `.bss` at `0x200211f8`.
- `effects_runtime`: 76 B in DTCM `.dtcmz` at `0x20000f30`.
- DTCM statics: 4,160 B; `.dtcm_fxbuf`: 126,912 B at `0x20001040`.
- SRAM1 normal static use: 295,792 B of 376,832 B, leaving 81,040 B.
- Static data RAM including the arena: 426,864 B; including ITCM code:
  430,632 B.
- Final production image SHA-256: `19c28feb138e8d371f5d755216631d420d3a3c70e80ed15d2dbe4ea6f19f3134`.

The diagnostic configuration was also clean-linked with
`DEV_MODE_DIAGNOSTIC=1` and `DEV_EFFECT_FORCE_TYPE=1u`; it exercised the
registry self-check and boot force-type compilation path. The production
defines were restored and the final image above was rebuilt with the default
`DEV_MODE_DIAGNOSTIC=0` and `DEV_EFFECT_FORCE_TYPE=0u`.

### 18.3 Remaining validation

The Step 4 production/diagnostic hardware gates remain pending hardware
access. Step 4 intentionally does not connect the Effect bus, so no audible
change is expected until Step 5. Before hardware testing, use
`DEV_MODE_DIAGNOSTIC=1` with `DEV_EFFECT_FORCE_TYPE=1u`, capture the boot
registry result and AutoSave token, then restore both development knobs to
their production values.

---

## 18. Review assessment (2026-09-27, post-implementation)

**Verdict:** accepted. The Step 4 source matches this schedule, and the build
succeeds. **Nothing is audible from this step, by design.**
`effects_process()` has no caller until the Step 5 bus. The only runtime
activity is `effects_service()` resolving the `off` type's three common rows
every block.

### 18.1 Source review (commit `1e47c56`, which contains Steps 1–4)

- **`EffectsManager.h/.c`** matches §2 and §6:
  - the registry (`off`, `flt`) and lookups;
  - `effects_paramAutomatable()` rules (local 63; WIDE8 needs expand7);
  - the self-check codes 1–A;
  - `effects_switchRuntime()` in handoff order (BeginExit → type/channels →
    export → zero runtime → init → force);
  - `effects_activateScene()`, and `effects_changeType()`, which keeps
    0..2, run/len/scale, and Morph amount, resets 3..63 to defaults, and
    clears all steps;
  - the full-rescan `effects_service()` and the accessors;
  - `effects_interpolate()` is byte-identical in arithmetic to
    `presetMorph_interpolate()`.
- **`EffectParamRows.h`, `StereoFilterParameters.c/.h`,
  `StereoFilterEffect.c/.h`** match §3–§5:
  - defaults 64/0/0/LP (D1 confirmed);
  - linked coefficients;
  - `init` also links the coefficients after `SVF_init` (harmless and
    correct).
- **`SVF_calcBlockZDFFloat()`** matches §7:
  - the invalid-type pass-through is checked before any state update;
  - the output expressions equal the int16 version without `__SSAT`;
  - literals are float-suffixed (`1.4f`, where the int16 version has double
    `1.4`), a negligible difference under -Ofast.
- **Hooks:**
  - `preset_sendDrumsetParameters()` (`:1519`) and
    `preset_startDrumsetApply()` (`:1554`) call `effects_activateScene()`;
  - `preset_morphScene()` bulk-sets Effect Morph (`:3007`);
  - `mixer.c:537` calls `effects_service()`;
  - `main.c:129` calls `effects_init()`;
  - the diagnostic row 2 has the `r` code (`main.c:314`);
  - the boot dev hook is at `main.c:1297–1307`.
- **AutoSave:** the type-token router (`Autosave.c:1088`), and the token
  marking in `autosave_markEffectDirty()` (`:2011–2018`).
- **SceneData:** the whole-commit pair (`SceneData.c:667/675`).
- **`config.h` `DEV_EFFECT_FORCE_TYPE`**, the Makefile (include, `SRCS`,
  `DSP_SRCS`, -Ofast rule), and the decoder update (`SCENE_PARAM_COUNT 41`,
  `fxm_amt`, Effect region labels) are all present.

### 18.2 Build

| Measurement | Result | vs Step 3 |
|---|---|---|
| `text`/`data`/`bss` | 463,552 / 416 / 425,936 | +4,888 / 0 / +84 |
| Flash | 463,968 B; **27,552 B headroom** | −4,888 B headroom |
| DTCM statics | 4,160 B (`effects_runtime` 76 B at `0x20000F30`) | +76 |
| FX arena | 126,912 B at `0x20001040` (margin 4,032 B) | −64 |
| `effects_state` | 76 B SRAM1 | new |

The flash growth is above the §17 estimate of 2–4 KiB. Most of it is
`SVF_calcBlockZDFFloat` at **2,772 B**, which -Ofast unrolls to the same size
class as the existing int16 `SVF_calcBlockZDF` (~2.9 KB). The rest is spread
over:

- `effects_service` 404 B;
- `stereoFilter_writeParam` 316 B;
- `stereoFilter_descriptors` 252 B;
- `effects_activateScene` 192 B;
- the registry 128 B;
- the `off` descriptors 108 B;
- the self-check, which is diagnostic-only and absent here.

Headroom is still well above the 16 KiB warning threshold. It is now the
number to watch through Steps 5–10.

**Build-host note:** the first clean link aborted with `lto1: internal
compiler error: Bus error: 10` and left an empty `build/lxr02.elf`, so a
plain rerun then failed at objcopy ("input file is empty"). Deleting the ELF
and relinking succeeded with no source change. This is a host/toolchain
fault, consistent with the earlier transient empty-ELF report (ST2 §26.3); it
most likely happens when two builds share `build/`. If it recurs, delete
`build/lxr02.elf` before rebuilding.

### 18.3 What can be tested (none of it is audible)

Step 4 changes no sound. Its hardware-observable effects are bookkeeping, as
listed in §17:

1. **Regression, production build:** normal play, Scene switching, Bank Load,
   and a `cpu` reading equal to Step 3.
2. **Global Morph → Scene parameter 40:** moving PERF `mrp` now marks
   `fxm_amt`, which the updated decoder shows in `asavetrc.bin` and the
   records.
3. **Diagnostic build with `DEV_EFFECT_FORCE_TYPE 1u`:**
   - the boot screen `r0`;
   - the active Scene's `.hcprms` Effect region reads `66 6C 74` + `00 10 04
     00 7F 40 40 00 00 00 …`. This also closes ST3 gate 7.

**Recommendation:** don't run a separate Step 4 bench session. Items 2–3 need
the same diagnostic build and card copy that Step 5's first listening test
uses (the dev hook selects `flt` and the bus makes it audible). Running both
checks in the Step 5 session covers Step 4 at no extra cost. Item 1 is only a
quick flash-and-play check, if wanted now.
