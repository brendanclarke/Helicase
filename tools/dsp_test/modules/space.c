/*
 * tools/dsp_test/modules/space.c: the Space types' host laws (S095 P1,
 * §S18). Built by `make space` after the crumpbit target's extractions.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Host substitutes for firmware-only pieces ------------------------------ */

/* __USAT: the ARM instruction clamps a signed value to [0, 2^bits - 1]. */
static inline uint32_t host_usat(int32_t v, unsigned bits)
{
    const int32_t hi = (int32_t)((1u << bits) - 1u);

    return (uint32_t)(v < 0 ? 0 : (v > hi ? hi : v));
}
#define __USAT(v, b) host_usat((int32_t)(v), (b))

#define EFFECT_COMMON_PARAM_AUDIO_OUT 0u
#define EFFECT_COMMON_PARAM_LEVEL     1u
#define EFFECT_COMMON_PARAM_PAN       2u
#define EFFECT_COMMON_PARAM_COUNT     3u

/* seq_getBpm(): the test sets the tempo the DSP reads. */
static uint16_t cb_bpm = 120u;
uint16_t seq_getBpm(void) { return cb_bpm; }

/* S095 P1: the Effect feed slot lock (plan §5.7, D19). */
static uint8_t cb_feed_lock;
uint8_t effects_feedSlotLock(void) { return cb_feed_lock; }
void effects_setFeedSlotLock(uint8_t on) { cb_feed_lock = (on != 0u) ? 1u : 0u; }

/* GetRngValue() (random.h): the hardware word `grn` draws once per block. */
static uint32_t cb_hw_rng = 0xC0FFEEu;
int16_t GetRngValue(void)
{
    cb_hw_rng = cb_hw_rng * 1664525u + 1013904223u;
    return (int16_t)(cb_hw_rng >> 16);
}

#include "cb_stepscale.h"
#include "cb_stepscale.c"
#include "cb_fxbuf.c"
#include "cb_efftypes.c"
#include "cb_effects.c"
#include "cb_params.c"
#include "cb_tables.h"
#include "cb_tables.c"
#include "cb_midipitch.c"   /* midiPitch_detune(): `pit` fine (S091 P5) */
/* fxbuf_effectShare(): the test arena (write_param and the export read it). */
static fx_share_t sp_share;
void fxbuf_effectShare(fx_share_t *out) { *out = sp_share; }
#include "SpaceKit.host.h"
#include "SpaceKit.host.c"
#include "SpaceKitDsp.host.c"
#include "CrumpBitEffect.host.h"
#include "CrumpBitEffect.host.c"
#include "sp_types.c"
#include "sp_params.c"
#include "SpaceEffect.host.h"
#include "SpaceEffect.host.c"

/*
 * Space host tests (S095 P1, §S18): the Delay, Reverb and Echo DSP
 * (SpaceEffect.c, the real source) on a host arena.
 *
 * Laws:
 *   SP1 Delay timing: an impulse returns on L and R at the `tim` curve's
 *       delay (+-2 samples) at spr 63; at spr 126 R plays `tim` and L half
 *       of it (relative spread, §S18);
 *   SP2 Delay stability: noise at -6 dBFS for 4 s through fbk 127, deg 127,
 *       `wid` 0 and 127 (S100 P2), `rev` and `hld` on and off, `spr` and
 *       `tim` moving:
 *       finite and below 4.0 every sample;
 *   SP3 Reverb: an impulse's tail is present (energy 0.2-0.7 s > 0) and
 *       decays (0.7-1.2 s below 0.2-0.7 s at dcy 64); 10 s of frz 2 with
 *       shm 127 after noise stays finite and below 8.0 (R-7, shortened);
 *   SP4 Echo: each routing gives a finite, bounded, non-silent output for
 *       an impulse, and the three differ;
 *   SP7, SP9, SP10 (§S18): the longest `tim` returns
 *       on the long channel (spr relative); Reverb
 *       `ton` darkens below 127; the stopped-transport idle ramps, idles and
 *       wakes;
 *   SP12 (S100 P2) ping-pong: `xfb` 127, an impulse on L only returns
 *       on L at D, R at 2D, L at 3D, the other side below 1 % each time,
 *       every repeat the input's polarity; SP12b at `xfb` 32 and 64 with
 *       `spr` 62 and fbk 110, kicks build; SP12c `str` 0 (mono) keeps
 *       its level at every `xfb`; SP12d the mix keeps the loop's energy;
 *   SP13 (S100 P2) the loop band-pass: no DC latch (a DC input at fbk 127
 *       leaves the output's mean below the input's); `wid` 0 rings at the
 *       `ton` centre (+-5 %); `wid` 127 passes 200 Hz and 4 kHz repeats
 *       at the `fbk` gain (+-1 dB);
 *   SP8 (S100 P2) `hld` follows `fbk`; SP14 `deg` adds no noise and keeps
 *       the level; SP15 `wow` depth as pitch; SP16 `str`; SP17 `spr`'s
 *       darkening; SP18 no clicks as `hld` engages or with `rev`; SP19
 *       `ton` bipolar and `wid` slow at the top; SP20 `dck` depth (each
 *       law's own comment);
 *   SP5 the feed: a Delay holding a tone exports, a Reverb enters with the
 *       record: the feed runs with the slot lock set; at full wet its output
 *       carries the tail while the feed runs; the feed and
 *       the lock clear within 7 s.
 * Outputs: one line per failure; "space failures: N"; exit 1 if N > 0.
 * Accessors: make -C tools/dsp_test space.
 * Affiliates: SpaceEffect.c, SpaceKit.c, SpaceKitDsp.c.
 */
static int failures;

/* Each law's measured line is printed (the record), a failure flagged. */
static void expect(int cond, const char *msg)
{
    if (cond)
        printf("ok   %s\n", msg);
    if (!cond) {
        if (failures < 40)
            printf("FAIL: %s\n", msg);
        failures++;
    }
}

static uint8_t sp_arena[SK_ARENA_PREF_BYTES] __attribute__((aligned(32)));
static SpaceRuntime sp_rt;
static uint32_t sp_rng = 12345u;

static float sp_noise(void)
{
    sp_rng = sp_rng * 1664525u + 1013904223u;
    return (float)(int32_t)sp_rng * (1.0f / 2147483648.0f);
}

/* Enter a type on the full arena with an optional handoff, all rows set. */
static void sp_enter(const effect_type_ops_t *ops, const fxbuf_handoff_t *h,
                     const uint8_t *rows, uint32_t nrows)
{
    uint32_t i;

    sp_share.base = sp_arena;
    sp_share.offset = 0u;
    sp_share.bytes = sizeof(sp_arena);
    memset(&sp_rt, 0, sizeof(sp_rt));
    ops->init(&sp_rt, h, &sp_share);
    for (i = 0u; i < nrows; i++)
        ops->write_param(&sp_rt, (uint8_t)(i + 3u), rows[i]);
}

static void sp_block(const effect_type_ops_t *ops, float *l, float *r)
{
    effect_io_t io;

    io.l = l;
    io.r = r;
    io.frames = 32u;
    io.channels = 2u;
    io.share = &sp_share;
    ops->process(&sp_rt, &io);
}

/* Every sample finite and below lim. */
static int sp_bounded(const float *x, uint32_t n, float lim)
{
    uint32_t i;

    for (i = 0u; i < n; i++)
        if (!(fabsf(x[i]) < lim))
            return 0;
    return 1;
}

/* A row's column in a rows[] array (rows 3.. of the type). */
#define SP_COL(row_) ((row_) - 3u)

/*
 * tim fbk ton wid xfb spr str wow deg dck hld rev (S100 P2; `ton` and `str`
 * bipolar, 63 = 0 on the page)
 */
#define SP_DLY_ROWS 12u
static const uint8_t sp_dlyRows[SP_DLY_ROWS] = { 64u, 0u, 63u, 127u, 0u,
                                                 63u, 63u, 0u, 0u, 0u, 0u,
                                                 0u };

