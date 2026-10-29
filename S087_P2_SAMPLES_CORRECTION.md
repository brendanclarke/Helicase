# S087 P2 — Samples and wavetables: display and naming corrections

**Status:** implemented 2026-10-29; see the work log in §6. P2-5 closed
without code.

**Purpose:** the exact code changes for the defects found on hardware after
the S087 P1 follow-up:
1. under `typ` `smp`, `slp` and `wtb`, the `wav` cell does not show the
   selected item;
2. the sample level seems low;
3. the modulator `wav` list shows `s01`, `s02`, `s03` instead of `s01`,
   `l01`, `w01`.

**Base:** commit `f3805478`; `Core/` is clean on that commit. Line numbers
below are for that tree.
**Scope:** firmware source in `Core/`, plus one host-test expectation that
the name rule changes (§4).

---

## 0. How to apply

- **Order:** apply each file's changes in the order listed. Within a file the
  changes run from the highest line to the lowest, so every cited line number
  stays valid until it is used.
- **Each change gives:** an ID, a file, a line or range, and **ADD** (insert
  after the cited line), **MOD** (replace the cited lines) or **DEL** (delete
  the cited lines).
- **Comment blocks:** each ADD or MOD that creates or changes a function
  carries its comment block in the project form (What / Why / Inputs /
  Outputs / Accessors / Affiliates). The block goes into the source with the
  code.
- **Code is given complete.** The cited lines are replaced exactly by the code
  shown.

### 0.1 Fix index

| ID | Defect | Cause | File(s) | Changes |
|---|---|---|---|---|
| P2-1 | Under `smp`/`slp`/`wtb`, `wav` shows a stock name (`sin`, `tri`, …) instead of `s01`/`l01`/`w01` | §1.1 | `menu.c` | MU-2, MU-3, MU-4, MU-5 |
| P2-2 | Modulator list shows `sNN` for its `l` and `w` entries | §1.1 | `menu.c` | MU-2, MU-3, MU-4, MU-5 |
| P2-3 | The single-parameter view shows a stray character in column 9; the automation formatter writes one byte past a stack buffer | §1.2 | `menu.c` | MU-1, MU-2 |
| P2-4 | Item names are cut to the first 8 characters; the user rule is first 4 + last 4 | §1.3 | `storageTypes.c/.h`, `modules/samples.c` | ST-1, ST-2, TB-1, TB-2 |
| P2-5 | Sample level seems low | §1.4: no attenuation found | none | none |

---

## 1. Findings

### 1.1 The compact `wav` cell ignores the type (P2-1, P2-2)

- **Where `wav` is drawn:**
  - The overview cells use `menu_formatCellValue3()`; the clicked-in value
    and the automation overlays use `va_formatValue3()`.
  - Both send every `DTYPE_MENU` value to `getMenuItemNameForValue()`
    (`menu.c:6488-6491`, `menu.c:3348-3351`).
- **What that lookup knows:** for `MENU_WAVEFORM` (`menu.c:11680-11687`), only
  the stock names for 0..7 and `s` + (value − 8) + 1 above that.
- **Consequences:**
  - **osc1 under `smp`/`slp`/`wtb`:** values start at 0, so item 0 shows
    `sin`, item 1 `tri`, and so on. Nothing on the page says a sample or
    wavetable is selected. This is the "no way to set the wavetable or
    sample" report.
  - **Modulator list:** the stock waves are followed by every sample (`s`),
    every sample again as a loop (`l`), then every wavetable (`w`). The
    lookup numbers them all as `sNN`. With one sample and one wavetable the
    list reads `s01 s02 s03`; it should read `s01 l01 w01`.
- **Already correct:**
  - `menu_formatWaveCell()` (`menu.c:11605-11671`) formats both cases
    correctly.
  - The single-parameter view (`menu.c:14343-14348`) and the step-automation
    display (`menu.c:13238-13265`) already use it.
  - The clamp is also already correct: `instrumentManager_waveCellCount()`
    gives the item count per type, and the flat-list length for the
    modulators.
