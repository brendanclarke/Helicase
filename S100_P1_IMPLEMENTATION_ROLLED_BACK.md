# S100 P1 — Implementation Schedule: Space degradation without idle hiss, D2, `hld`

**Date:** 2026-11-20. **Branch:** `dev-ph7-alkaline`. **Line numbers:** as
of `c930dcf5`. They are still valid at `6dc13b69` (session 099 doc
closeout), which changes none of the files scheduled here.
**Source:** `S100_P1_BG_NOISE_INVESTIGATION.md` §4 and §7.

**Decisions (user, 2026-11-20):**
- **A:** the degradation noise becomes modulation noise. It multiplies the
  loop signal, so no signal means no noise.
- **D2:** `deg` no longer raises the loop gain (makeup = 1/drive).
- **H1:** less feedback while `hld` is on, because the held loop rises too
  quickly.

**Superseded in part (2026-11-21, §12, S100 P1b):** D2 is withdrawn because
it stopped high feedback building to saturation; the `hld` law is replaced
(`fbk` now sets decay / hold / rise). §1–§11 are the record of S100 P1 as
scheduled and reviewed. The code in the tree is §12's.

**Status (as scheduled):** schedule only; nothing in the tree has changed. Every change
below was applied verbatim to a scratch worktree of `c930dcf5` and built,
gated and measured there (§2). The worktree has since been removed. Comment
text in this document may be tidied when it is pasted; the code is exactly
what was measured.

---

## 0. Summary

| ID | File | Line(s) | Action | What |
|---|---|---|---|---|
| S1 | `Core/DSP/Effects/Space/SpaceEffect.c` | after 70 | add | `SPACE_HOLD_T60_S`, `SPACE_HOLD_K` |
| S2 | same | 91–94 | modify | `space_tapeK_t` description (D2, A, `hold`) |
| S3 | same | after 109 | add | field `float hold;` in `space_tapeK_t` |
| S4 | same | after 226 | add | `space_holdGain()` |
| S5 | same | 351 | modify | `space_loop()` description |
| S6 | same | 364–368 | modify | **A:** `* makeup + nz` → `* (makeup + nz)` |
| S7 | same | 1227–1236 | modify | **D2 + A laws:** makeup = 1/drive; hiss relative |
| S8 | same | after 1250 | add | `hed` law calls `space_holdGain()` |
| S9 | same | 1567–1569 | modify | `space_tapeBegin()` description |
| S10 | same | after 1593 | add | `tim`/tempo branch calls `space_holdGain()` |
| S11 | same | 1645 | modify | **H1:** `hld` plays `k.hold` (was `1.0f`) |
| S12 | `Core/DSP/Effects/Space/SpaceParameters.h` | 45 | modify | `SPACE_DLY_DEGRADE` comment |
| S13 | same | 47 | modify | `SPACE_DLY_HOLD` comment |
| T1 | `tools/dsp_test/modules/space.c` | after 86 | add | law list: SP12, SP13 |
| T2 | same | after 689 | add | `sp12_run()`, `law_sp12()`, `sp13_run()`, `law_sp13()` |
| T3 | same | after 703 | add | `law_sp12(); law_sp13();` in `main()` |
| M1 | `tools/dsp_test/Makefile` | after 1659 | add | break builds `space_break_hiss` (SP12), `space_break_hold` (SP13) |

**Apply order:** within each file, apply from the highest line number to the
lowest (S11 → S1, T3 → T1), so the cited line numbers stay valid. Each change
also quotes its anchor text, so it can be found by search.

