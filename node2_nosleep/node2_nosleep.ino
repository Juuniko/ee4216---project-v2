// node2_nosleep: same as node2 (read BME280 -> send JSON over ESP-NOW to the base station),
// but stays awake and sends every SEND_INTERVAL_MS from loop() instead of deep-sleeping.
// The native USB serial port stays up, so the "-> ACK" lines can be watched in a monitor.
//
//   [node2] --ESP-NOW--> [basestation] --HTTP--> [gateway] --MQTT--> ThingSpeak
//
// Channel handling is the same as node2: EspNowSender remembers the channel that last got
// an ACK, and if a send fails it scans 1..MAX_WIFI_CHANNEL and keeps the new one.
//
// Libraries: libraries/EspNowJsonLink (in this repo), Adafruit BME280 Library.

#include <Wire.h>
#include <Adafruit_BME280.h>
#include <EspNowJsonLink.h>

#define I2C_SDA          1
#define I2C_SCL          2
#define BME280_ADDR      0x76  // SDO to GND; 0x77 if SDO to VCC (both are tried)

#define SEND_INTERVAL_MS 10000 // same rate as node2's 10 s sleep cycle

#define ESPNOW_CHANNEL   6     // first guess (iPhone hotspots are often on 6); the scan finds the real one
#define SEND_TIMEOUT_MS  50    // ACK wait per channel; a full scan costs ~13 x this
#define MAX_WIFI_CHANNEL 13

// The base station prints its MAC at boot: copy it here.
static const uint8_t BASE_STATION_MAC[6] = {0x34, 0x85, 0x18, 0xA1, 0x6F, 0xD8};

#define SENSOR_ERR       -999.0f  // sent as null

EspNowSender espnow;
Adafruit_BME280 bme;
bool bmeFound = false;
uint32_t sendCount = 0;

struct SensorReading {
  float temp;      // degC
  float hum;       // %RH
  float pressure;  // hPa
};

// Called once in setup(); readSensor() retries if the sensor wasn't found.
bool initSensor() {
  if (!bme.begin(BME280_ADDR, &Wire) && !bme.begin(0x77, &Wire)) {
    Serial.println("BME280 not found");
    return false;
  }
  bme.setSampling(Adafruit_BME280::MODE_FORCED,
                  Adafruit_BME280::SAMPLING_X1,   // temperature
                  Adafruit_BME280::SAMPLING_X1,   // pressure
                  Adafruit_BME280::SAMPLING_X1,   // humidity
                  Adafruit_BME280::FILTER_OFF);
  return true;
}

// One forced-mode measurement. Fields stay SENSOR_ERR if the sensor isn't available.
void readSensor(SensorReading& r) {
  r = {SENSOR_ERR, SENSOR_ERR, SENSOR_ERR};
  if (!bmeFound) bmeFound = initSensor();
  if (!bmeFound) return;
  if (!bme.takeForcedMeasurement()) {
    Serial.println("BME280 measurement failed");
    bmeFound = false;  // re-init next time
    return;
  }
  r.temp = bme.readTemperature();
  r.hum = bme.readHumidity();
  r.pressure = bme.readPressure() / 100.0f;  // Pa -> hPa
}

void fmtValue(char* out, size_t size, float v) {
  if (v == SENSOR_ERR || isnan(v)) snprintf(out, size, "null");
  else snprintf(out, size, "%.2f", v);
}

// {"temp":..,"RH":..,"hPa":..,"uptime":..}
void buildJson(char* out, size_t size, const SensorReading& r, uint32_t uptime_s) {
  char t[16], h[16], p[16];
  fmtValue(t, sizeof(t), r.temp);
  fmtValue(h, sizeof(h), r.hum);
  fmtValue(p, sizeof(p), r.pressure);
  snprintf(out, size, "{\"temp\":%s,\"RH\":%s,\"hPa\":%s,\"uptime\":%lu}",
           t, h, p, (unsigned long)uptime_s);
}

void setup() {
  Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
  Serial.setTxTimeoutMs(0);  // native USB: never block when no monitor is reading
#endif

  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setTimeOut(50);  // ms, so a stuck bus gives up
  bmeFound = initSensor();

  espnow.setAckTimeout(SEND_TIMEOUT_MS);
  espnow.setMaxChannel(MAX_WIFI_CHANNEL);
  espnow.begin(BASE_STATION_MAC, ESPNOW_CHANNEL);
}

void loop() {
  static uint32_t lastSend = 0;
  if (sendCount > 0 && millis() - lastSend < SEND_INTERVAL_MS) {
    delay(10);
    return;
  }
  lastSend = millis();
  sendCount++;

  SensorReading r;
  readSensor(r);

  char json[ESPNOW_JSON_MAX_LEN];
  buildJson(json, sizeof(json), r, millis() / 1000);

  bool acked = espnow.send(json);
  Serial.printf("#%lu %s -> %s (channel %u)\n", (unsigned long)sendCount, json,
                acked ? "ACK" : "no ACK", espnow.channel());
}
