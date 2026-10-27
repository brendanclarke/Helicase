# S073 golden harness and ARM codegen check (Step 0).
#
# What:       builds host old-vs-new tests and ARM loop checks.
# Why:        the plan permits only the existing cc and arm-none-eabi tools.
# Inputs:     frozen snapshot, current sources, extract.py and test files.
# Outputs:    generated fragments and binaries under build/.
# Accessors:  make -C tools/dsp_test <target>.
# Affiliates: fpseq.py, armcheck/, and the per-step test programs.
ROOT   := ../..
FROZEN := frozen
GEN    := build/gen
HOSTCC ?= cc
HOSTFLAGS := -O2 -std=gnu11 -Wall -Wno-unused-function -fno-vectorize -fno-slp-vectorize \
             -include prelude.h -I$(GEN)
ARMCC  := arm-none-eabi-gcc
ARMFLAGS := -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -Ofast -std=gnu11 \
            -fdata-sections -ffunction-sections -I$(ROOT) -I$(ROOT)/Core \
            -I$(ROOT)/Core/DSPAudio -I$(ROOT)/Core/compat -I$(GEN) \
            -I$(ROOT)/Core/MIDI -I$(ROOT)/Core/SampleRom \
            -I$(ROOT)/Core/Bank/Scene/Preset -I$(ROOT)/Core/DSP/Instruments
EX := python3 extract.py
FP := python3 fpseq.py

FILTER_SYMS := fastTanh fastTan tanhXdX softClipTwo SVF_setReso \
               SVF_directSetFilterValue SVF_init SVF_reset SVF_recalcFreq \
               SVF_setDrive SVF_calcBlockZDF SVF_calcBlockZDFFloat
NEW_FILTER_SYMS := svf_padeNum svf_padeDen $(FILTER_SYMS)
fn = $(foreach s,$(1),--func $(s))
rn = $(foreach s,$(2),--rename $(s)=$(1)$(s))

.PHONY: selftest filter special_tags pack postchain octave mixer \
        natural_order catalogue stock_gen osc_run osc_pwm osc_sample wtb osc_s0 \
        ampretrig \
        armcheck-osc-base armcheck-osc \
        armcheck-filter armcheck-postchain armcheck-mixer breakproofs clean
$(GEN):
	mkdir -p $(GEN)

# S085 host harness (C17.1). The real oscillator/section sources are compiled
# natively; hostcompat shadows the ARM-only CMSIS header and prelude_osc.h
# supplies placement macros and __SSAT.
OSCINC := -Ihostcompat -I. -I$(ROOT) -I$(ROOT)/Core \
          -I$(ROOT)/Core/DSPAudio -I$(ROOT)/Core/SampleRom \
          -I$(ROOT)/Core/compat -I$(ROOT)/Core/MIDI \
          -I$(ROOT)/Core/Bank/Scene/Preset -I$(ROOT)/Core/DSP/Instruments
OSCFLAGS := -O2 -std=gnu11 -Wall -Wno-unused-function -Wno-comment \
            -include prelude_osc.h $(OSCINC)
OSCSRCS := host_osc_support.c $(ROOT)/Core/DSPAudio/Oscillator.c \
           $(ROOT)/Core/DSPAudio/OscillatorPwm.c \
           $(ROOT)/Core/DSPAudio/OscillatorSample.c \
           $(ROOT)/Core/DSPAudio/OscillatorWavetable.c \
           $(ROOT)/Core/DSPAudio/OscRenderPool.c \
           $(ROOT)/Core/DSPAudio/OscSection.c \
           $(ROOT)/Core/DSPAudio/StockWaves.c \
           $(ROOT)/Core/DSPAudio/wavetable.c \
           $(ROOT)/Core/DSPAudio/Samples.c