static void law_sp1(void)
{
    uint32_t pass;
    char msg[160];

    for (pass = 0u; pass < 2u; pass++) {
        uint8_t rows[SP_DLY_ROWS];
        const float t = crumpBit_delaySamples(64u, 120u);
        const uint32_t at = 3200u;
        float l[32];
        float r[32];
        uint32_t b;
        uint32_t n;
        uint32_t pl = 0u;
        uint32_t pr = 0u;
        float ml = 0.0f;
        float mr = 0.0f;

        memcpy(rows, sp_dlyRows, sizeof(rows));
        rows[SP_COL(SPACE_DLY_SPREAD)] = pass ? 126u : 63u;
        cb_bpm = 120u;
        sp_enter(&space_delayOps, NULL, rows, SP_DLY_ROWS);
        /* Settle the delay glide from the floor (150 ms one-pole). */
        for (b = 0u; b < 9000u; b++) {
            memset(l, 0, sizeof(l));
            memset(r, 0, sizeof(r));
            sp_block(&space_delayOps, l, r);
        }
        for (b = 0u; b < (uint32_t)(1.2f * t + (float)at) / 32u + 4u; b++) {
            for (n = 0u; n < 32u; n++)
                l[n] = r[n] = (b * 32u + n == at) ? 1.0f : 0.0f;
            sp_block(&space_delayOps, l, r);
            for (n = 0u; n < 32u; n++) {
                const uint32_t f = b * 32u + n;

                if (f > at + 40u && fabsf(l[n]) > ml) { ml = fabsf(l[n]); pl = f; }
                if (f > at + 40u && fabsf(r[n]) > mr) { mr = fabsf(r[n]); pr = f; }
            }
        }
        /* spr relative (§S18): at 126 R plays `tim` and L half of it. */
        snprintf(msg, sizeof msg, "SP1 spr %u: L at %u want %.0f (peak %.3f)",
                 rows[SP_COL(SPACE_DLY_SPREAD)], pl - at, pass ? 0.5f * t : t, ml);
        expect(fabsf((float)(pl - at) - (pass ? 0.5f * t : t)) <= 2.0f &&
                   ml > 0.3f, msg);
        snprintf(msg, sizeof msg, "SP1 spr %u: R at %u want %.0f (peak %.3f)",
                 rows[SP_COL(SPACE_DLY_SPREAD)], pr - at, t, mr);
        expect(fabsf((float)(pr - at) - t) <= 2.0f && mr > 0.3f, msg);
    }
}

static void law_sp2(void)
{
    uint8_t rows[SP_DLY_ROWS];
    float l[32];
    float r[32];
    uint32_t b;
    uint32_t n;
    int ok = 1;

    memcpy(rows, sp_dlyRows, sizeof(rows));
    rows[SP_COL(SPACE_DLY_TIME)] = 40u;
    rows[SP_COL(SPACE_DLY_FEEDBACK)] = 127u;
    rows[SP_COL(SPACE_DLY_XFEED)] = 64u;
    rows[SP_COL(SPACE_DLY_DEGRADE)] = 127u;
    rows[SP_COL(SPACE_DLY_WOW)] = 127u;
    rows[SP_COL(SPACE_DLY_HOLD)] = 1u;   /* hld on part of the time (written below too) */
    sp_enter(&space_delayOps, NULL, rows, SP_DLY_ROWS);
    for (b = 0u; b < 5513u && ok; b++) {
        if ((b & 511u) == 0u) {
            space_delayOps.write_param(&sp_rt, SPACE_DLY_WIDTH,
                                       ((b >> 9) & 1u) ? 0u : 127u);
            space_delayOps.write_param(&sp_rt, SPACE_DLY_TONE,
                                       (uint8_t)((b >> 6) & 127u));
            space_delayOps.write_param(&sp_rt, SPACE_DLY_HOLD,
                                       (uint8_t)((b >> 10) & 1u));
            space_delayOps.write_param(&sp_rt, SPACE_DLY_REVERSE,
                                       (uint8_t)((b >> 9) & 1u));
            space_delayOps.write_param(&sp_rt, SPACE_DLY_SPREAD,
                                       (uint8_t)((b >> 7) & 127u));
            space_delayOps.write_param(&sp_rt, SPACE_DLY_TIME,
                                       (uint8_t)(20u + (b >> 9) * 9u));
        }
        for (n = 0u; n < 32u; n++) {
            l[n] = 0.5f * sp_noise();
            r[n] = 0.5f * sp_noise();
        }
        sp_block(&space_delayOps, l, r);
        ok = sp_bounded(l, 32u, 4.0f) && sp_bounded(r, 32u, 4.0f);
    }
    expect(ok, "SP2 Delay: a sample is not finite or above 4.0");
}

/* pre siz dcy dif lsh lsf hsh hsf mod wid frz shm int dtn env dck ton */
#define SP_RVB_ROWS 17u
static const uint8_t sp_rvbRows[SP_RVB_ROWS] = { 0u, 96u, 64u, 96u, 63u,
                                                 40u, 40u, 80u, 32u, 63u, 0u,
                                                 0u, 75u, 0u, 63u, 0u, 127u };

static void law_sp3(void)
{
    uint8_t rows[SP_RVB_ROWS];
    float l[32];
    float r[32];
    double e1 = 0.0;
    double e2 = 0.0;
    uint32_t b;
    uint32_t n;
    int ok = 1;
    char msg[160];

    memcpy(rows, sp_rvbRows, sizeof(rows));
    sp_enter(&space_reverbOps, NULL, rows, SP_RVB_ROWS);
    for (b = 0u; b < 1700u; b++) {
        for (n = 0u; n < 32u; n++)
            l[n] = r[n] = (b == 10u && n == 0u) ? 1.0f : 0.0f;
        sp_block(&space_reverbOps, l, r);
        ok = ok && sp_bounded(l, 32u, 8.0f) && sp_bounded(r, 32u, 8.0f);
        for (n = 0u; n < 32u; n++) {
            const double t = (double)(b * 32u + n) / 44108.0;

            if (t > 0.2 && t < 0.7)
                e1 += (double)l[n] * l[n] + (double)r[n] * r[n];
            else if (t > 0.7 && t < 1.2)
                e2 += (double)l[n] * l[n] + (double)r[n] * r[n];
        }
    }
    snprintf(msg, sizeof msg,
             "SP3 Reverb tail: e(0.2-0.7) %.3g e(0.7-1.2) %.3g (bounded %d)",
             e1, e2, ok);
    expect(ok && e1 > 1e-6 && e2 < e1, msg);
    /* R-7 (shortened): freeze with shimmer after noise, 10 s. */
    rows[10] = SPACE_FREEZE_SHIMMER;  /* frz: freeze with the shimmer */
    rows[11] = 127u;
    sp_enter(&space_reverbOps, NULL, rows, SP_RVB_ROWS);
    space_reverbOps.write_param(&sp_rt, SPACE_RVB_FREEZE, 0u);
    for (b = 0u; b < 13784u && ok; b++) {
        if (b == 200u)
            space_reverbOps.write_param(&sp_rt, SPACE_RVB_FREEZE,
                                        SPACE_FREEZE_SHIMMER);
        for (n = 0u; n < 32u; n++)
            l[n] = r[n] = (b < 200u) ? 0.5f * sp_noise() : 0.0f;
        sp_block(&space_reverbOps, l, r);
        ok = sp_bounded(l, 32u, 8.0f) && sp_bounded(r, 32u, 8.0f);
    }
    expect(ok, "SP3 Reverb freeze+shimmer: a sample is not finite or above 8.0");
}

/* tim fbk ton wid spr str wow deg dck e>r bln dcy lsh hsh rtg (S100 P2) */
#define SP_ECH_ROWS 15u
static const uint8_t sp_echRows[SP_ECH_ROWS] = { 64u, 64u, 63u, 127u, 63u,
                                                 63u, 0u, 0u, 0u, 64u, 48u,
                                                 48u, 63u, 40u, 0u };

static void law_sp4(void)
{
    double sum[3];
    uint32_t route;
    char msg[160];

    for (route = 0u; route < 3u; route++) {
        uint8_t rows[SP_ECH_ROWS];
        float l[32];
        float r[32];
        uint32_t b;
        uint32_t n;
        int ok = 1;

        memcpy(rows, sp_echRows, sizeof(rows));
        rows[SP_COL(SPACE_ECH_ROUTING)] = (uint8_t)route;   /* rtg stops 0, 1, 2 */
        sp_enter(&space_echoOps, NULL, rows, SP_ECH_ROWS);
        sum[route] = 0.0;
        for (b = 0u; b < 3000u; b++) {
            for (n = 0u; n < 32u; n++)
                l[n] = r[n] = (b == 10u && n == 0u) ? 1.0f : 0.0f;
            sp_block(&space_echoOps, l, r);
            ok = ok && sp_bounded(l, 32u, 8.0f) && sp_bounded(r, 32u, 8.0f);
            for (n = 0u; n < 32u; n++)
                sum[route] += (double)l[n] * l[n];
        }
        snprintf(msg, sizeof msg, "SP4 Echo routing %u: energy %.3g (bounded %d)",
                 route, sum[route], ok);
        expect(ok && sum[route] > 1e-4, msg);
    }
    expect(fabs(sum[0] - sum[1]) > 1e-6 * sum[0] &&
               fabs(sum[0] - sum[2]) > 1e-6 * sum[0],
           "SP4 Echo: the routings give the same output");
}

