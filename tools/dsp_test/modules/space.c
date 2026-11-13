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
 *       every `hed` stop, `rev` and `hld` on and off, `spr` and `tim`
 *       moving:
 *       finite and below 4.0 every sample;
 *   SP3 Reverb: an impulse's tail is present (energy 0.2-0.7 s > 0) and
 *       decays (0.7-1.2 s below 0.2-0.7 s at dcy 64); 10 s of frz 2 with
 *       shm 127 after noise stays finite and below 8.0 (R-7, shortened);
 *   SP4 Echo: each routing gives a finite, bounded, non-silent output for
 *       an impulse, and the three differ;
 *   SP6-SP10 (§S18): `hed` steps 0..6 in knob order (S095 P4); the longest `tim` returns
 *       on the long channel (spr relative); `hld` does not build; Reverb
 *       `ton` darkens below 127; the stopped-transport idle ramps, idles and
 *       wakes;
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

/* tim spr fbk xfb ton deg wow hld rev hed dck (S095 P4: no `mix`) */
#define SP_DLY_ROWS 11u
static const uint8_t sp_dlyRows[SP_DLY_ROWS] = { 64u, 63u, 0u, 0u, 63u,
                                                 0u, 0u, 0u, 0u,
                                                 SPACE_HEADS_DEFAULT, 0u };

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
        rows[1] = pass ? 126u : 63u;
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
                 rows[1], pl - at, pass ? 0.5f * t : t, ml);
        expect(fabsf((float)(pl - at) - (pass ? 0.5f * t : t)) <= 2.0f &&
                   ml > 0.3f, msg);
        snprintf(msg, sizeof msg, "SP1 spr %u: R at %u want %.0f (peak %.3f)",
                 rows[1], pr - at, t, mr);
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
    rows[0] = 40u;
    rows[2] = 127u;
    rows[3] = 64u;
    rows[5] = 127u;
    rows[6] = 127u;
    rows[7] = 1u;   /* hld on part of the time (written below too) */
    sp_enter(&space_delayOps, NULL, rows, SP_DLY_ROWS);
    for (b = 0u; b < 5513u && ok; b++) {
        if ((b & 511u) == 0u) {
            space_delayOps.write_param(&sp_rt, SPACE_DLY_HEADS,
                                       (uint8_t)((b >> 9) % SPACE_HEADS_STOPS));
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

/* tim fbk ton deg wow e>r bln dcy lsh hsh hed dck rtg */
#define SP_ECH_ROWS 13u
static const uint8_t sp_echRows[SP_ECH_ROWS] = { 64u, 64u, 63u, 0u, 0u,
                                                 64u, 48u, 48u, 63u, 40u,
                                                 SPACE_HEADS_DEFAULT, 0u, 0u };

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
        rows[12] = (uint8_t)route;   /* rtg stops 0, 1, 2 */
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
        rows[0] = lim;
        rows[1] = pass ? 127u : 0u;
        sp_enter(&space_delayOps, NULL, rows, SP_DLY_ROWS);
        /* Start at the target: no glide from the floor. */
        sp_rt.d[0] = sp_rt.d[1] = (float)(sp_rt.tape_n - 3u);
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
                 "want %.0f +-1%% (peak %.3f)", lim, rows[1], pk - at, t, mk);
        expect(mk > 0.1f && fabsf((float)(pk - at) - t) < 0.01f * t, msg);
    }
}

/* SP8 (§S18): `hld` sustains without building (4 s, fbk 127, deg 0). */
static void law_sp8(void)
{
    uint8_t rows[SP_DLY_ROWS];
    float l[32];
    float r[32];
    double e1 = 0.0;
    double e4 = 0.0;
    uint32_t b;
    uint32_t n;
    char msg[160];

    memcpy(rows, sp_dlyRows, sizeof(rows));
    rows[0] = 40u;
    rows[2] = 127u;
    sp_enter(&space_delayOps, NULL, rows, SP_DLY_ROWS);
    for (b = 0u; b < 7000u; b++) {
        if (b == 1500u)
            space_delayOps.write_param(&sp_rt, SPACE_DLY_HOLD, 1u);
        for (n = 0u; n < 32u; n++)
            l[n] = r[n] = (b < 1400u) ? 0.2f * sp_noise() : 0.0f;
        sp_block(&space_delayOps, l, r);
        for (n = 0u; n < 32u; n++) {
            if (b >= 1500u && b < 2878u)
                e1 += (double)l[n] * l[n];
            else if (b >= 5622u)
                e4 += (double)l[n] * l[n];
        }
    }
    snprintf(msg, sizeof msg,
             "SP8 hld: energy second 1 %.3g, second 4 %.3g (no growth)", e1, e4);
    expect(e1 > 1e-3 && e4 <= 1.05 * e1, msg);
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

int main(void)
{
    law_sp1();
    law_sp2();
    law_sp3();
    law_sp4();
    law_sp5();
    law_sp6();
    law_sp7();
    law_sp8();
    law_sp9();
    law_sp10();
    printf("space failures: %d\n", failures);
    return failures ? 1 : 0;
}
