/*
 * SHT31 + PMS7003 + MPXV7002DP Ambient Monitor
 * Target: Arduino Uno R3 (ATmega328P)
 * ─────────────────────────────────────────────────────────────────
 * LIBRARIES (Library Manager — only one needed):
 *   - Adafruit SHT31 Library  (+Adafruit BusIO, auto-installed)
 *
 * WIRING:
 *   SHT31      VCC → 3.3V | GND → GND | SDA → A4 | SCL → A5 | ADR → GND
 *   PMS7003    VCC → 5V   | GND → GND | TX → Pin 2 (via level shifter)
 *   MPXV7002DP VCC → 5V   | GND → GND | VOUT → A0
 *
 * MPXV7002DP INSTALLATION:
 *   Port 1 (high/+): tube runs INSIDE the clean box
 *   Port 2 (low/−):  open to ambient room air outside the box
 *   This gives a true differential reading: inside − outside
 *
 * TRANSFER FUNCTION (per datasheet + Gemini ISO 6 guidance):
 *   P(Pa) = (Vout − Voffset) × (1000 / (Vcc × 0.2))  →  ×200 at 5V
 *   [NOT × 800 — see pushMAF() for derivation]
 *   Range: ±2000 Pa  |  ISO 6 target: +25 Pa
 *
 * PRESSURE THRESHOLDS (ISO 6 cleanroom, ISPE guidelines):
 *   ≥ 25 Pa  →  ISO 6 COMPLIANT
 *   15–24 Pa →  WARNING  — low positive pressure
 *   < 15 Pa  →  CRITICAL — ISO 6 FAILED
 *
 * SERIAL OUTPUT: 115200 baud
 *   - JSON line every 1000 ms  → dashboard
 *   - Pressure status line every 500 ms  → Serial Monitor
 * SERIAL INPUT:  T<epoch>  — time sync from dashboard
 */

#include <Wire.h>
#include <Adafruit_SHT31.h>
#include <SoftwareSerial.h>

// ── Pins ──────────────────────────────────────────────────────────
#define PMS_RX_PIN   2    // PMS7003 TX → Arduino Pin 2 (via level shifter)
#define PMS_TX_PIN   3
#define PRESSURE_PIN A0   // MPXV7002DP VOUT
#define LED_PIN      13   // built-in LED heartbeat

// ── MPXV7002DP config ─────────────────────────────────────────────
#define PRESS_VCC          5.0f   // Supply voltage
#define PRESS_TARGET_PA   25.0f   // ISO 6 target: +25 Pa
#define PRESS_WARN_PA     15.0f   // Warning threshold
#define PRESS_MAF_SIZE    20      // Moving average filter window (samples)
#define PRESS_CAL_SAMPLES 50      // Calibration samples taken in setup()

// Moving average filter ring buffer
static float  mafBuf[PRESS_MAF_SIZE] = {0};
static uint8_t mafIdx  = 0;
static bool    mafFull = false;

// Zero-pressure voltage offset — set by calibrate() in setup()
static float pressOffsetV = 2.5f;  // default 2.5V; overwritten by calibration

// ── MPXV7002DP: calibration ───────────────────────────────────────
// Call once in setup() before any airflow starts.
// Reads 50 samples with the sensor at rest (both ports open to room air,
// or box sealed and HVAC off) to establish the true 0 Pa voltage offset.
void calibratePressure() {
  Serial.println(F("PRESSURE: calibrating zero offset (keep HVAC off)..."));
  long sum = 0;
  for (uint8_t i = 0; i < PRESS_CAL_SAMPLES; i++) {
    sum += analogRead(PRESSURE_PIN);
    delay(10);   // blocking OK — only runs once in setup()
  }
  float adcAvg      = sum / (float)PRESS_CAL_SAMPLES;
  pressOffsetV      = (adcAvg / 1023.0f) * PRESS_VCC;

  char buf[60];
  dtostrf(pressOffsetV, 5, 4, buf);
  Serial.print(F("PRESSURE: zero offset = "));
  Serial.print(buf);
  Serial.println(F(" V  (calibration complete)"));
}

// ── MPXV7002DP: single raw reading (V) ───────────────────────────
float readPressureVolts() {
  return (analogRead(PRESSURE_PIN) / 1023.0f) * PRESS_VCC;
}

