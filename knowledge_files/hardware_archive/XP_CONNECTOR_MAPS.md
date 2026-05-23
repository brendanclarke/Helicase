# XP Connector Pin Mappings — LXR-02 MCU Daughterboard

Source: physical tracing (connections_pots_encoders.txt) cross-referenced with
confirmed firmware pin assignments. MCU ball descriptions are the trace routing
notes from the back of the PCB — they describe which balls a via/trace passes
between, not necessarily all connected balls.

✓ = confirmed by firmware testing
~ = inferred from routing + functional cross-reference
? = unknown

---

## XP9 (pin 1 = upper left, moving down left side)

| Pin | Front panel connection | MCU trace note | MCU Pin | Status |
|-----|----------------------|----------------|---------|--------|
| 1 | DD3 pin 2 | | ? | SPI/shift reg |
| 2 | DD4 pin 2 | | ? | SPI/shift reg |
| 3 | DD3 pin 9 | | ? | SPI/shift reg |
| 4 | DD2 pin 5, U11 pin 1 | | ? | SPI/shift reg |
| 5 | RV1 pin 1 (enc 1 phase A) | adjacent PB1 | **PB0** | ✓ ADC1 CH8 |
| 6 | RV2 pin 1 (enc 2 phase A) | via: PA5,PC5,PA6,PB0 | **PC4** | ✓ ADC1 CH14 |
| 7 | GND | | GND | |
| 8 | unknown | | ? | |
| 9 | RV5 pin 1 (slider) | via: PA0,PA4,PA1,PA5 | **PA5** | ✓ ADC1 CH5 |
| 10 | GND | | GND | |
| 11 | RV6 pin 1 (slider) | via: PC3,VDD,PA4,PC4 | **PA4** | ✓ ADC1 CH4 |
| 12 | GND | | GND | |
| 13 | DD2 pin 12 | | ? | SPI/shift reg |
| 14 | RV2 pin 3 (enc 2 phase B) | via: PA4,PC4,PA5,PC5 | **PC5** | ✓ ADC1 CH15 |
| 15 | RV1 pin 3 (enc 1 phase B) | between PA7 and PB1 | **PB1** | ✓ ADC1 CH9 |
| 16 | GND | | GND | |
| 17 | DD3 pin 12 | | ? | SPI/shift reg |
| 18 | DD4 pin 5 | | ? | SPI/shift reg |
| 19 | DD3 pin 5 | | ? | SPI/shift reg |
| 20 | GND | | GND | |

---

## XP10 (pin 1 = lower right/square pad, moving left along bottom then around)

| Pin | Front panel connection | MCU trace note | MCU Pin | Status |
|-----|----------------------|----------------|---------|--------|
| 3 | R73(1kΩ) → Encoder pin1 (ENC A) | adjacent PE13 | **PE13** | ✓ |
| 4 | Encoder switch pin1 | between PE13 and PB11 area | **PE15** | ✓ |
| 16 | MIDI optocoupler → DD1 level shifter | direct trace → PB11 | **PB11** | ✓ USART3 RX |
| 18 | R74(1kΩ) → Encoder pin3 (ENC B) | via: PE15,PD14,PB10,PB13 | **PE14** | ✓ |
| others | ? | | ? | |

Note: PD14 in the XP10 pin 18 trace description is not bonded in TFBGA100 — it is
a physical ball position reference only. The signal connects to PE14 (ENC B).
PB10 (MIDI TX/USART3) is also in the area of that via but connects via XP12, not XP10.

---

## XP12 (pin 1 = upper left, moving right along top)

| Pin | Front panel connection | MCU Pin | Status |
|-----|----------------------|---------|--------|
| 1 | GND | GND | |
| 8 | RST IN jack input | **PD5** | ✓ GPIO input pull-up, EXTI5 rising edge low-to-high |
| 9 | OUT1 R jack detect | **PD7** | ✓ input pull-up; no plug=LOW, plug inserted=HIGH |
| 10 | DD2 pin 2 (74HC165 CLK) | **PB3** | ✓ SPI1 SCK AF5 |
| 13 | OUT1 L jack detect | **PD6** | ✓ input pull-up; no plug=LOW, plug inserted=HIGH |
| 14 | CLK IN jack input | **PD4** | ✓ GPIO input pull-up, EXTI4 rising edge low-to-high |
| others | ? | ? | |

---

## XP13 (pin 1 = upper left/square pad, moving right along top)

