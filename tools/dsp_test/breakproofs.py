#!/usr/bin/env python3
"""Break proofs for the S085 oscillator host tests (C17.9; assessment A10).

What:       copies Core/, tools/ and config.h into a temporary directory and,
            for each listed break, applies one deliberate defect to a source
            file there, runs the host target that should catch it, and
            reports whether the target fails (FAILS = good: the test can
            fail) or passes (PASSES = the test is blind to that defect). The
            working tree is never modified.
Why:        a test counts only if it fails on a deliberately broken version
            (S085 rule, C17.9). Each break names the assessment item or
            decision it guards.
Inputs:     optional break IDs (B1 B7 ...) to run a subset.
Outputs:    one line per break; exit 1 if any break passes or does not apply.
Accessors:  make -C tools/dsp_test breakproofs.
Affiliates: test_osc_run.c, test_osc_s0.c, test_amp_retrigger.c (B27-B32,
            S085 P2), the Makefile targets they name.
"""
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, '..', '..'))
D = 'Core/DSPAudio/'
MULTI = {'B15'}          # breaks that apply to every occurrence

breaks = [
 ('B1 A1 slew divisor reverted', D+'Oscillator.c', '? left : (uint16_t)OUTPUT_DMA_SIZE;', '? left : (uint16_t)(left ? left : 1u);', 'osc_run'),
 ('B2 A4 itp blend is a no-op', D+'OscSection.c', '''		buf[i] = (int16_t)((float)oldBuf[i] +
		                   t * ((float)buf[i] - (float)oldBuf[i]));''', '		(void)t;', 'osc_run'),
 ('B3 D13 reset lands one sample early', D+'OscSyncLane.h', 'return lane->wrap[i] ? reset : phase;', 'return (i + 1u < 32u && lane->wrap[i + 1u]) ? reset : phase;', 'osc_run'),
 ('B4 Q10 snc mix law swapped', D+'OscSection.c', '''		buf[i] = (int16_t)__SSAT((int32_t)((float)buf[i] * (1.0f - amount) +
		                                   (float)modBuf[i] * amount), 16);''', '''		buf[i] = (int16_t)__SSAT((int32_t)((float)buf[i] * amount +
		                                   (float)modBuf[i] * (1.0f - amount)), 16);''', 'osc_run'),
 ('B5 rng law shift', D+'OscSection.c', 'const int32_t ring = ((int32_t)out[i] * (int32_t)modBuf[i]) >> 15;', 'const int32_t ring = ((int32_t)out[i] * (int32_t)modBuf[i]) >> 14;', 'osc_run'),
 ('B6 F5 shadow ignores sample start', D+'OscSection.c', 'uint32_t phase = (mod->phase > base) ? (mod->phase - base) : 0u;', 'uint32_t phase = mod->phase; (void)base;', 'osc_run'),
 ('B7 D6 finished master still resets', D+'OscSection.c', 'const uint8_t live = (uint8_t)!oscSample_isFinished(mod);', 'const uint8_t live = 1u;', 'osc_run'),
 ('B8 itp copy uses the carrier ratio', D+'OscSection.c', '? (float)oldCar->phaseInc / (float)mod->phaseInc : 0.0f;', '? (float)car->phaseInc / (float)mod->phaseInc : 0.0f;', 'osc_run'),
 ('B9 smp/slp sync base 0', D+'OscillatorSample.c', '''			phase = oscSync_phase(lane, i, phase,
			                      start << OSC_SAMPLE_PHASE_SHIFT);''', '			phase = oscSync_phase(lane, i, phase, 0u);', 'osc_run'),
 ('B10 pool: pending ignores the pool', D+'OscSection.c', '''	if (oscRenderPool_claim(1u)) {
		s->itpState = OSC_ITP_ACTIVE;''', '''	if (oscRenderPool_claim(0u)) {
		s->itpState = OSC_ITP_ACTIVE;''', 'osc_run'),
 ('B11 smp end not silent', D+'OscillatorSample.c', '(done ? 0.0f : gain)', 'gain', 'osc_sample'),
 ('B12 slp seam reads the last frame twice', D+'OscillatorSample.c', '(looped ? start : index)', 'index', 'osc_sample'),
 ('B13 gain code ignored', D+'OscillatorSample.c', 'gain *= sampleMemory_gainFromCode(osc->sampleGainCode);', '(void)0;', 'osc_sample'),
 ('B14 clamp overwrites the request', D+'OscillatorSample.c', '''	osc->waveform = index;
	if (!osc->sampleCacheData''', '''	osc->waveform = index;
	osc->waveRequest = index;
	if (!osc->sampleCacheData''', 'osc_sample'),
 ('B15 wtb nx bound removed', D+'OscillatorWavetable.c', 'const uint32_t nx = (idx + 1u == w->frame_len) ? 0u : idx + 1u;', 'const uint32_t nx = idx + 1u;', 'wtb'),
 ('B16 wtb 8-bit shift', D+'OscillatorWavetable.c', 'const float a = (float)((int32_t)A[idx] << 8);', 'const float a = (float)((int32_t)A[idx] << 7);', 'wtb'),
 ('B17 wtb phase not written back', D+'OscillatorWavetable.c', '''		}
	}
	osc->phase = phase;
	osc->phaseInc = inc;
}''', '''		}
	}
	osc->phaseInc = inc;
}''', 'wtb'),
 ('B18 pwm F3 fraction 1/2^32', D+'OscillatorPwm.c', 'const float frac = (float)(phase & PWM_PHASE_MASK) * 2.384185791015625e-07f;', 'const float frac = (float)(phase & PWM_PHASE_MASK) * 2.3283064365386963e-10f;', 'osc_pwm'),
 ('B19 prc polarity inverted', D+'OscillatorPwm.c', '''	return __SSAT((int32_t)((float)(pwm_read1024(tab, phase) -
	                                pwm_read1024(tab, phase + shift)) * scale), 16);''', '''	return __SSAT((int32_t)((float)(pwm_read1024(tab, phase + shift) -
	                                pwm_read1024(tab, phase)) * scale), 16);''', 'osc_pwm'),
 ('B20 HiHat carrier gain ignored', D+'OscSection.c', 'calcNextOscSampleFmBlock(car, modBuf, buf, size, carGain);', 'calcNextOscSampleFmBlock(car, modBuf, buf, size, 1.0f);', 'osc_s0'),
 ('B21 noise value halved', D+'Oscillator.c', 'osc->output = (int16_t)(GetRngValue()); //normal pitched white noise\n\t\t}\n\t\tif (lane)', 'osc->output = (int16_t)(GetRngValue() >> 1);\n\t\t}\n\t\tif (lane)', 'osc_s0'),
 ('B22 table level capped at 9', D+'Oscillator.c', 'osc->tableOffset = overtoneIndex>10?10:overtoneIndex;', 'osc->tableOffset = overtoneIndex>9?9:overtoneIndex;', 'osc_s0'),
 ('B24 natural order: digit runs by length off', 'Core/Hardware/SD/storageTypes.c', '            if (la != lb)\n                return (int8_t)((la < lb) ? -1 : 1);', '            (void)lb;', 'natural_order'),
 ('B25 catalogue: gain code from peak off', 'Core/SampleRom/SampleInstall.c', 'entry.gain_code = sampleInstall_gainCode(sample_install.peak);', 'entry.gain_code = 0u;', 'catalogue'),
 ('B26 stock waves: level scale dropped', 'Core/SampleRom/StockWaveGen.c', 'sum *= stockWaveLevelScale[wave][level];', '(void)0;', 'stock_gen'),
 ('B33 F-A fm ramp ignores its start', D+'OscSection.c', 'const float span = 1.0f - from;', 'const float span = 0.0f; (void)from;', 'osc_run'),
 ('B34 F-A fm ramp not forced flat under itp', D+'OscSection.c', """	if (oldCar)
		fmRampFrom = 1.0f;""", """	(void)0;""", 'osc_run'),
 ('B35 F-A fm ramp runs the wrong way', D+'OscSection.c', 'const float g = from + ((float)i * inv) * span;', 'const float g = 1.0f - ((float)i * inv) * span;', 'osc_run'),
 ('B23 drum mix carrier gain', D+'OscSection.c', '		calcNextOscSampleBlock(car, buf, size, (1.0f - amount));', '		calcNextOscSampleBlock(car, buf, size, amount);', 'osc_s0'),
 # S085 P2 (C7.5): amp retrigger and the post-chain ramps.
 ('B27 retrigger inverts with slope', D+'SlopeEg2.c', 'v = gainNow / (1.0f + eg->invSlope * (1.0f - gainNow));', 'v = gainNow / (1.0f + eg->slope * (1.0f - gainNow));', 'ampretrig'),
 ('B28 retrigger keeps the raw value', D+'SlopeEg2.c', '	eg->value = v;\n	eg->state = EG_A;', '	(void)v;\n	eg->state = EG_A;', 'ampretrig'),
 ('B29 addGainDist ignores lastAmpGain', D+'voicePostChain.h', '''		const int16_t s = bufferTool_satAdd16(buf[j], add[j]);
		const int16_t g = bufferTool_floatToInt16Store(
			s * bufferTool_interpolatedGain(j, inv_size, ampGain,
			                                lastAmpGain));''', '''		const int16_t s = bufferTool_satAdd16(buf[j], add[j]);
		const int16_t g = bufferTool_floatToInt16Store(
			s * bufferTool_interpolatedGain(j, inv_size, ampGain,
			                                ampGain));''', 'ampretrig'),
 ('B30 drum velocity ramp swapped', D+'voicePostChain.h', '''			eg * bufferTool_interpolatedGain(i, inv_size, veloGain,
			                                 lastVeloGain));''', '''			eg * bufferTool_interpolatedGain(i, inv_size, lastVeloGain,
			                                 veloGain));''', 'ampretrig'),
 ('B31 audible threshold 0.01', D+'SlopeEg2.h', '#define SLOPEEG2_AUDIBLE_GAIN (1.0f / 32768.0f)', '#define SLOPEEG2_AUDIBLE_GAIN (0.01f)', 'ampretrig'),
 # B32: postchain stays at 0 differing (flat calls; checked by hand, §11
 # of the schedule); ampretrig must catch it.
 ('B32 mixAddGainDist ramps the wrong way', D+'voicePostChain.h', '''		const int16_t s = bufferTool_satAdd16(m, add[j]);
		const int16_t g = bufferTool_floatToInt16Store(
			s * bufferTool_interpolatedGain(j, inv_size, ampGain,
			                                lastAmpGain));''', '''		const int16_t s = bufferTool_satAdd16(m, add[j]);
		const int16_t g = bufferTool_floatToInt16Store(
			s * bufferTool_interpolatedGain(j, inv_size, lastAmpGain,
			                                ampGain));''', 'ampretrig'),
]


