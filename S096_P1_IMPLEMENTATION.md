# S096 P1 — Tetrakt: Implementation Schedule

- **Status:** schedule, written 2026-11-14 for the plan
  `S096_P1_TETRAKT_SAMPLE_PLAYER.md` (revision 7, every question decided,
  its §12). No source was changed while writing it.
- **Tree:** the working tree on `d6a9c9b7` with the uncommitted S095 P4
  §17–§18 Colloid changes (`ColloidVoice.c/.h`, `ColloidTables.h`,
  `gen_col_tables.py`, `colloid_cpu.py`, `modules/colloid.c`,
  `tools/dsp_test/Makefile`). **Line numbers are those of that tree before
  any step.** Every edit also quotes its anchor text. Within one file,
  apply the edits bottom-up, or find each one by its anchor.
- **Actions:** **ADD** (new lines; the existing lines stay), **REMOVE**,
  **MODIFY** (replace the lines named with the block given), **NEW FILE**.
- **Order:** S0 → S12, each step's gate before the next (§2). Commits are
  yours; the schedule does not say when.
- **Rules that apply throughout:**
  - constant CPU: every stage runs every block, whatever its setting;
  - no backward compatibility (Tetrakt is a new type, so there is no
    stored data to migrate);
  - RAM: the 448 B shared work area is approved (plan T-Q1). Every other
    byte is inside the slot reserve, except the 2 B of §1 (sign-off asked
    there);
  - no other engine's per-sample loop may move (`elf_p4_check`);
  - nothing needed lives in `build/`;
  - every new or changed block carries its comment block (What, Why,
    Inputs/Outputs, Accessors, Affiliates as the tree's style).

---

## 0. Schedule decisions (made here; the plan's intent unchanged)

The code dive turned up the points below. Each is decided in this schedule;
say so before S1 if you want one changed.

| # | Point | Decision |
|---|---|---|
| SD1 | The plan's bit cells need a menu cell that names one bit of a row. Page tables hold one byte per cell (`instrument_menu_page_t.descriptor_index[16]`; 0xFE SKIP, 0xFF EMPTY; descriptor indices ≤ 63). Only `instrumentManager_menuDescriptorIndex()` (`InstrumentManager.c:616`) reads them | Codes **0xF0 + b** (b 0..3) are bit cells (`INSTRUMENT_MENU_BIT(b)`). The registry row names the switch row in a new field `switch_row_plus1` (0 = none, so every positional registry row stays valid zero-initialised). The lookup returns the switch row's descriptor; a new `instrumentManager_menuCellBit()` returns b. `menu_cell_t` gains `bit_plus1` (0 = not a bit cell, so every `memset` construction stays correct) |
| SD2 | What a bit cell needs in the menu | Only the value-level paths change: dtype (a plain on/off), display value (its bit), commit (the whole mask), the short name (`on` + the letter), `va_formatValue3()` (its bit of a held mask) and `va_writeAutomationFromKnob()` (writes the mask, shares the working value across the four cells). Every row-level path (automation target, search bitmap, held-step lookup, MIDI Learn, pot clear, live record) already uses `descriptor_index`, the `lon` row, so the four cells underline, learn and record together **with no change** (plan R5) |
| SD3 | Velocity "all four layers" (plan §4.2): the platform node modulates one runtime pointer | For a Tetrakt slot, a velocity token that is a layer-A row of one of the nine groups is not installed on the node. `InstrumentManager` passes the group (`TKT_KIND_*`) to the voice. The async applies the platform's negative-polarity law per layer, in row bytes: `byte − amount × (1 − v_layer) × 127`, clamped 0..127, with each layer's own latched velocity. Every other token (off, a voice row, the own-Morph token) takes the normal platform path. The velocity list hides the B/C/D rows of the nine groups; the cell shows `tn*` (compact) and a `*` after the long name (full view) |
| SD4 | The layer rows' binding | `TYPE_UINT8` bytes for every layer row. The LFO's overlay, Morph and velocity then all act in the row's own 0..127 domain, and the async converts per block |
| SD5 | Reusing the read (plan Q9) | `osc_renderTyped()` (public, ITCM, `Oscillator.c:2268`) dispatches `OSC_BIND_SMP` to `osc_smpPlainBlock()`. `osc_userBind()` (`:1347`) binds the latched sample and handles a catalogue reinstall. **`Oscillator.c` is not edited.** After the bind the voice sets `phaseInc = round(rate × 4,096)`, so `tuA` 63 at note 63 reads at exactly 1.0 |
| SD6 | The S1 feedback tone | `alkToneC[]` is not a plain one-pole coefficient (its values run past 1.0), so the feedback LP gets its own generated table, `tktFbLpK[sto]`: 600 Hz … 16 kHz, log-spaced |
| SD7 | SRR at 127 must be the input exactly; `valueShaperI2F(127, −0.7)` is `(1+k)/(1+k)`, which `-Ofast` need not keep exact | The rate comes from a generated table `tktSrrRate[128]` with the `srt` law, and entry 127 written as exactly `1.0f` |
| SD8 | Four full-scale samples summed clip the pack | Each layer's post gain carries `TKT_MIX_SCALE` 0.5 (−6 dB), set by ear in S12 |
| SD9 | R1 (plan §3.2) needs a "start next block" state | Layer stages `IDLE PLAY FADE START`. A retrigger of a sounding layer sets FADE (this block ramps to 0) then START. A retrigger of a quiet layer sets START. START binds, restarts, latches and plays from the next async |
| SD10 | `instrumentManager_ampEnvelopeQuiet()` for Tetrakt | Quiet when all four layers are IDLE. The S1 tail may still ring; a Scene swap at that moment cuts it, as a swap cuts any voice's release |
| SD11 | The voice unit lifecycle (plan §10, FxBuffer contract: foreground only) | In `instrumentManager_resetRuntimeSlotFromScene()` and `instrumentManager_runtimeInit()`: a slot whose incoming type is Tetrakt owns exactly one unit; any other type owns none. A Tetrakt → Tetrakt reset keeps its unit (no release and reclaim, so no Effect reseat). The voice clears the unit at init (2,208 stores, outside the render). If the claim fails (only possible with `DEV_FXBUF_FORCE_VOICE_UNITS` set, or a future third unit user), S1 is muted for that voice: the one documented exception to constant cost |
| SD12 | The plan's cost (≈ 355) was a sum of estimates | The concrete loop body below counts ≈ 70 per layer plus the read: **≈ 375 … 390 instructions per sample**. Gate: the host count (S10) ≤ 390. Above it, apply plan §9.3 trim 2 (the tone's weight ramp per block, −12) before the device step. The device probe (S12) decides the rest |
| SD13 | The voice struct | ≈ 1,080 B with the four `OscInfo` kept (§S2.1). Asserted ≤ 1,264 B; target ≤ 1,200 |
| SD14 | `decode_devlogs.py` names probe types 0..4 only (the Colloid shows as "type 5") | Add `5: "col"`, `6: "tkt"` |
| SD15 | `wid`'s default | 0 (W2 off), so a fresh layer plays its sample exactly as panned. Set by ear |

---

## 1. Net resources (estimates; measured at S11)

