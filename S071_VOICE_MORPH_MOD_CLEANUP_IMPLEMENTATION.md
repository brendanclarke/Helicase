# S071 Voice Morph Automation and Modulation Cleanup — Implementation Schedule

Session: S071 · Branch: `dev-ph5-effects` · Base: commit `7341d3b`.

This document lists every code change required to implement the plan in
`S071_VOICE_MORPH_AUTOMATION_MODULATION_CLEANUP.md`. Each change is specified
by file, line, and add/remove/modify. Comment-block descriptions accompany
each change for documentation-in-place alongside the code.

---

## Part A — Per-Scene Voice-Edit Mask

### Change A1 — BankData.c: per-Scene mask storage declaration

**File**: `Core/Bank/BankData.c`
**Line**: 8
**Action**: MODIFY

**Current**:
```c
static uint16_t bank_scene_mask_voice_edit;
```

**New**:
```c
static uint16_t bank_scene_mask_voice_edit[BANK_SCENE_SLOT_COUNT];
```

**Comment block**:
```
/*
 * Per-Scene voice-edit fan-out mask.
 *
 * What: 16 × uint16_t (32 bytes SRAM). Each Scene owns an independent
 * bitmask controlling which Scenes receive VOICE-page and Scene-setting edits
 * when that Scene is active. bank_active_scene_slot selects the active entry.
 *
 * Inputs: bank_init() seeds each Scene's mask to (1u << scene_index) — self
 * only. Runtime: VOICE+SEQ toggles, bankset.bcg load, and Autosave restore.
 * Outputs: menu edit commit fan-out, VOICE-held SEQ LED display, Autosave
 * drain, Bank Save capture.
 *
 * Accessors: bank_sceneMaskVoiceEdit() returns the active Scene's mask.
 * bank_sceneMaskVoiceEditForScene(i) returns any Scene's mask by index.
 * bank_setSceneMaskVoiceEdit() sets the active Scene's mask.
 * bank_setSceneMaskVoiceEditForScene(i, mask) sets any Scene's mask by index.
 * bank_sceneInVoiceEditMask(i), bank_toggleSceneMaskVoiceEdit(i) operate on
 * the active Scene's mask.
 *
 * Affiliate: Autosave.h AUTOSAVE_BANK_VOICE_EDIT_MASK_BYTES (32),
 * storageTypes.h storage_bankset_t.scene_mask_voice_edit[16].
 */
```

---

### Change A2 — BankData.c: bank_init() per-Scene mask initialization

**File**: `Core/Bank/BankData.c`
**Lines**: 134–135
**Action**: MODIFY

**Current**:
```c
    bank_scene_mask_present = 1u;
    bank_scene_mask_voice_edit = 1u;
```

**New**:
```c
    bank_scene_mask_present = 1u;
    /*
     * Seed each Scene's voice-edit mask to contain only itself.
     *
     * Inputs: none. Output: Scene i starts with bit i set. The user opts into
     * multi-Scene fan-out explicitly via VOICE+SEQ toggles. This default
     * resolves the boot-state stale-bit bug (Part C, Q-C3) because no Scene
     * inherits a prior session's mask on a clean init.
     *
     * Affiliate: bank_selectActiveSceneForEditMask() enforces the active-bit
     * invariant after Scene switch. bank_setSceneMaskVoiceEditForScene()
     * provides the indexed setter for bankset.bcg and Autosave restore paths.
     */
    {
        uint8_t i;
        for (i = 0u; i < BANK_SCENE_SLOT_COUNT; i++)
            bank_scene_mask_voice_edit[i] = (uint16_t)(1u << i);
    }
```

---

### Change A3 — BankData.c: update bank_ensureActiveInVoiceEditMask()

**File**: `Core/Bank/BankData.c`
**Line**: 70
**Action**: MODIFY

**Current**:
```c
    uint16_t previous_mask = bank_scene_mask_voice_edit;
```

**New**:
```c
    uint16_t previous_mask = bank_scene_mask_voice_edit[bank_active_scene_slot];
```

**Line**: 79 (inside the function body, where the scalar is assigned)
**Action**: MODIFY — all remaining scalar references to
`bank_scene_mask_voice_edit` within this function become
`bank_scene_mask_voice_edit[bank_active_scene_slot]`.

The comment block on the function is already accurate. No comment change.

---

### Change A4 — BankData.c: update bank_setSceneMaskVoiceEdit()

**File**: `Core/Bank/BankData.c`
**Lines**: 290–308
**Action**: MODIFY

Replace every reference to the scalar `bank_scene_mask_voice_edit` with
`bank_scene_mask_voice_edit[bank_active_scene_slot]`.

Affected lines within the function:
- Line 292: `uint16_t previous_mask = bank_scene_mask_voice_edit;`
  → `uint16_t previous_mask = bank_scene_mask_voice_edit[bank_active_scene_slot];`
- Line 305: `bank_scene_mask_voice_edit = bank_normalizeSceneMask(mask);`
  → `bank_scene_mask_voice_edit[bank_active_scene_slot] = bank_normalizeSceneMask(mask);`
- Line 307: `if (bank_scene_mask_voice_edit != previous_mask)`
  → `if (bank_scene_mask_voice_edit[bank_active_scene_slot] != previous_mask)`

Existing comment block is accurate. No comment change.

---

### Change A5 — BankData.c: update bank_sceneMaskVoiceEdit()

**File**: `Core/Bank/BankData.c`
**Lines**: 311–324
**Action**: MODIFY

- Line 323: `return bank_scene_mask_voice_edit;`
  → `return bank_scene_mask_voice_edit[bank_active_scene_slot];`

The `bank_ensureActiveInVoiceEditMask()` call already operates on the
active Scene's entry (via Change A3). No comment change.

---

### Change A6 — BankData.c: update bank_sceneInVoiceEditMask()

**File**: `Core/Bank/BankData.c`
**Lines**: 326–330
**Action**: No change needed. This function calls `bank_sceneMaskVoiceEdit()`
which already returns the active Scene's entry after Change A5. No direct
scalar reference.

---

### Change A7 — BankData.c: update bank_toggleSceneMaskVoiceEdit()

**File**: `Core/Bank/BankData.c`
**Lines**: 332–355
**Action**: MODIFY

Replace every reference to the scalar `bank_scene_mask_voice_edit` with
`bank_scene_mask_voice_edit[bank_active_scene_slot]`.

Affected lines:
- Line 335: `uint16_t previous_mask = bank_scene_mask_voice_edit;`
  → `uint16_t previous_mask = bank_scene_mask_voice_edit[bank_active_scene_slot];`
- Lines 350–351: `bank_scene_mask_voice_edit = (uint16_t)(bank_scene_mask_voice_edit ^ bit);`
  → `bank_scene_mask_voice_edit[bank_active_scene_slot] = (uint16_t)(bank_scene_mask_voice_edit[bank_active_scene_slot] ^ bit);`
- Line 353: `if (bank_scene_mask_voice_edit != previous_mask)`
  → `if (bank_scene_mask_voice_edit[bank_active_scene_slot] != previous_mask)`

Existing comment block is accurate. No comment change.

---

### Change A8 — BankData.c: add per-Scene setter

**File**: `Core/Bank/BankData.c`
**After**: line 308 (end of `bank_setSceneMaskVoiceEdit()`)
**Action**: ADD

```c
void bank_setSceneMaskVoiceEditForScene(uint8_t scene_index, uint16_t mask)
{
    uint16_t previous_mask;

    /*
     * Set one specific Scene's voice-edit fan-out mask by index.
     *
     * Inputs: zero-based Scene index and raw 16-bit mask. Output: the indexed
     * Scene's mask is normalized, the active-bit invariant is enforced when the
     * index matches bank_active_scene_slot, and the Autosave field is marked
     * dirty when the final value differs from the entry value.
     *
     * Clients: autosave_applyBankPayload() (Autosave restore), filesystem.c
     * bankset.bcg load sites. These callers need to address individual Scene
     * masks by index without changing the active Scene.
     *
     * Affiliate: bank_setSceneMaskVoiceEdit() is the active-Scene convenience
     * wrapper. bank_sceneMaskVoiceEditForScene() is the read counterpart.
     */
    if (scene_index >= BANK_SCENE_SLOT_COUNT)
        return;
    previous_mask = bank_scene_mask_voice_edit[scene_index];
    bank_scene_mask_voice_edit[scene_index] = bank_normalizeSceneMask(mask);
    if (scene_index == bank_active_scene_slot)
        (void)bank_ensureActiveInVoiceEditMask();
    if (bank_scene_mask_voice_edit[scene_index] != previous_mask)
        autosave_markBankFieldDirty(AUTOSAVE_BANK_FIELD_VOICE_EDIT_MASK);
}
```

