# LXR-02 Hardware Map — STM32F765VIH6 TFBGA100

All pins confirmed by physical measurement unless noted.

## MCU
- **Part**: STM32F765VIH6, TFBGA100 package
- **SYSCLK**: 216MHz (HSE 16MHz crystal ZQ1, PLLM=16, PLLN=432, PLLP=2)
- **PCLK1**: 54MHz (APB1/4) — TIM6, USART3, I2S2, I2S3
- **PCLK2**: 108MHz (APB2/2) — SPI1, ADC
- **PLL48CLK**: 48MHz (PLLQ=9) — USB OTG FS
- **PLLI2S**: N=271, R=2 → 135.5MHz → Fs=44108Hz

## Audio
| Signal | Pin | AF | Notes |
|--------|-----|----|-------|
| I2S3_WS | PA15 | AF6 | DAC1 (CS4344 U23) |
| I2S3_CK | PC10 | AF6 | DAC1 |
| I2S3_SD | PB5  | AF6 | DAC1 |
| I2S3_MCK | PC7 | AF6 | DAC1 |
| I2S2_WS | PB12 | AF5 | DAC2 (CS4344 U24) |
| I2S2_CK | PB13 | AF5 | DAC2 |
| I2S2_SD | PB15 | AF5 | DAC2 |
| I2S2_MCK | PC6 | AF5 | DAC2 |

Audio uses circular DMA, no ISR. Both CS4344s output simultaneously.
24-bit I2S: DATLEN=01, CHLEN=1. DMA buffers are int16_t packed as MSW+LSW pairs.

## MIDI
| Signal | Pin | Notes |
|--------|-----|-------|
| USART3 TX (MIDI OUT) | PB10 | AF7, 31250 baud, BRR=0x06C0 at 54MHz PCLK1 |
| USART3 RX (MIDI IN) | PB11 | AF7 |

## USB MIDI
| Signal | Pin | Notes |
|--------|-----|-------|
| OTG_FS DM | PA11 | AF10 |
| OTG_FS DP | PA12 | AF10 |

Via ADUM3160 isolator. VBUS detection disabled (VBDEN=0). DCD_DevConnect() called explicitly after USBD_Init() because VBUS sensing ISR never fires.

## SD Card (bit-bang SPI)
| Signal | Pin | Notes |
|--------|-----|-------|
| SCLK   | PC12 | GPIO output |
| MOSI   | PD2  | GPIO output |
| MISO   | PC8  | GPIO input |
| CS     | PD0  | GPIO output, active low |
| DETECT | PD1  | GPIO input (card detect, optional) |

**Critical**: SD card is NOT on hardware SPI1. PA8 (original CS guess) was a false positive —
CMD0 response of 0x00 was the 74HC165 button shift register driving MISO low, not the SD card.
Actual SD pins confirmed by tracing and working bit-bang driver in Core/Hardware/SD/spi_sd.c.
SDHC confirmed working. Bootloader also uses bit-bang on these pins.

PD0 and PD1 are now confirmed connected (SD CS and DETECT). Do not treat as unknown.
PD2 confirmed as SD MOSI. PC8 confirmed as SD MISO. PC12 confirmed as SD SCLK.

## LED/Button Shift Registers (SPI1)
| Signal | Pin | Notes |
|--------|-----|-------|
| SPI1 MOSI | PA7 | LED data (74HC595 ×5) |
| SPI1 MISO | PA6 | Button data (74HC165 ×5) |
| SPI1 SCK | PB3 | Shared clock |
| LATCH | PB2 | Active HIGH — rising edge latches 595s and loads 165s |
| SW43 LED | PB8 | SHIFT/BAR1 LED, active HIGH, GPIO |
| SW43 SW | PB7 | SHIFT/BAR1 button, active HIGH, GPIO |

SPI exchange happens in TIM6_DAC_IRQHandler at 1kHz.

## LCD (4-bit parallel)
| Signal | Pin |
|--------|-----|
| RS | PE12 |
| E | PE11 |
| DB4 | PE7 |
| DB5 | PE8 |
| DB6 | PE9 |
| DB7 | PE10 |

Non-blocking TIM7-driven async queue (128 entries, 10kHz drain rate).
lcd_init() is still blocking but runs before TIM7 starts — safe.

## Main Encoder
| Signal | Pin | Notes |
|--------|-----|-------|
| ENC A | PE13 | AF1 (TIM1_CH3). External 10kΩ pull-up (R75). NO internal pull-up. |
| ENC B | PE14 | AF1 (TIM1_CH4). External 10kΩ pull-up (R76). NO internal pull-up. |
| ENC SW | PE15 | Internal pull-up (no external pull resistor on switch line). |