- **Fix:** route the two compact formatters through one helper,
  `menu_formatWaveValue3()`, built on `menu_formatWaveCell()`.

### 1.2 Name copies write a NUL past 8 characters (P2-3)

- **The getters:** `sampleMemory_getDisplayName()` and
  `sampleMemory_getWavetableName()` take a 9-byte buffer and write a
  terminating NUL at byte 8 (`SampleMemory.c:205-234`).
- **The caller:** `menu_formatWaveCell()` passes them its destination
  directly.
  - **Single-parameter view:** the destination is
    `&editDisplayBuffer[1][0]`, so `editDisplayBuffer[1][8]` becomes `'\0'`.
    The LCD shows character 0, a custom glyph, in column 9 next to the name.
  - **Automation formatter:** `menu_formatVoiceAutomationValue3()` passes
    `char name[8]`, so the NUL lands one byte past the array on the stack.
- **Fix:**
  - `menu_formatWaveCell()` reads the name into its own 9-byte local and
    copies 8 characters out.
  - The automation formatter uses the new helper and no longer owns a name
    buffer.
- **List arithmetic:** in the modulator branch, `(uint8_t)(2u * samples)`
  wraps to 0 at 128 samples. The rewritten branch uses `uint16_t`.

### 1.3 The 8-character name rule (P2-4)

- **What is specified and built:** a numeric prefix plus `_`, `-` or a space
  is stripped, then the **first 8 characters** are kept.
  - Sources: `S087_P0_IMPLEMENTATION.md` C-10/C-12 "Display names";
    `storage_displayNameFromLfn()`, `storageTypes.c:2525-2557`.
  - No earlier document records a first-4 + last-4 rule.
- **What the user wants (S087 P2):** a name longer than 8 characters keeps its
  **first 4 and last 4** characters, so names that differ only at the end
  (`kick_hard_01`, `kick_hard_02`) stay distinct:
  - `kick_hard_01` → `kickd_01` (today: `kick_har`);
  - `longer_than_eight` → `longight`.
- **Only item names are affected:** a sample's file name and a wavetable's
  folder name. The frame files inside a wavetable folder are never named or
  shown.
- **Unchanged:**
  - names of 8 characters or fewer (`ak_hvox`);
  - the prefix strip and the extension strip.
- **Applies to:** both sample names (the file name) and wavetable names (the
  folder name), because they share the one function.
- **When it takes effect:** names are written into the catalogue at install,
  so they change on the next **Sample upload**.

### 1.4 Sample level (P2-5): no attenuation found

Every stage from the SD file to the oscillator output was checked:

| Stage | Code | Effect on level |
|---|---|---|
| Source read | `SampleInstall.c:185-198` `sampleInstall_sourceSample()` | 8-bit → (x − 128) << 8; 16-bit unchanged; 24-bit → >> 8 (top 16 bits). Full scale in = full scale out. |
| Stereo → mono | `SampleInstall.c:209-218` | (L + R) / 2; the peak is measured **after** this. |
| Peak | `SampleInstall.c:269-275` | the absolute peak of the stored mono data. |
| Gain code | `SampleInstall.c:577-591` | the largest code whose gain keeps peak × gain ≤ 32767. Steps are 2^(1/24) (0.25 dB), so the played peak is within 0.25 dB of full scale. |
| SD feed | `filesystem.c:24639-24713` | the file's own format and block size; no scaling. |
| Bind | `Oscillator.c:1862-1868` `osc_bindGain()` | the entry's gain code for the bound sample index. |
| Readers | `Oscillator.c:1927`, `1977`, `2018`, `2087` (plain/FM: `gain × bindGain`); `2173`, `2227`, `2276`, `2339` (ring/sync: `bindGain`) | the same per-voice `gain` argument as the stock waves. |
| Engines | `DrumVoice.c:489/565`, `Snare.c:286`, `CymbalVoice.c:312`, `HiHat.c:330` | the typed and stock calls pass the same gain (`1 − fmModAmount`, `1.0`, `1 − mix`, `1.0`, `0.5`). |