---

### Change A9 — BankData.c: add per-Scene getter

**File**: `Core/Bank/BankData.c`
**After**: Change A8 (new `bank_setSceneMaskVoiceEditForScene()`)
**Action**: ADD

```c
uint16_t bank_sceneMaskVoiceEditForScene(uint8_t scene_index)
{
    /*
     * Read one specific Scene's voice-edit fan-out mask by index.
     *
     * Inputs: zero-based Scene index. Output: the indexed Scene's mask value,
     * or 0 for out-of-range indices. This getter does not enforce the active-bit
     * invariant — the invariant is a property of the active Scene only, and this
     * function is used by Autosave drain and Bank Save capture where the read
     * must reflect the stored state exactly.
     *
     * Clients: autosave_getLivePayloadByte() (Autosave drain), filesystem.c
     * bankset.bcg save site. Affiliate: bank_sceneMaskVoiceEdit() is the
     * active-Scene convenience getter (with self-repair).
     */
    if (scene_index >= BANK_SCENE_SLOT_COUNT)
        return 0u;
    return bank_scene_mask_voice_edit[scene_index];
}
```

---

### Change A10 — BankData.h: declare per-Scene setter/getter

**File**: `Core/Bank/BankData.h`
**After**: line 56 (`void bank_toggleSceneMaskVoiceEdit(uint8_t scene_index);`)
**Action**: ADD

```c
/*
 * Per-Scene voice-edit mask access by index.
 *
 * Inputs: zero-based Scene index for the setter also takes a raw 16-bit mask.
 * Outputs: the setter normalizes, enforces the active-bit invariant when the
 * index matches the active Scene, and marks the Autosave field dirty on change.
 * The getter returns the stored mask for Autosave drain and Bank Save capture.
 *
 * Clients: Autosave restore, bankset.bcg load/save, filesystem.c per-Scene
 * load loops. Affiliate: bank_setSceneMaskVoiceEdit() and
 * bank_sceneMaskVoiceEdit() are the active-Scene convenience wrappers.
 */
void bank_setSceneMaskVoiceEditForScene(uint8_t scene_index, uint16_t mask);
uint16_t bank_sceneMaskVoiceEditForScene(uint8_t scene_index);
```

---

### Change A11 — Autosave.h: add width constant

**File**: `Core/Bank/Scene/Autosave.h`
**After**: line 158 (`(AUTOSAVE_BANK_OFFSET + 13u)`)
**Action**: ADD

```c
/*
 * Per-Scene voice-edit mask region width (bytes).
 *
 * What: BANK_SCENE_SLOT_COUNT × 2 = 32 bytes. One little-endian uint16_t per
 * Scene, starting at AUTOSAVE_BANK_VOICE_EDIT_MASK_OFFSET. The full 32 bytes
 * fit within AUTOSAVE_BANK_SECTION_BYTES (128). Byte layout: offset 13 =
 * Scene 0 low, 14 = Scene 0 high, 15 = Scene 1 low, ..., 44 = Scene 15 high.
 *
 * Clients: autosave_markBankFieldDirty() uses this as the dirty-region width.
 * autosave_getLivePayloadByte() uses it to bound the offset range.
 * autosave_applyBankPayload() uses it to know how many bytes to read.
 *
 * Affiliate: AUTOSAVE_BANK_VOICE_EDIT_MASK_OFFSET (byte 13 within Bank).
 */
#define AUTOSAVE_BANK_VOICE_EDIT_MASK_BYTES \
    (BANK_SCENE_SLOT_COUNT * 2u)
```

---

### Change A12 — Autosave.c: dirty marking width

**File**: `Core/Bank/Scene/Autosave.c`
**Line**: 1475
**Action**: MODIFY

**Current**:
```c
        width = 2u;
```

**New**:
```c
        /*
         * Mark all 32 bytes of the per-Scene voice-edit mask region dirty.
         *
         * Inputs: field enum AUTOSAVE_BANK_FIELD_VOICE_EDIT_MASK. Output: 32
         * consecutive payload bits are set in the mutation bitmap so the drain
         * copy-forwards all 16 Scene masks. Each single-Scene mask change dirties
         * the entire region because the bitmap lacks sub-field granularity. The
         * amortized cost is acceptable: 32 bytes within a 128-byte Bank section.
         *
         * Affiliate: autosave_getLivePayloadByte() reads the same region during
         * drain; both must agree on offset and width.
         */
        width = AUTOSAVE_BANK_VOICE_EDIT_MASK_BYTES;
```

---

### Change A13 — Autosave.c: live payload getter expansion

**File**: `Core/Bank/Scene/Autosave.c`
**Lines**: 968–973
**Action**: MODIFY

**Current**:
```c
        if (payload_offset >= 13u && payload_offset < 15u) {
            bank_value = bank_sceneMaskVoiceEdit();
            *value = autosave_u16Byte(
                bank_value, (uint8_t)(payload_offset - 13u));
            return 1u;
        }
```

**New**:
```c
        /*
         * Read one byte from the per-Scene voice-edit mask region.
         *
         * Inputs: payload_offset in the range [13, 13 + 32). Output: the low or
         * high byte of one Scene's mask. Scene index = (offset - 13) / 2, byte
         * position = (offset - 13) % 2. Uses bank_sceneMaskVoiceEditForScene()
         * so the drain captures each Scene's mask independently.
         *
         * Affiliate: autosave_markBankFieldDirty() VOICE_EDIT_MASK case dirties
         * offsets 13..44. autosave_applyBankPayload() reads the same layout.
         */
        if (payload_offset >= 13u &&
            payload_offset < (13u + AUTOSAVE_BANK_VOICE_EDIT_MASK_BYTES)) {
            uint8_t relative = (uint8_t)(payload_offset - 13u);
            bank_value = bank_sceneMaskVoiceEditForScene(
                (uint8_t)(relative / 2u));
            *value = autosave_u16Byte(bank_value, (uint8_t)(relative % 2u));
            return 1u;
        }
```

---

### Change A14 — Autosave.c: bank payload apply expansion

**File**: `Core/Bank/Scene/Autosave.c`
**Lines**: 1196–1198
**Action**: MODIFY

**Current**:
```c
    bank_value = (uint16_t)(bank_section[13u] |
                            ((uint16_t)bank_section[14u] << 8u));
    bank_setSceneMaskVoiceEdit(bank_value);
```

**New**:
```c
    /*
     * Restore per-Scene voice-edit masks from the Bank payload.
     *
     * Inputs: 32 bytes at offset 13, two little-endian bytes per Scene.
     * Output: each Scene's mask is set independently via
     * bank_setSceneMaskVoiceEditForScene(). The setter normalizes the mask and
     * enforces the active-bit invariant when the index matches the active Scene.
     *
     * Old-format records: bytes beyond 14 are zero-filled by the initial record
     * create. No backward compatibility is required — old Autosave files are
     * disposed (V1 resolution).
     *
     * Affiliate: autosave_getLivePayloadByte() produces the same layout during
     * drain. Bank section byte 12 (active_scene) must be applied before this
     * loop so the invariant enforcer acts on the correct active Scene.
     */
    {
        uint8_t scene_i;
        for (scene_i = 0u; scene_i < BANK_SCENE_SLOT_COUNT; scene_i++) {
            uint8_t off = (uint8_t)(13u + scene_i * 2u);
            bank_value = (uint16_t)(bank_section[off] |
                                    ((uint16_t)bank_section[off + 1u] << 8u));
            bank_setSceneMaskVoiceEditForScene(scene_i, bank_value);
        }
    }
```

---

### Change A15 — storageTypes.h: bankset struct per-Scene mask

**File**: `Core/Hardware/SD/storageTypes.h`
**Lines**: 239–241
**Action**: MODIFY

**Current**:
```c
    uint8_t seen_scene_mask_voice_edit;
    uint8_t active_scene;
    uint16_t scene_mask_voice_edit;
```

**New**:
```c
    /*
     * Per-Scene voice-edit mask parse/write state.
     *
     * What: seen_scene_mask_voice_edit is a 16-bit field with one bit per Scene;
     * bit i is set when scene_mask_voice_edit_NN=XXXX for Scene i has been parsed.
     * scene_mask_voice_edit[] holds one uint16_t per Scene.
     *
     * Inputs: the parser sets individual entries and seen bits from per-Scene
     * keys (scene_mask_voice_edit_NN). The legacy single-key path
     * (scene_mask_voice_edit=XXXX) sets all 16 entries and all 16 seen bits for
     * backward compatibility with old bankset.bcg files.
     *
     * Outputs: filesystem.c load sites read seen bits and per-Scene masks.
     * The writer emits 16 per-Scene lines; old firmware ignores the new keys.
     *
     * Affiliate: BankData.c bank_scene_mask_voice_edit[BANK_SCENE_SLOT_COUNT],
     * Autosave.c bank payload layout bytes 13..44.
     */
    uint16_t seen_scene_mask_voice_edit;
    uint8_t active_scene;
    uint16_t scene_mask_voice_edit[BANK_SCENE_SLOT_COUNT];
```

