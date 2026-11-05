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
#               samples             modules/samples.c (+ break build)
#               crumpbit            modules/crumpbit.c (+ 18 breaks: P2
#                                   Sync key, R4 AC seed, P2 flat tone, P2
#                                   seat delay clamp, P2 Rate-limit probe;
#                                   P3 wet around the mix stage, grain
#                                   freeze, pitch direction, lane map; P4
#                                   grain fade asymmetry, `bal` side swap,
#                                   grain overwrite clamp; P5 data-line
#                                   table, `grn` echo curve, filter
#                                   hand-over, grain block-aligned ends,
#                                   one-pole `bal` side swap, dropped-
#                                   trigger ages)
#               crumpbit_tables     tools/plots/gen_crumpbit_tables.py --check
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
        stock_tables voicefold fold_tables crumpbit crumpbit_tables \
        crumpbit_elf_check crumpbit_cpu_check
all: oscillator modulation voice_control sequencer instrument_manager samples \
     sampleoptimize run_modes special_tags stock_tables voicefold fold_tables \
     crumpbit crumpbit_tables

$(GEN):
	mkdir -p $(GEN)

clean:
	rm -rf build

# The whole real Oscillator.h/.c, firmware includes and ITCM sections removed.
$(GEN)/Oscillator.host.h: $(ROOT)/Core/DSPAudio/Oscillator.h | $(GEN)
	$(EX) --src $< --body --out $@
$(GEN)/Oscillator.host.c: $(ROOT)/Core/DSPAudio/Oscillator.c | $(GEN)
	$(EX) --src $< --body --out $@

# S089 P3 breaks (fmramp group): the ramp dispatcher sending wtb to the
# one-frame twin must fail FR1; the depth stepped before its first use
# (d = d0 + dStep) must fail FR2. Each break edits the generated
# Oscillator.host.c and is found first through its own -I directory.
oscillator: $(GEN)/Oscillator.host.h $(GEN)/Oscillator.host.c
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

# S091 P1/P2/P3: the real CrumpBitEffect.c and its generated tables against
# the references in modules/crumpbit.c (laws CB1-CB19; CB1-CB4, CB10, CB16,
# CB17 and the CB15/CB19 identity laws bit-exact).
# Break proofs:
#   - the Sync search key ignoring the live share limit (key = target
#     instead of min(target, max)) must fail CB1 (S091 P2; the S091 P1
#     probe off-by-one break no longer applies, because the eighth probe
#     repairs a one-position overshoot);
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
#     start (the P4 defect) must fail CB14's block-aligned law.
CB_GEN := $(GEN)/cb_fxbuf.c $(GEN)/cb_efftypes.c $(GEN)/cb_effects.c \
          $(GEN)/cb_params.c \
          $(GEN)/cb_stepscale.c $(GEN)/cb_stepscale.h \
          $(GEN)/cb_tables.h $(GEN)/cb_tables.c \
          $(GEN)/CrumpBitEffect.host.h $(GEN)/CrumpBitEffect.host.c
crumpbit: | $(GEN)
	$(EX) --src $(ROOT)/Core/DSP/Effects/FxBuffer.h \
	  --typedef fx_share_t --typedef fxbuf_handoff_t \
	  --macro FXBUF_ALIGN_BYTES \
	  --macro FXBUF_VOICE_UNIT_COUNT --macro FXBUF_VOICE_UNIT_SAMPLES \
	  --macro FXBUF_VOICE_UNIT_BYTES --macro FXBUF_EFFECT_POINTER_COUNT \
	  --macro FXBUF_HANDOFF_POINTER_COUNT \
	  --macro FXBUF_HANDOFF_EFFECT_POINTER_BASE \
	  --macro FXBUF_STATE_EFFECT_WRITTEN --out $(GEN)/cb_fxbuf.c
	$(EX) --src $(ROOT)/Core/DSP/Effects/EffectTypes.h \
	  --macro EFFECT_SEQ_LANE_COUNT --out $(GEN)/cb_efftypes.c
	$(EX) --src $(ROOT)/Core/DSP/Effects/EffectsManager.h \
	  --macro EFFECT_LANE_MORPH_SOURCE --macro EFFECT_LANE_NONE \
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
	$(EX) --src $(ROOT)/Core/Sequencer/StepScale.c \
	  --object stepScale_q8Table --func stepScale_ticksQ8 \
	  --out $(GEN)/cb_stepscale.c
	$(EX) --src $(ROOT)/Core/DSP/Effects/CrumpBit/CrumpBitEffect.h --body \
	  --out $(GEN)/CrumpBitEffect.host.h
	$(EX) --src $(ROOT)/Core/DSP/Effects/CrumpBit/CrumpBitEffect.c --body \
	  --out $(GEN)/CrumpBitEffect.host.c
	$(HOSTCC) $(HOSTFLAGS) modules/crumpbit.c -o build/crumpbit -lm
	./build/crumpbit
	mkdir -p build/break_cb1 build/break_cb2
	sed 's/const float key = (target < max_samples) ? target : max_samples;/const float key = target;/' \
	  $(GEN)/CrumpBitEffect.host.c > build/break_cb1/CrumpBitEffect.host.c
	@grep -q 'const float key = target;' build/break_cb1/CrumpBitEffect.host.c
	$(HOSTCC) -Ibuild/break_cb1 $(HOSTFLAGS) modules/crumpbit.c \
	  -o build/crumpbit_break1 -lm
	@if ./build/crumpbit_break1 >/dev/null 2>&1; then \
	  echo "crumpbit_break1: FAILED (Sync live-limit break not caught)"; \
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
	sed 's/((1.0f - crumpBit_ratioOf(semis)) \/ window/((crumpBit_ratioOf(semis) - 1.0f) \/ window/' \
	  $(GEN)/CrumpBitEffect.host.c > build/break_cb8/CrumpBitEffect.host.c
	@grep -q '(crumpBit_ratioOf(semis) - 1.0f)' \
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
	sed 's/        rt->svf_l1 = rt->svf_l2;/        (void)0;/' \
	  $(GEN)/CrumpBitEffect.host.c > build/break_cb15/CrumpBitEffect.host.c
	@grep -q '        (void)0;' \
	  build/break_cb15/CrumpBitEffect.host.c
	$(HOSTCC) -Ibuild/break_cb15 $(HOSTFLAGS) modules/crumpbit.c \
	  -o build/crumpbit_break15 -lm
	@if ./build/crumpbit_break15 >/dev/null 2>&1; then \
	  echo "crumpbit_break15: FAILED (filter hand-over break not caught)"; \
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
	sed 's/const float kl = crumpBit_onePoleK\[fl\];/const float kl = crumpBit_onePoleK[fr];/' \
	  $(GEN)/CrumpBitEffect.host.c > build/break_cb17/CrumpBitEffect.host.c
	@grep -q 'const float kl = crumpBit_onePoleK\[fr\];' \
	  build/break_cb17/CrumpBitEffect.host.c
	$(HOSTCC) -Ibuild/break_cb17 $(HOSTFLAGS) modules/crumpbit.c \
	  -o build/crumpbit_break17 -lm
	@if ./build/crumpbit_break17 >/dev/null 2>&1; then \
	  echo "crumpbit_break17: FAILED (one-pole bal side break not caught)"; \
	  exit 1; \
	else echo "crumpbit_break17: caught (law CB16 fails as expected)"; fi
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

