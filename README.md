# 🎙️ ESP32-Voicebox

> A compact, battery-powered voice assistant platform built around the ESP32-S3, featuring stereo I²S audio in/out, a TFT display, 6-axis IMU, and USB-C charging.

![Board Version](https://img.shields.io/badge/PCB-v1.0-blue)
![MCU](https://img.shields.io/badge/MCU-ESP32--S3-red)
![License](https://img.shields.io/badge/license-MIT-green)

---

## ✨ Features

- **ESP32-S3-WROOM-1-N18R8** — 18MB Flash, 8MB PSRAM, dual-core 240MHz
- **Stereo I²S Microphones** — 2× ICS-43434 MEMS mics (L/R)
- **Stereo I²S Amplifiers** — 2× MAX98357A Class-D amps with EMI filtering
- **TFT Display** — SPI-connected with PWM backlight dimming
- **6-Axis IMU** — LSM6DSOXTR (accel + gyro) over I²C
- **USB-C** — Power input with CC resistors for proper PD detection
- **LiPo Charging** — MCP73831T single-cell charger
- **Battery Fuel Gauge** — MAX17048 over I²C
- **STEMMA QT / Qwiic** — 2× I²C expansion connectors
- **Power Gating** — TPS22918 load switches on mics, display, and amps

---

## 📸 Media

> Photos and demo video coming after first PCB assembly

---

## 📁 Repository Structure

```
├── firmware/
│   ├── src/
│   │   ├── main.cpp          # Main application (subsystem boot sequence)
│   │   ├── imu_test.cpp      # IMU diagnostic — serial table or WiFi UDP
│   │   └── mic_test.cpp      # Mic capture — record to PSRAM, dump WAV
│   ├── include/
│   │   ├── config.h          # All GPIO and I²C address definitions
│   │   ├── mic_audio.h       # I²S init, PSRAM recording, WAV header
│   │   ├── mic_serial.h      # WAV dump over USB CDC Serial
│   │   ├── mic_wifi.h        # WAV served over WiFi HTTP
│   │   └── wifi_config.h     # WiFi credentials (gitignored)
│   └── secrets.h.example     # Template for wifi_config.h
├── tools/
│   └── receive_wav.py        # PC-side WAV receiver for mic_test serial dump
├── imu_visualizer.py         # Pygame IMU visualizer (serial or UDP)
├── platformio.ini
├── hardware/                 # EasyEDA exports
│   ├── schematic.pdf
│   ├── gerbers/
│   └── BOM.csv
└── README.md
```

---

## 🔧 Hardware Overview

### Block Diagram

```
                        ┌─────────────────────────────┐
                        │        ESP32-S3-WROOM        │
         ┌──────────────│  18MB Flash | 8MB PSRAM      │──────────────┐
         │              │  Dual-core 240MHz             │              │
         │              └─────────────┬───────────────-─┘              │
         │                           │                                 │
    ┌────▼─────┐    ┌────────────────┼────────────────┐    ┌──────────▼──────────┐
    │   IMU    │    │           I²S Audio              │    │     SPI Display      │
    │LSM6DSOXTR│    │  ┌────────────┐  ┌────────────┐ │    │  TFT + PWM Backlight │
    │ Accel+   │    │  │ ICS-43434  │  │ MAX98357A  │ │    └─────────────────────-┘
    │  Gyro    │    │  │ Mic L + R  │  │ Amp L + R  │ │
    └──────────┘    │  └────────────┘  └────────────┘ │
                    └────────────────────────────────-─┘
         │
    ┌────▼──────────────────────────────────────────────┐
    │                  Power System                      │
    │  USB-C → MCP73831T (LiPo) → Power Path MOSFET     │
    │  MAX17048 Fuel Gauge | TPS22918 Load Switches      │
    └───────────────────────────────────────────────────┘
```

---

## 📌 Pin Assignments

| Signal         | GPIO | Description                       |
|----------------|------|-----------------------------------|
| I2S_MIC_SCK    | 7    | Microphone bit clock              |
| I2S_MIC_SD     | 15   | Microphone serial data            |
| I2S_MIC_WS     | 16   | Microphone word select (L/R)      |
| MIC_EN         | 6    | TPS22918 mic power enable         |
| I2S_SPK_BCLK   | 10   | Amplifier bit clock               |
| I2S_SPK_LRCLK  | 11   | Amplifier LR clock                |
| I2S_SPK_DIN    | 12   | Amplifier data in                 |
| SPK_EN         | 9    | Amplifier SD/enable               |
| SPI_MOSI       | 14   | Display data                      |
| SPI_SCK        | 13   | Display clock                     |
| SPI_CS         | 21   | Display chip select               |
| TFT_DC         | 47   | Display data/command              |
| TFT_RST        | 48   | Display reset                     |
| TFT_DIM        | 39   | Backlight PWM dimming             |
| TFT_EN         | 38   | TPS22918 display power enable     |
| I2C_SDA        | 5    | I²C data (IMU, fuel gauge, STEMMA)|
| I2C_SCL        | 4    | I²C clock                        |
| IMU_INT1       | 18   | LSM6DSOXTR interrupt 1            |
| IMU_INT2       | 8    | LSM6DSOXTR interrupt 2            |
| FUEL_INT       | 17   | MAX17048 alert interrupt          |
| LED_1          | 1    | Status LED                        |
| LED_2          | 2    | Status LED                        |
| LED_3          | 42   | Status LED                        |

---

## 🚀 Getting Started

### Prerequisites

- [PlatformIO](https://platformio.org/) (VS Code extension or CLI)
- arduino-esp32 >= 3.0.0 (platform `espressif32 >= 6.x`)
- Python 3.8+ — `pip install pyserial pygame numpy sounddevice`

### WiFi credentials

Required for `imu-test` WiFi mode and `mic_test` WiFi HTTP option:

```bash
cp firmware/secrets.h.example firmware/include/wifi_config.h
# edit wifi_config.h — fill in WIFI_SSID and WIFI_PASS
```

`wifi_config.h` is gitignored and will never be committed.

---

## 🔬 Firmware environments

### `imu-test` — IMU diagnostic

Streams LSM6DSOXTR accel + gyro + temperature data.

```bash
pio run -e imu-test -t upload
```

**Serial mode** (default) — verbose table + ASCII bar graphs at 10 Hz, 115200 baud:
```bash
pio device monitor -e imu-test
```

**WiFi UDP mode** — CSV stream at 100 Hz to `imu_visualizer.py`. Uncomment in `platformio.ini`:
```ini
-DIMU_WIFI=1
```
```bash
python imu_visualizer.py udp        # listens on port 4210
python imu_visualizer.py COM4       # or use serial mode
```

---

### `mic_test` — Stereo microphone capture

Records 5 s of stereo audio (48 kHz · 24-bit · ~1.4 MB) into PSRAM, then transfers the WAV to a PC.

```bash
pio run -e mic_test -t upload
```

**Option 1 — USB Serial** (no WiFi needed):
```bash
python tools/receive_wav.py COM4
# type "1" at the prompt — the script handles recording and capture
# saves output_YYYYMMDD_HHMMSS.wav
```

**Option 2 — WiFi HTTP** (requires `wifi_config.h`):
```
type "2" at the serial prompt
open http://<printed-IP>/ in a browser
click Download WAV or Re-record
```

**Adjusting gain** — edit `firmware/include/mic_audio.h`:
```cpp
#define MIC_GAIN_DB   30.0f   // +6 dB ≈ ×2, +20 dB ≈ ×10
```
The ICS-43434 has fixed hardware sensitivity; 20–36 dB of digital gain is typical for voice at conversational distance.

---

### `esp32-s3-devkitc-1` — Main application

Boots all subsystems. Audio pipeline, display UI, and fuel gauge readout are in development.

```bash
pio run -e esp32-s3-devkitc-1 -t upload
```

---

## 🧠 Design Decisions

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for a full breakdown. Highlights:

- **ESP32-S3** chosen for its native USB, larger PSRAM, and AI acceleration capabilities
- **ICS-43434** mics selected for flat frequency response and low noise floor
- **MAX98357A** amps chosen for simplicity (no I²C config needed), 3W output, I²S direct drive
- **Ferrite beads** on speaker outputs to suppress switching noise from Class-D amps
- **Per-rail power gating** to reduce idle current draw

---

## 📋 Bill of Materials

See [hardware/BOM.csv](hardware/BOM.csv) for the full BOM.

Key ICs:

| Part             | Description                    | Qty |
|------------------|--------------------------------|-----|
| ESP32-S3-WROOM-1 | Main MCU module                | 1   |
| MAX98357AETE     | I²S Class-D Amplifier          | 2   |
| ICS-43434        | I²S MEMS Microphone            | 2   |
| LSM6DSOXTR       | 6-axis IMU                     | 1   |
| MAX17048         | LiPo Fuel Gauge                | 1   |
| MCP73831T        | LiPo Charger                   | 1   |
| TPS22918         | Load Switch                    | 3   |
| DMG3415U         | P-Ch Power Path MOSFET         | 1   |
| AO3400A          | N-Ch Backlight MOSFET          | 1   |

---

## 📄 License

MIT License — see [LICENSE](LICENSE) for details.

---

## 👤 Author

**Your Name**
- GitHub: [@YOUR_USERNAME](https://github.com/YOUR_USERNAME)
- LinkedIn: [your-profile](https://linkedin.com/in/your-profile)
