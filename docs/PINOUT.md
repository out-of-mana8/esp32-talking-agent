# ESP32-S3 Pin Assignments

> ⚠️ GPIO numbers to be confirmed during firmware bring-up. Some pins are constrained by ESP32-S3 strapping pin requirements.

---

## I²S Audio — Microphones

| Signal      | GPIO | Notes                        |
|-------------|------|------------------------------|
| I2S_MIC_SD  | TBD  | Data (L/R set by SD pin)     |
| I2S_MIC_WS  | TBD  | Word Select (LRCLK)          |
| I2S_MIC_SCK | TBD  | Bit Clock                    |
| MIC_EN      | TBD  | Power gate (active high)     |

## I²S Audio — Amplifiers

| Signal         | GPIO | Notes                      |
|----------------|------|----------------------------|
| I2S_SPK_DIN    | TBD  | Data to both amps          |
| I2S_SPK_LRCLK  | TBD  | LR Clock                   |
| I2S_SPK_BCLK   | TBD  | Bit Clock                  |
| SPK_EN         | TBD  | Amp enable (via SD pin)    |

## SPI — Display

| Signal   | GPIO | Notes                      |
|----------|------|----------------------------|
| SPI_MOSI | TBD  | Display data               |
| SPI_SCK  | TBD  | Display clock              |
| SPI_CS   | TBD  | Chip select                |
| TFT_RST  | TBD  | Reset                      |
| TFT_DC   | TBD  | Data/Command select        |
| TFT_DIM  | TBD  | Backlight PWM              |
| TFT_EN   | TBD  | Display power gate         |

## I²C — Shared Bus

| Signal | GPIO | Notes                              |
|--------|------|------------------------------------|
| SDA    | TBD  | IMU, Fuel Gauge, STEMMA QT         |
| SCL    | TBD  | IMU, Fuel Gauge, STEMMA QT         |

## IMU Interrupts

| Signal    | GPIO | Notes            |
|-----------|------|------------------|
| IMU_INT1  | TBD  | Motion interrupt |
| IMU_INT2  | TBD  | Wake interrupt   |

## Power / System

| Signal    | GPIO | Notes                         |
|-----------|------|-------------------------------|
| FUEL_INT  | TBD  | MAX17048 low battery alert    |
| BOOT      | 0    | Boot mode (strapping pin)     |
| RESET     | EN   | System reset                  |

## Expansion

| Signal     | Connector | Notes                     |
|------------|-----------|---------------------------|
| SDA / SCL  | STEMMA1   | Qwiic/STEMMA QT port 1    |
| SDA / SCL  | STEMMA2   | Qwiic/STEMMA QT port 2    |

---

## ESP32-S3 Strapping Pins to Avoid

Avoid using these for general purpose I/O:

| GPIO | Strapping Function         |
|------|---------------------------|
| 0    | Boot mode (BOOT button)   |
| 3    | JTAG control              |
| 45   | VDD_SPI voltage select    |
| 46   | ROM messages              |