# S091 P2: the committed CrumpBit tables must equal the generator's output.
crumpbit_tables:
	cd $(ROOT) && python3 tools/plots/gen_crumpbit_tables.py --check

# S091 ARM gate (needs `make all` at the repository root first):
#   - each sub-engine's frame loop (stages 1-3, one symbol each,
#     CRUMPBIT_NOINLINE) at or below its measured longest path, no VDIV
#     (S091 P5 limits, measured on the implemented tree): `dly` 111 and
#     `cmb` 109 (engine A with the Q14 read; P4 124), `pit` 106 (drop-sample
#     heads; P4 135), `grn` 116 (two spans around the block's one trigger or
#     steal; no window lower clamp, plain echo; P4 136; --cond-heads
#     --in-range: its cold blocks close backward branches into the body,
#     which are not frame loops), `rvb` 131 (low-pass damping; P4 145);
#   - crumpBit_process(): the stage-4 output-filter loops, the SVF 31 (S091
#     P2; loop 1) and the one-pole 16 (S091 P5 F1; --rank 2);
#   - the unrolled Sync search crumpBit_divisionFor() at or below 117
#     instructions per block (the 128-step walk it replaced was up to
#     ~2,430), and the table-limit search crumpBit_tableLimit() at or below
#     63 (S091 P3: the S091 P2 crumpBit_rateLimit() search generalised to
#     every sub-type's table and kept out of line; eight probes, run once
#     per block by effects_service() through effective_max).
crumpbit_elf_check:
	python3 elf_looppath.py --elf $(ROOT)/build/lxr02.elf \
	  --sym crumpBit_runDelay --max 111
	python3 elf_looppath.py --elf $(ROOT)/build/lxr02.elf \
	  --sym crumpBit_runComb --max 109
	python3 elf_looppath.py --elf $(ROOT)/build/lxr02.elf \
	  --sym crumpBit_runPitch --max 106
	python3 elf_looppath.py --elf $(ROOT)/build/lxr02.elf \
	  --sym crumpBit_runGrain --cond-heads --in-range --top 2 --max 116
	python3 elf_looppath.py --elf $(ROOT)/build/lxr02.elf \
	  --sym crumpBit_runReverb --max 131
	python3 elf_looppath.py --elf $(ROOT)/build/lxr02.elf \
	  --sym crumpBit_process --top 2 --max 31
	python3 elf_looppath.py --elf $(ROOT)/build/lxr02.elf \
	  --sym crumpBit_process --top 2 --rank 2 --max 16
	python3 elf_looppath.py --elf $(ROOT)/build/lxr02.elf \
	  --sym crumpBit_divisionFor --straight --max 117
	python3 elf_looppath.py --elf $(ROOT)/build/lxr02.elf \
	  --sym crumpBit_tableLimit --straight --max 63

# S091 P5: the whole CrumpBit Effect at or below 170 instructions per frame
# in every sub-type, worst case (the user's cap): engine loop + filter loop
# + (engine per block + crumpBit_process() per block on that filter's
# branch + the manager's clamps) / 32, `dly` adding its Sync search
# (crumpbit_cpu.py; needs `make all` at the repository root first).
crumpbit_cpu_check:
	python3 crumpbit_cpu.py --elf $(ROOT)/build/lxr02.elf --cap 170
