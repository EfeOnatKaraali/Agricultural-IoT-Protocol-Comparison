// Initial BME280 -> ESP32 -> Mosquitto connectivity test.
// Board: ESP32 Dev Module. Serial Monitor: 115200 baud.
// BME280: VCC=3V3, GND=GND, SDA=GPIO21, SCL=GPIO22.
// Libraries: PubSubClient by Nick O'Leary and Adafruit BME280.
// MQTT 3.1.1, QoS 0, retained=false, persistent TCP connection.
// publish()==true means a local write succeeded, NOT receiver/CSV confirmation.
// This is a connectivity test, not the final protocol benchmark firmware.

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <Adafruit_BME280.h>
#include <cmath>
#include <cstdio>
#include <cstring>

// Enter these privately on your own computer. Keep the quotation marks.
const char* WIFI_SSID = "WIFI_ADINI_YAZ";
const char* WIFI_PASSWORD = "WIFI_SIFRENI_YAZ";

// Computer's current Wi-Fi IPv4 address. Check with ipconfig.
// 127.0.0.1 is NOT the computer's address from the ESP32.
const char* MQTT_HOST = "192.168.1.71";
constexpr uint16_t MQTT_PORT = 1884;
const char* MQTT_TOPIC = "agriculture/sensors/esp32_01";

constexpr uint8_t SDA_PIN = 21;
constexpr uint8_t SCL_PIN = 22;
constexpr uint32_t SEND_INTERVAL_MS = 2000;
constexpr uint32_t RETRY_MS = 5000;

Adafruit_BME280 bme;
WiFiClient networkClient;
PubSubClient mqtt(networkClient);
bool bmeReady = false;
uint32_t sequenceNumber = 0;
uint32_t lastSendAt = 0;
uint32_t lastConnectAttempt = 0;
String mqttClientId;

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
  if (WiFi.status() == WL_CONNECTED) return true;
  if (std::strcmp(WIFI_SSID, "WIFI_ADINI_YAZ") == 0 ||
      std::strcmp(WIFI_PASSWORD, "WIFI_SIFRENI_YAZ") == 0) {
    Serial.println("Once WIFI_SSID ve WIFI_PASSWORD alanlarini doldur.");
    return false;
  }
  Serial.print("Wi-Fi baglantisi deneniyor");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  const uint32_t startAt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAt < 20000) {
    delay(500);
    Serial.print('.');
  }
  Serial.println();
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Wi-Fi baglanamadi. 2.4 GHz agini ve bilgileri kontrol et.");
    return false;
  }
  Serial.print("Wi-Fi baglandi. ESP32 IP: ");
  Serial.println(WiFi.localIP());
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(1200);
  Serial.println("\n--- ESP32 MQTT veri aktarim testi ---");
  if (Wire.begin(SDA_PIN, SCL_PIN, 100000)) {
    Wire.setTimeOut(50);
    bmeReady = initialiseBme();
  } else {
    Serial.println("I2C baslatilamadi. SDA/SCL pinlerini kontrol et.");
    while (true) delay(1000);
  }
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  mqttClientId = String("esp32_01_") + WiFi.macAddress();
  mqttClientId.replace(":", "");
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setKeepAlive(15);
  mqtt.setSocketTimeout(4);
  if (!mqtt.setBufferSize(512)) {
    Serial.println("MQTT bellek ayirma hatasi.");
    while (true) delay(1000);
  }
  connectWifi();
}

void loop() {
  if (!bmeReady) {
    bmeReady = initialiseBme();
    delay(3000);
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    mqtt.disconnect();
    if (!connectWifi()) delay(3000);
    return;
  }
  if (!mqtt.connected()) {
    if (millis() - lastConnectAttempt < RETRY_MS) return;
    lastConnectAttempt = millis();
    Serial.printf("MQTT baglantisi deneniyor: %s:%u\n", MQTT_HOST, MQTT_PORT);
    if (mqtt.connect(mqttClientId.c_str())) {
      Serial.println("MQTT baglandi. QoS=0, retained=false.");
      Serial.print("Konu: ");
      Serial.println(MQTT_TOPIC);
      lastSendAt = millis();
    } else {
      Serial.printf("MQTT baglanamadi. Durum kodu: %d\n", mqtt.state());
      Serial.println("PC IPv4 adresi, Mosquitto penceresi, port ve guvenlik duvari izinlerini kontrol et.");
    }
    return;
  }
  // Keep servicing MQTT between measurements; do not block for two seconds.
  if (!mqtt.loop()) return;
  const uint32_t now = millis();
  if (now - lastSendAt < SEND_INTERVAL_MS) return;
  lastSendAt = now;

  const float temperature = bme.readTemperature();
  const float humidity = bme.readHumidity();
  const float pressure = bme.readPressure() / 100.0F;
  if (!std::isfinite(temperature) || !std::isfinite(humidity) ||
      !std::isfinite(pressure) || humidity < 0 || humidity > 100) {
    Serial.println("Gecersiz BME280 olcumu; bu olcum gonderilmedi.");
    return;
  }
  const uint32_t currentSequence = ++sequenceNumber;
  char payload[320];
  const int length = std::snprintf(
      payload, sizeof(payload),
      "{\"device_id\":\"esp32_01\",\"sequence\":%lu,\"uptime_ms\":%lu,"
      "\"temperature_c\":%.2f,\"humidity_pct\":%.2f,\"pressure_hpa\":%.2f}",
      static_cast<unsigned long>(currentSequence),
      static_cast<unsigned long>(millis()),
      static_cast<double>(temperature), static_cast<double>(humidity),
      static_cast<double>(pressure));
  if (length <= 0 || static_cast<size_t>(length) >= sizeof(payload)) {
    Serial.println("JSON mesaji olusturulamadi.");
    return;
  }
  const bool written = mqtt.publish(MQTT_TOPIC, payload, false);
  Serial.printf("MQTT yerel yazma=%s | sira=%lu\n", written ? "OK" : "HATA",
                static_cast<unsigned long>(currentSequence));
  Serial.println(payload);
  Serial.println("Teslimi PC abone penceresinde kontrol et; CSV kaydi bu kodla dogrulanmaz.");
}