- **Result:** a sample plays with its peak at full scale (within 0.25 dB),
  exactly as loud at its peak as a stock wave. Nothing in the path attenuates
  it.
- **Why it can still sound quieter:**
  - **Crest factor:** a stock saw or square is at full scale for most of its
    cycle. A drum hit reaches full scale only at its transient, and its
    average level is typically 10–20 dB below that.
  - **One-shot length:** a short `smp` one-shot ends while the amp envelope
    is still open.
- **Making samples louder** needs a loudness rule (for example RMS
  normalisation with a ceiling, or a level row), not a correction. It is not
  in this schedule; see §5.

---

## 2. `Core/Menu/menu.c` (P2-1, P2-2, P2-3)

### MU-1 `menu.c:13220-13265` — MOD `menu_formatVoiceAutomationValue3()`

Replace lines 13220-13265 (the comment block and the function) with:

```c
/*
 * Format a voice parameter's 7-bit automation value (S087 P1; S087 P2).
 *
 * What:       for an oscillator wave row (DTYPE_MENU, MENU_WAVEFORM) of a voice
 *             target: menu_formatWaveValue3() on a cell built for the target
 *             (sNN/lNN/wNN, the stock name, or "---" when the value selects
 *             nothing). Every other row: menu_formatAutomationValue3().
 * Why:        S087 P0 §12.4 and impl. §10.7: the automation display must name
 *             the entry the value selects for that slot's oscillator. S087 P2:
 *             this function's 8-byte name buffer received the catalogue
 *             getters' terminating NUL one byte past its end; the shared helper
 *             now owns the buffers.
 * Inputs:     target (canonical voice parameter ID), descriptor, value, buf[3].
 * Outputs:    buf (3 characters, not terminated).
 * Accessors:  the step-automation amount display and the automation edit line.
 * Affiliates: menu_formatWaveValue3(), menu_formatAutomationValue3(),
 *             instrumentParam_slot()/instrumentParam_local().
 */
static void menu_formatVoiceAutomationValue3(uint16_t target,
                                             const ParamDescriptor *descriptor,
                                             uint8_t value, char *buf)
{
    if (descriptor &&
        (descriptor->dtype & 0x0fu) == DTYPE_MENU &&
        (uint8_t)(descriptor->dtype >> 4) == MENU_WAVEFORM &&
        instrumentParam_isVoiceParameter(target)) {
        menu_cell_t cell;

        memset(&cell, 0, sizeof(cell));
        cell.kind = MENU_CELL_INSTRUMENT;
        cell.slot = instrumentParam_slot(target);
        cell.descriptor_index = instrumentParam_local(target);
        cell.descriptor = descriptor;
        menu_formatWaveValue3(&cell, value, buf);
        return;
    }
    menu_formatAutomationValue3(descriptor, value, buf);
}
```

### MU-2 `menu.c:11590-11671` — MOD `menu_formatWaveCell()`; ADD `menu_formatWaveValue3()`

Replace lines 11590-11671 (the comment block and the function) with:

