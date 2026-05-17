# Session Handoff Log — Session 1

**Project**: LXR-02 firmware port (STM32F765VIH6)
**Date**: 2026-04-19
**Session goal**: Reverse-engineer and confirm all hardware connections on the LXR-02 (STM32F765VIH6 TFBGA100). Port test firmware to verify each peripheral.

---

## Completed

- Confirmed ZQ1 = 16MHz HSE crystal (8MHz attempt produced octave-high tone)
- Confirmed and working: LCD, LEDs, buttons, main encoder, quad encoders (RV1-4), sliders (RV5-10), audio DAC1+2 (CS4344 I2S3/I2S2), MIDI DIN TX (USART3/PB10), USB MIDI (OTG_FS/PA11/PA12), SD card SPI (PA8=CS) *(later determined to be false positive — see SD_CARD_INVESTIGATION.md)*, CLK OUT (PC13), CLK IN (PD4, active LOW), RST IN (PD5, active LOW)
- SYSCLK brought to 216MHz (HSE/PLLM=16/PLLN=432/PLLP=2)
- RCC_DCKCFGR2 CLK48SEL written explicitly for USB 48MHz clock
- DCD_DevConnect() called explicitly after USBD_Init() to assert D+ pull-up (VBUS sensing disabled for ADUM3160 isolator)
- EXTI_IMR cleared at top of main() — bootloader leaves EXTI4/EXTI5 armed on CLK IN/RST IN pins; without this any voltage change on those jacks fires Default_Handler → freeze
- EXTI9_5_IRQHandler stub added (clears pending bits for EXTI0-15)
- Duplicate triggerJacks_isrTick() call in TIM6 ISR removed

## Verified on Hardware

All of the above confirmed working on physical LXR-02 board.

## Changes This Session

- `lxr02.tar.gz` delivered at end of session (final working tarball)

## Known Issues / Technical Debt

- LCD deadlock: lcd_wait_ms() blocks on SysTick while TIM6 spins on SPI1 BSY — can freeze if LCD called in tight loops during SPI contention. Non-blocking LCD driver needed before menu system.
- SPI1 contention: SD card shares SPI1 with LED/button TIM6 ISR. Needs mutex or TIM6 pause during SD access.
- TIM2 not initialised: needed for CLK IN BPM interval measurement (external clock sync).
- EXTI9_5_IRQHandler connected to sequencer calls commented out — needs TIM2 + sequencer before enabling.
- MidiParser RX not connected to sequencer/parameter system.
- Slider-to-parameter mapping not yet designed (new hardware, no original equivalent).
- GPIOD PD0, PD1, PD2, PD3, PD6, PD7 — unknown connections, do not configure unless traced.

## Next Session Recommended Goal

Non-blocking LCD driver, quad encoder port, menu stub.

## Critical Reminders for Next Session

- EXTI_IMR = 0 must remain as the very first operation in main(), before sysclk_init()
- Do NOT add pull-down to PD4 or PD5 — external circuit holds them HIGH at idle
- SPI1 is shared between SD card and TIM6 LED/button ISR — any SD access must coordinate
- lcd_wait_ms() is blocking — never call LCD functions from a context that can be interrupted by TIM6 SPI activity

---

## Post-Session 1 Discussion — Knowledge Base & Article Notes

**Topics covered** (not firmware changes):

### Project structure decided
- Two GitHub repos at https://github.com/brendanclarke:
  - Repo 1: Clean port of LXR 0.37 to LXR-02 hardware
  - Repo 2: Enhanced firmware (features documented in ENHANCED_FEATURES.md)
- Project knowledge base set up with 5 .md files: HARDWARE_MAP, AVR_TO_F765_MIGRATION, FIRMWARE_STATE, SESSION_HANDOFF_TEMPLATE, ENHANCED_FEATURES
- Workflow: upload lxr02.tar.gz + LXR-master.zip at start of each session; knowledge files provide persistent context
- Session log (this file) to be updated after each session

### Firmware format research (for WordPress article)
- **FIRMWARE.BIN** (original open-source LXR): 512-byte SPFI header (`SPFI` magic, CRC32 checksum, section count=3), followed by 39,424 bytes of ATmega644 AVR firmware, followed by 205,932 bytes of STM32F4 ARM firmware. Single file contained both processor images.
- **LXRV2IMG format** (LXR-02): 16-byte header: 8-byte magic `LXRV2IMG`, 4-byte payload size (LE uint32), 1-byte checksum `(~sum(payload)) & 0xFF` packed into a 4-byte slot (3 bytes zero padding), then raw ARM firmware. No AVR section — AVR eliminated, everything on single STM32F765.
- Checksum verification: bootloader checks `(sum(payload) + c) & 0xFF == 0xFF`
- MCU: STM32F765VIH6

### Article framing
- "The original LXR firmware was structured so that a single file contained firmware for both processors — a 512-byte SPFI header with magic number and CRC32 checksum, followed by the ATmega644 front panel firmware, then the STM32F4 audio engine firmware. The LXR-02, having eliminated the AVR entirely by consolidating everything onto a single STM32F765, uses a simpler format — a 16-byte LXRV2IMG header followed by a single ARM firmware image."
