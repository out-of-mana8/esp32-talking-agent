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
├── firmware/           # ESP32-S3 firmware (ESP-IDF / Arduino)
│   ├── src/            # Source files
│   ├── include/        # Header files
│   └── platformio.ini  # PlatformIO config
├── hardware/           # EasyEDA exports
│   ├── schematic.pdf   # Full schematic
│   ├── gerbers/        # Gerber files for PCB fab
│   └── BOM.csv         # Bill of materials
├── docs/               # Design documentation
│   ├── ARCHITECTURE.md # System block diagram & design decisions
│   ├── PINOUT.md       # ESP32-S3 pin assignments
│   └── CHANGELOG.md    # Version history
├── media/              # Photos, renders, demo videos
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

See [docs/PINOUT.md](docs/PINOUT.md) for the full table.

**Key signals:**

| Signal        | GPIO  | Description                  |
|---------------|-------|------------------------------|
| I2S_MIC_SD    | —     | Microphone data              |
| I2S_MIC_WS    | —     | Microphone word select       |
| I2S_MIC_SCK   | —     | Microphone clock             |
| I2S_SPK_DIN   | —     | Amplifier data in            |
| I2S_SPK_LRCLK | —     | Amplifier LR clock           |
| I2S_SPK_BCLK  | —     | Amplifier bit clock          |
| SPI_MOSI      | —     | Display data                 |
| SPI_SCK       | —     | Display clock                |
| TFT_DIM       | —     | Backlight PWM dimming        |
| TFT_EN        | —     | Display power enable         |
| SPK_EN        | —     | Amplifier enable             |
| MIC_EN        | —     | Microphone enable            |
| SDA / SCL     | —     | I²C (IMU, Fuel Gauge, STEMMA)|
| IMU_INT1/2    | —     | IMU interrupt lines          |

> GPIO numbers to be filled in after firmware bring-up

---

## 🚀 Getting Started

### Prerequisites
- [PlatformIO](https://platformio.org/) or ESP-IDF v5.x
- ESP32-S3 USB driver

### Build & Flash
```bash
git clone https://github.com/YOUR_USERNAME/esp32-voicebox.git
cd esp32-voicebox/firmware
pio run --target upload
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
