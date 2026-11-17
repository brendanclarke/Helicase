/*
 * MidiNoteModes.c - the special MIDI note modes (S096 P3, plan §4).
 * Contract: MidiNoteModes.h.
 */
#include "MidiNoteModes.h"
#include "config.h"
#ifdef MIDI_NOTEMODE_HOST_TEST
#include "notemode_host.h"
#else
#include "MidiIn.h"
#include "MidiPitch.h"
#include "SceneData.h"
#include "buttonHandler.h"
#include "sequencer.h"
#include "LiveRecord.h"
#include "InstrumentManager.h"
#include "ColloidParameters.h"   /* S096 P3 C3: COL_IDX_* */
#endif
#include "MidiNoteMap.h"     /* MIDI_NOTEMAP_ROOT_INTERNAL (header-only) */
#include <string.h>

/*
 * The note-mode pools (S096 P3, plan §4.2; SRAM approved 2026-11-17).
 *
 * What: midiNoteMode_group[6]: mode (MNM_PARA / MNM_POLY / 0 free), channel
 * (1..16, for revalidation), members (slot mask), count (keys held, 0..128)
 * and key[128] in press order (note = internal note, vel, owner: PARA 0 the
 * fundamental, 1..3 in1..in3, 0xFF none; POLY the voice slot, 0xFF
 * stolen). midiNoteMode_voice[6]: group (or 0xFF), flags (midiHold mirror,
 * reassert pending), note0 (the trigger note F0), fund (the current
 * fundamental F), age (POLY steal order), ivAge[3] (the order in which
 * in1..in3 were last assigned: round robin).
 * Why: every key of every group in full detail (user Q17): "all keys
 * released" is exact; the polyphonic mode adds code, not RAM.
 * Rules: a note-on of a key already held updates its velocity (no second
 * entry); a release removes the key and compacts the list (order kept); a
 * note-on or note-off searches at most 128 entries.
 * RAM: 6 x 388 + 6 x 8 + 4 = 2,380 B SRAM1 static, asserted.
 * Lifetime: runtime; cleared at boot (midiNoteMode_init()) and per group
 * when it no longer resolves (midiNoteMode_service()).
 */
typedef struct {            /* one held key, 3 B */
    uint8_t note;           /* internal note 0..127 */
    uint8_t vel;            /* its note-on velocity (poly: re-voicing a stolen key) */
    uint8_t owner;          /* PARA: 0 fundamental, 1..3 in1..in3, 0xFF none;
                             * POLY: the voice slot 0..5, 0xFF stolen */
} midiNoteMode_key_t;

typedef struct {            /* one group, 388 B */
    uint8_t mode;           /* MNM_PARA / MNM_POLY / 0 free */
    uint8_t channel;        /* 1..16, for revalidation */
    uint8_t members;        /* slot mask */
    uint8_t count;          /* keys held, 0..128, in press order */
    midiNoteMode_key_t key[MIDI_NOTEMODE_KEYS];
} midiNoteMode_group_t;

typedef struct {            /* one voice slot, 8 B */
    uint8_t group;          /* 0..5, or 0xFF */
    uint8_t flags;          /* midiHold mirror, reassert pending */
    uint8_t note0;          /* PARA: the trigger note F0; POLY: the note playing */
    uint8_t fund;           /* PARA: the current fundamental F */
    uint8_t age;            /* POLY: steal order */
    uint8_t ivAge[3];       /* PARA: assignment order of in1..in3 (round robin) */
} midiNoteMode_voice_t;

static midiNoteMode_group_t midiNoteMode_group[MIDI_NOTEMODE_GROUPS];
static midiNoteMode_voice_t midiNoteMode_voice[MIDI_NOTEMODE_GROUPS];
static uint8_t midiNoteMode_age;
/* S096 P3 C3: the playback epoch the pools last saw, so a Scene change
 * re-asserts sus / coa / intervals once (plan §4.4). A Scene change affects
 * every group, so one byte is enough (inside the pool's rounding allowance). */
