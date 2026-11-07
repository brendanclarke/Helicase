# S093 P2 — Alkaline B-R: Pre-Implementation Brief

- **Session:** 093, phase 2. This document is written for a **fresh session**
  that will write the code. It holds every detail needed, with
  back-references.
- **Goal of the implementation:** an on-device prototype of the Alkaline
  (TB-303) instrument with the **B-R** filter, as a **Basic** type fitted
  under the Drum CPU ceiling, so the user can listen to it.
- **Decision (user, S093):**
  - Implement B-R fitted under Drum, for a listening trial.
  - Afterwards the user chooses: reduce further, or make Alkaline Advanced
    (replacing Cymbal/HiHat in a Scene) and add post-distortion/FX types in
    the freed margin.
  - Recorded in `S092_P0_ALKALINE_INSTRUMENT_PLAN.md` §9 D2 and
    `S093_P1_ALKALINE_OVERHEAD_REDUCTIONS.md` §8.
- **Project rules that bind this work** (`MEMORY.md`):
  - Commits belong to the user: do not commit or suggest commits.
  - Any new or enlarged RAM needs the user's sign-off (§9).
  - No backward compatibility, ever.
  - The CPU ceiling is binding: the new type must be ≤ Drum's dearest
    setting, measured linked and on the device.
  - Constant per-sample cost: no skips on silence or settings.
  - New ITCM code only in a new `.itcm.sNNN` subsection (none is planned
    here).
  - Do not edit `ResonantFilter.c`'s ZDF twins or existing oscillator loops.
  - Do not use the `.claude` auto-memory; project memory is in the
    repository.

---

## 0. Document map (read in this order)

| Doc | What to take from it |
|---|---|
| `MEMORY.md` | Binding rules (RAM, CPU, DSP S073 invariants, ITCM order, voice chain) |
| `S092_P0_ALKALINE_INSTRUMENT_PLAN.md` | §1.3 CPU gate rules; §5 instrument definition (registry, rows, trigger semantics, gate timer, platform interactions); §9 decisions D1–D17 |
| `S092_P6_DSP_EQUATIONS.md` | The model: §3 audio path, §4 control path, §5.2 prewarp, §5.3 the real-time network (SplitNet), §5.4 the kernel pseudo-code |
| `S093_P0_ALKALINE_C_FUNCTIONS_CODE.md` | The existing C (§1–§4), the bench (§5), cost method (§6), integration plan (§7.1–§7.3), **the Re3/Sw3 removal plan (§10)** |
| `S093_P1_ALKALINE_OVERHEAD_REDUCTIONS.md` | The reductions, network form N1 (§5), the two drive fixes (§2.1), the variant table (§4); **B-R = `B_n1rq_ota32`** |
| `knowledge_files/specification_reference/dsp_instruments_effects/INSTRUMENTS_DSP_REFERENCE.md` | §3 runtime slots and dispatch, §4 descriptors and writers, §9.4 "Add an instrument type", §10 pitfalls |
| `tools/dsp_test/DSP_TEST.md` | Bench conventions (extract by name, break builds, `fpseq.py`) |

---

## 1. What exists today (S093 P0/P1), and its status

| File | Status | Change in P2 |
|---|---|---|
| `Core/DSPAudio/DiodeLadder.h/.c` | S093 P0 options A and B (S092 P6 SplitNet, [5/4] secants, 3 V out). Not in the build | **Replace the kernels with B-R and A-R** (§3) |
| `Core/DSP/Instruments/Alkaline/AlkalineControl.h/.c` | Control path (expo, MEG, accent network, VEG, prewarp). Validated | None |
| `Core/DSP/Instruments/Alkaline/AlkalineVoice.h/.c` | The engine (init, trigger, async, render) | Trigger signature, drive, quiet query, B-R call (§5) |
| `Core/DSP/Instruments/Alkaline/AlkalineTables.h/.c` | Generated (`gen_alk_tables.py`): SplitNet Res table, prewarp, VEG, fitted constants | **Regenerate as N1** (§4) |
| `Core/DSP/Instruments/Alkaline/AlkalineSquareTable.c` | Generated: the D8 303 square `[11][1024]` (22,528 B) | None (Alkaline keeps its own square; D3) |
| `tools/alkaline/` | Python model (`alk_model.py`), generator, validation (`validate.py`), C cross-check (`check_c.py`) | `gen_alk_tables.py` → N1; `check_c.py` gains drive cases |
| `tools/alkaline/variants/` | The S093 P1 experiment kernel `dlv_kernel.c` (switches), `run_variants.py`, `gen_n1.py` | Reference for the port; keep |
| `tools/dsp_test/modules/alkaline.c` + Makefile target `alkaline` | Laws AL-B/R/S/N/AB + break | Update (§7) |