// ── MPXV7002DP: push new sample into moving average filter ────────
// Returns filtered pressure in Pascals.
//
// DATASHEET TRANSFER FUNCTION:
//   Vout / Vcc = 0.5 + 0.2 × P(kPa)
//   → P(kPa) = (Vout/Vcc − 0.5) / 0.2
//   → P(Pa)  = (Vout/Vcc − 0.5) / 0.2 × 1000
//
// Substituting Vcc = 5.0V and factoring constants:
//   P(Pa) = (Vout − Voffset) × (1000 / (Vcc × 0.2))
//         = (Vout − Voffset) × (1000 / 1.0)
//         = (Vout − Voffset) × 200.0
//
// NOTE: The '× 800' shortcut that appears in some tutorials is WRONG —
// it conflates the voltage ratio with the raw voltage. At 5V Vcc the
// correct multiplier is 200, not 800.
float pushMAF(float vout) {
  mafBuf[mafIdx] = vout;
  mafIdx = (mafIdx + 1) % PRESS_MAF_SIZE;
  if (mafIdx == 0) mafFull = true;

  uint8_t count = mafFull ? PRESS_MAF_SIZE : mafIdx;
  float sum = 0;
  for (uint8_t i = 0; i < count; i++) sum += mafBuf[i];
  float vAvg = sum / (float)count;

  // Correct: (Vout − Voffset) × 1000 / (Vcc × 0.2)
  return (vAvg - pressOffsetV) * (1000.0f / (PRESS_VCC * 0.2f));
}

// ── MPXV7002DP: status string ─────────────────────────────────────
const char* pressureStatus(float pa) {
  if (pa >= PRESS_TARGET_PA) return "ISO 6 COMPLIANT";
  if (pa >= PRESS_WARN_PA)   return "WARNING - LOW PRESSURE";
  return                            "CRITICAL - ISO 6 FAILED";
}

// ── PMS7003 ───────────────────────────────────────────────────────
SoftwareSerial pmsSerial(PMS_RX_PIN, PMS_TX_PIN);

#define FRAME_LEN 32
static uint8_t  pmsBuf[FRAME_LEN];
static uint8_t  pmsBufIdx     = 0;
static bool     pmsOk         = false;
static uint32_t pmsFrameCount = 0;
static uint32_t pmsByteCount  = 0;

static uint16_t pm1=0, pm25=0, pm10=0;
static uint16_t n03=0, n05=0,  n10=0;
static uint16_t n25=0, n50=0,  n100=0;

inline uint16_t w16(uint8_t* b, uint8_t i) {
  return ((uint16_t)b[i] << 8) | b[i + 1];
}

// Non-blocking — drains SoftwareSerial buffer every loop()
void updatePMS() {
  while (pmsSerial.available()) {
    uint8_t b = (uint8_t)pmsSerial.read();
    pmsByteCount++;

    if (pmsBufIdx == 0 && b != 0x42) continue;
    if (pmsBufIdx == 1 && b != 0x4D) { pmsBufIdx = 0; continue; }

    pmsBuf[pmsBufIdx++] = b;

    if (pmsBufIdx == FRAME_LEN) {
      pmsBufIdx = 0;
      uint16_t sum = 0;
      for (uint8_t i = 0; i < 30; i++) sum += pmsBuf[i];
      if (sum == w16(pmsBuf, 30)) {
        pm1  = w16(pmsBuf,  4);
        pm25 = w16(pmsBuf,  6);
        pm10 = w16(pmsBuf,  8);
        n03  = w16(pmsBuf, 16);
        n05  = w16(pmsBuf, 18);
        n10  = w16(pmsBuf, 20);
        n25  = w16(pmsBuf, 22);
        n50  = w16(pmsBuf, 24);
        n100 = w16(pmsBuf, 26);
        pmsOk = true;
        pmsFrameCount++;
        digitalWrite(LED_PIN, pmsFrameCount % 2);
      }
    }
  }
}

// ── SHT31 ─────────────────────────────────────────────────────────
Adafruit_SHT31 sht31;
static bool  shtOk = false;
static float tempC = NAN;
static float hum   = NAN;

void updateSHT() {
  float t = sht31.readTemperature();
  float h = sht31.readHumidity();
  if (!isnan(t) && !isnan(h)) {
    tempC = t; hum = h; shtOk = true;
  } else {
    shtOk = false;
  }
}

// ── Soft clock ────────────────────────────────────────────────────
static unsigned long epochOffset = 0;
static unsigned long epochRefMs  = 0;
static bool          timeSynced  = false;

unsigned long nowEpoch() {
  return epochOffset + (millis() - epochRefMs) / 1000UL;
}

