# S086 P3 — Implementation schedule: `pos` (with P4 `Cym`)

**Implements:** `S086_P3_WAV_OSC_POS_PARAMETER.md` (decisions §9) and
`S086_P4_SAMPLE_CYM_POS.md` (Q1 decided; Q2–Q4 as recommended). The two are
implemented together (P4 Q3).
**Branch:** `dev-ph7-voice_expansion`, P2 state (Re3/Sw3; uncommitted)
**Written:** 2026-10-28
**Status:** implemented; hardware tests pending. Step 0 baseline recorded;
firmware changes were applied bottom-up and verified against the P2 image.

## Implementation notes

### Step 0 — P2 baseline (2026-10-28)

- `make all` passed before the P3 edits.
- `make img` passed and rebuilt `build/LXRV2_lxr02.img` at 659,088 bytes.
- Baseline build figures match the P2 report: `text=658,664`, `data=424`,
  `bss=430,296`, flash headroom `94,576` bytes, ITCM `8,512` bytes,
  DTCM statics `4,472` bytes, and FX margin `3,712` bytes.
- Added the test-bench targets for the P2 ELF snapshot and P3 comparisons;
  the snapshot will be written now from this unchanged P2-linked image.

### Step 1 — P2 linked-image snapshot (2026-10-28)

- Saved `frozen86/elf_osc_p2.txt` with 26 oscillator symbols and
  `frozen86/elf_loops_p2.txt` with 61 loop bodies from the P2 image.
- Added the engine identity allow-list for the four P2 waveform-condition
  replacements; the later P3 additions are required to be additions only.

### Step 2 — oscillator storage and helpers (2026-10-28)

- Added `position` and `positionApplied` to the existing `OscInfo` padding at
  offsets 18–19. Compile-time offset checks and the ARM `sizeof(OscInfo)==72`
  check passed.
- Added the phase-wave classification, trigger offset, circular per-block
  bend, cap constant (`64`, uncapped), and exact step-size assertion in
  `Oscillator.h`. `Oscillator.c` and all per-sample loops remain untouched.

### Step 3 — crash endpoint and engine integration (2026-10-28)

- Added the crash guard point and changed its public array contract to 32,769
  bytes. The guard repeats sample zero and does not alter the loop code.
- Added trigger-time position application to Drum, Snare, Cymbal, and HiHat.
  Added the temporary bend/restore around each osc1 render; Drum `snc`
  advances the tracker but intentionally renders with no bend.
- `make all` passed. Build after firmware integration: `text=658,832`,
  `data=424`, `bss=430,296`, flash headroom `94,408` bytes, ITCM `8,512`,
  DTCM statics `4,472`, FX margin `3,712`. `OscInfo` and `DrumVoice` did not
  grow.

### Step 4 — descriptors and menu layout (2026-10-28)

- Appended one plain `TYPE_UINT8` `osc1_position` row to each instrument;
  existing descriptor IDs remain stable. Counts are now Drum 40, Snare 39,
  Cymbal 40, HiHat 40.
- Put `pos` in the fourth cell of each first oscillator screen. Snare now
  shows `coa fin wav pos` on screen 1 and moves `noi mix` to screen 2.
- `special_tags` passed with 159 rows and 0 mismatches. The filesystem
  specification now documents the key, default, and updated descriptor counts.

### Step 5 — host and linked-image gates (2026-10-28)

- Added `test_osc_pos.c`, covering trigger offsets, held-value identity,
  circular wrapping, exact continuity, 4,096-block no-drift checks for Sine,
  Saw and Crash, non-phase waveforms, and a cap-8 build.
- `osc_pos` passed both builds (`osc_pos failures: 0` twice); `osc_stock`
  passed; `engine86_identity` passed after recording the one superseded P2
  Snare comment line; `elf86_p3_check` passed with 26 symbols unchanged and
  61 oscillator loops exact.
- The ordinary host build emits only the pre-existing extracted sample-pointer
  cast warning from `osc_updateSampleCache()`.

### Step 6 — final image and regression suite (2026-10-28)

- Final `make all` passed with the same resource figures recorded in Step 3.
- `make img` passed and wrote `build/LXRV2_lxr02.img` at 659,256 bytes.
- The combined regression run passed: `osc_mod`, `osc_stock`, `osc_pos`,
  `engine86_identity`, `special_tags`, `stock_tables`, `elf86_p3_check`,
  `selftest`, `pack`, `postchain`, `octave`, `mixer`, `armcheck-postchain`,
  `armcheck-mixer`, and `armcheck-oscmod`.
- The octave target reports its documented 16 edge cases within 2 ulps;
  this is unchanged from the P2/S086 baseline. No hardware checks have been
  run in this session; P3 H1–H10 and P4 C1–C4 remain for the user.

---

## 0. How to read this schedule

**Line numbers** are those of the working tree when this schedule was
written: P2 applied, nothing of P3. Within one file, apply the changes
**from the bottom up**, so the line numbers of earlier changes stay valid.
The order in §2 already lists each file's changes bottom-up.

**Actions:**
- **ADD after N:** insert new lines after line N.
- **ADD before N:** insert new lines before line N.
- **MODIFY N:** replace line N.
- **REMOVE:** none in this step.

**Comment blocks.** Every change carries a comment block in the house style
(What / Why / Inputs / Outputs / Accessors / Affiliates). Paste the block
into the source with the code: it is the documentation-in-place. Where a
change is a single table cell or count, the block is short and sits on the
line above.

**Measured facts this schedule relies on** (P2 tree, ARM build):

| Fact | Value | Source |
|---|---|---|
| `sizeof(OscInfo)` | 72 B | `arm-none-eabi-gcc` probe |
| `offsetof(tableOffset)` / `offsetof(pitchMod)` | 17 / 20 (padding at 18–19) | same |
| `sizeof(DrumVoice/SnareVoice/CymbalVoice/HiHatVoice)` | 596 / 420 / 476 / 484 B | same |
| voice render block size | always `OUTPUT_DMA_SIZE` = 32 | `mixer.c:965` |
| `osc_setFreq()` callers | only the four `*_calcAsyncVoice()`/`Drum_calcVoiceAsync()`, before the render in the same block | `grep` |
| block order | LFO dispatch (`mixer.c:878`) → async (`:882`) → render (`:965`) | `mixer.c` |
| runtime slot reset | `memset` of the whole runtime voice, then `*_initVoice()` | `InstrumentManager.c:1495` |
| missing kit key | 0 (`instrumentManager_resetSlot()` zeroes the slot before parsing) | `InstrumentManager.c:655` |
| `TYPE_UINT8` instance-offset rows | written as a plain byte | `instrumentManager_writeRuntimeInternal()`, `InstrumentManager.c:3526` |
| velocity range for `TYPE_UINT8` | the descriptor's 0..127 | `instrumentManager_buildModulationRange()`, `InstrumentManager.c:2317` |
| `crashSample` | 32,768 entries; `calcSampleOscBlock()` reads `[itg + 1]`, i.e. `[32768]` at the end | `Samples.c:55`, `Oscillator.c:909–911` |

---

## 1. Step 0: baseline (before any edit)

1. **Build and record:**

   ```
   make all && make img
   ```

   Expect text 658,664; ITCM 8,512; flash headroom 94,576 (the P2 figures).
2. **Save the P2 ELF dumps** as the P3 reference. They are saved by the
   two new Makefile targets of §4.1 (C-T5). Add those targets first; they
   are test-bench only.

   ```
   make -C tools/dsp_test elf86_p2_snapshot
   ```

   This writes `frozen86/elf_osc_p2.txt` and `frozen86/elf_loops_p2.txt`
   from the current `build/lxr02.elf`.
3. **Run the existing gates** and note the results: `osc86_identity`,
   `osc_mod`, `osc_stock`, `special_tags` (155 rows), `stock_tables`.

---

## 2. Change list (overview)

