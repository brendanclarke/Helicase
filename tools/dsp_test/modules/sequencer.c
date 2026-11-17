/*
 * Sequencer module host tests: the automation drain cursor (S087 stage A).
 *
 * What:       compiles the real seq_drainPendingAutomationTo() and
 *             seq_pendingAutomationMark() from sequencer.c with recording
 *             stubs for the write targets.
 *             Laws:
 *               1. a limited drain applies exactly [done, limit);
 *               2. a second drain continues from the cursor, with no record
 *                  applied twice;
 *               3. the queue resets only when fully drained;
 *               4. a record appended between passes is applied once.
 * Why:        S087 stage A drains only up to the mark taken with the trigger
 *             pops, so a pass can end with records left over. Break proof:
 *             the Makefile's sequencer rule rebuilds the generated
 *             fragment with "uint8_t i = 0u;" (the pre-S087 start), and law 2
 *             must fail.
 * Inputs:     build/gen/sequencer_drain.c.
 * Outputs:    one line per failure (first 40); "sequencer failures: N";
 *             exit 1 if N > 0.
 * Accessors:  make -C tools/dsp_test sequencer.
 * Affiliates: sequencer.c, sequencer.h, PATTERN_DYNAMIC_STACK.md.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define SEQ_PENDING_BUF_COUNT 128u
#define SEQ_PENDING_TYPE_AUTOMATION_BIT (1u << 10u)
#define SEQ_PENDING_LIVE_BIT (1u << 11u)   /* S096 P3: no ramp */
#define SEQ_PENDING_STEP_ID_MASK 0x03FFu
#define NUM_STEPS 16u
#define INSTRUMENT_SLOT_COUNT 6u
#define INSTRUMENT_TARGET_AUTOMATION 1u

typedef struct {
	uint16_t identity;
	uint16_t payload;
} seq_pending_automation_t;

typedef uint16_t instrument_param_id_t;
typedef uint8_t instrument_target_use_t;
typedef struct ParamDescriptor ParamDescriptor;

static volatile seq_pending_automation_t
	seq_pending_automation[SEQ_PENDING_BUF_COUNT];
static volatile uint8_t seq_pending_automation_count = 0u;
static volatile uint8_t seq_pending_automation_drain = 0u;
static volatile uint8_t seq_pending_automation_done = 0u;
static volatile uint8_t seq_effectAutomationReset = 0u;
static uint64_t seq_menuOverride[INSTRUMENT_SLOT_COUNT];
static const uint8_t *seq_filterLatch;
static const uint8_t *seq_filterErase;
static uint8_t seq_filterEraseTrack;
static uint8_t seq_perTrackActive = 0u;

#define __get_PRIMASK() 0u
#define __disable_irq() ((void)0)
#define __set_PRIMASK(x) ((void)(x))

/* ---------------------------------------------------------------- stubs -- */
static uint16_t applied[64];
static int applied_n;
static int failures;

static void expect(int cond, const char *msg)
{
	if (!cond) {
		if (failures < 40)
			printf("FAIL: %s\n", msg);
		failures++;
	}
}

static void effects_automationReset(void) { }
static void seq_rampConsumeSteps(void) { }
uint8_t seq_getTrackPlayedScene(uint8_t track) { (void)track; return 0u; }
uint8_t instrumentParam_isVoiceParameter(instrument_param_id_t id)
{
	(void)id;
	return 1u;
}
uint8_t instrumentParam_slot(instrument_param_id_t id)
{
	(void)id;
	return 0u;
}
uint8_t instrumentParam_local(instrument_param_id_t id)
{
	return (uint8_t)(id & 0x3fu);
}
uint8_t instrumentManager_targetValid(uint8_t scene, uint16_t target,
									 instrument_target_use_t use)
{
	(void)scene; (void)target; (void)use;
	return 1u;
}
static const ParamDescriptor *seq_writeInstrumentAutomation(
	uint8_t owner_scene, uint16_t target, uint8_t value)
{
	(void)owner_scene; (void)value;
	if (applied_n < 64)
		applied[applied_n++] = target;
	return (const ParamDescriptor *)0;
}
static void seq_rampStartIfPaired(uint8_t track, uint8_t scene, uint8_t step,
								  uint16_t target, uint8_t from,
								  const ParamDescriptor *descriptor)
{
	(void)track; (void)scene; (void)step; (void)target; (void)from;
	(void)descriptor;
}
static uint8_t sceneModTarget_isSceneTarget(uint16_t target)
{
	(void)target;
	return 0u;
}
static uint8_t seq_applySceneAutomation(uint16_t target, uint8_t value)
{
	(void)target; (void)value;
	return 0u;
}
static uint8_t effectTarget_isEffectId(uint16_t target)
{
	(void)target;
	return 0u;
}
static uint8_t effectTarget_local(uint16_t target)
{
	(void)target;
	return 0u;
}
static uint8_t seq_trackEffectTypeMatchesActive(uint8_t track)
{
	(void)track;
	return 0u;
}
uint8_t effects_applyAutomation(uint8_t track, uint8_t local, uint8_t value)
{
	(void)track; (void)local; (void)value;
	return 0u;
}

