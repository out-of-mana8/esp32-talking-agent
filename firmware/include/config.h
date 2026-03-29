#pragma once

// ─────────────────────────────────────────────
//  ESP32 Talking Agent Pin Configuration
//  GPIO assignments from PINMUX.md
// ─────────────────────────────────────────────

// I²S — Microphones (ICS-43434)
#define PIN_I2S_MIC_SD      15   // I2S Microphone Serial Data
#define PIN_I2S_MIC_WS      16   // I2S Microphone Word Select (L/R Clock)
#define PIN_I2S_MIC_SCK      7   // I2S Microphone Bit Clock
#define PIN_MIC_EN           6   // TPS22918 enable — microphone power

// I²S — Amplifiers (MAX98357A)
#define PIN_I2S_SPK_DIN     12   // I2S Speaker Data In
#define PIN_I2S_SPK_LRCLK   11   // I2S Speaker Left/Right Clock
#define PIN_I2S_SPK_BCLK    10   // I2S Speaker Bit Clock
#define PIN_SPK_EN           9   // Audio amplifier SD/enable

// SPI — Display
#define PIN_SPI_MOSI        14   // Display SPI Data
#define PIN_SPI_SCK         13   // Display SPI Clock
#define PIN_SPI_CS          21   // Display SPI Chip Select
#define PIN_TFT_RST         48   // Display Reset
#define PIN_TFT_DC          47   // Display Data/Command Select
#define PIN_TFT_DIM         39   // PWM backlight dimming
#define PIN_TFT_EN          38   // TPS22918 enable — display power

// I²C — Shared bus (IMU, Fuel Gauge, STEMMA)
#define PIN_I2C_SDA          5
#define PIN_I2C_SCL          4

// IMU Interrupts (LSM6DSOXTR)
#define PIN_IMU_INT1        18
#define PIN_IMU_INT2         8

// Fuel Gauge Alert (MAX17048)
#define PIN_FUEL_INT        17

// Status LEDs
#define PIN_LED_1            1
#define PIN_LED_2            2
#define PIN_LED_3           42

// I²C Addresses
#define I2C_ADDR_MAX17048   0x36
#define I2C_ADDR_LSM6DS     0x6A  // or 0x6B depending on SA0
