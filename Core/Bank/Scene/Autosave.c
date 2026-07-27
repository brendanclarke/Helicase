/*
 * Autosave.c -- retained-state binary codec and debounce ledger.
 *
 * Inputs are already-committed BankData/SceneData values. Outputs are one
 * bounded payload or one checked retained-state overlay; no function here
 * opens files, polls AsyncFATFS, or owns a duplicate workspace allocation.
 */
#include "Autosave.h"
#include "filesystem.h"
#include "BankData.h"
#include "SceneData.h"
#include "timebase.h"
#include "presetManager.h"
#include <string.h>

#define AUTOSAVE_IDLE_MS 5000u
#define AUTOSAVE_FORCE_MS 30000u
#define AUTOSAVE_NO_INDEX 0xffffu

static autosave_workspace_t *autosave_workspace(void)
{
    return filesystem_autosaveWorkspace();
}

static uint8_t autosave_bitGet(const autosave_workspace_t *w, uint16_t index)
{
    return (uint8_t)((w->dirty_bits[index >> 3] & (uint8_t)(1u << (index & 7u))) != 0u);
}

static void autosave_bitSet(autosave_workspace_t *w, uint16_t index, uint8_t value)
{
    uint8_t mask = (uint8_t)(1u << (index & 7u));
    if (value) w->dirty_bits[index >> 3] |= mask;
    else w->dirty_bits[index >> 3] &= (uint8_t)~mask;
}

uint8_t autosave_keyFromLogical(uint16_t logical_index, autosave_record_key_t *key)
{
    uint16_t relative;
    uint8_t within;
    if (!key || logical_index >= AUTOSAVE_RECORD_COUNT) return 0u;
    memset(key, 0, sizeof(*key));
    key->logical_index = logical_index;
    key->scene_index = 0xffu;
    key->instrument_slot = 0xffu;
    if (logical_index == 0u) { key->domain = AUTOSAVE_DOMAIN_BANKSET; return 1u; }
    relative = (uint16_t)(logical_index - 1u);
    key->scene_index = (uint8_t)(relative / AUTOSAVE_SCENE_RECORD_COUNT);
    within = (uint8_t)(relative % AUTOSAVE_SCENE_RECORD_COUNT);
    if (key->scene_index >= SCENE_COUNT) return 0u;
    if (within == 0u) key->domain = AUTOSAVE_DOMAIN_SCENESET;
    else if (within == 1u) key->domain = AUTOSAVE_DOMAIN_KITSET;
    else if (within <= 7u) { key->domain = AUTOSAVE_DOMAIN_INSTRUMENT; key->instrument_slot = (uint8_t)(within - 2u); }
    else key->domain = AUTOSAVE_DOMAIN_SCENE_COMMIT;
    return 1u;
}

void autosave_initWorkspace(autosave_workspace_t *w, uint16_t bank_slot)
{
    if (!w) return;
    memset(w, 0, sizeof(*w));
    w->in_flight_index = AUTOSAVE_NO_INDEX;
    w->active_bank_slot = bank_slot;
    w->initialized = 1u;
    w->next_generation = 1u;
    w->next_batch_id = 1u;
}

uint8_t autosave_mutationAllowed(void)
{
    /* This is an input/stage-ownership gate, not an autosave-availability
     * test.  Normal parameter editing must remain usable before the first
     * Bank is resident, after a failed Bank operation, and while a missing
     * blob is being created. During PRESET_AUTOSAVE_BARRIER the union is
     * still a ledger and normal edits may extend it; only UPDATE_READY and
     * LOAD_IN_PROGRESS can contain a typed loader image. */
    return (uint8_t)(preset_getStatus() == PRESET_IDLE ||
                     preset_getStatus() == PRESET_AUTOSAVE_BARRIER);
}

uint8_t autosave_barrierRequested(void)
{
    autosave_workspace_t *w = autosave_workspace();
    return (uint8_t)(w && w->barrier_requested);
}

void autosave_requestLoadBarrier(void)
{
    autosave_workspace_t *w = autosave_workspace();
    /* Before the first Bank activation the union contains no ledger. A load
     * is already safe in that case, so never interpret its uninitialized bytes. */
    if (w && w->initialized) w->barrier_requested = 1u;
}

void autosave_releaseLoadBarrier(void)
{
    autosave_workspace_t *w = autosave_workspace();
    if (w) w->barrier_requested = 0u;
}

