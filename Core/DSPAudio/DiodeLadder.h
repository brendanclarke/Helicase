/*
 * DiodeLadder.h - TB-303 diode-ladder VCF, its feedback network and the VCA
 * (OTA) output, real-time form (S093 P0; kernels B2R, B-R, A-R and network
 * form N1 since S093 P3).
 *
 * What:       the per-sample TPT diode ladder (S092 P6 §3.2-§3.3, §5.2) closed
 *             through the 303's linear network in form N1 (S093 P1 §5: the
 *             instantaneous gains and one asymptote one-pole per output per
 *             sample, every network state per block), followed by the BA662A
 *             OTA (S092 P6 §3.5). Three kernels, one per class option
 *             (S093_P2_ALKALINE_B-R_PRE_IMPLEMENTATION.md §3):
 *               B2R (DiodeLadder_renderB2): rungs 0-1 nonlinear, rungs 2-3
 *                 linear; the chosen kernel (user, S093); 3 VDIV per sample;
 *               B-R (DiodeLadder_renderB): all four rungs nonlinear; the
 *                 Advanced-class alternative; 3 VDIV per sample;
 *               A-R (DiodeLadder_renderA): linear rungs; the cheaper Basic
 *                 fallback; 2 VDIV per sample.
 *             All three: the input pair by the [5/4] Pade with its tail
 *             beyond the clamp, a linear Q21 stage, the OTA one sample late
 *             (B2R/B-R: [3/2] Pade; A-R: [5/4]), the rung secants (B2R/B-R)
 *             by the unclamped [3/2] Pade. Each reads the oscillator block in
 *             place (int16, volts via sScale) and writes the OTA output in
 *             place (int16, pre-volume, DL_OUT_SCALE).
 * Why:        S092 P0 §4 / D2: the candidates are separate functions so each
 *             can be measured (fpseq.py, elf_looppath.py) and the class
 *             decision revisited; no runtime switch inside a loop. Only B2R
 *             is called (AlkalineVoice.c); --gc-sections drops the others.
 * Inputs:     the oscillator block in buf; the prewarped ladder current and
 *             the OTA control current at the block end (ramped per sample
 *             from the previous block's values); the Res-interpolated network
 *             (DiodeLadder_setNet(), once per block before the render).
 * Outputs:    buf (int16, OTA pin 6 voltage x DL_OUT_SCALE, saturated); the
 *             ladder and network state.
 * Accessors:  AlkalineVoice.c only.
 * Affiliates: AlkalineTables.c (alkResTable and the N1 constants, generated
 *             by tools/alkaline/gen_alk_tables.py); tools/dsp_test
 *             modules/alkaline.c; tools/alkaline/check_c.py;
 *             tools/alkaline/variants/dlv_kernel.c (the reference: MODEL 3 =
 *             B2R, MODEL 1 = B-R, MODEL 0 = A-R).
 */
#ifndef DIODELADDER_H_
#define DIODELADDER_H_

#include <stdint.h>

/* Electrical constants (S092 P6 §3; the SPICE runs' 27 C thermal voltage). */
#define DL_FS          44108.0f
#define DL_VT          0.02585f
#define DL_2VT         (2.0f * DL_VT)
#define DL_C18         18e-9f          /* bottom ladder capacitor */
#define DL_C19         33e-9f          /* C19 = C24 = C26 */
/* Output level: int16 full scale = 7 V at OTA pin 6 (S093 P1 D17): a driven
 * OTA reaches R_OTA x I_abc, about 6.7 V; the 303 at its own drive peaks at
 * about 1.7 V. */
#define DL_OUT_SCALE   (32767.0f / 7.0f)

/*
 * One Resonance point of the real-time network, form N1 (S093 P1 §5),
 * generated.
 *
 * What:       the block system z' = phi z + gam_rows0..ALK_NET_STATES-1 .
 *             w_mean (ALK_NET_STATES states (6, r6: the common balanced
 *             basis)), plus the two asymptote copies (rows ALK_NET_STATES and
 *             ALK_NET_STATES+1 of gam; decays ALK_NET_DECAY_U/V); block
 *             outputs r = cs . z - (the two asymptote states); the asymptote
 *             gains ga and the instantaneous gains d.
 *             Rows/first index: 0 = u (into Q12), 1 = v (OTA input).
 *             Columns of gam, ga, d: 0 = tp_pre, 1 = s.
 * Why:        every entry depends on Resonance only and varies smoothly with
 *             it, so linear interpolation between points is valid (S092 P6
 *             §5.3). N1 has no per-sample fast modes, so S093 P0's alf, bef
 *             and cf fields are gone.
 * Layout:     72 floats (288 B) per point at r6.
 */
