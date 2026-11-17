# Makefile — LXR-02 firmware
# STM32F765, bare-metal, arm-none-eabi-gcc

TARGET  = lxr02
BUILD   = build
PREFIX  = arm-none-eabi-
CC      = $(PREFIX)gcc
CP      = $(PREFIX)objcopy
SZ      = $(PREFIX)size

MCU     = -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard

CFLAGS  = $(MCU) -O2 -flto -Wall -Wextra -std=gnu11 \
          -fdata-sections -ffunction-sections \
          -MMD -MP \
          -I. \
          -ICore \
          -ICore/Hardware \
          -ICore/Hardware/frontPanel \
          -ICore/Hardware/frontPanel/IO \
          -ICore/Hardware/SD \
          -ICore/Hardware/SD/SPI \
          -ICore/Hardware/SD/asyncfatfs \
          -ICore/Hardware/USB/OTG_Driver/inc \
          -ICore/Hardware/USB/Device_Library/inc \
          -ICore/Hardware/USB/App \
          -ICore/Hardware/USB/OTG_Driver/src \
          -ICore/Menu \
          -ICore/Menu/CopyClear \
          -ICore/Bank \
          -ICore/Bank/Scene/Preset \
          -ICore/MIDI \
          -ICore/DSPAudio \
          -ICore/DSP/Instruments \
          -ICore/DSP/Instruments/Drum \
          -ICore/DSP/Instruments/Snare \
          -ICore/DSP/Instruments/Cymbal \
          -ICore/DSP/Instruments/HiHat \
          -ICore/DSP/Instruments/Alkaline \
          -ICore/DSP/Instruments/Colloid \
          -ICore/DSP/Instruments/Tetrakt \
          -ICore/DSP/Effects \
          -ICore/DSP/Effects/CrumpBit \
          -ICore/DSP/Effects/Space \
          -ICore/Bank/Scene \
          -ICore/Bank/Scene/Pattern \
          -ICore/Sequencer \
          -ICore/SampleRom \
          -ICore/compat

ASFLAGS = $(MCU) -Wall -fdata-sections -ffunction-sections

LDFLAGS = $(MCU) \
          -specs=nano.specs -specs=nosys.specs \
          -TSTM32F765VIHx_FLASH.ld \
          -Wl,-Map=$(BUILD)/$(TARGET).map,--cref \
          -Wl,--gc-sections \
          -O2 -flto -lc -lm