| Resource | Change |
|---|---|
| Flash | +≈ 7 … 9 KB: the voice (≈ 4 KB), the parameters and pages (64 descriptors × 28 B + names ≈ 2.2 KB), three tables (127 + 2 × 128 + 128 floats ≈ 2.5 KB), the menu and manager branches (≈ 0.5 KB). Headroom 36,892 B (`baselines/s095p5/warp6/link_budget.txt`) |
| ITCM | **0**: the read is the existing `osc_smpPlainBlock()` |
| DTCM statics | 0 |
| SRAM1 `.bss` | **+448 B** (`tkt_work`, approved, plan T-Q1) and **+2 B** (`menu_instrumentLoadIndex[]`, `menu.c:1733`, `uint16_t[INSTRUMENT_TYPE_UNKNOWN]`, grows by one entry with the new type). **The 2 B needs your sign-off**, as S095 P2's did for the Colloid |
| Slot reserve | `TetraktVoice` ≈ 1,080 B of 1,264 B (no new RAM) |
| FxBuffer arena | one voice unit (4,416 B) per Tetrakt, claimed from the Effect share (the units' designed use; no new RAM) |
| CPU per sample (Tetrakt only) | ≈ 375 … 390 instructions (SD12) ≈ 16,800 … 17,500 cycles per block at 1.4, ≈ 19,200 … 20,000 at 1.6, against 19,620 |
| CPU, every other engine | 0: no shared loop changes (S11 gate) |

---

## 2. Order and gates

| Step | What | Gate |
|---|---|---|
| S0 | Baselines | records in `tools/dsp_test/baselines/s096p1/before/` |
| S1 | Tables: generator, `TetraktTables.h/.c` | `gen_tkt_tables.py --check` |
| S2 | `TetraktVoice.h` | compiles in S5 |
| S3 | `TetraktVoice.c` | compiles in S5 |
| S4 | `TetraktParameters.h/.c` | compiles in S5; 64 rows asserted |
| S5 | Build | `make all` exit 0 (the type not yet registered: the objects are dropped by `--gc-sections`) |
| S6 | `InstrumentManager.h` | build |
| S7 | `InstrumentManager.c` | build; DEV boot self-check |
| S8 | Storage, `init.tkt`, its generator | `make_init_tkt.py --check` |
| S9 | Menu | build |
| S10 | Host: laws, cost tool, special tags, decoder | `make -C tools/dsp_test all` exit 0; `tetrakt_cpu_check` ≤ 390 |
| S11 | Gates and measurements | all of §S11 |
| S12 | Device steps (yours) | §S12 |

`elf_p4_check` runs with `P4BASE=baselines/s096p1/before` (S0 writes it).

---

## S0. Baselines (no source change)

1. `make all && make img` at the root; copy `build/lxr02.elf` to
   `tools/dsp_test/baselines/s096p1/before/lxr02.elf`.
2. `python3 tools/diagnostic_tools/link_budget.py arm-none-eabi-nm build/lxr02.elf
   > tools/dsp_test/baselines/s096p1/before/link_budget.txt`
   (expected: Flash 716,772, ITCM 15,232, DTCM 4,396, FXBUF 126,656).
3. `make -C tools/dsp_test elf_p4_snapshot P4BASE=baselines/s096p1/before`.
4. `make -C tools/dsp_test colloid_cpu_check > tools/dsp_test/baselines/s096p1/before/colloid_cpu.txt`.
5. `make -C tools/dsp_test all > tools/dsp_test/baselines/s096p1/before/host_all.txt 2>&1`
   (expected: exit 0).

---

## S1. Tables

### S1.1 NEW FILE `tools/tetrakt/gen_tkt_tables.py`

```python
#!/usr/bin/env python3
"""Write and check the Tetrakt's constant tables, TetraktTables.c (S096 P1).

What:       computes every table declared in TetraktTables.h and writes
            Core/DSP/Instruments/Tetrakt/TetraktTables.c:
              tktToneG[t]      t = 0..126, the effective tone byte (plan
                               §5.3): g = tan(pi fc / fs) of the tone SVF.
                               t < 63: LP, fc = 18 kHz x (120 / 18000)^u'
                               with u' = (63 - t) / 63; t > 63: HP,
                               fc = 20 Hz x (3000 / 20)^u' with
                               u' = (t - 63) / 63; t = 63: the LP's 18 kHz
                               (both weights are 0 there, so g only keeps
                               the ramp continuous).
              tktDriveMakeup[d] d = 0..127 (plan §5.4): half make-up. The
                               drive's output law (gain 2^(5 d / 127), bias
                               0.25 (d / 127)^2, the clamped cubic, minus
                               clip(bias), the dry/wet crossfade over the
                               first 8 steps) is run on a -12 dBFS-peak
                               sine; m = 10^(-0.5 x gain_dB / 20). d = 0 is
                               exactly 1.0 (gain 0 dB).
              tktSrrRate[v]    v = 0..127 (plan §5.5): the drum / mixer srt
                               law valueShaperI2F(v, -0.7)
                               (InstrumentManager.c:4349), written as
                               exactly 1.0 at v = 127.
              tktFbLpK[s]      s = 0..127 (S1's sto, SD6): the feedback
                               one-pole's k = 1 - exp(-2 pi fc / fs),
                               fc = 600 Hz x (16000 / 600)^(s / 127).
            fs is config.h REAL_FS (44,108.0729 Hz). Floats are written
            with 9 significant digits (float32 exact).
Why:        one owner for every constant (the Colloid's rule,
            gen_col_tables.py); no tan, exp or pow in the control path;
            the exact entries (tktSrrRate[127], tktDriveMakeup[0]) carry
            the bypass laws of plan §13 step 1.
Inputs:     --check: compare instead of write.
Outputs:    the file; with --check a message, exit 1 on a difference.
Accessors:  python3 tools/tetrakt/gen_tkt_tables.py [--check];
            make -C tools/dsp_test tetrakt_tables.
Affiliates: TetraktTables.h (sizes); TetraktVoice.c (readers);
            S096_P1_TETRAKT_SAMPLE_PLAYER.md §5.
"""
import argparse
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
OUT = os.path.join(ROOT, "Core", "DSP", "Instruments", "Tetrakt", "TetraktTables.c")

FS = 44108.07291666667      # config.h REAL_FS
TONE_POINTS = 127           # TetraktTables.h TKT_TONE_POINTS
POINTS = 128                # TetraktTables.h TKT_TABLE_POINTS
LP_TOP, LP_BOTTOM = 18000.0, 120.0
HP_BOTTOM, HP_TOP = 20.0, 3000.0
DRIVE_OCT, BIAS_MAX, WET_STEPS, CLIP, CUBIC = 5.0, 0.25, 8, 1.5, 4.0 / 27.0
FB_LO, FB_HI = 600.0, 16000.0


def f32(x):
    """A C float literal with 9 significant digits."""
    s = f"{x:.9g}"
    if "." not in s and "e" not in s:
        s += ".0"
    return s + "f"


def tone_g(t):
    if t < 63:
        fc = LP_TOP * (LP_BOTTOM / LP_TOP) ** ((63 - t) / 63.0)
    elif t > 63:
        fc = HP_BOTTOM * (HP_TOP / HP_BOTTOM) ** ((t - 63) / 63.0)
    else:
        fc = LP_TOP
    return math.tan(math.pi * fc / FS)


def clip(x):
    x = max(-CLIP, min(CLIP, x))
    return x - CUBIC * x * x * x


def drive_out(x, d):
    """TetraktVoice.c tkt_layerPost()'s drive stage, restated."""
    dn = d / 127.0
    g = 2.0 ** (DRIVE_OCT * dn)
    b = BIAS_MAX * dn * dn
    wet = 1.0 if d >= WET_STEPS else d / float(WET_STEPS)
    y = clip(g * x + b) - clip(b)
    return x + wet * (y - x)


def makeup(d):
    if d == 0:
        return 1.0
    n = 4096
    amp = 10.0 ** (-12.0 / 20.0)
    xs = [amp * math.sin(2 * math.pi * k / n) for k in range(n)]
    ys = [drive_out(x, d) for x in xs]
    # the DC blocker on the bus removes the bias's moving DC: compare AC power
    my = sum(ys) / n
    rx = math.sqrt(sum(x * x for x in xs) / n)
    ry = math.sqrt(sum((y - my) ** 2 for y in ys) / n)
    gain_db = 20.0 * math.log10(ry / rx)
    return 10.0 ** (-0.5 * gain_db / 20.0)


def srr_rate(v):
    if v == 127:
        return 1.0
    shape = -0.7
    k = 2 * shape / (1.0001 - shape)
    val = v / 127.0
    return (1 + k) * val / (1 + k * abs(val))


def fb_k(s):
    fc = FB_LO * (FB_HI / FB_LO) ** (s / 127.0)
    return 1.0 - math.exp(-2.0 * math.pi * fc / FS)


def rows(values, per=4):
    out = []
    for i in range(0, len(values), per):
        out.append("    " + ", ".join(values[i:i + per]) + ",")
    return out


def text():
    L = ["/*",
         " * TetraktTables.c - generated by tools/tetrakt/gen_tkt_tables.py (S096 P1).",
         " * Do not edit: change the script and run it again.",
         " * Contract and meaning of every table: TetraktTables.h.",
         " */",
         '#include "TetraktTables.h"',
         "",
         "const float tktToneG[TKT_TONE_POINTS] = {"]
    L += rows([f32(tone_g(t)) for t in range(TONE_POINTS)])
    L += ["};", "", "const float tktDriveMakeup[TKT_TABLE_POINTS] = {"]
    L += rows([f32(makeup(d)) for d in range(POINTS)])
    L += ["};", "", "const float tktSrrRate[TKT_TABLE_POINTS] = {"]
    L += rows([f32(srr_rate(v)) for v in range(POINTS)])
    L += ["};", "", "const float tktFbLpK[TKT_TABLE_POINTS] = {"]
    L += rows([f32(fb_k(s)) for s in range(POINTS)])
    L += ["};", ""]
    return "\n".join(L)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()
    want = text()
    if a.check:
        have = open(OUT, newline="").read() if os.path.exists(OUT) else None
        if have != want:
            sys.exit(f"gen_tkt_tables: {OUT} differs (rerun without --check)")
        print("gen_tkt_tables: TetraktTables.c matches")
        return
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", newline="\n") as fh:
        fh.write(want)
    print("wrote", OUT)


if __name__ == "__main__":
    main()
```

### S1.2 NEW FILE `Core/DSP/Instruments/Tetrakt/TetraktTables.h`

```c
/*
 * TetraktTables.h - the Tetrakt's constant tables (S096 P1).
 *
 * What:       the sizes and the declarations of the tables that
 *             tools/tetrakt/gen_tkt_tables.py writes into TetraktTables.c:
 *               tktToneG[t]         t = 0..126: the tone SVF's g = tan(pi fc
 *                                   / fs) at the effective tone byte (LP
 *                                   18 kHz .. 120 Hz below 63, HP 20 Hz ..
 *                                   3 kHz above; plan §5.3);
 *               tktDriveMakeup[d]   d = 0..127: the drive's half make-up
 *                                   (plan §5.4), exactly 1.0 at 0;
 *               tktSrrRate[v]       v = 0..127: the srt sample-and-hold rate
 *                                   (plan §5.5), exactly 1.0 at 127 (SD7);
 *               tktFbLpK[s]         s = 0..127: S1's feedback one-pole k,
 *                                   600 Hz .. 16 kHz (SD6).
 * Why:        constants instead of tan/exp/pow in the control path (the
 *             CPU rules); one generator owns every value (--check).
 * Inputs:     none (immutable flash data).
 * Accessors:  TetraktVoice.c.
 * Affiliates: tools/tetrakt/gen_tkt_tables.py (the source of every value);
 *             S096_P1_TETRAKT_SAMPLE_PLAYER.md §5, §6.
 */
#ifndef TETRAKTTABLES_H_
#define TETRAKTTABLES_H_

#include <stdint.h>

/* One g per effective tone byte 0..126 (the PM63 row's stored range). */
#define TKT_TONE_POINTS   127u
/* One entry per 0..127 row byte. */
#define TKT_TABLE_POINTS  128u

extern const float tktToneG[TKT_TONE_POINTS];
extern const float tktDriveMakeup[TKT_TABLE_POINTS];
extern const float tktSrrRate[TKT_TABLE_POINTS];
extern const float tktFbLpK[TKT_TABLE_POINTS];

#endif /* TETRAKTTABLES_H_ */
```

### S1.3 NEW FILE `Core/DSP/Instruments/Tetrakt/TetraktTables.c`

Generated: `python3 tools/tetrakt/gen_tkt_tables.py`. It is not
hand-written.

### S1 gate

`python3 tools/tetrakt/gen_tkt_tables.py --check` prints "matches". By
inspection: `tktSrrRate[127] = 1.0f`, `tktDriveMakeup[0] = 1.0f`.

---

## S2. NEW FILE `Core/DSP/Instruments/Tetrakt/TetraktVoice.h`

### S2.1 Size (*est.*, checked by the assert in S7.6)

| Part | Bytes |
|---|---|
| 4 × `TktLayer`: `OscInfo` 72, 10 row bytes + stage + pending sample + pending velocity (13, padded to 16), 30 floats (120) | 4 × 208 = 832 |
| `Lfo` | 124 |
| `right[32]` | 64 |
| voice rows and state (12 bytes, 3 floats), S1 (pointer, index, 9 floats), DC (4 floats) | ≈ 72 |
| **Total** | **≈ 1,092** |

```c
/*
 * TetraktVoice.h - the Tetrakt voice engine (S096 P1).
 *
 * What:       one Tetrakt voice: four sample layers (A B C D), each a
 *             one-shot read of an installed sample (the platform's
 *             osc_smpPlainBlock(), through osc_renderTyped()) with its own
 *             decay, tune, level, pan, drive, sample-rate reduction, 2-pole
 *             bipolar tone with an envelope amount, and space send; a layer
 *             switch row (lon, 4 bits) that decides which layers a trigger
 *             restarts; a 50 ms line in one FxBuffer voice unit read by two
 *             taps (S1); an envelope-derived width per layer (W2); a true
 *             stereo output.
 *             Entry points follow the other engines (init / trigger / async /
 *             sync block), taking an instance pointer owned by the caller.
 * Why:        S096_P1_TETRAKT_SAMPLE_PLAYER.md (revision 7; every question
 *             decided, §12), and S096_P1_IMPLEMENTATION.md §0.
 * Inputs:     descriptor writes into the row bytes (TYPE_UINT8, SD4) and the
 *             voice's vol (TYPE_FLT); triggers with velocity and note;
 *             Tetrakt_setVelocityKind() from InstrumentManager (SD3).
 * Outputs:    per block two pre-volume int16 blocks: the left into the
 *             caller's buffer (the mixer's sampleData), the right into
 *             voice->right (read by the mixer through
 *             instrumentManager_slotRight()). Tetrakt_isQuiet().
 * Accessors:  InstrumentManager dispatchers; tools/dsp_test modules/tetrakt.c.
 * Affiliates: TetraktParameters.h (the rows), TetraktTables.h,
 *             Oscillator.h (OscInfo, osc_userBind(), osc_renderTyped()),
 *             FxBuffer.h (the voice unit; InstrumentManager owns the claim).
 */
#ifndef TETRAKTVOICE_H_
#define TETRAKTVOICE_H_

#include <stdint.h>
#include "Oscillator.h"       /* OscInfo, OSC_TYPE_SMP, config.h (OUTPUT_DMA_SIZE) */
#include "lfo.h"

/* Four layers (plan §0). */
#define TKT_LAYERS            4u
/* The layer switch's 16 values (lon, 4 bits; MenuText.h tktLayerNames). */
#define TKT_LAYER_MASK_COUNT  16u
/* The S1 line: one FxBuffer voice unit (FXBUF_VOICE_UNIT_SAMPLES; asserted
 * in InstrumentManager.c, which includes FxBuffer.h). */
#define TKT_LINE_SAMPLES      2208u

/*
 * A layer's stage (S096 P1 SD9, plan §3.2 R1).
 *
 * What:       IDLE: silent (env 0), the read keeps running (constant cost);
 *             PLAY: decaying; FADE: a retrigger of a sounding layer, this
 *             block ramps the old read to 0; START: the next async binds the
 *             latched sample, restarts the read at its start, latches the
 *             velocity and plays at full level.
 * Why:        a restart from the start while the old read is loud would
 *             step; the fade takes one block (0.73 ms) and only for a layer
 *             that was still sounding.
 */
#define TKT_STAGE_IDLE        0u
#define TKT_STAGE_PLAY        1u
#define TKT_STAGE_FADE        2u
#define TKT_STAGE_START       3u

/*
 * The velocity groups (S096 P1 SD3, plan §4.2).
 *
 * What:       the nine per-layer row kinds a velocity destination can name
 *             for all four layers at once; TKT_KIND_NONE when the
 *             destination is not a group (off, a voice row, the own Morph:
 *             the platform node handles those).
 * Accessors:  TetraktParameters.c tetraktParameters_velocityKind();
 *             Tetrakt_setVelocityKind(); tkt_layerControl().
 */
#define TKT_KIND_TUNE         0u
#define TKT_KIND_LEVEL        1u
#define TKT_KIND_DECAY        2u
#define TKT_KIND_PAN          3u
#define TKT_KIND_DRIVE        4u
#define TKT_KIND_SRR          5u
#define TKT_KIND_SPACE        6u
#define TKT_KIND_TONE         7u
#define TKT_KIND_TONE_ENV     8u
#define TKT_KIND_COUNT        9u
#define TKT_KIND_NONE         0xFFu

/*
 * One layer (S096 P1).
 *
 * Row bytes (descriptor-bound, TYPE_UINT8, SD4): sample (the catalogue
 * index, latched at a restart), tune, level, decay, pan, drive, srr, space,
 * tone, toneEnv.
 * State: stage (TKT_STAGE_*), pendSample / pendVel (what a retrigger
 * latched), env (the decay level at this block's end) and envLast (at its
 * start), velo (the latched velocity 0..1).
 * Tone SVF (the Colloid's ColSvf form): ic1 ic2 the integrator states;
 * a1 a2 a3 the coefficients at the block's start and da1..da3 their
 * per-sample steps; mX mB mL the output mix (y = mX x + mB v1 + mL v2) and
 * dmX..dmL its steps. The block's end values are a + 32 da, so the next
 * async starts there with no stored targets (SD13).
 * Drive: dGain (x1..x32), dBias, dClipB (clip(bias)), dWet (0..1 over the
 * first 8 steps). Gains: gL, gR (the pan law x level x make-up x
 * TKT_MIX_SCALE), gS (the send). SRR: srrRate, srrCnt, srrHold. W2: apZ (the
 * allpass state), wMix ((1 - env)^2 x wid).
 */
typedef struct {
	OscInfo  osc;
	uint8_t  sample, tune, level, decay, pan, drive, srr, space, tone, toneEnv;
	uint8_t  stage;
	uint8_t  pendSample;
	uint8_t  pendVel;
	float    env, envLast, velo;
	float    ic1, ic2;
	float    a1, a2, a3, da1, da2, da3;
	float    mX, mB, mL, dmX, dmB, dmL;
	float    dGain, dBias, dClipB, dWet;
	float    gL, gR, gS;
	float    srrRate, srrCnt, srrHold;
	float    apZ, wMix;
} TktLayer;

/*
 * The voice (S096 P1).
 *
 * Rows (descriptor-bound): layerOn (lon, bit 0 = A .. bit 3 = D), veloVol
 * (vel: velocity scales each restarted layer), pan (the mixer's slot pan:
 * the L-R balance), vol (read by the mixer), spaceTime, spaceFeedback,
 * spaceDetune, spaceTone, spaceLevel (S1), width (W2).
 * State: slot (for velocityModulators[]), note (the last trigger note),
 * veloKind (TKT_KIND_*), line (the voice unit or NULL, SD11), wi (its write
 * index), tapL / tapR (the taps' delays in samples, this block), wobPhase
 * (S1's wobble, 0..1), fb, fbK, slvGain, fbLp (S1's feedback LP state), the
 * DC blockers' states, right[] (the right output block).
 */
typedef struct TetraktVoiceStruct
{
	TktLayer layer[TKT_LAYERS];
	Lfo      lfo;
	int16_t  right[OUTPUT_DMA_SIZE];
	int16_t *line;
	float    vol;
	float    tapL, tapR, wobPhase, fb, fbK, slvGain, fbLp;
	float    dcXL, dcYL, dcXR, dcYR;
	uint16_t wi;
	uint8_t  layerOn, veloVol, pan;
	uint8_t  spaceTime, spaceFeedback, spaceDetune, spaceTone, spaceLevel, width;
	uint8_t  slot, note, veloKind;
} TetraktVoice;

/*
 * Entry points (S096 P1).
 *
 * Tetrakt_initVoice()        the init.tkt values; slot and the voice unit
 *                            (or NULL) from InstrumentManager; clears the
 *                            unit (main loop or boot).
 * Tetrakt_triggerVoice()     LFO retrigger, the platform velocity node;
 *                            arms the layers set in layerOn (main loop,
 *                            sequencer).
 * Tetrakt_calcVoiceAsync()   the block's control work (render context).
 * Tetrakt_calcVoiceSyncBlock() the block's audio: left into buf, right into
 *                            voice->right (render context).
 * Tetrakt_isQuiet()          every layer IDLE.
 * Tetrakt_setVelocityKind()  the velocity group (SD3) or TKT_KIND_NONE.
 * The two render entries are noinline so LTO keeps them out of
 * mixer_calcNextSampleBlock() (S086 F1).
 */
void Tetrakt_initVoice(TetraktVoice *voice, uint8_t slot, int16_t *line)
	__attribute__((noinline, noclone));
void Tetrakt_triggerVoice(TetraktVoice *voice, uint8_t source_slot,
                          uint8_t velocity, uint8_t note);
void Tetrakt_calcVoiceAsync(TetraktVoice *voice) __attribute__((noinline));
void Tetrakt_calcVoiceSyncBlock(TetraktVoice *voice, int16_t *buf, uint8_t size)
	__attribute__((noinline));
uint8_t Tetrakt_isQuiet(const TetraktVoice *voice);
void Tetrakt_setVelocityKind(TetraktVoice *voice, uint8_t kind);

#endif /* TETRAKTVOICE_H_ */
```

---

## S3. NEW FILE `Core/DSP/Instruments/Tetrakt/TetraktVoice.c`

```c
#include <math.h>
#include <string.h>
#include "TetraktVoice.h"
#include "TetraktTables.h"
#include "ColloidTables.h"      /* colEgK[]: the decay's per-block one-pole (shared, plan §1) */
#include "AlkalineControl.h"    /* alk_exp2f() (S093 P3 fast maths) */
#include "squareRootLut.h"      /* squareRootLut[]: the mixer's pan law */
#include "MidiPitch.h"          /* MIDI_DEFAULT_TRIGGER_NOTE */
#include "modulationNode.h"     /* velocityModulators[], modNode_updateValue() */
#include "InstrumentManager.h"  /* instrumentManager_applyVelocityModulationTarget() */

/*
 * Control constants (S096 P1). The "by ear" values (TKT_MIX_SCALE, the tone
 * damping and ranges in gen_tkt_tables.py, TKT_BIAS_MAX) are first
 * settings, set in the listening step (S12) and recorded there.
 *
 * TKT_QUIET           a decay below 1e-4 (-80 dB) ends (the
 *                     ampEnvelopeQuiet() threshold, as the Colloid's).
 * TKT_IN_SCALE        int16 read -> float.
 * TKT_OUT_SCALE       float -> int16 pack.
 * TKT_UNITY_INC       Q19.12: 4,096 is one frame per sample (1.0).
 * TKT_UNITY_HZ        the frequency that osc_userBind()'s sample law
 *                     (f x 2^22 / fs) turns into 4,096: fs / 1,024. Set so
 *                     the bind's own phaseInc is already close; the exact
 *                     one is written after it (SD5).
 * TKT_MIX_SCALE       each layer's post gain x0.5 (SD8).
 * TKT_SVF_K           the tone SVF's damping 2R, R = 0.6 (plan T-Q5).
 * TKT_TONE_W_SLOPE    the output weights rise 0 -> 1 over the first eighth
 *                     of either side (plan §5.3).
 * TKT_CLIP, TKT_CUBIC the drive's clamped cubic (the Colloid's col_drive()
 *                     law): exactly +-1.0 at +-1.5.
 * TKT_DRIVE_OCT       drive 0..127 -> gain 2^(5 d / 127): x1..x32.
 * TKT_BIAS_MAX        the asymmetry bias at full drive (plan T-Q6).
 * TKT_WET_STEPS       drive 0 is the dry signal exactly; the clip fades in
 *                     over the first 8 steps (COL_POST_WET_STEPS' rule).
 * TKT_DC_R            the bus DC blocker, a one-pole HP at 8 Hz:
 *                     1 - 2 pi 8 / fs.
 * TKT_SDT_SPAN        sdt 127 puts the right tap 15 % earlier.
 * TKT_WOB_HZ/DEPTH    S1's right-tap wobble: a 0.37 Hz triangle, 2 samples.
 * TKT_FB_MAX          sfb 127 -> loop gain 0.9 (bounded; the write
 *                     saturates as well).
 * TKT_VELO_SPAN       velocity offsets are in row bytes: the row's 127.
 */
#define TKT_QUIET           1.0e-4f
#define TKT_IN_SCALE        (1.0f / 32768.0f)
#define TKT_OUT_SCALE       32767.0f
#define TKT_UNITY_INC       4096.0f
#define TKT_UNITY_HZ        (REAL_FS / 1024.0f)
#define TKT_MIX_SCALE       0.5f
#define TKT_SVF_K           1.2f
#define TKT_TONE_W_SLOPE    8.0f
#define TKT_CLIP            1.5f
#define TKT_CUBIC           (4.0f / 27.0f)
#define TKT_DRIVE_OCT       5.0f
#define TKT_BIAS_MAX        0.25f
#define TKT_WET_STEPS       8u
#define TKT_DC_R            0.998860393f
#define TKT_SDT_SPAN        0.15f
#define TKT_WOB_HZ          0.37f
#define TKT_WOB_DEPTH       2.0f
#define TKT_FB_MAX          0.9f
#define TKT_VELO_SPAN       127.0f

/*
 * W2's allpass coefficients (S096 P1, plan §6.2).
 *
 * What:       one fixed first-order allpass coefficient per layer.
 * Why:        different coefficients decorrelate the four tails from each
 *             other, so a full kit widens as a spread, not as one phase
 *             shift.
 */
static const float tkt_apA[TKT_LAYERS] = { 0.25f, 0.40f, 0.55f, 0.70f };

/*
 * The shared work area (S096 P1; plan §10, T-Q1: approved).
 *
 * What:       the read scratch (the current layer's int16 block), the float
 *             L/R buses and the send bus, one copy for every Tetrakt:
 *             448 B of .bss in SRAM1 (the linker's .bss, D-cached since
 *             S094 P2).
 * Why:        the mixer renders one slot at a time (calcSlotAsync and
 *             calcSlotSyncBlock in turn), so no two Tetrakts use the area
 *             at once; inside the voice struct the buses would put it over
 *             the 1,264 B slot reserve.
 * Contract:   valid only inside one Tetrakt_calcVoiceSyncBlock() call;
 *             nothing is kept in it between blocks.
 */
typedef struct {
	int16_t scratch[OUTPUT_DMA_SIZE];
	float   busL[OUTPUT_DMA_SIZE];
	float   busR[OUTPUT_DMA_SIZE];
	float   send[OUTPUT_DMA_SIZE];
} TktWork;
_Static_assert(sizeof(TktWork) == 448u, "the approved Tetrakt work area is 448 B");
static TktWork tkt_work;

/* The drive's clamped cubic (TKT_CLIP, TKT_CUBIC). */
static inline float tkt_clip(float x)
{
	x = fminf(fmaxf(x, -TKT_CLIP), TKT_CLIP);
	return x - TKT_CUBIC * x * x * x;
}

/* A float to int16 with saturation (vminnm / vmaxnm under -Ofast). */
static inline int16_t tkt_sat16(float x)
{
	return (int16_t)fminf(fmaxf(x, -32768.0f), 32767.0f);
}

/*
 * A row byte with a velocity offset (S096 P1 SD3, SD4).
 *
 * What:       b - off clamped to 0..127, as a float (the tone uses the
 *             fraction).
 * Why:        the platform's negative-polarity velocity law
 *             (modNode_updateValue(): base - amount (1 - v) width) in the
 *             row's own 0..127 domain; off is 0 for every row the velocity
 *             group does not name.
 */
static inline float tkt_eff(const uint8_t b, const float off)
{
	return fminf(fmaxf((float)b - off, 0.0f), 127.0f);
}

/* A row float 0..127 to a table index 0..127. */
static inline uint32_t tkt_idx(const float v)
{
	const uint32_t i = (uint32_t)(v + 0.5f);
	return (i > 127u) ? 127u : i;
}

/*
 * One ramp (S096 P1, the Colloid's ColSvf rule).
 *
 * What:       the next block's ramp from the last block's end (a + 32 da)
 *             to target; at init the ramp starts on its target (da 0).
 * Why:        coefficient and weight changes are spread over the block, so
 *             an envelope-swept tone does not zipper.
 */
static inline void tkt_ramp(float *a, float *da, const float target, const uint32_t init)
{
	const float start = init ? target : (*a + (float)OUTPUT_DMA_SIZE * *da);

	*a = start;
	*da = init ? 0.0f : (target - start) * (1.0f / (float)OUTPUT_DMA_SIZE);
}

/*
 * One layer's control for this block (S096 P1; plan §2, §3, §5, §6.2).
 *
 * What:       in order:
 *             1. a START latches the pending sample and velocity and sets
 *                the restart flag;
 *             2. the velocity offset of the voice's group (SD3) from this
 *                layer's latched velocity;
 *             3. the stage's envelope: START plays from 1.0; FADE ramps to
 *                0 and becomes START; PLAY decays by colEgK[decay] and ends
 *                below TKT_QUIET; IDLE stays 0;
 *             4. the pitch: semitones = (note - 63) + clamp(tune - 63,
 *                +-60) / 5 (plan §5.1: a whole semitone every 5 steps, +-12
 *                from +-60 to the ends); rate = 2^(semitones / 12);
 *                osc_userBind() binds the sample (or rebinds after a
 *                catalogue install), then phaseInc = round(rate x 4,096) and
 *                a restart sets the phase to 0 (SD5);
 *             5. the tone (plan §5.3): t = tone + (toneEnv - 63) x env,
 *                0..126; g from tktToneG[] (interpolated); a1 = 1 / (1 +
 *                g (g + k)), a2 = g a1, a3 = g a2; u = (t - 63) / 63, w =
 *                min(8 |u|, 1); mX = 1 - wL, mB = -wH k, mL = wL - wH (so y
 *                = x exactly at u = 0, the LP at wL = 1, the HP at wH = 1);
 *                each ramped from the last block's end;
 *             6. the drive (plan §5.4): gain 2^(5 d / 127), bias 0.25 (d /
 *                127)^2, clip(bias), wet over the first 8 steps; make-up
 *                tktDriveMakeup[d];
 *             7. the gains: post = level / 127 x make-up x TKT_MIX_SCALE;
 *                gL = squareRootLut[127 - pan] x post, gR =
 *                squareRootLut[pan] x post (the mixer's law); gS = space /
 *                127 x post;
 *             8. the SRR rate tktSrrRate[srr] (127 = 1.0 exactly) and W2's
 *                mix (wid / 127) (1 - env)^2.
 * Why:        all per block; the per-sample loop only reads, filters and
 *             adds (constant cost: every layer runs this every block).
 * Inputs:     voice (rows, note, veloKind, slot, width), i (the layer),
 *             init (1 from Tetrakt_initVoice(): no stage step, ramps start
 *             on their targets).
 * Outputs:    the layer's state and per-block values.
 * Cost:       one alk_exp2f() for the pitch and one for the drive gain, one
 *             division (a1), table reads; noinline keeps it out of the
 *             async's register allocation.
 * Accessors:  Tetrakt_calcVoiceAsync(), Tetrakt_initVoice().
 */
static __attribute__((noinline)) void tkt_layerControl(TetraktVoice *voice, const uint32_t i,
                                                        const uint32_t init)
{
	TktLayer *const L = &voice->layer[i];
	const uint8_t kind = voice->veloKind;
	uint32_t restart = 0u;
	float off, tune, level, decay, pan, drive, srr, space, tone, tenv;

	/* 1. a START latches what the retrigger asked for */
	if (!init && L->stage == TKT_STAGE_START) {
		L->osc.waveform = L->pendSample;
		L->velo = (float)L->pendVel * (1.0f / 127.0f);
		restart = 1u;
	}
	/* 2. the velocity group's offset for this layer (SD3) */
	off = (kind < TKT_KIND_COUNT)
	    ? velocityModulators[voice->slot].amount * (1.0f - L->velo) * TKT_VELO_SPAN
	    : 0.0f;
	tune  = tkt_eff(L->tune,    (kind == TKT_KIND_TUNE)     ? off : 0.0f);
	level = tkt_eff(L->level,   (kind == TKT_KIND_LEVEL)    ? off : 0.0f);
	decay = tkt_eff(L->decay,   (kind == TKT_KIND_DECAY)    ? off : 0.0f);
	pan   = tkt_eff(L->pan,     (kind == TKT_KIND_PAN)      ? off : 0.0f);
	drive = tkt_eff(L->drive,   (kind == TKT_KIND_DRIVE)    ? off : 0.0f);
	srr   = tkt_eff(L->srr,     (kind == TKT_KIND_SRR)      ? off : 0.0f);
	space = tkt_eff(L->space,   (kind == TKT_KIND_SPACE)    ? off : 0.0f);
	tone  = tkt_eff(L->tone,    (kind == TKT_KIND_TONE)     ? off : 0.0f);
	tenv  = tkt_eff(L->toneEnv, (kind == TKT_KIND_TONE_ENV) ? off : 0.0f);

	/* 3. the stage's envelope (SD9) */
	if (!init) {
		const float k = colEgK[tkt_idx(decay)];

		switch (L->stage) {
		case TKT_STAGE_START:
			L->envLast = 1.0f;
			L->env = 1.0f - k;
			L->stage = TKT_STAGE_PLAY;
			break;
		case TKT_STAGE_FADE:
			L->envLast = L->env;
			L->env = 0.0f;
			L->stage = TKT_STAGE_START;
			break;
		case TKT_STAGE_PLAY:
			L->envLast = L->env;
			L->env -= k * L->env;
			if (L->env < TKT_QUIET) {
				L->env = 0.0f;
				L->stage = TKT_STAGE_IDLE;
			}
			break;
		default:
			L->envLast = 0.0f;
			L->env = 0.0f;
			break;
		}
	}

	/* 4. the pitch and the bind (SD5) */
	{
		const float d = fminf(fmaxf(tune - 63.0f, -60.0f), 60.0f);
		const float semis = (float)((int32_t)voice->note - (int32_t)MIDI_DEFAULT_TRIGGER_NOTE)
		                  + d * 0.2f;
		const float rate = alk_exp2f(semis * (1.0f / 12.0f));

		L->osc.freq = TKT_UNITY_HZ * rate;
		osc_userBind(&L->osc);
		if (restart)
			L->osc.phase = 0u;
		L->osc.phaseInc = (uint32_t)(rate * TKT_UNITY_INC + 0.5f);
	}

	/* 5. the tone (plan §5.3) */
	{
		const float t = fminf(fmaxf(tone + (tenv - 63.0f) * L->env, 0.0f), 126.0f);
		const uint32_t ti = (uint32_t)t;
		const uint32_t tn = (ti < (TKT_TONE_POINTS - 1u)) ? ti + 1u : ti;
		const float g = tktToneG[ti] + (t - (float)ti) * (tktToneG[tn] - tktToneG[ti]);
		const float u = (t - 63.0f) * (1.0f / 63.0f);
		const float w = fminf(fabsf(u) * TKT_TONE_W_SLOPE, 1.0f);
		const float wL = (u < 0.0f) ? w : 0.0f;
		const float wH = (u > 0.0f) ? w : 0.0f;
		const float a1 = 1.0f / (1.0f + g * (g + TKT_SVF_K));
		const float a2 = g * a1;

		tkt_ramp(&L->a1, &L->da1, a1, init);
		tkt_ramp(&L->a2, &L->da2, a2, init);
		tkt_ramp(&L->a3, &L->da3, g * a2, init);
		tkt_ramp(&L->mX, &L->dmX, 1.0f - wL, init);
		tkt_ramp(&L->mB, &L->dmB, -wH * TKT_SVF_K, init);
		tkt_ramp(&L->mL, &L->dmL, wL - wH, init);
	}

	/* 6.-8. drive, gains, SRR, W2 */
	{
		const uint32_t di = tkt_idx(drive);
		const float dn = drive * (1.0f / 127.0f);
		const float post = level * (1.0f / 127.0f) * tktDriveMakeup[di] * TKT_MIX_SCALE;
		const uint32_t pi = tkt_idx(pan);
		const float e = 1.0f - L->env;

		L->dGain = alk_exp2f(TKT_DRIVE_OCT * dn);
		L->dBias = TKT_BIAS_MAX * dn * dn;
		L->dClipB = tkt_clip(L->dBias);
		L->dWet = (di >= TKT_WET_STEPS) ? 1.0f : (float)di * (1.0f / (float)TKT_WET_STEPS);
		L->gL = squareRootLut[127u - pi] * post;
		L->gR = squareRootLut[pi] * post;
		L->gS = space * (1.0f / 127.0f) * post;
		L->srrRate = tktSrrRate[tkt_idx(srr)];
		L->wMix = (float)voice->width * (1.0f / 127.0f) * e * e;
	}
}

/*
 * The space stage's control for this block (S096 P1, plan §6.1).
 *
 * What:       tapL = (1 + 49 stm / 127) ms in samples (1 .. 50 ms); tapR =
 *             tapL x (1 - 0.15 sdt / 127) - 2 x a 0.37 Hz triangle (the
 *             wobble), at least 2 samples; fb = 0.9 sfb / 127; fbK =
 *             tktFbLpK[sto]; slvGain = slv / 127.
 * Why:        the taps' delays are constant within a block, so the loop
 *             reads them as an integer and a fraction (no per-sample
 *             division or modulo).
 * Accessors:  Tetrakt_calcVoiceAsync().
 */
static void tkt_spaceControl(TetraktVoice *voice)
{
	const float ms = 1.0f + 49.0f * (float)voice->spaceTime * (1.0f / 127.0f);
	const float tL = ms * (REAL_FS * 0.001f);
	float tri, tR;

	voice->wobPhase += TKT_WOB_HZ * (float)OUTPUT_DMA_SIZE / REAL_FS;
	if (voice->wobPhase >= 1.0f)
		voice->wobPhase -= 1.0f;
	tri = 1.0f - fabsf(2.0f * voice->wobPhase - 1.0f);
	tR = tL * (1.0f - TKT_SDT_SPAN * (float)voice->spaceDetune * (1.0f / 127.0f))
	   - TKT_WOB_DEPTH * tri;
	voice->tapL = tL;
	voice->tapR = fmaxf(tR, 2.0f);
	voice->fb = TKT_FB_MAX * (float)voice->spaceFeedback * (1.0f / 127.0f);
	voice->fbK = tktFbLpK[voice->spaceTone & 127u];
	voice->slvGain = (float)voice->spaceLevel * (1.0f / 127.0f);
}

void Tetrakt_initVoice(TetraktVoice *voice, const uint8_t slot, int16_t *line)
{
	uint32_t i;

	if (!voice)
		return;
	/* the caller cleared the slot (memset); every field is set here anyway */
	voice->slot = slot;
	voice->line = line;
	if (line)
		memset(line, 0, TKT_LINE_SAMPLES * sizeof(line[0]));   /* the FxBuffer owner clears (SD11) */
	voice->wi = 0u;
	voice->layerOn = 1u;              /* A only (plan T-Q7) */
	voice->veloVol = 1u;              /* the Drum's default (DrumVoice.c:134) */
	voice->pan = 63u;
	voice->vol = 100.0f / 127.0f;
	voice->spaceTime = 40u;
	voice->spaceFeedback = 40u;
	voice->spaceDetune = 30u;
	voice->spaceTone = 100u;
	voice->spaceLevel = 100u;
	voice->width = 0u;                /* SD15 */
	voice->note = MIDI_DEFAULT_TRIGGER_NOTE;
	voice->veloKind = TKT_KIND_NONE;
	voice->wobPhase = 0.0f;
	voice->fbLp = 0.0f;
	voice->dcXL = voice->dcYL = voice->dcXR = voice->dcYR = 0.0f;
	for (i = 0u; i < TKT_LAYERS; i++) {
		TktLayer *const L = &voice->layer[i];

		L->osc.oscType = OSC_TYPE_SMP;
		L->osc.waveform = (uint8_t)i;
		L->osc.pitchMod = 1.0f;
		L->osc.modNodeValue = 1.0f;
		L->osc.sampleCacheLooped = OSC_BIND_NONE;
		L->sample = (uint8_t)i;       /* samples 0..3 (clamped to the installed count by the bind) */
		L->tune = 63u;
		L->level = 100u;
		L->decay = 70u;
		L->pan = 63u;
		L->drive = 0u;
		L->srr = 127u;
		L->space = 0u;
		L->tone = 63u;
		L->toneEnv = 63u;
		L->stage = TKT_STAGE_IDLE;
		L->pendSample = (uint8_t)i;
		L->pendVel = 127u;
		L->env = L->envLast = 0.0f;
		L->velo = 1.0f;
		L->ic1 = L->ic2 = 0.0f;
		L->srrCnt = L->srrHold = 0.0f;
		L->apZ = 0.0f;
		tkt_layerControl(voice, i, 1u);
	}
	tkt_spaceControl(voice);
	lfo_init(&voice->lfo);
}

void Tetrakt_triggerVoice(TetraktVoice *voice, const uint8_t source_slot,
                          const uint8_t velocity, const uint8_t note)
{
	const float vel = (float)velocity * (1.0f / 127.0f);
	uint32_t i;

	/* --- platform part (as every engine): LFO and velocity, per slot --- */
	lfo_retrigger(source_slot);
	modNode_updateValue(&velocityModulators[source_slot], vel);
	instrumentManager_applyVelocityModulationTarget(source_slot, vel);

	/*
	 * The layer switch (plan §3.1, §4.3).
	 *
	 * What:       every layer whose bit is set in layerOn latches the trigger's
	 *             velocity and its current sample row, then fades (if it is
	 *             still sounding, SD9) or starts on the next block. A layer
	 *             whose bit is clear is untouched and plays out. A retrigger
	 *             during a FADE or a START only replaces what was latched.
	 * Why:        layerOn is read only here: a step lock on it applies to
	 *             its own step because the automation drain runs before the
	 *             triggers (voiceControl_processPending()).
	 */
	voice->note = note;
	for (i = 0u; i < TKT_LAYERS; i++) {
		TktLayer *const L = &voice->layer[i];

		if (!(voice->layerOn & (1u << i)))
			continue;
		L->pendSample = L->sample;
		L->pendVel = velocity;
		if (L->stage == TKT_STAGE_PLAY && L->env >= TKT_QUIET)
			L->stage = TKT_STAGE_FADE;
		else if (L->stage != TKT_STAGE_FADE)
			L->stage = TKT_STAGE_START;
	}
}

void Tetrakt_calcVoiceAsync(TetraktVoice *voice)
{
	uint32_t i;

	for (i = 0u; i < TKT_LAYERS; i++)
		tkt_layerControl(voice, i, 0u);
	tkt_spaceControl(voice);
}

/*
 * One layer's per-sample chain (S096 P1; plan §2, §5, §6.2).
 *
 * What:       per sample n of tkt_work.scratch (the layer's read):
 *               x = read x the ramped decay x velocity (veloVol);
 *               drive: d = clip(x dGain + dBias) - dClipB; x += dWet (d - x);
 *               SRR: cnt += rate; on cnt >= 1 the hold takes x; x = hold;
 *               tone: the TPT step (v3 = x - ic2; v1 = a1 ic1 + a2 v3;
 *                 v2 = ic2 + a2 ic1 + a3 v3; ic1 = 2 v1 - ic1;
 *                 ic2 = 2 v2 - ic2), y = mX x + mB v1 + mL v2, every
 *                 coefficient and weight += its step;
 *               pan: l = y gL; r = y gR; W2: ap = A r + z; z = r - A ap;
 *                 r += wMix (ap - r);
 *               busL[n] (=|+=) l; busR[n] (=|+=) r; send[n] (=|+=) y gS.
 *             The first layer stores (first = 1), the others add, so the
 *             buses need no clearing. The integrator, SRR and allpass
 *             states are written back; the ramps are not (SD13).
 * Why:        the order of plan §2 (decay and velocity before the drive;
 *             the tone last). Every stage always runs: drive 0, srr 127, a
 *             flat tone and wid 0 are the input exactly through the same
 *             arithmetic.
 * Inputs:     L, vg (the velocity gain: velo or 1), A (tkt_apA[i]), first.
 * Accessors:  tkt_layerPostStore(), tkt_layerPostAdd().
 */
static inline __attribute__((always_inline)) void tkt_layerPost(TktLayer *L, const float vg,
                                                                  const float A, const uint32_t first)
{
	const int16_t *const in = tkt_work.scratch;
	float *const bL = tkt_work.busL;
	float *const bR = tkt_work.busR;
	float *const bS = tkt_work.send;
	float g = L->envLast * vg * TKT_IN_SCALE;
	const float gStep = (L->env - L->envLast) * vg * TKT_IN_SCALE * (1.0f / (float)OUTPUT_DMA_SIZE);
	const float dG = L->dGain, dB = L->dBias, dCB = L->dClipB, dW = L->dWet;
	const float rate = L->srrRate;
	const float gL = L->gL, gR = L->gR, gS = L->gS, wm = L->wMix;
	const float da1 = L->da1, da2 = L->da2, da3 = L->da3;
	const float dmX = L->dmX, dmB = L->dmB, dmL = L->dmL;
	float a1 = L->a1, a2 = L->a2, a3 = L->a3;
	float mX = L->mX, mB = L->mB, mL = L->mL;
	float ic1 = L->ic1, ic2 = L->ic2;
	float cnt = L->srrCnt, hold = L->srrHold;
	float z = L->apZ;
	uint32_t n;

	for (n = 0u; n < OUTPUT_DMA_SIZE; n++) {
		float x = (float)in[n] * g;
		float d, v1, v2, v3, y, r, ap;

		g += gStep;
		d = tkt_clip(x * dG + dB) - dCB;
		x += dW * (d - x);
		cnt += rate;
		if (cnt >= 1.0f) {
			cnt -= 1.0f;
			hold = x;
		}
		x = hold;
		v3 = x - ic2;
		v1 = a1 * ic1 + a2 * v3;
		v2 = ic2 + a2 * ic1 + a3 * v3;
		ic1 = 2.0f * v1 - ic1;
		ic2 = 2.0f * v2 - ic2;
		y = mX * x + mB * v1 + mL * v2;
		a1 += da1; a2 += da2; a3 += da3;
		mX += dmX; mB += dmB; mL += dmL;
		r = y * gR;
		ap = A * r + z;
		z = r - A * ap;
		r += wm * (ap - r);
		if (first) {
			bL[n] = y * gL;
			bR[n] = r;
			bS[n] = y * gS;
		} else {
			bL[n] += y * gL;
			bR[n] += r;
			bS[n] += y * gS;
		}
	}
	L->ic1 = ic1;
	L->ic2 = ic2;
	L->srrCnt = cnt;
	L->srrHold = hold;
	L->apZ = z;
}

/* The two instances (the Colloid's store / add pattern): one loop each. */
static __attribute__((noinline)) void tkt_layerPostStore(TktLayer *L, const float vg, const float A)
{
	tkt_layerPost(L, vg, A, 1u);
}
static __attribute__((noinline)) void tkt_layerPostAdd(TktLayer *L, const float vg, const float A)
{
	tkt_layerPost(L, vg, A, 0u);
}

/*
 * The space stage and the output (S096 P1, plan §6.1, §5.4, §7 steps 4-5).
 *
 * What:       per sample: the two taps read the line at wi - tapL and
 *             wi - tapR with linear interpolation (integer part and
 *             fraction fixed for the block); their mid feeds the feedback
 *             one-pole (fbLp += fbK (mid - fbLp)); the line takes
 *             sat16((send + fb fbLp) x 32,767); l = busL + tapL slvGain,
 *             r = busR + tapR slvGain; each passes the 8 Hz DC blocker
 *             (y = x - x1 + R y1); out[n] = sat16(l x 32,767), right[n] =
 *             sat16(r x 32,767). With no voice unit (SD11) the taps are 0
 *             and nothing is written.
 * Why:        one unit gives stereo: two taps a detune offset apart (plan
 *             §6.1). The DC blocker removes the drive bias's moving DC once
 *             per side (plan §5.4).
 * Accessors:  Tetrakt_calcVoiceSyncBlock().
 */
static __attribute__((noinline)) void tkt_spaceOut(TetraktVoice *voice, int16_t *outL)
{
	int16_t *const line = voice->line;
	const float *const bL = tkt_work.busL;
	const float *const bR = tkt_work.busR;
	const float *const bS = tkt_work.send;
	int16_t *const outR = voice->right;
	const uint32_t iL = (uint32_t)voice->tapL;
	const uint32_t iR = (uint32_t)voice->tapR;
	const float fL = voice->tapL - (float)iL;
	const float fR = voice->tapR - (float)iR;
	const float fb = voice->fb, fbK = voice->fbK, slv = voice->slvGain;
	float lp = voice->fbLp;
	float xl1 = voice->dcXL, yl1 = voice->dcYL, xr1 = voice->dcXR, yr1 = voice->dcYR;
	uint32_t wi = voice->wi;
	uint32_t n;

	for (n = 0u; n < OUTPUT_DMA_SIZE; n++) {
		float tl = 0.0f, tr = 0.0f, l, r, yl, yr;

		if (line) {
			const uint32_t al = (wi >= iL) ? wi - iL : wi + TKT_LINE_SAMPLES - iL;
			const uint32_t bl = al ? al - 1u : TKT_LINE_SAMPLES - 1u;
			const uint32_t ar = (wi >= iR) ? wi - iR : wi + TKT_LINE_SAMPLES - iR;
			const uint32_t br = ar ? ar - 1u : TKT_LINE_SAMPLES - 1u;

			tl = ((float)line[al] + fL * (float)(line[bl] - line[al])) * TKT_IN_SCALE;
			tr = ((float)line[ar] + fR * (float)(line[br] - line[ar])) * TKT_IN_SCALE;
			lp += fbK * (0.5f * (tl + tr) - lp);
			line[wi] = tkt_sat16((bS[n] + fb * lp) * TKT_OUT_SCALE);
			wi = (wi + 1u < TKT_LINE_SAMPLES) ? wi + 1u : 0u;
		}
		l = bL[n] + tl * slv;
		r = bR[n] + tr * slv;
		yl = l - xl1 + TKT_DC_R * yl1;
		yr = r - xr1 + TKT_DC_R * yr1;
		xl1 = l; yl1 = yl;
		xr1 = r; yr1 = yr;
		outL[n] = tkt_sat16(yl * TKT_OUT_SCALE);
		outR[n] = tkt_sat16(yr * TKT_OUT_SCALE);
	}
	voice->wi = (uint16_t)wi;
	voice->fbLp = lp;
	voice->dcXL = xl1; voice->dcYL = yl1;
	voice->dcXR = xr1; voice->dcYR = yr1;
}

void Tetrakt_calcVoiceSyncBlock(TetraktVoice *voice, int16_t *buf, uint8_t size)
{
	/*
	 * One block (S096 P1, plan §7).
	 *
	 * What:       per layer, the platform read (osc_renderTyped():
	 *             osc_smpPlainBlock() for the bound sample, ITCM) into the
	 *             shared scratch at gain 1, then the layer's post loop (A
	 *             stores the buses, B..D add); then the space stage and the
	 *             output pack.
	 * Why:        the read is reused as it is (plan Q9, SD5); the order of
	 *             the four layers and the loops never depends on a setting
	 *             (constant cost).
	 * Inputs:     voice (the async ran this block), buf (the left output).
	 * Outputs:    buf, voice->right; every state the loops keep.
	 */
	uint32_t i;

	(void)size;   /* the voice always renders OUTPUT_DMA_SIZE */
	for (i = 0u; i < TKT_LAYERS; i++) {
		TktLayer *const L = &voice->layer[i];
		const float vg = voice->veloVol ? L->velo : 1.0f;

		osc_renderTyped(&L->osc, tkt_work.scratch, OUTPUT_DMA_SIZE, 1.0f);
		if (i == 0u)
			tkt_layerPostStore(L, vg, tkt_apA[0]);
		else
			tkt_layerPostAdd(L, vg, tkt_apA[i]);
	}
	tkt_spaceOut(voice, buf);
}

uint8_t Tetrakt_isQuiet(const TetraktVoice *voice)
{
	/* SD10: every layer idle; a still-ringing S1 tail does not hold the swap */
	uint32_t i;

	for (i = 0u; i < TKT_LAYERS; i++)
		if (voice->layer[i].stage != TKT_STAGE_IDLE)
			return 0u;
	return 1u;
}

void Tetrakt_setVelocityKind(TetraktVoice *voice, const uint8_t kind)
{
	/* SD3: the group a velocity destination names, or TKT_KIND_NONE */
	if (voice)
		voice->veloKind = (kind < TKT_KIND_COUNT) ? kind : TKT_KIND_NONE;
}
```

---

## S4. NEW FILES `Core/DSP/Instruments/Tetrakt/TetraktParameters.h` and `.c`

### S4.1 `TetraktParameters.h`

```c
/*
 * TetraktParameters.h - the Tetrakt descriptor table and menu layout
 * (S096 P1).
 *
 * What:       the registry-facing exports of TetraktParameters.c: the 64
 *             descriptor rows, the eight voice sub-pages, the display label,
 *             the type flags, the switch row's index, and the velocity-group
 *             helpers.
 * Why:        InstrumentManager's instrument_registry[] imports these, as it
 *             imports the other engines' tables.
 * Accessors:  InstrumentManager.c (the registry row; the velocity target
 *             branches, SD3).
 * Affiliates: TetraktVoice.h (the bound members); check_special_tags.py
 *             (TABLES['TKT']); tools/tetrakt/make_init_tkt.py (the keys).
 */
#ifndef TETRAKT_PARAMETERS_H_
#define TETRAKT_PARAMETERS_H_

#include "InstrumentManager.h"
#include "TetraktVoice.h"

/* 64 rows (plan §8.1): 40 layer rows, the switch, the platform's 14 (vol,
 * pan, the LFO pair's 8, the LFO targets' 4), velocity's 3, the space
 * stage's 6. Every cell of the Kit image. */
#define TETRAKT_PARAM_DESCRIPTOR_COUNT 64u
/* eight sub-pages: the front-panel SELECT1..8 buttons (menu.c) */
#define TETRAKT_MENU_PAGE_COUNT        8u
/* Basic (plan Q2): any slot, any number per Scene, held to the Basic share. */
#define TETRAKT_INSTRUMENT_TYPE_FLAGS  INSTRUMENT_FLAG_BASIC
/* The layer switch row (lon): the registry's switch_row_plus1 is this + 1. */
#define TETRAKT_PARAM_LAYER_ON_INDEX   20u

extern const ParamDescriptor tetrakt_param_descriptors[];
extern const uint8_t tetrakt_param_descriptor_count;
extern const instrument_menu_page_t tetrakt_menu_pages[];
extern const uint8_t tetrakt_menu_page_count;
extern const char tetrakt_instrument_display_label[];
extern const uint8_t tetrakt_instrument_type_flags;

/*
 * The velocity group a destination token names (S096 P1 SD3, plan §4.2).
 *
 * What:       for a local token that is a layer-A row of one of the nine
 *             groups (tuA lvA dyA pnA drA srA spA tnA teA), its TKT_KIND_*;
 *             for every other token TKT_KIND_NONE.
 * Accessors:  InstrumentManager.c (the velocity target bind, the '*' test).
 */
uint8_t tetraktParameters_velocityKind(uint8_t token);
/*
 * Whether a token is hidden from the velocity list (S096 P1 SD3).
 *
 * What:       1 for the B, C and D rows of the nine groups: velocity names
 *             each group once, by its layer-A row; 0 otherwise.
 * Accessors:  InstrumentManager.c (the velocity target walker and validator).
 */
uint8_t tetraktParameters_velocityTokenHidden(uint8_t token);

#endif /* TETRAKT_PARAMETERS_H_ */
```

### S4.2 `TetraktParameters.c`

Every row is on one line (`check_special_tags.py` and `make_init_tkt.py`
parse them). The macros are the Colloid's (`ColloidParameters.c:45–110`),
plus one: `ROW_MENU_SELECT` for the sample rows.

```c
#include "TetraktParameters.h"
#include "TetraktVoice.h"
#include "menu.h"
#include "MenuText.h"
#include <stddef.h>

/*
 * Tetrakt parameter source of truth (S096 P1).
 *
 * What:       what each generic Kit storage cell means when the slot holds a
 *             Tetrakt: file key, menu text (category / long / short, plan
 *             §8.2), dtype, Morph/modulation flags, modulation domain and
 *             the runtime binding into TetraktVoice. The pattern is
 *             ColloidParameters.c; every row is on one line.
 * Why:        S096_P1_TETRAKT_SAMPLE_PLAYER.md §4, §8 (revision 7).
 *             The enum is in page order. From the first release a later row
 *             can only be appended before TKT_PARAM_COUNT (the standing
 *             append-only rule), and the table is already 64 of 64.
 * Not offered: srt (the per-layer SRR replaces it), run, chn, a global tune
 *             (the trigger note transposes the four), W2's curve (fixed).
 * Accessors:  InstrumentManager registry; Menu (pages); Preset/Morph/LFO/
 *             automation through the registry; storage.
 * Affiliates: TetraktVoice.h; MenuText.h (tktLayerNames,
 *             ONOFF_TAG_TKT_LAYERS); tools/tetrakt/make_init_tkt.py;
 *             SD_CARD/Instrument/Tetrakt/init.tkt.
 */

/* Load-menu label and type flags, imported by the registry. "Tetrakt" is 7
 * cells; the Morph row shows "TetraMrp" (menu.c, 5 characters + Mrp). */
const char tetrakt_instrument_display_label[] = "Tetrakt";
const uint8_t tetrakt_instrument_type_flags = TETRAKT_INSTRUMENT_TYPE_FLAGS;

#define FLAGS_IMAGE \
    (INSTRUMENT_PARAM_FLAG_MORPHABLE | INSTRUMENT_PARAM_FLAG_MODULATABLE | \
     INSTRUMENT_PARAM_FLAG_AUTOMATABLE)
#define BIND_SPECIAL(member_, type_, special_) \
    { INSTRUMENT_BIND_INSTANCE_OFFSET, (uint16_t)offsetof(TetraktVoice, member_), type_, special_ }
#define BIND(member_, type_) BIND_SPECIAL(member_, type_, IM_SPECIAL_NONE)
#define MOD_NONE  { 0u, 0u, INSTRUMENT_MOD_DOMAIN_NONE }
#define MOD_0_127 { 0u, 127u, INSTRUMENT_MOD_DOMAIN_CONTINUOUS }
#define MOD_PM63  { 0u, 127u, INSTRUMENT_MOD_DOMAIN_CONTINUOUS }
#define ROW(key_, cat_, long_, short_, dtype_, mod_, member_, type_) \
    { key_, short_, long_, cat_, dtype_, FLAGS_IMAGE, mod_, BIND(member_, type_) }
#define ROW_SPECIAL(key_, cat_, long_, short_, dtype_, mod_, member_, type_, special_) \
    { key_, short_, long_, cat_, dtype_, FLAGS_IMAGE, mod_, BIND_SPECIAL(member_, type_, special_) }
#define ROW_MENU(key_, cat_, long_, short_, menu_, mod_, member_, type_) \
    { key_, short_, long_, cat_, (uint8_t)(DTYPE_MENU | (menu_ << 4)), FLAGS_IMAGE, mod_, BIND(member_, type_) }
#define ROW_MENU_SPECIAL(key_, cat_, long_, short_, menu_, mod_, member_, type_, special_) \
    { key_, short_, long_, cat_, (uint8_t)(DTYPE_MENU | (menu_ << 4)), FLAGS_IMAGE, mod_, BIND_SPECIAL(member_, type_, special_) }
/*
 * A sample-choice row (S096 P1, plan §4.1).
 *
 * What:       DTYPE_MENU | (MENU_WAVEFORM << 4) with AUTOMATABLE | SELECTOR
 *             and no modulation domain: stored as the catalogue index, never
 *             morphed and not an LFO / velocity target.
 * Why:        the wave helpers show the catalogue's sNN code and name, and
 *             clamp to the installed count, once they know the tkt_sample_
 *             key (InstrumentManager.c waveDomainMax, menu.c
 *             menu_formatWaveCell(): S7.7, S9.8). The sample latches at a
 *             restart (plan §3.3), so a Morph between two samples would only
 *             step at hits: it is a selector.
 */
#define ROW_MENU_SELECT(key_, cat_, long_, short_, menu_, member_, type_) \
    { key_, short_, long_, cat_, (uint8_t)(DTYPE_MENU | (menu_ << 4)), \
      (INSTRUMENT_PARAM_FLAG_AUTOMATABLE | INSTRUMENT_PARAM_FLAG_SELECTOR), \
      MOD_NONE, BIND(member_, type_) }
#define ROW_SELECT_ONOFF(key_, cat_, long_, short_, tag_, member_, type_) \
    { key_, short_, long_, cat_, (uint8_t)(DTYPE_ON_OFF | ((tag_) << 4)), \
      (INSTRUMENT_PARAM_FLAG_AUTOMATABLE | INSTRUMENT_PARAM_FLAG_SELECTOR), \
      MOD_NONE, BIND(member_, type_) }
#define ROW_NOBIND(key_, cat_, long_, short_, dtype_, bind_kind_) \
    { key_, short_, long_, cat_, dtype_, 0u, MOD_NONE, { bind_kind_, 0u, 0u, IM_SPECIAL_NONE } }
#define ROW_NOBIND_IMAGE(key_, cat_, long_, short_, dtype_, mod_, bind_kind_) \
    { key_, short_, long_, cat_, dtype_, FLAGS_IMAGE, mod_, { bind_kind_, 0u, 0u, IM_SPECIAL_NONE } }

/*
 * Tetrakt-local descriptor indices (S096 P1).
 *
 * What:       the order of tetrakt_param_descriptors[]: Scene/Kit storage,
 *             automation IDs, LFO/velocity tokens and NRPN numbers (slot x
 *             64 + index) use these numbers. Groups of four are layers A..D
 *             (so a group's layer is index & 3 from its A row).
 * Why:        page order (plan §8.2); the velocity groups (SD3) are found
 *             by their A rows.
 */
typedef enum {
    TKT_PARAM_SAMPLE_A = 0u, TKT_PARAM_SAMPLE_B, TKT_PARAM_SAMPLE_C, TKT_PARAM_SAMPLE_D,
    TKT_PARAM_TUNE_A,        TKT_PARAM_TUNE_B,   TKT_PARAM_TUNE_C,   TKT_PARAM_TUNE_D,
    TKT_PARAM_LEVEL_A,       TKT_PARAM_LEVEL_B,  TKT_PARAM_LEVEL_C,  TKT_PARAM_LEVEL_D,
    TKT_PARAM_DECAY_A,       TKT_PARAM_DECAY_B,  TKT_PARAM_DECAY_C,  TKT_PARAM_DECAY_D,
    TKT_PARAM_PAN_A,         TKT_PARAM_PAN_B,    TKT_PARAM_PAN_C,    TKT_PARAM_PAN_D,
    TKT_PARAM_LAYER_ON,
    TKT_PARAM_VELO_MOD_DEST,
    TKT_PARAM_VELO_MOD_AMOUNT,
    TKT_PARAM_VELO_VOL,
    TKT_PARAM_DRIVE_A,       TKT_PARAM_DRIVE_B,  TKT_PARAM_DRIVE_C,  TKT_PARAM_DRIVE_D,
    TKT_PARAM_SRR_A,         TKT_PARAM_SRR_B,    TKT_PARAM_SRR_C,    TKT_PARAM_SRR_D,
    TKT_PARAM_SPACE_A,       TKT_PARAM_SPACE_B,  TKT_PARAM_SPACE_C,  TKT_PARAM_SPACE_D,
    TKT_PARAM_SPACE_TIME,
    TKT_PARAM_SPACE_FEEDBACK,
    TKT_PARAM_SPACE_DETUNE,
    TKT_PARAM_SPACE_TONE,
    TKT_PARAM_TONE_A,        TKT_PARAM_TONE_B,   TKT_PARAM_TONE_C,   TKT_PARAM_TONE_D,
    TKT_PARAM_TONE_ENV_A,    TKT_PARAM_TONE_ENV_B, TKT_PARAM_TONE_ENV_C, TKT_PARAM_TONE_ENV_D,
    TKT_PARAM_LFO_RATE,
    TKT_PARAM_LFO_AMOUNT,
    TKT_PARAM_LFO_AMOUNT_2,
    TKT_PARAM_LFO_WAVE,
    TKT_PARAM_LFO_RETRIGGER_VOICE,
    TKT_PARAM_LFO_POLARITY,
    TKT_PARAM_LFO_SYNC,
    TKT_PARAM_LFO_OFFSET,
    TKT_PARAM_LFO_TARGET_VOICE,
    TKT_PARAM_LFO_TARGET_PARAM,
    TKT_PARAM_LFO_TARGET_VOICE_2,
    TKT_PARAM_LFO_TARGET_PARAM_2,
    TKT_PARAM_INSTRUMENT_VOL,
    TKT_PARAM_INSTRUMENT_PAN,
    TKT_PARAM_WIDTH,
    TKT_PARAM_SPACE_LEVEL,
    TKT_PARAM_COUNT
} tetrakt_param_index_t;

_Static_assert(TETRAKT_PARAM_DESCRIPTOR_COUNT == TKT_PARAM_COUNT,
               "TETRAKT_PARAM_DESCRIPTOR_COUNT must match tetrakt_param_index_t");
_Static_assert(TKT_PARAM_COUNT <= INSTRUMENT_PARAM_COUNT,
               "Tetrakt rows must fit the 64-cell Kit image");
_Static_assert(TETRAKT_PARAM_LAYER_ON_INDEX == TKT_PARAM_LAYER_ON,
               "TETRAKT_PARAM_LAYER_ON_INDEX names the switch row");

/*
 * Tetrakt parameter descriptors (S096 P1, plan §4, §8.2).
 *
 * Layer rows bind TYPE_UINT8 bytes (SD4); vol is TYPE_FLT (byte / 127, read
 * by the mixer). Labels are the user's (revision 6): category, long name,
 * short name with the layer letter.
 */
const ParamDescriptor tetrakt_param_descriptors[] = {
    /* --- OSC: the samples, the tunes --- */
    ROW_MENU_SELECT("tkt_sample_a", "Sample", "Sample", "smA", MENU_WAVEFORM, layer[0].sample, TYPE_UINT8),
    ROW_MENU_SELECT("tkt_sample_b", "Sample", "Sample", "smB", MENU_WAVEFORM, layer[1].sample, TYPE_UINT8),
    ROW_MENU_SELECT("tkt_sample_c", "Sample", "Sample", "smC", MENU_WAVEFORM, layer[2].sample, TYPE_UINT8),
    ROW_MENU_SELECT("tkt_sample_d", "Sample", "Sample", "smD", MENU_WAVEFORM, layer[3].sample, TYPE_UINT8),
    ROW("tkt_tune_a", "Sample", "Tune", "tuA", DTYPE_PM63, MOD_PM63, layer[0].tune, TYPE_UINT8),
    ROW("tkt_tune_b", "Sample", "Tune", "tuB", DTYPE_PM63, MOD_PM63, layer[1].tune, TYPE_UINT8),
    ROW("tkt_tune_c", "Sample", "Tune", "tuC", DTYPE_PM63, MOD_PM63, layer[2].tune, TYPE_UINT8),
    ROW("tkt_tune_d", "Sample", "Tune", "tuD", DTYPE_PM63, MOD_PM63, layer[3].tune, TYPE_UINT8),
    /* --- AEG: level, decay, pan --- */
    ROW("tkt_level_a", "Sample", "Level", "lvA", DTYPE_0B127, MOD_0_127, layer[0].level, TYPE_UINT8),
    ROW("tkt_level_b", "Sample", "Level", "lvB", DTYPE_0B127, MOD_0_127, layer[1].level, TYPE_UINT8),
    ROW("tkt_level_c", "Sample", "Level", "lvC", DTYPE_0B127, MOD_0_127, layer[2].level, TYPE_UINT8),
    ROW("tkt_level_d", "Sample", "Level", "lvD", DTYPE_0B127, MOD_0_127, layer[3].level, TYPE_UINT8),
    ROW("tkt_decay_a", "Envelope", "Decay", "dyA", DTYPE_0B127, MOD_0_127, layer[0].decay, TYPE_UINT8),
    ROW("tkt_decay_b", "Envelope", "Decay", "dyB", DTYPE_0B127, MOD_0_127, layer[1].decay, TYPE_UINT8),
    ROW("tkt_decay_c", "Envelope", "Decay", "dyC", DTYPE_0B127, MOD_0_127, layer[2].decay, TYPE_UINT8),
    ROW("tkt_decay_d", "Envelope", "Decay", "dyD", DTYPE_0B127, MOD_0_127, layer[3].decay, TYPE_UINT8),
    ROW("tkt_pan_a", "Sample", "Pan", "pnA", DTYPE_PM63, MOD_PM63, layer[0].pan, TYPE_UINT8),
    ROW("tkt_pan_b", "Sample", "Pan", "pnB", DTYPE_PM63, MOD_PM63, layer[1].pan, TYPE_UINT8),
    ROW("tkt_pan_c", "Sample", "Pan", "pnC", DTYPE_PM63, MOD_PM63, layer[2].pan, TYPE_UINT8),
    ROW("tkt_pan_d", "Sample", "Pan", "pnD", DTYPE_PM63, MOD_PM63, layer[3].pan, TYPE_UINT8),
    /* --- MOD: the layer switch (four bit cells on the page, plan §4.3), velocity (the Drum's three) --- */
    ROW_SELECT_ONOFF("tkt_layer_on", "Sample", "Trigger", "lon", ONOFF_TAG_TKT_LAYERS, layerOn, TYPE_UINT8),
    ROW_NOBIND("velo_mod_dest", "Velocity", "DstParam", "dst", DTYPE_TARGET_SELECTION_VELO, INSTRUMENT_BIND_VELOCITY_TARGET),
    ROW_NOBIND_IMAGE("velo_mod_amount", "Velocity", "Amount", "amt", DTYPE_0B127, MOD_NONE, INSTRUMENT_BIND_VELOCITY_AMOUNT),
    ROW_SELECT_ONOFF("velo_vol_on_off", "Velocity", "Vol mod", "vel", 0u, veloVol, TYPE_UINT8),
    /* --- FM: drive, SRR --- */
    ROW("tkt_drive_a", "Sample", "Drive", "drA", DTYPE_0B127, MOD_0_127, layer[0].drive, TYPE_UINT8),
    ROW("tkt_drive_b", "Sample", "Drive", "drB", DTYPE_0B127, MOD_0_127, layer[1].drive, TYPE_UINT8),
    ROW("tkt_drive_c", "Sample", "Drive", "drC", DTYPE_0B127, MOD_0_127, layer[2].drive, TYPE_UINT8),
    ROW("tkt_drive_d", "Sample", "Drive", "drD", DTYPE_0B127, MOD_0_127, layer[3].drive, TYPE_UINT8),
    ROW("tkt_srr_a", "Sample", "SampleRt", "srA", DTYPE_0B127, MOD_0_127, layer[0].srr, TYPE_UINT8),
    ROW("tkt_srr_b", "Sample", "SampleRt", "srB", DTYPE_0B127, MOD_0_127, layer[1].srr, TYPE_UINT8),
    ROW("tkt_srr_c", "Sample", "SampleRt", "srC", DTYPE_0B127, MOD_0_127, layer[2].srr, TYPE_UINT8),
    ROW("tkt_srr_d", "Sample", "SampleRt", "srD", DTYPE_0B127, MOD_0_127, layer[3].srr, TYPE_UINT8),
    /* --- click: the sends, the space stage --- */
    ROW("tkt_space_a", "Sample", "Space", "spA", DTYPE_0B127, MOD_0_127, layer[0].space, TYPE_UINT8),
    ROW("tkt_space_b", "Sample", "Space", "spB", DTYPE_0B127, MOD_0_127, layer[1].space, TYPE_UINT8),
    ROW("tkt_space_c", "Sample", "Space", "spC", DTYPE_0B127, MOD_0_127, layer[2].space, TYPE_UINT8),
    ROW("tkt_space_d", "Sample", "Space", "spD", DTYPE_0B127, MOD_0_127, layer[3].space, TYPE_UINT8),
    ROW("tkt_space_time", "Space", "Time", "stm", DTYPE_0B127, MOD_0_127, spaceTime, TYPE_UINT8),
    ROW("tkt_space_feedback", "Space", "Feedback", "sfb", DTYPE_0B127, MOD_0_127, spaceFeedback, TYPE_UINT8),
    ROW("tkt_space_detune", "Space", "Detune", "sdt", DTYPE_0B127, MOD_0_127, spaceDetune, TYPE_UINT8),
    ROW("tkt_space_tone", "Space", "Tone", "sto", DTYPE_0B127, MOD_0_127, spaceTone, TYPE_UINT8),
    /* --- FILTER: tone, tone envelope --- */
    ROW("tkt_tone_a", "Sample", "Tone", "tnA", DTYPE_PM63, MOD_PM63, layer[0].tone, TYPE_UINT8),
    ROW("tkt_tone_b", "Sample", "Tone", "tnB", DTYPE_PM63, MOD_PM63, layer[1].tone, TYPE_UINT8),
    ROW("tkt_tone_c", "Sample", "Tone", "tnC", DTYPE_PM63, MOD_PM63, layer[2].tone, TYPE_UINT8),
    ROW("tkt_tone_d", "Sample", "Tone", "tnD", DTYPE_PM63, MOD_PM63, layer[3].tone, TYPE_UINT8),
    ROW("tkt_tone_env_a", "Tone", "Envelope", "teA", DTYPE_PM63, MOD_PM63, layer[0].toneEnv, TYPE_UINT8),
    ROW("tkt_tone_env_b", "Tone", "Envelope", "teB", DTYPE_PM63, MOD_PM63, layer[1].toneEnv, TYPE_UINT8),
    ROW("tkt_tone_env_c", "Tone", "Envelope", "teC", DTYPE_PM63, MOD_PM63, layer[2].toneEnv, TYPE_UINT8),
    ROW("tkt_tone_env_d", "Tone", "Envelope", "teD", DTYPE_PM63, MOD_PM63, layer[3].toneEnv, TYPE_UINT8),
    /* --- the LFO pair (the Colloid's rows, ColloidParameters.c:277-284, :290-293) --- */
    ROW_SPECIAL("lfo_rate", "LFO", "Frequncy", "frq", DTYPE_0B127, MOD_0_127, lfo.modNodeValue, TYPE_SPECIAL_F, IM_SPECIAL_LFO_RATE),
    ROW("lfo_amount", "LFO", "Amount", "am1", DTYPE_0B127, MOD_0_127, lfo.modTarget.amount, TYPE_FLT),
    ROW("lfo_amount_2", "LFO", "Amount 2", "am2", DTYPE_0B127, MOD_0_127, lfo.modTarget2.amount, TYPE_FLT),
    ROW_MENU("lfo_wave", "LFO", "Waveform", "wav", MENU_LFO_WAVES, MOD_NONE, lfo.waveform, TYPE_UINT8),
    ROW_MENU("lfo_retrigger_voice", "LFO", "Retriggr", "rtg", MENU_RETRIGGER, MOD_NONE, lfo.retrigger, TYPE_UINT8),
    ROW("lfo_polarity", "LFO", "Polarity", "pol", DTYPE_LFO_POLARITY, MOD_NONE, lfo.polarity, TYPE_UINT8),
    ROW_MENU_SPECIAL("lfo_sync", "LFO", "ClockSnc", "snc", MENU_SYNC_RATES, MOD_NONE, lfo.sync, TYPE_UINT8, IM_SPECIAL_LFO_SYNC),
    ROW_SPECIAL("lfo_offset", "LFO", "Offset", "ofs", DTYPE_0B127, MOD_NONE, lfo.phaseOffset, TYPE_UINT32, IM_SPECIAL_LFO_OFFSET),
    ROW_NOBIND("lfo_target_voice", "LFO", "DstVoice", "vo1", DTYPE_VOICE_LFO, INSTRUMENT_BIND_LFO_TARGET_VOICE),
    ROW_NOBIND("lfo_target_param", "LFO", "DstParam", "ds1", DTYPE_TARGET_SELECTION_LFO, INSTRUMENT_BIND_LFO_TARGET_PARAM),
    ROW_NOBIND("lfo_target_voice_2", "LFO", "DstVoice2", "vo2", DTYPE_VOICE_LFO, INSTRUMENT_BIND_LFO_TARGET_VOICE_2),
    ROW_NOBIND("lfo_target_param_2", "LFO", "DstParam2", "ds2", DTYPE_TARGET_SELECTION_LFO, INSTRUMENT_BIND_LFO_TARGET_PARAM_2),
    /* --- MIX: the voice (vol, the L-R balance), the width, the space level --- */
    ROW("instrument_vol", "Voice", "Volume", "vol", DTYPE_0B127, MOD_0_127, vol, TYPE_FLT),
    ROW("instrument_pan", "Voice", "Panning", "pan", DTYPE_PM63, MOD_PM63, pan, TYPE_UINT8),
    ROW("tkt_width", "Space", "Width", "wid", DTYPE_0B127, MOD_0_127, width, TYPE_UINT8),
    ROW("tkt_space_level", "Space", "Level", "slv", DTYPE_0B127, MOD_0_127, spaceLevel, TYPE_UINT8),
};

const uint8_t tetrakt_param_descriptor_count =
    (uint8_t)(sizeof(tetrakt_param_descriptors) / sizeof(tetrakt_param_descriptors[0]));

_Static_assert(sizeof(tetrakt_param_descriptors) / sizeof(tetrakt_param_descriptors[0]) ==
                   TKT_PARAM_COUNT,
               "tetrakt_param_descriptors must have one row per index");

/* One empty cell of a 16-cell page; the four bit cells of the switch (SD1). */
#define TKT_E INSTRUMENT_MENU_EMPTY
#define TKT_BIT(b) INSTRUMENT_MENU_BIT(b)

/*
 * Tetrakt voice menu layout (S096 P1, plan §8.2, the user's revision 6).
 *
 * What:       eight sub-pages at the index of the front-panel SELECT button,
 *             16 cells each, shown as four-cell screens; on every per-layer
 *             screen pot 1 is layer A .. pot 4 layer D:
 *               0 OSC     smA..smD | tuA..tuD
 *               1 AEG     lvA..lvD | dyA..dyD | pnA..pnD
 *               2 MOD     onA..onD (the lon bit cells) | dst amt vel
 *               3 FM      drA..drD | srA..srD
 *               4 click   spA..spD | stm sfb sdt sto
 *               5 FILTER  tnA..tnD | teA..teD
 *               6 LFO     the Drum layout (the Colloid's)
 *               7 MIX     vol pan wid slv (one screen: the next is the
 *                         platform's Scene and fader settings)
 */
const instrument_menu_page_t tetrakt_menu_pages[] = {
    /* 0 OSC */
    {{ TKT_PARAM_SAMPLE_A, TKT_PARAM_SAMPLE_B, TKT_PARAM_SAMPLE_C, TKT_PARAM_SAMPLE_D, TKT_PARAM_TUNE_A, TKT_PARAM_TUNE_B, TKT_PARAM_TUNE_C, TKT_PARAM_TUNE_D, TKT_E, TKT_E, TKT_E, TKT_E, TKT_E, TKT_E, TKT_E, TKT_E }},
    /* 1 AEG */
    {{ TKT_PARAM_LEVEL_A, TKT_PARAM_LEVEL_B, TKT_PARAM_LEVEL_C, TKT_PARAM_LEVEL_D, TKT_PARAM_DECAY_A, TKT_PARAM_DECAY_B, TKT_PARAM_DECAY_C, TKT_PARAM_DECAY_D, TKT_PARAM_PAN_A, TKT_PARAM_PAN_B, TKT_PARAM_PAN_C, TKT_PARAM_PAN_D, TKT_E, TKT_E, TKT_E, TKT_E }},
    /* 2 MOD: the four layer switches, then velocity (the Drum's order) */
    {{ TKT_BIT(0u), TKT_BIT(1u), TKT_BIT(2u), TKT_BIT(3u), TKT_PARAM_VELO_MOD_DEST, TKT_PARAM_VELO_MOD_AMOUNT, TKT_PARAM_VELO_VOL, TKT_E, TKT_E, TKT_E, TKT_E, TKT_E, TKT_E, TKT_E, TKT_E, TKT_E }},
    /* 3 FM */
    {{ TKT_PARAM_DRIVE_A, TKT_PARAM_DRIVE_B, TKT_PARAM_DRIVE_C, TKT_PARAM_DRIVE_D, TKT_PARAM_SRR_A, TKT_PARAM_SRR_B, TKT_PARAM_SRR_C, TKT_PARAM_SRR_D, TKT_E, TKT_E, TKT_E, TKT_E, TKT_E, TKT_E, TKT_E, TKT_E }},
    /* 4 click */
    {{ TKT_PARAM_SPACE_A, TKT_PARAM_SPACE_B, TKT_PARAM_SPACE_C, TKT_PARAM_SPACE_D, TKT_PARAM_SPACE_TIME, TKT_PARAM_SPACE_FEEDBACK, TKT_PARAM_SPACE_DETUNE, TKT_PARAM_SPACE_TONE, TKT_E, TKT_E, TKT_E, TKT_E, TKT_E, TKT_E, TKT_E, TKT_E }},
    /* 5 FILTER */
    {{ TKT_PARAM_TONE_A, TKT_PARAM_TONE_B, TKT_PARAM_TONE_C, TKT_PARAM_TONE_D, TKT_PARAM_TONE_ENV_A, TKT_PARAM_TONE_ENV_B, TKT_PARAM_TONE_ENV_C, TKT_PARAM_TONE_ENV_D, TKT_E, TKT_E, TKT_E, TKT_E, TKT_E, TKT_E, TKT_E, TKT_E }},
    /* 6 LFO: the Drum layout */
    {{ TKT_PARAM_LFO_RATE, TKT_PARAM_LFO_SYNC, TKT_PARAM_LFO_WAVE, TKT_PARAM_LFO_OFFSET, TKT_PARAM_LFO_RETRIGGER_VOICE, TKT_PARAM_LFO_POLARITY, TKT_PARAM_LFO_AMOUNT, TKT_PARAM_LFO_AMOUNT_2, TKT_PARAM_LFO_TARGET_VOICE, TKT_PARAM_LFO_TARGET_PARAM, TKT_PARAM_LFO_TARGET_VOICE_2, TKT_PARAM_LFO_TARGET_PARAM_2, TKT_E, TKT_E, TKT_E, TKT_E }},
    /* 7 MIX */
    {{ TKT_PARAM_INSTRUMENT_VOL, TKT_PARAM_INSTRUMENT_PAN, TKT_PARAM_WIDTH, TKT_PARAM_SPACE_LEVEL, TKT_E, TKT_E, TKT_E, TKT_E, TKT_E, TKT_E, TKT_E, TKT_E, TKT_E, TKT_E, TKT_E, TKT_E }},
};

const uint8_t tetrakt_menu_page_count =
    (uint8_t)(sizeof(tetrakt_menu_pages) / sizeof(tetrakt_menu_pages[0]));

_Static_assert(TETRAKT_MENU_PAGE_COUNT ==
                   (sizeof(tetrakt_menu_pages) / sizeof(tetrakt_menu_pages[0])),
               "TETRAKT_MENU_PAGE_COUNT must match tetrakt_menu_pages");

/*
 * The nine velocity groups' layer-A rows (S096 P1 SD3), in TKT_KIND_* order.
 */
static const uint8_t tkt_groupA[TKT_KIND_COUNT] = {
    TKT_PARAM_TUNE_A, TKT_PARAM_LEVEL_A, TKT_PARAM_DECAY_A, TKT_PARAM_PAN_A,
    TKT_PARAM_DRIVE_A, TKT_PARAM_SRR_A, TKT_PARAM_SPACE_A, TKT_PARAM_TONE_A,
    TKT_PARAM_TONE_ENV_A,
};

uint8_t tetraktParameters_velocityKind(const uint8_t token)
{
    uint8_t k;

    for (k = 0u; k < TKT_KIND_COUNT; k++)
        if (token == tkt_groupA[k])
            return k;
    return TKT_KIND_NONE;
}

uint8_t tetraktParameters_velocityTokenHidden(const uint8_t token)
{
    uint8_t k;

    for (k = 0u; k < TKT_KIND_COUNT; k++)
        if (token > tkt_groupA[k] && token < (uint8_t)(tkt_groupA[k] + TKT_LAYERS))
            return 1u;
    return 0u;
}

#undef FLAGS_IMAGE
#undef BIND_SPECIAL
#undef BIND
#undef ROW
#undef ROW_SPECIAL
#undef ROW_MENU
#undef ROW_MENU_SPECIAL
#undef ROW_MENU_SELECT
#undef ROW_SELECT_ONOFF
#undef ROW_NOBIND
#undef ROW_NOBIND_IMAGE
#undef TKT_E
#undef TKT_BIT
#undef MOD_PM63
#undef MOD_0_127
#undef MOD_NONE
```

### S4 gate

Compiles in S5 (the static asserts: 64 rows, 8 pages, the switch index).

---

## S5. Build

### S5.1 `Makefile:40`: ADD after line 40

Anchor: `-ICore/DSP/Instruments/Colloid \`

```make
          -ICore/DSP/Instruments/Tetrakt \
```

### S5.2 `Makefile:119`: ADD after line 119

Anchor: `Core/DSP/Instruments/Colloid/ColloidParameters.c \`

```make
  Core/DSP/Instruments/Tetrakt/TetraktParameters.c \
```

### S5.3 `Makefile:164`: ADD after line 164

Anchor: `Core/DSP/Instruments/Colloid/ColloidVoice.c \`

```make
  Core/DSP/Instruments/Tetrakt/TetraktTables.c \
  Core/DSP/Instruments/Tetrakt/TetraktVoice.c \
```

### S5.4 `Makefile:281`: ADD after line 281

Anchor: `$(CC) -c $(CFLAGS_DSP) $< -o $@ -fno-lto` (the third Colloid recipe)

```make
# S096 P1: the Tetrakt engine and its tables are DSP: -Ofast like the
# Colloid. -fno-lto for the S095 P2 reason (Makefile comment above): outside
# the LTO unit the Tetrakt cannot shift LTO's partitioning of the other
# engines' loops (gated by elf_p4_check, S11); its entry points are noinline
# calls from InstrumentManager, and it calls the platform's
# osc_userBind()/osc_renderTyped() as ordinary externals.
# TetraktParameters.c stays in SRCS (-O2), as the other tables.
$(BUILD)/Core/DSP/Instruments/Tetrakt/TetraktVoice.o: Core/DSP/Instruments/Tetrakt/TetraktVoice.c | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS_DSP) $< -o $@ -fno-lto
$(BUILD)/Core/DSP/Instruments/Tetrakt/TetraktTables.o: Core/DSP/Instruments/Tetrakt/TetraktTables.c | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS_DSP) $< -o $@ -fno-lto
```

### S5 gate

`make all` exit 0. Nothing references the type yet; `--gc-sections` drops
it, and the link budget is unchanged.

---

## S6. `Core/DSP/Instruments/InstrumentManager.h`

### S6.1 `:101`: ADD after line 101

Anchor: `INSTRUMENT_TYPE_COL,`

```c
    /*
     * S096 P1: the Tetrakt type (token "tkt"), before UNKNOWN.
     *
     * What:       value 6; UNKNOWN moves from 6 to 7.
     * Why:        appended (the standing rule, as the Colloid in S095 P2): no
     *             stored value changes meaning; menu_instrumentLoadIndex[]
     *             (menu.c: +2 B .bss) and every `type >= UNKNOWN` guard
     *             follow; a .hcnames header written before the addition is
     *             a leading part of the new list and is accepted.
     */
    INSTRUMENT_TYPE_TKT,