static void law_sp5(void)
{
    static fxbuf_handoff_t h;
    uint8_t rows[SP_RVB_ROWS];
    float l[32];
    float r[32];
    uint32_t b;
    uint32_t n;
    double tail = 0.0;
    uint32_t end = 0u;
    char msg[200];

    /* A Delay holding a tone. */
    sp_enter(&space_delayOps, NULL, sp_dlyRows, SP_DLY_ROWS);
    for (b = 0u; b < 3000u; b++) {
        for (n = 0u; n < 32u; n++)
            l[n] = r[n] = 0.5f * sinf((float)(b * 32u + n) * 0.05f);
        sp_block(&space_delayOps, l, r);
    }
    memset(&h, 0, sizeof(h));
    for (n = 0u; n < FXBUF_HANDOFF_POINTER_COUNT; n++)
        h.read_offset[n] = h.write_offset[n] = FXBUF_OFFSET_NONE;
    space_delayOps.export_handoff(&sp_rt, &h);
    expect((h.state_flags & FXBUF_STATE_EFFECT_WRITTEN) != 0u,
           "SP5: the Delay's export has no audio");
    /* The Reverb enters with it, its plate silent (dcy 0, no input). */
    memcpy(rows, sp_rvbRows, sizeof(rows));
    rows[2] = 0u;
    cb_feed_lock = 0u;
    sp_enter(&space_reverbOps, &h, rows, SP_RVB_ROWS);
    expect(cb_feed_lock == 1u && sk_feeding(&sp_share, EFFECT_TYPE_REVERB),
           "SP5: no feed (or no lock) after the Reverb's init");
    for (b = 0u; b < 9650u; b++) {
        memset(l, 0, sizeof(l));
        memset(r, 0, sizeof(r));
        sp_block(&space_reverbOps, l, r);
        if (sk_feeding(&sp_share, EFFECT_TYPE_REVERB))
            for (n = 0u; n < 32u; n++)
                tail += (double)l[n] * l[n];
        else if (!end)
            end = b;
    }
    snprintf(msg, sizeof msg,
             "SP5: tail energy %.3g during the feed, ended at block %u, lock %u",
             tail, end, cb_feed_lock);
    expect(end != 0u && end < 9650u && cb_feed_lock == 0u && tail > 1e-3,
           msg);
}

/*
 * SP7 (§S18): the longest `tim` the arena holds (its clamp) returns on the
 * long channel at the curve's time, for spr 0 (L long) and 127 (R long).
 */
static void law_sp7(void)
{
    uint32_t pass;
    char msg[160];

    for (pass = 0u; pass < 2u; pass++) {
        uint8_t rows[SP_DLY_ROWS];
        float l[32];
        float r[32];
        uint8_t lim;
        float t;
        uint32_t b;
        uint32_t n;
        uint32_t pk = 0u;
        float mk = 0.0f;
        const uint32_t at = 64u;

        cb_bpm = 120u;
        sp_share.base = sp_arena;
        sp_share.bytes = sizeof(sp_arena);
        lim = space_timeLimit(SPACE_TAG_DELAY, &sp_share);
        t = crumpBit_delaySamples(lim, 120u);
        memcpy(rows, sp_dlyRows, sizeof(rows));
        rows[SP_COL(SPACE_DLY_TIME)] = lim;
        rows[SP_COL(SPACE_DLY_SPREAD)] = pass ? 127u : 0u;
        sp_enter(&space_delayOps, NULL, rows, SP_DLY_ROWS);
        /* Start at the target: no glide from the floor. */
        sp_rt.d[0] = sp_rt.d[1] = (float)(sp_rt.tape_n - 3u);
        sp_rt.dr[0] = sp_rt.dr[1] = sp_rt.d[0];
        sp_rt.v = (float)(sp_rt.tape_n - 3u) / t;
        for (b = 0u; b < (uint32_t)(1.1f * t) / 32u + 8u; b++) {
            for (n = 0u; n < 32u; n++)
                l[n] = r[n] = (b * 32u + n == at) ? 1.0f : 0.0f;
            sp_block(&space_delayOps, l, r);
            for (n = 0u; n < 32u; n++) {
                const float y = fabsf(pass ? r[n] : l[n]);

                if (b * 32u + n > at + 40u && y > mk) { mk = y; pk = b * 32u + n; }
            }
        }
        snprintf(msg, sizeof msg,
                 "SP7 tim %u (limit) spr %u: the long side returns at %u, "
                 "want %.0f +-1%% (peak %.3f)", lim, rows[SP_COL(SPACE_DLY_SPREAD)],
                 pk - at, t, mk);
        expect(mk > 0.1f && fabsf((float)(pk - at) - t) < 0.01f * t, msg);
    }
}

/*
 * SP8 (S100 P2): `hld` follows `fbk`. tim 40, a noise burst below about
 * 3 kHz, then hold; the L+R energy of the hold's second 4 against the
 * second from 0.25 s to 1.25 s (after the 50 ms crossfade and the first
 * passes, which still carry the pre-hold level):
 * (a) fbk 20, wid 127: the same level, +-1 dB;
 * (b) fbk 64, wid 127: grows slowly, +1 .. +6 dB (the law: 0.74 dB/s);
 * (c) fbk 120, wid 0: grows although the band is narrow, at least +6 dB.
 */
static double sp8_run(uint8_t fbk, uint8_t wid, double *e1)
{
    uint8_t rows[SP_DLY_ROWS];
    float l[32];
    float r[32];
    double e4 = 0.0;
    float lp_l = 0.0f;
    float lp_r = 0.0f;
    uint32_t b;
    uint32_t n;

    memcpy(rows, sp_dlyRows, sizeof(rows));
    rows[SP_COL(SPACE_DLY_TIME)] = 40u;
    rows[SP_COL(SPACE_DLY_FEEDBACK)] = fbk;
    rows[SP_COL(SPACE_DLY_WIDTH)] = wid;
    cb_bpm = 120u;
    sp_enter(&space_delayOps, NULL, rows, SP_DLY_ROWS);
    *e1 = 0.0;
    for (b = 0u; b < 7000u; b++) {
        if (b == 1500u)
            space_delayOps.write_param(&sp_rt, SPACE_DLY_HOLD, 1u);
        for (n = 0u; n < 32u; n++) {
            /* Noise below about 3 kHz (a one-pole), as programme. */
            lp_l += 0.35f * (((b < 1400u) ? 0.1f * sp_noise() : 0.0f) - lp_l);
            lp_r += 0.35f * (((b < 1400u) ? 0.1f * sp_noise() : 0.0f) - lp_r);
            l[n] = lp_l;
            r[n] = lp_r;
        }
        sp_block(&space_delayOps, l, r);
        for (n = 0u; n < 32u; n++) {
            const double v = (double)l[n] * l[n] + (double)r[n] * r[n];

            if (b >= 1845u && b < 3223u)
                *e1 += v;
            else if (b >= 5622u)
                e4 += v;
        }
    }
    return 10.0 * log10(e4 / *e1);
}

static void law_sp8(void)
{
    double e1;
    double g;
    char msg[160];

    g = sp8_run(20u, 127u, &e1);
    snprintf(msg, sizeof msg, "SP8a hld fbk 20 wid 127: second 4 %+.2f dB on second 1 (+-1)", g);
    expect(e1 > 1e-3 && fabs(g) <= 1.0, msg);
    g = sp8_run(64u, 127u, &e1);
    snprintf(msg, sizeof msg, "SP8b hld fbk 64 wid 127: second 4 %+.2f dB on second 1 (+1 .. +6)", g);
    expect(e1 > 1e-3 && g >= 1.0 && g <= 6.0, msg);
    g = sp8_run(120u, 0u, &e1);
    snprintf(msg, sizeof msg, "SP8c hld fbk 120 wid 0: second 4 %+.2f dB on second 1 (>= +6)", g);
    expect(e1 > 1e-9 && g >= 6.0, msg);
}

/* SP9 (§S18): Reverb `ton` 0 darkens the tail; 127 matches the flat path. */
static void law_sp9(void)
{
    double e[2];
    uint32_t pass;
    char msg[160];

    for (pass = 0u; pass < 2u; pass++) {
        uint8_t rows[SP_RVB_ROWS];
        float l[32];
        float r[32];
        float prev = 0.0f;
        uint32_t b;
        uint32_t n;

        memcpy(rows, sp_rvbRows, sizeof(rows));
        rows[16] = pass ? 0u : 127u;
        sp_enter(&space_reverbOps, NULL, rows, SP_RVB_ROWS);
        e[pass] = 0.0;
        for (b = 0u; b < 600u; b++) {
            /* No dry: the entry starts at 0 (SD2) and no mix row exists. */
            for (n = 0u; n < 32u; n++)
                l[n] = r[n] = (b == 200u && n == 0u) ? 1.0f : 0.0f;
            sp_block(&space_reverbOps, l, r);
            for (n = 0u; n < 32u && b > 200u; n++) {
                /* High-band energy: the first difference. */
                e[pass] += (double)(l[n] - prev) * (l[n] - prev);
                prev = l[n];
            }
        }
    }
    snprintf(msg, sizeof msg, "SP9 rvb ton: HF energy 127 %.3g, 0 %.3g", e[0],
             e[1]);
    expect(e[1] < 0.5 * e[0], msg);
}