SRCS = \
  main.c \
  Core/Hardware/AudioCodecManager.c \
  Core/Hardware/memtest.c \
  Core/Hardware/flashImage.c \
  Core/Hardware/triggerJacks.c \
  Core/Hardware/clocks.c \
  Core/Hardware/timebase.c \
  Core/Hardware/frontPanel/buttonHandler.c \
  Core/Hardware/frontPanel/lcd.c \
  Core/Hardware/frontPanel/ledHandler.c \
  Core/Hardware/frontPanel/IO/adcPots.c \
  Core/Hardware/frontPanel/IO/din.c \
  Core/Hardware/frontPanel/IO/dout.c \
  Core/Hardware/frontPanel/IO/encoder.c \
  Core/Hardware/frontPanel/IO/endlessPots.c \
  Core/Hardware/SD/SPI/spi_sd.c \
  Core/Hardware/SD/SPI/sd_routines.c \
  Core/Hardware/SD/asyncfatfs/asyncfatfs.c \
  Core/Hardware/SD/asyncfatfs/fat_standard.c \
  Core/Hardware/SD/asyncfatfs/sdcard_lxr02.c \
  Core/Hardware/SD/filesystem.c \
  Core/Hardware/SD/storageTypes.c \
  Core/Hardware/USB/OTG_Driver/src/usb_core.c \
  Core/Hardware/USB/OTG_Driver/src/usb_dcd.c \
  Core/Hardware/USB/OTG_Driver/src/usb_dcd_int.c \
  Core/Hardware/USB/Device_Library/src/usbd_core.c \
  Core/Hardware/USB/Device_Library/src/usbd_ioreq.c \
  Core/Hardware/USB/Device_Library/src/usbd_req.c \
  Core/Hardware/USB/App/usb_bsp.c \
  Core/Hardware/USB/App/usb_it.c \
  Core/Hardware/USB/App/usb_manager.c \
  Core/Hardware/USB/App/usb_midi_core.c \
  Core/Hardware/USB/App/usbd_desc.c \
  Core/Hardware/USB/App/usbd_usr.c \
  Core/Menu/menu.c \
  Core/Menu/menuEffects.c \
  Core/Menu/MenuLabels.c \
  Core/Sequencer/StepScale.c \
  Core/Menu/Cc2Text.c \
  Core/Menu/CopyClear/copyClearSession.c \
  Core/Menu/CopyClear/copyOps.c \
  Core/Menu/CopyClear/clearOps.c \
  Core/Menu/CopyClear/copyClearService.c \
  Core/Menu/screensaver.c \
  Core/Menu/SplashAnimation.c \
  Core/Bank/Scene/Preset/presetManager.c \
  Core/Bank/Scene/Preset/presetMorphEngine.c \
  Core/Bank/Scene/Preset/ParameterArray.c \
  Core/Bank/BankData.c \
  Core/Bank/Scene/SceneData.c \
  Core/Bank/Scene/Autosave.c \
  Core/Bank/Scene/AutosaveTrace.c \
  Core/Bank/Scene/Pattern/PatternTrace.c \
  Core/Bank/Scene/SceneModTargets.c \
  Core/DSP/Instruments/InstrumentManager.c \
  Core/DSP/Instruments/Drum/DrumParameters.c \
  Core/DSP/Instruments/Snare/SnareParameters.c \
  Core/DSP/Instruments/Cymbal/CymbalParameters.c \
  Core/DSP/Instruments/HiHat/HiHatParameters.c \
  Core/DSP/Instruments/Alkaline/AlkalineParameters.c \
  Core/DSP/Instruments/Colloid/ColloidParameters.c \
  Core/DSP/Instruments/Tetrakt/TetraktParameters.c \
  Core/DSP/Effects/FxBuffer.c \
  Core/DSP/Effects/EffectsManager.c \
  Core/DSP/Effects/CrumpBit/CrumpBitParameters.c \
  Core/DSP/Effects/CrumpBit/CrumpBitTables.c \
  Core/DSP/Effects/Space/SpaceKit.c \
  Core/DSP/Effects/Space/SpaceParameters.c \
  Core/MIDI/MidiBuffer.c \
  Core/MIDI/MidiDin.c \
  Core/MIDI/MidiTransport.c \
  Core/MIDI/MidiIn.c \
  Core/MIDI/MidiOut.c \
  Core/MIDI/MidiNoteMap.c \
  Core/MIDI/MidiNotes.c \
  Core/MIDI/MidiNoteModes.c \
  Core/MIDI/MidiProgram.c \
  Core/MIDI/MidiControl.c \
  Core/MIDI/MidiLearn.c \
  Core/MIDI/MidiVoiceControl.c \
  Core/Bank/Scene/Pattern/PatternData.c \
  Core/Bank/Scene/Pattern/PatternStackService.c \
  Core/Bank/Scene/Pattern/EuklidGenerator.c \
	  Core/Sequencer/sequencer.c \
	  Core/Sequencer/sequencerTimer.c \
	  Core/Sequencer/clockSync.c \
	  Core/Sequencer/LiveRecord.c \
  Core/SampleRom/SampleMemory.c \
  Core/SampleRom/SampleInstall.c \
  Core/SampleRom/sampleFlash.c \
  Core/Src/startup_stm32f765xx.s

