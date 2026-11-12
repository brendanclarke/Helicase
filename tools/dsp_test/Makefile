# Module host tests (tools/dsp_test).
#
# What:       one host program per firmware module, built from the real
#             source text (extract.py, by name; never by line range):
#               oscillator          modules/oscillator.c (+ the pos cap build,
#                                   + 2 breaks: S089 P3 FR1/FR2)
#               modulation          modules/modulation.c
#               voice_control       modules/voice_control.c (+ break build)
#               sequencer           modules/sequencer.c (+ break build)
#               instrument_manager  modules/instrument_manager.c (+ 2 breaks)
#               samples             modules/samples.c (+ catalogue and row
#                                   resampler break builds)
#               crumpbit            modules/crumpbit.c (+ 25 breaks: P6
#                                   `dly` curve, R4 AC seed, P2 flat tone, P2
#                                   seat delay clamp, P2 Rate-limit probe;
#                                   P3 wet around the mix stage, grain
#                                   freeze, pitch direction, lane map; P4
#                                   grain fade asymmetry, `bal` side swap,
#                                   grain overwrite clamp; P5 data-line
#                                   table, `grn` echo curve, filter
#                                   hand-over, grain block-aligned ends,
#                                   one-pole `bal` side swap, dropped-
#                                   trigger ages; P5 follow-up `pit` window
#                                   glide, `grn` +36 st step guard, `pit`
#                                   fine ignored; P6 `dly` limit plateau,
#                                   `grn` tap fade, `grn` low rows, `grn`
#                                   reseat tap)
#               crumpbit_tables     tools/plots/gen_crumpbit_tables.py --check
#               sampleoptimize      modules/sampleoptimize.py
#               special_tags        check_special_tags.py
#               stock_tables        tools/plots/gen_stock_tables.py --check
#               alkaline            modules/alkaline.c (S093 P0: DiodeLadder,
#                                   AlkalineControl, AlkalineVoice over the
#                                   real Oscillator.c; + legato, wtb blend
#                                   ramp and wtb glide breaks)
#               alkaline_tables     tools/alkaline/gen_alk_tables.py --check
#             A break build compiles a deliberately broken copy and must fail.
# Why:        keeps the laws earlier sessions proved runnable against today's
#             source. The effects retry kit is spike/ (its own Makefile).
# Inputs:     Core/ sources, prelude.h, osc_host.h, cat_host.h,
#             catalogue_host.c, extract.py.
# Outputs:    generated fragments and programs under build/.
# Accessors:  make -C tools/dsp_test <target> (or all).
# Affiliates: DSP_TEST.md, spike/Makefile, fpseq.py, elf_*.py.
ROOT   := ../..
GEN    := build/gen
HOSTCC ?= cc
HOSTFLAGS := -O2 -std=gnu11 -Wall -Wno-unused-function -fno-vectorize -fno-slp-vectorize \
             -include prelude.h -I$(GEN)
EX := python3 extract.py

CAT_HOST_SRC := $(ROOT)/Core/SampleRom/SampleMemory.c \
                $(ROOT)/Core/SampleRom/SampleInstall.c catalogue_host.c
CAT_HOST_INC := -I. -I$(ROOT)/Core/SampleRom -include cat_host.h
OSC_HOST_INC := $(CAT_HOST_INC) -I$(ROOT)/Core/MIDI -Wno-unknown-attributes

.PHONY: all clean oscillator modulation voice_control sequencer \
        instrument_manager samples sampleoptimize run_modes special_tags \
        stock_tables voicefold fold_tables pwm_tables crumpbit crumpbit_tables \
        crumpbit_elf_check crumpbit_cpu_check alkaline alkaline_tables \
        elf_p4_snapshot elf_p4_check seq_steplen lfo lfo_rate_table alk_post_tables \
        space space_cpu_check
all: oscillator modulation voice_control sequencer seq_steplen instrument_manager samples \
     sampleoptimize run_modes special_tags stock_tables voicefold fold_tables pwm_tables \
     crumpbit crumpbit_tables alkaline alkaline_tables alk_post_tables lfo lfo_rate_table \
     space

$(GEN):
	mkdir -p $(GEN)

clean:
	rm -rf build

# The whole real Oscillator.h/.c, firmware includes and ITCM sections removed.
$(GEN)/Oscillator.host.h: $(ROOT)/Core/DSPAudio/Oscillator.h | $(GEN)
	$(EX) --src $< --body --out $@
$(GEN)/Oscillator.host.c: $(ROOT)/Core/DSPAudio/Oscillator.c | $(GEN)
	$(EX) --src $< --body --out $@
$(GEN)/oscPwmTables.host.h: $(ROOT)/Core/DSPAudio/oscPwmTables.h | $(GEN)
	$(EX) --src $< --body --out $@
$(GEN)/oscPwmTables.host.c: $(ROOT)/Core/DSPAudio/oscPwmTables.c | $(GEN)
	$(EX) --src $< --body --out $@
# S094 P4: the shared pwm warp (Oscillator.c and DiodeLadder.c include it).
$(GEN)/oscPwmWarp.host.h: $(ROOT)/Core/DSPAudio/oscPwmWarp.h | $(GEN)
	$(EX) --src $< --body --out $@

# S089 P3 breaks (fmramp group): the ramp dispatcher sending wtb to the
# one-frame twin must fail FR1; the depth stepped before its first use
# (d = d0 + dStep) must fail FR2. Each break edits the generated
# Oscillator.host.c and is found first through its own -I directory.
oscillator: $(GEN)/Oscillator.host.h $(GEN)/Oscillator.host.c \
	$(GEN)/oscPwmTables.host.h $(GEN)/oscPwmTables.host.c \
	$(GEN)/oscPwmWarp.host.h
	$(HOSTCC) $(HOSTFLAGS) $(OSC_HOST_INC) modules/oscillator.c $(CAT_HOST_SRC) \
	  -o build/oscillator -lm
	$(HOSTCC) $(HOSTFLAGS) $(OSC_HOST_INC) -DOSC_POS_BEND_CAP=8 -DOSC_POS_TEST_CAP \
	  modules/oscillator.c $(CAT_HOST_SRC) -o build/oscillator_cap -lm
	./build/oscillator
	./build/oscillator_cap
	mkdir -p build/break_fmr1 build/break_fmr2
	sed 's/osc_wtbFmBlendRampBlock(osc, mod, buf, size, gain, d0, dStep);/osc_wtbFmRampBlock(osc, mod, buf, size, gain, d0, dStep);/' \
	  $(GEN)/Oscillator.host.c > build/break_fmr1/Oscillator.host.c
	$(HOSTCC) -Ibuild/break_fmr1 $(HOSTFLAGS) $(OSC_HOST_INC) modules/oscillator.c \
	  $(CAT_HOST_SRC) -o build/oscillator_break_fmr1 -lm
	@if ./build/oscillator_break_fmr1 >/dev/null 2>&1; then \
	  echo "oscillator_break_fmr1: FAILED (ramp wtb routing break not caught)"; exit 1; \
	else echo "oscillator_break_fmr1: caught (law FR1 fails as expected)"; fi
	sed 's/float d = d0;/float d = d0 + dStep;/' \
	  $(GEN)/Oscillator.host.c > build/break_fmr2/Oscillator.host.c
	$(HOSTCC) -Ibuild/break_fmr2 $(HOSTFLAGS) $(OSC_HOST_INC) modules/oscillator.c \
	  $(CAT_HOST_SRC) -o build/oscillator_break_fmr2 -lm
	@if ./build/oscillator_break_fmr2 >/dev/null 2>&1; then \
	  echo "oscillator_break_fmr2: FAILED (ramp step-order break not caught)"; exit 1; \
	else echo "oscillator_break_fmr2: caught (law FR2 fails as expected)"; fi
	# S094 P1 S3 breaks: forward second read fails PW2; unlimited width slew fails PW5.
	mkdir -p build/break_pwm1 build/break_pwm2
	sed 's/const uint32_t ib = (ia - d) \& 0x3FFu;/const uint32_t ib = (ia + d) \& 0x3FFu;/' \
	  $(GEN)/Oscillator.host.c > build/break_pwm1/Oscillator.host.c
	@grep -q '(ia + d) & 0x3FFu' build/break_pwm1/Oscillator.host.c || \
	  { echo "oscillator_break_pwm1: sed did not apply"; exit 1; }
	$(HOSTCC) -Ibuild/break_pwm1 $(HOSTFLAGS) $(OSC_HOST_INC) modules/oscillator.c \
	  $(CAT_HOST_SRC) -o build/oscillator_break_pwm1 -lm
	@if ./build/oscillator_break_pwm1 >/dev/null 2>&1; then \
	  echo "oscillator_break_pwm1: FAILED (a forward second read not caught)"; exit 1; \
	else echo "oscillator_break_pwm1: caught (law PW2 fails as expected)"; fi
	sed 's/if (d > OSC_PWM_SLEW_STEPS)/if (0)/' \
	  $(GEN)/Oscillator.host.c > build/break_pwm2/Oscillator.host.c
	@grep -q 'if (0)' build/break_pwm2/Oscillator.host.c || \
	  { echo "oscillator_break_pwm2: sed did not apply"; exit 1; }
	$(HOSTCC) -Ibuild/break_pwm2 $(HOSTFLAGS) $(OSC_HOST_INC) modules/oscillator.c \
	  $(CAT_HOST_SRC) -o build/oscillator_break_pwm2 -lm
	@if ./build/oscillator_break_pwm2 >/dev/null 2>&1; then \
	  echo "oscillator_break_pwm2: FAILED (an unlimited width step not caught)"; exit 1; \
	else echo "oscillator_break_pwm2: caught (law PW5 fails as expected)"; fi
	# S094 P4 break: Rec planned on the Saw width table must fail PW2 and PW10.
	mkdir -p build/break_pwm3
	sed 's/pl->d = oscPwmDRec\[k\];/pl->d = oscPwmDSaw[k];/' \
	  $(GEN)/Oscillator.host.c > build/break_pwm3/Oscillator.host.c
	@grep -q 'pl->d = oscPwmDSaw\[k\];' build/break_pwm3/Oscillator.host.c || \
	  { echo "oscillator_break_pwm3: sed did not apply"; exit 1; }
	$(HOSTCC) -Ibuild/break_pwm3 $(HOSTFLAGS) $(OSC_HOST_INC) modules/oscillator.c \
	  $(CAT_HOST_SRC) -o build/oscillator_break_pwm3 -lm
	@if ./build/oscillator_break_pwm3 >/dev/null 2>&1; then \
	  echo "oscillator_break_pwm3: FAILED (Rec width table break not caught)"; exit 1; \
	else echo "oscillator_break_pwm3: caught (laws PW2/PW10 fail as expected)"; fi

