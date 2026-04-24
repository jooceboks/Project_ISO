# Project ISO — Tabletop ISO-6 Cleanroom Chamber

Design, build, and validate a **tabletop ISO Class 6 cleanroom chamber** capable of maintaining ≤35,200 particles ≥0.5 µm/m³. Full-scale cleanrooms are impractical in an academic setting — this chamber brings equivalent contamination control to a benchtop form factor, enabling lithography experimentation, contamination control studies, and mechatronic system testing.

The project spans two disciplines: **Mechanical Engineering** (CAD, fabrication, HVAC/airflow design) and **Computer Science** (sensor integration, telemetry, control systems, and data visualization).

---

## Repository Structure

```
projectiso/
├── ISO_UNO/                  — Telemetry firmware (Arduino Uno R3)
│   └── ISO_UNO.ino
├── ISO_R4/                   — Control system firmware (Arduino Uno R4 WiFi)
│   ├── ISO_R4.ino
│   ├── Actuators.h / .cpp
│   ├── StateMachine.h / .cpp
│   └── secrets.h             — WiFi credentials (NOT committed to git)
├── ISO6.html                 — Unified browser dashboard
├── docs/
│   ├── SETUP.md              — Step-by-step hardware and software setup guide
│   └── superpowers/
│       ├── specs/            — Design specifications
│       └── plans/            — Implementation plans
└── README.md
```

---

## Systems Overview

### System 1 — Telemetry (Uno R3)

Monitors environmental conditions inside the chamber in real time. Streams live sensor data to the browser dashboard over USB using the **Web Serial API** — no server, no Wi-Fi, no cloud required.

| Sensor | Measures | Interface |
|---|---|---|
| Sensirion SHT31 | Temperature (±0.3°C), Relative Humidity | I2C → A4/A5 |
| Plantower PMS7003 | PM1.0, PM2.5, PM10, 6 particle size bins | UART 9600 baud → Pin 2 |
| NXP MPXV7002DP | Differential pressure ±2 kPa (positive pressure monitor) | Analog → A0 |

**ISO 6 pressure thresholds:**
- ≥ 25 Pa → ISO 6 COMPLIANT
- 15–24 Pa → WARNING — low positive pressure
- < 15 Pa → CRITICAL — ISO 6 FAILED

### System 2 — Control System (Uno R4 WiFi)

Automates a 7-step sample handling sequence via WiFi WebSocket commands from the dashboard. The R4 hosts a WebSocket server on port 81 — no additional software required.

**Motion sequence:**

| Step | Action | Hardware |
|---|---|---|
| 1 | Door opens | 4-inch linear actuator (relay) |
| 2 | Ballscrew moves right | NEMA 17 stepper + A4988 driver |
| 3 | Arm extends | Vevor OK628 linear actuator (relay) |
| 4 | Gripper grabs object | HS311 servo motor |
| 5 | Arm retracts | Vevor OK628 linear actuator (relay) |
| 6 | Ballscrew returns home | NEMA 17 stepper + A4988 driver |
| 7 | Door closes | 4-inch linear actuator (relay) |

---

## Dashboard (`ISO6.html`)

A single HTML file that runs entirely in the browser — no installation, no server.

**Telemetry panel** — connects to the Uno R3 via USB Web Serial:
- Live temperature, humidity, particle counts, differential pressure
- 60-point rolling history charts
- ISO 6 compliance traffic light indicator
- Time sync to Arduino clock

**Control system panel** — connects to the Uno R4 over WiFi WebSocket:
- Enter R4 IP address once (saved to browser localStorage)
- RUN SEQUENCE, ABORT, RESET buttons
- Real-time step progress bar (Step N of 7)
- Last run status (COMPLETE / ABORTED / FAULT with error name)

**Browser compatibility:** Chrome or Edge required (Web Serial API + WebSocket).

---

## Hardware Bill of Materials

### Telemetry System (Uno R3)

| Component | Purpose |
|---|---|
| Arduino Uno R3 | Microcontroller |
| Sensirion SHT31 | Temperature + humidity sensor |
| Plantower PMS7003 | Particle counter |
| NXP MPXV7002DP | Differential pressure sensor |
| BSS138 level shifter | 5V → 3.3V for PMS7003 TX line |