Nothing Alkaline is in the firmware build yet (`Makefile`,
`InstrumentManager`, menus).

---

## 2. Work order

| Phase | Content | Gate |
|---|---|---|
| **P2-0** (recommended first) | Remove Re3/Sw3 (S093 P0 §10, decided by the user). Frees 45,056 B of flash, which Alkaline needs (§8) | S093 P0 §10.4 W1–W7 |
| **P2-1** | DSP port: N1 tables, the B-R and A-R kernels, the 7 V output scale, drive | Bench `alkaline` green; `check_c.py`; ARM count of `DiodeLadder_renderB` ≈ 230 |
| **P2-2** | Firmware integration: type, registry, union, dispatch, parameters, menu, sequencer query, Makefile | `make all`, `special_tags`, the instrument-manager bench |
| **P2-3** | Gates: linked loop counts, mixer check, flash, device CPU, listening | §10 |

Confirm §9's decisions at the start of the session before P2-2.

---

## 3. The B-R kernel (`DiodeLadder_renderB`)

### 3.1 Definition

B-R is S093 P1's variant `B_n1rq_ota32`. The exact reference is
`tools/alkaline/variants/dlv_kernel.c` compiled with:

```
-DMODEL=1 -DPADE_RUNG=32 -DRUNG_CLAMP=0 -DPADE_IO=54 -DXIN_TAIL=1 -DGAU1=0
-DNET1=1 -DQ21_LIN=1 -DOTA_P32=1 -DSPLIT=0 -DUNROLL=1 -DRUNG_HALF=0 -DRRAMP=1
```

**The port is that file with the `#if` branches resolved**, written in the
style of today's `DiodeLadder.c`:
- state loaded into locals and written back once;
- no switch in the loop;
- one `__SSAT` per sample.

Its measured cost is **230 instructions per sample, 3 VDIV**: standalone
`-Ofast`, `run_variants.py`'s loop count, which reproduces S093 P0's
figures.

| Property | Value |
|---|---|
| Ladder rungs | All four nonlinear, secants by the [3/2] Padé `t = (27 + z²)/(27 + 9z²)`, **not clamped** |
| Input pair (Q12) | [5/4] Padé `t = (945 + 105z² + z⁴)/(945 + 420z² + 15z⁴)`, argument clamped to ±3.5, **with the tail**: the denominator × max(\|x_in\|, 3.5)/3.5 (S093 P1 §2.1) |
| Q21 output stage | **Linear**: tp_pre = −A_OUT · v_hp/(2 Vt). No secant; `at = KA`, `bt = KB · zo` |
| OTA | [3/2] Padé, argument clamped to ±3; one sample late (it shares the batch) |
| Network | **Form N1** (§4): no per-sample fast modes. Per sample: D terms and one asymptote one-pole per output (u at 50 Hz, v at 120 Hz). Per block: all eight states |
| s-asymptote (`gau1`) | Dropped per sample (also from the block copy z8, so the block system carries it) |
| Divisions | VDIV 1: the four continuants' shared reciprocal. VDIV 2: the loop closure. VDIV 3: the batched secants (rungs, input pair, OTA) |
| Output | int16 at **7 V full scale** (`DL_OUT_SCALE = 32767/7`) |
| Accuracy | LSD60 against the golden model, acid ×1/×4/×8: 0.19/0.69/0.64 dB; lowres ×1/×4: 0.17/0.57. Ring 2.018 ms / 0.372 (golden 2.018 / 0.364) |

### 3.2 Per-sample pseudo-code (B-R)

