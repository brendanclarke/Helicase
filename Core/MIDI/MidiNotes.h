/*
 * MidiNotes.h
 *
 *  Created on: 21.10.2026
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

#ifndef MIDINOTES_H_
#define MIDINOTES_H_

/*
 * MidiNotes - what a received note does (S083, plan 3.4, N6, C8).
 *
 * What: resolves a note-on/off through the active Scene's note map, then
 * either triggers the track once at the note's velocity and pitch, or,
 * when the BAR overlay is on (set by BAR `<`/`>` in PERF or by CC2 on G),
 * performs that overlay's action through the per-track overlay executor.
 * Reports each single trigger to LiveRecord with the note's arrival age.
 * CC2 sets the PERF BAR overlay itself (buttonHandler_setOverlay()); this
 * module keeps no separate CC2 state (S083 follow-up).
 * Why: N1-N6; Q5 (concurrent rolls per track); Q6 (arrival time).
 * Context: foreground (midiIn_dispatch()).
 * RAM: none (the former 1 B CC2 state is retired). Held notes and the
 * overlay state live in buttonHandler.c.
 * Affiliates: MidiNoteMap, buttonHandler overlay executor, sequencer
 * (seq_triggerVoice(), seq_isTrackMuted()), LiveRecord, MidiIn.
 */

#include <stdint.h>
#include "MidiDefs.h"

/*
 * Act on one note-on/off (RXF N already checked by the caller).
 * Output: 1 = consumed (it resolved to a track, even a muted one),
 * 0 = unmapped (the caller forwards it). Accessor: midiIn_dispatch().
 */
uint8_t midiNotes_handle(const MidiMsg *msg);
/*
 * S104 P1: release every note held on the Omni channel (the latched track's
 * note-offs through the overlay and the note modes), then clear the latch.
 * Accessor: midiIn_setOmniChannel() (before O changes).
 */
void midiNotes_omniReleaseAll(void);
/*
 * CC2 on G only (C8): 0 single trigger (overlay off), 1 BAR `<` overlay,
 * 2 BAR `>` overlay. Sets the PERF BAR overlay exactly like a BAR press:
 * the BAR LEDs follow in PERF, panel VOICE triggers and received notes use
 * it, and held overlays re-configure. The value persists until the next
 * CC2 or BAR press (last one wins); off at boot. Output: 1 consumed (0..2),
 * 0 other values (ignored and forwarded). Accessor: midiControl_handleCc()
 * (G-only check there).
 */
uint8_t midiNotes_setNoteAction(uint8_t value);

#endif /* MIDINOTES_H_ */