modulation: $(GEN)/Oscillator.host.h
	$(EX) --src $(ROOT)/Core/DSPAudio/modulationNode.c \
	  --func modNode_interpStampOf --func modNode_setWaveInterpEnabled \
	  --func modNode_setItpReserved --func modNode_resetTargets \
	  --func modNode_interpClaim \
	  --out $(GEN)/modulation_interp.c
	$(HOSTCC) $(HOSTFLAGS) $(OSC_HOST_INC) modules/modulation.c -o build/modulation -lm
	./build/modulation

# S094 P1 S1: the real LFO phase law and rate table (modules/lfo.c).
# Breaks: (1) the sync writer storing the byte only (lfo_setSync() without
# its recomputation) must fail LF1; (2) lfo_calc() scaling by modNodeValue
# while synced must fail LF4.
lfo: | $(GEN)
	$(EX) --src $(ROOT)/Core/DSPAudio/lfo.h --macro LFO_SR --out $(GEN)/lfo_sr.inc
	$(EX) --src $(ROOT)/Core/DSPAudio/lfo.h --macro LFO_SINE --macro LFO_TRI \
	  --macro LFO_SAW_UP --macro LFO_SAW_DOWN --macro LFO_REC --macro LFO_NOISE \
	  --macro LFO_EXP_UP --macro LFO_EXP_DOWN --out $(GEN)/lfo_waves.inc
	$(EX) --src $(ROOT)/Core/DSPAudio/lfoRateTable.c --object lfoRateHz \
	  --out $(GEN)/lfo_rate_table.c
	$(EX) --src $(ROOT)/Core/DSPAudio/lfo.c --func lfo_calc --func lfo_calcPhaseInc \
	  --func lfo_setFreq --func lfo_setSync --out $(GEN)/lfo_funcs.c
	$(HOSTCC) $(HOSTFLAGS) -I$(GEN) modules/lfo.c -o build/lfo -lm
	./build/lfo
	mkdir -p build/break_lfo1 build/break_lfo2
	sed 's/lfo->sync = sync;/lfo->sync = sync; return;/' \
	  $(GEN)/lfo_funcs.c > build/break_lfo1/lfo_funcs.c
	$(HOSTCC) -Ibuild/break_lfo1 $(HOSTFLAGS) -I$(GEN) modules/lfo.c -o build/lfo_break1 -lm
	@if ./build/lfo_break1 >/dev/null 2>&1; then \
	  echo "lfo_break1: FAILED (a store-only sync writer not caught)"; exit 1; \
	else echo "lfo_break1: caught (law LF1 fails as expected)"; fi
	sed 's/lfo->sync ? 1.0f : lfo->modNodeValue/lfo->modNodeValue/' \
	  $(GEN)/lfo_funcs.c > build/break_lfo2/lfo_funcs.c
	$(HOSTCC) -Ibuild/break_lfo2 $(HOSTFLAGS) -I$(GEN) modules/lfo.c -o build/lfo_break2 -lm
	@if ./build/lfo_break2 >/dev/null 2>&1; then \
	  echo "lfo_break2: FAILED (a synced LFO following the overlay not caught)"; exit 1; \
	else echo "lfo_break2: caught (law LF4 fails as expected)"; fi

lfo_rate_table:
	cd $(ROOT) && python3 tools/plots/gen_lfo_rate_table.py --check

# Break: the old trigger-then-drain order (-DBREAK_DRAIN_AFTER_FIRE); the
# program reports law 1 failing and exits 0 only when it does.
voice_control: | $(GEN)
	$(EX) --src $(ROOT)/Core/MIDI/MidiVoiceControl.c \
	  --func voiceControl_popPendingBatch --func voiceControl_prepareTrigger \
	  --func voiceControl_fireTrigger --func voiceControl_processPending \
	  --rename voiceControl_irqSave=sta_irqSave \
	  --rename voiceControl_irqRestore=sta_irqRestore \
	  --out $(GEN)/voice_control_pending.c
	$(HOSTCC) $(HOSTFLAGS) modules/voice_control.c -o build/voice_control -lm
	./build/voice_control
	$(HOSTCC) $(HOSTFLAGS) -DBREAK_DRAIN_AFTER_FIRE modules/voice_control.c \
	  -o build/voice_control_break -lm
	./build/voice_control_break

# Break: the drain starts at 0 instead of the cursor (law 2 must fail).
sequencer: | $(GEN)
	$(EX) --src $(ROOT)/Core/Sequencer/sequencer.c \
	  --func seq_drainPendingAutomationTo --func seq_pendingAutomationMark \
	  --out $(GEN)/sequencer_drain.c
	$(HOSTCC) $(HOSTFLAGS) modules/sequencer.c -o build/sequencer -lm
	./build/sequencer
	mkdir -p build/break_seq
	sed 's/uint8_t i = seq_pending_automation_done;/uint8_t i = 0u;/' \
	  $(GEN)/sequencer_drain.c > build/break_seq/sequencer_drain.c
	$(HOSTCC) -Ibuild/break_seq $(HOSTFLAGS) modules/sequencer.c \
	  -o build/sequencer_break -lm
	@if ./build/sequencer_break >/dev/null 2>&1; then \
	  echo "sequencer_break: FAILED (the cursor-start break was not caught)"; \
	  exit 1; else echo "sequencer_break: caught (law 2 fails as expected)"; fi

# S093 P4: law AL-T. seq_trackStepSamples() against the real step-scale table
# and a double-precision reference. Break: the tick product in 32 bits (it
# overflows at every tempo) must fail AL-T.
seq_steplen: | $(GEN)
	$(EX) --src $(ROOT)/Core/Sequencer/sequencer.c --func seq_trackStepSamples \
	  --out $(GEN)/seq_stepsamples.c
	$(HOSTCC) $(HOSTFLAGS) -I$(ROOT)/Core/Sequencer modules/seq_steplen.c \
	  $(ROOT)/Core/Sequencer/StepScale.c -o build/seq_steplen -lm
	./build/seq_steplen
	mkdir -p build/break_steplen
	sed 's/const uint64_t ticksQ8 = (uint64_t)stepScale_ticksQ8(scale);/const uint32_t ticksQ8 = stepScale_ticksQ8(scale);/' \
	  $(GEN)/seq_stepsamples.c > build/break_steplen/seq_stepsamples.c
	@grep -q 'const uint32_t ticksQ8' build/break_steplen/seq_stepsamples.c || \
	  { echo "seq_steplen_break: sed did not apply"; exit 1; }
	$(HOSTCC) -Ibuild/break_steplen $(HOSTFLAGS) -I$(ROOT)/Core/Sequencer \
	  modules/seq_steplen.c $(ROOT)/Core/Sequencer/StepScale.c -o build/seq_steplen_break -lm
	@if ./build/seq_steplen_break >/dev/null 2>&1; then \
	  echo "seq_steplen_break: FAILED (the 32-bit product was not caught)"; \
	  exit 1; else echo "seq_steplen_break: caught (AL-T fails as expected)"; fi

# Breaks: the gate without its SELECTOR branch (law 3); the row as
# DTYPE_0B127 (law 4).
instrument_manager: | $(GEN)
	$(EX) --src $(ROOT)/Core/Menu/menu.h --enum Datatypes --out $(GEN)/im_dtypes.inc
	$(EX) --src $(ROOT)/Core/Menu/MenuText.h --macro MENU_OSC_TYPE --out $(GEN)/im_menuid.inc
	$(EX) --src $(ROOT)/Core/DSP/Instruments/Drum/DrumParameters.c \
	  --macro ROW_SELECT_SPECIAL --out $(GEN)/im_rowmacro.inc
	$(EX) --src $(ROOT)/Core/DSP/Instruments/Drum/DrumParameters.c \
	  --macro ROW_SELECT_ONOFF_SPECIAL --out $(GEN)/im_onoffmacro.inc
	$(EX) --src $(ROOT)/Core/Menu/MenuText.h \
	  --macro ONOFF_TAG_OSC_RUN --macro ONOFF_TAG_VOICE_CHAIN \
	  --out $(GEN)/im_onofftag.inc
	$(EX) --src $(ROOT)/Core/DSP/Instruments/Drum/DrumParameters.c \
	  --typedef drum_param_index_t --out $(GEN)/im_index.inc
	$(EX) --src $(ROOT)/Core/DSP/Instruments/InstrumentManager.c \
	  --func instrumentManager_targetValid --out $(GEN)/im_gate.c
	$(EX) --src $(ROOT)/Core/Sequencer/sequencer.c --func seq_rampDtypeOk \
	  --out $(GEN)/im_ramp.c
	$(HOSTCC) $(HOSTFLAGS) modules/instrument_manager.c -o build/instrument_manager -lm
	./build/instrument_manager
	mkdir -p build/break_gate build/break_row
	sed 's/(descriptor->flags & INSTRUMENT_PARAM_FLAG_SELECTOR)/0/' \
	  $(GEN)/im_gate.c > build/break_gate/im_gate.c
	$(HOSTCC) -Ibuild/break_gate $(HOSTFLAGS) modules/instrument_manager.c \
	  -o build/instrument_manager_break_gate -lm
	@if ./build/instrument_manager_break_gate >/dev/null 2>&1; then \
	  echo "instrument_manager_break_gate: FAILED (selector branch break not caught)"; exit 1; \
	else echo "instrument_manager_break_gate: caught (law 3 fails as expected)"; fi
	sed 's/(uint8_t)(DTYPE_MENU | (menu_ << 4))/(uint8_t)DTYPE_0B127/' \
	  $(GEN)/im_rowmacro.inc > build/break_row/im_rowmacro.inc
	$(HOSTCC) -Ibuild/break_row $(HOSTFLAGS) modules/instrument_manager.c \
	  -o build/instrument_manager_break_row -lm
	@if ./build/instrument_manager_break_row >/dev/null 2>&1; then \
	  echo "instrument_manager_break_row: FAILED (dtype break not caught)"; exit 1; \
	else echo "instrument_manager_break_row: caught (law 4 fails as expected)"; fi
	mkdir -p build/break_onoff
	sed 's/DTYPE_ON_OFF | ((tag_) << 4)/DTYPE_ON_OFF/' \
	  $(GEN)/im_onoffmacro.inc > build/break_onoff/im_onoffmacro.inc
	$(HOSTCC) -Ibuild/break_onoff $(HOSTFLAGS) modules/instrument_manager.c \
	  -o build/instrument_manager_break_onoff -lm
	@if ./build/instrument_manager_break_onoff >/dev/null 2>&1; then \
	  echo "instrument_manager_break_onoff: FAILED (on/off tag break not caught)"; exit 1; \
	else echo "instrument_manager_break_onoff: caught (law 7 fails as expected)"; fi

