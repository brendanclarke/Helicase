/*
 * Voice control module host tests: step automation before the trigger
 * (S087 stage A, D6).
 *
 * What:       compiles the real voiceControl_processPending(),
 *             voiceControl_popPendingBatch(), voiceControl_prepareTrigger()
 *             and voiceControl_fireTrigger() from MidiVoiceControl.c (via
 *             extract.py, with the ARM PRIMASK helpers renamed to host
 *             no-ops) and records the order of every call they make.
 *             Laws:
 *               1. deferred Scene apply -> restore -> drain -> trigger;
 *               2. the drain limit is the mark taken with the pops;
 *               3. records appended after the mark are not in this pass's
 *                  limit and wait for the next pass;
 *               4. leftover reset bits restore before the drain;
 *               5. a popped trigger without its reset bit does not restore.
 * Why:        S087 P0 D6: a step's own automation must land before its
 *             trigger, or every hit's first block uses the previous values.
 *             Break proof: built with -DBREAK_DRAIN_AFTER_FIRE, the test runs
 *             broken_processPending() (the same extracted helpers in the old
 *             trigger-then-drain order) and law 1 must fail.
 * Inputs:     build/gen/voice_control_pending.c (the Makefile's voice_control
 *             rule).
 * Outputs:    one line per failure (first 40); "voice_control failures: N";
 *             exit 1 if N > 0.
 * Accessors:  make -C tools/dsp_test voice_control.
 * Affiliates: MidiVoiceControl.c, sequencer.h (seq_pendingAutomationMark(),
 *             seq_drainPendingAutomationTo()), DSP_TEST.md.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define VOICE_TRIGGER_RING_SIZE 32u
#define VOICE_TRIGGER_RING_MASK (VOICE_TRIGGER_RING_SIZE - 1u)
/* S086/S087: the LED index base the trigger funnel adds (ledHandler.h). */
#define LED_VOICE1 0u

typedef struct {
	uint8_t voice;
	uint8_t note;
	uint8_t vel;
} VoiceTriggerEvent;

/* The extracted functions' state, defined here exactly as in the source. */
static VoiceTriggerEvent voiceTriggerRing[VOICE_TRIGGER_RING_SIZE];
static uint8_t voiceTriggerHead = 0;
static uint8_t voiceTriggerTail = 0;
static uint8_t voiceControl_pendingReset;

/* ------------------------------------------------------------------ log -- */
enum { EV_DEFERRED = 1, EV_RESTORE, EV_MARK, EV_DRAIN, EV_TRIGGER };

typedef struct { uint8_t kind; uint8_t a; uint8_t b; } ev_t;
static ev_t evlog[256];
static int evn;
static uint8_t lastLimit;
static uint8_t lastMark;
static uint8_t g_queue;
static uint8_t g_appendAfterMark;
static int failures;

static void log_ev(uint8_t kind, uint8_t a, uint8_t b)
{
	if (evn < 256) {
		evlog[evn].kind = kind;
		evlog[evn].a = a;
		evlog[evn].b = b;
		evn++;
	}
}

static void reset_log(void)
{
	evn = 0;
	lastLimit = 0xffu;
	lastMark = 0xffu;
}

static void expect(int cond, const char *msg)
{
	if (!cond) {
		if (failures < 40)
			printf("FAIL: %s\n", msg);
		failures++;
	}
}

/* Host stand-ins for the ARM interrupt helpers the pop batch calls. */
static uint32_t sta_irqSave(void) { return 0u; }
static void sta_irqRestore(uint32_t p) { (void)p; }

/* ---------------------------------------------------------------- stubs -- */
void preset_applyDeferredSceneSlotForTrigger(uint8_t voice)
{
	log_ev(EV_DEFERRED, voice, 0u);
}

void seq_restoreAutomatedParameters(uint8_t voice)
{
	log_ev(EV_RESTORE, voice, 0u);
}

uint8_t seq_pendingAutomationMark(void)
{
	uint8_t m = g_queue;

	if (g_appendAfterMark) {
		g_appendAfterMark = 0u;
		if (g_queue < 255u)
			g_queue++;
	}
	lastMark = m;
	return m;
}

void seq_drainPendingAutomationTo(uint8_t limit)
{
	lastLimit = limit;
	log_ev(EV_DRAIN, limit, 0u);
}

void instrumentManager_triggerTrack(uint8_t voice, uint8_t note, uint8_t vel)
{
	log_ev(EV_TRIGGER, voice, note | (vel << 4));
}

void busComp_sidechainTrigger(uint8_t voice, uint8_t vel)
{
	(void)voice; (void)vel;
}

void led_pulseLed(uint8_t led)
{
	(void)led;
}

#include "voice_control_pending.c"