### Control System (Uno R4 WiFi)

| Component | Purpose |
|---|---|
| Arduino Uno R4 WiFi | Microcontroller + WiFi |
| Vevor OK628 (12V DC, 6-inch stroke) | Arm linear actuator |
| 4-inch DC linear actuator (12V) | Door actuator |
| NEMA 17 stepper motor (17HS4401 or equiv.) | Ballscrew drive |
| A4988 stepper driver module | Stepper controller |
| HS311 servo motor | Gripper |
| Songle 2-channel relay board × 2 | Switches actuators (active-HIGH) |
| 12V 10A DC power supply | Main power |
| LM2596 buck converter (12V → 5V, 2A) | Logic/servo power |

---

## Wiring Reference

### Telemetry (R3)

```
SHT31      VCC → 3.3V  |  GND → GND  |  SDA → A4  |  SCL → A5  |  ADR → GND
PMS7003    VCC (1+2) → 5V  |  GND (3+4) → GND  |  TX (9) → BSS138 → Pin 2
MPXV7002DP VCC → 5V  |  GND → GND  |  VOUT → A0
           Port 1 (+) → tube inside chamber
           Port 2 (−) → open to ambient room air
```

### Control System (R4)

```
Pin  Component         Function
D3   A4988 ENABLE      LOW = driver enabled (active-LOW)
D4   Relay ch1         Door EXTEND (HIGH = open)
D5   Relay ch2         Door RETRACT (HIGH = close)
D6   A4988 STEP        Stepper step pulse
D7   A4988 DIR         Stepper direction
D8   Relay ch3         Arm EXTEND (HIGH = extend)
D9   Relay ch4         Arm RETRACT (HIGH = retract)
D10  HS311 servo       Gripper PWM (0° = open, 90° = grab)

A4988:
  VMOT → 12V rail
  VDD  → 5V rail
  GND  → common GND
  MS1/MS2/MS3 → GND (full step, 200 steps/rev)
  SLEEP + RESET → tied together (or to 5V)

Power:
  12V rail  → actuators, A4988 VMOT
  5V rail   → relay coils, servo, A4988 VDD, R4 VIN
```

---

## Tunable Constants

These values are set in the firmware and should be calibrated with the physical hardware.

**`ISO_R4/Actuators.h`**

| Constant | Default | Description |
|---|---|---|
| `BALLSCREW_STEPS` | 400 | Steps for one full ballscrew traverse (tune to your pitch + travel distance) |
| `STEPPER_MAX_SPEED` | 800.0 | Steps/sec |
| `STEPPER_ACCELERATION` | 400.0 | Steps/sec² |
| `SERVO_OPEN_DEG` | 0 | Gripper open angle |
| `SERVO_GRAB_DEG` | 90 | Gripper closed/grab angle |

**`ISO_R4/StateMachine.h`**

| Constant | Default | Description |
|---|---|---|
| `T_DOOR_MS` | 3000 | Door open/close timeout (ms) — set ~20% longer than actual travel time |
| `T_BALLSCREW_MS` | 2000 | Ballscrew step timeout (ms) |
| `T_ACTUATOR_MS` | 2500 | Arm extend/retract timeout (ms) |
| `T_GRIPPER_MS` | 800 | Gripper grab dwell time (ms) |

---

## Tech Stack

| Layer | Technology |
|---|---|
| Telemetry firmware | Arduino C++ — SoftwareSerial, I2C, JSON over Serial |
| Control firmware | Arduino C++ — AccelStepper, Servo, WiFiS3, WebSocketsServer |
| Dashboard | HTML / Tailwind CSS / Vanilla JavaScript |
| Charts | Chart.js 3.9.1 — 60-point rolling history |
| Telemetry comms | Web Serial API (USB, Chrome/Edge only) |
| Control comms | WebSocket (WiFi, port 81) |
| CAD | SolidWorks |
| Standard | ISO 14644-1 cleanroom classification |

---

## Quick Start
