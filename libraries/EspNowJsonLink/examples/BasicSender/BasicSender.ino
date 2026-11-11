// Sends a small JSON message every 2 s. The receiver's channel is found automatically.
#include <EspNowJsonLink.h>

static const uint8_t RECEIVER_MAC[6] = {0x20, 0x50, 0x0D, 0xD3, 0x4A, 0xF4};  // change to your receiver

EspNowSender sender;

void setup() {
  Serial.begin(115200);
  sender.begin(RECEIVER_MAC);
}

void loop() {
  char json[64];
  snprintf(json, sizeof(json), "{\"uptime\":%lu}", millis() / 1000);
  bool ok = sender.send(json);
  Serial.printf("%s -> %s (channel %u)\n", json, ok ? "acked" : "no ack", sender.channel());
  delay(2000);
}
