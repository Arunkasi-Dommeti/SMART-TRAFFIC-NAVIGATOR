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
#define FPGA_TX    17    // ESP32 GPIO17 TX → Tang Nano 9K pin 17 (UART RX)
#define FPGA_RX    16    // unused — FPGA does not send back to ESP32

// ── Status LEDs ───────────────────────────────────────────────────────────
#define LED_POWER   4    // HIGH on boot
#define LED_ACTIVE 13    // HIGH during active emergency

// ── Protocol Constants ────────────────────────────────────────────────────
#define CMD_EMERGENCY    0x31    // FPGA → EMERGENCY state (Jn A GREEN, Jn B RED, 30 s)
#define CMD_NORMAL       0x30    // FPGA → normal S1_GREEN → S1_YELLOW → ... cycle
#define XOR_SECRET_KEY   0x5A    // must match Block 1 ambulance firmware
#define LORA_PACKET_SIZE    4    // fixed 4-byte secured packet, no more no less
#define LORA_SYNC_WORD   0x12    // must match Block 1

// ── Ambulance ID Whitelist ────────────────────────────────────────────────
//   16-bit IDs: (AMB_ID_H << 8) | AMB_ID_L
//   Add registered ambulance IDs here before deployment
const uint16_t ID_WHITELIST[]  = { 0x0001, 0x0002, 0x0003 };
const int      WHITELIST_COUNT = sizeof(ID_WHITELIST) / sizeof(ID_WHITELIST[0]);

// ── Runtime State ─────────────────────────────────────────────────────────
bool          emergencyActive = false;
unsigned long lastValidPacket = 0;    // millis() of last accepted packet
unsigned long totalPackets    = 0;    // total LoRa packets received
unsigned long validPackets    = 0;    // packets that passed validation

// ═══════════════════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  // Serial2: TX=GPIO17, RX=GPIO16 (RX not connected but declared for API)
  Serial2.begin(9600, SERIAL_8N1, FPGA_RX, FPGA_TX);
  delay(1000);

  Serial.println(F("\n════════════════════════════════════════════════════"));
  Serial.println(F("  SMART TRAFFIC NAVIGATOR — GATEWAY (Block 2)"));
  Serial.println(F("  LoRa RX → Validate → UART → Tang Nano 9K FPGA"));
  Serial.println(F("  Aditya University ECE  |  2026"));
  Serial.println(F("════════════════════════════════════════════════════\n"));

  pinMode(LED_POWER,  OUTPUT);
  pinMode(LED_ACTIVE, OUTPUT);
  digitalWrite(LED_POWER,  HIGH);
  digitalWrite(LED_ACTIVE, LOW);

  // Init LoRa receiver
  setupLoRa();

  // On boot, tell FPGA to start normal cycling
  // CMD_NORMAL with ID=0x0000, checksum = 0x30 ^ 0x00 ^ 0x00 ^ 0x5A = 0x6A
  forwardToFPGA(CMD_NORMAL, 0x00, 0x00);

  Serial.println(F("✅ GATEWAY READY"));
  Serial.println(F("📡 Listening on 433 MHz  (SF9 | BW 125 kHz | CR 4/5 | CRC)\n"));
}