# C17.3: natural order, catalogue v2 and stock generation.
natural_order: | $(GEN)
	$(EX) --src $(ROOT)/Core/Hardware/SD/storageTypes.c \
	  --func storage_naturalCompare --func storage_stripOrderPrefix \
	  --out $(GEN)/nat.c
	$(HOSTCC) -O2 -std=gnu11 -Wall -include prelude.h -I. test_natural_order.c \
	  $(GEN)/nat.c -o build/natural_order
	./build/natural_order

catalogue: | $(GEN)
	$(HOSTCC) -O2 -std=gnu11 -Wall -Wno-comment -include prelude_cat.h \
	  -Ihostcompat -I$(ROOT) -I$(ROOT)/Core \
	  -I$(ROOT)/Core/DSPAudio -I$(ROOT)/Core/SampleRom \
	  -I$(ROOT)/Core/compat -I. \
	  test_catalogue.c host_sampleflash_stub.c \
	  $(ROOT)/Core/SampleRom/SampleMemory.c \
	  $(ROOT)/Core/SampleRom/SampleInstall.c -o build/catalogue -lm
	./build/catalogue

stock_gen: | $(GEN)
	$(EX) --src $(ROOT)/Core/SampleRom/StockWaveGen.c \
	  --func stockWaveGen_point --rename stockWaveGen_point=host_stockWaveGen_point \
	  --rename static= --out $(GEN)/stock_gen.c
	$(EX) --src $(ROOT)/Core/SampleRom/StockWaveGen.c \
	  --object stock_gen_length --object stock_gen_harmonics \
	  --out $(GEN)/stock_geo.c
	$(EX) --src $(ROOT)/Core/SampleRom/StockWaveRecipes.c \
	  --object stockWaveRecipeA --object stockWaveRecipeB \
	  --object stockWaveLevelScale --out $(GEN)/stock_rec.c
	$(EX) --src $(ROOT)/Core/DSPAudio/wavetable.c --object sine_table \
	  --out $(GEN)/stock_sine.c
	$(HOSTCC) -O2 -std=gnu11 -Wall -Wno-comment -include prelude.h \
	  -include $(ROOT)/Core/DSPAudio/StockWaves.h \
	  -include $(ROOT)/Core/DSPAudio/wavetable.h \
	  -Ihostcompat -I$(GEN) -I. \
	  -I$(ROOT) -I$(ROOT)/Core -I$(ROOT)/Core/DSPAudio \
	  test_stock_gen.c stock_all.c -o build/stock_gen -lm
	./build/stock_gen

# C17.6: the real oscillator layers.
# S085 P2: gain-matched amp retrigger and the post-chain ramps.
ampretrig: | $(GEN)
	$(HOSTCC) $(OSCFLAGS) test_amp_retrigger.c \
	  $(ROOT)/Core/DSPAudio/SlopeEg2.c $(ROOT)/Core/DSPAudio/distortion.c \
	  -o build/ampretrig -lm
	./build/ampretrig

osc_run: | $(GEN)
	$(HOSTCC) $(OSCFLAGS) test_osc_run.c $(OSCSRCS) \
	  -o build/osc_run -lm
	./build/osc_run all

osc_pwm: osc_run
	./build/osc_run pwm

osc_sample: osc_run
	./build/osc_run sample

wtb: osc_run
	./build/osc_run wtb

# C17.5: bit-identical rst and the old fm/mix/FM2 compositions.
# Assessment A7: also the frozen noise loop, the frozen osc_setFreq() (a
# pitch sweep), and the verbatim frozen85 engine oscillator lines (Drum
# 313-326, Cymbal 240-247, HiHat 257-264) wrapped by s0_engines.c.
S0_RENAME_DISPATCH := --rename calcNextOscSampleBlock=old_dispatch \
	  --rename calcNextOscSampleFmBlock=old_dispatchFm