---

### Change A16 — storageTypes.c: parser per-Scene key expansion

**File**: `Core/Hardware/SD/storageTypes.c`
**Lines**: 1186–1199
**Action**: MODIFY

**Current**:
```c
    else if (storage_streq(key,"scene_mask_voice_edit")) {
        uint16_t value16 = 0u; uint8_t n = 0u; uint8_t digits = 0u;
        if (value[0] == '0' && (value[1] == 'x' || value[1] == 'X'))
            n = 2u;
        while (value[n] != '\0' && digits < 4u) {
            int8_t digit = storage_patternHex(value[n++]);
            if (digit < 0) return STORAGE_STATUS_BAD_VALUE;
            value16 = (uint16_t)((value16 << 4u) | (uint8_t)digit);
            digits++;
        }
        if (value[n] != '\0' || digits == 0u) return STORAGE_STATUS_BAD_VALUE;
        state->scene_mask_voice_edit = value16;
        state->seen_scene_mask_voice_edit = 1u;
    }
```

**New**:
```c
    /*
     * Parse per-Scene or legacy voice-edit mask from bankset.bcg.
     *
     * Two key forms are accepted:
     *   scene_mask_voice_edit=XXXX       — legacy: sets all 16 Scene entries.
     *   scene_mask_voice_edit_NN=XXXX    — per-Scene: sets entry NN (0..15).
     *
     * Inputs: key/value from the bankset line parser. Output: one or all
     * entries in state->scene_mask_voice_edit[] are set, and the corresponding
     * seen bits in state->seen_scene_mask_voice_edit are raised. The hex parse
     * is shared between both key forms.
     *
     * Backward compatibility: old bankset.bcg files contain only the legacy
     * key. New files emitted by A17 contain 16 per-Scene keys. Old firmware
     * loading a new file ignores the per-Scene keys and falls back to its
     * single-key default.
     *
     * Affiliate: storage_formatBanksetLine() emits the per-Scene form.
     * filesystem.c load sites read state->seen_scene_mask_voice_edit per-bit.
     */
    else if (storage_streq(key,"scene_mask_voice_edit") ||
             (key[0]=='s' && key[1]=='c' && key[2]=='e' && key[3]=='n' &&
              key[4]=='e' && key[5]=='_' && key[6]=='m' && key[7]=='a' &&
              key[8]=='s' && key[9]=='k' && key[10]=='_' && key[11]=='v' &&
              key[12]=='o' && key[13]=='i' && key[14]=='c' && key[15]=='e' &&
              key[16]=='_' && key[17]=='e' && key[18]=='d' && key[19]=='i' &&
              key[20]=='t' && key[21]=='_')) {
        uint16_t value16 = 0u; uint8_t n = 0u; uint8_t digits = 0u;
        uint8_t scene_i;
        uint8_t is_per_scene = (key[21] == '_') ? 1u : 0u;
        if (value[0] == '0' && (value[1] == 'x' || value[1] == 'X'))
            n = 2u;
        while (value[n] != '\0' && digits < 4u) {
            int8_t digit = storage_patternHex(value[n++]);
            if (digit < 0) return STORAGE_STATUS_BAD_VALUE;
            value16 = (uint16_t)((value16 << 4u) | (uint8_t)digit);
            digits++;
        }
        if (value[n] != '\0' || digits == 0u) return STORAGE_STATUS_BAD_VALUE;
        if (is_per_scene) {
            storage_status_t sst = storage_parseU8(&key[22], &scene_i);
            if (sst != STORAGE_STATUS_OK || scene_i >= BANK_SCENE_SLOT_COUNT)
                return STORAGE_STATUS_BAD_VALUE;
            state->scene_mask_voice_edit[scene_i] = value16;
            state->seen_scene_mask_voice_edit |= (uint16_t)(1u << scene_i);
        } else {
            for (scene_i = 0u; scene_i < BANK_SCENE_SLOT_COUNT; scene_i++)
                state->scene_mask_voice_edit[scene_i] = value16;
            state->seen_scene_mask_voice_edit = 0xffffu;
        }
    }
```

**Implementation note**: the char-by-char prefix test above is a pattern match
approach. At implementation time, a cleaner alternative is to use
`memcmp(key, "scene_mask_voice_edit", 21u) == 0` and then check `key[21]`:
if `'\0'` it's legacy, if `'_'` it's per-Scene. Either form is acceptable;
the implementation should use whichever is most consistent with the existing
parser style in this file.

---

### Change A17 — storageTypes.c: writer per-Scene lines

**File**: `Core/Hardware/SD/storageTypes.c`
**Lines**: 1209–1218
**Action**: MODIFY

**Current**:
```c
    if(line_index==3u) {
        static const char hex[]="0123456789abcdef";
        if (capacity < 28u) return 0u;
        memcpy(dst,"scene_mask_voice_edit=",22u);
        dst[22]=hex[(state->scene_mask_voice_edit >> 12u)&15u];
        dst[23]=hex[(state->scene_mask_voice_edit >> 8u)&15u];
        dst[24]=hex[(state->scene_mask_voice_edit >> 4u)&15u];
        dst[25]=hex[state->scene_mask_voice_edit&15u]; dst[26]='\n'; dst[27]='\0'; return 27u;
    }
    return 0u;
```

**New**:
```c
    /*
     * Emit one per-Scene voice-edit mask line.
     *
     * Lines 3..18 map to Scenes 0..15. Key format: "scene_mask_voice_edit_NN"
     * where NN is the decimal Scene index. Value: 4-digit lowercase hex.
     * The buffer needs at most 25 + 1 + 4 + 1 + 1 = 32 bytes
     * ("scene_mask_voice_edit_15=XXXX\n\0"). capacity >= 34 covers it.
     *
     * Inputs: state->scene_mask_voice_edit[scene_index]. Output: one formatted
     * line. Returns 0 when line_index > 18, terminating the self-terminating
     * writer loop in filesystem_nextBanksetLine().
     *
     * Affiliate: storage_banksetParseLine() reads both per-Scene and legacy
     * keys. Old firmware ignores the per-Scene keys.
     */
    if (line_index >= 3u && line_index < (3u + BANK_SCENE_SLOT_COUNT)) {
        static const char hex[]="0123456789abcdef";
        uint8_t scene_i = (uint8_t)(line_index - 3u);
        uint16_t mask = state->scene_mask_voice_edit[scene_i];
        uint8_t len;
        if (capacity < 34u) return 0u;
        memcpy(dst, "scene_mask_voice_edit_", 22u);
        if (scene_i >= 10u) {
            dst[22] = (char)('0' + scene_i / 10u);
            dst[23] = (char)('0' + scene_i % 10u);
            len = 24u;
        } else {
            dst[22] = (char)('0' + scene_i);
            len = 23u;
        }
        dst[len++] = '=';
        dst[len++] = hex[(mask >> 12u) & 15u];
        dst[len++] = hex[(mask >>  8u) & 15u];
        dst[len++] = hex[(mask >>  4u) & 15u];
        dst[len++] = hex[ mask         & 15u];
        dst[len++] = '\n';
        dst[len]   = '\0';
        return len;
    }
    return 0u;
```

---

### Change A18 — storageTypes.c: bankset init zero-fill

**File**: `Core/Hardware/SD/storageTypes.c`
**Action**: VERIFY — `storage_banksetInit()` (or equivalent) must zero-fill or
memset the `storage_bankset_t` struct including the new 32-byte array. Verify
that the existing init clears the entire struct. If it uses `memset(state, 0,
sizeof(*state))` then no change is needed. If it initializes fields
individually, add the per-Scene array init loop.

---

### Change A19 — filesystem.c: load site 1 (empty-Bank identity load)

**File**: `Core/Hardware/SD/filesystem.c`
**Line**: 13907
**Action**: MODIFY

**Current**:
```c
            bank_setSceneMaskVoiceEdit(op_bankset_state.scene_mask_voice_edit);
```

