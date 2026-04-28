# BBQ Monitor — Hardware Wiring Guide

## Components

| # | Component | Notes |
|---|---|---|
| 1 | AZDelivery D1 Mini (ESP8266-12F) | Your board |
| 2 | ADS1115 16-bit ADC module | I2C, 4-channel |
| 3 | Micro SD card module | SPI |
| 4 | 2× 3.5mm stereo jack socket (TRS, panel mount) | For the Morpilot probes |
| 5 | 2× 100 kΩ resistor | Voltage divider, one per probe |
| 6 | Morpilot NTC probes | Meat + Grill |
| 7 | USB battery pack | Existing |
| 8 | Perfboard + wire / jumpers | For prototyping |

---

## System Block Diagram

```
                    ┌──────────────────┐
                    │   D1 MINI        │
   USB battery ────▶│ 5V / GND        │
                    │                  │
                    │ D1 (SCL) ───────▶│──────────────────┐
                    │ D2 (SDA) ───────▶│──────────────────┤  ADS1115
                    │                  │                  │  ┌───────────┐
                    │ D5 (CLK) ───────▶│──────────────┐   ├─▶ SCL       │
                    │ D6 (MISO)◀───────│──────────────┤   ├─▶ SDA       │
                    │ D7 (MOSI)───────▶│──────────────┤   │  │           │
                    │ D8 (CS)  ───────▶│──────────────┤   │  │ A0 ◀──────┼── Meat divider
                    │                  │  SD CARD     │   │  │ A1 ◀──────┼── Grill divider
                    │ 3.3V ───────────▶│◀─────────────┘   └──▶ VDD       │
                    │ GND  ───────────▶│                     │ GND       │
                    └──────────────────┘                     └───────────┘
```

---

## D1 Mini → ADS1115 (I2C)

| D1 Mini Pin | Wire | ADS1115 Pin | Notes |
|---|---|---|---|
| 3.3V | Red | VDD | Power |
| GND | Black | GND | Ground |
| D1 (GPIO5) | Yellow | SCL | I2C clock |
| D2 (GPIO4) | Blue | SDA | I2C data |
| GND | Black | ADDR | Sets I2C address to **0x48** |

> **ADDR pin:** Tie ADDR to GND for address 0x48. This is what `config.h` expects.
> If you tie it to VCC you get 0x49 — update `ADS1115_I2C_ADDRESS` in `config.h`.

---

## D1 Mini → SD Card Module (SPI)

| D1 Mini Pin | Wire | SD Module Pin | Notes |
|---|---|---|---|
| 3.3V | Red | VCC | Power — use 3.3V not 5V |
| GND | Black | GND | Ground |
| D5 (GPIO14) | Orange | CLK | SPI clock |
| D6 (GPIO12) | Purple | MISO | SPI data in |
| D7 (GPIO13) | Green | MOSI | SPI data out |
| D8 (GPIO15) | White | CS | Chip select |

> **Power note:** Most SD modules have an onboard 3.3V regulator and level
> shifters. Use the 3.3V pin on the D1 Mini anyway to be safe. Check your
> module's datasheet — if it says "accepts 5V" you can use 5V, but 3.3V always works.

---

## Probe Voltage Divider (build 2× — one per probe)

Each probe is a 2-terminal NTC thermistor. The tip of the 3.5mm plug is one
terminal; the sleeve is the other (GND). Wire up one divider circuit per probe:

```
3.3V ──┬── 100 kΩ ──┬── ADS1115 channel ──┬── 3.5mm jack TIP
       │             │                     │   (probe terminal 1)
       │             │        PROBE        │
       │             │     ┌─────────┐     │
       │             └─────┤  NTC    ├─────┘
       │                   └─────────┘
       │                        │
      GND ─────────────────────┘
                            3.5mm jack SLEEVE
                            (probe terminal 2)
```

### Simplified view

```
3.3V ──── [100 kΩ] ──── ADS1115 A0 ──── PROBE ──── GND
3.3V ──── [100 kΩ] ──── ADS1115 A1 ──── PROBE ──── GND
```

