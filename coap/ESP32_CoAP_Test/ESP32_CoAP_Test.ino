// One-shot connectivity pilot. Confirmable POST, one transmission per boot.
#include <WiFi.h>
#include <WiFiUdp.h>
#include <Wire.h>
#include <Adafruit_BME280.h>
#include <coap-simple.h>
#include <esp_system.h>
#include <math.h>

const char* WIFI_SSID = "WIFI_ADINI_YAZ";
const char* WIFI_PASSWORD = "WIFI_SIFRENI_YAZ";
IPAddress receiver(192, 168, 1, 48);
constexpr uint16_t port = 5683;
WiFiUDP udp;
Coap coap(udp, 512);
Adafruit_BME280 bme;
uint32_t token;
bool waiting = false;
unsigned long sentAt;

void handleReply(CoapPacket &packet, IPAddress ip, int remotePort) {
  if (!waiting || ip != receiver || remotePort != port ||
      packet.tokenlen != sizeof(token) || memcmp(packet.token, &token, sizeof(token)) != 0) return;
  if ((packet.code >> 5) < 2) return;
  waiting = false;
  Serial.printf("Alici yaniti: %u.%02u | ", packet.code >> 5, packet.code & 31);
  Serial.write(packet.payload, packet.payloadlen);
  Serial.println();
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Wire.begin(21, 22);
  if (!bme.begin(0x76, &Wire) && !bme.begin(0x77, &Wire)) {
    Serial.println("BME280 bulunamadi."); return;
  }
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) delay(100);
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Wi-Fi baglanamadi."); return;
  }
  coap.response(handleReply);
  coap.start(56830);
  float t = bme.readTemperature(), h = bme.readHumidity(), p = bme.readPressure() / 100.0f;
  if (!isfinite(t) || !isfinite(h) || !isfinite(p)) {
    Serial.println("Gecersiz sensor olcumu."); return;
  }
  char payload[240];
  int n = snprintf(payload, sizeof(payload),
    "{\"device_id\":\"esp32_01\",\"sequence\":1,\"uptime_ms\":%lu,"
    "\"temperature_c\":%.2f,\"humidity_pct\":%.2f,\"pressure_hpa\":%.2f}", millis(), t, h, p);
  if (n <= 0 || n >= sizeof(payload)) return;
  token = esp_random();
  sentAt = millis();
  waiting = true;
  coap.send(receiver, port, "sensor", COAP_CON, COAP_POST,
            reinterpret_cast<uint8_t*>(&token), sizeof(token),
            reinterpret_cast<uint8_t*>(payload), n, static_cast<COAP_CONTENT_TYPE>(50),
            static_cast<uint16_t>(esp_random()));
  Serial.println("Olcum gonderimi denendi | sira=1");
}

void loop() {
  if (waiting) {
    coap.loop();
    if (waiting && millis() - sentAt >= 10000) {
      waiting = false;
      Serial.println("10 saniyede onay gelmedi. PC alici penceresini kontrol et.");
    }
  }
  delay(2);
}
