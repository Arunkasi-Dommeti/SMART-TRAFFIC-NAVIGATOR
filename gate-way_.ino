#include <SPI.h>
#include <LoRa.h>

// ── LoRa SPI Pins (SX1278 — same mapping as Block 1) ─────────────────────
#define LORA_SCK   18
#define LORA_MISO  19
#define LORA_MOSI  23
#define LORA_CS     5
#define LORA_RST   14
#define LORA_DIO0   2

// ── UART to Tang Nano 9K FPGA ─────────────────────────────────────────────
#define FPGA_TX    17    // ESP32 GPIO17 TX → Tang Nano 9K pin 40 (UART RX)
#define FPGA_RX    16    // FPGA ACK input — HIGH when FPGA is in EMERGENCY state

// ── Status LEDs ───────────────────────────────────────────────────────────
#define LED_POWER   4    // HIGH on boot
#define LED_ACTIVE 13    // HIGH during active emergency

// ── Protocol Constants ────────────────────────────────────────────────────
#define CMD_EMERGENCY    0x31
#define CMD_NORMAL       0x30
#define XOR_SECRET_KEY   0x5A
#define LORA_PACKET_SIZE    4
#define LORA_SYNC_WORD   0x12

// ── Progressive Corridor — RSSI-Based Proximity Trigger ──────────────────
// Instead of static ETA (which requires GPS), we use RSSI as a distance proxy.
// Higher RSSI (less negative) = ambulance closer to this junction.
// Ambulance starts far → low RSSI → stay NORMAL (don't pre-clear this junction yet).
// Ambulance approaches → RSSI rises above threshold → trigger EMERGENCY.
// Ambulance passes    → RSSI drops + signal-lost timeout → revert to NORMAL.
//
// This creates a REAL moving green wave with zero extra hardware:
//   Junction A (closer)  triggers first  → GREEN
//   Junction B (farther) triggers later  → still RED until ambulance approaches
//
// Tune RSSI_TRIGGER_DBM for your demo board spacing.
// Typical SX1278 at 1m: ~-40 dBm. At 5m: ~-65 dBm. At 20m: ~-85 dBm.
#define RSSI_TRIGGER_DBM   -85   // dBm threshold — ambulance within ~15-20m of junction
#define RSSI_SAMPLES         4   // rolling average over N packets — smooths noise

// ── Ambulance ID Whitelist ────────────────────────────────────────────────
const uint16_t ID_WHITELIST[]  = { 0x0001, 0x0002, 0x0003 };
const int      WHITELIST_COUNT = sizeof(ID_WHITELIST) / sizeof(ID_WHITELIST[0]);

// ── Runtime State ─────────────────────────────────────────────────────────
bool          emergencyActive = false;
unsigned long lastValidPacket = 0;
unsigned long totalPackets    = 0;
unsigned long validPackets    = 0;

// RSSI rolling average state
int  rssiBuffer[RSSI_SAMPLES] = {-120, -120, -120, -120};  // init far-away values
int  rssiIdx    = 0;
int  rssiAvg    = -120;

// FPGA ACK tracking
bool fpgaConfirmed = false;

// ═══════════════════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, FPGA_RX, FPGA_TX);
  delay(1000);

  Serial.println(F("\n════════════════════════════════════════════════════"));
  Serial.println(F("  SMART TRAFFIC NAVIGATOR — GATEWAY (Block 2)"));
  Serial.println(F("  LoRa RX → Validate → UART → Tang Nano 9K FPGA"));
  Serial.println(F("  Aditya University ECE  |  2026"));
  Serial.println(F("════════════════════════════════════════════════════\n"));

  pinMode(LED_POWER,  OUTPUT);
  pinMode(LED_ACTIVE, OUTPUT);
  pinMode(FPGA_RX,    INPUT);   // ACK feedback from FPGA EMERGENCY output
  digitalWrite(LED_POWER,  HIGH);
  digitalWrite(LED_ACTIVE, LOW);

  setupLoRa();
  forwardToFPGA(CMD_NORMAL, 0x00, 0x00);  // boot: FPGA starts normal cycling

  Serial.println(F("✅ GATEWAY READY"));
  Serial.printf("📡 Listening on 433 MHz  | RSSI trigger: %d dBm\n\n", RSSI_TRIGGER_DBM);
}

