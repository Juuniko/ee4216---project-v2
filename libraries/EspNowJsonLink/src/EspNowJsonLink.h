// EspNowJsonLink: send JSON text between ESP32 boards over ESP-NOW.
//
// Sender (e.g. a battery device that deep sleeps):
//   EspNowSender sender;
//   sender.begin(RECEIVER_MAC, 1);           // 1 = first channel to try
//   sender.send("{\"temp\":21.5}");          // true if the receiver ACKed
// The channel that worked is kept in RTC memory, so after deep sleep the next send goes
// straight to it. If it gets no ACK, channels 1..13 are tried until one does.
//
// Receiver (always on):
//   WiFi.mode(WIFI_STA);                   // and WiFi.begin(...) if it needs internet;
//                                          // ESP-NOW then runs on the router's channel
//   EspNowReceiver rx;
//   rx.begin();                            // queue mode, then in a loop or task:
//   EspNowMessage msg;
//   if (rx.read(msg) && msg.looksLikeJson()) { use msg.json, msg.mac, msg.rssi }
// Or rx.begin(myHandler) to get each message straight from the WiFi task (keep it short).
#pragma once
#include <Arduino.h>

#define ESPNOW_JSON_MAX_LEN 250  // ESP-NOW payload limit

struct EspNowMessage {
  uint8_t mac[6];                       // sender
  int8_t rssi;                          // signal strength in dBm (0 if unknown)
  uint8_t len;                          // payload length, without the terminator
  char json[ESPNOW_JSON_MAX_LEN + 1];   // payload, null-terminated

  // Cheap sanity check, not a full parser: one {...} object, no control characters or embedded nulls.
  bool looksLikeJson() const;
  // Sender MAC as 12 uppercase hex digits, e.g. "F09E9EADB79C". `out` must hold 13 chars.
  void macString(char *out) const;
};

class EspNowSender {
 public:
  // Starts WiFi (station mode) and ESP-NOW and adds the receiver as a peer.
  // firstChannel is only used until a channel has worked; after that the remembered one is used.
  bool begin(const uint8_t receiverMac[6], uint8_t firstChannel = 1);

  // Fire and forget: tries the remembered channel, then scans. True if the receiver ACKed.
  bool send(const char *json);                 // sends strlen(json) bytes
  bool send(const uint8_t *data, size_t len);

  uint8_t channel() const;                     // channel that last worked
  void setAckTimeout(uint16_t ms) { ackTimeoutMs_ = ms; }     // wait per channel, default 50 ms
  void setMaxChannel(uint8_t ch) { maxChannel_ = ch; }        // 13 (most countries), 11 (US)

 private:
  bool sendOnChannel(uint8_t ch, const uint8_t *data, size_t len);
  uint8_t mac_[6] = {0};
  uint16_t ackTimeoutMs_ = 50;
  uint8_t maxChannel_ = 13;
};

class EspNowReceiver {
 public:
  typedef void (*Handler)(const EspNowMessage &msg);

  // Call after WiFi is started. With a handler, each message is passed to it from the
  // WiFi task (copy what you need and return). Without one, messages go into a queue.
  bool begin(Handler handler = nullptr, size_t queueLen = 16);

  // Queue mode only: waits up to `wait` ticks for a message.
  bool read(EspNowMessage &msg, TickType_t wait = portMAX_DELAY);

  uint32_t received() const;  // messages accepted
  uint32_t rejected() const;  // empty or over ESPNOW_JSON_MAX_LEN
  uint32_t dropped() const;   // queue full (queue mode only)
};
