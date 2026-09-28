# S072 Step 6 — Implementation Schedule: Effect Storage

**Covers:** `.fx` v2, HCNAMES 161 rows, Scene/Bank `<name>.fx`, the
`sceneset.scg` key, and AutoSave v3 with its Effect reader.

**Scope:** `EFFECTS_BUS_FEATURE_PLAN.md` §7.1, §7.3, §14.1–§14.4, §15, and
§17.1 Step 6. The plan's Step 6 row reads: "`.fx` v2 parse/write; HCNAMES 161
rows with Effect provenance; Scene/Bank `<name>.fx` load/save; `sceneset.scg`
key; AutoSave header bump and Effect reader".

**Gate:** a hand-written fixture card covering legacy, v2, missing, malformed,
and a partial Bank, plus reboot restore.

**What becomes observable in the production image** (the first time in Phase 5):

- A Scene folder holding a hand-written `.fx` with `type=flt` loads a live
  StereoFilter through Scene Load or Bank Load.
- With a voice FX send raised, the filter is audible. All Step 5 fader-mode
  and send gates can therefore be run without the diagnostic dev hook.
- Scene Save writes the Effect back as `<name>.fx` v2.
- A reboot restores the Effect through AutoSave.

**Not in this step:**

- the Effect library `/Effect/NNN <name>.fx` and its browser (A35, §14.5);
- UI (Step 7);
- sequencer playback (Step 8);
- automation and LFO (Step 9);
- the edit-mask gate (Step 10).

**Status:** schedule only. No source has been changed.

**Line numbers** refer to the Step 5 working tree reviewed in
`S072_ST5_IMPLEMENTATION.md` §14.

---

## 0. Decisions and notes

### D1 — Effect is staged and committed atomically with the Scene and Kit (confirm)

Today the Scene loader has this order:

1. scan (phase 9);
2. `sceneset.scg` (12–16);
3. embedded Kit (17–32);
4. **commit Scene and Kit (33)**;
5. Pattern (44–53);
6. **`.fx` validation (56–60)**;
7. publish (61).

A `.fx` failure therefore arrives after the Scene and Kit are already live.

Plan §14.3 requires "the commit is all-or-nothing per Scene", with the Effect
stage co-resident with the Scene stage.

**Proposal:**

- Run phases 56–60 immediately after `sceneset.scg` closes (phase 16), while
  the working directory is still the Scene folder. Continue to the Kit at
  phase 17 afterwards.
- The parser fills a new `effect` member of `filesystem_scene_stage_t`.
  `filesystem_commitSceneStage()` copies settings, Kit and Effect together.
- A malformed `.fx` fails before any resident byte changes.
- The Pattern keeps its existing non-atomic policy.
- A missing `.fx` no longer fails phase 11. It stages `off` with a blank name
  (A37, G6).
- Bank-local children use the same path unchanged.

The Scene stage is 1,201 B and the Effect record is 420 B, so the stage is
1,621 B and fits in the existing 2,048 B staging union. This adds **no RAM**.

### D2 — Effect provenance field in the AutoSave Effect region (confirm)

HCNAMES Effect rows carry a source token (plan §14.2). The AutoSave Effect
region (§15) has no source field, while every Scene, Kit and Instrument
sub-object has one. The boot reader needs that field for its Case 1
cross-check, and winner regeneration needs it to rebuild the row.

**Proposal:** two little-endian bytes at **Effect-relative offset 430..431**.
These are the first bytes of the 82-byte reserved tail, immediately after the
419 live cells (`11 + 419 = 430`).

- This mirrors the Phase C source fields, which also absorbed reserved bytes.
- Region size, record size and mask are unchanged.
- The name bytes at 3..10 are populated the same way as the Scene, Kit and
  Instrument names: in the baseline record only (see note 3).

### D3 — `.fx` `step_scale` text tokens (confirm)

The shared 14-entry step-scale table (§11.3) is built in Step 8. The `.fx`
file must name the retained index now.

**Proposal:** the file-schema tokens below, in index order. Index 4 (`1/16`)
is the default. Step 8's table must keep this index order.

| Index | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Token | `1/64` | `1/32t` | `1/32` | `1/16t` | `1/16` | `1/8t` | `1/16.` | `1/8` | `1/4t` | `1/8.` | `1/4` | `1/2` | `1bar` | `2bar` |
| Ticks | 6 | 8 | 12 | 16 | 24 | 32 | 36 | 48 | 64 | 72 | 96 | 192 | 384 | 768 |

- The tokens contain no spaces, so hand-edited files cannot be broken by
  trailing whitespace.
- The run-mode tokens are `fwd`, `rev`, `pip`, `rnd`, `sel` (§11).

### D4 — A blank Effect name saves as `none.fx` (confirm understanding)

G5 says Effect names use the Instrument naming path unchanged: "whatever
`' .drm'` does today, `' .fx'` does the same".

- Today `storage_makeSavedInstrumentDisplayFilename()` turns an all-space stem
  into **`none`** (`storageTypes.c:752-767`, fallback at 756). A blank Instrument therefore
  saves as `none.drm`, not `' .drm'`.
- Through the same helper, a blank Effect saves as **`none.fx`**.
- Loading `none.fx` gives the name `none`. `filesystem_residentNameIsBlank()`
  already treats `none` as blank, so the HCNAMES row stays blank.
- The round trip is therefore stable.
- F4's `' .fx'` wording is superseded by G5; plan §14.2 gets a clarifying
  line.
- Nothing outside the Effects framework changes. The Effect helper calls the
  Instrument helper and then swaps the extension (§4.6).

### D5 — AutoSave format version 2 → 3; card preparation (acknowledge)

- Version 3 identifies the 161-row identity image, the Effect source field,
  and the live Effect reader.
- Version 2 records are rejected by the existing strict version check, and a
  fresh baseline is created (A38).
- A 145-row `.hcnames` fails the exact-row-count parser
  (`filesystem_bootReaderParseRegisterFile`), so the boot reader declines.
- **Card preparation before the first Step 6 boot** (A38, recommended): delete
  root `.hcnames`, `.hcnamtmp`, `.hcprms1` and `.hcprms2`.
  - The first boot then runs the canonical Bank Load and bootstraps both
    files.
  - Firmware recovery without the deletion is expected but not proven, and it
    is not in scope.

### D6 — RAM (acknowledge)

| Object | Region | Bytes | Status |
|---|---|---|---|
| `fs_resident_source[161]` (was 145) | SRAM1 | +32 | approved, §16 item 3 |
| `hcnames_name_mirror[161][9]` (was 145) | SRAM1 | +144 | approved, §16 item 3 |
| `op_effect_display_name[9]` | SRAM1 | **+9** | new: Effect row name captured during Scene Load/Save, like `op_pattern_display_name` |
| `storage_effect_state_t` 3 → 7 B (`op_effect_state`) | SRAM1 | **+4** | new: v2 parser state |
| Scene stage `effect_record_t` | stage union | 0 | fits the fixed 2,048 B union |

- Total: +189 B, of which **13 B is new** and requests acknowledgement.
- DTCM is unchanged.
- Estimated flash: **+4 to +6 KB** for the parser, writer, boot Effect loader,
  and AutoSave apply. Headroom is 25,752 B.

### Notes (no decision needed)

1. **No direct Effect sources yet.**
   - Step 6 Effect rows are always `-` (inherit from the Scene or Bank child)
     or `?`.
   - A numeric source written directly on an Effect row is treated as
     unresolvable, like the existing Instrument-row guard, until the Effect
     library exists.
2. **Boot Case 2 (narrow reload) opens `<row name>.fx` in the Scene folder.**
   - It uses the same name builder as Save, as the Pattern child does.
   - A hand-renamed file whose stem is longer than 8 characters therefore
     reloads as `off` on a Case 2 boot. Instrument files have the same limit.
   - Keep fixture stems to 8 characters or fewer.
3. **AutoSave name bytes are baseline-only**, like Scene, Kit and Instrument
   names.
   - `autosave_getLivePayloadByte()` never projects name cells. The boot
     reader takes names from `.hcnames`.
   - The record's name bytes are used only by winner regeneration.
   - Effect names follow exactly the same model.
4. **Runtime activation needs no change.**
   - Scene and Bank Load completion calls `preset_startDrumsetApply()`, which
     calls `effects_activateScene()` (`presetManager.c:1560`).
   - Boot calls `preset_sendDrumsetParameters()`, which does the same
     (`presetManager.c:1525`).
   - A loaded Effect type therefore becomes the runtime type through existing
     paths.
5. **Scene Save phases 33–36 are unreachable** (nothing sets `op_phase = 33u`).
   They are updated for consistency only; removing them is outside Effects
   scope.
6. **`tools/verify_bank_autosave.py` is already stale** (it expects 129 rows).
   It is not touched here (G5). The fixture checks below are manual.

---

## 1. Change index

| # | File | Line(s) | Op | Summary |
|---|---|---|---|---|
| 1 | `Core/DSP/Effects/EffectsManager.h` | after 183 | add | `EFFECT_LANE_MORPH_FILE_KEY`, `effects_recordDefaultsForType()`, `effects_laneFileKey()`, `effects_laneByFileKey()` |
| 2 | `Core/DSP/Effects/EffectsManager.c` | after 233 | add | Implementations of #1 |
| 3 | `Core/Hardware/SD/storageTypes.h` | 210–223 | modify | `storage_effect_state_t` v2 |
| 4 | `Core/Hardware/SD/storageTypes.h` | 541–544, 546–548 | modify | Effect parse/format/filename API |
| 5 | `Core/Hardware/SD/storageTypes.c` | after 20 | add | `#include "EffectsManager.h"` |
| 6 | `Core/Hardware/SD/storageTypes.c` | 628–633, 645–650 | modify | Stale "future FX" sceneset comments |
| 7 | `Core/Hardware/SD/storageTypes.c` | 660 | modify | `effect_morph_amount` sceneset key |
| 8 | `Core/Hardware/SD/storageTypes.c` | 1082–1154 | remove | Placeholder parser |
| 9 | `Core/Hardware/SD/storageTypes.c` | 1164–1172 | modify | v2 parser, writer, and filename helper (replaces the placeholder writer) |
| 10 | `Core/Bank/Scene/Autosave.h` | 36–48 | modify | Format version 3 |
| 11 | `Core/Bank/Scene/Autosave.h` | 91–133 | modify | 161 rows, `AUTOSAVE_HCNAMES_EFFECT_BASE` |
| 12 | `Core/Bank/Scene/Autosave.h` | after 193 | add | `AUTOSAVE_EFFECT_SOURCE_OFFSET` and asserts |
| 13 | `Core/Bank/Scene/Autosave.h` | 344–346 | modify | Row-mapping assert |
| 14 | `Core/Bank/Scene/Autosave.h` | after 651 | add | `autosave_applyEffectPayload()` prototype |
| 15 | `Core/Bank/Scene/Autosave.c` | after 562 | add | Baseline Effect name bytes |
| 16 | `Core/Bank/Scene/Autosave.c` | 1081–1087, before 1100 | modify/add | Getter comment; Effect source bytes |
| 17 | `Core/Bank/Scene/Autosave.c` | after 1375 | add | `autosave_setEffectParameter()` and `autosave_applyEffectPayload()` |
| 18 | `Core/Bank/Scene/Autosave.c` | 1745–1756, 1783 | modify | `autosave_markSourceDirty()` Effect branch |
| 19 | `Core/Bank/Scene/Autosave.c` | 1996–2021 | modify | `autosave_markEffectDirty()` marks the Effect source |
| 20 | `Core/Bank/Scene/Autosave.c` | 2204 | modify | `autosave_objectFullyCaptured()` Effect branch |
| 21 | `Core/Hardware/SD/filesystem.c` | 129–151 | modify | Effect row block, 161 rows |
| 22 | `Core/Hardware/SD/filesystem.c` | 872–885, 962–983 | modify | Scene stage holds the Effect; reserve 420 |
| 23 | `Core/Hardware/SD/filesystem.c` | 1015–1080 | modify | Register comments and asserts, cross-module row assert |
| 24 | `Core/Hardware/SD/filesystem.c` | 1238–1248 | modify/add | Effect scratch |
| 25 | `Core/Hardware/SD/filesystem.c` | 1478, 1589 | modify/add | Prototypes |
| 26 | `Core/Hardware/SD/filesystem.c` | after 5636 | add | `filesystem_residentEffectRow()` and row-class predicates |
| 27 | `Core/Hardware/SD/filesystem.c` | 5669, 5727, 5741, 5813, 5940 | modify | Pattern-only tests bounded; Effect resolution |
| 28 | `Core/Hardware/SD/filesystem.c` | 6097–6114, 6297–6303, 6389–6396 | modify | Refresh witness; Scene and Bank row overlays |
| 29 | `Core/Hardware/SD/filesystem.c` | 7665, 7688 | modify | AutoSave refresh sweeps include Effect rows |
| 30 | `Core/Hardware/SD/filesystem.c` | 11012–11016 | modify | 1,599 B scratch assert |
| 31 | `Core/Hardware/SD/filesystem.c` | 12040–12045, 12058–12063, 12143–12150 | modify | Scene Load scan, required children, phase 16 routing |
| 32 | `Core/Hardware/SD/filesystem.c` | 12333–12358 | modify | Scene Load stages the Effect-row source |
| 33 | `Core/Hardware/SD/filesystem.c` | 12664–12679, 12921, 12924–12998 | modify | Pattern → publish; Effect phases 56–60 reworked |
| 34 | `Core/Hardware/SD/filesystem.c` | 16231–16303, 16341 | modify | Stage defaults, commit, discovery reset |
| 35 | `Core/Hardware/SD/filesystem.c` | 16872, 16878–16889 | modify | sceneset line 10; Effect writer adapter |
| 36 | `Core/Hardware/SD/filesystem.c` | 19451–19466, 19574–19626, 19686–19727, 19797–19830 | modify | Scene Save writes `<name>.fx` v2 and stages the Effect row |
| 37 | `Core/Hardware/SD/filesystem.c` | 21541–21544, 21689–21700, 28113–28116 | modify | Comments: Effect rows carry no type column |
| 38 | `Core/Hardware/SD/filesystem.c` | 25551 | add | Operation reset clears the Effect name |
| 39 | `Core/Hardware/SD/filesystem.c` | 26347–26446, 26448–26475 | modify | Regeneration classifier: Effect name and source |
| 40 | `Core/Hardware/SD/filesystem.c` | after 27743 | add | `filesystem_bootReaderNarrowLoadEffect()` |
| 41 | `Core/Hardware/SD/filesystem.c` | 28150, 28226–28290, 28309–28324 | modify | Parser comment; EmptyScene; Effect numeric guard |
| 42 | `Core/Hardware/SD/filesystem.c` | 28326–28490 | modify | `filesystem_bootReaderEvaluateScene()`: nine rows |
| 43 | `Core/Hardware/SD/filesystem.c` | 28740–28800 | modify | `filesystem_bootHcnamesAuthoritativeLoad()`: nine rows |
| 44 | `Core/Hardware/SD/filesystem.h` | 385, 939, 1032 | modify | 145 → 161 comments |
| 45 | `Core/Bank/Scene/Preset/presetManager.c` | on_scene_save_complete comment (~539) | modify | "effect placeholder" → `<name>.fx` v2 |
| 46 | Docs | — | modify | FILESYSTEM_SPEC, AUTOSAVE, SRAM_MANIFEST, MODULE_INTERCHANGE, BANK_PRESET_ARCHITECTURE, plan §14/§15, MEMORY |

