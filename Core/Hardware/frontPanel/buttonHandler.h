/*
 * buttonHandler.h — LXR-02 button handler.
 * Ported from original LXR AVR buttonHandler.h by Julian Schmidt.
 *
 * ISR SAFETY: buttonHandler_buttonPressed / buttonReleased are called from
 * the TIM6 ISR (via din_dout_exchange). They must NOT call any LCD functions
 * or spin-wait. They record events; buttonHandler_processEvents() in the main
 * loop does all the work that touches the LCD/menu.
 */
#ifndef BUTTONHANDLER_H_
#define BUTTONHANDLER_H_
#include <stdint.h>

/* Select-button modes */
#define SELECT_MODE_VOICE       0x00
#define SELECT_MODE_PERF        0x01
#define SELECT_MODE_STEP        0x02
#define SELECT_MODE_LOAD_SAVE   0x03
#define SELECT_MODE_PAT_GEN     0x05
#define SELECT_MODE_SOM_GEN     0x06
#define SELECT_MODE_MENU        0x07

#define BUTTON_TIMEOUT          500u  /* ~500 ms; original AVR used 38 * 13.107 ms */
#define NO_STEP_SELECTED        -1

/* Button index constants — LXR-02 shift-register chain order
**
** Shift register chain: 40 buttons across 5 bytes, indices 0..39.
** Index 40 is SW43 (PB7 GPIO) — a hardware-separate button, registered
** here so the same button-event API covers it. SW43 is BAR1 on LXR-02.
**
** BAR1 / BAR2: new buttons on LXR-02 not present on the original LXR.
** No port functionality assigned to them yet — reserved for future
** firmware. They are read and the LEDs can be lit; nothing acts on the
** events. */
enum ButtonNumbers {
    BUT_VOICE_7    = 0,  BUT_VOICE_6  = 1,  BUT_VOICE_5  = 2,  BUT_VOICE_4  = 3,
    BUT_VOICE_3    = 4,  BUT_VOICE_2  = 5,  BUT_VOICE_1  = 6,  BUT_BAR2     = 7,
    BUT_SEQ13      = 8,  BUT_SEQ14    = 9,  BUT_SEQ15    = 10, BUT_SEQ16    = 11,
    BUT_SELECT8    = 12, BUT_SELECT7  = 13, BUT_SELECT6  = 14, BUT_SELECT5  = 15,
    BUT_SEQ9       = 16, BUT_SEQ10    = 17, BUT_SEQ11    = 18, BUT_SEQ12    = 19,
    BUT_SELECT4    = 20, BUT_SELECT3  = 21, BUT_SELECT2  = 22, BUT_SELECT1  = 23,
    BUT_SEQ5       = 24, BUT_SEQ6     = 25, BUT_SEQ7     = 26, BUT_SEQ8     = 27,
    BUT_MODE4      = 28, BUT_MODE3    = 29, BUT_MODE2    = 30, BUT_MODE1    = 31,
    BUT_SEQ1       = 32, BUT_SEQ2     = 33, BUT_SEQ3     = 34, BUT_SEQ4     = 35,
    BUT_COPY       = 36, BUT_REC      = 37, BUT_START_STOP=38, BUT_SHIFT    = 39,
    BUT_BAR1       = 40,
    BUT_COUNT      = 41,
};

/*
 * Called from ISR context (TIM6) — only records events, no LCD calls.
 */
void buttonHandler_buttonPressed(uint8_t buttonNr);
void buttonHandler_buttonReleased(uint8_t buttonNr);

/*
 * Called from main loop — drains the event queue and executes menu/LED actions.
 */
void buttonHandler_processEvents(void);

/* Legacy tick — kept for long-press timer (can be called from main loop) */
void buttonHandler_tick(void);

uint8_t buttonHandler_getMode(void);
uint8_t buttonHandler_getShift(void);
int8_t  buttonHandler_getArmedAutomationStep(void);
void    buttonHandler_setRunStopState(uint8_t running);
void    buttonHandler_showMuteLEDs(void);
void    buttonHandler_muteVoice(uint8_t voice, uint8_t isMuted);

extern uint8_t  buttonHandler_selectedStep;
extern uint16_t buttonHandler_originalParameter;
extern uint8_t  buttonHandler_originalValue;
extern uint8_t  buttonHandler_resetLock;

#endif
