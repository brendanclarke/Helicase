/*
 * Core/DSP/Effects/Space/SpaceParameters.h
 *
 *  Created on: 12.11.2026
 * ------------------------------------------------------------------------------------------------------------------------
 *  Copyright 2026 Brendan Clarke
 *  brendanpaulclarke@gmail.com
 *  https://www.brendanclarke.com
 * ------------------------------------------------------------------------------------------------------------------------
 *  This file is part of the LXR02 Open-Source software (licence: see SpaceKit.h).
 * ------------------------------------------------------------------------------------------------------------------------
 */

/*
 * Space Effect parameter rows (S095 P1 plan §4, D1, D23).
 *
 * What: the descriptor indices of the three Space types. Rows 0..2 are the
 * common rows (out, vol, pan); 3.. are each type's own, in the plan's row
 * order. The indices are type-relative (a Delay row 6 is not a Reverb row
 * 6), as every Effect's are.
 * Why: the registry lanes, the DSP's write_param and the page hooks name the
 * same rows.
 * Accessors: EffectsManager.c (registry rows), SpaceEffect.c,
 * SpaceParameters.c.
 */
#ifndef SPACE_PARAMETERS_H_
#define SPACE_PARAMETERS_H_

#include "EffectsManager.h"

/*
 * S095 P4 (T1): the Space types are 100 % wet. The Effect is a send bus, so
 * the voices' dry signal already reaches the outputs through the mixer; the
 * former `mix` row (index 3) added the bus input back and is gone. Every
 * row below moved down one index: stored Pattern automation, LFO targets
 * and AutoSave bytes on Space rows play one row off (no migration); `.fx`
 * and MIDI Learn store keys and are unaffected.
 */
typedef enum {
    SPACE_DLY_TIME = EFFECT_COMMON_PARAM_COUNT,  /* 3  CrumpBit's `dly` curve, share-truncated (D3) */
    SPACE_DLY_SPREAD,     /* 4  R/L time ratio 2^((spr-63)/63); the longer is `tim` */
    SPACE_DLY_FEEDBACK,   /* 5  0..1.20, unity at 100 (S095 P4) */
    SPACE_DLY_XFEED,      /* 6  L/R feedback cross-feed, continuous (D23) */
    SPACE_DLY_TONE,       /* 7  loop tone: LP < 63 = flat < HP */
    SPACE_DLY_DEGRADE,    /* 8  drive, head bandwidth, hiss (D5) */
    SPACE_DLY_WOW,        /* 9  wow + flutter + drift depth */
    SPACE_DLY_HOLD,       /* 10 on: input 0, loop gain 1 (D10) */
    SPACE_DLY_REVERSE,    /* 11 reverse playback head (O4) */
    SPACE_DLY_HEADS,      /* 12 seven head sets, stepped 0..6 (O6, S095 P4) */
    SPACE_DLY_DUCK,       /* 13 wet ducked by the input (O1) */
    SPACE_DELAY_PARAM_ENUM_COUNT
} space_delay_param_t;
#define SPACE_DELAY_PARAM_COUNT   14u

typedef enum {
    SPACE_RVB_PREDELAY = EFFECT_COMMON_PARAM_COUNT,  /* 3  0..1.4 s, share-truncated */
    SPACE_RVB_SIZE,       /* 4  0.35..1 x, glides (D12) */
    SPACE_RVB_DECAY,      /* 5  RT60 0.3..20 s */
    SPACE_RVB_DIFFUSION,  /* 6  input allpass 0.5..0.75 */
    SPACE_RVB_LOW_SHELF,  /* 7  low-band decay x0.5..x2 (D7) */
    SPACE_RVB_LOW_FREQ,   /* 8  100..600 Hz */
    SPACE_RVB_HIGH_SHELF, /* 9  high-band damping x1..x0.1 */
    SPACE_RVB_HIGH_FREQ,  /* 10 1.5..9 kHz */
    SPACE_RVB_MOD,        /* 11 tank modulation depth */
    SPACE_RVB_WIDTH,      /* 12 M/S width of the taps */
    SPACE_RVB_FREEZE,     /* 13 off / on / on with shimmer, stepped 0..2 (O12) */
    SPACE_RVB_SHIMMER,    /* 14 shimmer feed amount */
    SPACE_RVB_INTERVAL,   /* 15 -24..+24 st, rows 39..87 (63 = 0) */
    SPACE_RVB_DETUNE,     /* 16 0..50 cents, two voices +-dtn */
    SPACE_RVB_ENV,        /* 17 bloom < 63 = off < gate (O10) */
    SPACE_RVB_DUCK,       /* 18 (O11) */
    SPACE_RVB_TONE,       /* 19 output one-pole LP per side, 127 = flat */
    SPACE_REVERB_PARAM_ENUM_COUNT
} space_reverb_param_t;
#define SPACE_REVERB_PARAM_COUNT  20u

