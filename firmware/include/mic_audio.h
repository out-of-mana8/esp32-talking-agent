#pragma once
//
// mic_audio.h — ICS-43434 stereo mic: I²S init, PSRAM recording, WAV header
//
// Uses the ESP-IDF legacy I²S driver (driver/i2s.h) because the newer
// driver/i2s_std.h (ESP-IDF v5 component esp_driver_i2s) is not added to the
// Arduino-framework include path by PlatformIO's espressif32 builder.
// The legacy API is fully functional on ESP-IDF v5 / arduino-esp32 3.x and
// supports all features used here (48 kHz, 32-bit slots, APLL, PSRAM DMA).
//
// Buffer: ~1.37 MB allocated in PSRAM via ps_malloc().
// Bit packing: ICS-43434 outputs 24-bit audio left-justified in 32-bit I²S
// slots (MSB at bit 31, 8 trailing zero-bits). Arithmetic >>8 extracts the
// signed 24-bit value; it is then packed as 3 LE bytes into the WAV.
//
#include <Arduino.h>
#include <driver/i2s.h>
#include "config.h"
#include <cstring>

// ── Recording parameters ──────────────────────────────────────
#define MIC_SAMPLE_RATE      48000
#define MIC_REC_SECONDS      5
#define MIC_CHANNELS         2
#define MIC_BITS_PER_SAMPLE  24     // audio resolution written to WAV
#define MIC_BYTES_PER_SAMP   3      // 24-bit packed little-endian per sample

// Digital gain applied after capturing. 0 = unity, +6 ≈ ×2, -6 ≈ ×0.5.
// The result is hard-clipped to the 24-bit range so loud signals don't wrap.
#define MIC_GAIN_DB          30.0f

#define WAV_DATA_BYTES   ((uint32_t)MIC_SAMPLE_RATE * MIC_REC_SECONDS \
                          * MIC_CHANNELS * MIC_BYTES_PER_SAMP)
// = 48000 × 5 × 2 × 3 = 1 440 000 bytes (~1.37 MB)

#define WAV_HEADER_BYTES  44
#define WAV_TOTAL_BYTES   (WAV_HEADER_BYTES + WAV_DATA_BYTES)

// DMA read chunk: 512 stereo frames × 4 bytes = 4 kB on the stack.
#define MIC_DMA_FRAMES    512

// I²S DMA ring buffer sizing — larger = fewer underruns, more latency.
#define MIC_DMA_BUF_COUNT  8
#define MIC_DMA_BUF_LEN    512

#define MIC_I2S_PORT       I2S_NUM_0

struct MicBuf {
    uint8_t  *data;         // PSRAM pointer; first 44 bytes = WAV header
    uint32_t  total_bytes;  // WAV_TOTAL_BYTES when valid, 0 on failure
};

// ── WAV header writer ─────────────────────────────────────────
static void _write_wav_header(uint8_t *buf, uint32_t data_bytes) {
    const uint16_t channels    = MIC_CHANNELS;
    const uint32_t sample_rate = MIC_SAMPLE_RATE;
    const uint16_t bits        = MIC_BITS_PER_SAMPLE;
    const uint16_t block_align = channels * MIC_BYTES_PER_SAMP;    // 6
    const uint32_t byte_rate   = sample_rate * block_align;         // 288 000
    const uint32_t riff_size   = data_bytes + 36;
    const uint16_t audio_fmt   = 1;   // PCM
    const uint32_t fmt_chunk   = 16;

    memcpy(buf +  0, "RIFF",       4);
    memcpy(buf +  4, &riff_size,   4);
    memcpy(buf +  8, "WAVE",       4);
    memcpy(buf + 12, "fmt ",       4);
    memcpy(buf + 16, &fmt_chunk,   4);
    memcpy(buf + 20, &audio_fmt,   2);
    memcpy(buf + 22, &channels,    2);
    memcpy(buf + 24, &sample_rate, 4);
    memcpy(buf + 28, &byte_rate,   4);
    memcpy(buf + 32, &block_align, 2);
    memcpy(buf + 34, &bits,        2);
    memcpy(buf + 36, "data",       4);
    memcpy(buf + 40, &data_bytes,  4);
}

