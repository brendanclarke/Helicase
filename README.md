# LXR-02 Helicase 0.00
## A bare-metal rewrite of firmware for the Sonic Potions/Erica Synths LXR-02
### Introduction
The LXR02 is a digital drum synthesizer produced in collaboration with Sonic Potions and Erica Synths. It is based on a 32-bit Cortex-M7 processor. The 'Helicase' firmware is a complete rewrite of the firmware from the bare cortex register definitions up, using the native bootloader so that it can be loaded without a debugger and can be freely swapped with the stock Erica Synths firmware through the standard update process. If you want to go straight to the firmware and try it, it is './build/LXRV2_lxr02.img'. Put this in the root directory of the SD card and power on while pressing the encoder, as you would for any firmware update. You can switch between this and the Erica Synths firmware any time with this same method. If you use the Helicase firmware, it's recommended to also put the contents of the **'SD_CARD'** directory in the root directory of your micro SD card. This will be the same as my current testing files and it will probably be kinda random, but it will give you some content to start with. I'm not taking bug reports yet, there is still too much missing for that to be useful, but I'm open to general discussion. When I feel like this is good enough to actually use and I want to accept reports, I'll increment to 0.01 :)

### Stuff that's missing, might not work, or should be treated as suspicious:
- MIDI
- Live record with the 'rec' button
- Track scale, shuffle (length in steps works)
- If you've used LXR Catalyst, there's no looper yet, no 1-shot lfos, no background bank load, and no per-track switching yet, those will go in later. 

### How to use - Quickstart
The Helicase firmware uses the LXR UX as a starting point, but is almost entirely new code now. A full manual will be written when things are reasonably complete, but here are some pointers to get you started:
- The four mode buttons are used in the old 'LXR' format. From left to right these are: **VOICE**, **PERF**, **STEP**, and **LOAD/SAVE**. This means 'Load' on the LXR02 is mislabeled - it is 'Step' editing mode in Helicase. The 'Save' button is pushed once to get to the Load menu, and again to get to the 'Save' menu.

