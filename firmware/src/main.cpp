#include "Arduino.h"
#include "config.h"

// ─────────────────────────────────────────────
//  Forward declarations
// ─────────────────────────────────────────────
void initPower();
void initAudio();
void initDisplay();
void initIMU();

// ─────────────────────────────────────────────
//  Setup
// ─────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    Serial.println("[BOOT] ESP32-Voicebox v1.0");

    initPower();
    initDisplay();
    initIMU();
    initAudio();

    Serial.println("[BOOT] All subsystems initialized");
}

// ─────────────────────────────────────────────
//  Loop
// ─────────────────────────────────────────────
void loop() {
    // Main application logic here
    delay(10);
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

void initIMU() {
    // TODO: Init LSM6DSOXTR over I2C
    // TODO: Configure interrupt pins IMU_INT1, IMU_INT2
    Serial.println("[IMU] IMU init done");
}
