/*
 * VelocityModulation.c
 *
 *  Created on: 06.01.2013
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

/*
 *  Modified on: 17.05.2026
 * ------------------------------------------------------------------------------------------------------------------------
 *  Modifications Copyright 2026 Brendan Clarke
 *  brendanpaulclarke@gmail.com
 *  https://www.brendanclarke.com
 * ------------------------------------------------------------------------------------------------------------------------
 *  The modifications to this file are part of the LXR02 Open-Source software.
 *  The same license and restrictions on use for the LXR software apply.
 * ------------------------------------------------------------------------------------------------------------------------
 */




#include "modulationNode.h"
#include "DrumVoice.h"
#include "CymbalVoice.h"
#include "HiHat.h"
#include "Snare.h"
#include "Oscillator.h"
#include "../SampleRom/SampleMemory.h"
// TODO DSP_PORT
// #include "sequencer.h"

INCCMZ ModulationNode velocityModulators[6];
/* Oscillator waveform interpolation budget for one 32-frame DSP block.
**
** modNode_resetTargets() starts a new generation at the top of each mixer
** block. The first OSC_WAVE_INTERP_MAX_ACTIVE eligible oscillator waveform
** targets receive a fractional base/next pair; later eligible targets fall
** back to integer waveform IDs for that block. This bounds CPU cost while
** preserving the headline user-sample interpolation path. */
static INCCMZ uint8_t modNode_waveInterpEnabled = 0;
static INCCMZ uint32_t modNode_waveInterpGeneration = 1u;
static INCCMZ uint8_t modNode_waveInterpActiveCount = 0u;

static OscInfo* modNode_getWaveTargetOsc(uint16_t param)
{
	(void)param;
	return 0;
}

static uint8_t modNode_getMaxWaveformIndex(void)
{
	const uint8_t sampleCount = sampleMemory_getNumSamples();
	const uint16_t maxWave = (uint16_t)OSC_SAMPLE_START + sampleCount;
	if (maxWave == 0u) {
		return CRASH;
	}
	return (uint8_t)(maxWave - 1u);
}

void modNode_setWaveInterpEnabled(uint8_t enabled)
{
	modNode_waveInterpEnabled = enabled ? 1u : 0u;
}

uint8_t modNode_getWaveInterpEnabled(void)
{
	return modNode_waveInterpEnabled;
}

uint32_t modNode_getWaveInterpGeneration(void)
{
	return modNode_waveInterpGeneration;
}

static const Parameter *modNode_currentParameter(const ModulationNode* vm)
{
	/*
	 * Resolve the currently active target for one modulation node.
	 *
	 * Inputs: vm may be a legacy ParameterArray-backed node or a new
	 * descriptor-backed direct node. Output: the live Parameter pointer/type
	 * pair to read/write, or NULL when the node is off or malformed. This helper
	 * keeps every caller from repeating the namespace split: legacy destinations
	 * index parameterArray[], while descriptor destinations store an already
	 * resolved runtime pointer supplied by InstrumentManager.
	 */
	if (!vm) {
		return 0;
	}
	if (vm->destinationMode == MOD_NODE_DEST_DIRECT_PARAMETER) {
		return vm->directParameter.ptr ? &vm->directParameter : 0;
	}
	if (vm->destination >= END_OF_SOUND_PARAMETERS) {
		return 0;
	}
	return parameterArray[vm->destination].ptr ? &parameterArray[vm->destination] : 0;
}

static void modNode_writeParameterValue(const Parameter *p, ptrValue value)
{
	/*
	 * Write a typed modulation restore value through an already resolved target.
	 *
	 * Inputs: p is either a legacy parameterArray[] entry or a descriptor
	 * directParameter; value is the stored original value. Output: the target is
	 * restored in-place. This helper exists because descriptor targets cannot be
	 * restored through paramArray_setParameter(), but they must still obey the
	 * same scalar type tags that the legacy modulation node already used.
	 */
	if (!p || !p->ptr) {
		return;
	}
	switch(p->type)
	{
		case TYPE_UINT8:
			*((uint8_t*)p->ptr) = (uint8_t)value.itg;
			break;

		case TYPE_UINT32:
			*((uint32_t*)p->ptr) = value.itg;
			break;

		case TYPE_SPECIAL_F:
		case TYPE_SPECIAL_P:
		case TYPE_SPECIAL_FILTER_F:
		case TYPE_FLT:
			*((float*)p->ptr) = value.flt;
			break;

		default:
			break;
	}
}

