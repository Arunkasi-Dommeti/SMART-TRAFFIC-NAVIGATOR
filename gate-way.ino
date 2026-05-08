/*
 * GATEWAY MODULE - LoRa Receiver + UART to FPGA
 * Receives: AMB-001|1|17.385000|78.486700|45
 * Sends: UART commands to FPGA for 2 junction control
 */

#include <LoRa.h>

// LoRa Pins
#define LORA_SCK 18
#define LORA_MISO 19
#define LORA_MOSI 23
#define LORA_CS 5
#define LORA_RST 14
#define LORA_DIO0 2


// UART to FPGA
#define FPGA_TX 17    // GPIO17 → FPGA RX
#define FPGA_RX 16    // GPIO16 ← FPGA TX (not used but defined)

// LEDs
#define LED_POWER 4
#define LED_J1 13     // Junction 1 active indicator
#define LED_J2 12     // Junction 2 active indicator

// Junction Positions (GPS coordinates)
#define J1_LAT 17.3860    // Junction 1 at ~15cm
#define J1_LNG 78.4867
#define J2_LAT 17.3880    // Junction 2 at ~30cm
#define J2_LNG 78.4867

// Constants
#define EARTH_RADIUS 6371.0       // km
#define ACTIVATION_RANGE 0.015    // 15 meters (scaled for demo)
#define DEACTIVATION_RANGE 0.005  // 5 meters (ambulance passed)

// Global Variables
bool j1Active = false;
bool j2Active = false;
String ambulanceId = "";
float ambulanceLat = 0;
float ambulanceLng = 0;
int ambulanceSpeed = 0;
bool emergencyFlag = false;

unsigned long lastPacket = 0;
unsigned long packetCount = 0;

// SETUP
void setup() {
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, FPGA_RX, FPGA_TX);
  delay(1000);
  
  Serial.println("\n════════════════════════════════════════════════════");
  Serial.println("  GATEWAY - 2 JUNCTION TRAFFIC CONTROL");
  Serial.println("  LoRa Receiver + FPGA Interface");
  Serial.println("════════════════════════════════════════════════════\n");
  
  // LED Setup
  pinMode(LED_POWER, OUTPUT);
  pinMode(LED_J1, OUTPUT);
  pinMode(LED_J2, OUTPUT);
  
  digitalWrite(LED_POWER, HIGH);
  digitalWrite(LED_J1, LOW);
  digitalWrite(LED_J2, LOW);
  
  // Setup LoRa
  setupLoRa();
  
  // Initialize FPGA - both junctions normal mode
  sendToFPGA(1, 0, 0, 0);  // J1 normal
  sendToFPGA(2, 0, 0, 0);  // J2 normal
  
  Serial.println("Junction 1: " + String(J1_LAT, 6) + ", " + String(J1_LNG, 6));
  Serial.println("Junction 2: " + String(J2_LAT, 6) + ", " + String(J2_LNG, 6));
  Serial.println("\n✅ GATEWAY READY");
  Serial.println("📡 Listening for ambulance...\n");
}

// MAIN LOOP
void loop() {
  // Check for LoRa packets
  int packetSize = LoRa.parsePacket();
  
  if (packetSize) {
    String packet = "";
    while (LoRa.available()) {
      packet += (char)LoRa.read();
    }
    
    int rssi = LoRa.packetRssi();
    packetCount++;
    lastPacket = millis();
    
    Serial.println("═══════════════════════════════════════════════════");
    Serial.println("📡 PACKET #" + String(packetCount) + " | RSSI: " + String(rssi) + " dBm");
    Serial.println("═══════════════════════════════════════════════════");
    Serial.println("Data: " + packet);
    
    if (parsePacket(packet)) {
      processJunctions();
    }
    
    Serial.println("═══════════════════════════════════════════════════\n");
  }
  
  // Timeout check - deactivate if no packet for 10 seconds
  if (millis() - lastPacket > 10000 && (j1Active || j2Active)) {
    Serial.println("⚠️ Signal lost - deactivating junctions\n");
    deactivateJunction(1);
    deactivateJunction(2);
  }
  
  delay(10);
}

// LoRa Setup
void setupLoRa() {
  Serial.print("Initializing LoRa...");
  
  LoRa.setPins(LORA_CS, LORA_RST, LORA_DIO0);
  
  if (!LoRa.begin(433E6)) {
    Serial.println(" ❌ Failed!");
    while(1) {
      digitalWrite(LED_J1, !digitalRead(LED_J1));
      delay(200);
    }
  }
  
  LoRa.setSpreadingFactor(9);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(5);
  LoRa.enableCrc();
  
  Serial.println(" ✅");
  Serial.println("Frequency: 433 MHz");
  Serial.println("Spreading Factor: 9\n");
}