There is no change to SceneData, `mixer`, `menu`, `sequencer`, Kit Load, or
Instrument Load.

---

## 2. `EffectsManager.h` — after line 183 (`effects_paramModulatable` prototype)

```c
/*
 * `.fx` storage helpers (Session 072 step 6; plan §14.1).
 *
 * effects_recordDefaultsForType() builds one complete, unowned record for a
 * registered type: SceneData's `off` defaults (common out/vol/pan, fwd /
 * length 16 / 1/16, no locks), then the type id and every type-specific index
 * 3..63 set to that type's single default in BOTH endpoint images (0 where
 * the type has no row), the same values effects_changeType() installs.
 * Unknown types build `off`. It never marks AutoSave and never touches a
 * resident Scene: the caller owns the commit. Clients: the `.fx` parser,
 * AutoSave's Effect reader, and the boot narrow Effect loader.
 *
 * effects_laneFileKey() / effects_laneByFileKey() translate FX-sequence lane
 * numbers to and from their `.fx` `[sequence]` key (after the `lane.` prefix).
 * Lane 0 is always EFFECT_LANE_MORPH_FILE_KEY; other lanes use the descriptor
 * file_key the registry lane table names. EFFECT_LANE_NONE lanes have no key
 * (NULL / not found), so a file cannot address an unused lane. Why here: the
 * registry owns lane meaning; storage must not duplicate that table.
 */
#define EFFECT_LANE_MORPH_FILE_KEY "effect_morph"
void effects_recordDefaultsForType(effect_record_t *record,
                                   effect_type_id_t type);
const char *effects_laneFileKey(effect_type_id_t type, uint8_t lane);
uint8_t effects_laneByFileKey(effect_type_id_t type, const char *file_key,
                              uint8_t *lane_out);
```

---

## 3. `EffectsManager.c` — after line 233 (end of `effects_paramModulatable()`)

```c
void effects_recordDefaultsForType(effect_record_t *record,
                                   effect_type_id_t type)
{
    const effect_registry_entry_t *entry = effects_registryEntry(type);
    uint8_t index;

    /*
     * Build a type's complete default record (contract in EffectsManager.h).
     *
     * Inputs: caller-owned record and registry id. Output: `off` defaults
     * from SceneData, then this type's token and 3..63 defaults in normal and
     * Morph images. Unknown ids leave the `off` record. No AutoSave marks.
     */
    if (!record)
        return;
    scene_effectRecordDefaults(record);
    if (!entry)
        return;
    record->type = type;
    for (index = EFFECT_COMMON_PARAM_COUNT; index < EFFECT_PARAM_COUNT;
         index++) {
        uint8_t value = (index < entry->descriptor_count) ?
                        entry->descriptors[index].default_value : 0u;

        record->normal[index] = value;
        record->morph[index] = value;
    }
}

const char *effects_laneFileKey(effect_type_id_t type, uint8_t lane)
{
    const effect_registry_entry_t *entry = effects_registryEntry(type);
    uint8_t index;

    /* Lane 0 is the fixed Effect Morph source; others name descriptors. */
    if (!entry || lane >= EFFECT_SEQ_LANE_COUNT)
        return NULL;
    index = entry->lanes[lane];
    if (index == EFFECT_LANE_MORPH_SOURCE)
        return EFFECT_LANE_MORPH_FILE_KEY;
    if (index == EFFECT_LANE_NONE || index >= entry->descriptor_count)
        return NULL;
    return entry->descriptors[index].base.file_key;
}

uint8_t effects_laneByFileKey(effect_type_id_t type, const char *file_key,
                              uint8_t *lane_out)
{
    uint8_t lane;

    /* Linear scan of at most 16 lanes; used only while parsing `.fx`. */
    if (!file_key)
        return 0u;
    for (lane = 0u; lane < EFFECT_SEQ_LANE_COUNT; lane++) {
        const char *key = effects_laneFileKey(type, lane);

        if (key && strcmp(key, file_key) == 0) {
            if (lane_out)
                *lane_out = lane;
            return 1u;
        }
    }
    return 0u;
}
```

`EffectsManager.c` already includes `<string.h>`, which it uses for `strcmp`
in `effects_descriptorByKey`. It already reaches SceneData
(`scene_effectConst`).

---

## 4. `storageTypes`

### 4.1 `storageTypes.h` lines 210–223 — replace the placeholder state

```c
/*
 * Incremental parse state for one `.fx` Effect file (Session 072 step 6).
 *
 * Two grammars are accepted. Version 2 is the live format (plan §14.1):
 * top-level format/version/type, then optional [params], [morph], and
 * [sequence] sections. Version 1 with placeholder=1 is the legacy Scene
 * placeholder and loads as `off` (A37). type is the registry id resolved from
 * the three-character token; it must precede any section because section keys
 * are that type's descriptor and lane keys. current_section selects the
 * destination image. seen_* bits let Finalize reject incomplete files and
 * decide whether a missing [morph] copies [params]. The parser writes into a
 * caller-owned effect_record_t that Init reset; nothing resident changes until
 * filesystem.c commits the staged record. Clients: Scene Load phases 56..60
 * and the boot reader's narrow Effect loader.
 */
typedef struct {
    effect_type_id_t type;
    uint8_t current_section;
    uint8_t version;
    uint8_t seen_format;
    uint8_t seen_type;
    uint8_t seen_placeholder;
    uint8_t seen_morph_section;
} storage_effect_state_t;
```

### 4.2 `storageTypes.h` lines 541–544 and 546–548 — replace the Effect API (keep line 545)

```c
/*
 * `.fx` v2 parse, format, and filename helpers (Session 072 step 6; §14).
 *
 * Init resets the parser and fills target with `off` defaults. ParseLine
 * consumes one NUL-terminated line (CR/LF removed). Unknown top-level keys,
 * unknown sections, unknown parameter keys, and unknown lane keys are ignored
 * as in Instrument files; malformed values, an unknown or repeated type token,
 * a section before type=, and unsupported versions fail. Parameter values are
 * clamped to the descriptor maximum; [morph] writes only Morphable rows.
 * Finalize requires format + version and either type= (v2) or placeholder=1
 * (v1, loads `off`); a v2 file without a [morph] section copies every
 * Morphable [params] value into the Morph image.
 *
 * storage_formatEffectLine() streams the same grammar from a retained record,
 * one line per line_index, returning 0 after the final lane line. [sequence]
 * emits run_mode, length, step_scale, then `lane.<key>=0xMMMM,v0,...,v15` for
 * every named lane (bit s of the mask is step s). Unknown record types are
 * written as `off`.
 *
 * storage_makeSavedEffectDisplayFilename() produces `<stem>.fx` through the
 * unchanged Instrument stem rules (G5): printable-safe characters, trailing
 * spaces trimmed, all-space stem -> `none`. capacity must be at least 13.
 */
void storage_effectStateInit(storage_effect_state_t *state,
                             effect_record_t *target);
storage_status_t storage_effectParseLine(storage_effect_state_t *state,
                                         const char *line,
                                         effect_record_t *target);
storage_status_t storage_effectFinalize(const storage_effect_state_t *state,
                                        effect_record_t *target);
uint8_t storage_formatEffectLine(char *dst,
                                 uint16_t capacity,
                                 const effect_record_t *record,
                                 uint16_t line_index);
void storage_makeSavedEffectDisplayFilename(char *dst,
                                            uint8_t capacity,
                                            const char *stem);
```

`effect_record_t` is already visible through `SceneData.h` → `EffectTypes.h`.

### 4.3 `storageTypes.c` — after line 20 (`#include <stdint.h>`)

```c
#include "EffectsManager.h" /* `.fx` registry tokens, keys, lanes, defaults */
```

### 4.4 `storageTypes.c` — sceneset comments (modify, comment only)

Two sceneset comments still describe FX send and fader mode as future work,
which Step 5 made live.

- **Lines 628–633** (`fx_send_amount`): replace the comment body with:

  "Parse retained per-voice FX-send amounts. Inputs: six comma-separated
  0..127 values, one per instrument slot. Output: staged Scene settings; the
  mixer pulls the effective amount every block (Session 072 step 5), so
  storage applies nothing at runtime."

- **Lines 645–650** (`fader_setting`): replace the comment body with:

  "Parse retained per-voice fader modes. Inputs: six comma-separated values in
  the 0..2 domain (mixer.h MIXER_FADER_*). Output: staged Scene settings; the
  mixer reads the mode every block (Session 072 step 5)."

### 4.5 `storageTypes.c` line 660 — add the `effect_morph_amount` key

Replace the closing `    }` of the `fader_setting` branch at line 660 with
the branch below. Its closing brace ends the else-if chain before
`return STORAGE_STATUS_OK;` at line 661.

```c
    } else if (storage_streq(key, "effect_morph_amount")) {
        /*
         * Parse the Scene-level Effect Morph amount (Session 072 step 6).
         *
         * Input: one 0..255 value. Output: staged Scene settings. The key is
         * optional: older sceneset files keep the staged default 0. The amount
         * is a Scene setting and is deliberately absent from `.fx` (plan §7.1),
         * so a Scene Load carries it while an Effect file alone never does.
         */
        if (!target_settings)
            return STORAGE_STATUS_BAD_VALUE;
        st = storage_parseU8(value, &parsed);
        if (st != STORAGE_STATUS_OK)
            return st;
        target_settings->effect_morph_amount = parsed;
    }
```

### 4.6 `storageTypes.c` — Effect code

**Remove lines 1082–1154:** the placeholder `storage_effectStateInit()`,
`storage_effectParseLine()`, `storage_effectFinalize()`, and the trailing blank
line.

**Replace lines 1164–1172** (the comment and
`storage_formatEffectPlaceholderLine()`) with the block below. It now follows
`storage_patternHex()` (line 1156), which the lane parser reuses.

