// Prints every message received over ESP-NOW.
// Without WiFi it listens on channel 1; after WiFi.begin() it listens on the router's channel
// (senders using EspNowSender find it by scanning).
#include <WiFi.h>
#include <esp_mac.h>
#include <EspNowJsonLink.h>

EspNowReceiver rx;

void setup() {
  Serial.begin(115200);
  delay(1000);
  WiFi.mode(WIFI_STA);

  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  Serial.printf("Receiver MAC: %02X:%02X:%02X:%02X:%02X:%02X  <-- put this in the sender\n",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

  if (!rx.begin()) Serial.println("ESP-NOW init failed");
}

void loop() {
  EspNowMessage msg;
  if (rx.read(msg)) {
    char mac[13];
    msg.macString(mac);
    Serial.printf("%s (%d dBm): %s%s\n", mac, msg.rssi, msg.json, msg.looksLikeJson() ? "" : "  [not JSON]");
  }
}