### Data
Just a quick summary for now: 
- a **'Bank'** is everything that gets loaded into memory, apart from the device settings stored in shift+load/save (this is the settings.cfg file). A bank has its own settings/metadata (not much, it's mostly just a container), and then 16 **'Scenes'**.
- A scene contains some settings and one **'Kit'**, one **'Effect'**, and one **'Pattern'**. A Kit contains six instruments. An effect contains the effect type, its parameters, and a small, up-to-16 step sequence that can automate 16 effect parameters per-step (what those parameters are are set by the effect type, but every effect parameter can be automated on a track in the normal pattern).
- The **'Pattern'** is the 7 tracks, up to 128 steps per track (8 bars). The pattern storage is hybrid-event based. The pattern pool has enough space for *around* 3000 events per scene currently, an event defined as a parameter automation or some non-default note/velocity combination. Setting a step as a trigger at its default note/velocity is always allowed and doesn't count to that limit. It's possible I might be able to increase that later, I have to see how much is left after the rest of the features go in. You can check how much pattern storage is used on the current scene in the global settings menu (there are 2 indicators, 'cpu' and 'pat').
- There is also an autosave for all that: if you like to live dangerously, you should be able to just switch off ~5 seconds or so after your last parameter or pattern edit and it will just come up the same when you reboot. Because of some background necromancy I won't dive into too much (basically, the thing gets a vanishingly small amount of CPU to work with), if you switch the autosave **off** and then **re-enable it**, you should re-save or re-load the bank, otherwise the autosave refresh could take up to 10 *minutes* or so before it catches up.  

### Modes
#### **VOICE**
Edit parameters, set steps. Mostly superficially the same as before, with some additions:
- if you hold the 'Voice' button, you can link 'Scenes' - more on this below. What this means is that every voice parameter edit to the scene you're on also applies to every linked scene. So if you load the same kit to multiple scenes and link them, they work like the same kit but with different patterns. Link is one directional, it doesn't automatically back-link, but you can copy the Scene or Scene Settings from the PERF mode and apply the link that way, too. Probably the easiest way is once you've done a pattern and kit you like, link to a second scene slot, then copy the scene there too, and then clear the pattern. 
- if you press and hold any number of steps and then adjust a parameter, that parameter is automated with that value on all steps pressed. The first character of the parameter's name gets an underline if it's automated on the current scene/pattern, and if you re-hold an automated step, it will show the automated value with a little underline too. 
- there are up to 8 bars on a track. you can adjust the bar with the < BAR > buttons, the SELECT led will blink briefly to let you know what bar you selected. 
- the LFOs have three polarity options, and there are two LFO destinations per voice. 
#### **PERF**
Change scene with the SEQ STEP buttons. There are 16 scenes, each scene has its own kit, pattern, and effect. The scene's LED is lit if it has a kit and at least one active step. You can only switch to a scene if it has a valid kit loaded - you can always do so in the load menu, where the SEQ buttons represent scenes also, and you can select and load to multiple scenes with the buttons. There's no option to chain scenes yet, but I'll add that later. You can mute the voices here as before, and the global morph, individual voice morph, and the effects morph amounts are on the knobs. The looper will eventually go on the SELECT buttons. I'm not sure what to do with the < BAR > buttons here yet :)
#### **STEP** (The button that says 'Load' on the LXR02)
Edit tracks and steps. You can change track settings like length. The track settings menu comes up on entry or when pressing a track button. If you press a step you can edit the note, velocity, etc. and view/edit/add/delete individual automation on the step. The SELECT buttons let you jump to any bar on the track, and the led shows what bar you're on in the mode. 
#### **LOAD/SAVE**
Load: Load Kit, Kit Morph, Effect, Scene, Bank, Samples. Change type with the encoder. If the name comes up blank, give it a sec, it loads the entries (up to 1000) dynamically. These are sourced from the 'Library' directories on the SD card: Kit, Scene, Bank, Effect. Kits also store their morph natively. Load morph replaces the morph of a kit with the normal endpoint parameters of the selected kit. 
Press a track button: Load an instrument from the 'Instrument' library. Load an instrument morph. **Load a different instrument**: that's right. Any voice can load any instrument. You get up to 2 of cymbal and/or hi-hat, these take more CPU. If you put something other than a hi-hat in the voice 6 slot it automagically gets a second decay parameter.
Press the load/save button again: Save mode. Pretty much the same stuff. Select 'ok' to save, the text changes to 'OW' to let you know if you're replacing something. Press the track buttons to save an instrument to the library. The 'Name' field gets auto-filled from the last thing it was. If you want to get OCD about naming things, I recommend doing it in your mac/windows/linux. All the stuff is pretty human readable in directories. Names can be up to 8 characters not including their number slot or extension. The only dangerous thing is **if you rename an instrument in a kit, you must also rename it in the kitset.kcg file**. Kits have to track what instruments go in what slot, that's where they do it. 
  
### Shift + modes
#### **SHIFT + VOICE**: Edit morph 
Pretty self-explanatory, locks the interface to just show the morph values. Change the voice morph in the PERF mode first if you want to hear the results. 
#### **SHIFT + PERF**: Effects editor. 
The first parameter is effect type and you have to click it with the encoder to change it in 1-parameter view because each effect type can mutate the entire menu overlay in this mode and changing type resets all the effect parameters to default. The effect has its own morph (hold shift while in the mode), and it's own 16-step sequencer (hold a step, adjust a parameter). You can always scroll horizontally through all the effect parameters with the encoder. The send amount to the effect for each voice is set on the voice's second 'mix' page in VOICE mode - it's a bus, you set send amount, and you can set the fader pre- (fader attenuates send) or post- (send is always the same) FX send per voice (there are also other fader modes if you want to experiment :) ). There are only two effects so far - a simple stereo multimode filter same as what's on the voices and something I'm calling 'CrumpBit' (say it fast) that converts the input to 8-bit and lets you zero/invert per-bit on the SELECT buttons and has a tape-style delay. 
#### **SHIFT + STEP**: Nothing here yet. 
Well, this is probably still the SOM pattern generator but I have no idea how well this works. I'm going to combine Euklid, SOM, rotate/mutate, and probably an arpeggiator into something called 'Generators' here but I haven't started to work on it yet. 
#### **SHIFT + LOAD/SAVE**: Settings. 
The global settings, like on the LXR. I won't go through all of them here, but you can push the encoder on each one to get the full name. There's also a master compressor with sidechain and its 4 settings are in this menu at the end. The compressor settings are stored **per scene** (and copied with the Scene settings).  