```c
/*
 * Format one oscillator wave cell (S087 stage D; S087 P2).
 *
 * What:       the 8-character name and the 3-character code of the entry a
 *             wave value selects:
 *             - osc1 under wav: the stock name, no code;
 *             - osc1 under smp/slp/wtb: the item's catalogue name and
 *               sNN/lNN/wNN; both blank when the type has no item at that
 *               value;
 *             - a modulator: the stock name for 0..OSC_SAMPLE_START-1, then the
 *               flat list: every sample (s), every sample as a loop (l), every
 *               wavetable (w) (S087 P0 §12.4); blank past the end;
 *             - no instrument cell: the stock name, else blank.
 * Why:        S087 P0 §4.3/§12.4: one formatter for the single-parameter view,
 *             the compact cells and the automation display. S087 P2: the
 *             catalogue getters write a terminating NUL after 8 characters, so
 *             the name is read into a 9-byte local and 8 characters are copied
 *             out (the single-parameter view showed character 0 in column 9);
 *             the list bounds are computed in 16 bits (2 x 128 samples wrapped
 *             a uint8_t to 0).
 * Inputs:     cell (instrument cell, or NULL), value, nameDst[8], codeDst[3].
 * Outputs:    nameDst[0..7] and codeDst[0..2], space-padded, not terminated.
 * Accessors:  menu_formatWaveValue3(), the single-parameter edit painter.
 * Affiliates: instrumentManager_osc1Type(), sampleMemory_getDisplayName(),
 *             sampleMemory_getWavetableName(), menu_formatUserShortName().
 */
static void menu_formatWaveCell(const menu_cell_t *cell, uint8_t value,
                                char *nameDst, char *codeDst)
{
    const uint8_t stock = (uint8_t)waveformNames[0][0];
    const uint16_t samples = sampleMemory_getNumSamples();
    const uint16_t tables = sampleMemory_getNumWavetables();
    const uint8_t isInstrument = (uint8_t)(cell &&
        cell->kind == MENU_CELL_INSTRUMENT && cell->descriptor);
    const uint8_t isOsc1 = (uint8_t)(isInstrument &&
        strcmp(cell->descriptor->file_key, "osc1_wave") == 0);
    const uint8_t type = isOsc1 ? instrumentManager_osc1Type(cell->slot)
                                : (uint8_t)OSC_TYPE_WAV;
    char name[SAMPLE_DISPLAY_NAME_LEN + 1u];
    char prefix;
    uint8_t index;

    memset(nameDst, ' ', SAMPLE_DISPLAY_NAME_LEN);
    memset(codeDst, ' ', 3u);
    if (isOsc1 && type != OSC_TYPE_WAV) {
        /* osc1 under smp/slp/wtb: the value is the item index. */
        if (type == OSC_TYPE_WTB) {
            if (value >= tables)
                return;
            prefix = 'w';
        } else {
            if (value >= samples)
                return;
            prefix = (type == OSC_TYPE_SMP) ? 's' : 'l';
        }
        index = value;
    } else if (value < stock) {
        /* A stock wave: osc1 under wav, a modulator, or no cell. */
        memcpy(nameDst, waveformNames[value + 1u], 3u);
        return;
    } else if (isInstrument && !isOsc1) {
        /* A modulator's flat list after the stock waves. */
        const uint16_t entry = (uint16_t)(value - stock);

        if (entry < samples) {
            prefix = 's';
            index = (uint8_t)entry;
        } else if (entry < (uint16_t)(2u * samples)) {
            prefix = 'l';
            index = (uint8_t)(entry - samples);
        } else if (entry < (uint16_t)(2u * samples + tables)) {
            prefix = 'w';
            index = (uint8_t)(entry - 2u * samples);
        } else {
            return;
        }
    } else {
        /* osc1 under wav past the stock list, or no cell: nothing. */
        return;
    }

    if (prefix == 'w')
        sampleMemory_getWavetableName(index, name);
    else
        sampleMemory_getDisplayName(index, name);
    memcpy(nameDst, name, SAMPLE_DISPLAY_NAME_LEN);
    menu_formatUserShortName(prefix, index, codeDst);
}

/*
 * Three-character text of an oscillator wave value (S087 P2).
 *
 * What:       the entry's code (sNN/lNN/wNN) for user content, the stock name
 *             for a stock wave, "---" when the value selects nothing (osc1
 *             under smp/slp/wtb with no item at that value).
 * Why:        the compact cells and the value overlays formatted wav through
 *             getMenuItemNameForValue(), which knows only the stock names and
 *             sNN: osc1 under smp/slp/wtb showed a stock name for its item,
 *             and the modulators' l and w entries showed sNN (S087 P2 hardware
 *             report).
 * Inputs:     cell (an instrument wave cell), value, out[3].
 * Outputs:    out[0..2], not terminated.
 * Accessors:  va_formatValue3(), menu_formatCellValue3(),
 *             menu_formatVoiceAutomationValue3().
 * Affiliates: menu_formatWaveCell(); MENU_WAVEFORM.
 */
static void menu_formatWaveValue3(const menu_cell_t *cell, uint8_t value,
                                  char out[3])
{
    char name[SAMPLE_DISPLAY_NAME_LEN];
    char code[3];

    menu_formatWaveCell(cell, value, name, code);
    if (code[0] != ' ')
        memcpy(out, code, 3u);
    else if (name[0] != ' ')
        memcpy(out, name, 3u);
    else
        memcpy(out, menuText_dash, 3u);
}
```

