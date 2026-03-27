#pragma once
//
// mic_wifi.h — WAV served over WiFi HTTP (mic_test option 2)
//
// Connects to WiFi using credentials from firmware/include/secrets.h.
// Endpoints:
//   GET  /            — HTML page with download button + re-record button
//   GET  /record.wav  — streams the WAV file with correct headers
//   POST /rerecord    — triggers a new recording (mic_wifi_serve returns)
//
// mic_wifi_serve() blocks until /rerecord is posted, then returns so the
// caller can re-record and call mic_wifi_serve() again with the new buffer.
//
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include "mic_audio.h"
#include "wifi_config.h"   // defines WIFI_SSID, WIFI_PASS, shared with imu_test

static WebServer _http(80);
static MicBuf   *_wav_buf           = nullptr;
static bool      _rerecord_pending  = false;

// ── HTML page ─────────────────────────────────────────────────
// Stored in flash (PROGMEM) to save heap.
static const char _HTML[] PROGMEM = R"html(<!DOCTYPE html>
<html><head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>ESP32 Mic Test</title>
  <style>
    body  { font-family: monospace; background: #03030a; color: #c8e0ff;
            padding: 2rem; margin: 0; }
    h1   { color: #00f0c8; margin-bottom: .25rem; }
    p    { color: #303858; margin-top: .25rem; }
    .btn { display: inline-block; margin: .5rem .3rem 0;
           padding: .55rem 1.4rem; border: 1px solid #1c1c48;
           background: #07071e; color: #00f0c8; cursor: pointer;
           font-family: monospace; font-size: 1rem; text-decoration: none; }
    .btn:hover { background: #10103a; }
    form  { display: inline; }
  </style>
</head><body>
  <h1>// ESP32 Mic Test</h1>
  <p>%u Hz &nbsp;|&nbsp; 24-bit stereo &nbsp;|&nbsp; %u s &nbsp;|&nbsp; %.1f MB</p>
  <a class="btn" href="/record.wav" download="record.wav">&#8595;&nbsp;Download WAV</a>
  <form action="/rerecord" method="POST">
    <button class="btn" type="submit">&#9654;&nbsp;Re-record</button>
  </form>
</body></html>)html";

// ── Handlers ──────────────────────────────────────────────────
static void _wifi_handle_root() {
    char page[sizeof(_HTML) + 64];
    float size_mb = (float)WAV_TOTAL_BYTES / (1024.0f * 1024.0f);
    snprintf(page, sizeof(page), _HTML,
             MIC_SAMPLE_RATE, MIC_REC_SECONDS, size_mb);
    _http.send(200, "text/html", page);
}

static void _wifi_handle_wav() {
    if (!_wav_buf || !_wav_buf->data) {
        _http.send(503, "text/plain", "No recording available\n");
        return;
    }

    _http.sendHeader("Content-Disposition",
                     "attachment; filename=\"record.wav\"");
    _http.setContentLength(_wav_buf->total_bytes);
    _http.send(200, "audio/wav", "");

    // Stream in 8 kB chunks — WebServer.send() has no built-in large-file path.
    WiFiClient      client    = _http.client();
    const uint8_t  *ptr       = _wav_buf->data;
    uint32_t        remaining = _wav_buf->total_bytes;

    while (remaining > 0 && client.connected()) {
        uint32_t chunk = min(remaining, (uint32_t)8192);
        client.write(ptr, (size_t)chunk);
        ptr       += chunk;
        remaining -= chunk;
    }

    Serial.printf("[HTTP] Served record.wav (%lu bytes)\n",
                  (unsigned long)_wav_buf->total_bytes);
}

static void _wifi_handle_rerecord() {
    _rerecord_pending = true;
    _http.sendHeader("Location", "/");
    _http.send(303, "text/plain", "");
    Serial.println("[HTTP] Re-record requested");
}

// ── WiFi connect ──────────────────────────────────────────────
static bool _mic_wifi_connect() {
    Serial.printf("[WIFI] Connecting to \"%s\"", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - t0 > 20000) {
            Serial.println("\n[WIFI] Connection timed out");
            return false;
        }
        delay(500);
        Serial.print('.');
    }

    Serial.printf("\n[WIFI] Connected — IP: %s\n",
                  WiFi.localIP().toString().c_str());
    return true;
}

// ── Public entry point ────────────────────────────────────────
// Starts the HTTP server and blocks until /rerecord is POSTed.
// Returns so the caller can record again and call this again.
static void mic_wifi_serve(MicBuf *buf) {
    _wav_buf          = buf;
    _rerecord_pending = false;

    if (!_mic_wifi_connect()) return;

    _http.on("/",           HTTP_GET,  _wifi_handle_root);
    _http.on("/record.wav", HTTP_GET,  _wifi_handle_wav);
    _http.on("/rerecord",   HTTP_POST, _wifi_handle_rerecord);
    _http.begin();

    Serial.printf("[HTTP] Serving at http://%s/\n",
                  WiFi.localIP().toString().c_str());
    Serial.printf("[HTTP] Direct download: http://%s/record.wav\n",
                  WiFi.localIP().toString().c_str());

    while (!_rerecord_pending) {
        _http.handleClient();
        delay(2);
    }

    _http.stop();
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
}
