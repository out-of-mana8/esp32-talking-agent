#pragma once

// ─────────────────────────────────────────────
//  ESP32-Voicebox Pin Configuration
//  ⚠️ GPIO numbers TBD — fill in after bring-up
// ─────────────────────────────────────────────

// I²S — Microphones (ICS-43434)
#define PIN_I2S_MIC_SD      -1   // TODO
#define PIN_I2S_MIC_WS      -1   // TODO
#define PIN_I2S_MIC_SCK     -1   // TODO
#define PIN_MIC_EN          -1   // TODO — TPS22918 enable

// I²S — Amplifiers (MAX98357A)
#define PIN_I2S_SPK_DIN     -1   // TODO
#define PIN_I2S_SPK_LRCLK   -1   // TODO
#define PIN_I2S_SPK_BCLK    -1   // TODO
#define PIN_SPK_EN          -1   // TODO — amp SD/enable

// SPI — Display
#define PIN_SPI_MOSI        -1   // TODO
#define PIN_SPI_SCK         -1   // TODO
#define PIN_SPI_CS          -1   // TODO
#define PIN_TFT_RST         -1   // TODO
#define PIN_TFT_DC          -1   // TODO
#define PIN_TFT_DIM         -1   // TODO — PWM backlight
#define PIN_TFT_EN          -1   // TODO — TPS22918 enable

// I²C — Shared bus (IMU, Fuel Gauge, STEMMA)
#define PIN_I2C_SDA         -1   // TODO
#define PIN_I2C_SCL         -1   // TODO

// IMU Interrupts (LSM6DSOXTR)
#define PIN_IMU_INT1        -1   // TODO
#define PIN_IMU_INT2        -1   // TODO

// Fuel Gauge Alert (MAX17048)
#define PIN_FUEL_INT        -1   // TODO

// I²C Addresses
#define I2C_ADDR_MAX17048   0x36
#define I2C_ADDR_LSM6DS     0x6A  // or 0x6B depending on SA0