The single-parameter painter (`menu.c:14343-14348`) is unchanged. With MU-2
it shows the full 8-character name on the left: the sample's name for
`smp`/`slp`, the folder name for `wtb` (for example `ak_hvox`). The code
(`s01`, `l01`, `w01`) stays on the right.

### MU-3 `menu.c:6488-6491` — MOD the `DTYPE_MENU` case of `menu_formatCellValue3()`

Replace lines 6488-6491 with:

```c
    case DTYPE_MENU:
        /* S087 P2: wav follows osc1's type or the modulator list
         * (menu_formatWaveValue3()); every other menu uses its table. */
        if ((uint8_t)(menu_cellDtype(cell) >> 4) == MENU_WAVEFORM) {
            menu_formatWaveValue3(cell, value, valueAsText);
            break;
        }
        getMenuItemNameForValue((uint8_t)(menu_cellDtype(cell) >> 4),
                                value, valueAsText);
        break;
```

### MU-4 `menu.c:3348-3351` — MOD the `DTYPE_MENU` case of `va_formatValue3()`

Replace lines 3348-3351 with:

```c
    case DTYPE_MENU:
        /* S087 P2: wav follows osc1's type or the modulator list
         * (menu_formatWaveValue3()), as in menu_formatCellValue3(). */
        if ((uint8_t)(menu_cellDtype(cell) >> 4) == MENU_WAVEFORM) {
            menu_formatWaveValue3(cell, value, out);
            break;
        }
        getMenuItemNameForValue((uint8_t)(menu_cellDtype(cell) >> 4),
                                value, out);
        break;
```

### MU-5 `menu.c:2248` — ADD the forward declaration

After line 2248 (`static void va_formatValue3(...);`) add:

```c
/* S087 P2: wave value text; defined beside menu_formatWaveCell(). */
static void menu_formatWaveValue3(const menu_cell_t *cell, uint8_t value,
                                  char out[3]);
```

---

## 3. `Core/Hardware/SD/storageTypes.c/.h` (P2-4)

### ST-1 `storageTypes.c:2515-2557` — MOD `storage_displayNameFromLfn()`

Replace lines 2515-2557 (the comment block and the function) with:

```c
/*
 * Catalogue display name from a long filename (S087 stage C; S087 P2; see
 * the header).
 *
 * What:       optional extension strip, numeric-prefix strip, then a name of
 *             up to 8 characters copied as it is, or a longer name shortened
 *             to its first 4 and last 4 characters; space padding.
 * Why:        S087 P0 §5.2 names; the 8-character catalogue field is fixed.
 *             S087 P2 (user rule): first 4 + last 4 keeps names that differ
 *             only at the end distinct ("kick_hard_01" -> "kickd_01").
 * Inputs:     lfn, strip_ext.  Outputs: dst[8].
 * Accessors:  the install walker; tools/dsp_test/modules/samples.c.
 * Affiliates: storage_naturalCompare().
 */
void storage_displayNameFromLfn(char dst[8u], const char *lfn, uint8_t strip_ext)
{
    uint8_t len = 0u;
    uint8_t start = 0u;
    uint8_t count;
    uint8_t i;

    if (!dst)
        return;
    for (i = 0u; i < 8u; i++)
        dst[i] = ' ';
    if (!lfn)
        return;
    while (lfn[len] != '\0')
        len++;
    if (strip_ext) {
        uint8_t dot = len;

        while (dot > 0u && lfn[dot - 1u] != '.')
            dot--;
        if (dot > 0u && dot < len)
            len = (uint8_t)(dot - 1u);
    }
    while (start < len && lfn[start] >= '0' && lfn[start] <= '9')
        start++;
    if (start > 0u && start < len &&
        (lfn[start] == '_' || lfn[start] == '-' || lfn[start] == ' '))
        start++;
    else
        start = 0u;

    count = (uint8_t)(len - start);
    if (count <= 8u) {
        for (i = 0u; i < count; i++)
            dst[i] = lfn[start + i];
    } else {
        for (i = 0u; i < 4u; i++) {
            dst[i] = lfn[start + i];
            dst[4u + i] = lfn[len - 4u + i];
        }
    }
}
```

