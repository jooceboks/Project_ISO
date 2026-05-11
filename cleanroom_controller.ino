/*
  ============================================================
  ISO 6 Cleanroom Controller — Arduino UNO R4 WiFi
  ============================================================
  - AP web server (LedTest / Test1234 / http://192.168.1.1)
  - Pick-and-place sequence (GRAB / RETURN, mirrored gripper)
  - E-Stop with interruptible delays
  - Sensors:
      • SHT31  (I2C, addr 0x44)         -> temp / humidity
      • PMS7003 (Serial1 @ 9600 baud)   -> particle counts
      • MPXV7002DP (A0, via 2:3 divider)-> differential pressure

  Endpoints (matches the dashboard HTML):
    GET /on      -> Pin 2 LED ON
    GET /off     -> Pin 2 LED OFF
    GET /grab    -> Full 7-step cycle, gripper CLOSES (pick up)
    GET /return  -> Full 7-step cycle, gripper OPENS (drop off)
    GET /estop   -> Halts everything immediately
    GET /clear   -> Clears the E-Stop latch

  USB Serial commands (from dashboard):
    T<unix_seconds>  -> set wall-clock time (UTC)

  Status JSON is emitted on USB Serial every ~1 s for the dashboard.
  ============================================================
*/

#include <WiFiS3.h>
#include <Servo.h>
#include <Wire.h>

// ── WiFi AP credentials ──────────────────────────────────────
const char* ssid     = "ProjectISO";
const char* password = "Stags123";

// ── Motor / actuator pins ───────────────────────────────────
const int LED_PIN   = 2;
const int BLDC_PWM  = 3;
const int DOOR_EXT  = 5;
const int DOOR_RET  = 6;
const int RA_EXT    = 7;
const int RA_RET    = 8;
const int SERVO_PIN = 9;
const int BLDC_DIR  = 10;
const int BLDC_EN   = 11;

// ── Sensor pins ─────────────────────────────────────────────
const int PRESS_PIN = A0;
// SHT31 -> dedicated SDA / SCL pins
// PMS7003 -> Serial1 (pins 0/1)

// ── Sequence timings (DO NOT CHANGE) ────────────────────────
const unsigned long DOOR_TIME1 = 22000;
const unsigned long DOOR_TIME2 = 18500;
const unsigned long RA_TIME    = 38000;
const unsigned long BLDC_FWD   = 5500;
const unsigned long BLDC_REV   = 5750;
const unsigned long RELAY_GAP  = 1000;

const int GRIP_OPEN   = 0;
const int GRIP_CLOSED = 45;

// ── Globals ─────────────────────────────────────────────────
Servo gripper;
WiFiServer server(80);

volatile bool estopFlag  = false;
bool          ledState   = false;
bool          sequencing = false;

unsigned long lastStatusMs = 0;
const unsigned long STATUS_INTERVAL = 1000;

// ── Time sync ───────────────────────────────────────────────
unsigned long timeEpochAtBoot = 0;   // unix seconds at boot
bool          timeSynced      = false;
String        usbBuf;

// ── SHT31 state ─────────────────────────────────────────────
const uint8_t SHT31_ADDR = 0x44;
float lastTemp = 0, lastHum = 0;
bool  ok_sht   = false;
unsigned long lastShtMs = 0;

// ── MPXV7002DP state ────────────────────────────────────────
const int   PRESS_SAMPLES = 16;
float       pressBuf[PRESS_SAMPLES] = {0};
int         pressIdx = 0;
float       lastPressurePa = 0;
unsigned long lastPressMs = 0;
// Divider compensation: A0 sees V_sensor * (R2/(R1+R2)) = V_sensor * 2/3
// So V_sensor = V_adc * 3/2 = V_adc * 1.5
const float DIVIDER_GAIN = 1.5f;
const float ADC_REF_V    = 3.3f;
const float ADC_FULL     = 1023.0f;
const float PRESS_ZERO_V = 2.5f;     // sensor output at 0 Pa with 5V supply
const float PRESS_SENS   = 0.001f;   // V/Pa (1 mV/Pa)

// ── PMS7003 state ───────────────────────────────────────────
uint8_t  pms_buf[32];
uint8_t  pms_idx     = 0;
uint16_t pms_n03 = 0, pms_n05 = 0, pms_n10 = 0;
uint16_t pms_n25 = 0, pms_n50 = 0, pms_n100 = 0;
uint32_t pms_frames  = 0;
uint32_t pms_bytes   = 0;
bool     ok_pms      = false;

// Forward decls
void handleClient();
void sendStatusIfDue();
void sensorTick();

