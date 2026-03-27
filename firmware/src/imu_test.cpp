//
// imu_test.cpp — LSM6DSOX standalone diagnostic sketch
// Flash with: pio run -e imu-test -t upload
//
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_LSM6DSOX.h>
#include "config.h"

Adafruit_LSM6DSOX imu;

// ── helpers ───────────────────────────────────────────────
static void printBar(float val, float range, uint8_t width = 32) {
    float pct  = constrain((val + range) / (2.0f * range), 0.0f, 1.0f);
    int   mid  = width / 2;
    int   fill = (int)(pct * width);

    Serial.print('[');
    for (int i = 0; i < width; i++) {
        if (i == mid)                          Serial.print('|');
        else if (fill > mid && i >= mid && i < fill) Serial.print('#');
        else if (fill < mid && i >= fill && i < mid) Serial.print('#');
        else                                   Serial.print(' ');
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
            if      (addr == I2C_ADDR_LSM6DS)   Serial.print("  ← LSM6DSOX");
            else if (addr == I2C_ADDR_MAX17048)  Serial.print("  ← MAX17048");
            Serial.println();
            found++;
        }
    }
    Serial.printf("[I2C] %d device(s) found\n\n", found);
}

// ── setup ─────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(500);

    Serial.println("========================================");
    Serial.println("  LSM6DSOX  IMU  Diagnostic");
    Serial.println("========================================");
    Serial.printf("  SDA: GPIO%d   SCL: GPIO%d\n", PIN_I2C_SDA, PIN_I2C_SCL);
    Serial.printf("  INT1: GPIO%d  INT2: GPIO%d\n", PIN_IMU_INT1, PIN_IMU_INT2);
    Serial.println("========================================\n");

    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    i2cScan();

    Serial.print("[IMU] Initializing LSM6DSOX at 0x");
    Serial.print(I2C_ADDR_LSM6DS, HEX);
    Serial.print("... ");

    if (!imu.begin_I2C(I2C_ADDR_LSM6DS, &Wire)) {
        Serial.println("FAILED");
        Serial.println("[IMU] Check: address (SA0 pin), wiring, power");
        while (1) delay(100);
    }
    Serial.println("OK");

    imu.setAccelRange(LSM6DS_ACCEL_RANGE_4_G);
    imu.setGyroRange(LSM6DS_GYRO_RANGE_500_DPS);
    imu.setAccelDataRate(LSM6DS_RATE_104_HZ);
    imu.setGyroDataRate(LSM6DS_RATE_104_HZ);

    Serial.printf("[IMU] Accel range : ±4 g\n");
    Serial.printf("[IMU] Gyro  range : ±500 dps\n");
    Serial.printf("[IMU] Data rate   : 104 Hz\n\n");

    // Interrupt pins as inputs (active-high by default on LSM6DSOX)
    pinMode(PIN_IMU_INT1, INPUT);
    pinMode(PIN_IMU_INT2, INPUT);

    Serial.println("-------- streaming (10 Hz) --------");
    Serial.println("         ax       ay       az      |       gx       gy       gz      | temp");
}

// ── loop ──────────────────────────────────────────────────
static uint32_t lastPrint = 0;
static uint32_t sample    = 0;

void loop() {
    sensors_event_t accel, gyro, temp;
    imu.getEvent(&accel, &gyro, &temp);

    if (millis() - lastPrint >= 100) {   // 10 Hz print rate
        lastPrint = millis();
        sample++;

        float ax = accel.acceleration.x;
        float ay = accel.acceleration.y;
        float az = accel.acceleration.z;
        float gx = gyro.gyro.x;
        float gy = gyro.gyro.y;
        float gz = gyro.gyro.z;
        float t  = temp.temperature;

        // numeric line
        Serial.printf("%6u  %+7.3f  %+7.3f  %+7.3f  |  %+7.3f  %+7.3f  %+7.3f  | %+5.1f°C\n",
                      sample, ax, ay, az, gx, gy, gz, t);

        // bar graph every 5 samples
        if (sample % 5 == 0) {
            Serial.println();
            Serial.printf("  AX "); printBar(ax, 20); Serial.printf(" %+6.2f m/s²\n", ax);
            Serial.printf("  AY "); printBar(ay, 20); Serial.printf(" %+6.2f m/s²\n", ay);
            Serial.printf("  AZ "); printBar(az, 20); Serial.printf(" %+6.2f m/s²\n", az);
            Serial.printf("  GX "); printBar(gx, 8);  Serial.printf(" %+6.3f rad/s\n", gx);
            Serial.printf("  GY "); printBar(gy, 8);  Serial.printf(" %+6.3f rad/s\n", gy);
            Serial.printf("  GZ "); printBar(gz, 8);  Serial.printf(" %+6.3f rad/s\n", gz);
            Serial.printf("  INT1=%d  INT2=%d\n", digitalRead(PIN_IMU_INT1), digitalRead(PIN_IMU_INT2));
            Serial.println();
        }
    }
}
