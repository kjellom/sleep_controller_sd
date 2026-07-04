# sleep_controller_sd

ESP32-based personal air chiller controller with ambient audio masking.

## Overview

A closed-loop environmental controller built around an ESP32 dev board on a SunFounder Camera Extension board. Controls two 120mm fans and an ultrasonic mister to cool a gel-pack-based personal chiller, while playing looping ambient audio (rain sounds, etc.) for sleep sound masking. Controlled via a single rotary encoder with OLED display feedback.

## Hardware

| Component | Part | Notes |
|---|---|---|
| Microcontroller | ESP32-WROOM-32 on SunFounder Camera Extension | SD_MMC via extension board |
| DAC | PCM5102 I2S DAC module | Stereo, external I2S |
| Amplifiers | TDA2050 mono amplifier boards ×2 | Volume via onboard pot |
| Speakers | MakerHawk 10W 4Ω ×2 | One per TDA2050 |
| Fans | Pano-mounts CF12025BLA2 120mm 12V ×2 | 2-wire, MOSFET controlled |
| Mister | 24V ultrasonic mister | MOSFET controlled |
| Display | SSD1306 128×32 OLED | I2C |
| Encoder | Rotary encoder with pushbutton | Fan speed + UI control |
| Temp/humidity | DHT22 | 2m shielded cable |
| MOSFETs | IRLZ44N ×3 | Logic-level N-channel |
| Power | 12V 5A supply + 24V 1A supply | Dual barrel jack input |

## Pin Assignments

| Pin | Function |
|---|---|
| IO2 | SD_MMC D0 (internal) |
| IO4 | PCM5102 DIN |
| IO5 | Encoder SW |
| IO14 | SD_MMC CLK (internal) |
| IO15 | SD_MMC CMD (internal) |
| IO18 | Encoder CLK |
| IO19 | Encoder DT |
| IO21 | OLED SDA |
| IO22 | OLED SCL |
| IO23 | DHT22 data |
| IO25 | PCM5102 LCK |
| IO26 | PCM5102 BCK |
| IO27 | Fan 2 MOSFET gate |
| IO32 | Fan 1 MOSFET gate |
| IO33 | Mister MOSFET gate |
| IO12 | **UNCONNECTED** — boot strapping pin |
| IO13 | **RETIRED** — strapping pin, caused SD_MMC failures |

## Software

### Libraries Required
- ESP8266Audio by Earle F. Philhower III
- Adafruit SSD1306
- Adafruit GFX Library
- Adafruit DHT (pending)
- ESP32 Arduino core 3.3.10

### Controls
| Action | Function |
|---|---|
| Encoder turn | Fan speed 0–100% |
| Short press | Toggle fans on/off |
| Long press | Toggle audio play/pause |
| Double click | (planned) Toggle WiFi |
| Triple click | (planned) WiFi file manager |

### Audio Files
Place in SD card root:
- `/audio_a.wav` — left channel → TDA2050 A
- `/audio_b.wav` — right channel → TDA2050 B
- Format: 22050Hz, 16-bit, mono WAV

## Branch Structure

- `main` — stable working code, all hardware verified on breadboard
- `wifi-ota` — WiFi file manager + OTA firmware updates (in development)
- `closed-loop` — DHT22 sensor + bang-bang control logic (pending hardware)

## Development Status

### Working
- Dual stream WAV audio via PCM5102 → TDA2050 → speakers
- Rotary encoder fan speed control
- Fan MOSFET switching (IO32, IO27)
- SSD1306 OLED display with auto-dim
- Button short/long press logic
- SD card file system via SD_MMC

### Pending
- Mister MOSFET (IO33) integration
- DHT22 temp/humidity sensor (shielded cable on order)
- Closed-loop bang-bang control
- NVS setpoint persistence
- UI state machine for setpoint editing
- WiFi file manager + OTA updates
- Bode plot characterization of TDA2050 chain

## Known Issues / Lessons Learned

- **IO13** — strapping pin, permanently retired. Caused SD_MMC boot failures when used as LEDC PWM output.
- **IO12** — leave completely unconnected. Boot strapping pin.
- **PCM5102 strap pins** — XSMT, FMT, FLT, DEMP must all be tied to definite logic levels. Floating FMT causes loud noise.
- **Gate resistors** — 100Ω (brown-black-brown), not 100kΩ (brown-black-yellow).
- **SD card slot** — spring-loaded slot on SunFounder board is sensitive to physical disturbance.
- **Star ground** — buck converter input and output GND on separate wires to common point. USB shield must tie to common ground.
- **WAV header** — use `findWAVDataOffset()` parser, not fixed 44-byte offset.
- **RC soft-start** — 10Ω + 1000µF on 5V buck output recommended for permanent build.

## License

Private project — Nereid Expeditions LLC