typedef enum {
    SPACE_ECH_TIME = EFFECT_COMMON_PARAM_COUNT,  /* 3  the Delay's law, mono tape */
    SPACE_ECH_FEEDBACK,   /* 4  as the Delay's */
    SPACE_ECH_TONE,       /* 5 */
    SPACE_ECH_DEGRADE,    /* 6 */
    SPACE_ECH_WOW,        /* 7 */
    SPACE_ECH_TO_REVERB,  /* 8  `e>r`: the amount between tape and plate */
    SPACE_ECH_BLEND,      /* 9  `bln`: echo .. plate in the wet */
    SPACE_ECH_DECAY,      /* 10 plate RT60 */
    SPACE_ECH_LOW_SHELF,  /* 11 */
    SPACE_ECH_HIGH_SHELF, /* 12 */
    SPACE_ECH_HEADS,      /* 13 (O15), stepped 0..6 */
    SPACE_ECH_DUCK,       /* 14 (O14) */
    SPACE_ECH_ROUTING,    /* 15 e>r / r>e / plate in the loop, stepped 0..2 (O16) */
    SPACE_ECHO_PARAM_ENUM_COUNT
} space_echo_param_t;
#define SPACE_ECHO_PARAM_COUNT    16u

/*
 * Stepped rows' values (the DSP and the labels read the same codes).
 *
 * S095 P4 (T2): `hed`, `frz` and `rtg` store the stop itself (0..6, 0..2),
 * one stop per knob detent, as CrumpBit's `sub` and `typ` do. S095 P1 §S18
 * had stored them 0..127 in equal zones (about 18 detents per stop); the
 * label then had no relation to the knob position. A value above the last
 * stop (an older stored byte, a MIDI CC, a Pattern value) plays the last
 * stop: the manager clamps to the descriptor maximum, and space_step()
 * clamps again for a direct write_param.
 */
#define SPACE_FREEZE_OFF          0u
#define SPACE_FREEZE_ON           1u
#define SPACE_FREEZE_SHIMMER      2u   /* O12 */
#define SPACE_FREEZE_STOPS        3u
#define SPACE_HEADS_STOPS         7u   /* hed: A B C AB AC BC ABC */
#define SPACE_HEADS_DEFAULT       2u   /* "  C", D only */
#define SPACE_ROUTING_E2R         0u   /* echo into plate (§4.3) */
#define SPACE_ROUTING_R2E         1u   /* plate into echo */
#define SPACE_ROUTING_LOOP        2u   /* plate inside the echo loop */
#define SPACE_ROUTING_STOPS       3u
#define SPACE_INTERVAL_ROW_MIN    39u  /* -24 st */
#define SPACE_INTERVAL_ROW_MAX    87u  /* +24 st */

extern const effect_param_descriptor_t space_delayDescriptors[];
extern const effect_param_descriptor_t space_reverbDescriptors[];
extern const effect_param_descriptor_t space_echoDescriptors[];
extern const char space_delayToken3[], space_delayAbbrev5[], space_delayFull8[];
extern const char space_reverbToken3[], space_reverbAbbrev5[], space_reverbFull8[];
extern const char space_echoToken3[], space_echoAbbrev5[], space_echoFull8[];
extern const effect_ui_hooks_t space_delayUi;
extern const effect_ui_hooks_t space_reverbUi;
extern const effect_ui_hooks_t space_echoUi;

#endif /* SPACE_PARAMETERS_H_ */
