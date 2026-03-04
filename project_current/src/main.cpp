#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// ── Config ────────────────────────────────────────────────────────────────────
const char* WIFI_SSID      = "Wokwi-GUEST";
const char* WIFI_PASSWORD  = "";
const char* MQTT_BROKER    = "192.168.1.100";   // ← your HA/broker IP
const int   MQTT_PORT      = 1883;
const char* MQTT_USER      = "";                // leave empty if no auth
const char* MQTT_PASS      = "";
#define DEVICE_NAME "esp32_current"

// ── Sensor config ─────────────────────────────────────────────────────────────
// Calibration: adjust ICAL for your specific CT sensor
//   ZMCT103C  with 100 ohm burden: ICAL ~ 10.0  (1000:1 turns, 100 ohm)
//   SCT-013-030 (30A/1V version):  ICAL ~ 30.0  (built-in burden)
//   SCT-013-000 (split-core) w/ 62 ohm burden:  ICAL ~ 111.1
const float ICAL           = 30.0f;
const int   ADC_PIN        = 34;          // GPIO34 - ADC1_CH6, input-only pin
const int   ADC_SAMPLES    = 1000;        // samples per RMS calculation
const float ADC_VREF       = 3.3f;
const int   ADC_RESOLUTION = 4096;        // 12-bit

const unsigned long PUBLISH_INTERVAL_MS = 10000;

// ── MQTT topics ───────────────────────────────────────────────────────────────
const char* TOPIC_STATE    = "homeassistant/sensor/" DEVICE_NAME "/state";
const char* TOPIC_CONFIG   = "homeassistant/sensor/" DEVICE_NAME "/config";

// ── Globals ───────────────────────────────────────────────────────────────────
WiFiClient   wifiClient;
PubSubClient mqtt(wifiClient);
unsigned long lastPublish = 0;

// ── Forward declarations ──────────────────────────────────────────────────────
void connectWiFi();
void connectMQTT();
void publishHADiscovery();
float readCurrentRMS();
void publishReading(float amps);

// ─────────────────────────────────────────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);   // full 0-3.3V range on ADC

  connectWiFi();
  mqtt.setServer(MQTT_BROKER, MQTT_PORT);
  connectMQTT();
  publishHADiscovery();
}

void loop() {
  if (!mqtt.connected()) connectMQTT();
  mqtt.loop();

  if (millis() - lastPublish >= PUBLISH_INTERVAL_MS) {
    lastPublish = millis();
    float amps = readCurrentRMS();
    publishReading(amps);
  }
}

// ── WiFi ──────────────────────────────────────────────────────────────────────
void connectWiFi() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("[wifi] Connecting");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.printf("\n[wifi] Connected, IP: %s\n", WiFi.localIP().toString().c_str());
}

// ── MQTT ──────────────────────────────────────────────────────────────────────
void connectMQTT() {
  while (!mqtt.connected()) {
    Serial.print("[mqtt] Connecting...");
    bool ok = (strlen(MQTT_USER) > 0)
      ? mqtt.connect(DEVICE_NAME, MQTT_USER, MQTT_PASS)
      : mqtt.connect(DEVICE_NAME);

    if (ok) {
      Serial.println(" connected");
    } else {
      Serial.printf(" failed (rc=%d), retry in 5s\n", mqtt.state());
      delay(5000);
    }
  }
}

// ── Home Assistant MQTT auto-discovery ───────────────────────────────────────
void publishHADiscovery() {
  StaticJsonDocument<512> doc;
  doc["name"]               = "AC Current";
  doc["unique_id"]          = DEVICE_NAME;
  doc["state_topic"]        = TOPIC_STATE;
  doc["unit_of_measurement"]= "A";
  doc["device_class"]       = "current";
  doc["value_template"]     = "{{ value_json.current }}";
  doc["icon"]               = "mdi:current-ac";

  JsonObject device = doc.createNestedObject("device");
  device["identifiers"][0]  = DEVICE_NAME;
  device["name"]            = "ESP32 Current Meter";
  device["model"]           = "ZMCT103C / SCT-013";
  device["manufacturer"]    = "DIY";

  char buf[512];
  serializeJson(doc, buf);
  mqtt.publish(TOPIC_CONFIG, buf, true);   // retained
  Serial.printf("[ha] Discovery published to %s\n", TOPIC_CONFIG);
}

// ── RMS current measurement ───────────────────────────────────────────────────
float readCurrentRMS() {
  long sumSquares = 0;
  long biasSum    = 0;

  // First pass: find DC bias (midpoint voltage)
  for (int i = 0; i < ADC_SAMPLES; i++) {
    biasSum += analogRead(ADC_PIN);
    delayMicroseconds(100);
  }
  long bias = biasSum / ADC_SAMPLES;

  // Second pass: RMS calculation around bias
  for (int i = 0; i < ADC_SAMPLES; i++) {
    long sample = analogRead(ADC_PIN) - bias;
    sumSquares += sample * sample;
    delayMicroseconds(100);
  }

  float adc_rms  = sqrt((float)sumSquares / ADC_SAMPLES);
  float v_rms    = adc_rms * (ADC_VREF / ADC_RESOLUTION);
  float i_rms    = v_rms * ICAL;

  // Suppress noise floor (< ~0.1A reads as 0)
  if (i_rms < 0.1f) i_rms = 0.0f;

  return i_rms;
}

// ── MQTT publish ──────────────────────────────────────────────────────────────
void publishReading(float amps) {
  StaticJsonDocument<64> doc;
  doc["current"] = serialized(String(amps, 2));

  char buf[64];
  serializeJson(doc, buf);
  mqtt.publish(TOPIC_STATE, buf);
  Serial.printf("[mqtt] Published: %s → %s\n", TOPIC_STATE, buf);
}
