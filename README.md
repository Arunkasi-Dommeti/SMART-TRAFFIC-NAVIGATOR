<div align="center">

# 🚑 Smart Traffic Navigator
### IoT + FPGA + AI — Real-Time Ambulance Green Corridor System

[![EMT Interface](https://img.shields.io/badge/EMT%20Interface-Live%20Demo-brightgreen?style=for-the-badge)](https://arunkasi-dommeti.github.io/EMT-Interface/)
[![Hospital Dashboard](https://img.shields.io/badge/Hospital%20Dashboard-Live%20Demo-red?style=for-the-badge)](https://nandeeswari-7.github.io/Hospital-Dash-Board/)
[![FPGA](https://img.shields.io/badge/FPGA-Tang%20Nano%209K-blue?style=for-the-badge)](https://wiki.sipeed.com/hardware/en/tang/Tang-Nano-9K/Nano-9K.html)
[![LoRa](https://img.shields.io/badge/LoRa-433%20MHz-orange?style=for-the-badge)]()
[![License](https://img.shields.io/badge/License-All%20Rights%20Reserved-black?style=for-the-badge)]()

**Aditya University — ECE-1 — 2026**

</div>

---

## 📌 What Is This?

Ambulances in Indian cities lose **2–4 minutes per junction** at red lights. For cardiac arrest or stroke, that window is survival margin disappearing.

Smart Traffic Navigator creates an **automatic green corridor** at traffic junctions the moment an ambulance is dispatched — no centralized smart-city network backend required, no police coordination needed. A LoRa radio on the ambulance broadcasts a validated 4-byte packet with checksum and whitelist verification at 433 MHz. The junction gateway ESP32 receives it, validates it, and commands a Tang Nano 9K FPGA via UART. The FPGA FSM switches the ambulance lane to GREEN in 37 nanoseconds using parameterized cycle-based timing control at 27 MHz.

At the same time, the EMT sees a ranked list of nearby hospitals scored by specialization match, bed availability, and distance — all before clearing the first junction.

---

## 🎯 Project Scope

### ✅ In-Scope (Implemented)
* **Tang Nano 9K Verilog FSM:** Featuring UART RX, packet validator, parameterized cycle-based timing control, and hardware-level emergency traffic preemption.
* **Ambulance Hardware Module:** Using ESP32 firmware, validated 4-byte LoRa packet transmission with checksum and whitelist verification, battery-powered operation, and manual emergency fallback trigger.
* **Traffic Junction Gateway:** Using ESP32 firmware with LoRa reception, UART bridge to FPGA, 3-layer packet validation, and RSSI-based progressive green corridor proximity detection.
* **FastAPI Cloud Backend:** Supporting ambulance data ingestion, hospital ranking logic, REST endpoints, and WebSocket-based live ambulance status update infrastructure.
* **EMT Web Interface:** Supporting emergency case entry, hospital recommendation workflow, multilingual Telugu/English interaction, and first-aid guidance workflow with backend-assisted prompt-response support.
* **Hospital Dashboard Web Application:** Supporting live case monitoring, readiness workflow management, and emergency intake coordination via API Key authenticated patches.
* **Firebase Realtime Database Integration:** For synchronized emergency case propagation across ambulance, EMT, and hospital interfaces.
* **FPGA Verification Workflow:** Including behavioral simulation testbench coverage, synthesis, and bitstream generation for Tang Nano 9K deployment.

### ❌ Out-of-Scope (Not Implemented)
* Satellite communication fallback systems.
* Multi-ambulance arbitration for simultaneous junction conflicts.
* Adaptive 3-stage multi-distance corridor control using calibrated distance thresholds.
* Centralized city-wide traffic operations dashboard.
* Google Maps API integration with live traffic-aware ETA computation.
* Production cloud deployment and scalability orchestration.
* Native Android or iOS mobile applications.

---

## 🎥 Demo

> **Working Tabletop Physical Prototype** — 2 junctions, 8 LEDs, 52cm × 50cm board

| Component | Status |
|-----------|--------|
| 🚑 Ambulance LoRa TX | ✅ Working |
| 📡 Gateway LoRa RX + Validation | ✅ Working |
| 🔌 UART → FPGA | ✅ Working |
| 🟢 FPGA EMERGENCY State (37 ns) | ✅ Working |
| 🌐 EMT Web Interface | ✅ [Live](https://arunkasi-dommeti.github.io/EMT-Interface/) |
| 🏥 Hospital Dashboard | ✅ [Live](https://nandeeswari-7.github.io/Hospital-Dash-Board/) |
| 🤖 First-Aid Guidance Assistant | ✅ Working (Backend-assisted) |
| 🐍 Backend API & WebSockets | ✅ Working (FastAPI) |

---

## 🏗️ System Architecture

```text
┌──────────────────────────────────────────────────────────────────┐
│                        AMBULANCE (Block 1)                       │
│   Android App ──► ESP32 ──► SX1278 LoRa TX ─────────────────►   │
│   Firebase Stream ◄── EMT Web Interface                           │
└──────────────────────────────────────────────────────────────────┘
                               │  433 MHz LoRa
                               │  4-byte validated packet
                               ▼
┌──────────────────────────────────────────────────────────────────┐
│                     JUNCTION GATEWAY (Block 2)                   │
│   SX1278 LoRa RX ──► Validate (XOR + Whitelist) ──► UART TX     │
│                                │  9600 baud, GPIO17               │
│                                ▼                                  │
│                      Tang Nano 9K FPGA                            │
│   uart_rx.v ──► packet_validator.v ──► traffic_fsm.v             │
│   6 states: IDLE→S1_GREEN→S1_YELLOW→S2_GREEN→S2_YELLOW→EMRG     │
│   EMERGENCY: Jn A GREEN · Jn B RED · 30 seconds hold             │
│                                │  ACK pin (GPIO16)                │
│                                ▼                                  │
│                  Gateway reads FPGA confirmation                  │
└──────────────────────────────────────────────────────────────────┘
                               │  Firebase Realtime DB
                               ▼
┌──────────────────────────────────────────────────────────────────┐
│                         CLOUD BACKEND                             │
│   FastAPI ──► Hospital Ranker ──► EMT Interface + Dashboard       │
│   Score = (Specialization×0.50) + (Distance×0.35) + (Beds×0.15) │
└──────────────────────────────────────────────────────────────────┘```

---

## 📁 Repository Structure

```SMART-TRAFFIC-NAVIGATOR/
│
├── 📟  FIRMWARE
│   ├── ambulance-module.ino      Block 1 — ESP32 ambulance transmitter
│   └── gate-way.ino              Block 2 — ESP32 LoRa gateway + UART bridge
│
├── 🔲  FPGA VERILOG  (Tang Nano 9K — Gowin GW1NR-9, 27 MHz)
│   ├── uart_rx.v                 UART receiver — 8N1, 9600 baud, 2-FF metastability sync
│   ├── packet_validator.v        4-byte packet validator — XOR checksum + ID whitelist
│   ├── traffic_fsm.v             6-state traffic light FSM with EMERGENCY preemption
│   ├── traffic_top.v             Top module — instantiates all three above
│   ├── smart_traffic.cst         Pin constraints for Tang Nano 9K
│   └── tb_traffic_top.v          Simulation testbench (Gowin EDA)
│
├── 🌐  WEB INTERFACES
│   ├── EMT_interface.html        EMT field interface — emergency trigger + hospital selection
│   └── Hospital-dash-board.html  Hospital team dashboard — live case monitoring
│
├── 🐍  BACKEND
│   ├── main.py                   FastAPI server — hospital ranking API + WebSockets
│   ├── requirements.txt          Python dependencies
│   └── README.md                 Backend setup instructions
│
└── 🗄️  DATABASE
    └── data_base.json            Firebase Realtime DB schema + sample emergency cases
---

## ⚡ FPGA — The Core Innovation

Traffic light switching happens in **hardware**, not software. No OS, no interrupt latency, no thread scheduling.

| State | Junction A | Junction B | Duration @ 27 MHz |
|-------|-----------|-----------|-------------------|
| IDLE | All OFF | All OFF | 1 cycle = 37 ns |
| S1_GREEN | **GREEN** | RED | 405,000,000 cycles = 15 s |
| S1_YELLOW | **YELLOW** | RED | 54,000,000 cycles = 2 s |
| S2_GREEN | RED | **GREEN** | 405,000,000 cycles = 15 s |
| S2_YELLOW | RED | **YELLOW** | 54,000,000 cycles = 2 s |
| **EMERGENCY ⭐** | **GREEN** | RED | **810,000,000 cycles = 30 s** |

> EMERGENCY **preempts any active state in 1 clock cycle (37 ns).**
> Triggered only on a validated 4-byte LoRa packet — CMD + AMB_ID + XOR checksum + whitelist match.

### Progressive Green Corridor (RSSI-Based)

Each junction gateway independently uses **RSSI as a distance proxy** — no GPS required.


```
Ambulance far      →  RSSI below threshold  →  junction stays NORMAL
Ambulance close    →  RSSI above threshold  →  junction activates EMERGENCY ✅
Ambulance passed   →  RSSI drops + 10s timeout  →  junction reverts to NORMAL
```

With two junctions, this creates a **moving green wave** that travels with the ambulance. Junction A clears first; Junction B waits until the ambulance physically approaches its LoRa range.

---

## 🔒 Security Architecture

Every LoRa packet is validated at **two independent layers:**

```
Layer 1 — ESP32 Gateway:    CMD check  →  XOR checksum  →  ID whitelist
Layer 2 — FPGA Hardware:    CMD check  →  XOR checksum  →  ID whitelist
```

Even if the gateway firmware is compromised, the FPGA independently rejects unauthorized packets. An attacker would need to spoof both layers simultaneously.

**Packet format (4 bytes):**
```
Byte 0: CMD          0x31 (EMERGENCY) or 0x30 (NORMAL)
Byte 1: AMB_ID_H     High byte of 16-bit ambulance ID
Byte 2: AMB_ID_L     Low byte of 16-bit ambulance ID
Byte 3: CHECKSUM     CMD ^ ID_H ^ ID_L ^ 0x5A
```

---

## 🛠️ Hardware Setup

### Block 1 — Ambulance Module

| Component | Specification | Connection |
|-----------|--------------|-----------|
| ESP32 DevKit V1 | WROOM-32, 38-pin | Main controller |
| SX1278 LoRa | 433 MHz, 20 dBm | SCK→18, MISO→19, MOSI→23, NSS→5, RST→14, DIO0→2 |
| 18650 Li-ion × 2 | 3.7V, 2200mAh in parallel | Battery pack |
| XL6009 Boost | 3.7V → 5V | Powers ESP32 |
| AMS1117-3.3 | Dedicated LoRa rail | Prevents brownout at 120mA LoRa peak |
| TP4056 | Li-ion charger + protection | Safe charging |
| Status LEDs | Red, 220Ω | GPIO4 (power), GPIO13 (emergency) |

### Block 2 — Junction Gateway

| Component | Specification | Connection |
|-----------|--------------|-----------|
| ESP32 DevKit V1 | WROOM-32, 38-pin | Gateway controller |
| SX1278 LoRa | 433 MHz receiver | Same SPI mapping as Block 1 |
| Tang Nano 9K | GW1NR-9, 27 MHz, 8640 LUT4 | FPGA traffic controller |
| LEDs × 8 | R/Y/G/W × 2 junctions | Pins 25–32 on Tang Nano + 220Ω each |
| 10kΩ resistors × 2 | Pull-up on UART RX | Prevents false start-bits at boot |

### Tang Nano 9K — Pin Mapping

| Signal | Tang Nano Pin | Direction | Description |
|--------|--------------|-----------|-------------|
| clk | 52 | Input | 27 MHz onboard oscillator |
| rst_n | 4 | Input | S1 push button, active LOW |
| rx | 40 | Input | UART RX from ESP32 GPIO17 |
| jA_red | 25 | Output | Junction A — Red LED |
| jA_yellow | 26 | Output | Junction A — Yellow LED |
| jA_green | 27 | Output | Junction A — Green LED (ambulance lane) |
| jA_white | 28 | Output | Junction A — White LED (pedestrian) |
| jB_red | 29 | Output | Junction B — Red LED |
| jB_yellow | 30 | Output | Junction B — Yellow LED |
| jB_green | 31 | Output | Junction B — Green LED |
| jB_white | 32 | Output | Junction B — White LED |
| fpga_ack | 33 | Output | HIGH when in EMERGENCY → ESP32 GPIO16 |
| led_valid | 10 | Output | Onboard LED1 — valid packet received |
| led_invalid | 11 | Output | Onboard LED2 — spoof attempt detected |

> ⚠️ **Voltage Safety:** All Tang Nano 9K GPIO = 3.3V. Never apply 5V to any GPIO pin. Power via USB-C only.

---

## 🚀 Getting Started

### Prerequisites

- Arduino IDE 2.x with ESP32 board support
- Libraries: `LoRa` (Sandeep Mistry), `Firebase ESP Client` (Mobizt), `ArduinoJson` (Benoit Blanchon)
- Gowin EDA Suite (GowinIDE) for FPGA synthesis
- Python 3.9+ for backend

---

### Step 1 — Flash Ambulance Firmware

```bash
# 1. Open ambulance-module.ino in Arduino IDE
# 2. Update credentials at top of file (Set to your local configuration):
#      // TODO for Production: Move to EEPROM
#      #define WIFI_SSID      "your-wifi"
#      #define WIFI_PASSWORD  "your-password"
#      #define API_KEY        "your-firebase-api-key"
#      #define DATABASE_URL   "[https://your-project.firebaseio.com](https://your-project.firebaseio.com)"
# 3. Board: ESP32 Dev Module | Upload Speed: 921600
# 4. Flash to ambulance ESP32
```

---

### Step 2 — Flash Gateway Firmware

```bash
# 1. Open gate-way.ino in Arduino IDE
# 2. Tune proximity threshold for your demo board spacing:
#      #define RSSI_TRIGGER_DBM  -85   // default ~15-20m
#      #define RSSI_TRIGGER_DBM  -70   // for ~5m demo spacing
#      #define RSSI_TRIGGER_DBM  -60   // for ~1m demo spacing
# 3. Flash to gateway ESP32
# 4. Serial monitor at 115200 — watch RSSI Avg values to tune threshold
```

---

### Step 3 — Synthesize FPGA Bitstream

```bash
# Tool: Gowin EDA Suite (GowinIDE v1.9.9+)
# Device: GW1NR-LV9QN88PC6/I5

# 1. File → New Project → select device above
# 2. Add design sources:
#       uart_rx.v  |  packet_validator.v  |  traffic_fsm.v  |  traffic_top.v
# 3. Add constraint: smart_traffic.cst
# 4. Top module: traffic_top
# 5. Process → Synthesize → Place & Route → Generate Bitstream
# 6. Programmer → connect Tang Nano 9K via USB-C → Program Device

# Simulation only (no hardware needed):
# - Add tb_traffic_top.v as simulation source
# - In traffic_fsm.v: use `ifdef SIMULATION logic for test timings
# - Process → Behavioral Simulation → add signals to waveform
```

---

### Step 4 — Run Backend API

```bash
cd backend
pip install -r requirements.txt
uvicorn main:app --reload --port 8000

# Swagger UI: http://localhost:8000/docs
# Health check: http://localhost:8000/health
```

---

### Step 5 — Open Web Interfaces

```bash
# EMT Interface (live):
https://arunkasi-dommeti.github.io/EMT-Interface/

# Or open locally:
# Simply open EMT_interface.html in any browser — no server needed

# Hospital Dashboard:
# Open Hospital-dash-board.html in browser
```

---

## 📡 LoRa Configuration Reference

Both Block 1 and Block 2 must match **exactly:**

| Parameter | Value | Notes |
|-----------|-------|-------|
| Frequency | 433 MHz | ISM band, India |
| Spreading Factor | SF9 | Range vs. speed balance |
| Bandwidth | 125 kHz | Standard |
| Coding Rate | CR 4/5 | Error correction |
| CRC | Enabled | Packet integrity |
| Sync Word | 0x12 | Network isolation |
| TX Power | 20 dBm | Max for SX1278 |

---

## 🏥 Hospital Ranking Algorithm

```
Score = (Specialization Match × 0.50)
      + (Distance Score       × 0.35)
      + (Bed Availability     × 0.15)
```

- **Specialization (50%)** — emergency type must match hospital's registered specializations
- **Distance (35%)** — Haversine distance from ambulance GPS, normalized over 20 km
- **Beds (15%)** — current available beds, normalized to 0–1 scale

The backend is designed for low-latency hospital ranking responses. EMT sees hospital name, ETA, bed count, and match score — and can select and send pre-alert before clearing the first junction.

---

## 🔮 Roadmap (Future Enhancements)

| Phase | Feature | Status |
|-------|---------|--------|
| Phase 1 | Integration of Google Maps API for route-aware ETA estimation and predictive junction activation.| Planned |
| Phase 2 | Advanced bidirectional ambulance tracking and operator map visualization.| Planned |
| Phase 3 | Extending RSSI-based proximity detection into multi-stage corridor control with progressive visual alert and timing adaptation layers.| Planned |
| Phase 4 | Expanding the modular FPGA traffic architecture to coordinate multiple interconnected junction controllers. | Planned |
| Phase 5 | Native Android EMT application for improved offline capability and device-level integration.| Planned |
| Phase 6 | Centralized traffic operations dashboard for monitoring active emergency corridors across multiple junctions.| Planned |

---

## 📄 License

All rights reserved.

Unauthorized copying, modification, distribution, or deployment of this project or any of its components is strictly prohibited without prior written permission from the authors.

---

<div align="center">

**Smart Traffic Navigator** · VLSI Technology ·Techinical Hub· Aditya University 2026· All rights reserved.
**Team:** P.S.B.S.Varshith · D.ArunKasi · V.Nandeeswari · Y.Hasmitha · M.Varshitha · G.Krishna Swetha

*Every second counts. This system buys them back.*

</div>