```c
/* ---------------------------------------------------------------------------
 * `.fx` Effect files (Session 072 step 6; plan §14.1).
 *
 * Grammar (v2):
 *   format=helicase.effect
 *   version=2
 *   type=<token3>
 *   [params]    <descriptor file_key>=<0..255>   (clamped to max_value)
 *   [morph]     same keys, Morphable rows only; absent section = copy [params]
 *   [sequence]  run_mode=<fwd|rev|pip|rnd|sel>
 *               length=<1..16>
 *               step_scale=<token, S072_ST6 D3>
 *               lane.<lane key>=0x<mask>,<16 values 0..255>
 * Legacy (v1): format/version=1/placeholder=1 -> `off`.
 * The file never carries its own name: the filename stem is the Effect name.
 * ------------------------------------------------------------------------- */

#define STORAGE_SECTION_SEQUENCE   3u
#define STORAGE_SECTION_UNKNOWN    0xffu
/* "lane." plus the longest descriptor file_key, with headroom. */
#define STORAGE_EFFECT_KEY_MAX     40u
#define STORAGE_EFFECT_LANE_PREFIX "lane."
#define STORAGE_EFFECT_LANE_PREFIX_LEN 5u

/*
 * FX-sequence run-mode tokens in effect_seq_run_mode_t order (plan §11).
 * The retained byte is the index; only the file carries text.
 */
static const char *const storage_effectRunModeTokens[EFFECT_SEQ_RUN_MODE_COUNT] = {
    "fwd", "rev", "pip", "rnd", "sel"
};

/*
 * Step-scale tokens in shared scale-table index order (plan §11.3, S072_ST6
 * D3). Index 4 is the 1/16 default. Step 8's tick table must keep this order;
 * the tokens contain no spaces so hand-edited files survive trimming.
 */
static const char *const storage_effectStepScaleTokens[EFFECT_SEQ_SCALE_COUNT] = {
    "1/64", "1/32t", "1/32", "1/16t", "1/16", "1/8t", "1/16.",
    "1/8", "1/4t", "1/8.", "1/4", "1/2", "1bar", "2bar"
};

_Static_assert(sizeof(storage_effectStepScaleTokens) /
                   sizeof(storage_effectStepScaleTokens[0]) ==
                   EFFECT_SEQ_SCALE_COUNT,
               "one .fx step_scale token per shared scale index");

/* Exact token lookup; output index only on success. */
static uint8_t storage_effectTokenIndex(const char *const *tokens,
                                        uint8_t count,
                                        const char *text,
                                        uint8_t *index_out)
{
    uint8_t i;

    for (i = 0u; i < count; i++) {
        if (storage_streq(tokens[i], text)) {
            *index_out = i;
            return 1u;
        }
    }
    return 0u;
}

/*
 * Bounded append helpers for the lane writer.
 *
 * Inputs: destination, capacity, running length, text or byte. Output: zero
 * when the next byte would not leave room for the terminator, so a caller
 * never emits a truncated line. printf-family code stays out of the link
 * (see storage_formatLiteral()).
 */
static uint8_t storage_effectAppendText(char *dst, uint16_t capacity,
                                        uint16_t *len, const char *text)
{
    while (*text != '\0') {
        if (*len + 1u >= capacity)
            return 0u;
        dst[(*len)++] = *text++;
    }
    return 1u;
}

static uint8_t storage_effectAppendU8(char *dst, uint16_t capacity,
                                      uint16_t *len, uint8_t value)
{
    char digits[4];
    uint8_t count = 0u;

    do {
        digits[count++] = (char)('0' + (value % 10u));
        value = (uint8_t)(value / 10u);
    } while (value != 0u);
    while (count > 0u) {
        if (*len + 1u >= capacity)
            return 0u;
        dst[(*len)++] = digits[--count];
    }
    return 1u;
}

/*
 * Parse one `lane.<key>=0xMMMM,v0,...,v15` value into the staged record.
 *
 * Inputs: target record, resolved lane, and the value text after '='.
 * Output: for every step s, value[lane] = v_s and lock bit `lane` follows
 * mask bit s. Why lane-major: one keyed line per lane keeps each line under
 * the 160-byte reader limit and lets unknown lanes be skipped as whole lines
 * (A36). Strict: 1..4 hex digits after 0x, then exactly sixteen 0..255
 * values; anything else is BAD_VALUE and fails the file.
 */
static storage_status_t storage_effectParseLane(effect_record_t *target,
                                                uint8_t lane,
                                                const char *value)
{
    uint8_t values[EFFECT_SEQ_STEP_COUNT];
    uint16_t mask = 0u;
    uint16_t lane_bit = (uint16_t)(1u << lane);
    uint8_t digits = 0u;
    uint8_t step;
    storage_status_t st;

    if (value[0] != '0' || (value[1] != 'x' && value[1] != 'X'))
        return STORAGE_STATUS_BAD_VALUE;
    value += 2;
    while (*value != ',') {
        int8_t nibble = storage_patternHex(*value);

        if (nibble < 0 || digits >= 4u)
            return STORAGE_STATUS_BAD_VALUE;
        mask = (uint16_t)((mask << 4) | (uint16_t)nibble);
        digits++;
        value++;
    }
    if (digits == 0u)
        return STORAGE_STATUS_BAD_VALUE;
    st = storage_parseCsvU8(value + 1, values, EFFECT_SEQ_STEP_COUNT, 255u);
    if (st != STORAGE_STATUS_OK)
        return st;
    for (step = 0u; step < EFFECT_SEQ_STEP_COUNT; step++) {
        target->steps[step].value[lane] = values[step];
        if ((mask & (uint16_t)(1u << step)) != 0u)
            target->steps[step].lock_mask |= lane_bit;
        else
            target->steps[step].lock_mask &= (uint16_t)~lane_bit;
    }
    return STORAGE_STATUS_OK;
}

void storage_effectStateInit(storage_effect_state_t *state,
                             effect_record_t *target)
{
    /*
     * Reset parser state and the staged record before a `.fx` file.
     *
     * Inputs: caller-owned state and staging record. Output: top section,
     * no fields seen, and an `off` record, so a legacy placeholder or an early
     * failure never leaves bytes from a previous Scene in the stage.
     */
    if (state) {
        memset(state, 0, sizeof(*state));
        state->type = EFFECT_TYPE_OFF;
        state->current_section = STORAGE_SECTION_TOP;
    }
    if (target)
        effects_recordDefaultsForType(target, EFFECT_TYPE_OFF);
}

storage_status_t storage_effectParseLine(storage_effect_state_t *state,
                                         const char *line,
                                         effect_record_t *target)
{
    char key[STORAGE_EFFECT_KEY_MAX];
    const char *value;
    storage_status_t st;
    uint8_t parsed;
    uint8_t index;

    /*
     * Parse one `.fx` line (contract in storageTypes.h).
     *
     * Section headers are matched after trimming. A header before type= in a
     * v2 file is MISSING_REQUIRED because every section key depends on the
     * type. Unknown keys return OK without writing (forward compatibility).
     */
    if (!state || !line || !target)
        return STORAGE_STATUS_BAD_VALUE;
    line = storage_trimLeft(line);
    if (*line == '\0' || *line == '#')
        return STORAGE_STATUS_OK;
    if (*line == '[') {
        strncpy(key, line, sizeof(key) - 1u);
        key[sizeof(key) - 1u] = '\0';
        storage_trimRight(key);
        if (state->version != 2u || !state->seen_type)
            return STORAGE_STATUS_MISSING_REQUIRED;
        if (storage_streq(key, "[params]")) {
            state->current_section = STORAGE_SECTION_PARAMS;
        } else if (storage_streq(key, "[morph]")) {
            state->current_section = STORAGE_SECTION_MORPH;
            state->seen_morph_section = 1u;
        } else if (storage_streq(key, "[sequence]")) {
            state->current_section = STORAGE_SECTION_SEQUENCE;
        } else {
            state->current_section = STORAGE_SECTION_UNKNOWN;
        }
        return STORAGE_STATUS_OK;
    }
    st = storage_splitKeyValue(line, key, sizeof(key), &value);
    if (st != STORAGE_STATUS_OK)
        return st;

    if (state->current_section == STORAGE_SECTION_TOP) {
        if (storage_streq(key, "format")) {
            if (!storage_streq(value, "helicase.effect"))
                return STORAGE_STATUS_INVALID_FORMAT;
            state->seen_format = 1u;
        } else if (storage_streq(key, "version")) {
            st = storage_parseU8(value, &parsed);
            if (st != STORAGE_STATUS_OK)
                return st;
            if (parsed != 1u && parsed != 2u)
                return STORAGE_STATUS_UNSUPPORTED_VERSION;
            state->version = parsed;
        } else if (storage_streq(key, "placeholder")) {
            st = storage_parseU8(value, &parsed);
            if (st != STORAGE_STATUS_OK)
                return st;
            if (parsed != 1u)
                return STORAGE_STATUS_BAD_VALUE;
            state->seen_placeholder = 1u;
        } else if (storage_streq(key, "type")) {
            effect_type_id_t type;

            /* An unknown token fails the load (plan §14.1). */
            if (state->seen_type || strlen(value) != 3u ||
                !effects_typeFromToken(value, &type)) {
                return STORAGE_STATUS_BAD_TYPE;
            }
            state->type = type;
            state->seen_type = 1u;
            effects_recordDefaultsForType(target, type);
        }
        return STORAGE_STATUS_OK;
    }

    if (state->current_section == STORAGE_SECTION_PARAMS ||
        state->current_section == STORAGE_SECTION_MORPH) {
        const effect_param_descriptor_t *descriptor =
            effects_descriptorByKey(state->type, key, &index);

        if (!descriptor)
            return STORAGE_STATUS_OK;
        st = storage_parseU8(value, &parsed);
        if (st != STORAGE_STATUS_OK)
            return st;
        if (parsed > descriptor->max_value)
            parsed = descriptor->max_value;
        if (state->current_section == STORAGE_SECTION_PARAMS) {
            target->normal[index] = parsed;
        } else if ((descriptor->base.flags &
                    INSTRUMENT_PARAM_FLAG_MORPHABLE) != 0u) {
            /* Non-Morphable rows keep one endpoint, as Instrument files do. */
            target->morph[index] = parsed;
        }
        return STORAGE_STATUS_OK;
    }

    if (state->current_section == STORAGE_SECTION_SEQUENCE) {
        if (storage_streq(key, "run_mode")) {
            if (!storage_effectTokenIndex(storage_effectRunModeTokens,
                                          EFFECT_SEQ_RUN_MODE_COUNT,
                                          value, &parsed)) {
                return STORAGE_STATUS_BAD_VALUE;
            }
            target->seq_run_mode = parsed;
        } else if (storage_streq(key, "length")) {
            st = storage_parseU8(value, &parsed);
            if (st != STORAGE_STATUS_OK)
                return st;
            if (parsed < EFFECT_SEQ_LENGTH_MIN ||
                parsed > EFFECT_SEQ_LENGTH_MAX) {
                return STORAGE_STATUS_BAD_VALUE;
            }
            target->seq_length = parsed;
        } else if (storage_streq(key, "step_scale")) {
            if (!storage_effectTokenIndex(storage_effectStepScaleTokens,
                                          EFFECT_SEQ_SCALE_COUNT,
                                          value, &parsed)) {
                return STORAGE_STATUS_BAD_VALUE;
            }
            target->seq_step_scale = parsed;
        } else if (strncmp(key, STORAGE_EFFECT_LANE_PREFIX,
                           STORAGE_EFFECT_LANE_PREFIX_LEN) == 0) {
            uint8_t lane;

            if (!effects_laneByFileKey(
                    state->type, key + STORAGE_EFFECT_LANE_PREFIX_LEN,
                    &lane)) {
                return STORAGE_STATUS_OK;
            }
            return storage_effectParseLane(target, lane, value);
        }
    }
    return STORAGE_STATUS_OK;
}

storage_status_t storage_effectFinalize(const storage_effect_state_t *state,
                                        effect_record_t *target)
{
    const effect_registry_entry_t *entry;
    uint8_t index;

    /*
     * Accept or reject one completed `.fx` file (contract in storageTypes.h).
     *
     * v1 placeholders resolve to `off` whatever else they contained (A37).
     * A v2 file with no [morph] section mirrors Morphable [params] values into
     * the Morph image, matching the Instrument fallback, so Effect Morph is a
     * no-op until the user edits an endpoint.
     */
    if (!state || !target || !state->seen_format || state->version == 0u)
        return STORAGE_STATUS_MISSING_REQUIRED;
    if (state->version == 1u) {
        if (!state->seen_placeholder)
            return STORAGE_STATUS_MISSING_REQUIRED;
        effects_recordDefaultsForType(target, EFFECT_TYPE_OFF);
        return STORAGE_STATUS_OK;
    }
    if (!state->seen_type)
        return STORAGE_STATUS_MISSING_REQUIRED;
    if (!state->seen_morph_section) {
        entry = effects_registryEntry(state->type);
        for (index = 0u; entry && index < entry->descriptor_count; index++) {
            if ((entry->descriptors[index].base.flags &
                 INSTRUMENT_PARAM_FLAG_MORPHABLE) != 0u)
                target->morph[index] = target->normal[index];
        }
    }
    return STORAGE_STATUS_OK;
}

/* Count, or locate the ordinal-th, Morphable descriptor of one type. */
static uint8_t storage_effectMorphRow(const effect_registry_entry_t *entry,
                                      uint8_t ordinal,
                                      uint8_t *index_out)
{
    uint8_t index;
    uint8_t seen = 0u;

    for (index = 0u; index < entry->descriptor_count; index++) {
        if ((entry->descriptors[index].base.flags &
             INSTRUMENT_PARAM_FLAG_MORPHABLE) == 0u)
            continue;
        if (index_out && seen == ordinal) {
            *index_out = index;
            return 1u;
        }
        seen++;
    }
    return index_out ? 0u : seen;
}

/* Locate the ordinal-th named lane of one type (lanes with a file key). */
static uint8_t storage_effectNamedLane(effect_type_id_t type,
                                       uint8_t ordinal,
                                       uint8_t *lane_out)
{
    uint8_t lane;
    uint8_t seen = 0u;

    for (lane = 0u; lane < EFFECT_SEQ_LANE_COUNT; lane++) {
        if (!effects_laneFileKey(type, lane))
            continue;
        if (seen == ordinal) {
            *lane_out = lane;
            return 1u;
        }
        seen++;
    }
    return 0u;
}

/*
 * Format one `lane.<key>=0xMMMM,v0,...,v15\n` line.
 *
 * Inputs: record, lane, and its file key. Output: the line length, or 0 on
 * capacity exhaustion. The mask is always four uppercase hex digits so saved
 * files diff cleanly; the parser accepts 1..4.
 */
static uint8_t storage_formatEffectLaneLine(char *dst, uint16_t capacity,
                                            const effect_record_t *record,
                                            uint8_t lane, const char *key)
{
    static const char hex[] = "0123456789ABCDEF";
    uint16_t len = 0u;
    uint16_t mask = 0u;
    uint8_t step;
    char mask_text[5];

    for (step = 0u; step < EFFECT_SEQ_STEP_COUNT; step++) {
        if ((record->steps[step].lock_mask & (uint16_t)(1u << lane)) != 0u)
            mask |= (uint16_t)(1u << step);
    }
    mask_text[0] = hex[(mask >> 12) & 0xfu];
    mask_text[1] = hex[(mask >> 8) & 0xfu];
    mask_text[2] = hex[(mask >> 4) & 0xfu];
    mask_text[3] = hex[mask & 0xfu];
    mask_text[4] = '\0';
    if (!storage_effectAppendText(dst, capacity, &len,
                                  STORAGE_EFFECT_LANE_PREFIX) ||
        !storage_effectAppendText(dst, capacity, &len, key) ||
        !storage_effectAppendText(dst, capacity, &len, "=0x") ||
        !storage_effectAppendText(dst, capacity, &len, mask_text)) {
        return 0u;
    }
    for (step = 0u; step < EFFECT_SEQ_STEP_COUNT; step++) {
        if (!storage_effectAppendText(dst, capacity, &len, ",") ||
            !storage_effectAppendU8(dst, capacity, &len,
                                    record->steps[step].value[lane])) {
            return 0u;
        }
    }
    if (!storage_effectAppendText(dst, capacity, &len, "\n"))
        return 0u;
    dst[len] = '\0';
    return (uint8_t)len;
}

uint8_t storage_formatEffectLine(char *dst, uint16_t capacity,
                                 const effect_record_t *record,
                                 uint16_t line_index)
{
    const effect_registry_entry_t *entry;
    effect_type_id_t type;
    uint8_t param_count;
    uint8_t morph_count;
    uint8_t index;

    /*
     * Stream one `.fx` v2 line (contract in storageTypes.h).
     *
     * Line map: 0 format, 1 version, 2 type, 3 blank, 4 [params], then one
     * line per descriptor; blank, [morph], one line per Morphable descriptor;
     * blank, [sequence], run_mode, length, step_scale, then one line per named
     * lane; 0 terminates. Counts come from the registry on every call, so the
     * writer can never drift from the parser's key set.
     */
    if (!dst || capacity == 0u || !record)
        return 0u;
    type = effects_registryEntry(record->type) ? record->type
                                               : EFFECT_TYPE_OFF;
    entry = effects_registryEntry(type);
    param_count = entry->descriptor_count;
    morph_count = storage_effectMorphRow(entry, 0u, NULL);

    if (line_index == 0u)
        return storage_formatLiteral(dst, capacity, "format=helicase.effect\n");
    if (line_index == 1u)
        return storage_formatLiteral(dst, capacity, "version=2\n");
    if (line_index == 2u)
        return storage_formatAssignmentText(dst, capacity, "type",
                                            entry->token3);
    if (line_index == 3u)
        return storage_formatLiteral(dst, capacity, "\n");
    if (line_index == 4u)
        return storage_formatLiteral(dst, capacity, "[params]\n");
    line_index = (uint16_t)(line_index - 5u);
    if (line_index < param_count) {
        return storage_formatAssignmentU16(
            dst, capacity, entry->descriptors[line_index].base.file_key,
            record->normal[line_index]);
    }
    line_index = (uint16_t)(line_index - param_count);
    if (line_index == 0u)
        return storage_formatLiteral(dst, capacity, "\n");
    if (line_index == 1u)
        return storage_formatLiteral(dst, capacity, "[morph]\n");
    line_index = (uint16_t)(line_index - 2u);
    if (line_index < morph_count) {
        if (!storage_effectMorphRow(entry, (uint8_t)line_index, &index))
            return 0u;
        return storage_formatAssignmentU16(
            dst, capacity, entry->descriptors[index].base.file_key,
            record->morph[index]);
    }
    line_index = (uint16_t)(line_index - morph_count);
    if (line_index == 0u)
        return storage_formatLiteral(dst, capacity, "\n");
    if (line_index == 1u)
        return storage_formatLiteral(dst, capacity, "[sequence]\n");
    if (line_index == 2u)
        return storage_formatAssignmentText(
            dst, capacity, "run_mode",
            storage_effectRunModeTokens[
                record->seq_run_mode < EFFECT_SEQ_RUN_MODE_COUNT
                    ? record->seq_run_mode : EFFECT_SEQ_RUN_FWD]);
    if (line_index == 3u)
        return storage_formatAssignmentU16(dst, capacity, "length",
                                           record->seq_length);
    if (line_index == 4u)
        return storage_formatAssignmentText(
            dst, capacity, "step_scale",
            storage_effectStepScaleTokens[
                record->seq_step_scale < EFFECT_SEQ_SCALE_COUNT
                    ? record->seq_step_scale : EFFECT_SEQ_SCALE_DEFAULT]);
    line_index = (uint16_t)(line_index - 5u);
    if (line_index >= EFFECT_SEQ_LANE_COUNT ||
        !storage_effectNamedLane(type, (uint8_t)line_index, &index)) {
        return 0u;
    }
    return storage_formatEffectLaneLine(dst, capacity, record, index,
                                        effects_laneFileKey(type, index));
}

void storage_makeSavedEffectDisplayFilename(char *dst,
                                            uint8_t capacity,
                                            const char *stem)
{
    uint8_t len = 0u;

    /*
     * Build `<stem>.fx` through the unchanged Instrument stem rules (G5).
     *
     * Inputs: destination (>= 13 bytes) and the Effect HCNAMES name cells.
     * Output: exactly what Instrument naming produces for the same stem, with
     * the extension swapped. Reusing the Instrument helper verbatim, rather
     * than re-implementing its trimming and `none` fallback, guarantees that
     * `' .fx'` behaves as `' .drm'` does (S072_ST6 D4) without modifying code
     * outside the Effects framework.
     */
    storage_makeSavedInstrumentDisplayFilename(dst, capacity, stem,
                                               STORAGE_INSTRUMENT_DRM,
                                               0u, 0u);
    if (!dst || capacity == 0u)
        return;
    while (dst[len] != '\0')
        len++;
    if (len >= 4u && dst[len - 4u] == '.' && dst[len - 3u] == 'd' &&
        dst[len - 2u] == 'r' && dst[len - 1u] == 'm') {
        dst[len - 3u] = 'f';
        dst[len - 2u] = 'x';
        dst[len - 1u] = '\0';
    }
}
```