// ═══════════════════════════════════════════════════════════════════════════
void loop() {
  int packetSize = LoRa.parsePacket();

  // ── Incoming LoRa packet ────────────────────────────────────────────────
  if (packetSize > 0) {
    totalPackets++;

    byte buf[LORA_PACKET_SIZE + 4];
    int bytesRead = 0;
    while (LoRa.available() && bytesRead < (int)sizeof(buf))
      buf[bytesRead++] = LoRa.read();
    while (LoRa.available()) LoRa.read();

    int rssi = LoRa.packetRssi();

    Serial.println(F("═══════════════════════════════════════════════════"));
    Serial.printf("📡 PKT #%lu | RSSI: %d dBm | Avg: %d dBm | Len: %d\n",
                  totalPackets, rssi, rssiAvg, bytesRead);

    if (bytesRead != LORA_PACKET_SIZE) {
      Serial.printf("   ⚠️  Expected %d bytes, got %d — discarded\n\n",
                    LORA_PACKET_SIZE, bytesRead);
      Serial.println(F("═══════════════════════════════════════════════════\n"));
      return;
    }

    Serial.printf("   Raw: [%02X %02X %02X %02X]\n",
                  buf[0], buf[1], buf[2], buf[3]);

    if (validatePacket(buf)) {
      validPackets++;
      lastValidPacket = millis();

      bool isEmergency = (buf[0] == CMD_EMERGENCY);
      uint16_t ambId   = ((uint16_t)buf[1] << 8) | buf[2];

      // ── Update RSSI rolling average ─────────────────────────────────
      rssiBuffer[rssiIdx] = rssi;
      rssiIdx = (rssiIdx + 1) % RSSI_SAMPLES;
      int sum = 0;
      for (int i = 0; i < RSSI_SAMPLES; i++) sum += rssiBuffer[i];
      rssiAvg = sum / RSSI_SAMPLES;

      // ── Progressive Corridor Decision ──────────────────────────────
      // EMERGENCY packet received AND ambulance RSSI above proximity threshold
      // → this junction is in the active corridor window → GREEN
      // EMERGENCY packet received BUT RSSI below threshold
      // → ambulance still too far → hold NORMAL (let closer junctions act first)
      bool withinProximity = (rssiAvg >= RSSI_TRIGGER_DBM);

      Serial.printf("   ✅ VALID | ID: 0x%04X | CMD: %s | Proximity: %s\n",
                    ambId,
                    isEmergency ? "EMERGENCY" : "NORMAL",
                    withinProximity ? "IN RANGE ✅" : "TOO FAR ⏳");

      if (isEmergency && withinProximity) {
        // Ambulance is close enough — activate this junction's green corridor
        forwardToFPGA(CMD_EMERGENCY, buf[1], buf[2]);
        emergencyActive = true;
        digitalWrite(LED_ACTIVE, HIGH);
        Serial.println(F("   🚨 CORRIDOR ACTIVE → Jn A GREEN | Jn B RED"));

      } else if (isEmergency && !withinProximity) {
        // Ambulance transmitting but still far — stay normal, wait for it to approach
        // Do NOT cancel an already-active emergency (ambulance may be mid-junction)
        if (!emergencyActive) {
          Serial.println(F("   ⏳ AMBULANCE APPROACHING — holding normal cycle"));
        } else {
          Serial.println(F("   🚨 CORRIDOR HELD — ambulance still in junction zone"));
        }

      } else {
        // CMD_NORMAL received (ambulance cancelled/cleared junction)
        forwardToFPGA(CMD_NORMAL, 0x00, 0x00);
        emergencyActive = false;
        digitalWrite(LED_ACTIVE, LOW);
        // Reset RSSI buffer so next approach starts clean
        for (int i = 0; i < RSSI_SAMPLES; i++) rssiBuffer[i] = -120;
        rssiAvg = -120;
        Serial.println(F("   🟢 NORMAL CYCLE RESUMED"));
      }

      // ── FPGA ACK Check ──────────────────────────────────────────────
      fpgaConfirmed = digitalRead(FPGA_RX);
      Serial.printf("   🔌 FPGA State: %s\n",
                    fpgaConfirmed ? "EMERGENCY ✅ (ACK confirmed)" : "NORMAL / transitioning");

    } else {
      Serial.println(F("   ❌ INVALID — ignored (spoof attempt or unknown unit)"));
    }

    Serial.println(F("═══════════════════════════════════════════════════\n"));
  }

  // ── Signal-lost timeout ─────────────────────────────────────────────────
  if (emergencyActive && (millis() - lastValidPacket > 10000UL)) {
    Serial.println(F("⚠️  Signal lost (10s) — sending NORMAL to FPGA"));
    forwardToFPGA(CMD_NORMAL, 0x00, 0x00);
    emergencyActive = false;
    digitalWrite(LED_ACTIVE, LOW);
    for (int i = 0; i < RSSI_SAMPLES; i++) rssiBuffer[i] = -120;
    rssiAvg = -120;
    Serial.println();
  }

  delay(10);
}

