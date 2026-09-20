/*
 * PatternStackService.h — unified dynamic Pattern-pool mutation service.
 *
 * What: declares the foreground/queued mutation boundary for PatternData's
 * dynamic pool. Why: menu edits, TIM3 live erase, bounded bulk barriers, and
 * background relocation must have one pool owner. Inputs are resident
 * Scene/track/step coordinates and the existing Pattern value types. Output:
 * synchronous results when idle or optimistic queue admission while busy.
 * Affiliate: PatternData.h remains the raw storage/read API.
 */
#ifndef PATTERN_STACK_SERVICE_H_
#define PATTERN_STACK_SERVICE_H_

#include <stdint.h>
#include "PatternData.h"

/*
 * Initialize the service after all resident Pattern regions and boot loads are
 * complete. Inputs: seq_activePattern and live Scene bitmaps. Outputs: the
 * mutation target, queue, cursors, and admission gate are initialized. The
 * service owns no Pattern payload storage beyond its bounded event queue.
 */
void patSvc_init(void);

/*
 * Advance one bounded 500 Hz service pass.
 *
 * What: handles target handover, deferred queue events, bulk barriers,
 * reactive recovery, and a finite bounded repair epoch with owned
 * trailing-slack reservations. Why: no pool scan, copy, or relocation is
 * allowed in TIM3 context. Inputs: live service state and seq_activePattern.
 * Output: at most one queue/relocation transaction per foreground pass, with
 * bounded bulk/repair scan work.
 */
void patSvc_tick(void);

/*
 * Report whether the active service scene is snapshot-safe for AutoSave.
 *
 * Output is nonzero only when admission is open, no queue/barrier remains,
 * and the playback target agrees with the service target. Affiliates:
 * filesystem_autosavePatternDrainSchedule_tick().
 */
uint8_t patSvc_idle(void);

/*
 * Quiesce the service before a filesystem replaces resident Pattern bytes.
 *
 * What: close admission and drain the current service Scene's queue/barrier
 * work, returning nonzero only when the caller may write the whole resident
 * region. Why: filesystem loaders write address, bitmap, and pool sections
 * directly through pat_sceneRegionMut(); that replacement must not overlap a
 * service relocation or queued pool mutation. Inputs: target Scene index.
 * Output: zero while deferred work still drains, one when the replacement
 * boundary is safe. Affiliate: filesystem.c Pattern load state machines.
 */
uint8_t patSvc_prepareSceneReplace(uint8_t scene);

/*
 * Reconcile and reopen the service after a filesystem Pattern replacement.
 *
 * What: recount the replaced bitmap, reset maintenance cursors, and reopen
 * admission when the playback target still agrees with the service Scene. Why:
 * direct filesystem writes must become the new service-owned baseline before
 * queue or compaction work resumes. Inputs: the replaced Scene index; output:
 * quiesced service state or a pending playback handover. Affiliate:
 * filesystem.c load completion/error boundaries.
 */
void patSvc_finishSceneReplace(uint8_t scene);

/*
 * Foreground dynamic-pool mutation entrypoints.
 *
 * Each function rejects a non-service Scene or closed admission, executes
 * directly while the service is idle, and otherwise publishes one compact
 * FIFO event. A queued foreground mutation returns optimistic acceptance;
 * execution-time allocation failure is traced and dropped by patSvc_tick().
 * These declarations are the only mutation calls Menu and generators use.
 */
uint8_t patSvc_writeStepAutomation(uint8_t scene, uint8_t track,
                                   uint8_t step, uint16_t target,
                                   uint8_t value);
uint8_t patSvc_removeStepAutomation(uint8_t scene, uint8_t track,
                                    uint8_t step, uint16_t target);
uint8_t patSvc_setStepNote(uint8_t scene, uint8_t track, uint8_t step,
                           uint8_t value);
uint8_t patSvc_setStepVolume(uint8_t scene, uint8_t track, uint8_t step,
                             uint8_t value);
uint8_t patSvc_setStepProbability(uint8_t scene, uint8_t track,
                                  uint8_t step, uint8_t value);
void patSvc_eraseStep(uint8_t scene, uint8_t track, uint8_t step);
void patSvc_clearTrack(uint8_t scene, uint8_t track);
void patSvc_clearPattern(uint8_t scene);
uint8_t patSvc_removeTrackAutomationByTarget(uint8_t scene, uint8_t track,
                                             uint16_t target);

/*
 * Publish one TIM3 live-erase request.
 *
 * What: enqueue DELETE_DYNAMIC for the implicit current service Scene. Why:
 * TIM3 may clear a static trigger bit, but it may not reclaim pool/bitmap
 * state. Inputs are the Scene/track/step observed by the ISR; output is queue
 * admission or a traced drop when handover/overflow closes the path.
 */
void patSvc_enqueueErase(uint8_t scene, uint8_t track, uint8_t step);

/*
 * Query and consume one chunk's service-owned trailing reservation.
 *
 * What: patSvc_isChunkReserved() reports whether a backed pool chunk is
 * reserved as trailing slack; patSvc_consumeReservation() clears that bit.
 * Why: PatternData.c's Gate-6 in-place automation append must consume its own
 * reserved trailing chunk without reaching into service statics. Inputs: a
 * chunk index bounded by the service pool geometry. Outputs: a reservation
 * query or a cleared reservation bit. The caller sets the occupancy bitmap in
 * the same transaction as consumption. Affiliate: pat_tryAppendAutomation().
 */
uint8_t patSvc_isChunkReserved(uint16_t chunk);
void patSvc_consumeReservation(uint16_t chunk);

#endif /* PATTERN_STACK_SERVICE_H_ */
