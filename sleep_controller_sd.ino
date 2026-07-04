// ============================================================
//  sleep_controller_sd  —  personal air chiller controller
//  Rev 6 — dual stream audio, WAV header parser, fans off at boot
//
//  HARDWARE:
//    ESP32 dev board on SunFounder Camera Extension board
//    PCM5102 I2S DAC → dual mono TDA2050 amplifiers
//    Two 120mm 12V fans via IRLZ44N MOSFET low-side switches
//    SSD1306 128x32 OLED (I2C)
//    Single rotary encoder (fan speed + button)
//    SD card audio (dual WAV loop, ambient/masking sounds)
//    12V AC mains supply via barrel jack + rocker switch
//    Audio volume controlled by TDA2050 onboard potentiometers
//
//  ENCODER BEHAVIOUR:
//    Turn knob         → fan speed 0–100%
//    Short press       → toggle both fans on/off
//    Long press >600ms → toggle audio play/pause
//
//  OLED:
//    Full brightness on any knob turn or button press.
//    Dims to DIM_BRIGHTNESS after DIM_TIMEOUT_MS of inactivity.
//
//  AUDIO:
//    /audio_a.wav → PCM5102 left  → TDA2050 A → speaker A
//    /audio_b.wav → PCM5102 right → TDA2050 B → speaker B
//    Both files loop independently.
//    Format: 22050Hz, 16-bit, mono WAV (Audacity default export)
//    Volume set physically via TDA2050 board potentiometers.
//
//  FREERTOS:
//    Core 0 → audioTask  (I2S DMA, time-critical)
//    Core 1 → loop()     (encoder, display, fan PWM)
//
//  ── PIN ASSIGNMENTS ──────────────────────────────────────────
//    IO2  → SD_MMC D0
//    IO4  → PCM5102 DIN (I2S data)
//    IO5  → Encoder SW
//    IO14 → SD_MMC CLK
//    IO15 → SD_MMC CMD
//    IO18 → Encoder CLK
//    IO19 → Encoder DT (ISR on CHANGE)
//    IO21 → OLED SDA (I2C)
//    IO22 → OLED SCL (I2C, exclusive)
//    IO23 → DHT22 data (future — 1-wire)
//    IO25 → PCM5102 LCK (I2S left-right clock)
//    IO26 → PCM5102 BCK (I2S bit clock)
//    IO27 → Fan 2 MOSFET gate (moved from IO13)
//    IO32 → Fan 1 MOSFET gate
//    IO33 → Mister MOSFET gate (future, 4.7kΩ board pull-up present)
//    IO12 → UNCONNECTED (boot strapping — do not wire)
//    IO13 → RETIRED (strapping pin — caused SD_MMC boot failures)
//
//  PCM5102 STRAP PINS (must be wired — not optional):
//    XSMT → 3.3V  (unmute — floating = noise)
//    FMT  → GND   (I2S format — floating = noise)
//    FLT  → GND   (normal filter)
//    DEMP → GND   (de-emphasis off)
//
//  LIBRARIES REQUIRED (Arduino Library Manager):
//    • ESP8266Audio  by Earle F. Philhower III
//    • Adafruit SSD1306
//    • Adafruit GFX Library
// ============================================================

#include <Arduino.h>
#include <Wire.h>
#include <SD_MMC.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "AudioOutputI2S.h"

// ── Pin assignments ──────────────────────────────────────────
#define ENC_CLK     18
#define ENC_DT      19
#define ENC_SW      5
#define I2S_BCK     26
#define I2S_LCK     25
#define I2S_DIN     4
#define FAN1_PWM    32
#define FAN2_PWM    27    // moved from IO13 — strapping pin caused SD_MMC boot failures
#define MISTER_PWM  33    // 4.7kΩ board pull-up present — pull-down resistor required

// ── OLED ─────────────────────────────────────────────────────
#define SCREEN_WIDTH    128
#define SCREEN_HEIGHT   32
#define OLED_RESET      -1
#define OLED_ADDR       0x3C
#define FULL_BRIGHTNESS 255
#define DIM_BRIGHTNESS  5
#define DIM_TIMEOUT_MS  10000
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
bool displayOK = false;

