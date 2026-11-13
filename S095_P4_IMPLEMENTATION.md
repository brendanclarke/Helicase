# S095 P4 — FX bus tweaks: Implementation Schedule

- **Status:** schedule, written 2026-11-13 from the tree at `4602b3b3`, for
  the plan `S095_P4_FX_BUS_TWEAKS.md` (all questions answered, its §5). No
  source was changed while writing it.
- **Line numbers** are those of `4602b3b3` before any step. A step that
  edits a file moves the lines after it; every edit therefore also quotes
  its anchor text. Within one file, apply the edits bottom-up, or find each
  one by its anchor.
- **Actions:** **ADD** (new lines; the existing lines stay), **REMOVE**
  (delete the lines named), **MODIFY** (replace the lines named with the
  block given), **NEW FILE**, **DELETE FILE**.
- **Order:** S0 → S12, each step's gate before the next (§2). Commits are
  yours; the schedule does not say when.
- **Rules that apply throughout:** CPU ceiling (no per-sample increase;
  `MEMORY.md` §DSP), no backward compatibility (no migration of stored
  data), RAM sign-off for any new allocation, nothing needed in `build/`.

---

## 0. Schedule decisions (made here; the plan's intent unchanged)

The code dive turned up the points below. Each is decided in this schedule;
say so before S1 if you want one changed.

