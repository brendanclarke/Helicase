# S099 P1 — Chopped PWM waves and the Advanced BBD chorus: Implementation Schedule

Date: 2026-11-19. Branch `dev-ph7-alkaline` at `62ca53bf` (`756b5b70` plus
the two drafts and the build image). Every file, line and name below was
read in this tree for this schedule. Line numbers are as of `62ca53bf`, and
within one file they are given top to bottom for the unedited file. Apply
each file's edits bottom-up, or re-find the anchor text quoted with each
change.

Plan: `S099_P1_PWM_ADVANCED.md`. Sources:
- `DRAFT_S-R_PWM_WAVE.md` (the S-R draft);
- `DRAFT_ADVANCED_BBD_CHORUS.md` (the chorus draft).

**Status: schedule only. No code is changed by this document.**

---

## 0. Decisions, deviations and rules

### 0.1 Decisions (user, 2026-11-19, on the plan's §7)

| # | Decision |
|---|---|
| Q-1 | P-A → P-B → P-C → P-D → P-E. **P-E (Advanced Alkaline + chorus) is in this programme**, so its spare budget can be measured. P-B and P-C go on separate images (the plan's recommendation; the user did not ask to merge them). |
| Q-3 | **Accepted.** The chop loop goes in ITCM (≈ 308 B of the 424 B free). D-10 (moving the helpers) is not done. Chorus fallback C2 (its loop in ITCM) is therefore not available. |
| Q-7 | **"Sure, but report it up front."** If P-D's probe is over, the implementer may apply C1 (drop the pre-pole) and C3 (half-rate wet, after its image check) without asking, and must report each one at the top of the hand-over. C4 and C5 still need the user. |
| Q-13 | S098 P0 is tested and archived on hardware. No separate image is needed. |

### 0.2 Questions not answered: the plan's recommendations are used

Each item below is a default. The user can change it before the step that
uses it.

| # | Default used | Where it acts |
|---|---|---|
| Q-2 | The drum-family chop (P-F) is deferred. | §8 |
| Q-4 | `REx` on the Colloid and the Alkaline. | P-B, P-C |
| Q-5 | The P-A renders decide k and the gate orientation. Until then the tables use **k = 5, high first**; a change is one constant in the generator plus a regeneration. | A1 |
| Q-6 | The Advanced Colloid passes on **median and p95 ≤ 21,803** cycles per block. Windows next to an edit are reported but do not gate. | D-gate |
| Q-8 | The unit is claimed while `cho` ≠ off; `cho` is neither automatable, morphable nor a modulation target. | D6 |
| Q-9 | Tokens `clx` / `alx`, extensions `.clx` / `.alx`, folders `/Instrument/ColAdv` and `/Instrument/AlkAdv`, Load-menu labels `ColAdv` / `AlkAdv`. | D6, E3 |
| Q-10 | `var` rate 0.1–10 Hz (log). Wet cutoff 9 kHz on the Colloid, tracking the delay. `cmx` 64 = dry and wet at −3 dB each (equal power). **Dry gain in.** | D1, D2 |
| Q-11 | The S089 P2 ceiling counts as met when no Basic type's dearest mode rises and an Advanced type spends only the reserved Advanced share. | all gates |
| Q-12 | Alkaline: 12 dB wet always on; rows `cbw` (tone), `cdv` (BBD drive into a hard clip) and `cwd` (width). The ensemble tap is deferred until an ear check. | P-E |
| Q-14 | Peak-keeping level, `oscPwmDSaw` widths (floor 2.54 %). The level tables are **float**, not Q13 (§0.3 item 3). | A1 |
| Q-15 | Gate row lift 2 on both hosts. | A1 |
| Q-16 | The audition renders go to the session scratchpad, handed over by path. | A2 |

### 0.3 Deviations from the drafts (each is stated where it acts)

1. **The chop tables go in a new generated pair** (`oscChopTables.h/.c`),
   written by the existing generator `tools/plots/gen_pwm_tables.py`.
   - Why: `oscPwmTables.h/.c` stay byte-identical, so the shipped
     tables' checks T1–T9 are untouched.
2. **The Colloid chop loop normalises each oscillator's gate and applies
   each oscillator's level.**
   - What the draft had: the floor offset `c = w × span` with no
     normalisation, so a top row (A = 2.43) played up to 7.7 dB louder. It
     also used one mean level for the stack.
   - Why that fails: `REx`'s level varies 3.7× across the widths (2.21 at
     pos 0, 6.62 at pos 127). A stack-mean level could push a wide
     oscillator to 2.3× full scale into the drive clamp.
   - What is done instead: the gate is normalised with one packed Q14 gain
     per pair. That makes the floor offset exact as `(2p − q) >> 16`, which
     is the cheaper form the draft rejected only because the rows were not
     normalised. The level is applied through the bus multiplies the loop
     already does (packed `lv` / `lp` halves), at the same instruction
     count.
3. **The levels are float tables.** Both hosts consume them per block in
   float (the Alkaline's `sScale`; the Colloid's `inGain` fix-up).
4. **The chop DC needs no stored state.** It is computed per block from
   the start and end widths (`pwPos0` / `pwPos`), which the voice already
   holds. The draft's glide state (8 B) is not needed.
5. **The Colloid chop pass runs in the sync block, not the async.**
   - Why: `col_oscControl()`'s per-oscillator loop and
     `Colloid_calcVoiceAsync()` keep their code. Only the immediate for
     `COL_PWM_WAVE_COUNT` changes, 4 → 6.
   - Cost: the pass corrects `inGain` with one VDIV per block, in chop
     mode only.
6. **The chorus poles use the complement form.**
   - What: `y = x + (y − x)(1 − k)`, with the coefficient `1 − k` in
     SMLAWx's signed halfword.
   - Why: the draft's direct form cannot hold k > 0.5, which is any
     cutoff above 4.87 kHz, and so cannot reach the 8–10 kHz it proposed.
   - Consequence: every chorus cutoff, the tracking included, is floored
     at 5 kHz. The doubler's tracking therefore stops at 5 kHz.
7. **Dry gain:** `out = x·gd + wet·gw`, with gd carried as `gn = gd − 32768`
   so that `cmx` 0 returns the input bit for bit (law CH-MIX0).
8. **The longest delay is 990 samples (22.4 ms), not 1,022.**
   - Why: the `mid` form's pass B reads ring R after pass A has written
     the block's 32 samples into it.
9. **No handoff entries for the chorus unit.**
   - Why: no voice-unit owner writes them today; the Tetrakt does not
     either (checked: only `EffectsManager.c` calls
     `fxbuf_handoffBeginExit()`). The chorus draft §8 was wrong on this
     point.
10. **`instrumentManager_slotRight()` reads a per-slot pointer table**
    (24 B SRAM1). It is cheaper than today's type tests, and the Alkaline
    needs a right block only while its chorus runs.
11. **`instrumentManager_slotType()` keeps returning the registry type.**
    - Engine tests go through a new engine map (`instrumentManager_
      typeEngine()`), so the five registry-lookup sites (l. 3306, 3582,
      4076, 4957, 5052) are unchanged.
    - Switches gain explicit `case` labels, which cost nothing per call
      (jump tables).
12. **The ELF identity gates are this tree's names:**
    - `make -C tools/dsp_test elf_p4_check` (oscillator loops + mixer hot
      loops);
    - a new `elf_s099_check` for the Alkaline, Colloid and Tetrakt loops.
    The drafts called them `elf_osc_check` / `elf86_mixer_check`; no such
    targets exist.
13. **The ring's guard refresh is corrected.**
    - What the spike did: refreshed `ring[1024] = ring[0]` when
      `(w & 1023) < 32` after a block. w advances by whole blocks, so that
      is the block that wrote indices 992..1023. The guard took the
      *previous* cycle's `ring[0]`, and the block that then wrote the new
      `ring[0]` did not refresh it.
    - What this schedule does: refresh after the block whose last write
      `(w − 1) & 1023` is in 0..31, which is the block that wrote index 0.
      The tap reaches index 1,023 only when the write is d samples later
      (≥ 65), so it always reads the new sample.
    - Law CH-GUARD (exact modulo reference) proves it, and its break (no
      refresh at all) must fail.

### 0.4 Binding rules applied (MEMORY.md)

- **No per-sample cost rise on any Basic type's dearest mode** (S089 P2,
  S094 P2 §21, S093 per-block rule).
  - Two dispatch tests are added to existing paths, each ≤ 2 instructions
    per block (≈ +0.06 per sample). Each is reported and measured:
    - the Colloid sync block's chop test, on the pwm Saw/Rec path;
    - the Alkaline `alk_renderPwm()` chop test, on the pwm Saw/Sqr/Tri
      path.
  - Neither path is its voice's dearest mode on the Alkaline. On the
    Colloid, pwm Saw/Rec 6 is the dearest mode, so the P-C device gate
    compares it before and after (gates C-G4 and C-G5).
- **New code in new loop copies; no existing loop is edited.**
- **ITCM growth only in a new subsection after the last one**
  (`.itcm.s099p1` after `.itcm.s096p3`).
- **RAM sign-off:** every allocation is listed in §10.
- **No backward compatibility, no migration:** new types are appended; no
  stored value changes meaning.
- **This schedule edits nothing in `MEMORY.md` or `knowledge_files/`.**
  §11 lists the statements it makes out of date.

### 0.5 How each change is written

Each change gives:
- **File:line** (in the unedited tree);
- **ADD / MODIFY / REMOVE**;
- the code, with its comment block in the project's What / Why / Inputs /
  Outputs / Accessors / Affiliates form. The comment block is meant to go
  into the source as written;
- a short note outside the code only where a decision needs it.

---

## 1. Phase map and hardware rounds

| Phase | Content | Image | Device check |
|---|---|---|---|
| 0 | Baselines (no code change) | — | — |
| P-A | Chop tables in the generator; audition renders | none (tables unreferenced) | listening only |
| P-B | Alkaline `R-S` / `REx`: fused ladder mode 5 | image B | probe + ear |
| P-C | Colloid `R-S` / `REx`: form T, ITCM chop pair | image C | probe + ear (form T low notes) |
| P-D | `clx`: the Advanced Colloid + the BBD chorus | image D | probe (Q-6), reseat, Scene carry-over, ear |
| P-E | `alx`: the Advanced Alkaline + the chorus (mid form, 12 dB, drive, width) | image E | two-Advanced Scene probe, ear |
| P-F | Drum-family chop | deferred (Q-2) | — |

Each phase is ready for the device only after these pass, all with the
phase's gates:
- `make all && make img`;
- `make -C tools/dsp_test all`;
- `elf_p4_check`;
- `elf_s099_check`.

---

## 2. Phase 0 — baselines

No code change. Records go in the tree, not `build/` (user, S093).

**0.1** Build the unedited tree (`make clean && make all && make img`). Then:
- record `python3 tools/diagnostic_tools/link_budget.py arm-none-eabi-nm
  build/lxr02.elf` into `tools/dsp_test/baselines/s099p1/link_budget_pre.txt`;
- record `arm-none-eabi-size -A build/lxr02.elf | grep -E 'itcm|text|data|bss'`
  into the same folder.

**0.2** `make -C tools/dsp_test elf_p4_snapshot P4BASE=baselines/s099p1/p4base`.
This writes the oscillator and mixer loop baseline every later phase is
compared with.

**0.3** Add the `elf_s099_snapshot` / `elf_s099_check` targets (§9.1), then
run `make -C tools/dsp_test elf_s099_snapshot`. This writes
`baselines/s099p1/pre/engines.txt`.

**0.4** Record the host counts:
- `python3 tools/dsp_test/colloid_cpu.py --elf build/lxr02.elf --count 10
  > baselines/s099p1/colloid_cpu_pre.txt`;
- `python3 tools/alkaline/control_cost.py build/lxr02.elf
  > baselines/s099p1/alk_cost_pre.txt`.

**0.5** Record the sizes with a scratch translation unit (not in the
tree): `sizeof(ColloidVoice)` (expected 1,212 B, the drafts' measurement)
and `sizeof(AlkalineVoice)` (expected 564 B). Write the two numbers into
`baselines/s099p1/sizes_pre.txt`. §10 uses them.

**0.6** Copy the spike sources and counts named by both drafts from the
session scratchpad into `tools/dsp_test/baselines/s099p1/spike/`, as the
drafts require. If the scratchpad no longer holds them, write a README
there saying so.

## 3. Phase P-A — chop tables and audition renders

### A1. `tools/plots/gen_pwm_tables.py` — the chop tables

The script keeps writing `oscPwmTables.h/.c` byte for byte as today, and
also writes `Core/DSPAudio/oscChopTables.h/.c`. `--check` compares all four
files (deviation §0.3 item 1).

**A1.1 l. 2–56 (module docstring) — MODIFY: append before the closing `"""`:**

```python
S099 P1:    the chopped waves R-S and REx (DRAFT_S-R_PWM_WAVE.md §4, §6.4)
            go in their own pair, Core/DSPAudio/oscChopTables.h/.c, so the
            tables above (and T1-T9) stay byte-identical:
              oscChopCurveRs[1024], oscChopCurveRex[1024]
                                 int16 round(32767 c(x)), x = i/1024 - 1/8:
                                 the 1x ramp wrapped at 7/8 of the cycle
                                 (index (ph + 2^29) >> 22), so the wrap sits
                                 inside the last chopped section for every
                                 width <= 1/2. R-S: c(x) = x. REx: c(x) =
                                 s(x) / s(7/8), s(x) = (e^(kx) - 1) /
                                 (e^k - 1), k = CHOP_REX_K (D-11, by ear).
              oscChopGainQ14[11] int16 round(16384 x 65536 / span_r): the
                                 gate's normalisation per sawTable row r
                                 (span_r = 2 A_r 32767, A_r the row's naive
                                 saw amplitude from its middle slope).
              oscChopSpanF[11], oscChopInvSpanF[11]
                                 float span_r and 1 / span_r (the
                                 Alkaline's float form; no VDIV).
              oscChopDcRsQ15[128], oscChopDcRexQ15[128]
                                 int16 round(32767 dc(w)), w = oscPwmDSaw[k]
                                 / 1024: the wave's mean, the integral of c
                                 over the two passed segments [0, w/2) and
                                 [1/2, 1/2 + w/2).
              oscChopLevelRs[128], oscChopLevelRex[128]
                                 float 1 / P(w), P = c((1 + w)/2) - dc(w):
                                 the peak-keeping level (D-5, Q-14).
              oscChopRelRsQ15[128], oscChopRelRexQ15[128]
                                 int16 round(32767 level / Lref), Lref = the
                                 largest level of the wave (written as
                                 OSC_CHOP_LREF_RS / _REX in the header): the
                                 Colloid's per-oscillator level.
            Checks T10-T15 (below); T8 covers the new pair too.
```

**A1.2 l. 57–69 (imports and constants) — MODIFY: add `import re` after
`import math`, and after l. 69 ADD:**

```python
# --- S099 P1: the chopped waves (oscChopTables.h/.c) ---
CHOP_H_PATH = "Core/DSPAudio/oscChopTables.h"
CHOP_C_PATH = "Core/DSPAudio/oscChopTables.c"
WAVETABLE_C = "Core/DSPAudio/wavetable.c"
# D-11 (user, 2026-11-19): one fixed REx curve, chosen by ear from 4..6 in
# the P-A audition (S099_P1_IMPLEMENTATION.md A2); 5 until then
CHOP_REX_K = 5.0
CHOP_POINTS = 1024
CHOP_WRAP_INDEX = 128          # (ph + 2^29) >> 22 puts x = 0 at index 128
CHOP_ROWS = 11
```

**A1.3 after `split()` / `lift()` (l. 84–95) — ADD the chop functions:**

```python
def saw_rows():
    """sawTable[11][1024] from wavetable.c (comments stripped), as
    gen_stock_tables.py read_table() reads it."""
    text = open(WAVETABLE_C).read()
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    text = re.sub(r"//[^\n]*", "", text)
    m = re.search(r"sawTable\[11\]\[1024\]\s*=\s*\{(.*?)\};", text, re.S)
    v = [int(x) for x in re.findall(r"-?\d+", m.group(1))]
    if len(v) != CHOP_ROWS * CHOP_POINTS:
        sys.exit("gen_pwm_tables.py: sawTable has %d values" % len(v))
    return [v[r * CHOP_POINTS:(r + 1) * CHOP_POINTS] for r in range(CHOP_ROWS)]


def row_amplitude(row):
    """A_r: minus half the least-squares slope of the row's middle half
    (points 256..767) in full-scale units per cycle (S-R draft §4.1,
    Appendix A). About 1 for rows 0-5, up to 2.43 for row 10."""
    xs = [i / 1024.0 for i in range(256, 768)]
    ys = [row[i] / 32767.0 for i in range(256, 768)]
    mx = sum(xs) / len(xs)
    my = sum(ys) / len(ys)
    slope = (sum((x - mx) * (y - my) for x, y in zip(xs, ys)) /
             sum((x - mx) ** 2 for x in xs))
    return -slope / 2.0


def rex_s(x, k=None):
    k = CHOP_REX_K if k is None else k
    return (math.exp(k * x) - 1.0) / (math.exp(k) - 1.0)


def rex_S(x, k=None):
    """The integral of rex_s from 0 to x."""
    k = CHOP_REX_K if k is None else k
    return ((math.exp(k * x) - 1.0) / k - x) / (math.exp(k) - 1.0)


def curve_fns(wave):
    """(c, C): the wave's curve c(x) and its integral C(x) from 0, in the
    table's units (c(7/8) = 1 for REx; c(x) = x for R-S)."""
    if wave == "rs":
        return (lambda x: x), (lambda x: x * x / 2.0)
    top = rex_s(7.0 / 8.0)
    return (lambda x: rex_s(x) / top), (lambda x: rex_S(x) / top)


def chop_curve(wave):
    c, _ = curve_fns(wave)
    return [int(round(32767.0 * c(i / 1024.0 - 1.0 / 8.0))) for i in range(CHOP_POINTS)]


def chop_dc_level(wave, d_saw_v):
    """dc(w) and level(w) = 1 / P(w) at every pos (w = d / 1024)."""
    c, C = curve_fns(wave)
    dcs, levels = [], []
    for d in d_saw_v:
        w = d / 1024.0
        dc = (C(w / 2.0) - C(0.0)) + (C(0.5 + w / 2.0) - C(0.5))
        peak = c(0.5 + w / 2.0) - dc
        dcs.append(dc)
        levels.append(1.0 / peak)
    return dcs, levels


def chop_tables(d_saw_v):
    rows = saw_rows()
    amp = [row_amplitude(r) for r in rows]
    span = [2.0 * a * 32767.0 for a in amp]
    gain = [int(round(16384.0 * 65536.0 / s)) for s in span]
    out = {"rows": rows, "span": span, "gain": gain}
    for wave in ("rs", "rex"):
        dcs, lv = chop_dc_level(wave, d_saw_v)
        lref = max(lv)
        out[wave] = {
            "curve": chop_curve(wave),
            "dc_q15": [int(round(32767.0 * x)) for x in dcs],
            "dc": dcs,
            "level": lv,
            "lref": lref,
            "rel": [int(round(32767.0 * x / lref)) for x in lv],
        }
    return out


def chop_checks(t, d_saw_v):
    """T10-T15 (S099 P1). Each returns False with a printed reason."""
    ok = True
    def bad(msg):
        print("chop tables:", msg)
        return False
    for wave in ("rs", "rex"):
        cv = t[wave]["curve"]
        # T10: the curve is 0 at x = 0 (index 128), strictly increasing,
        # inside int16, and below full scale over the read range
        if cv[CHOP_WRAP_INDEX] != 0:
            ok = bad("T10 %s curve(0) = %d" % (wave, cv[CHOP_WRAP_INDEX]))
        if any(b <= a for a, b in zip(cv, cv[1:])):
            ok = bad("T10 %s curve not strictly increasing" % wave)
        if max(cv) >= 32767 or min(cv) < -32768:
            ok = bad("T10 %s curve out of range" % wave)
        # T13: the positive peak after the DC dominates (the level keeps the
        # peak, not the negative excursion -dc), and level x peak = 1
        for k, (dc, lv) in enumerate(zip(t[wave]["dc"], t[wave]["level"])):
            if not (1.0 / lv > dc > 0.0):
                ok = bad("T13 %s pos %d: peak %.4f dc %.4f" % (wave, k, 1.0 / lv, dc))
        # T14: the Colloid's relative levels fit Q15 and are >= 1
        rel = t[wave]["rel"]
        top = t[wave]["level"].index(t[wave]["lref"])
        if max(rel) > 32767 or min(rel) < 1 or rel[top] != 32767:
            ok = bad("T14 %s rel range" % wave)
    # T11: every gate gain fits a signed halfword and is positive
    if any(not (0 < g <= 32767) for g in t["gain"]):
        ok = bad("T11 gain %s" % t["gain"])
    # T12: the Colloid loop's range (col_chopPairAdd()): for every row and
    # width, |(D << 2) x g14 >> 16| + 32,768 < 2^17 and its product with any
    # curve value, >> 16, inside int16 x 1.1 (the Gibbs overshoot)
    for r, row in enumerate(t["rows"]):
        dmax = max(row) - min(row)
        gmax = ((dmax << 2) * t["gain"][r] >> 16) + 32768
        if gmax >= (1 << 17):
            ok = bad("T12 row %d gate %d" % (r, gmax))
        if (gmax * 32767) >> 16 > int(32767 * 1.1):
            ok = bad("T12 row %d product %d" % (r, (gmax * 32767) >> 16))
    # T15: pos 0 is the Alpha Juno Saw 2 (w = 1/2): R-S dc = 3/16, level
    # 1 / 0.5625 (S-R draft §2), to the table's rounding
    if abs(t["rs"]["dc"][0] - 0.1875) > 1e-9 or abs(t["rs"]["level"][0] - 1.0 / 0.5625) > 1e-9:
        ok = bad("T15 R-S pos 0 is not Saw 2")
    return ok
```

Note on T14: for both waves the largest level is at pos 127, because the
narrowest gate has the smallest peak. The check does not assume this: it
finds the largest level and requires its rel to be exactly 32767.

**A1.4 after `render_c()` (l. 247) — ADD the two renderers:**

```python
def render_chop_h(t):
    return "\n".join([
        "/* Generated by tools/plots/gen_pwm_tables.py (S099 P1). Do not edit. */",
        "#ifndef OSC_CHOP_TABLES_H_",
        "#define OSC_CHOP_TABLES_H_",
        "",
        "#include <stdint.h>",
        "",
        "/*",
        " * The chopped pwm waves R-S and REx (S099 P1; DRAFT_S-R_PWM_WAVE.md §4,",
        " * §6.4), flash.",
        " *",
        " * What:       y = c(ramp_w(phi)) x gate(phi) - dc: the 1x ramp, wrapped",
        " *             at 7/8 of the cycle and read through the wave's curve",
        " *             table, times a band-limited pulse an octave up (the",
        " *             shipped Rec two-read at twice the phase, on the sawTable",
        " *             row OSC_CHOP_LIFT octaves above the note's), minus the",
        " *             wave's mean. Tables by pos (positionApplied 0..127; the",
        " *             width w = oscPwmDSaw[pos] / 1,024 of each gate cycle):",
        " *               oscChopCurveRs / Rex: the curve on x = i / 1,024 - 1/8",
        " *                 (index (ph + 2^29) >> 22); R-S linear, REx",
        " *                 (e^(kx) - 1) / (e^k - 1) scaled to 1 at x = 7/8;",
        " *               oscChopGainQ14[row]: the gate's normalisation,",
        " *                 16384 x 65536 / span (D x g14 / 16384 spans 65,536);",
        " *               oscChopSpanF / InvSpanF[row]: span and 1 / span (float);",
        " *               oscChopDc*Q15[pos]: 32767 x the mean;",
        " *               oscChopLevel*[pos]: 1 / the peak after the mean;",
        " *               oscChopRel*Q15[pos]: 32767 x level / OSC_CHOP_LREF_*.",
        " * Why:        the Alpha Juno's PWM saw is a ramp chopped to its floor by",
        " *             a pulse at +1 octave; no sum of saw reads makes its kinks,",
        " *             a product does. Per-block lookups, no division.",
        " * Accessors:  AlkalineVoice.c alk_renderChop(); ColloidVoice.c",
        " *             col_chopBlock(); ColloidOsc.c col_chopPairAdd().",
        " * Affiliates: oscPwmTables.h (oscPwmDSaw, the widths);",
        " *             tools/plots/gen_pwm_tables.py (the law, T10-T15).",
        " */",
        "#define OSC_CHOP_LIFT 2u          /* Q-15: the gate row is the note's + 2 */",
        "#define OSC_CHOP_REX_K %.6ff     /* D-11: the REx curve's k */" % CHOP_REX_K,
        "#define OSC_CHOP_LREF_RS %.9ef  /* the largest R-S level */" % t["rs"]["lref"],
        "#define OSC_CHOP_LREF_REX %.9ef /* the largest REx level */" % t["rex"]["lref"],
        "extern const int16_t oscChopCurveRs[1024];",
        "extern const int16_t oscChopCurveRex[1024];",
        "extern const int16_t oscChopGainQ14[11];",
        "extern const float   oscChopSpanF[11];",
        "extern const float   oscChopInvSpanF[11];",
        "extern const int16_t oscChopDcRsQ15[128];",
        "extern const int16_t oscChopDcRexQ15[128];",
        "extern const float   oscChopLevelRs[128];",
        "extern const float   oscChopLevelRex[128];",
        "extern const int16_t oscChopRelRsQ15[128];",
        "extern const int16_t oscChopRelRexQ15[128];",
        "",
        "#endif /* OSC_CHOP_TABLES_H_ */",
        "",
    ])


def render_chop_c(t):
    f = lambda vals: ["    " + ", ".join("%.9ef" % v for v in vals[i:i + 4]) + ","
                      for i in range(0, len(vals), 4)]
    parts = ["/* Generated by tools/plots/gen_pwm_tables.py (S099 P1). Do not edit. */",
             '#include "oscChopTables.h"', ""]
    for name, wave in (("oscChopCurveRs", "rs"), ("oscChopCurveRex", "rex")):
        parts += ["const int16_t %s[1024] = {" % name]
        parts += rows(t[wave]["curve"], "%6d", 8) + ["};", ""]
    parts += ["const int16_t oscChopGainQ14[11] = {"] + rows(t["gain"], "%6d", 11) + ["};", ""]
    parts += ["const float oscChopSpanF[11] = {"] + f(t["span"]) + ["};", ""]
    parts += ["const float oscChopInvSpanF[11] = {"] + f([1.0 / s for s in t["span"]]) + ["};", ""]
    for wave, suffix in (("rs", "Rs"), ("rex", "Rex")):
        parts += ["const int16_t oscChopDc%sQ15[128] = {" % suffix] + rows(t[wave]["dc_q15"], "%6d", 8) + ["};", ""]
        parts += ["const float oscChopLevel%s[128] = {" % suffix] + f(t[wave]["level"]) + ["};", ""]
        parts += ["const int16_t oscChopRel%sQ15[128] = {" % suffix] + rows(t[wave]["rel"], "%6d", 8) + ["};", ""]
    return "\n".join(parts)
```

**A1.5 `main()` l. 250–271 — MODIFY:**

```python
def main():
    d_rec_v, d_saw_v = d_rec(), d_saw()
    l_rec, l_saw = level(d_rec_v), level(d_saw_v)
    tri_ws, tri_w, tri_ra, tri_rb = split(TRI_W_END)
    tri_lift = lift(tri_ws)
    sin_ws, sin_w, sin_ra, sin_rb = split(SIN_W_END)
    ok = checks(d_rec_v, d_saw_v, l_rec, l_saw, tri_ws, tri_w, tri_ra, tri_rb,
                tri_lift, sin_ws, sin_w, sin_ra, sin_rb)
    # S099 P1: the chop tables, on the same widths (oscPwmDSaw)
    chop = chop_tables(d_saw_v)
    ok = chop_checks(chop, d_saw_v) and ok
    h = render_h()
    c = render_c(d_rec_v, d_saw_v, l_rec, l_saw, tri_w, tri_ra, tri_rb, tri_lift,
                 sin_w, sin_ra, sin_rb)
    ch, cc = render_chop_h(chop), render_chop_c(chop)
    if "--check" in sys.argv:
        ok = (ok and open(H_PATH).read() == h and open(C_PATH).read() == c and
              open(CHOP_H_PATH).read() == ch and open(CHOP_C_PATH).read() == cc)
        print("pwm tables", "OK" if ok else "FAILED")
        return 0 if ok else 1
    if not ok:
        print("pwm tables: T1-T7/T9 or T10-T15 failed")
        return 1
    for path, text in ((H_PATH, h), (C_PATH, c), (CHOP_H_PATH, ch), (CHOP_C_PATH, cc)):
        open(path, "w").write(text)
    print("wrote", H_PATH, C_PATH, CHOP_H_PATH, CHOP_C_PATH)
    return 0
```

**A1.6** Run `python3 tools/plots/gen_pwm_tables.py`. Confirm `git diff
--stat` shows only the two new files under `Core/DSPAudio/`; the existing
`oscPwmTables.*` must not change. Commit the generated pair with the
script.

**A1.7 `Makefile` l. 196 (`Core/DSPAudio/wavetable.c`, the last line of
`DSP_SRCS`) — MODIFY / ADD:**

```make
  Core/DSPAudio/wavetable.c \
  Core/DSPAudio/oscChopTables.c
```

After the Colloid rules (l. 336–338) — ADD:

```make
# S099 P1: the chopped pwm waves' tables (generated, data only). -fno-lto for
# the S095 P2 reason: outside the LTO unit a new object cannot shift LTO's
# partitioning of the other engines' loops (gate: elf_p4_check). The
# Alkaline (in the LTO unit) and the Colloid (-fno-lto) both read them as
# ordinary externals.
$(BUILD)/Core/DSPAudio/oscChopTables.o: Core/DSPAudio/oscChopTables.c | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS_DSP) $< -o $@ -fno-lto
```

This rule must come **before** the generic `$(BUILD)/Core/DSPAudio/%.o`
rule (l. 274). Make prefers the more specific explicit target in any
case, but placing it after the DSP pattern rule keeps the file's reading
order consistent. With nothing referencing the tables yet, `--gc-sections`
drops them and the image stays byte-identical. Gate A-G2 checks this.

**A-gates:**
- **A-G1:** `make -C tools/dsp_test pwm_tables` prints OK.
- **A-G2:** `make all && make img`; then `cmp` `build/lxr02.bin` against
  the Phase 0 image. Equal is expected, since `--gc-sections` drops the
  unreferenced tables.
  - If the images differ, the new object has moved the link layout.
    `elf_p4_check` and `elf_s099_check` then decide: every loop must be
    unchanged.
  - Record the `cmp` result either way.

### A2. `tools/plots/render_chop_audition.py` — NEW (offline audition, D-6, D-11, Q-5)

```python
#!/usr/bin/env python3
"""Render the S099 P1 chop audition files (offline; no firmware change).

What:       writes 16-bit mono 44,108 Hz WAV files into --out (default: the
            path given; nothing is written into the tree):
              <wave>_<f>Hz_<form>.wav for wave in rs, rexk4, rexk5, rexk6,
              rec (the shipped Rec, the shipped D form) and rs-lowfirst (the
              other gate orientation, D-4); f in 55, 110, 440, 1760; form in
              T (both gate points truncated, the Colloid's form T) and I (both
              interpolated, the reference). Each file is 1 s at pos 0, 24, 48
              and 96, then a 4 s pos sweep 0 -> 127 -> 0 at the shipped slew
              (2 index steps per 32-sample block).
            The gate is the shipped two-read on the sawTable row lifted by
            OSC_CHOP_LIFT, read at twice the phase (osc_pwmReadD(), sign -1),
            floored by w; the curve is the table law of gen_pwm_tables.py
            (rex_s with k 4/5/6), read truncated (form T) or interpolated
            (form I); the DC is removed and the peak kept (the level law).
Why:        D-11 (k by ear), D-4 (orientation), and the form T low-note
            question (S-R draft §6.3) can be heard before any firmware work.
Inputs:     --out DIR (required).
Outputs:    the WAV files and a one-line summary each (RMS, peak).
Accessors:  python3 tools/plots/render_chop_audition.py --out <scratchpad>.
Affiliates: gen_pwm_tables.py (saw_rows(), curve_fns(), d_saw()).
"""
import argparse
import math
import os
import struct
import sys
import wave

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_pwm_tables as G   # noqa: E402

FS = 44108
BLOCK = 32


def octave(f):
    return min(10, max(0, int(math.floor(math.log2(f / 16.3516)))))


def read_d(row, p, d, interp):
    ia = (p >> 22) & 1023
    ib = (ia - d) & 1023
    if not interp:
        return row[ia] - row[ib]
    fr = ((p >> 7) & 0x7FFF) / 32768.0
    a = row[ia] + fr * (row[(ia + 1) & 1023] - row[ia])
    b = row[ib] + fr * (row[(ib + 1) & 1023] - row[ib])
    return a - b


def render(wave_name, f, form, k):
    rows = G.saw_rows()
    d_saw = G.d_saw()
    if wave_name.startswith("rex"):
        G.CHOP_REX_K = k
    curve_wave = "rs" if wave_name.startswith("rs") else "rex"
    curve = G.chop_curve(curve_wave)
    dcs, levels = G.chop_dc_level(curve_wave, d_saw)
    r = min(octave(f) + 2, 10)
    row = rows[r]
    span = 2.0 * G.row_amplitude(row) * 32767.0
    inc = int(round(f / FS * 2 ** 32)) & 0xFFFFFFFF
    plan = [0] * FS + [24] * FS + [48] * FS + [96] * FS
    sweep = []
    pos = 0
    for _ in range(4 * FS // BLOCK):
        target = 127 if len(sweep) < 2 * FS else 0
        pos += max(-2, min(2, target - pos))
        sweep += [pos] * BLOCK
    plan += sweep
    out = np.zeros(len(plan))
    p = 0
    interp = (form == "I")
    lowfirst = wave_name.endswith("lowfirst")
    for n, k_pos in enumerate(plan):
        d = d_saw[k_pos]
        w = d / 1024.0
        p2 = (p << 1) & 0xFFFFFFFF
        if lowfirst:
            # D-4's other orientation (R-S only): the gate passes the upper w
            # of each half-cycle (B read at +w: the shipped pulse shifted by
            # its own width), so the open parts are [1/2 - w/2, 1/2) and
            # [1 - w/2, 1). The ramp's wrap moves into the FIRST chopped
            # section, at 1/8 (inside [0, 1/2 - w/2) for every w <= 1/2): x =
            # phi, or phi + 1 below 1/8. The curve is evaluated exactly (the
            # firmware would need a table on [1/8, 9/8), A-G3), and the mean
            # and peak are this orientation's own.
            p2 = (p2 + (d << 22)) & 0xFFFFFFFF
            g = read_d(row, p2, d, interp) / span + w
            phi = p / 4294967296.0
            x = phi + 1.0 if phi < 0.125 else phi
            dc = ((0.25 - (0.5 - w / 2.0) ** 2) + (1.0 - (1.0 - w / 2.0) ** 2)) / 2.0
            out[n] = (x * g - dc) / (1.0 - dc)
            p = (p + inc) & 0xFFFFFFFF
            continue
        g = read_d(row, p2, d, interp) / span + w
        iv = ((p + 0x20000000) & 0xFFFFFFFF) >> 22
        if interp:
            fv = ((p + 0x20000000) & 0x3FFFFF) / 4194304.0
            v = (curve[iv] + fv * (curve[(iv + 1) & 1023] - curve[iv])) / 32767.0
        else:
            v = curve[iv] / 32767.0
        out[n] = (v * g - dcs[k_pos]) * levels[k_pos]
        p = (p + inc) & 0xFFFFFFFF
    return out


def render_rec(f):
    rows = G.saw_rows()
    d_rec = G.d_rec()
    row = rows[octave(f)]
    inc = int(round(f / FS * 2 ** 32)) & 0xFFFFFFFF
    out = np.zeros(8 * FS)
    p = 0
    for n in range(len(out)):
        pos = [0, 24, 48, 96][min(3, n // FS)] if n < 4 * FS else 64
        out[n] = read_d(row, p, d_rec[pos], True) / 65534.0
        p = (p + inc) & 0xFFFFFFFF
    return out


def write(path, x):
    x = np.clip(x * 0.5, -1.0, 1.0)
    with wave.open(path, "wb") as fh:
        fh.setnchannels(1)
        fh.setsampwidth(2)
        fh.setframerate(FS)
        fh.writeframes(b"".join(struct.pack("<h", int(round(v * 32767))) for v in x))
    print("%s rms %.3f peak %.3f" % (os.path.basename(path), float(np.sqrt(np.mean(x * x))),
                                     float(np.max(np.abs(x)))))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    for f in (55, 110, 440, 1760):
        write(os.path.join(a.out, "rec_%dHz.wav" % f), render_rec(f))
        for form in ("T", "I"):
            write(os.path.join(a.out, "rs_%dHz_%s.wav" % (f, form)), render("rs", f, form, 0))
            write(os.path.join(a.out, "rs-lowfirst_%dHz_%s.wav" % (f, form)),
                  render("rs-lowfirst", f, form, 0))
            for k in (4, 5, 6):
                write(os.path.join(a.out, "rexk%d_%dHz_%s.wav" % (k, f, form)),
                      render("rexk%d" % k, f, form, float(k)))


if __name__ == "__main__":
    main()
```

The "low first" render shifts the gate by its own width, so the open part
is the upper `w` of each half-cycle. That is the other orientation the S-R
draft §1.2 names (B read at `ph + w`). That orientation also moves the
ramp's wrap: with the open parts at the ends of the half-cycles, 7/8 falls
inside an open part for w > 1/4. So the render wraps at 1/8, inside the
first chopped section, and evaluates the curve exactly.

**A-gate A-G3:**
- The user listens and gives three answers:
  - **k** (4, 5 or 6);
  - the **orientation** (high first or low first);
  - whether **form T at 55 / 110 Hz** is acceptable.
- If k ≠ 5: set `CHOP_REX_K`, re-run the generator, and re-run A-G1 and
  A-G2.
- If low first is chosen, three things change together. The schedule's
  code is written for high first.
  1. **The generator:** the curve domain becomes `x = i / 1,024 + 1/8`,
     scaled to `c(9/8) = 1`, and the wrap index moves to 896. The dc and
     peak integrals move to the segments `[1/2 − w/2, 1/2)` and
     `[1 − w/2, 1)`. T10's index check moves with it.
  2. **The curve index** in mode 5 (B2.3) and in `col_chopPairAdd()` (C2.3)
     becomes `(ph − 0x20000000u) >> 22`.
  3. **B's offset changes sign:**
     - mode 5: `ib = (itg + pwD) & 0x3FF`;
     - the Colloid: `q = 2p + (width0 << 22)`, and the width term of `j`
       changes sign.
  The host laws AL-CH and CO-CHP restate the same three changes.
- If form T is rejected: P-C stops (plan R-3); P-B can still go ahead.


---

## 4. Phase P-B — Alkaline `R-S` / `REx` (fused ladder mode 5)

### B1. `Core/DSP/Instruments/Alkaline/AlkalineVoice.h`

**B1.1 l. 58–79 (the `osc.waveform` block and its defines) — MODIFY: replace with:**

```c
/*
 * osc.waveform: the Alkaline wave lists (S093 P8 row osc1_wave; S094 P4;
 * S099 P1 the chopped waves under pwm).
 *
 * What:       under typ wav: 0 Saw, 1 Sqr (the 303 square table), 2 Tri (the
 *             stock triangle rows): ALK_WAVE_COUNT entries. Under typ pwm:
 *             0 Saw (two-saw sum), 1 Sqr (the band-limited pulse: the stock
 *             Rec's pwm form), 2 Tri (the warp form of the triangle), 3 R-S
 *             (the Alpha Juno chopped saw), 4 REx (the same chop on a rising
 *             exponential): ALK_PWM_WAVE_COUNT entries. Under typ wtb the
 *             same byte is the wavetable index.
 * Why:        S094 P4 (user): Tri only under wav and pwm, no Rec (Sqr is the
 *             pulse), no Sin. S099 P1 (user, 2026-11-19, D-1): R-S and REx
 *             are pwm forms only (pos 0 of R-S is the fixed Saw 2), so the
 *             two lists now have their own sizes. A typ switch from pwm with
 *             R-S or REx to wav clamps to Tri (Alkaline_setOscType()); no
 *             stored value changes meaning (a pwm byte of 3 or 4 clamped to
 *             Tri before, and no such byte could be stored).
 * Accessors:  alk_renderWav(), alk_renderPwm(), alk_renderChop(),
 *             Alkaline_setOscType(), InstrumentManager.c
 *             instrumentManager_waveDomainMax(), MenuText.h alkWaveNames
 *             (ALK_WAVE_COUNT names) and alkPwmWaveNames (ALK_PWM_WAVE_COUNT).
 */
#define ALK_WAVE_SAW          0u
#define ALK_WAVE_SQUARE       1u
#define ALK_WAVE_TRI          2u
#define ALK_WAVE_COUNT        3u   /* Saw Sqr Tri: the wav list */
#define ALK_WAVE_RS           3u   /* S099 P1: pwm only, the chopped saw */
#define ALK_WAVE_REX          4u   /* S099 P1: pwm only, the chopped exponential */
#define ALK_PWM_WAVE_COUNT    5u   /* Saw Sqr Tri R-S REx: the pwm list */
```

**B1.2 l. 85–86 (the typ comment's "2 pwm (Saw two-saw sum, Sqr pulse,
S094 P4 + Tri; ...)") — MODIFY:** change it to "2 pwm (Saw two-saw sum,
Sqr pulse, Tri warp, and since S099 P1 R-S and REx, the chopped waves;
pos = width index, 0 = 50 %)".

**B1.3 l. 264–266 (`Alkaline_setOscType()` header, "clamps osc.waveform to
the new type's list (wav and pwm: Saw/Sqr; ...)") — MODIFY:** change it to
"clamps osc.waveform to the new type's list (wav: Saw Sqr Tri; pwm: Saw
Sqr Tri R-S REx; wtb: the installed tables)".

### B2. `Core/DSPAudio/DiodeLadder.c`

**B2.1 after l. 115 (`#endif` of `ALK_PWM_READ_FORM`) — ADD:**

```c
/*
 * The Alkaline chop read form (S099 P1, fused mode 5).
 *
 * What:       how fused mode 5 reads its three points (the gate's A and B,
 *             the curve):
 *               2  all three truncated (the shipped form, mode 3's form 2);
 *               0  all three interpolated (the reference for bench law AL-CH;
 *                  never shipped).
 * Why:        inside the fused loop a truncated read is the cheaper form, and
 *             the ladder's low-pass hides the gate edges' quantisation to
 *             1/2,048 of a cycle (mode 3 accepted the same; S-R draft §4.3).
 *             AL-CH prints the RMS error against form 0, as AL-PW does for
 *             mode 3.
 * Inputs:     -DALK_CHOP_READ_FORM (bench and measurement builds).
 * Accessors:  dl_renderB2Body() fused mode 5.
 */
#ifndef ALK_CHOP_READ_FORM
#define ALK_CHOP_READ_FORM 2
#endif
```

**B2.2 l. 315–330 (the `fused` comment in `dl_renderB2Body()`) — MODIFY:**
after the mode 4 lines (l. 325–327) add:

```c
	 *   5  S099 P1: pwm R-S / REx, the chop: the gate (mode 3's
	 *      difference on the lifted saw row, read at twice the phase,
	 *      floored by chC) times the 1x ramp read through the wave's curve
	 *      table (rowB) at (ph + 2^29) >> 22, minus chDc
	 *      (DiodeLadder_renderB2OscChop()).
```

and replace l. 328–330 ("rowB, ff0 and ffStep are read only under 2, ...")
with:

```c
	 * rowB, ff0 and ffStep are read only under 2, and under 5 (rowB the
	 * curve table, ff0 chC, ffStep chDc: the chop reuses the three so the
	 * body's signature, and every other wrapper, is unchanged); pwD only
	 * under 3 and 5; pwSign only under 3; wPh, wRa and wRb only under 4; the
	 * other wrappers pass 0 / 0.0f and the compiler drops them with the
	 * branch. */
```

**B2.3 l. 470–471 (between mode 4's `s = (a + fr * (b - a)) * sScale;` /
`}` and `} else if (fused) {`) — ADD a branch:**

```c
		} else if (fused == 5) {
			/*
			 * S099 P1: the Alkaline chopped waves R-S and REx.
			 *
			 * What:       the gate g = row[2 phi] - row[2 phi - pwD] + chC:
			 *             mode 3's difference (high first, the shipped Rec
			 *             orientation) on the caller's sawTable row
			 *             OSC_CHOP_LIFT octaves above the note's, read at
			 *             twice the phase (point (ph >> 21) & 1,023: a pulse
			 *             an octave up), floored at 0 by chC = w x span (w =
			 *             pwD / 1,024 of each gate cycle passes); the ramp
			 *             v = rowB[(ph + 2^29) >> 22], the wave's curve table
			 *             (oscChopCurveRs linear, oscChopCurveRex the
			 *             exponential) on the 1x phase wrapped at 7/8 of the
			 *             cycle, inside the last chopped section for every
			 *             width; s = (v x g - chDc) x sScale, chDc the wave's
			 *             mean (oscChopDc*Q15[pos] x span), sScale =
			 *             ALK_CHOP_SCALE_V x level x drive / span (the
			 *             caller's). ALK_CHOP_READ_FORM 2 truncates all three
			 *             points; 0 interpolates them (the bench reference).
			 * Why:        DRAFT_S-R_PWM_WAVE.md §4: the Alpha Juno's PWM saw is
			 *             a ramp chopped to its floor by a pulse an octave up.
			 *             A smooth ramp times a band-limited gate, with the
			 *             ramp's own wrap inside a chopped section (where the
			 *             gate is about 0), keeps the alias near the shipped
			 *             pulse's (prototype §5.3: -72 dB, against -39 dB with
			 *             the wrap at the cycle end). The gate's B offset is a
			 *             whole number of points for the block, as mode 3's
			 *             (the S094 P1 S5 form; the ladder hides the
			 *             block-edge step of a moving width).
			 * Cost:       estimated mode 3 + 5 (S-R draft §4.3); the linked
			 *             count is gate B-G3 (loop + per block <= 263.7).
			 */
			const float chC = ff0, chDc = ffStep;
			const uint32_t itg = (ph >> 21) & 0x000003FFu;
			const uint32_t ib = (itg - pwD) & 0x000003FFu;
			const uint32_t iv = (ph + 0x20000000u) >> 22;
#if ALK_CHOP_READ_FORM == 2
			const float g = (float)(row[itg] - row[ib]) + chC;
			const float v = (float)rowB[iv];
#else
			const float fg = (float)((ph << 1) & 0x003FFFFFu) * 2.384185791015625e-07f;
			const float fv = (float)((ph + 0x20000000u) & 0x003FFFFFu) * 2.384185791015625e-07f;
			const float ga = (float)row[itg] + fg * (float)(row[(itg + 1u) & 0x000003FFu] - row[itg]);
			const float gb = (float)row[ib] + fg * (float)(row[(ib + 1u) & 0x000003FFu] - row[ib]);
			const float g = ga - gb + chC;
			const float v = (float)rowB[iv] + fv * (float)(rowB[(iv + 1u) & 0x000003FFu] - rowB[iv]);
#endif
			ph += phaseInc;
			s = (v * g - chDc) * sScale;
```

(The next existing line is `} else if (fused) {`, unchanged.)

**B2.4 after l. 710 (the closing `}` of `DiodeLadder_renderB2OscPwmWt()`) — ADD:**

```c

/*
 * B2R with the chopped pwm waves fused: R-S and REx (S099 P1, fused mode 5).
 *
 * What:       the fused loop with the input s_j = (C(r_j) x G_j - chDc) x
 *             sScale: G_j the gate, the row's difference at twice the phase
 *             and pwD points earlier plus chC; C the curve table read at the
 *             1x ramp wrapped at 7/8 (ALK_CHOP_READ_FORM); the phase advances
 *             by phaseInc per sample and is written back.
 * Why:        the Alkaline's R-S and REx (S-R draft §4.3, stage 1): one more
 *             loop copy in flash, no ITCM. noinline as the other wrappers: its
 *             registers stay out of the caller, and its only loop is the audio
 *             loop (control_cost.py weight 0).
 * Inputs:     dl (the ladder; DiodeLadder_setNet() and DiodeLadder_setPost()
 *             ran this block), buf (output only), row (the gate's sawTable
 *             row, the note's octave + OSC_CHOP_LIFT, clamped to 10), curve
 *             (oscChopCurveRs or oscChopCurveRex), phase (in/out), phaseInc,
 *             pwD (oscPwmDSaw[pos], 512 .. 26 points of the gate's cycle),
 *             chC (w x span), chDc (the mean x span), sScale (ALK_CHOP_SCALE_V
 *             x level x drive / span), iLadEnd, iAbcEnd (as the other B2R
 *             wrappers).
 * Outputs:    buf (int16, the ladder's toned VCA output); *phase; the ladder
 *             state.
 * Accessors:  AlkalineVoice.c alk_renderChop().
 * Affiliates: oscChopTables.h; ALK_CHOP_READ_FORM; bench law AL-CH;
 *             control_cost.py ITERS.
 */
__attribute__((noinline)) void DiodeLadder_renderB2OscChop(DiodeLadder *dl, int16_t *buf,
                              const int16_t *row, const int16_t *curve,
                              uint32_t *phase, const uint32_t phaseInc,
                              const uint32_t pwD, const float chC, const float chDc,
                              const float sScale,
                              const float iLadEnd, const float iAbcEnd)
{
	dl_renderB2Body(dl, buf, sScale, iLadEnd, iAbcEnd, 5, row, phase, phaseInc,
	                curve, chC, chDc, pwD, 0.0f, 0u, 0u, 0u);
}
```

### B3. `Core/DSPAudio/DiodeLadder.h` l. 236 (after the `DiodeLadder_renderB2OscPwmWt()` prototype) — ADD:

```c
/*
 * B2R with the chopped pwm waves fused (S099 P1): R-S and REx (mode 5), the
 * gate (a saw-row difference at twice the phase plus chC) times the curve
 * table at the 1x ramp wrapped at 7/8, minus chDc, s = (C G - chDc) x
 * sScale. Contract in DiodeLadder.c.
 * Accessors: AlkalineVoice.c alk_renderChop().
 */
void DiodeLadder_renderB2OscChop(DiodeLadder *dl, int16_t *buf, const int16_t *row,
                                 const int16_t *curve, uint32_t *phase,
                                 uint32_t phaseInc, uint32_t pwD, float chC,
                                 float chDc, float sScale, float iLadEnd,
                                 float iAbcEnd);
```

### B4. `Core/DSP/Instruments/Alkaline/AlkalineVoice.c`

**B4.1 l. 11 — MODIFY:**

```c
#include "oscPwmTables.h"         /* S094 P1 S5: the pwm level table */
#include "oscChopTables.h"        /* S099 P1: the R-S / REx curves, gate gains, mean and level */
```

**B4.2 after l. 65 (`#define ALK_TRI_SCALE_V ...`) — ADD:**

```c

/*
 * The chopped waves' input scale (S099 P1).
 *
 * What:       volts per unit of the chop's normalised wave (peak after its
 *             mean = 1 x level): 2.675 V / 32,767 x the table units, so the
 *             chop's positive peak reaches the 303 saw's 2.675 V.
 * Why:        the chop is unipolar around its mean (the ramp's floor is the
 *             gate's floor); keeping its peak at the saw's peak drives the
 *             ladder about as hard at pos 0. By ear in the B-G5 listening.
 * Accessors:  alk_renderChop().
 */
#define ALK_CHOP_SCALE_V  (2.675f / 32767.0f)
```

**B4.3 before l. 695 (the `alk_renderPwm()` header) — ADD the chop render:**

```c
/*
 * One block of typ pwm R-S or REx (S099 P1, fused mode 5).
 *
 * What:       k = positionApplied (0..127: 0 the gate at 50 %, the Alpha
 *             Juno's Saw 2; 127 the narrowest, 2.54 %); the gate row r =
 *             min(tableOffset + OSC_CHOP_LIFT, 10) (the gate plays an octave
 *             up; lift 2 halves its band again, Q-15); d = oscPwmDSaw[k];
 *             then per block, with span = oscChopSpanF[r]:
 *               chC    = d / 1,024 x span     (the gate's floor at 0)
 *               chDc   = oscChopDc*Q15[k] x span (the wave's mean)
 *               sScale = ALK_CHOP_SCALE_V x oscChopLevel*[k] x drive
 *                        x oscChopInvSpanF[r]
 *             into DiodeLadder_renderB2OscChop() with the wave's curve table.
 *             REx for waveform 4 and above (a stored byte past the list plays
 *             the last entry, the platform's rule).
 * Why:        noinline, as the other renders: each type's render keeps its
 *             registers to itself. Every value is a table read or a product:
 *             no VDIV.
 * Inputs:     voice (the async ran this block: phaseInc, tableOffset,
 *             positionApplied, driveGain, makeup); buf (output).
 * Outputs:    buf; osc.phase; the ladder state.
 * Accessors:  alk_renderPwm().
 * Affiliates: oscChopTables.h; DiodeLadder_renderB2OscChop(); bench law AL-CH.
 */
static __attribute__((noinline)) void alk_renderChop(AlkalineVoice *voice, int16_t *buf)
{
	OscInfo *const osc = &voice->osc;
	const uint32_t k = (uint32_t)(osc->positionApplied & 127u);
	const uint8_t rex = (uint8_t)(osc->waveform >= ALK_WAVE_REX);
	uint32_t r = (uint32_t)osc->tableOffset + OSC_CHOP_LIFT;

	if (r > 10u)
		r = 10u;
	{
		const uint32_t d = oscPwmDSaw[k];
		const float span = oscChopSpanF[r];
		const float chC = (float)d * (1.0f / 1024.0f) * span;
		const float chDc = (float)(rex ? oscChopDcRexQ15[k] : oscChopDcRsQ15[k]) * span;
		const float level = rex ? oscChopLevelRex[k] : oscChopLevelRs[k];

		DiodeLadder_renderB2OscChop(&voice->ladder, buf, sawTable[r],
		                            rex ? oscChopCurveRex : oscChopCurveRs,
		                            &osc->phase, osc->phaseInc, d, chC, chDc,
		                            ALK_CHOP_SCALE_V * level * voice->driveGain
		                                * oscChopInvSpanF[r],
		                            voice->iLadEnd, voice->iAbcEnd * voice->makeup);
	}
}

```

**B4.4 l. 696–757 (`alk_renderPwm()`) — MODIFY:** in the header's "By wave:"
list (l. 699–708), add the line "R-S / REx -> alk_renderChop() (mode 5,
S099 P1)". In the body, after l. 730 (`const float iAbc = ...;`), add:

```c

	/*
	 * S099 P1: R-S and REx have their own render (fused mode 5).
	 *
	 * What:       one compare per block before the Tri / Saw / Sqr forms.
	 * Cost:       about 2 instructions per block on those forms (≈ +0.06 per
	 *             sample); none of them is the Alkaline's dearest setting
	 *             (wtb with a slot, 276.7).
	 */
	if (osc->waveform >= ALK_WAVE_RS) {
		alk_renderChop(voice, buf);
		return;
	}
```

**B4.5 l. 796–800 (`Alkaline_setOscType()`) — MODIFY:**

```c
	/* the number of entries of the new type's wav list (S094 P4; S099 P1):
	 * Saw Sqr Tri under wav, Saw Sqr Tri R-S REx under pwm, the installed
	 * tables under wtb; a stored byte past the list clamps to its last
	 * entry (a pwm R-S / REx switched to wav plays Tri) */
	const uint8_t count = (type == OSC_TYPE_WTB) ? sampleMemory_getNumWavetables()
	                    : ((type == OSC_TYPE_PWM) ? (uint8_t)ALK_PWM_WAVE_COUNT
	                                              : (uint8_t)ALK_WAVE_COUNT);
```

**B4.6 l. 1–3 (file header) — MODIFY:** "S094 P1/P4: pwm and the Tri wave;
S099 P1: the chopped waves R-S and REx (mode 5)".

### B5. Wave list, menu and domain

**B5.1 `Core/DSP/Instruments/InstrumentManager.c` l. 1074–1079
(`instrumentManager_waveDomainMax()`, the Alkaline branch) — MODIFY:**

```c
            /* S093 P8 / S094 P4: an Alkaline slot lists Saw, Sqr, Tri under
             * wav; S099 P1: Saw, Sqr, Tri, R-S, REx under pwm. The
             * LFO/velocity range, menu clamp, edit count and typ switch clamp
             * all take it from here. */
            if (osc && instrumentManager_slotType(slot) == INSTRUMENT_TYPE_ALK) {
                if (osc->oscType == OSC_TYPE_WAV)
                    return (uint8_t)(ALK_WAVE_COUNT - 1u);
                if (osc->oscType == OSC_TYPE_PWM)
                    return (uint8_t)(ALK_PWM_WAVE_COUNT - 1u);
            }
```

(P-D, step D6.9, changes the type test to the engine test.)