**New**:
```c
            /*
             * Restore per-Scene voice-edit masks from the parsed bankset.
             *
             * Inputs: op_bankset_state.scene_mask_voice_edit[] populated by
             * storage_banksetParseLine(). Only Scenes whose seen bit is set
             * are applied; unseen Scenes keep their bank_init() defaults.
             * Output: each seen Scene's mask is set via the indexed setter.
             *
             * Affiliate: bank_selectActiveSceneForEditMask() (line above) has
             * already set the active Scene, so the invariant enforcer in the
             * setter acts on the correct slot.
             */
            {
                uint8_t mask_i;
                for (mask_i = 0u; mask_i < BANK_SCENE_SLOT_COUNT; mask_i++) {
                    if (op_bankset_state.seen_scene_mask_voice_edit &
                        (uint16_t)(1u << mask_i))
                        bank_setSceneMaskVoiceEditForScene(
                            mask_i,
                            op_bankset_state.scene_mask_voice_edit[mask_i]);
                }
            }
```

---

### Change A20 — filesystem.c: load site 2 (non-empty Bank Load)

**File**: `Core/Hardware/SD/filesystem.c`
**Line**: 14090
**Action**: MODIFY — same pattern as Change A19.

Replace:
```c
        bank_setSceneMaskVoiceEdit(op_bankset_state.scene_mask_voice_edit);
```

With the same per-Scene seen-gated loop as A19.

---

### Change A21 — filesystem.c: load site 3 (Bank Save commit)

**File**: `Core/Hardware/SD/filesystem.c`
**Line**: 18726
**Action**: MODIFY — same pattern as Change A19.

Replace:
```c
        bank_setSceneMaskVoiceEdit(op_bankset_state.scene_mask_voice_edit);
```

With the same per-Scene seen-gated loop as A19.

---

### Change A22 — filesystem.c: load site 4 (Autosave reader Bank commit)

**File**: `Core/Hardware/SD/filesystem.c`
**Line**: 27385
**Action**: MODIFY — same pattern as Change A19.

Replace:
```c
    bank_setSceneMaskVoiceEdit(op_bankset_state.scene_mask_voice_edit);
```

With the same per-Scene seen-gated loop as A19.

---

### Change A23 — filesystem.c: save site (Bank Save capture)

**File**: `Core/Hardware/SD/filesystem.c`
**Lines**: 29569–29573
**Action**: MODIFY

**Current**:
```c
    op_bankset_state.scene_mask_voice_edit = bank_sceneMaskVoiceEdit();
    op_bankset_state.seen_format = 1u;
    op_bankset_state.seen_version = 1u;
    op_bankset_state.seen_active_scene = 1u;
    op_bankset_state.seen_scene_mask_voice_edit = 1u;
```

**New**:
```c
    /*
     * Capture all 16 per-Scene voice-edit masks for Bank Save.
     *
     * Inputs: bank_sceneMaskVoiceEditForScene(i) reads each Scene's mask from
     * BankData. Output: op_bankset_state carries all 16 masks for the bankset
     * writer. All 16 seen bits are set because the save always emits all Scenes.
     *
     * Affiliate: storage_formatBanksetLine() reads these during the
     * self-terminating writer loop.
     */
    {
        uint8_t cap_i;
        for (cap_i = 0u; cap_i < BANK_SCENE_SLOT_COUNT; cap_i++)
            op_bankset_state.scene_mask_voice_edit[cap_i] =
                bank_sceneMaskVoiceEditForScene(cap_i);
    }
    op_bankset_state.seen_format = 1u;
    op_bankset_state.seen_version = 1u;
    op_bankset_state.seen_active_scene = 1u;
    op_bankset_state.seen_scene_mask_voice_edit = 0xffffu;
```

---

### Change A24 — presetManager.c: morph rebuild on Scene switch

**File**: `Core/Bank/Scene/Preset/presetManager.c`
**Line**: 1139 (after `preset_syncSceneMorphMirrors(scene);`)
**Action**: ADD

```c
    /*
     * Queue a bounded Morph rebuild so the DSP converges to the new Scene's
     * per-voice morph amounts within the foreground tick budget.
     *
     * Inputs: the active Scene index whose morph mirrors were just synchronized.
     * Output: all 6 voice slots are queued for the bounded morph worker. This
     * closes the timing gap between mirror sync and the deferred slot worker: the
     * morph worker starts interpolation immediately, while the deferred worker
     * commits instrument images one slot at a time.
     *
     * The morph worker reads from scene->kit.instruments[slot] (the new Scene's
     * images) and gates runtime writes on scene_index == scene_getActiveIndex().
     * If both Scenes share an instrument type in a slot, writes are correct
     * immediately. The deferred worker's per-slot presetMorph_applyVoiceNow()
     * overrides any bounded-worker intermediate state when it commits that slot.
     *
     * Affiliate: preset_startDrumsetApply() calls preset_applySceneSettings()
     * before the deferred worker begins. presetMorph_rebuildScene() queues all
     * slots via presetMorph_requestAll(). See V6 in the session plan.
     */
    presetMorph_rebuildScene(scene_index);
```

---

## Part B — Base-Independent LFO Voice-Morph Contribution

### Change B1 — presetMorphEngine.h: add direction enum

**File**: `Core/Bank/Scene/Preset/presetMorphEngine.h`
**After**: line 4 (`#include <stdint.h>`)
**Action**: ADD

```c
/*
 * LFO voice-morph contribution direction.
 *
 * What: replaces the active/amount pair with a direction/depth pair that is
 * base-independent. The direction tells the resolver which endpoint to move
 * toward; depth is a 0..255 normalized distance. The resolver computes the
 * actual delta from the current effective base at resolution time.
 *
 * NONE: no active contribution (equivalent to old active=0).
 * MAIN: move toward the main endpoint (morph amount 0). At depth 255 the
 *   effective amount reaches 0 regardless of base.
 * MORPH: move toward the full-morph endpoint (morph amount 255). At depth 255
 *   the effective amount reaches 255 regardless of base.
 *
 * Inputs: InstrumentManager maps LFO polarity/amount/source to direction and
 * depth. Output: presetMorph_resolveLfoAmount() uses direction and the
 * current effective base to compute a signed delta.
 *
 * Affiliate: mod_node_polarity_t (modulationNode.h:67-71) defines the
 * polarity constants that InstrumentManager maps from. preset_morph_lfo_
 * contribution_t uses this enum for its direction field.
 */
typedef enum {
    PRESET_MORPH_LFO_DIRECTION_NONE  = 0,
    PRESET_MORPH_LFO_DIRECTION_MAIN  = 1,
    PRESET_MORPH_LFO_DIRECTION_MORPH = 2
} PresetMorphLfoDirection;
```

---

### Change B2 — presetMorphEngine.h: change setter signature

**File**: `Core/Bank/Scene/Preset/presetMorphEngine.h`
**Lines**: 60–77 (comment block and declaration of `presetMorph_setVoiceLfoModulation`)
**Action**: MODIFY

**Current declaration** (line 72–77):
```c
void presetMorph_setVoiceLfoModulation(uint8_t scene_index,
                                       uint8_t target_slot,
                                       uint8_t source_slot,
                                       uint8_t target_pair,
                                       uint8_t active,
                                       uint8_t amount);
```

**New declaration**:
```c
/*
 * Store one hidden LFO Morph contribution as base-independent direction+depth.
 *
 * Inputs: resident Scene index, target voice slot, source LFO slot, target
 * pair index, direction (NONE/MAIN/MORPH), and normalized depth (0..255).
 * Output: the contribution table is updated and the target voice is queued for
 * bounded Morph apply. This does not touch SceneData or PERF mirrors.
 *
 * Direction+depth replaces the former active+amount contract. The old contract
 * stored an absolute shaped Morph amount that the resolver converted to a
 * delta from the base. The new contract stores the direction the LFO wants to
 * push and how far (normalized), so the resolver computes the delta from the
 * current effective base at resolution time. This eliminates the stale-base
 * bug when step automation changes the base between LFO sample and resolve.
 *
 * Clients: instrumentManager_updateLfoSceneDestination() VOICE_MORPH case.
 * Affiliate: presetMorph_resolveLfoAmount() consumes the stored direction
 * and depth. presetMorph_clearLfoSource() stores {NONE, 0}.
 */
void presetMorph_setVoiceLfoModulation(uint8_t scene_index,
                                       uint8_t target_slot,
                                       uint8_t source_slot,
                                       uint8_t target_pair,
                                       PresetMorphLfoDirection direction,
                                       uint8_t depth);
```

---

### Change B3 — presetMorphEngine.c: change contribution struct

**File**: `Core/Bank/Scene/Preset/presetMorphEngine.c`
**Lines**: 22–25
**Action**: MODIFY