Per block on entry:
- ramps: `di = (iEnd − i)/32`, `dia = (iAbcEnd − ia)/32`,
  `dru = (rU − rUPrev)/32`, `drv = (rV − rVPrev)/32`;
- start values `ru = rUPrev`, `rv = rVPrev`;
- `sScale` already includes the drive.

```
i += di;  ia += dia;  ru += dru;  rv += drv
s  = buf[j] * sScale
w0 = K0*i;  w1 = K1*i
d01 = x0-x1;  d12 = x1-x2;  d23 = x2-x3
a0 = t0*d01;  a1 = t1*d12;  a2 = t2*d23;  a3 = t3*x3
r0 = x0 - w0*(a0 + tin*xinL)
r1 = x1 + w1*(a0 - a1);   r2 = x2 + w1*(a1 - a2)
r3 = x3 + w1*(a2 - a3) + KL*(KG*zo - vhpL)
e0 = w0*t0;  e1 = w1*t0;  e2 = w1*t1;  e3 = w1*t2
th1 = 1+e0
th2 = (1+e1+e2)*th1 - e1*e0
th3 = (1+e2+e3)*th2 - e2*e2*th1
th4 = (1+e3+w1*t3+D3X)*th3 - e3*e3*th2
p12 = th1*th2;  p123 = p12*th3
inv = 1/(p123*th4)                                        // VDIV 1
i123 = inv*th4;  i12 = i123*th3
ip0 = i12*th2;  ip1 = i12*th1*th1;  ip2 = i123*p12*th2;  ip3 = inv*p123*th3
m1 = e1*ip0;  m2 = e2*ip1;  m3 = e3*ip2
y1 = r1 + m1*r0;  y2 = r2 + m2*y1;  y3 = r3 + m3*y2
q0 = -w0*tin;  q1 = m1*q0;  q2 = m2*q1;  q3 = m3*q2
pr3 = y3*ip3;  pq3 = q3*ip3
quk  = ALU*qu + BEU*gau0*tpL
base = ru + quk + ku1*s
xin  = (base + ku0*(KA*pr3 + KB*zo)) / (2Vt - ku0*KA*pq3)    // VDIV 2
n3 = pr3 + pq3*xin
n2 = (y2 + q2*xin + e3*n3)*ip2
n1 = (y1 + q1*xin + e2*n2)*ip1
n0 = (r0 + q0*xin + e0*n1)*ip0
vhp = (2Vt*n3 - zo)*KG;   zo += TWO_G*vhp
// secants
zE = clamp(xin, ±3.5);  sE = zE²;  nE = 945 + sE*(105 + sE)
dE = (945 + sE*(420 + 15*sE)) * (max(|xin|, 3.5) / 3.5)
zG = clamp(vcaL/2Vt, ±3);  sG = zG²;  nG = 27 + sG;  dG = 27 + 9*sG
zA = n0-n1; zB = n1-n2; zC = n2-n3; zD = n3        // unclamped
nX = 27 + zX²;  dX = 27 + 9*zX²                    // X = A..D
pAB = dA*dB;  pABC = pAB*dC;  pABCD = pABC*dD;  prod = dE*dG
r = 1/(pABCD*prod)                                  // VDIV 3
iP = r*pABCD;   r *= prod                           // iP = 1/(dE dG); r = 1/pABCD
t3 = nD*r*pABC;  r *= dD
t2 = nC*r*pAB;   r *= dC
t1 = nB*r*dA
t0 = nA*r*dB
tin  = nE*iP*dG
tota = nG*iP*dE
// output stage, network, OTA
tp  = -A_OUT * vhp/2Vt                              // linear Q21
qu  = quk + BEU*gau0*tp
qv  = ALV*qv + BEV*gav0*(tpL + tp)
vca = rv + qv + dv0*tp
out = R_OTA * iaL * zG * tota                       // the OTA, one sample late
buf[j] = SSAT16(out * (32767/7))
accTp += tp;  accS += s
x0..x3 = n0..n3;  tpL = tp;  xinL = xin;  vhpL = vhp;  vcaL = vca;  iaL = ia
```

Per block on exit:
- write the state back;
- `slowUpdate(accTp/32, accS/32)` (§4.3).

