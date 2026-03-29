//
// main.cpp — ESP32 Talking Agent firmware
//
// Captures mic audio → streams PCM to server → plays TTS audio back.
//
// Architecture
// ────────────
// loop()        — WebSocket event loop + drains mic send queue
// mic_task      — I²S RX, RMS gate, enqueue frame (core 1)
// speaker_task  — dequeue from ring buffer, I²S TX  (core 0)
//
// WebSocket library: Links2004/arduinoWebSockets
// I²S driver:        legacy driver/i2s.h  (works with arduino-esp32 3.x)
//
// Build:   pio run -e voicebox -t upload
// Monitor: pio device monitor -e voicebox
//

#include <Arduino.h>
#include <WiFi.h>
#include <WebSocketsClient.h>
#include <driver/i2s.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/ringbuf.h>
#include <cmath>
#include "config.h"
#include "wifi_config.h"   // gitignored — copy from wifi_config.h.example

// ── Microphone pins (ICS-43434) ───────────────────────────────
// These match the actual PCB wiring from firmware/include/config.h.
// Adjust if your hardware differs.
#define MIC_BCK         7    // I²S bit clock  (PIN_I2S_MIC_SCK)
#define MIC_WS          16   // Word select     (PIN_I2S_MIC_WS)
#define MIC_DATA        15   // Serial data in  (PIN_I2S_MIC_SD)
#define MIC_EN_PIN      6    // TPS22918 power enable

// ── Speaker pins (MAX98357A) ──────────────────────────────────
#define SPK_BCK         10   // I²S bit clock   (PIN_I2S_SPK_BCLK)
#define SPK_WS          11   // LR clock        (PIN_I2S_SPK_LRCLK)
#define SPK_DATA        12   // Serial data out (PIN_I2S_SPK_DIN)
#define SPK_EN_PIN      9    // Amplifier SD/enable

// ── Audio parameters ──────────────────────────────────────────
#define SAMPLE_RATE_MIC  16000   // Hz — matches server STT input
#define SAMPLE_RATE_SPK  24000   // Hz — matches kokoro-onnx output
#define DMA_BUF_SAMPLES  512     // samples per DMA buffer
#define DMA_BUF_COUNT    8

// ── Digital gain ──────────────────────────────────────────────
// ICS-43434 output is quiet; apply gain before RMS gating and transmission.
// 30 dB ≈ ×31.6 — matches the gain used in mic_test / mic_audio.h.
#define MIC_GAIN_DB      30.0f

// ── VAD gate ──────────────────────────────────────────────────
// Frames with normalised RMS below this are not sent to the server.
// Evaluated *after* gain is applied.
#define MIC_THRESHOLD    0.015f

// ── Ring buffer sizing ────────────────────────────────────────
// 2 s of 24 kHz 16-bit audio = 96 000 bytes.  Holds a full TTS sentence.
#define SPK_RING_BYTES   (SAMPLE_RATE_SPK * 2 * 2)

// ── I²S ports ────────────────────────────────────────────────
#define I2S_MIC_PORT   I2S_NUM_0
#define I2S_SPK_PORT   I2S_NUM_1

// ── Send queue (mic → loop) ───────────────────────────────────
// Fixed-size frames keep the queue memory predictable and avoid malloc.
#define SEND_QUEUE_DEPTH  8

struct MicFrame {
    int16_t pcm[DMA_BUF_SAMPLES];   // mono int16
    uint32_t n_samples;
};

static QueueHandle_t   s_mic_queue  = nullptr;
static RingbufHandle_t s_spk_ring   = nullptr;
static WebSocketsClient webSocket;
static volatile bool    s_ws_connected = false;