/*
 * Block-system states of the slow network (S093 P3/P4: r6).
 *
 * What:       the size of DiodeLadderNet's phi/gam/cs and of the state z[].
 *             6 = the reduced network on one balanced basis common to all Res
 *             points (gen_alk_tables.py reduced_basis()); 8 = the physical
 *             capacitor basis.
 * Why:        r6 costs about 3.3 instructions per sample less (P3 §9.2 S5),
 *             its [SN] p7 ring is closer to the golden model (2.018 ms /
 *             0.363) and it is inaudible against 8 states (-43..-49 dB).
 *             Measured on the M7: DiodeLadderNet 288 B (432 at 8 states),
 *             DiodeLadder 232 B (248), AlkalineVoice 540 B (556).
 * Inputs:     none (compile time). The generated AlkalineTables.h #errors
 *             when its tables were made for another count.
 * Outputs:    the layouts of DiodeLadderNet and DiodeLadder.
 * Affiliates: DL_NET_ROWS (DiodeLadder.c: 6 / 2 phases = 3 rows per block);
 *             tools/alkaline/gen_alk_tables.py NET_STATES.
 */
#ifndef ALK_NET_STATES
#define ALK_NET_STATES 6u
#endif
typedef struct {
	float phi[ALK_NET_STATES][ALK_NET_STATES];
	float gam[ALK_NET_STATES + 2u][2];
	float cs[2][ALK_NET_STATES];
	float ga[2][2];
	float d[2][2];
} DiodeLadderNet;

/*
 * Ladder, network and OTA state of one voice (S093 P0, P3).
 *
 * What:       x0..x3 the four ladder capacitor voltages / 2 Vt; t0..t3, tin,
 *             tota the secant gains of the previous sample (rungs, input
 *             pair, OTA; B2R keeps t2 = t3 = 1, A-R all four); zo the C27/C25
 *             coupling state (V); qu, qv the asymptote one-poles;
 *             z[ALK_NET_STATES + 2] the block system; rU/rV the block outputs
 *             (this block's end and the
 *             previous); the previous sample's tp_pre, x_in, v_hp, OTA
 *             argument and I_abc; the ladder current and I_abc reached at the
 *             last block end; the per-block coefficient set (setNet).
 *             T2-2 (S093 P3): the block system advances once per
 *             ALK_NET_BLOCK (64) samples, split evenly over the window's
 *             render blocks (dl_slowUpdate() in DiodeLadder.c): the latched
 *             means and Res blend, z_new solved chunk by chunk into zn, the
 *             outputs committed with the last chunk and ramped (uStep,
 *             vStep, rk) over the next window.
 * Why:        S092 P0 §1.3: no state lives across a render outside the voice
 *             struct.
 * Layout:     232 B at r6 (measured, arm-none-eabi-gcc 14.2).
 */
typedef struct {
	float x0, x1, x2, x3;
	float t0, t1, t2, t3, tin, tota;
	float zo;
	float qu, qv;
	float z[ALK_NET_STATES + 2u];
	float rU, rV, rUPrev, rVPrev;
	float tpLast, xinLast, vhpLast, vcaLast;
	float iAbcLast;      /* the last sample's I_abc x R_OTA x DL_OUT_SCALE */
	float iLad;          /* prewarped ladder current reached (A) */
	float iAbc;          /* OTA control current reached (A) */
	/* per-block coefficients (DiodeLadder_setNet) */
	const DiodeLadderNet *netLo;
	const DiodeLadderNet *netHi;
	float netFrac;
	float gau0, gav0, ku0, ku1, dv0;
	/* T2-2: the block system per ALK_NET_BLOCK samples, split over blocks */
	float zn[ALK_NET_STATES];  /* z_new, solved chunk by chunk */
	float ruAcc, rvAcc;   /* the new outputs' chunk sums */
	float uStep, vStep;   /* the output ramp, per block */
	float accTp, accS;    /* the window's sums so far */
	float mTp, mS;        /* the latched window means */
	const DiodeLadderNet *latLo;
	const DiodeLadderNet *latHi;
	float latFrac;
	uint8_t rk;           /* blocks of output ramp left */
	uint8_t phase;        /* block index within the window */
} DiodeLadder;

