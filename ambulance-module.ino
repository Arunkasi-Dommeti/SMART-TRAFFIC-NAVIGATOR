#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <LoRa.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"
#include <TinyGPS++.h>
#include <HardwareSerial.h>

#define WIFI_SSID "YOUR_SSID_HERE" 
#define WIFI_PASSWORD "YOUR_PASSWORD_HERE"

// Firebase Configuration
#define API_KEY "YOUR_FIREBASE_API_KEY_HERE"
#define DATABASE_URL "https://your-project.firebaseio.com"

// Ambulance Configuration
#define AMBULANCE_ID "AMB-001"

// LoRa Pins
#define LORA_SCK 18
#define LORA_MISO 19
#define LORA_MOSI 23
#define LORA_CS 5
#define LORA_RST 14
#define LORA_DIO0 2

// LED Pins
#define LED_POWER 4
#define LED_EMERGENCY 13


#define MANUAL_TRIGGER_BTN  0

// Protocol Constants 
#define AMBULANCE_NUM_ID 0x0001  // Binary ID to match gateway whitelist (0x0001)
#define CMD_EMERGENCY    0x31
#define CMD_NORMAL       0x30
#define XOR_SECRET_KEY   0x5A
#define LORA_SYNC_WORD   0x12

//  GPS Configuration 
#define GPS_RX_PIN 16 
#define GPS_TX_PIN 17 
#define GPS_BAUD 9600

TinyGPSPlus gps;
HardwareSerial gpsSerial(2); // UART2

// Global Object
FirebaseData fbdo;
FirebaseData stream;
FirebaseAuth auth;
FirebaseConfig config;

bool isEmergencyActive = false;
String currentEmergencyId = "";
unsigned long lastUpdate = 0;
const unsigned long UPDATE_INTERVAL = 1000;
const unsigned long GPS_MAX_FIX_AGE_MS = 5000;

// LoRa-Only Fallback State 
bool          wifiAvailable  = false;
// uccessful WiFi connect
bool          loraOnlyMode   = false;
// Activate when WiFi fails 
bool          btnLastState   = HIGH;
unsigned long btnPressTime   = 0;
const unsigned long DEBOUNCE_MS = 50;


float currentLat = 0.0;
float currentLng = 0.0;
int currentSpeed = 0;
bool hasGpsFix = false;
unsigned long lastGpsFixAt = 0;
uint32_t currentSatellites = 0;
float currentHdop = 0.0;

// SETUP
void setup() {
  Serial.begin(115200);
  delay(1000);
 
  Serial.println("\n\n════════════════════════════════════════════════════");
  Serial.println("  SMART AMBULANCE - " + String(AMBULANCE_ID));
  Serial.println("  Firebase + LoRa + NEO-6M Mode");
  Serial.println("════════════════════════════════════════════════════\n");

  // LED Setup
  pinMode(LED_POWER, OUTPUT);
  pinMode(LED_EMERGENCY, OUTPUT);
  digitalWrite(LED_POWER, LOW);
  digitalWrite(LED_EMERGENCY, LOW);

  
  pinMode(MANUAL_TRIGGER_BTN, INPUT_PULLUP);
 
  // Connect WiFi
  connectWiFi();

  // Setup Firebase
  setupFirebase();

  //  Determine operating mode 
  if (!wifiAvailable) {
    loraOnlyMode = true;
    Serial.println("╔══════════════════════════════════════════════════╗");
    Serial.println("║  ⚠️  LoRa-Only Fallback Mode ACTIVE               ║");
    Serial.println("║  WiFi unavailable — Firebase logging disabled     ║");
    Serial.println("║  Press BOOT button (GPIO0) to trigger emergency   ║");
    Serial.println("║  Press again to cancel                            ║");
    Serial.println("╚══════════════════════════════════════════════════╝\n");
  }
 
  // Setup LoRa
  setupLoRa();
 
  // Power LED ON
  digitalWrite(LED_POWER, HIGH);
  
  // Initialize GPS UART
  gpsSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
  Serial.println("✅ NEO-6M GPS Serial Started\n");
  
  Serial.println("\n✅ SYSTEM READY");
  Serial.println("Waiting for emergency from EMT...\n");
}

