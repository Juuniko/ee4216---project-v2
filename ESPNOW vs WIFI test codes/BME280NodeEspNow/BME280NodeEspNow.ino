// BME280 node: ESP32-S3. Each wake: read BME280 -> send JSON over ESP-NOW -> deep sleep 2 s.
// Power-draw test, compared against BME280NodeWiFi. Adapted from my CS3237 power-test code.
// Libraries: Adafruit BME280 Library, libraries/EspNowJsonLink (in this repo).

#include <Wire.h>
#include <Adafruit_BME280.h>
#include <EspNowJsonLink.h>  // libraries/EspNowJsonLink
#include "secrets.h"         // BASE_STATION_MAC; copy secrets.h.example to secrets.h

#define I2C_SDA   1
#define I2C_SCL   2
#define SLEEP_US  2000000ULL  // 2 s

Adafruit_BME280 bme;
EspNowSender espnow;

void setup() {
  Wire.begin(I2C_SDA, I2C_SCL);
  bme.begin(0x76, &Wire);  // 0x77 if SDO is tied high

  char json[ESPNOW_JSON_MAX_LEN];
  snprintf(json, sizeof(json),
           "{\"device_type\":\"bme280\",\"data\":{\"temp\":%.2f,\"RH\":%.2f,\"pressure\":%.2f}}",
           bme.readTemperature(), bme.readHumidity(), bme.readPressure() / 100.0f);

  espnow.begin(BASE_STATION_MAC, 3);
  espnow.send(json);

  esp_deep_sleep(SLEEP_US);
}

void loop() {}
