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
            -I$(ROOT)/Core/DSPAudio -I$(ROOT)/Core/DSP/InsertEffects \
            -I$(ROOT)/Core/compat -I$(GEN)
EX := python3 extract.py
FP := python3 fpseq.py

FILTER_SYMS := fastTanh fastTan tanhXdX softClipTwo SVF_setReso \
               SVF_directSetFilterValue SVF_init SVF_reset SVF_recalcFreq \
               SVF_setDrive SVF_calcBlockZDF SVF_calcBlockZDFFloat
NEW_FILTER_SYMS := svf_padeNum svf_padeDen $(FILTER_SYMS)
fn = $(foreach s,$(1),--func $(s))
rn = $(foreach s,$(2),--rename $(s)=$(1)$(s))

.PHONY: selftest filter special_tags pack postchain octave mixer \
        armcheck-filter armcheck-postchain armcheck-mixer \
        lfo_oneshot osc_pwm osc_sample osc_run swap_level \
        natural_order catalogue stock_gen wtb transient shaper test_fxbuf \
        test_insert_types test_voicebuffer_cases test_crumpbit_elastic \
        armcheck-vx2 armcheck-vx3 clean
$(GEN):
	mkdir -p $(GEN)

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

# S084 P1 WP1/WP4/WP5/WP6/WP7: the new voice foundations. Each target compiles
# the real source text (extracted verbatim) against the host prelude, so a
# change to the firmware function is what the test measures.
lfo_oneshot: | $(GEN)
	$(EX) --src $(ROOT)/Core/DSPAudio/wavetable.c --object sine_table \
	  --out $(GEN)/lfo_tab.c
	$(EX) --src $(ROOT)/Core/DSPAudio/lfo.c --func lfo_isOneShot \
	  --func lfo_retriggerPhase --func lfo_calc \
	  --out $(GEN)/lfo_fns.c
	cat $(GEN)/lfo_tab.c $(GEN)/lfo_fns.c > $(GEN)/lfo_new.c
	$(HOSTCC) $(HOSTFLAGS) test_lfo_oneshot.c -o build/lfo_oneshot -lm
	./build/lfo_oneshot

osc_pwm: | $(GEN)
	$(EX) --src $(ROOT)/Core/DSPAudio/wavetable.c --object sine_table \
	  --object sawTable --object triTable --object recTable \
	  --out $(GEN)/pwm_tab.c
	$(EX) --src $(ROOT)/Core/DSPAudio/Oscillator.c --object osc_octaveEdgeHz \
	  --func freqToTableIndex --func osc_freqToTableIndex \
	  --out $(GEN)/pwm_idx.c
	$(EX) --src $(ROOT)/Core/DSPAudio/OscillatorPwm.c --func pwm_read1024 \
	  --func pwm_readSine --func pwm_readWarp --func pwm_sawPair \
	  --func pwm_sawSum --func pwm_warp --func pwm_sweep \
	  --func pwm_familyLevel --func oscPwm_prepare --func oscPwm_render \
	  --func oscPwm_renderFm --out $(GEN)/pwm_fns.c
	cat $(GEN)/pwm_tab.c $(GEN)/pwm_idx.c $(GEN)/pwm_fns.c > $(GEN)/pwm_new.c
	$(HOSTCC) $(HOSTFLAGS) test_osc_pwm.c -o build/osc_pwm -lm
	./build/osc_pwm

osc_sample: | $(GEN)
	$(EX) --src $(ROOT)/Core/DSPAudio/OscillatorSample.c \
	  --func oscSample_bind --func oscSample_restart \
	  --func oscSample_phaseIncForFreq --func oscSample_render \
	  --func oscSample_renderFm --out $(GEN)/sample_new.c
	$(HOSTCC) $(HOSTFLAGS) test_osc_sample.c -o build/osc_sample -lm
	./build/osc_sample

osc_run: | $(GEN)
	$(EX) --src $(ROOT)/Core/DSPAudio/OscSection.c \
	  --func oscSection_init --func oscSection_beginTrigger \
	  --func oscSection_claimOngoing --func oscSection_claimPending \
	  --func oscSection_blendItp --func oscSection_finishBlock \
	  --func oscSection_filterRecalc \
	  --out $(GEN)/oscsection_new.c
	$(HOSTCC) $(HOSTFLAGS) test_osc_run.c -o build/osc_run -lm
	./build/osc_run