/*
 * SP10 (§S18): the stopped-transport idle. Silent blocks with the
 * transport stopped: the return gain falls to 0 over the 1 s ramp after
 * the 14 quiet blocks, then processing is skipped and the outputs zeroed;
 * input above -90 dBFS wakes it at gain 1; a running transport resets it.
 */
static void law_sp10(void)
{
    float l[32];
    float r[32];
    effect_io_t io;
    uint32_t b;
    uint32_t idle_at = 0u;
    float g_mid = -1.0f;
    char msg[200];

    memset(sp_arena, 0, SK_PREFIX_BYTES);
    sp_share.base = sp_arena;
    sp_share.bytes = sizeof(sp_arena);
    sk_idleReset(&sp_share);
    io.l = l;
    io.r = r;
    io.frames = 32u;
    io.channels = 2u;
    io.share = &sp_share;
    for (b = 0u; b < 1500u; b++) {
        memset(l, 0, sizeof(l));
        memset(r, 0, sizeof(r));
        if (sk_idleBefore(&io, 0u)) {
            if (!idle_at)
                idle_at = b;
            continue;
        }
        sk_idleAfter(&io);
        if (b == 14u + 689u)
            g_mid = sk_idleGain(&sp_share);
    }
    l[0] = 1e-3f;
    {
        const uint8_t skipped = sk_idleBefore(&io, 0u);
        const float g_wake = sk_idleGain(&sp_share);

        snprintf(msg, sizeof msg,
                 "SP10 idle: gain at mid-ramp %.3f, idle from block %u, "
                 "wakes on -60 dBFS (skipped %u, gain %.1f)", g_mid, idle_at,
                 skipped, g_wake);
        expect(g_mid > 0.45f && g_mid < 0.55f && idle_at >= 1392u &&
                   idle_at <= 1394u && skipped == 0u && g_wake == 1.0f, msg);
    }
}

/*
 * SP11 (S096 P2 F1): no frame slips in the heads or the reverse head.
 *
 * What:       a 440 Hz sine at 0.5 into the Delay (feedback 0) and the Echo
 *             (feedback 0), 4 s after the glides settle; the largest third
 *             difference of the output L. A smooth (or smoothly wobbled)
 *             sine gives about 1.2e-4 and the linear main head's moving
 *             fraction about 4e-3; a one-frame slip of this sine 0.06 ..
 *             0.09. Cases: Delay at wow 127; Delay reverse at wow 0 and
 *             127; Echo at wow 127 (S100 P2: one head, no `hed`). Each must
 *             stay below 0.012.
 * Why:        the D/3 and 2D/3 heads and the reverse windows read the
 *             nearest frame before S096 P2, which slipped as soon as wow
 *             (or the reverse ramp) moved their positions by fractions: the
 *             user's quiet popping (S096_P2_INSTRUMENT_FIXES.md §1).
 */
static float sp11_buf[44100u * 4u];

static float sp11_run(const effect_type_ops_t *ops, const uint8_t *rows, uint32_t nrows)
{
    float l[32];
    float r[32];
    uint32_t b;
    uint32_t n;
    uint32_t k = 0u;
    uint32_t ph = 0u;
    float mx = 0.0f;

    cb_bpm = 120u;
    sp_enter(ops, NULL, rows, nrows);
    for (b = 0u; b < 9000u + (44100u * 4u) / 32u; b++) {
        for (n = 0u; n < 32u; n++)
            l[n] = r[n] = 0.5f * sinf(2.0f * 3.14159265f * 440.0f * (float)(ph++) / 44108.07f);
        sp_block(ops, l, r);
        if (b >= 9000u)
            for (n = 0u; n < 32u; n++)
                sp11_buf[k++] = l[n];
    }
    for (n = 3u; n < k; n++) {
        const float d3 = sp11_buf[n] - 3.0f * sp11_buf[n - 1u] +
                         3.0f * sp11_buf[n - 2u] - sp11_buf[n - 3u];

        if (fabsf(d3) > mx)
            mx = fabsf(d3);
    }
    return mx;
}

static void law_sp11(void)
{
    static const struct { uint8_t rev, wow; const char *what; } dly[3] = {
        { 0u, 127u, "Delay, wow 127" },
        { 1u, 0u, "Delay reverse, wow 0" },
        { 1u, 127u, "Delay reverse, wow 127" },
    };
    char msg[160];
    uint32_t c;

    for (c = 0u; c < 3u; c++) {
        uint8_t rows[SP_DLY_ROWS];
        float mx;

        memcpy(rows, sp_dlyRows, sizeof(rows));
        rows[SP_COL(SPACE_DLY_FEEDBACK)] = 0u;
        rows[SP_COL(SPACE_DLY_WOW)] = dly[c].wow;
        rows[SP_COL(SPACE_DLY_REVERSE)] = dly[c].rev;
        mx = sp11_run(&space_delayOps, rows, SP_DLY_ROWS);
        snprintf(msg, sizeof msg, "SP11 %s: largest third difference %.4f (< 0.012)",
                 dly[c].what, mx);
        expect(mx < 0.012f, msg);
    }
    {
        uint8_t rows[SP_ECH_ROWS];
        float mx;

        memcpy(rows, sp_echRows, sizeof(rows));
        rows[SP_COL(SPACE_ECH_FEEDBACK)] = 0u;
        rows[SP_COL(SPACE_ECH_WOW)] = 127u;
        mx = sp11_run(&space_echoOps, rows, SP_ECH_ROWS);
        snprintf(msg, sizeof msg, "SP11 Echo, wow 127: largest third difference %.4f (< 0.012)",
                 mx);
        expect(mx < 0.012f, msg);
    }
}

/*
 * SP12 (S100 P2): `xfb` 127 ping-pongs. An impulse on L only (spr 63, fbk
 * 100, wid 127): the first three repeats land on L at D, R at 2D and L at
 * 3D (+-2 samples), each with the other side below 1 % of it.
 */
static void law_sp12(void)
{
    uint8_t rows[SP_DLY_ROWS];
    const float t = crumpBit_delaySamples(64u, 120u);
    const uint32_t at = 3200u;
    float pkl[3] = { 0.0f, 0.0f, 0.0f };
    float pkr[3] = { 0.0f, 0.0f, 0.0f };
    float sg[3] = { 0.0f, 0.0f, 0.0f };
    uint32_t pos[3] = { 0u, 0u, 0u };
    float l[32];
    float r[32];
    uint32_t b;
    uint32_t n;
    uint32_t k;
    int ok = 1;
    char msg[200];

    memcpy(rows, sp_dlyRows, sizeof(rows));
    rows[SP_COL(SPACE_DLY_FEEDBACK)] = 100u;
    rows[SP_COL(SPACE_DLY_XFEED)] = 127u;
    cb_bpm = 120u;
    sp_enter(&space_delayOps, NULL, rows, SP_DLY_ROWS);
    for (b = 0u; b < 9000u; b++) {
        memset(l, 0, sizeof(l));
        memset(r, 0, sizeof(r));
        sp_block(&space_delayOps, l, r);
    }
    for (b = 0u; b < (uint32_t)(3.5f * t + (float)at) / 32u; b++) {
        for (n = 0u; n < 32u; n++) {
            l[n] = (b * 32u + n == at) ? 1.0f : 0.0f;
            r[n] = 0.0f;
        }
        sp_block(&space_delayOps, l, r);
        for (n = 0u; n < 32u; n++) {
            const float f = (float)(b * 32u + n) - (float)at;

            /* Window k: (k + 0.5) D .. (k + 1.5) D around repeat k + 1. */
            for (k = 0u; k < 3u; k++)
                if (f > ((float)k + 0.5f) * t && f < ((float)k + 1.5f) * t) {
                    if (fabsf(l[n]) > pkl[k]) {
                        pkl[k] = fabsf(l[n]);
                        if (k != 1u) {
                            pos[k] = (uint32_t)f;
                            sg[k] = l[n];
                        }
                    }
                    if (fabsf(r[n]) > pkr[k]) {
                        pkr[k] = fabsf(r[n]);
                        if (k == 1u) {
                            pos[k] = (uint32_t)f;
                            sg[k] = r[n];
                        }
                    }
                }
        }
    }
    for (k = 0u; k < 3u; k++) {
        const float on = (k == 1u) ? pkr[k] : pkl[k];
        const float off = (k == 1u) ? pkl[k] : pkr[k];

        ok = ok && on > 0.2f && off < 0.01f * on && sg[k] > 0.0f &&
             fabsf((float)pos[k] - (float)(k + 1u) * t) <= 2.0f;
    }
    snprintf(msg, sizeof msg,
             "SP12 ping-pong: L %.3f/%.3f at %u, R %.3f/%.3f at %u, "
             "L %.3f/%.3f at %u (D %.0f)", pkl[0], pkr[0], pos[0], pkr[1],
             pkl[1], pos[1], pkl[2], pkr[2], pos[2], t);
    expect(ok, msg);
}