# S089 P2: voiceFold.c (WFd chain DSP, .itcm.s089) and its generated table
# use the Core/DSPAudio -Ofast rule below like every other voice DSP file.
DSP_SRCS = \
  Core/DSPAudio/1PoleLp.c \
  Core/DSPAudio/BufferTools.c \
  Core/DSPAudio/BusCompressor.c \
  Core/DSP/Instruments/Alkaline/AlkalineControl.c \
  Core/DSP/Instruments/Alkaline/AlkalineSquareTable.c \
  Core/DSP/Instruments/Alkaline/AlkalineTables.c \
  Core/DSP/Instruments/Alkaline/AlkalinePostTables.c \
  Core/DSP/Instruments/Alkaline/AlkalineVoice.c \
  Core/DSP/Instruments/Colloid/ColloidOsc.c \
  Core/DSP/Instruments/Colloid/ColloidTables.c \
  Core/DSP/Instruments/Colloid/ColloidVoice.c \
  Core/DSP/Instruments/Tetrakt/TetraktTables.c \
  Core/DSP/Instruments/Tetrakt/TetraktVoice.c \
  Core/DSP/Instruments/Cymbal/CymbalVoice.c \
  Core/DSPAudio/Decay.c \
  Core/DSPAudio/DiodeLadder.c \
  Core/DSPAudio/distortion.c \
  Core/DSPAudio/dither.c \
  Core/DSP/Instruments/Drum/DrumVoice.c \
  Core/DSP/Instruments/HiHat/HiHat.c \
  Core/DSP/Effects/CrumpBit/CrumpBitEffect.c \
  Core/DSP/Effects/Space/SpaceKitDsp.c \
  Core/DSP/Effects/Space/SpaceEffect.c \
  Core/DSPAudio/lfo.c \
  Core/DSPAudio/lfoRateTable.c \
  Core/DSPAudio/distortionMakeup.c \
  Core/DSPAudio/mixer.c \
  Core/DSPAudio/modulationNode.c \
  Core/DSPAudio/Oscillator.c \
  Core/DSPAudio/oscPwmTables.c \
  Core/DSPAudio/random.c \
  Core/DSPAudio/ResonantFilter.c \
  Core/DSPAudio/SlopeEg2.c \
  Core/DSPAudio/snapEg.c \
  Core/DSP/Instruments/Snare/Snare.c \
  Core/DSPAudio/squareRootLut.c \
  Core/DSPAudio/transientGenerator.c \
  Core/DSPAudio/transientTables.c \
  Core/DSPAudio/voiceFold.c \
  Core/DSPAudio/voiceFoldTables.c \
  Core/DSPAudio/wavetable.c

# -Ofast for DSP: enables -ffast-math (float reordering, NaN/Inf assumptions)
CFLAGS_DSP = $(subst -O2,-Ofast,$(CFLAGS))

# S097 P2 step 1: cold sources compiled for size (-Os). Only sources that never
# run in an ISR, on the render path (audio_check_and_render(), the mixer,
# voiceControl_processPending(), TIM3) or per MIDI message, checked by call
# graph: the FAT engine (main loop, inside filesystem operations), sample
# install (Load:[Samples], audio suspended) and boot-only code. Compile-time
# -Os is kept per function through LTO. Gate (S097_P2_UI_STORAGE_IMPLEMENTATION.md):
# every per-sample loop exact, hot call sets and ISR bodies unchanged. Do not
# add a file here without that gate: a larger shrink moves the LTO partition
# cuts and with them the DSP loops (S097 P0 §3).
OS_SRCS = \
  Core/Hardware/SD/asyncfatfs/asyncfatfs.c \
  Core/SampleRom/SampleInstall.c \
  Core/SampleRom/sampleFlash.c \
  Core/Hardware/flashImage.c \
  Core/Menu/SplashAnimation.c

# S097 P2 step 3: filesystem.c with -Os as well (-36 KB, every per-sample loop
# exact with COLD_LAST below). Off by default again: on the device the FS_OS=1
# image showed a sequencer stumble during a Scene Load into non-playing slots
# (user, 2026-11-17). The logs neither confirm nor exclude -Os as the cause, so
# it waits for an A/B test of the same action on this FS_OS=0 image
# (S097_P2_UI_STORAGE_IMPLEMENTATION.md §10). Build it with
# `make clean && make all FS_OS=1`. FS_OS_BUILD keeps the helpers reachable
# from TIM3/MIDI at -O2 (FS_HOT_O2 in filesystem.c). Changing FS_OS needs
# `make clean` (compiler flags are not object prerequisites).
FS_OS ?= 0
ifeq ($(FS_OS),1)
OS_SRCS += Core/Hardware/SD/filesystem.c
$(BUILD)/Core/Hardware/SD/filesystem.o: CFLAGS += -DFS_OS_BUILD
endif

# S097 P2: cold UI/storage objects are linked after the DSP objects.
#
# What: COLD_LAST sources move from their SRCS position to the end of the link.
# Why: LTO's balanced partitioner fills partitions in object order. With these
# sources before the DSP objects, any change in their size moved the partition
# cuts through the voice renders and the mixer, and so changed per-sample loops
# (S097 P0 §3: ±1 instruction "jitter"). Linked last, they share partitions
# only with each other. Measured on the S097 P1 image: every oscillator and
# mixer loop exact, hot call sets and ISR bodies unchanged, -1,096 B; and
# filesystem.c at -Os then leaves every loop exact (S097_P2_UI_STORAGE_
# IMPLEMENTATION.md). Keep new cold UI/storage sources in this list.
COLD_LAST = \
  Core/Hardware/SD/filesystem.c \
  Core/Hardware/SD/storageTypes.c \
  Core/Hardware/SD/asyncfatfs/asyncfatfs.c \
  Core/Hardware/SD/asyncfatfs/fat_standard.c \
  Core/Menu/menu.c \
  Core/Menu/menuEffects.c \
  Core/Menu/CopyClear/copyClearSession.c \
  Core/Menu/CopyClear/copyOps.c \
  Core/Menu/CopyClear/clearOps.c \
  Core/Menu/CopyClear/copyClearService.c