/* --------------------------------------------------------------- helpers -- */
static void push(uint8_t voice, uint8_t note, uint8_t vel)
{
	voiceTriggerRing[voiceTriggerHead].voice = voice;
	voiceTriggerRing[voiceTriggerHead].note = note;
	voiceTriggerRing[voiceTriggerHead].vel = vel;
	voiceTriggerHead = (uint8_t)((voiceTriggerHead + 1u) & VOICE_TRIGGER_RING_MASK);
}

static void reset_world(void)
{
	memset(voiceTriggerRing, 0, sizeof(voiceTriggerRing));
	voiceTriggerHead = 0u;
	voiceTriggerTail = 0u;
	voiceControl_pendingReset = 0u;
	g_queue = 0u;
	g_appendAfterMark = 0u;
	reset_log();
}

#ifdef BREAK_DRAIN_AFTER_FIRE
/* The pre-S087 order, through the same extracted helpers: trigger first. */
static void broken_processPending(void)
{
	VoiceTriggerEvent batch[VOICE_TRIGGER_RING_SIZE];
	uint8_t resetFlags[VOICE_TRIGGER_RING_SIZE];
	uint8_t leftover, mark, n, i;

	n = voiceControl_popPendingBatch(batch, resetFlags, &leftover, &mark);
	for (i = 0u; i < n; i++)
		voiceControl_prepareTrigger(batch[i].voice, resetFlags[i]);
	for (i = 0u; i < n; i++)
		voiceControl_fireTrigger(batch[i].voice, batch[i].note, batch[i].vel);
	seq_drainPendingAutomationTo(mark);
}
#endif

static void run_pass(void)
{
#ifdef BREAK_DRAIN_AFTER_FIRE
	broken_processPending();
#else
	voiceControl_processPending();
#endif
}

static int ev_is(int at, uint8_t kind, uint8_t a)
{
	return at < evn && evlog[at].kind == kind && evlog[at].a == a;
}

int main(void)
{
	/* 1/2: two triggers, one resetting; order and marked limit. */
	reset_world();
	push(0u, 60u, 100u);
	push(1u, 62u, 90u);
	voiceControl_pendingReset = 1u; /* voice 0 resets */
	g_queue = 1u;
	run_pass();
	expect(evn == 6, "law 1: six calls");
	expect(ev_is(0, EV_DEFERRED, 0u), "law 1: deferred voice 0 first");
	expect(ev_is(1, EV_RESTORE, 0u), "law 1: restore voice 0 second");
	expect(ev_is(2, EV_DEFERRED, 1u), "law 1: deferred voice 1 third");
	expect(ev_is(3, EV_DRAIN, 1u), "law 1: drain fourth");
	expect(ev_is(4, EV_TRIGGER, 0u), "law 1: trigger voice 0 fifth");
	expect(ev_is(5, EV_TRIGGER, 1u), "law 1: trigger voice 1 sixth");
	expect(lastLimit == lastMark, "law 2: drain limit is the mark");
	expect(lastLimit == 1u, "law 2: one published entry");

	/* 3: an entry published after the mark waits for the next pass. */
	reset_world();
	push(0u, 60u, 100u);
	voiceControl_pendingReset = 1u;
	g_queue = 1u;
	g_appendAfterMark = 1u;
	run_pass();
	expect(lastMark == 1u && lastLimit == 1u,
	       "law 3: the mark excludes the late record");
	expect(g_queue == 2u, "law 3: producer advanced after the mark");
	reset_log();
	run_pass();
	expect(lastMark == 2u && lastLimit == 2u,
	       "law 3: the next pass applies the late record");

	/* 4: a leftover reset bit restores before the drain. */
	reset_world();
	voiceControl_pendingReset = (uint8_t)(1u << 2);
	g_queue = 1u;
	run_pass();
	expect(evn == 2, "law 4: restore then drain");
	expect(ev_is(0, EV_RESTORE, 2u), "law 4: leftover voice restores first");
	expect(ev_is(1, EV_DRAIN, 1u), "law 4: drain after the leftover restore");

	/* 5: a popped trigger with no reset bit does not restore. */
	reset_world();
	push(0u, 60u, 100u);
	g_queue = 1u;
	run_pass();
	expect(!ev_is(1, EV_RESTORE, 0u), "law 5: no restore without the bit");
	expect(evn == 3, "law 5: deferred, drain, trigger");

#ifdef BREAK_DRAIN_AFTER_FIRE
	/* The break run must fail law 1 (it reports the failures). */
	printf("voice_control failures: %d\n", failures);
	return failures > 0 ? 0 : 1;
#else
	printf("voice_control failures: %d\n", failures);
	return failures ? 1 : 0;
#endif
}