**Line length.** The worst-case lane line (`lane.filter_drive=0xFFFF` plus
sixteen `,255`) is 89 bytes including the newline. That is under
`FS_TEXT_LINE_MAX` (160) for both `op_line_buf` and `op_write_line_buf`.

---

## 5. `Autosave.h`

### 5.1 Lines 36–48 — format version (modify)

```c
/*
 * Session 072 step 6 format version for the scalar AutoSave record.
 *
 * What: identifies the 161-row HCNAMES identity image (Pattern rows 129..144
 * plus Effect rows 145..160), the Effect source field at Effect-relative
 * 430..431, and a live Effect reader. Why: version 2 records carry no Effect
 * provenance and a 145-row identity image, so they must not be interpreted
 * by the Step 6 reader; they are rejected and a fresh baseline is created
 * (A38). Inputs/outputs: compile-time tag consumed by the writer and boot
 * validator; the scalar record byte geometry remains unchanged. Affiliates:
 * autosave_streamValidationUpdate(), filesystem_autosaveBootReaderBlocking(),
 * and HCNAMES Pattern/Effect provenance.
 */
#define AUTOSAVE_HEADER_FORMAT_VERSION        3u
```

### 5.2 Lines 91–133 — identity rows (modify)

- **Line 91–102 comment:** rows 0..144 → 0..160. Add the sentence "Effect
  rows 145..160 map to each Scene's Effect region name (3..10) and source
  (430..431)."
- **Line 103:** `#define AUTOSAVE_HCNAMES_ROW_COUNT 161u`.
- **After line 127** (end of `AUTOSAVE_HCNAMES_PATTERN_BASE`), add:

```c
/*
 * HCNAMES Effect row base (Session 072 step 6).
 *
 * What: rows 145..160 are one Effect identity per resident Scene, appended
 * after the Pattern block so every earlier coordinate is unchanged. Why:
 * unlike Pattern, the Effect payload lives inside this record (the Scene's
 * 512-byte Effect region), so its row participates in source marking, the
 * fully-captured test, and Case 1/2/3 boot evaluation exactly like a Kit row.
 * Affiliates: autosave_markSourceDirty(), autosave_objectFullyCaptured(),
 * filesystem_residentEffectRow(), and the boot reader's ninth row.
 */
#define AUTOSAVE_HCNAMES_EFFECT_BASE \
    (AUTOSAVE_HCNAMES_PATTERN_BASE + AUTOSAVE_SCENE_COUNT)
```

### 5.3 After line 193 (`AUTOSAVE_EFFECT_PARAM_COUNT`) — add

```c
/*
 * Effect provenance field (Session 072 step 6, S072_ST6 D2).
 *
 * What: the Effect row's 2-byte little-endian HCNAMES source, stored in the
 * first reserved bytes after the 419 live cells (Effect-relative 430..431).
 * Why: every other identity sub-object carries its source beside its payload
 * so the boot reader can cross-check Case 1 and winner regeneration can
 * rebuild the row; absorbing reserved bytes keeps the region, mask, and record
 * geometry unchanged, as Phase C did. Affiliates: autosave_getSourceByte(),
 * autosave_markSourceDirty(), filesystem_regenClassifyPayloadByte().
 */
#define AUTOSAVE_EFFECT_SOURCE_OFFSET \
    (AUTOSAVE_EFFECT_PARAMETERS_OFFSET + AUTOSAVE_EFFECT_PARAM_COUNT)

_Static_assert(AUTOSAVE_EFFECT_SOURCE_OFFSET == 430u,
               "Effect source follows the 419 live Effect cells");
_Static_assert(AUTOSAVE_EFFECT_SOURCE_OFFSET + AUTOSAVE_SOURCE_BYTES <=
                   AUTOSAVE_EFFECT_SECTION_BYTES,
               "Effect source must remain inside the 512-byte Effect region");
```

### 5.4 Lines 344–346 — row-mapping assert (modify)

```c
_Static_assert(AUTOSAVE_HCNAMES_PATTERN_BASE + AUTOSAVE_SCENE_COUNT ==
                   AUTOSAVE_HCNAMES_EFFECT_BASE &&
               AUTOSAVE_HCNAMES_EFFECT_BASE + AUTOSAVE_SCENE_COUNT ==
                   AUTOSAVE_HCNAMES_ROW_COUNT,
               "autosave name mapping must consume all HCNAMES rows");
```

### 5.5 After line 651 (`autosave_applyScenePayload` prototype) — add

```c
/*
 * Apply a validated winner record's Effect region to one resident Scene
 * (Session 072 step 6). Input: the 512-byte Effect region. Output: the
 * retained Effect record replaced through SceneData's whole-commit pair; an
 * absent or unknown type token restores `off` defaults (plan §15).
 */
void autosave_applyEffectPayload(uint8_t scene_index,
                                 const uint8_t *effect_section);
```

---

## 6. `Autosave.c`

### 6.1 After line 562 (end of the Kit-name branch in `autosave_initialRecordByte()`) — add

```c
        /*
         * Baseline Effect name (Session 072 step 6).
         *
         * Inputs: HCNAMES Effect row for this Scene. Output: the eight name
         * cells at Effect-relative 3..10. Like Scene/Kit/Instrument names these
         * bytes are baseline-only: the live getter never projects them and
         * the boot reader takes names from `.hcnames`; winner regeneration is
         * their only reader.
         */
        {
            uint32_t effect_name_offset = scene_offset +
                AUTOSAVE_EFFECT_OFFSET + AUTOSAVE_EFFECT_NAME_OFFSET;

            if (record_offset >= effect_name_offset &&
                record_offset < effect_name_offset + AUTOSAVE_NAME_BYTES) {
                return autosave_nameByte(
                    resident_names[AUTOSAVE_HCNAMES_EFFECT_BASE + scene],
                    (uint8_t)(record_offset - effect_name_offset));
            }
        }
```

### 6.2 Lines 1081–1087 and before 1100 — Effect bytes in `autosave_getLivePayloadByte()`

- **Lines 1081–1087, comment (modify):** replace "The name bytes 131..138
  remain absent until Step 6" with "The name bytes 131..138 are
  baseline-only, like every identity name; the source at Scene-relative
  558..559 is projected below."
- **Before line 1100, add:**

```c
    /*
     * Effect row source (Session 072 step 6): Effect-relative 430..431.
     *
     * Checked before the parameter interval, which spans the reserved tail
     * and would otherwise report these bytes as nonexistent.
     */
    if (relative >= AUTOSAVE_EFFECT_OFFSET + AUTOSAVE_EFFECT_SOURCE_OFFSET &&
        relative < AUTOSAVE_EFFECT_OFFSET + AUTOSAVE_EFFECT_SOURCE_OFFSET +
                   AUTOSAVE_SOURCE_BYTES) {
        return autosave_getSourceByte(
            (uint16_t)(AUTOSAVE_HCNAMES_EFFECT_BASE + scene_index),
            (uint8_t)(relative - AUTOSAVE_EFFECT_OFFSET -
                      AUTOSAVE_EFFECT_SOURCE_OFFSET),
            value);
    }
```

### 6.3 After line 1375 (end of `autosave_applyScenePayload()`) — add