**Initial state** (`DiodeLadder_init`): everything 0, except t0–t3,
tin and tota, which are 1.

### 3.3 Constants (float literals in `DiodeLadder.c`)

Derivations are in S093 P0 §3.2 and S092 P6 §3.

| Name | Value | Note |
|---|---|---|
| `K0`, `K1` | 6090.59326, 3322.14185 | (h/2)/(4 Vt C18), (h/2)/(4 Vt C19) |
| `KG`, `TWO_G` | 0.995162427, 0.00972212479 | C27/C25 TPT one-pole |
| `KL`, `D3X` | 0.142460123, 0.00732955895 | Term L |
| `KA`, `KB`, `A_OUT` | −5.33020973, 103.098831, 5.35612011 | Q21 |
| `R_OTA` | 52525.9531 | OTA load |
| `2Vt`, `1/2Vt` | 0.0517, 19.342359 | Vt = 25.85 mV |
| `ALU`, `BEU`, `DECAY_U` | 0.992902756, 1.12955859e-05, 0.796187937 | N1 loop asymptote, 50 Hz (`variants/dlv_n1.h`) |
| `ALV`, `BEV`, `DECAY_V` | 0.983050883, 1.12397456e-05, 0.578678608 | N1 VCA asymptote, 120 Hz |
| `DL_OUT_SCALE` | 32767/7 | Output full scale 7 V (S093 P1 D17) |

The generator must assert `DECAY_U`/`DECAY_V` against the network it
writes, as it does today for the 5 Hz decay (compare as float32).

### 3.4 A-R (`DiodeLadder_renderA`, the fallback, kept measurable)

Reference flags: `-DMODEL=0 -DXIN_TAIL=1 -DGAU1=0 -DNET1=1 -DQ21_LIN=1`
(OTA [5/4]). Cost 206 + 2 VDIV.
- The rungs are linear, so the next sample's continuants share the secant
  batch, and the first sample's pivots are computed once per block (see
  `dlv_kernel.c` `pivots()`).
- Not called by `AlkalineVoice`; it is kept so the class decision can be
  revisited.
- LTO's `--gc-sections` drops it from the image while unreferenced.

---

## 4. Tables: network form N1 (`gen_alk_tables.py`)

### 4.1 Generator change

- Build every Res point with `SplitNet` configured as N1:
  `F_SPLIT = 1e9` (no fast modes), `A_ASYM_U = 2π·50`, `A_ASYM_V = 2π·120`.
  `alk_model.SplitNet` already supports per-output poles (S093 P1).
- Today's `gen_n1.py` shows the configuration and the assertions; fold it
  into `gen_alk_tables.py` and delete the variant copy, or keep the variant
  for the experiments.
- `--check` must still prove the committed files.

### 4.2 Layout (drop the fast-mode fields)

```c
typedef struct {
	float phi[8][8];   /* slow block system, physical basis */
	float gam[10][2];  /* block input gains (tp_pre mean, s mean); rows 8, 9 = asymptote copies */
	float cs[2][8];    /* slow outputs u, v */
	float ga[2][2];    /* asymptote gains [u|v][tp|s] */
	float d[2][2];     /* instantaneous gains [u|v][tp|s] */
} DiodeLadderNet;      /* 108 floats, 432 B; 9 points = 3,888 B */
```

Per-block `DiodeLadder_setNet()`, with f the interpolation fraction and
g = 1 − f:

```
gau0 = lerp(ga[0][0]);  gav0 = lerp(ga[1][0]);  dv0 = lerp(d[1][0])
ku0  = lerp(d[0][0]) + BEU*gau0
ku1  = lerp(d[0][1])                    /* gau1 dropped */
```

### 4.3 The slow update (per block)

```
for r in 0..7:  zn[r] = g*(phiLo[r]·z + gamLo[r]·(m0, m1)) + f*(phiHi[r]·z + gamHi[r]·(m0, m1))
z8 = DECAY_U*z8 + g*gamLo[8][0]*m0 + f*gamHi[8][0]*m0        /* no s input (gau1 dropped) */
z9 = DECAY_V*z9 + g*(gamLo[9]·(m0, m1)) + f*(gamHi[9]·(m0, m1))
rUPrev = rU;  rVPrev = rV
rU = lerp(cs[0])·zn − z8;   rV = lerp(cs[1])·zn − z9;   z[0..7] = zn
```