/*
 * SP12b (S100 P2): at `xfb` 64, `spr` 62 (R 1 % shorter) and fbk 110, a
 * 55 Hz kick every beat builds: the L+R level of second 8 is at least 4 dB
 * above second 1 (measured +6.6; the L/R average +0.6, the old law +2.5). An L/R average at this row cancelled the low end where
 * the two times differ, and the loop sat at one level.
 */
/*
 * SP12b/SP12c helper (S100 P2): the Delay at tim 90, 120 bpm, with the
 * given fbk / spr / xfb / str, fed a 55 Hz kick every beat (0.5 peak, a
 * 45 ms decay) on both inputs for 8 s; e[0..7] gets the L+R output energy
 * of each second.
 */
static void sp12_kicks(uint8_t fbk, uint8_t spr, uint8_t xfb, uint8_t str,
                       double *e)
{
    uint8_t rows[SP_DLY_ROWS];
    float l[32];
    float r[32];
    uint32_t b;
    uint32_t n;
    uint32_t t = 0u;

    memcpy(rows, sp_dlyRows, sizeof(rows));
    rows[SP_COL(SPACE_DLY_TIME)] = 90u;
    rows[SP_COL(SPACE_DLY_FEEDBACK)] = fbk;
    rows[SP_COL(SPACE_DLY_SPREAD)] = spr;
    rows[SP_COL(SPACE_DLY_XFEED)] = xfb;
    rows[SP_COL(SPACE_DLY_STEREO)] = str;
    for (b = 0u; b < 8u; b++)
        e[b] = 0.0;
    cb_bpm = 120u;
    sp_enter(&space_delayOps, NULL, rows, SP_DLY_ROWS);
    for (b = 0u; b < 8u * 1378u; b++) {
        for (n = 0u; n < 32u; n++, t++) {
            const uint32_t ph = t % 22054u;

            l[n] = r[n] = (ph < 8000u)
                ? 0.5f * expf(-(float)ph / 2000.0f) *
                      sinf(6.2831853f * 55.0f * (float)ph / 44108.07f)
                : 0.0f;
        }
        sp_block(&space_delayOps, l, r);
        for (n = 0u; n < 32u; n++)
            e[b / 1378u] += (double)l[n] * l[n] + (double)r[n] * r[n];
    }
}

static void law_sp12b(void)
{
    static const uint8_t xfb[2] = { 32u, 64u };
    double e[8];
    char msg[160];
    uint32_t c;

    for (c = 0u; c < 2u; c++) {
        sp12_kicks(110u, 62u, xfb[c], 63u, e);
        snprintf(msg, sizeof msg,
                 "SP12b xfb %u, spr 62, fbk 110: second 8 is %+.1f dB on second 1 (>= +4)",
                 xfb[c], 10.0 * log10(e[7] / e[0]));
        expect(e[7] >= 2.51 * e[0], msg);
    }
}

/*
 * SP12c (S100 P2): `str` 0 (mono) keeps its level as `xfb` rises: kicks,
 * fbk 90, spr 63; the energy of seconds 2..8 at xfb 64 and 127 within 1 dB
 * of xfb 0 (a rotation lost cos(angle) of the mid each pass: every other
 * repeat cancelled in mono at 127).
 */
static void law_sp12c(void)
{
    static const uint8_t xfb[3] = { 0u, 64u, 127u };
    double e[8];
    double s[3];
    char msg[200];
    uint32_t c;
    uint32_t k;

    for (c = 0u; c < 3u; c++) {
        sp12_kicks(90u, 63u, xfb[c], 0u, e);
        s[c] = 0.0;
        for (k = 1u; k < 8u; k++)
            s[c] += e[k];
    }
    snprintf(msg, sizeof msg,
             "SP12c str 0: xfb 64 %+.2f dB, xfb 127 %+.2f dB on xfb 0 (+-1)",
             10.0 * log10(s[1] / s[0]), 10.0 * log10(s[2] / s[0]));
    expect(fabs(10.0 * log10(s[1] / s[0])) <= 1.0 &&
               fabs(10.0 * log10(s[2] / s[0])) <= 1.0, msg);
}

/* SP13 helper: run the Delay (tim 20) on a source for nb blocks. */
static void sp13_run(uint8_t fbk, uint8_t ton, uint8_t wid,
                     float (*src)(uint32_t), uint32_t nb, float *out)
{
    uint8_t rows[SP_DLY_ROWS];
    float l[32];
    float r[32];
    uint32_t b;
    uint32_t n;

    memcpy(rows, sp_dlyRows, sizeof(rows));
    rows[SP_COL(SPACE_DLY_TIME)] = 20u;
    rows[SP_COL(SPACE_DLY_FEEDBACK)] = fbk;
    rows[SP_COL(SPACE_DLY_TONE)] = ton;
    rows[SP_COL(SPACE_DLY_WIDTH)] = wid;
    cb_bpm = 120u;
    sp_enter(&space_delayOps, NULL, rows, SP_DLY_ROWS);
    /* Settle the delay glide from the floor (150 ms one-pole), as SP1. */
    for (b = 0u; b < 9000u; b++) {
        memset(l, 0, sizeof(l));
        memset(r, 0, sizeof(r));
        sp_block(&space_delayOps, l, r);
    }
    for (b = 0u; b < nb; b++) {
        for (n = 0u; n < 32u; n++)
            l[n] = r[n] = src(b * 32u + n);
        sp_block(&space_delayOps, l, r);
        for (n = 0u; n < 32u; n++)
            out[b * 32u + n] = l[n];
    }
}

static float sp13_dc(uint32_t i) { (void)i; return 0.25f; }
static float sp13_burst(uint32_t i) { return (i < 2000u) ? 0.3f * sp_noise() : 0.0f; }
static float sp13_f;
static float sp13_tone(uint32_t i)
{
    return (i < 1500u) ? 0.25f * sinf(6.2831853f * sp13_f * (float)i / 44108.07f)
                       : 0.0f;
}
static float sp13_buf[44100u * 6u];

/*
 * The peak of out[] in [a, b).
 */
static float sp13_peak(const float *x, uint32_t a, uint32_t b)
{
    float m = 0.0f;

    for (; a < b; a++)
        m = __builtin_fmaxf(m, fabsf(x[a]));
    return m;
}

/*
 * SP13 (S100 P2): the loop band-pass.
 * (a) no DC latch: a constant 0.25 input at fbk 127 for 6 s: the output's
 *     mean (last second) is the input's own DC played once, 0.25 +-0.01
 *     (before S100 P2 the loop latched to about 0.65 against the clip);
 * (b) wid 0 resonates at the ton centre: a noise burst at fbk 110, ton 0
 *     (row 63: 458 Hz, the room's centre) rings; zero crossings over the
 *     last second give 458 Hz +-5 %;
 * (c) wid 127 is nearly flat: 200 Hz and 4 kHz bursts at fbk 80 keep the
 *     repeat-to-repeat ratio (repeats 2 and 3) at 0.8 +-1 dB.
 */
static void law_sp13(void)
{
    const uint32_t nb = (44100u * 6u) / 32u;
    const float d = crumpBit_delaySamples(20u, 120u);
    /* S100 P2: `ton` 0 (row 63) centres the band in 10 Hz .. 21 kHz. */
    const float fc = 458.257569f;
    double mean = 0.0;
    uint32_t zc = 0u;
    uint32_t i;
    uint32_t c;
    char msg[200];

    sp13_run(127u, 63u, 127u, sp13_dc, nb, sp13_buf);
    for (i = nb * 32u - 44108u; i < nb * 32u; i++)
        mean += sp13_buf[i];
    mean /= 44108.0;
    snprintf(msg, sizeof msg, "SP13a no DC latch: output mean %.4f (0.25 +-0.01)", mean);
    expect(fabs(mean - 0.25) < 0.01, msg);

    sp13_run(110u, 63u, 0u, sp13_burst, nb, sp13_buf);
    for (i = nb * 32u - 44108u; i < nb * 32u; i++)
        zc += (sp13_buf[i - 1u] < 0.0f) != (sp13_buf[i] < 0.0f);
    snprintf(msg, sizeof msg,
             "SP13b wid 0 rings at %.0f Hz, want %.0f +-5 %% (peak %.3f)",
             0.5f * (float)zc, fc, sp13_peak(sp13_buf, nb * 32u - 44108u, nb * 32u));
    expect(fabsf(0.5f * (float)zc - fc) < 0.05f * fc &&
               sp13_peak(sp13_buf, nb * 32u - 44108u, nb * 32u) > 0.05f, msg);

    for (c = 0u; c < 2u; c++) {
        float p2;
        float p3;
        float db;

        sp13_f = c ? 4000.0f : 200.0f;
        sp13_run(80u, 63u, 127u, sp13_tone, (uint32_t)(4.0f * d) / 32u + 8u,
                 sp13_buf);
        p2 = sp13_peak(sp13_buf, (uint32_t)(1.9f * d), (uint32_t)(2.9f * d));
        p3 = sp13_peak(sp13_buf, (uint32_t)(2.9f * d), (uint32_t)(3.9f * d));
        db = 20.0f * log10f(p3 / p2 / 0.8f);
        snprintf(msg, sizeof msg,
                 "SP13c wid 127, %.0f Hz: repeat ratio %.3f, %+.2f dB from fbk 0.8",
                 sp13_f, p3 / p2, db);
        expect(p2 > 0.05f && fabsf(db) < 1.0f, msg);
    }
}

