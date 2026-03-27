"""
ICS-43434 Stereo Mic Visualizer — Cyberpunk Edition
Reads CSV from mic_test firmware: rms_l,rms_r,peak_l,peak_r  (normalised 0..1)
Usage:  python mic_visualizer.py [PORT] [BAUD]
Deps:   pip install pyserial pygame
"""
import math, re, sys, threading, time
import pygame
import serial
from collections import deque

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM4"
BAUD = int(sys.argv[2]) if len(sys.argv) > 2 else 115200

DB_MIN    = -60.0
DB_MAX    =   0.0
HISTORY   = 400       # rolling samples in history chart

# ── palette ───────────────────────────────────────────────────
BG      = (  4,   4,  14)
PANEL   = (  8,   8,  22)
GRID    = ( 14,  14,  38)
BORDER  = ( 30,  30,  80)
DIM     = ( 50,  60,  90)
WHITE   = (220, 230, 255)

L_COL   = (  0, 255, 220)   # cyan
R_COL   = (255,  50, 200)   # magenta

SEG_COLORS = [
    ((  0, 210,  70), (-60.0, -18.0)),   # green
    ((200, 210,   0), (-18.0,  -9.0)),   # yellow
    ((255, 130,   0), ( -9.0,  -3.0)),   # orange
    ((255,  35,  35), ( -3.0,   0.0)),   # red
]