### ST-2 `storageTypes.h:668-681` — MOD the declaration's comment block

Replace lines 668-681 with:

```c
/*
 * Catalogue display name from a long filename (S087 stage C; S087 P2).
 *
 * What:       optionally drops the extension, strips a leading numeric prefix
 *             followed by '_', '-' or a space, then keeps a name of up to 8
 *             characters as it is and shortens a longer one to its first 4 and
 *             last 4 characters; pads with spaces.
 * Why:        a sample's catalogue name is its LFN without the extension; a
 *             wavetable's name is its folder name (S087 P0 §5.2). First 4 +
 *             last 4 keeps numbered sets distinct (S087 P2).
 * Inputs:     lfn, strip_ext.  Outputs: dst[8], always space-padded.
 * Accessors:  the install walker; tools/dsp_test/modules/samples.c.
 * Affiliates: storage_naturalCompare().
 */
void storage_displayNameFromLfn(char dst[8u],
                                const char *lfn, uint8_t strip_ext);
```

---

## 4. `tools/dsp_test/modules/samples.c` (P2-4)

The host test's name law checks the old first-8 rule. These two edits keep it
true to ST-1; nothing here is required for the firmware.

### TB-1 `samples.c:357-358` — MOD the truncation check

Replace lines 357-358 with:

```c
    storage_displayNameFromLfn(name, "longer_than_eight.wav", 1u);
    expect(memcmp(name, "longight", 8u) == 0, "law 3: first 4 + last 4");
    storage_displayNameFromLfn(name, "kick_hard_01.wav", 1u);
    expect(memcmp(name, "kickd_01", 8u) == 0, "law 3: names differing at the end stay distinct");
```

### TB-2 `samples.c:30-32` — MOD the order law 3 text

Replace lines 30-32 with:

```c
 *                          3. a numeric prefix plus '_', '-' or ' ' is
 *                             stripped, the extension dropped when asked; up
 *                             to 8 characters kept, a longer name shortened
 *                             to its first 4 and last 4; padded to 8.
```

---

## 5. Notes

- **Display after these changes** (one sample `kick`, one table folder
  `ak_hvox`):

  | Row | Overview cell | Single-parameter view |
  |---|---|---|
  | osc1 `typ` `smp`, `wav` 0 | `s01` | `kick` … `s01` |
  | osc1 `typ` `slp`, `wav` 0 | `l01` | `kick` … `l01` |
  | osc1 `typ` `wtb`, `wav` 0 | `w01` | `ak_hvox` … `w01` |
  | modulator `wav` 8 / 9 / 10 | `s01` / `l01` / `w01` | `kick` … `s01` / `kick` … `l01` / `ak_hvox` … `w01` |
  | osc1 `typ` `smp`, no samples | `---` | blank |

- **Names already in flash** keep the first-8 form until the next Sample
  upload (ST-1 runs at install).
- **CPU:** no change to any audio path. The changes are in menu formatting
  and the install-time name.
- **Not changed:**
  - `getMenuItemNameForValue()`'s `MENU_WAVEFORM` branch
    (`menu.c:11680-11687`) still formats `sNN` for values ≥ 8. After MU-3
    and MU-4 its only remaining wave callers have no cell
    (`menu_formatAutomationValue3()` for a non-voice descriptor), and no
    wave row has such a descriptor.
  - Sample level (§1.4): no attenuation exists, so there is nothing to
    correct. If samples should play louder than peak-normalised, that is a
    new rule for the user to choose: loudness (RMS) normalisation with a
    ceiling at install, or a per-oscillator level. It is not part of this
    schedule.