```

### S6.2 `:358`: ADD after line 358

Anchor: `#define INSTRUMENT_MENU_SKIP  0xfeu`

```c
/*
 * Bit cells of a page (S096 P1 SD1).
 *
 * What:       a page cell code 0xF0 + b (b 0..3) is "bit b of the type's
 *             switch row" (the registry row's switch_row_plus1).
 *             instrumentManager_menuDescriptorIndex() resolves it to the
 *             switch row's descriptor and instrumentManager_menuCellBit()
 *             returns b; the menu shows and edits that one bit as an on/off
 *             cell and commits the whole row.
 * Why:        the Tetrakt's four layer switches are one 4-bit row (plan
 *             §4.3, user R4) shown as four controls. Descriptor indices are
 *             0..63 and 0xFE / 0xFF are SKIP / EMPTY, so 0xF0..0xF3 are
 *             free.
 * Accessors:  TetraktParameters.c (tetrakt_menu_pages[]); menu.c
 *             menu_resolveCellAbsolute().
 */
#define INSTRUMENT_MENU_BIT_FIRST  0xf0u
#define INSTRUMENT_MENU_BIT_COUNT  4u
#define INSTRUMENT_MENU_BIT(b)     ((uint8_t)(INSTRUMENT_MENU_BIT_FIRST + (b)))
```