**B5.2 `Core/DSP/Instruments/InstrumentManager.c` l. 1036–1043 (the
`waveDomainMax` header, "an Alkaline under wav or pwm: ALK_WAVE_COUNT - 1
(Saw, Sqr, Tri)") — MODIFY:** change it to "an Alkaline under wav:
ALK_WAVE_COUNT - 1 (Saw Sqr Tri); under pwm ALK_PWM_WAVE_COUNT - 1 (... R-S
REx, S099 P1)".

**B5.3 `Core/Menu/MenuText.h` after l. 270 (the closing `};` of
`alkWaveNames`) — ADD:**

```c
/*
 * Alkaline pwm waveform names (S099 P1).
 *
 * What:       [0] = count (5), then Saw, Sqr, Tri, R-S, REx: ALK_WAVE_SAW 0 ..
 *             ALK_WAVE_REX 4 under typ pwm.
 * Why:        under pwm the list has the two chopped waves as well (D-1: pwm
 *             only); menu_formatWaveCell() names it and
 *             instrumentManager_waveDomainMax() limits the range to it. The
 *             count must equal ALK_PWM_WAVE_COUNT (asserted in menu.c).
 * Accessors:  menu_formatWaveCell() (menu.c).
 */
static const char alkPwmWaveNames[][4] = {
    {5}, {"Saw"},{"Sqr"},{"Tri"},{"R-S"},{"REx"},
};
```

**B5.4 `Core/Menu/menu.c` l. 12494–12516 (`menu_formatWaveCell()`, the
Alkaline branch) — MODIFY:**

```c
    /*
     * S093 P8: an Alkaline slot's osc1 under wav lists Saw, Sqr, Tri; S099
     * P1: under pwm Saw, Sqr, Tri, R-S, REx.
     *
     * What:       value -> the list's name (alkWaveNames under wav,
     *             alkPwmWaveNames under pwm), copied to the name and
     *             compact-code fields; any other value names nothing (blank),
     *             as a Drum value past its list.
     * Why:        the stock list (waveformNames: Sin, Tri, ...) is not the
     *             Alkaline's; the voice plays Saw for 0, Sqr for 1, Tri for 2
     *             and (pwm) R-S for 3 and REx for 4. Under wtb the Alkaline
     *             cell takes the shared branch below (wNN, the table name).
     * Accessors:  menu_formatWaveValue3() (compact cells, overlays, the voice
     *             automation display), the single-parameter painter.
     */
    if (isOsc1 && (type == OSC_TYPE_WAV || type == OSC_TYPE_PWM) &&
        instrumentManager_runtimeType(cell->slot) == INSTRUMENT_TYPE_ALK) {
        const char (*const names)[4] = (type == OSC_TYPE_PWM) ? alkPwmWaveNames
                                                              : alkWaveNames;

        if (value < (uint8_t)names[0][0]) {
            memcpy(nameDst, names[value + 1u], 3u);
            memcpy(codeDst, names[value + 1u], 3u);
        }
        return;
    }
```

(P-E, step E6.4, changes the type test to the engine test.)

**B5.5 `Core/Menu/menu.c` after l. 12911 (the `alkWaveNames` assert) — ADD:**

```c
/* S099 P1: the Alkaline pwm list names every pwm value: Saw, Sqr, Tri, R-S,
 * REx (ALK_PWM_WAVE_COUNT, AlkalineVoice.h). */
_Static_assert((sizeof(alkPwmWaveNames) / sizeof(alkPwmWaveNames[0])) - 1u ==
                   ALK_PWM_WAVE_COUNT,
               "alkPwmWaveNames must list Saw, Sqr, Tri, R-S and REx");
```

### B6. Cost tool — `tools/alkaline/control_cost.py`

**B6.1 l. 74 (after the `DiodeLadder_renderB2OscPwmWt` ITERS entry) — ADD:**

```python
    # S099 P1: the chopped waves (alk_renderChop(), mode 5), noinline like
    # OscPwm: the only loop is the audio loop; weight 0, checked by the same
    # guard
    ("DiodeLadder_renderB2OscChop", 1): 0,
```

**B6.2 l. 247–248 (the guard's name tuple) — MODIFY:** append
`"DiodeLadder_renderB2OscChop"` to the tuple.

### B7. Host law AL-CH — `tools/dsp_test/modules/alkaline.c` and `Makefile`

**B7.1 `tools/dsp_test/Makefile`, the `alkaline` rule (l. 439–441
prerequisites; l. 442–457 extracts) — MODIFY:** add these to the
prerequisites and extracts:

```make
	$(EX) --src $(ROOT)/Core/DSPAudio/oscChopTables.h --body --out $(GEN)/oscChopTables.host.h
	$(EX) --src $(ROOT)/Core/DSPAudio/oscChopTables.c --body --out $(GEN)/oscChopTables.host.c
```

Then, after the S094 P1 S5 reference build (l. 458–461), ADD the chop
reference:

```make
# S099 P1: the chop read-form reference (ALK_CHOP_READ_FORM 0, every point
# interpolated) renders build/alk_chop_ref.bin for law AL-CH.
	$(HOSTCC) $(HOSTFLAGS) -DALK_CHOP_READ_FORM=0 $(OSC_HOST_INC) modules/alkaline.c \
	  $(CAT_HOST_SRC) -o build/alkaline_chop_ref -lm
	./build/alkaline_chop_ref --chop-render build/alk_chop_ref.bin
```

After the last Alkaline break (l. 545, `alkaline_break_tri`) — ADD three
breaks:

```make
# S099 P1 breaks (law AL-CH must fail each): (1) the ramp wrapped at the
# cycle end (no 2^29 offset); (2) the gate's floor offset missing (chC 0);
# (3) the gate a sum (sign +1).
	mkdir -p build/break_alk_chwrap build/break_alk_chc build/break_alk_chsign
	sed 's/const uint32_t iv = (ph + 0x20000000u) >> 22;/const uint32_t iv = ph >> 22;/' \
	  $(GEN)/DiodeLadder.host.c > build/break_alk_chwrap/DiodeLadder.host.c
	@grep -q 'const uint32_t iv = ph >> 22;' build/break_alk_chwrap/DiodeLadder.host.c || \
	  { echo "alkaline_break_chwrap: sed did not apply"; exit 1; }
	$(HOSTCC) -Ibuild/break_alk_chwrap $(HOSTFLAGS) $(OSC_HOST_INC) modules/alkaline.c \
	  $(CAT_HOST_SRC) -o build/alkaline_break_chwrap -lm
	@if ./build/alkaline_break_chwrap >/dev/null 2>&1; then \
	  echo "alkaline_break_chwrap: FAILED (a wrap at the cycle end not caught)"; exit 1; \
	else echo "alkaline_break_chwrap: caught (law AL-CH fails as expected)"; fi
	sed 's/const float g = (float)(row\[itg\] - row\[ib\]) + chC;/const float g = (float)(row[itg] - row[ib]);/' \
	  $(GEN)/DiodeLadder.host.c > build/break_alk_chc/DiodeLadder.host.c
	@grep -q 'const float g = (float)(row\[itg\] - row\[ib\]);' build/break_alk_chc/DiodeLadder.host.c || \
	  { echo "alkaline_break_chc: sed did not apply"; exit 1; }
	$(HOSTCC) -Ibuild/break_alk_chc $(HOSTFLAGS) $(OSC_HOST_INC) modules/alkaline.c \
	  $(CAT_HOST_SRC) -o build/alkaline_break_chc -lm
	@if ./build/alkaline_break_chc >/dev/null 2>&1; then \
	  echo "alkaline_break_chc: FAILED (a gate without its floor offset not caught)"; exit 1; \
	else echo "alkaline_break_chc: caught (law AL-CH fails as expected)"; fi
	sed 's/const float g = (float)(row\[itg\] - row\[ib\]) + chC;/const float g = (float)(row[itg] + row[ib]) + chC;/' \
	  $(GEN)/DiodeLadder.host.c > build/break_alk_chsign/DiodeLadder.host.c
	@grep -q 'row\[itg\] + row\[ib\]' build/break_alk_chsign/DiodeLadder.host.c || \
	  { echo "alkaline_break_chsign: sed did not apply"; exit 1; }
	$(HOSTCC) -Ibuild/break_alk_chsign $(HOSTFLAGS) $(OSC_HOST_INC) modules/alkaline.c \
	  $(CAT_HOST_SRC) -o build/alkaline_break_chsign -lm
	@if ./build/alkaline_break_chsign >/dev/null 2>&1; then \
	  echo "alkaline_break_chsign: FAILED (a summed gate not caught)"; exit 1; \
	else echo "alkaline_break_chsign: caught (law AL-CH fails as expected)"; fi
```

**B7.2 `tools/dsp_test/modules/alkaline.c`:**
- add `#include "oscChopTables.host.h"` and `#include "oscChopTables.host.c"`
  where the module includes `oscPwmTables.host.*`;
- extend the group enum (l. 164) with `G_CH` and the name list (l. 168)
  with `"AL-CH"`;
- in `main()` (l. 1988–1989) ADD:

```c
	/* S099 P1: write the chop reference render (the form-0 build) */
	if (argc > 2 && !strcmp(argv[1], "--chop-render"))
		return mode_chopRender(argv[2]);
```

Then add `test_chop();` beside `test_pwm();`, and ADD the law (after the
AL-PW block):

```c
/* ---------------------------------------------------------------- AL-CH
 * S099 P1: the Alkaline chopped waves R-S and REx (fused mode 5).
 *   - pos 0, 64, 127 under R-S and REx: finite, bounded, sounding;
 *   - the width slew and the note snap are the pwm rules (AL-PW's, on R-S);
 *   - the read-form error: this build's render (ALK_CHOP_READ_FORM 2) against
 *     the form-0 reference written by the second bench build
 *     (build/alk_chop_ref.bin): printed; it must stay at least 15 dB below the
 *     reference RMS (a gross-error guard, as AL-PW). The three Makefile breaks
 *     (wrap at the cycle end, no floor offset, a summed gate) each move the
 *     render by far more than that, so each fails here;
 *   - level: R-S at pos 0 within 6 dB RMS of Saw under pwm at pos 0 (printed;
 *     the ALK_CHOP_SCALE_V setting, by ear in B-G5). */
#define CH_PATH "build/alk_chop_ref.bin"

static void ch_render(int16_t *out)
{
	AlkalineVoice v;

	gold_voice(&v, ALK_WAVE_SAW, 0u);
	Alkaline_setOscType(&v, ALK_TYP_PWM);
	v.osc.waveform = ALK_WAVE_RS;
	v.osc.position = 24u;
	gold_pattern(&v, out);
	gold_voice(&v, ALK_WAVE_SAW, 0u);
	Alkaline_setOscType(&v, ALK_TYP_PWM);
	v.osc.waveform = ALK_WAVE_REX;
	v.osc.position = 96u;
	gold_pattern(&v, out + (size_t)GOLD_PAT_BLOCKS * BLK);
}

static int mode_chopRender(const char *path)
{
	int16_t *buf = malloc((size_t)2 * GOLD_PAT_BLOCKS * BLK * sizeof(int16_t));
	FILE *fo = fopen(path, "wb");

	ch_render(buf);
	if (!fo || fwrite(buf, sizeof(int16_t), (size_t)2 * GOLD_PAT_BLOCKS * BLK, fo)
	           != (size_t)2 * GOLD_PAT_BLOCKS * BLK)
		return 1;
	fclose(fo);
	free(buf);
	return 0;
}

static void test_chop(void)
{
	static const uint8_t pos[3] = { 0u, 64u, 127u };
	static const uint8_t wave[2] = { ALK_WAVE_RS, ALK_WAVE_REX };

	/* bounds at the width extremes */
	for (int w = 0; w < 2; w++)
		for (int p = 0; p < 3; p++) {
			static int16_t buf[GOLD_PAT_BLOCKS * BLK];
			AlkalineVoice v;
			double s = 0.0;

			gold_voice(&v, ALK_WAVE_SAW, 0u);
			Alkaline_setOscType(&v, ALK_TYP_PWM);
			v.osc.waveform = wave[w];
			v.osc.position = pos[p];
			gold_pattern(&v, buf);
			for (size_t i = 0; i < (size_t)GOLD_PAT_BLOCKS * BLK; i++)
				s += (double)buf[i] * buf[i];
			if (!isfinite(s) || s <= 0.0) fail(G_CH, "bounded and sounding", w, pos[p]);
		}
	/* the read form against the reference */
	{
		const size_t n = (size_t)2 * GOLD_PAT_BLOCKS * BLK;
		int16_t *now = malloc(n * sizeof(int16_t));
		int16_t *ref = malloc(n * sizeof(int16_t));
		FILE *fi = fopen(CH_PATH, "rb");
		double se = 0.0, sr = 0.0;

		ch_render(now);
		if (!fi || fread(ref, sizeof(int16_t), n, fi) != n) {
			fail(G_CH, "no reference render", 0, 0);
		} else {
			for (size_t i = 0; i < n; i++) {
				const double e = (double)now[i] - ref[i];

				se += e * e;
				sr += (double)ref[i] * ref[i];
			}
			{
				const double db = 10.0 * log10((se + 1e-9) / (sr + 1e-9));

				printf("AL-CH: chop form %d against form 0 %.1f dB RMS\n", ALK_CHOP_READ_FORM, db);
				if (ALK_CHOP_READ_FORM != 0 && db > -15.0) fail(G_CH, "read-form error", db, -15.0);
			}
		}
		if (fi) fclose(fi);
		free(now);
		free(ref);
	}
	/* level against Saw under pwm at pos 0 (printed, bounded) */
	{
		AlkalineVoice v;
		static int16_t a[GOLD_PAT_BLOCKS * BLK];
		double sa = 0.0;
		double db;

		gold_voice(&v, ALK_WAVE_SAW, 0u);
		Alkaline_setOscType(&v, ALK_TYP_PWM);
		v.osc.waveform = ALK_WAVE_RS;
		v.osc.position = 0u;
		gold_pattern(&v, a);
		for (size_t i = 0; i < (size_t)GOLD_PAT_BLOCKS * BLK; i++)
			sa += (double)a[i] * a[i];
		db = 20.0 * log10(sqrt(sa / ((double)GOLD_PAT_BLOCKS * BLK)) /
		                  pw_rms(ALK_WAVE_SAW, ALK_TYP_PWM, 0u));
		printf("AL-CH: R-S pwm at 0 against Saw pwm at 0 %+.2f dB RMS\n", db);
		if (fabs(db) > 6.0) fail(G_CH, "R-S level", db, 6.0);
	}
}
```

Note: the reference binary runs `--chop-render` before AL-CH exists in
`build/alk_chop_ref.bin`, so its own AL-CH run is not used. The Makefile
runs only `--chop-render` with it, as it does for AL-PW.

### B-gates

- **B-G1:**
  - `make all && make img`: builds with no warning;
  - `make -C tools/dsp_test all`: every law passes, AL-CH included, and the
    three new breaks print "caught".
- **B-G2:**
  - `elf_p4_check`: no oscillator or mixer loop changes;
  - `elf_s099_check`: the existing B2R wrappers, `alk_renderWav`, every
    Colloid loop and the Tetrakt's are unchanged. `alk_renderPwm` is not in
    the set: it changes by its dispatch test (B4.4).
- **B-G3:** run `python3 tools/dsp_test/elf_looppath.py --elf build/lxr02.elf
  --sym DiodeLadder_renderB2OscChop --cond-heads --in-range` and then
  `control_cost.py` (with the new ITERS entry). The loop plus its per-block
  work must be ≤ **263.7** per sample. The estimate is ≈ 262 + 3.
- **B-G4 (device, image B):**
  - the probe's cold first instance on an Alkaline in pwm R-S, then REx, at
    pos 0 / 64 / 127, must be ≤ **19,609** (the Alkaline's measured worst,
    `wtb` blend);
  - pwm Saw at pos 0 is recorded on the same run, for the record.
- **B-G5 (ear):**
  - R-S and REx against the A2 renders: orientation, k, and the
    ALK_CHOP_SCALE_V level;
  - a pos sweep from an LFO has no zipper beyond pwm Sqr's.

---

## 5. Phase P-C — Colloid `R-S` / `REx` (form T, ITCM chop pair)

### C1. `Core/DSP/Instruments/Colloid/ColloidVoice.h`

**C1.1 l. 185–226 (the pwm list, `COL_PWM_IS_WARP`, `COL_PWM_FORM_COUNT`,
the assert) — MODIFY:**

```c
/*
 * osc.waveform under typ pwm (S095 P3 F2, Q10; S099 P1 the chopped waves).
 *
 * What:       the platform's pwm list and order (Oscillator.h SINE, TRI,
 *             SAW, REC; OSC_PWM_WAVE_COUNT) and then the Colloid's two
 *             chopped waves: 0 Sin and 1 Tri are the phase-warp forms
 *             (col_warpStore/Add), 2 Saw and 3 Rec the two-read form
 *             (col_pwmPairStore/Add, sign +1 / -1), 4 R-S (the Alpha Juno
 *             chopped saw) and 5 REx (the same chop on a rising exponential):
 *             the chop form (col_chopBlock(): col_chopDcInit() and three
 *             col_chopPairAdd() calls). A byte past REx plays Rec (the
 *             unchanged clamp in col_oscControl() and the sync block).
 * Why:        the first four keep the drum engines' names and order, so a
 *             Scene reads alike across engines; the chopped waves are the
 *             Colloid's (S099 P1, D-1; the drum family's chop is deferred,
 *             plan P-F, Q-2), so the Colloid's pwm names are its own list
 *             (MenuText.h colPwmWaveNames).
 * Accessors:  col_oscControl(), Colloid_calcVoiceSyncBlock(), col_chopBlock(),
 *             Colloid_setOscType(); InstrumentManager.c waveDomainMax.
 */
#define COL_PWM_SIN           0u
#define COL_PWM_TRI           1u
#define COL_PWM_SAW           2u
#define COL_PWM_REC           3u
#define COL_PWM_RS            4u    /* S099 P1: the chopped saw */
#define COL_PWM_REX           5u    /* S099 P1: the chopped exponential */
#define COL_PWM_WAVE_COUNT    6u
```

The `COL_PWM_IS_WARP` / `COL_PWM_FORM_COUNT` comment (l. 208–222) keeps its
text; add one sentence to its "What": "The chop forms (R-S, REx) are not
warp forms: they render COL_PWM_COUNT (6) oscillators, as Saw/Rec."

Then, after `COL_PWM_FORM_COUNT` (l. 224), ADD:

```c
/* S099 P1: 1 for the chop forms R-S and REx (osc.waveform under typ pwm;
 * col_oscControl() and the sync block clamp a byte past the list to Rec
 * before testing, so the test is w >= COL_PWM_RS). */
#define COL_PWM_IS_CHOP(w)    ((uint32_t)(w) >= COL_PWM_RS)
```

and replace the assert (l. 225–226):

```c
_Static_assert(COL_PWM_WAVE_COUNT == OSC_PWM_WAVE_COUNT + 2u,
               "the Colloid's pwm list is the platform's, then R-S and REx "
               "(S099 P1; the drum family's chop is deferred)");
```

### C2. `Core/DSP/Instruments/Colloid/ColloidOsc.c` (and `.h`)

**C2.1 after l. 52 (`#define COL_LOOP_P96 ...`) — ADD:**

```c

/*
 * Placement of the chop pair loop (S099 P1, D-9; ITCM approved Q-3).
 *
 * What:       COL_S099P1_SECTION puts col_chopPairAdd() in .itcm.s099p1,
 *             which STM32F765VIHx_FLASH.ld pins after .itcm.s096p3, so no
 *             existing ITCM function moves (MEMORY.md ITCM order rule).
 *             col_chopDcInit() stays in flash (6 instructions a sample, once
 *             per block; the draft's plan).
 * Why:        the R-S / REx forms render three pairs per block; ITCM runs
 *             them without flash wait states. ≈ 308 B of the 424 B free (the
 *             S-R draft's spike; measured at gate C-G3).
 */
#define COL_S099P1_SECTION __attribute__((section(".itcm.s099p1")))
#define COL_LOOP_P99       COL_S099P1_SECTION __attribute__((noinline, noclone))
```

**C2.2 after l. 104 (the closing `}` of `col_smlawt()`) — ADD:**

```c

/* S099 P1: col_smulwt(s, pq) = (s x (int16)pq[31:16]) >> 16 (SMULWT), the
 * top-half twin of col_smulwb(); the chop loop's second gate gain. */
static inline __attribute__((always_inline)) int32_t col_smulwt(int32_t s, uint32_t pq)
{
#if defined(__ARM_FEATURE_DSP)
	int32_t r;
	__asm__ ("smulwt %0, %1, %2" : "=r"(r) : "r"(s), "r"(pq));
	return r;
#else
	return (int32_t)(((int64_t)s * (int16_t)(pq >> 16)) >> 16);
#endif
}
```

**C2.3 at the end of the file (after l. 522) — ADD:**

```c

/*
 * The chop forms' DC start pass (S099 P1).
 *
 * What:       writes the mid bus and the side bus (one 64-word array, mid
 *             then side) with minus the stack's mean, gliding linearly across
 *             the block: per sample n, bus[n] = aM >> 5, bus[n + 32] = aS >>
 *             5, then aM += dM, aS += dS. The caller passes aM = -32 x the
 *             mean at the block's start widths and dM = start - end (so aM
 *             reaches -32 x the end mean after the block), the same for the
 *             side.
 * Why:        the chop wave has a mean (the gate passes the ramp's floor
 *             upward), and it moves with the width. Starting the buses at
 *             minus the stack's mean takes it out before the drive and the
 *             filters with no work per oscillator, and replaces the Store
 *             form's job of starting the buses (S-R draft §6.3 step 1). The
 *             glide follows the widths' own glide (pwPos0 -> pwPos), so a
 *             moving width moves the mean without a step at the block edge.
 * Cost:       6 instructions a sample (the S-R draft's spike), flash.
 * Inputs:     bus (voice->busM; busS follows it, asserted in ColloidVoice.c),
 *             aM, dM, aS, dS (x 32 of the means; see col_chopBlock()).
 * Outputs:    busM[0..31], busS[0..31].
 * Accessors:  col_chopBlock() (ColloidVoice.c), once per block before the
 *             three col_chopPairAdd() calls.
 */
__attribute__((noinline, noclone)) void col_chopDcInit(int32_t *bus, int32_t aM, const int32_t dM,
                                                       int32_t aS, const int32_t dS)
{
	int32_t *bm = bus;
	int32_t *const end = bus + OUTPUT_DMA_SIZE;

	do {
		bm[0] = aM >> 5;
		bm[OUTPUT_DMA_SIZE] = aS >> 5;
		aM += dM;
		aS += dS;
		bm++;
	} while (bm != end);
}

/*
 * The chop pair loop, form T (S099 P1; S-R draft §6.3, §6.4, D-8).
 *
 * What:       two oscillators per call, added into both buses. Per
 *             oscillator, per sample:
 *               D = row[(p >> 21) & 1,023] - row[q >> 22]   the gate's two
 *                   points, both truncated (form T): A at twice the phase (a
 *                   pulse an octave up), B on its own phase q in the gate's
 *                   2x domain;
 *               g = (D << 2) x g14 >> 16 + ((2p - q) >> 16)  the gate
 *                   normalised to 0 .. 65,536: g14 = 16384 x 65536 / span
 *                   (oscChopGainQ14[row], packed per pair in gq) makes D span
 *                   65,536; (2p - q) is the width x 2^22, so >> 16 is
 *                   w x 65,536, the floor offset, exact and gliding with q;
 *               v = curve[(p + 2^29) >> 22]   the ramp wrapped at 7/8 of the
 *                   cycle (inside the last chopped section for every width)
 *                   read through the wave's curve table (oscChopCurveRs /
 *                   Rex), truncated;
 *               y = g x v >> 16   (SMULWB: about 32,767 x c x gate);
 *               busM += y x lv / 65536, busS += y x lp / 65536 (SMLAWB /
 *                   SMLAWT on the pair's packed halves): lv = the oscillator's
 *                   level relative to the wave's largest (oscChopRel*Q15), lp
 *                   = lv x its stereo position / 2, so the side bus keeps the
 *                   Colloid's S = M x p / 2 rule (L = M + 2S in the filter loop).
 *             B's phase starts at q = 2p - (width0 << 22) and advances by
 *             j = 2 inc - (width - width0) x 2^17 per sample, so after 32
 *             samples q = 2p - (width << 22): the S096 P2 F5 glide, in the
 *             gate's domain. Both phases are written back as the pulse pair
 *             writes p (q is rebuilt from p each block).
 * Why:        the Alpha Juno chopped saw (S-R draft §4) at six oscillators
 *             under the Basic share: form T is the one read form that fits
 *             (54 against the shipped pulse pair's 66, spike). The gate's
 *             normalisation and the per-oscillator level (S099 P1 deviation
 *             §0.3 item 2) cost the bus multiplies the loop needs anyway
 *             (two SMLAW per bus per pair, as col_pwmPairAdd()) and one
 *             SMULW per oscillator; the draft's form left a top row's
 *             oscillator up to 7.7 dB loud and could not hold REx's 3.7x
 *             level range across widths with one stack level.
 * Range:      |D| <= 65,534; (D << 2) x g14 >> 16 <= 66,200 (gen_pwm_tables.py
 *             T12) plus 32,768: g < 2^17; |y| <= g x 32,767 >> 16 < 36,050;
 *             y x lv >> 16 < 18,100 per oscillator; six of them inside int32.
 * Cost:       ≈ 54 instructions a pair a sample (spike, S-R draft §6.5); the
 *             linked count is gate C-G3 (<= 58).
 * Inputs:     row[0..1] (the pair's gate rows: sawTable[octave + 2]),
 *             phase[0..1] (updated), inc[0..1], width[0..1] / width0[0..1]
 *             (pwD / pwD0: oscPwmDSaw at the block's end / start), gq (the
 *             pair's g14 halves), lv / lp (the pair's level and level x
 *             position halves, Q15), curve (the wave's 1,024-point table),
 *             bus[64] (mid then side).
 * Outputs:    busM, busS (added); phase[0..1] advanced by 32 x inc.
 * Placement:  .itcm.s099p1 (COL_LOOP_P99).
 * Accessors:  col_chopBlock() (ColloidVoice.c), three calls per block.
 * Affiliates: oscChopTables.h; tools/dsp_test modules/colloid.c law CO-CHP.
 */
COL_LOOP_P99 void col_chopPairAdd(const int16_t *const *row, uint32_t *phase,
                                  const uint32_t *inc, const uint16_t *width,
                                  const uint16_t *width0, const uint32_t gq,
                                  const uint32_t lv, const uint32_t lp,
                                  const int16_t *curve, int32_t *bus)
{
	const int16_t *const r0 = row[0];
	const int16_t *const r1 = row[1];
	const uint32_t i0 = inc[0];
	const uint32_t i1 = inc[1];
	/* B's increment in the gate's 2x domain: twice the oscillator's, less
	 * the width's change over the block (2^17 per point a sample) */
	const uint32_t j0 = (i0 << 1) - (uint32_t)(((int32_t)width[0] - (int32_t)width0[0]) * 131072);
	const uint32_t j1 = (i1 << 1) - (uint32_t)(((int32_t)width[1] - (int32_t)width0[1]) * 131072);
	uint32_t p0 = phase[0];
	uint32_t p1 = phase[1];
	uint32_t q0 = (p0 << 1) - ((uint32_t)width0[0] << 22);
	uint32_t q1 = (p1 << 1) - ((uint32_t)width0[1] << 22);
	int32_t *bm = bus;
	int32_t *const end = bus + OUTPUT_DMA_SIZE;

	do {
		/* form T: the gate's two points truncated */
		const int32_t d0 = r0[(p0 >> 21) & 0x3FFu] - r0[q0 >> 22];
		const int32_t d1 = r1[(p1 >> 21) & 0x3FFu] - r1[q1 >> 22];
		/* the gate, 0 .. 65,536 (normalised, floored by the width) */
		const int32_t g0 = col_smulwb(d0 << 2, gq) + (int32_t)(((p0 << 1) - q0) >> 16);
		const int32_t g1 = col_smulwt(d1 << 2, gq) + (int32_t)(((p1 << 1) - q1) >> 16);
		/* the ramp wrapped at 7/8, through the curve */
		const int32_t v0 = curve[(p0 + 0x20000000u) >> 22];
		const int32_t v1 = curve[(p1 + 0x20000000u) >> 22];
		const int32_t y0 = col_smulwb(g0, (uint32_t)v0);
		const int32_t y1 = col_smulwb(g1, (uint32_t)v1);

		bm[0] = col_smlawt(y1, lv, col_smlawb(y0, lv, bm[0]));
		bm[OUTPUT_DMA_SIZE] = col_smlawt(y1, lp, col_smlawb(y0, lp, bm[OUTPUT_DMA_SIZE]));
		bm++;
		p0 += i0;
		p1 += i1;
		q0 += j0;
		q1 += j1;
	} while (bm != end);
	phase[0] = p0;
	phase[1] = p1;
}
```

Note: the host form of `col_smulwb()` reads `(int16_t)(pq & 0xFFFF)`, so
`(uint32_t)v0` (a sign-extended int16) gives v0 exactly. On the device
SMULWB reads the bottom half. The two forms agree.

**C2.4 `ColloidOsc.h` l. 86 (after `col_scanPairAdd()`) — ADD the
prototypes, and add a line for the chop loops to the header's "What" list:**

```c
void col_chopDcInit(int32_t *bus, int32_t aM, int32_t dM, int32_t aS, int32_t dS);
void col_chopPairAdd(const int16_t *const *row, uint32_t *phase,
                     const uint32_t *inc, const uint16_t *width,
                     const uint16_t *width0, uint32_t gq, uint32_t lv,
                     uint32_t lp, const int16_t *curve, int32_t *bus);
```

Header "What" line to add after the scan loops' entry (l. 29):

```c
 *               col_chopDcInit / col_chopPairAdd  S099 P1: the chopped pwm
 *                 waves R-S and REx: the buses start at minus the stack's
 *                 mean (flash), then three pairs of (the 2x gate x the
 *                 curve at the 1x ramp) are added, each oscillator at its
 *                 own level (form T: every point truncated; ITCM).
```

### C3. `Core/DSP/Instruments/Colloid/ColloidVoice.c`

**C3.1 l. 15 — MODIFY:**

```c
#include "oscPwmTables.h"       /* the S094 P4 width law: D, levels, warp W/Ra/Rb, Tri lift */
#include "oscChopTables.h"      /* S099 P1: the R-S / REx curves, gate gains, mean and level */
```

**C3.2 before l. 1823 (the `busS must follow busM` assert) — ADD the chop
block:**

```c
/*
 * One block of pwm R-S or REx (S099 P1; S-R draft §6.3, §6.4).
 *
 * What:       for each of the COL_PWM_COUNT (6) oscillators o, with k =
 *             pwPos[o] (the block's end width index) and k0 = pwPos0[o] (its
 *             start):
 *               - the gate row: the octave row col_oscControl() picked for
 *                 the pulse forms (sawTable[octave], read back from row[o]),
 *                 lifted by OSC_CHOP_LIFT, at most row 10;
 *               - the widths: pwD = oscPwmDSaw[k], pwD0 = oscPwmDSaw[k0]
 *                 (col_oscControl() wrote the Rec table's for every pulse
 *                 form; the chop uses the Saw floor of 2.54 %, D-5);
 *               - the pair's packed halves: g14 = oscChopGainQ14[row], rel =
 *                 oscChopRel*Q15[k], rel x position / 2;
 *               - the stack's mean at the start and end widths, mid and side,
 *                 in bus units (dc x rel >> 16; the side also x position);
 *             then col_chopDcInit() starts both buses at minus that mean,
 *             gliding from the start to the end, and col_chopPairAdd() adds
 *             the three pairs. Finally the bus-to-drive gain: the async set
 *             inGain with the stack's mean Rec level (col_oscControl()'s
 *             return for a pulse form, oscPwmLevelRecQ14 / 16,384 averaged);
 *             the chop's level is rel x OSC_CHOP_LREF_* per oscillator, and
 *             its bus multiplies halve the bus, so inGain (and inGainR in the
 *             stereo route) is scaled by 2 x Lref / that mean: one VDIV per
 *             block, chop forms only.
 * Why:        the chop runs in the sync block, not in the async, so
 *             col_oscControl()'s per-oscillator loop and
 *             Colloid_calcVoiceAsync() keep their code (S096 P3 noise update:
 *             anything added to that loop costs every mode); the values this
 *             pass replaces (row, pwD, pwD0) are rewritten by the next
 *             block's col_oscControl() before anything else reads them.
 *             The mean needs no state: both ends of its glide come from the
 *             widths the voice already holds (S099 P1 deviation §0.3 item 4).
 * Range:      per oscillator dc x rel >> 16 < 6,200; the six summed, x 32
 *             (col_chopDcInit()'s Q5), < 1.2 M.
 * Inputs:     voice (the async ran this block: row, pwPos, pwPos0, posPair,
 *             phase, inc, inGain, inGainR, route), form (COL_PWM_RS or
 *             COL_PWM_REX).
 * Outputs:    busM, busS; phase[0..5]; row, pwD, pwD0 (this block's); inGain,
 *             inGainR.
 * Cost:       per block: the six-oscillator loop, one VDIV, the init pass
 *             (6 a sample) and the three pairs (≈ 54 a pair a sample).
 * Accessors:  Colloid_calcVoiceSyncBlock() (typ pwm, wav R-S or REx).
 * Affiliates: col_chopDcInit(), col_chopPairAdd() (ColloidOsc.c);
 *             oscChopTables.h; law CO-CHP.
 */
static __attribute__((noinline)) void col_chopBlock(ColloidVoice *voice, const uint8_t form)
{
	const uint8_t rex = (uint8_t)(form == COL_PWM_REX);
	const int16_t *const curve = rex ? oscChopCurveRex : oscChopCurveRs;
	const int16_t *const dcTab = rex ? oscChopDcRexQ15 : oscChopDcRsQ15;
	const int16_t *const relTab = rex ? oscChopRelRexQ15 : oscChopRelRsQ15;
	const float lref = rex ? OSC_CHOP_LREF_REX : OSC_CHOP_LREF_RS;
	uint32_t gq[COL_PWM_COUNT / 2u] = { 0u };
	uint32_t lv[COL_PWM_COUNT / 2u] = { 0u };
	uint32_t lp[COL_PWM_COUNT / 2u] = { 0u };
	int32_t dcM0 = 0, dcM1 = 0, dcS0 = 0, dcS1 = 0;
	int32_t recSum = 0;
	uint32_t i;

#pragma GCC unroll 1
	for (i = 0u; i < COL_PWM_COUNT; i++) {
		const uint32_t k = (uint32_t)(voice->pwPos[i] & 127u);
		const uint32_t k0 = (uint32_t)(voice->pwPos0[i] & 127u);
		const uint32_t half = (i & 1u) ? 16u : 0u;
		/* the oscillator's Q15 stereo position (posPair: even low, odd high) */
		const int32_t pos = (int32_t)(int16_t)(voice->posPair[i >> 1] >> half);
		const int32_t rel = relTab[k];
		const int32_t relPos = (rel * pos) >> 16;   /* rel x pos / 2, Q15 */
		/* the octave row col_oscControl() picked (a sawTable row) */
		uint32_t r = (uint32_t)(voice->row[i] - sawTable[0]) >> 10;

		r = (r + OSC_CHOP_LIFT > 10u) ? 10u : r + OSC_CHOP_LIFT;
		voice->row[i] = sawTable[r];
		voice->pwD[i] = oscPwmDSaw[k];
		voice->pwD0[i] = oscPwmDSaw[k0];
		recSum += oscPwmLevelRecQ14[k];
		gq[i >> 1] |= (uint32_t)(uint16_t)oscChopGainQ14[r] << half;
		lv[i >> 1] |= (uint32_t)(uint16_t)rel << half;
		lp[i >> 1] |= (uint32_t)(uint16_t)relPos << half;
		dcM0 += ((int32_t)dcTab[k0] * rel) >> 16;
		dcM1 += ((int32_t)dcTab[k] * rel) >> 16;
		dcS0 += ((int32_t)dcTab[k0] * relPos) >> 16;
		dcS1 += ((int32_t)dcTab[k] * relPos) >> 16;
	}
	col_chopDcInit(voice->busM, -(dcM0 * 32), dcM0 - dcM1, -(dcS0 * 32), dcS0 - dcS1);
	for (i = 0u; i < COL_PWM_COUNT / 2u; i++)
		col_chopPairAdd(&voice->row[2u * i], &voice->phase[2u * i], &voice->inc[2u * i],
		                &voice->pwD[2u * i], &voice->pwD0[2u * i], gq[i], lv[i], lp[i],
		                curve, voice->busM);   /* busM then busS: one 64-word bus */
	{
		/* the stack's level: 2 x Lref (the bus multiplies halve the bus) in
		 * place of the mean Rec level the async put into inGain */
		const float ratio = (2.0f * 16384.0f * (float)COL_PWM_COUNT) * lref / (float)recSum;

		voice->inGain *= ratio;
		if (voice->route == COL_RTE_STR || voice->route >= COL_RTE_COUNT)
			voice->inGainR *= ratio;
	}
}

```

**C3.3 l. 1830–1853 (the sync block's header) — MODIFY:** after the "typ
pwm, wav Saw or Rec" lines add:

```c
	 *               typ pwm, wav R-S or REx (S099 P1): col_chopBlock() (the
	 *                 mean's start pass and three col_chopPairAdd() calls);
```

**C3.4 l. 1886 (`} else if (isPwm) {`) — MODIFY:**

```c
	} else if (isPwm && COL_PWM_IS_CHOP(form)) {
		/* S099 P1: the chopped waves (the dispatch costs pwm Saw/Rec about 2
		 * instructions a block, ≈ +0.06 a sample; measured at gate C-G4) */
		col_chopBlock(voice, form);
	} else if (isPwm) {
```

**C3.5 l. 922–925 (`col_oscControl()`) — no code change.** Its form clamp
`(wave < COL_PWM_WAVE_COUNT) ? wave : COL_PWM_REC` (l. 942) now passes 4
and 5 through. They take the pulse path, which is the right one for the
chop's rows, counts and positions. Only the immediate changes (4 → 6).
Add one line to its header's `row:` item (l. 877–881): "R-S and REx (S099
P1) take the Saw/Rec rows, widths and level here; col_chopBlock() lifts the
rows and replaces the widths and the level in the sync block."

### C4. `STM32F765VIHx_FLASH.ld`

**C4.1 l. 182 (after the S096 P3 lines of the ITCM comment) — ADD:**

```
         *             S099 P1: .itcm.s099p1 (the Colloid's chop pair loop,
         *             ColloidOsc.c col_chopPairAdd(); ITCM approved
         *             2026-11-19, plan Q-3) is named after .itcm.s096p3 for
         *             the same reason.
```

**C4.2 l. 192 — MODIFY:**

```
        *(.itcm) *(.itcm.s085) *(.itcm.s087 .itcm.s087.*) *(.itcm.s089) *(.itcm.s089p3) *(.itcm.s094p1) *(.itcm.s095p2) *(.itcm.s095p3) *(.itcm.s096p3) *(.itcm.s099p1) *(.itcm.*)
```

### C5. Menu

**C5.1 `Core/Menu/MenuText.h` after l. 333 (the closing `};` of
`colWaveNames`) — ADD:**

```c
/*
 * The Colloid's pwm wave names (S099 P1).
 *
 * What:       [0] = count (6), then Sin, Tri, Saw, Rec (the platform's pwm
 *             list and order), R-S, REx (the chopped waves): COL_PWM_SIN ..
 *             COL_PWM_REX.
 * Why:        the shared pwm branch of menu_formatWaveCell() names the drum
 *             engines' four from waveformNames, where index 4 is Noi; the
 *             Colloid's two extra waves need their own list. The count must
 *             equal COL_PWM_WAVE_COUNT (asserted in menu.c).
 * Accessors:  menu_formatWaveCell() (menu.c).
 */
static const char colPwmWaveNames[][4] = {
    {6}, {"Sin"},{"Tri"},{"Saw"},{"Rec"},{"R-S"},{"REx"},
};
```

Also amend the `colWaveNames` line of the comment at l. 320–323: "under
pwm the cell takes the Colloid's own pwm list (colPwmWaveNames, S099 P1)".

**C5.2 `Core/Menu/menu.c` l. 12532–12546 (between the Colloid wav branch
and the shared pwm branch) — MODIFY the comment at l. 12532–12537 and ADD
a branch after l. 12545:**

```c
    /*
     * S095 P2 (C-D20): a Colloid slot's osc1 under wav lists Saw, Rec, Tri,
     * Sin (colWaveNames); S099 P1: under pwm its own list Sin, Tri, Saw, Rec,
     * R-S, REx (colPwmWaveNames, the branch below); under wtb the shared
     * catalogue branch (wNN, the table name).
     */
```

```c
    /*
     * S099 P1: a Colloid slot's osc1 under pwm (Sin Tri Saw Rec R-S REx).
     *
     * What:       value 0..COL_PWM_WAVE_COUNT - 1 -> colPwmWaveNames; any
     *             other value names nothing. No compact code (the shared pwm
     *             branch has none either).
     * Why:        the shared branch below names the drum engines' four from
     *             waveformNames, where index 4 is Noi; the drum family has no
     *             chopped waves (plan P-F, deferred).
     */
    if (isOsc1 && type == OSC_TYPE_PWM &&
        instrumentManager_runtimeType(cell->slot) == INSTRUMENT_TYPE_COL) {
        if (value < COL_PWM_WAVE_COUNT)
            memcpy(nameDst, colPwmWaveNames[value + 1u], 3u);
        return;
    }
```

(P-D, step D7.6, changes the type test to the engine test.)

**C5.3 `Core/Menu/menu.c` after l. 12919 (the `colWaveNames` assert) — ADD:**

```c
/* S099 P1: the Colloid's pwm list names every pwm value (COL_PWM_WAVE_COUNT). */
_Static_assert((sizeof(colPwmWaveNames) / sizeof(colPwmWaveNames[0])) - 1u ==
                   COL_PWM_WAVE_COUNT,
               "colPwmWaveNames must list Sin, Tri, Saw, Rec, R-S and REx");
```

**C5.4** `instrumentManager_waveDomainMax()` (l. 1086–1087) and
`Colloid_setOscType()` (l. 2012) already read `COL_PWM_WAVE_COUNT`, so they
follow with no edit. Their comments (l. 1080–1082; ColloidVoice.c l.
2009–2010) say "Sin..Rec"; change them to "Sin..REx (S099 P1)".

### C6. Cost tool — `tools/dsp_test/colloid_cpu.py`

**C6.1 l. 132–230 (`main()`) — MODIFY:**
- after the `scan = ...` line (l. 161) ADD
  `chop = present(a.elf, 'col_chopPairAdd') and present(a.elf, 'col_chopDcInit') and present(a.elf, 'col_chopBlock')`;
- extend `modes` with `+ ([('chop', PWM_COUNT)] if chop else [])`;
- in the per-mode body, handle `typ == 'chop'` before the generic formula:

```python
        if typ == 'chop':
            # S099 P1: the mean's start pass, then n / 2 add pairs; per block
            # the sync block's chop pass (col_chopBlock(), a loop over the six
            # oscillators) on top of the pulse forms' control
            pairs = n // 2
            for mode, render in (('par', 'col_renderPar'), ('ser', 'col_renderSer')):
                per_sample = (loops(a.elf, 'col_chopDcInit')[0][0]
                              + pairs * loops(a.elf, 'col_chopPairAdd')[0][0]
                              + render_per_sample(a.elf, render))
                blk = (base_blk + n * ctl_len
                       + B.block_path(a.elf, 'col_chopDcInit')[0]
                       + pairs * B.block_path(a.elf, 'col_chopPairAdd')[0]
                       + n * loops(a.elf, 'col_chopBlock')[0][0]
                       + B.block_path(a.elf, 'col_chopBlock')[0]
                       + B.block_path(a.elf, render)[0])
                total = per_sample + blk / FRAMES
                name = f'chop{n}-{mode}'
                note = ''
                if name in want and total > want[name] + 0.5:
                    note = f'  GREW (baseline {want[name]:.1f})'
                    fail = True
                print(f'{name}: loops {per_sample:.1f} + per block {blk}/{FRAMES} = {total:.1f}{note}')
            continue
```

### C7. Host laws — `tools/dsp_test/modules/colloid.c` and `Makefile`

**C7.1 `Makefile`, the `colloid` rule (l. 586–597) — MODIFY:** add
`$(GEN)/oscChopTables.host.h` and `$(GEN)/oscChopTables.host.c` to its
prerequisites, with the same two `$(EX)` lines as B7.1 (or a shared rule
for both targets).

After the last Colloid break — ADD three:

```make
# S099 P1 breaks (law CO-CHP must fail each): the ramp wrapped at the cycle
# end; the gate's floor offset missing; the gate a sum.
	mkdir -p build/break_col_chwrap build/break_col_chc build/break_col_chsign
	sed 's/curve\[(p0 + 0x20000000u) >> 22\]/curve[p0 >> 22]/' \
	  $(GEN)/ColloidOsc.host.c > build/break_col_chwrap/ColloidOsc.host.c
	@grep -q 'curve\[p0 >> 22\]' build/break_col_chwrap/ColloidOsc.host.c || \
	  { echo "colloid_break_chwrap: sed did not apply"; exit 1; }
	$(HOSTCC) -Ibuild/break_col_chwrap $(HOSTFLAGS) $(OSC_HOST_INC) modules/colloid.c \
	  $(CAT_HOST_SRC) -o build/colloid_break_chwrap -lm
	@if ./build/colloid_break_chwrap >/dev/null 2>&1; then \
	  echo "colloid_break_chwrap: FAILED (a wrap at the cycle end not caught)"; exit 1; \
	else echo "colloid_break_chwrap: caught (law CO-CHP fails as expected)"; fi
	sed 's/ + (int32_t)(((p0 << 1) - q0) >> 16);/;/' \
	  $(GEN)/ColloidOsc.host.c > build/break_col_chc/ColloidOsc.host.c
	@! grep -q '((p0 << 1) - q0) >> 16' build/break_col_chc/ColloidOsc.host.c || \
	  { echo "colloid_break_chc: sed did not apply"; exit 1; }
	$(HOSTCC) -Ibuild/break_col_chc $(HOSTFLAGS) $(OSC_HOST_INC) modules/colloid.c \
	  $(CAT_HOST_SRC) -o build/colloid_break_chc -lm
	@if ./build/colloid_break_chc >/dev/null 2>&1; then \
	  echo "colloid_break_chc: FAILED (a gate without its floor not caught)"; exit 1; \
	else echo "colloid_break_chc: caught (law CO-CHP fails as expected)"; fi
	sed 's/- r0\[q0 >> 22\];/+ r0[q0 >> 22];/' \
	  $(GEN)/ColloidOsc.host.c > build/break_col_chsign/ColloidOsc.host.c
	@grep -q '+ r0\[q0 >> 22\];' build/break_col_chsign/ColloidOsc.host.c || \
	  { echo "colloid_break_chsign: sed did not apply"; exit 1; }
	$(HOSTCC) -Ibuild/break_col_chsign $(HOSTFLAGS) $(OSC_HOST_INC) modules/colloid.c \
	  $(CAT_HOST_SRC) -o build/colloid_break_chsign -lm
	@if ./build/colloid_break_chsign >/dev/null 2>&1; then \
	  echo "colloid_break_chsign: FAILED (a summed gate not caught)"; exit 1; \
	else echo "colloid_break_chsign: caught (law CO-CHP fails as expected)"; fi
```

**C7.2 `modules/colloid.c`:**
- include `oscChopTables.host.h/.c` next to `oscPwmTables.host.*`;
- add `G_CHP` / `"CO-CHP"` to the group lists (l. 139–146), and a line to
  the header's law list;
- in `check_pwm_form()` (l. 1150–1180), give the chop forms their restated
  bus;
- in `test_pwg()` (l. 1683), run the forms through `COL_PWM_REX`;
- add `test_chop()` to `main()`.

```c
/* the chop law, restated (S099 P1): one oscillator's y at sample n of the
 * block, from its block-start phase, increment, end / start widths and row */
static int32_t ref_chopY(const int16_t *row, const int16_t *curve, uint32_t ph0,
                         uint32_t inc, unsigned n, uint16_t d, uint16_t d0, int16_t g14)
{
	const uint32_t p = ph0 + n * inc;
	const uint32_t j = (inc << 1) - (uint32_t)(((int32_t)d - (int32_t)d0) * 131072);
	const uint32_t q = (ph0 << 1) - ((uint32_t)d0 << 22) + n * j;
	const int32_t dd = row[(p >> 21) & 1023u] - row[q >> 22];
	const int32_t g = (int32_t)(((int64_t)(dd * 4) * g14) >> 16) + (int32_t)(((p << 1) - q) >> 16);
	const int32_t v = curve[(p + 0x20000000u) >> 22];

	return (int32_t)(((int64_t)g * v) >> 16);
}
```

In `check_pwm_form()`, replace the loop body's `else` (l. 1171–1173) with:

```c
			else if (COL_PWM_IS_CHOP(form)) {
				/* S099 P1: the restated chop; the voice's bus also holds the
				 * mean's start pass, so the check is the pairs' sum less
				 * that pass (rebuilt below) */
				const int16_t *const curve = (form == COL_PWM_REX) ? oscChopCurveRex : oscChopCurveRs;
				const int16_t *const relT = (form == COL_PWM_REX) ? oscChopRelRexQ15 : oscChopRelRsQ15;
				const uint32_t r = (uint32_t)(V.row[i] - sawTable[0]) >> 10;
				const int32_t y = ref_chopY(V.row[i], curve, ph[i], V.inc[i], n,
				                            V.pwD[i], V.pwD0[i], oscChopGainQ14[r]);
				v = (int32_t)(((int64_t)y * relT[k]) >> 16);
			} else   /* S096 P2 F5: the gliding pulse, from pwD0 to pwD */
```

Then add the mean's start pass for the chop forms.
- Before the `for (n ...)` loop (l. 1162), ADD:

```c
	/* S099 P1: a chop form's bus starts at minus the stack's mean
	 * (col_chopBlock()'s start pass), rebuilt here from the widths */
	int32_t dcM0 = 0, dcM1 = 0;
	if (COL_PWM_IS_CHOP(form)) {
		const int16_t *const dcT = (form == COL_PWM_REX) ? oscChopDcRexQ15 : oscChopDcRsQ15;
		const int16_t *const relT = (form == COL_PWM_REX) ? oscChopRelRexQ15 : oscChopRelRsQ15;

		for (i = 0u; i < COL_PWM_COUNT; i++) {
			const int32_t rel = relT[V.pwPos[i] & 127u];

			dcM0 += ((int32_t)dcT[V.pwPos0[i] & 127u] * rel) >> 16;
			dcM1 += ((int32_t)dcT[V.pwPos[i] & 127u] * rel) >> 16;
		}
	}
```

- After the inner `for (i ...)` loop, before the compare (l. 1175–1176),
  ADD:

```c
		if (COL_PWM_IS_CHOP(form))
			m += (-(dcM0 * 32) + (int32_t)n * (dcM0 - dcM1)) >> 5;
```

The side bus check (with `lp`) is in `test_chop()`.

```c
/* CO-CHP (S099 P1): the chop pair loop against the restated law, both
 * buses, on random rows, phases, widths and levels; then a voice-level
 * check: an R-S stack at psp 0, pos 0 and 127, has a mean of the mid bus
 * within 0.3 % of one oscillator's full scale (the start pass removes the
 * mean), and every oscillator's gate row is the octave row + 2. */
static void test_chop(void)
{
	uint32_t s = 0x5EEDu;
	unsigned t, n;

	for (t = 0u; t < 400u; t++) {
		int32_t bus[2 * BLK], refb[2 * BLK];
		const int16_t *rows[2];
		uint32_t ph[2], inc[2], ph0[2];
		uint16_t w[2], w0[2];
		int16_t g14[2], rel[2], rp[2];
		const int16_t *const curve = (t & 1u) ? oscChopCurveRex : oscChopCurveRs;
		uint32_t gq, lv, lp;

		for (unsigned o = 0u; o < 2u; o++) {
			const uint32_t r = golden_rand(&s) % 11u;
			const uint32_t k = golden_rand(&s) & 127u;
			/* the start width within the slew (+-2 index steps) of the end */
			const uint32_t k0 = (k + 128u + (golden_rand(&s) % 5u) - 2u) & 127u;

			rows[o] = sawTable[r];
			ph0[o] = ph[o] = golden_rand(&s);
			inc[o] = golden_rand(&s) >> 6;
			w[o] = oscPwmDSaw[k];
			w0[o] = oscPwmDSaw[k0];
			g14[o] = oscChopGainQ14[r];
			rel[o] = (int16_t)(golden_rand(&s) & 0x7FFFu);
			rp[o] = (int16_t)((int32_t)(golden_rand(&s) & 0xFFFFu) - 32768);
		}
		gq = (uint32_t)(uint16_t)g14[0] | ((uint32_t)(uint16_t)g14[1] << 16);
		lv = (uint32_t)(uint16_t)rel[0] | ((uint32_t)(uint16_t)rel[1] << 16);
		lp = (uint32_t)(uint16_t)rp[0] | ((uint32_t)(uint16_t)rp[1] << 16);
		for (n = 0u; n < 2u * BLK; n++)
			bus[n] = refb[n] = (int32_t)(golden_rand(&s) & 0xFFFFu) - 32768;
		col_chopPairAdd(rows, ph, inc, w, w0, gq, lv, lp, curve, bus);
		for (n = 0u; n < BLK; n++) {
			const int32_t y0 = ref_chopY(rows[0], curve, ph0[0], inc[0], n, w[0], w0[0], g14[0]);
			const int32_t y1 = ref_chopY(rows[1], curve, ph0[1], inc[1], n, w[1], w0[1], g14[1]);
			const int32_t m = refb[n] + (int32_t)(((int64_t)y0 * rel[0]) >> 16)
			                          + (int32_t)(((int64_t)y1 * rel[1]) >> 16);
			const int32_t sd = refb[n + BLK] + (int32_t)(((int64_t)y0 * rp[0]) >> 16)
			                                 + (int32_t)(((int64_t)y1 * rp[1]) >> 16);

			if (bus[n] != m) { fail(G_CHP, "chop mid bus", bus[n], m); break; }
			if (bus[n + BLK] != sd) { fail(G_CHP, "chop side bus", bus[n + BLK], sd); break; }
		}
		if (ph[0] != ph0[0] + BLK * inc[0]) fail(G_CHP, "chop phase advance", ph[0], 0);
	}
	/* the voice: the mean is removed, the rows are lifted */
	for (unsigned form = COL_PWM_RS; form <= COL_PWM_REX; form++) {
		static const uint8_t posv[2] = { 0u, 127u };

		for (unsigned pi = 0u; pi < 2u; pi++) {
			double sum = 0.0;
			unsigned b;

			voice_init(); host_running = 1u;
			Colloid_setOscType(&V, COL_TYP_PWM);
			V.osc.waveform = (uint8_t)form;
			V.osc.position = posv[pi];
			V.posSpread = 0.0f;
			Colloid_triggerVoice(&V, 0u, 100u, 63u, 5514u, 1u);
			for (b = 0u; b < 400u; b++) {
				block();
				for (n = 0u; n < BLK; n++)
					sum += V.busM[n];
			}
			if (fabs(sum / (400.0 * BLK)) > 0.003 * 32767.0 * COL_PWM_COUNT)
				fail(G_CHP, "the stack's mean is not removed", form, sum / (400.0 * BLK));
			for (unsigned i = 0u; i < COL_PWM_COUNT; i++) {
				const uint32_t r = (uint32_t)(V.row[i] - sawTable[0]) >> 10;

				if (r < OSC_CHOP_LIFT && r != 10u)
					fail(G_CHP, "gate row not lifted", i, r);
			}
		}
	}
}
```

**C7.3 `test_pwg()` l. 1683 — MODIFY:**
`for (form = COL_PWM_SIN; form <= COL_PWM_REX; form++) {`. The click law
applies unchanged to the chop forms: B's gliding phase is the F5 fix in
the gate's domain.

### C-gates

- **C-G1:**
  - `make all && make img`;
  - `make -C tools/dsp_test all`: CO-CHP passes, CO-PWM passes with its
    chop branch, CO-PWG passes on R-S / REx, and the three new breaks print
    "caught".
- **C-G2:**
  - `elf_p4_check`;
  - `elf_s099_check`: every existing Colloid loop is unchanged, and so are
    the Alkaline and Tetrakt sets.
  - `Colloid_calcVoiceSyncBlock` is not in the set: it changes by the
    dispatch test (C3.4).
  - `col_oscControl`'s loop must keep its instruction count. Check it with
    `elf_loopdump.py --sym col_oscControl` against the Phase 0 dump; only
    an immediate may differ.
- **C-G3 (ITCM, Q-3):**
  - `arm-none-eabi-nm --size --radix=d build/lxr02.elf | grep col_chopPairAdd`
    must be ≤ 424 B, and the linker's ITCM ASSERT must hold;
  - record `_eitcm − _sitcm`;
  - `elf_looppath.py --cond-heads --in-range`: `col_chopPairAdd` ≤ **58** a
    pair a sample (spike 54), `col_chopDcInit` ≤ **8**.
  - If the pair loop is > 58, spills came back. Stop and report before the
    device step.
- **C-G4 (host count):** `colloid_cpu.py`: `chop6-par` ≤ `pwm6-par − 20`
  (the draft's −30 less margin). `pwm6-par` ≤ the Phase 0 record + 0.1 (the
  dispatch test).
- **C-G5 (device, image C, the S096 P3 probe procedure):**
  - a lone Colloid, cold first instance, stereo route;
  - pwm Saw/Rec 6 against the S096 P3 record (19,136 median): unchanged
    within the run's noise;
  - chop 6 (R-S, then REx) on the same run: ≤ pwm Saw/Rec 6 and ≤ 19,620.
- **C-G6 (ear):**
  - form T at 55 / 110 Hz (R-3);
  - the R-S / REx level against pwm Rec at pos 0;
  - a width sweep (no click, CO-PWG on the device).

---

## 6. Phase P-D — `clx`: the Advanced Colloid and the BBD chorus

### D1. `tools/chorus/gen_chorus_tables.py` — NEW

```python
#!/usr/bin/env python3
"""Generate the BBD chorus tables (S099 P1; DRAFT_ADVANCED_BBD_CHORUS.md).

What:       writes Core/DSP/Instruments/Chorus/BbdChorusTables.h/.c, every
            table indexed by a descriptor byte (0..127) or by delay / 8:
              bbdRateHz[128]   float 0.1 x 100^(i/127) Hz: the crt law for
                               the var and dbl modes (D-6, Q-10: log 0.1..10)
              bbdKcTone[128]   int16 round(65536 e^(-2 pi fc / fs)), fc =
                               5 kHz x 2.8^(i/127) (5 .. 14 kHz): the
                               complement coefficient 1 - k of the pre and
                               post poles (cbw; the Colloid's fixed
                               BBD_COL_TONE)
              bbdKcTrack[128]  int16 the same for the post pole's tracking
                               cutoff at the delay d = 8 j + 4 samples:
                               0.33 x the MN3009 clock 256 / (2 d / fs),
                               clamped to 5 .. 20 kHz
              bbdMixGain[128]  uint32 the packed dry / wet gains of cmx: low
                               half gn = round(32768 cos(pi m / 2)) - 32768
                               (the dry gain less 1, <= 0), high half gw =
                               round(32767 sin(pi m / 2)), m = i / 127;
                               entry 0 is exactly (0, 0): the input alone
              bbdDrvQ13[128]   int16 round(8192 x 2^(2 i / 127)), capped at
                               32,767: the Alkaline's BBD input gain x1 .. x4
              bbdDrvInvQ15[128] int16 round(32767 x 8192 / bbdDrvQ13[i]): the
                               wet gain's compensation (the wet level holds
                               until the clip)
Why:        per-block lookups for the chorus control (bbdChorus_control()):
            no exp, no VDIV per block.
Checks:     K1 every coefficient in 0..32,767 (SMLAWB / SMLAWT read the
            halfword signed); K2 bbdKcTone strictly decreasing (the cutoff
            rises), bbdKcTrack non-decreasing in j (longer delay, lower
            cutoff); K3 bbdMixGain[0] == 0, the dry gain non-increasing and
            the wet gain non-decreasing in i, bbdMixGain[127]'s dry gain
            within 1 LSB of 0; K4 bbdDrvQ13[0] == 8192 and bbdDrvInvQ15[0] ==
            32767; K5 bbdRateHz increasing, [0] = 0.1, [127] = 10.0; K6
            --check compares both files byte for byte. Exit 1 on a failure.
Inputs:     --check.
Outputs:    the two files, or a pass/fail line.
Accessors:  make -C tools/dsp_test chorus_tables.
Affiliates: BbdChorus.c (the consumer); tools/dsp_test modules/chorus.c.
"""
import math
import sys

FS = 44108.0
N = 128
H_PATH = "Core/DSP/Instruments/Chorus/BbdChorusTables.h"
C_PATH = "Core/DSP/Instruments/Chorus/BbdChorusTables.c"
RATE_LO, RATE_HI = 0.1, 10.0
TONE_LO, TONE_HI = 5000.0, 14000.0     # the complement form's floor is 4.87 kHz
TRACK_K, TRACK_HI = 0.33, 20000.0
D_MIN = 65
DRV_OCT = 2.0


def kc(fc):
    return int(round(65536.0 * math.exp(-2.0 * math.pi * fc / FS)))


def tables():
    rate = [RATE_LO * (RATE_HI / RATE_LO) ** (i / 127.0) for i in range(N)]
    tone = [kc(TONE_LO * (TONE_HI / TONE_LO) ** (i / 127.0)) for i in range(N)]
    track = []
    for j in range(N):
        d = max(8 * j + 4, D_MIN)
        clock = 256.0 / (2.0 * d / FS)
        track.append(kc(min(TRACK_HI, max(TONE_LO, TRACK_K * clock))))
    mix = []
    for i in range(N):
        m = i / 127.0
        gn = int(round(32768.0 * math.cos(math.pi * m / 2.0))) - 32768
        gw = int(round(32767.0 * math.sin(math.pi * m / 2.0)))
        if i == 0:
            gn, gw = 0, 0
        mix.append(((gw & 0xFFFF) << 16) | (gn & 0xFFFF))
    drv = [min(32767, int(round(8192.0 * 2.0 ** (DRV_OCT * i / 127.0)))) for i in range(N)]
    inv = [int(round(32767.0 * 8192.0 / g)) for g in drv]
    return rate, tone, track, mix, drv, inv


def checks(rate, tone, track, mix, drv, inv):
    ok = all(0 <= x <= 32767 for x in tone + track)                       # K1
    ok = ok and all(b < a for a, b in zip(tone, tone[1:]))                 # K2
    ok = ok and all(b >= a for a, b in zip(track, track[1:]))
    gn = [((m & 0xFFFF) ^ 0x8000) - 0x8000 for m in mix]
    gw = [m >> 16 for m in mix]
    ok = ok and mix[0] == 0                                                # K3
    ok = ok and all(b <= a for a, b in zip(gn, gn[1:]))
    ok = ok and all(b >= a for a, b in zip(gw, gw[1:]))
    ok = ok and abs(gn[127] + 32768) <= 1
    ok = ok and drv[0] == 8192 and inv[0] == 32767                         # K4
    ok = ok and all(b > a for a, b in zip(rate, rate[1:]))                 # K5
    ok = ok and abs(rate[0] - 0.1) < 1e-9 and abs(rate[127] - 10.0) < 1e-9
    return ok


def rows(vals, fmt, per):
    return ["    " + ", ".join(fmt % v for v in vals[i:i + per]) + ","
            for i in range(0, len(vals), per)]


def render_h():
    return "\n".join([
        "/* Generated by tools/chorus/gen_chorus_tables.py (S099 P1). Do not edit. */",
        "#ifndef BBD_CHORUS_TABLES_H_",
        "#define BBD_CHORUS_TABLES_H_",
        "",
        "#include <stdint.h>",
        "",
        "/*",
        " * BBD chorus control tables (S099 P1), flash; see the generator's",
        " * docstring for each law. Indexed by a descriptor byte 0..127, or",
        " * (bbdKcTrack) by the delay in samples / 8.",
        " * Accessors: BbdChorus.c bbdChorus_control(), bbd_sidePlan().",
        " */",
        "#define BBD_TABLE_N 128u",
        "extern const float    bbdRateHz[BBD_TABLE_N];",
        "extern const int16_t  bbdKcTone[BBD_TABLE_N];",
        "extern const int16_t  bbdKcTrack[BBD_TABLE_N];",
        "extern const uint32_t bbdMixGain[BBD_TABLE_N];",
        "extern const int16_t  bbdDrvQ13[BBD_TABLE_N];",
        "extern const int16_t  bbdDrvInvQ15[BBD_TABLE_N];",
        "",
        "#endif /* BBD_CHORUS_TABLES_H_ */",
        "",
    ])


def render_c(rate, tone, track, mix, drv, inv):
    p = ["/* Generated by tools/chorus/gen_chorus_tables.py (S099 P1). Do not edit. */",
         '#include "BbdChorusTables.h"', ""]
    p += ["const float bbdRateHz[BBD_TABLE_N] = {"] + rows(rate, "%.9ef", 4) + ["};", ""]
    p += ["const int16_t bbdKcTone[BBD_TABLE_N] = {"] + rows(tone, "%6d", 8) + ["};", ""]
    p += ["const int16_t bbdKcTrack[BBD_TABLE_N] = {"] + rows(track, "%6d", 8) + ["};", ""]
    p += ["const uint32_t bbdMixGain[BBD_TABLE_N] = {"] + rows(mix, "0x%08Xu", 4) + ["};", ""]
    p += ["const int16_t bbdDrvQ13[BBD_TABLE_N] = {"] + rows(drv, "%6d", 8) + ["};", ""]
    p += ["const int16_t bbdDrvInvQ15[BBD_TABLE_N] = {"] + rows(inv, "%6d", 8) + ["};", ""]
    return "\n".join(p)


def main():
    t = tables()
    ok = checks(*t)
    h, c = render_h(), render_c(*t)
    if "--check" in sys.argv:
        ok = ok and open(H_PATH).read() == h and open(C_PATH).read() == c
        print("chorus tables", "OK" if ok else "FAILED")
        return 0 if ok else 1
    if not ok:
        print("chorus tables: K1-K5 failed")
        return 1
    open(H_PATH, "w").write(h)
    open(C_PATH, "w").write(c)
    print("wrote", H_PATH, C_PATH)
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

Run it once from the repository root; it writes the generated pair.

### D2. `Core/DSP/Instruments/Chorus/BbdChorus.h` — NEW

```c
/*
 * BbdChorus.h - the Advanced types' BBD chorus (S099 P1).
 *
 * What:       a stereo emulation of the Roland Alpha Juno chorus (two
 *             MN3009 lines, one triangle LFO, the right line's sweep
 *             inverted; DRAFT_ADVANCED_BBD_CHORUS.md §1, §2): per side a
 *             pre-BBD pole, an int16 ring in the slot's FxBuffer voice unit,
 *             a linearly interpolated tap at a delay that moves linearly
 *             inside each block, a post-BBD pole whose cutoff follows the
 *             delay, and dry x gd + wet x gw. Hosts:
 *               the Colloid (clx), stereo in: csr L-R (each side its own
 *                 channel, bbd_sideD() twice) or mid (one mid into both
 *                 rings, bbd_midA() then bbd_tapB());
 *               the Alkaline (alx), mono in: bbd_alkA() then bbd_alkB() on
 *                 one ring, with the BBD input drive into a hard clip and a
 *                 12 dB wet (a second post pole).
 *             Every value of the unit (rings, guards and BbdChorusState) is
 *             the chorus's; the host keeps only the unit pointer and its
 *             rows' bytes.
 * Why:        the brief (user, 2026-11-19): one 50 ms voice unit while the
 *             effect type is selected, inside the Advanced share.
 * Unit layout (int16 samples of the 2,208-sample unit):
 *               0 .. 1,023      ring L        1,024  guard L (= ring L [0])
 *               1,032 .. 2,055  ring R        2,056  guard R
 *               2,064 ..        BbdChorusState (<= 288 B, asserted)
 *             The pads keep ring R and the state 4-byte aligned.
 * Accessors:  InstrumentManager.c (the cho writer, the clx / alx async and
 *             sync cases, the quiet test).
 * Affiliates: BbdChorusTables.h; FxBuffer.h; tools/dsp_test modules/chorus.c.
 */
#ifndef BBD_CHORUS_H_
#define BBD_CHORUS_H_

#include <stdint.h>
#include "config.h"      /* OUTPUT_DMA_SIZE, REAL_FS */
#include "FxBuffer.h"    /* FXBUF_VOICE_UNIT_SAMPLES, FXBUF_VOICE_UNIT_BYTES */

/* cho (the chorus type row): off, the three Juno-60 modes, the Alpha Juno's
 * variable rate and depth, a wide doubler (draft §5.1). */
#define BBD_MODE_OFF      0u
#define BBD_MODE_I        1u
#define BBD_MODE_II       2u
#define BBD_MODE_I_II     3u
#define BBD_MODE_VAR      4u
#define BBD_MODE_DBL      5u
#define BBD_MODE_COUNT    6u
/* csr (the Colloid's source switch, draft §2.5) */
#define BBD_SRC_LR        0u
#define BBD_SRC_MID       1u
#define BBD_SRC_COUNT     2u

#define BBD_RING          1024u   /* a power of two: the wrap is one AND (D-3) */
#define BBD_RMASK         (BBD_RING - 1u)
#define BBD_RING_R_OFS    1032u
#define BBD_STATE_OFS     2064u
/* the delay's range in samples: >= 65 keeps the guard rule (the tap reaches
 * index 1,023 at least two blocks after ring[0] was written); <= 990 keeps
 * the mid form's pass B behind the 32 samples pass A wrote (S099 P1
 * deviation §0.3 item 8) */
#define BBD_D_MIN         65.0f
#define BBD_D_MAX         990.0f
/* the quiet test: 32 blocks (23 ms) after the host went quiet, every ring
 * sample written while it sounded has passed every tap */
#define BBD_TAIL_BLOCKS   32u
/* the Colloid's fixed wet cutoff (no cbw row on the clx, draft §7.2):
 * bbdKcTone[73] = 9.0 kHz (Q-10, by ear) */
#define BBD_COL_TONE      73u

/*
 * One side of the chorus (S099 P1).
 *
 * What:       ring (this side's ring: ring L, ring R, or ring L again for the
 *             Alkaline's mono form), rp (the Q16 read position at the block's
 *             start: index << 16 | fraction), inc (the Q16 read step per
 *             sample), p (the pre pole's state, x 2), o / o2 (the post
 *             poles' states, x 2; o2 the Alkaline's second pole), kk (packed
 *             1 - k, Q16: the pre pole in the low half, the post pole in the
 *             high half), dEnd (the Q16 delay at the block's end, the next
 *             block's start).
 */
typedef struct {
	int16_t *ring;
	uint32_t rp;
	uint32_t inc;
	int32_t  p, o, o2;
	uint32_t kk;
	int32_t  dEnd;
} BbdSide;

/*
 * The chorus state, kept in the unit's tail (S099 P1).
 *
 * What:       the two sides; w (the ring write count, free-running: the
 *             index is w & 1,023); gk (packed gains: gn = gd - 32768 in the
 *             low half, gw in the high half, Q15); gDrv (the Alkaline's BBD
 *             input gain, Q13, x1 .. x4); lfoPh (the triangle's phase, 2^32 a
 *             cycle); depthNow / cNow (the slewed sweep depth and centre, in
 *             samples); idleBlocks (blocks since the host went quiet, held at
 *             0xFFFF); mono (1 for the Alkaline's one-ring form).
 * Why:        the voice structs keep only 12 B (Colloid) / 76 B (Alkaline);
 *             the unit has 288 B after its rings (draft §3.1).
 */
typedef struct {
	BbdSide  side[2];
	uint32_t w;
	uint32_t gk;
	uint32_t gDrv;
	uint32_t lfoPh;
	float    depthNow;
	float    cNow;
	uint16_t idleBlocks;
	uint8_t  mono;
	uint8_t  pad;
} BbdChorusState;

_Static_assert(sizeof(BbdChorusState) <= (FXBUF_VOICE_UNIT_SAMPLES - BBD_STATE_OFS) * 2u,
               "BbdChorusState must fit the unit's tail (288 B)");
_Static_assert(BBD_RING_R_OFS >= BBD_RING + 1u && BBD_STATE_OFS >= BBD_RING_R_OFS + BBD_RING + 1u,
               "ring R and the state must follow ring L and its guard");
_Static_assert((BBD_RING_R_OFS % 2u) == 0u && (BBD_STATE_OFS % 2u) == 0u,
               "ring R and the state must be 4-byte aligned");

/*
 * Entry points (S099 P1).
 *
 * bbdChorus_init(unit, mono, mode)
 *   What:     clears the whole unit (the FxBuffer contract: owners clear
 *             what they claim) and seeds the state: both sides on their
 *             rings (mono: both on ring L), the centre at the mode's, the
 *             depth 0 (it slews in), the delay ends at the centre, the gains
 *             dry only (until the first control block).
 *   Context:  foreground (the cho writer, a first bind after a slot reset).
 * bbdChorus_control(unit, mode, rate, depth, mix, tone, drive, width, quiet)
 *   What:     the block's control: the LFO step, the depth and centre slew,
 *             each side's delay at the block's end and its read plan
 *             (bbd_sidePlan()), the pole coefficients (tone, and the post
 *             pole's tracking), the gains (mix, drive), the quiet count.
 *   Context:  render (the slot async), every block while the unit is bound.
 * bbdChorus_renderColloid(unit, left, right, src)
 * bbdChorus_renderAlkaline(unit, left, right)
 *   What:     the block's audio, in place on the host's two int16 blocks
 *             (the Alkaline's right block is written from the left first).
 *   Context:  render (the slot sync), after the host's own render.
 * bbdChorus_tailDone(unit)
 *   What:     1 once the host has been quiet for BBD_TAIL_BLOCKS blocks.
 */
void bbdChorus_init(int16_t *unit, uint8_t mono, uint8_t mode);
void bbdChorus_control(int16_t *unit, uint8_t mode, uint8_t rate, uint8_t depth,
                       uint8_t mix, uint8_t tone, uint8_t drive, uint8_t width,
                       uint8_t quiet) __attribute__((noinline));
void bbdChorus_renderColloid(int16_t *unit, int16_t *left, int16_t *right,
                             uint8_t src) __attribute__((noinline));
void bbdChorus_renderAlkaline(int16_t *unit, int16_t *left, int16_t *right)
	__attribute__((noinline));
uint8_t bbdChorus_tailDone(const int16_t *unit);

#endif /* BBD_CHORUS_H_ */
```

### D3. `Core/DSP/Instruments/Chorus/BbdChorus.c` — NEW

```c
/*
 * BbdChorus.c - the Advanced types' BBD chorus (S099 P1). Contract:
 * BbdChorus.h. Behaviour: DRAFT_ADVANCED_BBD_CHORUS.md §1-§2, §5.1, with the
 * S099 P1 deviations (S099_P1_IMPLEMENTATION.md §0.3 items 6-9).
 */
#include <math.h>
#include <string.h>
#include "BbdChorus.h"
#include "BbdChorusTables.h"

/*
 * The DSP multiplies (S099 P1): the Colloid's col_smlaw* pattern.
 *
 * What:       bbd_smlawb(a, b, acc) = acc + (a x (int16)b[15:0]) >> 16 and
 *             bbd_smlawt the same with b's high half (SMLAWB / SMLAWT); the
 *             SMUAD dual multiply-add; SSAT to int16. The host build (no DSP
 *             extension) uses the exact C equivalents.
 * Why:        one instruction each; GCC does not reliably select them from
 *             the C expression (voiceFold.c, S089 P2).
 */
static inline __attribute__((always_inline)) int32_t bbd_smlawb(int32_t a, uint32_t b, int32_t acc)
{
#if defined(__ARM_FEATURE_DSP)
	int32_t r;
	__asm__ ("smlawb %0, %1, %2, %3" : "=r"(r) : "r"(a), "r"(b), "r"(acc));
	return r;
#else
	return (int32_t)(((int64_t)a * (int16_t)(b & 0xFFFFu)) >> 16) + acc;
#endif
}

static inline __attribute__((always_inline)) int32_t bbd_smlawt(int32_t a, uint32_t b, int32_t acc)
{
#if defined(__ARM_FEATURE_DSP)
	int32_t r;
	__asm__ ("smlawt %0, %1, %2, %3" : "=r"(r) : "r"(a), "r"(b), "r"(acc));
	return r;
#else
	return (int32_t)(((int64_t)a * (int16_t)(b >> 16)) >> 16) + acc;
#endif
}

static inline __attribute__((always_inline)) int32_t bbd_smulwb(int32_t a, uint32_t b)
{
#if defined(__ARM_FEATURE_DSP)
	int32_t r;
	__asm__ ("smulwb %0, %1, %2" : "=r"(r) : "r"(a), "r"(b));
	return r;
#else
	return (int32_t)(((int64_t)a * (int16_t)(b & 0xFFFFu)) >> 16);
#endif
}

static inline __attribute__((always_inline)) int32_t bbd_smuad(uint32_t a, uint32_t b)
{
#if defined(__ARM_FEATURE_DSP)
	int32_t r;
	__asm__ ("smuad %0, %1, %2" : "=r"(r) : "r"(a), "r"(b));
	return r;
#else
	return (int32_t)(int16_t)(a & 0xFFFFu) * (int16_t)(b & 0xFFFFu) +
	       (int32_t)(int16_t)(a >> 16) * (int16_t)(b >> 16);
#endif
}

static inline __attribute__((always_inline)) int16_t bbd_ssat16(int32_t x)
{
#if defined(__ARM_FEATURE_DSP)
	int32_t r;
	__asm__ ("ssat %0, #16, %1" : "=r"(r) : "r"(x));
	return (int16_t)r;
#else
	return (int16_t)((x > 32767) ? 32767 : ((x < -32768) ? -32768 : x));
#endif
}

/*
 * The interpolated tap (S099 P1; the spike's tapD()).
 *
 * What:       i = (rp >> 16) & 1,023, f = (rp >> 2) & 0x3FFF (Q14); one
 *             32-bit load of [i] (low half) and [i + 1] (high half), then
 *             SMUAD with (f << 16) | (0x4000 - f): (1 - f) r[i] + f r[i + 1]
 *             in Q14, >> 13 = x 2 (the post poles' state scale).
 * Why:        the guard sample at index 1,024 makes the pair read never wrap
 *             (BBD_D_MIN); the M7 allows the unaligned load in DTCM
 *             (SCB->CCR UNALIGN_TRP is clear: checked, no source sets it).
 */
static inline __attribute__((always_inline)) int32_t bbd_tap(const int16_t *r, const uint32_t rp)
{
	const uint32_t i = (rp >> 16) & BBD_RMASK;
	const uint32_t f = (rp >> 2) & 0x3FFFu;
	uint32_t pair;

	memcpy(&pair, r + i, 4u);
	return bbd_smuad(pair, (f << 16) | (0x4000u - f)) >> 13;
}

/* The state in the unit's tail. */
static inline BbdChorusState *bbd_state(int16_t *unit)
{
	return (BbdChorusState *)(void *)(unit + BBD_STATE_OFS);
}

/*
 * The loops (S099 P1). Each renders one channel of one 32-sample block in
 * place: one channel per pass (draft §4.2: a single stereo loop spilled
 * about 16 values, 66-69 a frame; one channel a pass is 22). Per sample:
 *   pre pole  p = x2 + (p - x2)(1 - kPre)      (x2 = 2 x the input; the
 *             complement form, S099 P1 deviation §0.3 item 6)
 *   write     ring[w & 1,023] = p >> 1
 *   tap       t = bbd_tap(ring, rp)            (x 2)
 *   post pole o = t + (o - t)(1 - kPost)
 *   out       x (gd) + o / 2 x gw = x + x2 gn / 65,536 + o gw / 65,536
 *             (gn = gd - 32,768 <= 0: cmx 0 has gn = gw = 0, the input
 *             alone, exactly; law CH-MIX0), saturated to int16.
 * Placement:  flash, noinline: these are called from the -O2/LTO
 *             InstrumentManager, and their registers stay their own.
 */
#define BBD_LOOP __attribute__((noinline, noclone))

/*
 * bbd_sideD: one side of the L-R form (the spike's cho_sideD; 22 + the dry
 * gain). x is the side's channel, read as the dry and written as the output.
 * The guard is refreshed after a block whose writes wrapped.
 */
BBD_LOOP static void bbd_sideD(BbdSide *s, int16_t *x, uint32_t w, const uint32_t gk)
{
	int16_t *const r = s->ring;
	uint32_t rp = s->rp;
	const uint32_t inc = s->inc;
	const uint32_t kk = s->kk;
	int32_t p = s->p, o = s->o;
	int16_t *const end = x + OUTPUT_DMA_SIZE;

	do {
		const int32_t xi = *x;
		const int32_t x2 = xi << 1;

		p = bbd_smlawb(p - x2, kk, x2);
		r[w & BBD_RMASK] = (int16_t)(p >> 1);
		{
			const int32_t t = bbd_tap(r, rp);

			o = bbd_smlawt(o - t, kk, t);
		}
		*x++ = bbd_ssat16(bbd_smlawt(o, gk, bbd_smlawb(x2, gk, xi)));
		w++;
		rp += inc;
	} while (x != end);
	/* this block wrote index 0 (its last write (w - 1) is in 0..31): the
	 * guard takes the new ring[0] (the spike's (w & 1023) < 32 refreshed
	 * it one block early, with the previous cycle's sample; CH-GUARD) */
	if (((w - 1u) & BBD_RMASK) < OUTPUT_DMA_SIZE)
		r[BBD_RING] = r[0];
	s->rp = rp;
	s->p = p;
	s->o = o;
}

/*
 * bbd_midA: the mid form's pass A (the spike's cho_monoA + the ring-R mirror
 * store, draft §2.5): m = L + R (2 x the mid) through the pre pole into BOTH
 * rings, so each ring always holds the selected source's last 23 ms (a csr
 * switch needs no clear and no memory action); tap L from ring L, the L dry
 * from xL. xR is read only.
 */
BBD_LOOP static void bbd_midA(BbdSide *s, int16_t *ringR, int16_t *xL, const int16_t *xR,
                              uint32_t w, const uint32_t gk)
{
	int16_t *const r = s->ring;
	uint32_t rp = s->rp;
	const uint32_t inc = s->inc;
	const uint32_t kk = s->kk;
	int32_t p = s->p, o = s->o;
	int16_t *const end = xL + OUTPUT_DMA_SIZE;

	do {
		const int32_t l = *xL;
		const int32_t m = l + *xR++;

		p = bbd_smlawb(p - m, kk, m);
		{
			const int16_t q = (int16_t)(p >> 1);

			r[w & BBD_RMASK] = q;
			ringR[w & BBD_RMASK] = q;
		}
		{
			const int32_t t = bbd_tap(r, rp);

			o = bbd_smlawt(o - t, kk, t);
		}
		*xL++ = bbd_ssat16(bbd_smlawt(o, gk, bbd_smlawb(l << 1, gk, l)));
		w++;
		rp += inc;
	} while (xL != end);
	/* this block wrote index 0 (its last write (w - 1) is in 0..31): the
	 * guard takes the new ring[0] (the spike's (w & 1023) < 32 refreshed
	 * it one block early, with the previous cycle's sample; CH-GUARD) */
	if (((w - 1u) & BBD_RMASK) < OUTPUT_DMA_SIZE) {
		r[BBD_RING] = r[0];
		ringR[BBD_RING] = ringR[0];
	}
	s->rp = rp;
	s->p = p;
	s->o = o;
}

/*
 * bbd_tapB: a tap with no write (the spike's cho_monoB): the mid form's
 * right side on ring R, after pass A wrote the block; dry and output in x.
 */
BBD_LOOP static void bbd_tapB(BbdSide *s, int16_t *x, const uint32_t gk)
{
	const int16_t *const r = s->ring;
	uint32_t rp = s->rp;
	const uint32_t inc = s->inc;
	const uint32_t kk = s->kk;
	int32_t o = s->o;
	int16_t *const end = x + OUTPUT_DMA_SIZE;

	do {
		const int32_t xi = *x;
		const int32_t t = bbd_tap(r, rp);

		o = bbd_smlawt(o - t, kk, t);
		*x++ = bbd_ssat16(bbd_smlawt(o, gk, bbd_smlawb(xi << 1, gk, xi)));
		rp += inc;
	} while (x != end);
	s->rp = rp;
	s->o = o;
}

/*
 * bbd_alkA: the Alkaline's pass A (mono in, draft §4.5 and §5.2): the dry
 * copied to xR first (pass B's dry and output block); the pre pole; the BBD
 * input gain (Q13, x1 .. x4, cdv) into a hard clip at int16 (no compander:
 * a loud input distorts, draft §1.1); tap L; two post poles (a 12 dB wet,
 * Q-12); the L output.
 */
BBD_LOOP static void bbd_alkA(BbdSide *s, int16_t *xL, int16_t *xR, uint32_t w,
                              const uint32_t gk, const uint32_t gDrv)
{
	int16_t *const r = s->ring;
	uint32_t rp = s->rp;
	const uint32_t inc = s->inc;
	const uint32_t kk = s->kk;
	int32_t p = s->p, o = s->o, o2 = s->o2;
	int16_t *const end = xL + OUTPUT_DMA_SIZE;

	do {
		const int32_t xi = *xL;
		const int32_t x2 = xi << 1;

		*xR++ = (int16_t)xi;
		p = bbd_smlawb(p - x2, kk, x2);
		r[w & BBD_RMASK] = bbd_ssat16(bbd_smulwb(p << 2, gDrv));
		{
			const int32_t t = bbd_tap(r, rp);

			o = bbd_smlawt(o - t, kk, t);
			o2 = bbd_smlawt(o2 - o, kk, o);
		}
		*xL++ = bbd_ssat16(bbd_smlawt(o2, gk, bbd_smlawb(x2, gk, xi)));
		w++;
		rp += inc;
	} while (xL != end);
	/* this block wrote index 0 (its last write (w - 1) is in 0..31): the
	 * guard takes the new ring[0] (the spike's (w & 1023) < 32 refreshed
	 * it one block early, with the previous cycle's sample; CH-GUARD) */
	if (((w - 1u) & BBD_RMASK) < OUTPUT_DMA_SIZE)
		r[BBD_RING] = r[0];
	s->rp = rp;
	s->p = p;
	s->o = o;
	s->o2 = o2;
}

/* bbd_alkB: the Alkaline's right side: tap R on the one ring, two post
 * poles, the dry from x (pass A's copy). */
BBD_LOOP static void bbd_alkB(BbdSide *s, int16_t *x, const uint32_t gk)
{
	const int16_t *const r = s->ring;
	uint32_t rp = s->rp;
	const uint32_t inc = s->inc;
	const uint32_t kk = s->kk;
	int32_t o = s->o, o2 = s->o2;
	int16_t *const end = x + OUTPUT_DMA_SIZE;

	do {
		const int32_t xi = *x;
		const int32_t t = bbd_tap(r, rp);

		o = bbd_smlawt(o - t, kk, t);
		o2 = bbd_smlawt(o2 - o, kk, o);
		*x++ = bbd_ssat16(bbd_smlawt(o2, gk, bbd_smlawb(xi << 1, gk, xi)));
		rp += inc;
	} while (x != end);
	s->rp = rp;
	s->o = o;
	s->o2 = o2;
}

/*
 * The modes (S099 P1; draft §1.1, §5.1).
 *
 * What:       per cho value, the LFO rate (Hz; 0 = from crt), the centre
 *             delay and the full sweep depth (samples at 44,108 Hz):
 *               I     0.513 Hz, 154.6 +- 81.4  (1.66 .. 5.35 ms, Juno-60)
 *               II    0.863 Hz, the same
 *               1+2   9.75 Hz,  154.4 +- 8.8   (3.3 .. 3.7 ms)
 *               var   crt (0.1 .. 10 Hz), 154.6 +- 81.4 x cdp / 127
 *               dbl   crt, 662 (15 ms) +- 110 x cdp / 127: a wide doubler
 *             off is never rendered (no unit); its row only fills the table.
 * Why:        the Juno-60's measured table (pendragon-andyh), the Alpha
 *             Juno's variable rate (assumed, D-6), and the doubler that uses
 *             the rest of the 23 ms.
 */
typedef struct {
	float rateHz;
	float centre;
	float depth;
} bbd_mode_t;

static const bbd_mode_t bbdModes[BBD_MODE_COUNT] = {
	{ 0.0f,   154.6f, 0.0f   },   /* off */
	{ 0.513f, 154.6f, 81.4f  },   /* I */
	{ 0.863f, 154.6f, 81.4f  },   /* II */
	{ 9.75f,  154.4f, 8.8f   },   /* I+II */
	{ 0.0f,   154.6f, 81.4f  },   /* var */
	{ 0.0f,   662.0f, 110.0f },   /* dbl */
};

/* the LFO's phase step per Hz per block: 32 / fs x 2^32 */
#define BBD_LFO_PH_PER_HZ  ((float)OUTPUT_DMA_SIZE * 4294967296.0f / REAL_FS)
/* the depth's slew per block (draft §2.2: a jump would blip the pitch) */
#define BBD_DEPTH_SLEW     0.125f
/* the centre's largest move per block, samples: at most half a sample per
 * sample, so a mode change between I and dbl glides (about 31 blocks) with
 * no more than an octave of pitch bend */
#define BBD_CENTRE_STEP    16.0f

void bbdChorus_init(int16_t *unit, const uint8_t mono, const uint8_t mode)
{
	BbdChorusState *const st = bbd_state(unit);
	const bbd_mode_t *const m = &bbdModes[(mode < BBD_MODE_COUNT) ? mode : BBD_MODE_DBL];
	const int32_t dc = (int32_t)(m->centre * 65536.0f);
	const uint32_t k = (uint32_t)(uint16_t)bbdKcTone[BBD_COL_TONE];
	uint32_t i;

	memset(unit, 0, FXBUF_VOICE_UNIT_BYTES);
	st->side[0].ring = unit;
	st->side[1].ring = mono ? unit : unit + BBD_RING_R_OFS;
	for (i = 0u; i < 2u; i++) {
		st->side[i].dEnd = dc;
		st->side[i].kk = k | (k << 16);
	}
	st->cNow = m->centre;
	st->gDrv = 8192u;     /* x1 */
	st->mono = mono;
}

/*
 * One side's plan for the block (S099 P1; draft §2.2).
 *
 * What:       the delay d (samples) clamped to BBD_D_MIN .. BBD_D_MAX; rp =
 *             (w << 16) - the previous block's end delay (exact at every
 *             block edge, so the read position cannot drift); inc = 65,536 -
 *             (dEnd - dPrev) / 32 (the delay moves linearly across the block:
 *             any LFO shape costs only per block); the pre pole's 1 - k from
 *             tone, the post pole's the larger of tone's and the tracking
 *             table's at this delay (the BBD's clock falls as the delay
 *             rises, so its band does).
 */
static void bbd_sidePlan(BbdSide *s, const uint32_t w, const float d, const uint8_t tone)
{
	const float dc = fminf(fmaxf(d, BBD_D_MIN), BBD_D_MAX);
	const int32_t dEnd = (int32_t)(dc * 65536.0f);
	const int32_t kPre = bbdKcTone[tone & 127u];
	const int32_t kTr = bbdKcTrack[((uint32_t)dc >> 3) & 127u];
	const int32_t kPost = (kTr > kPre) ? kTr : kPre;

	s->rp = (w << 16) - (uint32_t)s->dEnd;
	s->inc = (uint32_t)(65536 - ((dEnd - s->dEnd) >> 5));
	s->dEnd = dEnd;
	s->kk = (uint32_t)(uint16_t)kPre | ((uint32_t)(uint16_t)kPost << 16);
}

void bbdChorus_control(int16_t *unit, const uint8_t mode, const uint8_t rate,
                       const uint8_t depth, const uint8_t mix, const uint8_t tone,
                       const uint8_t drive, const uint8_t width, const uint8_t quiet)
{
	/*
	 * The block's control (S099 P1; draft §2.2, about 34 instructions in the
	 * spike plus the tables).
	 *
	 * What:       the quiet count; the triangle LFO (tri = 1 - 2 |t|, t the
	 *             phase as -1..1); the depth's 1/8 slew and the centre's
	 *             step-limited slew toward the mode's; the delays L = c + depth
	 *             x tri, R = c - width x depth x tri (width +1 is the Juno's
	 *             inverted sweep, 0 an unswept R, -1 the same tap: a mono wet;
	 *             cwd on the Alkaline, fixed +1 on the Colloid); each side's
	 *             plan; the gains: bbdMixGain[cmx] (equal power, cmx 0 the
	 *             input alone), the wet gain held against the BBD drive
	 *             (bbdDrvInvQ15) so the drive only adds the clip.
	 * Why:        everything that shapes the sweep and the tone runs here, per
	 *             block, so the loops are the same for every mode (constant
	 *             CPU: a mode never skips work).
	 * Inputs:     the rows' bytes (each masked to 0..127; mode clamped);
	 *             quiet (the host is silent this block).
	 * Outputs:    the state (sides, gains, LFO, slews, idleBlocks).
	 */
	BbdChorusState *const st = bbd_state(unit);
	const bbd_mode_t *const m = &bbdModes[(mode < BBD_MODE_COUNT) ? mode : BBD_MODE_DBL];
	const uint8_t user = (uint8_t)(mode >= BBD_MODE_VAR);
	const float hz = user ? bbdRateHz[rate & 127u] : m->rateHz;
	const float depthT = user ? m->depth * (float)(depth & 127u) * (1.0f / 127.0f) : m->depth;
	const float wd = (float)(width & 127u) * (2.0f / 127.0f) - 1.0f;
	float t, tri;

	st->idleBlocks = quiet ? (uint16_t)((st->idleBlocks < 0xFFFFu) ? st->idleBlocks + 1u : 0xFFFFu)
	                       : 0u;
	st->lfoPh += (uint32_t)(hz * BBD_LFO_PH_PER_HZ);
	t = (float)(int32_t)st->lfoPh * (1.0f / 2147483648.0f);
	tri = 1.0f - 2.0f * fabsf(t);
	st->depthNow += (depthT - st->depthNow) * BBD_DEPTH_SLEW;
	st->cNow += fminf(fmaxf(m->centre - st->cNow, -BBD_CENTRE_STEP), BBD_CENTRE_STEP);
	{
		const float sw = st->depthNow * tri;

		bbd_sidePlan(&st->side[0], st->w, st->cNow + sw, tone);
		bbd_sidePlan(&st->side[1], st->w, st->cNow - wd * sw, tone);
	}
	{
		const uint32_t gm = bbdMixGain[mix & 127u];
		const int32_t gw = (int32_t)(gm >> 16);
		const int32_t gwC = (gw * (int32_t)bbdDrvInvQ15[drive & 127u]) >> 15;

		st->gk = (gm & 0xFFFFu) | ((uint32_t)gwC << 16);
		st->gDrv = (uint32_t)(uint16_t)bbdDrvQ13[drive & 127u];
	}
}

void bbdChorus_renderColloid(int16_t *unit, int16_t *left, int16_t *right, const uint8_t src)
{
	/*
	 * The Colloid's chorus pass (S099 P1; draft §2.5).
	 *
	 * What:       csr mid: pass A writes the mid into both rings and taps L,
	 *             pass B taps R on ring R; R's pre-pole state follows the
	 *             mid's (a switch to L-R starts from it). csr L-R: each side's
	 *             own channel and ring. The write count advances one block.
	 * Why:        the form is chosen once per block (a mode under the
	 *             constant-CPU rule); the slot is charged for L-R, the dearer.
	 *             Both forms write both rings, so a switch is seamless.
	 */
	BbdChorusState *const st = bbd_state(unit);

	if (src == BBD_SRC_MID) {
		bbd_midA(&st->side[0], st->side[1].ring, left, right, st->w, st->gk);
		bbd_tapB(&st->side[1], right, st->gk);
		st->side[1].p = st->side[0].p;
	} else {
		bbd_sideD(&st->side[0], left, st->w, st->gk);
		bbd_sideD(&st->side[1], right, st->w, st->gk);
	}
	st->w += OUTPUT_DMA_SIZE;
}

void bbdChorus_renderAlkaline(int16_t *unit, int16_t *left, int16_t *right)
{
	/*
	 * The Alkaline's chorus pass (S099 P1; draft §4.5): the Alpha Juno's own
	 * topology, a mono voice into a stereo chorus; right is written from
	 * left's dry in pass A, then pass B makes it the right output.
	 */
	BbdChorusState *const st = bbd_state(unit);

	bbd_alkA(&st->side[0], left, right, st->w, st->gk, st->gDrv);
	bbd_alkB(&st->side[1], right, st->gk);
	st->w += OUTPUT_DMA_SIZE;
}

uint8_t bbdChorus_tailDone(const int16_t *unit)
{
	return (uint8_t)(((const BbdChorusState *)(const void *)(unit + BBD_STATE_OFS))->idleBlocks
	                 >= BBD_TAIL_BLOCKS);
}
```

Note on `bbd_alkA()`'s clip:
- `p << 2` times Q13 through SMULWB is `p × gain / 2`, and p is twice the
  filtered input, so the ring holds `input × gain`.
- SSAT clips that at int16.
- At `cdv` 0 (gain 8192 = ×1) the ring holds `p >> 1` exactly, the same as
  the Colloid's store. Law CH-ALK checks this.

### D4. `Makefile`

**D4.1 l. 196 (the end of `DSP_SRCS`, now `oscChopTables.c` after A1.7) — ADD:**

```make
  Core/DSPAudio/oscChopTables.c \
  Core/DSP/Instruments/Chorus/BbdChorus.c \
  Core/DSP/Instruments/Chorus/BbdChorusTables.c
```

**D4.2 after the A1.7 `oscChopTables.o` rule — ADD:**

```make
# S099 P1: the BBD chorus (the Advanced types' insert) and its tables are DSP:
# -Ofast, and -fno-lto for the S095 P2 reason (a new object outside the LTO
# unit cannot shift the other engines' loops; its entry points are noinline
# calls from InstrumentManager).
$(BUILD)/Core/DSP/Instruments/Chorus/BbdChorus.o: Core/DSP/Instruments/Chorus/BbdChorus.c | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS_DSP) $< -o $@ -fno-lto
$(BUILD)/Core/DSP/Instruments/Chorus/BbdChorusTables.o: Core/DSP/Instruments/Chorus/BbdChorusTables.c | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS_DSP) $< -o $@ -fno-lto
```

**D4.3 l. 41 (`-ICore/DSP/Instruments/Tetrakt \` in `CFLAGS`) — ADD after
it:**

```make
          -ICore/DSP/Instruments/Chorus \