swap_level: | $(GEN)
	$(EX) --src $(ROOT)/Core/DSPAudio/mixer.c \
	  --func mixer_slotLevelAndDeclick --out $(GEN)/swap_new.c
	$(HOSTCC) $(HOSTFLAGS) test_swap_level.c -o build/swap_level -lm
	./build/swap_level

natural_order: | $(GEN)
	$(EX) --src $(ROOT)/Core/Hardware/SD/storageTypes.c \
	  --func storage_naturalCompare --func storage_stripOrderPrefix \
	  --out $(GEN)/storage_natural.c
	$(HOSTCC) $(HOSTFLAGS) test_natural_order.c -o build/natural_order -lm
	./build/natural_order

stock_gen: | $(GEN)
	python3 $(ROOT)/tools/gen_stock_recipes.py \
	  --emit-ref frozen/stock_ref.bin
	$(EX) --src $(ROOT)/Core/DSPAudio/wavetable.c --object sine_table \
	  --out $(GEN)/stock_sine.c
	$(EX) --src $(ROOT)/Core/DSPAudio/StockWaves.c \
	  --object stockWave_levelOffset --object stockWave_levelShift \
	  --out $(GEN)/stock_levels.c
	$(HOSTCC) $(HOSTFLAGS) -I$(ROOT)/Core/SampleRom \
	  -I$(ROOT)/Core/DSPAudio -I$(ROOT)/Core -I$(ROOT)/Core/compat \
	  test_stock_gen.c -o build/stock_gen -lm
	./build/stock_gen

wtb: | $(GEN)
	$(EX) --src $(ROOT)/Core/DSPAudio/OscillatorWavetable.c \
	  --func oscWavetable_bind --func oscWavetable_render \
	  --func oscWavetable_renderFm --out $(GEN)/wtb_new.c
	$(HOSTCC) $(HOSTFLAGS) test_wtb.c -o build/wtb -lm
	./build/wtb

shaper: | $(GEN)
	python3 $(ROOT)/tools/plots/plot_waveshapers.py \
	  --export-ref frozen/shaper
	$(HOSTCC) $(HOSTFLAGS) -I$(ROOT)/Core/DSPAudio \
	  test_shaper.c $(ROOT)/Core/DSPAudio/DriveShaper.c -o build/shaper -lm
	./build/shaper

catalogue: | $(GEN)
	$(HOSTCC) $(HOSTFLAGS) -I$(ROOT) -I$(ROOT)/Core \
	  -I$(ROOT)/Core/SampleRom -I$(ROOT)/Core/DSPAudio \
	  -I$(ROOT)/Core/compat test_catalogue.c -o build/catalogue -lm
	./build/catalogue

transient: | $(GEN)
	$(EX) --src $(ROOT)/Core/DSPAudio/transientGenerator.c \
	  --func transient_resolve --func transient_init --func transient_trigger --func transient_setType \
	  --func transient_setWave --func transient_waveMax \
	  --func transient_calcBlock --out $(GEN)/transient_new.c
	$(HOSTCC) $(HOSTFLAGS) -I$(ROOT) -I$(ROOT)/Core/DSPAudio -I$(ROOT)/Core/compat \
	  test_transient.c -o build/transient -lm
	./build/transient

test_fxbuf: | $(GEN)
	$(HOSTCC) -O2 -std=gnu11 -Wall -Wno-unused-function \
	  -DFXBUF_HOST_TEST -I$(ROOT) -I$(ROOT)/Core/DSP/Effects \
	  test_fxbuf.c -o build/fxbuf -lm
	./build/fxbuf

