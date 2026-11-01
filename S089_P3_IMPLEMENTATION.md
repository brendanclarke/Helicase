# S089 P3 — Drum FM Depth Ramp Folded into the Typed FM Carriers: Implementation Schedule

**Plan:** `S089_P3_FM_RAMP_EFFICIENCY.md` (option B, three carriers, §5.1).
**Baseline:** HEAD `d944c87f` ("wavefolders complete; efficiency planning"),
whose image is byte-identical to `build/lxr02.bin` in the working tree. All
line numbers below are for that tree.
**Status:** implemented 2026-10-30 (§12); hardware checks H1–H6 pending.
**Pre-verified:**
every code block below was applied to a copy of HEAD in the session
scratchpad (`git archive HEAD`), built with the firmware flags (`make all`)
and measured, and its host test and break proofs were run (§11). The
numbers in this document are from that build, not estimates.
**Constraint:** CPU is at its ceiling (`S089_P2_WAVEFOLDER_CHAIN.md` §8,
binding). P3 only lowers cost. No loop that exists today changes, and no
voice, carrier or engine gets dearer.

---

## 0. Decisions applied (user, 2026-10-30)

| # | Decision | Applied as |
|---|---|---|
| B | Option B, three carriers (`slp`, `wtb` blend with its one-frame fallback, `smp`) | four ramped loop twins, one flash dispatcher, Drum's typed `fm` arm only (§4–§6) |
| D1 | ITCM / RAM expansion approved | +2,208 B ITCM in the new subsection `.itcm.s089p3` (13,032 → 15,240 / 16,384 B; 1,144 B left). 0 B SRAM, DTCM, `.data` or `.bss` (§11) |
| D2 | Hardware gate | no `cpu`-widget pass criterion: **the static per-sample counts are the gate** (§9.1). The user judges the improvement on hardware |
| D3 | The ramped dispatcher is `noinline`, in flash | `osc_renderTypedFmRamp()`: `noinline, noclone`, no section attribute (§4.1) |
| — | The copies are written out in full (no shared template) | the four originals are not edited; their machine code is unchanged (§9.1 gate 2) |
| — | `gain` stays a parameter of every twin | each twin matches its original except for the depth |
| — | `lastFmDepth` is updated on both arms | stored before each carrier call (§1 F7) |
| — | One S0 host law per covered carrier | `fmramp` group, law FR1 (§8) |

## 1. Findings from the deep-dive

The plan's intent holds. These findings make it simpler or correct its
details.

1. **The split already exists in the code.** Drum's `fm` branch already
   branches on `osc_isTyped()` (`DrumVoice.c:808`), and typed means exactly
   `smp`/`slp`/`wtb` (`Oscillator.h:783–787`, `OSC_TYPE_SMP..OSC_TYPE_WTB`).
   So option B is:
   - the typed arm calls the new ramped dispatcher, with no pass;
   - the untyped (stock `wav`) arm keeps today's pass and today's call.

   The plan's "two mechanisms in one branch" risk comes down to moving the
   pass into the `else`. The choice is a configuration (the oscillator
   type), never whether the ramp is armed, so the constant-cost mandate
   holds.
2. **The separate copies are required.** Cymbal (`CymbalVoice.c:429`) and
   HiHat (`HiHat.c:447`) call `osc_renderTypedFm()`, so the originals must
   stay as they are (plan §3.1).
3. **Measured baseline (the HEAD ELF, `elf_loopdump.py`).** The hot loops
   (closing on a conditional branch to their own start):

   | Loop | Path | Instructions per sample |
   |---|---|---:|
   | `osc_slpFmBlock` | inside the loop / lead-in | 50 / 39 |
   | `osc_smpFmBlock` | — | 42 |
   | `osc_wtbFmBlendBlock` | 16-bit / 8-bit frames | 46 / 42 |
   | `osc_wtbFmBlock` | 8-bit / 16-bit frames | 32 / 34 |
   | the depth-ramp pass (inlined in `mixer_calcNextSampleBlock`) | — | 10 |