---

## 5. `AlkalineVoice` changes

| Item | Change |
|---|---|
| Trigger | `Alkaline_triggerVoice(voice, source_slot, velocity, note, stepSamples)`. Add the platform part, as `Drum_triggerVoice()` does (`DrumVoice.c` ≈ l. 241–254): `lfo_retrigger(source_slot)`, `modNode_updateValue(&velocityModulators[source_slot], velocity/127.f)`, `instrumentManager_applyVelocityModulationTarget(source_slot, velocity/127.f)`. The rest stays as today (S093 P0 §4.2): accent ≥ 100, legato, slide latch, gate in samples |
| Drive | New member `float drive` (0..1, TYPE_FLT). In `calcVoiceAsync`: `voice->driveGain = exp2f(3.0f * voice->drive)` (×1..×8, D16). In the render: `sScale × driveGain` |
| Render | `DiodeLadder_renderB(&ladder, buf, sScale * driveGain, iLadEnd, iAbcEnd)` |
| Quiet query | `uint8_t Alkaline_isQuiet(const AlkalineVoice *v)`: `gateLeft == 0 && !accentOn && iAbcEnd < 1e-7f` (for `instrumentManager_ampEnvelopeQuiet()`, which otherwise returns 1 for a type without a `SlopeEg2`) |
| `noinline` | `Alkaline_calcVoiceSyncBlock()` gets `__attribute__((noinline))` (S092 P0 §1.3: the LTO risk into `mixer_calcNextSampleBlock()`) |
| Struct | `DiodeLadder` loses y0/y1, tout and the fast-mode coefficients, and gains nothing (it was 220 B). `AlkalineVoice` stays about 500 B, inside the 1,264 B slot. Add `_Static_assert(sizeof(AlkalineVoice) <= INSTRUMENT_RUNTIME_SLOT_BYTES)` at integration |

---

## 6. Parameters and menu (`AlkalineParameters.c/.h`)

Pattern: `DrumParameters.c`, with its row macros: `ROW`, `ROW_SPECIAL`,
`ROW_MENU`, `ROW_SELECT_ONOFF_SPECIAL`, `ROW_NOBIND`, `ROW_NOBIND_IMAGE`.
- `BIND` uses `offsetof(AlkalineVoice, …)`.
- Each row on one line (for `check_special_tags.py`).
- `ALKALINE_PARAM_DESCRIPTOR_COUNT` must equal the enum count
  (`_Static_assert`).
- `ALKALINE_INSTRUMENT_TYPE_FLAGS INSTRUMENT_FLAG_BASIC`; display label
  `"Alkaline"`.