// ── Fan PWM ──────────────────────────────────────────────────
#define PWM_FREQ      25000
#define PWM_RES_BITS  8
#define PWM_MAX       255
#define MIN_DUTY_PCT  40

// ── Audio ─────────────────────────────────────────────────────
#define AUDIO_FILE_A  "/audio_a.wav"
#define AUDIO_FILE_B  "/audio_b.wav"
#define AUDIO_GAIN    0.8f

// ── Shared state (mutex-protected) ───────────────────────────
SemaphoreHandle_t stateMutex;
bool audioPlaying = true;

// ── Fan + encoder state ───────────────────────────────────────
volatile int  encoderPos   = 75;
volatile bool encoderMoved = false;
bool          fan1On       = false;   // fans off at startup
bool          fan2On       = false;

// ── OLED dim state ────────────────────────────────────────────
unsigned long lastActivityTime = 0;
bool          isDimmed         = false;

// ── Button timing ────────────────────────────────────────────
#define DEBOUNCE_MS    50
#define LONG_PRESS_MS  600
unsigned long btnPressTime    = 0;
unsigned long btnDebounceTime = 0;
bool          btnHeld         = false;

// ── Display refresh ───────────────────────────────────────────
unsigned long lastDisplayTime = 0;
#define DISPLAY_MS 100

// ── Encoder ISR ──────────────────────────────────────────────
void IRAM_ATTR encoderISR() {
  static int lastDT = HIGH;
  int dt = digitalRead(ENC_DT);
  if (dt != lastDT) {
    lastDT = dt;
    if (digitalRead(ENC_CLK) != dt) encoderPos++;
    else                              encoderPos--;
    if (encoderPos < 0)   encoderPos = 0;
    if (encoderPos > 100) encoderPos = 100;
    encoderMoved = true;
  }
}

// ── Activity tracking ─────────────────────────────────────────
void registerActivity() {
  lastActivityTime = millis();
  if (isDimmed && displayOK) {
    isDimmed = false;
    display.ssd1306_command(SSD1306_SETCONTRAST);
    display.ssd1306_command(FULL_BRIGHTNESS);
  }
}

// ── Fan PWM ──────────────────────────────────────────────────
void applyFan(int pin, bool on) {
  int duty = 0;
  if (on && encoderPos > 0) {
    int pct = map(encoderPos, 1, 100, MIN_DUTY_PCT, 100);
    duty    = map(pct, 0, 100, 0, PWM_MAX);
  }
  ledcWrite(pin, duty);
}

void applyAllFans() {
  applyFan(FAN1_PWM, fan1On);
  applyFan(FAN2_PWM, fan2On);
}

// ── Display ──────────────────────────────────────────────────
void updateDisplay() {
  if (!displayOK) return;
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // Row 0: fan states + audio indicator
  display.setCursor(0, 0);
  display.print("F1:");
  display.print(fan1On ? "ON " : "OFF");
  display.print(" F2:");
  display.print(fan2On ? "ON " : "OFF");
  display.setCursor(108, 0);
  display.print(audioPlaying ? " >" : "||");

  // Row 1: fan speed + progress bar
  display.setCursor(0, 11);
  display.print("Fan:");
  char buf[5];
  snprintf(buf, sizeof(buf), "%3d%%", encoderPos);
  display.print(buf);

  const int BAR_X = 42, BAR_Y = 11, BAR_W = 84, BAR_H = 8;
  display.drawRect(BAR_X, BAR_Y, BAR_W, BAR_H, SSD1306_WHITE);
  int fillW = map(encoderPos, 0, 100, 0, BAR_W - 4);
  if (fillW > 0)
    display.fillRect(BAR_X + 2, BAR_Y + 2, fillW, BAR_H - 4, SSD1306_WHITE);

  for (int pct : {25, 50, 75}) {
    int x = BAR_X + map(pct, 0, 100, 0, BAR_W);
    display.drawPixel(x, BAR_Y + BAR_H,     SSD1306_WHITE);
    display.drawPixel(x, BAR_Y + BAR_H + 1, SSD1306_WHITE);
  }

  // Row 2: hint text
  display.setCursor(0, 24);
  display.print("short=fans  long=audio");

  display.display();
}

