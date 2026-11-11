// node2: wake -> read sensor -> send JSON over ESP-NOW to the base station -> deep sleep.
// Based on CS3237/EdgeDevice.
//
//   [node2] --ESP-NOW--> [basestation] --HTTP--> [gateway] --MQTT--> ThingSpeak
//
// Channel: ESP-NOW only works if node2 transmits on the channel the base station is on.
// The base station sits on the IoT-GW AP's channel, and the gateway's AP is forced onto
// the channel of the phone hotspot it connects to. So when the hotspot changes channel,
// the whole chain moves. EspNowSender handles this: it remembers the channel that last
// got an ACK (in RTC memory, survives deep sleep), and if that fails it scans 1..13 and
// remembers the new one. ESPNOW_CHANNEL is only the first guess after a power-on.
//
// Libraries: libraries/EspNowJsonLink (in this repo), Adafruit BME280 Library.

#include <Wire.h>
#include <esp_sleep.h>
#include <esp_timer.h>
#include <Adafruit_BME280.h>
#include <EspNowJsonLink.h>

#define I2C_SDA          1
#define I2C_SCL          2
#define BME280_ADDR      0x76  // SDO to GND; 0x77 if SDO to VCC (both are tried)

#define SLEEP_SECONDS    10
#define AWAKE_BUDGET_MS  5000  // if still awake after this, assume a hang and sleep anyway

#define ESPNOW_CHANNEL   6     // first guess (iPhone hotspots are often on 6); the scan finds the real one
#define SEND_TIMEOUT_MS  50    // ACK wait per channel; a full scan costs ~13 x this
#define MAX_WIFI_CHANNEL 13

// The base station prints its MAC at boot: copy it here.
static const uint8_t BASE_STATION_MAC[6] = {0x34, 0x85, 0x18, 0xA1, 0x6F, 0xD8};

#define SENSOR_ERR       -999.0f  // sent as null

RTC_DATA_ATTR uint64_t uptimeMs = 0;  // time across sleep cycles
RTC_DATA_ATTR uint32_t bootCount = 0;
EspNowSender espnow;
Adafruit_BME280 bme;

struct SensorReading {
  float temp;      // degC
  float hum;       // %RH
  float pressure;  // hPa
};

// One forced-mode measurement: the BME280 takes a single reading, then sleeps.
// Fields stay SENSOR_ERR if the sensor isn't found.
void readSensor(SensorReading& r) {
  r = {SENSOR_ERR, SENSOR_ERR, SENSOR_ERR};

  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setTimeOut(50);  // ms, so a stuck bus gives up
  if (!bme.begin(BME280_ADDR, &Wire) && !bme.begin(0x77, &Wire)) {
    Serial.println("BME280 not found");
    return;
  }
  bme.setSampling(Adafruit_BME280::MODE_FORCED,
                  Adafruit_BME280::SAMPLING_X1,   // temperature
                  Adafruit_BME280::SAMPLING_X1,   // pressure
                  Adafruit_BME280::SAMPLING_X1,   // humidity
                  Adafruit_BME280::FILTER_OFF);
  if (!bme.takeForcedMeasurement()) {
    Serial.println("BME280 measurement failed");
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

void goToSleep() {
  uptimeMs += millis() + (uint64_t)SLEEP_SECONDS * 1000;
  esp_sleep_enable_timer_wakeup((uint64_t)SLEEP_SECONDS * 1000000ULL);
  esp_deep_sleep_start();
}

// Runs in the esp_timer task, so it fires even if setup() is stuck.
void deadman(void*) {
  goToSleep();
}

void armDeadman() {
  static esp_timer_handle_t timer;
  const esp_timer_create_args_t args = {.callback = deadman, .name = "deadman"};
  esp_timer_create(&args, &timer);
  esp_timer_start_once(timer, (uint64_t)AWAKE_BUDGET_MS * 1000ULL);
}

void setup() {
  armDeadman();  // before anything that can block
  Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
  Serial.setTxTimeoutMs(50);  // native USB: short wait so lines aren't cut, but never blocks for long
#endif
  bootCount++;

  SensorReading r;
  readSensor(r);

  char json[ESPNOW_JSON_MAX_LEN];
  buildJson(json, sizeof(json), r, (uint32_t)((uptimeMs + millis()) / 1000));

  espnow.setAckTimeout(SEND_TIMEOUT_MS);
  espnow.setMaxChannel(MAX_WIFI_CHANNEL);
  espnow.begin(BASE_STATION_MAC, ESPNOW_CHANNEL);
  bool acked = espnow.send(json);
  Serial.printf("#%lu %s -> %s (channel %u)\n", (unsigned long)bootCount, json,
                acked ? "ACK" : "no ACK", espnow.channel());
  Serial.flush();

  goToSleep();
}

void loop() {
  // Never reached: setup() ends in deep sleep.
}