/*
 * SP14 (S100 P2): `deg` adds no noise and keeps the level.
 * (a) deg 127, fbk 90, wid 127, from init with zero input: exact 0.0 for
 *     4 s; after a 0.3 s noise burst, exact 0.0 again within 8 s (fbk 40:
 *     below the row where the makeup's quiet-signal gain, 1.79 at deg 127,
 *     takes the loop to unity);
 * (b) a -12 dBFS 1 kHz burst, fbk 100: the second repeat's RMS at deg 127
 *     is within 1.5 dB of deg 0's (equal amplitude).
 */
static float sp14_rep2(uint8_t deg)
{
    uint8_t rows[SP_DLY_ROWS];
    const float d = crumpBit_delaySamples(64u, 120u);
    float l[32];
    float r[32];
    double pk = 0.0;
    uint32_t b;
    uint32_t n;

    memcpy(rows, sp_dlyRows, sizeof(rows));
    rows[SP_COL(SPACE_DLY_FEEDBACK)] = 100u;
    rows[SP_COL(SPACE_DLY_DEGRADE)] = deg;
    cb_bpm = 120u;
    sp_enter(&space_delayOps, NULL, rows, SP_DLY_ROWS);
    for (b = 0u; b < 9000u; b++) {
        memset(l, 0, sizeof(l));
        memset(r, 0, sizeof(r));
        sp_block(&space_delayOps, l, r);
    }
    for (b = 0u; b < (uint32_t)(2.9f * d) / 32u; b++) {
        for (n = 0u; n < 32u; n++) {
            const uint32_t i = b * 32u + n;

            l[n] = r[n] = (i < 4000u)
                ? 0.25f * sinf(6.2831853f * 1000.0f * (float)i / 44108.07f)
                : 0.0f;
        }
        sp_block(&space_delayOps, l, r);
        for (n = 0u; n < 32u; n++)
            if ((float)(b * 32u + n) > 1.9f * d)
                pk += (double)l[n] * l[n];
    }
    return (float)sqrt(pk);
}

static void law_sp14(void)
{
    uint8_t rows[SP_DLY_ROWS];
    float l[32];
    float r[32];
    uint32_t b;
    uint32_t n;
    uint32_t nz = 0u;
    uint32_t last = 0u;
    float p0;
    float p1;
    char msg[160];

    memcpy(rows, sp_dlyRows, sizeof(rows));
    rows[SP_COL(SPACE_DLY_FEEDBACK)] = 90u;
    rows[SP_COL(SPACE_DLY_DEGRADE)] = 127u;
    sp_enter(&space_delayOps, NULL, rows, SP_DLY_ROWS);
    for (b = 0u; b < 5513u; b++) {
        memset(l, 0, sizeof(l));
        memset(r, 0, sizeof(r));
        sp_block(&space_delayOps, l, r);
        for (n = 0u; n < 32u; n++)
            nz += (l[n] != 0.0f) || (r[n] != 0.0f);
    }
    snprintf(msg, sizeof msg, "SP14a deg 127 from init, no input: %u non-zero samples in 4 s", nz);
    expect(nz == 0u, msg);
    rows[SP_COL(SPACE_DLY_FEEDBACK)] = 40u;
    sp_enter(&space_delayOps, NULL, rows, SP_DLY_ROWS);
    for (b = 0u; b < 11025u; b++) {
        for (n = 0u; n < 32u; n++)
            l[n] = r[n] = (b < 413u) ? 0.3f * sp_noise() : 0.0f;
        sp_block(&space_delayOps, l, r);
        for (n = 0u; n < 32u; n++)
            if (l[n] != 0.0f || r[n] != 0.0f)
                last = b;
    }
    snprintf(msg, sizeof msg, "SP14a deg 127 fbk 40 after a burst: last non-zero at %.2f s (< 7.5)",
             (double)last * 32.0 / 44108.0);
    expect(last < 11025u - 690u, msg);
    p0 = sp14_rep2(0u);
    p1 = sp14_rep2(127u);
    snprintf(msg, sizeof msg, "SP14b deg 127 second repeat RMS %.3f, deg 0 %.3f (%+.2f dB, +-1.5)",
             p1, p0, 20.0f * log10f(p1 / p0));
    expect(p0 > 1.0f && fabsf(20.0f * log10f(p1 / p0)) <= 1.5f, msg);
}

/*
 * SP15 (S100 P2): `wow` depth as pitch. A 1 kHz tone (fbk 0, tim 64): the
 * largest deviation of the wet's zero-crossing rate over 2 s (after 2 s
 * of settling) at wow 127 is 1.2 .. 3.5 % (the law: 2 % wow + 0.3 %
 * flutter), at wow 0 below 0.1 %.
 */
static float sp15_run(uint8_t wow)
{
    uint8_t rows[SP_DLY_ROWS];
    float l[32];
    float r[32];
    float prev = 0.0f;
    float last = -1.0f;
    float lo = 1e9f;
    float hi = 0.0f;
    uint32_t b;
    uint32_t n;
    uint32_t i = 0u;
    uint32_t k = 0u;
    float iv[8] = { 0 };
    float acc = 0.0f;

    memcpy(rows, sp_dlyRows, sizeof(rows));
    rows[SP_COL(SPACE_DLY_WOW)] = wow;
    cb_bpm = 120u;
    sp_enter(&space_delayOps, NULL, rows, SP_DLY_ROWS);
    for (b = 0u; b < 5513u; b++) {
        for (n = 0u; n < 32u; n++, i++)
            l[n] = r[n] = 0.5f * sinf(6.2831853f * 1000.0f * (float)i / 44108.07f);
        sp_block(&space_delayOps, l, r);
        for (n = 0u; n < 32u; n++) {
            const float x = l[n];

            if (b > 2757u && prev < 0.0f && x >= 0.0f) {
                /* The crossing by linear interpolation. */
                const float at = (float)(b * 32u + n) - x / (x - prev);

                if (last >= 0.0f) {
                    /* An 8-period mean, so the reading is smooth. */
                    acc += (at - last) - iv[k & 7u];
                    iv[k & 7u] = at - last;
                    if (++k >= 8u) {
                        const float f = 8.0f * 44108.07f / acc;

                        lo = __builtin_fminf(lo, f);
                        hi = __builtin_fmaxf(hi, f);
                    }
                }
                last = at;
            }
            prev = x;
        }
    }
    return 100.0f * 0.5f * (hi - lo) / 1000.0f;
}

static void law_sp15(void)
{
    const float d0 = sp15_run(0u);
    const float d1 = sp15_run(127u);
    char msg[160];

    snprintf(msg, sizeof msg, "SP15 wow pitch: 127 +-%.2f %% (1.2 .. 3.5), 0 +-%.3f %% (< 0.1)", d1, d0);
    expect(d1 >= 1.2f && d1 <= 3.5f && d0 < 0.1f, msg);
}

/*
 * SP16 (S100 P2): `str`. (a) str 0: L and R are the same, sample for
 * sample, for a stereo input; (b) str 127, spr 126 (L at D/2, R at D): an
 * impulse on both returns on L at D/2 and at 2D/3 (tap A, L's lane a third
 * of the way to R's time), on R at D and at 5D/6 (tap B), each tap at least
 * 0.2 of its head; and the taps are only in the field: L's tap at 2D/3 is
 * louder on L than on R.
 */
