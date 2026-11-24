// First BME280 -> Wi-Fi -> PC HTTP/CSV connectivity test.
// Board: ESP32 Dev Module. Serial Monitor: 115200 baud.
// Existing wiring: SDA=GPIO21, SCL=GPIO22, VCC=3V3, GND=GND.
// Uses the already installed Adafruit BME280 library and ESP32 WiFi/HTTPClient.
// This is a connectivity test, not the final protocol comparison firmware.

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <HTTPClient.h>
#include <Adafruit_BME280.h>
#include <cmath>
#include <cstdio>
#include <cstring>

// Fill in these two values on your own computer; keep the quotation marks.
const char* WIFI_SSID = "WIFI_ADINI_YAZ";
const char* WIFI_PASSWORD = "WIFI_SIFRENI_YAZ";

// PC IPv4 address supplied during setup. Update it if your PC's IP changes.
const char* SERVER_URL = "http://192.168.1.71:5000/sensor";

constexpr uint8_t SDA_PIN = 21;
constexpr uint8_t SCL_PIN = 22;
constexpr uint32_t WIFI_WAIT_MS = 20000;
constexpr uint32_t SEND_PAUSE_MS = 2000;
Adafruit_BME280 bme;
bool bmeReady = false;
uint32_t sequenceNumber = 0;

int makePayload(char* buffer, size_t capacity, uint32_t sequence, uint32_t uptime,
                float temperature, float humidity, float pressure) {
  return std::snprintf(
      buffer, capacity,
      "{\"device_id\":\"esp32_01\",\"sequence\":%lu,\"uptime_ms\":%lu,"
      "\"temperature_c\":%.2f,\"humidity_pct\":%.2f,\"pressure_hpa\":%.2f}",
      static_cast<unsigned long>(sequence), static_cast<unsigned long>(uptime),
      static_cast<double>(temperature), static_cast<double>(humidity),
      static_cast<double>(pressure));
}

bool initialiseBme() {
  const uint8_t addresses[] = {0x76, 0x77};
  for (uint8_t address : addresses) {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() == 0 && bme.begin(address, &Wire)) {
      Serial.print("BME280 hazir. Adres: 0x");
      Serial.println(address, HEX);
      return true;
    }
  }
  Serial.println("BME280 bulunamadi. VCC/GND ve SDA/SCL baglantilarini kontrol et.");
  return false;
}

bool connectWifi() {
  if (WiFi.status() == WL_CONNECTED) {
    return true;
  }
  if (std::strcmp(WIFI_SSID, "WIFI_ADINI_YAZ") == 0 ||
      std::strcmp(WIFI_PASSWORD, "WIFI_SIFRENI_YAZ") == 0) {
    Serial.println("Once kodun basindaki WIFI_SSID ve WIFI_PASSWORD alanlarini doldur.");
    return false;
  }
  Serial.print("Wi-Fi baglantisi deneniyor");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  const uint32_t started = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - started < WIFI_WAIT_MS) {
    delay(500);
    Serial.print('.');
  }
  Serial.println();
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Wi-Fi baglanamadi. Ag adi, sifre ve 2.4 GHz agini kontrol et.");
    return false;
  }
  Serial.print("Wi-Fi baglandi. ESP32 IP: ");
  Serial.println(WiFi.localIP());
  Serial.print("Alici: ");
  Serial.println(SERVER_URL);
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(1200);
  Serial.println("\n--- ESP32 HTTP veri aktarim testi ---");
  if (Wire.begin(SDA_PIN, SCL_PIN, 100000)) {
    Wire.setTimeOut(50);
    bmeReady = initialiseBme();
  } else {
    Serial.println("I2C baslatilamadi.");
  }
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  connectWifi();
}

void loop() {
  if (!bmeReady) {
    bmeReady = initialiseBme();
    delay(3000);
    return;
  }
  if (!connectWifi()) {
    delay(3000);
    return;
  }

  const float temperature = bme.readTemperature();
  const float humidity = bme.readHumidity();
  const float pressure = bme.readPressure() / 100.0F;
  if (!std::isfinite(temperature) || !std::isfinite(humidity) ||
      !std::isfinite(pressure) || humidity < 0 || humidity > 100) {
    Serial.println("Gecersiz BME280 olcumu; bu olcum gonderilmedi.");
    delay(SEND_PAUSE_MS);
    return;
  }

  const uint32_t currentSequence = ++sequenceNumber;
  char payload[320];
  const int length = makePayload(payload, sizeof(payload), currentSequence, millis(),
                                 temperature, humidity, pressure);
  if (length <= 0 || static_cast<size_t>(length) >= sizeof(payload)) {
    Serial.println("JSON mesaji olusturulamadi.");
    delay(SEND_PAUSE_MS);
    return;
  }

  WiFiClient client;
  HTTPClient http;
  http.setConnectTimeout(4000);
  http.setTimeout(4000);
  // A fresh TCP connection for each reading in this initial setup test.
  http.setReuse(false);
  if (!http.begin(client, SERVER_URL)) {
    Serial.println("HTTP adresi acilamadi.");
    delay(SEND_PAUSE_MS);
    return;
  }
  http.addHeader("Content-Type", "application/json");
  Serial.print("Gonderiliyor: ");
  Serial.println(payload);
  const int status = http.POST(String(payload));
  Serial.print("HTTP=");
  Serial.print(status);
  Serial.print(" | sira=");
  Serial.println(currentSequence);
  if (status > 0) {
    Serial.print("Alici yaniti: ");
    Serial.println(http.getString());
  } else {
    Serial.print("Baglanti hatasi: ");
    Serial.println(HTTPClient::errorToString(status));
    Serial.println("PC alicisi, PC IP adresi ve yerel ag erisimini kontrol et.");
  }
  http.end();
  delay(SEND_PAUSE_MS);
}
