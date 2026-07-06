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
          -ICore/Scene/Preset \
          -ICore/MIDI \
          -ICore/DSPAudio \
          -ICore/Scene/Pattern \
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
  Core/Hardware/SD/kitBrowser.c \
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
  Core/Menu/Cc2Text.c \
  Core/Menu/copyClearTools.c \
  Core/Menu/screensaver.c \
  Core/Scene/Preset/presetManager.c \
  Core/Scene/Preset/ParameterArray.c \
  Core/MIDI/FIFO.c \
  Core/MIDI/MidiRealtime.c \
  Core/MIDI/Uart.c \
	  Core/MIDI/MidiVoiceControl.c \
	  Core/MIDI/MidiParser.c \
  Core/Scene/Pattern/PatternData.c \
  Core/Scene/Pattern/EuklidGenerator.c \
  Core/Scene/Pattern/SomGenerator.c \
  Core/Scene/Pattern/SomData.c \
	  Core/Sequencer/sequencer.c \
	  Core/Sequencer/sequencerTimer.c \
	  Core/Sequencer/clockSync.c \
  Core/SampleRom/SampleMemory.c \
  Core/SampleRom/sampleFlash.c \
  Core/Src/startup_stm32f765xx.s

DSP_SRCS = \
  Core/DSPAudio/1PoleLp.c \
  Core/DSPAudio/automationNode.c \
  Core/DSPAudio/BufferTools.c \
  Core/DSPAudio/CymbalVoice.c \
  Core/DSPAudio/Decay.c \
  Core/DSPAudio/distortion.c \
  Core/DSPAudio/dither.c \
  Core/DSPAudio/DrumVoice.c \
  Core/DSPAudio/HiHat.c \
  Core/DSPAudio/lfo.c \
  Core/DSPAudio/mixer.c \
  Core/DSPAudio/modulationNode.c \
  Core/DSPAudio/Oscillator.c \
  Core/DSPAudio/random.c \
  Core/DSPAudio/ResonantFilter.c \
  Core/DSPAudio/Samples.c \
  Core/DSPAudio/SlopeEg2.c \
  Core/DSPAudio/snapEg.c \
  Core/DSPAudio/Snare.c \
  Core/DSPAudio/squareRootLut.c \
  Core/DSPAudio/transientGenerator.c \
  Core/DSPAudio/transientTables.c \
  Core/DSPAudio/wavetable.c

# -Ofast for DSP: enables -ffast-math (float reordering, NaN/Inf assumptions)
CFLAGS_DSP = $(subst -O2,-Ofast,$(CFLAGS))

OBJS = $(patsubst %.c,$(BUILD)/%.o,$(patsubst %.s,$(BUILD)/%.o,$(SRCS))) \
       $(patsubst %.c,$(BUILD)/%.o,$(DSP_SRCS))

# -----------------------------------------------------------------------
all: $(BUILD)/$(TARGET).bin
	$(SZ) $(BUILD)/$(TARGET).elf

$(BUILD)/$(TARGET).bin: $(BUILD)/$(TARGET).elf
	$(CP) -O binary -S $< $@

$(BUILD)/$(TARGET).elf: $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) -o $@

# DSP sources compiled with -Ofast (more specific rule wins over the generic one below)
$(BUILD)/Core/DSPAudio/%.o: Core/DSPAudio/%.c | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS_DSP) $< -o $@

$(BUILD)/%.o: %.c | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD)/%.o: %.s | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) -c $(ASFLAGS) $< -o $@

$(BUILD):
	mkdir -p $(BUILD)

img: $(BUILD)/$(TARGET).bin
	python3 tools/build_lxrv2_img.py \
	    $(BUILD)/$(TARGET).bin \
	    $(BUILD)/LXRV2_$(TARGET).img
	@echo ">>> Copy $(BUILD)/LXRV2_$(TARGET).img to SD card root"

clean:
	rm -rf $(BUILD)

.PHONY: all img clean