// ─────────────────────────────────────────────────────────────
// WebSocket event handler — called from webSocket.loop()
// ─────────────────────────────────────────────────────────────
static void onWsEvent(WStype_t type, uint8_t *payload, size_t length)
{
    switch (type) {

    case WStype_CONNECTED:
        s_ws_connected = true;
        digitalWrite(PIN_LED_3, HIGH);
        Serial.printf("[WS] Connected to ws://%s:%d/device\n",
                      SERVER_IP, SERVER_PORT);
        break;

    case WStype_DISCONNECTED:
        s_ws_connected = false;
        digitalWrite(PIN_LED_3, LOW);
        Serial.println("[WS] Disconnected — reconnecting in 2 s");
        break;

    case WStype_BIN:
        // TTS audio: 24 kHz 16-bit mono PCM — push to speaker ring buffer.
        if (s_spk_ring && length > 0) {
            BaseType_t sent = xRingbufferSend(
                s_spk_ring, payload, length, pdMS_TO_TICKS(20));
            if (sent != pdTRUE) {
                Serial.printf("[SPK] Ring buffer full — dropped %u B\n",
                              (unsigned)length);
            }
        }
        break;

    case WStype_TEXT:
        // JSON state messages from server (informational, no action needed).
        Serial.printf("[WS] msg: %.*s\n", (int)length, (char *)payload);
        break;

    case WStype_ERROR:
        Serial.println("[WS] Error");
        break;

    default:
        break;
    }
}

// ─────────────────────────────────────────────────────────────
// I²S microphone init  (ICS-43434, legacy driver)
// ─────────────────────────────────────────────────────────────
static bool mic_init()
{
    // Power the ICS-43434s via the TPS22918 load switch.
    pinMode(MIC_EN_PIN, OUTPUT);
    digitalWrite(MIC_EN_PIN, HIGH);
    delay(10);   // < 1 ms rise time; 10 ms ensures startup sequence completes

    // ICS-43434 outputs 24-bit audio left-justified in a 32-bit I²S slot
    // (MSB at bit 31, bits [7:0] = 0).  Configure for 32-bit DMA reads;
    // shift >>16 in mic_task to obtain 16-bit samples.
    const i2s_config_t cfg = {
        .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate          = SAMPLE_RATE_MIC,
        .bits_per_sample      = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format       = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count        = DMA_BUF_COUNT,
        .dma_buf_len          = DMA_BUF_SAMPLES,
        .use_apll             = false,
        .tx_desc_auto_clear   = false,
        .fixed_mclk           = 0,
    };
    const i2s_pin_config_t pins = {
        .mck_io_num   = I2S_PIN_NO_CHANGE,
        .bck_io_num   = MIC_BCK,
        .ws_io_num    = MIC_WS,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num  = MIC_DATA,
    };

    if (i2s_driver_install(I2S_MIC_PORT, &cfg, 0, nullptr) != ESP_OK) {
        Serial.println("[MIC] i2s_driver_install failed");
        return false;
    }
    if (i2s_set_pin(I2S_MIC_PORT, &pins) != ESP_OK) {
        Serial.println("[MIC] i2s_set_pin failed");
        i2s_driver_uninstall(I2S_MIC_PORT);
        return false;
    }
    i2s_zero_dma_buffer(I2S_MIC_PORT);
    Serial.printf("[MIC] Ready  %d Hz  APLL  BCK=%d WS=%d DATA=%d\n",
                  SAMPLE_RATE_MIC, MIC_BCK, MIC_WS, MIC_DATA);
    return true;
}

// ─────────────────────────────────────────────────────────────
// I²S speaker init  (MAX98357A, legacy driver)
// ─────────────────────────────────────────────────────────────
static bool spk_init()
{
    pinMode(SPK_EN_PIN, OUTPUT);
    digitalWrite(SPK_EN_PIN, HIGH);

    const i2s_config_t cfg = {
        .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate          = SAMPLE_RATE_SPK,
        .bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format       = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count        = DMA_BUF_COUNT,
        .dma_buf_len          = DMA_BUF_SAMPLES,
        .use_apll             = true,
        // Auto-clear TX DMA descriptors — outputs silence between writes
        // instead of re-transmitting the last buffer (prevents stuck audio).
        .tx_desc_auto_clear   = true,
        .fixed_mclk           = 0,
    };
    const i2s_pin_config_t pins = {
        .mck_io_num   = I2S_PIN_NO_CHANGE,
        .bck_io_num   = SPK_BCK,
        .ws_io_num    = SPK_WS,
        .data_out_num = SPK_DATA,
        .data_in_num  = I2S_PIN_NO_CHANGE,
    };

    if (i2s_driver_install(I2S_SPK_PORT, &cfg, 0, nullptr) != ESP_OK) {
        Serial.println("[SPK] i2s_driver_install failed");
        return false;
    }
    if (i2s_set_pin(I2S_SPK_PORT, &pins) != ESP_OK) {
        Serial.println("[SPK] i2s_set_pin failed");
        i2s_driver_uninstall(I2S_SPK_PORT);
        return false;
    }
    Serial.printf("[SPK] Ready  %d Hz  BCK=%d WS=%d DATA=%d\n",
                  SAMPLE_RATE_SPK, SPK_BCK, SPK_WS, SPK_DATA);
    return true;
}