| # | Point | Decision |
|---|---|---|
| SD1 | After T1 the Space lane tables have a hole where lane 1 (`mix`) was. Shifting the other lanes left would change what stored FX-lane data on lanes 2..15 drives | Lanes 2..15 keep their rows. Lane 1 takes the type's only row without a lane: Reverb `siz`, Echo `hsh`; the Delay has none left, so its lane 1 is empty |
| SD2 | `space_enter()` starts the dry gain at 1.0 when the outgoing type wrote no handoff (OFF, boot). With no dry target after T1, that is a jump to the whole dry bus followed by a fade | The dry starts at 0.0 when there is no handoff (OFF's return is silent, so silence is the continuous start) |
| SD3 | The export's dry was `1 − mix` (the target). With the target always 0, a switch during the dry glide would hand over 0 while the glide is still sounding | The export hands over `rt->dry`, the glide's current value |
| SD4 | `SpaceRuntime.mix` is the ramped wet gain, not the `mix` row | Renamed `wet` |
| SD5 | `space_stop()` maps 0..127 onto zones; after the stepped-row fix nothing uses zones | Replaced by `space_step()`: the value itself, clamped to the last stop |
| SD6 | `SPACE_ROWS_MAX` is 18 (Reverb rows 3..20) | 17 after T1; `SpaceRuntime` shrinks 4 B inside the 100 B union (no arena change) |
| SD7 | C3 needs a makeup factor per `drv` row | A generated 128-entry float table (512 B flash), half makeup, from a committed generator; `Distortion.inv_shape` (unused) becomes `out_gain` = makeup × (1 + k) |
| SD8 | C5's readout of a drum bus at 2 Hz shows a random instant | The cell shows the deepest reduction and the loudest detector level since its last repaint (peak hold). Needs 8 B `.bss` (SRAM1, `menu.c`): **sign-off asked** (§1) |
| SD9 | MIDI and Pattern values on the new stepped rows | Unchanged rule (`MIDI_SPEC.md` §5.5): identity, clamped to the row's maximum, so CC 0..6 select the head sets and 7..127 hold `ABC`, as for every selector |
| SD10 | CrumpBit `rvb` damping on the full row | `crumpBit_toneK[ton >> 1]`; 126 and 127 are both exactly flat |
| SD11 | T3 frees id 1 | The ids are renumbered (CrumpBit 1, Delay 2, Reverb 3, Echo 4, count 5). Ids index the registry only; tokens are the stored identity (`EffectsManager.h:61–71`) |

---

## 1. Net resources (estimates; measured at S11)

| Resource | Change |
|---|---|
| Flash | T3 −3.7 KB (`SVF_calcBlockZDFFloat` 3,076 B; StereoFilter ≈ 650 B); T2 formatter ≈ −0.5 KB; C3 table +512 B, generator none; C5 ≈ +0.3 KB. Net ≈ −3.4 KB |
| ITCM | 0 |
| DTCM | `SpaceRuntime` −4 B inside the 100 B union (the union and the arena do not move); `space_hdr_t` −4 B inside the 448 B prefix |
| SRAM1 `.bss` | **+8 B** (SD8, two floats in `menu.c`). Sign-off needed before S10 |
| CPU per sample | 0. T1 keeps the dry term (plan D-a); C3 keeps the curve's operation count (S9 gate); C1, C2, C5 are per block or foreground |
| CPU per block | Space: three fewer multiplies in the tape/plate set-up; CrumpBit `rvb`: a shift in place of a compare-select; C1: two constants |

---

## 2. Order and gates

| Step | What | Gate |
|---|---|---|
| S0 | Baselines | records in `tools/dsp_test/baselines/s095p4/before/` |
| S1 | T3: remove StereoFilter | build; DEV registry self-check 0; host `all`; link budget −3.7 KB |
| S2 | T1: Space `mix` removed | build; host `space`; `space_cpu_check` ≤ caps |
| S3 | T2: plain value text | build; host `space`, `crumpbit` |
| S4 | T2: `hed`/`frz`/`rtg` stepped | build; host `space` (SP2, SP3, SP4, SP6 rewritten) |
| S5 | T2: `fbk` unity at 100 | build; host `space` |
| S6 | T2: CrumpBit `rvb` damping | build; host `crumpbit` (CB15(d) rewritten); `crumpbit_cpu_check` ≤ 184 / 204 |
| S7 | C1: compressor curve | build; `elf_p4_check` (the compressor's loop unchanged) |
| S8 | C2: filter drive floor | build; `elf_p4_check` (`SVF_calcBlockZDF` loop unchanged) |
| S9 | C3: distortion makeup | build; host `distortion`, `dist_makeup_table`, `run_modes`; `elf_p4_check` (mixer loops ≤ before) |
| S10 | C5: gain-reduction readout | build; DEV BC18 check passes; device |
| S11 | Gates and measurements | all of §S11 |
| S12 | Device steps (yours) | §S12 |

`elf_p4_check` compares against `P4BASE`; every check in this schedule runs
with `P4BASE=baselines/s095p4/before` (S0 writes it).

---

## S0. Baselines (no source change)

1. `make all && make img` at the root; copy `build/lxr02.elf` to
   `tools/dsp_test/baselines/s095p4/before/lxr02.elf` (the S11 diffs read
   it; it is a record, not a build output).
2. `python3 tools/diagnostic_tools/link_budget.py arm-none-eabi-nm build/lxr02.elf
   > tools/dsp_test/baselines/s095p4/before/link_budget.txt`
   (expected: Flash 720,068, ITCM 15,232, FXBUF 126,656).
3. `make -C tools/dsp_test elf_p4_snapshot P4BASE=baselines/s095p4/before`
   (`osc.txt`, `mixer.txt`).
4. `make -C tools/dsp_test space_cpu_check > tools/dsp_test/baselines/s095p4/before/space_cpu.txt`
   and `crumpbit_cpu_check > …/crumpbit_cpu.txt`.
5. `make -C tools/dsp_test all > tools/dsp_test/baselines/s095p4/before/host_all.txt 2>&1`
   (expected: exit 0).

---

## S1. T3 — remove StereoFilter (`flt`)

### S1.1 DELETE FILE ×4

- `Core/DSP/Effects/StereoFilter/StereoFilterEffect.c`
- `Core/DSP/Effects/StereoFilter/StereoFilterEffect.h`
- `Core/DSP/Effects/StereoFilter/StereoFilterParameters.c`
- `Core/DSP/Effects/StereoFilter/StereoFilterParameters.h`

and the empty directory.

### S1.2 `Makefile`

- **REMOVE** line 42: `          -ICore/DSP/Effects/StereoFilter \`
- **REMOVE** line 123: `  Core/DSP/Effects/StereoFilter/StereoFilterParameters.c \`
- **REMOVE** line 174: `  Core/DSP/Effects/StereoFilter/StereoFilterEffect.c \`
- **REMOVE** lines 285–289 (the comment "StereoFilter DSP runs with the
  fast-math policy …" and its three-line rule).
- **MODIFY** line 291 (`# CrumpBit DSP (S074) runs with the fast-math policy like StereoFilter; its`):

```make
# CrumpBit DSP (S074) runs with the fast-math policy; its
```

### S1.3 `Core/DSP/Effects/EffectsManager.h` lines 61–77: MODIFY

```c
/*
 * Registry ids are append-only within a build; the persisted identity is the
 * three-byte token (AutoSave, `.fx`, MIDI Learn), so ids may be renumbered
 * when a type is removed.
 * EFFECT_TYPE_CRUMPBIT (S074): the first buffer-using type (`cbt`, 8-bit
 * data lines and an 8-bit tape delay in the FxBuffer share).
 * S095 P1: the Space types, clean counterparts to CrumpBit, sharing one
 * kit (Core/DSP/Effects/Space/): EFFECT_TYPE_DELAY (`dly`, true-stereo
 * 16-bit varispeed tape), EFFECT_TYPE_REVERB (`rvb`, half-rate plate with
 * shimmer), EFFECT_TYPE_ECHO (`ech`, a mono tape into the plate).
 * S095 P4 (T3): the dual filter (`flt`, StereoFilter, id 1) is removed and
 * the ids below it close up. A stored `flt` token no longer resolves: a
 * `.fx` file with `type=flt` fails as an invalid Scene, an AutoSave Scene
 * loads its Effect as `off`, a MIDI Learn row on `flt` is dropped (no
 * migration, standing rule).
 */
#define EFFECT_TYPE_CRUMPBIT             1u
#define EFFECT_TYPE_DELAY                2u
#define EFFECT_TYPE_REVERB               3u
#define EFFECT_TYPE_ECHO                 4u
#define EFFECT_TYPE_COUNT                5u
```

What depends on the numbers: the registry array index, the feed control
block's owner stamp (`SK_CTL_MAGIC | id`, a DTCM runtime value, never
stored), `DEV_EFFECT_FORCE_TYPE` (S1.7). Every user names the ids by macro
(checked: `CrumpBitEffect.c`, `SpaceEffect.c`, `EffectsManager.c`, host
`crumpbit.c`, `space.c`), so nothing else changes.

### S1.4 `Core/DSP/Effects/EffectsManager.c`

- **REMOVE** lines 20–21:

```c
#include "StereoFilterParameters.h"
#include "StereoFilterEffect.h"
```

- **REMOVE** line 57 (`    StereoFilterRuntime stereo_filter;`) from
  `effects_runtime_t`. The union stays 100 B (its largest member is
  `CrumpBitRuntime`, asserted below it).
- **REMOVE** lines 97–110: the whole StereoFilter registry row, from
  `    {` after the `off` row's closing `    },` through its own
  `    },` (anchor: `stereoFilter_token3, stereoFilter_abbrev5,`).
- **REMOVE** lines 199–200:

```c
_Static_assert(sizeof(StereoFilterRuntime) <= sizeof(effects_runtime_t),
               "Effect runtime union must hold StereoFilter");
```

### S1.5 `Core/DSPAudio/ResonantFilter.h` lines 121–134: REMOVE

The comment "Float-I/O ZDF block for Effects only (Session 072 step 4)" and
the prototype `void SVF_calcBlockZDFFloat(...)`. Keep one
`//----…` separator between `SVF_calcBlockZDF` and `SVF_recalcFreq`.

### S1.6 `Core/DSPAudio/ResonantFilter.c`

- **REMOVE** lines 383–513: the separator, the comment "Float-I/O
  counterpart of SVF_calcBlockZDF", the `#if USE_SHAPER_NONLINEARITY
  #error` guard and the whole `SVF_calcBlockZDFFloat()` body, to the end of
  the file. The file then ends with `SVF_calcBlockZDF()`'s closing brace at
  line 382. `fastTanh()`, `softClipTwo()` and `svf_padeNum/Den()` stay
  (the int16 path uses them).
- **MODIFY** lines 285–287 (the int16 solver's Affiliates line):

```c
		 * Affiliates: svf_padeNum()/svf_padeDen() and the golden filter
		 *             harness. (The float twin and its Effect client were
		 *             removed in S095 P4 T3.)
```

### S1.7 Comments that name StereoFilter: MODIFY

| File:line | New text |
|---|---|
| `Core/DSP/Effects/FxBuffer.c:425–426` | `* slots 12..15). Without this, a type that exports none (OFF)` / `* would leave the previous owner's wrap domain and gains behind,` |
| `Core/DSP/Effects/Space/SpaceKit.h:351–352` | `* drain), then resets its counters; 0 otherwise (the switch cuts, as` / `* for OFF, SD11). sk_drainStep(): per drain block, scales io by the` |
| `Core/DSP/Effects/Space/SpaceKit.h:379` | `/* Awake at full return (every runtime switch: OFF never writes the block). */` |
| `Core/DSP/Effects/CrumpBit/CrumpBitParameters.h:191` | `* the offset the voice filter rows apply (`svf_type = value + 1`).` |
| `Core/DSP/Effects/CrumpBit/CrumpBitParameters.h:200` | `* Affiliates: MENU_FILTER, ResonantFilter.h filterTypeEnum.` |
| `Core/DSP/Effects/CrumpBit/CrumpBitParameters.c:68–70` | `*   Pattern-automatable but not LFO-modulatable, like a voice filter's` / `*   `typ` (user U2); `frq` and `res` are full image rows. Defaults are` / `*   frq 64, res 0 and `typ` `off`, so a` |
| `config.h:252` | `* activation. Id 1 is CrumpBit (`cbt`). Production builds ignore this` |

### S1.8 Test card (yours, before the card meets the new image)

Change `type=flt` to `type=off` in the three files (§3.3 of the plan):
`SD_CARD/Bank/000 FluxTime/11 Eris/effects.fx`,
`SD_CARD/Bank/002 FluxTim2/11 Eris/effects.fx`,
`SD_CARD/Bank/027 NoBankOk/11 Eris/effects.fx`. Their `filter_*` keys are
then unknown to `off` and skipped on load.

### S1 gate

`make all` (no warning about an unused `fastTanh`/`softClipTwo`), a DEV
build boots with `effects_registryCheckResult()` 0, `make -C tools/dsp_test
all` exits 0, link budget flash about −3.7 KB.

---

## S2. T1 — the Space types lose `mix`

### S2.1 `Core/DSP/Effects/Space/SpaceParameters.h` lines 31–88: MODIFY (the three enums)

```c
/*
 * S095 P4 (T1): the Space types are 100 % wet. The Effect is a send bus, so
 * the voices' dry signal already reaches the outputs through the mixer; the
 * former `mix` row (index 3) added the bus input back and is gone. Every
 * row below moved down one index: stored Pattern automation, LFO targets
 * and AutoSave bytes on Space rows play one row off (no migration); `.fx`
 * and MIDI Learn store keys and are unaffected.
 */
typedef enum {
    SPACE_DLY_TIME = EFFECT_COMMON_PARAM_COUNT,  /* 3  CrumpBit's `dly` curve, share-truncated (D3) */
    SPACE_DLY_SPREAD,     /* 4  R/L time ratio 2^((spr-63)/63); the longer is `tim` */
    SPACE_DLY_FEEDBACK,   /* 5  0..1.20, unity at 100 (S095 P4) */
    SPACE_DLY_XFEED,      /* 6  L/R feedback cross-feed, continuous (D23) */
    SPACE_DLY_TONE,       /* 7  loop tone: LP < 63 = flat < HP */
    SPACE_DLY_DEGRADE,    /* 8  drive, head bandwidth, hiss (D5) */
    SPACE_DLY_WOW,        /* 9  wow + flutter + drift depth */
    SPACE_DLY_HOLD,       /* 10 on: input 0, loop gain 1 (D10) */
    SPACE_DLY_REVERSE,    /* 11 reverse playback head (O4) */
    SPACE_DLY_HEADS,      /* 12 seven head sets, stepped 0..6 (O6, S095 P4) */
    SPACE_DLY_DUCK,       /* 13 wet ducked by the input (O1) */
    SPACE_DELAY_PARAM_ENUM_COUNT
} space_delay_param_t;
#define SPACE_DELAY_PARAM_COUNT   14u

typedef enum {
    SPACE_RVB_PREDELAY = EFFECT_COMMON_PARAM_COUNT,  /* 3  0..1.4 s, share-truncated */
    SPACE_RVB_SIZE,       /* 4  0.35..1 x, glides (D12) */
    SPACE_RVB_DECAY,      /* 5  RT60 0.3..20 s */
    SPACE_RVB_DIFFUSION,  /* 6  input allpass 0.5..0.75 */
    SPACE_RVB_LOW_SHELF,  /* 7  low-band decay x0.5..x2 (D7) */
    SPACE_RVB_LOW_FREQ,   /* 8  100..600 Hz */
    SPACE_RVB_HIGH_SHELF, /* 9  high-band damping x1..x0.1 */
    SPACE_RVB_HIGH_FREQ,  /* 10 1.5..9 kHz */
    SPACE_RVB_MOD,        /* 11 tank modulation depth */
    SPACE_RVB_WIDTH,      /* 12 M/S width of the taps */
    SPACE_RVB_FREEZE,     /* 13 off / on / on with shimmer, stepped 0..2 (O12) */
    SPACE_RVB_SHIMMER,    /* 14 shimmer feed amount */
    SPACE_RVB_INTERVAL,   /* 15 -24..+24 st, rows 39..87 (63 = 0) */
    SPACE_RVB_DETUNE,     /* 16 0..50 cents, two voices +-dtn */
    SPACE_RVB_ENV,        /* 17 bloom < 63 = off < gate (O10) */
    SPACE_RVB_DUCK,       /* 18 (O11) */
    SPACE_RVB_TONE,       /* 19 output one-pole LP per side, 127 = flat */
    SPACE_REVERB_PARAM_ENUM_COUNT
} space_reverb_param_t;
#define SPACE_REVERB_PARAM_COUNT  20u

typedef enum {
    SPACE_ECH_TIME = EFFECT_COMMON_PARAM_COUNT,  /* 3  the Delay's law, mono tape */
    SPACE_ECH_FEEDBACK,   /* 4  as the Delay's */
    SPACE_ECH_TONE,       /* 5 */
    SPACE_ECH_DEGRADE,    /* 6 */
    SPACE_ECH_WOW,        /* 7 */
    SPACE_ECH_TO_REVERB,  /* 8  `e>r`: the amount between tape and plate */
    SPACE_ECH_BLEND,      /* 9  `bln`: echo .. plate in the wet */
    SPACE_ECH_DECAY,      /* 10 plate RT60 */
    SPACE_ECH_LOW_SHELF,  /* 11 */
    SPACE_ECH_HIGH_SHELF, /* 12 */
    SPACE_ECH_HEADS,      /* 13 (O15), stepped 0..6 */
    SPACE_ECH_DUCK,       /* 14 (O14) */
    SPACE_ECH_ROUTING,    /* 15 e>r / r>e / plate in the loop, stepped 0..2 (O16) */
    SPACE_ECHO_PARAM_ENUM_COUNT
} space_echo_param_t;
#define SPACE_ECHO_PARAM_COUNT    16u
```

(The `fbk` and stepped-row comments above already say what S4 and S5
make true; S2 alone leaves `hed` zoned and unity at 106 until those steps.)

### S2.2 `Core/DSP/Effects/Space/SpaceParameters.c`

- **MODIFY** lines 46–50 (the Delay rows' comment):

```c
/*
 * Delay rows (plan §4.1; D3, D4, D5, D10, D23; options O1, O3, O4, O6, O8,
 * O9). 100 % wet since S095 P4 T1 (the Effect is a send; the level is the
 * common `vol`). Defaults: a musical starting point (time row 64 = a
 * StepScale stop, feedback 64, flat tone, clean tape).
 */
```

- **REMOVE** lines 54–55 (`dly_mix`), lines 94–95 (`rvb_mix`), lines
  146–147 (`ech_mix`): each two-line `EFFECT_ROW("…_mix", …)`.

### S2.3 `Core/DSP/Effects/Space/SpaceEffect.h`

- **MODIFY** lines 42–43:

```c
/* Raw rows 3.. of the largest type (Reverb, 17 rows since S095 P4 T1). */
#define SPACE_ROWS_MAX           17u
```

- **MODIFY** lines 63–65 (the runtime comment):

```c
 * - wet (the wet gain the next block ramps from: the ducking, and on the
 *   Reverb the gate/bloom; S095 P4: no `mix` row, so 1 is the top),
 *   dry (the dry gain, gliding from the outgoing type's to 0; it exists
 *   only for the type-change handoff, S095 P4 D-a), duck (the input
 *   follower, per block);
```

- **MODIFY** line 84: `    float    wet;` (was `mix`, SD4).

### S2.4 `Core/DSP/Effects/Space/SpaceEffect.c`

**(a) MODIFY lines 87–95** (the tape coefficients' comment) and **REMOVE
line 107** (`    float mix;`):

```c
/*
 * The tape's coefficients (write_param, from the rows' laws): fbk (D4), spr
 * (D23), bw (the playback LP at speed 1), comp (O8: its level-dependent
 * loss), tlp/thp (`ton`: below 63 the playback LP's cutoff ceiling, above
 * 63 a one-pole high-pass; flat at 63: 1 and 0), drive/makeup/hiss (`deg`,
 * D5), hg[3] (the heads D/3, 2D/3, D, O6, normalised), wow/dck/xfb (the
 * linear rows: wow depth, ducking, cross-feed). S095 P4 T1: no wet mix;
 * the Space types are 100 % wet.
 */
```

**(b) MODIFY lines 1082–1110** (the law enum and table: `SPACE_LAW_MIX`
goes and each row loses its first entry; the lane change, SD1, is in the
registry, S2.5):

```c
/* The laws a row drives (write_param), per type in row order (3..). */
enum {
    SPACE_LAW_NONE = 0, SPACE_LAW_TIME, SPACE_LAW_SPR,
    SPACE_LAW_FBK, SPACE_LAW_XFB, SPACE_LAW_TONE, SPACE_LAW_DEG,
    SPACE_LAW_WOW, SPACE_LAW_HOLD, SPACE_LAW_REV,
    SPACE_LAW_HED, SPACE_LAW_DCK, SPACE_LAW_PRE, SPACE_LAW_SIZ,
    SPACE_LAW_DCY, SPACE_LAW_DIF, SPACE_LAW_LSH, SPACE_LAW_LSF,
    SPACE_LAW_HSH, SPACE_LAW_HSF, SPACE_LAW_MOD, SPACE_LAW_WID,
    SPACE_LAW_FRZ, SPACE_LAW_SHM, SPACE_LAW_INT, SPACE_LAW_ENV,
    SPACE_LAW_E2R, SPACE_LAW_BLN, SPACE_LAW_RTG, SPACE_LAW_RTONE
};

/*
 * Each type's rows 3.. in order (S095 P4 T1: the `mix` column is gone, so
 * column 0 is now row 3, `tim` / `pre`).
 */
static const uint8_t space_laws[3][SPACE_ROWS_MAX] = {
    /* Delay: tim spr fbk xfb ton deg wow hld rev hed dck */
    { SPACE_LAW_TIME, SPACE_LAW_SPR, SPACE_LAW_FBK, SPACE_LAW_XFB,
      SPACE_LAW_TONE, SPACE_LAW_DEG, SPACE_LAW_WOW, SPACE_LAW_HOLD,
      SPACE_LAW_REV, SPACE_LAW_HED, SPACE_LAW_DCK },
    /* Reverb: pre siz dcy dif lsh lsf hsh hsf mod wid frz shm int dtn env dck ton */
    { SPACE_LAW_PRE, SPACE_LAW_SIZ, SPACE_LAW_DCY, SPACE_LAW_DIF,
      SPACE_LAW_LSH, SPACE_LAW_LSF, SPACE_LAW_HSH, SPACE_LAW_HSF,
      SPACE_LAW_MOD, SPACE_LAW_WID, SPACE_LAW_FRZ, SPACE_LAW_SHM,
      SPACE_LAW_INT, SPACE_LAW_INT, SPACE_LAW_ENV, SPACE_LAW_DCK,
      SPACE_LAW_RTONE },
    /* Echo: tim fbk ton deg wow e>r bln dcy lsh hsh hed dck rtg */
    { SPACE_LAW_TIME, SPACE_LAW_FBK, SPACE_LAW_TONE, SPACE_LAW_DEG,
      SPACE_LAW_WOW, SPACE_LAW_E2R, SPACE_LAW_BLN, SPACE_LAW_DCY,
      SPACE_LAW_LSH, SPACE_LAW_HSH, SPACE_LAW_HED, SPACE_LAW_DCK,
      SPACE_LAW_RTG },
};
```

The `_Static_assert((int)SPACE_ECH_TIME == (int)SPACE_DLY_TIME, …)` at line
1112 stays true (both 3).

**(c) REMOVE line 1160** (`    case SPACE_LAW_MIX: k->mix = g; break;`).

**(d) MODIFY lines 1294–1296** (`space_enter()`, SD2):

```c
    /*
     * The dry starts at the outgoing type's (plan §5.3 step 5) and glides
     * to 0. S095 P4 (SD2): with no handoff written (OFF, boot) it starts
     * at 0, because OFF's return is silent; before T1 it started at 1 and
     * glided to 1 - mix, a step up to the whole dry bus on entry.
     */
    rt->dry = (h && (h->state_flags & FXBUF_STATE_EFFECT_WRITTEN))
                  ? (float)h->effect_dry_uq15 * (1.0f / 32768.0f) : 0.0f;
```

**(e) MODIFY line 1451 and lines 1474–1477** (`space_export()`, SD3). In
the Reverb branch, the `sk_exportBegin(...)` dry argument
`1.0f - space_hdr(&share)->k.mix` becomes `rt->dry`; in the tape branch,
the argument

```c
                       1.0f - ((share.base && share.bytes >= SK_PREFIX_BYTES)
                                   ? space_hdr(&share)->k.mix : 0.0f),
```

becomes `rt->dry,`. Add above the Reverb branch's call:

```c
        /*
         * S095 P4 (SD3): the dry handed over is the glide's current value
         * (0 once settled; non-zero only right after a switch into this
         * type), so a switch during the glide continues it rather than
         * stepping. Before T1 this was the target 1 - mix.
         */
```

**(f) MODIFY lines 1507–1508** (the comment of `space_tapeBegin()`, step 3):

```c
 * 3. the output ramps: dry from the outgoing type's to 0 (plan §5.3 step
 *    5; S095 P4 T1: only the handoff uses it); wet = 1 x the ducking (O1).
```

**(g) MODIFY lines 1587–1594** (`space_tapeBegin()`):

```c
    /*
     * S095 P4 T1: the dry glides to 0 (one-pole, 0.1 per block, as before;
     * -20 dB after about 22 blocks, 16 ms) and the wet is the ducking alone.
     * The output loop still runs `in x dry` every frame (constant cost,
     * plan D-a); dry is 0 once the handoff has faded.
     */
    dry1 = 0.9f * rt->dry;
    wg1 = 1.0f - rt->duck * h->k.dck;
    ps->dry = rt->dry;
    ps->ddry = (dry1 - rt->dry) * inv;
    ps->wg = rt->wet;
    ps->dwg = (wg1 - rt->wet) * inv;
    rt->dry = dry1;
    rt->wet = wg1;
```

**(h) MODIFY lines 1673–1678** (the comment of `space_plateGains()`) and
**lines 1699–1700**:

```c
/*
 * The block's wet gain shared by the plate types: the ducking (O1), the
 * Reverb's gate/bloom (O10: a transient is the fast follower above twice
 * the slow one; gate: 1 for its time after it, then 0; bloom: rising to 1
 * over its time), the dry glide to 0 (S095 P4 T1). Returns the block-end
 * wet gain (1 at most: no `mix` row).
 */
```

```c
    *dry1 = 0.9f * rt->dry;
    return pl->env_g * (1.0f - rt->duck * h->k.dck);
```

**(i) MODIFY lines 1777–1780** (`space_reverbProcess()`): `rt->mix` →
`rt->wet` in the four places (`rt->mix,` and `(wg1 - rt->mix)` in the call,
`rt->mix = wg1;`).

**(j) MODIFY lines 1652–1653, 1781–1782, 1878–1879**: each
`sk_feedMix(…, h->k.mix, h->k.mix);` becomes

```c
    /* S095 P4 T1: the outgoing tail is heard at full wet (no `mix` row). */
    sk_feedMix(io->share, EFFECT_TYPE_DELAY, io->l, io->r, io->frames,
               1.0f, 1.0f);
```

(with `EFFECT_TYPE_REVERB` / `EFFECT_TYPE_ECHO` in the other two).

### S2.5 `Core/DSP/Effects/EffectsManager.c` Space lane rows: MODIFY

**Lines 144–154** (the comment above the Space rows):

```c
    /*
     * S095 P1: Delay (`dly`), Reverb (`rvb`), Echo (`ech`). Stereo in and
     * out. Every one is an elastic buffer user (D2): it plays in the whole
     * usable share above the arena prefix (§S18). buffer_min_bytes is the
     * minimum share, buffer_pref_bytes the arena (declarative, as
     * CrumpBit's). No sub-types: one fixed lane row each (plan §4, lanes
     * 14/15 = the common vol and pan). S095 P4 T1 (SD1): the `mix` row is
     * gone; lanes 2..15 keep their rows, so FX-lane data on them keeps its
     * meaning, and lane 1 takes the one row that had no lane (Reverb `siz`,
     * Echo `hsh`; the Delay has none, so its lane 1 is empty). The default
     * layout places rows 3.. on SELECT 2.. (menuEffects.c, no
     * select_layout); each type's ui hooks label its times and stepped rows
     * (one hook set per type: format_value3 receives no type id).
     */
```

**Line 160** (Delay): `SPACE_DLY_MIX,` → `EFFECT_LANE_NONE,`.
**Line 174** (Reverb): `SPACE_RVB_MIX,` → `SPACE_RVB_SIZE,`.
**Line 188** (Echo): `SPACE_ECH_MIX,` → `SPACE_ECH_HIGH_SHELF,`.

The self-check (`effects_registrySelfCheck()`, codes 5 and 6) accepts a
`NONE` lane and requires no row twice; `siz` and `hsh` appear nowhere else
in their rows.

### S2.6 Host: `tools/dsp_test/modules/space.c` (row tables index by position)

- **MODIFY lines 157–161:**

```c
/* tim spr fbk xfb ton deg wow hld rev hed dck (S095 P4: no `mix`) */
#define SP_DLY_ROWS 11u
static const uint8_t sp_dlyRows[SP_DLY_ROWS] = { 64u, 63u, 0u, 0u, 63u,
                                                 0u, 0u, 0u, 0u,
                                                 SPACE_HEADS_DEFAULT, 0u };
```

- **MODIFY lines 253–256:**

```c
/* pre siz dcy dif lsh lsf hsh hsf mod wid frz shm int dtn env dck ton */
#define SP_RVB_ROWS 17u
static const uint8_t sp_rvbRows[SP_RVB_ROWS] = { 0u, 96u, 64u, 96u, 63u,
                                                 40u, 40u, 80u, 32u, 63u, 0u,
                                                 0u, 75u, 0u, 63u, 0u, 127u };
```

- **MODIFY lines 307–311:**

```c
/* tim fbk ton deg wow e>r bln dcy lsh hsh hed dck rtg */
#define SP_ECH_ROWS 13u
static const uint8_t sp_echRows[SP_ECH_ROWS] = { 64u, 64u, 63u, 0u, 0u,
                                                 64u, 48u, 48u, 63u, 40u,
                                                 SPACE_HEADS_DEFAULT, 0u, 0u };
```

- **MODIFY** the positional writes (each index one lower; the row named in
  the comment is unchanged):

| Line | Was | Becomes | Row |
|---|---|---|---|
| 182 | `rows[2] = pass ? 126u : 63u;` | `rows[1] = …` | `spr` |
| 223 | `rows[1] = 40u;` | `rows[0] = 40u;` | `tim` |
| 224 | `rows[3] = 127u;` | `rows[2] = 127u;` | `fbk` |
| 225 | `rows[4] = 64u;` | `rows[3] = 64u;` | `xfb` |
| 226 | `rows[6] = 127u;` | `rows[5] = 127u;` | `deg` |
| 227 | `rows[7] = 127u;` | `rows[6] = 127u;` | `wow` |
| 228 | `rows[8] = 1u;` | `rows[7] = 1u;` | `hld` |
| 292 | `rows[11] = 127u;` | `rows[10] = …` (S4 sets the value) | `frz` |
| 293 | `rows[12] = 127u;` | `rows[11] = 127u;` | `shm` |
| 328 | `rows[13] = …` | `rows[12] = …` (S4 sets the value) | `rtg` |
| 375 | `rows[3] = 0u;` | `rows[2] = 0u;` | `dcy` |
| 457 | `rows[1] = lim;` | `rows[0] = lim;` | `tim` |
| 458 | `rows[2] = pass ? 127u : 0u;` | `rows[1] = …` | `spr` |
| 493 | `rows[1] = 40u;` | `rows[0] = 40u;` | `tim` |
| 494 | `rows[3] = 127u;` | `rows[2] = 127u;` | `fbk` |
| 530 | `rows[17] = pass ? 0u : 127u;` | `rows[16] = …` | `ton` |

- **MODIFY** the comments at lines 88–89 (`… with the Reverb's mix up its
  output carries the tail …` → `… at full wet its output carries the tail
  …`) and line 534 (`/* After the dry glide to 1 - mix = 0 (no dry impulse). */`
  → `/* No dry: the entry starts at 0 (SD2) and no mix row exists. */`).

### S2 gate

`make all`; `make -C tools/dsp_test space` (SP1–SP10 pass);
`make -C tools/dsp_test space_cpu_check` at or below the S0 figures (the
loops are untouched; the per-block set-up loses three multiplies).

---

## S3. T2 — plain value text

### S3.1 `Core/DSP/Effects/Space/SpaceParameters.c`: MODIFY lines 197–351

Replace from the comment above the row-kind enum (`/*` at line 197,
"Row kinds of the page text") through the end of `space_formatValue()`
(line 351) with the block below. Lines 181–195 (the descriptor-count
`_Static_assert`s) stay.

```c
/*
 * Row kinds of the page text (one per row 3..; SPACE_TXT_NONE: the plain
 * dtype text, 0..127 or +-63).
 *
 * S095 P4 (T2, user 2026-11-13): only the rows that need a label to be
 * played keep one: `tim` (the synced stops and the clamped row), and the
 * stepped rows `hed`, `frz`, `rtg` (names, not numbers). `spr`, `fbk`,
 * `xfb`, `dck`, `pre`, `dcy`, `int`, `dtn`, `env` show plain numbers; their
 * laws are unchanged, so the number is a position on the same curve.
 */
enum {
    SPACE_TXT_NONE = 0,
    SPACE_TXT_TIME,      /* `tim`: the row the DSP plays, CrumpBit's labels (D3) */
    SPACE_TXT_HEADS,     /* `hed`: the head set (O6), letters (S095 P4) */
    SPACE_TXT_FREEZE,    /* `frz`: off / on / shm (O12) */
    SPACE_TXT_ROUTE      /* `rtg`: e>r / r>e / lp (O16) */
};

/* tim spr fbk xfb ton deg wow hld rev hed dck */
static const uint8_t space_delayText[SPACE_DELAY_PARAM_COUNT - 3u] = {
    SPACE_TXT_TIME, SPACE_TXT_NONE, SPACE_TXT_NONE, SPACE_TXT_NONE,
    SPACE_TXT_NONE, SPACE_TXT_NONE, SPACE_TXT_NONE, SPACE_TXT_NONE,
    SPACE_TXT_NONE, SPACE_TXT_HEADS, SPACE_TXT_NONE
};

/* pre siz dcy dif lsh lsf hsh hsf mod wid frz shm int dtn env dck ton */
static const uint8_t space_reverbText[SPACE_REVERB_PARAM_COUNT - 3u] = {
    SPACE_TXT_NONE, SPACE_TXT_NONE, SPACE_TXT_NONE, SPACE_TXT_NONE,
    SPACE_TXT_NONE, SPACE_TXT_NONE, SPACE_TXT_NONE, SPACE_TXT_NONE,
    SPACE_TXT_NONE, SPACE_TXT_NONE, SPACE_TXT_FREEZE, SPACE_TXT_NONE,
    SPACE_TXT_NONE, SPACE_TXT_NONE, SPACE_TXT_NONE, SPACE_TXT_NONE,
    SPACE_TXT_NONE
};

/* tim fbk ton deg wow e>r bln dcy lsh hsh hed dck rtg */
static const uint8_t space_echoText[SPACE_ECHO_PARAM_COUNT - 3u] = {
    SPACE_TXT_TIME, SPACE_TXT_NONE, SPACE_TXT_NONE, SPACE_TXT_NONE,
    SPACE_TXT_NONE, SPACE_TXT_NONE, SPACE_TXT_NONE, SPACE_TXT_NONE,
    SPACE_TXT_NONE, SPACE_TXT_NONE, SPACE_TXT_HEADS, SPACE_TXT_NONE,
    SPACE_TXT_ROUTE
};

/* The stepped rows' names (O12, O16). */
static const char space_frzNames[3][4] = { "off", " on", "shm" };
static const char space_rtgNames[3][4] = { "e>r", "r>e", "lp " };

/*
 * The seven `hed` stops in knob order (O6; S095 P4 Q3: letters, one per
 * head, A = D/3, B = 2D/3, C = D, so a label cannot be read as a number or
 * a knob position). The order is the one §S18 settled: single heads, then
 * pairs, then all three.
 */
static const char space_hedNames[SPACE_HEADS_STOPS][4] = {
    "  A", "  B", "  C", " AB", " AC", " BC", "ABC"
};

/*
 * The value text of a Space row (S095 P1 plan §6 item 8; S095 P4 T2).
 * What: out[0..2] for the row's kind (above); the stepped kinds also when
 * `target` (the STEP automation page and Pattern entry), where a name is
 * still clearer than 0..6. The `tim` text is the row the DSP plays: the
 * value clamped first to the live tape's limit (space_timeLimit(), the
 * clamp effects_service() applies).
 * Inputs: the type's kind table and tag, row index, value, out, target.
 * Output: nonzero when out was written (0: the generic dtype text).
 * Foreground. Callers: the three types' format hooks below.
 */
static __attribute__((optimize("Os"))) uint8_t space_formatValue(const uint8_t *kinds, uint8_t count,
                                 uint8_t tag, uint8_t index, uint8_t value,
                                 char *out, uint8_t target)
{
    uint8_t kind;

    if (index < EFFECT_COMMON_PARAM_COUNT ||
        index - EFFECT_COMMON_PARAM_COUNT >= count)
        return 0u;
    kind = kinds[index - EFFECT_COMMON_PARAM_COUNT];
    if (target && kind == SPACE_TXT_TIME)
        return 0u;
    switch (kind) {
    case SPACE_TXT_TIME: {
        fx_share_t share;
        uint8_t limit;

        fxbuf_effectShare(&share);
        limit = space_timeLimit(tag, &share);
        crumpBit_formatDelayRow((value > limit) ? limit : value, out);
        return 1u;
    }
    case SPACE_TXT_HEADS:
        memcpy(out, space_hedNames[space_step(value, SPACE_HEADS_STOPS)], 3u);
        return 1u;
    case SPACE_TXT_FREEZE:
        memcpy(out, space_frzNames[space_step(value, 3u)], 3u);
        return 1u;
    case SPACE_TXT_ROUTE:
        memcpy(out, space_rtgNames[space_step(value, 3u)], 3u);
        return 1u;
    default:
        return 0u;
    }
}
```

`space_tenths()` (lines 250–257) is removed with the block. `space_step()`
is added in S4.2; S3 and S4 land together, or S3 keeps `space_stop()` at
these three calls until S4.

**MODIFY lines 389–393** (the hooks' comment): `"… no label variants."` →
`"… no label variants. S095 P4: values only for `tim` and the stepped
rows."`

### S3.2 `Core/DSP/Effects/CrumpBit/CrumpBitParameters.c`

**(a) REMOVE lines 486–500**: the comment "Signed semitones of a bipolar
pitch row …" and `crumpBit_formatSemitones()`.

**(b) MODIFY lines 601–611:**

```c
    /*
     * S095 P4 T2: the pitch rows show plain +-63, which is the semitone
     * count already (stored 27..99, 63 = 0, the row stops at +-36, so the
     * old clamp never acted). `aux` keeps `---` where its sub-type does not
     * read it (`dly`, `cmb`, `rvb`): that is the inactive-row marker, as for
     * `snc` and `fin`, not a value text. `p2`/`p3` need no hook.
     */
    if (index == CRUMPBIT_PARAM_AUX &&
        sub != CRUMPBIT_SUBTYPE_PITCH && sub != CRUMPBIT_SUBTYPE_GRAIN) {
        memcpy(out, "---", 3u);
        return 1u;
    }
```

**(c) REMOVE lines 663–667** (the `grn` `fbk` percent branch and its
comment line 663 `/* S091 P5 G4: … */`). The `grn` echo gain law
(`crumpBit_grainFeedbackGain()`, × 0.98/127) is unchanged; the row shows
0..127.

**(d) MODIFY the hook's comment, lines 520–548:** remove the bullets
"`aux` in `pit`/`grn`, and `p2`/`p3` in `grn`: signed semitones …" and
"`fbk` in `grn` (G4, Q7): the echo gain in percent …"; replace the R1
bullet with the S6 text (S6.2 b); add after "Everything else returns 0 …":

```c
 * S095 P4 (T2, user): the pitch rows (`aux`, `p2`, `p3`) and `grn`'s `fbk`
 * show plain numbers; `rte`, `den`, `spn`/`pos` in `grn` and `sub` keep
 * their labels, as do the `---` inactive markers.
```

### S3 gate

`make all`; host `space` and `crumpbit` pass (no host law reads the page
text of these rows).

---

## S4. T2 — `hed`, `frz`, `rtg` as stepped rows

### S4.1 `Core/DSP/Effects/Space/SpaceParameters.h` lines 90–103: MODIFY

```c
/*
 * Stepped rows' values (the DSP and the labels read the same codes).
 *
 * S095 P4 (T2): `hed`, `frz` and `rtg` store the stop itself (0..6, 0..2),
 * one stop per knob detent, as CrumpBit's `sub` and `typ` do. S095 P1 §S18
 * had stored them 0..127 in equal zones (about 18 detents per stop); the
 * label then had no relation to the knob position. A value above the last
 * stop (an older stored byte, a MIDI CC, a Pattern value) plays the last
 * stop: the manager clamps to the descriptor maximum, and space_step()
 * clamps again for a direct write_param.
 */
#define SPACE_FREEZE_OFF          0u
#define SPACE_FREEZE_ON           1u
#define SPACE_FREEZE_SHIMMER      2u   /* O12 */
#define SPACE_FREEZE_STOPS        3u
#define SPACE_HEADS_STOPS         7u   /* hed: A B C AB AC BC ABC */
#define SPACE_HEADS_DEFAULT       2u   /* "  C", D only */
#define SPACE_ROUTING_E2R         0u   /* echo into plate (§4.3) */
#define SPACE_ROUTING_R2E         1u   /* plate into echo */
#define SPACE_ROUTING_LOOP        2u   /* plate inside the echo loop */
#define SPACE_ROUTING_STOPS       3u
```

(Keep `SPACE_INTERVAL_ROW_MIN/MAX` after it unchanged. The host extracts
every macro it already names; the two new ones are not extracted.)

### S4.2 `Core/DSP/Effects/Space/SpaceEffect.h` lines 122–126: MODIFY

```c
/*
 * A stepped row's stop (S095 P4): the stored value is the stop; anything
 * above the last stop plays the last one (the descriptor maximum already
 * clamps every managed write; this covers a direct write_param, the host
 * laws). Replaces S095 P1's space_stop() zones. Callers: the `hed`, `frz`
 * and `rtg` laws (SpaceEffect.c) and labels (SpaceParameters.c).
 */
static inline uint8_t space_step(uint8_t value, uint8_t n)
{
    return (value < n) ? value : (uint8_t)(n - 1u);
}
```

### S4.3 `Core/DSP/Effects/Space/SpaceParameters.c` descriptors: MODIFY

- **Lines 78–82** (`dly_heads`, with its comment) and **lines 169–172** (`ech_heads`):

```c
    /* S095 P4: seven stops 0..6, one per detent (A B C AB AC BC ABC). */
    EFFECT_ROW("dly_heads", "Delay", "Heads", "hed", DTYPE_0B127,
               INSTRUMENT_PARAM_FLAG_MORPHABLE |
               INSTRUMENT_PARAM_FLAG_AUTOMATABLE,
               EFFECT_MOD_NONE, SPACE_HEADS_DEFAULT, SPACE_HEADS_STOPS - 1u),
```

  (`"ech_heads", "Echo"` for the Echo row.)
- **Lines 117–120** (`rvb_freeze`):

```c
    /* S095 P4: three stops 0..2 (off on shm). */
    EFFECT_ROW("rvb_freeze", "Reverb", "Freeze", "frz", DTYPE_0B127,
               INSTRUMENT_PARAM_FLAG_MORPHABLE |
               INSTRUMENT_PARAM_FLAG_AUTOMATABLE,
               EFFECT_MOD_NONE, SPACE_FREEZE_OFF, SPACE_FREEZE_STOPS - 1u),
```

- **Lines 175–178** (`ech_routing`): the same form, `"ech_routing", "Echo",
  "Routing", "rtg"`, default `SPACE_ROUTING_E2R`, maximum
  `SPACE_ROUTING_STOPS - 1u`, comment `/* S095 P4: three stops 0..2 (e>r r>e lp). */`.

The registry self-check (code 7) requires default ≤ maximum: 2 ≤ 6, 0 ≤ 2.

### S4.4 `Core/DSP/Effects/Space/SpaceEffect.c` laws: MODIFY

- **Lines 1186–1192** (`SPACE_LAW_HED`): the comment becomes

```c
        /*
         * O6: the stops' head sets in knob order A B C AB AC BC ABC (bit 0
         * D/3, bit 1 2D/3, bit 2 D; S095 P4: the stored value is the stop).
         */
```

  and `sets[space_stop(value, SPACE_HEADS_STOPS)]` becomes
  `sets[space_step(value, SPACE_HEADS_STOPS)]`.
- **Line 1205** (`SPACE_LAW_RTG`): `space_stop(value, 3u)` →
  `space_step(value, SPACE_ROUTING_STOPS)`.
- **Lines 1225–1229** (`SPACE_LAW_FRZ`):

```c
    case SPACE_LAW_FRZ: {
        /* Three stops: off, on, on with the shimmer (O12; S095 P4 stepped). */
        const uint8_t s = space_step(value, SPACE_FREEZE_STOPS);

        pl->frz = (s != SPACE_FREEZE_OFF) ? 1.0f : 0.0f;
        space_flag(rt, SPACE_F_FRZ_SHM, s == SPACE_FREEZE_SHIMMER);
        break;
    }
```

### S4.5 Host `tools/dsp_test/modules/space.c`

- **Line 83** (header comment): `` `hed` steps in knob order `` →
  `` `hed` steps 0..6 in knob order (S095 P4) ``.
- **Lines 232–233** (SP2): the heads write becomes
  `(uint8_t)((b >> 9) % SPACE_HEADS_STOPS)`.
- **Line 292** (SP3, S2 moved it to `rows[10]`):
  `rows[10] = SPACE_FREEZE_SHIMMER;  /* frz: freeze with the shimmer */`.
- **Lines 295, 298** (SP3): `SPACE_RVB_FREEZE, 0u` stays (`SPACE_FREEZE_OFF`);
  `SPACE_RVB_FREEZE, 127u` → `SPACE_RVB_FREEZE, SPACE_FREEZE_SHIMMER`.
- **Line 328** (SP4, now `rows[12]`): `rows[12] = (uint8_t)route;   /* rtg stops 0, 1, 2 */`.
- **Lines 397–428** (SP6): MODIFY

```c
/*
 * SP6 (§S18; S095 P4): `hed` steps A B C AB AC BC ABC for the stored
 * values 0..6, in order, each its own head set; a value above 6 plays ABC;
 * the default is C (D only).
 */
static void law_sp6(void)
{
    static const uint8_t sets[SPACE_HEADS_STOPS] = { 1u, 2u, 4u, 3u, 5u, 6u, 7u };
    uint32_t v;
    uint8_t seen = 0u;
    int ok = 1;
    char msg[120];

    sp_enter(&space_delayOps, NULL, sp_dlyRows, SP_DLY_ROWS);
    for (v = 0u; v <= SPACE_HEADS_STOPS; v++) {
        /* v == SPACE_HEADS_STOPS is one past the last stop: it plays ABC. */
        const uint8_t st = (v < SPACE_HEADS_STOPS) ? (uint8_t)v
                                                   : (uint8_t)(SPACE_HEADS_STOPS - 1u);
        fx_share_t sh;
        const space_tapeK_t *k;
        uint8_t mask;

        space_delayOps.write_param(&sp_rt, SPACE_DLY_HEADS, (uint8_t)v);
        fxbuf_effectShare(&sh);
        k = &space_hdr(&sh)->k;
        mask = (uint8_t)((k->hg[0] > 0.0f) | ((k->hg[1] > 0.0f) << 1) |
                         ((k->hg[2] > 0.0f) << 2));
        ok = ok && mask == sets[st];
        seen |= (uint8_t)(1u << st);
    }
    snprintf(msg, sizeof msg, "SP6 hed: stops %d, seen 0x%02x, default %u",
             ok, seen, (unsigned)SPACE_HEADS_DEFAULT);
    expect(ok && seen == 0x7Fu && SPACE_HEADS_DEFAULT == 2u, msg);
}
```

### S4 gate

`make all` (self-check code 7 holds); host `space` (SP2, SP3, SP4, SP6).

---

## S5. T2 — feedback unity at row 100

### S5.1 `Core/DSP/Effects/Space/SpaceEffect.c` lines 200–205: MODIFY

```c
/*
 * The tape loop gain of an `fbk` row (D4; S095 P4, user 2026-11-13): linear
 * to unity at row 100, then to 1.20 at 127, CrumpBit's law
 * (CRUMPBIT_FEEDBACK_UNITY_ROW, CRUMPBIT_FEEDBACK_ABOVE_STEP), so every
 * feedback that can pass unity reaches it at the same row. Unity stays
 * unmarked on the page (the row shows 0..127). Before P4 unity was row 106.
 * Callers: space_writeParam() (SPACE_LAW_FBK). The page no longer calls it
 * (S095 P4 T2).
 */
SPACE_SIZE float space_fbkGain(uint8_t row)
{
    return (row <= 100u) ? (float)row * (1.0f / 100.0f)
                         : 1.0f + (float)(row - 100u) * (0.2f / 27.0f);
}
```

(`100 × (1/100)` rounds to exactly 1.0f, as CrumpBit's comment notes for
its own law; `127` gives `1 + 27 × 0.2/27 = 1.2f`.)

### S5.2 `Core/DSP/Effects/Space/SpaceEffect.h` lines 117–120: MODIFY

```c
/* The row laws the DSP uses (S095 P4: the page shows these rows plain). */
float space_fbkGain(uint8_t row);      /* 0..1.20, unity at 100 (D4, S095 P4) */
float space_sprRatio(uint8_t row);     /* R/L time 0.5..2 (D23) */
float space_rt60(uint8_t row);         /* `dcy`: 0.3..20 s */
```

### S5 gate

`make all`; host `space` (SP2 runs at `fbk` 127, gain 1.2 unchanged; SP8
`hld` forces unity and does not read the law).

---

## S6. T2 — CrumpBit `rvb` damping on the whole row

### S6.1 `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`

**(a) MODIFY lines 2653–2655** (the `crumpBit_runReverb()` header):

```c
 * input muted); the per-line damping (`ton`; low-pass only, so S095 P4
 * spreads it over the whole row: k = crumpBit_toneK[ton >> 1], 0 the
 * darkest, 126 and 127 an exact pass-through; S091 P5 R1 had used only
 * 0..63 and played 64..127 flat); the
```

**(b) MODIFY lines 2697–2703:**

```c
    /*
     * tone_k (S095 P4, user rule: a low-pass-only `ton` is 0..127 with 127
     * flat): the damping pole reads the `ton` table's low-pass half across
     * the whole row, entry ton >> 1 (0..63). crumpBit_toneK[63] is exactly
     * 1, so ton 126 and 127 give s_k = y_k bit for bit (flat). The row stays
     * the shared bipolar `ton` (the tilt sub-types read it as +-63), so the
     * default 63 is mid damping here. Per block; a shift in place of the
     * old compare-select.
     */
    const float tone_k = crumpBit_toneK[rt->tone_raw >> 1];
```

`tone_raw` is at most 127 (the descriptor maximum), so the index is at
most 63.

### S6.2 `Core/DSP/Effects/CrumpBit/CrumpBitParameters.c`

**(a) MODIFY lines 657–661:**

```c
    /*
     * S095 P4: `rvb` damping (`dmp`) uses the whole row, low-pass only, 127
     * flat (CrumpBitEffect.c crumpBit_runReverb()), so it shows the plain
     * 0..127 position rather than the shared row's +-63.
     */
    if (sub == CRUMPBIT_SUBTYPE_REVERB && index == CRUMPBIT_PARAM_TONE) {
        numtostrpu(out, value, ' ');
        return 1u;
    }
```

**(b) MODIFY lines 542–543** (the hook comment's R1 bullet):

```c
 *   - `ton` (`dmp`) in `rvb` (S095 P4): the plain 0..127 position (the
 *     damping is low-pass only and spans the whole row, 127 flat);
```

### S6.3 Host `tools/dsp_test/modules/crumpbit.c`

- **MODIFY lines 120–121** (CB15 in the header):
  `* within +-6 dB over 1 s; S095 P4: `ton` 2m and 2m+1 play bit-identically`
  / `* (one damping entry per pair), and 126 equals 127 (flat);`
- **MODIFY lines 2473–2520** (CB15(d)):

```c
    /*
     * (d) S095 P4: the damping reads crumpBit_toneK[ton >> 1], so `ton` 2m
     * and 2m+1 play bit-identically whatever the other rows (and 126 / 127
     * are both the exact pass-through). Pass 0 checks 126 against 127; the
     * others a random pair.
     */
    for (pass = 0u; pass < 8u; pass++) {
        static uint8_t arena_r1[2][16384];
        CrumpBitRuntime rr[2];
        fx_share_t sh[2];
        const uint8_t even = pass ? (uint8_t)((rnd() % 64u) * 2u) : 126u;
        const uint8_t fbk = (uint8_t)(rnd() & 0x7Fu);
        const uint8_t rte = (uint8_t)(rnd() & 0x7Fu);
        uint32_t bad = 0u;
        uint32_t block;
        uint32_t k;

        for (k = 0u; k < 2u; k++) {
            memset(arena_r1[k], 0, sizeof arena_r1[k]);
            sh[k].base = arena_r1[k];
            sh[k].offset = 0u;
            sh[k].bytes = sizeof arena_r1[k];
            p3_init(&rr[k], CRUMPBIT_SUBTYPE_REVERB);
            p3_write(&rr[k], CRUMPBIT_PARAM_MIX, 127u, &sh[k]);
            p3_write(&rr[k], CRUMPBIT_PARAM_FEEDBACK, fbk, &sh[k]);
            p3_write(&rr[k], CRUMPBIT_PARAM_RATE, rte, &sh[k]);
            p3_write(&rr[k], CRUMPBIT_PARAM_TONE,
                     (uint8_t)(even + k), &sh[k]);
        }
        /* (the block loop and its comparison, lines 2500–2516, unchanged) */
        …
        snprintf(msg, sizeof msg, "CB15 ton %u = %u: %u samples differ",
                 even, even + 1u, bad);
        expect(bad == 0u, msg);
    }
```

  Keep lines 2500–2516 (the 3,000-block run and `same_bits()` count) as
  they are; only the set-up and the message change.

### S6 gate

`make all`; host `crumpbit` (CB15 passes); `make -C tools/dsp_test
crumpbit_cpu_check` with `rvb` at or below its S0 figure (181.3 steady,
201.3 with the feed; caps 184 / 204).

---

## S7. T4 C1 — the compressor's threshold curve and makeup reference

### S7.1 `Core/DSPAudio/BusCompressor.c` lines 39–40: MODIFY

```c
/*
 * Makeup reference (S095 P4 C1): the detector level whose static gain
 * reduction the makeup restores (scaled by 1 - 0.3a), and the power the cell
 * is seeded with on a fade-in. -20 dBFS (was -12): a clean kit sits around
 * -12 to -20 dBFS on the 5 ms detector, so the makeup now tracks a real bus
 * instead of one 8 dB louder than most Scenes. REF_POWER = 10^(-20/10).
 */
#define BUS_COMP_REF_LEVEL_DB      (-20.0f)
#define BUS_COMP_REF_POWER         0.01f
```

### S7.2 Same file, lines 246–258 (comment) and line 263 (threshold): MODIFY

The comment block above the macro reads becomes:

```c
    /*
     * cam/ctm macro (S074 §4.2/§4.3): threshold, ratio 1..8, makeup, and
     * cubic drive. These are recomputed per block so Scene changes take
     * effect smoothly next block.
     *
     * Tuning revision (user, after hardware listening): makeup is the static
     * reduction at the reference level scaled by (1 - 0.3a), so the level
     * rise tapers off progressively across cam. Drive is 1 + 0.5a^2:
     * unchanged at low cam, ceiling -3.5 dBFS at cam 127. ref_gr_db also
     * seeds the cell on fade-in.
     *
     * S095 P4 C1 (S095_P4_FX_BUS_TWEAKS.md §4): the threshold runs
     * -8 .. -40 dBFS (was -3 .. -30). A clean voice reaches the bus around
     * -10 dBFS peak / -12 dBFS on the 5 ms detector, so the old curve only
     * engaged once distortion lifted the voices; now cam 48 gives about
     * 3-6 dB of reduction on a clean kit, and cam 127 is a deliberate squash.
     * The ratio curve and the drive are unchanged; the makeup reference
     * moved to -20 dBFS (BUS_COMP_REF_LEVEL_DB).
     */
```

and the threshold line (current line 263):

```c
    threshold = -8.0f - 32.0f * a;
```

Per block only; the per-sample loop is untouched.

### S7 gate

`make all`; `make -C tools/dsp_test elf_p4_check P4BASE=baselines/s095p4/before`
(`busComp_processBlock`'s loop unchanged). The host replica (S11.4) checks
the table in the plan's §4.2 C1.

---

## S8. T4 C2 — the voice filter's drive floor

### S8.1 `Core/DSPAudio/ResonantFilter.c` lines 144–152: MODIFY

```c
/*
 * Filter input drive from the `drv` row (Drum, Snare, Cymbal, HiHat;
 * IM_SPECIAL_FILTER_DRIVE).
 *
 * What: the gain in front of the ZDF filter's Pade saturator, 1.0 at drv 0
 * up to 6.4 at drv 127 (1 + 5.4 x^2, x = drv/127).
 * Why (S095 P4 C2): the floor was 0.4, so every filtered voice reached the
 * bus 8 dB below the same voice with the filter `off` (which passes the
 * input unchanged), and a clean kit sat well under the bus compressor's
 * threshold. 1.0 keeps a full-scale oscillator at the saturator's knee
 * (gentle compression of its peaks only) and leaves the top (6.4, +16 dB)
 * where it was. The naive LP2 path reads the same drive.
 * Inputs: drive row 0..127. Output: filter->drive. Per row change; no
 * per-sample cost.
 * Callers: InstrumentManager.c IM_SPECIAL_FILTER_DRIVE.
 */
void SVF_setDrive(ResonantFilter* filter,uint8_t drive)
{
#if USE_SHAPER_NONLINEARITY
	filter->drive = drive;
	setDistortionShape(&filter->shaper, filter->drive);
#else
	filter->drive = 1.0f + (drive/127.f)*(drive/127.f)*5.4f;
#endif
}
```

### S8.2 Same file, line 66 (`SVF_init()`): MODIFY

```c
		/* S095 P4 C2: the drv 0 value of SVF_setDrive() (was 0.5). */
		filter->drive = 1.0f;
```

### S8 gate

`make all`; `elf_p4_check` (`SVF_calcBlockZDF` loop lengths unchanged:
only the drive value differs).

---

## S9. T4 C3 — half makeup on the voice distortion

### S9.1 NEW FILE `tools/plots/gen_dist_makeup.py`

```python
#!/usr/bin/env python3
"""Generate the voice distortion makeup table (S095 P4 C3).

What:       writes Core/DSPAudio/distortionMakeup.c with
            const float distortion_makeup[128]: for each drive row s the
            factor m(s) that takes back SHARE of the level the curve
            y = (1+k)x / (1+k|x|), k = 2(s/128) / (1 - s/128), adds to two
            reference hits; entry 0 is exactly 1.0.
Why:        the Drum/Snare/Cymbal/HiHat distortion had no makeup, so `drv`
            raised a voice's level by up to ~20 dB RMS and was the only way a
            clean kit reached the bus compressor (S095_P4_FX_BUS_TWEAKS.md
            §4). SHARE 0.5 (user, Q6): half of the rise is taken back, so
            distortion still pushes, less.
Reference:  a 55 Hz kick (decay 120 ms) and a 190 Hz + noise snare (decay
            60 ms, noise from a fixed 32-bit LCG), each 0.3 s at REAL_FS,
            peak 0.3 (-10.5 dBFS, a clean voice through the filter at drv 0);
            the rise is the mean of the two RMS ratios in dB.
Checks:     C1 entry 0 == 1.0; C2 non-increasing; C3 every entry in (0, 1];
            C4 --check: regenerate in memory and compare with the committed
            file byte for byte. Exit 1 on any failure.
Outputs:    Core/DSPAudio/distortionMakeup.c, or a pass/fail line with
            --check.
Accessors:  tools/dsp_test/Makefile target dist_makeup_table.
Affiliates: distortion.c setDistortionShape(); distortion.h
            distortion_makeup; tools/dsp_test/modules/distortion.c.
"""
import math
import sys

FS = 44108.07291666667          # config.h REAL_FS
N = int(0.3 * FS)
PEAK = 0.3
SHARE = 0.5
OUT = "Core/DSPAudio/distortionMakeup.c"


def lcg_noise(n):
    s = 0x12345678
    out = []
    for _ in range(n):
        s = (s * 1664525 + 1013904223) & 0xFFFFFFFF
        out.append(((s >> 8) / 8388608.0) - 1.0)
    return out


def norm(v):
    p = max(abs(x) for x in v)
    return [PEAK * x / p for x in v]


def references():
    t = [i / FS for i in range(N)]
    kick = norm([math.sin(2 * math.pi * 55 * ti) * math.exp(-ti / 0.12)
                 for ti in t])
    nz = lcg_noise(N)
    snare = norm([(0.5 * math.sin(2 * math.pi * 190 * ti) + 0.5 * nz[i]) *
                  math.exp(-ti / 0.06) for i, ti in enumerate(t)])
    return kick, snare


def rise_db(sig, k):
    a = sum(x * x for x in sig)
    b = sum(((1 + k) * x / (1 + k * abs(x))) ** 2 for x in sig)
    return 10 * math.log10(b / a)


def table():
    kick, snare = references()
    tab = [1.0]
    for s in range(1, 128):
        k = 2 * (s / 128) / (1 - s / 128)
        r = 0.5 * (rise_db(kick, k) + rise_db(snare, k))
        tab.append(10 ** (-SHARE * r / 20))
    return tab


def render(tab):
    lines = [
        "/*",
        " * Core/DSPAudio/distortionMakeup.c — GENERATED by",
        " * tools/plots/gen_dist_makeup.py (S095 P4 C3). Do not edit by hand.",
        " *",
        " * distortion_makeup[s]: the level factor setDistortionShape() folds",
        " * into the distortion curve for drive row s (half of the curve's RMS",
        " * rise on the reference hits; entry 0 exactly 1).",
        " */",
        '#include "distortion.h"',
        "",
        "const float distortion_makeup[DISTORTION_MAKEUP_COUNT] = {",
    ]
    for i in range(0, 128, 4):
        vals = ", ".join("%.9ef" % v for v in tab[i:i + 4])
        lines.append("    %s,  /* %3d..%3d */" % (vals, i, i + 3))
    lines.append("};")
    return "\n".join(lines) + "\n"


def main():
    tab = table()
    ok = tab[0] == 1.0
    ok = ok and all(tab[i + 1] <= tab[i] for i in range(127))
    ok = ok and all(0.0 < v <= 1.0 for v in tab)
    text = render(tab)
    if "--check" in sys.argv:
        try:
            same = open(OUT).read() == text
        except OSError:
            same = False
        print("dist_makeup_table: %s" % ("ok" if ok and same else "FAILED"))
        return 0 if ok and same else 1
    if not ok:
        print("dist_makeup_table: checks C1-C3 FAILED")
        return 1
    open(OUT, "w").write(text)
    print("wrote %s" % OUT)
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

Run once from the root: `python3 tools/plots/gen_dist_makeup.py`. The
values it writes (run on 2026-11-13, for the record; the file is the
generator's output, never hand-edited):

| s | 0 | 16 | 32 | 48 | 64 | 80 | 96 | 112 | 127 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| m | 1.000000 | 0.901121 | 0.812818 | 0.731885 | 0.655681 | 0.581591 | 0.506147 | 0.422065 | 0.294358 |
| dB | 0 | −0.90 | −1.80 | −2.71 | −3.67 | −4.71 | −5.91 | −7.49 | −10.62 |

### S9.2 NEW FILE `Core/DSPAudio/distortionMakeup.c`

The generator's output (S9.1). Not edited by hand.

### S9.3 `Makefile`: ADD after line 179 (`  Core/DSPAudio/lfoRateTable.c \`)

```make
  Core/DSPAudio/distortionMakeup.c \
```

(The DSP source list, as `lfoRateTable.c`: a const table, `-Ofast` like its
reader.)

### S9.4 `Core/DSPAudio/distortion.h`

**(a) MODIFY lines 44–48** (the struct):

```c
/*
 * Voice distortion state (Drum, Snare, Cymbal, HiHat post-chain).
 *
 * What: shape = k of the curve y = g x / (1 + k|x|); out_gain = g =
 * m(drv) (1 + k), where m is the makeup factor (distortion_makeup[]).
 * Why (S095 P4 C3): folding m into the numerator costs nothing per sample
 * (the numerator was (1 + k), already a per-voice constant), and keeps
 * the curve's shape while lowering its level. out_gain replaces the
 * unused inv_shape (no RAM change). At drv 0, k = 0 and out_gain = 1: the
 * curve is the identity, bit for bit as before.
 * Written by: setDistortionShape() (per row change). Read by: the curve
 * helpers below (per sample).
 */
typedef struct DistStruct
{
	float shape;
	float out_gain;
}Distortion;
```

**(b) ADD after line 54** (after the `distortion_calcSampleFloat`
prototype):

```c
/*
 * Makeup factors per drive row (S095 P4 C3; generated,
 * distortionMakeup.c, tools/plots/gen_dist_makeup.py): half of the curve's
 * RMS rise on two reference hits, entry 0 exactly 1.0, non-increasing.
 * Accessor: setDistortionShape().
 */
#define DISTORTION_MAKEUP_COUNT 128u
extern const float distortion_makeup[DISTORTION_MAKEUP_COUNT];
```

**(c) MODIFY lines 56–85** (the two inline curve helpers): in each,
`x = (1+dist->shape)*x/(1+dist->shape*fabsf(x));` becomes

```c
	x = dist->out_gain*x/(1+dist->shape*fabsf(x));
```

and the comment above `distortion_curveSample16()` gains:

```c
 * S095 P4 C3: the numerator is out_gain = m(drv)(1 + k) (the makeup folded
 * in, setDistortionShape()); same operations per sample as (1 + k).
```

### S9.5 `Core/DSPAudio/distortion.c`

**(a) MODIFY lines 56–59** (`setDistortionShape()`):

```c
/*
 * Set the curve from the drive row (S073; S095 P4 C3).
 *
 * What: k = 2(s/128) / (1 - s/128) as before; out_gain = m(s)(1 + k), with
 * m the half-makeup factor of row s (distortion_makeup[], generated).
 * Why: the curve added up to ~20 dB RMS with no makeup, so `drv` was the
 * only way a clean kit reached the bus compressor; half is taken back, the
 * shape is unchanged. Per row change (the IM_SPECIAL_INSTRUMENT_DRIVE
 * writer, voice init): one table read in place of nothing, the division
 * as before.
 * Inputs: dist, drive row 0..127 (values above 127 read entry 127).
 * Outputs: dist->shape, dist->out_gain.
 */
__inline void setDistortionShape(Distortion *dist, uint8_t shape)
{
	const uint8_t s = (shape > 127u) ? 127u : shape;

	dist->shape = 2*(s/128.f)/(1-(s/128.f));
	dist->out_gain = distortion_makeup[s] * (1.0f + dist->shape);
}
```

(The previous form had no clamp; every caller passes 0..127 or the init
value 2, so the clamp changes nothing today and keeps the table read in
range.)

**(b) MODIFY line 74** (`distortion_calcSampleFloat()`):

```c
	return dist->out_gain*x/(1+dist->shape*fabsf(x));
```

### S9.6 `Core/DSPAudio/voicePostChain.h`

No code change: the four loops call `distortion_curveSampleF()`. MODIFY
the comment lines 25–27 to add `(S095 P4 C3: the curve's numerator carries
the makeup; the same operations per sample)`.

### S9.7 Host

**(a) `tools/dsp_test/prelude.h` line 49: MODIFY**

```c
typedef struct DistStruct { float shape; float out_gain; } Distortion;
```

`modules/run_modes.c` keeps its identity stand-ins (they ignore the
fields).

**(b) NEW FILE `tools/dsp_test/modules/distortion.c`:**

```c
/*
 * tools/dsp_test/modules/distortion.c — the voice distortion curve and its
 * makeup (S095 P4 C3).
 *
 * Laws:
 *   DI1 drv 0: distortion_curveSampleF() returns the S073 identity,
 *       (int16)(int32)(x / 32767 * 32767), bit for bit, over a float sweep
 *       of -40000..40000 (step 0.37) and every int16 through
 *       distortion_curveSample16();
 *   DI2 every row, inputs -32767..32767: the output stays within +-32767
 *       (the makeup only lowers the curve) and the curve is odd
 *       (f(-x) == -f(x)); -32768 is left out, the identity at drv 0 passes
 *       it through unchanged, as before;
 *   DI3 the table: entry 0 exactly 1, non-increasing, all in (0, 1];
 *   DI4 every row: out_gain == distortion_makeup[s] * (1 + shape) exactly
 *       (setDistortionShape() is the only writer).
 * Outputs: one line per failure; "distortion failures: N"; exit 1 if N > 0.
 * Accessors: make -C tools/dsp_test distortion.
 * Affiliates: distortion.h/.c, distortionMakeup.c, gen_dist_makeup.py.
 */
#include <stdint.h>
#include <stdio.h>
#include <math.h>
/* prelude.h (Distortion, INITCM_EFFECT) is force-included by HOSTFLAGS. */

#define DISTORTION_MAKEUP_COUNT 128u
/*
 * Plain prototypes, as distortion.h gives the firmware: with them the
 * extracted `__inline` definition of setDistortionShape() is an external
 * definition (C99 6.7.4), so the host link never misses it.
 */
void setDistortionShape(Distortion *dist, uint8_t shape);
float distortion_calcSampleFloat(const Distortion *dist, float x);
#include "dist_table.c"     /* the real distortion_makeup[] */
#include "dist_curve.c"     /* the real curve helpers (distortion.h) */
#include "dist_shape.c"     /* the real setDistortionShape() */

static int failures;

static void expect(int cond, const char *msg)
{
    if (!cond) {
        printf("FAIL %s\n", msg);
        failures++;
    }
}

int main(void)
{
    Distortion d;
    float x;
    int32_t i;
    uint32_t s;
    int ok;

    /* DI1 */
    setDistortionShape(&d, 0u);
    ok = 1;
    for (x = -40000.0f; x <= 40000.0f; x += 0.37f) {
        const float ref = x * (1.f / 32767.f);

        ok = ok && distortion_curveSampleF(&d, x) ==
                   (int16_t)(int32_t)((1.0f * ref / (1.0f + 0.0f * fabsf(ref))) * 32767);
    }
    for (i = -32768; i <= 32767; i++) {
        const float ref = (int16_t)i / 32767.f;

        ok = ok && distortion_curveSample16(&d, (int16_t)i) ==
                   (int16_t)(int32_t)((1.0f * ref / (1.0f + 0.0f * fabsf(ref))) * 32767);
    }
    expect(ok, "DI1 drv 0 is not the identity");
    /* DI2, DI4 */
    for (s = 0u; s < 128u; s++) {
        char msg[80];

        setDistortionShape(&d, (uint8_t)s);
        ok = d.out_gain == distortion_makeup[s] * (1.0f + d.shape);
        for (i = -32767; i <= 32767 && ok; i += 7) {
            const int16_t y = distortion_curveSample16(&d, (int16_t)i);

            ok = (y <= 32767 && y >= -32767) &&
                 distortion_curveSample16(&d, (int16_t)-i) == (int16_t)-y;
        }
        snprintf(msg, sizeof msg, "DI2/DI4 row %u", (unsigned)s);
        expect(ok, msg);
    }
    /* DI3 */
    ok = distortion_makeup[0] == 1.0f;
    for (s = 1u; s < 128u; s++)
        ok = ok && distortion_makeup[s] <= distortion_makeup[s - 1u] &&
             distortion_makeup[s] > 0.0f;
    expect(ok, "DI3 the makeup table");
    printf("distortion failures: %d\n", failures);
    return failures ? 1 : 0;
}
```

**(c) `tools/dsp_test/Makefile`: ADD** (after the `lfo_rate_table` target,
lines 185–186), and add `distortion dist_makeup_table` to `.PHONY` (lines
57–62) and to `all` (lines 63–66):

```make
# S095 P4 C3: the voice distortion curve with its makeup (modules/distortion.c)
# on the real helpers and the real generated table.
distortion: | $(GEN)
	$(EX) --src $(ROOT)/Core/DSPAudio/distortionMakeup.c \
	  --object distortion_makeup --out $(GEN)/dist_table.c
	$(EX) --src $(ROOT)/Core/DSPAudio/distortion.h \
	  --func distortion_curveSample16 --func distortion_curveSampleF \
	  --out $(GEN)/dist_curve.c
	$(EX) --src $(ROOT)/Core/DSPAudio/distortion.c \
	  --func setDistortionShape --func distortion_calcSampleFloat \
	  --out $(GEN)/dist_shape.c
	$(HOSTCC) $(HOSTFLAGS) -I$(GEN) modules/distortion.c -o build/distortion -lm
	./build/distortion

dist_makeup_table:
	cd $(ROOT) && python3 tools/plots/gen_dist_makeup.py --check
```

The module's own prototypes (above) handle the `__inline` definition; the
target's build is the check that the extraction compiles.

### S9 gate

- `make all`; host `distortion`, `dist_makeup_table`, `run_modes` pass.
- `make -C tools/dsp_test elf_p4_check P4BASE=baselines/s095p4/before`:
  the mixer's inlined post-chain loops (the `voicePost_*` bodies in
  `mixer_calcNextSampleBlock`) at or below their S0 lengths. The numerator
  load replaces the `1 + shape` add; if LTO lengthens a loop, stop and
  report (the CPU ceiling).

---

## S10. T4 C5 — the gain-reduction readout

**Needs your RAM sign-off first (SD8): 8 B `.bss`, SRAM1, owner `menu.c`.**

### S10.1 `Core/DSPAudio/BusCompressor.h`: ADD after the
`busComp_sidechainTrigger()` prototype

```c
/*
 * Raw meter of the bus compressor (S095 P4 C5).
 *
 * What: the detector's smoothed power (mean of L^2 + R^2 over 2, full scale
 * 1.0) and the reduction the cell applies, min(fast, memory), in dB (<= 0).
 * Why: the compressor page shows them so `cam` can be set by eye; the cost
 * is a foreground read of two floats (no log here: the caller converts at
 * its own refresh).
 * Inputs: two out pointers. Output: 1 while the compressor runs (cmp on, or
 * fading), 0 when it is off (outputs untouched).
 * Context: foreground; the render runs in the same main loop, so no race.
 * Caller: menu.c (the `cgr` widget).
 */
uint8_t busComp_meterRaw(float *power, float *gr_db);
```

### S10.2 `Core/DSPAudio/BusCompressor.c`: ADD at the end of the file

```c
uint8_t busComp_meterRaw(float *power, float *gr_db)
{
    /* S095 P4 C5: contract in BusCompressor.h. */
    if (busComp.active == SCENE_BUS_COMP_MODE_OFF || !power || !gr_db)
        return 0u;
    *power = busComp.power;
    *gr_db = fminf(busComp.gr_fast_db, busComp.gr_memory_db);
    return 1u;
}
```

### S10.3 `Core/Menu/menu.h`

- **ADD after line 152** (`TEXT_BUS_COMP_TIME, TEXT_BUS_COMP_SIDECHAIN,`):
  `    /* S095 P4 C5: read-only gain-reduction readout. */` /
  `    TEXT_BUS_COMP_GR,`
- **ADD after line 234** (`LONG_BUS_COMP_TIME, LONG_BUS_COMP_SIDECHAIN,`):
  `    LONG_BUS_COMP_GR,`
- **ADD after line 280** (`SHORT_BUS_COMP_TIME, SHORT_BUS_COMP_SIDECHAIN,`):
  `    SHORT_BUS_COMP_GR,`
- **ADD after line 309** (`#define PAR_PAT_STORE_USE 0xFFFDu`):

```c
/*
 * Virtual parameter sentinel for the read-only bus-compressor readout
 * (S095 P4 C5).
 *
 * What: the fifth cell of the compressor page (`cgr`): the deepest gain
 * reduction and the loudest detector level since the last repaint. Why:
 * `cam` is set by ear and eye; the cell has no ParameterArray value, so it
 * follows the PAR_PAT_STORE_USE pattern (formatted specially, edits
 * rejected) at every site that handles that sentinel.
 * Affiliates: menuPages.h, busComp_meterRaw(), menu_serviceRuntimeWidgets().
 */
#define PAR_BUS_COMP_GR 0xFFFCu
```

### S10.4 `Core/Menu/MenuText.h`

- **MODIFY line 369:** `    {"cmp"},{"cam"},{"ctm"},{"csc"},{"cgr"},`
- **MODIFY line 442:** `    {"BusComp"},{"CompAmt"},{"CompTime"},{"CompSC"},{"CompGR"},`

(Both arrays are parallel to the enums in S10.3; the new entry sits in the
same position in all three.)

### S10.5 `Core/Menu/menuPages.h` lines 74–77: MODIFY

```c
  {TEXT_BUS_COMP_MODE,TEXT_BUS_COMP_AMOUNT,TEXT_BUS_COMP_TIME,TEXT_BUS_COMP_SIDECHAIN,
    TEXT_BUS_COMP_GR,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,
    PAR_BUS_COMP_MODE,PAR_BUS_COMP_AMOUNT,PAR_BUS_COMP_TIME,PAR_BUS_COMP_SIDECHAIN,
    PAR_BUS_COMP_GR,PAR_NONE,PAR_NONE,PAR_NONE},
```

and the comment above (lines 62–72) gains: `S095 P4 C5: a second screen
holds the read-only `cgr` readout (column 4); the rest of the second half
stays empty.`

### S10.6 `Core/Menu/menu.c`

**(a) `valueNames`, ADD after line 1388**
(`{SHORT_BUS_COMP_SIDECHAIN,CAT_SCENE,LONG_BUS_COMP_SIDECHAIN},`):

```c
    {SHORT_BUS_COMP_GR,CAT_SCENE,LONG_BUS_COMP_GR},
```

**(b) The readout's state and helpers. ADD after `menu_formatPatStoreUsePercent4()`
(ends line 12070):**

```c
/*
 * Bus-compressor readout hold (S095 P4 C5; 8 B .bss, approved <date>).
 *
 * What: the loudest detector power and the deepest reduction seen since the
 * readout last painted, -1 / +1 meaning "nothing seen". Why: the cell
 * repaints at the CPU widget's cadence (500 ms); a drum bus's reduction
 * changes every hit, so an instant read would show noise. The hold is fed
 * every foreground pass while the readout is visible
 * (menu_busCompGrSample()) and cleared when it paints.
 * Affiliates: busComp_meterRaw(), menu_formatBusCompGr3/16().
 */
static float menu_busCompPowerHold = -1.0f;
static float menu_busCompGrHold = 1.0f;

/* Take one meter reading into the hold (foreground, every pass while shown). */
static void menu_busCompGrSample(void)
{
    float power;
    float gr;

    if (!busComp_meterRaw(&power, &gr))
        return;
    menu_busCompPowerHold = fmaxf(menu_busCompPowerHold, power);
    menu_busCompGrHold = fminf(menu_busCompGrHold, gr);
}

/*
 * The held reduction in whole dB, or 0 with `off` when nothing was seen
 * (cmp off). Clears the hold. Output: nonzero when a value exists.
 */
static uint8_t menu_busCompGrTake(int8_t *level_db, int8_t *gr_db)
{
    const uint8_t have = (uint8_t)(menu_busCompGrHold <= 0.0f);
    float l;

    if (have) {
        l = 3.0103f * log2f(menu_busCompPowerHold + 1.0e-12f);
        *level_db = (int8_t)((l < -99.0f) ? -99 : (int32_t)lrintf(l));
        *gr_db = (int8_t)((menu_busCompGrHold < -99.0f) ? -99
                          : (int32_t)lrintf(menu_busCompGrHold));
    }
    menu_busCompPowerHold = -1.0f;
    menu_busCompGrHold = 1.0f;
    return have;
}

/*
 * Compact cell: the held reduction in signed dB, written as the +-63 rows
 * write signed values (numtostrps(): "  0", "- 6", "-12"), or "off".
 */
static void menu_formatBusCompGr3(char *buf)
{
    int8_t level;
    int8_t gr;

    if (!menu_busCompGrTake(&level, &gr)) {
        memcpy(buf, "off", 3u);
        return;
    }
    numtostrps(buf, gr);
}

/*
 * Click-in view: "Comp in / GR" over "in-18dB gr -6dB" (input level from the
 * 5 ms detector, held reduction), or "off" when cmp is off.
 */
static void menu_displayBusCompGrEdit(void)
{
    static const char title[] = "Comp in / GR";
    int8_t level;
    int8_t gr;
    uint8_t i;

    memset(&editDisplayBuffer[0][0], ' ', 16u);
    memset(&editDisplayBuffer[1][0], ' ', 16u);
    for (i = 0u; i < sizeof(title) - 1u && i < 16u; i++)
        editDisplayBuffer[0][i] = title[i];
    if (!menu_busCompGrTake(&level, &gr)) {
        memcpy(&editDisplayBuffer[1][13], "off", 3u);
        return;
    }
    memcpy(&editDisplayBuffer[1][0], "in", 2u);
    numtostrps(&editDisplayBuffer[1][2], level);
    memcpy(&editDisplayBuffer[1][5], "dB gr", 5u);
    numtostrps(&editDisplayBuffer[1][11], gr);
    memcpy(&editDisplayBuffer[1][14], "dB", 2u);
}
```

and **ADD the prototypes** after line 2091
(`static void menu_displayPatStoreUseEdit(void);`):

```c
/* S095 P4 C5: the bus-compressor readout (same repaint paths as StoreUse). */
static uint8_t menu_busCompGrWidgetVisible(void);
static void menu_busCompGrSample(void);
static void menu_formatBusCompGr3(char *buf);
static void menu_displayBusCompGrEdit(void);
```

**ADD after line 73** (`#include <stdint.h>`, the last include):

```c
#include <math.h>              /* S095 P4 C5: log2f/lrintf/fmaxf (the readout) */
#include "BusCompressor.h"     /* S095 P4 C5: busComp_meterRaw() */
```

**(c) The five sites that special-case `PAR_PAT_STORE_USE`: ADD the new
sentinel beside it.**

| Line | Site | Addition |
|---|---|---|
| 15096–15099 | edit view (`menu_repaintGeneric()`) | `if (cell.kind == MENU_CELL_STATIC && cell.static_param == PAR_BUS_COMP_GR) { menu_displayBusCompGrEdit(); return; }` after the StoreUse case |
| 15403–15405 | compact row | `} else if (cell.kind == MENU_CELL_STATIC && cell.static_param == PAR_BUS_COMP_GR) { menu_formatBusCompGr3(valueAsText);` after the StoreUse case |
| 15518–15521 | encoder edit guard | `|| cell.static_param == PAR_BUS_COMP_GR` inside the parenthesis |
| 17652–17654 | pot edit guard | `|| cell.static_param == PAR_BUS_COMP_GR` inside the parenthesis |
| 17788–17801 | visibility | a sibling `menu_busCompGrWidgetVisible()` (below) |

**(d) ADD after `menu_patStoreUseWidgetVisible()` (ends line 17801):**

```c
/*
 * Report whether the compressor readout is visible (S095 P4 C5).
 *
 * What: the Scene settings sub-page's second screen (column 4) or the
 * readout's own click-in view. Output: nonzero when sampling and repainting
 * are useful. Affiliates: menu_serviceRuntimeWidgets(), PAR_BUS_COMP_GR.
 */
static uint8_t menu_busCompGrWidgetVisible(void)
{
    uint8_t activePage = (uint8_t)((menuIndex & MASK_PAGE) >> PAGE_SHIFT);
    uint8_t activeParameter = menuIndex & MASK_PARAMETER;

    if (menu_activePage != MENU_MIDI_PAGE)
        return 0u;
    if (editModeActive) {
        menu_cell_t cell = menu_resolveCell(activePage, activeParameter);
        return (uint8_t)(cell.kind == MENU_CELL_STATIC &&
                         cell.static_param == PAR_BUS_COMP_GR);
    }
    return (uint8_t)(activePage == MENU_GLOBAL_SCENE_SUBPAGE &&
                     activeParameter >= 4u);
}
```

**(e) `menu_serviceRuntimeWidgets()`:**

- ADD before line 17960 (`if ((uint16_t)(now - menu_cpuUseLastRefresh) < MENU_CPU_USE_REFRESH_MS)`):

```c
    /*
     * S095 P4 C5: feed the compressor readout's hold every pass while it is
     * shown (two float compares; nothing when hidden, the page test first).
     */
    if (menu_activePage == MENU_MIDI_PAGE && menu_busCompGrWidgetVisible())
        menu_busCompGrSample();
```

- MODIFY line 17986:

```c
        (menu_cpuUseWidgetVisible() || menu_patStoreUseWidgetVisible() ||
         menu_busCompGrWidgetVisible()))
```

**(f) BC18 check, `menu_devCheckGlobalSceneSubPage()` lines 20502–20520:**
the comment's "an empty second half (top5)" becomes "the `cgr` readout in
top5 (S095 P4 C5) and nothing after it"; the condition
`scene_page->top5 != TEXT_EMPTY` becomes

```c
                            scene_page->top5 != TEXT_BUS_COMP_GR ||
                            scene_page->top6 != TEXT_EMPTY);
```

### S10 gate

`make all`; a DEV build boots without the BC18 notice; device: the
compressor page scrolls to `cgr`; it reads `off` with `cmp off` and a
negative number on hits with `cmp St1`; turning a pot on it does nothing.

---

## S11. Gates and measurements (all steps in)

1. `make all && make img`; `link_budget.py` → `tools/dsp_test/baselines/s095p4/after/link_budget.txt`:
   flash ≈ −3.4 KB against S0; ITCM 15,232 unchanged; FXBUF unchanged
   (126,656).
2. `make -C tools/dsp_test all` exits 0 (`space`, `crumpbit`,
   `distortion`, `dist_makeup_table`, `run_modes` among them) →
   `…/after/host_all.txt`.
3. `elf_p4_check P4BASE=baselines/s095p4/before` → `…/after/p4_check.txt`:
   every `P4_OSC_SYMS` loop and every mixer loop at or below S0.
4. Compressor host replica (no new tool file; a few lines run once and
   kept in `…/after/comp_curve.txt`): the C1 table of the plan §4.2 for
   `cam` 32/48/64/96/127 at −10/−14/−18/−22 dBFS, from the exact block
   formulas (`busComp_staticGainDb()`, the new threshold and reference).
5. `space_cpu_check`, `crumpbit_cpu_check` → `…/after/`: every type at or
   below its S0 figure.
6. DEV build boot: `effects_registryCheckResult()` 0; no BC18 notice.

---

## S12. Device steps (yours)

1. **Card:** the three `flt` files fixed (S1.8). Load a Bank that used
   them: it loads, its Scene 11 Effect `off`.
2. **Effect page:** step `typ` through `off cbt dly rvb ech` (no `flt`).
3. **Space types (T1):** a Delay, a Reverb and an Echo at `vol` 127 on a
   voice with full send: the wet sits about level with the dry output, and
   switching the type in or out does not jump the dry level. Switching
   CrumpBit → Delay fades CrumpBit's dry out (no step).
4. **Values (T2):** `spr fbk xfb dck pre dcy int dtn env` show plain
   numbers; `tim` still shows its stops; `hed` steps `A B C AB AC BC ABC`
   one per detent; `frz`, `rtg` one stop per detent; CrumpBit `rvb` `dmp`
   0..127 darkens toward 0 and is flat at 127.
5. **Feedback:** Delay `fbk` 100 repeats at a steady level; 101+ grows into
   the tape's soft clip.
6. **Compressor (C1/C2/C3/C5):** a clean kit, `cmp St1`, `cam` 48, 64, 96:
   read `cgr`; then the same kit with `drv` 64 on two voices. Tell me the
   readings and what you hear; C1's curve and C3's share are the two
   numbers to tune.
7. **CPU:** the `cpu` widget on the heaviest Scene against before.

---

## 13. Statements this makes out of date (for you to update)

- `MEMORY.md` §DSP S073 invariants: "int16 voice filter and float Effect
  filter are twins" (S1).
- `EFFECTS_BUS_REFERENCE.md` §4.2a registered types and ids, §8, and the
  StereoFilter mentions; the Space lane tables (S2.5).
- `EFFECTS_MODULES_DSP_REFERENCE.md` §5 (StereoFilter); §6.1 CrumpBit
  `rvb` damping (S6).
- `MIXER_SYSTEM_DSP_REFERENCE.md` §4.2 (threshold −8..−40, reference
  −20 dBFS, the worked table), §4.5 (the meter getter), and the voice
  gain-staging facts (drive floor 1.0, distortion makeup).
- `INSTRUMENTS_DSP_REFERENCE.md`: the filter drive law and the distortion
  curve (C2, C3).
- `MENU_UI_SPEC.md` §11 (the third runtime widget, `cgr`).
- `CPU_USE_DSP_AUDIT.md`: StereoFilter as the Effect in the worst-case
  tables.
- `S095_P1_FX_DELAY_RVB_ECHO.md` D4 (unity 106), O6 labels;
  `S095_P1_IMPLEMENTATION.md` §S18 (the `mix` rows, the zone rule).
- `STORAGE_SRAM_MANIFEST.md`: +8 B `.bss` (S10), `SpaceRuntime` −4 B.
- `REPOSITORY_LAYOUT.md`, `MODULE_INTERCHANGE_SPEC.md`, `AUTOSAVE.md`,
  `DEV_MODES.md`, `FILESYSTEM_SPEC.md`: StereoFilter / `flt` mentions;
  `REPOSITORY_LAYOUT.md` also gains `distortionMakeup.c`,
  `gen_dist_makeup.py`, `modules/distortion.c`.

---

## 14. Implementation log (this session)

Working tree at `d58c0280` (source unchanged since the schedule's
`4602b3b3`); session goal `S095_P4_FX_BUS_TWEAKS.md`. Memory expansions
quoted in the schedule are approved by the user for this session,
including the 8 B `.bss` readout hold (SD8).

| Step | State | Notes |
|---|---|---|
| S0 | done | `make all` at `d58c0280`: Flash 720,068 / ITCM 15,232 / FXBUF 126,656, matching §S0. Records written to `tools/dsp_test/baselines/s095p4/before/`: `lxr02.elf`, `link_budget.txt`, `osc.txt`, `mixer.txt`, `space_cpu.txt` (dly 194.7, rvb 189.7, ech 193.1), `crumpbit_cpu.txt` (worst `grn` 182.7, rvb 181.3), `host_all.txt` (exit 0). |
| S1 | done | StereoFilter deleted (4 files + directory), `Makefile` include/source/-Ofast rule removed, registry ids 1..4 (count 5), the union member and both registry/assert lines removed, `SVF_calcBlockZDFFloat()` and its header block removed, and the comment-only StereoFilter names updated (FxBuffer.c, SpaceKit.h, CrumpBitParameters.h/.c, config.h; `ResonantFilter.c`'s twin references too). Build clean; flash 716,204 B (T3 −3,864 B at this point); host `all` exit 0. The three `flt` card files are left for §S1.8 (yours). |
| S2 | done | T1: `mix` row removed from all three Space types (counts 14/20/16, rows renumbered), `SPACE_LAW_MIX`/`space_tapeK_t.mix` gone, `SpaceRuntime.mix` → `wet`, dry target 0 (SD2, SD3), export hands over `rt->dry`, feed mix 1.0, lane 1 = Delay none / Reverb `siz` / Echo `hsh`, host row tables shifted. Host `space` 0 failures; `space_cpu_check` dly 194.6 / rvb 189.5 / ech 193.0, all ≤ S0. |
| S3 | done | T2 plain text: Space text tables reduced to `tim`/`hed`/`frz`/`rtg`; `SPACE_TXT_RATIO/FBK/PCT/PRE/DECAY/SIGNED/CENTS/ENV` and `space_tenths()` gone; CrumpBit `crumpBit_formatSemitones()`, the `aux`/`p2`/`p3` semitone text and the `grn` `fbk` percent text gone (`aux` keeps `---`). Host `space`/`crumpbit` 0 failures. |
| S4 | done | `hed`/`frz`/`rtg` store the stop (0..6, 0..2), defaults 2/0/0, letters `A B C AB AC BC ABC`; `space_stop()` replaced by `space_step()`; descriptors, laws and host SP2/SP3/SP4/SP6 updated. **Corrigendum:** the `space` host target now also extracts `SPACE_FREEZE_STOPS` and `SPACE_ROUTING_STOPS` (the extracted `SpaceEffect.c` names them, so they cannot be left out of `tools/dsp_test/Makefile`). SP6 passes (7 stops, default 2). |
| S5 | done | `space_fbkGain()` unity at row 100, 1.2 at 127; header comment updated. Host `space` SP2/SP8 pass. |
| S6 | done | CrumpBit `rvb` damping reads `crumpBit_toneK[ton >> 1]`; page text is the plain 0..127 row; host CB15(d) now checks `ton` 2m/2m+1 bit-identical (126 vs 127 in pass 0). **Corrigendum:** CB15(c)'s freeze law now pins `ton` 127, because 63 is mid damping by design and the test measures the freeze loop, not the damping. `crumpbit` 0 failures; `crumpbit_cpu_check` rvb 181.3 / 201.2 (caps 184 / 204). |
| S7 | done | C1: threshold `-8 - 32a`, makeup reference −20 dBFS (`BUS_COMP_REF_POWER` 0.01), comment revised. `elf_p4_check`: `busComp_processBlock` loop exact. |
| S8 | done | C2: `SVF_setDrive()` floor 1.0 (`1 + 5.4x²`), `SVF_init()` drive 1.0; comment added. `elf_p4_check`: `SVF_calcBlockZDF` loop exact. |
| S9 | done | C3: generated `distortionMakeup.c` (128 floats, half makeup; values match §S9.1), `distortion.h` `out_gain` + table declaration, `setDistortionShape()` folds the makeup, both curve helpers use `out_gain`, host `modules/distortion.c` + `distortion`/`dist_makeup_table` targets added, `prelude.h` updated. Host DI1–DI4 and `dist_makeup_table` pass; `run_modes` 0 failures; `elf_p4_check`: mixer 58 hot loops equal, 0 grew (96 osc loops exact). |
| S10 | done | C5: `busComp_meterRaw()`, `TEXT/LONG/SHORT_BUS_COMP_GR`, `PAR_BUS_COMP_GR 0xFFFC`, page column 4, valueNames triplet, `menu.c` hold/sample/take/format/edit helpers (8 B `.bss`, approved), encoder and pot guards, visibility predicate, service sampling + repaint, BC18 update. Build clean. Device readout behaviour is §S12.6. |
| S11 | done (device items open) | `make all && make img` OK (image 716,404 B). After-records in `baselines/s095p4/after/`: Flash 716,404 (**−3,664 B** vs S0; ITCM 15,232 and FXBUF 126,656 unchanged), `host_all.txt` exit 0, `p4_check.txt` 96/96 exact loops and 0 hot loops grew, `comp_curve.txt` reproduces the plan §4.2 C1 table exactly (cam 32/48/64/96/127), `space_cpu.txt`/`crumpbit_cpu.txt` all ≤ S0. A DEV boot (registry self-check 0, no BC18 notice) and the §S12 listening steps remain yours. |

---

## 15. Assessment (review of the implementation, 2026-11-13)

Reviewed: the working tree on top of `d58c0280` against this schedule
(§0–§S12) and the log in §14. Every source diff was read, and the build and
gates were re-run independently. No source was changed by the review.

### 15.1 Verdict

**The implementation matches the schedule.** Every step S1–S10 is in, the
schedule decisions SD1–SD11 are followed, and every gate re-run passes. The
two corrigenda in §14 are sound. Three findings need a decision or a small
follow-up (§15.4: F1, F2, F3). F4 is a listening item for S12, and F5 and F6
are open items on your side. None blocks the device run.

### 15.2 Gates re-run by the review

| Gate | Result |
|---|---|
| `make all` after touching every changed source | exit 0. Warnings: only the newlib `_close/_lseek/_read/_write` stubs and the LTO serial note (both pre-existing) |
| Link budget | Flash 716,404 B (**−3,664 B** vs S0 720,068), ITCM 15,232 (unchanged), FXBUF 126,656 (unchanged), DTCM statics 4,396 |
| `make -C tools/dsp_test all` | exit 0. `space` 0, `crumpbit` 0, `distortion` 0, `dist_makeup_table` ok, `run_modes` 0; every `*_break` target caught (the `voice_control failures: 6` line is the deliberate `voice_control_break` build) |
| `elf_p4_check P4BASE=baselines/s095p4/before` | 96 baseline loops, 96 exact; mixer 58 hot loops equal, 0 grew. **No per-sample cost change** (C3's `out_gain` numerator compiles to the same loops as `1 + shape`) |
| `space_cpu_check` | dly 194.6, rvb 189.5, ech 193.0 (S0: 194.7 / 189.7 / 193.1; caps 202.9 / 191.8 / 199.2) |
| `crumpbit_cpu_check` | worst `grn` 182.7 (cap 184), feed worst 202.7 (cap 204); `rvb` 181.3 / 201.2 (S0 181.3 / 201.3); `pit` 155.1 = S0 |
| `gen_dist_makeup.py --check` | ok (the committed table is the generator's output) |
| `baselines/s095p4/after/comp_curve.txt` | equals the plan's §4.2 C1 table (cam 32/48/64/96/127 at −10/−14/−18/−22 dBFS) |
| Stale names (`space_stop`, `SVF_calcBlockZDFFloat`, `StereoFilter`, `inv_shape`, `SPACE_*_MIX`, `k.mix`) | only in comments that record the change (`distortion.h:52`, `EffectsManager.h:71`, `SpaceEffect.h:128`) |

### 15.3 Code review by step

| Step | Checked | Notes |
|---|---|---|
| S1 T3 | ids, registry, union, asserts, Makefile, float twin, comments | Correct. Every id user names the macro. The DTCM feed stamp (`SK_CTL_MAGIC \| id`) is runtime only |
| S2 T1 | enums, descriptors, law table, `space_tapeK_t`, `wet`, dry glide, export, feed mix, lanes, host rows | Correct. Lane rows are unique per type (the self-check codes 5/6 statically hold); dry glide `0.9 × dry` per block as specified |
| S3 T2 | Space text kinds, formatter, CrumpBit hook | Correct. `aux` keeps `---`; `p2`/`p3`/`int`/`env` print the same numbers via the dtype |
| S4 | `space_step()`, descriptors (max 6 / 2, defaults 2 / 0 / 0), laws, labels, host SP2/3/4/6 | Correct. Self-check code 7 (default ≤ max) statically holds. Host corrigendum (`SPACE_FREEZE_STOPS`/`SPACE_ROUTING_STOPS` extracted) is required and right |
| S5 | `space_fbkGain()` | Correct: 100 → exactly 1.0, 127 → 1.2 |
| S6 | `toneK[ton >> 1]`, page text, CB15(d) | Correct. CB15(c) corrigendum is right for the test, but see F1 |
| S7 C1 | constants, threshold, comment | Correct; loop exact |
| S8 C2 | `SVF_setDrive()`, `SVF_init()` | Correct; loop exact. See F4 |
| S9 C3 | struct, table, `setDistortionShape()`, curve helpers, host module | Correct. All four voice types call `setDistortionShape()` in their `*_initVoice()` (`DrumVoice.c:146`, `Snare.c:95/106`, `CymbalVoice.c:89`, `HiHat.c:100`), so no live `Distortion` keeps `out_gain` 0 |
| S10 C5 | getter, text ids (three enums + two arrays + `valueNames`), sentinel at all five `PAR_PAT_STORE_USE` sites, visibility, sampling, BC18 | Correct as scheduled. See F2 |

### 15.4 Findings

**F1 — CrumpBit `rvb` freeze no longer holds at the default damping
(sound; needs your decision).**
- **What happens:** `ton` 63 is the shared row's default (flat for the
  tilt sub-types). Under §2.7 it is mid damping in `rvb`. The damping
  one-pole stays in the loop while frozen (`snc` on), so a frozen tail
  loses its highs pass after pass.
- **Measured** (CB15(c) re-run by the review at `ton` 63 instead of the
  pinned 127): **8.08 dB over 1 s** (at flat: 2.15 dB; the law allows
  ±6 dB). A Scene that freezes at the default damping now fades instead of
  holding.
- **Recommended fix:** while frozen, the damping pole is flat:
  `tone_k = rt->sync ? 1.0f : crumpBit_toneK[rt->tone_raw >> 1]` in
  `crumpBit_runReverb()` (`CrumpBitEffect.c`, the `tone_k` line).
  - Cost: per block, one select; no per-sample change.
  - It matches what a freeze means, and CB15(c) could go back to the
    default `ton`.
- **Alternative:** accept it (damping then also shapes the frozen tail).

**F2 — the `cgr` readout can show `off` while the compressor is on (UI,
minor).**
- `menu_busCompGrTake()` clears the hold on every paint, and the hold is
  fed once per foreground pass. A paint with no sample since the last one
  reads "nothing seen" and prints `off`. That happens on the first paint on
  arrival at the cell, and on any second repaint within the same pass
  (knob/marker repaints).
- **Fix:** when the hold is empty, read `busComp_meterRaw()` once directly
  and print `off` only when it returns 0. Foreground only; no RAM.

**F3 — CrumpBit `rvb` `dmp` reads ±63 on the STEP automation page (UI,
minor).**
- The Effect page (compact, full and held views) goes through
  `format_value3` and shows the plain 0..127.
- The automation target text goes through `crumpBit_uiFormatTarget3()`,
  which only names `sub`, so there the same row still shows ±63.
- **Fix:** give `crumpBit_uiFormatTarget3()` the same `rvb`/`ton` case,
  using `crumpBit_shownSubtype()`. Or accept the difference, since that
  page shows raw targets elsewhere too.

**F4 — C2 removes 8 dB of headroom in front of the voice filter (listen in
S12.6).**
- LP leaves through `fastTanh()`, but HP, BP, UBP, Nch and Pek leave
  through `× FILTER_GAIN` and `__SSAT` (hard int16 clip). A resonant
  non-LP voice at low `drv` can now hard-clip where it did not.
- The bus also runs closer to full scale, and with `cmp off` the pack's
  24-bit clip is hard.
- **Listen for:** a high-`res` BP/Pek voice at `drv` 0, and a full kit
  with `cmp off`.
- **Fallback (plan §4.2):** the middle floor `0.7 + 5.7x²`, one line in
  `SVF_setDrive()` and `SVF_init()`.

**F5 — test card (yours, §S1.8):** the three `effects.fx` files still say
`type=flt` (`SD_CARD/Bank/000 FluxTime`, `002 FluxTim2`,
`027 NoBankOk`, each `11 Eris/effects.fx`). Those Scenes fail to load
until they are changed to `type=off`.

**F6 — not verifiable from this tree:**
- `DEV_MODE_DIAGNOSTIC` is 0 (`config.h:68`), so the built image contains
  neither the registry self-check nor BC18. Both were checked statically
  here (§15.3); the diagnostic boot in §S11.6 needs a diagnostic build.
- Every listening step in §S12 is still open.

### 15.5 Side effects of the review

The forced rebuild rewrote `build/` outputs (same sources, so the same
image). The host run rewrote the tracked artefacts under
`tools/dsp_test/build/`, which the implementation run had already
modified. One scratch copy of `modules/crumpbit.c` (for the F1
measurement) was compiled outside the tree and its temporary copy in
`modules/` was removed.

---

## 16. Follow-up fixes (user decisions on §15, 2026-11-13)

User decisions on §15:
- **F1:** "for sure, we want longer freezes; the bigger issue is that for
  ALL low-pass only ton controls, 127 should be the default."
- **F4:** "hard clip very bad, reduce now."
- **F2, F3:** the review's recommendations accepted.

Everything below is in the tree, built and gated. Records are in
`tools/dsp_test/baselines/s095p4/after_fixes/`.

### 16.1 F1: CrumpBit `rvb` damping gets its own row (default 127), and a freeze holds

**Low-pass-only tone controls, audited:**

| Control | Kind | Default before | Default now |
|---|---|---|---|
| Space Reverb `ton` (`rvb_tone`) | LP only, 127 flat | 127 | 127 (no change) |
| Alkaline `alk_tone` | LP only, 127 flat | 127 (`AlkalineVoice.c:272`, `init.alk`) | 127 (no change) |
| Colloid `col_tone` | LP only, 127 flat | 127 (`ColloidVoice.c:745`, `init.col`) | 127 (no change) |
| CrumpBit `rvb` damping | LP only, 127 flat | **63**, the shared `ton` row's default (flat for the tilt sub-types, mid damping here) | **127**, its own row |

**Why a separate row:**
- The shared `ton` row (row 11) has one default for all sub-types, and
  the tilt sub-types (`dly`, `cmb`, `pit`) need 63 as their flat point.
- Giving `rvb` its own row lets its default be 127 without moving the
  tilt rows' 63.
- It costs no RAM: `CrumpBitRuntime` had a padding byte before its union
  (55 B of fields; it is still asserted at exactly 100 B,
  `CrumpBitEffect.c:196`).

**Changes:**

| File | Change |
|---|---|
| `CrumpBitParameters.h` | `CRUMPBIT_PARAM_RVB_DAMP` (row 24) appended; `CRUMPBIT_PARAM_COUNT` 25; new owner mask `CRUMPBIT_ROWS_RVB`; `CRUMPBIT_RVB_DAMP_FLAT` 127; comments (row list, owners, the `ton` centre note) |
| `CrumpBitParameters.c` | Descriptor `crump_rvb_damp` ("Reverb", "Damping", `dmp`, `DTYPE_0B127`, Morph/LFO/Pattern, default 127). The ownership asserts take the fourth mask. `rvb`'s `ton` label now uses the base name. Value hook: `ton` in `rvb` and `dmp` outside `rvb` show `---`. `crumpBit_layoutCombReverb` is split into `crumpBit_layoutComb` (as before) and `crumpBit_layoutReverb` (s3: `frz dmp wid`) |
| `CrumpBitEffect.h` | `uint8_t rvb_damp` in the former padding byte |
| `CrumpBitEffect.c` | Neutral value 127 at entry; `write_param` case; the `rvb` lane map's lane 10 is `dmp`. `crumpBit_runReverb()`: `tone_k = rt->sync ? 1.0f : crumpBit_toneK[rt->rvb_damp >> 1]`, so the pole is exactly flat while frozen (per block; no per-sample change) |
| Host `crumpbit.c` | CB9: the fourth mask in the disjoint/cover checks, `rvb` rows are lanes of `rvb` only, `rvb` lane 10 = `dmp`. CB15(a): the extremes drive `dmp`. CB15(c): the freeze law now runs at **mid damping** (`dmp` 63) and must hold. CB15(d): `dmp` 2m and 2m+1 bit-identical |
| `tools/dsp_test/Makefile` | Extracts `CRUMPBIT_ROWS_RVB` and `CRUMPBIT_RVB_DAMP_FLAT` |

This also settles §15's **F3**: `dmp` is a plain `DTYPE_0B127` row, so
it reads 0..127 on every page, the STEP automation page included.

**Measured** (CB15(c)):

| Damping during freeze | Loss over 1 s |
|---|---|
| Mid, before this fix (§15 F1) | 8.08 dB |
| Mid, now | 2.48 dB |
| Flat, before | 2.15 dB |

The law allows ±6 dB.

**Stored data (no migration):**
- A `.fx` file without `crump_rvb_damp` loads 127 (flat).
- A CrumpBit `rvb` Scene in the AutoSave reads whatever byte slot 24
  held (usually 0, the darkest): reset or re-save it.
- `ton` values stored for `rvb` are kept and ignored there; they still
  drive `dly`/`cmb`/`pit` after a sub-type change.
- MIDI and Pattern values reach the new row by its own key and index.

### 16.2 F4: the voice filter's drive floor per type

**Measured** on the host: the real `SVF_calcBlockZDF()`, full-scale saw,
kick and noise, `drv` 0, `res` 0..127, cutoff 0.02..0.4. Figures are mean
clipped samples / worst setting (`after_fixes/filter_clip.txt`):

| Type | Output stage | Floor 0.4 (pre-P4) | Floor 1.0 (C2) |
|---|---|---|---|
| LP | `fastTanh` (soft) | 0 % | 0 % |
| LP2 | clamped states × 0.883 | 0 % | 0 % |
| HP | `__SSAT` (hard) | 0.014 % / 0.35 % | 2.1 % / 16 % |
| BP | `__SSAT` | 0.000 % / 0.01 % | 0.34 % / 6.1 % |
| UBP | `__SSAT` | 0 % | 0.03 % / 1.2 % |
| Nch | `__SSAT` | 0 % | 0.02 % / 0.34 % |
| Pek | `__SSAT` | 1.2 % / 16 % | 7.6 % / 44 % |

**What was tried first:** a floor of 0.8 kept UBP and Nch clean at
`drv` 0, but clipped them more than before mid-knob. At `drv` 48, Nch
went from 0.65 % to 10.3 %, and UBP from 0.09 % to 0.18 %.

**Decision:** every hard-clipping type returns to the pre-P4 floor (0.4);
LP and LP2, which cannot hard-clip, keep C2's 1.0.

**Change** (`ResonantFilter.c`):
- `svf_driveFloorOffset[9]` holds an offset per type: LP 0; HP, BP,
  UBP, Nch, Pek −0.6; LP2 and `off` 0.
- `SVF_calcBlockZDF()`'s ZDF branch adds it to `filter->drive` once per
  block.
- The hard-clipping types get `0.4 + 5.4x²`, which is at most the pre-P4
  `0.4 + 6x²` at every `drv`. They clip no more than before anywhere:
  measured at `drv` 0..112, HP, BP and Pek clip slightly less above
  `drv` 0, and are identical at 0.
- The `SVF_setDrive()` comment notes the offset.

**What remains:**
- C2's level gain now applies to LP (and LP2) voices only, which reach
  the bus 8 dB hotter than before. Non-LP voices are where they were
  before P4.
- The 8 dB gap between filter `off` and LP is closed; HP, BP, UBP, Nch
  and Pek keep their pre-P4 level.

**Pre-existing, not changed:** the non-LP types hard-clip under drive as
they always have (HP 10 % of samples at `drv` 64; Pek 1.2 % even at
`drv` 0). A real cure needs a soft output stage on those types, as LP
has, which is a per-sample cost the CPU ceiling would have to pay for.
That is a separate decision.

**Loop gate:**
- `elf_p4_check` no longer reports "exact", because the per-block add
  moved the register allocation of `SVF_calcBlockZDF` (85 exact,
  6 renamed, 5 missing).
- Compared by length, every remaining SVF loop is unchanged (36, 38, 39,
  87, 87, 88, 89, 91, 92 instructions). Two fragments merged away,
  including the 97-instruction one, so the longest SVF loop went from 97
  to 92.
- The mixer: 58 hot loops equal, 0 grew.
- No per-sample cost increase.

### 16.3 F2: the `cgr` readout no longer shows `off` while the compressor runs

`menu.c` `menu_busCompGrTake()`: when the hold is empty, it takes one
direct reading (`menu_busCompGrSample()`) before deciding. `off` now
appears only when `busComp_meterRaw()` returns 0, i.e. `cmp` is off.
Foreground only; no RAM.

### 16.4 Gates after the fixes

| Gate | Result |
|---|---|
| `make all` | exit 0; only the pre-existing newlib/LTO notes |
| Link budget | Flash 716,644 B (+240 over §15; −3,424 vs S0); ITCM 15,232; FXBUF 126,656; DTCM statics 4,396 (unchanged) |
| `make -C tools/dsp_test all` | exit 0; `crumpbit` 0 (CB9, CB15 incl. the mid-damping freeze), `space` 0, `distortion` 0, `dist_makeup_table` ok; every break target caught |
| Loops | as §16.2: no loop grew; mixer 58 equal |
| `crumpbit_cpu_check` | `rvb` 181.3 / 201.2 (unchanged), worst 182.7 ≤ 184, feed 202.7 ≤ 204 |
| `space_cpu_check` | dly 194.6, rvb 189.5, ech 193.0 (unchanged) |
| `make img` | `build/LXRV2_lxr02.img` rewritten with these fixes (payload 716,644 B) |

### 16.5 Notes

- **CB15 decay figure:** the S0 record (`before/host_all.txt`) shows
  19.48 dB, but the law built from the commit `d58c0280` (pre-P4 source,
  scratch worktree) gives 15.58 dB. The current tree gives 15.42 dB at
  flat damping, and repeated draws vary 15.1–15.8 dB. The 19.48 dB figure
  is not reproducible from the pre-P4 code, so it is not a P4 change. All
  three figures are inside the law's 9..21 dB.
- **`tools/dsp_test/spike` `filter` target** (not part of `all`): it
  extracts `SVF_calcBlockZDFFloat` from the repository file, which T3
  removed, so it no longer builds against the tree. The baseline-only
  gate still works. Drop the symbol from `FILTER_SYMS` and the float half
  of `test_filter.c` if the kit is used again.
- **Clip harness:** `after_fixes/filter_clip_sweep.c` and
  `filter_clip_curve.c` reproduce §16.2. They need the extracted pre-F4
  `SVF_calcBlockZDF` (the header comment in the sweep file gives the
  command).

### 16.6 Device steps added to §S12

1. **CrumpBit `rvb`:** a fresh `rvb` is bright (`dmp` 127, flat). Turning
   `dmp` down darkens it. Freeze at any `dmp` holds (no fade, no
   darkening). `ton` shows `---` in `rvb`.
2. **Filters:** LP voices are about 8 dB hotter than before P4. HP, BP,
   UBP, Nch and Pek voices are at their pre-P4 level. With a high `res`
   BP or Pek at `drv` 0, the clipping heard before P4 is unchanged and
   no new clipping appears.
3. **`cgr`:** on arriving at the cell with `cmp` on, it shows a number,
   never `off`.

### 16.7 Statements this makes out of date (additions to §13)

- `EFFECTS_MODULES_DSP_REFERENCE.md` §6/§6.2: CrumpBit has 25 rows (row
  24 `dmp`, `rvb` only); `rvb` lane 10 is `dmp`; `rvb` has its own
  layout; the freeze holds the damping flat.
- `EFFECTS_BUS_REFERENCE.md` §8.6: the `rvb` page (s3 `frz dmp wid`).
- `MEMORY.md` §Effects (CrumpBit binding rules: "24 rows").
- `INSTRUMENTS_DSP_REFERENCE.md`: the voice filter's per-type drive floor
  (LP/LP2 1.0, the others 0.4).
