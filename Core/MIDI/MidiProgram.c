/*
 * MidiProgram.c - received program change and Bank change (S083).
 */

/*
 *  Modified on: 21.10.2026
 * ------------------------------------------------------------------------------------------------------------------------
 *  Modifications Copyright 2026 Brendan Clarke
 *  brendanpaulclarke@gmail.com
 *  https://www.brendanclarke.com
 * ------------------------------------------------------------------------------------------------------------------------
 *  The modifications to this file are part of the LXR02 Open-Source software.
 *  The same license and restrictions on use for the LXR software apply.
 * ------------------------------------------------------------------------------------------------------------------------
 */

#include "MidiProgram.h"
#include "MidiDefs.h"
#include "MidiIn.h"
#include "MidiNoteMap.h"
#include "menu.h"
#include "sequencer.h"
#include "BankData.h"
#include "SceneData.h"
#include "presetManager.h"

/* Contract in MidiProgram.h. */
uint8_t midiProgram_programChange(uint8_t channel, uint8_t program)
{
	uint8_t global = midiIn_globalChannel();
	midiNoteMap_settings_t map;
	uint8_t tracks;

	if (program > 15u || !bank_sceneAvailable(program))
		return 0u;
	/*
	 * S104 P1, Omni: a program change on O assigns only the selected track
	 * to the Scene, as a PC on that track's own channel would.
	 */
	{
		const uint8_t omni = midiIn_omniTrack(channel);

		if (omni != 0xFFu) {
			seq_assignTracksToScene((uint8_t)(1u << omni), program);
			return 1u;
		}
	}
	/* G wins when a channel is both G and a voice channel (3.7). */
	if (global != 0u && channel == global) {
		menu_perfModeSceneChangeFrom(program, MIDI_ORIGIN_MIDI);
		return 1u;
	}
	midiNoteMap_load(scene_getActiveIndex(), &map);
	tracks = midiNoteMap_tracksOnChannel(&map, channel);
	if (tracks == 0u)
		return 0u;
	seq_assignTracksToScene(tracks, program);
	return 1u;
}

/* Contract in MidiProgram.h. */
uint8_t midiProgram_bankChange(uint8_t channel, uint8_t value)
{
	uint8_t global = midiIn_globalChannel();

	/* S104 P1: a Bank change belongs to G only; on O it is not consumed. */
	if (midiIn_omniTrack(channel) != 0xFFu)
		return 0u;
	if (global == 0u || channel != global || value > 127u)
		return 0u;
	/*
	 * S082 request API: MIDI origin is always Replace with all 16 Scenes
	 * (masks ignored, Q15); held as the one pending request until Preset,
	 * the facade and the Menu are idle (D33); it then switches the UI to
	 * Load/Save -> Bank (D7, which also cancels a waiting Learn, F-12).
	 */
	return preset_requestBackgroundBankLoad(value, 0u, 0u,
	                                        PRESET_BANK_ORIGIN_MIDI);
}