```c
/*
 * Write one ordered Effect wire cell into a record (inverse of
 * autosave_getEffectParameter()).
 *
 * Inputs: mutable record, Effect-relative parameter index 0..418, and value.
 * Output: the matching sequence setting, endpoint, lock-mask byte, or lane
 * value. Explicit projection keeps C layout out of the wire format.
 */
static void autosave_setEffectParameter(effect_record_t *record,
                                        uint16_t parameter_index,
                                        uint8_t value)
{
    if (parameter_index == AUTOSAVE_EFFECT_PARAM_SEQ_RUN_MODE) {
        record->seq_run_mode = value;
    } else if (parameter_index == AUTOSAVE_EFFECT_PARAM_SEQ_LENGTH) {
        record->seq_length = value;
    } else if (parameter_index == AUTOSAVE_EFFECT_PARAM_SEQ_STEP_SCALE) {
        record->seq_step_scale = value;
    } else if (parameter_index < AUTOSAVE_EFFECT_PARAM_MORPH_BASE) {
        record->normal[parameter_index -
                       AUTOSAVE_EFFECT_PARAM_NORMAL_BASE] = value;
    } else if (parameter_index < AUTOSAVE_EFFECT_PARAM_STEPS_BASE) {
        record->morph[parameter_index -
                      AUTOSAVE_EFFECT_PARAM_MORPH_BASE] = value;
    } else if (parameter_index < AUTOSAVE_EFFECT_PARAM_COUNT) {
        uint16_t step_relative = (uint16_t)(
            parameter_index - AUTOSAVE_EFFECT_PARAM_STEPS_BASE);
        effect_seq_step_t *step =
            &record->steps[step_relative / AUTOSAVE_EFFECT_STEP_BYTES];
        uint8_t field = (uint8_t)(step_relative % AUTOSAVE_EFFECT_STEP_BYTES);

        if (field == AUTOSAVE_EFFECT_STEP_MASK_LO_OFFSET)
            step->lock_mask = (uint16_t)((step->lock_mask & 0xff00u) | value);
        else if (field == AUTOSAVE_EFFECT_STEP_MASK_HI_OFFSET)
            step->lock_mask = (uint16_t)((step->lock_mask & 0x00ffu) |
                                         ((uint16_t)value << 8));
        else
            step->value[field - AUTOSAVE_EFFECT_STEP_VALUES_OFFSET] = value;
    }
}

/*
 * Apply a validated winner record's Effect region (Session 072 step 6).
 *
 * What: the inverse of the Effect portion of autosave_getLivePayloadByte().
 * Resolves the 3-byte token through the registry; an absent (zero baseline)
 * or unknown token restores `off` defaults instead of failing the Scene
 * (plan §15). Otherwise the 419 live cells are copied. Inputs: scene_index
 * and the 512-byte Effect region (scene_section + AUTOSAVE_EFFECT_OFFSET).
 * Output: the resident record, rewritten in place through SceneData's
 * whole-commit pair, which normalizes sequence bytes and marks AutoSave (a
 * no-op while boot tracking is disabled). Why in place: no 420-byte stack
 * copy on the boot path, which already holds a 1,920-byte section buffer.
 * Affiliates: autosave_getEffectParameter(), effects_typeFromToken(),
 * effects_recordDefaultsForType(), the boot reader's Case 1 Effect row.
 */
void autosave_applyEffectPayload(uint8_t scene_index,
                                 const uint8_t *effect_section)
{
    effect_record_t *record;
    effect_type_id_t type;
    uint16_t index;

    if (!effect_section || scene_index >= AUTOSAVE_SCENE_COUNT)
        return;
    record = scene_effectRecordForWholeCommit(scene_index);
    if (!record)
        return;
    if (!effects_typeFromToken(
            (const char *)(effect_section + AUTOSAVE_EFFECT_TYPE_OFFSET),
            &type)) {
        effects_recordDefaultsForType(record, EFFECT_TYPE_OFF);
    } else {
        record->type = type;
        for (index = 0u; index < AUTOSAVE_EFFECT_PARAM_COUNT; index++) {
            autosave_setEffectParameter(
                record, index,
                effect_section[AUTOSAVE_EFFECT_PARAMETERS_OFFSET + index]);
        }
    }
    scene_finishEffectWholeCommit(scene_index);
}
```

### 6.4 Lines 1745–1756 and 1783 — `autosave_markSourceDirty()` (modify)

- **Comment:** "Input: fixed row 0..144" → "Input: fixed row 0..160". Replace
  "a present Scene, Kit, or Instrument" with "a present Scene, Kit, Instrument,
  or Effect".
- **Before the Pattern branch at line 1783, insert:**

```c
    } else if (hcnames_row >= AUTOSAVE_HCNAMES_EFFECT_BASE &&
               hcnames_row < AUTOSAVE_HCNAMES_ROW_COUNT) {
        /* Effect provenance lives in the Scene's Effect region (step 6). */
        uint8_t scene_index = (uint8_t)(
            hcnames_row - AUTOSAVE_HCNAMES_EFFECT_BASE);

        if (!autosave_scenePayloadBase(scene_index, &payload_base))
            return;
        payload_base = (uint16_t)(
            payload_base + AUTOSAVE_EFFECT_OFFSET +
            AUTOSAVE_EFFECT_SOURCE_OFFSET);
```

The existing `} else if (hcnames_row >= AUTOSAVE_HCNAMES_PATTERN_BASE) {` then
follows unchanged. Pattern rows can no longer reach it with an Effect
coordinate, because the Effect branch is tested first.

### 6.5 Lines 1996–2021 — `autosave_markEffectDirty()` (modify)

- **Comment:** replace "Effect name ownership joins in Step 6." with "Step 6
  adds the Effect source bytes (row 145 + Scene), mirroring
  autosave_markKitDirty()'s Kit source. Names stay baseline-only."
- **After the token-byte loop's closing `}`** (inside the
  `autosave_scenePayloadBase` block), add:

```c
        /* A whole Effect commit re-captures its provenance (step 6). */
        autosave_markSourceDirty(
            (uint16_t)(AUTOSAVE_HCNAMES_EFFECT_BASE + scene_index));
```

### 6.6 Line 2204 — `autosave_objectFullyCaptured()` (modify)

Replace the Pattern branch:

```c
    } else if (hcnames_row >= AUTOSAVE_HCNAMES_PATTERN_BASE) {
        /* Pattern provenance has no scalar-record payload interval. */
        return 1u;
```

with:

```c
    } else if (hcnames_row >= AUTOSAVE_HCNAMES_EFFECT_BASE) {
        /*
         * Effect row (step 6): the Scene's whole 512-byte Effect region,
         * including reserved cells, so the witness clears only once token,
         * live cells, and source are all clean.
         */
        if (hcnames_row >= AUTOSAVE_HCNAMES_ROW_COUNT)
            return 0u;
        payload_start = AUTOSAVE_BANK_SECTION_BYTES +
            ((uint32_t)(hcnames_row - AUTOSAVE_HCNAMES_EFFECT_BASE) *
             AUTOSAVE_SCENE_SECTION_BYTES) + AUTOSAVE_EFFECT_OFFSET;
        payload_end = payload_start + AUTOSAVE_EFFECT_SECTION_BYTES;
    } else if (hcnames_row >= AUTOSAVE_HCNAMES_PATTERN_BASE) {
        /* Pattern provenance has no scalar-record payload interval. */
        return 1u;
```

Also add "Effect" to the function comment's object list.

---

## 7. `filesystem.c`

### 7.1 Lines 129–151 — row layout (modify)

- **Comment 129–142:** replace "and one Pattern row for each resident Scene"
  with "one Pattern row, and one Effect row for each resident Scene (Session
  072 step 6)". Replace "a complete v4 register has 146 lines while
  FS_RESIDENT_NAMES_ROW_COUNT stays 145" with "a complete register has 162
  lines while FS_RESIDENT_NAMES_ROW_COUNT stays 161". Replace "AutoSave's
  independent wire image is also 145 rows in S064" with "AutoSave's identity
  image is also 161 rows (format version 3)".
- **Replace lines 150–151** with:

```c
/*
 * Effect rows (Session 072 step 6; plan §14.2): one per resident Scene,
 * appended after the Pattern block so every earlier coordinate is unchanged.
 * Rows use the non-Instrument form name<TAB>source[<TAB>R]. The name is the
 * Scene's `.fx` filename stem (independent of the Scene directory name, G6);
 * the source is `-` for Scene/Bank children and `?` when unknown. Unlike
 * Pattern rows, the Effect payload lives in the scalar AutoSave record, so
 * these rows take part in the refresh witness and Case 1/2/3 evaluation.
 */
#define FS_RESIDENT_NAMES_EFFECT_BASE \
    (FS_RESIDENT_NAMES_PATTERN_BASE + STORAGE_BANK_SCENE_MAX_SLOTS)
#define FS_RESIDENT_NAMES_ROW_COUNT \
    (FS_RESIDENT_NAMES_EFFECT_BASE + STORAGE_BANK_SCENE_MAX_SLOTS)
```

### 7.2 Lines 872–885, 962–983 — Scene stage (modify)

- **Comment 872–880:** replace "Selected Scene settings/Kit validate
  atomically" with "Scene settings, its embedded Kit, and its Effect record
  validate atomically". Replace "and the later Effect payload design" with
  "and Effect phases 56..60, which run before the Kit (S072_ST6 D1)".
- **Struct:**

```c
typedef struct {
    scene_settings_t settings;
    kit_t kit;
    /* Staged `.fx` record (Session 072 step 6); committed with settings/Kit. */
    effect_record_t effect;
} filesystem_scene_stage_t;
```

- **Comment 962–975:** replace "reserves 384 bytes for a future non-Pattern
  Effect stage" with "reserves the 420-byte staged Effect record, which is
  co-resident with the Scene settings and Kit during one Scene Load".
- **Line 983:** `#define FS_STAGE_EFFECT_RESERVE_BYTES 420u`.

### 7.3 Lines 1015–1080 — register comments and asserts (modify)

- **Line 1016 comment:** "This is the user-approved 258-byte cache" → "322-byte
  (161 x uint16_t) cache, grown by 32 B for Effect rows (plan §16 item 3)".
- **Line 1032 comment:** "145 rows x 9 bytes = 1,305 bytes" → "161 rows x 9
  bytes = 1,449 bytes (Effect rows added in Session 072 step 6)".
- **Lines 1076–1079:**

```c
_Static_assert(sizeof(fs_resident_source) == 322u,
               "HCNAMES provenance register must remain 161 x uint16_t");
_Static_assert(sizeof(hcnames_name_mirror) == 1449u,
               "Option 1C: HCNAMES mirror must remain exactly 161 x 9 bytes");
/* One row geometry is shared by the register and AutoSave's identity image. */
_Static_assert(FS_RESIDENT_NAMES_ROW_COUNT == AUTOSAVE_HCNAMES_ROW_COUNT &&
               FS_RESIDENT_NAMES_EFFECT_BASE == AUTOSAVE_HCNAMES_EFFECT_BASE &&
               FS_RESIDENT_NAMES_PATTERN_BASE == AUTOSAVE_HCNAMES_PATTERN_BASE,
               "filesystem and AutoSave must agree on HCNAMES rows");
```

- **After the stage asserts (line ~1094), add:**

```c
/* Plan §14.3: the Effect stage is co-resident with the Scene/Kit stage. */
_Static_assert(sizeof(effect_record_t) <= FS_STAGE_EFFECT_RESERVE_BYTES,
               "staged Effect record exceeds its reserve");
_Static_assert(sizeof(filesystem_scene_stage_t) <= FS_STAGE_CACHE_BYTES,
               "Scene + Kit + Effect stage must fit the 2,048-byte union");
```

### 7.4 Lines 1238–1248 — Effect scratch (modify/add)

- **Line 1242:** keep `op_scene_effect_open_name`, but add a comment:
  "short alias of the discovered `.fx` for Load; reused as the `<name>.fx`
  save component (at most 12 bytes plus NUL) by Scene Save".
- **After line 1247 (`op_pattern_source`), add:**

```c
/*
 * Effect identity captured while a Scene operation is in flight (Session 072
 * step 6). Load copies the discovered `.fx` stem (blank when the Scene has
 * none, G6); Save copies the stem it wrote. The HCNAMES Scene/Bank overlay
 * publishes it to the Effect row after the payload commits. The source is
 * always inherit in this step, so no source byte is retained (+9 B, D6).
 */
static char op_effect_display_name[STORAGE_KIT_DISPLAY_NAME_LEN + 1u];
```

- **Line 1248:** `op_effect_state` is unchanged in declaration; its type grows
  to 7 B.

### 7.5 Prototypes (modify/add)

- **After line 1478**, add:

```c
static uint16_t filesystem_residentEffectRow(uint8_t scene_index);
static uint8_t filesystem_residentRowIsPattern(uint16_t row);
static uint8_t filesystem_residentRowIsEffect(uint16_t row);
static uint8_t filesystem_bootReaderNarrowLoadEffect(
    uint8_t scene_index, uint16_t source_slot, uint16_t resolved_row);
```

- **Line 1589:** `filesystem_nextEffectPlaceholderLine` →
  `filesystem_nextEffectLine`. Keep the same signature.

### 7.6 After line 5636 (end of `filesystem_residentPatternRow()`) — add

