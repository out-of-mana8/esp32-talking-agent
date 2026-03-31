#pragma once
//
// health.h — Device health web dashboard for ESP32 Talking Agent
//
// Serves a dark-themed status page on port 80 showing:
//   Battery (MAX17048), IMU (LSM6DSOX), load switches, voice agent state, system info
//
// Runs via ESPAsyncWebServer — its own internal FreeRTOS task.
// Never touches loop(), I²S, or the WebSocket client.
//
// Usage:
//   health_init(mic_ok, spk_ok);           // once in setup(), after WiFi
//   health_update(ws_connected, mic_rms);  // every loop() iteration
//

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <SparkFun_MAX1704x_Fuel_Gauge_Arduino_Library.h>
#include "config.h"

// ── LSM6DSOX raw register map ─────────────────────────────────
#define _LSM6_WHO_AM_I   0x0F   // expected: 0x6C (LSM6DSOX)
#define _LSM6_CTRL1_XL   0x10   // accel: 0x40 = 104 Hz, ±2g
#define _LSM6_CTRL2_G    0x11   // gyro:  0x40 = 104 Hz, 250 dps
#define _LSM6_OUT_TEMP_L 0x20   // temperature (2 bytes)
#define _LSM6_OUTX_L_G   0x22   // gyro XYZ (6 bytes)
#define _LSM6_OUTX_L_A   0x28   // accel XYZ (6 bytes)

#define _LSM6_ACCEL_SCALE  0.000061f    // g per LSB at ±2g
#define _LSM6_GYRO_SCALE   0.00875f     // dps per LSB at 250 dps
#define _LSM6_TEMP_SCALE   (1.0f/256.0f)
#define _LSM6_TEMP_OFFSET  25.0f        // °C at raw=0

#define HEALTH_SENSOR_INTERVAL_MS  5000

// ── Shared state (written by loop/mic_task, read by web handler) ──
struct HealthData {
    // Set at init
    bool mic_ok;
    bool spk_ok;

    // Updated every loop() call
    volatile bool    ws_connected;
    volatile float   mic_rms;

    // Updated every 5 s (from _sensor_read in loop context)
    volatile float   batt_pct;
    volatile float   batt_volts;
    volatile float   batt_crate;      // %/hr; positive = charging
    volatile bool    batt_ok;

    volatile bool    imu_ok;
    volatile float   ax, ay, az;      // g
    volatile float   gx, gy, gz;      // dps
    volatile float   imu_temp;        // °C

    volatile bool    pin_mic_en;
    volatile bool    pin_spk_en;
    volatile bool    pin_tft_en;

    volatile int32_t  rssi;
    volatile uint32_t free_heap;
    volatile uint32_t uptime_s;
    char              ip_str[16];     // written once in init
};

// ── Module-private state ──────────────────────────────────────
static HealthData     _hd;
static SFE_MAX1704X   _lipo(MAX1704X_MAX17048);
static AsyncWebServer _hsrv(80);
static bool           _lipo_ok = false;
static bool           _imu_ok  = false;

