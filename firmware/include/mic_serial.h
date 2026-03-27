#pragma once
//
// mic_serial.h — WAV dump over USB CDC Serial (mic_test option 1)
//
// Protocol:
//   WAV_START <total_bytes>\r\n
//   <binary WAV data — header + PCM>
//   WAV_END\r\n
//
// Receiver: tools/receive_wav.py
// LED1 (GPIO1) blinks during transfer to show progress.
//
#include <Arduino.h>
#include "mic_audio.h"
#include "config.h"

#define SERIAL_CHUNK_BYTES   4096
#define SERIAL_LED_BLINK_MS  150

static void mic_serial_dump(const MicBuf *buf) {
    if (!buf || !buf->data || buf->total_bytes == 0) return;

    // Announce transfer with byte count so the receiver knows when to stop.
    Serial.printf("WAV_START %lu\r\n", (unsigned long)buf->total_bytes);
    Serial.flush();

    const uint8_t *ptr       = buf->data;
    uint32_t       remaining = buf->total_bytes;
    bool           led_on    = false;
    uint32_t       last_blink = millis();

    while (remaining > 0) {
        if (millis() - last_blink >= SERIAL_LED_BLINK_MS) {
            led_on = !led_on;
            digitalWrite(PIN_LED_1, led_on ? HIGH : LOW);
            last_blink = millis();
        }

        uint32_t chunk = min(remaining, (uint32_t)SERIAL_CHUNK_BYTES);
        Serial.write(ptr, (size_t)chunk);
        ptr       += chunk;
        remaining -= chunk;
    }

    Serial.flush();
    Serial.print("\r\nWAV_END\r\n");
    Serial.flush();

    digitalWrite(PIN_LED_1, LOW);
    Serial.printf("[MIC] Serial dump complete — %lu bytes\n",
                  (unsigned long)buf->total_bytes);
}
