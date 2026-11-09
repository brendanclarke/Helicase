# S093 P7 — Alkaline Samples and Wavetables as Oscillator Types: Implementation Schedule

- **Session:** 093, phase 7. This is the literal code schedule for
  `S093_P6_PARAMETER_TWEAKS.md` item 7 (§7). It covers:
  - the `typ` row (`wav` / `smp` / `slp` / `wtb`, as on Drum), with the `wav`
    cell naming Saw/Sqr or the catalogue item;
  - the S1 fused reader loops;
  - the S2 direct pitch;
  - S3, `pos` without the restore;
  - the S4 blend on the interpolation pool;
  - the conditional slot reserve (§7.5 of the plan).
- **Prerequisite:** `S093_P6_IMPLEMENTATION.md`, **landed 2026-11-09** and
  assessed (P6 doc §13). This schedule edits the P6 versions of
  `AlkalineVoice.c` (attack, the `pos` bend, ×16 drive). Line numbers in the
  Alkaline files, `make_init_alk.py` and the bench are from the tree **with
  P6 applied**; the other files did not change in P6.
- **Tags:** **ADD** / **MODIFY** / **REMOVE** / **NONE** (verified: no change,
  with the reason). Comment blocks are part of the change.
- **Revision 2 (2026-11-09, after P6 landed):**
  - line anchors moved to the post-P6 tree (`AlkalineParameters.c/.h`,
    `AlkalineVoice.c/.h`, `make_init_alk.py`, the bench); the other files
    are unchanged by P6;
  - the `wav` baseline is the P6 image's (214 + 1,230/32 = 252.4) and so is
    the flash headroom (47,836 B);
  - added the "Not offered" header edit (§3.1).

  Nothing in §0's conclusion or the code changes.
- **Binding rules:**
  - the CPU ceiling (whole voice, per-block work included);
  - constant per-sample cost;
  - no new ITCM code;
  - the Oscillator.c S087 loops and the ResonantFilter ZDF twins are not
    edited;
  - RAM sign-off (+1 B `modNode_interpLocked` approved for the reserve only);
  - no backward compatibility;
  - commits belong to the user.

---

## 0. Measured before writing this schedule: the plan's budget does not hold

The code in §3–§7 was built in scratch, outside the tree, on top of the P6
scratch build:
- on the host, all fifteen bench groups pass (0 failures), including the new
  AL-X2: every fused variant is bit-identical to reader + plain B2R;
- for the Cortex-M7 it was linked with `-Ofast -flto`, with the real
  `Oscillator.c` and `SampleMemory.c`, and counted with
  `control_cost.py --elf` and `elf_looppath.py`.

The method reproduces the firmware: with the typed paths compiled out, the
same link gives 1,232 per block against the landed P6 image's 1,230, and a
2,996 B sync block against its 3,000 B (re-measured on the P6 image,
2026-11-09).

| Mode | Loop (linked, per sample) | Per block (/32) | **Whole voice** | Against 261.3 |
|---|---:|---:|---:|---:|
| `wav` (Saw/Sqr, P6) | 214 (firmware; 215 in the scratch link) | 1,230 → 38.4 (firmware; 1,232 scratch) | **252.4** | −8.9 |
| `smp` | 217 | 1,769 → 55.3 | **≈ 272** | +11 |
| `slp` | 229 | 1,769 → 55.3 | **≈ 284** | +23 |
| `wtb` plain | 235 / 239 (16- / 8-bit copy) | ≤ 1,769 → ≤ 55.3 | **≈ 288–294** | +27…+33 |
| `wtb` blend | 252 / 262 | ≤ 1,769 → ≤ 55.3 | **≈ 307–317** | +46…+56 (≈ 23 of it is the blend premium) |

How to read the table:
- **Per block** is the longest path through the typed branches: 1,769
  against 1,232 for `wav`, so +537.
  - **Async +355:** `osc_userBind()` 89 plus its callees, and
    `osc_typedPositionApply()`. That includes a 64-bit division,
    `__aeabi_ldivmod` + `__udivmoddi4` = 213, for the sample speed cap
    `(int64)CAP × phaseInc / frames`.
  - **Sync +182:** the dearest setup, the blend's claim and plan.
  - The 64-bit division is not on the `wtb` path, so `wtb` is about 6.7 per
    sample lower than the column shows.
- **The plan's estimates (§7.3: smp/slp 258–260, wtb plain 256–258) were
  low.** The fused loops cost 217–262 (estimated 214–218), and the typed
  per-block work costs +537 (estimated +115).
- **The arithmetic that decides it:** `slp` and `wtb` exceed 261.3 on their
  loop plus `wav`'s own per-block work (229 + 38.5 = 267.5). **No per-block
  saving can bring them under the Drum ceiling.** `smp` (217) has 44 per
  sample of room, against the 55 measured.
- **The reserve (§7.5) does not cover it.** A pool slot is worth Drum's blend
  premium, about 10–12 per sample (plan §7.3 S4), so two locked slots cover
  about 20–24, against +11 to +56 here.
- **Flash:** the sync block grows from 2,996 to 9,228 B. The smp variant adds
  1,370 B and the async 354 B. Alkaline code grows by **≈ +8 KB**, against
  a headroom of **47,836 B** after P6 (measured on the P6 image).
- **ITCM:** no new ITCM code; all of it is flash.

### Decision P7-D1 (yours, before any of §3–§9 lands)

| Option | What | Measured consequence |
|---|---|---|
| **A** | Typed modes take an **Advanced** place: a `typ` other than `wav` is admitted only while an Advanced place is free, and refused otherwise. This is the §7.5 grant/refuse/release mechanics (§8), pointed at the two-Advanced budget instead of the pool. `wav` stays Basic | Every mode in the table runs. The tree records no per-voice Advanced ceiling (the audit counts two Advanced + four Basic, `CPU_USE_DSP_AUDIT.md` l. 97), so admission needs the device CPU check (§10 G-dev) |
| **B** | First cut the per-block work, then re-measure. Candidates, measured sizes: the 64-bit division (213 per block, 6.7 per sample) by an Alkaline-local cap in float (one VDIV); `osc_userBind()` every block (89 + callees) by bind-on-change plus the direct increment | `smp` could reach ≈ 258–262; `slp`/`wtb` cannot fit Basic (arithmetic above), so B still needs A for them |
| **C** | Ship `smp` only, as Basic, after B | Leaves `slp`/`wtb` out |

**Recommendation: A.**
- It is the only option that delivers all four types.
- It reuses the refusal pattern the plan already designed (§7.5) and that the
  two-Advanced rule already shows in the menu
  (`menu_instrumentTypeSelectableForMask()`).
- It leaves `wav`, today's 303, untouched at 252.4.

B is worth doing under A anyway (it lowers every typed voice by ≈ 7–10). It
is scheduled as §4.6, optional.

The code in §3–§7 is the same under every option: only the admission rule
(§8) and the gates (§10) differ.

---

## 1. Corrections the code dive makes to the plan