static uint8_t midiNoteMode_epoch;

_Static_assert(sizeof(midiNoteMode_key_t) == 3u, "a held key is three bytes");
_Static_assert(sizeof(midiNoteMode_group_t) == 388u, "a group is 388 bytes");
_Static_assert(sizeof(midiNoteMode_voice_t) == 8u, "a voice is eight bytes");
_Static_assert(sizeof(midiNoteMode_group) + sizeof(midiNoteMode_voice) +
                   sizeof(midiNoteMode_age) <= 2384u,
               "the note-mode pools exceed the approved 2,380 B");

/* The slot a visible track plays (track 6 -> slot 5). */
static uint8_t midiNoteMode_slotOf(uint8_t track)
{
    return (uint8_t)((track >= 6u) ? 5u : track);
}

/* The active Scene's slot type, or a value outside every mode. */
static uint8_t midiNoteMode_slotType(uint8_t scene_index, uint8_t slot)
{
    const kit_instrument_slot_t *instr = scene_instrumentSlotConst(scene_index, slot);

    return instr ? (uint8_t)instr->type : 0xFFu;
}

void midiNoteMode_init(void)
{
    uint8_t i;

    memset(midiNoteMode_group, 0, sizeof(midiNoteMode_group));
    memset(midiNoteMode_voice, 0, sizeof(midiNoteMode_voice));
    for (i = 0u; i < MIDI_NOTEMODE_GROUPS; i++)
        midiNoteMode_voice[i].group = 0xFFu;
    midiNoteMode_age = 0u;
    midiNoteMode_epoch = 0u;
}

midiNoteMode_t midiNoteMode_trackMode(uint8_t scene_index, uint8_t track,
                                      uint8_t channel)
{
    const uint8_t type = midiNoteMode_slotType(scene_index, midiNoteMode_slotOf(track));

    if (type == (uint8_t)INSTRUMENT_TYPE_TKT)
        return MNM_LAYER4;
    /* S096 P3 C3: a Colloid on its own (non-G) channel, with the BAR overlay
     * off, is paraphonic. A Colloid on G keeps the single trigger. */
    if (type == (uint8_t)INSTRUMENT_TYPE_COL &&
        channel != midiIn_globalChannel() &&
        buttonHandler_overlayActive() == 0u)
        return MNM_PARA;
    return MNM_NORMAL;
}

uint8_t midiNoteMode_wideTracks(uint8_t scene_index)
{
    uint8_t mask = 0u;
    uint8_t track;

    for (track = 0u; track < 7u; track++) {
        if (midiNoteMode_slotType(scene_index, midiNoteMode_slotOf(track)) ==
            (uint8_t)INSTRUMENT_TYPE_TKT)
            mask |= (uint8_t)(1u << track);
    }
    return mask;
}

uint8_t midiNoteMode_layerMask(uint8_t track, uint8_t sub)
{
    if (track >= 7u || sub > 3u)
        return 0u;
    if (midiNoteMode_slotType(scene_getActiveIndex(), midiNoteMode_slotOf(track)) !=
        (uint8_t)INSTRUMENT_TYPE_TKT)
        return 0u;
    return (uint8_t)(1u << sub);
}

static uint8_t midiNoteMode_clampByte(int16_t v)
{
    return (uint8_t)((v < 0) ? 0 : ((v > 127) ? 127 : v));
}

/* The effective channel of a track in a Scene (0 = none, as the note map). */
static uint8_t midiNoteMode_effChannel(uint8_t scene_index, uint8_t track)
{
    uint8_t ch = scene_getTrackMidiChannel(scene_index, track);

    if (ch == MIDI_NOTEMAP_CHANNEL_OFF)
        return 0u;
    if (ch > 16u)
        ch = 0u;
    return ch ? ch : ((midiIn_globalChannel() <= 16u) ? midiIn_globalChannel() : 0u);
}

