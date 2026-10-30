# Spike retry kit: transient, filter drive and D+S (S084 spike, S086 eval).
#
# What:       transient   the spike transient generator (test_transient.c);
#             shaper      the spike drive shapers against the reference export
#                         of tools/plots/plot_waveshapers.py (test_shaper.c);
#             filter      today's ZDF filter (baseline/) against the
#                         repository's: identical, then the S1 bounds
#                         (test_filter.c);
#             armcheck    per-sample instructions of each transient kind and
#                         each shaped ZDF drive type (armcheck/probes.c);
#             armcheck-filter  VDIV count, baseline against the repository;
#             armcheck-ds per-loop cost of the D+S Mix insert.
# Why:        the post-mortem (docs/S086_EFFECTS_CHAIN_POST_MORTEM_EVAL.md)
#             requires models, failing tests and target cost numbers before a
#             retry; these are the ones the spike had.
# Inputs:     src/ (the spike text, by Core path), baseline/ (S087 text of the
#             files a retry changes), ../extract.py, ../fpseq.py, ../prelude.h.
#             SRC=../../.. runs transient/shaper/armcheck/armcheck-ds against
#             the repository once a retry lands there.
# Outputs:    build/ (generated fragments, programs, reports).
# Accessors:  make -C tools/dsp_test/spike <target>.
# Affiliates: README.md, docs/.
REPO   := ../../..
SRC    ?= src
BASE   := baseline
GEN    := build/gen
HOSTCC ?= cc
HOSTFLAGS := -O2 -std=gnu11 -Wall -Wno-unused-function -fno-vectorize -fno-slp-vectorize \
             -include ../prelude.h -I$(GEN)
ARMCC  := arm-none-eabi-gcc
ARMFLAGS := -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -Ofast -std=gnu11 \
            -fdata-sections -ffunction-sections
SRC_INC  := -I$(SRC) -I$(SRC)/Core -I$(SRC)/Core/DSPAudio -I$(SRC)/Core/SampleRom \
            -I$(SRC)/Core/DSP/InsertEffects -I$(SRC)/Core/DSP/Effects -I$(SRC)/Core/compat
REPO_INC := -I$(REPO) -I$(REPO)/Core -I$(REPO)/Core/DSPAudio -I$(REPO)/Core/compat
EX := python3 ../extract.py
FP := python3 ../fpseq.py

FILTER_SYMS := svf_padeNum svf_padeDen fastTanh fastTan tanhXdX softClipTwo \
               SVF_setReso SVF_directSetFilterValue SVF_init SVF_reset SVF_recalcFreq \
               SVF_setDrive SVF_calcBlockZDF SVF_calcBlockZDFFloat
fn = $(foreach s,$(1),--func $(s))
rn = $(foreach s,$(2),--rename $(s)=$(1)$(s))

.PHONY: all transient shaper filter armcheck armcheck-filter armcheck-ds clean
all: transient shaper filter

$(GEN):
	mkdir -p $(GEN)

clean:
	rm -rf build

transient: | $(GEN)
	$(EX) --src $(SRC)/Core/DSPAudio/transientGenerator.c \
	  --func transient_resolve --func transient_init --func transient_trigger \
	  --func transient_setType --func transient_setWave --func transient_waveMax \
	  --func transient_calcBlock --out $(GEN)/transient_new.c
	$(HOSTCC) $(HOSTFLAGS) -I$(SRC) -I$(SRC)/Core/DSPAudio -I$(SRC)/Core/compat \
	  test_transient.c -o build/transient -lm
	./build/transient

shaper: | $(GEN)
	python3 $(REPO)/tools/plots/plot_waveshapers.py --export-ref build/shaper
	$(HOSTCC) $(HOSTFLAGS) -I$(SRC)/Core/DSPAudio \
	  test_shaper.c $(SRC)/Core/DSPAudio/DriveShaper.c -o build/shaper_test -lm
	./build/shaper_test

