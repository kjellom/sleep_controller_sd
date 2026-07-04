# Sleep controller — open items and recommendations
## Rev 7 — updated after breadboard fan verification

---

## Hardware — confirmed working

- [x] SD card audio — dual stream WAV looping
- [x] PCM5102 I2S DAC — EXTERNAL_I2S mode on core 3.3.10
- [x] Both TDA2050 amplifiers
- [x] OLED SSD1306 128x32
- [x] Rotary encoder — fan speed + button logic
- [x] Fan 1 MOSFET — IO32, IRLZ44N
- [x] Fan 2 MOSFET — IO27 (moved from IO13)
- [x] Buck converter boot — reliable after IO13 removal
- [x] PCM5102 strap pins — XSMT/FMT/FLT/DEMP all tied

---

## Hardware — pending

- [ ] **Mister MOSFET** — IO33, same circuit as fans. 4.7kΩ board pull-up on IO33 — pull-down resistor still required. Confirm mister type (ultrasonic vs solenoid) — flyback diode only needed for solenoid.
- [ ] **DHT22 sensor** — shielded cable on order. Wire with 4.7kΩ pull-up at sensor end, 100Ω series + 1kΩ/2kΩ divider at ESP32 end. Test reads over full 2m lead before integrating control loop.
- [ ] **Confirm mister PWM behavior** — determine minimum on-time for meaningful mist. Affects bang-bang vs PWM duty cycle control approach.
- [ ] **Confirm 24V PSU inrush** — bundled PSU rated exactly 1A. Test for inrush on startup — upgrade to 24V 2A if needed.
- [ ] **RC soft-start on 5V rail** — 10Ω series + 1000µF to GND on buck converter output. Recommended for permanent build to ensure clean cold-boot from PSU without USB.

---

## Software — pending

- [ ] **Update sketch: FAN2_PWM IO13 → IO27, MISTER_PWM IO33** — reflect current pin assignments. *(IO27 change already made in sketch — verify IO33 mister define is added)*
- [ ] **Mister MOSFET integration** — add IO33 LEDC channel to sketch. Same ledcAttach pattern as fans. Drive LOW explicitly in setup() before ledcAttach.
- [ ] **DHT22 integration** — add Adafruit DHT library. Read every 2 seconds minimum. Test over full lead before adding control logic.
- [ ] **Closed-loop control — bang-bang with hysteresis:**
  - Temp > setpoint + 0.5°C → fans on at set speed
  - Temp < setpoint − 0.5°C → fans off
  - Humidity < setpoint − 3% RH → mister on
  - Humidity > setpoint + 3% RH → mister off
- [ ] **UI state machine:**
  - Normal mode → OLED shows setpoints, audio state
  - Single press → cycle edit mode (temp → humidity → normal)
  - Double click → toggle audio play/pause
  - Long press → toggle all outputs on/off
  - Encoder turn in edit mode → adjust setpoint
- [ ] **NVS persistence** — save/restore temp setpoint, humidity setpoint, audio play/pause across power cycles. Use ESP32 Preferences library.
- [ ] **OLED layout update** — current layout shows fan speed bar. Final layout: temp setpoint, humidity setpoint, audio state. Current/actual temp and humidity via Serial Monitor only.
- [ ] **MIN_DUTY_PCT** — currently 40%, fans run reliably at 3%. Lower to 3-5% for full useful range.
- [ ] **Audio loop cleanup** — clean up loop points in WAV files to eliminate click at restart. Use Audacity to match amplitude and phase at loop boundaries.
- [ ] **Proportional fan speed ramping** — deferred post-breadboard. Fan speed currently fixed when above setpoint.
- [ ] **Soft-start fan ramp** — optional: gradually increase from 0% to set speed over ~2 seconds on fan toggle-on for smoother feel.

---

## Wiring — permanent build