// ============================================================
//  Hardware stop helpers
// ============================================================
void stopRA()   { digitalWrite(RA_EXT, LOW);   digitalWrite(RA_RET, LOW); }
void stopDoor() { digitalWrite(DOOR_EXT, LOW); digitalWrite(DOOR_RET, LOW); }
void stopBLDC() {
  analogWrite(BLDC_PWM, 0);
  delay(50);
  digitalWrite(BLDC_EN, HIGH);
  delay(100);
  digitalWrite(BLDC_EN, LOW);
  delay(50);
  digitalWrite(BLDC_EN, HIGH);
}
void haltAll() {
  digitalWrite(RA_EXT,   LOW);
  digitalWrite(RA_RET,   LOW);
  digitalWrite(DOOR_EXT, LOW);
  digitalWrite(DOOR_RET, LOW);
  analogWrite(BLDC_PWM, 0);
  digitalWrite(BLDC_EN, HIGH);
}

// ============================================================
//  Interruptible delay — polls everything every 5 ms
// ============================================================
bool waitMs(unsigned long ms) {
  unsigned long start = millis();
  while (millis() - start < ms) {
    if (estopFlag) return false;
    handleClient();
    sensorTick();
    sendStatusIfDue();
    delay(5);
  }
  return !estopFlag;
}

#define WAIT_OR_BAIL(ms) do {                                  \
    if (!waitMs(ms)) {                                         \
      haltAll();                                               \
      sequencing = false;                                      \
      Serial.println(F("[SEQ] *** ABORTED BY E-STOP ***"));    \
      return;                                                  \
    }                                                          \
  } while (0)

// ============================================================
//  Pick-and-place sequence (UNCHANGED ORDER & TIMING)
// ============================================================
void runSequence(const char* name, int gripperTarget) {
  if (sequencing || estopFlag) return;
  sequencing = true;
  Serial.print(F("[SEQ] ")); Serial.print(name); Serial.println(F(" start"));

  // 1. Door opens
  WAIT_OR_BAIL(RELAY_GAP);
  digitalWrite(DOOR_EXT, HIGH);
  WAIT_OR_BAIL(DOOR_TIME2);
  stopDoor();
  WAIT_OR_BAIL(1000);

  // 2. BLDC forward
  digitalWrite(BLDC_DIR, LOW);
  WAIT_OR_BAIL(100);
  digitalWrite(BLDC_EN, LOW);
  analogWrite(BLDC_PWM, 80);
  WAIT_OR_BAIL(BLDC_FWD);
  stopBLDC();
  Serial.println(F("[DEBUG] BLDC done, estop=")); Serial.println(estopFlag);
  WAIT_OR_BAIL(500);

  // 3. RA extends
  WAIT_OR_BAIL(RELAY_GAP);
  digitalWrite(RA_EXT, HIGH);
  WAIT_OR_BAIL(RA_TIME);
  stopRA();
  WAIT_OR_BAIL(1000);

  // 4. Gripper actuates
  gripper.write(gripperTarget);
  WAIT_OR_BAIL(1000);

  // 5. RA retracts
  WAIT_OR_BAIL(RELAY_GAP);
  digitalWrite(RA_RET, HIGH);
  WAIT_OR_BAIL(RA_TIME);
  stopRA();
  WAIT_OR_BAIL(500);

  // 6. BLDC reverse
  digitalWrite(BLDC_DIR, HIGH);
  WAIT_OR_BAIL(100);
  digitalWrite(BLDC_EN, LOW);
  analogWrite(BLDC_PWM, 80);
  WAIT_OR_BAIL(BLDC_REV);
  stopBLDC();
  WAIT_OR_BAIL(500);

  // 7. Door closes
  WAIT_OR_BAIL(RELAY_GAP);
  digitalWrite(DOOR_RET, HIGH);
  WAIT_OR_BAIL(DOOR_TIME1);
  stopDoor();
  WAIT_OR_BAIL(1000);

  sequencing = false;
  Serial.print(F("[SEQ] ")); Serial.print(name); Serial.println(F(" done"));
}
inline void runGrab()   { runSequence("GRAB",   GRIP_CLOSED); }
inline void runReturn() { runSequence("RETURN", GRIP_OPEN);   }

// ============================================================
//  SHT31 raw I2C read (no external library)
// ============================================================
bool sht31Begin() {
  // Soft reset
  Wire.beginTransmission(SHT31_ADDR);
  Wire.write(0x30);
  Wire.write(0xA2);
  bool ok = (Wire.endTransmission() == 0);
  delay(10);
  return ok;
}