OBJS = $(patsubst %.c,$(BUILD)/%.o,$(patsubst %.s,$(BUILD)/%.o,$(filter-out $(COLD_LAST),$(SRCS)))) \
       $(patsubst %.c,$(BUILD)/%.o,$(DSP_SRCS)) \
       $(patsubst %.c,$(BUILD)/%.o,$(COLD_LAST))

-include $(OBJS:.o=.d)

# -----------------------------------------------------------------------
all: $(BUILD)/$(TARGET).bin
	$(SZ) $(BUILD)/$(TARGET).elf
	# Link budget (Session 072): flash headroom vs the 736 KiB application
	# region (S073) and the DTCM FX arena size. Report only; the linker ASSERTs
	# in STM32F765VIHx_FLASH.ld are the enforcing guards.
	python3 tools/diagnostic_tools/link_budget.py $(PREFIX)nm $(BUILD)/$(TARGET).elf

# The .bin is the raw objcopy output. `make img` stamps the image check and
# packages it (tools/build_lxrv2_img.py).
$(BUILD)/$(TARGET).bin: $(BUILD)/$(TARGET).elf
	$(CP) -O binary -S $< $@

# The linker script is a prerequisite so layout edits relink without a clean.
$(BUILD)/$(TARGET).elf: $(OBJS) STM32F765VIHx_FLASH.ld
	$(CC) $(OBJS) $(LDFLAGS) -o $@

# DSP sources compiled with -Ofast (more specific rule wins over the generic one below)
$(BUILD)/Core/DSPAudio/%.o: Core/DSPAudio/%.c | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS_DSP) $< -o $@

# Moved instrument render sources retain the DSP fast-math policy. Descriptor
# and registry control files remain in SRCS and use ordinary -O2 semantics.
$(BUILD)/Core/DSP/Instruments/Drum/DrumVoice.o: Core/DSP/Instruments/Drum/DrumVoice.c | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS_DSP) $< -o $@
$(BUILD)/Core/DSP/Instruments/Snare/Snare.o: Core/DSP/Instruments/Snare/Snare.c | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS_DSP) $< -o $@
$(BUILD)/Core/DSP/Instruments/Cymbal/CymbalVoice.o: Core/DSP/Instruments/Cymbal/CymbalVoice.c | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS_DSP) $< -o $@
$(BUILD)/Core/DSP/Instruments/HiHat/HiHat.o: Core/DSP/Instruments/HiHat/HiHat.c | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS_DSP) $< -o $@

# S093 P4: the Alkaline engine and its control path are DSP: -Ofast like the
# other engines (the P3 counts and the constant-cost loops assume it). The two
# table files are data; -Ofast changes nothing for them and keeps them with
# their users.
$(BUILD)/Core/DSP/Instruments/Alkaline/AlkalineVoice.o: Core/DSP/Instruments/Alkaline/AlkalineVoice.c | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS_DSP) $< -o $@
$(BUILD)/Core/DSP/Instruments/Alkaline/AlkalineControl.o: Core/DSP/Instruments/Alkaline/AlkalineControl.c | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS_DSP) $< -o $@
$(BUILD)/Core/DSP/Instruments/Alkaline/AlkalineTables.o: Core/DSP/Instruments/Alkaline/AlkalineTables.c | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS_DSP) $< -o $@
$(BUILD)/Core/DSP/Instruments/Alkaline/AlkalineSquareTable.o: Core/DSP/Instruments/Alkaline/AlkalineSquareTable.c | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS_DSP) $< -o $@
# S094 P1 S4: the post-filter and A3 tables (data, with their user).
$(BUILD)/Core/DSP/Instruments/Alkaline/AlkalinePostTables.o: Core/DSP/Instruments/Alkaline/AlkalinePostTables.c | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS_DSP) $< -o $@
# S095 P2: the Colloid engine, its oscillator loops and its tables are DSP:
# -Ofast like the Alkaline (the measured counts and the constant-cost loops
# assume it). ColloidParameters.c stays in SRCS (-O2), as the other tables.
#
# -fno-lto on these three objects (S095 P2, measured): with them in the LTO
# unit, the larger program shifted LTO's partitioning and ten Drum-family PWM
# loops (osc_pwmWt/Ws Fm, FmRamp, Ring, Sync; osc_smpFm, smpFmRamp) were
# re-laid as predicated forms, 1 to 3 instructions longer per sample, though
# their source is unchanged. Outside the LTO unit the Colloid leaves every
# other engine's code generation as it was: every oscillator loop equal to
# the step 0 image, the mixer 0 grew, the Alkaline ladder loops equal. The
# Colloid loses nothing it needs from LTO: its entry points are noinline
# calls from InstrumentManager, and its helpers are its own. Cost: +544 B of
# flash and +24 B of ITCM against the LTO build.
$(BUILD)/Core/DSP/Instruments/Colloid/ColloidVoice.o: Core/DSP/Instruments/Colloid/ColloidVoice.c | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS_DSP) $< -o $@ -fno-lto
$(BUILD)/Core/DSP/Instruments/Colloid/ColloidOsc.o: Core/DSP/Instruments/Colloid/ColloidOsc.c | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS_DSP) $< -o $@ -fno-lto
$(BUILD)/Core/DSP/Instruments/Colloid/ColloidTables.o: Core/DSP/Instruments/Colloid/ColloidTables.c | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS_DSP) $< -o $@ -fno-lto

