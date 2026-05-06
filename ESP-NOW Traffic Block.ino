#include <esp_now.h>
#include <WiFi.h>

const int J1_G = 14;
const int J1_Y = 12;
const int J1_R = 13;

const int J2_G = 27;
const int J2_Y = 26;
const int J2_R = 25;


typedef struct struct_message {
  int emergency_status;
} struct_message;
struct_message myData;

bool isEmergency = false;
unsigned long emergencyStartTime = 0;
const unsigned long emergencyDuration = 10000;

unsigned long previousMillis = 0;
int trafficState = 0;

void OnDataRecv(const esp_now_recv_info * info, const uint8_t *incomingData, int len) {
  memcpy(&myData, incomingData, sizeof(myData));
 

  if (myData.emergency_status == 1) {
    isEmergency = true;
    emergencyStartTime = millis();
    Serial.println("🚨 EMERGENCY RECEIVED! J1 GREEN");
  }
}
void setup() {
  Serial.begin(115200);
 
  pinMode(J1_G, OUTPUT); pinMode(J1_Y, OUTPUT); pinMode(J1_R, OUTPUT);
  pinMode(J2_G, OUTPUT); pinMode(J2_Y, OUTPUT); pinMode(J2_R, OUTPUT);

  // WiFi Station Mode
  WiFi.mode(WIFI_STA);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW Error");
    return;
  }
 
  esp_now_register_recv_cb(OnDataRecv);
}

void loop() {
  if (isEmergency) {
 
    digitalWrite(J1_G, HIGH); digitalWrite(J1_Y, LOW); digitalWrite(J1_R, LOW);
    digitalWrite(J2_G, LOW); digitalWrite(J2_Y, LOW); digitalWrite(J2_R, HIGH);
 
    if (millis() - emergencyStartTime >= emergencyDuration) {
      isEmergency = false;
      Serial.println("BACK TO NORMAL");
    }
  }
  else {
    normalTrafficCycle();
  }
}
void normalTrafficCycle() {
  unsigned long currentMillis = millis();
 
  if (trafficState == 0) { // J1 Green, J2 Red
    digitalWrite(J1_G, HIGH); digitalWrite(J1_R, LOW);
    digitalWrite(J2_R, HIGH); digitalWrite(J2_G, LOW);
    if (currentMillis - previousMillis >= 5000) {
      trafficState = 1; previousMillis = currentMillis;
    }
  }
  else if (trafficState == 1) { // J1 Yellow, J2 Red
    digitalWrite(J1_G, LOW); digitalWrite(J1_Y, HIGH);
    if (currentMillis - previousMillis >= 2000) {
      trafficState = 2; previousMillis = currentMillis;
    }
  }
  else if (trafficState == 2) { // J1 Red, J2 Green
    digitalWrite(J1_Y, LOW); digitalWrite(J1_R, HIGH);
    digitalWrite(J2_R, LOW); digitalWrite(J2_G, HIGH);
    if (currentMillis - previousMillis >= 5000) {
      trafficState = 3; previousMillis = currentMillis;
    }
  }
  else if (trafficState == 3) { // J1 Red, J2 Yellow
    digitalWrite(J2_G, LOW); digitalWrite(J2_Y, HIGH);
    if (currentMillis - previousMillis >= 2000) {
      digitalWrite(J2_Y, LOW);
      trafficState = 0; previousMillis = currentMillis;
    }
  }
}