// ─────────────────────────────────────────────────────────────
// mic_task — I²S capture, RMS gate, enqueue for WebSocket send
// Pinned to core 1.
// ─────────────────────────────────────────────────────────────
static void mic_task(void *arg)
{
    // Heap-allocated 32-bit DMA buffer: 512 stereo frames × 4 bytes = 4 kB.
    int32_t *raw = (int32_t *)malloc(DMA_BUF_SAMPLES * 2 * sizeof(int32_t));
    if (!raw) {
        Serial.println("[MIC] malloc failed — task exiting");
        vTaskDelete(nullptr);
        return;
    }

    MicFrame frame;
    static const float s_gain = powf(10.0f, MIC_GAIN_DB / 20.0f);
    Serial.println("[MIC] Task running");

    for (;;) {
        size_t   bytes_read = 0;
        esp_err_t err = i2s_read(
            I2S_MIC_PORT, raw,
            DMA_BUF_SAMPLES * 2 * sizeof(int32_t),
            &bytes_read, portMAX_DELAY);

        static uint32_t s_read_count = 0;
        if (++s_read_count % 50 == 0)
            Serial.printf("[MIC] i2s_read err=0x%x bytes=%u\n", err, (unsigned)bytes_read);

        // LED1: on solid while i2s_read is returning data.
        digitalWrite(PIN_LED_1, (err == ESP_OK && bytes_read > 0) ? HIGH : LOW);

        if (err != ESP_OK || bytes_read == 0) continue;

        const uint32_t n_raw = bytes_read / sizeof(int32_t);

        // ICS-43434: 24-bit MSB-justified in 32-bit slot.
        // LEFT channel is every even 32-bit word; >>16 gives 16-bit.
        // Apply digital gain then hard-clip to int16 range.
        uint32_t n_mono = 0;
        int64_t  sum_sq = 0;

        for (uint32_t i = 0; i < n_raw; i += 2) {
            int32_t s32 = (int32_t)((float)(int16_t)(raw[i] >> 16) * s_gain);
            if (s32 >  32767) s32 =  32767;
            if (s32 < -32768) s32 = -32768;
            int16_t s = (int16_t)s32;
            frame.pcm[n_mono++] = s;
            sum_sq += (int64_t)s * s;
            if (n_mono >= DMA_BUF_SAMPLES) break;
        }

        if (n_mono == 0) continue;

        // Normalised RMS: sqrt(mean(s²)) / 32768
        float rms_norm = sqrtf((float)sum_sq / n_mono) / 32768.0f;

        static uint32_t s_rms_count = 0;
        if (++s_rms_count % 50 == 0)
            Serial.printf("[MIC] rms=%.4f  thr=%.4f\n", rms_norm, (float)MIC_THRESHOLD);

        if (rms_norm < MIC_THRESHOLD) continue;   // gate: below noise floor
        if (!s_ws_connected)          continue;   // no connection yet

        // LED2: blink on each frame that passes the VAD gate.
        digitalWrite(PIN_LED_2, HIGH);
        digitalWrite(PIN_LED_2, LOW);

        frame.n_samples = n_mono;
        // Non-blocking: drop frame if queue is full rather than stalling I²S.
        xQueueSend(s_mic_queue, &frame, 0);
    }

    free(raw);
    vTaskDelete(nullptr);
}