// ═══════════════════════════════════════════════════════════════════════════
void loop() {
  int packetSize = LoRa.parsePacket();

  // ── Incoming LoRa packet ────────────────────────────────────────────────
  if (packetSize > 0) {
    totalPackets++;

    // Read all available bytes (should be exactly LORA_PACKET_SIZE)
    byte buf[LORA_PACKET_SIZE + 4];  // +4 guard against oversized junk
    int bytesRead = 0;
    while (LoRa.available() && bytesRead < (int)sizeof(buf)) {
      buf[bytesRead++] = LoRa.read();
    }
    // Drain any extra bytes we didn't read
    while (LoRa.available()) LoRa.read();

    int rssi = LoRa.packetRssi();

    Serial.println(F("═══════════════════════════════════════════════════"));
    Serial.printf("📡 PKT #%lu | RSSI: %d dBm | Length: %d bytes\n",
                  totalPackets, rssi, bytesRead);

    // Wrong length → discard immediately
    if (bytesRead != LORA_PACKET_SIZE) {
      Serial.printf("   ⚠️  Expected %d bytes, got %d — discarded\n\n",
                    LORA_PACKET_SIZE, bytesRead);
      Serial.println(F("═══════════════════════════════════════════════════\n"));
      return;
    }

    Serial.printf("   Raw: [%02X %02X %02X %02X]\n",
                  buf[0], buf[1], buf[2], buf[3]);

    // Validate packet (checksum + whitelist)
    if (validatePacket(buf)) {
      validPackets++;
      lastValidPacket = millis();

      bool isEmergency  = (buf[0] == CMD_EMERGENCY);
      uint16_t ambId    = ((uint16_t)buf[1] << 8) | buf[2];

      Serial.printf("   ✅ VALID | ID: 0x%04X | CMD: %s\n",
                    ambId, isEmergency ? "EMERGENCY (0x31)" : "NORMAL (0x30)");

      // ── THE FIX: forward raw 4-byte packet to FPGA ──────────────────
      //   FPGA packet_validator.v checks byte[0] == 0x31/0x30 to act
      //   Old code sent [0xFF]... which always failed the FPGA validator
      forwardToFPGA(buf[0], buf[1], buf[2]);

      if (isEmergency) {
        emergencyActive = true;
        digitalWrite(LED_ACTIVE, HIGH);
        Serial.println(F("   🚨 FPGA EMERGENCY → Jn A GREEN | Jn B RED | 30 sec hold"));
      } else {
        emergencyActive = false;
        digitalWrite(LED_ACTIVE, LOW);
        Serial.println(F("   🟢 FPGA NORMAL → S1_GREEN cycling resumed"));
      }

    } else {
      // Invalid — spoofed packet or unknown ambulance
      Serial.println(F("   ❌ INVALID — ignored (spoof attempt or unknown unit)"));
    }

    Serial.println(F("═══════════════════════════════════════════════════\n"));
  }

  // ── Signal-lost timeout ────────────────────────────────────────────────
  // If emergency was active but no valid packet arrives for 10 s, cancel it
  if (emergencyActive && (millis() - lastValidPacket > 10000UL)) {
    Serial.println(F("⚠️  Signal lost (10 s timeout) — sending NORMAL to FPGA"));
    forwardToFPGA(CMD_NORMAL, 0x00, 0x00);
    emergencyActive = false;
    digitalWrite(LED_ACTIVE, LOW);
    Serial.println();
  }

  delay(10);
}

// ═══════════════════════════════════════════════════════════════════════════
//  setupLoRa() — configure SX1278 as receiver
//  Parameters MUST match Block 1 ambulance transmitter exactly
// ═══════════════════════════════════════════════════════════════════════════
void setupLoRa() {
  Serial.print(F("Initializing LoRa 433 MHz ... "));
  LoRa.setPins(LORA_CS, LORA_RST, LORA_DIO0);

  if (!LoRa.begin(433E6)) {
    Serial.println(F("❌ FAILED — check wiring (CS/RST/DIO0)"));
    // Blink LED_ACTIVE rapidly to signal hardware fault
    while (true) {
      digitalWrite(LED_ACTIVE, !digitalRead(LED_ACTIVE));
      delay(200);
    }
  }

  LoRa.setSpreadingFactor(9);       // SF9  — must match Block 1
  LoRa.setSignalBandwidth(125E3);   // 125 kHz — must match Block 1
  LoRa.setCodingRate4(5);           // CR 4/5 — must match Block 1
  LoRa.enableCrc();                 // CRC ON — must match Block 1
  LoRa.setSyncWord(LORA_SYNC_WORD); // 0x12 — must match Block 1

  Serial.println(F("✅"));
  Serial.println(F("   SF9 | BW 125 kHz | CR 4/5 | CRC ON | Sync 0x12\n"));
}

// ═══════════════════════════════════════════════════════════════════════════
bool validatePacket(byte buf[]) {
  byte cmd  = buf[0];
  byte id_h = buf[1];
  byte id_l = buf[2];
  byte chk  = buf[3];

  // Layer 0: CMD must be 0x31 or 0x30
  if (cmd != CMD_EMERGENCY && cmd != CMD_NORMAL) {
    Serial.printf("   Reject: unknown CMD 0x%02X\n", cmd);
    return false;
  }

  // Layer 1: XOR checksum
  byte expected_chk = cmd ^ id_h ^ id_l ^ XOR_SECRET_KEY;
  if (chk != expected_chk) {
    Serial.printf("   Reject: checksum 0x%02X ≠ expected 0x%02X\n",
                  chk, expected_chk);
    return false;
  }

  // Layer 2: Whitelist
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