INSERT_TYPE_SRCS := \
	$(ROOT)/Core/DSP/InsertEffects/InsertDriveSrr.c \
	$(ROOT)/Core/DSP/InsertEffects/InsertFold.c \
	$(ROOT)/Core/DSP/InsertEffects/InsertCrush.c \
	$(ROOT)/Core/DSP/InsertEffects/InsertRing.c \
	$(ROOT)/Core/DSP/InsertEffects/InsertPhaser.c \
	$(ROOT)/Core/DSP/InsertEffects/InsertFlanger.c \
	$(ROOT)/Core/DSP/InsertEffects/InsertChorus.c \
	$(ROOT)/Core/DSP/InsertEffects/InsertEcho.c \
	$(ROOT)/Core/DSP/InsertEffects/InsertComb.c \
	$(ROOT)/Core/DSP/InsertEffects/InsertRoom.c \
	$(ROOT)/Core/DSP/InsertEffects/Transient/TransientResonator.c \
	$(ROOT)/Core/DSP/InsertEffects/Transient/TransientPluck.c \
	$(ROOT)/Core/DSP/InsertEffects/Transient/TransientTube.c \
	$(ROOT)/Core/DSP/InsertEffects/Transient/TransientFlam.c \
	$(ROOT)/Core/DSP/InsertEffects/Transient/TransientStutter.c \
	$(ROOT)/Core/DSP/InsertEffects/Transient/TransientReverse.c \
	$(ROOT)/Core/DSP/InsertEffects/Transient/TransientSpring.c \
	host_transient_tables.c

test_insert_types: | $(GEN)
	$(HOSTCC) -O2 -std=gnu11 -Wall -Wno-unused-function \
	  -fno-vectorize -fno-slp-vectorize -DINSERT_HOST_TEST \
	  -I$(ROOT) -I$(ROOT)/Core \
	  -I$(ROOT)/Core/DSPAudio -I$(ROOT)/Core/DSP/Effects \
	  -I$(ROOT)/Core/DSP/InsertEffects -I$(ROOT)/Core/compat \
	  test_insert_types.c $(INSERT_TYPE_SRCS) -o build/insert_types -lm
	./build/insert_types

test_voicebuffer_cases: | $(GEN)
	$(HOSTCC) -O2 -std=gnu11 -Wall -Wno-unused-function \
	  -DINSERT_HOST_TEST -DFXBUF_HOST_TEST \
	  -I$(ROOT) -I$(ROOT)/Core -I$(ROOT)/Core/DSPAudio \
	  -I$(ROOT)/Core/DSP/Effects -I$(ROOT)/Core/DSP/InsertEffects \
	  -I$(ROOT)/Core/compat test_voicebuffer_cases.c -o build/voicebuffer_cases -lm
	./build/voicebuffer_cases

test_crumpbit_elastic: | $(GEN)
	$(EX) --src $(ROOT)/Core/DSP/Effects/CrumpBit/CrumpBitEffect.c \
	  --func crumpBit_rateSamples --func crumpBit_divisionFor \
	  --func crumpBit_syncDivision --func crumpBit_reverse \
	  --func crumpBit_compact --func crumpBit_seat \
	  --func crumpBit_effectiveMin --out $(GEN)/crumpbit_elastic_new.c
	$(HOSTCC) $(HOSTFLAGS) -I$(ROOT) -I$(ROOT)/Core \
	  test_crumpbit_elastic.c -o build/crumpbit_elastic -lm
	./build/crumpbit_elastic

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
	  --ref old_addGain --ref old_calcDistBlock --new new_drum
	$(FP) --obj build/ac_postchain.o --ref ref_snare:0 --ref old_calcDistBlock \
	  --new new_snare
	$(FP) --obj build/ac_postchain.o --ref ref_cymbal:0 --ref old_calcDistBlock \
	  --new new_addgain

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

