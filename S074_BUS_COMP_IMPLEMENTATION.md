# S074 — Master bus compressor (`cmp`) implementation schedule

**Status:** schedule only. No source file has been changed.

**Specification:** `S074_BUS_COMP.md` (draft 2, every decision settled).
This document lists every code change that specification needs.

**Base:** the working tree of 2026-09-29, which is HEAD `1dbdd70` plus
uncommitted documentation only (`S074_CRUMPBIT_EFFECT.md`,
`S074_BUS_COMP.md`). Every line number below is a line number in **that**
tree, before any edit. Apply the changes in each file **from the highest line
number down** so the numbers stay valid (§1.3 gives the order).

**Stages:** each change is tagged with its stage (§1.2). Every stage builds
on its own.

---

## 1. Overview

### 1.1 Change list

| ID | File | Line(s) | Action | Stage | Purpose |
|---|---|---|---|---|---|
| N1 | `Core/DSPAudio/BusCompressor.h` | — | ADD file | 2 | Public contract |
| N2 | `Core/DSPAudio/BusCompressor.c` | — | ADD file | 2 | Detector, cell, sidechain, per-sample stage |
| K1 | `Makefile` | after L135 | ADD | 2 | `DSP_SRCS` (`-Ofast`) |
| X1 | `Core/DSPAudio/mixer.c` | after L62 | ADD | 2 | Include |
| X2 | `Core/DSPAudio/mixer.c` | after L1038 | ADD | 2 | Call the stage (St1 = `output2`, St2 = `output`) |
| V1 | `Core/MIDI/MidiVoiceControl.c` | after L65 | ADD | 2 | Include |
| V2 | `Core/MIDI/MidiVoiceControl.c` | after L169 | ADD | 2 | Sidechain intake |
| S1 | `Core/Bank/Scene/SceneData.h` | after L119 | ADD | 1 | Field enum and constants |
| S2 | `Core/Bank/Scene/SceneData.h` | after L189 | ADD | 1 | `bus_comp[4]` |
| S3 | `Core/Bank/Scene/SceneData.h` | after L452 | ADD | 1 | Accessor declarations |
| S4 | `Core/Bank/Scene/SceneData.c` | after L850 | ADD | 1 | Tables, defaults, clamp, setter, getter |
| S5 | `Core/Bank/Scene/SceneData.c` | after L871 | ADD | 1 | Defaults in `scene_initAll()` |
| A1 | `Core/Bank/Scene/Autosave.h` | L180–181 | MODIFY | 1 | Layout comment |
| A2 | `Core/Bank/Scene/Autosave.h` | L190 | MODIFY | 1 | Live bytes 41 → 45 |
| A3 | `Core/Bank/Scene/Autosave.h` | L242–244 | MODIFY | 1 | Parameters 41–44 |
| A4 | `Core/Bank/Scene/Autosave.c` | after L86 | ADD | 1 | Group asserts |
| A5 | `Core/Bank/Scene/Autosave.c` | L913–916 | MODIFY | 1 | Getter branch |
| A6 | `Core/Bank/Scene/Autosave.c` | L1324, L1330 | MODIFY | 1 | Reader comment |
| A7 | `Core/Bank/Scene/Autosave.c` | L1394–1397 | MODIFY | 1 | Reader branch |
| F1 | `Core/Hardware/SD/storageTypes.h` | after L387 | ADD | 1 | Key accessor declaration |
| F2 | `Core/Hardware/SD/storageTypes.c` | after L513 | ADD | 1 | Key table and helpers |
| F3 | `Core/Hardware/SD/storageTypes.c` | after L524 | ADD | 1 | Local `field` |
| F4 | `Core/Hardware/SD/storageTypes.c` | L675–676 | MODIFY | 1 | Parse branch |
| F5 | `Core/Hardware/SD/filesystem.c` | after L16320 | ADD | 1 | Stage defaults |
| F6 | `Core/Hardware/SD/filesystem.c` | after L16948 | ADD | 1 | Writer lines 11–14 |
| F7 | `Core/Hardware/SD/filesystem.c` | after L28440 | ADD | 1 | Empty-Scene defaults |
| P1 | `Core/Bank/Scene/Preset/presetManager.h` | after L530 | ADD | 1 | Declarations |
| P2 | `Core/Bank/Scene/Preset/presetManager.c` | after L1295 | ADD | 1 | Mirror refresh on Scene apply |
| P3 | `Core/Bank/Scene/Preset/presetManager.c` | after L3097 | ADD | 1 | Setter, mirror sync, order assert |
| M1 | `Core/Bank/Scene/Preset/ParameterArray.h` | after L171, after L177 | ADD | 1 | `PAR_BUS_COMP_*` ids (58–61) and assert |
| M2 | `Core/Menu/menu.h` | after L26; L135; L145; after L175; L203 | ADD/MODIFY | 3 | Constant; text, category, long, short ids |
| M3 | `Core/Menu/MenuText.h` | after L139; L148; after L172 | ADD/MODIFY | 3 | Labels |
| M4 | `Core/Menu/menuPages.h` | L44 | REPLACE | 3 | Sub-page 2 row |
| E1 | `Core/Menu/menu.c` | after L1031 | ADD | 3 | Dtype entries |
| E2 | `Core/Menu/menu.c` | after L1181 | ADD | 3 | `valueNames` rows |
| E3 | `Core/Menu/menu.c` | after L1811 | ADD | 3 | Prototypes |
| E4 | `Core/Menu/menu.c` | after L3717 | ADD | 3 | Commit branch |
| E5 | `Core/Menu/menu.c` | after L4323 | ADD | 3 | Compact value text |
| E6 | `Core/Menu/menu.c` | after L4423 | ADD | 3 | Clamp |
| E7 | `Core/Menu/menu.c` | L8133–8149 | REPLACE | 3 | `^` / `+` cues |
| E8 | `Core/Menu/menu.c` | after L10000 | ADD | 3 | Full-view value text |
| E9 | `Core/Menu/menu.c` | after L11240 | ADD | 3 | Helper definitions |
| E10 | `Core/Menu/menu.c` | after L13202 | ADD | 3 | Bulk-apply guard |
| E11 | `Core/Menu/menu.c` | after L13654 | ADD | 3 | Diagnostic BC18 check |
| E12 | `Core/Menu/menu.c` | after L13706 | ADD | 3 | Mirror defaults at init |
| E13 | `Core/Menu/menu.c` | after L13718 | ADD | 3 | Call the check |

### 1.2 Stages

- **Stage 1 — data, persistence and Preset**
  - Changes: S1–S5, A1–A7, F1–F7, P1–P3, M1.
  - Scene settings carry `bus_comp[4]`, and AutoSave cells 41–44 go live.
    `sceneset.scg` gains four lines, and Preset keeps the mirrors.
  - Nothing audible or visible changes.
- **Stage 2 — DSP**
  - Changes: N1, N2, K1, X1, X2, V1, V2.
  - The compressor runs on any Scene whose `bus_comp_mode` is nonzero. You
    can listen before the menu exists by setting `bus_comp_mode=1` in a
    Scene's `sceneset.scg` and loading it.
- **Stage 3 — menu**
  - Changes: M2–M4, E1–E13.
  - Adds the settings page, its cues, and the diagnostic check.

### 1.3 Order within each file (highest line first)

| File | Order |
|---|---|
| `menu.c` | E13 (13718), E12 (13706), E11 (13654), E10 (13202), E9 (11240), E8 (10000), E7 (8133–8149), E6 (4423), E5 (4323), E4 (3717), E3 (1811), E2 (1181), E1 (1031) |
| `filesystem.c` | F7 (28440), F6 (16948), F5 (16320) |
| `storageTypes.c` | F4 (675–676), F3 (524), F2 (513) |
| `Autosave.c` | A7 (1394–1397), A6 (1330, 1324), A5 (913–916), A4 (86) |
| `Autosave.h` | A3 (242–244), A2 (190), A1 (180–181) |
| `presetManager.c` | P3 (3097), P2 (1295) |
| `SceneData.c` | S5 (871), S4 (850) |
| `SceneData.h` | S3 (452), S2 (189), S1 (119) |
| `menu.h` | M2e (203), M2d (175), M2c (145), M2b (135), M2a (26) |
| `MenuText.h` | M3c (172), M3b (148), M3a (139) |
| `mixer.c` | X2 (1038), X1 (62) |
| `MidiVoiceControl.c` | V2 (169), V1 (65) |
| `ParameterArray.h` | M1b (177), M1a (171) |
| Single-site files | K1, P1, F1, M4 |

### 1.4 Decisions carried from the specification

- **Stage position:** St1 = `output2` (DAC1, MAIN and headphones); St2 =
  `output` (DAC2, OUT2). No jack fallback.
- **Cost:** no work while `cmp` is `off`. Every on/off or target change
  fades over one block, and `St1` ↔ `St2` fades out, then in.
- **Detector:** feed-forward on the pair's input. `½(L²+R²)`, smoothed in the
  power domain over 5 ms, read with a one-block lag in a single pass.
- **Static curve:** knee 12 dB; `T = −3 − 27a` dBFS; `R = 1 + 3a + 4a³`.
- **Makeup:** `M = −GR(−12 dBFS)`.
- **Cell:**
  - attack `5 + 10t` ms;
  - fast release `60 + 540t²` ms;
  - memory stage charging in 300 ms towards `0.5·G_f`, releasing in
    `500 + 4,500t²` ms;
  - applied reduction `min(G_f, G_m)`.
- **Sidechain:** `duck = −(9 + 9a)·(v/127)³` dB below the static target,
  lighting `G_f`. Track 7 counts as voice 6 (BC11, working assumption).
- **Saturator:** cubic, `d = 1 + 0.4a²`, ceiling `1/d`, unity small-signal
  gain.
- **Storage and menu:**
  - storage is `bus_comp[4]` in `scene_bus_comp_field_t` order, the same
    order as AutoSave 41–44, the file keys and `PAR_BUS_COMP_MODE + field`;
  - defaults are `off`, 48, 48, `off`;
  - edits reach every Scene in the VOICE edit mask;
  - there is no modulation;
  - the four menu ids never commit through `menu_parseGlobalParam()`, where
    they only refresh the mirrors;
  - the BC18 check runs under `DEV_MODE_DIAGNOSTIC`.

### 1.5 Resources