| # | Plan said | The tree shows | This schedule |
|---|---|---|---|
| K1 | "three fused loops (smp/slp one, wtb plain, wtb blend)" | smp and slp differ in the next index (the slp seam) and the phase update (a hold against a wrap) | **Four** wrappers: Smp, Slp, Wtb, WtbBlend. The compiler splits each wtb loop into an 8- and a 16-bit copy (the reader's `bits` test is loop-invariant), so six loops link |
| K2 | the `wav` cell: "Drum's `MOD_WAVE` dynamic range" in the user types, the ON/OFF tag in `wav` | One row cannot be both a `DTYPE_ON_OFF` tag and a `DTYPE_MENU`: the formatter, clamp and domain branch on the dtype | Row 0 becomes Drum's `osc1_wave` shape (`ROW_MENU`, `MENU_WAVEFORM`, `MOD_WAVE`), bound to `osc.waveform`. Alkaline gets its case in the three wave helpers (§3.4–§3.6). `ONOFF_TAG_ALK_WAVE` is removed; `alkWaveNames` stays |
| K3 | — | `osc_setType()` clamps `waveform` to `OSC_STOCK_LAST` under `wav` (Oscillator.c l. 1441), not to Saw/Sqr | The `typ` writer clamps an Alkaline slot to `ALK_WAVE_SQUARE` after `osc_setType()` (§3.7) |
| K4 | "the S2 bind in the `typ`/`wav` writer" | `osc_userBind()` binds only when (kind, index, generation) change and recomputes `phaseInc` every block (l. 1609–1650). The trigger's `osc_typedTriggerPhase()` binds first (l. 1709) | The async calls `osc_userBind()` in place of `osc_setTableFreq()` (same frequency product, l. 1555 / l. 1232). No writer-side bind is needed |
| K5 | "the reader's entry ≈ +20" | The readers' block constants come from five static helpers (`osc_slpBlockInc`, `osc_bindGain`, `osc_wtbFrame`, `osc_wtbFrameOf`, `osc_wtbPlanBlock`) | Five non-static wrappers in `Oscillator.c` (flash), so the fused loops take exactly the readers' values; `osc_wtbPlan_t` moves to `Oscillator.h`. No S087 loop or helper is edited (§6) |
| K6 | — | Removing `AlkalineVoice.wave` frees one byte in an 8-byte `uint8_t` run that ends at an `int32_t` | `sizeof(AlkalineVoice)` stays 548 B (P6). The reserve's per-voice flag (§8) takes that byte back |
| K7 | — | The bench's `modNode_interpClaim` stub returns 0 (`alkaline.c` l. 69) | A switchable stub (`host_claim`) for AL-X2's blend runs |

---

## 2. Work order

| Phase | Content | Gate |
|---|---|---|
| **P7-0** | **Decision P7-D1** (§0) | — |
| **P7-1** | Rows and menu (§3); voice (§4); DiodeLadder (§5); Oscillator accessors (§6) | `make -C tools/dsp_test alkaline special_tags oscillator`; AL-X2; `make all` |
| **P7-2** | Admission (§8: Option A, or the pool reserve) | Host laws AL-T; device menu check |
| **P7-3** | Tools (§9); measurement on the linked image | §10 G3–G7 |
| **P7-4** | `init.alk`, documents (§11) | `special_tags` |

---

## 3. Rows and menu

### 3.1 `Core/DSP/Instruments/Alkaline/AlkalineParameters.c` l. 144–148 — MODIFY (row 0)

```c
    /*
     * wav (S093 P7: Drum's osc1_wave shape). Under typ wav: 0 Saw (the stock
     * sawTable, the 303 ramp within 0.5 %), 1 Sqr (the D8 per-octave Q8
     * square, AlkalineSquareTable.c); under smp/slp/wtb: the catalogue item
     * index, named sNN/lNN/wNN.
     *
     * What:       DTYPE_MENU | MENU_WAVEFORM, MOD_WAVE (integer, dynamic max),
     *             bound to osc.waveform, as DrumParameters.c l. 233. Image,
     *             Morph and LFO cell like Drum's (a morph or LFO steps through
     *             the entries; under wav it switches Saw/Sqr).
     * Why:        one cell names Saw/Sqr or the item, by typ (plan §7.1). The
     *             former ON/OFF tag (ONOFF_TAG_ALK_WAVE) cannot carry the item
     *             list. The range, names and clamp come from the three wave
     *             helpers, which know the Alkaline case
     *             (instrumentManager_waveDomainMax(), menu_formatWaveCell()).
     * Inputs:     the byte. Outputs: osc.waveform.
     * Accessors:  Alkaline_calcVoiceSyncBlock() (square = typ wav && wav 1);
     *             osc_userBind() (the item index); the menu wave helpers.
     * Affiliates: ALK_WAVE_SAW / ALK_WAVE_SQUARE (AlkalineVoice.h);
     *             alkWaveNames (MenuText.h); the osc1_type row below.
     */
    ROW_MENU("osc1_wave", "Oscilltr", "Waveform", "wav", MENU_WAVEFORM, MOD_WAVE, osc.waveform, TYPE_UINT8),
```

**After l. 52 (`MOD_PM63`) — ADD** (`MOD_WAVE`, as `DrumParameters.c` l. 57–59):

```c
/* S093 P7: a wave row's domain: integers, the max from the slot's type
 * (instrumentManager_waveDomainMax()) */
#define MOD_WAVE  { 0u, 0u, (uint8_t)(INSTRUMENT_MOD_DOMAIN_INTEGER | INSTRUMENT_MOD_DOMAIN_DYNAMIC_MAX) }
```

**After `ROW_SELECT_ONOFF` (l. 72–75) — ADD** (`ROW_SELECT_SPECIAL`, as
`DrumParameters.c` l. 103–106):

```c
/*
 * A selector with a special writer (S093 P7; DrumParameters.c l. 88-106).
 *
 * What:       DTYPE_MENU | (menu << 4); AUTOMATABLE | SELECTOR (not morphed,
 *             not an LFO/velocity target); MOD_NONE; BIND_SPECIAL.
 * Why:        osc1_type is chosen, not tuned: one value per automation step;
 *             its writer is osc_setType() (IM_SPECIAL_OSC_TYPE).
 */
#define ROW_SELECT_SPECIAL(key_, cat_, long_, short_, menu_, member_, type_, special_) \
    { key_, short_, long_, cat_, (uint8_t)(DTYPE_MENU | (menu_ << 4)), \
      (INSTRUMENT_PARAM_FLAG_AUTOMATABLE | INSTRUMENT_PARAM_FLAG_SELECTOR), \
      MOD_NONE, BIND_SPECIAL(member_, type_, special_) }
```

**l. 61–75 — REMOVE** `ROW_SELECT_ONOFF` and its comment block: its only
user was the `alk_wave` row (l. 148), so no Alkaline row uses it after this
change.

**The file header, l. 20–23 — MODIFY** (missed in the first issue of this
schedule): the "Not offered" list drops `typ` and its note:

```c
 * Not offered (S092 P0 §5.5): srt (no SRR, D4), run, chn, fine pitch,
 *             velo_vol_on_off (the 303's velocity is the accent, D1).
 *             typ is offered since S093 P7 (wav / smp / slp / wtb).
```

**The `#undef` list (l. 264–276) — MODIFY:** `#undef ROW_SELECT_ONOFF`
becomes `#undef ROW_SELECT_SPECIAL`, and `#undef MOD_WAVE` is added after
`#undef MOD_PM63`. Every macro the file defines stays undefined after the
tables, as today.

### 3.2 `AlkalineParameters.c` — the enum, the new row, the page

**Enum (after P6's `ALK_PARAM_OSC1_POSITION`, l. 126) — ADD:**

```c
    ALK_PARAM_OSC1_TYPE,         /* 28 (S093 P7): wav / smp / slp / wtb */
    ALK_PARAM_COUNT
```

**Row (after P6's `osc1_position` row, l. 208) — ADD:**

```c
    /*
     * typ: the oscillator type (S093 P7, as DrumParameters.c l. 335).
     *
     * What:       wav (the 303's Saw/Sqr, fused table read), smp (one-shot
     *             sample), slp (looped sample), wtb (wavetable; pos is the
     *             frame position). The writer is osc_setType() through
     *             IM_SPECIAL_OSC_TYPE; for an Alkaline slot it then clamps wav
     *             to Saw/Sqr (InstrumentManager.c, §3.7).
     * Why:        plan §7.1; the class question is P7-D1 (§0, §8).
     * Accessors:  Alkaline_triggerVoice(), Alkaline_calcVoiceAsync(),
     *             Alkaline_calcVoiceSyncBlock() (osc_isTyped()).
     */
    ROW_SELECT_SPECIAL("osc1_type", "Oscilltr", "Type", "typ", MENU_OSC_TYPE, osc.oscType, TYPE_UINT8, IM_SPECIAL_OSC_TYPE | IM_SPECIAL_OSC1),
```

**`AlkalineParameters.h` l. 22 — MODIFY:** `#define ALKALINE_PARAM_DESCRIPTOR_COUNT 29u`.
Its comment adds "osc1_type (S093 P7)".

**Page 0 (P6: `coa tun wav pos`, l. 239–240) — MODIFY** (Drum's screen 2 places `typ` at
cell 4, `DrumParameters.c` l. 396–398):

```c
    /* 0 OSC: coa tun wav pos | typ (S093 P7: Drum's screen 2) */
    {{ ALK_PARAM_OSC1_PITCH_COARSE, ALK_PARAM_TUNING, ALK_PARAM_WAVE, ALK_PARAM_OSC1_POSITION, ALK_PARAM_OSC1_TYPE, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY }},
```

**`check_special_tags.py` — NONE**, checked against `expected()` (l. 37–55):
- `osc1_type` expects `{IM_SPECIAL_OSC_TYPE, IM_SPECIAL_OSC1}`, which the row
  carries;
- `osc1_wave` is a `ROW_MENU`, untagged.

The firmware classifier (`InstrumentManager.c` l. 3987–4034) returns the same
values: `osc1_` gives OSC1 for every type.

**The file header comment (l. 27):** "MenuText.h (alkWaveNames,
ONOFF_TAG_ALK_WAVE)" becomes "MenuText.h (alkWaveNames)".

### 3.3 `Core/Menu/MenuText.h` l. 225–238 — REMOVE; l. 239–252 — MODIFY

- **REMOVE** the `ONOFF_TAG_ALK_WAVE` comment block and `#define`
  (l. 225–238). No row carries tag 3 after §3.1, and an unknown tag falls to
  `oscRunNames` (`menu.c` l. 12359).
- **MODIFY** the `alkWaveNames` comment:

```c
/*
 * Alkaline waveform names (S093 P4; S093 P7: the osc1_wave cell under wav).
 *
 * What:       [0] = count (2), then Saw, Sqr.
 * Why:        an Alkaline slot's wav list under typ wav is these two
 *             (ALK_WAVE_SAW 0, ALK_WAVE_SQUARE 1); menu_formatWaveCell() names
 *             them, instrumentManager_waveDomainMax() limits the range to them.
 *             The count must equal ALK_WAVE_SQUARE + 1 (asserted in menu.c).
 * Accessors:  menu_formatWaveCell() (menu.c).
 */
```

### 3.4 `Core/Menu/menu.c` — the tag and the formatter

- **l. 12336–12362 (`menu_onoffTagNames()`) — MODIFY:** remove
  `case ONOFF_TAG_ALK_WAVE:    return alkWaveNames;` and the
  "ONOFF_TAG_ALK_WAVE -> alkWaveNames (Saw, Sqr);" line and the
  `alkWaveNames` affiliate from its comment.
- **l. 12505–12508 — MODIFY** (the assert stays, its comment names the new
  user):
  ```c
  /* S093 P4/P7: the Alkaline wav names list both values of an Alkaline slot
   * under typ wav (ALK_WAVE_SAW 0, ALK_WAVE_SQUARE 1, AlkalineVoice.h). */
  ```
- **`menu_formatWaveCell()` l. 12148–12176 — ADD** the Alkaline case before
  `if (isOsc1 && type != OSC_TYPE_WAV) {`:
  ```c
      /*
       * S093 P7: an Alkaline slot's osc1 under wav lists Saw, Sqr.
       *
       * What:       value 0 -> "Saw", 1 -> "Sqr" (alkWaveNames), no code;
       *             any other value names nothing (blank, "---" in three
       *             characters), as a Drum value past its list.
       * Why:        the stock list (waveformNames: Sin, Tri, ...) is not
       *             Alkaline's; the Alkaline voice plays Saw for 0 and Sqr
       *             for 1 (Alkaline_calcVoiceSyncBlock()).
       * Accessors:  menu_formatWaveValue3() (compact cells, overlays, the
       *             voice automation display), the single-parameter painter.
       */
      if (isOsc1 && type == OSC_TYPE_WAV &&
          instrumentManager_runtimeType(cell->slot) == INSTRUMENT_TYPE_ALK) {
          if (value < (uint8_t)alkWaveNames[0][0])
              memcpy(nameDst, alkWaveNames[value + 1u], 3u);
          return;
      }
  ```
  `instrumentManager_runtimeType()` is public (`InstrumentManager.h` l. 891).

**NONE**, verified site by site:

| Site | Why no change |
|---|---|
| `MENU_WAVEFORM` sites 3408, 6590 | go through `menu_formatWaveValue3()` → the formatter above |
| 5360 (the typ-switch clamp), 6888 (the edit count) | go through `instrumentManager_waveCellCount()` (§3.5) |
| 15147 (the edit painter) | calls `menu_formatWaveCell()` |
| Voice automation | `menu_formatVoiceAutomationValue3()` (l. 13984–14012) builds a cell and calls `menu_formatWaveValue3()` |
| 13929 (`menu_formatAutomationValue3()`) | is reached for wave rows only without a voice cell (effects) |
| 12084 (`getMaxEntriesForMenu`) | the cell-less stock count, as for Drum |
| Automation value range | `menu_automationValueMax()` for `MENU_WAVEFORM` is 127 (default, l. 13853–13875), as Drum: an Alkaline `wav` value of 2..127 shows `---` and plays Saw |

### 3.5 `Core/DSP/Instruments/InstrumentManager.c` l. 838–857 — MODIFY (`instrumentManager_waveDomainMax()`)

```c
        if (strcmp(descriptor->file_key, "osc1_wave") == 0 &&
            slot < INSTRUMENT_SLOT_COUNT) {
            const OscInfo *osc = instrumentManager_osc(slot, "osc1_wave");
            const uint8_t count = osc ? osc_userCount(osc->oscType)
                                      : (uint8_t)OSC_SAMPLE_START;

            /* S093 P7: an Alkaline slot under wav has two entries (Saw, Sqr):
             * the LFO/velocity range, the menu clamp, the edit count and the
             * typ-switch clamp (menu.c l. 5360) all take it from here */
            if (osc && osc->oscType == OSC_TYPE_WAV &&
                instrumentManager_slotType(slot) == INSTRUMENT_TYPE_ALK)
                return (uint8_t)ALK_WAVE_SQUARE;
            return count ? (uint8_t)(count - 1u) : 0u;
        }
```

`AlkalineVoice.h` is included at l. 16, so `ALK_WAVE_SQUARE` is visible.
`instrumentManager_osc()` already resolves `osc1_` for `INSTRUMENT_TYPE_ALK`
(l. 2380–2384).

The function comment (l. 820–836) adds: "an Alkaline slot under wav:
ALK_WAVE_SQUARE (Saw, Sqr)".

### 3.6 `instrumentManager_waveCellCount()` (l. 872–876), `instrumentManager_osc1Type()` (l. 887–892) — NONE

The first is waveDomainMax + 1, so 2 for Alkaline `wav`. The second already
resolves the Alkaline oscillator.

### 3.7 `InstrumentManager.c` l. 3881–3899 — MODIFY (the `IM_SPECIAL_OSC_TYPE` writer)

```c
        OscInfo *osc = instrumentManager_oscBySelector(
            slot, (uint8_t)(special & IM_SPECIAL_OSC_MASK));
        if (!osc) return 0u;
        osc_setType(osc, byteValue);
        /*
         * S093 P7: an Alkaline slot's wav list is Saw, Sqr.
         *
         * What:       after osc_setType() (which clamps wav to the stock list,
         *             OSC_STOCK_LAST), an Alkaline oscillator under wav is
         *             clamped to ALK_WAVE_SQUARE: an item index past 1 becomes
         *             Sqr, the last entry, as Drum's clamp takes the last.
         * Why:        typ wav after smp with item 5 would otherwise keep 5,
         *             which names nothing and plays Saw.
         * Affiliates: instrumentManager_waveDomainMax() (the same rule for the
         *             range); menu.c l. 5323-5360 (clamps the stored images).
         */
        if (instrumentManager_slotType(slot) == INSTRUMENT_TYPE_ALK &&
            osc->oscType == OSC_TYPE_WAV && osc->waveform > ALK_WAVE_SQUARE)
            osc->waveform = ALK_WAVE_SQUARE;
        instrumentManager_waveDomainChanged(slot);
        return 1u; }
```

Under Option A (§8) the admission check precedes `osc_setType()`.

**Load order — NONE.** Rows apply in index order: `osc1_wave` (0) then
`osc1_type` (28), which is Drum's order (row 0, then the `typ` row), so
`osc_setType()` clamps the stored item index to the new type's count after the
byte is written.

### 3.8 `tools/alkaline/make_init_alk.py` l. 47 and l. 57 — MODIFY (`INIT`)

- `"alk_wave": "0"` → `"osc1_wave": "0"`;
- ADD `"osc1_type": "0"` (wav).

This gives 29 `[params]` keys. `osc1_wave` is now an image row, so it is
written under `[morph]` too: 23 `[morph]` rows, against 28 / 22 in the landed
P6 `init.alk` (verified).

---

## 4. The voice

### 4.1 `AlkalineVoice.h` — MODIFY

- **l. 56–57:** the comment on `ALK_WAVE_SAW` / `ALK_WAVE_SQUARE` becomes
  "osc.waveform under typ wav (S093 P7; was AlkalineVoice.wave)".
- **l. 61:** `OscInfo osc; /* VCO: typ (oscType), wav (waveform: Saw/Sqr under wav, the item under smp/slp/wtb), pos */`.
- **l. 79 — REMOVE:** `uint8_t wave;              /* ALK_WAVE_* */`. Size
  unchanged (K6).

### 4.2 `AlkalineVoice.c` after l. 18 (`ALK_SAW_SCALE_V`) — ADD (the user-source scale)

```c
/* S093 P7: a user item (sample, wavetable) enters the ladder at the saw's
 * scale: a full-scale item swings as far as the stock saw (+-32,625 LSB =
 * the 303 saw's 5.35 V), times this block's drive */
#define ALK_USER_SCALE_V ALK_SAW_SCALE_V
```

### 4.3 `Alkaline_initVoice()` l. 25 and l. 52 — MODIFY / REMOVE

```c
	voice->osc.oscType = OSC_TYPE_WAV;     /* S093 P7: typ wav (init.alk osc1_type 0) */
	voice->osc.waveform = ALK_WAVE_SAW;    /* S093 P7: wav Saw (was the stock SAW, 2, for the octave index; osc_setTableFreq() never read it) */
```

- **REMOVE** l. 52: `voice->wave = ALK_WAVE_SAW;`.
- `osc_setTableFreq()` does not read `waveform` (Oscillator.c l. 1225–1239).
  `calcWavetableOscBlock()` does not read it either. Nothing in the platform
  reads an Alkaline `waveform` as a stock wave: `InstrumentManager.c` has no
  `waveform` reads for oscillators.

### 4.4 `Alkaline_triggerVoice()` l. 110–140 — MODIFY (P6's non-legato branch)

```c
		alkVeg_trigger(&voice->veg, voice->attack);
		if (osc_isTyped(&voice->osc)) {
			/*
			 * S093 P7: a typed note starts at the item's pos point.
			 *
			 * What:       osc_typedTriggerPhase() binds first (an item changed
			 *             on this step starts correctly), then smp/slp start at
			 *             pos/128 of the item (slp: inside its loop), wtb at
			 *             phase 0 (pos is its frame position); positionApplied =
			 *             pos, so the next block does not glide.
			 * Why:        Drum's typed start (DrumVoice.c l. 353-354); a legato
			 *             note keeps the playhead (no branch taken), as the
			 *             303's VCO keeps running through a slide.
			 * Affiliates: osc_typedTriggerPhase() (Oscillator.c l. 1689).
			 */
			voice->osc.phase = osc_typedTriggerPhase(&voice->osc);
		} else {
			/* S093 P6: pos on Saw/Sqr (S093_P6_IMPLEMENTATION.md §7.1) */
			const uint8_t pos = (uint8_t)(voice->osc.position & 127u);

			if (pos != 0u)
				voice->osc.phase = (uint32_t)pos << 25;
			voice->osc.positionApplied = pos;
		}
	}
```

### 4.5 `Alkaline_calcVoiceAsync()` l. 182–216 — MODIFY (P6's pitch tail: `osc_setTableFreq()` l. 184 and the bend block)

```c
	voice->osc.freq = alk_exp2f(voice->pitchLog2
	                        + (voice->tuning * 127.0f - 63.0f) * (7.0f / 12.0f / 63.0f));
	if (osc_isTyped(&voice->osc)) {
		/*
		 * S093 P7 S2 + S3: the typed pitch and pos.
		 *
		 * What:       osc_userBind() re-reads the item only when kind, index or
		 *             the catalogue generation changed, and sets phaseInc from
		 *             f = freq x pitchMod x modNodeValue (the product
		 *             osc_setTableFreq() uses): wtb by the cycle law
		 *             freq2PhaseIncr(f), smp/slp by f x 2^22 / REAL_FS
		 *             (Q19.12 frames per sample). osc_typedPositionApply(.., 1)
		 *             then glides pos: smp/slp bend phaseInc by d x frames
		 *             (capped, D3-D5); wtb moves the frame position at most one
		 *             frame per block, phaseInc untouched.
		 * Why:        no restore (S3): phaseInc is recomputed here every block,
		 *             so the bend clears itself; positionBendStep is read only by
		 *             the restore (smp/slp) and by osc_wtbPlanBlock() right after
		 *             this call (wtb), so leaving it set is exact.
		 * Cost:       the measured +355 per block of §0 (the 64-bit cap
		 *             division 213 of it, smp/slp only).
		 * Affiliates: osc_userBind() l. 1552; osc_typedPositionApply() l. 1753;
		 *             alk_renderTyped() (reads the bind).
		 */
		osc_userBind(&voice->osc);
		osc_typedPositionApply(&voice->osc, 1u);
	} else {
		osc_setTableFreq(&voice->osc);
		/* S093 P6: the Saw/Sqr pos bend (S093_P6_IMPLEMENTATION.md §7.2) */
		const uint32_t u = ((uint32_t)(voice->osc.position & 127u) -
		                    (uint32_t)voice->osc.positionApplied + 64u) & 127u;
		const int32_t d = (int32_t)u - 64;

		voice->osc.positionApplied =
			(uint8_t)(((uint32_t)voice->osc.positionApplied + (uint32_t)d) & 127u);
		voice->osc.phaseInc += (uint32_t)(d * (int32_t)OSC_POS_BEND_PER_STEP);
	}
```

**`pos` legato rule — NONE.** A slide keeps the playhead, and pos keeps
gliding per block.

### 4.6 Optional per-block savings (Option B; only if you choose it)

Measured sizes for both items are in §0. Neither is in the code above. Each
would get its own bench law against Drum's function before it lands.

- **B1:** an Alkaline-local copy of `osc_typedPositionApply()`'s smp/slp cap
  with the speed limit in float:
  `maxStep = (int32_t)((float)OSC_POS_SAMPLE_SPEED_CAP * (float)phaseInc / (float)frames)`.
  That is one VDIV against 213 instructions. Above 2^24 the float operands
  round, so the cap can differ by 1 from Drum's at a boundary. The law states
  the tolerance.
- **B2:** bind only on a change of (oscType, waveform, generation), as
  `osc_userBind()`'s test, and compute `phaseInc` inline. That saves the call
  and the generic dispatch (89 + callees).

### 4.7 `Alkaline_calcVoiceSyncBlock()` l. 254–279 — MODIFY; `alk_renderTyped()` — ADD (before it)

```c
/*
 * The user sources' silent block (S093 P7).
 *
 * What:       two zero frames: the smp fused loop over them with end 0 reads 0
 *             at every sample and holds its (local) phase at 0.
 * Why:        osc_renderTyped() writes a zero block when nothing is bound or
 *             the item has no data; B2R over a zero block equals B2R over the
 *             smp read of these frames bit for bit (s = 0 x sScale = +0), so
 *             no fifth loop (the plain B2R, 1,244 B) is linked for that case.
 */
static const int16_t alk_zeroFrames[2] = { 0, 0 };

/*
 * One block of a user oscillator type through its fused B2R loop (S093 P7).
 *
 * What:       fills a DlSrc from the bound catalogue item, exactly as the
 *             typed reader reads it this block, and calls the variant:
 *             - smp: data, end = frames << 12, the item gain;
 *             - slp: data, the loop, the item gain; the step is the reader's
 *               osc_slpBlockInc() of this block's (bent) phaseInc;
 *             - wtb: with two or more frames and an interpolation slot granted
 *               (modNode_interpClaim(), as osc_wtbBlendBlock()), the blend of
 *               the plan's frame pair(s); else the nearest frame;
 *             - nothing bound, or no data: the silent block.
 * Why:        plan §7.3 S1/S4: one fused loop per source, each bit-identical
 *             to osc_renderTyped(gain 1) + plain B2R (bench law AL-X2); the
 *             per-block dispatch keeps every loop free of a source branch.
 * Inputs:     voice (bound by Alkaline_calcVoiceAsync()); buf (output).
 * Outputs:    buf; voice->osc.phase; the ladder state; a slot grant.
 * Accessors:  Alkaline_calcVoiceSyncBlock().
 * Affiliates: osc_renderTyped() and its readers (Oscillator.c l. 1921, 1965,
 *             2855, 2901); osc_*For() accessors (§6);
 *             DiodeLadder_renderB2Smp/Slp/Wtb/WtbBlend() (§5).
 */
static void alk_renderTyped(AlkalineVoice *voice, int16_t *buf)
{
	OscInfo *const osc = &voice->osc;
	/* a full-scale item enters at the saw's level, times this block's drive */
	const float sScale = ALK_USER_SCALE_V * voice->driveGain;
	/* the ladder current and the OTA current (with the drive make-up), as the
	 * wav path passes them */
	const float iLad = voice->iLadEnd;
	const float iAbc = voice->iAbcEnd * voice->makeup;
	DlSrc src;

	switch (osc->sampleCacheLooped) {
	case OSC_BIND_SMP:
		/* osc_smpPlainBlock()'s guard: data and a length */
		if (osc->sampleCacheData && osc->sampleCacheSize != 0u) {
			src.d16 = (const int16_t *)osc->sampleCacheData;
			/* the playhead holds on the item's first pad frame */
			src.endPh = osc->sampleCacheSize << OSC_SAMPLE_PHASE_SHIFT;
			/* gain 1 x the item's gain code (the reader's g = gain x bind) */
			src.gain = osc_bindGainFor(osc);
			DiodeLadder_renderB2Smp(&voice->ladder, buf, &src, &osc->phase,
			                        osc->phaseInc, sScale, iLad, iAbc);
			return;
		}
		break;
	case OSC_BIND_SLP: {
		const sample_catalogue_entry_t *e = sampleMemory_sampleEntry(osc->sampleCacheIndex);
		/* the reader's loop: the entry's, or the whole item without one */
		const uint32_t ls = e ? e->loop_start : 0u;
		const uint32_t le = e ? e->loop_end : osc->sampleCacheSize;

		/* osc_slpPlainBlock()'s guard: data, a length, a loop */
		if (osc->sampleCacheData && osc->sampleCacheSize != 0u && le != ls) {
			src.d16 = (const int16_t *)osc->sampleCacheData;
			src.ls = ls;
			src.le = le;
			/* Q19.12 loop length and end: the wrap subtracts one length */
			src.lenPh = (le - ls) << OSC_SAMPLE_PHASE_SHIFT;
			src.loopEndPh = le << OSC_SAMPLE_PHASE_SHIFT;
			src.gain = osc_bindGainFor(osc);
			/* the block step: forward modulo the loop, or a backward glide as
			 * the equal forward step (osc_slpBlockInc(), F5) */
			DiodeLadder_renderB2Slp(&voice->ladder, buf, &src, &osc->phase,
			                        osc_slpBlockIncFor(osc->phaseInc, osc->phase,
			                                           ls << OSC_SAMPLE_PHASE_SHIFT,
			                                           src.lenPh),
			                        sScale, iLad, iAbc);
			return;
		}
		break; }
	case OSC_BIND_WTB8:
	case OSC_BIND_WTB16: {
		const wavetable_catalogue_entry_t *wt = sampleMemory_wavetableEntry(osc->sampleCacheIndex);

		/* osc_wtbPlainBlock()'s guard: an entry and data */
		if (wt && osc->sampleCacheData) {
			const uint32_t count = wt->frame_count;

			src.len = wt->frame_len;
			src.bits = wt->bits;
			/* S4: the blend only with one of the two system slots; the claim
			 * is the reader's (sticky across blocks), evaluated only for two
			 * or more frames, as osc_wtbBlendBlock()'s guard order */
			if (count >= 2u && modNode_interpClaim(osc)) {
				osc_wtbPlan_t plan;

				/* the block's glide from positionApplied - positionBendStep to
				 * positionApplied, in 1 or 2 segments (osc_wtbPlanBlock()) */
				osc_wtbPlanFor(osc, count, &plan);
				src.fa = osc_wtbFrameFor(osc, wt, plan.pair[0]);
				src.fb = osc_wtbFrameFor(osc, wt, plan.pair[0] + 1u);
				src.ff = plan.ff[0];
				src.step = plan.step;
				if (plan.segments == 2u) {
					/* the glide crosses a frame at sample end[0] */
					src.seg = plan.end[0];
					src.fa2 = osc_wtbFrameFor(osc, wt, plan.pair[1]);
					src.fb2 = osc_wtbFrameFor(osc, wt, plan.pair[1] + 1u);
					src.ff2 = plan.ff[1];
				} else {
					/* one segment: the switch sample is never reached */
					src.seg = OUTPUT_DMA_SIZE;
					src.fa2 = src.fa;
					src.fb2 = src.fb;
					src.ff2 = src.ff;
				}
				DiodeLadder_renderB2WtbBlend(&voice->ladder, buf, &src, &osc->phase,
				                             osc->phaseInc, sScale, iLad, iAbc);
				return;
			}
			/* no slot or one frame: the nearest frame (the Q9 fallback) */
			src.fa = osc_wtbFrameFor(osc, wt, osc_wtbNearestFrame(osc->positionApplied, count));
			DiodeLadder_renderB2Wtb(&voice->ladder, buf, &src, &osc->phase,
			                        osc->phaseInc, sScale, iLad, iAbc);
			return;
		}
		break; }
	default:
		break;
	}
	/* nothing bound or no data: the silent block; the oscillator's phase is
	 * not advanced (osc_renderTyped()'s zero block leaves it) */
	{
		uint32_t still = 0u;

		src.d16 = alk_zeroFrames;
		src.endPh = 0u;
		src.gain = 1.0f;
		DiodeLadder_renderB2Smp(&voice->ladder, buf, &src, &still, 0u, sScale, iLad, iAbc);
	}
}

void Alkaline_calcVoiceSyncBlock(AlkalineVoice *voice, int16_t *buf, const uint8_t size)
{
	/* S093 P7: Saw/Sqr is typ wav with wav 1 (was AlkalineVoice.wave) */
	const uint8_t square = (uint8_t)(voice->osc.waveform == ALK_WAVE_SQUARE);

	(void)size;   /* the ladder always renders OUTPUT_DMA_SIZE */
	/* S093 P7: a user type takes its own fused loop; the branch is per block,
	 * outside every loop */
	if (osc_isTyped(&voice->osc)) {
		alk_renderTyped(voice, buf);
		return;
	}
	/* ... today's body from "B2R (user, S093)" through #endif, unchanged ... */
}
```

`AlkalineVoice.c` reaches `sampleMemory_*` (through `Oscillator.h` l. 45),
`modNode_interpClaim()` (`modulationNode.h`, included at l. 11) and `DlSrc`
(through `AlkalineVoice.h` → `DiodeLadder.h`).

---

## 5. `Core/DSPAudio/DiodeLadder.h` / `.c` — the fused readers (S1)

### 5.1 `DiodeLadder.h` before l. 163 (`DiodeLadder_renderB`) — ADD

```c
/*
 * One block's user-source read for a fused B2R loop (S093 P7, S1).
 *
 * What:       the per-block constants of the oscillator reader that a fused
 *             variant inlines; the caller fills it once per block from the
 *             bound catalogue item; the phase lives in the oscillator (the
 *             wrappers' phase pointer) and its step is their phaseInc.
 * Why:        plan §7.3 S1: the read fused into the ladder loop, as
 *             DiodeLadder_renderB2Osc() fused the table read (-9 per sample,
 *             AL-X); one compile-time variant per source, no source branch
 *             inside the loop.
 * Fields:     d16      smp/slp: the int16 frames (the item's base);
 *             endPh    smp: frames << 12, where the playhead holds;
 *             ls, le   slp: loop start / end frames (the seam reads ls);
 *             loopEndPh, lenPh  slp: le << 12 and (le - ls) << 12;
 *             gain     smp/slp: the item's gain code (osc_bindGainFor());
 *             fa, fb   wtb: frame base(s) (plain: fa only; blend: the pair);
 *             fa2, fb2 wtb blend: the second segment's pair;
 *             seg      wtb blend: the first sample of segment 2
 *                      (OUTPUT_DMA_SIZE when the block has one segment);
 *             ff, ff2, step  wtb blend: the frame fraction at each segment's
 *                      first sample and its per-sample change;
 *             len, bits  wtb: frame length (points) and depth (8 or 16).
 * Accessors:  DiodeLadder_renderB2Smp/Slp/Wtb/WtbBlend(); filled by
 *             AlkalineVoice.c alk_renderTyped().
 * Affiliates: Oscillator.c osc_smpPlainBlock(), osc_slpPlainBlock(),
 *             osc_wtbPlainBlock(), osc_wtbBlendBlock() (the arithmetic each
 *             variant reproduces bit for bit; bench law AL-X2).
 */
typedef struct {
	const int16_t *d16;
	uint32_t endPh;
	uint32_t ls, le, loopEndPh, lenPh;
	float    gain;
	const uint8_t *fa, *fb, *fa2, *fb2;
	uint32_t len;
	uint8_t  bits;
	uint8_t  seg;
	float    ff, ff2, step;
} DlSrc;

/* S093 P7: B2R with a user source's read fused (see DlSrc); buf is output
 * only; phase/phaseInc are the oscillator's (slp: the block step from
 * osc_slpBlockIncFor()). */
void DiodeLadder_renderB2Smp(DiodeLadder *dl, int16_t *buf, const DlSrc *src,
                             uint32_t *phase, uint32_t phaseInc, float sScale,
                             float iLadEnd, float iAbcEnd);
void DiodeLadder_renderB2Slp(DiodeLadder *dl, int16_t *buf, const DlSrc *src,
                             uint32_t *phase, uint32_t phaseInc, float sScale,
                             float iLadEnd, float iAbcEnd);
void DiodeLadder_renderB2Wtb(DiodeLadder *dl, int16_t *buf, const DlSrc *src,
                             uint32_t *phase, uint32_t phaseInc, float sScale,
                             float iLadEnd, float iAbcEnd);
void DiodeLadder_renderB2WtbBlend(DiodeLadder *dl, int16_t *buf, const DlSrc *src,
                                  uint32_t *phase, uint32_t phaseInc, float sScale,
                                  float iLadEnd, float iAbcEnd);
```

The file header's "Accessors" line (l. 34) stays ("AlkalineVoice.c only").

### 5.2 `DiodeLadder.c` l. 269–281 — MODIFY (the body's head)

Replace the `dl_renderB2Body(...)` signature and its first comment/line with:

```c
/* the source of a B2R block (a compile-time constant in each wrapper):
 * BUF reads buf (renderB2), TABLE the [11][1024] row (renderB2Osc), SMP/SLP
 * a sample item, WTB one wavetable frame, WTBB a frame pair blended */
enum { DL_SRC_BUF = 0, DL_SRC_TABLE, DL_SRC_SMP, DL_SRC_SLP, DL_SRC_WTB, DL_SRC_WTBB };

/*
 * osc_wtbRead() (Oscillator.c l. 2699) exactly (S093 P7).
 *
 * What:       one interpolated point at a cycle phase: index = (ph x len) >>
 *             32, so 2^32 is one frame of any length; the fraction is the low
 *             word of that product; the last point wraps to 0; 8-bit data x 256.
 * Why:        the fused wtb loops must reproduce the reader bit for bit; the
 *             reader's helper is static in Oscillator.c and stays unedited.
 */
static inline __attribute__((always_inline)) float dl_wtbRead(
	const uint8_t *data, uint8_t bits, uint32_t len, uint32_t ph)
{
	const uint64_t m = (uint64_t)ph * len;          /* frame position, Q32 */
	const uint32_t idx = (uint32_t)(m >> 32);       /* the point at or before ph */
	const uint32_t n = (idx + 1u == len) ? 0u : idx + 1u;   /* its successor, wrapping */
	const float fr = (float)(uint32_t)m * (1.0f / 4294967296.0f);   /* 0..1 between them */
	int32_t a;
	int32_t b;

	if (bits == 8u) {
		a = ((const int8_t *)data)[idx] << 8;
		b = ((const int8_t *)data)[n] << 8;
	} else {
		a = ((const int16_t *)data)[idx];
		b = ((const int16_t *)data)[n];
	}
	return (float)a + fr * (float)(b - a);
}

static inline __attribute__((always_inline)) void dl_renderB2Body(
	DiodeLadder *dl, int16_t *buf, const float sScale, const float iLadEnd,
	const float iAbcEnd, const int src, const int16_t *const row,
	const DlSrc *const sp, uint32_t *const phase, const uint32_t phaseInc)
{
	/* src (a compile-time constant in each wrapper): DL_SRC_BUF reads buf;
	 * every other source is the oscillator's read fused into the loop, its
	 * reader's arithmetic exactly (linear interpolation, truncation to int16,
	 * then x sScale as the separate path does) */
	const int fused = (src != DL_SRC_BUF);
	uint32_t ph = fused ? *phase : 0u;
	/* the source's block constants, hoisted out of the loop; each is a
	 * compile-time 0 for the sources that do not use it */
	const int16_t *const d16 = (src == DL_SRC_SMP || src == DL_SRC_SLP) ? sp->d16 : 0;
	const uint32_t endPh = (src == DL_SRC_SMP) ? sp->endPh : 0u;
	const uint32_t ls = (src == DL_SRC_SLP) ? sp->ls : 0u;
	const uint32_t le = (src == DL_SRC_SLP) ? sp->le : 0u;
	const uint32_t loopEndPh = (src == DL_SRC_SLP) ? sp->loopEndPh : 0u;
	const uint32_t lenPh = (src == DL_SRC_SLP) ? sp->lenPh : 0u;
	const float g = (src == DL_SRC_SMP || src == DL_SRC_SLP) ? sp->gain : 1.0f;
	const uint8_t *fa = (src == DL_SRC_WTB || src == DL_SRC_WTBB) ? sp->fa : 0;
	const uint8_t *fb = (src == DL_SRC_WTBB) ? sp->fb : 0;
	const uint32_t wlen = (src == DL_SRC_WTB || src == DL_SRC_WTBB) ? sp->len : 0u;
	const uint8_t bits = (src == DL_SRC_WTB || src == DL_SRC_WTBB) ? sp->bits : 0u;
	const uint8_t seg = (src == DL_SRC_WTBB) ? sp->seg : 0u;
	float ff = (src == DL_SRC_WTBB) ? sp->ff : 0.0f;          /* the blend fraction */
	const float ffStep = (src == DL_SRC_WTBB) ? sp->step : 0.0f;
```

The rest of the declarations (l. 282–298) are unchanged.

### 5.3 `DiodeLadder.c` l. 307–317 — MODIFY (the per-sample source read)

```c
		if (src == DL_SRC_TABLE) {
			/* calcWavetableOscBlock() (gain 1), unchanged from S093 P3 */
			const uint32_t itg = ph >> 22;
			int16_t o = row[itg];

			o += (ph & 0x003FFFFFu) * 2.384185791015625e-07f
			   * (row[(itg + 1u) & 0x000003FFu] - o);
			ph += phaseInc;
			s = (float)o * sScale;
		} else if (src == DL_SRC_SMP) {
			/* osc_smpPlainBlock(): Q19.12 (frame = ph >> 12, fraction = the
			 * low 12 bits / 4096); the two pad frames read 0; the playhead
			 * holds at endPh */
			const uint32_t idx = ph >> 12;
			const int16_t a = d16[idx];
			const int16_t b = d16[idx + 1u];
			const float out = (float)a + (float)(ph & 0xFFFu) * (1.0f / 4096.0f) * (float)(b - a);
			const uint32_t nx = ph + phaseInc;

			/* truncated to int16 as the reader stores it, then volts */
			s = (float)(int16_t)(out * g) * sScale;
			ph = (nx > endPh) ? endPh : nx;
		} else if (src == DL_SRC_SLP) {
			/* osc_slpPlainBlock(): the seam reads loop_start (A6); one wrap at
			 * loop_end with the block step (phaseInc = osc_slpBlockIncFor()) */
			const uint32_t idx = ph >> 12;
			const uint32_t nxt = (idx + 1u == le) ? ls : idx + 1u;
			const int16_t a = d16[idx];
			const int16_t b = d16[nxt];
			const float out = (float)a + (float)(ph & 0xFFFu) * (1.0f / 4096.0f) * (float)(b - a);

			s = (float)(int16_t)(out * g) * sScale;
			ph += phaseInc;
			if (ph >= loopEndPh)
				ph -= lenPh;
		} else if (src == DL_SRC_WTB) {
			/* osc_wtbPlainBlock() at gain 1: the nearest frame */
			s = (float)(int16_t)dl_wtbRead(fa, bits, wlen, ph) * sScale;
			ph += phaseInc;
		} else if (src == DL_SRC_WTBB) {
			/* osc_wtbBlendBlock() at gain 1: the frame pair mixed by ff, which
			 * steps per sample; from sample seg on, the second segment's pair
			 * and its own starting ff (the reader restarts ff per segment) */
			if (j == seg) {
				fa = sp->fa2;
				fb = sp->fb2;
				ff = sp->ff2;
			}
			{
				const float y0 = dl_wtbRead(fa, bits, wlen, ph);
				const float y1 = dl_wtbRead(fb, bits, wlen, ph);

				s = (float)(int16_t)(y0 + ff * (y1 - y0)) * sScale;
			}
			ff += ffStep;
			ph += phaseInc;
		} else {
			s = (float)buf[j] * sScale;
		}
```

The gain-1 sources drop the reader's `* gain`: `x * 1.0f == x` exactly, so
the truncation is the same.

### 5.4 `DiodeLadder.c` l. 406–419 — MODIFY (the two wrappers' calls) and ADD (four wrappers)

```c
	dl_renderB2Body(dl, buf, sScale, iLadEnd, iAbcEnd, DL_SRC_BUF, 0, 0, 0, 0u);
```

```c
	dl_renderB2Body(dl, buf, sScale, iLadEnd, iAbcEnd, DL_SRC_TABLE, row, 0, phase, phaseInc);
}

/* S093 P7 S1: the four user-source variants (DlSrc; AlkalineVoice.c
 * alk_renderTyped()); each is the body with one compile-time source */
void DiodeLadder_renderB2Smp(DiodeLadder *dl, int16_t *buf, const DlSrc *src,
                             uint32_t *phase, const uint32_t phaseInc, const float sScale,
                             const float iLadEnd, const float iAbcEnd)
{
	dl_renderB2Body(dl, buf, sScale, iLadEnd, iAbcEnd, DL_SRC_SMP, 0, src, phase, phaseInc);
}

void DiodeLadder_renderB2Slp(DiodeLadder *dl, int16_t *buf, const DlSrc *src,
                             uint32_t *phase, const uint32_t phaseInc, const float sScale,
                             const float iLadEnd, const float iAbcEnd)
{
	dl_renderB2Body(dl, buf, sScale, iLadEnd, iAbcEnd, DL_SRC_SLP, 0, src, phase, phaseInc);
}

void DiodeLadder_renderB2Wtb(DiodeLadder *dl, int16_t *buf, const DlSrc *src,
                             uint32_t *phase, const uint32_t phaseInc, const float sScale,
                             const float iLadEnd, const float iAbcEnd)
{
	dl_renderB2Body(dl, buf, sScale, iLadEnd, iAbcEnd, DL_SRC_WTB, 0, src, phase, phaseInc);
}

void DiodeLadder_renderB2WtbBlend(DiodeLadder *dl, int16_t *buf, const DlSrc *src,
                                  uint32_t *phase, const uint32_t phaseInc, const float sScale,
                                  const float iLadEnd, const float iAbcEnd)
{
	dl_renderB2Body(dl, buf, sScale, iLadEnd, iAbcEnd, DL_SRC_WTBB, 0, src, phase, phaseInc);
}
```

**Measured (scratch, `arm-none-eabi-gcc -Ofast`):**
- `DiodeLadder_renderB2` and `DiodeLadder_renderB2Osc` compile to identical
  instructions before and after (316 and 343 lines of disassembly, `cmp`
  equal; loops 198 and 213).
- Standalone loops: Smp 217, Slp 229, Wtb 220 (two copies), WtbBlend 239
  (two copies).
- Linked into the sync block: §0.

`DiodeLadder.c` is a `Core/DSPAudio` source (`-Ofast`, Makefile l. 210–212),
the same flags as `Oscillator.c`. The arithmetic matches on the target as it
does on the host.

---

## 6. `Core/DSPAudio/Oscillator.c` / `.h` — five accessors, one type moved (K5)

### 6.1 `Oscillator.c` l. 2767–2773 — REMOVE (the `osc_wtbPlan_t` typedef; its comment block l. 2755–2766 moves with it)

Replace with `/* S093 P7: osc_wtbPlan_t is in Oscillator.h (the Alkaline fused blend takes the plan) */`.

### 6.2 `Oscillator.c` after l. 2940 (the end of `osc_wtbBlendBlock()`) — ADD

```c
/* ------------------------------------------------------------------------
 * S093 P7: block-setup accessors for the Alkaline fused readers.
 *
 * What:       non-static wrappers over five static helpers of the typed
 *             readers, so Alkaline_calcVoiceSyncBlock() fills a DlSrc with
 *             exactly the values the readers compute: the slp block step,
 *             the item gain, a wavetable frame's base, the nearest frame, the
 *             blend plan (at OUTPUT_DMA_SIZE, the size every reader call
 *             passes, so the existing constprop clone serves it).
 * Why:        S093 P7 S1: the fused loops must be bit-identical to reader +
 *             plain B2R (AL-X2); calling the readers' own helpers keeps one
 *             definition of each rule. The helpers and the S087 loops are not
 *             edited; these wrappers are flash code (no section attribute),
 *             called once per block.
 * Accessors:  AlkalineVoice.c alk_renderTyped().
 * Affiliates: osc_slpBlockInc() l. 1895, osc_bindGain() l. 1861,
 *             osc_wtbFrame() l. 2719, osc_wtbFrameOf() l. 2729,
 *             osc_wtbPlanBlock() l. 2795; DlSrc (DiodeLadder.h).
 * ------------------------------------------------------------------------ */
uint32_t osc_slpBlockIncFor(const uint32_t phaseInc, const uint32_t phase,
                            const uint32_t loopStartPh, const uint32_t lenPh)
{
	return osc_slpBlockInc(phaseInc, phase, loopStartPh, lenPh);
}

float osc_bindGainFor(const OscInfo* osc)
{
	return osc_bindGain(osc);
}

const uint8_t *osc_wtbFrameFor(const OscInfo* osc,
                               const wavetable_catalogue_entry_t *wt,
                               const uint32_t frame)
{
	return osc_wtbFrame(osc, wt, frame);
}

uint32_t osc_wtbNearestFrame(const uint8_t position, const uint32_t count)
{
	return osc_wtbFrameOf(position, count);
}

void osc_wtbPlanFor(const OscInfo* osc, const uint32_t count, osc_wtbPlan_t* plan)
{
	osc_wtbPlanBlock(osc, count, OUTPUT_DMA_SIZE, plan);
}
```

### 6.3 `Oscillator.h` before l. 985 (`uint32_t osc_buildLoopSyncList(`) — ADD

```c
/*
 * One wavetable blend block, split at a frame crossing (S087 P1; moved from
 * Oscillator.c in S093 P7 so the Alkaline fused blend can take the plan).
 *
 * What:       segments (1 or 2); for each segment its exclusive end sample,
 *             the lower frame of its pair, and ff at its first sample; step is
 *             the per-sample change of the frame position.
 * Why:        osc_typedPositionApply() limits one block's move to at most one
 *             frame, so a glide crosses at most one frame boundary per block;
 *             splitting there keeps every sample a blend of the two frames
 *             around its own position.
 * Accessors:  osc_wtbPlanBlock() (writes), the four blend copies and
 *             osc_wtbPlanFor() (read).
 */
typedef struct {
	uint8_t  segments;
	uint8_t  end[2];
	uint32_t pair[2];
	float    ff[2];
	float    step;
} osc_wtbPlan_t;

/* S093 P7: block-setup accessors for the Alkaline fused readers (Oscillator.c,
 * after osc_wtbBlendBlock()); each returns its static helper's value. */
uint32_t osc_slpBlockIncFor(uint32_t phaseInc, uint32_t phase,
                            uint32_t loopStartPh, uint32_t lenPh);
float osc_bindGainFor(const OscInfo* osc);
const uint8_t *osc_wtbFrameFor(const OscInfo* osc,
                               const wavetable_catalogue_entry_t *wt,
                               uint32_t frame);
uint32_t osc_wtbNearestFrame(uint8_t position, uint32_t count);
void osc_wtbPlanFor(const OscInfo* osc, uint32_t count, osc_wtbPlan_t* plan);
```

`wavetable_catalogue_entry_t` reaches `Oscillator.h` through its include of
`SampleMemory.h` (l. 45).

**Gate (§10 G7):** the S087 loops are unchanged. Check `elf_loopsrc.py
--compare` on the oscillator twins, and ITCM stays 14,552 B
(`link_budget.py`). An extra caller of a static helper could change LTO's
inlining in an existing loop, and this gate catches that.

---

## 7. Release on a slot change — NONE today, required by §8

`instrumentManager_resetRuntimeSlotFromScene()` (l. 1676–1699) and
`instrumentManager_runtimeInit()` (l. 1513–1557) zero the slot union, then
re-init. An Alkaline voice's typed state is in its own `osc`, so it is gone
with the slot. That needs no code, unless §8 adds a held reservation.

---

## 8. Admission (P7-D1)

### 8.1 Option A — an Advanced place for a typed Alkaline (recommended)

**The rule today (verified).**
- `instrumentManager_advancedCountForScene(scene, ignore_slot)`
  (`InstrumentManager.c` l. 415–439) counts the kit slots whose **type**
  carries `INSTRUMENT_FLAG_ADVANCED` (read from
  `scene->kit.instruments[slot].type`).
- `instrumentManager_typeSelectableForSceneSlot(scene, slot, candidate)`
  (l. 441–468) admits an Advanced candidate while that count, the destination
  ignored, is < 2.
- Its callers:
  - the Load type browser: `menu.c` l. 9594/9598
    (`menu_instrumentTypeSelectableForMask()`), 9649–9655 (the base type),
    11103 (the voice's type), 11201 and 11225 (the type list count and step);
  - Copy: `CopyClear/copyOps.c` l. 706, which drops a copy with
    `AUTOSAVE_TRACE_CC_DROP_ADVANCED_LIMIT`.
- A slot's stored `typ` is `parameter_images.instrument_parameters[ALK_PARAM_OSC1_TYPE]`
  of its `kit_instrument_slot_t` (`SceneData.h` l. 37–66). `osc1_type` is a
  selector, so only the main image holds it.

**The change under A:**
- `instrumentManager_advancedCountForScene()` also counts an Alkaline slot
  whose stored `typ` is not `wav`.
- The `typ` cell edit (the `osc1_type` writer path, §3.7) admits
  `wav` → `smp`/`slp`/`wtb` only while that count, this slot ignored, is < 2.
  Otherwise the value stays `wav`.
- `typ` back to `wav` frees the place at once, since the count reads the
  image.
- **RAM 0.**

**Your decisions under A** (policy, not tree facts, so they are not settled
here):
- **A-1:** Load of an Alkaline preset whose file has a typed `osc1_type`, in
  a Scene with both Advanced places taken. Either refuse the Load (the
  browser cannot see the file's `typ` before reading it), or load it with
  `typ` forced to `wav`.
- **A-2:** Copy of a typed Alkaline slot into such a Scene: drop, as
  `copyOps.c` l. 706 does for an Advanced type, or copy with `typ` = `wav`.
- **A-3:** Morph, Scene switch and step automation that change `typ`: refuse
  the step (the writer returns 0), or clamp to `wav`.

Once A-1 to A-3 are decided, P7-2 writes the count term, the writer check and
law AL-T: with two Advanced places taken, a third typed Alkaline is refused,
and `typ` `wav` frees the place.

### 8.2 The pool reserve (the plan's §7.5): measured to be insufficient

The reserve was designed for an excess of ≈ 1–3 per sample (plan §7.3). §0
measures +11 to +56, and two locked slots cover ≈ 20–24. If you keep it for
`smp` alone (Option C), it is built as the plan describes. The verified
anchors are below.
- **`modulationNode.c` l. 68 — ADD** after `modNode_interpReserved`:
  `static INCCMZ uint8_t modNode_interpLocked = 0u;` (+1 B, approved), with
  `uint8_t modNode_interpLock(void)` (granted while `locked < OSC_WAVE_INTERP_MAX_ACTIVE`)
  and `void modNode_interpUnlock(void)`.
- **`modNode_interpClaim()` l. 136–158 — MODIFY:** both grant tests add the
  locked count, `active + locked < MAX` for a holder and
  `active + reserved + locked < MAX` for a newcomer. A holder of last block
  then steps once the lock leaves no room, which is the cost the plan states.
- **The writer (§3.7):** lock on `wav` → typed, unlock on typed → `wav`.
- **`AlkalineVoice`:** one `uint8_t interpLock` in the byte K6 frees (0 B).
- **Release:** `instrumentManager_resetRuntimeSlotFromScene()` before its
  `memset` (l. 1683): unlock if the outgoing Alkaline voice holds a lock.

---

## 9. Tools and bench

### 9.1 `tools/alkaline/control_cost.py` l. 53–70 — MODIFY (`ITERS`) and l. 211–218 (the guard)

Today `ITERS[("Alkaline_calcVoiceSyncBlock", 1)] = 0` covers the one fused
loop. After P7 the linked sync block has **six** audio loops (§0: 262, 252,
239, 235, 229, 215), and `DiodeLadder_renderB2Smp` stays a separate function
called from two places (its loop is 217).

```python
    # S093 P4 (G4) / S093 P7: on the linked image LTO inlines the fused B2R
    # reads into the sync block: wav (renderB2Osc), slp, wtb (8/16-bit
    # copies) and the wtb blend (two copies) are its six loops, all audio
    # loops (weight 0); smp stays its own function (two call sites)
    ("Alkaline_calcVoiceSyncBlock", 1): 0,
    ("Alkaline_calcVoiceSyncBlock", 2): 0,
    ("Alkaline_calcVoiceSyncBlock", 3): 0,
    ("Alkaline_calcVoiceSyncBlock", 4): 0,
    ("Alkaline_calcVoiceSyncBlock", 5): 0,
    ("Alkaline_calcVoiceSyncBlock", 6): 0,
    ("DiodeLadder_renderB2Smp", 1): 0,
    ("DiodeLadder_renderB2Slp", 1): 0,
    ("DiodeLadder_renderB2Wtb", 1): 0,
    ("DiodeLadder_renderB2WtbBlend", 1): 0,
```

**The guard (l. 211–218) — MODIFY:** it already tests every weight-0 rank of
the sync block (`it == 0 and (lv != 3 or length < 200)`). Extend its condition
to the four `DiodeLadder_renderB2*` user variants, so a control loop that
lands at a weight-0 rank stops the count:

```python
        if (base == "Alkaline_calcVoiceSyncBlock" or base.startswith("DiodeLadder_renderB2")) \
                and it == 0 and (lv != 3 or length < 200):
```

**The stub link (`STUBS` l. 73–108, `build()`):** this mode compiles
`AlkalineVoice.c` without `Oscillator.c`. **ADD** stubs:
- `osc_userBind`, `osc_typedPositionApply`, `osc_typedTriggerPhase`
  (`return 0u`);
- `sampleMemory_sampleEntry` and `sampleMemory_wavetableEntry` (`return 0`);
- `modNode_interpClaim` (`return 0u`);
- the five `osc_*For` accessors.

In `main()` (l. 268–273) add `"osc_userBind", "osc_typedPositionApply"` to the
oscillator list costed from `Oscillator.o`. The stub mode stays an estimate;
**the gate is `--elf`**.

**Measured with exactly these weights** on the scratch link: per block 1,769
(typed) and 1,232 (`wav` only). The tool exits on any loop without a count,
which is how the six ranks were found.

**Per-mode figures.** The tool reports the longest path, the dearest typed
branch, so its per-block figure is an upper bound for every typed mode. The
gate takes each mode's own loop (`elf_looppath.py --top 8`, mapped by
`addr2line -i` on the `-g` twin) plus that bound.

### 9.2 Bench `tools/dsp_test/modules/alkaline.c` — MODIFY

- **l. 80 — REMOVE** the claim stub; **ADD** in the AL-X2 block:
  `static uint8_t host_claim; uint8_t modNode_interpClaim(void* osc) { (void)osc; return host_claim; }`.
- **l. 145, 169, 534, 815, 861:** `v->wave` / `v.wave` / `a.wave` →
  `->osc.waveform` / `.osc.waveform`. The `--pattern` / `--sweep` argument 2
  keeps its meaning (0 Saw, 1 Sqr); `oscType` stays `OSC_TYPE_WAV` after
  `memset` + init.
- **Groups (l. 114–117; P6: 14) — MODIFY:** add `G_X2`, `"AL-X2"`.
- **ADD law AL-X2** (before `main()`; call `test_fused_typed();` after
  `test_pos();`, l. 933):

```c
/* ---------------------------------------------------------------- AL-X2
 * S093 P7: every fused user-source loop is bit-identical to the reader
 * (osc_renderTyped(), gain 1) followed by the plain B2R, block for block:
 * the same async, the same triggers (pos starts, slides), a pos sweep that
 * bends a sample's playhead and glides a wavetable across frames; the phase
 * after every block equal too. Modes: smp, slp, wtb 16-bit and 8-bit with
 * the slot granted (blend) and refused (nearest frame), and smp with no
 * sample installed (the silent block). */
static uint8_t host_claim;
uint8_t modNode_interpClaim(void* osc) { (void)osc; return host_claim; }

/* one sample (3,000 frames, a decaying chirp, loop 700..2900) and two
 * 3-frame wavetables (600 points 16-bit, 256 points 8-bit), through the
 * installer the oscillator bench uses (modules/oscillator.c l. 747, 895) */
static void x2_install(void)
{
	static int16_t smp[3000];
	static int16_t f16[3][600];
	static int8_t f8[3][256];
	const sample_install_fmt_t fmt = { 1u, 1u, 44100u, 2u, 16u, 16u };
	const sample_install_fmt_t fw16 = { 1u, 1u, 44100u, 2u, 16u, 16u };
	const sample_install_fmt_t fw8 = { 1u, 1u, 44100u, 1u, 8u, 8u };
	int i, f;

	for (i = 0; i < 3000; i++)   /* peak 32767 keeps the gain code at 0 */
		smp[i] = (int16_t)(32767.0 * exp(-i / 1500.0) * sin(0.002 * i * i / 30.0 + 0.3));
	smp[0] = 32767;
	for (f = 0; f < 3; f++) {   /* frames with different harmonic content */
		for (i = 0; i < 600; i++)
			f16[f][i] = (int16_t)(30000.0 * sin(6.2831853 * i / 600.0 * (f + 1)));
		for (i = 0; i < 256; i++)
			f8[f][i] = (int8_t)(120.0 * sin(6.2831853 * i / 256.0 * (2 * f + 1)));
	}
	catalogue_host_reset();
	sampleInstall_begin();
	(void)sampleInstall_beginSample("chirp   ", 3000u, &fmt);
	(void)sampleInstall_writeFrames((const uint8_t *)smp, 3000u);
	(void)sampleInstall_finishSample(700u, 2900u, 1u);
	(void)sampleInstall_beginWavetable("w16     ", 600u, 3u, 16u, &fw16);
	for (f = 0; f < 3; f++)
		(void)sampleInstall_writeFrame((const uint8_t *)f16[f], 600u);
	(void)sampleInstall_finishWavetable();
	(void)sampleInstall_beginWavetable("w8      ", 256u, 3u, 8u, &fw8);
	for (f = 0; f < 3; f++)
		(void)sampleInstall_writeFrame((const uint8_t *)f8[f], 256u);
	(void)sampleInstall_finishWavetable();
	(void)sampleInstall_commit();
	sampleMemory_refresh();
}

/* the reference block: the same async, then the reader and the plain B2R
 * with the arguments the fused path uses */
static void x2_ref(AlkalineVoice *v, int16_t *buf)
{
	Alkaline_calcVoiceAsync(v);
	osc_renderTyped(&v->osc, buf, BLK, 1.0f);
	DiodeLadder_renderB2(&v->ladder, buf, ALK_USER_SCALE_V * v->driveGain,
	                     v->iLadEnd, v->iAbcEnd * v->makeup);
}

static void x2_run(const char *name, uint8_t type, uint8_t wave, uint8_t claim)
{
	AlkalineVoice a, b;
	int16_t ba[BLK], bb[BLK];
	unsigned diff = 0u, phd = 0u;
	double rms = 0.0;
	int n, j;

	host_claim = claim;
	memset(&a, 0, sizeof(a));
	Alkaline_initVoice(&a);
	a.cutoff = 0.55f; a.reso = 0.5f; a.envMod = 0.6f; a.decay = 0.4f; a.accent = 0.7f; a.drive = 0.4f;
	osc_setType(&a.osc, type);
	a.osc.waveform = wave;
	b = a;                          /* two identical voices */
	for (n = 0; n < 3000; n++) {    /* 3,000 blocks = 2.2 s */
		if (n % 172 == 0) {         /* a 16th at 120 BPM: notes, accents, slides */
			const uint8_t vel = (n / 172) % 3 ? 80u : 127u;
			const uint8_t note = (uint8_t)(51 + (n * 7) % 30);

			a.slide = b.slide = (uint8_t)((n / 172) % 4 == 2);
			Alkaline_triggerVoice(&a, 0u, vel, note, ALK_STEP);
			Alkaline_triggerVoice(&b, 0u, vel, note, ALK_STEP);
		}
		if (n % 23 == 0)   /* pos sweeps: bends the playhead, glides frames */
			a.osc.position = b.osc.position = (uint8_t)((n * 37) & 127);
		block(&a, ba, K_B2R);       /* the fused path */
		x2_ref(&b, bb);             /* reader + plain B2R */
		for (j = 0; j < BLK; j++) {
			diff += (ba[j] != bb[j]);
			rms += (double)ba[j] * ba[j];
		}
		phd += (a.osc.phase != b.osc.phase);
	}
	printf("AL-X2 %-14s: %u sample(s) differ, %u phase(s) differ, RMS %.0f LSB\n",
	       name, diff, phd, sqrt(rms / (3000.0 * BLK)));
	if (diff || phd)
		fail(G_X2, name, diff, phd);
	/* a mode that should sound must not pass by being silent */
	if (rms == 0.0 && strcmp(name, "smp none") != 0)
		fail(G_X2, "the mode is not silent", 0, 0);
}

static void test_fused_typed(void)
{
	x2_install();
	x2_run("smp", OSC_TYPE_SMP, 0u, 0u);
	x2_run("slp", OSC_TYPE_SLP, 0u, 0u);
	x2_run("wtb16 blend", OSC_TYPE_WTB, 0u, 1u);
	x2_run("wtb16 nearest", OSC_TYPE_WTB, 0u, 0u);
	x2_run("wtb8 blend", OSC_TYPE_WTB, 1u, 1u);
	x2_run("wtb8 nearest", OSC_TYPE_WTB, 1u, 0u);
	catalogue_host_reset();      /* nothing installed: the silent block */
	sampleMemory_refresh();
	x2_run("smp none", OSC_TYPE_SMP, 0u, 0u);
}
```

**Measured (scratch, this code, with §3–§6 applied to host copies):**
- every mode reports 0 differing samples and 0 differing phases;
- RMS smp 729, slp 938, wtb16 blend 1,958 / nearest 2,411, wtb8 blend 2,059 /
  nearest 2,208, smp none 0 LSB;
- coverage, counted in a scratch copy: the smp run held a finished one-shot
  on 2,611 blocks, slp took a backward glide on 531, and each blend crossed a
  frame mid-block on 89;
- all fifteen groups pass with 0 failures.

**`make -C tools/dsp_test alkaline` — NONE in the rule.** The bench already
links `SampleMemory.c`, `SampleInstall.c` and `catalogue_host.c` (Makefile
l. 318–319). The host header copies come from `extract.py` as today, and the
new accessors reach the bench through `Oscillator.host.c`.

---

## 10. Gates

| Gate | Command | Pass |
|---|---|---|
| Host | `make -C tools/dsp_test alkaline special_tags oscillator seq_steplen`, one by one | All groups 0 failures, AL-X2 included; the legato break still caught; oscillator laws unchanged |
| G3 (loops) | `elf_looppath.py --elf build/lxr02.elf --sym Alkaline_calcVoiceSyncBlock.constprop.0 --cond-heads --in-range --top 8`; the same for `DiodeLadder_renderB2Smp`; `addr2line -i` on the `-g` twin maps each loop to its wrapper | `wav` loop 214 (unchanged); each user loop recorded |
| G4 (per block) | `control_cost.py --elf build/lxr02.elf` (§9.1 weights) | `wav` path 1,230 (the P6 image); typed bound recorded (scratch: 1,769) |
| G5 (whole voice) | loop + per block / 32, per mode | `wav` ≤ 261.3 (Basic). Typed modes: as P7-D1 decides (Advanced under A; ≤ 261.3 for any mode kept Basic under B/C) |
| G6/G7 (others unchanged) | `make -C tools/dsp_test elf_p4_check`; `elf_loopsrc.py --compare` on base/new `-g` twins | 0 MISSING, 0 grew; the S087 loops equal; ITCM 14,552 B |
| Flash | `link_budget.py arm-none-eabi-nm build/lxr02.elf` | headroom ≥ 16 KiB (measured basis: ≈ +8 KB on the P6 image's 47,836 B) |
| G-dev | Device: four Alkalines (`wav`, Res and drive full) as today; plus the P7-D1 worst case (A: two typed Alkalines in the Advanced places in their dearest mode, `wtb` blend, with two blends running); 0 underruns over 5 min; listening per type | User |

---

## 11. Documents and data

- `SD_CARD/Instrument/Alkaline/init.alk` (`make_init_alk.py`): `osc1_wave`,
  `osc1_type`; 29 keys, 23 morph rows.
- `FILESYSTEM_SPEC.md`: the Alkaline key list (`alk_wave` → `osc1_wave`,
  `osc1_type`; no migration).
- `MIDI_NRPN_TABLE.md`: regenerate (row 0 renamed, row 28).
- `INSTRUMENTS_DSP_REFERENCE.md` §5.5:
  - the types;
  - the fused readers;
  - the measured costs (§0);
  - the admission rule chosen.
- `CPU_USE_DSP_AUDIT.md` item 71: P7 figures per mode.
- The 093 log: P7 entry, with the §0 measurement and the P7-D1 decision.
- `MEMORY.md`: "Alkaline typed modes are over the Basic ceiling: loop alone
  217–262" (until P7-D1 lands).

---

## 12. Verified in the tree and measured for this schedule

| Fact | Where |
|---|---|
| `alk_wave` row, ON/OFF tag 3, `alkWaveNames`, tag switch, assert | `AlkalineParameters.c` l. 144–148; `MenuText.h` l. 225–252; `menu.c` l. 12336–12362, 12505–12508 |
| `voice->wave` readers (post-P6 lines) | `AlkalineVoice.c` l. 52, 256; bench l. 145, 169, 534, 815, 861 |
| Drum `osc1_wave` / `osc1_type` rows and macros | `DrumParameters.c` l. 57–59, 103–106, 233, 335 |
| Wave domain, cell count, `osc1Type`, `instrumentManager_osc()` for ALK | `InstrumentManager.c` l. 838–892, 2348–2385 |
| `typ` writer, classifier | `InstrumentManager.c` l. 3881–3899, 3987–4034 |
| `osc_setType()` clamps to `OSC_STOCK_LAST` | `Oscillator.c` l. 1439–1444 |
| `osc_userBind()` frequency product = `osc_setTableFreq()`'s | `Oscillator.c` l. 1555, 1232 |
| Reader arithmetic and guards | `Oscillator.c` l. 1921–2000 (smp/slp), 2699–2940 (wtb) |
| Pool counters, claim, per-block reset | `modulationNode.c` l. 64–97, 136–158, 515–545 |
| Slot reset paths | `InstrumentManager.c` l. 1513–1557, 1676–1699 |
| Menu `MENU_WAVEFORM` sites | `menu.c` l. 3408, 5360, 6590, 6888, 12084, 13929, 15147 |
| Compile flags: Oscillator.c and DiodeLadder.c both `-Ofast` | Makefile l. 185, 210–212 |
| Today's sync block: flash, 3,000 B, inlined B2R | `build/lxr02.elf` (`arm-none-eabi-nm -S`) |
| No Advanced per-voice ceiling recorded | `CPU_USE_DSP_AUDIT.md` (searched) |

**Measurements (S093 P7, scratch in the session scratchpad `p7/`; no tree
file changed):**

| What | How | Result |
|---|---|---|
| Existing wrappers unchanged | disassembly `cmp` before/after | identical (316 / 343 lines) |
| Fused loops | `elf_looppath.py` on the object and on the `-flto` link | standalone 217/229/220/239; linked 217/229/235–239/252–262; `wav` 215 |
| Per block | `control_cost.py --elf` on the link, §9.1 weights | typed 1,769; `wav` only 1,232 (P6 expects 1,230) |
| Flash | `arm-none-eabi-nm -S` | sync block 2,996 → 9,228 B; Smp 1,370 B; async +354 B |
| Bit-identity | AL-X2, host | 0 differing samples and phases in all seven modes; all groups pass |
