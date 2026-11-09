# S093 P6 — Alkaline Parameter Tweaks: Implementation Schedule

- **Session:** 093, phase 6. This document is the literal code schedule for
  `S093_P6_PARAMETER_TWEAKS.md` items 1–6 and 8:
  - the menu moves;
  - attack;
  - the decay range;
  - resonance ×1.5 with self-oscillation;
  - drive ×16;
  - `pos` on Saw/Sqr.

  Item 7 (samples/wavetables as oscillator types) is its own schedule:
  `S093_P7_WAVETABLE_SAMPLES_OSC_IMPLEMENTATION.md`.
- **Tree:** HEAD `ac3241e0` and its successors (S093 P4/P5 committed). Line
  numbers are from the current working tree. They shift as edits land, so
  apply each file's changes from the bottom up, or search for the quoted
  anchor.
- **Tags:** **ADD** / **MODIFY** / **REMOVE** / **NONE** (verified: no change
  needed, with the reason). Comment blocks are part of the change and go into
  the source as shown.
- **Binding rules (`MEMORY.md`):**
  - the CPU ceiling (whole voice, per-block work included);
  - constant per-sample cost;
  - RAM sign-off (approved for this phase: +8 B inside the voice slot);
  - no backward compatibility;
  - rows appended;
  - commits belong to the user;
  - nothing a later step needs goes in `build/`.

---

## 0. Corrections the code dive makes to the plan

| # | Plan said | The tree shows | This schedule |
|---|---|---|---|
| C1 | Today's attack is the 0.65 ms trigger point | `fit_spice.py` l. 87–112 fits `att_delay` / `tau_att` at a **0.70 ms** trigger (`VCA.veg_netlist(12.0, 0.70e-3, …)`) | `atk` runs **0.70 ms (byte 0, today exactly) → 0.55 ms (byte 127)**. SPICE gives 10→90 % attack of 2.11 → 5.15 ms, so the range is about 2–5 ms, as you asked |
| C2 | `alk_expf(−DT/τ)` at the trigger | `alkVeg_step()` l. 281 multiplies by the **compiler-folded** `expf(-ALK_DT / ALK_VEG_TAU_ATT_S)`; a runtime `alk_expf()` would not be bit-identical at byte 0 | The generator emits each point's per-block factor `keep`; the trigger interpolates three stored numbers. Byte 0 reads point 0 exactly |
| C3 | RAM +8 B in `AlkVeg` | `delayLeft` and `attLeft` exist; only the factor is new | +4 B `AlkVeg.keep` + 4 B `AlkalineVoice.attack` = **+8 B** (approved). `AlkalineVoice` 540 → 548 B inside the 1,264 B slot |
| C4 | `alk_prewarp()` clamp to 1.5 | Both tables are indexed by **knob position** (`reso` 0..1): the network at `reso × (ALK_RES_POINTS − 1)` (`AlkalineVoice.c` l. 186) and the prewarp at `res × 10` with `k > 9` hard-coded (`AlkalineControl.c` l. 304, 308) | The generator samples both at k_eff = 1.5 · knob. `ALK_RES_POINTS` 9 → **13** (+1,152 B). `ALK_PREWARP_POINTS` 11 → **16**, at the same 0.1 spacing in k_eff, so the stock range keeps every `PW_C` point (+20 B). `alk_prewarp()` uses point-count expressions; its clamp stays 0..1 (knob) |
| C5 | prewarp "derived from the model for k > 1" | `PW_C` is the linearised loop's −3 dB point (`alk_model.py` l. 503–510). It is undefined once the loop self-oscillates | c(k) **held at c(1) = 1.470 above k = 1**. The experiment did exactly this and the self-oscillation pitch was within 0.35 % of golden (P6 plan §5.5) |
| C6 | — | `alkAcc_step()` receives the same `reso`, and its VR4b gang (l. 221–222) must stay physical | The call site passes `fminf(ALK_RES_KMAX * voice->reso, 1.0f)` |
| C7 | — | The model's `k` is the physical/effective feedback (SPICE knob positions ≤ 1). The C side now takes a knob | The model keeps `k` = k_eff. **Every harness that hands a SPICE `res` to the C bench converts `k / ALK_RES_KMAX`** (§5.7) |
| C8 | — | `listen_bc.py` renders "x8" as `drv` = 1.0; under `16^drive` that is ×16 | Its x8 sets pass `drv` = 0.75 (16^0.75 = 8); a ×16 set is added (§6.3) |
| C9 | — | The harnesses pass SPICE's `dec` knob to the C voice and the model (`check_c.py` l. 165, `listen_bc.py` l. 84, `validate.py` l. 253 and 414). Under the stretched law the same knob is a different resistance | `alk_model.decay_knob_303(d)` gives the knob with the 303's r at d (closed form, §4.2); every such site converts. Measured: C2 then matches today's per step within 0.01 dB / 0.1 cent |
| C10 | "the accent branch is unchanged" | True of the branch, but the 4066 shorts VR6 only while the accent lane is high (`AlkalineVoice.c` l. 127–129), so an accented note's tail follows the stretched law | V3b's accented case (125 ms lane, then decay 1) has no SPICE target any more: 0.826 s → 1.170 s (model, measured). AL-M checks the branch with accent held (174.8 ms, unchanged) |
| C11 | — | `alk_prewarp()` hard-codes the clamp twice: `if (k > 9)` (l. 308) **and** `k = 9;` (l. 309) | Both change (§5.3). A scratch build with only l. 308 changed failed AL-F (prewarp error 0.44) |
| C12 | — | `check_c.py` C0 (l. 87) also writes the model's `k` into the tree bench's traces file | Converted too (§5.7). The variant hosts (`run_variants.py`) keep k_eff: `dlv_host.c`'s quoted includes resolve to the frozen 9-point copies in `variants/` |
| C13 | Drive +1 per block; `pos` +0.8 per sample | `t*t*t` and `t2*t2` are both two multiplies; the bend compiles to 13 instructions (`arm-none-eabi-gcc -Ofast`, Cortex-M7) | Drive +0; `pos` +13 per block (+0.41 per sample); VEG −1; MEG +0 |

**Measured for this schedule (S093 P6, scratch).** The changes in §3–§7 were
applied as written to host copies of the sources outside the tree. The bench
ran with all fourteen groups, including the new ones, at **0 failures**, and
C0–C2 ran through a copy of `check_c.py` with the §5.7 conversions. The figures
quoted below as "measured" come from those runs; nothing in the tree was
changed.

---

## 1. Work order

| Phase | Content | Gate |
|---|---|---|
| **P6-1** | Rows and menu (§2); `pos` (§7); drive (§6) | `make -C tools/dsp_test alkaline special_tags`; `make all` |
| **P6-2** | Attack: SPICE sweep, fit, generator, firmware (§3) | V3d (attack); AL-A |
| **P6-3** | Decay range: fit, generator, firmware (§4) | V3b; AL-M |
| **P6-4** | Resonance ×1.5: model, generator, firmware, harnesses, SPICE (§5) | `gen_alk_tables.py --check`; AL-R, AL-SO, AL-B; C0–C2 |
| **P6-5** | Regenerate everything; `init.alk`; docs; gates (§8–§10) | §9 |

P6-2, P6-3 and P6-4 all regenerate `AlkalineTables.h/.c`. Run the generator
once at the end of each, and `--check` in P6-5.

---

## 2. Rows and menu (items 1, 3, 4; the new rows)

### 2.1 `Core/DSP/Instruments/Alkaline/AlkalineParameters.h` l. 19–21 — MODIFY

```c
/* 28 rows: 10 Alkaline rows, vol/pan, 8 LFO rows, 2 velocity rows, 4 LFO
 * target rows, then (S093 P6, appended) alk_attack and osc1_position
 * (asserted against alkaline_param_index_t in the .c) */
#define ALKALINE_PARAM_DESCRIPTOR_COUNT 28u
```

### 2.2 `AlkalineParameters.c` l. 119–120 — MODIFY (the enum tail)

```c
    ALK_PARAM_LFO_TARGET_PARAM_2,
    /*
     * S093 P6: appended rows. Indices 26 and 27 are new, so every stored cell,
     * automation ID and NRPN number of rows 0..25 keeps its meaning.
     */
    ALK_PARAM_ATTACK,            /* 26: VEG attack as the CPU trigger width */
    ALK_PARAM_OSC1_POSITION,     /* 27: note-start phase (0 = free-running) */
    ALK_PARAM_COUNT
} alkaline_param_index_t;
```

### 2.3 `AlkalineParameters.c` l. 185 — ADD after the `lfo_target_param_2` row

```c
    /*
     * S093 P6: atk, the VEG attack as the 303's CPU trigger-pulse width.
     * Byte 0 = a 0.70 ms trigger, today's attack bit for bit; 127 = 0.55 ms
     * (SPICE 10->90 % 2.11 -> 5.15 ms). The exponential C42 discharge stays;
     * the trigger interpolates its delay, length and per-block factor from
     * alkVegAttack[] (generated). Heard after rests and long notes; on running
     * 16ths C42 has not recharged (the circuit's behaviour).
     */
    ROW("alk_attack", "Veloc EG", "Attack", "atk", DTYPE_0B127, MOD_0_127, attack, TYPE_FLT),
    /*
     * S093 P6: pos on Saw/Sqr, the Drum row shape (DrumParameters.c l. 314)
     * bound to the oscillator's position byte. 0 = free-running (the 303's
     * VCO, today's sound); 1..127 = every non-legato note starts at pos/128
     * of a cycle; while it moves, the per-block bend (AlkalineVoice.c).
     */
    ROW("osc1_position", "Oscilltr", "Position", "pos", DTYPE_0B127, MOD_0_127, osc.position, TYPE_UINT8),
```

`check_special_tags.py` — **NONE**. Both rows are `ROW` (no `_SPECIAL`).
`expected()` gives `osc1_position` no tag: its `osc1_` branch only tags
`pitch_coarse` / `pitch_fine` / `type` / `run`. `alk_attack` matches no rule.

### 2.4 `AlkalineParameters.c` l. 207–232 — MODIFY (the page comment and table)

```c
/*
 * Alkaline voice menu layout (S093 P4 F3; S093 P6 rearranged).
 *
 * What:       eight sub-pages at the index of the front-panel SELECT button
 *             (menu.c ≈ l. 4608): 0 oscillator, 1 amp envelope, 2 mod
 *             (pitch/velocity), 3 FM, 4 click (transient), 5 filter, 6 LFO,
 *             7 mix. Each page is 16 cells shown as four-cell screens.
 * Why:        S093 P6 (user): sld on mod; atk and dec on AEG (dec is the MEG
 *             decay and keeps its label: it moves so FILTER is one screen,
 *             cut res env drv); acc on click; pos with the oscillator.
 */
const instrument_menu_page_t alkaline_menu_pages[] = {
    /* 0 OSC: coa tun wav pos */
    {{ ALK_PARAM_OSC1_PITCH_COARSE, ALK_PARAM_TUNING, ALK_PARAM_WAVE, ALK_PARAM_OSC1_POSITION, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY }},
    /* 1 AEG: atk dec */
    {{ ALK_PARAM_ATTACK, ALK_PARAM_DECAY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY }},
    /* 2 mod: dst amt sld */
    {{ ALK_PARAM_VELO_MOD_DEST, ALK_PARAM_VELO_MOD_AMOUNT, ALK_PARAM_SLIDE, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY }},
    /* 3 FM: none */
    ALK_PAGE_EMPTY,
    /* 4 click: acc */
    {{ ALK_PARAM_ACCENT, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY }},
    /* 5 FILTER: cut res env drv (one screen) */
    {{ ALK_PARAM_CUTOFF, ALK_PARAM_RESO, ALK_PARAM_ENVMOD, ALK_PARAM_DRIVE, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY }},
    /* 6 LFO: the Drum layout (DrumParameters.c l. 411) */
    {{ ALK_PARAM_LFO_RATE, ALK_PARAM_LFO_SYNC, ALK_PARAM_LFO_WAVE, ALK_PARAM_LFO_OFFSET, ALK_PARAM_LFO_RETRIGGER_VOICE, ALK_PARAM_LFO_POLARITY, ALK_PARAM_LFO_AMOUNT, ALK_PARAM_LFO_AMOUNT_2, ALK_PARAM_LFO_TARGET_VOICE, ALK_PARAM_LFO_TARGET_PARAM, ALK_PARAM_LFO_TARGET_VOICE_2, ALK_PARAM_LFO_TARGET_PARAM_2, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY }},
    /* 7 MIX: vol pan */
    {{ ALK_PARAM_INSTRUMENT_VOL, ALK_PARAM_INSTRUMENT_PAN, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY }},
};
```

`ALK_PAGE_EMPTY` is now used once (page 3). The `#undef` list is unchanged.

### 2.5 `tools/alkaline/make_init_alk.py` l. 46–55 — MODIFY (`INIT`)

Add after `"lfo_target_param_2": "255",`:

```python
    # S093 P6 (appended rows): attack at a 0.70 ms trigger (today's), pos 0
    # (free-running)
    "alk_attack": "0", "osc1_position": "0",
```

Then run `python3 tools/alkaline/make_init_alk.py`. It writes 28 `[params]`
keys and 22 `[morph]` rows, since both new rows are `ROW` (FLAGS_IMAGE).
`make -C tools/dsp_test special_tags` checks the file.