static uint8_t midiNoteMode_groupOfSlot(uint8_t slot)
{
    uint8_t gi;

    for (gi = 0u; gi < MIDI_NOTEMODE_GROUPS; gi++)
        if (midiNoteMode_group[gi].mode != 0u &&
            (midiNoteMode_group[gi].members & (uint8_t)(1u << slot)) != 0u)
            return gi;
    return 0xFFu;
}

static uint8_t midiNoteMode_newGroup(uint8_t slot, uint8_t channel)
{
    uint8_t gi;

    for (gi = 0u; gi < MIDI_NOTEMODE_GROUPS; gi++) {
        midiNoteMode_group_t *g = &midiNoteMode_group[gi];

        if (g->mode != 0u)
            continue;
        g->mode = MNM_PARA;
        g->channel = channel;
        g->members = (uint8_t)(1u << slot);
        g->count = 0u;
        midiNoteMode_voice[slot].group = gi;
        midiNoteMode_epoch = seq_getPlaybackEpoch();
        return gi;
    }
    return 0xFFu;
}

static void midiNoteMode_freeGroup(uint8_t gi)
{
    uint8_t slot;

    if (gi >= MIDI_NOTEMODE_GROUPS)
        return;
    for (slot = 0u; slot < MIDI_NOTEMODE_GROUPS; slot++)
        if (midiNoteMode_voice[slot].group == gi)
            midiNoteMode_voice[slot].group = 0xFFu;
    midiNoteMode_group[gi].mode = 0u;
    midiNoteMode_group[gi].count = 0u;
}

static int16_t midiNoteMode_findKey(const midiNoteMode_group_t *g, uint8_t note)
{
    uint8_t i;

    for (i = 0u; i < g->count; i++)
        if (g->key[i].note == note)
            return (int16_t)i;
    return -1;
}

static void midiNoteMode_removeKey(midiNoteMode_group_t *g, uint8_t at)
{
    uint8_t i;

    for (i = at; (uint8_t)(i + 1u) < g->count; i++)
        g->key[i] = g->key[i + 1u];
    g->count--;
}

static void midiNoteMode_queue(uint8_t track, uint16_t target, uint8_t value)
{
    (void)seq_queueForegroundAutomation(track, target, value);
}

/* The patch's stored byte for a descriptor index (63 when unreadable). */
static uint8_t midiNoteMode_patchByte(uint8_t slot, uint8_t index)
{
    const kit_instrument_slot_t *instr =
        scene_instrumentSlotConst(scene_getActiveIndex(), slot);

    return instr ? (uint8_t)instr->parameter_images.instrument_parameters[index]
                 : 63u;
}

static void midiNoteMode_queueSus(uint8_t track, uint8_t slot, uint8_t on)
{
    midiNoteMode_queue(track, instrumentParam_make(slot, COL_IDX_SUSTAIN), on);
    instrumentManager_setMidiHold(slot, on);
}

/* Re-queue sus = 1, coa and every owned interval for a held group (Q6/Q4). */
static void midiNoteMode_queueState(uint8_t track, uint8_t slot, uint8_t gi)
{
    const midiNoteMode_group_t *g = &midiNoteMode_group[gi];
    const uint8_t coa = midiNoteMode_clampByte(
        (int16_t)midiNoteMode_patchByte(slot, COL_IDX_COARSE) +
        (int16_t)midiNoteMode_voice[slot].fund -
        (int16_t)midiNoteMode_voice[slot].note0);
    uint8_t i;

    midiNoteMode_queueSus(track, slot, 1u);
    midiNoteMode_queue(track, instrumentParam_make(slot, COL_IDX_COARSE), coa);
    for (i = 0u; i < g->count; i++) {
        if (g->key[i].owner >= 1u && g->key[i].owner <= 3u)
            midiNoteMode_queue(track,
                instrumentParam_make(slot, (uint8_t)(COL_IDX_INTERVAL_1 +
                                                     g->key[i].owner - 1u)),
                midiNoteMode_clampByte(63 + (int16_t)g->key[i].note -
                                       (int16_t)midiNoteMode_voice[slot].fund));
    }
}

