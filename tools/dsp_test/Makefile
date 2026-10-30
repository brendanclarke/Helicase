# Module host tests (tools/dsp_test).
#
# What:       one host program per firmware module, built from the real
#             source text (extract.py, by name; never by line range):
#               oscillator          modules/oscillator.c (+ the pos cap build)
#               modulation          modules/modulation.c
#               voice_control       modules/voice_control.c (+ break build)
#               sequencer           modules/sequencer.c (+ break build)
#               instrument_manager  modules/instrument_manager.c (+ 2 breaks)
#               samples             modules/samples.c (+ break build)
#               sampleoptimize      modules/sampleoptimize.py
#               special_tags        check_special_tags.py
#               stock_tables        tools/plots/gen_stock_tables.py --check
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
        stock_tables voicefold fold_tables
all: oscillator modulation voice_control sequencer instrument_manager samples \
     sampleoptimize run_modes special_tags stock_tables voicefold fold_tables

$(GEN):
	mkdir -p $(GEN)

clean:
	rm -rf build

# The whole real Oscillator.h/.c, firmware includes and ITCM sections removed.
$(GEN)/Oscillator.host.h: $(ROOT)/Core/DSPAudio/Oscillator.h | $(GEN)
	$(EX) --src $< --body --out $@
$(GEN)/Oscillator.host.c: $(ROOT)/Core/DSPAudio/Oscillator.c | $(GEN)
	$(EX) --src $< --body --out $@

oscillator: $(GEN)/Oscillator.host.h $(GEN)/Oscillator.host.c
	$(HOSTCC) $(HOSTFLAGS) $(OSC_HOST_INC) modules/oscillator.c $(CAT_HOST_SRC) \
	  -o build/oscillator -lm
	$(HOSTCC) $(HOSTFLAGS) $(OSC_HOST_INC) -DOSC_POS_BEND_CAP=8 -DOSC_POS_TEST_CAP \
	  modules/oscillator.c $(CAT_HOST_SRC) -o build/oscillator_cap -lm
	./build/oscillator
	./build/oscillator_cap

modulation: $(GEN)/Oscillator.host.h
	$(EX) --src $(ROOT)/Core/DSPAudio/modulationNode.c \
	  --func modNode_interpStampOf --func modNode_setWaveInterpEnabled \
	  --func modNode_setItpReserved --func modNode_resetTargets \
	  --func modNode_interpClaim \
	  --out $(GEN)/modulation_interp.c
	$(HOSTCC) $(HOSTFLAGS) $(OSC_HOST_INC) modules/modulation.c -o build/modulation -lm
	./build/modulation

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

fold_tables:
	cd $(ROOT) && python3 tools/plots/gen_fold_tables.py --check
