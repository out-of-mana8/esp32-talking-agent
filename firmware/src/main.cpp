#include "Arduino.h"
#include "config.h"
#include <Wire.h>
#include <Adafruit_LSM6DSOX.h>

// ─────────────────────────────────────────────
//  Forward declarations
// ─────────────────────────────────────────────
void initPower();
void initAudio();
void initDisplay();
void initIMU();
void initLEDs();

Adafruit_LSM6DSOX imu;

// ─────────────────────────────────────────────
//  Setup
// ─────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    Serial.println("[BOOT] ESP32-Voicebox v1.0");

    initPower();
    initLEDs();
    initDisplay();
    initIMU();
    initAudio();

    Serial.println("[BOOT] All subsystems initialized");
}

// ─────────────────────────────────────────────
//  Loop
// ─────────────────────────────────────────────
void loop() {
    sensors_event_t accel, gyro, temp;
    imu.getEvent(&accel, &gyro, &temp);

    // CSV format for visualizer: ax,ay,az,gx,gy,gz
    Serial.printf("%.3f,%.3f,%.3f,%.3f,%.3f,%.3f\n",
        accel.acceleration.x, accel.acceleration.y, accel.acceleration.z,
        gyro.gyro.x, gyro.gyro.y, gyro.gyro.z);

    delay(100);
}

// ─────────────────────────────────────────────
//  Subsystem Init
// ─────────────────────────────────────────────
void initPower() {
    // Enable load switches
    pinMode(PIN_MIC_EN, OUTPUT);
    pinMode(PIN_TFT_EN, OUTPUT);

    digitalWrite(PIN_TFT_EN, HIGH);
    digitalWrite(PIN_MIC_EN, HIGH);

    // TODO: Init MAX17048 fuel gauge over I2C
    Serial.println("[PWR] Power init done");
}

void initAudio() {
    // TODO: Configure I2S for microphones (ICS-43434)
    // TODO: Configure I2S for amplifiers (MAX98357A)
    // TODO: Set SPK_EN
    Serial.println("[AUDIO] Audio init done");
}

void initDisplay() {
    // TODO: Init SPI display
    // TODO: Set up PWM on PIN_TFT_DIM for backlight
    Serial.println("[DISPLAY] Display init done");
}

void initLEDs() {
    pinMode(PIN_LED_1, OUTPUT);
    pinMode(PIN_LED_2, OUTPUT);
    digitalWrite(PIN_LED_1, LOW);
    digitalWrite(PIN_LED_2, LOW);
    Serial.println("[LED] LED init done");
}

void initIMU() {
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);

    if (!imu.begin_I2C(I2C_ADDR_LSM6DS, &Wire)) {
        Serial.println("[IMU] ERROR: LSM6DSOX not found — check wiring/address");
        while (1) delay(10);
    }

    imu.setAccelRange(LSM6DS_ACCEL_RANGE_4_G);
    imu.setGyroRange(LSM6DS_GYRO_RANGE_500_DPS);
    imu.setAccelDataRate(LSM6DS_RATE_104_HZ);
    imu.setGyroDataRate(LSM6DS_RATE_104_HZ);

    Serial.println("[IMU] LSM6DSOX online");
}