static void midiNoteMode_report(uint8_t track, uint8_t note, uint8_t vel,
                                uint8_t kind, uint16_t ageUs,
                                const uint16_t *targets,
                                const uint8_t *values, uint8_t n)
{
    liveRec_trigger_t lr;
    uint8_t i;

    memset(&lr, 0, sizeof(lr));
    lr.track = track;
    lr.note = note;
    lr.velocity = vel;
    lr.kind = kind;
    lr.source = LIVEREC_SRC_MIDI;
    lr.note_is_default = (uint8_t)(note == MIDI_NOTEMAP_ROOT_INTERNAL);
    lr.age_us = ageUs;
    for (i = 0u; i < n && i < 4u; i++) {
        lr.autoTarget[i] = targets[i];
        lr.autoValue[i] = values[i];
    }
    lr.autoCount = (uint8_t)((n > 4u) ? 4u : n);
    liveRec_triggerIn(&lr);
}

/*
 * A paraphonic note-on (S096 P3 C3, plan §4.3).
 *
 * What: the first key of a group (no keys held) fires one resetting trigger
 * at its velocity and pitch (the fundamental F = F0) and automates sus = on;
 * the oscillators play the patch's own intervals. Each later key takes the
 * first interval group (in1..in3) no held key owns, else the group assigned
 * longest ago (round robin by ivAge), and automates that interval to note - F
 * (PM63 byte 63 + note - F, clamped to 0..126 = -63..+63 semitones); no
 * trigger, so the envelopes continue. Every write is transient automation
 * (seq_queueForegroundAutomation(), decided Q5) and is reported to
 * LiveRecord: the first key as a hit with sus = on at its step, later keys
 * as automation-only events (LIVEREC_HIT_AUTO) at theirs.
 * Results: keys C3 (F), E3, G3, B3, D4 held in that order: in1 = +4, in2 =
 * +7, in3 = +11; D4 takes in1 (+14), E3 no longer sounds.
 * Inputs: track, channel, internal, vel, ageUs. Output: 1 (consumed).
 */
static uint8_t midiNoteMode_paraOn(uint8_t track, uint8_t channel,
                                   uint8_t internal, uint8_t vel, uint16_t ageUs)
{
    const uint8_t slot = midiNoteMode_slotOf(track);
    uint8_t gi = midiNoteMode_groupOfSlot(slot);
    midiNoteMode_group_t *g;
    int16_t ki;
    uint16_t targets[1];
    uint8_t values[1];

    if (gi == 0xFFu)
        gi = midiNoteMode_newGroup(slot, channel);
    if (gi == 0xFFu)
        return 1u;   /* no group (six slots already own one) */
    g = &midiNoteMode_group[gi];
    ki = midiNoteMode_findKey(g, internal);
    if (ki >= 0) {
        g->key[ki].vel = vel;   /* a repeated note-on updates its velocity */
        return 1u;
    }
    if (g->count >= MIDI_NOTEMODE_KEYS)
        return 1u;
    if (g->count == 0u) {
        midiNoteMode_voice_t *v = &midiNoteMode_voice[slot];

        g->key[0].note = internal;
        g->key[0].vel = vel;
        g->key[0].owner = 0u;
        g->count = 1u;
        v->group = gi;
        v->note0 = internal;
        v->fund = internal;
        v->ivAge[0] = v->ivAge[1] = v->ivAge[2] = 0u;
        midiNoteMode_epoch = seq_getPlaybackEpoch();
        seq_triggerVoice(track, vel, internal, 1u);
        midiNoteMode_queueSus(track, slot, 1u);
        targets[0] = instrumentParam_make(slot, COL_IDX_SUSTAIN);
        values[0] = 1u;
        midiNoteMode_report(track, internal, vel, LIVEREC_HIT_SINGLE, ageUs,
                            targets, values, 1u);
        return 1u;
    }
    {
        uint8_t used[4] = { 0u, 0u, 0u, 0u };
        uint8_t own = 0u;
        uint8_t i, o;
        uint8_t value;
        uint16_t target;

        for (i = 0u; i < g->count; i++)
            if (g->key[i].owner >= 1u && g->key[i].owner <= 3u)
                used[g->key[i].owner] = 1u;
        for (o = 1u; o <= 3u; o++)
            if (!used[o]) { own = o; break; }
        if (own == 0u) {
            own = 1u;
            for (o = 2u; o <= 3u; o++)
                if (midiNoteMode_voice[slot].ivAge[o - 1u] <
                    midiNoteMode_voice[slot].ivAge[own - 1u])
                    own = o;
            for (i = 0u; i < g->count; i++)
                if (g->key[i].owner == own) { g->key[i].owner = 0xFFu; break; }
        }
        midiNoteMode_voice[slot].ivAge[own - 1u] = ++midiNoteMode_age;
        g->key[g->count].note = internal;
        g->key[g->count].vel = vel;
        g->key[g->count].owner = own;
        g->count++;
        value = midiNoteMode_clampByte(63 + (int16_t)internal -
                                       (int16_t)midiNoteMode_voice[slot].fund);
        target = instrumentParam_make(slot,
                 (uint8_t)(COL_IDX_INTERVAL_1 + own - 1u));
        midiNoteMode_queue(track, target, value);
        targets[0] = target;
        values[0] = value;
        midiNoteMode_report(track, internal, vel, LIVEREC_HIT_AUTO, ageUs,
                            targets, values, 1u);
    }
    return 1u;
}

