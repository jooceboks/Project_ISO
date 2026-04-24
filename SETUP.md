# Setup Guide — Project ISO Cleanroom Chamber

This guide walks through everything needed to get both systems running from scratch: the telemetry system (Uno R3) and the control system (Uno R4 WiFi), plus the browser dashboard.

---

## What You Need

### Software
- **Arduino IDE 2.x** — [arduino.cc/en/software](https://www.arduino.cc/en/software)
- **Chrome or Edge** browser (required for Web Serial API and WebSocket)
- Git (optional, for version control)

### Hardware
See the full bill of materials in [README.md](../README.md).

---

## Part 1 — Telemetry System (Uno R3)

### Step 1: Install Arduino Libraries

Open Arduino IDE. Go to **Tools → Manage Libraries** and install:

| Library | Search term | Author |
|---|---|---|
| Adafruit SHT31 | `SHT31` | Adafruit |
| Adafruit BusIO | auto-installed with SHT31 | Adafruit |

### Step 2: Wire the Sensors

**SHT31 (temperature + humidity)**
```
SHT31 VCC  → Arduino 3.3V
SHT31 GND  → Arduino GND
SHT31 SDA  → Arduino A4
SHT31 SCL  → Arduino A5
SHT31 ADR  → Arduino GND     (sets I2C address to 0x44)
```

**PMS7003 (particle counter)**
```
PMS7003 VCC  (pins 1+2) → Arduino 5V
PMS7003 GND  (pins 3+4) → Arduino GND
PMS7003 TX   (pin 9)    → BSS138 level shifter → Arduino Pin 2
```
> The PMS7003 outputs 3.3V logic — the BSS138 level shifter steps it down safely before Pin 2.

**MPXV7002DP (differential pressure)**
```
MPXV7002DP VCC  → Arduino 5V
MPXV7002DP GND  → Arduino GND
MPXV7002DP VOUT → Arduino A0

Tube routing:
  Port 1 (+) → small tube running INSIDE the cleanroom chamber
  Port 2 (−) → open to ambient room air outside the chamber
```
> The sensor reads inside − outside pressure. Target is +25 Pa (positive pressure keeps particles out).

### Step 3: Calibrate the Pressure Sensor

The firmware automatically calibrates on power-up. Before uploading:
1. Make sure the HVAC/fan is **OFF**
2. Both sensor ports should be equalised (or Port 1 tube is disconnected from the chamber)

The calibration runs for ~0.5 seconds in `setup()` and prints the zero offset to Serial Monitor.

### Step 4: Upload the Telemetry Firmware

1. Connect the Uno R3 to your computer via USB
2. Open `ISO_UNO/ISO_UNO.ino` in Arduino IDE
3. Select **Tools → Board → Arduino Uno**
4. Select **Tools → Port → (your Arduino port)**
5. Click **Upload**
6. Open **Tools → Serial Monitor** at **115200 baud**

Expected Serial Monitor output:
```
PRESSURE: calibrating zero offset (keep HVAC off)...
PRESSURE: zero offset = 2.5012 V  (calibration complete)
{"ok_sht":true,"ok_pms":false,"ok_press":true,...}
```
> `ok_pms` will be `false` for ~10 seconds while the PMS7003 warms up. This is normal.

### Step 5: Open the Dashboard (Telemetry)

1. Open `ISO6.html` in **Chrome or Edge** (not Firefox — Web Serial not supported)
2. Click **connect serial** in the top-right of the dashboard
3. Select your Arduino Uno R3 from the port list
4. Live data should begin streaming immediately

**What you'll see:**
- Temperature, humidity, pressure readings updating every second
- Particle count table with ISO 6 compliance status
- Rolling 60-point history charts
- Traffic light (green = compliant, yellow = warning, red = critical)

---

## Part 2 — Control System (Uno R4 WiFi)

### Step 1: Install Arduino Board Package

1. Open Arduino IDE
2. Go to **Tools → Board → Boards Manager**
3. Search for `Arduino UNO R4`
4. Install **Arduino UNO R4 Boards** by Arduino

### Step 2: Install Arduino Libraries

Go to **Tools → Manage Libraries** and install:

| Library | Search term | Author |
|---|---|---|
| WebSockets | `WebSockets` | Markus Sattler |
| AccelStepper | `AccelStepper` | Mike McCauley |

> The `Servo` and `WiFiS3` libraries are built into the R4 board package — no separate install needed.

### Step 3: Wire the Control System

**Power supply setup first**
```
12V 10A PSU
  ├── 12V rail  → Actuators (via relays), A4988 VMOT
  └── LM2596    → 5V output → Relay coils (VCC), HS311 servo (VCC), A4988 VDD, R4 5V pin
```
> IMPORTANT: Do NOT power the R4 from the 12V rail directly (max VIN is 21V but noise from motor switching can damage it). Use the regulated 5V from the buck converter.

**A4988 Stepper Driver**
```
A4988 VMOT  → 12V rail
A4988 GND   → common GND (12V side)
A4988 VDD   → 5V rail
A4988 GND   → common GND (5V side)
A4988 STEP  → R4 D6
A4988 DIR   → R4 D7
A4988 ENABLE→ R4 D3
A4988 MS1   → GND  (full step mode — 200 steps/rev)
A4988 MS2   → GND
A4988 MS3   → GND
A4988 SLEEP → tied to RESET
A4988 RESET → tied to SLEEP (or both to 5V)
A4988 1A/1B/2A/2B → NEMA 17 motor coils (check motor datasheet for coil pairing)
```

**Relay boards (active-HIGH — relay energises on HIGH)**
```
Relay board 1:
  VCC → 5V rail
  GND → common GND
  IN1 → R4 D4    (door EXTEND — HIGH = door opens)
  IN2 → R4 D5    (door RETRACT — HIGH = door closes)
  COM1/NO1 → door actuator + terminal
  COM2/NO2 → door actuator − terminal
  (wire so that IN1 energised = current flows to extend, IN2 energised = current flows to retract)

Relay board 2:
  VCC → 5V rail
  GND → common GND
  IN1 → R4 D8    (arm EXTEND — HIGH = arm extends)
  IN2 → R4 D9    (arm RETRACT — HIGH = arm retracts)
  COM1/NO1 → Vevor OK628 + terminal
  COM2/NO2 → Vevor OK628 − terminal
```
> The Vevor OK628 datasheet: Red wire (+) → extend, Black wire (−) → retract. Reversing polarity via the relay reverses direction.

**HS311 Servo (gripper)**
```
HS311 Red   → 5V rail
HS311 Black → common GND
HS311 White/Yellow (signal) → R4 D10
```

**Arduino Uno R4 WiFi**
```
R4 5V pin → 5V rail (from LM2596)
R4 GND    → common GND
```

### Step 4: Set WiFi Credentials

Open `ISO_R4/secrets.h` and replace the placeholders:

```cpp
#define WIFI_SSID "YourNetworkName"
#define WIFI_PASS "YourPassword"
```

> `secrets.h` is listed in `.gitignore` and will NOT be committed to git. Keep your credentials safe.

### Step 5: Upload the Control Firmware

1. Connect the Uno R4 WiFi to your computer via USB
2. Open `ISO_R4/ISO_R4.ino` in Arduino IDE
3. Select **Tools → Board → Arduino UNO R4 WiFi**
4. Select **Tools → Port → (your R4 port)**
5. Click **Upload**
6. Open **Tools → Serial Monitor** at **115200 baud**

Expected output:
```
Connecting to WiFi.....
ISO_R4 IP: 192.168.1.42
WebSocket server started on port 81
Ready.
```

> If you see `WiFi FAILED — WebSocket unavailable`, check your credentials in `secrets.h` and that your 2.4 GHz network is reachable. The R4 does not support 5 GHz WiFi.

**Write down the IP address** — you'll need it to connect the dashboard.

### Step 6: Connect the Dashboard (Control)

1. Open `ISO6.html` in Chrome or Edge (same file as telemetry)
2. Scroll to the **Control System** panel at the bottom
3. Enter the R4's IP address (e.g. `192.168.1.42`) in the input field
4. Click **connect**
5. The dot turns green and status shows **connected**

---

## Part 3 — Calibrating the Control System

Before running the full sequence with hardware connected, tune the constants for your specific build.

### Step 1: Tune the Ballscrew Steps

The default `BALLSCREW_STEPS = 400` is a starting estimate. The actual number depends on your ballscrew pitch and the distance the arm needs to travel.

1. Open `ISO_R4/Actuators.h`
2. Find `#define BALLSCREW_STEPS 400`
3. Run the sequence via the dashboard — watch how far the ballscrew moves
4. **Overshoots** → decrease the value
5. **Undershoots** → increase the value
6. Re-upload and repeat until travel distance is correct

> Rule of thumb: at full step (200 steps/rev), a 2mm pitch ballscrew moves 2mm per revolution. For 50mm of travel: 50 / 2 × 200 = 5000 steps.

### Step 2: Tune Timeouts

Default timeouts are conservative estimates. Adjust them based on observed hardware timing.

Open `ISO_R4/StateMachine.h`:

```cpp
#define T_DOOR_MS     3000   // door open/close travel time
#define T_ACTUATOR_MS 2500   // arm extend/retract travel time
#define T_GRIPPER_MS   800   // time for servo to reach grab position
#define T_BALLSCREW_MS 2000  // max time allowed for stepper to complete
```

Run the sequence and watch the Serial Monitor. If a FAULT fires with `DOOR_TIMEOUT` or `BALLSCREW_TIMEOUT`, increase the relevant constant by 500ms and re-upload.

> The Vevor OK628 has internal limit switches that auto-stop at end positions — you don't need the timeout to be exact, just longer than the actual travel time.

### Step 3: Tune Gripper Angles

Default: 0° = open, 90° = grab.

Open `ISO_R4/Actuators.h`:

```cpp
#define SERVO_OPEN_DEG   0
#define SERVO_GRAB_DEG  90
```

If the gripper doesn't fully open or close, adjust these values. The HS311 has a 180° range. Test by running the sequence and observing the gripper at Step 4.

### Step 4: Commit Calibrated Values

Once the sequence runs correctly end-to-end:

```bash
git add ISO_R4/Actuators.h ISO_R4/StateMachine.h
git commit -m "tune: calibrated constants for hardware"
```

---

## Running the Full System

Once both systems are set up:

1. Power on the 12V supply (control system)
2. Connect Uno R3 via USB to your computer
3. Connect Uno R4 WiFi via USB to your computer (or just power — it connects over WiFi)
4. Open `ISO6.html` in Chrome/Edge
5. Click **connect serial** → select R3 → telemetry data streams
6. Scroll down, enter R4 IP, click **connect** → control panel goes live
7. Click **RUN SEQUENCE** to execute the full 7-step motion

---

## Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| Dashboard shows no data after connecting serial | Wrong COM port selected | Disconnect and reconnect, select correct port |
| `ok_pms: false` in Serial Monitor | PMS7003 still warming up | Wait 10–15 seconds |
| Pressure reads wildly wrong | Calibration ran with HVAC on | Power cycle R3 with HVAC off |
| WiFi FAILED on R4 boot | Wrong credentials or 5 GHz network | Check `secrets.h`, ensure 2.4 GHz band |
| Control panel shows disconnected after entering IP | R4 not on same network, or wrong IP | Check Serial Monitor for correct IP |
| DOOR_TIMEOUT fault | Door actuator travel time > T_DOOR_MS | Increase `T_DOOR_MS` in `StateMachine.h` |
| BALLSCREW_TIMEOUT fault | Stepper stalled or `T_BALLSCREW_MS` too short | Check motor wiring, increase timeout |
| Gripper doesn't grab | Servo angles need tuning | Adjust `SERVO_GRAB_DEG` in `Actuators.h` |
| Relay clicks but actuator doesn't move | Actuator wired to NC instead of NO terminal | Move wires to NO (Normally Open) terminals on relay |
| Stepper makes noise but doesn't move | A4988 ENABLE pin not driven LOW | Check D3 wiring, check `actuatorsInit()` ran |
| Sequence aborts mid-run | ABORT button clicked, or WebSocket disconnected | Check network stability, re-run |

---

## WebSocket Protocol Reference

The R4 firmware communicates via JSON over WebSocket (port 81).

**Commands (dashboard → R4):**

| Message | Action |
|---|---|
| `{"cmd":"RUN"}` | Start the 7-step sequence (only works from IDLE state) |
| `{"cmd":"ABORT"}` | Emergency stop — cuts all outputs immediately, returns to IDLE |
| `{"cmd":"RESET"}` | Clears FAULT state, returns to IDLE (no motion) |

**Status messages (R4 → dashboard):**

| Message | Meaning |
|---|---|
| `{"state":"DOOR_OPENING","step":1,"of":7}` | Step in progress |
| `{"state":"IDLE","result":"OK"}` | Sequence completed successfully |
| `{"state":"IDLE","result":"ABORTED"}` | Sequence was aborted |
| `{"state":"IDLE","result":"RESET"}` | FAULT was cleared |
| `{"state":"FAULT","error":"DOOR_TIMEOUT"}` | Step timed out — awaiting RESET |
| `{"error":"NOT_IDLE"}` | RUN rejected — sequence already running |

**Fault error codes:**

| Error | Meaning |
|---|---|
| `DOOR_TIMEOUT` | Door did not complete travel within `T_DOOR_MS` |
| `BALLSCREW_TIMEOUT` | Stepper did not complete move within `T_BALLSCREW_MS` |
| `BALLSCREW_HOME_TIMEOUT` | Stepper did not return home within `T_BALLSCREW_MS` |

---

## File Reference

| File | Purpose |
|---|---|
| `ISO_UNO/ISO_UNO.ino` | Telemetry firmware — reads sensors, emits JSON over Serial |
| `ISO_R4/ISO_R4.ino` | Control firmware — WiFi, WebSocket server, main loop |
| `ISO_R4/Actuators.h` | Pin assignments, tunable constants, function declarations |
| `ISO_R4/Actuators.cpp` | Relay, stepper, servo control functions |
| `ISO_R4/StateMachine.h` | State enum, timeout constants, class declaration |
| `ISO_R4/StateMachine.cpp` | 9-state machine logic, JSON broadcast |
| `ISO_R4/secrets.h` | WiFi credentials (not in git — edit this file) |
| `ISO6.html` | Browser dashboard — telemetry + control in one file |