static uint8_t modNode_captureOriginalValue(ModulationNode* vm)
{
	const Parameter *p = modNode_currentParameter(vm);

	/*
	 * Capture the baseline that modNode_resetTargets() restores before the next
	 * audio block's modulation overlay.
	 *
	 * Inputs: vm already has its target mode and pointer/id installed. Output:
	 * vm->originalValue is refreshed and the function returns nonzero when a
	 * live target exists. TYPE_SPECIAL_F intentionally keeps the legacy baseline
	 * of 1.0f; oscillator pitch and LFO-rate rows use modNodeValue as a
	 * multiplier overlay rather than as the stored edit value.
	 */
	if (!p) {
		return 0u;
	}
	switch(p->type)
	{
		case TYPE_UINT8:
			vm->originalValue.itg = *((uint8_t*)p->ptr);
			break;

		case TYPE_SPECIAL_F:
			vm->originalValue.flt = 1.f;
			break;

		case TYPE_SPECIAL_P:
		case TYPE_SPECIAL_FILTER_F:
		case TYPE_FLT:
			vm->originalValue.flt = *((float*)p->ptr);
			break;

		case TYPE_UINT32:
			vm->originalValue.itg = *((uint32_t*)p->ptr);
			break;

		default:
			break;
	}
	return 1u;
}

static void modNode_restoreTarget(ModulationNode* vm)
{
	const Parameter *p;

	/*
	 * Restore one node's current target before clearing or applying modulation.
	 *
	 * Inputs: vm can point at either target namespace. Output: legacy nodes are
	 * restored through paramArray_setParameter() to preserve the old API, while
	 * descriptor-backed nodes write directly through their cached Parameter.
	 * modNode_resetTargets() and retargeting both use this so block-level
	 * overlays do not permanently replace the user's stored/base value.
	 */
	if (!vm) {
		return;
	}
	if (vm->destinationMode == MOD_NODE_DEST_LEGACY_PARAM_ARRAY) {
		paramArray_setParameter(vm->destination, vm->originalValue);
		return;
	}
	p = modNode_currentParameter(vm);
	modNode_writeParameterValue(p, vm->originalValue);
}

static void modNode_resetIdentity(ModulationNode* vm)
{
	/*
	 * Forget target identity without changing amount or lastVal.
	 *
	 * Inputs: vm is a source modulation node whose previous target has already
	 * been restored. Output: both target namespaces are cleared. This is private
	 * so public callers go through modNode_clearDestination(), which performs
	 * the restore first.
	 */
	if (!vm) {
		return;
	}
	vm->destination = 0u;
	vm->type = 0u;
	vm->originalValue.itg = 0u;
	vm->originalValue.flt = 0.f;
	vm->destinationMode = MOD_NODE_DEST_LEGACY_PARAM_ARRAY;
	vm->directParameter.ptr = 0;
	vm->directParameter.type = 0u;
	vm->waveInterpTarget = 0;
}