4. **ITCM sizes.**
   - **The originals (bytes):** `slp` 604, `smp` 324, `wtb` blend 860,
     `wtb` 412.
   - **The twins as built:** 604 / 324 / **868** / 412, **2,208 B** in total
     (the blend twin's segment set-up is 8 B longer).
   - **Totals:** ITCM 13,032 → 15,240 B (including the three veneer stubs,
     which keep their 24 B).
5. **A plan correction.** In the plan's §5.1 table, the folded value for
   "`wtb` not blending" should read **63–65**, not "73–75":
   - folded = 32–34 + 1 + 30 (the crash modulator) = 63–65;
   - today = 32–34 + 10 + 30 = 72–74, which the plan prints as 74.
6. **`osc_renderTypedFm()` has no symbol of its own.** LTO inlines it into
   `mixer_calcNextSampleBlock()`, and the loops are reached through
   flash → ITCM veneers.
   - The new dispatcher is a real function (D3).
   - LTO still propagates the constant `size` = 32 into it (`movs r3, #32`),
     but it stays one symbol, `osc_renderTypedFmRamp`, 50 B of flash.
7. **`lastFmDepth` is stored before the carrier call on both arms.** The
   S088 code stored it before the render, and doing the same keeps
   `newDepth` from being live across the call (the S086 P3 rule,
   `INSTRUMENTS_DSP_REFERENCE.md` §12). A first draft that stored it after
   the call measured the same, but the rule is kept anyway.
8. **What the compiler makes of the ramp set-up (typed arm, verified in the
   disassembly):**
   - **Unarmed:** `dStep` is a literal `0.0f` from the literal pool
     (`newDepth − newDepth` folded under `-Ofast`), and `d0` is `fmMod`.
     So S0 holds by construction as well as by law FR1.
   - **Armed:** `d0 = vminnm(4·fmMod, lastFmDepth)` after a sign test, then
     `dStep = (fmMod − d0) × (1/31)`. There is no VDIV on this path; the
     S088 `lastFmDepth / newDepth` divide stays only on the stock arm.
9. **Armed-block difference (S2, an improvement).** Today the pass computes
   `(int16_t)(m × s)`, where the start ratio `s` can be up to 4. When
   `lastFmDepth > fmMod` and the modulator is near full scale, that product
   leaves the int16 range and the store wraps. The twins never form that
   int16: `mod × d` goes straight into the existing `__SSAT(…, 16)`, so it
   saturates instead. This only occurs on rare armed blocks, and it removes
   a wrap. The stock arm keeps today's behaviour (unchanged, S0).
10. **ITCM veneer stubs.** The linker lays out its ITCM → flash veneers
    (`osc_wtbPlanBlock`, `memset`, `osc_userBind`) after the last ITCM
    subsection, so they move from after `.itcm.s089` to after
    `.itcm.s089p3`. They are stubs, not loops. P2 accepted the same move
    (`S089_P2_IMPLEMENTATION.md` §17, step 7). No ITCM function moves.

---

## 2. Order of work and gates

| Step | What | Gate after the step |
|---|---|---|
| 1 | Linker: name `.itcm.s089`, append `.itcm.s089p3` (§3) | `build/lxr02.bin` byte-identical to the baseline (verified) |
| 2 | `Oscillator.c` S089 P3 block and `Oscillator.h` declaration (§4.1, §5) | builds; the new code is unreferenced, so `--gc-sections` drops it and the image is identical |
| 3 | Drum `fm` branch (§6.1) | builds; ITCM 15,240 B; §9.1 gates 1–6 |
| 4 | Comment-only edits (§4.2, §6.2, §7) | `build/lxr02.bin` byte-identical to step 3 (verified) |
| 5 | Host tests (§8) | `oscillator`: every group 0 failures, including `fmramp`; both break builds caught |
| 6 | Hardware (§9.3) | the user's listening and load check |

---

## 3. Linker script: pin `.itcm.s089`, append `.itcm.s089p3`

**File:** `STM32F765VIHx_FLASH.ld`, **lines 150–170. MODIFY** (the comment
block and the input-order line).

```diff
         /*
-         * ITCM input order (S089 P2).
+         * ITCM input order (S089 P2; S089 P3).
          *
          * What:       .itcm first, then the named S085 and S087 subsections
          *             (and their linker-generated .itcm.s087.* stubs) in
-         *             their current order, then every other .itcm.*
-         *             subsection, which from S089 P2 is .itcm.s089.
+         *             their current order, then .itcm.s089 (S089 P2, the
+         *             voiceFold loops), then .itcm.s089p3 (S089 P3, the Drum
+         *             ramped FM carriers), then any other .itcm.*
+         *             subsection.
          * Why:        a bare *(.itcm.*) lays subsections out in LTRANS
          *             partition order, so a new subsection in another
          *             partition could land before .itcm.s085 and move
          *             every existing ITCM loop, changing their short (b.n)
          *             tail branches. That is the S085 failure this rule
          *             exists to prevent (Oscillator.c S085 ITCM note).
          *             Naming the existing subsections keeps today's
          *             addresses exactly; new code is always appended.
+         *             S089 P3: .itcm.s089 was reached only through the
+         *             catch-all; it is named so that .itcm.s089p3 always
+         *             follows it. The exact pattern *(.itcm.s089) does not
+         *             match .itcm.s089p3.
          * Check:      with no other change the image must be byte-identical
-         *             to the previous build (S089 P2 schedule step 1).
+         *             to the previous build (S089 P2 schedule step 1; S089
+         *             P3 schedule step 1). The linker's ITCM -> flash veneer
+         *             stubs are laid out after the last subsection, so they
+         *             move when a subsection is added; no function moves.
          * Affiliates: Oscillator.c OSC_S085_SECTION / OSC_S087_SECTION;
-         *             voiceFold.c VOICE_FOLD_SECTION; the ASSERT below.
+         *             Oscillator.c OSC_S089P3_SECTION; voiceFold.c
+         *             VOICE_FOLD_SECTION; the ASSERT below.
          */
-        *(.itcm) *(.itcm.s085) *(.itcm.s087 .itcm.s087.*) *(.itcm.*)
+        *(.itcm) *(.itcm.s085) *(.itcm.s087 .itcm.s087.*) *(.itcm.s089) *(.itcm.s089p3) *(.itcm.*)
```

**Verified:** with only the input-order line changed, the relinked
`build/lxr02.bin` is byte-identical to the baseline, and the map shows
`*(.itcm.s089)` then `*(.itcm.s089p3)` (empty) in that order.

---

## 4. `Core/DSPAudio/Oscillator.c`

### 4.1 ADD: the S089 P3 block at the end of the file

**After line 3257** (the closing `}` of `osc_wtbSyncBlendBlock()`, the last
line of the file). **ADD** the block below.
- **Why the end of the file:** every helper it uses is defined above it,
  so no forward declaration is needed:
  - `osc_zeroBlock()`, `osc_smpReadPhase()`, `osc_frac12()` and
    `osc_bindGain()` (1867–1892);
  - `osc_slpBlockInc()` (1919);
  - `osc_wtbRead()`, `osc_wtbFrame()` and `osc_wtbFrameOf()` (2716–2751);
  - `osc_wtbPlan_t` and `osc_wtbPlanBlock()` (≈2784–2830).
- **Twins:** each loop is its original with exactly one change. The depth
  `osc->fmMod` becomes the local `d`, stepped once per sample after its use.

```c
/*
 * ===========================================================================
 * Drum FM depth-ramp carriers (S089 P3).
 * ===========================================================================
 *
 * What:       ramped twins of the four typed FM carrier loops (smp, slp, wtb
 *             blend and its one-frame wtb fallback) and the flash dispatcher
 *             osc_renderTypedFmRamp() that selects among them. Each twin is
 *             its original with exactly one change: the loop-invariant FM
 *             depth osc->fmMod becomes a per-sample depth d that starts at
 *             d0 and moves by dStep after every sample (d_i = d0 + i x dStep,
 *             accumulated). Everything else - the read rule, the clamps, the
 *             phase advance, the gain, the zero-block guards - is copied
 *             unchanged.
 * Why:        Drum's fm branch used to scale the whole modulator block by a
 *             per-sample ramp (S088 P0 Layer 2e) before the carrier read it:
 *             a separate 10-instruction pass on every block, although the
 *             ramp only moves on the one block after a fre/itp retrigger.
 *             Folding the ramp into the carrier's existing depth multiply
 *             costs one vadd per sample instead, so Drum fm with an smp, slp
 *             or wtb carrier drops by about 9 instructions per sample (the
 *             dearest carriers: slp 90 -> 81, wtb blend 86 -> 77, smp 82 ->
 *             73; S089_P3_FM_RAMP_EFFICIENCY.md §5.1). The CPU is at its
 *             ceiling (S089_P2_WAVEFOLDER_CHAIN.md §8), so this is a saving
 *             that lowers the worst case.
 * Rules:      - The originals stay byte-identical: Cymbal and HiHat call them
 *               through osc_renderTypedFm(), and an added per-sample vadd
 *               would cost them +1 for nothing (the ceiling rule). New
 *               per-sample code is a new function, never an edit to an
 *               existing loop (S087 rule).
 *             - Unarmed blocks (every rst block and almost every other):
 *               the caller passes d0 = osc->fmMod and dStep = 0.0f, so d is
 *               exactly fmMod on every sample (x + 0.0f == x) and every
 *               product is the same expression as the original's:
 *               bit-identical output (S0; host law FR1).
 *             - Armed blocks: S2. The modulator is no longer truncated to
 *               int16 after the ramp scale (less quantisation), and a ramp
 *               start above 1 x fmMod now saturates in the existing __SSAT
 *               instead of wrapping in the old (int16_t) cast.
 *             - Keep each twin in step with its original: a fix to one is
 *               a fix to both (the originals name their twin in Affiliates).
 * Placement:  the four loops in .itcm.s089p3, pinned after .itcm.s089 in
 *             STM32F765VIHx_FLASH.ld so no existing ITCM function moves
 *             (S085 rule); the dispatcher runs once per block from flash.
 * Accessors:  Drum_calcVoiceSyncBlock() (fm branch, typed carrier) only.
 * Affiliates: osc_smpFmBlock(), osc_slpFmBlock(), osc_wtbFmBlock(),
 *             osc_wtbFmBlendBlock() (the originals); osc_renderTypedFm()
 *             (their dispatcher, still used by Cymbal/HiHat); DrumVoice.c
 *             lastFmDepth / ampRampArm; tools/dsp_test/modules/oscillator.c
 *             group fmramp (FR1-FR3).
 */

/*
 * S089 P3 ITCM placement.
 *
 * What:       .itcm.s089p3 for the four ramped carrier loops; noinline and
 *             noclone keep each loop its own symbol (elf_loopdump.py measures
 *             it by name) and stop LTO from folding it into a caller or
 *             cloning it per call site (as OSC_S087_LOOP).
 * Why:        S087 P0 §2 rules 2-3 and the S089 P2 linker pin: a new
 *             subsection is appended after every existing one, so the S085,
 *             S087 and S089 P2 loops keep their addresses. The name must not
 *             match the exact *(.itcm.s089) pattern (voiceFold.c).
 * Accessors:  osc_smpFmRampBlock(), osc_slpFmRampBlock(),
 *             osc_wtbFmRampBlock(), osc_wtbFmBlendRampBlock().
 * Affiliates: OSC_S085_SECTION, OSC_S087_SECTION, voiceFold.c
 *             VOICE_FOLD_SECTION; STM32F765VIHx_FLASH.ld ITCM input order;
 *             link_budget.py.
 */
#define OSC_S089P3_SECTION __attribute__((section(".itcm.s089p3")))
#define OSC_S089P3_LOOP    OSC_S089P3_SECTION __attribute__((noinline, noclone))

/*
 * smp FM carrier with a per-sample depth ramp (S089 P3; twin of
 * osc_smpFmBlock()).
 *
 * What:       the one-shot read at ph + (ssat(mod x d) << 9), clamped to
 *             [0, endPh], with d = d0 on the first sample and d += dStep
 *             after each sample. The phase advance is the plain one.
 * Why:        the Drum FM depth ramp folded into the carrier's own depth
 *             multiply (see the block comment above).
 * Inputs:     osc (bound smp: sampleCacheData/Size/Index, phase, phaseInc),
 *             mod[size] (the modulator block at fmModAmount, unscaled by any
 *             ramp), size, gain, d0 (depth on sample 0), dStep (depth change
 *             per sample; 0.0f when not armed).
 * Outputs:    buf[size]; osc->phase.
 * Cost:       the original's 42 instructions per sample + 1 (the vadd).
 * Accessors:  osc_renderTypedFmRamp().
 * Affiliates: osc_smpFmBlock() (keep in step); osc_smpReadPhase(),
 *             osc_frac12(), osc_bindGain().
 */
static OSC_S089P3_LOOP void osc_smpFmRampBlock(OscInfo* osc,
                                               const int16_t *mod,
                                               int16_t *buf, uint8_t size,
                                               float gain, float d0,
                                               float dStep)
{
	const int16_t *data = (const int16_t *)osc->sampleCacheData;
	/* The last valid read position: the zero pad after the item. */
	const uint32_t endPh = osc->sampleCacheSize << OSC_SAMPLE_PHASE_SHIFT;
	/* Render gain x the item's stored gain code, once per block. */
	const float g = gain * osc_bindGain(osc);
	uint32_t ph = osc->phase;
	const uint32_t inc = osc->phaseInc;
	/* The ramped FM depth: d0 here, d0 + i x dStep at sample i. */
	float d = d0;
	uint8_t i;

	if (!data || osc->sampleCacheSize == 0u) {
		osc_zeroBlock(buf, size);
		return;
	}
	for (i = 0u; i < size; i++) {
		/*
		 * FM read offset: modulator x this sample's depth, saturated to
		 * int16 and scaled to read-phase units (<< 9, the S087 A17 scale
		 * shared with every typed carrier). Unarmed, d == fmMod exactly,
		 * so this is the original's expression bit for bit.
		 */
		const int32_t off = (int32_t)__SSAT(
			(int32_t)((float)mod[i] * d), 16) << OSC_FM_SAMPLE_SHIFT;
		int32_t rp = (int32_t)ph + off;
		uint32_t idx;
		int16_t a;
		int16_t b;
		float out;

		/* Step the depth after its use: sample 0 reads d0 and sample
		 * size - 1 reads d0 + (size - 1) x dStep = the block's fmMod. */
		d += dStep;
		/* One-shot clamp: never before the start, never past the pad. */
		if (rp < 0)
			rp = 0;
		else if ((uint32_t)rp > endPh)
			rp = (int32_t)endPh;
		idx = (uint32_t)rp >> OSC_SAMPLE_PHASE_SHIFT;
		a = data[idx];
		b = data[idx + 1u];
		/* Linear interpolation on the 12-bit phase fraction. */
		out = (float)a + osc_frac12((uint32_t)rp) * (float)(b - a);
		buf[i] = (int16_t)(out * g);
		ph = osc_smpReadPhase(ph + inc, endPh);
	}
	osc->phase = ph;
}

/*
 * slp FM carrier with a per-sample depth ramp (S089 P3; twin of
 * osc_slpFmBlock()).
 *
 * What:       the looped read at phase + (ssat(mod x d) << 9), with d = d0 on
 *             the first sample and d += dStep after each sample. The read
 *             rule is chosen once per block, from where the block starts:
 *             - inside the loop (phase >= loop_start): the offset is bounded
 *               to half a loop and the read, relative to loop_start, wraps
 *               once each way; the index is loop_start + that point;
 *             - in the lead-in (phase < loop_start): the absolute read is
 *               clamped to [0, loop_end - 1].
 *             The seam reads loop_start. The phase takes the per-block step
 *             from osc_slpBlockInc() and wraps once at loop_end.
 * Why:        the Drum FM depth ramp folded into the carrier's own depth
 *             multiply (see the block comment above). slp is the dearest
 *             Drum fm carrier (50 + 30 modulator + 10 pass = 90 before).
 * Inputs:     osc (bound slp), mod[size], size, gain, d0, dStep (as
 *             osc_smpFmRampBlock()).
 * Outputs:    buf[size]; osc->phase.
 * Edge:       a block that starts in the lead-in and enters the loop keeps
 *             the lead-in read rule (clamp) for its remaining samples (as the
 *             original).
 * Cost:       the original's 50 (in the loop) / 39 (lead-in) instructions per
 *             sample + 1.
 * Accessors:  osc_renderTypedFmRamp().
 * Affiliates: osc_slpFmBlock() (keep in step); osc_slpBlockInc(),
 *             sampleMemory_sampleEntry() (loop points).
 */
static OSC_S089P3_LOOP void osc_slpFmRampBlock(OscInfo* osc,
                                               const int16_t *mod,
                                               int16_t *buf, uint8_t size,
                                               float gain, float d0,
                                               float dStep)
{
	const int16_t *data = (const int16_t *)osc->sampleCacheData;
	const sample_catalogue_entry_t *entry =
		sampleMemory_sampleEntry(osc->sampleCacheIndex);
	/* Loop region in frames; a missing entry loops the whole item. */
	const uint32_t ls = entry ? entry->loop_start : 0u;
	const uint32_t le = entry ? entry->loop_end : osc->sampleCacheSize;
	const uint32_t len = le - ls;
	/* The same region in read-phase units (frames << 12). */
	const uint32_t lenPh = len << OSC_SAMPLE_PHASE_SHIFT;
	const uint32_t loopStartPh = ls << OSC_SAMPLE_PHASE_SHIFT;
	const uint32_t loopEndPh = le << OSC_SAMPLE_PHASE_SHIFT;
	const float g = gain * osc_bindGain(osc);
	uint32_t ph = osc->phase;
	/* The ramped FM depth: d0 here, d0 + i x dStep at sample i. */
	float d = d0;
	uint32_t inc;
	uint8_t i;

	if (!data || osc->sampleCacheSize == 0u || len == 0u) {
		osc_zeroBlock(buf, size);
		return;
	}
	/* Per-block step: forward modulo the loop (a backward pos glide is a
	 * forward wrap), so the per-sample wrap below needs one compare. */
	inc = osc_slpBlockInc(osc->phaseInc, ph, loopStartPh, lenPh);
	if (ph >= loopStartPh) {
		const int32_t loopLen = (int32_t)lenPh;
		/* Half a loop: the largest offset that still wraps only once. */
		const int32_t half = loopLen >> 1;

		for (i = 0u; i < size; i++) {
			/* FM read offset at this sample's depth (S0 when d == fmMod). */
			int32_t off = (int32_t)__SSAT(
				(int32_t)((float)mod[i] * d), 16) << OSC_FM_SAMPLE_SHIFT;
			int32_t rp;
			uint32_t idx;
			uint32_t next;
			int16_t a;
			int16_t b;
			float out;

			/* Depth steps after its use (sample 0 = d0). */
			d += dStep;
			/* Bound the offset to +/- half a loop, then wrap the read
			 * point (relative to loop_start) once in either direction. */
			off = (off > half) ? half : off;
			off = (off < -half) ? -half : off;
			rp = (int32_t)(ph - loopStartPh) + off;
			rp = (rp < 0) ? rp + loopLen : rp;
			rp = (rp >= loopLen) ? rp - loopLen : rp;
			idx = ls + ((uint32_t)rp >> OSC_SAMPLE_PHASE_SHIFT);
			/* The seam interpolates toward loop_start, not loop_end. */
			next = (idx + 1u == le) ? ls : idx + 1u;
			a = data[idx];
			b = data[next];
			out = (float)a + osc_frac12((uint32_t)rp) * (float)(b - a);
			buf[i] = (int16_t)(out * g);
			ph += inc;
			if (ph >= loopEndPh)
				ph -= lenPh;
		}
	} else {
		/* The last readable point before loop_end (lead-in clamp). */
		const int32_t lastPh = (int32_t)loopEndPh - 1;

		for (i = 0u; i < size; i++) {
			/* FM read offset at this sample's depth (S0 when d == fmMod). */
			const int32_t off = (int32_t)__SSAT(
				(int32_t)((float)mod[i] * d), 16) << OSC_FM_SAMPLE_SHIFT;
			int32_t rp = (int32_t)ph + off;
			uint32_t idx;
			uint32_t next;
			int16_t a;
			int16_t b;
			float out;

			/* Depth steps after its use (sample 0 = d0). */
			d += dStep;
			/* Lead-in: absolute read clamped to [0, loop_end - 1]. */
			rp = (rp < 0) ? 0 : rp;
			rp = (rp > lastPh) ? lastPh : rp;
			idx = (uint32_t)rp >> OSC_SAMPLE_PHASE_SHIFT;
			next = (idx + 1u == le) ? ls : idx + 1u;
			a = data[idx];
			b = data[next];
			out = (float)a + osc_frac12((uint32_t)rp) * (float)(b - a);
			buf[i] = (int16_t)(out * g);
			ph += inc;
			if (ph >= loopEndPh)
				ph -= lenPh;
		}
	}
	osc->phase = ph;
}

/*
 * One-frame wavetable FM carrier with a per-sample depth ramp (S089 P3;
 * twin of osc_wtbFmBlock()).
 *
 * What:       reads the nearest pos frame at ph + (ssat(mod x d) << 19), the
 *             table-FM fraction of a cycle, with d = d0 on the first sample
 *             and d += dStep after each sample.
 * Why:        osc_wtbFmBlendRampBlock() falls back to this when it has no
 *             interpolation slot or the table has one frame. Without a ramped
 *             fallback those blocks would need the old modulator pass, two
 *             mechanisms on one path (S089 P3 plan §5.1).
 * Inputs:     osc (bound wtb: sampleCacheIndex/Data, positionApplied, phase,
 *             phaseInc), mod[size], size, gain, d0, dStep.
 * Outputs:    buf[size]; osc->phase.
 * Cost:       the original's 32 (8-bit) / 34 (16-bit) instructions per
 *             sample + 1.
 * Accessors:  osc_wtbFmBlendRampBlock() (fallback).
 * Affiliates: osc_wtbFmBlock() (keep in step); osc_wtbFrameOf(),
 *             osc_wtbFrame(), osc_wtbRead().
 */
static OSC_S089P3_LOOP void osc_wtbFmRampBlock(OscInfo* osc,
                                               const int16_t* mod,
                                               int16_t* buf, uint8_t size,
                                               float gain, float d0,
                                               float dStep)
{
	const wavetable_catalogue_entry_t *wt =
		sampleMemory_wavetableEntry(osc->sampleCacheIndex);
	uint32_t ph = osc->phase;
	const uint32_t inc = osc->phaseInc;
	/* The ramped FM depth: d0 here, d0 + i x dStep at sample i. */
	float d = d0;
	uint8_t i;

	if (!wt || !osc->sampleCacheData) {
		osc_zeroBlock(buf, size);
		return;
	}
	{
		/* The nearest frame to pos (no blend in the fallback). */
		const uint32_t frame = osc_wtbFrameOf(osc->positionApplied,
		                                      wt->frame_count);
		const uint8_t *base = osc_wtbFrame(osc, wt, frame);

		for (i = 0u; i < size; i++) {
			/* Read phase + FM offset: ssat(mod x d) << 19 is the same
			 * fraction of a cycle as the table-wave FM (S087 A17). */
			const uint32_t rp = ph + ((uint32_t)__SSAT(
				(int32_t)((float)mod[i] * d), 16) << 19);

			/* Depth steps after its use (sample 0 = d0). */
			d += dStep;
			buf[i] = (int16_t)(osc_wtbRead(base, wt->bits, wt->frame_len, rp) *
			                   gain);
			ph += inc;
		}
	}
	osc->phase = ph;
}

/*
 * Wavetable FM carrier with frame blending and a per-sample depth ramp
 * (S089 P3; twin of osc_wtbFmBlendBlock()).
 *
 * What:       the S087 P1 frame plan (osc_wtbPlanBlock(): one or two
 *             segments, each blending one frame pair with ff stepping by
 *             plan.step); both frames are read at ph + (ssat(mod x d) << 19),
 *             with d = d0 on the first sample and d += dStep after each
 *             sample. d runs across the segment boundary unchanged: it is a
 *             per-sample ramp over the whole block, not per segment.
 *             Without a slot, or with one frame, the ramped one-frame copy
 *             runs with the same d0/dStep.
 * Why:        the Drum FM depth ramp folded into the carrier's own depth
 *             multiply (see the block comment above).
 * Inputs:     osc (bound wtb), mod[size], size, gain, d0, dStep.
 * Outputs:    buf[size]; osc->phase; one interpolation slot claimed for this
 *             block (modNode_interpClaim()), exactly as the original.
 * Cost:       the original's 46 (16-bit) / 42 (8-bit) instructions per
 *             sample + 1.
 * Accessors:  osc_renderTypedFmRamp().
 * Affiliates: osc_wtbFmBlendBlock() (keep in step); osc_wtbPlanBlock(),
 *             osc_wtbFmRampBlock() (fallback), modNode_interpClaim().
 */
static OSC_S089P3_LOOP void osc_wtbFmBlendRampBlock(OscInfo* osc,
                                                    const int16_t* mod,
                                                    int16_t* buf,
                                                    uint8_t size, float gain,
                                                    float d0, float dStep)
{
	const wavetable_catalogue_entry_t *wt =
		sampleMemory_wavetableEntry(osc->sampleCacheIndex);
	uint32_t ph = osc->phase;
	const uint32_t inc = osc->phaseInc;
	const uint32_t count = wt ? wt->frame_count : 0u;
	osc_wtbPlan_t plan;
	/* The ramped FM depth: d0 here, d0 + i x dStep at sample i. */
	float d = d0;
	uint8_t i = 0u;
	uint8_t s;

	/* The claim is the last test, so a slot is only taken when the blend
	 * really runs (as the original). */
	if (!wt || !osc->sampleCacheData || count < 2u ||
	    !modNode_interpClaim(osc)) {
		osc_wtbFmRampBlock(osc, mod, buf, size, gain, d0, dStep);
		return;
	}
	{
		const uint8_t bits = wt->bits;
		const uint32_t len = wt->frame_len;

		osc_wtbPlanBlock(osc, count, size, &plan);
		/* i runs on across the segments: segment s ends at plan.end[s]. */
		for (s = 0u; s < plan.segments; s++) {
			const uint8_t *a = osc_wtbFrame(osc, wt, plan.pair[s]);
			const uint8_t *b = osc_wtbFrame(osc, wt, plan.pair[s] + 1u);
			float ff = plan.ff[s];

			for (; i < plan.end[s]; i++) {
				/* One FM read point for both frames (S0 when d == fmMod). */
				const uint32_t rp = ph + ((uint32_t)__SSAT(
					(int32_t)((float)mod[i] * d), 16) << 19);
				const float x0 = osc_wtbRead(a, bits, len, rp);
				const float x1 = osc_wtbRead(b, bits, len, rp);
				/* Frame blend at this sample's fractional position. */
				const float out = x0 + ff * (x1 - x0);

				/* Depth steps after its use (sample 0 = d0). */
				d += dStep;
				buf[i] = (int16_t)(out * gain);
				ff += plan.step;
				ph += inc;
			}
		}
	}
	osc->phase = ph;
}

/*
 * Typed FM render with a per-sample depth ramp (S089 P3; see the header).
 *
 * What:       selects the ramped carrier by the oscillator's bind kind, as
 *             osc_renderTypedFm() does for the originals: smp, slp, wtb
 *             (8- and 16-bit, blend with its one-frame fallback); anything
 *             else (no item installed) renders a zero block.
 * Why:        one per-block entry for Drum's fm branch. noinline/noclone and
 *             no section attribute: it runs from flash once per block, and
 *             keeping it out of mixer_calcNextSampleBlock() (where the Drum
 *             render is inlined) means the change adds one call there, not a
 *             switch of three calls that LTO would re-allocate registers
 *             around (the S086 P3 failure). The call costs a few cycles per
 *             block (well under 1 per sample).
 * Inputs:     osc (typed: osc_isTyped()), mod[size], buf, size, gain, d0,
 *             dStep.
 * Outputs:    buf[size]; osc->phase (and a wtb interpolation slot claim).
 * Accessors:  Drum_calcVoiceSyncBlock() (fm branch, typed carrier).
 * Affiliates: osc_renderTypedFm() (the unramped dispatcher; keep the cases
 *             in step); OSC_BIND_*.
 */
void __attribute__((noinline, noclone)) osc_renderTypedFmRamp(
	OscInfo* osc, const int16_t* mod, int16_t* buf, uint8_t size,
	float gain, float d0, float dStep)
{
	switch (osc->sampleCacheLooped) {
	case OSC_BIND_SMP:
		osc_smpFmRampBlock(osc, mod, buf, size, gain, d0, dStep);
		break;
	case OSC_BIND_SLP:
		osc_slpFmRampBlock(osc, mod, buf, size, gain, d0, dStep);
		break;
	case OSC_BIND_WTB8:
	case OSC_BIND_WTB16:
		osc_wtbFmBlendRampBlock(osc, mod, buf, size, gain, d0, dStep);
		break;
	default:
		osc_zeroBlock(buf, size);
		break;
	}
}
```

**Notes for review:**
- **Where the step sits.** `d += dStep` follows the depth's single use in
  each loop, so sample 0 reads `d0` and sample `size − 1` reads
  `d0 + (size − 1) × dStep`. Law FR2 pins this, and break `fmr2` (the depth
  stepped before its first use) must fail it.
- **The `slp` twin keeps both read rules,** each with its own `d` step. Only
  one rule runs per block.
- **The blend twin carries `d` across the segment boundary.** The ramp
  spans the whole block, not each segment.
- **The fallback.** `osc_wtbFmBlendRampBlock()` calls
  `osc_wtbFmRampBlock()` with the same `d0`/`dStep`. The call is a short
  branch inside ITCM.

### 4.2 MODIFY: comment-only twin notes on the originals

The originals' code does not change. Each comment block names its twin, so
a later fix to one loop is made to both. **Verified:** with these edits the
image is byte-identical (§2 step 4).

**Line 2034** (`osc_smpFmBlock()` comment). MODIFY:

```diff
- * Affiliates: calcFmBlock(); test_osc_sample.c.
+ * Affiliates: calcFmBlock(); test_osc_sample.c; osc_smpFmRampBlock() (the
+ *             S089 P3 Drum twin with a per-sample depth ramp: a change to
+ *             this loop is made to the twin too).
```

**Lines 2095–2096** (`osc_slpFmBlock()` comment). MODIFY:

```diff
  * Affiliates: osc_slpBlockInc(), osc_slpPlainBlock(), osc_smpFmBlock() (the
- *             same FM scale, A17 addendum).
+ *             same FM scale, A17 addendum); osc_slpFmRampBlock() (the S089
+ *             P3 Drum twin with a per-sample depth ramp: a change to this
+ *             loop is made to the twin too).
```

**Line 2429** (`osc_renderTypedFm()`). ADD before it:

```c
/* S089 P3: osc_renderTypedFmRamp() (end of file) is this dispatcher's Drum
** twin with a per-sample FM depth ramp; keep their cases in step. Cymbal and
** HiHat keep calling this one (no ramp, no extra per-sample cost). */
```

**Lines 2964–2965** (the "Wavetable FM copies" comment above
`osc_wtbFmBlock()`). MODIFY:

```diff
  * Why:        A17 addendum: amt means the same on every type.
  * Accessors:  osc_renderTypedFm().
+ * Affiliates: osc_wtbFmRampBlock() / osc_wtbFmBlendRampBlock() (the S089 P3
+ *             Drum twins with a per-sample depth ramp: a change to these
+ *             loops is made to the twins too).
  */
```

**Lines 3008–3009** (`osc_wtbFmBlendBlock()` comment). MODIFY:

```diff
  * Affiliates: osc_wtbPlanBlock(), osc_wtbFmBlock(), calcFmBlock() (the same
- *             FM scale), modNode_interpClaim().
+ *             FM scale), modNode_interpClaim(); osc_wtbFmBlendRampBlock()
+ *             (the S089 P3 Drum twin with a per-sample depth ramp: a change
+ *             to this loop is made to the twin too).
```

---

## 5. `Core/DSPAudio/Oscillator.h`

**After line 890** (`void osc_renderUserList(…);`, the last of the S087 D
typed render entries). **ADD:**

```c
/*
 * Typed FM render with a per-sample depth ramp (S089 P3).
 *
 * What:       renders a typed carrier (smp, slp, wtb) with FM, exactly as
 *             osc_renderTypedFm() except that the FM depth is not the
 *             loop-invariant osc->fmMod: sample i uses d0 + i x dStep
 *             (accumulated in float, stepped after each sample). The
 *             carrier is chosen by the bind kind (osc->sampleCacheLooped):
 *             smp, slp, or wtb with frame blending (falling back to the
 *             nearest frame without an interpolation slot or with one
 *             frame); no item renders a zero block.
 * Why:        Drum's fm FM-depth ramp (S088 P0 Layer 2e) folded into the
 *             carrier's existing depth multiply: one vadd per sample instead
 *             of a separate 10-instruction modulator pass (S089 P3). Separate
 *             ramped loops, because the originals are shared with Cymbal and
 *             HiHat and must not get dearer (the S089 P2 §8 ceiling).
 * Inputs:     osc (osc_isTyped(); bound by osc_userBind()); mod[size] (the
 *             modulator block, not pre-scaled); buf; size (<=
 *             OUTPUT_DMA_SIZE); gain (render gain, x the item's gain code);
 *             d0 (FM depth on sample 0); dStep (depth change per sample).
 *             With d0 = osc->fmMod and dStep = 0.0f the output is
 *             bit-identical to osc_renderTypedFm() (host law FR1).
 * Outputs:    buf[size]; osc->phase; a wtb blend claims one interpolation
 *             slot for the block (modNode_interpClaim()).
 * Cost:       per sample, each carrier's osc_renderTypedFm() loop + 1; one
 *             flash call per block.
 * Accessors:  Drum_calcVoiceSyncBlock() (fm branch) only.
 * Affiliates: osc_renderTypedFm(); the four ramped loops in Oscillator.c
 *             (S089 P3 block, .itcm.s089p3); DrumVoice.lastFmDepth.
 */
void osc_renderTypedFmRamp(OscInfo* osc, const int16_t* mod, int16_t* buf,
                           uint8_t size, float gain, float d0, float dStep);
```

---

## 6. `Core/DSP/Instruments/Drum/DrumVoice.c`

### 6.1 REPLACE: the `fm` branch

**Lines 758–812.** REPLACE everything from `case DRUM_MOD_FM:` through its
`break;`, up to but not including the switch's closing `}` at 813. The
removed text is today's:
- the modulator render;
- the S088 comment block;
- the `start`/`dstep` pass;
- `voice->lastFmDepth = newDepth;`;
- the `osc_isTyped()` choice between `osc_renderTypedFm()` and
  `calcNextOscSampleFmBlock()`.

The replacement (tabs as in the file):

```c
	case DRUM_MOD_FM:
	default:
		//calc next mod osc sampleBlock
		calcNextOscSampleBlock(&voice->modOsc,modBuf,size,voice->fmModAmount);
		/*
		 * FM depth ramp, by carrier type (S088 P0 §3 Layer 2e, D1; S089 P3).
		 *
		 * What:       the FM index glides from the previous block's depth
		 *             (lastFmDepth) to this block's (osc.fmMod) across the
		 *             block after a fre/itp retrigger (ampRampArm). The first
		 *             sample's depth is lastFmDepth, clamped to [0, 4 x fmMod]
		 *             (the S088 start-ratio clamp 0..4); the last sample's is
		 *             fmMod. Two mechanisms, chosen by the carrier type:
		 *             - typed carrier (smp, slp, wtb): the ramp is folded into
		 *               the carrier's own depth multiply.
		 *               osc_renderTypedFmRamp() reads d0 (the first sample's
		 *               depth) and dStep (the change per sample) and the
		 *               modulator block is used as rendered; no pass.
		 *             - stock carrier (wav: table waves, sine, crash, noise):
		 *               the S088 pass scales modBuf by a ratio that moves
		 *               from lastFmDepth / fmMod to 1, then the unchanged
		 *               calcNextOscSampleFmBlock() applies fmMod.
		 * Why:        on a retrigger the mod envelope restarts, so osc.fmMod
		 *             jumps; without the ramp that step is a click whose size
		 *             follows the modulator amplitude. The fold-in (S089 P3)
		 *             removes the 10-instruction pass from the three dearest
		 *             carriers, which set the CPU ceiling (slp 90 -> 81, wtb
		 *             blend 86 -> 77, smp 82 -> 73 per sample, including the
		 *             crash modulator). The cheaper stock carriers keep the
		 *             pass: they are already below the new worst case, and
		 *             their loops are shared with Cymbal/HiHat.
		 * Cost:       constant per configuration: the branch depends on the
		 *             oscillator type, never on whether the ramp is armed.
		 *             Every block computes its ramp and runs the same loops.
		 *             Unarmed, the typed path gets d0 = fmMod and dStep =
		 *             exactly 0 (x - x == 0), and the stock path a ratio of
		 *             exactly 1, so rst stays bit-identical on both (S0).
		 * Inputs:     modBuf (modulator at fmModAmount), osc.fmMod (this
		 *             block's depth, from Drum_calcVoiceAsync()), lastFmDepth,
		 *             ampRampArm (cleared later by voicePost_drum()), size.
		 * Outputs:    buf (carrier section); modBuf scaled on the stock path
		 *             only; lastFmDepth = osc.fmMod for the next block (both
		 *             paths store it before their carrier call).
		 * Accessors:  this fm branch only.
		 * Affiliates: osc_renderTypedFmRamp() (Oscillator.c S089 P3 block);
		 *             osc_renderTypedFm() (Cymbal/HiHat, unramped);
		 *             calcNextOscSampleFmBlock(); DrumVoice.lastFmDepth;
		 *             ampRampArm; Drum_triggerVoice() (arms it).
		 */
		{
			const float newDepth = voice->osc.fmMod;

			if (osc_isTyped(&voice->osc)) {
				/*
				 * Typed carrier: ramp endpoints for the carrier's own
				 * multiply. d0 is the depth on sample 0; armed, it is the
				 * previous block's depth, bounded to [0, 4 x newDepth] so
				 * the first sample's FM index is the same as the S088
				 * ratio clamp (0..4) gave. Unarmed, or with a depth too
				 * small to ramp from, d0 = newDepth.
				 */
				float d0 = newDepth;
				float dStep;

				if (voice->ampRampArm && newDepth > 0.0001f) {
					d0 = voice->lastFmDepth;
					if (d0 < 0.0f) d0 = 0.0f;
					else if (d0 > 4.0f * newDepth) d0 = 4.0f * newDepth;
				}
				/*
				 * Linear step so that sample size - 1 reads newDepth
				 * (d0 + (size - 1) x dStep), as the S088 ratio reached 1 on
				 * the last sample. Computed on every block (constant cost);
				 * unarmed, newDepth - d0 is exactly 0.0f, so dStep is 0 and
				 * the carrier's depth never moves. No divide by the depth
				 * (the S088 lastFmDepth / newDepth is gone on this path).
				 */
				dStep = (size > 1u)
					? (newDepth - d0) / (float)(size - 1u) : 0.0f;
				/* This block's depth is the next ramp's start. Stored
				 * before the render, as the S088 code did, so newDepth is
				 * not live across the carrier call (MEMORY.md S086 P3). */
				voice->lastFmDepth = newDepth;
				osc_renderTypedFmRamp(&voice->osc, modBuf, buf, size, 1.0f,
				                      d0, dStep);
			} else {
				/* Stock carrier: the S088 pass, unchanged. */
				float start = 1.0f;

				if (voice->ampRampArm && newDepth > 0.0001f) {
					start = voice->lastFmDepth / newDepth;
					if (start < 0.0f) start = 0.0f;
					else if (start > 4.0f) start = 4.0f;
				}
				/* Always runs (constant cost, D1): with start == 1 the
				** multiply is the identity and the output matches the
				** pre-S088 block. */
				{
					const float dstep = (size > 1u)
						? (1.0f - start) / (float)(size - 1u) : 0.0f;
					uint8_t k;
					float s = start;

					for (k = 0u; k < size; k++) {
						modBuf[k] = (int16_t)((float)modBuf[k] * s);
						s += dstep;
					}
				}
				voice->lastFmDepth = newDepth;
				calcNextOscSampleFmBlock(&voice->osc,modBuf,buf,size,1.0f);
			}
		}
		break;
```

**What does not change:**
- **The stock arm is today's code,** in today's order: the pass, then the
  `lastFmDepth` store, then `calcNextOscSampleFmBlock()`. Only the
  enclosing `if` and the indentation are new. Its pass loop measures 10 in
  the mixer, as before (§11).
- **The order of the branch:** the modulator render first, then the ramp,
  then the carrier.
- **Everything after the switch:** `osc_positionBendRestore()`, the `itp`
  blend and the chain stage.
- **`ampRampArm`** is still cleared later by `voicePost_drum()`
  (line 911); P3 does not touch it.

### 6.2 MODIFY: the `osc.fmMod` writer's comment

**Lines 502–503** (in `Drum_calcVoiceAsync()`, the "Mod envelope" comment
above `voice->osc.fmMod = voice->fmModAmount * egPitchVal;`). MODIFY:

```diff
-	 * Accessors:  calcNextOscSampleFmBlock() (fm); Drum_modEnvDepth() (snc,
-	 *             rng), both in Drum_calcVoiceSyncBlock().
+	 * Accessors:  calcNextOscSampleFmBlock() or, for a typed osc1 (S089 P3),
+	 *             osc_renderTypedFmRamp() (fm); Drum_modEnvDepth() (snc, rng),
+	 *             all in Drum_calcVoiceSyncBlock(); the fm depth ramp also
+	 *             stores it in lastFmDepth.
```

---

## 7. `Core/DSP/Instruments/Drum/DrumVoice.h` (comments only)

No field changes: `DrumVoice` keeps its size, and `runtime_slots` stays
7,584 B.

**Line 243** (`ampRampArm` comment). MODIFY:

```diff
-	 * Affiliates: voicePost_drum(); lastVeloGain; lastFmDepth.
+	 * Affiliates: voicePost_drum(); lastVeloGain; lastFmDepth; the fm branch's
+	 *             depth ramp (the S088 pass for a stock osc1, the d0/dStep of
+	 *             osc_renderTypedFmRamp() for a typed one, S089 P3).
```

**Lines 308–315** (`lastFmDepth` comment). MODIFY:

```diff
-	 * Why:        the FM depth ramp starts the modulator block at
-	 *             lastFmDepth / fmMod and moves to 1 across the armed block, so
-	 *             the depth change of a retrigger glides instead of stepping.
-	 *             On every other block the start ratio is exactly 1, so the
-	 *             multiply is the identity and rst stays bit-identical.
-	 * Inputs:     Drum_calcVoiceSyncBlock() (fm branch), after the ramp.
+	 * Why:        the FM depth ramp glides the FM index from this value to
+	 *             the new fmMod across the armed block, so the depth change of
+	 *             a retrigger glides instead of stepping:
+	 *             - stock osc1 (wav): the S088 pass scales the modulator block
+	 *               by a ratio from lastFmDepth / fmMod to 1;
+	 *             - typed osc1 (smp, slp, wtb; S089 P3): the carrier's depth
+	 *               runs from d0 = lastFmDepth (clamped to [0, 4 x fmMod]) to
+	 *               fmMod inside osc_renderTypedFmRamp(), with no pass.
+	 *             On every other block the ratio is exactly 1 / the step is
+	 *             exactly 0, so rst stays bit-identical on both paths.
+	 * Inputs:     Drum_calcVoiceSyncBlock() (fm branch), stored before the
+	 *             carrier call on both paths.
 	 * Outputs:    the next fm block's ramp start.
-	 * Affiliates: the FM depth ramp; ampRampArm; Drum_calcVoiceAsync().
+	 * Affiliates: the FM depth ramp; ampRampArm; Drum_calcVoiceAsync();
+	 *             osc_renderTypedFmRamp().
```

---

## 8. Host tests

### 8.1 `tools/dsp_test/modules/oscillator.c`

**Lines 2–3** (the header title). MODIFY:

```diff
- * Oscillator module host tests (S085 snc/rng, S086 stock waves and pos,
- * S087 typ, samples and wavetables).
+ * Oscillator module host tests (S085 snc/rng, S086 stock waves and pos,
+ * S087 typ, samples and wavetables, S088 itp, S089 P3 Drum FM ramp).
```

**After line 34** (`W8 a modulator w entry plays frame 0.`) in the group
list. **ADD:**

```c
 *               fmramp FR1 the ramped typed FM carriers with d0 = fmMod,
 *                      dStep = 0 equal osc_renderTypedFm() bit for bit (every
 *                      carrier, depth and gain); FR2 sample i of a ramped
 *                      block equals a one-sample original at d0 + i x dStep.
 *                      Breaks (Makefile): wtb routed to the one-frame twin
 *                      fails FR1; the depth stepped before use fails FR2.
```

(The existing `itp` group is not listed in this header today. That is
outside P3 and left unchanged.)

**Before line 1295** (`int main(void)`). **ADD** the group. It reuses the
module's `install_sample()`, `bind_typed()`, `install_table()`,
`bind_wtb()`, `random_mod()`, `same_buf()`, `fail()` and `g_claim`:

```c
/*
 * Drum FM depth-ramp carriers (S089 P3).
 *
 * What:       the ramped typed FM carriers (osc_renderTypedFmRamp() and its
 *             four loops) against the unramped originals they copy:
 *               FR1 S0: with d0 = fmMod and dStep = 0 the ramped render is
 *                   bit-identical to osc_renderTypedFm() - same samples, same
 *                   end phase - for every carrier case (smp; slp in the loop
 *                   and in the lead-in; wtb 16-bit blend with a slot, wtb
 *                   8-bit blend, wtb without a slot (the one-frame fallback);
 *                   no item), several depths (0 .. 3.9), two gains and
 *                   full-scale random modulators;
 *               FR2 the ramp schedule: with d0 != d1, sample i of one ramped
 *                   block equals a one-sample osc_renderTypedFm() at depth
 *                   d_i, where d_0 = d0 and d_(i+1) = d_i + dStep (the float
 *                   accumulation the loops perform), for the same cases
 *                   (wtb with a held pos, so a one-sample plan is the block's
 *                   plan); a ramp down (2.5 x d1 -> d1) and up (0 -> d1).
 *             Break proofs (Makefile oscillator rule): the dispatcher sending
 *             wtb to the one-frame copy must fail FR1; the depth stepped
 *             before its first use (d = d0 + dStep) must fail FR2.
 * Why:        S089 P3 plan §5.1 gate: unarmed blocks are S0 (a host law per
 *             covered carrier), and the armed ramp must start at d0 on
 *             sample 0 and reach d1 on sample N - 1 (the S088 ramp's ends).
 * Inputs:     the real Oscillator.c (host build) over the real catalogue;
 *             g_claim (the modNode_interpClaim() stub).
 * Outputs:    failure lines; the group count.
 * Affiliates: Oscillator.c S089 P3 block; osc_renderTypedFm();
 *             DrumVoice.c fm branch (computes d0/dStep).
 */

/* One FR case: how to bind and where the block starts. */
typedef struct {
    const char *name;
    uint8_t     kind;      /* 0 smp, 1 slp, 2 wtb16, 3 wtb8, 4 none */
    uint8_t     claim;     /* g_claim for the block (wtb) */
    uint32_t    phase;     /* start phase */
    uint32_t    inc;       /* phaseInc */
} FmrCase;

/* Bind o for case c on the item installed by fmr_install(c->kind). */
static void fmr_bind(OscInfo *o, const FmrCase *c)
{
    if (c->kind == 2u || c->kind == 3u) {
        bind_wtb(o, 0u);
        o->position = 64u;          /* between the two frames: a real blend */
        o->positionApplied = 64u;
        o->positionBendStep = 0;    /* held pos: one segment, constant ff */
    } else if (c->kind == 4u) {
        memset(o, 0, sizeof(*o));
        o->oscType = OSC_TYPE_SMP;  /* typed, but bound to nothing */
        o->sampleCacheLooped = OSC_BIND_NONE;
    } else {
        bind_typed(o, c->kind == 0u ? OSC_TYPE_SMP : OSC_TYPE_SLP, 0u, c->inc);
    }
    o->phase = c->phase;
    o->phaseInc = c->inc;
}

/* Install the item case c plays: a 100-point random sample (peak 32767 so
** the gain code is 0), loop 40..60 for slp; or a 2-frame 64-point table. */
static void fmr_install(uint8_t kind)
{
    static int16_t smp[100];
    static int16_t f0[64];
    static int16_t f1[64];
    static int8_t  f0b[64];
    static int8_t  f1b[64];
    uint32_t s = 0x08903u;

    if (kind <= 1u || kind == 4u) {
        for (unsigned i = 0u; i < 100u; i++)
            smp[i] = (int16_t)(golden_rand(&s) >> 17);
        smp[50] = 32767;
        install_sample(smp, 100u, 40u, 60u, 1u);
    } else if (kind == 2u) {
        for (unsigned i = 0u; i < 64u; i++) {
            f0[i] = (int16_t)(golden_rand(&s) >> 16);
            f1[i] = (int16_t)(golden_rand(&s) >> 16);
        }
        install_table(64u, 16u, f0, f1);
    } else {
        for (unsigned i = 0u; i < 64u; i++) {
            f0b[i] = (int8_t)(golden_rand(&s) >> 24);
            f1b[i] = (int8_t)(golden_rand(&s) >> 24);
        }
        install_table(64u, 8u, (const int16_t *)f0b, (const int16_t *)f1b);
    }
}

static void run_fmramp(void)
{
    /* Phases in Q19.12 for the sample cases; full 32-bit for wtb. */
    static const FmrCase cases[] = {
        { "smp",      0u, 0u, 3u << OSC_SAMPLE_PHASE_SHIFT,  1500u },
        { "smp end",  0u, 0u, 96u << OSC_SAMPLE_PHASE_SHIFT, 4096u },
        { "slp loop", 1u, 0u, 45u << OSC_SAMPLE_PHASE_SHIFT, 3000u },
        { "slp lead", 1u, 0u, 10u << OSC_SAMPLE_PHASE_SHIFT,  512u },
        { "wtb16",    2u, 1u, 0x12345678u, 0x01234567u },
        { "wtb16 ns", 2u, 0u, 0x12345678u, 0x01234567u },
        { "wtb8",     3u, 1u, 0x87654321u, 0x00F00000u },
        { "none",     4u, 0u, 0u, 0u },
    };
    static const float depths[] = { 0.0f, 0.37f, 1.0f, 3.9f };
    static const float gains[] = { 1.0f, 0.5f };
    int16_t mod[N];
    int16_t ra[N];
    int16_t rb[N];
    OscInfo a;
    OscInfo b;
    uint32_t seed = 0x0890003u;

    for (unsigned c = 0u; c < sizeof(cases) / sizeof(cases[0]); c++) {
        const FmrCase *fc = &cases[c];

        fmr_install(fc->kind);
        g_claim = fc->claim;

        /* FR1: unarmed (d0 = fmMod, dStep = 0) == the original, exactly. */
        for (unsigned di = 0u; di < 4u; di++) {
            for (unsigned gi = 0u; gi < 2u; gi++) {
                for (unsigned r = 0u; r < 8u; r++) {
                    random_mod(mod, &seed);
                    fmr_bind(&a, fc);
                    a.fmMod = depths[di];
                    b = a;
                    for (unsigned i = 0u; i < N; i++)
                        ra[i] = rb[i] = 0x5A5A;
                    osc_renderTypedFm(&a, mod, ra, N, gains[gi]);
                    osc_renderTypedFmRamp(&b, mod, rb, N, gains[gi],
                                          b.fmMod, 0.0f);
                    if (!same_buf(ra, rb) || a.phase != b.phase)
                        fail("FR1 unarmed ramp == original", c, di, gi);
                }
            }
        }

        /* FR2: the armed schedule, against one-sample original renders. */
        for (unsigned dir = 0u; dir < 2u; dir++) {
            for (unsigned r = 0u; r < 8u; r++) {
                const float d1 = 0.8f;
                const float d0 = dir ? 0.0f : 2.5f * d1;
                const float dStep = (d1 - d0) / (float)(N - 1u);
                float d = d0;

                random_mod(mod, &seed);
                fmr_bind(&a, fc);
                a.fmMod = d1;
                b = a;
                osc_renderTypedFmRamp(&b, mod, rb, N, 1.0f, d0, dStep);
                for (unsigned i = 0u; i < N; i++) {
                    a.fmMod = d;
                    osc_renderTypedFm(&a, &mod[i], &ra[i], 1u, 1.0f);
                    d += dStep;
                }
                if (!same_buf(ra, rb) || a.phase != b.phase)
                    fail("FR2 ramp sample i reads d0 + i x dStep", c, dir, r);
            }
        }
    }
    g_claim = 0u;
}
```

**After line 1307** (`group("itp", run_itp);`). **ADD:**

```c
    group("fmramp", run_fmramp);
```

**How the laws are built:**
- **FR2 needs no model.** The reference is the real original loop, called
  one sample at a time at the depth the ramp should have reached. For
  every case the per-sample call reproduces the block exactly:
  - `smp` advances per sample;
  - the `slp` read rule and `osc_slpBlockInc()` step depend only on where
    the block starts, and the cases stay inside one rule;
  - a held `wtb` pos (`positionBendStep` 0) plans one segment with a
    constant `ff`.

  So any difference is the ramp itself.
- **The case table covers every arm of the dispatcher and the twins:**
  - `smp`, including the end hold;
  - `slp` inside the loop and in the lead-in;
  - `wtb` 16-bit with a slot (the blend), 16-bit without a slot (the
    fallback) and 8-bit with a slot;
  - no item (zero block).

### 8.2 `tools/dsp_test/Makefile`

**Line 5** (the header list). MODIFY:

```diff
-#               oscillator          modules/oscillator.c (+ the pos cap build)
+#               oscillator          modules/oscillator.c (+ the pos cap build,
+#                                   + 2 breaks: S089 P3 FR1/FR2)
```

**Line 52** (before `oscillator: $(GEN)/Oscillator.host.h …`). **ADD** a
comment above the target:

```make
# S089 P3 breaks (fmramp group): the ramp dispatcher sending wtb to the
# one-frame twin must fail FR1; the depth stepped before its first use
# (d = d0 + dStep) must fail FR2. Each break edits the generated
# Oscillator.host.c and is found first through its own -I directory.
```

**After line 58** (`./build/oscillator_cap`). **ADD** to the `oscillator`
recipe (each line begins with a tab):

```make
	mkdir -p build/break_fmr1 build/break_fmr2
	sed 's/osc_wtbFmBlendRampBlock(osc, mod, buf, size, gain, d0, dStep);/osc_wtbFmRampBlock(osc, mod, buf, size, gain, d0, dStep);/' \
	  $(GEN)/Oscillator.host.c > build/break_fmr1/Oscillator.host.c
	$(HOSTCC) -Ibuild/break_fmr1 $(HOSTFLAGS) $(OSC_HOST_INC) modules/oscillator.c \
	  $(CAT_HOST_SRC) -o build/oscillator_break_fmr1 -lm
	@if ./build/oscillator_break_fmr1 >/dev/null 2>&1; then \
	  echo "oscillator_break_fmr1: FAILED (ramp wtb routing break not caught)"; exit 1; \
	else echo "oscillator_break_fmr1: caught (law FR1 fails as expected)"; fi
	sed 's/float d = d0;/float d = d0 + dStep;/' \
	  $(GEN)/Oscillator.host.c > build/break_fmr2/Oscillator.host.c
	$(HOSTCC) -Ibuild/break_fmr2 $(HOSTFLAGS) $(OSC_HOST_INC) modules/oscillator.c \
	  $(CAT_HOST_SRC) -o build/oscillator_break_fmr2 -lm
	@if ./build/oscillator_break_fmr2 >/dev/null 2>&1; then \
	  echo "oscillator_break_fmr2: FAILED (ramp step-order break not caught)"; exit 1; \
	else echo "oscillator_break_fmr2: caught (law FR2 fails as expected)"; fi
```

**Self-checking.** If a `sed` pattern stops matching (for example because
the dispatcher line is reformatted), the break build becomes the good build.
It then passes, and the rule reports `FAILED` and exits 1, so a break that
goes stale cannot pass silently.

---

## 9. Verification

### 9.1 Static gates

Keep a copy of the baseline `build/lxr02.elf` / `.bin` before step 1 (no
frozen snapshots are kept).

1. **Step 1:** `build/lxr02.bin` is byte-identical to the baseline.
2. **The originals and the stock carriers are unchanged.** Dump each loop
   set from the baseline and the new ELF and compare: the dumps must be
   identical.
   - Originals:

     ```
     python3 tools/dsp_test/elf_loopdump.py --elf <elf> --sym osc_smpFmBlock --sym osc_slpFmBlock --sym osc_wtbFmBlock --sym osc_wtbFmBlendBlock --out <file>
     ```

   - Stock FM loops:
     `--sym calcFmBlock --sym calcSampleOscFmBlock --sym calcNextOscSampleFmBlock`.

   This is the Cymbal/HiHat guarantee.
3. **Twin costs.** Dump the twins with `elf_loopdump.py` (`--sym
   osc_smpFmRampBlock --sym osc_slpFmRampBlock --sym osc_wtbFmRampBlock
   --sym osc_wtbFmBlendRampBlock`):
   - every hot loop is its original + 1 (§11 table);
   - the opcode multiset of each hot loop is its original's plus exactly
     one `vadd.f32` (no spill, no reload).
4. **ITCM.**
   - Every function in ITCM keeps its baseline address (`nm`, addresses
     below 0x4000).
   - Only the three veneer stubs move, to after `.itcm.s089p3`.
   - `.itcm.s089p3` follows `.itcm.s089` in the map.
   - ITCM 15,240 / 16,384 B.
5. **Mixer (P0 §4.2).** Dump `mixer_calcNextSampleBlock` with
   `elf_loopdump.py` for both builds, then run `elf_hotloops.py --compare`:
   - 0 hot loops grew;
   - the 10-instruction pass loop is still present (the stock arm).
6. **Inlined set and RAM.**
   - The set of function symbols differs from the baseline only by the five
     new functions and their three flash veneers:
     `osc_renderTypedFmRamp`, `osc_{smp,slp}FmRampBlock`,
     `osc_wtbFm{,Blend}RampBlock`, and `__osc_{smp,slp,wtbFmBlend}…_veneer`.
   - No function leaves or joins the inlined set.
   - `.data` 424 B and `.bss` 412,884 B are unchanged.
7. **Optional, for the record:** `fpseq.py --obj build/lxr02.elf --ref
   osc_slpFmBlock --new osc_slpFmRampBlock --report` (and the same for the
   other three pairs). The hot loop's value-op multiset differs only by one
   `vadd.f32`.

### 9.2 Host tests

`make -C tools/dsp_test oscillator modulation`:
- every `oscillator` group reports 0 failures, `fmramp` included;
- `oscillator_break_fmr1: caught (law FR1 fails as expected)`;
- `oscillator_break_fmr2: caught (law FR2 fails as expected)`;
- `modulation` reports 0 failures.

### 9.3 Hardware (the user's check)

There is no `cpu`-widget pass criterion (D2). Suggested listening checks:

| # | Check |
|---|---|
| H1 | Drum `fm` with osc 1 on `smp`, `slp` and `wtb`, in `rst`: sounds exactly as before (S0). |
| H2 | The same in `fre` and `itp`, retriggering into a ringing tail at high `amt`: no click; the FM-index glide sounds as before. |
| H3 | Drum `fm` with stock carriers (sine, saw/tri/rec, `Sq3`/`Sw3`, crash, noise) in `rst`/`fre`/`itp`: unchanged. |
| H4 | Cymbal and HiHat with typed carriers (`smp`/`slp`/`wtb`): unchanged. |
| H5 | `wtb` FM with both interpolation slots taken (several `wtb` voices, or `itp` fades holding the slots): the one-frame fallback, no clicks on retrigger. |
| H6 | The worst-case Scene: the user's judgement of the load. |

---

## 10. Documentation (after hardware passes)

| Document | Change |
|---|---|
| `CPU_USE_DSP_AUDIT.md` | **Session 089 P3 additions:** item 61 CHANGED: the Drum FM depth ramp is folded into the typed carriers (twins +1 per sample, the pass gone on typed `osc1`; the §11 table); item 62 CHANGED: ITCM 13,032 → 15,240 B (`.itcm.s089p3`; the veneers move); flash headroom 72,008 → 69,728 B. Amend item 52: the pass now runs on stock `osc1` only. |
| `INSTRUMENTS_DSP_REFERENCE.md` | §12: the Drum ceiling (`fm`, user samples) moves from ≈ 90 to ≈ 81 per sample (`slp` 51 + crash 30). Update the FM depth ramp description (two mechanisms, by carrier type). Add a code rule: "a typed FM carrier loop has a Drum ramp twin (`osc_*FmRampBlock`); change both". |
| `STORAGE_SRAM_MANIFEST.md` | the ITCM total 15,240 / 16,384 B; the `.itcm.s089p3` row (2,208 B, Drum `fm` ramped carriers, approved 2026-10-30) |
| `S089_P3_FM_RAMP_EFFICIENCY.md` | status → implemented; the §5.1 row correction (§1 F5); the gate's `cpu` line replaced by D2 |
| `S089_P2_WAVEFOLDER_CHAIN.md` §8 | in the list of possible savings, mark the Drum FM depth-ramp loop as used by P3 (typed carriers) |
| `MEMORY.md` | Binding Rules / DSP: "Drum FM ramp (S089 P3): a typed `osc1` (`smp`/`slp`/`wtb`) renders `fm` through `osc_renderTypedFmRamp()` and the four `osc_*FmRampBlock` twins (`.itcm.s089p3`), with no modulator pass; a stock `osc1` keeps the S088 pass. A twin and its original change together. Cymbal/HiHat never call the twins." |
---

## 11. Pre-verification record (session scratchpad, 2026-10-30)

**Method.**
- **The copy:** HEAD was exported with `git archive` into the scratchpad.
  Its `make all` image is byte-identical to the project's `build/lxr02.bin`.
- **Applied:** §3, §4, §5, §6 and §7 exactly as written here, plus the §8
  code. The §8 header comments are text only.
- **Built** with the project Makefile (`make all`, LTO, `-Ofast` DSP rule).
- **Untouched:** the project tree.

**Results.**

| Gate | Result |
|---|---|
| Step 1 (linker only) | relinked; `lxr02.bin` byte-identical |
| Comment-only edits (step 4) | `lxr02.bin` byte-identical to the code-only build |
| Originals' loops (`osc_smp/slp/wtbFm/wtbFmBlendBlock`) | dump text identical to the baseline |
| Stock FM loops (`calcFmBlock` ×4, `calcSampleOscFmBlock`, `calcNextOscSampleFmBlock`) | dump text identical |
| ITCM function addresses | all 40 ITCM functions (and `_sitcm`) unchanged; the 3 veneer stubs moved to 0x3B70–0x3B87 |
| ITCM | 13,032 → **15,240 / 16,384 B** (+2,208; 1,144 B left) |
| Mixer `elf_hotloops.py` | **39 hot loops equal, 0 shrank, 0 grew**; the pass loop is present (10) |
| `mixer_calcNextSampleBlock` | 14,412 → 14,440 B (+28: the typed arm's set-up and call) |
| Symbols | only the 5 new functions and 3 new flash veneers |
| `.text` / `.data` / `.bss` | 681,232 → 683,512 B / 424 B unchanged / 412,884 B unchanged |
| Flash headroom | 72,008 → **69,728 B** |
| Host `oscillator` | all groups 0 failures (`fmramp` included); `fmr1` and `fmr2` breaks caught, each failing only its own law (FR1 on the claimed `wtb` cases; FR2 on the sample cases) |
| Host `modulation`, `voice_control`, `sequencer`, `instrument_manager`, `samples`, `special_tags`, `voicefold`, `fold_tables` | pass (as before) |

**Per-sample counts (instructions; the hot loops):**

| Carrier | Original | Twin | Drum `fm` today (+ crash 30 + pass 10) | Drum `fm` after (+ crash 30) | Change |
|---|---:|---:|---:|---:|---:|
| `slp`, inside the loop | 50 | **51** | 90 | **81** | −9 |
| `slp`, lead-in | 39 | 40 | 79 | 70 | −9 |
| `wtb` blend, 16-bit | 46 | **47** | 86 | **77** | −9 |
| `wtb` blend, 8-bit | 42 | 43 | 82 | 73 | −9 |
| `smp` | 42 | **43** | 82 | **73** | −9 |
| `wtb` fallback, 8-bit / 16-bit | 32 / 34 | 33 / 35 | 72 / 74 | 63 / 65 | −9 |
| stock carriers (table, crash, sine, noise) | 35 / 38 / 29 / 10 | — | unchanged | unchanged | 0 |

**Notes on the counts:**
- **Each twin's hot loop is its original's opcode multiset plus one
  `vadd.f32`.**
- **Fragments that are not hot loops** (the blend twin's segment-loop
  set-up paths, which close on an unconditional branch) went 81 → 80 and
  84 → 82.
- **Per block, the typed arm adds** one flash call: the dispatcher (≈ 8
  instructions) plus one more veneer hop. That is ≈ 0.3–0.5 per sample,
  against −9 per sample.
- **The new Drum ceiling** is `fm` + `slp` ≈ 81 per sample (was ≈ 90). The
  next settings are `fm` + crash carrier 78 (stock, unchanged) and `rng`
  ≈ 77.

---

## 12. Implementation record (working session, 2026-10-30)

**Tree:** HEAD `d944c87f`; baseline `build/lxr02.bin` copied to
`/tmp/s089p3_baseline/` before step 1 (SHA-256
`0bd5c243…490f1`). All project edits below were made in order; the §10
documentation is deliberately left for after hardware passes.

| Step | Result |
|---|---|
| 1 | Linker comment + input order applied. `make all` relinked to a byte-identical `build/lxr02.bin` (SHA-256 unchanged); ITCM 13,032 B. |
| 2 | `Oscillator.c` S089 P3 block + `Oscillator.h` declaration applied. Builds. The unreferenced block is dropped by `--gc-sections` (`nm` shows none of the new functions), but see the veneer note below. |
| 3 | Drum `fm` branch replaced with the typed/stock split. Builds; `text` 683,512 B, `.data` 424 B, `.bss` 412,884 B, ITCM **15,240 B**, flash headroom **69,728 B**. |
| 4 | Comment-only twin notes in `Oscillator.c`, `DrumVoice.c`, `DrumVoice.h`. `build/lxr02.bin` byte-identical to the step-3 build (verified). |
| 5 | Host tests added (`fmramp` group, 2 break builds). `make -C tools/dsp_test oscillator modulation` passes; both breaks are caught. |
| 6 | **Pending (user hardware checks H1–H6).** `build/LXRV2_lxr02.img` built for flashing. |

A clean copy of the finished working tree (build directories excluded) was
also built with `make all`: its `build/lxr02.bin` is byte-identical to the
workspace image (SHA-256 `b75be9d5…511c0`), so no gate depends on
incremental build state.

### 12.1 One deviation from §2 step 2

The fresh HEAD build in a scratch tree is byte-identical to the baseline,
so the baseline itself is sound. After adding the (unreferenced) S089 P3
block, however, `build/lxr02.bin` was **not** byte-identical to the
baseline: the only difference was the **order of the linker-generated
ITCM→flash veneer stubs**. Every function symbol kept its address; `nm`
differed only in the veneer list (e.g. `__memset_veneer`,
`__osc_userBind_veneer`, `__osc_wtbPlanBlock.constprop.0_veneer`). LTO still
partitions the module with the new functions present before DCE, and the
veneer stubs follow the last ITCM subsection in that order. No loop and no
ITCM address moved. §9.1 gates 2–6 all pass, and §2 step 4's
byte-identical check (step 3 → step 4) holds exactly.

### 12.2 Static gates (§9.1)

- **Step 1:** byte-identical to the baseline.
- **Originals** (`osc_smpFmBlock`, `osc_slpFmBlock`, `osc_wtbFmBlock`,
  `osc_wtbFmBlendBlock`): `elf_loopdump.py` text identical to the baseline.
- **Stock FM loops** (`calcFmBlock`, `calcSampleOscFmBlock`,
  `calcNextOscSampleFmBlock`): dump text identical.
- **Twins:** hot-loop counts are exactly the originals + 1, and the mnemonic
  multiset differs by exactly one `vadd.f32` each (register renaming only on
  the lead-in/blend set-up paths):

  | Twin | Hot loop(s) |
  |---|---:|
  | `osc_slpFmRampBlock` | 51 (loop) / 40 (lead-in) |
  | `osc_smpFmRampBlock` | 43 |
  | `osc_wtbFmBlendRampBlock` | 47 (16-bit) / 43 (8-bit) |
  | `osc_wtbFmRampBlock` | 33 (8-bit) / 35 (16-bit) |

- **ITCM:** all existing ITCM functions keep their baseline addresses; the
  four twins occupy `0x32d0`–`0x3b70` (`.itcm.s089p3`, 0x8a0 = 2,208 B),
  immediately after `.itcm.s089` (`0x3154`, 0x17c). The three veneer stubs
  moved to `0x3b70`–`0x3b87` as predicted.
- **Mixer:** `elf_hotloops.py --compare` reports **39 hot loops equal, 0
  shrank, 0 grew**; the 10-instruction pass loop is still present on the
  stock arm.
- **Symbols:** only the five new functions and their three flash veneers
  (`__osc_{smp,slp,wtbFmBlend}…_veneer`) were added; nothing left the set.
  `.data` 424 B and `.bss` 412,884 B unchanged.

### 12.3 Host tests (§9.2)

`make -C tools/dsp_test oscillator modulation`:

```
oscillator ring/sync/stock/pos/type/sample/wtb/itp/fmramp failures: 0
oscillator_break_fmr1: caught (law FR1 fails as expected)
oscillator_break_fmr2: caught (law FR2 fails as expected)
modulation failures: 0
```

Also run and passing: `voice_control`, `sequencer`, `instrument_manager`,
`samples`, `special_tags`, `voicefold`, `fold_tables`.

### 12.4 Hardware (§9.3, user)

`build/LXRV2_lxr02.img` (683,952 B) is ready on the build tree. The six
checks H1–H6 in §9.3 are still to be done on the instrument; there is no
`cpu`-widget pass criterion (D2).

---

## 13. Post-implementation assessment (2026-10-30)

### 13.1 Verdict

**The implementation conforms to the schedule.** Its firmware image is
byte-identical to the build used to verify the schedule (§11), and every
§9.1 static gate and §9.2 host test passes when re-run independently. No
defect was found in the new code. One finding is about the schedule itself,
not the implementation (A1). One is cosmetic (A2). Hardware H1–H6 (§9.3)
are still open.

### 13.2 What was checked

- **Conformance.** Each changed file was diffed against the scratch tree
  that the schedule's code was verified in (§11):
  - **Code:** `Oscillator.c` (the S089 P3 block and the twin notes),
    `Oscillator.h` (the declaration), `DrumVoice.c` (the `fm` branch and the
    `fmMod` comment), `DrumVoice.h` (comments) and `modules/oscillator.c`
    (the `fmramp` group and its registration) are identical to the
    schedule. The only differences are two blank lines (A2).
  - **Comment-only items** that the scratch tree did not carry: the linker
    comment block (§3), the test header (§8.1) and the Makefile comments
    (§8.2). Every line the schedule adds is present.
  - **Unscheduled changes:** none in source.
- **Independent rebuild.** A clean copy of the working tree (tracked files,
  build directories excluded) was rebuilt with `make all` / `make img`:
  - `build/lxr02.bin` is byte-identical to the workspace image and to the
    §11 build;
  - `build/LXRV2_lxr02.img` is byte-identical to the workspace image.
- **Static gates (§9.1), re-run on that build against the HEAD baseline:**

  | Gate | Result |
  |---|---|
  | Twin hot loops | `slp` 51 / 40, `smp` 43, `wtb` blend 47 / 43, `wtb` fallback 33 / 35: each original + 1 |
  | Originals and stock FM loops | `elf_loopdump.py` text identical to the baseline |
  | ITCM | every function keeps its address; only the three ITCM veneer stubs and `_eitcm` changed; `*(.itcm.s089)` then `*(.itcm.s089p3)` in the map; 15,240 / 16,384 B |
  | Mixer | `elf_hotloops.py`: 39 hot loops equal, 0 shrank, 0 grew |
  | Sizes | `.text` 683,512 B, `.data` 424 B, `.bss` 412,884 B |

- **Host tests (§9.2),** from an empty `tools/dsp_test/build`:
  - every `oscillator` group, `fmramp` included, and `modulation` report
    0 failures;
  - both breaks are caught, and each fails only its own law: `fmr1` fails
    FR1 only (160 failures, all in `fmramp`), `fmr2` fails FR2 only (90);
  - the break patterns match where intended: `float d = d0;` occurs 4 times
    (once per twin), and the `fmr1` pattern occurs once, on the
    dispatcher's `wtb` case (`Oscillator.c:3718`), not on the fallback
    call (`:3650`).
- **Code review of the new code** (the schedule's own code included):
  - **Unarmed blocks:** `dStep` is exactly 0 and `d0` is exactly `fmMod`,
    so the typed path is S0. FR1 shows it, and the disassembly loads a
    literal `0.0f`.
  - **Armed blocks:** the first sample's depth equals the S088 clamp
    (`new × clamp(last/new, 0, 4)` = `clamp(last, 0, 4·new)`), and the last
    sample's equals `fmMod` (FR2).
  - **The stock arm** is today's code in today's order.
  - **`lastFmDepth`** is stored on both arms before the carrier call, so a
    change of `osc1` type between blocks keeps the ramp history.
  - **`modBuf`:** it is no longer scaled on the typed arm. Nothing reads it
    afterwards: `transient_calcBlock()` overwrites it under T+D, and WFd
    does not read it.
  - **Interpolation slots:** the blend twin claims a slot exactly where the
    original does, so the slot accounting is unchanged.
  - **Other engines:** Cymbal/HiHat and every engine other than Drum are
    untouched.

### 13.3 Findings

| # | Kind | Finding | Action |
|---|---|---|---|
| A1 | Schedule defect (recorded correctly in §12.1) | §2 step 2 predicted that the unreferenced S089 P3 block leaves the image identical. Reproduced: it does not. The image differs in 177 bytes, all of them **linker veneer stubs** and the branch offsets of their call sites: the three ITCM → flash stubs **and** the flash → ITCM veneers (`0x080aaf30`–`0x080ab027`) are laid out in a different order. Every function keeps its address, and every loop dump is identical. The veneer order depends on LTO partitioning, which sees the new functions before `--gc-sections` drops them. | None to the code. Read §2 step 2's gate as "builds; every function symbol keeps its address; only veneer stubs reorder". §12.1 already says so. |
| A2 | Cosmetic | Two blank separator lines from the schedule are missing: between `osc_wtbSyncBlendBlock()`'s closing `}` and the S089 P3 block comment (`Oscillator.c:3269`), and between the `osc_renderUserList()` declaration and the new comment block (`Oscillator.h:890`). There is no effect on the build: the comment-only step-4 check holds. | Optional: add the two blank lines. |

### 13.4 Open

- **Hardware H1–H6 (§9.3):** not yet reported. The image to flash is
  `build/LXRV2_lxr02.img` (683,952 B).
- **§10 documentation:** after hardware passes.