| Pin | Front panel connection | MCU trace note | MCU Pin | Status |
|-----|----------------------|----------------|---------|--------|
| 2 | OUT2 R jack detect | traced under package between PB7/PB4 region | **PB6** | ✓ no plug=LOW, plug inserted=HIGH |
| 7 | RV4 pin 1 (enc 4 phase A) | between PC0 and VSSA | **PC0** | ✓ ADC1 CH10 |
| 8 | RV3 pin 1 (enc 3 phase A) | via: PC2,PE6,PC1,PC3 | **PC2** | ✓ ADC1 CH12 |
| 9 | RV9 pin 1 (slider) | between VDDA and VSS | **PA1** | ✓ ADC1 CH1 |
| 10 | RV7 pin 1 (slider) | adjacent PA3 | **PA3** | ✓ ADC1 CH3 |
| 12 | RV8 pin 1 (slider) | between PA3 and VDD | **PA2** | ✓ ADC1 CH2 |
| 13 | RV10 pin 1 (slider) | (not in MCU notes) | **PA0** | ✓ ADC1 CH0 |
| 14 | RV3 pin 3 (enc 3 phase B) | between NRST and PC0 | **PC3** | ✓ ADC1 CH13 |
| 15 | RV4 pin 3 (enc 4 phase B) | adjacent PC0 | **PC1** | ✓ ADC1 CH11 |
| 17 | CLK OUT → DD1 pin5 (74AHCT125 2A) | between PC13 and PE2 | **PC13** | ✓ GPIO output |
| 20 | OUT2 L jack detect | traced near PB4 ball | **PB4** | ✓ no plug=LOW, plug inserted=HIGH |
| others | ? | ? | ? | |

---

## Potentiometer / Encoder Wiring Summary

All analog controls are single-supply (Vs = 3.3V), wiper to MCU ADC pin.

**Quad encoders (dual-phase potentiometers, 4 pins):**
- Pin 1: Phase A → MCU ADC
- Pin 2: GND
- Pin 3: Phase B → MCU ADC
- Pin 4: Vs (3.3V)

| Control | Phase A | Phase B | ADC Ch A | ADC Ch B |
|---------|---------|---------|----------|----------|
| RV1 | PB0 | PB1 | CH8 | CH9 |
| RV2 | PC4 | PC5 | CH14 | CH15 |
| RV3 | PC2 | PC3 | CH12 | CH13 |
| RV4 | PC0 | PC1 | CH10 | CH11 |

**Sliders (single-phase, 2 pins + GND lug at bottom):**
- Pin 1: Wiper → MCU ADC
- Pin 2: Vs (3.3V)
- Bottom lug: GND

| Control | MCU Pin | ADC Ch |
|---------|---------|--------|
| RV5 | PA5 | CH5 |
| RV6 | PA4 | CH4 |
| RV7 | PA3 | CH3 |
| RV8 | PA2 | CH2 |
| RV9 | PA1 | CH1 |
| RV10 | PA0 | CH0 |

Note: RV5/RV6 physical label order on board is swapped — PA5=RV5, PA4=RV6
confirmed correct by slider response testing.

RV11 (volume slider) connects to U6 (TL072 op-amp), not to MCU — analog audio
volume control only, no firmware involvement.

**Main encoder (rotary + pushbutton, via resistors on XP10):**

| Signal | XP10 Pin | Series R | Pull-up R | MCU Pin |
|--------|----------|----------|-----------|---------|
| ENC A (rotary) | 3 | R73 1kΩ | R75 10kΩ to VCC | PE13 |
| ENC B (rotary) | 18 | R74 1kΩ | R76 10kΩ to VCC | PE14 |
| ENC SW (push) | 4 | — | none (internal pull-up used) | PE15 |

**Critical**: PE13 and PE14 have external 10kΩ pull-ups (R75, R76) — do NOT enable
internal pull-ups on these pins. Internal pull-ups fight the external circuit.
PE15 (switch) has no external pull — internal pull-up must be enabled in firmware.

R73/R74 (1kΩ) are series resistors for ESD/noise filtering.
RC time constant with PCB parasitic capacitance (~30pF): τ = 1kΩ × 30pF = 30ns (negligible for bounce).
No actual filter capacitors on the encoder lines — bounce is purely mechanical.

---

## Unknown / Untraced

The following signals are routed through XP connectors but specific pin numbers
have not been traced: SPI1 MOSI (PA7), SPI1 MISO (PA6), LATCH (PB2), LCD signals
(PE7-PE12), I2S2/I2S3 lines, USB D+/D- (PA11/PA12), MIDI TX (PB10),
most of XP12 pins beyond 8/9/10/13/14.

**SD card pin assignments (confirmed by tracing and working firmware):**
- PD0 = SD CS (active low)
- PD1 = SD DETECT
- PD2 = SD MOSI
- PC8 = SD MISO
- PC12 = SD SCLK

Note: PC12 was previously believed to be I2S3_SD. It is not — I2S3_SD is PB5.
PA8 (original SD CS guess) was a false positive — CMD0 0x00 response was the
74HC165 button shift register driving MISO low, not the SD card.

**Do not configure the following GPIOD pins without further tracing:**
PD3 — connection unknown, may be NC or connected to unidentified signals.
PD0, PD1, PD2, PD4, PD5, PD6, PD7 are confirmed and in use — do not reconfigure.
