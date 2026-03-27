"""
receive_wav.py — interactive serial receiver for mic_test option 1
──────────────────────────────────────────────────────────────────
pip install pyserial
python tools/receive_wav.py [PORT] [output.wav]

Connects to the ESP32, mirrors all serial output to stdout, and forwards
your keypresses to the board — so you can use the mic_test menu directly
from this terminal without needing a separate serial monitor.

When WAV_START is detected the script switches to binary receive mode,
captures the WAV, confirms WAV_END, and writes the file.
"""
import sys, time, datetime, threading, struct
import serial
import serial.tools.list_ports

# ── config ────────────────────────────────────────────────────
BAUD         = 921600
WAIT_TIMEOUT = 180    # seconds to wait for WAV_START
CHUNK        = 8192

def find_esp32_port():
    keywords = ("esp32", "cp210", "ch340", "ch9102", "usb serial", "cdc", "uart")
    for p in serial.tools.list_ports.comports():
        if any(k in (p.description or "").lower() for k in keywords):
            return p.device
    return "COM4"

# ── receiver ──────────────────────────────────────────────────
def receive_wav(port: str, out_path: str) -> bool:
    print(f"[receive_wav] Opening {port} @ {BAUD} baud")
    try:
        ser = serial.Serial(port, BAUD, timeout=0.1)
    except serial.SerialException as e:
        print(f"[receive_wav] Cannot open port: {e}")
        return False

    ser.reset_input_buffer()

    wav_found   = threading.Event()
    total_bytes = [None]
    stop_stdin  = threading.Event()

    # ── stdin → serial forwarding thread ─────────────────────
    # Lets you type menu choices (e.g. "1") directly in this terminal.
    # Stops forwarding once WAV_START is seen (binary mode begins).
    def _stdin_fwd():
        while not stop_stdin.is_set() and not wav_found.is_set():
            try:
                line = input()          # blocks until Enter
            except (EOFError, OSError):
                break
            if not wav_found.is_set():
                ser.write((line + "\r\n").encode("ascii", errors="ignore"))

    threading.Thread(target=_stdin_fwd, daemon=True).start()

    print(f"[receive_wav] Ready — type your menu choice below (e.g. 1)")
    print()

    # ── wait for WAV_START, echo everything else ──────────────
    t0 = time.monotonic()
    while time.monotonic() - t0 < WAIT_TIMEOUT:
        raw = ser.readline()
        if not raw:
            continue
        line = raw.decode("ascii", errors="ignore").strip()
        if line:
            print(f"  > {line}", flush=True)
        if line.startswith("WAV_START"):
            parts = line.split()
            if len(parts) >= 2:
                try:
                    total_bytes[0] = int(parts[1])
                except ValueError:
                    pass
            wav_found.set()
            break
    else:
        stop_stdin.set()
        print("[receive_wav] Timed out waiting for WAV_START")
        ser.close()
        return False

    stop_stdin.set()

    n = total_bytes[0]
    if n is None:
        print("[receive_wav] WAV_START found but missing byte count")
        ser.close()
        return False

    # ── binary receive ────────────────────────────────────────
    size_mb = n / (1024 * 1024)
    print(f"[receive_wav] Receiving {n:,} bytes  ({size_mb:.2f} MB) → {out_path}")

    ser.timeout = 5
    buf     = bytearray()
    t_start = time.monotonic()
    t_print = t_start

    while len(buf) < n:
        chunk = ser.read(min(CHUNK, n - len(buf)))
        if not chunk:
            elapsed = time.monotonic() - t_start
            print(f"\n[receive_wav] Read timeout after {elapsed:.1f} s "
                  f"({len(buf):,} / {n:,} bytes)")
            ser.close()
            return False
        buf.extend(chunk)

        now = time.monotonic()
        if now - t_print >= 0.4:
            pct  = 100.0 * len(buf) / n
            kb_s = len(buf) / max(now - t_start, 0.001) / 1024
            print(f"  {len(buf):>10,} / {n:,}  ({pct:5.1f}%)  {kb_s:6.1f} KB/s",
                  end="\r", flush=True)
            t_print = now

    elapsed = time.monotonic() - t_start
    print(f"  {len(buf):>10,} / {n:,}  (100.0%)  "
          f"{len(buf) / elapsed / 1024:.1f} KB/s  — done          ")

    # ── verify WAV_END ────────────────────────────────────────
    ser.timeout = 3
    end_line = ser.readline().decode("ascii", errors="ignore").strip()
    if end_line == "WAV_END":
        print("[receive_wav] WAV_END confirmed")
    else:
        print(f"[receive_wav] Warning: expected WAV_END, got {repr(end_line)}")

    ser.close()

    with open(out_path, "wb") as f:
        f.write(buf)

    print(f"[receive_wav] Saved  {out_path}  ({len(buf) / 1024:,.0f} KB)")

    # ── WAV sanity check ──────────────────────────────────────
    if buf[:4] == b"RIFF" and buf[8:12] == b"WAVE":
        sr  = struct.unpack_from("<I", buf, 24)[0]
        ch  = struct.unpack_from("<H", buf, 22)[0]
        bps = struct.unpack_from("<H", buf, 34)[0]
        dur = struct.unpack_from("<I", buf, 40)[0] / (sr * ch * (bps // 8))
        print(f"[receive_wav] WAV: {sr} Hz · {bps}-bit · {ch}ch · {dur:.2f} s")
    else:
        print("[receive_wav] Warning: RIFF header not found — file may be corrupt")

    return True

# ── entry ─────────────────────────────────────────────────────
if __name__ == "__main__":
    port = sys.argv[1] if len(sys.argv) > 1 else find_esp32_port()
    ts   = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    out  = sys.argv[2] if len(sys.argv) > 2 else f"output_{ts}.wav"

    print(f"[receive_wav] Port: {port}  |  Output: {out}")
    ok = receive_wav(port, out)
    sys.exit(0 if ok else 1)
