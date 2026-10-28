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
            -I$(ROOT)/Core/DSPAudio -I$(ROOT)/Core/compat -I$(GEN)
EX := python3 extract.py
FP := python3 fpseq.py

FILTER_SYMS := fastTanh fastTan tanhXdX softClipTwo SVF_setReso \
               SVF_directSetFilterValue SVF_init SVF_reset SVF_recalcFreq \
               SVF_setDrive SVF_calcBlockZDF SVF_calcBlockZDFFloat
NEW_FILTER_SYMS := svf_padeNum svf_padeDen $(FILTER_SYMS)
fn = $(foreach s,$(1),--func $(s))
rn = $(foreach s,$(2),--rename $(s)=$(1)$(s))

.PHONY: selftest filter special_tags pack postchain octave mixer \
        armcheck-filter armcheck-postchain armcheck-mixer clean \
        oscmod_identity osc_mod armcheck-oscmod elf_osc_baseline elf_osc_check
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

# ---------------------------------------------------------------------------
# S085 P1: Drum snc / rng (S085_P1_OSC_MOD_IMPLEMENTATION.md §5, §6).
#
# What:       oscmod_identity  the pre-change oscillator/buffer source text is
#                              unchanged (only the S085 P1 block is added);
#             osc_mod          host behaviour of the new code (test_osc_mod.c);
#             armcheck-oscmod  per-sample instruction counts of the new
#                              loops beside today's plain/FM loops (G-B);
#             elf_osc_baseline / elf_osc_check
#                              normalised disassembly of every existing
#                              oscillator loop in the linked firmware,
#                              before and after (G-A).
# Why:        S085 P1 rule: no increase in CPU, and fm/mix unchanged.
# Inputs:     frozen85/ (snapshot85.sh), the current sources, build/lxr02.elf.
# Accessors:  make -C tools/dsp_test <target>.
# Affiliates: elf_symdump.py, fpseq.py, extract.py, prelude.h.
# ---------------------------------------------------------------------------
FROZEN85 := frozen85
FW_ELF   := $(ROOT)/build/lxr02.elf
FW_INC   := $(addprefix -I$(ROOT)/,$(patsubst -I%,%,$(filter -I%, \
            $(shell sed -n '/^CFLAGS/,/^$$/p' $(ROOT)/Makefile))))
OSC_ELF_SYMS := calcSineBlock calcFmSineBlock calcFmBlock calcWavetableOscBlock \
                calcNoiseBlock calcSampleOscBlock calcSampleOscFmBlock \
                calcUserSampleOscBlock calcUserSampleOscFmBlock \
                calcNextOscSampleBlock calcNextOscSampleFmBlock \
                calcPeriodicInterpBlock calcPeriodicInterpFmBlock osc_setFreq \
                bufferTool_addBuffersSaturating
OSC_PLAIN := freq2PhaseIncr1024 freq2PhaseIncr32767 osc_clearWaveInterp \
             osc_updateSampleCache osc_waveInterpActive calcSineBlock \
             calcWavetableOscBlock calcNoiseBlock calcSampleOscBlock \
             calcUserSampleOscBlock calcPeriodicInterpBlock calcNextOscSampleBlock
OSC_S085  := osc_syncPull osc_ringSample osc_syncRatioQ16 osc_syncWaveform \
             osc_renderPlainWave \
             osc_syncClockInc osc_buildSyncList calcSineSyncBlock \
             calcWavetableOscSyncBlock calcSampleOscSyncBlock osc_renderSyncWave \
             calcPeriodicInterpSyncBlock calcNextOscSampleSyncBlock \
             calcSineRingBlock calcWavetableOscRingBlock calcNoiseRingBlock \
             calcSampleOscRingBlock calcUserSampleOscRingBlock \
             calcPeriodicInterpRingBlock calcNextOscSampleRingBlock