**Current**:
```c
typedef struct {
    uint8_t active;
    uint8_t amount;
} preset_morph_lfo_contribution_t;
```

**New**:
```c
/*
 * One LFO voice-morph contribution: base-independent direction and depth.
 *
 * What: replaces the former {active, amount} pair. Same 2 bytes per entry,
 * zero additional RAM. The direction field doubles as the active indicator:
 * PRESET_MORPH_LFO_DIRECTION_NONE means inactive.
 *
 * Inputs: presetMorph_setVoiceLfoModulation() writes direction and depth from
 * InstrumentManager's polarity/amount/source encoding. Output: the resolver
 * (presetMorph_resolveLfoAmount) reads direction and depth and computes a
 * signed delta from the current effective base.
 *
 * Affiliate: PresetMorphLfoDirection (presetMorphEngine.h), the contribution
 * array morph_lfo_contributions[6][6][2] (144 bytes, unchanged).
 */
typedef struct {
    uint8_t direction;
    uint8_t depth;
} preset_morph_lfo_contribution_t;
```

---

### Change B4 — presetMorphEngine.c: update has-LFO-layer check

**File**: `Core/Bank/Scene/Preset/presetMorphEngine.c`
**Line**: 127
**Action**: MODIFY

**Current**:
```c
            if (morph_lfo_contributions[slot][source][pair].active)
```

**New**:
```c
            if (morph_lfo_contributions[slot][source][pair].direction !=
                PRESET_MORPH_LFO_DIRECTION_NONE)
```

---

### Change B5 — presetMorphEngine.c: rewrite LFO resolver

**File**: `Core/Bank/Scene/Preset/presetMorphEngine.c`
**Lines**: 134–182 (entire `presetMorph_resolveLfoAmount()` body)
**Action**: MODIFY

Replace the resolver body. The function signature stays the same.

**Current inner loop** (lines 169–175):
```c
        for (pair = 0u; pair < 2u; pair++) {
            const preset_morph_lfo_contribution_t *contribution =
                &morph_lfo_contributions[slot][source][pair];
            if (contribution->active)
                effective += (int32_t)contribution->amount - base;
        }
```

**New inner loop**:
```c
        for (pair = 0u; pair < 2u; pair++) {
            const preset_morph_lfo_contribution_t *contribution =
                &morph_lfo_contributions[slot][source][pair];
            /*
             * Compute a signed delta from the current effective base.
             *
             * Inputs: contribution direction (NONE/MAIN/MORPH) and normalized
             * depth (0..255). Base is the effective center — step-automation
             * overlay when active, otherwise the retained Scene amount.
             *
             * MORPH direction: delta = (255 - base) * depth / 255, pushing
             *   the effective value toward 255 (full morph endpoint).
             * MAIN direction: delta = -(base * depth / 255), pushing toward 0
             *   (main endpoint).
             * NONE: inactive, no contribution.
             *
             * The +127 rounding bias was verified correct at boundaries:
             *   base=0, depth=255 → MORPH delta = 255
             *   base=255, depth=255 → MAIN delta = -255
             *   base=128, depth=128 → MORPH delta ≈ 64
             *
             * Affiliate: InstrumentManager encodes polarity/amount/source into
             * direction+depth without reading the base (Change B7).
             */
            if (contribution->direction ==
                PRESET_MORPH_LFO_DIRECTION_MORPH)
                effective += ((int32_t)(255u - (uint8_t)base) *
                              contribution->depth + 127) / 255;
            else if (contribution->direction ==
                     PRESET_MORPH_LFO_DIRECTION_MAIN)
                effective -= ((int32_t)(uint8_t)base *
                              contribution->depth + 127) / 255;
        }
```

The outer structure (base selection from step override or Scene, and final
clamping to 0..255) is unchanged.

**Updated comment block for the function** (lines 141–155):
```c
    /*
     * Resolve the effective Morph amount for one LFO-modulated voice.
     *
     * Inputs: current Scene and target voice slot. Output: retained base Morph
     * plus all active direction+depth contributions, clamped to 0..255. Each
     * contribution stores a base-independent direction and normalized depth.
     * The resolver computes the delta from the current effective base at
     * resolution time, so step-automation base changes are reflected immediately
     * without waiting for a new LFO sample.
     *
     * This cannot be folded into every descriptor interpolation because that
     * would recalculate the same LFO/base combination for every morphable
     * parameter in the voice. It also cannot run in LFO dispatch because
     * dispatch must not walk descriptor tables or update all morphed
     * parameters immediately.
     */
```

---

### Change B6 — presetMorphEngine.c: update init and clear paths

**File**: `Core/Bank/Scene/Preset/presetMorphEngine.c`
**Lines**: 261–265 (init loop)
**Action**: MODIFY

**Current**:
```c
    for (uint8_t target = 0u; target < INSTRUMENT_SLOT_COUNT; target++) {
        for (uint8_t source = 0u; source < INSTRUMENT_SLOT_COUNT; source++) {
            for (uint8_t pair = 0u; pair < 2u; pair++) {
                morph_lfo_contributions[target][source][pair].active = 0u;
                morph_lfo_contributions[target][source][pair].amount = 0u;
```

**New**:
```c
    for (uint8_t target = 0u; target < INSTRUMENT_SLOT_COUNT; target++) {
        for (uint8_t source = 0u; source < INSTRUMENT_SLOT_COUNT; source++) {
            for (uint8_t pair = 0u; pair < 2u; pair++) {
                morph_lfo_contributions[target][source][pair].direction =
                    PRESET_MORPH_LFO_DIRECTION_NONE;
                morph_lfo_contributions[target][source][pair].depth = 0u;
```

Both fields are zero, same as before. Semantics change from active/amount to
direction/depth.

**File**: `Core/Bank/Scene/Preset/presetMorphEngine.c`
**Lines**: 653–655 (`presetMorph_clearLfoSource()` inner loop)
**Action**: MODIFY

**Current**:
```c
            morph_lfo_contributions[target][source_slot][target_pair].active = 0u;
            morph_lfo_contributions[target][source_slot][target_pair].amount = 0u;
```

**New**:
```c
            morph_lfo_contributions[target][source_slot][target_pair].direction =
                PRESET_MORPH_LFO_DIRECTION_NONE;
            morph_lfo_contributions[target][source_slot][target_pair].depth = 0u;
```

---

### Change B7 — presetMorphEngine.c: update setter body

**File**: `Core/Bank/Scene/Preset/presetMorphEngine.c`
**Lines**: 607–634 (entire `presetMorph_setVoiceLfoModulation()`)
**Action**: MODIFY

**New signature**:
```c
void presetMorph_setVoiceLfoModulation(uint8_t scene_index,
                                       uint8_t target_slot,
                                       uint8_t source_slot,
                                       uint8_t target_pair,
                                       PresetMorphLfoDirection direction,
                                       uint8_t depth)
```

**New body** (replace lines 614–633):
```c
    /*
     * Store one hidden LFO Morph contribution and queue its target voice.
     *
     * Inputs: Scene index, target voice slot, source LFO slot, target pair,
     * direction (NONE/MAIN/MORPH), and normalized depth (0..255). Output: the
     * contribution table is updated and the target voice is queued for bounded
     * Morph apply. This does not touch SceneData or PERF mirrors, preserving
     * the difference between retained base Morph and the invisible LFO layer.
     *
     * Direction replaces the former active flag: NONE means inactive.
     * Depth replaces the former absolute shaped amount.
     */
    if (!scene_getConst(scene_index) ||
        target_slot >= INSTRUMENT_SLOT_COUNT ||
        source_slot >= INSTRUMENT_SLOT_COUNT ||
        target_pair > 1u) {
        return;
    }
    morph_lfo_contributions[target_slot][source_slot][target_pair].direction =
        (uint8_t)direction;
    morph_lfo_contributions[target_slot][source_slot][target_pair].depth =
        depth;
    presetMorph_requestVoice(scene_index, target_slot);
```

---

### Change B8 — InstrumentManager.c: polarity-to-direction encoding

**File**: `Core/DSP/Instruments/InstrumentManager.c`
**Lines**: 2323–2337 (`SCENE_MOD_TARGET_KIND_VOICE_MORPH` case)
**Action**: MODIFY

**Current**:
```c
    case SCENE_MOD_TARGET_KIND_VOICE_MORPH:
        /*
         * Shape LFO motion around the effective step-automation center when
         * one is active; otherwise use the retained Scene base as before.
         */
        base = presetMorph_getEffectiveVoiceAmount(
            scene_getActiveIndex(), descriptor->voice_slot);
        shaped = modNode_shapeRangeU16(base, descriptor->min_value,
                                       descriptor->max_value,
                                       lfo_value_0_1, amount, polarity);
        presetMorph_setVoiceLfoModulation(scene_getActiveIndex(),
                                          descriptor->voice_slot,
                                          source_slot, target_pair,
                                          1u, (uint8_t)shaped);
        return 1u;
```