### Other stuff
#### Copy and Clear
There is a new copy/clear utility. In general, for copy, hold 'copy', select what you want, keep holding 'copy', move to the scene/track/etc you want (you can navigate though PERF mode to copy/paste) and press again where you want to paste it. You can select different paste modes from the encoder in the meantime before you press to paste. You can also copy and paste different sub-objects of Scenes (like the effect) in the PERF mode. The new thing is you can select a range of steps (or range of bars in STEP mode): hold a step, press another step. 
For clear, it's pretty much the same, except the clear operation won't actually happen until you scroll off 'cancel' onto one of the clear modes, and then press again what you want to clear. You can also clear a parameter's automation across an entire pattern: hold shift+clear, keep holding 'clear', turn the knob for an automated parameter. *poof*, automation gone. 
#### Card format
**USE A FAT32 FORMATTED CARD, MBR partition**. FAT16 also works, but MBR-FAT32 is the recommended cross-compatible format. FAT12 and exFAT are not supported; if one is detected at boot, the firmware shows `Unsupported card` / `use MBR-FAT32` and does not mount or load from it. On my mac this is just MBR partition, MS-DOS(FAT), but I'm including the terminal commands for future-proofing. If you are using an SD >32GB you may need to manually create a partition that is smaller, those options are included below, just remove/edit the <options> for your system and card. In your respective terminal, for disk <X>:
    - Linux: sudo parted -s /dev/sd<X> mklabel msdos mkpart primary fat32 1MiB <32GiB> 100% && sudo mkfs.vfat -F 32 -n "LXR" /dev/sd<X>1
    - Mac: 'diskutil partitionDisk disk3 MBR FAT32 "LXR" <R *if 32G or less, otherwise* 32G>'
    - Win: "select disk <X>", "clean", "convert mbr", "create partition primary <size=32768>", "format fs=fat32 quick label=LXR", "assign" | diskpart
- **Use the content from the 'SD_CARD' sub-directory to start with**
#### Samples
Memory mapping is updated - the program uses ~500kB now so there is a slight reduction in sample storage, about 1.4MB available for sample storage in flash, or about 14 seconds.
#### Compressor
There is a master compressor which is sorta supposed to be a pseudo-optical-RMS character kind of thing - ie, more like a full mix bus compressor than a drums submix destructo-compressor. The compressor works directly on the output stream of one of the stereo pairs. There is a sidechain, which works by *trigger* on the selected track: ie not a literal audio threshold, to keep it light on CPU and simple. It uses the *velocity* of the trigger to set the depth in combination with the compressor amount. Other than the channel and sidechain there are only two parameters to keep it simple: *amount*, which is a fudge of threshold, ratio, and makeup gain so that it's roughly equal-volume, with a teensy bit of post-compressor saturation at the very end; and *rate*, which is a fudge of attack time and release time, increasing both across its range but increasing the release much more. Let me know what you think - I'd like to keep it somewhere in the range of transparent-to-chunky and maybe add something in the effects types that includes more aggressive compressor destruction later on. 

Enjoy! If you have an idea or make some cool music, feel free to join the Discord server: https://discord.gg/sWjGWuavUX

And if you want to support the absurd nonsense I get up to in general: https://patreon.com/voskomm

### Some notes for developers or prospective developers
This repository is designed to be as LLM-friendly as I could make it so that adding features would be easy for everyone. The whole sausage-making process of the hardware trace, bring-up, driver wrangling, and application port is there in the session logs. The idea is that you can start a session with something like:
 "The goal of this session is to implement < some feature >. Read @README.md and @MEMORY.md for project context and any further files as necessary, then write a plan of implementation with possible conflicts and risk factors to the root directory as < some feature >_AUDIT.md." 
And then the LLM will grab the context it needs as required. Then you read the plan, work through it, and write back these files and the logs when you're done. There's verbose logs, a template for that, and a lightweight log index to keep the context sorta-manageable.  
The build requirements are pretty lightweight, too. See 'requirements.txt'. You just need gcc, make, and python 3. If you don't want to deal with that, you can probably just drop the whole zip file into an LLM and make it build an .img for you. LLM-stuff starts below the fold. Have fun!
Brendan
brendanpaulclarke@gmail.com
https://brendanclarke.com


Above banged out on a keybard by me. LLM agent tags and stuff below...
_______

## Repository
**Structure**
- **Working firmware:** primary branch `LXR02Open-prime`; repository root is the working tree root
- **Git history:** project became a git repository after Session 023
- **Original open-source LXR source:** available read-only at `knowledge_files/LXR-master/` — must not be modified
- **Session Logs** see `knowledge_files/log_archive/000_SESSION_INDEX.md`