### 3.5mm TRS Jack Wiring

| Jack Contact | Connects To |
|---|---|
| TIP | Junction of 100 kΩ resistor + ADS1115 channel |
| RING | Leave unconnected (or bridge to sleeve) |
| SLEEVE | GND |

---

## ADS1115 Channel Assignments

| ADS1115 Channel | Probe | D1 Mini config.h constant |
|---|---|---|
| A0 | Meat probe | `ADS1115_CH_MEAT 0` |
| A1 | Grill / ambient probe | `ADS1115_CH_GRILL 1` |
| A2 | Spare | — |
| A3 | Spare | — |

---

## Full Pin Reference — D1 Mini

| D1 Mini Label | GPIO | Used For | Connected To |
|---|---|---|---|
| D1 | GPIO5 | I2C SCL | ADS1115 SCL |
| D2 | GPIO4 | I2C SDA | ADS1115 SDA |
| D5 | GPIO14 | SPI CLK | SD CLK |
| D6 | GPIO12 | SPI MISO | SD MISO |
| D7 | GPIO13 | SPI MOSI | SD MOSI |
| D8 | GPIO15 | SPI CS | SD CS |
| 3.3V | — | Power rail | ADS1115 VDD, SD VCC |
| GND | — | Ground rail | ADS1115 GND+ADDR, SD GND, probe sleeves |
| A0 | ADC | **Not used** | (ESP8266 has only 1 ADC — we use ADS1115 instead) |
| D0, D3, D4, TX, RX | — | Spare | — |

---

## Suggested Breadboard Build Order

Build and test in this order so you can catch problems early:

1. **Power first** — connect D1 Mini to USB, confirm 3.3V on the rail with a multimeter before connecting anything.

2. **ADS1115** — wire VDD, GND, SCL, SDA, ADDR. Flash a basic I2C scanner sketch and confirm address `0x48` appears in Serial Monitor.

3. **One probe divider** — wire the 100 kΩ resistor and one 3.5mm jack to ADS1115 A0. Plug the meat probe in. Flash a thermistor test sketch and confirm a sensible resistance reading (~100 kΩ at room temp).

4. **Second probe divider** — repeat for ADS1115 A1 with the grill probe.

5. **SD card** — wire CLK, MISO, MOSI, CS. Use the `SD` library `CardInfo` example to confirm the card mounts and reads back a file.

6. **Full firmware** — flash `bbq-thermometer.ino`. Open Serial Monitor at 115200 baud to confirm each step of the boot sequence. Copy `config.h` from `config.example.h` first.

7. **Web UI** — upload `device/data/` folder to LittleFS using the LittleFS upload plugin. Open `http://<IP>` in a browser and confirm the LIVE indicator appears.

---

## Power Budget

| Component | Current (approx) |
|---|---|
| D1 Mini (WiFi active) | 70–80 mA |
| ADS1115 | < 1 mA |
| SD card (active write) | 30–50 mA |
| **Total** | **~100–130 mA** |

A 10,000 mAh USB battery pack provides an estimated **70–90 hours** of runtime.

---

## Decoupling Capacitors (recommended, not required for breadboard)

When you move to perfboard, add:
- 100 nF ceramic capacitor across ADS1115 VDD–GND (close to the chip)
- 100 nF ceramic capacitor across SD module VCC–GND
- 10 µF electrolytic across the 3.3V rail overall

These reduce noise on the ADC readings and prevent SD card write failures.

---

## Calibration Reference Temperatures

When running the probe calibration tool in the web UI:

| Point | Medium | Temperature |
|---|---|---|
| 1 | Ice water (well stirred, 50/50 ice and water) | 0.0°C |
| 2 | Room temperature water + reference thermometer | ~20–25°C |
| 3 | Boiling water (at sea level) | 100.0°C |

> Boiling point drops ~0.3°C per 100 m altitude. At 500 m use 98.5°C,
> at 1000 m use 96.7°C.

Leave the probe in each medium for 30 seconds before recording to let
the temperature stabilise.
