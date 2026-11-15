#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <PubSubClient.h>
#include "secrets.h"

const char* AP_SSID = "IoT-GW";

const char* STA_SSID = "ASUSiPhone";

const char* MQTT_HOST = "mqtt3.thingspeak.com";
const int   MQTT_PORT = 1883;
const char* TOPIC_PUBLISH  = "channels/3569731/publish";
const char* TOPIC_PUMP_CMD = "channels/3570144/subscribe/fields/field1";

const char* NODE1_CMD_URL = "http://192.168.4.11/api/cmd";

WebServer server(80);
WiFiClient netClient;
PubSubClient mqtt(netClient);

float node1Soil = 0;

// From the base station (node2's BME280). Each is published once per new reading;
// a field the base station leaves out (sensor error) is not published.
float envTemp = 0, envRH = 0, envHPa = 0;
bool  hasTemp = false, hasRH = false, hasHPa = false;

String pendingCmd = "";
bool   hasResult  = false;
int    pumpResult = 0;

const unsigned long PUBLISH_INTERVAL = 20000;
unsigned long lastPublish = 0;
const unsigned long RECONNECT_INTERVAL = 5000;
unsigned long lastMqttTry = 0;

void handleData() {
  String id = server.arg("id");

  if (id == "base") {
    if (server.hasArg("temp")) { envTemp = server.arg("temp").toFloat(); hasTemp = true; }
    if (server.hasArg("RH"))   { envRH   = server.arg("RH").toFloat();   hasRH   = true; }
    if (server.hasArg("hPa"))  { envHPa  = server.arg("hPa").toFloat();  hasHPa  = true; }
    server.send(200, "text/plain", "ok");
    Serial.printf("HTTP  base temp=%s RH=%s hPa=%s\n", server.arg("temp").c_str(),
                  server.arg("RH").c_str(), server.arg("hPa").c_str());
    return;
  }

  float soil = server.arg("soil").toFloat();

  if (id == "node1") {
    node1Soil = soil;
  }

  server.send(200, "text/plain", "ok");
  Serial.printf("HTTP  %s soil=%.1f\n", id.c_str(), soil);
}

void onMqttMessage(char* topic, byte* payload, unsigned int length) {
  String value = "";
  for (unsigned int i = 0; i < length; i++) {
    value += (char)payload[i];
  }
  Serial.printf("MQTT  received %s = '%s'\n", topic, value.c_str());

  if (value.length() == 0) {
    return;
  }
  pendingCmd = value;
}

void connectMqtt() {
  mqtt.connect(MQTT_CLIENT_ID, MQTT_USERNAME, MQTT_PASSWORD);
  if (mqtt.connected()) {
    mqtt.subscribe(TOPIC_PUMP_CMD);
  }
  Serial.printf("MQTT  connect -> state %d (0 means connected)\n", mqtt.state());
}

void forwardCommand() {
  HTTPClient http;
  http.begin(NODE1_CMD_URL);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  int code = http.POST("pump=" + pendingCmd);
  String reply = http.getString();
  http.end();

  if (code == 200) {
    pumpResult = pendingCmd.toInt();
  } else if (code == 400) {
    pumpResult = -2;
  } else {
    pumpResult = -1;
  }
  hasResult = true;
  Serial.printf("CMD   pump=%s -> node %d '%s' -> result %d\n",
                pendingCmd.c_str(), code, reply.c_str(), pumpResult);
  pendingCmd = "";
}

void setup() {
  Serial.begin(115200);

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, AP_PASS);
  WiFi.setAutoReconnect(true);
  WiFi.begin(STA_SSID, STA_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.print("\nAP IP  = ");
  Serial.println(WiFi.softAPIP());
  Serial.print("STA IP = ");
  Serial.println(WiFi.localIP());

  server.on("/api/data", HTTP_POST, handleData);
  server.begin();

  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(onMqttMessage);
  connectMqtt();
}

void loop() {
  server.handleClient();
  mqtt.loop();

  if (!mqtt.connected() && WiFi.status() == WL_CONNECTED
      && millis() - lastMqttTry >= RECONNECT_INTERVAL) {
    lastMqttTry = millis();
    connectMqtt();
  }

  if (pendingCmd.length() > 0) {
    forwardCommand();
  }

  if (millis() - lastPublish >= PUBLISH_INTERVAL) {
    lastPublish = millis();
    String payload = "field1=" + String(node1Soil, 1);
    if (hasResult) {
      payload += "&field2=" + String(pumpResult);
      hasResult = false;
    }
    if (hasTemp) { payload += "&field3=" + String(envTemp, 2); hasTemp = false; }
    if (hasRH)   { payload += "&field4=" + String(envRH, 2);   hasRH   = false; }
    if (hasHPa)  { payload += "&field5=" + String(envHPa, 2);  hasHPa  = false; }
    bool ok = mqtt.publish(TOPIC_PUBLISH, payload.c_str());
    Serial.printf("MQTT  publish %s -> %s\n", payload.c_str(), ok ? "ok" : "fail");
  }
}