// ── I²S init ─────────────────────────────────────────────────
static bool mic_audio_init() {
    // ── Mic power rail ───────────────────────────────────────
    // TPS22918 load switch on PIN_MIC_EN (GPIO6): drive HIGH to connect VDD
    // to both ICS-43434 mics. The switch has < 1 ms rise time; 10 ms ensures
    // the mics complete their startup sequence before I²S clocking begins.
    pinMode(PIN_MIC_EN, OUTPUT);
    digitalWrite(PIN_MIC_EN, HIGH);
    delay(10);

    const i2s_config_t cfg = {
        .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate          = MIC_SAMPLE_RATE,
        .bits_per_sample      = I2S_BITS_PER_SAMPLE_32BIT,
        // ICS-43434 uses standard I²S: left channel on WS-low (ch0),
        // right channel on WS-high (ch1). RIGHT_LEFT interleaves them in the
        // DMA buffer as [L0, R0, L1, R1, ...].
        .channel_format       = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count        = MIC_DMA_BUF_COUNT,
        .dma_buf_len          = MIC_DMA_BUF_LEN,
        // APLL gives cleaner clocking than the default PLL (lower jitter,
        // better SNR). On ESP32-S3, APLL does not conflict with WiFi/BT
        // (that restriction applied to classic ESP32 only). We disable it
        // before connecting to WiFi anyway (mic_audio_deinit is called first).
        .use_apll             = true,
        .tx_desc_auto_clear   = false,
        .fixed_mclk           = 0,
    };

    const i2s_pin_config_t pins = {
        .mck_io_num   = I2S_PIN_NO_CHANGE,
        .bck_io_num   = PIN_I2S_MIC_SCK,   // GPIO7
        .ws_io_num    = PIN_I2S_MIC_WS,    // GPIO16
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num  = PIN_I2S_MIC_SD,    // GPIO15
    };

    esp_err_t err = i2s_driver_install(MIC_I2S_PORT, &cfg, 0, NULL);
    if (err != ESP_OK) {
        Serial.printf("[MIC] i2s_driver_install failed: 0x%x\n", err);
        return false;
    }
    err = i2s_set_pin(MIC_I2S_PORT, &pins);
    if (err != ESP_OK) {
        Serial.printf("[MIC] i2s_set_pin failed: 0x%x\n", err);
        i2s_driver_uninstall(MIC_I2S_PORT);
        return false;
    }

    i2s_zero_dma_buffer(MIC_I2S_PORT);
    Serial.printf("[MIC] I²S ready — %d Hz, 24-bit stereo, APLL\n", MIC_SAMPLE_RATE);
    return true;
}

// ── Record into PSRAM ─────────────────────────────────────────
static MicBuf mic_record() {
    MicBuf result = {nullptr, 0};

    // ── PSRAM allocation ─────────────────────────────────────
    // ps_malloc() uses MALLOC_CAP_SPIRAM, placing the buffer in the 8 MB OPI
    // PSRAM rather than the 512 kB internal SRAM. WAV_TOTAL_BYTES ≈ 1.37 MB,
    // so internal RAM is not sufficient.
    uint8_t *buf = (uint8_t *)ps_malloc(WAV_TOTAL_BYTES);
    if (!buf) {
        Serial.println("[MIC] ps_malloc failed — is PSRAM enabled? (BOARD_HAS_PSRAM)");
        return result;
    }

    _write_wav_header(buf, WAV_DATA_BYTES);

    // DMA read buffer on the stack — 512 stereo frames × 4 bytes = 4 kB.
    int32_t dma_buf[MIC_DMA_FRAMES * 2];

    uint8_t       *dst         = buf + WAV_HEADER_BYTES;
    const uint8_t *end         = buf + WAV_TOTAL_BYTES;
    uint32_t       frames_left = (uint32_t)MIC_SAMPLE_RATE * MIC_REC_SECONDS;

    Serial.printf("[MIC] Recording %lu frames (%d s)...\n",
                  (unsigned long)frames_left, MIC_REC_SECONDS);

    while (frames_left > 0 && dst < end) {
        uint32_t want_frames = min((uint32_t)MIC_DMA_FRAMES, frames_left);
        size_t   want_bytes  = want_frames * 2 * sizeof(int32_t);
        size_t   got_bytes   = 0;

        i2s_read(MIC_I2S_PORT, dma_buf, want_bytes, &got_bytes, portMAX_DELAY);

        uint32_t got_samples = (uint32_t)(got_bytes / sizeof(int32_t));

        for (uint32_t i = 0; i < got_samples && dst + MIC_BYTES_PER_SAMP <= end; i++) {
            // ICS-43434 outputs 24-bit audio left-justified in the 32-bit I²S
            // slot: MSB at bit 31, LSB at bit 8, bits [7:0] = 0.
            // Arithmetic >>8 strips the trailing zeros and sign-extends the
            // 24-bit value into a 32-bit int ready for LE packing.
            int32_t val24 = dma_buf[i] >> 8;

            // Apply digital gain (MIC_GAIN_DB), then hard-clip to 24-bit range.
            static const float _gain = powf(10.0f, MIC_GAIN_DB / 20.0f);
            val24 = (int32_t)constrain((float)val24 * _gain, -8388608.0f, 8388607.0f);

            // Pack as 24-bit little-endian (WAV standard byte order).
            *dst++ = (uint8_t)( val24        & 0xFF);
            *dst++ = (uint8_t)((val24 >>  8) & 0xFF);
            *dst++ = (uint8_t)((val24 >> 16) & 0xFF);
        }

        frames_left -= (uint32_t)(got_bytes / (2 * sizeof(int32_t)));
    }

    Serial.println("[MIC] Recording complete");
    result.data        = buf;
    result.total_bytes = WAV_TOTAL_BYTES;
    return result;
}

// ── Teardown ──────────────────────────────────────────────────
// Uninstalls the I²S driver and cuts the mic power rail.
// Does NOT free the MicBuf — the caller owns the buffer.
static void mic_audio_deinit() {
    i2s_driver_uninstall(MIC_I2S_PORT);
    // Cut VDD to both ICS-43434s via the TPS22918 load switch.
    digitalWrite(PIN_MIC_EN, LOW);
}