# Break: the header is published in begin() (after the erase), so an
# uncommitted install no longer reads EMPTY (catalogue law 3 must fail).
samples: | $(GEN)
	$(EX) --src $(ROOT)/Core/Hardware/SD/storageTypes.c \
	  --func storage_naturalLower --func storage_naturalCompare \
	  --func storage_displayNameFromLfn --out $(GEN)/samples_order.c
	$(HOSTCC) $(HOSTFLAGS) $(CAT_HOST_INC) modules/samples.c $(CAT_HOST_SRC) \
	  -o build/samples -lm
	./build/samples
	mkdir -p build/break_cat
	sed 's|install_fault = (uint8_t)(sampleFlash_eraseAllSamples() != 0);|install_fault = (uint8_t)(sampleFlash_eraseAllSamples() != 0); { sample_catalogue_header_t bh; uint32_t ba; memset(\&bh, 0, sizeof(bh)); bh.magic = SAMPLE_CATALOGUE_MAGIC; bh.version = SAMPLE_CATALOGUE_VERSION; bh.sample_entry_bytes = SAMPLE_ENTRY_BYTES; bh.wavetable_entry_bytes = WAVETABLE_ENTRY_BYTES; ba = SAMPLE_CATALOGUE_START_ADDRESS; (void)sampleFlash_writeBytes(\&ba, (const uint8_t *)\&bh, sizeof(bh)); }|' \
	  $(ROOT)/Core/SampleRom/SampleInstall.c > build/break_cat/SampleInstall.c
	$(HOSTCC) $(HOSTFLAGS) -I$(ROOT)/Core/SampleRom $(CAT_HOST_INC) \
	  modules/samples.c $(ROOT)/Core/SampleRom/SampleMemory.c \
	  build/break_cat/SampleInstall.c catalogue_host.c \
	  -o build/samples_break -lm
	@if ./build/samples_break >/dev/null 2>&1; then \
	  echo "samples_break: FAILED (early-header break not caught)"; exit 1; \
	else echo "samples_break: caught (catalogue law 3 fails as expected)"; fi
	mkdir -p build/break_rows
	sed 's|#define SAMPLE_INSTALL_ROW_HALF 32 |#define SAMPLE_INSTALL_ROW_HALF 16 |' \
	  $(ROOT)/Core/SampleRom/SampleInstall.c > build/break_rows/SampleInstall.c
	@grep -q 'SAMPLE_INSTALL_ROW_HALF 16 ' build/break_rows/SampleInstall.c || \
	  { echo "samples_break_rows: sed did not apply"; exit 1; }
	$(HOSTCC) $(HOSTFLAGS) -I$(ROOT)/Core/SampleRom $(CAT_HOST_INC) \
	  modules/samples.c $(ROOT)/Core/SampleRom/SampleMemory.c \
	  build/break_rows/SampleInstall.c catalogue_host.c \
	  -o build/samples_break_rows -lm
	@if ./build/samples_break_rows >/dev/null 2>&1; then \
	  echo "samples_break_rows: FAILED (H = 16 kernel not caught)"; exit 1; \
	else echo "samples_break_rows: caught (rows law 4 fails as expected)"; fi

sampleoptimize:
	python3 modules/sampleoptimize.py

# S088 P0: gain-matched retrigger (slopeEg2_retriggerFrom) and the ramped
# post-chains (voicePostChain.h). Break: `currentGain / denom` -> `currentGain`,
# which must make law A1 fail.
run_modes: | $(GEN)
	$(EX) --src $(ROOT)/Core/DSPAudio/SlopeEg2.h \
	  --typedef SlopeEg2 --out $(GEN)/rm_slope_type.c
	$(EX) --src $(ROOT)/Core/DSPAudio/SlopeEg2.h \
	  --macro EG_STOPPED --macro EG_A --macro EG_D --macro EG_REPEAT \
	  --out $(GEN)/rm_slope_consts.c
	$(EX) --src $(ROOT)/Core/DSPAudio/SlopeEg2.c \
	  --func slopeEg2_setSlope --func slopeEg2_init \
	  --func slopeEg2_trigger \
	  --func slopeEg2_retriggerFrom --func slopeEg2_calcSlopeValue \
	  --func slopeEg2_calc \
	  --out $(GEN)/rm_slope_eg.c
	$(EX) --src $(ROOT)/Core/DSPAudio/SlopeEg2.h \
	  --func slopeEg2_isAudibleGain --out $(GEN)/rm_audible.c
	$(EX) --src $(ROOT)/Core/DSPAudio/BufferTools.h \
	  --func bufferTool_floatToInt16Store --func bufferTool_satAdd16 \
	  --func bufferTool_interpolatedGain --out $(GEN)/rm_buffertools.c
	$(EX) --src $(ROOT)/Core/DSPAudio/voicePostChain.h \
	  --func voicePost_drum --func voicePost_mixAddGainDist \
	  --func voicePost_addGainDist --out $(GEN)/rm_postchain.c
	$(HOSTCC) $(HOSTFLAGS) modules/run_modes.c -o build/run_modes -lm
	./build/run_modes
	mkdir -p build/break_rm
	sed 's|v = currentGain / denom;|v = currentGain;|' \
	  $(GEN)/rm_slope_eg.c > build/break_rm/rm_slope_eg.c
	$(HOSTCC) -Ibuild/break_rm $(HOSTFLAGS) modules/run_modes.c \
	  -o build/run_modes_break -lm
	@if ./build/run_modes_break >/dev/null 2>&1; then \
	  echo "run_modes_break: FAILED (gain-match break not caught)"; exit 1; \
	else echo "run_modes_break: caught (law A1 fails as expected)"; fi

special_tags:
	cd $(ROOT) && python3 tools/dsp_test/check_special_tags.py
	cd $(ROOT) && python3 tools/alkaline/make_init_alk.py --check

stock_tables:
	cd $(ROOT) && python3 tools/plots/gen_stock_tables.py --check

# S089 P2: the real voiceFold.c (ITCM sections stripped) against reference
# models; break proof: dropping the bias DC term ("t * p - kC" in
# voiceFold_sinLoop(), S089 P2 F4) must fail law F3.
voicefold: | $(GEN)
	$(EX) --src $(ROOT)/Core/DSPAudio/transientGenerator.h \
	  --typedef TransientGenerator --out $(GEN)/vf_transient.c
	$(EX) --src $(ROOT)/Core/DSPAudio/snapEg.h --typedef SnapEg --out $(GEN)/vf_snap.c
	$(EX) --src $(ROOT)/Core/DSPAudio/voiceFold.h --body --out $(GEN)/voiceFold.host.h
	$(EX) --src $(ROOT)/Core/DSPAudio/voiceFold.c --body --out $(GEN)/voiceFold.host.c
	$(EX) --src $(ROOT)/Core/DSPAudio/voiceFoldTables.c --body \
	  --out $(GEN)/voiceFoldTables.host.c
	$(HOSTCC) $(HOSTFLAGS) -Wno-unknown-attributes modules/voicefold.c \
	  -o build/voicefold -lm
	./build/voicefold
	mkdir -p build/break_vf
	sed 's/(t \* p - kC)/(t * p)/' $(GEN)/voiceFold.host.c \
	  > build/break_vf/voiceFold.host.c
	$(HOSTCC) -Ibuild/break_vf $(HOSTFLAGS) -Wno-unknown-attributes \
	  modules/voicefold.c -o build/voicefold_break -lm
	@if ./build/voicefold_break >/dev/null 2>&1; then \
	  echo "voicefold_break: FAILED (DC-term break not caught)"; exit 1; \
	else echo "voicefold_break: caught (law F3 fails as expected)"; fi

# S093 P0: the Alkaline engine (DiodeLadder.c, AlkalineControl.c,
# AlkalineVoice.c, the generated tables) over the whole real Oscillator.c and
# the real sawTable. Laws AL-B, AL-R, AL-S, AL-N, AL-AB (modules/alkaline.c).
# Break: the legato rule ignored (every note retriggers) must fail AL-S.
ALK := $(ROOT)/Core/DSP/Instruments/Alkaline
ALK_GEN := $(GEN)/DiodeLadder.host.h $(GEN)/DiodeLadder.host.c \
           $(GEN)/AlkalineControl.host.h $(GEN)/AlkalineControl.host.c \
           $(GEN)/AlkalineTables.host.h $(GEN)/AlkalineTables.host.c \
           $(GEN)/AlkalineSquareTable.host.c \
           $(GEN)/AlkalineVoice.host.h $(GEN)/AlkalineVoice.host.c \
	           $(GEN)/alk_sawTable.c $(GEN)/alk_triTable.c $(GEN)/oscPwmTables.host.h \
	           $(GEN)/oscPwmTables.host.c $(GEN)/oscPwmWarp.host.h
alkaline: $(GEN)/Oscillator.host.h $(GEN)/Oscillator.host.c \
	$(GEN)/oscPwmTables.host.h $(GEN)/oscPwmTables.host.c \
	$(GEN)/oscPwmWarp.host.h | $(GEN)
	$(EX) --src $(ROOT)/Core/DSPAudio/DiodeLadder.h --body --out $(GEN)/DiodeLadder.host.h
	$(EX) --src $(ROOT)/Core/DSPAudio/DiodeLadder.c --body --out $(GEN)/DiodeLadder.host.c
	$(EX) --src $(ALK)/AlkalineControl.h --body --out $(GEN)/AlkalineControl.host.h
	$(EX) --src $(ALK)/AlkalineControl.c --body --out $(GEN)/AlkalineControl.host.c
	$(EX) --src $(ALK)/AlkalineTables.h --body --out $(GEN)/AlkalineTables.host.h
	$(EX) --src $(ALK)/AlkalineTables.c --body --out $(GEN)/AlkalineTables.host.c
	$(EX) --src $(ALK)/AlkalinePostTables.h --body --out $(GEN)/AlkalinePostTables.host.h
	$(EX) --src $(ALK)/AlkalinePostTables.c --body --out $(GEN)/AlkalinePostTables.host.c
	$(EX) --src $(ALK)/AlkalineSquareTable.c --body --out $(GEN)/AlkalineSquareTable.host.c
	$(EX) --src $(ALK)/AlkalineVoice.h --body --out $(GEN)/AlkalineVoice.host.h
	$(EX) --src $(ALK)/AlkalineVoice.c --body --out $(GEN)/AlkalineVoice.host.c
	$(EX) --src $(ROOT)/Core/DSPAudio/wavetable.c --object sawTable --out $(GEN)/alk_sawTable.raw.c
	sed 's/const int16_t sawTable/int16_t sawTable/' $(GEN)/alk_sawTable.raw.c > $(GEN)/alk_sawTable.c