# S084 VX2 WP10: per-loop instruction report for the new render code.
armcheck-vx2: | $(GEN)
	$(EX) --src $(ROOT)/Core/DSPAudio/Oscillator.c \
	  --func calcStockWaveBlock --func calcStockWaveFmBlock \
	  --rename static= --out $(GEN)/vx2_stock_arm.c
	$(EX) --src $(ROOT)/Core/DSPAudio/OscillatorWavetable.c \
	  --func oscWavetable_bind --func oscWavetable_render \
	  --func oscWavetable_renderFm --rename static= \
	  --out $(GEN)/vx2_wtb_arm.c
	$(EX) --src $(ROOT)/Core/DSPAudio/transientGenerator.c \
	  --func transient_resolve --func transient_calcBlock \
	  --rename static= --out $(GEN)/vx2_tr_arm.c
	$(EX) --src $(ROOT)/Core/DSPAudio/ResonantFilter.c \
	  --func svf_padeNum --func svf_padeDen --func fastTanh --func tanhXdX \
	  --func softClipTwo --func SVF_calcBlockZDF \
	  --func SVF_calcBlockZDFShaped --rename static= \
	  --out $(GEN)/vx2_flt_arm.c
	cat $(GEN)/vx2_stock_arm.c $(GEN)/vx2_wtb_arm.c $(GEN)/vx2_tr_arm.c \
	  $(GEN)/vx2_flt_arm.c > $(GEN)/vx2_fns_arm.c
	$(ARMCC) $(ARMFLAGS) -I$(ROOT)/Core/SampleRom \
	  -c armcheck/vx2.c -o build/ac_vx2.o
	# S084 VX2 review fix F3/F5: per-type probes (armcheck/vx2_spec.c). The
	# shared functions hold several overlapping loops, so their loop 0 is not
	# a meaningful figure; each probe below is one specialised loop.
	sed -e 's/^INITCM_EFFECT_NOINLINE void SVF_calcBlockZDFShaped(/static inline __attribute__((always_inline)) void spec_zdfs(/' \
	    -e 's/^INITCM_EFFECT_NOINLINE void SVF_calcBlockZDF(/static inline __attribute__((always_inline)) void spec_zdf(/' \
	    -e '/^INITCM_EFFECT float \(tanhXdX\|softClipTwo\)/s/^INITCM_EFFECT /static inline /' \
	    $(GEN)/vx2_flt_arm.c > $(GEN)/vx2_flt_spec.c
	$(EX) --src $(ROOT)/Core/DSPAudio/transientGenerator.c \
	  --func transient_calcBlock --rename transient_calcBlock=spec_tr \
	  --out $(GEN)/vx2_tr_spec_raw.c
	sed -e 's/^void spec_tr(/static inline __attribute__((always_inline)) void spec_tr(/' \
	    $(GEN)/vx2_tr_spec_raw.c > $(GEN)/vx2_tr_spec.c
	$(ARMCC) $(ARMFLAGS) -I$(ROOT)/Core/SampleRom \
	  -c armcheck/vx2_spec.c -o build/ac_vx2_spec.o
	@echo "--- stock reader: loop 1 = per-sample loop (loop 0 is the fallback call)"
	$(FP) --obj build/ac_vx2.o --ref calcStockWaveBlock:1 \
	  --new calcStockWaveFmBlock:0 --report --all | tail -1
	@echo "--- wtb plain / FM"
	$(FP) --obj build/ac_vx2.o --ref oscWavetable_render:0 \
	  --new oscWavetable_renderFm:0 --report --all | tail -1
	@echo "--- transient kinds (cap 22)"
	@for k in pcm sample sine table noise crash; do printf "%-7s " $$k; \
	  $(FP) --obj build/ac_vx2_spec.o --ref tr_$$k:0 --new tr_$$k:0 \
	    --report --all | tail -1; done
	@echo "--- ZDF LP: sft reference, then each drive type (target sft + 6)"
	@printf "%-7s " sft; $(FP) --obj build/ac_vx2_spec.o --ref zdf_sft_lp:0 \
	  --new zdf_sft_lp:0 --report --all | tail -1
	@for t in rct art thf fdc bsw tsw lsw lsq sfd afd asf chb rsb; do \
	  printf "%-7s " $$t; $(FP) --obj build/ac_vx2_spec.o --ref zdf_$$t:0 \
	    --new zdf_$$t:0 --report --all | tail -1; done

# S084 VX3 remediation §18.4: per-type insert loop cost report (Gate G3/G4).
# Report only; the caps and the per-type decisions are in
# S084_P3_IMPLEMENTATION.md §18.4.
armcheck-vx3: | $(GEN)
	sh armcheck/vx3_probe.sh "$(ARMCC) $(ARMFLAGS)"
	python3 looplen.py build/vx3_p_*.o