| # | Key | Short / long, category | dtype, mod | Binding |
|---:|---|---|---|---|
| 0 | `alk_wave` | `wav` / "Waveform", "303" | ON/OFF tag `ONOFF_TAG_ALK_WAVE` (new, value 3; names `{2}, "Saw", "Sqr"`); selector, MOD_NONE | `ROW_SELECT_ONOFF_SPECIAL(..., wave, TYPE_UINT8, IM_SPECIAL_NONE)` |
| 1 | `osc1_pitch_coarse` | `coa` / "Coarse", "303" | `DTYPE_0B127`, MOD_0_127 | `ROW_SPECIAL(..., osc.modNodeValue, TYPE_SPECIAL_F, IM_SPECIAL_PITCH_COARSE \| IM_SPECIAL_OSC1)` (default 36 = C2) |
| 2 | `alk_tuning` | `tun` / "Tuning", "303" | `DTYPE_PM63`, MOD_PM63 | `ROW(..., tuning, TYPE_FLT)`; ±700 cents at control rate (no new writer) |
| 3 | `alk_slide` | `sld` / "Slide", "303" | `DTYPE_ON_OFF`, MOD_NONE | `ROW(..., slide, TYPE_UINT8)` |
| 4 | `alk_cutoff` | `cut` / "Cutoff", "VCF" | `DTYPE_0B127`, MOD_0_127 | `ROW(..., cutoff, TYPE_FLT)` |
| 5 | `alk_reso` | `res` / "Resnance", "VCF" | as 4 | `ROW(..., reso, TYPE_FLT)` |
| 6 | `alk_envmod` | `env` / "Env Mod", "VCF" | as 4 | `ROW(..., envMod, TYPE_FLT)` |
| 7 | `alk_decay` | `dec` / "Decay", "VCF" | as 4 | `ROW(..., decay, TYPE_FLT)` |
| 8 | `alk_accent` | `acc` / "Accent", "Voice" | as 4 | `ROW(..., accent, TYPE_FLT)` |
| 9 | `alk_drive` | `drv` / "Drive", "VCF" | as 4 | `ROW(..., drive, TYPE_FLT)` (×1..×8) |
| 10 | `instrument_vol` | `vol` | as Drum | `ROW(..., vol, TYPE_FLT)` |
| 11 | `instrument_pan` | `pan` | as Drum | `ROW(..., pan, TYPE_UINT8)` |
| 12–19 | `lfo_rate`, `lfo_amount`, `lfo_amount_2`, `lfo_wave`, `lfo_retrigger_voice`, `lfo_polarity`, `lfo_sync`, `lfo_offset` | as Drum | as Drum | as Drum, on `lfo.*` |
| 20–21 | `velo_mod_amount`, `velo_mod_dest` | as Drum | as Drum | `ROW_NOBIND_IMAGE` / `ROW_NOBIND` as Drum (no `velo_vol_on_off`: the 303's velocity is the accent) |
| 22–25 | `lfo_target_voice`, `lfo_target_param`, `lfo_target_voice_2`, `lfo_target_param_2` | as Drum | as Drum | `ROW_NOBIND` as Drum |

- **Not offered** (S092 P0 §5.5): `srt` (D4, no SRR), `run`, `chn`, `pos`,
  `typ`, fine pitch.
- **Menu pages** (16 cells per page, four-cell screens, padded with
  `INSTRUMENT_MENU_EMPTY`):
  - `303`: coa tun wav sld;
  - `VCF`: cut res env dec, then drv;
  - `Voice`: acc vol pan;
  - LFO page as Drum;
  - velocity: dst amt.
- **New menu text:** `alkWaveNames` in `MenuText.h`. Add a branch in
  `menu_onoffTagNames()` (`menu.c` ≈ l. 12340, which today maps every tag
  other than VOICE_CHAIN to `oscRunNames`). Check the other `DTYPE_ON_OFF`
  tag sites in `menu.c`: S088 lists six.
- **`check_special_tags.py`:** add
  `'ALK': 'Core/DSP/Instruments/Alkaline/AlkalineParameters.c'` to `TABLES`;
  the coarse row must classify as `PITCH_COARSE | OSC1`.

---

## 7. Integration (InstrumentManager and the rest)

Line numbers are from the S093 tree and will shift.

### 7.1 `InstrumentManager.h/.c`

| Site | Change |
|---|---|
| `InstrumentManager.h` ≈ l. 35 | `INSTRUMENT_TYPE_ALK` before `INSTRUMENT_TYPE_UNKNOWN`. UNKNOWN moves from 4 to 5: check stored bytes that might hold it (S092 P0 §5.5) |
| `instrument_registry[]` ≈ l. 35 | `{ INSTRUMENT_TYPE_ALK, "alk", alkaline_instrument_display_label, ".alk", "Alkaline", ALKALINE_INSTRUMENT_TYPE_FLAGS, alkaline_param_descriptors, ALKALINE_PARAM_DESCRIPTOR_COUNT, alkaline_menu_pages, ALKALINE_MENU_PAGE_COUNT }` |
| Runtime union ≈ l. 171 | `AlkalineVoice alkaline;` + `_Static_assert` + `instrumentManager_alkalineRuntime(slot)` |
| `instrumentManager_runtimeInstance` (1408) | Return `instrumentManager_alkalineRuntime(slot)` |
| `instrumentManager_runtimeLfo` (1425) | `&voice->lfo` |
| `instrumentManager_runtimeInit` (1493), `resetRuntimeSlotFromScene` (1624) | `Alkaline_initVoice()` |
| `instrumentManager_ampEnvelopeQuiet` (1502) | Branch for ALK: `Alkaline_isQuiet()` |
| `instrumentManager_runtimePan` (1739), `runtimeVolume` (1768) | `voice->pan`, `voice->vol` |
| `instrumentManager_slotRunMode` (1801), `slotVoiceChain` (1829), `slotHasActiveItpFade` (1919) | Default (0) |
| `instrumentManager_recalcSlotFilter` (1881) | No-op (no ZDF filter) |
| `instrumentManager_calcSlotAsync` (1990) | `Alkaline_calcVoiceAsync()` |
| `instrumentManager_calcSlotSyncBlock` (2024) | `Alkaline_calcVoiceSyncBlock(..., buf, size)` |
| `instrumentManager_slotUsesSrr` (2047) | Not listed, so it returns 0: no SRR (D4) |
| `instrumentManager_triggerTrack` (2149) | `Alkaline_triggerVoice(voice, slot, velocity, note, seq_trackStepSamples(trigger_track))` |
| `instrumentManager_oscBySelector` (2311) | `IM_SPECIAL_OSC1` → `&voice->osc` (the coarse writer) |
| `_filter` (2335), `_ampEg` (2355), `_distortion` (2389), `_transient` (2409) | Return 0 (no such stage) |
| `instrumentManager_applySlot6AlternateDecay` | No change: it finds no `amp_envelope_decay` row, so track 7's alternate has no effect on Alkaline in this prototype (S092 P0 §5.5 mapping to MEG decay deferred) |

### 7.2 Other files

| File | Change |
|---|---|
| `Core/Sequencer/sequencer.c/.h` | New read-only `uint32_t seq_trackStepSamples(uint8_t track)` = `stepScale_ticksQ8(seq_effectiveTrackScale[track]) × 44108 × 60 / (256 × 96 × seq_getBpm())` (96 PPQ, Q8.8 ticks; a 16th = 24 ticks). Clamp the BPM ≥ 1; track ≥ `NUM_TRACKS` → use the default scale. Never writes Pattern data (MEMORY.md S078). Check that `seq_tempo` follows external clock (S092 P0 §5.4) |
| `Core/Menu/MenuText.h`, `menu.c` | `ONOFF_TAG_ALK_WAVE`, `alkWaveNames`, the tag-name branch (§6) |
| `Core/Menu/menu.c` l. 1728 | `menu_instrumentLoadIndex[INSTRUMENT_TYPE_UNKNOWN]` grows by 1 entry: **+2 B `.bss` (needs sign-off, §9)** |
| `SceneData.c` 1128, `filesystem.c` 18242 | The default Scene stays Drum/Snare/Cymbal/HiHat (no change) |
| `storageTypes.h` 141 | `#define STORAGE_INSTRUMENT_ALK INSTRUMENT_TYPE_ALK` |
| `Makefile` | `-ICore/DSP/Instruments/Alkaline`. `DSP_SRCS` gets `Core/DSPAudio/DiodeLadder.c`, `Core/DSP/Instruments/Alkaline/AlkalineVoice.c`, `AlkalineControl.c`, `AlkalineTables.c`, `AlkalineSquareTable.c`, with explicit `-Ofast` rules for the three non-`DSPAudio` `.c` files (as `DrumVoice.o`). `AlkalineParameters.c` goes in `SRCS` |
| `MIDI_NRPN_TABLE.md`, `INSTRUMENTS_DSP_REFERENCE.md` §3.1/§3.2/§5/§8, `FILESYSTEM_SPEC.md` (keys) | Documentation |

---

## 8. Budgets

| Resource | Figure |
|---|---|
| **CPU per sample** | B-R 230 + oscillator 24 = **254**, 3 VDIV, against Drum ≈ 257, 4 VDIV (S093 P0 §6.5; S093 P1 §6). This is a 3-instruction margin on standalone figures: the linked count decides (§10) |
| CPU per block | Unchanged from S093 P0 §6.2 (about 600–800 operations: libm calls in the MEG/expo, the four accent solves, the slow network) |
| Flash | `DiodeLadder.o` about 4.5 KB (both kernels); `AlkalineControl.o` 3,200; `AlkalineVoice.o` about 0.9 KB; N1 tables 3,888 + VEG and prewarp about 300; square table 22,528; parameters and menu about 2–3 KB. **About 37–38 KB against 40,980 B headroom**: without P2-0 only about 3 KB would remain, well below the 16 KiB `link_budget` warning. **With P2-0 (Re3/Sw3 removed), about 48 KB remain** |
| RAM | `AlkalineVoice` about 500 B inside the existing slot union (no growth). `menu_instrumentLoadIndex` +2 B (sign-off) |
| ITCM | 0 (the oscillator reuses the existing ITCM table loop; the ladder runs from flash) |

---

## 9. Decisions to confirm at the start of the session

| # | Item | Proposed |
|---|---|---|
| R1 | `menu_instrumentLoadIndex` +2 B, `.bss`, SRAM1, owner `menu.c` | Approve (the only new RAM) |
| D14 | Re3/Sw3 removal renumbers user samples (S093 P0 §10.2) | Accept (no migration) |
| D16 | Drive mapping ×1..×8 as `8^(v/127)`, default 0 (×1) | Accept |
| P2-0 order | Re3/Sw3 removal before the Alkaline integration | Yes (flash) |
| D1 | Accent at velocity ≥ 100 | Already decided by the user (S092) |

---

## 10. Verification and gates

1. **Bench** (`make -C tools/dsp_test alkaline alkaline_tables special_tags oscillator`):
   - **AL-R:** ring 2.0 ms ± 3 %, decay 0.364 ± 0.03 (B-R gives 0.372).
   - **AL-AB:** A-R against B-R within 0.3 dB at ×1.
   - **New AL-D (drive):** at ×8, bounded, no NaN, output within full
     scale, the input-pair tail active.
   - **AL-S:** unchanged.
   - **Break build** kept.
   - `make all` stops at `run_modes` today (S093 P0 O7): run the targets
     individually, or fix O7 first.
2. **`tools/alkaline/check_c.py`:**
   - C1 against SPICE and the Python B_rt: B-R is N1, so compare with
     `run_variants.py`'s `B_n1rq_ota32` instead (LSD60 against golden
     ≈ 0.19 at ×1).
   - C2 pattern within S093 P0 §5.4's figures ± 0.3 dB.
   - Add drive cases ×4/×8.