bool sht31Read(float& tempC, float& humPct) {
  // High-repeatability single-shot, no clock stretching
  Wire.beginTransmission(SHT31_ADDR);
  Wire.write(0x24);
  Wire.write(0x00);
  if (Wire.endTransmission() != 0) return false;

  delay(20);   // measurement time

  if (Wire.requestFrom(SHT31_ADDR, (uint8_t)6) != 6) return false;
  uint8_t b[6];
  for (int i = 0; i < 6; i++) b[i] = Wire.read();

  uint16_t rawT = ((uint16_t)b[0] << 8) | b[1];
  uint16_t rawH = ((uint16_t)b[3] << 8) | b[4];
  tempC  = -45.0f + 175.0f * ((float)rawT / 65535.0f);
  humPct = 100.0f * ((float)rawH / 65535.0f);
  return true;
}

void readSht31IfDue() {
  if (millis() - lastShtMs < 1000) return;
  lastShtMs = millis();
  float t, h;
  if (sht31Read(t, h)) {
    lastTemp = t; lastHum = h; ok_sht = true;
  } else {
    ok_sht = false;
  }
}

// ============================================================
//  MPXV7002DP read with running average
// ============================================================
void readPressureIfDue() {
  if (millis() - lastPressMs < 50) return;   // 20 Hz
  lastPressMs = millis();

  int   raw      = analogRead(PRESS_PIN);
  float v_adc    = (raw / ADC_FULL) * ADC_REF_V;
  float v_sensor = v_adc * DIVIDER_GAIN;
  float pa       = (v_sensor - PRESS_ZERO_V) / PRESS_SENS +782.0f;

  pressBuf[pressIdx] = pa;
  pressIdx = (pressIdx + 1) % PRESS_SAMPLES;

  float sum = 0;
  for (int i = 0; i < PRESS_SAMPLES; i++) sum += pressBuf[i];
  lastPressurePa = sum / PRESS_SAMPLES;
}

// ============================================================
//  PMS7003 byte-stream parser (state machine, non-blocking)
//  Frame: 0x42 0x4D | len(2) | data(26) | checksum(2)  = 32 bytes
// ============================================================
void pollPms7003() {
  while (Serial1.available()) {
    uint8_t b = Serial1.read();
    pms_bytes++;

    if (pms_idx == 0) {
      if (b == 0x42) { pms_buf[pms_idx++] = b; }
    } else if (pms_idx == 1) {
      if (b == 0x4D) { pms_buf[pms_idx++] = b; }
      else           { pms_idx = 0; }
    } else {
      pms_buf[pms_idx++] = b;
      if (pms_idx == 32) {
        // Verify checksum
        uint16_t sum = 0;
        for (int i = 0; i < 30; i++) sum += pms_buf[i];
        uint16_t cks = ((uint16_t)pms_buf[30] << 8) | pms_buf[31];
        if (sum == cks) {
          // Particles per 0.1 L start at byte index 16
          pms_n03  = ((uint16_t)pms_buf[16] << 8) | pms_buf[17];
          pms_n05  = ((uint16_t)pms_buf[18] << 8) | pms_buf[19];
          pms_n10  = ((uint16_t)pms_buf[20] << 8) | pms_buf[21];
          pms_n25  = ((uint16_t)pms_buf[22] << 8) | pms_buf[23];
          pms_n50  = ((uint16_t)pms_buf[24] << 8) | pms_buf[25];
          pms_n100 = ((uint16_t)pms_buf[26] << 8) | pms_buf[27];
          pms_frames++;
          ok_pms = true;
        }
        pms_idx = 0;
      }
    }
  }
}

// ============================================================
//  Sensor tick — call from loop() and inside waitMs()
// ============================================================
void sensorTick() {
  pollPms7003();
  readSht31IfDue();
  readPressureIfDue();
}

// ============================================================
//  USB Serial command parser (handles "T<unix>" from dashboard)
// ============================================================
void pollUsbSerial() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (usbBuf.length() > 1 && usbBuf[0] == 'T') {
        unsigned long t = (unsigned long) usbBuf.substring(1).toInt();
        if (t > 1700000000UL) {            // sanity: must be > Nov 2023
          timeEpochAtBoot = t - (millis() / 1000);
          timeSynced = true;
          Serial.println(F("[CMD] time synced"));
        }
      }
      usbBuf = "";
    } else {
      usbBuf += c;
      if (usbBuf.length() > 32) usbBuf = "";   // overflow guard
    }
  }
}

