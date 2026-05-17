# SD Card Investigation — LXR-02

## Status: UNVERIFIED — possible false positive on initial test

## What We Think We Know (needs verification)

- **Interface**: SPI mode (not SDMMC native) — inferred from:
  - Ribbon cable routing between SD socket and MCU (SDMMC 4-bit signaling is sensitive to impedance discontinuities through connectors)
  - Original LXR used SPI for SD
  - Component density around socket appears consistent with 4 signal traces not 6
- **SPI peripheral**: SPI1 (shared with LED/button shift registers)
- **CS pin**: PA8 — **UNVERIFIED, possible false positive**
- **MOSI**: PA7 (AF5) — assumed from SPI1 assignment, not traced to card
- **MISO**: PA6 (AF5) — assumed from SPI1 assignment, not traced to card
- **SCK**: PB3 (AF5) — assumed from SPI1 assignment, not traced to card

## The False Positive Problem

The original SD test (sdTest.c) sent CMD0 over SPI1 with PA8 as CS and received
`0x00` back. We interpreted this as "card responded, already initialised by bootloader".

However `0x00` is an ambiguous response:
- It could indicate a card in operating state (our interpretation)
- It could be a pull-down on MISO, bus noise, or a coincidence
- We did NOT confirm that PA8 is actually the CS line
- We did NOT confirm MOSI/MISO/SCK route to the card

## Physical Observations

- SD socket is XS9 on the SLXR4C IO board
- 6 visible traces leave the socket (photo taken, image shows yellow-marked traces)
- Expected signals: 3.3V, GND, SCK, CS, MOSI, MISO
- Socket part number visible: Molex 10621EC (push-push microSD)
- VD20 label visible near socket (likely the 3.3V supply diode/regulator)
- FB27, FB17, FB19, FB16, FB15 ferrite beads visible (power filtering)
- U20 IC visible nearby (function unknown)

## Standard microSD SPI Pin Assignment (reference)

For a push-push microSD socket in SPI mode, card pin numbers:

| Card Pin | Card Signal | SPI Function |
|----------|------------|--------------|
| 1 (DAT3) | DAT3 | CS (active low) |
| 2 (CMD) | CMD | MOSI |
| 3 (VSS) | GND | GND |
| 4 (VDD) | VDD | 3.3V |
| 5 (CLK) | CLK | SCK |
| 6 (VSS) | GND | GND |
| 7 (DAT0) | DAT0 | MISO |

DAT1 and DAT2 are NC in SPI mode.

## Required Verification Steps

1. Probe continuity from each of the 6 socket pads to known reference points:
   - 3.3V rail: any confirmed 3.3V point on board
   - GND: ground
   - PB3 (SCK): accessible at XP12 pin 10 on the header — **this is the easiest one to verify**
   - PA7 (MOSI): XP header pin unknown — needs tracing
   - PA6 (MISO): XP header pin unknown — needs tracing
   - PA8 (CS): XP header pin unknown — needs tracing

2. Once physical connections confirmed, re-run CMD0 test with verified pin assignments

3. If PA8 is NOT CS — identify which MCU pin actually connects to card CS pad,
   update sdTest.c accordingly

## Known SPI1 Conflicts

SPI1 is shared between:
- SD card (CS = PA8, unverified)
- LED shift registers 74HC595×5 (MOSI = PA7)
- Button shift registers 74HC165×5 (MISO = PA6)
- SCK shared = PB3
- LATCH for shift registers = PB2

TIM6 ISR exchanges LED/button data via SPI1 every 1ms. Any SD card access
must be coordinated to avoid collision — either disable TIM6 during SD access,
or use a mutex/flag. This is unresolved and must be addressed before FatFS works.

## XP Pin Unknowns for SD Signals

PA6 (MISO), PA7 (MOSI), PA8 (CS) XP connector pins have not been traced.
PB3 (SCK) = XP12 pin 10 (confirmed).

Connections document for the digital/SPI side of the board has not yet been
obtained — equivalent to what was done for pots/encoders. This is needed to
identify probe points for the remaining SPI signals at the ribbon headers.
