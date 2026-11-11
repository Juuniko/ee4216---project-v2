#include "EspNowJsonLink.h"
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_idf_version.h>
#include <atomic>

// ---- Message ----------------------------------------------------------------

bool EspNowMessage::looksLikeJson() const {
  if (len < 2 || strlen(json) != len || json[0] != '{' || json[len - 1] != '}') return false;
  for (uint8_t i = 0; i < len; i++) {
    if ((uint8_t)json[i] < 0x20) return false;
  }
  return true;
}

void EspNowMessage::macString(char *out) const {
  snprintf(out, 13, "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

// ---- Sender -----------------------------------------------------------------

RTC_DATA_ATTR static uint8_t s_channel = 0;  // last channel the receiver ACKed on; survives deep sleep
static volatile bool s_sendDone = false;
static volatile bool s_sendOk = false;

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 0)
static void onSent(const wifi_tx_info_t *, esp_now_send_status_t status) {
#else
static void onSent(const uint8_t *, esp_now_send_status_t status) {
#endif
  s_sendOk = (status == ESP_NOW_SEND_SUCCESS);
  s_sendDone = true;
}

bool EspNowSender::begin(const uint8_t receiverMac[6], uint8_t firstChannel) {
  memcpy(mac_, receiverMac, 6);
  if (s_channel == 0) s_channel = firstChannel;

  WiFi.mode(WIFI_STA);
  if (esp_now_init() != ESP_OK) return false;
  esp_now_register_send_cb(onSent);

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, mac_, 6);
  peer.channel = 0;  // 0 = whatever channel the radio is on, so the peer follows the scan
  peer.encrypt = false;
  esp_err_t err = esp_now_add_peer(&peer);
  return err == ESP_OK || err == ESP_ERR_ESPNOW_EXIST;
}

bool EspNowSender::sendOnChannel(uint8_t ch, const uint8_t *data, size_t len) {
  esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
  s_sendDone = false;
  s_sendOk = false;
  if (esp_now_send(mac_, data, len) != ESP_OK) return false;

  uint32_t start = millis();
  while (!s_sendDone && millis() - start < ackTimeoutMs_) delay(1);
  return s_sendOk;
}

bool EspNowSender::send(const char *json) {
  return send((const uint8_t *)json, strlen(json));
}

bool EspNowSender::send(const uint8_t *data, size_t len) {
  if (len == 0 || len > ESPNOW_JSON_MAX_LEN) return false;

  // fast path: the channel that worked last time
  if (sendOnChannel(s_channel, data, len)) return true;

  // receiver moved (its router/hotspot changed channel): try every other channel
  for (uint8_t ch = 1; ch <= maxChannel_; ch++) {
    if (ch == s_channel) continue;
    if (sendOnChannel(ch, data, len)) {
      s_channel = ch;
      return true;
    }
  }
  return false;  // keep the old channel; the next send tries it first again
}

uint8_t EspNowSender::channel() const {
  return s_channel;
}

// ---- Receiver ---------------------------------------------------------------

static QueueHandle_t s_queue = nullptr;
static EspNowReceiver::Handler s_handler = nullptr;
static std::atomic<uint32_t> s_received{0};
static std::atomic<uint32_t> s_rejected{0};
static std::atomic<uint32_t> s_dropped{0};

// Runs in the WiFi task.
#if ESP_ARDUINO_VERSION_MAJOR >= 3
static void onRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  const uint8_t *mac = info->src_addr;
  int8_t rssi = info->rx_ctrl ? info->rx_ctrl->rssi : 0;
#else
static void onRecv(const uint8_t *mac, const uint8_t *data, int len) {
  int8_t rssi = 0;
#endif
  if (len < 1 || len > ESPNOW_JSON_MAX_LEN) {
    s_rejected++;
    return;
  }
  static EspNowMessage msg;  // static: keeps ~260 bytes off the WiFi task's stack (only this task runs it)
  memcpy(msg.mac, mac, 6);
  msg.rssi = rssi;
  msg.len = len;
  memcpy(msg.json, data, len);
  msg.json[len] = '\0';
  s_received++;

  if (s_handler) s_handler(msg);
  else if (xQueueSend(s_queue, &msg, 0) != pdTRUE) s_dropped++;  // drop if full
}

bool EspNowReceiver::begin(Handler handler, size_t queueLen) {
  s_handler = handler;
  if (!handler && !s_queue) {
    s_queue = xQueueCreate(queueLen, sizeof(EspNowMessage));
    if (!s_queue) return false;
  }
  if (esp_now_init() != ESP_OK) return false;
  return esp_now_register_recv_cb(onRecv) == ESP_OK;
}

bool EspNowReceiver::read(EspNowMessage &msg, TickType_t wait) {
  return s_queue && xQueueReceive(s_queue, &msg, wait) == pdTRUE;
}

uint32_t EspNowReceiver::received() const { return s_received; }
uint32_t EspNowReceiver::rejected() const { return s_rejected; }
uint32_t EspNowReceiver::dropped() const { return s_dropped; }