---

## 3. Attack: the CPU trigger width (item 2)

### 3.1 `tools/spice/tb303/vca.py` l. 225–238 — MODIFY (the trigger sweep)

The SPICE sweep must include the widths the knob uses:

```python
    # ---------- 3. attack against trigger width (gate 12 V)
    # S093 P6: the 0.70 -> 0.55 ms range of the Alkaline atk knob (nine
    # points, alk_constants.json veg.att_table) beside the coarse S092 widths
    res["attack"] = []
    widths = (50e-6, 200e-6, 500e-6, 1e-3, 2e-3, 5e-3) + tuple(
        round(0.70e-3 - q * (0.15e-3 / 8.0), 9) for q in range(9))
    for tw in widths:
```

The loop body (l. 228–238) is unchanged. Each run writes `trigger_ms`,
`peak_uA`, `attack_10_90_ms` and `c42_min_V` into `vca_results.json`, and
V3d reads them (§3.5).

### 3.2 `tools/alkaline/fit_spice.py` l. 87–112 — MODIFY (`fit_veg()`: one fit per width)

Replace the single 0.70 ms fit with a per-width table. The first point *is*
the existing fit (same netlist, same grid), so `att_delay` / `tau_att` stay
the same numbers.

```python
    # attack (S093 P6): fit delay + exponential discharge of C42 per CPU
    # trigger width, 0.70 ms (S092 P3 §3.2: today's attack) down to 0.55 ms in
    # nine steps; the knob alk_attack interpolates this table.
    #   model: v42(t) = v0 for t < delay, v0 exp(-(t - delay) / tau) after,
    #   grid-searched for least squares on the SPICE C42 voltage after the
    #   trigger edge (t = 10 ms). Point 0 is the S092 fit exactly (same width,
    #   netlist and grid), so att_delay / tau_att keep their values.
    def fit_att(tw):
        raw = VCA.run(f"fit_veg_att_{int(round(tw * 1e7))}",
                      VCA.veg_netlist(12.0, tw, 5e-3, 0.5, 0.04, t_step=5e-6))
        t, v42 = raw[:, 0], raw[:, 3]
        m = t >= 0.01
        tt, vv2 = t[m] - 0.01, v42[m]
        best = None
        # the delay is searched to 4 ms and tau to 6 ms: a 0.55 ms trigger
        # turns Q32 only just on (5.15 ms 10->90 %)
        for delay in np.linspace(0.0, 4.0e-3, 81):
            for tau in np.linspace(0.1e-3, 6.0e-3, 119):
                mod = np.where(tt < delay, vv2[0], vv2[0] * np.exp(-(tt - delay) / tau))
                e = float(np.mean((mod - vv2) ** 2))
                if best is None or e < best[0]:
                    best = (e, delay, tau)
        return best
    widths = [round(0.70e-3 - q * (0.15e-3 / 8.0), 9) for q in range(9)]
    att = [fit_att(tw) for tw in widths]
    best = att[0]
```

Return (l. 110–112):

```python
    return dict(v42=list(map(float, grid)), i_uA=list(map(float, 1e6 * itab)),
                v_rec=float(bestr[1]), tau_rec=float(bestr[2]), rec_fit_mse=bestr[0],
                att_delay=float(best[1]), tau_att=float(best[2]), att_fit_mse=best[0],
                # S093 P6: the knob table, point 0 = the two values above
                att_table=[dict(trigger_ms=1e3 * tw, att_delay=float(b[1]),
                                tau_att=float(b[2]), fit_mse=b[0])
                           for tw, b in zip(widths, att)])
```

The new grid has the old one's 0.05 ms spacing (4 ms / 80; (6 − 0.1) / 118),
so it contains every old grid point.

**Measured (ngspice, this netlist and grid).** Point 0 reproduces the stored
fit exactly, which the generator asserts (§3.4).

| q | trigger ms | att_delay ms | tau_att ms | SPICE 10→90 % ms | firmware 10→90 % ms (per block) |
|---|---|---|---|---|---|
| 0 | 0.7000 | 0.65 | 1.20 | 2.105 | 2.176 |
| 1 | 0.6813 | 0.65 | 1.30 | 2.270 | 2.176 |
| 2 | 0.6625 | 0.70 | 1.35 | 2.460 | 2.902 |
| 3 | 0.6438 | 0.70 | 1.45 | 2.690 | 2.902 |
| 4 | 0.6250 | 0.70 | 1.60 | 2.965 | 2.902 |
| 5 | 0.6063 | 0.70 | 1.75 | 3.315 | 3.627 |
| 6 | 0.5875 | 0.65 | 2.00 | 3.755 | 4.353 |
| 7 | 0.5688 | 0.65 | 2.25 | 4.340 | 4.353 |
| 8 | 0.5500 | 0.60 | 2.60 | 5.150 | 5.078 |

The firmware column counts in 0.726 ms blocks (the VEG steps per block), so
it is within one block of SPICE at every point. Every fitted value is well
inside the grid (delay ≤ 0.70 of 4 ms, tau ≤ 2.6 of 6 ms), so no point sits
on a grid edge.

### 3.3 `tools/alkaline/alk_model.py` l. 230–256 — MODIFY (`Veg`)

```python
class Veg:
    """VCA envelope: C42 state (recharge tau R123 C42 = 1.5 s, emptied by the
    trigger with the fitted attack), current through Q31 from the gate as a
    table fitted to SPICE (S092 P6 §3.4). S093 P6: the attack follows the CPU
    trigger width through the fitted att_table (knob 0 = 0.70 ms = the
    original single fit; knob 1 = 0.55 ms), interpolated as AlkalineControl.c
    alkVeg_trigger() does: delay, discharge length 8 tau and the per-block
    factor exp(-dt / tau), each linear in the knob between points."""

    def __init__(self):
        c = constants()["veg"]
        self.v = 12.0 - 1e-3
        self.att_table = c["att_table"]
        self.v_tab = np.array(c["v42"])
        self.i_tab = np.array(c["i_uA"]) * 1e-6
        self.att_left = 0.0
        self.delay_left = 0.0
        self.keep = 1.0
        self.tau_rec = c.get("tau_rec", 1.5)
        self.v_rec = c.get("v_rec", 12.0)

    def trigger(self, attack=0.0, dt=BLOCK / FS):
        # knob position over the table points
        n = len(self.att_table)
        p = min(max(attack, 0.0), 1.0) * (n - 1)
        k = min(int(p), n - 2)
        f = p - k
        a, b = self.att_table[k], self.att_table[k + 1]
        lerp = lambda x, y: x + f * (y - x)
        self.delay_left = lerp(a["att_delay"], b["att_delay"])
        self.att_left = lerp(8 * a["tau_att"], 8 * b["tau_att"])
        self.keep = lerp(math.exp(-dt / a["tau_att"]), math.exp(-dt / b["tau_att"]))

    def step(self, dt, gate_on):
        if self.delay_left > 0:
            self.delay_left -= dt
        elif self.att_left > 0:
            self.v *= self.keep
            self.att_left -= dt
        self.v = self.v_rec + (self.v - self.v_rec) * math.exp(-dt / self.tau_rec)
        return float(np.interp(self.v, self.v_tab, self.i_tab)) if gate_on else 0.0
```

Callers of `Veg.trigger()` (`validate.py` l. 54 dsp_pattern, V3d l. 221)
keep working; `attack` defaults to 0, today's attack.

### 3.4 `tools/alkaline/gen_alk_tables.py` — MODIFY (emit the attack table)

**`emit_header()`, after l. 205 (`ALK_VEG_TAU_REC_S`) — ADD:**

```python
        "",
        "/* S093 P6: VEG attack against the CPU trigger width (alk_attack): knob",
        " * 0 = 0.70 ms (the original fit, ALK_VEG_ATT_DELAY_S / ALK_VEG_TAU_ATT_S),",
        " * knob 1 = 0.55 ms. Per point: the delay before C42 empties, the discharge",
        " * length 8 tau, and the per-block factor exp(-ALK_DT / tau) as float32 so",
        " * point 0 equals the compiler-folded constant of the former code. */",
        "typedef struct { float delay, len, keep; } AlkVegAttack;",
        f"#define ALK_VEG_ATT_POINTS    {len(veg['att_table'])}u",
        "extern const AlkVegAttack alkVegAttack[ALK_VEG_ATT_POINTS];",
```

**`emit_source()`, after the `alkVegCurrent` table (l. 280–282) — ADD:**

```python
    # S093 P6: the attack table. dt and tau are rounded to float32 first and
    # the quotient is formed in float32, as the C expression
    # -ALK_DT / ALK_VEG_TAU_ATT_S is with float constants; exp() in double,
    # then the correctly rounded float32 (what GCC's constant folding gives).
    dt32 = np.float32(np.float32(32.0) / np.float32(44108.0))
    att = c["veg"]["att_table"]
    assert (f32(att[0]["att_delay"]) == f32(c["veg"]["att_delay"]) and
            f32(att[0]["tau_att"]) == f32(c["veg"]["tau_att"])), \
        "att_table point 0 must be the original attack fit"
    lines += ["const AlkVegAttack alkVegAttack[ALK_VEG_ATT_POINTS] = {"]
    for e in att:
        tau = np.float32(e["tau_att"])
        keep = np.float32(math.exp(float(np.float32(-dt32 / tau))))
        length = np.float32(8.0) * tau          # exact: x8 is a power of two
        lines.append(f"    {{ {f32(e['att_delay'])}, {f32(length)}, {f32(keep)} }},"
                     f"   /* trigger {e['trigger_ms']:.4f} ms */")
    lines += ["};", ""]
```

### 3.5 `tools/alkaline/validate.py` l. 213–236 — MODIFY (V3d: the attack per knob)

After `out["veg"] = dict(...)` (l. 229–235), add:

```python
    # S093 P6: attack 10->90 % against SPICE per trigger width; the knob runs
    # the att_table (0 = 0.70 ms, 1 = 0.55 ms); a 1-block (0.73 ms) tolerance
    # because the model steps C42 per block
    sp_att = {round(a["trigger_ms"], 4): a["attack_10_90_ms"] for a in p3["attack"]}
    out["veg_attack"] = []
    for q, e in enumerate(M.constants()["veg"]["att_table"]):
        vg = M.Veg()
        vg.trigger(q / (len(M.constants()["veg"]["att_table"]) - 1), dt)
        cur = np.array([vg.step(dt, True) for _ in range(int(0.05 / dt))])
        t = (np.arange(len(cur)) + 1) * dt
        pk = cur.max()
        a10 = t[np.where(cur > 0.1 * pk)[0][0]]
        a90 = t[np.where(cur > 0.9 * pk)[0][0]]
        out["veg_attack"].append(dict(trigger_ms=e["trigger_ms"], dsp_ms=1e3 * (a90 - a10),
                                      spice_ms=sp_att.get(round(e["trigger_ms"], 4))))
    print("V3d attack:", out["veg_attack"])
```

### 3.6 `Core/DSP/Instruments/Alkaline/AlkalineControl.h` l. 53–57 and l. 121 — MODIFY

```c
typedef struct {
	float v;              /* C42, V */
	float delayLeft;      /* s before the trigger starts emptying C42 */
	float attLeft;        /* s of discharge left */
	/*
	 * S093 P6: the per-block discharge factor exp(-ALK_DT / tau) of this
	 * note's attack, set by alkVeg_trigger() from alkVegAttack[] (the CPU
	 * trigger width, alk_attack). 4 B; approved.
	 */
	float keep;
} AlkVeg;
```

```c
void  alkVeg_init(AlkVeg *g);
/*
 * Start the VEG attack (S092 P6 §4.4; S093 P6: the trigger width).
 *
 * What:       interpolates (delay, length, per-block factor) from
 *             alkVegAttack[] at attack x (ALK_VEG_ATT_POINTS - 1) and stores
 *             them in g; alkVeg_step() then holds C42 for the delay and
 *             multiplies it by the factor once per block for the length.
 * Why:        the 303's attack is set by how long the CPU trigger turns Q32
 *             on (S092 P3 §3.2); the exponential shape stays, its timing
 *             moves.
 * Inputs:     g; attack 0..1 (alk_attack / 127): 0 = a 0.70 ms trigger
 *             (bit-identical to the former constants), 1 = 0.55 ms.
 * Outputs:    g->delayLeft, g->attLeft, g->keep.
 * Accessors:  Alkaline_triggerVoice() (main loop, non-legato notes).
 */
void  alkVeg_trigger(AlkVeg *g, float attack);
```

### 3.7 `AlkalineControl.c` l. 263–284 — MODIFY (`alkVeg_init`, `alkVeg_trigger`, `alkVeg_step`)