## Boot Process
1. LXR-02 bootloader (LXRV2) loads from flash
2. Bootloader reads SD card for `LXRV2_lxr02.img`
3. Image format: `[8B magic "LXRV2IMG"][4B payload size LE][4B checksum LE][payload]`
4. App loaded at 0x08008000 (window 0x08008000–0x080BFFFF, 736 KiB, since Session 073), SP=0x20080000
5. Boot by holding main encoder button while powering on
6. Packager: `tools/build_lxrv2_img.py` (`make img`) is the only image script: it stamps the per-sector CRCs that the firmware checks at every boot (`Img BAD s:…` names a bad sector) and writes the LXRV2 header
7. User samples live in on-chip flash sectors 7–11 (`0x080C0000`), installed with Load:[Samples]

## Toolchain
```
arm-none-eabi-gcc -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard
make all && make img  → build/LXRV2_lxr02.img   (bare `make` can stop at build/main.o in an incremental tree)
```

## Directory Structure
```
./
├── README.md                        ← this file
├── MEMORY.md                        ← project context, known issues, critical reminders
├── main.c
├── config.h
├── Makefile
├── STM32F765VIHx_FLASH.ld
├── requirements.txt
├── tools/
│   ├── build_lxrv2_img.py          ← stamps the boot image check and packages the .bin → LXRV2_lxr02.img
│   ├── link_budget.py              ← flash/ITCM/DTCM/arena report after every build
│   ├── decode_devlogs.py           ← decodes /bootlog.bin and /asavetrc.bin (V/X layouts updated S074)
│   └── dsp_test/                   ← host DSP test bench (DSP_TEST.md, S073)
├── build/                          ← generated, not in VCS
├── knowledge_files/
│   ├── SESSION_HANDOFF_TEMPLATE.md ← template for writing new session handoff logs
│   ├── ENHANCED_FEATURES.md        ← future enhancement notes
│   ├── MEMORY_AUDIT.md             ← memory region audit notes
│   ├── DSP_AUDIT.md                ← DSP pipeline audit and hot-path notes
│   ├── OSC_INTERP_AUDIT.md         ← oscillator interpolation audit
│   ├── hardware_archive/
│   │   ├── HARDWARE_MAP.md         ← full confirmed pin table, IRQ numbers
│   │   ├── AVR_TO_F765_MIGRATION.md ← architectural notes, sequencer ISR design baseline
│   │   ├── FRONTPANEL_AUDIT.md     ← legacy front-panel bridge elimination audit
│   │   ├── SD_CARD_INVESTIGATION.md ← SD false-positive analysis (PA8, 74HC165)
│   │   └── XP_CONNECTOR_MAPS.md   ← ribbon cable pin mappings
│   └── log_archive/
│       ├── 000_SESSION_INDEX.md    ← index of all sessions with keyword lookup
│       ├── 001_SESSION_HANDOFF_LOG.md
│       ├── 002_SESSION_HANDOFF_LOG.md
│       ├... etc                     ← see 000_SESSION_INDEX.md for current list
└── Core/
    ├── globals.h
    ├── datatypes.h
    ├── Src/
    │   └── startup_stm32f765xx.s
    ├── Hardware/
    │   ├── clocks.c/h               ← sysclk_init(), FPU enable via CPACR
    │   ├── timebase.c/h             ← SysTick 4kHz mainboard tick, TIM6 1kHz counters + 500Hz foreground service, TIM7 5kHz LCD drain
    │   ├── AudioCodecManager.c/h    ← consolidated audio: DMA ISRs, I2S/GPIO/DMA init, SPSC queue
    │   ├── triggerJacks.c/h         ← CLK OUT/IN, RST IN; OUT jack detect is foreground-polled
    │   ├── memtest.c/h              ← flash sector probe (boot-time, MEMTEST_ENABLED gate)
    │   ├── flashImage.c/h           ← boot-time per-sector CRC32 check of the app image (S073)
    │   ├── frontPanel/
    │   │   ├── buttonHandler.c/h    ← ISR-safe event ring, main-loop processEvents()
    │   │   ├── lcd.c/h              ← TIM7-driven async queue, 128-entry SPSC ring
    │   │   ├── ledHandler.c/h
    │   │   └── IO/
    │   │       ├── adcPots.c/h      ← sliders RV5-10, ADC1 DMA
    │   │       ├── din.c/h          ← 74HC165×5 buttons, SPI1
    │   │       ├── dout.c/h         ← 74HC595×5 LEDs, SPI1
    │   │       ├── encoder.c/h      ← SW42, TIM1 IC, Dannegger, accel + rebound suppression
    │   │       └── endlessPots.c/h  ← RV1-4, atan2 delta tracking
    │   ├── SD/
    │   │   ├── filesystem.c/h       ← public facade: typed async load/save/name/scan operations
    │   │   ├── SPI/
    │   │   │   ├── spi_sd.c/h       ← bit-bang SPI: PC12/PD2/PC8/PD0
    │   │   │   └── sd_routines.c/h  ← SD_init() only; blocking read/write superseded
    │   │   └── asyncfatfs/
    │   │       ├── asyncfatfs.c/h   ← Betaflight asyncfatfs (modified for LXR-02)
    │   │       ├── fat_standard.c/h
    │   │       ├── sdcard.h
    │   │       └── sdcard_lxr02.c/h ← SD block-device shim over bit-bang SPI
    │   └── USB/
    │       ├── OTG_Driver/
    │       ├── Device_Library/
    │       └── App/                 ← usb_manager, usb_midi_core, etc.
    ├── Menu/
    │   ├── menu.c/h                 ← full port, all pages, load/save UI
    │   ├── menuPages.h              ← 16-page × 8-subpage table
    │   ├── MenuText.h               ← all label strings
    │   ├── Cc2Text.c                ← modTargets[] 205 entries
    │   ├── CcNr2Text.h
    │   ├── copyClearTools.c/h       ← copy/clear UI; pattern mutation through PatternData
    │   ├── menuEffects.c/h          ← SHIFT+PERF Effect page (Phase 5)
    │   └── screensaver.c/h          ← screensaver with explicit LCD off/on phases
    ├── MIDI/
    │   ├── Uart.c/h                 ← USART3, 31250 baud, interrupt-driven dual FIFO (realtime + normal)
    │   ├── MidiRealtime.c/h         ← 32-entry timestamped SPSC ring for MIDI_CLOCK/START/CONTINUE/STOP
    │   ├── FIFO.c/h
    │   ├── MidiMessages.h           ← full mainboard version (MIDI_NRPN_* prefix)
    │   ├── MidiNoteNumbers.h
    │   ├── MidiParser.c/h
    │   ├── MidiVoiceControl.c/h
    │   ├── SeqStep.h
    │   └── valueShaper.h
    ├── Scene/
    │   ├── Pattern/
    │   │   ├── PatternData.c/h      ← pattern/track/step storage and edit API
    │   │   ├── EuklidGenerator.c/h  ← pattern generator
    │   │   ├── SomData.c/h          ← SOM data tables
    │   │   └── SomGenerator.c/h     ← SOM pattern/performance generator
    │   └── Preset/
    │       ├── ParameterArray.h/c   ← supersedes Parameters.h; NUM_PARAMS=275
    │       └── presetManager.c/h    ← typed load/save for kit, morph, pattern, performance, all, globals
    ├── SampleRom/
    │   ├── SampleMemory.c/h         ← sample flash metadata/runtime cache, 120 entries, loop flags
    │   └── sampleFlash.c/h          ← guarded F765 sector 7-11 erase/program helpers
    ├── Sequencer/
    │   ├── sequencerTimer.c/h       ← TIM3 4kHz sequencer timing owner (IRQ29, priority 2) — Session 019
    │   ├── sequencer.c/h            ← original LXR sequencer source (driven by TIM3_IRQHandler)
    │   ├── clockSync.c/h
    │   ├── StepScale.c/h            ← shared track/FX step-scale table (Phase 5)
    ├── DSP/
    │   ├── Effects/                 ← Phase 5: EffectsManager (registry/resolution), FxBuffer (DTCM arena), EffectTypes.h, StereoFilter/, CrumpBit/ (S074)
    │   └── Instruments/             ← InstrumentManager + Drum/Snare/Cymbal/HiHat descriptor tables
    ├── DSPAudio/
    │   ├── random.c/h               ← F765 RNG port (PLL48CLK, bare register)
    │   ├── BusCompressor.c/h        ← Scene-owned master bus compressor on St1/St2 (S074)
    │   └── [all DSP voice files]    ← ported; mixer_calcNextSampleBlock wired to AudioCodecManager
    └── compat/
        ├── stm32f4xx.h              ← vestigial-include shim via <stdint.h>
        └── cmsis_intrinsics.h
```