// ─────────────────────────────────────────────────────────────
// speaker_task — drain ring buffer, write to I²S TX
// Pinned to core 0.
// ─────────────────────────────────────────────────────────────
static void speaker_task(void *arg)
{
    Serial.println("[SPK] Task running");

    for (;;) {
        size_t item_size = 0;
        void  *item = xRingbufferReceive(s_spk_ring, &item_size, portMAX_DELAY);
        if (!item) continue;

        size_t written = 0;
        esp_err_t err = i2s_write(
            I2S_SPK_PORT, item, item_size, &written, pdMS_TO_TICKS(500));
        if (err != ESP_OK) {
            Serial.printf("[SPK] i2s_write error 0x%x\n", err);
        }

        vRingbufferReturnItem(s_spk_ring, item);
    }

    vTaskDelete(nullptr);
}

// ─────────────────────────────────────────────────────────────
// WiFi
// ─────────────────────────────────────────────────────────────
static void wifi_connect()
{
    Serial.printf("[WIFI] Connecting to \"%s\" ", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - t0 > 20000) {
            Serial.println("\n[WIFI] Timeout — restarting");
            ESP.restart();
        }
        delay(500);
        Serial.print('.');
    }
    Serial.printf("\n[WIFI] Connected  IP=%s  RSSI=%d dBm\n",
                  WiFi.localIP().toString().c_str(), WiFi.RSSI());
}

// ─────────────────────────────────────────────────────────────
// setup / loop
// ─────────────────────────────────────────────────────────────
void setup()
{
    Serial.begin(115200);
    delay(200);
    Serial.println("[BOOT] ESP32 Talking Agent v1.0");

    pinMode(PIN_LED_1, OUTPUT);
    pinMode(PIN_LED_2, OUTPUT);
    pinMode(PIN_LED_3, OUTPUT);
    digitalWrite(PIN_LED_1, LOW);
    digitalWrite(PIN_LED_2, LOW);
    digitalWrite(PIN_LED_3, LOW);

    wifi_connect();

    if (!mic_init() || !spk_init()) {
        Serial.println("[BOOT] I²S init failed — halting");
        while (true) delay(1000);
    }

    // Speaker ring buffer — 2 s at 24 kHz 16-bit = 96 kB.
    // Large enough to hold a full TTS sentence without dropping.
    s_spk_ring = xRingbufferCreate(SPK_RING_BYTES, RINGBUF_TYPE_BYTEBUF);
    if (!s_spk_ring) {
        Serial.println("[BOOT] Speaker ring buffer alloc failed — halting");
        while (true) delay(1000);
    }

    // Mic send queue — fixed-size MicFrame structs, depth 8.
    s_mic_queue = xQueueCreate(SEND_QUEUE_DEPTH, sizeof(MicFrame));
    if (!s_mic_queue) {
        Serial.println("[BOOT] Mic queue alloc failed — halting");
        while (true) delay(1000);
    }

    // WebSocket client — connects to /device on the Python server.
    webSocket.begin(SERVER_IP, SERVER_PORT, "/device");
    webSocket.onEvent(onWsEvent);
    webSocket.setReconnectInterval(2000);   // retry 2 s after disconnect
    Serial.printf("[WS] Connecting to ws://%s:%d/device\n",
                  SERVER_IP, SERVER_PORT);

    // mic_task on core 1 (same core as Arduino loop — no WS contention).
    xTaskCreatePinnedToCore(mic_task,     "mic",     4096,
                             nullptr, 5, nullptr, 1);
    // speaker_task on core 0 — isolates I²S TX from network activity.
    xTaskCreatePinnedToCore(speaker_task, "speaker", 4096,
                             nullptr, 5, nullptr, 0);

    Serial.println("[BOOT] All subsystems started — listening");
}

void loop()
{
    // Drive WebSocket state machine (handles connect, ping/pong, callbacks).
    webSocket.loop();

    // Drain mic send queue and forward frames over WebSocket.
    // webSocket.sendBIN must only be called from the task running webSocket.loop().
    MicFrame frame;
    while (xQueueReceive(s_mic_queue, &frame, 0) == pdTRUE) {
        if (s_ws_connected) {
            webSocket.sendBIN(
                (const uint8_t *)frame.pcm,
                frame.n_samples * sizeof(int16_t));
        }
    }

    // Brief yield so FreeRTOS scheduler can run other tasks on this core.
    delay(1);
}