// ============================================================
//  HTTP helpers
// ============================================================
void sendShortResponse(WiFiClient &client, const char* body) {
  client.println(F("HTTP/1.1 200 OK"));
  client.println(F("Content-Type: text/plain"));
  client.println(F("Access-Control-Allow-Origin: *"));
  client.println(F("Connection: close"));
  client.println();
  client.println(body);
}

void handleClient() {
  WiFiClient client = server.available();
  if (!client) return;

  String request = "";
  unsigned long timeout = millis();
  while (client.connected() && millis() - timeout < 200) {
    if (client.available()) {
      char c = client.read();
      request += c;
      if (request.endsWith("\r\n\r\n")) break;
      if (request.length() > 512) break;
    }
  }

  if (request.indexOf("GET /estop") >= 0) {
    estopFlag = true;
    haltAll();
    Serial.println(F("[CMD] *** E-STOP FIRED ***"));
    sendShortResponse(client, "ESTOP");
  }
  else if (request.indexOf("GET /clear") >= 0) {
    estopFlag = false;
    Serial.println(F("[CMD] E-Stop cleared"));
    sendShortResponse(client, "CLEAR");
  }
  else if (request.indexOf("GET /on") >= 0) {
    ledState = true; digitalWrite(LED_PIN, HIGH);
    Serial.println(F("[CMD] LED ON"));
    sendShortResponse(client, "LED ON");
  }
  else if (request.indexOf("GET /off") >= 0) {
    ledState = false; digitalWrite(LED_PIN, LOW);
    Serial.println(F("[CMD] LED OFF"));
    sendShortResponse(client, "LED OFF");
  }
  else if (request.indexOf("GET /grab") >= 0) {
    if (estopFlag)        { sendShortResponse(client, "BLOCKED: E-STOP ACTIVE"); }
    else if (sequencing)  { sendShortResponse(client, "BUSY"); }
    else {
      sendShortResponse(client, "GRAB started");
      client.stop();
      runGrab();
      return;
    }
  }
  else if (request.indexOf("GET /data") >= 0) {
  WiFiClient dataClient = client;
  dataClient.println(F("HTTP/1.1 200 OK"));
  dataClient.println(F("Content-Type: application/json"));
  dataClient.println(F("Access-Control-Allow-Origin: *"));
  dataClient.println(F("Connection: close"));
  dataClient.println();

  dataClient.print(F("{"));
  dataClient.print(F("\"ok_sht\":")); dataClient.print(ok_sht ? F("true") : F("false"));
  if (ok_sht) {
    dataClient.print(F(",\"temp\":")); dataClient.print(lastTemp, 2);
    dataClient.print(F(",\"hum\":")); dataClient.print(lastHum, 2);
  }
  dataClient.print(F(",\"pressure_pa\":")); dataClient.print(lastPressurePa, 2);
  dataClient.print(F(",\"ok_pms\":")); dataClient.print(ok_pms ? F("true") : F("false"));
  dataClient.print(F(",\"frames\":")); dataClient.print(pms_frames);
  dataClient.print(F(",\"bytes\":")); dataClient.print(pms_bytes);
  dataClient.print(F(",\"estop\":")); dataClient.print(estopFlag ? F("true") : F("false"));
  if (ok_pms) {
    dataClient.print(F(",\"n03\":")); dataClient.print(pms_n03);
    dataClient.print(F(",\"n05\":")); dataClient.print(pms_n05);
    dataClient.print(F(",\"n10\":")); dataClient.print(pms_n10);
    dataClient.print(F(",\"n25\":")); dataClient.print(pms_n25);
    dataClient.print(F(",\"n50\":")); dataClient.print(pms_n50);
    dataClient.print(F(",\"n100\":")); dataClient.print(pms_n100);
  }
  dataClient.println(F("}"));
}
  else if (request.indexOf("GET /return") >= 0) {
    if (estopFlag)        { sendShortResponse(client, "BLOCKED: E-STOP ACTIVE"); }
    else if (sequencing)  { sendShortResponse(client, "BUSY"); }
    else {
      sendShortResponse(client, "RETURN started");
      client.stop();
      runReturn();
      return;
    }
  }
  else {
    sendShortResponse(client, "OK");
  }

  client.stop();
}