// ── WAV header parser ─────────────────────────────────────────
// Finds the actual start of PCM data regardless of header length.
// Standard WAV headers are 44 bytes but Audacity and other tools
// sometimes add metadata chunks that extend the header.
uint32_t findWAVDataOffset(File &f) {
  f.seek(0);
  char chunk[4];

  // Verify RIFF header
  f.read((uint8_t*)chunk, 4);
  if (memcmp(chunk, "RIFF", 4) != 0) {
    Serial.println("WAV: not a RIFF file — using offset 44");
    return 44;
  }

  // Skip file size (4 bytes) and WAVE marker (4 bytes)
  f.seek(12);

  // Walk chunks until we find "data"
  uint32_t pos = 12;
  while (pos < 4096) {   // sanity limit
    f.seek(pos);
    if (f.read((uint8_t*)chunk, 4) < 4) break;
    uint32_t size = 0;
    f.read((uint8_t*)&size, 4);
    if (memcmp(chunk, "data", 4) == 0) {
      return pos + 8;    // data starts after chunk ID (4) + size (4)
    }
    pos += 8 + size;
    // Align to even byte boundary (WAV spec requirement)
    if (size & 1) pos++;
  }

  Serial.println("WAV: data chunk not found — using offset 44");
  return 44;
}

// ── Audio task (Core 0) ───────────────────────────────────────
AudioOutputI2S *i2sOut = nullptr;