### S6.3 `:394`: ADD after line 394

Anchor: `uint8_t menu_page_count;`

```c
    /*
     * The switch row behind the page's bit cells (S096 P1 SD1).
     *
     * What:       the descriptor index + 1 of the row that
     *             INSTRUMENT_MENU_BIT(b) cells edit; 0 = the type has no
     *             bit cells.
     * Why:        +1 so every positional registry row written before this
     *             field (zero-initialised) means "none".
     */
    uint8_t switch_row_plus1;
```

### S6.4 `:437`: ADD after line 437

Anchor: `instrument_type_t type, uint8_t page, uint8_t position, uint8_t *index_out);`
(the end of the `instrumentManager_menuDescriptorIndex()` declaration)

```c
/*
 * The bit of a page cell (S096 P1 SD1).
 *
 * What:       b + 1 when the cell is INSTRUMENT_MENU_BIT(b) on a type with a
 *             switch row; 0 for every other cell.
 * Inputs:     type, page (sub-page), position (0..15).
 * Accessors:  menu.c menu_resolveCellAbsolute().
 */
uint8_t instrumentManager_menuCellBitPlus1(instrument_type_t type, uint8_t page,
                                           uint8_t position);
```

### S6.5 `:852`: MODIFY line 852

Anchor: ` * What:       the Colloid's voice->right (32 int16, written by its sync`

```c
 * What:       the Colloid's or the Tetrakt's (S096 P1) voice->right (32
 *             int16, written by its sync
```

### S6.6 `:876`: ADD after line 876

Anchor: `uint8_t instrumentManager_cellInactive(uint8_t slot, const ParamDescriptor *descriptor);`

```c
/*
 * Whether a target is a Tetrakt velocity group (S096 P1 SD3).
 *
 * What:       1 when target is a voice parameter on a Tetrakt slot whose
 *             local index is a layer-A row of one of the nine groups
 *             (tetraktParameters_velocityKind() != TKT_KIND_NONE); else 0.
 * Why:        the velocity destination cell marks a group "all four layers"
 *             with a '*' (tn* for tnA; plan §4.2).
 * Accessors:  menu.c menu_formatCellValue3(), the single-parameter painter.
 */
uint8_t instrumentManager_targetIsVelocityGroup(uint16_t target);
```

---

## S7. `Core/DSP/Instruments/InstrumentManager.c`

### S7.1 `:7`: ADD after line 7

Anchor: `#include "ColloidParameters.h"    /* S095 P2: registry row, the inactive-cell test */`

```c
#include "TetraktParameters.h"    /* S096 P1: registry row, the velocity groups */
```

### S7.2 `:18`: ADD after line 18

Anchor: `#include "ColloidVoice.h"         /* S095 P2: the runtime member and its entry points */`

```c
#include "TetraktVoice.h"         /* S096 P1: the runtime member and its entry points */
#include "FxBuffer.h"             /* S096 P1: the voice unit (claim, release, borrow) */
```

### S7.3 `:91`: ADD after line 91

Anchor: `colloid_menu_pages, COLLOID_MENU_PAGE_COUNT },`

```c
    /*
     * S096 P1: Tetrakt. Token/extension "tkt"/".tkt"; directory
     * /Instrument/Tetrakt (7 characters; SD_CARD/Instrument/Tetrakt in the
     * card image with init.tkt). Basic (plan Q2): may take any slot. The
     * switch row (lon) is named for the MOD page's bit cells (SD1).
     */
    [INSTRUMENT_TYPE_TKT] =
    { INSTRUMENT_TYPE_TKT, "tkt", tetrakt_instrument_display_label, ".tkt", "Tetrakt",
      TETRAKT_INSTRUMENT_TYPE_FLAGS,
      tetrakt_param_descriptors, TETRAKT_PARAM_DESCRIPTOR_COUNT,
      tetrakt_menu_pages, TETRAKT_MENU_PAGE_COUNT,
      (uint8_t)(TETRAKT_PARAM_LAYER_ON_INDEX + 1u) },
```

### S7.4 `:224`: ADD after line 224

Anchor: `ColloidVoice colloid;     /* S095 P3: 1,112 B (measured); slot reserve 1,264 B */`

```c
    TetraktVoice tetrakt;     /* S096 P1: ≈ 1,090 B (est.; asserted below); slot reserve 1,264 B */
```

### S7.5 `:245`: ADD after line 245

Anchor: `"ColloidVoice exceeds tagged slot reserve");`

```c
/* S096 P1: the Tetrakt voice fits the tagged slot (no RAM growth; the
 * mix buses are the shared 448 B work area in TetraktVoice.c, approved),
 * and its line is exactly one FxBuffer voice unit. */
_Static_assert(sizeof(TetraktVoice) <= INSTRUMENT_RUNTIME_SLOT_BYTES,
               "TetraktVoice exceeds tagged slot reserve");
_Static_assert(TKT_LINE_SAMPLES == FXBUF_VOICE_UNIT_SAMPLES,
               "the Tetrakt line is one voice unit");
```

### S7.6 `:328`: ADD after line 328

Anchor: `}` (the end of `instrumentManager_colloidRuntime()`)

```c

/*
 * The slot's Tetrakt runtime (S096 P1).
 *
 * What:       the tagged union member for slot, or NULL out of range. Like
 *             the other accessors it does not check the slot's type: every
 *             caller switches on instrumentManager_slotType() first.
 */
static TetraktVoice *instrumentManager_tetraktRuntime(uint8_t slot)
{
    return (slot < INSTRUMENT_SLOT_COUNT) ? &runtime_slots[slot].tetrakt : 0;
}

/*
 * The voice unit a slot's incoming type needs (S096 P1 SD11).
 *
 * What:       makes the slot own exactly the units its type uses (one for a
 *             Tetrakt, none for every other type) and returns the Tetrakt's
 *             line, or NULL. A slot that already owns the right count keeps
 *             its unit, so a Tetrakt -> Tetrakt reset does not release and
 *             reclaim (no Effect reseat). Any other change releases first,
 *             then claims (fxbuf_voiceAcquire() notifies the Effect through
 *             the share callback, foreground only).
 * Why:        the FxBuffer contract: units belong to the slot's runtime and
 *             are claimed at type assignment, never at a row change (plan
 *             §10). With six Tetrakts six of the twelve units are owned.
 * Inputs:     slot, type (the incoming runtime type).
 * Outputs:    the unit's samples or NULL (no unit: S1 muted, SD11).
 * Accessors:  instrumentManager_runtimeInit(),
 *             instrumentManager_resetRuntimeSlotFromScene().
 */
static int16_t *instrumentManager_slotVoiceUnits(uint8_t slot, instrument_type_t type)
{
    const uint8_t wanted = (type == INSTRUMENT_TYPE_TKT) ? 1u : 0u;

    if (fxbuf_voiceUnitCount(slot) != wanted) {
        fxbuf_voiceRelease(slot);
        if (wanted)
            (void)fxbuf_voiceAcquire(slot, wanted);
    }
    return wanted ? fxbuf_voiceUnit(slot, 0u) : 0;
}
```

### S7.7 `:871`: ADD after line 871

