/*
 * VelocityModulation.h
 *
 *  Created on: 06.01.2013
 *  Modified on 17.05.2026 by Brendan Clarke
 * ------------------------------------------------------------------------------------------------------------------------
 *  Copyright 2013 Julian Schmidt
 *  Julian@sonic-potions.com
 * ------------------------------------------------------------------------------------------------------------------------
 *  This file is part of the Sonic Potions LXR drumsynth firmware.
 * ------------------------------------------------------------------------------------------------------------------------
 *  Redistribution and use of the LXR code or any derivative works are permitted
 *  provided that the following conditions are met:
 *
 *       - The code may not be sold, nor may it be used in a commercial product or activity.
 *
 *       - Redistributions that are modified from the original source must include the complete
 *         source code, including the source code for all components used by a binary built
 *         from the modified sources. However, as a special exception, the source code distributed
 *         need not include anything that is normally distributed (in either source or binary form)
 *         with the major components (compiler, kernel, and so on) of the operating system on which
 *         the executable runs, unless that component itself accompanies the executable.
 *
 *       - Redistributions must reproduce the above copyright notice, this list of conditions and the
 *         following disclaimer in the documentation and/or other materials provided with the distribution.
 * ------------------------------------------------------------------------------------------------------------------------
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 *   INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *   DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 *   SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 *   WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 *   USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * ------------------------------------------------------------------------------------------------------------------------
 */


#ifndef VELOCITYMODULATION_H_
#define VELOCITYMODULATION_H_

#include "stm32f4xx.h"
#include "ParameterArray.h"

/*
 * Modulation destination namespace.
 *
 * Why: legacy modulation targets are indexes into parameterArray[], while new
 * instrument targets are canonical descriptor ids resolved to direct runtime
 * pointers by InstrumentManager. The enum lets every ModulationNode remember
 * which interpretation its destination field uses so descriptor ids are never
 * accidentally read as legacy array indexes.
 */
typedef enum {
	MOD_NODE_DEST_LEGACY_PARAM_ARRAY = 0,
	MOD_NODE_DEST_DIRECT_PARAMETER
} mod_node_destination_mode_t;

/*
 * Polarity values shared by LFO dispatch and ModulationNode target shaping.
 *
 * Inputs: Lfo::polarity stores one of these byte values through descriptor
 * menu writes. Output: modNode_updateValuePolarity() maps the raw 0..1 LFO
 * value into a range-relative destination delta. These constants live here
 * instead of lfo.h so ModulationNode does not include the LFO type and create
 * a circular ownership dependency.
 */
typedef enum {
	MOD_NODE_POLARITY_NEGATIVE = 0,
	MOD_NODE_POLARITY_POSITIVE,
	MOD_NODE_POLARITY_BIPOLAR
} mod_node_polarity_t;

/*
 * Cached legacy direct-target modulation range.
 *
 * Inputs: InstrumentManager builds this from the target descriptor, scalar
 * type, and runtime affiliation when installing or refreshing a direct target.
 * Outputs: ModulationNode uses min/max to scale amount by usable range instead
 * of by the current value. This keeps small base values from collapsing LFO
 * depth and avoids descriptor/range lookup inside the audio block hot path.
 * New descriptor-backed LFO targets should prefer InstrumentManager's
 * parameter-domain adapters; this range remains for legacy/direct backends such
 * as velocity while that migration is intentionally staged.
 */
typedef struct {
	float min;
	float max;
	uint8_t valid;
} mod_node_range_t;

/*
 * Runtime modulation source state.
 *
 * Accessors/clients: voice LFOs own one ModulationNode as Lfo::modTarget, and
 * velocityModulators[] owns six velocity nodes. Inputs arrive through
 * modNode_setDestination() for legacy ParameterArray ids or
 * modNode_setDirectDestination() for descriptor-resolved targets. Direct
 * targets also cache a min/max contract supplied by InstrumentManager for
 * legacy/direct backends. Descriptor-backed LFO instrument targets are now
 * installed as InstrumentManager adapters instead, because they must be shaped
 * in descriptor parameter space and then applied through owner-specific DSP
 * writers. Outputs are block-local parameter overlays applied by
 * modNode_updateValue()/modNode_updateValuePolarity() and restored by
 * modNode_resetTargets().
 */
typedef struct ModulatorStruct
{

	uint16_t	destination;	/**< legacy ParameterArray id or descriptor target id */
	uint8_t		type;			/**< pointer type */
	ptrValue	originalValue;	/**< stores the original value of the parameter*/
	float		amount;			/**< modulation amount*/
	float 		lastVal;
	uint8_t		destinationMode;	/**< mod_node_destination_mode_t value */
	Parameter	directParameter;	/**< descriptor-resolved runtime target */
	void		*waveInterpTarget;	/**< optional OscInfo* for waveform blend */
	mod_node_range_t range;		/**< cached direct-target min/max contract */

} ModulationNode;

//TODO move into corresponding voice
extern ModulationNode velocityModulators[6];

void modNode_init(ModulationNode* vm);
void modNode_resetTargets();
void modNode_reassignVeloMod();