void audioTask(void *pvParameters) {
  // Wait for SD_MMC and setup to fully complete
  vTaskDelay(pdMS_TO_TICKS(2000));

  i2sOut = new AudioOutputI2S(0, AudioOutputI2S::EXTERNAL_I2S, 32);
  i2sOut->SetPinout(I2S_BCK, I2S_LCK, I2S_DIN);
  i2sOut->SetGain(AUDIO_GAIN);
  i2sOut->SetRate(22050);
  i2sOut->SetChannels(2);
  i2sOut->begin();

  // Open both files
  File fileA = SD_MMC.open(AUDIO_FILE_A);
  File fileB = SD_MMC.open(AUDIO_FILE_B);

  if (!fileA) {
    Serial.println("ERROR: audio_a.wav not found");
    vTaskDelay(portMAX_DELAY);
  }
  if (!fileB) {
    Serial.println("ERROR: audio_b.wav not found");
    vTaskDelay(portMAX_DELAY);
  }

  // Find actual PCM data start in each file
  uint32_t offsetA = findWAVDataOffset(fileA);
  uint32_t offsetB = findWAVDataOffset(fileB);
  Serial.printf("Data offset A: %d  B: %d\n", offsetA, offsetB);

  fileA.seek(offsetA);
  fileB.seek(offsetB);

  Serial.println("Dual stream audio running");

  for (;;) {
    bool playing;
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    playing = audioPlaying;
    xSemaphoreGive(stateMutex);

    if (playing) {
      int16_t left = 0, right = 0;

      // Read one 16-bit sample from file A (left channel)
      if (fileA.available() >= 2) {
        fileA.read((uint8_t*)&left, 2);
      } else {
        fileA.seek(offsetA);
        fileA.read((uint8_t*)&left, 2);
        Serial.println("Loop A");
      }

      // Read one 16-bit sample from file B (right channel)
      if (fileB.available() >= 2) {
        fileB.read((uint8_t*)&right, 2);
      } else {
        fileB.seek(offsetB);
        fileB.read((uint8_t*)&right, 2);
        Serial.println("Loop B");
      }

      // Apply gain
      left  = (int16_t)((float)left  * AUDIO_GAIN);
      right = (int16_t)((float)right * AUDIO_GAIN);

      // Push stereo frame to I2S DMA
      // Left  → PCM5102 OUTL → TDA2050 A → speaker A
      // Right → PCM5102 OUTR → TDA2050 B → speaker B
      int16_t frame[2] = { left, right };
      i2sOut->ConsumeSample(frame);

    } else {
      // Paused — yield to other tasks
      vTaskDelay(pdMS_TO_TICKS(50));
    }

    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

// ── Setup ────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  // SD_MMC first — before any other peripheral
  // IO12 must remain unconnected (boot strapping pin)
  if (!SD_MMC.begin("/sdcard", true)) {
    Serial.println("SD_MMC mount failed — check card and IO12");
  } else {
    Serial.println("SD_MMC ready");
  }

  // Fan pins — drive LOW before LEDC attaches
  // IO13 is a strapping pin — explicit LOW prevents boot glitch
  pinMode(FAN1_PWM, OUTPUT); digitalWrite(FAN1_PWM, LOW);
  pinMode(FAN2_PWM, OUTPUT); digitalWrite(FAN2_PWM, LOW);
  ledcAttach(FAN1_PWM, PWM_FREQ, PWM_RES_BITS);
  ledcAttach(FAN2_PWM, PWM_FREQ, PWM_RES_BITS);

  // Encoder
  pinMode(ENC_CLK, INPUT_PULLUP);
  pinMode(ENC_DT,  INPUT_PULLUP);
  pinMode(ENC_SW,  INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(ENC_DT), encoderISR, CHANGE);

  // OLED — non-fatal if not connected
  Wire.begin();
  displayOK = display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  if (!displayOK) {
    Serial.println("SSD1306 not found — display disabled");
  } else {
    display.ssd1306_command(SSD1306_SETCONTRAST);
    display.ssd1306_command(FULL_BRIGHTNESS);
    display.clearDisplay();
    display.display();
  }

  stateMutex       = xSemaphoreCreateMutex();
  lastActivityTime = millis();

  applyAllFans();
  updateDisplay();

  // Launch audio task on Core 0, priority 2
  xTaskCreatePinnedToCore(
    audioTask,
    "audioTask",
    8192,
    nullptr,
    2,
    nullptr,
    0
  );

  Serial.println("Ready.");
}

// ── Loop (Core 1) ────────────────────────────────────────────
void loop() {

  // ── Button: short = fans, long = audio ───────────────────────
  int swState = digitalRead(ENC_SW);

  if (swState == LOW && !btnHeld) {
    unsigned long now = millis();
    if (now - btnDebounceTime > DEBOUNCE_MS) {
      btnHeld        = true;
      btnPressTime   = now;
      btnDebounceTime = now;
      registerActivity();
    }
  }

  if (swState == HIGH && btnHeld) {
    unsigned long duration = millis() - btnPressTime;
    btnHeld = false;

    if (duration >= LONG_PRESS_MS) {
      // Long press → toggle audio play/pause
      xSemaphoreTake(stateMutex, portMAX_DELAY);
      audioPlaying = !audioPlaying;
      xSemaphoreGive(stateMutex);
      Serial.printf("Audio %s\n", audioPlaying ? "playing" : "paused");
    } else {
      // Short press → toggle both fans on/off
      bool newState = !(fan1On && fan2On);
      fan1On = fan2On = newState;
      applyAllFans();
      Serial.printf("Fans %s at %d%%\n",
                    newState ? "ON" : "OFF", encoderPos);
    }
    updateDisplay();
  }

  // ── Encoder: fan speed ────────────────────────────────────────
  if (encoderMoved) {
    encoderMoved = false;
    applyAllFans();
    registerActivity();
    Serial.printf("Fan speed: %d%%\n", encoderPos);
  }

  // ── OLED dim check ────────────────────────────────────────────
  if (displayOK && !isDimmed &&
      millis() - lastActivityTime >= DIM_TIMEOUT_MS) {
    isDimmed = true;
    display.ssd1306_command(SSD1306_SETCONTRAST);
    display.ssd1306_command(DIM_BRIGHTNESS);
  }

  // ── Display refresh ───────────────────────────────────────────
  if (millis() - lastDisplayTime >= DISPLAY_MS) {
    lastDisplayTime = millis();
    updateDisplay();
  }
}