static void law_sp16(void)
{
    uint8_t rows[SP_DLY_ROWS];
    const float t = crumpBit_delaySamples(64u, 120u);
    const uint32_t at = 3200u;
    float l[32];
    float r[32];
    float hl = 0.0f;
    float ml = 0.0f;
    float hr = 0.0f;
    float mr = 0.0f;
    float xr = 0.0f;
    uint32_t b;
    uint32_t n;
    int same = 1;
    char msg[200];

    memcpy(rows, sp_dlyRows, sizeof(rows));
    rows[SP_COL(SPACE_DLY_STEREO)] = 0u;
    rows[SP_COL(SPACE_DLY_SPREAD)] = 100u;
    rows[SP_COL(SPACE_DLY_FEEDBACK)] = 60u;
    sp_enter(&space_delayOps, NULL, rows, SP_DLY_ROWS);
    for (b = 0u; b < 3000u; b++) {
        for (n = 0u; n < 32u; n++) {
            l[n] = 0.3f * sp_noise();
            r[n] = 0.3f * sinf((float)(b * 32u + n) * 0.03f);
        }
        sp_block(&space_delayOps, l, r);
        for (n = 0u; n < 32u; n++)
            same = same && l[n] == r[n];
    }
    expect(same, "SP16a str 0: L and R the same, sample for sample");

    memcpy(rows, sp_dlyRows, sizeof(rows));
    rows[SP_COL(SPACE_DLY_STEREO)] = 127u;
    rows[SP_COL(SPACE_DLY_SPREAD)] = 126u;
    cb_bpm = 120u;
    sp_enter(&space_delayOps, NULL, rows, SP_DLY_ROWS);
    for (b = 0u; b < 9000u; b++) {
        memset(l, 0, sizeof(l));
        memset(r, 0, sizeof(r));
        sp_block(&space_delayOps, l, r);
    }
    for (b = 0u; b < (uint32_t)(1.2f * t + (float)at) / 32u; b++) {
        for (n = 0u; n < 32u; n++)
            l[n] = r[n] = (b * 32u + n == at) ? 1.0f : 0.0f;
        sp_block(&space_delayOps, l, r);
        for (n = 0u; n < 32u; n++) {
            const float f = (float)(b * 32u + n) - (float)at;

            if (fabsf(f - 0.5f * t) < 4.0f) hl = __builtin_fmaxf(hl, fabsf(l[n]));
            if (fabsf(f - (2.0f / 3.0f) * t) < 4.0f) {
                ml = __builtin_fmaxf(ml, fabsf(l[n]));
                xr = __builtin_fmaxf(xr, fabsf(r[n]));
            }
            if (fabsf(f - t) < 4.0f) hr = __builtin_fmaxf(hr, fabsf(r[n]));
            if (fabsf(f - (5.0f / 6.0f) * t) < 4.0f) mr = __builtin_fmaxf(mr, fabsf(r[n]));
        }
    }
    snprintf(msg, sizeof msg,
             "SP16b str 127 spr 126: L head %.3f tap %.3f (on R %.3f), R head %.3f tap %.3f",
             hl, ml, xr, hr, mr);
    expect(hl > 0.2f && hr > 0.2f && ml > 0.2f * hl && mr > 0.2f * hr &&
               ml > xr, msg);
}

/*
 * SP17 (S100 P2): `spr` darkens the loop that runs ahead of `tim`. fbk 100,
 * xfb 0, wid 127, a noise burst: the high share (first-difference energy /
 * energy) of L's repeats against R's from 1.4 D on, (a) at spr 126 (L at
 * D/2) below 0.6 (0.86 without the darkening: the shorter loop makes more
 * passes), (b) at spr 63 (no spread, no darkening) 0.9 .. 1.1. The scaling
 * by `wid` / 127 is the law itself (space_tapeBegin()); a narrower band
 * changes both loops' high share, so it is not measured here.
 */
static float sp17_run(uint8_t spr, uint8_t wid)
{
    uint8_t rows[SP_DLY_ROWS];
    const float t = crumpBit_delaySamples(64u, 120u);
    float l[32];
    float r[32];
    float pl = 0.0f;
    float pr = 0.0f;
    double el = 0.0;
    double dl = 0.0;
    double er = 0.0;
    double dr = 0.0;
    uint32_t b;
    uint32_t n;

    memcpy(rows, sp_dlyRows, sizeof(rows));
    rows[SP_COL(SPACE_DLY_SPREAD)] = spr;
    rows[SP_COL(SPACE_DLY_FEEDBACK)] = 100u;
    rows[SP_COL(SPACE_DLY_WIDTH)] = wid;
    cb_bpm = 120u;
    sp_enter(&space_delayOps, NULL, rows, SP_DLY_ROWS);
    for (b = 0u; b < 9000u; b++) {
        memset(l, 0, sizeof(l));
        memset(r, 0, sizeof(r));
        sp_block(&space_delayOps, l, r);
    }
    for (b = 0u; b < (uint32_t)(3.4f * t) / 32u; b++) {
        for (n = 0u; n < 32u; n++)
            l[n] = r[n] = (b * 32u + n < 400u) ? 0.3f * sp_noise() : 0.0f;
        sp_block(&space_delayOps, l, r);
        for (n = 0u; n < 32u; n++) {
            const float f = (float)(b * 32u + n);

            if (f > 1.4f * t) {
                el += (double)l[n] * l[n];
                dl += (double)(l[n] - pl) * (l[n] - pl);
            }
            if (f > 1.4f * t) {
                er += (double)r[n] * r[n];
                dr += (double)(r[n] - pr) * (r[n] - pr);
            }
            pl = l[n];
            pr = r[n];
        }
    }
    return (float)((dl / el) / (dr / er));
}

static void law_sp17(void)
{
    const float sp = sp17_run(126u, 127u);
    const float s0 = sp17_run(63u, 127u);
    char msg[160];

    snprintf(msg, sizeof msg,
             "SP17 spr darkening: L/R high share spr 126 %.3f (< 0.6), spr 63 %.3f (0.9 .. 1.1)",
             sp, s0);
    expect(sp < 0.6f && s0 >= 0.9f && s0 <= 1.1f, msg);
}

/*
 * SP12d (S100 P2): the `xfb` mix keeps the loop's energy. fbk 100, spr 30
 * (R 0.71 x L's time), a quiet stereo noise burst (independent L and R, so
 * the side is as large as the mid): the L+R energy of second 4 against
 * second 1 of the tail at xfb 32 and 64 is within 1 dB of the same at
 * xfb 0 (two independent loops). A side scaled by the coefficient, or an
 * L/R average, loses the side every pass.
 */
static double sp12d_run(uint8_t xfb)
{
    uint8_t rows[SP_DLY_ROWS];
    float l[32];
    float r[32];
    double e1 = 0.0;
    double e4 = 0.0;
    uint32_t b;
    uint32_t n;

    memcpy(rows, sp_dlyRows, sizeof(rows));
    rows[SP_COL(SPACE_DLY_TIME)] = 40u;
    rows[SP_COL(SPACE_DLY_FEEDBACK)] = 100u;
    rows[SP_COL(SPACE_DLY_SPREAD)] = 30u;
    rows[SP_COL(SPACE_DLY_XFEED)] = xfb;
    cb_bpm = 120u;
    sp_enter(&space_delayOps, NULL, rows, SP_DLY_ROWS);
    for (b = 0u; b < 9000u; b++) {
        memset(l, 0, sizeof(l));
        memset(r, 0, sizeof(r));
        sp_block(&space_delayOps, l, r);
    }
    for (b = 0u; b < 6000u; b++) {
        for (n = 0u; n < 32u; n++) {
            l[n] = (b < 100u) ? 0.05f * sp_noise() : 0.0f;
            r[n] = (b < 100u) ? 0.05f * sp_noise() : 0.0f;
        }
        sp_block(&space_delayOps, l, r);
        for (n = 0u; n < 32u; n++) {
            const double v = (double)l[n] * l[n] + (double)r[n] * r[n];

            if (b >= 200u && b < 1578u)
                e1 += v;
            else if (b >= 4334u && b < 5712u)
                e4 += v;
        }
    }
    return 10.0 * log10(e4 / e1);
}

static void law_sp12d(void)
{
    const double d0 = sp12d_run(0u);
    const double d32 = sp12d_run(32u);
    const double d64 = sp12d_run(64u);
    char msg[200];

    snprintf(msg, sizeof msg,
             "SP12d energy kept: second 4 on second 1 at xfb 0 %+.2f, 32 %+.2f, 64 %+.2f dB (32, 64 within 1 of 0)",
             d0, d32, d64);
    expect(fabs(d32 - d0) <= 1.0 && fabs(d64 - d0) <= 1.0, msg);
}

/*
 * SP18 (S100 P2, after the first build clicked under `hld` and `rev`): a
 * 220 Hz sine at 0.3 into the Delay (tim 64, fbk 60), `hld` on at 2 s and,
 * where listed, off again at 5 s; the largest third difference of L from
 * 1 s to 8 s stays below 0.01 (the sine itself gives about 0.002; the
 * first build's splice gave 0.2 .. 0.5 on every pass). Cases: hold; hold
 * with wow 64; hold then release; rev + hold; rev + hold with wow 64; rev
 * alone.
 */
static float sp18_run(uint8_t rev, uint8_t wow, int on_at, int off_at)
{
    uint8_t rows[SP_DLY_ROWS];
    float l[32];
    float r[32];
    float hi[3] = { 0.0f, 0.0f, 0.0f };
    float mx = 0.0f;
    uint32_t b;
    uint32_t n;
    uint32_t i = 0u;

    memcpy(rows, sp_dlyRows, sizeof(rows));
    rows[SP_COL(SPACE_DLY_FEEDBACK)] = 60u;
    rows[SP_COL(SPACE_DLY_REVERSE)] = rev;
    rows[SP_COL(SPACE_DLY_WOW)] = wow;
    cb_bpm = 120u;
    sp_enter(&space_delayOps, NULL, rows, SP_DLY_ROWS);
    for (b = 0u; b < 8u * 1378u; b++) {
        if ((int)b == on_at)
            space_delayOps.write_param(&sp_rt, SPACE_DLY_HOLD, 1u);
        if ((int)b == off_at)
            space_delayOps.write_param(&sp_rt, SPACE_DLY_HOLD, 0u);
        for (n = 0u; n < 32u; n++, i++)
            l[n] = r[n] = 0.3f * sinf(6.2831853f * 220.0f * (float)i / 44108.07f);
        sp_block(&space_delayOps, l, r);
        for (n = 0u; n < 32u; n++) {
            const float d3 = l[n] - 3.0f * hi[0] + 3.0f * hi[1] - hi[2];

            if (b > 1378u)
                mx = __builtin_fmaxf(mx, fabsf(d3));
            hi[2] = hi[1];
            hi[1] = hi[0];
            hi[0] = l[n];
        }
    }
    return mx;
}