# S094 P4: the real triangle for the Tri laws (AL-XT, AL-PWW).
	$(EX) --src $(ROOT)/Core/DSPAudio/wavetable.c --object triTable --out $(GEN)/alk_triTable.raw.c
	sed 's/const int16_t triTable/int16_t triTable/' $(GEN)/alk_triTable.raw.c > $(GEN)/alk_triTable.c
# S094 P1 S5: the pwm read-form reference (ALK_PWM_READ_FORM 0, both reads
# interpolated) renders build/alk_pwm_ref.bin for law AL-PW (P-D6 evidence).
	$(HOSTCC) $(HOSTFLAGS) -DALK_PWM_READ_FORM=0 $(OSC_HOST_INC) modules/alkaline.c \
	  $(CAT_HOST_SRC) -o build/alkaline_pwm_ref -lm
	./build/alkaline_pwm_ref --pwm-render build/alk_pwm_ref.bin
	$(HOSTCC) $(HOSTFLAGS) $(OSC_HOST_INC) modules/alkaline.c $(CAT_HOST_SRC) \
	  -o build/alkaline -lm
	./build/alkaline
	mkdir -p build/break_alk
	sed 's/const uint8_t legato = (uint8_t)(voice->slideLatch \&\& voice->gateLeft > 0);/const uint8_t legato = 0u;/' \
	  $(GEN)/AlkalineVoice.host.c > build/break_alk/AlkalineVoice.host.c
	@grep -q 'legato = 0u;' build/break_alk/AlkalineVoice.host.c || \
	  { echo "alkaline_break: sed did not apply"; exit 1; }
	$(HOSTCC) -Ibuild/break_alk $(HOSTFLAGS) $(OSC_HOST_INC) modules/alkaline.c \
	  $(CAT_HOST_SRC) -o build/alkaline_break -lm
	@if ./build/alkaline_break >/dev/null 2>&1; then \
	  echo "alkaline_break: FAILED (legato break not caught)"; exit 1; \
	else echo "alkaline_break: caught (law AL-S fails as expected)"; fi
# S093 P8 revision 3: two break proofs for the wavetable glide and blend.
# (1) the blend's mix held at its start value (no per-sample ramp): law
#     AL-W "slot" must fail (the reference ramps from start to end).
	mkdir -p build/break_alk_wtb
	sed 's/const float ffStep = (end - start) \* (1.0f \/ (float)OUTPUT_DMA_SIZE);/const float ffStep = 0.0f;/' \
	  $(GEN)/AlkalineVoice.host.c > build/break_alk_wtb/AlkalineVoice.host.c
	@grep -q 'const float ffStep = 0.0f;' build/break_alk_wtb/AlkalineVoice.host.c || \
	  { echo "alkaline_break_wtb: sed did not apply"; exit 1; }
	$(HOSTCC) -Ibuild/break_alk_wtb $(HOSTFLAGS) $(OSC_HOST_INC) modules/alkaline.c \
	  $(CAT_HOST_SRC) -o build/alkaline_break_wtb -lm
	@if ./build/alkaline_break_wtb >/dev/null 2>&1; then \
	  echo "alkaline_break_wtb: FAILED (a blend without its ramp not caught)"; exit 1; \
	else echo "alkaline_break_wtb: caught (law AL-W fails as expected)"; fi
# (2) the glide may cross a frame boundary inside a block (the boundary
#     one frame further on): the position law of AL-W must fail.
	mkdir -p build/break_alk_glide
	sed 's/const float k = floorf(start) + 1.0f;/const float k = floorf(start) + 2.0f;/' \
	  $(GEN)/AlkalineVoice.host.c > build/break_alk_glide/AlkalineVoice.host.c
	@grep -q 'floorf(start) + 2.0f;' build/break_alk_glide/AlkalineVoice.host.c || \
	  { echo "alkaline_break_glide: sed did not apply"; exit 1; }
	$(HOSTCC) -Ibuild/break_alk_glide $(HOSTFLAGS) $(OSC_HOST_INC) modules/alkaline.c \
	  $(CAT_HOST_SRC) -o build/alkaline_break_glide -lm
	@if ./build/alkaline_break_glide >/dev/null 2>&1; then \
	  echo "alkaline_break_glide: FAILED (a glide across a frame boundary not caught)"; exit 1; \
	else echo "alkaline_break_glide: caught (law AL-W fails as expected)"; fi
# S094 P1 S4 breaks for P9-D7 (AL-OD0 must fail): (1) the tone not open at
# 127 (c + 0.001); (2) the overdrive gain not exactly 1 at 0 (x 1.0001).
	mkdir -p build/break_alk_tone build/break_alk_ovd
	sed 's/alkToneC\[ti\])/alkToneC[ti] + 0.001f)/' \
	  $(GEN)/AlkalineVoice.host.c > build/break_alk_tone/AlkalineVoice.host.c
	@grep -q 'alkToneC\[ti\] + 0.001f' build/break_alk_tone/AlkalineVoice.host.c || \
	  { echo "alkaline_break_tone: sed did not apply"; exit 1; }
	$(HOSTCC) -Ibuild/break_alk_tone $(HOSTFLAGS) $(OSC_HOST_INC) modules/alkaline.c \
	  $(CAT_HOST_SRC) -o build/alkaline_break_tone -lm
	@if ./build/alkaline_break_tone >/dev/null 2>&1; then \
	  echo "alkaline_break_tone: FAILED (a closed tone at 127 not caught)"; exit 1; \
	else echo "alkaline_break_tone: caught (law AL-OD0 fails as expected)"; fi
	sed 's/alkOvdGain\[oi\],/alkOvdGain[oi] * 1.0001f,/' \
	  $(GEN)/AlkalineVoice.host.c > build/break_alk_ovd/AlkalineVoice.host.c
	@grep -q 'alkOvdGain\[oi\] \* 1.0001f' build/break_alk_ovd/AlkalineVoice.host.c || \
	  { echo "alkaline_break_ovd: sed did not apply"; exit 1; }
	$(HOSTCC) -Ibuild/break_alk_ovd $(HOSTFLAGS) $(OSC_HOST_INC) modules/alkaline.c \
	  $(CAT_HOST_SRC) -o build/alkaline_break_ovd -lm
	@if ./build/alkaline_break_ovd >/dev/null 2>&1; then \
	  echo "alkaline_break_ovd: FAILED (an overdrive gain off 1 at 0 not caught)"; exit 1; \
	else echo "alkaline_break_ovd: caught (law AL-OD0 fails as expected)"; fi
# S094 P4 breaks: a swapped warp (ra / rb) must fail AL-PWW; the Tri wav
# read on the Saw row must fail AL-XT.
	mkdir -p build/break_alk_pwmw
	sed 's/osc_pwmWarp(ph, wPh, wRa, wRb)/osc_pwmWarp(ph, wPh, wRb, wRa)/' \
	  $(GEN)/DiodeLadder.host.c > build/break_alk_pwmw/DiodeLadder.host.c
	@grep -q 'osc_pwmWarp(ph, wPh, wRb, wRa)' build/break_alk_pwmw/DiodeLadder.host.c || \
	  { echo "alkaline_break_pwmw: sed did not apply"; exit 1; }
	$(HOSTCC) -Ibuild/break_alk_pwmw $(HOSTFLAGS) $(OSC_HOST_INC) modules/alkaline.c \
	  $(CAT_HOST_SRC) -o build/alkaline_break_pwmw -lm
	@if ./build/alkaline_break_pwmw >/dev/null 2>&1; then \
	  echo "alkaline_break_pwmw: FAILED (a swapped warp not caught)"; exit 1; \
	else echo "alkaline_break_pwmw: caught (law AL-PWW fails as expected)"; fi
	mkdir -p build/break_alk_tri
	sed 's/\[1024\])triTable/[1024])sawTable/' \
	  $(GEN)/AlkalineVoice.host.c > build/break_alk_tri/AlkalineVoice.host.c
	@grep -qF 'ALK_WAVE_TRI) ? (const int16_t (*)[1024])sawTable' \
	  build/break_alk_tri/AlkalineVoice.host.c || \
	  { echo "alkaline_break_tri: sed did not apply"; exit 1; }
	$(HOSTCC) -Ibuild/break_alk_tri $(HOSTFLAGS) $(OSC_HOST_INC) modules/alkaline.c \
	  $(CAT_HOST_SRC) -o build/alkaline_break_tri -lm
	@if ./build/alkaline_break_tri >/dev/null 2>&1; then \
	  echo "alkaline_break_tri: FAILED (a Tri read on the Saw row not caught)"; exit 1; \
	else echo "alkaline_break_tri: caught (law AL-XT fails as expected)"; fi

alkaline_tables:
	cd $(ROOT) && python3 tools/alkaline/gen_alk_tables.py --check

# S094 P1 S4: the post-filter and A3 tables (generator mirror and neutral
# entries).
alk_post_tables:
	cd $(ROOT) && python3 tools/alkaline/gen_alk_post_tables.py --check