// Parse LoRa Packet
bool parsePacket(String packet) {
  // Format: AMB-001|1|17.385000|78.486700|45
  
  int pipe1 = packet.indexOf('|');
  int pipe2 = packet.indexOf('|', pipe1 + 1);
  int pipe3 = packet.indexOf('|', pipe2 + 1);
  int pipe4 = packet.indexOf('|', pipe3 + 1);
  
  if (pipe1 == -1 || pipe2 == -1 || pipe3 == -1 || pipe4 == -1) {
    Serial.println("⚠️ Invalid format\n");
    return false;
  }
  
  ambulanceId = packet.substring(0, pipe1);
  emergencyFlag = packet.substring(pipe1 + 1, pipe2) == "1";
  ambulanceLat = packet.substring(pipe2 + 1, pipe3).toFloat();
  ambulanceLng = packet.substring(pipe3 + 1, pipe4).toFloat();
  ambulanceSpeed = packet.substring(pipe4 + 1).toInt();
  
  Serial.println("Parsed:");
  Serial.println("  ID: " + ambulanceId);
  Serial.println("  Emergency: " + String(emergencyFlag ? "YES" : "NO"));
  Serial.printf("  Location: %.6f, %.6f\n", ambulanceLat, ambulanceLng);
  Serial.println("  Speed: " + String(ambulanceSpeed) + " km/h");
  
  return true;
}
// Process Junctions
void processJunctions() {
  if (!emergencyFlag) {
    Serial.println("ℹ️ No emergency - normal mode\n");
    deactivateJunction(1);
    deactivateJunction(2);
    return;
  }
  
  // Calculate distances
  float distJ1 = calculateDistance(ambulanceLat, ambulanceLng, J1_LAT, J1_LNG);
  float distJ2 = calculateDistance(ambulanceLat, ambulanceLng, J2_LAT, J2_LNG);
  
  Serial.println("\nDistances:");
  Serial.printf("  J1: %.4f km (%.1f m)\n", distJ1, distJ1 * 1000);
  Serial.printf("  J2: %.4f km (%.1f m)\n", distJ2, distJ2 * 1000);
  
  // Junction 1 Logic
  if (distJ1 <= ACTIVATION_RANGE && distJ1 > DEACTIVATION_RANGE) {
    activateJunction(1, distJ1);
  } else if (distJ1 <= DEACTIVATION_RANGE && j1Active) {
    Serial.println("✅ J1: Ambulance passed\n");
    deactivateJunction(1);
  } else if (distJ1 > ACTIVATION_RANGE && j1Active) {
    deactivateJunction(1);
  }
  
  // Junction 2 Logic
  if (distJ2 <= ACTIVATION_RANGE && distJ2 > DEACTIVATION_RANGE) {
    activateJunction(2, distJ2);
  } else if (distJ2 <= DEACTIVATION_RANGE && j2Active) {
    Serial.println("✅ J2: Ambulance passed\n");
    deactivateJunction(2);
  } else if (distJ2 > ACTIVATION_RANGE && j2Active) {
    deactivateJunction(2);
  }
}

// Activate Junction
void activateJunction(int junctionId, float distance) {
  bool *active = (junctionId == 1) ? &j1Active : &j2Active;
  int led = (junctionId == 1) ? LED_J1 : LED_J2;
  
  if (!*active) {
    Serial.println("\n🚨 J" + String(junctionId) + " ACTIVATED - GREEN CORRIDOR 🚨");
    *active = true;
    digitalWrite(led, HIGH);
  }
  
  // Calculate ETA
  int eta = 0;
  if (ambulanceSpeed > 0) {
    eta = (distance / ambulanceSpeed) * 3600;  // Convert to seconds
  } else {
    eta = 30;  // Default 30 seconds
  }
  
  if (eta < 10) eta = 10;   // Minimum 10 seconds
  if (eta > 120) eta = 120; // Maximum 2 minutes
  
  Serial.println("  Distance: " + String(distance * 1000, 1) + " m");
  Serial.println("  Speed: " + String(ambulanceSpeed) + " km/h");
  Serial.println("  ETA: " + String(eta) + " seconds");
  
  // Send to FPGA
  int distanceMeters = (int)(distance * 1000);
  sendToFPGA(junctionId, 1, distanceMeters, eta);
  
  Serial.println("  → FPGA: Priority mode");
}

// Deactivate Junction
void deactivateJunction(int junctionId) {
  bool *active = (junctionId == 1) ? &j1Active : &j2Active;
  int led = (junctionId == 1) ? LED_J1 : LED_J2;
  
  if (*active) {
    Serial.println("🟢 J" + String(junctionId) + " DEACTIVATED - NORMAL MODE");
    *active = false;
    digitalWrite(led, LOW);
    
    sendToFPGA(junctionId, 0, 0, 0);
    Serial.println("  → FPGA: Normal cycle\n");
  }
}

// Calculate Distance (Haversine)
float calculateDistance(float lat1, float lon1, float lat2, float lon2) {
  // Convert to radians using Arduino's built-in radians() macro
  float dLat = radians(lat2 - lat1);
  float dLon = radians(lon2 - lon1);
  lat1 = radians(lat1);
  lat2 = radians(lat2);
  
  float a = sin(dLat/2) * sin(dLat/2) +
            sin(dLon/2) * sin(dLon/2) * cos(lat1) * cos(lat2);
  float c = 2 * atan2(sqrt(a), sqrt(1-a));
  
  return EARTH_RADIUS * c;
}




// Send to FPGA via UART
void sendToFPGA(byte junctionId, byte emergencyFlag, int distance, int eta) {
  // Packet format: [0xFF][Junction][Emergency][Dist_H][Dist_L][ETA_H][ETA_L][0xFE]
  
  byte packet[8];
  packet[0] = 0xFF;                     // Start
  packet[1] = junctionId;               // 1 or 2
  packet[2] = emergencyFlag;            // 1=emergency, 0=normal
  packet[3] = (distance >> 8) & 0xFF;   // Distance high
  packet[4] = distance & 0xFF;          // Distance low
  packet[5] = (eta >> 8) & 0xFF;        // ETA high
  packet[6] = eta & 0xFF;               // ETA low
  packet[7] = 0xFE;                     // End
  
  Serial2.write(packet, 8);
  
  Serial.print("🔌 UART TX: [");
  for (int i = 0; i < 8; i++) {
    Serial.print("0x");
    if (packet[i] < 16) Serial.print("0");
    Serial.print(packet[i], HEX);
    if (i < 7) Serial.print(" ");
  }
  Serial.println("]");
}