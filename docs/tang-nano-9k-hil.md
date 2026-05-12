# Tang Nano 9K Hardware-in-the-Loop Test Workflow

This workflow verifies the competition demo path end to end:

1. Firebase emergency trigger reaches the ambulance ESP32 stream listener.
2. Ambulance ESP32 transmits the 4-byte LoRa emergency packet.
3. Gateway ESP32 validates LoRa and forwards the packet over UART.
4. Tang Nano 9K packet validator accepts the packet and the FSM enters EMERGENCY.
5. Gateway reads the FPGA ACK pin and reports the corridor state.

## Required Hardware

| Item | Role |
| --- | --- |
| ESP32 running `ambulance-module.ino` | Firebase stream listener, NEO-6M GPS reader, LoRa transmitter |
| NEO-6M GPS module | Real ambulance latitude/longitude source |
| ESP32 running `gate-way_.ino` | LoRa receiver, packet validator, UART bridge |
| SX1278 LoRa modules, 433 MHz | Ambulance-to-gateway radio link |
| Tang Nano 9K | UART RX, packet validator, traffic FSM |
| LEDs or demo board | Junction state visibility |
| Firebase Realtime Database project | Emergency trigger path |

## Wiring Checklist

| Signal | Source | Destination |
| --- | --- | --- |
| Ambulance GPS TX | NEO-6M TX | Ambulance ESP32 GPIO16 |
| Ambulance GPS RX | NEO-6M RX | Ambulance ESP32 GPIO17 |
| Gateway UART TX | Gateway ESP32 GPIO17 | Tang Nano 9K pin 40 (`rx`) |
| FPGA ACK | Tang Nano 9K pin 33 (`fpga_ack`) | Gateway ESP32 GPIO16 |
| Ground | ESP32 / LoRa / Tang Nano | Common GND |

All Tang Nano 9K GPIO signals must stay at 3.3V logic.

## Simulation Gate Before Hardware

Run the Verilog simulation before flashing hardware.

1. Open the Gowin project for device `GW1NR-LV9QN88PC6/I5`.
2. Add `uart_rx.v`, `packet_validator.v`, `traffic_fsm.v`, `traffic_top.v`, and `tb_traffic_top.v`.
3. Ensure `tb_traffic_top.v` defines `SIMULATION`.
4. Run behavioral simulation.
5. Watch `rx`, `valid_pkt`, `invalid_attempt`, `emergency`, `fpga_ack`, and junction LED outputs.

Expected simulation results:

| Test | Expected Result |
| --- | --- |
| Valid AMB-001 emergency packet `[31 00 01 6A]` | `valid_pkt` pulses, `fpga_ack` goes HIGH, Junction A GREEN |
| Spoofed checksum | `invalid_attempt` pulses, `fpga_ack` remains unchanged |
| Unknown ID | `invalid_attempt` pulses |
| Valid AMB-001 normal packet `[30 00 01 6B]` | FSM exits EMERGENCY and resumes normal cycle |

## HIL Procedure

1. Flash `ambulance-module.ino` to the ambulance ESP32.
2. Confirm the NEO-6M serial log reports a fresh fix before recording a judged run.
3. Flash `gate-way_.ino` to the gateway ESP32.
4. Program the Tang Nano 9K bitstream built from `traffic_top.v`.
5. Open serial monitors for both ESP32 boards at 115200 baud.
6. In Firebase, set `/ambulances/AMB-001/emergency_id` to a valid case ID such as `EMG-2026-0001`.
7. Ensure `/active_emergencies/EMG-2026-0001` exists so the ambulance can fetch case details.
8. Move the ambulance LoRa module inside the gateway RSSI threshold or temporarily tune `RSSI_TRIGGER_DBM` for tabletop spacing.

## Expected Evidence

Capture these logs/screenshots for the competition submission:

| Evidence | Pass Condition |
| --- | --- |
| Ambulance serial monitor | Firebase event received, emergency active, NEO-6M location published, LoRa TX packet shown |
| Gateway serial monitor | Valid packet, ID `0x0001`, checksum accepted, UART packet forwarded |
| Tang Nano LEDs | Emergency lane turns GREEN and ACK remains HIGH during emergency |
| Firebase console | `location.gps_fix = true`, `location.source = neo-6m`, real `lat` and `lng` fields update |
| Normal/cancel test | Gateway forwards NORMAL with active ambulance ID, FPGA exits EMERGENCY |

## Negative Tests

Run these before the final demo:

| Case | Action | Expected Result |
| --- | --- | --- |
| Bad checksum | Send `[31 00 01 00]` via UART testbench or modified gateway packet | FPGA raises `invalid_attempt`; no emergency |
| Unknown ambulance | Send `[31 A1 01 CA]` | Gateway or FPGA rejects the packet |
| GPS not fixed | Cover or disconnect GPS before emergency trigger | Firebase marks `gps_fix = false` and does not publish fallback lat/lng |
| Signal lost | Stop ambulance LoRa after emergency | Gateway timeout sends NORMAL with the last valid ambulance ID |

## Pytest Regression Coverage

`test_integration_paths.py` provides source-level integration checks for:

- Ambulance, gateway, and FPGA packet constants.
- AMB-001 emergency packet checksum and whitelist acceptance.
- Gateway NORMAL/cancel forwarding with the last valid ambulance ID.
- Firebase stream trigger contract in the ambulance firmware.
- EMT interface AI proxy usage with public config allowlisting.