```

### D5. Types, writer tag and the Colloid's rows

**D5.1 `Core/DSP/Instruments/InstrumentManager.h` l. 112–113 (after
`INSTRUMENT_TYPE_TKT,`, before `INSTRUMENT_TYPE_UNKNOWN`) — ADD:**

```c
    /*
     * S099 P1: the Advanced Colloid (token "clx"), before UNKNOWN.
     *
     * What:       value 7; UNKNOWN moves from 7 to 8. The type runs the
     *             Colloid engine (ColloidVoice, the same union member) and the
     *             BBD chorus (BbdChorus.h) in one FxBuffer voice unit while
     *             its cho row is not off; its registry row points at the
     *             Colloid's descriptor table with 64 rows (the Colloid's 59,
     *             then the five chorus rows).
     * Why:        appended (the standing rule): no stored value changes
     *             meaning; menu_instrumentLoadIndex[] (menu.c: +2 B .bss) and
     *             every `type >= UNKNOWN` guard follow; a .hcnames header
     *             written before the addition is a leading part of the new
     *             list and is accepted. Engine-level code tests the engine
     *             (instrumentManager_typeEngine(): clx -> col), not the type.
     */
    INSTRUMENT_TYPE_CLX,
```

**D5.2 `Core/DSP/Instruments/InstrumentManager.h` l. 258–259 (after
`IM_SPECIAL_LFO_SYNC,`, before `IM_SPECIAL_WRITER_COUNT`) — ADD:**

```c
    /*
     * S099 P1: chorus_type writer (the Advanced types' cho row).
     *
     * What:       IM_SPECIAL_CHORUS_TYPE = 24 (next after IM_SPECIAL_LFO_SYNC,
     *             23); IM_SPECIAL_WRITER_COUNT becomes 25 (<= 32, the 5-bit
     *             writer field, asserted below). The writer clamps the byte to
     *             BBD_MODE_COUNT - 1, stores it in the voice, makes the slot
     *             own one FxBuffer voice unit while it is not off (none while
     *             off), binds and clears the unit on a first bind, and
     *             refreshes the slot's right-block pointer.
     * Why:        the brief: the chorus's 50 ms unit is held only while its
     *             type is selected. The row is a selector that is neither
     *             automatable, morphable nor a modulation target, so every
     *             write comes from the foreground (FxBuffer's share callback
     *             is foreground-only).
     * Inputs:     slot, byte value (0..5; larger values become dbl).
     * Outputs:    the voice's choType and choUnit; the slot's units.
     * Accessors:  instrumentManager_writeSpecialRuntime() switch.
     * Affiliates: BBD_MODE_* (BbdChorus.h); the chorus_type rows;
     *             tools/dsp_test/check_special_tags.py; the diagnostic
     *             classifier.
     */
    IM_SPECIAL_CHORUS_TYPE,
