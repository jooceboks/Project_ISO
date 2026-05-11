# Project ISO — Tabletop ISO-6 Cleanroom Chamber

Design, build, and validate a **tabletop ISO Class 6 cleanroom chamber** capable of maintaining ≤35,200 particles ≥0.5 µm/m³. Full-scale cleanrooms are impractical in an academic setting — this chamber brings equivalent contamination control to a benchtop form factor, enabling lithography experimentation, contamination control studies, and mechatronic system testing.

The project spans two disciplines: **Mechanical Engineering** (CAD, fabrication, HVAC/airflow design) and **Computer Science** (sensor integration, telemetry, control systems, and data visualization).

---

## Repository Structure

```
projectiso/
├── cleanroom_controller.ino  — Unified firmware (Arduino Uno R4 WiFi)
├── dashboard.html            — Browser dashboard (telemetry + control)
├── .gitignore
└── README.md
```

---

## How It Works

A single **Arduino Uno R4 WiFi** runs everything: environmental sensors, a pick-and-place motion sequence, and an HTTP server. The R4 creates its own WiFi access point — no router or internet required.

The **browser dashboard** (`dashboard.html`) connects two ways:
1. **USB Serial** (Web Serial API) — streams live sensor data from the R4 at 1 Hz
2. **WiFi HTTP** — sends control commands (GRAB, RETURN, E-STOP) to the R4's built-in web server

---

## Sensors

| Sensor | Measures | Interface |
|---|---|---|
| Sensirion SHT31 | Temperature (±0.3 °C), Relative Humidity | I2C (SDA/SCL) |
| Plantower PMS7003 | Particle counts: ≥0.3, ≥0.5, ≥1.0, ≥2.5, ≥5.0, ≥10.0 µm | Serial1 @ 9600 baud |
| NXP MPXV7002DP | Differential pressure ±2 kPa | Analog (A0), via 2:3 voltage divider |

### ISO 6 Compliance Thresholds

**Particles (per m³):**

| Size | ISO 6 Limit |
|---|---|
| ≥0.3 µm | 102,000 |
| ≥0.5 µm | 35,200 |
| ≥1.0 µm | 8,320 |
| ≥5.0 µm | 293 |

**Pressure:**
- ≥25 Pa — COMPLIANT (positive pressure keeps particles out)
- 15–24 Pa — WARNING
- <15 Pa — CRITICAL

---

## Pick-and-Place Sequence

The firmware runs a 7-step motion sequence triggered by HTTP commands. Two modes: **GRAB** (gripper closes to pick up) and **RETURN** (gripper opens to drop off).

| Step | Action | Hardware |
|---|---|---|
| 1 | Door opens | Linear actuator (relay) |
| 2 | BLDC motor moves carriage forward | BLDC motor (PWM + direction + enable) |
| 3 | Arm extends | Vevor OK628 linear actuator (relay) |
| 4 | Gripper actuates | HS311 servo (0° open, 45° closed) |
| 5 | Arm retracts | Vevor OK628 linear actuator (relay) |
| 6 | BLDC motor returns carriage | BLDC motor (reverse direction) |
| 7 | Door closes | Linear actuator (relay) |

An **E-Stop** can halt the sequence at any point — the firmware checks for the E-Stop flag every 5 ms during motion.

---

## Dashboard (`dashboard.html`)

A single HTML file that runs entirely in the browser — no installation, no backend server.

**Telemetry panel** (USB Serial):
- Live temperature, humidity, particle counts, differential pressure
- 60-point rolling history charts (Chart.js)
- ISO 6 compliance traffic light and risk gauge
- Time sync from browser to Arduino clock

**Control panel** (WiFi HTTP):
- Connect to the R4's WiFi AP, then access `http://192.168.1.1`
- GRAB, RETURN, E-STOP buttons
- Keyboard shortcuts: **G** = Grab, **R** = Return, **Esc** = E-Stop
- LED on/off toggle
- Real-time sequence status and E-Stop indicator

**Browser compatibility:** Chrome or Edge required (Web Serial API).

---

## WiFi Access Point

The R4 creates its own WiFi network — connect your laptop to it directly.

| Setting | Value |
|---|---|
| SSID | `ProjectISO` |
| Password | `Stags123` |
| IP Address | `192.168.1.1` |
| Port | 80 (HTTP) |

---

## HTTP Endpoints

| Endpoint | Action |
|---|---|
| `GET /grab` | Start GRAB sequence (gripper closes) |
| `GET /return` | Start RETURN sequence (gripper opens) |
| `GET /estop` | Emergency stop — halts all outputs immediately |
| `GET /clear` | Clear E-Stop latch |
| `GET /on` | Turn LED on (Pin 2) |
| `GET /off` | Turn LED off (Pin 2) |
| `GET /data` | Returns current sensor data as JSON |

**`/data` response example:**
```json
{
  "ok_sht": true,
  "temp": 23.45,
  "hum": 42.10,
  "pressure_pa": 27.30,
  "ok_pms": true,
  "n03": 850,
  "n05": 320,
  "n10": 45,
  "n25": 8,
  "n50": 1,
  "n100": 0,
  "estop": false,
  "frames": 142,
  "bytes": 4544
}
```

---

## Hardware Bill of Materials

| Component | Purpose |
|---|---|
| Arduino Uno R4 WiFi | Microcontroller + WiFi AP + HTTP server |
| Sensirion SHT31 | Temperature + humidity sensor |
| Plantower PMS7003 | Particle counter |
| NXP MPXV7002DP | Differential pressure sensor |
| 2:3 voltage divider (resistors) | Steps MPXV7002DP 5V output down for R4's 3.3V ADC |
| BLDC motor | Carriage drive (forward/reverse) |
| Linear actuator (12V DC) | Door open/close |
| Vevor OK628 (12V DC, 6-inch stroke) | Arm extend/retract |
| HS311 servo motor | Gripper (0° open, 45° closed) |
| 2-channel relay board x2 | Switches door and arm actuators |
| 12V DC power supply | Main power for actuators and motor |
| LM2596 buck converter (12V → 5V) | Logic power for R4, relays, servo |

