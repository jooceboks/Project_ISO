# Project ISO — Tabletop ISO Class 6 Cleanroom Chamber

A benchtop cleanroom system designed to replicate **ISO 14644-1 Class 6** conditions in a compact academic setup. The chamber combines environmental monitoring, automated motion control, and a browser-based control interface to support lithography experiments, contamination-control studies, and mechatronic testing.

Instead of relying on a full-scale cleanroom, this project brings controlled airflow and particle monitoring to a tabletop form factor while remaining accessible for research and prototyping.

The project combines two disciplines:

- **Mechanical Engineering** — CAD, chamber fabrication, airflow/HVAC design, actuator integration
- **Computer Science** — embedded systems, telemetry pipelines, web communication, and real-time visualization

---

# System Architecture

The system is split across two microcontrollers so environmental sensing and motion control can run independently without blocking each other.

```text
              ┌──────────────────────────────────────┐
              │       HTML5 Operator Console         │
              │          (Web Dashboard)             │
              └───────────┬──────────────────┬───────┘
                          │                  │
        USB Serial Link   │                  │  Wi-Fi Link
      (Web Serial API)    │                  │  (WebSockets)
                          ▼                  ▼
          ┌──────────────────┐    ┌────────────────────┐
          │  Arduino Uno R3  │    │ Arduino Uno R4 WiFi│
          │ (Telemetry Node) │    │   (Control Node)   │
          └────────┬─────────┘    └─────────┬──────────┘
                   │                        │
                   ├─► SHT31 Sensor         ├─► NEMA 17 Stepper
                   ├─► PMS7003 Sensor       ├─► Linear Actuators
                   └─► MPXV7002DP           └─► Servo Gripper
```

---

# Telemetry Subsystem (Arduino Uno R3)

The Uno R3 is dedicated to environmental monitoring inside the chamber. Sensor data is streamed directly to the dashboard over USB serial using the browser’s Web Serial API.

## Responsibilities

- Monitor **temperature** and **humidity** using the SHT31
- Read particulate data from the PMS7003 particle counter
- Measure positive chamber pressure using the MPXV7002DP differential pressure sensor
- Continuously stream formatted telemetry packets to the dashboard

## Environmental Metrics

The system tracks:

- Temperature
- Relative humidity
- Differential pressure (Pa)
- Particle concentrations across 6 particle-size bins:
  - 0.3 µm
  - 0.5 µm
  - 1.0 µm
  - 2.5 µm
  - 5.0 µm
  - 10 µm

---

# Motion Control Subsystem (Arduino Uno R4 WiFi)

The Uno R4 WiFi handles all physical automation and hosts a local wireless access point for remote control.

The controller runs a timed 7-step pick-and-place sequence used for transferring samples into and out of the chamber.

## Automated Sequence

```text
Door Open
→ Carriage Advance
→ Arm Extend
→ Gripper Actuation
→ Arm Retract
→ Carriage Return
→ Door Close
```

## Safety System

The controller continuously checks for:

- Emergency stop requests
- Abort commands
- Timeout conditions

If triggered, all actuators and motors are halted immediately.

---

# Web Dashboard

The frontend is a standalone HTML/CSS/JavaScript dashboard that acts as the operator interface.

It supports both:

- **USB Serial communication** through the Web Serial API
- **Wireless communication** over WebSockets

## Dashboard Features

- Real-time environmental telemetry
- ISO Class 6 compliance monitoring
- Live particle-count visualization
- Pressure status indicators
- System event logging
- Motion-control commands
- Keyboard shortcuts for rapid operation

## Keybindings

| Key | Action |
|---|---|
| `Esc` | Emergency Stop |
| `G` | Run GRAB sequence |
| `R` | Run RETURN sequence |

---

# Hardware Overview

## Telemetry System (Uno R3)

| Component | Purpose |
|---|---|
| **Sensirion SHT31** | Temperature & humidity sensing |
| **Plantower PMS7003** | Laser particle counter |
| **MPXV7002DP** | Differential pressure sensing |

## Motion Control System (Uno R4 WiFi)

| Component | Purpose |
|---|---|
| **NEMA 17 Stepper + A4988** | Linear carriage movement |
| **Linear Actuators** | Door and arm actuation |
| **HS311 Servo** | Gripper open/close control |
| **Relay Modules** | Directional actuator switching |

---

# ISO 6 Compliance Targets

The chamber is designed around ISO 14644-1 Class 6 particle limits.

## Primary Thresholds

- ≤ 102,000 particles/m³ at ≥ 0.3 µm
- ≤ 35,200 particles/m³ at ≥ 0.5 µm

## Pressure Status

| Pressure | Status |
|---|---|
| ≥ 25 Pa | Compliant |
| 15–24 Pa | Warning |
| < 15 Pa | Critical |

Positive pressure helps prevent external particle contamination from entering the chamber.

---

# Software Configuration

## Motion Parameters

- `BALLSCREW_STEPS = 400`
- `STEPPER_MAX_SPEED = 800 steps/sec`
- `ACCELERATION = 400 steps/sec²`

## Timeout Limits

- Door travel timeout: `3000 ms`
- Ball screw traverse timeout: `2000 ms`
- Arm actuator timeout: `2500 ms`

---

# Getting Started

## Requirements

- Arduino IDE 2.0+
- Arduino Uno R3
- Arduino Uno R4 WiFi
- Chromium-based browser (Chrome or Edge recommended)

# Project Goals

Project ISO was built to explore how cleanroom-grade environmental control can be miniaturized into a low-cost academic platform.

The system serves as a testbed for:

- contamination-control experiments
- lithography workflows
- embedded control systems
- real-time telemetry pipelines
- automation and mechatronics research
- browser-based industrial interfaces