- [ ] **Move all connections to perfboard** — solder all components, eliminate breadboard contact failures
- [ ] **Star ground layout** — single common ground point. Buck converter input/output GND on separate wires. USB shield tied to common ground.
- [ ] **Decoupling capacitors** — 100µF + 100nF at each buck converter output. 100nF at PCM5102 VCC and ESP32 VIN.
- [ ] **Coupling capacitors** — 10µF per TDA2050 channel. + leg toward PCM5102, − leg toward TDA2050 input.
- [ ] **MOSFET gate resistors** — 100Ω series on IO32 (Fan 1), IO27 (Fan 2), IO33 (Mister). Verify brown-black-brown (100Ω) not brown-black-yellow (100kΩ).
- [ ] **Pull-down resistors** — 10kΩ from each gate to GND. IO33 has board pull-up — pull-down still required.
- [ ] **Flyback diodes** — 1N4007 across each fan (cathode to 12V). Mister: only if solenoid type.
- [ ] **DHT22 shielded cable** — shield grounded at ESP32 end only.
- [ ] **RC soft-start** — 10Ω + 1000µF on 5V buck output before ESP32 VIN.

---

## BOM — items to order / verify on hand

- [ ] Shielded 3-conductor cable 22AWG ~3m (audio mic cable)
- [ ] JST-PH 2.0mm connectors + crimp pins (encoder, OLED, power switch)
- [ ] JST-XH 2.54mm connectors + crimp pins (fan, TDA2050 signal harnesses)
- [ ] Engineer PA-09 crimping tool
- [ ] Hammond 1591XXFLBK enclosure 120×65×40mm ABS
- [ ] DC barrel jack panel mount 5.5mm/2.1mm × 2
- [ ] Rocker switch SPST 3A panel mount (Digi-Key EG2396-ND)
- [ ] 12V 5A switching supply (upgrade from 3A)
- [ ] M3 brass standoffs for perfboard mounting
- [ ] Rubber grommets for cable entry
- [ ] Silicone sealant (condensation protection)
- [ ] 10Ω resistor × 1 (RC soft-start)
- [ ] 1000µF electrolytic capacitor 10V+ × 1 (RC soft-start)
- [ ] Confirm: IRLZ44N × 3 (Fan 1, Fan 2, Mister)
- [ ] Confirm: 1N4007 × 3
- [ ] Confirm: 10µF electrolytic × 2 (audio coupling)
- [ ] Confirm: 100µF electrolytic × 2 (buck output filtering)
- [ ] Confirm: 100nF ceramic × 4 (decoupling)
- [ ] Confirm: resistors on hand — 100Ω ×3, 1kΩ ×1, 2kΩ ×1, 4.7kΩ ×1, 10kΩ ×3, 10Ω ×1

---

## Lessons learned — worth remembering for permanent build

- **IO13** — strapping pin. Never use for driven outputs. Caused repeated SD_MMC boot failures.
- **IO12** — leave completely unconnected. Boot strapping pin.
- **PCM5102 strap pins** — all four must be tied to definite logic levels. Floating FMT or XSMT causes noise or silence.
- **SD card slot** — spring-loaded slot on SunFounder board is fussy. Perfboard with stable mounting will help.
- **Resistor color codes** — 100Ω (brown-black-brown) vs 100kΩ (brown-black-yellow). Verify with multimeter before installing.
- **Star ground** — buck converter input and output GND on separate wires to common point. USB shield must tie to common ground.
- **WAV header** — standard is 44 bytes but Audacity may add metadata. Use `findWAVDataOffset()` parser rather than fixed offset.
- **5V power source** — PCM5102 on dedicated buck converter 5V rail rather than ESP32 3.3V regulator reduces noise and regulator load.
- **IO22** — exclusively I2C SCL. Do not share with I2S DIN or any other peripheral.

---

## Bode plot — deferred, planned after permanent build

- [ ] Characterize TDA2050 frequency response using Siglent SDG1032X + SDS1104X-E
- [ ] Signal chain: PCM5102 OUTL → coupling cap → TDA2050 input → speaker
- [ ] Scope CH1 at TDA2050 input (AC coupled), CH2 at speaker terminals (AC coupled)
- [ ] SDG in HiZ mode, probes in 10x mode, all grounds common
- [ ] Sweep 20Hz–20kHz, record Vout/Vin ratio in dB
- [ ] Consider RC low-pass filter (1kΩ + 10nF) if noise floor unacceptable
