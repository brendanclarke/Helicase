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
 * Runtime modulation source state.
 *
 * Accessors/clients: voice LFOs own one ModulationNode as Lfo::modTarget, and
 * velocityModulators[] owns six velocity nodes. Inputs arrive through
 * modNode_setDestination() for legacy ParameterArray ids or
 * modNode_setDirectDestination() for descriptor-resolved targets. Outputs are
 * block-local parameter overlays applied by modNode_updateValue() and restored
 * by modNode_resetTargets().
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
									 void *waveInterpTarget);
/*
 * Refresh originalValue for descriptor-backed targets after a base-value edit.
 *
 * Inputs: destination is a canonical descriptor target id that InstrumentManager
 * just applied through the ordinary runtime edit/load/morph path. Output: any
 * active direct modulation node pointing at that id captures the current base
 * value again. This is separate from modNode_originalValueChanged(), which
 * still receives legacy ParameterArray ids.
 */
void modNode_directOriginalValueChanged(uint16_t destination);
void modNode_updateValue(ModulationNode* vm, float val);
void modNode_setWaveInterpEnabled(uint8_t enabled);
uint8_t modNode_getWaveInterpEnabled(void);
uint32_t modNode_getWaveInterpGeneration(void);
#endif /* VELOCITYMODULATION_H_ */