TIM1 Input Capture, ICxF=0xF hardware filter. Dannegger algorithm. encode_read4() only.

## Endless Pots / Analog Quadrature Encoders (RV1-4) — ADC1
| Encoder | A Pin | B Pin | ADC Ch |
|---------|-------|-------|--------|
| RV1 | PB0 | PB1 | CH8/CH9 |
| RV2 | PC4 | PC5 | CH14/CH15 |
| RV3 | PC2 | PC3 | CH12/CH13 |
| RV4 | PC0 | PC1 | CH10/CH11 |

atan2-based delta tracking. DO NOT change atan2f(b, a) argument order.

## Sliders (RV5-10) — ADC1
| Slider | Pin | ADC Ch |
|--------|-----|--------|
| RV5 | PA5 | CH5 |
| RV6 | PA4 | CH4 |
| RV7 | PA3 | CH3 |
| RV8 | PA2 | CH2 |
| RV9 | PA1 | CH1 |
| RV10 | PA0 | CH0 |

ADC1: 14-channel circular scan via DMA2 Stream0 Ch0. 480-cycle sample time. 4MHz ADC clock.

## Rear Panel Jacks
| Jack | Signal | MCU Pin | Logic | Notes |
|------|--------|---------|-------|-------|
| XS3 | CLK OUT | PC13 | Active HIGH | Via DD1 74AHCT125 level shifter pin 5(2A)→pin 6(2Y) |
| XS1 | CLK IN | PD4 | Active LOW | Via VT1 transistor, inverted. Idle=HIGH, signal=LOW |
| XS2 | RST IN | PD5 | Active LOW | Via VT2 transistor, inverted. Idle=HIGH, signal=LOW |
| OUT1 L detect | Jack detect switch | PD6 | Active HIGH on insert | No plug=0V (GND), plug inserted=~3.2V |
| OUT1 R detect | Jack detect switch | PD7 | Active HIGH on insert | No plug=0V (GND), plug inserted=~3.2V |
| OUT2 L detect | Jack detect switch | PB4 | Active HIGH on insert | No plug=0V (GND), plug inserted=~3.2V |
| OUT2 R detect | Jack detect switch | PB6 | Active HIGH on insert | No plug=0V (GND), plug inserted=~3.2V |

**Critical**: EXTI4 and EXTI5 are armed by the bootloader for CLK IN and RST IN. At application startup, `EXTI_IMR` must be written to 0x00000000 before any other init, otherwise any voltage change on those jacks fires an unhandled EXTI → Default_Handler → freeze. This is done at the top of main() before sysclk_init().

On STM32F765, EXTI4 has its own IRQ10. CLK IN on PD4 is handled by `EXTI4_IRQHandler`; RST IN on PD5 is handled by `EXTI9_5_IRQHandler` through EXTI5. Session 021 also routes OUT1 detect inputs PD6/PD7 through EXTI9_5 (both edges) for change-driven updates.

## Clocks / Interrupts Summary
| IRQ | Priority | Handler | Period | Function |
|-----|----------|---------|--------|----------|
| SysTick | 0 (highest) | SysTick_Handler | 1ms | lcd_ms_ticks for LCD blocking delays only |
| TIM1_CC (IRQ27) | 1 | TIM1_CC_IRQHandler | event | Main encoder A/B input capture |
| TIM6_DAC (IRQ54) | 1 | TIM6_DAC_IRQHandler | 1ms (1kHz) | LED/button SPI, OUT2 detect sample (PB4/PB6), systick |
| TIM7 (IRQ55) | 2 | TIM7_IRQHandler | 100µs (10kHz) | LCD async queue drain |
| EXTI4 (IRQ10) | 3 | EXTI4_IRQHandler | event | CLK IN (EXTI4/PD4) |
| EXTI9_5 (IRQ23) | 3 | EXTI9_5_IRQHandler | event | RST IN (EXTI5/PD5) + OUT1 detect edges (PD6/PD7) |
| OTG_FS (IRQ67) | 5 | OTG_FS_IRQHandler | event | USB MIDI |

## Package Notes
- TFBGA100: PD0-PD7 are bonded (PD8-PD15 are NOT present)
- GPIOD is fully functional but must have AHB1ENR bit 3 set before register access
- PD0=SD CS, PD1=SD DETECT, PD2=SD MOSI — now confirmed, no longer unknown
- PD4/PD5 idle HIGH because external VT1/VT2 circuit holds them there
- Do NOT configure PD4/PD5 with pull-down — this fights the external circuit
- PD6/PD7 are now confirmed as OUT1 L/R jack-detect inputs
