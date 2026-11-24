// ESP32-WROOM-32D: first local sensor test, no Wi-Fi required.
// Soil module: VCC=3V3, GND=GND, AO=GPIO34. DO is unused.
// BME280: VCC=3V3, GND=GND, SDA/SDI=GPIO21, SCL/SCK=GPIO22.
// For the six-pin BME280 module: CSB=3V3, SDO=GND (address 0x76).
// Libraries: Adafruit BME280 Library, Adafruit Unified Sensor, Adafruit BusIO.
// Serial Monitor: 115200 baud. Soil output is raw ADC data, NOT a percentage.

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_BME280.h>
#include <cmath>

constexpr uint8_t SOIL_PIN = 34;
constexpr uint8_t SDA_PIN = 21;
constexpr uint8_t SCL_PIN = 22;

Adafruit_BME280 bme;
bool bmeReady = false;

void setup() {
  Serial.begin(115200);
  delay(1200);
  Serial.println("\n--- Prototip sensor testi ---");

  pinMode(SOIL_PIN, INPUT);
  analogReadResolution(12);
  analogSetPinAttenuation(SOIL_PIN, ADC_11db);

  const bool wireReady = Wire.begin(SDA_PIN, SCL_PIN, 100000);
  Wire.setTimeOut(50);
  const uint8_t addresses[] = {0x76, 0x77};

  if (wireReady) {
    for (uint8_t address : addresses) {
      Wire.beginTransmission(address);
      if (Wire.endTransmission() != 0) {
        continue;
      }

      Serial.print("I2C cihaz adresi: 0x");
      Serial.println(address, HEX);
      if (bme.begin(address, &Wire)) {
        bmeReady = true;
        Serial.println("BME280 hazir.");
        break;
      }

      Serial.print("Bu adreste BME280 taninamadi. Sensor ID: 0x");
      Serial.println(bme.sensorID(), HEX);
    }
  }

  if (!bmeReady) {
    Serial.println("BME280 bulunamadi: guc, SDA/SCL ve modul pinlerini kontrol et.");
    Serial.println("Toprak nemi testi devam edecek.");
  }
  Serial.println("Toprak_ham: 0-4095; yuzde icin daha sonra kalibrasyon yapilacak.");
}

void loop() {
  const uint16_t soilRaw = analogRead(SOIL_PIN);
  Serial.print("Toprak_ham=");
  Serial.print(soilRaw);

  if (bmeReady) {
    const float temperature = bme.readTemperature();
    const float humidity = bme.readHumidity();
    const float pressure = bme.readPressure() / 100.0F;

    if (std::isfinite(temperature) && std::isfinite(humidity) && std::isfinite(pressure)) {
      Serial.print(" | Sicaklik_C=");
      Serial.print(temperature, 1);
      Serial.print(" | Hava_nemi_pct=");
      Serial.print(humidity, 1);
      Serial.print(" | Basinc_hPa=");
      Serial.print(pressure, 1);
    } else {
      Serial.print(" | BME280=okuma_hatasi");
    }
  } else {
    Serial.print(" | BME280=bulunamadi");
  }

  Serial.println();
  delay(1000);
}
