
#include <stdint.h>
#include <string.h>
#include "AlkalineVoice.h"
const int16_t sawTable[11][1024];
const int16_t alkSqrTable[11][1024];
void lfo_init(Lfo *l) { memset(l, 0, sizeof(*l)); }
void osc_setBaseNote(OscInfo *o, uint8_t n) { o->baseNote = n; }
void osc_setFreq(OscInfo *o) { o->phaseInc = (uint32_t)o->freq; }
void osc_setTableFreq(OscInfo *o) { o->phaseInc = (uint32_t)o->freq; }
void calcWavetableOscBlock(OscInfo *o, const int16_t t[][1024], int16_t *b,
                           const uint8_t n, const float g)
{ (void)o; (void)t; (void)g; memset(b, 0, n * 2u); }
AlkalineVoice v;
int16_t buf[32];
int main(void)
{
	Alkaline_initVoice(&v);
	for (;;) {
		Alkaline_triggerVoice(&v, 101u, 63u, 5514u);
		Alkaline_calcVoiceAsync(&v);
		Alkaline_calcVoiceSyncBlock(&v, buf, 32u);
		if (Alkaline_isQuiet(&v)) break;
	}
	return 0;
}
