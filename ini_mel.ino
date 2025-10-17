#include <Arduino.h>
#include <Wire.h>
#include <BH1750.h>
#include <WiFi.h>
#include <PubSubClient.h>

// ================== CONFIG ==================
/* WiFi */
const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";

/* Antares MQTT (isi sesuai akun/proyek kamu)
   Catatan:
   - Beberapa setup Antares pakai broker: "mqtt.antares.id", port 1883
   - username/password biasanya berupa "Access-Key" (API Key) sebagai username,
     password boleh dikosongkan atau sama (tergantung setting akun kamu)
   - TOPIC contoh (gaya sederhana): "antares/{projectName}/{deviceName}"
     Jika kamu pakai skema oneM2M, topik & payload beda (lihat catatan di bawah)
*/
const char* MQTT_BROKER = "mqtt.antares.id";
const uint16_t MQTT_PORT = 1883;
const char* MQTT_USER = "YOUR_ANTARES_ACCESS_KEY";   // atau kosongkan "" jika tidak dipakai
const char* MQTT_PASS = "";                           // isi bila diperlukan
const char* MQTT_CLIENT_ID = "esp32-bh1750-01";
const char* MQTT_TOPIC = "antares/YourProject/YourDevice";  // ganti sesuai project/device

/* Sensor & LED */
#define I2C_SDA 21
#define I2C_SCL 22
#define LED_PIN 5

// Rentang pemetaan dan interval publish
const float LUX_MAX = 4000.0f;    // lux dianggap terang pada 4000 lux (silakan sesuaikan)
const uint8_t OUT_MIN = 0;        // nilai terendah untuk publish/PWM
const uint8_t OUT_MAX = 225;      // Syarat user: 0..225 (bukan 255)
const uint32_t PUBLISH_MS = 2000; // publish setiap 2 detik
// ===========================================

BH1750 lightMeter;
WiFiClient espClient;
PubSubClient mqtt(espClient);

// LEDC (PWM) setup
const int LEDC_CHANNEL = 0;
const int LEDC_FREQ = 1000;      // 1 kHz
const int LEDC_RES_BITS = 8;     // 8-bit (0..255), tapi kita batasi 0..225

// Utils
uint32_t lastPublish = 0;

void wifiConnect() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("WiFi connecting to %s", WIFI_SSID);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.printf("\nWiFi connected. IP: %s\n", WiFi.localIP().toString().c_str());
}

void mqttConnect() {
  mqtt.setServer(MQTT_BROKER, MQTT_PORT);
  Serial.printf("MQTT connecting to %s:%u\n", MQTT_BROKER, MQTT_PORT);
  while (!mqtt.connected()) {
    if (mqtt.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASS)) {
      Serial.println("MQTT connected.");
    } else {
      Serial.printf("MQTT failed, rc=%d. Retry in 2s...\n", mqtt.state());
      delay(2000);
    }
  }
}

uint8_t mapLuxToOut(float lux) {
  // Batasi lux ke 0..LUX_MAX
  if (lux < 0) lux = 0;
  if (lux > LUX_MAX) lux = LUX_MAX;

  // Skala langsung 0..225 sesuai terang (terang -> nilai besar)
  float scaled = (lux / LUX_MAX) * OUT_MAX;   // 0 (gelap) .. 225 (terang)
  // Karena LED harus makin redup saat terang, kita balik:
  int inverted = (int)round(OUT_MAX - scaled);

  // Jaga batas
  if (inverted < OUT_MIN) inverted = OUT_MIN;
  if (inverted > OUT_MAX) inverted = OUT_MAX;
  return (uint8_t)inverted;
}

void setup() {
  Serial.begin(115200);
  delay(300);

  // I2C & BH1750
  Wire.begin(I2C_SDA, I2C_SCL);
  if (lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE)) {
    Serial.println("BH1750 init OK");
  } else {
    Serial.println("BH1750 init FAILED. Check wiring/addr.");
  }

  // LED PWM
  ledcSetup(LEDC_CHANNEL, LEDC_FREQ, LEDC_RES_BITS);
  ledcAttachPin(LED_PIN, LEDC_CHANNEL);
  ledcWrite(LEDC_CHANNEL, 0); // mulai OFF

  // WiFi & MQTT
  wifiConnect();
  mqttConnect();
}

void loop() {
  if (!mqtt.connected()) mqttConnect();
  mqtt.loop();

  // Baca lux
  float lux = lightMeter.readLightLevel(); // lux
  if (lux < 0) {
    // kadang library bisa return negatif jika error
    Serial.println("BH1750 read error, skip.");
    delay(200);
    return;
  }

  // Hitung nilai 0..225 (semakin terang -> nilai semakin kecil untuk PWM)
  uint8_t outVal = mapLuxToOut(lux);

  // Terapkan ke LED (0..225), karena LEDC 8-bit = 0..255, aman
  ledcWrite(LEDC_CHANNEL, outVal);

  // Publish berkala
  uint32_t now = millis();
  if (now - lastPublish >= PUBLISH_MS) {
    lastPublish = now;

    // Payload JSON sederhana
    // Kirim keduanya: lux & value 0..225 agar mudah debug di Antares
    // Contoh: {"lux":123.4,"value":198}
    char payload[128];
    snprintf(payload, sizeof(payload), "{\"lux\":%.1f,\"value\":%u}", lux, outVal);

    bool ok = mqtt.publish(MQTT_TOPIC, payload);
    Serial.printf("PUB → %s : %s  (%s)\n", MQTT_TOPIC, payload, ok ? "OK" : "FAIL");
  }

  delay(50);
}