/*
 * A paraphonic note-off (S096 P3 C3, plan §4.3).
 *
 * What: removes the key (the list stays in press order).
 *  - No keys left: sus = off is automated and the hold cleared; the gate
 *    closes and the voice releases (C1).
 *  - The fundamental released: the earliest remaining key becomes F'; coa
 *    is automated to the stored coa + (F' - F0) (the step's note F0 cannot
 *    change without a trigger: n = coa + note - 63) and every owned interval
 *    to 63 + key - F', so every held pitch keeps sounding; F''s old interval
 *    group returns to the patch's value.
 *  - Another owner released: its group returns to the patch's value
 *    (decided Q9).
 * Results: C3 E3 G3 held, C3 released: F' = E3; coa + 4; in2 (G3) = +3; in1
 * (E3's old group) back to the patch.
 */
static uint8_t midiNoteMode_paraOff(uint8_t track, uint8_t internal)
{
    const uint8_t slot = midiNoteMode_slotOf(track);
    const uint8_t gi = midiNoteMode_groupOfSlot(slot);
    midiNoteMode_group_t *g;
    int16_t ki;
    uint8_t owner;

    if (gi == 0xFFu)
        return 1u;
    g = &midiNoteMode_group[gi];
    ki = midiNoteMode_findKey(g, internal);
    if (ki < 0)
        return 1u;
    owner = g->key[ki].owner;
    midiNoteMode_removeKey(g, (uint8_t)ki);
    if (g->count == 0u) {
        uint16_t targets[1];
        uint8_t values[1];

        midiNoteMode_queueSus(track, slot, 0u);
        targets[0] = instrumentParam_make(slot, COL_IDX_SUSTAIN);
        values[0] = 0u;
        midiNoteMode_report(track, internal, 0u, LIVEREC_HIT_AUTO, 0u,
                            targets, values, 1u);
        midiNoteMode_freeGroup(gi);
        return 1u;
    }
    if (owner == 0u) {
        uint16_t targets[4];
        uint8_t values[4];
        uint8_t n = 0u;
        uint8_t i;
        const uint8_t fPrime = g->key[0].note;
        const uint8_t oldOwner = g->key[0].owner;
        const uint8_t coa = midiNoteMode_clampByte(
            (int16_t)midiNoteMode_patchByte(slot, COL_IDX_COARSE) +
            (int16_t)fPrime - (int16_t)midiNoteMode_voice[slot].note0);

        midiNoteMode_queue(track, instrumentParam_make(slot, COL_IDX_COARSE), coa);
        targets[n] = instrumentParam_make(slot, COL_IDX_COARSE);
        values[n++] = coa;
        if (oldOwner >= 1u && oldOwner <= 3u) {
            const uint8_t patch = midiNoteMode_patchByte(slot,
                (uint8_t)(COL_IDX_INTERVAL_1 + oldOwner - 1u));

            midiNoteMode_queue(track,
                instrumentParam_make(slot,
                    (uint8_t)(COL_IDX_INTERVAL_1 + oldOwner - 1u)), patch);
            targets[n] = instrumentParam_make(slot,
                (uint8_t)(COL_IDX_INTERVAL_1 + oldOwner - 1u));
            values[n++] = patch;
        }
        g->key[0].owner = 0u;
        for (i = 0u; i < g->count; i++) {
            if (g->key[i].owner >= 1u && g->key[i].owner <= 3u) {
                const uint8_t value = midiNoteMode_clampByte(
                    63 + (int16_t)g->key[i].note - (int16_t)fPrime);
                const uint16_t target = instrumentParam_make(slot,
                    (uint8_t)(COL_IDX_INTERVAL_1 + g->key[i].owner - 1u));

                midiNoteMode_queue(track, target, value);
                if (n < 4u) {
                    targets[n] = target;
                    values[n++] = value;
                }
            }
        }
        midiNoteMode_voice[slot].fund = fPrime;
        midiNoteMode_report(track, internal, 0u, LIVEREC_HIT_AUTO, 0u,
                            targets, values, n);
    } else if (owner >= 1u && owner <= 3u) {
        const uint8_t patch = midiNoteMode_patchByte(slot,
            (uint8_t)(COL_IDX_INTERVAL_1 + owner - 1u));
        const uint16_t target = instrumentParam_make(slot,
            (uint8_t)(COL_IDX_INTERVAL_1 + owner - 1u));

        midiNoteMode_queue(track, target, patch);
        midiNoteMode_report(track, internal, 0u, LIVEREC_HIT_AUTO, 0u,
                            &target, &patch, 1u);
    }
    return 1u;
}