osc_s0: | $(GEN)
	$(EX) --src $(FROZEN)85/Core/DSPAudio/Oscillator.c \
	  --func calcSineBlock --func calcFmSineBlock \
	  --func calcWavetableOscBlock --func calcFmBlock \
	  --func calcSampleOscBlock --func calcSampleOscFmBlock \
	  --func calcNoiseBlock \
	  --rename calcSineBlock=old_calcSineBlock \
	  --rename calcFmSineBlock=old_calcFmSineBlock \
	  --rename calcWavetableOscBlock=old_calcWavetableOscBlock \
	  --rename calcFmBlock=old_calcFmBlock \
	  --rename calcSampleOscBlock=old_calcSampleOscBlock \
	  --rename calcSampleOscFmBlock=old_calcSampleOscFmBlock \
	  --rename calcNoiseBlock=old_calcNoiseBlock \
	  --out $(GEN)/s0_old.c
	$(EX) --src $(FROZEN)85/Core/DSPAudio/Oscillator.c \
	  --object osc_octaveEdgeHz --func freqToTableIndex \
	  --func freq2PhaseIncr --func freq2PhaseIncr1024 --func freq2PhaseIncr32767 \
	  --func osc_calcSineFreqValue --func osc_calcNoiseFreqValue \
	  --func osc_calcWavetableFreqValue --func osc_calcSampleFreqValue \
	  --func osc_calcUserSampleFreqValue --func osc_setFreq \
	  --rename osc_octaveEdgeHz=old_osc_octaveEdgeHz \
	  --rename freqToTableIndex=old_freqToTableIndex \
	  --rename freq2PhaseIncr=old_freq2PhaseIncr \
	  --rename freq2PhaseIncr1024=old_freq2PhaseIncr1024 \
	  --rename freq2PhaseIncr32767=old_freq2PhaseIncr32767 \
	  --rename osc_calcSineFreqValue=old_osc_calcSineFreqValue \
	  --rename osc_calcNoiseFreqValue=old_osc_calcNoiseFreqValue \
	  --rename osc_calcWavetableFreqValue=old_osc_calcWavetableFreqValue \
	  --rename osc_calcSampleFreqValue=old_osc_calcSampleFreqValue \
	  --rename osc_calcUserSampleFreqValue=old_osc_calcUserSampleFreqValue \
	  --rename osc_setFreq=old_osc_setFreq \
	  --out $(GEN)/s0_old_freq.c
	$(EX) --src $(FROZEN)85/Core/DSP/Instruments/Drum/DrumVoice.c \
	  --lines 313-326 $(S0_RENAME_DISPATCH) --out $(GEN)/s0_drum_lines.c
	$(EX) --src $(FROZEN)85/Core/DSP/Instruments/Cymbal/CymbalVoice.c \
	  --lines 240-247 $(S0_RENAME_DISPATCH) --out $(GEN)/s0_cym_lines.c
	$(EX) --src $(FROZEN)85/Core/DSP/Instruments/HiHat/HiHat.c \
	  --lines 257-264 $(S0_RENAME_DISPATCH) --out $(GEN)/s0_hat_lines.c
	$(HOSTCC) $(OSCFLAGS) -I$(GEN) -include $(ROOT)/Core/DSPAudio/Oscillator.h \
	  -include $(ROOT)/Core/DSPAudio/Samples.h -include $(ROOT)/Core/DSPAudio/random.h \
	  test_osc_s0.c s0_engines.c $(GEN)/s0_old.c $(GEN)/s0_old_freq.c \
	  $(OSCSRCS) -o build/osc_s0 -lm
	./build/osc_s0

# C17.4: ARM per-sample loop report (assessment A3). Each probe wrapper fixes
# its block constants so its innermost loop is one case's per-sample body;
# osccost.py prints the static instruction count of every innermost loop.
# armcheck-osc-base prices the frozen pre-S085 loops on the same compiler
# flags; armcheck-osc prices the S085 loops and the probe wrappers. The
# figures are recorded in S085_P1_IMPLEMENTATION.md C19.2.
COST := python3 osccost.py
PROBE_FLAGS := $(ARMFLAGS) -I$(ROOT)/Core/DSPAudio -I$(ROOT)/Core/SampleRom

