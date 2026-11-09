# BBQ Monitor — Hardware Wiring Guide

This guide covers the planned battery build, which fits a 2000 mAh LiPo cell and
a PowerBoost 1000C inside the case. It has not been built or tested yet. The case
concept is in [`case-concept.html`](case-concept.html).

## Components

| # | Component | Notes |
|---|---|---|
| 1 | AZDelivery D1 Mini (ESP8266-12F) | Micro-USB version |
| 2 | DFRobot Gravity ADS1115 16-bit ADC | I2C, 4-channel, 32 × 32 mm |
| 3 | DFRobot Fermion MicroSD module | SPI, 28 × 20 mm |
| 4 | 2× 3.5mm stereo jack socket (TRS, panel mount) | Replaces the Adafruit terminal blocks to save space |
| 5 | 2× 100 kΩ resistor | Voltage divider, one per probe |
| 6 | Morpilot NTC probes | Meat + Grill |
| 7 | 2000 mAh 3.7 V LiPo cell, JST-PH | The Pi Hut SKU 105187, with protection circuit |
| 8 | Adafruit PowerBoost 1000C | Charger, load sharing and 5V boost |
| 9 | Small SPST or SPDT slide switch | Power switch, carries signal current only |
| 10 | 2-pin disconnect for the 5V lead | Lets you isolate the PowerBoost for wired flashing |

---

## System Block Diagram

```
 USB pack or charger
        │ micro-USB
        ▼
 ┌──────────────┐  JST-PH   ┌───────────┐
 │ PowerBoost   │◀─────────▶│ LiPo cell │
 │ 1000C        │           └───────────┘
 │              │
 │ EN ──────────┼──[ switch ]── GND      (switch closed = OFF)
 │ 5V ──────────┼──[ disconnect ]──▶ D1 Mini 5V
 │ GND ─────────┼──────────────────▶ D1 Mini G
 └──────────────┘

                    ┌──────────────────┐
                    │   D1 MINI        │
                    │ D1 (SCL) ────────┼──────────────────┐
                    │ D2 (SDA) ────────┼──────────────────┤  ADS1115
                    │                  │                  │  ┌───────────┐
                    │ D5 (CLK) ────────┼──────────────┐   ├─▶ SCL       │
                    │ D6 (MISO)◀───────┼──────────────┤   └─▶ SDA       │
                    │ D7 (MOSI)────────┼──────────────┤      │           │
                    │ D8 (CS)  ────────┼──────────────┤      │ A0 ◀──────┼── Meat divider
                    │                  │  SD CARD     │      │ A1 ◀──────┼── Grill divider
                    │ 5V   ────────────┼─▶ SD VCC                        │
                    │ 3.3V ────────────┼─▶ ADS1115 VCC, dividers         │
                    │ G ───────────────┼─▶ ADS1115 GND, SD GND, jacks    │
                    └──────────────────┘                     └───────────┘
```

---

## Power Wiring (PowerBoost 1000C)

| PowerBoost Pad | Connects To | Notes |
|---|---|---|
| JST socket | LiPo cell | Check polarity against the board's `+` and `-` marks before plugging in |
| 5V | D1 Mini 5V, through the disconnect | Boosted output, about 5.2V |
| GND | D1 Mini G | Common ground |
| EN | One side of the switch | |
| GND | Other side of the switch | Closing the switch turns the 5V output off |
| Micro-USB | External charger or USB pack | Mount this port at the case opening |
| BAT, USB, VS, LBO | Leave unconnected | |

> **Warning: do not plug USB into the D1 Mini while the PowerBoost 5V lead is
> connected.** Adafruit says not to apply voltage to the 5V pad. Separate the
> 5V disconnect first. OTA updates do not need the D1 Mini's USB port.

> **Run or charge:** the PowerBoost can charge the cell and power the device at
> the same time. Plug a USB pack into its micro-USB port during long cooks.

> **Charging while off:** Adafruit does not say whether the cell charges with EN
> tied to GND. Test this before relying on it.

> **Why boost to 5V:** the firmware assumes a fixed 3.3V divider supply
> (`SUPPLY_VOLTAGE` in `config.h`). Feeding the LiPo straight into the D1 Mini
> would let that rail sag as the cell runs down, which would shift the readings.

---

## D1 Mini → ADS1115 (I2C)

| D1 Mini Pin | Wire | ADS1115 Pin | Notes |
|---|---|---|---|
| 3.3V | Red | VCC | Power |
| G | Black | GND | Ground |
| D1 (GPIO5) | Yellow | SCL | I2C clock |
| D2 (GPIO4) | Blue | SDA | I2C data |

> **Address:** set the board's ADDR selector to **0x48**. This is what `config.h`
> expects. If you use 0x49, update `ADS1115_I2C_ADDRESS` in `config.h`.

---

## D1 Mini → SD Card Module (SPI)