**New**:
```c
    case SCENE_MOD_TARGET_KIND_VOICE_MORPH: {
        /*
         * Encode LFO polarity/amount/source as base-independent direction+depth.
         *
         * Inputs: normalized LFO value (0..1), polarity enum, and normalized
         * amount (0..1). Output: a PresetMorphLfoDirection and uint8_t depth
         * stored in the contribution table. The effective base is no longer
         * read at this site — the resolver applies it at resolution time.
         *
         * Mapping from polarity + LFO source to signed depth:
         *   POSITIVE: signed_depth = +amount × source        → toward morph(255)
         *   NEGATIVE: signed_depth = -amount × (1 - source)  → toward main(0)
         *   BIPOLAR:  signed_depth = +amount × (2·source - 1) → both sides
         *
         * The sign of signed_depth determines direction:
         *   positive → MORPH (toward 255), negative → MAIN (toward 0),
         *   zero → NONE (inactive).
         *
         * This matches the existing modNode_shapeRangeU16() behavior for
         * voice-morph targets where min=0, max=255. The key difference is that
         * the base is not baked into the stored value.
         *
         * Affiliate: presetMorph_resolveLfoAmount() consumes direction+depth
         * with the current effective base. presetMorph_clearLfoSource() clears
         * contributions when the LFO target changes.
         */
        float signed_depth;
        PresetMorphLfoDirection dir;
        uint8_t depth_u8;

        switch (polarity) {
        case MOD_NODE_POLARITY_POSITIVE:
            signed_depth = amount * lfo_value_0_1;
            break;
        case MOD_NODE_POLARITY_NEGATIVE:
            signed_depth = -(amount * (1.0f - lfo_value_0_1));
            break;
        case MOD_NODE_POLARITY_BIPOLAR:
        default:
            signed_depth = amount * (2.0f * lfo_value_0_1 - 1.0f);
            break;
        }
        if (signed_depth > 0.001f) {
            dir = PRESET_MORPH_LFO_DIRECTION_MORPH;
            depth_u8 = (uint8_t)(signed_depth * 255.0f + 0.5f);
        } else if (signed_depth < -0.001f) {
            dir = PRESET_MORPH_LFO_DIRECTION_MAIN;
            depth_u8 = (uint8_t)((-signed_depth) * 255.0f + 0.5f);
        } else {
            dir = PRESET_MORPH_LFO_DIRECTION_NONE;
            depth_u8 = 0u;
        }
        presetMorph_setVoiceLfoModulation(scene_getActiveIndex(),
                                          descriptor->voice_slot,
                                          source_slot, target_pair,
                                          dir, depth_u8);
        return 1u;
    }
```

**Note**: The `base` and `shaped` local variables declared at the top of the
function (line 2309–2310) are still used by other cases (DECIMATION_ALL etc.)
and should not be removed.

---

## Part C — Scene Superpage Live Display, Immediate Underline, Boot State

### Change C1 — presetManager.c: add audio out step-override table

**File**: `Core/Bank/Scene/Preset/presetManager.c`
**After**: the existing `#include` block (near top of file)
**Action**: ADD (static storage at file scope, near other static state)

```c
/*
 * Per-voice step-automation audio-out override.
 *
 * What: 6 bytes SRAM. One entry per instrument slot. When active, the
 * effective audio-out route is the override value; otherwise the effective
 * route is the retained Scene setting.
 *
 * Inputs: seq_applySceneAutomation() AUDIO_OUT case writes the override via
 * preset_setAudioOutStepOverride(). Transport-boundary restore clears all
 * overrides via preset_clearAllAudioOutStepOverrides().
 *
 * Outputs: preset_getEffectiveAudioOut() returns the override when active,
 * else the Scene setting. The Scene superpage display reads this getter to
 * show the live effective value during playback.
 *
 * Lifetime: static runtime state only; cleared at boot and transport restore.
 * Never serialized. Does not modify SceneData or Autosave.
 *
 * Affiliate: morph_step_override[] in presetMorphEngine.c is the equivalent
 * pattern for voice morph. preset_applyVoiceAudioOutRuntime() applies the DSP
 * routing independently.
 */
static struct {
    uint8_t active;
    uint8_t route;
} audio_out_step_override[INSTRUMENT_SLOT_COUNT];
```

---

### Change C2 — presetManager.c: add FX send step-override table

**File**: `Core/Bank/Scene/Preset/presetManager.c`
**After**: Change C1
**Action**: ADD

```c
/*
 * Per-voice step-automation FX-send override.
 *
 * What: 6 bytes SRAM. One entry per instrument slot. When active, the
 * effective FX-send amount is the override value; otherwise the effective
 * amount is the retained Scene setting.
 *
 * Inputs: seq_applySceneAutomation() FX_SEND case writes the override via
 * preset_setFxSendStepOverride(). Transport-boundary restore clears all
 * overrides via preset_clearAllFxSendStepOverrides().
 *
 * Outputs: preset_getEffectiveFxSendAmount() returns the override when
 * active, else the Scene setting. The Scene superpage display reads this
 * getter to show the live effective value during playback.
 *
 * Lifetime: static runtime state only; cleared at boot and transport restore.
 * Never serialized. Does not modify SceneData or Autosave.
 *
 * Affiliate: morph_step_override[] in presetMorphEngine.c is the equivalent
 * pattern for voice morph.
 */
static struct {
    uint8_t active;
    uint8_t amount;
} fx_send_step_override[INSTRUMENT_SLOT_COUNT];
```

---

### Change C3 — presetManager.c: add audio out step-override set/clear/get

**File**: `Core/Bank/Scene/Preset/presetManager.c`
**After**: `preset_applyVoiceAudioOutRuntime()` (line 1031)
**Action**: ADD

```c
void preset_setAudioOutStepOverride(uint8_t slot, uint8_t route)
{
    /*
     * Set one voice's transient step-automation audio-out override.
     *
     * Inputs: zero-based instrument slot and route value in the mixer enum
     * domain. Output: the override table records the active route without
     * changing SceneData or Autosave. The DSP routing update is performed
     * separately by preset_applyVoiceAudioOutRuntime().
     *
     * Client: seq_applySceneAutomation() AUDIO_OUT case.
     * Restore: preset_clearAllAudioOutStepOverrides().
     */
    if (slot >= INSTRUMENT_SLOT_COUNT)
        return;
    audio_out_step_override[slot].active = 1u;
    audio_out_step_override[slot].route = route;
}

void preset_clearAllAudioOutStepOverrides(uint8_t scene_index)
{
    /*
     * Clear every transient step-automation audio-out override.
     *
     * Inputs: active Scene index (for restoring the Scene routing). Output:
     * override flags are cleared. No SceneData or Autosave write occurs.
     *
     * Client: seq_restoreAllSceneAutomation() (transport boundary).
     */
    uint8_t slot;
    for (slot = 0u; slot < INSTRUMENT_SLOT_COUNT; slot++) {
        audio_out_step_override[slot].active = 0u;
        audio_out_step_override[slot].route = 0u;
    }
}

uint8_t preset_getEffectiveAudioOut(uint8_t scene_index, uint8_t slot)
{
    /*
     * Read the effective audio-out route for one voice.
     *
     * Inputs: resident Scene index and zero-based voice slot. Output: the
     * active step-automation override route when present, otherwise the
     * retained Scene audio-out setting.
     *
     * Clients: menu_cellDisplayValue() SCENE_SETTING AUDIO_OUT case for the
     * superpage live display. This is a read-only bridge; it never changes
     * SceneData, Autosave, or DSP state.
     *
     * Affiliate: presetMorph_getEffectiveVoiceAmount() is the equivalent
     * pattern for voice morph in presetMorphEngine.c.
     */
    if (slot >= INSTRUMENT_SLOT_COUNT)
        return 0u;
    if (audio_out_step_override[slot].active)
        return audio_out_step_override[slot].route;
    return scene_getVoiceAudioOut(scene_index, slot);
}
```

---

### Change C4 — presetManager.c: add FX send step-override set/clear/get

**File**: `Core/Bank/Scene/Preset/presetManager.c`
**After**: Change C3
**Action**: ADD