void DiodeLadder_init(DiodeLadder *dl);
void DiodeLadder_setNet(DiodeLadder *dl, const DiodeLadderNet *lo,
                        const DiodeLadderNet *hi, float frac);
void DiodeLadder_renderB2(DiodeLadder *dl, int16_t *buf, float sScale,
                          float iLadEnd, float iAbcEnd);
void DiodeLadder_renderB2Osc(DiodeLadder *dl, int16_t *buf, const int16_t *row,
                             uint32_t *phase, uint32_t phaseInc, float sScale,
                             float iLadEnd, float iAbcEnd);
/*
 * One block's user-source read for a fused B2R loop (S093 P7, S1).
 *
 * What:       the per-block constants of the oscillator reader that a fused
 *             variant inlines; the caller fills it once per block from the
 *             bound catalogue item; the phase lives in the oscillator (the
 *             wrappers' phase pointer) and its step is their phaseInc.
 * Why:        plan §7.3 S1: the read fused into the ladder loop, as
 *             DiodeLadder_renderB2Osc() fused the table read (-9 per sample,
 *             AL-X); one compile-time variant per source, no source branch
 *             inside the loop.
 * Fields:     d16      smp/slp: the int16 frames (the item's base);
 *             endPh    smp: frames << 12, where the playhead holds;
 *             ls, le   slp: loop start / end frames (the seam reads ls);
 *             loopEndPh, lenPh  slp: le << 12 and (le - ls) << 12;
 *             gain     smp/slp: the item's gain code (osc_bindGainFor());
 *             fa, fb   wtb: frame base(s) (plain: fa only; blend: the pair);
 *             fa2, fb2 wtb blend: the second segment's pair;
 *             seg      wtb blend: the first sample of segment 2
 *                      (OUTPUT_DMA_SIZE when the block has one segment);
 *             ff, ff2, step  wtb blend: the frame fraction at each segment's
 *                      first sample and its per-sample change;
 *             len, bits  wtb: frame length (points) and depth (8 or 16).
 * Accessors:  DiodeLadder_renderB2Smp/Slp/Wtb/WtbBlend(); filled by
 *             AlkalineVoice.c alk_renderTyped().
 * Affiliates: Oscillator.c osc_smpPlainBlock(), osc_slpPlainBlock(),
 *             osc_wtbPlainBlock(), osc_wtbBlendBlock() (the arithmetic each
 *             variant reproduces bit for bit; bench law AL-X2).
 */
typedef struct {
	const int16_t *d16;
	uint32_t endPh;
	uint32_t ls, le, loopEndPh, lenPh;
	float    gain;
	const uint8_t *fa, *fb, *fa2, *fb2;
	uint32_t len;
	uint8_t  bits;
	uint8_t  seg;
	float    ff, ff2, step;
} DlSrc;

/* S093 P7: B2R with a user source's read fused (see DlSrc); buf is output
 * only; phase/phaseInc are the oscillator's (slp: the block step from
 * osc_slpBlockIncFor()). */
void DiodeLadder_renderB2Smp(DiodeLadder *dl, int16_t *buf, const DlSrc *src,
                             uint32_t *phase, uint32_t phaseInc, float sScale,
                             float iLadEnd, float iAbcEnd);
void DiodeLadder_renderB2Slp(DiodeLadder *dl, int16_t *buf, const DlSrc *src,
                             uint32_t *phase, uint32_t phaseInc, float sScale,
                             float iLadEnd, float iAbcEnd);
void DiodeLadder_renderB2Wtb(DiodeLadder *dl, int16_t *buf, const DlSrc *src,
                             uint32_t *phase, uint32_t phaseInc, float sScale,
                             float iLadEnd, float iAbcEnd);
void DiodeLadder_renderB2WtbBlend(DiodeLadder *dl, int16_t *buf, const DlSrc *src,
                                  uint32_t *phase, uint32_t phaseInc, float sScale,
                                  float iLadEnd, float iAbcEnd);
void DiodeLadder_renderB(DiodeLadder *dl, int16_t *buf, float sScale,
                         float iLadEnd, float iAbcEnd);
void DiodeLadder_renderA(DiodeLadder *dl, int16_t *buf, float sScale,
                         float iLadEnd, float iAbcEnd);

#endif /* DIODELADDER_H_ */
