/*
 * MidiPitch.h
 *
 *  Derived from MidiNoteNumbers.h, original LXR firmware by Julian Schmidt.
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

#ifndef MIDIPITCH_H_
#define MIDIPITCH_H_

/*
 * MidiPitch.h - pitch constants shared by MIDI, the Sequencer and the DSP
 * oscillators (S083, was MidiNoteNumbers.h).
 *
 * What: the default internal note (63 = the voice's default pitch), the
 * MIDI-note frequency table, and the fine-tune helper.
 * Why: MidiNoteNumbers.h is renamed with the Core/MIDI rebuild (Q7), and
 * midiParser_calcDetune() moves here because MidiParser is deleted and the
 * helper is pure pitch maths (Oscillator.c is its only user).
 * Affiliates: Oscillator.c (frequency, detune), sequencer.c, SceneData.c,
 * filesystem.c, buttonHandler.c, MidiNoteMap.c (63 base).
 */

#include <stdint.h>

/*
 * Default internal note (unchanged value).
 *
 * Inputs: Sequencer, overlay, preview and the note map use this when
 * no pitch offset applies. Output: 63; the oscillator plays its default
 * pitch, and note - 63 is the offset in semitones (Oscillator.c
 * osc_setBaseNote()). S083: a track's MIDI note (`nte`) is no longer a
 * pitch; it is only the root of the track's receive range (plan 3.3, C-1).
 */
#define MIDI_DEFAULT_TRIGGER_NOTE 63u

/* 2^(1/12): one semitone. */
#define MIDI_PITCH_SEMITONE_UP 1.0594630943592952645618252949463f

static const float MidiNoteFrequencies[] =
{
		8.1757989156,
		8.6619572180,
		9.1770239974,
		9.7227182413,
		10.3008611535,
		10.9133822323,
		11.5623257097,
		12.2498573744,
		12.9782717994,
		13.7500000000,
		14.5676175474,
		15.4338531643,
		16.3515978313,
		17.3239144361,
		18.3540479948,
		19.4454364826,
		20.6017223071,
		21.8267644646,
		23.1246514195,
		24.4997147489,
		25.9565435987,
		27.5000000000,
		29.1352350949,
		30.8677063285,
		32.7031956626,
		34.6478288721,
		36.7080959897,
		38.8908729653,
		41.2034446141,
		43.6535289291,
		46.2493028390,
		48.9994294977,
		51.9130871975,
		55.0000000000,
		58.2704701898,
		61.7354126570,
		65.4063913251,
		69.2956577442,
		73.4161919794,
		77.7817459305,
		82.4068892282,
		87.3070578583,
		92.4986056779,
		97.9988589954,
		103.8261743950,
		110.0000000000,
		116.5409403795,
		123.4708253140,
		130.8127826503,
		138.5913154884,
		146.8323839587,
		155.5634918610,
		164.8137784564,
		174.6141157165,
		184.9972113558,
		195.9977179909,
		207.6523487900,
		220.0000000000,
		233.0818807590,
		246.9416506281,
		261.6255653006,
		277.1826309769,
		293.6647679174,
		311.1269837221,
		329.6275569129,
		349.2282314330,
		369.9944227116,
		391.9954359817,
		415.3046975799,
		440.0000000000,
		466.1637615181,
		493.8833012561,
		523.2511306012,
		554.3652619537 ,
		587.3295358348,
		622.2539674442,
		659.2551138257,
		698.4564628660,
		739.9888454233,
		783.9908719635,
		830.6093951599,
		880.0000000000,
		932.3275230362,
		987.7666025122,
		1046.5022612024,
		1108.7305239075,
		1174.6590716696,
		1244.5079348883,
		1318.5102276515,
		1396.9129257320,
		1479.9776908465,
		1567.9817439270,
		1661.2187903198,
		1760.0000000000,
		1864.6550460724,
		1975.5332050245,
		2093.0045224048,
		2217.4610478150,
		2349.3181433393,
		2489.0158697766,
		2637.0204553030,
		2793.8258514640,
		2959.9553816931,
		3135.9634878540,
		3322.4375806396,
		3520.0000000000,
		3729.3100921447,
		3951.0664100490,
		4186.009044809,
		4434.922095630,
		4698.636286678,
		4978.031739553,
		5274.040910605,
		5587.651702928,
		5919.910763386,
		6271.926975708,
		6644.875161279,
		7040.000000000,
		7458.620234756,
		7902.132834658,
		8372.0180896192,
		8869.8441912599,
		9397.2725733570,
		9956.0634791066,
		10548.0818212118,
		11175.3034058561,
		11839.8215267723,
		12543.8539514160
};

/*
 * Fine-tune factor from a 7-bit value (was midiParser_calcDetune()).
 *
 * What: 0..127 maps linearly to about -1/2 .. +1/2 semitone around 1.0.
 * Inputs: value 0..127 (the oscillator's fine-tune byte). Output: frequency
 * multiplier. The old function had two identical branches; the result is
 * the same expression. Accessors: Oscillator.c osc_setBaseNote(),
 * osc_recalcFreq().
 */
static inline float midiPitch_detune(uint8_t value)
{
	const float frac = ((float)value / 127.f) - 0.5f;

	return 1.0f + frac * (MIDI_PITCH_SEMITONE_UP - 1.0f);
}

#endif /* MIDIPITCH_H_ */