// MAIN LOOP
void loop() {
  static unsigned long lastHeartbeat = 0;

  //  GPS Data 
  while (gpsSerial.available() > 0) {
    gps.encode(gpsSerial.read());
  }

  // LoRa
  checkManualButton();

  // Update location and transmit LoRa 
  if (isEmergencyActive && (millis() - lastUpdate >= UPDATE_INTERVAL)) {
    updateLocation();
    transmitLoRa();
    lastUpdate = millis();
  }
 
  // Heartbeat to show system is alive
  if (millis() - lastHeartbeat >= 5000) {
    if (isEmergencyActive) {
      Serial.println("💓 System active - LoRa transmitting...");
    } else {
      Serial.println("💓 System ready - waiting for emergency...");
    }
    lastHeartbeat = millis();
  }
 
  delay(10);
}

// WiFi Connection
void connectWiFi() {
  Serial.print("Connecting to WiFi: ");
  Serial.println(WIFI_SSID);
 
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
 
  int attempts = 0;

  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
 
  if (WiFi.status() == WL_CONNECTED) {
    wifiAvailable = true;
    Serial.println("\n✅ WiFi Connected!");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
    Serial.print("Signal: ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm\n");
  } else {
    Serial.println("\n❌ WiFi Failed!");
    Serial.println("Check SSID and password\n");
  }
}

// Firebase Setup
void setupFirebase() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Skipping Firebase - no WiFi\n");
    return;
  }
 
  Serial.println("Initializing Firebase...");
 
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
 
  Serial.println("Signing in...");
  if (Firebase.signUp(&config, &auth, "", "")) {
    Serial.println("✅ Firebase Auth Success");
  } else {
    Serial.println("❌ Auth Failed");
    Serial.printf("Error Code: %d\n", config.signer.signupError.code);
    return;
  }
 
  config.token_status_callback = tokenStatusCallback;
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
 
  Serial.print("Connecting to database");
  int count = 0;
  while (!Firebase.ready() && count < 20) {
    Serial.print(".");
    delay(500);
    count++;
  }
  Serial.println();

  if (Firebase.ready()) {
    Serial.println("✅ Firebase Connected!\n");
    setupEmergencyListener();
  } else {
    Serial.println("❌ Firebase Connection Failed\n");
  }
}

// Emergency Listener
void setupEmergencyListener() {
  String path = "/ambulances/" + String(AMBULANCE_ID) + "/emergency_id";
 
  Serial.println("Setting up emergency listener:");
  Serial.println("Path: " + path);
 
  if (!Firebase.RTDB.beginStream(&stream, path.c_str())) {
    Serial.println("❌ Stream Error");
    Serial.println(stream.errorReason());
  } else {
    Serial.println("✅ Listener Active\n");
    Firebase.RTDB.setStreamCallback(&stream, onEmergencyTriggered, onStreamTimeout);
  }
}

// Emergency Callback - HANDLES ALL DATA TYPES
void onEmergencyTriggered(FirebaseStream data) {
  Serial.println("\n🔥🔥🔥 FIREBASE EVENT RECEIVED 🔥🔥🔥");
  Serial.println("Path: " + data.dataPath());
  Serial.println("Type: " + data.dataType());
 
  String emergencyId = "";

  // Handle different data types
  if (data.dataType() == "string") {
    emergencyId = data.stringData();
    Serial.println("String value: " + emergencyId);
  }
  else if (data.dataType() == "int") {
    emergencyId = String(data.intData());
    Serial.println("Int value: " + emergencyId);
  }
  else if (data.dataType() == "null" || data.dataType() == "undefined") {
    Serial.println("⚠️ Null value - ignoring\n");
    return;
  }
  else {
    Serial.println("⚠️ Unknown type - ignoring\n");
    return;
  }
 
  Serial.println("Emergency ID: '" + emergencyId + "'");
  Serial.println("Length: " + String(emergencyId.length()));

  // Validate emergency ID
  if (emergencyId.length() > 5 &&
      emergencyId != "null" &&
      emergencyId != "waiting" &&
      emergencyId != "idle" &&
      emergencyId != "0" &&
      emergencyId != "") {
 
    Serial.println("\n🚨🚨🚨 EMERGENCY ACTIVATED! 🚨🚨🚨\n");
    currentEmergencyId = emergencyId;
    isEmergencyActive = true;
    digitalWrite(LED_EMERGENCY, HIGH);
 
    // details
    fetchEmergencyDetails(emergencyId);

    // Update status
    updateAmbulanceStatus("responding");
 
    Serial.println("📡 LoRa transmission STARTED\n");
  } else {
    Serial.println("⚠️ Invalid ID - waiting for valid emergency...\n");
    isEmergencyActive = false;
    digitalWrite(LED_EMERGENCY, LOW);
  }
}