---

## 6. Work log (applied 2026-10-29)

**Repository:** `/Users/bc/Helicase Project/Helicase`.
**Base:** `f3805478`; `Core/` was clean before the first edit. The only
untracked file was this document.
**Result:** MU-1 … MU-5, ST-1, ST-2, TB-1 and TB-2 applied as written.
P2-5 needs no code.

### 6.1 Changes landed

| ID | File | New location | Note |
|---|---|---|---|
| MU-1 | `Core/Menu/menu.c` | `menu_formatVoiceAutomationValue3()`, line 13291 | now calls `menu_formatWaveValue3()`; owns no name buffer |
| MU-2 | `Core/Menu/menu.c` | `menu_formatWaveCell()`, line 11631; `menu_formatWaveValue3()`, line 11711 | 9-byte local for the getters; 16-bit list arithmetic |
| MU-3 | `Core/Menu/menu.c` | `menu_formatCellValue3()`, `DTYPE_MENU` case | `MENU_WAVEFORM` routed to the new helper |
| MU-4 | `Core/Menu/menu.c` | `va_formatValue3()`, `DTYPE_MENU` case | `MENU_WAVEFORM` routed to the new helper |
| MU-5 | `Core/Menu/menu.c` | line 2250 | forward declaration with its comment |
| ST-1 | `Core/Hardware/SD/storageTypes.c` | `storage_displayNameFromLfn()`, line 2529 | first 4 + last 4 for names over 8 |
| ST-2 | `Core/Hardware/SD/storageTypes.h` | declaration block | comment matches ST-1 |
| TB-1 | `tools/dsp_test/modules/samples.c` | law 3 expectations | `longight`, `kickd_01` |
| TB-2 | `tools/dsp_test/modules/samples.c` | law 3 text | first 4 + last 4 |

The single-parameter painter is unchanged: it still calls
`menu_formatWaveCell()` directly and now receives 8 copied characters while
the getters' NUL stays in the helper's local.

### 6.2 Checks run

- **Firmware compile:** `make all` compiled every changed file and linked
  `build/lxr02.elf`. It then failed at the final report step because the
  Makefile calls `tools/link_budget.py`, which does not exist in this tree;
  the script lives at `tools/diagnostic_tools/link_budget.py`. This is
  present at base `f3805478` and is not caused by P2. No source edit was
  made for it.
- **Manual link budget:** `python3 tools/diagnostic_tools/link_budget.py
  arm-none-eabi-nm build/lxr02.elf` — flash 670,628 / 753,664 B used,
  headroom 83,036 B; ITCM 12,640 / 16,384 B; DTCM statics 4,344 B; FXBUF
  margin 3,840 B.
- **Image:** `make img` wrote `build/LXRV2_lxr02.img` (670,644 bytes on
  disk, 16-byte header plus the reported 670,628-byte payload); OK.
- **Host tests:** `make -C tools/dsp_test samples` — catalogue, install and
  order all 0 failures; the law-3 break build fails as designed.
  `make -C tools/dsp_test all` — oscillator, modulation, voice_control,
  sequencer, instrument_manager, samples and sampleoptimize all pass;
  `check_special_tags.py` reports 163 rows, 0 mismatches. The
  `voice_control_break` run prints its six expected law failures, and the
  `stock_tables` check fails because it resolves
  `tools/Core/DSPAudio/wavetable.c`; both are pre-existing and unrelated
  to P2.

### 6.3 Hardware check still to do

On the next flash, confirm the §5 table with one sample and one wavetable
folder: `s01`/`l01`/`w01` in the overview cells, the item name plus code in
the single-parameter view, `s01 l01 w01` in the modulator list, and `---`
under `smp` when no sample is installed. No character should appear in
column 9 of the single-parameter view.

Re-upload Samples to see the new first-4 + last-4 catalogue names; entries
already in sample flash keep their old names until then.