// ============================================================
//  Status JSON for the dashboard (USB Serial, ~1 Hz)
// ============================================================
void sendStatusIfDue() {
  if (millis() - lastStatusMs < STATUS_INTERVAL) return;
  lastStatusMs = millis();

  Serial.print(F("{"));

  // Time
  if (timeSynced) {
    unsigned long now = timeEpochAtBoot + (millis() / 1000);
    unsigned long s   = now % 86400UL;
    int h = s / 3600, m = (s % 3600) / 60, ss = s % 60;
    char buf[12];
    snprintf(buf, sizeof(buf), "\"%02d:%02d:%02d\"", h, m, ss);
    Serial.print(F("\"time\":")); Serial.print(buf);
    Serial.print(F(",\"synced\":true,"));
  }

  // Machine state
  Serial.print(F("\"estop\":")); Serial.print(estopFlag ? F("true") : F("false"));
  Serial.print(F(",\"led\":"));  Serial.print(ledState  ? F("true") : F("false"));
  Serial.print(F(",\"seq\":\"")); Serial.print(sequencing ? F("running") : F("idle")); Serial.print(F("\""));

  // SHT31
  Serial.print(F(",\"ok_sht\":")); Serial.print(ok_sht ? F("true") : F("false"));
  if (ok_sht) {
    Serial.print(F(",\"temp\":")); Serial.print(lastTemp, 2);
    Serial.print(F(",\"hum\":"));  Serial.print(lastHum, 2);
  }

  // Pressure
  Serial.print(F(",\"pressure_pa\":")); Serial.print(lastPressurePa, 2);

  // PMS7003
  Serial.print(F(",\"ok_pms\":")); Serial.print(ok_pms ? F("true") : F("false"));
  Serial.print(F(",\"frames\":")); Serial.print(pms_frames);
  Serial.print(F(",\"bytes\":"));  Serial.print(pms_bytes);
  if (ok_pms) {
    Serial.print(F(",\"n03\":"));  Serial.print(pms_n03);
    Serial.print(F(",\"n05\":"));  Serial.print(pms_n05);
    Serial.print(F(",\"n10\":"));  Serial.print(pms_n10);
    Serial.print(F(",\"n25\":"));  Serial.print(pms_n25);
    Serial.print(F(",\"n50\":"));  Serial.print(pms_n50);
    Serial.print(F(",\"n100\":")); Serial.print(pms_n100);
  }

  Serial.println(F("}"));
}

// ============================================================
//  setup()
// ============================================================
void setup() {
  Serial.begin(115200);
  unsigned long t0 = millis();
  while (!Serial && millis() - t0 < 3000) { /* spin briefly */ }

  Serial.println(F("---------------------------"));
  Serial.println(F("  Cleanroom Controller R4  "));
  Serial.println(F("---------------------------"));

  // BLDC init (matches original)
  pinMode(BLDC_DIR, OUTPUT);
  pinMode(BLDC_EN,  OUTPUT);
  pinMode(BLDC_PWM, OUTPUT);
  digitalWrite(BLDC_EN,  HIGH);
  digitalWrite(BLDC_DIR, LOW);
  analogWrite(BLDC_PWM,  0);
  delay(2000);
  digitalWrite(BLDC_EN, LOW);

  // Actuators
  pinMode(RA_EXT,   OUTPUT);
  pinMode(RA_RET,   OUTPUT);
  pinMode(DOOR_EXT, OUTPUT);
  pinMode(DOOR_RET, OUTPUT);
  stopRA();
  stopDoor();

  // Servo
  gripper.attach(SERVO_PIN);
  gripper.write(GRIP_OPEN);
  delay(1000);

  // LED
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // Sensors
  pinMode(PRESS_PIN, INPUT);
  Wire.begin();
  if (sht31Begin()) {
    Serial.println(F("SHT31 OK"));
  } else {
    Serial.println(F("WARN: SHT31 not responding (check wiring)"));
  }
  Serial1.begin(9600);   // PMS7003 baud
  Serial.println(F("PMS7003 listening on Serial1"));

  // WiFi AP
  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println(F("ERROR: WiFi module not found!"));
    while (true);
  }
  IPAddress ip(192, 168, 1, 1);
  WiFi.config(ip);
  int status = WiFi.beginAP(ssid, password);
  if (status != WL_AP_LISTENING) {
    Serial.print(F("ERROR: AP failed, status=")); Serial.println(status);
    while (true);
  }
  delay(1000);
  server.begin();

  Serial.println(F("---------------------------"));
  Serial.print(F("Network : ")); Serial.println(ssid);
  Serial.print(F("Password: ")); Serial.println(password);
  Serial.print(F("Open    : http://")); Serial.println(WiFi.localIP());
  Serial.println(F("Endpoints: /on /off /grab /return /estop /clear"));
  Serial.println(F("---------------------------"));
  Serial.println(F("Ready."));
}

// ============================================================
//  loop()
// ============================================================
void loop() {
  handleClient();
  sensorTick();
  pollUsbSerial();
  sendStatusIfDue();
}
