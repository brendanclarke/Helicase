# S085 post-mortem: menu layout decisions

This records where the oscillator controls appear and how they display, as
agreed by the end of S085. It includes the late changes: the `wav`/`mod`
swap, the renames and the Cymbal/HiHat `pwm` exclusion. A re-implementation
should reproduce this without re-asking.

**Pages:** 16 cells each, shown **four per screen** behind the same SELECT
button. "Screen 1" below is cells 1–4, "screen 2" cells 5–8, and so on.

---

## 1. The oscillator rows

| Short | Long name | File key | What it is |
|---|---|---|---|
| `typ` | Type | `oscN_type` | Oscillator type: `wav pwm smp slp wtb` (0–4). |
| `wav` | Waveform | `oscN_wave` | **The selection** for the type: stock wave, pwm family, sample or wavetable. |
| `mod` | Mod | `oscN_source` | **The continuous control** for the type, a plain 0–127: pwm width, sample start (`smp`/`slp`), wavetable position (`wtb`). Ignored under `wav`. |
| `run` | RunMode | `osc_run_mode` | One per voice: `rst fre itp` (0–2). |

- **History:** `mod` was called `src` ("Source") and carried the selection,
  while `wav` carried the continuous control. The user had them swapped at
  the end of S085; the file keys did not change.
- **Category labels** (first line of the single-parameter view):
  - `Oscilltr` for the main oscillator and `run`;
  - `FM` for the Drum modulator;
  - `Mod Osc` for the Cymbal/HiHat modulators.

---

## 2. Where the rows sit

### 2.1 Drum

| Page | Screen 1 | Screen 2 |
|---|---|---|
| Oscillator (page 1) | `coa fin wav typ` | `mod run` |
| FM (page 4) | `amt frq wav mod`† | `typ mod` |

† The fourth cell on the FM page is `osc2_mod_type` (long name "Mode",
values `fm mix snc rng`), whose short name is also `mod`. Screen 2's `mod`
is the FM oscillator's continuous control. This name clash was flagged
and **not resolved**; a rename such as `cmb` was offered.

### 2.2 Snare

| Page | Screen 1 | Screen 2 |
|---|---|---|
| Oscillator (page 1) | `coa fin noi mix` | `wav typ mod run` |

Snare's page 1 already used cells 1–5, so its new rows take cells 6–8 (S085
decision D3). The noise oscillator has no type row.

### 2.3 Cymbal and HiHat (same layout)

| Page | Screen 1 | Screen 2 | Screen 3 |
|---|---|---|---|
| Oscillator (page 1) | `coa fin wav typ` | `mod run` | — |
| Mod Osc (page 4) | `f1 f2 g1 g2` | `wav wav typ mod` | `typ mod` |

On the Mod Osc page:
- screen 2 is `osc2 wav`, `osc3 wav`, `osc2 typ`, `osc2 mod`;
- screen 3 is `osc3 typ`, `osc3 mod`.

---

## 3. What each value shows

### 3.1 Compact (three characters: overview and edit field)

| Row | Under type | Shows |
|---|---|---|
| `typ` | — | `wav pwm smp slp wtb` |
| `run` | — | `rst fre itp` |
| `wav` | `wav` | `Sin Tri Saw Rec Noi Cym Re3 Sw3 ReA TrA SwA` (values 0–10) |
| `wav` | `pwm` | `Rec Saw Tri Re3 Sw3 Swp` (families 0–5) |
| `wav` | `smp` / `slp` | `sNN`: the sample number, 1-based (`s01`…`s99`, then `sA0`, `sA1`, … for 100+) |
| `wav` | `wtb` | `wNN`: the wavetable number, same scheme |
| `mod` | any | the number 0–127 |

**Naming rules (user, end of S085):**
- **Stock waves:** capitalised like the originals. `Re` = rectangle
  (formerly `sq`), `Sw` = saw, `Tr` = triangle.
  - Old → new: `sq3`→`Re3`, `sw3`→`Sw3`, `sqa`→`ReA`, `tra`→`TrA`,
    `swa`→`SwA`.
  - The code constants keep the old spelling (`SQ3 SW3 SQA TRA SWA`).