### 6.4 Notes for the next session

- The P2 change touches only menu formatting and the install-time name.
  No oscillator, mixer, engine or sample-reader code changed, so the S087
  per-slot CPU gates do not apply.
- `getMenuItemNameForValue()`'s `MENU_WAVEFORM` branch is intentionally
  left alone (see §5); after MU-3/MU-4 no live wave cell reaches it.
- P2-5 is closed as "no attenuation found". Loudness normalisation or a
  per-oscillator level would be a new user rule.

---

## 7. Assessment (2026-10-29)

**Result:** the implementation matches this schedule exactly. No defects
found; nothing in `Core/` needs a follow-up for P2.

### 7.1 How it was checked

- **Method:** the schedule's code blocks were applied to a clean copy of
  `f3805478` by the cited line numbers (each MOD checked its first and last
  line before replacing). The result was compared with the working tree.
- **Result:**
  - `menu.c` and `storageTypes.h` are identical.
  - `storageTypes.c` and `modules/samples.c` differ only where this document
    was corrected after the copy was made (the name example in the ST-1
    comment and the second TB-1 check now use `kick_hard_01` → `kickd_01`).
    The tree has the corrected text.
- **No other file in `Core/` changed** (`git diff --stat f3805478 -- Core`:
  `menu.c`, `storageTypes.c`, `storageTypes.h` only).
- No further builds or tests were run for this assessment; §6.2 records
  the implementer's.

### 7.2 Review points

| Point | Finding |
|---|---|
| Every wave display path | Overview cells (`menu_formatCellValue3()`), value overlays (`va_formatValue3()`) and step-automation text (`menu_formatVoiceAutomationValue3()`) now go through `menu_formatWaveValue3()`; the single-parameter view calls `menu_formatWaveCell()`. The remaining `getMenuItemNameForValue()` calls are reached only for non-wave menus (`menu.c:3358`, `6504`, `14395`) or non-voice descriptors (`13246`, also reached from `menu_formatEffectAutomationValue3()`, `13366`, which has no wave rows). |
| Formatter and clamp agree | Both read osc1's runtime `oscType` (`instrumentManager_osc1Type()` / `instrumentManager_waveCellCount()`) and the same catalogue counts, so the last selectable value is the last item shown. |
| Modulators | `osc2_wave`/`osc3_wave` take the flat-list branch; the bounds are 16-bit, so 128 samples no longer wrap. |
| Name buffers | The catalogue getters' NUL stays in the 9-byte local; `nameDst` and `editDisplayBuffer[1][8]` receive only the 8 copied characters. The 8-byte stack buffer in the automation formatter is gone. |
| Name rule | `count = len − start` is at least 9 in the shortening branch, so `lfn[len − 4 + i]` stays inside the name. Names are space-padded, so no NUL reaches the catalogue entry. An LFN is at most 255 characters, so the `uint8_t` length cannot wrap. |
| CPU | No audio path changed. The added per-repaint work (one `strcmp()` and one slot lookup per wave cell) is on the menu side. |

### 7.3 Open items not caused by P2

The work log (§6.2) found two failures. Both come from the tool moves in
`f3805478` (scripts moved into subfolders without their paths updated):

| Item | Cause | Fix |
|---|---|---|
| `make all` fails at its last step | `Makefile:187` runs `tools/link_budget.py`, now at `tools/diagnostic_tools/link_budget.py` | change the path on `Makefile:187` |
| `make -C tools/dsp_test stock_tables` fails | `tools/plots/gen_stock_tables.py:47-48` still assume the script sits in `tools/`: `ROOT = dirname(HERE)` resolves to `tools/`, and `CSV_PATH` adds a second `plots/` | `ROOT = os.path.dirname(os.path.dirname(HERE))` and `CSV_PATH = os.path.join(HERE, "stock_wave_harmonics.csv")` |

### 7.4 Hardware

§6.3 stands as the device check. Re-upload the samples before judging the
names: entries already in flash keep the old first-8 form.