# G-A source half: nothing removed or changed in Oscillator.c/.h (lines may
# only be added); BufferTools.c/.h byte-identical.
oscmod_identity:
	@for f in Core/DSPAudio/Oscillator.c Core/DSPAudio/Oscillator.h; do \
	  n=$$(diff $(FROZEN85)/$$f $(ROOT)/$$f | grep -c '^<' || true); \
	  echo "$$f: removed or changed lines: $$n"; test "$$n" -eq 0 || exit 1; \
	done
	cmp $(FROZEN85)/Core/DSPAudio/BufferTools.c $(ROOT)/Core/DSPAudio/BufferTools.c
	cmp $(FROZEN85)/Core/DSPAudio/BufferTools.h $(ROOT)/Core/DSPAudio/BufferTools.h
	@echo "oscmod_identity: OK"

# Host behaviour of the new code against the unchanged plain loops.
# S086 P3: OscInfo is 71-143 (position, positionApplied, positionBendStep).
osc_mod: | $(GEN)
	$(EX) --src $(ROOT)/Core/DSPAudio/Oscillator.h --lines 49-65 --out $(GEN)/om_defs.inc
	$(EX) --src $(ROOT)/Core/DSPAudio/Oscillator.h --lines 71-143 --out $(GEN)/om_oscinfo.inc
	$(EX) --src $(ROOT)/Core/DSPAudio/Oscillator.c --object osc_stockTables \
	  --out $(GEN)/om_stock.inc
	$(EX) --src $(ROOT)/Core/SampleRom/SampleMemory.h --lines 104-109 \
	  --out $(GEN)/om_sampleinfo.inc
	$(EX) --src $(ROOT)/Core/DSPAudio/Oscillator.c --lines 123-124 --out $(GEN)/om_scratch.inc
	$(EX) --src $(ROOT)/Core/DSPAudio/Oscillator.h --func osc_syncDepthQ30 \
	  --func osc_ringDepthQ15 --out $(GEN)/om_map.c
	$(EX) --src $(ROOT)/Core/DSPAudio/Oscillator.c $(call fn,$(OSC_PLAIN)) \
	  --out $(GEN)/om_plain.c
	$(EX) --src $(ROOT)/Core/DSPAudio/Oscillator.c $(call fn,$(OSC_S085)) \
	  --out $(GEN)/om_s085.c
	$(HOSTCC) $(HOSTFLAGS) test_osc_mod.c -o build/osc_mod -lm
	./build/osc_mod

# G-B: static per-sample counts, new loops beside the loops they replace.
# Sync copies print two loops (no-reset and reset paths); budget the larger.
armcheck-oscmod: | $(GEN)
	$(ARMCC) $(ARMFLAGS) $(FW_INC) -c $(ROOT)/Core/DSPAudio/Oscillator.c -o build/ac_osc.o
	$(FP) --obj build/ac_osc.o --ref osc_buildSyncList --new osc_buildSyncList --report --all
	$(FP) --obj build/ac_osc.o --ref calcWavetableOscBlock --new calcWavetableOscSyncBlock --report --all
	$(FP) --obj build/ac_osc.o --ref calcSineBlock --new calcSineSyncBlock --report --all
	$(FP) --obj build/ac_osc.o --ref calcSampleOscBlock --new calcSampleOscSyncBlock --report --all
	$(FP) --obj build/ac_osc.o --ref calcFmBlock --new calcWavetableOscRingBlock --report --all
	$(FP) --obj build/ac_osc.o --ref calcFmSineBlock --new calcSineRingBlock --report --all
	$(FP) --obj build/ac_osc.o --ref calcSampleOscFmBlock --new calcSampleOscRingBlock --report --all
	$(FP) --obj build/ac_osc.o --ref calcNoiseBlock --new calcNoiseRingBlock --report --all
	$(FP) --obj build/ac_osc.o --ref calcUserSampleOscFmBlock --new calcUserSampleOscRingBlock --report --all

# G-A firmware half. Run elf_osc_baseline once on the unchanged tree (after
# `make clean && make all` at the repository root); elf_osc_check after.
elf_osc_baseline:
	python3 elf_symdump.py --elf $(FW_ELF) $(foreach s,$(OSC_ELF_SYMS),--sym $(s)) \
	  --out $(FROZEN85)/elf_osc.txt