```

**D5.3 `Core/DSP/Instruments/InstrumentManager.h` after l. 1076 (the
`instrumentManager_runtimeType()` prototype) — ADD:**

```c
/*
 * The engine behind a type (S099 P1).
 *
 * What:       the type whose engine (union member, entry points, engine
 *             rules) a registry type runs: INSTRUMENT_TYPE_CLX -> COL (P-E:
 *             ALX -> ALK); every other type is its own engine; UNKNOWN and
 *             out-of-range values give UNKNOWN.
 *             instrumentManager_runtimeEngine(slot) is the same for the slot's
 *             live runtime type.
 * Why:        the Advanced types run the Basic engines unchanged; code that
 *             asks "is this a Colloid voice" (menu wave names, the MIDI
 *             note modes, the engine branches in InstrumentManager.c) tests
 *             the engine, and registry lookups keep the type.
 * Accessors:  InstrumentManager.c; menu.c menu_formatWaveCell();
 *             MidiNoteModes.c.
 */
instrument_type_t instrumentManager_typeEngine(instrument_type_t type);
instrument_type_t instrumentManager_runtimeEngine(uint8_t slot);
```

**D5.4 `Core/DSP/Instruments/InstrumentManager.h` l. 918–934 (the
`instrumentManager_slotRight()` contract) — MODIFY its "What" and "Why":**

```c
 * What:       the stereo voice's right block (32 int16, written by its
 *             sync block in the same call that fills the mixer's left
 *             buffer): the Colloid's and Advanced Colloid's voice->right, the
 *             Tetrakt's (S096 P1), and (S099 P1, P-E) the Advanced
 *             Alkaline's while its chorus runs; NULL for every other type, an
 *             Advanced Alkaline with the chorus off, and an empty slot.
 *             S099 P1: read from a per-slot pointer table set where the
 *             runtime type or the chorus's unit changes
 *             (instrumentManager_slotRightUpdate()).
 * Why:        the mixer renders one mono stack block per slot (sampleData);
 *             the right block lives inside the voice (the slot reserve), so a
 *             stereo voice costs no new RAM. noinline keeps the call out of
 *             mixer_calcNextSampleBlock()'s register allocation; the table
 *             read is shorter than the type tests it replaces.
```

**D5.5 `Core/DSP/Instruments/Colloid/ColloidVoice.h` l. 506–507 (after
`float nHSt, nLSt;`, before `} ColloidVoice;`) — ADD:**

```c
	/*
	 * The Advanced Colloid's chorus (S099 P1; DRAFT_ADVANCED_BBD_CHORUS.md
	 * §2, §7).
	 *
	 * What:       choUnit: the slot's FxBuffer voice unit while cho is not
	 *             off (the rings, guards and BbdChorusState, BbdChorus.h), else
	 *             NULL; choType cho (BBD_MODE_*), choRate crt, choDepth cdp,
	 *             choMix cmx, choSrc csr (BBD_SRC_*): the clx type's five rows,
	 *             the descriptor bytes as they are.
	 * Why:        the clx type runs this engine unchanged, then the chorus pass
	 *             (instrumentManager_calcSlotSyncBlock()). A Basic Colloid (col,
	 *             59 rows) never writes these bytes, and choUnit stays NULL
	 *             (the slot reset clears the whole member).
	 * RAM:        12 B in the 1,264 B slot reserve (1,212 -> 1,224 B, measured
	 *             at step 0.5): appended, so no existing member moves and no
	 *             loop's offsets change.
	 * Accessors:  ColloidParameters.c (rows 59..63); InstrumentManager.c
	 *             (IM_SPECIAL_CHORUS_TYPE, the clx async and sync, the quiet
	 *             test); colloidParameters_cellInactive().
	 */
	int16_t *choUnit;
	uint8_t  choType;
	uint8_t  choRate;
	uint8_t  choDepth;
	uint8_t  choMix;
	uint8_t  choSrc;
```

**D5.6 `Core/DSP/Instruments/Colloid/ColloidParameters.h`:**

l. 27 (after `#define COLLOID_PARAM_DESCRIPTOR_COUNT 59u`) — ADD:

```c
/* S099 P1: the Advanced Colloid (clx) uses the same table with 64 rows: the
 * 59 above, then cho crt cdp cmx csr (asserted against the enum). */
#define COLLOID_ADV_PARAM_DESCRIPTOR_COUNT 64u
```

l. 31 (after `#define COLLOID_INSTRUMENT_TYPE_FLAGS ...`) — ADD:

```c
/* S099 P1: clx is Advanced (at most two Advanced types per Scene,
 * instrumentManager_typeSelectableForSceneSlot()). */
#define COLLOID_ADV_INSTRUMENT_TYPE_FLAGS INSTRUMENT_FLAG_ADVANCED
```

l. 37 (after `extern const uint8_t colloid_instrument_type_flags;`) — ADD:

```c
/* S099 P1: the clx registry row's pages and label */
extern const instrument_menu_page_t colloid_adv_menu_pages[];
extern const char colloid_adv_instrument_display_label[];
```

l. 46 (after `#define COL_IDX_INTERVAL_1 12u ...`) — ADD:

```c
#define COL_IDX_CHORUS_TYPE 59u   /* S099 P1: chorus_type (clx only) */
```

Then change the `colloidParameters_cellInactive()` contract (l. 59–72):
add "S099 P1: crt and cdp unless cho is var or dbl; cmx with cho off; csr
with cho off or in a mono route" to its "What".

**D5.7 `Core/DSP/Instruments/Colloid/ColloidParameters.c`:**

l. 44–45 — ADD after `const uint8_t colloid_instrument_type_flags = ...;`:

```c
/* S099 P1: the Advanced Colloid's Load-menu label ("ColAdv", 6 cells; the
 * Morph row shows "ColAdMrp") */
const char colloid_adv_instrument_display_label[] = "ColAdv";
```

l. 106 (after the `ROW_SELECT_ONOFF` macro) — ADD:

```c
/*
 * A fixed selector with a special writer (S099 P1: chorus_type).
 *
 * What:       DTYPE_ON_OFF | (tag << 4) with INSTRUMENT_PARAM_FLAG_SELECTOR
 *             only: not automatable, not morphed, not an LFO / velocity
 *             target; the special writer stores it.
 * Why:        the chorus type claims or releases a voice unit, which only the
 *             foreground may do (FxBuffer.h); with no AUTOMATABLE flag no step
 *             lock or drain write can reach it (instrumentManager_targetValid()
 *             admits a non-morphable row for automation only with SELECTOR
 *             and AUTOMATABLE), and a Scene apply writes it through
 *             preset_applyKitVoiceSelectors(), foreground.
 */
#define ROW_SELECT_FIXED_SPECIAL(key_, cat_, long_, short_, tag_, member_, type_, special_) \
    { key_, short_, long_, cat_, (uint8_t)(DTYPE_ON_OFF | ((tag_) << 4)), \
      INSTRUMENT_PARAM_FLAG_SELECTOR, \
      MOD_NONE, BIND_SPECIAL(member_, type_, special_) }
```

l. 192–196 (the enum's end: `COL_PARAM_NOISE_PAN,` / `COL_PARAM_COUNT` /
`} colloid_param_index_t;`) — MODIFY:

```c
    COL_PARAM_NOISE_PAN,
    COL_PARAM_COUNT,
    /*
     * S099 P1 (append-only, clx only): the chorus rows. The Basic Colloid's
     * registry row stops at COL_PARAM_COUNT (59); the Advanced Colloid's at
     * COL_PARAM_ADV_COUNT (64 = INSTRUMENT_PARAM_COUNT: no row is left).
     */
    COL_PARAM_CHORUS_TYPE = COL_PARAM_COUNT,
    COL_PARAM_CHORUS_RATE,
    COL_PARAM_CHORUS_DEPTH,
    COL_PARAM_CHORUS_MIX,
    COL_PARAM_CHORUS_SOURCE,
    COL_PARAM_ADV_COUNT
} colloid_param_index_t;
```

l. 207–208 (after the `COL_IDX_INTERVAL_1` assert) — ADD:

```c
/* S099 P1 */
_Static_assert(COLLOID_ADV_PARAM_DESCRIPTOR_COUNT == COL_PARAM_ADV_COUNT,
               "COLLOID_ADV_PARAM_DESCRIPTOR_COUNT must match colloid_param_index_t");
_Static_assert(COL_PARAM_ADV_COUNT <= INSTRUMENT_PARAM_COUNT,
               "the clx rows must fit the 64-cell Kit image");
_Static_assert(COL_PARAM_CHORUS_TYPE == COL_IDX_CHORUS_TYPE,
               "COL_IDX_CHORUS_TYPE must name chorus_type");
```

l. 343–344 (after the `col_noise_pan` row, before `};`) — ADD:

```c
    /*
     * @advanced-rows: the rows below are the clx type's only (S099 P1;
     * DRAFT_ADVANCED_BBD_CHORUS.md §7.2). tools/colloid/make_init_col.py
     * writes init.col up to this marker and init.clx with every row.
     *
     * cho: off I II 1+2 var dbl (BBD_MODE_*; choTypeNames). A fixed
     *   selector: its writer claims or releases the voice unit
     *   (IM_SPECIAL_CHORUS_TYPE).
     * crt: the var / dbl LFO rate, bbdRateHz[] (0.1 .. 10 Hz, log).
     * cdp: the var / dbl depth, x / 127 of the mode's full sweep.
     * cmx: dry / wet, equal power; 0 = the input alone; 64 = both -3 dB.
     * csr: the source, L-R (each side its own channel) or mid (the
     *   Juno: one signal into both lines); an automatable selector (a csr
     *   switch needs no memory action: both rings are always written).
     */
    ROW_SELECT_FIXED_SPECIAL("chorus_type", CAT_CHORUS, LONG_TYPE, SHORT_CHO, ONOFF_TAG_CHO_TYPE, choType, TYPE_UINT8, IM_SPECIAL_CHORUS_TYPE),
    ROW("chorus_rate", CAT_CHORUS, LONG_RATE, SHORT_CRT, DTYPE_0B127, MOD_0_127, choRate, TYPE_UINT8),
    ROW("chorus_depth", CAT_CHORUS, LONG_DEPTH, SHORT_CDP, DTYPE_0B127, MOD_0_127, choDepth, TYPE_UINT8),
    ROW("chorus_mix", CAT_CHORUS, LONG_MIX, SHORT_CMX, DTYPE_0B127, MOD_0_127, choMix, TYPE_UINT8),
    ROW_SELECT_ONOFF("chorus_source", CAT_CHORUS, LONG_SOURCE, SHORT_CSR, ONOFF_TAG_CHO_SOURCE, choSrc, TYPE_UINT8),
```

l. 1–5 (includes) — ADD `#include "BbdChorus.h"     /* S099 P1: BBD_MODE_* (cellInactive) */`.

l. 350–352 (the descriptor count assert) — MODIFY:

```c
_Static_assert(sizeof(colloid_param_descriptors) / sizeof(colloid_param_descriptors[0]) ==
                   COL_PARAM_ADV_COUNT,
               "colloid_param_descriptors must have one row per index (59 col + 5 clx)");
```

`colloid_param_descriptor_count` (l. 347–348) then counts 64. Nothing
reads it (checked: no reference outside its own file). Its comment
becomes "the clx count; the col registry row uses
COLLOID_PARAM_DESCRIPTOR_COUNT".

l. 379–396 (`colloid_menu_pages[]`) — MODIFY: pull pages 0–6 into a macro
both tables share, then ADD the clx table:

```c
/* S099 P1: pages 0..6, shared by the col and clx layouts (one source) */
#define COL_PAGES_0_6 \
    /* 0 OSC */ \
    {{ COL_PARAM_OSC1_PITCH_COARSE, COL_PARAM_OSC1_PITCH_FINE, COL_PARAM_OSC1_POSITION, COL_PARAM_POS_SPREAD, COL_PARAM_WAVE, COL_PARAM_OSC1_TYPE, COL_PARAM_PENV_AMOUNT, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E }}, \
    /* 1 AEG */ \
    {{ COL_PARAM_ATTACK, COL_PARAM_DECAY, COL_PARAM_SUSTAIN_LEVEL, COL_PARAM_RELEASE, COL_PARAM_PENV_ATTACK, COL_PARAM_PENV_DECAY, COL_PARAM_PENV_SUSTAIN_LEVEL, COL_PARAM_PENV_RELEASE, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E }}, \
    /* 2 MOD */ \
    {{ COL_PARAM_VELO_MOD_DEST, COL_PARAM_VELO_MOD_AMOUNT, COL_PARAM_SUSTAIN, COL_PARAM_SLEW, COL_PARAM_NOISE_LEVEL, COL_PARAM_NOISE_TONE, COL_PARAM_NOISE_WIDTH, COL_PARAM_NOISE_PAN, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E }}, \
    /* 3 FM */ \
    {{ COL_PARAM_INTERVAL_1, COL_PARAM_INTERVAL_2, COL_PARAM_INTERVAL_3, COL_PARAM_DETUNE, COL_PARAM_SPREAD, COL_PARAM_MOTION, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E }}, \
    /* 4 click */ \
    {{ COL_PARAM_FENV_ATTACK, COL_PARAM_FENV_DECAY, COL_PARAM_FENV_SUSTAIN_LEVEL, COL_PARAM_FENV_RELEASE, COL_PARAM_FENV_AMOUNT, COL_PARAM_DRIVE, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E }}, \
    /* 5 FILTER */ \
    {{ COL_PARAM_CUTOFF, COL_PARAM_RESO, COL_PARAM_FILT_TYPE, COL_PARAM_ROUTE, COL_PARAM_CUT_OFS, COL_PARAM_RES_OFS, COL_PARAM_ENV_OFS, COL_PARAM_DRIVE_OFS, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E }}, \
    /* 6 LFO */ \
    {{ COL_PARAM_LFO_RATE, COL_PARAM_LFO_SYNC, COL_PARAM_LFO_WAVE, COL_PARAM_LFO_OFFSET, COL_PARAM_LFO_RETRIGGER_VOICE, COL_PARAM_LFO_POLARITY, COL_PARAM_LFO_AMOUNT, COL_PARAM_LFO_AMOUNT_2, COL_PARAM_LFO_TARGET_VOICE, COL_PARAM_LFO_TARGET_PARAM, COL_PARAM_LFO_TARGET_VOICE_2, COL_PARAM_LFO_TARGET_PARAM_2, COL_E, COL_E, COL_E, COL_E }},

const instrument_menu_page_t colloid_menu_pages[] = {
    COL_PAGES_0_6
    /* 7 MIX: vol pan, the tone, the overdrive (the Alkaline's cells) */
    {{ COL_PARAM_INSTRUMENT_VOL, COL_PARAM_INSTRUMENT_PAN, COL_PARAM_TONE, COL_PARAM_INSTRUMENT_DRIVE, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E }},
};

/*
 * The Advanced Colloid's layout (S099 P1; chorus draft §7.2).
 *
 * What:       the col pages 0..6, then MIX: vol pan ton drv | cho crt cdp cmx
 *             | csr. The second screen of MIX was empty, so no SELECT
 *             position moves.
 * Why:        the chorus is the last stage before the mixer, beside the
 *             post-filter tone and overdrive.
 */
const instrument_menu_page_t colloid_adv_menu_pages[] = {
    COL_PAGES_0_6
    /* 7 MIX + the chorus */
    {{ COL_PARAM_INSTRUMENT_VOL, COL_PARAM_INSTRUMENT_PAN, COL_PARAM_TONE, COL_PARAM_INSTRUMENT_DRIVE, COL_PARAM_CHORUS_TYPE, COL_PARAM_CHORUS_RATE, COL_PARAM_CHORUS_DEPTH, COL_PARAM_CHORUS_MIX, COL_PARAM_CHORUS_SOURCE, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E }},
};

_Static_assert(COLLOID_MENU_PAGE_COUNT ==
                   (sizeof(colloid_adv_menu_pages) / sizeof(colloid_adv_menu_pages[0])),
               "the clx layout has the col layout's page count");
```

The page initialisers of `colloid_menu_pages[]` do not change, only their
spelling (the macro). The Basic layout is byte-identical. Gate D-G2 checks
this with an `objdump -s` of the symbol.

l. 424–446 (`colloidParameters_cellInactive()`) — MODIFY:

```c
uint8_t colloidParameters_cellInactive(const ColloidVoice *voice,
                                       const ParamDescriptor *descriptor)
{
	/* the row's index, or no answer for a pointer outside this table (S099
	 * P1: the table now ends at the clx rows; a col slot never resolves
	 * them, its registry count is 59) */
	const ParamDescriptor *const base = colloid_param_descriptors;

	if (!voice || !descriptor || descriptor < base ||
	    descriptor >= base + COL_PARAM_ADV_COUNT)
		return 0u;
	switch ((colloid_param_index_t)(descriptor - base)) {
	case COL_PARAM_OSC1_POSITION:
	case COL_PARAM_POS_SPREAD:
		/* free-running wav oscillators: no start phase, frame or width */
		return (uint8_t)(voice->osc.oscType == OSC_TYPE_WAV);
	case COL_PARAM_SPREAD:
	case COL_PARAM_MOTION:
	case COL_PARAM_NOISE_PAN:   /* S096 P3: no pan in a mono route */
		/* a mono route (rte a filter type): positions have no effect (C-D16,
		 * S095 P3 F9) */
		return (uint8_t)(voice->route != COL_RTE_STR && voice->route < COL_RTE_COUNT);
	/* S099 P1: the chorus rows */
	case COL_PARAM_CHORUS_RATE:
	case COL_PARAM_CHORUS_DEPTH:
		/* crt and cdp set var and dbl only; the Juno modes fix both */
		return (uint8_t)(voice->choType < BBD_MODE_VAR);
	case COL_PARAM_CHORUS_MIX:
		return (uint8_t)(voice->choType == BBD_MODE_OFF);
	case COL_PARAM_CHORUS_SOURCE:
		/* no choice with the chorus off, or in a mono route (L = R: both
		 * forms play the same; the selected one still runs, constant cost) */
		return (uint8_t)(voice->choType == BBD_MODE_OFF ||
		                 (voice->route != COL_RTE_STR && voice->route < COL_RTE_COUNT));
	default:
		return 0u;
	}
}
```

l. 459–460 (the `#undef` list) — ADD `#undef ROW_SELECT_FIXED_SPECIAL` and
`#undef COL_PAGES_0_6`.

### D6. `Core/DSP/Instruments/InstrumentManager.c`

**D6.1 l. 20–21 (includes) — ADD after `#include "TetraktVoice.h"`:**

```c
#include "BbdChorus.h"            /* S099 P1: the Advanced types' chorus (unit, control, render) */
```

**D6.2 l. 102–108 (after the Tetrakt registry row) — ADD:**

```c
    /*
     * S099 P1: the Advanced Colloid. Token/extension "clx"/".clx"; directory
     * /Instrument/ColAdv (6 characters; SD_CARD/Instrument/ColAdv in the
     * card image with init.clx; filesystem.c creates the folder on a first
     * boot that lacks it). Advanced: at most two per Scene. Its rows are the
     * Colloid's table with the five chorus rows (64); the pages are the
     * Colloid's with the chorus on MIX's second screen.
     */
    [INSTRUMENT_TYPE_CLX] =
    { INSTRUMENT_TYPE_CLX, "clx", colloid_adv_instrument_display_label, ".clx", "ColAdv",
      COLLOID_ADV_INSTRUMENT_TYPE_FLAGS,
      colloid_param_descriptors, COLLOID_ADV_PARAM_DESCRIPTOR_COUNT,
      colloid_adv_menu_pages, COLLOID_MENU_PAGE_COUNT },
```

**D6.3 after l. 117 (the registry count assert) — ADD the engine map:**

```c

/*
 * The engine behind each type (S099 P1). Contract: InstrumentManager.h.
 *
 * What:       instrument_engine_of[type] = the type whose union member, entry
 *             points and engine rules the type runs; every row is its own
 *             engine except the Advanced types (clx -> col; P-E: alx -> alk).
 *             UNKNOWN maps to UNKNOWN.
 * Why:        one table, so a later Advanced type is one row here and the
 *             engine tests stay one load and one compare.
 * Accessors:  instrumentManager_typeEngine(), instrumentManager_runtimeEngine(),
 *             instrumentManager_slotEngine().
 */
static const uint8_t instrument_engine_of[INSTRUMENT_TYPE_UNKNOWN + 1u] = {
    [INSTRUMENT_TYPE_DRM] = INSTRUMENT_TYPE_DRM,
    [INSTRUMENT_TYPE_SNR] = INSTRUMENT_TYPE_SNR,
    [INSTRUMENT_TYPE_CYM] = INSTRUMENT_TYPE_CYM,
    [INSTRUMENT_TYPE_HAT] = INSTRUMENT_TYPE_HAT,
    [INSTRUMENT_TYPE_ALK] = INSTRUMENT_TYPE_ALK,
    [INSTRUMENT_TYPE_COL] = INSTRUMENT_TYPE_COL,
    [INSTRUMENT_TYPE_TKT] = INSTRUMENT_TYPE_TKT,
    [INSTRUMENT_TYPE_CLX] = INSTRUMENT_TYPE_COL,
    [INSTRUMENT_TYPE_UNKNOWN] = INSTRUMENT_TYPE_UNKNOWN,
};

instrument_type_t instrumentManager_typeEngine(const instrument_type_t type)
{
    return (type <= INSTRUMENT_TYPE_UNKNOWN)
        ? (instrument_type_t)instrument_engine_of[type] : INSTRUMENT_TYPE_UNKNOWN;
}
```

**D6.4 l. 243 (after `static InstrumentRuntimeSlot runtime_slots[...];`) — ADD:**

```c

/*
 * The slot's right block, for the mixer (S099 P1).
 *
 * What:       instrumentManager_rightBlock[slot] = the stereo voice's
 *             right[] (Colloid, Advanced Colloid, Tetrakt; P-E: the Advanced
 *             Alkaline while its chorus runs) or NULL. Set by
 *             instrumentManager_slotRightUpdate() wherever the runtime type or
 *             a chorus unit changes (runtimeInit, resetRuntimeSlotFromScene,
 *             the cho writer); instrumentManager_slotRight() reads it.
 * Why:        the Advanced Alkaline is stereo only while its chorus runs, so
 *             "stereo" is no longer a property of the type alone; one table
 *             read is also shorter than the type tests it replaces (a gain on
 *             every slot's block, measured by elf_s099_check's mixer record).
 * RAM:        24 B SRAM1 .bss (S099 P1 §10, sign-off).
 */
static const int16_t *instrumentManager_rightBlock[INSTRUMENT_SLOT_COUNT];
```

**D6.5 l. 455–483 (`instrumentManager_slotVoiceUnits()` and its comment) —
MODIFY:**

```c
/*
 * The voice units a slot's incoming image needs (S096 P1 SD11; S099 P1).
 *
 * What:       1 for a Tetrakt (its line); S099 P1: 1 for an Advanced type
 *             whose stored chorus_type (the Normal image, where selectors live)
 *             is not off (its chorus); 0 for every other type and an empty
 *             slot.
 * Why:        the reset claims what the incoming sound will use, so a Scene
 *             change into an Advanced slot with its chorus on reseats the
 *             Effect once (here), not twice (a release at the reset and a
 *             claim at the cho apply).
 * Inputs:     incoming (the Scene's slot, or NULL).
 * Accessors:  instrumentManager_runtimeInit(),
 *             instrumentManager_resetRuntimeSlotFromScene().
 */
static uint8_t instrumentManager_unitsWanted(const kit_instrument_slot_t *incoming)
{
    if (!incoming)
        return 0u;
    switch (incoming->type) {
    case INSTRUMENT_TYPE_TKT:
        return 1u;
    case INSTRUMENT_TYPE_CLX:
        return (uint8_t)(incoming->parameter_images.instrument_parameters[COL_IDX_CHORUS_TYPE]
                         != BBD_MODE_OFF);
    default:
        return 0u;
    }
}

/*
 * Make the slot own exactly `wanted` voice units (S096 P1 SD11; S099 P1).
 *
 * What:       a slot that already owns the right count keeps its unit, so a
 *             Tetrakt -> Tetrakt reset (or clx -> clx with the chorus on in
 *             both) does not release and reclaim (no Effect reseat). Any other
 *             change releases first, then claims (fxbuf_voiceAcquire()
 *             notifies the Effect through the share callback, foreground
 *             only). Returns the first unit or NULL.
 * Why:        the FxBuffer contract: units belong to the slot's runtime. A
 *             Tetrakt's unit is claimed at type assignment; an Advanced type's
 *             follows its chorus_type row (Q-8), also at type assignment here
 *             and then at every cho write (instrumentManager_chorusBind()).
 *             With six Tetrakts, or four plus two Advanced choruses, six of
 *             the twelve units are owned.
 * Inputs:     slot, wanted (0 or 1).
 * Outputs:    the unit's samples or NULL.
 * Accessors:  instrumentManager_runtimeInit(),
 *             instrumentManager_resetRuntimeSlotFromScene().
 */
static int16_t *instrumentManager_slotVoiceUnits(uint8_t slot, uint8_t wanted)
{
    if (fxbuf_voiceUnitCount(slot) != wanted) {
        fxbuf_voiceRelease(slot);
        if (wanted)
            (void)fxbuf_voiceAcquire(slot, wanted);
    }
    return wanted ? fxbuf_voiceUnit(slot, 0u) : 0;
}

/*
 * Refresh the slot's right-block pointer (S099 P1). Contract: the table
 * above (instrumentManager_rightBlock[]).
 */
static void instrumentManager_slotRightUpdate(uint8_t slot)
{
    const int16_t *right = 0;

    if (slot >= INSTRUMENT_SLOT_COUNT)
        return;
    switch (runtime_slot_type[slot]) {
    case INSTRUMENT_TYPE_COL:
    case INSTRUMENT_TYPE_CLX:
        right = runtime_slots[slot].colloid.right;
        break;
    case INSTRUMENT_TYPE_TKT:
        right = runtime_slots[slot].tetrakt.right;
        break;
    default:
        break;
    }
    instrumentManager_rightBlock[slot] = right;
}
```

`instrumentManager_slotRightUpdate()` reads `runtime_slot_type[]` (l. 217)
and `runtime_slots[]` (l. 243) directly, so it can sit here, before
`instrumentManager_slotType()`'s definition (l. 3060).

**D6.6 l. 1741–1750 (`instrumentManager_runtimeInstance()`) — ADD a case
after l. 1747:**

```c
    case INSTRUMENT_TYPE_CLX: return instrumentManager_colloidRuntime(slot);   /* S099 P1 */
```

**D6.7 l. 1771–1773 (`instrumentManager_runtimeLfo()`, the COL case) —
MODIFY:** add the label `case INSTRUMENT_TYPE_CLX:   /* S099 P1: the
Colloid engine */` directly above `case INSTRUMENT_TYPE_COL:`. The two
labels share the body.

**D6.8 l. 1816–1866 (`instrumentManager_runtimeInit()`) — MODIFY:**
- in the switch, the COL case (l. 1850–1852) becomes:

```c
        case INSTRUMENT_TYPE_CLX:   /* S099 P1: the Colloid engine; its chorus
                                     * unit is claimed here when the resident
                                     * image's cho is on, and bound at the boot
                                     * apply's cho write */
            (void)instrumentManager_slotVoiceUnits(slot, instrumentManager_unitsWanted(instrument));
            /* fall through */
        case INSTRUMENT_TYPE_COL:   /* S095 P2 */
            Colloid_initVoice(instrumentManager_colloidRuntime(slot));
            break;
```

- the TKT case (l. 1860–1861) passes `instrumentManager_slotVoiceUnits(slot, 1u)`;
- after the switch's closing `}` (l. 1865), still inside the slot loop, ADD:

```c
        /* S099 P1: the mixer's right-block pointer for the new type */
        instrumentManager_slotRightUpdate(slot);
```

**D6.9 l. 1052–1102 (`instrumentManager_waveDomainMax()`) — MODIFY the
two type tests to engine tests:**

```c
            if (osc && instrumentManager_typeEngine(instrumentManager_slotType(slot))
                           == INSTRUMENT_TYPE_ALK) {
```

(the Alkaline branch written in B5.1), and l. 1083:

```c
            if (osc && instrumentManager_typeEngine(instrumentManager_slotType(slot))
                           == INSTRUMENT_TYPE_COL) {
```

**D6.10 l. 1869–1916 (`instrumentManager_ampEnvelopeQuiet()`) — MODIFY
l. 1889 and l. 1901–1909:**

```c
    if (instrumentManager_typeEngine(instrumentManager_slotType(slot)) == INSTRUMENT_TYPE_ALK) {
```

```c
    /*
     * S095 P2: the Colloid has no SlopeEg2 either; it is quiet when its amp
     * envelope is idle (Colloid_isQuiet(): the release fell below 1e-4, this
     * function's threshold). S099 P1: an Advanced Colloid's chorus still
     * sounds after the envelope idles (up to 990 samples in its ring); it is
     * quiet once the chorus has seen BBD_TAIL_BLOCKS quiet blocks
     * (bbdChorus_tailDone()), so a deferred Scene commit does not cut the wet
     * tail (chorus draft §8). A Basic Colloid's choUnit is always NULL.
     */
    if (instrumentManager_typeEngine(instrumentManager_slotType(slot)) == INSTRUMENT_TYPE_COL) {
        const ColloidVoice *voice = instrumentManager_colloidRuntime(slot);

        if (!voice)
            return 1u;
        return (uint8_t)(Colloid_isQuiet(voice) &&
                         (!voice->choUnit || bbdChorus_tailDone(voice->choUnit)));
    }
```

**D6.11 l. 2137–2190 (`instrumentManager_resetRuntimeSlotFromScene()`) —
MODIFY:**

l. 2148–2161 (the S096 P1 SD11 block):

```c
    /*
     * S096 P1 SD11: the slot owns exactly the voice units its incoming type
     * uses (a Tetrakt one; S099 P1: an Advanced type one while its stored cho
     * is on; every other type none). This is the only tagged-member
     * replacement point, so it is the only place units change owner after
     * boot apart from the cho writer. Foreground (the Preset activation
     * worker, between render blocks), as the FxBuffer share callback
     * requires.
     */
    {
        int16_t *const line =
            instrumentManager_slotVoiceUnits(slot, instrumentManager_unitsWanted(incoming));

        if (instrumentManager_slotType(slot) == INSTRUMENT_TYPE_TKT)
            Tetrakt_initVoice(instrumentManager_tetraktRuntime(slot), slot, line);
    }
```

l. 2178–2180 (the COL case) — add `case INSTRUMENT_TYPE_CLX:   /* S099 P1:
the Colloid engine (the chorus binds at its cho apply) */` above
`case INSTRUMENT_TYPE_COL:`.

l. 2189 (after `instrumentManager_overlayClearSlot(slot);`) — ADD:

```c
    /* S099 P1: the mixer's right-block pointer for the incoming type */
    instrumentManager_slotRightUpdate(slot);
```

**D6.12 l. 2301 (`instrumentManager_runtimePan()`) and l. 2339
(`instrumentManager_runtimeVolume()`) — ADD** `case INSTRUMENT_TYPE_CLX:`
above each `case INSTRUMENT_TYPE_COL:`, with a `/* S099 P1 */` comment.

**D6.13 l. 2530–2595 (`instrumentManager_calcSlotAsync()`):**

l. 2566 — MODIFY:

```c
        if (instrumentManager_typeEngine(instrumentManager_slotType(slot)) == INSTRUMENT_TYPE_COL)
```

l. 2589 (before `case INSTRUMENT_TYPE_TKT:`) — ADD:

```c
    case INSTRUMENT_TYPE_CLX: {
        /*
         * S099 P1: the Advanced Colloid: the Colloid's control, then the
         * chorus's (the LFO, the sweep, the read plans, the coefficients and
         * gains, the quiet count; bbdChorus_control(), noinline, flash). With
         * cho off there is no unit and no chorus work: a mode (constant CPU);
         * the slot is budgeted for chorus on, L-R. The Colloid has no cbw,
         * cdv or cwd row: its wet cutoff is BBD_COL_TONE (9 kHz, by ear), no
         * BBD drive, the Juno's inverted sweep (width 127).
         */
        ColloidVoice *const col = instrumentManager_colloidRuntime(slot);

        Colloid_calcVoiceAsync(col);
        if (col->choUnit)
            bbdChorus_control(col->choUnit, col->choType, col->choRate, col->choDepth,
                              col->choMix, BBD_COL_TONE, 0u, 127u, Colloid_isQuiet(col));
        break; }
```

**D6.14 l. 2597–2667 (`instrumentManager_calcSlotSyncBlock()`) — ADD
before `case INSTRUMENT_TYPE_TKT:` (l. 2651):**

```c
    case INSTRUMENT_TYPE_CLX: {
        /*
         * S099 P1: the Advanced Colloid: the Colloid's block (left into buf,
         * right into voice->right), then the chorus pass in place on both
         * (bbdChorus_renderColloid(): csr L-R two side passes, csr mid the
         * mid form; DRAFT_ADVANCED_BBD_CHORUS.md §2). Pre-volume and pre-pan,
         * as the mixer expects; the mixer's stereo path reads right through
         * instrumentManager_slotRight() as for a Colloid.
         */
        ColloidVoice *const col = instrumentManager_colloidRuntime(slot);

        Colloid_calcVoiceSyncBlock(col, buf, size);
        if (col->choUnit)
            bbdChorus_renderColloid(col->choUnit, buf, col->right, col->choSrc);
        break; }
```

**D6.15 l. 2690–2705 (`instrumentManager_slotRight()`) — MODIFY the body:**

```c
/*
 * The slot's right output block (S095 P2; S099 P1: the pointer table).
 * Contract: InstrumentManager.h.
 */
const int16_t *instrumentManager_slotRight(uint8_t slot)
{
    return (slot < INSTRUMENT_SLOT_COUNT) ? instrumentManager_rightBlock[slot] : 0;
}
```

**D6.16 l. 2711–2719 (`instrumentManager_cellInactive()`) — MODIFY l. 2715:**

```c
    if (!descriptor ||
        instrumentManager_typeEngine(instrumentManager_slotType(slot)) != INSTRUMENT_TYPE_COL)
        return 0u;
```

(`colloidParameters_cellInactive()` now covers the clx rows, D5.7.)

**D6.17 l. 2849 (`instrumentManager_triggerTrack()`, the COL case) — ADD**
`case INSTRUMENT_TYPE_CLX:   /* S099 P1 */` above `case INSTRUMENT_TYPE_COL:`.

**D6.18 the Colloid-only hooks — MODIFY each `== INSTRUMENT_TYPE_COL` /
`!= INSTRUMENT_TYPE_COL` test to the engine test:**
- l. 2910 (`instrumentManager_requestGateDuck()`); this one runs in TIM3
  context, where a byte read and a table load are safe;
- l. 2971 (`renewGate`);
- l. 2985 (`changeArm`);
- l. 2998 (`changeDecide`);
- l. 3008 (`setMidiHold`);
- l. 3018 (`automationWritten`);
- l. 3125 (`instrumentManager_osc()`).