static void autosave_markIndexUnchecked(autosave_workspace_t *w, uint16_t index)
{
    if (!w || index >= AUTOSAVE_RECORD_COUNT) return;
    if (!autosave_bitGet(w, index)) w->first_dirty_tick[index] = time_sysTick;
    w->last_dirty_tick[index] = time_sysTick;
    autosave_bitSet(w, index, 1u);
}

static void autosave_markIndex(uint16_t index)
{
    autosave_workspace_t *w = autosave_workspace();
    /* A mutation may be legal even where there is no autosave destination.
     * Keep that mutation in the live Bank data, but only touch the reused
     * stage-union ledger after Bank activation initialized it for this Bank. */
    if (!autosave_mutationAllowed() || !w || !w->initialized ||
        !bank_hasResidentBank()) return;
    autosave_markIndexUnchecked(w, index);
}

void autosave_markBankset(void) { autosave_markIndex(0u); }
void autosave_markSceneset(uint8_t scene) { if (scene < SCENE_COUNT) autosave_markIndex((uint16_t)(1u + scene * 9u)); }
void autosave_markKitset(uint8_t scene) { if (scene < SCENE_COUNT) autosave_markIndex((uint16_t)(2u + scene * 9u)); }
void autosave_markInstrument(uint8_t scene, uint8_t slot) { if (scene < SCENE_COUNT && slot < INSTRUMENT_SLOT_COUNT) autosave_markIndex((uint16_t)(3u + scene * 9u + slot)); }

void autosave_markSceneBatch(uint8_t scene, uint16_t domain_mask)
{
    autosave_workspace_t *w = autosave_workspace();
    uint8_t slot;
    /* Import commits call this after their stage copy and before Preset leaves
     * LOAD_IN_PROGRESS; they deliberately bypass the live-input gate. */
    if (!w || scene >= SCENE_COUNT || !w->initialized || !bank_hasResidentBank()) return;
    w->pending_scene_batch_id[scene] = ++w->next_batch_id;
    if (domain_mask & 1u) autosave_markIndexUnchecked(w, (uint16_t)(1u + scene * 9u));
    if (domain_mask & 2u) autosave_markIndexUnchecked(w, (uint16_t)(2u + scene * 9u));
    for (slot = 0u; slot < INSTRUMENT_SLOT_COUNT; slot++)
        if (domain_mask & (uint16_t)(1u << (slot + 2u)))
            autosave_markIndexUnchecked(w, (uint16_t)(3u + scene * 9u + slot));
    autosave_markIndexUnchecked(w, (uint16_t)(9u + scene * 9u));
}

void autosave_markWholeBankSnapshot(void)
{
    autosave_workspace_t *w = autosave_workspace();
    uint8_t scene;

    /* A typed load overwrites the shared union, including any old dirty-bit
     * ledger. Re-mark the complete resident parameter Bank after that load
     * commits. This is a background recovery snapshot: it costs no foreground
     * load latency and guarantees that pre-load changes in untouched Scenes
     * are not silently discarded merely because their bookkeeping was reused. */
    if (!w || !w->initialized || !bank_hasResidentBank()) return;
    autosave_markIndexUnchecked(w, 0u);
    for (scene = 0u; scene < SCENE_COUNT; scene++)
        autosave_markSceneBatch(scene, 0x00ffu);
}

uint8_t autosave_hasDirty(void)
{
    autosave_workspace_t *w = autosave_workspace();
    uint16_t i;
    if (!w || !w->initialized) return 0u;
    for (i = 0u; i < AUTOSAVE_RECORD_COUNT; i++) if (autosave_bitGet(w, i)) return 1u;
    return 0u;
}

uint8_t autosave_selectDue(uint16_t now, autosave_record_key_t *key)
{
    autosave_workspace_t *w = autosave_workspace();
    uint16_t i, chosen = AUTOSAVE_NO_INDEX;
    uint16_t best_age = 0u;
    if (!w || !key || w->in_flight_index != AUTOSAVE_NO_INDEX || !w->initialized ||
        (preset_getStatus() != PRESET_IDLE &&
         preset_getStatus() != PRESET_AUTOSAVE_BARRIER)) return 0u;
    for (i = 0u; i < AUTOSAVE_RECORD_COUNT; i++) {
        uint16_t age, idle;
        if (!autosave_bitGet(w, i)) continue;
        age = (uint16_t)(now - w->first_dirty_tick[i]);
        idle = (uint16_t)(now - w->last_dirty_tick[i]);
        /* Normal edits debounce. The short typed-load handoff is dispatched
         * before this scheduler once its current SD transaction completes. */
        if (!w->barrier_requested && age < AUTOSAVE_FORCE_MS && idle < AUTOSAVE_IDLE_MS) continue;
        if (chosen == AUTOSAVE_NO_INDEX || age > best_age) { chosen = i; best_age = age; }
    }
    if (chosen == AUTOSAVE_NO_INDEX || !autosave_keyFromLogical(chosen, key)) return 0u;
    w->current_key = *key;
    w->in_flight_index = chosen;
    return 1u;
}