| # | File | Lines (P2 tree) | Action |
|---|---|---|---|
| C1 | `Core/DSPAudio/Oscillator.h` | after 369 | ADD the cap and step constants, the divisibility assert, three helpers |
| C2 | `Core/DSPAudio/Oscillator.h` | after 102 | ADD `#include <stddef.h>` and three layout asserts |
| C3 | `Core/DSPAudio/Oscillator.h` | after 79 | ADD two `OscInfo` members with their block |
| C4 | `Core/DSPAudio/Samples.c` | before 2104; 55 | ADD the guard element; MODIFY the array size (P4 Q4) |
| C5 | `Core/DSPAudio/Samples.h` | 40 | MODIFY the declaration size (P4 Q4) |
| C6 | `Core/DSP/Instruments/Drum/DrumVoice.c` | after 463; before 405; after 212 | ADD bend restore; ADD bend apply; ADD trigger offset |
| C7 | `Core/DSP/Instruments/Snare/Snare.c` | after 237; before 236; after 150 | ADD restore; ADD apply; ADD trigger offset |
| C8 | `Core/DSP/Instruments/Cymbal/CymbalVoice.c` | after 247; before 247; after 158 | ADD restore; ADD apply; ADD trigger offset |
| C9 | `Core/DSP/Instruments/HiHat/HiHat.c` | after 264; before 264; after 172 | ADD restore; ADD apply; ADD trigger offset |
| C10 | `Core/DSP/Instruments/Drum/DrumParameters.c` | 262; after 244; after 171 | MODIFY page 1; ADD row; ADD enum |
| C11 | `Core/DSP/Instruments/Drum/DrumParameters.h` | 14 | MODIFY the count 39 → 40 |
| C12 | `Core/DSP/Instruments/Snare/SnareParameters.c` | 250; after 234; after 169 | MODIFY page 1; ADD row; ADD enum |
| C13 | `Core/DSP/Instruments/Snare/SnareParameters.h` | 14 | MODIFY the count 38 → 39 |
| C14 | `Core/DSP/Instruments/Cymbal/CymbalParameters.c` | 251; after 236; after 170 | MODIFY page 1; ADD row; ADD enum |
| C15 | `Core/DSP/Instruments/Cymbal/CymbalParameters.h` | 14 | MODIFY the count 39 → 40 |
| C16 | `Core/DSP/Instruments/HiHat/HiHatParameters.c` | 256; after 239; after 173 | MODIFY page 1; ADD row; ADD enum |
| C17 | `Core/DSP/Instruments/HiHat/HiHatParameters.h` | 14 | MODIFY the count 39 → 40 |
| C-T1 | `tools/dsp_test/Makefile` | 252 | MODIFY the `OscInfo` line range 71-102 → 71-124 |
| C-T2 | `tools/dsp_test/Makefile` | after 379 | ADD `osc_pos`, `engine86_identity`, `elf86_p2_snapshot`, `elf86_p3_check` |
| C-T3 | `tools/dsp_test/test_osc_pos.c` | new file | ADD the host test P1–P7 (+ P4) |
| C-T4 | `tools/dsp_test/frozen86/engine_identity_allowed.txt` | new file | ADD the four P2 engine lines |
| C-T5 | `tools/dsp_test/DSP_TEST.md` | 168, 199; after 213 | MODIFY 155 → 159 rows; ADD the new target rows |
| C-D1 | `knowledge_files/specification_reference/FILESYSTEM_SPEC.md` | 1815–1818 | MODIFY: add `osc1_position` to the oscillator keys |

**Not changed, and why:**

| File | Why it needs no change |
|---|---|
| `Core/DSPAudio/Oscillator.c` | No loop or dispatcher is touched; the bend works through `phaseInc`, which every loop already reads. `osc86_identity` and the ELF gates prove it. |
| `InstrumentManager.c/.h` | The row is a plain `ROW` with `TYPE_UINT8`: the generic instance-offset writer, the LFO adapter and the velocity range already handle it. There is no `IM_SPECIAL_*` tag, and the key classifier returns `IM_SPECIAL_NONE` for `osc1_position`. |
| `Core/Menu/menu.c`, `MenuText.h` | `DTYPE_0B127` displays the number; no menu code is tied to cell positions. |
| `storageTypes.c`, `filesystem.c`, `presetManager.c`, `copyOps.c`, Morph | All are generic over descriptors and key strings. |
| `modulationNode.c` | The LFO target list follows `mod_domain`; `MOD_0_127` makes `pos` a destination. |
| `tools/dsp_test/check_special_tags.py` | `osc1_position` classifies to "no tag", which matches a plain `ROW`. Only the printed count changes (155 → 159). |
| Host converters | An absent key loads 0 (P3 Q9). |

---

## 3. Firmware changes in detail

### C1. `Core/DSPAudio/Oscillator.h`: ADD after line 369 (after `osc_isTableWave()`, before `#endif` at 371)

```c

/*
 * pos bend cap and step size (S086 P3).
 *
 * What:       OSC_POS_BEND_CAP is the largest change of pos (in 1/128ths of
 *             a cycle) that one block's bend may apply. A larger change is
 *             applied over several blocks, each moving positionApplied by
 *             at most the cap toward position. 64 is "no cap": the circular
 *             difference is already within -64..+63.
 *             OSC_POS_BEND_PER_STEP is the per-sample phaseInc bend for one
 *             step of pos: (1 << 25) / OUTPUT_DMA_SIZE, so a bend of d steps
 *             held for one block moves the phase by exactly d << 25.
 * Why:        S086 P3 Q11: build the cap as one constant, off at first, and
 *             decide by ear (a large step squeezed into one block is a short
 *             pitch blip that also aliases). The exact step size keeps the
 *             offset from drifting.
 * Inputs:     OUTPUT_DMA_SIZE (config.h); may be overridden before this
 *             header for tests (tools/dsp_test osc_pos_cap).
 * Outputs:    two constants.
 * Accessors:  osc_positionBend().
 * Affiliates: S086_P3_WAV_OSC_POS_PARAMETER.md §2.3; mixer.c renders every
 *             voice block at OUTPUT_DMA_SIZE.
 */
#ifndef OSC_POS_BEND_CAP
#define OSC_POS_BEND_CAP		64
#endif
#define OSC_POS_BEND_PER_STEP	(0x2000000u / OUTPUT_DMA_SIZE)
_Static_assert((0x2000000u % OUTPUT_DMA_SIZE) == 0u,
               "the pos bend must divide one 1/128-cycle step exactly");

/*
 * Whether pos moves a waveform as a phase (S086 P3, P4).
 *
 * What:       1 for SINE, CRASH and the table waves (osc_isTableWave()):
 *             the waves whose full 2^32 phase range is one cycle, or, for
 *             CRASH, the whole 32,768-point sample. 0 for NOISE and the v1
 *             user samples.
 * Why:        pos << 25 is pos/128 of that range for every such wave; the
 *             user samples get their own rule in S2 (P4 Q2).
 * Inputs:     waveform.  Outputs: 0 or 1.
 * Accessors:  osc_positionTrigger(), osc_positionBend().
 * Affiliates: osc_isTableWave(); S086_P4_SAMPLE_CYM_POS.md §1.
 */
static inline uint8_t osc_positionIsPhase(const uint8_t waveform)
{
	return (uint8_t)(waveform == SINE || waveform == CRASH ||
	                 osc_isTableWave(waveform));
}

/*
 * pos at a trigger (S086 P3).
 *
 * What:       returns the phase offset to add to the engine's start phase,
 *             (position & 127) << 25 for the phase waves and 0 otherwise,
 *             and sets positionApplied = position, so the first block after
 *             the hit does not bend.
 * Why:        the hit starts exactly at today's start phase + pos; pos 0
 *             returns 0, which is today's behaviour bit for bit. The offset
 *             is added after the transient offset-mode scaling (P3 Q6).
 * Inputs:     osc->position (the osc1_position row), osc->waveform.
 * Outputs:    the offset; osc->positionApplied.
 * Accessors:  Drum_triggerVoice(), Snare_triggerVoice(),
 *             Cymbal_triggerVoice(), HiHat_triggerVoice().
 * Affiliates: osc_positionBend() (the continuous part);
 *             S086_P3_WAV_OSC_POS_PARAMETER.md §2.2.
 */
static inline uint32_t osc_positionTrigger(OscInfo* osc)
{
	const uint32_t pos = (uint32_t)(osc->position & 127u);

	osc->positionApplied = (uint8_t)pos;
	if (!osc_positionIsPhase(osc->waveform))
		return 0u;
	return pos << 25;
}

/*
 * pos while playing: the per-block phaseInc bend (S086 P3).
 *
 * What:       d = the circular difference position - positionApplied in
 *             -64..+63 (127 -> 0 is +1, not -127), limited to
 *             +-OSC_POS_BEND_CAP. positionApplied advances by d. Returns
 *             d x OSC_POS_BEND_PER_STEP for the phase waves (0 otherwise).
 *             The caller adds the result to osc->phaseInc before rendering
 *             the block and subtracts it afterwards, so the phase glides by
 *             exactly d << 25 over the block, with no jump.
 * Why:        modulation (LFO, knob, velocity, automation, Morph) changes pos
 *             once per block. Spreading each change over the block's samples
 *             through phaseInc keeps the phase continuous with no loop
 *             change and no per-sample cost; pitch modulation already
 *             changes phaseInc per block the same way. Non-phase waves still
 *             consume the change, so switching wav later releases nothing.
 *             A negative result can make the bent increment wrap below zero
 *             at low pitch; the phase then runs backwards for the block,
 *             which the Sine/table/crash loops handle by modular indexing.
 * Inputs:     osc->position, osc->positionApplied, osc->waveform.
 * Outputs:    the signed bend; osc->positionApplied.
 * Accessors:  Drum_calcVoiceSyncBlock() (not in snc, P3 Q12),
 *             Snare_calcSyncBlockVoice(), Cymbal_calcSyncBlockVoice(),
 *             HiHat_calcSyncBlockVoice().
 * Affiliates: osc_setFreq() (its cache would keep a bend left in phaseInc,
 *             hence the restore); calcPeriodicInterp*() (end the block at
 *             start + phaseInc x size, which includes the bend);
 *             S086_P3_WAV_OSC_POS_PARAMETER.md §2.3-§2.5.
 */
static inline int32_t osc_positionBend(OscInfo* osc)
{
	const uint32_t u = ((uint32_t)(osc->position & 127u) -
	                    (uint32_t)osc->positionApplied + 64u) & 127u;
	int32_t d = (int32_t)u - 64;

	if (d > (int32_t)OSC_POS_BEND_CAP)
		d = (int32_t)OSC_POS_BEND_CAP;
	else if (d < -(int32_t)OSC_POS_BEND_CAP)
		d = -(int32_t)OSC_POS_BEND_CAP;
	osc->positionApplied =
		(uint8_t)(((uint32_t)osc->positionApplied + (uint32_t)d) & 127u);
	if (!osc_positionIsPhase(osc->waveform))
		return 0;
	return d * (int32_t)OSC_POS_BEND_PER_STEP;
}
```