# ---------------------------------------------------------------------------
# S093 P4: linked-image loop gates for the Alkaline integration.
#
# What:       elf_p4_snapshot writes the baseline loop dumps of a known-good
#             image (the P3 image, before any Alkaline code is linked):
#               - every oscillator, filter, transient and fold symbol
#                 (P4_OSC_SYMS), with elf_loopdump.py;
#               - mixer_calcNextSampleBlock(), whose voice renders are inlined.
#             elf_p4_check dumps the current image the same way and compares:
#               - elf_loopdump.py --compare: 0 MISSING per-sample loops
#                 (RENAMED passes; dispatch fragments may move);
#               - elf_hotloops.py --compare on the mixer: 0 GREW. LTO re-lays
#                 the mixer out when the set of inlined engines changes
#                 (P3 §2.3), so a GREW is checked by source pairing on the -g
#                 twins (elf_loopsrc.py, S093_P5 §3.2) before it is accepted or
#                 rejected.
# Why:        MEMORY.md (S089 ceiling): adding Alkaline must not lengthen any
#             other engine's per-sample loop. The S086 targets were removed in
#             f3805478; this is their replacement against a P4 baseline.
# Inputs:     FW_ELF (default: the firmware build output); P4BASE (the tracked
#             baseline folder).
# Outputs:    $(P4BASE)/osc.txt, $(P4BASE)/mixer.txt; build/p4_*_now.txt;
#             exit 1 on a MISSING or GREW.
# Accessors:  make -C tools/dsp_test elf_p4_snapshot | elf_p4_check.
# Affiliates: elf_loopdump.py, elf_hotloops.py, elf_loopsrc.py; S093_P3 §2.3.
FW_ELF ?= $(ROOT)/build/lxr02.elf
# The baselines are records, so they live in the tree, not in a build folder:
# the root `make clean` removes $(ROOT)/build (S093 P5). S093 P8 re-baselines
# the ten wavetable readers: removing their 8-bit copies (D9) and using the
# fixed 1,024-point read replaces their P4 loops by design.
P4BASE ?= baselines/s093p8
P4_OSC_SYMS := SVF_calcBlockZDF calcFmBlock calcNextOscSampleBlock \
  calcNextOscSampleRingBlock calcNextOscSampleSyncBlock calcNoiseRingBlock \
  calcSampleOscFmBlock calcSampleOscRingBlock calcSampleOscSyncBlock \
  calcSineRingBlock calcSineSyncBlock calcWavetableOscRingBlock \
  calcWavetableOscSyncBlock oscSection_renderOldCarrier osc_buildLoopSyncList \
  osc_buildSyncList osc_renderTypedFmRamp osc_setFreq osc_slpFmBlock \
  osc_slpFmRampBlock osc_slpPlainBlock osc_slpRingBlock osc_slpSyncBlock \
  osc_smpFmBlock osc_smpFmRampBlock osc_smpPlainBlock osc_smpRingBlock \
  osc_smpSyncBlock osc_wtbBlendBlock osc_wtbFmBlendBlock osc_wtbFmBlendRampBlock \
  osc_wtbFmBlock osc_wtbFmRampBlock osc_wtbPlainBlock osc_wtbPlanBlock \
  osc_wtbRingBlendBlock osc_wtbRingBlock osc_wtbSyncBlendBlock osc_wtbSyncBlock \
  transient_calcUserBlock voiceFold_asnLoop voiceFold_chbLoop voiceFold_fd2Loop \
  voiceFold_process voiceFold_sinLoop voiceFold_triLoop busComp_processBlock \
  osc_pwmDPlainBlock osc_pwmWtPlainBlock osc_pwmWsPlainBlock \
  osc_pwmDFmBlock osc_pwmWtFmBlock osc_pwmWsFmBlock \
  osc_pwmDFmRampBlock osc_pwmWtFmRampBlock osc_pwmWsFmRampBlock \
  osc_pwmDRingBlock osc_pwmWtRingBlock osc_pwmWsRingBlock \
  osc_pwmDSyncBlock osc_pwmWtSyncBlock osc_pwmWsSyncBlock
# S094 P1 S3: the fifteen PWM loops (.itcm.s094p1) join the gate from the s3
# snapshot on, so S4/S5 must leave them unchanged.

elf_p4_snapshot:
	mkdir -p $(P4BASE)
	python3 elf_loopdump.py --elf $(FW_ELF) $(foreach s,$(P4_OSC_SYMS),--sym $(s)) \
	  --out $(P4BASE)/osc.txt
	python3 elf_loopdump.py --elf $(FW_ELF) --sym mixer_calcNextSampleBlock \
	  --out $(P4BASE)/mixer.txt

elf_p4_check:
	mkdir -p build
	python3 elf_loopdump.py --elf $(FW_ELF) $(foreach s,$(P4_OSC_SYMS),--sym $(s)) \
	  --out build/p4_osc_now.txt
	python3 elf_loopdump.py --compare $(P4BASE)/osc.txt build/p4_osc_now.txt
	python3 elf_loopdump.py --elf $(FW_ELF) --sym mixer_calcNextSampleBlock \
	  --out build/p4_mixer_now.txt
	python3 elf_hotloops.py --compare $(P4BASE)/mixer.txt build/p4_mixer_now.txt

fold_tables:
	cd $(ROOT) && python3 tools/plots/gen_fold_tables.py --check

pwm_tables:
	cd $(ROOT) && python3 tools/plots/gen_pwm_tables.py --check

# S091 P1/P2/P3: the real CrumpBitEffect.c and its generated tables against
# the references in modules/crumpbit.c (laws CB1-CB22; CB1-CB4, CB10, CB16,
# CB17 and the CB15/CB19 identity laws bit-exact).
# Break proofs:
#   - S091 P6 (replacing the S091 P2 Sync-key break; the Sync search is
#     gone): the `dly` curve's top step ratio with one square root instead
#     of two ((t_max / t_/2)^(1/2), so rows 125..127 all play t_max) must
#     fail CB1;
#   - the AC memory seeded 0 instead of 128 (a full-scale step on the first
#     sample after init) must fail CB4/CB5;
#   - the flat tone weighted (1, 1) instead of (0, 1) (the return would be
#     s + v, not v) must fail CB4;
#   - the seat without its delay clamp (a share shrink leaves the delay
#     longer than the loop) must fail the CB11 seat law (S091 P4: made
#     deterministic; CB4/CB11 met it only by chance);
#   - the Rate-limit search without its eighth probe (seven steps reach
#     only 127 entries, so the whole arena would give 126) must fail CB6;
#   - S091 P3: the `rvb` wet added around the mix stage (mix 0 no longer
#     the dry signal) must fail CB10;
#   - the `grn` freeze ignored (the record line keeps recording) must fail
#     CB14;
#   - the `pit` phase step with its sign flipped (+12 st plays -12) must
#     fail CB13;
#   - a granular row on a `pit` lane must fail CB9;
#   - S091 P4: a grain fade-out that falls twice as fast as its fade-in
#     (back-to-back grains no longer sum to 1) must fail CB14's copy
#     identity;
#   - the left channel's filter built from the right channel's cutoff must
#     fail CB16;
#   - the grain overwrite clamp removed (grains read into unrecorded or
#     overwritten audio) must fail CB14's read-safety law;
#   - S091 P5: the data-line table built with OR instead of EOR (inverted
#     lines forced high) must fail CB17;
#   - the `grn` echo on the shared 0..1.2 curve instead of 0..0.98 (the
#     echo grows) must fail CB18;
#   - the SVF -> one-pole hand-over skipped (the one-pole starts from the
#     band integrator) must fail CB19;
#   - a grain's span not rounded to a block boundary must fail CB14's
#     block-aligned law;
#   - the one-pole's left k taken from the right cutoff must fail CB16 on
#     `rvb`;
#   - the ages of a dropped or retried trigger not put back to the block
#     start (the P4 defect) must fail CB14's block-aligned law;
#   - S091 P5 follow-up: the `pit` window glide removed (the window jumps
#     to each `win` value, as before) must fail CB21's step law;
#   - the `grn` Q3.12 step not held at 32,767 (+36 st overflows int16 and
#     plays reversed at a low pitch) must fail CB20;
#   - `pit`'s fine pitch ignored (the ratio without midiPitch_detune())
#     must fail CB22;
#   - S091 P6: the `dly` knob limit counting the t_max plateau (the search
#     on <= instead of <, so below 42 BPM the knob travels through rows that
#     all play t_max, the dead travel of R6) must fail CB1;
#   - the `grn` tap fade removed (the gain holds its block-start value, so
#     a delay change steps) must fail CB23;
#   - `grn` rows 0..3 all at the 2-sample minimum must fail CB23;
#   - the `grn` tap's two line guards removed (the reseat no longer
#     silences the tap, and the block no longer holds tap_d inside the
#     line), so an old D outlives a shorter line, must fail CB23 (either
#     guard alone keeps D inside the line).
CB_GEN := $(GEN)/cb_fxbuf.c $(GEN)/cb_efftypes.c $(GEN)/cb_effects.c \
          $(GEN)/cb_params.c \
          $(GEN)/cb_stepscale.c $(GEN)/cb_stepscale.h \
          $(GEN)/cb_tables.h $(GEN)/cb_tables.c $(GEN)/cb_midipitch.c \
          $(GEN)/SpaceKit.host.h $(GEN)/SpaceKit.host.c \
          $(GEN)/SpaceKitDsp.host.c \
          $(GEN)/CrumpBitEffect.host.h $(GEN)/CrumpBitEffect.host.c