```c
/*
 * Convert one resident Scene coordinate into its appended Effect row
 * (Session 072 step 6). Output: row 145..160, or the row-count sentinel.
 */
static uint16_t filesystem_residentEffectRow(uint8_t scene_index)
{
    if (scene_index >= STORAGE_BANK_SCENE_MAX_SLOTS)
        return FS_RESIDENT_NAMES_ROW_COUNT;
    return (uint16_t)(FS_RESIDENT_NAMES_EFFECT_BASE + scene_index);
}

/*
 * Row-class predicates for the two appended per-Scene blocks.
 *
 * Why: before Step 6, "row >= PATTERN_BASE" meant "Pattern row" because the
 * Pattern block ended the register. Effect rows now follow it, so every
 * Pattern-only rule (the `@` hidden-file source, terminal resolution, and
 * exclusion from scalar AutoSave refresh sweeps) must use a bounded test.
 */
static uint8_t filesystem_residentRowIsPattern(uint16_t row)
{
    return (uint8_t)(row >= FS_RESIDENT_NAMES_PATTERN_BASE &&
                     row < FS_RESIDENT_NAMES_EFFECT_BASE);
}

static uint8_t filesystem_residentRowIsEffect(uint16_t row)
{
    return (uint8_t)(row >= FS_RESIDENT_NAMES_EFFECT_BASE &&
                     row < FS_RESIDENT_NAMES_ROW_COUNT);
}
```

### 7.7 Row-class sites (modify)

| Line | Before | After | Comment |
|---|---|---|---|
| 5669–5670 | `source == FS_RESIDENT_SOURCE_PATTERN_AUTOSAVE && row >= FS_RESIDENT_NAMES_PATTERN_BASE` | `source == FS_RESIDENT_SOURCE_PATTERN_AUTOSAVE && filesystem_residentRowIsPattern(row)` | "`@` on a Pattern row is the hidden AutoSave file; Effect rows have no `@` source in Step 6." |
| 5727 | `if (row >= FS_RESIDENT_NAMES_PATTERN_BASE &&` | `if (filesystem_residentRowIsPattern(row) &&` | — |
| 5741 | `if (row >= FS_RESIDENT_NAMES_PATTERN_BASE) {` | the block below | — |
| 5813 | `value = (row >= FS_RESIDENT_NAMES_PATTERN_BASE)` | `value = filesystem_residentRowIsPattern(row)` | "`@` on an Effect row decodes as INSTRUMENT_DIRECT, which residentSourceValid() then rejects: malformed register text." |
| 5940–5942 | comment "Pattern rows have the ordinary source/R suffix but no type." | "Pattern and Effect rows (129..160) have the ordinary source/R suffix but no type." | Logic unchanged (the `>= PATTERN_BASE` test covers both blocks correctly here). |

The block that replaces line 5741:

```c
        if (filesystem_residentRowIsEffect(row)) {
            /* Effect rows inherit through their resident Scene (step 6). */
            row = (uint16_t)(1u + (row - FS_RESIDENT_NAMES_EFFECT_BASE));
        } else if (row >= FS_RESIDENT_NAMES_PATTERN_BASE) {
```

### 7.8 Refresh and publication helpers (modify)

**6097–6114, `filesystem_setResidentSceneRefreshed()`:**

- Comment: "Scene, Kit, Pattern, and six Instrument rows" → "Scene, Kit,
  Pattern, Effect, and six Instrument rows".
- After line 6110, add:

```c
    filesystem_setResidentRefreshed(filesystem_residentEffectRow(scene_index));
```

**After 6302, `filesystem_cacheCurrentResidentSceneChildNames()`** (after the
Pattern block inside the loop), add:

```c
        /*
         * Effect row (Session 072 step 6): the `.fx` stem this Scene action
         * loaded or saved, inheriting the Scene/Bank source like the Kit.
         */
        row = filesystem_residentEffectRow(scene_index);
        if (row < FS_RESIDENT_NAMES_ROW_COUNT) {
            filesystem_cacheResidentName(row, op_effect_display_name);
            (void)filesystem_setResidentSource(row,
                                               FS_RESIDENT_SOURCE_INHERIT);
        }
```

Its function comment's row list "rows 17..32, 33..128, and 129..144" becomes
"…, 129..144, and 145..160".

**After 6395, `filesystem_cacheCurrentBankSceneNameBlock()`** (after the
Pattern block), add:

```c
    {
        /* Effect row joins the Bank child's inherited block (step 6). */
        uint16_t fx_row = filesystem_residentEffectRow(scene_index);
        if (fx_row < FS_RESIDENT_NAMES_ROW_COUNT) {
            filesystem_cacheResidentName(fx_row, op_effect_display_name);
            (void)filesystem_setResidentSource(fx_row,
                                               FS_RESIDENT_SOURCE_INHERIT);
        }
    }
```

Its comment "its six Instrument rows, and its Pattern row" becomes "…, its
Pattern row, and its Effect row".

### 7.9 Lines 7665 and 7688 — AutoSave refresh sweeps (modify)

Replace both `for (row = 0u; row < FS_RESIDENT_NAMES_PATTERN_BASE; row++) {`
with:

```c
    for (row = 0u; row < FS_RESIDENT_NAMES_ROW_COUNT; row++) {
        /* Pattern rows are owned by their whole-file transaction; Effect rows
         * (step 6) are scalar-record objects and follow the ordinary rule. */
        if (filesystem_residentRowIsPattern(row))
            continue;
```

The existing loop bodies are unchanged. In the 7688 comment, "Pattern rows are
excluded" becomes "Pattern rows are excluded; Effect rows are included".

### 7.10 Lines 11012–11016 — scratch assert (modify)

`== 1455u` → `== 1599u`. Add a comment: "Effect rows add 16 x 9 mirror bytes
(Session 072 step 6)".

### 7.11 Scene Load state machine

**Phase 9, lines 12040–12045 (modify):**

```c
            } else if (op_scene_effect_open_name[0] == '\0' &&
                       filesystem_nameHasExtension(op_object.id.displayName,
                                                   ".fx")) {
                /*
                 * First `.fx` child (plan §14.2). The short alias opens the
                 * file; the display stem is the Effect name through the
                 * unchanged Instrument stem rule (G5), independent of the
                 * Scene directory name (G6).
                 */
                storage_copyFilename(op_scene_effect_open_name,
                                     op_object.id.shortName);
                filesystem_copyInstrumentStemDisplay(
                    op_effect_display_name, op_object.id.displayName);
            }
```

**Phase 11, lines 12058–12063 (modify):** remove
`||\n            op_scene_effect_open_name[0] == '\0'` from the failure
condition. Add a comment:

"A Scene without `.fx` is valid and loads `off` with a blank name (A37, G6);
the Kit and Pattern children remain required."

**Phase 16, line 12150 (modify):** replace `op_phase = 17;` with:

```c
        /*
         * Stage the Effect before the embedded Kit (Session 072 step 6, D1).
         *
         * The working directory is still the Scene folder, where the `.fx`
         * child was discovered. Parsing it here means a malformed Effect fails
         * before filesystem_commitSceneStage() changes any resident byte. A
         * Scene with no `.fx` keeps the `off` stage from
         * filesystem_initSceneStage() and goes straight to the Kit.
         */
        op_phase = (op_scene_effect_open_name[0] != '\0') ? 56u : 17u;
```

**Line 12333 onwards, per-Scene source staging (modify):** inside the
`source_scene` loop, after the Kit-row `filesystem_setResidentSource(...)`,
add:

```c
                        /* The Scene supplies its Effect child too (step 6). */
                        (void)filesystem_setResidentSource(
                            filesystem_residentEffectRow(source_scene),
                            FS_RESIDENT_SOURCE_INHERIT);
```

**Lines 12664–12679, comment block before `case 44` (replace):**

```c
    /*
     * Named v4 Pattern child.
     *
     * The Effect child was staged before the embedded Kit (phases 56..60 run
     * from phase 16) and committed with the Scene settings and Kit. Only the
     * Pattern is read after commit, under its agreed non-atomic policy. Both
     * children publish HCNAMES rows (129..144 Pattern, 145..160 Effect)
     * through the Scene action's marked-children block (Session 061
     * invariant). Affiliates: filesystem_cacheCurrentResidentSceneChildNames(),
     * filesystem_cacheCurrentBankSceneNameBlock(), and the boot reader.
     */
```

**Line 12921, phase 53 (modify):** `op_phase = 56u;` → `op_phase = 61u;`,
with the comment `/* Effect was already staged before the Kit (D1). */`.

**Phases 56–60, lines 12924–12998 (modify):**

- Rename the case labels "effect placeholder" → "Effect file".
- **56:**

```c
    case 56: /* OPEN Effect file (from phase 16; cwd = Scene folder) */
        storage_effectStateInit(&op_effect_state,
                                &fs_stage_workspace.scene_stage.effect);
        op_line_len = 0u;
        if (filesystem_bankPayloadDetailActive())
            filesystem_bootLoggingSetBankSceneDetail('K');
```

  The rest of 56 is unchanged. The detail letter becomes `'K'` because this
  I/O now belongs to the metadata/Kit family.
- **58:** `storage_effectParseLine(&op_effect_state, op_line_buf,
  &fs_stage_workspace.scene_stage.effect)` and `storage_effectFinalize(
  &op_effect_state, &fs_stage_workspace.scene_stage.effect)`.
- **59:** the detail letter `'P'` → `'K'`.
- **60, line 12998:** `op_phase = 61;` → `op_phase = 17;`, with the comment
  `/* Effect staged; continue with the embedded Kit (D1). */`.

  The failure paths (`op_phase = 62`) are unchanged. They now run before any
  commit, so a malformed `.fx` leaves the resident Scene untouched.

### 7.12 Stage defaults, commit, and discovery reset (modify)

**`filesystem_initSceneStage()`, after the slot loop (line ~16270):**

```c
    /* Staged Effect defaults to `off` (Session 072 step 6); a Scene with no
     * `.fx` commits exactly this record. effect_morph_amount is zero from the
     * memset and is overwritten only by the optional sceneset key. */
    scene_effectRecordDefaults(&stage->effect);
```

**`filesystem_commitSceneStage()`, after
`target->kit = fs_stage_workspace.scene_stage.kit;`:**

```c
        /* Effect commits with its Scene, never with a Kit (plan §7.3). */
        target->effect = fs_stage_workspace.scene_stage.effect;
```

Add "Effect record" to the function comment's output list.

**`filesystem_resetSceneLoadChildDiscovery()`, after line 16341:**

```c
    /* Blank until a `.fx` child is found (G6: missing file = blank name). */
    memset(op_effect_display_name, ' ', STORAGE_KIT_DISPLAY_NAME_LEN);
    op_effect_display_name[STORAGE_KIT_DISPLAY_NAME_LEN] = '\0';
```

Mention the Effect name in the function comment.

**Line 25551, `filesystem_start()` reset:** add the same two lines after the
`op_scene_effect_open_name` memset.

### 7.13 `sceneset.scg` and the Effect writer adapter (modify)

**After line 16872** (`case 9u` returns), before `default:`:

```c
    case 10u:
        /* Scene Effect Morph amount (Session 072 step 6; optional on read). */
        return filesystem_formatAssignmentU16Line(
            dst, cap, "effect_morph_amount",
            scene->settings.effect_morph_amount);
```

**Lines 16878–16889 (replace):**

```c
static uint8_t filesystem_nextEffectLine(char *dst, uint16_t cap, void *raw)
{
    /*
     * Adapt storageTypes' `.fx` v2 writer to filesystem_writeTextLine
     * (Session 072 step 6).
     *
     * Input: raw is the saved Scene's retained effect_record_t; the generic
     * writer supplies op_write_line_index. Output: one schema line, or 0 after
     * the final lane. storageTypes owns the grammar so save cannot drift from
     * the parser.
     */
    return storage_formatEffectLine(dst, cap, (const effect_record_t *)raw,
                                    op_write_line_index);
}
```

### 7.14 Scene Save (modify)

**Lines 19451–19466 and 19574–19589:** replace both "Unregistered Effect
child (future HCNAMES row)" blocks with:

```c
    /*
     * Effect child `<name>.fx` (Session 072 step 6).
     *
     * What: the Scene's retained Effect record is written as `.fx` v2, named
     * from the resident HCNAMES Effect row through the unchanged Instrument
     * stem rules (G5; blank -> `none.fx`). The Scene directory was deleted and
     * recreated, so no stale `.fx` survives. The written stem is captured for
     * the Effect row the Scene action publishes. Affiliates:
     * storage_formatEffectLine(), filesystem_nextEffectLine(), and
     * filesystem_cacheCurrentResidentSceneChildNames().
     */
```

**Phase 82 open, lines 19797–19806, and the unreachable phase 33 open at
19590–19602:** replace the `afatfs_fopen_lfn("effects.fx", …` call with:

```c
        {
            const char *fx_name = filesystem_cachedResidentName(
                filesystem_residentEffectRow(op_kit_save_source_scene));

            /* A blank or invalid cell takes the Instrument all-space rule. */
            if (filesystem_residentNameIsBlank(fx_name))
                fx_name = "        ";
            storage_makeSavedEffectDisplayFilename(
                op_scene_effect_open_name,
                sizeof(op_scene_effect_open_name), fx_name);
            filesystem_copyInstrumentStemDisplay(op_effect_display_name,
                                                 op_scene_effect_open_name);
        }
        if (!afatfs_fopen_lfn(op_scene_effect_open_name, "w",
                              AFATFS_MATCH_CASE_INSENSITIVE,
                              op_root_open_name, on_file_opened))
            return;
```

**Phase 84 write (19822–19826), and the unreachable phase 35 (19619–19622):**

```c
        if (filesystem_writeTextLine(filesystem_nextEffectLine,
                                     (void *)&scene->effect))
            return;
```

Rename the labels "WRITE effects placeholder" / "open effects" to "WRITE
Effect `<name>.fx`" / "open Effect".

**Root Scene Save source staging, after line 19725** (the Pattern-row
`filesystem_setResidentSource`), add:

```c
            /* Scene Save rewrites the Effect child too (step 6): it inherits
             * the saved Scene's source, and its AutoSave bytes are recaptured. */
            (void)filesystem_setResidentSource(
                filesystem_residentEffectRow(op_kit_save_source_scene),
                FS_RESIDENT_SOURCE_INHERIT);
            autosave_markSourceDirty(
                filesystem_residentEffectRow(op_kit_save_source_scene));
```

In the Pattern comment above it, "145-row" → "161-row".

### 7.15 Comments only (modify)

- **Lines 21541–21544, `filesystem_appendInstrumentTypeField()`:** the return
  comment becomes `/* Bank/Scene/Kit/Pattern/Effect rows carry no type field */`.
  The logic is unchanged: `>= PATTERN_BASE` returns 1 for both blocks.
- **Lines 28113–28116, `filesystem_bootReaderApplyRowType()`:** the same
  comment change.
- **Lines 21689–21700, `filesystem_nextResidentNameLine()` comment:** append
  "…then sixteen Pattern rows and sixteen Effect rows, all blank in the boot
  serializer".

### 7.16 Lines 26347–26475 — winner regeneration (modify)

- **Classifier comment (26347–26361):** add "Effect name (Scene-relative
  131..138) and Effect source (558..559)".
- **In `filesystem_regenClassifyPayloadByte()`,** after the `if (r < 10u) { …
  }` Scene block and before the Kit test at line 26415, insert:

```c
    /* Effect row identity (Session 072 step 6): name 3..10, source 430..431. */
    if (r >= AUTOSAVE_EFFECT_OFFSET + AUTOSAVE_EFFECT_NAME_OFFSET &&
        r < AUTOSAVE_EFFECT_OFFSET + AUTOSAVE_EFFECT_NAME_OFFSET +
                AUTOSAVE_NAME_BYTES) {
        *row = (uint16_t)(FS_RESIDENT_NAMES_EFFECT_BASE + scene_index);
        *byte_index = (uint8_t)(r - AUTOSAVE_EFFECT_OFFSET -
                                AUTOSAVE_EFFECT_NAME_OFFSET);
        return 1u;
    }
    if (r >= AUTOSAVE_EFFECT_OFFSET + AUTOSAVE_EFFECT_SOURCE_OFFSET &&
        r < AUTOSAVE_EFFECT_OFFSET + AUTOSAVE_EFFECT_SOURCE_OFFSET +
                AUTOSAVE_SOURCE_BYTES) {
        *row = (uint16_t)(FS_RESIDENT_NAMES_EFFECT_BASE + scene_index);
        *byte_index = (uint8_t)(r - AUTOSAVE_EFFECT_OFFSET -
                                AUTOSAVE_EFFECT_SOURCE_OFFSET);
        return 2u;
    }
```

- **Regeneration comment (26448–26475):** "expanded 145-row .hcnames from the
  winner's 129 AutoSave-wire identity fields" → "161-row .hcnames from the
  winner's identity fields (Pattern rows stay UNKNOWN; Effect rows carry the
  record's name and source)". Add "Effect name (scene_base + 131) and source
  (+558)" to the extracted-cell list.

### 7.17 After line 27743 (end of `filesystem_bootReaderNarrowLoadInstrument()`) — add

```c
/*
 * Boot-time narrow Effect reload (Case 2, Session 072 step 6).
 *
 * What: reloads one Scene's Effect from `<row name>.fx` inside the resolved
 * Scene folder (root Scene/NNN or the Bank child), using the same name builder
 * as Scene Save. Inputs: scene_index, the resolved Scene/Bank source, and the
 * row that supplied it. Output: scene->effect replaced from the staged parse;
 * no HCNAMES source or refresh changes (P2, as the other narrow loaders).
 * An absent file restores `off` (A37) and succeeds; a present but malformed
 * file returns 0 so the caller applies Case 3. A direct (library) Effect
 * source is not supported until the Effect library exists and returns 0.
 * Why: the async Scene loader is unavailable during boot, and reloading the
 * whole Scene would overwrite children that carry their own sources.
 * Affiliates: filesystem_bootReaderEnterSceneFolder(),
 * storage_makeSavedEffectDisplayFilename(), storage_effect*(), and
 * filesystem_bootReaderEvaluateScene()/filesystem_bootHcnamesAuthoritativeLoad().
 */
static uint8_t filesystem_bootReaderNarrowLoadEffect(
    uint8_t scene_index, uint16_t source_slot, uint16_t resolved_row)
{
    uint16_t effect_row = filesystem_residentEffectRow(scene_index);
    char file_name[STORAGE_KIT_FILENAME_MAX];
    const char *stem;
    afatfsFilePtr_t file;
    effect_record_t *staged = &fs_stage_workspace.scene_stage.effect;
    uint8_t len = 0u;
    uint8_t ready = 0u;
    uint8_t eof = 0u;
    uint8_t ok = 0u;
    scene_t *scene = scene_get(scene_index);

    if (!scene || effect_row >= FS_RESIDENT_NAMES_ROW_COUNT ||
        resolved_row == effect_row) {
        return 0u;
    }
    if (!filesystem_bootReaderEnterSceneFolder(
            scene_index, source_slot, resolved_row)) {
        return 0u;
    }
    stem = hcnames_name_mirror[effect_row];
    if (filesystem_residentNameIsBlank(stem))
        stem = "        ";
    storage_makeSavedEffectDisplayFilename(file_name, sizeof(file_name),
                                           stem);
    storage_effectStateInit(&op_effect_state, staged);
    filesystem_resetTextReader();
    file = filesystem_blockOpenLfn(file_name);
    if (!file) {
        /* Missing `.fx` is a valid `off` Effect (A37). */
        scene->effect = *staged;
        return 1u;
    }
    for (;;) {
        storage_status_t st = filesystem_bootReadLineBlocking(
            file, op_line_buf, &len, sizeof(op_line_buf), &ready, &eof);

        if (st != STORAGE_STATUS_OK)
            break;
        if (ready) {
            if (storage_effectParseLine(&op_effect_state, op_line_buf,
                                        staged) != STORAGE_STATUS_OK)
                break;
            ready = 0u;
            continue;
        }
        if (eof) {
            ok = (uint8_t)(storage_effectFinalize(&op_effect_state, staged) ==
                           STORAGE_STATUS_OK);
            break;
        }
    }
    (void)filesystem_blockClose(file);
    if (!ok)
        return 0u;
    scene->effect = *staged;
    return 1u;
}
```

### 7.18 Boot reader (modify)

**Line 28150, `filesystem_bootReaderParseRegisterFile()` comment:** "(header
plus 145 rows)" → "(header plus 161 rows)".

**`filesystem_bootReaderEmptyScene()`, lines 28226–28290:**

- Comment: "Scene's nine HCNAMES rows (Scene, Kit, Pattern, and six
  Instruments)" → "ten HCNAMES rows (Scene, Kit, Pattern, Effect, and six
  Instruments)". Add "and the `off` Effect record" to the defaults list.
- After the slot loop (before `pat_initScene`), add:

```c
    /* memset() zeroed the Effect record (level 0, length 0); restore the
     * valid `off` defaults a fresh Scene has (Session 072 step 6). */
    scene_effectRecordDefaults(&scene->effect);
```

- After the Pattern-row reset, add:

```c
    row = filesystem_residentEffectRow(scene_index);
    fs_resident_source[row] = (uint16_t)(
        FS_RESIDENT_SOURCE_UNKNOWN | FS_RESIDENT_SOURCE_REFRESHED_FLAG);
```

**`filesystem_bootReaderResolveResidentRow()`, lines 28309–28324:** after the
Instrument guard, add:

```c
    if (resolved < FS_RESIDENT_SOURCE_DIRECT_SLOT_LIMIT &&
        filesystem_residentRowIsEffect(row) &&
        resolved_row && *resolved_row == row) {
        /* No Effect library exists yet (A35): a numeric source directly on
         * an Effect row is unresolvable until it does (Session 072 step 6). */
        resolved = FS_RESIDENT_SOURCE_UNKNOWN;
    }
```

Add "and Effect-row" to the comment.

### 7.19 `filesystem_bootReaderEvaluateScene()`, lines 28326–28490 (modify)

- **Comment 28326–28346:** "eight identity rows" → "nine identity rows (Scene,
  Kit, six Instruments, Effect)". Add `autosave_applyEffectPayload()` and
  `filesystem_bootReaderNarrowLoadEffect()` to the affiliates.
- **Line 28370:** `for (index = 0u; index < 8u; index++) {` →
  `for (index = 0u; index < 9u; index++) {`.
- **Row selection, lines 28375–28380** (the final `else`):

```c
        else if (index < 8u)
            row = filesystem_residentInstrumentRow(
                scene_index, (uint8_t)(index - 2u));
        else
            /* Ninth row: the Scene's Effect (Session 072 step 6). */
            row = filesystem_residentEffectRow(scene_index);
```

- **Case 1:** between the `index == 1u` branch and the Instrument `} else {`,
  insert:

```c
            } else if (index == 8u) {
                /* Effect region; unknown/absent token restores `off` (§15). */
                const uint8_t *effect_section =
                    scene_section + AUTOSAVE_EFFECT_OFFSET;

                autosave_applyEffectPayload(scene_index, effect_section);
                section = effect_section + AUTOSAVE_EFFECT_SOURCE_OFFSET;
                embedded_offset = 0u;
```

- **Case 2, after the `index == 1u` narrow-Kit branch (~28455), insert:**

```c
                } else if (index == 8u) {
                    load_ok = filesystem_bootReaderNarrowLoadEffect(
                        scene_index, resolved, resolved_row);
```

The ordering puts the Effect last, after every Kit/Instrument narrow load that
reuses `fs_stage_workspace`. The staged Effect therefore cannot be overwritten
between its parse and its copy.

### 7.20 `filesystem_bootHcnamesAuthoritativeLoad()`, lines 28740–28800 (modify)

- **Comment:** "per-Scene resolution of the eight rows" → "nine rows".
- **Line 28756:** `index < 8u` → `index < 9u`.
- **Row selection:** the same change as §7.19.
- **Narrow-load dispatch:** add the same `index == 8u` branch calling
  `filesystem_bootReaderNarrowLoadEffect()`.

---

## 8. `filesystem.h` (comments only)

| Line | Change |
|---|---|
| 385 | "all 145 HCNAMES rows" → "all 161 HCNAMES rows" |
| 939 | "all 145 root HCNAMES rows" → "all 161 root HCNAMES rows" |
| 1032 | "temporary 145-row HCNAMES view" → "temporary 161-row HCNAMES view" |

## 9. `presetManager.c` (comment only)

`on_scene_save_complete()` comment: "pattern stub, and effect placeholder" →
"named Pattern child, and `<name>.fx` Effect (v2)".

---

## 10. Documentation (same change set)

- **`FILESYSTEM_SPEC.md`:**
  - the `.fx` v2 grammar (§4.6 header block) and the D3 tokens;
  - Scene children: a missing `.fx` is now valid, and exactly the first `.fx`
    is used;
  - load order (D1);
  - Scene Save `<name>.fx` naming (D4);
  - the `sceneset.scg` `effect_morph_amount` key;
  - HCNAMES 161 rows with the Effect block (145..160) and its source tokens.
- **`AUTOSAVE.md`:**
  - format version 3;
  - Effect source bytes 430..431 (D2);
  - Effect name bytes are baseline-only;
  - the Effect row in Case 1/2/3;
  - `autosave_applyEffectPayload()`;
  - the regeneration classifier;
  - card preparation (D5).
- **`SRAM_MANIFEST.md`:** `fs_resident_source` 322 B; `hcnames_name_mirror`
  1,449 B; `op_effect_display_name` 9 B; `storage_effect_state_t` 7 B; the
  scratch assert total 1,599 B; the stage union unchanged at 2,048 B, with the
  Scene stage now 1,621 B.
- **`MODULE_INTERCHANGE_SPEC.md`:**
  - storageTypes ↔ EffectsManager (`effects_recordDefaultsForType`,
    `effects_laneFileKey`/`effects_laneByFileKey`, `effects_descriptorByKey`,
    `effects_typeFromToken`);
  - filesystem ↔ Autosave (Effect row, `autosave_applyEffectPayload`).
- **`BANK_PRESET_ARCHITECTURE.md`:** Scene Load/Save and Bank child Effect
  behavior; Kit Load does not touch the Effect.
- **`EFFECTS_BUS_FEATURE_PLAN.md`:** §14.1 (step-scale tokens); §14.2 (D4
  `none.fx` note); §14.3 (D1 load order); §15 (source field 430..431, version
  3); §17.1 Step 6 marked as scheduled.
- **`MEMORY.md` volatile note:** "S072 Step 6: `.fx` v2 storage live. Scene and
  Bank children load/save `<name>.fx`, with HCNAMES Effect rows 145..160 and
  AutoSave v3 carrying the Effect source at 430..431. Delete `.hcnames` and
  `.hcprms*` before the first boot."

---

## 11. Build and verification gates

### Build

1. `make clean && make all` succeeds with no new warnings.
2. `link_budget.py`:
   - DTCM 4,448 B and FXBUF 126,624 B are unchanged.
   - Record the flash delta (expected +4 to +6 KB).
   - Record the SRAM1 `.bss` delta (expected +189 B, allowing for alignment).

### Card preparation (D5)

3. Back up the card. Delete root `.hcnames`, `.hcnamtmp`, `.hcprms1` and
   `.hcprms2`. Boot once, which runs the canonical Bank Load.
4. Confirm `.hcnames` has one `#types` line and **161** data rows, with rows
   146..161 of the file being the Effect rows.