def main():
    only = sys.argv[1:]
    root = tempfile.mkdtemp(prefix='s085_breaks_')
    bad = 0
    try:
        for part in ('Core', 'tools'):
            shutil.copytree(os.path.join(REPO, part), os.path.join(root, part),
                            ignore=shutil.ignore_patterns('build', '__pycache__'))
        shutil.copy(os.path.join(REPO, 'config.h'), root)
        tdir = os.path.join(root, 'tools', 'dsp_test')
        for name, f, old, new, target in breaks:
            if only and name.split()[0] not in only:
                continue
            p = os.path.join(root, f)
            src = open(p).read()
            n = src.count(old)
            if n == 0 or (n != 1 and name.split()[0] not in MULTI):
                print(f'{name}: pattern found x{n}, not applied')
                bad += 1
                continue
            open(p, 'w').write(src.replace(old, new))
            try:
                r = subprocess.run(['make', '-s', target], cwd=tdir,
                                   capture_output=True, text=True)
                fails = [l for l in (r.stdout + r.stderr).splitlines()
                         if l.startswith('FAIL')]
                if r.returncode != 0 and fails:
                    status = 'FAILS (good)'
                else:
                    status = 'BUILD ERROR' if r.returncode else 'PASSES (blind)'
                    bad += 1
                print(f'{name} [{target}]: {status}'
                      + ('; ' + fails[0] if fails else ''))
            finally:
                open(p, 'w').write(src)
    finally:
        shutil.rmtree(root, ignore_errors=True)
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