### Where to look for things

| Question | File |
|----------|------|
| Which session introduced a fix? | `knowledge_files/log_archive/000_SESSION_INDEX.md` |
| Full details of a fix or decision? | `knowledge_files/log_archive/0xx_SESSION_HANDOFF_LOG.md` |
| Confirmed pin assignments / IRQs? | `knowledge_files/hardware_archive/HARDWARE_MAP.md` |
| Sequencer / DSP architecture plans? | `knowledge_files/hardware_archive/AVR_TO_F765_MIGRATION.md` |
| Current known issues and reminders? | `MEMORY.md` |
| Effect system (FX bus, Effect types, FX sequencer)? | `knowledge_files/specification_reference/dsp_instruments_effects/EFFECTS_BUS_REFERENCE.md` |
| How the instrument DSP and modulation work, what they cost, how to extend them? | `knowledge_files/specification_reference/dsp_instruments_effects/INSTRUMENTS_DSP_REFERENCE.md` |
| Mixer, FX bus and Effect DSP, output pipeline, costs? | `knowledge_files/specification_reference/dsp_instruments_effects/EFFECTS_MIXER_DSP_REFERENCE.md` |
| Flash, sample flash and RAM layout? | `knowledge_files/specification_reference/STORAGE_SRAM_MANIFEST.md` |
| Testing a DSP change on the host? | `tools/dsp_test/DSP_TEST.md` |
| Module/API ownership and specifications? | `knowledge_files/specification_reference/` (indexed in `MEMORY.md`) |

