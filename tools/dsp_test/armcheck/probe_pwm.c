/*
 * ARM cost probe: pwm families (S085 C17.4, A3).
 *
 * What:       includes OscillatorPwm.c; one wrapper per family and swp
 *             segment, plain and FM, with the family and segment fixed on a
 *             local copy, so the per-sample family switch folds and the
 *             innermost loop is that family's body. p_pwm_*_lane is the
 *             firmware form (oscPwm_blockLane(): runtime lane test, loop
 *             unswitching off): its count includes the sync select, i.e. the
 *             synced-slave cost; the plain firmware cost is the plain count
 *             plus the lane test.
 * Accessors:  make -C tools/dsp_test armcheck-osc (osccost.py).
 */
#include "OscillatorPwm.c"

#define PROBE __attribute__((noinline))

#define PWM_PLAIN(name, fam, sg)                                          \
PROBE void p_pwm_##name(struct OscStruct *o, int16_t *b, uint8_t n)       \
{                                                                         \
	struct OscStruct l = *o;                                              \
	l.waveform = (fam);                                                   \
	l.pwm.seg = (sg);                                                     \
	oscPwm_block(&l, 0, b, n, 1.0f, 0);                                   \
	o->phase = l.phase;                                                   \
}                                                                         \
PROBE void p_pwm_##name##_fm(struct OscStruct *o, const int16_t *m,       \
                             int16_t *b, uint8_t n)                       \
{                                                                         \
	struct OscStruct l = *o;                                              \
	l.waveform = (fam);                                                   \
	l.pwm.seg = (sg);                                                     \
	oscPwm_block(&l, m, b, n, 1.0f, 0);                                   \
	o->phase = l.phase;                                                   \
}                                                                         \
OSC_SHARED_LOOP void p_pwm_##name##_lane(struct OscStruct *o, int16_t *b, \
                                         uint8_t n, const OscSyncLane *ln) \
{                                                                         \
	struct OscStruct l = *o;                                              \
	l.waveform = (fam);                                                   \
	l.pwm.seg = (sg);                                                     \
	oscPwm_block(&l, 0, b, n, 1.0f, ln);                                  \
	o->phase = l.phase;                                                   \
}

PWM_PLAIN(prc, OSC_PWM_PRC, 0u)
PWM_PLAIN(psw, OSC_PWM_PSW, 0u)
PWM_PLAIN(ptr, OSC_PWM_PTR, 0u)
PWM_PLAIN(pq3, OSC_PWM_PQ3, 0u)
PWM_PLAIN(pw3, OSC_PWM_PW3, 0u)
PWM_PLAIN(swp0, OSC_PWM_SWP, 0u)
PWM_PLAIN(swp1, OSC_PWM_SWP, 1u)
PWM_PLAIN(swp2, OSC_PWM_SWP, 2u)
PWM_PLAIN(swp3, OSC_PWM_SWP, 3u)