static void law_sp18(void)
{
    static const struct { uint8_t rev, wow; int on, off; const char *what; } c[6] = {
        { 0u, 0u, 2756, -1, "hold" },
        { 0u, 64u, 2756, -1, "hold, wow 64" },
        { 0u, 0u, 2756, 6890, "hold, then release" },
        { 1u, 0u, 2756, -1, "rev + hold" },
        { 1u, 64u, 2756, -1, "rev + hold, wow 64" },
        { 1u, 0u, -1, -1, "rev" },
    };
    char msg[160];
    uint32_t k;

    for (k = 0u; k < 6u; k++) {
        const float mx = sp18_run(c[k].rev, c[k].wow, c[k].on, c[k].off);

        snprintf(msg, sizeof msg, "SP18 %s: largest third difference %.4f (< 0.01)",
                 c[k].what, mx);
        expect(mx < 0.01f, msg);
    }
}

/*
 * SP19 helper (S100 P2): the Delay at tim 64 (7,890 frames), fbk 80, the
 * given `ton` and `wid`, a 6,000-frame sine burst of f at 0.25 at the start;
 * returns the RMS of repeat 3 over repeat 2 in dB relative to fbk's 0.8.
 */
static float sp19_ratio(uint8_t ton, uint8_t wid, float f)
{
    uint8_t rows[SP_DLY_ROWS];
    const float d = crumpBit_delaySamples(64u, 120u);
    float l[32];
    float r[32];
    double e2 = 0.0;
    double e3 = 0.0;
    uint32_t b;
    uint32_t n;

    memcpy(rows, sp_dlyRows, sizeof(rows));
    rows[SP_COL(SPACE_DLY_FEEDBACK)] = 80u;
    rows[SP_COL(SPACE_DLY_TONE)] = ton;
    rows[SP_COL(SPACE_DLY_WIDTH)] = wid;
    cb_bpm = 120u;
    sp_enter(&space_delayOps, NULL, rows, SP_DLY_ROWS);
    for (b = 0u; b < 9000u; b++) {
        memset(l, 0, sizeof(l));
        memset(r, 0, sizeof(r));
        sp_block(&space_delayOps, l, r);
    }
    for (b = 0u; b < (uint32_t)(3.95f * d) / 32u; b++) {
        for (n = 0u; n < 32u; n++) {
            const uint32_t i = b * 32u + n;

            l[n] = r[n] = (i < 6000u)
                ? 0.25f * sinf(6.2831853f * f * (float)i / 44108.07f) : 0.0f;
        }
        sp_block(&space_delayOps, l, r);
        for (n = 0u; n < 32u; n++) {
            const float t = (float)(b * 32u + n);

            if (t > 2.0f * d && t < 2.0f * d + 6000.0f)
                e2 += (double)l[n] * l[n];
            else if (t > 3.0f * d && t < 3.0f * d + 6000.0f)
                e3 += (double)l[n] * l[n];
        }
    }
    return (float)(10.0 * log10(e3 / e2)) - 20.0f * log10f(0.8f);
}

/*
 * SP19 (S100 P2, user 2026-11-21): `ton` bipolar and `wid`'s top.
 * (a) `ton` 0 (row 63), `wid` 127 cuts nothing: a 40 Hz repeat keeps fbk's
 *     ratio within 1 dB;
 * (b) `ton` +63 cuts the lows (100 Hz at least 6 dB below `ton` 0's) and
 *     keeps 8 kHz (within 1 dB of `ton` 0's); `ton` -63 cuts the highs
 *     (8 kHz at least 6 dB below) and keeps 100 Hz (within 1 dB);
 * (c) `wid` 111 (16 rows below the top) still passes 40 Hz and 8 kHz
 *     within 1.5 dB of `wid` 127 (the earlier law had narrowed to 47 Hz ..
 *     4.5 kHz there).
 * The comparisons are against `ton` 0 / `wid` 127 at the same frequency,
 * so the tape LP and the linear read's own losses cancel.
 */
static void law_sp19(void)
{
    const float r40 = sp19_ratio(63u, 127u, 40.0f);
    const float r100 = sp19_ratio(63u, 127u, 100.0f);
    const float r8k = sp19_ratio(63u, 127u, 8000.0f);
    const float up100 = sp19_ratio(127u, 127u, 100.0f) - r100;
    const float up8k = sp19_ratio(127u, 127u, 8000.0f) - r8k;
    const float dn100 = sp19_ratio(0u, 127u, 100.0f) - r100;
    const float dn8k = sp19_ratio(0u, 127u, 8000.0f) - r8k;
    const float w40 = sp19_ratio(63u, 111u, 40.0f) - r40;
    const float w8k = sp19_ratio(63u, 111u, 8000.0f) - r8k;
    char msg[200];

    snprintf(msg, sizeof msg, "SP19a ton 0 wid 127: 40 Hz %+.2f dB from fbk (+-1)", r40);
    expect(fabsf(r40) <= 1.0f, msg);
    snprintf(msg, sizeof msg,
             "SP19b ton +63: 100 Hz %+.2f dB (<= -6), 8 kHz %+.2f dB (+-1); "
             "ton -63: 8 kHz %+.2f dB (<= -6), 100 Hz %+.2f dB (+-1)",
             up100, up8k, dn8k, dn100);
    expect(up100 <= -6.0f && fabsf(up8k) <= 1.0f && dn8k <= -6.0f &&
               fabsf(dn100) <= 1.0f, msg);
    snprintf(msg, sizeof msg, "SP19c wid 111: 40 Hz %+.2f dB, 8 kHz %+.2f dB on wid 127 (+-1.5)",
             w40, w8k);
    expect(fabsf(w40) <= 1.5f && fabsf(w8k) <= 1.5f, msg);
}

/*
 * SP20 (S100 P2, user 2026-11-21: "dck doesn't seem to do much"): a steady
 * 0.25 sine on both inputs (a -12 dBFS send), tim 20, fbk 0; the output's
 * RMS over the second half of 2 s at `dck` 127 is at least 30 dB below
 * `dck` 0's (fully ducked: the law is now twice as deep, and the follower
 * reads 0.5), and at `dck` 64 between 3 and 12 dB below.
 */
static double sp20_rms(uint8_t dck)
{
    uint8_t rows[SP_DLY_ROWS];
    float l[32];
    float r[32];
    double e = 0.0;
    uint32_t b;
    uint32_t n;
    uint32_t i = 0u;

    memcpy(rows, sp_dlyRows, sizeof(rows));
    rows[SP_COL(SPACE_DLY_TIME)] = 20u;
    rows[SP_COL(SPACE_DLY_DUCK)] = dck;
    cb_bpm = 120u;
    sp_enter(&space_delayOps, NULL, rows, SP_DLY_ROWS);
    for (b = 0u; b < 2756u; b++) {
        for (n = 0u; n < 32u; n++, i++)
            l[n] = r[n] = 0.25f * sinf(6.2831853f * 440.0f * (float)i / 44108.07f);
        sp_block(&space_delayOps, l, r);
        if (b >= 1378u)
            for (n = 0u; n < 32u; n++)
                e += (double)l[n] * l[n];
    }
    return sqrt(e / (1378.0 * 32.0)) + 1e-12;
}

static void law_sp20(void)
{
    const double r0 = sp20_rms(0u);
    const double d64 = 20.0 * log10(sp20_rms(64u) / r0);
    const double d127 = 20.0 * log10(sp20_rms(127u) / r0);
    char msg[160];

    snprintf(msg, sizeof msg, "SP20 dck: 64 %+.1f dB (-3 .. -12), 127 %+.1f dB (<= -30)",
             d64, d127);
    expect(r0 > 0.05 && d64 <= -3.0 && d64 >= -12.0 && d127 <= -30.0, msg);
}

int main(void)
{
    law_sp1();
    law_sp2();
    law_sp3();
    law_sp4();
    law_sp5();
    law_sp7();
    law_sp8();
    law_sp9();
    law_sp10();
    law_sp11();
    law_sp12();
    law_sp12b();
    law_sp12c();
    law_sp12d();
    law_sp13();
    law_sp14();
    law_sp15();
    law_sp16();
    law_sp17();
    law_sp18();
    law_sp19();
    law_sp20();
    printf("space failures: %d\n", failures);
    return failures ? 1 : 0;
}