crumpbit: | $(GEN)
	$(EX) --src $(ROOT)/Core/DSP/Effects/FxBuffer.h \
	  --typedef fx_share_t --typedef fxbuf_handoff_t \
	  --macro FXBUF_ALIGN_BYTES \
	  --macro FXBUF_VOICE_UNIT_COUNT --macro FXBUF_VOICE_UNIT_SAMPLES \
	  --macro FXBUF_VOICE_UNIT_BYTES --macro FXBUF_EFFECT_POINTER_COUNT \
	  --macro FXBUF_HANDOFF_POINTER_COUNT \
	  --macro FXBUF_HANDOFF_EFFECT_POINTER_BASE \
	  --macro FXBUF_STATE_EFFECT_WRITTEN \
	  --macro FXBUF_SEG_SIDE_L --macro FXBUF_SEG_SIDE_R \
	  --macro FXBUF_SEG_SIDE_BOTH --macro FXBUF_SEG_SIDE_MASK \
	  --macro FXBUF_SEG_SIDE_SHIFT --macro FXBUF_UQ15_ONE \
	  --macro FXBUF_OFFSET_NONE --out $(GEN)/cb_fxbuf.c
	$(EX) --src $(ROOT)/Core/DSP/Effects/EffectTypes.h \
	  --macro EFFECT_SEQ_LANE_COUNT --macro EFFECT_TYPE_OFF \
	  --out $(GEN)/cb_efftypes.c
	$(EX) --src $(ROOT)/Core/DSP/Effects/EffectsManager.h \
	  --macro EFFECT_LANE_MORPH_SOURCE --macro EFFECT_LANE_NONE \
	  --macro EFFECT_TYPE_CRUMPBIT \
	  --typedef effect_io_t --typedef effect_type_ops_t \
	  --out $(GEN)/cb_effects.c
	$(EX) --src $(ROOT)/Core/DSP/Effects/CrumpBit/CrumpBitParameters.h \
	  --typedef crumpbit_param_t --macro CRUMPBIT_PARAM_COUNT \
	  --macro CRUMPBIT_ROW_BIT --macro CRUMPBIT_ROWS_SHARED \
	  --macro CRUMPBIT_ROWS_SUBTYPE --macro CRUMPBIT_ROWS_GRN \
	  --macro CRUMPBIT_SUBTYPE_DELAY --macro CRUMPBIT_SUBTYPE_COMB \
	  --macro CRUMPBIT_SUBTYPE_PITCH --macro CRUMPBIT_SUBTYPE_GRAIN \
	  --macro CRUMPBIT_SUBTYPE_REVERB --macro CRUMPBIT_SUBTYPE_COUNT \
	  --macro CRUMPBIT_TONE_FLAT \
	  --macro CRUMPBIT_PITCH_ROW_MIN --macro CRUMPBIT_PITCH_ROW_MAX \
	  --macro CRUMPBIT_FILTER_LP --macro CRUMPBIT_FILTER_HP \
	  --macro CRUMPBIT_FILTER_BP --macro CRUMPBIT_FILTER_UBP \
	  --macro CRUMPBIT_FILTER_NOTCH --macro CRUMPBIT_FILTER_PEAK \
	  --macro CRUMPBIT_FILTER_LP2 --macro CRUMPBIT_FILTER_OFF \
	  --out $(GEN)/cb_params.c
	$(EX) --src $(ROOT)/Core/DSP/Effects/CrumpBit/CrumpBitTables.h --body \
	  --out $(GEN)/cb_tables.h
	$(EX) --src $(ROOT)/Core/DSP/Effects/CrumpBit/CrumpBitTables.c --body \
	  --out $(GEN)/cb_tables.c
	$(EX) --src $(ROOT)/Core/Sequencer/StepScale.h \
	  --macro STEP_SCALE_COUNT --macro STEP_SCALE_DEFAULT \
	  --out $(GEN)/cb_stepscale.h
	$(EX) --src $(ROOT)/Core/MIDI/MidiPitch.h \
	  --macro MIDI_PITCH_SEMITONE_UP --func midiPitch_detune \
	  --out $(GEN)/cb_midipitch.c
	$(EX) --src $(ROOT)/Core/Sequencer/StepScale.c \
	  --object stepScale_q8Table --func stepScale_ticksQ8 \
	  --out $(GEN)/cb_stepscale.c
	$(EX) --src $(ROOT)/Core/DSP/Effects/Space/SpaceKit.h --body \
	  --out $(GEN)/SpaceKit.host.h
	$(EX) --src $(ROOT)/Core/DSP/Effects/Space/SpaceKit.c --body \
	  --out $(GEN)/SpaceKit.host.c
	$(EX) --src $(ROOT)/Core/DSP/Effects/Space/SpaceKitDsp.c --body \
	  --out $(GEN)/SpaceKitDsp.host.c
	$(EX) --src $(ROOT)/Core/DSP/Effects/CrumpBit/CrumpBitEffect.h --body \
	  --out $(GEN)/CrumpBitEffect.host.h
	$(EX) --src $(ROOT)/Core/DSP/Effects/CrumpBit/CrumpBitEffect.c --body \
	  --out $(GEN)/CrumpBitEffect.host.c
	$(HOSTCC) $(HOSTFLAGS) modules/crumpbit.c -o build/crumpbit -lm
	./build/crumpbit
	mkdir -p build/break_cb1 build/break_cb2
	sed 's/c->q_hi = sqrtf(sqrtf(/c->q_hi = (sqrtf(/' \
	  $(GEN)/CrumpBitEffect.host.c > build/break_cb1/CrumpBitEffect.host.c
	@grep -q 'c->q_hi = (sqrtf(' build/break_cb1/CrumpBitEffect.host.c
	$(HOSTCC) -Ibuild/break_cb1 $(HOSTFLAGS) modules/crumpbit.c \
	  -o build/crumpbit_break1 -lm
	@if ./build/crumpbit_break1 >/dev/null 2>&1; then \
	  echo "crumpbit_break1: FAILED (dly curve break not caught)"; \
	  exit 1; \
	else echo "crumpbit_break1: caught (law CB1 fails as expected)"; fi
	sed 's/rt->ac_c_l = CRUMPBIT_CODE_MID;/rt->ac_c_l = 0;/' \
	  $(GEN)/CrumpBitEffect.host.c > build/break_cb2/CrumpBitEffect.host.c
	@grep -q 'rt->ac_c_l = 0;' build/break_cb2/CrumpBitEffect.host.c
	$(HOSTCC) -Ibuild/break_cb2 $(HOSTFLAGS) modules/crumpbit.c \
	  -o build/crumpbit_break2 -lm
	@if ./build/crumpbit_break2 >/dev/null 2>&1; then \
	  echo "crumpbit_break2: FAILED (AC-seed break not caught)"; exit 1; \
	else echo "crumpbit_break2: caught (law CB4/CB5 fails as expected)"; fi
	mkdir -p build/break_cb3 build/break_cb4
	sed 's/((t == CRUMPBIT_TONE_FLAT) ? 0.0f : -1.0f)/((t == CRUMPBIT_TONE_FLAT) ? 1.0f : -1.0f)/' \
	  $(GEN)/CrumpBitEffect.host.c > build/break_cb3/CrumpBitEffect.host.c
	@grep -q '(t == CRUMPBIT_TONE_FLAT) ? 1.0f' \
	  build/break_cb3/CrumpBitEffect.host.c
	$(HOSTCC) -Ibuild/break_cb3 $(HOSTFLAGS) modules/crumpbit.c \
	  -o build/crumpbit_break3 -lm
	@if ./build/crumpbit_break3 >/dev/null 2>&1; then \
	  echo "crumpbit_break3: FAILED (flat-tone break not caught)"; exit 1; \
	else echo "crumpbit_break3: caught (law CB4 fails as expected)"; fi
	sed 's/t->delay = (float)(len - CRUMPBIT_LOOP_GUARD);/(void)0;/' \
	  $(GEN)/CrumpBitEffect.host.c > build/break_cb4/CrumpBitEffect.host.c
	@grep -q '(void)0;' build/break_cb4/CrumpBitEffect.host.c
	$(HOSTCC) -Ibuild/break_cb4 $(HOSTFLAGS) modules/crumpbit.c \
	  -o build/crumpbit_break4 -lm
	@if ./build/crumpbit_break4 >/dev/null 2>&1; then \
	  echo "crumpbit_break4: FAILED (seat delay-clamp break not caught)"; \
	  exit 1; \
	else echo "crumpbit_break4: caught (law CB11 seat fails as expected)"; fi
	mkdir -p build/break_cb5
	sed 's/    count += (table\[count\] <= max_samples) ? 1u : 0u;/    (void)0;/' \
	  $(GEN)/CrumpBitEffect.host.c > build/break_cb5/CrumpBitEffect.host.c
	@grep -q '    (void)0;' build/break_cb5/CrumpBitEffect.host.c
	$(HOSTCC) -Ibuild/break_cb5 $(HOSTFLAGS) modules/crumpbit.c \
	  -o build/crumpbit_break5 -lm
	@if ./build/crumpbit_break5 >/dev/null 2>&1; then \
	  echo "crumpbit_break5: FAILED (Rate-limit probe break not caught)"; \
	  exit 1; \
	else echo "crumpbit_break5: caught (law CB6 fails as expected)"; fi
	mkdir -p build/break_cb6 build/break_cb7 build/break_cb8 build/break_cb9
	sed 's/left\[n\] = crumpBit_mixFrame(hl, mid_k \* (e + o) + side_k \* (e - o),/left[n] = mid_k * (e + o) + crumpBit_mixFrame(hl, side_k * (e - o),/' \
	  $(GEN)/CrumpBitEffect.host.c > build/break_cb6/CrumpBitEffect.host.c
	@grep -q 'left\[n\] = mid_k \* (e + o) + crumpBit_mixFrame' \
	  build/break_cb6/CrumpBitEffect.host.c
	$(HOSTCC) -Ibuild/break_cb6 $(HOSTFLAGS) modules/crumpbit.c \
	  -o build/crumpbit_break6 -lm
	@if ./build/crumpbit_break6 >/dev/null 2>&1; then \
	  echo "crumpbit_break6: FAILED (wet-around-mix break not caught)"; \
	  exit 1; \
	else echo "crumpbit_break6: caught (law CB10 fails as expected)"; fi
	sed 's/k.rec_w = frozen ? k.dline : b->base;/k.rec_w = b->base;/' \
	  $(GEN)/CrumpBitEffect.host.c > build/break_cb7/CrumpBitEffect.host.c
	@grep -q 'k.rec_w = b->base;' build/break_cb7/CrumpBitEffect.host.c
	$(HOSTCC) -Ibuild/break_cb7 $(HOSTFLAGS) modules/crumpbit.c \
	  -o build/crumpbit_break7 -lm
	@if ./build/crumpbit_break7 >/dev/null 2>&1; then \
	  echo "crumpbit_break7: FAILED (freeze break not caught)"; exit 1; \
	else echo "crumpbit_break7: caught (law CB14 fails as expected)"; fi
	sed 's/((1.0f - crumpBit_ratioOf(semis) \*/((-1.0f + crumpBit_ratioOf(semis) */' \
	  $(GEN)/CrumpBitEffect.host.c > build/break_cb8/CrumpBitEffect.host.c
	@grep -q '((-1.0f + crumpBit_ratioOf(semis) \*' \
	  build/break_cb8/CrumpBitEffect.host.c
	$(HOSTCC) -Ibuild/break_cb8 $(HOSTFLAGS) modules/crumpbit.c \
	  -o build/crumpbit_break8 -lm
	@if ./build/crumpbit_break8 >/dev/null 2>&1; then \
	  echo "crumpbit_break8: FAILED (pitch direction break not caught)"; \
	  exit 1; \
	else echo "crumpbit_break8: caught (law CB13 fails as expected)"; fi
	sed 's/        CRUMPBIT_PARAM_AUX, EFFECT_LANE_NONE },/        CRUMPBIT_PARAM_GRAIN_ST2, EFFECT_LANE_NONE },/' \
	  $(GEN)/CrumpBitEffect.host.c > build/break_cb9/CrumpBitEffect.host.c
	@grep -q 'CRUMPBIT_PARAM_GRAIN_ST2, EFFECT_LANE_NONE },' \
	  build/break_cb9/CrumpBitEffect.host.c
	$(HOSTCC) -Ibuild/break_cb9 $(HOSTFLAGS) modules/crumpbit.c \
	  -o build/crumpbit_break9 -lm
	@if ./build/crumpbit_break9 >/dev/null 2>&1; then \
	  echo "crumpbit_break9: FAILED (lane-map break not caught)"; exit 1; \
	else echo "crumpbit_break9: caught (law CB9 fails as expected)"; fi
	mkdir -p build/break_cb10 build/break_cb11 build/break_cb12
	sed 's/        s->out_a -= CRUMPBIT_GRAIN_FADE_K;/        s->out_a -= 2.0f * CRUMPBIT_GRAIN_FADE_K;/' \
	  $(GEN)/CrumpBitEffect.host.c > build/break_cb10/CrumpBitEffect.host.c
	@grep -q 's->out_a -= 2.0f \* CRUMPBIT_GRAIN_FADE_K;' \
	  build/break_cb10/CrumpBitEffect.host.c
	$(HOSTCC) -Ibuild/break_cb10 $(HOSTFLAGS) modules/crumpbit.c \
	  -o build/crumpbit_break10 -lm
	@if ./build/crumpbit_break10 >/dev/null 2>&1; then \
	  echo "crumpbit_break10: FAILED (grain fade break not caught)"; \
	  exit 1; \
	else echo "crumpbit_break10: caught (law CB14 fails as expected)"; fi
	sed 's/crumpBit_filterCoeffs(crumpBit_filterG\[fl\], r, &a1l/crumpBit_filterCoeffs(crumpBit_filterG[fr], r, \&a1l/' \
	  $(GEN)/CrumpBitEffect.host.c > build/break_cb11/CrumpBitEffect.host.c
	@grep -q 'crumpBit_filterG\[fr\], r, &a1l' \
	  build/break_cb11/CrumpBitEffect.host.c
	$(HOSTCC) -Ibuild/break_cb11 $(HOSTFLAGS) modules/crumpbit.c \
	  -o build/crumpbit_break11 -lm
	@if ./build/crumpbit_break11 >/dev/null 2>&1; then \
	  echo "crumpbit_break11: FAILED (bal side break not caught)"; \
	  exit 1; \
	else echo "crumpbit_break11: caught (law CB16 fails as expected)"; fi
	sed 's/        span = (span > s_max) ? s_max : span;/        (void)s_max;/' \
	  $(GEN)/CrumpBitEffect.host.c > build/break_cb12/CrumpBitEffect.host.c
	@grep -q '        (void)s_max;' build/break_cb12/CrumpBitEffect.host.c
	$(HOSTCC) -Ibuild/break_cb12 $(HOSTFLAGS) modules/crumpbit.c \
	  -o build/crumpbit_break12 -lm
	@if ./build/crumpbit_break12 >/dev/null 2>&1; then \
	  echo "crumpbit_break12: FAILED (overwrite clamp break not caught)"; \
	  exit 1; \
	else echo "crumpbit_break12: caught (law CB14 fails as expected)"; fi
	mkdir -p build/break_cb13 build/break_cb14 build/break_cb15
	sed 's/        crumpBit_dataLines\[c\] = (uint8_t)((c \& keep) ^ inv);/        crumpBit_dataLines[c] = (uint8_t)((c \& keep) | inv);/' \
	  $(GEN)/CrumpBitEffect.host.c > build/break_cb13/CrumpBitEffect.host.c
	@grep -q '(uint8_t)((c & keep) | inv);' \
	  build/break_cb13/CrumpBitEffect.host.c
	$(HOSTCC) -Ibuild/break_cb13 $(HOSTFLAGS) modules/crumpbit.c \
	  -o build/crumpbit_break13 -lm
	@if ./build/crumpbit_break13 >/dev/null 2>&1; then \
	  echo "crumpbit_break13: FAILED (data-line table break not caught)"; \
	  exit 1; \
	else echo "crumpbit_break13: caught (law CB17 fails as expected)"; fi
	sed 's/    s.fb = crumpBit_grainFeedbackGain(rt->feedback_raw);/    s.fb = crumpBit_feedbackGain(rt->feedback_raw);/' \
	  $(GEN)/CrumpBitEffect.host.c > build/break_cb14/CrumpBitEffect.host.c
	@grep -q '    s.fb = crumpBit_feedbackGain(rt->feedback_raw);' \
	  build/break_cb14/CrumpBitEffect.host.c
	$(HOSTCC) -Ibuild/break_cb14 $(HOSTFLAGS) modules/crumpbit.c \
	  -o build/crumpbit_break14 -lm
	@if ./build/crumpbit_break14 >/dev/null 2>&1; then \
	  echo "crumpbit_break14: FAILED (grn echo curve break not caught)"; \
	  exit 1; \
	else echo "crumpbit_break14: caught (law CB18 fails as expected)"; fi
	# S094 P2 AS-13: the one SVF keeps its state across a sub-type switch; a
	# switch that clears it must fail CB19 (c). (Was the S091 P5 F1
	# one-pole hand-over break; that code is gone.)
	sed 's/^    crumpBit_bytePosition(rt, &pos, &written);$$/    crumpBit_bytePosition(rt, \&pos, \&written); rt->svf_l1 = 0.0f;/' \
	  $(GEN)/CrumpBitEffect.host.c > build/break_cb15/CrumpBitEffect.host.c
	@grep -q '&written); rt->svf_l1 = 0.0f;' \
	  build/break_cb15/CrumpBitEffect.host.c
	$(HOSTCC) -Ibuild/break_cb15 $(HOSTFLAGS) modules/crumpbit.c \
	  -o build/crumpbit_break15 -lm
	@if ./build/crumpbit_break15 >/dev/null 2>&1; then \
	  echo "crumpbit_break15: FAILED (filter state on a switch not caught)"; \
	  exit 1; \
	else echo "crumpbit_break15: caught (law CB19 fails as expected)"; fi
	mkdir -p build/break_cb16 build/break_cb17 build/break_cb18
	sed 's/~(CRUMPBIT_GRAIN_MIN_PERIOD - 1u)) - ev;/0xFFFFFFFFu) - ev;/' \
	  $(GEN)/CrumpBitEffect.host.c > build/break_cb16/CrumpBitEffect.host.c
	@grep -q '0xFFFFFFFFu) - ev;' \
	  build/break_cb16/CrumpBitEffect.host.c
	$(HOSTCC) -Ibuild/break_cb16 $(HOSTFLAGS) modules/crumpbit.c \
	  -o build/crumpbit_break16 -lm
	@if ./build/crumpbit_break16 >/dev/null 2>&1; then \
	  echo "crumpbit_break16: FAILED (grain block-end break not caught)"; \
	  exit 1; \
	else echo "crumpbit_break16: caught (law CB14 fails as expected)"; fi
	# break_cb17 (the S091 P5 one-pole's `bal` side) went with the one-pole in
	# S094 P2 AS-13; the SVF's `bal` side break above covers every sub-type.
	sed 's/        age_a = g->age_a;/        (void)0;/' \
	  $(GEN)/CrumpBitEffect.host.c > build/break_cb18/CrumpBitEffect.host.c
	@grep -q '        (void)0;' \
	  build/break_cb18/CrumpBitEffect.host.c
	$(HOSTCC) -Ibuild/break_cb18 $(HOSTFLAGS) modules/crumpbit.c \
	  -o build/crumpbit_break18 -lm
	@if ./build/crumpbit_break18 >/dev/null 2>&1; then \
	  echo "crumpbit_break18: FAILED (dropped-trigger age break not caught)"; \
	  exit 1; \
	else echo "crumpbit_break18: caught (law CB14 fails as expected)"; fi
	mkdir -p build/break_cb19 build/break_cb20
	sed 's/#define CRUMPBIT_PITCH_WIN_GLIDE      0.03563f/#define CRUMPBIT_PITCH_WIN_GLIDE      1.0f/' \
	  $(GEN)/CrumpBitEffect.host.c > build/break_cb19/CrumpBitEffect.host.c
	@grep -q 'CRUMPBIT_PITCH_WIN_GLIDE      1.0f' \
	  build/break_cb19/CrumpBitEffect.host.c
	$(HOSTCC) -Ibuild/break_cb19 $(HOSTFLAGS) modules/crumpbit.c \
	  -o build/crumpbit_break19 -lm
	@if ./build/crumpbit_break19 >/dev/null 2>&1; then \
	  echo "crumpbit_break19: FAILED (pit window glide break not caught)"; \
	  exit 1; \
	else echo "crumpbit_break19: caught (law CB21 fails as expected)"; fi
	sed 's/        q = (q > 32767) ? 32767 : q;/        (void)0;/' \
	  $(GEN)/CrumpBitEffect.host.c > build/break_cb20/CrumpBitEffect.host.c
	@grep -q '        (void)0;' \
	  build/break_cb20/CrumpBitEffect.host.c
	$(HOSTCC) -Ibuild/break_cb20 $(HOSTFLAGS) modules/crumpbit.c \
	  -o build/crumpbit_break20 -lm
	@if ./build/crumpbit_break20 >/dev/null 2>&1; then \
	  echo "crumpbit_break20: FAILED (grn step guard break not caught)"; \
	  exit 1; \
	else echo "crumpbit_break20: caught (law CB20 fails as expected)"; fi
	mkdir -p build/break_cb21
	sed 's/midiPitch_detune(rt->pit_fine)) \//1.0f) \//' \
	  $(GEN)/CrumpBitEffect.host.c > build/break_cb21/CrumpBitEffect.host.c
	@grep -q '1.0f) /' build/break_cb21/CrumpBitEffect.host.c
	$(HOSTCC) -Ibuild/break_cb21 $(HOSTFLAGS) modules/crumpbit.c \
	  -o build/crumpbit_break21 -lm
	@if ./build/crumpbit_break21 >/dev/null 2>&1; then \
	  echo "crumpbit_break21: FAILED (pit fine break not caught)"; \
	  exit 1; \
	else echo "crumpbit_break21: caught (law CB22 fails as expected)"; fi
	mkdir -p build/break_cb22
	sed 's/) < m)/) <= m)/' \
	  $(GEN)/CrumpBitEffect.host.c > build/break_cb22/CrumpBitEffect.host.c
	@test "$$(grep -c ') <= m)' build/break_cb22/CrumpBitEffect.host.c)" = 2
	$(HOSTCC) -Ibuild/break_cb22 $(HOSTFLAGS) modules/crumpbit.c \
	  -o build/crumpbit_break22 -lm
	@if ./build/crumpbit_break22 >/dev/null 2>&1; then \
	  echo "crumpbit_break22: FAILED (dly plateau limit break not caught)"; \
	  exit 1; \
	else echo "crumpbit_break22: caught (law CB1 fails as expected)"; fi
	mkdir -p build/break_cb23 build/break_cb24 build/break_cb25
	sed 's/k\.tap_inc = ((float)on - s\.tap_g) \* b->per_frame;/k.tap_inc = 0.0f;/' \
	  $(GEN)/CrumpBitEffect.host.c > build/break_cb23/CrumpBitEffect.host.c
	@grep -q 'k.tap_inc = 0.0f;' build/break_cb23/CrumpBitEffect.host.c
	$(HOSTCC) -Ibuild/break_cb23 $(HOSTFLAGS) modules/crumpbit.c \
	  -o build/crumpbit_break23 -lm
	@if ./build/crumpbit_break23 >/dev/null 2>&1; then \
	  echo "crumpbit_break23: FAILED (grn tap fade break not caught)"; \
	  exit 1; \
	else echo "crumpbit_break23: caught (law CB23 fails as expected)"; fi
	sed 's/d = crumpBit_lowRowSamples(q, row);/d = crumpBit_lowRowSamples(q, 0u);/' \
	  $(GEN)/CrumpBitEffect.host.c > build/break_cb24/CrumpBitEffect.host.c
	@grep -q 'crumpBit_lowRowSamples(q, 0u);' \
	  build/break_cb24/CrumpBitEffect.host.c
	$(HOSTCC) -Ibuild/break_cb24 $(HOSTFLAGS) modules/crumpbit.c \
	  -o build/crumpbit_break24 -lm
	@if ./build/crumpbit_break24 >/dev/null 2>&1; then \
	  echo "crumpbit_break24: FAILED (grn low rows break not caught)"; \
	  exit 1; \
	else echo "crumpbit_break24: caught (law CB23 fails as expected)"; fi
	sed -e '/S091 P6: the tap restarts silent/{n;s/g->tap_on = 0u;/(void)0;/;}' \
	  -e 's/(!g->tap_on || g->tap_d > len - 2u)/(!g->tap_on)/' \
	  $(GEN)/CrumpBitEffect.host.c > build/break_cb25/CrumpBitEffect.host.c
	@test "$$(grep -c 'g->tap_on = 0u;' build/break_cb25/CrumpBitEffect.host.c)" = 0
	@grep -q '(!g->tap_on) ?' build/break_cb25/CrumpBitEffect.host.c
	$(HOSTCC) -Ibuild/break_cb25 $(HOSTFLAGS) modules/crumpbit.c \
	  -o build/crumpbit_break25 -lm
	@if ./build/crumpbit_break25 >/dev/null 2>&1; then \
	  echo "crumpbit_break25: FAILED (grn reseat tap break not caught)"; \
	  exit 1; \
	else echo "crumpbit_break25: caught (law CB23 fails as expected)"; fi