void onStreamTimeout(bool timeout) {
  if (timeout) {
    Serial.println("⚠️ Stream timeout");
  }
}

//  Emergency Details
void fetchEmergencyDetails(String emergencyId) {
  String path = "/active_emergencies/" + emergencyId;

  if (Firebase.RTDB.getJSON(&fbdo, path.c_str())) {
    FirebaseJson &json = fbdo.jsonObject();
    FirebaseJsonData result;
 
    Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    Serial.println("     EMERGENCY DETAILS");
    Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━");
 
    if (json.get(result, "emergency_type/english")) {
      Serial.println("Type: " + result.stringValue);
    }
 
    if (json.get(result, "patient/age")) {
      Serial.printf("Age: %d years\n", result.intValue);
    }
 
    if (json.get(result, "patient/gender")) {
      Serial.println("Gender: " + result.stringValue);
    }
 
    Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
  } else {
    Serial.println("Could not fetch details\n");
  }
}

bool gpsFixIsFresh() {
  return gps.location.isValid() && gps.location.age() <= GPS_MAX_FIX_AGE_MS;
}

// Update location
void updateLocation() {
  hasGpsFix = gpsFixIsFresh();

  if (hasGpsFix) {
    currentLat = gps.location.lat();
    currentLng = gps.location.lng();
    lastGpsFixAt = millis();
    Serial.println("✅ Fresh NEO-6M GPS fix locked");
  } else {
    Serial.println("⚠️ Waiting for fresh NEO-6M GPS fix - Firebase lat/lng withheld");
  }

  if (gps.speed.isValid() && gps.speed.age() <= GPS_MAX_FIX_AGE_MS) {
    currentSpeed = gps.speed.kmph();
  } else {
    currentSpeed = 0; 
  }

  if (gps.satellites.isValid()) {
    currentSatellites = gps.satellites.value();
  }

  if (gps.hdop.isValid()) {
    currentHdop = gps.hdop.hdop();
  }

  // Update Firebase
  if (Firebase.ready()) {
    String path = "/active_emergencies/" + currentEmergencyId + "/location";
 
    FirebaseJson json;
    json.set("gps_fix", hasGpsFix);
    json.set("source", "neo-6m");
    json.set("satellites", currentSatellites);
    json.set("hdop", currentHdop);
    json.set("speed", currentSpeed);
    json.set("last_fix_age_ms", lastGpsFixAt == 0 ? -1 : (long)(millis() - lastGpsFixAt));
    if (hasGpsFix) {
      json.set("lat", currentLat);
      json.set("lng", currentLng);
    }
    // Use Firebase server 
    json.set("timestamp/.sv", "timestamp");
 
    Firebase.RTDB.updateNode(&fbdo, path.c_str(), &json);

    if (hasGpsFix) {
      String ambulancePath = "/ambulances/" + String(AMBULANCE_ID);
      FirebaseJson ambulanceJson;
      ambulanceJson.set("current_lat", currentLat);
      ambulanceJson.set("current_lng", currentLng);
      ambulanceJson.set("current_speed", currentSpeed);
      ambulanceJson.set("gps_fix", true);
      ambulanceJson.set("gps_source", "neo-6m");
      ambulanceJson.set("last_updated/.sv", "timestamp");
      Firebase.RTDB.updateNode(&fbdo, ambulancePath.c_str(), &ambulanceJson);
    }
  }
 
  if (hasGpsFix) {
    Serial.printf("📍 NEO-6M Location: %.6f, %.6f @ %d km/h\n", currentLat, currentLng, currentSpeed);
  } else {
    Serial.println("📍 Location not published yet; waiting for real GPS data");
  }
}