armcheck-osc-base: | $(GEN)
	$(ARMCC) $(ARMFLAGS) -include armcheck/osc_base_compat.h \
	  -c $(FROZEN)85/Core/DSPAudio/Oscillator.c \
	  -o build/osc_base.o
	$(COST) --obj build/osc_base.o --func calcSineBlock --func calcFmSineBlock \
	  --func calcWavetableOscBlock --func calcFmBlock --func calcNoiseBlock \
	  --func calcSampleOscBlock --func calcSampleOscFmBlock

armcheck-osc: | $(GEN)
	$(ARMCC) $(ARMFLAGS) -c $(ROOT)/Core/DSPAudio/Oscillator.c -o build/osc_new.o
	$(ARMCC) $(PROBE_FLAGS) -c armcheck/probe_wav.c -o build/probe_wav.o
	$(ARMCC) $(PROBE_FLAGS) -c armcheck/probe_pwm.c -o build/probe_pwm.o
	$(ARMCC) $(PROBE_FLAGS) -c armcheck/probe_smp.c -o build/probe_smp.o
	$(ARMCC) $(PROBE_FLAGS) -c armcheck/probe_wtb.c -o build/probe_wtb.o
	$(ARMCC) $(PROBE_FLAGS) -c armcheck/probe_section.c -o build/probe_section.o
	$(COST) --obj build/osc_new.o --func calcSineBlock --func calcFmSineBlock \
	  --func calcWavetableOscBlock --func calcFmBlock --func calcNoiseBlock \
	  --func calcSampleOscBlock --func calcSampleOscFmBlock \
	  --func calcStockWaveBlock --func calcStockWaveFmBlock
	$(COST) --obj build/probe_wav.o --func p_table_sync --func p_sine_sync \
	  --func p_noise_sync --func p_crash_sync --func p_stock_sync
	$(COST) --obj build/probe_pwm.o \
	  $(foreach f,prc psw ptr pq3 pw3 swp0 swp1 swp2 swp3,--func p_pwm_$(f) \
	  --func p_pwm_$(f)_fm --func p_pwm_$(f)_lane)
	$(COST) --obj build/probe_smp.o --func p_smp --func p_smp_fm --func p_smp_lane \
	  --func p_slp --func p_slp_fm --func p_slp_lane
	$(COST) --obj build/probe_wtb.o --func p_wtb --func p_wtb_fm --func p_wtb_lane
	$(COST) --obj build/probe_section.o --func p_lane_table --func p_lane_sample \
	  --func oscSection_renderSync --func p_rng --func p_blend --func p_fm_ramp

# Step 0 gate: exact frozen source against itself.
selftest: | $(GEN)
	$(EX) --src $(FROZEN)/Core/DSPAudio/ResonantFilter.c $(call fn,$(FILTER_SYMS)) \
	  $(call rn,A_,$(FILTER_SYMS)) --out $(GEN)/filter_a.c
	$(EX) --src $(FROZEN)/Core/DSPAudio/ResonantFilter.c $(call fn,$(FILTER_SYMS)) \
	  $(call rn,B_,$(FILTER_SYMS)) --out $(GEN)/filter_b.c
	$(HOSTCC) $(HOSTFLAGS) -c $(GEN)/filter_a.c -o build/filter_a.o
	$(HOSTCC) $(HOSTFLAGS) -c $(GEN)/filter_b.c -o build/filter_b.o
	$(HOSTCC) $(HOSTFLAGS) test_filter.c build/filter_a.o build/filter_b.o -o build/selftest -lm
	./build/selftest --expect-identical

clean:
	rm -rf build

# Step 2 host proof: tagged rows equal the pre-S073 key classifier.
special_tags:
	cd $(ROOT) && python3 tools/dsp_test/check_special_tags.py