//-----------------------------------------------------------------------
void modNode_init(ModulationNode* vm)
{
	/*
	 * Initialize a modulation node in the off state.
	 *
	 * Inputs: vm is owned by a voice LFO or velocity modulator. Output:
	 * amount/lastVal are reset and both destination namespaces are empty. This
	 * does not call modNode_setDestination(0), because that function is the
	 * legacy ParameterArray API and should not be the generic "off" primitive
	 * for descriptor-backed nodes.
	 */
	if (!vm) {
		return;
	}
	modNode_resetIdentity(vm);
	vm->lastVal = 0.f;
	vm->amount = 0.f;
}
//-----------------------------------------------------------------------
static void modNode_setOriginalValueChanged(ModulationNode* vm, uint16_t idx)
{
	/*
	 * Refresh legacy target baselines after a ParameterArray-backed edit.
	 *
	 * Inputs: vm is one node and idx is a legacy ParameterArray id. Output:
	 * vm->originalValue is refreshed only when this legacy node targets idx.
	 * Descriptor-backed targets are handled by modNode_directOriginalValueChanged()
	 * because their destination ids live in a different namespace.
	 */
	if(vm &&
	   vm->destinationMode == MOD_NODE_DEST_LEGACY_PARAM_ARRAY &&
	   vm->destination == idx) {
		(void)modNode_captureOriginalValue(vm);
	}
}
//-----------------------------------------------------------------------
static void modNode_setDirectOriginalValueChanged(ModulationNode* vm, uint16_t destination)
{
	/*
	 * Refresh descriptor target baselines after an InstrumentManager edit.
	 *
	 * Inputs: vm is one node and destination is a canonical descriptor target
	 * id. Output: active direct nodes with the same id recapture the current
	 * runtime base value. This is the descriptor equivalent of
	 * modNode_setOriginalValueChanged(), kept separate so legacy ids and
	 * descriptor ids cannot be accidentally compared.
	 */
	if(vm &&
	   vm->destinationMode == MOD_NODE_DEST_DIRECT_PARAMETER &&
	   vm->destination == destination) {
		(void)modNode_captureOriginalValue(vm);
	}
}
//-----------------------------------------------------------------------
// This is called when a user changes a parameter value on the front. It saves
// the new value as originalValue. Since the value changes as modulation happens,
// we need to restore to the original value from time to time
void modNode_originalValueChanged(uint16_t idx)
{
	uint8_t i;
	for(i=0;i<6;i++)
	{
		modNode_setOriginalValueChanged(&velocityModulators[i],idx);
	}

	modNode_setOriginalValueChanged(&voiceArray[0].lfo.modTarget,idx);
	modNode_setOriginalValueChanged(&voiceArray[1].lfo.modTarget,idx);
	modNode_setOriginalValueChanged(&voiceArray[2].lfo.modTarget,idx);
	modNode_setOriginalValueChanged(&snareVoice.lfo.modTarget,idx);
	modNode_setOriginalValueChanged(&cymbalVoice.lfo.modTarget,idx);
	modNode_setOriginalValueChanged(&hatVoice.lfo.modTarget,idx);
}
//-----------------------------------------------------------------------
void modNode_directOriginalValueChanged(uint16_t destination)
{
	uint8_t i;

	/*
	 * Public descriptor-target original-value refresh.
	 *
	 * Inputs: destination is a canonical descriptor id supplied by
	 * InstrumentManager after it writes an ordinary instrument runtime value.
	 * Output: every velocity/LFO node using direct descriptor mode for that id
	 * updates its restore baseline. This is intentionally parallel to the
	 * legacy modNode_originalValueChanged() API rather than overloading it with
	 * a second id namespace.
	 */
	for(i=0;i<6;i++)
	{
		modNode_setDirectOriginalValueChanged(&velocityModulators[i],destination);
	}

	modNode_setDirectOriginalValueChanged(&voiceArray[0].lfo.modTarget,destination);
	modNode_setDirectOriginalValueChanged(&voiceArray[1].lfo.modTarget,destination);
	modNode_setDirectOriginalValueChanged(&voiceArray[2].lfo.modTarget,destination);
	modNode_setDirectOriginalValueChanged(&snareVoice.lfo.modTarget,destination);
	modNode_setDirectOriginalValueChanged(&cymbalVoice.lfo.modTarget,destination);
	modNode_setDirectOriginalValueChanged(&hatVoice.lfo.modTarget,destination);
}
//-----------------------------------------------------------------------
void modNode_resetTargets()
{
	modNode_waveInterpGeneration++;
	if (modNode_waveInterpGeneration == 0u) {
		modNode_waveInterpGeneration = 1u;
	}
	modNode_waveInterpActiveCount = 0u;

	uint8_t i;
	for(i=0;i<6;i++)
	{
		modNode_restoreTarget(&velocityModulators[i]);
	}

	modNode_restoreTarget(&voiceArray[0].lfo.modTarget);
	modNode_restoreTarget(&voiceArray[1].lfo.modTarget);
	modNode_restoreTarget(&voiceArray[2].lfo.modTarget);
	modNode_restoreTarget(&snareVoice.lfo.modTarget);
	modNode_restoreTarget(&cymbalVoice.lfo.modTarget);
	modNode_restoreTarget(&hatVoice.lfo.modTarget);


}
//-----------------------------------------------------------------------
void modNode_reassignVeloMod()
{
	uint8_t i;
	for(i=0;i<6;i++)
	{
		modNode_updateValue(&velocityModulators[i], velocityModulators[i].lastVal);
	}
}
//-----------------------------------------------------------------------
void modNode_clearDestination(ModulationNode* vm)
{
	/*
	 * Clear one node after restoring any active target.
	 *
	 * Inputs: vm is the source node. Output: target state is restored and both
	 * target namespaces are cleared, while amount/lastVal are preserved so a
	 * later retarget can keep the user's modulation depth. InstrumentManager
	 * uses this for descriptor off values; legacy callers should still use
	 * modNode_setDestination() when they truly have a ParameterArray id.
	 */
	if (!vm) {
		return;
	}
	modNode_restoreTarget(vm);
	modNode_resetIdentity(vm);
}
//-----------------------------------------------------------------------
// set a modulation destination to one of the sound parameters.
// This is called when the mod target changes or is initialized.
// The target's actual value needs to be preserved because it will be modulated.
void modNode_setDestination(ModulationNode* vm, uint16_t dest)
{
	/*
	 * Legacy ParameterArray destination installer.
	 *
	 * Inputs: dest is an old flat sound-parameter id. Output: vm targets the
	 * corresponding parameterArray[] entry, or is cleared when the id is off,
	 * out of range, or has no live pointer. Descriptor-backed ids must never be
	 * routed here; InstrumentManager installs those through
	 * modNode_setDirectDestination() after resolving a live runtime pointer.
	 */
	if (!vm) {
		return;
	}
	modNode_resetTargets();
	modNode_clearDestination(vm);
	if (dest >= END_OF_SOUND_PARAMETERS || !parameterArray[dest].ptr) {
		return;
	}

	vm->destinationMode = MOD_NODE_DEST_LEGACY_PARAM_ARRAY;
	vm->destination = dest;
	vm->type = parameterArray[dest].type;
	(void)modNode_captureOriginalValue(vm);
}
//-----------------------------------------------------------------------
uint8_t modNode_setDirectDestination(ModulationNode* vm,
									 uint16_t destination,
									 Parameter parameter,
									 void *waveInterpTarget)
{
	/*
	 * Descriptor runtime destination installer.
	 *
	 * Inputs: destination is a canonical descriptor id for identity matching,
	 * parameter is the live runtime pointer/type resolved by InstrumentManager,
	 * and waveInterpTarget is an optional OscInfo* for waveform interpolation.
	 * Output: nonzero on success; the old target is restored, the direct target
	 * is cached, and originalValue captures the current modulation baseline.
	 * This API exists so descriptor targets do not masquerade as legacy
	 * parameterArray[] ids.
	 */
	if (!vm || !parameter.ptr) {
		return 0u;
	}
	modNode_resetTargets();
	modNode_clearDestination(vm);
	vm->destinationMode = MOD_NODE_DEST_DIRECT_PARAMETER;
	vm->destination = destination;
	vm->type = parameter.type;
	vm->directParameter = parameter;
	vm->waveInterpTarget = waveInterpTarget;
	if (!modNode_captureOriginalValue(vm)) {
		modNode_resetIdentity(vm);
		return 0u;
	}
	return 1u;
}
//-----------------------------------------------------------------------
// This is called to actually modulate the value for a modulation node
void modNode_updateValue(ModulationNode* vm, float val)
{
	Parameter const *p = modNode_currentParameter(vm);

	if (vm) {
		vm->lastVal = val;
	}

	// --AS **PATROT avoid setting this if it's not set to something good
	if(!p || !p->ptr)
		return;

	switch(p->type)
	{
	case TYPE_UINT8:
	{
		uint8_t *dst = (uint8_t*)p->ptr;
		const float current = *dst;
		float modulated = current * vm->amount * val + (1.f - vm->amount) * current;

		if (modulated < 0.f) {
			modulated = 0.f;
		}

		if (modNode_waveInterpEnabled) {
			OscInfo *osc = (vm->destinationMode == MOD_NODE_DEST_DIRECT_PARAMETER)
				? (OscInfo *)vm->waveInterpTarget
				: modNode_getWaveTargetOsc(vm->destination);
			if (osc && modNode_waveInterpActiveCount < OSC_WAVE_INTERP_MAX_ACTIVE) {
				const uint8_t maxWave = modNode_getMaxWaveformIndex();
					if (modulated > maxWave) {
						modulated = maxWave;
					}

					uint8_t base = (uint8_t)modulated;
					if (base > maxWave) {
						base = maxWave;
					}
					float frac = modulated - (float)base;
					uint8_t next = base;
					if (base < maxWave) {
						next = (uint8_t)(base + 1u);
					} else {
						frac = 0.f;
					}

					*dst = base;
					osc->waveInterpNext = next;
					osc->waveInterpFrac = frac;
					osc->waveInterpGeneration = modNode_waveInterpGeneration;
					modNode_waveInterpActiveCount++;
					break;
				}
		}

		*dst = (uint8_t)modulated;
		break;
	}

	case TYPE_UINT32:
		(*((uint32_t*)p->ptr)) = (*((uint32_t*)p->ptr)) * vm->amount * val + (1.f-vm->amount) * (*((uint32_t*)p->ptr));
		break;

	case TYPE_FLT:
		(*((float*)p->ptr)) = (*((float*)p->ptr)) * vm->amount * val + (1.f-vm->amount) * (*((float*)p->ptr));
		break;

	case TYPE_SPECIAL_F:
		(*((float*)p->ptr)) = (*((float*)p->ptr)) * vm->amount * val + (1.f-vm->amount) * (*((float*)p->ptr));
		break;

	case TYPE_SPECIAL_P:
	case TYPE_SPECIAL_FILTER_F:
	default:
		break;

	}
}