uint8_t midiNoteMode_noteOn(uint8_t track, uint8_t channel, uint8_t internal,
                            uint8_t sub, uint8_t midiNote, uint8_t vel,
                            uint16_t ageUs)
{
    liveRec_trigger_t lr;
    const midiNoteMode_t mode = midiNoteMode_trackMode(scene_getActiveIndex(),
                                                       track, channel);

    if (mode == MNM_PARA)
        return midiNoteMode_paraOn(track, channel, internal, vel, ageUs);
    memset(&lr, 0, sizeof(lr));
    lr.track = track;
    lr.note = internal;
    lr.velocity = vel;
    lr.kind = LIVEREC_HIT_SINGLE;
    lr.source = LIVEREC_SRC_MIDI;
    lr.note_is_default = (uint8_t)(internal == MIDI_NOTEMAP_ROOT_INTERNAL);
    lr.age_us = ageUs;
    if (mode == MNM_LAYER4) {
        /*
         * A layer note (S096 P3 T1, plan §6.2).
         *
         * What: mask = 1 << sub (A..D); one trigger of the track with that mask
         * (seq_triggerVoiceLayers(): no drop of the voice's queued triggers,
         * finding D1), at the note's velocity and internal pitch (63 + d / 4);
         * reported to LiveRecord with `layers` (the step's lon automation is
         * written OR-merged, §6.3).
         * Why: the brief: each of four notes triggers one sample independently;
         * two notes in one render pass both sound (each trigger carries its own
         * mask, so the drain's last-writer rule cannot merge them).
         * Inputs: track, internal, sub 0..3, vel, ageUs. Output: 1 (consumed).
         */
        const uint8_t mask = (uint8_t)(1u << sub);

        seq_triggerVoiceLayers(track, vel, internal, mask);
        lr.layers = mask;
        liveRec_triggerIn(&lr);
        (void)midiNote;
        return 1u;
    }
    /*
     * NORMAL (S096 P3: the S083 single trigger, moved here unchanged).
     * One resetting trigger at the note's velocity and resolved pitch,
     * reported to LiveRecord (note_is_default records no note special).
     */
    seq_triggerVoice(track, vel, internal, 1u);
    liveRec_triggerIn(&lr);
    (void)midiNote;
    return 1u;
}