# Step 3a gate: old and word-store packers must produce identical bytes.
pack: | $(GEN)
	$(EX) --src $(FROZEN)/Core/Hardware/AudioCodecManager.c \
	  --func sampleMix_toS24 --func pack_half \
	  --rename sampleMix_toS24=old_sampleMix_toS24 --rename pack_half=old_pack_half \
	  --out $(GEN)/pack_old.c
	$(EX) --src $(ROOT)/Core/Hardware/AudioCodecManager.c \
	  --func sampleMix_toS24 --func pack_frameWord --func pack_half \
	  --rename sampleMix_toS24=new_sampleMix_toS24 --rename pack_frameWord=new_pack_frameWord \
	  --rename pack_half=new_pack_half --out $(GEN)/pack_new.c
	$(HOSTCC) $(HOSTFLAGS) test_pack.c -o build/pack -lm
	./build/pack

# Step 4 host gate: frozen multi-pass voice chains equal fused helpers.
postchain: | $(GEN)
	$(EX) --src $(FROZEN)/Core/DSPAudio/BufferTools.h --func bufferTool_satAdd16 \
	  --out $(GEN)/pc_sat.c
	$(EX) --src $(FROZEN)/Core/DSPAudio/BufferTools.c \
	  --func bufferTool_addGainInterpolated --func bufferTool_addGain \
	  --rename bufferTool_addGainInterpolated=old_addGainInterpolated \
	  --rename bufferTool_addGain=old_addGain --out $(GEN)/pc_old_bt.c
	$(EX) --src $(FROZEN)/Core/DSPAudio/distortion.c --func calcDistBlock \
	  --rename calcDistBlock=old_calcDistBlock --out $(GEN)/pc_old_dist.c
	$(EX) --src $(FROZEN)/Core/DSP/Instruments/Snare/Snare.c --lines 239-262 \
	  --rename calcDistBlock=old_calcDistBlock --out $(GEN)/pc_old_snare_body.inc
	$(EX) --src $(FROZEN)/Core/DSP/Instruments/Cymbal/CymbalVoice.c --lines 252-272 \
	  --rename calcDistBlock=old_calcDistBlock --out $(GEN)/pc_old_cym_body.inc
	$(EX) --src $(FROZEN)/Core/DSP/Instruments/HiHat/HiHat.c --lines 270-291 \
	  --rename calcDistBlock=old_calcDistBlock --out $(GEN)/pc_old_hat_body.inc
	$(EX) --src $(ROOT)/Core/DSPAudio/BufferTools.h \
	  --func bufferTool_floatToInt16Store --func bufferTool_interpolatedGain \
	  --out $(GEN)/pc_new_bt.c
	$(EX) --src $(ROOT)/Core/DSPAudio/distortion.h \
	  --func distortion_curveSample16 --out $(GEN)/pc_new_dist.c
	$(EX) --src $(ROOT)/Core/DSPAudio/voicePostChain.h \
	  --func voicePost_drum --func voicePost_mixAddGainDist --func voicePost_addGainDist \
	  --out $(GEN)/pc_new_chain.c
	$(HOSTCC) $(HOSTFLAGS) test_postchain.c -o build/postchain -lm
	./build/postchain

# Step 4 ARM gate: compare frozen pass loops with fused target codegen.
armcheck-postchain: postchain
	$(EX) --src $(FROZEN)/Core/DSPAudio/BufferTools.c \
	  --func bufferTool_addGainInterpolated --func bufferTool_addGain \
	  --rename bufferTool_addGainInterpolated=old_addGainInterpolated \
	  --rename bufferTool_addGain=old_addGain --rename static= \
	  --out $(GEN)/pc_old_bt_arm.c
	$(EX) --src $(FROZEN)/Core/DSPAudio/distortion.c --func calcDistBlock \
	  --rename calcDistBlock=old_calcDistBlock --out $(GEN)/pc_old_dist_arm.c
	$(ARMCC) $(ARMFLAGS) -c armcheck/postchain.c -o build/ac_postchain.o
	$(FP) --obj build/ac_postchain.o --ref old_addGainInterpolated \
	  --ref old_addGain --ref old_calcDistBlock --new new_drum --report
	$(FP) --obj build/ac_postchain.o --ref ref_snare:0 --ref old_calcDistBlock \
	  --new new_snare --report
	$(FP) --obj build/ac_postchain.o --ref ref_cymbal:0 --ref old_calcDistBlock \
	  --new new_addgain --report
	# S085 P2 (C7.3): the ramps add value operations by design, so the three
	# runs above are reports; this is the per-sample cost (35 / 35 / 30).
	python3 osccost.py --obj build/ac_postchain.o --func new_drum \
	  --func new_snare --func new_addgain --func ramp_drum \
	  --func ramp_snare --func ramp_addgain