```c
void preset_setFxSendStepOverride(uint8_t slot, uint8_t amount)
{
    /*
     * Set one voice's transient step-automation FX-send override.
     *
     * Inputs: zero-based instrument slot and 0..127 FX-send amount. Output:
     * the override table records the active amount without changing SceneData
     * or Autosave. No DSP runtime owner exists yet for FX send.
     *
     * Client: seq_applySceneAutomation() FX_SEND case.
     * Restore: preset_clearAllFxSendStepOverrides().
     */
    if (slot >= INSTRUMENT_SLOT_COUNT)
        return;
    fx_send_step_override[slot].active = 1u;
    fx_send_step_override[slot].amount = amount;
}

void preset_clearAllFxSendStepOverrides(void)
{
    /*
     * Clear every transient step-automation FX-send override.
     *
     * Inputs: none. Output: override flags are cleared. No SceneData or
     * Autosave write occurs. No DSP restore is needed because FX send has no
     * runtime owner yet.
     *
     * Client: seq_restoreAllSceneAutomation() (transport boundary).
     */
    uint8_t slot;
    for (slot = 0u; slot < INSTRUMENT_SLOT_COUNT; slot++) {
        fx_send_step_override[slot].active = 0u;
        fx_send_step_override[slot].amount = 0u;
    }
}

uint8_t preset_getEffectiveFxSendAmount(uint8_t scene_index, uint8_t slot)
{
    /*
     * Read the effective FX-send amount for one voice.
     *
     * Inputs: resident Scene index and zero-based voice slot. Output: the
     * active step-automation override amount when present, otherwise the
     * retained Scene FX-send setting.
     *
     * Clients: menu_cellDisplayValue() SCENE_SETTING FX_SEND case for the
     * superpage live display. This is a read-only bridge; it never changes
     * SceneData, Autosave, or DSP state.
     *
     * Affiliate: presetMorph_getEffectiveVoiceAmount() is the equivalent
     * pattern for voice morph in presetMorphEngine.c.
     */
    if (slot >= INSTRUMENT_SLOT_COUNT)
        return 0u;
    if (fx_send_step_override[slot].active)
        return fx_send_step_override[slot].amount;
    return scene_getVoiceFxSendAmount(scene_index, slot);
}
```

---

### Change C5 — presetManager.h: declare new step-override API

**File**: `Core/Bank/Scene/Preset/presetManager.h`
**After**: line 393 (`void preset_applyVoiceAudioOutRuntime(...)`)
**Action**: ADD

```c
/*
 * Audio-out step-automation override API.
 *
 * Inputs: set takes slot and route. clear takes Scene index for restore.
 * get takes Scene index and slot, returns the effective route.
 *
 * Clients: Sequencer step-automation apply/restore, Menu superpage display.
 * Affiliate: preset_applyVoiceAudioOutRuntime() handles the DSP routing
 * separately. These functions manage only the readable effective-value layer.
 */
void preset_setAudioOutStepOverride(uint8_t slot, uint8_t route);
void preset_clearAllAudioOutStepOverrides(uint8_t scene_index);
uint8_t preset_getEffectiveAudioOut(uint8_t scene_index, uint8_t slot);

/*
 * FX-send step-automation override API.
 *
 * Inputs: set takes slot and amount. clear takes no arguments (no DSP owner).
 * get takes Scene index and slot, returns the effective amount.
 *
 * Clients: Sequencer step-automation apply/restore, Menu superpage display.
 * Affiliate: no DSP runtime owner exists yet for FX send.
 */
void preset_setFxSendStepOverride(uint8_t slot, uint8_t amount);
void preset_clearAllFxSendStepOverrides(void);
uint8_t preset_getEffectiveFxSendAmount(uint8_t scene_index, uint8_t slot);
```

---

### Change C6 — sequencer.c: use audio out step override in apply

**File**: `Core/Sequencer/sequencer.c`
**Lines**: 831–833 (`SCENE_MOD_TARGET_KIND_AUDIO_OUT` case in `seq_applySceneAutomation()`)
**Action**: MODIFY

**Current**:
```c
	case SCENE_MOD_TARGET_KIND_AUDIO_OUT:
		preset_applyVoiceAudioOutRuntime(descriptor->voice_slot, value);
		break;
```

**New**:
```c
	case SCENE_MOD_TARGET_KIND_AUDIO_OUT:
		/*
		 * Apply audio-out step automation: both the DSP route and the readable
		 * step-override so the superpage can display the live effective value.
		 */
		preset_applyVoiceAudioOutRuntime(descriptor->voice_slot, value);
		preset_setAudioOutStepOverride(descriptor->voice_slot, value);
		break;
```

---

### Change C7 — sequencer.c: use FX send step override in apply

**File**: `Core/Sequencer/sequencer.c`
**Lines**: 834–836 (`SCENE_MOD_TARGET_KIND_FX_SEND` case in `seq_applySceneAutomation()`)
**Action**: MODIFY

**Current**:
```c
	case SCENE_MOD_TARGET_KIND_FX_SEND:
		/* No FX runtime owner exists yet, so there is no overlay to restore. */
		return 1u;
```

**New**:
```c
	case SCENE_MOD_TARGET_KIND_FX_SEND:
		/*
		 * Record the FX-send step-override so the superpage can display the
		 * live effective value. No DSP runtime owner exists yet for FX send.
		 */
		preset_setFxSendStepOverride(descriptor->voice_slot, value);
		break;
```

---

### Change C8 — sequencer.c: clear audio out and FX send overrides in restore

**File**: `Core/Sequencer/sequencer.c`
**Lines**: 287–288 (inside `seq_restoreAllSceneAutomation()`, after
`presetMorph_clearAllStepAutomationOverrides()` and
`instrumentManager_clearSlot6Track7StepDecayOverride()`)
**Action**: ADD

```c
    /*
     * Clear audio-out and FX-send step-automation overrides at transport
     * boundary so the effective-value getters fall back to retained Scene
     * settings. The audio-out DSP restore happens per-voice in the AUDIO_OUT
     * case below via preset_applyKitAudioRouting().
     */
    preset_clearAllAudioOutStepOverrides(scene_index);
    preset_clearAllFxSendStepOverrides();
```

---

### Change C9 — menu.c: superpage live value display

**File**: `Core/Menu/menu.c`
**Lines**: 3199–3221 (`MENU_CELL_SCENE_SETTING` case in `menu_cellDisplayValue()`)
**Action**: MODIFY

**Current**:
```c
    if (cell->kind == MENU_CELL_SCENE_SETTING) {
        uint8_t scene_index = scene_getActiveIndex();
        switch (cell->scene_setting) {
        case MENU_SCENE_SETTING_AUDIO_OUT:
            return scene_getVoiceAudioOut(scene_index, cell->slot);
        case MENU_SCENE_SETTING_FX_SEND_AMOUNT:
            return scene_getVoiceFxSendAmount(scene_index, cell->slot);
        case MENU_SCENE_SETTING_FADER_SETTING:
            return scene_getVoiceFaderSetting(scene_index, cell->slot);
        case MENU_SCENE_SETTING_VOICE_MORPH:
            return scene_getVoiceMorphAmount(scene_index, cell->slot);
        default:
            return 0u;
        }
    }
```

**New**:
```c
    if (cell->kind == MENU_CELL_SCENE_SETTING) {
        /*
         * Display the effective runtime value for Scene-setting cells.
         *
         * Inputs: active Scene index and voice slot from the resolved cell.
         * Output: for automatable settings (morph, audio out, FX send), the
         * effective value reflects the step-automation override when active,
         * otherwise the retained Scene setting. Fader setting has no step
         * override and reads from SceneData directly.
         *
         * This enables the Scene superpage (VOICE/mix appended screen) to
         * show live step-automation values during playback, matching the
         * behavior of the PERF page's morph display.
         *
         * Clients: menu_sceneLiveRefreshService() triggers ~8 Hz repaints
         * when Scene-setting cells are visible during playback.
         *
         * Affiliate: presetMorph_getEffectiveVoiceAmount() (morph),
         * preset_getEffectiveAudioOut() (audio out),
         * preset_getEffectiveFxSendAmount() (FX send).
         */
        uint8_t scene_index = scene_getActiveIndex();
        switch (cell->scene_setting) {
        case MENU_SCENE_SETTING_AUDIO_OUT:
            return preset_getEffectiveAudioOut(scene_index, cell->slot);
        case MENU_SCENE_SETTING_FX_SEND_AMOUNT:
            return preset_getEffectiveFxSendAmount(scene_index, cell->slot);
        case MENU_SCENE_SETTING_FADER_SETTING:
            return scene_getVoiceFaderSetting(scene_index, cell->slot);
        case MENU_SCENE_SETTING_VOICE_MORPH:
            return presetMorph_getEffectiveVoiceAmount(scene_index,
                                                       cell->slot);
        default:
            return 0u;
        }
    }
```

