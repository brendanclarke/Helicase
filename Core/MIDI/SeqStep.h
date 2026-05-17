/*
 * SeqStep.h — port of original LXR Preset/SeqStep.h.
 *
 * This struct lives in two places by design: original mainboard
 * sequencer.h has Step (full pattern data), and the AVR side uses
 * StepData as the buffer for one step received over UART. On F765
 * single-chip there's no UART round-trip, but pattern files on disk
 * still use this layout — keep verbatim for file compatibility.
 *
 * Only change vs. original: avr/io.h → stdint.h.
 */

/*
 *  Modified on: 17.05.2026
 * ------------------------------------------------------------------------------------------------------------------------
 *  Modifications Copyright 2026 Brendan Clarke
 *  brendanpaulclarke@gmail.com
 *  https://www.brendanclarke.com
 * ------------------------------------------------------------------------------------------------------------------------
 *  The modifications to this file are part of the LXR02 Open-Source software.
 *  The same license and restrictions on use for the LXR software apply.
 * ------------------------------------------------------------------------------------------------------------------------
 */

#ifndef SEQSTEP_H_
#define SEQSTEP_H_

#include <stdint.h>
// TODO DSP_PORT
// 'StepStruct' confilcts with adding sequencer.h, renamed, we might not need this now.
typedef struct PortStepStruct {
    uint8_t volume;     /* 0-127 volume → 0x7f lower 7 bits, upper bit = active */
    uint8_t prob;       /* step probability */
    uint8_t note;       /* MIDI note value */

    /* parameter automation */
    uint8_t param1Nr;
    uint8_t param1Val;
    uint8_t param2Nr;
    uint8_t param2Val;
} StepData;

#endif