### Fixture card (hand-written, stems of 8 characters or fewer)

| Fixture | Location | Contents | Expected |
|---|---|---|---|
| F1 legacy | an existing Scene | existing `effects.fx` (v1 placeholder) | loads `off`; Effect row `effects	-` |
| F2 v2 | `Scene/NNN FxTest/filter.fx` (copy another Scene, replace its `.fx`) | see below | loads `flt`; Effect row `filter	-` |
| F3 missing | a Scene with its `.fx` deleted | — | loads (previously failed); `off`; blank Effect row |
| F4 malformed | a Scene with `bad.fx` containing `type=zzz` | — | Scene Load fails; resident Scene unchanged |
| F5 partial Bank | Bank children `00` = F2, `01` = F3, `02` = F4 | — | 00 and 01 load; 02 fails (existing partial-Bank handling) |

The F2 file:

```
format=helicase.effect
version=2
type=flt

[params]
effect_audio_out=0
effect_level=127
effect_pan=64
filter_freq=40
filter_reso=90
filter_drive=0
filter_type=0
```

F2 deliberately has no `[morph]` and no `[sequence]`, which exercises the
copy-`[params]` fallback and the sequence defaults.

### Hardware, production image

5. **Load F2.** Raise one voice's FX send. The filtered copy is heard on St1.
   - Run the Step 5 §12 gates here: the three fader modes, pre-volume tap,
     pan, FX_SEND automation, jack fallback, headroom, CPU, and switching to
     an `off` Scene.
6. **F1, F3, F4:** results as in the table.
   - F4 must leave the previously resident Scene sounding exactly as before.
     This is the D1 atomicity check.
7. **Scene Save F2 to a new slot.** Inspect the card:
   - `filter.fx` holds `version=2`, `type=flt`, `[params]` (7 rows), `[morph]`
     (Morphable rows only), and `[sequence]` with `run_mode=fwd`, `length=16`,
     `step_scale=1/16`, `lane.effect_morph=0x0000,…` plus the 6 flt lanes.
   - `sceneset.scg` ends with `effect_morph_amount=0`.
   - There is no `effects.fx`.
8. **Blank name:** save the F3 Scene. The card receives `none.fx` (D4).
   Reloading it gives a blank Effect row.
9. **Bank Save / Bank Load** of F5 (children 00 and 01): each child folder
   holds its `<name>.fx`, and the reload is identical.
10. **Reboot restore (Case 1).** With F2 loaded, wait for AutoSave, then
    power-cycle. The filter is still live.
    - `.hcprms`: header version byte 3.
    - Effect region: `66 6C 74` followed by the live cells.
    - Source bytes at Scene offset 558..559: `FF 1F`
      (`FS_RESIDENT_SOURCE_INHERIT` = 0x1FFF, little-endian).
11. **Case 2 (optional).** Load F2, then power-cycle *before* the AutoSave
    drain (within the debounce window). The boot reader narrow-reloads
    `filter.fx`; the filter is live after boot.
12. **Regression:**
    - Kit Load and Instrument Load do not change the Effect.
    - Scene switching is unchanged.
    - The `cpu` widget matches Step 5 with the same Effect state.
13. **Step 4 carry-over:** boot screen `r0` (diagnostic image only); PERF
    `mrp` marks Scene parameter 40.

### Not testable yet

- the Effect library and its browser;
- Effect parameter edits (Step 7);
- sequence playback (Step 8);
- the edit-mask gate (Step 10).

### Rollback

Revert the change set. On the card, restore the backed-up `.hcnames` and
`.hcprms*`, or delete them again. v2 `.fx` files are ignored by older
firmware's strict placeholder parser: such Scenes fail to load there, so keep
the backup.

## Implementation notes (2026-09-28)

### Code completed

- Added registry-backed Effect storage helpers in `EffectsManager`, including
  default construction, descriptor key lookup, and reverse key lookup.
- Replaced the Effect storage placeholder with a strict streaming `.fx` v2
  parser/writer. It accepts legacy v1 `placeholder=1` as `off`, resolves
  parameters and lanes through the registry, applies `[morph]`/`[sequence]`
  defaults, and writes bounded lines for asynchronous SD streaming.
- Connected the optional named `.fx` child through Scene and Bank load/save.
  The Effect is staged with Scene settings and Kit before commit, so malformed
  files do not partially replace a resident Scene. A missing child is a valid
  `off` default; a blank identity uses `none.fx` on save.
- Expanded HCNAMES from 145 to 161 rows. Effect rows are 145..160, use the
  non-Instrument grammar, inherit through the owning Scene, and carry no direct
  root Effect source. Scene/Bank save, AutoSave refresh tracking, regeneration,
  and both boot readers now include the Effect row.
- Bumped the scalar AutoSave header to v3. Effect source bytes are projected at
  relative bytes 430..431; Effect name bytes remain a baseline mirror rather
  than live mask cells. `autosave_applyEffectPayload()` restores the complete
  512-byte Effect region during Case 1 boot restore.
- Added the narrow boot Effect reader for Case 2 and updated the Scene-set
  schema with optional `effect_morph_amount`.
- Updated the adjacent `.c`/`.h` descriptive blocks and the filesystem,
  AutoSave, SRAM, module-boundary, Bank/Preset, feature-plan, and project-memory
  references to describe the implemented ST6 contract.

### Decision and memory record

- D1: the Effect record is staged atomically with Scene settings and Kit in the
  existing 2,048-byte union; the peak Scene stage is 1,621 bytes.
- D2: Effect source is stored in the first reserved Effect bytes, relative
  430..431, without changing the 512-byte per-Scene region.
- D3: the `.fx` schema uses the approved 13 step-scale tokens and
  `fwd/rev/pip/rnd/sel` run tokens; the default step scale is `1/16`.
- D4: blank Effect identity uses the existing Instrument-name formatting path;
  the saved fallback filename is `none.fx` while the HCNAMES name remains
  blank.
- D5: old 145-row HCNAMES and AutoSave v1/v2 records are not migrated. The
  first ST6 card should remove `.hcnames`, `.hcnamtmp`, `.hcprms1`, and
  `.hcprms2` so firmware can establish a clean v3 baseline.
- D6: the approved SRAM1 expansion is 32 bytes for the source register,
  144 bytes for the HCNAMES mirror, 9 bytes for the Effect display stem, and
  7 bytes for the Effect parser state. DTCM allocation is unchanged.

### Verification status

The final clean production build completed with
`text=470,208`, `data=408`, and `bss=426,128`; the flash payload was
470,616 bytes and the packaged `LXRV2_lxr02.img` is 470,632 bytes including
its 16-byte image header. The link-budget report showed 470,616 of 491,520 flash bytes
used (20,904 bytes remaining), 3,768 ITCM bytes, unchanged 4,448 DTCM
statics, and the approved 126,624-byte FX buffer arena. Compiler output had
only the existing packed-member/unused-static and linker syscall warnings.

The hand-written fixture/card and hardware gates remain pending in this
workspace: no SD-card hardware is attached for F1–F5, Bank round-trip, or
reboot Case-1/Case-2 acceptance. The clean production build and image
regeneration are complete.

---

## 12. Assessment (review of the implemented Step 6 tree)

Reviewed on 2026-09-28. I read the full working-tree diff for `storageTypes`,
`EffectsManager`, `Autosave`, `filesystem`, `presetManager` and the headers,
then did an independent clean rebuild.

### 12.1 Build

- **Clean rebuild:** `make clean && make all`, exit 0.
  - `text=470,208`, `data=408`, `bss=426,128`.
  - `link_budget.py`: flash 470,616 / 491,520 B, headroom **20,904 B**
    (+4,848 B against Step 5, inside the +4 to +6 KB estimate).
  - DTCM statics 4,448 B and FXBUF 126,624 B are unchanged.
- **RAM (`nm`):** `fs_resident_source` 322 B, `hcnames_name_mirror` 1,449 B,
  `op_effect_display_name` 9 B, `op_effect_state` 7 B. `.bss` grew by 192 B
  (189 B plus alignment), matching D6.
- **Warnings:** 20 in total. The only ones from touched files are the
  pre-existing unused-function warnings in `filesystem.c`; no new warning.

### 12.2 Code against schedule

| § | Item | Result |
|---|---|---|
| 2–3 | EffectsManager helpers | Match. |
| 4.1–4.2 | Parser state and API | Match. Header comment blocks are condensed but accurate. |
| 4.4–4.5 | sceneset comments and `effect_morph_amount` | Match. |
| 4.6 | Parser, writer, filename helper | Match. The lane parser adds a `'\0'` guard in the hex loop; that improves on the schedule. |
| 5 | `Autosave.h` | Match. `autosave_extractPayloadSource()` now takes a `uint16_t` offset bounded by the 512 B Effect region, so the Effect source at 430 can use the common cross-check. This is equivalent and acceptable. |
| 6 | `Autosave.c` | Match. The live Effect parameter window is narrowed to the 419 cells, so the source bytes are never reported as parameters. |
| 7.1–7.10 | Row layout, predicates, row-class sites, refresh sweeps, asserts | Match. |
| 7.11 | Scene Load phases | Match: 16 → 56, 60 → 17, 53 → 61, phase 11 no longer requires `.fx`, and the Effect row is staged `-`. One deviation: phase 9 takes the display stem through `filesystem_patternDisplayFromFilename()` instead of `filesystem_copyInstrumentStemDisplay()`. Output is identical (first eight cells before the first `.`), so no behavior change. |
| 7.12–7.13 | Stage defaults, commit, resets; sceneset line 10; writer adapter | Match. The adapter takes the `scene_t` pointer and projects `&scene->effect`, which is equivalent. |
| 7.14 | Scene Save | See **Finding 1**. |
| 7.16–7.20 | Regeneration, narrow loader, EmptyScene, numeric guard, nine-row readers | Match. |
| 8–9 | `filesystem.h` and `presetManager.c` comments | Match. |

### 12.3 Findings

1. **Blank-name Scene Save can write `inst.fx` instead of `none.fx`
   (defect, low impact).**
   - **Where:** Scene Save phase 82 (`filesystem.c` ~19884) and the
     unreachable phase 33 (~19655).
   - **What happens:** both call
     `storage_makeSavedEffectDisplayFilename(…, fx_stem)` with the raw mirror
     cell, even when `filesystem_residentNameIsBlank(fx_stem)` is true.
   - A blank HCNAMES row read back from the card is stored in the mirror as
     all-NUL: `filesystem_cacheResidentRecord()` copies only a non-empty name
     (`filesystem.c:6064`).
   - The Instrument helper maps an empty stem to `inst`
     (`storageTypes.c:762`). The file therefore becomes `inst.fx`, which
     reloads with the non-blank name `inst`.
   - Within one session, a blank row cached from `op_effect_display_name` is
     all spaces and correctly gives `none.fx`. The result therefore depends on
     whether `.hcnames` has been re-read.
   - **Knock-on:** the boot narrow loader maps a blank row to `none.fx`. Case 2
     would then miss an `inst.fx` on the card and restore `off`.
   - **Fix (schedule §7.14, as written):** in both places, pass
     `blank ? "        " : fx_stem` to
     `storage_makeSavedEffectDisplayFilename()`. This is a two-line change.
2. **Kit Save now writes the Effect row (unscheduled; conflicts with plan
   §7.3).**
   - **Where:** `filesystem_saveKitDirectory_tick()` phase 21
     (`filesystem.c:17960-17964`) sets the Effect row source to `-` and marks
     its AutoSave source bytes dirty.
   - Kit operations must not touch the Effect: the adjacent comment still says
     "seven dirty source cells".
   - It is harmless while every Effect row is already `-`. It is wrong for a
     row left at `?` by boot Case 3 (`EmptyScene`): a later Kit Save would
     silently make that Effect resolvable through the Scene folder.
   - **Fix:** remove those five lines.
3. **Record inaccuracy in the implementation notes (documentation).**
   - The notes above say "13 step-scale tokens". The code (correctly) has
     **14** (`EFFECT_SEQ_SCALE_COUNT`, statically asserted).
4. **Condensed comment blocks (cosmetic).**
   - Several scheduled comment blocks were shortened, for example
     `storage_effectStateInit`, `storage_effectParseLine`,
     `autosave_applyEffectPayload` and `filesystem_bootReaderNarrowLoadEffect`.
   - The shortened versions are accurate. The fuller rationale lives in the
     updated FILESYSTEM_SPEC and AUTOSAVE docs. No action is required.

I found no other correctness defects.

- **Staging overlap:** the staged Effect sits at offsets 1,201..1,620 of the
  2,048 B union. The Kit stage (`kit_t`, 0..1,159) and the Instrument stage
  do not overlap it, and the Scene loader writes only
  `scene_stage.kit/settings/effect`. The Effect record therefore survives the
  Kit phases that now run after it.

### 12.4 Fixture-gate expectation to note before the bench run

A malformed `.fx` (F4) in a **root** Scene fails with
`FS_LOAD_INVALID_SCENE`.

- The existing quarantine path (phase 62 → 68) then **renames that root Scene
  folder** with the quarantine prefix. The resident Scene is untouched (D1
  holds), but the fixture folder name changes on the card.
- Bank-local children skip the rename (phase 62 → 72) and clear their present
  bit instead.
- This is the existing behavior for any invalid Scene child. Use a disposable
  copy for F4.

### 12.5 Hardware status

Not yet run: the fixture card, card preparation (D5), and gates 3–13 are
pending. I recommend applying Findings 1 and 2 before the bench session so
gate 8 (blank name) and Kit Save regression are tested on the final code.