### C2. `Core/DSPAudio/Oscillator.h`: ADD after line 102 (`} OscInfo;`)

```c

/*
 * OscInfo layout checks (S086 P3).
 *
 * What:       position and positionApplied must fill the padding between
 *             tableOffset (17) and pitchMod (20) exactly, so no OscInfo,
 *             voice or runtime slot grows. On the ARM build the size is
 *             checked as well (72 bytes, pointers are 4 bytes there).
 * Why:        S086 P3 §3: pos costs no RAM. A field moved or added in front
 *             of pitchMod would silently grow every voice ×(2-3 oscillators)
 *             ×6 slots.
 * Inputs:     the OscInfo definition above.  Outputs: none (compile time).
 * Accessors:  the compiler.
 * Affiliates: DrumVoice/SnareVoice/CymbalVoice/HiHatVoice; runtime_slots
 *             (InstrumentManager.c); tools/link_budget.py.
 * Note:       <stddef.h> is included here, not at the top, so the line
 *             ranges the test bench extracts (tools/dsp_test/Makefile
 *             osc_mod: defines 49-65, OscInfo 71-124) do not move.
 */
#include <stddef.h>
_Static_assert(offsetof(OscInfo, position) == offsetof(OscInfo, tableOffset) + 1u,
               "OscInfo.position must follow tableOffset (padding)");
_Static_assert(offsetof(OscInfo, positionApplied) + 1u == offsetof(OscInfo, pitchMod),
               "OscInfo.positionApplied must end the padding before pitchMod");
#if defined(__arm__)
_Static_assert(sizeof(OscInfo) == 72u, "OscInfo must stay 72 bytes (S086 P3)");
#endif
```

### C3. `Core/DSPAudio/Oscillator.h`: ADD after line 79 (`uint8_t tableOffset;`)

This adds exactly 22 lines, so `} OscInfo;` moves from 102 to 124 (C-T1).

```c
	/*
	 * pos: the continuous position control and its applied part (S086 P3).
	 *
	 * What:       position is the osc1_position row value, 0..127. For the
	 *             waves osc_positionIsPhase() accepts, it is a phase offset of
	 *             position/128 of a cycle (CRASH: of the whole sample).
	 *             positionApplied is the part of position already contained
	 *             in phase. It is runtime only and never stored in a file.
	 * Why:        the per-block bend (osc_positionBend()) moves the phase by
	 *             the change of position, so the applied part must be kept.
	 *             Both bytes sit in the padding before pitchMod (offsets
	 *             18-19), so OscInfo does not grow (asserted below).
	 * Inputs:     position: the descriptor runtime writer (menu edit, Morph,
	 *             step automation, LFO adapter, velocity node).
	 *             positionApplied: osc_positionTrigger(), osc_positionBend().
	 * Outputs:    read by those two helpers only.
	 * Accessors:  Drum/Snare/Cymbal/HiHat trigger and render functions.
	 * Affiliates: the osc1_position rows in *Parameters.c;
	 *             S086_P3_WAV_OSC_POS_PARAMETER.md §2-§3; S086_P4 (CRASH).
	 */
	uint8_t		position;
	uint8_t		positionApplied;
```

### C4. `Core/DSPAudio/Samples.c` (P4 Q4)

**ADD before line 2104** (`};`), after the last data row (line 2103). That
row ends without a comma, so add one to it (correction made after the
assessment, F5b):

```c
	/*
	 * Guard point (S086 P4): a copy of crashSample[0].
	 *
	 * What:       entry 32768, so calcSampleOscBlock() and its FM, sync and
	 *             ring copies read a real value at itg + 1 when itg = 32767,
	 *             the last point. The crash loops (its 32,768 points span the
	 *             full 2^32 phase), so the next point is point 0.
	 * Why:        the readers have always read one entry past the end; pos
	 *             (S086 P3/P4) can now place the phase anywhere on purpose.
	 *             The loops are unchanged (Oscillator.c is not edited).
	 * Affiliates: Samples.h (the declared size); test_osc_mod.c and
	 *             test_osc_stock.c host copies (already 32,769).
	 */
	0x80
```

**MODIFY line 55:**
- from: `const unsigned char crashSample[32768] = {`
- to:   `const unsigned char crashSample[32769] = {	/* +1 guard (S086 P4) */`

### C5. `Core/DSPAudio/Samples.h`: MODIFY line 40

- from: `extern const unsigned char crashSample[32768];`
- to:

```c
/*
 * The crash sample: 32,768 unsigned 8-bit points plus one guard point
 * (S086 P4).
 *
 * What:       entries 0..32767 are the sample; entry 32768 repeats entry 0
 *             so the interpolating readers' itg + 1 read at the last point
 *             stays inside the array.
 * Accessors:  Oscillator.c calcSampleOscBlock() and its FM/sync/ring copies.
 * Affiliates: Samples.c; S086_P4_SAMPLE_CYM_POS.md Q4.
 */
extern const unsigned char crashSample[32769];
```

### C6. `Core/DSP/Instruments/Drum/DrumVoice.c`