Each becomes `instrumentManager_typeEngine(instrumentManager_slotType(slot))
== INSTRUMENT_TYPE_COL` (or `!=`). l. 3120 (`instrumentManager_osc()`, ALK)
becomes the ALK engine test. Add one comment above l. 2924's block: "S099
P1: every Colloid-only hook acts on the Colloid engine, so the Advanced
Colloid (clx) shares the note gate, the change rule and the MIDI hold."

**D6.19 l. 3181 (`instrumentManager_oscBySelector()`) — ADD**
`case INSTRUMENT_TYPE_CLX:   /* S099 P1 */` above `case INSTRUMENT_TYPE_COL:`.

**D6.20 l. 4603, 4614, 4697, 4702 (`instrumentManager_writeSpecialRuntime()`,
the INSTRUMENT_DRIVE and OSC_TYPE branches) — MODIFY each type test to the
engine test**, e.g.:

```c
        if (instrumentManager_typeEngine(instrumentManager_slotType(slot)) == INSTRUMENT_TYPE_ALK) {
```

**D6.21 before l. 4440 (`instrumentManager_writeSpecialRuntime()`) — ADD
the chorus writer:**

```c
/*
 * Bind or release the slot's chorus unit (S099 P1).
 *
 * What:       mode off: release the slot's unit (fxbuf_voiceRelease(): the
 *             share callback may grow the Effect's share) and clear the
 *             voice's pointer. Any other mode: claim one unit if the slot owns
 *             none (fxbuf_voiceAcquire(), which cannot fail: at most one unit
 *             per slot, twelve for six slots); then, if the unit differs from
 *             the voice's pointer (a new claim, or the first bind after a slot
 *             reset left the pointer NULL), clear and seed it
 *             (bbdChorus_init()) and store the pointer. A change between two
 *             modes that are both on keeps the unit and its rings (the
 *             chorus's centre and depth slew to the new mode).
 * Why:        the brief: the unit is held while the effect type is selected
 *             and released when it is not.
 * Inputs:     slot, mode (clamped), unit (&voice->choUnit), mono (1 for the
 *             Alkaline's one-ring form).
 * Context:    foreground only (the FxBuffer share callback).
 * Accessors:  instrumentManager_writeChorusType().
 */
static void instrumentManager_chorusBind(uint8_t slot, uint8_t mode, int16_t **unit,
                                         uint8_t mono)
{
    if (mode == BBD_MODE_OFF) {
        if (fxbuf_voiceUnitCount(slot))
            fxbuf_voiceRelease(slot);
        *unit = 0;
        return;
    }
    if (!fxbuf_voiceUnitCount(slot))
        (void)fxbuf_voiceAcquire(slot, 1u);
    {
        int16_t *const u = fxbuf_voiceUnit(slot, 0u);

        if (u && u != *unit)
            bbdChorus_init(u, mono, mode);
        *unit = u;
    }
}

/*
 * The chorus_type writer (S099 P1, Q-8). Contract: IM_SPECIAL_CHORUS_TYPE
 * (InstrumentManager.h).
 *
 * What:       clamps the byte to BBD_MODE_COUNT - 1, stores it in the slot's
 *             Advanced voice, binds or releases the chorus unit, then refreshes
 *             the slot's right-block pointer. A slot whose live type has no
 *             chorus row returns 0 (the generic store then has no member to
 *             write: such a slot never resolves the row; defensive).
 * Why:        this changes the S096 P1 rule "units are claimed at type
 *             assignment, never at a row change" for this row only. It is safe
 *             because the row is a fixed selector (ROW_SELECT_FIXED_SPECIAL:
 *             no AUTOMATABLE, MORPHABLE or MODULATABLE flag), so its writes
 *             come from a menu edit, a MIDI write (a menu edit, Q1), an
 *             Instrument Load or a Scene apply (preset_applyKitVoiceSelectors()):
 *             all foreground. A cho toggle during play reseats the bus
 *             Effect, the path a Tetrakt type assignment already takes.
 * Inputs:     slot, value (the descriptor byte).
 * Outputs:    choType, choUnit; the slot's units; the right-block pointer.
 * Accessors:  instrumentManager_writeSpecialRuntime() (IM_SPECIAL_CHORUS_TYPE).
 */
static uint8_t instrumentManager_writeChorusType(uint8_t slot, uint8_t value)
{
    const uint8_t mode = (value >= BBD_MODE_COUNT) ? (uint8_t)(BBD_MODE_COUNT - 1u) : value;

    switch (instrumentManager_slotType(slot)) {
    case INSTRUMENT_TYPE_CLX: {
        ColloidVoice *const col = instrumentManager_colloidRuntime(slot);

        col->choType = mode;
        instrumentManager_chorusBind(slot, mode, &col->choUnit, 0u);
        break; }
    default:
        return 0u;
    }
    instrumentManager_slotRightUpdate(slot);
    return 1u;
}
```

**D6.22 l. 4676–4677 (after the `IM_SPECIAL_LFO_SYNC` case's `return 1u; }`)
— ADD:**

```c
    case IM_SPECIAL_CHORUS_TYPE:
        /* S099 P1: the Advanced types' cho row (claims / releases the unit) */
        return instrumentManager_writeChorusType(slot, byteValue);
```

**D6.23 l. 4874 (`instrumentManager_classifySpecialKey()`, DEV_MODE_DIAGNOSTIC
only) — ADD after the `lfo_sync` line:**

```c
    /* S099 P1: the Advanced types' chorus type claims its unit through
     * IM_SPECIAL_CHORUS_TYPE */
    if (strcmp(key, "chorus_type") == 0) return IM_SPECIAL_CHORUS_TYPE;
```

**D6.24 after l. 3086 (`instrumentManager_runtimeType()`) — ADD:**

```c

instrument_type_t instrumentManager_runtimeEngine(uint8_t slot)
{
    /* S099 P1: the live runtime's engine (contract in InstrumentManager.h) */
    return instrumentManager_typeEngine(instrumentManager_slotType(slot));
}
```

**D6.25 the comments that state the old rule — MODIFY** (no code):
- l. 238–240 (the union's member comments): "ColloidVoice colloid; /*
  S095 P3: 1,112 B ... */" becomes "1,224 B since S099 P1 (measured; the clx
  chorus bytes)".
- l. 258–262: the assert comment likewise.
- l. 465–467: "claimed at type assignment, never at a row change (plan
  §10)" is in D6.5 already.

### D7. Menu, labels and tags

**D7.1 `Core/Menu/menu.h` l. 212 (`CAT_PLATE,`) — MODIFY / ADD:**

```c
    CAT_PLATE,
    /* S099 P1: the Advanced types' chorus rows ("Chorus") */
    CAT_CHORUS,
```

l. 279 (`LONG_BLEND,`) — ADD after it:

```c
    /* S099 P1: the chorus rows (Depth, Source) */
    LONG_DEPTH, LONG_SOURCE,
```

l. 349 (`SHORT_INT, SHORT_E_TO_R, SHORT_BLN,`) — ADD after it:

```c
    /* S099 P1: the chorus rows: cho crt cdp cmx csr (clx, alx) */
    SHORT_CHO, SHORT_CRT, SHORT_CDP, SHORT_CMX, SHORT_CSR,
```

The pools stay under 256 (`MenuLabels.c` asserts it): shorts 224 → 229,
longs 178 → 180, categories 48 → 49 (counted from the enums in this tree).

**D7.2 `Core/Menu/MenuLabels.c` — ADD at the same positions:**
- l. 76: append `{"cho"},{"crt"},{"cdp"},{"cmx"},{"csr"},` after `{"bln"},`;
- l. 117: append `{"Chorus"},` after `{"Plate"},`;
- l. 177: append `{"Depth"},{"Source"},` after `{"Blend"},`.

Add a `/* S099 P1: the chorus rows */` comment on each line.

**D7.3 `Core/Menu/MenuText.h` after l. 360 (the closing `};` of
`tktLayerNames`) — ADD:**

```c
/*
 * DTYPE_ON_OFF tags for the Advanced types' chorus (S099 P1).
 *
 * What:       ONOFF_TAG_CHO_TYPE 9 (cho, choTypeNames), ONOFF_TAG_CHO_SOURCE
 *             10 (csr, choSrcNames). The rows' dtype byte is DTYPE_ON_OFF |
 *             (tag << 4). Tags 1-3, 5-8 stay; 4 and 11-15 are free.
 * Why:        every DTYPE_MENU id is taken (S078); menu_onoffTagNames()
 *             resolves the table per tag, so display, clamp, automation range
 *             and the step-edit line follow the count.
 * Accessors:  ColloidParameters.c, AlkalineParameters.c (the chorus rows);
 *             menu.c menu_onoffTagNames().
 * Affiliates: BBD_MODE_COUNT, BBD_SRC_COUNT (BbdChorus.h): asserted in menu.c.
 */
#define ONOFF_TAG_CHO_TYPE         9u
#define ONOFF_TAG_CHO_SOURCE       10u
/*
 * The chorus name tables (S099 P1). [0] = count, then 3-character names.
 * choTypeNames  cho: off, I, II, 1+2 (the Juno-60's three modes), var (the
 *               Alpha Juno's variable rate and depth), dbl (the doubler).
 * choSrcNames   csr: L-R (each side its own channel), mid (one signal into
 *               both lines, the Juno).
 */
static const char choTypeNames[][4] = {
    {6}, {"off"},{"I  "},{"II "},{"1+2"},{"var"},{"dbl"},
};
static const char choSrcNames[][4] = {
    {2}, {"L-R"},{"mid"},
};
```

**D7.4 `Core/Menu/menu.c` l. 12759–12760 (`menu_onoffTagNames()`) — ADD
after the `ONOFF_TAG_TKT_LAYERS` case:**

```c
    case ONOFF_TAG_CHO_TYPE:       return choTypeNames;       /* S099 P1 */
    case ONOFF_TAG_CHO_SOURCE:     return choSrcNames;        /* S099 P1 */
```

**D7.5 `Core/Menu/menu.c` after l. 12927 (the `tktLayerNames` assert) — ADD:**

```c
/* S099 P1: the chorus lists name every stored value (BbdChorus.h). */
_Static_assert((sizeof(choTypeNames) / sizeof(choTypeNames[0])) - 1u == BBD_MODE_COUNT,
               "choTypeNames must list off I II 1+2 var dbl");
_Static_assert((sizeof(choSrcNames) / sizeof(choSrcNames[0])) - 1u == BBD_SRC_COUNT,
               "choSrcNames must list L-R and mid");
```

Also add `#include "BbdChorus.h"   /* S099 P1: BBD_MODE_COUNT, BBD_SRC_COUNT (the name asserts) */`
after l. 68 (`#include "ColloidVoice.h"`).

**D7.6 `Core/Menu/menu.c` l. 12510, 12539 and the C5.2 branch — MODIFY**
each `instrumentManager_runtimeType(cell->slot) == INSTRUMENT_TYPE_ALK` /
`_COL` to `instrumentManager_runtimeEngine(cell->slot) == ...`. The
Advanced types then name their waves like their Basic engines.

### D8. MIDI note modes and storage defines

**D8.1 `Core/MIDI/MidiNoteModes.c` l. 116, 594, 613 — MODIFY** each
`type == (uint8_t)INSTRUMENT_TYPE_COL` / `midiNoteMode_slotType(scene, slot)
== / != (uint8_t)INSTRUMENT_TYPE_COL` to the engine test, e.g.:

```c
    if (instrumentManager_typeEngine((instrument_type_t)type) == INSTRUMENT_TYPE_COL &&
```

Add a comment at l. 113: "S099 P1: the Colloid engine, so the Advanced
Colloid is paraphonic too (it shares the Colloid's rows and indices:
COL_IDX_*)". MidiNoteModes.c already includes InstrumentManager.h, since
it calls `instrumentManager_setMidiHold()`.

**D8.2 `Core/Hardware/SD/storageTypes.h` l. 144 — ADD after it:**

```c
#define STORAGE_INSTRUMENT_CLX     INSTRUMENT_TYPE_CLX   /* S099 P1 */
```

### D9. Checker, init writer and the card image

**D9.1 `tools/dsp_test/check_special_tags.py`:**
- l. 39 (after `'TKT': ...`) — ADD:

```python
    # S099 P1: the Advanced Colloid shares the Colloid's table (its 59 rows,
    # then the five chorus rows); chorus_type is IM_SPECIAL_CHORUS_TYPE, the
    # other chorus keys NONE.
    'CLX': 'Core/DSP/Instruments/Colloid/ColloidParameters.c',
```

- l. 80–84 (the `simple` dict) — ADD the entry
  `'chorus_type': 'CHORUS_TYPE',` with the comment
  `# S099 P1: the chorus type claims its voice unit (IM_SPECIAL_CHORUS_TYPE)`.

**D9.2 `tools/colloid/make_init_col.py` — MODIFY:**
- l. 37 (after `OUT = ...`) ADD
  `OUT_ADV = os.path.join(ROOT, "SD_CARD", "Instrument", "ColAdv", "init.clx")`;
- after l. 81 (the end of `INIT`) ADD:

```python
# S099 P1: the clx type's rows (after the @advanced-rows marker): cho I
# (the Juno-60's mode I), crt 40 / cdp 127 (var and dbl only), cmx 64 (dry and
# wet at -3 dB each), csr L-R; by ear in the D-G6 listening
INIT_ADV = {
    "chorus_type": "1", "chorus_rate": "40", "chorus_depth": "127",
    "chorus_mix": "64", "chorus_source": "0",
}
MARKER = "@advanced-rows"
```

- `rows()` (l. 83–86) — MODIFY to return `(basic, advanced)`, splitting at
  the marker line:

```python
def rows():
    """((macro, key) of the Basic rows, (macro, key) of the clx rows) in
    table order; the clx rows follow the @advanced-rows marker (S099 P1)."""
    basic, adv, seen = [], [], False
    with open(TABLE) as fh:
        for line in fh:
            if MARKER in line:
                seen = True
                continue
            m = ROW.match(line)
            if m:
                (adv if seen else basic).append((m.group(1), m.group(2)))
    return basic, adv
```

- `text(r)` gains a `typ` argument (`"col"` / `"clx"`) and an `init` dict;
- `main()` checks and writes both files:
  - init.col from the Basic rows and INIT, which must be byte-identical to
    today's file;
  - init.clx from every row and `{**INIT, **INIT_ADV}`;
  - the missing/extra key test runs per file.
- Add a docstring paragraph for S099 P1.

**D9.3** Run `python3 tools/colloid/make_init_col.py`. It writes
`SD_CARD/Instrument/ColAdv/init.clx`, and `init.col` stays unchanged (check
with `git diff --exit-code SD_CARD/Instrument/Colloid/init.col`). The
`special_tags` target (`tools/dsp_test/Makefile` l. 398) runs `--check`,
which now covers both files.

**D9.4 `tools/diagnostic_tools/decode_devlogs.py` l. 1124 (the cycle
probe's type names) — MODIFY:**

```python
            # S096 P1: the Colloid (5) and the Tetrakt (6) are named too
            # (the probe's type field is InstrumentManager.h's enum);
            # S099 P1: the Advanced Colloid (7) and Advanced Alkaline (8)
            types = {0: "drm", 1: "snr", 2: "cym", 3: "hat", 4: "alk", 5: "col", 6: "tkt",
                     7: "clx", 8: "alx"}
```

Why:
- the probe records `slot | instrumentManager_runtimeType(slot) << 3`
  (mixer.c l. 1237). `runtimeType()` keeps returning the registry type
  (deviation §0.3 item 11), so the D-G5 and E-G4 records name the Advanced
  slots;
- 8 << 3 = 64 still fits the flags byte;
- `alx` (8) only exists from P-E, but naming it here keeps the decoder one
  edit.

### D10. Host laws

**D10.1 `tools/dsp_test/modules/chorus.c` — NEW.** It builds the real
`BbdChorus.c` and `BbdChorusTables.c`, both extracted with the host's
`FxBuffer.h` constants. Laws:

```c
/*
 * modules/chorus.c - host laws for the BBD chorus (S099 P1).
 *
 * What:       builds the real BbdChorus.c and BbdChorusTables.c (extracted)
 *             and checks:
 *               CH-TAP   bbd_tap() against (1 - f) r[i] + f r[i + 1] in
 *                        float, every Q14 fraction at a fixed index: within
 *                        1 LSB of the x 2 scale;
 *               CH-GUARD 1,000 blocks of random input at delays swept over
 *                        65 .. 990 (wraps at every block position): every tap
 *                        equals a reference circular buffer read with exact
 *                        modulo indexing (no guard); break: no guard refresh;
 *               CH-LAW   the control: every block's rp equals (w << 16) - the
 *                        previous block's end delay exactly; with width 127
 *                        R's delay is 2c - L's; over one LFO cycle the delay's
 *                        extremes are the mode table's (within 0.5 sample)
 *                        and the pitch swing is +-6.5 / +-11.0 / +-13.4 cents
 *                        for I / II / 1+2 (within 5 %, printed);
 *               CH-MIX0  cmx 0: both forms' output equals the input exactly,
 *                        every sample; break: gn's sign;
 *               CH-SLEW  a cdp step 0 -> 127 under var moves the delay by at
 *                        most 1/8 of the step plus one LFO step per block; a
 *                        cho change I -> dbl moves the centre <= 16 samples a
 *                        block;
 *               CH-SRC   both csr forms leave both rings holding the selected
 *                        source's last 1,024 pre-pole samples (a reference
 *                        pre pole run on L, R or the mid); a switch every
 *                        block on a steady input equals the reference whose
 *                        ring input changes source at that block; break: no
 *                        ring-R mirror store in pass A (mid -> L-R fails);
 *               CH-ALK   the Alkaline form: right[] holds the dry before pass
 *                        B; at cdv 0 the ring holds p >> 1 exactly; at cdv
 *                        127 a full-scale input clips the ring at int16; the
 *                        12 dB wet attenuates a 10 kHz sine at least 4 dB more
 *                        than one post pole at the same coefficient;
 *               CH-INIT  bbdChorus_init() clears all 4,416 bytes and sets the
 *                        sides' rings (mono: both on ring L).
 * Inputs:     generated fragments (Makefile target chorus).
 * Outputs:    failure counts per group; exit 1 on any failure.
 * Accessors:  make -C tools/dsp_test chorus.
 * Affiliates: BbdChorus.c; tools/chorus/gen_chorus_tables.py.
 */
```

The laws above are the specification. Their bodies use the module's
`fail()` pattern and restate each formula independently, as
`modules/colloid.c` does. The Makefile target:

```make
# S099 P1: the BBD chorus (laws CH-TAP .. CH-INIT) and three breaks.
chorus: | $(GEN)
	$(EX) --src $(ROOT)/Core/DSP/Instruments/Chorus/BbdChorus.h --body --out $(GEN)/BbdChorus.host.h
	$(EX) --src $(ROOT)/Core/DSP/Instruments/Chorus/BbdChorus.c --body --out $(GEN)/BbdChorus.host.c
	$(EX) --src $(ROOT)/Core/DSP/Instruments/Chorus/BbdChorusTables.h --body --out $(GEN)/BbdChorusTables.host.h
	$(EX) --src $(ROOT)/Core/DSP/Instruments/Chorus/BbdChorusTables.c --body --out $(GEN)/BbdChorusTables.host.c
	$(HOSTCC) $(HOSTFLAGS) -I. modules/chorus.c -o build/chorus -lm
	./build/chorus
	mkdir -p build/break_cho_guard build/break_cho_mirror build/break_cho_mix
	sed 's/r\[BBD_RING\] = r\[0\];/(void)0;/' $(GEN)/BbdChorus.host.c > build/break_cho_guard/BbdChorus.host.c
	@grep -q '(void)0;' build/break_cho_guard/BbdChorus.host.c || { echo "chorus_break_guard: sed did not apply"; exit 1; }
	$(HOSTCC) -Ibuild/break_cho_guard $(HOSTFLAGS) -I. modules/chorus.c -o build/chorus_break_guard -lm
	@if ./build/chorus_break_guard >/dev/null 2>&1; then echo "chorus_break_guard: FAILED"; exit 1; \
	else echo "chorus_break_guard: caught (law CH-GUARD fails as expected)"; fi
	sed 's/ringR\[w & BBD_RMASK\] = q;/(void)ringR;/' $(GEN)/BbdChorus.host.c > build/break_cho_mirror/BbdChorus.host.c
	@grep -q '(void)ringR;' build/break_cho_mirror/BbdChorus.host.c || { echo "chorus_break_mirror: sed did not apply"; exit 1; }
	$(HOSTCC) -Ibuild/break_cho_mirror $(HOSTFLAGS) -I. modules/chorus.c -o build/chorus_break_mirror -lm
	@if ./build/chorus_break_mirror >/dev/null 2>&1; then echo "chorus_break_mirror: FAILED"; exit 1; \
	else echo "chorus_break_mirror: caught (law CH-SRC fails as expected)"; fi
	sed 's/bbd_smlawb(x2, gk, xi)/bbd_smlawb(-x2, gk, xi)/' $(GEN)/BbdChorus.host.c > build/break_cho_mix/BbdChorus.host.c
	@grep -q 'bbd_smlawb(-x2, gk, xi)' build/break_cho_mix/BbdChorus.host.c || { echo "chorus_break_mix: sed did not apply"; exit 1; }
	$(HOSTCC) -Ibuild/break_cho_mix $(HOSTFLAGS) -I. modules/chorus.c -o build/chorus_break_mix -lm
	@if ./build/chorus_break_mix >/dev/null 2>&1; then echo "chorus_break_mix: FAILED"; exit 1; \
	else echo "chorus_break_mix: caught (law CH-MIX0 fails as expected; at cmx 64)"; fi

chorus_tables:
	cd $(ROOT) && python3 tools/chorus/gen_chorus_tables.py --check
```

Add `chorus chorus_tables` to `.PHONY` and `all` (l. 55–66).

Notes on the breaks:
- The mix break flips the dry term's sign. At cmx 0 `gn` is 0, so CH-MIX0
  must also run at cmx 64, where the dry gain is 0.707. That test compares
  the chorus output with a restated `x·gd + wet·gw` (and so does CH-SRC's
  reference), and the break fails it.
- `extract.py --body` drops every quoted `#include` and keeps the
  angle-bracket ones (`<math.h>`, `<string.h>`). The host prelude gives
  `OUTPUT_DMA_SIZE` (prelude.h l. 28). So `modules/chorus.c`:
  - defines `REAL_FS` as `osc_host.h` l. 31 does;
  - defines `FXBUF_VOICE_UNIT_SAMPLES 2208u` and `FXBUF_VOICE_UNIT_BYTES
    (FXBUF_VOICE_UNIT_SAMPLES * 2u)`, the values of `FxBuffer.h`;
  - then includes, in order, `BbdChorusTables.host.h`,
    `BbdChorus.host.h`, `BbdChorusTables.host.c` and `BbdChorus.host.c`.
  The `static` loops are then visible to the laws in the same translation
  unit.

**D10.2 `tools/dsp_test/modules/instrument_manager.c` — ADD law 9; and
`tools/dsp_test/check_special_tags.py` — ADD a macro check.**

The module already compiles the real `instrumentManager_targetValid()`
(Makefile l. 296–297) over a stub `instrumentManager_descriptor()`
(modules/instrument_manager.c l. 174–179: index 0 the typ row, any other
the plain row). Law 9 adds two descriptors with the chorus rows' flags,
returned for indices 2 and 3, so it needs no Colloid struct and no extract.

At l. 174–179 (the descriptor stub) — MODIFY:

```c
/* S099 P1 law 9: the chorus rows' flags (chorus_type a fixed selector,
 * chorus_source an automatable one); every other field zero, as the plain
 * row */
static const ParamDescriptor g_cho_fixed = { .flags = INSTRUMENT_PARAM_FLAG_SELECTOR };
static const ParamDescriptor g_cho_onoff = {
    .flags = (uint8_t)(INSTRUMENT_PARAM_FLAG_AUTOMATABLE | INSTRUMENT_PARAM_FLAG_SELECTOR) };

const ParamDescriptor *instrumentManager_descriptor(instrument_type_t type,
                                                    uint8_t index)
{
    (void)type;
    return index == 0u ? &g_typ_row
         : (index == 2u ? &g_cho_fixed : (index == 3u ? &g_cho_onoff : &g_plain_row));
}
```

In `main()`, after law 4 — ADD:

```c
    /*
     * 9 (S099 P1): the gate keeps a fixed selector out of automation and
     * modulation (chorus_type, whose writer claims and releases a voice
     * unit, a foreground-only operation); an automatable selector
     * (chorus_source) stays step-automatable and is never a modulation
     * target.
     */
    expect(instrumentManager_targetValid(0u,
               (instrument_param_id_t)(2u), INSTRUMENT_TARGET_AUTOMATION) == 0u,
           "law 9: a fixed selector is refused for automation");
    expect(instrumentManager_targetValid(0u,
               (instrument_param_id_t)(2u), INSTRUMENT_TARGET_MODULATION) == 0u,
           "law 9: a fixed selector is refused for modulation");
    expect(instrumentManager_targetValid(0u,
               (instrument_param_id_t)(3u), INSTRUMENT_TARGET_AUTOMATION) == 1u,
           "law 9: an on/off selector is valid for automation");
    expect(instrumentManager_targetValid(0u,
               (instrument_param_id_t)(3u), INSTRUMENT_TARGET_MODULATION) == 0u,
           "law 9: an on/off selector is invalid for modulation");
```

Add law 9 to the module header's law list.

`check_special_tags.py` — ADD to `main()` before the summary line:

```python
    # S099 P1: chorus_type must be a fixed selector in every table that has
    # it: its macro sets INSTRUMENT_PARAM_FLAG_SELECTOR and not AUTOMATABLE,
    # MORPHABLE or MODULATABLE (the cho writer claims a voice unit, a
    # foreground-only operation; law 9 proves the gate side).
    for t, path in TABLES.items():
        text = open(path).read()
        if '"chorus_type"' not in text:
            continue
        m = re.search(r'#define ROW_SELECT_FIXED_SPECIAL\(.*?\n(.*?)\n\s*MOD_NONE', text, re.S)
        rowm = re.search(r'^\s*(ROW[A-Z_]*)\("chorus_type"', text, re.M)
        if not m or not rowm or rowm.group(1) != 'ROW_SELECT_FIXED_SPECIAL' or \
                'AUTOMATABLE' in m.group(1) or 'MORPHABLE' in m.group(1) or \
                'MODULATABLE' in m.group(1) or 'FLAG_SELECTOR' not in m.group(1):
            print(f'{path}: chorus_type is not a fixed selector (ROW_SELECT_FIXED_SPECIAL, SELECTOR only)')
            bad += 1
```

### D-gates

- **D-G1:**
  - `make all && make img`;
  - `make -C tools/dsp_test all`: `chorus` and `chorus_tables` pass, the
    three chorus breaks print "caught", law 9 passes, and `special_tags`
    passes (CLX rows, both init files).
- **D-G2 (Basic identity):**
  - `elf_p4_check`;
  - `elf_s099_check`: every Colloid, Alkaline and Tetrakt loop is
    unchanged, and so are `Colloid_calcVoiceSyncBlock` and
    `Colloid_calcVoiceAsync` (against the P-C image);
  - `arm-none-eabi-objdump -s -j .rodata` of `colloid_menu_pages` is equal
    to the P-C image's;
  - `git diff --exit-code SD_CARD/Instrument/Colloid/init.col`.
- **D-G3 (loops):** `elf_looppath.py --cond-heads --in-range` on the linked
  image:
  - `bbd_sideD` ≤ **24** a sample (spike 22 + the dry gain 2);
  - `bbd_midA` ≤ **28**, `bbd_tapB` ≤ **17**;
  - `elf_blockpath.py`: `bbdChorus_control` ≤ **90** a block,
    `bbdChorus_renderColloid` entry/exit ≤ **30**.
  - A loop over its line means spills: stop and report.
- **D-G4 (host total):** `colloid_cpu.py` gains a line `clx-pwm6-par` =
  `pwm6-par` + 2 × `bbd_sideD` + (`bbdChorus_control` +
  `bbdChorus_renderColloid` + 2 × `bbd_sideD` block paths) / 32. It must be
  ≤ `pwm6-par` + **50**; the draft's figure is 49.
- **D-G5 (device, image D; Q-6):**
  - a clx in pwm Saw/Rec 6, `rte` str, `cho` II, `csr` L-R: the probe's cold
    first instance has median and p95 ≤ **21,803**. Windows next to an edit
    are reported.
  - Then `csr` mid on the same run: it must be lower.
  - **If over:** apply C1 (drop the pre-pole: `bbd_sideD` / `bbd_midA`
    store `x2 >> 1`), then C3 (half-rate wet, after its host image check).
    Each is reported at the top of the hand-over (Q-7). C4 and C5 wait for
    the user.
- **D-G6 (device, behaviour):**
  - `cho` off ↔ II during play with a bus Delay running: the Effect reseats
    (the Tetrakt path), and there is no fault;
  - Scene change to the same clx with the chorus on in both Scenes: no
    click;
  - a clx Scene commit waits for the chorus tail (no cut wet);
  - the Load menu lists `ColAdv`, and the kit cap of two Advanced types
    holds;
  - an AutoSave / reboot round trip keeps `cho`.
- **D-G7 (ear):** the three Juno modes, `var` over its range, `dbl`, `cmx`
  64 against a recording if one is available, and `csr` L-R against mid.
- **D-G8 (flash):** `link_budget.py` warns at < 16 KiB of headroom. The
  estimate is ≈ 18.5 KB left after P-D (§10).

---

## 7. Phase P-E — `alx`: the Advanced Alkaline and the chorus

P-E reuses the P-D machinery: the engine map, the pointer table, the
writer, the unit rule and the kernel. It adds the `alx` cases next to the
`alk` ones. Line numbers are the unedited tree's, as everywhere; where P-D
already edited a site, P-E's edit goes beside P-D's.

### E1. `Core/DSP/Instruments/InstrumentManager.h`

**E1.1 after the D5.1 `INSTRUMENT_TYPE_CLX,` — ADD:**

```c
    /*
     * S099 P1 (P-E): the Advanced Alkaline (token "alx"), before UNKNOWN.
     *
     * What:       value 8; UNKNOWN moves from 8 to 9. The type runs the
     *             Alkaline engine (AlkalineVoice) and the BBD chorus in its
     *             mono-in form (the Alpha Juno's own topology) with a 12 dB
     *             wet, a BBD drive into a hard clip, a tone and a width; while
     *             the chorus runs the slot is stereo (instrumentManager_
     *             slotRight() returns the voice's right block).
     * Why:        appended, as clx; menu_instrumentLoadIndex[] +2 B .bss.
     */
    INSTRUMENT_TYPE_ALX,
```

### E2. `Core/DSP/Instruments/Alkaline/AlkalineVoice.h` l. 210–211 (after
`float makeup;`, before `} AlkalineVoice;`) — ADD:

```c
	/*
	 * The Advanced Alkaline's chorus (S099 P1 P-E; DRAFT_ADVANCED_BBD_CHORUS.md
	 * §4.5, §5.2, §7.2).
	 *
	 * What:       choUnit: the slot's FxBuffer voice unit while cho is not
	 *             off, else NULL; the alx rows' bytes: choType cho, choRate
	 *             crt, choDepth cdp, choMix cmx, choTone cbw (the wet's cutoff,
	 *             5 .. 14 kHz, still tracking the delay), choDrive cdv (the BBD
	 *             input x1 .. x4 into a hard clip), choWidth cwd (127 the
	 *             Juno's opposed sweeps, 0 a mono wet); right: the right output
	 *             block while the chorus runs (written by the chorus pass:
	 *             the dry copy, then the right tap).
	 * Why:        the Alkaline is a mono voice; with the chorus it becomes a
	 *             stereo one (the mixer's per-slot pan then acts as a balance,
	 *             as for the Colloid). A Basic Alkaline (alk, 31 rows) never
	 *             writes these bytes and never reads right; choUnit stays NULL.
	 * RAM:        76 B in the 1,264 B slot reserve (564 -> 640 B, measured at
	 *             step 0.5): appended, so no existing member moves and no
	 *             loop's offsets change.
	 * Accessors:  AlkalineParameters.c (rows 31..37); InstrumentManager.c (the
	 *             cho writer, the alx async and sync, the right-block table,
	 *             the quiet test); alkalineParameters_cellInactive().
	 */
	int16_t *choUnit;
	uint8_t  choType;
	uint8_t  choRate;
	uint8_t  choDepth;
	uint8_t  choMix;
	uint8_t  choTone;
	uint8_t  choDrive;
	uint8_t  choWidth;
	uint8_t  choPad;
	int16_t  right[OUTPUT_DMA_SIZE];
```

### E3. `Core/DSP/Instruments/Alkaline/AlkalineParameters.h` / `.c`

**E3.1 `.h` after the descriptor count define — ADD:**

```c
/* S099 P1 (P-E): the Advanced Alkaline (alx) uses the same table with 38
 * rows: the 31 above, then cho crt cdp cmx cbw cdv cwd (asserted). */
#define ALKALINE_ADV_PARAM_DESCRIPTOR_COUNT 38u
#define ALKALINE_ADV_INSTRUMENT_TYPE_FLAGS  INSTRUMENT_FLAG_ADVANCED
#define ALK_IDX_CHORUS_TYPE                 31u   /* chorus_type (alx only) */
extern const instrument_menu_page_t alkaline_adv_menu_pages[];
extern const char alkaline_adv_instrument_display_label[];
/*
 * Whether an Advanced Alkaline cell has no effect now (S099 P1 P-E).
 *
 * What:       1 for crt and cdp unless cho is var or dbl; for cmx, cbw, cdv and
 *             cwd with cho off; else 0 (any row outside the chorus rows, and
 *             any pointer outside this table: 0).
 * Why:        the compact cell shows "---" for a value that changes nothing
 *             (menu_formatCellValue3()), as the Colloid's.
 * Inputs:     voice (the slot's runtime member), descriptor.
 * Accessors:  InstrumentManager.c instrumentManager_cellInactive().
 */
struct AlkalineVoiceStruct;
uint8_t alkalineParameters_cellInactive(const struct AlkalineVoiceStruct *voice,
                                        const ParamDescriptor *descriptor);
```

**E3.2 `.c`:**
- l. 2 (after `#include "AlkalineVoice.h"`) — ADD
  `#include "BbdChorus.h"     /* S099 P1: BBD_MODE_* (the cell test) */`;
- l. 36 — ADD
  `const char alkaline_adv_instrument_display_label[] = "AlkAdv";`;
- after the `ROW_SELECT_ONOFF_SPECIAL` macro (l. 82–87) — ADD the same
  `ROW_SELECT_FIXED_SPECIAL` macro and comment as D5.7;
- l. 143–144 (enum end) — MODIFY:

```c
    ALK_PARAM_TONE,              /* 30: alk_tone, the post-filter tone */
    ALK_PARAM_COUNT,
    /* S099 P1 (P-E, append-only, alx only): the chorus rows */
    ALK_PARAM_CHORUS_TYPE = ALK_PARAM_COUNT,   /* 31 */
    ALK_PARAM_CHORUS_RATE,
    ALK_PARAM_CHORUS_DEPTH,
    ALK_PARAM_CHORUS_MIX,
    ALK_PARAM_CHORUS_TONE,
    ALK_PARAM_CHORUS_DRIVE,
    ALK_PARAM_CHORUS_WIDTH,
    ALK_PARAM_ADV_COUNT
```

- after the existing asserts (l. 147–150) — ADD the three asserts, as D5.7:
  the count equals `ALK_PARAM_ADV_COUNT`, it is ≤ 64, and
  `ALK_PARAM_CHORUS_TYPE == ALK_IDX_CHORUS_TYPE`;
- l. 282–283 (after the `alk_tone` row) — ADD:

```c
    /*
     * @advanced-rows: the rows below are the alx type's only (S099 P1 P-E;
     * chorus draft §5.2, §7.2, Q-12). tools/alkaline/make_init_alk.py writes
     * init.alk up to this marker and init.alx with every row.
     * cho crt cdp cmx: as the Colloid's (no csr: the Alkaline is mono, the
     *   mid form always). cbw: the wet's cutoff, bbdKcTone[] (5 .. 14 kHz),
     *   the tracking still lowering it at long delays. cdv: the BBD's input
     *   gain x1 .. x4 into a hard clip at full scale (no compander; the wet
     *   level is held, so cdv adds only the clip). cwd: 127 the Juno's
     *   opposed sweeps, 64 an unswept right tap, 0 one tap (a mono wet).
     */
    ROW_SELECT_FIXED_SPECIAL("chorus_type", CAT_CHORUS, LONG_TYPE, SHORT_CHO, ONOFF_TAG_CHO_TYPE, choType, TYPE_UINT8, IM_SPECIAL_CHORUS_TYPE),
    ROW("chorus_rate", CAT_CHORUS, LONG_RATE, SHORT_CRT, DTYPE_0B127, MOD_0_127, choRate, TYPE_UINT8),
    ROW("chorus_depth", CAT_CHORUS, LONG_DEPTH, SHORT_CDP, DTYPE_0B127, MOD_0_127, choDepth, TYPE_UINT8),
    ROW("chorus_mix", CAT_CHORUS, LONG_MIX, SHORT_CMX, DTYPE_0B127, MOD_0_127, choMix, TYPE_UINT8),
    ROW("chorus_tone", CAT_CHORUS, LONG_TONE, SHORT_CBW, DTYPE_0B127, MOD_0_127, choTone, TYPE_UINT8),
    ROW("chorus_drive", CAT_CHORUS, LONG_DRIVE_WORD, SHORT_CDV, DTYPE_0B127, MOD_0_127, choDrive, TYPE_UINT8),
    ROW("chorus_width", CAT_CHORUS, LONG_WIDTH, SHORT_CWD, DTYPE_0B127, MOD_0_127, choWidth, TYPE_UINT8),
```

- l. 289–290 (the count assert) — MODIFY: compare with `ALK_PARAM_ADV_COUNT`;
- l. 312–330 (the pages) — MODIFY as D5.7: a macro `ALK_PAGES_0_6`, the Basic
  table unchanged, and:

```c
/* S099 P1 (P-E): MIX: vol pan ton drv | cho crt cdp cmx | cbw cdv cwd */
const instrument_menu_page_t alkaline_adv_menu_pages[] = {
    ALK_PAGES_0_6
    {{ ALK_PARAM_INSTRUMENT_VOL, ALK_PARAM_INSTRUMENT_PAN, ALK_PARAM_TONE, ALK_PARAM_VCA_DRIVE, ALK_PARAM_CHORUS_TYPE, ALK_PARAM_CHORUS_RATE, ALK_PARAM_CHORUS_DEPTH, ALK_PARAM_CHORUS_MIX, ALK_PARAM_CHORUS_TONE, ALK_PARAM_CHORUS_DRIVE, ALK_PARAM_CHORUS_WIDTH, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY }},
};
```

- ADD the cell test:

```c
uint8_t alkalineParameters_cellInactive(const struct AlkalineVoiceStruct *voice,
                                        const ParamDescriptor *descriptor)
{
	const ParamDescriptor *const base = alkaline_param_descriptors;

	if (!voice || !descriptor || descriptor < base + ALK_PARAM_CHORUS_TYPE ||
	    descriptor >= base + ALK_PARAM_ADV_COUNT)
		return 0u;
	switch ((alkaline_param_index_t)(descriptor - base)) {
	case ALK_PARAM_CHORUS_RATE:
	case ALK_PARAM_CHORUS_DEPTH:
		return (uint8_t)(voice->choType < BBD_MODE_VAR);
	case ALK_PARAM_CHORUS_TYPE:
		return 0u;
	default:
		return (uint8_t)(voice->choType == BBD_MODE_OFF);
	}
}
```

(`alkaline_param_index_t` is the enum's typedef name, l. 145.
`AlkalineParameters.c` already includes `AlkalineVoice.h` (l. 2), so only
`BbdChorus.h` is a new include. The `ALK_PAGES_0_6` macro holds pages 0–6
of l. 313–326 as they are, `ALK_PAGE_EMPTY` (l. 295) for page 3 included;
`ALK_PAGE_EMPTY` stays defined above it.)

- ADD `#undef ROW_SELECT_FIXED_SPECIAL` and `#undef ALK_PAGES_0_6` to the
  list at l. 339–353.

### E4. `Core/DSP/Instruments/InstrumentManager.c` — the `alx` cases

Each edit below sits beside its P-D counterpart.

| Site | Edit |
|---|---|
| D6.2 registry | ADD the row below. |
| D6.3 engine map | ADD `[INSTRUMENT_TYPE_ALX] = INSTRUMENT_TYPE_ALK,`. |
| D6.5 `unitsWanted()` | ADD `case INSTRUMENT_TYPE_ALX: return (uint8_t)(incoming->parameter_images.instrument_parameters[ALK_IDX_CHORUS_TYPE] != BBD_MODE_OFF);` |
| D6.5 `slotRightUpdate()` | ADD `case INSTRUMENT_TYPE_ALX: right = runtime_slots[slot].alkaline.choUnit ? runtime_slots[slot].alkaline.right : 0; break;` |
| l. 1746 `runtimeInstance` | ADD `case INSTRUMENT_TYPE_ALX:` above `case INSTRUMENT_TYPE_ALK:`. |
| l. 1768 `runtimeLfo` | the same. |
| l. 1847 `runtimeInit` | ADD `case INSTRUMENT_TYPE_ALX: (void)instrumentManager_slotVoiceUnits(slot, instrumentManager_unitsWanted(instrument)); /* fall through */` above the ALK case. |
| l. 2175 reset | ADD `case INSTRUMENT_TYPE_ALX:` above the ALK case. |
| l. 2298, 2336 pan / volume | ADD `case INSTRUMENT_TYPE_ALX:` above each ALK case. |
| l. 2583 `calcSlotAsync` | ADD the case below. |
| l. 2626 `calcSlotSyncBlock` | ADD the case below. |
| l. 2836 `triggerTrack` | ADD `case INSTRUMENT_TYPE_ALX:` above the ALK case. |
| l. 3176 `oscBySelector` | the same. |
| D6.10 `ampEnvelopeQuiet` | the ALK branch becomes `voice ? (uint8_t)(Alkaline_isQuiet(voice) && (!voice->choUnit || bbdChorus_tailDone(voice->choUnit))) : 1u`. |
| D6.16 `cellInactive` | ADD before the Colloid test: `if (instrumentManager_slotType(slot) == INSTRUMENT_TYPE_ALX) { const AlkalineVoice *alk = instrumentManager_alkalineRuntime(slot); return alk ? alkalineParameters_cellInactive(alk, descriptor) : 0u; }` |
| D6.21 `writeChorusType` | ADD the case below. |

Registry row (after the clx row):

```c
    /*
     * S099 P1 (P-E): the Advanced Alkaline. Token/extension "alx"/".alx";
     * directory /Instrument/AlkAdv (6 characters; SD_CARD/Instrument/AlkAdv
     * with init.alx). Advanced. Its rows are the Alkaline's table with the
     * seven chorus rows (38); the pages put the chorus on MIX.
     */
    [INSTRUMENT_TYPE_ALX] =
    { INSTRUMENT_TYPE_ALX, "alx", alkaline_adv_instrument_display_label, ".alx", "AlkAdv",
      ALKALINE_ADV_INSTRUMENT_TYPE_FLAGS,
      alkaline_param_descriptors, ALKALINE_ADV_PARAM_DESCRIPTOR_COUNT,
      alkaline_adv_menu_pages, ALKALINE_MENU_PAGE_COUNT },
```

Async case (before `case INSTRUMENT_TYPE_COL:` in `calcSlotAsync`):

```c
    case INSTRUMENT_TYPE_ALX: {
        /*
         * S099 P1 (P-E): the Advanced Alkaline: the Alkaline's control, then
         * the chorus's with the alx rows (tone cbw, drive cdv, width cwd). The
         * quiet count follows Alkaline_isQuiet() (gate closed, OTA current
         * below 1e-7 A).
         */
        AlkalineVoice *const alk = instrumentManager_alkalineRuntime(slot);

        Alkaline_calcVoiceAsync(alk);
        if (alk->choUnit)
            bbdChorus_control(alk->choUnit, alk->choType, alk->choRate, alk->choDepth,
                              alk->choMix, alk->choTone, alk->choDrive, alk->choWidth,
                              Alkaline_isQuiet(alk));
        break; }
```

Sync case (before the COL case in `calcSlotSyncBlock`):

```c
    case INSTRUMENT_TYPE_ALX: {
        /*
         * S099 P1 (P-E): the Advanced Alkaline: the Alkaline's mono block,
         * then the chorus's mono-in form (bbdChorus_renderAlkaline(): the dry
         * copied to voice->right, tap L in place on buf, tap R on right). With
         * the chorus on, instrumentManager_slotRight() returns right and the
         * mixer takes its existing stereo path for the slot (no mixer change);
         * with it off the slot stays mono.
         */
        AlkalineVoice *const alk = instrumentManager_alkalineRuntime(slot);

        Alkaline_calcVoiceSyncBlock(alk, buf, size);
        if (alk->choUnit)
            bbdChorus_renderAlkaline(alk->choUnit, buf, alk->right);
        break; }
```

Writer case (in `instrumentManager_writeChorusType()`, before `default:`):

```c
    case INSTRUMENT_TYPE_ALX: {
        AlkalineVoice *const alk = instrumentManager_alkalineRuntime(slot);

        alk->choType = mode;
        instrumentManager_chorusBind(slot, mode, &alk->choUnit, 1u);
        break; }
```

The writer already calls `instrumentManager_slotRightUpdate()` last. The
Alkaline's right block therefore appears in the mixer in the same
foreground step that binds the unit, and disappears in the step that
releases it. The next render block either has a right block written by
its own sync, or none.

### E5. Labels, defines, checker, init writer

- **`menu.h` l. 349 (after the D7.1 shorts) — ADD**
  `SHORT_CBW, SHORT_CDV, SHORT_CWD,` with `/* S099 P1 (P-E): cbw cdv cwd (alx) */`.
- **`MenuLabels.c` l. 76 — append** `{"cbw"},{"cdv"},{"cwd"},`.
  The short pool is 232 of 256.
- **`storageTypes.h` — ADD**
  `#define STORAGE_INSTRUMENT_ALX     INSTRUMENT_TYPE_ALX   /* S099 P1 */`.
- **`check_special_tags.py` — ADD**
  `'ALX': 'Core/DSP/Instruments/Alkaline/AlkalineParameters.c',`, with a
  comment as D9.1.
- **`tools/alkaline/make_init_alk.py` — the same changes as D9.2:**
  - `OUT_ADV` = `SD_CARD/Instrument/AlkAdv/init.alx`;
  - the `@advanced-rows` split; type `alx`;
  - `INIT_ADV`: `chorus_type` 1, `chorus_rate` 40, `chorus_depth` 127,
    `chorus_mix` 64, `chorus_tone` 73 (9 kHz), `chorus_drive` 0,
    `chorus_width` 127;
  - init.alk must stay byte-identical.

### E6. Menu engine tests

- **E6.1** `menu.c`: the D7.6 engine test already covers the Alkaline
  branch.
- **E6.2** `MidiNoteModes.c` has no Alkaline test (checked).
- **E6.3** `instrumentManager_waveDomainMax()` (D6.9) already uses the
  engine test.
- **E6.4** Nothing else in `menu.c` tests `INSTRUMENT_TYPE_ALK` (checked:
  l. 12510 is the only site).

### E7. Host law CH-ALK

CH-ALK is in `modules/chorus.c` (D10.1). P-E adds no other host law. The
Alkaline engine is unchanged by P-E. AL-* runs as before: the struct grew
at its end only.

### E-gates

- **E-G1:**
  - `make all && make img`;
  - `make -C tools/dsp_test all`, `special_tags` included (ALX rows, both
    init files).
- **E-G2:**
  - `elf_p4_check`: the mixer loops are unchanged; P-E edits no mixer code,
    and the Alkaline's stereo path is the existing one;
  - `elf_s099_check` against the P-D image;
  - init.alk is unchanged.
- **E-G3:** `elf_looppath.py`: `bbd_alkA` ≤ **31** a sample (`bbd_sideD` +
  the dry copy 1, the drive 2, the second pole 2), `bbd_alkB` ≤ **19**.
- **E-G4 (device, image E: the budget the user asked for):**
  - an alx alone, pwm R-S, `cho` II, `cdv` 64, cold first instance: record
    its median and p95 and the spare against 21,803;
  - then the two-Advanced Scene (clx + alx + an active bus Effect) against
    the S096 P3 Scene's whole-mixer figure (R-1).
  - The alx's measured spare is the number the user asked for. The ensemble
    tap (+15 a sample, chorus draft §5.2) fits only if the spare exceeds
    ≈ 700 cycles.
- **E-G5 (ear):**
  - `cbw` across its range;
  - `cdv` (where the clip starts on a loud 303 line);
  - `cwd` 0 / 64 / 127;
  - the alx at `cmx` 0 against a plain alk, which must be identical by
    ear. CH-MIX0 proves it on the host.

---

## 8. Phase P-F — deferred (Q-2)

The drum-family chop (S-R draft §6.2) is not scheduled. When it is:
- the five role loops need ≈ 0.9–1.0 KB, which do not fit the ITCM left
  after P-C (≈ 116 B);
- that leaves S-R draft D-2 option a (flash, cycle-gated), b (mixed), or an
  ITCM review (D-10, ≈ 810 B);
- the menu then needs a drum pwm list, and the Colloid's assert (C1.1)
  returns to `COL_PWM_WAVE_COUNT == OSC_PWM_WAVE_COUNT` once the platform
  list has R-S and REx.

---

## 9. Shared gate tooling

### 9.1 `tools/dsp_test/Makefile` — ADD after `elf_p4_check` (l. 1024):

```make
# ---------------------------------------------------------------------------
# S099 P1: the engines' loop identity (the Alkaline's B2R wrappers, the
# Colloid's oscillator and filter loops, the Tetrakt's sync block), against a
# snapshot taken before the phase. elf_p4_check covers the oscillator and
# mixer loops; this covers the engines elf_p4_check does not.
#
# What:       elf_s099_snapshot writes $(S099BASE)/engines.txt; elf_s099_check
#             dumps the same symbols from $(FW_ELF) and compares (exit 1 on a
#             MISSING or GREW).
# Accessors:  make -C tools/dsp_test elf_s099_snapshot | elf_s099_check
#             [S099BASE=baselines/s099p1/<phase>].
S099BASE ?= baselines/s099p1/pre
S099_SYMS := DiodeLadder_renderB2Osc DiodeLadder_renderB2Osc2 \
  DiodeLadder_renderB2OscPwm DiodeLadder_renderB2OscPwmWt alk_renderWav alk_renderWtb \
  col_oscPairStore col_oscPairAdd col_pwmPairStore col_pwmPairAdd col_warpStore \
  col_warpAdd col_scanPairStore col_scanPairAdd col_renderPar col_renderSer \
  Colloid_calcVoiceAsync Tetrakt_calcVoiceSyncBlock

elf_s099_snapshot:
	mkdir -p $(S099BASE)
	python3 elf_loopdump.py --elf $(FW_ELF) $(foreach s,$(S099_SYMS),--sym $(s)) \
	  --out $(S099BASE)/engines.txt

elf_s099_check:
	mkdir -p build
	python3 elf_loopdump.py --elf $(FW_ELF) $(foreach s,$(S099_SYMS),--sym $(s)) \
	  --out build/s099_engines_now.txt
	python3 elf_loopdump.py --compare $(S099BASE)/engines.txt build/s099_engines_now.txt
```

Add both to `.PHONY`. After each phase's device pass, take a new snapshot
(`S099BASE=baselines/s099p1/<phase>`) so the next phase compares against
the accepted image. Symbols a phase changes on purpose are left out of
that phase's compare and named in its gate (`alk_renderPwm` B,
`Colloid_calcVoiceSyncBlock` C).