# S096 P1: the Tetrakt engine and its tables are DSP: -Ofast like the
# Colloid. -fno-lto for the S095 P2 reason (Makefile comment above): outside
# the LTO unit the Tetrakt cannot shift LTO's partitioning of the other
# engines' loops (gated by elf_p4_check, S11); its entry points are noinline
# calls from InstrumentManager, and it calls the platform's
# osc_userBind()/osc_renderTyped() as ordinary externals.
# TetraktParameters.c stays in SRCS (-O2), as the other tables.
$(BUILD)/Core/DSP/Instruments/Tetrakt/TetraktVoice.o: Core/DSP/Instruments/Tetrakt/TetraktVoice.c | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS_DSP) $< -o $@ -fno-lto
$(BUILD)/Core/DSP/Instruments/Tetrakt/TetraktTables.o: Core/DSP/Instruments/Tetrakt/TetraktTables.c | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS_DSP) $< -o $@ -fno-lto

# CrumpBit DSP (S074) runs with the fast-math policy; its
# descriptor, layout and page-hook source stays an ordinary -O2 source in SRCS.
$(BUILD)/Core/DSP/Effects/CrumpBit/CrumpBitEffect.o: Core/DSP/Effects/CrumpBit/CrumpBitEffect.c | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS_DSP) $< -o $@

# S095 P1: the Space kit's per-frame half (the feed read, the drain envelope)
# runs with the fast-math policy like CrumpBit. Its init-time half
# (SpaceKit.c) is an ordinary -O2 source in SRCS: a per-file -Os was tried
# on it and left the image byte-identical. That does not hold in general:
# compile-time -Os is kept per function through LTO (S097 P0 §4 A measured
# -135 KB with -Os on every SRCS file).
$(BUILD)/Core/DSP/Effects/Space/SpaceKitDsp.o: Core/DSP/Effects/Space/SpaceKitDsp.c | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS_DSP) $< -o $@

# S095 P1: the Space types' DSP and ops, the fast-math policy like CrumpBit;
# their descriptors and page text (SpaceParameters.c) stay in SRCS.
$(BUILD)/Core/DSP/Effects/Space/SpaceEffect.o: Core/DSP/Effects/Space/SpaceEffect.c | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS_DSP) $< -o $@

$(patsubst %.c,$(BUILD)/%.o,$(OS_SRCS)): CFLAGS += -Os

$(BUILD)/%.o: %.c | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD)/%.o: %.s | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) -c $(ASFLAGS) $< -o $@

$(BUILD):
	mkdir -p $(BUILD)

# One script builds the card image: it stamps the per-sector CRCs that
# flashImage.c checks at every boot (S073) and writes the LXRV2 header. If the
# layout is not what the linker script promises it fails and leaves no image,
# so no unstamped or stale image can be copied to the card (S074).
img: $(BUILD)/$(TARGET).bin
	python3 tools/build_lxrv2_img.py $(PREFIX)nm \
	    $(BUILD)/$(TARGET).elf \
	    $(BUILD)/$(TARGET).bin \
	    $(BUILD)/LXRV2_$(TARGET).img
	@echo ">>> Copy $(BUILD)/LXRV2_$(TARGET).img to SD card root"

clean:
	rm -rf $(BUILD)

.PHONY: all img clean
