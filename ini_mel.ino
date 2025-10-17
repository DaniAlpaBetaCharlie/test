/*
  ESP8266 + BH1750 + LED (PWM) + AntaresESPMQTT (oneM2M)
  - Semakin terang -> LED makin redup
  - Publish ke Antares nilai 0..225 (field: "value") + lux
  - MQTT server: platform.antares.id, port 1338 (handled by antares.setMqttServer())
  - Topic (di-handle lib): /oneM2M/req/ACCESSKEY/antares-cse/json
*/

#include <Arduino.h>
#include <Wire.h>
#include <BH1750.h>
//#include <ESP8266WiFi.h>
#include <AntaresESPMQTT.h>

// ======== GANTI DENGAN KREDENSIAL KAMU ========
#define ACCESSKEY   "YOUR-ACCESS-KEY"     // Antares Access Key
#define WIFISSID    "YOUR-WIFI-SSID"
#define PASSWORD    "YOUR-WIFI-PASSWORD"

#define projectName "YOUR-APPLICATION-NAME" // Nama Application di Antares
#define deviceName  "YOUR-DEVICE-NAME"      // Nama Device di Antares
// ==============================================

// Pin (NodeMCU)
#define LED_PIN   D5   // GPIO14, PWM OK
#define SDA_PIN   D2   // GPIO4
#define SCL_PIN   D1   // GPIO5

// Mapping & interval
const float   LUX_MAX     = 4000.0f;   // anggap 4000 lux = sangat terang (silakan sesuaikan)
const uint8_t OUT_MIN     = 0;
const uint8_t OUT_MAX     = 225;       // requirement user
const uint32_t PUBLISH_MS = 2000;      // publish tiap 2 detik

BH1750 lightMeter;
AntaresESPMQTT antares(ACCESSKEY);

uint32_t lastPub = 0;

// Map lux -> 0..225 terbalik (terang -> kecil), untuk publish & PWM basis
uint8_t mapLuxToOut(float lux) {
  if (lux < 0) lux = 0;
  if (lux > LUX_MAX) lux = LUX_MAX;
  float scaled = (lux / LUX_MAX) * OUT_MAX; // 0..225 (gelap..terang)
  int inverted = (int)round(OUT_MAX - scaled);
  if (inverted < OUT_MIN) inverted = OUT_MIN;
  if (inverted > OUT_MAX) inverted = OUT_MAX;
  return (uint8_t)inverted;
}

void setup() {
  Serial.begin(115200);
  delay(200);

  // I2C + BH1750
  Wire.begin(SDA_PIN, SCL_PIN);
  if (lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE)) {
    Serial.println("BH1750 init OK");
  } else {
    Serial.println("BH1750 init FAILED (cek wiring/addr)");
  }

  // LED PWM (ESP8266: 0..1023)
  pinMode(LED_PIN, OUTPUT);
  analogWriteRange(1023);
  analogWriteFreq(1000);
  analogWrite(LED_PIN, 0);

  // Antares WiFi + MQTT (oneM2M)
  antares.setDebug(true);
  antares.wifiConnection(WIFISSID, PASSWORD);
  antares.setMqttServer(); // platform.antares.id:1338 + topic oneM2M default
}

void loop() {
  // Pastikan koneksi MQTT tetap hidup
  antares.checkMqttConnection();

  // Baca lux
  float lux = lightMeter.readLightLevel(); // lux
  if (lux < 0) {
    Serial.println("BH1750 read error.");
    delay(200);
    return;
  }

  // Hitung nilai 0..225 (semakin terang → makin kecil)
  uint8_t value225 = mapLuxToOut(lux);

  // Terapkan ke LED (PWM 0..1023). Skala dari 0..225 -> 0..1023
  int pwm = (int)round(value225 * 1023.0 / 255.0); // 225 ~ 903
  if (pwm < 0) pwm = 0;
  if (pwm > 1023) pwm = 1023;
  analogWrite(LED_PIN, pwm);

  // Publish periodik ke Antares (oneM2M handled by library)
  uint32_t now = millis();
  if (now - lastPub >= PUBLISH_MS) {
    lastPub = now;

    // Tambah field ke buffer Antares
    antares.add("lux", lux);           // float ok
    antares.add("value", (int)value225); // 0..225 sesuai permintaan
    antares.add("pwm", pwm);           // opsional: untuk debug sisi dashboard

    // Kirim
    antares.publish(projectName, deviceName);

    // Log serial
    Serial.printf("PUB Antares  lux=%.1f  value=%u  pwm=%d\n", lux, value225, pwm);
  }

  delay(30);
}
