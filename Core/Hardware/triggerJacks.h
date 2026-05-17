#ifndef TRIGGERJACKS_H_
#define TRIGGERJACKS_H_

#include <stdint.h>
#include "globals.h"

#define PIN_TRACK_1 GPIO_Pin_8	//Port D
#define PIN_TRACK_2 GPIO_Pin_9	//Port D
#define PIN_TRACK_3 GPIO_Pin_10	//Port D
#define PIN_TRACK_4 GPIO_Pin_11	//Port D
#define PIN_TRACK_5 GPIO_Pin_12	//Port D
#define PIN_TRACK_6 GPIO_Pin_13	//Port D
#define PIN_TRACK_7 GPIO_Pin_14	//Port D

#define PIN_CLOCK_1 GPIO_Pin_15	//Port D
#define PIN_CLOCK_2 GPIO_Pin_9	//Port A
#define PIN_RESET GPIO_Pin_10	//Port A

#define PIN_CLOCK_IN GPIO_Pin_9 //Port C
#define PIN_RESET_IN GPIO_Pin_8 //Port A

#define PULSE_LENGTH_MS 20 //[ms]
#define PULSE_LENGTH (PULSE_LENGTH_MS * SYSTICK_TICKS_PER_MS) // systick_ticks is 0.25ms

enum
{
	TRIGGER_1 = 0,
	TRIGGER_2,
	TRIGGER_3,
	TRIGGER_4,
	TRIGGER_5,
	TRIGGER_6,
	TRIGGER_7,

	CLOCK_1,
	CLOCK_2,
	TRIGGER_RESET,
	TRIGGER_ALL,
	NUM_PINS,
};

// since we have 4 main steps per quarter and 8 sub steps per main step
// our native resolution is 32ppq
enum Prescaler
{
	PRE_1_PPQ	= 32/1,
	PRE_4_PPQ	= 32/4,
	PRE_8_PPQ	= 32/8,
	PRE_16_PPQ	= 32/16,
	PRE_32_PPQ	= 1,
};

typedef enum TriggerModes
{
	TRIGGER_ON,
	TRIGGER_OFF,
	TRIGGER_PULSE
} triggerMode;

extern uint8_t trigger_dividerClockOut1;
extern uint8_t trigger_dividerClockOut2;
extern uint8_t trigger_prescalerClockInput;


void    triggerJacks_init(void);  // replaces void trigger_init();
void    triggerJacks_toggleClkOut(void);
void    triggerJacks_isrTick(void);      /* call from TIM6_DAC_IRQHandler */
uint8_t triggerJacks_tick(void);  /* drained by TIM3 sequencer timing owner */
uint8_t triggerJacks_displayActive(void);
uint8_t triggerJacks_clockInputRecently(uint32_t nowUs);
void    triggerJacks_setClockInputPpq(uint8_t menuValue);
void    triggerJacks_setClockOut1Ppq(uint8_t menuValue);
void    triggerJacks_setClockOut2Ppq(uint8_t menuValue);
void    trigger_triggerVoice(uint8_t voiceNr, uint8_t onOff); // stub, no individual analog triggers
void    trigger_clockTick(uint8_t pos);// the sequencer calls this function whenever a step is played to generate 
                                    // corresponding trigger out clocks. pretty sure just needs a stub - only
                                    // ever need to toggle clock

void trigger_reset(uint8_t value); // include commented - might need later
void trigger_tickPhaseCounter(); // include commented - might need later
uint8_t trigger_isGateModeOn(); // stub, not used
void trigger_setGatemode(uint8_t onOff); // stub, not used
void trigger_allOff(); // stub, not used



#endif /* TRIGGERJACKS_H_ */