# S091 P2: the committed CrumpBit tables must equal the generator's output.
crumpbit_tables:
	cd $(ROOT) && python3 tools/plots/gen_crumpbit_tables.py --check

# S091 ARM gate (needs `make all` at the repository root first):
#   - each sub-engine's frame loop (stages 1-3, one symbol each,
#     CRUMPBIT_NOINLINE) at or below its measured longest path, no VDIV
#     (S091 P5 limits, measured on the implemented tree): `dly` 109 (S091
#     P6; P5 111) and `cmb` 109 (engine A with the Q14 read; P4 124), `pit` 106 (drop-sample
#     heads; P4 135), `grn` 114 (two spans around the block's one trigger or
#     steal; no window lower clamp, plain echo; P4 136; S091 P6 the tap
#     gain multiply in place of the gate mask, P5 116; --cond-heads
#     --in-range: its cold blocks close backward branches into the body,
#     which are not frame loops), `rvb` 131 (low-pass damping; P4 145);
#   - crumpBit_process(): the stage-4 output-filter loops, the SVF 31 (S091
#     P2; loop 1) and the one-pole 16 (S091 P5 F1; --rank 2);
#   - the table-limit search crumpBit_tableLimit() at or below 63 (S091
#     P3: the S091 P2 crumpBit_rateLimit() search generalised to every
#     sub-type's table and kept out of line; eight probes, run once per
#     block by effects_service() through effective_max), and (S091 P6)
#     `dly`'s curve-limit search crumpBit_delayLimit() at or below 152 on
#     its longest per-block path (loop-free: eight probes of the curve at
#     the tempo, unrolled; it replaced the Sync search
#     crumpBit_divisionFor(), 117, and the `dly` use of the table search).
crumpbit_elf_check:
	python3 elf_looppath.py --elf $(ROOT)/build/lxr02.elf \
	  --sym crumpBit_runDelay --max 109
	python3 elf_looppath.py --elf $(ROOT)/build/lxr02.elf \
	  --sym crumpBit_runComb --max 109
	python3 elf_looppath.py --elf $(ROOT)/build/lxr02.elf \
	  --sym crumpBit_runPitch --max 106
	python3 elf_looppath.py --elf $(ROOT)/build/lxr02.elf \
	  --sym crumpBit_runGrain --cond-heads --in-range --top 2 --max 114
	python3 elf_looppath.py --elf $(ROOT)/build/lxr02.elf \
	  --sym crumpBit_runReverb --max 131
	python3 elf_looppath.py --elf $(ROOT)/build/lxr02.elf \
	  --sym crumpBit_process --top 2 --max 31
	python3 elf_looppath.py --elf $(ROOT)/build/lxr02.elf \
	  --sym crumpBit_process --top 2 --rank 2 --max 16
	python3 elf_blockpath.py --elf $(ROOT)/build/lxr02.elf \
	  --sym crumpBit_delayLimit --max 152
	python3 elf_looppath.py --elf $(ROOT)/build/lxr02.elf \
	  --sym crumpBit_tableLimit --straight --max 63

