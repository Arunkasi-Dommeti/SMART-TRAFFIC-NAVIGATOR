#include <esp_now.h>
#include <WiFi.h>

uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

typedef struct struct_message {
  int emergency_status;
} struct_message;

struct_message myData;
esp_now_peer_info_t peerInfo;

const int switchPin = 4; 
void setup() {
  Serial.begin(115200);
 
  pinMode(switchPin, INPUT_PULLUP);
 
  WiFi.mode(WIFI_STA);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW Error! Initialize avvaledu.");
    return;
  }
 
  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
 
  if (esp_now_add_peer(&peerInfo) != ESP_OK){
    Serial.println("Failed to add peer");
    return;
  }
}

void loop() {
  if (digitalRead(switchPin) == LOW) {
 
    myData.emergency_status = 1;
 
    esp_err_t result = esp_now_send(broadcastAddress, (uint8_t *) &myData, sizeof(myData));
 
    if (result == ESP_OK) {
      Serial.println("🚨 EMERGENCY SIGNAL SENT!");
    } else {
      Serial.println("Error sending the data");
    }
 
    delay(500);
  }
}