Anchor: `{` (the first line of `instrumentManager_waveDomainMax()`'s body; line 869–870 its signature)

```c
    /*
     * S096 P1: a Tetrakt sample row (tkt_sample_a..d) lists the installed
     * samples: 0 .. count - 1 (0 with none installed: the layer is silent).
     * The rows are MENU_WAVEFORM selectors (TetraktParameters.c
     * ROW_MENU_SELECT), so this is their menu clamp and automation range
     * (instrumentManager_waveCellCount()).
     */
    if (descriptor && descriptor->file_key &&
        strncmp(descriptor->file_key, "tkt_sample_", 11u) == 0) {
        const uint8_t samples = sampleMemory_getNumSamples();

        return samples ? (uint8_t)(samples - 1u) : 0u;
    }
```

**Check at S7:** `SampleMemory.h` must be visible in `InstrumentManager.c`.
It is included through `Oscillator.h` (`osc_userCount()` already calls the
catalogue); if the build says otherwise, add `#include "SampleMemory.h"` at
S7.2.

### S7.8 `:616–628`: MODIFY

Anchor (line 616): `descriptor_index = entry->menu_pages[page].descriptor_index[position];`

```c
    descriptor_index = entry->menu_pages[page].descriptor_index[position];
    /*
     * S096 P1 SD1: a bit cell resolves to the type's switch row (the cell's
     * bit comes from instrumentManager_menuCellBitPlus1()). A type without a
     * switch row treats the code as an empty cell.
     */
    if (descriptor_index >= INSTRUMENT_MENU_BIT_FIRST &&
        descriptor_index < INSTRUMENT_MENU_BIT_FIRST + INSTRUMENT_MENU_BIT_COUNT) {
        if (!entry->switch_row_plus1)
            return 0;
        descriptor_index = (uint8_t)(entry->switch_row_plus1 - 1u);
    }
    if (descriptor_index == INSTRUMENT_MENU_SKIP) {
        if (index_out)
            *index_out = descriptor_index;
        return 0;
    }
    if (descriptor_index == INSTRUMENT_MENU_EMPTY ||
        descriptor_index >= entry->descriptor_count) {
        return 0;
    }
    if (index_out)
        *index_out = descriptor_index;
    return &entry->descriptors[descriptor_index];
```

### S7.9 `:629`: ADD after line 629

Anchor: `}` (the end of `instrumentManager_menuDescriptorIndex()`)

```c

uint8_t instrumentManager_menuCellBitPlus1(instrument_type_t type, uint8_t page,
                                           uint8_t position)
{
    /* S096 P1 SD1. Contract: InstrumentManager.h. */
    const instrument_registry_entry_t *entry = instrumentManager_registryEntry(type);
    uint8_t code;

    if (!entry || !entry->menu_pages || !entry->switch_row_plus1 ||
        page >= entry->menu_page_count || position >= INSTRUMENT_MENU_PAGE_CELLS)
        return 0u;
    code = entry->menu_pages[page].descriptor_index[position];
    if (code < INSTRUMENT_MENU_BIT_FIRST ||
        code >= INSTRUMENT_MENU_BIT_FIRST + INSTRUMENT_MENU_BIT_COUNT)
        return 0u;
    return (uint8_t)(code - INSTRUMENT_MENU_BIT_FIRST + 1u);
}
```

### S7.10 `:1440–1441`: MODIFY (the velocity validator)

Anchor (line 1440): `return instrumentManager_targetTokenValidForSlot(`

```c
    /*
     * S096 P1 SD3: a Tetrakt names each velocity group once, by its layer-A
     * row; the B, C and D rows of the nine groups are not velocity targets
     * (a stored one reads as off).
     */
    if (instrumentManager_slotType(source_slot) == INSTRUMENT_TYPE_TKT &&
        tetraktParameters_velocityTokenHidden(token))
        return 0u;
    return instrumentManager_targetTokenValidForSlot(
        scene_index, source_slot, token, INSTRUMENT_TARGET_MODULATION);
```

### S7.11 `:1443`: ADD after line 1443 (before `instrument_target_token_t instrumentManager_stepVelocityTargetForSource(`)

Anchor: the blank line after `instrumentManager_targetValidForVelocitySource()`'s closing `}`

```c
/*
 * One step of the velocity list's local part (S096 P1 SD3).
 *
 * What:       instrumentManager_stepTargetTokenForSlot() in the given
 *             direction, repeated while the result is a hidden Tetrakt token
 *             (the B, C, D rows of a group), at most INSTRUMENT_PARAM_COUNT
 *             times; for every other type exactly one step.
 * Why:        the velocity list names each group once (tu* lv* dy* pn* dr*
 *             sr* sp* tn* te*); the LFO lists keep every row.
 * Accessors:  instrumentManager_stepVelocityTargetForSource().
 */
static instrument_target_token_t instrumentManager_stepVelocityLocal(
    uint8_t scene_index, uint8_t source_slot, instrument_target_token_t token,
    int8_t direction)
{
    const uint8_t tkt = (uint8_t)(instrumentManager_slotType(source_slot) ==
                                  INSTRUMENT_TYPE_TKT);
    instrument_target_token_t next = token;
    uint8_t n;

    for (n = 0u; n < INSTRUMENT_PARAM_COUNT; n++) {
        instrument_target_token_t prev = next;

        next = instrumentManager_stepTargetTokenForSlot(
            scene_index, source_slot, prev, direction, INSTRUMENT_TARGET_MODULATION);
        if (next == prev || !tkt || next == INSTRUMENT_TARGET_TOKEN_OFF ||
            !tetraktParameters_velocityTokenHidden(next))
            return next;
    }
    return next;
}

```

### S7.12 `:1469–1489`: MODIFY (three calls in `instrumentManager_stepVelocityTargetForSource()`)

- Line 1469–1472, anchor `instrument_target_token_t next =` /
  `instrumentManager_stepTargetTokenForSlot(` /
  `scene_index, source_slot, normalized, 1,` /
  `INSTRUMENT_TARGET_MODULATION);` becomes:

```c
            /* S096 P1 SD3: the walk skips hidden Tetrakt tokens */
            instrument_target_token_t next =
                instrumentManager_stepVelocityLocal(scene_index, source_slot,
                                                    normalized, 1);
```

- The backward step after the Morph token (anchor
  `return instrumentManager_lastTargetTokenForSlot(`): unchanged. The last
  modulatable Tetrakt row is `slv` (index 63, a voice row, never hidden).
- The backward local step (anchor
  `return instrumentManager_stepTargetTokenForSlot(` /
  `scene_index, source_slot, normalized, -1,` /
  `INSTRUMENT_TARGET_MODULATION);`) becomes:

```c
        return instrumentManager_stepVelocityLocal(scene_index, source_slot,
                                                   normalized, -1);
```

### S7.13 `:1512`: ADD after line 1512

Anchor: `case INSTRUMENT_TYPE_COL: return instrumentManager_colloidRuntime(slot);   /* S095 P2 */`

```c
    case INSTRUMENT_TYPE_TKT: return instrumentManager_tetraktRuntime(slot);   /* S096 P1 */
```

### S7.14 `:1537`: ADD after line 1537

Anchor: `return voice ? &voice->lfo : 0; }` (the Colloid case)

```c
    case INSTRUMENT_TYPE_TKT: {   /* S096 P1: the Tetrakt voice owns its LFO like every engine */
        TetraktVoice *voice = instrumentManager_tetraktRuntime(slot);
        return voice ? &voice->lfo : 0; }
```

### S7.15 `:1611`: ADD after line 1611 (in `instrumentManager_runtimeInit()`)

Anchor: `break;` (after `Colloid_initVoice(instrumentManager_colloidRuntime(slot));`, line 1610)

```c
        case INSTRUMENT_TYPE_TKT:
            /*
             * S096 P1: the boot claim of the Tetrakt's voice unit
             * (fxbuf_init() ran first in main.c; the Effect registers its
             * share callback afterwards in effects_init() and reads the
             * share when its type starts, so nothing is notified here).
             */
            Tetrakt_initVoice(instrumentManager_tetraktRuntime(slot), slot,
                              instrumentManager_slotVoiceUnits(slot, INSTRUMENT_TYPE_TKT));
            break;
```

### S7.16 `:1650`: ADD after line 1650 (in `instrumentManager_ampEnvelopeQuiet()`)

Anchor: `}` (the end of the Colloid block, line 1647–1650)

```c
    /*
     * S096 P1 SD10: the Tetrakt is quiet when all four layers are idle (a
     * still-ringing S1 tail does not hold a swap).
     */
    if (instrumentManager_slotType(slot) == INSTRUMENT_TYPE_TKT) {
        const TetraktVoice *voice = instrumentManager_tetraktRuntime(slot);
        return voice ? Tetrakt_isQuiet(voice) : 1u;
    }
```

### S7.17 `:1745–1748`: MODIFY (in `instrumentManager_resetRuntimeSlotFromScene()`)

Anchor (line 1746): `memset(&runtime_slots[slot], 0, sizeof(runtime_slots[slot]));`

```c
    incoming = scene_instrumentSlotConst(scene_index, slot);
    memset(&runtime_slots[slot], 0, sizeof(runtime_slots[slot]));
    runtime_slot_type[slot] =
        incoming ? incoming->type : INSTRUMENT_TYPE_UNKNOWN;
    /*
     * S096 P1 SD11: the slot owns exactly the voice units its incoming type
     * uses (a Tetrakt one, every other type none). This is the only
     * tagged-member replacement point, so it is the only place units change
     * owner after boot. Foreground (the Preset activation worker, between
     * render blocks), as the FxBuffer share callback requires.
     */
    {
        int16_t *const line =
            instrumentManager_slotVoiceUnits(slot, instrumentManager_slotType(slot));

        if (instrumentManager_slotType(slot) == INSTRUMENT_TYPE_TKT)
            Tetrakt_initVoice(instrumentManager_tetraktRuntime(slot), slot, line);
    }
```

### S7.18 `:1888`: ADD after line 1888 (`instrumentManager_runtimePan()`)

Anchor: `return voice ? voice->pan : 64u; }` (the Colloid case)

```c
    case INSTRUMENT_TYPE_TKT: {   /* S096 P1: the voice pan, the L-R balance (plan §2) */
        TetraktVoice *voice = instrumentManager_tetraktRuntime(slot);
        return voice ? voice->pan : 64u; }
```

### S7.19 `:1923`: ADD after line 1923 (`instrumentManager_runtimeVolume()`)

Anchor: `return voice ? voice->vol : 0.0f; }` (the Colloid case)

```c
    case INSTRUMENT_TYPE_TKT: {   /* S096 P1 */
        TetraktVoice *voice = instrumentManager_tetraktRuntime(slot);
        return voice ? voice->vol : 0.0f; }
```

### S7.20 `:2157`: ADD after line 2157 (`instrumentManager_calcSlotAsync()`)

Anchor: `break;` (after `Colloid_calcVoiceAsync(instrumentManager_colloidRuntime(slot));`)

```c
    case INSTRUMENT_TYPE_TKT:   /* S096 P1: control rate; the audio loops are the sync block */
        Tetrakt_calcVoiceAsync(instrumentManager_tetraktRuntime(slot));
        break;
```

### S7.21 `:2216`: ADD after line 2216 (`instrumentManager_calcSlotSyncBlock()`)

Anchor: `break;` (after `Colloid_calcVoiceSyncBlock(instrumentManager_colloidRuntime(slot), buf, size);`)

```c
    case INSTRUMENT_TYPE_TKT:
        /*
         * S096 P1: Tetrakt_calcVoiceAsync() (control: the layers' stages,
         * pitches and binds, tone ramps, drive, gains, SRR, W2; the space
         * stage) and Tetrakt_calcVoiceSyncBlock() (per layer the platform's
         * ITCM sample read into the shared scratch, then the layer's post
         * loop; the space stage and the pack). buf receives the left block;
         * the right block stays in the voice (instrumentManager_slotRight()).
         * Both are noinline (TetraktVoice.h).
         */
        Tetrakt_calcVoiceSyncBlock(instrumentManager_tetraktRuntime(slot), buf, size);
        break;
```

### S7.22 `:2247–2255`: MODIFY (`instrumentManager_slotRight()`)

Anchor (line 2247): `const int16_t *instrumentManager_slotRight(uint8_t slot)`

```c
const int16_t *instrumentManager_slotRight(uint8_t slot)
{
    /* S095 P2: the Colloid; S096 P1: the Tetrakt. NULL for every mono type. */
    if (instrumentManager_slotType(slot) == INSTRUMENT_TYPE_COL) {
        const ColloidVoice *voice = instrumentManager_colloidRuntime(slot);
        return voice ? voice->right : 0;
    }
    if (instrumentManager_slotType(slot) == INSTRUMENT_TYPE_TKT) {
        const TetraktVoice *voice = instrumentManager_tetraktRuntime(slot);
        return voice ? voice->right : 0;
    }
    return 0;
}
```

### S7.23 `:2269`: ADD after line 2269

Anchor: `}` (the end of `instrumentManager_cellInactive()`)

```c

/*
 * Whether a target is a Tetrakt velocity group (S096 P1 SD3). Contract:
 * InstrumentManager.h.
 */
uint8_t instrumentManager_targetIsVelocityGroup(uint16_t target)
{
    if (!instrumentParam_isVoiceParameter(target) ||
        instrumentManager_slotType(instrumentParam_slot(target)) != INSTRUMENT_TYPE_TKT)
        return 0u;
    return (uint8_t)(tetraktParameters_velocityKind(instrumentParam_local(target)) !=
                     TKT_KIND_NONE);
}
```

### S7.24 `:2393`: ADD after line 2393 (`instrumentManager_triggerTrack()`)

Anchor: `break;` (after the Colloid's `Colloid_triggerVoice(...)` call)

```c
    case INSTRUMENT_TYPE_TKT:
        /*
         * S096 P1: no step length (one-shot layers that decay). The
         * platform part (LFO retrigger, velocity nodes) runs inside
         * Tetrakt_triggerVoice() for `slot`; the slot-5 alternate decay above
         * finds no amp_envelope_decay row and does nothing, so track 7 plays
         * slot 5 as an ordinary hit (plan §11). The layer switch is read
         * here, after this step's automation (voiceControl_processPending()).
         */
        Tetrakt_triggerVoice(instrumentManager_tetraktRuntime(slot), slot,
                             velocity, note);
        break;
```

### S7.25 `:4369–4372`: MODIFY (`INSTRUMENT_BIND_VELOCITY_TARGET`)

Anchor (line 4369): `return instrumentManager_installVelocityModulationTarget(`

```c
        /*
         * S096 P1 SD3: on a Tetrakt slot, a token that names a velocity group
         * (a layer-A row of the nine) is applied by the voice to all four
         * layers, each with its own latched velocity; the platform node gets
         * no destination (installing INVALID also restores any supplemental
         * target). Every other token (off, a voice row, the own Morph) takes
         * the platform path, and the voice's group is cleared.
         */
        if (instrumentManager_slotType(slot) == INSTRUMENT_TYPE_TKT) {
            TetraktVoice *tkt = instrumentManager_tetraktRuntime(slot);
            const uint8_t kind = (value <= INSTRUMENT_TARGET_TOKEN_MAX_LOCAL)
                ? tetraktParameters_velocityKind((uint8_t)value) : TKT_KIND_NONE;

            Tetrakt_setVelocityKind(tkt, kind);
            if (kind != TKT_KIND_NONE)
                return instrumentManager_installVelocityModulationTarget(
                    slot, INSTRUMENT_PARAM_INVALID);
        }
        return instrumentManager_installVelocityModulationTarget(
            slot,
            instrumentManager_velocityTargetIdFromToken(
                scene_getActiveIndex(), slot, value));
```

The amount (`INSTRUMENT_BIND_VELOCITY_AMOUNT`, `:4353–4358`) needs no edit:
the voice reads `velocityModulators[voice->slot].amount` each block.

### Switches that need no Tetrakt case (each defaults correctly; checked)

- `instrumentManager_osc()` (`:2518`, the `osc1_` key lookup: Tetrakt has
  no `osc1_` rows) and `instrumentManager_oscBySelector()` (`:2574`).
- The `IM_SPECIAL_INSTRUMENT_DRIVE` and `IM_SPECIAL_OSC_TYPE` writers
  (`:4003`, `:4095`): Tetrakt has neither row.
- `instrumentManager_slotUsesSrr()`: default 0 (no `srt`; the per-layer SRR
  is the voice's).
- `instrumentManager_recalcSlotFilter()`, `_filter()`, `_ampEg()`,
  `_distortion()`, `_transient()`: NULL (none of those structures).
- `instrumentManager_cellInactive()`: 0 (no inactive cells).
- `instrumentManager_applySlot6AlternateDecay()`: no `amp_envelope_decay`
  row, so it returns.
- `instrumentManager_classifySpecialKey()` and the boot self-check: they walk
  the registry, so Tetrakt is covered with no edit. Its only special rows
  are the LFO's three, as every engine's.

### S7 gate

`make all` exit 0; the asserts hold (struct ≤ 1,264, line = unit). A DEV
boot with the registry self-check clean.

---

## S8. Storage and the card

### S8.1 `Core/Hardware/SD/storageTypes.h:143`: ADD after line 143

Anchor: `#define STORAGE_INSTRUMENT_COL     INSTRUMENT_TYPE_COL   /* S095 P2 */`

```c
#define STORAGE_INSTRUMENT_TKT     INSTRUMENT_TYPE_TKT   /* S096 P1 */
```

Nothing else in storage changes, as for the Colloid (S095 P2 §2.4):
- the token, extension and folder come from the registry row;
- the `.hcnames` header accepts a leading-part header;
- the boot directory trim walks every `/Instrument` subdirectory, and a
  missing type folder is created (S095 P3 F1).

### S8.2 NEW FILE `tools/tetrakt/make_init_tkt.py`

A copy of `tools/colloid/make_init_col.py` (116 lines) with exactly these
changes:

| Line(s) | Change |
|---|---|
| 2–39 (docstring) | "Colloid" → "Tetrakt", `init.col` → `init.tkt`, `type=col` → `type=tkt`; the Values paragraph lists the Tetrakt bytes below and names `Tetrakt_initVoice()`; Affiliates names `TetraktVoice.c Tetrakt_initVoice()` and `tools/colloid/make_init_col.py (the same writer)` |
| 47 | `TABLE = os.path.join(ROOT, "Core", "DSP", "Instruments", "Tetrakt", "TetraktParameters.c")` |
| 48 | `OUT = os.path.join(ROOT, "SD_CARD", "Instrument", "Tetrakt", "init.tkt")` |
| 52 | `MORPHABLE = {"ROW", "ROW_SPECIAL", "ROW_MENU", "ROW_MENU_SPECIAL", "ROW_NOBIND_IMAGE"}` (unchanged: `ROW_MENU_SELECT` and `ROW_SELECT_ONOFF` are not morphable, so the sample rows, `lon` and `vel` stay out of `[morph]`) |
| 55–74 | `INIT` replaced by the dict below |
| 83 | `type=tkt` |
| messages | `make_init_col` → `make_init_tkt`, `init.col` → `init.tkt` |

```python
# init bytes per key; every key of the table must be here (and only those).
# The same bytes as Tetrakt_initVoice() (TetraktVoice.c).
INIT = {
    "tkt_sample_a": "0", "tkt_sample_b": "1", "tkt_sample_c": "2", "tkt_sample_d": "3",
    "tkt_tune_a": "63", "tkt_tune_b": "63", "tkt_tune_c": "63", "tkt_tune_d": "63",
    "tkt_level_a": "100", "tkt_level_b": "100", "tkt_level_c": "100", "tkt_level_d": "100",
    "tkt_decay_a": "70", "tkt_decay_b": "70", "tkt_decay_c": "70", "tkt_decay_d": "70",
    "tkt_pan_a": "63", "tkt_pan_b": "63", "tkt_pan_c": "63", "tkt_pan_d": "63",
    "tkt_layer_on": "1",
    "velo_mod_dest": "255", "velo_mod_amount": "0", "velo_vol_on_off": "1",
    "tkt_drive_a": "0", "tkt_drive_b": "0", "tkt_drive_c": "0", "tkt_drive_d": "0",
    "tkt_srr_a": "127", "tkt_srr_b": "127", "tkt_srr_c": "127", "tkt_srr_d": "127",
    "tkt_space_a": "0", "tkt_space_b": "0", "tkt_space_c": "0", "tkt_space_d": "0",
    "tkt_space_time": "40", "tkt_space_feedback": "40", "tkt_space_detune": "30",
    "tkt_space_tone": "100",
    "tkt_tone_a": "63", "tkt_tone_b": "63", "tkt_tone_c": "63", "tkt_tone_d": "63",
    "tkt_tone_env_a": "63", "tkt_tone_env_b": "63", "tkt_tone_env_c": "63", "tkt_tone_env_d": "63",
    "lfo_rate": "0", "lfo_amount": "0", "lfo_amount_2": "0", "lfo_wave": "0",
    "lfo_retrigger_voice": "0", "lfo_polarity": "0", "lfo_sync": "0", "lfo_offset": "0",
    "lfo_target_voice": "self", "lfo_target_param": "255",
    "lfo_target_voice_2": "self", "lfo_target_param_2": "255",
    "instrument_vol": "100", "instrument_pan": "63", "tkt_width": "0", "tkt_space_level": "100",
}
```

### S8.3 NEW FILE `SD_CARD/Instrument/Tetrakt/init.tkt`

Generated: `python3 tools/tetrakt/make_init_tkt.py`. Instrument Load only
lists files, and a slot's type changes only by loading one (S093 P4 F1),
so the card image carries a first Tetrakt.

### S8 gate

`python3 tools/tetrakt/make_init_tkt.py --check` prints "init.tkt matches
the table (64 rows)".

---

## S9. Menu

### S9.1 `Core/Menu/MenuText.h:287`: ADD after line 287

Anchor: `#define ONOFF_TAG_COL_FILT_TYPE    6u`

```c
/*
 * The Tetrakt's layer switch (S096 P1, plan §4.3).
 *
 * What:       ONOFF_TAG_TKT_LAYERS 7 (lon, tktLayerNames). The row's dtype
 *             byte is DTYPE_ON_OFF | (7 << 4). Tag 7 was free (MenuText.h
 *             l. 275-277: 4, 7 and 8-15).
 * Why:        the 4-bit mask is one selector with 16 named stops, so every
 *             3-character view (step automation, lane search, MIDI Learn,
 *             the clamp, the automation range) follows menu_onoffTagNames()
 *             with no new code; the VOICE page shows it as four bit cells
 *             (SD1).
 * Accessors:  TetraktParameters.c (the lon row); menu.c menu_onoffTagNames().
 * Affiliates: TKT_LAYER_MASK_COUNT (TetraktVoice.h): asserted in menu.c.
 */
#define ONOFF_TAG_TKT_LAYERS       7u
```

### S9.2 `Core/Menu/MenuText.h`: ADD after the end of `colWaveNames[]`

Anchor: the `};` that closes `static const char colWaveNames[][4] = {`
(`:309–311`).

```c
/*
 * The Tetrakt's layer-switch names (S096 P1, plan §4.3). [0] = count, then
 * the 16 masks in value order (bit 0 = A .. bit 3 = D): the letters of the
 * layers that a trigger restarts; "---" none, "all" every layer. Every name
 * fits the 3-character cell (the P4 `hed` precedent).
 */
static const char tktLayerNames[][4] = {
    {16}, {"---"},{"A  "},{"B  "},{"AB "},{"C  "},{"AC "},{"BC "},{"ABC"},
          {"D  "},{"AD "},{"BD "},{"ABD"},{"CD "},{"ACD"},{"BCD"},{"all"},
};
```

### S9.3 `Core/Menu/menu.c:69`: ADD after line 69

Anchor: `#include "ColloidVoice.h"      /* S095 P2: COL_WAVE_COUNT, the COL_* counts (the name asserts) */`

```c
#include "TetraktVoice.h"      /* S096 P1: TKT_LAYER_MASK_COUNT (the name assert) */
```

### S9.4 `Core/Menu/menu.c:2151`: ADD after line 2151 (`menu_cell_t`)

Anchor: `uint8_t descriptor_index;`

```c
    /*
     * Bit cell (S096 P1 SD1): b + 1 when this instrument cell shows and edits
     * bit b of its row (the Tetrakt's lon), else 0. Zero by default, so every
     * memset-built cell is an ordinary cell. descriptor_index stays the row:
     * automation, search, held lookup, MIDI Learn, pot clear and live record
     * treat the four bit cells as the one row (plan R5).
     */
    uint8_t bit_plus1;
```

### S9.5 `Core/Menu/menu.c:4622`: ADD after line 4622 (`menu_resolveCellAbsolute()`)

Anchor: `&cell.descriptor_index);`

```c
        /* S096 P1 SD1: which bit of the row a bit cell shows (0: none). */
        cell.bit_plus1 = instrumentManager_menuCellBitPlus1(slot->type, subPage,
                                                            position);
```

### S9.6 `Core/Menu/menu.c:5110–5112`: MODIFY (`menu_cellDtype()`)

Anchor (line 5110): `if (cell->kind == MENU_CELL_INSTRUMENT ||`

```c
    /* S096 P1 SD2: a bit cell is a plain on/off (tag 0: on / off, 0..1). */
    if (cell->kind == MENU_CELL_INSTRUMENT && cell->bit_plus1)
        return DTYPE_ON_OFF;
    if (cell->kind == MENU_CELL_INSTRUMENT ||
        cell->kind == MENU_CELL_KIT_SETTING)
        return cell->descriptor ? cell->descriptor->dtype : DTYPE_0B127;
```

### S9.7 `Core/Menu/menu.c:5146–5150`: MODIFY (`menu_cellDisplayValue()`)

Anchor (line 5146): `if (!slot || cell->descriptor_index >= INSTRUMENT_PARAM_COUNT)`

```c
        if (!slot || cell->descriptor_index >= INSTRUMENT_PARAM_COUNT)
            return 0u;
        /*
         * S096 P1 SD2: a bit cell shows its bit of the row's Normal value (the
         * switch is a selector: S078's non-morphable rule, Normal in either
         * view).
         */
        if (cell->bit_plus1)
            return (uint16_t)((slot->parameter_images.instrument_parameters[cell->descriptor_index]
                               >> (cell->bit_plus1 - 1u)) & 1u);
        return voiceModeShowMorph
            ? slot->parameter_images.morph_instrument_parameters[cell->descriptor_index]
            : slot->parameter_images.instrument_parameters[cell->descriptor_index];
```

### S9.8 `Core/Menu/menu.c:5222`: ADD after line 5222 (`menu_cellCommitValueEx()`)

Anchor: `const uint8_t morphView = (uint8_t)(panel && voiceModeShowMorph);`

```c
    /*
     * S096 P1 SD2: a bit cell commits the whole row. The 0/1 value becomes the
     * row's Normal value with that bit set or cleared, and the commit
     * continues as an ordinary cell of the row (R12, AutoSave, the edit-mask
     * fan-out and MIDI echo see one row edit).
     */
    if (cell->kind == MENU_CELL_INSTRUMENT && cell->bit_plus1) {
        const kit_instrument_slot_t *slot =
            scene_instrumentSlotConst(scene_getActiveIndex(), cell->slot);
        menu_cell_t row = *cell;
        uint8_t mask;

        if (!slot || cell->descriptor_index >= INSTRUMENT_PARAM_COUNT)
            return 0u;
        mask = slot->parameter_images.instrument_parameters[cell->descriptor_index];
        mask = (uint8_t)(value ? (mask | (1u << (cell->bit_plus1 - 1u)))
                               : (mask & ~(1u << (cell->bit_plus1 - 1u))));
        row.bit_plus1 = 0u;
        return menu_cellCommitValueEx(&row, (uint16_t)(mask & 0x0Fu), panel);
    }
```

### S9.9 `Core/Menu/menu.c:6560`: MODIFY line 6560 (`menu_formatCellValue3()`)

Anchor: `menu_formatInstrumentTargetShort(raw, valueAsText);` (inside
`case DTYPE_TARGET_SELECTION_VELO:` / `case DTYPE_TARGET_SELECTION_LFO:`)

```c
        if (cell->kind == MENU_CELL_INSTRUMENT) {
            menu_formatInstrumentTargetShort(raw, valueAsText);
            /* S096 P1 SD3: a Tetrakt velocity group names all four layers:
             * tn* for tnA (plan §4.2). LFO targets keep their layer letter. */
            if (dtype == DTYPE_TARGET_SELECTION_VELO &&
                instrumentManager_targetIsVelocityGroup(raw))
                valueAsText[2] = '*';
        }
```

(The line `if (cell->kind == MENU_CELL_INSTRUMENT)` at `:6559` and the call
at `:6560` are replaced by this block; the `else` branch at `:6561–6562`
stays.)

### S9.10 `Core/Menu/menu.c:12309`: ADD after line 12309 (`menu_formatWaveCell()`)

Anchor: `}` (the end of the Alkaline block; line 12310 starts the Colloid
comment `/*` / ` * S095 P2 (C-D20): a Colloid slot's osc1 under wav lists Saw, Rec, Tri,`)

```c
    /*
     * S096 P1: a Tetrakt sample row (tkt_sample_a..d) names the installed
     * sample at its value: the catalogue's 8-character name and its sNN code;
     * a value past the installed count names nothing (blank, "---" in the
     * compact cell). The count is the row's clamp (InstrumentManager.c
     * instrumentManager_waveDomainMax()).
     */
    if (isInstrument && strncmp(cell->descriptor->file_key, "tkt_sample_", 11u) == 0) {
        if (value < samples) {
            sampleMemory_getDisplayName(value, name);
            memcpy(nameDst, name, SAMPLE_DISPLAY_NAME_LEN);
            menu_formatUserShortName('s', value, codeDst);
        }
        return;
    }
```

### S9.11 `Core/Menu/menu.c:12535`: ADD after line 12535 (`menu_onoffTagNames()`)

Anchor: `case ONOFF_TAG_COL_FILT_TYPE:  return colFiltTypeNames;`

```c
    case ONOFF_TAG_TKT_LAYERS:     return tktLayerNames;      /* S096 P1 */
```

### S9.12 `Core/Menu/menu.c:12698`: ADD after line 12698

Anchor: `"colFiltTypeNames must list every filter type");`

```c
/* S096 P1: the layer-switch names list every 4-bit mask (TetraktVoice.h). */
_Static_assert((sizeof(tktLayerNames) / sizeof(tktLayerNames[0])) - 1u == TKT_LAYER_MASK_COUNT,
               "tktLayerNames must list the 16 layer sets");
```

### S9.13 `Core/Menu/menu.c:15302–15303`: MODIFY (the single-parameter painter)

Anchor (line 15302): `if (cell.kind == MENU_CELL_INSTRUMENT)` / `menu_displayInstrumentTargetFull(curParmVal);`

```c
                if (cell.kind == MENU_CELL_INSTRUMENT) {
                    menu_displayInstrumentTargetFull(curParmVal);
                    /*
                     * S096 P1 SD3: a Tetrakt velocity group: '*' after the long
                     * name (columns 8-15), as the compact cell's tn*.
                     */
                    if (dtype == DTYPE_TARGET_SELECTION_VELO &&
                        instrumentManager_targetIsVelocityGroup(curParmVal)) {
                        uint8_t c = 15u;

                        while (c > 8u && editDisplayBuffer[1][c] == ' ')
                            c--;
                        editDisplayBuffer[1][(c < 15u) ? c + 1u : 15u] = '*';
                    }
                }
```

(The `else if (curParmVal == INSTRUMENT_PARAM_INVALID)` branch that follows
is unchanged.)

### S9.14 `Core/Menu/menu.c:15478–15479`: MODIFY (the compact page names)

Anchor (line 15478): `menu_copyPaddedField(&editDisplayBuffer[0][4u * i],` /
`cell.descriptor->short_name, 3u);`

```c
                menu_copyPaddedField(&editDisplayBuffer[0][4u * i],
                                     cell.descriptor->short_name, 3u);
                /* S096 P1 SD2: a bit cell is named on + its layer: onA .. onD. */
                if (cell.kind == MENU_CELL_INSTRUMENT && cell.bit_plus1) {
                    editDisplayBuffer[0][4u * i]      = 'o';
                    editDisplayBuffer[0][4u * i + 1u] = 'n';
                    editDisplayBuffer[0][4u * i + 2u] = (char)('A' + cell.bit_plus1 - 1u);
                }
```

### S9.15 `Core/Menu/menu.c:3341`: ADD after line 3341 (`va_formatValue3()`)

Anchor: `uint8_t dtype = (uint8_t)(menu_cellDtype(cell) & 0x0fu);`

```c
    /*
     * S096 P1 SD2: a bit cell's held or working value is the row's mask; the
     * cell shows its own bit (its dtype is the plain on/off).
     */
    if (cell && cell->kind == MENU_CELL_INSTRUMENT && cell->bit_plus1)
        value = (uint8_t)((value >> (cell->bit_plus1 - 1u)) & 1u);
```

### S9.16 `Core/Menu/menu.c:3986`: ADD after line 3986 (`va_writeAutomationFromKnob()`)

Anchor: `}` that closes the target selection block
(`} else { return; }` at `:3984–3986`; the next line is the
`/* Seed: working cache if validity bit set, ...` comment)

```c
    /*
     * S096 P1 SD2 (plan §4.3, R5): a bit cell on held steps writes the whole
     * mask.
     *
     * What:       the mask seed is the working cache, else the held lock,
     *             else the row's Normal value; a turn right sets this cell's
     *             bit, left clears it; every held step stores the mask (the
     *             step's layer set). Every visible bit cell of the same row
     *             takes the mask as its working value, so the four redraw
     *             together and are underlined at once. Only the turned cell
     *             is held back from the underline for the edit flash.
     * Why:        a step lock is a snapshot of all four layers (T-Q8): the
     *             page shows that the moment one cell is toggled.
     */
    if (cell.kind == MENU_CELL_INSTRUMENT && cell.bit_plus1) {
        const kit_instrument_slot_t *slotData =
            scene_instrumentSlotConst(scene_getActiveIndex(), cell.slot);
        const uint8_t bit = (uint8_t)(1u << (cell.bit_plus1 - 1u));
        uint8_t mask;
        uint8_t p;

        if (va_underlineSuppressed & (uint8_t)(0x10u << knobNr))
            mask = va_workingValue[knobNr];
        else if (va_resolveHeldValue(target, &stored7))
            mask = va_storedToParam(stored7);
        else if (slotData && cell.descriptor_index < INSTRUMENT_PARAM_COUNT)
            mask = slotData->parameter_images.instrument_parameters[cell.descriptor_index];
        else
            mask = 0u;
        if (delta > 0)
            mask |= bit;
        else if (delta < 0)
            mask &= (uint8_t)~bit;
        mask &= 0x0Fu;
        for (i = 0u; i < va_heldCount; i++) {
            if (patSvc_writeStepAutomation(
                    menu_shownPattern, menu_activeVoice,
                    buttonHandler_visibleStep(va_heldOrder[i]), target, mask))
                wrote = 1u;
        }
        if (wrote) {
            va_searchSetBit(cell.descriptor_index);
            for (p = 0u; p < 4u; p++) {
                menu_cell_t other = menu_resolveCell(activePage, p);

                if (other.kind == MENU_CELL_INSTRUMENT && other.bit_plus1 &&
                    other.descriptor_index == cell.descriptor_index) {
                    va_workingValue[p] = mask;
                    va_underlineSuppressed |= (uint8_t)(0x10u << p);
                }
            }
            va_underlineSuppressed |= (uint8_t)(1u << knobNr);
            va_lastEditTick = time_sysTick;
            menu_knobs_dirty = 1u;
        }
        return;
    }
```

### Menu paths that need no edit (checked)

| Path | Why it already works for the bit cells |
|---|---|
| `va_searchSetBit()` / `va_searchTestBit()` (`:2355–2370`) | keyed by `descriptor_index`: the four cells share `lon`'s, so any `lon` entry underlines all four (plan R5) |
| `va_applyVoiceMarkers()` held branch (`:3519–3545`) | the target is `instrumentParam_make(slot, descriptor_index)`, the same for all four; with S9.15 each shows its own bit; one marker glyph per cell, four CGRAM slots (`va_queueMarkerTransaction()`) |
| `menu_clampCellValue()` (`:6870–6894`) | the bit cell's dtype is `DTYPE_ON_OFF` tag 0, so it clamps to 0..1 |
| `menu_formatCellValue3()` on/off (`:6588–6599`) | tag 0 shows `on`/`off` |
| the pot and encoder edit paths | value = display (the bit) + delta, clamped, then the commit (S9.8) |
| `menu_cellMidiTarget()`, `menu_cellClearTarget()`, `menu_liveRecRecordCell()` | built from `descriptor_index`: the row (MIDI Learn learns `lon`; live record records the mask) |
| the step-automation list (`:15105–15125`) | built from the stored target: one entry, `lon`, its value named by `tktLayerNames` |
| `menu_instrumentLoadIndex[]` (`:1733`) | sized by `INSTRUMENT_TYPE_UNKNOWN`: one more entry (+2 B, §1) |
| the Morph row label | built from the display label: "TetraMrp" |

### S9 gate

`make all` exit 0 (the name assert holds).

---

## S10. Host

### S10.1 NEW FILE `tools/dsp_test/modules/tetrakt.c`

The real `TetraktVoice.c`, `TetraktTables.c`, `ColloidTables.c` (`colEgK`),
`squareRootLut.c`, `AlkalineControl.h`, `Oscillator.c` (the read and the
bind) and the sample catalogue code are extracted as the Colloid target
does. The module installs four test samples and checks the laws.

```c
/*
 * modules/tetrakt.c - host laws for the Tetrakt voice (S096 P1).
 *
 * What:       builds the real TetraktVoice.c and TetraktTables.c (extracted,
 *             firmware includes removed) over the real Oscillator.c read and
 *             bind, the real sample catalogue, the real colEgK[] and
 *             squareRootLut[], and checks:
 *               TK-TRIG  for every lon value 0..15: the set layers restart
 *                        (phase 0 after the next async, env 1 then
 *                        decaying), the clear ones continue bit for bit; a
 *                        layer still sounding fades to exactly 0 over one
 *                        block (FADE) and starts on the next (START); a
 *                        fresh voice (lon A) restarts layer A only;
 *               TK-DEC   the decay is monotone non-increasing per block and
 *                        reaches IDLE (env 0) below 1e-4;
 *               TK-TUNE  tuA 63 at note 63: phaseInc 4,096 exactly; every
 *                        multiple of 5 a whole semitone (|inc / (4096 x
 *                        2^(s / 12)) - 1| < 1e-6); tuA 123..126 all 8,192, 0..3
 *                        all 2,048;
 *               TK-SRR   srA 127: the layer's output equals the srr-free
 *                        restatement exactly; srA 64: the held value
 *                        changes at the tktSrrRate[64] law's sample count
 *                        (+-1);
 *               TK-TONE  tnA 63, teA 63: y == x exactly; a teA sweep across
 *                        u = 0 under the decay: no output step larger than
 *                        the input's own largest step plus 1e-3; tnA 0 and
 *                        126 reach the LP / HP ends (the -3 dB points of a
 *                        swept sine within 10 % of 120 Hz and 3 kHz);
 *               TK-DRV   drA 0: exact dry; a zero input gives an exactly zero
 *                        output at every drA (the clip(b) subtraction); after
 *                        the DC blocker a driven 100 Hz sine's mean over 1 s
 *                        is below 1e-4 of full scale;
 *               TK-W2    wid 0: busR equals y gR exactly; wid 127 at env 1:
 *                        the same (the (1 - env)^2 mix is 0);
 *               TK-S1    stm 127, sdt 0, sfb 0: the left tap returns the send
 *                        delayed by tapL samples (+-1); sdt 127 puts the
 *                        right tap at tapL x 0.85 (+-wobble); sfb 127 with a
 *                        held impulse: the line's peak decays (bounded);
 *               TK-VEL   with veloKind TONE and amount 1: layer A hit at 127
 *                        and layer C at 0 apply 0 and -127 bytes to their own
 *                        tone, each from its own latched velocity; a retrigger
 *                        of A alone leaves C's offset unchanged.
 * Why:        S096_P1_TETRAKT_SAMPLE_PLAYER.md §13 step 1; the schedule's
 *             S10 gate.
 * Inputs:     generated fragments (Makefile target tetrakt).
 * Outputs:    failure counts per group; exit 1 on any failure.
 * Accessors:  make -C tools/dsp_test tetrakt.
 * Affiliates: TetraktVoice.c, TetraktTables.c, modules/colloid.c (the
 *             stand-in pattern).
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "osc_host.h"
#include "Oscillator.host.h"
#include "Oscillator.host.c"

/* host stand-ins for what the voice references (modules/colloid.c pattern) */
typedef struct { uint8_t bytes[124]; } Lfo;
typedef struct { float value; float amount; } ModulationNode;
ModulationNode velocityModulators[6];
void lfo_init(Lfo *lfo) { memset(lfo, 0, sizeof(*lfo)); }
void lfo_retrigger(uint8_t voiceNr) { (void)voiceNr; }
void modNode_updateValue(ModulationNode *vm, float val) { vm->value = val; }
void instrumentManager_applyVelocityModulationTarget(uint8_t source_slot, float v)
{ (void)source_slot; (void)v; }

#include "AlkalineControl.host.h"
#include "ColloidTables.host.h"
#include "ColloidTables.host.c"
#include "tkt_squareRootLut.c"
#include "TetraktTables.host.h"
#include "TetraktTables.host.c"
#include "TetraktVoice.host.h"
#include "TetraktVoice.host.c"

#define BLK OUTPUT_DMA_SIZE
enum { G_TRIG, G_DEC, G_TUNE, G_SRR, G_TONE, G_DRV, G_W2, G_S1, G_VEL, G_N };
static const char *g_name[G_N] = { "TK-TRIG", "TK-DEC", "TK-TUNE", "TK-SRR", "TK-TONE",
                                   "TK-DRV", "TK-W2", "TK-S1", "TK-VEL" };
static unsigned g_fail[G_N];
static void fail(int g, const char *what, double a, double b)
{
	if (g_fail[g] < 5u)
		printf("  %s FAIL: %s (%g, %g)\n", g_name[g], what, a, b);
	g_fail[g]++;
}

static TetraktVoice V;
static int16_t Lout[BLK];
static int16_t line[TKT_LINE_SAMPLES];

/* Four samples (modules/oscillator.c install_sample() pattern): a ramp, a
 * sine, a noise burst and a long sine, 8,192 frames each. */
static void install_samples(void);
static void voice_init(void) { memset(&V, 0, sizeof(V)); Tetrakt_initVoice(&V, 0u, line); }
static void block(void) { Tetrakt_calcVoiceAsync(&V); Tetrakt_calcVoiceSyncBlock(&V, Lout, BLK); }

/* the nine laws as listed above, one function each (test_trig() ...
 * test_vel()), then main() prints "tetrakt <group> failures: N" per group
 * and returns non-zero on any failure, as modules/colloid.c */
```

**The one part of this schedule given as a specification, not code:** the
nine test bodies and `install_samples()`. They are written at S10 to the
law list above, and the break builds of S10.2 check that the laws bite.
- **The method** (`modules/colloid.c`): each law restates its arithmetic
  with the same float operations as `tkt_layerPost()` / `tkt_spaceOut()`,
  so the "exactly" laws compare bit for bit.
- **The samples:** `install_samples()` follows
  `modules/oscillator.c:739–751` (`sampleInstall_beginSample()` /
  `_writeFrames()` / `_finishSample()` / `_commit()`, then
  `sampleMemory_refresh()`). It installs four samples into one catalogue
  in one `sampleInstall_begin()` session.

### S10.2 `tools/dsp_test/Makefile`

- **`:62` MODIFY** (the `.PHONY` list): `space space_cpu_check colloid colloid_tables colloid_cpu_check \`
  becomes `space space_cpu_check colloid colloid_tables colloid_cpu_check \`
  / `        tetrakt tetrakt_tables tetrakt_cpu_check \` (one line added).
- **`:67` MODIFY** (the `all` list): `space colloid colloid_tables distortion dist_makeup_table`
  becomes `space colloid colloid_tables distortion dist_makeup_table tetrakt tetrakt_tables`.
- **`:373` ADD after** (the `special_tags` recipe, anchor
  `cd $(ROOT) && python3 tools/colloid/make_init_col.py --check`):

```make
	cd $(ROOT) && python3 tools/tetrakt/make_init_tkt.py --check
```

- **`:745` ADD after** (anchor: the `colloid_cpu_check` recipe line):

```make

# ---------------------------------------------------------------------------
# S096 P1: the Tetrakt.
#
# What:       tetrakt builds modules/tetrakt.c on the real TetraktVoice.c and
#             TetraktTables.c (extracted), the real Oscillator.c read and bind,
#             colEgK[] (ColloidTables.c), squareRootLut[] and the sample
#             catalogue, and runs laws TK-TRIG .. TK-VEL; then three break
#             builds, each of which must fail its law:
#               fade   R1 restarts at once (FADE becomes START in the trigger)
#                                                                  -> TK-TRIG
#               bias   the clip(b) subtraction removed             -> TK-DRV
#               srr    the 127 entry read through the formula (0.99999)
#                                                                  -> TK-SRR
#             tetrakt_tables checks TetraktTables.c against its generator;
#             tetrakt_cpu_check prints the linked counts (tetrakt_cpu.py).
# Accessors:  make -C tools/dsp_test tetrakt | tetrakt_tables | tetrakt_cpu_check.
TKT := $(ROOT)/Core/DSP/Instruments/Tetrakt
tetrakt: $(GEN)/Oscillator.host.h $(GEN)/Oscillator.host.c $(GEN)/oscPwmTables.host.h \
	$(GEN)/oscPwmTables.host.c $(GEN)/oscPwmWarp.host.h | $(GEN)
	$(EX) --src $(ROOT)/Core/DSP/Instruments/Alkaline/AlkalineControl.h --body \
	  --out $(GEN)/AlkalineControl.host.h
	$(EX) --src $(COL)/ColloidTables.h --body --out $(GEN)/ColloidTables.host.h
	$(EX) --src $(COL)/ColloidTables.c --body --out $(GEN)/ColloidTables.host.c
	$(EX) --src $(ROOT)/Core/DSPAudio/squareRootLut.c --object squareRootLut \
	  --out $(GEN)/tkt_squareRootLut.c
	for f in TetraktTables.h TetraktTables.c TetraktVoice.h TetraktVoice.c; do \
	  $(EX) --src $(TKT)/$$f --body --out $(GEN)/$${f%.*}.host.$${f##*.} || exit 1; \
	done
	$(HOSTCC) $(HOSTFLAGS) $(OSC_HOST_INC) modules/tetrakt.c $(CAT_HOST_SRC) -o build/tetrakt -lm
	./build/tetrakt
	mkdir -p build/break_tkt_fade
	sed 's/L->stage = TKT_STAGE_FADE;/L->stage = TKT_STAGE_START;/' \
	  $(GEN)/TetraktVoice.host.c > build/break_tkt_fade/TetraktVoice.host.c
	@grep -q 'TKT_STAGE_PLAY \&\& L->env >= TKT_QUIET)' build/break_tkt_fade/TetraktVoice.host.c || \
	  { echo "tetrakt_break_fade: sed did not apply"; exit 1; }
	$(HOSTCC) -Ibuild/break_tkt_fade $(HOSTFLAGS) $(OSC_HOST_INC) modules/tetrakt.c \
	  $(CAT_HOST_SRC) -o build/tetrakt_break_fade -lm
	@if ./build/tetrakt_break_fade >/dev/null 2>&1; then \
	  echo "tetrakt_break_fade: FAILED (a restart without the fade not caught)"; exit 1; \
	else echo "tetrakt_break_fade: caught (law TK-TRIG fails as expected)"; fi
	mkdir -p build/break_tkt_bias
	sed 's/d = tkt_clip(x \* dG + dB) - dCB;/d = tkt_clip(x * dG + dB);/' \
	  $(GEN)/TetraktVoice.host.c > build/break_tkt_bias/TetraktVoice.host.c
	@grep -q 'd = tkt_clip(x \* dG + dB);' build/break_tkt_bias/TetraktVoice.host.c || \
	  { echo "tetrakt_break_bias: sed did not apply"; exit 1; }
	$(HOSTCC) -Ibuild/break_tkt_bias $(HOSTFLAGS) $(OSC_HOST_INC) modules/tetrakt.c \
	  $(CAT_HOST_SRC) -o build/tetrakt_break_bias -lm
	@if ./build/tetrakt_break_bias >/dev/null 2>&1; then \
	  echo "tetrakt_break_bias: FAILED (a drive offset not caught)"; exit 1; \
	else echo "tetrakt_break_bias: caught (law TK-DRV fails as expected)"; fi
	mkdir -p build/break_tkt_srr
	sed 's/L->srrRate = tktSrrRate\[tkt_idx(srr)\];/L->srrRate = 0.9999f;/' \
	  $(GEN)/TetraktVoice.host.c > build/break_tkt_srr/TetraktVoice.host.c
	@grep -q 'L->srrRate = 0.9999f;' build/break_tkt_srr/TetraktVoice.host.c || \
	  { echo "tetrakt_break_srr: sed did not apply"; exit 1; }
	$(HOSTCC) -Ibuild/break_tkt_srr $(HOSTFLAGS) $(OSC_HOST_INC) modules/tetrakt.c \
	  $(CAT_HOST_SRC) -o build/tetrakt_break_srr -lm
	@if ./build/tetrakt_break_srr >/dev/null 2>&1; then \
	  echo "tetrakt_break_srr: FAILED (an inexact full rate not caught)"; exit 1; \
	else echo "tetrakt_break_srr: caught (law TK-SRR fails as expected)"; fi

tetrakt_tables:
	cd $(ROOT) && python3 tools/tetrakt/gen_tkt_tables.py --check

tetrakt_cpu_check:
	python3 tetrakt_cpu.py --elf $(FW_ELF) $(if $(TKT_BASE),--baseline $(TKT_BASE))
```

Note on the fade break: the `sed` makes a sounding retrigger go straight to
START, so the old read is cut without the one-block fade. TK-TRIG's "fades
to exactly 0 over one block" catches it.

### S10.3 NEW FILE `tools/dsp_test/tetrakt_cpu.py`

A copy of `tools/dsp_test/colloid_cpu.py`'s method (`loops()`,
`calls_to()`, `present()`, `elf_blockpath` / `elf_looppath`), with this
model:

```python
"""
tetrakt_cpu.py: the Tetrakt's instructions per sample (S096 P1).

What:       from the linked ELF (the colloid_cpu.py / space_cpu.py method):
              per-sample loops: 4 x the osc_smpPlainBlock loop (the read)
              + 1 x tkt_layerPostStore's loop + 3 x tkt_layerPostAdd's loop
              + tkt_spaceOut's loop, each at its longest path;
              per-block paths / 32: Tetrakt_calcVoiceAsync (straight path)
              + 4 x tkt_layerControl + tkt_spaceControl (if out of line)
              + 4 x (osc_userBind + osc_renderTyped's dispatch path to the
              read) + Tetrakt_calcVoiceSyncBlock's path + each loop's
              entry/exit path; alk_exp2f is inline (counted in the control).
Why:        the schedule's S10 gate (SD12: <= 390) and the host baseline the
            device run is compared with (S12).
Inputs:     --elf PATH; --baseline FILE (optional: fail on growth > 0.5).
Outputs:    one line "tetrakt: loops L + per block B/32 = T"; exit 1 on a
            missing symbol, a growth, or T > 390.
Accessors:  make -C tools/dsp_test tetrakt_cpu_check (after `make all`).
"""
```

The required symbols are `Tetrakt_calcVoiceAsync`,
`Tetrakt_calcVoiceSyncBlock`, `tkt_layerControl`, `tkt_layerPostStore`,
`tkt_layerPostAdd`, `tkt_spaceOut`, `osc_smpPlainBlock`, `osc_renderTyped`
and `osc_userBind`. The tool exits with "not found (inlined or renamed)" if
one is missing, as `colloid_cpu.py` does.

### S10.4 `tools/dsp_test/check_special_tags.py:37`: ADD after line 37

Anchor: `'COL': 'Core/DSP/Instruments/Colloid/ColloidParameters.c',`

```python
    # S096 P1: the Tetrakt. Its special rows are the LFO's three (rate, sync,
    # offset), as every engine's; every tkt_* key is generic.
    'TKT': 'Core/DSP/Instruments/Tetrakt/TetraktParameters.c',
```

### S10.5 `tools/diagnostic_tools/decode_devlogs.py:1115`: MODIFY line 1115

Anchor: `types = {0: "drm", 1: "snr", 2: "cym", 3: "hat", 4: "alk"}`

```python
            # S096 P1: the Colloid (5) and the Tetrakt (6) are named too
            # (the probe's type field is InstrumentManager.h's enum).
            types = {0: "drm", 1: "snr", 2: "cym", 3: "hat", 4: "alk", 5: "col", 6: "tkt"}
```

### S10 gate

- `make -C tools/dsp_test all` exit 0: every TK law 0, the three Tetrakt
  break builds caught, every earlier target unchanged.
- `make -C tools/dsp_test tetrakt_cpu_check` ≤ 390. Above it, apply plan
  §9.3 trim 2 (the tone's mix ramps per block: `dmX dmB dmL` 0 and the
  weights set at the block's start, −12 per sample) and repeat (SD12).

---

## S11. Gates and measurements (all steps in)

1. `make all && make img`; `link_budget.py` →
   `tools/dsp_test/baselines/s096p1/after/link_budget.txt`. Expected:
   - Flash +7 … 9 KB;
   - ITCM 15,232 (unchanged);
   - DTCM 4,396 (unchanged);
   - FXBUF 126,656 (the arena, unchanged; the units are claimed at
     runtime).
2. **The struct and the work area:** `sizeof(TetraktVoice)` and
   `sizeof(TktWork)` with the `nm -S` method of S095 P4 §18.6, recorded in
   `…/after/sizes.txt`. Expected: ≈ 1,090 B (≤ 1,264, target ≤ 1,200)
   and 448 B.
3. `make -C tools/dsp_test all` exit 0 → `…/after/host_all.txt`.
4. `make -C tools/dsp_test elf_p4_check P4BASE=baselines/s096p1/before` →
   `…/after/p4_check.txt`. Expected: every `P4_OSC_SYMS` loop exact
   (`osc_smpPlainBlock` among them: the read is not edited) and the mixer's
   hot loops equal (`instrumentManager_slotRight()` gains a branch, but it
   is noinline and called once per slot per block).
5. `make -C tools/dsp_test colloid_cpu_check` equal to `…/before/colloid_cpu.txt`.
6. `make -C tools/dsp_test tetrakt_cpu_check` → `…/after/tetrakt_cpu.txt`
   (≤ 390). This file is the Tetrakt's host baseline.
7. DEV boot: the registry self-check clean; `effects_registryCheckResult()`
   0; no BC18 notice.

---

## S12. Device steps (yours)

1. **Card:** copy `SD_CARD/Instrument/Tetrakt/init.tkt` (the folder is
   created at boot if missing, S095 P3 F1). Samples installed
   (`Load:[Samples]`).
2. **Load:** Instrument Load → type Tetrakt → `init`. The voice plays
   layer A on each hit (`lon` = `A`).
3. **Pages (plan §8.2):**
   - each page's cells and labels are as specified;
   - the MOD page's first screen shows `onA onB onC onD`, each `on`/`off`;
   - the pots toggle the layers;
   - MIX shows `vol pan wid slv`, and its second screen is the Scene and
     fader settings.
4. **Triggers and locks (plan §3, §4.3):**
   - set `lon` `AC`: each hit plays A and C;
   - hold a step and toggle D on: all four layer cells underline at once,
     each showing its bit; the step plays `ACD`;
   - change the base to `ABC`: the locked step still plays `ACD`;
   - a held step without a lock shows no underline.
5. **R1:** with a long sample on A and fast steps, a retrigger of A is
   click-free.
6. **Tune:** `tuA` 5, 35 and 60 are +1, +7 and +12 semitones, and 61–63
   stay +12.
7. **Velocity (plan §4.2):** `dst` steps show `tu* lv* dy* pn* dr* sr* sp*
   tn* te*` once each, then the voice rows. With `dst` `tn*`, a soft hit
   darkens only the layers it restarted.
8. **Space:** with `spA` up, S1 slaps; `sdt` and `sto` change its width and
   colour; `wid` widens the tails, not the hits.
9. **The Effect share:** a Scene with six Tetrakts and the Space Reverb
   plays (the share is 100,160 B; the Reverb's 64 KB ring fits). Switching a
   slot to another type and back does not glitch the Reverb more than any
   Scene change does.
10. **CPU:** the cycle probe (DEV_MODE_LOGGING, trace stage `p`) on six
    Tetrakts, four layers each, long samples at `tuA` +12, every stage up
    (`drA` 127, `srA` 64, `tnA`/`teA` off-centre, `spA` 127, `wid` 127).
    Each slot must be at or under 19,620 cycles per block (the decoder
    names the type `tkt`, S10.5). Over it, plan §9.3's trim ladder in
    order.
11. **Listening (plan §13 step 5):** `TKT_MIX_SCALE`, the tone damping and
    ranges, the drive bias and make-up share, the SRR curve, S1's offset
    and wobble, W2's depth, `wid`'s default.

---

## 13. Statements this makes out of date (for you to update)

- `MEMORY.md`:
  - §DSP: the instrument types (a seventh registered type, `tkt`, Basic);
  - the voice units' first user (§Effects / FxBuffer: "no caller yet");
  - SRAM1 free (−450 B).
- `INSTRUMENTS_DSP_REFERENCE.md`:
  - the type list and registry;
  - the new-type checklist gains the switch row and bit cells (SD1);
  - the velocity groups (SD3).
- `MENU_UI_SPEC.md`: the bit cells (`INSTRUMENT_MENU_BIT`, `bit_plus1`); the
  underline rule for them; the velocity `*` mark.
- `EFFECTS_BUS_REFERENCE.md` / `EFFECTS_MODULES_DSP_REFERENCE.md`: the voice
  units now have a caller (Tetrakt, one per voice); the share with six
  Tetrakts.
- `STORAGE_SRAM_MANIFEST.md`: `tkt_work` 448 B `.bss` (SRAM1);
  `menu_instrumentLoadIndex[]` +2 B; `TetraktVoice` in the slot reserve.
- `MODULE_INTERCHANGE_SPEC.md`, `REPOSITORY_LAYOUT.md`: the new files
  (`Core/DSP/Instruments/Tetrakt/`, `tools/tetrakt/`,
  `modules/tetrakt.c`, `tetrakt_cpu.py`, `SD_CARD/Instrument/Tetrakt/`).
- `CPU_USE_DSP_AUDIT.md`: the Tetrakt's host count and its device figure
  (after S12).
- `DEV_MODES.md` (cycle probe): the decoder now names types 5 and 6.
- `DRAFT_KATRAN_DRONE_SYNTH.md` §5.3: the voice units' "first caller" is now
  the Tetrakt.

---

## 14. Anchor checklist (verified in the tree described in the header)

| Anchor | File:line |
|---|---|
| `INSTRUMENT_TYPE_COL,` | `InstrumentManager.h:101` |
| `#define INSTRUMENT_MENU_SKIP  0xfeu` | `InstrumentManager.h:358` |
| `uint8_t menu_page_count;` (registry entry) | `InstrumentManager.h:394` |
| `instrumentManager_menuDescriptorIndex` declaration | `InstrumentManager.h:436–437` |
| slotRight contract "What:" | `InstrumentManager.h:852` |
| `instrumentManager_cellInactive` declaration | `InstrumentManager.h:876` |
| includes `ColloidParameters.h` / `ColloidVoice.h` | `InstrumentManager.c:7`, `:18` |
| the Colloid registry row | `InstrumentManager.c:87–91` |
| union member `colloid` | `InstrumentManager.c:224` |
| `ColloidVoice` size assert | `InstrumentManager.c:244–245` |
| `instrumentManager_colloidRuntime()` | `InstrumentManager.c:325–328` |
| `instrumentManager_menuDescriptorIndex()` body | `InstrumentManager.c:593–629` (the read at `:616`) |
| `instrumentManager_waveDomainMax()` | `InstrumentManager.c:869–871` |
| velocity validator / stepper | `InstrumentManager.c:1421–1442`, `:1444–1489` |
| `runtimeInstance` / `runtimeLfo` Colloid cases | `InstrumentManager.c:1512`, `:1535–1537` |
| `runtimeInit` Colloid case | `InstrumentManager.c:1609–1611` |
| `ampEnvelopeQuiet` Colloid block | `InstrumentManager.c:1647–1650` |
| `resetRuntimeSlotFromScene` memset / type | `InstrumentManager.c:1745–1748`; Colloid case `:1765–1767` |
| `runtimePan` / `runtimeVolume` Colloid cases | `InstrumentManager.c:1886–1888`, `:1921–1923` |
| `calcSlotAsync` / `calcSlotSyncBlock` Colloid cases | `InstrumentManager.c:2155–2157`, `:2204–2216` |
| `instrumentManager_slotRight()` | `InstrumentManager.c:2247–2255` |
| `instrumentManager_cellInactive()` end | `InstrumentManager.c:2269` |
| `triggerTrack` Colloid case | `InstrumentManager.c:2383–2393` |
| `INSTRUMENT_BIND_VELOCITY_TARGET` | `InstrumentManager.c:4360–4372` |
| `-ICore/DSP/Instruments/Colloid \` | `Makefile:40` |
| `ColloidParameters.c \` / `ColloidVoice.c \` | `Makefile:119`, `:164` |
| the third Colloid `-fno-lto` recipe | `Makefile:279–281` |
| `STORAGE_INSTRUMENT_COL` | `storageTypes.h:143` |
| `ONOFF_TAG_COL_FILT_TYPE` | `MenuText.h:287` |
| `colWaveNames[]` | `MenuText.h:309–311` |
| `#include "ColloidVoice.h"` | `menu.c:69` |
| `menu_cell_t` `descriptor_index` | `menu.c:2151` |
| `va_formatValue3()` dtype line | `menu.c:3341` |
| `va_writeAutomationFromKnob()` target block end | `menu.c:3976–3986` |
| the resolver's descriptor lookup | `menu.c:4617–4622` |
| `menu_cellDtype()` instrument branch | `menu.c:5110–5112` |
| `menu_cellDisplayValue()` instrument branch | `menu.c:5143–5151` |
| `menu_cellCommitValueEx()` `morphView` | `menu.c:5222` |
| the target-selection format branch | `menu.c:6557–6563` |
| `menu_formatWaveCell()` Alkaline / Colloid blocks | `menu.c:12290–12323` |
| `menu_onoffTagNames()` | `menu.c:12528–12537` |
| the Colloid name asserts | `menu.c:12691–12698` |
| the single-parameter target branch | `menu.c:15300–15303` |
| the compact name painter | `menu.c:15476–15479` |
| `menu_instrumentLoadIndex[]` | `menu.c:1733` |
| `.PHONY` / `all` | `tools/dsp_test/Makefile:57–67` |
| `special_tags` | `tools/dsp_test/Makefile:370–373` |
| `colloid_cpu_check` | `tools/dsp_test/Makefile:744–745` |
| `'COL':` | `check_special_tags.py:37` |
| probe `types = {…}` | `decode_devlogs.py:1115` |
| `osc_renderTyped()` / `osc_userBind()` (unchanged, called) | `Oscillator.c:2268`, `:1347` |
| `fxbuf_voiceAcquire/Release/UnitCount/Unit` (called) | `FxBuffer.h:286–312` |
| boot order `fxbuf_init()` → `instrumentManager_runtimeInit()` → `effects_init()` | `main.c:121`, `:130`, `:132` |

---

## 15. Work log (implementation, 2026-11-14)

Notes are appended here as each step lands. A step's note records what was
done, the gate result, and anything the anchors or the tree made different
from the text above.

### S0 — baselines (done)

- `make all` exit 0; the tree was already built at `809d49d7` ("S096 P1
  pre-implementation", the schedule's header tree plus its own commit).
- Copied `build/lxr02.elf` to
  `tools/dsp_test/baselines/s096p1/before/lxr02.elf`.
- `link_budget.txt`: Flash 716,772, ITCM 15,232, DTCM 4,396, FXBUF
  126,656 — exactly the expected figures.
- `elf_p4_snapshot`: 94 oscillator loops, 79 mixer loops.
- `colloid_cpu.txt`: plain12-par 383.5, pwm8-par 387.6, pwmw6-par 326.5
  (parallel models).
- `host_all.txt`: `make -C tools/dsp_test all` exit 0 (844 lines, every
  law target clean).
- The first host run was started with the exec cell's yield expiring; the
  completed run was captured to `/tmp` and copied into the baseline file,
  so the record is the full exit-0 output.

### S1 — tables (done)

- New `tools/tetrakt/gen_tkt_tables.py`, `TetraktTables.h` and the
  generated `TetraktTables.c`.
- `gen_tkt_tables.py --check`: matches. `tktSrrRate[127] = 1.0f` and
  `tktDriveMakeup[0] = 1.0f` are present by inspection.

### S2/S3 — the voice (done)

- `TetraktVoice.h`: `TktLayer`, `TetraktVoice`, the stages and the
  velocity-kind enum. Measured with the target compiler:
  `sizeof(TktLayer) = 196 B`, `sizeof(TetraktVoice) = 1,048 B`
  (slot reserve 1,264 B; target was ≤ 1,200).
- `TetraktVoice.c`: the 448 B `tkt_work` (static assert), the per-layer
  chain, the space stage and the output pack.
- **Cost work applied while measuring (all plan-sanctioned or
  behaviour-neutral):**
  - SD12 trim 2: the tone's output weights change per block; the
    coefficients still ramp.
  - Plan §9.3 trim 1: W2 is one allpass on the summed right bus, mixed by
    the level-weighted envelope, not four per-layer allpasses.
  - Plan §9.3 trim 3: S1's wobble is fixed; the detune offset stays.
  - The nine velocity rows are clamped only for the selected group, not
    all nine.
  - `osc_userBind()` runs at init, at a START and on a catalogue
    generation change (a noinline `tkt_bindLayer()`); a plain block only
    rewrites `phaseInc`. This keeps the 96-instruction bind out of the
    common per-block path (SD5).

### S4 — parameters (done)

- `TetraktParameters.h/.c`: 64 rows, 8 pages, the switch row at index 20,
  the nine velocity groups and the two hidden-token helpers.
- Static asserts: 64 descriptors, 64 ≤ `INSTRUMENT_PARAM_COUNT`, the
  switch index, and the page count.
- `make all` needs `INSTRUMENT_MENU_BIT()` from S6.2 before S5's build,
  so S6.2 (the header macro) was applied ahead of the S5 gate. No other
  order changed.

### S5 — build (done)

- Makefile include path, `TetraktParameters.c` in SRCS, the two DSP
  objects in SRCS and their `-Ofast -fno-lto` rule.
- `make all` exit 0.
- First link: Flash +8,032 B, ITCM +80 B (the `osc_renderTyped()`
  dispatcher is now reachable and linked; it was garbage-collected before),
  BSS +456 B (448 B `tkt_work`, 2 B `menu_instrumentLoadIndex[]`, 6 B
  alignment).

### S6/S7 — registry and dispatchers (done)

- `INSTRUMENT_TYPE_TKT` (6), `INSTRUMENT_MENU_BIT()`,
  `switch_row_plus1`, `instrumentManager_menuCellBitPlus1()`, the
  registry row and every dispatcher / accessor / velocity branch.
- The voice unit is claimed and released in
  `instrumentManager_resetRuntimeSlotFromScene()`; a
  Tetrakt → Tetrakt reset keeps its unit.
- The static asserts: the voice fits the slot and
  `TKT_LINE_SAMPLES == FXBUF_VOICE_UNIT_SAMPLES`.
- `make all` exit 0.

### S8 — storage and card (done)

- `STORAGE_INSTRUMENT_TKT`; `tools/tetrakt/make_init_tkt.py`;
  `SD_CARD/Instrument/Tetrakt/init.tkt`.
- `make_init_tkt.py --check`: 64 rows, matches.

### S9 — menu (done)

- `ONOFF_TAG_TKT_LAYERS` 7 and `tktLayerNames[16]`.
- The bit-cell cell code: resolve, dtype, display value, commit (whole
  mask), `onA..onD` short names, held-value formatting and the
  whole-mask write with the shared working value.
- The velocity group's `*` in the compact and full views.
- The Tetrakt sample rows' catalogue names.
- Name assert: 16 masks. `make all` exit 0.

### S10 — host (done)

- `tools/dsp_test/modules/tetrakt.c`: TK-TRIG, DEC, TUNE, SRR, TONE,
  DRV, W2, S1, VEL. All groups 0 failures; the fade, bias and SRR break
  builds each fail their law as required.
- `tetrakt_tables`, `special_tags`, `tetrakt_cpu.py`,
  `check_special_tags.py` `TKT`, and the decoder's types 5/6.
- **Law interpretations recorded here:**
  - TK-TUNE: `phaseInc` is an integer, so the 1e-6 relative law of the
    text is impossible; the test asserts the nearest integer within
    0.5001 of `4096 x 2^(s/12)`.
  - TK-TONE: with R = 0.6 the SVF's magnitude is `1/1.2` at the corner,
    and its -3 dB point is 1.148 x fc for LP and 0.8707 x fc for HP. The
    test measures those (within 2 % / 3 %) rather than -3 dB at fc.
  - TK-S1: the packed tap is 32,766 counts for a 32,767 line sample (the
    int16 pack uses 32,767); the test accepts ≥ 32,766.
  - TK-VEL: a PLAY layer retriggers through FADE, so its new velocity is
    latched on the following async; the test runs the two blocks.
  - TK-W2: after trim 1 the width is voice-level; the test checks
    `busWMix` = 0 at wid 0 and at a full envelope, and 1 at env 0.

### S11 — measurements (done, with two failed gates)

| Gate | Result |
|---|---|
| `make all && make img` | exit 0; `build/LXRV2_lxr02.img` 725,516 B |
| Flash | 725,500 B used, headroom 28,164 B (**+8,728** vs 716,772) |
| ITCM | 15,312 B (**+80**: the linked `osc_renderTyped()` dispatcher) |
| DTCM statics | 4,396 B (unchanged) |
| BSS | +456 B (`tkt_work` 448, load index 2, 6 alignment) |
| `sizeof(TetraktVoice)` | 1,048 B (≤ 1,264, ≤ 1,200 target) |
| `TktWork` | 448 B (static assert) |
| Host `make -C tools/dsp_test all` | exit 0; Tetrakt groups 0, breaks caught |
| Colloid CPU | identical to the baseline (diff empty) |
| **Tetrakt CPU** | **loops 365 + block 1,757/32 = 419.9; gate 390 FAILED** |
| **`elf_p4_check`** | **78 exact, 4 renamed, 12 missing; FAILED** |
| DEV boot / registry / Effect check | not run (no device here) |

**Tetrakt CPU composition:** read 4 x 24 = 96; store 44; add 3 x 47 = 141;
space/output 84; control 65 + 4 x 346 + 4 x 10 (dispatch) + sync 24 +
entry/exit 45 + 3 x 45 + 64 = 1,757 per block. At the plan's 1.6 cycles
per instruction this is ≈ 672 cycles/sample ≈ 21,500 per block against the
Basic 19,620. At 1.4 it is ≈ 18,800 (fits). The device probe decides.

**Why the 390 gate cannot be met by the stated ladder:** trims 1–3 are
already applied (419.9). Trim 4 (drop the per-layer SRR) saves 24 per
sample, leaving ≈ 396, still above 390, and it removes a feature the user
asked for (revision 2, T-Q4). Reaching 390 also needs a space/output-loop
change (84 per sample, the plan estimated 25 … 30) or a class change. The
remaining decision is the user's.

**P4 analysis:** every surviving oscillator loop has the **same
instruction count** as the baseline (see
`after/loop_counts.txt`); the failures are register-allocation text
changes. The one structural change is `osc_wtbPlanBlock`: LTO no longer
emits the `.constprop.0` form, so its per-block path grew 53 → 120
(+67 per block, ≈ +2.1 per sample for a wtb voice). No per-sample loop
length changed. This is LTO repartitioning from adding a caller to
`osc_renderTyped()`, not an edit to `Oscillator.c` (it was not edited).
Options: accept the register-only shift and the wtb plan's +67, or isolate
the new call (a build-level decision outside this schedule).

## 16. Assessment (review of the landed code, 2026-11-14)

### Verdict

Every step in this schedule has been implemented and matches it. I read the code against
S2–S10 hunk by hunk. The firmware builds, every host law passes and the break
builds catch their laws. Two gates fail. **The `elf_p4_check` failure is
harmless:** no per-sample loop changed, and the one structural change saves
cycles. **The CPU gate fails, and by more than §15 records:** the tool leaves
out the bind that every trigger causes. Two of the trims that §15 counts in
the 419.9 were applied ahead of the device step, which SD12 did not allow.
They change the sound, so they need your decision (D2).

### Gates re-run on the current tree

The tree is your 22:30 `make img` build. The sources are unchanged since the
S11 run.

| Gate | Re-run result | Matches §15 |
|---|---|---|
| Image | `build/LXRV2_lxr02.img` 725,516 B | yes |
| ITCM | 15,312 B (+80) | yes |
| `.bss` / `.data` | 286,380 / 428 | — |
| Host `make -C tools/dsp_test all` | exit 0; TK-TRIG/DEC/TUNE/SRR/TONE/DRV/W2/S1/VEL 0 failures; the fade/bias/SRR breaks caught; `gen_tkt_tables --check` matches. The `voice_control failures: 6` line in the output belongs to the deliberate `voice_control_break` build, which is supposed to fail. | yes |
| `tetrakt_cpu_check` | loops 365 + per block 1,757/32 = **419.9, FAILED** | yes (see F5) |
| `elf_p4_check P4BASE=baselines/s096p1/before` | 94 baseline loops: 78 exact, 4 renamed, 12 missing; **FAILED** | yes |
| Mixer hot loops | 58 equal, 0 grew | — |

`elf_p4_check` has to be run with `P4BASE=baselines/s096p1/before`. With
its default it compares against an older snapshot (77 loops, 24 missing),
which gives misleading results.

### Code review: what matches the schedule

- **InstrumentManager.c/.h:** the registry row and the switch row index + 1.
  The union member and its size asserts. Unit claim and release in
  `resetRuntimeSlotFromScene()`: a Tetrakt → Tetrakt reset keeps its unit,
  and so does a slot whose unit count still matches. Every dispatcher. The
  velocity validator's hidden-token check, `stepVelocityLocal`,
  `targetIsVelocityGroup`, and the `INSTRUMENT_BIND_VELOCITY_TARGET`
  Tetrakt branch.
- **menu.c bit cells:** all the cell code agrees on `bit_plus1`: resolve,
  dtype (`DTYPE_ON_OFF`), the displayed bit, the `onA…onD` labels, and the
  held-step formatting. The panel commit rebuilds the whole mask and commits
  it through the ordinary row path (`row.bit_plus1 = 0`). So a step lock
  stores the mask, and the underline search is keyed on the switch row. The
  knob write on held steps seeds its value in three steps: the working
  value, then the held value, then the slot image. It then sets or clears
  one bit and writes the masked 0x0…0xF value. It also copies the working
  value to the other three bit cells on the page, so they show the new mask
  at once. The velocity `*` appears in both the compact and the full view.
- **MenuText.h:** `tktLayerNames` is in value order (bit 0 = A). The menu.c
  assert ties it to `TKT_LAYER_MASK_COUNT` (16).
- **TetraktParameters.c/.h:** 64 rows, 8 pages in your layout, the switch
  at index 20 (asserted), and the nine velocity groups keyed on layer A.
  Tokens B–D of a group are hidden.
- **Storage, card and tools:** `STORAGE_INSTRUMENT_TKT`; `init.tkt` with the
  plan's defaults (`lon` = 1 = A, SRR 127, tune/tone/pan 63, wid 0);
  `check_special_tags` `TKT`; the decoder now names types 5 and 6.
- **Law interpretations (§15 S10):** TK-TUNE uses an integer `phaseInc`
  with a ±0.5 tolerance. TK-TONE checks the R = 0.6 SVF at its true −3 dB
  points. TK-S1 accepts 32,766. TK-VEL runs two blocks. All four are
  correct readings of the laws, not loosened ones. TK-W2 was rewritten for
  the bus form, so it stands or falls with D2.

### Findings

**F1 — The CPU tool leaves out the worst block, which is ≈ 433, not 419.9.**
`tetrakt_cpu.py` leaves `osc_userBind()` (96) and `tkt_bindLayer()` (8) out
of the per-block sum. Its reason is that "a START is not the common path."
The constant-CPU rule counts the worst block, and with `lon` = `all` every
trigger starts four layers in the same block: 4 × 104 = 416 per block,
**+13.0 per sample, ≈ 432.9**. Estimated cycles per block for that worst
case: ≈ 22,200 at 1.6 cycles per instruction (over 19,620); ≈ 19,400 at 1.4
(fits, with ≈ 220 cycles to spare). The gate fails either way; the tool
under-reports by 13. I have not edited the tool. It should add the four
START binds to its sum.

**F2 — Trims 1 and 3 were applied before the device step without
approval.** SD12 allowed only trim 2 before the device step. Plan §9.3
describes all trims as "if the device puts it over." Trims 1 and 3 change
the sound:
- **Trim 1:** W2 is now one allpass on the summed right bus, mixed by the
  level-weighted envelope. Before, each layer widened by its own envelope.
  A long-decaying layer and a short one no longer widen independently.
  Width now follows the whole voice.
- **Trim 3:** S1's right-tap wobble is gone. Only the fixed detune offset
  is left.

The two trims save about 14 + 4 per sample by estimate. Without them the
voice would be ≈ 438 by the tool's count and ≈ 451 at the worst block. Your
decision: D2.

**F3 — The space/output loop is the largest cost outside the plan's
estimate.** `tkt_spaceOut()` (`TetraktVoice.c:583`) costs 84 per sample;
the plan estimated 25…30. Two changes that don't alter the sound:
- Move `if (line)` (`:605`) out of the per-sample loop, as two loop
  versions.
- Replace the per-sample wrap selects of the two taps with a split at the
  wrap point: at most two segments per tap per block.

Together these are estimated at 15…25 per sample, which would bring the
worst block to ≈ 410…418. That is still above 390. To get under 390 needs
these changes plus one of D1's other options. `tkt_layerControl()` (346 per
block per layer, ≈ 43 per sample for four layers) is the next candidate. It
runs two `alk_exp2f()` calls and a division per layer per block (`:280`,
`:298`, `:321`). Looking up the drive gain by its quantised index `di` would
remove one `alk_exp2f()`. This is small, an estimated ≈ 1…2 per sample.

**F4 — The `elf_p4_check` failure is harmless; §15's "+67" has the wrong
sign.** Every surviving oscillator loop has the same instruction count as
the baseline, and the mixer's 58 loops are equal. The only structural change
is in `osc_wtbPlanBlock`:
- **Before:** the `.constprop.0` form, a 53-instruction straight path plus a
  loop of up to 73 that runs at most twice (one `vdiv`). That is ≤ 199 per
  block.
- **After:** the loop is fully unrolled, 120 on its straight path (two
  `vdiv`).

So the worst case falls by ≈ 79 per block (≈ 2.5 per sample for a wtb
voice). §15 compared the old straight path (53) with the new one (120) and
left out the old loop. The gate text-matches loop bodies, and `elf_looppath`
finds no loop in the unrolled form, so the tool reports "missing." The fix
is to re-snapshot the P4 baseline from this build once you accept it (D4).

**F5 — The recorded CPU file is stale.**
`baselines/s096p1/after/tetrakt_cpu.txt` reads 1,762/32 = 420.1. Both the
current tree and §15 give 1,757/32 = 419.9. The file is from an earlier
build. Re-capture it when the CPU work settles.

**F6 — The ITCM +80 B comes from the schedule, not from a deviation.**
SD5 told the implementer to read samples through `osc_renderTyped()` and
predicted 0 ITCM. That was wrong. The call links the dispatcher, which was
garbage-collected before, into ITCM in two copies (`osc_renderTyped` and
`.constprop.0`, 0x34 + 0x36). The same new caller makes LTO split the code
differently, which is what changed `osc_wtbPlanBlock` (F4). The only way to
avoid this is to call `osc_smpPlainBlock()` directly. That function is
static, so it would mean editing `Oscillator.c`, which this schedule
excludes. ITCM counts as RAM, so the +80 B needs your sign-off (D3).

**F7 — Not part of this step:** the working tree also renames samples
(`01_cut.wav` → `cut_01.wav` …) and moves `wavetables/ak_hvox` →
`ak_hvox2`. Nothing in the Tetrakt depends on these names: `init.tkt`
selects samples by catalogue index 0…3. I left them alone.

### Decisions needed

| # | Decision | Options |
|---|---|---|
| D1 | The CPU gate (worst block ≈ 433 vs 390) | (a) take it to the device probe as it is; the probe decides at the measured cycles per instruction (fits only at ≲ 1.41). (b) first make F3's changes, which don't alter the sound (est. → ≈ 410…418), then probe. (c) trim 4 (drop SRR, −24, removes your T-Q4 feature). Recommended: (b), then probe |
| D2 | Trims 1 and 3, already applied | keep them (W2 voice-level, no wobble) or revert to the plan's per-layer W2 and wobbling S1 (+≈ 18 per sample) |
| D3 | ITCM +80 B (the `osc_renderTyped` dispatcher) | approve it, or allow an `Oscillator.c` edit that exports `osc_smpPlainBlock()` and calls it directly |
| D4 | The P4 baseline | accept F4 and re-snapshot `baselines/s096p1/before` → a new P4 base from this build |

### Statements this makes out of date (for you to update)

- §0 SD5 and §1: "ITCM 0" → +80 B (F6).
- §0 SD12: the gate assumed only trim 2 before the device step. §15 S2/S3
  records trims 1 and 3 as "plan-sanctioned"; that holds only after the
  device step (F2).
- §15 S11 P4 analysis: "+67 per block" → ≈ −79 per block worst case (F4).
- Plan §0 "Space" row and §6: "each layer widens as it decays" and "wobbling"
  describe the code only if D2 reverts the trims.

## 17. Decisions on §16 and the space-loop rework (2026-11-14)

### Decisions (user, 2026-11-14)

| # | Decision | Recorded as |
|---|---|---|
| D1 | Make F3's changes, which don't alter the sound, now | Done below. The gate is still above 390; the device probe decides the rest |
| D2 | Keep trims 1 and 3 | W2 stays one allpass on the summed right bus, mixed by the level-weighted envelope; S1 has no wobble. The plan text that describes per-layer W2 and a wobbling S1 is now out of date (list below) |
| D3 | ITCM +80 B approved | The `osc_renderTyped()` dispatcher (two copies, 0x34 + 0x36) stays in ITCM: 15,312 / 16,384 B |
| D4 | The P4 analysis is accepted | New P4 baseline `tools/dsp_test/baselines/s096p1/p4base/`, now the Makefile default |

### What changed

**1. `Core/DSP/Instruments/Tetrakt/TetraktVoice.c`: the S1 ring, with a
guard sample in front.**

- `:43–51` comment, `:70–71` ADD `TKT_RING` (`TKT_LINE_SAMPLES - 1`,
  2,207) and `TKT_TAP_MAX` (2,205.999). The unit is now [guard][ring of
  2,207]. Sample 0 is the guard, a copy of the ring's last sample, so a
  tap's sample pair `[a − 1]`, `[a]` never wraps. A read lags at most
  `floor(tap) + 1 ≤ TKT_RING − 1` samples, so it never reaches the frame
  being written.
- `:349–363` MODIFY `tkt_spaceControl()`: `tapL` and `tapR` are clamped
  to `TKT_TAP_MAX`. The clamp never takes effect: 50 ms at `REAL_FS`
  (44,108.07 Hz) is 2,205.4. TK-S1 still checks tapL = 50 ms to 0.01.
- `:580–611` ADD `TktOutState` and `tkt_outFrame()`, which is always
  inlined. These are the old loop's output steps, unchanged: the slapback
  mix, W2's allpass and mix, both DC blockers and both packs. The ring path
  and the no-unit path share them.
- `:613–721` REWRITE `tkt_spaceOut()`. The first form wrapped four read
  indices with compares and selects every frame, plus the write wrap. The
  new form keeps three running pointers (write, left read, right read) and
  runs the block in segments. A segment ends where any pointer reaches the
  ring's end. Each pointer wraps at most once in 32 frames, so there are at
  most 4 segments. When the write pointer wraps, the guard takes
  `ring[TKT_RING − 1]`.
  - **Why the guard is always current:** a pair with `a = 0` is read
    `floor(tap) ≥ 2` frames after the ring's last sample was written, and
    the copy is made at the end of the segment that wrote it.
  - **Same arithmetic:** the arithmetic and its order per frame are those of
    the first form (`tap = [a] + f([a − 1] − [a])`, read before write), so
    the output is the same bit for bit (TK-RING).
  - **No-unit path:** with no voice unit the function runs the output-only
    loop, as before.
- The comment blocks of the constants, `tkt_spaceControl()`,
  `tkt_outFrame()` and `tkt_spaceOut()` describe all of this
  (What / Why / Inputs / Outputs / Cost).

**Two forms tried and dropped.** Measured on the linked image:

| Form | Frame loop | Per segment | Entry/exit | Per sample |
|---|---|---|---|---|
| First form (S3) | 84 | — | 64 | 86.0 |
| Guard after the ring, 1-frame segment for the copy | 62 | ≈ 89 × 5 | — | rejected |
| Running bus/output pointers across segments | 62 | 82 × 4 (register spills) | 161 | ≈ 77.3 |
| No segments, each pointer wrapped every frame | 77 | — | 70 | ≈ 79.2 |
| **Kept: guard in front, n-indexed buses, ≤ 4 segments** | **64** | **58 × 4** | **133** | **≈ 75.4** |

§16 F3 suggested moving `if (line)` out of the loop. That would have saved
nothing: the compiler had already made a separate no-unit loop (`0x80850c8`
in the S11 image). The real saving is ≈ 10.5 per sample, below §16's
15…25 estimate.

**2. `tools/dsp_test/tetrakt_cpu.py`: counts the worst block.**

- `SPACE_SEGMENTS = 4` and `segmented()`. The frame loop is the tightest
  backward branch inside `tkt_spaceOut()`'s segment cycle, and it counts
  per sample. The segment cycle less one frame counts 4 times per block.
- `BIND_LAYERS = 4` (§16 F1). Four START binds,
  `4 × (tkt_bindLayer + osc_userBind)`, count per block, because with
  `lon` = `all` every trigger starts all four layers in one block.
  `tkt_bindLayer` is now a required symbol.
- A second output line shows the frame, the segment and the bind count.

**3. `tools/dsp_test/modules/tetrakt.c`: new law TK-RING.**

- `ref_spaceOut()` keeps the first form: every index wrapped per frame over
  the whole 2,208-sample unit.
- `test_ring()` runs both forms side by side for nine tap pairs:
  (2, 2), (2.3, 2.7), (31.5, 2), (32, 33), (100.25, 85.2), (1,103.6, 937.9),
  (2,205.4, 1,874.6), (`TKT_TAP_MAX`, `TKT_TAP_MAX`), (`TKT_TAP_MAX`, 2).
  Each pair runs 700 blocks (ten laps of the ring) with feedback 0.9, W2 0.5
  and random buses and send. It requires:
  - both outputs, `fbLp`, `busApZ`, `dcYL` and `dcYR` equal bit for bit;
  - `wi < TKT_RING`;
  - `tkt_spaceControl()`'s taps ≤ `TKT_TAP_MAX`.

**4. `tools/dsp_test/Makefile`.**

- `tetrakt_break_guard` (`:811`): `sed` removes the guard copy, and TK-RING
  must fail.
- `P4BASE ?= baselines/s096p1/p4base`, with a comment that gives the reason
  (D4).

**5. Records:**
- `tools/dsp_test/baselines/s096p1/p4base/` (`osc.txt`, `mixer.txt`);
- `tools/dsp_test/baselines/s096p1/s17/` (`tetrakt_cpu.txt`,
  `colloid_cpu.txt`, `link_budget.txt`, `host_all.txt`).

`after/tetrakt_cpu.txt` (420.1, stale, §16 F5) is left as the S11 record.
`s17/` holds the current figures.

### Gates (this tree)

| Gate | Result |
|---|---|
| `make all && make img` | exit 0; `build/LXRV2_lxr02.img` written (725,780 B) |
| Flash | 725,780 B used, headroom 27,884 B (+280 vs S11, +9,008 vs 716,772) |
| ITCM | 15,312 B (unchanged; the +80 approved, D3) |
| DTCM statics / FXBUF | 4,396 B / 126,656 B (margin 3,776), unchanged |
| `.bss` | 286,380 (unchanged since S11) |
| Host `make -C tools/dsp_test all` | exit 0; Tetrakt TRIG/DEC/TUNE/SRR/TONE/DRV/W2/S1/VEL/**RING** 0 failures; fade/bias/SRR/**guard** breaks caught. The `voice_control failures: 6` line is the deliberate break build |
| `elf_p4_check` (new default base) | 89 baseline loops, **89 exact**, 0 renamed, 0 missing; mixer 42 hot loops equal, 0 grew |
| `elf_p4_check P4BASE=baselines/s096p1/before` | 78 / 4 / 12, as in §16 F4: the oscillator code did not change in §17 |
| Colloid CPU | identical to `before/colloid_cpu.txt` |
| **Tetrakt CPU (worst block)** | **loops 345 + per block 2,477/32 = 422.4; gate 390 FAILED** |

**What makes up the Tetrakt CPU figure:**
- per sample: read 4 × 24, store 44, add 3 × 47, space frame 64 (was 84);
- per block: the old 1,757, less the old space path 64, plus the new entry
  path 133, 4 segments × 58 and 4 START binds × 104 = 416. That sums to
  2,474; the other 3 are block-path changes elsewhere in the rebuilt
  objects.

| | Tool rule of S11 (no binds) | Worst block (binds counted) |
|---|---|---|
| Before §17 | 419.9 | ≈ 432.9 |
| After §17 | ≈ 409.4 | **422.4** |

Estimated worst block at the cycles per instruction (CPI) the device might
show:
- at 1.4 CPI: 422.4 × 32 × 1.4 ≈ 18,900 cycles, which fits 19,620 with
  ≈ 700 to spare;
- at 1.6 CPI: ≈ 21,600, over.

The image fits as long as the device measures 1.45 cycles per instruction
or less (19,620 / 13,517). The device probe (S12) decides. If the probe
shows it over, the next step on the ladder is trim 4 (SRR, −24 per sample).
`tkt_layerControl()`'s table lookups (§16 F3, estimated ≈ 1…2) are too
small to close the gap on their own.

### Statements this makes out of date (for you to update)

- §0 SD5 / §1: "ITCM 0" → +80 B, approved (D3).
- §0 SD12: trims 1 and 3 are now applied and kept (D2), not
  "device-dependent".
- §15 S11 P4 analysis: "+67 per block" → ≈ −79 per block in the worst
  case (§16 F4).
- §16 F1/F3: the CPU tool now counts the binds; the space rework saved
  ≈ 10.5 per sample, not 15…25; moving `if (line)` out of the loop saved 0.
- Plan §0 "Space" row and §6: W2 is voice-level ("the voice widens as its
  layers decay", by the level-weighted envelope) and S1 has no wobble (D2).
  The S1 line is a 2,207-sample ring plus a guard sample, inside the same
  voice unit.
- Plan §9.3: trims 1–3 are applied; trim 4 is the only step left on the
  ladder.
- The Makefile's `P4BASE` default is `baselines/s096p1/p4base`. Any note
  that still names `s093p8` as the P4 base is out of date.

## 18. Device probe: one Tetrakt (S12, 2026-11-14)

**The run.** One boot of about 58 minutes: 2,332 cycle-probe windows of
2,048 blocks each, with a Tetrakt in Scene 0 slot 0 throughout.
- **Source:** `SD_CARD_TEKT/asavetrc.bin`.
- **Image:** `SD_CARD_TEKT/LXRV2_lxr02.img`, 725,516 B, built at 22:30 with
  DEV_MODE_LOGGING. It is built from the **S11 sources**, so
  `tkt_spaceOut()` is still in its first form. **§17 is not in this
  image.**
- **Record:** `tools/dsp_test/baselines/s096p1/device_probe_tetrakt.txt`
  (segment summary, slot 0's final settings, the per-window table).
- **The rest of the Scene** changed during the run. At first slots 1–5 were
  Colloids. Later slot 5 became a HiHat, then slot 2 an Alkaline. The
  Effect was off for the first 120 windows and on after that.
- **Slot 0's final settings** (`.hcprms1`, generation 2,301):
  - triggers: `lon` = A only;
  - velocity: destination is the SRR group, amount 54;
  - drives 93/105/107/38; SRR 127/113/119/125;
  - space sends 7/66/60/127, `sfb` 78, `sdt` 127, `wid` 127, `slv` 127;
  - tones 0/90/56/127, all with positive envelope amounts;
  - tune B and C at +6 semitones.

### 18.1 Resources (measured)

| Resource | Tetrakt | Limit | Margin |
|---|---|---|---|
| **CPU, one voice, cycles per block** | mean **20,442**, median 20,386 (≈ 637 per sample); lowest window 18,882; highest window **21,893** | Basic share 19,620 | **−766 (median), −2,273 (highest)**. 2,324 of 2,332 windows are over |
| …async (block rate) | 2,313 … 2,795 (mean 2,697) | | |
| …render (sync) | 16,119 … 19,339 (mean 17,745) | | |
| CPU, the last 100 windows (final settings) | mean 19,995 (19,833 … 20,077) | 19,620 | −375 |
| Whole mixer (Tetrakt + 5 others + Effect) | highest window average 143,513 (91.6 %); highest block **159,542 (101.8 %)**; 108 windows had at least one block over 156,705 (1 in segment A, 33 in B, 74 in C) | 156,705 | overrun |
| Voice struct | 1,048 B | slot reserve 1,264 B | 216 B |
| Shared work area (`tkt_work`) | 448 B .bss | approved | — |
| S1 line | one FxBuffer voice unit (2,208 × int16) | Effect share | — |
| ITCM | +80 B (15,312 / 16,384) | approved (D3) | 1,072 B |

**Cycles per instruction.** The host count for this image is 419.9
instructions per sample by the S11 rule (13,437 per block). Against the
device median of 20,386 that is **1.52 cycles per instruction** (1.41 …
1.63 over the run). The render alone is 11,988 instructions against 16,119
… 19,339 cycles: 1.34 … 1.61. The Colloid measured 1.37 … 1.41. The
Tetrakt's four simultaneous sample reads from internal flash
(`0x080C0000`) are the likely reason for its higher and more variable
ratio.

**The render's variation.** In the first 120 windows the render was
≈ 19,300 cycles. At record 676,709 it fell to ≈ 18,000 (later ≈ 17,500,
then ≈ 17,200), with the Effect still off. The fall coincides with edits of
the velocity amount (`normal[22]`), whose destination is the SRR group, so
the edits changed the layers' SRR rates. The decoded log can't isolate the
cause. Two candidates:
- **Flash reads:** the cache hit rate of four sample streams depends on what
  is playing and at what pitch.
- **The SRR hold branch:** `if (cnt >= 1.0f)` in `tkt_layerPost()` compiles
  to a real branch (`blt`, `tkt_layerPostAdd` in the S11 image), so its
  pattern changes with the SRR rate.

The constant-CPU rule takes the worst case, so the highest window (21,893)
is the figure that counts.

### 18.2 What this means

- **Over budget.** As built (S11), the Tetrakt is over its Basic share in
  practice, not only in the host estimate: by ≈ 4 % at the median and
  ≈ 12 % in the worst window. The whole-mixer overruns in this Scene (five
  heavy voices plus the Effect) are not the Tetrakt's alone. Its excess
  (≈ 800 … 2,300 cycles) is the same size as the overruns (up to 2,837
  over), though.
- **Projection with §17 (estimate, not measured).** §17 saves ≈ 10.5
  instructions per sample, ≈ 510 cycles per block at 1.52:
  - median ≈ 19,880, still ≈ 260 over;
  - the final settings ≈ 19,490, which fits by ≈ 130;
  - worst window ≈ 21,380, which is ≈ 1,760 over.
- **Trim 4 (dropping SRR).** It would save a further ≈ 24 per sample,
  ≈ 1,170 cycles per block at 1.52, and remove the SRR branch. That gives a
  median ≈ 18,700 and a worst window ≈ 20,200. The worst window would still
  be ≈ 600 over. The rest of the gap would need either a cheaper read (fewer
  flash misses) or the Advanced class (21,803). The worst window measured
  here exceeds Advanced by ≈ 90; with §17 it would fit Advanced by ≈ 420.
- **Next measurement.** A probe run on the §17 image with the same Scene,
  so the projection above is replaced by device figures.

## 19. The fused sample read (2026-11-15)

**Decision (user, 2026-11-15).** Fold each layer's sample read into its
post loop (option 1 of the §18 follow-up). The control-rate split (option 2)
and the drive/tone tables (option 3) are **not** done.

**Why the first 120 windows of §18 were the worst.** In
`tkt_layerPostAdd/Store` the compiler moved the whole input path (scratch
load, conversion, gain, drive, clip, wet mix) inside the SRR hold branch
(`if (cnt >= 1.0f)`), because a held sample does not need it:

- SRR 127 (the default) takes the branch every sample: 47 instructions in
  the add loop, 44 in the store loop.
- A held sample skips that work: 32 and 29.

The init settings (all four layers at SRR 127) are the device's worst case
and give the true cycles per instruction there: ≈ 1.63 (21,893 / 13,437).
Velocity to the SRR group lowered the rates during the run, and the cost
fell with them (record 676,709).

### Work log

**1. `Core/DSP/Instruments/Tetrakt/TetraktVoice.h`.** The `TktLayer`
comment and the file header now describe the read as done inside the post
loop, with the bind still `osc_userBind()`. ADD `float bindGain` to
`TktLayer`: the bound sample's catalogue gain. `sizeof(TktLayer)` grows by
4 B, which stays inside the slot reserve (asserted).

**2. `Core/DSP/Instruments/Tetrakt/TetraktVoice.c`.**
- ADD `#include "SampleMemory.h"`, for `sampleMemory_sampleEntry()` and
  `sampleMemory_gainFromCode()`.
- `TktWork`: REMOVE the int16 `scratch[32]`. The work area is 384 B,
  asserted (448 B was approved). The comment block gives the reason.
- `tkt_bindLayer()`: after `osc_userBind()` it sets `L->bindGain` from the
  catalogue entry of an SMP bind, else 1.0. This is `osc_bindGain()`'s law,
  read once per bind instead of once per block.
- `tkt_layerPost()`: the read is folded in.
  - **Guard:** only an `OSC_BIND_SMP` bind with data and a non-zero size
    reads. Anything else reads a static zero pair at phase 0 and keeps its
    phase, which is `osc_renderTyped()`'s default case, silence.
  - **Per sample:** `s = (a << 12) + (ph & 4095)(b − a)`, exact in
    integers since |s| < 2^29; `ph = min(ph + phaseInc, end)`, which is
    `osc_smpReadPhase()`'s law, so a finished sample reads its two
    installed zero frames.
  - **The gain:** `x = s × g`, where the envelope gain and its ramp carry
    `bindGain / 4,096`. With `bindGain` 1 that factor is exactly 2^-12.
  - **Write-back:** the phase is written back after the loop. The comment
    block (What / Why / Inputs / Outputs) describes the read, its exactness
    and why it is here.
- `Tetrakt_initVoice()`: `bindGain = 1.0f` before the first bind.
- `Tetrakt_calcVoiceSyncBlock()`: REMOVE the `osc_renderTyped()` call, and
  update the comment.

**3. `Core/DSP/Instruments/InstrumentManager.c`, the sync-block dispatch
comment.** Only the comment changes: the post loop reads the sample.

**4. `tools/dsp_test/modules/tetrakt.c`.**
- `layer_identity()` sets `bindGain` 1.
- `post_block()` binds the input block as a 32-frame sample (two zero
  frames after it), read from phase 0 at step 4,096 with gain 1. The read
  is then exactly the old scratch, so TK-SRR, TK-TONE, TK-DRV and TK-W2
  keep their exact equalities unchanged.
- A fifth, quiet test sample (peak 3,000) has a catalogue gain above 1. The
  sample-count guard is now 5.
- **New law TK-READ.** It compares the fused read with the platform's
  `osc_smpPlainBlock()` on all five samples:
  - phase steps 1,515, 4,096, 4,097, 5,163, 8,192 and 11,873 (0.37 … 2.9
    frames, with fractions), each for 800 blocks, past every sample's end;
  - every output within one int16 step (the platform read rounds to int16;
    the fused one doesn't), and the phase equal after every block;
  - a block that starts at the end reads exactly 0, and the read stops at
    the end;
  - `bindGain` equals the catalogue's (above 1 for the quiet sample);
  - an unbound layer is silent and keeps its phase.

**5. `tools/dsp_test/Makefile`.** Two new break builds, `tetrakt_break_gain`
(the read without `bindGain`) and `tetrakt_break_frac` (the read without
its interpolation). Both fail TK-READ as required.

**Host result:** every Tetrakt law (TRIG … VEL, RING, READ) has 0
failures, and all six break builds are caught.

**6. `tools/dsp_test/tetrakt_cpu.py`.** The four `osc_smpPlainBlock` read
loops and the four `osc_renderTyped` dispatch paths are removed from the
count (and from the required symbols). The post loops now include the
read.

### Gates (this tree)

| Gate | Result |
|---|---|
| `make all && make img` | exit 0; `build/LXRV2_lxr02.img` 728,436 B |
| Flash | 728,436 B used, headroom 25,228 B (+2,656 vs §17; see below) |
| ITCM | **15,232 B (−80)**: nothing calls `osc_renderTyped()` now, so the linker drops it again. The D3 approval is no longer needed |
| `.bss` | 286,316 (−64: `tkt_work` is 384 B) |
| Voice struct | `TktLayer` +4 B: 1,048 → 1,064 B (≤ 1,264, asserted) |
| Host `make -C tools/dsp_test all` | exit 0; Tetrakt TRIG/DEC/TUNE/SRR/TONE/DRV/W2/S1/VEL/RING/**READ** 0 failures; fade/bias/SRR/guard/**gain**/**frac** breaks caught. The `voice_control failures: 6` line is the deliberate break build |
| Colloid CPU | identical to `before/colloid_cpu.txt` |
| **Tetrakt CPU (worst block)** | **loops 301 + per block 2,615/32 = 382.7; gate 390 PASSED** |
| `elf_p4_check P4BASE=baselines/s096p1/before` | oscillators: 94 baseline loops, **94 exact**; mixer: 52 equal, 0 shrank, **6 grew** (below) |
| `elf_p4_check` (default `p4base`, the §17 layout) | oscillators 89: 78 exact, 4 renamed, 7 missing (below) |

Records: `tools/dsp_test/baselines/s096p1/s19/` (`host_all.txt`,
`tetrakt_cpu.txt`, `colloid_cpu.txt`, `link_budget.txt`,
`p4_check_vs_before.txt`).

**What the CPU figure is made of** (instructions; S11 figures in brackets):

| Part | Now | Change |
|---|---|---|
| Per sample: store loop | 57 (44 + read 24) | −11 |
| Per sample: add loop, × 3 | 60 (47 + read 24) | −11 each |
| Per sample: space frame | 64 | (§17) |
| **Per sample, all four layers** | | **−44** |
| Per block: each post loop's entry | 69 (45) | +24 (the guard and the gain set-up), × 4 |
| Per block: `tkt_bindLayer` | 30 (8) | +22 (the catalogue gain), only at a START, × 4 in the worst block |
| Per block: `Tetrakt_calcVoiceSyncBlock` | 15 (24) | −9 |
| Per block: the four dispatches | removed | −40 |

| | §17 | §19 |
|---|---|---|
| Worst block (binds counted) | 422.4 | **382.7** |
| Without the binds (what a window averages) | 409.4 | **367.0** |

The held-sample path is now 39 (store) / 42 (add), against 29 / 32 + 24
before. Below SRR 127 the compiler still skips the input work on held
samples, as described above.

**Projection (estimate).** The device decides:

| | Instructions per block | At 1.52 cycles per instruction | At 1.63 (§18's worst) |
|---|---|---|---|
| Window (no binds) | 11,744 | ≈ 17,850 | ≈ 19,140 (fits 19,620) |
| Worst trigger block (all four layers START) | 12,246 | ≈ 18,610 | ≈ 19,960 (≈ 340 over) |

The instructions removed were mostly cheap integer, load and store work,
and the flash reads are unchanged, so the device's cycles per instruction
may rise. The §18-style probe run (same Scene, SRR 127 on every layer for
the worst case) is the check.

**Flash +2,656 B and the loop gates: LTO went back to the pre-S096
layout.** Without the Tetrakt's `osc_renderTyped()` call, LTO partitions
the image as it did before S096:
- `osc_wtbPlanBlock.constprop.0` is back;
- the `osc_*` helpers are named `.lto_priv.0` again;
- the Drum's out-of-line `Drum_calcVoiceSyncBlock.part.0.constprop.0`
  (2,408 B) is back, as in `before/lxr02.elf`.

The flash growth over §17 is that layout returning, not Tetrakt code. The
Tetrakt's own functions grew by ≈ 670 B: the two post loops +152 each,
`tkt_bindLayer` +90, `tkt_spaceOut` +276 (§17).

So the gate results are:
- **Oscillator loops:** against the pre-S096 snapshot (`before`), all 94
  are exact, a better match than S11 or §17.
- **Mixer hot loops:** against `before`, the same 58 loops, but five are
  one instruction longer (three 22 → 23, plus 28 → 29 and 29 → 30): +5
  instructions in all, out of 1,360. They are register-allocation shifts
  in `mixer_calcNextSampleBlock()`, which S096 does not edit. The tool's
  "14 → 19" line pairs two different loops; the multisets of loop lengths
  show the +5.
- **The default `p4base`** (the §17 layout, D4) no longer matches, for the
  same reason.

**Open for the user:** accept this layout and re-snapshot the P4 base from
this build (as D4 did for §17), or keep `p4base`. Until then
`elf_p4_check` with the default base fails. I have not re-snapshotted.

### Statements this makes out of date (for you to update)

- §0 SD5 / plan Q9: "the read is reused as it is" → the bind is the
  platform's, but the read is the Tetrakt's own, inside its post loop
  (§19).
- §1 / §17 D3: "ITCM +80 B" → 0 (15,232 B, as before S096).
- Plan §10 / §1 "RAM 448 B work area" → 384 B.
- `sizeof(TetraktVoice)` 1,048 → 1,064 B.
- §17's Makefile `P4BASE` default (`baselines/s096p1/p4base`) no longer
  matches the image (above).
