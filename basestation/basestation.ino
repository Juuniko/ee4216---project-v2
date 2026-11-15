// Base station: receives node2's BME280 JSON over ESP-NOW and forwards the readings to the
// gateway over HTTP, the same way node1 does (joins the gateway's AP, POSTs to /api/data).
//
//   [node2] --ESP-NOW--> [basestation] --HTTP POST--> [gateway] --MQTT--> ThingSpeak
//
// ESP-NOW runs on whatever channel the IoT-GW AP is on (which follows the gateway's
// hotspot). Nodes using EspNowSender scan channels, so they find it on their own.
// Libraries: libraries/EspNowJsonLink (in this repo), ArduinoJson (v7).

#include <WiFi.h>
#include <HTTPClient.h>
#include <esp_mac.h>
#include <ArduinoJson.h>
#include <EspNowJsonLink.h>
#include "secrets.h"

const char* AP_SSID = "IoT-GW";

IPAddress baseIP(192, 168, 4, 12);
IPAddress gatewayIP(192, 168, 4, 1);
IPAddress subnet(255, 255, 255, 0);

const char* GATEWAY_DATA_URL = "http://192.168.4.1/api/data";
const uint16_t HTTP_TIMEOUT_MS = 2000;

EspNowReceiver espnow;  // queue mode: messages are read in loop()

// Appends "&key=value" if the node's JSON has a number for `key`.
// node2 sends null when the sensor failed; that field is left out so ThingSpeak gets no value.
bool addField(String& body, JsonDocument& doc, const char* key) {
  JsonVariant v = doc[key];
  if (!v.is<float>()) return false;
  body += "&";
  body += key;
  body += "=";
  body += String(v.as<float>(), 2);
  return true;
}

// Same endpoint and encoding as node1: id=base&temp=<degC>&RH=<%>&hPa=<hPa>
void forwardToGateway(const EspNowMessage& msg) {
  char mac[13];
  msg.macString(mac);

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, msg.json, msg.len);
  if (err) {
    Serial.printf("BAD   %s %s  (JSON: %s)\n", mac, msg.json, err.c_str());
    return;
  }

  String body = "id=base";
  int fields = addField(body, doc, "temp") + addField(body, doc, "RH") + addField(body, doc, "hPa");
  if (fields == 0) {
    Serial.printf("SKIP  %s %s  (no readings)\n", mac, msg.json);
    return;
  }

  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.begin(GATEWAY_DATA_URL);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  int code = http.POST(body);
  http.end();

  Serial.printf("RX    %s (%d dBm) %s  POST %s -> %d\n", mac, msg.rssi, msg.json, body.c_str(), code);
}

void setup() {
  Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
  Serial.setTxTimeoutMs(50);  // native USB: don't stall if no monitor is reading
#endif
  delay(1500);  // let the USB monitor reconnect so the MAC line below isn't missed

  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  Serial.printf("Base station MAC: %02X:%02X:%02X:%02X:%02X:%02X  <-- BASE_STATION_MAC on the node\n",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);  // modem sleep makes ESP-NOW reception unreliable
  WiFi.setAutoReconnect(true);
  WiFi.config(baseIP, gatewayIP, subnet);
  WiFi.begin(AP_SSID, AP_PASS);  // non-blocking: ESP-NOW runs meanwhile, watchChannel() logs the join
  Serial.printf("WIFI  joining %s...\n", AP_SSID);

  if (!espnow.begin()) {
    Serial.println("ESP-NOW init failed");
  }
}

// The gateway's AP follows its hotspot's channel. If that changes, this board drops off
// IoT-GW, auto-reconnects on the new channel, and ESP-NOW moves with it; nodes find it
// again by scanning. This just logs it so the switch is visible on serial.
void watchChannel() {
  static bool wasUp = false;
  static int lastChannel = -1;
  bool up = WiFi.status() == WL_CONNECTED;
  if (up && (!wasUp || WiFi.channel() != lastChannel)) {
    Serial.printf("WIFI  on IoT-GW, IP %s, channel %d (was %d)\n",
                  WiFi.localIP().toString().c_str(), WiFi.channel(), lastChannel);
    lastChannel = WiFi.channel();
  } else if (!up && wasUp) {
    Serial.println("WIFI  lost IoT-GW (auto-reconnecting)");
  }
  wasUp = up;
}

void loop() {
  watchChannel();

  EspNowMessage msg;
  if (espnow.read(msg, pdMS_TO_TICKS(100))) {
    if (!msg.looksLikeJson()) {
      Serial.printf("BAD   %u bytes, not a JSON object\n", msg.len);
    } else if (WiFi.status() != WL_CONNECTED) {
      char mac[13];
      msg.macString(mac);
      Serial.printf("DROP  %s %s  (WiFi down)\n", mac, msg.json);
    } else {
      forwardToGateway(msg);
    }
  }
}
