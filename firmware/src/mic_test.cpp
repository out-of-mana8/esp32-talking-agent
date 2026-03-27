//
// mic_test.cpp — ICS-43434 stereo I2S microphone diagnostic
// Flash with: pio run -e mic-test -t upload
//
// IO6 (MIC_EN) powers the mics via a TPS22918 load switch — driven HIGH here.
// Both mics share one I2S data line (SD); each drives only its own WS phase:
//   Mic with SEL=GND  → left  channel (WS = 0)
//   Mic with SEL=VDD  → right channel (WS = 1)
//
// Output CSV every ~20 ms: rms_l,rms_r,peak_l,peak_r  (normalised 0..1)
//
#include <Arduino.h>
#include <driver/i2s.h>
#include "config.h"

// ── I2S config ────────────────────────────────────────────────
#define I2S_PORT        I2S_NUM_0
#define SAMPLE_RATE     16000
#define FRAMES_PER_READ 320    // 320 frames @ 16 kHz = 20 ms → 50 Hz serial rate
#define DMA_BUF_COUNT   4
#define DMA_BUF_FRAMES  FRAMES_PER_READ

// Interleaved stereo buffer: [ch0_0, ch1_0, ch0_1, ch1_1, ...]
// ch0 = left  channel (WS=0 phase)
// ch1 = right channel (WS=1 phase)
static int32_t dma[FRAMES_PER_READ * 2];

// ── helpers ───────────────────────────────────────────────────
static void printBar(float norm, uint8_t width = 28) {
    int fill = (int)(norm * width);
    Serial.print('[');
    for (int i = 0; i < width; i++) Serial.print(i < fill ? '#' : ' ');
    Serial.print(']');
}

// ── setup ─────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(300);

    Serial.println("=========================================");
    Serial.println("  ICS-43434  Stereo Mic  Diagnostic");
    Serial.println("=========================================");
    Serial.printf("  MIC_EN  : GPIO%-2d  (HIGH → load switch ON)\n", PIN_MIC_EN);
    Serial.printf("  SCK     : GPIO%-2d\n", PIN_I2S_MIC_SCK);
    Serial.printf("  SD      : GPIO%-2d\n", PIN_I2S_MIC_SD);
    Serial.printf("  WS/LR   : GPIO%-2d\n", PIN_I2S_MIC_WS);
    Serial.printf("  Rate    : %d Hz,  20 ms blocks\n", SAMPLE_RATE);
    Serial.println("=========================================\n");

    // ── power on mics ─────────────────────────────────────────
    pinMode(PIN_MIC_EN, OUTPUT);
    digitalWrite(PIN_MIC_EN, HIGH);
    delay(30);   // ICS-43434 startup time ≤ 10 ms; give a bit extra
    Serial.println("[MIC] Load switch ON — mics powered");

    // ── I2S driver install ────────────────────────────────────
    const i2s_config_t cfg = {
        .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate          = SAMPLE_RATE,
        .bits_per_sample      = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format       = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count        = DMA_BUF_COUNT,
        .dma_buf_len          = DMA_BUF_FRAMES,
        .use_apll             = false,
        .tx_desc_auto_clear   = false,
        .fixed_mclk           = 0,
    };

    const i2s_pin_config_t pins = {
        .mck_io_num   = I2S_PIN_NO_CHANGE,
        .bck_io_num   = PIN_I2S_MIC_SCK,
        .ws_io_num    = PIN_I2S_MIC_WS,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num  = PIN_I2S_MIC_SD,
    };

    esp_err_t err = i2s_driver_install(I2S_PORT, &cfg, 0, NULL);
    if (err != ESP_OK) {
        Serial.printf("[MIC] ERROR: i2s_driver_install failed (0x%x)\n", err);
        while (1) delay(100);
    }

    err = i2s_set_pin(I2S_PORT, &pins);
    if (err != ESP_OK) {
        Serial.printf("[MIC] ERROR: i2s_set_pin failed (0x%x)\n", err);
        while (1) delay(100);
    }

    i2s_zero_dma_buffer(I2S_PORT);
    Serial.println("[MIC] I2S running\n");
    Serial.println("--- CSV: rms_l,rms_r,peak_l,peak_r  (0..1 normalised) ---");
}

// ── loop ──────────────────────────────────────────────────────
static uint32_t block = 0;

void loop() {
    size_t bytes_read = 0;
    i2s_read(I2S_PORT, dma, sizeof(dma), &bytes_read, portMAX_DELAY);

    int n = (int)(bytes_read / sizeof(int32_t));

    double sum_r = 0, sum_l = 0;
    int32_t pk_r = 0,  pk_l = 0;
    int count = 0;

    for (int i = 0; i + 1 < n; i += 2) {
        // ICS-43434: 24-bit value left-justified in 32-bit word → shift right 8
        int32_t s_l = (int32_t)dma[i]   >> 8;   // ch0 (left,  WS=0)
        int32_t s_r = (int32_t)dma[i+1] >> 8;   // ch1 (right, WS=1)

        sum_r += (double)s_r * s_r;
        sum_l += (double)s_l * s_l;

        int32_t ar = abs(s_r), al = abs(s_l);
        if (ar > pk_r) pk_r = ar;
        if (al > pk_l) pk_l = al;
        count++;
    }

    if (count < 1) return;

    const double SCALE = 1.0 / (double)(1 << 23);   // normalise 24-bit peak to 0..1
    float rms_r = (float)(sqrt(sum_r / count) * SCALE);
    float rms_l = (float)(sqrt(sum_l / count) * SCALE);
    float peak_r = (float)(pk_r * SCALE);
    float peak_l = (float)(pk_l * SCALE);

    // CSV for visualiser
    Serial.printf("%.5f,%.5f,%.5f,%.5f\n", rms_l, rms_r, peak_l, peak_r);

    // Every 25 blocks (~0.5 s) also print a human-readable summary
    block++;
    if (block % 25 == 0) {
        // convert to dBFS
        float db_l = (rms_l > 0) ? 20.0f * log10f(rms_l) : -99.0f;
        float db_r = (rms_r > 0) ? 20.0f * log10f(rms_r) : -99.0f;

        Serial.printf("\n  L  %+6.1f dBFS  ", db_l);
        printBar(fminf(rms_l * 10.0f, 1.0f));
        Serial.printf("  %s\n", db_l > -60.0f ? "OK" : "SILENT");

        Serial.printf("  R  %+6.1f dBFS  ", db_r);
        printBar(fminf(rms_r * 10.0f, 1.0f));
        Serial.printf("  %s\n\n", db_r > -60.0f ? "OK" : "SILENT");
    }
}
