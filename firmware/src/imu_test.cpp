//
// imu_test.cpp — LSM6DSOX IMU diagnostic / WiFi streamer
// Flash with: pio run -e imu-test -t upload
//
// Mode is selected by a build flag in platformio.ini [env:imu-test]:
//
//   (default)      Serial only — I2C scan, verbose table + bar graphs at 10 Hz
//   -DIMU_WIFI=1   WiFi UDP    — streams CSV to imu_visualizer.py at 100 Hz,
//                                plus a brief serial summary every 2 s
//
// Visualizer:
//   Serial mode  →  pio device monitor
//   WiFi mode    →  python imu_visualizer.py udp
//
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_LSM6DSOX.h>
#include "config.h"

#ifdef IMU_WIFI
  #include <WiFi.h>
  #include <WiFiUdp.h>
  #include "wifi_config.h"
  #define RECONNECT_MS 5000
  static WiFiUDP   udp;
  static IPAddress broadcast;
#endif

Adafruit_LSM6DSOX imu;

// ── helpers ───────────────────────────────────────────────────
static void printBar(float val, float range, uint8_t width = 32) {
    float pct = constrain((val + range) / (2.0f * range), 0.0f, 1.0f);
    int mid    = width / 2;
    int fill   = (int)(pct * width);
    Serial.print('[');
    for (int i = 0; i < width; i++) {
        if      (i == mid)                          Serial.print('|');
        else if (fill > mid && i >= mid && i < fill) Serial.print('#');
        else if (fill < mid && i >= fill && i < mid) Serial.print('#');
        else                                         Serial.print(' ');
    }
    Serial.print(']');
}

static void i2cScan() {
    Serial.println("\n[I2C] Scanning bus...");
    uint8_t found = 0;
    for (uint8_t addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            Serial.printf("  0x%02X", addr);
            if      (addr == I2C_ADDR_LSM6DS)  Serial.print("  ← LSM6DSOX");
            else if (addr == I2C_ADDR_MAX17048) Serial.print("  ← MAX17048");
            Serial.println();
            found++;
        }
    }
    Serial.printf("[I2C] %d device(s) found\n\n", found);
}

#ifdef IMU_WIFI
static void connectWiFi() {
    Serial.printf("[WIFI] Connecting to \"%s\"", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - t0 > 15000) {
            Serial.println("\n[WIFI] Timeout — retrying...");
            WiFi.disconnect();
            delay(RECONNECT_MS);
            WiFi.begin(WIFI_SSID, WIFI_PASS);
            t0 = millis();
        }
        Serial.print('.');
        delay(500);
    }
    IPAddress ip   = WiFi.localIP();
    IPAddress mask = WiFi.subnetMask();
    broadcast = IPAddress(
        (ip[0] & mask[0]) | (~mask[0] & 0xFF),
        (ip[1] & mask[1]) | (~mask[1] & 0xFF),
        (ip[2] & mask[2]) | (~mask[2] & 0xFF),
        (ip[3] & mask[3]) | (~mask[3] & 0xFF)
    );
    Serial.printf("\n[WIFI] Connected  IP %s  →  broadcast %s:%d\n",
                  ip.toString().c_str(), broadcast.toString().c_str(), IMU_UDP_PORT);
}
#endif

// ── setup ─────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(400);

    Serial.println("========================================");
#ifdef IMU_WIFI
    Serial.println("  LSM6DSOX  IMU  [WiFi UDP mode]");
#else
    Serial.println("  LSM6DSOX  IMU  [Serial mode]");
#endif
    Serial.println("========================================");
    Serial.printf("  SDA: GPIO%d   SCL: GPIO%d\n",  PIN_I2C_SDA,  PIN_I2C_SCL);
    Serial.printf("  INT1: GPIO%d  INT2: GPIO%d\n",  PIN_IMU_INT1, PIN_IMU_INT2);
    Serial.println("========================================\n");

    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    i2cScan();

    Serial.printf("[IMU] Initializing at 0x%02X ... ", I2C_ADDR_LSM6DS);
    if (!imu.begin_I2C(I2C_ADDR_LSM6DS, &Wire)) {
        Serial.println("FAILED — check wiring / SA0 pin");
        while (1) delay(100);
    }
    Serial.println("OK");

    imu.setAccelRange(LSM6DS_ACCEL_RANGE_4_G);
    imu.setGyroRange(LSM6DS_GYRO_RANGE_500_DPS);
    imu.setAccelDataRate(LSM6DS_RATE_104_HZ);
    imu.setGyroDataRate(LSM6DS_RATE_104_HZ);

    Serial.println("[IMU] ±4 g  |  ±500 dps  |  104 Hz");

    pinMode(PIN_IMU_INT1, INPUT);
    pinMode(PIN_IMU_INT2, INPUT);