**Not changed:**
- `SpaceRuntime` (`SpaceEffect.h`);
- the Echo's rows: it has no `hld` and shares S6/S7;
- the Reverb;
- the menu labels, which show only "Degrade" and "Hold"
  ([MenuLabels.c:178](Core/Menu/MenuLabels.c#L178));
- any stored format. Stored `deg` and `hld` bytes keep their keys and
  change meaning, with no migration (standing rule).

---

## 1. What changes in the sound

### 1.1 A: modulation noise (S6, S7)

- **Mechanism:** the noise term multiplies the stage's gain:
  `y·(1 − y²/3)·(makeup + nz)`. With `y` = 0 the noise is exactly 0, so an
  empty tape stays exact zero.
- **Level law:** `hiss = makeup · 10^((−57 + 24g)/20) / 2³¹`. The relative
  jitter peak is −57 dB at `deg` 1 and −33 dB at `deg` 127. Per-pass RMS S/N
  is 61.6 dB at `deg` 1 and 37.8 dB at 127. This equals the S095 absolute
  hiss under a −15 dBFS tape.
- **Character:** tape modulation noise riding on the repeats. On low, sparse
  repeats it follows the waveform (zero at each zero crossing). It tracks
  `ton` at both ends: its level follows the loop signal after the LP and the
  HP.
- **Tuning (ear):** the constant −57 can be moved by ear.

### 1.2 D2: `deg` keeps unity at `fbk` row 100 (S7)

- **Mechanism:** `makeup = 1/drive` (was `1/(1 + 2g)`).
- **Before:** the small-signal loop gain was `fbk · (1 + 5g)/(1 + 2g)`, up
  to 2× `fbk`.
- **After:** it is `space_fbkGain(fbk)` at every `deg`.
- **Effect on repeats:** each repeat loses the factor `(1 + 2g)/(1 + 5g)` it
  used to gain: −2.3 dB at `deg` 16, −4.9 dB at 64, −6.0 dB at 127. So
  tails with `deg` up are shorter than today at the same `fbk`.
- **Clip ceiling:** the clipped ceiling becomes `(2/3)/drive`, −19.1 dBFS at
  `deg` 127 (was −13.1).

### 1.3 H1: `hld` (S1, S3, S4, S8, S10, S11)

**Mechanism.**
- Today `hld` sets the loop gain to exactly 1.0 and cuts the input.
- It now sets the loop gain to `k.hold`:
  `k.hold = (1 − x)/((1 + x)·S)`.
- `x = SPACE_HOLD_K · P`, where P is the pass of the shortest active head
  (`tim` × 1/3 with head A, × 2/3 with B, × 1 for C alone).
- S is the heads' sum: 1, √2 or √3.
- The result is a decay of **60 dB in 30 s** (`SPACE_HOLD_T60_S`): −2 dB per
  second at low frequency, the same at every `tim` and every head.

**Why each term.**
- **Why S:** the heads are power-normalised (1/√n each), so where they add
  in phase (multiples of 3·fs/D) a multi-head loop's gain was √n. That was
  the rise.
- **Why D2 matters here:** D2 removes the other rise, `deg`'s ×2.
- **Why the shortest head:** a head-A loop passes three times as often as
  head C, so its gain per pass must be that of a third of the time.

**Measured (host, Delay `tim` 40 = 3,899 samples).** "Held fall" is the
energy drop from held second 1 to held second 4 (3 s apart). Negative means
the hold **rose**.

| `hed` | Today: noise `deg` 0 / 127 | Today: 200 Hz `deg` 0 / 127 | S100: noise `deg` 0 / 127 | S100: 200 Hz `deg` 0 / 127 |
|---|---|---|---|---|
| A | 9.9 / **−1.1** | 1.60 / **−0.60** | 19.8 / 40.2 | 8.04 / 9.98 |
| B | 6.3 / **−2.4** | 0.76 / **−1.30** | 13.6 / 19.1 | 6.76 / 7.91 |
| C (default) | 6.6 / **−4.2** | 0.49 / **−2.13** | 13.5 / 17.1 | 6.53 / 7.30 |
| AB | **−5.8** / **−1.1** | **−3.15** / **−0.61** | 15.8 / 22.0 | 5.12 / 7.25 |
| AC | **−10.6** / **−1.6** | **−5.03** / **−0.81** | 12.2 / 18.1 | 3.86 / 5.46 |
| BC | **−17.7** / **−2.2** | **−8.14** / **−1.14** | 13.9 / 22.6 | 5.52 / 6.76 |
| ABC | **−4.2** / **−1.4** | **−2.48** / **−0.70** | 12.8 / 19.0 | 3.82 / 5.91 |

Reading the table:
- **Today:** every multi-head stop rises, and every stop rises at `deg` 127.
  At `deg` 127 the clip caps the rise, which is why it is small.
- **S100:** every stop falls.
  - 200 Hz on a single head falls by the designed 6 dB plus the loop's own
    per-pass losses.
  - Broadband noise falls further because the loop's low-passes remove its
    high end on every pass. That also happens today: head C at `deg` 0 falls
    6.6 dB with unity hold.
  - With unity hold restored but D2 and A in, multi-head stops still rise by
    1–18 dB over 3 s. Both H1 and D2 are needed.

**Tuning (ear):** `SPACE_HOLD_T60_S` 30 s. Longer gives a longer hold.

---

## 2. Verification record (scratch worktree, all from `c930dcf5`)

| Gate | Today | With S1–S13, T1–T3, M1 | Cap / rule |
|---|---|---|---|
| `space_cpu.py` Delay | 251.6 (feed 271.6) | **250.8** (270.8) | 252.0 (272.0) |
| `space_cpu.py` Echo | 210.0 (feed 229.9) | **210.1** (230.1) | 210.5 (230.5) |
| `space_cpu.py` Reverb | 189.5 | 189.5 | 191.8 |
| Delay loop `space_delayRun` | 236 / frame | 235 / frame | — |
| Echo loop `space_echoTapeRun` | 110 / frame | 110 / frame | — |
| `make -C tools/dsp_test space` | 24 ok, 0 failures | 0 failures (24 + 5 SP12 + 4 SP13 lines) | all pass |
| Break `space_break_hiss` | — | caught (SP12 fails, 5 lines) | must fail |
| Break `space_break_hold` | — | caught (SP13 fails, 4 lines) | must fail |
| Breaks `lin1`, `lin2` | caught | caught | must fail |
| `make all` flash | 730,936 B | **731,080 B (+144 B)** | headroom 22,584 B |
| ITCM / DTCM statics / FXBUF | 15,432 / 4,396 / 126,656 | unchanged | — |
| `mixer_calcNextSampleBlock` loops (`elf_loopdump.py --compare`) | — | 68 / 68 exact | inlined set unchanged |
| `elf_s099_check` | — | 15 / 15 exact | — |
| `elf_p4_check` | fails: 15 missing osc loops | **identical failure** | pre-existing: stale S094 P4 baseline, unrelated to Space |

Notes:
- **Echo +0.1:** the per-block path grows by 5 instructions (797.25 vs
  792.25 per block): the `k.hold` load in `space_tapeBegin()`, which the
  Echo shares. That leaves 0.4 under the cap.
- **Delay −0.8:** S6 removes one instruction per frame from the loop, and
  the per-block path grows by about 6.
- **`space_holdGain()`** runs only on a `hed` row change and in the
  `tim`/tempo branch. `space_cpu.py` leaves that branch out of the steady
  total.
- **CPU ceiling** (`MEMORY.md`, S089 P2): no per-sample cost increases on
  the Delay. The Echo's +0.1 is per-block work inside its cap. The rule says
  a new cost must be paid for by an equal or larger measured saving, so this
  needs your judgement: the Delay's −0.8 is in the same Effect slot's
  budget.
- **RAM sign-off needed (`MEMORY.md`, RAM and CPU Budget):** +4 B,
  `space_tapeK_t.hold`, inside the existing 448 B `SK_STATE_BYTES` arena
  prefix in the FX arena (DTCM). `space_hdr_t` goes from 392 to 396 B
  (sizes from the compiler). The prefix does not grow and nothing new is
  allocated. Owner: `SpaceEffect.c`.

---

## 3. Change schedule — `Core/DSP/Effects/Space/SpaceEffect.c`

### S11 — line 1645, modify: `hld` plays `k.hold`

Anchor (remove):

```c
    ps->fbk = (rt->flags & SPACE_F_HOLD) ? 1.0f : h->k.fbk;
```

Replace with:

```c
    /*
     * S100 P1 H1: `hld` plays k.hold (space_holdGain(): a 30 s decay divided
     * by the heads' sum), never above unity at any frequency the heads can
     * add in phase; it was 1.0, which rose with a multi-head stop. The input
     * is still cut (ing 0, D10).
     */
    ps->fbk = (rt->flags & SPACE_F_HOLD) ? h->k.hold : h->k.fbk;
```

- **Inputs:** `rt->flags & SPACE_F_HOLD` (the `hld` row, `SPACE_LAW_HOLD`),
  `h->k.hold` (S4), `h->k.fbk`.
- **Output:** `ps->fbk`, read by `space_loop()` through `space_delayRun()`
  and `space_echoTapeRun()`.
- **Unchanged:** line 1646 (`ps->ing`) stays as it is.

### S10 — after line 1593, add: recompute the hold gain with the pass time

Anchor (keep):

```c
        rt->t_samples = crumpBit_delaySamples(rt->t_row, bpm);
    }
```

Insert between the two lines:

```c
        /*
         * S100 P1 H1: the `hld` gain follows the pass time, so it is
         * recomputed with it (only when `tim` or the tempo moved; one
         * division, outside the steady per-block path).
         */
        space_holdGain(rt, &h->k);
```

- **Inputs:** `rt->t_samples` (just computed), `h->k.hg[]`.
- **Output:** `h->k.hold`.
- **Runs:** on the first block after `space_enter()` (`t_bpm` starts at 0),
  then on every `tim` row or tempo change.

### S9 — lines 1567–1569, modify: `space_tapeBegin()` description

Anchor (remove):

```c
 * 2. the heads' gains (O6) x the valid gate; the loop's gains (`hld`: no
 *    input, unity loop, D10); the record LP and the playback bandwidth at
 *    the tape speed;
```

Replace with:

```c
 * 2. the heads' gains (O6) x the valid gate; the loop's gains (`hld`: no
 *    input, the loop at k.hold, D10; S100 P1 H1: a 30 s decay divided by
 *    the heads' sum, was unity); the record LP and the playback bandwidth
 *    at the tape speed;
```

### S8 — after line 1250, add: the `hed` law updates the hold gain

Anchor (keep):

```c
        k->hg[2] = (s & 4u) ? h : 0.0f;
        break;
```

Insert between the two lines:

```c
        /* S100 P1 H1: the `hld` gain divides by the heads' sum. */
        space_holdGain(rt, k);
```

- **Why here:** `k.hold` depends on which heads are on (the shortest
  head's pass) and on their sum. A `hed` change must refresh it in the same
  `write_param` call, before the next block plays it.
- **Inputs:** `rt->t_samples`, the new `k->hg[]`.
- **Output:** `k->hold`.
- **Note:** `rt` is `space_writeParam()`'s `SpaceRuntime *` (line 1196).

### S7 — lines 1227–1236, modify: the `deg` law (D2 and A)

Anchor (remove):

```c
    case SPACE_LAW_DEG:
        /* D5: drive x1..x6, bandwidth 16 kHz down 2 octaves, hiss -72..-48 dBFS. */
        k->drive = 1.0f + 5.0f * g;
        k->makeup = 1.0f / (1.0f + 2.0f * g);
        k->bw = space_onePoleK(16000.0f * exp2f(-2.0f * g));
        k->comp = 0.25f + 0.5f * g;
        k->hiss = (value == 0u) ? 0.0f
                : exp2f((-72.0f + 24.0f * g) * 0.16609640f) *
                  (1.0f / 2147483648.0f);
        break;
```

Replace with:

```c
    case SPACE_LAW_DEG:
        /*
         * D5 (S100 P1 D2 and A): drive x1..x6, bandwidth 16 kHz down 2
         * octaves, tape compression, and the modulation noise.
         *
         * makeup = 1 / drive (D2; was 1 / (1 + 2g)).
         * What:  the clip stage is unity at small signal at every `deg`;
         *        `deg` lowers the clip point relative to the signal
         *        (the clipped ceiling is (2/3) / drive: -19.1 dBFS at 127).
         * Why:   with 1 / (1 + 2g) the small-signal loop gain was
         *        fbk x (1 + 5g) / (1 + 2g), up to 2 x fbk, so `deg` 127 moved
         *        unity from `fbk` row 100 to row 50: a mid `fbk` became an
         *        endless drone that never let the idle engage, and `hld`
         *        rose. Now `fbk` means space_fbkGain() at every `deg`.
         *
         * hiss = makeup x 10^((-57 + 24g) / 20) / 2^31 (A; was an absolute
         * -72..-48 dBFS added to the loop).
         * What:  nz = hiss x LCG word, so |nz / makeup| <= 10^((-57+24g)/20):
         *        the loop gain jitters by that relative amount; RMS S/N per
         *        pass 61.6 dB at `deg` 1, 37.8 dB at 127. 0 at `deg` 0.
         * Why:   matches the S095 hiss under a -15 dBFS tape, and is 0 on an
         *        empty one (space_loop()).
         * Inputs: g = deg / 127. Outputs: k->drive, makeup, bw, comp, hiss.
         * Affiliates: space_loop(), space_delayRun(), space_echoTapeRun();
         *        host laws SP2, SP12, SP13.
         */
        k->drive = 1.0f + 5.0f * g;
        k->makeup = 1.0f / k->drive;
        k->bw = space_onePoleK(16000.0f * exp2f(-2.0f * g));
        k->comp = 0.25f + 0.5f * g;
        k->hiss = (value == 0u) ? 0.0f
                : k->makeup * exp2f((-57.0f + 24.0f * g) * 0.16609640f) *
                  (1.0f / 2147483648.0f);
        break;
```

- **Cost:** `write_param` only (cold, `SPACE_SIZE`), as before.
- **Readers:** `k.drive`, `k.makeup` and `k.hiss` are read once per block
  in `space_delayRun()` (lines 419–422) and `space_echoTapeRun()` (lines
  561–564). Those readers are unchanged.

### S6 — lines 364–368, modify: the noise multiplies (A)

Anchor (remove):

```c
    /*
     * x - x^3 / 3: slope 1 at 0 (§S18: the 1.5 x - 0.5 x^3 clip had a small-
     * signal gain of 1.5, so `fbk` unity and `hld` grew), 2/3 at the clip.
     */
    y = y * (1.0f - (1.0f / 3.0f) * y * y) * makeup + nz;
```

Replace with:

```c
    /*
     * x - x^3 / 3: slope 1 at 0 (§S18: the 1.5 x - 0.5 x^3 clip had a small-
     * signal gain of 1.5, so `fbk` unity and `hld` grew), 2/3 at the clip.
     *
     * The degradation noise (S100 P1 A): a gain jitter, (makeup + nz).
     * What:       nz (hiss x one LCG word, shared by both channels) is added
     *             to the stage's gain, not to its output, so the noise is
     *             y x nz: modulation noise that rides on the loop signal.
     * Why:        an additive hiss (S095 P1 D5, `+ nz`) was recorded every
     *             frame whenever `deg` and `fbk` were above 0, so an empty
     *             tape played hiss (-71 dBFS RMS at deg 64 / fbk 50, host)
     *             forever, with no send and no input, and kept the stopped-
     *             transport idle from engaging (S100 P1 §4). y = 0 now gives
     *             exactly 0, sample by sample: an empty tape stays exact
     *             zero, and a tail returns to exact zero once it decays.
     * Inputs:     y (after the tone and the clip), makeup and nz (hiss is
     *             makeup x the relative jitter, SPACE_LAW_DEG).
     * Cost:       the add folds into the existing multiply: the Delay loop
     *             is 1 instruction a frame shorter, the Echo unchanged
     *             (space_cpu.py, S100 P1).
     * Affiliates: SPACE_LAW_DEG (the hiss law), host law SP12.
     */
    y = y * (1.0f - (1.0f / 3.0f) * y * y) * (makeup + nz);
```

- **Callers:** `space_loop()` is inlined twice in `space_delayRun()` (lines
  510–513, L and R, one `nz` for both) and once in `space_echoTapeRun()`
  (line 587). All three take the new form; no caller changes.

### S5 — line 351, modify: `space_loop()` description

Anchor (remove):

```c
 * gain, §S18) with makeup, the hiss,
```

Replace with:

```c
 * gain, §S18) with makeup (S100 P1 D2: 1 / drive, so the stage is unity
 * at small signal at every `deg`), the modulation noise (S100 P1 A),
```

### S4 — after line 226, add: `space_holdGain()`

Anchor (keep): the end of `space_onePoleK()`:

```c
    return 1.0f - exp2f(fc * (-6.2831853f * 1.4426950f / SK_FS));
}
```

Insert after it, with one blank line before:

```c
/*
 * The `hld` loop gain per pass (S100 P1 H1).
 *
 * What:       k->hold = (1 - x) / ((1 + x) x S), where
 *             x = SPACE_HOLD_K x P (clamped to 0..1, so the gain stays in
 *             0..1), P = the loop's pass in samples: t_samples (the `tim`
 *             channel) x the shortest active head's share of D (1/3 with
 *             head A, 2/3 with B and no A, 1 for C alone), and
 *             S = hg[0] + hg[1] + hg[2], the heads' sum (1 for one head,
 *             sqrt(2) for two, sqrt(3) for three; floored at 1, which also
 *             covers the all-zero gains before the `hed` law has run).
 *             The shortest head sets P because it recirculates most often:
 *             a head-A loop passes every D/3, so its gain per pass must be
 *             the gain of a third of the time.
 * Why:        the held loop must never rise. The heads are normalised for
 *             power (1 / sqrt(n) each), so at low frequency, where they add
 *             in phase, a multi-head stop's loop gain is sqrt(n) x fbk.
 *             Dividing by the sum makes the in-phase gain exactly the
 *             decay's pass gain at every stop. With D2 (makeup = 1 / drive)
 *             `deg` adds nothing at small signal, so no other stage can
 *             lift the held loop above that gain.
 * Inputs:     rt->t_samples (the `tim` curve's samples at the tempo; 0 until
 *             the first block), k->hg[] (which heads are on, and their sum).
 * Outputs:    k->hold.
 * Cost:       one division; runs only on a `hed` row change and when `tim`
 *             or the tempo moves (the branch space_cpu.py leaves out of the
 *             steady total), never per block or per frame. No libm.
 * Accessors:  space_writeParam() (SPACE_LAW_HED), space_tapeBegin().
 * Affiliates: SPACE_HOLD_T60_S / SPACE_HOLD_K, SPACE_F_HOLD, host law SP13.
 * Note:       the `spr` channel shorter than `tim` passes more often at the
 *             same gain, so it decays up to twice as fast (spr's limit).
 */
static SPACE_SIZE void space_holdGain(const SpaceRuntime *rt, space_tapeK_t *k)
{
    const float share = (k->hg[0] > 0.0f) ? (1.0f / 3.0f)
                      : ((k->hg[1] > 0.0f) ? (2.0f / 3.0f) : 1.0f);
    const float x = __builtin_fminf(SPACE_HOLD_K * rt->t_samples * share,
                                    1.0f);
    const float s = __builtin_fmaxf(k->hg[0] + k->hg[1] + k->hg[2], 1.0f);

    k->hold = (1.0f - x) / ((1.0f + x) * s);
}
```

- **Placement:** after `space_onePoleK()` and before the "The tape" banner
  (line 228). It must come before its two callers, `space_writeParam()`
  (S8) and `space_tapeBegin()` (S10). `SpaceRuntime` comes from
  `SpaceEffect.h`, which is included at line 20; `SK_FS` from
  `SpaceKit.h`.
- **The Echo:** `hed` and `tim` call this for the Echo too. Its `k.hold`
  is computed but never played, because the Echo has no `hld` row.

### S3 — after line 109, add: `space_tapeK_t.hold`

Anchor (keep):

```c
    float xfb;
} space_tapeK_t;
```

Insert between the two lines:

```c
    /*
     * S100 P1 H1: the `hld` loop gain per pass.
     * Writers: space_holdGain() (from the `hed` law and from
     * space_tapeBegin() when `tim` or the tempo moves). Reader:
     * space_tapeBegin(), which plays it as the loop's fbk while `hld` is on.
     * 0 after space_enter()'s memset until the first block (the first block
     * always recomputes, t_bpm starts at 0). +4 B inside the fixed 448 B
     * arena prefix (space_hdr_t 392 -> 396 B); no new allocation.
     */
    float hold;
```

- **Lifetime:** `space_enter()` (line 1359) clears the whole header, so no
  init change is needed.
- **Size:** the `_Static_assert` at line 187 still holds (396 ≤ 448).
- **RAM sign-off:** see §2.

### S2 — lines 91–94, modify: `space_tapeK_t` description

Anchor (remove):

```c
 * 63 a one-pole high-pass; flat at 63: 1 and 0), drive/makeup/hiss (`deg`,
 * D5), hg[3] (the heads D/3, 2D/3, D, O6, normalised), wow/dck/xfb (the
 * linear rows: wow depth, ducking, cross-feed). S095 P4 T1: no wet mix;
 * the Space types are 100 % wet.
```

Replace with:

```c
 * 63 a one-pole high-pass; flat at 63: 1 and 0), drive/makeup/hiss (`deg`,
 * D5; S100 P1: makeup = 1 / drive, hiss is a relative gain jitter), hg[3]
 * (the heads D/3, 2D/3, D, O6, normalised), wow/dck/xfb (the linear rows:
 * wow depth, ducking, cross-feed), hold (S100 P1 H1: the `hld` loop gain per
 * pass, space_holdGain()). S095 P4 T1: no wet mix; the Space types are
 * 100 % wet.
```

### S1 — after line 70, add: the hold decay constants

Anchor (keep):

```c
#define SPACE_DUCK_REL       0.0036f
```

Insert after it:

```c
/*
 * The `hld` decay (S100 P1 H1).
 *
 * What:       SPACE_HOLD_T60_S is the time a held loop takes to fall 60 dB
 *             at low frequency (where the playback and record low-passes
 *             lose nothing); SPACE_HOLD_K is the per-sample constant of
 *             space_holdGain()'s pass gain, ln(1000) / 2 / (fs x T60), so
 *             that x = SPACE_HOLD_K x (one pass in samples) and the pass gain
 *             (1 - x) / (1 + x) is the [1/1] Pade form of 10^(-3 pass / T60).
 * Why:        `hld` used a loop gain of exactly 1, so any gain in the loop
 *             above unity (a multi-head stop's coherent sum, up to sqrt(3) at
 *             low frequency) made a held loop rise to the clip within
 *             seconds. A decay set in seconds, not per pass, holds the same
 *             time at every `tim`: a per-pass constant would empty a short
 *             delay in a fraction of a second and barely touch a long one.
 * Range:      30 s: about -2 dB per second, -10 dB after 5 s. A tuning value
 *             (user, by ear); the Pade form is within 0.8 dB per pass of the
 *             exact exponential at the longest tape (4.25 s).
 * Affiliates: space_holdGain(), space_tapeBegin(), host law SP13.
 */
#define SPACE_HOLD_T60_S     30.0f
#define SPACE_HOLD_K         (3.4538776f / (SK_FS * SPACE_HOLD_T60_S))
```

- **The constant:** 3.4538776 = ln(1000)/2. `SK_FS` is 44,108.0f
  ([SpaceKit.h:78](Core/DSP/Effects/Space/SpaceKit.h#L78)), so the
  quotient folds at compile time.

---

## 4. Change schedule — `Core/DSP/Effects/Space/SpaceParameters.h`

### S13 — line 47, modify

```c
    SPACE_DLY_HOLD,       /* 10 on: input 0, loop gain 1 (D10) */
```

becomes

```c
    SPACE_DLY_HOLD,       /* 10 on: input 0, loop decays 60 dB in 30 s (D10, S100 P1) */
```

### S12 — line 45, modify

```c
    SPACE_DLY_DEGRADE,    /* 8  drive, head bandwidth, hiss (D5) */
```

becomes

```c
    SPACE_DLY_DEGRADE,    /* 8  drive, head bandwidth, modulation noise (D5, S100 P1) */
```

The enum values and the rows are unchanged. The `space` target extracts this
header (`sp_params.c`), and the new comments pass through it.

---

## 5. Change schedule — `tools/dsp_test/modules/space.c`

### T3 — after line 703, add: run the new laws

Anchor (keep):

```c
    law_sp11();
```

Insert after it:

```c
    law_sp12();
    law_sp13();
```

### T2 — after line 689, add: SP12 and SP13

Anchor: the closing `}` of `law_sp11()` (line 689), followed by a blank line
and `int main(void)` (line 691). Insert after line 689, keeping one blank line
before `int main`:

```c

/*
 * SP12 (S100 P1 A): no degradation noise without a signal.
 *
 * What:       (a) from init with zero input for 3 s, every output sample is
 *             exactly 0.0: the Delay at deg 127 / fbk 127 / heads ABC and at
 *             deg 64 / fbk 90 / head C, the Echo at deg 127 / fbk 127 /
 *             heads ABC; (b) after 0.5 s of noise (peak 0.25) the Delay and
 *             the Echo at deg 127 / fbk 90 / head C reach exact 0.0 within
 *             12 s and stay there.
 * Why:        the S095 additive hiss played forever from an empty tape
 *             (S100_P1_BG_NOISE_INVESTIGATION.md §7.4); its break build
 *             (the additive form back) must fail (a).
 * Inputs:     sp12_run(): the type's ops, its rows, the burst length and the
 *             run length in blocks. Output: the first block from which every
 *             later sample was exactly 0.0 (-1 if the output never settled).
 */
static long sp12_run(const effect_type_ops_t *ops, const uint8_t *rows,
                     uint32_t nrows, uint32_t burst, uint32_t blocks)
{
    float l[32];
    float r[32];
    long zero_at = -1;
    uint32_t b;
    uint32_t n;

    sp_enter(ops, NULL, rows, nrows);
    for (b = 0u; b < blocks; b++) {
        int quiet = 1;

        for (n = 0u; n < 32u; n++) {
            l[n] = (b < burst) ? 0.25f * sp_noise() : 0.0f;
            r[n] = (b < burst) ? 0.25f * sp_noise() : 0.0f;
        }
        sp_block(ops, l, r);
        for (n = 0u; n < 32u; n++)
            if (l[n] != 0.0f || r[n] != 0.0f)
                quiet = 0;
        if (!quiet)
            zero_at = -1;
        else if (zero_at < 0)
            zero_at = (long)b;
    }
    return zero_at;
}

static void law_sp12(void)
{
    uint8_t d[SP_DLY_ROWS];
    uint8_t e[SP_ECH_ROWS];
    char msg[160];
    long z;

    /* (a) silence in: exact zero from the first block (3 s). */
    memcpy(d, sp_dlyRows, sizeof(d));
    d[2] = 127u; d[5] = 127u; d[9] = 6u;
    z = sp12_run(&space_delayOps, d, SP_DLY_ROWS, 0u, 4135u);
    snprintf(msg, sizeof msg, "SP12 Delay deg 127 fbk 127 ABC, no input: zero from block %ld (want 0)", z);
    expect(z == 0, msg);
    d[2] = 90u; d[5] = 64u; d[9] = SPACE_HEADS_DEFAULT;
    z = sp12_run(&space_delayOps, d, SP_DLY_ROWS, 0u, 4135u);
    snprintf(msg, sizeof msg, "SP12 Delay deg 64 fbk 90 C, no input: zero from block %ld (want 0)", z);
    expect(z == 0, msg);
    memcpy(e, sp_echRows, sizeof(e));
    e[1] = 127u; e[3] = 127u; e[10] = 6u;
    z = sp12_run(&space_echoOps, e, SP_ECH_ROWS, 0u, 4135u);
    snprintf(msg, sizeof msg, "SP12 Echo deg 127 fbk 127 ABC, no input: zero from block %ld (want 0)", z);
    expect(z == 0, msg);
    /* (b) a tail returns to exact zero (12.5 s run, 0.5 s burst). */
    d[2] = 90u; d[5] = 127u; d[9] = SPACE_HEADS_DEFAULT;
    z = sp12_run(&space_delayOps, d, SP_DLY_ROWS, 689u, 17229u);
    snprintf(msg, sizeof msg, "SP12 Delay deg 127 fbk 90 C, burst: exact zero from %.2f s (want 0 < t <= 12)",
             (z < 0) ? -1.0 : (double)z * 32.0 / 44108.0);
    expect(z > 0 && z <= 16540, msg);
    e[1] = 90u; e[3] = 127u; e[10] = SPACE_HEADS_DEFAULT;
    z = sp12_run(&space_echoOps, e, SP_ECH_ROWS, 689u, 17229u);
    snprintf(msg, sizeof msg, "SP12 Echo deg 127 fbk 90 C, burst: exact zero from %.2f s (want 0 < t <= 12)",
             (z < 0) ? -1.0 : (double)z * 32.0 / 44108.0);
    expect(z > 0 && z <= 16540, msg);
}

/*
 * SP13 (S100 P1 H1, D2): `hld` never rises and decays at SPACE_HOLD_T60_S.
 *
 * What:       the Delay at tim 40 (3,899 samples at 120 BPM) with input and
 *             fbk 0 until `hld` goes on at 1.09 s, so the tape holds the
 *             input at its own level under every head (D/3, 2D/3 and D
 *             back). At fbk 127 the short loop would reach the clip within
 *             that second, and the clip's compression would hide a rise.
 *             (a) every `hed` stop at deg 0 and 127, 1 s of noise (0.02):
 *             the energy of the 4th held second is at most the 1st's (SP8
 *             at every stop and deg, without its 5 % allowance). The heads
 *             add in phase at multiples of 3 fs / D (34 Hz here), where a
 *             multi-head stop's gain was sqrt(n).
 *             (b) deg 0, each single head A, B, C: a 200 Hz sine (0.02) for
 *             1 s: the level falls 5.5..8.5 dB from held second 1 to held
 *             second 4: the hold's 6 dB (-2 dB a second at every head, not
 *             per pass) plus the loop's own small per-pass losses, which add
 *             up faster on a shorter head (measured S100 P1: C 6.5, B 6.8,
 *             A 8.0 dB; with the old unity hold, C fell 0.5 dB).
 * Why:        with a loop gain of exactly 1, `hld` rose to the clip in
 *             seconds on a multi-head stop or with `deg` up (S100 P1 H1);
 *             its break build (hold back at 1.0) must fail (a).
 * Inputs:     sp13_run(): the `hed` stop, `deg`, sine (1) or noise (0).
 *             Output: the 1st held second's energy; *e4 the 4th's.
 */
static double sp13_run(uint8_t hed, uint8_t deg, int sine, double *e4)
{
    uint8_t rows[SP_DLY_ROWS];
    float l[32];
    float r[32];
    double e1 = 0.0;
    double ph = 0.0;
    uint32_t b;
    uint32_t n;

    memcpy(rows, sp_dlyRows, sizeof(rows));
    rows[0] = 40u;   /* 3,899 samples at 120 BPM */
    rows[2] = 0u;    /* no loop growth before `hld`: the tape holds the input level */
    rows[5] = deg;
    rows[9] = hed;
    sp_enter(&space_delayOps, NULL, rows, SP_DLY_ROWS);
    *e4 = 0.0;
    for (b = 0u; b < 7000u; b++) {
        if (b == 1500u)
            space_delayOps.write_param(&sp_rt, SPACE_DLY_HOLD, 1u);
        for (n = 0u; n < 32u; n++) {
            float x = 0.0f;

            if (b < 1500u) {   /* up to `hld`: every head's span holds input */
                x = sine ? 0.02f * (float)sin(ph) : 0.02f * sp_noise();
                ph += 2.0 * 3.14159265358979 * 200.0 / 44108.0;
            }
            l[n] = r[n] = x;
        }
        sp_block(&space_delayOps, l, r);
        for (n = 0u; n < 32u; n++) {
            if (b >= 1500u && b < 2878u)
                e1 += (double)l[n] * l[n];
            else if (b >= 5622u)
                *e4 += (double)l[n] * l[n];
        }
    }
    return e1;
}

static void law_sp13(void)
{
    char msg[160];
    double e1;
    double e4;
    double worst = 0.0;
    int ok = 1;
    uint8_t hed;
    uint8_t di;

    for (di = 0u; di < 2u; di++)
        for (hed = 0u; hed < SPACE_HEADS_STOPS; hed++) {
            e1 = sp13_run(hed, di ? 127u : 0u, 0, &e4);
            if (!(e1 > 1e-3 && e4 <= e1))
                ok = 0;
            if (e1 > 0.0 && e4 / e1 > worst)
                worst = e4 / e1;
        }
    snprintf(msg, sizeof msg, "SP13 hld, every hed at deg 0/127: worst 4th/1st second energy %.3f (<= 1)", worst);
    expect(ok, msg);
    for (hed = 0u; hed < 3u; hed++) {
        static const char name[3] = { 'A', 'B', 'C' };

        e1 = sp13_run(hed, 0u, 1, &e4);
        snprintf(msg, sizeof msg, "SP13 hld, 200 Hz on %c: fall over held seconds 1..4 %.2f dB (want 5.5..8.5)",
                 name[hed], (e4 > 0.0) ? 10.0 * log10(e1 / e4) : 999.0);
        expect(e4 > 0.0 && 10.0 * log10(e1 / e4) >= 5.5 &&
               10.0 * log10(e1 / e4) <= 8.5, msg);
    }
}
```

**Row indices used** (from the row-order comments at lines 157 and 308):

- Delay `d[]`: 0 `tim`, 2 `fbk`, 5 `deg`, 9 `hed`.
- Echo `e[]`: 1 `fbk`, 3 `deg`, 10 `hed`.
- `SPACE_HEADS_STOPS` and `SPACE_HEADS_DEFAULT` come from the `sp_params.c`
  extraction (Makefile lines 1628 and 1631).
- `sin()`/`log10()`/`fabs()`: `<math.h>` is already included.

**Measured output (scratch):**

```
ok   SP12 Delay deg 127 fbk 127 ABC, no input: zero from block 0 (want 0)
ok   SP12 Delay deg 64 fbk 90 C, no input: zero from block 0 (want 0)
ok   SP12 Echo deg 127 fbk 127 ABC, no input: zero from block 0 (want 0)
ok   SP12 Delay deg 127 fbk 90 C, burst: exact zero from 10.23 s (want 0 < t <= 12)
ok   SP12 Echo deg 127 fbk 90 C, burst: exact zero from 10.33 s (want 0 < t <= 12)
ok   SP13 hld, every hed at deg 0/127: worst 4th/1st second energy 0.054 (<= 1)
ok   SP13 hld, 200 Hz on A: fall over held seconds 1..4 8.04 dB (want 5.5..8.5)
ok   SP13 hld, 200 Hz on B: fall over held seconds 1..4 6.76 dB (want 5.5..8.5)
ok   SP13 hld, 200 Hz on C: fall over held seconds 1..4 6.53 dB (want 5.5..8.5)
space failures: 0
```

The existing SP8 still passes ("energy second 1 5.98e+03, second 4 380").

### T1 — after line 86, add: the law list

Anchor (keep), lines 84–86:

```c
 *       on the long channel (spr relative); `hld` does not build; Reverb
 *       `ton` darkens below 127; the stopped-transport idle ramps, idles and
 *       wakes;
```

Insert after line 86 (before ` *   SP5 the feed: ...`):

```c
 *   SP12 (S100 P1 A): the Delay and the Echo are exact zero with no input
 *       at any `deg` / `fbk` / `hed` (no hiss from an empty tape), and a
 *       tail at `deg` 127 / `fbk` 90 returns to exact zero within 12 s;
 *   SP13 (S100 P1 H1, D2): `hld` never rises (every `hed` stop at `deg`
 *       0 and 127: the 4th held second's energy at most the 1st's), and a
 *       200 Hz tone held on head A, B or C falls 5.5..8.5 dB over held
 *       seconds 1..4 (SPACE_HOLD_T60_S 30: -2 dB a second, plus the loop's
 *       own losses);
```

---

## 6. Change schedule — `tools/dsp_test/Makefile`

### M1 — after line 1659, add: the two break builds

Anchor (keep), the end of the `space:` recipe:

```make
	@if ./build/space_break_lin1 >/dev/null 2>&1; then \
	  echo "space_break_lin1: FAILED (a frame-stepped Echo head not caught)"; exit 1; \
	else echo "space_break_lin1: caught (law SP11 fails as expected)"; fi
```

Insert after it. Recipe lines start with a **tab**:

```make
	@# S100 P1: the S095 additive hiss back (`* makeup + nz`) must fail SP12
	@# (an empty tape plays noise); the unity `hld` loop back (`? 1.0f :`)
	@# must fail SP13 (a multi-head hold rises).
	mkdir -p build/break_sp_hiss build/break_sp_hold
	sed 's/\* (makeup + nz);/* makeup + nz;/' \
	  $(GEN)/SpaceEffect.host.c > build/break_sp_hiss/SpaceEffect.host.c
	@grep -q '\* makeup + nz;' build/break_sp_hiss/SpaceEffect.host.c || \
	  { echo "space_break_hiss: sed did not apply"; exit 1; }
	$(HOSTCC) -Ibuild/break_sp_hiss $(HOSTFLAGS) modules/space.c -o build/space_break_hiss -lm
	@if ./build/space_break_hiss >/dev/null 2>&1; then \
	  echo "space_break_hiss: FAILED (an additive hiss not caught)"; exit 1; \
	else echo "space_break_hiss: caught (law SP12 fails as expected)"; fi
	sed 's/? h->k.hold : h->k.fbk;/? 1.0f : h->k.fbk;/' \
	  $(GEN)/SpaceEffect.host.c > build/break_sp_hold/SpaceEffect.host.c
	@grep -q '? 1.0f : h->k.fbk;' build/break_sp_hold/SpaceEffect.host.c || \
	  { echo "space_break_hold: sed did not apply"; exit 1; }
	$(HOSTCC) -Ibuild/break_sp_hold $(HOSTFLAGS) modules/space.c -o build/space_break_hold -lm
	@if ./build/space_break_hold >/dev/null 2>&1; then \
	  echo "space_break_hold: FAILED (a unity hld loop not caught)"; exit 1; \
	else echo "space_break_hold: caught (law SP13 fails as expected)"; fi
```

- **Pattern:** the S096 P2 `lin1`/`lin2` breaks: sed one line of the
  extracted host source, confirm the sed applied, build against it, and
  require the program to fail.
- **Measured:** both print "caught".
- **Header comment:** the target list at the top of the Makefile has no
  per-break entry for `space`, so it needs no change.

---

## 7. Verification steps (in order, after applying)

1. `make all && make img`. Expect flash about 731,080 B and a clean build.
2. `python3 tools/link_budget.py arm-none-eabi-nm build/lxr02.elf` (headroom
   above 16 KiB).
3. `make -C tools/dsp_test space`. Expect 0 failures, and all four
   `space_break_*` caught.
4. `make -C tools/dsp_test space_cpu_check`. Expect Delay 250.8 ≤ 252.0 and
   Echo 210.1 ≤ 210.5 (feed 270.8 / 230.1).
5. `make -C tools/dsp_test crumpbit` (the `space` target depends on it; 0
   failures, breaks caught) and `make -C tools/dsp_test elf_s099_check`
   (15/15 exact).
6. Mixer inlined set: `python3 tools/dsp_test/elf_loopdump.py --elf
   build/lxr02.elf --sym mixer_calcNextSampleBlock --out <after>` and
   compare with a `c930dcf5` dump. Expect 68/68 exact.
   `elf_p4_check` fails identically before and after (stale P4 baseline);
   do not read that failure as this change's.
7. Device and ear:
   - **A:** with no input, and after a tail has died, the return is silent.
     The noise rides on the repeats at the old `deg` loudness under a
     −15 dBFS tape. Adjust the −57 by ear if needed.
   - **D2:** `fbk` 99 holds just below sustain at every `deg`. Note how
     repeats at high `deg` sound, now that they are shorter and quieter at
     the clip.
   - **H1:** `hld` on at every `hed` stop never builds, and fades about
     10 dB in 5 s. Adjust `SPACE_HOLD_T60_S` by ear if needed.
   - **Idle:** with the transport stopped after a tail, the Effect return
     idles within about 1 s once the output is below −90 dBFS.
   - **CPU:** read the `cpu` widget on the worst-case Scene with the Delay
     and with the Echo.

---

## 8. Statements this makes out of date (for the user to act on)

- `S100_P1_BG_NOISE_INVESTIGATION.md` §7.6 recommends B + D2; the decision
  is A + D2 plus H1. Its §1 "Fix" paragraph says the same.
- `SPACE_GROUP_EFFECTS_MODELLING_REFERENCE.md` and
  `SPACE_GROUP_EFFECTS_MODULE_REFERENCE.md` do not describe `deg` or `hld`
  at all. They need:
  - the `deg` law (drive, makeup = 1/drive, bandwidth, compression, and the
    relative modulation-noise law);
  - the `hld` law (`k.hold`, the 30 s T60, the shortest-head pass, the
    division by the heads' sum);
  - laws SP12 and SP13.
- `S095_P1_FX_DELAY_RVB_ECHO.md`:
  - D-6 says "the hiss sits at about −48 dBFS", an absolute hiss;
  - its rows text says "`hld`: input 0, loop gain 1" (D10) and "`deg` …
    hiss (−72 to −48 dBFS)" (D5).
- `MIXER_SYSTEM_DSP_REFERENCE.md`: no change from this schedule. Its stale
  dry-path text is listed in the investigation's §9.

## 9. Not in this schedule (for decision)

- **Multi-head stops under normal `fbk`.**
  - Problem: the same √n in-phase gain applies. With heads AB/AC/BC the
    loop reaches unity at about `fbk` row 71, and with ABC at about row 58,
    not 100. A tail then sustains or runs away at low frequency. This is the
    "caught out" pattern again, outside `hld`.
  - Fix: the S4 head-sum division applied to `k.fbk` in the `fbk` and `hed`
    laws. It is cold-path only and costs nothing per frame.
  - Effect on the sound: multi-head echoes at a given `fbk` become shorter.
  - Not measured. Needs your call.
- **`spr` and `hld`:** the shorter channel decays up to twice as fast (S4
  note). Matching both would need a per-channel `fbk` in the frame loop,
  which costs per frame. Left as is.

---

## 10. Working notes (implementation session, 2026-11-20)

State: **implementation in progress**. Every change below follows §3–§6
verbatim; nothing else is touched.

### 10.1 Source and tests applied

- **S1–S11 applied** in `Core/DSP/Effects/Space/SpaceEffect.c`:
  `SPACE_HOLD_T60_S`/`SPACE_HOLD_K` (after the ducking constants),
  the `space_tapeK_t` description and `float hold` field,
  `space_holdGain()` (after `space_onePoleK()`), the `space_loop()`
  description and `(makeup + nz)`, `SPACE_LAW_DEG` (D2 + A), the
  `SPACE_LAW_HED` call, the `space_tapeBegin()` description, its
  `tim`/tempo-branch call, and `ps->fbk = ... h->k.hold ...`.
- **S12–S13 applied** in `Core/DSP/Effects/Space/SpaceParameters.h`: the
  two row comments now name the new laws, with a short block above the enum
  stating the S100 P1 meanings (keys and enum values unchanged).
- **T1–T3 applied** in `tools/dsp_test/modules/space.c`: the law list,
  `sp12_run()`/`law_sp12()`, `sp13_run()`/`law_sp13()`, and the two
  calls in `main()`.
- **M1 applied** in `tools/dsp_test/Makefile`: `space_break_hiss` and
  `space_break_hold`, after the S096 P2 `lin1` break.

### 10.2 Host result (`make -C tools/dsp_test space`)

```
ok   SP12 Delay deg 127 fbk 127 ABC, no input: zero from block 0 (want 0)
ok   SP12 Delay deg 64 fbk 90 C, no input: zero from block 0 (want 0)
ok   SP12 Echo deg 127 fbk 127 ABC, no input: zero from block 0 (want 0)
ok   SP12 Delay deg 127 fbk 90 C, burst: exact zero from 10.23 s (want 0 < t <= 12)
ok   SP12 Echo deg 127 fbk 90 C, burst: exact zero from 10.33 s (want 0 < t <= 12)
ok   SP13 hld, every hed at deg 0/127: worst 4th/1st second energy 0.054 (<= 1)
ok   SP13 hld, 200 Hz on A: fall over held seconds 1..4 8.04 dB (want 5.5..8.5)
ok   SP13 hld, 200 Hz on B: fall over held seconds 1..4 6.76 dB (want 5.5..8.5)
ok   SP13 hld, 200 Hz on C: fall over held seconds 1..4 6.53 dB (want 5.5..8.5)
space failures: 0
space_break_lin2: caught (law SP11 fails as expected)
space_break_lin1: caught (law SP11 fails as expected)
space_break_hiss: caught (law SP12 fails as expected)
space_break_hold: caught (law SP13 fails as expected)
```

The measured values match §2 exactly; SP8 still passes (5.98e+03 / 380).
The existing laws SP1–SP11 all pass.

### 10.3 Firmware and gates

| Gate | Result | Cap / rule |
|---|---|---|
| `make all` | clean (pre-existing warnings only) | — |
| `make img` | `LXRV2_lxr02.img` 731,080 B | — |
| `link_budget.py` | Flash **731,080 / 753,664**, headroom **22,584**; ITCM **15,432 / 16,384**; DTCM statics **4,396**; FXBUF **126,656** (margin 3,776) | headroom > 16 KiB |
| `space_cpu.py` Delay | **250.8** (feed 270.8) | 252.0 (272.0) |
| `space_cpu.py` Echo | **210.1** (feed 230.1) | 210.5 (230.5) |
| `space_cpu.py` Reverb | **189.5** (feed 209.5) | 191.8 (211.8) |
| `make -C tools/dsp_test crumpbit` | 0 failures, every `crumpbit_break*` caught | — |
| `elf_s099_check` | **15 / 15 exact** | — |
| Mixer inlined set | **68 / 68 exact** against a `c930dcf5` build (isolated `git archive` copy under `/tmp`) | inlined set unchanged |
| `elf_p4_check` | fails identically before and after: 89 baseline loops, 70 exact, 4 renamed, **15 missing** (the stale P4 baseline's PWM WS loops) | pre-existing, not this change |

Firmware flash grew exactly as §2: 730,936 → **731,080 B (+144 B)**.
ITCM, DTCM statics and the FX buffer are unchanged (the +4 B `hold`
field lives inside the cleared 448 B arena prefix).

### 10.4 Remaining

- Device and ear checks (§7 item 7) are not possible on the host: the
  hiss bed under real repeats (A), `fbk` 99 at every `deg` (D2), `hld`
  fade at every `hed` stop (H1), the return idling about 1 s after a tail,
  and the `cpu` widget on the worst-case Scene.
- Statements now out of date: §8 lists them; nothing outside this phase's
  files was edited.

---

## 11. Assessment (review session, 2026-11-20)

**Verdict: the implementation matches the schedule and passes every host
and ELF gate.** No functional deviation was found. Only the device and ear
checks (§7 item 7) remain. The §10 notes are accurate apart from the two
small corrections in §11.4.

### 11.1 Code against the schedule

The tree was diffed against the exact files built and measured in the
scheduling session's scratch worktree:

| File | Result |
|---|---|
| `SpaceEffect.c` | **Code identical.** Two differences, both non-functional: a blank line after `SPACE_DUCK_REL` (S1), and the S7 comment's S/N figure (61.6 dB, the corrected value in §3). |
| `SpaceParameters.h` | S12/S13 identical. **Addition beyond the schedule:** a 12-line comment block above the enum restating the S100 P1 meanings of `deg` and `hld` (§10.1 records it). The text is accurate. "decays 60 dB in 30 s at every head set" holds at low frequency, as §1.3 states. No code. |
| `tools/dsp_test/modules/space.c` | **Code identical.** Differences are comments only: the tidied SP13 description and the SP12/SP13 `Inputs:` lines from §5. |
| `tools/dsp_test/Makefile` | **Identical** to the tested M1 recipe, line for line. |

No other source file changed (`git status`).

### 11.2 Gates rerun independently

| Gate | Result | Expected (§2) |
|---|---|---|
| `make all` | exit 0, no warnings in the build log | clean |
| Flash / ITCM / DTCM statics / FXBUF | 731,080 / 15,432 / 4,396 / 126,656 B | same |
| `make img` | payload 731,080 B ("Written … (731080b) OK"); file 731,096 B with the 16 B header | — |
| `space_cpu.py` Delay | **250.8** (feed 270.8) | 250.8 ≤ 252.0 |
| `space_cpu.py` Echo | **210.1** (feed 230.1) | 210.1 ≤ 210.5 |
| `space_cpu.py` Reverb | 189.5 (feed 209.5) | unchanged |
| `make -C tools/dsp_test space` | 33 `ok`, 0 failures; SP8, SP12 (5), SP13 (4) values identical to §2 | same |
| Breaks `lin1`, `lin2`, `hiss`, `hold` | all four caught | all caught |
| `elf_s099_check` | 15 / 15 exact | same |
| Mixer inlined set, against a fresh `c930dcf5` build (scratch worktree, removed) | `elf_loopdump --compare`: **68 / 68 exact**; `elf_hotloops --compare`: **59 hot loops equal, 0 grew** | unchanged |
| ELF text symbols | 1,765 → 1,766: `space_holdGain()`, out of line (`SPACE_SIZE`), as intended | +1 |

`elf_p4_check` was not rerun. §10.3 records the same failure before and
after, and it is unrelated (stale S094 P4 oscillator baseline).

### 11.3 Review of behaviour

- **Init order.**
  - `space_enter()` clears `k.hold` to 0.
  - The `hed` law sets it to `1/S` while `t_samples` is still 0.
  - The first block always takes the `tim`/tempo branch (`t_bpm` starts at
    0), and that branch recomputes `k.hold` before `ps->fbk` is selected
    later in the same function.
  - So a Scene loaded with `hld` already on plays the correct gain from its
    first block.
- **Echo.** The Echo shares S6, S7 and `space_tapeBegin()`. Its `k.hold` is
  computed but never selected, because the Echo has no `hld` row. Its +5
  instructions per block (+0.1 per frame) are the `k.hold` load; it remains
  0.4 under its cap.
- **`deg` law order.** `k->hiss` uses the `k->makeup` written two lines
  above in the same case, so a `deg` change can never pair a new hiss with
  an old makeup.
- **Edge case, no action needed.** If `tim` were beyond the share's limit,
  the played delay would be shorter than `t_samples` and the hold would
  decay faster than 30 s. The descriptor clamps `tim` to `space_timeLimit()`,
  so this needs a direct `write_param` (host only).
- **Open from §9, unchanged:** multi-head stops under normal `fbk` (unity at
  about row 58–71), and `spr` with `hld`.

### 11.4 Corrections to this document

- **§7 step 2** gives `tools/link_budget.py`. The script is at
  `tools/diagnostic_tools/link_budget.py`, and `make all` already runs it
  (Makefile line 270). `MEMORY.md` §DSP "Measuring flash" names it without
  a path; it is unaffected.
- **§10.3** gives the image as 731,080 B. That is the payload; the file is
  731,096 B, with the 16 B `LXRV2IMG` header.
- **Header "Status"** ("schedule only; nothing in the tree has changed") is
  now historical: the changes are in, uncommitted.

### 11.5 Working tree as reviewed

- **Modified source:** `SpaceEffect.c`, `SpaceParameters.h`,
  `tools/dsp_test/modules/space.c`, `tools/dsp_test/Makefile`.
- **Modified tracked outputs** (regenerated by the gates):
  - `build/LXRV2_lxr02.img`;
  - `tools/dsp_test/build/gen/SpaceEffect.host.c` and `sp_params.c`;
  - the `break_sp_lin1`/`lin2` sources;
  - the `space` and break binaries;
  - two `__pycache__` files (same size).
- **New, untracked:** `tools/dsp_test/build/break_sp_hiss/`,
  `break_sp_hold/`, `space_break_hiss`, `space_break_hold` (M1's outputs).
  Whether to track them like the existing `lin1`/`lin2` outputs is your
  call.
- **Remaining before closing S100 P1:** the §7 item 7 device and ear checks,
  and the §8 document updates.

---

## 12. S100 P1b — follow-up changes (implemented directly, 2026-11-21)

### 12.1 What was asked, and what went wrong

User requests, after listening to S100 P1:
- **`hld`:** it decayed far too fast. It should behave as feedback gain 1,
  holding the same level, and `fbk` should still control the held level:
  - rows below 64 decay slowly: about 15 s at row 0, about a minute at 64;
  - rows 64..80 hold the level;
  - rows above 80 rise slowly, with 127 saturating after about 20 s.
- **Row order:** swap `xfb` and `ton` on the Delay, so `fbk` and `ton` share
  a screen.
- **Depth:** double how far `xfb`, `deg` and `wow` reach at 127.

**Regression found while this was in progress (user):** "feedback > 100 no
longer builds to saturation." The cause was **D2** (S100 P1, makeup =
1/drive). It divides the level a loop above unity saturates at by the drive.
Measured on the host, Delay `tim` 64, 0.1 s of noise then silence, RMS of
the 10th second:

| `fbk` / `deg` | Committed `6dc13b69` | S100 P1 (D2) | P1b with D2 and drive ×11 | **S100 P1b as delivered** |
|---|---:|---:|---:|---:|
| 127 / 0 | −3.8 | −3.8 | −3.8 | **−3.8** |
| 127 / 64 | −8.4 | −19.4 | −24.6 | **−8.2** |
| 127 / 127 | −11.7 | −24.6 | −29.7 | **−11.6** |
| 110 / 64 | −9.3 | −20.0 | −24.4 | **−9.2** |
| 110 / 127 | −12.7 | −24.5 | −29.7 | **−12.7** |

`deg` 0 never changed; with `deg` up the S100 P1 loop built to a ceiling
10–13 dB lower. **D2 is withdrawn.** The clip stage is back to the S095 law
(drive ×1..×6, makeup 1/(1 + 2g)), and high feedback builds as it did before
S100, within 0.4 dB. New host law SP14 and break build `space_break_d2` stop
this from coming back.

**Consequence the user should know:** with D2 withdrawn, `deg` again raises
the small-signal loop gain to `fbk × (1 + 5g)/(1 + 2g)` (×2 at 127). So with
`deg` up, a loop reaches unity below `fbk` row 100, as it did before S100.
Under `hld` this no longer matters: the held loop uses its own clip stage
(§12.2).

### 12.2 Changes (all in the tree, uncommitted)

**`Core/DSP/Effects/Space/SpaceEffect.c`:**

| Change | What |
|---|---|
| Hold law constants | `SPACE_HOLD_T60_S`/`SPACE_HOLD_K` (P1) replaced by `SPACE_HOLD_T60_MIN_S` 15, `SPACE_HOLD_T60_OCT` 2, `SPACE_HOLD_UNITY_LO` 64, `SPACE_HOLD_UNITY_HI` 80, `SPACE_HOLD_RISE_DBS` 1.2, `SPACE_HOLD_DRIVE` 0.25, plus `SPACE_COMP_FLOOR` 0.125. Each has a What/Why block. |
| `space_exp2()` | New. 2^x without libm (alk_exp2f's form and coefficients) for the cold hold law. It runs in `space_tapeBegin()`'s `tim`/tempo branch, where this file allows no libm. |
| `space_holdGain()` | Rewritten. `k.hold = 2^(R·P/6.0206) / S`. R = the `fbk` row's rate: −60/(15 s·2^(2·row/63)) dB/s for rows 0..63, 0 for 64..80, +1.2·(row − 80)/47 dB/s for 81..127. P = the shortest active head's pass in seconds. S = the heads' sum. Delay only; any other type plays 0 dB/s. |
| `SPACE_LAW_FBK` | Also calls `space_holdGain()`, so a `fbk` change re-rates a running hold. |
| `space_pass_t` | +3 floats: `drive`, `makeup`, `hiss` (stack, per block). |
| `space_tapeBegin()` | Per block, picks the clip stage. Under `hld`: drive 0.25, makeup 4, which is unity at small signal and nearly linear to full scale. Otherwise `deg`'s drive and makeup. The noise is the relative jitter × the makeup played. |
| `space_delayRun()` / `space_echoTapeRun()` | Read drive, makeup and noise from the pass (were `h->k.*`). The O8 bandwidth factor is floored, `max(1 − comp·\|play\|, 0.125)`. |
| `SPACE_LAW_DEG` | Drive ×1..×6 and makeup 1/(1 + 2g) (S095, restored). Bandwidth 16 kHz down **4** octaves (to 1 kHz; was 2). Compression 0.25..**1.25** (was 0.75; floored). Noise **−57 + 30g** dB relative (was 24g; twice as deep at 127). `k.hiss` is now the relative jitter alone. **The drive is not doubled:** with this makeup, ×11 would give 3.7×`fbk` small-signal gain. |
| `SPACE_LAW_XFB` / `SPACE_LAW_WOW` | `xfb` 0..1 (was 0..0.5): 127 is a full L/R swap, true ping-pong. `wow` ±1 % of D (was ±0.5 %). |
| `space_laws[]` (Delay) | `ton` and `xfb` swapped. |
| Comments | struct, `space_loop()`, `space_tapeBegin()`, hold field. |

**`Core/DSP/Effects/Space/SpaceParameters.h`:**
- Enum: `SPACE_DLY_TONE` is now row 6 and `SPACE_DLY_XFEED` row 7.
- Row comments updated, and a block explaining the swap and its stored-data
  effect.

**`Core/DSP/Effects/Space/SpaceParameters.c`:**
- Descriptor order is `dly_tone` then `dly_xfeed`.
- The `space_delayText` comment is updated; its values are all `NONE` for
  both rows, so nothing else changes.

**Lanes:** the lanes ([EffectsManager.c:147](Core/DSP/Effects/EffectsManager.c#L147))
name the enum, so they follow the swap without a change.

**Stored data on the swapped rows:** Pattern automation, LFO targets and
AutoSave bytes store rows 6 and 7 by index, so each now plays the other
row's value (no migration, standing rule). `.fx` files and MIDI Learn store
keys and are unaffected.

**Echo:** shares the `deg` and `wow` laws, the floor and the clip-stage
select. It has no `hld` or `xfb` row.

**`tools/dsp_test/modules/space.c`:**
- `sp_dlyRows` follows the swap, and SP2 writes `xfb` at index 4.
- SP8 moves to `fbk` 72, since 127 now rises by design.
- SP12(b) uses `fbk` 45, because `deg` 127 at `fbk` 90 builds again.
- SP13 is rewritten for the `fbk`-driven law: (a) no rise at `fbk` 72 on
  any head set at `deg` 0/127; (b) windows for `fbk` 72/63/0/127 on heads
  A/B/C at −20 dBFS.
- New SP14: `fbk` 127 builds to at least −4.5/−9.0/−12.5 dBFS at `deg`
  0/64/127.

**`tools/dsp_test/Makefile`:** new break build `space_break_d2` (D2's makeup
back must fail SP14).

### 12.3 Measured

**Hold, host, Delay, 200 Hz tone, held from −20 dBFS:**

| Case | 5 s | 10 s | 15 s | 20 s | 25 s | 30 s |
|---|---:|---:|---:|---:|---:|---:|
| `fbk` 64, `deg` 0, `tim` 64 | −20.1 | −20.2 | −20.3 | −20.4 | −20.6 | −20.7 |
| `fbk` 64, `deg` 64 | −20.2 | −20.4 | −20.7 | −21.0 | −21.3 | −21.6 |
| `fbk` 127, `deg` 0 (from −20) | −16.9 | −11.2 | −6.2 | −2.7 | −0.9 | −0.5 |
| `fbk` 127, `deg` 0 (from −30) | −26.9 | −21.0 | −15.1 | −9.6 | −5.0 | −1.9 |
| `fbk` 0, `deg` 0 | −26.4 | −46.7 | −69.2 | silent | | |

SP13 rates at `tim` 40, −20 dBFS, change over held seconds 1..4 (designed
value in brackets):

| `fbk` | Change | Designed |
|---|---|---|
| 72 | 0.13–0.38 dB | 0 |
| 63 | 3.1–3.4 dB | 3 |
| 0 | 12.2–12.6 dB | 12 |
| 127 | −3.2 to −3.5 dB | −3.6 |

Before the hold's own clip stage, a −10 dBFS hold lost 3.3–30 dB in 3 s,
depending on `deg` and head. The cubic loses level on every pass, and
`deg`'s drive makes that worse.

**Gates:**

| Gate | Result |
|---|---|
| `make all` | exit 0. The four newlib stub warnings (`_close`, `_lseek`, `_read`, `_write`) also appear at `6dc13b69`; they are pre-existing. |
| Flash | **731,376 B** (+296 on S100 P1, +440 on `6dc13b69`); headroom 22,288 B |
| ITCM / DTCM statics / FXBUF | unchanged (15,432 / 4,396 / 126,656) |
| `space_cpu.py` Delay | **251.1** (feed 271.1) / cap 252.0 (S100 P1 250.8) |
| `space_cpu.py` Echo | **210.4** (feed 230.4) / cap 210.5 (S100 P1 210.1). The per-block clip-stage select and floor cost +9 per block. **0.1 left under the cap.** |
| `space_cpu.py` Reverb | 189.5 (unchanged) |
| `make -C tools/dsp_test space` | 0 failures; all SP1–SP14 lines `ok` |
| Breaks `lin1`, `lin2`, `hiss`, `hold`, `d2` | all caught |
| `crumpbit` | 0 failures |
| `elf_s099_check` | 15/15 exact |
| Mixer vs `c930dcf5` | `elf_loopdump` 68/68 exact; `elf_hotloops` 59 equal, 0 grew |
| RAM | no new static allocation. `k.hold` (S100 P1) is unchanged. The new stack use is `space_pass_t` +12 B. |

### 12.4 Open, for the user

- **`deg` 127 under `hld`:** the doubled bandwidth drop (down to 1 kHz)
  still dulls a held loop on every pass. At `tim` 40, a 200 Hz tone held at
  `fbk` 72 loses 5.6 (head C) to 17 dB (head A) in 3 s. At `deg` 64 it is
  0.35–1.1 dB. To make `hld` ignore `deg`'s bandwidth as well, the same
  per-block select could hold `bw` too; it costs about +2 per block, which
  the Echo cannot take (0.1 left). Your call.
- **Echo CPU:** 0.1 under its cap. Any further Echo per-block work needs a
  saving first.
- **The `deg` drive was not doubled** (§12.2). The bandwidth, compression
  and noise were.
- **Ear checks:**
  - `fbk` 110/127 builds at `deg` 0/64/127 as before;
  - the `hld` levels at `fbk` 0/63/64/80/81/127;
  - `xfb` 127 ping-pong;
  - `wow` 127 at long `tim`;
  - `deg` 127's sound;
  - the swapped page (screen 1: tim spr fbk ton).
- **Documents now out of date** (in addition to §8):
  - §1.2 and §3 S7 describe D2, now withdrawn;
  - §1.3 and S1/S4 describe the 30 s hold law, now replaced;
  - `S095_P4_FX_BUS_TWEAKS.md` and `S095_P4_IMPLEMENTATION.md` give the
    Delay row order `tim spr fbk xfb | ton …`;
  - `EFFECTS_BUS_REFERENCE.md` and the Space references, wherever they give
    the Delay rows, the `deg` law or the `hld` behaviour.
