#include <WiFi.h>
#include <HTTPClient.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// Forward declarations
void connectWiFi();
float readTemperature();
void sendToSheets(float temp);

// --- Config ---
const char* WIFI_SSID     = "Wokwi-GUEST";
const char* WIFI_PASSWORD = "";
const char* SCRIPT_URL    = "https://script.google.com/macros/s/YOUR_SCRIPT_ID/exec";

const int   ONE_WIRE_PIN  = 4;
const bool  BATTERY_MODE  = false;

// --- DS18B20 ---
OneWire           oneWire(ONE_WIRE_PIN);
DallasTemperature sensors(&oneWire);

void setup() {
  Serial.begin(115200);
  sensors.begin();

  connectWiFi();
  float temp = readTemperature();

  if (temp != DEVICE_DISCONNECTED_C) {
    Serial.printf("[setup] Temperature: %.2f C\n", temp);
    sendToSheets(temp);
  } else {
    Serial.println("[setup] Sensor error: DEVICE_DISCONNECTED");
  }
}

void loop() {
  if (!BATTERY_MODE) {
    float temp = readTemperature();
    if (temp != DEVICE_DISCONNECTED_C) {
      Serial.printf("[loop] Temperature: %.2f C\n", temp);
      sendToSheets(temp);
    } else {
      Serial.println("[loop] Sensor read error");
    }
    delay(60000);
  }
}

void connectWiFi() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("[wifi] Connecting");
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 30) {
    delay(500);
    Serial.print(".");
    tries++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\n[wifi] Connected, IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("\n[wifi] Failed to connect");
  }
}

float readTemperature() {
  sensors.requestTemperatures();
  return sensors.getTempCByIndex(0);
}

void sendToSheets(float temp) {
  if (WiFi.status() != WL_CONNECTED) connectWiFi();

  HTTPClient http;
  String url = String(SCRIPT_URL) + "?temp=" + String(temp, 2);
  Serial.printf("[http] GET %s\n", url.c_str());
  http.begin(url);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  int code = http.GET();
  String body = http.getString();
  Serial.printf("[http] Response: %d — %s\n", code, body.c_str());
  http.end();
}