# S091 P5: the whole CrumpBit Effect at or below 170 instructions per frame
# in every sub-type, worst case (the user's cap): engine loop + filter loop
# + (engine per block + crumpBit_process() per block on that filter's
# branch + the manager's clamps, `dly`'s with its curve-limit search) / 32
# (crumpbit_cpu.py; needs `make all` at the repository root first).
# S094 P2 AS-13: `grn`/`rvb` run the resonant SVF again (the S091 P5 one-pole
# is gone): `grn` measured 180.8 and `rvb` 179.4 per frame (were 165.6 /
# 163.9); `dly`/`cmb`/`pit` 1 lower. The cap moves from S091 P5's 170 to 181,
# inside the Effect's cycle share (16,634 per block,
# S094_P2_ALKALINE_SAVINGS.md §21, §23).
# S095 P1 (S095_P1_IMPLEMENTATION.md §S18): steady cap 184 (the prefix,
# seat fast path and LTO re-layout: `grn` 181.5 -> 183.7 with the fixed
# jump-table tool), and --feed-cap 204: each sub-type plus the type-change
# feed (sk_feedMix) or the growth wait after it, whichever is dearer. The
# feed fits the Effect's own share; nothing is taken from Advanced.
# S095 P1 (§S18): the Space types' host laws (modules/space.c) on the real
# DSP (SpaceEffect.c with the kit and CrumpBit's `dly` curve), after the
# crumpbit target's extractions.
space: crumpbit
	$(EX) --src $(ROOT)/Core/DSP/Effects/EffectsManager.h \
	  --macro EFFECT_TYPE_DELAY --macro EFFECT_TYPE_REVERB \
	  --macro EFFECT_TYPE_ECHO --out $(GEN)/sp_types.c
	$(EX) --src $(ROOT)/Core/DSP/Effects/Space/SpaceParameters.h \
	  --typedef space_delay_param_t --typedef space_reverb_param_t \
	  --typedef space_echo_param_t --macro SPACE_HEADS_STOPS \
	  --macro SPACE_FREEZE_SHIMMER --macro SPACE_FREEZE_OFF \
	  --macro SPACE_HEADS_DEFAULT --macro SPACE_ROUTING_E2R \
	  --macro SPACE_ROUTING_R2E --macro SPACE_ROUTING_LOOP \
	  --macro SPACE_INTERVAL_ROW_MIN --macro SPACE_INTERVAL_ROW_MAX \
	  --out $(GEN)/sp_params.c
	$(EX) --src $(ROOT)/Core/DSP/Effects/Space/SpaceEffect.h --body \
	  --out $(GEN)/SpaceEffect.host.h
	$(EX) --src $(ROOT)/Core/DSP/Effects/Space/SpaceEffect.c --body \
	  --out $(GEN)/SpaceEffect.host.c
	$(HOSTCC) $(HOSTFLAGS) modules/space.c -o build/space -lm
	./build/space

# S095 P1 (§S18): the Space types' instructions per frame (space_cpu.py).
space_cpu_check:
	python3 space_cpu.py --elf $(ROOT)/build/lxr02.elf

crumpbit_cpu_check:
	python3 crumpbit_cpu.py --elf $(ROOT)/build/lxr02.elf --cap 184 --feed-cap 204

# ---------------------------------------------------------------------------
# S094 P4: dump the shipped pwm and stock waves for the PWM investigation.
#
# What:       builds modules/pwm_dump.c on the real Oscillator.c (host build)
#             and the real stock tables (extracted from wavetable.c, const
#             removed), runs it, and writes build/pwm_dump.csv for
#             tools/plots/plot_s094p4_pwm.py. Not part of `all`: it checks
#             nothing.
# Accessors:  make -C tools/dsp_test pwm_dump.
pwm_dump: $(GEN)/Oscillator.host.h $(GEN)/Oscillator.host.c \
	$(GEN)/oscPwmTables.host.h $(GEN)/oscPwmTables.host.c \
	$(GEN)/oscPwmWarp.host.h | $(GEN)
	for t in sine_table sawTable triTable recTable; do \
	  $(EX) --src $(ROOT)/Core/DSPAudio/wavetable.c --object $$t --out $(GEN)/pd_$$t.raw.c && \
	  sed "s/const int16_t $$t/int16_t $$t/" $(GEN)/pd_$$t.raw.c > $(GEN)/pd_$$t.c || exit 1; \
	done
	$(EX) --src $(ROOT)/Core/DSP/Instruments/Alkaline/AlkalineSquareTable.c --body \
	  --out $(GEN)/AlkalineSquareTable.host.c
	$(HOSTCC) $(HOSTFLAGS) $(OSC_HOST_INC) modules/pwm_dump.c $(CAT_HOST_SRC) \
	  -o build/pwm_dump -lm
	./build/pwm_dump > build/pwm_dump.csv
