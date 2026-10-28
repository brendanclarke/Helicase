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
        instrument_manager samples sampleoptimize special_tags stock_tables
all: oscillator modulation voice_control sequencer instrument_manager samples \
     sampleoptimize special_tags stock_tables

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
	  --func modNode_resetTargets --func modNode_interpClaim \
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

special_tags:
	cd $(ROOT) && python3 tools/dsp_test/check_special_tags.py

stock_tables:
	cd $(ROOT) && python3 tools/plots/gen_stock_tables.py --check