elf_osc_check: | $(GEN)
	python3 elf_symdump.py --elf $(FW_ELF) $(foreach s,$(OSC_ELF_SYMS),--sym $(s)) \
	  --out build/elf_osc_now.txt
	python3 elf_symdump.py --compare $(FROZEN85)/elf_osc.txt build/elf_osc_now.txt

# ---------------------------------------------------------------------------
# S086 P2: the stock table waves Re3/Sw3 (S086_P2_ADD_STOCK_WAVES_CORRECTLY.md).
#
# What:       stock_tables      gen_stock_tables.py --check (C1-C4: the method
#                               reproduces sawTable; peak/DC; out-of-band; the
#                               committed wavetableStock.c regenerates
#                               byte-identical);
#             osc86_identity    Oscillator.c/.h against frozen86/: only the
#                               lines in frozen86/identity_allowed.txt are
#                               removed or changed; BufferTools identical;
#             osc_stock         host routing and law checks (test_osc_stock.c);
#             elf_osc86_baseline / elf_osc86_check
#                               whole-function text of the oscillator symbols
#                               (incl. the S085 sync/ring copies), before/after;
#             elf_loops86_baseline / elf_loops86_check
#                               the per-sample loop bodies inside them
#                               (elf_loopdump.py): the dispatchers may change,
#                               the loops may not.
# Why:        S086 P1 §1.3/§5: no existing loop changes; the new waves run the
#             Tri/Saw/Rec loops.
# Inputs:     frozen86/ (snapshot86.sh, at e56bb957), the current sources,
#             build/lxr02.elf. Run the *_baseline targets with FW_ELF pointing
#             at an e56bb957 build only (they rewrite frozen86/).
# Accessors:  make -C tools/dsp_test <target>.
# Affiliates: elf_symdump.py, elf_loopdump.py, extract.py, gen_stock_tables.py.
# ---------------------------------------------------------------------------
FROZEN86 := frozen86
OSC86_ELF_SYMS := calcFmBlock calcNextOscSampleBlock calcNextOscSampleFmBlock \
                  calcNextOscSampleRingBlock calcNextOscSampleSyncBlock \
                  calcNoiseRingBlock calcPeriodicInterpBlock calcPeriodicInterpFmBlock \
                  calcSampleOscFmBlock calcSampleOscRingBlock calcSampleOscSyncBlock \
                  calcSineRingBlock calcSineSyncBlock calcUserSampleOscBlock \
                  calcUserSampleOscFmBlock calcUserSampleOscRingBlock \
                  calcWavetableOscRingBlock calcWavetableOscSyncBlock \
                  osc_buildSyncList osc_renderPlainWave osc_renderSyncWave osc_setFreq \
                  calcSineBlock calcFmSineBlock calcWavetableOscBlock calcNoiseBlock \
                  calcSampleOscBlock calcPeriodicInterpSyncBlock \
                  calcPeriodicInterpRingBlock bufferTool_addBuffersSaturating
.PHONY: stock_tables osc86_identity osc_stock elf_osc86_baseline elf_osc86_check \
        elf_loops86_baseline elf_loops86_check

stock_tables:
	cd $(ROOT) && python3 tools/gen_stock_tables.py --check

OSC_FM86   := calcFmSineBlock calcFmBlock calcSampleOscFmBlock calcUserSampleOscFmBlock \
              calcPeriodicInterpFmBlock calcNextOscSampleFmBlock
OSC_FREQ86 := freqToTableIndex osc_calcSineFreqValue osc_calcNoiseFreqValue \
              osc_calcWavetableFreqValue osc_calcSampleFreqValue \
              osc_calcUserSampleFreqValue osc_setFreq
osc_stock: osc_mod
	$(EX) --src $(ROOT)/Core/DSPAudio/Oscillator.c $(call fn,$(OSC_FM86)) --out $(GEN)/os_fm.c
	$(EX) --src $(ROOT)/Core/DSPAudio/Oscillator.c --lines 108-111 --out $(GEN)/os_inc4096.inc
	$(EX) --src $(ROOT)/Core/DSPAudio/Oscillator.c --object osc_octaveEdgeHz \
	  $(call fn,$(OSC_FREQ86)) --out $(GEN)/os_freq.c
	$(EX) --src $(ROOT)/Core/DSPAudio/Oscillator.h --func osc_isTableWave \
	  --out $(GEN)/os_tablewave.c
	$(HOSTCC) $(HOSTFLAGS) test_osc_stock.c -o build/osc_stock -lm
	./build/osc_stock