# Step 1 gate: rounding baseline, then frozen (A) against the current batched
# solver (B) over the full S1 grid and both I/O twins.
filter: selftest | $(GEN)
	$(HOSTCC) $(HOSTFLAGS) -ffp-contract=off -c $(GEN)/filter_b.c \
	  -o build/filter_b_strict.o
	$(HOSTCC) $(HOSTFLAGS) test_filter.c build/filter_a.o build/filter_b_strict.o \
	  -o build/filter_baseline -lm
	./build/filter_baseline --baseline-out build/baseline_div.txt
	$(EX) --src $(ROOT)/Core/DSPAudio/ResonantFilter.c $(call fn,$(NEW_FILTER_SYMS)) \
	  $(call rn,B_,$(NEW_FILTER_SYMS)) --out $(GEN)/filter_b_new.c
	$(HOSTCC) $(HOSTFLAGS) -c $(GEN)/filter_b_new.c -o build/filter_b_new.o
	$(HOSTCC) $(HOSTFLAGS) test_filter.c build/filter_a.o build/filter_b_new.o -o build/filter -lm
	./build/filter --s1-report build/baseline_div.txt

# Step 1 report: target division count before and after the algebraic batch.
armcheck-filter:
	$(ARMCC) $(ARMFLAGS) -c $(FROZEN)/Core/DSPAudio/ResonantFilter.c -o build/rf_old.o
	$(ARMCC) $(ARMFLAGS) -c $(ROOT)/Core/DSPAudio/ResonantFilter.c -o build/rf_new.o
	@echo "vdiv old: $$(arm-none-eabi-objdump -d build/rf_old.o | grep -c vdiv)"
	@echo "vdiv new: $$(arm-none-eabi-objdump -d build/rf_new.o | grep -c vdiv)"

# Step 6 host gate: the edge-count wavetable selector is good enough at all
# changed inputs; see test_octave.c for the full float-domain report.
octave: | $(GEN)
	$(EX) --src $(FROZEN)/Core/DSPAudio/Oscillator.c --func freqToTableIndex \
	  --rename freqToTableIndex=old_freqToTableIndex --out $(GEN)/oct_old.c
	$(EX) --src $(ROOT)/Core/DSPAudio/Oscillator.c --object osc_octaveEdgeHz \
	  --func freqToTableIndex --rename osc_octaveEdgeHz=new_osc_octaveEdgeHz \
	  --rename freqToTableIndex=new_freqToTableIndex --out $(GEN)/oct_new.c
	$(HOSTCC) $(HOSTFLAGS) test_octave.c -o build/octave -lm
	./build/octave