// Update Ambulance Status
void updateAmbulanceStatus(String status) {
  if (!Firebase.ready()) return;
 
  String path = "/ambulances/" + String(AMBULANCE_ID);
 
  FirebaseJson json;
  json.set("status", status);
  json.set("ambulance_id", AMBULANCE_ID);
  json.set("last_updated/.sv", "timestamp");
 
  Firebase.RTDB.updateNode(&fbdo, path.c_str(), &json);
  Serial.println("Status: " + status);
}

// ── Manual Trigger Button 
void checkManualButton() {
  if (!loraOnlyMode) return; 

  bool btnNow = digitalRead(MANUAL_TRIGGER_BTN);

  
  if (btnLastState == HIGH && btnNow == LOW) {
    btnPressTime = millis();
  }

  
  if (btnLastState == LOW && btnNow == HIGH) {
    if (millis() - btnPressTime >= DEBOUNCE_MS) {
      
      if (!isEmergencyActive) {
        isEmergencyActive  = true;
        currentEmergencyId = "MANUAL-" + String(millis());
        digitalWrite(LED_EMERGENCY, HIGH);
        Serial.println("\n🔴🔴🔴 MANUAL TRIGGER — EMERGENCY ACTIVATED 🔴🔴🔴");
        Serial.println("   LoRa TX starting (WiFi-independent mode)");
        Serial.println("   Firebase logging: SKIPPED (no WiFi)");
        Serial.println("   Press BOOT button again to cancel\n");
      } else {
        isEmergencyActive = false;
        currentEmergencyId = "";
        digitalWrite(LED_EMERGENCY, LOW);
        Serial.println("\n✅ MANUAL CANCEL — Emergency deactivated");
        Serial.println("   LoRa TX stopped\n");
      }
    }
  }

  btnLastState = btnNow;
}

// LoRa Setup
void setupLoRa() {
  Serial.println("Initializing LoRa...");
  LoRa.setPins(LORA_CS, LORA_RST, LORA_DIO0);
 
  if (!LoRa.begin(433E6)) {
    Serial.println("❌ LoRa Failed!");
    Serial.println("Check connections:");
    Serial.println("  VCC → 3.3V");
    Serial.println("  GND → GND");
    Serial.println("  SCK → GPIO 18");
    Serial.println("  MISO → GPIO 19");
    Serial.println("  MOSI → GPIO 23");
    Serial.println("  NSS → GPIO 5");
    Serial.println("  RST → GPIO 14");
    Serial.println("  DIO0 → GPIO 2\n");
    return;
  }
 
  LoRa.setTxPower(20);
  LoRa.setSpreadingFactor(9);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(5);
  LoRa.enableCrc();

  // Gateway sync word match
  LoRa.setSyncWord(LORA_SYNC_WORD); 
 
  Serial.println("✅ LoRa Ready (433 MHz)\n");
}

// LoRa Transmit
void transmitLoRa() {
  // 4-byte binary packet expected by Gateway
  byte cmd = isEmergencyActive ? CMD_EMERGENCY : CMD_NORMAL;
  byte id_h = (AMBULANCE_NUM_ID >> 8) & 0xFF;
  byte id_l = AMBULANCE_NUM_ID & 0xFF;
  byte chk = cmd ^ id_h ^ id_l ^ XOR_SECRET_KEY;


  byte packet[4] = {cmd, id_h, id_l, chk};
 
  LoRa.beginPacket();
  LoRa.write(packet, 4);
  LoRa.endPacket();

  Serial.printf("📡 LoRa TX: CMD: 0x%02X | ID: 0x%04X | CHK: 0x%02X\n", cmd, AMBULANCE_NUM_ID, chk);
}
