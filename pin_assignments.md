# Sleep controller — pin assignments
## Rev 7 — IO13 removed, Fan 2 → IO27, Mister → IO33

## Assigned pins

| Pin | Function | Protocol | Notes |
|---|---|---|---|
| IO2 | SD_MMC D0 | SD_MMC | 1-bit mode — internal to extension board |
| IO4 | PCM5102 DIN | I2S | data — moved from IO22 to avoid I2C conflict |
| IO5 | Encoder SW | GPIO | INPUT_PULLUP |
| IO14 | SD_MMC CLK | SD_MMC | 1-bit mode — internal to extension board |
| IO15 | SD_MMC CMD | SD_MMC | 1-bit mode — internal to extension board |
| IO18 | Encoder CLK | GPIO | INPUT_PULLUP |
| IO19 | Encoder DT | GPIO | INPUT_PULLUP — ISR on CHANGE |
| IO21 | OLED SDA | I2C | Wire.begin() default |
| IO22 | OLED SCL | I2C | Wire.begin() default — exclusive, no sharing |
| IO23 | DHT22 data | 1-wire | 100Ω series + 1kΩ/2kΩ divider at ESP32 end; 4.7kΩ pull-up at sensor end |
| IO25 | PCM5102 LCK | I2S | left-right / word select clock |
| IO26 | PCM5102 BCK | I2S | bit clock |
| IO27 | Fan 2 MOSFET gate | LEDC PWM | moved from IO13 — clean bidirectional, no restrictions |
| IO32 | Fan 1 MOSFET gate | LEDC PWM | clean bidirectional, no restrictions |
| IO33 | Mister MOSFET gate | LEDC PWM | 4.7kΩ board pull-up present — pull-down resistor on gate still required |

---

## Removed from assignment

| Pin | Previous use | Reason |
|---|---|---|
| IO13 | Fan 2 MOSFET gate | Strapping pin caused repeated SD_MMC boot failures — permanently reassigned |

---

## Must leave unconnected

| Pin | Reason |
|---|---|
| IO12 | Boot strapping pin — flash voltage on WROOM modules. Caused SD_MMC init failure during development. Do not connect anything here. |
| IO13 | Strapping pin for flash voltage — now unassigned. Leave floating or tie to GND only, never to a driven signal. |

---

## Available for future expansion

| Pin | Type | Notes |
|---|---|---|
| IO34 | Input only | No internal pull-up/down, no output capability |
| IO35 | Input only | No internal pull-up/down, no output capability |
| I36 | Input only | No internal pull-up/down, no output capability |
| I39 | Input only | No internal pull-up/down, no output capability |
| IO0 | Bidirectional | Boot sensitive — avoid pulling LOW at power-on |

---

## PCM5102 strap pins — all required

| Pin | Connection | Consequence if floating |
|---|---|---|
| XSMT | 3.3V | Output muted or noisy |
| FMT | GND | Wrong data format — noise output |
| FLT | GND | Undefined filter behavior |
| DEMP | GND | Undefined de-emphasis state |

---

## DHT22 termination detail

| Location | Component | Value | Purpose |
|---|---|---|---|
| Sensor end | Pull-up resistor | 4.7kΩ DATA to VCC | Required for 1-wire; at sensor end for long-run reliability |
| ESP32 end | Series resistor | 100Ω in DATA line | Limits reflections on long cable |
| ESP32 end | Voltage divider R1 | 1kΩ DATA to IO23 | Level shift: 5V DHT22 → 3.3V ESP32 safe |
| ESP32 end | Voltage divider R2 | 2kΩ IO23 to GND | Level shift lower leg — divides to 3.33V |
| Cable shield | Ground | ESP32 end only | One-end grounding prevents ground loop |

---

## Power architecture

| Rail | Source | Loads |
|---|---|---|
| 12V | PSU via barrel jack + rocker switch | TDA2050 A, TDA2050 B, fan MOSFETs, mister MOSFET drain, both buck converter inputs |
| 24V | Mister PSU via barrel jack 2 | Mister load only |
| 5V logic | Buck module 1 (12V → 5V) → ESP32 VIN | ESP32, OLED, DHT22 VCC, encoder |
| 5V audio | Buck module 2 (12V → 5V) | PCM5102 VCC (dedicated clean rail) |
| 3.3V | ESP32 onboard AMS1117 | ESP32 core, OLED logic, encoder VCC, PCM5102 strap pins |

---

## MOSFET wiring (per channel)

```
ESP32 GPIO ──[100Ω]──┬── GATE
                     │
                   [10kΩ]
                     │
                    GND

DRAIN → Fan/Mister – lead
SOURCE → GND
12V → Fan/Mister + lead
1N4007 cathode → 12V/24V rail
1N4007 anode → DRAIN
```

---

## Notes

- **IO2/14/15** — connected internally via SunFounder extension board traces to SD card slot. Do not add external wires to these terminals.
- **IO13** — permanently retired. Strapping pin behavior caused repeated SD_MMC boot failures when used as LEDC PWM output. Do not reassign.
- **IO22** — exclusively I2C SCL. PCM5102 DIN moved to IO4 to avoid sharing.
- **IO33** — 4.7kΩ board pull-up present. Pull-down resistor on mister MOSFET gate still required to ensure MOSFET stays off at boot.
- **Star ground** — all GNDs tie to single common point. Buck converter input GND and output GND on separate wires to star, not daisy-chained. USB shield on ESP32 dev board must connect to common ground.
- **SD_MMC 1-bit mode** — pass `true` as second argument to `SD_MMC.begin("/sdcard", true)`. Avoids IO12 strapping issue in 4-bit mode.
- **Boot power sequencing** — ESP32 boots reliably from buck converter 5V via VIN pin. IO13 removal resolved previous boot instability. RC soft-start (10Ω + 1000µF) recommended for permanent build.
- **Resistor values** — double check 100Ω (brown-black-brown) vs 100kΩ (brown-black-yellow) gate resistors. Confusion caused fan control failure during development.