- **pwm families:** no `p` prefix, because `typ` already says pwm.
  - Old → new: `prc`→`Rec`, `psw`→`Saw`, `ptr`→`Tri`, `pq3`→`Re3`,
    `pw3`→`Sw3`, `swp`→`Swp`.
  - The constants keep `OSC_PWM_PRC`…`OSC_PWM_SWP`.

### 3.2 Single-parameter view (second line, 16 characters)

| Row | Under type | Left (from column 0) | Right (columns 13–15) |
|---|---|---|---|
| `wav` | `smp` / `slp` | the sample's **8-character name** | `sNN` |
| `wav` | `wtb` | the wavetable's **8-character name** | `wNN` |
| `wav` | `wav` | the stock wave name | the same name |
| `wav` | `pwm` | — | the family name |
| `mod` | any | **the number, once** | nothing |
| `typ`, `run` | — | — | the name |

**Rules (user, end of S085):**
- `mod` is only ever a number, shown once, on the left, with no label. The
  type already says what it is (width, start or position). The earlier
  "width NNN", "start NNN" and "frame NNN" texts were removed.
- `wav` shows the file or wavetable name on the left in the sample and
  wavetable types.

**The value shown follows the stored Scene image, not the runtime (S085
D5).** The formatter reads the oscillator's **stored** `typ` from the same
image as the cell value. So the display is correct while step automation
holds `typ`, during a deferred slot swap, and for a per-track played Scene.

---

## 4. Ranges and stepping

| Row | Range |
|---|---|
| `typ` | 0–4 |
| `run` | 0–2 |
| `wav` under `wav` | 0–10 |
| `wav` under `pwm` | 0–5 |
| `wav` under `smp`/`slp`/`wtb` | 0 to installed count − 1 (0 when nothing is installed; at most 127) |
| `mod` | 0–127 |

- **Clamping:** a menu or MIDI edit is clamped to the current maximum for
  the stored type (`menu_clampCellValue()`). Step-automation editing uses
  the same per-slot maximum (capped at 127) and the same formatted text.
- **Cymbal/HiHat modulators (`osc2`, `osc3`) cannot be `pwm`** (user, end of
  S085). Their `typ` rows step `wav ↔ smp`, skipping `pwm`:
  - an encoder step onto `pwm` moves on in the direction of travel;
  - an absolute pot never stores it;
  - a `pwm` arriving from a file, AutoSave, automation or MIDI is played as
    `wav` and displays as `wav`.

  The main oscillators and the Drum FM oscillator keep all five types.
- **Retained requests:** changing `typ` and back restores the previous
  `wav` and `mod` values. They are held as request bytes and only clamped
  for display and playback. For example, sample 100 survives
  `smp → wav → smp`. A smaller sample reinstall clamps the selection
  without losing it.

---

## 5. Morph, automation and SHIFT

- **Selector rows:** `typ` and `run` are automatable but **not morphable**.
  - They are stored single-ended (the Morph cell mirrors Normal) and applied
    before the Morph image on every slot reset.
  - **Under SHIFT (Morph view)** they display the mirror, and an edit
    commits to Normal (S085 decision D12).
- **Image rows:** `wav` and `mod` are ordinary Morphable rows, LFO and
  velocity targets, and automatable. Morphing or modulating `wav` under
  `smp`/`slp`/`wtb` steps through the installed samples or tables.
- **Drum `osc2_mod_type`** (`fm mix snc rng`) stays an ordinary Morphable
  row that steps through the four modes.

---

## 6. Not decided / left open

- The Drum FM page's two `mod` labels (§2.1).
- Whether `wav` under `smp`/`slp`/`wtb` *should* be morphable (§5). It
  became morphable as a side effect of the swap; this was flagged, not
  decided.