```c
void alkVeg_init(AlkVeg *g)
{
	g->v = 12.0f - 1e-3f;
	g->delayLeft = 0.0f;
	g->attLeft = 0.0f;
	g->keep = alkVegAttack[0].keep;   /* the 0.70 ms attack until a trigger sets one */
}

void alkVeg_trigger(AlkVeg *g, const float attack)
{
	/* the knob over the table points; the clamp keeps a modulated value in
	 * range, and k stops one short of the end so k + 1 is a point */
	const float p = fminf(fmaxf(attack, 0.0f), 1.0f) * (float)(ALK_VEG_ATT_POINTS - 1u);
	uint32_t k = (uint32_t)p;
	float f;

	if (k > ALK_VEG_ATT_POINTS - 2u)
		k = ALK_VEG_ATT_POINTS - 2u;
	f = p - (float)k;
	{
		const AlkVegAttack *a = &alkVegAttack[k];
		const AlkVegAttack *b = &alkVegAttack[k + 1u];

		/* linear in the knob between the fitted trigger widths; at attack 0
		 * f is 0, so each is point 0 exactly: the former delay, 8 tau and
		 * the folded exp(-ALK_DT / tau) */
		g->delayLeft = a->delay + f * (b->delay - a->delay);
		g->attLeft = a->len + f * (b->len - a->len);
		g->keep = a->keep + f * (b->keep - a->keep);
	}
}

float alkVeg_step(AlkVeg *g, const uint8_t gate)
{
	if (g->delayLeft > 0.0f) {
		g->delayLeft -= ALK_DT;
	} else if (g->attLeft > 0.0f) {
		/* S093 P6: this note's factor (alkVeg_trigger()); was the folded
		 * expf(-ALK_DT / ALK_VEG_TAU_ATT_S), which is alkVegAttack[0].keep */
		g->v *= g->keep;
		g->attLeft -= ALK_DT;
	}
	g->v = ALK_VEG_V_REC + (g->v - ALK_VEG_V_REC) * expf(-ALK_DT / ALK_VEG_TAU_REC_S);
```

The rest of `alkVeg_step()` (l. 285–299) is unchanged.

**Per block:** −1 instruction (measured: the delay/attack branch compiles
to 21 instructions against 22 today, `arm-none-eabi-gcc -Ofast`, Cortex-M7). `ALK_VEG_ATT_DELAY_S` / `ALK_VEG_TAU_ATT_S` stay emitted, for the
generator's point-0 assertion and bench law AL-A. Their only firmware readers
today are `AlkalineControl.c` l. 272, 273 and 281, and this change replaces
all three (`grep -rn` over `Core/`).

### 3.8 `AlkalineVoice.h` l. 69–79 — MODIFY (the parameter fields)

Add after `float   drive;` (l. 75):

```c
	float   attack;            /* 0..1 (S093 P6, alk_attack): VEG attack as the CPU trigger width, 0 = 0.70 ms (today), 1 = 0.55 ms */
```

### 3.9 `AlkalineVoice.c` — MODIFY

- **l. 48 — ADD after `voice->drive = 0.0f;`:**

  ```c
  	voice->attack = 0.0f;   /* S093 P6: init.alk alk_attack = 0, today's attack */
  ```

- **l. 113:** `alkVeg_trigger(&voice->veg);` →
  `alkVeg_trigger(&voice->veg, voice->attack);`

### 3.10 Bench `tools/dsp_test/modules/alkaline.c` — ADD law AL-A

**Groups (l. 103–106) — MODIFY:**

```c
static unsigned g_fail[16];
enum { G_B, G_R, G_S, G_N, G_AB, G_D, G_Q, G_F, G_O, G_X, G_A, G_M, G_SO, G_PS };
static const char *g_name[] = { "AL-B", "AL-R", "AL-S", "AL-N", "AL-AB", "AL-D", "AL-Q",
                                "AL-F", "AL-O", "AL-X", "AL-A", "AL-M", "AL-SO", "AL-PS" };
```

**`main()` l. 723 — MODIFY.** The report loop is hard-coded to ten groups,
`for (g = 0u; g < 10u; g++) {`. It becomes:

```c
	/* every group in g_name (S093 P6: fourteen; was a literal 10) */
	for (g = 0u; g < sizeof g_name / sizeof g_name[0]; g++) {
```

**New function, before `main()` — ADD:**

```c
/* ---------------------------------------------------------------- AL-A
 * S093 P6: the VEG attack as the CPU trigger width.
 *   - attack 0 is the former VEG bit for bit: delay, length and factor equal
 *     the old constants (the factor against the compiler-folded expression);
 *   - the 10->90 % OTA-current rise after silence grows monotonically from
 *     knob 0 to 1 and lies in 2.0..2.8 ms at 0 and 4.6..5.7 ms at 1 (SPICE
 *     2.11 / 5.15 ms, one block of resolution plus the fit);
 *   - legato does not restart it (AL-S covers the trigger). */
static void test_attack(void)
{
	AlkVeg g;
	const float folded = expf(-ALK_DT / ALK_VEG_TAU_ATT_S);
	double prev = 0.0;
	int q;

	alkVeg_init(&g);
	alkVeg_trigger(&g, 0.0f);
	if (g.keep != folded || g.delayLeft != ALK_VEG_ATT_DELAY_S ||
	    g.attLeft != 8.0f * ALK_VEG_TAU_ATT_S)
		fail(G_A, "attack 0 is the former VEG exactly", g.keep, folded);
	for (q = 0; q <= 4; q++) {
		const float knob = (float)q / 4.0f;
		float cur[200], pk = 0.0f;
		int n, i10 = -1, i90 = -1;

		alkVeg_init(&g);
		alkVeg_trigger(&g, knob);
		for (n = 0; n < 200; n++) {
			cur[n] = alkVeg_step(&g, 1u);
			if (cur[n] > pk) pk = cur[n];
		}
		for (n = 0; n < 200; n++) {
			if (i10 < 0 && cur[n] > 0.1f * pk) i10 = n;
			if (i90 < 0 && cur[n] > 0.9f * pk) i90 = n;
		}
		{
			/* 10->90 % in whole blocks (the VEG steps once per block) */
			const double ms = (double)(i90 - i10) * ALK_DT * 1e3;

			if (ms < prev)
				fail(G_A, "attack grows with the knob", knob, ms);
			if (q == 0 && (ms < 2.0 || ms > 2.8))
				fail(G_A, "attack 0: 2.0..2.8 ms", knob, ms);
			if (q == 4 && (ms < 4.6 || ms > 5.7))
				fail(G_A, "attack 1: 4.6..5.7 ms", knob, ms);
			prev = ms;
		}
	}
}
```

In `main()`, call `test_attack();` after `test_fused();` (l. 722).

**Measured:** knob 0 / 0.25 / 0.5 / 0.75 / 1 gives 2.176 / 2.902 / 2.902 /
4.353 / 5.078 ms, and the knob-0 factor equals the folded expression. 0
failures.

---

## 4. Decay range −20 % / +50 % (item 3)

### 4.1 `tools/alkaline/fit_meg_range.py` — ADD (new file)

```python
#!/usr/bin/env python3
"""Fit the Alkaline MEG decay range (S093 P6).

What:       finds the two resistances of the decay law
              r = r_min + r_span x taper(decay)
            (R138 68 k + VR6 1 M on the 303) so that the MEG's time from the
            peak to 10 % is 0.80 x the stock time at decay 0 (-20 %, user) and
            1.50 x the stock time at decay 1 (+50 %, user). The stock times
            come from the same model with r_min = 68 k and r_span = 1 M; each
            target is solved by bisection on r with the reference MEG (the
            diode stack makes the time not proportional to r). The firmware's
            one-Newton-step MEG (FAST_CONTROL) gives the same r to within one
            block of t10 (measured, S093 P6).
Why:        S093_P6_PARAMETER_TWEAKS.md §3.2: dec 0 173 ms -> ~138 ms,
            dec 1 2.54 s -> ~3.81 s.
Inputs:     tools/alkaline/alk_constants.json (meg section); --check.
Outputs:    meg.r_min, meg.r_span written into alk_constants.json (other keys
            unchanged); a report. --check exits 1 when the stored values
            differ from a fresh fit by more than 0.1 %.
Accessors:  python3 tools/alkaline/fit_meg_range.py [--check]; then
            gen_alk_tables.py emits ALK_MEG_R_MIN / ALK_MEG_R_SPAN.
Affiliates: alk_model.Meg (reads the two values); validate.py V3b.
"""
import argparse
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.argv, _argv = sys.argv[:1], sys.argv
sys.path.insert(0, HERE)
import alk_model as M   # noqa: E402

PATH = os.path.join(HERE, "alk_constants.json")
SHORT, LONG = 0.80, 1.50          # the user's -20 % / +50 %


def t10_at_r(r):
    """MEG time from the peak to 10 % (s) with the discharge resistance r."""
    mg = M.Meg()
    mg.r_min, mg.r_span = r, 0.0          # decay knob irrelevant: r fixed
    rest = mg.n53()
    mg.trigger()
    dt = M.BLOCK / M.FS
    t, peak, tp = 0.0, None, 0.0
    while t < 8.0:
        v = mg.step(dt, 0.0, False)
        t += dt
        if peak is None or v > peak:
            peak, tp = v, t
        elif v < rest + 0.1 * (peak - rest):
            return t - tp
    return float("inf")


def solve(target):
    """r with t10_at_r(r) = target (t10 rises with r): bisection to 0.01 %."""
    lo, hi = 1e3, 2e7
    for _ in range(60):
        mid = (lo * hi) ** 0.5
        if t10_at_r(mid) < target:
            lo = mid
        else:
            hi = mid
    return (lo * hi) ** 0.5


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args(_argv[1:])
    stock_min, stock_max = t10_at_r(68e3), t10_at_r(68e3 + 1e6)
    r_min = solve(SHORT * stock_min)
    r_max = solve(LONG * stock_max)
    print(f"stock t10 {stock_min * 1e3:.1f} ms / {stock_max:.3f} s -> targets "
          f"{SHORT * stock_min * 1e3:.1f} ms / {LONG * stock_max:.3f} s: "
          f"r_min {r_min:.0f}, r_span {r_max - r_min:.0f}")
    c = json.load(open(PATH))
    if a.check:
        old = c["meg"]
        bad = (abs(old["r_min"] / r_min - 1) > 1e-3 or
               abs(old["r_span"] / (r_max - r_min) - 1) > 1e-3)
        sys.exit(1 if bad else 0)
    c["meg"]["r_min"], c["meg"]["r_span"] = r_min, r_max - r_min
    with open(PATH, "w") as fh:
        json.dump(c, fh, indent=1)


if __name__ == "__main__":
    main()
```

**Bootstrapping:** `Meg.__init__` reads `r_min`/`r_span` from the json
(§4.2), and the fit sets them on its instance before stepping. So the first
run needs the two keys present: seed `alk_constants.json` `meg` with
`"r_min": 68000.0, "r_span": 1000000.0` (the stock law) before running it.
The file is written back with `indent=1`, the format `fit_spice.py` l. 148
already uses, so only the two keys change in the diff.

