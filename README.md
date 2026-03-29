<div align="center">

# ESP32 Talking Agent

**A battery-powered voice assistant — speak to it, it thinks, it speaks back.**

ESP32-S3 · Stereo I²S Audio · TFT Display · 6-axis IMU · USB-C · LiPo

[![PCB](https://img.shields.io/badge/PCB-v1.0-00f0c8?style=flat-square)](hardware/)
[![MCU](https://img.shields.io/badge/MCU-ESP32--S3-e03030?style=flat-square)](https://www.espressif.com/en/products/socs/esp32-s3)
[![Framework](https://img.shields.io/badge/framework-Arduino%20%2F%20ESP--IDF-orange?style=flat-square)](https://platformio.org/)
[![License](https://img.shields.io/badge/license-MIT-green?style=flat-square)](LICENSE)

<br>

<img src="img/top.png" width="580" alt="ESP32 Talking Agent — front render"/>

<br><br>

<img src="img/layout.png" width="580" alt="PCB layout"/>

</div>

---

## What it does

You speak into the onboard microphone. The ESP32-S3 streams raw PCM audio over WiFi to a Python server, which:

1. Detects the end of your utterance with a voice activity detector
2. Transcribes your speech with [faster-whisper](https://github.com/SYSTRAN/faster-whisper)
3. Sends the transcript to Claude (Anthropic) and streams the response
4. Synthesises each sentence with [kokoro-onnx](https://github.com/thewh1teagle/kokoro-onnx) TTS
5. Streams the audio back to the ESP32, which plays it through the Class-D amplifier

A browser UI at `http://localhost:8080` shows the live conversation — transcript, streaming tokens, and pipeline state.

**Status:** Hardware bring-up complete. Full voice pipeline working end-to-end.

---

## Architecture

```
                  ws://server:8765
ESP32-S3  ──/device──▶  server/main.py  ◀──/ui──  browser
   │  ◀──────────────────────────┘                    │
   │  PCM audio out (24 kHz TTS)                      │
   │                                                  │
   ├─ mic_task (core 1)                            live UI
   │    I²S RX → 30 dB gain → RMS gate              ├─ transcript
   │    → FreeRTOS queue                             ├─ streaming tokens
   │                                                 └─ state badge
   ├─ loop()
   │    webSocket.loop()
   │    drain queue → sendBIN()
   │
   └─ speaker_task (core 0)
        ring buffer → I²S TX → MAX98357A

        server pipeline
        ───────────────
        PCM binary frames
              ↓
          VADBuffer          ← speech/silence detection
              ↓ utterance
        faster-whisper        ← STT (beam=1, ~300 ms)
              ↓ transcript
        Claude Sonnet         ← LLM streaming
              ↓ tokens
        sentence_chunker      ← split on . ! ?
              ↓ sentences (concurrent with LLM)
        kokoro-onnx TTS       ← per sentence (~400 ms)
              ↓ 24 kHz PCM
        websocket.send()      → ESP32 → speaker
```

**Latency to first audio (typical):**

| Stage | Time |
|-------|------|
| VAD silence detection | ~700 ms |
| faster-whisper STT (base.en) | ~300 ms |
| Claude Sonnet TTFT | ~500 ms |
| First TTS sentence | ~400 ms |
| **Total** | **~1.9 s** |

Subsequent sentences play with ~300–500 ms gaps because the LLM and TTS run concurrently via an `asyncio.Queue` pipeline.

---

## Hardware

| Subsystem | Part | Notes |
|-----------|------|-------|
| MCU | ESP32-S3-WROOM-1 | 16 MB Flash · 8 MB OPI PSRAM · 240 MHz dual-core |
| Microphones | ICS-43434 × 2 | Stereo I²S · 24-bit · 65 dBA SNR · power-gated |
| Amplifiers | MAX98357A × 2 | Stereo I²S Class-D · 3 W · no I²C config |
| Display | TFT (SPI) | PWM backlight · power-gated |
| IMU | LSM6DSOXTR | 6-axis accel + gyro · I²C · HW gesture interrupts |
| Fuel gauge | MAX17048 | LiPo SOC · I²C · low-battery alert |
| Charger | MCP73831T | Single-cell LiPo · USB-C input |
| Power gating | TPS22918 × 3 | Per-rail load switches — mics, display |

<details>
<summary><b>GPIO assignments</b></summary>
<br>

| Signal | GPIO | Description |
|--------|------|-------------|
| I2S_MIC_SCK | 7 | Microphone bit clock |
| I2S_MIC_SD | 15 | Microphone serial data |
| I2S_MIC_WS | 16 | Microphone word select |
| MIC_EN | 6 | Mic power rail enable (TPS22918) |
| I2S_SPK_BCLK | 10 | Amplifier bit clock |
| I2S_SPK_LRCLK | 11 | Amplifier LR clock |
| I2S_SPK_DIN | 12 | Amplifier data in |
| SPK_EN | 9 | Amplifier SD / enable |
| SPI_MOSI | 14 | Display data |
| SPI_SCK | 13 | Display clock |
| SPI_CS | 21 | Display chip select |
| TFT_DC | 47 | Display D/C |
| TFT_RST | 48 | Display reset |
| TFT_DIM | 39 | Display backlight PWM |
| TFT_EN | 38 | Display power enable |
| I2C_SDA | 5 | IMU + fuel gauge |
| I2C_SCL | 4 | IMU + fuel gauge |
| IMU_INT1 | 18 | IMU interrupt 1 |
| IMU_INT2 | 8 | IMU interrupt 2 |
| FUEL_INT | 17 | Fuel gauge alert |
| LED1 | 1 | Status LED — I²S active |
| LED2 | 2 | Status LED — VAD gate |
| LED3 | 42 | Status LED — WebSocket connected |

</details>

---

## Prerequisites

- [PlatformIO](https://platformio.org/) — for building and flashing firmware
- Python 3.9+
- An [Anthropic API key](https://console.anthropic.com/)

---

## Quick start

### 1 · Download TTS model files

These are large binaries not included in the repo. Download once into `server/`:

```bash
# macOS / Linux
curl -L -o server/kokoro-v1.0.onnx \
  "https://github.com/thewh1teagle/kokoro-onnx/releases/download/model-files-v1.0/kokoro-v1.0.fp16.onnx"
curl -L -o server/voices-v1.0.bin \
  "https://github.com/thewh1teagle/kokoro-onnx/releases/download/model-files-v1.0/voices-v1.0.bin"
```

```powershell
# Windows PowerShell
curl.exe -L -o server\kokoro-v1.0.onnx "https://github.com/thewh1teagle/kokoro-onnx/releases/download/model-files-v1.0/kokoro-v1.0.fp16.onnx"
curl.exe -L -o server\voices-v1.0.bin  "https://github.com/thewh1teagle/kokoro-onnx/releases/download/model-files-v1.0/voices-v1.0.bin"
```

### 2 · Install server dependencies

```bash
# macOS / Linux
make install

# Windows (PowerShell)
python -m venv server/.venv
server\.venv\Scripts\pip install -r server\requirements.txt
```

### 3 · Configure environment

```bash
cp .env.example .env
```

Open `.env` and fill in:

```env
ANTHROPIC_API_KEY=sk-ant-...
```

The Whisper model downloads automatically from HuggingFace on first run.

### 4 · Flash firmware

Edit the top of [firmware/src/main.cpp](firmware/src/main.cpp) with your network credentials and server IP:

```cpp
#define WIFI_SSID      "your_network"
#define WIFI_PASSWORD  "your_password"
#define SERVER_IP      "192.168.x.x"   // LAN IP of the machine running the server
```

Find your server's IP with `ipconfig` (Windows) or `ip a` (Linux/Mac) — look for the WiFi adapter address.

Then flash:

```bash
# macOS / Linux
make flash

# Windows
pio run --environment voicebox --target upload
```

### 5 · Start the server

```bash
# macOS / Linux
make run-server

# Windows
server\.venv\Scripts\python server\main.py
```

You should see:
```
[server] WS  listening on :8765
[server] HTTP listening on :8080
```

### 6 · Open the UI

Navigate to **http://localhost:8080** in a browser.

The status badge will show **disconnected** until the ESP32 connects, then switch to **listening**. Speak — the badge cycles through **transcribing → thinking → speaking** as the pipeline runs.

---

## Environment variables

| Variable | Default | Description |
|----------|---------|-------------|
| `ANTHROPIC_API_KEY` | — | **Required.** |
| `WHISPER_MODEL` | `base.en` | `tiny.en` is faster, `small.en` is more accurate. |
| `KOKORO_VOICE` | `af_heart` | kokoro-onnx voice ID. |
| `SPEECH_THRESHOLD` | `0.06` | Normalised RMS above which a chunk is considered speech. Raise if ambient noise triggers false utterances. |
| `SILENCE_THRESHOLD` | `0.04` | Normalised RMS below which a chunk is considered silence. Should sit just above your ambient noise floor. |
| `SILENCE_DURATION_MS` | `700` | Continuous silence required to end an utterance. Lower = faster response; higher = less word clipping. |
| `WS_PORT` | `8765` | WebSocket port (must match firmware `SERVER_PORT`). |
| `HTTP_PORT` | `8080` | HTTP port for the browser UI. |

---

## How it works

### Firmware

`mic_task` (core 1) reads 512-sample frames from the ICS-43434 via I²S (32-bit stereo, left channel only), applies 30 dB digital gain, computes normalised RMS, and enqueues frames above `MIC_THRESHOLD` into a FreeRTOS queue.

The Arduino `loop()` drives the WebSocket state machine and drains the mic queue, forwarding each frame as a binary WebSocket message to `/device` on the server.

When TTS audio arrives as a binary WebSocket message, the `onWsEvent` handler pushes it into a FreeRTOS ring buffer (2 seconds capacity at 24 kHz). `speaker_task` (core 0) drains this buffer and writes PCM to the MAX98357A amplifier via I²S.

### Server

**VADBuffer** accumulates raw PCM chunks. It enters speech mode when normalised RMS exceeds `SPEECH_THRESHOLD`, and emits the complete utterance once silence lasts longer than `SILENCE_DURATION_MS` after the last speech frame.

**run_stt** passes the utterance to faster-whisper in a thread pool (beam_size=1, vad_filter=True) and returns the transcript string in ~300 ms.

**run_llm** opens a streaming request to Claude Sonnet and yields tokens as they arrive. Each token is also broadcast to browser clients as an `llm_token` event for live display.

**sentence_chunker** buffers the token stream and yields a complete sentence each time it sees `.`, `!`, or `?`. This lets TTS synthesis begin on the first sentence while the LLM is still generating the rest — the key to low perceived latency.

**run_tts** synthesises one sentence with kokoro-onnx in a thread pool and returns raw 24 kHz 16-bit PCM. The server sends this binary frame directly to the ESP32.

The LLM collector and TTS loop run as concurrent asyncio tasks so sentence N+1 is ready in the queue as soon as TTS for sentence N finishes.

### Browser UI

Connects to `/ui` and displays `llm_token` events as they arrive by appending text nodes directly (no `innerHTML`, no reflow). The status badge reflects the current pipeline state with a pulsing dot animation.

---

## VAD tuning

If the system is triggering on background noise, or missing quiet speech, adjust `SPEECH_THRESHOLD` and `SILENCE_THRESHOLD` in `.env`.

To find your hardware's actual noise floor, watch the serial monitor:

```bash
pio device monitor --baud 115200
```

Look for `[MIC] rms=` lines. Note the RMS at silence and at normal speech. Set `SPEECH_THRESHOLD` to the midpoint between them, and `SILENCE_THRESHOLD` to just above the silence level.

| Condition | Example | Suggested setting |
|-----------|---------|-------------------|
| Ambient silence | 0.03 | `SILENCE_THRESHOLD=0.04` |
| Normal speech | 0.10 | `SPEECH_THRESHOLD=0.06` |

---

## Troubleshooting

| Symptom | Likely cause | Fix |
|---------|-------------|-----|
| Status badge stuck on **disconnected** | Server not running, or wrong `SERVER_IP` in firmware | Check server terminal; `SERVER_IP` must be the LAN IP of the server machine, not `localhost` |
| Status badge stuck on **listening**, nothing happens | VAD threshold too high | Lower `SPEECH_THRESHOLD` in `.env` |
| Badge flickers through states with no speech | Noise floor above threshold | Raise `SPEECH_THRESHOLD` and `SILENCE_THRESHOLD` |
| Speech is cut off at the end | Silence budget too short | Increase `SILENCE_DURATION_MS` (try 900) |
| TTS audio is distorted | Ring buffer overflow | Audio coming faster than speaker can drain; expected with long sentences |
| Whisper returns empty transcript | Utterance too short or quiet | Speak louder; lower `SPEECH_THRESHOLD` |
| `[MIC] i2s_read err≠0` in serial | I²S init failed | Check `MIC_EN` pin is driven HIGH; verify I²S pin config |

---

## Repository layout

```
esp32-talking-agent/
├── firmware/
│   ├── src/
│   │   ├── main.cpp          # Talking agent firmware  (env: voicebox)
│   │   ├── imu_test.cpp      # IMU diagnostic          (env: imu-test)
│   │   └── mic_test.cpp      # Mic capture + WAV dump  (env: mic_test)
│   └── include/
│       ├── config.h          # GPIO assignments
│       └── mic_audio.h       # I²S init + PSRAM recording
├── server/
│   ├── main.py               # asyncio WebSocket + HTTP server
│   ├── pipeline.py           # VAD · STT · LLM · TTS
│   ├── config.py             # Settings dataclass (loads .env)
│   ├── requirements.txt
│   └── tests/
│       └── test_pipeline.py  # pytest suite
├── ui/
│   └── index.html            # Single-file browser monitor (vanilla JS)
├── docs/
│   ├── ARCHITECTURE.md       # Hardware design decisions
│   └── PINOUT.md             # Full GPIO reference
├── hardware/                 # Schematics, gerbers, BOM
├── tools/
│   └── receive_wav.py        # WAV receiver for mic_test serial dump
├── platformio.ini            # All PlatformIO environments
├── Makefile                  # install · run-server · flash · monitor · test
└── .env.example              # Environment variable reference
```

---

## PlatformIO environments

| Environment | Source file | Purpose |
|-------------|-------------|---------|
| `voicebox` | `main.cpp` | Main talking agent firmware |
| `mic_test` | `mic_test.cpp` | 5-second WAV capture, dump via Serial or WiFi HTTP |
| `imu-test` | `imu_test.cpp` | LSM6DSOX streaming to serial or UDP visualiser |
| `esp32-s3-devkitc-1` | (all) | Full build for display + IMU + fuel gauge integration |

---

## License

MIT — see [LICENSE](LICENSE).