---

### Change C10 — menu.c: immediate underline on Scene-target automation write

**File**: `Core/Menu/menu.c`
**Lines**: 2650–2658 (after `if (wrote) {` block)
**Action**: MODIFY

**Current** (lines 2650–2658):
```c
    if (wrote) {
        /* Pattern-wide name markers remain descriptor-only; Scene-setting
         * cells use exact held-value markers and need no extra SRAM mask. */
        if (cell.kind == MENU_CELL_INSTRUMENT)
            va_searchSetBit(cell.descriptor_index);
        va_underlineSuppressed |= (uint8_t)((1u << knobNr) | (0x10u << knobNr));
        va_lastEditTick = time_sysTick;
        menu_knobs_dirty = 1u;
    }
```

**New**:
```c
    if (wrote) {
        /*
         * Set immediate search-result markers for the written automation.
         *
         * Instrument cells set the descriptor-based search bit for the
         * Pattern-wide name markers. Scene-setting cells set the
         * va_searchSceneMask bit so the underline appears on the next repaint
         * without waiting for the progressive scan to complete a full 128-step
         * sweep. Both paths suppress the edit-mode underline flash and mark
         * the display dirty.
         *
         * Affiliate: va_scanService() discovers these markers during the
         * progressive scan; this path provides immediate feedback.
         */
        if (cell.kind == MENU_CELL_INSTRUMENT)
            va_searchSetBit(cell.descriptor_index);
        else if (cell.kind == MENU_CELL_SCENE_SETTING)
            va_searchSceneMask |= va_sceneSearchBitForCell(&cell);
        va_underlineSuppressed |= (uint8_t)((1u << knobNr) | (0x10u << knobNr));
        va_lastEditTick = time_sysTick;
        menu_knobs_dirty = 1u;
    }
```

---

### Change C11 — Boot-state stale mask bits

**No code change needed.** Resolved by Part A (Change A2: `bank_init()` seeds
each Scene's mask to `(1u << i)`, so no Scene inherits stale bits from a prior
session). See plan section C3.

---

## SRAM Summary

| Region | Before | After | Delta | Owner |
|--------|--------|-------|-------|-------|
| `bank_scene_mask_voice_edit` | 2 bytes | 32 bytes | +30 bytes | BankData.c |
| `audio_out_step_override` | 0 | 12 bytes | +12 bytes | presetManager.c |
| `fx_send_step_override` | 0 | 12 bytes | +12 bytes | presetManager.c |
| `morph_lfo_contributions` | 144 bytes | 144 bytes | 0 | presetMorphEngine.c |
| **Total** | | | **+54 bytes** | |

Bank data +30 bytes approved (V4). Step-override tables +12 bytes approved (V3).
Total +54 bytes. LFO contribution table unchanged (same 2-byte struct, different
field names).

**Note**: The Autosave Bank section stays at 128 bytes. The per-Scene mask region
(bytes 13–44, 32 bytes) fits within the existing allocation.

---

## Implementation Order

1. **A1–A10** (BankData per-Scene array, accessors, Autosave, storageTypes,
   filesystem, morph rebuild) — build and verify Part A in isolation.
2. **C1–C5** (step-override tables and effective-value getters in
   presetManager.c/h) — add the readable override layer.
3. **C6–C8** (sequencer apply/restore integration) — wire up the new override
   set/clear calls.
4. **C9** (menu live display) — switch display to effective-value getters.
5. **C10** (immediate underline) — one-line bug fix, can land at any point.
6. **B1–B7** (contribution type, resolver, setter, InstrumentManager
   encoding) — Part B changes are self-contained and must land together.

Build after step 6. Each group can be committed independently, but all groups
must be present before the build and test pass.

---

## Files Modified (complete list)

| File | Changes | Lines |
|---|---|---|
| `Core/Bank/BankData.c` | A1–A9: array, init, all accessors, per-Scene set/get | 8, 67–79, 120–147, 264–355 + new |
| `Core/Bank/BankData.h` | A10: per-Scene set/get declarations | after 56 |
| `Core/Bank/Scene/Autosave.h` | A11: width constant | after 158 |
| `Core/Bank/Scene/Autosave.c` | A12–A14: dirty width, live getter, bank apply | 968–973, 1196–1198, 1472–1475 |
| `Core/Hardware/SD/storageTypes.h` | A15: bankset struct per-Scene array | 239–241 |
| `Core/Hardware/SD/storageTypes.c` | A16–A17: parser+writer per-Scene keys | 1186–1220 |
| `Core/Hardware/SD/filesystem.c` | A19–A23: 4 load + 1 save per-Scene loops | 13907, 14090, 18726, 27385, 29569 |
| `Core/Bank/Scene/Preset/presetManager.c` | A24: morph rebuild; C1–C4: override tables+API | 1139 + new |
| `Core/Bank/Scene/Preset/presetManager.h` | C5: override API declarations | after 393 |
| `Core/Sequencer/sequencer.c` | C6–C8: apply/restore integration | 287, 831–836 |
| `Core/Menu/menu.c` | C9: live display; C10: immediate underline | 2650–2658, 3199–3221 |
| `Core/Bank/Scene/Preset/presetMorphEngine.h` | B1–B2: direction enum, setter signature | after 4, 60–77 |
| `Core/Bank/Scene/Preset/presetMorphEngine.c` | B3–B7: struct, has-check, resolver, init, clear, setter | 22–25, 127, 134–182, 261–265, 607–659 |
| `Core/DSP/Instruments/InstrumentManager.c` | B8: polarity-to-direction encoding | 2323–2337 |

---

## Applied implementation notes — 2026-09-25

The S071 implementation is now applied to the working tree.

### Part A applied

- `BankData.c/.h` now owns one VOICE edit mask per resident Scene. `bank_init()`
  seeds Scene `i` with only bit `i`; active-entry UI accessors retain their
  existing signatures, while indexed accessors serve Autosave and bankset
  restore/capture.
- The existing 128-byte Autosave Bank section uses bytes 13..44 for the 16
  little-endian masks. Dirty marking, live-byte capture, and boot apply all use
  the same 32-byte layout.
- `storage_bankset_t` now stages 16 masks and a 16-bit seen bitmap. The parser
  accepts both legacy `scene_mask_voice_edit=XXXX` (expanded to every Scene)
  and indexed `scene_mask_voice_edit_NN=XXXX` keys. Bank Save emits 16 indexed
  lines; all four load/commit paths apply only parsed entries.
- Scene activation now queues the per-voice Morph rebuild after mirror sync.

### Part B applied

- The hidden 144-byte LFO contribution table remains two bytes per entry, but
  now stores `direction + depth` instead of `active + absolute amount`.
- The bounded Morph resolver computes each signed endpoint delta from the
  current effective base, so a step-automation base change between LFO sample
  and worker resolution is handled without a new LFO sample.
- InstrumentManager encodes positive, original-LXR negative, and bipolar
  polarity into that base-independent representation without reading the Morph
  base or calling `modNode_shapeRangeU16()` for voice Morph.

### Part C applied

- Audio-out and FX-send now have six-entry runtime-only step overlays and
  effective-value getters. They are cleared at transport restore and preset
  initialization; retained SceneData and AutoSave are untouched.
- The Scene superpage reads live effective Morph/audio-out/FX-send values.
- Held-step Scene-target writes immediately set the Scene search mask so their
  underline does not wait for the progressive 128-step scan.
- The per-Scene mask default removes the stale boot fan-out-bit path; no extra
  present-mask intersection was added.

### Allocation record

The linked image reports `text=456620`, `data=416`, `bss=291900`.

- BankData mask: 2 → 32 bytes, +30 bytes normal SRAM.
- `op_bankset_state`: 8 → 40 bytes, +32 bytes normal SRAM staging required to
  parse/capture all 16 bankset masks.
- Audio-out overlay: 12 bytes normal SRAM.
- FX-send overlay: 12 bytes normal SRAM.
- LFO contribution table: 144 bytes before and after.

The Autosave record allocation remains unchanged; no new persistent payload
section was introduced.

### Verification

- `make img -j2` PASS; the generated image payload is 457,036 bytes (the
  wrapped `build/LXRV2_lxr02.img` file is 457,052 bytes).
- `git diff --check` PASS.
- `tools/verify_bank_autosave.py` and `tools/decode_devlogs.py` now understand
  both the expanded per-Scene mask region and legacy bankset text.
- Hardware isolation and persistence fixtures from the plan remain to be run
  against the new image: per-Scene mask isolation, Scene morph retention,
  multi-Scene fan-out, bankset round-trip, AutoSave round-trip, live display,
  underline timing, and LFO/step-base composition.