#include "sequencer_drain.c"

/*
 * S096 P3 C1: the plain-step length window (law SQ-LEN).
 *
 * What:       the real seq_lenStart(), seq_rollClearTrack() and
 *             seq_processRollTicks() over the real StepScale table, with the
 *             roll engine idle, so the window's countdown and its release
 *             request are the only live paths.
 * Why:        the length special (fill) now ends a Colloid's plain step at
 *             step x fill / 100; a following trigger replaces the window and
 *             a roll reset clears it.
 */
#define NUM_TRACKS 7u
uint16_t stepScale_ticksQ8(uint8_t cc);   /* Core/Sequencer/StepScale.c */
static uint8_t seq_rollTickAccum[NUM_TRACKS];
static uint8_t seq_rollVelPos[NUM_TRACKS];
static uint16_t seq_rollWindow[NUM_TRACKS];
static uint8_t seq_rollActive[NUM_TRACKS];
static uint8_t seq_rollLastRate[NUM_TRACKS];
static uint8_t seq_rollLastRhythm[NUM_TRACKS];
static uint8_t seq_rollLastFill[NUM_TRACKS];
static uint8_t seq_rollStepNote[NUM_TRACKS];
static uint16_t seq_lenWindow[NUM_TRACKS];
static uint8_t seq_effectiveTrackScale[NUM_TRACKS];
#define SEQ_ROLL_RUN 0x01u

static unsigned len_releases;
static uint8_t len_releaseTrack;
static uint8_t len_releaseBrk;
static unsigned len_triggers;

static uint8_t seq_rollPpq(uint8_t rate) { (void)rate; return 0u; }
static uint8_t seq_rollComputeVelocity(uint8_t track) { (void)track; return 100u; }
static void seq_triggerVoice(uint8_t track, uint8_t vol, uint8_t note, uint8_t reset)
{ (void)track; (void)vol; (void)note; (void)reset; len_triggers++; }
void instrumentManager_requestGateRelease(uint8_t track, uint8_t brk)
{ len_releases++; len_releaseTrack = track; len_releaseBrk = brk; }
void instrumentManager_requestGateDuck(uint8_t track) { (void)track; }

#include "sequencer_len.c"

/* --------------------------------------------------------------- helpers -- */
static void queue_clear(void)
{
	seq_pending_automation_count = 0u;
	seq_pending_automation_drain = 0u;
	seq_pending_automation_done = 0u;
	seq_effectAutomationReset = 0u;
	applied_n = 0;
	memset(applied, 0, sizeof(applied));
}

static void queue_put(uint8_t target, uint8_t value)
{
	uint8_t i = seq_pending_automation_count;

	seq_pending_automation[i].identity =
		(uint16_t)(0u | SEQ_PENDING_TYPE_AUTOMATION_BIT);
	seq_pending_automation[i].payload =
		(uint16_t)((uint16_t)target | ((uint16_t)(value & 0x7fu) << 9u));
	seq_pending_automation_count = (uint8_t)(i + 1u);
	seq_pending_automation_drain = 1u;
}

static void queue_put_live(uint8_t target, uint8_t value)
{
	uint8_t i = seq_pending_automation_count;

	seq_pending_automation[i].identity =
		(uint16_t)(0u | SEQ_PENDING_TYPE_AUTOMATION_BIT | SEQ_PENDING_LIVE_BIT);
	seq_pending_automation[i].payload =
		(uint16_t)((uint16_t)target | ((uint16_t)(value & 0x7fu) << 9u));
	seq_pending_automation_count = (uint8_t)(i + 1u);
	seq_pending_automation_drain = 1u;
}

/* SQ-LEN (S096 P3 C1): a plain step's fill window, its end, replacement and
 * clear. */
