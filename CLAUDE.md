# CLAUDE.md — sleep_controller_sd project context

This file provides context for Claude Code working on the sleep_controller_sd project.
Read this before making any changes to the codebase.

---

## Project summary

ESP32-based personal air chiller controller with ambient audio masking.
Developed by Jacques (Kjell Nelson), Nereid Expeditions LLC.
Built on ESP32-WROOM-32 dev board seated in SunFounder Camera Extension board.

The device controls two 120mm 12V fans and a 24V ultrasonic mister to cool
a gel-pack chiller enclosure, while playing looping ambient WAV audio through
a dual mono TDA2050 amplifier system for sleep sound masking.

---

## Critical constraints — read before touching anything

### Pins that must never be used for driven signals
- **IO12** — boot strapping pin (flash voltage). Leave completely unconnected.
  Any connection at power-on risks boot failure or flash corruption.
- **IO13** — strapping pin. Caused repeated SD_MMC boot failures when used
  as LEDC PWM output. Permanently retired. Do not reassign.

### IO22 is exclusively I2C SCL
PCM5102 DIN was deliberately moved to IO4 to free IO22 for I2C.
Do not share IO22 with any other peripheral.

### SD_MMC must initialize before I2S
SD_MMC.begin() must be called in setup() before audioTask is launched.
audioTask has a 2000ms delay at startup to let SD_MMC settle.
Race conditions between SD_MMC and I2S initialization have caused
mount failures — do not remove or shorten this delay without testing.

### PCM5102 strap pins are all required
XSMT → 3.3V (floating = muted or noisy)
FMT  → GND  (floating = wrong data format, loud noise)
FLT  → GND
DEMP → GND
These are hardware connections, not software concerns, but document
any changes to the wiring clearly.

### IO33 has a 4.7kΩ board pull-up
The SunFounder extension board has a pull-up on IO33.
The mister MOSFET gate pull-down resistor is still required to
ensure the MOSFET stays off at boot.

### ESP32 Arduino core version
Stay on core **3.3.10**. Do not downgrade to 2.x.
The audio system uses AudioOutputI2S::EXTERNAL_I2S which works
correctly on 3.x. The internal DAC approach (which required 2.x)
was abandoned in favour of the PCM5102 external DAC.

### LEDC API (core 3.x)
Use ledcAttach(pin, freq, resolution) — not the old ledcSetup/ledcAttachPin.
Use ledcWrite(pin, duty) — not ledcWrite(channel, duty).

---

## Current pin assignments

| Pin | Function | Protocol | Status |
|---|---|---|---|
| IO2 | SD_MMC D0 | SD_MMC | Working |
| IO4 | PCM5102 DIN | I2S | Working |
| IO5 | Encoder SW | GPIO | Working |
| IO14 | SD_MMC CLK | SD_MMC | Working |
| IO15 | SD_MMC CMD | SD_MMC | Working |
| IO18 | Encoder CLK | GPIO | Working |
| IO19 | Encoder DT | GPIO | Working |
| IO21 | OLED SDA | I2C | Working |
| IO22 | OLED SCL | I2C | Working — exclusive |
| IO23 | DHT22 data | 1-wire | Pending hardware |
| IO25 | PCM5102 LCK | I2S | Working |
| IO26 | PCM5102 BCK | I2S | Working |
| IO27 | Fan 2 MOSFET | LEDC PWM | Working |
| IO32 | Fan 1 MOSFET | LEDC PWM | Working |
| IO33 | Mister MOSFET | LEDC PWM | Pending integration |

Available for expansion: IO34, IO35, I36, I39 (input only), IO0 (boot sensitive)

---

## Architecture

### FreeRTOS task layout
- **Core 0** → audioTask (I2S DMA, time-critical)
- **Core 1** → loop() (encoder, display, fan PWM)

Shared state between cores is protected by `stateMutex`.
Always use xSemaphoreTake/xSemaphoreGive around audioPlaying reads/writes.

### Audio system
Dual stream: two WAV files read sample-by-sample from SD card,
interleaved as stereo L/R frames, pushed to PCM5102 via I2S.
Left channel → TDA2050 A → speaker A
Right channel → TDA2050 B → speaker B

WAV files must be: 22050Hz, 16-bit, mono, standard PCM WAV.
findWAVDataOffset() parses the RIFF header to find the data chunk —
do not replace this with a fixed 44-byte offset assumption.

Audio loops independently per file. Files can be different lengths.
Loop is implemented by seeking back to offsetA/offsetB on EOF.

### Fan control
MIN_DUTY_PCT = 3 (fans spin reliably from full stop at this level)
encoderPos 0–100 maps to MIN_DUTY_PCT–100% of PWM_MAX
Both fans share the same duty cycle from the single encoder.
Fan state (on/off) is independent of encoder position.

### OLED
displayOK boolean gates all display calls — sketch runs without display.
Auto-dims after DIM_TIMEOUT_MS (10 seconds) of inactivity.
registerActivity() call restores brightness and resets timer.

### Button logic
Short press (<600ms) → toggle fans on/off
Long press (>600ms) → toggle audio play/pause
Double click → (planned) WiFi toggle
Triple click → (planned) WiFi file manager

---

## Branch structure