osc86_identity:
	@for f in Core/DSPAudio/Oscillator.c Core/DSPAudio/Oscillator.h; do \
	  diff $(FROZEN86)/$$f $(ROOT)/$$f | grep '^<' | sed "s|^|$$f:|" ; \
	done > build/osc86_removed.txt || true
	@if diff -q build/osc86_removed.txt $(FROZEN86)/identity_allowed.txt >/dev/null; then \
	  echo "osc86_identity: removed/changed lines = the $$(wc -l < build/osc86_removed.txt | tr -d ' ') allowed"; \
	else echo "osc86_identity: unexpected removed/changed lines:"; \
	  diff build/osc86_removed.txt $(FROZEN86)/identity_allowed.txt; exit 1; fi
	cmp $(FROZEN86)/Core/DSPAudio/BufferTools.c $(ROOT)/Core/DSPAudio/BufferTools.c
	cmp $(FROZEN86)/Core/DSPAudio/BufferTools.h $(ROOT)/Core/DSPAudio/BufferTools.h
	@echo "osc86_identity: OK"

elf_osc86_baseline:
	python3 elf_symdump.py --elf $(FW_ELF) $(foreach s,$(OSC86_ELF_SYMS),--sym $(s)) \
	  --out $(FROZEN86)/elf_osc.txt

elf_osc86_check: | $(GEN)
	python3 elf_symdump.py --elf $(FW_ELF) $(foreach s,$(OSC86_ELF_SYMS),--sym $(s)) \
	  --out build/elf_osc86_now.txt
	python3 elf_symdump.py --compare $(FROZEN86)/elf_osc.txt build/elf_osc86_now.txt

elf_loops86_baseline:
	python3 elf_loopdump.py --elf $(FW_ELF) $(foreach s,$(OSC86_ELF_SYMS),--sym $(s)) \
	  --out $(FROZEN86)/elf_loops.txt

elf_loops86_check: | $(GEN)
	python3 elf_loopdump.py --elf $(FW_ELF) $(foreach s,$(OSC86_ELF_SYMS),--sym $(s)) \
	  --out build/elf_loops86_now.txt
	python3 elf_loopdump.py --compare $(FROZEN86)/elf_loops.txt build/elf_loops86_now.txt

# ---------------------------------------------------------------------------
# S086 P3/P4: continuous oscillator position and the Cym crash guard.
#
# What:       osc_pos tests the trigger offset and exact per-block bend with
#             the header's own constants (op_consts.inc is cut from
#             Oscillator.h between "#ifndef OSC_POS_BEND_CAP" and the cap
#             range assert, so a wrong constant fails here; assessment F3);
#             engine86_identity proves the four voice files only add the P3
#             blocks on top of P2's waveform-condition changes;
#             elf86_p2_snapshot saves the P2 linked-image reference before the
#             oscillator engines are edited; elf86_p3_check compares the P3
#             linked oscillator functions and loop bodies against that copy.
# Why:        P3 requires no oscillator loop edits, no phaseInc residue after
#             rendering, and no accidental change to existing loop code.
# Inputs:     Oscillator.h/.c, the four voice files, frozen86/, and lxr02.elf.
# Outputs:    host pass/fail lines and normalised P2/P3 ELF comparisons.
# Accessors:  make -C tools/dsp_test <target>.
# Affiliates: test_osc_pos.c, extract.py, elf_symdump.py, elf_loopdump.py.
# ---------------------------------------------------------------------------
OSC_POS86 := osc_isTableWave osc_positionIsPhase osc_positionTrigger \
             osc_positionBendApply osc_positionBendRestore
ENGINES86 := Core/DSP/Instruments/Drum/DrumVoice.c Core/DSP/Instruments/Snare/Snare.c \
             Core/DSP/Instruments/Cymbal/CymbalVoice.c Core/DSP/Instruments/HiHat/HiHat.c
