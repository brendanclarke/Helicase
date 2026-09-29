# S074 — CrumpBit (`cbt`): the first buffer-using Effect

**Status:** implemented (commit `1dbdd70`) and **accepted on hardware by the
user (2026-09-29)** as the v1 baseline; further additions and changes are
expected (§12). The specification is final for v1 (draft 3). Every decision
is settled (§11): the answers to Q1, Q5, Q7, Q13 and Q24, and the follow-ups
F1–F6. The line-level schedule is `S074_CRUMPBIT_IMPLEMENTATION.md`.

**Goal (user, 2026-09-29):** an Effect type that converts the stereo FX send
to 8 bits, manipulates the bits, and feeds a mono 8-bit tape-style delay that
uses the shared DTCM arena. It is also Phase 5 item A8 (the buffer-using
template type), so it must close the three arena gaps listed in
`S074_EFFECT_BUGS_BUFFER_USE.md` §3.3.

**Base:** branch `dev-ph5-effects`, working tree after the S074 underline fix
(`S074_EFFECT_P_LOCK_DISPLAY_IMPLEMENTATION.md`). Line numbers cite that
tree.

**Authorities used:** `EFFECTS_BUS_REFERENCE.md` §4, §8, §13–§15;
`EFFECTS_MIXER_DSP_REFERENCE.md` §2, §3.3, §4, §5, §7.1;
`S074_EFFECT_BUGS_BUFFER_USE.md` §3; and the current code (`EffectsManager.h`,
`FxBuffer.h`, `menuEffects.c`, `mixer.c`, `StepScale.c`).

Where your description leaves a choice open, this draft picks one and marks
it with its question number (**Q*n***). The body shows the proposed default;
§11 shows the alternatives.

---

## 1. Summary

```
send L ─► 8-bit ADC ─► data lines ─► DAC ─► AC ─► hL ───────────► (1−mix)·hL + mix·gL·wet ─► out L
send R ─► 8-bit ADC ─► data lines ─► DAC ─► AC ─► hR ───────────► (1−mix)·hR + mix·gR·wet ─► out R
                                                   │                               ▲
                                                   └─► ½(hL+hR) ─► (+) ─► 8-bit ─► tape loop ─┴─► wet
                                                                   ▲    (arena share)       │
                                                                   └──── feedback · wet ◄───┘
```

- **ADC and data lines:** each channel is converted as a bipolar 8-bit ADC
  (offset binary, code 128 = silence). One pair of 8-bit masks (`bit off`,
  `bit invert`) acts on the eight data lines of both channels. Each bit is
  normal, forced off, or inverted; invert wins if both are set. An 8-bit DAC
  decodes the result, and an AC-coupling stage (proposed, F2) removes the
  DC that the masks create.
- **Delay module (sub-type `dly`):** a mono 8-bit tape-style delay in the
  Effect's arena share. `Rate` sets the tape speed and so the delay time.
  Time changes glide the pitch, as tape does. `Sync` snaps the time to the
  nearest step-scale division at the current tempo. `Mix` crossfades the
  bit-module output with the delay (0 = dry, 127 = delay only). `Pan` places
  the mono delay in the stereo return.
- **Page:** SELECT 1 keeps the two manager screens. A custom overlay shows the
  eight bit states on the top row. Each SELECT button toggles one bit and
  jumps to the overlay, and the SELECT LEDs always show the bits.

---

## 2. Identity

| Field | Value | Notes |
|---|---|---|
| `token3` | `cbt` | permanent `.fx` identity; unique |
| `abbrev5` | `CrmBt` | exactly 5 characters (self-check code 9) |
| `full8` | `CrumpBit` | 8 characters, shown in the `typ` full view |
| Registry id | `EFFECT_TYPE_CRUMPBIT = 2`, `EFFECT_TYPE_COUNT = 3` | ids are append-only |
| `io_flags` | `EFFECT_IO_STEREO_IN \| EFFECT_IO_STEREO_OUT` | the send pair in, stereo out |
| Folder | `Core/DSP/Effects/CrumpBit/` | new files, §6 (**Q23**) |

---

## 3. Signal path (DSP specification)

All processing is foreground, float, in place on `io->l` / `io->r`,
`io->frames` = 32. 1.0 = int16 full scale (`EffectsManager.h`
`effect_io_t`). **Every stage runs on every sample whatever its settings**
(constant CPU): the delay runs at `Mix` 0, feedback is computed at 0, and the
bit logic runs with every bit normal.

### 3.1 8-bit ADC (both channels)

The front end simulates a bipolar 8-bit audio ADC (Q1, answered 2026-09-29).

- **Output format: offset binary.** Codes 0..255, **code 128 = 0 V**
  (silence), with 0 and 255 the negative and positive rails.
  - This is the usual output of 8-bit audio converters and the format of
    unsigned 8-bit PCM (for example 8-bit WAV).
  - Two's complement is the same code with the MSB flipped; some converters
    offer it as an option.
  - Sign-magnitude is not used by audio ADCs, so draft 1's proposal is
    dropped.
- **Conversion:** `c = clamp(round(x · 128) + 128, 0, 255)`, with `x` in
  full-scale floats.
  - One LSB = 1/128 of full scale. +1.0 clips to 255 (+127/128); −1.0 is 0.
  - Rounding makes the transfer mid-tread, so silence sits exactly on
    code 128 (**Q3**).
- **Consequences:**
  - With every bit normal the Effect is still an 8-bit quantiser. It is never
    transparent (**Q30**).
  - A quiet send uses few codes: a −20 dBFS send uses about ±13 of ±128. That
    is the intended grit (**Q3**: an input-gain row can come later).

### 3.2 Data lines (bit module) and DAC

The eight data lines between the simulated ADC and DAC, with one pair of
masks applied to both channels. For each bit:

- **normal:** the bit passes;
- **off:** the line is forced to 0 (`c & ~O`), like a grounded data line;
- **invert:** the line is inverted, `c ^ I` (**F1, answered:** "flip
  whatever it otherwise would be").

Invert takes precedence when both are set:
`c' = (c & ~(O & ~I)) ^ I`, with `O = bit_off` and `I = bit_invert`.

**DAC decode:** `y = (c' − 128) / 128`, range −1.0 … +127/128.

This is branch-free. The per-block masks are built once; each sample costs
one convert, one clamp, two logic operations and one convert back per
channel.

What the lines do (offset binary, XOR invert):

| Bit | Off | Invert |
|---|---|---|
| 7 (MSB, half scale) | Every positive sample drops by half scale, so the whole signal sits below 0: heavy DC and half-wave distortion. **Silence becomes −1.0 (full-scale DC).** | The code is read as two's complement: each half-wave jumps to the other side at every zero crossing (the "unsigned read as signed" sound). **Silence becomes −1.0.** |
| 6..0 (weight 2ⁿ) | Removes that weight where the bit is set: coarser steps, about −2ⁿ⁻¹ average DC | Adds a signal-correlated ±2ⁿ pattern (bit-flip noise), near-zero average DC; **silence becomes +2ⁿ/128 DC** |

The bit masks therefore create **DC even with no input**, as a bent 8-bit
sampler does.

### 3.2a AC coupling (F2, approved)

One-pole high-pass per channel after the DAC, at about 10 Hz:
`h[n] = y[n] − y[n−1] + R · h[n−1]`, with `R = 1 − 2π·10/44,108 ≈ 0.99858`.
It stands in for the DAC output coupling capacitor of a real sampler.

**Why it is needed:**

1. Without it, the mask DC (up to full scale at silence) reaches the
   outputs.
2. DC in the delay feed builds up through the feedback to `DC / (1 − fb)`
   and pins the 8-bit loop at a rail.

**What it cannot remove:** switching a high bit still makes a step, which
decays with τ ≈ 16 ms (a thump), because the masks change instantly.

**Cost:** about 3 operations per sample per channel, constant; 16 B of state
(§3.7).

### 3.3 Mono feed and the tape loop

- **Delay input (Q11):** `mono = ½ (hL + hR)`, the two AC-coupled DAC
  outputs (§3.2a). Without F2 it would be `½ (yL + yR)`, DC included.
- **Buffer:** the Effect share (`io->share`), 1 byte per sample. The arena
  is DTCM: single-cycle, uncached, never cleared by the system.
- **Write:** `w = clamp(mono + fb · wet, −1, +127/128)`, quantised with the
  same 8-bit converter as the input (`round(w · 128)`, clamped to
  −128..+127, which is the offset-binary code minus 128) and stored as int8.
  - The loop is re-quantised to 8 bits on every pass, like a sampler that
    re-records its own 8-bit output. That is the tape character, and it bounds
    the feedback (the float state can never run away).
  - The data-line masks are not re-applied in the loop (**Q8**).
- **Read (Q10):** fractional position `rp = wp − D`, wrapped by compare and
  subtract (the share is not a power of two). Linear interpolation between
  two stored samples, then `/ 128`, gives `wet` within ±1.0.
- **Tape glide (Q6):** the delay length `D` (in samples) follows its target
  `D_t` through a one-pole slew every sample, `D += (D_t − D) · k`. Proposed:
  about 150 ms time constant. A change of `Rate`, `Sync`, the division, or
  the tempo therefore glides the pitch like a tape machine changing speed.
  No second tap and no crossfade (S074 decision B8: tape style).
- **Unwritten region:** after `init` the loop content is undefined (boot
  DTCM) or stale. Rather than clearing up to 126 KB in one foreground call,
  a fill counter `valid` counts samples written since the last clear. `wet`
  is multiplied by `(D + 1 ≤ valid)`: one compare per sample, the same cost
  whatever the state. This is "clear unless you adopt" implemented by
  masking.

### 3.4 Rate, time range and Sync

**Rate (Q5):** 0..127, where a higher rate means faster tape and so a
shorter delay (proposed). Exponential:
`t(r) = T_max · (T_min / T_max)^(r/127)`, with `T_min` = 20 ms and `T_max` =
1.60 s. That is about 20 steps per octave of time.

- `D_t = t(r) · 44,108`. Rate 64 ≈ 180 ms.
- `T_max` = 1.60 s fits the **minimum** share (73,632 B = 1.67 s of 8-bit
  mono), so a Scene sounds the same whether or not future instruments claim
  voice units.
- `buffer_min_bytes = buffer_pref_bytes = 70,592` (1.60 s plus 2 guard
  samples, rounded up to 32 B).
- No row is `BUFFER_DEPENDENT`, so `effective_max` is not needed. The DSP
  still clamps `D ≤ share − 2` as a safety.
- The alternative uses up to 2.87 s of the whole arena, with a
  share-dependent limit (**Q5**).

**Sync (Q7, answered 2026-09-29):** `off`/`on`. When on, the delay time is
the step-scale division nearest (in log time) to `t(r)` at the current tempo.
The raw 0..127 Rate stays underneath:

- The pots, the encoder, storage, `.fx`, AutoSave, FX locks, Pattern
  automation and the LFO all work on the raw value. Sync only changes which
  time the DSP derives from it. Turning Rate with Sync on steps through the
  divisions as the raw value crosses each boundary.
- Only the Effect page shows the snapped division (below); everywhere else
  shows the raw number.

The rules:

- Divisions: the shared `StepScale` table, `/64 32t /32 16t /16 /8t 16. /8
  /4t /8. /4 /2 1br 2br` (6..768 ticks at 96 PPQ, `StepScale.c`).
- `t_d = ticks_d / 96 × 60 / BPM`, with BPM from `seq_getBpm()`. That
  follows the internal tempo and also MIDI and pulse clock, because
  `clockSync.c` and `triggerJacks.c` call `seq_setBpm()`.
- Only divisions with `t_d ≤ T_max` are candidates. At 120 BPM that is
  `/64` … `/2` (1 s); `1br` (2 s) and `2br` (4 s) do not fit. If none fits
  (BPM below about 4), the shortest division is used and clamped to `T_max`.
- The division is recomputed every block (14 compares and one divide:
  constant), so a tempo change glides the delay to the new length.
- **Display (Effect page only):** with Sync on, the `rte` value shows the
  division label (`/16`, `16t`, `1br` …; the `StepScale` short labels)
  instead of 0..127. This covers the compact cell (overlay and page 2), the
  full view, and a held step's `rte` value, through the new format hook
  (§5.4).
  - The STEP automation page, the `.fx` file and every other place keep the
    raw number.
  - The label follows the tempo through the existing live-refresh cadence
    (§5.4 item 4). No new RAM is needed.

### 3.5 Mix and delay pan

- **Mix (Q9):** a linear crossfade per channel, `m = mix / 127`:
  `outL = (1 − m) · hL + m · gL · wet`, `outR = (1 − m) · hR + m · gR · wet`
  (`h` = the AC-coupled DAC output; `y` if F2 is declined).
- **Delay pan (Q9):** 0..127, centre 64, with the same balance law as the
  mixer's stereo return (`mixer.c:1007`):
  `gL = p ≤ 64 ? 1 : (127 − p)/63` and `gR = p ≥ 64 ? 1 : p/64`. It only
  attenuates, as you asked.
- **Ramps:** `m`, `fb`, `gL` and `gR` ramp linearly across each 32-sample
  block from the previous block's value, as the mixer's return does (no
  zipper noise under an LFO).
- **Level:** the common `vol` and `pan` rows (0..2) still scale and place
  the whole return in the mixer. Nothing changes there.

### 3.6 Arena contract (`EFFECTS_MIXER_DSP_REFERENCE.md` §5.2)

| Op | CrumpBit behaviour |
|---|---|
| `init(rt, handoff)` | `wp = 0`, `valid = 0` (no bulk clear, §3.3), and `D` primed to `D_t` on the first block (no glide on entry). **Never adopts** in v1 (**Q19**). |
| `export_handoff(rt, out)` | type `cbt`, 1 channel, 8 bits, 44,108 Hz, share bounds, `write_offset[12] = wp`, `read_offset[12] = ⌊rp⌋`, and `FXBUF_STATE_EFFECT_WRITTEN` when `valid > 0`. |
| `buffer_changed(rt, share)` | Wrap `wp` into the new length, `valid = 0` (drop content whose geometry changed), and clamp `D`. |
| `effective_max` | NULL (no `BUFFER_DEPENDENT` row under the Q5 proposal). |
| Same-type Scene switch | The runtime stays live (no `init`), so tails keep ringing into the new Scene's settings (the S072 design, F6). The handoff is refreshed (§5.4 gap 1). |

### 3.7 Runtime state and cost

The proposed `CrumpBitRuntime` (DTCM union member):

| Field | Bytes | Purpose |
|---|---:|---|
| `wp`, `valid` | 8 | write index; fill counter |
| `delay`, `delay_target` | 8 | glide state (samples) |
| `mix`, `mix_target`, `fb`, `fb_target` | 16 | block ramps |
| `gain_l`, `gain_r`, `gain_l_target`, `gain_r_target` | 16 | pan ramps |
| `ac_x_l`, `ac_x_r`, `ac_y_l`, `ac_y_r` | 16 | AC-coupling state (F2) |
| `bit_off`, `bit_inv`, `rate`, `sync`, `subtype`, `primed` | 6 | resolved row values and flags |
| padding | 2 | |
| **Total** | **≈ 72** (56 without F2) | the union is 76 B today (StereoFilter): **no growth, 0 B RAM**, but only 4 B spare |

- **The share is not stored:** `io->share` arrives with every `process()`
  call, and `buffer_changed()` covers geometry changes.
- **Headroom:** with F2 the struct is 4 B under the union. Any further
  per-sample state (a second tap, a tone filter) would grow the union and
  shrink the arena, which needs RAM approval.
- **Cost estimate:** about 75–95 instructions per sample for both channels
  and the delay together, against 160–206 for StereoFilter's two ZDF
  channels (`EFFECTS_MIXER_DSP_REFERENCE.md` §6.1). There is no division per
  sample: one `D_t` divide and one `exp`-free table or `powf` per block.
- To be measured with every send open and `rte` under an LFO (the worst
  case).

---

## 4. Parameters (rows, flags, lanes)

Proposed table (**Q12**, **Q20–Q22**). Flags: **M** = MORPHABLE,
**L** = MODULATABLE with a 0..127 domain (LFO), **A** = AUTOMATABLE (Pattern).

| Idx | File key (permanent) | Category | Long | Short | dtype | Range | Default | Flags | Lane |
|---:|---|---|---|---|---|---|---:|---|---:|
| 0 | `effect_audio_out` | Effect | AudioOut | `out` | MENU_AUDIO_OUT | 0..5 | 0 | A | — |
| 1 | `effect_level` | Effect | Level | `vol` | 0B127 | 0..127 | 127 | M L A | 7 |
| 2 | `effect_pan` | Effect | Panning | `pan` | PM63 | 0..127 | 64 | M L A | 8 |
| 3 | `crump_bit_off` | Bits | BitOff | `bof` | 0B255 | 0..255 | 0 | — | 1 |
| 4 | `crump_bit_invert` | Bits | BitInv | `biv` | 0B255 | 0..255 | 0 | — | 2 |
| 5 | `crump_mix` | Delay | Mix | `mix` | 0B127 | 0..127 | 40 | M L A | 3 |
| 6 | `crump_feedback` | Delay | Feedback | `fbk` | 0B127 | 0..127 | 48 | M L A | 4 |
| 7 | `crump_rate` | Delay | Rate | `rte` | 0B127 (+ hook) | 0..127 | 64 | M L A | 5 |
| 8 | `crump_subtype` | CrumpBit | SubType | `sub` | 0B127 (+ hook) | 0..0 | 0 (`dly`) | — | — |
| 9 | `crump_sync` | Delay | Sync | `syn` | ON_OFF | 0..1 | 0 | A | 6 |
| 10 | `crump_dly_pan` | Delay | DlyPan | `dpn` | PM63 | 0..127 | 64 | M L A | 9 |

- **Bit masks (rows 3–4):**
  - not Morphable, because interpolating a mask produces unrelated bits;
  - not LFO-modulatable;
  - not Pattern-automatable in v1: a Pattern value is 7-bit, so it cannot
    reach bit 7 (**Q12**);
  - sequenceable through FX lanes 1–2, which store full 8-bit values
    (`effect_seq_step_t.value`), so each FX step can carry its own bit
    pattern.
- **Sub-type (row 8):** a single entry for now. `max_value` 0 keeps it fixed
  at `dly`. All 16 `DTYPE_MENU` table ids (`MenuText.h`) are in use, so its
  label comes from the new type format hook (§5.4), not a menu table.
- **Pan display quirk (existing, not fixed):** the Effect pan rows store
  64 = centre (the mixer's law), but `DTYPE_PM63` displays `value − 63`, so
  centre shows `1`. The existing common `pan` row already behaves this way;
  `dpn` follows the same convention. Worth logging in `SCOPING_TARGETS.md`.
- **No WIDE8 on the masks.** `EFFECT_PARAM_FLAG_WIDE8` only controls Pattern
  automation expansion (`effects_paramAutomatable()`,
  `effects_applyAutomation()`, the STEP-page value display), and the masks
  are not Pattern-automatable. `max_value` 255 alone gives the 0..255 domain
  for storage, clamps and FX lanes.
- **Names and keys (F4, answered):** as in the table. The sub-type uses
  delay-specific rows now; future sub-types add their own rows. Backward
  compatibility is not a constraint yet (user).
- **Lanes used:** 9 of 15. Lane 0 is Effect Morph, as always.

---

## 5. Effect page

### 5.1 Screen map

| SELECT (sub-page) | Screen | Row 0 | Cells (row 1 values) |
|---|---|---|---|
| 1 | 0 (unchanged) | `typ out vol pan` | manager and common rows |
| 1 | 1 (unchanged) | `run len scl mrp` | sequence settings, Effect Morph |
| 2 | 0 = **overlay** (home) | `- - - - - - - ->` bit states | `mix` `fbk` `rte` `mrp` (no names) |
| 2 | 1 | `mix fbk rte sub` | the same three rows, then the sub-type |
| 2 | 2 | `syn dpn` | sync, delay pan; cells 3–4 empty |

- **Layout:** a `select_layout` with `screen_count[1] = 3` and every other
  SELECT set to 0.
- **Navigation:** the encoder walks SELECT 1 s0 → s1 → overlay → page 2 →
  page 3, as the default page does ("scroll to after the 2 default
  screens").
- **`mrp` on the overlay** is the Scene Effect Morph amount (0..255), the same
  manager cell as SELECT 1 screen 1 (**Q28**). The layout needs a sentinel
  for it (§5.4).
- **Page 2 and page 3** are ordinary compact screens: names, the S074
  automation underlines, click-in full views, held-step value markers.

### 5.2 The overlay's top row

```
col:  0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15
      b0  b1  b2  b3  b4  b5  b6   b7    >
      -   -   0   -   !   -   -    -     >     (example: bit 2 off, bit 4 inverted)
```

- Columns 0, 2, …, 14 show bits 0..7, least significant bit on the left.
- `-` = normal, `0` = off, `!` = inverted. Column 15 is the ordinary
  next-screen marker (`menuEffects_scrollSign()` returns `>` on screen 0 of
  a 3-screen button).
- Row 1 is the ordinary compact value row, so the pots, the encoder, the
  full view, SHIFT (Morph view) and SEQ-hold values all work as on any
  Effect page.
- **During a SEQ hold (Q13 and F3, answered):** the top row shows the
  **last step held**: its lane 1–2 lock values where locked, otherwise the
  retained masks. The SELECT LEDs follow the same source. On release both
  return to the retained masks. There is no underline marker for the bit
  characters.
- **Hold source for the whole page:** the Effect page's held values (every
  cell, not only the masks) now come from the last step held rather than
  the lowest-numbered one (S072). One screen therefore never mixes two
  steps. The FX-step hold edit on `flt` follows the same rule. If the last
  step held is released while others stay down, the highest-numbered
  remaining step is shown.
- **Cursor (Q15):** with no names, the uppercase active-name cue disappears.
  Proposed for v1: no cursor cue (RV1–RV4 map to the four columns anyway).
  Alternatives in Q15.
- **Automation underlines (Q16):** the S074 name marker must not underline
  a bit character, so name markers are suppressed on a custom row 0. Held
  value markers (row 1) still work.

### 5.3 SELECT buttons and LEDs

| Gesture | Proposed behaviour |
|---|---|
| SELECT *n* (1..8) | Bit *n−1* moves normal → off → invert → normal (an old off+invert state counts as invert, so the next press gives normal). Written through `effects_setParameter()` on rows 3–4 (normal image; fans out to same-type masked Scenes and marks AutoSave). Then the page **jumps to the overlay**: it leaves the full view, abandons an open `typ` browse (**Q17**), and repaints with `menu_repaint()` (not `repaintAll`, per the S074 marker-ordering rule). |
| SELECT *n* while SEQ steps are held (**Q13 and F3, answered**) | Writes FX locks on the held steps instead of the retained masks. The screen and the LEDs show the **last step held** (the most recently pressed SEQ button that is still down): its lane 1–2 locks where locked, otherwise the retained masks. SELECT cycles bit *n−1* of those shown masks, and the resulting **whole `bit off` and `bit invert` values go to every held step**, so all held steps end up the same. Both mask lanes are locked on every held step. The page stays on (or jumps to) the overlay. |
| SHIFT+SELECT *n* (**Q14**) | Proposed: reset bit *n−1* to normal. |
| SELECT LEDs (**Q13 answered; Q18**) | LED *n* on when bit *n−1* is off or inverted, always, while `cbt` is on the Effect page. Source: the retained masks, or the last step held's (locked) masks while SEQ steps are held (F3); not the live per-step value during playback (Q18 default). They are re-rendered on every hold transition and after every SELECT edit. |

### 5.4 Framework changes the page needs (**Q24**)

The current Effect framework cannot express this page. Each item below is a
small, registry-driven extension that `flt` does not use (its layout and
hooks stay NULL), so its behaviour is unchanged.

1. **`effect_select_layout_t`** (`EffectsManager.h:139`), add:
   - `uint8_t custom_row0[8]`: a bit per screen that the type paints on
     row 0;
   - `uint8_t home_sub_page`, `home_screen`: the screen a hooked SELECT
     jumps to;
   - cell sentinel `EFFECT_LAYOUT_CELL_MORPH` (`0xFD`): the manager `mrp`
     cell, resolved in `menuEffects_cellAt()` (`menuEffects.c:134`).
2. **`effect_ui_hooks_t`** (`EffectsManager.h:130`), add:
   - `void (*paint_row0)(uint8_t sub_page, uint8_t screen, char row0[15])`;
   - `uint8_t (*format_value3)(uint8_t index, uint8_t value, char out[3])`,
     which returns nonzero when handled. It is used for `sub` → `dly` and
     for `rte` with Sync on. **Effect page only (Q7):** the compact cell, the
     full view and held values; the STEP automation page and storage keep
     raw numbers;
   - `uint8_t flags`, with `EFFECT_UI_OWNS_SELECT_LEDS`;
   - a select-hook return code `EFFECT_UI_SHOW_HOME` (2), meaning "handled;
     show the home screen".
3. **`menuEffects.c/.h`:**
   - the layout sentinel;
   - `menuEffects_paintRow0()` and `menuEffects_screenHasCustomRow0()`;
   - the type hook inside `menuEffects_formatValue3()` (`menuEffects.c:453`),
     plus `menuEffects_formatParamValue3(cell, value, dst)` for explicit
     values (held display);
   - `menuEffects_renderSelectLeds(sub_page)`, which calls the type's LED
     render when it owns the SELECT LEDs and otherwise
     `led_setActiveSelectButton()`;
   - `menuEffects_home()`;
   - `menuEffects_hookSelect()` (`menuEffects.c:707`) returns the action and
     abandons a `typ` browse when the hook handles the press;
   - a held-aware row writer for the SELECT hook (Q13/F3),
     `menuEffects_editParam()`: with no hold, `effects_setParameter()`;
     during a hold, `effects_setSeqLaneLock()` with the whole held mask, so
     every held step receives the same value. The hook writes both mask rows,
     so both lanes are always locked together;
   - `menuEffects_shownParam()`: the value a row shows, which is the last
     step held's lock where locked, otherwise the retained value. It is used
     by the row-0 painter, the LEDs, the SELECT hook and the Sync label;
   - the last step held is tracked in spare bits of the existing hold-flag
     byte (0 B new RAM);
   - SELECT LEDs re-rendered from `menuEffects_service()` on each hold
     transition;
   - `menuEffects_liveRefreshWanted()`: nonzero when the active type has a
     `format_value3` hook, so the Sync label follows the tempo.
4. **`menu.c`:**
   - compact branch: paint the custom row 0 after the uppercase and scroll
     marker (`menu.c:10002–10003`);
   - PARAM full view: apply the format hook after the dtype switch
     (`menu.c:9936` area);
   - `menu_applyEffectMarkers()` (`menu.c:2731`): format held PARAM values
     through the hook, and skip name markers on custom row-0 screens;
   - new public `menu_effectShowHome()`;
   - `menu_sceneLiveRefreshService()` (`menu.c:2851`): add the Effect page
     as a visible case when `menuEffects_liveRefreshWanted()`. It then gets
     the existing bounded repaint (`SCENE_LIVE_REFRESH_INTERVAL_MS`, while
     the transport runs, never while editing or under the screensaver), so
     the snapped division follows tempo changes with no new state;
   - replace the Effect-page `led_setActiveSelectButton(sub_page)` calls at
     `menu.c:10225`, `12496` and `12788` with `menuEffects_renderSelectLeds()`.
5. **`buttonHandler.c`:** the FX SELECT press (`:963–966`) and SHIFT+SELECT
   (`:937`) act on `EFFECT_UI_SHOW_HOME`, and the LED call at `:966` goes
   through `menuEffects_renderSelectLeds()`.

These are Menu, EffectsManager-contract and buttonHandler changes. The S074
startup scope ("Menu only") was for the underline bug; the new type needs
them.

---

## 6. Files

| File | Change |
|---|---|
| `Core/DSP/Effects/CrumpBit/CrumpBitParameters.c/.h` | **new** (**Q23**): tokens, descriptor table, `CRUMPBIT_PARAM_*` enum, `select_layout`, UI hooks (SELECT, LEDs, row 0, format), the Sync/Rate helper shared with the DSP |
| `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c/.h` | **new** (**Q23**): `CrumpBitRuntime`, `effect_type_ops_t` (init, export_handoff, write_param, process, buffer_changed) |
| `Core/DSP/Effects/EffectsManager.h` | type id and count; layout and hook contract extensions (§5.4) |
| `Core/DSP/Effects/EffectsManager.c` | union member, `_Static_assert`, registry row; same-type handoff refresh (gap 1) |
| `Core/DSP/Effects/FxBuffer.c` | `fxbuf_handoffResetAll()` directly after `fxbuf_clearOwners()` (gap 3, diagnostic only) |
| `Core/DSPAudio/mixer.c` | zero `mixer_fx_return_last_gain[]` in an `else` of `if (fx_active)` (`mixer.c:972`) (gap 2) |
| `Core/Menu/menuEffects.c/.h`, `Core/Menu/menu.c/.h`, `Core/Hardware/frontPanel/buttonHandler.c` | page support (§5.4) |
| `Makefile` | `CrumpBitEffect.c` in `DSP_SRCS` with an explicit `-Ofast` rule; `CrumpBitParameters.c` in `SRCS`; `-ICore/DSP/Effects/CrumpBit` |
| Docs | `EFFECTS_BUS_REFERENCE.md` (registry, page, hooks), `EFFECTS_MIXER_DSP_REFERENCE.md` §4–§5 (DSP, cost), `MODULE_INTERCHANGE_SPEC.md`, `STORAGE_SRAM_MANIFEST.md` (flash; no RAM change expected) |

---

## 7. Implementation plan (staged so you can listen early)

Each stage builds, runs and can be tested on its own.

1. **Stage 0: arena gaps 2 and 3.** The mixer return ramp while `off`, and
   the `fxbuf_init()` order. There is no audible change for `flt`, and
   switching `flt` → `off` → `flt` starts the return ramp from 0.
2. **Stage 1: the sound, default page.**
   - `CrumpBitParameters` and `CrumpBitEffect`, the registry row with **no**
     `select_layout` and **no** hooks.
   - `cbt` is selectable from `typ`; every row, including the masks as plain
     0..255 numbers, appears on the default SELECT 2 screens.
   - **This is the "try it out" build:** the 8-bit ADC, data lines, DAC and
     AC coupling (F1/F2 as answered), the delay, glide, Sync at the current
     tempo (the raw Rate number is shown until Stage 2), Mix, Pan, `.fx`
     save and load, AutoSave, and FX locks on every lane, including whole
     masks by value.
3. **Stage 2: the page.**
   - The §5.4 framework extensions, then `cbt`'s layout (overlay, page 2,
     page 3), the row-0 painter, the format hook (`dly`, Sync divisions,
     Effect page only), the live-refresh case for the Sync label, the SELECT
     hook with the held-step lock writer (Q13/F3), held-step display of the
     masks on row 0 and the LEDs, and LED ownership.
   - Regression-check `flt` and the VOICE pages.
4. **Stage 3: the contract.**
   - `export_handoff`, `buffer_changed`, and the same-type Scene switch
     refresh (gap 1).
   - Minimum-share test with `DEV_MODE_DIAGNOSTIC 1` and
     `DEV_FXBUF_FORCE_VOICE_UNITS 12` (Stage 0 made that valid).
5. **Stage 4: records.** Documentation, link numbers, hardware results.

The line-level schedule is `S074_CRUMPBIT_IMPLEMENTATION.md`; §12 records
the hardware acceptance.

---

## 8. Resources

| Resource | Estimate |
|---|---|
| Static RAM | **0 B.** The runtime (≈ 56 B) fits the existing 76 B DTCM union. Hook tables, the layout and descriptors are `const` (flash). If the final struct exceeds 76 B, the union grows and the arena shrinks; that needs RAM approval before it is merged. |
| Arena | 70,592 B of the Effect share (Q5 proposal), within the 73,632 B minimum |
| Flash | an estimated 3–5 KB (DSP, descriptors, UI hooks, framework changes); 265,704 B free |
| CPU | an estimated 70–90 instructions per sample; measure on the worst-case Scene |

**Flash crossing into sector 6:** the current image ends at `0x0807F218`,
**3,560 B below sector 6**. Stage 1 will almost certainly produce the **first
image the LXRV2 bootloader must write past `0x08080000`**, a path that has
never been tested (`MEMORY.md`, S073; §10 R1).

---

## 9. Verification

**Host:** the `tools/dsp_test` rules (`DSP_TEST.md` §6.3): stability,
bounded output, parameter jumps, a wrap at the share end, glide, Sync
division selection, and the bit-module truth table. An Effect runner in
`tools/dsp_test/` would be a **new file** (**Q23**).

**Build:**

- `make all && make img` (one build at a time);
- `link_budget.py`: `bss` and `data` unchanged;
- `DEV_MODE_DIAGNOSTIC 1` build: registry self-check `FxBf` = 0;
- confirm `sizeof(CrumpBitRuntime) ≤ 76`.

**Hardware (yours):**

1. `typ` → `cbt`; `.fx` save and reload; AutoSave restore after a reboot.
2. Bit module: every bit through normal/off/invert, on quiet and loud
   material and on silence, in both channels. Offset binary: bit 7 off or
   invert moves silence to a rail; the AC coupling (F2) lets it settle back
   to 0 within about 50 ms, with a thump on each switch.
3. During a SEQ hold: the top row and the LEDs show the held step's masks;
   SELECT writes locks; releasing restores the retained masks.
4. Sync on: the `rte` value shows the division on the Effect page and
   follows a tempo change while playing (internal and external clock); the
   STEP automation page still shows the raw number.
5. Delay: Rate across its range, glide on changes, Sync on at several
   tempos (internal and external clock), Mix 0/64/127, Feedback up to the
   maximum (bounded, no runaway), Pan extremes.
6. Page (Stage 2):
   - the overlay row matches the LEDs;
   - SELECT cycles each bit and jumps to the overlay from any screen and
     from the full view;
   - the encoder walk passes through all five screens;
   - the S074 underlines on pages 2 and 3, and none on the overlay's row 0.
7. FX sequencer: lanes 1–9 locked on held steps (masks per step, Q13).
8. Scene switch same type (tails continue) and to another type or `off`
   (clean, no stale return ramp); `typ` away and back (no old tail, Q19).
9. Minimum share (`DEV_FXBUF_FORCE_VOICE_UNITS 12`): full delay range still
   available.
10. Worst-case Scene with `cbt`, every send open, and `rte` under an LFO:
    underrun count and the `cpu` widget.
11. Regression: `flt` page and sound; VOICE underlines and holds.

---

## 10. Risks

| # | Risk | Mitigation |
|---|---|---|
| R1 | **First image past `0x08080000`.** The closed bootloader may not erase or program sector 6. | The boot image check reports `Img BAD s:.....6` instead of running corrupt code. Keep the current known-good image (`LXRV2_lxr02.img`, SHA-256 `0e004720…ddeea`) to reflash. Recover by holding the encoder at power-on. |
| R2 | Data-line changes on high bits are **loud**: with offset binary, bit 7 off or invert moves even silence to a rail, and each switch is a step. | Output stays within ±1.0 (the DAC range). The AC coupling (F2) returns it to 0 in tens of ms. Test at low monitor volume. The return still clamps at ±255 × full scale. |
| R3 | **DC:** the masks create DC up to full scale, even with no input. Without removal it reaches the outputs and, through the feedback, pins the delay loop at a rail. | The AC-coupling stage (F2, §3.2a). If it is declined, the delay feed needs its own DC removal or feedback has to stay low. |
| R4 | The page needs **framework contract changes** used by every Effect type (§5.4). | `flt` has NULL layout and hooks, so its paths are unchanged. Regression-check `flt` and VOICE (the S074 underline and marker ordering). |
| R5 | **SELECT LED ownership:** any SELECT LED writer not routed through `menuEffects_renderSelectLeds()` would overwrite the bit display. | The four Effect-page call sites are listed (§5.4). Other modes set their own SELECT LEDs on entry. |
| R6 | The runtime struct grows past 76 B. | The union grows and the arena shrinks. Stop and get RAM approval (byte count, DTCM, lifetime, owner). |
| R7 | **Sync label vs tempo:** the `rte` label depends on BPM. | The live-refresh cadence (§5.4 item 4) repaints it while playing. When stopped, a tempo change shows on the next repaint. |
| R8 | Pattern automation (7-bit) cannot address mask bit 7. | Masks are sequenced through FX lanes only in v1 (**Q12**). |
| R9 | File keys and the token are permanent once saved. | Confirm the §4 keys before Stage 1 (**Q21**). |
| R10 | "Tape" glide under a fast LFO on `rte` gives large pitch swings. | That is the intended character; the glide time sets how wild it gets (**Q6**). |

---

## 11. Decisions and follow-ups

### 11.1 Answered (user, 2026-09-29)

| Q | Answer | Where it lands |
|---|---|---|
| Q1 | Simulate an 8-bit ADC, so offset binary (code 128 = silence), with the data lines between the ADC and an 8-bit DAC | §3.1–§3.2; new follow-ups F1 and F2 |
| Q5 | Draft defaults accepted provisionally (higher = shorter, 20 ms – 1.60 s, fixed range); to be tuned after listening | §3.4 |
| Q7 | Nearest division at the current tempo. The raw 0..127 stays underneath (pots, storage, automation, locks, LFO). Only the Effect page shows the snapped division; a menu overlay is enough. | §3.4, §5.4 (format hook scope; live refresh) |
| Q13 | Yes: SELECT during a SEQ hold writes held-step locks. The SELECT LEDs and the top row show the held step's (locked) masks. No underline for the bits. | §5.2, §5.3, §5.4; follow-up F3 |
| Q24 | Framework extensions approved | §5.4 |

### 11.2 Follow-ups (answered 2026-09-29)

| F | Answer | Where it lands |
|---|---|---|
| F1 | Off is always 0; invert flips whatever the bit would otherwise be (XOR) | §3.2 |
| F2 | AC coupling approved (about 10 Hz, both channels, after the DAC) | §3.2a, §3.7 |
| F3 | All held steps end up the same. The screen and LEDs show the last step held, and any change writes that step's whole `bit off` and `bit invert` to every held step. | §5.2, §5.3 |
| F4 | §4 names, keys and sub-type approach accepted; backward compatibility is not a constraint yet | §4 |
| F5 | The four source files are approved. No `tools/dsp_test` runner was requested, so none is scheduled. | §6 |
| F6 | Noted: keep the current known-good image before flashing the first sector-6 build | §8, §10 R1 |

### 11.3 Draft defaults in force

| Q | Default |
|---|---|
| Q3 | Round to nearest (silence on code 128); hard clip at ±1.0 on input; no input-gain row in v1 |
| Q6 | Tape glide: one-pole, about 150 ms; Sync and tempo changes glide too |
| Q8 | Feedback 0..0.99; repeats are re-quantised to 8 bits but not re-masked |
| Q9 | Linear Mix crossfade; delay Pan as balance (attenuate only, the mixer's law) |
| Q10 | Linear interpolation on the delay read |
| Q11 | Delay feed `½ (L + R)` |
| Q12 | Flags as in §4. Masks: FX lanes only (no Morph, LFO or Pattern automation) |
| Q14 | SHIFT+SELECT resets that bit to normal |
| Q15 | No cursor cue on the overlay |
| Q16 | No automation underline on the overlay's row 0 |
| Q17 | SELECT leaves the full view or an open `typ` browse and shows the overlay |
| Q18 | LEDs show the retained masks, or the held step's during a hold; not the live playing value |
| Q19 | A same-type Scene switch keeps the tail; re-entering `cbt` never adopts old buffer content |
| Q22 | Defaults: Mix 40, Feedback 48, Rate 64, Sync off, Pan centre, every bit normal |
| Q26–Q30 | As drafted in §11.4 |

### 11.4 Draft 1 question list (reference)

Kept for the numbers used in the body. Q1, Q2, Q5, Q7, Q13 and Q24 are
settled or replaced (§11.1, §11.2).

#### A. The bit module

- **Q1 — Sample representation.** *Answered:* simulate an 8-bit ADC, so
  offset binary (§3.1). Draft 1 proposed sign-magnitude instead; that is
  withdrawn.
- **Q2 — "Invert" meaning.** *Replaced by §11.2 F1*, since × −1 does not fit
  an 8-bit code.
- **Q3 — Conversion.**
  - Round (default) or truncate toward zero (truncation adds a crossover dead
    zone, which is grittier).
  - Hard clip at ±1.0 full scale on the way in: OK?
  - Do you want an input-gain or drive row, now or later?
  - A DC blocker after the bit module: now a firm proposal, §11.2 F2.
- **Q30 — Never transparent.** With every bit normal the Effect is still an
  8-bit quantiser. OK?

#### B. The delay

- **Q5 — Rate.**
  - Direction: default is higher = faster tape = shorter delay. Or is rate 0
    the shortest?
  - Curve and range: default is exponential, 20 ms – 1.60 s.
  - Share use: default is a fixed `T_max` = 1.60 s that fits the minimum
    share (deterministic). The alternative, up to 2.87 s with the whole
    arena, needs a share-dependent limit: `effective_max` clamps a
    *maximum* value, and with "higher = faster" the limit is a *minimum*
    rate, so it would need either a new `effective_min` op or a row stored
    as time rather than speed.
- **Q6 — Tape glide.** One-pole, about 150 ms. Faster or slower? Should Sync
  and tempo changes glide too (default yes), or jump?
- **Q7 — Sync semantics.**
  - Default: nearest division in log time to `t(rate)` at the current BPM,
    among divisions that fit. The same Rate therefore gives different
    divisions at different tempos, and some divisions are only reachable at
    some tempos.
  - Alternative: split Rate 0..127 into 14 equal zones, one per division,
    independent of tempo.
  - Should the Effect page repaint when the tempo changes, so the label
    follows (R7)?
- **Q8 — Feedback.**
  - Range: default 0..0.99 × wet. Or allow ≥ 1.0 for self-oscillation that
    stays bounded by the 8-bit clip?
  - Should the repeats pass through the bit module again (default no; they
    are only re-quantised)?
- **Q9 — Mix and pan laws.** Linear crossfade (default) or equal power?
  Delay pan as balance, attenuating only, with the mixer's law (default)?
- **Q10 — Read interpolation.** Linear (default, smoother glides) or none
  (sample-and-hold, grittier and aliasing, fewer cycles)?
- **Q11 — Mono feed.** `½ (L + R)` (default) or `L + R` clipped?
- **Q19 — Tails (S074 B7).**
  - A same-type Scene switch keeps the runtime and its tail (default,
    design F6).
  - Re-entering `cbt` after another type never adopts old buffer content
    (default: clear by the fill counter). Or should it adopt a matching
    handoff and replay the old tail?

#### C. Parameters and storage

- **Q12 — Row flags.** The defaults are in §4. In particular, the bit masks
  are not Morph, not LFO, not Pattern automation, and FX lanes only. Pattern
  automation of masks would need a WIDE8 `expand7` rule for 7 → 8 bits:
  0..127 → bits 0..6 only, or bits 1..7?
- **Q20 — Sub-type architecture.** Future sub-types "define" the three
  parameters.
  - (a) Default: delay-specific rows and keys now; each future sub-type adds
    its own rows (61 rows available; the tighter limit is 15 lanes per type).
  - (b) Generic rows (`crump_a/b/c`) whose labels come from the sub-type
    through a label hook, so lanes and automation keep meaning across
    sub-types.
  - This decides file keys, which are permanent.
- **Q21 — Names and keys.** Confirm the short and long names (`bof biv mix
  fbk rte sub syn dpn`) and the file keys in §4.
- **Q22 — Defaults.** Mix 40, Feedback 48, Rate 64 (≈180 ms), Sync off, Pan
  centre, every bit normal. Change any?
- **Q27 — Page 3.** Only `syn` and `dpn`. Two cells are free if you want
  anything else there later.
- **Q28 — `mrp` on the overlay.** Proposed: the Scene Effect Morph amount
  (0..255), the same cell as SELECT 1 screen 1. Confirm.

#### D. Page and gestures

- **Q13 — SELECT during a SEQ hold.** Default: toggle the bit in the held
  steps' FX locks, with the LEDs and top row showing the first held step's
  masks. Or should SELECT always edit the retained masks?
- **Q14 — SHIFT+SELECT.** Default: reset that bit to normal. Or ignore it?
  Or go straight to invert?
- **Q15 — Cursor on the overlay.** No names means no uppercase cue. Options:
  - (a) none (default);
  - (b) the LCD hardware cursor under the active value;
  - (c) briefly show the active column's name while the encoder moves.
- **Q16 — Automation underlines on the overlay.** Default: none on row 0
  (bits there, not names). The same parameters show their underlines on
  page 2. OK?
- **Q17 — SELECT from the full view or during a `typ` browse.** Default:
  leave the full view, abandon the `typ` browse, show the overlay. OK?
- **Q18 — LED source.** Default: the retained masks, or the held step's
  during a hold. Alternative: the live effective masks while playing (FX
  locks, overlays), which would flicker with the sequence.
- **Q26 — Leaving the page.** Other modes restore their own SELECT LEDs on
  entry. Confirm there is no mode where the bit LEDs should persist.

#### E. Architecture and process

- **Q23 — New files.** Approve the four source files in §6
  (`CrumpBitParameters.c/.h`, `CrumpBitEffect.c/.h`). Optionally approve an
  Effect runner for `tools/dsp_test/`: new files there need your approval
  (S074 startup §3.5).
- **Q24 — Framework contract changes (§5.4).** Approve extending
  `effect_select_layout_t` and `effect_ui_hooks_t`, the SELECT-LED routing,
  and the `buttonHandler` FX SELECT path. The alternative is a `cbt`-special
  case in `menu.c`, which is not recommended: the registry is meant to keep
  type-specific UI out of Menu.
- **Q25 — Sector 6 (R1).** Stage 1 is expected to be the first image written
  past `0x08080000`. Do you want to keep a copy of the current known-good
  image and plan the first flash of Stage 1 as its own test?
- **Q29 — The common rows stay on SELECT 1 screen 0** (`out vol pan`),
  unchanged. Confirm.

---

## 12. Hardware acceptance (user, 2026-09-29)

**Result: accepted.** The user has tested the implementation on hardware and
accepts it as the v1 baseline ("pretty happy with the implementation for
now"). Additions and changes will follow in later sessions. The individual
checks in §9 and in `S074_CRUMPBIT_IMPLEMENTATION.md` §9 were not reported
one by one.

**Build record (tree at `1dbdd70`, image built 2026-09-29 18:22):**

| Item | Value |
|---|---|
| `size` | `text=499,144`, `data=416`, `bss=426,336` |
| RAM against the S073 close | **unchanged** (`data`, `bss`, ITCM 4,168 B, DTCM statics 4,448 B, FX arena 126,624 B) |
| Flash | 499,560 / 753,664 B, headroom 254,104 B (+11,600 B over the underline-fix build) |
| Image end | `_eflash_load` = `0x08081F68`, **8,040 B into sector 6** |
| `LXRV2_lxr02.img` | 499,576 B, SHA-256 `ae8ba9c0bcf966c48ce96c7c37f2e3b7564666316d920cb78d018a0e00205d8b` |

**Notes:**

- **Sector 6 (R1).** This image reaches past `0x08080000`. If it is the image
  that was accepted, the LXRV2 bootloader has written sector 6 and the boot
  image check passed. That would be the first such image, resolving R1 and
  the S073 open question. To be confirmed by the user.
- **Flash is larger than estimated** (+11.6 KB against the schedule's 3–4 KB).
  Most of it is `-Ofast` unrolling: `crumpBit_process` is 4,804 B, with the
  32-frame loop unrolled, and `crumpBit_syncDivision` is 3,684 B (the
  14-step walk unrolled, with `expf` inlined). It costs no RAM or CPU. It
  can be trimmed later if flash gets tight (for example
  `#pragma GCC unroll 1` on those loops), which would need a listening
  check.
- **Carried to the follow-up work:** the user's planned additions; logging
  the `DTYPE_PM63` pan display quirk (§4) in `SCOPING_TARGETS.md`; the §10
  documentation updates of `S074_CRUMPBIT_IMPLEMENTATION.md`.
