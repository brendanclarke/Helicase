# S094 P1 — Implementation Schedule

- **Status:** code schedule for `S094_P1_VOICE_DSP_ADJUSTMENTS.md` revision 3
  (S094, 2026-11-11). **Nothing in the tree is changed by this document.**
- **Scope:** S0 baselines, S1 LFO, S2 crash removal, S3 drum-side PWM, S4
  Alkaline post-FX, S5 Alkaline PWM, S6 ceiling assessment. The master
  compressor is not in this pass (plan §9).
- **Binding rules restated for the implementer:**
  - S089 / S093 cost rules: per-sample loops plus per-block / 32, VDIV
    counted separately, longest path;
  - every step gates before the next starts (§0.3);
  - no step edits `MEMORY.md` or `knowledge_files/`;
  - ITCM growth approved for S3 only (+≈2.9 KB, `.itcm.s094p1`);
  - no other RAM growth: S4's 20 B lie inside the Alkaline slot reserve, which
    `_Static_assert(sizeof(AlkalineVoice) <= INSTRUMENT_RUNTIME_SLOT_BYTES)`
    (`InstrumentManager.c` l. 226) keeps honest;
  - new per-sample code is a new function, never an edit to an existing loop;
    nothing stays live across an inlined render (S086 F1).

---

## 0. Conventions and order

### 0.1 How changes are cited

- **File, line, action.** Each change gives the file, the line(s) **as they
  are in the tree at `ce7197fa` before S0**, and one of:
  - **ADD** (new text inserted after the cited line);
  - **REMOVE** (the cited lines go);
  - **MODIFY** (the cited lines are replaced);
  - **NEW FILE** / **DELETE FILE**.
- **Line drift.** Where an earlier step in this schedule already edited the
  same file above the cited line, the line number is the pre-S0 one and an
  **anchor** (quoted text that does not move) is given too. Use the anchor.
- **Comment blocks.** Every block of new code carries its own documentation
  comment in the project's What / Why / Inputs / Outputs / Accessors /
  Affiliates style, written to stay in the source. Important locals, loops and
  arithmetic carry their own comments too.

### 0.2 Step order

S0 → S1 → S2 → S3 → S4 → S5 → S6 → one image → device test (plan §10).

### 0.3 Gates run after every step

| Gate | Command | Pass |
|---|---|---|
| Build | `make all && make img` | no warnings beyond the S0 count |
| Host tests | `make -C tools/dsp_test all` | every group 0 failures; every break build "caught" |
| Loops | `make -C tools/dsp_test elf_p4_check P4BASE=baselines/s094p1/s<N−1>` | 0 MISSING (except where a step removes a loop by design), mixer 0 GREW (a GREW is source-paired on the `-g` twins with `elf_loopsrc.py` before it is accepted or rejected) |
| Alkaline | `python3 tools/alkaline/control_cost.py --elf build/lxr02.elf` and `elf_looppath.py --cond-heads --in-range` on the ladder wrappers | as the step states |
| Budget | `python3 tools/diagnostic_tools/link_budget.py` | ITCM and flash as the step states |
| Snapshot | `make -C tools/dsp_test elf_p4_snapshot P4BASE=baselines/s094p1/s<N>` | taken only after the step is accepted |

---

## S0 — Baselines and the Alkaline golden capture

No firmware change.

### S0.1 Record the starting image

1. `make all && make img`.
2. `make -C tools/dsp_test elf_p4_snapshot P4BASE=baselines/s094p1/s0`.
3. Save into `tools/dsp_test/baselines/s094p1/s0/`:
   - `control_cost.txt`: `python3 tools/alkaline/control_cost.py --elf build/lxr02.elf`;
   - `alk_loops.txt`: `elf_looppath.py --cond-heads --in-range` on
     `DiodeLadder_renderB2Osc` and `DiodeLadder_renderB2Osc2`;
   - `link_budget.txt`: `link_budget.py`.

The two Alkaline figures settle the drafts' 256.1 / 256.5 disagreement; every
later Alkaline gate uses them.

### S0.2 `tools/dsp_test/modules/alkaline.c` — the golden mode (MODIFY / ADD)

**ADD** after l. 1223 (the closing brace of `w_loop()`'s section, before
`static void test_typ(void)` at l. 1225). Anchor: `static void test_typ(void)`.

```c
/* ---------------------------------------------------------------- golden
 * The Alkaline golden capture and law AL-OD0 (S094 P1 S0/S4).
 *
 * What:       gold_render() plays a fixed set of voice configurations into
 *             one int16 stream:
 *               1. typ wav, Saw then Sqr, x drive bytes 0, 64, 127: the S092
 *                  P4 pattern (mode_pattern()'s steps) at fixed knobs;
 *               2. typ wav, Saw, drive 0: the mode_sweep() Res sweep, 4 s;
 *               3. typ wtb (w_install(1): two tables) at drive 0, the P4
 *                  pattern, once without a slot (w_grant 0) and once with
 *                  (w_grant 1).
 *             --golden OUT writes that stream to OUT (S0, before any firmware
 *             change). test_golden() renders it again and compares it with
 *             the committed file:
 *               - every sample of every drive-0 render must be byte-identical;
 *               - drive bytes 64 and 127 may differ by at most 2 LSB (A3:
 *                 their table entries may sit 1 ulp from the runtime
 *                 exp2 polynomial, plan finding 5); the largest difference is
 *                 printed either way.
 * Why:        P9-D7 (user): Overdrive 0 and Tone 127 must leave the output
 *             bit-identical to today. The capture is made from the S0 tree, so
 *             every later step (S1..S5) is checked against today's voice, not
 *             against itself. The voice's new fields (S4: vcaDrive 0, tone 1)
 *             are set by Alkaline_initVoice(), so this code never names them
 *             and compiles unchanged before and after S4.
 * Inputs:     none (fixed configurations); OUT path (--golden); the committed
 *             file baselines/s094p1/alk_golden.bin (test_golden()).
 * Outputs:    the file (--golden); law AL-OD0 (test_golden()).
 * Accessors:  main() (--golden mode; test_golden() in the law run).
 * Affiliates: mode_pattern(), mode_sweep(), w_install(), w_grant;
 *             S094_P0_ALKALINE_POST_FX.md §4.
 */
#define GOLD_PATH   "baselines/s094p1/alk_golden.bin"
/* blocks per pattern render: the pattern's 8 steps at 0.125 s plus 0.30 s
 * of tail, 1.3 s x 44,108 / 32 = 1,791.8 -> 1,792 blocks (an integer
 * literal, so the buffer sizes below are constant expressions) */
#define GOLD_PAT_BLOCKS   1792
/* blocks of the Res sweep: 4 s x 44,108 / 32 = 5,513.5 -> 5,513 */
#define GOLD_SWEEP_BLOCKS 5513
/* configurations: 2 waves x 3 drives + 1 sweep + 2 wtb */
#define GOLD_RUNS   9
/* samples in the whole capture: 8 pattern renders and one sweep */
#define GOLD_TOTAL  ((size_t)(8 * GOLD_PAT_BLOCKS + GOLD_SWEEP_BLOCKS) * BLK)

/* one pattern render into out; returns the number of samples written */
static size_t gold_pattern(AlkalineVoice *v, int16_t *out)
{
	/* the S092 P4 pattern: (semitones above C2, accent, slide); -1 = rest */
	static const int pat[8][3] = { {0, 0, 0}, {0, 1, 0}, {3, 0, 1}, {7, 0, 0},
	                               {12, 1, 1}, {10, 1, 0}, {-1, 0, 0}, {0, 1, 0} };
	/* samples per pattern step: 0.125 s */
	const int stepBlocks = (int)(0.125 * 44108.0 / BLK);
	int b, next = 0;

	for (b = 0; b < GOLD_PAT_BLOCKS; b++) {
		/* trigger each step on the first block at or after its start */
		if (next < 8 && b >= next * stepBlocks) {
			if (pat[next][0] >= 0) {
				v->slide = (uint8_t)pat[next][2];
				Alkaline_triggerVoice(v, 0u, pat[next][1] ? 127u : 80u,
				                      (uint8_t)(63 + pat[next][0]), ALK_STEP);
			}
			next++;
		}
		block(v, out + (size_t)b * BLK, K_B2R);
	}
	return (size_t)GOLD_PAT_BLOCKS * BLK;
}

/* a voice at the golden knobs: cutoff, Res, env, decay, accent fixed;
 * drive = byte / 127 (the descriptor's TYPE_FLT store) */
static void gold_voice(AlkalineVoice *v, uint8_t wave, uint8_t driveByte)
{
	memset(v, 0, sizeof(*v));
	Alkaline_initVoice(v);
	v->osc.waveform = wave;
	v->drive = (float)driveByte / 127.0f;
	v->cutoff = 0.45f; v->reso = 0.75f; v->envMod = 0.6f;
	v->decay = 0.4f; v->accent = 0.7f;
}

/*
 * Render every golden configuration.
 *
 * Inputs:  out (GOLD_TOTAL samples), drv0 (per-run flag array, GOLD_RUNS:
 *          1 when the run is at drive byte 0), len (per-run sample counts).
 * Outputs: out; drv0; len; returns the number of samples (GOLD_TOTAL).
 */
static size_t gold_render(int16_t *out, uint8_t *drv0, size_t *len)
{
	static const uint8_t drives[3] = { 0u, 64u, 127u };
	AlkalineVoice v;
	size_t n = 0u;
	int r = 0;

	/* 1. wav: Saw, Sqr x the three drive bytes */
	for (int w = 0; w < 2; w++)
		for (int d = 0; d < 3; d++, r++) {
			gold_voice(&v, (uint8_t)w, drives[d]);
			len[r] = gold_pattern(&v, out + n);
			drv0[r] = (uint8_t)(drives[d] == 0u);
			n += len[r];
		}
	/* 2. the Res sweep (mode_sweep()'s line, 4 s), Saw, drive 0 */
	{
		static const int semi[4] = { 0, 0, 3, 12 };
		int step = 0;

		gold_voice(&v, ALK_WAVE_SAW, 0u);
		v.cutoff = 0.35f; v.envMod = 0.5f; v.decay = 0.4f; v.accent = 0.6f;
		for (int b = 0; b < GOLD_SWEEP_BLOCKS; b++) {
			const double t = (double)b * BLK / 44108.0;

			/* Res 0 -> 1 over the 4 s */
			v.reso = (float)(t / 4.0);
			while ((double)step * ALK_STEP <= (double)b * BLK) {
				v.slide = (uint8_t)((step & 3) == 2);
				Alkaline_triggerVoice(&v, 0u, (step & 3) == 3 ? 127u : 80u,
				                      (uint8_t)(63 + semi[step & 3]), ALK_STEP);
				step++;
			}
			block(&v, out + n + (size_t)b * BLK, K_B2R);
		}
		len[r] = (size_t)GOLD_SWEEP_BLOCKS * BLK;
		drv0[r] = 1u;
		n += len[r];
		r++;
	}
	/* 3. wtb: two installed tables, without and with an interpolation slot */
	w_install(1);
	for (int g = 0; g < 2; g++, r++) {
		gold_voice(&v, 1u, 0u);
		Alkaline_setOscType(&v, ALK_TYP_WTB);
		v.osc.position = 40u;             /* between frames: the blend is heard */
		w_grant = g;
		len[r] = gold_pattern(&v, out + n);
		drv0[r] = 1u;
		n += len[r];
	}
	w_grant = 0;
	return n;
}

/* --golden OUT: write the capture (run once, at S0) */
static int mode_golden(const char *path)
{
	uint8_t drv0[GOLD_RUNS];
	size_t len[GOLD_RUNS];
	int16_t *buf = malloc(GOLD_TOTAL * sizeof(int16_t));
	const size_t n = gold_render(buf, drv0, len);
	FILE *fo;

	fo = fopen(path, "wb");
	if (!fo || fwrite(buf, sizeof(int16_t), n, fo) != n)
		return 1;
	fclose(fo);
	free(buf);
	printf("golden: %zu samples written to %s\n", n, path);
	return 0;
}

/* law AL-OD0: today's renders against the committed capture */
static void test_golden(void)
{
	uint8_t drv0[GOLD_RUNS];
	size_t len[GOLD_RUNS];
	const size_t n = GOLD_TOTAL;
	int16_t *now = malloc(n * sizeof(int16_t)), *ref = malloc(n * sizeof(int16_t));
	FILE *fi = fopen(GOLD_PATH, "rb");
	size_t off = 0u;

	if (!fi || fread(ref, sizeof(int16_t), n, fi) != n) {
		fail(G_OD0, "golden file missing or short", 0, 0);
		if (fi) fclose(fi);
		free(now); free(ref);
		return;
	}
	fclose(fi);
	gold_render(now, drv0, len);
	for (int r = 0; r < GOLD_RUNS; r++) {
		int worst = 0;

		/* the largest per-sample difference of this run */
		for (size_t i = 0; i < len[r]; i++) {
			const int d = abs((int)now[off + i] - (int)ref[off + i]);

			if (d > worst) worst = d;
		}
		printf("AL-OD0 run %d (drive-0 %u): largest difference %d LSB\n", r, drv0[r], worst);
		/* drive 0: byte-identical; drive bytes 64/127: A3's 1-ulp entries */
		if (drv0[r] ? (worst != 0) : (worst > 2))
			fail(G_OD0, "golden render differs", r, worst);
		off += len[r];
	}
	free(now); free(ref);
}
```

**MODIFY** l. 146–150, the group list (so AL-OD0 and the S4/S5 laws report).
Anchor: `static unsigned g_fail[16];`.

```c
/* law groups (S094 P1: AL-OD0 golden, AL-A3 drive tables, AL-OV overdrive,
 * AL-TN tone, AL-PW pwm appended; the array holds one counter per group) */
static unsigned g_fail[21];
enum { G_B, G_R, G_S, G_N, G_AB, G_D, G_Q, G_F, G_O, G_X, G_A, G_M, G_SO, G_PS, G_W, G_TY,
       G_OD0, G_A3, G_OV, G_TN, G_PW };
static const char *g_name[] = { "AL-B", "AL-R", "AL-S", "AL-N", "AL-AB", "AL-D", "AL-Q",
                                "AL-F", "AL-O", "AL-X", "AL-A", "AL-M", "AL-SO", "AL-PS",
                                "AL-W", "AL-TY", "AL-OD0", "AL-A3", "AL-OV", "AL-TN",
                                "AL-PW" };
```

**MODIFY** `main()` l. 1265–1271, the mode dispatch. Anchor:
`if (argc > 1 && !strcmp(argv[1], "--sweep"))`.

```c
	if (argc > 1 && !strcmp(argv[1], "--sweep"))
		return mode_sweep(argc, argv);
	/* S094 P1 S0: write the golden capture (once, before any firmware edit) */
	if (argc > 2 && !strcmp(argv[1], "--golden"))
		return mode_golden(argv[2]);
```

**ADD** in `main()` after `test_typ();` (l. 1306). Anchor: `	test_typ();`.

```c
	/* S094 P1: law AL-OD0 against the S0 capture (every step re-runs it) */
	test_golden();
```

### S0.3 Write the capture

1. `make -C tools/dsp_test alkaline` (all laws pass; AL-OD0 reports the file
   as missing at this point, which is expected only now).
2. `cd tools/dsp_test && ./build/alkaline --golden baselines/s094p1/alk_golden.bin`.
3. `make -C tools/dsp_test alkaline` again: AL-OD0 must show 0 for every run.
4. The `.bin` stays in the tree (records live under `baselines/`, never in
   `build/`).

---

## S1 — LFO sync and rate table

### S1.1 NEW FILE `tools/plots/gen_lfo_rate_table.py`

```python
#!/usr/bin/env python3
"""Generate the LFO rate table (S094 P1 S1).

What:       writes Core/DSPAudio/lfoRateTable.c with
            const float lfoRateHz[LFO_RATE_COUNT] (128 entries, Hz):
              - entry 0:     0 Hz, the LFO stops (phaseInc 0; it holds its
                             phase, a retrigger sets it to the offset);
              - entries 1..62: F_MIN (0.01 Hz, a 100 s cycle) up to just below
                             MIDI note 0, with a step that eases linearly from
                             S0 semitones at entry 1 to 1 semitone at the
                             junction, so log2(f) is a quadratic in the entry
                             index and the step changes by at most 0.03
                             semitone from one entry to the next;
              - entries 63..127: MIDI notes 0..64 exactly
                             (440 x 2^((n - 69) / 12), A4 = 440 Hz), 8.18 Hz to
                             329.63 Hz (E4), one semitone apart.
Why:        user decision D-3 (S094): a smooth range with more options at both
            ends of the control, slow rates down to 100 s and audio-rate rates
            tuned to semitones; frq 0 a true zero. The top stops at E4 so
            that twice the top rate (the 0..2 rate-modulation overlay,
            TYPE_SPECIAL_F) keeps lfo_calc()'s phase step below 2^32 and leaves
            at least 4 LFO values per cycle at the 1,378 Hz block rate.
Checks:     C1 entry 0 is exactly 0.0; entry 1 is F_MIN to 1e-9; entries 63..127
            equal the note law to 1 ppm; C2 strictly increasing from entry 1;
            C3 every step 1..2.8 semitones, the step's change between
            neighbours <= 0.03 semitone; C4 2 x top / (fs / 32) x 2^32 < 2^32;
            C5 --check: regenerate in memory and compare with the committed
            file byte for byte. Exit 1 on any failure.
Inputs:     the constants below (FS and BLOCK must match config.h REAL_FS and
            OUTPUT_DMA_SIZE).
Outputs:    Core/DSPAudio/lfoRateTable.c, or a pass/fail line with --check.
Accessors:  tools/dsp_test/Makefile target lfo_rate_table.
Affiliates: lfo.h lfoRateHz / LFO_RATE_COUNT; lfo.c lfo_setFreq();
            tools/dsp_test/modules/lfo.c (laws LF2, LF3).
"""
import math
import sys

FS = 44108.07291666667          # config.h REAL_FS
BLOCK = 32                      # config.h OUTPUT_DMA_SIZE: one LFO step per block
COUNT = 128                     # frq 0..127
F_MIN = 0.01                    # frq 1: a 100 s cycle
GRID_FIRST = 63                 # frq of MIDI note 0
TOP_NOTE = 64                   # frq 127 = E4
PATH = "Core/DSPAudio/lfoRateTable.c"


def note_hz(n):
    """Equal-tempered MIDI note n in Hz, A4 (69) = 440 Hz."""
    return 440.0 * 2.0 ** ((n - 69) / 12.0)


def table():
    """The 128 rates in Hz (module docstring)."""
    # semitones from F_MIN (frq 1) up to note 0 (frq GRID_FIRST)
    span = 12.0 * math.log2(note_hz(0) / F_MIN)
    steps = GRID_FIRST - 1
    # the first step s0, so that a step easing linearly from s0 to 1 covers
    # the span in `steps` steps: span = steps x (s0 + 1) / 2
    s0 = 2.0 * span / steps - 1.0
    out = [0.0]
    for i in range(1, COUNT):
        if i >= GRID_FIRST:
            out.append(note_hz(i - GRID_FIRST))
        else:
            # x = 0 at frq 1, 1 at the junction; the semitone position is the
            # integral of the step s(x) = s0 + (1 - s0) x
            x = (i - 1) / steps
            semis = steps * (s0 * x + (1.0 - s0) * x * x / 2.0)
            out.append(F_MIN * 2.0 ** (semis / 12.0))
    return out


def checks(t):
    ok = t[0] == 0.0 and abs(t[1] - F_MIN) < 1e-9
    ok = ok and all(abs(t[i] / note_hz(i - GRID_FIRST) - 1.0) < 1e-6
                    for i in range(GRID_FIRST, COUNT))
    ok = ok and all(b > a for a, b in zip(t[1:], t[2:]))
    st = [12.0 * math.log2(t[i + 1] / t[i]) for i in range(1, COUNT - 1)]
    ok = ok and all(0.999 <= s <= 2.8 for s in st)
    ok = ok and all(abs(b - a) <= 0.03 for a, b in zip(st, st[1:]))
    ok = ok and 2.0 * t[-1] / (FS / BLOCK) * 2.0 ** 32 < 2.0 ** 32
    return ok


def render(t):
    lines = [
        "/* Generated by tools/plots/gen_lfo_rate_table.py (S094 P1). Do not edit. */",
        '#include "lfo.h"',
        "",
        "/*",
        " * LFO rate per frq value, Hz (S094 P1, user decision D-3).",
        " *",
        " * What:       0 = stopped; 1..62 = 0.01 Hz (100 s) .. 7.71 Hz, the step",
        " *             easing from 2.73 to 1 semitone; 63..127 = MIDI notes 0..64",
        " *             (8.18 .. 329.63 Hz), one semitone apart.",
        " * Why:        replaces the cubic 200 x ((v + 1) / 128)^3 of lfo_setFreq():",
        " *             usable slow rates and semitone-tuned audio rates.",
        " * Accessors:  lfo_setFreq() (lfo.c), once per frq write.",
        " * Affiliates: lfo.h LFO_RATE_COUNT; tools/dsp_test/modules/lfo.c.",
        " */",
        "const float lfoRateHz[LFO_RATE_COUNT] = {",
    ]
    for i in range(0, COUNT, 4):
        lines.append("    " + ", ".join("%.9ef" % x for x in t[i:i + 4]) +
                     ",  /* %3d..%3d */" % (i, i + 3))
    lines.append("};")
    return "\n".join(lines) + "\n"


def main():
    t = table()
    ok = checks(t)
    text = render(t)
    if "--check" in sys.argv:
        ok = ok and open(PATH).read() == text
        print("lfo rate table", "OK" if ok else "FAILED")
        return 0 if ok else 1
    if not ok:
        print("lfo rate table: C1-C4 failed")
        return 1
    open(PATH, "w").write(text)
    print("wrote", PATH)
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

Run `python3 tools/plots/gen_lfo_rate_table.py` once to write
`Core/DSPAudio/lfoRateTable.c` (NEW FILE, generated; never edited by hand).

### S1.2 `Core/DSPAudio/lfo.h`

**MODIFY** l. 61 (`#define LFO_MAX_F 		200 //[Hz]`). The macro has no other
user (checked: only `lfo.c` l. 241).

```c
/*
 * LFO rate table (S094 P1 S1).
 *
 * What:       LFO_RATE_COUNT = 128 rates in Hz, one per frq value 0..127
 *             (lfoRateTable.c, generated by tools/plots/gen_lfo_rate_table.py):
 *             0 = stopped (0 Hz); 1..62 = 0.01 Hz (100 s) .. 7.71 Hz;
 *             63..127 = MIDI notes 0..64, 8.18 .. 329.63 Hz, a semitone apart.
 * Why:        user decision D-3 (S094): replaces LFO_MAX_F (200 Hz) and the
 *             cubic law. The top is E4 so that the 0..2 rate-modulation
 *             overlay (lfo.modNodeValue) keeps lfo_calc()'s step below 2^32
 *             (2.05 x 10^9 at the top) and the 1,378 Hz block rate still gives
 *             at least 4 values per cycle.
 * Inputs:     none (flash, 512 B).
 * Outputs:    read by lfo_setFreq().
 * Affiliates: lfo_calcPhaseInc() (the Hz -> phase step law, unchanged);
 *             tools/dsp_test/modules/lfo.c laws LF2/LF3.
 */
#define LFO_RATE_COUNT	128u
extern const float lfoRateHz[LFO_RATE_COUNT];
```

**MODIFY** l. 93–94 (the `sync` and `freq` members). The struct layout is
unchanged.

```c
	/*
	 * Clock-sync selector (S094 P1 S1: written only through lfo_setSync()).
	 *
	 * What:       0 = free rate (freq); 1..11 = 4/1 .. 1/32 of a bar at the
	 *             sequencer tempo (lfo_calcPhaseInc()).
	 * Why:        a store that skips lfo_setSync() leaves phaseInc at the
	 *             other mode's rate (the S094 bug). The lfo_sync rows carry
	 *             IM_SPECIAL_LFO_SYNC so every writer goes through it.
	 */
	uint8_t 	sync;
	/*
	 * The free rate in Hz (lfoRateHz[frq], S094 P1).
	 *
	 * What:       kept current by lfo_setFreq() in both modes, so a later
	 *             lfo_setSync(.., 0) returns to it without a frq write.
	 */
	float 		freq;
```

**MODIFY** l. 137–139 (the three prototypes): add their contracts, signatures
unchanged.

```c
/*
 * Phase step per block for a rate or a sync division.
 *
 * Inputs:     freq (Hz, used when sync is 0), sync 0..11.
 * Outputs:    the uint32 phase step per 32-sample block (2^32 = one cycle).
 * Accessors:  lfo_setFreq(), lfo_setSync(), instrumentManager_recalcRuntimeLfoSync().
 */
uint32_t lfo_calcPhaseInc(float freq, uint8_t sync);
/*
 * Set the free rate from a frq value (S094 P1: the lfoRateHz table).
 *
 * Inputs:     lfo; f = the frq value 0..127 as a float (the special writer's
 *             byte; a value past 127 clamps to 127).
 * Outputs:    lfo->freq = lfoRateHz[frq]; lfo->phaseInc for the current sync.
 * Accessors:  instrumentManager_writeSpecialRuntime() (IM_SPECIAL_LFO_RATE).
 */
void lfo_setFreq(Lfo *lfo, float f);
/*
 * Set the sync selector and recompute the phase step (S094 P1 S1).
 *
 * Inputs:     lfo; sync 0..11 (a value past 11 is treated as free by
 *             lfo_calcPhaseInc()'s default case, as before).
 * Outputs:    lfo->sync; lfo->phaseInc from lfo->freq (free) or the tempo.
 * Accessors:  instrumentManager_writeSpecialRuntime() (IM_SPECIAL_LFO_SYNC).
 */
void lfo_setSync(Lfo* lfo, uint8_t sync);
```

### S1.3 `Core/DSPAudio/lfo.c`

**MODIFY** l. 66–72 (the head of `lfo_calc()`): sync wins over rate
modulation (L-D2).

```c
float lfo_calc(Lfo *lfo)
{
	uint32_t oldPhase 		= lfo->phase;
	/*
	 * Rate modulation scale (S094 P1 S1, user decision L-D2: sync wins).
	 *
	 * What:       lfo->modNodeValue is the 0..2 overlay an LFO or velocity
	 *             target on lfo_rate writes every block (TYPE_SPECIAL_F,
	 *             modNode_updateValuePolarity()). While the LFO is synced the
	 *             scale is 1.0, so the step is exactly the sync division's.
	 * Why:        "the LFO always uses the sync rate when it is set" (user,
	 *             S094). A select, not a branch: the same instructions run in
	 *             both modes (constant cost).
	 * Cost:       measured in S1.9 with elf_blockpath.py on lfo_calc(); it must
	 *             not raise the per-block count, or be paid for inside the LFO
	 *             path (S089), else this select is removed and the S094 report
	 *             says so.
	 */
	const float rateScale	= lfo->sync ? 1.0f : lfo->modNodeValue;
	float inc = lfo->phaseInc*rateScale;
	uint32_t incInt = inc;
	lfo->phase 			   += incInt;
	const uint8_t overflow 	= oldPhase>lfo->phase;
```

**MODIFY** l. 236–243 (`lfo_setFreq()`).

```c
/*
 * Free rate from the frq value (S094 P1 S1; contract in lfo.h).
 *
 * What:       freq = lfoRateHz[frq]; frq is f rounded down and clamped to
 *             0..LFO_RATE_COUNT - 1; then the phase step for the current sync.
 * Why:        the table (D-3) replaces 200 x ((f + 1) / 128)^3. A table read
 *             is cheaper than the cubic, which matters because a step
 *             automation ramp can write frq on every block.
 */
void lfo_setFreq(Lfo *lfo, float f)
{
	/* the frq value: the special writer passes the byte as a float */
	const uint32_t frq = (f >= (float)(LFO_RATE_COUNT - 1u))
	                     ? (LFO_RATE_COUNT - 1u)
	                     : ((f > 0.0f) ? (uint32_t)f : 0u);

	lfo->freq = lfoRateHz[frq];
	lfo->phaseInc = lfo_calcPhaseInc(lfo->freq,lfo->sync);
}
```

**MODIFY** l. 244–249 (`lfo_setSync()`): the body is unchanged; add its
comment above it.

```c
/*
 * Sync selector write (S094 P1 S1; contract in lfo.h).
 *
 * What:       stores sync and recomputes phaseInc from lfo->freq (free) or
 *             the tempo (synced).
 * Why:        the S094 bug: the lfo_sync rows stored the byte without this
 *             recomputation, so a sync change waited for a frq or tempo
 *             change, and a load applied the rate with the previous sync byte.
 *             The rows now carry IM_SPECIAL_LFO_SYNC, whose writer calls this.
 */
```

### S1.4 `Makefile`