// ── HTML dashboard ────────────────────────────────────────────
static const char _health_html[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32 Health</title>
<style>
:root{--bg:#0f0f0f;--card:#1a1a1a;--border:#252525;
     --ok:#22c55e;--err:#ef4444;--warn:#f59e0b;
     --text:#e5e5e5;--dim:#666;--accent:#00f0c8;}
*{box-sizing:border-box;margin:0;padding:0;}
body{background:var(--bg);color:var(--text);
     font-family:'Courier New',Courier,monospace;font-size:13px;padding:20px;}
header{display:flex;justify-content:space-between;align-items:center;
       margin-bottom:20px;}
h1{font-size:16px;color:var(--accent);letter-spacing:3px;text-transform:uppercase;}
#sub{font-size:11px;color:var(--dim);}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(270px,1fr));gap:12px;}
.card{background:var(--card);border:1px solid var(--border);border-radius:8px;padding:14px;}
.card-title{font-size:10px;color:var(--dim);text-transform:uppercase;
            letter-spacing:2px;margin-bottom:10px;}
.row{display:flex;justify-content:space-between;align-items:center;
     padding:4px 0;border-bottom:1px solid var(--border);}
.row:last-child{border-bottom:none;}
.lbl{color:var(--dim);}
.val{text-align:right;}
.badge{display:inline-block;padding:1px 7px;border-radius:3px;
       font-size:11px;font-weight:bold;}
.ok  {background:#052e16;color:var(--ok);border:1px solid #166534;}
.err {background:#2d0a0a;color:var(--err);border:1px solid #7f1d1d;}
.warn{background:#2d1a00;color:var(--warn);border:1px solid #78350f;}
.off {background:#111;color:#444;border:1px solid #333;}
.dot {display:inline-block;width:8px;height:8px;border-radius:50%;margin-right:5px;}
.dot.ok {background:var(--ok);box-shadow:0 0 6px #22c55e88;
         animation:pulse 2s ease-in-out infinite;}
.dot.err{background:var(--err);}
@keyframes pulse{0%,100%{opacity:1}50%{opacity:.4}}
footer{margin-top:16px;font-size:10px;color:var(--dim);text-align:right;}
</style>
</head>
<body>
<header>
  <h1>&#9642; ESP32-S3 Talking Agent</h1>
  <span id="sub">—</span>
</header>
<div class="grid" id="grid"><div class="card"><div class="card-title">Connecting…</div></div></div>
<footer id="ts"></footer>
<script>
function b(ok,t,f){t=t||'OK';f=f||'ERR';return'<span class="badge '+(ok?'ok':'err')+'">'+( ok?t:f)+'</span>';}
function dot(ok){return'<span class="dot '+(ok?'ok':'err')+'"></span>';}
function row(l,v){return'<div class="row"><span class="lbl">'+l+'</span><span class="val">'+v+'</span></div>';}
function card(t,r){return'<div class="card"><div class="card-title">'+t+'</div>'+r+'</div>';}
function f(n,d){return typeof n==='number'?n.toFixed(d===undefined?2:d):'—';}
function uptime(s){var h=Math.floor(s/3600),m=Math.floor((s%3600)/60),ss=s%60;
  return(h?h+'h ':'')+(m?m+'m ':'')+ss+'s';}
function render(d){
  document.getElementById('sub').textContent=d.ip+'  ·  up '+uptime(d.uptime_s);
  var charging=d.batt_crate>0.1?'<span class="badge ok">CHARGING</span>':
               d.batt_crate<-0.1?'<span class="badge warn">DISCHARGING</span>':
               '<span class="badge off">IDLE</span>';
  var html=
    card('Battery',
      row('Fuel gauge',    b(d.batt_ok))+
      row('State of charge', f(d.batt_pct,1)+'%')+
      row('Voltage',       f(d.batt_volts,3)+' V')+
      row('Change rate',   f(d.batt_crate,2)+'%/hr &nbsp;'+charging))+

    card('Voice Agent',
      row('Server WS',    dot(d.ws_connected)+(d.ws_connected?'Connected':'Disconnected'))+
      row('Microphone',   b(d.mic_ok))+
      row('Speaker',      b(d.spk_ok))+
      row('Mic RMS',      f(d.mic_rms,4)))+

    card('Load Switches',
      row('MIC_EN  (GPIO 6)',  b(d.pin_mic_en,'HIGH','LOW'))+
      row('SPK_EN  (GPIO 9)',  b(d.pin_spk_en,'HIGH','LOW'))+
      row('TFT_EN  (GPIO 38)', d.pin_tft_en?'<span class="badge ok">HIGH</span>':'<span class="badge off">LOW</span>'))+

    card('IMU — LSM6DSOX',
      row('Status',   b(d.imu_ok,'DETECTED','NOT FOUND'))+
      (d.imu_ok?
        row('Accel X/Y/Z', f(d.ax)+' / '+f(d.ay)+' / '+f(d.az)+' g')+
        row('Gyro X/Y/Z',  f(d.gx,1)+' / '+f(d.gy,1)+' / '+f(d.gz,1)+' °/s')+
        row('Die temp',    f(d.imu_temp,1)+' °C')
      :''))+

    card('System',
      row('IP address', d.ip)+
      row('WiFi RSSI',  d.rssi+' dBm')+
      row('Free heap',  f(d.free_heap/1024,1)+' kB')+
      row('CPU',        d.cpu_mhz+' MHz')+
      row('Uptime',     uptime(d.uptime_s)));

  document.getElementById('grid').innerHTML=html;
  document.getElementById('ts').textContent='Updated '+new Date().toLocaleTimeString();
}
(function poll(){
  fetch('/api/status')
    .then(function(r){return r.json();})
    .then(render)
    .catch(function(e){document.getElementById('ts').textContent='Error: '+e;});
  setTimeout(poll,3000);
})();
</script>
</body>
</html>
)rawhtml";

// ── Raw I²C helpers ───────────────────────────────────────────
static bool _i2c_write(uint8_t addr, uint8_t reg, uint8_t val) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    Wire.write(val);
    return Wire.endTransmission() == 0;
}

static bool _i2c_read(uint8_t addr, uint8_t reg,
                       uint8_t *buf, uint8_t len) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((uint8_t)addr, len) != len) return false;
    for (uint8_t i = 0; i < len; i++) buf[i] = Wire.read();
    return true;
}

static int16_t _le16(const uint8_t *b) {
    return (int16_t)((uint16_t)b[1] << 8 | b[0]);
}

// ── LSM6DSOX init ─────────────────────────────────────────────
static bool _imu_init() {
    uint8_t who = 0;
    if (!_i2c_read(I2C_ADDR_LSM6DS, _LSM6_WHO_AM_I, &who, 1)) {
        Serial.println("[HEALTH] IMU: WHO_AM_I read failed");
        return false;
    }
    if (who != 0x6C) {
        Serial.printf("[HEALTH] IMU: unexpected WHO_AM_I 0x%02X (want 0x6C)\n", who);
        return false;
    }
    _i2c_write(I2C_ADDR_LSM6DS, _LSM6_CTRL1_XL, 0x40);  // 104 Hz, ±2g
    _i2c_write(I2C_ADDR_LSM6DS, _LSM6_CTRL2_G,  0x40);  // 104 Hz, 250 dps
    Serial.println("[HEALTH] IMU: LSM6DSOX OK  104 Hz  ±2g  250 dps");
    return true;
}

// ── Sensor read (called every 5 s from loop context) ─────────
static void _sensor_read() {
    // Load switch pin states
    _hd.pin_mic_en = digitalRead(PIN_MIC_EN);
    _hd.pin_spk_en = digitalRead(PIN_SPK_EN);
    _hd.pin_tft_en = digitalRead(PIN_TFT_EN);

    // System
    _hd.rssi      = WiFi.RSSI();
    _hd.free_heap = esp_get_free_heap_size();
    _hd.uptime_s  = millis() / 1000;

    // Battery
    if (_lipo_ok) {
        _hd.batt_pct   = _lipo.getSOC();
        _hd.batt_volts = _lipo.getVoltage();
        _hd.batt_crate = _lipo.getChangeRate();
    }

    // IMU
    if (_imu_ok) {
        uint8_t raw[6];

        if (_i2c_read(I2C_ADDR_LSM6DS, _LSM6_OUT_TEMP_L, raw, 2))
            _hd.imu_temp = _LSM6_TEMP_OFFSET + _le16(raw) * _LSM6_TEMP_SCALE;

        if (_i2c_read(I2C_ADDR_LSM6DS, _LSM6_OUTX_L_G, raw, 6)) {
            _hd.gx = _le16(raw + 0) * _LSM6_GYRO_SCALE;
            _hd.gy = _le16(raw + 2) * _LSM6_GYRO_SCALE;
            _hd.gz = _le16(raw + 4) * _LSM6_GYRO_SCALE;
        }

        if (_i2c_read(I2C_ADDR_LSM6DS, _LSM6_OUTX_L_A, raw, 6)) {
            _hd.ax = _le16(raw + 0) * _LSM6_ACCEL_SCALE;
            _hd.ay = _le16(raw + 2) * _LSM6_ACCEL_SCALE;
            _hd.az = _le16(raw + 4) * _LSM6_ACCEL_SCALE;
        }
    }
}

// ── Public: init ──────────────────────────────────────────────
inline void health_init(bool mic_ok, bool spk_ok) {
    memset(&_hd, 0, sizeof(_hd));
    _hd.mic_ok = mic_ok;
    _hd.spk_ok = spk_ok;
    _hd.batt_ok = false;
    _hd.imu_ok  = false;
    strncpy(_hd.ip_str, WiFi.localIP().toString().c_str(), sizeof(_hd.ip_str) - 1);

    // I²C
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    Serial.printf("[HEALTH] I²C  SDA=%d SCL=%d\n", PIN_I2C_SDA, PIN_I2C_SCL);

    // MAX17048
    if (_lipo.begin(Wire)) {
        _lipo_ok = true;
        _hd.batt_ok = true;
        Serial.println("[HEALTH] MAX17048 OK");
    } else {
        Serial.println("[HEALTH] MAX17048 not found — check I²C wiring");
    }

    // LSM6DSOX
    _imu_ok = _imu_init();
    _hd.imu_ok = _imu_ok;

    // Do a first sensor read immediately so the page isn't blank
    _sensor_read();

    // HTTP routes
    _hsrv.on("/", HTTP_GET, [](AsyncWebServerRequest *req) {
        req->send_P(200, "text/html", _health_html);
    });

    _hsrv.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *req) {
        char buf[640];
        snprintf(buf, sizeof(buf),
            "{"
            "\"batt_ok\":%s,\"batt_pct\":%.2f,\"batt_volts\":%.3f,\"batt_crate\":%.2f,"
            "\"ws_connected\":%s,\"mic_ok\":%s,\"spk_ok\":%s,\"mic_rms\":%.4f,"
            "\"pin_mic_en\":%s,\"pin_spk_en\":%s,\"pin_tft_en\":%s,"
            "\"imu_ok\":%s,"
            "\"ax\":%.4f,\"ay\":%.4f,\"az\":%.4f,"
            "\"gx\":%.3f,\"gy\":%.3f,\"gz\":%.3f,"
            "\"imu_temp\":%.2f,"
            "\"ip\":\"%s\",\"rssi\":%ld,"
            "\"free_heap\":%lu,\"cpu_mhz\":%u,\"uptime_s\":%lu"
            "}",
            _hd.batt_ok    ? "true" : "false",
            (float)_hd.batt_pct, (float)_hd.batt_volts, (float)_hd.batt_crate,
            _hd.ws_connected ? "true" : "false",
            _hd.mic_ok     ? "true" : "false",
            _hd.spk_ok     ? "true" : "false",
            (float)_hd.mic_rms,
            _hd.pin_mic_en ? "true" : "false",
            _hd.pin_spk_en ? "true" : "false",
            _hd.pin_tft_en ? "true" : "false",
            _hd.imu_ok     ? "true" : "false",
            (float)_hd.ax, (float)_hd.ay, (float)_hd.az,
            (float)_hd.gx, (float)_hd.gy, (float)_hd.gz,
            (float)_hd.imu_temp,
            _hd.ip_str, (long)_hd.rssi,
            (unsigned long)_hd.free_heap,
            (unsigned)getCpuFrequencyMhz(),
            (unsigned long)_hd.uptime_s
        );
        req->send(200, "application/json", buf);
    });

    _hsrv.begin();
    Serial.printf("[HEALTH] Dashboard → http://%s/\n", _hd.ip_str);
}

// ── Public: update (call every loop()) ───────────────────────
inline void health_update(bool ws_connected, float mic_rms) {
    _hd.ws_connected = ws_connected;
    _hd.mic_rms      = mic_rms;

    static uint32_t _last_ms = 0;
    uint32_t now = millis();
    if (now - _last_ms >= HEALTH_SENSOR_INTERVAL_MS) {
        _last_ms = now;
        _sensor_read();
    }
}