**Measured (the same bisection on the tree's model):**
- stock t10 174.1 ms (decay 0) and 2.569 s (decay 1);
- targets 139.3 ms / 3.854 s;
- **r_min 54,069 Ω, r_span 1,560,243 Ω** (r_max 1.614 MΩ).

Both the reference and the FAST_CONTROL MEG give these, and the ratios check
at 0.800 / 1.500. Decay 0.5 moves 0.663 s → 0.900 s.

### 4.2 `tools/alkaline/alk_model.py` l. 136–148 — MODIFY (`Meg`)

```python
    def __init__(self):
        c = constants()["meg"]
        self.v_peak, self.v_inf, self.t_strike = c["v_peak"], c["v_inf"], c["t_strike"]
        self.drop, self.soft, self.v53_min = c["drop"], c["soft"], c["v53_min"]
        # S093 P6: the decay law's two resistances (fit_meg_range.py): the
        # 303's R138 68 k + VR6 1 M stretched to -20 % / +50 % of its range
        self.r_min, self.r_span = c["r_min"], c["r_span"]
        self.v = c["v_rest"]
        self.strike_left = 0.0
        self.i = 0.0                     # FAST_CONTROL: the stack current

    def trigger(self):
        self.strike_left = self.t_strike

    def step(self, dt, decay, accent_on):
        # accent: the 4066 shorts VR6 (68 k + 250 R, unchanged: an accented
        # note decays as on the 303); otherwise the stretched range
        r = (68e3 + R_4066) if accent_on else (self.r_min + self.r_span * taper_a(decay))
```

The rest of `step()` is unchanged.

**ADD after `taper_a()` (l. 81–83):** the stock-knob conversion (C9).

```python
def decay_knob_303(d):
    """S093 P6: the decay knob that gives, under the stretched law, the
    resistance the 303's VR6 has at knob d.

    What:       r(d) = 68 k + 1 M taper(d) on the 303; the stretched law is
                r_min + r_span taper(d'), so taper(d') = (r(d) - r_min) / r_span
                and, with taper(x) = (16^x - 1) / 15,
                d' = log16(15 (r(d) - r_min) / r_span + 1).
    Why:        SPICE's runs (e2e, control) use the 303's knob; a harness that
                hands that knob to the stretched model or C voice would
                measure the range change as an error. Every comparison
                against SPICE passes decay_knob_303(dec) instead.
    Range:      d 0 -> 0.045, d 1 -> 0.857 (both inside 0..1 for the fitted
                r_min / r_span; the -20 % / +50 % ends are beyond the 303's).
    Accessors:  check_c.py C2, listen_bc.py pattern(), validate.py V3c and
                dsp_pattern (S093_P6_IMPLEMENTATION.md §5.7).
    """
    c = constants()["meg"]
    r = 68e3 + 1e6 * taper_a(d)
    return math.log(15.0 * (r - c["r_min"]) / c["r_span"] + 1.0, 16.0)
```

### 4.3 `gen_alk_tables.py` `emit_header()` l. 208–214 — ADD after `ALK_MEG_V53_MIN`

```python
        "/* S093 P6: the decay law r = R_MIN + R_SPAN x alk_taper(decay), fitted by",
        " * fit_meg_range.py (-20 % at decay 0, +50 % at decay 1 against the 303's",
        " * 68 k + 1 M); the accent path keeps 68 k + the 4066. */",
        f"#define ALK_MEG_R_MIN         {f32(meg['r_min'])}",
        f"#define ALK_MEG_R_SPAN        {f32(meg['r_span'])}",
```

### 4.4 `AlkalineControl.c` l. 105–115 — MODIFY (`alkMeg_step()`)

```c
 * T1-2: the discharge through R138 + VR6 (or the 4066 on ACCENT) into the
 * D28 / D27 + D26 stack, whose voltage vy follows its current i:
 * r i + vy(i) = v. ...
 * S093 P6: the decay range is stretched, -20 % at decay 0 and +50 % at
 * decay 1 (user): r = ALK_MEG_R_MIN + ALK_MEG_R_SPAN x taper (fitted,
 * fit_meg_range.py). The accent branch keeps 68 k + 250 R: while the
 * accent lane is high (one step) the discharge is the 303's; the tail after
 * it follows the stretched law, as on the 303, where the 4066 shorts VR6
 * only during the lane (decay 1: 0.83 s -> 1.17 s to 10 %, the model).
 */
float alkMeg_step(AlkMeg *m, const float decay, const uint8_t accent)
{
	const float r = accent ? (68e3f + R_4066)
	                       : (ALK_MEG_R_MIN + ALK_MEG_R_SPAN * alk_taper(decay));
```

The accent value is the same float sum as before (`68e3f + R_4066`), so it is
bit-identical. **CPU 0.**

### 4.5 `validate.py` V3b, l. 184–203 — MODIFY (the targets)

**l. 192 — MODIFY:** `while t < 4.0:` → `while t < 8.0:`. Decay 1 now
takes 3.85 s to 10 %, and the accented run 1.17 s.

After `out["meg_t10"] = …` (l. 202), add:

```python
    # S093 P6: decay 0 / 1 are -20 % / +50 % of the 303 (SPICE). The accented
    # run (a 125 ms accent lane, then decay 1) has no target: the 4066 shorts
    # VR6 only during the lane, so its tail follows the stretched law (model
    # 0.826 s -> 1.170 s); AL-M checks the accent branch itself. Decay 0.5 has
    # no SPICE target (P4 measured 0.0 / 0.5 / 1.0; 0.5 is reported).
    want = {"0.0": 0.80 * sp["0.0"], "1.0": 1.50 * sp["1.0"]}
    out["meg_t10_s093p6"] = {k: dict(dsp=meg_t[k], target=v, err=meg_t[k] / v - 1)
                             for k, v in want.items()}
    print("V3b S093 P6 targets:", out["meg_t10_s093p6"])
```

The SPICE times are 173.4 ms and 2.536 s (`control_results.json`), so the
targets are 138.7 ms / 3.804 s. The model gives 140.0 ms / 3.854 s, which is
+0.9 % / +1.3 %: the model's own stock offset (+0.4 % / +1.3 %) plus one
block at decay 0. Pass: within 4 %.

**l. 253 (V3c, the accent run against SPICE at decay 0.5) — MODIFY (C9):**
`n53 = mg.step(dt, 0.5, acc)` →
`n53 = mg.step(dt, M.decay_knob_303(0.5), acc)`.

### 4.6 Bench — ADD law AL-M

```c
/* ---------------------------------------------------------------- AL-M
 * S093 P6: the MEG decay range. Time from N53's peak to 10 % of its swing,
 * the firmware MEG alone (one Newton step, per block): decay 0 is 0.80 x and
 * decay 1 is 1.50 x the model's stock times (174.1 ms / 2.569 s,
 * fit_meg_range.py), within 2 % (one block is 0.5 % at 140 ms). With accent
 * held for the whole decay the 4066 path is the 303's (68 k + 250 R), so
 * the time is the model's stock decay-0 time within 2 %. */
static double meg_t10(float decay, uint8_t accent)
{
	/* steps the MEG from a strike; pk tracks N53's maximum and npk its block,
	 * and the first block below rest + 10 % of the swing after it ends the
	 * count (20,000 blocks = 14.5 s, past the 3.85 s longest decay) */
	AlkMeg m;
	float rest, pk = -1e9f;
	int n, npk = 0;

	alkMeg_init(&m);
	rest = alkMeg_n53(&m);
	alkMeg_trigger(&m);
	for (n = 0; n < 20000; n++) {
		const float v = alkMeg_step(&m, decay, accent);

		if (v > pk) { pk = v; npk = n; }
		else if (v < rest + 0.1f * (pk - rest))
			return (double)(n - npk) * ALK_DT;
	}
	return 1e9;
}

static void test_meg_range(void)
{
	const double t0 = meg_t10(0.0f, 0u), t1 = meg_t10(1.0f, 0u), ta = meg_t10(1.0f, 1u);

	if (fabs(t0 / (0.80 * 0.1741) - 1.0) > 0.02) fail(G_M, "decay 0 = 0.80 x 174.1 ms", t0, 0.80 * 0.1741);
	if (fabs(t1 / (1.50 * 2.569) - 1.0) > 0.02) fail(G_M, "decay 1 = 1.50 x 2.569 s", t1, 1.50 * 2.569);
	if (fabs(ta / 0.1741 - 1.0) > 0.02) fail(G_M, "accent held: the 303's short decay", ta, 0.1741);
	printf("AL-M: t10 %.1f ms / %.3f s (accent %.1f ms)\n", t0 * 1e3, t1, ta * 1e3);
}
```

Call `test_meg_range();` in `main()` after `test_attack();`.

**Measured (the C MEG with r_min / r_span above):** 140.02 ms (+0.5 %), 3.8524 s
(−0.04 %), and 174.84 ms with accent held (+0.4 %). Decay 0.5 gives 0.900 s.
0 failures.

---

## 5. Resonance: feedback ×1.5, self-oscillation allowed (item 5)

### 5.1 `tools/alkaline/alk_model.py` — MODIFY (`FeedbackNet`; the knob law)

**After l. 60 (`VR = 50e3`) — ADD:**

```python
# S093 P6: the Res knob runs the effective feedback k_eff = RES_KMAX x knob
# (user: max resonance 50 % higher, self-oscillation allowed). k <= 1 is the
# 303's pot; k > 1 adds a virtual ideal transformer at the VR4a wiper
# (FeedbackNet). The model's k arguments are k_eff (SPICE knob positions are
# k_eff <= 1); the C side takes the knob, k_eff / RES_KMAX.
RES_KMAX = 1.5
```

**`FeedbackNet.__init__` / `_solve`, l. 278–348 — MODIFY.** This is the
experiment's `patch.py` (P6 plan §5.5), written into the class.

```python
    def __init__(self, k):
        """k: the effective feedback. k <= 1 is the 303's VR4a (wiper divider
        TP6 -> W -> 0). k > 1 (S093 P6) is the pot at its top plus an ideal
        transformer at the wiper: V(W) = k V(TP6), TP6 supplying k x the
        W-side current, the pot's 50 k as the load on W. At k = 1 that is the
        stock wire exactly (A, B, C, D within 4e-8, P6 plan §5.5); a plain
        buffer (VCVS) moved the dynamics by 2 % at k = 1 because it stops
        C15/C22 loading TP6, so the knob would step at the 303's maximum."""
        self.gain = float(k) if k > 1.0 else None
        rin18 = 300 * (10e3 * 12.2e3 / 22.2e3)
        if self.gain is None:
            r_top = max(50e3 * (1 - k), 1e-3)
            r_bot = max(50e3 * k, 1e-3)
            pot = (("TP6", "W", r_top), ("W", "0", r_bot))
        else:
            pot = (("W", "0", 50e3),)
        self.res = pot + (("A1", "P3", 220e3),
                    ("P3", "0", 2.2e3), ("B1", "B18", 2.2e3), ("B18", "NB18", 100e3),
                    ("NB18", "0", 100e3), ("B18", "0", rin18), ("A2", "P3", 100e3),
                    ("F1", "INR", 10e3), ("INR", "NB", 2.2e3), ("NB", "0", 2.2e3),
                    ("G1", "INL", 220e3), ("INL", "NB", 2.2e3))
        nodes = sorted({n for a, b, _ in self.res for n in (a, b)} |
                       {n for a, b, _ in self.CAPS for n in (a, b)} |
                       {"E", "S", "Q", "W", "TP6"} - {"0"})
        nodes = [n for n in nodes if n != "0"]
        self.idx = {n: i for i, n in enumerate(nodes)}
        nn = len(nodes)
        # MNA unknowns: node voltages, then the branch currents of E, S, Q
        # (and the transformer secondary for k > 1), then the 8 caps
        self.n_br = 3 + (1 if self.gain else 0)
        self.n_unk = nn + self.n_br + len(self.CAPS)
```

The column loop (l. 296–312) is unchanged.

```python
    def _solve(self, z, w, nn):
        n = self.n_unk
        G = np.zeros((n, n))
        rhs = np.zeros(n)
        def node(x):
            return None if x == "0" else self.idx[x]
        for a, b, r in self.res:
            ia, ib = node(a), node(b)
            g = 1.0 / r
            for i, j, sgn in ((ia, ia, 1), (ib, ib, 1), (ia, ib, -1), (ib, ia, -1)):
                if i is not None and j is not None:
                    G[i, j] += sgn * g
        # voltage-defined branches: E = tp_pre, S = s, Q = 0.99 V(B18); for
        # k > 1 the transformer secondary W = k V(TP6); caps V(a)-V(b) = z
        branches = [("E", "0", ("v", w[0])), ("S", "0", ("v", w[1])),
                    ("Q", "0", ("vcvs", "B18", 0.99))]
        if self.gain:
            branches.append(("W", "0", ("xfmr", "TP6", self.gain)))
        branches += [(a, b, ("v", z[q])) for q, (a, b, _) in enumerate(self.CAPS)]
        for q, (a, b, kind) in enumerate(branches):
            row = nn + q
            ia, ib = node(a), node(b)
            if ia is not None:
                G[ia, row] += 1.0           # branch current leaves a
                G[row, ia] += 1.0
            if ib is not None:
                G[ib, row] -= 1.0
                G[row, ib] -= 1.0
            if kind[0] == "v":
                rhs[row] = kind[1]
            elif kind[0] == "vcvs":         # V(a) - gain V(ref) = 0
                G[row, self.idx[kind[1]]] -= kind[2]
            else:
                # ideal transformer: V(W) - k V(TP6) = 0, and TP6 delivers k x
                # the secondary current (power conserving): the KCL term of
                # TP6 carries -k j. The sign is the one that makes k = 1 the
                # stock wire (verified, P6 plan §5.5).
                G[row, self.idx[kind[1]]] -= kind[2]
                G[self.idx[kind[1]], row] -= kind[2]
        sol = np.linalg.solve(G, rhs)
        v = sol[:nn]
        ic = sol[nn + self.n_br:]
        return v, ic
```

**l. 503–510, prewarp — MODIFY** (the comment only; `prewarp()` already clamps
via `np.interp`):

```python
# ... c(k) = f_-3dB / (w1 / 2 pi), measured on the linearised model (S092 P6
# §5.2). S093 P6: above k = 1 the linearised loop can be unstable (self-
# oscillation), where a -3 dB point does not exist; c is held at c(1) (np.interp
# clamps). Measured: the self-oscillation pitch is then within 0.35 % of the
# golden RK4 model (S093_P6_PARAMETER_TWEAKS.md §5.5).
```

**Continuity assertion — ADD to `gen_alk_tables.py` `main()`, after
`a = ap.parse_args()` (l. 333).** It runs on every regeneration and every
`--check`:

```python
    # S093 P6: the wiper transformer (k > 1) must continue the 303's pot
    # (k <= 1) without a step: the network's A, B, C, D at k = 1 and just
    # above, relative to their size. Measured 4.1e-8 (A, B) and 7.2e-9 (C, D);
    # the reversed primary sign gives 8.2e-2 (A, B; measured), a VCVS in its
    # place 2e-2 (P6 plan §5.5).
    n0, n1 = M.FeedbackNet(1.0), M.FeedbackNet(1.0 + 1e-9)
    for nm in "ABCD":
        x, y = getattr(n0, nm), getattr(n1, nm)
        rel = float(np.max(np.abs(x - y)) / np.max(np.abs(x)))
        assert rel < 1e-6, f"FeedbackNet not continuous at k = 1 ({nm}: {rel:.2e})"
```

### 5.2 `tools/alkaline/gen_alk_tables.py` — MODIFY

- **l. 42:** `RES_POINTS = 9` →
  ```python
  RES_POINTS = 13                     # S093 P6: 0.125 spacing in k_eff over 0..RES_KMAX (= today's 9 over 0..1)
  RES_KMAX = M.RES_KMAX               # S093 P6: knob 1 = feedback x1.5
  PREWARP_POINTS = 16                 # S093 P6: k_eff 0, 0.1 .. 1.5 (today's 11 points plus 5 held at c(1))
  ```
- **`n1_net()` l. 66–75** — the knob → k_eff mapping, so `res_point()`,
  `reduced_point()` and `reduced_basis()` (all of which call `n1_net(k)` with
  the knob) see the effective feedback:
  ```python
  def n1_net(k):
      """SplitNet in form N1 (S093 P1 §5): no per-sample fast modes.
      k is the knob position 0..1 (S093 P6); the network is built at the
      effective feedback RES_KMAX x k (alk_model.FeedbackNet)."""
      ...
      try:
          return M.SplitNet(RES_KMAX * k, block=NET_BLOCK)
  ```
- **`emit_header()` l. 177–180 — ADD** after `ALK_RES_POINTS`:
  ```python
          f"#define ALK_RES_KMAX          {f32(RES_KMAX)}  /* S093 P6: knob 1 = feedback x{RES_KMAX:g} */",
  ```
- **l. 194–195 — MODIFY:**
  ```python
          "/* TPT prewarp c(k_eff) at k_eff = 0, 0.1 .. RES_KMAX, indexed by the knob",
          " * (S092 P6 §5.2; S093 P6: held at c(1) above k_eff 1). */",
          f"#define ALK_PREWARP_POINTS    {PREWARP_POINTS}u",
  ```
- **`emit_source()` l. 264–267 — MODIFY** (the point comment):
  ```python
      for n in range(RES_POINTS):
          k = n / (RES_POINTS - 1)
          r = res_point(k)
          ind = "        "
          lines += [
              f"    {{   /* Res knob {k:.4f}, k_eff {RES_KMAX * k:.4f} */",
  ```
- **l. 278 — MODIFY** (the prewarp table):
  ```python
      pw = [float(np.interp(RES_KMAX * n / (PREWARP_POINTS - 1), M.PW_K, M.PW_C))
            for n in range(PREWARP_POINTS)]
      assert all(abs(pw[q] - M.PW_C[q]) < 1e-12 for q in range(11)), \
          "the stock range must keep every PW_C point (k_eff 0.1 steps)"
      lines += ["};", "",
                "const float alkPrewarpC[ALK_PREWARP_POINTS] = " + vec(pw) + ";", "",
  ```
  `vec()` takes a sequence (l. 238); `PW_C` was a tuple, `pw` is a list.
- **`main()` `--points` help (l. 326–327) — MODIFY:** "experiments only: Res
  points (the firmware uses RES_POINTS = 13 over the knob)".

The 13-point basis was measured against today's tables in the stock range at
−56 / −58 dB, LSD60 0.03 dB (plan §5.5).

### 5.3 `AlkalineControl.c` l. 299–315 — MODIFY (`alk_prewarp()`)

```c
/* ---- §5.2 TPT prewarp: the factor on the ladder current ----
 * T1-5: tan(th)/th = (945 - 105 th^2 + th^4) / (945 - 420 th^2 + 15 th^4)
 * (the [5/4] tan Pade over th; th <= 1.3, where it is within 1e-5).
 * S093 P6: alkPrewarpC is indexed by the knob over ALK_PREWARP_POINTS (16:
 * k_eff 0, 0.1 .. 1.5, held at c(1) above 1), not by a fixed 11 points. */
float alk_prewarp(const float iLad, const float res)
{
	/* the knob over the table: p = 15 x knob = 10 x k_eff, the same point
	 * and fraction as the former 10 x k for every stock setting */
	float p = fminf(fmaxf(res, 0.0f), 1.0f) * (float)(ALK_PREWARP_POINTS - 1u);
	int32_t k = (int32_t)p;
	float c, th, s;

	if (k > (int32_t)ALK_PREWARP_POINTS - 2)
		k = (int32_t)ALK_PREWARP_POINTS - 2;
```

Both clamp lines change, l. 308 (the test) and **l. 309 (`k = 9;`, the
assignment)** (C11). The rest is unchanged.

The bench's AL-F tan check (`alkaline.c` l. 436–445) uses
`alk_prewarp(il, 1.0f)` with `c(1) = 1.47`. At knob 1, c is still 1.470
(held), so AL-F is **NONE**. Measured: prewarp error 6.4e-6 against 2e-5.
With l. 309 left at `k = 9`, knob 1 reads `alkPrewarpC[9] + 6 × (…)`, outside
the segment, and AL-F fails at 0.44. That is the failure mode this law
catches.

### 5.4 `AlkalineVoice.c` l. 162–163 — MODIFY (the VR4b gang)

```c
	/* S093 P6: the accent sweep's VR4b gang stays on the 303's pot: the
	 * knob's physical rotation is min(k_eff, 1), k_eff = ALK_RES_KMAX x knob
	 * (above 1 only VR4a's virtual wiper stage grows) */
	alkAcc_step(&voice->accNet, voice->accentOn, voice->accent,
	            fminf(ALK_RES_KMAX * voice->reso, 1.0f), n53, voice->v134, &vW4, &iAcc);
```

**NONE:**
- the network index (`AlkalineVoice.c` l. 186–191) uses `ALK_RES_POINTS`;
- `alk_prewarp(iLad, voice->reso)` (l. 168) takes the knob;
- `DiodeLadder_setNet()` is unchanged.

### 5.5 `AlkalineVoice.h` l. 70 — MODIFY (the field comment)

```c
	float   reso;              /* 0..1 knob, VR4: feedback k_eff = ALK_RES_KMAX x knob (S093 P6: x1.5 at 1; the 303's max at 1/1.5) */
```

### 5.6 `tools/spice/tb303/e2e.py` l. 78–81 and l. 230 — MODIFY (a golden cross-check above k = 1)

```python
def pot(name, p1, w, p3, total, frac):
    a = max(total * frac, 1.0)
    b = max(total * (1.0 - frac), 1.0)
    return f"R{name}A {p1} {w} {a:.3f}\nR{name}B {w} {p3} {b:.3f}\n"


def vr4a(res):
    """S093 P6: VR4A for res <= 1 as before; above 1 the pot at its top plus
    an ideal transformer at the wiper (alk_model.FeedbackNet): E sets
    V(f_wip) = res V(tp6), V_X senses the secondary current, F_X draws res x
    that current from tp6; 50 k (the pot) loads the wiper."""
    if res <= 1.0:
        return pot("VR4A", "0", "f_wip", "tp6", 50e3, res)
    return (f"E_X4 f_wipx 0 tp6 0 {res:.6f}\nV_X4 f_wipx f_wip 0\n"
            f"F_X4 tp6 0 V_X4 {res:.6f}\nR_X4 f_wip 0 50k\n")
```

- **l. 230 — MODIFY:** `{pot("VR4A", "0", "f_wip", "tp6", 50e3, res)}` →
  `{vr4a(res)}`.
- **l. 267 — MODIFY:** `{pot("VR4B", "c_v4p1", "c_w4", "c_c13", 50e3, res)}` →
  `{pot("VR4B", "c_v4p1", "c_w4", "c_c13", 50e3, min(res, 1.0))}`. VR4b is the
  physical pot (C6). Above 1, `pot()` would otherwise build 75 kΩ against its
  1 Ω floor, which is no position of the pot.
- **l. 491 — ADD** to `runs`:
  ```python
          # S093 P6: the x1.5 Res top (the acid knobs at k_eff 1.5) for C1
          "acid_k15": dict(wave="saw", cut=0.35, res=1.5, env=0.6, dec=0.45, acc=0.8),
  ```
- **`check_c.py` C1, l. 112 — MODIFY:** `for tag in ("acid", "lowres"):` →
  `for tag in ("acid", "lowres", "acid_k15"):`. C0 and C2 stay on the stock
  cases, since the variants and the Python pattern driver are stock-range
  references.

**Measured (a scratch copy of `e2e.py` with exactly these edits; ngspice;
outputs in the scratchpad):**
- **Sign:** on a static netlist, `F_X4 tp6 0 V_X4 1.0` gives V(tp6) =
  0.8333 V, the same as the stock wire; the reversed F gives 1.25 V.
- **Continuity:** the full pattern at res 1.0 against 1.0001 (pot against
  transformer) differs by at most 0.48 mV on a 1.43 V peak, a level of
  0.001 dB.
- **×1.5 (`acid_k15`) against the scratch C B2R at knob 1:** level −0.36 dB,
  LSD60 7.9 dB (the stock acid run is 11.8 dB in C1 today). The spectral peak
  is 1,236 Hz against SPICE's 1,185 Hz (+4.3 %); at res 1.0 both are
  259.3 Hz. C was within 0.35 % of the golden model in self-oscillation
  (P6 plan §5.5), so the 4 % sits between the reduced model and SPICE. It is
  recorded as C1's `acid_k15` baseline: a listening item, not a gate.
- The run takes 30 s for the three cases.

### 5.7 Harness conversions (C7): knob = k / ALK_RES_KMAX at every C boundary

Two conversions (C7, C9): the C side takes the Res knob, which is
k_eff / ALK_RES_KMAX; and the stretched decay knob, which is
`decay_knob_303(dec)` for SPICE's `dec`.

| File (line) | Now | Change |
|---|---|---|
| `tools/alkaline/check_c.py` C0, l. 87 | `[np.float32(k)]` into the tree bench's traces file | `[np.float32(k / M.RES_KMAX)]` (`check_c` imports `alk_model as M` at l. 21) |
| `check_c.py` C1, l. 118 | `[np.float32(k)]` | `[np.float32(k / M.RES_KMAX)]` |
| `check_c.py` `run_pattern()` (C2), l. 164–165 | `str(knobs["res"])`, `str(knobs["dec"])` | `str(knobs["res"] / M.RES_KMAX)`, `str(M.decay_knob_303(knobs["dec"]))` |
| `tools/alkaline/listen_bc.py` `pattern()`, l. 84 | `str(knobs["res"])`, `str(knobs["dec"])` | the same two conversions; ADD `import alk_model as M  # noqa: E402` after l. 50 (it imports only `validate as V` today) |
| `tools/alkaline/listen_net.py` `golden()`, l. 86 | `[np.float32(k)]` | `[np.float32(k / M.RES_KMAX)]`; ADD `import alk_model as M  # noqa: E402` after l. 39. Its patterns go through `LB.pattern()` (listen_bc), converted above |
| `tools/alkaline/validate.py` `dsp_pattern()`, l. 414 | `mg.step(dt, knobs["dec"], acc_on)` | `mg.step(dt, M.decay_knob_303(knobs["dec"]), acc_on)` (V5 and C2's Python driver compare against SPICE's knob) |
| `validate.py` V3c, l. 253 | `mg.step(dt, 0.5, acc)` | `M.decay_knob_303(0.5)` (§4.5) |
| `validate.py` V3b, l. 193 | `mg.step(dt, dec, …)` | **NONE**: it measures the stretched law against the new targets (§4.5) |
| `tools/alkaline/variants/run_variants.py` `run_host()` l. 203, `reference()` l. 182 | `k` into the variant hosts and the golden model | **NONE**. `dlv_host.c`'s quoted includes (`"AlkalineTables.host.c"`, l. 14–17) resolve to the frozen 9-point copies in `variants/` before `-I build/gen`, so those hosts take k_eff, and the model takes k_eff |
| `validate.py` V1–V4, V6 | model `k` only | **NONE** |

Every one of these keeps the stock meaning. The SPICE `acid` res 0.85 is
k_eff 0.85, so the C knob is 0.567.

**Measured (C0–C2 through a copy of `check_c.py` with these conversions,
against the scratch P6 bench), compared with the committed
`check_c_results.json`:**
- **C0 against golden:** LSD60 within 0.01 dB and level within 0.01 dB of
  today at every case (acid and lowres, ×1/×4/×8; B2R, A-R, B-R).
- **C0 against the frozen variants:** level −0.09 dB at lowres ×1 (was
  0.00). That is the 13- against the 9-point table at res 0.2 (k_eff 0.2 falls
  between points). The variant comparison is a kernel check, and the tree's
  tables are now the reference; the golden numbers above are the gate.
- **C1:** B2R acid level −0.105 dB (was −0.107), LSD60 11.768 dB (11.795);
  lowres −0.021 dB (−0.027).
- **C2 per step:** out RMS within 0.01 dB and cutoff peaks within 0.1 cent of
  today on all 8 steps, both patterns, all three kernels.

### 5.8 Bench `tools/dsp_test/modules/alkaline.c`

**`test_ring()` l. 188–231 — MODIFY.** It now takes the knob, and finds the
table pair and prewarp for it:

```c
/* ---------------------------------------------------------------- AL-R
 * The ladder alone at the calibration point, the saw at C2 from the real
 * oscillator; the ring is read on the OTA output with I_abc constant.
 * S093 P6: at a given Res knob (the table pair and fraction as
 * Alkaline_calcVoiceAsync() forms them): knob 1/ALK_RES_KMAX is the 303's
 * maximum, knob 1 the x1.5 feedback. */
static void test_ring(int k, float knob, double *period, double *decay)
{
	...
	const float kk = knob * (float)(ALK_RES_POINTS - 1u);
	unsigned lo = (unsigned)kk;

	if (lo > ALK_RES_POINTS - 2u)
		lo = ALK_RES_POINTS - 2u;
	...
	DiodeLadder_setNet(&dl, &alkResTable[lo], &alkResTable[lo + 1u], kk - (float)lo);
	for (b = 0; b < nb; b++) {
		...
		render_k(k, &dl, buf, ALK_SAW_SCALE_V, i0 * alk_prewarp(i0, knob), 50e-6f);
	}
```

The peak search (l. 211–230) is unchanged.

**`main()` l. 698–706 — MODIFY.** AL-R at both points:

```c
	for (k = 0; k < 3; k++) {
		double per, dec, per15, dec15;

		/* the 303's maximum: the stock ring, as before */
		test_ring(k, 1.0f / ALK_RES_KMAX, &per, &dec);
		/* S093 P6: x1.5 feedback at the calibration point: a long ring that
		 * still decays (measured on the 13-point tables: B2R 1.791 ms / 0.823,
		 * B-R 1.791 / 0.822, A-R 1.791 / 0.831 per cycle; corrected in §13) */
		test_ring(k, 1.0f, &per15, &dec15);
		printf("AL-R %s: ring period %.3f ms, decay per cycle %.3f; x1.5: %.3f ms, %.3f%s\n",
		       k_name[k], per, dec, per15, dec15,
		       (k == K_AR) ? " (information: A-R is not stable at Res full with the T2-2 64-sample network, S093 P3)" : "");
		if (k == K_AR)
			continue;
		if (fabs(per - 2.0) > 0.06) fail(G_R, "ring period", k, per);
		if (fabs(dec - 0.364) > 0.03) fail(G_R, "ring decay", k, dec);
		if (fabs(per15 - 1.78) > 0.06) fail(G_R, "x1.5 ring period", k, per15);
		if (fabs(dec15 - 0.823) > 0.03) fail(G_R, "x1.5 ring decay", k, dec15);
	}
```

`listen_net.py` `ring()` (l. 74) parses
`AL-R B2R: ring period ([0-9.]+) ms, decay per cycle ([0-9.-]+)`. The new
line keeps that prefix, and the `;` after the decay value ends the match, so
it reads the stock ring as before.

**Measured:** at knob 1/1.5, B2R and B-R give 2.018 ms / 0.364 (today's,
exactly) and A-R 2.040 / 0.362; at knob 1, the values in the comment above.
0 failures.

**`pattern_rms()` (AL-AB), l. 332 — MODIFY:** `v.reso = 0.85f;` →
`v.reso = 0.85f / ALK_RES_KMAX;`. It compares kernels at the 303's settings
and stays there.

**`test_drive()` l. 375 — NONE in value.** Res knob 1 and drive 1 are now
the loudest corner (×1.5 feedback, ×16 drive), as the plan's AL-D asks. Its
messages change to ×16 in §6.2.

**AL-SO — ADD** (self-oscillation, before `main()`):

```c
/* ---------------------------------------------------------------- AL-SO
 * S093 P6: at knob 1 (feedback x1.5) with a high cutoff the loop sustains
 * after its input stops, bounded below full scale; at the 303's maximum it
 * does not. The ladder alone: a saw at C2 for 0.25 s, then silence for 0.5 s,
 * the cutoff current 4 x the calibration current. (The pitch against the
 * golden model is checked by check_c.py C1 with the acid_k15 case.) */
static double sustain_rms(float knob, int16_t *peak)
{
	static int16_t out[3 * 11027];
	DiodeLadder dl;
	OscInfo osc;
	const int nb = (int)(sizeof(out) / sizeof(out[0])) / BLK;
	const float il = 4.0f * 7.375e-6f;
	const float kk = knob * (float)(ALK_RES_POINTS - 1u);
	unsigned lo = (unsigned)kk;
	double acc = 0.0;
	int b, j, n = 0;

	if (lo > ALK_RES_POINTS - 2u)
		lo = ALK_RES_POINTS - 2u;
	memset(&osc, 0, sizeof(osc));
	osc.waveform = SAW; osc.freq = 65.406f; osc.pitchMod = 1.0f; osc.modNodeValue = 1.0f;
	osc_setFreq(&osc);
	DiodeLadder_init(&dl);
	DiodeLadder_setNet(&dl, &alkResTable[lo], &alkResTable[lo + 1u], kk - (float)lo);
	*peak = 0;
	for (b = 0; b < nb; b++) {
		int16_t *buf = &out[b * BLK];

		if (b * BLK < 11027)   /* 0.25 s of saw, then the input is silent */
			calcWavetableOscBlock(&osc, (const int16_t (*)[1024])sawTable, buf, BLK, 1.0f);
		else
			memset(buf, 0, sizeof(int16_t) * BLK);
		DiodeLadder_renderB2(&dl, buf, ALK_SAW_SCALE_V, il * alk_prewarp(il, knob), 50e-6f);
		for (j = 0; j < BLK; j++) {
			if (abs(buf[j]) > *peak) *peak = (int16_t)abs(buf[j]);
			if (b * BLK + j >= 2 * 11027) { acc += (double)buf[j] * buf[j]; n++; }
		}
	}
	return n ? sqrt(acc / n) : 0.0;
}

static void test_selfosc(void)
{
	int16_t pk15, pk10;
	const double r15 = sustain_rms(1.0f, &pk15);
	const double r10 = sustain_rms(1.0f / ALK_RES_KMAX, &pk10);

	printf("AL-SO: tail RMS x1.5 %.1f LSB (peak %d), 303 max %.1f LSB\n", r15, pk15, r10);
	if (r15 < 100.0) fail(G_SO, "x1.5 sustains after the input stops", r15, 100.0);
	if (pk15 >= 32767) fail(G_SO, "x1.5 bounded below full scale", pk15, 32767);
	if (r10 > 10.0) fail(G_SO, "the 303's maximum does not sustain", r10, 10.0);
}
```

Call `test_selfosc();` in `main()` after AL-R.

**Measured (tail RMS after the input stops; the cutoff current as a multiple
of the calibration current):**

| I / I_cal | knob 1 (×1.5): tail RMS, peak (LSB) | knob 1/1.5 (303 max): tail RMS (LSB) |
|---|---|---|
| 0.5 | 0.7, 1,296 | 0.0 |
| 1 | 339, 1,814 | 0.2 |
| 2 | 1,424, 3,455 | 0.8 |
| **4 (the law)** | **1,770, 4,454** | **1.4** |
| 8 | 1,860, 4,306 | 124 |
| 16 | 1,939, 4,719 | 510 |

At 4 × both conditions hold with a wide margin: 1,770 against 100, and 1.4
against 10. The peak is 4,454, far below full scale. Above 8 × the stock
maximum also sustains weakly on today's tables. That is today's behaviour
and outside this law. 0 failures.

**NONE:**
- `test_net()` (AL-N) loops over `ALK_RES_POINTS` and is generic;
- `test_bounds()` (AL-B) uses the extremes 0/1, so it now covers ×1.5;
- `mode_traces` / `mode_pattern` take the knob (the harnesses convert,
  §5.7);
- `mode_sweep` (l. 670) sweeps the knob 0..1, now up to ×1.5. That is an
  intended listening change; record it in the `listen_bc.py` docstring.

---

## 6. Drive ×16 (item 6)

### 6.1 `AlkalineVoice.c` l. 170–178 — MODIFY

```c
	/* drive x1..x16 (S093 P6, user: twice the former x8 maximum; D16) and the
	 * output make-up falling with it (B-2) */
	{
		const float drv = fminf(fmaxf(voice->drive, 0.0f), 1.0f);

		const float t = alk_exp2f(drv);                             /* T1-8 */
		const float t2 = t * t;

		/* 16^drv = (2^drv)^4: x1 at 0, x4 at the middle, x16 at 1 */
		voice->driveGain = t2 * t2;
		voice->makeup = alk_exp2f(ALK_MAKEUP_OCT * (1.0f - drv));
	}
```

**CPU:** +0. `t * t * t` and `t2 * t2` are both two multiplies (C13;
compiled for the M7 at `-Ofast`, 4 instructions each). The
make-up law is unchanged: at drive 1 it is ×1 (0 dB), so ×16 is 6 dB hotter
into the ladder than ×8 was. Its level is set by ear (the open B-2 device
step).

**`AlkalineVoice.h` l. 75 and l. 96 — MODIFY** (comments):
`/* 0..1: input gain 16^drive, x1..x16 (D16, E2; S093 P6 x16) */`;
`/* 16^drive, this block */`.

**`AlkalineVoice.h` l. 42–47 — MODIFY** (the make-up comment): "+6 dB at
drive ×1, 0 dB at ×16" in place of "at x8".

### 6.2 Bench `test_drive()` l. 350–390 — MODIFY (the steps, labels and messages)

**l. 352–356 — MODIFY:** five steps, x1 to x16, each a whole power of two
under 16^drive:

```c
	/* S093 P6: 16^drive: 0, 1/4, 1/2, 3/4, 1 are x1 x2 x4 x8 x16 */
	static const float drv[5] = { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };
	double rms[5];
	int d, clip;

	for (d = 0; d < 5; d++) {
		rms[d] = pattern_rms(K_B2R, drv[d], &clip);
		printf("AL-D: drive x%.0f: %+.2f dBFS RMS, %d full-scale samples\n",
		       exp2(4.0 * drv[d]), 20.0 * log10(rms[d] / 32767.0 + 1e-12), clip);
```

The loop body's three checks are unchanged:
- no full-scale sample;
- finite and non-silent;
- each step louder than 0.9 × the previous.

**l. 367, 384, 386, 388 — MODIFY (text only):** "x8" → "x16" in the block
comment `/* x16: bounded state, … */`, the `fail()` message
`"x16 state bounded and finite"`, the `printf`
`"AL-D: x16 largest input-pair argument …"` and the `fail()` message
`"x16 drives the input pair past its clamp (tail in use)"`. That block already
runs Res knob 1 and drive 1, now ×1.5 feedback and ×16: the loudest corner,
as the plan's AL-D asks.

**Measured:**

| drive | x1 | x2 | x4 | x8 | x16 |
|---|---|---|---|---|---|
| dBFS RMS (AL-AB pattern, the 303's Res) | −29.18 | −25.42 | −21.98 | −19.00 | −17.13 |
| full-scale samples | 0 | 0 | 0 | 0 | 0 |

- **The corner** (knob 1, ×16): bounded and finite; largest input-pair
  argument 12.74 (12.70 at today's ×8).
- **The top step's level is today's drive-1 level** (−17.13 dBFS at ×8
  today). The input pair is saturated there, so the doubling is heard as a
  harder edge, not as level. The make-up law (`ALK_MAKEUP_OCT`, B-2) is
  untouched, and its device level stays the user's.
- 0 failures.

### 6.3 `tools/alkaline/listen_bc.py` l. 150–155 — MODIFY (C8)

```python
        # S093 P6: drive is 16^drv now: x8 is drv 0.75, x16 is drv 1.0
        "acid_x8": (lambda e: pattern(e, knobs["acid"]["knobs"], 0.75), int(0.125 * FS)),
        "acid_x16": (lambda e: pattern(e, knobs["acid"]["knobs"], 1.0), int(0.125 * FS)),
        "sweep": (lambda e: sweep(e, 0.0), FS),
        "sweep_x8": (lambda e: sweep(e, 0.75), FS),
```

`listen_net.py` l. 117–121: the same change for its `acid_x8` / `sweep_x8`.
`check_c.py` C2 runs `--pattern` without the `drv` argument (drive 0): it is
**NONE**. Its C0/C1 `--traces` runs pass the drive as a direct gain on the
SPICE saw (`str(drv)`), not the knob, so they are also **NONE**. Add ×16 to
C0 (`for drv in (1.0, 4.0, 8.0, 16.0)`) with a golden reference from
`RV.reference(tag, 16.0)`.

---

## 7. `pos` on Saw/Sqr (item 8)

### 7.1 `AlkalineVoice.c` l. 109–114 — MODIFY (the trigger)

```c
	if (!legato) {
		/* a new note: jump the pitch, strike the MEG, trigger the VEG */
		voice->pitchJump = 1u;
		alkMeg_trigger(&voice->meg);
		alkVeg_trigger(&voice->veg, voice->attack);
		/*
		 * S093 P6: pos, the note-start phase (user).
		 *
		 * What:       pos 0 leaves the oscillator free-running (the 303's VCO,
		 *             today's sound bit for bit); pos 1..127 sets the phase to
		 *             pos/128 of a cycle at every non-legato note, so each
		 *             note starts at the same point of the waveform (a
		 *             zero-crossing start and a peak start give a different
		 *             onset into the ladder). positionApplied re-anchors to
		 *             pos either way, so no bend follows the trigger.
		 * Why:        a phase offset on a free-running oscillator is not heard
		 *             (S093 P6 plan §8.1); the reset gives pos a sound.
		 * Inputs:     osc.position (osc1_position, 0..127).
		 * Outputs:    osc.phase (pos != 0), osc.positionApplied.
		 * Affiliates: the per-block bend in Alkaline_calcVoiceAsync(); the
		 *             fused loop's 32-bit cycle (DiodeLadder.c: index ph >> 22
		 *             of 1024), so pos << 25 is pos/128 of it.
		 */
		{
			const uint8_t pos = (uint8_t)(voice->osc.position & 127u);

			if (pos != 0u)
				voice->osc.phase = (uint32_t)pos << 25;
			voice->osc.positionApplied = pos;
		}
	}
```

Legato notes take neither branch, so a slide keeps the running phase.

### 7.2 `AlkalineVoice.c` l. 158 — ADD after `osc_setTableFreq(&voice->osc);`

```c
	/*
	 * S093 P6: pos while it moves: the per-block bend (the stock waves' rule,
	 * Oscillator.h osc_positionBendApply(), INSTRUMENTS_DSP_REFERENCE §6.1.1).
	 *
	 * What:       d = the circular step from the applied position to pos,
	 *             -64..+63 (127 -> 0 is +1); positionApplied advances by d and
	 *             phaseInc gains d x OSC_POS_BEND_PER_STEP (2^25 / 32), which
	 *             held for the 32-sample block moves the phase by exactly d/128
	 *             of a cycle: the oscillator lands on the new offset, no drift.
	 * Why:        a pos change at a block edge would click; the bend spreads it
	 *             over the block as a small pitch excursion. The stock helper
	 *             is not used: it requires osc_positionBendRestore() after the
	 *             render, and Alkaline recomputes phaseInc every block
	 *             (osc_setTableFreq() above), so the bend clears itself. That is
	 *             S3 of the plan (§7.3), a saving shared with P7.
	 * Cost:       13 instructions per block (+0.41 per sample; the block
	 *             compiled alone for the M7 at -Ofast). The plan's +0.8
	 *             counted the stock apply/restore pair; the linked count
	 *             (control_cost.py) decides.
	 *             No cap: |d| <= 64 = OSC_POS_BEND_CAP.
	 * Constant:   at a steady pos d = 0 and phaseInc is unchanged (pos 0:
	 *             today's oscillator bit for bit).
	 */
	{
		const uint32_t u = ((uint32_t)(voice->osc.position & 127u) -
		                    (uint32_t)voice->osc.positionApplied + 64u) & 127u;
		const int32_t d = (int32_t)u - 64;

		voice->osc.positionApplied =
			(uint8_t)(((uint32_t)voice->osc.positionApplied + (uint32_t)d) & 127u);
		voice->osc.phaseInc += (uint32_t)(d * (int32_t)OSC_POS_BEND_PER_STEP);
	}
```

`OSC_POS_BEND_PER_STEP` is in `Oscillator.h` l. 658, which reaches this file
through `AlkalineVoice.h` → `Oscillator.h`.

### 7.3 Bench — ADD law AL-PS

```c
/* ---------------------------------------------------------------- AL-PS
 * S093 P6: pos on Saw/Sqr.
 *   - pos 0: a note does not touch the phase (free-running, as before);
 *   - pos 64: every non-legato note starts at half a cycle (phase 1 << 31);
 *   - legato keeps the running phase;
 *   - a pos sweep 0 -> 100 -> 0 leaves no lasting pitch change: after it,
 *     one block advances the phase by exactly 32 x phaseInc. */
static void test_pos(void)
{
	AlkalineVoice v;
	int16_t buf[BLK];
	uint32_t ph0;
	int n;

	memset(&v, 0, sizeof(v));
	Alkaline_initVoice(&v);
	v.osc.phase = 0x12345678u;
	Alkaline_triggerVoice(&v, 0u, 80u, 63u, ALK_STEP);
	if (v.osc.phase != 0x12345678u) fail(G_PS, "pos 0 leaves the phase alone", v.osc.phase, 0);
	v.osc.position = 64u;
	for (n = 0; n < 3; n++) block(&v, buf, K_B2R);     /* let the bend settle */
	Alkaline_triggerVoice(&v, 0u, 80u, 63u, ALK_STEP);
	if (v.osc.phase != (64u << 25)) fail(G_PS, "pos 64 starts at half a cycle", v.osc.phase, 64u << 25);
	v.slide = 1u;
	Alkaline_triggerVoice(&v, 0u, 80u, 63u, ALK_STEP);  /* latches the slide */
	block(&v, buf, K_B2R);
	ph0 = v.osc.phase;
	Alkaline_triggerVoice(&v, 0u, 80u, 63u, ALK_STEP);  /* legato */
	if (v.osc.phase != ph0) fail(G_PS, "legato keeps the phase", v.osc.phase, ph0);
	for (n = 0; n <= 100; n += 10) { v.osc.position = (uint8_t)n; block(&v, buf, K_B2R); }
	for (n = 100; n >= 0; n -= 10) { v.osc.position = (uint8_t)n; block(&v, buf, K_B2R); }
	for (n = 0; n < 4; n++) block(&v, buf, K_B2R);
	ph0 = v.osc.phase;
	block(&v, buf, K_B2R);
	if (v.osc.phase - ph0 != 32u * v.osc.phaseInc)
		fail(G_PS, "no lasting pitch change after a pos sweep", v.osc.phase - ph0, 32u * v.osc.phaseInc);
}
```

Call `test_pos();` in `main()` after `test_fused();`. AL-X (`test_fused()`)
is unchanged. Both paths run the same async, so the fused and unfused
outputs stay bit-identical with any pos.

---

## 8. Regeneration, `init.alk`, and the files that follow

**Commands, in order:**

```
python3 tools/spice/tb303/vca.py                 # the attack sweep (needs ngspice)
python3 tools/alkaline/fit_spice.py              # alk_constants.json veg.att_table
python3 tools/alkaline/fit_meg_range.py          # meg.r_min / r_span (seed first, §4.1)
python3 tools/spice/tb303/e2e.py acid_k15        # the x1.5 SPICE run for C1 (§5.6; 30 s)
python3 tools/alkaline/gen_alk_tables.py         # AlkalineTables.h/.c: 13 Res points, 16 prewarp, attack, MEG range
python3 tools/alkaline/gen_alk_tables.py --check
python3 tools/alkaline/make_init_alk.py          # SD_CARD/Instrument/Alkaline/init.alk (28 keys)
python3 tools/alkaline/check_c.py                # C0 (x16 added), C1 (acid_k15), C2
python3 tools/alkaline/validate.py --fast v3     # V3b targets, V3d attack
```

**Generated:** `Core/DSP/Instruments/Alkaline/AlkalineTables.h/.c`
(regenerated in full). `AlkalineSquareTable.c` is unchanged (`--check`).

**Flash:** +1,152 B (4 Res points) + 20 B (5 prewarp points) + 108 B
(attack table) + the code ≈ +1.4 KB. Headroom before: **49,428 B**
(`link_budget.py arm-none-eabi-nm build/lxr02.elf`, the build of `6665b5d3`).

**RAM:** +8 B inside the voice slot (`AlkalineVoice` 540 → 548), approved.
No allocation grows; `_Static_assert(sizeof(AlkalineVoice) <= INSTRUMENT_RUNTIME_SLOT_BYTES)`
holds (548 < 1,264). The struct is 4-byte aligned (540 = 4 × 135, no
8-byte member), and both additions are floats beside floats, so no padding
appears: +4 for `AlkVeg.keep` (12 → 16 B) and +4 for `attack`.

---

## 9. Gates

| Gate | Command | Pass |
|---|---|---|
| Host | `make -C tools/dsp_test alkaline alkaline_tables special_tags oscillator seq_steplen`, one by one | All groups 0 failures, including AL-A, AL-M, AL-SO, AL-PS and the new AL-R pair; the legato break still caught |
| Tables | `gen_alk_tables.py --check`; `fit_meg_range.py --check` | Match |
| Model | `check_c.py`; `validate.py --fast v3` | Against the committed `check_c_results.json` at the stock cases: C0 vs golden LSD60 and level within 0.02 dB; C1 level within 0.02 dB; C2 per step within 0.02 dB and 0.2 cent (measured: 0.01 / 0.01 / 0.01 dB, 0.1 cent, §5.7). C1 `acid_k15` recorded (measured −0.36 dB, LSD60 7.9 dB, §5.6). V3b targets within 4 %; V3d attack within one block of SPICE per point (measured, §3.2) |
| Build | `make all`; `link_budget.py` | Clean; headroom ≥ 16 KiB |
| CPU | `control_cost.py --elf build/lxr02.elf`; `elf_looppath.py --sym Alkaline_calcVoiceSyncBlock --cond-heads --in-range` | Loop 214 (unchanged: no P6 edit is in the sync block). Per block 1,218 + 12: `pos` bend +13, VEG −1, drive and MEG +0 (C13). The attack's interpolation runs in the trigger, not per block. Whole voice ≈ 252.1 + 0.4 = **252.5**, against 261.3 |
| Others | `make -C tools/dsp_test elf_p4_check`, then `elf_loopsrc.py --compare` (base and new `-g` twins) | 0 MISSING, 0 grew |
| Device | Listening: attack after rests (byte 0 against 127), decay 0/127, the resonance top and self-oscillation, drive ×16, `pos` 0/64; CPU worst case (four Alkalines at Res and drive 1) | User |

---

## 10. Documents

- `knowledge_files/specification_reference/dsp_instruments_effects/INSTRUMENTS_DSP_REFERENCE.md`
  §5.5:
  - the page layout;
  - attack as the trigger width (the table, byte 0 = today);
  - the decay law and the fit;
  - Res ×1.5 (wiper transformer, 13 points, c held above 1, VR4b physical,
    self-oscillation);
  - drive ×16;
  - `pos` (start phase; 0 free-running);
  - the measured cost.
- `FILESYSTEM_SPEC.md`: 28 Alkaline keys (`alk_attack`, `osc1_position`); the
  new meanings of `alk_decay` / `alk_reso` / `alk_drive` bytes (no
  migration).
- `MIDI_NRPN_TABLE.md`: regenerate (rows 26–27).
- `MIDI_SPEC.md` §5.5: the value notes for `atk` and `pos`.
- `CPU_USE_DSP_AUDIT.md`: the item 71 figures after P6.
- The 093 log: P6 entry.
- `MEMORY.md`: the knob → k_eff rule (C side takes the knob; the model takes
  k_eff; harnesses convert).

---

## 11. Verified in the tree for this schedule

| Fact | Where |
|---|---|
| Res index `reso × (ALK_RES_POINTS − 1)`, clamp 0..1 | `AlkalineVoice.c` l. 186–191 |
| prewarp `res × 10`, `k > 9` | `AlkalineControl.c` l. 304, 308 |
| VR4b `ra/rb = VR × res / (1 − res)` | `AlkalineControl.c` l. 221–222 |
| VEG attack folded `expf(-ALK_DT / ALK_VEG_TAU_ATT_S)`, length `8 × τ` | `AlkalineControl.c` l. 270–283 |
| Today's attack fit at 0.70 ms | `fit_spice.py` l. 87–112 |
| SPICE width sweep list | `vca.py` l. 227 |
| MEG law `68e3 + (accent ? R_4066 : 1e6 × taper)` | `AlkalineControl.c` l. 115; `alk_model.py` l. 148 |
| Drive `t × t × t` | `AlkalineVoice.c` l. 174–176 |
| Fused-loop phase: full 32-bit cycle, `ph >> 22` of 1024 | `DiodeLadder.c` l. 308 |
| `OSC_POS_BEND_PER_STEP` = 2²⁵ / 32, cap 64 | `Oscillator.h` l. 656–658 |
| Drum's `osc1_position` row shape | `DrumParameters.c` l. 314 |
| The model's `FeedbackNet` MNA and the transformer's sign (continuity 4e-8 at k = 1) | `alk_model.py` l. 278–357; scratch experiment |
| `PW_C` definition (linearised −3 dB) | `alk_model.py` l. 503–510 |
| Harness Res sites | `check_c.py` l. 112–118, 164; `listen_bc.py` l. 84; `listen_net.py` l. 86; `run_variants.py` l. 168–183 |
| SPICE VR4A/VR4B pots | `e2e.py` l. 230, 267 |
| Bench laws and groups | `alkaline.c` l. 103–106, 188–231, 332, 375, 698–722 |
| Bench report loop hard-coded to 10 groups | `alkaline.c` l. 723 |
| `alk_prewarp()` clamp assignment `k = 9;` | `AlkalineControl.c` l. 309 |
| Accent lane clears `accentOn` | `AlkalineVoice.c` l. 127–129 |
| `dec` reaches the C voice / model from SPICE knobs | `check_c.py` l. 165; `listen_bc.py` l. 84; `validate.py` l. 253, 414 |
| Variant hosts include their frozen tables (quoted includes, own directory first) | `variants/dlv_host.c` l. 14–17; `run_variants.py` l. 189–196 |
| SPICE VR4B takes the raw `res` | `e2e.py` l. 267 |
| `init.alk` today: 26 `[params]`, 20 `[morph]` | `SD_CARD/Instrument/Alkaline/init.alk` |
| Flash headroom 49,428 B | `link_budget.py`, `build/lxr02.elf` |

**Measurements behind this schedule (S093 P6, scratch; no tree file changed).**
The scripts and outputs are in the session scratchpad (`p6chk/`).

| What | How | Result |
|---|---|---|
| Attack table | ngspice, nine widths, the §3.2 grid | Point 0 = 0.65 / 1.20 ms (the stored fit); SPICE 2.105 → 5.150 ms |
| Firmware attack | per-block VEG, the generated table | 2.176 → 5.078 ms; knob-0 factor = folded constant |
| Decay fit | bisection on the tree's model | r_min 54,069 Ω, r_span 1,560,243 Ω; ratios 0.800 / 1.500 |
| Bench, all §3–§7 edits applied to host copies | `modules/alkaline.c` with AL-A/M/SO/PS and the new AL-R pair | 14 groups, 0 failures |
| C0–C2 | `check_c.py` copy with the §5.7 conversions | Stock cases within 0.01 dB / 0.1 cent of today |
| SPICE transformer | static netlist; full e2e at 1.0 / 1.0001 / 1.5 | Sign correct; continuity 0.001 dB; ×1.5 against C −0.36 dB, LSD60 7.9 dB, peak +4.3 % |
| Cost of the control edits | `arm-none-eabi-gcc -Ofast -mcpu=cortex-m7` on each edit | `pos` 13, drive 0, MEG 0, VEG −1 instructions |

---

## 12. Implementation log (2026-11-09)

**Status: implemented in the tree and gated; the device listening/CPU trial
is the only open item.**

### 12.1 Order actually taken

1. `alk_model.py`: `RES_KMAX`, `decay_knob_303()`, `Meg` decay law, `Veg`
   attack table, `FeedbackNet` wiper transformer, prewarp comment.
2. `fit_spice.py` per-width attack fit and `vca.py` sweep; ran both
   (ngspice 47). `fit_meg_range.py` added and run (seed values, then the
   fit). `gen_alk_tables.py` range/table/attack/MEG changes; regenerated
   `AlkalineTables.h/.c` (and `--check`).
3. Firmware: `AlkalineControl.c/.h`, `AlkalineVoice.c/.h`,
   `AlkalineParameters.c/.h`; `make_init_alk.py` and `init.alk`.
4. Bench: 14 groups, AL-A/M/SO/PS and the AL-R pair; drive x16; `pos`.
5. Harnesses: `validate.py` V3b/V3c/V3d and `dsp_pattern`;
   `check_c.py` C0/C1/C2; `listen_bc.py`; `listen_net.py`; `e2e.py`
   (`vr4a()` and `acid_k15`); ran `e2e.py acid_k15`, `check_c.py`,
   `validate.py --fast v3`.
6. Build and static gates; documents.

### 12.2 Measurements reproduced in the tree

| Item | Measured | Schedule expectation |
|---|---|---|
| SPICE attack sweep | 2.105 → 5.150 ms | 2.105 → 5.150 ms |
| VEG fitted table | delay 0.65→0.60 ms, tau 1.20→2.60 ms | same |
| Firmware attack (AL-A / V3d) | 2.176 / 2.902 / 2.902 / 4.353 / 5.078 ms | same (one block) |
| MEG fit | stock 174.1 ms / 2.569 s; targets 139.3 ms / 3.853 s; r_min 54,069, r_span 1,560,243 | same |
| Firmware MEG (AL-M) | 139.3 ms / 3.852 s; accent held 174.8 ms | 140.0 ms / 3.852 s / 174.8 ms |
| Transformer continuity | A/B 4.1e-8, C/D 7.2e-9 | 4e-8 / 7e-9 |
| AL-R | 2.018 ms / 0.364 (stock); ×1.5 1.791 ms / 0.823 (B2R, B-R), A-R 1.791 / 0.831 | same |
| AL-SO | tail 1,769.9 LSB, peak 4,448; 303 max 1.4 LSB | 1,770 / 4,454 / 1.4 |
| AL-D | x1/x2/x4/x8/x16 = −29.18/−25.42/−21.98/−19.00/−17.13 dBFS; 0 clipped; corner 12.74 | same |
| C1 acid / lowres | −0.105 / −0.021 dB; LSD60 11.768 / 4.681 | −0.105 / −0.021; 11.768 |
| C1 acid_k15 | −0.361 dB, LSD60 7.906 dB | −0.36 dB, 7.9 dB |
| C0 ×16 | added; B2R acid 5.317 dB LSD60 vs golden, level −0.15 dB | new case |
| C2 | per-step within 0.01 dB / 0.1 cent of the committed run | same |
| V3b | +0.93 % / +1.30 % of the new targets | within 4 % |
| CPU | loop 214 (3 VDIV); per block 884+332+14 = 1,230 (9 VDIV); whole 252.5 | 252.5 |
| Flash | headroom 47,836 B; ITCM 14,552 B | +1.4 KB, ≥16 KiB |

### 12.3 Gates

- `make -C tools/dsp_test alkaline`: 14 groups, 0 failures; the legato break
  still caught.
- `gen_alk_tables.py --check`, `make_init_alk.py --check`, `special_tags`
  (199 rows), `alkaline_tables`: pass.
- `check_c.py`: pass; `validate.py --fast v3`: V3b targets, V3c, V3d attack
  per point all as expected.
- `make all`: clean; `link_budget.py`: headroom 47,836 B.
- `control_cost.py --elf`: 1,230 / 9 VDIV; `elf_looppath.py`: 214 / 3 VDIV.
- `make -C tools/dsp_test elf_p4_check`: 93/93 exact oscillator loops and
  30 hot mixer loops equal, 0 grew. `elf_loopsrc.py --compare` was not run:
  it is the source-pairing check for a GREW, and there was none.
- `oscillator`, `seq_steplen`: pass.

### 12.4 Deviations / notes

- `Meg.__init__` reads `r_min`/`r_span` with the stock defaults when the
  keys are absent. `fit_spice.py` rewrites `alk_constants.json` from
  scratch, so this keeps the documented `fit_spice.py` →
  `fit_meg_range.py` order reproducible without a manual seed step; the
  fitted values are still written back and used.
- `MIDI_NRPN_TABLE.md` has no generator in `tools/`; rows 26–27, the counts
  and the value notes were edited by hand from the descriptor table.
- `listen_net.py` changed `acid_x8` and `sweep_x8` to drive 0.75 only, as
  scheduled; the ×16 listening set lives in `listen_bc.py`.
- The SPICE `.cir` files for the new runs were written under
  `tools/spice/tb303/out/`, the tracked record folder.
- Regenerated records: `alk_constants.json`, `vca_results.json`,
  `e2e_results.json` (+`acid_k15`), `check_c_results.json`,
  `validate_results.json`, the C comparison WAVs and the new plots.

---

## 13. Assessment of the implementation (2026-11-09)

**Verdict: P6 is implemented as scheduled and every gate holds.** I read each
source change against this schedule and re-ran every host and static gate
myself on the working tree (not from the §12 log). Two figures in this
schedule were wrong. Both came from my scratch runs, not from the
implementation, and both are corrected below. One small robustness issue
remains in the tools (F3). Open: the device trial (OPEN_TESTS A5, A10–A13).

### 13.1 Code review against the schedule

| Area | Schedule | Tree | Verdict |
|---|---|---|---|
| Rows, enum, count, pages | §2 | rows 26–27 appended; count 28; pages 0 `coa tun wav pos`, 1 `atk dec`, 2 `dst amt sld`, 4 `acc`, 5 `cut res env drv`, 7 `vol pan` | as written |
| Attack | §3.6–§3.9 | `AlkVeg.keep`; `alkVeg_trigger(g, attack)` interpolation; `g->v *= g->keep`; `voice->attack` init 0; trigger passes it | as written |
| Decay | §4.4 | `accent ? 68e3f + R_4066 : R_MIN + R_SPAN × taper` | as written (comment corrected, F2) |
| Prewarp | §5.3 | both clamp lines use `ALK_PREWARP_POINTS` (C11) | as written |
| VR4b | §5.4 | `fminf(ALK_RES_KMAX * reso, 1)` | as written |
| Drive | §6.1 | `t2 * t2`; header comments ×16 | as written |
| `pos` | §7.1–§7.2 | trigger reset in the non-legato branch; per-block bend after `osc_setTableFreq()` | as written |
| Generated tables | §3.4, §4.3, §5.2 | 13 Res points (identical to the scratch set, 936 of 936 numbers); 16 prewarp points (stock range = `PW_C`); attack point 0 = today's constants; `ALK_MEG_R_MIN` 54,068.84 / `R_SPAN` 1,560,242.5 | as written |
| Model, fits, harness conversions | §3.1–§3.5, §4.1–§4.5, §5.1–§5.7, §6.3 | transformer branch; `decay_knob_303()`; `k / RES_KMAX` at C0, C1, C2, `listen_bc`, `listen_net`; `decay_knob_303()` at C2, `listen_bc`, V3c, `dsp_pattern`; V3b to 8 s, accented target dropped; VR4B `min(res, 1)`; `acid_k15`; C0 ×16; x8 = drive 0.75 | as written; one deviation (F3) |
| Bench | §3.10, §4.6, §5.8, §6.2, §7.3 | 14 groups; AL-A/M/SO/PS; AL-R pair; AL-D five steps; report loop over `g_name` | as written (comment corrected, F1) |

### 13.2 Gates, re-run for this assessment

| Gate | Result |
|---|---|
| `make -C tools/dsp_test alkaline` | 14 groups, 0 failures; legato break caught |
| `alkaline_tables`, `gen_alk_tables.py --check` | match |
| `fit_meg_range.py --check` | 0 (r_min 54,069, r_span 1,560,243) |
| `special_tags`; `make_init_alk.py --check` | 199 rows, 0 mismatches; `init.alk` 28 rows (22 morph) |
| `oscillator`, `seq_steplen` | every group 0 failures; breaks caught |
| `make all` | clean, no warnings |
| `link_budget.py` | flash headroom 47,836 B (P5 image 49,428: −1,592 B); ITCM 14,552 B (unchanged) |
| `control_cost.py --elf` | 884 + 332 + 14 = **1,230** per block, 9 VDIV |
| `elf_looppath.py` sync block | **214**, 3 VDIV |
| Whole voice | 214 + 1,230/32 = **252.4**, against 261.3 |
| `elf_p4_check` | oscillators 93/93 exact; mixer 30 hot loops equal, 0 grew |
| Sizes | `Alkaline_calcVoiceAsync` 0xF4E, sync block 0xBB8 (as before) |

### 13.3 Findings

- **F1 (my schedule error; corrected).** §5.8 gave the ×1.5 ring on B2R as
  1.768 ms / 0.823. That number came from my first scratch build, made
  before the second prewarp clamp line (C11) was fixed. That build also failed
  AL-F. The same scratch binary now prints **1.791 / 0.823**, as the tree
  does, and as the P6 plan's experiment did (§5.5 there). The law's band
  (1.78 ± 0.06) passes either way.
  - The wrong value had reached the bench source as a comment: corrected at
    `alkaline.c` l. 901–902 (B2R 1.791 / 0.823, B-R 1.791 / 0.822, A-R
    1.791 / 0.831), and in §5.8 above.
  - The §12.2 log is right.
- **F2 (my wording; corrected).** §4.4's comment said "an accented note's
  decay is the 303's". As C10 found, the 4066 shorts VR6 only while the
  accent lane is high; the tail after it follows the stretched law (decay 1:
  0.83 s → 1.17 s to 10 %, the model). The wording had reached two places:
  - `AlkalineControl.c` l. 114–117;
  - OPEN_TESTS A11, which expected "accent note decays as before", a device
    test that would have reported a false failure.

  Both are corrected (comment and test text only), and so is §4.4 above.
- **F2b (scratch rounding; no action).** §4.6 quoted the C MEG at decay 0 as
  140.02 ms. That scratch build stored `R_MIN` rounded to 54,069. With the
  tree's 54,068.84 it is **139.29 ms**, one 0.73 ms block shorter, which I
  confirmed by building the scratch bench against the tree's tables. The law
  (2 % of 139.3 ms) passes either way.
- **F3 (low; open for your call).**
  - `fit_spice.py` rebuilds `alk_constants.json` from nothing (`main()`
    l. 155–166), so a lone re-run drops `meg.r_min` / `meg.r_span`.
  - The implementation then behaves three ways:
    - `alk_model.Meg` falls back silently to the stock 303 law
      (`c.get(..., 68e3)`, the §12.4 deviation);
    - `decay_knob_303()` raises a `KeyError`;
    - `gen_alk_tables.py` raises a `KeyError`.
  - The generator protects the firmware, so this is a tooling hazard, not a
    defect. But `validate.py` would compare the stock law against the new
    targets without saying so.
  - Recommended: `fit_spice.py` updates the existing file instead of
    replacing it (load, set its keys, write), and `Meg` reads the two keys
    without a default, as §4.2 specified. Not changed here.
- **Notes, no action:**
  - The 19 new `.cir` files under `tools/spice/tb303/out/` are untracked in a
    folder that already tracks 123 of them, so they are records to add with
    the commit.
  - The `__pycache__` changes are noise from the runs.
  - `elf_loopsrc.py --compare` was rightly skipped, since nothing grew.
  - `MIDI_NRPN_TABLE.md` was edited by hand (no generator exists); rows
    26–27 match the descriptor table.

### 13.4 What P6 changes for P7

`S093_P7_WAVETABLE_SAMPLES_OSC_IMPLEMENTATION.md` is updated for the landed
tree:
- line anchors in the Alkaline files, `make_init_alk.py` and the bench;
- the `wav` baseline, 252.4 per voice and 1,230 per block (both from the P6
  image);
- flash headroom 47,836 B;
- one missed edit: the parameter file's "Not offered" header still lists
  `typ`.

Its measured conclusion (§0 there: typed modes over the Basic ceiling;
decision P7-D1) does not change. Its scratch base differs from the landed P6
only in the `R_MIN` rounding above, which no P7 figure depends on.