void autosave_writeCompleted(uint8_t ok)
{
    autosave_workspace_t *w = autosave_workspace();
    uint16_t index;
    if (!w || w->in_flight_index == AUTOSAVE_NO_INDEX) return;
    index = w->in_flight_index;
    if (ok) {
        autosave_bitSet(w, index, 0u);
        /* Commit is written last; only then may later standalone edits leave
         * this Scene's batch identity. */
        if (w->current_key.domain == AUTOSAVE_DOMAIN_SCENE_COMMIT &&
            w->current_key.scene_index < SCENE_COUNT)
            w->pending_scene_batch_id[w->current_key.scene_index] = 0u;
    } else {
        /* Retry after a fresh debounce interval instead of retrying every pass. */
        w->last_dirty_tick[index] = time_sysTick;
    }
    w->in_flight_index = AUTOSAVE_NO_INDEX;
}

static uint8_t put8(uint8_t *d, uint16_t cap, uint16_t *p, uint8_t v) { if (*p >= cap) return 0u; d[(*p)++] = v; return 1u; }
static uint8_t put16(uint8_t *d, uint16_t cap, uint16_t *p, uint16_t v) { return (uint8_t)(put8(d,cap,p,(uint8_t)v) && put8(d,cap,p,(uint8_t)(v>>8))); }

uint16_t autosave_encodePayload(const autosave_record_key_t *key, uint8_t *d, uint16_t cap)
{
    const scene_t *scene;
    uint16_t p = 0u;
    uint8_t i;
    if (!key || !d) return 0u;
    if (key->domain == AUTOSAVE_DOMAIN_BANKSET) return put8(d,cap,&p,bank_activeSceneSlot()) && put16(d,cap,&p,bank_sceneMaskVoiceEdit()) ? p : 0u;
    scene = scene_getConst(key->scene_index);
    if (!scene) return 0u;
    if (key->domain == AUTOSAVE_DOMAIN_SCENESET) {
        if (!put8(d,cap,&p,scene->settings.morph_amount) || !put8(d,cap,&p,scene->settings.voice_decimation_all)) return 0u;
        for (i=0u;i<INSTRUMENT_SLOT_COUNT;i++) if (!put8(d,cap,&p,scene->settings.voice_morph_amount[i])) return 0u;
        for (i=0u;i<INSTRUMENT_SLOT_COUNT;i++) if (!put8(d,cap,&p,scene->settings.audio_out[i])) return 0u;
        for (i=0u;i<INSTRUMENT_SLOT_COUNT;i++) if (!put8(d,cap,&p,scene->settings.fx_send_amount[i])) return 0u;
        for (i=0u;i<INSTRUMENT_SLOT_COUNT;i++) if (!put8(d,cap,&p,scene->settings.fader_setting[i])) return 0u;
        for (i=0u;i<NUM_TRACKS;i++) if (!put8(d,cap,&p,scene->settings.midi_channel[i])) return 0u;
        for (i=0u;i<NUM_TRACKS;i++) if (!put8(d,cap,&p,scene->settings.midi_note[i])) return 0u;
    } else if (key->domain == AUTOSAVE_DOMAIN_KITSET) {
        if (!put8(d,cap,&p,scene->kit.settings.slot6_track7_amp_envelope_decay) || !put8(d,cap,&p,scene->kit.settings.slot6_track7_morph_amp_envelope_decay)) return 0u;
        for (i=0u;i<INSTRUMENT_SLOT_COUNT;i++) if (!put8(d,cap,&p,(uint8_t)scene->kit.instruments[i].type)) return 0u;
    } else if (key->domain == AUTOSAVE_DOMAIN_INSTRUMENT && key->instrument_slot < INSTRUMENT_SLOT_COUNT) {
        const kit_instrument_slot_t *slot = &scene->kit.instruments[key->instrument_slot];
        if (!put8(d,cap,&p,key->instrument_slot) || !put8(d,cap,&p,(uint8_t)slot->type)) return 0u;
        for (i=0u;i<INSTRUMENT_PARAM_COUNT;i++) if (!put8(d,cap,&p,slot->parameter_images.instrument_parameters[i])) return 0u;
        for (i=0u;i<INSTRUMENT_PARAM_COUNT;i++) if (!put8(d,cap,&p,slot->parameter_images.morph_instrument_parameters[i])) return 0u;
    } else if (key->domain == AUTOSAVE_DOMAIN_SCENE_COMMIT) {
        if (!put16(d,cap,&p,0x01ffu)) return 0u;
    } else return 0u;
    return p;
}

