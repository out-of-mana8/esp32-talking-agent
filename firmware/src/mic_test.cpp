//
// mic_test.cpp — ICS-43434 stereo mic capture for ESP32 Talking Agent
//
// Flash:  pio run -e mic_test -t upload
// Monitor: pio device monitor -e mic_test   (921600 baud)
//
// On boot, prompts:
//   [1] Record + dump via USB Serial   → receive with tools/receive_wav.py
//   [2] Record + serve via WiFi HTTP   → open the printed IP in a browser
//
// Audio: 48 kHz · 24-bit · stereo · 5 s
// Buffer: ~1.37 MB in PSRAM (ps_malloc)
// I²S driver: ESP-IDF v5 (driver/i2s_std.h)  — needs arduino-esp32 >= 3.0.0
//
// Do not include this file from other environments; it defines its own
// setup() and loop(). The [env:mic_test] build_src_filter isolates it.
//
#include <Arduino.h>
#include "mic_audio.h"
#include "mic_serial.h"
#include "mic_wifi.h"

// ── State ─────────────────────────────────────────────────────
static enum { WAITING, RECORDING, TRANSFERRING } _state = WAITING;
static char _choice = 0;

// ── Helpers ───────────────────────────────────────────────────
static void print_menu() {
    Serial.println();
    Serial.println("=== mic_test ===");
    Serial.printf ("  Sample rate : %d Hz\n",        MIC_SAMPLE_RATE);
    Serial.printf ("  Bit depth   : %d-bit stereo\n", MIC_BITS_PER_SAMPLE);
    Serial.printf ("  Duration    : %d s\n",          MIC_REC_SECONDS);
    Serial.printf ("  Buffer      : %.2f MB PSRAM\n",
                   (float)WAV_TOTAL_BYTES / (1024.0f * 1024.0f));
    Serial.println();
    Serial.println("[1] Record + dump via USB Serial");
    Serial.println("[2] Record + serve via WiFi HTTP");
    Serial.println();
    Serial.print  ("Enter choice: ");
    _state = WAITING;
}

// ── setup ─────────────────────────────────────────────────────
void setup() {
    Serial.begin(921600);
    delay(200);

    pinMode(PIN_LED_1, OUTPUT);
    pinMode(PIN_LED_2, OUTPUT);
    digitalWrite(PIN_LED_1, LOW);
    digitalWrite(PIN_LED_2, LOW);

    print_menu();
}

// ── loop ──────────────────────────────────────────────────────
void loop() {
    if (_state != WAITING) return;
    if (!Serial.available()) return;

    char c = Serial.read();
    if (c != '1' && c != '2') return;

    _choice = c;
    Serial.println(c);  // echo

    // ── Record ────────────────────────────────────────────────
    if (!mic_audio_init()) {
        Serial.println("[MIC] I²S init failed — check wiring and PSRAM");
        print_menu();
        return;
    }

    digitalWrite(PIN_LED_2, HIGH);   // LED2 solid = recording in progress
    MicBuf buf = mic_record();
    digitalWrite(PIN_LED_2, LOW);

    mic_audio_deinit();              // stop I²S and cut mic power rail

    if (!buf.data) {
        Serial.println("[MIC] Recording failed — PSRAM allocation error?");
        print_menu();
        return;
    }

    // ── Transfer ──────────────────────────────────────────────
    if (_choice == '1') {
        // Option 1: binary dump over USB CDC Serial.
        // tools/receive_wav.py stays connected through the whole session
        // (menu + recording + dump) so no separate prompt is needed.
        mic_serial_dump(&buf);
        free(buf.data);
        buf.data = nullptr;
        print_menu();

    } else {
        // Option 2: WiFi HTTP.
        // mic_wifi_serve blocks until the user clicks Re-record in the browser,
        // then returns so we can record again without rebooting.
        while (true) {
            mic_wifi_serve(&buf);

            // Re-record was requested — overwrite the existing buffer.
            // mic_audio_deinit() already ran; re-init for a fresh take.
            if (!mic_audio_init()) {
                Serial.println("[MIC] I²S re-init failed");
                break;
            }

            digitalWrite(PIN_LED_2, HIGH);
            MicBuf new_buf = mic_record();
            digitalWrite(PIN_LED_2, LOW);

            mic_audio_deinit();

            // Swap: free the old PSRAM buffer, use the new one.
            free(buf.data);
            buf = new_buf;

            if (!buf.data) {
                Serial.println("[MIC] Re-recording failed");
                break;
            }
        }

        if (buf.data) free(buf.data);
        print_menu();
    }
}