3. **ARM:**
   - `fpseq.py` (or `run_variants.py`'s loop counter) on `DiodeLadder.o`:
     `DiodeLadder_renderB` ≈ 230, 3 VDIV.
   - After `make all`: `elf_looppath.py --elf build/lxr02.elf --sym DiodeLadder_renderB`
     (the linked count).
4. **Mixer:** `make -C tools/dsp_test elf86_mixer_check` and
   `elf_hotloops.py`: 0 loops grew in the other engines (Alkaline's render
   is `noinline`).
5. **Flash:** `make all` runs `link_budget.py`; record the headroom.
6. **Device:**
   - An Alkaline track plays the S092 P4 pattern. Compare with
     `tools/alkaline/alk_c_acid_B.wav` (the S093 P0 render; B-R differs by
     about 0.1–0.2 dB).
   - Worst-case Scene with Alkaline in place of the dearest Drum: the `cpu`
     widget ≤ baseline and 0 underruns (S092 P0 §1.3).
   - Listen at drive ×1/×4/×8.

---

## 11. After the listening trial (the user's options)

| Path | What it means |
|---|---|
| Reduce further (stay Basic, gain margin) | Remaining candidates: rung secants every second sample (−3.5 in B-R, accuracy cost at low Res); the A-R form (206) with weaker drive |
| Advanced (replace Cymbal/HiHat in a Scene) | B-R without the [3/2] OTA (`B_n1rq`, 237 + 24 = 261 against ≈ 270) leaves about 9 instructions and 1 VDIV. Post-distortion/FX types would need their cost inside that, or a cheaper B-R. E1 (post-VCA overdrive with the shared `distortion_curveSampleF()`, ≈ +22–25 and +1 VDIV, S092 P0 §8) on the 254 form gives ≈ 277–279: about 9 over the Advanced envelope (≈ 270) and about 22 over Basic (≈ 257), so E1 needs further reductions in either class |