// ═══════════════════════════════════════════════════════════════════════════
void setupLoRa() {
  Serial.print(F("Initializing LoRa 433 MHz ... "));
  LoRa.setPins(LORA_CS, LORA_RST, LORA_DIO0);

  if (!LoRa.begin(433E6)) {
    Serial.println(F("❌ FAILED — check wiring (CS/RST/DIO0)"));
    while (true) {
      digitalWrite(LED_ACTIVE, !digitalRead(LED_ACTIVE));
      delay(200);
    }
  }

  LoRa.setSpreadingFactor(9);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(5);
  LoRa.enableCrc();
  LoRa.setSyncWord(LORA_SYNC_WORD);

  Serial.println(F("✅"));
  Serial.println(F("   SF9 | BW 125 kHz | CR 4/5 | CRC ON | Sync 0x12\n"));
}

// ═══════════════════════════════════════════════════════════════════════════
bool validatePacket(byte buf[]) {
  byte cmd  = buf[0];
  byte id_h = buf[1];
  byte id_l = buf[2];
  byte chk  = buf[3];

  if (cmd != CMD_EMERGENCY && cmd != CMD_NORMAL) {
    Serial.printf("   Reject: unknown CMD 0x%02X\n", cmd);
    return false;
  }

  byte expected_chk = cmd ^ id_h ^ id_l ^ XOR_SECRET_KEY;
  if (chk != expected_chk) {
    Serial.printf("   Reject: checksum 0x%02X ≠ expected 0x%02X\n", chk, expected_chk);
    return false;
  }

  uint16_t ambId = ((uint16_t)id_h << 8) | id_l;
  for (int i = 0; i < WHITELIST_COUNT; i++) {
    if (ID_WHITELIST[i] == ambId) return true;
  }

  Serial.printf("   Reject: ID 0x%04X not in whitelist\n", ambId);
  return false;
}

// ═══════════════════════════════════════════════════════════════════════════
void forwardToFPGA(byte cmd, byte id_h, byte id_l) {
  byte chk = cmd ^ id_h ^ id_l ^ XOR_SECRET_KEY;
  byte pkt[LORA_PACKET_SIZE] = { cmd, id_h, id_l, chk };

  Serial2.write(pkt, LORA_PACKET_SIZE);

  Serial.printf("   🔌 UART → FPGA: [%02X %02X %02X %02X]  (%s)\n",
                pkt[0], pkt[1], pkt[2], pkt[3],
                (cmd == CMD_EMERGENCY) ? "EMERGENCY" : "NORMAL");
}