**ADD** after l. 167 (`  Core/DSPAudio/lfo.c \`).

```make
  Core/DSPAudio/lfoRateTable.c \
```

### S1.5 `Core/DSP/Instruments/InstrumentManager.h`

**ADD** after l. 215 (`    IM_SPECIAL_VOICE_CHAIN,`), before
`IM_SPECIAL_WRITER_COUNT`.

```c
    /*
     * S094 P1 S1: lfo_sync writer.
     *
     * What:       IM_SPECIAL_LFO_SYNC = 23 (next after IM_SPECIAL_VOICE_CHAIN,
     *             22); IM_SPECIAL_WRITER_COUNT becomes 24 (<= 32, the 5-bit
     *             writer field, asserted below). The writer calls
     *             lfo_setSync(), which stores the byte and recomputes the
     *             LFO's phase step.
     * Why:        the plain ROW_MENU store left phaseInc at the previous
     *             mode's rate until a frq or tempo change (S094 bug report),
     *             and a Kit/Scene/Instrument load applied lfo_rate (an earlier
     *             row) with the previous sound's sync byte.
     * Inputs:     slot, byte value (0..11; MENU_SYNC_RATES).
     * Outputs:    the slot LFO's sync and phaseInc.
     * Accessors:  instrumentManager_writeSpecialRuntime() switch.
     * Affiliates: lfo_setSync() (lfo.c); the five lfo_sync rows;
     *             tools/dsp_test/check_special_tags.py; the diagnostic
     *             classifier.
     */
    IM_SPECIAL_LFO_SYNC,
```

### S1.6 `Core/DSP/Instruments/InstrumentManager.c` — the writer

**ADD** after l. 3889 (the `return 1u; }` closing the
`IM_SPECIAL_LFO_OFFSET` case), before l. 3890 `case IM_SPECIAL_OSC_TYPE: {`.
Anchor: `case IM_SPECIAL_OSC_TYPE: {`.

```c
    case IM_SPECIAL_LFO_SYNC: {
        /*
         * lfo_sync writer (S094 P1 S1).
         *
         * What:       lfo_setSync(): stores the byte and recomputes phaseInc
         *             from the LFO's free rate (sync 0) or the tempo (1..11).
         * Why:        every path (menu, Kit/Scene/Instrument apply, step
         *             automation, MIDI) now changes the rate with the mode; the
         *             row order on load no longer matters, because lfo->freq is
         *             kept current by IM_SPECIAL_LFO_RATE in both modes.
         * Inputs:     slot, byteValue (0..11; larger values behave as free,
         *             lfo_calcPhaseInc()'s default case).
         * Outputs:    lfo->sync, lfo->phaseInc.
         * Affiliates: IM_SPECIAL_LFO_RATE (lfo_setFreq()); seq_setBpm() ->
         *             instrumentManager_recalcRuntimeLfoSync() (tempo).
         */
        Lfo *lfo = instrumentManager_runtimeLfo(slot);
        if (!lfo) return 0u;
        lfo_setSync(lfo, byteValue);
        return 1u; }
```

### S1.7 `Core/DSP/Instruments/InstrumentManager.c` — the diagnostic classifier

**ADD** after l. 4075 (`    if (strcmp(key, "lfo_offset") == 0) return IM_SPECIAL_LFO_OFFSET;`).

```c
    /*
     * S094 P1 S1: lfo_sync uses IM_SPECIAL_LFO_SYNC so a sync change
     * recomputes the LFO's phase step (the boot diagnostic compares every
     * descriptor tag against this rule).
     */
    if (strcmp(key, "lfo_sync") == 0) return IM_SPECIAL_LFO_SYNC;
```

### S1.8 The five `lfo_sync` rows

Each row keeps its key, labels, menu, member and type. Only the macro and the
tag change, so stored bytes, NRPN numbers and menu cells are unchanged.

| File | Line | Action |
|---|---:|---|
| `Core/DSP/Instruments/Drum/DrumParameters.c` | 276 | MODIFY |
| `Core/DSP/Instruments/Snare/SnareParameters.c` | 262 | MODIFY |
| `Core/DSP/Instruments/Cymbal/CymbalParameters.c` | 262 | MODIFY |
| `Core/DSP/Instruments/HiHat/HiHatParameters.c` | 265 | MODIFY |
| `Core/DSP/Instruments/Alkaline/AlkalineParameters.c` | 203 | MODIFY |

The same text in all five:

```c
    /* lfo_sync (S094 P1 S1): the special writer recomputes the LFO's phase
     * step with the mode (IM_SPECIAL_LFO_SYNC -> lfo_setSync()); a plain
     * byte store left the rate at the previous mode's until frq or the tempo
     * moved */
    ROW_MENU_SPECIAL("lfo_sync", "LFO", "ClockSnc", "snc", MENU_SYNC_RATES, MOD_NONE, lfo.sync, TYPE_UINT8, IM_SPECIAL_LFO_SYNC),
```

**`AlkalineParameters.c` ADD** after l. 65 (the `ROW_MENU` macro's second
line). The Alkaline table has no `ROW_MENU_SPECIAL` yet.

```c
/* ROW_MENU_SPECIAL: ROW_MENU with a constant DSP writer tag (S094 P1; the
 * Drum table's macro, DrumParameters.c l. 85-87, the same expansion) */
#define ROW_MENU_SPECIAL(key_, cat_, long_, short_, menu_, mod_, member_, type_, special_) \
    { key_, short_, long_, cat_, (uint8_t)(DTYPE_MENU | (menu_ << 4)), FLAGS_IMAGE, mod_, BIND_SPECIAL(member_, type_, special_) }
```

**`AlkalineParameters.c` ADD** after l. 303 (`#undef ROW_MENU`).

```c
#undef ROW_MENU_SPECIAL
```

### S1.9 `tools/dsp_test/check_special_tags.py`

**MODIFY** l. 71 (`        'lfo_offset': 'LFO_OFFSET'}`).

```python
        'lfo_offset': 'LFO_OFFSET',
        # S094 P1 S1: lfo_sync recomputes the LFO step through
        # IM_SPECIAL_LFO_SYNC (lfo_setSync()).
        'lfo_sync': 'LFO_SYNC'}
```

### S1.10 `tools/alkaline/make_init_alk.py`

**MODIFY** l. 43 (`MORPHABLE = {...}`).

```python
# rows whose macro sets INSTRUMENT_PARAM_FLAG_MORPHABLE (FLAGS_IMAGE).
# S094 P1: ROW_MENU_SPECIAL (lfo_sync) is FLAGS_IMAGE too; without it
# the [morph] section would lose lfo_sync and init.alk would change.
MORPHABLE = {"ROW", "ROW_SPECIAL", "ROW_MENU", "ROW_MENU_SPECIAL", "ROW_NOBIND_IMAGE"}
```

`make -C tools/dsp_test special_tags` runs `make_init_alk.py --check`. It must
still report a match: `init.alk`'s text does not change in S1.

### S1.11 NEW FILE `tools/dsp_test/modules/lfo.c`

```c
/*
 * LFO host test (S094 P1 S1).
 *
 * What:       compiles the real lfo_calc(), lfo_calcPhaseInc(), lfo_setFreq()
 *             and lfo_setSync() (lfo.c), the LFO_SR macro (lfo.h) and the real
 *             generated lfoRateHz[] (lfoRateTable.c), and checks:
 *               LF1  order independence: after rate -> sync, sync -> rate and
 *                    sync -> off writes, phaseInc equals
 *                    lfo_calcPhaseInc(freq, sync) for every sync 0..11 and
 *                    frq 0, 1, 63, 127; sync off returns to the free rate
 *                    without a frq write;
 *               LF2  the table: entry 0 is 0 Hz and gives phaseInc 0 (the LFO
 *                    holds its phase over 1,000 blocks); entry 1 is 0.01 Hz;
 *                    entries 63..127 are MIDI notes 0..64 to 1 ppm;
 *               LF3  increasing from entry 1; every step 1..2.8 semitones; the
 *                    step changes by <= 0.03 semitone between neighbours;
 *               LF4  sync wins (L-D2): with sync set, a modNodeValue of 0, 0.5
 *                    and 2 advances the phase by exactly phaseInc per block;
 *                    with sync off it advances by phaseInc x modNodeValue;
 *               LF5  the top rate x 2 (the overlay's maximum) stays below
 *                    2^32 in lfo_calc()'s step.
 *             Break builds (Makefile): a sync writer that only stores the
 *             byte must fail LF1; lfo_calc() without the sync select must
 *             fail LF4.
 * Why:        S094 P1 plan §3: the sync bug and the new rate law are host
 *             laws, not only a listening check.
 * Inputs:     generated fragments (Makefile target lfo).
 * Outputs:    failure counts per law; exit 1 on any failure.
 * Accessors:  make -C tools/dsp_test lfo.
 * Affiliates: tools/plots/gen_lfo_rate_table.py (the same C1..C4 checks on
 *             the generator side).
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#define REAL_FS ((float)44108.07291666667f)
#define OUTPUT_DMA_SIZE 32
#define LFO_RATE_COUNT 128u
#include "lfo_sr.inc"            /* the real LFO_SR macro (lfo.h) */

/* the waveform ids lfo_calc() switches on (lfo.h) */
#include "lfo_waves.inc"
#define SINE 0

/*
 * Lfo stand-in: the members the extracted functions read, by their real
 * names (as modules/alkaline.c stands in for Lfo). The phase law does not
 * depend on the other members.
 */
typedef struct {
	uint32_t phase, phaseInc;
	uint8_t waveform, retrigger;
	uint32_t phaseOffset;
	float rnd;
	uint8_t sync;
	float freq;
	float modNodeValue;
	uint8_t polarity;
} Lfo;

/* host stand-ins for what lfo_calc() reads */
int16_t sine_table[4097];
int16_t GetRngValue(void) { return 0; }
static uint16_t host_bpm = 120u;
uint16_t seq_getBpm(void) { return host_bpm; }

#include "lfo_rate_table.c"      /* the real lfoRateHz[] */
#include "lfo_funcs.c"           /* the real lfo_calc/_calcPhaseInc/_setFreq/_setSync */

static unsigned g_fail[5];
static const char *g_name[] = { "LF1", "LF2", "LF3", "LF4", "LF5" };

static void fail(int g, const char *what, double a, double b)
{
	if (g_fail[g] < 5u)
		printf("  %s FAIL: %s (%g, %g)\n", g_name[g], what, a, b);
	g_fail[g]++;
}

/* LF1: every write order ends on the law */
static void test_order(void)
{
	static const uint8_t frqs[] = { 0u, 1u, 63u, 127u };

	for (unsigned f = 0u; f < sizeof frqs; f++)
		for (uint8_t s = 0u; s <= 11u; s++) {
			Lfo a, b;

			memset(&a, 0, sizeof a);
			memset(&b, 0, sizeof b);
			/* rate then sync */
			lfo_setFreq(&a, frqs[f]);
			lfo_setSync(&a, s);
			/* sync then rate */
			lfo_setSync(&b, s);
			lfo_setFreq(&b, frqs[f]);
			if (a.phaseInc != lfo_calcPhaseInc(a.freq, s)) fail(0, "rate then sync", f, s);
			if (b.phaseInc != lfo_calcPhaseInc(b.freq, s)) fail(0, "sync then rate", f, s);
			/* back to free without a rate write */
			lfo_setSync(&a, 0u);
			if (a.phaseInc != lfo_calcPhaseInc(lfoRateHz[frqs[f]], 0u))
				fail(0, "sync off returns to the free rate", f, s);
		}
}

/* LF2 / LF3 / LF5: the table */
static void test_table(void)
{
	Lfo l;

	if (lfoRateHz[0] != 0.0f) fail(1, "entry 0 is 0 Hz", lfoRateHz[0], 0);
	if (fabs(lfoRateHz[1] - 0.01) > 1e-7) fail(1, "entry 1 is 0.01 Hz", lfoRateHz[1], 0.01);
	for (unsigned i = 63u; i < 128u; i++) {
		const double want = 440.0 * pow(2.0, ((double)(i - 63u) - 69.0) / 12.0);

		if (fabs(lfoRateHz[i] / want - 1.0) > 1e-6) fail(1, "semitone grid", i, lfoRateHz[i]);
	}
	/* frq 0 stops: phaseInc 0, the phase holds */
	memset(&l, 0, sizeof l);
	l.modNodeValue = 1.0f;
	l.phase = 0x12345678u;
	lfo_setFreq(&l, 0.0f);
	for (int b = 0; b < 1000; b++)
		(void)lfo_calc(&l);
	if (l.phaseInc != 0u || l.phase != 0x12345678u) fail(1, "frq 0 holds the phase", l.phaseInc, 0);
	for (unsigned i = 1u; i < 127u; i++) {
		const double st = 12.0 * log2((double)lfoRateHz[i + 1u] / lfoRateHz[i]);

		if (!(st >= 0.999 && st <= 2.8)) fail(2, "step 1..2.8 semitones", i, st);
		if (i < 126u) {
			const double nx = 12.0 * log2((double)lfoRateHz[i + 2u] / lfoRateHz[i + 1u]);

			if (fabs(nx - st) > 0.03) fail(2, "step changes <= 0.03 semitone", i, nx - st);
		}
	}
	if (2.0 * lfo_calcPhaseInc(lfoRateHz[127], 0u) >= 4294967296.0)
		fail(4, "top x 2 below 2^32", lfo_calcPhaseInc(lfoRateHz[127], 0u), 0);
}

/* LF4: sync wins over the rate overlay */
static void test_syncwins(void)
{
	static const float mods[] = { 0.0f, 0.5f, 2.0f };

	for (unsigned m = 0u; m < 3u; m++)
		for (uint8_t s = 0u; s < 2u; s++) {
			Lfo l;
			uint32_t before;

			memset(&l, 0, sizeof l);
			lfo_setFreq(&l, 90.0f);
			lfo_setSync(&l, s ? 10u : 0u);
			l.modNodeValue = mods[m];
			before = l.phase;
			(void)lfo_calc(&l);
			{
				const uint32_t want = s ? l.phaseInc
				                        : (uint32_t)((float)l.phaseInc * mods[m]);

				if (l.phase - before != want) fail(3, "phase step", m, s);
			}
		}
}

int main(void)
{
	unsigned total = 0u;

	test_order();
	test_table();
	test_syncwins();
	for (unsigned g = 0u; g < 5u; g++) {
		printf("lfo %s: %u failure(s)\n", g_name[g], g_fail[g]);
		total += g_fail[g];
	}
	return total ? 1 : 0;
}
```

### S1.12 `tools/dsp_test/Makefile`

**MODIFY** l. 56–62 (`.PHONY` and `all`): add `lfo lfo_rate_table` to both.

```make
.PHONY: all clean oscillator modulation voice_control sequencer \
        instrument_manager samples sampleoptimize run_modes special_tags \
        stock_tables voicefold fold_tables crumpbit crumpbit_tables \
        crumpbit_elf_check crumpbit_cpu_check alkaline alkaline_tables \
        elf_p4_snapshot elf_p4_check seq_steplen lfo lfo_rate_table
all: oscillator modulation voice_control sequencer seq_steplen instrument_manager samples \
     sampleoptimize run_modes special_tags stock_tables voicefold fold_tables \
     crumpbit crumpbit_tables alkaline alkaline_tables lfo lfo_rate_table
```

**ADD** after l. 113 (the end of the `modulation` target).

```make
# S094 P1 S1: the real LFO phase law and rate table (modules/lfo.c).
# Breaks: (1) the sync writer storing the byte only (lfo_setSync() without
# its recomputation) must fail LF1; (2) lfo_calc() scaling by modNodeValue
# while synced must fail LF4.
lfo: | $(GEN)
	$(EX) --src $(ROOT)/Core/DSPAudio/lfo.h --macro LFO_SR --out $(GEN)/lfo_sr.inc
	$(EX) --src $(ROOT)/Core/DSPAudio/lfo.h --macro LFO_SINE --macro LFO_TRI \
	  --macro LFO_SAW_UP --macro LFO_SAW_DOWN --macro LFO_REC --macro LFO_NOISE \
	  --macro LFO_EXP_UP --macro LFO_EXP_DOWN --out $(GEN)/lfo_waves.inc
	$(EX) --src $(ROOT)/Core/DSPAudio/lfoRateTable.c --object lfoRateHz \
	  --out $(GEN)/lfo_rate_table.c
	$(EX) --src $(ROOT)/Core/DSPAudio/lfo.c --func lfo_calc --func lfo_calcPhaseInc \
	  --func lfo_setFreq --func lfo_setSync --out $(GEN)/lfo_funcs.c
	$(HOSTCC) $(HOSTFLAGS) -I$(GEN) modules/lfo.c -o build/lfo -lm
	./build/lfo
	mkdir -p build/break_lfo1 build/break_lfo2
	sed 's/lfo->sync = sync;/lfo->sync = sync; return;/' \
	  $(GEN)/lfo_funcs.c > build/break_lfo1/lfo_funcs.c
	$(HOSTCC) -Ibuild/break_lfo1 $(HOSTFLAGS) -I$(GEN) modules/lfo.c -o build/lfo_break1 -lm
	@if ./build/lfo_break1 >/dev/null 2>&1; then \
	  echo "lfo_break1: FAILED (a store-only sync writer not caught)"; exit 1; \
	else echo "lfo_break1: caught (law LF1 fails as expected)"; fi
	sed 's/lfo->sync ? 1.0f : lfo->modNodeValue/lfo->modNodeValue/' \
	  $(GEN)/lfo_funcs.c > build/break_lfo2/lfo_funcs.c
	$(HOSTCC) -Ibuild/break_lfo2 $(HOSTFLAGS) -I$(GEN) modules/lfo.c -o build/lfo_break2 -lm
	@if ./build/lfo_break2 >/dev/null 2>&1; then \
	  echo "lfo_break2: FAILED (a synced LFO following the overlay not caught)"; exit 1; \
	else echo "lfo_break2: caught (law LF4 fails as expected)"; fi

lfo_rate_table:
	cd $(ROOT) && python3 tools/plots/gen_lfo_rate_table.py --check
```

The break directories come first on the include path, so the broken
`lfo_funcs.c` replaces the real one; the same scheme as the
`instrument_manager` breaks.

### S1.13 Gates for S1

1. **Build and tags:** `make all` builds; the boot tag diagnostic reports 0
   mismatches; `special_tags` reports OK (one more tagged row per table).
2. **Host:** `make -C tools/dsp_test lfo lfo_rate_table special_tags alkaline`
   all pass. AL-OD0 is unchanged: the bench has no LFO path.
3. **Loops:** `elf_p4_check` against `s0`: 0 MISSING, mixer 0 GREW.
4. **Per block:** `elf_blockpath.py` on `lfo_calc` (or on its inlined copy in
   `instrumentManager_dispatchRuntimeLfos`) against S0.
   - **0 grew:** accept.
   - **It grew:** remove the select from S1.3 (the overlay keeps applying
     while synced) and note L-D2 as not taken in the step report.
5. **Snapshot:** `elf_p4_snapshot P4BASE=baselines/s094p1/s1`.

**Out of date for the user:**
- every saved `frq` plays at the new rate;
- `init.alk`'s `lfo_rate=0` is now "stopped" (its amounts are 0, so it is
  silent either way).

---

## S2 — Remove the crash wave; the card sample

The order inside S2 matters. The card sample is generated **from the ROM array
before the array is deleted** (S2.1). The card scan (S2.2) runs before the
removal, while `Cym` still has its meaning.

### S2.1 NEW FILE `tools/samples/make_crash_wav.py`

```python
#!/usr/bin/env python3
"""Write the crash wave as a card sample (S094 P1 S2, decision C-D2).

What:       reads crashSample[32769] from Core/DSPAudio/Samples.c (unsigned
            8-bit: 32,768 points and a guard point equal to point 0) and
            writes SD_CARD/samples/02_crash.wav:
              - PCM, 16-bit, mono, 44,100 Hz in the header (the user-sample
                pitch law is f x 2^22 / REAL_FS: 1,024 points per nominal
                period, the same as the ROM wave's <<17 law, so the header
                rate is not used for pitch);
              - 32,769 frames, point k = (u_k - 127) x 256 exactly
                (u in 15..254 gives -28,416..+32,512: no clamp);
              - a smpl chunk with one forward loop, dwStart 0, dwEnd 32,767
                (inclusive; the installer stores loop_end = dwEnd + 1,
                filesystem.c filesystem_parseWav()), so the 32,769th frame is
                the guard the reader interpolates into at the seam.
Why:        the crash wave leaves the firmware (C-D1: -3 per sample on Drum
            and Snare, -6 on Cymbal/HiHat, 32,769 B of flash, ~540 B ITCM).
            The same points come back as a user sample, playable as slp (a
            carrier typ) or as an l entry (a modulator).
            Provenance (Samples.c header): freesound, X-Act heavy metal kit,
            Sabian AAX Studio Crash 16", CC0 1.0 (public domain); cut, EQ'd and
            compressed by Julian Schmidt; originally 8-bit, 32 kHz.
Checks:     K1 every frame equals (u - 127) x 256 of the array (read back);
            K2 frame 32,768 equals frame 0; K3 the smpl loop reads back
            0..32,767. Exit 1 on any failure.
Inputs:     Core/DSPAudio/Samples.c (must still exist: run before S2.3).
Outputs:    SD_CARD/samples/02_crash.wav.
Accessors:  run once by hand in S2; the file is kept in the tree afterwards.
Affiliates: SampleInstall.c sampleInstall_finishSample() (the loop rule);
            S094_P0_SAMPLE_VOICE_REDUCTIONS.md §2.3.
"""
import re
import struct
import sys

SRC = "Core/DSPAudio/Samples.c"
OUT = "SD_CARD/samples/02_crash.wav"
POINTS = 32768


def read_rom():
    """The 32,769 unsigned bytes of crashSample[], in order."""
    text = open(SRC).read()
    body = text[text.index("crashSample[32769]"):]
    body = body[body.index("{") + 1:body.index("};")]
    body = re.sub(r"/\*.*?\*/", "", body, flags=re.S)
    vals = [int(x, 16) for x in re.findall(r"0x[0-9A-Fa-f]{2}", body)]
    if len(vals) != POINTS + 1:
        sys.exit(f"make_crash_wav: {len(vals)} points, expected {POINTS + 1}")
    return vals


def wav_bytes(frames):
    """RIFF/WAVE with fmt, data and smpl (one forward loop 0..POINTS-1)."""
    data = struct.pack("<%dh" % len(frames), *frames)
    fmt = struct.pack("<HHIIHH", 1, 1, 44100, 44100 * 2, 2, 16)
    # smpl: manufacturer, product, period (ns at 44.1 kHz), unity note 60,
    # pitch fraction, SMPTE format/offset, one loop, no sampler data; then
    # the loop: id 0, type 0 (forward), start, end (inclusive), fraction, count
    smpl = struct.pack("<IIIIIIIII", 0, 0, 22676, 60, 0, 0, 0, 1, 0)
    smpl += struct.pack("<IIIIII", 0, 0, 0, POINTS - 1, 0, 0)
    chunks = (b"fmt " + struct.pack("<I", len(fmt)) + fmt +
              b"data" + struct.pack("<I", len(data)) + data +
              b"smpl" + struct.pack("<I", len(smpl)) + smpl)
    return b"RIFF" + struct.pack("<I", 4 + len(chunks)) + b"WAVE" + chunks


def main():
    rom = read_rom()
    frames = [(u - 127) * 256 for u in rom]
    blob = wav_bytes(frames)
    with open(OUT, "wb") as fh:
        fh.write(blob)
    # read back: K1..K3
    back = open(OUT, "rb").read()
    d = back.index(b"data") + 8
    got = list(struct.unpack("<%dh" % len(frames), back[d:d + 2 * len(frames)]))
    s = back.index(b"smpl") + 8 + 36
    start, end = struct.unpack("<II", back[s + 8:s + 16])
    ok = got == frames and got[POINTS] == got[0] and (start, end) == (0, POINTS - 1)
    print("crash wav", "OK" if ok else "FAILED", OUT)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
```

Run: `python3 tools/samples/make_crash_wav.py`. Commit the output file
`SD_CARD/samples/02_crash.wav` (a card asset; the user installs it with
`Load:[Samples]`).

### S2.2 NEW FILE `tools/samples/scan_wave_refs.py`

```python
#!/usr/bin/env python3
"""List the card files whose stored wave bytes change meaning (S094 P1 S2).

What:       walks SD_CARD (or a path given as the argument) for Instrument
            files (*.drm, *.snr, *.cym, *.hat; in /Instrument and inside Kit
            folders) and reads their [params] section:
              - osc1_wave = 5 with osc1_type 0 or absent: a Cym carrier; after
                S2 it clamps to Noi (OSC_STOCK_LAST);
              - a modulator wave (Drum osc2_wave; Cymbal/HiHat osc2_wave and
                osc3_wave) = 5: the Cym modulator; after S2 it is the first
                user entry (s01) when a sample is installed;
              - a modulator wave >= 6: a user entry; after S2 every one plays
                the next entry in the list (the meaning of each index moves
                down one).
            Prints one line per affected file and key; a count at the end.
Why:        C-D3: no remap code (standing rule: no backward compatibility);
            the user re-points by hand from this list.
Inputs:     the card tree; argv[1] optional root (default SD_CARD).
Outputs:    the list on stdout; exit 0.
Accessors:  run by hand in S2, before S2.3.
Affiliates: Oscillator.h OSC_SAMPLE_START (6 -> 5), OSC_STOCK_LAST (CRASH ->
            NOISE).
"""
import os
import sys

MODULATORS = {"drm": ("osc2_wave",), "cym": ("osc2_wave", "osc3_wave"),
              "hat": ("osc2_wave", "osc3_wave"), "snr": ()}


def params(path):
    """The [params] key=value pairs of one instrument file."""
    out, on = {}, False
    for line in open(path, errors="replace"):
        line = line.strip()
        if line.startswith("["):
            on = line == "[params]"
        elif on and "=" in line:
            k, v = line.split("=", 1)
            out[k] = v
    return out


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else "SD_CARD"
    hits = 0
    for d, _, files in os.walk(root):
        for f in sorted(files):
            ext = f.rsplit(".", 1)[-1].lower()
            if ext not in MODULATORS:
                continue
            p = params(os.path.join(d, f))
            wav = p.get("osc1_wave", "")
            if wav == "5" and p.get("osc1_type", "0") == "0":
                print(f"{os.path.join(d, f)}: osc1_wave=5 (Cym carrier) -> Noi")
                hits += 1
            for k in MODULATORS[ext]:
                v = p.get(k, "")
                if v.isdigit() and int(v) >= 5:
                    what = "Cym modulator -> s01" if v == "5" else "user entry moves down one"
                    print(f"{os.path.join(d, f)}: {k}={v} ({what})")
                    hits += 1
    print(f"scan_wave_refs: {hits} reference(s)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

Run `python3 tools/samples/scan_wave_refs.py` and hand the list to the user.
`SD_CARD_RESTORED` and `SD_CARD_TEST_1` can be scanned by passing the path.

### S2.3 `Core/DSPAudio/Oscillator.h`

**REMOVE** l. 54 (`#define CRASH				0x05`).

**MODIFY** l. 55–69 (the stock-wave comment, `OSC_STOCK_LAST`,
`OSC_SAMPLE_START`).

```c
/*
 * The built-in (stock) waves are SINE..NOISE (S093 P3; S094 P1 S2).
 *
 * What:       OSC_STOCK_LAST is the last built-in wave: the wav-row clamp and
 *             the menu range. User entries (modulator list) start at
 *             OSC_SAMPLE_START.
 * Why:        the S086 stock table waves Re3/Sw3 (0x06/0x07) were removed in
 *             S093 (S093_P0_ALKALINE_C_FUNCTIONS_CODE.md §10); their recipes and
 *             generator stay in tools/plots/ (INSTRUMENTS_DSP_REFERENCE.md
 *             §6.1.2). S094 P1 S2 removed CRASH (0x05): its plain loop (30 per
 *             sample) set the plain-read ceiling of every modulator and plain
 *             oscillator; the same points are the card sample
 *             SD_CARD/samples/02_crash.wav (tools/samples/make_crash_wav.py).
 *             Restoring a stock wave moves OSC_SAMPLE_START up again.
 */
#define OSC_STOCK_LAST		NOISE
#define OSC_SAMPLE_START 	0x05
```

**MODIFY** l. 226–229 (part of the `position` member comment): "for CRASH the
cycle is the complete sample" goes.

```c
	 * What:       position is the 0..127 osc1_position value. For the phase
	 *             waves (osc_positionIsPhase()) it is position/128 of a cycle;
	 *             under typ pwm (S094 P1) it is the pulse width (1..127; 0 acts
	 *             as 1). positionApplied
```

**REMOVE** l. 394–397 (`int16_t calcSampleOscFm(...)`, its separator, the
comment `//used to play back the crash cymbal sample` and
`void calcSampleOscFmBlock(...)`).

**REMOVE** l. 433–435 (`void calcSampleOscBlock(...)`, its separator,
`int16_t calcSampleOsc(...)`).

**MODIFY** l. 484–490 (the `osc_syncClockInc()` contract bullets).

```c
 *             - Sine, table (incl. the stock table waves) and noise phases
 *               already are.
 *             - User samples (a modulator's s / l entry: Q19.12, 1,024 points
 *               per nominal period) are scaled << 10 after a clamp to
 *               0x003FFFFF, so the shift cannot overflow; a master at or above
 *               fs then resets the slave on every sample.
 * Why:        plan A1/A4: in P1 every master is a clock at its frequency.
 *             User samples act as noise (one reset per nominal period), not
 *             as their audio. (S094 P1 S2: the crash << 5 rule went with
 *             CRASH.)
```

**MODIFY** l. 531–532 (the `calcNextOscSampleSyncBlock()` contract).

```c
 *             - Synced: Sin, Tri, Saw, Rec (S094 P1 S2: Crash removed).
```

**MODIFY** l. 638–639 (the `osc_isTableWave()` contract).

```c
 * What:       1 for TRI, SAW, REC; 0 for SINE, NOISE and user samples (the
 *             S086 stock table waves were removed in S093, CRASH in S094).
```

**MODIFY** l. 685–688 and l. 700–704 (`osc_positionIsPhase()`).

```c
 * What:       SINE and all table waves use the full 32-bit phase range;
 *             NOISE and v1 user samples retain their existing no-position
 *             behaviour (S094 P1 S2: CRASH removed).
 * Why:        one phase unit is position/128 of the wave.
...
static inline uint8_t osc_positionIsPhase(const uint8_t waveform)
{
	return (uint8_t)(waveform == SINE || osc_isTableWave(waveform));
}
```

### S2.4 `Core/DSPAudio/Oscillator.c`

| Lines | Action | What goes |
|---|---|---|
| 52 | REMOVE | `#include "Samples.h"` |
| 117–121 | REMOVE | `freq2PhaseIncr32767()` and the separator above it. Its only firmware user was `osc_calcSampleFreqValue()`; the host test's use goes in S2.8 |
| 292–294 | REMOVE | `case CRASH:` in `calcNextOscSampleFmBlock()` |
| 331–333 | REMOVE | `case CRASH:` in `calcNextOscSampleFm()` |
| 421–423 | REMOVE | `case CRASH:` in `calcNextOscSampleBlock()` |
| 456–458 | REMOVE | `case CRASH:` in `calcNextOscSample()` |
| 516–614 | REMOVE | `calcSampleOscFmBlock()`, `calcSampleOscFm()`, `calcSampleOscBlock()`, `calcSampleOsc()` with their separators |
| 790–792 | REMOVE | `case CRASH:` in `osc_renderPlainWave()` |
| 922–957 | REMOVE | `calcSampleOscSyncBlock()` and its comment |
| 992–994 | REMOVE | `case CRASH:` in `osc_renderSyncWave()` |
| 1125–1158 | REMOVE | `calcSampleOscRingBlock()` and its comment |
| 1189–1191 | REMOVE | `case CRASH:` in `calcNextOscSampleRingBlock()` |
| 1241–1245 | REMOVE | `osc_calcSampleFreqValue()` and its separator |
| 1333–1335 | REMOVE | `case CRASH:` in `osc_setFreq()` (with the blank line above it) |

**MODIFY** l. 802–826 (`osc_syncClockInc()`: the crash branch and its comment
go).

```c
/*
 * Sync clock increment for the FM-page oscillator (S085 P1).
 * Contract: Oscillator.h. User samples run in Q19.12 point units (f x 2^22 /
 * fs), which is 1/1,024 of the 2^32-per-period clock, hence << 10 after the
 * clamp. S094 P1 S2: the crash << 5 branch went with CRASH.
 */
uint32_t osc_syncClockInc(const OscInfo* master)
{
	uint32_t inc = master->phaseInc;

	if (master->oscType == OSC_TYPE_MODLIST &&
	    (master->sampleCacheLooped == OSC_BIND_SMP ||
	     master->sampleCacheLooped == OSC_BIND_SLP)) {
		/* S087 D: a Q19.12 sample master is scaled <<10 to clock units. */
		if (inc > 0x003FFFFFu)
			inc = 0x003FFFFFu;
		inc <<= 10;
	}
	return inc;
}
```

**MODIFY** l. 962 (in the `osc_renderSyncWave()` comment).

```c
 * What:       dispatches Sin/Tri/Saw/Rec to their sync copies (S094 P1 S2:
 *             Crash removed). NOISE renders unsynced through
```

**MODIFY** l. 1487 (the `osc_userCount()` comment's stale "wav -> 8").

```c
 * What:       wav -> OSC_SAMPLE_START stock entries (5 after S094 P1 S2);
 *             smp/slp -> sample count; wtb -> wavetable count; pwm (S094 P1
 *             S3) -> OSC_PWM_WAVE_COUNT (Sin, Tri, Saw, Rec).
```

### S2.5 DELETE FILES `Core/DSPAudio/Samples.c`, `Core/DSPAudio/Samples.h`

`Samples.c` holds only `crashSample[]`, and `Samples.h` only its declaration
(checked). The provenance note moves into `make_crash_wav.py`'s docstring
(S2.1).

`Makefile` **REMOVE** l. 173 (`  Core/DSPAudio/Samples.c \`).

### S2.6 `Core/Menu/MenuText.h`

**MODIFY** l. 145–150.

```c
/* The built-in waves SINE..NOISE (Oscillator.h); the count must equal
** OSC_SAMPLE_START (asserted in menu.c). User samples follow as sNN.
** S093 P3: the S086 Re3/Sw3 entries were removed with their tables.
** S094 P1 S2: Cym was removed (the card sample 02_crash.wav replaces it). */
static const char waveformNames[][4] = {
    {5}, {"Sin"},{"Tri"},{"Saw"},{"Rec"},{"Noi"},
};
```

`menu.c` l. 12518's assert (`waveformNames` count == `OSC_SAMPLE_START`) holds
with 5 = 5; no edit.

### S2.7 `Core/DSP/Instruments/Drum/DrumVoice.c`

**MODIFY** l. 367–369 (comment only).

```c
		 *             position << 25 (position/128 of the phase range) for Sin,
		 *             Tri, Saw and Rec, 0 for noise and v1 user samples (S094 P1
		 *             S2: Cym removed). The helper also sets
```

### S2.8 Host tests

**`tools/dsp_test/osc_host.h`**
- **MODIFY** l. 36: `/* wavetable.h (writable on the host) */`.
- **REMOVE** l. 42: `extern unsigned char crashSample[32769];`.

**`tools/dsp_test/modules/alkaline.c`**
- **REMOVE** l. 98: `unsigned char crashSample[32769];`.

**`tools/dsp_test/modules/oscillator.c`**

| Lines | Action | Change |
|---|---|---|
| 66 | REMOVE | `unsigned char crashSample[32769];` |
| 121–122 | REMOVE | the `crashSample` fill in `fill_random_tables()` |
| 136–138 | REMOVE | the `crashSample` fill in `fill_shape_tables()` |
| 177 | MODIFY | `static const uint8_t waves[] = { SINE, TRI, SAW, REC, NOISE };` |
| 225 | MODIFY | `static const uint8_t waves[] = { SINE, TRI, SAW, REC, NOISE };` |
| 261 | MODIFY | `static const uint8_t waves[] = { SINE, TRI, SAW, REC };` |
| 269 | MODIFY | `const uint32_t slaveInc = 0x03000000u;` |
| 344–348 | REMOVE | the "S5 crash << 5" and "S5 crash clamp" checks |
| 363 | MODIFY | the second oscillator of the equal-rate check becomes a modulator bound to a one-shot user sample (the << 10 law), below |
| 432–433 | MODIFY | `OSC_SAMPLE_START != 5` / `OSC_STOCK_LAST != NOISE` |
| 437 | MODIFY | `const uint32_t want = 0x00123456u;` (no stock wave is scaled) |
| 467–468 | REMOVE | the `CRASH` branch of `render()` |
| 491–497 | REMOVE | the `CRASH` branch of `expected_sample()` |
| 509 | MODIFY | `static const uint8_t phase_waves[] = { SINE, TRI, SAW, REC };` |
| 518–519 | MODIFY | `const int phase_wave = (w == SINE || w == TRI || w == SAW || w == REC);` |

**MODIFY** l. 363 (the equal-rate check's second oscillator) and the comment
above l. 361.

```c
    /*
     * S5 (S094 P1 S2: the crash master is gone): a modulator bound to a user
     * sample clocks at the same rate as a table wave of the same frequency.
     * Its phaseInc is the Q19.12 sample law (osc_userBind(): f x 2^22 / fs),
     * which osc_syncClockInc() scales << 10 into 2^32-per-period units.
     */
    for (unsigned f = 0u; f < 3u; ++f) {
        OscInfo tri = make_osc(TRI, 3u, 0u, freq2PhaseIncr1024(freqs[f]));
        OscInfo cr = make_osc(OSC_SAMPLE_START, 3u, 0u,
                              (uint32_t)((freqs[f] * 4194304.0f) / REAL_FS));
        cr.oscType = OSC_TYPE_MODLIST;
        cr.sampleCacheLooped = OSC_BIND_SMP;
```

The rest of the loop body (l. 364–379) is unchanged. The clock comparison
allows one Q19.12 quantization step (1,024 phase-clock units after `<< 10`);
the reset-count tolerance remains one.

### S2.9 Gates for S2

1. `make all && make img`; `make -C tools/dsp_test all`.
2. `elf_p4_check P4BASE=baselines/s094p1/s1`. Expected MISSING, by design:
   - the crash loops `calcSampleOscBlock`, `calcSampleOscFmBlock`,
     `calcSampleOscSyncBlock`, `calcSampleOscRingBlock`;
   - the crash case inside `calcNextOscSampleBlock`.

   Every other loop must be present.
   - **Accepted S089 offset exception (user approved 2026-11-11):** source
     pairing reports two raw +1 groups in `mixer_calcNextSampleBlock()`:
     `calcFmSineBlock` in Cymbal 28 → 29 and HiHat 29 → 30 instructions per
     sample. All other matched loop groups are equal or lower. The removed
     crash paths save about 6 instructions/sample on the dearest Cymbal/HiHat
     path (the slp FM carrier plus two crash modulators), about 3 per itp
     crash-modulator slot, and about 3 on Drum's dearest path. This funds the
     two +1 groups under S089. The whole-voice path accounting falls from
     261.3 to 258.3 for Drum and from about 292.9 to 286.9 for the dearest
     Cymbal/HiHat path. Keep the two raw rises visible in reports rather than
     describing the source-pair result as literally 0 GREW; all other paired
     loop groups are equal or lower. S6 must measure these same sine-FM paths
     again in the final image.
   - One bounded dispatch-shape probe retained `case OSC_SAMPLE_START` on
     the existing zero-fill default. The `-g` image stayed at 679,876 B flash
     and the two source-paired counts remained 28 → 29 and 29 → 30, so the
     probe was removed. `nm -S` has no standalone `calcFmSineBlock` or
     `calcNextOscSampleFmBlock` symbols in either LTO image; readelf confirms
     they were inlined. Further S2-specific tuning is not justified.
3. `link_budget.py`:
   - ITCM falls by ≈ 540 B or more;
   - flash falls by ≈ 33 KB.
4. **Per-voice loops** (`elf_looppath.py --cond-heads --in-range`): the
   plain `slp` path is `osc_slpPlainBlock`, 27 instructions; the stock
   `calcNextOscSampleBlock` dispatch path is 24 in the linked S2 image.
5. **Snapshot:** `baselines/s094p1/s2`.

---

## S3 — PWM on Drum, Snare, Cymbal, HiHat

### S3.0 The design in one place (plan §5, findings 1–4)

**Type and bind**
- `typ` 4 = `pwm` on osc1 of Drum, Snare, Cymbal and HiHat.
- `OSC_BIND_PWM` (3) is the bind kind; the typed path dispatches on it.

**Width**
- `p` = `positionApplied` clamped to 1..127 (P-D3).
- `positionApplied` slews toward `pos` by ≤ 2 per block (P-D7) and snaps on a
  trigger.

**Per wave**

| `wav` | Form | Read |
|---|---|---|
| `Sin` | **WS**: warp on `sine_table` (4,096 points + guard, a −cos) | rising half over `[0, W)`, falling half over `[W, 2³²)`, `W = p << 25`; no DC (finding 1) |
| `Tri` | **WT**: warp on `triTable[row + lift(p)]` | the same split; rounded octave lift (finding 4) |
| `Saw` | **D**, sign +1: `y = s(φ) + s(φ − w)` | two reads of the descending saw row `w = p × 8` points apart; level `64 / max(p, 128 − p)` |
| `Rec` | **D**, sign −1: `y = s(φ) − s(φ − w)` | the band-limited pulse, high for the first `w` (finding 2) |

**Arithmetic**
- **Warp:**
  `ψ = (φ < W) ? umulh(φ, Ra) << 6 : (umulh(φ − W, Rb) << 6) + 2³¹`, with
  `Ra = (2³² − 1)/p` and `Rb = (2³² − 1)/(128 − p)` from a table. Both
  halves are computed and one is selected: constant cost.
- **Interpolation:** integer, with a Q15 fraction. Bound: a neighbouring-point
  step ≤ 32,767, so the D difference ≤ 65,534, and 65,534 × 32,767 < 2³¹
  (finding 2).
- **Store:** `SSAT16((o × gQ14) >> 14)`, with `gQ14 = level × render gain`.
  The render gain is ≤ 1 at every call site (checked: Drum `1 − amt` / 1,
  Snare `1 − mix`, Cymbal 1, HiHat 0.5).

**Loops and control**
- **Loops:** fifteen in `.itcm.s094p1`, one per {D, WT, WS} × {plain, FM, FM
  ramp, ring, sync}. Each is `noinline, noclone`, its own symbol.
- **Per block, flash:** `osc_pwmPlan()` fills a plan from the oscillator; five
  role functions (`osc_pwmRender*()`) pick the loop. The existing ITCM typed
  dispatchers gain one `case OSC_BIND_PWM:` each.
- **Never claims** an interpolation slot.

### S3.1 NEW FILE `tools/plots/gen_pwm_tables.py`

```python
#!/usr/bin/env python3
"""Generate the PWM oscillator tables (S094 P1 S3).

What:       writes Core/DSPAudio/oscPwmTables.h and oscPwmTables.c:
              oscPwmRecip[128]   uint32: floor((2^32 - 1) / n) for n = 1..127
                                 ([0] = 0xFFFFFFFF, never read: p >= 1). The
                                 warp's half-cycle scales Ra = recip[p],
                                 Rb = recip[128 - p] (umulh(x, recip[n]) is
                                 x / n rounded down, no division per block);
              oscPwmLevelQ14[64] int16: round(16384 x 64 / (64 + m)),
                                 m = max(p, 128 - p) - 64 (0..63): the D form's
                                 level 64 / max(p, 128 - p), which keeps the
                                 two-saw sum and difference at the table's peak
                                 (a difference of a saw ramp at width w peaks at
                                 2 max(w, 1 - w));
              oscPwmLift[128]    uint8: round(log2(64 / min(p, 128 - p))) for
                                 p = 1..127 ([0] = [1]): octave rows the warped
                                 triangle moves up, because the warp compresses
                                 one half-cycle by 64 / min(p, 128 - p).
Why:        the per-block plan reads constants instead of dividing (no VDIV,
            no UDIV per block) and the loops stay division-free. The lift is
            rounded, not ceiled: a ceiling lifts a whole octave at pos 63 (a
            1.6 % compression) and the triangle's harmonics fall as 1/n^2, so
            the rounding's aliasing stays far below the table's own (plan
            finding 4).
Checks:     T1 recip[n] x n <= 2^32 - 1 < (recip[n] + 1) x n for n = 1..127;
            T2 level[0] = 16384, strictly decreasing, level[63] = 8257;
            T3 lift[64] = 0, lift[1] = lift[127] = 6, symmetric
            (lift[p] = lift[128 - p]); T4 --check compares both files byte for
            byte. Exit 1 on any failure.
Inputs:     none.
Outputs:    the two files, or a pass/fail line with --check.
Accessors:  tools/dsp_test/Makefile target pwm_tables.
Affiliates: Oscillator.c osc_pwmPlan(); AlkalineVoice.c alk_renderPwm();
            tools/dsp_test/modules/oscillator.c group pwm.
"""
import math
import sys

H_PATH = "Core/DSPAudio/oscPwmTables.h"
C_PATH = "Core/DSPAudio/oscPwmTables.c"


def recip():
    return [0xFFFFFFFF] + [0xFFFFFFFF // n for n in range(1, 128)]


def level():
    return [int(round(16384.0 * 64.0 / (64.0 + m))) for m in range(64)]


def lift():
    out = [int(round(math.log2(64.0 / min(p, 128 - p)))) for p in range(1, 128)]
    return [out[0]] + out


def checks(r, lv, lf):
    ok = all(r[n] * n <= 0xFFFFFFFF < (r[n] + 1) * n for n in range(1, 128))
    ok = ok and lv[0] == 16384 and lv[63] == 8257
    ok = ok and all(b < a for a, b in zip(lv, lv[1:]))
    ok = ok and lf[64] == 0 and lf[1] == 6 and lf[127] == 6
    ok = ok and all(lf[p] == lf[128 - p] for p in range(1, 128))
    return ok


def render_h():
    return "\n".join([
        "/* Generated by tools/plots/gen_pwm_tables.py (S094 P1 S3). Do not edit. */",
        "#ifndef OSC_PWM_TABLES_H_",
        "#define OSC_PWM_TABLES_H_",
        "",
        "#include <stdint.h>",
        "",
        "/*",
        " * PWM oscillator constants (S094 P1 S3), flash.",
        " *",
        " * What:       oscPwmRecip[n] = floor((2^32 - 1) / n): the warp's half-cycle",
        " *             scales; oscPwmLevelQ14[m] = 16384 x 64 / (64 + m): the two-read",
        " *             level, m = max(p, 128 - p) - 64; oscPwmLift[p]: octave rows the",
        " *             warped triangle moves up.",
        " * Accessors:  Oscillator.c osc_pwmPlan(); AlkalineVoice.c alk_renderPwm().",
        " */",
        "extern const uint32_t oscPwmRecip[128];",
        "extern const int16_t  oscPwmLevelQ14[64];",
        "extern const uint8_t  oscPwmLift[128];",
        "",
        "#endif /* OSC_PWM_TABLES_H_ */",
        ""])


def rows(vals, fmt, per):
    return ["    " + ", ".join(fmt % v for v in vals[i:i + per]) + ","
            for i in range(0, len(vals), per)]


def render_c(r, lv, lf):
    lines = ["/* Generated by tools/plots/gen_pwm_tables.py (S094 P1 S3). Do not edit. */",
             '#include "oscPwmTables.h"', "",
             "/* floor((2^32 - 1) / n), n = 0..127 ([0] unused) */",
             "const uint32_t oscPwmRecip[128] = {"]
    lines += rows(r, "0x%08Xu", 4) + ["};", "",
              "/* round(16384 x 64 / (64 + m)), m = 0..63 */",
              "const int16_t oscPwmLevelQ14[64] = {"]
    lines += rows(lv, "%5d", 8) + ["};", "",
              "/* round(log2(64 / min(p, 128 - p))), p = 0..127 ([0] = [1]) */",
              "const uint8_t oscPwmLift[128] = {"]
    lines += rows(lf, "%du", 16) + ["};", ""]
    return "\n".join(lines)


def main():
    r, lv, lf = recip(), level(), lift()
    ok = checks(r, lv, lf)
    h, c = render_h(), render_c(r, lv, lf)
    if "--check" in sys.argv:
        ok = ok and open(H_PATH).read() == h and open(C_PATH).read() == c
        print("pwm tables", "OK" if ok else "FAILED")
        return 0 if ok else 1
    if not ok:
        print("pwm tables: T1-T3 failed")
        return 1
    open(H_PATH, "w").write(h)
    open(C_PATH, "w").write(c)
    print("wrote", H_PATH, C_PATH)
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

Run once: `python3 tools/plots/gen_pwm_tables.py`. This writes the two NEW
FILES `Core/DSPAudio/oscPwmTables.h` and `oscPwmTables.c` (generated; 768 B of
flash).

`Makefile` **ADD** after l. 170 (`  Core/DSPAudio/Oscillator.c \`):

```make
  Core/DSPAudio/oscPwmTables.c \
```

### S3.2 `Core/DSPAudio/Oscillator.h`

**MODIFY** l. 72–100 (the type list).

```c
/*
 * Oscillator type: the typ row (S087 stage B; S094 P1 S3 pwm).
 *
 * What:       OSC_TYPE_WAV 0 (stock waves), OSC_TYPE_SMP 1 (one-shot sample),
 *             OSC_TYPE_SLP 2 (looped sample), OSC_TYPE_WTB 3 (wavetable),
 *             OSC_TYPE_PWM 4 (S094 P1: pulse-width forms of Sin/Tri/Saw/Rec,
 *             pos = width), OSC_TYPE_COUNT 5. OSC_TYPE_MODLIST 5 is not a typ
 *             value: it marks a modulator (Drum osc2, Cymbal/HiHat osc2/osc3),
 *             whose wav list is the stock waves followed by the user entries
 *             s01.., l01.., w01.. (S087 P0 §12.4). It moved from 4 to 5 in
 *             S094 P1; it is set at engine init and never stored.
 *             OSC_TYPE_SELECTABLE is the number of typ values the menu and
 *             the writer accept.
 * Why:        S087 P0 §4, A21/A22: osc1 selects its type with typ; the
 *             modulators have no typ row and select user content through
 *             their wav list. One byte in OscInfo carries both meanings so
 *             osc_setFreq() and the renderers can tell them apart. pwm is
 *             appended, so no stored typ value changes meaning.
 * Inputs:     the osc1_type descriptor (osc1) and the engine init functions
 *             (modulators).
 * Outputs:    OscInfo.oscType.
 * Accessors:  osc_setType(), osc_isTyped(), osc_setFreq(), the engines'
 *             typed branches, menu.c (count), InstrumentManager.c (writer).
 * Affiliates: MENU_OSC_TYPE and oscTypeNames (MenuText.h); the osc1_type rows
 *             in the four *Parameters.c files; Alkaline_setOscType().
 */
#define OSC_TYPE_WAV         0u
#define OSC_TYPE_SMP         1u
#define OSC_TYPE_SLP         2u
#define OSC_TYPE_WTB         3u
#define OSC_TYPE_PWM         4u
#define OSC_TYPE_COUNT       5u
#define OSC_TYPE_MODLIST     5u
#define OSC_TYPE_SELECTABLE  5u   /* S094 P1: wav smp slp wtb pwm */
_Static_assert(OSC_TYPE_SELECTABLE >= 1u && OSC_TYPE_SELECTABLE <= OSC_TYPE_COUNT,
               "OSC_TYPE_SELECTABLE out of range");
_Static_assert(OSC_TYPE_MODLIST >= OSC_TYPE_COUNT,
               "OSC_TYPE_MODLIST must not be a typ value (osc_isTyped())");

/*
 * PWM constants (S094 P1 S3).
 *
 * What:       OSC_PWM_WAVE_COUNT 4: under pwm the wav list is Sin, Tri, Saw,
 *             Rec (0..3; a stored value past 3 clamps to Rec, the S087 r8
 *             rule). OSC_PWM_SLEW_STEPS 2: the most the applied width moves
 *             toward pos per block (P-D7: 64 steps in about 23 ms).
 * Why:        a width change is a waveform step at the block rate; the slew
 *             keeps an LFO or knob sweep from stepping audibly, and the
 *             trigger snaps it (osc_typedTriggerPhase()).
 * Accessors:  osc_userCount(), osc_typedPositionApply(), menu.c
 *             menu_formatWaveCell(), AlkalineVoice.c (the Alkaline slew).
 */
#define OSC_PWM_WAVE_COUNT   4u
#define OSC_PWM_SLEW_STEPS   2
```

**MODIFY** l. 185–205 (the bind kinds: 3 becomes PWM).

```c
 * What:       OSC_SAMPLE_PHASE_SHIFT 12 (Q19.12 phase: point index << 12 +
 *             fraction); OSC_BIND_NONE/SMP/SLP/PWM/WTB16 (S093 P8: WTB8 is
 *             gone with 8-bit tables; S094 P1: 3 = PWM, a bind with no
 *             catalogue item: the oscillator plays the stock tables in a
 *             pulse-width form); OSC_POS_SAMPLE_SPEED_CAP
...
#define OSC_SAMPLE_PHASE_SHIFT    12u
#define OSC_BIND_NONE             0u
#define OSC_BIND_SMP              1u
#define OSC_BIND_SLP              2u
#define OSC_BIND_PWM              3u
#define OSC_BIND_WTB16            4u
```

(Lines 199–200 and 206–207 of the comment and the two later defines are
unchanged.)

**MODIFY** l. 810–827 (`osc_isTyped()`).

```c
/*
 * Whether osc1 plays a typed oscillator (S087 stage B; S094 P1 pwm).
 *
 * What:       1 for OSC_TYPE_SMP, SLP, WTB and PWM; 0 for wav and for
 *             modulators (OSC_TYPE_MODLIST, 5).
 * Why:        the engines branch on it once per block to call the typed
 *             renderers instead of the existing dispatchers (S087 P0 A2), and
 *             the pos helpers use it to choose the rule (D3). PWM rides the
 *             typed path, so the four engines' render code is not edited:
 *             only this inlined constant changes (a compare immediate of the
 *             same encoding).
 * Inputs:     osc.  Outputs: 0 or 1.
 * Accessors:  the four engine renders and triggers; osc_positionBendApply()/
 *             Restore(); osc_setFreq().
 * Affiliates: OSC_TYPE_*; osc_setType().
 */
static inline uint8_t osc_isTyped(const OscInfo* osc)
{
	return (uint8_t)(osc->oscType >= OSC_TYPE_SMP &&
	                 osc->oscType <= OSC_TYPE_PWM);
}
```

**MODIFY** l. 836–838 (`osc_setType()` contract: the wav clamp per type).

```c
 *             - clamps osc->waveform to an entry that exists for the new type
 *               (wav: OSC_STOCK_LAST; smp/slp: sample count - 1; wtb: table
 *               count - 1; pwm: OSC_PWM_WAVE_COUNT - 1, Rec; none installed:
 *               0) - the value is otherwise used
```

### S3.3 `Core/DSPAudio/Oscillator.c` — control functions

**ADD** after l. 54 (`#include "modulationNode.h"`). Anchor:
`#include "modulationNode.h"`.

```c
#include "oscPwmTables.h"   /* S094 P1 S3: the PWM plan's constants */
```

**MODIFY** l. 1495–1505 (`osc_userCount()`'s switch): add the PWM case.

```c
	switch (type) {
	case OSC_TYPE_WAV:
		return OSC_SAMPLE_START;
	case OSC_TYPE_SMP:
	case OSC_TYPE_SLP:
		return sampleMemory_getNumSamples();
	case OSC_TYPE_WTB:
		return sampleMemory_getNumWavetables();
	case OSC_TYPE_PWM:
		/* S094 P1 S3: Sin, Tri, Saw, Rec (the stock waves that have a
		 * pulse-width form; Noi has none) */
		return OSC_PWM_WAVE_COUNT;
	default:
		return 0u;
	}
```

**ADD** in `osc_userBind()` after l. 1560 (`	uint8_t count;`). Anchor:
`	if (osc->oscType == OSC_TYPE_SMP || osc->oscType == OSC_TYPE_SLP) {`.

```c
	/*
	 * pwm bind (S094 P1 S3).
	 *
	 * What:       marks the bind OSC_BIND_PWM (the typed dispatchers' case)
	 *             with no catalogue item, then sets phaseInc and tableOffset
	 *             by the stock table law (osc_calcWavetableFreqValue():
	 *             2^32 per cycle, the octave row for the band limit) every
	 *             block. Sine and the tables share the 2^32-per-cycle phase,
	 *             so one law serves all four pwm waves.
	 * Why:        pwm plays the built-in tables, so the catalogue bind below
	 *             (and its positionApplied re-anchor on a change) does not
	 *             apply; a re-anchor every block would defeat the width slew.
	 * Inputs:     osc (oscType pwm), currentFreq.
	 * Outputs:    sampleCacheLooped/Index/Size/Data/Generation (once, on the
	 *             first pwm block), phaseInc, tableOffset.
	 * Affiliates: osc_pwmPlan() (reads tableOffset and waveform);
	 *             osc_setType() (the type switch re-anchors positionApplied).
	 */
	if (osc->oscType == OSC_TYPE_PWM) {
		if (osc->sampleCacheLooped != OSC_BIND_PWM) {
			osc->sampleCacheLooped = OSC_BIND_PWM;
			osc->sampleCacheIndex = 0u;
			osc->sampleCacheSize = 0u;
			osc->sampleCacheData = 0;
			osc->sampleCacheGeneration = 0u;
		}
		osc_calcWavetableFreqValue(osc, currentFreq);
		return;
	}
```

**MODIFY** l. 1710–1711 (`osc_typedTriggerPhase()`).

```c
	if (osc->oscType == OSC_TYPE_WTB)
		return 0u;   /* stage E: pos is a frame position, not a phase */
	/*
	 * pwm start (S094 P1 S3).
	 *
	 * What:       phase 0; the width has already snapped to pos above
	 *             (positionApplied = position, the first line).
	 * Why:        under pwm pos is the width, not a start offset; 0 starts
	 *             every hit on the same edge (the wtb rule). Drum's transient
	 *             offset applies only to wav (DrumVoice.c trigger chain).
	 */
	if (osc->oscType == OSC_TYPE_PWM)
		return 0u;
```

**ADD** in `osc_typedPositionApply()` after l. 1757 (`	int32_t maxStep;`).
Anchor: `	if (osc->oscType == OSC_TYPE_WTB) {` (the first occurrence in this
function).

```c
	/*
	 * pwm width slew (S094 P1 S3, P-D7).
	 *
	 * What:       moves positionApplied (the applied width) toward pos by at
	 *             most OSC_PWM_SLEW_STEPS per block, linearly (width is not
	 *             circular: 127 -> 0 is a full sweep, not one step); clears
	 *             positionBendStep; never touches phaseInc.
	 * Why:        the width arrives once per block (knob, LFO, Morph,
	 *             automation, velocity); a jump of many steps is a waveform
	 *             step, a slew of two is not audible (to confirm by ear,
	 *             plan §10). bend (0 for Drum snc) is irrelevant: pwm never
	 *             bends the phase.
	 * Inputs:     osc->position, osc->positionApplied.
	 * Outputs:    osc->positionApplied, osc->positionBendStep (0).
	 * Affiliates: osc_pwmPlan() (reads positionApplied, clamped to 1..127);
	 *             osc_typedTriggerPhase() (the snap at a hit).
	 */
	if (osc->oscType == OSC_TYPE_PWM) {
		/* the remaining distance to pos, then limited to the slew */
		d = (int32_t)(osc->position & 127u) - (int32_t)osc->positionApplied;
		if (d > OSC_PWM_SLEW_STEPS)
			d = OSC_PWM_SLEW_STEPS;
		else if (d < -OSC_PWM_SLEW_STEPS)
			d = -OSC_PWM_SLEW_STEPS;
		osc->positionApplied = (uint8_t)((int32_t)osc->positionApplied + d);
		osc->positionBendStep = 0;
		(void)bend;
		return;
	}
```

**MODIFY** l. 1834 (`osc_typedPositionRestore()`).

```c
	/* wtb and pwm (S094 P1) never bend phaseInc: nothing to restore */
	if (osc->oscType == OSC_TYPE_WTB || osc->oscType == OSC_TYPE_PWM) {
```

### S3.4 `Core/DSPAudio/Oscillator.c` — the dispatchers

**ADD** after l. 2386 (the last S087 E forward declaration,
`osc_wtbSyncBlendBlock(...)`). Anchor: `/* S087 D/E: typed render entries (see the header). */`.

```c
/*
 * S094 P1 S3: the pwm role renderers (defined in the S094 P1 block at the end
 * of this file). One per role; each builds the block's plan (osc_pwmPlan())
 * and calls one of its three .itcm.s094p1 loops. They run from flash once per
 * block (noinline, noclone), so the ITCM dispatchers below gain one call each.
 */
static void osc_pwmRenderPlain(OscInfo* osc, int16_t* buf, uint8_t size,
                               float gain);
static void osc_pwmRenderFm(OscInfo* osc, const int16_t* mod, int16_t* buf,
                            uint8_t size, float gain);
static void osc_pwmRenderFmRamp(OscInfo* osc, const int16_t* mod, int16_t* buf,
                                uint8_t size, float gain, float d0, float dStep);
static void osc_pwmRenderRing(OscInfo* osc, const int16_t* modBuf, int16_t* buf,
                              uint8_t size, int32_t depthQ15, int32_t depthStepQ15);
static void osc_pwmRenderSync(OscInfo* osc, const uint32_t* over,
                              uint32_t ratioQ16, int32_t depthQ30,
                              int16_t* buf, uint8_t size);
```

**ADD** one case in each typed dispatcher, before its `default:`. Every one
carries the same comment:

```c
	/* S094 P1 S3: pwm (no catalogue item; never claims a slot) */
```

| Dispatcher | Insert before | Case |
|---|---|---|
| `osc_renderTyped()` | l. 2402 `default:` | `case OSC_BIND_PWM: osc_pwmRenderPlain(osc, buf, size, gain); break;` |
| `osc_renderTypedFm()` | l. 2424 | `case OSC_BIND_PWM: osc_pwmRenderFm(osc, mod, buf, size, gain); break;` |
| `osc_renderTypedSync()` | l. 2446 | `case OSC_BIND_PWM: osc_pwmRenderSync(osc, over, ratioQ16, depthQ30, buf, size); break;` |
| `osc_renderTypedRing()` | l. 2467 | `case OSC_BIND_PWM: osc_pwmRenderRing(osc, modBuf, buf, size, depthQ15, depthStepQ15); break;` |
| `oscSection_renderOldCarrier()` | l. 2581 | `case OSC_BIND_PWM: osc_pwmRenderPlain(&view, osc_itpScratch, size, 1.0f); break;` |
| `osc_renderTypedFmRamp()` | l. 3684 | `case OSC_BIND_PWM: osc_pwmRenderFmRamp(osc, mod, buf, size, gain, d0, dStep); break;` |

**The old carrier.** The `itp` snapshot already holds `waveform`,
`tableOffset`, `positionApplied`, `oscType` and `sampleCacheLooped`
(`OscCarrierSnapshot`). So the old carrier plays at its frozen width, and the
view's `positionApplied` is that width. Add to the comment above
`oscSection_renderOldCarrier()` (l. 2513–2516):

```c
 *             PLAIN into osc_itpScratch (typed smp/slp/wtb plain copies, the
 *             pwm plain role at the frozen width (S094 P1), or
 *             calcNextOscSampleBlock() for the stock/one-shot waveforms), then
```

### S3.5 `Core/DSPAudio/Oscillator.c` — the S094 P1 PWM block

**ADD** at the end of the file, after l. 3688 (the closing brace of
`osc_renderTypedFmRamp()`).

```c
/* =========================================================================
** S094 P1 S3: PWM (typ pwm) for Drum, Snare, Cymbal and HiHat osc1
** =========================================================================
**
** What:       the per-block plan, three always-inline reads, fifteen loops
**             (forms D / WT / WS x roles plain, FM, FM ramp, ring, sync) and
**             the five role renderers the typed dispatchers call for
**             OSC_BIND_PWM.
**               D   two reads of the descending saw row, w = p x 8 points
**                   apart, summed (Saw, sign +1) or differenced (Rec, sign
**                   -1): a band-limited two-saw or pulse, because each read
**                   is a band-limited saw;
**               WT  the triangle row, phase-warped: the rising half-cycle
**                   over [0, W), the falling one over [W, 2^32), W = p << 25,
**                   with the octave lifted by oscPwmLift[p] for the
**                   compressed half;
**               WS  the same warp on sine_table (a -cos: trough first, so
**                   its halves are the rising and falling ones and the
**                   warped wave has no DC; plan finding 1).
** Why:        S094 P1 plan §5 / PWM draft §1: a width on every stock wave
**             with a meaning of its own, inside every role's budget. Integer
**             Q15 interpolation and a Q14 gain replace the stock loops' float
**             interpolation and the two conversions, which pays for the
**             second read (D) or the warp (W) (PWM draft §0: 23 against the
**             stock read's 24 linked).
** Placement:  loops in .itcm.s094p1, pinned after .itcm.s089p3 in
**             STM32F765VIHx_FLASH.ld (no existing ITCM function moves);
**             plan and role renderers in flash (noinline, noclone: one call
**             per block each, kept out of the ITCM dispatchers and out of
**             mixer_calcNextSampleBlock()).
** Cost:       per sample, the PWM draft's standalone counts (plain D 23,
**             WT 23, WS 22; FM 30-31; FM ramp 31; ring 31; sync 35) against
**             the role ceilings after S2 (plain 27, FM 50, FM ramp 51, ring
**             42, sync 41); per block, the plan (two table reads, one gain
**             multiply) and one role call. Re-measured linked in S3.10.
** Accessors:  osc_renderTyped(), osc_renderTypedFm(), osc_renderTypedSync(),
**             osc_renderTypedRing(), osc_renderTypedFmRamp(),
**             oscSection_renderOldCarrier().
** Affiliates: oscPwmTables.h (generated); osc_userBind() (pwm bind);
**             osc_typedPositionApply() (width slew); osc_syncPull(),
**             osc_ringSample() (the S085 laws, reused unchanged);
**             tools/dsp_test/modules/oscillator.c group pwm.
*/
#define OSC_S094P1_SECTION __attribute__((section(".itcm.s094p1")))
#define OSC_S094P1_LOOP    OSC_S094P1_SECTION __attribute__((noinline, noclone))

/* The three forms of a pwm block (osc_pwmPlan_t.form). */
#define OSC_PWM_FORM_D   0u   /* two-read: Saw (sum), Rec (difference) */
#define OSC_PWM_FORM_WT  1u   /* warp on a 1,024-point table row: Tri */
#define OSC_PWM_FORM_WS  2u   /* warp on the 4,096-point sine: Sin */

/*
 * One pwm block's constants (S094 P1 S3).
 *
 * What:       row (D, WT: the 1,024-point row; WS: unused), wPh (the warp's
 *             split point W = p << 25), ra / rb (the half-cycle scales
 *             (2^32 - 1)/p and (2^32 - 1)/(128 - p)), d (D: the second read's
 *             offset, p x 8 points of 1,024), sign (D: +1 sum, -1
 *             difference), gQ14 (the store gain in Q14: level x render
 *             gain), form.
 * Why:        filled once per block by osc_pwmPlan() and read by a loop
 *             before its first sample, so nothing is computed per sample and
 *             nothing stays live across the render in the engine.
 * Lifetime:   a local of the role renderer (the stack), one block.
 */
typedef struct {
	const int16_t *row;
	uint32_t wPh;
	uint32_t ra;
	uint32_t rb;
	uint32_t d;
	int32_t  sign;
	int32_t  gQ14;
	uint8_t  form;
} osc_pwmPlan_t;

/*
 * Build one pwm block's plan (S094 P1 S3).
 *
 * What:       p = positionApplied clamped to 1..127 (P-D3: pos 0 acts as 1);
 *             m = max(p, 128 - p) - 64 (0..63, the level index); the warp
 *             constants for every form (unused ones cost two loads); then by
 *             wav: Sin -> WS at gain; Tri -> WT on triTable[min(tableOffset +
 *             lift[p], 10)] at gain; Saw -> D sum, Rec (and any larger value)
 *             -> D difference, both on sawTable[tableOffset] at gain x
 *             level[m] / 16384.
 * Why:        one place holds every per-block decision; the loops are pure.
 *             level keeps the two-read form at the table's peak at every
 *             width (64 / max(p, 128 - p)); the warp keeps the table's own
 *             amplitude, so it needs no level.
 * Inputs:     osc (waveform, tableOffset from osc_userBind(), positionApplied
 *             from osc_typedPositionApply()), gain (0..1 at every call site).
 * Outputs:    *pl.
 * Cost:       per block, flash; no division (the tables).
 * Accessors:  the five osc_pwmRender*() roles.
 */
static __attribute__((noinline, noclone)) void osc_pwmPlan(const OscInfo* osc,
                                                           float gain,
                                                           osc_pwmPlan_t* pl)
{
	/* the applied width, 1..127 */
	uint32_t p = (uint32_t)(osc->positionApplied & 127u);

	if (p < 1u)
		p = 1u;
	/* the level index: distance of the wider part from half, 0..63 */
	{
		const uint32_t m = ((p > 64u) ? p : (128u - p)) - 64u;

		/* W: the split point and the two half-cycle scales */
		pl->wPh = p << 25;
		pl->ra = oscPwmRecip[p];
		pl->rb = oscPwmRecip[128u - p];
		/* D: the second read's offset in points: p / 128 of 1,024 */
		pl->d = p << 3;
		switch (osc->waveform) {
		case SINE:
			pl->form = OSC_PWM_FORM_WS;
			pl->row = 0;
			pl->sign = 0;
			pl->gQ14 = (int32_t)(gain * 16384.0f + 0.5f);
			break;
		case TRI: {
			/* the octave row, lifted for the compressed half-cycle */
			uint32_t r = (uint32_t)osc->tableOffset + oscPwmLift[p];

			if (r > 10u)
				r = 10u;
			pl->form = OSC_PWM_FORM_WT;
			pl->row = triTable[r];
			pl->sign = 0;
			pl->gQ14 = (int32_t)(gain * 16384.0f + 0.5f);
			break; }
		case SAW:
			pl->form = OSC_PWM_FORM_D;
			pl->row = sawTable[osc->tableOffset];
			pl->sign = 1;
			pl->gQ14 = (int32_t)(gain * (float)oscPwmLevelQ14[m] + 0.5f);
			break;
		default:   /* REC; a stored value past Rec plays Rec (S087 r8) */
			pl->form = OSC_PWM_FORM_D;
			pl->row = sawTable[osc->tableOffset];
			pl->sign = -1;
			pl->gQ14 = (int32_t)(gain * (float)oscPwmLevelQ14[m] + 0.5f);
			break;
		}
	}
}

/*
 * The warp (S094 P1 S3, form W).
 *
 * What:       maps the oscillator phase ph to the read phase psi:
 *               ph <  W: psi = umulh(ph, ra) << 6            (0 .. 2^31)
 *               ph >= W: psi = (umulh(ph - W, rb) << 6) + 2^31 (2^31 .. 2^32)
 *             umulh(x, (2^32 - 1)/n) is x / n rounded down, so the first
 *             half maps [0, W) onto [0, 2^31) (x 64/p) and the second maps
 *             [W, 2^32) onto [2^31, 2^32) (x 64/(128 - p)). At p = 64 psi is
 *             ph to within 64 phase units (1/2^26 of a cycle).
 * Why:        both halves are computed and one is selected (a conditional
 *             select): the same instructions run at every width (constant
 *             cost); the << 6 is exact because the loops read only psi >> 7
 *             and above.
 * Inputs:     ph, wPh, ra, rb.  Outputs: psi.
 * Accessors:  the WT and WS loops.
 */
static inline __attribute__((always_inline)) uint32_t osc_pwmWarp(
	const uint32_t ph, const uint32_t wPh, const uint32_t ra, const uint32_t rb)
{
	/* first half-cycle: x 64/p */
	const uint32_t lo = (uint32_t)(((uint64_t)ph * ra) >> 32) << 6;
	/* second half-cycle: x 64/(128 - p), shifted up by half a cycle */
	const uint32_t hi = ((uint32_t)(((uint64_t)(ph - wPh) * rb) >> 32) << 6)
	                    + 0x80000000u;

	return (ph < wPh) ? lo : hi;
}

/*
 * Read a 1,024-point row at psi, integer interpolation (S094 P1 S3).
 *
 * What:       a = row[psi >> 22], b = the next point (1,023 wraps to 0),
 *             f = the next 15 bits ((psi >> 7) & 0x7FFF); returns
 *             a + ((b - a) x f >> 15).
 * Range:      |b - a| <= 32,767 for every stock row (measured: saw 32,767,
 *             rec 32,753, tri 207), so (b - a) x f < 2^30: no overflow.
 * Accessors:  the WT loops.
 */
static inline __attribute__((always_inline)) int32_t osc_pwmReadT(
	const int16_t* row, const uint32_t ps)
{
	const uint32_t ia = ps >> 22;
	const uint32_t na = (ia + 1u) & 0x3FFu;
	const int32_t a = row[ia];

	return a + (((row[na] - a) * (int32_t)((ps >> 7) & 0x7FFFu)) >> 15);
}

/*
 * Read the 4,096-point sine at psi, integer interpolation (S094 P1 S3).
 *
 * What:       a = sine_table[psi >> 20], b = the next point (the table's
 *             guard point 4,096 makes the last one safe, as calcSineBlock()),
 *             f = (psi >> 5) & 0x7FFF; returns a + ((b - a) x f >> 15).
 * Range:      |b - a| <= 51 (measured).
 * Accessors:  the WS loops.
 */
static inline __attribute__((always_inline)) int32_t osc_pwmReadS(
	const uint32_t ps)
{
	const uint32_t ia = ps >> 20;
	const int32_t a = sine_table[ia];

	return a + (((sine_table[ia + 1u] - a) * (int32_t)((ps >> 5) & 0x7FFFu)) >> 15);
}

/*
 * The two-read value at ph (S094 P1 S3, form D).
 *
 * What:       A at ph, B at ph - w (d points earlier, the row wraps):
 *               d0 = row[ia] + sign x row[ib], d1 = the same one point on,
 *               out = d0 + ((d1 - d0) x f >> 15), f = (ph >> 7) & 0x7FFF.
 *             Both reads share one fraction (w is a whole number of points),
 *             so the combined value is interpolated once.
 * Why:        reading B at ph - w (not + w) makes the difference high for the
 *             first w of the cycle, as the stock Rec is (plan finding 2);
 *             p = 64 gives a band-limited square of the Rec orientation.
 * Range:      |d1 - d0| <= 2 x 32,767 = 65,534 and 65,534 x 32,767 < 2^31.
 * Accessors:  the D loops.
 */
static inline __attribute__((always_inline)) int32_t osc_pwmReadD(
	const int16_t* row, const uint32_t ph, const uint32_t d, const int32_t sign)
{
	const uint32_t ia = ph >> 22;
	const uint32_t na = (ia + 1u) & 0x3FFu;
	const uint32_t ib = (ia - d) & 0x3FFu;
	const uint32_t nb = (ib + 1u) & 0x3FFu;
	const int32_t d0 = row[ia] + sign * row[ib];
	const int32_t d1 = row[na] + sign * row[nb];

	return d0 + (((d1 - d0) * (int32_t)((ph >> 7) & 0x7FFFu)) >> 15);
}

/*
 * The store (S094 P1 S3): (o x gQ14) >> 14, saturated to int16.
 * |o| <= 65,534 and gQ14 <= 16,384, so the product fits int32; SSAT16
 * catches the band-limited overshoot (Gibbs) of the two-read form.
 */
#define OSC_PWM_STORE(o, g) ((int16_t)__SSAT(((o) * (g)) >> 14, 16))

/* ---------------------------------------------------------------- plain */

/*
 * pwm plain, form D (S094 P1 S3).
 *
 * What:       one block of the two-read value at the plan's row, offset and
 *             sign, stored at gQ14; the phase advances by phaseInc.
 * Inputs:     osc (phase, phaseInc), pl (row, d, sign, gQ14), buf, size.
 * Outputs:    buf; osc->phase.
 * Cost:       23 per sample (PWM draft m2c / d_plainSigned, standalone).
 * Accessors:  osc_pwmRenderPlain().
 */
static OSC_S094P1_LOOP void osc_pwmDPlainBlock(OscInfo* osc,
                                               const osc_pwmPlan_t* pl,
                                               int16_t* buf, uint8_t size)
{
	const int16_t *row = pl->row;
	const uint32_t d = pl->d;
	const int32_t sign = pl->sign;
	const int32_t g = pl->gQ14;
	const uint32_t inc = osc->phaseInc;
	uint32_t ph = osc->phase;
	uint8_t i;

	for (i = 0u; i < size; i++) {
		const int32_t o = osc_pwmReadD(row, ph, d, sign);

		ph += inc;
		buf[i] = OSC_PWM_STORE(o, g);
	}
	osc->phase = ph;
}

/*
 * pwm plain, form WT (S094 P1 S3): the warped triangle row.
 * As osc_pwmDPlainBlock(), with o = osc_pwmReadT(row, warp(ph)).
 * Cost: 23 per sample (PWM draft m1b). Accessors: osc_pwmRenderPlain().
 */
static OSC_S094P1_LOOP void osc_pwmWtPlainBlock(OscInfo* osc,
                                                const osc_pwmPlan_t* pl,
                                                int16_t* buf, uint8_t size)
{
	const int16_t *row = pl->row;
	const uint32_t wPh = pl->wPh, ra = pl->ra, rb = pl->rb;
	const int32_t g = pl->gQ14;
	const uint32_t inc = osc->phaseInc;
	uint32_t ph = osc->phase;
	uint8_t i;

	for (i = 0u; i < size; i++) {
		const int32_t o = osc_pwmReadT(row, osc_pwmWarp(ph, wPh, ra, rb));

		ph += inc;
		buf[i] = OSC_PWM_STORE(o, g);
	}
	osc->phase = ph;
}

/*
 * pwm plain, form WS (S094 P1 S3): the warped sine.
 * As osc_pwmWtPlainBlock(), reading sine_table (osc_pwmReadS()).
 * Cost: 22 per sample (PWM draft m1sin). Accessors: osc_pwmRenderPlain().
 */
static OSC_S094P1_LOOP void osc_pwmWsPlainBlock(OscInfo* osc,
                                                const osc_pwmPlan_t* pl,
                                                int16_t* buf, uint8_t size)
{
	const uint32_t wPh = pl->wPh, ra = pl->ra, rb = pl->rb;
	const int32_t g = pl->gQ14;
	const uint32_t inc = osc->phaseInc;
	uint32_t ph = osc->phase;
	uint8_t i;

	for (i = 0u; i < size; i++) {
		const int32_t o = osc_pwmReadS(osc_pwmWarp(ph, wPh, ra, rb));

		ph += inc;
		buf[i] = OSC_PWM_STORE(o, g);
	}
	osc->phase = ph;
}

/* ------------------------------------------------------------------- FM */

/*
 * pwm FM carriers (S094 P1 S3; Cymbal/HiHat through osc_renderTypedFm()).
 *
 * What:       the plain loops with the read phase rp = ph + (ssat16(mod[i] x
 *             fmMod) << 19), the table-FM fraction of a cycle (S087 A17: amt
 *             means the same on every type). The warp (W) is applied to rp;
 *             D reads both points at rp and rp - w. fmMod is loaded once
 *             before the loop.
 * Inputs:     osc (phase, phaseInc, fmMod), pl, mod[size] (the modulator
 *             block), buf, size.
 * Outputs:    buf; osc->phase.
 * Cost:       30-31 per sample (PWM draft m2fm / m1fm) against the FM role's
 *             50 (osc_slpFmBlock).
 * Accessors:  osc_pwmRenderFm().
 */
static OSC_S094P1_LOOP void osc_pwmDFmBlock(OscInfo* osc, const osc_pwmPlan_t* pl,
                                            const int16_t* mod, int16_t* buf,
                                            uint8_t size)
{
	const int16_t *row = pl->row;
	const uint32_t d = pl->d;
	const int32_t sign = pl->sign, g = pl->gQ14;
	const float fm = osc->fmMod;
	const uint32_t inc = osc->phaseInc;
	uint32_t ph = osc->phase;
	uint8_t i;

	for (i = 0u; i < size; i++) {
		/* the FM read phase: the S087 table-FM offset */
		const uint32_t rp = ph + ((uint32_t)__SSAT(
			(int32_t)((float)mod[i] * fm), 16) << 19);
		const int32_t o = osc_pwmReadD(row, rp, d, sign);

		ph += inc;
		buf[i] = OSC_PWM_STORE(o, g);
	}
	osc->phase = ph;
}

static OSC_S094P1_LOOP void osc_pwmWtFmBlock(OscInfo* osc, const osc_pwmPlan_t* pl,
                                             const int16_t* mod, int16_t* buf,
                                             uint8_t size)
{
	const int16_t *row = pl->row;
	const uint32_t wPh = pl->wPh, ra = pl->ra, rb = pl->rb;
	const int32_t g = pl->gQ14;
	const float fm = osc->fmMod;
	const uint32_t inc = osc->phaseInc;
	uint32_t ph = osc->phase;
	uint8_t i;

	for (i = 0u; i < size; i++) {
		const uint32_t rp = ph + ((uint32_t)__SSAT(
			(int32_t)((float)mod[i] * fm), 16) << 19);
		const int32_t o = osc_pwmReadT(row, osc_pwmWarp(rp, wPh, ra, rb));

		ph += inc;
		buf[i] = OSC_PWM_STORE(o, g);
	}
	osc->phase = ph;
}

static OSC_S094P1_LOOP void osc_pwmWsFmBlock(OscInfo* osc, const osc_pwmPlan_t* pl,
                                             const int16_t* mod, int16_t* buf,
                                             uint8_t size)
{
	const uint32_t wPh = pl->wPh, ra = pl->ra, rb = pl->rb;
	const int32_t g = pl->gQ14;
	const float fm = osc->fmMod;
	const uint32_t inc = osc->phaseInc;
	uint32_t ph = osc->phase;
	uint8_t i;

	for (i = 0u; i < size; i++) {
		const uint32_t rp = ph + ((uint32_t)__SSAT(
			(int32_t)((float)mod[i] * fm), 16) << 19);
		const int32_t o = osc_pwmReadS(osc_pwmWarp(rp, wPh, ra, rb));

		ph += inc;
		buf[i] = OSC_PWM_STORE(o, g);
	}
	osc->phase = ph;
}

/* -------------------------------------------------------------- FM ramp */

/*
 * pwm FM carriers with a per-sample depth ramp (S094 P1 S3; Drum fm through
 * osc_renderTypedFmRamp(), the S089 P3 twin rule).
 *
 * What:       the FM loops with the depth d = d0 on sample 0 and d += dStep
 *             after each sample, in place of the loop-invariant fmMod. With
 *             d0 = fmMod and dStep = 0 the output equals the FM loop's (law
 *             PW6).
 * Inputs:     osc, pl, mod[size], buf, size, d0, dStep.
 * Outputs:    buf; osc->phase.
 * Cost:       31 per sample (PWM draft d_fmRamp) against the FM-ramp role's
 *             51 (osc_slpFmRampBlock).
 * Accessors:  osc_pwmRenderFmRamp().
 */
static OSC_S094P1_LOOP void osc_pwmDFmRampBlock(OscInfo* osc,
                                                const osc_pwmPlan_t* pl,
                                                const int16_t* mod, int16_t* buf,
                                                uint8_t size, float d0, float dStep)
{
	const int16_t *row = pl->row;
	const uint32_t dp = pl->d;
	const int32_t sign = pl->sign, g = pl->gQ14;
	const uint32_t inc = osc->phaseInc;
	uint32_t ph = osc->phase;
	/* the ramped FM depth: d0 here, d0 + i x dStep at sample i */
	float dd = d0;
	uint8_t i;

	for (i = 0u; i < size; i++) {
		const uint32_t rp = ph + ((uint32_t)__SSAT(
			(int32_t)((float)mod[i] * dd), 16) << 19);
		const int32_t o = osc_pwmReadD(row, rp, dp, sign);

		/* the depth steps after its use (sample 0 = d0) */
		dd += dStep;
		ph += inc;
		buf[i] = OSC_PWM_STORE(o, g);
	}
	osc->phase = ph;
}

static OSC_S094P1_LOOP void osc_pwmWtFmRampBlock(OscInfo* osc,
                                                 const osc_pwmPlan_t* pl,
                                                 const int16_t* mod, int16_t* buf,
                                                 uint8_t size, float d0, float dStep)
{
	const int16_t *row = pl->row;
	const uint32_t wPh = pl->wPh, ra = pl->ra, rb = pl->rb;
	const int32_t g = pl->gQ14;
	const uint32_t inc = osc->phaseInc;
	uint32_t ph = osc->phase;
	float dd = d0;
	uint8_t i;

	for (i = 0u; i < size; i++) {
		const uint32_t rp = ph + ((uint32_t)__SSAT(
			(int32_t)((float)mod[i] * dd), 16) << 19);
		const int32_t o = osc_pwmReadT(row, osc_pwmWarp(rp, wPh, ra, rb));

		dd += dStep;
		ph += inc;
		buf[i] = OSC_PWM_STORE(o, g);
	}
	osc->phase = ph;
}

static OSC_S094P1_LOOP void osc_pwmWsFmRampBlock(OscInfo* osc,
                                                 const osc_pwmPlan_t* pl,
                                                 const int16_t* mod, int16_t* buf,
                                                 uint8_t size, float d0, float dStep)
{
	const uint32_t wPh = pl->wPh, ra = pl->ra, rb = pl->rb;
	const int32_t g = pl->gQ14;
	const uint32_t inc = osc->phaseInc;
	uint32_t ph = osc->phase;
	float dd = d0;
	uint8_t i;

	for (i = 0u; i < size; i++) {
		const uint32_t rp = ph + ((uint32_t)__SSAT(
			(int32_t)((float)mod[i] * dd), 16) << 19);
		const int32_t o = osc_pwmReadS(osc_pwmWarp(rp, wPh, ra, rb));

		dd += dStep;
		ph += inc;
		buf[i] = OSC_PWM_STORE(o, g);
	}
	osc->phase = ph;
}

/* ----------------------------------------------------------------- ring */

/*
 * pwm ring carriers (S094 P1 S3; Drum rng through osc_renderTypedRing()).
 *
 * What:       the carrier sample car = SSAT16((o x gQ14) >> 14) at the plan's
 *             level (render gain 1, as the S085 ring copies), then the S085
 *             ring law osc_ringSample(car, modBuf[i], d_i) with
 *             d_i = depthQ15 + (i + 1) x depthStepQ15. d = 0 is the carrier
 *             exactly (law PW6).
 * Inputs:     osc, pl (gQ14 at gain 1), modBuf[size], buf, size, depthQ15,
 *             depthStepQ15.
 * Outputs:    buf; osc->phase.
 * Cost:       31 per sample (PWM draft d_ring) against the ring role's 42.
 * Accessors:  osc_pwmRenderRing().
 */
static OSC_S094P1_LOOP void osc_pwmDRingBlock(OscInfo* osc, const osc_pwmPlan_t* pl,
                                              const int16_t* modBuf, int16_t* buf,
                                              uint8_t size, int32_t depthQ15,
                                              int32_t depthStepQ15)
{
	const int16_t *row = pl->row;
	const uint32_t d = pl->d;
	const int32_t sign = pl->sign, g = pl->gQ14;
	const uint32_t inc = osc->phaseInc;
	uint32_t ph = osc->phase;
	/* the ramped ring depth, Q15 */
	int32_t rd = depthQ15;
	uint8_t i;

	for (i = 0u; i < size; i++) {
		const int32_t o = osc_pwmReadD(row, ph, d, sign);

		ph += inc;
		rd += depthStepQ15;
		buf[i] = osc_ringSample(OSC_PWM_STORE(o, g), modBuf[i], rd);
	}
	osc->phase = ph;
}

static OSC_S094P1_LOOP void osc_pwmWtRingBlock(OscInfo* osc, const osc_pwmPlan_t* pl,
                                               const int16_t* modBuf, int16_t* buf,
                                               uint8_t size, int32_t depthQ15,
                                               int32_t depthStepQ15)
{
	const int16_t *row = pl->row;
	const uint32_t wPh = pl->wPh, ra = pl->ra, rb = pl->rb;
	const int32_t g = pl->gQ14;
	const uint32_t inc = osc->phaseInc;
	uint32_t ph = osc->phase;
	int32_t rd = depthQ15;
	uint8_t i;

	for (i = 0u; i < size; i++) {
		const int32_t o = osc_pwmReadT(row, osc_pwmWarp(ph, wPh, ra, rb));

		ph += inc;
		rd += depthStepQ15;
		buf[i] = osc_ringSample(OSC_PWM_STORE(o, g), modBuf[i], rd);
	}
	osc->phase = ph;
}

static OSC_S094P1_LOOP void osc_pwmWsRingBlock(OscInfo* osc, const osc_pwmPlan_t* pl,
                                               const int16_t* modBuf, int16_t* buf,
                                               uint8_t size, int32_t depthQ15,
                                               int32_t depthStepQ15)
{
	const uint32_t wPh = pl->wPh, ra = pl->ra, rb = pl->rb;
	const int32_t g = pl->gQ14;
	const uint32_t inc = osc->phaseInc;
	uint32_t ph = osc->phase;
	int32_t rd = depthQ15;
	uint8_t i;

	for (i = 0u; i < size; i++) {
		const int32_t o = osc_pwmReadS(osc_pwmWarp(ph, wPh, ra, rb));

		ph += inc;
		rd += depthStepQ15;
		buf[i] = osc_ringSample(OSC_PWM_STORE(o, g), modBuf[i], rd);
	}
	osc->phase = ph;
}

/* ----------------------------------------------------------------- sync */

/*
 * pwm sync slaves (S094 P1 S3; Drum snc through osc_renderTypedSync()).
 *
 * What:       the plain loops at render gain 1, with the phase update
 *             ph = osc_syncPull(ph + inc, over[i], ratioQ16, depthQ30) (the
 *             S085 pull: the reset lands on sample i + 1). The warp and the
 *             two reads follow the pulled phase, so a hard sync restarts the
 *             pulse at its rising edge. depthQ30 = 0 is the plain loop (law
 *             PW6).
 * Inputs:     osc, pl (gQ14 at gain 1), over[size], ratioQ16 (from the
 *             dispatcher), depthQ30, buf, size.
 * Outputs:    buf; osc->phase.
 * Cost:       35 per sample (PWM draft d_sync) against the sync role's 41.
 * Accessors:  osc_pwmRenderSync().
 */
static OSC_S094P1_LOOP void osc_pwmDSyncBlock(OscInfo* osc, const osc_pwmPlan_t* pl,
                                              const uint32_t* over, uint32_t ratioQ16,
                                              int32_t depthQ30, int16_t* buf,
                                              uint8_t size)
{
	const int16_t *row = pl->row;
	const uint32_t d = pl->d;
	const int32_t sign = pl->sign, g = pl->gQ14;
	const uint32_t inc = osc->phaseInc;
	uint32_t ph = osc->phase;
	uint8_t i;

	for (i = 0u; i < size; i++) {
		const int32_t o = osc_pwmReadD(row, ph, d, sign);

		ph = osc_syncPull(ph + inc, over[i], ratioQ16, depthQ30);
		buf[i] = OSC_PWM_STORE(o, g);
	}
	osc->phase = ph;
}

static OSC_S094P1_LOOP void osc_pwmWtSyncBlock(OscInfo* osc, const osc_pwmPlan_t* pl,
                                               const uint32_t* over, uint32_t ratioQ16,
                                               int32_t depthQ30, int16_t* buf,
                                               uint8_t size)
{
	const int16_t *row = pl->row;
	const uint32_t wPh = pl->wPh, ra = pl->ra, rb = pl->rb;
	const int32_t g = pl->gQ14;
	const uint32_t inc = osc->phaseInc;
	uint32_t ph = osc->phase;
	uint8_t i;

	for (i = 0u; i < size; i++) {
		const int32_t o = osc_pwmReadT(row, osc_pwmWarp(ph, wPh, ra, rb));

		ph = osc_syncPull(ph + inc, over[i], ratioQ16, depthQ30);
		buf[i] = OSC_PWM_STORE(o, g);
	}
	osc->phase = ph;
}

static OSC_S094P1_LOOP void osc_pwmWsSyncBlock(OscInfo* osc, const osc_pwmPlan_t* pl,
                                               const uint32_t* over, uint32_t ratioQ16,
                                               int32_t depthQ30, int16_t* buf,
                                               uint8_t size)
{
	const uint32_t wPh = pl->wPh, ra = pl->ra, rb = pl->rb;
	const int32_t g = pl->gQ14;
	const uint32_t inc = osc->phaseInc;
	uint32_t ph = osc->phase;
	uint8_t i;

	for (i = 0u; i < size; i++) {
		const int32_t o = osc_pwmReadS(osc_pwmWarp(ph, wPh, ra, rb));

		ph = osc_syncPull(ph + inc, over[i], ratioQ16, depthQ30);
		buf[i] = OSC_PWM_STORE(o, g);
	}
	osc->phase = ph;
}

/* ---------------------------------------------------------------- roles */

/*
 * The pwm role renderers (S094 P1 S3; declared before the typed dispatchers).
 *
 * What:       each builds the block's plan (render gain for plain / FM /
 *             FM ramp; gain 1 for ring and sync, as the S085 copies) and calls
 *             the form's loop: D, WT or WS.
 * Why:        the ITCM dispatchers gain one call each; the three-way form
 *             choice and the plan live in flash, once per block.
 * Inputs:     the dispatcher's arguments.
 * Outputs:    buf; osc->phase.
 * Accessors:  the six typed dispatchers (S3.4).
 */
static __attribute__((noinline, noclone)) void osc_pwmRenderPlain(
	OscInfo* osc, int16_t* buf, uint8_t size, float gain)
{
	osc_pwmPlan_t pl;

	osc_pwmPlan(osc, gain, &pl);
	if (pl.form == OSC_PWM_FORM_D)
		osc_pwmDPlainBlock(osc, &pl, buf, size);
	else if (pl.form == OSC_PWM_FORM_WT)
		osc_pwmWtPlainBlock(osc, &pl, buf, size);
	else
		osc_pwmWsPlainBlock(osc, &pl, buf, size);
}

static __attribute__((noinline, noclone)) void osc_pwmRenderFm(
	OscInfo* osc, const int16_t* mod, int16_t* buf, uint8_t size, float gain)
{
	osc_pwmPlan_t pl;

	osc_pwmPlan(osc, gain, &pl);
	if (pl.form == OSC_PWM_FORM_D)
		osc_pwmDFmBlock(osc, &pl, mod, buf, size);
	else if (pl.form == OSC_PWM_FORM_WT)
		osc_pwmWtFmBlock(osc, &pl, mod, buf, size);
	else
		osc_pwmWsFmBlock(osc, &pl, mod, buf, size);
}

static __attribute__((noinline, noclone)) void osc_pwmRenderFmRamp(
	OscInfo* osc, const int16_t* mod, int16_t* buf, uint8_t size, float gain,
	float d0, float dStep)
{
	osc_pwmPlan_t pl;

	osc_pwmPlan(osc, gain, &pl);
	if (pl.form == OSC_PWM_FORM_D)
		osc_pwmDFmRampBlock(osc, &pl, mod, buf, size, d0, dStep);
	else if (pl.form == OSC_PWM_FORM_WT)
		osc_pwmWtFmRampBlock(osc, &pl, mod, buf, size, d0, dStep);
	else
		osc_pwmWsFmRampBlock(osc, &pl, mod, buf, size, d0, dStep);
}

static __attribute__((noinline, noclone)) void osc_pwmRenderRing(
	OscInfo* osc, const int16_t* modBuf, int16_t* buf, uint8_t size,
	int32_t depthQ15, int32_t depthStepQ15)
{
	osc_pwmPlan_t pl;

	osc_pwmPlan(osc, 1.0f, &pl);
	if (pl.form == OSC_PWM_FORM_D)
		osc_pwmDRingBlock(osc, &pl, modBuf, buf, size, depthQ15, depthStepQ15);
	else if (pl.form == OSC_PWM_FORM_WT)
		osc_pwmWtRingBlock(osc, &pl, modBuf, buf, size, depthQ15, depthStepQ15);
	else
		osc_pwmWsRingBlock(osc, &pl, modBuf, buf, size, depthQ15, depthStepQ15);
}

static __attribute__((noinline, noclone)) void osc_pwmRenderSync(
	OscInfo* osc, const uint32_t* over, uint32_t ratioQ16, int32_t depthQ30,
	int16_t* buf, uint8_t size)
{
	osc_pwmPlan_t pl;

	osc_pwmPlan(osc, 1.0f, &pl);
	if (pl.form == OSC_PWM_FORM_D)
		osc_pwmDSyncBlock(osc, &pl, over, ratioQ16, depthQ30, buf, size);
	else if (pl.form == OSC_PWM_FORM_WT)
		osc_pwmWtSyncBlock(osc, &pl, over, ratioQ16, depthQ30, buf, size);
	else
		osc_pwmWsSyncBlock(osc, &pl, over, ratioQ16, depthQ30, buf, size);
}
```

### S3.6 `STM32F765VIHx_FLASH.ld`

**MODIFY** l. 180.

```ld
        *(.itcm) *(.itcm.s085) *(.itcm.s087 .itcm.s087.*) *(.itcm.s089) *(.itcm.s089p3) *(.itcm.s094p1) *(.itcm.*)
```

**ADD** to the comment block above it (after its S089 P3 sentence, ending
"does not match .itcm.s089p3."):

```ld
         *             S094 P1: .itcm.s094p1 (the PWM loops, Oscillator.c
         *             OSC_S094P1_SECTION) is named after .itcm.s089p3 so it is
         *             always appended; no existing function moves.
```

### S3.7 `Core/Menu/MenuText.h`

**MODIFY** l. 151–164 (`oscTypeNames` and its comment).

```c
/*
 * osc1_type names (S087 stage B; S094 P1 S3 pwm).
 *
 * What:       [0] = count (OSC_TYPE_COUNT), then wav smp slp wtb pwm.
 * Why:        the typ cell, its automation display and the single-parameter
 *             view share one table; the menu shows only the first
 *             OSC_TYPE_SELECTABLE entries (all five after S094 P1).
 * Accessors:  getMaxEntriesForMenu(), getMenuItemNameForValue(),
 *             menu_automationValueMax() (menu.c).
 * Affiliates: OSC_TYPE_* (Oscillator.h); MENU_OSC_TYPE.
 */
static const char oscTypeNames[][4] = {
    {5}, {"wav"},{"smp"},{"slp"},{"wtb"},{"pwm"},
};
```

`menu.c` l. 12541's assert (count == `OSC_TYPE_COUNT`) holds with 5 = 5.

### S3.8 `Core/Menu/menu.c` — `menu_formatWaveCell()`

**ADD** before l. 12188 (`    if (isOsc1 && type != OSC_TYPE_WAV) {`). Anchor:
`/* osc1 under smp/slp/wtb: the value is the item index. */`.

```c
    /*
     * S094 P1 S3: osc1 under pwm names Sin, Tri, Saw, Rec.
     *
     * What:       value 0..OSC_PWM_WAVE_COUNT - 1 -> the stock name (no
     *             compact code, as wav); any other value names nothing.
     * Why:        under pwm the wav byte picks one of the four stock waves
     *             that have a pulse-width form (osc_userCount()); the item
     *             branch below would read it as a catalogue index. An
     *             Alkaline slot under pwm (S5) takes the Saw/Sqr branch above.
     */
    if (isOsc1 && type == OSC_TYPE_PWM) {
        if (value < OSC_PWM_WAVE_COUNT)
            memcpy(nameDst, waveformNames[value + 1u], 3u);
        return;
    }
```

### S3.9 Host tests

**`tools/dsp_test/Makefile` ADD** after l. 74 (the `Oscillator.host.c`
rule).

```make
# S094 P1 S3: the generated PWM tables, bodies for the host (the oscillator
# and alkaline modules compile the whole Oscillator.c, which reads them).
$(GEN)/oscPwmTables.host.h: $(ROOT)/Core/DSPAudio/oscPwmTables.h | $(GEN)
	$(EX) --src $< --body --out $@
$(GEN)/oscPwmTables.host.c: $(ROOT)/Core/DSPAudio/oscPwmTables.c | $(GEN)
	$(EX) --src $< --body --out $@
```

**MODIFY** l. 80 and l. 319 (the `oscillator:` and `alkaline:` prerequisite
lists): add `$(GEN)/oscPwmTables.host.h $(GEN)/oscPwmTables.host.c` to each.

**ADD** to the `oscillator` recipe after the `break_fmr2` block (after
l. 103). These are the S3 break proofs.

```make
# S094 P1 S3 breaks (pwm group): the second read at ph + w instead of
# ph - w must fail PW2/PW3; the width slew without its limit must fail PW5.
	mkdir -p build/break_pwm1 build/break_pwm2
	sed 's/const uint32_t ib = (ia - d) \& 0x3FFu;/const uint32_t ib = (ia + d) \& 0x3FFu;/' \
	  $(GEN)/Oscillator.host.c > build/break_pwm1/Oscillator.host.c
	@grep -q '(ia + d) & 0x3FFu' build/break_pwm1/Oscillator.host.c || \
	  { echo "oscillator_break_pwm1: sed did not apply"; exit 1; }
	$(HOSTCC) -Ibuild/break_pwm1 $(HOSTFLAGS) $(OSC_HOST_INC) modules/oscillator.c \
	  $(CAT_HOST_SRC) -o build/oscillator_break_pwm1 -lm
	@if ./build/oscillator_break_pwm1 >/dev/null 2>&1; then \
	  echo "oscillator_break_pwm1: FAILED (a forward second read not caught)"; exit 1; \
	else echo "oscillator_break_pwm1: caught (law PW2 fails as expected)"; fi
	sed 's/if (d > OSC_PWM_SLEW_STEPS)/if (0)/' \
	  $(GEN)/Oscillator.host.c > build/break_pwm2/Oscillator.host.c
	@grep -q 'if (0)' build/break_pwm2/Oscillator.host.c || \
	  { echo "oscillator_break_pwm2: sed did not apply"; exit 1; }
	$(HOSTCC) -Ibuild/break_pwm2 $(HOSTFLAGS) $(OSC_HOST_INC) modules/oscillator.c \
	  $(CAT_HOST_SRC) -o build/oscillator_break_pwm2 -lm
	@if ./build/oscillator_break_pwm2 >/dev/null 2>&1; then \
	  echo "oscillator_break_pwm2: FAILED (an unlimited width step not caught)"; exit 1; \
	else echo "oscillator_break_pwm2: caught (law PW5 fails as expected)"; fi
```

**ADD** a `pwm_tables` target next to `fold_tables` (after l. 435), and add
`pwm_tables` to `.PHONY` and `all` (l. 56–62).

```make
pwm_tables:
	cd $(ROOT) && python3 tools/plots/gen_pwm_tables.py --check
```

**`tools/dsp_test/osc_host.h` ADD** after l. 61
(`#include "Oscillator.host.h"`).

```c
/* S094 P1 S3: the PWM tables' declarations (generated header body) */
#include "oscPwmTables.host.h"
```

**`tools/dsp_test/modules/oscillator.c` and `modules/alkaline.c` ADD** after
`#include "Oscillator.host.c"` (l. 57 and l. 92 respectively).

```c
#include "oscPwmTables.host.c"   /* S094 P1 S3: the real generated tables */
```

**`tools/dsp_test/modules/oscillator.c` ADD** after l. 54
(`#include <math.h>`). The pwm group uses `abs()`; the module does not
include `<stdlib.h>` today (checked).

```c
#include <stdlib.h>   /* S094 P1 S3: abs() in the pwm group */
```

**`tools/dsp_test/modules/oscillator.c` ADD** a `pwm` group before `main()`
(before l. 1327; anchor `int main(void)`), and
`group("pwm", run_pwm);` after l. 1340 (`group("fmramp", run_fmramp);`).

```c
/* ================================================================ pwm --
 * S094 P1 S3 laws for typ pwm.
 *   PW1  p = 64: WS equals calcSineBlock() and WT equals the stock table
 *        read (calcWavetableOscBlock()) within 2 LSB (integer against float
 *        interpolation) at gain 1;
 *   PW2  D, both signs, p in {1, 16, 63, 64, 65, 100, 127}, three phases and
 *        increments: bit-exact against an independent reader written here;
 *   PW3  orientation on an ideal descending saw (1 - 2x): the difference is
 *        high (> 0) for the first w of the cycle and low after; the sum and
 *        the difference both peak at the table's level (within the Q14
 *        rounding);
 *   PW4  WT on an ideal triangle: the output's peak sits at phase p / 128
 *        (within one table point) for p in {8, 32, 64, 96, 120};
 *   PW5  the width slew moves at most OSC_PWM_SLEW_STEPS per block toward
 *        pos, lands on it, never overshoots, leaves phaseInc untouched;
 *        osc_typedTriggerPhase() snaps it and returns 0;
 *   PW6  depth 0 identities: FM (fmMod 0), FM ramp (d0 = dStep = 0), ring
 *        (depth 0), sync (depth 0) equal the plain role at gain 1 bit for
 *        bit, every form;
 *   PW7  routing: osc_renderTyped / Fm / FmRamp / Ring / Sync send an
 *        OSC_BIND_PWM oscillator to the pwm roles (outputs equal the role
 *        called directly); oscSection_renderOldCarrier() on a pwm snapshot
 *        plays the frozen width;
 *   PW8  type and bind: osc_setType(pwm) clamps wav 4 -> 3 (Rec);
 *        osc_isTyped() is 1 for pwm, 0 for OSC_TYPE_MODLIST (5);
 *        osc_userCount(pwm) is 4; osc_userBind() gives
 *        freq2PhaseIncr1024(f) and freqToTableIndex(f), bind OSC_BIND_PWM.
 * Break builds: the Makefile's break_pwm1 (forward second read) and
 * break_pwm2 (unlimited slew).
 */
static OscInfo pwm_osc(uint8_t wave, uint8_t width, uint32_t phase, uint32_t inc)
{
    OscInfo o = make_osc(wave, 3u, phase, inc);

    o.oscType = OSC_TYPE_PWM;
    o.sampleCacheLooped = OSC_BIND_PWM;
    o.position = width;
    o.positionApplied = width;
    return o;
}

/* the independent D reader (PW2): the law written out, not the loop */
static int16_t pwm_refD(uint8_t width, int sign, uint32_t ph, int g)
{
    const int16_t *row = sawTable[3];
    const uint32_t w = (uint32_t)(width ? width : 1u) * 8u;
    const uint32_t ia = ph >> 22, ib = (ia + 1024u - w) & 1023u;
    const int32_t a0 = row[ia], a1 = row[(ia + 1u) & 1023u];
    const int32_t b0 = row[ib], b1 = row[(ib + 1u) & 1023u];
    const int32_t d0 = a0 + sign * b0, d1 = a1 + sign * b1;
    const int32_t o = d0 + (((d1 - d0) * (int32_t)((ph >> 7) & 0x7FFF)) >> 15);
    int32_t v = (o * g) >> 14;

    return (int16_t)(v > 32767 ? 32767 : (v < -32768 ? -32768 : v));
}

static void pwm_d_exact(void)
{
    static const uint8_t widths[] = { 1u, 16u, 63u, 64u, 65u, 100u, 127u };
    static const uint32_t phases[] = { 0u, 0x7fff1234u, 0xfffff000u };
    static const uint32_t incs[] = { 0x00800000u, 0x07000000u, 0x30000000u };
    uint32_t seed = 0x94d0u;

    /*
     * Random row 3, bounded to +-8,192: a wrong offset or sign shows at once,
     * and every neighbouring-point step stays <= 16,383, inside the D form's
     * proven range (the stock rows' worst step is 32,767; unbounded random
     * int16 rows would overflow the Q15 product, which no stock row can).
     */
    for (unsigned i = 0u; i < 1024u; ++i)
        sawTable[3][i] = (int16_t)((int32_t)(golden_rand(&seed) >> 18) - 8192);
    for (unsigned s = 0u; s < 2u; ++s)
     for (unsigned w = 0u; w < sizeof widths; ++w)
      for (unsigned p = 0u; p < 3u; ++p)
       for (unsigned k = 0u; k < 3u; ++k) {
        OscInfo o = pwm_osc(s ? REC : SAW, widths[w], phases[p], incs[k]);
        const unsigned m = ((widths[w] > 64u) ? widths[w] : 128u - widths[w]) - 64u;
        const int g = oscPwmLevelQ14[m];
        int16_t out[N];
        uint32_t ph = phases[p];

        osc_pwmRenderPlain(&o, out, N, 1.0f);
        for (unsigned i = 0u; i < N; ++i, ph += incs[k])
            if (out[i] != pwm_refD(widths[w], s ? -1 : 1, ph, g)) {
                fail("PW2 D exact", s, widths[w], i);
                break;
            }
    }
}

static void pwm_identities(void)
{
    static const uint8_t waves[] = { SINE, TRI, SAW, REC };
    int16_t mod[N], plain[N], out[N];
    uint32_t over[N];
    uint32_t s = 0x94u;

    random_mod(mod, &s);
    for (unsigned i = 0u; i < N; ++i) over[i] = OSC_SYNC_NONE;
    for (unsigned w = 0u; w < 4u; ++w) {
        OscInfo a = pwm_osc(waves[w], 37u, 0x12340000u, 0x02345678u), b;

        b = a; osc_pwmRenderPlain(&b, plain, N, 1.0f);
        b = a; b.fmMod = 0.0f; osc_pwmRenderFm(&b, mod, out, N, 1.0f);
        if (!same_buf(out, plain)) fail("PW6 FM depth 0", w, 0u, 0u);
        b = a; osc_pwmRenderFmRamp(&b, mod, out, N, 1.0f, 0.0f, 0.0f);
        if (!same_buf(out, plain)) fail("PW6 FM ramp depth 0", w, 0u, 0u);
        b = a; osc_pwmRenderRing(&b, mod, out, N, 0, 0);
        if (!same_buf(out, plain)) fail("PW6 ring depth 0", w, 0u, 0u);
        b = a; osc_pwmRenderSync(&b, over, 65536u, 0, out, N);
        if (!same_buf(out, plain)) fail("PW6 sync depth 0", w, 0u, 0u);
        /* PW7: the ITCM dispatchers route OSC_BIND_PWM to the roles */
        b = a; osc_renderTyped(&b, out, N, 1.0f);
        if (!same_buf(out, plain)) fail("PW7 osc_renderTyped", w, 0u, 0u);
        /*
         * PW7: the itp old carrier of a pwm snapshot plays its frozen width.
         * The live block is the plain render itself, so the crossfade of two
         * equal signals returns them to within 1 LSB (the blend's float
         * rounding); a wrong width or a zero block would be far off.
         */
        {
            OscCarrierSnapshot snap;
            int16_t live[N];

            memset(&snap, 0, sizeof snap);
            snap.phase = a.phase;
            snap.phaseInc = a.phaseInc;
            snap.waveform = a.waveform;
            snap.tableOffset = a.tableOffset;
            snap.oscType = OSC_TYPE_PWM;
            snap.sampleCacheLooped = OSC_BIND_PWM;
            snap.positionApplied = a.positionApplied;
            snap.active = 1u;
            snap.counter = OSC_RUN_ITP_FADE_BLOCKS;
            memcpy(live, plain, sizeof live);
            oscSection_renderOldCarrier(&snap, live, N);
            for (unsigned i = 0u; i < N; ++i)
                if (abs(live[i] - plain[i]) > 1) {
                    fail("PW7 old carrier at its frozen width", w, i, 0u);
                    break;
                }
        }
    }
}

static void pwm_slew_and_type(void)
{
    OscInfo o = pwm_osc(SAW, 10u, 0u, 0x01000000u);
    const uint32_t inc = o.phaseInc;

    o.position = 100u;
    for (unsigned b = 0u; b < 60u; ++b) {
        const uint8_t before = o.positionApplied;

        osc_typedPositionApply(&o, 1u);
        if (abs((int)o.positionApplied - (int)before) > OSC_PWM_SLEW_STEPS)
            fail("PW5 slew step", b, before, o.positionApplied);
        if (o.positionApplied > 100u) fail("PW5 overshoot", b, o.positionApplied, 0u);
        if (o.phaseInc != inc || o.positionBendStep != 0)
            fail("PW5 phaseInc untouched", b, 0u, 0u);
        osc_typedPositionRestore(&o);
    }
    if (o.positionApplied != 100u) fail("PW5 lands", o.positionApplied, 100u, 0u);
    o.position = 3u;
    if (osc_typedTriggerPhase(&o) != 0u || o.positionApplied != 3u)
        fail("PW5 trigger snaps", o.positionApplied, 0u, 0u);
    /* PW8 */
    {
        OscInfo t = make_osc(SAW, 0u, 0u, 0u);

        t.freq = 220.0f; t.pitchMod = 1.0f; t.modNodeValue = 1.0f;
        t.waveform = 4u;
        osc_setType(&t, OSC_TYPE_PWM);
        if (t.waveform != 3u) fail("PW8 wav clamps to Rec", t.waveform, 3u, 0u);
        if (!osc_isTyped(&t)) fail("PW8 pwm is typed", 0u, 0u, 0u);
        t.oscType = OSC_TYPE_MODLIST;
        if (osc_isTyped(&t)) fail("PW8 MODLIST is not typed", 0u, 0u, 0u);
        if (osc_userCount(OSC_TYPE_PWM) != 4u) fail("PW8 count", osc_userCount(OSC_TYPE_PWM), 4u, 0u);
        t.oscType = OSC_TYPE_PWM;
        osc_userBind(&t);
        if (t.sampleCacheLooped != OSC_BIND_PWM || t.phaseInc != freq2PhaseIncr1024(220.0f) ||
            t.tableOffset != freqToTableIndex(220.0f))
            fail("PW8 bind law", t.phaseInc, freq2PhaseIncr1024(220.0f), t.tableOffset);
    }
}

static void pwm_shapes(void)
{
    /* PW1: p = 64 against the stock reads (shape tables) */
    {
        OscInfo a = pwm_osc(SINE, 64u, 0x01000000u, 0x00654321u), b;
        int16_t x[N], y[N];

        b = a; b.oscType = OSC_TYPE_WAV;
        osc_pwmRenderPlain(&a, x, N, 1.0f);
        calcSineBlock(&b, y, N, 1.0f);
        for (unsigned i = 0u; i < N; ++i)
            if (abs(x[i] - y[i]) > 2) { fail("PW1 sine at 64", i, x[i], y[i]); break; }
        a = pwm_osc(TRI, 64u, 0x01000000u, 0x00654321u);
        b = a; b.oscType = OSC_TYPE_WAV;
        osc_pwmRenderPlain(&a, x, N, 1.0f);
        calcWavetableOscBlock(&b, (const int16_t (*)[1024])triTable, y, N, 1.0f);
        for (unsigned i = 0u; i < N; ++i)
            if (abs(x[i] - y[i]) > 2) { fail("PW1 tri at 64", i, x[i], y[i]); break; }
    }
    /* PW3: orientation on an ideal descending saw */
    for (unsigned i = 0u; i < 1024u; ++i)
        sawTable[3][i] = (int16_t)lrint(32767.0 * (1.0 - 2.0 * (double)i / 1024.0));
    {
        static const uint8_t widths[] = { 16u, 64u, 100u };

        for (unsigned w = 0u; w < 3u; ++w) {
            OscInfo o = pwm_osc(REC, widths[w], 0u, 0x00800000u);   /* 1/512 cycle */
            int16_t out[N];
            unsigned hiBad = 0u, loBad = 0u;

            for (unsigned blk = 0u; blk < 16u; ++blk) {
                const uint32_t ph0 = o.phase;

                osc_pwmRenderPlain(&o, out, N, 1.0f);
                for (unsigned i = 0u; i < N; ++i) {
                    const uint32_t ph = ph0 + i * 0x00800000u;
                    const uint32_t edge = (uint32_t)widths[w] << 25;
                    /* away from the two edges (3 points either side) */
                    const int nearEdge = (ph >> 22) < 3u || (ph >> 22) > 1020u ||
                        ((ph >> 22) + 3u > (edge >> 22) && (ph >> 22) < (edge >> 22) + 3u);

                    if (nearEdge) continue;
                    if (ph < edge && out[i] <= 0) hiBad++;
                    if (ph >= edge && out[i] >= 0) loBad++;
                }
            }
            if (hiBad || loBad) fail("PW3 pulse high first", widths[w], hiBad, loBad);
        }
    }
    /* PW4: the warped triangle peaks at p / 128 */
    fill_shape_tables();
    {
        static const uint8_t widths[] = { 8u, 32u, 64u, 96u, 120u };

        for (unsigned w = 0u; w < 5u; ++w) {
            OscInfo o = pwm_osc(TRI, widths[w], 0u, 0x00400000u);   /* 1/1024 */
            int16_t out[N];
            int best = -40000;
            uint32_t bestPh = 0u;

            for (unsigned blk = 0u; blk < 32u; ++blk) {
                const uint32_t ph0 = o.phase;

                osc_pwmRenderPlain(&o, out, N, 1.0f);
                for (unsigned i = 0u; i < N; ++i)
                    if (out[i] > best) { best = out[i]; bestPh = ph0 + i * 0x00400000u; }
            }
            if (abs((int)(bestPh >> 22) - (int)widths[w] * 8) > 1)
                fail("PW4 tri peak at p/128", widths[w], bestPh >> 22, widths[w] * 8u);
        }
    }
}

static void run_pwm(void)
{
    /* the shape tables (bounded steps) for the identities, then pwm_d_exact()
     * replaces row 3 of the saw with bounded random data */
    fill_shape_tables();
    pwm_identities();
    pwm_slew_and_type();
    pwm_d_exact();
    fill_shape_tables();
    pwm_shapes();
}
```

The group calls the file-static role functions and plan. That is possible
because the module compiles the whole `Oscillator.c` into one translation
unit (`#include "Oscillator.host.c"`, the existing pattern).

### S3.10 Gates for S3

1. **Tables:** `pwm_tables` (generator check), `oscillator` (pwm group, two
   breaks caught), `alkaline` (AL-OD0 unchanged).
2. **Old loops:** `elf_p4_check P4BASE=baselines/s094p1/s2`.
   - 0 MISSING, 0 changed for every old loop.
   - The typed dispatchers are not loops; their per-block paths change.
   - The engines' `osc_isTyped()` constant: confirm by source pairing that
     no inlined mixer loop grew (mixer 0 GREW).
3. **New loops:** `elf_looppath.py --cond-heads --in-range` on the fifteen
   `osc_pwm*Block` symbols, each against its role ceiling:

   | Role | Ceiling |
   |---|---:|
   | plain | 27 |
   | FM | 50 |
   | FM ramp | 51 |
   | ring | 42 |
   | sync | 41 |

   The PWM draft's standalone counts are the expectation (plain 22–23, FM
   30–31, FM ramp 31, ring 31, sync 35). Any loop over its role ceiling
   stops the step.
4. **Per block:** `elf_blockpath.py` on the five `osc_pwmRender*` roles plus
   `osc_pwmPlan` and `osc_userBind`'s pwm branch.
   - **Whole voices:** Drum's whole voice with osc1 pwm in each mode, and
     Cymbal/HiHat with a pwm carrier, are computed as in S6 (loops +
     per block / 32).
   - **They must stay at or below:** Drum's dearest after S2 (258.3) and
     Cymbal/HiHat's (≈ 286.9).
5. **ITCM:** `link_budget.py` shows ITCM ≤ 11,590 + 2,950 B (approved
   ≈ 2.9 KB), and the linker's ITCM assert holds.
6. **Snapshot:** `baselines/s094p1/s3`, adding the fifteen new symbols to
   `P4_OSC_SYMS` (`tools/dsp_test/Makefile` l. 399–410) so later steps gate
   them.

---

## S4 — Alkaline post-filter VCA overdrive, tone and A3

### S4.0 The design in one place (post-FX draft revision 3, plan §6)

**Overdrive**
- The OTA's input argument is `vcaL × otaIn`, with
  `otaIn = alkOvdGain[ovd] × 1/(2Vt)` set per block.
  - `alkOvdGain[0]` is exactly 1.0f, so at Overdrive 0 the product is
    today's `vcaL × DL_INV_2VT` with the same two operands.
  - The make-up `alkOvdMakeup[ovd]` is folded into the per-block OTA current
    (`voice->makeup`), with `[0]` = 1.0f.

**Tone**
- `tz = o + toneC × (tz − o)` on the OTA output `o = iaL × zG × tota`, in the
  loop, followed by the existing `SSAT16`.
- `toneC = alkToneC[tone]`, with `[127]` = 0.0f exactly. So
  `tz = o + 0 × (…) = o`, fused or not.

**A3**
- `driveGain = alkDriveGain[drv]` and the drive make-up
  `alkDriveMakeup[drv]` replace the two `alk_exp2f()` calls per block.
- At drive 0 the entries are exact (the polynomial's fraction is 0). Other
  entries are within 1 ulp (law AL-A3).

**Storage and controls**
- **Tables:** a new generated pair, `AlkalinePostTables.c/.h` (plan
  finding 8), 5 × 512 B of flash.
- **Rows (appended):**
  - 29 `instrument_drive` (K1: the shared key; the other four types' labels;
    an explicit Alkaline branch in the shared writer);
  - 30 `alk_tone`.
- **Mix page:** `vol pan ton drv`.
- **RAM:** `DiodeLadder` +12 B (`otaIn`, `toneC`, `toneZ`) and
  `AlkalineVoice` +8 B (`vcaDrive`, `tone`). Both lie inside the 1,264 B slot
  reserve: no growth.

### S4.1 NEW FILE `tools/alkaline/gen_alk_post_tables.py`

```python
#!/usr/bin/env python3
"""Generate the Alkaline post-filter and drive tables (S094 P1 S4).

What:       writes Core/DSP/Instruments/Alkaline/AlkalinePostTables.h and .c,
            five float tables of ALK_POST_POINTS (128) entries indexed by a
            descriptor byte 0..127:
              alkOvdGain[k]     the VCA overdrive's OTA input gain,
                                2^(4k/127): x1 .. x16; [0] = 1.0f exactly;
              alkOvdMakeup[k]   its output make-up, 2^(-2k/127) (a starting
                                curve, 1/sqrt of the gain, P9-D4: set by ear
                                on the device); [0] = 1.0f exactly;
              alkToneC[k]       the tone one-pole's coefficient
                                c = exp(-2 pi fc / fs), fc = 800 Hz x
                                (20 kHz / 800 Hz)^(k/127) for k < 127, and
                                [127] = 0.0f exactly (open: bit-identical);
              alkDriveGain[k]   A3: the input drive 16^(k/127), evaluated as
                                the firmware does: t = alk_exp2f(k/127.0f),
                                (t x t) x (t x t) in float32;
              alkDriveMakeup[k] A3: alk_exp2f(ALK_MAKEUP_OCT x (1 - k/127.0f)).
            alk_exp2f is emulated in float32 with the firmware's Horner order
            and one fused multiply-add per step (GCC -Ofast contracts each
            a + f x b; checked against the S0 image's disassembly of
            Alkaline_calcVoiceAsync in S4.13 step 1).
Why:        post-FX draft P9-D1 (A3) and §3: per-block table reads replace two
            exp2 evaluations and give the overdrive and tone their curves; the
            neutral entries (overdrive 0, tone 127, drive 0) are exact, so
            Overdrive 0 / Tone 127 is bit-identical to the S0 voice (P9-D7).
Checks:     Q1 alkOvdGain[0] == alkOvdMakeup[0] == 1.0 and alkToneC[127] == 0.0;
            Q2 alkOvdGain increasing, alkOvdMakeup decreasing, alkToneC
            decreasing in k, every entry finite and positive except
            alkToneC[127]; Q3 alkDriveGain[0] and alkDriveMakeup[0] equal the
            exact f = 0 evaluation; Q4 --check compares both files byte for
            byte. Exit 1 on any failure.
Inputs:     the constants below and AlkalineControl.h's alk_exp2f
            coefficients (copied here; Q3 and the bench law AL-A3 catch a
            mismatch).
Outputs:    the two files, or a pass/fail line with --check.
Accessors:  tools/dsp_test/Makefile target alk_post_tables.
Affiliates: AlkalineVoice.c Alkaline_calcVoiceAsync(); DiodeLadder.h
            DiodeLadder_setPost(); tools/dsp_test/modules/alkaline.c laws
            AL-A3, AL-OV, AL-TN, AL-OD0.
"""
import math
import sys

import numpy as np

F = np.float32
H_PATH = "Core/DSP/Instruments/Alkaline/AlkalinePostTables.h"
C_PATH = "Core/DSP/Instruments/Alkaline/AlkalinePostTables.c"
N = 128
FS = 44108.07291666667
OVD_OCT = 4.0                    # x16 at 127
OVD_MAKEUP_OCT = -2.0            # 1 / sqrt(gain): starting curve (P9-D4)
TONE_FC_MIN, TONE_FC_MAX = 800.0, 20000.0
ALK_MAKEUP_OCT = F(1.0)          # AlkalineVoice.h
# alk_exp2f's coefficients, c0..c5 (AlkalineControl.h)
EXP2_C = [F(0.999999762), F(0.693156779), F(0.240131676),
          F(0.055876568), F(0.00894057751), F(0.00189437857)]


def fma32(a, b, c):
    """float32 fused multiply-add: a x b + c rounded once (via double)."""
    return F(math.fma(float(a), float(b), float(c)))


def alk_exp2f(x):
    """alk_exp2f() in float32, the firmware's Horner order with fused steps."""
    x = F(min(max(float(x), -126.0), 127.0))
    xi = F(math.floor(float(x)))
    f = F(x - xi)
    r = EXP2_C[5]
    for c in reversed(EXP2_C[:5]):     # c4, c3, c2, c1, c0
        r = fma32(f, r, c)
    return F(F(2.0 ** int(xi)) * r)


def tables():
    k = np.arange(N)
    ovd_gain = [F(2.0 ** (OVD_OCT * i / 127.0)) for i in k]
    ovd_makeup = [F(2.0 ** (OVD_MAKEUP_OCT * i / 127.0)) for i in k]
    tone = []
    for i in k:
        if i == N - 1:
            tone.append(F(0.0))
        else:
            fc = TONE_FC_MIN * (TONE_FC_MAX / TONE_FC_MIN) ** (i / 127.0)
            tone.append(F(math.exp(-2.0 * math.pi * fc / FS)))
    drv_gain, drv_makeup = [], []
    for i in k:
        drv = F(F(i) / F(127.0))           # the descriptor's byte / 127.0f
        t = alk_exp2f(drv)
        t2 = F(t * t)
        drv_gain.append(F(t2 * t2))
        drv_makeup.append(alk_exp2f(F(ALK_MAKEUP_OCT * F(F(1.0) - drv))))
    return ovd_gain, ovd_makeup, tone, drv_gain, drv_makeup


def checks(og, om, tc, dg, dm):
    ok = og[0] == 1.0 and om[0] == 1.0 and tc[N - 1] == 0.0
    ok = ok and all(b > a for a, b in zip(og, og[1:]))
    ok = ok and all(b < a for a, b in zip(om, om[1:]))
    ok = ok and all(b < a for a, b in zip(tc, tc[1:]))
    ok = ok and all(np.isfinite(v) and v > 0 for v in og + om + dg + dm + tc[:-1])
    t0 = F(EXP2_C[0])                     # alk_exp2f(0) = 1 x c0
    ok = ok and dg[0] == F(F(t0 * t0) * F(t0 * t0))
    ok = ok and dm[0] == F(F(2.0) * EXP2_C[0])   # alk_exp2f(1) = 2 x c0
    return ok


def render_h():
    return "\n".join([
        "/* Generated by tools/alkaline/gen_alk_post_tables.py (S094 P1 S4). Do not edit. */",
        "#ifndef ALKALINE_POST_TABLES_H_",
        "#define ALKALINE_POST_TABLES_H_",
        "",
        "/*",
        " * Alkaline post-filter and drive tables (S094 P1 S4), flash, indexed by a",
        " * descriptor byte 0..127 (the knob's 0..1 value x 127, rounded).",
        " *",
        " * What:       alkOvdGain / alkOvdMakeup: the VCA overdrive's OTA input gain",
        " *             (x1..x16) and output make-up; alkToneC: the tone one-pole's c",
        " *             (0 = open at 127); alkDriveGain / alkDriveMakeup: A3, the input",
        " *             drive x1..x16 and its make-up (alk_exp2f() evaluated offline).",
        " * Accessors:  AlkalineVoice.c Alkaline_calcVoiceAsync().",
        " */",
        "#define ALK_POST_POINTS 128u",
        "extern const float alkOvdGain[ALK_POST_POINTS];",
        "extern const float alkOvdMakeup[ALK_POST_POINTS];",
        "extern const float alkToneC[ALK_POST_POINTS];",
        "extern const float alkDriveGain[ALK_POST_POINTS];",
        "extern const float alkDriveMakeup[ALK_POST_POINTS];",
        "",
        "#endif /* ALKALINE_POST_TABLES_H_ */",
        ""])


def arr(name, vals, note):
    lines = [f"/* {note} */", f"const float {name}[ALK_POST_POINTS] = {{"]
    for i in range(0, N, 4):
        lines.append("    " + ", ".join("%.9ef" % float(v) for v in vals[i:i + 4]) +
                     ",  /* %3d..%3d */" % (i, i + 3))
    return lines + ["};", ""]


def render_c(og, om, tc, dg, dm):
    lines = ["/* Generated by tools/alkaline/gen_alk_post_tables.py (S094 P1 S4). Do not edit. */",
             '#include "AlkalinePostTables.h"', ""]
    lines += arr("alkOvdGain", og, "2^(4k/127): the OTA input gain; [0] = 1 exactly")
    lines += arr("alkOvdMakeup", om, "2^(-2k/127): the overdrive make-up (starting curve); [0] = 1")
    lines += arr("alkToneC", tc, "exp(-2 pi fc / fs), fc 800 Hz .. 20 kHz; [127] = 0 (open)")
    lines += arr("alkDriveGain", dg, "A3: (alk_exp2f(k/127)^2)^2 in float32; [0] exact")
    lines += arr("alkDriveMakeup", dm, "A3: alk_exp2f(1 - k/127) in float32; [0] exact")
    return "\n".join(lines)


def main():
    og, om, tc, dg, dm = tables()
    ok = checks(og, om, tc, dg, dm)
    h, c = render_h(), render_c(og, om, tc, dg, dm)
    if "--check" in sys.argv:
        ok = ok and open(H_PATH).read() == h and open(C_PATH).read() == c
        print("alkaline post tables", "OK" if ok else "FAILED")
        return 0 if ok else 1
    if not ok:
        print("alkaline post tables: Q1-Q3 failed")
        return 1
    open(H_PATH, "w").write(h)
    open(C_PATH, "w").write(c)
    print("wrote", H_PATH, C_PATH)
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

Run once to write the NEW FILES `AlkalinePostTables.h` and `.c` (generated).

**`Makefile` ADD** after l. 156 (`  Core/DSP/Instruments/Alkaline/AlkalineTables.c \`):

```make
  Core/DSP/Instruments/Alkaline/AlkalinePostTables.c \
```

**`Makefile` ADD** after l. 244 (the end of the `AlkalineSquareTable.o`
rule):

```make
# S094 P1 S4: the post-filter and A3 tables (data, with their user).
$(BUILD)/Core/DSP/Instruments/Alkaline/AlkalinePostTables.o: Core/DSP/Instruments/Alkaline/AlkalinePostTables.c | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS_DSP) $< -o $@
```

### S4.2 `Core/DSPAudio/DiodeLadder.h`

**ADD** after l. 150 (`	float latFrac;`): the post-filter state, kept in
the float block before the two bytes so no padding moves.

```c
	/*
	 * Post-filter VCA overdrive and tone (S094 P1 S4).
	 *
	 * What:       otaIn: the OTA input argument's scale per block, gain x
	 *             1/(2 Vt) (DL_2VT); 1/(2 Vt) at Overdrive 0. toneC: the tone
	 *             one-pole's coefficient c (0 = open). toneZ: the one-pole's
	 *             state, the last stored output before its int16 saturation.
	 * Why:        P9-A' (post-FX draft §3): the overdrive turns the OTA's
	 *             constant input scale into a per-block variable, and the tone
	 *             filters the OTA output inside the loop; at their neutral
	 *             values both are exact (bit-identical output, P9-D7).
	 * Inputs:     DiodeLadder_setPost() (per block, from the async);
	 *             DiodeLadder_init().
	 * Outputs:    read by dl_renderB2Body(); toneZ written back per block.
	 * Layout:     +12 B (DiodeLadder 232 -> 244 B at r6); inside the voice's
	 *             slot reserve (no RAM growth).
	 */
	float otaIn;
	float toneC;
	float toneZ;
```

**ADD** after l. 157 (the `DiodeLadder_setNet()` prototype).

```c
/*
 * Set the post-filter controls for the next block (S094 P1 S4).
 *
 * What:       otaIn = gain x (1 / DL_2VT); toneC = c.
 * Why:        inline, so the async pays two stores and one multiply; the
 *             constant 1.0f / DL_2VT is the same expression DiodeLadder.c's
 *             DL_INV_2VT folds, so gain 1.0f gives today's OTA scale bit for
 *             bit (x x 1.0f = x).
 * Inputs:     dl; gain (alkOvdGain[], 1..16); c (alkToneC[], 0..0.9).
 * Outputs:    dl->otaIn, dl->toneC.
 * Accessors:  Alkaline_calcVoiceAsync().
 */
static inline void DiodeLadder_setPost(DiodeLadder *dl, const float gain, const float c)
{
	dl->otaIn = gain * (1.0f / DL_2VT);
	dl->toneC = c;
}
```

### S4.3 `Core/DSPAudio/DiodeLadder.c`

**ADD** after l. 132 (`	dl->phase = 0u;` in `DiodeLadder_init()`).

```c
	/* S094 P1 S4: the post-filter controls at their neutral values: the
	 * OTA's own input scale, the tone open, its state clear */
	dl->otaIn = DL_INV_2VT;
	dl->toneC = 0.0f;
	dl->toneZ = 0.0f;
```

**ADD** after l. 299 (`	float vhpL = dl->vhpLast, vcaL = dl->vcaLast, iaL = dl->iAbcLast;` in
`dl_renderB2Body()`).

```c
	/*
	 * Post-filter controls for this block (S094 P1 S4).
	 *
	 * What:       otaIn, the OTA input scale (constant over the block);
	 *             toneC, the tone coefficient (constant); tz, the one-pole's
	 *             running state (loaded here, stored after the loop).
	 * Why:        locals, as every other ladder value: one load per block, no
	 *             memory traffic in the loop.
	 */
	const float otaIn = dl->otaIn;
	const float toneC = dl->toneC;
	float tz = dl->toneZ;
```

**MODIFY** l. 407 (the OTA argument).

```c
		/* S094 P1 S4: the OTA argument v / 2Vt x the overdrive gain (otaIn =
		 * gain / 2Vt, per block; at Overdrive 0 the same operands as before) */
		const float zG = dl_clamp(vcaL * otaIn, DL_P32_ZMAX), sG = zG * zG;
```

**MODIFY** l. 426 (the store).

```c
		/*
		 * The OTA output and the tone (S094 P1 S4).
		 *
		 * What:       o = R_OTA I_abc tanh(v / 2Vt), scaled (today's value,
		 *             the same product order); tz = o + c (tz - o), a one-pole
		 *             low-pass on it; the store saturates tz to int16.
		 * Why:        the c form is exact when open: c = 0 gives o + 0 x (tz - o)
		 *             = o, fused or not, so Tone 127 stores today's sample
		 *             (P9-D7; the k form z + k (o - z) is not exact at k = 1).
		 *             The tone filters the overdrive's added highs after the
		 *             OTA, inside the loop: no second pass over the block.
		 * Cost:       one subtract and one multiply-add per sample (the post-FX
		 *             spike measured the overdrive and tone at 215 -> 219 per
		 *             sample on the one-row loop).
		 */
		const float o = iaL * zG * tota;

		tz = o + toneC * (tz - o);
		buf[j] = (int16_t)__SSAT((int32_t)tz, 16);   /* R_OTA I_abc tanh(v/2Vt), scaled, toned */
```

**ADD** after l. 439 (`	dl->vhpLast = vhpL; dl->vcaLast = vcaL; dl->iAbcLast = iaL;`).

```c
	dl->toneZ = tz;   /* S094 P1 S4: the tone state for the next block */
```

`DiodeLadder_renderB()` and `DiodeLadder_renderA()` (l. 501 onward) are **not
changed**. They are bench-only kernels (the voice calls B2R only;
`--gc-sections` drops them in firmware), and AL-AB compares them with B2R at
the neutral settings, where B2R is unchanged.

### S4.4 `Core/DSP/Instruments/Alkaline/AlkalineVoice.h`

**ADD** after l. 130 (`	uint8_t slide; ...`). As implemented (S094 P1 ledger, S4): the two controls are bytes, filling padding after `slide`, so the async indexes the tables with no clamp or float conversion.

```c
	/*
	 * Post-filter controls (S094 P1 S4).
	 *
	 * What:       vcaDrive: the VCA overdrive byte 0..127 (row
	 *             instrument_drive, the mix page's drv cell; stored by
	 *             IM_SPECIAL_INSTRUMENT_DRIVE's Alkaline branch): the index of
	 *             the OTA input gain x1..x16 and its make-up (alkOvdGain[],
	 *             alkOvdMakeup[]). tone: the tone byte 0..127 (row alk_tone, the
	 *             mix page's ton cell; generic TYPE_UINT8 store): the index of
	 *             the one-pole's c (alkToneC[]), 127 = open.
	 * Why:        P9-A' (post-FX draft §3.4): the shared voice-drive key and
	 *             cell of the other four types (K1), and a tone that rolls off
	 *             the overdrive's highs. Bytes, not 0..1 floats: the tables are
	 *             indexed by the byte, so the async reads it directly (no clamp
	 *             and float-to-index conversion per block), and the two bytes
	 *             fill padding after slide.
	 * Inputs:     descriptor writes (menu, apply, Morph, automation, LFO and
	 *             velocity targets, all in the 0..127 descriptor domain).
	 * Outputs:    read by Alkaline_calcVoiceAsync() once per block.
	 */
	uint8_t vcaDrive;
	uint8_t tone;
```

**MODIFY** l. 163–164 (the `driveGain` / `makeup` comments).

```c
	float   driveGain;         /* 16^drive, this block (S094 P1 A3: alkDriveGain[]) */
	float   makeup;            /* the OTA current's make-up: alkDriveMakeup[drive] x alkOvdMakeup[vcaDrive] (S094 P1) */
```

### S4.5 `Core/DSP/Instruments/Alkaline/AlkalineVoice.c`

**ADD** after l. 8 (`#include "AlkalineTables.h"`).

```c
#include "AlkalinePostTables.h"   /* S094 P1 S4: overdrive, tone, A3 drive */
```

**ADD** after l. 227 (`	voice->attack = 0.0f; ...` in `Alkaline_initVoice()`).

```c
	/* S094 P1 S4: init.alk instrument_drive 0 (no overdrive) and alk_tone
	 * 127 (open): the neutral, bit-identical settings */
	voice->vcaDrive = 0u;
	voice->tone = 127u;
```

**MODIFY** l. 425–436 (the drive block in `Alkaline_calcVoiceAsync()`).

```c
	/*
	 * Drive, overdrive and tone for this block (S093 P6 drive x1..x16;
	 * S094 P1 S4: A3 and the post-filter controls).
	 *
	 * What:       each knob (0..1) becomes a byte index 0..127 (x 127 + 0.5,
	 *             rounded down: a descriptor value k / 127 gives k exactly; a
	 *             modulated value rounds to the nearest byte, about 0.19 dB of
	 *             drive per step):
	 *               di (alk_drive): driveGain = alkDriveGain[di] (x1..x16, the
	 *                  input pair), the drive make-up alkDriveMakeup[di];
	 *               oi (instrument_drive, a byte 0..127 already): the OTA
	 *                  input gain alkOvdGain[oi] and its make-up
	 *                  alkOvdMakeup[oi], multiplied into the OTA current's
	 *                  make-up;
	 *               ti (alk_tone, a byte 0..127 already): the one-pole's c,
	 *                  alkToneC[ti].
	 *             The two post-filter bytes are masked to 0..127 (one AND
	 *             each), so no write path can index past a table.
	 * Why:        A3 (post-FX §5): two table reads replace two alk_exp2f()
	 *             calls (-40 per block, measured); bit-identical at drive 0
	 *             (both entries are the exact f = 0 evaluation). The overdrive
	 *             and tone are per-block constants of the loop (P9-A').
	 *             alkOvdMakeup[0] = 1.0f keeps the make-up exact at Overdrive 0.
	 * Inputs:     voice->drive, vcaDrive, tone.
	 * Outputs:    voice->driveGain, voice->makeup; ladder otaIn, toneC
	 *             (DiodeLadder_setPost()).
	 * Cost:       per block only; no VDIV.
	 */
	{
		const uint32_t di = (uint32_t)(fminf(fmaxf(voice->drive, 0.0f), 1.0f) * 127.0f + 0.5f);
		const uint32_t oi = (uint32_t)voice->vcaDrive & 127u;
		const uint32_t ti = (uint32_t)voice->tone & 127u;

		voice->driveGain = alkDriveGain[di];
		voice->makeup = alkDriveMakeup[di] * alkOvdMakeup[oi];
		DiodeLadder_setPost(&voice->ladder, alkOvdGain[oi], alkToneC[ti]);
	}
```

The render calls are unchanged: they already pass `voice->driveGain` and
`voice->iAbcEnd * voice->makeup` (l. 487–488, 576–577, 585–586).

### S4.6 `Core/DSP/Instruments/Alkaline/AlkalineParameters.h`

**MODIFY** l. 23.

```c
#define ALKALINE_PARAM_DESCRIPTOR_COUNT 31u   /* S094 P1 S4: + instrument_drive (29), alk_tone (30) */
```

### S4.7 `Core/DSP/Instruments/Alkaline/AlkalineParameters.c`

**ADD** after l. 133 (`    ALK_PARAM_OSC1_TYPE,         /* 28 ...`).

```c
    /*
     * S094 P1 S4: appended rows (29, 30); every stored cell, automation ID
     * and NRPN number of rows 0..28 keeps its meaning.
     */
    ALK_PARAM_VCA_DRIVE,         /* 29: instrument_drive, the VCA overdrive */
    ALK_PARAM_TONE,              /* 30: alk_tone, the post-filter tone */
```

**ADD** after l. 242 (the `osc1_type` row), before `};`.

```c
    /*
     * S094 P1 S4: the VCA overdrive (K1, user P9-D9).
     *
     * What:       the shared instrument_drive key and labels of the other four
     *             types ("Voice" / "Overdriv" / drv) on the mix page's fourth
     *             cell; the byte 0..127 -> vcaDrive through
     *             IM_SPECIAL_INSTRUMENT_DRIVE's Alkaline branch
     *             (InstrumentManager.c); the voice turns it into the OTA input
     *             gain x1..x16 and its make-up (alkOvdGain[], alkOvdMakeup[]).
     * Why:        the same name in the LFO/velocity target lists, the NRPN
     *             chart's voice-drive group and the Learn pattern as the other
     *             types; the filter page's alk_drive keeps its labels (user:
     *             category "Filter" makes it clear).
     */
    ROW_SPECIAL("instrument_drive", "Voice", "Overdriv", "drv", DTYPE_0B127, MOD_0_127, vcaDrive, TYPE_UINT8, IM_SPECIAL_INSTRUMENT_DRIVE),
    /*
     * S094 P1 S4: the tone (post-FX draft §3.4.1).
     *
     * What:       the mix page's third cell (the other types' srt cell; the
     *             Alkaline uses no SRR, so the cell is free, not the row);
     *             the byte 0..127 -> tone (generic TYPE_UINT8 store), the one-pole's
     *             c from alkToneC[]: 127 = open (exact), lower values roll off
     *             toward about 800 Hz. Default 127.
     */
    ROW("alk_tone", "Voice", "Tone", "ton", DTYPE_0B127, MOD_0_127, tone, TYPE_UINT8),
```

**MODIFY** l. 287–288 (page 7, MIX).

```c
    /* 7 MIX: vol pan ton drv (S094 P1 S4: the cells line up with the other
     * four types' vol pan srt drv; ton takes the srt cell) */
    {{ ALK_PARAM_INSTRUMENT_VOL, ALK_PARAM_INSTRUMENT_PAN, ALK_PARAM_TONE, ALK_PARAM_VCA_DRIVE, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY }},
```

The `alk_drive` row (l. 190) is **not changed** (user, 2026-11-11).

### S4.8 `Core/DSP/Instruments/InstrumentManager.c` — the drive writer

**MODIFY** l. 3853–3857 (`case IM_SPECIAL_INSTRUMENT_DRIVE:`).

```c
    case IM_SPECIAL_INSTRUMENT_DRIVE: {
        /*
         * S094 P1 S4 (K1): an Alkaline slot's instrument_drive is the VCA
         * overdrive, kept as the byte 0..127 (the index of the voice's
         * overdrive tables, read per block by the async). The
         * Alkaline has no Distortion, so without this branch the lookup below
         * returned 0 and the write fell through to the generic offset store
         * by accident; the branch makes the meaning explicit.
         */
        if (instrumentManager_slotType(slot) == INSTRUMENT_TYPE_ALK) {
            AlkalineVoice *alk = instrumentManager_alkalineRuntime(slot);

            if (!alk) return 0u;
            alk->vcaDrive = (byteValue > 127u) ? 127u : byteValue;
            return 1u;
        }
        Distortion *distortion = instrumentManager_distortion(slot);
        if (!distortion) return 0u;
        setDistortionShape(distortion, byteValue);
        return 1u; }
```

### S4.9 `tools/alkaline/make_init_alk.py` and `init.alk`

**MODIFY** l. 26–29 (the end of `INIT`).

```python
    # S093 P8 (appended row): typ wav (Saw/Sqr); osc1_wave (row 0, was
    # alk_wave) is an image row since P8, so it is under [morph] too
    "osc1_type": "0",
    # S094 P1 S4 (appended rows): no VCA overdrive, the tone open: the
    # neutral settings (bit-identical to the S093 voice)
    "instrument_drive": "0", "alk_tone": "127",
}
```

Then run `python3 tools/alkaline/make_init_alk.py` (no `--check`). This
rewrites `SD_CARD/Instrument/Alkaline/init.alk`: `instrument_drive=0` and
`alk_tone=127` appear at the end of `[params]` and of `[morph]`.

### S4.10 `tools/dsp_test/check_special_tags.py`

No logic change: `instrument_drive` already classifies as
`IM_SPECIAL_INSTRUMENT_DRIVE` for every type, and the Alkaline row carries
that tag. **MODIFY** the comment at l. 26–29 (the `'ALK'` entry).

```python
    # S093 P4: the coarse row classifies as PITCH_COARSE | OSC1 (the osc1_
    # branch is type-independent here and in the firmware classifier);
    # every alk_* key and the platform rows classify as NONE or as their
    # Drum twins. S094 P1 S4: instrument_drive (the VCA overdrive) is the
    # Drum twin's INSTRUMENT_DRIVE; alk_tone is NONE.
```

### S4.11 Host tests

**`tools/dsp_test/Makefile`, `alkaline` target.**

**ADD** after l. 325 (the `AlkalineTables.c` extraction line):

```make
	$(EX) --src $(ALK)/AlkalinePostTables.h --body --out $(GEN)/AlkalinePostTables.host.h
	$(EX) --src $(ALK)/AlkalinePostTables.c --body --out $(GEN)/AlkalinePostTables.host.c
```

**ADD** after the `break_alk_glide` block (after l. 368): the P9-D7 breaks.

```make
# S094 P1 S4 breaks for P9-D7 (AL-OD0 must fail): (1) the tone not open at
# 127 (c + 0.001); (2) the overdrive gain not exactly 1 at 0 (x 1.0001).
	mkdir -p build/break_alk_tone build/break_alk_ovd
	sed 's/alkToneC\[ti\])/alkToneC[ti] + 0.001f)/' \
	  $(GEN)/AlkalineVoice.host.c > build/break_alk_tone/AlkalineVoice.host.c
	@grep -q 'alkToneC\[ti\] + 0.001f' build/break_alk_tone/AlkalineVoice.host.c || \
	  { echo "alkaline_break_tone: sed did not apply"; exit 1; }
	$(HOSTCC) -Ibuild/break_alk_tone $(HOSTFLAGS) $(OSC_HOST_INC) modules/alkaline.c \
	  $(CAT_HOST_SRC) -o build/alkaline_break_tone -lm
	@if ./build/alkaline_break_tone >/dev/null 2>&1; then \
	  echo "alkaline_break_tone: FAILED (a closed tone at 127 not caught)"; exit 1; \
	else echo "alkaline_break_tone: caught (law AL-OD0 fails as expected)"; fi
	sed 's/alkOvdGain\[oi\],/alkOvdGain[oi] * 1.0001f,/' \
	  $(GEN)/AlkalineVoice.host.c > build/break_alk_ovd/AlkalineVoice.host.c
	@grep -q 'alkOvdGain\[oi\] \* 1.0001f' build/break_alk_ovd/AlkalineVoice.host.c || \
	  { echo "alkaline_break_ovd: sed did not apply"; exit 1; }
	$(HOSTCC) -Ibuild/break_alk_ovd $(HOSTFLAGS) $(OSC_HOST_INC) modules/alkaline.c \
	  $(CAT_HOST_SRC) -o build/alkaline_break_ovd -lm
	@if ./build/alkaline_break_ovd >/dev/null 2>&1; then \
	  echo "alkaline_break_ovd: FAILED (an overdrive gain off 1 at 0 not caught)"; exit 1; \
	else echo "alkaline_break_ovd: caught (law AL-OD0 fails as expected)"; fi
```

**ADD** an `alk_post_tables` target after `alkaline_tables` (after l. 371),
and add it to `.PHONY` and `all`.

```make
alk_post_tables:
	cd $(ROOT) && python3 tools/alkaline/gen_alk_post_tables.py --check
```

**`tools/dsp_test/modules/alkaline.c`.**

**ADD** after the `#include "AlkalineTables.host.h"` line (l. 137) and after
the `#include "AlkalineTables.host.c"` line (l. 139).

```c
#include "AlkalinePostTables.host.h"   /* S094 P1 S4 */
```

```c
#include "AlkalinePostTables.host.c"   /* S094 P1 S4: the real generated tables */
```

**ADD** the S4 laws before `int main` (anchor `int main(int argc, char **argv)`),
and in `main()` after `test_golden();`:

```c
	test_a3();
	test_overdrive();
	test_tone();
```

```c
/* ---------------------------------------------------------------- AL-A3
 * S094 P1 S4: the A3 tables against the runtime formula.
 *   alkDriveGain[k] / alkDriveMakeup[k] against (alk_exp2f(k/127)^2)^2 and
 *   alk_exp2f(1 - k/127) evaluated here (the host compiler's contraction):
 *   entry 0 exactly; every entry within 1 ulp. */
static int ulps(float a, float b)
{
	int32_t ia, ib;

	memcpy(&ia, &a, 4);
	memcpy(&ib, &b, 4);
	return abs(ia - ib);
}

static void test_a3(void)
{
	int worst = 0;

	for (unsigned k = 0u; k < ALK_POST_POINTS; k++) {
		const float drv = (float)k / 127.0f;
		const float t = alk_exp2f(drv), t2 = t * t;
		const float g = t2 * t2, m = alk_exp2f(ALK_MAKEUP_OCT * (1.0f - drv));
		const int e = ulps(g, alkDriveGain[k]) > ulps(m, alkDriveMakeup[k])
		            ? ulps(g, alkDriveGain[k]) : ulps(m, alkDriveMakeup[k]);

		if (e > worst) worst = e;
		if (k == 0u && e != 0) fail(G_A3, "drive 0 exact", e, 0);
		if (e > 1) fail(G_A3, "entry within 1 ulp", k, e);
	}
	printf("AL-A3: largest difference %d ulp\n", worst);
}

/* ---------------------------------------------------------------- AL-OV
 * S094 P1 S4: the VCA overdrive. The pattern at overdrive bytes 0, 64, 127
 * (drive 0, tone open): every sample finite and inside int16 (the store
 * saturates); the crest factor (peak / RMS) falls as the overdrive rises (the
 * OTA's tanh flattens the peaks). */
static double ov_crest(uint8_t ovd, double *rms)
{
	static int16_t buf[GOLD_PAT_BLOCKS * BLK];
	AlkalineVoice v;
	double s = 0.0;
	int pk = 0;

	gold_voice(&v, ALK_WAVE_SAW, 0u);
	v.vcaDrive = ovd;
	gold_pattern(&v, buf);
	for (size_t i = 0; i < (size_t)GOLD_PAT_BLOCKS * BLK; i++) {
		s += (double)buf[i] * buf[i];
		if (abs(buf[i]) > pk) pk = abs(buf[i]);
	}
	*rms = sqrt(s / ((double)GOLD_PAT_BLOCKS * BLK));
	return (*rms > 0.0) ? pk / *rms : 0.0;
}

static void test_overdrive(void)
{
	double r0, r64, r127;
	const double c0 = ov_crest(0u, &r0), c64 = ov_crest(64u, &r64), c127 = ov_crest(127u, &r127);

	printf("AL-OV: crest %.2f / %.2f / %.2f, RMS %.0f / %.0f / %.0f LSB at overdrive 0 / 64 / 127\n",
	       c0, c64, c127, r0, r64, r127);
	if (!(c127 < c0)) fail(G_OV, "the overdrive flattens the peaks", c0, c127);
	if (!(r0 > 0.0 && r64 > 0.0 && r127 > 0.0)) fail(G_OV, "silent", r64, r127);
}

/* ---------------------------------------------------------------- AL-TN
 * S094 P1 S4: the tone.
 *   table: alkToneC[127] == 0; decreasing; for k <= 63 (design fc up to
 *   4 kHz) the one-pole's -3 dB point, fc = acos(1 - (1 - c)^2 / (2c)) x
 *   fs / 2 pi (exact for y = (1 - c) x + c y), within 10 % of the design
 *   800 x 25^(k/127) Hz (computed: at most 2.7 %). Above 4 kHz
 *   c = exp(-2 pi fc / fs) drifts above the design toward Nyquist (+25 % at
 *   k = 100), which is inaudible as a tone setting and not checked;
 *   render (measured S094 P1 S4): tone 0 lowers the pattern's high band
 *   (the RMS of the second difference, a 12 dB/octave weighting) by at least
 *   3 dB at overdrive 0, 63 and 127 (measured 5.4 / 5.4 / 7.5 dB), and by more
 *   at 127 than at 0 (the tone rolls off the overdrive's added highs). The
 *   pattern's high band sits at the ladder's resonance peak (about 1-2 kHz,
 *   where an 800 Hz one-pole takes 5-8 dB); above it the 24 dB/octave ladder
 *   has already removed most of the energy. */
static double tn_hiRms(uint8_t tone, uint8_t ovd)
{
	static int16_t buf[GOLD_PAT_BLOCKS * BLK];
	AlkalineVoice v;
	double s = 0.0;

	gold_voice(&v, ALK_WAVE_SAW, 0u);
	v.tone = tone;
	v.vcaDrive = ovd;
	gold_pattern(&v, buf);
	for (size_t i = 2; i < (size_t)GOLD_PAT_BLOCKS * BLK; i++) {
		const double d = (double)buf[i] - 2.0 * buf[i - 1] + buf[i - 2];

		s += d * d;
	}
	return sqrt(s / ((double)GOLD_PAT_BLOCKS * BLK));
}

static void test_tone(void)
{
	if (alkToneC[127] != 0.0f) fail(G_TN, "open at 127", alkToneC[127], 0);
	for (unsigned k = 0u; k + 1u < ALK_POST_POINTS; k++) {
		if (!(alkToneC[k + 1u] < alkToneC[k])) fail(G_TN, "decreasing", k, alkToneC[k]);
		if (k <= 63u) {
			const double c = alkToneC[k];
			const double fc = acos(1.0 - (1.0 - c) * (1.0 - c) / (2.0 * c)) * 44108.0 / (2.0 * M_PI);
			const double want = 800.0 * pow(25.0, k / 127.0);

			if (fabs(fc / want - 1.0) > 0.10) fail(G_TN, "-3 dB point", k, fc);
		}
	}
	{
		double cut[3];

		for (unsigned o = 0u; o < 3u; o++) {
			/* overdrive bytes 0, 63, 127 */
			const uint8_t ovd = (uint8_t)(o * 63u + (o == 2u));
			const double open = tn_hiRms(127u, ovd), dark = tn_hiRms(0u, ovd);

			cut[o] = 20.0 * log10(dark / open);
			printf("AL-TN: overdrive %u, high band at tone 0 %+.1f dB against tone 127\n",
			       ovd, cut[o]);
			if (cut[o] > -3.0) fail(G_TN, "tone 0 rolls off the highs", ovd, cut[o]);
		}
		/* the tone's purpose: it takes more off the overdriven voice */
		if (!(cut[2] < cut[0])) fail(G_TN, "the cut grows with overdrive", cut[0], cut[2]);
	}
}
```

`test_a3()` uses the bench's existing `alk_exp2f` (from
`AlkalineControl.host.h`) and `ALK_MAKEUP_OCT`. `ov_crest()` and `tn_hiRms()`
reuse `gold_voice()` / `gold_pattern()` (S0.2). Their declarations come
before these laws in the file.

### S4.12 The P9-D4 starting curves

| Control | Curve | Range |
|---|---|---|
| Overdrive gain | `2^(4k/127)` | ×1 … ×16 |
| Overdrive make-up | `2^(−2k/127)` | 0 … −12 dB |
| Tone | 800 Hz … 20 kHz, exponential | 127 open |

They are set by listening on the device (plan §10). A retune is an edit of the
generator's constants plus a regeneration: no code edit.

### S4.13 Gates for S4

1. **Generator mirror:** in the S0 image, disassemble
   `Alkaline_calcVoiceAsync`'s inlined `alk_exp2f` and confirm the Horner
   steps are `vfma` (five fused steps). If they are not fused, change
   `fma32()` to a separate multiply and add (both roundings) and regenerate.
   AL-A3 confirms the mirror either way (≤ 1 ulp).
2. **Host tests:**
   - `make -C tools/dsp_test alkaline alk_post_tables special_tags`;
   - AL-OD0 0 LSB on every drive-0 run and ≤ 2 LSB at 64/127;
   - the two new breaks caught;
   - AL-A3, AL-OV and AL-TN pass;
   - every older law unchanged;
   - `python3 tools/alkaline/check_c.py c2` (the pattern against the
     validated reference) passes.
3. **Loops:**
   - `elf_looppath.py` on `DiodeLadder_renderB2Osc` and `…Osc2`, against
     S0's figures: the post-FX spike measured 213 → 219 and 228 → 234;
   - `control_cost.py --elf`: per block ≈ 1,361 expected.
   - **Whole voice without a slot ≈ 261.5 (P9-D8) and with a slot ≈ 276.5
     (P9-D2).** A result materially higher (more than +1 on either) is
     reported to the user before the step is accepted.
4. **Other loops:** `elf_p4_check P4BASE=baselines/s094p1/s3`: 0 MISSING,
   mixer 0 GREW.
5. **Budget:** `link_budget.py`: flash +≈2.6 KB; `sizeof(AlkalineVoice)`
   (`nm -S` or a temporary `_Static_assert`) ≤ 1,264 (S093 P8 548 → 568).
6. **Snapshot:** `baselines/s094p1/s4`.

---

## S5 — PWM on the Alkaline

### S5.0 The design in one place (PWM draft §2.4, §5; plan §7)

**Type**
- `typ` 2 = `pwm` (`ALK_TYP_PWM`). The oscillator type becomes `OSC_TYPE_PWM`,
  so the shared wave-cell helpers see the same type as the drums.

**Wave and width**
- `wav` keeps its Alkaline list:
  - `Saw` → the two-saw sum (sign +1);
  - `Sqr` → the pulse (sign −1).
- Both read the `sawTable` row of the octave (`osc_setTableFreq()`).
- `pos` is the width `p` (1..127), slewed ≤ 2 per block in the async. A
  non-legato note snaps it; the phase runs free (the 303's VCO).

**Ladder**
- Fused mode 3 in `dl_renderB2Body()`, wrapper `DiodeLadder_renderB2OscPwm()`
  (noinline, flash).
- The A read is fused mode 1's read exactly (float fraction, int16
  truncation).
- The B read is at `ph − p × 8` points, chosen by `ALK_PWM_READ_FORM`:

  | Form | B read | Use |
  |---:|---|---|
  | 0 | interpolated | the reference only |
  | 1 | truncated (default, the 212 form) | |
  | 2 | A truncated as well (204) | |

- The ladder input is `(A + sign × B) × scale`, with `scale = level ×
  (Sqr ? 1.40 × saw scale : saw scale) × driveGain`. The 1.40 is plan
  finding 3; `level` is `oscPwmLevelQ14[m] / 16384`.
- The post-filter stages (S4) apply as in every mode.
- **No interpolation slot** is ever claimed.

### S5.1 `Core/DSPAudio/DiodeLadder.c`

**ADD** after l. 86 (the `ALK_RES_NEAREST` block's `#endif`).

```c
/*
 * The Alkaline pwm read form (S094 P1 S5, P-D6).
 *
 * What:       how fused mode 3 reads its second (B) point:
 *               0  A and B both interpolated (the reference: the bench's
 *                  AL-PW error law builds it; never shipped);
 *               1  A interpolated as fused mode 1, B truncated at the same
 *                  point offset (the PWM draft's 212 form);
 *               2  A and B both truncated (the 204 form; the default, see
 *                  the S5 measurement below).
 * Why:        on a ramp both reads share one fraction, so a truncated B
 *             quantises only the position of B's band-limited edge to
 *             1/1,024 of a cycle; the ladder's low-pass hides it (AL-PW
 *             measures the error against form 0).
 * Inputs:     -DALK_PWM_READ_FORM (bench and measurement builds).
 * Accessors:  dl_renderB2Body() fused mode 3.
 */
/*
 * S094 P1 S5 measurement (S5.8, linked on the S4 body): the pwm loop is
 * 236 (form 0), 227 (form 1) and 215 (form 2); with the per block (about
 * 41.6 per sample) only form 2 stays under S4's Alkaline figure (263.7), so
 * form 2 is the default. Against form 0 both cheaper forms differ by about
 * -29 dB RMS sample for sample (AL-PW prints it): B's band-limited edge read
 * at a whole table point, a sub-sample time shift of a steep edge.
 */
#ifndef ALK_PWM_READ_FORM
#define ALK_PWM_READ_FORM 2
#endif
```

**MODIFY** l. 273–287 (`dl_renderB2Body()`'s signature and its `fused`
comment).

```c
static inline __attribute__((always_inline)) void dl_renderB2Body(
	DiodeLadder *dl, int16_t *buf, const float sScale, const float iLadEnd,
	const float iAbcEnd, const int fused, const int16_t *const row,
	uint32_t *const phase, const uint32_t phaseInc,
	const int16_t *const rowB, const float ff0, const float ffStep,
	const uint32_t pwD, const float pwSign)
{
	/* fused (a compile-time constant in each wrapper):
	 *   0  buf is the input (the unfused B2R, DiodeLadder_renderB2());
	 *   1  the oscillator's table read fused into the loop,
	 *      calcWavetableOscBlock()'s arithmetic exactly (gain 1: linear
	 *      interpolation, truncation to int16);
	 *   2  S093 P8 revision 3: two wavetable rows (row, rowB) read at the
	 *      same phase and mixed by ff (DiodeLadder_renderB2Osc2());
	 *   3  S094 P1 S5: pwm, two reads of one saw row pwD points apart,
	 *      summed (pwSign +1) or differenced (-1)
	 *      (DiodeLadder_renderB2OscPwm()).
	 * rowB, ff0 and ffStep are read only under 2, pwD and pwSign only under
	 * 3; the other wrappers pass 0 / 0.0f and the compiler drops them with
	 * the branch. */
```

**ADD** before l. 347 (`		} else if (fused) {`), inside the loop's input
selection (anchor: the line `		} else if (fused) {`).

```c
		} else if (fused == 3) {
			/*
			 * S094 P1 S5: the Alkaline pwm read.
			 *
			 * What:       A = row at the phase (fused mode 1's read and
			 *             truncation exactly); B = row pwD points earlier
			 *             (ph - w, the row wraps at 1,024), truncated
			 *             (ALK_PWM_READ_FORM 1), interpolated (0) or with A
			 *             truncated too (2); s = (A + pwSign x B) x sScale.
			 *             pwSign -1 gives the band-limited pulse (high for the
			 *             first w of the cycle), +1 the two-saw sum.
			 * Why:        PWM draft §2.4: inside the fused loop the second
			 *             read costs about 6 per sample with interpolation and
			 *             -1 without (measured 219 / 212 / 204 standalone on
			 *             the S093 P8 body); the form is chosen by the S5
			 *             linked measurement on the S4 body.
			 */
			const uint32_t itg = ph >> 22;
			const uint32_t ib = (itg - pwD) & 0x000003FFu;
#if ALK_PWM_READ_FORM == 2
			const int16_t o = row[itg];
			const float b = (float)row[ib];
#else
			int16_t o = row[itg];

			o += (ph & 0x003FFFFFu) * 2.384185791015625e-07f
			   * (row[(itg + 1u) & 0x000003FFu] - o);
#if ALK_PWM_READ_FORM == 0
			int16_t ob = row[ib];

			ob += (ph & 0x003FFFFFu) * 2.384185791015625e-07f
			    * (row[(ib + 1u) & 0x000003FFu] - ob);
			const float b = (float)ob;
#else
			const float b = (float)row[ib];
#endif
#endif
			ph += phaseInc;
			s = ((float)o + pwSign * b) * sScale;
```

**MODIFY** l. 449, l. 458–459 and l. 495–496 (the three existing wrappers'
calls): append `, 0u, 0.0f` to each argument list.

```c
	dl_renderB2Body(dl, buf, sScale, iLadEnd, iAbcEnd, 0, 0, 0, 0u, 0, 0.0f, 0.0f, 0u, 0.0f);
```
```c
	dl_renderB2Body(dl, buf, sScale, iLadEnd, iAbcEnd, 1, row, phase, phaseInc,
	                0, 0.0f, 0.0f, 0u, 0.0f);
```
```c
	dl_renderB2Body(dl, buf, sScale, iLadEnd, iAbcEnd, 2, rowA, phase, phaseInc,
	                rowB, ff0, ffStep, 0u, 0.0f);
```

**ADD** after l. 497 (the closing brace of `DiodeLadder_renderB2Osc2()`).

```c
/*
 * B2R with the pwm read fused (S094 P1 S5).
 *
 * What:       the fused loop with the input s_j = (A_j + pwSign x B_j) x
 *             sScale, A_j the row at the phase (fused mode 1's read), B_j the
 *             row pwD points earlier (ALK_PWM_READ_FORM); the phase advances
 *             by phaseInc per sample and is written back.
 * Why:        the Alkaline's pwm type (PWM draft §2.4): one more loop copy,
 *             like DiodeLadder_renderB2Osc2(); noinline keeps its registers
 *             out of the caller (the Osc2 measurement: 228 out of line
 *             against 231 inlined).
 * Inputs:     dl (the ladder; DiodeLadder_setNet() and DiodeLadder_setPost()
 *             ran this block), buf (output only), row (a 1,024-point sawTable
 *             row), phase (in/out), phaseInc, pwD (p x 8, 8..1,016 points),
 *             pwSign (+1 Saw sum, -1 Sqr pulse), sScale (the scale x level x
 *             drive), iLadEnd, iAbcEnd (as the other B2R wrappers).
 * Outputs:    buf (int16, the ladder's toned VCA output); *phase; the ladder
 *             state.
 * Accessors:  AlkalineVoice.c alk_renderPwm().
 * Affiliates: ALK_PWM_READ_FORM; control_cost.py ITERS (the audio loop is
 *             rank 1, as the other wrappers).
 */
__attribute__((noinline)) void DiodeLadder_renderB2OscPwm(DiodeLadder *dl, int16_t *buf,
                              const int16_t *row, uint32_t *phase,
                              const uint32_t phaseInc, const uint32_t pwD,
                              const float pwSign, const float sScale,
                              const float iLadEnd, const float iAbcEnd)
{
	dl_renderB2Body(dl, buf, sScale, iLadEnd, iAbcEnd, 3, row, phase, phaseInc,
	                0, 0.0f, 0.0f, pwD, pwSign);
}
```

### S5.2 `Core/DSPAudio/DiodeLadder.h`

**ADD** after l. 180 (the `DiodeLadder_renderB2Osc2()` prototype).

```c
/*
 * B2R with the pwm read fused (S094 P1 S5): two reads of one saw row pwD
 * points apart, s = (A + pwSign x B) x sScale. Contract in DiodeLadder.c.
 * Accessors: AlkalineVoice.c alk_renderPwm().
 */
void DiodeLadder_renderB2OscPwm(DiodeLadder *dl, int16_t *buf, const int16_t *row,
                                uint32_t *phase, uint32_t phaseInc, uint32_t pwD,
                                float pwSign, float sScale, float iLadEnd,
                                float iAbcEnd);
```

### S5.3 `Core/DSP/Instruments/Alkaline/AlkalineVoice.h`

**MODIFY** l. 62–81 (the `typ` byte).

```c
/*
 * The typ byte (row osc1_type, S093 P8; S094 P1 S5 pwm).
 *
 * What:       the stored and displayed value of the Alkaline typ cell:
 *             0 wav (Saw/Sqr, the 303's VCO), 1 wtb (a wavetable frame),
 *             2 pwm (S094 P1: Saw = two-saw sum, Sqr = pulse, pos = width);
 *             ALK_TYP_COUNT entries (alkTypeNames, MenuText.h).
 *             Alkaline_setOscType() maps it to OSC_TYPE_WAV / OSC_TYPE_WTB /
 *             OSC_TYPE_PWM in osc.oscType.
 * Why:        S093 P8: wavetables only. Looped samples are over the Basic
 *             ceiling on their loop alone (P8 assessment §6); one-shot samples
 *             fit (loop 215) but their render grew two mixer loops of other
 *             engines by 1 (P8 schedule §0), so they are left out (user,
 *             S093 P8). S094 P1: pwm appended (no stored value changes
 *             meaning except 2 and above, which were wtb and are now pwm; no
 *             migration). osc.oscType keeps the platform's values so the
 *             shared wave-cell helpers (instrumentManager_osc1Type(),
 *             osc_userCount(), menu_formatWaveCell()) need no Alkaline map.
 * Accessors:  Alkaline_setOscType(); AlkalineParameters.c osc1_type row.
 */
#define ALK_TYP_WAV           0u
#define ALK_TYP_WTB           1u
#define ALK_TYP_PWM           2u
#define ALK_TYP_COUNT         3u
```

**MODIFY** l. 218–221 (the `Alkaline_setOscType()` contract).

```c
 * What:       maps ALK_TYP_* to osc.oscType (0 wav, 1 wtb, 2 and above pwm);
 *             clamps osc.waveform to the new type's list (wav and pwm:
 *             Saw/Sqr; wtb: the installed tables); clears the bind so the
 *             next block (or trigger) binds; re-anchors positionApplied to
 *             pos. The phase runs on (every type is 2^32 per cycle). An
 *             unchanged type is a no-op.
```

### S5.4 `Core/DSP/Instruments/Alkaline/AlkalineVoice.c`

**ADD** after the S4 include (`#include "AlkalinePostTables.h"`).

```c
#include "oscPwmTables.h"         /* S094 P1 S5: the pwm level table */
```

**ADD** after l. 24 (`#define ALK_WTB_SCALE_V  ALK_SAW_SCALE_V`).

```c
/*
 * The pwm pulse's input scale (S094 P1 S5, plan finding 3).
 *
 * What:       volts per LSB of the two-read pulse under Sqr: the saw's scale
 *             x 1.40.
 * Why:        the 303 square (alkSqrTable, ALK_SQR_SCALE_V) is tilted and
 *             carries more RMS than a flat pulse of the same peak: measured
 *             on rows 3 and 5, square 3.74 / 3.86 V RMS against the saw
 *             difference's 2.66 / 2.69 V, ratio 1.41 / 1.43 (1.09 on row 1).
 *             1.40 makes pos 64 under pwm about as loud as Sqr under wav into
 *             the ladder. Saw under pwm keeps the saw's own scale (its sum at
 *             pos 64 is the octave-up saw at the same peak).
 * Accessors:  alk_renderPwm().
 */
#define ALK_PWM_SQR_SCALE_V  (ALK_SAW_SCALE_V * 1.40f)
```

**MODIFY** l. 296–301 (`Alkaline_triggerVoice()`: the typ branch at a
non-legato note).

```c
		/* S093 P8: typ wtb starts on pos's frame position (alk_wtbTrigger();
		 * revision 3: fractional, blended by the render); S094 P1 S5: typ pwm
		 * snaps the width to pos and leaves the phase free-running (the 303's
		 * VCO; pos is the width, not a start phase); Saw/Sqr keep the P6
		 * start-phase rule below */
		if (voice->osc.oscType == OSC_TYPE_WTB) {
			alk_wtbTrigger(voice);
		} else if (voice->osc.oscType == OSC_TYPE_PWM) {
			voice->osc.positionApplied = (uint8_t)(voice->osc.position & 127u);
		} else
```

**MODIFY** l. 371–373 and l. 407–409 (`Alkaline_calcVoiceAsync()`'s type
branch). Anchors: `if (voice->osc.oscType == OSC_TYPE_WAV) {` and the
`} else {` / `alk_wtbControl(voice);` pair that closes it.

```c
	/* S093 P8: Saw/Sqr (typ wav) keep the table law and the P6 pos bend;
	 * S094 P1 S5: pwm takes the table law and the width slew; typ wtb takes
	 * its own per-block control (alk_wtbControl()) */
	if (voice->osc.oscType == OSC_TYPE_WAV) {
```

```c
	} else if (voice->osc.oscType == OSC_TYPE_PWM) {
		/*
		 * S094 P1 S5: pwm control.
		 *
		 * What:       osc_setTableFreq() (phaseInc and the octave row, as
		 *             wav: the pwm reads are sawTable rows); then the width
		 *             slew: positionApplied moves toward pos by at most
		 *             OSC_PWM_SLEW_STEPS (2), linearly (the drums' rule,
		 *             osc_typedPositionApply()).
		 * Why:        a width jump is a waveform step at the block rate; the
		 *             slew keeps knob and LFO sweeps smooth (P-D7). No phase
		 *             bend: pos is not a phase under pwm.
		 * Cost:       per block: the table law (as wav) and about 6
		 *             instructions for the slew.
		 */
		osc_setTableFreq(&voice->osc);
		{
			int32_t d = (int32_t)(voice->osc.position & 127u) -
			            (int32_t)voice->osc.positionApplied;

			if (d > OSC_PWM_SLEW_STEPS)
				d = OSC_PWM_SLEW_STEPS;
			else if (d < -OSC_PWM_SLEW_STEPS)
				d = -OSC_PWM_SLEW_STEPS;
			voice->osc.positionApplied =
				(uint8_t)((int32_t)voice->osc.positionApplied + d);
		}
	} else {
		alk_wtbControl(voice);
	}
```

**ADD** after l. 587 (the closing brace of `alk_renderWtb()`).

```c
/*
 * One block of typ pwm (S094 P1 S5).
 *
 * What:       p = the applied width clamped to 1..127; the level
 *             64 / max(p, 128 - p) from oscPwmLevelQ14[] (the drums' D form:
 *             keeps the sum and the difference at the table's peak); the
 *             scale = (Sqr ? ALK_PWM_SQR_SCALE_V : ALK_SAW_SCALE_V) x level x
 *             the drive; then DiodeLadder_renderB2OscPwm() on the octave's
 *             sawTable row (osc.tableOffset, set by osc_setTableFreq() in the
 *             async) with the second read p x 8 points earlier, summed (Saw)
 *             or differenced (Sqr).
 * Why:        PWM draft §5: Sqr becomes the variable pulse, Saw the two-saw
 *             sum (P-D2); both from the band-limited saw rows, so every
 *             width is band-limited. noinline, as alk_renderWav() and
 *             alk_renderWtb(): each type's render keeps its registers to
 *             itself (the S093 P8 spike measured the shared form 2 dearer).
 * Inputs:     voice (the async ran this block: phaseInc, tableOffset,
 *             positionApplied, driveGain, makeup, the post-filter controls);
 *             buf (output).
 * Outputs:    buf; osc.phase; the ladder state.
 * Accessors:  Alkaline_calcVoiceSyncBlock().
 * Affiliates: DiodeLadder_renderB2OscPwm(); oscPwmLevelQ14 (oscPwmTables.h);
 *             bench law AL-PW.
 */
static __attribute__((noinline)) void alk_renderPwm(AlkalineVoice *voice, int16_t *buf)
{
	OscInfo *const osc = &voice->osc;
	/* the applied width, 1..127 (pos 0 acts as 1, P-D3) */
	uint32_t p = (uint32_t)(osc->positionApplied & 127u);

	if (p < 1u)
		p = 1u;
	{
		/* the level index: distance of the wider part from half, 0..63 */
		const uint32_t m = ((p > 64u) ? p : (128u - p)) - 64u;
		/* the level 0.5..1 (Q14 / 16384: a multiply, never a VDIV) */
		const float level = (float)oscPwmLevelQ14[m] * (1.0f / 16384.0f);
		const uint8_t square = (uint8_t)(osc->waveform == ALK_WAVE_SQUARE);

		DiodeLadder_renderB2OscPwm(&voice->ladder, buf, sawTable[osc->tableOffset],
		                           &osc->phase, osc->phaseInc, p << 3,
		                           square ? -1.0f : 1.0f,
		                           (square ? ALK_PWM_SQR_SCALE_V : ALK_SAW_SCALE_V)
		                               * level * voice->driveGain,
		                           voice->iLadEnd, voice->iAbcEnd * voice->makeup);
	}
}
```

**MODIFY** l. 592–605 (`Alkaline_calcVoiceSyncBlock()`'s dispatch).

```c
	/*
	 * S093 P8: the per-block type dispatch (S094 P1 S5: pwm added).
	 *
	 * What:       wtb -> alk_renderWtb() (the frame glide; a two-row blend
	 *             with a slot, the nearest frame without), pwm ->
	 *             alk_renderPwm(), anything else -> alk_renderWav() (Saw/Sqr).
	 * Why:        one branch per block, outside every loop; each type feeds
	 *             its own out-of-line table loop.
	 */
	if (voice->osc.oscType == OSC_TYPE_WTB)
		alk_renderWtb(voice, buf);
	else if (voice->osc.oscType == OSC_TYPE_PWM)
		alk_renderPwm(voice, buf);
	else
		alk_renderWav(voice, buf);
```

**MODIFY** l. 620–625 (`Alkaline_setOscType()`'s mapping and count).

```c
	/* the byte -> the platform type: 0 wav, 1 wtb, 2 and above pwm */
	const uint8_t type = (typ == ALK_TYP_WAV) ? OSC_TYPE_WAV
	                   : ((typ == ALK_TYP_WTB) ? OSC_TYPE_WTB : OSC_TYPE_PWM);
	/* the number of entries of the new type's wav list: Saw/Sqr under wav
	 * and pwm, the installed tables under wtb */
	const uint8_t count = (type == OSC_TYPE_WTB) ? sampleMemory_getNumWavetables()
	                    : (uint8_t)(ALK_WAVE_SQUARE + 1u);
	const uint8_t last = count ? (uint8_t)(count - 1u) : 0u;
```

The rest of the function is unchanged. Under pwm the bind byte
(`ALK_BIND_NONE`) is not read: `alk_wtbKeyChanged()` runs only under wtb.

### S5.5 `Core/Menu/MenuText.h` and `menu.c`

**MODIFY** `MenuText.h` l. 240–253 (`alkTypeNames`).

```c
/*
 * Alkaline type names (S093 P8; S094 P1 S5 pwm).
 *
 * What:       [0] = count (3), then wav, wtb, pwm: the Alkaline typ byte
 *             0..2 (ALK_TYP_WAV / ALK_TYP_WTB / ALK_TYP_PWM).
 * Why:        display, clamp and automation range of the typ cell come from
 *             this count (the six tagged sites in menu.c). The count must
 *             equal ALK_TYP_COUNT (asserted in menu.c).
 * Accessors:  menu_onoffTagNames() (menu.c).
 */
static const char alkTypeNames[][4] = {
    {3}, {"wav"},{"wtb"},{"pwm"},
};
```

**MODIFY** `menu.c` l. 12532–12534.

```c
/* S093 P8 / S094 P1: the Alkaline typ list is wav, wtb, pwm (ALK_TYP_COUNT,
 * AlkalineVoice.h) */
_Static_assert((sizeof(alkTypeNames) / sizeof(alkTypeNames[0])) - 1u == 3u,
               "alkTypeNames must list wav, wtb and pwm");
```

**MODIFY** `menu.c` l. 12180 (the Alkaline wave-name branch's condition).
Anchor: `if (isOsc1 && type == OSC_TYPE_WAV &&`.

```c
    /* S094 P1 S5: under pwm the Alkaline's wav list is still Saw, Sqr */
    if (isOsc1 && (type == OSC_TYPE_WAV || type == OSC_TYPE_PWM) &&
        instrumentManager_runtimeType(cell->slot) == INSTRUMENT_TYPE_ALK) {
```

### S5.6 `Core/DSP/Instruments/InstrumentManager.c` — `instrumentManager_waveDomainMax()`

**MODIFY** l. 848–853.

```c
            /* S093 P8: an Alkaline slot under wav has two entries (Saw,
             * Sqr): the LFO/velocity range, menu clamp, edit count and typ
             * switch clamp all take it from here. S094 P1 S5: pwm has the
             * same two (sum, pulse). */
            if (osc && (osc->oscType == OSC_TYPE_WAV || osc->oscType == OSC_TYPE_PWM) &&
                instrumentManager_slotType(slot) == INSTRUMENT_TYPE_ALK)
                return (uint8_t)ALK_WAVE_SQUARE;
```

### S5.7 Host tests

**`tools/dsp_test/modules/alkaline.c` MODIFY** l. 1240–1242 (AL-TY: 7 is no
longer wtb).

```c
	Alkaline_setOscType(&v, ALK_TYP_WTB);
	if (v.osc.oscType != OSC_TYPE_WTB || v.osc.sampleCacheLooped != ALK_BIND_WTB)
		fail(G_TY, "the same type is a no-op", v.osc.oscType, 0);
```

**ADD** at the end of `test_typ()` (before its closing brace).

```c
	/* S094 P1 S5: 2 and above are pwm; pwm clamps wav to Sqr; the phase runs
	 * on; the slot is dropped */
	v.osc.waveform = 9u;
	v.osc.phase = 0x0badf00du;
	Alkaline_setOscType(&v, 7u);
	if (v.osc.oscType != OSC_TYPE_PWM) fail(G_TY, "7 is pwm", v.osc.oscType, 0);
	if (v.osc.waveform != ALK_WAVE_SQUARE) fail(G_TY, "pwm clamps to Sqr", v.osc.waveform, 0);
	if (v.osc.phase != 0x0badf00du) fail(G_TY, "the phase runs on (-> pwm)", 0, 0);
```

**ADD** the AL-PW law and its render mode before `int main` (anchor
`int main(int argc, char **argv)`). In `main()`, add the dispatch after
`--golden`, and `test_pwm();` after `test_tone();`.

```c
	/* S094 P1 S5: write the pwm reference render (the form-0 build) */
	if (argc > 2 && !strcmp(argv[1], "--pwm-render"))
		return mode_pwmRender(argv[2]);
```

```c
/* ---------------------------------------------------------------- AL-PW
 * S094 P1 S5: the Alkaline pwm.
 *   - pos 1, 64, 127 under Saw and Sqr: finite, bounded (the pattern);
 *   - the width slew: positionApplied moves <= OSC_PWM_SLEW_STEPS per block
 *     toward pos and lands; a non-legato note snaps it; the phase runs on
 *     through the note (free-running VCO);
 *   - level: Sqr at pos 64 under pwm within 3 dB RMS of Sqr under wav (the
 *     1.40 scale; printed);
 *   - P-D6 error: this build's render (ALK_PWM_READ_FORM, default 2 after
 *     the S5 measurement) against the form-0 reference written by the second
 *     bench build (build/alk_pwm_ref.bin, Makefile): printed; it must stay
 *     at least 15 dB below the reference RMS (a gross-error guard: about
 *     -29 dB measured for forms 1 and 2). */
#define PW_PATH "build/alk_pwm_ref.bin"

static void pw_render(int16_t *out)
{
	AlkalineVoice v;

	/* Sqr at pos 32, then Saw at pos 96: one pattern each */
	gold_voice(&v, ALK_WAVE_SQUARE, 0u);
	Alkaline_setOscType(&v, ALK_TYP_PWM);
	v.osc.position = 32u;
	gold_pattern(&v, out);
	gold_voice(&v, ALK_WAVE_SAW, 0u);
	Alkaline_setOscType(&v, ALK_TYP_PWM);
	v.osc.position = 96u;
	gold_pattern(&v, out + (size_t)GOLD_PAT_BLOCKS * BLK);
}

static int mode_pwmRender(const char *path)
{
	int16_t *buf = malloc((size_t)2 * GOLD_PAT_BLOCKS * BLK * sizeof(int16_t));
	FILE *fo = fopen(path, "wb");

	pw_render(buf);
	if (!fo || fwrite(buf, sizeof(int16_t), (size_t)2 * GOLD_PAT_BLOCKS * BLK, fo)
	           != (size_t)2 * GOLD_PAT_BLOCKS * BLK)
		return 1;
	fclose(fo);
	free(buf);
	return 0;
}

/* the pattern's RMS for one wave (Saw/Sqr), typ and pos */
static double pw_rms(uint8_t wave, uint8_t typ, uint8_t pos)
{
	static int16_t buf[GOLD_PAT_BLOCKS * BLK];
	AlkalineVoice v;
	double s = 0.0;

	gold_voice(&v, wave, 0u);
	Alkaline_setOscType(&v, typ);
	v.osc.position = pos;
	gold_pattern(&v, buf);
	for (size_t i = 0; i < (size_t)GOLD_PAT_BLOCKS * BLK; i++)
		s += (double)buf[i] * buf[i];
	return sqrt(s / ((double)GOLD_PAT_BLOCKS * BLK));
}

static void test_pwm(void)
{
	/* bounds at the width extremes */
	{
		static const uint8_t pos[3] = { 1u, 64u, 127u };

		for (int w = 0; w < 2; w++)
			for (int p = 0; p < 3; p++) {
				const double r = pw_rms(w ? ALK_WAVE_SQUARE : ALK_WAVE_SAW,
				                        ALK_TYP_PWM, pos[p]);

				if (!isfinite(r) || r <= 0.0) fail(G_PW, "bounded and sounding", w, pos[p]);
			}
	}
	/* the slew and the snap */
	{
		AlkalineVoice v;
		int16_t buf[BLK];

		gold_voice(&v, ALK_WAVE_SAW, 0u);
		Alkaline_setOscType(&v, ALK_TYP_PWM);
		v.osc.position = 10u;
		Alkaline_triggerVoice(&v, 0u, 80u, 63u, ALK_STEP);
		if (v.osc.positionApplied != 10u) fail(G_PW, "a note snaps the width", v.osc.positionApplied, 10);
		v.osc.position = 100u;
		for (int b = 0; b < 60; b++) {
			const uint8_t before = v.osc.positionApplied;

			block(&v, buf, K_B2R);
			if (abs((int)v.osc.positionApplied - (int)before) > OSC_PWM_SLEW_STEPS)
				fail(G_PW, "slew step", b, v.osc.positionApplied);
		}
		if (v.osc.positionApplied != 100u) fail(G_PW, "the slew lands", v.osc.positionApplied, 100);
		{
			const uint32_t ph = v.osc.phase;

			Alkaline_triggerVoice(&v, 0u, 80u, 63u, ALK_STEP);
			if (v.osc.phase != ph) fail(G_PW, "the phase runs free through a note", 0, 0);
		}
	}
	/* level against Sqr under wav */
	{
		const double db = 20.0 * log10(pw_rms(ALK_WAVE_SQUARE, ALK_TYP_PWM, 64u) /
		                               pw_rms(ALK_WAVE_SQUARE, ALK_TYP_WAV, 0u));

		printf("AL-PW: Sqr pwm at 64 against Sqr wav %+.2f dB RMS\n", db);
		if (fabs(db) > 3.0) fail(G_PW, "pulse level", db, 3.0);
	}
	/* P-D6: against the both-interpolated reference */
	{
		const size_t n = (size_t)2 * GOLD_PAT_BLOCKS * BLK;
		int16_t *now = malloc(n * sizeof(int16_t)), *ref = malloc(n * sizeof(int16_t));
		FILE *fi = fopen(PW_PATH, "rb");

		if (fi && fread(ref, sizeof(int16_t), n, fi) == n) {
			double se = 0.0, sr = 0.0;

			pw_render(now);
			for (size_t i = 0; i < n; i++) {
				const double e = (double)now[i] - ref[i];

				se += e * e;
				sr += (double)ref[i] * ref[i];
			}
			{
				const double db = 10.0 * log10((se + 1e-30) / (sr + 1e-30));

				printf("AL-PW: read form %d against form 0: %.1f dB\n", ALK_PWM_READ_FORM, db);
				/*
				 * S094 P1 S5: a gross-error guard, not a quality bound. The
				 * cheaper forms measure about -29 dB sample for sample
				 * against form 0 (a steep band-limited edge read at a whole
				 * table point: a sub-sample time shift, which an RMS of the
				 * sample difference overstates); the quality call is the
				 * device listening (plan §10). A read past the row, a lost
				 * level or a wrong sign comes out near 0 dB.
				 */
				if (db > -15.0) fail(G_PW, "read-form error", db, -15.0);
			}
		} else {
			fail(G_PW, "reference render missing", 0, 0);
		}
		if (fi) fclose(fi);
		free(now); free(ref);
	}
}
```

**`tools/dsp_test/Makefile` ADD** in the `alkaline` recipe, before the main
bench's compile line (before l. 331, `$(HOSTCC) $(HOSTFLAGS) $(OSC_HOST_INC)
modules/alkaline.c ...`). The reference build has to run first, because
AL-PW reads its output.

```make
# S094 P1 S5: the pwm read-form reference (ALK_PWM_READ_FORM 0, both reads
# interpolated) renders build/alk_pwm_ref.bin for law AL-PW (P-D6 evidence).
	$(HOSTCC) $(HOSTFLAGS) -DALK_PWM_READ_FORM=0 $(OSC_HOST_INC) modules/alkaline.c \
	  $(CAT_HOST_SRC) -o build/alkaline_pwm_ref -lm
	./build/alkaline_pwm_ref --pwm-render build/alk_pwm_ref.bin
```

`build/alk_pwm_ref.bin` is a regenerated scratch output (made on every run),
so it belongs in `build/`. The golden capture, a record, does not
(S0.3).

### S5.8 Gates for S5 (and the P-D6 choice)

1. **Host tests:** `make -C tools/dsp_test alkaline oscillator`:
   - AL-TY and AL-PW pass;
   - AL-OD0 unchanged (the wav and wtb renders do not touch mode 3);
   - the S4 breaks are still caught.
2. **P-D6 measurement:** build the firmware once with each form:
   - `make all CFLAGS_EXTRA=-DALK_PWM_READ_FORM=1`, then `=2`; if the root
     Makefile has no `CFLAGS_EXTRA` hook, use a temporary local edit of the
     `#define` and revert it;
   - for each: `elf_looppath.py` on `DiodeLadder_renderB2OscPwm`, plus
     `control_cost.py --elf` with the pwm path.
3. **Whole voice:** the Alkaline under pwm (loop + per block / 32) **must not
   exceed S4's accepted no-slot figure** (≈ 261.5).
   - **Form 1 fits:** keep the default.
   - **Only form 2 fits:** set the default to 2 and re-run AL-PW.
   - **Neither fits:** report to the user; the pwm type is not shipped on the
     Alkaline.
4. **Other loops:** `elf_p4_check P4BASE=baselines/s094p1/s4`: 0 MISSING,
   mixer 0 GREW. The wrappers `…Osc` and `…Osc2` must equal S4's counts: the
   body's new parameters are constants in them.
5. **Snapshot:** `baselines/s094p1/s5`.

---

## S6 — Ceiling assessment (D-1, D-2)

No firmware change. Method: `tools/dsp_test/baselines/s093p4/drum_ceiling.txt`
(the P4 freeze), on the final image:

1. **Linked loops:** `elf_looppath.py --cond-heads --in-range` on every
   oscillator, filter, transient, fold, post-chain and Alkaline loop symbol.
2. **Mixer-inlined loops:** build the `-g` twin of the final tree
   (`make all` with `-g` added, as S093 P8's `liveg`). Attribute the inlined
   loops of `mixer_calcNextSampleBlock` by source line with `elf_loopsrc.py`.
3. **Per block:** `elf_blockpath.py` on each engine's async, sync-block entry,
   dispatch and the typed / pwm per-block paths. Entries that LTO inlines into
   the mixer are counted standalone (`-Ofast`, no LTO), as the P8 Advanced
   measurement did. The Alkaline uses `control_cost.py --elf`.
4. **VDIV:** counted separately for every path.
5. **For each engine, the dearest setting:**

   | Engine | Settings walked |
   |---|---|
   | Drum | `fm` / `mix` / `snc` / `rng` × carrier typ (wav, smp, slp, wtb ± slot, pwm D/WT/WS) × modulator (stock, s, l, w) × chain (T+D, WFd) |
   | Snare | tonal typ × chain |
   | Cymbal / HiHat | carrier typ × the two modulators × chain |
   | Alkaline | wav, wtb ± slot, pwm |

   Whole voice = loops + per block / 32 (+ SRR 10 where the chain uses it).
6. **Write:**
   - `tools/dsp_test/baselines/s094p1/basic_worst.txt`: Drum, Snare, Alkaline
     dearest, with their settings, loop list, per-block list and VDIV;
   - `tools/dsp_test/baselines/s094p1/advanced_ceiling.txt`: the Cymbal /
     HiHat dearest, the same detail (the first frozen Advanced ceiling;
     expected ≈ 286.9).
7. **Report to the user** before either figure is used as a gate:
   - the Basic worst case against 261.3 (the Alkaline's accepted ≈ 261.5 is
     the known exception);
   - the Advanced worst case, and how far it fell from ≈ 292.9.

---

## Appendix — files touched, by step

## Execution notes — 2026-11-11

This ledger records the work and gates as each schedule step is carried out.
Entries are updated only after the named step reaches its gate.

| Step | Status | Notes / gate evidence |
|---|---|---|
| S0 | Complete | Starting tree: `9c4f0260`, clean. `make all` and `make img` pass; flash 710,836 B, ITCM 12,128/16,384 B, DTCM statics 4,396 B. S0 loop, control-cost, and link-budget records plus the oscillator/mixer snapshot are in `tools/dsp_test/baselines/s094p1/s0/`. `alk_golden.bin` is 1,270,336 B (635,168 samples); `make -C tools/dsp_test alkaline` passes every law, including AL-OD0 (all nine maximum differences 0 LSB), and catches all three break builds. |
| S1 | Complete | Rate generator passes `--check`; LF1–LF5 pass and both break builds are caught. `special_tags` reports 200 rows / 0 mismatches; `make_init_alk.py --check` confirms `init.alk` unchanged. `make all && make img` passes (flash headroom 42,012 B; ITCM 12,128 B). Against S0, P4 has 0 missing loops, mixer has 0 grew loops; linked `lfo_dispatchNextValue` per-block path fell from 49 to 46 instructions. Snapshot saved as `s1/`. Device boot diagnostic remains on the final hardware checklist. |
| S2 | Complete with accepted S089 offset | The S2 image and host suite pass. `02_crash.wav` was generated and read back: 32,769 PCM16 frames, guard frame equals frame 0, `smpl` loop 0..32,767. The pre-removal scan found 69 affected card references (Cym carriers clamp to Noi; modulator 5 becomes s01; later user entries move down one). Final `make all` and `make img` pass; flash use falls 31,776 B (711,652 → 679,876) and ITCM falls 728 B (12,128 → 11,400), with DTCM unchanged at 4,396 B. `make -C tools/dsp_test all` passes. The S2 P4 snapshot is in `s2/`. On reconstructed S1 and S2 `-g` twins, `elf_loopsrc.py` confirms the crash loops are gone; two raw source-paired mixer groups rise by one instruction/sample (Cymbal `calcFmSineBlock` 28 → 29, HiHat 29 → 30). User approved applying S089's offset rule: crash removal saves ≈6/sample on the dearest Cymbal/HiHat slp path, ≈3 per itp crash-modulator slot, and ≈3 on Drum's dearest path; the whole-voice path accounting falls 261.3 → 258.3 (Drum) and ≈292.9 → ≈286.9 (Cymbal/HiHat). All other paired loop groups are equal or lower. S6 must remeasure these two paths in the final image. A bounded dispatch-shape probe (explicit former selector to zero-fill) kept flash at 679,876 B and did not change either count; removed. LTO leaves neither `calcFmSineBlock` nor `calcNextOscSampleFmBlock` as a standalone symbol. `calcNextOscSampleBlock` measures 24; the `slp` loop is 27 in `osc_slpPlainBlock`. The sample-clock test tolerance is one Q19.12 step (1,024 units). `make -C tools/dsp_test all` needed a float identity stand-in in `modules/run_modes.c`, because the voice-post test used `distortion_curveSampleF()` without one. `git diff --check` passes. |
| S3 | Complete with accepted S089 offset | **Review corrections (2026-11-11):** the unscheduled `noinline, noclone` on `busComp_processBlock()` (`BusCompressor.c/.h`) and the rewrite of the existing `mixer_decimateBlock()` loop (`mixer.c`) were reverted: both files are out of this pass, and the second edited an existing per-sample loop. The "missing" compressor symbol was the gate comparing against S2's layout: at S0 and S1 the compressor is inlined into the mixer (no standalone symbol in `s0/` or `s1/`), at S2 it was out of line, at S3 it is inlined again. Gated against **S0**: no loop lost beyond S2's accepted crash removals; S3 against S2 differs only in that compressor symbol. Mixer by source pairing (`-g` twins, `elf_loopsrc.py --compare`, S0 vs S3): `mixer_decimateBlock` (`mixer.c:177`) **10 -> 11**, every SRR voice. Bisected with scratch variants: the `osc_isTyped()` constant does not move it; removing the six `case OSC_BIND_PWM:` arms does (LTO inlines the S087 typed dispatchers into the engine renders in the mixer). Routing pwm through the dispatchers' `default:` arm instead removed the decimator rise but moved the FX-send function into the mixer and raised six of its loops by 1-4 (687/688/690/699, `satAdd32` 73 -> 74, 738 9 -> 13), so the scheduled explicit cases stay. **The decimator +1 is accepted under S089's offset rule (user, 2026-11-11):** it falls only on SRR voices (Drum, Snare, Cymbal, HiHat), whose crash-removal savings on the same paths exceed it (Drum dearest 257.8 with it, under the 261.3 gate; Advanced 285.9 with it, under the former ≈ 292.9). The Alkaline, the Basic worst case at 263.7, uses no SRR and is unaffected, so the worst-case Scene does not rise. The rejected default-arm routing would have raised the FX-send loops that every slot with an active send uses, the Alkaline included. Confirmed on the final image: `mixer_addVoiceInt16ToOutputAndFx()` is out of line at 0xd88 B as at S0, 27 -> 27 loops, 0 grew. The other source-paired change is new mixer-inlined copies of `calcSineBlock` (22) and `calcWavetableOscBlock` (23), at or below their out-of-line loops. PWM loops (linked): plain 24/24/24, FM 32/30/30, FM ramp 33/31/31, ring 35/33/33, sync 36/36/36 (D/WT/WS) against ceilings 27/50/51/42/41. Per block: `osc_pwmPlan` 39, roles 16-24. Host suite passes; breaks caught. The fifteen PWM loops were added to `P4_OSC_SYMS` (`tools/dsp_test/Makefile`). Flash 684,236 B; ITCM 13,984 B. Snapshot `s3/` with `loopsrc_mixer_vs_s0.txt`. |
| S4 | Complete; overrun for user decision | Generator `gen_alk_post_tables.py` (its fused Horner mirror confirmed against the S0 disassembly: five `vfma` and a `vmul` per `alk_exp2f`). AL-OD0 0 LSB on all nine runs, drive bytes 64/127 included (A3 entries exact); AL-A3 0 ulp; AL-OV crest 10.8 / 9.0 / 4.5 at overdrive 0/64/127; both new breaks caught; `check_c.py c2` unchanged (bit-identical at neutral). **Deviations from the schedule text (both updated in this document):** (1) `vcaDrive` and `tone` are bytes (0..127; `TYPE_UINT8`; the Alkaline drive writer stores the byte), not 0..1 floats: the per-block clamps and float-to-index conversions cost 11 per block; (2) AL-TN's render law was an unmeasured -6 dB on the first difference; measured with the second difference: -5.4 / -5.4 / -7.5 dB at overdrive 0/63/127 (the pattern's high band is the resonance peak at about 1-2 kHz); the law is now >= 3 dB at every overdrive and a larger cut at 127 than at 0. Loops: one-row 213 -> **221** (spike 219), blend 228 -> 234; per block 1,366 (S0 1,378). **Whole voice: no slot 263.7, slot 276.9**, against the accepted about 261.5 / 276.5: the no-slot figure is 2.2 over, beyond S4.13's +1, so it is held for the user. Cause: the three new live floats in the loop (function stack loads/stores 30 -> 41). Other loops 95/95 exact against S3; mixer 0 grew. Flash 686,988 B. `runtime_slots` unchanged (6 x 1,264 B). Snapshot `s4/`. |
| S5 | Complete (read form 2) | Implemented as scheduled. P-D6 by S5.8's rule, measured linked on the S4 body: pwm loop 236 (form 0), 227 (form 1), **215 (form 2)**; whole voice under pwm about 277.7 / 268.7 / **256.6**: only form 2 is under S4's figure, so `ALK_PWM_READ_FORM` defaults to 2. AL-PW: Sqr pwm at 64 is +1.14 dB RMS against Sqr wav (within 3); forms 1 and 2 differ from form 0 by -29.5 / -28.5 dB sample for sample (B's steep band-limited edge read at a whole table point). The PWM draft's claim that a truncated B cancels on the ramps holds only when both reads are truncated. The schedule's -40 dB bound was a guess; the law is now a printed measurement with a -15 dB gross-error guard, and the quality call is the device listening. `control_cost.py` gained the pwm wrapper as an audio loop (weight 0, the wrapper guard). Old loops: 94/95 exact; `osc_wtbFmBlendRampBlock`'s 74-instruction per-segment cycle changed only its stack offsets (frame 4 B smaller), accepted as layout; `…Osc` 221 and `…Osc2` 234 unchanged; mixer 0 grew against S4. Host suite passes; 45 breaks caught. Flash 688,756 B; ITCM 14,032 B (veneers +48 B; S3+S5 total +2,632 of the approved +2,950). Snapshot `s5/`. |
| S6 | Proposed, pending user review | `basic_worst.txt`, `advanced_ceiling.txt`. Per block measured; `osc_setFreq`'s paths split by exit, because it tail-branches to `osc_userBind`, which `elf_blockpath.py` counts as an exit: stock 128 -> 87, typed + bind 118 -> 96. Drum dearest **257.8** (loops 219.5 + 872/32 + SRR 11); Alkaline no slot **263.7** is the Basic worst case. Advanced dearest (Cymbal/HiHat, `slp` FM carrier, two l-entry modulators) **285.9** (loops 245.5 + 940/32 + SRR 11), 7.0 below S0's ≈ 292.9. **Allocation (user, 2026-11-11):** requested as 4.0 per sample to each of the two interpolation slots, on an estimated 8.0 drop. Confirmed drop: **7.0** (292.9 -> 285.9; the potential part is 3.0, not ≈ 3.8). The split of 7.0 is pending the user (`advanced_ceiling.txt`). Final mixer against S0: only the decimator +1 (accepted, S3 row); S2's `calcFmSineBlock` +1 groups are gone (28 / 29 = S0). |
| Device | Pending | One final image and the plan's hardware checklist. |

| Step | New files | Modified files | Deleted |
|---|---|---|---|
| S0 | `baselines/s094p1/s0/*`, `baselines/s094p1/alk_golden.bin` | `tools/dsp_test/modules/alkaline.c` | — |
| S1 | `tools/plots/gen_lfo_rate_table.py`, `Core/DSPAudio/lfoRateTable.c` (gen.), `tools/dsp_test/modules/lfo.c` | `lfo.h`, `lfo.c`, `Makefile`, `InstrumentManager.h`, `InstrumentManager.c`, `DrumParameters.c`, `SnareParameters.c`, `CymbalParameters.c`, `HiHatParameters.c`, `AlkalineParameters.c`, `check_special_tags.py`, `make_init_alk.py`, `tools/dsp_test/Makefile` | — |
| S2 | `tools/samples/make_crash_wav.py`, `tools/samples/scan_wave_refs.py`, `SD_CARD/samples/02_crash.wav` | `Oscillator.h`, `Oscillator.c`, `Makefile`, `MenuText.h`, `DrumVoice.c` (comment), `osc_host.h`, `modules/oscillator.c`, `modules/alkaline.c`, `modules/run_modes.c` (float distortion stand-in) | `Samples.c`, `Samples.h` |
| S3 | `tools/plots/gen_pwm_tables.py`, `Core/DSPAudio/oscPwmTables.h/.c` (gen.) | `Oscillator.h`, `Oscillator.c`, `STM32F765VIHx_FLASH.ld`, `Makefile`, `MenuText.h`, `menu.c`, `osc_host.h`, `modules/oscillator.c`, `modules/alkaline.c`, `tools/dsp_test/Makefile` (incl. the PWM loops in `P4_OSC_SYMS`) | — |
| S4 | `tools/alkaline/gen_alk_post_tables.py`, `AlkalinePostTables.h/.c` (gen.) | `DiodeLadder.h`, `DiodeLadder.c`, `AlkalineVoice.h`, `AlkalineVoice.c`, `AlkalineParameters.h`, `AlkalineParameters.c`, `InstrumentManager.c`, `Makefile`, `make_init_alk.py`, `SD_CARD/Instrument/Alkaline/init.alk` (gen.), `check_special_tags.py` (comment), `modules/alkaline.c`, `tools/dsp_test/Makefile` | — |
| S5 | — | `DiodeLadder.h`, `DiodeLadder.c`, `AlkalineVoice.h`, `AlkalineVoice.c`, `MenuText.h`, `menu.c`, `InstrumentManager.c`, `modules/alkaline.c`, `tools/dsp_test/Makefile`, `tools/alkaline/control_cost.py` (pwm wrapper ITERS) | — |
| S6 | `baselines/s094p1/basic_worst.txt`, `advanced_ceiling.txt`, `s1..s5/` snapshots | — | — |

**Not touched by any step:** `MEMORY.md`, `knowledge_files/`, the four engine
render files' code (only the S2 comment in `DrumVoice.c`), `BusCompressor.c`
and `mixer.c`.

**Statements made out of date:** plan §11, for the user to log.
`MIDI_NRPN_TABLE.md` (yours) gains the Alkaline rows 29 and 30, and the
voice-drive group includes the Alkaline.