| Resource | Change |
|---|---|
| SRAM1 `scenes[16]` | `scene_settings_t` 41 → 45 B; `scene_t` 1,622 → 1,626 B (measured with the toolchain): **+64 B** (approved) |
| DTCM `.dtcmz` | `bus_comp_state_t` **24 B** (approved up to 32 B; the build asserts ≤ 32). `_edtcmz` 0x20001160 → 0x20001178, and the 32-byte aligned arena base moves to 0x20001180: FX arena 126,624 → **126,592 B**, 3,712 B above the 122,880 B minimum |
| `parameter_values[]` | ids 58–61, inside the fixed 384: **0 B** |
| AutoSave record | unchanged (34,768 B); cells 41–44 were reserved and are zero in every existing record |
| Stage cache | `filesystem.c:1092` assert sum 2,005 → 2,009 of 2,048 B |
| Flash | about +3 KB (module, menu, storage; `log2f`/`exp2f` from the already linked libm, sharing `expf`'s `__exp2f_data` table); headroom 254,104 B |
| CPU | 0 while `off`; about 0.8–1 % of 216 MHz while on (spec §5) |

---

## 2. New files

### N1 — `Core/DSPAudio/BusCompressor.h` — ADD file (Stage 2)

```c
/*
 * Core/DSPAudio/BusCompressor.h
 *
 *  Created on: 29.09.2026
 * ------------------------------------------------------------------------------------------------------------------------
 *  Copyright 2026 Brendan Clarke
 *  brendanpaulclarke@gmail.com
 *  https://www.brendanclarke.com
 * ------------------------------------------------------------------------------------------------------------------------
 *  This file is part of the LXR02 Open-Source software.
 * ------------------------------------------------------------------------------------------------------------------------
 *  Redistribution and use of the LXR02 Open-Source, hardware driver code, or any derivative works are permitted
 *  provided that the following conditions are met:
 *
 *       - The code may not be sold, nor may it be used in a commercial product or activity.
 *
 *       - Redistributions that are modified from the original source must include the complete
 *         source code, including the source code for all components used by a binary built
 *         from the modified sources. However, as a special exception, the source code distributed
 *         need not include anything that is normally distributed (in either source or binary form)
 *         with the major components (compiler, kernel, and so on) of the operating system on which
 *         the executable runs, unless that component itself accompanies the executable.
 *
 *       - Redistributions must reproduce the above copyright notice, this list of conditions and the
 *         following disclaimer in the documentation and/or other materials provided with the distribution.
 * ------------------------------------------------------------------------------------------------------------------------
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 *   INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *   DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 *   SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 *   WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 *   USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * ------------------------------------------------------------------------------------------------------------------------
 */

#ifndef BUS_COMPRESSOR_H_
#define BUS_COMPRESSOR_H_

#include <stdint.h>
#include "sample_mix.h"

/*
 * Master bus compressor (S074): one Scene-owned, soft-knee, RMS, optical
 * (LA-2A-like) compressor on the St1 or St2 output pair, with a trigger
 * sidechain.
 *
 * What:       a master-bus stage, not an Effect slot. The active Scene's
 *             four settings (SceneData bus_comp[]: cmp target, cam amount,
 *             ctm time, csc sidechain voice) are read every block; nothing
 *             is pushed into this module.
 * Why:        levelling on one output pair, at a CPU cost that counts
 *             against the worst-case Scene only while it is on.
 * Ownership:  BusCompressor.c owns its 24 B DTCM state; SceneData owns the
 *             settings; the mixer and the trigger funnel are the only
 *             callers, both in the foreground.
 * Affiliates: mixer_calcNextSampleBlock(), voiceControl_triggerNow(),
 *             S074_BUS_COMP.md, EFFECTS_MIXER_DSP_REFERENCE.md.
 */

/*
 * Process one 32-frame block of the selected pair in place.
 *
 * Inputs:     st1 = the DAC1 interleaved buffer (the mixer's `output2`:
 *             MAIN jacks and headphones); st2 = the DAC2 buffer (`output`:
 *             OUT2). Both hold signed 24-bit scale in int32 (1.0 =
 *             8,388,352) after every voice and the FX return are summed.
 *             scene_index = the active Scene.
 * Outputs:    the selected pair is compressed, made up and soft-limited in
 *             place (ceiling 1/d <= full scale); the other pair is not
 *             touched. With cmp off and no fade pending the call returns
 *             after one settings read and costs nothing further.
 * Transitions: off->on, on->off and St1<->St2 cross-fade over one block
 *             each (St1<->St2 fades the old pair out, then the new pair in).
 * Caller:     mixer_calcNextSampleBlock(), once per block.
 */
void busComp_processBlock(sample_mx_t *st1, sample_mx_t *st2,
                          uint8_t scene_index);

/*
 * Sidechain intake: one trigger from the shared trigger funnel.
 *
 * Inputs:     track = zero-based visible track 0..6; track 7 (index 6)
 *             counts as voice 6 (BC11 working assumption); velocity 0..127
 *             (the step velocity for sequencer triggers).
 * Outputs:    when track matches the active Scene's csc and velocity > 0,
 *             records the weight (velocity/127)^3; the largest weight since
 *             the last block wins. busComp_processBlock() scales it by the
 *             cam depth, applies it once and clears it.
 * Caller:     voiceControl_triggerNow(), drained by main.c just before each
 *             mixer block, so the duck lands in the voice's own block.
 */
void busComp_sidechainTrigger(uint8_t track, uint8_t velocity);

#endif /* BUS_COMPRESSOR_H_ */
```

---

### N2 — `Core/DSPAudio/BusCompressor.c` — ADD file (Stage 2)

```c
/*
 * Core/DSPAudio/BusCompressor.c
 *
 *  Created on: 29.09.2026
 * ------------------------------------------------------------------------------------------------------------------------
 *  Copyright 2026 Brendan Clarke
 *  brendanpaulclarke@gmail.com
 *  https://www.brendanclarke.com
 * ------------------------------------------------------------------------------------------------------------------------
 *  This file is part of the LXR02 Open-Source software.
 * ------------------------------------------------------------------------------------------------------------------------
 *  Redistribution and use of the LXR02 Open-Source, hardware driver code, or any derivative works are permitted
 *  provided that the following conditions are met:
 *
 *       - The code may not be sold, nor may it be used in a commercial product or activity.
 *
 *       - Redistributions that are modified from the original source must include the complete
 *         source code, including the source code for all components used by a binary built
 *         from the modified sources. However, as a special exception, the source code distributed
 *         need not include anything that is normally distributed (in either source or binary form)
 *         with the major components (compiler, kernel, and so on) of the operating system on which
 *         the executable runs, unless that component itself accompanies the executable.
 *
 *       - Redistributions must reproduce the above copyright notice, this list of conditions and the
 *         following disclaimer in the documentation and/or other materials provided with the distribution.
 * ------------------------------------------------------------------------------------------------------------------------
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 *   INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *   DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 *   SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 *   WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 *   USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * ------------------------------------------------------------------------------------------------------------------------
 */

/*
 * BusCompressor.c — Scene-owned master bus compressor (S074).
 * See BusCompressor.h for the contract and S074_BUS_COMP.md §4 for the model.
 */

#include "BusCompressor.h"
#include "config.h"
#include "SceneData.h"
#include <math.h>

/*
 * Model constants (S074_BUS_COMP.md §4; tuning starting points).
 *
 * What:       block period, bus scale, dB conversions, detector floor,
 *             knee width, makeup reference, the fixed RMS and memory-charge
 *             times, the memory share, and the saturator span.
 * Why:        every per-block formula below reads these names, so retuning
 *             is a one-line edit. The ctm/cam curves themselves are written
 *             inline in busComp_processBlock() beside their spec formulas.
 * Inputs:     config.h OUTPUT_DMA_SIZE (32) and REAL_FS (44,108.07 Hz).
 * Affiliates: busComp_processBlock(), busComp_coef().
 */
#define BUS_COMP_BLOCK_MS          (1000.0f * (float)OUTPUT_DMA_SIZE / REAL_FS)
#define BUS_COMP_FULL_SCALE        8388352.0f   /* 1.0 = 0 dBFS = int16 FS << 8 */
#define BUS_COMP_INV_FULL_SCALE    (1.0f / BUS_COMP_FULL_SCALE)
#define BUS_COMP_DB_PER_LOG2_POWER 3.01029996f  /* 10 * log10(2) */
#define BUS_COMP_LOG2_PER_DB_GAIN  0.16609640f  /* 1 / (20 * log10(2)) */
#define BUS_COMP_POWER_FLOOR       1.0e-12f     /* -120 dBFS; log2f never sees 0 */
#define BUS_COMP_KNEE_DB           12.0f
#define BUS_COMP_REF_LEVEL_DB      (-12.0f)     /* makeup reference, RMS dBFS */
#define BUS_COMP_REF_POWER         0.06309573f  /* 10^(-12/10), fade-in seed */
#define BUS_COMP_RMS_MS            5.0f
#define BUS_COMP_CHARGE_MS         300.0f
#define BUS_COMP_MEMORY_SHARE      0.5f
#define BUS_COMP_SAT_SPAN          1.5f         /* u = d*y/1.5; ceiling 1/d */

/*
 * Compressor runtime state (24 B DTCM; approved up to 32 B, S074).
 *
 * What:       power = smoothed mean square of the processed pair's input,
 *             full-scale units; gr_fast_db / gr_memory_db = the optical
 *             cell's fast and memory stages, dB <= 0; gain_prev = linear
 *             gain reached at the end of the last block (the ramp origin);
 *             sc_pending = largest sidechain weight (v/127)^3 since the last
 *             block; active = pair processed in the last block
 *             (SCENE_BUS_COMP_MODE_*; OFF = no work).
 * Why DTCM:   read and written every block, beside the mixer's own state.
 * Lifetime:   static; zeroed at startup (OFF, cell at rest). Every fade-in
 *             reseeds it, so no stale state survives an off period.
 * Owner:      this file. Writers: busComp_processBlock() and
 *             busComp_sidechainTrigger(), both foreground (no ISR access).
 */
typedef struct {
    float power;
    float gr_fast_db;
    float gr_memory_db;
    float gain_prev;
    float sc_pending;
    uint8_t active;
} bus_comp_state_t;

_Static_assert(sizeof(bus_comp_state_t) <= 32u,
               "S074: bus compressor state is approved for at most 32 B DTCM");

static INDTCMZ bus_comp_state_t busComp;

/*
 * One-pole coefficient for one block from a time constant in ms.
 *
 * Inputs:     tau_ms > 0.
 * Outputs:    k = T_b / (tau + T_b/2), the bilinear form of
 *             1 - exp(-T_b/tau); within 0.2 % of it for tau >= 5 ms.
 * Why:        one division instead of an exponential per block; constant
 *             arguments fold at compile time.
 * Callers:    busComp_processBlock().
 */
static inline float busComp_coef(float tau_ms)
{
    return BUS_COMP_BLOCK_MS / (tau_ms + 0.5f * BUS_COMP_BLOCK_MS);
}

/*
 * Soft-knee static gain computer (S074_BUS_COMP.md §4.2).
 *
 * Inputs:     level and threshold in dBFS; slope = 1 - 1/ratio.
 * Outputs:    gain reduction in dB (<= 0): zero below the 12 dB knee,
 *             quadratic inside it, -slope * over above it.
 * Why:        one function for the per-block target and the makeup
 *             reference, so makeup always matches the curve it undoes.
 * Callers:    busComp_processBlock().
 */
static inline float busComp_staticGainDb(float level_db, float threshold_db,
                                         float slope)
{
    const float over = level_db - threshold_db;
    float x;

    if (over <= -0.5f * BUS_COMP_KNEE_DB)
        return 0.0f;
    if (over >= 0.5f * BUS_COMP_KNEE_DB)
        return -slope * over;
    x = over + 0.5f * BUS_COMP_KNEE_DB;
    return -slope * x * x * (0.5f / BUS_COMP_KNEE_DB);
}

void busComp_sidechainTrigger(uint8_t track, uint8_t velocity)
{
    const uint8_t source = scene_getBusCompSetting(scene_getActiveIndex(),
                                                   SCENE_BUS_COMP_SIDECHAIN);
    /* BC11 working assumption: track 7 (index 6) counts as voice 6. */
    const uint8_t voice = (track >= 5u) ? 6u : (uint8_t)(track + 1u);
    float v;
    float weight;

    /*
     * Record one matching trigger (contract in BusCompressor.h).
     *
     * The cube puts most of the depth between velocity 64 and 127, and 127
     * is always the full depth. The cam depth (9 + 9a dB) is applied per
     * block, so a cam edit between trigger and block is honoured.
     */
    if (source == SCENE_BUS_COMP_SIDECHAIN_OFF || velocity == 0u ||
        voice != source)
        return;
    v = (velocity >= 127u) ? 1.0f : (float)velocity * (1.0f / 127.0f);
    weight = v * v * v;
    if (weight > busComp.sc_pending)
        busComp.sc_pending = weight;
}

void busComp_processBlock(sample_mx_t *st1, sample_mx_t *st2,
                          uint8_t scene_index)
{
    const uint8_t mode = scene_getBusCompSetting(scene_index,
                                                 SCENE_BUS_COMP_MODE);
    sample_mx_t *buf;
    float a;
    float t;
    float threshold;
    float slope;
    float makeup_db;
    float drive;
    float target;
    float memory_target;
    float k;
    float gain;
    float gk;
    float gk_step;
    float w;
    float w_step;
    float c_in;
    float c_out;
    float c_out3;
    float sum;
    uint8_t fade_out = 0u;
    uint8_t i;

    /*
     * Off: no work (user decision 2). A trigger taken while off must not
     * fire when the compressor is next switched on, so it is dropped here.
     */
    if (busComp.active == SCENE_BUS_COMP_MODE_OFF &&
        mode == SCENE_BUS_COMP_MODE_OFF) {
        busComp.sc_pending = 0.0f;
        return;
    }

    /*
     * cam and ctm macros (S074_BUS_COMP.md §4.2, §4.3).
     *
     * a, t in 0..1. Threshold -3..-30 dBFS, ratio 1..8 (slope 0..0.875),
     * makeup = the static reduction at the -12 dBFS RMS reference (so a
     * moderately full bus leaves at about the same level for every cam),
     * saturation drive d = 1 + 0.4a^2. No state: recomputed every block,
     * so a Scene switch or edit takes effect on the next block.
     */
    a = (float)scene_getBusCompSetting(scene_index, SCENE_BUS_COMP_AMOUNT) *
        (1.0f / 127.0f);
    t = (float)scene_getBusCompSetting(scene_index, SCENE_BUS_COMP_TIME) *
        (1.0f / 127.0f);
    threshold = -3.0f - 27.0f * a;
    slope = 1.0f - 1.0f / (1.0f + 3.0f * a + 4.0f * a * a * a);
    makeup_db = -busComp_staticGainDb(BUS_COMP_REF_LEVEL_DB, threshold,
                                      slope);
    drive = 1.0f + 0.4f * a * a;

    /*
     * Target transitions (S074_BUS_COMP.md §4.6).
     *
     * Fade-in: seed the cell as if the bus sat at the reference level
     * (G_f = -makeup, so the total gain starts at unity, and an empty
     * memory stage), then cross-fade dry -> wet over this block.
     * Fade-out: the target went off or to the other pair; process the old
     * pair this block while cross-fading wet -> dry, then go OFF. A new
     * pair fades in on the next block. Steady: fully wet.
     */
    if (busComp.active == SCENE_BUS_COMP_MODE_OFF) {
        busComp.active = mode;
        busComp.power = BUS_COMP_REF_POWER;
        busComp.gr_fast_db = -makeup_db;
        busComp.gr_memory_db = 0.0f;
        busComp.gain_prev = 1.0f;
        w = 0.0f;
        w_step = 1.0f / (float)OUTPUT_DMA_SIZE;
    } else if (mode != busComp.active) {
        fade_out = 1u;
        w = 1.0f;
        w_step = -1.0f / (float)OUTPUT_DMA_SIZE;
    } else {
        w = 1.0f;
        w_step = 0.0f;
    }
    buf = (busComp.active == SCENE_BUS_COMP_MODE_ST1) ? st1 : st2;

    /*
     * Gain computer and optical cell, once per block, dB domain.
     *
     * The level is the RMS measured through the previous block. The fast
     * stage attacks in 5..15 ms and releases in 60..600 ms. A pending
     * sidechain weight then lights the cell to (9 + 9a) * weight dB below
     * the static target: measured from the target, so repeated triggers
     * never stack. The memory stage charges in 300 ms towards half the fast
     * stage and releases in 0.5..5 s; the deeper stage applies.
     */
    target = busComp_staticGainDb(
        BUS_COMP_DB_PER_LOG2_POWER *
            log2f(busComp.power + BUS_COMP_POWER_FLOOR),
        threshold, slope);
    k = (target < busComp.gr_fast_db)
            ? busComp_coef(5.0f + 10.0f * t)
            : busComp_coef(60.0f + 540.0f * t * t);
    busComp.gr_fast_db += k * (target - busComp.gr_fast_db);
    if (busComp.sc_pending > 0.0f) {
        const float duck_db = target - (9.0f + 9.0f * a) * busComp.sc_pending;

        if (duck_db < busComp.gr_fast_db)
            busComp.gr_fast_db = duck_db;
        busComp.sc_pending = 0.0f;
    }
    memory_target = BUS_COMP_MEMORY_SHARE * busComp.gr_fast_db;
    k = (memory_target < busComp.gr_memory_db)
            ? busComp_coef(BUS_COMP_CHARGE_MS)
            : busComp_coef(500.0f + 4500.0f * t * t);
    busComp.gr_memory_db += k * (memory_target - busComp.gr_memory_db);
    gain = exp2f((fminf(busComp.gr_fast_db, busComp.gr_memory_db) +
                  makeup_db) * BUS_COMP_LOG2_PER_DB_GAIN);

    /*
     * Per-sample stage: detector accumulate, gain ramp, cubic saturator,
     * cross-fade, write back.
     *
     * Scaling: c_in folds d/1.5 and the int32->full-scale conversion into
     * the ramped gain; c_out folds 1.5/d and the scale back. Both use this
     * block's d, so the small-signal gain is exactly the ramped g and stays
     * continuous across cam changes. u is clamped to +-1, so the wet sample
     * is bounded by full scale / d (the soft ceiling). The dry sample is
     * float(int32), exact for all 24-bit values. The accumulate uses the
     * input, so the detector is feed-forward. Cost: about 26 cycles/frame.
     */
    c_in = drive * (1.0f / BUS_COMP_SAT_SPAN) * BUS_COMP_INV_FULL_SCALE;
    c_out = (BUS_COMP_SAT_SPAN * BUS_COMP_FULL_SCALE) / drive;
    c_out3 = c_out * (-1.0f / 3.0f);
    gk = busComp.gain_prev * c_in;
    gk_step = (gain - busComp.gain_prev) * c_in *
              (1.0f / (float)OUTPUT_DMA_SIZE);
    sum = 0.0f;
    for (i = 0u; i < OUTPUT_DMA_SIZE; i++) {
        const float xl = (float)buf[2u * i];
        const float xr = (float)buf[2u * i + 1u];
        float ul;
        float ur;

        gk += gk_step;
        w += w_step;
        sum += xl * xl + xr * xr;
        ul = fminf(fmaxf(xl * gk, -1.0f), 1.0f);
        ur = fminf(fmaxf(xr * gk, -1.0f), 1.0f);
        ul *= c_out + c_out3 * ul * ul;
        ur *= c_out + c_out3 * ur * ur;
        buf[2u * i] = (sample_mx_t)(xl + w * (ul - xl));
        buf[2u * i + 1u] = (sample_mx_t)(xr + w * (ur - xr));
    }
    busComp.gain_prev = gain;

    /* Detector: this block's mean of (L^2 + R^2)/2, smoothed over 5 ms. */
    busComp.power += busComp_coef(BUS_COMP_RMS_MS) *
        (sum * (0.5f / (float)OUTPUT_DMA_SIZE) *
             (BUS_COMP_INV_FULL_SCALE * BUS_COMP_INV_FULL_SCALE) -
         busComp.power);
    if (fade_out)
        busComp.active = SCENE_BUS_COMP_MODE_OFF;
}
```

**Notes on N2:**

- **The fade ramp:** `w` and `gk` step **before** use. The first sample of a
  block gets one step and the last gets the exact target: `w` = 1 at the end
  of a fade-in, and 0 at the end of a fade-out, which leaves the pair fully
  dry.
- **`fminf`/`fmaxf`** compile to single-cycle `VMINNM`/`VMAXNM` on the
  Cortex-M7's FPv5 under `-Ofast`.
- **The float→int convert** is `VCVT` (round to zero, saturating). The
  cross-faded value always lies between an int32 dry sample and a wet sample
  bounded by full scale, so it cannot overflow.
- **Subnormals:** the Cortex-M7 FPU handles them in hardware. The ε on the
  power keeps `log2f` finite on silence, so no extra snaps are needed.

---

## 3. DSP wiring (Stage 2)

### K1 — `Makefile` after L135 — ADD

Current:

```make
DSP_SRCS = \
  Core/DSPAudio/1PoleLp.c \
  Core/DSPAudio/BufferTools.c \
```

Add after L135:

```make
  Core/DSPAudio/BusCompressor.c \
```

A Makefile has no comment block for a list entry. The contract is that
`DSP_SRCS` builds with `CFLAGS_DSP` (`-Ofast`, L160), which the stage needs
for `VMINNM`/`VMAXNM` and fused multiply-adds. `-ICore/DSPAudio` (L32) is
already on the include path.

---

### X2 — `Core/DSPAudio/mixer.c` after L1038 — ADD

Context (L1036–1040):

```c
		mixer_fx_return_last_gain[0] = 0.0f;
		mixer_fx_return_last_gain[1] = 0.0f;
	}

}
```

Insert after L1038 (the `}` that closes the FX `else` branch):

```c

	/*
	 * Master bus compressor (S074), the last stage before the DMA pack.
	 *
	 * What:       compresses the Scene-selected output pair in place, after
	 *             every voice and the FX return are summed into it.
	 * Mapping:    St1 = DAC1 = `output2` (MAIN and headphones); St2 = DAC2 =
	 *             `output` (OUT2). mixer_moveDataToOutput() and the voice
	 *             adders write MIXER_ROUTING_DAC1_* to outL2/outR2.
	 * Why here:   it must see the whole pair, including the FX return. No
	 *             jack fallback (user decision): the pair is the one the
	 *             Scene names, not the effective route.
	 * Cost:       none while cmp is off; about 1 % while on, counted against
	 *             the worst-case Scene.
	 * Inputs:     both buffers; fx_scene (the active Scene, snapshotted above).
	 * Outputs:    the selected buffer only.
	 * Affiliates: BusCompressor.h, voiceControl_triggerNow() (sidechain),
	 *             S074_BUS_COMP.md §2.
	 */
	busComp_processBlock(&output2[pos], &output[pos], fx_scene);
```

---

### X1 — `Core/DSPAudio/mixer.c` after L62 — ADD

Context: `#include "presetManager.h"` (L62). Add:

```c
/* S074 master bus compressor: the final stage of mixer_calcNextSampleBlock(). */
#include "BusCompressor.h"
```

---

### V2 — `Core/MIDI/MidiVoiceControl.c` after L169 — ADD

Context (L168–171):

```c
	seq_restoreAutomatedParameters(voice);
	instrumentManager_triggerTrack(voice, note, vel);

	led_pulseLed((uint8_t)(LED_VOICE1 + voice));
```

Insert after L169:

```c
	/*
	 * Bus compressor sidechain intake (S074).
	 *
	 * What:       offers this trigger to the compressor, which keeps it only
	 *             when the track matches the active Scene's csc and the
	 *             velocity is nonzero (track 7 counts as voice 6).
	 * Why here:   every trigger source (sequencer, rolls, MIDI, front-panel
	 *             previews) passes this funnel, and main.c drains it just
	 *             before each mixer block, so the duck lands in the voice's
	 *             own block.
	 * Inputs:     the visible track and the trigger velocity (the step's
	 *             velocity for sequencer triggers).
	 * Outputs:    a pending sidechain weight only; no DSP work here.
	 * Affiliates: busComp_sidechainTrigger(), busComp_processBlock().
	 */
	busComp_sidechainTrigger(voice, vel);
```

---

### V1 — `Core/MIDI/MidiVoiceControl.c` after L65 — ADD

Context: `#include "sequencer.h"` (L65). Add:

```c
/* S074: bus compressor sidechain intake in voiceControl_triggerNow(). */
#include "BusCompressor.h"
```

---

## 4. Scene data (Stage 1)

### S3 — `Core/Bank/Scene/SceneData.h` after L452 — ADD

Context: `uint8_t scene_getEffectMorphAmount(uint8_t scene_index);` (L452),
then a blank line and `#endif`. Insert after L452:

```c
/*
 * S074 master bus compressor accessors (cmp, cam, ctm, csc).
 *
 * scene_busCompDefaults(): writes the defaults (off, 48, 48, off) into a
 *   settings image. Used by scene_initAll(), the Scene Load stage and the
 *   boot reader's empty Scene, so every default path agrees.
 * scene_busCompClamp(): the field's domain clamp (mode 0..2, amount and
 *   time 0..127, sidechain 0..6); an invalid field returns 0.
 * scene_setBusCompSetting(): clamps, then commits through the change-aware
 *   store, which marks AutoSave Scene parameter 41 + field and invalidates
 *   the Scene's card-clean bit. Equal values and invalid Scenes or fields
 *   are no-ops. No runtime push: BusCompressor.c reads every block.
 * scene_getBusCompSetting(): the retained value, or 0 (off) for an invalid
 *   Scene or field.
 * Affiliates: Preset's setter and mirror, Autosave's getter and reader,
 * storageTypes.c's parser, BusCompressor.c.
 */
void scene_busCompDefaults(scene_settings_t *settings);
uint8_t scene_busCompClamp(uint8_t field, uint8_t value);
void scene_setBusCompSetting(uint8_t scene_index, uint8_t field,
                             uint8_t value);
uint8_t scene_getBusCompSetting(uint8_t scene_index, uint8_t field);
```

---

### S2 — `Core/Bank/Scene/SceneData.h` after L189 — ADD

Context: `uint8_t effect_morph_amount;` (L189), followed by the "Autosave
extension rule" comment. Insert after L189:

```c
    /*
     * S074 master bus compressor: cmp, cam, ctm, csc, indexed by
     * scene_bus_comp_field_t.
     *
     * These are Scene settings, not Kit or Effect data: they travel with
     * Scene and Bank save/load, sceneset.scg (bus_comp_* keys) and AutoSave
     * (Scene parameters 41..44). Written only through
     * scene_setBusCompSetting() (or a validated whole-Scene commit); read
     * every block by BusCompressor.c. +4 B per Scene, +64 B SRAM1 in
     * scenes[16] (approved 2026-09-29).
     */
    uint8_t bus_comp[SCENE_BUS_COMP_FIELD_COUNT];
```

---

### S1 — `Core/Bank/Scene/SceneData.h` after L119 — ADD

Context: `} kit_t;` (L118), a blank line (L119), then
`typedef struct {` (L120), which opens `scene_settings_t`. Insert after L119:

```c
/*
 * S074 master bus compressor settings: one byte each, in wire order.
 *
 * What:       indexes scene_settings_t::bus_comp[] and, in the same order,
 *             AutoSave Scene parameters 41..44, the sceneset.scg keys
 *             bus_comp_mode/amount/time/sidechain and the PAR_BUS_COMP_*
 *             page mirrors (PAR_BUS_COMP_MODE + field).
 * Domains:    MODE 0 off, 1 St1 (DAC1: MAIN), 2 St2 (DAC2: OUT2); AMOUNT
 *             and TIME 0..127; SIDECHAIN 0 off or voice 1..6 (track 7
 *             counts as 6).
 * Why an array: one clamp table, one default table and one setter keep the
 *             four fields on the same owner path and wire order.
 * Affiliates: BusCompressor.c (reader), Preset (setter, mirror), Autosave,
 *             storageTypes.c, menu.c.
 */
typedef enum {
    SCENE_BUS_COMP_MODE = 0,
    SCENE_BUS_COMP_AMOUNT,
    SCENE_BUS_COMP_TIME,
    SCENE_BUS_COMP_SIDECHAIN,
    SCENE_BUS_COMP_FIELD_COUNT
} scene_bus_comp_field_t;

#define SCENE_BUS_COMP_MODE_OFF        0u
#define SCENE_BUS_COMP_MODE_ST1        1u
#define SCENE_BUS_COMP_MODE_ST2        2u
#define SCENE_BUS_COMP_SIDECHAIN_OFF   0u
#define SCENE_BUS_COMP_DEFAULT_AMOUNT  48u
#define SCENE_BUS_COMP_DEFAULT_TIME    48u

```

---

### S5 — `Core/Bank/Scene/SceneData.c` after L871 — ADD

Context (L870–871):

```c
    for (scene_index = 0u; scene_index < SCENE_COUNT; scene_index++) {
        scenes[scene_index].settings.voice_decimation_all = 127u;
```

Insert after L871:

```c
        /* S074: bus compressor defaults (off, 48, 48, off). */
        scene_busCompDefaults(&scenes[scene_index].settings);
```

---

### S4 — `Core/Bank/Scene/SceneData.c` after L850 — ADD

Context: `scene_getEffectMorphAmount()` ends at L850, and `scene_initAll()`
starts at L852. Insert after L850:

```c

/*
 * S074 bus compressor domain and default tables, in scene_bus_comp_field_t
 * order.
 *
 * What:       maxima (mode 2 = St2, amount 127, time 127, sidechain 6) and
 *             defaults (off, 48, 48, off).
 * Why:        the parser, the menu clamp, AutoSave restore and the setter
 *             all clamp through scene_busCompClamp(), so no path can store
 *             an unreachable value.
 * Affiliates: scene_busCompClamp(), scene_busCompDefaults().
 */
static const uint8_t scene_busCompMax[SCENE_BUS_COMP_FIELD_COUNT] = {
    SCENE_BUS_COMP_MODE_ST2, 127u, 127u, INSTRUMENT_SLOT_COUNT
};
static const uint8_t scene_busCompDefault[SCENE_BUS_COMP_FIELD_COUNT] = {
    SCENE_BUS_COMP_MODE_OFF, SCENE_BUS_COMP_DEFAULT_AMOUNT,
    SCENE_BUS_COMP_DEFAULT_TIME, SCENE_BUS_COMP_SIDECHAIN_OFF
};

void scene_busCompDefaults(scene_settings_t *settings)
{
    uint8_t field;

    /*
     * Seed one settings image with the bus compressor defaults.
     *
     * Direct assignment is allowed here (the extension rule's
     * initialization case): callers are scene_initAll(), the Scene Load
     * stage and the boot reader's empty Scene, which all mark or commit the
     * whole Scene themselves.
     */
    if (!settings)
        return;
    for (field = 0u; field < SCENE_BUS_COMP_FIELD_COUNT; field++)
        settings->bus_comp[field] = scene_busCompDefault[field];
}

uint8_t scene_busCompClamp(uint8_t field, uint8_t value)
{
    /* Contract in SceneData.h. */
    if (field >= SCENE_BUS_COMP_FIELD_COUNT)
        return 0u;
    return (value > scene_busCompMax[field]) ? scene_busCompMax[field]
                                             : value;
}

void scene_setBusCompSetting(uint8_t scene_index, uint8_t field,
                             uint8_t value)
{
    scene_t *scene = scene_get(scene_index);

    /*
     * Store one bus compressor byte through the scalar owner funnel.
     *
     * Inputs: resident Scene, field, any byte. Output: the clamped value is
     * stored before its AutoSave bit (41 + field) is marked; equal values do
     * nothing. Callers: preset_setBusCompSetting(),
     * autosave_applyScenePayload().
     */
    if (!scene || field >= SCENE_BUS_COMP_FIELD_COUNT)
        return;
    scene_storeParameterByte(
        scene_index, &scene->settings.bus_comp[field],
        (uint8_t)(AUTOSAVE_SCENE_PARAM_BUS_COMP_BASE + field),
        scene_busCompClamp(field, value));
}

uint8_t scene_getBusCompSetting(uint8_t scene_index, uint8_t field)
{
    const scene_t *scene = scene_getConst(scene_index);

    /* Return the retained byte, or 0 (off) for an invalid Scene or field. */
    if (!scene || field >= SCENE_BUS_COMP_FIELD_COUNT)
        return 0u;
    return scene->settings.bus_comp[field];
}
```

---

## 5. AutoSave (Stage 1)

### A3 — `Core/Bank/Scene/Autosave.h` L242–244 — MODIFY

Current:

```c
    /* Scene Effect Morph amount; appended so earlier wire positions stay fixed. */
    AUTOSAVE_SCENE_PARAM_EFFECT_MORPH = 40,
    AUTOSAVE_SCENE_PARAM_COUNT = 41
```

Replace with:

```c
    /* Scene Effect Morph amount; appended so earlier wire positions stay fixed. */
    AUTOSAVE_SCENE_PARAM_EFFECT_MORPH = 40,
    /*
     * S074 bus compressor: SceneData bus_comp[0..3] in
     * scene_bus_comp_field_t order (cmp, cam, ctm, csc).
     *
     * Appended into previously reserved cells: the record, mask, offsets and
     * section sizes are unchanged, so there is no format-version bump (the
     * index-40 precedent). Every existing record holds 0 here (records are
     * created zero-filled and reserved cells are never written), which
     * restores as off, cam 0, ctm 0, csc off.
     */
    AUTOSAVE_SCENE_PARAM_BUS_COMP_BASE = 41,
    AUTOSAVE_SCENE_PARAM_COUNT = 45
```

---

### A2 — `Core/Bank/Scene/Autosave.h` L190 — MODIFY

Current:

```c
#define AUTOSAVE_SCENE_PARAMETER_LIVE_BYTES    41u
```

Replace with:

```c
#define AUTOSAVE_SCENE_PARAMETER_LIVE_BYTES    45u  /* S074: +4 bus compressor */
```

The block comment above (A1) states the contract. The existing asserts at
L360–365 bind this value to `AUTOSAVE_SCENE_PARAM_COUNT` and to the 118
reserved cells.

---

### A1 — `Core/Bank/Scene/Autosave.h` L180–181 — MODIFY

Current:

```c
 * Scene source occupies bytes 8..9; parameters occupy bytes 10..127, of
 * which indices 0..40 exist (40 = Effect Morph amount, Session 072). The
```

Replace with:

```c
 * Scene source occupies bytes 8..9; parameters occupy bytes 10..127, of
 * which indices 0..44 exist (40 = Effect Morph amount, Session 072; 41..44 =
 * the bus compressor's cmp/cam/ctm/csc, S074). The
```

---

### A7 — `Core/Bank/Scene/Autosave.c` L1394–1397 — MODIFY

Current:

```c
        } else {
            /* Index 40 restores the retained Scene Effect Morph amount. */
            scene_setEffectMorphAmount(scene_index, value);
        }
```

Replace with:

```c
        } else if (parameter_index == AUTOSAVE_SCENE_PARAM_EFFECT_MORPH) {
            /* Index 40 restores the retained Scene Effect Morph amount. */
            scene_setEffectMorphAmount(scene_index, value);
        } else {
            /*
             * Indices 41..44 restore the S074 bus compressor through its
             * clamping, change-aware setter. A pre-S074 record's zeros
             * restore as off, cam 0, ctm 0, csc off (safe; spec R8).
             */
            scene_setBusCompSetting(
                scene_index,
                (uint8_t)(parameter_index -
                          AUTOSAVE_SCENE_PARAM_BUS_COMP_BASE),
                value);
        }
```

---

### A6 — `Core/Bank/Scene/Autosave.c` L1324 and L1330 — MODIFY (comment only)

- L1324: replace `* What: the inverse of autosave_getSceneParameter(). Reads the 40 live`
  with:

  ```c
   * What: the inverse of autosave_getSceneParameter(). Reads the 45 live
  ```

- L1330: replace `* fader_setting[6], midi_channel[7], midi_note[7] all updated in`
  with two lines:

  ```c
   * fader_setting[6], midi_channel[7], midi_note[7], effect_morph_amount and
   * the S074 bus_comp[4] all updated in
  ```

---

### A5 — `Core/Bank/Scene/Autosave.c` L913–916 — MODIFY

Current:

```c
    } else {
        /* Index 40 is the retained Scene Effect Morph amount. */
        *value = scene->settings.effect_morph_amount;
    }
```

Replace with:

```c
    } else if (parameter_index == AUTOSAVE_SCENE_PARAM_EFFECT_MORPH) {
        /* Index 40 is the retained Scene Effect Morph amount. */
        *value = scene->settings.effect_morph_amount;
    } else {
        /* Indices 41..44 are the S074 bus compressor settings (cmp..csc). */
        *value = scene->settings.bus_comp[
            parameter_index - AUTOSAVE_SCENE_PARAM_BUS_COMP_BASE];
    }
```

The bound check at L886–889 (`parameter_index >= AUTOSAVE_SCENE_PARAM_COUNT`)
keeps the array index within 0..3.

---

### A4 — `Core/Bank/Scene/Autosave.c` after L86 — ADD

Context: the Effect Morph and MIDI-note group assert ends at L86. Insert
after L86:

```c
/*
 * The bus compressor group follows Effect Morph and closes the Scene list
 * (S074).
 *
 * Inputs: the Scene parameter enum and SceneData's field count. Output: a
 * build failure if Effect Morph stops being exactly one cell, or if the
 * group stops covering every bus_comp[] field. Affiliates:
 * autosave_getSceneParameter(), autosave_applyScenePayload(),
 * scene_setBusCompSetting().
 */
_Static_assert(AUTOSAVE_SCENE_PARAM_BUS_COMP_BASE ==
                   AUTOSAVE_SCENE_PARAM_EFFECT_MORPH + 1u,
               "Scene Effect Morph must remain one cell");
_Static_assert(AUTOSAVE_SCENE_PARAM_COUNT -
                   AUTOSAVE_SCENE_PARAM_BUS_COMP_BASE ==
                   SCENE_BUS_COMP_FIELD_COUNT,
               "Scene bus compressor group must cover every field");
```

These need no other AutoSave change:

- the marker bound (L1691);
- the whole-Scene marker loop (L2144);
- the live payload projection (L1085–1091).

All three already iterate or clamp to `AUTOSAVE_SCENE_PARAM_COUNT` or to
the 118 allocated cells.

---

## 6. Storage (Stage 1)

### F1 — `Core/Hardware/SD/storageTypes.h` after L387 — ADD

Context: `storage_status_t storage_scenesetFinalize(const storage_sceneset_t *state);`
(L387). Insert after L387:

```c
/*
 * sceneset.scg key of one S074 bus compressor field.
 *
 * Inputs: field in scene_bus_comp_field_t order. Output: "bus_comp_mode",
 * "bus_comp_amount", "bus_comp_time" or "bus_comp_sidechain", or NULL for
 * an invalid field. Why: the parser and filesystem.c's Scene writer share
 * one key table, so a key cannot be spelled two ways. The keys are
 * permanent (approved 2026-09-29).
 */
const char *storage_busCompKey(uint8_t field);
```

---

### F4 — `Core/Hardware/SD/storageTypes.c` L675–676 — MODIFY

Current:

```c
        target_settings->effect_morph_amount = parsed;
    }
```

Replace with:

```c
        target_settings->effect_morph_amount = parsed;
    } else if (storage_busCompFieldForKey(key, &field)) {
        /*
         * Parse one S074 bus compressor setting.
         *
         * Input: one 0..255 value for bus_comp_mode, bus_comp_amount,
         * bus_comp_time or bus_comp_sidechain. Output: the staged Scene
         * byte, clamped to its field's domain (mode 0..2, amount/time
         * 0..127, sidechain 0..6), so a hand-edited file cannot store an
         * unreachable value. The keys are optional: an older file keeps the
         * stage defaults (off, 48, 48, off) from filesystem_initSceneStage().
         */
        if (!target_settings)
            return STORAGE_STATUS_BAD_VALUE;
        st = storage_parseU8(value, &parsed);
        if (st != STORAGE_STATUS_OK)
            return st;
        target_settings->bus_comp[field] = scene_busCompClamp(field, parsed);
    }
```

---

### F3 — `Core/Hardware/SD/storageTypes.c` after L524 — ADD

Context: the locals of `storage_scenesetParseLine()`, ending with
`uint8_t parsed;` (L524). Insert after L524:

```c
    uint8_t field = 0u;   /* S074: bus compressor field matched by key (F4) */
```

---

### F2 — `Core/Hardware/SD/storageTypes.c` after L513 — ADD

Context: `storage_scenesetInit()` ends at L513, and
`storage_scenesetParseLine()` starts at L515. `storage_streq()` is defined at
L177. Insert after L513:

```c

/*
 * S074 bus compressor sceneset keys, in scene_bus_comp_field_t order.
 *
 * What: the four permanent sceneset.scg keys. Why: one table for the parser
 * below and the Scene writer (filesystem_nextScenesetLine() lines 11..14).
 * Older firmware ignores unknown keys, so files stay loadable both ways.
 * Affiliates: storage_busCompKey(), storage_busCompFieldForKey().
 */
static const char *const storage_busCompKeys[SCENE_BUS_COMP_FIELD_COUNT] = {
    "bus_comp_mode",
    "bus_comp_amount",
    "bus_comp_time",
    "bus_comp_sidechain",
};

const char *storage_busCompKey(uint8_t field)
{
    /* Contract in storageTypes.h. */
    return (field < SCENE_BUS_COMP_FIELD_COUNT) ? storage_busCompKeys[field]
                                                : NULL;
}

/*
 * Match one sceneset key against the bus compressor keys.
 *
 * Inputs: the parsed key. Output: 1 and *field on a match, otherwise 0 with
 * *field unchanged. Caller: storage_scenesetParseLine().
 */
static uint8_t storage_busCompFieldForKey(const char *key, uint8_t *field)
{
    uint8_t i;

    for (i = 0u; i < SCENE_BUS_COMP_FIELD_COUNT; i++) {
        if (storage_streq(key, storage_busCompKeys[i])) {
            *field = i;
            return 1u;
        }
    }
    return 0u;
}
```

---

### F7 — `Core/Hardware/SD/filesystem.c` after L28440 — ADD

Context: `filesystem_bootReaderEmptyScene()`, after
`memset(scene, 0, sizeof(*scene));` (L28439) and
`scene->settings.voice_decimation_all = 127u;` (L28440). Insert after
L28440:

```c
    /*
     * S074: the emptied Scene gets the same bus compressor defaults as
     * scene_initAll() (off, 48, 48, off), from the shared SceneData helper.
     */
    scene_busCompDefaults(&scene->settings);
```

---

### F6 — `Core/Hardware/SD/filesystem.c` after L16948 — ADD

Context (L16945–16950):

```c
    case 10u:
        return filesystem_formatAssignmentU16Line(
            dst, cap, "effect_morph_amount",
            scene->settings.effect_morph_amount);
    default:
        return 0u;
```

Insert after L16948:

```c
    case 11u:
    case 12u:
    case 13u:
    case 14u: {
        /*
         * S074 bus compressor, one line per field in field order.
         *
         * Inputs: line index 11..14 selects the field. Output:
         * "bus_comp_<name>=<value>\n" with the key from the shared
         * storageTypes table, so the writer and parser cannot disagree.
         * Default then returns 0 after line 14, ending the file.
         */
        const uint8_t field = (uint8_t)(op_write_line_index - 11u);

        return filesystem_formatAssignmentU16Line(
            dst, cap, storage_busCompKey(field),
            scene->settings.bus_comp[field]);
    }
```

---

### F5 — `Core/Hardware/SD/filesystem.c` after L16320 — ADD

Context: `filesystem_initSceneStage()`,
`stage->settings.voice_decimation_all = 127u;` (L16320). Insert after
L16320:

```c
    /*
     * S074: stage the bus compressor defaults (off, 48, 48, off) so a
     * sceneset.scg without the bus_comp_* keys loads the same values as a
     * fresh Scene. Shared helper with scene_initAll().
     */
    scene_busCompDefaults(&stage->settings);
```

`filesystem_commitSceneStage()` copies the whole settings struct
(`target->settings = …scene_stage.settings`), so it needs no change.

---

## 7. Preset (Stage 1)

### P3 — `Core/Bank/Scene/Preset/presetManager.c` after L3097 — ADD

Context: `preset_setVoiceDecimationAll()` ends at L3097, and
`preset_morphTick()` starts at L3099. Insert after L3097:

```c

/*
 * The PAR_BUS_COMP_* mirrors and SceneData's fields share one order
 * (S074): PAR_BUS_COMP_MODE + field. A build failure here means the two
 * lists drifted.
 */
_Static_assert(PAR_BUS_COMP_SIDECHAIN - PAR_BUS_COMP_MODE + 1 ==
                   SCENE_BUS_COMP_FIELD_COUNT,
               "bus compressor mirrors must match SceneData's field order");

void preset_setBusCompSetting(uint8_t scene_index, uint8_t field,
                              uint8_t value)
{
    /*
     * Retain one S074 bus compressor setting for one Scene.
     *
     * Inputs: Scene index, field (scene_bus_comp_field_t), any byte.
     * Outputs: SceneData stores the clamped value (AutoSave marked; equal
     * values are no-ops); the page mirror is updated only when this is the
     * active Scene, so a fan-out to other edit-masked Scenes never shows a
     * value the active Scene does not have. No runtime apply:
     * BusCompressor.c reads the active Scene every block.
     * Affiliates: menu_commitBusCompParam(), scene_setBusCompSetting().
     */
    if (!scene_get(scene_index) || field >= SCENE_BUS_COMP_FIELD_COUNT)
        return;
    value = scene_busCompClamp(field, value);
    scene_setBusCompSetting(scene_index, field, value);
    if (scene_index == scene_getActiveIndex())
        parameter_values[PAR_BUS_COMP_MODE + field] = value;
}

void preset_syncBusCompMirrors(void)
{
    const uint8_t scene_index = scene_getActiveIndex();
    uint8_t field;

    /*
     * Copy the active Scene's four bus compressor values into the page
     * mirrors.
     *
     * Inputs: none (active Scene). Output: parameter_values[PAR_BUS_COMP_*].
     * Callers: preset_applySceneSettings() (Scene activation and loads),
     * menu_commitBusCompParam() (after a fan-out edit), and
     * menu_parseGlobalParam() (the Global bulk apply after a Settings Load,
     * which may have zeroed these bytes). Read-only on SceneData.
     */
    for (field = 0u; field < SCENE_BUS_COMP_FIELD_COUNT; field++)
        parameter_values[PAR_BUS_COMP_MODE + field] =
            scene_getBusCompSetting(scene_index, field);
}
```

---

### P2 — `Core/Bank/Scene/Preset/presetManager.c` after L1295 — ADD

Context: the end of `preset_applySceneSettings()`:

```c
    parameter_values[PAR_VOICE_DECIMATION_ALL] =
        scene->settings.voice_decimation_all;
    preset_applyVoiceDecimationAllRuntime(scene->settings.voice_decimation_all);
}
```

Insert after L1295:

```c
    /*
     * S074: the bus compressor page mirrors follow the newly active Scene.
     *
     * Output: parameter_values[PAR_BUS_COMP_*] = this Scene's four values,
     * so the settings page repaints with them. No runtime apply: the
     * compressor reads the active Scene every block, and its cell carries
     * over a Scene switch that keeps the same target.
     */
    preset_syncBusCompMirrors();
```

---

### P1 — `Core/Bank/Scene/Preset/presetManager.h` after L530 — ADD

Context: `void    preset_setVoiceDecimationAll(uint8_t scene_index, uint8_t value);`
(L530). Insert after L530:

```c
/*
 * S074 master bus compressor Scene settings (cmp, cam, ctm, csc).
 *
 * preset_setBusCompSetting(): clamps and commits one field of one Scene
 * through SceneData (AutoSave marked), then refreshes that field's page
 * mirror when the Scene is active. preset_syncBusCompMirrors(): copies the
 * active Scene's four values into parameter_values[PAR_BUS_COMP_*]. Neither
 * touches the DSP: BusCompressor.c reads the active Scene every block.
 * Affiliates: menu.c's bus compressor commit and bulk-apply guard,
 * preset_applySceneSettings().
 */
void    preset_setBusCompSetting(uint8_t scene_index, uint8_t field,
                                 uint8_t value);
void    preset_syncBusCompMirrors(void);
```

---

### M1 — `Core/Bank/Scene/Preset/ParameterArray.h` — ADD (Stage 1)

**M1b, after L177** (after the `PAR_AUTOSAVE_ENABLED` assert):

```c
_Static_assert(PAR_BUS_COMP_SIDECHAIN < NUM_PARAMS,
	"S074 bus compressor mirrors must fit the fixed flat parameter allocation");
```

**M1a, after L171** (`PAR_AUTOSAVE_ENABLED,`):

```c

	/*
	 * S074 bus compressor page mirrors (settings menu: cmp cam ctm csc).
	 *
	 * What: display and edit mirrors of the active Scene's bus_comp[], in
	 * scene_bus_comp_field_t order (PAR_BUS_COMP_MODE + field); ids 58..61.
	 * Why: static settings cells resolve values through parameter_values[];
	 * the Scene stays the only owner. Writers: Preset only (Scene apply,
	 * edits, mirror sync). They are never saved to settings.cfg, and the
	 * Global bulk apply only refreshes them (menu_parseGlobalParam()).
	 * NUM_PARAMS remains fixed: 0 B.
	 */
	PAR_BUS_COMP_MODE,
	PAR_BUS_COMP_AMOUNT,
	PAR_BUS_COMP_TIME,
	PAR_BUS_COMP_SIDECHAIN,
```

The ids are 58–61 (measured: `PAR_AUTOSAVE_ENABLED` = 57). No id at or above
128 is added. The legacy CC2 paths in `MidiParser.c` (L1322, L1331) write
only their own CC2 ids. The keyed settings parser writes only the ids in its
key table (`filesystem.c:2674–2687`). Nothing else writes these indices.

---

## 8. Menu (Stage 3)

### M2 — `Core/Menu/menu.h` — ADD/MODIFY

**M2e, L203.** Current: `SHORT_AUTOSAVE`. Replace with:

```c
    SHORT_AUTOSAVE,
    /* S074 bus compressor compact labels (cmp cam ctm csc). */
    SHORT_BUS_COMP_MODE, SHORT_BUS_COMP_AMOUNT,
    SHORT_BUS_COMP_TIME, SHORT_BUS_COMP_SIDECHAIN
```

**M2d, after L175** (`LONG_AUTOSAVE,`):

```c
    /* S074 bus compressor long names (BusComp CompAmt CompTime CompSC). */
    LONG_BUS_COMP_MODE, LONG_BUS_COMP_AMOUNT,
    LONG_BUS_COMP_TIME, LONG_BUS_COMP_SIDECHAIN,
```

**M2c, L145.** Current: `    CAT_GENERATOR, CAT_MIDI, CAT_TRIGGER`. Replace
with:

```c
    CAT_GENERATOR, CAT_MIDI, CAT_TRIGGER,
    /* S074: Scene-owned static cells (the bus compressor page). */
    CAT_SCENE
```

**M2b, L135.** Current: `    TEXT_AUTOSAVE,` followed by `NUM_NAMES` at
L136. Replace L135 with:

```c
    TEXT_AUTOSAVE,
    /*
     * S074 Scene-owned bus compressor page (MENU_GLOBAL_SCENE_SUBPAGE).
     * The parallel short, long and valueNames tables stay aligned with
     * these four ids. NUM_NAMES becomes 99 (top1..top8 are uint8_t).
     */
    TEXT_BUS_COMP_MODE, TEXT_BUS_COMP_AMOUNT,
    TEXT_BUS_COMP_TIME, TEXT_BUS_COMP_SIDECHAIN,
```

**M2a, after L26** (`#define NUM_SUB_PAGES   8`):

```c
/*
 * Settings sub-page holding the Scene-owned bus compressor page (S074,
 * BC18 option a).
 *
 * What: the MENU_MIDI_PAGE sub-page whose first half is cmp cam ctm csc.
 * Rule: it must remain the LAST populated settings sub-page. New global
 * pages go BEFORE it, and this constant moves up with them. Its second half
 * stays empty. Users: checkScrollSign() ('+' on it, '^' on the first
 * settings screen) and the DEV_MODE_DIAGNOSTIC boot check
 * menu_devCheckGlobalSceneSubPage(). Affiliates: menuPages.h.
 */
#define MENU_GLOBAL_SCENE_SUBPAGE 2u
_Static_assert(MENU_GLOBAL_SCENE_SUBPAGE < NUM_SUB_PAGES,
               "the Scene-owned settings page must be a real sub-page");
```

---

### M3 — `Core/Menu/MenuText.h` — ADD/MODIFY

**M3c, after L172** (`{"AutoSave"},`):

```c
    /* S074 bus compressor long names; valueNames assigns category Scene. */
    {"BusComp"},{"CompAmt"},{"CompTime"},{"CompSC"},
```

**M3b, L148.** Current: `    {"Generatr"},{"MIDI"},{"Trigger"},`. Replace
with:

```c
    {"Generatr"},{"MIDI"},{"Trigger"},
    /* S074: CAT_SCENE, the full-view category of Scene-owned static cells. */
    {"Scene"},
```

**M3a, after L139** (`{"ats"},`):

```c
    /* S074 bus compressor compact labels. */
    {"cmp"},{"cam"},{"ctm"},{"csc"},
```

---

### M4 — `Core/Menu/menuPages.h` L44 — REPLACE

Current L44 (sub-page 2, all empty):

```c
  {TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY, PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE},
```

Replace with:

```c
  /* S074 Scene-owned bus compressor page, sub-page MENU_GLOBAL_SCENE_SUBPAGE.
   * It must stay the LAST populated settings sub-page: add new global pages
   * BEFORE it and move the constant (menu.h). Its empty second half ends
   * encoder traversal here; SELECT loops back to sub-page 0. Values are
   * mirrors of the active Scene, committed by menu_commitBusCompParam(). */
  {TEXT_BUS_COMP_MODE,TEXT_BUS_COMP_AMOUNT,TEXT_BUS_COMP_TIME,TEXT_BUS_COMP_SIDECHAIN,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,
    PAR_BUS_COMP_MODE,PAR_BUS_COMP_AMOUNT,PAR_BUS_COMP_TIME,PAR_BUS_COMP_SIDECHAIN,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE},
```

Navigation needs no change, as verified in `menu.c`:

- **Encoder** (L10329–10364): scrolling past `pts` (sub-page 1, cell 7)
  moves to sub-page 2, cell 0. It stops at the empty cell 4.
- **SELECT** (`menu_switchSubPage()` L12433): SELECT 3 opens sub-page 2.
  Pressing it again (no second half) returns to sub-page 0.
- **Cue on the previous screen:** the `…pts` screen's `<` becomes `*` through
  the existing `menuPages[MENU_MIDI_PAGE][activePage+1].top1` test.

---

### E13 — `Core/Menu/menu.c` after L13718 — ADD

Context: `menu_init()`, `menu_activeVoice = 0;` (L13718). Insert after
L13718:

```c
#if DEV_MODE_DIAGNOSTIC
    /* S074 BC18: the bus compressor page must be the last settings page. */
    menu_devCheckGlobalSceneSubPage();
#endif
```

---

### E12 — `Core/Menu/menu.c` after L13706 — ADD

Context: `parameter_values[PAR_VOICE_DECIMATION_ALL] = 127u;` (L13706).
Insert after L13706:

```c
    /*
     * S074 bus compressor mirrors start at the Scene defaults (off, 48, 48,
     * off) until preset_applySceneSettings() mirrors the loaded Scene.
     * Inputs: the zeroed buffer above (mode and sidechain are already 0).
     * Output: truthful early page values; no Scene or DSP effect.
     */
    parameter_values[PAR_BUS_COMP_AMOUNT] = SCENE_BUS_COMP_DEFAULT_AMOUNT;
    parameter_values[PAR_BUS_COMP_TIME] = SCENE_BUS_COMP_DEFAULT_TIME;
```

---

### E11 — `Core/Menu/menu.c` after L13654 — ADD

Context: the blank line (L13654) before the `menu_init` banner (L13655). Insert
after L13654:

```c
#if DEV_MODE_DIAGNOSTIC
/*
 * BC18 "always last" check for the settings menu (S074; diagnostic builds).
 *
 * What:       verifies that MENU_GLOBAL_SCENE_SUBPAGE holds the bus
 *             compressor page (cmp first, second half empty) and that no
 *             later MENU_MIDI_PAGE sub-page is populated.
 * Why:        the page's '+'/'^' cues and last-page behaviour rely on the
 *             rule "new global pages go before it". A compile-time assert
 *             cannot read a const table in C.
 * Inputs:     menuPages[][].
 * Outputs:    nothing on success. On failure, two LCD rows for 1.5 s,
 *             after which boot continues. No state changes.
 * Build:      production (DEV_MODE_DIAGNOSTIC 0) compiles it out, as it
 *             does the FxBf boot diagnostic.
 * Affiliates: menuPages.h, MENU_GLOBAL_SCENE_SUBPAGE, DEV_MODES.md.
 */
static void menu_devCheckGlobalSceneSubPage(void)
{
    const Page *scene_page =
        &menuPages[MENU_MIDI_PAGE][MENU_GLOBAL_SCENE_SUBPAGE];
    uint8_t bad = (uint8_t)(scene_page->top1 != TEXT_BUS_COMP_MODE ||
                            scene_page->top5 != TEXT_EMPTY);
    uint8_t sub_page;
    uint16_t t0;

    for (sub_page = (uint8_t)(MENU_GLOBAL_SCENE_SUBPAGE + 1u);
         sub_page < NUM_SUB_PAGES; sub_page++) {
        if (menuPages[MENU_MIDI_PAGE][sub_page].top1 != TEXT_EMPTY)
            bad = 1u;
    }
    if (!bad)
        return;
    lcd_clear();
    lcd_setcursor(0, 1);
    lcd_string("Menu: cmp page  ");
    lcd_setcursor(0, 2);
    lcd_string("not last (BC18) ");
    lcd_waitForIdle();
    t0 = time_sysTick;
    while ((uint16_t)(time_sysTick - t0) < 1500u) { /* diagnostic hold */ }
}
#endif

```

`menu_init()` runs after `time_initSysTick()` and `lcd_init()` (`main.c`
L515–517), so the LCD and tick are live. `menu.c` already includes
`config.h`, `lcd.h` and `timebase.h`.

---

### E10 — `Core/Menu/menu.c` after L13202 — ADD

Context: `case PAR_VOICE_DECIMATION_ALL:` ends with `break;` and `}`
(L13201–13202), and `case PAR_ROLL:` follows at L13204. Insert after
L13202:

```c

    case PAR_BUS_COMP_MODE:
    case PAR_BUS_COMP_AMOUNT:
    case PAR_BUS_COMP_TIME:
    case PAR_BUS_COMP_SIDECHAIN:
        /*
         * S074 bus compressor mirrors: refresh, never write.
         *
         * What: the Global bulk apply (menu_sendAllGlobals(),
         * menu_tickGlobalApply()) replays every id from
         * PAR_BEGINNING_OF_GLOBALS after a Settings or legacy .all load, from
         * bytes the legacy paths may have zeroed. For these Scene-owned ids
         * it only copies the active Scene's values back into the mirrors, so
         * a settings load can neither write any Scene nor leave the page
         * showing zeros. Edits never arrive here: menu_cellCommitValue()
         * commits them directly. Input value: ignored. Output: the mirrors.
         */
        preset_syncBusCompMirrors();
        break;
```

---

### E9 — `Core/Menu/menu.c` after L11240 — ADD

Context: `menu_paramIsMorphAmount()` ends at L11240, and
`menu_updateEndlessPotScales()` starts at L11242. Insert after L11240:

```c

/*
 * S074 bus compressor cell helpers (prototypes near the top of the file).
 *
 * menu_paramIsBusComp(): nonzero for the four PAR_BUS_COMP_* mirrors.
 * menu_busCompValueText(): writes three characters for cmp (off/St1/St2)
 *   and csc (off/"  1".."  6") and returns 1; returns 0 for cam, ctm and
 *   every other id, so the generic numeric text stands.
 * menu_commitBusCompParam(): sends one edited value to every Scene in the
 *   VOICE edit mask (as PERF srt does), then refreshes the mirror from the
 *   active Scene, so the page stays truthful even when the active Scene is
 *   outside the mask. Returns 1 (repaint).
 * Affiliates: menu_cellCommitValue(), menu_clampCellValue(),
 * menu_formatCellValue3(), the full edit view, preset_setBusCompSetting(),
 * preset_syncBusCompMirrors().
 */
static uint8_t menu_paramIsBusComp(uint16_t paramNr)
{
    return (uint8_t)(paramNr >= PAR_BUS_COMP_MODE &&
                     paramNr <= PAR_BUS_COMP_SIDECHAIN);
}

static uint8_t menu_busCompValueText(uint16_t paramNr, uint8_t value,
                                     char *dst)
{
    if (paramNr == PAR_BUS_COMP_MODE) {
        if (value == SCENE_BUS_COMP_MODE_ST1)
            memcpy(dst, "St1", 3);
        else if (value == SCENE_BUS_COMP_MODE_ST2)
            memcpy(dst, "St2", 3);
        else
            memcpy(dst, menuText_off, 3);
        return 1u;
    }
    if (paramNr == PAR_BUS_COMP_SIDECHAIN) {
        if (value == SCENE_BUS_COMP_SIDECHAIN_OFF)
            memcpy(dst, menuText_off, 3);
        else
            numtostrpu(dst, value, ' ');
        return 1u;
    }
    return 0u;
}

static uint8_t menu_commitBusCompParam(uint16_t paramNr, uint8_t value)
{
    const uint8_t field = (uint8_t)(paramNr - PAR_BUS_COMP_MODE);
    const uint16_t edit_mask = bank_sceneMaskVoiceEdit();
    uint8_t scene_index;

    for (scene_index = 0u;
         scene_index < SCENE_COUNT && scene_index < 16u;
         scene_index++) {
        if ((edit_mask & (uint16_t)(1u << scene_index)) != 0u)
            preset_setBusCompSetting(scene_index, field, value);
    }
    preset_syncBusCompMirrors();
    return 1u;
}
```

---

### E8 — `Core/Menu/menu.c` after L10000 — ADD

Context: in the full edit view, the S074 Effect value hook ends at L10000,
before `} else {` (L10001, the compact overview). Insert after L10000:

```c
        /*
         * S074 bus compressor value text in the full view (cmp, csc).
         *
         * What: replaces the generic DTYPE_0B127 number at [1][13..15] with
         * off/St1/St2 or off/1..6. The category (Scene) and long name
         * already come from valueNames. cam and ctm keep the number.
         */
        if (cell.kind == MENU_CELL_STATIC) {
            const uint8_t bus_value =
                (curParmVal > 255u) ? 255u : (uint8_t)curParmVal;

            (void)menu_busCompValueText(cell.static_param, bus_value,
                                        &editDisplayBuffer[1][13]);
        }
```

---

### E7 — `Core/Menu/menu.c` L8133–8149 — REPLACE

Current:

```c
    if (menu_activePage == MENU_MIDI_PAGE) {
        if (is2ndPage) {
            if ((activePage < NUM_SUB_PAGES-1) &&
                (menuPages[MENU_MIDI_PAGE][activePage+1].top1 != TEXT_EMPTY))
                return '*';
            else
                return '<';
        } else {
            if (has2ndPage(activePage)) {
                if (activePage > 0) return '*';
                else return '>';
            } else {
                if (activePage > 0) return '<';
                else return 0;
            }
        }
    }
```

Replace with:

```c
    if (menu_activePage == MENU_MIDI_PAGE) {
        /*
         * Settings-menu scroll cues with the Scene-owned compressor page
         * (S074, the VOICE mix convention).
         *
         * What: the compressor page (MENU_GLOBAL_SCENE_SUBPAGE, first half)
         * shows '+': Scene-owned, and never '<' even though SELECT loops
         * from it. The first settings screen shows '^' because a Scene page
         * follows at the end. Middle screens keep '*'. The screen before the
         * compressor page becomes '*' through the unchanged next-page test.
         * Inputs: settings sub-page and cursor half. Output: the column-15
         * marker. Affiliates: menuPages.h, MENU_GLOBAL_SCENE_SUBPAGE.
         */
        if (activePage == MENU_GLOBAL_SCENE_SUBPAGE && !is2ndPage)
            return '+';
        if (is2ndPage) {
            if ((activePage < NUM_SUB_PAGES-1) &&
                (menuPages[MENU_MIDI_PAGE][activePage+1].top1 != TEXT_EMPTY))
                return '*';
            else
                return '<';
        } else {
            if (has2ndPage(activePage)) {
                if (activePage > 0) return '*';
                else return '^';
            } else {
                if (activePage > 0) return '<';
                else return 0;
            }
        }
    }
```

---

### E6 — `Core/Menu/menu.c` after L4423 — ADD

Context: in `menu_clampCellValue()`, the `MENU_CELL_SCENE_SETTING` block ends
at L4423 (`return;` L4422, `}` L4423). `dtype = …` follows at L4425. Insert
after L4423:

```c

    /*
     * S074 bus compressor cells clamp to their Scene field's domain.
     *
     * What: cmp 0..2, cam/ctm 0..127, csc 0..6, from SceneData's table
     * (scene_busCompClamp() of 255 returns the field maximum). Why before
     * the generic path: their DTYPE_0B127 entry would allow unreachable
     * cmp/csc values, and no dtype code is free for a tighter one.
     */
    if (cell->kind == MENU_CELL_STATIC &&
        menu_paramIsBusComp(cell->static_param)) {
        const uint8_t max = scene_busCompClamp(
            (uint8_t)(cell->static_param - PAR_BUS_COMP_MODE), 255u);

        if (*value > max)
            *value = max;
        return;
    }
```

---

### E5 — `Core/Menu/menu.c` after L4323 — ADD

Context: in `menu_formatCellValue3()`, the fader-name special case ends at
L4323, before `switch (dtype) {`. Insert after L4323:

```c
    /*
     * S074 bus compressor compact value text: cmp off/St1/St2, csc
     * off/1..6. cam and ctm fall through to the plain 0..127 text.
     */
    if (cell && cell->kind == MENU_CELL_STATIC &&
        menu_busCompValueText(cell->static_param, value, valueAsText))
        return;
```

---

### E4 — `Core/Menu/menu.c` after L3717 — ADD

Context: in `menu_cellCommitValue()`, the `MENU_CELL_SCENE_SETTING` block
closes at L3717, and `if (cell->kind == MENU_CELL_STATIC) {` follows at
L3718. Insert after L3717:

```c
    /*
     * S074 bus compressor cells commit to the Scene, not to Globals.
     *
     * What: the four settings-page cells are Scene-owned. The clamped value
     * goes to every Scene in the VOICE edit mask through Preset (AutoSave
     * marked), and the mirror is refreshed from the active Scene.
     * Why before the generic static path: that path would write the mirror
     * as if it were a Global, mark settings.cfg dirty, and commit through
     * menu_parseGlobalParam(), which the Global bulk apply also replays.
     * Inputs: the clamped value. Output: nonzero (repaint).
     * Affiliates: menu_commitBusCompParam(), preset_setBusCompSetting().
     */
    if (cell->kind == MENU_CELL_STATIC &&
        menu_paramIsBusComp(cell->static_param))
        return menu_commitBusCompParam(cell->static_param, (uint8_t)value);
```

---

### E3 — `Core/Menu/menu.c` after L1811 — ADD

Context: `static void menu_clampCellValue(const menu_cell_t *cell, uint16_t *value);`
(L1811). Insert after L1811:

```c
/* S074 bus compressor cells; definitions beside menu_paramIsMorphAmount(). */
static uint8_t menu_paramIsBusComp(uint16_t paramNr);
static uint8_t menu_busCompValueText(uint16_t paramNr, uint8_t value,
                                     char *dst);
static uint8_t menu_commitBusCompParam(uint16_t paramNr, uint8_t value);
```

---

### E2 — `Core/Menu/menu.c` after L1181 — ADD

Context: `{SHORT_AUTOSAVE,CAT_GLOBAL,LONG_AUTOSAVE},` (L1181), the last
`valueNames[NUM_NAMES]` row. Insert after L1181:

```c
    /* S074 bus compressor: `cmp cam ctm csc` / Scene / BusComp.. (M2, M3). */
    {SHORT_BUS_COMP_MODE,CAT_SCENE,LONG_BUS_COMP_MODE},
    {SHORT_BUS_COMP_AMOUNT,CAT_SCENE,LONG_BUS_COMP_AMOUNT},
    {SHORT_BUS_COMP_TIME,CAT_SCENE,LONG_BUS_COMP_TIME},
    {SHORT_BUS_COMP_SIDECHAIN,CAT_SCENE,LONG_BUS_COMP_SIDECHAIN},
```

The table is sized `[NUM_NAMES]`. Too many rows fail the build, but a
missing row would be zero-filled silently, so keep these four rows in the
same order as the M2b text ids.

---

### E1 — `Core/Menu/menu.c` after L1031 — ADD

Context: `[PAR_AUTOSAVE_ENABLED] = DTYPE_ON_OFF,` (L1031). Insert after
L1031:

```c
    /*
     * S074 bus compressor mirrors. The generic dtype only sets the numeric
     * text and endless-pot scale: menu_clampCellValue() applies each field's
     * own domain (cmp 0..2, csc 0..6), and cmp/csc text is special-cased,
     * because every dtype code and DTYPE_MENU id is already taken.
     */
    [PAR_BUS_COMP_MODE] = DTYPE_0B127,
    [PAR_BUS_COMP_AMOUNT] = DTYPE_0B127,
    [PAR_BUS_COMP_TIME] = DTYPE_0B127,
    [PAR_BUS_COMP_SIDECHAIN] = DTYPE_0B127,
```

---

## 9. Verification

### 9.1 Build (each stage)

- Run `make all`, then `make img`. Expect no new warnings. The existing
  filesystem/pattern warnings and newlib linker notes are unchanged.
- Check `tools/link_budget.py`, which runs as part of `all`:
  - **Stage 1:** SRAM1 `bss` +64 B (`scenes`).
  - **Stage 2:** DTCM statics 4,448 → 4,472 B (+24); FX arena 126,624 →
    126,592 B; the arena minimum assert still passes (3,712 B margin).
- Run `arm-none-eabi-nm -S build/lxr02.elf | grep -i buscomp`. The state
  symbol (LTO may add a `.lto_priv` suffix) should be `0x18` bytes at a
  `0x2000xxxx` address.
- **Stage 1:** the AutoSave record size is unchanged (34,768 B). The two A4
  asserts and the existing L360–365 asserts all hold.
- **Stage 3:** `NUM_NAMES` = 99. The `valueNames[NUM_NAMES]` row count
  matches.
- **Diagnostic build** (optional): set `DEV_MODE_DIAGNOSTIC 1` and boot. The
  BC18 check must stay silent. Then temporarily populate sub-page 3 in
  `menuPages.h`; the check must show its message. Restore both.

### 9.2 Arithmetic spot-checks (spec §4 tables)

- **Makeup:** `cam` 48, 64, 96 and 127 give +1.2, +3.1, +9.1 and +15.8 dB.
- **Duck:** `cam` 48 at velocities 127, 100, 80 and 64 gives −12.4, −6.1,
  −3.1 and −1.6 dB.
- **Saturator at `d = 1`:** −6 dBFS in gives −0.33 dB; 0 dBFS in gives
  −1.39 dB. At `d = 1.4` the ceiling is −2.92 dBFS.
- **Coefficients:** `k(5 ms)` = 0.1353; `k(60 ms)` = 0.01202.

### 9.3 Hardware and listening (yours)

| # | Check | Expect |
|---|---|---|
| H1 | Settings menu: SELECT 1 | Marker `^` on the first screen |
| H2 | Scroll to `… pts` | `*` (was `<`) |
| H3 | Scroll on | `cmp cam ctm csc` with marker `+`; the encoder stops at `csc` |
| H4 | SELECT 3; then SELECT again | Opens the page; loops back to sub-page 0 |
| H5 | Full view of each cell | `Scene` plus `BusComp`, `CompAmt`, `CompTime`, `CompSC`; values `off St1 St2` and `off 1..6` |
| H6 | `cmp` `off` → `St1` → `off` while playing | Fade in and out with no click, and no level jump at switch-on |
| H7 | `cmp` `St1` ↔ `St2` with sounds on both pairs | Fade out, then fade in; neither pair steps |
| H8 | `cmp St1`, headphones only | Compressed on the headphones |
| H9 | `cam` sweep 0 → 127 on a full mix | Increasing squash; output loudness roughly constant; gentle saturation |
| H10 | `ctm` sweep | Faster to slower release; long "memory" tails after sustained compression at high `ctm` |
| H11 | `csc 1`, kick on track 1 at velocities 30, 64, 100, 127 | Barely audible, light, clear, deep duck; 127 is always the deepest |
| H12 | `csc 6` with track 7 triggers | Ducks (BC11 assumption; tell me if track 7 should not count) |
| H13 | Two Scenes with different settings; switch while playing | The values change with the Scene; the page updates; no click |
| H14 | Edit with a multi-Scene VOICE edit mask | Every masked Scene takes the value |
| H15 | Save Scene; load it; check `sceneset.scg` | Four `bus_comp_*` lines; the values round-trip |
| H16 | Power-cycle (AutoSave on) | The values restore |
| H17 | Settings Load while on the page | The page values do not change |
| H18 | Worst-case Scene: `cmp` off against `St1`, `cpu` widget | Your manual CPU accounting (about 1 % expected) |
| H19 | `cmp off` while `csc` triggers fire, then switch on | No stale duck at switch-on |

---

## 10. Documentation follow-ups (after acceptance)

| Document | Update |
|---|---|
| `knowledge_files/specification_reference/EFFECTS_MIXER_DSP_REFERENCE.md` | New master-bus stage after the FX return; St1 = `output2`, St2 = `output`; the cost (0 while off, about 1 % on); sidechain via the trigger funnel |
| `…/AUTOSAVE.md` | Scene parameters 0..44 (L165: "45 live"; 41..44 bus compressor), no version bump, zero-restore of old records |
| `…/FILESYSTEM_SPEC.md` | `sceneset.scg` keys `bus_comp_mode/amount/time/sidechain`, domains, clamp, defaults |
| `…/STORAGE_SRAM_MANIFEST.md` | `scenes[]` +64 B; DTCM statics +24 B; FX arena 126,592 B |
| `…/MODULE_INTERCHANGE_SPEC.md` | Scene settings list gains the bus compressor |
| `DEV_MODES.md` | The BC18 settings-menu check under `DEV_MODE_DIAGNOSTIC` |
| `S074_BUS_COMP.md` | Status → implemented; the hardware acceptance section |
| `MEMORY.md` | Session log entry (per the project's closeout practice) |
| `tools/populate_scene_directory.py` | Optional: emit the four keys (missing keys already load as defaults) |

---

## 11. Observation (existing behaviour, not changed here)

**O1 — The bulk global apply replays PERF Scene mirrors into edit-masked
Scenes.**

- **Where:** `menu_tickGlobalApply()` and `menu_sendAllGlobals()` call
  `menu_parseGlobalParam()` for every id from `PAR_BEGINNING_OF_GLOBALS`
  up. They run after a Settings Load (`PRESET_OP_GLOBALS_LOAD`) and a
  legacy `.all` load.
- **What it does:** that range includes `PAR_VOICE1_MORPH..PAR_VOICE6_MORPH`
  and `PAR_VOICE_DECIMATION_ALL`. Their cases send the mirrored (active
  Scene) value to **every Scene in the VOICE edit mask**, marking AutoSave.
- **Effect:** when the mask spans Scenes with different per-voice Morph or
  `srt` values, a Settings Load makes them equal to the active Scene's. On
  the legacy load paths the mirrors are also zeroed first, so `srt` 0 would
  be written.
- **The compressor avoids this** by design (E10). A later fix for the
  existing ids could use the same refresh-only pattern. I have not verified
  this on hardware.