### 9.2 Probe records

Device probe outputs (`device_probe_*.txt`) go to
`tools/dsp_test/baselines/s099p1/`, one file per image, named
`device_probe_<phase>.txt`. Use the S096 P3 run's format and stage list
(trace stage `p`, DEV_MODE_LOGGING).

---

## 10. RAM, flash and ITCM ledger (for sign-off)

| Item | Bytes | Region | Owner | Phase |
|---|---:|---|---|---|
| `ColloidVoice` growth (`choUnit`, five bytes, pad) | +12 (1,212 → 1,224) | SRAM1, inside the existing 1,264 B union slot (no growth of `runtime_slots[]`) | `ColloidVoice` | P-D |
| `AlkalineVoice` growth (`choUnit`, seven bytes, pad, `right[32]`) | +76 (564 → 640) | the same union slot | `AlkalineVoice` | P-E |
| `instrumentManager_rightBlock[6]` | 24 | SRAM1 .bss | InstrumentManager | P-D |
| `menu_instrumentLoadIndex[]` (sized by UNKNOWN, uint16) | +4 (2 types) | SRAM1 .bss | menu.c | P-D, P-E |
| Voice unit per chorus slot | 4,416 claimed, not new | DTCM `.dtcm_fxbuf` | the slot while `cho` ≠ off | P-D, P-E |
| `col_chopPairAdd()` | ≈ 308 (≤ 424) | ITCM `.itcm.s099p1` | ColloidOsc.c | P-C (Q-3 approved) |
| Stack: `col_chopBlock()` locals | ≈ 40 | the render stack | — | P-C |

Effect share:
- worst case unchanged at 100,160 B (six units: four Tetrakts plus two
  Advanced choruses, or six Tetrakts);
- 117,824 B with two choruses and no Tetrakt (chorus draft §3.3).

Flash (*estimate*; measured by `link_budget.py` at each gate):

| Item | Bytes |
|---|---:|
| `oscChopTables.c`: curves 4,096, DC 512, levels 1,024, rel 512, row tables 110 | ≈ 6,250 |
| `DiodeLadder_renderB2OscChop()` + `alk_renderChop()` | ≈ 1,600 |
| `col_chopBlock()` + `col_chopDcInit()` (the ITCM loop's load image counts in flash too) | ≈ 900 |
| `BbdChorusTables.c` | ≈ 2,050 |
| `BbdChorus.c` (seven loops, control, init) | ≈ 1,800 |
| Descriptor rows (12 × 20), pages (2 × 128), labels, InstrumentManager / menu code | ≈ 1,400 |
| **Total** | **≈ 14,000** |

Against 32,888 B of headroom this leaves ≈ 18.9 KB, above `link_budget.py`'s
16 KiB warning. If the measured total is larger, the curve tables are the
first candidate: REx and R-S could share a table at the cost of one
multiply, but this schedule does not do it.

---

## 11. Statements this work makes out of date (for the user to act on)

This schedule edits nothing in `MEMORY.md` or `knowledge_files/`.

- **`MEMORY.md` §DSP:**
  - Alkaline: "`alk_wave` 1, or `alkTriTable` ... (`ALK_WAVE_COUNT` 3)" and
    "`ALK_TYP_PWM 2u` ... read form 2, 204 instructions". Since P-B:
    `ALK_PWM_WAVE_COUNT` 5 under pwm (R-S, REx), and fused mode 5
    (`DiodeLadder_renderB2OscChop()`, `ALK_CHOP_READ_FORM` 2).
  - "PWM oscillator type (S094 P1 S3)": the drum family is unchanged. The
    Colloid's pwm list is now six (`COL_PWM_WAVE_COUNT` 6, the assert's new
    form).
  - Drum FM ramp twins: "ITCM **13,984** / 16,384 B after S094". It was
    already stale (15,960) and is ≈ 16,268 after P-C (measured at C-G3).
  - The ITCM subsection order: it ends with `.itcm.s095p2`, `.itcm.s095p3`,
    `.itcm.s096p3` and now `.itcm.s099p1`. MEMORY.md names only up to
    `.itcm.s094p1`.
  - "Cymbal/HiHat now Basic (S094 P2 §22): ... No current type is
    Advanced." `clx` and `alx` are Advanced (P-D, P-E).
  - "SRAM1 free after S094: 87,404 B": −28 B (the pointer table, the load
    index).
- **The FxBuffer rule "claimed at type assignment, never at a row
  change":**
  - stated in `InstrumentManager.c` (the comment is edited in D6.5),
    `EFFECTS_BUS_REFERENCE.md` and `EFFECTS_MODULES_DSP_REFERENCE.md`;
  - the cho row is the exception (Q-8);
  - the voice-unit owners are now the Tetrakt and the Advanced types'
    chorus.
- **`ALKALINE_MODULE_REFERENCE.md`** §3.1 (the waves under pwm) and §7.1
  (the pwm R-S / REx cost row; the alx row).
- **`INSTRUMENTS_DSP_REFERENCE.md`:**
  - §5.5 (Alkaline waves);
  - §8.1 / §8.2 (the measured loops: mode 5, the chop pair, the chorus
    loops);
  - the Advanced type entries;
  - the add-a-type checklist §9.2 (an Advanced type shares its Basic
    table: the engine map, `@advanced-rows`, the shared pages macro).
- **The Colloid reference:**
  - the pwm wave list;
  - the assert's form;
  - the ITCM total;
  - the clx type.
- **`CPU_USE_DSP_AUDIT.md`:**
  - the pwm chop rows;
  - the first Advanced slots and their measured cost (D-G5, E-G4);
  - `instrumentManager_slotRight()`'s table form.
- **`MENU_UI_SPEC.md`:**
  - the ON/OFF tags 9 and 10;
  - the Colloid's and the Alkaline's pwm name lists;
  - the Advanced MIX pages.
- **`knowledge_files/drafts/DRAFT_COLLOID_ALKALINE_ADVANCED_FX.md`:** the
  stale counts listed in the chorus draft §11.
- **`DRAFT_ADVANCED_BBD_CHORUS.md` §8:** "Record the unit's write position
  per Scene switch, as the Tetrakt does". No voice-unit owner writes
  handoff entries (deviation §0.3 item 9).

---

## 12. Items only the device or the ear can settle

| Item | Step | Default in the code |
|---|---|---|
| `REx` k (4–6) | A-G3 | 5 |
| Gate orientation | A-G3 | high first |
| Form T at low notes | A-G3, C-G6 | form T |
| `ALK_CHOP_SCALE_V` (the chop's level into the ladder) | B-G5 | the saw's peak |
| The Colloid's wet cutoff (`BBD_COL_TONE`) | D-G7 | 73 (9 kHz) |
| `cmx` law and default (equal power, 64) | D-G7 | as generated |
| The tracking factor (0.33 × the BBD clock) | D-G7 | 0.33 |
| `var` rate range | D-G7 | 0.1–10 Hz |
| The doubler's centre and depth | D-G7 | 15 ms ± 2.5 ms |
| The init.clx / init.alx bytes | D-G7, E-G5 | §D9.2 / §E5 |
| C1 / C3 if the clx probe is over | D-G5 | applied and reported (Q-7) |
| The Alkaline's spare (the ensemble tap's room) | E-G4 | measured, not acted on |

---

## 13. Implementation log (this session)

Notes are appended here as each step is applied. Line numbers below are the
schedule's (unedited-tree) numbers unless a note says otherwise.

### Phase 0 - baselines (done)

- **0.1** `make clean && make all && make img` on the unedited tree: clean,
  no warning. `link_budget_pre.txt`:
  `Flash 720,776 / 753,664 B (headroom 32,888)`, `ITCM 15,960 / 16,384`,
  `DTCM statics 4,396`, `FXBUF 126,656 B (margin 3,776)`.
  `.itcm` 15,960 B; `.text` 703,384 B.
- **0.2** `elf_p4_snapshot P4BASE=baselines/s099p1/p4base`: 76 mixer loops,
  the oscillator set written.
- **0.3** `elf_s099_snapshot` / `elf_s099_check` added to
  `tools/dsp_test/Makefile` (after `elf_p4_check`, in `.PHONY`).
  `elf_s099_snapshot` writes `baselines/s099p1/pre/engines.txt`: 15 loops in
  18 symbols. Two of the listed symbols,
  `alk_renderWav` / `alk_renderWtb`, are wrappers since S097 (their bodies
  `bl DiodeLadder_renderB2Osc`); they carry no loop, so they add no lines to
  the snapshot. The comparison is unaffected (the file is keyed by name and
  body).
- **0.4** `colloid_cpu_pre.txt` (count 10):
  `plain10-par 376.6`, `pwm6-par 404.7`, `pwm6-ser 377.9`, `pwmw6-par 407.8`,
  `scan6-par 408.2`. `alk_cost_pre.txt`: per sample 42.7 instructions,
  0.28 VDIV, per block 1,365 instructions / 9 VDIV.
- **0.5** `sizes_pre.txt`: `sizeof(ColloidVoice)` **1,212 B**,
  `sizeof(AlkalineVoice)` **564 B** (scratch translation unit, not in the
  tree). Both match the schedule; §10's growths land in the 1,264 B slot.
- **0.6** The session scratchpad no longer holds the spike sources. Written
  `baselines/s099p1/spike/README.md` saying so; the phase gates measure
  every new loop directly and do not depend on it.

### Phase P-A - chop tables and audition (done)