**(a) ADD after line 463** (the switch's closing `}`), before
`//calc transient sample` at 465:

```c

	/* S086 P3: remove the pos bend added before the switch (see there). */
	voice->osc.phaseInc -= (uint32_t)posBend;
```

**(b) ADD before line 405** (`switch (voice->modMode)`), after the S085
comment block ending at 404:

```c
	/*
	 * pos bend for the carrier (S086 P3).
	 *
	 * What:       osc_positionBend() returns the phaseInc bend that moves
	 *             osc1's phase by this block's change of pos (circular, capped
	 *             by OSC_POS_BEND_CAP), and advances osc.positionApplied. The
	 *             bend is added for the switch below and removed after it, so
	 *             the carrier glides to the new offset by the end of the block.
	 *             In snc the bend is dropped (posBend = 0): each master reset
	 *             sets the slave's phase to the reset target, which does not
	 *             contain pos, and osc_syncRatioQ16() reads the slave's
	 *             phaseInc. The helper is still called, so positionApplied
	 *             follows pos in every mode.
	 * Why:        pos is continuous (S086 P3 §2.3). modOsc is a separate
	 *             OscInfo and is not touched, so in fm, mix and rng only the
	 *             carrier moves against the modulator.
	 * Inputs:     voice->osc.position/positionApplied/waveform; voice->modMode;
	 *             osc.phaseInc as set by osc_setFreq() in Drum_calcVoiceAsync()
	 *             earlier in this block.
	 * Outputs:    osc.phaseInc bent for the render below; osc.positionApplied.
	 * Accessors:  this function only.
	 * Affiliates: osc_positionBend() (Oscillator.h); Drum_triggerVoice()
	 *             (osc_positionTrigger()); S086_P3 Q12 (snc).
	 */
	int32_t posBend = osc_positionBend(&voice->osc);
	if (voice->modMode == DRUM_MOD_SNC)
		posBend = 0;
	voice->osc.phaseInc += (uint32_t)posBend;

```

**(c) ADD after line 212** (`voice->osc.phase = 0;`), inside the braces
closing at 214:

```c
		/*
		 * pos at the trigger (S086 P3).
		 *
		 * What:       adds osc_positionTrigger(): pos/128 of a cycle for the
		 *             phase waves (Sin, Tri, Saw, Rec, Re3, Sw3; Cym: pos/128
		 *             of the sample), 0 for noise and user samples. It also
		 *             resets osc.positionApplied, so the first block does not
		 *             bend.
		 * Why:        the hit starts exactly at today's start phase + pos; pos
		 *             0 is today's phase bit for bit. It is added after the
		 *             offset-mode scaling above (S086 P3 Q6). Velocity is
		 *             already applied (modNode_updateValue() above), so a
		 *             velocity target on pos is exact for this hit.
		 * Inputs:     voice->osc.position, voice->osc.waveform.
		 * Outputs:    voice->osc.phase, voice->osc.positionApplied.
		 * Accessors:  this trigger.
		 * Affiliates: osc_positionTrigger() (Oscillator.h);
		 *             Drum_calcVoiceSyncBlock() (the per-block bend).
		 */
		voice->osc.phase += osc_positionTrigger(&voice->osc);
```

`USE_AMP_FILTER` is not defined, so the block at 195–214 always runs, and the
new line runs on every trigger.

### C7. `Core/DSP/Instruments/Snare/Snare.c`

**(a) ADD after line 237** (`calcNextOscSampleBlock(&voice->osc,transBuf,size,(1.f-voice->mix));`):

```c
	voice->osc.phaseInc -= (uint32_t)posBend;	/* S086 P3: remove the pos bend */
```

**(b) ADD before line 236** (`//calc next osc sample`):

```c
	/*
	 * pos bend for osc1 (S086 P3).
	 *
	 * What:       osc_positionBend() returns the phaseInc bend that moves the
	 *             oscillator's phase by this block's change of pos (circular,
	 *             capped by OSC_POS_BEND_CAP) and advances positionApplied.
	 *             It is added for the osc1 render only and removed right after.
	 * Why:        pos is continuous (S086 P3 §2.3): the phase glides to the
	 *             new offset within the block, with no jump and no loop change.
	 *             noiseOsc is a separate OscInfo and is not bent.
	 * Inputs:     voice->osc.position/positionApplied/waveform; osc.phaseInc
	 *             from osc_setFreq() in Snare_calcAsyncVoice() this block.
	 * Outputs:    osc.phaseInc for the render; osc.positionApplied.
	 * Accessors:  this function only.
	 * Affiliates: osc_positionBend() (Oscillator.h); Snare_triggerVoice().
	 */
	const int32_t posBend = osc_positionBend(&voice->osc);
	voice->osc.phaseInc += (uint32_t)posBend;
```

**(c) ADD after line 150** (`voice->osc.phase = 0;`), before
`DecayEg_trigger()` at 152:

```c
	/*
	 * pos at the trigger (S086 P3).
	 *
	 * What:       adds osc_positionTrigger(): pos/128 of a cycle for the
	 *             phase waves (Sin, Tri, Saw, Rec, Re3, Sw3; Cym: pos/128 of
	 *             the sample), 0 for noise and user samples. It also resets
	 *             osc.positionApplied, so the first block does not bend.
	 * Why:        the hit starts exactly at today's start phase + pos; pos 0
	 *             is today's phase bit for bit. It is added after the
	 *             offset-mode scaling above (S086 P3 Q6). Velocity is already
	 *             applied, so a velocity target on pos is exact for this hit.
	 * Inputs:     voice->osc.position, voice->osc.waveform.
	 * Outputs:    voice->osc.phase, voice->osc.positionApplied.
	 * Accessors:  this trigger.
	 * Affiliates: osc_positionTrigger() (Oscillator.h);
	 *             Snare_calcSyncBlockVoice() (the per-block bend).
	 */
	voice->osc.phase += osc_positionTrigger(&voice->osc);
```

### C8. `Core/DSP/Instruments/Cymbal/CymbalVoice.c`

**(a) ADD after line 247** (`calcNextOscSampleFmBlock(&voice->osc,mod,buf,size,1.f) ;`):

```c
	voice->osc.phaseInc -= (uint32_t)posBend;	/* S086 P3: remove the pos bend */
```

**(b) ADD before line 247** (after the modulator mix at 245):

```c
	/*
	 * pos bend for osc1 (S086 P3).
	 *
	 * What:       osc_positionBend() returns the phaseInc bend that moves the
	 *             oscillator's phase by this block's change of pos (circular,
	 *             capped by OSC_POS_BEND_CAP) and advances positionApplied.
	 *             It is added for the osc1 FM render only and removed right
	 *             after.
	 * Why:        pos is continuous (S086 P3 §2.3): the phase glides to the
	 *             new offset within the block, with no jump and no loop change.
	 *             modOsc and modOsc2 are separate OscInfo and are not bent
	 *             (their pos rows wait for the typ step, S086 P3 Q10).
	 * Inputs:     voice->osc.position/positionApplied/waveform; osc.phaseInc
	 *             from osc_setFreq() in Cymbal_calcAsyncVoice() this block.
	 * Outputs:    osc.phaseInc for the render; osc.positionApplied.
	 * Accessors:  this function only.
	 * Affiliates: osc_positionBend() (Oscillator.h); Cymbal_triggerVoice().
	 */
	const int32_t posBend = osc_positionBend(&voice->osc);
	voice->osc.phaseInc += (uint32_t)posBend;
```

**(c) ADD after line 158** (`voice->osc.phase = 0;`), before the modulator
resets at 160–161:

```c
	/*
	 * pos at the trigger (S086 P3).
	 *
	 * What:       adds osc_positionTrigger(): pos/128 of a cycle for the
	 *             phase waves (Sin, Tri, Saw, Rec, Re3, Sw3; Cym: pos/128 of
	 *             the sample), 0 for noise and user samples. It also resets
	 *             osc.positionApplied, so the first block does not bend.
	 * Why:        the hit starts exactly at today's start phase + pos; pos 0
	 *             is today's phase bit for bit. It is added after the
	 *             offset-mode scaling above (S086 P3 Q6). Velocity is already
	 *             applied, so a velocity target on pos is exact for this hit.
	 * Inputs:     voice->osc.position, voice->osc.waveform.
	 * Outputs:    voice->osc.phase, voice->osc.positionApplied.
	 * Accessors:  this trigger.
	 * Affiliates: osc_positionTrigger() (Oscillator.h);
	 *             Cymbal_calcSyncBlockVoice() (the per-block bend).
	 */
	voice->osc.phase += osc_positionTrigger(&voice->osc);
```

### C9. `Core/DSP/Instruments/HiHat/HiHat.c`

**(a) ADD after line 264** (`calcNextOscSampleFmBlock(&voice->osc,mod1,buf,size,0.5f) ;`):

```c
	voice->osc.phaseInc -= (uint32_t)posBend;	/* S086 P3: remove the pos bend */
```

**(b) ADD before line 264** (after the modulator mix at 262):

```c
	/*
	 * pos bend for osc1 (S086 P3).
	 *
	 * What:       osc_positionBend() returns the phaseInc bend that moves the
	 *             oscillator's phase by this block's change of pos (circular,
	 *             capped by OSC_POS_BEND_CAP) and advances positionApplied.
	 *             It is added for the osc1 FM render only and removed right
	 *             after.
	 * Why:        pos is continuous (S086 P3 §2.3): the phase glides to the
	 *             new offset within the block, with no jump and no loop change.
	 *             modOsc and modOsc2 are separate OscInfo and are not bent
	 *             (their pos rows wait for the typ step, S086 P3 Q10).
	 *             Track 7 (the slot-6 alternate trigger) uses this same
	 *             instance, so it bends identically.
	 * Inputs:     voice->osc.position/positionApplied/waveform; osc.phaseInc
	 *             from osc_setFreq() in HiHat_calcAsyncVoice() this block.
	 * Outputs:    osc.phaseInc for the render; osc.positionApplied.
	 * Accessors:  this function only.
	 * Affiliates: osc_positionBend() (Oscillator.h); HiHat_triggerVoice().
	 */
	const int32_t posBend = osc_positionBend(&voice->osc);
	voice->osc.phaseInc += (uint32_t)posBend;
```

**(c) ADD after line 172** (`voice->osc.phase = 0;`), before
`osc_setBaseNote()` at 174:

```c
	/*
	 * pos at the trigger (S086 P3).
	 *
	 * What:       adds osc_positionTrigger(): pos/128 of a cycle for the
	 *             phase waves (Sin, Tri, Saw, Rec, Re3, Sw3; Cym: pos/128 of
	 *             the sample), 0 for noise and user samples. It also resets
	 *             osc.positionApplied, so the first block does not bend.
	 * Why:        the hit starts exactly at today's start phase + pos; pos 0
	 *             is today's phase bit for bit. It is added after the
	 *             offset-mode scaling above (S086 P3 Q6). Velocity is already
	 *             applied, so a velocity target on pos is exact for this hit.
	 * Inputs:     voice->osc.position, voice->osc.waveform.
	 * Outputs:    voice->osc.phase, voice->osc.positionApplied.
	 * Accessors:  this trigger.
	 * Affiliates: osc_positionTrigger() (Oscillator.h);
	 *             HiHat_calcSyncBlockVoice() (the per-block bend).
	 */
	voice->osc.phase += osc_positionTrigger(&voice->osc);
```

### C10–C17. The descriptor tables

The four tables get the same three edits. The row must stay on **one line**,
because `check_special_tags.py` matches rows line by line.

**The row comment block** is identical in all four files. It goes directly
above the row.

```c
    /*
     * osc1_position: pos, the oscillator's continuous position (S086 P3).
     *
     * What:       0..127, stored in osc.position. For Sin/Tri/Saw/Rec/Re3/
     *             Sw3 it is a phase offset of pos/128 of a cycle; for Cym
     *             pos/128 of the sample (S086 P4). The hit starts at today's
     *             start phase + pos; while playing, changes glide by the
     *             per-block phaseInc bend. Noise and user samples ignore it
     *             (user samples: S2).
     * Why:        S086 P3: the fourth cell of the first oscillator screen.
     *             Appended last, so every existing descriptor index,
     *             automation ID, LFO/velocity token, NRPN ID and MIDI Learn row
     *             is unchanged. A file without the key loads 0 (today's sound).
     * Inputs:     menu edit, Morph, step automation, LFO adapter (MOD_0_127),
     *             velocity node (0..127 range for TYPE_UINT8).
     * Outputs:    osc.position (TYPE_UINT8, plain store; no special writer).
     * Accessors:  osc_positionTrigger(), osc_positionBend() (Oscillator.h).
     * Affiliates: the engine's *_triggerVoice() and render function;
     *             S086_P3_WAV_OSC_POS_PARAMETER.md; FILESYSTEM_SPEC.md keys.
     */
    ROW("osc1_position", "Oscilltr", "Position", "pos", DTYPE_0B127, MOD_0_127, osc.position, TYPE_UINT8),
```

#### C10. `Core/DSP/Instruments/Drum/DrumParameters.c`

| Line | Action | Content |
|---|---|---|
| 262 | MODIFY | page 1 cell 4: `INSTRUMENT_MENU_EMPTY` → `DRUM_PARAM_OSC1_POSITION` (the fourth entry only). Add above the line: `/* S086 P3: coa fin wav pos (pos = osc1_position, cell 4). */` |
| after 244 | ADD | the row comment block and the row (above) |
| after 171 | ADD | `    DRUM_PARAM_OSC1_POSITION,	/* S086 P3: appended; must stay last before COUNT (ID stability) */` |

Line 262 after the change:

```c
    /* S086 P3: coa fin wav pos (pos = osc1_position, cell 4). */
    {{ DRUM_PARAM_OSC1_PITCH_COARSE, DRUM_PARAM_OSC1_PITCH_FINE, DRUM_PARAM_OSC1_WAVE, DRUM_PARAM_OSC1_POSITION, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY }},
```

#### C11. `Core/DSP/Instruments/Drum/DrumParameters.h`: MODIFY line 14

```c
#define DRUM_PARAM_DESCRIPTOR_COUNT 40u	/* S086 P3: + osc1_position */
```

#### C12. `Core/DSP/Instruments/Snare/SnareParameters.c`

| Line | Action | Content |
|---|---|---|
| 250 | MODIFY | page 1 reordered (P3 Q3): screen 1 `coa fin wav pos`, screen 2 `noi mix` |
| after 234 | ADD | the row comment block and the row |
| after 169 | ADD | `    SNARE_PARAM_OSC1_POSITION,	/* S086 P3: appended; must stay last before COUNT (ID stability) */` |

Line 250 after the change:

```c
    /*
     * S086 P3 (Q3): every oscillator page now has the same first screen,
     * coa fin wav pos; noi mix move to screen 2 (cells 5-6), where typ and
     * run will follow. Only cell positions change; descriptor indices,
     * automation and Learn IDs are unchanged.
     */
    {{ SNARE_PARAM_OSC1_PITCH_COARSE, SNARE_PARAM_OSC1_PITCH_FINE, SNARE_PARAM_OSC1_WAVE, SNARE_PARAM_OSC1_POSITION, SNARE_PARAM_NOISE_FREQ, SNARE_PARAM_OSC1_NOISE_MIX, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY }},
```

#### C13. `Core/DSP/Instruments/Snare/SnareParameters.h`: MODIFY line 14

```c
#define SNARE_PARAM_DESCRIPTOR_COUNT 39u	/* S086 P3: + osc1_position */
```

#### C14. `Core/DSP/Instruments/Cymbal/CymbalParameters.c`

| Line | Action | Content |
|---|---|---|
| 251 | MODIFY | page 1 cell 4 → `CYMBAL_PARAM_OSC1_POSITION`, with `/* S086 P3: coa fin wav pos (pos = osc1_position, cell 4). */` above |
| after 236 | ADD | the row comment block and the row |
| after 170 | ADD | `    CYMBAL_PARAM_OSC1_POSITION,	/* S086 P3: appended; must stay last before COUNT (ID stability) */` |

#### C15. `Core/DSP/Instruments/Cymbal/CymbalParameters.h`: MODIFY line 14

```c
#define CYMBAL_PARAM_DESCRIPTOR_COUNT 40u	/* S086 P3: + osc1_position */
```

#### C16. `Core/DSP/Instruments/HiHat/HiHatParameters.c`

| Line | Action | Content |
|---|---|---|
| 256 | MODIFY | page 1 cell 4 → `HIHAT_PARAM_OSC1_POSITION`, with `/* S086 P3: coa fin wav pos (pos = osc1_position, cell 4). */` above |
| after 239 | ADD | the row comment block and the row |
| after 173 | ADD | `    HIHAT_PARAM_OSC1_POSITION,	/* S086 P3: appended; must stay last before COUNT (ID stability) */` |

#### C17. `Core/DSP/Instruments/HiHat/HiHatParameters.h`: MODIFY line 14

```c
#define HIHAT_PARAM_DESCRIPTOR_COUNT 40u	/* S086 P3: + osc1_position */
```

**Compile-time guards that catch a mistake in C10–C17:**
- `_Static_assert(*_PARAM_DESCRIPTOR_COUNT == *_PARAM_COUNT)` at
  Drum :175, Snare :173, Cymbal :174, HiHat :177;
- the row count must equal the enum, or `drum_param_descriptor_count` and
  the registry disagree.

---

## 4. Test bench

### C-T1. `tools/dsp_test/Makefile`: MODIFY line 252

- from: `--lines 71-102 --out $(GEN)/om_oscinfo.inc`
- to:   `--lines 71-124 --out $(GEN)/om_oscinfo.inc`

Add a comment line above the `osc_mod:` target:

```make
# S086 P3: OscInfo is 71-124 (C3 adds 22 lines: position, positionApplied).
```

`osc_mod` and `osc_stock` both use this fragment. Line 251 (defines 49-65)
does not move, because C2's include and asserts sit after the struct.

### C-T2. `tools/dsp_test/Makefile`: ADD after line 379 (end of file)

```make

# ---------------------------------------------------------------------------
# S086 P3: pos (S086_P3_WAV_OSC_POS_PARAMETER.md, S086_P4_SAMPLE_CYM_POS.md).
#
# What:       osc_pos            test_osc_pos.c P1-P7: the trigger offset, the
#                                per-block bend (identity, no drift, circular,
#                                continuity, non-phase waves) on the real
#                                table, sine and crash loops; built twice, with
#                                no cap (OSC_POS_BEND_CAP 64) and with cap 8
#                                (P7);
#             engine86_identity  the four engine .c files against frozen86/:
#                                removed/changed lines equal
#                                frozen86/engine_identity_allowed.txt (the P2
#                                osc_isTableWave lines); P3 only adds lines;
#             elf86_p2_snapshot  saves the P2 ELF dumps (run once, before P3);
#             elf86_p3_check     the oscillator functions and loops against the
#                                P2 dumps: P3 must not change any of them.
# Why:        S086 P3 §7: the loops do not change; the bend is exact.
# Inputs:     Oscillator.h/.c (extracted), frozen86/, build/lxr02.elf.
# Accessors:  make -C tools/dsp_test <target>.
# Affiliates: extract.py, elf_symdump.py, elf_loopdump.py, test_osc_stock.c.
# ---------------------------------------------------------------------------
OSC_POS86 := osc_isTableWave osc_positionIsPhase osc_positionTrigger osc_positionBend
.PHONY: osc_pos engine86_identity elf86_p2_snapshot elf86_p3_check

osc_pos: osc_mod
	$(EX) --src $(ROOT)/Core/DSPAudio/Oscillator.h $(call fn,$(OSC_POS86)) \
	  --out $(GEN)/op_helpers.c
	$(HOSTCC) $(HOSTFLAGS) test_osc_pos.c -o build/osc_pos -lm
	$(HOSTCC) $(HOSTFLAGS) -DOSC_POS_BEND_CAP=8 -DOSC_POS_TEST_CAP test_osc_pos.c \
	  -o build/osc_pos_cap -lm
	./build/osc_pos
	./build/osc_pos_cap

ENGINES86 := Core/DSP/Instruments/Drum/DrumVoice.c Core/DSP/Instruments/Snare/Snare.c \
             Core/DSP/Instruments/Cymbal/CymbalVoice.c Core/DSP/Instruments/HiHat/HiHat.c
engine86_identity: | $(GEN)
	@for f in $(ENGINES86); do \
	  diff $(FROZEN86)/$$f $(ROOT)/$$f | grep '^<' | sed "s|^|$$f:|" ; \
	done > build/engine86_removed.txt || true
	@if diff -q build/engine86_removed.txt $(FROZEN86)/engine_identity_allowed.txt >/dev/null; then \
	  echo "engine86_identity: OK (removed/changed lines = the allowed P2 lines)"; \
	else echo "engine86_identity: unexpected removed/changed lines:"; \
	  diff build/engine86_removed.txt $(FROZEN86)/engine_identity_allowed.txt; exit 1; fi

elf86_p2_snapshot:
	python3 elf_symdump.py --elf $(FW_ELF) $(foreach s,$(OSC86_ELF_SYMS),--sym $(s)) \
	  --out $(FROZEN86)/elf_osc_p2.txt
	python3 elf_loopdump.py --elf $(FW_ELF) $(foreach s,$(OSC86_ELF_SYMS),--sym $(s)) \
	  --out $(FROZEN86)/elf_loops_p2.txt

elf86_p3_check: | $(GEN)
	python3 elf_symdump.py --elf $(FW_ELF) $(foreach s,$(OSC86_ELF_SYMS),--sym $(s)) \
	  --out build/elf_osc86_now.txt
	python3 elf_symdump.py --compare $(FROZEN86)/elf_osc_p2.txt build/elf_osc86_now.txt
	python3 elf_loopdump.py --elf $(FW_ELF) $(foreach s,$(OSC86_ELF_SYMS),--sym $(s)) \
	  --out build/elf_loops86_now.txt
	python3 elf_loopdump.py --compare $(FROZEN86)/elf_loops_p2.txt build/elf_loops86_now.txt
```

### C-T3. `tools/dsp_test/test_osc_pos.c` (new)

**Header block:**

```c
/*
 * pos: trigger offset and per-block bend (S086 P3/P4 host gate).
 *
 * What:       runs the Oscillator.h pos helpers (extracted) with the real
 *             Sine, table and crash loops (om_plain.c), the way the engines
 *             call them (bend added before the render, removed after):
 *               P1 osc_positionTrigger(): pos << 25 for Sin Tri Saw Rec Cym
 *                  Re3 Sw3, 0 for Noi and user samples (waveforms 0..15, pos
 *                  0/1/64/127, also pos > 127 masked); positionApplied = pos.
 *               P2 identity: pos held at any value -> bend 0 every block, and
 *                  the blocks are bit-identical to rendering without helpers.
 *               P3 no drift: random pos sequences over 4,096 blocks on
 *                  calcSineBlock, calcWavetableOscBlock (sawTable) and
 *                  calcSampleOscBlock (crash): final phase == free-running
 *                  phase + (pos_final << 25) mod 2^32; phaseInc restored
 *                  exactly after every block.
 *               P4 circular: 127->0 = +1 step, 0->127 = -1, 0->64 = -64,
 *                  64->0 = -64; positionApplied lands on pos.
 *               P5 continuity: inside a bent block every sample equals an
 *                  independent per-sample reader at phase0 + k x (inc + bend)
 *                  (no jump at the block edge).
 *               P6 non-phase waves (NOISE, user sample 8): bend 0,
 *                  positionApplied follows pos.
 *               P7 (build with -DOSC_POS_TEST_CAP, cap 8): a step of s glides
 *                  over ceil(|s| / 8) blocks and lands exactly.
 * Why:        S086 P3 §7 and S086 P4 §4: the offset is exact, continuous and
 *             never drifts; pos 0 is today. Each case names the break that
 *             must make it fail (P3 §7 break proofs).
 * Inputs:     generated fragments (Makefile osc_pos, after osc_mod).
 * Outputs:    one line per failure (first 40); "osc_pos failures: N";
 *             exit 1 if N > 0.
 * Accessors:  make -C tools/dsp_test osc_pos.
 * Affiliates: Oscillator.h pos helpers; test_osc_stock.c (the host setup).
 */
```

**Body:**
- **Host setup:** copy `test_osc_stock.c` lines 36–85: the includes, the
  defines, the host tables (incl. `sq3Table`/`sw3Table`, which
  `om_stock.inc` names), the stubs, the two dispatcher prototypes, and the
  `#include`s of `om_defs.inc`, `om_sampleinfo.inc`, `om_oscinfo.inc`,
  `om_scratch.inc`, `om_stock.inc`, `om_plain.c`. Do not include
  `om_s085.c`, `os_fm.c` or `os_freq.c`; the pos tests need only the plain
  loops.
- **Then:**

  ```c
  #ifndef OSC_POS_BEND_CAP
  #define OSC_POS_BEND_CAP 64
  #endif
  #define OSC_POS_BEND_PER_STEP (0x2000000u / OUTPUT_DMA_SIZE)
  #include "op_helpers.c"
  ```

- **Fill the tables** with a smooth saw/sine and the real-shaped crash, so
  that P5 compares against an independent reader. The reader is written in
  the test as `itg = phase >> shift`, interpolated in double, then
  truncated, the same law as the loops.
- **P7** is compiled only under `OSC_POS_TEST_CAP`.

### C-T4. `tools/dsp_test/frozen86/engine_identity_allowed.txt` (new)

Exactly these four lines, which are the P2 changes:

```
Core/DSP/Instruments/Drum/DrumVoice.c:< 		else if(voice->osc.waveform > SINE && voice->osc.waveform <= REC)
Core/DSP/Instruments/Snare/Snare.c:< 	else if(voice->osc.waveform > SINE && voice->osc.waveform <= REC)
Core/DSP/Instruments/Cymbal/CymbalVoice.c:< 	else if(voice->osc.waveform > SINE && voice->osc.waveform <= REC)
Core/DSP/Instruments/HiHat/HiHat.c:< 	else if(voice->osc.waveform > SINE && voice->osc.waveform <= REC)
```

### C-T5. `tools/dsp_test/DSP_TEST.md`

| Line | Action |
|---|---|
| 168 | MODIFY `(155 rows, 0 mismatches)` → `(159 rows, 0 mismatches)` and add "(S086 P3: + four `osc1_position` rows)" |
| 199 | MODIFY the `special_tags` expected output to `special tags OK (159 rows, 0 mismatches)` |
| after 213 | ADD four rows: `osc_pos` (P1–P7, two builds, `osc_pos failures: 0` twice); `engine86_identity` (`OK`); `elf86_p2_snapshot` (run once before P3); `elf86_p3_check` (`0 missing, 0 changed`; loops: all exact or renamed, the noise fragments hand-checked as in P2) |

---

## 5. Documentation

| # | File | Lines | Action |
|---|---|---|---|
| C-D1 | `knowledge_files/specification_reference/FILESYSTEM_SPEC.md` | 1815–1818 | MODIFY the oscillator key list: add `osc1_position` after `osc1_pitch_fine`. Add a sentence: "`osc1_position` (S086 P3) is 0..127, the oscillator position (phase offset pos/128; Cym: of the sample). Absent in older files, which load 0 (unchanged sound); older firmware skips it." |
| — | `MEMORY.md` | Current working source | at the close: S086 P3/P4 status, build figures, hardware results |
| — | `knowledge_files/specification_reference/dsp_instruments_effects/INSTRUMENTS_DSP_REFERENCE.md` | oscillator section | at the close: the start-phase table plus `pos` (trigger and bend), the snc rule, the Cym rule |

---

## 6. Order of work and gates

| Step | Do | Gate |
|---|---|---|
| 1 | Step 0 (§1), with C-T2's `elf86_p2_snapshot` target added first | P2 figures reproduce |
| 2 | `Oscillator.h`, bottom-up: C1 (after 369), then C2 (after 102), then C3 (after 79) | `make all` builds, so the `_Static_assert`s pass |
| 3 | C4, C5 (crash guard) | builds; text +1 B (alignment may round) |
| 4 | C6–C9 (engines, each bottom-up: restore, apply, trigger) | builds; no new warnings in the four files |
| 5 | C10–C17 (tables and counts) | builds; the count asserts pass |
| 6 | `make all && make img` | flash ≈ P2 + a few hundred B; **ITCM 8,512** (unchanged; if it moves, find out why before going on); bss 430,296; DrumVoice 596 |
| 7 | C-T1, C-T3, C-T4, C-T5; `make -C tools/dsp_test osc_mod osc_stock osc_pos osc86_identity engine86_identity special_tags elf86_p3_check stock_tables` | all pass: `osc_pos failures: 0` ×2; 159 rows; elf `0 missing, 0 changed` against P2 |
| 8 | Break proofs (P3 §7), each applied, failing, reverted, and the sources confirmed byte-identical | each fails its named check |
| 9 | The rest of the bench (`selftest`, `pack`, `postchain`, `octave`, `mixer`, `filter`, `armcheck-*`) | unchanged |
| 10 | C-D1; write the P3 report | — |
| 11 | Hardware: P3 H1–H10, P4 C1–C4 | the user's results |

**If `elf86_p3_check` shows changes:** the engine edits sit outside the
oscillator symbols, but LTO may re-inline. Any change must be
register-renamed only (`elf_loopdump.py`), with no instruction added to a
per-sample loop. Otherwise stop and report.

---

## 7. Risks

| Risk | Mitigation |
|---|---|
| A bend left in `phaseInc` would persist through `osc_setFreq()`'s cache | the add and subtract are adjacent around the render in every engine; P3 checks restore exactness |
| An early `return` added later between add and subtract | none exists today in the four render sections; the comment blocks name the pairing |
| LTO changes an oscillator clone | `elf86_p3_check` against the P2 dumps (§6 step 7) |
| After a slot reset, `positionApplied` = 0 while the loaded `pos` ≠ 0, so the first block glides from 0 | harmless: the voice is silent until a trigger, and the trigger resets `positionApplied`; noted, not fixed |
| A `pos` byte above 127 (corrupt file) | masked with `& 127` in both helpers |
| The crash guard byte moves rodata | addresses only; the ELF gates compare normalised code |

---

## 8. Assessment (2026-10-28)

**Scope.** Checked independently against this schedule and against the P3/P4
plans:
- the full diff against `703941cb` (P2 + the P3 documents);
- a clean rebuild;
- a pristine P2 build from `git archive HEAD`;
- the whole bench;
- the break proofs;
- the linked-image comparisons.

No source file was changed by the assessment. The image rebuilt from a clean
tree is byte-identical to the delivered `build/LXRV2_lxr02.img`.

### 8.1 Verdict

**The implementation matches the schedule and is correct.**
- Every firmware change C1–C17 is present, in the right place, with a
  comment block.
- The oscillator code and every oscillator per-sample loop are
  byte-identical to P2.
- All host and ARM gates pass.

**Two things need attention before the work is called finished:**
- **F1:** LTO re-laid out `mixer_calcNextSampleBlock()`, which contains the
  inlined voice renders. Two inlined per-sample loops there are statically
  slightly dearer. The device G-C reading (H10) decides; a cheap mitigation
  is ready (§8.4).
- **F3:** a test gap lets a wrong bend constant in `Oscillator.h` pass.

### 8.2 Verified

| Item | Result | Evidence |
|---|---|---|
| C1 helpers, constants, divisibility assert | as scheduled; masks, circular difference in `uint32_t`, cap clamp, `positionApplied` advance | diff |
| C2 layout asserts, `<stddef.h>` after the struct | present; the ARM `sizeof == 72` assert passes | build |
| C3 `OscInfo` fields | 22 lines inserted; `} OscInfo;` at 124, matching `osc_mod --lines 71-124` | diff, `osc_mod` passes |
| Struct sizes | `OscInfo` 72; Drum/Snare/Cymbal/HiHat voices 596 / 420 / 476 / 484 B, all unchanged | ARM probe |
| C4/C5 crash guard | `crashSample` 32,769 B; guard 0x80 = entry 0 | diff, `nm` |
| C6–C9 engines | trigger line after each start-phase block; bend added and restored around exactly the `osc1` render; Drum `snc` calls the helper and zeroes the bend; modulators are never bent; no early return between add and restore | diff |
| C10–C17 tables | the enum entry is last before `*_COUNT`; the row is last and on one line; page 1 cell 4 = position (Snare `coa fin wav pos` / `noi mix`); every page row still has 16 cells; counts 40/39/40/40 | diff, script |
| Build | text 658,832 (+168 B on P2), data 424, bss 430,296, flash headroom 94,408, **ITCM 8,512 (unchanged)**; no warnings in the changed files | clean `make all` |
| P2 snapshot is genuine | `frozen86/elf_osc_p2.txt`, `elf_loops_p2.txt` are byte-identical to dumps of a fresh pristine P2 build | `make elf86_p3_check FW_ELF=<P2 build>` |
| Oscillator code unchanged | P3 against P2: 26 symbols, 0 changed; 61 loops, 61 exact; the dumps are byte-identical | `elf86_p3_check` |
| `armcheck-oscmod` | identical to `frozen86/armcheck_oscmod_baseline.txt` | diff |
| Host bench | `osc_mod` 0, `osc_stock` 0, `osc_pos` 0 ×2, `osc86_identity` OK, `engine86_identity` OK, `special_tags` 159/0, `stock_tables` C1–C4 OK, `selftest` 0 differing, `pack` 0, `postchain` 0, `octave` 16 / 2 ulps (documented), `mixer` 0, `armcheck-postchain` 3 MATCH, `armcheck-mixer` MATCH, `armcheck-filter` 81/39, `filter` S1 PASS (SDR 91.6896 / 86.5468) | full run |
| Docs | `FILESYSTEM_SPEC.md` key list and counts; `INSTRUMENTS_DSP_REFERENCE.md`; `MEMORY.md`; `DSP_TEST.md` rows | diff |

### 8.3 Findings

| # | Severity | Finding |
|---|---|---|
| **F1** | medium: decide at G-C | **The mixer's inlined loops moved.** The four render functions are inlined into `mixer_calcNextSampleBlock()` (flash, not ITCM), which grew 12,576 → 13,036 B (+460) as LTO re-allocated registers. `elf86_p3_check` covers only the oscillator symbols, so the schedule's "no per-sample loop changes" holds there and not for this function. Comparing its loops P2 → P3 by opcode sequence, most are identical. Two real per-sample loops are dearer: (1) an **inlined FM loop with a Sine carrier** (`ubfx #20`, the `calcFmSineBlock` body) now reloads **two literal-pool constants every sample** (+2, and the loads go to flash); (2) an **inlined noise loop** now reloads **one stack spill every sample** (+1). The other length differences are jump-table and setup fragments, not savings. Cause: `posBend` stays live in a register across the inlined render, which raises register pressure in this one very large function. It is not a source-level loop change. The dearest setting of each voice (the user-sample loops) is not among the affected loops, so rule (a) probably still holds. But rule (b) shows +1/+2, and the plan's claim of "nothing per sample" is not exact for these inlined copies. |
| **F2** | low (test) | **`test_osc_pos.c` has three gaps against schedule C-T3:** (a) **P2 identity** (held `pos` renders bit-identical to no helpers) is not implemented as its own case, although the report says it is; (b) **P1** samples `pos` 0, 17 … 119, so it never tests 127 or a value above 127 (the `& 127` mask); (c) **P1's expected value** is computed with `osc_positionIsPhase()` itself, so the wave set is not checked independently. Only P3 catches a missing wave, and only for Sine, Saw and Cym. |
| **F3** | low (test) | **The header's constants are untested.** `test_osc_pos.c` redefines `OSC_POS_BEND_PER_STEP` (and `OSC_POS_BEND_CAP`) itself, and the extracted helpers use the test's copy. The break proof "step size ×2 in `Oscillator.h`" therefore **passes** the bench, although on the device it would bend the pitch and leave the offset wrong. |
| F4 | info | **Break proofs** (schedule §6 step 8) were not recorded by the implementation. Run now on `Oscillator.h`: `pos << 24` fails P1; table waves dropped from `osc_positionIsPhase()` fails P3 (Saw); `CRASH` dropped fails P3 (Cym); linear difference fails P4; `positionApplied` not advanced fails P4; cap ignored fails P4 (cap build). **Step size ×2 passes (F3).** The source was restored byte-identical. |
| F5 | info | **Deviations from the schedule, all acceptable:** (a) the Snare edit replaced the `//calc next osc sample` comment instead of adding above it; this is documented in `engine_identity_allowed.txt` and `DSP_TEST.md`; (b) schedule error: C4 said the last crash data row ended with a comma, but it did not; the implementation correctly added one; (c) the comment blocks are reworded and shorter than the schedule's text but carry What/Why/Inputs/Outputs/Accessors/Affiliates. |
| F6 | info | **Trigger functions:** Drum/Cymbal/HiHat `*_triggerVoice.part.0` +20 B each. `Snare_triggerVoice.part.0` is 812 → 324 B, as LTO split the Snare trigger differently. These run per trigger, not per sample. No action. |

### 8.4 Recommended actions

1. **F3 (one line, firmware build).** Pin the constant where it is defined.
   In `Oscillator.h`, after `OSC_POS_BEND_PER_STEP`:

   ```c
   _Static_assert(OSC_POS_BEND_PER_STEP * OUTPUT_DMA_SIZE == (1u << 25),
                  "one pos step over one block must move exactly 1/128 cycle");
   ```

   In `test_osc_pos.c`, take the two `#define`s from the header (`extract.py
   --lines`) instead of redefining them. Re-run the "×2" break proof; it must
   then fail the build.
2. **F2.** In `test_osc_pos.c`:
   - add P2 as its own case;
   - make P1 cover `pos` 0, 1, 64, 127, 128, 255 against an **independent**
     expected wave set (`SINE TRI SAW REC CRASH SQ3 SW3` → `pos << 25`,
     masked; everything else → 0);
   - run P3 also for TRI, REC, SQ3, SW3.
3. **F1: measure first (H10, six-slot worst case, with Cymbal/HiHat carriers
   on Sine and Snare noise), then decide.**
   - **If the `cpu` reading is worse than P2,** remove the live register: keep
     the block's step `d` in `OscInfo` instead of a local. There are 3 free
     padding bytes after `waveInterpNext`, for example `int8_t positionBendStep`.
   - Split the helper into `osc_positionBendApply()`: compute `d`, store it,
     add `d × step` to `phaseInc`. Add `osc_positionBendRestore()`: subtract
     `positionBendStep × step`.
   - Nothing is then live across the inlined render. Re-run `elf86_p3_check`
     and the mixer loop comparison; the two loops should return to their P2
     form.
   - Extend the ELF gates to `mixer_calcNextSampleBlock`, with an
     opcode-sequence comparison: register- and offset-insensitive, so layout
     moves don't flag.
4. **Hardware:** P3 H1–H10 and P4 C1–C4 are unchanged. H10 now carries F1's
   decision.

### 8.5 Resources (P2 → P3)

| | P2 | P3 | Change |
|---|---:|---:|---:|
| text | 658,664 | 658,832 | +168 |
| flash headroom | 94,576 | 94,408 | −168 |
| ITCM | 8,512 | 8,512 | 0 |
| bss / data / DTCM | 430,296 / 424 / 4,472 | same | 0 |
| `OscInfo`; voices | 72; 596 / 420 / 476 / 484 | same | 0 |
| descriptors (D/S/C/H) | 39 / 38 / 39 / 39 | 40 / 39 / 40 / 40 | +1 each |
| `special_tags` rows | 155 | 159 | +4 |

---

## 9. Fixes after the assessment (2026-10-28)

All findings of §8.3 are fixed except F6, which needs no action. In this
section "P3 as delivered" means the tree assessed in §8.

**API change:** this section supersedes the helper API of C1 and the engine
code of C6–C9. `osc_positionBend()` (which returned the bend) is replaced by
`osc_positionBendApply(osc, bend)` / `osc_positionBendRestore(osc)`, which
keep the step in the oscillator.

### 9.1 F1: no live state across the inlined render

**Change:**

| File | What |
|---|---|
| `Core/DSPAudio/Oscillator.h` | new `int8_t positionBendStep` in the padding after `waveInterpNext`, with a full comment block; two new layout asserts (it follows `waveInterpNext`; `waveInterpFrac` does not move). `osc_positionBend()` replaced by `osc_positionBendApply(OscInfo*, uint8_t bend)` (computes the circular, capped step, advances `positionApplied`, stores the step and adds it to `phaseInc`; `bend = 0` or a non-phase wave stores 0 and only consumes the delta) and `osc_positionBendRestore(OscInfo*)` (subtracts the stored step from `phaseInc` and clears it). All related comment blocks rewritten |
| `DrumVoice.c` | `osc_positionBendApply(&voice->osc, modMode != DRUM_MOD_SNC)` before the mode switch; `osc_positionBendRestore()` after it. No local variable |
| `Snare.c`, `CymbalVoice.c`, `HiHat.c` | `osc_positionBendApply(&voice->osc, 1u)` before and `osc_positionBendRestore()` after the osc1 render. No local variable |

**Why it works:**
- The step is read back from the oscillator after the render.
- So no value has to stay in a register across the voice render that LTO
  inlines into `mixer_calcNextSampleBlock()`.
- That register pressure was what made two inlined per-sample loops reload
  values.

**Result, measured on the linked image** against the pristine P2 build:

| Loop in `mixer_calcNextSampleBlock()` | P2 | P3 as delivered | After the fix |
|---|---:|---:|---:|
| inlined FM loop, Sine carrier (`calcFmSineBlock` body) | 29 | 31 | **29** |
| inlined noise loops | 15 / 17 / 18 | 16 / … | **15 / 17 / 18** |
| per-sample do-while loops compared by `elf86_mixer_check` | — | 3 grew | **0 grew** (33 equal; 6 "shrank" are fragment-variant pairings) |

**The remaining textual differences** in that function are:
- register renaming and scheduling;
- cold saturation stubs that the extractor appends to fragments, with equal
  hot-path instruction multisets (§8 analysis, repeated);
- one extra stack store in a per-block setup path, which is not per sample.

**New permanent gate** (assessment recommendation 3):
- `tools/dsp_test/elf_hotloops.py` and the Makefile targets
  `elf86_mixer_snapshot` / `elf86_mixer_check`.
- **Baseline:** `frozen86/elf_mixer_p2.txt`, taken from the pristine P2
  build (`git archive 703941cb`).
- **What it checks:** it pairs the mixer's per-sample do-while loops with P2
  by opcode sequence and fails if one is longer.
- **Break proof:** on P3 as delivered it fails, naming the noise loop (15 → 16)
  and the FM-sine loop (29 → 31); on the fixed tree it passes.

### 9.2 F3: the header's constants are what the test compiles

- **`Oscillator.h`:** two new asserts in the constants block.
  - `OSC_POS_BEND_PER_STEP * OUTPUT_DMA_SIZE == 1 << 25`: one step over one
    block is exactly 1/128 cycle.
  - `OSC_POS_BEND_CAP` must be in 1..64, because `positionBendStep` is an
    `int8_t`.
- **`tools/dsp_test/Makefile` `osc_pos`:**
  - `op_consts.inc` is now cut from `Oscillator.h` (from
    `#ifndef OSC_POS_BEND_CAP` to the cap-range assert), with a check that
    the block was found;
  - `test_osc_pos.c` no longer defines the constants itself.
- **Break proof:** the "step size ×2" mutation, which passed in §8, now
  stops the build at the new assert, both in the firmware and in the host
  test.

### 9.3 F2: `test_osc_pos.c` rewritten for the new API

| Check | Now |
|---|---|
| P1 | waveforms 0–15 × `pos` 0, 1, 64, 127, 128, 255 against an **independent** truth table (Sin Tri Saw Rec Cym Re3 Sw3 → `(pos & 127) << 25`, else 0) |
| P2 | **new:** a held `pos` (16 random values × 7 phase waves × 8 blocks) renders bit-identically to the plain loop from start + `pos << 25`; `phaseInc` and `positionBendStep` untouched |
| P3 | no drift on **all seven** phase waves (Re3/Sw3 tables now hold distinct data, so a wrong table would show) |
| P4 | circular steps, the stored step, the `phaseInc` change, and the exact restore |
| P5 | continuity on all seven phase waves, plus "lands exactly on the offset" |
| P6 | non-phase waves: no bend, step 0, the delta is consumed, the restore is a no-op |
| P7 | cap build: a step of 40 moves 8 per block (checked each block) and lands |
| P8 | **new:** consume only (`bend = 0`, Drum `snc`): no bend, step 0, `positionApplied` follows |

**Break proofs**, each applied to `Oscillator.h` and reverted; the source was
confirmed byte-identical afterwards:

| Mutation | Fails |
|---|---|
| `pos << 24` at the trigger | P1 |
| the trigger's `& 127` mask removed | P1 (pos 128/255) |
| table waves dropped from `osc_positionIsPhase()` | P1 |
| `CRASH` dropped | P1 |
| linear instead of circular difference | P4 |
| `positionApplied` not advanced | P3 |
| step size ×2 | **build (assert)** |
| cap ignored | P4 (cap build) |
| Restore does not subtract | P3 |
| Restore keeps the step | P3 |
| the `bend` argument ignored | P8 |

### 9.4 Minors

- **F5a:** the original Snare `//calc next osc sample` line is back, directly
  above its render. `frozen86/engine_identity_allowed.txt` is again exactly
  the four P2 lines, and P3 only adds lines to the engines.
- **F5b:** the schedule's C4 text (§3) is corrected: the last crash data row
  had no trailing comma.
- **F5c, comment blocks:**
  - All P3 comment blocks now state what the code does after the fix: the
    four trigger blocks, the four render blocks, the `OscInfo` blocks, the
    constants block, the helpers, and the four descriptor-row blocks.
  - The row blocks were three compressed one-liners; they are now full
    What/Why/Inputs/Outputs/Accessors/Affiliates blocks.
  - No stale reference to `osc_positionBend()` remains.
- **Docs:** `DSP_TEST.md` rows for `osc_pos`, `engine86_identity`,
  `elf86_mixer_snapshot`, `elf86_mixer_check`; `MEMORY.md` (S086 P3 entry and
  build figures).

### 9.5 Verification (clean build of the fixed tree)

| Gate | Result |
|---|---|
| `make all && make img` | text **658,872** (+208 on P2, +40 on P3 as delivered), data 424, bss 430,296, flash headroom **94,368**, **ITCM 8,512** (unchanged), DTCM 4,472; no warnings in the changed files; image 659,296 B |
| Sizes | `OscInfo` 72; voices 596 / 420 / 476 / 484 (unchanged) |
| `elf86_p3_check` | 26 symbols, 0 changed; 61 loops, 61 exact (oscillator code still byte-identical to P2) |
| `elf86_mixer_check` | 0 grew |
| `armcheck-oscmod` | identical to the frozen baseline |
| `osc_mod`, `osc_stock`, `osc_pos` ×2 | 0 failures each |
| `osc86_identity`, `engine86_identity` | OK |
| `special_tags` | 159 rows, 0 mismatches |
| `stock_tables` | C1–C4 OK |
| `selftest`, `pack`, `postchain`, `mixer` | 0 differing |
| `octave` | 16 / 2 ulps (documented) |
| `armcheck-postchain`, `armcheck-mixer`, `armcheck-filter` | MATCH / MATCH / 81→39 |
| `filter` | S1 PASS (SDR 91.6896 / 86.5468) |

### 9.6 Still open

- **Hardware:** P3 H1–H10 and P4 C1–C4. H10 (G-C, the six-slot worst case)
  is still the CPU gate. The static evidence now shows no per-sample loop
  longer than P2, in the oscillator code or in the mixer.