| D1 Mini Pin | Wire | SD Module Pin | Notes |
|---|---|---|---|
| 5V | Red | VCC | See the power note below |
| G | Black | GND | Ground |
| D5 (GPIO14) | Orange | SCK | SPI clock |
| D6 (GPIO12) | Purple | MISO | SPI data in |
| D7 (GPIO13) | Green | MOSI | SPI data out |
| D8 (GPIO15) | White | SS | Chip select |

> **Power note:** DFRobot rates this module at 5V, so wire VCC to 5V. Check your
> own module's rating before powering it. A bare SD breakout with no onboard
> regulator or level shifting is 3.3V-only and must go on 3.3V instead.

---

## Probe Voltage Divider (build 2× — one per probe)

Each probe is a 2-terminal NTC thermistor. The tip of the 3.5mm plug is one
terminal; the sleeve is the other (GND). Wire up one divider per probe, and
solder each resistor close to its jack with heat shrink over the joints.

```
3.3V ──── [100 kΩ] ──┬── ADS1115 A0 (meat) or A1 (grill)
                     │
                 JACK TIP
                     │
                 NTC PROBE
                     │
                JACK SLEEVE ──── GND
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
| 5V | — | Power in | PowerBoost 5V, through the disconnect; SD VCC |
| D1 | GPIO5 | I2C SCL | ADS1115 SCL |
| D2 | GPIO4 | I2C SDA | ADS1115 SDA |
| D5 | GPIO14 | SPI CLK | SD SCK |
| D6 | GPIO12 | SPI MISO | SD MISO |
| D7 | GPIO13 | SPI MOSI | SD MOSI |
| D8 | GPIO15 | SPI CS | SD SS |
| 3.3V | — | Power rail | ADS1115 VCC, both 100 kΩ resistors |
| G | — | Ground rail | PowerBoost GND, ADS1115 GND, SD GND, jack sleeves |
| A0 | ADC | **Not used** | (ESP8266 has only 1 ADC — we use ADS1115 instead) |
| D0, D3, D4, TX, RX | — | Spare | — |

---

## Suggested Build Order

Build and test in this order so you can catch problems early:

1. **Power first** — with nothing connected to its 5V pad, plug the cell into the PowerBoost. Confirm about 5.2V on the pad with a multimeter, and that closing the switch turns it off. Plug in USB and confirm the charge LED lights.

2. **D1 Mini** — connect it to the PowerBoost 5V and GND. Confirm 3.3V on its rail before connecting anything else.

3. **ADS1115** — wire VCC, GND, SCL and SDA, and set the address to 0x48. Confirm `0x48` appears on an I2C scanner sketch.

4. **One probe divider** — wire the 100 kΩ resistor and one jack to ADS1115 A0. Plug the meat probe in and confirm a sensible resistance reading (~100 kΩ at room temp).

5. **Second probe divider** — repeat for ADS1115 A1 with the grill probe.

6. **SD card** — wire SCK, MISO, MOSI and SS. Use the `SD` library `CardInfo` example to confirm the card mounts and reads back a file.

7. **Full firmware** — disconnect the 5V lead before using the D1 Mini's USB port. Flash `bbq-thermometer.ino` and open Serial Monitor at 115200 baud to confirm each step of the boot sequence. Copy `config.h` from `config.example.h` first.

8. **Web UI** — upload `device/data/` folder to LittleFS using the LittleFS upload plugin. Open `http://<IP>` in a browser and confirm the LIVE indicator appears.

9. **Battery run** — reconnect the 5V lead and run from the cell with the probes in a known temperature. Compare the readings with USB power and re-run calibration if they have shifted.

---

## Power Budget

| Component | Current (approx) |
|---|---|
| D1 Mini (WiFi active) | 70–80 mA |
| ADS1115 | 2–3 mA |
| SD card (active write) | 30–50 mA |
| PowerBoost (enabled) | 5 mA |
| **Total** | **~105–135 mA** |

| Power Source | Estimated Runtime |
|---|---|
| 2000 mAh cell | 8.5–11 hours |
| 10,000 mAh USB pack, through the charge port | 40–50 hours, less while it also charges the cell |

The cell estimate assumes 7.4 Wh, 90% boost efficiency and 10% unusable capacity.
It has not been measured on the device. A full charge from empty will take a few
hours at the 1000 mA maximum, and longer from a low-current USB source.

---

## Battery Safety

- Keep the cell away from heat. Do not charge it while the case is hot or in direct sun.
- Fit a printed divider over the cell so solder joints and pins cannot press into it.
- Do not use a swollen, punctured or crushed cell.
- Do not leave the device charging unattended.

---

## Decoupling Capacitors (recommended, not required for breadboard)

Add these to the final build:
- 100 nF ceramic capacitor across ADS1115 VCC–GND (close to the board)
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