uint8_t autosave_applyPayload(const autosave_record_key_t *key, const uint8_t *s, uint16_t len)
{
    scene_t *scene;
    uint16_t p = 0u;
    uint8_t i;
    if (!key || !s) return 0u;
    if (key->domain == AUTOSAVE_DOMAIN_BANKSET) { if (len != 3u) return 0u; bank_selectActiveSceneForEditMask(s[0]); bank_setSceneMaskVoiceEdit((uint16_t)s[1] | ((uint16_t)s[2]<<8)); return 1u; }
    scene = scene_get(key->scene_index);
    if (!scene) return 0u;
    if (key->domain == AUTOSAVE_DOMAIN_SCENESET) {
        if (len != (uint16_t)(2u + 4u*INSTRUMENT_SLOT_COUNT + 2u*NUM_TRACKS)) return 0u;
        scene->settings.morph_amount=s[p++]; scene->settings.voice_decimation_all=s[p++];
        for(i=0u;i<INSTRUMENT_SLOT_COUNT;i++) scene->settings.voice_morph_amount[i]=s[p++];
        for(i=0u;i<INSTRUMENT_SLOT_COUNT;i++) scene->settings.audio_out[i]=s[p++];
        for(i=0u;i<INSTRUMENT_SLOT_COUNT;i++) scene->settings.fx_send_amount[i]=s[p++];
        for(i=0u;i<INSTRUMENT_SLOT_COUNT;i++) scene->settings.fader_setting[i]=s[p++];
        for(i=0u;i<NUM_TRACKS;i++) scene->settings.midi_channel[i]=s[p++];
        for(i=0u;i<NUM_TRACKS;i++) scene->settings.midi_note[i]=s[p++];
    } else if (key->domain == AUTOSAVE_DOMAIN_KITSET) {
        if (len != (uint16_t)(2u+INSTRUMENT_SLOT_COUNT)) return 0u;
        scene->kit.settings.slot6_track7_amp_envelope_decay=s[p++]; scene->kit.settings.slot6_track7_morph_amp_envelope_decay=s[p++];
        for(i=0u;i<INSTRUMENT_SLOT_COUNT;i++) { if (s[p] >= INSTRUMENT_TYPE_UNKNOWN) return 0u; scene->kit.instruments[i].type=(instrument_type_t)s[p++]; }
    } else if (key->domain == AUTOSAVE_DOMAIN_INSTRUMENT && key->instrument_slot < INSTRUMENT_SLOT_COUNT) {
        kit_instrument_slot_t *slot=&scene->kit.instruments[key->instrument_slot];
        if (len != (uint16_t)(2u+2u*INSTRUMENT_PARAM_COUNT) || s[p++] != key->instrument_slot || s[p++] != (uint8_t)slot->type) return 0u;
        for(i=0u;i<INSTRUMENT_PARAM_COUNT;i++) slot->parameter_images.instrument_parameters[i]=s[p++];
        for(i=0u;i<INSTRUMENT_PARAM_COUNT;i++) slot->parameter_images.morph_instrument_parameters[i]=s[p++];
    } else if (key->domain != AUTOSAVE_DOMAIN_SCENE_COMMIT) return 0u;
    return 1u;
}

uint32_t autosave_crc32(const uint8_t *data, uint16_t length)
{
    uint32_t crc=0xffffffffu; uint16_t i; uint8_t bit;
    for(i=0u;i<length;i++) { crc^=data[i]; for(bit=0u;bit<8u;bit++) crc=(crc>>1)^((crc&1u)?0xedb88320u:0u); }
    return crc^0xffffffffu;
}
