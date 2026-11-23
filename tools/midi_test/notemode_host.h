/* Host stand-ins for MidiNoteModes.c (S096 P3). */
#ifndef NOTEMODE_HOST_H_
#define NOTEMODE_HOST_H_

#include <stdint.h>
#include <string.h>

typedef uint16_t instrument_param_id_t;

typedef enum { INSTRUMENT_TYPE_UNKNOWN = 0, INSTRUMENT_TYPE_COL = 1,
               INSTRUMENT_TYPE_TKT = 6 } instrument_type_t;
typedef struct {
	uint8_t type;
	struct { uint8_t instrument_parameters[64]; } parameter_images;
} kit_instrument_slot_t;

/* The Colloid row indices the paraphonic mode writes (ColloidParameters.h). */
#define COL_IDX_COARSE      0u
#define COL_IDX_SUSTAIN     10u
#define COL_IDX_INTERVAL_1  12u

extern uint8_t host_slot_type[16][6];
extern uint8_t host_slot_param[16][6][64];
extern uint8_t host_track_channel[16][7];
extern uint8_t host_active_scene;
extern uint8_t host_global_channel;
extern uint8_t host_overlay;
extern uint8_t host_epoch;

static inline const kit_instrument_slot_t *scene_instrumentSlotConst(
        uint8_t scene, uint8_t slot)
{
	static kit_instrument_slot_t s;

	if (scene > 15u || slot > 5u)
		return 0;
	s.type = host_slot_type[scene][slot];
	memcpy(s.parameter_images.instrument_parameters, host_slot_param[scene][slot],
	       64u);
	return &s;
}
static inline uint8_t scene_getActiveIndex(void) { return host_active_scene; }
static inline uint8_t scene_getTrackMidiChannel(uint8_t scene, uint8_t track)
{
	return (scene <= 15u && track < 7u) ? host_track_channel[scene][track] : 17u;
}
static inline uint8_t seq_getPlaybackEpoch(void) { return host_epoch; }
static inline instrument_param_id_t instrumentParam_make(uint8_t slot,
                                                         uint8_t index)
{
	return (instrument_param_id_t)((uint16_t)slot * 64u + index);
}

/* LiveRecord's hit record (the fields the module writes). */
enum { LIVEREC_HIT_SINGLE = 0, LIVEREC_HIT_AUTO = 5 };
enum { LIVEREC_SRC_OVERLAY = 0, LIVEREC_SRC_MIDI = 1 };
typedef struct {
	uint8_t track, note, velocity, kind, roll, rhythm, source, note_is_default;
	uint16_t age_us;
	uint8_t layers, autoCount;
	uint16_t autoTarget[4];
	uint8_t autoValue[4];
} liveRec_trigger_t;

void seq_triggerVoice(uint8_t track, uint8_t vol, uint8_t note, uint8_t reset);
void seq_triggerVoiceLayers(uint8_t track, uint8_t vol, uint8_t note,
                            uint8_t layers);
void liveRec_triggerIn(const liveRec_trigger_t *t);
uint8_t seq_queueForegroundAutomation(uint8_t track, uint16_t target,
                                      uint8_t value);
void instrumentManager_setMidiHold(uint8_t slot, uint8_t on);
uint8_t midiIn_globalChannel(void);
uint8_t buttonHandler_overlayActive(void);

/* Recorded calls. */
void host_reset_calls(void);
unsigned host_normal_calls(void);
unsigned host_layer_calls(void);
uint8_t host_last_mask(void);
uint8_t host_last_track(void);
uint8_t host_last_internal(void);
uint8_t host_last_vel(void);
const liveRec_trigger_t *host_last_lr(void);

#endif /* NOTEMODE_HOST_H_ */