### main
Stable working code. All hardware verified on breadboard:
- Dual stream audio ✓
- PCM5102 I2S DAC ✓
- Both TDA2050 amplifiers ✓
- OLED display ✓
- Rotary encoder ✓
- Fan MOSFETs IO32/IO27 ✓

### wifi-ota (create from main)
WiFi file manager + OTA firmware updates.
Requirements:
- Triple-click encoder toggles WiFi on/off
- mDNS hostname: sleep-controller.local
- Web interface: file list, upload, download, delete for SD card root
- OTA: firmware upload page in web interface + Arduino OTA
- OLED shows IP address when WiFi active
- Audio continues during WiFi — do not pause for file operations
- Open access (no password) on local LAN
- Credentials stored in NVS, not hardcoded in sketch
- WiFiManager for credential setup via captive portal

Libraries to add:
- ESPAsyncWebServer
- AsyncTCP
- ArduinoOTA
- ESPmDNS
- Preferences (for NVS credential storage)

### closed-loop (create from main, merge after wifi-ota)
DHT22 sensor integration + bang-bang control.
Requirements:
- DHT22 on IO23, 2-second sampling interval
- 5V supply, voltage divider on data line (1kΩ/2kΩ) for 3.3V safe input
- Bang-bang with hysteresis:
  - Temp > setpoint + 0.5°C → fans on at encoder speed
  - Temp < setpoint − 0.5°C → fans off
  - Humidity < setpoint − 3% RH → mister on (IO33)
  - Humidity > setpoint + 3% RH → mister off
- Setpoint ranges: temp 15–30°C, humidity 40–80% RH
- NVS persistence: temp setpoint, humidity setpoint, audio play/pause
- UI state machine:
  - Normal: OLED shows temp setpoint, humidity setpoint, audio state
  - Single press: cycle edit mode (temp → humidity → normal)
  - Turn in edit mode: adjust setpoint (±0.5°C or ±1% RH per click)
  - Double click: toggle audio
  - Long press: toggle all outputs
- Adafruit DHT library

---

## Power architecture

| Rail | Source | Loads |
|---|---|---|
| 12V | PSU via barrel jack + rocker switch | TDA2050s, fan MOSFETs, buck inputs |
| 24V | Mister PSU via barrel jack 2 | Mister MOSFET drain only |
| 5V logic | Buck 1 (12V→5V) → ESP32 VIN | ESP32, OLED, DHT22, encoder |
| 5V audio | Buck 2 (12V→5V) → PCM5102 VCC | PCM5102 dedicated clean rail |
| 3.3V | ESP32 onboard AMS1117 | ESP32 core, OLED, encoder, PCM5102 straps |

Star ground: all GNDs to single common point.
Buck input and output GND on separate wires — not daisy-chained.
USB shield must tie to common ground.

---

## Libraries

```
ESP8266Audio          by Earle F. Philhower III
Adafruit SSD1306      by Adafruit
Adafruit GFX Library  by Adafruit
Adafruit DHT          by Adafruit (pending)
ESPAsyncWebServer     by ESP Async WebServer (wifi-ota branch)
AsyncTCP              by dvarrel (wifi-ota branch)
```

---

## Coding conventions

- All shared state between Core 0 and Core 1 protected by stateMutex
- ISRs marked IRAM_ATTR
- Display calls gated by displayOK boolean
- Fan pins driven LOW explicitly before ledcAttach in setup()
- SD_MMC initialized first in setup(), before all other peripherals
- audioTask always starts with vTaskDelay(2000ms)
- Serial.printf used for all debug output
- #define constants for all pin numbers and timing values
- No blocking delays in loop() — millis() comparison pattern throughout

---

## File structure

```
sleep_controller_sd/
├── sleep_controller_sd.ino    # main sketch
├── README.md                  # project overview
├── CLAUDE.md                  # this file
├── .gitignore                 # Arduino build artifacts etc
├── docs/
│   ├── pin_assignments.md     # pin reference
│   ├── sleep_controller_reference.html  # block diagram + tables
│   └── sleep_controller_checklist.md   # open items
└── audio/
    └── README.md              # audio file format requirements
```

---

## Common failure modes seen during development

| Symptom | Cause | Fix |
|---|---|---|
| SD_MMC mount failed 0x107 | IO12 connected, or IO13 on LEDC, or card unseated | Remove IO12 wire, retire IO13, reseat card |
| Loud noise from speakers | PCM5102 FMT pin floating | Tie FMT to GND |
| No audio output | PCM5102 XSMT pin floating | Tie XSMT to 3.3V |
| Audio plays then stops | WAV loop seek fails | Delete/recreate AudioFileSourceFS on EOF |
| Fan runs uncontrolled | Fan – connected to GND not MOSFET drain | Wire fan – to drain, not ground bus |
| Fan doesn't respond to control | Wrong gate resistor value (100kΩ not 100Ω) | Verify brown-black-brown |
| ESP32 won't boot from PSU | IO13 loading strapping pin during fast ramp | Move to IO27, add RC soft-start |
| SD mounts then fails after adding component | Wire shift during breadboard work | Methodically check IO2/14/15 area |
| Audio stutters when paused | DMA buffer underflow on pause | Feed silence frames instead of stopping |