// ── Serial command parser ─────────────────────────────────────────
static char    cmdBuf[24];
static uint8_t cmdLen = 0;

void checkSerial() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (cmdLen > 0) {
        cmdBuf[cmdLen] = '\0';
        if (cmdBuf[0] == 'T' && cmdBuf[1] >= '0') {
          unsigned long e = atol(cmdBuf + 1);
          if (e > 1000000000UL) {
            epochOffset = e;
            epochRefMs  = millis();
            timeSynced  = true;
          }
        }
        cmdLen = 0;
      }
    } else if (cmdLen < (uint8_t)(sizeof(cmdBuf) - 1)) {
      cmdBuf[cmdLen++] = c;
    }
  }
}

// ── Pressure Serial Monitor print (500 ms) ────────────────────────
// Separate from emitJSON — human-readable for Serial Monitor
// Uses F() macro to keep strings in flash, saving ~80 bytes of SRAM
void printPressureStatus(float pa) {
  char paStr[10];
  dtostrf(pa, 6, 2, paStr);

  Serial.print(F("PRESSURE: "));
  Serial.print(paStr);
  Serial.print(F(" Pa  |  STATUS: "));
  Serial.println(pressureStatus(pa));
}

// ── JSON emit — single println to minimise interrupt blocking ─────
void emitJSON(float pressurePa) {
  unsigned long t = nowEpoch() % 86400UL;
  char timeBuf[10];
  snprintf(timeBuf, sizeof(timeBuf), "%02lu:%02lu:%02lu",
           t / 3600, (t % 3600) / 60, t % 60);

  char tStr[8], hStr[8], pStr[10];
  if (shtOk) {
    dtostrf(tempC, 4, 1, tStr);
    dtostrf(hum,   4, 1, hStr);
  } else {
    strcpy(tStr, "null");
    strcpy(hStr, "null");
  }
  dtostrf(pressurePa, 5, 1, pStr);

  // ok_press = positive pressure maintained (> 0 Pa above ambient).
  // Full ISO 6 compliance (>= 25 Pa) is evaluated in the HTML dashboard.
  bool pressOk = (pressurePa > 0.0f);

  char json[300];
  snprintf(json, sizeof(json),
    "{"
    "\"ok_sht\":%s,\"ok_pms\":%s,\"ok_press\":%s,\"synced\":%s,"
    "\"time\":\"%s\","
    "\"temp\":%s,\"hum\":%s,"
    "\"pressure_pa\":%s,"
    "\"pm1\":%u,\"pm25\":%u,\"pm10\":%u,"
    "\"n03\":%u,\"n05\":%u,\"n10\":%u,"
    "\"n25\":%u,\"n50\":%u,\"n100\":%u,"
    "\"frames\":%lu,\"bytes\":%lu"
    "}",
    shtOk    ? "true" : "false",
    pmsOk    ? "true" : "false",
    pressOk  ? "true" : "false",
    timeSynced ? "true" : "false",
    timeBuf,
    tStr, hStr,
    pStr,
    pm1, pm25, pm10,
    n03, n05, n10,
    n25, n50, n100,
    pmsFrameCount, pmsByteCount
  );

  Serial.println(json);
}

// ── Setup ─────────────────────────────────────────────────────────
void setup() {
  pinMode(LED_PIN, OUTPUT);
  pinMode(PRESSURE_PIN, INPUT);
  Serial.begin(115200);
  pmsSerial.begin(9600);
  Wire.begin();
  shtOk      = sht31.begin(0x44);
  epochRefMs = millis();

  // Calibrate pressure zero offset BEFORE starting HVAC/fan
  calibratePressure();
}

// ── Loop ──────────────────────────────────────────────────────────
static unsigned long lastJSON   = 0;   // JSON to dashboard:     1000 ms
static unsigned long lastStatus = 0;   // Status to Serial Mon:   500 ms

void loop() {
  checkSerial();
  updatePMS();   // non-blocking — must run every loop()

  // Always push a new ADC sample into the moving average filter
  float filteredPa = pushMAF(readPressureVolts());

  // ── 500 ms: print human-readable pressure status ──────────────
  if (millis() - lastStatus >= 500UL) {
    lastStatus = millis();
    printPressureStatus(filteredPa);
  }

  // ── 1000 ms: update SHT31 and emit JSON to dashboard ──────────
  if (millis() - lastJSON >= 1000UL) {
    lastJSON = millis();
    updateSHT();
    emitJSON(filteredPa);
  }
}
