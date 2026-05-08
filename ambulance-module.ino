#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <LoRa.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"

// WiFi Configuration
#define WIFI_SSID "Arun"
#define WIFI_PASSWORD "11111110"

// Firebase Configuration
#define API_KEY "AIzaSyAhDKZsyio33xZjlLu9wrpP6FvCz0nx4Qo"
#define DATABASE_URL "https://emt-ambulance-case-data-default-rtdb.firebaseio.com"

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


// Global Objects
FirebaseData fbdo;
FirebaseData stream;
FirebaseAuth auth;
FirebaseConfig config;

bool isEmergencyActive = false;
String currentEmergencyId = "";
unsigned long lastUpdate = 0;
const unsigned long UPDATE_INTERVAL = 1000;

// GPS simulation
float currentLat = 17.3850;
float currentLng = 78.4867;
int currentSpeed = 0;

// SETUP
void setup() {
  Serial.begin(115200);
  delay(1000);
 
  Serial.println("\n\n════════════════════════════════════════════════════");
  Serial.println("  SMART AMBULANCE - " + String(AMBULANCE_ID));
  Serial.println("  Firebase + LoRa Mode");
  Serial.println("════════════════════════════════════════════════════\n");
 
  // LED Setup
  pinMode(LED_POWER, OUTPUT);
  pinMode(LED_EMERGENCY, OUTPUT);
  digitalWrite(LED_POWER, LOW);
  digitalWrite(LED_EMERGENCY, LOW);
 
  // Connect WiFi
  connectWiFi();
 
  // Setup Firebase
  setupFirebase();
 
  // Setup LoRa
  setupLoRa();
 
  // Power LED ON
  digitalWrite(LED_POWER, HIGH);
 
  Serial.println("\n✅ SYSTEM READY");
  Serial.println("Waiting for emergency from EMT...\n");
}

// MAIN LOOP
void loop() {
  static unsigned long lastHeartbeat = 0;
 
  // Update location and transmit LoRa if emergency active
  if (isEmergencyActive && (millis() - lastUpdate >= UPDATE_INTERVAL)) {
    updateLocation();
    transmitLoRa();
    lastUpdate = millis();
  }
 
  // Heartbeat every 5 seconds to show system is alive
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
 
    // Fetch details
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

// Fetch Emergency Details
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

// Update Location
void updateLocation() {
  // Simulate GPS movement
  currentLat += random(-20, 30) * 0.00001;
  currentLng += random(-20, 30) * 0.00001;
  currentSpeed = random(30, 60);
 
  // Update Firebase
  if (Firebase.ready()) {
    String path = "/active_emergencies/" + currentEmergencyId + "/location";
 
    FirebaseJson json;
    json.set("lat", currentLat);
    json.set("lng", currentLng);
    json.set("speed", currentSpeed);
    json.set("timestamp", millis());
 
    Firebase.RTDB.updateNode(&fbdo, path.c_str(), &json);
  }
 
  Serial.printf("📍 Location: %.6f, %.6f @ %d km/h\n",
                currentLat, currentLng, currentSpeed);
}

// Update Ambulance Status
void updateAmbulanceStatus(String status) {
  if (!Firebase.ready()) return;
 
  String path = "/ambulances/" + String(AMBULANCE_ID);
 
  FirebaseJson json;
  json.set("status", status);
  json.set("ambulance_id", AMBULANCE_ID);
  json.set("last_updated", millis());
 
  Firebase.RTDB.updateNode(&fbdo, path.c_str(), &json);
  Serial.println("Status: " + status);
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
 
  Serial.println("✅ LoRa Ready (433 MHz)\n");
}

// LoRa Transmit
void transmitLoRa() {
  String packet = String(AMBULANCE_ID) + "|" +
                  String(isEmergencyActive ? "1" : "0") + "|" +
                  String(currentLat, 6) + "|" +
                  String(currentLng, 6) + "|" +
                  String(currentSpeed);
 
  LoRa.beginPacket();
  LoRa.print(packet);
  LoRa.endPacket();
 
  Serial.println("📡 LoRa TX: " + packet);
}