# Step 5 host gate: dry + send old versus combined, bit-identical.
mixer: | $(GEN)
	$(EX) --src $(FROZEN)/Core/DSPAudio/BufferTools.h --func bufferTool_satAdd32 \
	  --out $(GEN)/mx_sat.c
	$(EX) --src $(FROZEN)/Core/DSPAudio/squareRootLut.c --object squareRootLut \
	  --out $(GEN)/mx_lut.c
	$(EX) --src $(FROZEN)/Core/DSPAudio/mixer.c --func mixer_addVoiceInt16ToOutput \
	  --func mixer_addVoiceToFxBus --rename mixer_addVoiceInt16ToOutput=old_dry \
	  --rename mixer_addVoiceToFxBus=old_send --rename mixer_fx_bus=old_bus \
	  --out $(GEN)/mx_old.c
	$(EX) --src $(ROOT)/Core/DSPAudio/mixer.c --func mixer_addVoiceInt16ToOutputAndFx \
	  --rename mixer_addVoiceInt16ToOutputAndFx=new_combined \
	  --rename mixer_fx_bus=new_bus --out $(GEN)/mx_new.c
	$(HOSTCC) $(HOSTFLAGS) test_mixer.c -o build/mixer -lm
	./build/mixer

# Step 5 ARM gate: one loop of each dry x send combination, plus the default
# (send-only) case, and the D-5 instruction counts for each combination.
# Loop indices were chosen from their printed multisets (fpseq.py prints one
# line per backward branch; loop rotation gives several per case):
#   old_dry:1 single-output routing   old_dry:4  stereo routing
#   old_send:0 mono-input send        old_send:3 stereo-input send
#   new_combined:0 / 22 / 2 / 27      the four combinations, in that order
#   new_combined:87 / 12              default case, mono / stereo send
# A code change that moves a loop makes its line DIFF rather than pass.
# The combined loop converts the input sample and the loop index once, where
# the old dry and send loops each converted them (see fpseq.py --shared-op).
MX_SHARED = --shared-op vcvt.f32.s32 --shared-op vcvt.f32.u32
armcheck-mixer: mixer
	$(EX) --src $(FROZEN)/Core/DSPAudio/mixer.c --func mixer_addVoiceInt16ToOutput \
	  --func mixer_addVoiceToFxBus --rename mixer_addVoiceInt16ToOutput=old_dry \
	  --rename mixer_addVoiceToFxBus=old_send --rename mixer_fx_bus=old_bus \
	  --rename static= --out $(GEN)/mx_old_arm.c
	$(EX) --src $(ROOT)/Core/DSPAudio/mixer.c --func mixer_addVoiceInt16ToOutputAndFx \
	  --rename mixer_addVoiceInt16ToOutputAndFx=new_combined \
	  --rename mixer_fx_bus=new_bus --rename static= --out $(GEN)/mx_new_arm.c
	$(ARMCC) $(ARMFLAGS) -c armcheck/mixer.c -o build/ac_mixer.o
	$(FP) --obj build/ac_mixer.o --ref old_dry:1 --ref old_send:0 \
	  --new new_combined:0 $(MX_SHARED)
	$(FP) --obj build/ac_mixer.o --ref old_dry:1 --ref old_send:3 \
	  --new new_combined:22 $(MX_SHARED)
	$(FP) --obj build/ac_mixer.o --ref old_dry:4 --ref old_send:0 \
	  --new new_combined:2 $(MX_SHARED)
	$(FP) --obj build/ac_mixer.o --ref old_dry:4 --ref old_send:3 \
	  --new new_combined:27 $(MX_SHARED)
	$(FP) --obj build/ac_mixer.o --ref old_send:0 --new new_combined:87
	$(FP) --obj build/ac_mixer.o --ref old_send:3 --new new_combined:12
	$(FP) --obj build/ac_mixer.o --ref old_dry:1 --ref old_send:0 --new new_combined:0 \
	  --report --all
	$(FP) --obj build/ac_mixer.o --ref old_dry:1 --ref old_send:3 --new new_combined:22 \
	  --report --all
	$(FP) --obj build/ac_mixer.o --ref old_dry:4 --ref old_send:0 --new new_combined:2 \
	  --report --all
	$(FP) --obj build/ac_mixer.o --ref old_dry:4 --ref old_send:3 --new new_combined:27 \
	  --report --all

# C17.9 / assessment A10: each listed defect, applied in a temporary copy of
# the tree, must make its host target fail. The working tree is untouched.
breakproofs:
	python3 breakproofs.py
