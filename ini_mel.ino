#include <Arduino.h>
#include <Wire.h>
#include <BH1750.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>

// ========= CONFIG =========
// WiFi
const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";

// Antares MQTT (ubah sesuai akun/proyekmu)
const char* MQTT_BROKER   = "mqtt.antares.id";
const uint16_t MQTT_PORT  = 1883;
const char* MQTT_USER     = "YOUR_ANTARES_ACCESS_KEY"; // kosongkan "" jika tidak dipakai
const char* MQTT_PASS     = "";                        // isi bila perlu
const char* MQTT_CLIENTID = "esp8266-bh1750-01";
const char* MQTT_TOPIC    = "antares/YourProject/YourDevice"; // ganti

// I/O
#define LED_PIN D5      // GPIO14, PWM OK di ESP8266
#define SDA_PIN D2      // GPIO4
#define SCL_PIN D1      // GPIO5

// Mapping dan interval
const float  LUX_MAX    = 4000.0f;  // anggap 4000 lux = sangat terang
const uint8_t OUT_MIN   = 0;
const uint8_t OUT_MAX   = 225;      // permintaan user
const uint32_t PUBLISH_MS = 2000;   // publish tiap 2 detik
// ==========================

BH1750 lightMeter;
WiFiClient espClient;
PubSubClient mqtt(espClient);

uint32_t lastPublish = 0;

void wifiConnect() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("WiFi connecting to %s", WIFI_SSID);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500); Serial.print(".");
  }
  Serial.printf("\nWiFi connected. IP: %s\n", WiFi.localIP().toString().c_str());
}

void mqttConnect() {
  mqtt.setServer(MQTT_BROKER, MQTT_PORT);
  Serial.printf("MQTT connecting to %s:%u\n", MQTT_BROKER, MQTT_PORT);
  while (!mqtt.connected()) {
    if (mqtt.connect(MQTT_CLIENTID, MQTT_USER, MQTT_PASS)) {
      Serial.println("MQTT connected.");
    } else {
      Serial.printf("MQTT failed, rc=%d. Retry in 2s...\n", mqtt.state());
      delay(2000);
    }
  }
}

// Map lux -> 0..225 (terang -> kecil), sekaligus beri nilai PWM 0..1023
uint8_t mapLuxToOut(float lux) {
  if (lux < 0) lux = 0;
  if (lux > LUX_MAX) lux = LUX_MAX;
  float scaled = (lux / LUX_MAX) * OUT_MAX;  // 0..225 (gelap..terang)
  int inverted = (int)round(OUT_MAX - scaled);
  if (inverted < OUT_MIN) inverted = OUT_MIN;
  if (inverted > OUT_MAX) inverted = OUT_MAX;
  return (uint8_t)inverted;
}

void setup() {
  Serial.begin(115200);
  delay(200);

  // I2C & BH1750
  Wire.begin(SDA_PIN, SCL_PIN);
  if (lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE)) {
    Serial.println("BH1750 init OK");
  } else {
    Serial.println("BH1750 init FAILED (cek wiring/addr).");
  }

  // PWM LED (ESP8266: 10-bit, 0..1023)
  pinMode(LED_PIN, OUTPUT);
  analogWriteRange(1023);
  analogWriteFreq(1000);
  analogWrite(LED_PIN, 0);

  // WiFi & MQTT
  wifiConnect();
  mqttConnect();
}

void loop() {
  if (!mqtt.connected()) mqttConnect();
  mqtt.loop();

  float lux = lightMeter.readLightLevel();  // lux
  if (lux < 0) {
    Serial.println("BH1750 read error.");
    delay(200);
    return;
  }

  // Hitung nilai publish 0..225 (semakin terang -> makin kecil)
  uint8_t outVal = mapLuxToOut(lux);

  // Terapkan ke PWM (0..1023). Skala dari 0..225 -> 0..1023:
  int pwm = (int)round(outVal * 1023.0 / 255.0);  // 225 ≈ 903
  if (pwm < 0) pwm = 0;
  if (pwm > 1023) pwm = 1023;
  analogWrite(LED_PIN, pwm);

  // Publish periodik
  uint32_t now = millis();
  if (now - lastPublish >= PUBLISH_MS) {
    lastPublish = now;

    // JSON sederhana: lux & value 0..225
    char payload[128];
    snprintf(payload, sizeof(payload), "{\"lux\":%.1f,\"value\":%u}", lux, outVal);

    bool ok = mqtt.publish(MQTT_TOPIC, payload);
    Serial.printf("PUB → %s : %s  (%s)\n", MQTT_TOPIC, payload, ok ? "OK" : "FAIL");
  }

  delay(50);
}