- **A1** `tools/plots/gen_pwm_tables.py` extended as scheduled; it writes
  `Core/DSPAudio/oscChopTables.h/.c` (new) and leaves `oscPwmTables.*`
  byte-identical (`git status`: only the two new files). Values: gate gains
  6,738..19,540; `OSC_CHOP_LREF_RS` 1.975558, `OSC_CHOP_LREF_REX` 6.628639
  (the draft's 6.62); `CHOP_REX_K` 5.
- **Deviation, T12.** The scheduled closed form (`dmax = max(row) - min(row)`,
  then `(dmax<<2) x g14 >> 16 + 32768`, times `32767 >> 16` <= 1.1 x 32767)
  fails on the correct tables (49,083 at row 0): it pairs the row's largest
  span with the curve's largest value, which never occur at the same phase.
  Replaced by `chop_range()`: an exact sweep of the loop's own integer
  arithmetic (every row, every width, 2,048 phases a cycle). Result: largest
  |gate| **70,605** (< 2^17) and largest |y| **24,687** (R-S) / **17,295**
  (REx), both under the bound. T10, T11, T13, T14, T15 pass unchanged.
- **A1.7** `oscChopTables.c` added to `DSP_SRCS` with its own `-fno-lto`
  rule (after the Colloid rules).
- **A-G1** `make -C tools/dsp_test pwm_tables` prints `pwm tables OK`.
- **A-G2** `make all && make img` after the tables: the `oscChop*` symbols
  are absent from the ELF (`--gc-sections`), flash still 720,776 B. The
  image is *not* byte-identical to Phase 0: 250 bytes differ, all of them
  the call sites and bodies of `__osc_setFreq_veneer`, which LTO moved 16 B
  (0x80b3c20 -> 0x80b3c30). `elf_p4_check`: 86 of 86 loops exact; the mixer:
  58 equal, 0 grew. No instruction changed; the veneer order did.
- **A2** `tools/plots/render_chop_audition.py` added and run into
  `/tmp/s099p1_audition` (44 WAV files: `rec`, `rs`, `rs-lowfirst`,
  `rexk4/5/6`, each at 55 / 110 / 440 / 1,760 Hz and form T / I). **A-G3
  is open (yours):** k, the gate orientation, and form T at 55 / 110 Hz.
  The code is written for the default (k = 5, high first, form T).

### Phase P-B - Alkaline R-S / REx (done)

- **Applied:** B1.1-B1.3 (wave lists: `ALK_WAVE_RS` 3, `ALK_WAVE_REX` 4,
  `ALK_PWM_WAVE_COUNT` 5), B2.1-B2.4 (mode 5 in `dl_renderB2Body()`, the
  `ALK_CHOP_READ_FORM` form, `DiodeLadder_renderB2OscChop()`), B3 (the
  prototype), B4 (`Alkaline_renderChop()`, the `alk_renderPwm()` dispatch,
  the per-type clamp), B5.1-B5.5 (menu names, wave domain, asserts), B6
  (`control_cost.py`), B7 (law AL-CH and its three breaks).
- **Deviation 1, `Alkaline_renderChop()` is its own file**
  (`AlkalineChop.c`, declared in `AlkalineVoice.h`, `-fno-lto`), instead of
  a static function in `AlkalineVoice.c`. Reason: R-5. With the render in
  the LTO unit, `elf_p4_check` showed ten Drum-family loops changed, two of
  them +1 (`calcSineRingBlock` 30 -> 31, `calcSineSyncBlock` 33 -> 34).
- **Deviation 2, `DiodeLadder.o` is compiled `-fno-lto`.** With only
  `AlkalineChop.o` outside the unit, the new copy of `dl_renderB2Body()`
  still re-partitioned it (fourteen loops changed, two +1). Outside, no
  loop grows, the four existing B2R wrappers are instruction-identical
  (`elf_s099_check`: 15 of 15 exact, `elf_p4_check`'s mixer: 58 equal), and
  the ladder's own loops are unchanged by the flag (checked: all four B2R
  wrappers exact). Cost: `Alkaline_calcVoiceAsync` 935 -> 937 (+2 a block,
  the dispatch test's budget).
- **B-G1** `make all`: clean, no new warning. Flash **728,144 B**
  (+7,368; headroom 25,520). ITCM **15,984 / 16,384** (+24, so 400 free;
  P-C's 424 B budget moves to 400 — see the P-C note).
  `make -C tools/dsp_test all`: exit 0; AL-CH 0 failures, its three
  breaks caught; the rest of the suite passes.
- **B-G1, AL-TY amended.** The law asserted "pwm clamps to Tri" for a byte
  past the pwm list; the pwm list now ends at REx. It now asserts "pwm
  clamps to REx" and adds "pwm REx -> wav = Tri".
- **B-G2** `elf_p4_check`: 74 exact, 12 MISSING by text, **0 loops longer**
  (`baselines/s099p1/p4_lengths_b.txt`: the ten symbols that differ are
  shorter or keep an equal-length twin; the other two are a renamed
  symbol). Read on length, as S095 P2 step 0 established. `elf_s099_check`:
  15 of 15 exact. The mixer: 58 equal, 0 shrank, 0 grew.
- **B-G3** `DiodeLadder_renderB2OscChop`: **220** a sample (3 VDIV);
  per-block 1,370 / 32 = **42.8**; total **262.8 per sample**
  (<= 263.7). (`control_cost.py` with the new ITERS entry and the guard.)
- **B-G4/B-G5 are open (yours):** the device probe and the ear.
  Measured level on the host: R-S at pos 0 is **-2.67 dB RMS** against
  pwm Saw at 0, inside AL-CH's 6 dB window; the read form 2 is **-33.7 dB**
  against form 0 (the guard is -15 dB).

### Phase P-C - Colloid R-S / REx (done)

- **Applied:** C1 (pwm list 0..5, `COL_PWM_IS_CHOP`, the assert), C2 (the
  section macro, `col_smulwt`, `col_chopDcInit`, `col_chopPairAdd`, the
  header), C3 (`col_chopBlock`, the sync dispatch, the comments), C4 (the
  linker script: `.itcm.s099p1` after `.itcm.s096p3`), C5 (menu names,
  branch, asserts), C6 (`colloid_cpu.py` chop row), C7 (CO-CHP, the
  restated law in `check_pwm_form()`, `test_pwg()` to REx, three breaks).
- **`col_chopBlock()` is two passes, not one** (deviation, C-G4): the first
  form built three packed arrays in a 6-iteration loop and spilled on every
  iteration (84 instructions an oscillator). It now computes the four mean
  sums in the first pass (nothing else can run before `col_chopDcInit()`),
  then builds each pair's halves in registers in a second pass (72 an
  oscillator).
- **C-G1** `make all && make img`: clean. Flash **729,736 B** (+1,592;
  headroom 23,928). ITCM **16,288 / 16,384**: 96 B free (P-B left 400, so
  the loop and its padding took 304). The linker's ITCM ASSERT holds.
  `make -C tools/dsp_test all`: exit 0; CO-CHP 0 failures, CO-PWM and
  CO-PWG pass on R-S / REx, the three CO-CHP breaks caught.
- **C-G2** `elf_p4_check`: same 12-by-12 text drift as P-B, 0 longer
  (unchanged). `elf_s099_check`: 15 of 15 exact. `col_oscControl`: the
  longest loop **187 -> 185**, so no cost rise, but the loop set itself
  changed (10 loops -> 8; GCC re-chose its unrolling in the same object).
  Reported as a deviation from "only an immediate may differ"; the
  cost gate holds. `Colloid_calcVoiceSyncBlock`'s straight path
  126 -> 134 (+8 a block: the new branch and its call, 0.25 a sample).
- **C-G3** `col_chopPairAdd` **306 B** (<= 424), in `.itcm.s099p1`;
  `_eitcm - _sitcm` = 16,288. `col_chopPairAdd` **54** a pair a sample
  (<= 58, the spike's figure exactly); `col_chopDcInit` **8** (<= 8).
- **C-G4** `colloid_cpu.py --count 10`: **pwm6-par 404.7**,
  **chop6-par 392.9** (-11.8), chop6-ser 366.1; plain 376.3 (was 376.6).
  The gate asked for `chop6-par <= pwm6-par - 20`; **-11.8 is short of
  it.** The draft's -30 was an estimate that omitted the six-oscillator
  mean/normalisation pass this schedule requires (deviation §0.3 item 2);
  the schedule's own per-block addition (one VDIV, the pass, the init)
  costs about 16 a sample. The device gate (C-G5) is the deciding one: the
  host model has the chop below pwm Saw/Rec, and both are below 19,620.
- **C-G5/C-G6 are open (yours):** the device probe (chop 6 against pwm
  Saw/Rec 6) and the form T ear check at 55 / 110 Hz.

### Phase P-D - `clx`, the Advanced Colloid and the chorus (done)

- **Applied:** D1 (`tools/chorus/gen_chorus_tables.py`, run; the pair is in
  the tree), D2/D3 (`BbdChorus.h/.c`), D4 (Makefile: `-I.../Chorus`, the two
  objects `-fno-lto`), D5 (types, writer tag, engine map, the pointer table,
  units, the Colloid's five rows, the 64-row table, the shared pages macro,
  `colloidParameters_cellInactive()`), D6 (InstrumentManager: the registry
  row, every `clx` case, the writer, the quiet tail, the right-block table),
  D7 (CAT_CHORUS, LONG_DEPTH/SOURCE, SHORT_CHO/CRT/CDP/CMX/CSR, the tags 9
  and 10, the two name lists, the menu branch), D8 (MIDI note modes, the
  storage define), D9 (checker, `make_init_col.py` -> `init.clx`, the decoder),
  D10 (`modules/chorus.c`, the `chorus`/`chorus_tables` targets, three
  breaks, law 9, the checker's macro test).
- **D-G1** `make all && make img`: clean. Flash **734,688 B** (+4,952;
  headroom **18,976**, above `link_budget.py`'s 16 KiB). ITCM 16,288 (no
  change). `make -C tools/dsp_test all` passes; `chorus` passes CH-TAP ..
  CH-INIT with the three breaks caught; law 9 passes; `special_tags` 394
  rows; `init.col` byte-identical, `init.clx` written (64 rows).
- **D-G2** `elf_p4_check` / `elf_s099_check`: the same drift as P-B/P-C
  (74 exact, 12 by text, **0 longer**; 15 of 15 exact), so the Basic engines'
  cost is unchanged. `colloid_menu_pages` is the same table through the
  shared macro (the Basic layout's spelling only).
- **D-G3 (loop counts, measured, over on three of the four lines):**
  `bbd_sideD` **25** (spike 22; gate <= 24), `bbd_midA` **29** (<= 28),
  `bbd_tapB` **18** (<= 17), `bbdChorus_renderColloid` entry/exit **24**
  (<= 30). Each is one instruction over its line, not the 16-value spill the
  gate warns about (the loops are 25/29/18 instructions wide).
  `bbdChorus_control`'s straight path is **146** a block (gate <= 90): its
  two inlined `bbd_sidePlan()` calls plus the clamps and the two slews.
- **D-G4** `colloid_cpu.py` gains `clx-pwm6-par` = 404.7 + **57.0** =
  **461.7** (the gate's `pwm6 + 50` is 454.7; **+7 over**), made of
  2 x 25 loop plus (146 control + 24 render + 2 x 25 entry/exit) / 32.
- **D-G5 is open (yours):** the device probe (median and p95 <= 21,803 in
  pwm Saw/Rec 6, cho II, csr L-R; then csr mid lower), the reseat, the Scene
  carry-over, `ColAdv` in the Load menu, the two-Advanced cap and the reboot
  round trip. No C1/C3 fallback was needed or applied: the host counts, not
  the device, are what is over the schedule's estimates.

### Phase P-E - `alx`, the Advanced Alkaline and the chorus (done)

- **Applied:** E1 (`INSTRUMENT_TYPE_ALX` 8, UNKNOWN 9), E2 (the
  `AlkalineVoice` chorus block: `choUnit`, seven bytes, `right[32]`), E3
  (the 38-row table, `ALK_PARAM_CHORUS_*`, the shared `ALK_PAGES_0_6`
  macro, `alkaline_adv_menu_pages`, `alkalineParameters_cellInactive()`), E4
  (the registry row, the engine map, `unitsWanted`, `slotRightUpdate`, every
  `alx` case, the mono chorus pass, the quiet tail, the writer), E5 (the
  `SHORT_CBW/CDV/CWD` labels, `STORAGE_INSTRUMENT_ALX`, the checker's ALX
  table, `make_init_alk.py` -> `init.alx`).
- **E-G1** `make all && make img`: clean. Flash **735,584 B** (+896;
  headroom **18,080**, above the 16 KiB warning). ITCM 16,288 (no change).
  `make -C tools/dsp_test all`: exit 0, `special_tags` 439 rows, `init.alk`
  byte-identical, `init.alx` written (38 rows).
- **E-G2** `elf_p4_check`: the same drift as before, **0 loops longer**
  (the mixer: 58 equal, 0 grew); `elf_s099_check`: 15 of 15 exact. P-E edits
  no mixer code (the stereo path is the Colloid's existing one).
- **E-G3 (measured, over on both lines):** `bbd_alkA` **36** (gate <= 31),
  `bbd_alkB` **20** (<= 19); `bbdChorus_renderAlkaline` entry/exit **20**.
  The Alkaline form is 1-5 instructions wider than the spike's estimate, no
  spill.
- **E-G4/E-G5 are open (yours):** the alx probe (median, p95 and the spare
  against 21,803), the two-Advanced Scene against S096 P3, and the ear
  (`cbw`, `cdv`, `cwd`, and `cmx` 0 identical to a plain Alkaline).

### Final verification (this tree)

- `make all && make img`: clean, no new warning.
  Flash **735,584 / 753,664 B** (headroom 18,080); ITCM **16,288 / 16,384**
  (96 free); DTCM statics 4,396; FXBUF 126,656 B.
- `make -C tools/dsp_test all`: exit 0 (every law, every break, the table
  checks and both init writers).
- `elf_p4_check`: 74 exact, 12 by text, **0 longer**
  (`baselines/s099p1/p4_lengths_b.txt`); mixer 58 equal, 0 grew.
  `elf_s099_check`: 15 of 15 exact.
- Device/ear gates left open: A-G3, B-G4/B-G5, C-G5/C-G6, D-G5/D-G6/D-G7,
  E-G4/E-G5. The A2 audition files are in `/tmp/s099p1_audition`.

### Measured ledger (§10's rows, for sign-off)

| Item | Measured |
|---|---:|
| `sizeof(ColloidVoice)` | 1,212 -> **1,224** (+12) |
| `sizeof(AlkalineVoice)` | 564 -> **640** (+76) |
| `instrumentManager_rightBlock[6]` | **24 B** SRAM1 .bss |
| `menu_instrumentLoadIndex[]` (two types) | +4 B .bss |
| Voice unit per chorus slot | 4,416 B claimed from the existing DTCM arena (no new RAM) |
| `col_chopPairAdd()` | **306 B** in `.itcm.s099p1`; ITCM 15,960 -> **16,288** (96 free) |
| Flash | 720,776 -> **735,584 B** (+14,808); headroom 18,080 |
| `col_chopBlock()` stack | ≈ 40 B (the render stack) |
| Records | `baselines/s099p1/` (`link_budget_post.txt`, `colloid_cpu_post.txt`, `alk_cost_post.txt`, `p4_lengths_final.txt`) |

### Gate deviations, collected (all measured in this tree)

1. **A-G2**: the table-only image is not byte-identical: 250 bytes differ,
   all `__osc_setFreq_veneer` call sites and bodies; LTO moved the veneer
   16 B. Every loop is exact (86/86).
2. **B-G2/C-G2/D-G2/E-G2**: `elf_p4_check` reports 12 loop dumps by text
   (ten symbols, each shorter or with an equal-length twin); **0 loops
   longer**, the mixer 58 equal. Read on length, the S095 P2 step 0 rule.
   `elf_s099_check` is 15 of 15 exact throughout.
3. **`DiodeLadder.o` and `AlkalineChop.o` are `-fno-lto`**: with the mode-5
   body in the LTO unit, ten Drum loops changed and two grew by 1; outside
   it, none grows. `DiodeLadder.o`'s own wrappers are unchanged by the flag
   (checked).
4. **C-G4**: `chop6-par` is 392.9, i.e. -11.8 against `pwm6-par` 404.7, not
   the -20 the schedule asked. The per-oscillator mean/normalisation pass
   (deviation §0.3 item 2) costs about 16 a sample; the two-pass form cut it
   from 84 to 72 an oscillator.
5. **D-G3/D-G4**: `bbd_sideD` 25 (gate 24), `bbd_midA` 29 (28), `bbd_tapB` 18
   (17), `bbdChorus_control` 146 a block (90); `clx-pwm6-par` 461.7 against
   the gate's 454.7.
6. **E-G3**: `bbd_alkA` 36 (31), `bbd_alkB` 20 (19).
7. **`col_oscControl`**: its longest loop shrank 187 -> 185, but its loop set
   changed (10 -> 8; GCC re-chose unrolling in the same object).
8. The host laws' order in `modules/colloid.c`: `test_chop()` is last, so
   the existing laws keep the RNG stream they had (CO-PWG's click metric is
   borderline and read differently when it ran earlier).

---

## 14. Review of the implementation (2026-11-19)

Reviewed: the working tree on `2c889c40` (the S099 P1 changes uncommitted),
against §2–§12 and the implementation log in §13. Method:
- the gates were re-run independently on this tree;
- the diffs of every changed source file were read;
- loop lengths were compared directly from the ELF dumps, not only from the
  log's summary.

### 14.1 Verdict

**The implementation matches the schedule, and the host and ELF gates
hold.** Every new law passes, every new break is caught, and no Basic loop
grew. Two findings need a decision before the device round:
- **F1:** the Advanced Colloid is likely over the Q-6 criterion on the
  device (p95);
- **F2:** the Alkaline's per-block cost rose by 5 instructions on every
  mode, its dearest included.

Nothing was found that is wrong in behaviour.

### 14.2 Re-run results (this review)

| Gate | Result |
|---|---|
| `make all && make img` | clean, no warning. Flash **735,584 / 753,664 B** (headroom 18,080, above the 16 KiB warning); ITCM **16,288 / 16,384** (96 B free). Matches §13. |
| `make -C tools/dsp_test all` | exit 0. AL-CH, CO-CHP, CH-TAP … CH-INIT and law 9: 0 failures. All nine new breaks print "caught" (3 AL-CH, 3 CO-CHP, 3 chorus). `special_tags` 439 rows, 0 mismatches. `pwm_tables` and `chorus_tables` OK. |
| Init files | `init.col` (59 rows) and `init.alk` (31) match their tables and are unchanged against HEAD. `init.clx` (64) and `init.alx` (38) are written. In `init.clx`, `chorus_type` and `chorus_source` are correctly absent from `[morph]`. |
| `elf_s099_check` | 15 of 15 loops exact. |
| `elf_p4_check` | **exits 2**: 86 baseline loops, 74 exact, 12 missing by text (F5). Direct length compare of the same dumps: in the 10 changed oscillator symbols the longest loop is equal or shorter (e.g. `osc_pwmWsSyncBlock` 38 → 37/35, `osc_smpFmBlock` 43 → 42). In `osc_slpRingBlock` / `osc_slpSyncBlock` the text differs at equal length. Mixer: **0 grew**. |
| Colloid host cost (`colloid_cpu_pre/post.txt`) | every Basic mode equal or lower: `plain10-par` 376.6 → 376.3, `pwm6-par` 404.7 → 404.7, `pwmw6-par` 407.8 → 407.8, `scan6-par` 408.2 → 408.2. New: `chop6-par` 392.9, `clx-pwm6-par` 461.7. |
| Alkaline host cost (`alk_cost_pre/post.txt`) | per block 1,365 → **1,370** (async 935 → 937; `alk_renderWav` / `alk_renderWtb` 361 / 412 → 364 / 415; `DiodeLadder_setNet` now a call). See F2. |

### 14.3 Code read against the schedule

These match the schedule's design, or an equivalent of it:
- **The chorus kernel** (`BbdChorus.c`): the five loops, the control,
  `bbd_sidePlan()`, init and the tail test. This includes the corrected
  guard refresh (deviation §0.3 item 13), proved by CH-GUARD and its break.
- **The Colloid chop:**
  - `col_chopPairAdd()` is in `.itcm.s099p1`, the linker pattern is after
    `.itcm.s096p3`, and the loop is 306 B;
  - `col_chopDcInit()`;
  - `col_chopBlock()`: the two-pass form computes the same sums and halves
    as the scheduled one pass;
  - the sync dispatch and the `inGain` / `inGainR` fix-up.
- **The Alkaline chop:** mode 5 in `dl_renderB2Body()`, with `ff0` /
  `ffStep` / `rowB` reused as scheduled; `DiodeLadder_renderB2OscChop()`;
  the dispatch in `alk_renderPwm()`; and the per-type clamp in
  `Alkaline_setOscType()`.
- **InstrumentManager:**
  - both registry rows, the engine map, `unitsWanted()`, the two-argument
    `slotVoiceUnits()`, the pointer table and `slotRightUpdate()`;
  - the writer and `chorusBind()`;
  - the quiet tails, and every `clx` / `alx` case.
  I checked each case label's fall-through target in all ten switches:
  every `clx` label reaches the Colloid body and every `alx` label the
  Alkaline body, and the `runtimeInit()` claims precede the right engine's
  init.
- **The engine tests** in the hooks, the special writer branches,
  `menu_formatWaveCell()` (all three branches) and `MidiNoteModes.c` (all
  three sites).
- **Tables, labels, tags and tooling:** the name tables and asserts, the
  ON/OFF tags 9 and 10, `storageTypes.h`, the decoder's type names, and
  `check_special_tags.py` (CLX / ALX tables, the `chorus_type` writer, the
  macro check).

These deviations from the schedule are reported in §13 and are accepted
here:
- **`AlkalineChop.c` and `DiodeLadder.o` are `-fno-lto`.** The reason is
  measured: in the LTO unit, two Drum loops grew by 1. The effect is F2.
- **T12 is an exact sweep of the loop's integer arithmetic.** The
  scheduled closed form paired a row's largest span with the curve's
  largest value, which never occur at the same phase. The sweep is the
  stronger check.
- **AL-TY is amended.** A pwm byte past the list now clamps to REx, the
  list's new last entry (the platform's rule), and pwm REx → wav gives Tri.
  That is the intended behaviour.
- **`test_chop()` runs last in `modules/colloid.c`,** so the existing laws
  keep their RNG stream.

### 14.4 Findings

**F1 — the Advanced Colloid is likely over Q-6 at p95 on the device
(decide before image D, or accept the Q-7 fallbacks).**
- The chorus measures **57.0 instructions a sample** with its per-block
  work, against the plan's 49 (`clx-pwm6-par` 461.7 against `pwm6-par`
  404.7). That is 1,824 instructions a block.
- On the S096 P3 pwm Saw/Rec 6 record (median 19,136, p95 19,562), using
  the draft's CPI band:

| CPI | added | median | p95 | against 21,803 |
|---:|---:|---:|---:|---|
| 1.2 | 2,189 | 21,325 | 21,751 | both pass |
| 1.4 | 2,554 | 21,690 | **22,116** | median passes by 113, p95 over by 313 |
| 1.6 | 2,918 | **22,054** | **22,480** | both over |

- Where the 8 over the plan come from:
  - `bbdChorus_control()` is **146** instructions a block (+3.5 a sample
    against the plan's ≈ 44);
  - each loop is one instruction over its line: `bbd_sideD` 25,
    `bbd_midA` 29, `bbd_tapB` 18;
  - none of them spills.
- Recommended order, cheapest sound cost first:
  1. **Trim `bbdChorus_control()`.** It is per-block code, so a trim
     changes no sound. Its two inlined `bbd_sidePlan()` calls each clamp in
     float, convert to Q16 and do three table reads. Folding the clamp into
     one fixed-point step per side, and computing the shared tone
     coefficient once instead of in each call, should recover about half.
     That is ≈ 2 a sample, ≈ 90–120 cycles a block.
  2. **C1 (drop the pre-pole):** −2 a channel, ≈ 180 cycles. Pre-authorised
     under Q-7, reported up front.
  3. **C3 (half-rate wet):** ≈ 670 cycles. Pre-authorised after its host
     image check.
- Item 1 plus C1 brings p95 at CPI 1.4 to about the line (≈ 21,810).
  Only C3 gives margin there.
- The probe decides: at the low end of the band (CPI 1.2) nothing is needed.

**F2 — the Alkaline's per-block cost rose by 5 instructions on every mode,
its dearest (`wtb` with a slot) included (≈ +0.16 a sample).**
- Cause: `DiodeLadder.o` left the LTO unit.
  - `DiodeLadder_setNet()` is now a call from `Alkaline_calcVoiceAsync()`
    (+2).
  - Each render wrapper's call into the ladder costs +3: `alk_renderWav`
    and `alk_renderWtb`, and the Saw/Sqr/Tri path of `alk_renderPwm`.
- None of the existing loops changed (`elf_s099_check` exact). The rise is
  ≈ 7 cycles a block, well inside the probe's noise, but under the S089 P2
  reading the schedule uses (Q-11, "no Basic type's dearest rises") it is a
  rise. Options:
  - accept it, recorded as S099 P1's cost of keeping the Drum loops exact;
  - declare `DiodeLadder_setNet()` / `DiodeLadder_setPost()` as `static
    inline` in `DiodeLadder.h`, so the Alkaline's LTO-unit async inlines
    them again. That should recover the +2; the wrappers' +3 needs a look
    at their call-site argument moves;
  - offset it elsewhere in the Alkaline's control path.
- B-G4 measures it on the device either way.

**F3 — `chop6-par` is 11.8 below `pwm6-par`, not the 20 C-G4 asked.** Not
a problem:
- the gate's purpose holds, since chop 6 is cheaper than the Colloid's
  dearest mode and no Basic mode rose;
- the margin went to the per-oscillator normalisation (deviation §0.3
  item 2);
- C-G5 on the device stays the deciding check.

**F4 — the Advanced Alkaline loops are wider than planned** (`bbd_alkA` 36
against 31, `bbd_alkB` 20 against 19). No spill. The Alkaline has ≈ 5,200
cycles of Advanced room, so this only reduces the spare that E-G4 is meant
to measure.

**F5 — `elf_p4_check` now exits non-zero by design.**
- The 12 text-only changes come from the LTO partition moving (two loops
  became two-variant forms). The length compare in §14.2 shows none
  longer.
- Leaving the gate red hides a real regression in a later phase.
- Recommendation: once image E passes on the device, re-snapshot (`make -C
  tools/dsp_test elf_p4_snapshot P4BASE=baselines/s099p1/p4base_accepted`)
  and point later checks at it.
- The S096 P1 re-baseline (Makefile l. 984–987) is the precedent.

**F6 — pre-existing, outside S099: `voice_control_break` is not
asserted.**
- The rule (`tools/dsp_test/Makefile` l. 253–255) runs the break binary,
  and that binary prints 14 failures but exits 0.
- So the suite passes whether or not the break is caught. Every other break
  in the suite is wrapped in an `if ... then FAILED` test.
- S099 did not touch it. It is reported because it shows up in this run's
  output.

**F7 — minor:**
- **A stale name in a comment:** the engine-map comment
  (`InstrumentManager.c` l. 154) names `instrumentManager_slotEngine()` as
  an accessor, and no such function exists. Delete the name.
- **The A2 renders are in `/tmp/s099p1_audition`** (44 files, present now),
  not the session scratchpad (Q-16). `/tmp` is cleared at reboot, so copy
  them before then if A-G3 is not done yet.

### 14.5 Open, as before (device and ear)

These stay open:
- A-G3 (k, orientation, form T at low notes);
- B-G4 / B-G5;
- C-G5 / C-G6;
- D-G5 / D-G6 / D-G7 (F1 decides how D-G5 is approached);
- E-G4 / E-G5.

No device-dependent claim in §13 or here has been checked on hardware.

### 14.6 Additions to §11 (statements now out of date, for the user)

- **`MEMORY.md` §DSP, the ITCM total:** 16,288 / 16,384 (96 B free) after
  S099 P1.
- **The flash total:** 735,584 B, headroom 18,080.
- **`MEMORY.md` §DSP Alkaline and the Makefile notes:** `DiodeLadder.o`,
  `AlkalineChop.o`, `oscChopTables.o`, `BbdChorus.o` and
  `BbdChorusTables.o` build `-fno-lto` (the S095 P2 rule, extended).
- **`ALKALINE_MODULE_REFERENCE.md` §7.1:** the Alkaline control path's
  per-block count is 1,370 (F2).

## 15. Review follow-ups (2026-11-19)

The user's instructions on §14:
- **F1:** get the cost down; if that is not enough, try C3; if C3 is still
  not enough, come back with C1's details.
- **F2:** fix it only if the Alkaline's worst case, Basic or Advanced, is
  over the cap.
- **Everything else:** fix it directly, and make the code comments correct.
- **Separately:** remove the Colloid's amp-attack minimum, so the note is as
  long as the step (or the length special), as from a keyboard.

### 15.1 Summary

- **F1 is done, with C3.** The Advanced Colloid's chorus is **47.0**
  instructions a sample, down from 57.0. `clx-pwm6-par` is **449.9**, down
  from 461.7. On the S096 P3 record, p95 passes at CPI 1.2 and 1.4 and is 73
  cycles over at CPI 1.6 (§15.2). C1 was not applied.
- **F2 needs no change.** Neither Alkaline worst case is over its cap
  (§15.3).
- **The Colloid note length is now the gate alone.** All three envelopes run
  a plain ADSR from it (§15.4). As a side effect every Colloid mode's
  per-block cost fell by 56 instructions.
- **F6 was a wrong finding and is withdrawn.** F7's stale name is fixed. F3,
  F4 and F5 need no code change (§15.5).
- **Gates:** the host suite exits 0 and every break is caught; the ELF gates
  hold against the S099 snapshot (§15.6).

### 15.2 F1: the Advanced Colloid's chorus

**Step 1: trims that leave the output unchanged.** Every chorus law gives
the same result before and after. Files: `BbdChorus.c` and `BbdChorus.h`.
- **The pre-pole state is kept at the sample's own scale.** It was ×2 and
  stored `>> 1`, so the ring store now needs no shift. The ring holds the
  same values: a one-pole filter of an int16 input stays inside int16.
- **The writing loops end on the write count, not an end pointer.** The
  loops are `bbd_sideD`, `bbd_midA` and `bbd_alkA`. The test is
  `while ((w & (OUTPUT_DMA_SIZE - 1u)) != 0u)`, which is correct because `w`
  is a multiple of 32 at every block start. The register allocator had
  spilled the end pointer and reloaded it every sample.
- **The guard test is one compare:** `(w & BBD_RMASK) == OUTPUT_DMA_SIZE`.
- **The loops are inlined.** `BBD_LOOP` is now `always_inline`, so the loops
  go into the two `noinline` render functions, and a block pays one entry
  and exit instead of three.
- **`bbd_sidePlan()` is `always_inline`.** The control reads the pre-pole
  coefficient once.
- **Result:** the full-rate L-R form went from 57.0 to **51.8** (`bbd_sideD`
  25 → 23 a channel, control 146 → 141, render paths 45). That was still
  over the target of 50: p95 at CPI 1.4 against 21,803.

**Step 2: C3, the half-rate wet.** It is selected by `BBD_COL_HALF_RATE`
(default 1) in `BbdChorus.h`; the Alkaline always runs at full rate.
- **Design:**
  - **Ring rate:** the rings are written and read at fs/2. Each ring sample
    is the mean of two pre-pole outputs, which puts a zero at 22 kHz.
  - **Tap:** one interpolated tap per output pair. The first output of the
    pair uses the mean of this tap and the previous one (`tq`), so the two
    outputs interpolate linearly between ring samples.
  - **Full rate:** the pre pole, the post pole and the dry + wet mix stay
    at full rate. The dry path is exact (CH-MIX0).
  - **`w` counts ring samples:** 16 a block (`BBD_HALF_BLOCK`). The guard
    refresh is `(w & BBD_RMASK) == BBD_HALF_BLOCK`.
  - **Read position:** `bbd_sidePlan()` takes a `half` argument and returns
    the read position in ring samples.
  - **Struct changes:** `BbdSide` gained `tq` (+4 B a side), and
    `BbdChorusState`'s `pad` byte became `half`. The state is still inside
    the unit's 288 B tail. No RAM moved.
- **New loops:** `bbd_sideH` (41 / 39 a pair), `bbd_midH` (52 a pair) and
  `bbd_tapBH` (29 a pair). None spills, except three spill instructions for
  `inc` / `tq` in `bbd_sideH`, which were kept.
- **Cost:** control 145 and render paths 63 a block. The dearer form is the
  mid form, at (52 + 29) / 2 = 40.5 a sample, plus (145 + 63) / 32 = 6.5,
  for **47.0**.
- **Rejected:** dropping the interpolation, which is cheaper but raises the
  images by about 9 dB.

**What C3 does to the sound.** New law **CH-HALF** compares the half-rate
and full-rate forms through the same unit. Results are below; the bounds are
in brackets.

| Measure | Result |
|---|---|
| 1 kHz wet, half against full | −0.07 dB (±0.5) |
| Image of 2 kHz at 20.05 kHz | −38.4 dB re the wet partial (≤ −18) |
| Image of 5 kHz at 17.05 kHz | −20.7 dB re the wet partial (≤ −18) |
| Fold of 15 kHz at 7.05 kHz | −14.5 dB re the input (≤ −10); the full-rate wet at 15 kHz is −8.4 dB |
| Fold of 18 kHz at 4.05 kHz | −17.0 dB re the input (≤ −10); the full-rate wet at 18 kHz is −9.4 dB |

The wet's content above about 11 kHz folds and images. The post pole
(5–9 kHz) already takes most of it off. Whether the result sounds acceptable
is a D-G6 ear check.

**Cost against the record.** On the S096 P3 pwm Saw/Rec 6 record (median
19,136, p95 19,562, measured at `pwm6-par` 404.7), `clx-pwm6-par` 449.9 adds
45.2 instructions a sample, or 1,446 a block:

| CPI | added | median | p95 | against 21,803 |
|---:|---:|---:|---:|---|
| 1.2 | 1,736 | 20,872 | 21,298 | both pass |
| 1.4 | 2,025 | 21,161 | 21,587 | both pass (p95 by 216) |
| 1.6 | 2,314 | 21,450 | **21,876** | median passes; p95 over by 73 |

The 45.2 includes the −1.8 a sample from §15.4, which applies to the Basic
Colloid as well.

**If the device probe (D-G5) lands at the top of the CPI band, C1 is next.**
- C1 drops the pre pole: about −2 instructions a sample in each form, about
  −64 a block, so about −100 cycles at CPI 1.6. That clears the 73.
- The cost is that the wet loses the pre-filter before the ring. With C3
  the ring's 2-tap mean is then the only anti-alias filter, so the 15 and
  18 kHz folds would rise. CH-HALF would measure by how much.
- C1 is not applied. It needs your decision, and only if D-G5 asks for it.

**Laws, breaks and tooling.**
- **CH-ALK was wrong before this change.** Its "12 dB wet" check used a
  formula that ignored the Alkaline's second post pole. It now measures the
  wet at 16 kHz against the restated three-pole response: −13.13 dB, which
  matches.
- **CH-GUARD has a half-rate case:** the tap is checked against the exact
  ring, and the guard refresh at `w & 1,023 == 16` is checked.
- **CH-LAW** reads `rp` in ring units (`>> half`).
- **`make -C tools/dsp_test chorus`** builds the default (half-rate) and
  `-DBBD_COL_HALF_RATE=0` (`chorus_full`) versions, and both must pass.
- **The chorus breaks are fixed:**
  - each break now greps for its own law's FAIL;
  - the mirror sed covers both mid loops;
  - the mix sed matches the loops' new `x << 1` form. It had stopped
    applying, and the break had been "caught" only by way of CH-ALK.
- **`colloid_cpu.py`:**
  - it finds the clx loops without needing `bbd_sideD`;
  - it reads `BBD_COL_HALF_RATE` from `BbdChorus.h`;
  - it charges max(the L-R form, the mid form) of the four loops divided by
    the rate, plus the control and render paths divided by 32.
- **Records:** `tools/dsp_test/baselines/s099p1/colloid_cpu_post2.txt`.

**The Alkaline's loops are narrower after the trims:** `bbd_alkA` 36 → 34,
`bbd_alkB` 20 (F4).

### 15.3 F2: the Alkaline's worst cases against the caps

Neither is over.
- **Basic:** the worst on record is **16,591** cycles, from S094 P2 run 2,
  segment E. The cap is 19,620. F2's +5 instructions a block is about 7
  cycles.
  - The "19,609" in the S-R draft has no record behind it. I found no log
    with that number.
- **Advanced:** 16,591 plus the Alkaline's chorus. The chorus costs about
  1,925 instructions a block: loops 34 + 20 = 54 a sample (1,728 a block),
  plus the control and render paths. That gives 18,900, 19,290 and 19,670 at
  CPI 1.2, 1.4 and 1.6. The cap is 21,803.
- **No change was made.** E-G4 on the device still measures the real
  figure.

### 15.4 The Colloid note length: a plain ADSR from the gate

**The rule (user, 2026-11-19).**
- The note lasts exactly as long as the step, or the length special's
  window when there is one, as if it were played from a keyboard.
- Sus on holds the note, and a later sus-off automation ends it.
- Every envelope runs a normal ADSR from that gate.
- This removes the S096 P3 amp-attack minimum. Under that rule:
  - a short step played the whole amp attack, then went straight to release
    with no decay;
  - the filter and pitch envelopes were held with the amp
    (`heldFP = held || amp in attack`).

**The code (`ColloidVoice.c`).**
- **`col_egStep()`** lost its `isAmp` argument. For every envelope: if the
  gate is not held and the stage is not idle, the stage goes to release,
  whatever it was, attack included. Attack at 1.0 always goes to decay.
  Decay and sustain no longer test `held` themselves, because the check at
  the top covers them. The doc block is rewritten.
- **`Colloid_calcVoiceAsync()`:** `heldFP` is gone, and all three envelopes
  step with `held`. The gate comment is rewritten.
- **Comments elsewhere:**
  - `ColloidVoice.h` (the gate fields' Why) now says there is no amp-attack
    minimum;
  - in `sequencer.c`, the `seq_lenWindow` note and `seq_lenStart()` no longer
    say "the amp attack sets the note's length".
- **Behaviour you will hear:** a step shorter than the amp attack now
  releases mid-attack, from the level it reached, so a slow-attack patch on
  short steps plays quieter notes than before.
  - Fill 0 still rounds up to a 1-tick window, about 5 ms at 120 BPM. On a
    slow attack that is now nearly silent.
  - This follows the keyboard rule. If you want fill 0 to mean something
    else, that is a separate decision.
- **Cost:** every Colloid mode's per-block cost fell by 56 instructions (for
  example `pwm6-par` 404.7 → 402.9). Flash fell by 120 B. Nothing changed
  per sample.

**The laws (`tools/dsp_test/modules/colloid.c`).**
- **CO-GATE is rewritten** to the new rule:
  - a step of g blocks, shorter than the attack, stays in attack for g
    blocks, releases on block g + 1, and has not reached 1.0;
  - a long step still holds decay and sustain until its end;
  - sus holds past a short gate, and a following sus-off write releases all
    three envelopes on the next block (new);
  - a one-block gate holds amp, filter and pitch in attack for one block,
    and all three release together on the next.
- **CO-EG, CO-RETRIG, CO-CHANGE and CO-PENV** pass unchanged.
- **The breaks (`tools/dsp_test/Makefile`):**
  - `colloid_break_atk` now restores the old rule: attack is exempt from
    the gate-closed release. CO-GATE catches it in four places.
  - `colloid_break_sus` now disables the gate-closed release altogether.
    The line it used to target no longer exists. CO-EG catches it.

### 15.5 The other findings

- **F3 (`chop6-par` margin):** no change, as §14 said. It is now 391.2,
  against `pwm6-par` 402.9.
- **F4 (Alkaline chorus loops wider than planned):** reduced by the §15.2
  trims (`bbd_alkA` 34). No further change.
- **F5 (`elf_p4_check` red against the S096 P1 default baseline):** no
  change. The re-snapshot waits until image E passes on the device, as §14
  recommends.
- **F6 is withdrawn.** It was a wrong finding.
  - `voice_control.c` l. 364–367 inverts the break build's exit status:
    `return failures > 0 ? 0 : 1;`.
  - So `make` stops if the break is not caught. The 14 printed failures are
    the break working as intended.
- **F7:**
  - The stale name is fixed. The engine map's Accessors line in
    `InstrumentManager.c` no longer names `instrumentManager_slotEngine()`.
  - The A2 renders are still in `/tmp/s099p1_audition` (44 files, 30 MB).
    Per Q-16 they are not records, so they were not moved into the tree.
    Copy them before a reboot if A-G3 is still open.
- **Other comments made correct:**
  - The `BBD_D_MIN` / `BBD_D_MAX` note now covers the half-rate case: 32.5
    to 495 ring samples against 16 a block, so the guard and pass-B rules
    hold by the same block margins.
  - The `BBD_COL_HALF_RATE` doc block gives the measured 47.0 in place of
    the estimate.
  - The loop, struct and pole-scale comments in `BbdChorus.c` and
    `BbdChorus.h` were read against the code and match it.

### 15.6 Gates re-run (this tree)

| Gate | Result |
|---|---|
| `make all && make img` | clean, no new warning. Flash **735,736 / 753,664 B** (headroom 17,928); ITCM **16,288 / 16,384** (96 B free, unchanged). |
| `make -C tools/dsp_test all` | exit 0: every law has 0 failures (`chorus` and `chorus_full` included) and all 87 breaks are caught. `special_tags`, `pwm_tables`, `chorus_tables` and the init writers are OK. |
| `elf_s099_check` | 15 of 15 loops exact. |
| `elf_p4_check P4BASE=baselines/s099p1/p4base` | the same as §14: 74 exact, the same 12 text-only moves, and per-symbol lengths identical to `p4_lengths_final.txt` (0 longer). Mixer: 58 equal, 0 shrank, 0 grew. |
| `elf_p4_check` (default S096 P1 base) | red, as F5 says. Against that older base, which predates later mixer work, the matcher pairs unrelated loops, so its "grew" lines do not show a regression. Use the S099 snapshot. |
| Colloid host cost | `colloid_cpu_post2.txt`: every mode −1.8 a sample against `colloid_cpu_post.txt`; `clx-pwm6-par` 449.9. |
| Alkaline host cost | `alk_cost_post2.txt`: unchanged, 1,370 a block. |

### 15.7 Additions to §11 and §14.6 (statements now out of date, for the user)

- **The flash total** (§14.6): 735,736 B, headroom 17,928.
- **`MEMORY.md` §DSP, the Colloid:**
  - the note length is the gate alone, and all envelopes run a plain ADSR
    (S099 P1 §15.4);
  - the S096 P3 amp-attack minimum is gone.
- **`MEMORY.md` §DSP, the chorus:** the Colloid's wet runs at half rate
  (`BBD_COL_HALF_RATE`), and the Alkaline's at full rate.
- **`S096_P3_IMPLEMENTATION.md` / `S096_P3_COLLOID_TETRAKT_INST_MIDI.md`**
  §2.1 rules 2 and 4 (the amp attack extends the gate) are superseded by
  §15.4. They stay as historical records.
- **D-G5:** judge the device probe against `clx-pwm6-par` 449.9 and the
  §15.2 table. C1 is the remaining option if p95 is over.

### 15.8 Flash: where S099 P1's 15 KB went

The user asked why flash went up so much.
- **This round added only +152 B over the review:** C3's half-rate loops
  +272, the §15.4 envelope change −120.
- **S099 P1 as a whole added +14,960 B:** 720,776 → 735,736, against the
  schedule's estimate of about 14,000 (§12 flash table).
- **Method:** the pre-implementation commit (`2c889c40`) was rebuilt in a
  scratch worktree. It reproduced 720,776 B exactly. The two images were
  then compared symbol by symbol (`arm-none-eabi-nm -S`, flash sections
  only).

| Item | Bytes | Estimate |
|---|---:|---:|
| Chop tables (`oscChopTables.c`: the R-S and REx curves at 2,048 B each, plus the level, DC and release tables) | +6,254 | ≈ 6,250 |
| Chop code (`DiodeLadder_renderB2OscChop` 1,420, `col_chopBlock` 584, `col_chopPairAdd` 306, `Alkaline_renderChop` 260, `col_chopDcInit` 34) | +2,604 | ≈ 2,500 |
| Chorus tables (`BbdChorusTables.c`) | +2,120 | ≈ 2,050 |
| Chorus code (render ×2, control, init) | +1,934 | ≈ 1,800 |
| Advanced types plumbing (InstrumentManager: the chorus bind, the inactive-cell test, the slot reset; descriptors, pages, MIDI note modes) | +1,966 | ≈ 1,400 |
| `DiodeLadder.o` out of LTO (`Alkaline_initVoice` no longer inlines the ladder init) | −236 | — |
| Other (menus, MIDI, LTO reshuffle) | +139 | — |
| Alignment and literal pools | +179 | — |

- **Most of the overrun is the plumbing,** about 570 B over its estimate.
- **Headroom is 17,928 B,** which is 1,544 B above `link_budget.py`'s 16 KiB
  warning.

### 15.9 Flash options, measured (PWM Sin since applied: §15.10)

These were measured, not applied. Each was a trial build in a scratch copy of
this tree:
- the copy, unmodified, reproduced 735,736 B exactly;
- each trial is a minimal edit that cuts the feature's references, and the
  linker drops the dead code and tables;
- the wave lists, names and counts were left alone, so a real removal saves
  a few more bytes (the names, a branch) and needs the law and menu updates
  besides.

Each figure is that change on its own. The savings do not simply add up
where two changes share tables.

| Option | Flash saved | ITCM freed | What goes |
|---|---:|---:|---|
| **Drop PWM Sin** (the drum family's form WS and the Colloid's pwm Sin) | **2,552** | **856** | `oscPwmSinW/Ra/Rb` (1,536), the five `osc_pwmWs*Block` ITCM loops (860), and part of `osc_pwmPlan` (56) |
| **Drop REx** (Colloid and Alkaline) | **3,200** | 0 | the REx curve (2,048), level (512), release and DC tables (256 each), and the REx selects in `col_chopBlock` and `Alkaline_renderChop` (132) |
| **Replace PWM Saw with R-S** (drop Saw; R-S already exists on the Colloid and the Alkaline) | **440** | 0 | `oscPwmLevelSawQ14` (256) and the Saw branches in `col_oscControl` and `osc_pwmPlan` (144) |
| **Drop the chop from the Alkaline only** (R-S and REx; the Colloid keeps them) | **2,808** | 0 | `DiodeLadder_renderB2OscChop` (1,420), `Alkaline_renderChop` (260), the level tables, which only the Alkaline reads (1,024), and `oscChopSpanF/InvSpanF` (88) |

Notes:
- **PWM Sin is the only option that frees ITCM.** It would raise ITCM's free
  space from 96 B to 952 B. The drum family's Sin is a Basic sound, so
  dropping it removes a sound from every drum engine as well as the Colloid.
  Dropping it from the Colloid alone saves almost nothing: the Sin tables
  stay for the drum family, and the warp loops stay for Tri.
- **PWM Saw shares its code with Rec everywhere:** the drum family's form D,
  the Colloid's pair loops and the Alkaline's ladder mode 3. Its width table
  `oscPwmDSaw` is also the chop's width table, so it stays. Only Saw's level
  table and its branches go.
  - "Replacing Saw with R-S" in the drum family would add the chop there.
    That costs flash and cycles: it is the deferred P-F / Q-2.
- **Dropping the Alkaline's chop saves less than the 2,604 B of chop code.**
  The Colloid's chop code (924 B) and the shared tables stay. The 2,808
  includes the Alkaline-only level tables.
- **REx and the Alkaline chop overlap.** Both remove `oscChopLevelRex`, so
  `Alkaline_renderChop`'s REx select is also counted in both. Together they
  would save about 5,430 B (3,200 + 2,808 − 512 − 64), not 6,008.

### 15.10 PWM Sin dropped from every engine (applied)

**Decision (user, 2026-11-19):**
- drop PWM Sin from all modes;
- collapse the parameter values down;
- no migration.

**The new pwm lists.** Under typ pwm the wave byte is now the pwm list's own
index. In the drum family it is no longer the stock wave id.

| Engine | Before | After |
|---|---|---|
| Drum, Snare, Cymbal, HiHat (osc1) | 0 Sin, 1 Tri, 2 Saw, 3 Rec | **0 Tri, 1 Saw, 2 Rec** (`OSC_PWM_TRI/SAW/REC`, `OSC_PWM_WAVE_COUNT` 3) |
| Colloid, Basic and Advanced | 0 Sin, 1 Tri, 2 Saw, 3 Rec, 4 R-S, 5 REx | **0 Tri, 1 Saw, 2 Rec, 3 R-S, 4 REx** (`COL_PWM_*`, `COL_PWM_WAVE_COUNT` 5) |
| Alkaline, Basic and Advanced | Saw, Sqr, Tri, R-S, REx | unchanged (its list never had Sin) |

The wav lists are unchanged: Sin is still a wav wave on every engine.

**Stored values are not migrated.** A pwm byte saved before this change now
plays the next wave up:
- Drum family: old Sin → Tri, Tri → Saw, Saw → Rec. Old Rec (3) still plays
  Rec, because `osc_pwmPlan()` sends any value past the list to Rec. The
  menu shows it blank until the value is edited, since it is out of range.
- Colloid: old Sin → Tri, Tri → Saw, Saw → Rec, Rec → R-S, R-S → REx.
  Old REx (5) plays Rec, through the clamp past the list.
- A wav ↔ pwm switch keeps the byte and clamps it, as before (the S087 r8
  rule). The wav and pwm lists no longer line up value for value: for
  example wav Saw (2) becomes pwm Rec (2).

**Code.**
- **`Oscillator.h`:** added `OSC_PWM_TRI` 0, `OSC_PWM_SAW` 1 and
  `OSC_PWM_REC` 2; `OSC_PWM_WAVE_COUNT` 4 → 3; the typ and pwm comments are
  updated.
- **`Oscillator.c`:**
  - form WS is gone: `OSC_PWM_FORM_WS`, `osc_pwmReadS()` and the five
    `osc_pwmWs*Block()` ITCM loops;
  - `osc_pwmPlan()` switches on `OSC_PWM_TRI` / `OSC_PWM_SAW` / default
    (Rec);
  - the five role renderers choose between D and WT only;
  - the section, plan and `osc_userCount()` comments are updated.
- **`oscPwmWarp.h`:** the comments now name WT as the only drum warp form.
- **`oscPwmTables.c/.h`:** regenerated without `oscPwmSinW/Ra/Rb`.
  `tools/plots/gen_pwm_tables.py` dropped `SIN_W_END`, the Sin split and
  Sin's T3–T6/T9 checks. `oscChopTables.c/.h` are byte-identical.
- **`ColloidVoice.h`:**
  - the `COL_PWM_*` values are renumbered;
  - `COL_PWM_IS_WARP(w)` is now `w == COL_PWM_TRI`;
  - the `_Static_assert` against `OSC_PWM_WAVE_COUNT + 2` still holds
    (5 = 3 + 2).
- **`ColloidVoice.c`:**
  - `col_oscControl()`: `isSin` is now
    `isPwm ? 0u : (!isWtb && wave == COL_WAVE_SIN)`. That spelling is
    deliberate. The plainer `!isPwm && !isWtb && …` moved the function's
    register allocation and cost 1 to 5 instructions a block on every
    Colloid mode, the dearest included. This form costs 5 *fewer*
    (`colloid_cpu_post3.txt`).
  - The sync block's warp branch reads `oscPwmTri*` only.
  - The comments in `ColloidOsc.c/.h` and `ColloidParameters.c` are
    updated.
- **Menus:**
  - `MenuText.h` `colPwmWaveNames` is now {5, Tri, Saw, Rec, R-S, REx};
  - `menu.c` names a drum pwm value as `waveformNames[TRI + value + 1]`;
  - the assert text and comments are updated, and so is the
    `InstrumentManager.c` wave-domain comment.

**Laws, breaks and tools.**
- **`modules/oscillator.c`:**
  - every pwm wave now uses `OSC_PWM_*`;
  - PW1 drops its sine case;
  - PW6/PW7 run over three waves;
  - PW8 checks the clamp to `OSC_PWM_REC` and a count of 3;
  - PW9 drops the Sin tables;
  - PW10's second half now checks that the Tri peak moves strictly earlier
    with pos. It was the Sin peak.
- **`modules/colloid.c`:**
  - CO-PWM's warp test runs on three Tri rows and the Tri tables;
  - the form bus loses its Sin branch;
  - CO-JOIN's warp step uses Tri;
  - CO-PWG starts at `COL_PWM_TRI`.
- **`modules/alkaline.c`, AL-PWW:** the unfused reference sets
  `OSC_PWM_TRI`. It had set the stock id `TRI` (1), which now means Saw, and
  failed 3 checks until it was fixed.
- **`modules/pwm_dump.c`** dumps Tri, Saw and Rec.
  `tools/plots/plot_s094p4_pwm.py` dropped Sin from `WAVES`; the script was
  not re-run.
- **The `tools/dsp_test/Makefile` P4 symbol list** dropped the five WS
  loops. An older baseline reports them MISSING.
- **Unchanged on purpose:**
  - `gen_col_tables.py`'s comment ("the first six, Sin/Tri") is written into
    the generated `ColloidTables.h`, so it is left alone to keep the
    `--check` byte-exact;
  - the S095 P5 history notes in `ColloidVoice.h` stay as records.
- **Init files:** `init.col`/`init.clx` and `init.alk`/`init.alx` set typ
  wav, so they do not change.

**Measured.**

| Gate | Result |
|---|---|
| `make all && make img` | no new warning. Flash **733,144 / 753,664 B** (−2,592; headroom **20,520**). ITCM **15,432 / 16,384** (−856; **952 B free**). |
| `make -C tools/dsp_test all` | exit 0; every law has 0 failures; 87 breaks caught (break_pwm1–3 included); `pwm tables OK`. |
| `elf_s099_check` | 15 of 15 exact. |
| `elf_p4_check P4BASE=baselines/s099p1/p4base` | 73 exact, 13 missing by text: the five WS loops, which are gone, plus the earlier LTO text moves. Per-symbol lengths: 0 longer (`p4_lengths_post3.txt`). Mixer: 58 equal, 0 grew. |
| Colloid host cost | `colloid_cpu_post3.txt`: every mode −5 instructions a block against `colloid_cpu_post2.txt` (for example `scan6-par` 406.4 → 406.3, `pwm6-par` 402.9 → 402.8); `clx-pwm6-par` 449.8. |
| Alkaline host cost | unchanged (1,370 a block). |
| Drum-family cost | no loop longer. Pwm Sin's own loops (WS: plain 22–23, FM 31, FM ramp 32, ring 34, sync 38) are gone. Tri / Saw / Rec are unchanged. |

**Statements now out of date (for the user):**
- **`MEMORY.md`:**
  - the pwm lists (drum family Tri Saw Rec, Colloid Tri Saw Rec R-S REx,
    the byte is the list index);
  - flash 733,144 B (headroom 20,520);
  - ITCM 15,432 B (952 B free).
- **Device notes:** any listening notes or Scenes that use pwm Sin (see
  "Stored values" above).