static void test_len(void)
{
	unsigned i;
	const uint16_t half = (uint16_t)(((uint32_t)stepScale_ticksQ8(76u) * 50u +
	                                  12800u) / 25600u);

	seq_effectiveTrackScale[0] = 76u;
	len_releases = 0u;
	seq_lenStart(0u, 50u);
	expect(seq_lenWindow[0] == half, "SQ-LEN: fill 50 window is step x 50 / 100");
	for (i = 0u; i < (unsigned)(half - 1u); i++)
		seq_processRollTicks();
	expect(len_releases == 0u, "SQ-LEN: no release before the window's end");
	seq_processRollTicks();
	expect(len_releases == 1u && len_releaseTrack == 0u && len_releaseBrk == 0u,
	       "SQ-LEN: one unbroken release request at the window's end");
	expect(seq_lenWindow[0] == 0u, "SQ-LEN: the window closes");

	/* a following trigger replaces the window */
	seq_lenStart(0u, 25u);
	expect(seq_lenWindow[0] ==
	       (uint16_t)(((uint32_t)stepScale_ticksQ8(76u) * 25u + 12800u) / 25600u),
	       "SQ-LEN: a following trigger replaces the window");
	/* a roll reset clears it */
	seq_rollClearTrack(0u);
	expect(seq_lenWindow[0] == 0u, "SQ-LEN: seq_rollClearTrack clears the window");
	/* fill 100: no window; fill 0: one tick */
	seq_lenStart(0u, 100u);
	expect(seq_lenWindow[0] == 0u, "SQ-LEN: fill 100 has no window");
	len_releases = 0u;
	seq_processRollTicks();
	expect(len_releases == 0u, "SQ-LEN: fill 100 never releases");
	seq_lenStart(0u, 0u);
	expect(seq_lenWindow[0] == 1u, "SQ-LEN: fill 0 is one tick");
	seq_processRollTicks();
	expect(len_releases == 1u, "SQ-LEN: fill 0 releases at the next tick");
}

int main(void)
{
	/* Build a three-record queue. */
	queue_clear();
	queue_put(1u, 10u);
	queue_put(2u, 20u);
	queue_put(3u, 30u);

	/* 1: limited drain applies exactly [0, 2). */
	seq_drainPendingAutomationTo(2u);
	expect(applied_n == 2, "law 1: two records applied");
	expect(applied[0] == 1u && applied[1] == 2u, "law 1: order 1 then 2");
	expect(seq_pending_automation_done == 2u, "law 1: cursor advanced to 2");

	/* 3: the queue did not reset. */
	expect(seq_pending_automation_count == 3u, "law 3: queue kept");
	expect(seq_pending_automation_drain == 1u, "law 3: drain flag kept");

	/* 2: the second drain continues at 2, then resets. */
	seq_drainPendingAutomationTo(3u);
	expect(applied_n == 3, "law 2: one more record");
	expect(applied[2] == 3u, "law 2: record 3 applied once");
	expect(seq_pending_automation_count == 0u &&
	       seq_pending_automation_done == 0u &&
	       seq_pending_automation_drain == 0u,
	       "law 2/3: a fully drained queue resets");

	/* 4: append between passes; the new record applies once. */
	queue_clear();
	queue_put(4u, 40u);
	seq_drainPendingAutomationTo(1u);
	queue_put(5u, 50u);
	seq_drainPendingAutomationTo(2u);
	expect(applied_n == 2 && applied[0] == 4u && applied[1] == 5u,
	       "law 4: each record applied once, in order");
	expect(seq_pending_automation_count == 0u, "law 4: drained");

	/* Mark is the producer count. */
	queue_clear();
	queue_put(6u, 60u);
	expect(seq_pendingAutomationMark() == 1u, "mark equals the producer count");

	/* S096 P3 C3: a LIVE entry beyond the mark lands in the same drain;
	 * an ordinary entry beyond it still waits. */
	queue_clear();
	queue_put(7u, 70u);
	queue_put_live(8u, 80u);
	seq_drainPendingAutomationTo(1u);
	expect(applied_n == 2 && applied[0] == 7u && applied[1] == 8u,
	       "S096 P3: LIVE entry beyond the mark applies now");
	queue_clear();
	queue_put(9u, 90u);
	queue_put_live(10u, 100u);
	queue_put(11u, 110u);
	seq_drainPendingAutomationTo(1u);
	expect(applied_n == 2 && applied[1] == 10u,
	       "S096 P3: an ordinary entry beyond the mark waits");
	seq_drainPendingAutomationTo(3u);
	expect(applied_n == 3 && applied[2] == 11u,
	       "S096 P3: the waiting entry applies on the next pass");

	test_len();

	printf("sequencer failures: %d\n", failures);
	return failures ? 1 : 0;
}
