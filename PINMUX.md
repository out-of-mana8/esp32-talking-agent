# esp32-voice-agent — Pinmux

This file lists the GPIO assignments for the esp32-voice-agent hardware.

| Signal Name | ESP32 Pin / GPIO | Function |
|---|---:|---|
| SDA | IO5 | I2C Data (Sensors/Fuel Gauge) |
| SCL | IO4 | I2C Clock (Sensors/Fuel Gauge) |
| MIC_EN | IO6 | Microphone Power Enable (Load Switch) |
| I2S_MIC_SCK | IO7 | I2S Microphone Bit Clock |
| I2S_MIC_SD | IO15 | I2S Microphone Serial Data |
| I2S_MIC_WS | IO16 | I2S Microphone Word Select (L/R Clock) |
| FUEL_INT | IO17 | Battery Fuel Gauge Interrupt |
| IMU_INT1 | IO18 | IMU Interrupt 1 |
| IMU_INT2 | IO8 | IMU Interrupt 2 |
| SPK1 | IO41 | Speaker 1 Control / Audio |
| SPK2 | IO40 | Speaker 2 Control / Audio |
| TFT_DIM | IO39 | Display Backlight PWM Dimming |
| TFT_EN | IO38 | Display Power Enable (Load Switch) |
| TFT_RST | IO48 | Display Reset |
| TFT_DC | IO47 | Display Data/Command Select |
| TFT_CS | IO21 | Display SPI Chip Select |
| SPI_MOSI | IO14 | Display SPI Data |
| SPI_SCK | IO13 | Display SPI Clock |
| I2S_SPK_DIN | IO12 | I2S Speaker Data In |
| I2S_SPK_LRCLK | IO11 | I2S Speaker Left/Right Clock |
| I2S_SPK_BCLK | IO10 | I2S Speaker Bit Clock |
| SPK_EN | IO9 | Audio Amplifier Enable |
| DN / DP | IO19 / IO20 | USB Data Negative / Positive |
| TX / RX | TXD0 / RXD0 | Hardware UART for Debugging |
| LED | IO1, IO2, IO42 | Status LEDs |

Notes:
- Pin names use the board `IO#` notation matching the hardware schematic.
- `TX / RX` are the UART0 pins (labelled `TXD0` / `RXD0`).

If you prefer this section included directly in the top-level README, see the suggested snippet in `README_PINMUX_SNIPPET.md`.