uint8_t midiNoteMode_noteOff(uint8_t track, uint8_t channel,
                             uint8_t internal, uint8_t sub)
{
    (void)sub;
    if (track < 7u &&
        midiNoteMode_trackMode(scene_getActiveIndex(), track, channel) == MNM_PARA)
        return midiNoteMode_paraOff(track, internal);
    /* NORMAL and LAYER4: a note-off still ends an overlay roll (handled by
     * MidiNotes before this call); a single trigger ignores it. */
    return 1u;
}

void midiNoteMode_prepareTrigger(uint8_t voice)
{
    uint8_t slot, gi;

    if (voice >= 7u)
        return;
    slot = midiNoteMode_slotOf(voice);
    gi = midiNoteMode_groupOfSlot(slot);
    if (gi == 0xFFu || midiNoteMode_group[gi].mode != MNM_PARA ||
        midiNoteMode_group[gi].count == 0u)
        return;
    /* A slot with held paraphonic keys re-queues sus / coa / intervals before
     * this pass's drain, so a Pattern trigger does not undo the live hand
     * (decided Q6). */
    midiNoteMode_queueState(voice, slot, gi);
}

void midiNoteMode_service(void)
{
    const uint8_t scene = scene_getActiveIndex();
    const uint8_t epochNow = seq_getPlaybackEpoch();
    const uint8_t epochChanged = (uint8_t)(midiNoteMode_epoch != epochNow);
    uint8_t gi;

    for (gi = 0u; gi < MIDI_NOTEMODE_GROUPS; gi++) {
        midiNoteMode_group_t *g = &midiNoteMode_group[gi];
        uint8_t slot = 0xFFu;
        uint8_t track;
        uint8_t ok = 1u;
        uint8_t s;

        if (g->mode != MNM_PARA)
            continue;
        for (s = 0u; s < MIDI_NOTEMODE_GROUPS; s++)
            if ((g->members & (uint8_t)(1u << s)) != 0u) { slot = s; break; }
        if (slot == 0xFFu) {
            midiNoteMode_freeGroup(gi);
            continue;
        }
        track = (uint8_t)((slot >= 5u) ? 5u : slot);
        if (midiNoteMode_slotType(scene, slot) != (uint8_t)INSTRUMENT_TYPE_COL ||
            buttonHandler_overlayActive() != 0u ||
            g->channel == midiIn_globalChannel())
            ok = 0u;
        if (ok && slot >= 5u) {
            ok = (uint8_t)(midiNoteMode_effChannel(scene, 5u) == g->channel ||
                           midiNoteMode_effChannel(scene, 6u) == g->channel);
        } else if (ok) {
            ok = (uint8_t)(midiNoteMode_effChannel(scene, track) == g->channel);
        }
        if (!ok) {
            /*
             * S096 P3 assessment F4: sus = off is queued only while the slot
             * still holds a Colloid (the overlay came on, or the channel
             * moved). After a type change the Colloid's sus index names
             * another type's row (a Tetrakt's level C), so only the hold is
             * cleared and the group freed; the new instrument was reset by
             * its load and holds no paraphonic state.
             */
            if (midiNoteMode_slotType(scene, slot) == (uint8_t)INSTRUMENT_TYPE_COL)
                midiNoteMode_queue(track, instrumentParam_make(slot, COL_IDX_SUSTAIN), 0u);
            instrumentManager_setMidiHold(slot, 0u);
            midiNoteMode_freeGroup(gi);
            continue;
        }
        if (epochChanged) {
            /* A Scene change: keep the keys and re-assert the live values
             * (decided Q4). */
            midiNoteMode_queueState(track, slot, gi);
        }
    }
    midiNoteMode_epoch = epochNow;
}