## Confirmed Working Hardware
- LCD 4-bit parallel (PE7-PE12), TIM7 async driver
- LEDs: 74HC595x5 via SPI1
- Buttons: 74HC165x5 via SPI1 (40 inputs, 1kHz poll, event ring)
- SW43 SHIFT/BAR1 button (PB7) and LED (PB8)
- Main encoder SW42 (TIM1 IC, PE13/PE14, Dannegger + acceleration + rebound suppression)
- Endless pots RV1-RV4 (ADC1 DMA, atan2 delta tracking)
- Sliders RV5-RV10 (ADC1 DMA, PA0-PA5)
- Audio DAC1 (CS4344, I2S3, PA15/PB5/PC7/PC10), 24-bit signed payload
- Audio DAC2 (CS4344, I2S2, PB12/PB13/PB15/PC6), 24-bit signed payload
- MIDI DIN RX/TX (USART3, PB10 TX / PB11 RX, 31250 baud, interrupt-driven dual FIFO)
- USB MIDI (OTG_FS, PA11/PA12, enumerates as "Sonic Potions USB MIDI")
- SD card SPI bit-bang (PC12/PD2/PC8/PD0), SDHC confirmed
- CLK OUT jack (PC13)
- CLK IN jack (PD4, GPIO input pull-up, EXTI4 rising edge low-to-high)
- RST IN jack (PD5, GPIO input pull-up, EXTI5 rising edge low-to-high)
- OUT1 L/R jack detect (PD6/PD7, input pull-up, no plug=LOW, plug inserted=HIGH, sampled by foreground service)
- OUT2 L/R jack detect (PB4/PB6, no plug=LOW, plug inserted=HIGH, sampled by foreground service)
- I-Cache enabled (16KB, ICIALLU invalidate)
- D-Cache enabled (16KB) with MPU (WT for SRAM, Normal non-cacheable for DMA buffers since Session 073)
- DMA buffers in `.dma_nocache` linker section (MPU region 1, Normal non-cacheable; the DMA pack ISR ends with `DSB`)
- `audioOutBuffer` in DTCM (INDTCMZ, single-cycle access)
- Flash sector layout probed (Session 007): single-bank confirmed. Current layout: bootloader sector 0, application sectors 1–6, samples sectors 7–11 (`knowledge_files/specification_reference/STORAGE_SRAM_MANIFEST.md`)

### Clock Configuration (confirmed)
- HSE = 16MHz (ZQ1 crystal confirmed)
- SYSCLK = 216MHz: PLLM=16, PLLN=432, PLLP=2
- PCLK1 = 54MHz (APB1/4)
- PCLK2 = 108MHz (APB2/2)
- PLL48CLK = 48MHz (PLLQ=9) — USB
- PLLI2S: N=271, R=2 → 135.5MHz → Fs=44108Hz
- RCC_DCKCFGR2 (0x40023890): CLK48SEL=00 written explicitly