#ifdef IMU_WIFI
    connectWiFi();
    udp.begin(IMU_UDP_PORT);
    Serial.printf("[UDP] Socket open — streaming to broadcast:%d\n", IMU_UDP_PORT);
    Serial.println("      Run: python imu_visualizer.py udp\n");
#else
    Serial.println("\n--- streaming (10 Hz) ---");
    Serial.println("         ax       ay       az      |       gx       gy       gz      | temp");
#endif
}

// ── loop ──────────────────────────────────────────────────────
static uint32_t lastTick     = 0;
static uint32_t lastSummary  = 0;
static uint32_t sample       = 0;

#ifdef IMU_WIFI
static uint32_t lastWifiCheck = 0;
#endif

void loop() {
    uint32_t now = millis();

#ifdef IMU_WIFI
    // WiFi watchdog
    if (now - lastWifiCheck >= RECONNECT_MS) {
        lastWifiCheck = now;
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("[WIFI] Lost — reconnecting...");
            connectWiFi();
        }
    }

    // 100 Hz UDP stream
    if (now - lastTick >= 10) {
        lastTick = now;
        if (WiFi.status() != WL_CONNECTED) return;

        sensors_event_t accel, gyro, temp;
        imu.getEvent(&accel, &gyro, &temp);

        char buf[80];
        int  len = snprintf(buf, sizeof(buf),
            "%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.1f\n",
            accel.acceleration.x, accel.acceleration.y, accel.acceleration.z,
            gyro.gyro.x, gyro.gyro.y, gyro.gyro.z,
            temp.temperature);
        udp.beginPacket(broadcast, IMU_UDP_PORT);
        udp.write((const uint8_t *)buf, len);
        udp.endPacket();

        // Brief serial summary every 2 s
        if (now - lastSummary >= 2000) {
            lastSummary = now;
            Serial.printf("[IMU] ax=%+.2f ay=%+.2f az=%+.2f  gx=%+.3f gy=%+.3f gz=%+.3f  %.1f°C\n",
                accel.acceleration.x, accel.acceleration.y, accel.acceleration.z,
                gyro.gyro.x, gyro.gyro.y, gyro.gyro.z,
                temp.temperature);
        }
    }

#else   // ── Serial mode ──────────────────────────────────────

    if (now - lastTick >= 100) {   // 10 Hz
        lastTick = now;
        sample++;

        sensors_event_t accel, gyro, temp;
        imu.getEvent(&accel, &gyro, &temp);

        float ax = accel.acceleration.x, ay = accel.acceleration.y, az = accel.acceleration.z;
        float gx = gyro.gyro.x,          gy = gyro.gyro.y,          gz = gyro.gyro.z;
        float t  = temp.temperature;

        Serial.printf("%6u  %+7.3f  %+7.3f  %+7.3f  |  %+7.3f  %+7.3f  %+7.3f  | %+5.1f°C\n",
                      sample, ax, ay, az, gx, gy, gz, t);

        if (sample % 5 == 0) {
            Serial.println();
            Serial.printf("  AX "); printBar(ax, 20); Serial.printf(" %+6.2f m/s²\n", ax);
            Serial.printf("  AY "); printBar(ay, 20); Serial.printf(" %+6.2f m/s²\n", ay);
            Serial.printf("  AZ "); printBar(az, 20); Serial.printf(" %+6.2f m/s²\n", az);
            Serial.printf("  GX "); printBar(gx, 8.73f); Serial.printf(" %+6.3f rad/s\n", gx);
            Serial.printf("  GY "); printBar(gy, 8.73f); Serial.printf(" %+6.3f rad/s\n", gy);
            Serial.printf("  GZ "); printBar(gz, 8.73f); Serial.printf(" %+6.3f rad/s\n", gz);
            Serial.printf("  INT1=%d  INT2=%d\n\n",
                          digitalRead(PIN_IMU_INT1), digitalRead(PIN_IMU_INT2));
        }
    }
#endif
}