---

## Wiring Reference

### Pin Map (Arduino Uno R4 WiFi)

```
Pin  Component         Function
D2   LED               Status indicator
D3   BLDC PWM          Motor speed (analogWrite)
D5   Relay 1 ch1       Door EXTEND (HIGH = open)
D6   Relay 1 ch2       Door RETRACT (HIGH = close)
D7   Relay 2 ch1       Arm EXTEND (HIGH = extend)
D8   Relay 2 ch2       Arm RETRACT (HIGH = retract)
D9   HS311 servo       Gripper PWM (0° = open, 45° = closed)
D10  BLDC DIR          Motor direction
D11  BLDC EN           Motor enable (LOW = enabled)
A0   MPXV7002DP        Differential pressure (via 2:3 divider)
SDA  SHT31             Temperature/humidity I2C data
SCL  SHT31             Temperature/humidity I2C clock
RX1  PMS7003 TX        Particle counter serial data (9600 baud)
```

### Power

```
12V DC Power Supply
  ├── 12V rail → BLDC motor, door actuator (via relays), arm actuator (via relays)
  └── LM2596 buck converter → 5V rail
        ├── Arduino R4 (5V pin)
        ├── Relay board VCC x2
        └── HS311 servo VCC
```

### Pressure Sensor

```
MPXV7002DP VOUT → 2:3 voltage divider → A0
  Port 1 (+) → tube running INSIDE the cleanroom chamber
  Port 2 (−) → open to ambient room air
```

---

## Tunable Constants

These are set at the top of `cleanroom_controller.ino`. Adjust for your specific hardware.

### Sequence Timings

| Constant | Default | Description |
|---|---|---|
| `DOOR_TIME1` | 22000 ms | Door close travel time |
| `DOOR_TIME2` | 18500 ms | Door open travel time |
| `RA_TIME` | 38000 ms | Arm extend/retract travel time |
| `BLDC_FWD` | 5500 ms | BLDC forward run time |
| `BLDC_REV` | 5750 ms | BLDC reverse run time |
| `RELAY_GAP` | 1000 ms | Pause between relay switches |

### Gripper Angles

| Constant | Default | Description |
|---|---|---|
| `GRIP_OPEN` | 0° | Servo angle for open position |
| `GRIP_CLOSED` | 45° | Servo angle for closed/grab position |

---

## Quick Start

### 1. Install the Arduino Board Package

Open Arduino IDE → **Tools → Board → Boards Manager** → search `Arduino UNO R4` → install **Arduino UNO R4 Boards**.

No external libraries are required — the firmware uses only built-in libraries (`WiFiS3`, `Servo`, `Wire`).

### 2. Upload the Firmware

1. Connect the R4 WiFi to your computer via USB
2. Open `cleanroom_controller.ino` in Arduino IDE
3. Select **Tools → Board → Arduino UNO R4 WiFi**
4. Select your port under **Tools → Port**
5. Click **Upload**
6. Open **Serial Monitor** at **115200 baud**

You should see:

```
---------------------------
  Cleanroom Controller R4
---------------------------
Network : ProjectISO
Password: Stags123
Open    : http://192.168.1.1
Endpoints: /on /off /grab /return /estop /clear
---------------------------
Ready.
```

### 3. Connect and Use the Dashboard

1. On your laptop, join the **ProjectISO** WiFi network (password: `Stags123`)
2. Open `dashboard.html` in **Chrome or Edge**
3. Click **Connect Serial** in the dashboard to start receiving live sensor data over USB
4. The control panel sends commands over WiFi to `http://192.168.1.1`

### 4. Calibrate

Run the GRAB sequence and observe the hardware. Adjust the timing constants in `cleanroom_controller.ino` until the door, carriage, arm, and gripper move correctly for your build. Re-upload after each change.

---

## Tech Stack

| Layer | Technology |
|---|---|
| Firmware | Arduino C++ (WiFiS3, Servo, Wire) |
| Dashboard | HTML / Tailwind CSS / Vanilla JavaScript |
| Charts | Chart.js — 60-point rolling history |
| Telemetry link | Web Serial API (USB, Chrome/Edge only) |
| Control link | HTTP fetch (WiFi AP, port 80) |
| CAD | SolidWorks |
| Standard | ISO 14644-1 Class 6 |

---

## Troubleshooting

| Symptom | Likely Cause | Fix |
|---|---|---|
| No sensor data in dashboard | Wrong COM port or not connected | Click Connect Serial, select the R4 port |
| `ok_pms: false` in Serial Monitor | PMS7003 warming up | Wait 10–15 seconds after power-on |
| Pressure reads wrong | Voltage divider mismatch or sensor not calibrated | Check divider resistor values, verify at 0 Pa |
| Can't connect to ProjectISO WiFi | R4 not powered or AP failed | Check Serial Monitor for error messages |
| GRAB/RETURN does nothing | E-Stop is active | Send `/clear` or press Clear in dashboard |
| Sequence stops mid-run | E-Stop triggered (Esc key or `/estop`) | Clear E-Stop and re-run |
| Relay clicks but actuator doesn't move | Wired to NC instead of NO terminal | Move wires to NO (Normally Open) on relay |
| BLDC motor doesn't spin | Enable pin not driven LOW | Check D11 wiring |
| Servo doesn't move | Wrong signal pin or no 5V power | Check D9 wiring and 5V rail from buck converter |