/** if multiple nodes address the same target we need to update the other modNodes if one of them changes the destionation*/
//void modNode_originalValueModulated(uint16_t idx, ModulationNode* modSource);
void modNode_originalValueChanged(uint16_t idx);
/*
 * Clear one modulation destination regardless of its namespace.
 *
 * Inputs: vm is the source modulation node to clear. Output: any active target
 * is restored to vm->originalValue, then the node forgets its target pointer
 * and identity while preserving amount/lastVal. This exists separately from
 * modNode_setDestination(..., 0) because legacy ParameterArray id 0 and the new
 * descriptor-target off state are different concepts. Clients are
 * InstrumentManager's descriptor target off cases, modNode_init(), and the
 * future velocity/LFO target migration work.
 */
void modNode_clearDestination(ModulationNode* vm);
void modNode_setDestination(ModulationNode* vm, uint16_t dest);
/*
 * Install a descriptor-resolved runtime target directly into one mod node.
 *
 * Inputs: vm is the source node, destination is the canonical descriptor target
 * id used only for identity/refresh matching, parameter is the live runtime
 * pointer/type resolved by InstrumentManager, and waveInterpTarget is an
 * optional OscInfo* carried as void* for oscillator waveform interpolation.
 * Output: nonzero when the pointer is valid and the direct destination was
 * installed. This cannot be folded into modNode_setDestination(), whose input
 * remains a legacy ParameterArray index; overloading that API would recreate
 * the descriptor-id/legacy-id confusion that broke LFO target apply.
 */
uint8_t modNode_setDirectDestination(ModulationNode* vm,
									 uint16_t destination,
									 Parameter parameter,
									 void *waveInterpTarget,
									 mod_node_range_t range);
/*
 * Refresh originalValue and range for descriptor-backed targets after a base-value edit.
 *
 * Inputs: destination is a canonical descriptor target id that InstrumentManager
 * just applied through the ordinary runtime edit/load/morph path, and range is
 * the current descriptor min/max contract for that same live target. Output:
 * any active direct modulation node pointing at that id captures the current
 * base value and range again. This is separate from
 * modNode_originalValueChanged(), which still receives legacy ParameterArray ids.
 */
void modNode_directOriginalValueChanged(uint16_t destination,
										mod_node_range_t range);
void modNode_updateValue(ModulationNode* vm, float val);
/*
 * Apply one modulation sample with an explicit polarity.
 *
 * Inputs: vm is a legacy or descriptor-backed modulation node, val is the raw
 * modulation source value in 0..1, and polarity is mod_node_polarity_t. Output:
 * descriptor-backed nodes with valid cached ranges receive stable range-scaled
 * modulation; legacy nodes fall back to the historical negative/value-relative
 * formula used by velocity modulation. This cannot be folded into
 * modNode_updateValue() because LFOs need positive and bipolar polarity while
 * existing velocity callers still use the legacy negative API.
 */
void modNode_updateValuePolarity(ModulationNode* vm, float val,
								 uint8_t polarity);
/*
 * Shape a normalized modulation source against a descriptor parameter range.
 *
 * Inputs: retained base descriptor value, target min/max, normalized source,
 * normalized amount, and MOD_NODE_POLARITY_* selector. Output: a clamped
 * descriptor value suitable for instrumentManager_writeRuntime() or other
 * owner-specific setters. Negative polarity deliberately matches original LXR:
 * after the block reset restores the base value, the target is multiplied by
 * `(1 - amount + amount * source)`. It is not a subtraction from the full legal
 * range, which is what made shaped envelope/runtime fields move in the wrong
 * direction and saturate early.
 */
uint16_t modNode_shapeParameterU16(uint16_t base,
								   uint16_t min_value,
								   uint16_t max_value,
								   float source_0_1,
								   float amount_0_1,
								   uint8_t polarity);
/*
 * Shape a normalized modulation source against an explicit integer range.
 *
 * Inputs: retained base value, target min/max, normalized source,
 * normalized amount, and MOD_NODE_POLARITY_* selector. Output: a clamped
 * integer target value. This compatibility name now forwards to the
 * parameter-domain shaper so supplemental and Scene targets share the same
 * original-LXR negative polarity as descriptor adapters.
 *
 * This helper must be separate from modNode_rangeValue() because Scene targets
 * and slot-decimation targets are not ModulationNode destinations and should
 * not construct fake nodes or fake Parameters just to reuse polarity math.
 */
uint16_t modNode_shapeRangeU16(uint16_t base,
							   uint16_t min_value,
							   uint16_t max_value,
							   float source_0_1,
							   float amount_0_1,
							   uint8_t polarity);
void modNode_setWaveInterpEnabled(uint8_t enabled);
uint8_t modNode_getWaveInterpEnabled(void);
uint32_t modNode_getWaveInterpGeneration(void);
/*
 * Return the current maximum waveform index for oscillator waveform modulation.
 *
 * Inputs: none; the value follows SampleMemory's current sample count. Output:
 * the highest waveform id that a modulated oscillator waveform field may hold.
 * InstrumentManager uses this while building a direct-target range, and
 * ModulationNode uses it again for the interpolation write path. Keeping one
 * accessor prevents Menu or InstrumentManager from duplicating sample/waveform
 * packing rules.
 */
uint8_t modNode_getMaxWaveformIndex(void);
#endif /* VELOCITYMODULATION_H_ */