.PHONY: osc_pos engine86_identity elf86_p2_snapshot elf86_p3_check

osc_pos: osc_mod
	sed -n '/^#ifndef OSC_POS_BEND_CAP/,/OSC_POS_BEND_CAP must be 1\.\.64/p' \
	  $(ROOT)/Core/DSPAudio/Oscillator.h > $(GEN)/op_consts.inc
	@grep -q 'OSC_POS_BEND_PER_STEP \* OUTPUT_DMA_SIZE' $(GEN)/op_consts.inc || \
	  { echo "osc_pos: constants block not found in Oscillator.h"; exit 1; }
	$(EX) --src $(ROOT)/Core/DSPAudio/Oscillator.h $(call fn,$(OSC_POS86)) \
	  --out $(GEN)/op_helpers.c
	$(HOSTCC) $(HOSTFLAGS) test_osc_pos.c -o build/osc_pos -lm
	$(HOSTCC) $(HOSTFLAGS) -DOSC_POS_BEND_CAP=8 -DOSC_POS_TEST_CAP test_osc_pos.c \
	  -o build/osc_pos_cap -lm
	./build/osc_pos
	./build/osc_pos_cap

engine86_identity: | $(GEN)
	@for f in $(ENGINES86); do \
	  diff $(FROZEN86)/$$f $(ROOT)/$$f | grep '^<' | sed "s|^|$$f:|" ; \
	done > build/engine86_removed.txt || true
	@if diff -q build/engine86_removed.txt $(FROZEN86)/engine_identity_allowed.txt >/dev/null; then \
	  echo "engine86_identity: OK (removed/changed lines = the allowed P2 lines)"; \
	else echo "engine86_identity: unexpected removed/changed lines:"; \
	 diff build/engine86_removed.txt $(FROZEN86)/engine_identity_allowed.txt; exit 1; fi

elf86_p2_snapshot:
	python3 elf_symdump.py --elf $(FW_ELF) $(foreach s,$(OSC86_ELF_SYMS),--sym $(s)) \
	  --out $(FROZEN86)/elf_osc_p2.txt
	python3 elf_loopdump.py --elf $(FW_ELF) $(foreach s,$(OSC86_ELF_SYMS),--sym $(s)) \
	  --out $(FROZEN86)/elf_loops_p2.txt

elf86_p3_check: | $(GEN)
	python3 elf_symdump.py --elf $(FW_ELF) $(foreach s,$(OSC86_ELF_SYMS),--sym $(s)) \
	  --out build/elf_osc86_p3.txt
	python3 elf_symdump.py --compare $(FROZEN86)/elf_osc_p2.txt build/elf_osc86_p3.txt
	python3 elf_loopdump.py --elf $(FW_ELF) $(foreach s,$(OSC86_ELF_SYMS),--sym $(s)) \
	  --out build/elf_loops86_p3.txt
	python3 elf_loopdump.py --compare $(FROZEN86)/elf_loops_p2.txt build/elf_loops86_p3.txt

# S086 P3 assessment F1: the per-sample hot loops of mixer_calcNextSampleBlock(),
# where LTO inlines the four voice renders. elf86_mixer_snapshot is run once,
# on a P2 build (FW_ELF=<P2 lxr02.elf>); elf86_mixer_check fails when any
# per-sample do-while loop in the mixer is longer than in P2 (elf_hotloops.py:
# opcode pairing, layout-insensitive). Fragments closing with an unconditional
# branch (jump tables, setup paths, cold saturation stubs) are not compared.
.PHONY: elf86_mixer_snapshot elf86_mixer_check
elf86_mixer_snapshot:
	python3 elf_loopdump.py --elf $(FW_ELF) --sym mixer_calcNextSampleBlock \
	  --out $(FROZEN86)/elf_mixer_p2.txt

elf86_mixer_check: | $(GEN)
	python3 elf_loopdump.py --elf $(FW_ELF) --sym mixer_calcNextSampleBlock \
	  --out build/elf_mixer_now.txt
	python3 elf_hotloops.py --compare $(FROZEN86)/elf_mixer_p2.txt build/elf_mixer_now.txt
