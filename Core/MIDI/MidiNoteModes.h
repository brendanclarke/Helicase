/*
 * MidiNoteModes - what a received note does to a voice (S096 P3, plan §4).
 *
 * What: owns every special MIDI note mode, per track, resolved per message
 * from the active Scene (never a cached Scene index, MEMORY.md MIDI):
 *   MNM_NORMAL  today's single trigger (moved here from midiNotes_handle());
 *   MNM_LAYER4  a Tetrakt track: 4 notes per pitch, each triggers one layer
 *               (A..D) with a layer mask (no state);
 *   MNM_PARA    a Colloid track on its own (non-G) channel with the BAR
 *               overlay off: paraphonic (the first key triggers and holds,
 *               later keys set in1..in3, the earliest held key is the
 *               fundamental, all keys up releases);
 *   MNM_POLY    reserved for the polyphonic mode (a later pNN group), which
 *               will take precedence over MNM_PARA.
 * State: two fixed pools sized for the polyphonic mode, every key in full
 * detail (approved): groups (a set of held keys, press order) and voices
 * (one per slot). Six instances of any mode fit; tracks 5 and 6 share slot
 * 5's voice.
 * Why: the brief (2026-11-17): a separate module for the note modes, sized
 * for six instances and a later multi-voice polyphonic mode.
 * Context: foreground only (MIDI receive in the main loop, the trigger
 * funnel's prepare, the main-loop service).
 * Affiliates: MidiNotes (caller), MidiNoteMap (the wide mask), sequencer
 * (triggers, foreground automation), InstrumentManager (midiHold),
 * LiveRecord (recording), buttonHandler (the overlay's layer query).
 */
#ifndef MIDINOTEMODES_H_
#define MIDINOTEMODES_H_

#include <stdint.h>

typedef enum {
    MNM_NORMAL = 0,
    MNM_LAYER4,
    MNM_PARA,
    MNM_POLY
} midiNoteMode_t;

/* Empty the pools (boot). */
void midiNoteMode_init(void);

/*
 * Resolve a track's note mode (S096 P3, plan §4.1).
 * Inputs: scene_index (the active Scene, read per message), track 0..6, the
 * note's channel 1..16. Output: the mode; MNM_POLY is never returned in
 * S096 P3. Context: foreground. Affiliates: midiNoteMap_effectiveChannel()
 * (the track's channel is the note's channel for a resolved note),
 * buttonHandler_overlayActive().
 */
midiNoteMode_t midiNoteMode_trackMode(uint8_t scene_index, uint8_t track,
                                      uint8_t channel);

/*
 * The wide (four-notes-per-pitch) track mask for one Scene.
 * Inputs: scene_index. Output: bit t set when track t's slot holds a Tetrakt.
 * Context: foreground and TIM3 (midiNoteMap_load() is called from
 * seq_triggerVoice()); byte reads of SceneData only, never the pools or the
 * overlay. Caller: midiNoteMap_load().
 */
uint8_t midiNoteMode_wideTracks(uint8_t scene_index);

/*
 * A layer note's mask (plan §6.2). Inputs: track 0..6, sub 0..3. Output:
 * 1 << sub on a Tetrakt track in the active Scene, else 0. Caller: the
 * overlay start (buttonHandler). Context: foreground.
 */
uint8_t midiNoteMode_layerMask(uint8_t track, uint8_t sub);

/*
 * Offer one resolved note-on / note-off. Inputs: track, channel, internal
 * note, sub (the place within a wide track's group), the MIDI note and
 * velocity, and the arrival age in us. Output: 1 when consumed (the caller
 * does not forward it). Accessors: midiNotes_handle(). Context: foreground.
 */
uint8_t midiNoteMode_noteOn(uint8_t track, uint8_t channel, uint8_t internal,
                            uint8_t sub, uint8_t midiNote, uint8_t vel,
                            uint16_t ageUs);
uint8_t midiNoteMode_noteOff(uint8_t track, uint8_t channel,
                             uint8_t internal, uint8_t sub);

/*
 * A resetting trigger is about to restore a slot (P3f): a slot with held
 * paraphonic keys re-queues its sus / intervals / coa before the drain, so a
 * Pattern trigger does not undo the live hand (decided Q6). Caller:
 * voiceControl_prepareTrigger(). Context: foreground.
 */
void midiNoteMode_prepareTrigger(uint8_t voice);

/* Revalidate every active group against the active Scene (P3f). Caller:
 * main.c midi_service(), every main-loop pass. Context: foreground. */
void midiNoteMode_service(void);

#endif /* MIDINOTEMODES_H_ */