# Gate 0: the baseline against itself. Gate 1: the -ffp-contract=off rounding
# baseline, then the repository's filter over the full S1 grid.
filter: | $(GEN)
	$(EX) --src $(BASE)/Core/DSPAudio/ResonantFilter.c $(call fn,$(FILTER_SYMS)) \
	  $(call rn,A_,$(FILTER_SYMS)) --out $(GEN)/filter_a.c
	$(EX) --src $(BASE)/Core/DSPAudio/ResonantFilter.c $(call fn,$(FILTER_SYMS)) \
	  $(call rn,B_,$(FILTER_SYMS)) --out $(GEN)/filter_b.c
	$(EX) --src $(REPO)/Core/DSPAudio/ResonantFilter.c $(call fn,$(FILTER_SYMS)) \
	  $(call rn,B_,$(FILTER_SYMS)) --out $(GEN)/filter_b_new.c
	$(HOSTCC) $(HOSTFLAGS) -c $(GEN)/filter_a.c -o build/filter_a.o
	$(HOSTCC) $(HOSTFLAGS) -c $(GEN)/filter_b.c -o build/filter_b.o
	$(HOSTCC) $(HOSTFLAGS) -ffp-contract=off -c $(GEN)/filter_b.c -o build/filter_b_strict.o
	$(HOSTCC) $(HOSTFLAGS) -c $(GEN)/filter_b_new.c -o build/filter_b_new.o
	$(HOSTCC) $(HOSTFLAGS) test_filter.c build/filter_a.o build/filter_b.o -o build/filter_self -lm
	$(HOSTCC) $(HOSTFLAGS) test_filter.c build/filter_a.o build/filter_b_strict.o \
	  -o build/filter_baseline -lm
	$(HOSTCC) $(HOSTFLAGS) test_filter.c build/filter_a.o build/filter_b_new.o -o build/filter -lm
	./build/filter_self --expect-identical
	./build/filter_baseline --baseline-out build/baseline_div.txt
	./build/filter --s1-report build/baseline_div.txt

armcheck-filter: | $(GEN)
	$(ARMCC) $(ARMFLAGS) $(REPO_INC) -c $(BASE)/Core/DSPAudio/ResonantFilter.c -o build/rf_base.o
	$(ARMCC) $(ARMFLAGS) $(REPO_INC) -c $(REPO)/Core/DSPAudio/ResonantFilter.c -o build/rf_repo.o
	@echo "vdiv baseline: $$(arm-none-eabi-objdump -d build/rf_base.o | grep -c vdiv)"
	@echo "vdiv repo:     $$(arm-none-eabi-objdump -d build/rf_repo.o | grep -c vdiv)"

# One specialised loop per transient kind and per shaped ZDF drive type: the
# shared functions hold overlapping loops, so each probe folds one case.
armcheck: | $(GEN)
	$(EX) --src $(SRC)/Core/DSPAudio/ResonantFilter.c \
	  --func svf_padeNum --func svf_padeDen --func fastTanh --func tanhXdX \
	  --func softClipTwo --func SVF_calcBlockZDF --func SVF_calcBlockZDFShaped \
	  --rename static= --out $(GEN)/vx2_flt_arm.c
	sed -e 's/^INITCM_EFFECT_NOINLINE void SVF_calcBlockZDFShaped(/static inline __attribute__((always_inline)) void spec_zdfs(/' \
	    -e 's/^INITCM_EFFECT_NOINLINE void SVF_calcBlockZDF(/static inline __attribute__((always_inline)) void spec_zdf(/' \
	    -e '/^INITCM_EFFECT float \(tanhXdX\|softClipTwo\)/s/^INITCM_EFFECT /static inline /' \
	    $(GEN)/vx2_flt_arm.c > $(GEN)/vx2_flt_spec.c
	$(EX) --src $(SRC)/Core/DSPAudio/transientGenerator.c \
	  --func transient_calcBlock --rename transient_calcBlock=spec_tr \
	  --out $(GEN)/vx2_tr_spec_raw.c
	sed -e 's/^void spec_tr(/static inline __attribute__((always_inline)) void spec_tr(/' \
	    $(GEN)/vx2_tr_spec_raw.c > $(GEN)/vx2_tr_spec.c
	$(ARMCC) $(ARMFLAGS) $(SRC_INC) -I$(GEN) -c armcheck/probes.c -o build/probes.o
	@echo "--- transient kinds (cap 22)"
	@for k in pcm sample sine table noise crash; do printf "%-7s " $$k; \
	  $(FP) --obj build/probes.o --ref tr_$$k:0 --new tr_$$k:0 \
	    --report --all | tail -1; done
	@echo "--- ZDF LP: sft reference, then each drive type"
	@printf "%-7s " sft; $(FP) --obj build/probes.o --ref zdf_sft_lp:0 \
	  --new zdf_sft_lp:0 --report --all | tail -1
	@for t in rct art thf fdc bsw tsw lsw lsq sfd afd asf chb rsb; do \
	  printf "%-7s " $$t; $(FP) --obj build/probes.o --ref zdf_$$t:0 \
	    --new zdf_$$t:0 --report --all | tail -1; done

armcheck-ds: | $(GEN)
	sh armcheck/drive_srr.sh "$(ARMCC) $(ARMFLAGS) $(SRC_INC)" "$(abspath $(SRC))"
	python3 looplen.py build/ds_probe.o
