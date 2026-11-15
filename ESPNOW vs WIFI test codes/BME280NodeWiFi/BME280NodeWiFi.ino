// BME280 node, WiFi version: ESP32-S3. Each wake: read BME280 -> connect WiFi -> publish JSON
// straight to the MQTT broker (no base station) -> deep sleep 2 s.
// Power-draw test, compared against BME280NodeEspNow. Adapted from my CS3237 power-test code.
// Libraries: Adafruit BME280 Library. MQTT uses the ESP-IDF client built into the core.

#include <Wire.h>
#include <WiFi.h>
#include <mqtt_client.h>
#include <Adafruit_BME280.h>
#include "secrets.h"  // WiFi + MQTT broker; copy secrets.h.example to secrets.h

#define I2C_SDA   1
#define I2C_SCL   2
#define SLEEP_US  2000000ULL  // 2 s

Adafruit_BME280 bme;
volatile bool mqttConnected = false;

void onMqttEvent(void *, esp_event_base_t, int32_t id, void *) {
  if (id == MQTT_EVENT_CONNECTED) mqttConnected = true;
}

void setup() {
  Wire.begin(I2C_SDA, I2C_SCL);
  bme.begin(0x76, &Wire);  // 0x77 if SDO is tied high

  char json[160];
  snprintf(json, sizeof(json),
           "{\"device_type\":\"bme280\",\"data\":{\"temp\":%.2f,\"RH\":%.2f,\"pressure\":%.2f}}",
           bme.readTemperature(), bme.readHumidity(), bme.readPressure() / 100.0f);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED && millis() < 10000) delay(10);

  // Topic: ee4216/<MAC>/data (only used for this power test)
  String mac = WiFi.macAddress();
  mac.replace(":", "");
  String topic = "ee4216/" + mac + "/data";

  esp_mqtt_client_config_t cfg = {};
  cfg.broker.address.transport = MQTT_TRANSPORT_OVER_TCP;
  cfg.broker.address.hostname = MQTT_HOST;
  cfg.broker.address.port = MQTT_PORT;
  cfg.credentials.client_id = "ee4216-bme280";
  cfg.credentials.username = MQTT_USER;
  cfg.credentials.authentication.password = MQTT_PASSWORD;
  esp_mqtt_client_handle_t mqtt = esp_mqtt_client_init(&cfg);
  esp_mqtt_client_register_event(mqtt, MQTT_EVENT_ANY, onMqttEvent, nullptr);
  esp_mqtt_client_start(mqtt);
  while (!mqttConnected && millis() < 15000) delay(10);

  esp_mqtt_client_publish(mqtt, topic.c_str(), json, 0, 0, 0);
  delay(100);  // let the packet leave before the radio goes off

  esp_deep_sleep(SLEEP_US);
}

void loop() {}