def seg_color(db, active):
    for color, (lo, hi) in SEG_COLORS:
        if db < hi:
            return color if active else tuple(c // 7 for c in color)
    return SEG_COLORS[-1][0] if active else tuple(c // 7 for c in SEG_COLORS[-1][0])

def to_db(v):
    return 20.0 * math.log10(max(v, 1e-7))

# ── shared state ──────────────────────────────────────────────
data    = {'rms_l': 0.0, 'rms_r': 0.0, 'peak_l': 0.0, 'peak_r': 0.0}
hist_l  = deque([DB_MIN] * HISTORY, HISTORY)
hist_r  = deque([DB_MIN] * HISTORY, HISTORY)
status  = {'conn': False, 'hz': 0.0}
lock    = threading.Lock()

def serial_reader():
    while True:
        try:
            ser = serial.Serial(PORT, BAUD, timeout=0.5)
            with lock: status['conn'] = True
            t0, frames = time.monotonic(), 0
            while True:
                raw = ser.readline().decode('utf-8', errors='ignore').strip()
                parts = raw.split(',')
                if len(parts) == 4:
                    try:
                        rl, rr, pl, pr = map(float, parts)
                        with lock:
                            data['rms_l']  = rl
                            data['rms_r']  = rr
                            data['peak_l'] = pl
                            data['peak_r'] = pr
                            hist_l.append(to_db(rl))
                            hist_r.append(to_db(rr))
                            frames += 1
                            elapsed = time.monotonic() - t0
                            if elapsed >= 1.0:
                                status['hz'] = frames / elapsed
                                frames, t0 = 0, time.monotonic()
                    except ValueError:
                        pass
        except serial.SerialException:
            with lock: status['conn'] = False
            time.sleep(1)

threading.Thread(target=serial_reader, daemon=True).start()

# ── pygame init ───────────────────────────────────────────────
pygame.init()
W, H = 1024, 680
screen = pygame.display.set_mode((W, H))
pygame.display.set_caption("ICS-43434  //  MIC VISUALIZER")
clock  = pygame.time.Clock()

try:
    font_b = pygame.font.SysFont("Consolas", 13, bold=True)
    font_m = pygame.font.SysFont("Consolas", 11)
    font_s = pygame.font.SysFont("Consolas",  9)
except Exception:
    font_b = pygame.font.SysFont("Courier New", 13, bold=True)
    font_m = pygame.font.SysFont("Courier New", 11)
    font_s = pygame.font.SysFont("Courier New",  9)

_glow = pygame.Surface((W, H), pygame.SRCALPHA)

# pre-render scanlines
_scanlines = pygame.Surface((W, H), pygame.SRCALPHA)
for row in range(0, H, 3):
    pygame.draw.line(_scanlines, (0, 0, 0, 38), (0, row), (W, row))

# ── layout constants ──────────────────────────────────────────
PAD    = 14
BAR_H  = 38

N_SEGS  = 50            # LED segments per meter
SEG_H   = 8             # px tall per segment
SEG_GAP = 2             # px gap between segments
STEP    = SEG_H + SEG_GAP   # = 10 px per segment slot

METER_H = N_SEGS * STEP - SEG_GAP   # 498 px
METER_W = 88
METER_Y = PAD + 30
METER_LX = 70           # x of left  meter
METER_RX = 70 + METER_W + 30   # x of right meter

CHART_X = METER_RX + METER_W + 30
CHART_Y = METER_Y
CHART_W = W - CHART_X - PAD
CHART_H = METER_H

# ── peak hold state ───────────────────────────────────────────
PEAK_HOLD_FRAMES = 120   # ~2 s at 60 fps
PEAK_DECAY_DB    = 0.6   # dB per frame after hold expires

peak_state = {
    'L': {'db': DB_MIN, 'hold': 0},
    'R': {'db': DB_MIN, 'hold': 0},
}

def update_peak(ch, rms_db):
    ps = peak_state[ch]
    if rms_db >= ps['db']:
        ps['db']   = rms_db
        ps['hold'] = PEAK_HOLD_FRAMES
    else:
        if ps['hold'] > 0:
            ps['hold'] -= 1
        else:
            ps['db'] = max(ps['db'] - PEAK_DECAY_DB, DB_MIN)

# ── drawing helpers ───────────────────────────────────────────
def db_to_y(db, my=METER_Y, mh=METER_H):
    pct = (db - DB_MIN) / (DB_MAX - DB_MIN)
    pct = max(0.0, min(1.0, pct))
    return my + mh - int(pct * mh)

def draw_vu_meter(x, rms_db, peak_db, label, color):
    # background
    pygame.draw.rect(screen, PANEL, (x, METER_Y, METER_W, METER_H))

    # segments (drawn bottom to top)
    for seg in range(N_SEGS):
        seg_db  = DB_MIN + (seg / N_SEGS) * (DB_MAX - DB_MIN)
        sy      = METER_Y + METER_H - (seg + 1) * STEP + SEG_GAP
        active  = seg_db <= rms_db
        c       = seg_color(seg_db, active)
        pygame.draw.rect(screen, c, (x, sy, METER_W, SEG_H))

    # peak hold tick
    if peak_db > DB_MIN:
        pk_seg  = int((peak_db - DB_MIN) / (DB_MAX - DB_MIN) * N_SEGS)
        pk_seg  = max(0, min(N_SEGS - 1, pk_seg))
        pk_y    = METER_Y + METER_H - (pk_seg + 1) * STEP + SEG_GAP
        # draw bright tick in the channel colour
        _glow.fill((0,0,0,0))
        pygame.draw.rect(_glow, (*color, 255), (x, pk_y, METER_W, SEG_H))
        pygame.draw.rect(_glow, (*color,  80), (x, pk_y-2, METER_W, SEG_H+4))
        screen.blit(_glow, (0, 0))

    # border & label
    pygame.draw.rect(screen, BORDER, (x, METER_Y, METER_W, METER_H), 1)
    lbl = font_b.render(label, True, color)
    screen.blit(lbl, (x + METER_W // 2 - lbl.get_width() // 2, METER_Y + METER_H + 8))

    # current dB readout
    db_str = f"{rms_db:+.1f}"
    dbt = font_m.render(db_str, True, color)
    screen.blit(dbt, (x + METER_W // 2 - dbt.get_width() // 2, METER_Y + METER_H + 26))

def draw_db_scale():
    for db in (0, -3, -6, -9, -12, -18, -24, -36, -48, -60):
        ty = db_to_y(db)
        pygame.draw.line(screen, BORDER, (METER_LX - 10, ty), (METER_LX, ty))
        pygame.draw.line(screen, BORDER, (METER_RX + METER_W, ty), (METER_RX + METER_W + 10, ty))
        lbl = font_s.render(f"{db:+d}", True, DIM)
        screen.blit(lbl, (METER_LX - lbl.get_width() - 14, ty - 5))

def draw_history():
    pygame.draw.rect(screen, PANEL,  (CHART_X, CHART_Y, CHART_W, CHART_H))
    pygame.draw.rect(screen, BORDER, (CHART_X, CHART_Y, CHART_W, CHART_H), 1)

    # grid
    for db in (-12, -24, -36, -48):
        gy = db_to_y(db, CHART_Y, CHART_H)
        pygame.draw.line(screen, GRID, (CHART_X, gy), (CHART_X + CHART_W, gy))
        screen.blit(font_s.render(f"{db:+d}", True, GRID), (CHART_X + 4, gy - 10))

    # 0 dB reference line
    gy0 = db_to_y(0.0, CHART_Y, CHART_H)
    pygame.draw.line(screen, (50, 50, 80), (CHART_X, gy0), (CHART_X + CHART_W, gy0))

    screen.blit(font_b.render("// RMS HISTORY  dBFS", True, DIM), (CHART_X + 8, CHART_Y + 6))

    with lock:
        hl = list(hist_l)
        hr = list(hist_r)

    n = len(hl)
    if n < 2:
        return

    step = CHART_W / (n - 1)

    for history, color in ((hl, L_COL), (hr, R_COL)):
        for w, a in ((4, 25), (2, 90), (1, 230)):
            _glow.fill((0, 0, 0, 0), (CHART_X, CHART_Y, CHART_W, CHART_H))
            pts = []
            for i, db in enumerate(history):
                px = int(CHART_X + i * step)
                py = int(CHART_Y + CHART_H - max(0.0, min(1.0, (db - DB_MIN) / (DB_MAX - DB_MIN))) * CHART_H)
                pts.append((px, py))
            if len(pts) >= 2:
                pygame.draw.lines(_glow, (*color, a), False, pts, w)
            screen.blit(_glow, (0, 0))

    # legend
    for i, (lbl, col) in enumerate((("L", L_COL), ("R", R_COL))):
        lx = CHART_X + CHART_W - 80 + i * 40
        pygame.draw.line(screen, col, (lx, CHART_Y + 14), (lx + 14, CHART_Y + 14), 2)
        screen.blit(font_m.render(lbl, True, col), (lx + 17, CHART_Y + 7))

def bracket(x, y, w, h, color, s=12, t=2):
    for px, py, sx, sy in [(x,y,1,1),(x+w,y,-1,1),(x,y+h,1,-1),(x+w,y+h,-1,-1)]:
        pygame.draw.line(screen, color, (px, py), (px + sx*s, py), t)
        pygame.draw.line(screen, color, (px, py), (px, py + sy*s), t)

# ── main loop ─────────────────────────────────────────────────
running = True
while running:
    for ev in pygame.event.get():
        if ev.type == pygame.QUIT:
            running = False
        elif ev.type == pygame.KEYDOWN and ev.key == pygame.K_ESCAPE:
            running = False

    screen.fill(BG)

    # dot grid
    for bx in range(0, W, 40):
        for by in range(0, H - BAR_H, 40):
            pygame.draw.circle(screen, (12, 12, 30), (bx, by), 1)

    with lock:
        rms_l  = data['rms_l']
        rms_r  = data['rms_r']
        is_conn = status['conn']
        cur_hz  = status['hz']

    rms_l_db = to_db(rms_l)
    rms_r_db = to_db(rms_r)

    update_peak('L', rms_l_db)
    update_peak('R', rms_r_db)

    # title
    screen.blit(font_b.render("// ICS-43434  STEREO MIC  MONITOR", True, DIM), (PAD, PAD))

    draw_db_scale()
    draw_vu_meter(METER_LX, rms_l_db, peak_state['L']['db'], "L", L_COL)
    draw_vu_meter(METER_RX, rms_r_db, peak_state['R']['db'], "R", R_COL)
    bracket(METER_LX, METER_Y, METER_W, METER_H, BORDER)
    bracket(METER_RX, METER_Y, METER_W, METER_H, BORDER)

    draw_history()
    bracket(CHART_X, CHART_Y, CHART_W, CHART_H, BORDER)

    # scanlines
    screen.blit(_scanlines, (0, 0))

    # ── status bar ────────────────────────────────────────────
    bar_y = H - BAR_H
    pygame.draw.rect(screen, (6, 6, 18), (0, bar_y, W, BAR_H))
    pygame.draw.line(screen, BORDER,     (0, bar_y), (W, bar_y))

    dot = (0, 255, 100) if is_conn else (255, 50, 50)
    pygame.draw.circle(screen, dot, (16, bar_y + BAR_H // 2), 5)
    screen.blit(font_m.render(PORT if is_conn else "WAITING", True, dot), (28, bar_y + 13))
    screen.blit(font_m.render(f"{cur_hz:.0f} Hz", True, DIM), (140, bar_y + 13))

    for i, (txt, col) in enumerate([
        (f"L  rms {rms_l_db:+.1f} dBFS", L_COL),
        (f"R  rms {rms_r_db:+.1f} dBFS", R_COL),
        (f"L  peak {peak_state['L']['db']:+.1f}",  tuple(c//2 for c in L_COL)),
        (f"R  peak {peak_state['R']['db']:+.1f}",  tuple(c//2 for c in R_COL)),
    ]):
        screen.blit(font_m.render(txt, True, col), (240 + i * 190, bar_y + 13))

    screen.blit(font_m.render(f"{clock.get_fps():.0f} FPS", True, DIM), (W - 70, bar_y + 13))

    pygame.display.flip()
    clock.tick(60)

pygame.quit()
